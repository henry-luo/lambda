#pragma once

// Tier-0 AST interpreter (D8.1.1v2, vibe/Lambda_Design_Ast_Interpreter.md).
//
// T0 walks the typed AST directly, manipulating boxed Items only (AI3) and
// calling the same C-ABI runtime helper library generated code calls. An
// activation is a C++ InterpFrame (control state, no Items) paired with a
// statically-sized window on the existing side-root stack plus a number-stack
// watermark (AI4/D5.1.1) — no fourth stack mechanism exists.
//
// Log prefixes: `interp:` (walker/runner) and `frame-plan:` (the plan pass).

#include "transpiler.hpp"
#include "side_stack.h"

// ---------------------------------------------------------------------------
// Tier selection
// ---------------------------------------------------------------------------

typedef enum LambdaTier {
    LAMBDA_TIER_JIT = 0,   // today's eager whole-module MIR Direct pipeline
    LAMBDA_TIER_INTERP,    // T0 only, never promote
    LAMBDA_TIER_AUTO,      // T0 + per-function promotion (P2; P0/P1 fall back to JIT)
} LambdaTier;

// Parsed once at startup from LAMBDA_TIER; unset or `jit` keeps the shipped
// path bit-for-bit. Safe to call before any parse.
LambdaTier lambda_tier_selected(void);
void lambda_tier_set(LambdaTier tier);
// Parses "auto" | "interp" | "jit"; returns false on an unrecognized value.
bool lambda_tier_parse(const char* text, LambdaTier* out);

// ---------------------------------------------------------------------------
// Evaluation modes and statement signals
// ---------------------------------------------------------------------------

enum class EvalMode : uint8_t {
    RUNTIME,   // full language; CONST and PREDICATE modes arrive in P3 (AI16/AI17)
};

// The only non-local mechanism for language control flow (AI14); longjmp stays
// fault-only. RETURNED / ERROR_SKIP payloads live in the frame's reserved
// signal slot, never in a C++ local that a GC could invalidate.
enum class EvalSignal : uint8_t {
    NORMAL,
    RETURNED,
    BROKE,
    CONTINUED,
    ERROR_SKIP,
    // Self-tail-call: the parameter slots already hold the next iteration's
    // arguments and interp_call should re-enter the body rather than recurse.
    TAIL_CALL,
};

// The same iteration ceiling lowering applies to its TCO loop, so a runaway
// tail recursion faults at the same point in either tier.
#define LAMBDA_INTERP_TCO_MAX_ITERATIONS LAMBDA_TCO_MAX_ITERATIONS

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------

struct InterpState;

// Slot window layout, matching FnFramePlan (ast-core.hpp):
//   [ 0 .. param_count )                          parameters
//   [ param_count .. param_count+local_count )    locals (block scopes flattened)
//   [ signal_index ]                              RETURNED / ERROR_SKIP payload
//   [ scratch_base .. total_slots )               operand scratch
struct InterpFrame {
    InterpState*        st;
    const AstFuncNode*  fn;          // NULL at module top level
    Script*             module;      // owner of const_list / type_list / slab
    const FnFramePlan*  plan;
    uint64_t*           slots;       // side-root window base (Item lanes)
    Item*               env;         // closure capture env (NULL when not a closure)
    uint32_t            env_count;
    uint32_t            slot_count;
    uint32_t            scratch_base;
    uint32_t            scratch_top; // debug-checked <= slot_count
    uint32_t            signal_index;
    // The pending statement signal for this activation (AI14). Its payload —
    // a RETURNED value or an ERROR_SKIP error — lives in slots[signal_index],
    // never in a C++ local that a collection could invalidate. Keeping the
    // signal on the frame rather than in every return type lets it propagate
    // through `eval_expr` unchanged, since a content block is an expression.
    EvalSignal          signal;
    InterpFrame*        caller;
    const AstNode*      cur;         // currently evaluating node (backtrace/step)
};

// True while a break/continue/return/error-skip is unwinding this activation:
// every construct that sequences children must stop as soon as it is set.
static inline bool interp_frame_pending(const InterpFrame* f) {
    return f->signal != EvalSignal::NORMAL;
}

struct InterpState {
    EvalContext* ctx;
    Runtime*     runtime;
    InterpFrame* top;
    EvalMode     mode;
    uint32_t     depth;          // remaining recursion budget
    uint32_t     depth_limit;
    uint64_t     node_count;     // evaluated nodes, for the measurement report
    bool         depth_exhausted;
};

// Per-run counters printed in the run summary; gates pin `fallback` to 0 on
// gated corpora (no silent caps — R4).
typedef struct InterpRunStats {
    uint64_t scripts_executed;
    uint64_t scripts_fallback;
    uint64_t scripts_excluded;
    uint64_t nodes_evaluated;
    double   exec_ms;
} InterpRunStats;

InterpRunStats* interp_run_stats(void);
void interp_run_stats_reset(void);

// ---------------------------------------------------------------------------
// Frame-plan pass (interp_plan.cpp)
// ---------------------------------------------------------------------------

// Assigns NameEntry slots/storage classes and static scratch depths onto every
// FnAnalysis plus the Script's own top-level plan. Idempotent per Script.
// Returns false only on an internal invariant failure.
bool interp_plan_script(Script* script);

// Whole-AST pre-scan: true when every reachable node kind is covered by the
// current walker. On false, `*reject` receives the first unsupported kind.
bool interp_scan_supported(Script* script, AstNodeType* reject);

// Human-readable node kind, for fallback diagnostics.
const char* interp_node_kind_name(AstNodeType kind);

// ---------------------------------------------------------------------------
// Entry points (interp.cpp)
// ---------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif
// Interpreted-callee arm of lambda_dynamic_call (AI7). `args` points at a span
// the caller has already rooted; the result is re-homed into the caller's
// number extent before this returns.
Item interp_call(Function* fn, const Item* args, int argc);
#ifdef __cplusplus
}
#endif

// Executes an already-loaded, already-planned Script under T0. Arms the
// EXECUTION_BOUNDARY recovery frame, opens the module top-level frame, and
// returns the script result (or a fault Item).
Item interp_run_script(Runner* runner, bool run_main);

// Creates a cold Function value for a definition site: entry_abi
// LAMBDA_INTERPRETED, ptr NULL, def = fn_node. Captures are snapshotted by
// value per D6.2.3.
Function* interp_make_closure(Script* module, const AstFuncNode* fn_node,
                              InterpFrame* creating_frame);
