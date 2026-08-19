// Tier-0 AST walker (D8.1.1v2 / AI1–AI5, AI13–AI15).
//
// Boxed Items only, all semantics through the same C-ABI runtime helpers
// generated code calls (AI3). Every GC-visible intermediate lives in a frame
// slot on the side-root stack before any child evaluation or MAY_GC helper
// call and is re-read from that slot afterwards (D5.3.3): container *objects*
// are stable but their data buffers move under gc_compact_data, so no
// `items[]`/`data` pointer may ever be cached across an allocating call.

#include "interp.hpp"
#include "runtime-state.h"
#include "recovery_frame.h"
#include "heap_api.h"
#include "type_contract.hpp"
#include "../../lib/log.h"
#include "../../lib/url.h"
#include <stdlib.h>

extern "C" Item lambda_module_var_read_slot(void* module_state, uint32_t slot);

// ---------------------------------------------------------------------------
// Tier selection
// ---------------------------------------------------------------------------

static LambdaTier g_lambda_tier = LAMBDA_TIER_JIT;

LambdaTier lambda_tier_selected(void) { return g_lambda_tier; }
void lambda_tier_set(LambdaTier tier) { g_lambda_tier = tier; }

bool lambda_tier_parse(const char* text, LambdaTier* out) {
    if (!text || !out) return false;
    if (strcmp(text, "jit") == 0)    { *out = LAMBDA_TIER_JIT;    return true; }
    if (strcmp(text, "interp") == 0) { *out = LAMBDA_TIER_INTERP; return true; }
    if (strcmp(text, "auto") == 0)   { *out = LAMBDA_TIER_AUTO;   return true; }
    return false;
}

static InterpRunStats g_interp_stats = {0};
InterpRunStats* interp_run_stats(void) { return &g_interp_stats; }
void interp_run_stats_reset(void) { memset(&g_interp_stats, 0, sizeof(g_interp_stats)); }

#define INTERP_DEFAULT_DEPTH 10000
#define INTERP_DEFAULT_PREDICATE_FUEL 1024
#define INTERP_DEFAULT_CONST_FUEL 1024
#define INTERP_CONST_FRAME_SLOTS 4096

// A predicate is a bounded decision, not an unbounded second execution path.
// Invalid knob values keep the reviewed default rather than weakening the
// no-effect boundary through an accidental unlimited budget (AI17).
static uint32_t interp_predicate_fuel_budget(void) {
    const char* env = getenv("LAMBDA_PREDICATE_FUEL");
    if (!env || !*env) return INTERP_DEFAULT_PREDICATE_FUEL;
    long value = strtol(env, NULL, 10);
    if (value < 1 || value > 1000000L) return INTERP_DEFAULT_PREDICATE_FUEL;
    return (uint32_t)value;
}

// a constant fold is a bounded compiler attempt, not a second unbounded
// evaluator. Invalid knobs retain the reviewed default just as predicates do.
static uint32_t interp_const_fuel_budget(void) {
    const char* env = getenv("LAMBDA_CONST_FUEL");
    if (!env || !*env) return INTERP_DEFAULT_CONST_FUEL;
    long value = strtol(env, NULL, 10);
    if (value < 1 || value > 1000000L) return INTERP_DEFAULT_CONST_FUEL;
    return (uint32_t)value;
}

static bool interp_const_fold_enabled(void) {
    const char* env = getenv("LAMBDA_CONST_FOLD");
    return !env || env[0] != '0' || env[1] != '\0';
}

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------

// RAII so no EvalSignal path can skip the close: the root window and the
// number watermark are released together, strictly LIFO. A fault landing
// bypasses this by design — LambdaRecoveryCheckpoint restores both watermarks
// and abandons every interpreter frame above the landing wholesale, which is
// correct because frames own no other resource.
class InterpFrameGuard {
    InterpFrame frame_;
    LambdaRootFrame roots_;
    LambdaSideStackSnapshot mark_;
    bool ok_;

public:
    InterpFrameGuard(InterpState* st, const AstFuncNode* fn, Script* module,
                     const FnFramePlan* plan, Item* env, uint32_t env_count)
            : frame_{}, roots_{}, mark_{}, ok_(false) {
        mark_ = lambda_side_stack_snapshot();
        size_t slots = plan && plan->planned ? plan->total_slots : 1;
        if (!lambda_root_frame_begin(&roots_, slots)) {
            // Fail closed through the armed recovery point rather than run
            // with non-rooting slots.
            lambda_root_frame_overflow_error();
            return;
        }
        frame_.st = st;
        frame_.fn = fn;
        frame_.module = module;
        frame_.plan = plan;
        frame_.slots = roots_.slots;
        frame_.env = env;
        frame_.env_count = env_count;
        frame_.slot_count = (uint32_t)slots;
        uint32_t named = plan ? (uint32_t)plan->param_count + plan->local_count : 0;
        frame_.vargs_index = plan && plan->vargs_index != UINT16_MAX
            ? plan->vargs_index : UINT32_MAX;
        frame_.signal_index = named + (frame_.vargs_index != UINT32_MAX ? 1 : 0);
        frame_.scratch_base = frame_.signal_index + 1;
        frame_.scratch_top = frame_.scratch_base;
        if (frame_.vargs_index != UINT32_MAX) {
            frame_.slots[frame_.vargs_index] = ITEM_NULL;
        }
        frame_.slots[frame_.signal_index] = ITEM_NULL;   // zero before publish
        frame_.signal = EvalSignal::NORMAL;
        frame_.caller = st->top;
        st->top = &frame_;
        ok_ = true;
    }

    ~InterpFrameGuard() {
        if (!ok_) return;
        frame_.st->top = frame_.caller;
        lambda_root_frame_end(&roots_);
        // The frame's number-stack extent dies with it; any wide scalar that
        // must outlive it has already been re-homed into the caller's extent.
        lambda_side_stack_restore(mark_);
    }

    bool valid() const { return ok_; }
    InterpFrame* frame() { return &frame_; }

    InterpFrameGuard(const InterpFrameGuard&) = delete;
    InterpFrameGuard& operator=(const InterpFrameGuard&) = delete;
};

// The dynamic-call adapter materializes a variadic rest list before it enters
// T0. Keep that List rooted in the callee frame while `current_vargs` exposes
// it to `varg()`: Context itself is not a GC root (D5.1.1).
static bool interp_set_frame_vargs(InterpFrame* frame, Item rest) {
    if (!frame || frame->vargs_index == UINT32_MAX ||
            frame->vargs_index >= frame->slot_count) {
        log_error("interp: variadic frame has no rest-list root");
        return false;
    }
    TypeId tid = get_type_id(rest);
    if (rest.item != ITEM_NULL && tid != LMD_TYPE_ARRAY) {
        log_error("interp: variadic rest argument is not a list (type %d)",
            (int)tid);
        return false;
    }
    if (rest.item == ITEM_NULL) {
        // The shared adapter uses null as its no-rest transport sentinel, but
        // a Lambda `varg()` observes an empty Array. Materialize it in this
        // rooted frame slot before any body allocation can run.
        List* empty = list();
        if (!empty) return false;
        rest = (Item){.item = (uint64_t)(uintptr_t)empty};
    }
    frame->slots[frame->vargs_index] = rest.item;
    (void)set_vargs((List*)(uintptr_t)rest.item);
    return true;
}

class InterpVargsGuard {
    List* previous_;
    bool active_;

public:
    InterpVargsGuard(InterpFrame* frame, Item rest) : previous_(NULL), active_(false) {
        if (!frame || frame->vargs_index == UINT32_MAX) return;
        previous_ = frame->st && frame->st->ctx ? frame->st->ctx->current_vargs : NULL;
        if (!interp_set_frame_vargs(frame, rest)) return;
        active_ = true;
    }

    ~InterpVargsGuard() {
        if (active_) restore_vargs(previous_);
    }

    bool valid() const { return active_; }
    InterpVargsGuard(const InterpVargsGuard&) = delete;
    InterpVargsGuard& operator=(const InterpVargsGuard&) = delete;
};

// Saves the ordinary runtime mode around an isolated `that` attempt.  Nested
// constrained checks retain the enclosing fuel counter, so a predicate cannot
// manufacture a fresh budget by spelling `~ is OtherConstrainedType` (AI17).
class InterpEvalModeGuard {
    InterpState* st_;
    EvalMode saved_mode_;
    uint32_t saved_fuel_;
    bool saved_exhausted_;
    bool saved_rejected_;
    bool owns_mode_;
public:
    InterpEvalModeGuard(InterpState* st, EvalMode mode, uint32_t fuel)
            : st_(st), saved_mode_(st->mode), saved_fuel_(st->mode_fuel),
              saved_exhausted_(st->mode_exhausted),
              saved_rejected_(st->mode_rejected),
              owns_mode_(st->mode == EvalMode::RUNTIME) {
        if (!owns_mode_) return;
        st_->mode = mode;
        st_->mode_fuel = fuel;
        st_->mode_exhausted = false;
        st_->mode_rejected = false;
    }
    ~InterpEvalModeGuard() {
        if (!owns_mode_) return;
        st_->mode = saved_mode_;
        st_->mode_fuel = saved_fuel_;
        st_->mode_exhausted = saved_exhausted_;
        st_->mode_rejected = saved_rejected_;
    }
    bool completed() const {
        return !st_->mode_exhausted && !st_->mode_rejected;
    }
    InterpEvalModeGuard(const InterpEvalModeGuard&) = delete;
    InterpEvalModeGuard& operator=(const InterpEvalModeGuard&) = delete;
};

// One frame-relative Item home, held across a child eval or a MAY_GC call.
class Scratch {
    InterpFrame* f_;
    uint32_t index_;
    uint64_t fallback_;

public:
    explicit Scratch(InterpFrame* f) : f_(f), index_(0), fallback_(ITEM_NULL) {
        if (f_->scratch_top < f_->slot_count) {
            index_ = f_->scratch_top++;
            f_->slots[index_] = ITEM_NULL;   // zero before publish
        } else {
            // A plan undercount is a pass bug, never a growth path. Log once
            // per site and degrade to an unrooted local rather than scribbling
            // past the window; the debug assert below catches it in test runs.
            log_error("interp: scratch overflow depth=%u cap=%u fn=%s",
                f_->scratch_top, f_->slot_count,
                f_->fn && f_->fn->name ? f_->fn->name->chars : "<top>");
            index_ = UINT32_MAX;
        }
    }

    ~Scratch() { if (index_ != UINT32_MAX) f_->scratch_top--; }

    void set(Item v) { *home() = v.item; }
    Item get() const {
        return (Item){.item = index_ == UINT32_MAX ? fallback_ : f_->slots[index_]};
    }
    uint64_t* home() { return index_ == UINT32_MAX ? &fallback_ : &f_->slots[index_]; }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
};

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static Item eval_expr(InterpFrame* f, AstNode* node);
static Item eval_content(InterpFrame* f, AstListNode* list_node, bool hoist_functions);
static Item eval_list(InterpFrame* f, AstListNode* list_node);
static Item eval_for(InterpFrame* f, AstForNode* for_node, bool result_demanded);
static void exec_declaration(InterpFrame* f, AstNode* node);
static void interp_note_backedge(InterpFrame* frame);
static bool interp_eval_constrained_predicate(InterpFrame* f,
    AstConstrainedTypeNode* constrained, Scratch& subject);

// Raises a statement signal, parking its payload in the frame's reserved slot.
static inline void interp_signal(InterpFrame* f, EvalSignal signal, Item payload) {
    f->signal = signal;
    f->slots[f->signal_index] = payload.item;
}

static inline Item interp_signal_payload(InterpFrame* f) {
    return (Item){.item = f->slots[f->signal_index]};
}

static bool interp_is_statement_handler(AstNode* node) {
    AstNode* effective = ast_unwrap_primary(node);
    return effective && effective->node_type == AST_NODE_HANDLER_STAM;
}

static bool interp_propagate_handler_error(InterpFrame* f, AstNode* node,
        Item value) {
    if (!interp_is_statement_handler(node) || !item_is_error(value)) return false;
    // Primary wrappers are transparent in the AST but content lowering may
    // classify the wrapper as a value item; preserve the handler arm's fresh
    // error instead of silently discarding it as a non-final proc expression.
    interp_signal(f, EvalSignal::RETURNED, value);
    return true;
}

// Consumes a loop-scoped signal. `break` and `continue` stop at the nearest
// enclosing loop; `return` and `error-skip` keep unwinding past it.
static inline void interp_clear_loop_signal(InterpFrame* f) {
    if (f->signal == EvalSignal::BROKE || f->signal == EvalSignal::CONTINUED) {
        f->signal = EvalSignal::NORMAL;
        f->slots[f->signal_index] = ITEM_NULL;
    }
}

// Direct-pointer Items: the object's own header byte already carries its
// TypeId, so no high-byte tag is added (the canonical carrier fn_is/fn_query
// expect).
static inline Item interp_ptr_item(void* ptr) {
    return ptr ? (Item){.item = (uint64_t)(uintptr_t)ptr} : ItemNull;
}

// ---------------------------------------------------------------------------
// Bindings
// ---------------------------------------------------------------------------

// Post-order import cone excluding the root — the same shape run_script_mir
// collects, rebuilt here because that helper is static to the MIR translation
// unit and its cone is only populated on the lowering path.
static void interp_cone_postorder(Script* script, ArrayList* cone, ArrayList* seen) {
    if (!script) return;
    for (int i = 0; i < seen->length; i++) if (seen->data[i] == script) return;
    arraylist_append(seen, script);
    if (script->direct_imports) {
        for (int i = 0; i < script->direct_imports->length; i++) {
            interp_cone_postorder((Script*)script->direct_imports->data[i], cone, seen);
        }
    }
    arraylist_append(cone, script);
}

static ArrayList* interp_collect_import_cone(Script* main_script) {
    if (!main_script || !main_script->direct_imports ||
            main_script->direct_imports->length == 0) return NULL;
    ArrayList* with_main = arraylist_new(8);
    ArrayList* seen = arraylist_new(8);
    interp_cone_postorder(main_script, with_main, seen);
    arraylist_free(seen);
    ArrayList* cone = arraylist_new(with_main->length);
    for (int i = 0; i < with_main->length; i++) {
        if (with_main->data[i] != main_script) arraylist_append(cone, with_main->data[i]);
    }
    arraylist_free(with_main);
    return cone;
}

static LambdaModuleState* interp_module_state(Script* module) {
    EvalContext* owner = context;
    if (!owner || !module) return NULL;
    uint32_t id = module->module_state_id;
    if (id >= owner->module_state_capacity || !owner->module_states) return NULL;
    return owner->module_states[id];
}

static Item interp_read_module_slot(Script* module, int32_t slot) {
    LambdaModuleState* state = interp_module_state(module);
    if (!state || slot < 0 || (uint32_t)slot >= state->var_count) return ItemNull;
    // Read the stored Item verbatim, as generated code's slab load does. The
    // wide-scalar payload lives in the module state's own var_payloads array,
    // which outlives every reader, so re-homing is unnecessary — and lossy:
    // it would collapse a small u64 back into the int lane and change type().
    return state->vars[slot];
}

static void interp_write_module_slot(Script* module, int32_t slot, Item value) {
    LambdaModuleState* state = interp_module_state(module);
    if (!state || slot < 0 || (uint32_t)slot >= state->var_count) return;
    lambda_module_var_store(state, (uint32_t)slot, value);
}

// Captures are a by-value snapshot taken at closure creation (D6.2.3), so an
// enclosing name is either this frame's own slot or an env index — never a
// live cell. Stage 1 has no mutable upvalues (S9.1.4 via D6.2.3).
static int interp_capture_index(const AstFuncNode* fn, const NameEntry* entry) {
    if (!fn) return -1;
    int index = 0;
    for (FnCapture* cap = fn->captures; cap; cap = cap->next, index++) {
        if (cap->entry == entry) return index;
    }
    return -1;
}

static Item interp_read_binding(InterpFrame* f, NameEntry* entry) {
    if (!entry) return ItemNull;
    if (!entry->storage_assigned) {
        log_error("interp: identifier '%.*s' has no frame-plan storage",
            entry->name ? (int)entry->name->len : 0,
            entry->name ? entry->name->chars : "");
        return ItemError;
    }
    if (entry->binding_storage == BINDING_STORAGE_MODULE) {
        // Cross-module reads resolve against the *declaring* Script (§4.1):
        // the two modules number their slabs independently, so an imported
        // name's slot indexes its owner's slab, not this frame's.
        Script* owner = entry->import_owner ? entry->import_owner : f->module;
        return interp_read_module_slot(owner, entry->slot);
    }
    int cap = interp_capture_index(f->fn, entry);
    if (cap >= 0) {
        if (!f->env || (uint32_t)cap >= f->env_count) return ItemNull;
        // Capture storage is owned by the closure and outlives every frame that
        // reads it, so the Item is taken as-is (immortal) rather than re-homed.
        return owned_item_slot_read(f->env, f->env_count, cap, true);
    }
    if ((uint32_t)entry->slot >= f->scratch_base) {
        log_error("interp: binding slot %d out of window (%u) for '%.*s'",
            entry->slot, f->scratch_base,
            entry->name ? (int)entry->name->len : 0,
            entry->name ? entry->name->chars : "");
        return ItemError;
    }
    return (Item){.item = f->slots[entry->slot]};
}

static void interp_write_binding(InterpFrame* f, NameEntry* entry, Item value) {
    if (!entry || !entry->storage_assigned) return;
    if (entry->binding_storage == BINDING_STORAGE_MODULE) {
        // An imported binding is read-only here: its owner initializes it.
        Script* owner = entry->import_owner ? entry->import_owner : f->module;
        interp_write_module_slot(owner, entry->slot, value);
        return;
    }
    if ((uint32_t)entry->slot < f->scratch_base) f->slots[entry->slot] = value.item;
}

// A declared compact numeric lane has a canonical Item representation even
// when static checking already accepted an ordinary numeric source. Root the
// source while the shared coercion helper can allocate, or a u64 result may
// outlive the caller's unrooted local across its number-home allocation.
static Item interp_coerce_declared_numeric(InterpFrame* f, Item value,
        Type* declared_type) {
    if (!f || item_is_error(value)) return value;
    Type* target = unwrap_simple_type_type(declared_type);
    if (!target || (target->type_id != LMD_TYPE_NUM_SIZED &&
            target->type_id != LMD_TYPE_UINT64)) return value;
    Scratch source_root(f);
    source_root.set(value);
    if (target->type_id == LMD_TYPE_NUM_SIZED) {
        return coerce_num_sized(source_root.get(),
            (int64_t)type_num_sized_kind(target));
    }
    return coerce_uint64(source_root.get());
}

static Item interp_eval_cow_path_key(InterpFrame* f, AstNode* key_node,
        bool is_member) {
    AstNode* key = ast_unwrap_primary(key_node);
    if (is_member && key && key->node_type == AST_NODE_IDENT) {
        // a dotted segment is a literal property key, never a binding read.
        AstIdentNode* name = (AstIdentNode*)key;
        return (Item){.item = s2it(heap_create_name(name->name->chars,
            name->name->len))};
    }
    return eval_expr(f, key_node);
}

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

