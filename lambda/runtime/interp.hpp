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
    LAMBDA_TIER_AUTO,      // T0 + per-function satellite promotion (P2)
} LambdaTier;

// Parsed once at startup from LAMBDA_TIER; unset keeps the shipped `auto`
// policy, while `jit` explicitly selects eager whole-module compilation.
// Safe to call before any parse.
LambdaTier lambda_tier_selected(void);
void lambda_tier_set(LambdaTier tier);
// Parses "auto" | "interp" | "jit"; returns false on an unrecognized value.
bool lambda_tier_parse(const char* text, LambdaTier* out);

// ---------------------------------------------------------------------------
// Evaluation modes and statement signals
// ---------------------------------------------------------------------------

enum class EvalMode : uint8_t {
    RUNTIME,    // full language; effects are admitted by the ordinary walker
    CONST,      // pass-manager const folder: pure, fuel-bounded, no effects (AI16)
    PREDICATE,  // pure, fuel-bounded `that` evaluation (AI17)
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
    // self-tail-call: the parameter slots already hold the next iteration's
    // arguments and interp_call should re-enter the body rather than recurse.
    TAIL_CALL,
    // a direct self-tail call reached the T1 threshold. Its argument slots
    // hold the rooted source values for one ordinary boxed native entry.
    TAIL_CALL_JIT,
};

// The same iteration ceiling lowering applies to its TCO loop, so a runaway
// tail recursion faults at the same point in either tier.
#define LAMBDA_INTERP_TCO_MAX_ITERATIONS LAMBDA_TCO_MAX_ITERATIONS

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------

struct InterpState;

// Complete Lambda-AST child traversal used by interpreter-owned analyses.
// The core AstIndex visitor intentionally omits Lambda-only statement edges;
// callers that need whole-script facts must use this traversal instead.
typedef void (*InterpAstChildVisitor)(AstNode* child, void* ctx);
void interp_visit_children(AstNode* node, InterpAstChildVisitor visit, void* ctx);

