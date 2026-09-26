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

// A `that` predicate needs no mode of its own: it is `fn` context, checked
// statically, and runs in full like any expression (AI17v2).
enum class EvalMode : uint8_t {
    RUNTIME,    // full language; effects are admitted by the ordinary walker
    CONST,      // pass-manager const folder: pure, fuel-bounded, no effects (AI16)
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
    Type**              binder_env;  // slot-indexed runtime type bindings
    uint16_t            binder_count;
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
    // bit i: `var` parameter slot i has a caller home or legacy entry, so a
    // replaced root reaches the caller at return (CW33)
    uint32_t            var_publish_mask;
    // bit i: this activation has share-marked its `var` parameter slot i (an
    // alias bind or a captured store value) -- the T0 twin of MIR's
    // compile-time `cow_marked`; a re-borrow of that slot prepares first
    uint32_t            var_marked_mask;
    InterpFrame*        caller;
    // the window the running `that` predicate reserved for the names it binds
    // itself (BINDING_STORAGE_PREDICATE); NULL outside a predicate
    uint64_t*           predicate_window;
    uint32_t            predicate_window_count;
    const AstNode*      cur;         // currently evaluating node (backtrace/step)
    // An `on` handler activation: it has no `fn` node, yet its body is
    // procedural (S12.1.3), so its blocks yield their last value (S2.5.3)
    // exactly as MIR's `in_proc` handler functions do.
    bool                proc_handler;
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
    uint64_t* index;           // `~key` — current index/key, when present
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
    // Per-activation links from immutable dotted-member AST identifiers to
    // this EvalContext's NamePool records. The shared AST never owns these.
    struct hashmap* static_member_names;
    // The current subscript owner for a nested `last` expression. This points
    // at a live frame slot and is restored when that subscript completes.
    uint64_t*    last_index_item;
    EvalMode     mode;
    // The unit being folded, set only by the const-fold pass. CONST-mode
    // evaluation needs it to resolve a const binding from its declarator: the
    // module slab is empty at compile time, so an identifier has no slot to
    // read (RC3.3). NULL in every other mode.
    struct Transpiler* const_owner;
    uint32_t     depth;          // remaining recursion budget
    uint32_t     depth_limit;
    uint64_t     node_count;     // evaluated nodes, for the measurement report
    bool         depth_exhausted;
    // CONST mode consumes this per-node budget.  A rejected call or an
    // exhausted budget is local to the attempt: the fold is declined, never
    // published from a partial result (AI16).
    uint32_t     mode_fuel;
    bool         mode_exhausted;
    bool         mode_rejected;
    // S2.5.5v2 void versus null: the list producer (for / `(…)` / block) that
    // sits directly in an item position and so finishes with the skip marker;
    // consumed once at the producer's entry (mirrors MirTranspiler).
    AstNode*     list_item_producer;
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
// Returns the execution-local P2 state for a definition. Non-cached scripts
// use the definition-owned cell; AST-cache instances use their Script overlay.
FnPromotionCell* interp_promotion_cell(Script* script, const AstFuncNode* fn);
// T27-6: NULL when supported, otherwise a short reason (an AST node kind
// name for a scanner refusal) for the pinned-function log line.
const char* interp_satellite_refusal(const AstFuncNode* fn);
// D8.1.1v9: a satellite image co-compiles the target's direct-callee cluster;
// each extra member's boxed entry is published to its T0 Function here.
bool interp_publish_satellite_member(Script* script, AstFuncNode* def, void* entry);
// Retire queued worker jobs before a Script or its cached execution shell can
// release the AST they read. Safe to call for scripts that never queued work.
void interp_satellite_request_cancel_script(Script* script);
void interp_satellite_cancel_script(Script* script);
// True when an imported binding has a planned T0 owner and a stable module
// slab slot. Satellite lowering uses this predicate before embedding that
// `{module_id, slot}` pair instead of linking a generated import symbol.
bool interp_satellite_import_supported(const Script* importer,
                                       const NameEntry* entry);
// Resolves a satellite import through the current Script overlay without
// publishing transient slot facts into the parser-owned binding.
bool interp_satellite_import_binding(const Script* importer,
                                     const NameEntry* entry,
                                     Script** out_owner, int* out_slot);

// Parser-owned imports stay immutable in cached AST templates. These helpers
// recover the fresh Script shell and generated strings for one execution.
Script* lambda_ast_overlay_import_script(const Script* importer,
                                         const AstImportNode* import_node);
const char* lambda_ast_overlay_string(Script* script, const char* text);

// The pure system functions CONST mode may call; the runtime repeats this gate
// as defense in depth when a future AST form reaches eval_call directly.
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
// T21-3b: the caller published CW33 `var` homes (borrowed dispatch mode)
Item interp_call_borrowed(Function* fn, const Item* args, int argc);
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

// Calls an initialized public module function from a native document loader.
// The module's own slab and T0 dispatch state remain authoritative; a module
// compiled by MIR Direct is entered through its boxed `_b` export (D7.2.2).
Item interp_call_module_export(Runtime* runtime, Script* module,
                               const char* export_name,
                               const Item* args, int argc);

// Publish `entry`, the boxed (`_b`) MIR entry of `def`, as `fn`'s native entry
// with its context ABI, procedure/function colour, and public return shape.
void lambda_function_publish_boxed_entry(Function* fn, const AstFuncNode* def,
                                         void* entry);

// Invokes a retained Lambda callback after its original T0 activation ended.
// The callback's defining module owns both its slab and interpreter state.
Item interp_call_runtime_function(Runtime* runtime, Function* function,
                                  const Item* args, int argc);
// True while this thread is executing inside a T0 interpreter activation.
// Reentrant native callbacks must reuse that state instead of opening a retained call.
bool interp_has_active_state(void);
// Runs a retained callback on the interpreter's bounded large-stack worker.
// The caller must keep callback arguments rooted until this synchronous call returns.
typedef void (*InterpLargeStackThreadHook)(void* opaque);
Item interp_call_runtime_function_large_stack(Runtime* runtime, Function* function,
                                              const Item* args, int argc,
                                              InterpLargeStackThreadHook worker_enter = nullptr,
                                              InterpLargeStackThreadHook worker_leave = nullptr,
                                              void* worker_context = nullptr);
typedef Item (*InterpLargeStackCall)(void* opaque);
// Transfers one Runtime context to the bounded interpreter worker, invokes the
// callback synchronously, then restores the context to the originating thread.
Item interp_run_on_large_stack(EvalContext* eval, InterpLargeStackCall call,
                               void* opaque);

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