static void* interp_const_at(Script* module, int index) {
    if (!module || !module->const_list || index < 0 ||
            index >= module->const_list->length) return NULL;
    return module->const_list->data[index];
}

// Mirrors the literal arm of transpile_box_item: the value lives in the node's
// Type*, resolved against the *owning* Script's const_list, not on the node.
static Item eval_literal(InterpFrame* f, AstNode* node) {
    Type* type = node->type;
    TypeId tid = type->type_id;
    TypeConst* tc = (TypeConst*)type;
    switch (tid) {
    case LMD_TYPE_NULL:
        return ItemNull;
    case LMD_TYPE_BOOL:
        return (Item){.item = b2it(parse_bool_literal(f->module->source, node->node)
            ? BOOL_TRUE : BOOL_FALSE)};
    case LMD_TYPE_INT: {
        if (type == &LIT_INT || ts_node_symbol(node->node) == SYM_INT) {
            return (Item){.item = i2it(parse_int_literal(f->module->source, node->node))};
        }
        const int64_t* slot = (const int64_t*)interp_const_at(f->module, tc->const_index);
        return (Item){.item = i2it(slot ? *slot : 0)};
    }
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64: {
        // Literal floats own a stable TypeFloat slot; encode the canonical
        // Item bits directly instead of re-tagging the const pointer.
        TypeFloat* tf = (TypeFloat*)type;
        return lambda_float_ptr_to_item(&tf->double_val);
    }
    case LMD_TYPE_COMPLEX: {
        TypeComplex* tcx = (TypeComplex*)type;
        return complex_new(tcx->real, tcx->imag);
    }
    case LMD_TYPE_INT64: {
        const int64_t* slot = (const int64_t*)interp_const_at(f->module, tc->const_index);
        return slot ? (Item){.item = l2it(slot)} : ItemNull;
    }
    case LMD_TYPE_UINT64: {
        const uint64_t* slot = (const uint64_t*)interp_const_at(f->module, tc->const_index);
        return slot ? (Item){.item = u2it(slot)} : ItemNull;
    }
    case LMD_TYPE_STRING: {
        void* ptr = interp_const_at(f->module, tc->const_index);
        return ptr ? (Item){.item = s2it(ptr)} : ItemNull;
    }
    case LMD_TYPE_SYMBOL: {
        void* ptr = interp_const_at(f->module, tc->const_index);
        return ptr ? (Item){.item = y2it(ptr)} : ItemNull;
    }
    case LMD_TYPE_BINARY: {
        void* ptr = interp_const_at(f->module, tc->const_index);
        return ptr ? (Item){.item = x2it(ptr)} : ItemNull;
    }
    case LMD_TYPE_DECIMAL: {
        void* ptr = interp_const_at(f->module, tc->const_index);
        return ptr ? (Item){.item = c2it(ptr)} : ItemNull;
    }
    case LMD_TYPE_DTIME: {
        const DateTime* ptr = (const DateTime*)interp_const_at(f->module, tc->const_index);
        return ptr ? push_k(*ptr) : ItemNull;
    }
    case LMD_TYPE_NUM_SIZED: {
        TypeNumSized* ns = (TypeNumSized*)type;
        return (Item){.item = NUM_SIZED_PACK(ns->num_type, ns->raw_bits)};
    }
    default:
        log_error("interp: unhandled literal type %d", (int)tid);
        return ItemError;
    }
}

// ---------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------

// Operand order and helper selection mirror the boxed arms of
// transpile_binary/transpile_unary; the numeric tower, meets and int totality
// (S4.4, SI7) all live inside the helpers, so nothing is re-decided here.
static Item eval_unary(InterpFrame* f, AstUnaryNode* node) {
    if (node->op == OPERATOR_SPREAD) return eval_expr(f, node->operand);
    // Publish before the call: the helper allocates, and an operand living only
    // in a C++ local would not be reachable from the collector (D5.3.3).
    Item operand_value = eval_expr(f, node->operand);
    Scratch operand_slot(f);
    operand_slot.set(operand_value);
    Item operand = operand_slot.get();
    switch (node->op) {
    case OPERATOR_NOT:      return (Item){.item = b2it(fn_not(operand))};
    case OPERATOR_NEG:      return fn_neg(operand);
    case OPERATOR_POS:      return fn_pos(operand);
    case OPERATOR_PROPAGATE:
        if (item_is_error(operand) && !interp_frame_pending(f)) {
            interp_signal(f, EvalSignal::RETURNED, operand);
        }
        return operand;
    default:
        log_error("interp: unhandled unary operator %d", (int)node->op);
        return ItemError;
    }
}

static Item eval_binary(InterpFrame* f, AstBinaryNode* node) {
    // `and`/`or` short-circuit before the right operand is evaluated (S10.2.3);
    // truthiness is by tag through the shipped helper (S3.1/S3.2).
    if (node->op == OPERATOR_AND || node->op == OPERATOR_OR) {
        // Short-circuit, then let the helper decide (S10.2.3). The early exit
        // is only taken where fn_and/fn_or would return the left operand
        // anyway, so containment stays the helper's call: an error is *falsy*
        // (is_truthy), which is what makes `int("x") or 7` yield 7 rather than
        // propagating — checking for an error here would break that.
        Item left_value = eval_expr(f, node->left);
        if (interp_frame_pending(f)) return left_value;
        Scratch lhs(f);
        lhs.set(left_value);
        Bool truth = is_truthy(lhs.get());
        if (node->op == OPERATOR_AND ? truth == BOOL_FALSE : truth == BOOL_TRUE) {
            return lhs.get();
        }
        Item right_value = eval_expr(f, node->right);
        if (interp_frame_pending(f)) return right_value;
        Scratch rhs(f);
        rhs.set(right_value);
        return node->op == OPERATOR_AND ? fn_and(lhs.get(), rhs.get())
                                        : fn_or(lhs.get(), rhs.get());
    }

    // Whole-module lowering already emits constrained `is` as a base test
    // followed by the AST predicate.  Do not route it through fn_is: that
    // helper intentionally retains S11.4.6's base-only generic behavior.
    AstConstrainedTypeNode* constrained = node->op == OPERATOR_IS
        ? ast_constrained_type_node(node->right) : NULL;
    if (constrained) {
        Item left_value = eval_expr(f, node->left);
        if (interp_frame_pending(f)) return left_value;
        Scratch lhs(f);
        lhs.set(left_value);
        TypeConstrained* type = (TypeConstrained*)constrained->type;
        if (!type || !type->base || get_type_id(lhs.get()) != type->base->type_id) {
            return (Item){.item = b2it(BOOL_FALSE)};
        }
        return (Item){.item = b2it(interp_eval_constrained_predicate(f, constrained, lhs)
            ? BOOL_TRUE : BOOL_FALSE)};
    }

    // The slot is taken *after* the left operand is evaluated: holding it
    // across that recursion would make scratch use proportional to nesting
    // depth, while the plan's cost model is max(need(a), 1 + need(b)).
    // Nothing between the eval and the store can collect.
    Item left_value = eval_expr(f, node->left);
    Scratch lhs(f);
    lhs.set(left_value);
    Item right_value = eval_expr(f, node->right);  // may collect; lhs is rooted
    Scratch rhs(f);
    rhs.set(right_value);
    Item left = lhs.get();
    Item right = rhs.get();
    // Keyword comparisons are the only vectorized comparison syntax; symbolic
    // < <= > >= stay scalar so masks are never implicit truth values.
    if (node->op >= OPERATOR_ELEM_EQ && node->op <= OPERATOR_ELEM_GE) {
        return vec_cmp(left, right, (int)(node->op - OPERATOR_ELEM_EQ));
    }
    switch (node->op) {
    case OPERATOR_ADD:       return fn_add(left, right);
    case OPERATOR_SUB:       return fn_sub(left, right);
    case OPERATOR_MUL:       return fn_mul(left, right);
    case OPERATOR_DIV:       return fn_div(left, right);
    case OPERATOR_IDIV:      return fn_idiv(left, right);
    case OPERATOR_MOD:       return fn_mod(left, right);
    case OPERATOR_POW:       return fn_pow(left, right);
    case OPERATOR_JOIN:      return fn_join(left, right);
    case OPERATOR_EQ:        return (Item){.item = b2it(fn_eq(left, right))};
    case OPERATOR_NE:        return (Item){.item = b2it(fn_ne(left, right))};
    case OPERATOR_LT:        return fn_lt(left, right);
    case OPERATOR_LE:        return fn_le(left, right);
    case OPERATOR_GT:        return fn_gt(left, right);
    case OPERATOR_GE:        return fn_ge(left, right);
    case OPERATOR_TO:        return fn_to(left, right);
    case OPERATOR_UNION:     return fn_union(left, right);
    case OPERATOR_IS:        return (Item){.item = b2it(fn_is(left, right))};
    // `expr is nan` lowers to a one-operand fn_is_nan call (transpile-mir.cpp);
    // the parsed `nan` on the right is a marker, not a compared value.
    case OPERATOR_IS_NAN:    return (Item){.item = b2it(fn_is_nan(left))};
    case OPERATOR_IN:        return (Item){.item = b2it(fn_in(left, right))};
    case OPERATOR_AT:        return (Item){.item = b2it(fn_at(left, right))};
    default:
        log_error("interp: unhandled binary operator %d", (int)node->op);
        return ItemError;
    }
}

// ---------------------------------------------------------------------------
// Calls
// ---------------------------------------------------------------------------

static Item interp_rejected_parameter_error(const TypeFunc* signature,
        const Item* args, int argc);
static Item interp_call_with_borrowed(Function* fn, const Item* args, int argc,
        InterpFrame* caller, NameEntry* const* borrowed_entries);

bool interp_native_sys_item_supported(const SysFuncInfo* info) {
    if (!info || info->c_arg_conv != C_ARG_NATIVE) return false;
    switch (info->fn) {
    case SYSFUNC_BAND:
    case SYSFUNC_BOR:
    case SYSFUNC_BXOR:
    case SYSFUNC_SHL:
    case SYSFUNC_SHR:
        return info->arg_count == 2;
    case SYSFUNC_BNOT:
        return info->arg_count == 1;
    default:
        return false;
    }
}

// MIR keeps an all-`int` bitwise call in a machine lane, but routes every
// other lane through these Item helpers. The raw registry ABI cannot preserve
// a sized or bigint result, so keep that split at the interpreter boundary.
static Item eval_native_sys_item_call(const SysFuncInfo* info, const Item* args,
        int argc, Type* result_type) {
    if (!interp_native_sys_item_supported(info) || !args || argc != info->arg_count) {
        log_error("interp: unsupported native system function call");
        return ItemError;
    }

    bool int_result = result_type && result_type->type_id == LMD_TYPE_INT;
    switch (info->fn) {
    case SYSFUNC_BAND:
        return int_result ? int2it_i64(fn_band(_barg(args[0]), _barg(args[1])))
            : fn_band_item(args[0], args[1]);
    case SYSFUNC_BOR:
        return int_result ? int2it_i64(fn_bor(_barg(args[0]), _barg(args[1])))
            : fn_bor_item(args[0], args[1]);
    case SYSFUNC_BXOR:
        return int_result ? int2it_i64(fn_bxor(_barg(args[0]), _barg(args[1])))
            : fn_bxor_item(args[0], args[1]);
    case SYSFUNC_BNOT:
        return int_result ? int2it_i64(fn_bnot(_barg(args[0]))) : fn_bnot_item(args[0]);
    case SYSFUNC_SHL:
        return int_result ? int2it_i64_or_error(fn_shl(_barg(args[0]), _barg(args[1])))
            : fn_shl_item(args[0], args[1]);
    case SYSFUNC_SHR:
        return int_result ? int2it_i64_or_error(fn_shr(_barg(args[0]), _barg(args[1])))
            : fn_shr_item(args[0], args[1]);
    default:
        return ItemError;
    }
}

// Direct C call through the registry entry, using the same result boxing MIR
// lowering selects from sysfunc_c_ret_type_id. Same registry, both tiers.
static Item eval_sys_call(InterpFrame* f, SysFuncInfo* info, const Item* args,
        int argc, Type* result_type) {
    if (!info || !info->func_ptr) {
        log_error("interp: system function '%s' has no entry point",
            info && info->name ? info->name : "<null>");
        return ItemError;
    }
    void* fp = (void*)info->func_ptr;
    TypeId c_ret = sysfunc_c_ret_type_id(info);

    if (sysfunc_params_reject_error(info)) {
        for (int i = 0; i < argc; i++) {
            if (item_is_error(args[i])) {
                // MIR returns a rejected system-call error from this activation
                // before a local handler can turn it into ordinary content.
                interp_signal(f, EvalSignal::RETURNED, args[i]);
                return args[i];
            }
        }
    }

    if (info->c_arg_conv == C_ARG_NATIVE) {
        return eval_native_sys_item_call(info, args, argc, result_type);
    }

    if (info->c_ret_type == C_RET_RETITEM) {
        // These entries return the 16-byte RetItem, not an Item. Calling one
        // through an Item-returning prototype is undefined and drops `.err`
        // silently; the registry stores the raw function, and lowering reaches
        // it through an `_mir` wrapper that does exactly this mapping.
        RetItem ri =
            argc == 0 ? ((RetItem(*)())fp)() :
            argc == 1 ? ((RetItem(*)(Item))fp)(args[0]) :
            argc == 2 ? ((RetItem(*)(Item, Item))fp)(args[0], args[1]) :
            argc == 3 ? ((RetItem(*)(Item, Item, Item))fp)(args[0], args[1], args[2]) :
                        ((RetItem(*)(Item, Item, Item, Item))fp)(
                            args[0], args[1], args[2], args[3]);
        return ri.err ? ItemError : ri.value;
    }

#define SYS_DISPATCH(RetT) \
    (argc == 0 ? ((RetT(*)())fp)() : \
     argc == 1 ? ((RetT(*)(Item))fp)(args[0]) : \
     argc == 2 ? ((RetT(*)(Item, Item))fp)(args[0], args[1]) : \
     argc == 3 ? ((RetT(*)(Item, Item, Item))fp)(args[0], args[1], args[2]) : \
     argc == 4 ? ((RetT(*)(Item, Item, Item, Item))fp)(args[0], args[1], args[2], args[3]) : \
                 ((RetT(*)(Item, Item, Item, Item, Item))fp)(args[0], args[1], args[2], args[3], args[4]))

    if (argc > 5) {
        log_error("interp: system function '%s' arity %d exceeds the dispatch table",
            info->name ? info->name : "<null>", argc);
        return ItemError;
    }
    switch (c_ret) {
    case LMD_TYPE_INT: {
        // len() has a raw integer C ABI, so its value-family error guard cannot
        // travel in the return register. Keep the rejected operand intact
        // before boxing the ordinary count (S7.6/S7.7).
        if (info->fn == SYSFUNC_LEN) {
            for (int i = 0; i < argc; i++) {
                if (get_type_id(args[i]) == LMD_TYPE_ERROR) return ItemError;
            }
        }
        return (Item){.item = i2it(SYS_DISPATCH(int64_t))};
    }
    case LMD_TYPE_INT64:
        // int64() returns a raw int64_t even when its semantic type is
        // int64 | error; INT64_ERROR is its out-of-band failure signal, so the
        // boundary boxing must be the error-aware one lowering uses.
        if (info->fn == SYSFUNC_INT64) {
            return box_int64_result_or_error(SYS_DISPATCH(int64_t));
        }
        return box_int64_value(SYS_DISPATCH(int64_t));
    case LMD_TYPE_BOOL: {
        // Bool-returning helpers use BOOL_ERROR as their third state. Treating
        // it as C truth would turn a failed value computation into `true` at
        // the interpreter ABI boundary; the JIT bool boxer preserves it as
        // ItemError (S7.6 value-family propagation).
        Bool result = SYS_DISPATCH(Bool);
        return result >= BOOL_ERROR ? ItemError
            : (Item){.item = b2it(result ? BOOL_TRUE : BOOL_FALSE)};
    }
    case LMD_TYPE_FLOAT:  return push_d(SYS_DISPATCH(double));
    case LMD_TYPE_STRING: {
        String* str = SYS_DISPATCH(String*);
        // Pointer-returning conversion APIs cannot carry ItemError in their C
        // signature. A null result is therefore the error carrier here; the
        // successful `string(null)` case returns the non-null STR_NULL object.
        return str ? (Item){.item = s2it(str)} : ItemError;
    }
    case LMD_TYPE_SYMBOL: {
        Symbol* sym = SYS_DISPATCH(Symbol*);
        // `name(null)` has a legitimate null result, while an error operand
        // must remain an error. The input check distinguishes those cases for
        // both name() and symbol(), whose C ABI uses a nullable pointer.
        if (sym) return (Item){.item = y2it(sym)};
        for (int i = 0; i < argc; i++) {
            if (get_type_id(args[i]) == LMD_TYPE_ERROR) return ItemError;
        }
        return ItemNull;
    }
    case LMD_TYPE_TYPE:   return interp_ptr_item(SYS_DISPATCH(Type*));
    case LMD_TYPE_DTIME:  return push_k(SYS_DISPATCH(DateTime));
    default:              return SYS_DISPATCH(Item);
    }
#undef SYS_DISPATCH
}

// a direct checked map store emits emit_return_if_item_error before the generic
// proc-side-effect lowering sees it. Keep that early return in T0 too: losing
// the failed candidate here would let a rejected typed write look successful.
static bool interp_direct_typed_map_assignment(AstNode* node) {
    if (!node || (node->node_type != AST_NODE_INDEX_ASSIGN_STAM &&
            node->node_type != AST_NODE_MEMBER_ASSIGN_STAM)) return false;
    AstCompoundAssignNode* assignment = (AstCompoundAssignNode*)node;
    AstCowPath path = {};
    if (!ast_collect_cow_path(&path, assignment->object) || path.count != 0 ||
            !path.root || path.root->node_type != AST_NODE_IDENT) return false;
    NameEntry* root = ((AstIdentNode*)path.root)->entry;
    return root && ast_declared_type_is_map(root->declared_type);
}

// T0 discards procedural statement values in content blocks just as MIR does.
// preserve a failure before that discard when the enclosing procedure exposes
// an error channel, or a direct checked map store already owns that boundary.
static void interp_propagate_proc_side_effect_error(InterpFrame* f,
        AstNode* node, Item value) {
    if (!f || !node || !item_is_error(value) ||
            !side_effect_result_can_error(node->node_type)) return;
    TypeFunc* signature = f->fn ? (TypeFunc*)((AstNode*)f->fn)->type : NULL;
    if (interp_direct_typed_map_assignment(node) ||
            (signature && signature->type_id == LMD_TYPE_FUNC && signature->can_raise)) {
        interp_signal(f, EvalSignal::RETURNED, value);
    }
}


