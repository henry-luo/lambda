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
#include "../../lib/log.h"
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
        frame_.signal_index = named;
        frame_.scratch_base = named + 1;
        frame_.scratch_top = frame_.scratch_base;
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
static void exec_declaration(InterpFrame* f, AstNode* node);

// Direct-pointer Items: the object's own header byte already carries its
// TypeId, so no high-byte tag is added (the canonical carrier fn_is/fn_query
// expect).
static inline Item interp_ptr_item(void* ptr) {
    return ptr ? (Item){.item = (uint64_t)(uintptr_t)ptr} : ItemNull;
}

// ---------------------------------------------------------------------------
// Bindings
// ---------------------------------------------------------------------------

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
        // Cross-module reads resolve against the *declaring* Script (§4.1);
        // P0 is single-module, so the frame's module is the declaring one.
        return interp_read_module_slot(f->module, entry->slot);
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
        interp_write_module_slot(f->module, entry->slot, value);
        return;
    }
    if ((uint32_t)entry->slot < f->scratch_base) f->slots[entry->slot] = value.item;
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
    case OPERATOR_IS_ERROR: return (Item){.item = b2it(item_is_error(operand)
                                    ? BOOL_TRUE : BOOL_FALSE)};
    default:
        log_error("interp: unhandled unary operator %d", (int)node->op);
        return ItemError;
    }
}