// Slot window layout, matching FnFramePlan (ast-core.hpp):
//   [ 0 .. param_count )                          parameters
//   [ param_count .. param_count+local_count )    locals (block scopes flattened)
//   [ vargs_index ] (variadic functions only)     adapter-owned rest-list root
//   [ signal_index ]                              RETURNED / ERROR_SKIP payload
//   [ scratch_base .. total_slots )               operand scratch
struct InterpFrame {
    InterpState*        st;
    const AstFuncNode*  fn;          // NULL at module top level
    Script*             module;      // owner of const_list / type_list / slab
    const FnFramePlan*  plan;
    uint64_t*           slots;       // side-root window base (Item lanes)
    // the active Function is rooted beside the frame slots so a tail handoff
    // can publish and invoke it across satellite compilation (D8.1.1v5).
    uint64_t*           callable_slot;
    Item*               env;         // closure capture env (NULL when not a closure)
    uint32_t            env_count;
    const TypeMethod*   method;      // non-null for an interpreted bound object method
    uint64_t*           method_self; // separately rooted receiver slot for that method
    uint32_t            slot_count;
    uint32_t            scratch_base;
    uint32_t            scratch_top; // debug-checked <= slot_count
    uint32_t            vargs_index; // UINT32_MAX when this frame is not variadic
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

// Implicit contexts (AI15): `~`, `~#` and friends are slot-backed, innermost
// wins (S10.1.3). The values live in the pushing construct's own frame slots —
// so they are rooted like any other operand — and this stack only holds
// pointers to those slots, never Items of its own.
struct InterpContext {
    uint64_t* item;            // `~`  — current item
    uint64_t* index;           // `~#` — current index/key
    uint64_t* parent;          // parent occurrence of `~`, when present
    uint64_t* root;            // root occurrence of `~`, when present
    InterpContext* prev;
};

// A handler-local `^` is rooted by the frame slot that owns the caught
// completion.  The chain is interpreter control state only; it never owns an
// Item, so nested handlers can restore the enclosing binding without adding a
// second unrooted value stack (S7.6.1v4).
struct InterpErrorContext {
    uint64_t* error;
    InterpErrorContext* prev;
};

// A view activation overlays its state/handler parameters on the ordinary
// frame bindings. These entries remain rooted in the activation's RootSpan;
// the chain itself is interpreter control state only.
struct InterpViewBinding {
    NameEntry* entry;
    uint64_t* value;
    uint64_t* model;
    const char* template_ref;
    const char* state_name;
    bool is_state;
    InterpViewBinding* prev;
};

struct InterpState {
    EvalContext* ctx;
    Runtime*     runtime;
    InterpFrame* top;
    InterpContext* contexts;
    InterpErrorContext* errors;
    InterpViewBinding* view_bindings;
    // The current subscript owner for a nested `last` expression. This points
    // at a live frame slot and is restored when that subscript completes.
    uint64_t*    last_index_item;
    EvalMode     mode;
    uint32_t     depth;          // remaining recursion budget
    uint32_t     depth_limit;
    uint64_t     node_count;     // evaluated nodes, for the measurement report
    bool         depth_exhausted;
    // Restricted modes consume this per-node budget.  A rejected call or an
    // exhausted budget is local to the attempt: its caller receives false,
    // never a partial predicate result (AI16/AI17).
    uint32_t     mode_fuel;
    bool         mode_exhausted;
    bool         mode_rejected;
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

// P4 owns one Script for the lifetime of an interactive session. New inputs
// append only their own typed AST fragment and execute only that fragment.
typedef struct InterpReplSession {
    Runner runner;
    bool initialized;
    // True when the last eval REJECTED the fragment and restored the session
    // (parse/type failure), false when evaluation ran to completion — even if
    // it produced an error VALUE. Both return ItemError, but only the first is
    // a rollback: an error value is an ordinary result the REPL should print,
    // which is what the JIT REPL path does.
    bool last_input_rejected;
} InterpReplSession;

InterpRunStats* interp_run_stats(void);
void interp_run_stats_reset(void);

// ---------------------------------------------------------------------------
// Frame-plan pass (interp_plan.cpp)
// ---------------------------------------------------------------------------

// Assigns NameEntry slots/storage classes and static scratch depths onto every
// FnAnalysis plus the Script's own top-level plan. Idempotent per Script.
// Returns false only on an internal invariant failure.
bool interp_plan_script(Script* script);
// Extends a planned module with one REPL AST fragment. Existing slots and
// frame shapes remain immutable; only new module bindings receive new slots.
bool interp_plan_repl_fragment(Script* script, AstNode* fragment);

// Whole-AST pre-scan: true when every reachable node kind is covered by the
// current walker. On false, `*reject` receives the first unsupported kind.
bool interp_scan_supported(Script* script, AstNodeType* reject);

// True only for the currently implemented satellite boundary: task-backed
// procedures and synchronous functions without captures, nested definitions,
// or unsupported mutation. Module bindings are read from T0's shared slab; a
// rejected function remains T0 for semantic safety.
bool interp_satellite_supported(const AstFuncNode* fn);
// True when an imported binding has a planned T0 owner and a stable module
// slab slot. Satellite lowering uses this predicate before embedding that
// `{module_id, slot}` pair instead of linking a generated import symbol.
bool interp_satellite_import_supported(const NameEntry* entry);

// Conservative, effect-free `that` subset.  The caller must reject rather
// than execute a predicate outside this shape; runtime repeats the call gate
// as defense in depth when a future AST form reaches eval_call directly.
bool interp_predicate_supported(AstNode* predicate);
bool interp_eval_mode_allows_sys_func(EvalMode mode, const SysFuncInfo* info);
// System-call names are syntactic labels only: MIR lowers their values in
// source order. Pure rows therefore share T0's positional Item ABI; procedural
// rows remain excluded because a piped receiver has no COW write-back channel.
bool interp_named_sys_args_supported(const AstNode* callee);
// the P3 pass-manager entry evaluates pure literal subtrees in CONST mode and
// publish only immediate results into the indexed AST fact table.
bool interp_const_fold_script(Transpiler* tp);
// True for the native-word registry rows whose Item-level wrappers are shared
// with MIR's non-native bitwise lowering. The scanner and evaluator use one
// policy so an admitted native call cannot reach an unimplemented ABI arm.
bool interp_native_sys_item_supported(const SysFuncInfo* info);

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

// Apply() dispatches an interpreter-owned view through this bridge. Edit
// templates remain on the generated registry path; retained event callbacks
// borrow their document EvalContext when no script runner is active.
extern "C" Item interp_eval_view_template(Context* context, Script* module,
                                           AstViewNode* view, Item model);

// Event dispatch uses the same overlay for interpreted view handlers. The
// bridge is inert for generated MIR entries, which retain their existing ABI.
extern "C" Item interp_eval_view_handler(Context* context, Script* module,
                                          AstViewNode* view,
                                          AstEventHandler* handler,
                                          Item model, Item event);

// Called by the shared dynamic dispatcher immediately before invoking a T0
// function. On success it upgrades that Function in place to its published
// boxed MIR entry and returns true; on a miss/pinned definition it returns
// false and the dispatcher executes interp_call normally. T0 loop backedges
// mark the definition for promotion at a later entry; no frame is replaced
// while it is active (no OSR, D8.1.1v2 §5.1).
bool interp_promote_function_if_hot(Function* fn);

// Executes an already-loaded, already-planned Script under T0. Arms the
// EXECUTION_BOUNDARY recovery frame, opens the module top-level frame, and
// returns the script result (or a fault Item).
Item interp_run_script(Runner* runner, bool run_main);
// Executes one already planned P4 REPL fragment against the Script's existing
// persistent module slab; earlier top-level nodes are not re-run.
Item interp_run_repl_fragment(Runner* runner, AstNode* fragment);

bool interp_repl_session_init(InterpReplSession* session, Runtime* runtime);
void interp_repl_session_destroy(InterpReplSession* session);
Item interp_repl_session_eval(InterpReplSession* session, const char* source);

// Creates a cold Function value for a definition site: entry_abi
// LAMBDA_INTERPRETED, ptr NULL, def = fn_node. Captures are snapshotted by
// value per D6.2.3.
Function* interp_make_closure(Script* module, const AstFuncNode* fn_node,
                              InterpFrame* creating_frame);

// Binds an AST-defined object method to a receiver (S12.3.3v2). Returns NULL
// when the method has no AST definition or needs captures; callers then fall
// back to the compiled-entry binding.
Function* interp_bind_object_method(const struct TypeMethod* method, Item self);