static Item eval_call(InterpFrame* f, AstCallNode* node, const Item* injected) {
    int source_argc = 0;
    for (AstNode* a = node->argument; a; a = a->next) source_argc++;
    int argc = source_argc + (injected ? 1 : 0);

    AstNode* callee = ast_unwrap_primary(node->function);
    AstFuncNode* direct_fn = ast_direct_call_function(node);
    bool has_named_args = ast_call_has_named_args(node);
    TypeFunc* direct_signature = direct_fn && ((AstNode*)direct_fn)->type &&
            ((AstNode*)direct_fn)->type->type_id == LMD_TYPE_FUNC
        ? (TypeFunc*)((AstNode*)direct_fn)->type : NULL;
    if (f->st->mode != EvalMode::RUNTIME) {
        SysFuncInfo* info = callee && callee->node_type == AST_NODE_SYS_FUNC
            ? ((AstSysFuncNode*)callee)->fn_info : NULL;
        if (!interp_eval_mode_allows_sys_func(f->st->mode, info)) {
            // The static scan keeps this unreachable for admitted predicates;
            // retain the runtime gate so a new AST edge cannot turn a `that`
            // clause into an effectful call by omission (AI17).
            f->st->mode_rejected = true;
            return ItemError;
        }
    }

    if (argc > LAMBDA_MAX_FUNCTION_ARGS) {
        log_error("interp: call arity %d exceeds the Core Lambda limit", argc);
        return ItemError;
    }
    if (node->interp_self_tail_call && f->fn && f->plan) {
        // Every argument is evaluated *before* any parameter is rebound: an
        // argument may read a parameter (`loop(n - 1, acc + n)`), and writing
        // the slots as we go would feed the new n into acc.
        uint16_t params = f->plan->param_count;
        RootSpan next_args((size_t)(params > 0 ? params : 1));
        uint64_t* words = next_args.words();
        AstNode* resolved_args[LAMBDA_MAX_FUNCTION_ARGS] = {0};
        if (has_named_args) {
            ast_resolve_call_args(node->argument, direct_fn, source_argc, resolved_args);
        }
        AstNode* positional = node->argument;
        AstNamedNode* parameter = f->fn->param;
        for (int i = 0; i < (int)params; i++) {
            AstNode* value_node = has_named_args ? resolved_args[i] : positional;
            if (!has_named_args && positional) positional = positional->next;
            if (value_node) {
                words[i] = eval_expr(f, value_node).item;
            } else {
                TypeParam* type_param = parameter && parameter->type &&
                    parameter->type->kind == TYPE_KIND_PARAM
                    ? (TypeParam*)parameter->type : NULL;
                words[i] = type_param && type_param->default_value
                    ? eval_expr(f, type_param->default_value).item : ITEM_NULL;
            }
            if (interp_frame_pending(f)) return ItemNull;
            if (parameter) {
                Item coerced = interp_coerce_declared_numeric(f,
                    (Item){.item = words[i]}, parameter->declared_type);
                words[i] = coerced.item;
            }
            if (parameter) parameter = (AstNamedNode*)((AstNode*)parameter)->next;
        }
        TypeFunc* current_signature = f->fn && ((AstNode*)f->fn)->type &&
                ((AstNode*)f->fn)->type->type_id == LMD_TYPE_FUNC
            ? (TypeFunc*)((AstNode*)f->fn)->type : NULL;
        Item rejected = interp_rejected_parameter_error(current_signature,
            (const Item*)(void*)words, params);
        if (item_is_error(rejected)) {
            // Tail iteration skips lambda_dynamic_call, so it must preserve
            // the same rejected-parameter exit before the callee body sees it.
            interp_signal(f, EvalSignal::RETURNED, rejected);
            return rejected;
        }
        for (int p = 0; p < (int)params; p++) {
            f->slots[p] = words[p];
        }
        // MIR's self-tail loop has no write to its hidden `_vargs` parameter:
        // it rebinds fixed slots only, so the initial rest-list remains the
        // activation's `varg()` view for every iteration.
        f->signal = EvalSignal::TAIL_CALL;
        return ItemNull;
    }

    int named_param_count = argc;
    if (has_named_args) {
        if (injected || !direct_fn) {
            // Named operands are reordered against a Lambda declaration; a
            // dynamic or injected call has no equivalent positional ABI.
            log_error("interp: named arguments need a direct Lambda call");
            return ItemError;
        }
        named_param_count = 0;
        for (AstNamedNode* param = direct_fn->param; param;
                param = (AstNamedNode*)((AstNode*)param)->next) {
            named_param_count++;
        }
        if (direct_signature && direct_signature->is_variadic &&
                source_argc > named_param_count) {
            // ast_resolve_call_args keeps source positions after the fixed
            // formals for the rest-list builder that both tiers already use.
            named_param_count = source_argc;
        }
        if (named_param_count > LAMBDA_MAX_FUNCTION_ARGS) {
            log_error("interp: named call has %d parameters, limit is %d",
                named_param_count, LAMBDA_MAX_FUNCTION_ARGS);
            return ItemError;
        }
    }

    if (!injected && source_argc == 1 && callee &&
            callee->node_type == AST_NODE_TYPE) {
        Type* target_type = ((AstNode*)node)->type;
        if (target_type && (target_type->type_id == LMD_TYPE_NUM_SIZED ||
                target_type->type_id == LMD_TYPE_UINT64)) {
            Item source = eval_expr(f, node->argument);
            if (interp_frame_pending(f)) return ItemNull;
            // A type AST evaluates to a Type value, not a Function.  MIR
            // intercepts these one-argument calls and uses the shared numeric
            // coercion helpers, so T0 must do likewise before dynamic call
            // dispatch treats the Type value as a non-callable target.
            return interp_coerce_declared_numeric(f, source, target_type);
        }
    }

    // `print` is Lambda-variadic but not C-variadic: lowering emits one
    // pn_print per argument with an explicit " " separator between them, and
    // the whole call yields null. Mirror that shape rather than inventing a
    // marshalling rule for a function that has none.
    if (callee && callee->node_type == AST_NODE_SYS_FUNC &&
            ((AstSysFuncNode*)callee)->fn_info &&
            ((AstSysFuncNode*)callee)->fn_info->fn == SYSPROC_PRINT) {
        bool first = true;
        for (AstNode* a = node->argument; a; a = a->next) {
            if (!first) {
                // Keep the separator explicit, as lowering does, so a future
                // config change has one place to land in either tier.
                Item sep = (Item){.item = s2it(heap_strcpy(" ", 1))};
                pn_print(sep);
            }
            Item value = eval_expr(f, a);
            if (interp_frame_pending(f)) return ItemNull;
            Scratch arg_slot(f);
            arg_slot.set(value);
            pn_print(arg_slot.get());
            first = false;
        }
        return ItemNull;
    }

    if (callee && callee->node_type == AST_NODE_SYS_FUNC) {
        SysFuncInfo* sinfo = ((AstSysFuncNode*)callee)->fn_info;
        AstNode* owner_arg = !injected ? ast_unwrap_primary(node->argument) : NULL;
        NameEntry* owner_entry = owner_arg && owner_arg->node_type == AST_NODE_IDENT
            ? ((AstIdentNode*)owner_arg)->entry : NULL;
        if (sinfo && owner_entry && !owner_entry->import &&
                (sinfo->fn == SYSPROC_PUSH || sinfo->fn == SYSPROC_SPLICE)) {
            // MIR evaluates the non-owner operands before it detaches a shared
            // binding. Calling pn_push/pn_splice directly mutated both aliases;
            // use the shared COW entries and publish their replacement root.
            RootSpan value_args((size_t)(argc - 1));
            uint64_t* values = value_args.words();
            AstNode* value_node = node->argument->next;
            for (int i = 0; value_node; value_node = value_node->next, i++) {
                values[i] = eval_expr(f, value_node).item;
                if (interp_frame_pending(f)) return ItemNull;
            }
            Item owner = interp_read_binding(f, owner_entry);
            Item replacement = sinfo->fn == SYSPROC_PUSH
                ? pn_push_cow(owner, (Item){.item = values[0]})
                : pn_splice_cow(owner, (Item){.item = values[0]},
                    (Item){.item = values[1]});
            if (item_is_error(replacement)) return replacement;
            interp_write_binding(f, owner_entry, replacement);
            return replacement;
        }
        // Arguments must all be rooted before the C entry runs: the entry may
        // allocate, and an earlier argument would otherwise be unreachable.
        RootSpan arg_roots((size_t)(argc > 0 ? argc : 1));
        uint64_t* words = arg_roots.words();
        int i = 0;
        if (injected) words[i++] = injected->item;
        for (AstNode* a = node->argument; a; a = a->next, i++) {
            words[i] = eval_expr(f, a).item;
        }
        // `map()` / `map([k, v, …])` carry no boxed entry point: lowering emits
        // vmap_new / vmap_from_array directly (transpile-mir.cpp), so the walker
        // calls the same two helpers rather than going through the dispatch.
        if (sinfo && sinfo->fn == SYSFUNC_VMAP_NEW) {
            return argc == 0 ? vmap_new() : vmap_from_array((Item){.item = words[0]});
        }
        Item sresult = eval_sys_call(f, sinfo, (const Item*)(void*)words, argc,
            node->type);
        // floor/ceil/round/trunc/abs preserve their argument's lane: the boxed
        // helper always yields float, while lowering unboxes that result by the
        // *call node's* static type (POST_PROCESS_UNBOX, transpile-mir.cpp), so
        // `trunc(n:int)` stays an int. Re-narrowing on the same static type
        // reproduces the choice without duplicating type inference.
        if (sinfo && sinfo->native_c_name && sinfo->native_func_ptr &&
                !sysfunc_native_math_always_float(sinfo->fn) &&
                node->type && node->type->type_id == LMD_TYPE_INT &&
                get_type_id(sresult) == LMD_TYPE_FLOAT) {
            sresult = (Item){.item = i2it((int32_t)it2d(sresult))};
        }
        return sresult;
    }

    Item callee_value = eval_expr(f, node->function);
    Scratch fn_slot(f);
    fn_slot.set(callee_value);
    if (item_is_error(fn_slot.get())) return fn_slot.get();

    int dispatch_argc = has_named_args ? named_param_count : argc;
    RootSpan arg_roots((size_t)(dispatch_argc > 0 ? dispatch_argc : 1));
    uint64_t* words = arg_roots.words();
    int i = 0;
    if (has_named_args) {
        AstNode* resolved_args[LAMBDA_MAX_FUNCTION_ARGS] = {0};
        ast_resolve_call_args(node->argument, direct_fn, source_argc, resolved_args);
        for (i = 0; i < dispatch_argc; i++) {
            words[i] = resolved_args[i] ? eval_expr(f, resolved_args[i]).item :
                ITEM_MISSING_ARGUMENT;
            if (interp_frame_pending(f)) return ItemNull;
        }
    } else {
        if (injected) words[i++] = injected->item;
        for (AstNode* a = node->argument; a; a = a->next, i++) {
            words[i] = eval_expr(f, a).item;
        }
    }

    Item callee_item = fn_slot.get();
    if (get_type_id(callee_item) != LMD_TYPE_FUNC) {
        log_error("interp: call target is not a function (type %d)",
            (int)get_type_id(callee_item));
        return ItemError;
    }
    Function* fn = (Function*)(uintptr_t)callee_item.item;
    if (!injected && ast_type_func_has_var_parameter(direct_signature)) {
        NameEntry* borrowed[LAMBDA_MAX_FUNCTION_ARGS] = {0};
        if (!ast_direct_call_var_parameter_entries(node, direct_signature, borrowed)) {
            log_error("interp: unsupported direct `var` argument layout");
            return ItemError;
        }
        for (int index = 0; index < dispatch_argc; index++) {
            NameEntry* entry = borrowed[index];
            Item owner = (Item){.item = words[index]};
            if (!entry || !entry->cow_owned ||
                    !is_container_type_id(get_type_id(owner))) {
                continue;
            }
            // MIR detaches a borrowed COW root before entering a `var` callee:
            // the callee has no replacement channel until return, so delaying
            // this copy would let its first store mutate the caller's alias.
            Item private_owner = cow_prepare_write(owner);
            if (item_is_error(private_owner)) return private_owner;
            words[index] = private_owner.item;
            interp_write_binding(f, entry, private_owner);
        }
        return interp_call_with_borrowed(fn, (const Item*)(void*)words,
            dispatch_argc, f, borrowed);
    }
    // Every callee — interpreted or native — reaches its body through the
    // single dynamic dispatch point (AI7). Routing interpreted calls through it
    // too is what gives them the shared arity check plus the optional/rest
    // adapter, instead of a second, divergent argument protocol.
    List args = {};
    args.length = dispatch_argc;
    args.items = (Item*)(void*)words;
    uint64_t result_home = 0;
    return fn_call_into(fn, dispatch_argc ? &args : NULL, &result_home);
}

// ---------------------------------------------------------------------------
// Closures
// ---------------------------------------------------------------------------

Function* interp_make_closure(Script* module, const AstFuncNode* fn_node,
                              InterpFrame* creating_frame) {
    int arity = 0;
    for (AstNamedNode* p = fn_node->param; p; p = (AstNamedNode*)((AstNode*)p)->next) arity++;
    int cap_count = 0;
    for (FnCapture* c = fn_node->captures; c; c = c->next) cap_count++;

    // Two allocations plus a capture read per slot, every one of them a
    // safepoint: the env and the Function must be rooted across all of them
    // and re-read afterwards (D5.3.3).
    RootFrame roots(2);
    Rooted<void*> env_root(roots, NULL);
    if (cap_count > 0) {
        // Same env shape the JIT builds: cap_count Item lanes followed by
        // cap_count owned-scalar tails (16 bytes per capture).
        void* env = heap_calloc((size_t)cap_count * 16, 0);
        if (!env) return NULL;
        env_root.set(env);
    }
    Rooted<Function*> fn_root(roots, to_closure_named(NULL, arity, env_root.get(),
        fn_node->name ? fn_node->name->chars : NULL));
    if (!fn_root.get()) return NULL;
    Function* fn = fn_root.get();
    fn->closure_field_count = (uint8_t)cap_count;
    fn->entry_abi = FN_ENTRY_ABI_LAMBDA_INTERPRETED;
    fn->def = fn_node;
    fn->def_module = module;
    fn->runtime_context = (Context*)context;
    lambda_function_set_type(fn, fn_node->type);

    // Snapshot captures by value (D6.2.3).
    if (cap_count > 0 && creating_frame) {
        int index = 0;
        for (FnCapture* cap = fn_node->captures; cap; cap = cap->next, index++) {
            Item value = interp_read_binding(creating_frame, cap->entry);
            Function* owner = fn_root.get();
            owned_item_slot_store((Item*)owner->closure_env, cap_count, index, value);
        }
    }
    return fn_root.get();
}

// ---------------------------------------------------------------------------
// Containers
// ---------------------------------------------------------------------------

// Vector functions dispatch on the runtime container type_id, so a homogeneous
// numeric literal MUST become the same compact ArrayNum the JIT builds — a
// generic Array would silently take a different helper branch (e.g. fn_take's
// is_content list instead of an ArrayNum slice). Selection mirrors
// transpile_array; interp_array_elem_kind is the shared gate, and the pre-scan
// falls the script back for shapes this path does not build (N-D literals,
// spreads, in-literal lets).
typedef enum InterpArrayKind {
    INTERP_ARRAY_GENERIC = 0,
    INTERP_ARRAY_INT,
    INTERP_ARRAY_FLOAT,
    INTERP_ARRAY_SIZED,
} InterpArrayKind;

// A child that produces a stream (for-expression, spread, pipe) makes every
// element of the literal a spread candidate — the same all-or-nothing rule
// transpile_array applies, and it also disables the compact-numeric paths.
static bool interp_array_has_spread(AstArrayNode* node, bool* pipe_spread) {
    bool spreadable = false;
    if (pipe_spread) *pipe_spread = false;
    for (AstNode* item = node->item; item; item = item->next) {
        if (item->node_type == AST_NODE_FOR_EXPR || item->node_type == AST_NODE_SPREAD) {
            spreadable = true;
        } else if (item->node_type == AST_NODE_PIPE) {
            if (pipe_spread) *pipe_spread = true;
        }
    }
    return spreadable;
}

static InterpArrayKind interp_array_kind(AstArrayNode* node,
                                         ArrayNumElemType* sized_elem) {
    TypeArray* arr_type = (TypeArray*)node->type;
    if (!node->item || !arr_type || !arr_type->nested) return INTERP_ARRAY_GENERIC;
    bool pipe_spread = false;
    if (interp_array_has_spread(node, &pipe_spread) || pipe_spread) {
        return INTERP_ARRAY_GENERIC;
    }
    // A compact numeric array cannot faithfully carry an open value, null or an
    // error; keep those members in generic storage.
    for (AstNode* item = node->item; item; item = item->next) {
        TypeId item_type = item->type ? item->type->type_id : LMD_TYPE_ANY;
        if (item_type == LMD_TYPE_ANY || item_type == LMD_TYPE_NULL ||
                item_type == LMD_TYPE_ERROR) return INTERP_ARRAY_GENERIC;
    }
    switch (arr_type->nested->type_id) {
    case LMD_TYPE_INT:   return INTERP_ARRAY_INT;
    case LMD_TYPE_FLOAT: return INTERP_ARRAY_FLOAT;
    case LMD_TYPE_UINT64:
        // A generic container read intentionally normalizes a small u64 to
        // int, while MIR's typed-index path keeps its raw u64 lane. Do not
        // admit this carrier until T0 has that direct indexed-read contract.
        return INTERP_ARRAY_GENERIC;
    case LMD_TYPE_NUM_SIZED:
        // The AST records the homogeneous NumSized subtype on `nested`; using
        // that same witness is required so T0 does not widen a u8[]/f32[]
        // literal to a generic array before its typed-array boundary.
        *sized_elem = num_sized_to_elem_type(
            type_num_sized_kind(arr_type->nested));
        return INTERP_ARRAY_SIZED;
    default:
        return INTERP_ARRAY_GENERIC;
    }
}

// Fills an N-D literal's leaves in row-major order, mirroring
// mir_emit_ndim_leaves.
static void interp_fill_ndim(InterpFrame* f, AstNode* node, Scratch& arr, int* flat) {
    node = ast_unwrap_primary(node);
    if (!node || node->node_type != AST_NODE_ARRAY) return;
    AstArrayNode* a = (AstArrayNode*)node;
    TypeArray* type = (TypeArray*)a->type;
    bool nested = type && type->nested && type->nested->type_id == LMD_TYPE_ARRAY;
    for (AstNode* item = a->item; item; item = item->next) {
        if (nested) { interp_fill_ndim(f, item, arr, flat); continue; }
        Item value = eval_expr(f, item);
        if (interp_frame_pending(f)) return;
        // Re-read: element evaluation is a safepoint.
        array_num_set_item((ArrayNum*)(uintptr_t)arr.get().item, (*flat)++, value);
    }
}