static Item eval_binary(InterpFrame* f, AstBinaryNode* node) {
    // `and`/`or` short-circuit before the right operand is evaluated (S10.2.3);
    // truthiness is by tag through the shipped helper (S3.1/S3.2).
    if (node->op == OPERATOR_AND) {
        Item left = eval_expr(f, node->left);
        if (item_is_error(left)) return left;
        if (!is_truthy(left)) return left;
        return eval_expr(f, node->right);
    }
    if (node->op == OPERATOR_OR) {
        Item left = eval_expr(f, node->left);
        if (item_is_error(left)) return left;
        if (is_truthy(left)) return left;
        return eval_expr(f, node->right);
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

// Direct C call through the registry entry, using the same result boxing MIR
// lowering selects from sysfunc_c_ret_type_id. Same registry, both tiers.
static Item eval_sys_call(SysFuncInfo* info, const Item* args, int argc) {
    if (!info || !info->func_ptr) {
        log_error("interp: system function '%s' has no entry point",
            info && info->name ? info->name : "<null>");
        return ItemError;
    }
    if (info->c_arg_conv != C_ARG_ITEM) {
        // Bitwise/shift entries take native int64 words; P0 routes them to the
        // whole-module fallback rather than guessing a marshalling rule.
        log_error("interp: system function '%s' uses a native argument convention",
            info->name ? info->name : "<null>");
        return ItemError;
    }
    void* fp = (void*)info->func_ptr;
    TypeId c_ret = sysfunc_c_ret_type_id(info);

#define SYS_DISPATCH(RetT) \
    (argc == 0 ? ((RetT(*)())fp)() : \
     argc == 1 ? ((RetT(*)(Item))fp)(args[0]) : \
     argc == 2 ? ((RetT(*)(Item, Item))fp)(args[0], args[1]) : \
     argc == 3 ? ((RetT(*)(Item, Item, Item))fp)(args[0], args[1], args[2]) : \
     argc == 4 ? ((RetT(*)(Item, Item, Item, Item))fp)(args[0], args[1], args[2], args[3]) : \
                 ((RetT(*)(Item, Item, Item, Item, Item))fp)(args[0], args[1], args[2], args[3], args[4]))

    if (argc > 5) {
        log_error("interp: system function '%s' arity %d exceeds the P0 dispatch table",
            info->name ? info->name : "<null>", argc);
        return ItemError;
    }
    switch (c_ret) {
    case LMD_TYPE_INT:    return (Item){.item = i2it(SYS_DISPATCH(int64_t))};
    case LMD_TYPE_INT64:
        // int64() returns a raw int64_t even when its semantic type is
        // int64 | error; INT64_ERROR is its out-of-band failure signal, so the
        // boundary boxing must be the error-aware one lowering uses.
        if (info->fn == SYSFUNC_INT64) {
            return box_int64_result_or_error(SYS_DISPATCH(int64_t));
        }
        return box_int64_value(SYS_DISPATCH(int64_t));
    case LMD_TYPE_BOOL:   return (Item){.item = b2it(SYS_DISPATCH(Bool) ? BOOL_TRUE : BOOL_FALSE)};
    case LMD_TYPE_FLOAT:  return push_d(SYS_DISPATCH(double));
    case LMD_TYPE_STRING: { String* str = SYS_DISPATCH(String*); return str ? (Item){.item = s2it(str)} : ItemNull; }
    case LMD_TYPE_SYMBOL: { Symbol* sym = SYS_DISPATCH(Symbol*); return sym ? (Item){.item = y2it(sym)} : ItemNull; }
    case LMD_TYPE_TYPE:   return interp_ptr_item(SYS_DISPATCH(Type*));
    case LMD_TYPE_DTIME:  return push_k(SYS_DISPATCH(DateTime));
    default:              return SYS_DISPATCH(Item);
    }
#undef SYS_DISPATCH
}

static AstNode* interp_unwrap_primary(AstNode* node) {
    while (node && node->node_type == AST_NODE_PRIMARY &&
            ((AstPrimaryNode*)node)->expr) {
        node = ((AstPrimaryNode*)node)->expr;
    }
    return node;
}

static Item eval_call(InterpFrame* f, AstCallNode* node) {
    int argc = 0;
    for (AstNode* a = node->argument; a; a = a->next) argc++;
    if (argc > LAMBDA_MAX_FUNCTION_ARGS) {
        log_error("interp: call arity %d exceeds the Core Lambda limit", argc);
        return ItemError;
    }

    AstNode* callee = interp_unwrap_primary(node->function);
    if (callee && callee->node_type == AST_NODE_SYS_FUNC) {
        // Arguments must all be rooted before the C entry runs: the entry may
        // allocate, and an earlier argument would otherwise be unreachable.
        RootSpan arg_roots((size_t)(argc > 0 ? argc : 1));
        uint64_t* words = arg_roots.words();
        int i = 0;
        for (AstNode* a = node->argument; a; a = a->next, i++) {
            words[i] = eval_expr(f, a).item;
        }
        return eval_sys_call(((AstSysFuncNode*)callee)->fn_info,
            (const Item*)(void*)words, argc);
    }

    Item callee_value = eval_expr(f, node->function);
    Scratch fn_slot(f);
    fn_slot.set(callee_value);
    if (item_is_error(fn_slot.get())) return fn_slot.get();

    RootSpan arg_roots((size_t)(argc > 0 ? argc : 1));
    uint64_t* words = arg_roots.words();
    int i = 0;
    for (AstNode* a = node->argument; a; a = a->next, i++) {
        words[i] = eval_expr(f, a).item;
    }

    Item callee_item = fn_slot.get();
    if (get_type_id(callee_item) != LMD_TYPE_FUNC) {
        log_error("interp: call target is not a function (type %d)",
            (int)get_type_id(callee_item));
        return ItemError;
    }
    Function* fn = (Function*)(uintptr_t)callee_item.item;
    // Every callee — interpreted or native — reaches its body through the
    // single dynamic dispatch point (AI7). Routing interpreted calls through it
    // too is what gives them the shared arity check plus the optional/rest
    // adapter, instead of a second, divergent argument protocol.
    List args = {};
    args.length = argc;
    args.items = (Item*)(void*)words;
    uint64_t result_home = 0;
    return fn_call_into(fn, argc ? &args : NULL, &result_home);
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

static InterpArrayKind interp_array_kind(AstArrayNode* node,
                                         ArrayNumElemType* sized_elem) {
    TypeArray* arr_type = (TypeArray*)node->type;
    if (!node->item || !arr_type || !arr_type->nested) return INTERP_ARRAY_GENERIC;
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
    case LMD_TYPE_NUM_SIZED:
        // Sized/u64 element selection depends on lowering's per-item effective
        // type, not on the AST array type alone; the pre-scan routes these
        // literals to the JIT rather than risk a different element width.
        (void)sized_elem;
        return INTERP_ARRAY_GENERIC;
    default:
        return INTERP_ARRAY_GENERIC;
    }
}

static Item eval_array(InterpFrame* f, AstArrayNode* node) {
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

    Scratch acc(f);
    acc.set(interp_ptr_item(array()));
    if (!node->item) return acc.get();   // array_end() on an empty array is spreadable-null
    for (AstNode* item = node->item; item; item = item->next) {
        Item value = eval_expr(f, item);
        // Re-read the accumulator after every element: growth may collect.
        Array* arr = (Array*)(uintptr_t)acc.get().item;
        if (!arr) return ItemError;
        if (item->node_type == AST_NODE_SPREAD) array_push_spread(arr, value);
        else array_push(arr, value);
    }
    return array_end((Array*)(uintptr_t)acc.get().item);
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

// ---------------------------------------------------------------------------
// Content blocks
// ---------------------------------------------------------------------------

// Mirrors transpile_content's split: declarations bind, side-effect statements
// run for effect, and the value expressions form the block's result — one
// value passes through, several accumulate into a list.
static Item eval_content(InterpFrame* f, AstListNode* list_node, bool hoist_functions) {
    int value_count = 0;
    AstNode* last_value = NULL;
    for (AstNode* item = list_node->item; item; item = item->next) {
        if (is_declaration_node(item->node_type) ||
                is_side_effect_stam(item->node_type)) continue;
        value_count++;
        last_value = item;
    }

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

    if (value_count <= 1) {
        Item result = value_count == 0 ? list_end(list()) : ItemNull;
        for (AstNode* item = list_node->item; item; item = item->next) {
            if (is_declaration_node(item->node_type)) {
                bool already_hoisted = hoist_functions &&
                    (item->node_type == AST_NODE_FUNC || item->node_type == AST_NODE_PROC ||
                     item->node_type == AST_NODE_FUNC_EXPR);
                if (!already_hoisted) exec_declaration(f, item);
            } else if (is_side_effect_stam(item->node_type)) {
                eval_expr(f, item);
            } else if (item == last_value) {
                result = eval_expr(f, item);
            }
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
            continue;
        }
        if (is_side_effect_stam(item->node_type)) { eval_expr(f, item); continue; }
        Item value = eval_expr(f, item);
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
            interp_write_binding(f, named->entry, eval_expr(f, named->as));
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
        List* ls = (List*)(uintptr_t)acc.get().item;   // re-read: push may collect
        if (!ls) return ItemError;
        list_push_spread(ls, value);
    }
    return list_end((List*)(uintptr_t)acc.get().item);
}

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

static void exec_declaration(InterpFrame* f, AstNode* node) {
    switch (node->node_type) {
    case AST_NODE_LET_STAM:
    case AST_NODE_PUB_STAM:
    case AST_NODE_VAR_STAM: {
        for (AstNode* decl = ((AstLetNode*)node)->declare; decl; decl = decl->next) {
            if (decl->node_type != AST_NODE_ASSIGN) continue;
            AstNamedNode* named = (AstNamedNode*)decl;
            Item value = eval_expr(f, named->as);
            interp_write_binding(f, named->entry, value);
        }
        break;
    }
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

static Item eval_expr(InterpFrame* f, AstNode* node) {
    if (!node) return ItemNull;
    f->cur = node;
    f->st->node_count++;

    switch (node->node_type) {
    case AST_NODE_PRIMARY: {
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        if (pri->expr) return eval_expr(f, pri->expr);
        return eval_literal(f, node);
    }
    case AST_NODE_IDENT:
        return interp_read_binding(f, ((AstIdentNode*)node)->entry);
    case AST_NODE_UNARY:
        return eval_unary(f, (AstUnaryNode*)node);
    case AST_NODE_BINARY:
        return eval_binary(f, (AstBinaryNode*)node);
    case AST_NODE_IF_EXPR: {
        AstIfNode* branch = (AstIfNode*)node;
        Item cond = eval_expr(f, branch->cond);
        if (item_is_error(cond)) return cond;
        if (is_truthy(cond)) return eval_expr(f, branch->then);
        if (branch->otherwise) return eval_expr(f, branch->otherwise);
        return ItemNull;
    }
    case AST_NODE_CALL_EXPR:
        return eval_call(f, (AstCallNode*)node);
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
        // null totality (S7.1.1) comes from the helper; both operands are
        // published first because it allocates.
        return fn_member(obj.get(), key_slot.get());
    }
    case AST_NODE_INDEX_EXPR: {
        AstFieldNode* field = (AstFieldNode*)node;
        Item object_value = eval_expr(f, field->object);
        Scratch obj(f);
        obj.set(object_value);
        Item index_value = eval_expr(f, field->field);
        Scratch index_slot(f);
        index_slot.set(index_value);
        return fn_index(obj.get(), index_slot.get());
    }
    case AST_NODE_ARRAY:
        return eval_array(f, (AstArrayNode*)node);
    case AST_NODE_MAP:
        return eval_map(f, (AstMapNode*)node);
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
        exec_declaration(f, node);
        return ItemNull;
    case AST_NODE_SYS_FUNC:
        // A bare system-function reference outside a call is not a first-class
        // value in T0; the pre-scan lets it through only as a call callee.
        log_error("interp: system function reference used as a value");
        return ItemError;
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

extern "C" Item interp_call(Function* fn, const Item* args, int argc) {
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
    if (st->depth == 0) {
        // A clean S7.4.3-channel fault before either the C-stack guard or the
        // side-stack limit fires. Fault timing may differ from T1 (S7.11.4).
        st->depth_exhausted = true;
        log_error("interp: recursion depth budget %u exhausted in '%s'",
            st->depth_limit, fn->name ? fn->name : "<anonymous>");
        lambda_recovery_frame_raise_fault(LAMBDA_FAULT_STACK_OVERFLOW, ERR_OK);
        return ItemError;
    }

    st->depth--;
    Item result = ItemNull;
    {
        InterpFrameGuard guard(st, fn_node, module, &fn_node->analysis->frame_plan,
            (Item*)fn->closure_env, fn->closure_field_count);
        if (!guard.valid()) { st->depth++; return ItemError; }
        InterpFrame* frame = guard.frame();
        uint16_t params = fn_node->analysis->frame_plan.param_count;
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
            frame->slots[index] = value.item;
        }

        result = eval_expr(frame, fn_node->body);
        // Re-home before the guard restores the callee's number watermark:
        // a wide scalar living in this frame's extent must move into the
        // caller's before its home dies.
        result = scalar_storage_read(result, false);
    }
    st->depth++;
    return result;
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

static __thread InterpState* g_interp_state = NULL;
static InterpState* interp_current_state(void) { return g_interp_state; }

static uint32_t interp_depth_budget(void) {
    const char* env = getenv("LAMBDA_INTERP_DEPTH");
    if (!env || !*env) return INTERP_DEFAULT_DEPTH;
    long value = strtol(env, NULL, 10);
    if (value < 16 || value > 10000000L) return INTERP_DEFAULT_DEPTH;
    return (uint32_t)value;
}

static Item interp_execute(Runner* runner, InterpState* st) {
    Script* script = runner->script;
    AstScript* root = (AstScript*)script->ast_root;


    if (!lambda_module_state_prepare(script->module_state_id,
            script->interp_slab_count)) {
        log_error("interp: could not prepare module slab for '%s'", script->reference);
        return ItemError;
    }
    runner->context->consts = script->const_list ? script->const_list->data : NULL;
    runner->context->type_list = script->type_list;

    InterpFrameGuard guard(st, NULL, script, &script->interp_plan, NULL, 0);
    if (!guard.valid()) return ItemError;
    InterpFrame* frame = guard.frame();

    // The script body is a chain of top-level content lists; evaluate them the
    // way transpile_content does and keep the last value as the result.
    Item result = ItemNull;
    Scratch tail(frame);
    for (AstNode* item = root->child; item; item = item->next) {
        if (item->node_type == AST_NODE_CONTENT || item->node_type == AST_NODE_LIST) {
            tail.set(eval_content(frame, (AstListNode*)item, true));
        } else if (item->node_type != AST_NODE_IMPORT) {
            tail.set(eval_expr(frame, item));
        }
    }
    result = scalar_storage_read(tail.get(), false);
    return result;
}

Item interp_run_script(Runner* runner, bool run_main) {
    if (!runner || !runner->script || !runner->context) return ItemError;
    Script* script = runner->script;
    runner->context->run_main = run_main;

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
        result = recovered;
    } else if (!lambda_recovery_frame_arm(boundary)) {
        log_error("interp: failed to arm the execution recovery frame");
        lambda_recovery_frame_end(boundary);
        result = lambda_recovery_publish_fault_item((Context*)runner->context,
            LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK);
    } else {
        result = interp_execute(runner, &st);
        lambda_recovery_frame_end(boundary);
    }

    g_interp_state = saved;
    runner->context->result = result;
    if (runner->context->heap) runner->context->heap->result_root = result.item;
    g_interp_stats.scripts_executed++;
    g_interp_stats.nodes_evaluated += st.node_count;
    log_notice("interp: executed script='%s' nodes=%llu depth_used=%u",
        script->reference ? script->reference : "<none>",
        (unsigned long long)st.node_count, st.depth_limit - st.depth);
    return result;
}