static Item eval_array(InterpFrame* f, AstArrayNode* node) {
    // Nested numeric literals fold into one shaped ArrayNum rather than an
    // array of arrays; detect_ndim_literal is lowering's own detector.
    int64_t shape[32];
    ArrayNumElemType nd_elem;
    bool nd_pipe_spread = false;
    if (!interp_array_has_spread(node, &nd_pipe_spread) && !nd_pipe_spread) {
        int ndim = detect_ndim_literal((AstNode*)node, shape, 32, &nd_elem, true);
        if (ndim >= 2) {
            int64_t total = 1;
            for (int i = 0; i < ndim; i++) total *= shape[i];
            Scratch acc(f);
            acc.set(interp_ptr_item(array_num_new_ndim(nd_elem, total, ndim, shape)));
            int flat = 0;
            interp_fill_ndim(f, (AstNode*)node, acc, &flat);
            return acc.get();
        }
    }

    ArrayNumElemType sized_elem = ELEM_INT;
    InterpArrayKind kind = interp_array_kind(node, &sized_elem);
    int count = 0;
    for (AstNode* item = node->item; item; item = item->next) count++;

    if (kind != INTERP_ARRAY_GENERIC) {
        ArrayNum* fresh =
            kind == INTERP_ARRAY_INT   ? array_int_new(count) :
            kind == INTERP_ARRAY_FLOAT ? array_float_new(count) :
                                         array_num_new(sized_elem, count);
        if (!fresh) return ItemError;
        // Element evaluation is a safepoint even though the stores are not, so
        // the fresh array is held in a frame slot and re-read every iteration.
        Scratch acc(f);
        acc.set(interp_ptr_item(fresh));
        int index = 0;
        for (AstNode* item = node->item; item; item = item->next, index++) {
            Item value = eval_expr(f, item);
            ArrayNum* arr = (ArrayNum*)(uintptr_t)acc.get().item;
            if (!arr) return ItemError;
            if (kind == INTERP_ARRAY_INT) {
                array_int_set(arr, index, it2l(value));
            } else if (kind == INTERP_ARRAY_FLOAT) {
                array_float_set(arr, index, it2d(value));
            } else {
                array_num_set_item(arr, index, value);
            }
        }
        return acc.get();
    }

    bool pipe_spread = false;
    bool has_spreadable = interp_array_has_spread(node, &pipe_spread);
    bool any_spread = has_spreadable || pipe_spread;

    Scratch acc(f);
    acc.set(interp_ptr_item(array()));
    if (!node->item) return acc.get();   // array_end() on an empty array is spreadable-null
    for (AstNode* item = node->item; item; item = item->next) {
        // `let` bindings inside an array literal are transparent: they bind but
        // contribute no element.
        if (item->node_type == AST_NODE_ASSIGN) {
            AstNamedNode* named = (AstNamedNode*)item;
            Item bound = eval_expr(f, named->as);
            if (interp_frame_pending(f)) return acc.get();
            bound = interp_coerce_declared_numeric(f, bound, named->declared_type);
            interp_write_binding(f, named->entry, bound);
            continue;
        }
        Item value = eval_expr(f, item);
        if (interp_frame_pending(f)) return acc.get();
        // Re-read the accumulator after every element: growth may collect.
        Array* arr = (Array*)(uintptr_t)acc.get().item;
        if (!arr) return ItemError;
        if (item->node_type == AST_NODE_PIPE) array_push_spread_all(arr, value);
        else if (has_spreadable || item->node_type == AST_NODE_SPREAD) {
            array_push_spread(arr, value);
        } else {
            array_push(arr, value);
        }
    }
    Array* built = (Array*)(uintptr_t)acc.get().item;
    Item result = array_end(built);
    // An all-empty stream reports spreadable-null; a literal `[for ...]` is an
    // empty array instead.
    if (any_spread && result.item == ITEM_NULL_SPREADABLE) return interp_ptr_item(built);
    return result;
}

// Same order as transpile_map's fallback path: every value is evaluated and
// rooted first, then the map is allocated and filled. Allocating the map
// before the values would keep a fresh container live across calls that can
// collect, for no gain.
static Item eval_map(InterpFrame* f, AstMapNode* node) {
    TypeMap* map_type = (TypeMap*)node->type;
    int val_count = 0;
    for (AstNode* item = node->item; item; item = item->next) val_count++;
    if (val_count == 0) {
        return interp_ptr_item(map_with_type_tl(map_type, f->module->type_list));
    }

    RootSpan values((size_t)val_count);
    uint64_t* words = values.words();
    int vi = 0;
    for (AstNode* item = node->item; item; item = item->next) {
        AstNode* value_node = item->node_type == AST_NODE_KEY_EXPR
            ? ((AstNamedNode*)item)->as : item;
        words[vi++] = value_node ? eval_expr(f, value_node).item : ItemNull.item;
    }
    Map* built = map_with_type_tl(map_type, f->module->type_list);
    if (!built) return ItemError;
    return interp_ptr_item(map_fill_items(built, (const Item*)(void*)words, vi));
}


// RAII push/pop for an implicit-context binding, so no early return can leave
// a stale `~` visible to an outer expression.
class InterpContextGuard {
    InterpState* st_;
    InterpContext entry_;
public:
    InterpContextGuard(InterpState* st, uint64_t* item, uint64_t* index)
            : st_(st), entry_{item, index, NULL, NULL, st->contexts} {
        st_->contexts = &entry_;
    }
    InterpContextGuard(InterpState* st, uint64_t* item, uint64_t* index,
            uint64_t* parent, uint64_t* root)
            : st_(st), entry_{item, index, parent, root, st->contexts} {
        st_->contexts = &entry_;
    }
    ~InterpContextGuard() { st_->contexts = entry_.prev; }
    InterpContextGuard(const InterpContextGuard&) = delete;
    InterpContextGuard& operator=(const InterpContextGuard&) = delete;
};

// Handler bodies run in the same activation as their operand. Keeping the
// caught Item in a Scratch home makes `^` safe across every allocating child
// evaluation while this guard restores an enclosing handler's binding.
class InterpErrorContextGuard {
    InterpState* st_;
    InterpErrorContext entry_;
public:
    InterpErrorContextGuard(InterpState* st, uint64_t* error)
            : st_(st), entry_{error, st->errors} {
        st_->errors = &entry_;
    }
    ~InterpErrorContextGuard() { st_->errors = entry_.prev; }
    InterpErrorContextGuard(const InterpErrorContextGuard&) = delete;
    InterpErrorContextGuard& operator=(const InterpErrorContextGuard&) = delete;
};

// `last` derives from the innermost subscript owner. The owner is a frame slot
// so evaluating an arithmetic index cannot leave a movable container in a C++
// local across fn_len's allocation-capable integer boxing path.
class InterpLastIndexGuard {
    InterpState* st_;
    uint64_t* previous_;
public:
    InterpLastIndexGuard(InterpState* st, uint64_t* item)
            : st_(st), previous_(st ? st->last_index_item : NULL) {
        if (st_) st_->last_index_item = item;
    }
    ~InterpLastIndexGuard() {
        if (st_) st_->last_index_item = previous_;
    }
    InterpLastIndexGuard(const InterpLastIndexGuard&) = delete;
    InterpLastIndexGuard& operator=(const InterpLastIndexGuard&) = delete;
};

// A native fault abandons every C++ activation below this helper.  The
// recovery checkpoint restores the side stacks; restore the interpreter-only
// chains as well before the handler observes the resulting Error Item.
static Item interp_eval_local_fault_operand(InterpFrame* f, AstNode* expression) {
    if (!f || !f->st || !expression) return ItemError;
    InterpState* st = f->st;
    LambdaRecoveryFrame* recovery = lambda_recovery_frame_begin_for(
        (Context*)st->ctx, LAMBDA_RECOVERY_CAP_LOCAL_FAULT);
    if (!recovery) {
        (void)lambda_recovery_frame_raise_fault(LAMBDA_FAULT_OUT_OF_MEMORY, ERR_OK);
        return ItemError;
    }

    InterpFrame* saved_top = st->top;
    InterpContext* saved_contexts = st->contexts;
    InterpErrorContext* saved_errors = st->errors;
    uint64_t* saved_last_index_item = st->last_index_item;
    uint32_t saved_depth = st->depth;
    List* saved_vargs = st->ctx ? st->ctx->current_vargs : NULL;
    uint32_t saved_scratch_top = f->scratch_top;
    EvalSignal saved_signal = f->signal;
    uint64_t saved_signal_payload = f->slots[f->signal_index];
    const AstNode* saved_cur = f->cur;

    if (LAMBDA_RECOVERY_FRAME_SETJMP(recovery)) {
        Item fault = ItemError;
        if (lambda_recovery_frame_restore_landing(recovery)) {
            fault = lambda_recovery_frame_fault_item((Context*)st->ctx, recovery);
        } else {
            log_error("interp: local fault recovery landing invariant failed");
        }
        lambda_recovery_frame_end(recovery);
        st->top = saved_top;
        st->contexts = saved_contexts;
        st->errors = saved_errors;
        st->last_index_item = saved_last_index_item;
        st->depth = saved_depth;
        // A native longjmp skips the abandoned variadic frame's destructor.
        // Restore the enclosing call's varg() binding before its handler runs.
        if (st->ctx) st->ctx->current_vargs = saved_vargs;
        f->scratch_top = saved_scratch_top;
        f->signal = saved_signal;
        f->slots[f->signal_index] = saved_signal_payload;
        f->cur = saved_cur;
        return fault;
    }
    if (!lambda_recovery_frame_arm(recovery)) {
        log_error("interp: failed to arm local fault recovery frame");
        lambda_recovery_frame_end(recovery);
        return ItemError;
    }

    Item result = eval_expr(f, expression);
    lambda_recovery_frame_end(recovery);
    return result;
}

// One handler evaluation owns one operand completion. The error arm is a
// local branch on that completion; it does not use a recovery boundary and a
// failure produced by the body remains the body's own returned outcome.
static Item eval_handler(InterpFrame* f, AstHandlerNode* handler) {
    Scratch operand(f);
    bool local_fault_operand = handler->is_statement && f->fn &&
        f->fn->node_type == AST_NODE_PROC;
    operand.set(local_fault_operand
        ? interp_eval_local_fault_operand(f, handler->operand)
        : eval_expr(f, handler->operand));
    if (interp_frame_pending(f)) return operand.get();

    Item operand_value = operand.get();
    if (item_is_error(operand_value)) {
        InterpErrorContextGuard error_scope(f->st, operand.home());
        Item body_value = eval_expr(f, handler->body);
        // A statement handler discards only a normal arm result. An error
        // created by the selected body is a fresh completion and must leave
        // this activation instead of being converted to statement null.
        return handler->is_statement && !item_is_error(body_value)
            ? ItemNull : body_value;
    }

    if (handler->value_body) {
        InterpContextGuard value_scope(f->st, operand.home(), NULL);
        Item value = eval_expr(f, handler->value_body);
        return handler->is_statement && !item_is_error(value)
            ? ItemNull : value;
    }
    return handler->is_statement ? ItemNull : operand_value;
}

// ---------------------------------------------------------------------------
// Match
// ---------------------------------------------------------------------------

// Runs an admitted `that` body with the candidate installed as `~`.  The
// predicate does not signal a language error when it is unsupported, faults,
// or exhausts fuel: that check simply fails, as the JIT predicate path's false
// branch does.  Object constraints remain on their separate S11.4.6 path.
static bool interp_eval_constrained_predicate(InterpFrame* f,
        AstConstrainedTypeNode* constrained, Scratch& subject) {
    if (!constrained || !constrained->constraint ||
            !interp_predicate_supported(constrained->constraint)) return false;

    InterpEvalModeGuard mode(f->st, EvalMode::PREDICATE,
        interp_predicate_fuel_budget());
    Scratch index_slot(f);
    index_slot.set(ItemNull);
    Scratch parent_slot(f);
    Scratch root_slot(f);
    if (f->st->contexts && f->st->contexts->parent) {
        parent_slot.set((Item){.item = *f->st->contexts->parent});
    } else {
        parent_slot.set(ItemNull);
    }
    if (f->st->contexts && f->st->contexts->root) {
        root_slot.set((Item){.item = *f->st->contexts->root});
    } else {
        root_slot.set(subject.get());
    }
    InterpContextGuard bound(f->st, subject.home(), index_slot.home(),
        parent_slot.home(), root_slot.home());
    Item result = eval_expr(f, constrained->constraint);
    return !interp_frame_pending(f) && mode.completed() &&
        is_truthy(result) == BOOL_TRUE;
}

// One arm's pattern test. Mirrors emit_single_pattern_test: a type pattern uses
// fn_is, a range pattern fn_in, anything else fn_eq — and a constrained pattern
// checks its base TypeId then evaluates the `that` clause with `~` bound to the
// scrutinee. Constrained arms remain base-plus-predicate, which is the shipped
// behaviour S11.4.6 describes; this changes no ruling.
static bool interp_pattern_matches(InterpFrame* f, AstNode* pattern, Scratch& scrut) {
    if (!pattern) return false;

    // Union patterns (A | B) match if any alternative does.
    if (pattern->node_type == AST_NODE_BINARY_TYPE) {
        AstBinaryNode* bi = (AstBinaryNode*)pattern;
        if (bi->op == OPERATOR_UNION) {
            return interp_pattern_matches(f, bi->left, scrut) ||
                   interp_pattern_matches(f, bi->right, scrut);
        }
    }

    if (pattern->node_type == AST_NODE_CONSTRAINED_TYPE) {
        AstConstrainedTypeNode* ct = (AstConstrainedTypeNode*)pattern;
        TypeConstrained* constrained = (TypeConstrained*)ct->type;
        if (constrained && constrained->base) {
            if (get_type_id(scrut.get()) != constrained->base->type_id) return false;
            return interp_eval_constrained_predicate(f, ct, scrut);
        }
    }

    Item pattern_value = eval_expr(f, pattern);
    if (interp_frame_pending(f)) return false;
    Scratch pat(f);
    pat.set(pattern_value);
    TypeId pat_tid = get_type_id(pat.get());
    if (pat_tid == LMD_TYPE_TYPE) return fn_is(scrut.get(), pat.get()) == BOOL_TRUE;
    if (pat_tid == LMD_TYPE_RANGE) return fn_in(scrut.get(), pat.get()) == BOOL_TRUE;
    return fn_eq(scrut.get(), pat.get()) == BOOL_TRUE;
}

static Item eval_match(InterpFrame* f, AstMatchNode* node) {
    // The scrutinee is evaluated exactly once (S11.2.1) and stays `~` for every
    // arm's pattern and body.
    Item value = eval_expr(f, node->scrutinee);
    if (interp_frame_pending(f)) return value;
    Scratch scrut(f);
    scrut.set(value);
    Scratch index_slot(f);
    index_slot.set(ItemNull);
    Scratch parent_slot(f);
    Scratch root_slot(f);
    if (f->st->contexts && f->st->contexts->item) {
        parent_slot.set((Item){.item = *f->st->contexts->item});
    } else {
        parent_slot.set(ItemNull);
    }
    if (f->st->contexts && f->st->contexts->root) {
        root_slot.set((Item){.item = *f->st->contexts->root});
    } else {
        root_slot.set(scrut.get());
    }
    InterpContextGuard bound(f->st, scrut.home(), index_slot.home(),
        parent_slot.home(), root_slot.home());

    for (AstNode* arm = (AstNode*)node->first_arm; arm; arm = arm->next) {
        AstMatchArm* match_arm = (AstMatchArm*)arm;
        if (!match_arm->pattern) return eval_expr(f, match_arm->body);   // default arm
        if (interp_pattern_matches(f, match_arm->pattern, scrut)) {
            return eval_expr(f, match_arm->body);
        }
        if (interp_frame_pending(f)) return ItemNull;
    }
    return ItemNull;   // no arm matched
}

// ---------------------------------------------------------------------------
// Pipes
// ---------------------------------------------------------------------------

// build_ast already owns this question — transpile_pipe asks it the same way to
// choose between argument injection and the mapping loop, so both tiers split
// on one predicate.
extern bool has_current_item_ref(AstNode* node);

// Mirrors transpile_pipe: `a | b` maps b over a's members with `~`/`~#` bound,
// `a where b` filters a by b, and a `~`-free `a | f(x)` injects a as f's first
// argument. A scalar left operand is lifted to a one-element stream.
static Item eval_pipe(InterpFrame* f, AstBinaryNode* node) {
    Item left_value = eval_expr(f, node->left);
    if (interp_frame_pending(f)) return left_value;
    Scratch left(f);
    left.set(left_value);

    if (node->op == OPERATOR_PIPE && !has_current_item_ref(node->right)) {
        // Aggregate form: the whole left value becomes the right side's first
        // argument — `d |> f(x)` is `f(d, x)` and `d |> sum` is `sum(d)`.
        // Evaluating the right side on its own would call it with no receiver.
        AstNode* target = ast_unwrap_primary(node->right);
        Item piped = left.get();
        if (target && target->node_type == AST_NODE_CALL_EXPR) {
            return eval_call(f, (AstCallNode*)target, &piped);
        }
        if (target && target->node_type == AST_NODE_SYS_FUNC) {
            return eval_sys_call(f, ((AstSysFuncNode*)target)->fn_info, &piped, 1,
                node->type);
        }
        // A first-class callable on the right: apply it to the piped value.
        Item callee = eval_expr(f, node->right);
        if (interp_frame_pending(f)) return callee;
        if (get_type_id(callee) != LMD_TYPE_FUNC) {
            log_error("interp: pipe target is not callable");
            return ItemError;
        }
        uint64_t home = 0;
        List args = {};
        args.length = 1;
        args.items = (Item*)&piped;
        return fn_call_into((Function*)(uintptr_t)callee.item, &args, &home);
    }

    Item source = left.get();
    TypeId source_tid = get_type_id(source);
    bool is_map = source_tid == LMD_TYPE_MAP || source_tid == LMD_TYPE_ELEMENT ||
        source_tid == LMD_TYPE_OBJECT;
    SymbolKeyList* keys = is_map ? item_keys(source) : NULL;

    int64_t len = fn_len(source);
    // A bare scalar pipes as a one-element stream; a genuinely empty collection
    // stays empty. Only the non-collection tags lift.
    bool is_scalar = false;
    if (len == 0 && source_tid != LMD_TYPE_ARRAY && source_tid != LMD_TYPE_ARRAY_NUM &&
            source_tid != LMD_TYPE_RANGE && source_tid != LMD_TYPE_ELEMENT &&
            source_tid != LMD_TYPE_STRING && source_tid != LMD_TYPE_NULL) {
        len = 1;
        is_scalar = true;
    }

    // Plain array(), not array_spreadable(): a mapping pipe yields one value,
    // so `[1,2,3] |> ~ * 2` prints as [2, 4, 6] rather than flattening into the
    // enclosing block. transpile_pipe allocates the same way.
    Scratch out(f);
    out.set(interp_ptr_item(array()));
    Scratch item_slot(f);
    Scratch index_slot(f);
    Scratch parent_slot(f);
    Scratch root_slot(f);
    parent_slot.set(source);
    if (f->st->contexts && f->st->contexts->root) {
        root_slot.set((Item){.item = *f->st->contexts->root});
    } else {
        root_slot.set(source);
    }
    {
        // A pipe occurrence is rooted by its source container; nested pipes
        // retain the outer traversal root while replacing only the parent.
        InterpContextGuard bound(f->st, item_slot.home(), index_slot.home(),
            parent_slot.home(), root_slot.home());
        for (int64_t i = 0; i < len; i++) {
            Item current, key;
            if (is_map) {
                Symbol* sym = symbol_key_list_at(keys, i);
                current = sym ? item_attr(left.get(), sym->chars) : ItemNull;
                key = sym ? (Item){.item = y2it(sym)} : ItemNull;
            } else if (is_scalar) {
                current = left.get();
                key = (Item){.item = i2it(i)};
            } else {
                current = item_at(left.get(), i);
                key = (Item){.item = i2it(i)};
            }
            item_slot.set(current);
            index_slot.set(key);

            Item produced = eval_expr(f, node->right);
            if (interp_frame_pending(f)) break;
            Array* result = (Array*)(uintptr_t)out.get().item;   // re-read: may collect
            if (!result) break;
            if (node->op == OPERATOR_WHERE) {
                if (is_truthy(produced)) array_push(result, item_slot.get());
            } else {
                array_push(result, produced);
            }
        }
    }
    if (keys) symbol_key_list_free(keys);
    return array_end((Array*)(uintptr_t)out.get().item);
}

// ---------------------------------------------------------------------------
// For comprehensions
// ---------------------------------------------------------------------------

// AstLoopNode carries its variables as names, not NameEntry pointers: its
// AstNamedNode alias overlaps a real field, so push_name deliberately leaves no
// back-link (the same divergence FnAnalysis::decl_entry works around). The
// owning `for` scope is small, so resolve by name at bind time.
static NameEntry* interp_scope_lookup(NameScope* scope, String* name) {
    if (!scope || !name) return NULL;
    for (NameEntry* e = scope->first; e; e = e->next) {
        if (e->name && e->name->len == name->len &&
                memcmp(e->name->chars, name->chars, name->len) == 0) return e;
    }
    return NULL;
}

typedef struct ForCtx {
    InterpFrame* f;
    AstForNode*  node;
    uint64_t*    output;    // rooted Array* accumulator slot, NULL when discarded
} ForCtx;

static bool interp_for_level(ForCtx* fc, AstLoopNode* loop);

// Innermost level: run the let clause, apply `where`, then push the body value.
static bool interp_for_emit(ForCtx* fc) {
    InterpFrame* f = fc->f;
    for (AstNode* decl = fc->node->let_clause; decl; decl = decl->next) {
        if (decl->node_type == AST_NODE_ASSIGN) {
            AstNamedNode* named = (AstNamedNode*)decl;
            Item bound = eval_expr(f, named->as);
            if (interp_frame_pending(f)) return false;
            bound = interp_coerce_declared_numeric(f, bound, named->declared_type);
            interp_write_binding(f, named->entry, bound);
        } else if (decl->node_type == AST_NODE_DECOMPOSE) {
            exec_declaration(f, decl);
            if (interp_frame_pending(f)) return false;
        }
    }
    if (fc->node->where) {
        Item keep = eval_expr(f, fc->node->where);
        if (interp_frame_pending(f)) return false;
        if (!is_truthy(keep)) return true;    // filtered out, keep iterating
    }
    Item value = eval_expr(f, fc->node->then);
    if (interp_frame_pending(f)) return false;
    if (fc->output) {
        // Re-read the accumulator: the body evaluation above is a safepoint.
        Array* out = (Array*)(uintptr_t)*fc->output;
        if (out) array_push_spread(out, value);
    }
    return true;
}

static bool interp_for_level(ForCtx* fc, AstLoopNode* loop) {
    InterpFrame* f = fc->f;
    if (!loop) return interp_for_emit(fc);

    Item collection = eval_expr(f, loop->as);
    if (interp_frame_pending(f)) return false;
    Scratch coll_slot(f);
    coll_slot.set(collection);

    // item_keys allocates; the collection must already be published.
    SymbolKeyList* keys = item_keys(coll_slot.get());
    int key_filter = (int)loop->key_filter;
    int64_t length = iter_len(coll_slot.get(), keys, key_filter);

    NameEntry* value_entry = interp_scope_lookup(fc->node->vars, loop->name);
    NameEntry* index_entry = loop->index_name
        ? interp_scope_lookup(fc->node->vars, loop->index_name) : NULL;

    bool ok = true;
    for (int64_t i = 0; i < length && ok; i++) {
        Item current = loop->key_only
            ? iter_key_at(coll_slot.get(), keys, i, key_filter)
            : iter_val_at(coll_slot.get(), keys, i, key_filter);
        interp_write_binding(f, value_entry, current);
        if (index_entry) {
            interp_write_binding(f, index_entry,
                iter_key_at(coll_slot.get(), keys, i, key_filter));
        }
        ok = interp_for_level(fc, (AstLoopNode*)((AstNode*)loop)->next);
        if (f->signal == EvalSignal::BROKE) { interp_clear_loop_signal(f); break; }
        if (f->signal == EvalSignal::CONTINUED) { interp_clear_loop_signal(f); ok = true; }
        if (ok && i + 1 < length) interp_note_backedge(f);
    }
    if (keys) symbol_key_list_free(keys);
    return ok && !interp_frame_pending(f);
}

static Item eval_for(InterpFrame* f, AstForNode* for_node, bool result_demanded) {
    AstLoopNode* loop = (AstLoopNode*)for_node->loop;
    if (!loop) return ItemNull;

    ForCtx fc = {};
    fc.f = f;
    fc.node = for_node;
    // A procedural `for` statement still runs its body, but its comprehension
    // stream is dead when the enclosing block discards it.
    Scratch out_slot(f);
    if (result_demanded) {
        out_slot.set(interp_ptr_item(array_spreadable()));
        fc.output = out_slot.home();
    }

    interp_for_level(&fc, loop);
    if (!result_demanded) return ItemNull;

    Array* out = (Array*)(uintptr_t)out_slot.get().item;
    if (for_node->offset || for_node->limit) {
        Scratch selected(f);
        selected.set(out_slot.get());
        if (for_node->offset) {
            Item offset = eval_expr(f, for_node->offset);
            if (interp_frame_pending(f)) return ItemNull;
            Scratch offset_slot(f);
            offset_slot.set(offset);
            selected.set(fn_drop(selected.get(), offset_slot.get()));
            if (item_is_error(selected.get())) return selected.get();
        }
        if (for_node->limit) {
            Item limit = eval_expr(f, for_node->limit);
            if (interp_frame_pending(f)) return ItemNull;
            Scratch limit_slot(f);
            limit_slot.set(limit);
            // MIR applies unordered windows after the complete stream, so an
            // early exit would incorrectly suppress body effects on later rows.
            selected.set(for_node->limit_from_end
                ? fn_take_last(selected.get(), limit_slot.get())
                : fn_take(selected.get(), limit_slot.get()));
        }
        return selected.get();
    }
    Item result = array_end(out);
    // array_end reports an all-empty comprehension as spreadable-null; a
    // top-level for-expression yields a real empty array instead.
    if (result.item == ITEM_NULL_SPREADABLE) return interp_ptr_item(out);
    return result;
}

// Mirrors transpile_element: allocate the element, fill its attributes from a
// rooted span (same order as the map literal — values first, container after),
// then push content through list_push_spread and finalize with list_end. The
// element pointer itself is the value; list_end only closes its content frame.
static Item eval_element(InterpFrame* f, AstElementNode* node) {
    TypeElmt* type = (TypeElmt*)node->type;
    if (!type) return ItemError;

    int attr_count = 0;
    for (AstNode* a = node->item; a; a = a->next) attr_count++;

    RootSpan attrs((size_t)(attr_count > 0 ? attr_count : 1));
    uint64_t* attr_words = attrs.words();
    int ai = 0;
    for (AstNode* a = node->item; a; a = a->next) {
        AstNode* value_node = a->node_type == AST_NODE_KEY_EXPR
            ? ((AstNamedNode*)a)->as : a;
        attr_words[ai++] = value_node ? eval_expr(f, value_node).item : ItemNull.item;
        if (interp_frame_pending(f)) return ItemNull;
    }

    Element* fresh = elmt_with_tl(type->type_index, f->module->type_list);
    if (!fresh) return ItemError;
    Scratch acc(f);
    acc.set(interp_ptr_item(fresh));
    if (attr_count > 0) {
        elmt_fill_items((Element*)(uintptr_t)acc.get().item,
            (const Item*)(void*)attr_words, ai);
    }

    if (node->content) {
        for (AstNode* c = node->content; c; c = c->next) {
            Item value = eval_expr(f, c);
            if (interp_frame_pending(f)) return acc.get();
            // Re-read the element: content evaluation is a safepoint.
            Element* owner = (Element*)(uintptr_t)acc.get().item;
            if (!owner) return ItemError;
            list_push_spread((List*)owner, value);
        }
        list_end((List*)(uintptr_t)acc.get().item);
    } else if (node->item) {
        // Attributes but no content still closes the element's content frame.
        list_end((List*)(uintptr_t)acc.get().item);
    }
    return acc.get();
}

// ---------------------------------------------------------------------------
// Content blocks
// ---------------------------------------------------------------------------

// Mirrors transpile_content's split: declarations bind, side-effect statements
// run for effect, and the value expressions form the block's result — one
// value passes through, several accumulate into a list.
static Item eval_content(InterpFrame* f, AstListNode* list_node, bool hoist_functions) {
    // Procedural context, exactly as transpile_content decides it: inside a `pn`
    // body, or any block declaring a `var`. It matters because a proc block's
    // value is its LAST value expression only — every earlier one is a
    // statement. Without this, `pn main() { print(a) … "done" }` accumulates
    // each intermediate result into the block's list instead of discarding it.
    bool is_proc = false;
    if (f->fn) {
        TypeFunc* signature = (TypeFunc*)((AstNode*)f->fn)->type;
        is_proc = ((AstNode*)f->fn)->node_type == AST_NODE_PROC ||
            (signature && signature->type_id == LMD_TYPE_FUNC && signature->is_proc);
    }
    if (!is_proc) {
        for (AstNode* scan = list_node->item; scan; scan = scan->next) {
            if (scan->node_type == AST_NODE_VAR_STAM) { is_proc = true; break; }
        }
    }

    int value_count = 0, decl_count = 0, stam_count = 0;
    AstNode* last_value = NULL;
    if (is_proc) {
        AstNode* last_executable = NULL;
        for (AstNode* scan = list_node->item; scan; scan = scan->next) {
            if (!is_declaration_node(scan->node_type)) last_executable = scan;
        }
        for (AstNode* item = list_node->item; item; item = item->next) {
            if (is_declaration_node(item->node_type)) { decl_count++; continue; }
            if (is_side_effect_stam(item->node_type) ||
                    is_proc_flow_side_effect_node(item, last_executable) ||
                    item->node_type == AST_NODE_WHILE_STAM ||
                    item->node_type == AST_NODE_FOR_STAM) {
                stam_count++;
                continue;
            }
            value_count++;
            last_value = item;
        }
    } else {
        for (AstNode* item = list_node->item; item; item = item->next) {
            if (is_declaration_node(item->node_type)) { decl_count++; continue; }
            if (is_side_effect_stam(item->node_type)) { stam_count++; continue; }
            value_count++;
            last_value = item;
        }
    }

    // In a proc block with several values, only the last is the block's value;
    // the rest run for effect. That collapses to the single-value shape below.
    if (is_proc && value_count > 1) value_count = 1;
    // transpile_content's block-expression shortcut deliberately excludes a
    // lone `for`: its result is spreadable and must go through list_push_spread
    // so the stream flattens into the block instead of nesting one level.
    bool direct_value = value_count == 1 && last_value &&
        ((decl_count == 0 && stam_count == 0) ||
         (last_value->node_type != AST_NODE_FOR_EXPR &&
          last_value->node_type != AST_NODE_FOR_STAM));

    // build_content's pass 1 registers every top-level `fn`/`pn` before any
    // body is built, so a call may legally precede the textual definition.
    if (hoist_functions) {
        for (AstNode* item = list_node->item; item; item = item->next) {
            if (item->node_type == AST_NODE_FUNC || item->node_type == AST_NODE_PROC ||
                    item->node_type == AST_NODE_FUNC_EXPR) {
                exec_declaration(f, item);
            }
        }
    }

    if (value_count == 0 || direct_value) {
        Item result = value_count == 0 ? list_end(list()) : ItemNull;
        for (AstNode* item = list_node->item; item; item = item->next) {
            if (is_declaration_node(item->node_type)) {
                bool already_hoisted = hoist_functions &&
                    (item->node_type == AST_NODE_FUNC || item->node_type == AST_NODE_PROC ||
                     item->node_type == AST_NODE_FUNC_EXPR);
                if (!already_hoisted) exec_declaration(f, item);
            } else if (is_side_effect_stam(item->node_type)) {
                Item side_effect = eval_expr(f, item);
                interp_propagate_handler_error(f, item, side_effect);
                interp_propagate_proc_side_effect_error(f, item, side_effect);
            } else if (item == last_value) {
                result = eval_expr(f, item);
            } else if (item->node_type == AST_NODE_FOR_STAM) {
                eval_for(f, (AstForNode*)item, false);   // statement: stream discarded
            } else {
                Item side_effect = eval_expr(f, item);   // side effect only
                interp_propagate_handler_error(f, item, side_effect);
                interp_propagate_proc_side_effect_error(f, item, side_effect);
            }
            // break / continue / return / error-skip abandon the rest of the
            // block; the enclosing loop or call frame consumes the signal.
            if (interp_frame_pending(f)) return result;
        }
        return result;
    }

    Scratch acc(f);
    acc.set(interp_ptr_item(list()));
    for (AstNode* item = list_node->item; item; item = item->next) {
        if (is_declaration_node(item->node_type)) {
            bool already_hoisted = hoist_functions &&
                (item->node_type == AST_NODE_FUNC || item->node_type == AST_NODE_PROC ||
                 item->node_type == AST_NODE_FUNC_EXPR);
            if (!already_hoisted) exec_declaration(f, item);
            if (interp_frame_pending(f)) return acc.get();
            continue;
        }
        if (is_side_effect_stam(item->node_type)) {
            Item side_effect = eval_expr(f, item);
            interp_propagate_handler_error(f, item, side_effect);
            interp_propagate_proc_side_effect_error(f, item, side_effect);
            if (interp_frame_pending(f)) return acc.get();
            continue;
        }
        Item value = eval_expr(f, item);
        if (interp_frame_pending(f)) return acc.get();
        List* ls = (List*)(uintptr_t)acc.get().item;   // re-read: push may collect
        if (!ls) return ItemError;
        list_push_spread(ls, value);
    }
    List* ls = (List*)(uintptr_t)acc.get().item;
    return list_end(ls);
}

// A list block is not a content block: `(let x = …, body)` keeps its bindings
// in `declare` and its values in `item`, and it never runs side-effect
// statements. Mirrors transpile_list.
static Item eval_list(InterpFrame* f, AstListNode* list_node) {
    for (AstNode* decl = list_node->declare; decl; decl = decl->next) {
        if (decl->node_type == AST_NODE_ASSIGN) {
            AstNamedNode* named = (AstNamedNode*)decl;
            Item bound = eval_expr(f, named->as);
            if (interp_frame_pending(f)) return bound;
            bound = interp_coerce_declared_numeric(f, bound, named->declared_type);
            interp_write_binding(f, named->entry, bound);
        } else if (decl->node_type == AST_NODE_DECOMPOSE) {
            exec_declaration(f, decl);
            if (interp_frame_pending(f)) return ItemNull;
        }
    }
    int val_count = 0;
    AstNode* last_value = NULL;
    for (AstNode* item = list_node->item; item; item = item->next) {
        if (is_declaration_node(item->node_type)) continue;
        val_count++;
        last_value = item;
    }
    if (list_node->declare && val_count == 1) {
        return eval_expr(f, last_value);   // block expression: yield the value directly
    }

    Scratch acc(f);
    acc.set(interp_ptr_item(list()));
    for (AstNode* item = list_node->item; item; item = item->next) {
        if (is_declaration_node(item->node_type)) { exec_declaration(f, item); continue; }
        Item value = eval_expr(f, item);
        if (interp_frame_pending(f)) return acc.get();
        List* ls = (List*)(uintptr_t)acc.get().item;   // re-read: push may collect
        if (!ls) return ItemError;
        list_push_spread(ls, value);
    }
    return list_end((List*)(uintptr_t)acc.get().item);
}

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

static bool interp_exec_decompose(InterpFrame* f, AstDecomposeNode* dec) {
    if (!f || !dec || !dec->as || dec->name_count < 1 || !dec->entries) {
        log_error("interp: malformed decomposition declaration");
        return false;
    }
    Scratch source(f);
    source.set(eval_expr(f, dec->as));
    if (interp_frame_pending(f)) return false;
    for (int i = 0; i < dec->name_count; i++) {
        NameEntry* entry = dec->entries[i];
        String* name = dec->names ? dec->names[i] : NULL;
        if (!entry || !name) {
            log_error("interp: decomposition target %d has no binding", i);
            return false;
        }
        // MIR evaluates the source once, then extracts positional values with
        // item_at or named values with item_attr. Keep the source in a scratch
        // root across every extraction because either helper can allocate.
        Item value = dec->is_named
            ? item_attr(source.get(), name->chars)
            : item_at(source.get(), i);
        interp_write_binding(f, entry, value);
    }
    return true;
}

static void exec_declaration(InterpFrame* f, AstNode* node) {
    switch (node->node_type) {
    case AST_NODE_TYPE_STAM:
        // `type T = …` binds a compile-time name. The Type* graph is already
        // built and every use site resolves through it, so there is nothing to
        // evaluate or store at run time — the same reason lowering emits no
        // value for a type declaration.
        break;
    case AST_NODE_LET_STAM:
    case AST_NODE_PUB_STAM:
    case AST_NODE_VAR_STAM: {
        for (AstNode* decl = ((AstLetNode*)node)->declare; decl; decl = decl->next) {
            if (decl->node_type == AST_NODE_DECOMPOSE) {
                if (!interp_exec_decompose(f, (AstDecomposeNode*)decl)) return;
                continue;
            }
            if (decl->node_type != AST_NODE_ASSIGN) continue;
            AstNamedNode* named = (AstNamedNode*)decl;
            Item value = eval_expr(f, named->as);
            if (interp_frame_pending(f)) return;
            // A declared `float` binding is a coercion boundary (S7.7.2):
            // lowering stores the initializer in a double lane, so
            // `let x: float = 7 div 2` observes 3.0, not the int 3. Only the
            // lanes whose boxed form already agrees reach here (interp_plan.cpp
            // gates the rest), so this one conversion closes the gap.
            // Aliasing an owned container is an ownership boundary: the root
            // must be marked shared before any later write, or array_set_cow /
            // map_set_cow would see an unshared owner and mutate in place, so
            // the aliased `let` would observe the write (proc/let_finality.ls).
            // Same helper and same condition lowering uses at its `cow_binding`
            // site, minus the register bookkeeping T0 has no use for (AI3).
            AstNode* init = ast_unwrap_primary(named->as);
            if (init && init->node_type == AST_NODE_IDENT) {
                NameEntry* src = ((AstIdentNode*)init)->entry;
                TypeId init_tid = named->as->type
                    ? named->as->type->type_id : LMD_TYPE_ANY;
                TypeId var_tid = named->declared_type
                    ? named->declared_type->type_id : LMD_TYPE_ANY;
                if (ast_declared_type_is_open_any_array(named->declared_type)) {
                    // a repeated `any` annotation is carried as a TypeUnary,
                    // not LMD_TYPE_ARRAY. Restore its runtime container shape
                    // before the alias boundary, or COW would leave both
                    // annotated bindings pointing at the mutable same array.
                    var_tid = LMD_TYPE_ARRAY;
                }
                bool declared_open_any_array =
                    ast_declared_type_is_open_any_array(named->declared_type);
                if (src && src->cow_owned && (declared_open_any_array ||
                        ast_expr_may_return_container(named->as, init_tid, var_tid))) {
                    // cow_bind_var may detach a copy, so it is a safepoint: the
                    // operand has to be reachable from a frame slot, not a C++
                    // local, or a collection during the clone frees it.
                    Scratch alias_slot(f);
                    alias_slot.set(value);
                    value = cow_bind_var(alias_slot.get());
                }
            }
            Type* decl_type = named->declared_type;
            if (decl_type && decl_type->type_id == LMD_TYPE_FLOAT &&
                    get_type_id(value) != LMD_TYPE_FLOAT) {
                TypeId vt = get_type_id(value);
                if (vt == LMD_TYPE_INT || vt == LMD_TYPE_INT64) {
                    value = push_d((double)it2l(value));
                }
            }
            value = interp_coerce_declared_numeric(f, value, decl_type);
            interp_write_binding(f, named->entry, value);
        }
        break;
    }
    case AST_NODE_DECOMPOSE:
        (void)interp_exec_decompose(f, (AstDecomposeNode*)node);
        break;
    case AST_NODE_FUNC:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_PROC: {
        AstFuncNode* fn_node = (AstFuncNode*)node;
        NameEntry* entry = fn_node->analysis ? fn_node->analysis->decl_entry : NULL;
        if (!entry) break;   // anonymous definitions bind nothing
        Function* fn = interp_make_closure(f->module, fn_node, f);
        if (!fn) break;
        interp_write_binding(f, entry, interp_ptr_item(fn));
        break;
    }
    default:
        // TYPE_STAM / OBJECT_TYPE / patterns / views are P1 work; the pre-scan
        // has already routed scripts containing them to the JIT fallback.
        break;
    }
}

// ---------------------------------------------------------------------------
// Expression dispatch
// ---------------------------------------------------------------------------

// AST_NODE_START (541) and AST_NODE_EVENT_HANDLER (542) are distinct values;
// pin that here so a future renumbering cannot silently merge the two arms.
static_assert((int)AST_NODE_START != (int)AST_NODE_EVENT_HANDLER,
    "AST_NODE_START and AST_NODE_EVENT_HANDLER must stay distinguishable");

// Dynamic root/parent navigation may retain lineage only across a direct
// member/index chain. The carrier is activation state, never a field on the
// resulting Lambda value.
static bool interp_navigation_chain_has_current(AstNode* node) {
    node = ast_unwrap_primary(node);
    if (!node) return false;
    if (node->node_type == AST_NODE_CURRENT_ITEM) return true;
    if (node->node_type == AST_NODE_NAVIGATION_EXPR) {
        return interp_navigation_chain_has_current(((AstNavigationNode*)node)->object);
    }
    if (node->node_type != AST_NODE_MEMBER_EXPR &&
            node->node_type != AST_NODE_INDEX_EXPR) return false;
    return interp_navigation_chain_has_current(((AstFieldNode*)node)->object);
}

static AstNode* interp_navigation_direct_parent(AstNode* node) {
    node = ast_unwrap_primary(node);
    if (!node || (node->node_type != AST_NODE_MEMBER_EXPR &&
            node->node_type != AST_NODE_INDEX_EXPR)) return NULL;
    AstFieldNode* field = (AstFieldNode*)node;
    return interp_navigation_chain_has_current(field->object)
        ? field->object : NULL;
}

static Item eval_expr(InterpFrame* f, AstNode* node) {
    if (!node) return ItemNull;
    f->cur = node;
    f->st->node_count++;
    if (f->st->mode != EvalMode::RUNTIME) {
        if (f->st->mode_fuel == 0) {
            f->st->mode_exhausted = true;
            return ItemError;
        }
        f->st->mode_fuel--;
    }

    switch (node->node_type) {
    case AST_NODE_PRIMARY: {
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        if (pri->expr) return eval_expr(f, pri->expr);
        return eval_literal(f, node);
    }
    case AST_NODE_IDENT: {
        AstIdentNode* ident = (AstIdentNode*)node;
        NameEntry* entry = ident->entry;
        // An unresolved identifier is an error value, not null: MIR emits the
        // same ItemError carrier so a surrounding handler can recover it.
        if (!entry) return ItemError;
        // A name bound by `type T = …` is a compile-time binding: it denotes
        // the Type* its declaration built, not a slab slot (which a type
        // declaration never writes). Lowering makes the same distinction in
        // mir_is_type_value_node's IDENT arm.
        AstNode* decl = entry ? entry->node : NULL;
        if (decl && decl->node_type == AST_NODE_ASSIGN &&
                ((AstNamedNode*)decl)->is_type_definition) {
            Type* declared = decl->type;
            TypeId tid = LMD_TYPE_ANY;
            TypeType* singleton = lambda_type_node_singleton(declared, &tid);
            if (singleton) return interp_ptr_item(singleton);
            return interp_ptr_item(declared ? declared : base_type(tid));
        }
        return interp_read_binding(f, entry);
    }
    case AST_NODE_UNARY:
        return eval_unary(f, (AstUnaryNode*)node);
    case AST_NODE_SPREAD:
        // `*expr` marks its operand spreadable; the collection builders flatten
        // it from there (transpile_spread is this same one call).
        return item_spread(eval_expr(f, ((AstSpreadNode*)node)->argument));
    case AST_NODE_BINARY:
        return eval_binary(f, (AstBinaryNode*)node);
    case AST_NODE_PIPE:
        return eval_pipe(f, (AstBinaryNode*)node);
    case AST_NODE_MATCH_EXPR:
        return eval_match(f, (AstMatchNode*)node);
    case AST_NODE_CURRENT_ITEM:
        return f->st->contexts
            ? (Item){.item = *f->st->contexts->item} : ItemNull;
    case AST_NODE_CURRENT_INDEX:
        return f->st->contexts
            ? (Item){.item = *f->st->contexts->index} : ItemNull;
    case AST_NODE_LAST_INDEX:
        if (!f->st->last_index_item) {
            log_error("interp: `last` used outside a subscript");
            return ItemError;
        }
        return int2it_i64(fn_len((Item){.item = *f->st->last_index_item}) - 1);
    case AST_NODE_CURRENT_ERROR:
        return f->st->errors
            ? (Item){.item = *f->st->errors->error} : ItemError;
    case AST_NODE_IF_EXPR: {
        AstIfNode* branch = (AstIfNode*)node;
        Item cond = eval_expr(f, branch->cond);
        // `if` belongs to the truthy error family: an error condition is
        // false, not an ordinary value-family propagation edge (S7.6).
        if (is_truthy(cond)) return eval_expr(f, branch->then);
        if (branch->otherwise) return eval_expr(f, branch->otherwise);
        return ItemNull;
    }
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)node;
        Item result = eval_call(f, call, NULL);
        if (call->propagate && !interp_frame_pending(f) && item_is_error(result)) {
            // '^' propagation: the error leaves through the function boundary
            // instead of flowing on as a value (the emit_return_if_item_error
            // placement lowering uses for this node).
            interp_signal(f, EvalSignal::RETURNED, result);
        }
        return result;
    }
    case AST_NODE_HANDLER_EXPR:
    case AST_NODE_HANDLER_STAM:
        return eval_handler(f, (AstHandlerNode*)node);
    case AST_NODE_MEMBER_EXPR: {
        AstFieldNode* field = (AstFieldNode*)node;
        Item object_value = eval_expr(f, field->object);
        Scratch obj(f);
        obj.set(object_value);
        Item key;
        if (field->field && field->field->node_type == AST_NODE_IDENT) {
            // A dotted member name is a static key, not an identifier read:
            // lowering interns it as a runtime-pool name Item (the property-key
            // path), and reading it as a binding would resolve to null.
            AstIdentNode* name = (AstIdentNode*)field->field;
            key = (Item){.item = s2it(heap_create_name(name->name->chars,
                name->name->len))};
        } else {
            key = eval_expr(f, field->field);
        }
        Scratch key_slot(f);
        key_slot.set(key);
        // A Path's built-in properties are read through item_attr, which loads
        // its metadata; fn_member does not, and would extend the path instead
        // (transpile_member makes the same split). The runtime type is the
        // authority here, exactly as the JIT's obj_tid check is.
        if (field->field && field->field->node_type == AST_NODE_IDENT &&
                get_type_id(obj.get()) == LMD_TYPE_PATH) {
            const char* k = ((AstIdentNode*)field->field)->name->chars;
            if (path_is_property_name(k)) return item_attr(obj.get(), k);
        }
        // null totality (S7.1.1) comes from the helper; both operands are
        // published first because it allocates.
        return fn_member(obj.get(), key_slot.get());
    }
    case AST_NODE_INDEX_EXPR: {
        AstFieldNode* field = (AstFieldNode*)node;
        Item object_value = eval_expr(f, field->object);
        Scratch obj(f);
        obj.set(object_value);
        InterpLastIndexGuard last_scope(f->st, obj.home());
        Item index_value = eval_expr(f, field->field);
        Scratch index_slot(f);
        index_slot.set(index_value);
        return fn_index(obj.get(), index_slot.get());
    }
    case AST_NODE_ARRAY:
        return eval_array(f, (AstArrayNode*)node);
    case AST_NODE_MAP:
        return eval_map(f, (AstMapNode*)node);
    case AST_NODE_ELEMENT:
        return eval_element(f, (AstElementNode*)node);

    // ---- P1.3: paths and queries ----
    case AST_NODE_PATH_EXPR: {
        // path_new + one path_extend / wildcard per segment. Path* is a
        // container pointer, so it is its own Item carrier.
        AstPathNode* path_node = (AstPathNode*)node;
        Pool* pool = f->st->ctx->pool;
        Scratch acc(f);
        Path* initial = path_node->authority
            ? path_new_authority(pool, (int)path_node->scheme, path_node->authority->chars)
            : path_new(pool, (int)path_node->scheme);
        acc.set(interp_ptr_item(initial));
        for (int i = 0; i < path_node->segment_count; i++) {
            AstPathSegment* seg = &path_node->segments[i];
            // Re-read the accumulator: every extension allocates.
            Path* base = (Path*)(uintptr_t)acc.get().item;
            Path* next;
            if (seg->type == LPATH_SEG_WILDCARD) {
                next = path_wildcard(pool, base);
            } else if (seg->type == LPATH_SEG_WILDCARD_REC) {
                next = path_wildcard_recursive(pool, base);
            } else if (seg->type == LPATH_SEG_PARENT) {
                next = path_select_parent(pool, base);
            } else if (seg->type == LPATH_SEG_ROOT) {
                next = path_select_root(pool, base);
            } else if (seg->type == LPATH_SEG_INT) {
                next = path_extend_int(pool, base, seg->int_value);
            } else {
                next = path_extend(pool, base, seg->name ? seg->name->chars : "");
            }
            acc.set(interp_ptr_item(next));
        }
        return acc.get();
    }
    case AST_NODE_PATH_INDEX_EXPR: {
        AstPathIndexNode* pix = (AstPathIndexNode*)node;
        Item base_value = eval_expr(f, pix->base_path);
        if (interp_frame_pending(f)) return base_value;
        Scratch base(f);
        base.set(base_value);
        Item segment = eval_expr(f, pix->segment_expr);
        if (interp_frame_pending(f)) return segment;
        Scratch seg_slot(f);
        seg_slot.set(segment);
        Item key = seg_slot.get();
        Path* base_path = (Path*)(uintptr_t)base.get().item;
        if (get_type_id(key) == LMD_TYPE_INT && key.int_val >= 0) {
            return interp_ptr_item(path_extend_int(f->st->ctx->pool, base_path, key.int_val));
        }
        if (get_type_id(key) == LMD_TYPE_INT64 && key.get_int64() >= 0) {
            return interp_ptr_item(path_extend_int(f->st->ctx->pool, base_path, key.get_int64()));
        }
        const char* text = fn_to_cstr(key);
        return interp_ptr_item(path_extend(f->st->ctx->pool, base_path, text));
    }
    case AST_NODE_NAVIGATION_EXPR: {
        AstNavigationNode* nav = (AstNavigationNode*)node;
        if (!nav->object) return ItemNull;
        Item object = eval_expr(f, nav->object);
        if (interp_frame_pending(f)) return object;
        if (get_type_id(object) == LMD_TYPE_PATH) {
            Path* path = (Path*)(uintptr_t)object.item;
            Path* result = nav->root
                ? path_select_root(f->st->ctx->pool, path)
                : path_select_parent(f->st->ctx->pool, path);
            return interp_ptr_item(result);
        }
        // The contextual atom (`~`) carries the active occurrence relation in
        // the interpreter context. Keep that relation out of Lambda values;
        // the navigation node consumes the rooted activation slots directly.
        if (f->st->contexts && interp_navigation_chain_has_current(nav->object)) {
            InterpContext* context = f->st->contexts;
            if (nav->root) {
                return context->root
                    ? (Item){.item = *context->root} : object;
            }
            AstNode* direct_parent = interp_navigation_direct_parent(nav->object);
            if (direct_parent) {
                Scratch parent(f);
                parent.set(eval_expr(f, direct_parent));
                return parent.get();
            }
            AstNode* nav_object = ast_unwrap_primary(nav->object);
            if (nav_object && nav_object->node_type == AST_NODE_NAVIGATION_EXPR) {
                AstNavigationNode* inner = (AstNavigationNode*)nav_object;
                AstNode* inner_object = ast_unwrap_primary(inner->object);
                // A second parent after bare `~~` has no defined relation;
                // a member/index occurrence's first parent is still `~`, so
                // its next parent is the active context parent.
                if (inner->root || (inner_object &&
                        inner_object->node_type == AST_NODE_CURRENT_ITEM)) {
                    return ItemNull;
                }
            }
            return context->parent
                ? (Item){.item = *context->parent} : ItemNull;
        }
        // Dynamic occurrence lineage is added in the navigation phase. A
        // non-path value without a cursor has no parent/root relation.
        return nav->root ? object : ItemNull;
    }
    case AST_NODE_QUERY_EXPR: {
        AstQueryNode* query = (AstQueryNode*)node;
        Item object = eval_expr(f, query->object);
        if (interp_frame_pending(f)) return object;
        Scratch obj(f);
        obj.set(object);
        Item type_value = eval_expr(f, query->query);
        if (interp_frame_pending(f)) return type_value;
        Scratch type_slot(f);
        type_slot.set(type_value);
        return fn_query(obj.get(), type_slot.get(), query->direct ? 1 : 0);
    }
    case AST_NODE_FOR_EXPR:
    case AST_NODE_FOR_STAM:
        // Reached as an expression, a `for` always yields its stream — the
        // discard decision belongs to the enclosing content block, exactly as
        // transpile_expr defers it to transpile_content.
        return eval_for(f, (AstForNode*)node, true);
    case AST_NODE_CONTENT:
        return eval_content(f, (AstListNode*)node, false);
    case AST_NODE_LIST:
        return eval_list(f, (AstListNode*)node);
    case AST_NODE_FUNC:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_PROC: {
        Function* fn = interp_make_closure(f->module, (AstFuncNode*)node, f);
        return fn ? interp_ptr_item(fn) : ItemError;
    }
    case AST_NODE_KEY_EXPR:
    case AST_NODE_ASSIGN:
        return eval_expr(f, ((AstNamedNode*)node)->as);
    case AST_NODE_LET_STAM:
    case AST_NODE_PUB_STAM:
    case AST_NODE_VAR_STAM:
    case AST_NODE_TYPE_STAM:
        exec_declaration(f, node);
        return ItemNull;

    // ---- procedural statements (S12.1.2): EvalSignal is the only non-local
    // mechanism for language control flow (AI14); longjmp stays fault-only ----
    case AST_NODE_ASSIGN_STAM: {
        AstAssignStamNode* assign = (AstAssignStamNode*)node;
        Item value = eval_expr(f, assign->value);
        if (interp_frame_pending(f)) return ItemNull;
        AstNode* rhs = ast_unwrap_primary(assign->value);
        bool fresh_rhs_error = item_is_error(value) && rhs &&
            rhs->node_type != AST_NODE_CURRENT_ERROR &&
            rhs->node_type != AST_NODE_CURRENT_ITEM;
        NameEntry* target = assign->target_entry;
        if (!target && assign->left && assign->left->node_type == AST_NODE_IDENT) {
            target = ((AstIdentNode*)assign->left)->entry;
        }
        if (!target) {
            log_error("interp: assignment target has no binding");
            return ItemError;
        }
        if (fresh_rhs_error && target->declared_type &&
                !lambda_type_accepts_error(target->declared_type)) {
            // A checked binding rejects a fresh RHS error before publishing it,
            // but an untyped binding must retain that ItemError so a later
            // error-excluding `var` call short-circuits before COW mutation.
            return value;
        }
        value = interp_coerce_declared_numeric(f, value, target->declared_type);
        interp_write_binding(f, target, value);
        return fresh_rhs_error ? value : ItemNull;
    }
    case AST_NODE_INDEX_ASSIGN_STAM:
    case AST_NODE_MEMBER_ASSIGN_STAM: {
        // `obj.field = v` / `arr[i] = v` where the target root is a plain
        // binding. The *_cow helpers own S9.1.2: they hand back the owner to
        // publish, which is a fresh private copy when the old one was shared,
        // so COW stays unobservable without the walker reasoning about sharing.
        // Nested paths use cow_path_set below, which detaches and relinks the
        // complete owner spine before its replacement root is published.
        AstCompoundAssignNode* ca = (AstCompoundAssignNode*)node;
        AstCowPath path = {};
        bool has_path = ast_collect_cow_path(&path, ca->object);
        NameEntry* root = has_path && path.root &&
                path.root->node_type == AST_NODE_IDENT
            ? ((AstIdentNode*)path.root)->entry : NULL;
        if (!root) {
            log_error("interp: compound assignment target is not a simple binding");
            return ItemError;
        }

        if (path.count > 0) {
            // MIR snapshots the RHS before resolving a COW owner spine. A
            // nested key or child detach can allocate, so hold both the value
            // and every collected key in frame slots before cow_path_set.
            Scratch value_slot(f);
            value_slot.set(eval_expr(f, ca->value));
            if (interp_frame_pending(f)) return value_slot.get();

            Scratch path_slot(f);
            path_slot.set(interp_ptr_item(array_plain()));
            for (int i = 0; i < path.count; i++) {
                Scratch key_slot(f);
                key_slot.set(interp_eval_cow_path_key(f, path.segment[i],
                    path.is_member[i]));
                if (interp_frame_pending(f)) return key_slot.get();
                Array* keys = (Array*)(uintptr_t)path_slot.get().item;
                if (!keys) return ItemError;
                array_push(keys, key_slot.get());
            }
            Scratch terminal_slot(f);
            terminal_slot.set(interp_eval_cow_path_key(f, ca->key,
                node->node_type == AST_NODE_MEMBER_ASSIGN_STAM));
            if (interp_frame_pending(f)) return terminal_slot.get();
            Array* keys = (Array*)(uintptr_t)path_slot.get().item;
            if (!keys) return ItemError;
            array_push(keys, terminal_slot.get());

            Scratch owner_slot(f);
            owner_slot.set(interp_read_binding(f, root));
            Item replacement = cow_path_set(owner_slot.get(), path_slot.get(),
                value_slot.get());
            if (item_is_error(replacement)) return replacement;
            interp_write_binding(f, root, replacement);
            return ItemNull;
        }
        // A RHS call can detach and publish this same root through a `var`
        // parameter. Mirror MIR's COW branch: root lookup happens only after
        // the RHS and key have completed, never from a stale pre-call owner.
        Scratch value_slot(f);
        Item value = eval_expr(f, ca->value);
        if (interp_frame_pending(f)) return ItemNull;
        value_slot.set(value);

        Scratch key_slot(f);
        key_slot.set(interp_eval_cow_path_key(f, ca->key,
            node->node_type == AST_NODE_MEMBER_ASSIGN_STAM));
        if (interp_frame_pending(f)) return ItemNull;

        Scratch owner(f);
        owner.set(interp_read_binding(f, root));

        // Dispatch on the owner's runtime type, not the syntax: `m["k"] = v`
        // is an INDEX_ASSIGN over a map, and lowering picks the setter by owner
        // type too. Each *_cow entry rejects a mismatched owner itself.
        Item replacement;
        if (ast_declared_type_is_map(root->declared_type)) {
            // a typed map write validates a detached candidate before it is
            // visible. Explicit `var` parameters were detached at the caller
            // boundary, so their private root can use the in-place contract.
            const char* boundary = node->node_type == AST_NODE_MEMBER_ASSIGN_STAM
                ? "typed map member assignment" : "typed map computed assignment";
            replacement = root->is_var_param
                ? lambda_map_set_checked_inplace(owner.get(), key_slot.get(),
                    value_slot.get(), root->declared_type, boundary)
                : lambda_map_set_checked(owner.get(), key_slot.get(),
                    value_slot.get(), root->declared_type, boundary);
        } else {
            switch (get_type_id(owner.get())) {
            case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_NUM: case LMD_TYPE_ELEMENT:
                replacement = array_set_cow(owner.get(), it2l(key_slot.get()),
                    value_slot.get());
                break;
            case LMD_TYPE_VMAP:
                replacement = vmap_set_cow(owner.get(), key_slot.get(), value_slot.get());
                break;
            default:
                replacement = map_set_cow(owner.get(), key_slot.get(), value_slot.get());
                break;
            }
        }
        if (item_is_error(replacement)) return replacement;
        // Publish the (possibly new) owner back at its binding.
        interp_write_binding(f, root, replacement);
        return ItemNull;
    }
    case AST_NODE_WHILE_STAM:
    case AST_NODE_DO_WHILE_STAM: {
        AstWhileNode* loop = (AstWhileNode*)node;
        bool test_first = node->node_type == AST_NODE_WHILE_STAM;
        for (;;) {
            if (test_first) {
                Item cond = eval_expr(f, loop->cond);
                if (interp_frame_pending(f)) break;
                if (item_is_error(cond)) return cond;
                if (!is_truthy(cond)) break;
            }
            eval_expr(f, loop->body);
            if (f->signal == EvalSignal::BROKE) { interp_clear_loop_signal(f); break; }
            if (f->signal == EvalSignal::CONTINUED) interp_clear_loop_signal(f);
            // RETURNED and ERROR_SKIP keep unwinding past this loop.
            else if (interp_frame_pending(f)) break;
            if (!test_first) {
                Item cond = eval_expr(f, loop->cond);
                if (interp_frame_pending(f)) break;
                if (item_is_error(cond)) return cond;
                if (!is_truthy(cond)) break;
            }
            interp_note_backedge(f);
        }
        return ItemNull;   // loops are statements; their value is never used
    }
    case AST_NODE_BREAK_STAM:
        interp_signal(f, EvalSignal::BROKE, ItemNull);
        return ItemNull;
    case AST_NODE_CONTINUE_STAM:
        interp_signal(f, EvalSignal::CONTINUED, ItemNull);
        return ItemNull;
    case AST_NODE_RETURN_STAM: {
        AstReturnNode* ret = (AstReturnNode*)node;
        Item value = ret->value ? eval_expr(f, ret->value) : ItemNull;
        if (interp_frame_pending(f)) return value;
        interp_signal(f, EvalSignal::RETURNED, value);
        return value;
    }
    case AST_NODE_RAISE_STAM:
    case AST_NODE_RAISE_EXPR: {
        // `raise` unwinds to the function boundary carrying its error value —
        // the same shape lowering gives it via emit_function_error_return
        // (S7.4.2). It travels as RETURNED so an enclosing loop cannot swallow
        // it, exactly as break/continue are the only loop-scoped signals.
        AstRaiseNode* raise_node = (AstRaiseNode*)node;
        Item value = raise_node->value ? eval_expr(f, raise_node->value) : ItemError;
        if (interp_frame_pending(f)) return value;
        interp_signal(f, EvalSignal::RETURNED, value);
        return value;
    }
    // A type expression evaluates to its build-time Type* as a direct-pointer
    // Item — the same carrier fn_is/fn_query expect. The Type* graph is built
    // by the frontend; T0 constructs no types of its own (§P1.2).
    case AST_NODE_TYPE: {
        TypeId tid = LMD_TYPE_ANY;
        TypeType* singleton = lambda_type_node_singleton(node->type, &tid);
        return interp_ptr_item(singleton ? (Type*)singleton : base_type(tid));
    }
    case AST_NODE_BINARY_TYPE:
    case AST_NODE_UNARY_TYPE:
    case AST_NODE_CONTENT_TYPE:
    case AST_NODE_LIST_TYPE:
    case AST_NODE_ARRAY_TYPE:
    case AST_NODE_MAP_TYPE:
    case AST_NODE_ELMT_TYPE:
    case AST_NODE_FUNC_TYPE:
        // Composite type expressions already carry their resolved Type* on the
        // node; lowering emits that pointer, so the walker publishes it too.
        return interp_ptr_item(node->type);
    case AST_NODE_CONSTRAINED_TYPE: {
        AstConstrainedTypeNode* constrained = (AstConstrainedTypeNode*)node;
        TypeConstrained* type = (TypeConstrained*)constrained->type;
        // The type-list entry is the Type* identity lowering publishes; the
        // build-time AST node is not a runtime type value on its own.
        return interp_ptr_item(type ? const_type(type->type_index) : &LIT_TYPE_ERROR);
    }
    case AST_NODE_SYS_FUNC:
        // Bare system functions are first-class identity values. Mirror the
        // MIR `to_sys_fn_named` wrapper so equality and `name(len)` observe the
        // same callable metadata instead of turning the reference into an
        // unrelated interpreter error.
        {
            AstSysFuncNode* sys = (AstSysFuncNode*)node;
            SysFuncInfo* info = sys->fn_info;
            if (!info || !info->func_ptr) return ItemError;
            Function* fn = to_sys_fn_named(info->func_ptr, info->arg_count,
                info->name);
            return interp_ptr_item(fn);
        }
    default:
        log_error("interp: unhandled node kind %s",
            interp_node_kind_name(node->node_type));
        return ItemError;
    }
}

// ---------------------------------------------------------------------------
// Calls into interpreted functions
// ---------------------------------------------------------------------------

static InterpState* interp_current_state(void);

// Loop counters belong to the active definition, but promotion is deferred
// until its next entry: an interpreter frame can never be materialized into a
// satellite's native locals (no OSR, D8.1.1v2 §5.1).
static void interp_note_backedge(InterpFrame* frame) {
    if (!frame || !frame->fn || lambda_tier_selected() != LAMBDA_TIER_AUTO ||
            !frame->fn->analysis) return;
    FnPromotionCell* cell = &frame->fn->analysis->promotion;
    if (cell->state == FN_PROMOTION_INTERP && cell->backedge_count != UINT32_MAX) {
        cell->backedge_count++;
    }
}

static Item interp_rejected_parameter_error(const TypeFunc* signature,
        const Item* args, int argc) {
    if (!signature || !args) return ItemNull;
    const TypeParam* param = signature->param;
    for (int index = 0; param && index < argc; param = param->next, index++) {
        if (param->contract_type && !lambda_type_accepts_error(param->contract_type) &&
                item_is_error(args[index])) {
            // Reject before frame entry: MIR preserves this ItemError at the
            // caller boundary, so the callee cannot turn it into a value.
            return args[index];
        }
    }
    return ItemNull;
}

typedef struct InterpBorrowedCall {
    InterpFrame* caller;
    NameEntry* entries[LAMBDA_MAX_FUNCTION_ARGS];
} InterpBorrowedCall;

static Item interp_call_internal(Function* fn, const Item* args, int argc,
        const InterpBorrowedCall* borrowed) {
    InterpState* st = interp_current_state();
    if (!st) {
        log_error("interp: no interpreter state for an interpreted call");
        return ItemError;
    }
    const AstFuncNode* fn_node = (const AstFuncNode*)fn->def;
    Script* module = fn->def_module;
    if (!fn_node || !module || !fn_node->analysis ||
            !fn_node->analysis->frame_plan.planned) {
        log_error("interp: interpreted function '%s' has no frame plan",
            fn->name ? fn->name : "<anonymous>");
        return ItemError;
    }
    TypeFunc* signature = (TypeFunc*)fn->fn_type;
    Item rejected = interp_rejected_parameter_error(signature, args, argc);
    if (item_is_error(rejected)) return rejected;
    if (st->depth == 0) {
        // The interpreter budget is a language/runtime completion, not the
        // native stack-fault carve-out. Return it through this call frame so a
        // caller handler can consume it without a non-local jump.
        st->depth_exhausted = true;
        log_error("interp: recursion depth budget %u exhausted in '%s'",
            st->depth_limit, fn->name ? fn->name : "<anonymous>");
        LambdaError* error = err_create_heap(ERR_STACK_OVERFLOW,
            "Stack overflow", NULL);
        return error ? err2it(error) : ItemError;
    }

    st->depth--;
    Item result = ItemNull;
    TypeId escaped_scalar_type = LMD_TYPE_NULL;
    uint64_t escaped_scalar_payload = 0;
    Item borrowed_values[LAMBDA_MAX_FUNCTION_ARGS] = {};
    TypeId borrowed_scalar_types[LAMBDA_MAX_FUNCTION_ARGS] = {};
    uint64_t borrowed_scalar_payloads[LAMBDA_MAX_FUNCTION_ARGS] = {};
    {
        InterpFrameGuard guard(st, fn_node, module, &fn_node->analysis->frame_plan,
            (Item*)fn->closure_env, fn->closure_field_count);
        if (!guard.valid()) { st->depth++; return ItemError; }
        InterpFrame* frame = guard.frame();
        uint16_t params = fn_node->analysis->frame_plan.param_count;
        bool is_variadic = signature && signature->type_id == LMD_TYPE_FUNC &&
            signature->is_variadic;
        Item rest = is_variadic && argc > (int)params
            ? args[params] : ItemNull;
        InterpVargsGuard vargs(frame, rest);
        if (is_variadic && !vargs.valid()) return ItemError;
        int index = 0;
        for (AstNamedNode* p = fn_node->param; p && index < (int)params;
                p = (AstNamedNode*)((AstNode*)p)->next, index++) {
            Item value = index < argc ? args[index] : (Item){.item = ITEM_MISSING_ARGUMENT};
            if (value.item == ITEM_MISSING_ARGUMENT) {
                // The adapter marks an omitted optional; its default evaluates
                // in the callee frame, as the generated wrapper's default arm does.
                TypeParam* param_type = (TypeParam*)((AstNode*)p)->type;
                AstNode* fallback = param_type ? param_type->default_value : NULL;
                value = fallback ? eval_expr(frame, fallback) : ItemNull;
            }
            value = interp_coerce_declared_numeric(frame, value, p->declared_type);
            frame->slots[index] = value.item;
        }

        uint64_t iterations = 0;
        for (;;) {
            result = eval_expr(frame, fn_node->body);
            if (frame->signal != EvalSignal::TAIL_CALL) break;
            frame->signal = EvalSignal::NORMAL;
            // The parameter slots already hold the next iteration's arguments,
            // and scratch is released with each body evaluation, so the frame
            // is reused rather than re-opened — that is what keeps deep tail
            // recursion off the native stack.
            frame->scratch_top = frame->scratch_base;
            if (++iterations > LAMBDA_INTERP_TCO_MAX_ITERATIONS) {
                lambda_stack_overflow_error(fn->name ? fn->name : "<anonymous>");
                result = ItemError;
                break;
            }
        }
        // An explicit `return` unwinds to exactly this boundary and its payload
        // is the call's value; the signal never escapes the activation.
        if (frame->signal == EvalSignal::RETURNED) result = interp_signal_payload(frame);
        if (borrowed && borrowed->caller) {
            for (int index = 0; index < (int)params; index++) {
                if (!borrowed->entries[index]) continue;
                Item value = (Item){.item = frame->slots[index]};
                TypeId type = get_type_id(value);
                borrowed_scalar_types[index] = type;
                if (type == LMD_TYPE_INT64) {
                    borrowed_scalar_payloads[index] = (uint64_t)value.get_int64();
                    borrowed_values[index] = ItemNull;
                } else if (type == LMD_TYPE_UINT64) {
                    borrowed_scalar_payloads[index] = value.get_uint64();
                    borrowed_values[index] = ItemNull;
                } else {
                    borrowed_values[index] = scalar_storage_read(value, false);
                }
            }
        }
        // A u64/i64 Item points into the callee's number extent. Capturing
        // its payload before that extent closes, then boxing after the guard,
        // preserves both lifetime and its observable numeric type; the generic
        // scalar reader intentionally narrows small u64 values for property
        // reads, which is wrong at a function return boundary.
        escaped_scalar_type = get_type_id(result);
        if (escaped_scalar_type == LMD_TYPE_INT64) {
            escaped_scalar_payload = (uint64_t)result.get_int64();
            result = ItemNull;
        } else if (escaped_scalar_type == LMD_TYPE_UINT64) {
            escaped_scalar_payload = result.get_uint64();
            result = ItemNull;
        } else {
            escaped_scalar_type = LMD_TYPE_NULL;
            result = scalar_storage_read(result, false);
        }
    }
    if (escaped_scalar_type == LMD_TYPE_INT64) {
        result = box_int64_value((int64_t)escaped_scalar_payload);
    } else if (escaped_scalar_type == LMD_TYPE_UINT64) {
        result = box_uint64_value(escaped_scalar_payload);
    }
    if (borrowed && borrowed->caller) {
        for (int index = 0; index < LAMBDA_MAX_FUNCTION_ARGS; index++) {
            NameEntry* entry = borrowed->entries[index];
            if (!entry) continue;
            Item value = borrowed_scalar_types[index] == LMD_TYPE_INT64
                ? box_int64_value((int64_t)borrowed_scalar_payloads[index])
                : borrowed_scalar_types[index] == LMD_TYPE_UINT64
                    ? box_uint64_value(borrowed_scalar_payloads[index])
                    : borrowed_values[index];
            // The callee frame owns its param slot, but a `var` argument is
            // the caller's mutable root. Publish after the callee extent
            // closes so a wide scalar cannot retain a dead number-stack home.
            interp_write_binding(borrowed->caller, entry, value);
        }
    }
    st->depth++;
    return result;
}

static Item interp_call_with_borrowed(Function* fn, const Item* args, int argc,
        InterpFrame* caller, NameEntry* const* borrowed_entries) {
    InterpBorrowedCall borrowed = {};
    borrowed.caller = caller;
    for (int index = 0; index < LAMBDA_MAX_FUNCTION_ARGS; index++) {
        borrowed.entries[index] = borrowed_entries ? borrowed_entries[index] : NULL;
    }
    return interp_call_internal(fn, args, argc, &borrowed);
}

extern "C" Item interp_call(Function* fn, const Item* args, int argc) {
    return interp_call_internal(fn, args, argc, NULL);
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

static __thread InterpState* g_interp_state = NULL;
static InterpState* interp_current_state(void) { return g_interp_state; }

// const accepts only literal scalar syntax. The same eval_expr walker still
// performs the operation, but this narrow admission guarantees a fold cannot
// read a binding, call user code, allocate a container, or publish an effect.
static bool interp_const_node_supported(AstNode* node) {
    if (!node) return false;
    switch (node->node_type) {
    case AST_NODE_PRIMARY: {
        AstNode* expr = ((AstPrimaryNode*)node)->expr;
        if (expr) return interp_const_node_supported(expr);
        if (!node->type || !node->type->is_literal) return false;
        switch (node->type->type_id) {
        case LMD_TYPE_NULL:
        case LMD_TYPE_BOOL:
        case LMD_TYPE_INT:
        case LMD_TYPE_FLOAT:
        case LMD_TYPE_FLOAT64:
            return true;
        default:
            return false;
        }
    }
    case AST_NODE_UNARY: {
        Operator op = ((AstUnaryNode*)node)->op;
        return (op == OPERATOR_NOT || op == OPERATOR_NEG || op == OPERATOR_POS) &&
            interp_const_node_supported(((AstUnaryNode*)node)->operand);
    }
    case AST_NODE_BINARY: {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        switch (binary->op) {
        case OPERATOR_ADD: case OPERATOR_SUB: case OPERATOR_MUL:
        case OPERATOR_DIV: case OPERATOR_IDIV: case OPERATOR_MOD: case OPERATOR_POW:
        case OPERATOR_AND: case OPERATOR_OR:
        case OPERATOR_EQ: case OPERATOR_NE: case OPERATOR_LT: case OPERATOR_LE:
        case OPERATOR_GT: case OPERATOR_GE:
            return interp_const_node_supported(binary->left) &&
                interp_const_node_supported(binary->right);
        default:
            return false;
        }
    }
    case AST_NODE_IF_EXPR: {
        AstIfNode* branch = (AstIfNode*)node;
        return branch->cond && branch->then &&
            interp_const_node_supported(branch->cond) &&
            interp_const_node_supported(branch->then) &&
            (!branch->otherwise || interp_const_node_supported(branch->otherwise));
    }
    default:
        return false;
    }
}

// fold facts deliberately carry only tagged immediate values. Float and all
// pointer-backed results may be valid during the attempt but must not survive
// its side-stack lifetime or enter cacheable MIR as a stale address (DI14).
static bool interp_const_result_is_immediate(Item result) {
    switch (get_type_id(result)) {
    case LMD_TYPE_NULL:
    case LMD_TYPE_BOOL:
    case LMD_TYPE_INT:
        return true;
    default:
        return false;
    }
}

bool interp_const_fold_script(Transpiler* tp) {
    if (!tp || !tp->ast_index.nodes || !tp->ast_index.facts ||
            !context || !interp_const_fold_enabled()) {
        return true;
    }

    // The pass owns a throwaway frame rather than borrowing a runtime frame:
    // every intermediate is rooted while helpers run, and its side-stack
    // extent disappears after each fold attempt (D5.3.3).
    FnFramePlan plan = {};
    plan.scratch_depth = INTERP_CONST_FRAME_SLOTS - 1;
    plan.total_slots = INTERP_CONST_FRAME_SLOTS;
    plan.planned = true;

    InterpState st = {};
    st.ctx = context;
    st.runtime = tp->runtime;
    st.mode = EvalMode::CONST;
    st.depth_limit = 1;
    st.depth = 1;
    InterpState* saved_state = g_interp_state;
    g_interp_state = &st;

    for (uint32_t id = 0; id < tp->ast_index.count; id++) {
        AstNode* node = tp->ast_index.nodes[id];
        AstNodeFacts* facts = &tp->ast_index.facts[id];
        facts->flags &= ~AST_NODE_FACT_CONST_FOLDED;
        facts->folded_item = ITEM_NULL;
        if (!node || node->node_type == AST_NODE_PRIMARY ||
                !interp_const_node_supported(node)) continue;

        st.mode_fuel = interp_const_fuel_budget();
        st.mode_exhausted = false;
        st.mode_rejected = false;
        {
            InterpFrameGuard guard(&st, NULL, (Script*)tp, &plan, NULL, 0);
            if (!guard.valid()) continue;
            // a fold attempt is not a user-visible execution boundary. Contain
            // an unexpected native fault and leave this AST fact empty instead
            // of letting a compiler optimization alter program completion.
            Item result = interp_eval_local_fault_operand(guard.frame(), node);
            if (interp_frame_pending(guard.frame()) || item_is_error(result) ||
                    st.mode_exhausted || st.mode_rejected ||
                    !interp_const_result_is_immediate(result) || !node->type ||
                    get_type_id(result) != node->type->type_id) {
                continue;
            }
            facts->folded_item = result.item;
            facts->flags |= AST_NODE_FACT_CONST_FOLDED;
        }
    }
    g_interp_state = saved_state;
    return true;
}

static uint32_t interp_promotion_threshold(const char* env_name, uint32_t fallback) {
    const char* env = getenv(env_name);
    if (!env || !*env) return fallback;
    char* end = NULL;
    unsigned long value = strtoul(env, &end, 10);
    if (end == env || *end != '\0' || value > UINT32_MAX) return fallback;
    return (uint32_t)value;
}

static uint32_t interp_jit_threshold(void) {
    return interp_promotion_threshold("LAMBDA_JIT_THRESHOLD", 3);
}

static uint32_t interp_jit_backedge_threshold(void) {
    return interp_promotion_threshold("LAMBDA_JIT_BACKEDGE", 1024);
}

static void interp_upgrade_function_entry(Function* fn, const AstFuncNode* def,
        void* entry) {
    if (!fn || !def || !entry) return;
    // Publish the native pointer before the ABI byte: every later dispatcher
    // either sees the old T0 pair or a complete boxed entry (D8.1.1v2 §5.3).
    fn->ptr = (fn_ptr)entry;
    lambda_function_mark_mir_context_abi(fn);
    if (def->node_type == AST_NODE_PROC) {
        lambda_function_mark_lambda_boxed_procedure(fn);
    } else {
        lambda_function_mark_lambda_boxed_function(fn);
    }
    FnVariantAnalysis* public_variant = def->analysis
        ? fn_analysis_variant(def->analysis, FN_ENTRY_PUBLIC_WRAPPER) : NULL;
    uint32_t public_shape = LAMBDA_MIR_PUBLIC_RETURN_UNKNOWN;
    if (public_variant) {
        public_shape = public_variant->result.shape == RETURN_SHAPE_ITEM
            ? LAMBDA_MIR_PUBLIC_RETURN_ITEM
            : public_variant->result.shape == RETURN_SHAPE_ITEM_SCALAR
                ? LAMBDA_MIR_PUBLIC_RETURN_ITEM_COMPANION
                : LAMBDA_MIR_PUBLIC_RETURN_UNKNOWN;
    }
    lambda_function_mark_mir_public_return_shape(fn, public_shape);
}

bool interp_promote_function_if_hot(Function* fn) {
    if (!fn || fn->entry_abi != FN_ENTRY_ABI_LAMBDA_INTERPRETED ||
            lambda_tier_selected() != LAMBDA_TIER_AUTO) {
        return false;
    }
    InterpState* st = interp_current_state();
    const AstFuncNode* def = (const AstFuncNode*)fn->def;
    Script* script = fn->def_module;
    if (!st || !st->runtime || !def || !script || !def->analysis) return false;

    FnPromotionCell* cell = &def->analysis->promotion;
    if (cell->state == FN_PROMOTION_COMPILED && cell->boxed_entry) {
        interp_upgrade_function_entry(fn, def, cell->boxed_entry);
        return true;
    }
    if (cell->state == FN_PROMOTION_PINNED_INTERP ||
            cell->state == FN_PROMOTION_COMPILING) {
        return false;
    }
    if (cell->call_count != UINT32_MAX) cell->call_count++;
    if (cell->call_count < interp_jit_threshold() &&
            cell->backedge_count < interp_jit_backedge_threshold()) return false;
    if (!interp_satellite_supported(def)) {
        // A pinned definition is a declared interpreter policy, never a
        // fallback to a different module compilation path.
        cell->state = FN_PROMOTION_PINNED_INTERP;
        log_debug("interp-tier: pinned function='%s' reason=satellite-boundary",
            def->name ? def->name->chars : "<anonymous>");
        return false;
    }

    cell->state = FN_PROMOTION_COMPILING;
    void* entry = NULL;
    if (!compile_ast_function_satellite(st->runtime, script, def, &entry) || !entry) {
        // Promotion failure is not user-visible execution failure: the source
        // was already accepted by T0, so retain that semantic implementation.
        cell->state = FN_PROMOTION_PINNED_INTERP;
        log_error("interp-tier: satellite compile failed function='%s'; pinned to T0",
            def->name ? def->name->chars : "<anonymous>");
        return false;
    }
    cell->boxed_entry = entry;
    cell->state = FN_PROMOTION_COMPILED;
    interp_upgrade_function_entry(fn, def, entry);
    return true;
}

static uint32_t interp_depth_budget(void) {
    const char* env = getenv("LAMBDA_INTERP_DEPTH");
    if (!env || !*env) return INTERP_DEFAULT_DEPTH;
    long value = strtol(env, NULL, 10);
    if (value < 16 || value > 10000000L) return INTERP_DEFAULT_DEPTH;
    return (uint32_t)value;
}

// Runs one module's top level in its own frame. Used for every module in the
// import cone and for the main script, so an initializer sees exactly the same
// environment either way.
static Item interp_execute_top_level_nodes(Runner* runner, InterpState* st,
        Script* script, AstNode* first, bool run_main) {
    InterpFrameGuard guard(st, NULL, script, &script->interp_plan, NULL, 0);
    if (!guard.valid()) return ItemError;
    InterpFrame* frame = guard.frame();

    // The script body is a chain of top-level content lists; evaluate them the
    // way transpile_content does and keep the last value as the result.
    Item result = ItemNull;
    Scratch tail(frame);
    // A script whose top level is a single statement carries it directly under
    // AST_SCRIPT rather than inside a content list, so definitions have to be
    // hoisted and bound here too — evaluating one as an expression would build
    // an anonymous closure and leave its name unbound (build_content's pass 1).
    for (AstNode* item = first; item; item = item->next) {
        if (item->node_type == AST_NODE_FUNC || item->node_type == AST_NODE_PROC ||
                item->node_type == AST_NODE_FUNC_EXPR) {
            exec_declaration(frame, item);
        }
    }
    for (AstNode* item = first; item; item = item->next) {
        if (item->node_type == AST_NODE_CONTENT) {
            tail.set(eval_content(frame, (AstListNode*)item, true));
        } else if (item->node_type == AST_NODE_LIST) {
            // a lexical list carries `(let …, body)` bindings in `declare`;
            // eval_content ignores that chain, leaving its body to read empty
            // slots. Preserve the AST's block kind at the module boundary.
            tail.set(eval_list(frame, (AstListNode*)item));
        } else if (is_declaration_node(item->node_type)) {
            bool hoisted = item->node_type == AST_NODE_FUNC ||
                item->node_type == AST_NODE_PROC || item->node_type == AST_NODE_FUNC_EXPR;
            if (!hoisted) exec_declaration(frame, item);
        } else if (item->node_type != AST_NODE_IMPORT) {
            tail.set(eval_expr(frame, item));
        }
        if (interp_frame_pending(frame)) {
            // A module-scope `raise` (or propagated error) ends the script with
            // that value, the way an error return from `main` does.
            if (frame->signal == EvalSignal::RETURNED) {
                tail.set(interp_signal_payload(frame));
            }
            break;
        }
    }
    // `lambda.exe run` invokes a user-defined `pn main()` after module init and
    // makes its result the script result — the same scan and zero-arg call the
    // generated module entry emits under Context::run_main.
    if (run_main) {
        // An import/top-level expression can return a rich ERROR Item without
        // raising an EvalSignal; do not invoke main after that failed module
        // completion, or the interpreter would silently discard the immediate
        // frame's explicit failure.
        if (item_is_error(tail.get())) return tail.get();
        // A top-level `pn main` is reachable through two root children — it is
        // linked both as a root statement and inside the content node's item
        // list — so the scan must stop at the first call, not merely leave the
        // inner loop. Calling it twice ran the whole procedure twice
        // (test/lambda/pdf/phase2_font.ls printed its result twice).
        bool called_main = false;
        for (AstNode* item = first; item && !called_main; item = item->next) {
            AstNode* stmt = item;
            if (stmt->node_type == AST_NODE_CONTENT) stmt = ((AstListNode*)stmt)->item;
            for (; stmt; stmt = stmt->next) {
                if (stmt->node_type != AST_NODE_PROC) continue;
                AstFuncNode* proc = (AstFuncNode*)stmt;
                if (!proc->name || proc->name->len != 4 ||
                        memcmp(proc->name->chars, "main", 4) != 0) continue;
                NameEntry* entry = proc->analysis ? proc->analysis->decl_entry : NULL;
                Item callee = entry ? interp_read_binding(frame, entry) : ItemNull;
                if (get_type_id(callee) != LMD_TYPE_FUNC) {
                    log_error("interp: run_main found no callable 'main' "
                        "(slot=%d, got type %d)", entry ? entry->slot : -1,
                        (int)get_type_id(callee));
                    break;
                }
                uint64_t result_home = 0;
                tail.set(fn_call_into((Function*)(uintptr_t)callee.item, NULL,
                    &result_home));
                called_main = true;
                break;
            }
            if (interp_frame_pending(frame)) break;
        }
    }

    result = scalar_storage_read(tail.get(), false);
    return result;
}

static Item interp_execute_module(Runner* runner, InterpState* st, Script* script,
                                  bool run_main) {
    AstScript* root = (AstScript*)script->ast_root;
    if (!root) return ItemNull;
    if (!lambda_module_state_prepare(script->module_state_id,
            script->interp_slab_count)) {
        log_error("interp: could not prepare module slab for '%s'", script->reference);
        return ItemError;
    }
    runner->context->consts = script->const_list ? script->const_list->data : NULL;
    runner->context->type_list = script->type_list;
    return interp_execute_top_level_nodes(runner, st, script, root->child, run_main);
}

static Item interp_execute_repl_fragment(Runner* runner, InterpState* st,
        AstNode* fragment) {
    Script* script = runner ? runner->script : NULL;
    if (!script || !fragment) return ItemError;
    if (!lambda_module_state_prepare(script->module_state_id,
            script->interp_slab_count)) {
        log_error("interp: could not prepare REPL module slab for '%s'", script->reference);
        return ItemError;
    }
    runner->context->consts = script->const_list ? script->const_list->data : NULL;
    runner->context->type_list = script->type_list;
    return interp_execute_top_level_nodes(runner, st, script, fragment, false);
}

// Module initialization is transactional (D7.2.2/S7.7.6): a fault inside an
// initializer lands on its own barrier, resets the partial module slab, then
// forwards to the still-armed execution boundary, so a half-initialized module
// is never visible to a local handler.
static Item interp_run_module_init(Runner* runner, InterpState* st, Script* module) {
    LambdaRecoveryFrame* barrier = lambda_recovery_frame_begin_for(
        (Context*)runner->context, LAMBDA_RECOVERY_CAP_TRANSACTION_BARRIER);
    if (!barrier) {
        log_error("interp: failed to allocate a module transaction frame");
        lambda_recovery_frame_raise_fault(LAMBDA_FAULT_OUT_OF_MEMORY, ERR_OK);
        return ItemError;
    }
    List* saved_vargs = runner->context ? runner->context->current_vargs : NULL;
    if (LAMBDA_RECOVERY_FRAME_SETJMP(barrier)) {
        LambdaFaultReason reason = LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT;
        LambdaErrorCode prior = ERR_OK;
        if (lambda_recovery_frame_restore_landing(barrier)) {
            reason = barrier->fault.reason;
            prior = barrier->fault.prior_error_code;
        } else {
            log_error("interp: module transaction landing invariant failed");
        }
        lambda_recovery_frame_end(barrier);
        if (runner->context) runner->context->current_vargs = saved_vargs;
        lambda_module_state_reset();
        st->top = NULL;   // frames above the landing are abandoned wholesale
        lambda_recovery_frame_raise_fault(reason, prior);
        return ItemError;
    }
    if (!lambda_recovery_frame_arm(barrier)) {
        log_error("interp: failed to arm a module transaction frame");
        lambda_recovery_frame_end(barrier);
        return ItemError;
    }
    log_info("interp: running imported module init index=%d", module->index);
    Item result = interp_execute_module(runner, st, module, false);
    lambda_recovery_frame_end(barrier);
    // Module transaction cleanup is complete before the explicit completion
    // crosses to the importing frame; ordinary errors never use the barrier.
    return result;
}

// Post-order over the import cone, then the main script — the order
// run_script_mir walks, so each initializer observes its dependencies.
static Item interp_execute(Runner* runner, InterpState* st) {
    ArrayList* cone = interp_collect_import_cone(runner->script);
    if (cone) {
        for (int i = 0; i < cone->length; i++) {
            Script* module = (Script*)cone->data[i];
            if (!module || !module->ast_root) continue;
            Item module_result = interp_run_module_init(runner, st, module);
            if (item_is_error(module_result)) {
                arraylist_free(cone);
                return module_result;
            }
        }
        arraylist_free(cone);
    }
    return interp_execute_module(runner, st, runner->script,
        runner->context->run_main);
}

static Item interp_run_nodes(Runner* runner, bool run_main, AstNode* repl_fragment) {
    if (!runner || !runner->script || !runner->context) return ItemError;
    Script* script = runner->script;
    runner->context->run_main = run_main;
    if (!runner->context->cwd) {
        // T0 bypasses the JIT output wrapper, which normally owns this URL at
        // one execution boundary; REPL fragments therefore recreate it here.
        runner->context->cwd = get_current_dir();
    }
    List* saved_vargs = runner->context->current_vargs;

    InterpState st = {};
    st.ctx = runner->context;
    st.runtime = runner->runtime;
    st.mode = EvalMode::RUNTIME;
    st.depth_limit = interp_depth_budget();
    st.depth = st.depth_limit;
    InterpState* saved = g_interp_state;
    g_interp_state = &st;

    Item result = ItemError;
    LambdaRecoveryFrame* boundary = lambda_recovery_frame_begin_for(
        (Context*)runner->context, LAMBDA_RECOVERY_CAP_EXECUTION_BOUNDARY);
    if (!boundary) {
        log_error("interp: failed to allocate the execution recovery frame");
        result = lambda_recovery_publish_fault_item((Context*)runner->context,
            LAMBDA_FAULT_OUT_OF_MEMORY, ERR_OK);
    } else if (LAMBDA_RECOVERY_FRAME_SETJMP(boundary)) {
        Item recovered = ItemError;
        if (!lambda_recovery_frame_restore_landing(boundary)) {
            log_error("interp: recovery frame landing invariant failed");
            recovered = lambda_recovery_publish_fault_item((Context*)runner->context,
                LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK);
        } else {
            recovered = lambda_recovery_frame_fault_item((Context*)runner->context,
                boundary);
        }
        _lambda_stack_overflow_flag = false;
        lambda_recovery_frame_end(boundary);
        // The landing restored both side-stack watermarks; every interpreter
        // frame above it is abandoned wholesale, which is safe because frames
        // own no resource beyond those two watermarks.
        st.top = NULL;
        runner->context->current_vargs = saved_vargs;
        result = recovered;
    } else if (!lambda_recovery_frame_arm(boundary)) {
        log_error("interp: failed to arm the execution recovery frame");
        lambda_recovery_frame_end(boundary);
        result = lambda_recovery_publish_fault_item((Context*)runner->context,
            LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK);
    } else {
        result = repl_fragment
            ? interp_execute_repl_fragment(runner, &st, repl_fragment)
            : interp_execute(runner, &st);
        lambda_recovery_frame_end(boundary);
    }

    g_interp_state = saved;
    // A fault landing bypasses C++ destructors. Restore the runner-level
    // binding unconditionally so a later REPL/history execution cannot see
    // an abandoned callee's rest list.
    runner->context->current_vargs = saved_vargs;
    runner->context->result = result;
    if (runner->context->heap) runner->context->heap->result_root = result.item;
    if (runner->context->cwd) {
        url_destroy(runner->context->cwd);
        runner->context->cwd = NULL;
    }
    g_interp_stats.scripts_executed++;
    g_interp_stats.nodes_evaluated += st.node_count;
    log_notice("interp: executed script='%s' nodes=%llu depth_used=%u",
        script->reference ? script->reference : "<none>",
        (unsigned long long)st.node_count, st.depth_limit - st.depth);
    return result;
}

Item interp_run_script(Runner* runner, bool run_main) {
    return interp_run_nodes(runner, run_main, NULL);
}

Item interp_run_repl_fragment(Runner* runner, AstNode* fragment) {
    return interp_run_nodes(runner, false, fragment);
}
