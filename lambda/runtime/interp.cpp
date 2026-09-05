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
#include "re2_wrapper.hpp"
#include "heap_api.h"
#include "type_contract.hpp"
#include "lambda-number-types.hpp"
#include "lambda-number-runtime.hpp"
#include "../js/js_runtime.h"
#include "module_registry.h"
#include "template_registry.h"
#include "template_state.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include "../../lib/url.h"
#include <stdlib.h>

extern "C" Item lambda_module_var_read_slot(void* module_state, uint32_t slot);
extern "C" Item pn_output2_mir(Item source, Item target);
extern "C" Item pn_output_append_mir(Item source, Item target);

// ---------------------------------------------------------------------------
// Tier selection
// ---------------------------------------------------------------------------

// auto is the shipped policy: cold definitions start in T0 and eligible hot
// definitions promote through the per-function satellite path (D8.1.1v4).
static LambdaTier g_lambda_tier = LAMBDA_TIER_AUTO;

static InterpState* interp_current_state(void);
static bool interp_whole_script_poc_enabled(void);
static bool interp_whole_script_publish_function(Script* script,
        AstFuncNode* def, Function* known_fn);

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
                     const FnFramePlan* plan, Item* env, uint32_t env_count,
                     const TypeMethod* method = NULL, Item method_self = ItemNull,
                     Function* callable = NULL)
            : frame_{}, roots_{}, mark_{}, ok_(false) {
        mark_ = lambda_side_stack_snapshot();
        size_t slots = plan && plan->planned ? plan->total_slots : 1;
        size_t root_slots = slots + (callable ? 1 : 0) + (method ? 1 : 0);
        if (!lambda_root_frame_begin(&roots_, root_slots)) {
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
        size_t auxiliary_slot = slots;
        frame_.callable_slot = callable ? roots_.slots + auxiliary_slot++ : NULL;
        if (frame_.callable_slot) {
            *frame_.callable_slot = (uint64_t)(uintptr_t)callable;
        }
        frame_.env = env;
        frame_.env_count = env_count;
        frame_.method = method;
        frame_.method_self = method ? roots_.slots + auxiliary_slot : NULL;
        if (frame_.method_self) *frame_.method_self = method_self.item;
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
static Item interp_item_at(Item source, int64_t index);
static Item eval_content(InterpFrame* f, AstListNode* list_node, bool hoist_functions);
static Item eval_list(InterpFrame* f, AstListNode* list_node);
static Item eval_for(InterpFrame* f, AstForNode* for_node, bool result_demanded);
static void exec_declaration(InterpFrame* f, AstNode* node);
static void interp_note_backedge(InterpFrame* frame);
static bool interp_note_tail_call(InterpFrame* frame);
static bool interp_tail_handoff_candidate(const InterpFrame* frame);
static bool interp_promote_function_from_tail(Function* fn);
static uint32_t interp_jit_threshold(void);
static void interp_format_parameter_boundary(char* boundary, size_t capacity,
    const AstFuncNode* fn_node, const char* fallback_name, int index);
static bool interp_eval_constrained_predicate(InterpFrame* f,
    AstConstrainedTypeNode* constrained, Scratch& subject);

// Multi-axis ArrayNum operations use one stack-local coordinate buffer. The
// admitted syntax permits only scalar coordinates, but each expression still
// finishes before the next one so a future supported leaf cannot borrow an
// unrooted Item across a safepoint.
static bool interp_eval_ndim_indices(InterpFrame* f, AstNode* first,
        int64_t* indices, int* count) {
    *count = 0;
    for (AstNode* index = first; index; index = index->next) {
        if (*count >= AST_COW_PATH_MAX) {
            log_error("interp: N-D index exceeds %d axes", AST_COW_PATH_MAX);
            return false;
        }
        Item value = eval_expr(f, index);
        if (interp_frame_pending(f)) return false;
        indices[(*count)++] = it2l(value);
    }
    return *count >= 2;
}

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

// Hosted imports are already evaluated by their language runtime and publish
// rooted namespace values. Resolve the raw export through that membrane rather
// than assigning it a Lambda slab slot; a qualified alias still uses the
// declaring synthetic node name, not the importer's qualified spelling.
static Item interp_read_cross_lang_binding(InterpState* st, NameEntry* entry) {
    if (!st || !entry || !entry->import || !entry->import->script) return ItemNull;
    Script* owner = entry->import->script;
    Runtime* runtime = st->runtime ? st->runtime
        : (st->ctx ? st->ctx->runtime : NULL);
    ModuleDescriptor* module = runtime
        ? module_get_for_runtime(runtime, owner->reference) : NULL;
    AstNamedNode* declaration = entry->node ? (AstNamedNode*)entry->node : NULL;
    const char* name = declaration && declaration->name
        ? declaration->name->chars : (entry->name ? entry->name->chars : NULL);
    if (!module || !name) {
        log_error("interp: cross-language import '%s' has no namespace export",
            owner->reference ? owner->reference : "<unknown>");
        return ItemError;
    }
    return module_namespace_get(module, name);
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

static bool interp_is_object_field_entry(const NameEntry* entry) {
    return entry && entry->node && entry->node->node_type == AST_NODE_KEY_EXPR;
}

static Item interp_read_binding(InterpFrame* f, NameEntry* entry) {
    if (!entry) return ItemNull;
    for (InterpViewBinding* binding = f->st ? f->st->view_bindings : NULL;
            binding; binding = binding->prev) {
        if (binding->entry == entry && binding->value) {
            return (Item){.item = *binding->value};
        }
    }
    if (entry->import && entry->import->is_cross_lang) {
        return interp_read_cross_lang_binding(f->st, entry);
    }
    if (interp_is_object_field_entry(entry)) {
        if (!f->method_self || !entry->name) {
            log_error("interp: object field read escaped its method frame");
            return ItemError;
        }
        return item_attr((Item){.item = *f->method_self}, entry->name->chars);
    }
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
    if (!entry) return;
    for (InterpViewBinding* binding = f->st ? f->st->view_bindings : NULL;
            binding; binding = binding->prev) {
        if (binding->entry != entry || !binding->value) continue;
        *binding->value = value.item;
        if (binding->is_state && binding->model && binding->template_ref &&
                binding->state_name) {
            // State writes must update the shared store, not only the
            // activation overlay; tmpl_state_set also marks the render-map
            // entry dirty for the next reconciliation pass.
            tmpl_state_set((Item){.item = *binding->model}, binding->template_ref,
                binding->state_name, value);
        }
        return;
    }
    if (interp_is_object_field_entry(entry)) {
        if (!f->method_self || !entry->name) {
            log_error("interp: object field write escaped its method frame");
            return;
        }
        // The receiver was detached before its bound closure was made; root
        // both operands while the shared setter may rebuild a field shape.
        RootFrame roots(2);
        Rooted<Item> self(roots, (Item){.item = *f->method_self});
        Rooted<Item> stored(roots, value);
        Item key = {.item = s2it(entry->name)};
        fn_map_set(self.get(), key, stored.get());
        return;
    }
    if (!entry->storage_assigned) return;
    if (entry->binding_storage == BINDING_STORAGE_MODULE) {
        // An imported binding is read-only here: its owner initializes it.
        Script* owner = entry->import_owner ? entry->import_owner : f->module;
        interp_write_module_slot(owner, entry->slot, value);
        return;
    }
    if ((uint32_t)entry->slot < f->scratch_base) f->slots[entry->slot] = value.item;
}

// A declared numeric lane is a runtime admission boundary even when static
// checking accepted the source. The compact and u64 coercers are conversion
// operations (including sized wraparound); every other numeric contract uses
// lambda_type_check's shared conversion/rejection policy.
static Item interp_coerce_declared_numeric(InterpFrame* f, Item value,
        Type* declared_type, const char* boundary) {
    if (!f || item_is_error(value)) return value;
    Type* target = unwrap_simple_type_type(declared_type);
    if (!target) return value;
    bool compact_or_u64 = target->type_id == LMD_TYPE_NUM_SIZED ||
        target->type_id == LMD_TYPE_UINT64;
    if (!compact_or_u64 &&
            lambda_numeric_kind_from_type(target) == LAMBDA_NUM_INVALID) {
        return value;
    }
    Scratch source_root(f);
    source_root.set(value);
    if (target->type_id == LMD_TYPE_NUM_SIZED) {
        // lambda_type_check correctly rejects out-of-range admission, but
        // explicit i8/u8/etc. conversions wrap before this binding boundary.
        return coerce_num_sized(source_root.get(),
            (int64_t)type_num_sized_kind(target));
    }
    if (target->type_id == LMD_TYPE_UINT64) {
        return coerce_uint64(source_root.get());
    }
    return lambda_type_check(source_root.get(), target, boundary);
}

static Item interp_coerce_declared_array(InterpFrame* f, Item value,
        Type* declared_type, const char* boundary) {
    if (!f || item_is_error(value)) return value;
    Type* element = ast_declared_array_element(declared_type);
    if (!element) return value;
    Scratch source_root(f);
    source_root.set(value);
    if (element->type_id == LMD_TYPE_ANY) {
        // MIR widens an ArrayNum at an any[] declaration boundary; retaining
        // its packed N-D carrier would make later scalar indexing flatten a
        // row instead of replacing the boxed sequence element.
        void* boxed = ensure_typed_array(source_root.get(), LMD_TYPE_ANY);
        return boxed ? interp_ptr_item(boxed) : ItemError;
    }
    LaneStorageDesc lane = {};
    if (lambda_type_lane_storage_desc(element, &lane) &&
            (lane.nullable || lane.kind == LANE_STORAGE_POINTER)) {
        // Optional elements are TypeUnary contracts and pointer elements have
        // no bare TypeId carrier. Passing either to ensure_typed_array rejects
        // a valid string[] source or loses T?'s null lane; the shared boundary
        // detaches/rebuilds the exact native carrier before T0's first store.
        return lambda_type_check(source_root.get(), declared_type, boundary);
    }
    void* typed = element->type_id == LMD_TYPE_NUM_SIZED
        ? ensure_sized_array(source_root.get(),
            (int64_t)num_sized_to_elem_type(type_num_sized_kind(element)))
        : ensure_typed_array(source_root.get(), element->type_id);
    if (!typed) return ItemError;
    // MIR establishes T[]'s physical carrier at this declaration boundary;
    // retaining a generic array here would let an indexed T0 store bypass the
    // checked element contract and silently diverge after a later mutation.
    return interp_ptr_item(typed);
}

static bool interp_declared_optional_array(Type* type) {
    Type* semantic = type_field_unwrap_simple_decl(type);
    if (!semantic || semantic->type_id != LMD_TYPE_TYPE ||
            semantic->kind != TYPE_KIND_UNARY ||
            ((TypeUnary*)semantic)->op != OPERATOR_OPTIONAL) return false;
    Type* base = type_field_unwrap_simple_decl(((TypeUnary*)semantic)->operand);
    return base && base->type_id == LMD_TYPE_ARRAY;
}

static Item interp_coerce_declared_binding(InterpFrame* f, Item value,
        Type* declared_type, const char* boundary) {
    value = interp_coerce_declared_array(f, value, declared_type, boundary);
    if (item_is_error(value)) return value;
    if (ast_declared_type_is_map(declared_type)) {
        Scratch source_root(f);
        source_root.set(value);
        // A dynamic structural value must be recursively converted before the
        // binding publishes it; otherwise nested float fields evade Person's
        // int contract until a later write observes the wrong representation.
        return lambda_type_check(source_root.get(), declared_type, boundary);
    }
    Type* target = unwrap_simple_type_type(declared_type);
    if (!target) return value;
    if (interp_declared_optional_array(target)) {
        TypeId actual = get_type_id(value);
        if (actual == LMD_TYPE_NULL || actual == LMD_TYPE_RANGE ||
                actual == LMD_TYPE_ARRAY || actual == LMD_TYPE_ARRAY_NUM) {
            // `array?` is the open nullable container contract; the validator
            // treats its max-length metadata as an occurrence bound, unlike MIR.
            return value;
        }
    }
    LambdaNumericKind numeric_kind = lambda_numeric_kind_from_type(target);
    if (numeric_kind != LAMBDA_NUM_INVALID || target->type_id == LMD_TYPE_NUM_SIZED ||
            target->type_id == LMD_TYPE_UINT64) {
        return interp_coerce_declared_numeric(f, value, declared_type, boundary);
    }
    // Explicit non-numeric contracts (including unions) still form runtime
    // admission boundaries; leaving them unchecked let T0 publish a rejected
    // assignment that MIR returns through its checked-boundary edge.
    Scratch source_root(f);
    source_root.set(value);
    return lambda_type_check(source_root.get(), declared_type, boundary);
}

static Item interp_coerce_parameter_binding(InterpFrame* f, Item value,
        AstNamedNode* parameter, const char* boundary) {
    if (!parameter) return value;
    TypeParam* parameter_type = parameter->type &&
        parameter->type->kind == TYPE_KIND_PARAM ? (TypeParam*)parameter->type : NULL;
    if (parameter_type && parameter_type->is_optional && value.item == ITEM_NULL) {
        // The optional-call adapter resolves an omitted argument to null before
        // this boundary; numeric admission must preserve that valid absence,
        // matching the boxed wrapper rather than rejecting it as `int`.
        return value;
    }
    return interp_coerce_declared_binding(f, value, parameter->declared_type,
        boundary ? boundary : "declared parameter binding");
}

static bool interp_bind_declared_value(InterpFrame* f, AstDeclaratorNode* named,
        Item value) {
    if (!named) return false;
    Item source = value;
    char boundary[192];
    snprintf(boundary, sizeof(boundary), "declaration '%.*s'",
        named->name ? (int)named->name->len : 0,
        named->name ? named->name->chars : "");
    Item bound = interp_coerce_declared_binding(f, value, named->declared_type,
        boundary);
    // CW24v2 phase 2: a place-copy binding (`var row = m.rows[i]`) marks the
    // read value so the first write DETACHES -- a real S9.1.2 snapshot --
    // instead of aliasing a child a fresh literal never captured. All T0
    // declaration paths funnel through this bind. Mark-only: cannot allocate.
    if (named->entry && named->entry->is_place_copy &&
            named->entry->place_copy_mutated) {
        cow_mark_shared(bound);
    }
    if (!item_is_error(source) && item_is_error(bound) && named->declared_type &&
            !lambda_type_accepts_error(named->declared_type)) {
        // A failed deferred boundary must not publish ItemError into the slot:
        // MIR returns before the store, so continuing would expose a binding
        // that cannot exist and would run statements past the declaring block.
        interp_signal(f, EvalSignal::ERROR_SKIP, bound);
        return false;
    }
    interp_write_binding(f, named->entry, bound);
    return true;
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
        return (Item){.item = b2it(parse_bool_literal_span(f->module->source, node->source_span)
            ? BOOL_TRUE : BOOL_FALSE)};
    case LMD_TYPE_INT: {
        if (type == &LIT_INT) {
            return (Item){.item = i2it(parse_int_literal_span(f->module->source,
                node->source_span))};
        }
        const int64_t* slot = (const int64_t*)interp_const_at(f->module, tc->const_index);
        return (Item){.item = i2it(slot ? *slot : 0)};
    }
    case LMD_TYPE_FLOAT: {
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
    case OPERATOR_INTERSECT: return fn_intersect(left, right);
    case OPERATOR_EXCLUDE:   return fn_exclude(left, right);
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
static bool interp_parameter_rejects_error(const AstNamedNode* parameter,
        Item value);
static Item interp_call_with_borrowed(Function* fn, const Item* args, int argc,
        InterpFrame* caller, NameEntry* const* borrowed_entries,
        uint64_t* const* borrow_homes);

// CW33 (COW §11.10): the address of a binding's Item home, when plain stores
// through it fully update the binding. View-state overlays are excluded
// (their writes must also run tmpl_state_set) and object-field entries have
// no slot of their own; both keep the legacy entry write-back channel.
static uint64_t* interp_borrow_home(InterpFrame* f, NameEntry* entry) {
    if (!f || !entry || !entry->storage_assigned) return NULL;
    if (entry->binding_storage != BINDING_STORAGE_REGISTER) return NULL;
    if ((uint32_t)entry->slot >= f->scratch_base) return NULL;
    if (interp_is_object_field_entry(entry)) return NULL;
    for (InterpViewBinding* binding = f->st ? f->st->view_bindings : NULL;
            binding; binding = binding->prev) {
        if (binding->entry == entry) return NULL;
    }
    return &f->slots[entry->slot];
}
static Function* interp_make_method_closure(Script* module,
        const TypeMethod* method, Item self);
static void interp_upgrade_function_entry(Function* fn, const AstFuncNode* def,
        void* entry);


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
static bool interp_typed_map_assignment(AstNode* node) {
    if (!node || (node->node_type != AST_NODE_INDEX_ASSIGN_STAM &&
            node->node_type != AST_NODE_MEMBER_ASSIGN_STAM)) return false;
    AstCompoundAssignNode* assignment = (AstCompoundAssignNode*)node;
    AstCowPath path = {};
    if (!ast_collect_cow_path(&path, assignment->object) ||
            !path.root || path.root->node_type != AST_NODE_IDENT) return false;
    NameEntry* root = ((AstIdentNode*)path.root)->entry;
    return root && ast_declared_type_is_map(root->declared_type);
}

static bool interp_typed_array_assignment(AstNode* node) {
    if (!node || (node->node_type != AST_NODE_INDEX_ASSIGN_STAM &&
            node->node_type != AST_NODE_MEMBER_ASSIGN_STAM)) return false;
    AstCompoundAssignNode* assignment = (AstCompoundAssignNode*)node;
    AstCowPath path = {};
    if (!ast_collect_cow_path(&path, assignment->object) ||
            !path.root || path.root->node_type != AST_NODE_IDENT) return false;
    NameEntry* root = ((AstIdentNode*)path.root)->entry;
    // A typed array store has its own checked setter even when the enclosing
    // procedure does not declare `^E`; dropping that setter's ItemError would
    // turn a rejected element into a successful statement (S7.6).
    return root && ast_declared_array_element(root->declared_type) != NULL;
}

// T0 discards procedural statement values in content blocks just as MIR does.
// preserve a failure before that discard when the enclosing procedure exposes
// an error channel, or a direct checked map store already owns that boundary.
static void interp_propagate_proc_side_effect_error(InterpFrame* f,
        AstNode* node, Item value) {
    if (!f || !node || !item_is_error(value) ||
            !side_effect_result_can_error(node->node_type)) return;
    TypeFunc* signature = f->fn ? (TypeFunc*)((AstNode*)f->fn)->type : NULL;
    if (interp_typed_map_assignment(node) || interp_typed_array_assignment(node) ||
            (signature && signature->type_id == LMD_TYPE_FUNC && signature->can_raise)) {
        interp_signal(f, EvalSignal::RETURNED, value);
    }
}

static Item interp_call_js_export(Function* function, const uint64_t* words,
        int argc, uint64_t* result_home) {
    const Item* args = (const Item*)(const void*)words;
    switch (argc) {
    case 0: return js_call_export_0_into(function, result_home);
    case 1: return js_call_export_1_into(function, args[0], result_home);
    case 2: return js_call_export_2_into(function, args[0], args[1], result_home);
    case 3: return js_call_export_3_into(function, args[0], args[1], args[2], result_home);
    case 4: return js_call_export_4_into(function, args[0], args[1], args[2], args[3], result_home);
    case 5: return js_call_export_5_into(function, args[0], args[1], args[2], args[3], args[4], result_home);
    case 6: return js_call_export_6_into(function, args[0], args[1], args[2], args[3], args[4], args[5], result_home);
    case 7: return js_call_export_7_into(function, args[0], args[1], args[2], args[3], args[4], args[5], args[6], result_home);
    case 8: return js_call_export_8_into(function, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], result_home);
    default:
        log_error("interp: JavaScript import call arity %d exceeds bridge limit", argc);
        return ItemError;
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
        bool tail_handoff_candidate = interp_tail_handoff_candidate(f);
        RootSpan handoff_args(tail_handoff_candidate ? (size_t)params : 0);
        if (tail_handoff_candidate && params > 0 && !handoff_args.valid()) return ItemError;
        uint64_t* handoff_words = handoff_args.words();
        AstNode* resolved_args[LAMBDA_MAX_FUNCTION_ARGS] = {0};
        if (has_named_args) {
            ast_resolve_call_args(node->argument, direct_fn, source_argc, resolved_args);
        }
        AstNode* positional = node->argument;
        AstNamedNode* parameter = f->fn->param;
        bool fresh_parameter_rejection = false;
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
            if (tail_handoff_candidate) handoff_words[i] = words[i];
            if (parameter) {
                Item source = (Item){.item = words[i]};
                char boundary[192];
                interp_format_parameter_boundary(boundary, sizeof(boundary),
                    f->fn, f->fn && f->fn->name ? f->fn->name->chars : NULL, i);
                Item coerced = interp_coerce_parameter_binding(f,
                    source, parameter, boundary);
                words[i] = coerced.item;
                // An incoming ItemError is an expression value returned to the
                // tail-call expression; only a failed non-error conversion
                // has MIR's return-before-body boundary semantics.
                fresh_parameter_rejection = fresh_parameter_rejection ||
                    (!item_is_error(source) &&
                     interp_parameter_rejects_error(parameter, coerced));
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
            // the same fresh rejected-parameter exit before the callee body
            // sees it. An already-error argument instead returns normally to
            // this expression, allowing its enclosing `or` or handler to run.
            if (fresh_parameter_rejection) {
                interp_signal(f, EvalSignal::RETURNED, rejected);
            }
            return rejected;
        }
        for (int p = 0; p < (int)params; p++) {
            f->slots[p] = words[p];
        }
        // tco reuses this frame instead of re-entering lambda_dynamic_call;
        // count the logical recursive invocation and its entry-equivalent
        // tail edge here. A direct tail boundary can hand off to T1 without
        // materializing arbitrary interpreter locals (D8.1.1v5).
        bool tail_hot = interp_note_tail_call(f);
        if (tail_hot && tail_handoff_candidate && f->callable_slot) {
            Function* callable = (Function*)(uintptr_t)*f->callable_slot;
            if (interp_promote_function_from_tail(callable)) {
                // the public wrapper must receive source values, not the T0
                // binding conversions above, or a handoff could apply a
                // parameter contract twice with different fault boundaries.
                for (int p = 0; p < (int)params; p++) {
                    f->slots[p] = handoff_words[p];
                }
                f->signal = EvalSignal::TAIL_CALL_JIT;
                return ItemNull;
            }
        }
        // MIR's self-tail loop has no write to its hidden `_vargs` parameter:
        // it rebinds fixed slots only, so the initial rest-list remains the
        // activation's `varg()` view for every iteration.
        f->signal = EvalSignal::TAIL_CALL;
        return ItemNull;
    }

    int named_param_count = argc;
    if (has_named_args && !interp_named_sys_args_supported(callee)) {
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
            return interp_coerce_declared_numeric(f, source, target_type,
                "numeric conversion");
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
                (sinfo->fn == SYSPROC_PUSH || sinfo->fn == SYSPROC_SPLICE ||
                 sinfo->fn == SYSPROC_VMAP_SET)) {
            // MIR evaluates the non-owner operands before it detaches a shared
            // binding. Calling a mutating helper directly would update both
            // aliases; use its shared COW entry and publish the replacement.
            RootSpan value_args((size_t)(argc - 1));
            uint64_t* values = value_args.words();
            AstNode* value_node = node->argument->next;
            for (int i = 0; value_node; value_node = value_node->next, i++) {
                values[i] = eval_expr(f, value_node).item;
                if (interp_frame_pending(f)) return ItemNull;
            }
            Item owner = interp_read_binding(f, owner_entry);
            Item replacement = ItemError;
            if (sinfo->fn == SYSPROC_PUSH) {
                replacement = pn_push_cow(owner, (Item){.item = values[0]});
            } else if (sinfo->fn == SYSPROC_SPLICE) {
                replacement = pn_splice_cow(owner, (Item){.item = values[0]},
                    (Item){.item = values[1]});
            } else {
                // VMap owns its storage behind a vtable, so map_set_cow cannot
                // model its snapshot. vmap_set_cow is the matching COW bridge.
                replacement = vmap_set_cow(owner, (Item){.item = values[0]},
                    (Item){.item = values[1]});
            }
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

    AstFieldNode* method_member = callee && callee->node_type == AST_NODE_MEMBER_EXPR
        ? (AstFieldNode*)callee : NULL;
    AstNode* method_name_node = method_member
        ? ast_unwrap_primary(method_member->field) : NULL;
    AstNode* receiver_node = method_member
        ? ast_unwrap_primary(method_member->object) : NULL;
    AstIdentNode* method_name = method_name_node &&
            method_name_node->node_type == AST_NODE_IDENT
        ? (AstIdentNode*)method_name_node : NULL;
    AstIdentNode* receiver_ident = receiver_node &&
            receiver_node->node_type == AST_NODE_IDENT
        ? (AstIdentNode*)receiver_node : NULL;
    TypeObject* static_receiver_type = method_member && method_member->object &&
            method_member->object->type &&
            type_nominal_record(method_member->object->type) != NULL
        ? (TypeObject*)method_member->object->type : NULL;
    TypeMethod* object_method = static_receiver_type && method_name
        ? ast_lookup_object_method(static_receiver_type, method_name->name) : NULL;
    if (!injected && object_method && object_method->ast_def) {
        if (!receiver_ident || !receiver_ident->entry || receiver_ident->entry->import ||
                object_method->ast_def->captures ||
                ast_type_func_has_var_parameter(object_method->fn_type) ||
                has_named_args) {
            log_error("interp: unsupported object method call boundary");
            return ItemError;
        }
        Scratch receiver(f);
        receiver.set(eval_expr(f, method_member->object));
        if (interp_frame_pending(f)) return receiver.get();
        // D2.6.6v2 phase 2: a method receiver is any value carrying a record.
        if (!lambda_value_nominal(get_type_id(receiver.get()),
                (const void*)(uintptr_t)receiver.get().item)) {
            log_error("interp: object method receiver is not an object");
            return ItemError;
        }
        if (object_method->is_proc) {
            if (!node->is_proc_method || !receiver_ident->entry->is_mutable) {
                log_error("interp: procedural object method needs a mutable direct receiver");
                return ItemError;
            }
            // The bound closure snapshots self, so publish the private COW
            // receiver before its body can write implicit object fields.
            Item replacement = cow_prepare_write(receiver.get());
            if (item_is_error(replacement)) return replacement;
            receiver.set(replacement);
            interp_write_binding(f, receiver_ident->entry, replacement);
        }
        Scratch method_fn(f);
        method_fn.set(interp_ptr_item(interp_make_method_closure(f->module,
            object_method, receiver.get())));
        if (item_is_error(method_fn.get()) || method_fn.get().item == ITEM_NULL) {
            return ItemError;
        }
        RootSpan method_args((size_t)(source_argc > 0 ? source_argc : 1));
        uint64_t* words = method_args.words();
        int index = 0;
        for (AstNode* argument = node->argument; argument; argument = argument->next) {
            words[index++] = eval_expr(f, argument).item;
            if (interp_frame_pending(f)) return ItemNull;
        }
        List args = {};
        args.length = source_argc;
        args.items = (Item*)(void*)words;
        uint64_t result_home = 0;
        return fn_call_into((Function*)(uintptr_t)method_fn.get().item,
            source_argc ? &args : NULL, &result_home);
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
    AstIdentNode* imported_ident = callee && callee->node_type == AST_NODE_IDENT
        ? (AstIdentNode*)callee : NULL;
    bool cross_lang_js_call = imported_ident && imported_ident->entry &&
        imported_ident->entry->import && imported_ident->entry->import->is_cross_lang &&
        imported_ident->entry->import->script &&
        imported_ident->entry->import->script->profile == &js_profile;
    if (cross_lang_js_call) {
        if (injected || has_named_args) {
            log_error("interp: JavaScript imports require positional arguments");
            return ItemError;
        }
        uint64_t result_home = 0;
        return interp_call_js_export(fn, words, dispatch_argc, &result_home);
    }
    if (!injected && ast_type_func_has_var_parameter(direct_signature)) {
        NameEntry* borrowed[LAMBDA_MAX_FUNCTION_ARGS] = {0};
        uint64_t* borrow_homes[LAMBDA_MAX_FUNCTION_ARGS] = {0};
        AstNode* borrow_args[LAMBDA_MAX_FUNCTION_ARGS] = {0};
        if (!ast_direct_call_var_parameter_entries(node, direct_signature, borrowed,
                borrow_args)) {
            log_error("interp: unsupported direct `var` argument layout");
            return ItemError;
        }
        for (int index = 0; index < dispatch_argc; index++) {
            NameEntry* entry = borrowed[index];
            // CW25 / S9.2.2: `f(var m.rows[i])` borrows a PLACE. Detach the
            // whole spine and pass the detached leaf; it is already installed
            // in its parent, so the callee's in-place `var` writes reach the
            // caller's container and need no writeback binding. Mirrors the
            // MIR argument-loop hook so the tiers cannot diverge.
            if (!entry && borrow_args[index]) {
                AstCowPath place = {};
                if (!ast_collect_cow_path(&place, borrow_args[index]) ||
                        place.count == 0 || !place.root ||
                        place.root->node_type != AST_NODE_IDENT) {
                    continue;
                }
                NameEntry* root_entry = ((AstIdentNode*)place.root)->entry;
                if (!root_entry) continue;
                Scratch root_slot(f);
                root_slot.set(interp_read_binding(f, root_entry));
                if (!is_container_type_id(get_type_id(root_slot.get()))) continue;
                Item private_root = cow_prepare_write(root_slot.get());
                if (item_is_error(private_root)) return private_root;
                root_slot.set(private_root);
                interp_write_binding(f, root_entry, root_slot.get());

                Scratch path_slot(f);
                path_slot.set(interp_ptr_item(array_plain()));
                bool path_ok = true;
                for (int seg = 0; seg < place.count && path_ok; seg++) {
                    Scratch key_slot(f);
                    key_slot.set(interp_eval_cow_path_key(f, place.segment[seg],
                        place.is_member[seg]));
                    if (interp_frame_pending(f)) return ItemNull;
                    Array* keys = (Array*)(uintptr_t)path_slot.get().item;
                    if (!keys) { path_ok = false; break; }
                    array_push(keys, key_slot.get());
                }
                if (!path_ok) continue;
                Item leaf = cow_path_borrow(root_slot.get(), path_slot.get());
                if (item_is_error(leaf)) return leaf;
                words[index] = leaf.item;
                continue;
            }
            if (!entry) continue;
            uint64_t* home = interp_borrow_home(f, entry);
            if (home) {
                // CW33: pass the ADDRESS of the caller's binding home. The
                // callee prologue prepares through it unconditionally (a
                // byte-test no-op when unique), which deletes the caller-side
                // cow_owned gate -- the T0 twin of the MIR allow-list gate
                // that silently skipped un-share-at-borrow for composite
                // declared types.
                borrow_homes[index] = home;
                // the entry is retained beside the home: the callee prologue
                // reads is_var_param off it to distinguish a chain-root
                // borrow (prepare) from a re-borrow (inherit uniqueness)
                continue;
            }
            Item owner = (Item){.item = words[index]};
            if (!entry->cow_owned ||
                    !is_container_type_id(get_type_id(owner))) {
                continue;
            }
            // Legacy channel (view-state / object-field homes): detach before
            // the call because the callee publishes only at return.
            Item private_owner = cow_prepare_write(owner);
            if (item_is_error(private_owner)) return private_owner;
            words[index] = private_owner.item;
            interp_write_binding(f, entry, private_owner);
        }
        return interp_call_with_borrowed(fn, (const Item*)(void*)words,
            dispatch_argc, f, borrowed, borrow_homes);
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
    if (module && module->interp_whole_script_poc_active) {
        // A definition materialized after the trigger should join the sealed
        // whole-module image when it has the same no-capture boxed contract.
        (void)interp_whole_script_publish_function(module, (AstFuncNode*)fn_node, fn);
    }

    // Task-backed procedures use MIR's resumable state machine at their first
    // entry. T0 owns the surrounding module activation, but it must not turn
    // an async procedure into a synchronous AST call: publish the generated
    // boxed satellite before the function value escapes (D8.1.1v2 / D5.1.3).
    if (fn_node->node_type == AST_NODE_PROC && fn_node->analysis &&
            (fn_node->analysis->may_await || fn_node->analysis->needs_task_context)) {
        InterpState* st = interp_current_state();
        void* entry = NULL;
        if (st && st->runtime && compile_ast_function_satellite(
                st->runtime, module, fn_node, &entry) && entry) {
            interp_upgrade_function_entry(fn, fn_node, entry);
        } else {
            log_error("interp: async procedure '%s' could not publish its MIR satellite",
                fn_node->name ? fn_node->name->chars : "<anonymous>");
        }
    }

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

static Function* interp_make_method_closure(Script* module,
        const TypeMethod* method, Item self) {
    Script* definition_module = method ? method->ast_module : NULL;
    if (!definition_module) definition_module = module;
    if (!definition_module || !method || !method->ast_def || method->ast_def->captures) {
        return NULL;
    }
    RootFrame roots(2);
    Rooted<void*> env(roots, heap_calloc_closure_env(sizeof(Item)));
    if (!env.get()) return NULL;
    Rooted<Function*> fn(roots, to_closure_named(NULL, method->arity, env.get(),
        method->ast_def->name ? method->ast_def->name->chars : NULL));
    if (!fn.get()) return NULL;
    // A method receiver is an ordinary closure Item so forced GC updates it
    // before implicit field reads and writes resume inside the callee frame.
    owned_item_slot_store((Item*)env.get(), 1, 0, self);
    fn.get()->closure_field_count = 1;
    fn.get()->entry_abi = FN_ENTRY_ABI_LAMBDA_INTERPRETED;
    fn.get()->def = method->ast_def;
    // Imported nominal methods retain an AST pointer into their declaring
    // Script. Reusing the caller's module here reads unrelated slab slots and
    // makes field-dependent bodies observe zero/null instead of the receiver.
    fn.get()->def_module = definition_module;
    fn.get()->method = method;
    fn.get()->runtime_context = (Context*)context;
    lambda_function_set_type(fn.get(), method->fn_type);
    return fn.get();
}

// S12.3.3v2: bare `obj.m` is a bound value, not only a call. The runtime
// member lanes live in lambda-eval/lambda-data-runtime and cannot reach T0's
// receiver-boxing convention (a GC-visible env slot, not a raw self pointer),
// so this is the seam they bind an un-JITted method through. The module falls
// out of the method's own ast_module.
Function* interp_bind_object_method(const TypeMethod* method, Item self) {
    return interp_make_method_closure(NULL, method, self);
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
    INTERP_ARRAY_UINT64,
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
        // Declared u64[] literals use the same ELEM_UINT64 carrier as MIR;
        // generic mixed arrays are handled by the lane-preserving read bridge
        // in interp_item_at rather than by widening this compact declaration.
        return INTERP_ARRAY_UINT64;
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
            kind == INTERP_ARRAY_UINT64 ? array_num_new(ELEM_UINT64, count) :
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
        if (item->node_type == AST_NODE_VARIABLE_DECLARATOR) {
            AstDeclaratorNode* named = (AstDeclaratorNode*)item;
            Item bound = eval_expr(f, named->init);
            if (interp_frame_pending(f)) return acc.get();
            if (!interp_bind_declared_value(f, named, bound)) {
                return interp_signal_payload(f);
            }
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
        } else if (ast_expr_insertion_needs_capture(item)) {
            // S9.3.1: only a NAMED element needs capture; a fresh one has no
            // second observer.
            array_push_capture(arr, value);
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
    if (node->has_computed_key) {
        Scratch owner(f);
        owner.set(map_literal_begin());
        if (get_type_id(owner.get()) == LMD_TYPE_ERROR) {
            interp_signal(f, EvalSignal::RETURNED, owner.get());
            return owner.get();
        }
        for (AstNode* item = node->item; item; item = item->next) {
            AstNamedNode* named = item->node_type == AST_NODE_KEY_EXPR
                ? (AstNamedNode*)item : NULL;
            if (!named) return ItemError;
            if (named->is_spread) {
                Scratch source(f);
                source.set(named->as ? eval_expr(f, named->as) : ItemNull);
                if (interp_frame_pending(f)) return ItemNull;
                Item result = map_literal_spread(owner.get(), source.get());
                if (get_type_id(result) == LMD_TYPE_ERROR) {
                    interp_signal(f, EvalSignal::RETURNED, result);
                    return result;
                }
                owner.set(result);
                continue;
            }
            Scratch key(f);
            key.set(named->key ? eval_expr(f, named->key) : (Item){.item = s2it(
                heap_strcpy(named->name ? named->name->chars : "",
                    named->name ? named->name->len : 0))});
            if (interp_frame_pending(f)) return ItemNull;
            Scratch value(f);
            value.set(named->as ? eval_expr(f, named->as) : ItemNull);
            if (interp_frame_pending(f)) return ItemNull;
            if (named->as && ast_expr_insertion_needs_capture(named->as)) {
                cow_capture_value(value.get());
            }
            Item result = map_literal_put(owner.get(), key.get(), value.get());
            if (get_type_id(result) == LMD_TYPE_ERROR) {
                interp_signal(f, EvalSignal::RETURNED, result);
                return result;
            }
            owner.set(result);
        }
        return owner.get();
    }
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
        Item value = value_node ? eval_expr(f, value_node) : ItemNull;
        // S9.3.1: a named field value is captured into the literal.
        if (value_node && ast_expr_insertion_needs_capture(value_node)) {
            cow_capture_value(value);
        }
        words[vi++] = value.item;
    }
    Map* built = map_with_type_tl(map_type, f->module->type_list);
    if (!built) return ItemError;
    return interp_ptr_item(map_fill_items(built, (const Item*)(void*)words, vi));
}

static Item eval_object_literal(InterpFrame* f, AstObjectLiteralNode* node) {
    TypeObject* object_type = node ? (TypeObject*)node->type : NULL;
    if (!object_type || !type_nominal_record((Type*)object_type)) return ItemError;

    int field_count = (int)object_type->length;
    RootSpan values((size_t)(field_count > 0 ? field_count : 1));
    uint64_t* words = values.words();
    AstNode* spread_node = ast_object_literal_spread_value(node);
    Scratch spread(f);
    if (spread_node) spread.set(eval_expr(f, spread_node));
    ShapeEntry* field = object_type->shape;
    for (int index = 0; index < field_count && field; index++, field = field->next) {
        AstNode* value_node = ast_object_literal_value_for_shape(node, field);
        if (value_node) {
            Item value = eval_expr(f, value_node);
            // S9.3.1: a named field value is captured into the literal.
            if (ast_expr_insertion_needs_capture(value_node)) cow_capture_value(value);
            words[index] = value.item;
        } else if (spread_node && field->name) {
            // Preserve `*:source` fields before typed storage conversion; an
            // omitted float/int field must not be sent to set_field_value as a
            // null Item, which has no numeric payload to decode.
            Scratch key(f);
            key.set((Item){.item = s2it(heap_create_name(field->name->str,
                field->name->length))});
            words[index] = fn_member(spread.get(), key.get()).item;
        } else {
            words[index] = field->default_value
                ? eval_expr(f, field->default_value).item : ItemNull.item;
        }
        if (interp_frame_pending(f)) return ItemNull;
    }

    Object* fresh = object_with_tl(object_type->type_index, f->module->type_list);
    if (!fresh) return ItemError;
    Scratch object(f);
    object.set(interp_ptr_item(fresh));
    if (field_count > 0) {
        object_fill_items((Object*)(uintptr_t)object.get().item,
            (const Item*)(void*)words, field_count);
    }
    // S2.1.3: content children. Evaluated after the attributes so the object is
    // already published in `object` while the content list allocates, and the
    // receiver is re-read from the scratch slot after each item in case a
    // nested allocation collected.
    AstNode* content_node = node->content;
    if (content_node) {
        AstNode* first = ((AstListNode*)content_node)->item;
        int content_count = 0;
        for (AstNode* scan = first; scan; scan = scan->next) content_count++;
        if (content_count > 0) {
            RootSpan content_values((size_t)content_count);
            uint64_t* content_words = content_values.words();
            int ci = 0;
            for (AstNode* scan = first; scan; scan = scan->next) {
                Item value = eval_expr(f, scan);
                // S9.3.1: insertion captures by value, as for attributes.
                if (ast_expr_insertion_needs_capture(scan)) cow_capture_value(value);
                content_words[ci++] = value.item;
                if (interp_frame_pending(f)) return ItemNull;
            }
            object_content_fill_items((Object*)(uintptr_t)object.get().item,
                (const Item*)(void*)content_words, ci);
        }
    }
    return object.get();
}


// RAII push/pop for an implicit-context binding, so no early return can leave
// a stale `~` visible to an outer expression.
class InterpContextGuard {
    InterpState* st_;
    InterpContext entry_;
    bool active_;
public:
    InterpContextGuard(InterpState* st, uint64_t* item, uint64_t* index)
            : st_(st), entry_{item, index, NULL, NULL, st ? st->contexts : NULL},
              active_(st && item) {
        if (active_) st_->contexts = &entry_;
    }
    InterpContextGuard(InterpState* st, uint64_t* item, uint64_t* index,
            uint64_t* parent, uint64_t* root)
            : st_(st), entry_{item, index, parent, root, st ? st->contexts : NULL},
              active_(st && item) {
        if (active_) st_->contexts = &entry_;
    }
    ~InterpContextGuard() { if (active_) st_->contexts = entry_.prev; }
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

// Generic arrays own their wide scalar payload in the tail of the same
// container.  `array_get` intentionally canonicalizes a small u64 through the
// generic scalar reader, but a Lambda value read must retain its declared
// numeric identity just like the static MIR carrier.  Re-home that payload in
// the current number extent before publishing it from T0 (D3.2.2).
static Item interp_preserve_array_u64(Item source, int64_t index, Item value) {
    if (get_type_id(source) != LMD_TYPE_ARRAY || !source.array ||
            index < 0 || index >= source.array->length ||
            array_has_native_lane(source.array)) return value;
    // Native nullable lanes deliberately store raw words in `items`; treating
    // those words as Item values would dereference a lane such as `1` as a
    // pointer while probing its TypeId. Only generic Item arrays may be
    // inspected for a preserved u64 carrier (D3.2.2).
    Item raw = source.array->items[index];
    if (get_type_id(raw) == LMD_TYPE_UINT64) {
        return box_uint64_value(raw.get_uint64());
    }
    return value;
}

static Item interp_item_at(Item source, int64_t index) {
    Item value = item_at(source, index);
    return interp_preserve_array_u64(source, index, value);
}

static Item interp_iter_val_at(Item source, SymbolKeyList* keys, int64_t index,
        int key_filter, bool key_only) {
    Item value = key_only
        ? iter_key_at(source, keys, index, key_filter)
        : iter_val_at(source, keys, index, key_filter);
    return key_only ? value : interp_preserve_array_u64(source, index, value);
}

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
    // Elements are maps for attribute lookup but lists for pipe traversal: a
    // group Element's attributes describe its key while its children are the
    // rows. Treating it as a map here discarded every grouped row (S10.1.3).
    bool is_map = source_tid == LMD_TYPE_MAP || source_tid == LMD_TYPE_VMAP;
    SymbolKeyList* keys = is_map ? item_keys(source) : NULL;

    int64_t len = fn_seq_count(source);
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
                current = interp_item_at(left.get(), i);
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
    uint64_t*    keys;      // rooted Array* sort-key stream for ordered expressions
} ForCtx;

static bool interp_for_level(ForCtx* fc, AstLoopNode* loop);

// The row clauses run before a group is materialized. Keeping them separate
// from body emission protects the post-group scope: `into g` must not observe
// a stale row or `let` binding while aggregate expressions execute.
static bool interp_for_apply_row_clauses(ForCtx* fc, bool* keep) {
    InterpFrame* f = fc->f;
    *keep = true;
    for (AstNode* decl = fc->node->let_clause; decl; decl = decl->next) {
        if (decl->node_type == AST_NODE_VARIABLE_DECLARATOR) {
            AstDeclaratorNode* named = (AstDeclaratorNode*)decl;
            Item bound = eval_expr(f, named->init);
            if (interp_frame_pending(f)) return false;
            if (!interp_bind_declared_value(f, named, bound)) return false;
        } else if (decl->node_type == AST_NODE_DECOMPOSE) {
            exec_declaration(f, decl);
            if (interp_frame_pending(f)) return false;
        }
    }
    if (fc->node->where) {
        Item where = eval_expr(f, fc->node->where);
        if (interp_frame_pending(f)) return false;
        if (!is_truthy(where)) *keep = false;
    }
    return true;
}

// Emit an already-admitted row. Grouped for-expressions call this only after
// `fn_group_by_keys_items` has replaced the row scope with the `into` value.
static bool interp_for_emit_value(ForCtx* fc) {
    InterpFrame* f = fc->f;
    Item value = eval_expr(f, fc->node->then);
    if (interp_frame_pending(f)) return false;
    if (fc->output) {
        // Re-read the accumulator: the body evaluation above is a safepoint.
        Array* out = (Array*)(uintptr_t)*fc->output;
        if (out) array_push_spread(out, value);
    }
    if (fc->node->order) {
        AstOrderSpec* spec = (AstOrderSpec*)fc->node->order;
        Item key = eval_expr(f, spec->expr);
        if (interp_frame_pending(f)) return false;
        if (fc->keys) {
            Array* keys = (Array*)(uintptr_t)*fc->keys;
            if (keys) array_push(keys, key);
        }
    }
    return true;
}

// Innermost ordinary level: run the row clauses, then push the body value.
static bool interp_for_emit(ForCtx* fc) {
    bool keep = true;
    if (!interp_for_apply_row_clauses(fc, &keep)) return false;
    return !keep || interp_for_emit_value(fc);
}

static bool interp_for_level(ForCtx* fc, AstLoopNode* loop) {
    InterpFrame* f = fc->f;
    if (!loop) return interp_for_emit(fc);

    Item collection = eval_expr(f, loop->as);
    if (interp_frame_pending(f)) return false;
    Scratch coll_slot(f);
    coll_slot.set(collection);
    // CW30/S9.2.3: the body may write this collection's root, so the loop
    // walks the entry-time value. The mark makes the body's first write
    // detach into the BINDING; coll_slot keeps the original independently.
    if (loop->snapshot_collection) cow_mark_shared(coll_slot.get());

    // item_keys allocates; the collection must already be published.
    SymbolKeyList* keys = item_keys(coll_slot.get());
    int key_filter = (int)loop->key_filter;
    int64_t length = iter_len(coll_slot.get(), keys, key_filter);

    NameEntry* value_entry = interp_scope_lookup(fc->node->vars, loop->name);
    NameEntry* index_entry = loop->index_name
        ? interp_scope_lookup(fc->node->vars, loop->index_name) : NULL;

    bool ok = true;
    for (int64_t i = 0; i < length && ok; i++) {
        Item current = interp_iter_val_at(coll_slot.get(), keys, i, key_filter,
            loop->key_only);
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

// Append one grouping key in the representation accepted by fn_group_by_keys:
// a scalar for one key and an Array tuple for multiple keys. Each intermediate
// Item is frame-rooted because pushing it may grow a GC-managed array.
static bool interp_for_append_group_key(ForCtx* fc, uint64_t* key_stream_home) {
    InterpFrame* f = fc->f;
    AstGroupClause* group = fc->node->group;
    if (!group || !key_stream_home) return false;

    Scratch group_key(f);
    if (group->key_count <= 1) {
        AstGroupKey* spec = group->keys;
        group_key.set(spec && spec->expr ? eval_expr(f, spec->expr) : ItemNull);
        if (interp_frame_pending(f)) return false;
    } else {
        group_key.set(interp_ptr_item(array_plain()));
        for (AstGroupKey* spec = group->keys; spec;
                spec = (AstGroupKey*)((AstNode*)spec)->next) {
            Scratch key_part(f);
            key_part.set(spec->expr ? eval_expr(f, spec->expr) : ItemNull);
            if (interp_frame_pending(f)) return false;
            Array* tuple = (Array*)(uintptr_t)group_key.get().item;
            if (tuple) array_push(tuple, key_part.get());
        }
    }
    Array* key_stream = (Array*)(uintptr_t)*key_stream_home;
    if (key_stream) array_push(key_stream, group_key.get());
    return true;
}

// MIR collects source rows and keys before it constructs group Elements. Do
// that same two-stage work here; a grouped expression can therefore reuse the
// runtime's numeric equality, null handling, key aliases, and member shape.
static Array* interp_for_collect_groups(ForCtx* fc, AstLoopNode* loop) {
    InterpFrame* f = fc->f;
    AstGroupClause* group = fc->node->group;
    if (!group || !group->entry || !loop || loop->next) return NULL;

    Item collection = eval_expr(f, loop->as);
    if (interp_frame_pending(f)) return NULL;
    Scratch collection_slot(f);
    collection_slot.set(collection);
    Scratch rows_slot(f);
    rows_slot.set(interp_ptr_item(array_plain()));
    Scratch key_stream_slot(f);
    key_stream_slot.set(interp_ptr_item(array_plain()));

    SymbolKeyList* keys = item_keys(collection_slot.get());
    int key_filter = (int)loop->key_filter;
    int64_t length = iter_len(collection_slot.get(), keys, key_filter);
    NameEntry* value_entry = interp_scope_lookup(fc->node->vars, loop->name);
    NameEntry* index_entry = loop->index_name
        ? interp_scope_lookup(fc->node->vars, loop->index_name) : NULL;

    for (int64_t i = 0; i < length; i++) {
        Scratch row_slot(f);
        row_slot.set(interp_iter_val_at(collection_slot.get(), keys, i,
            key_filter, loop->key_only));
        interp_write_binding(f, value_entry, row_slot.get());
        if (index_entry) {
            interp_write_binding(f, index_entry,
                iter_key_at(collection_slot.get(), keys, i, key_filter));
        }

        bool keep = true;
        if (!interp_for_apply_row_clauses(fc, &keep)) break;
        if (keep) {
            Array* rows = (Array*)(uintptr_t)rows_slot.get().item;
            if (rows) array_push(rows, row_slot.get());
            if (!interp_for_append_group_key(fc, key_stream_slot.home())) break;
        }
        if (i + 1 < length) interp_note_backedge(f);
    }
    if (keys) symbol_key_list_free(keys);
    if (interp_frame_pending(f)) return NULL;

    Scratch aliases_slot(f);
    aliases_slot.set(interp_ptr_item(array_plain()));
    for (AstGroupKey* spec = group->keys; spec;
            spec = (AstGroupKey*)((AstNode*)spec)->next) {
        // Group aliases originate in the parser's name pool; the helper reads
        // a String-tagged Item, so a raw pointer would decode as null attrs.
        Scratch alias_slot(f);
        alias_slot.set((Item){.item = s2it(heap_create_name(
            spec->alias ? spec->alias->chars : ""))});
        Array* aliases = (Array*)(uintptr_t)aliases_slot.get().item;
        if (aliases) array_push(aliases, alias_slot.get());
    }
    return fn_group_by_keys_items(rows_slot.get(), key_stream_slot.get(),
        aliases_slot.get());
}

// Finalize every demanded comprehension stream in the same place so plain and
// grouped `for`s share the ordered-window and spreadable-result invariants.
static Item interp_for_finalize_output(InterpFrame* f, AstForNode* for_node,
        uint64_t* output_home, uint64_t* key_stream_home) {
    Array* out = output_home ? (Array*)(uintptr_t)*output_home : NULL;
    if (!out) return ItemNull;
    if (for_node->order) {
        AstOrderSpec* spec = (AstOrderSpec*)for_node->order;
        // MIR records one key per emitted row, then delegates ordering to this
        // shared stable helper before applying ordered windows (S6).
        fn_sort_by_keys(interp_ptr_item(out),
            (Item){.item = key_stream_home ? *key_stream_home : ITEM_NULL},
            spec->descending ? 1 : 0);
        if (for_node->offset) {
            Item offset = eval_expr(f, for_node->offset);
            if (interp_frame_pending(f) || item_is_error(offset)) return offset;
            out = output_home ? (Array*)(uintptr_t)*output_home : NULL;
            if (out) array_drop_inplace(out, it2l(offset));
        }
        if (for_node->limit) {
            Item limit = eval_expr(f, for_node->limit);
            if (interp_frame_pending(f) || item_is_error(limit)) return limit;
            out = output_home ? (Array*)(uintptr_t)*output_home : NULL;
            if (out) {
                if (for_node->limit_from_end) array_limit_last_inplace(out, it2l(limit));
                else array_limit_inplace(out, it2l(limit));
            }
        }
        // Sorting follows MIR's dedicated ordered-output path, which closes
        // the spreadable stream before the result reaches its enclosing list.
        out = output_home ? (Array*)(uintptr_t)*output_home : NULL;
        Item result = out ? array_end(out) : ItemNull;
        return result.item == ITEM_NULL_SPREADABLE && out ? interp_ptr_item(out) : result;
    }
    if (for_node->offset || for_node->limit) {
        Scratch selected(f);
        selected.set((Item){.item = *output_home});
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
    return result.item == ITEM_NULL_SPREADABLE ? interp_ptr_item(out) : result;
}

static Item interp_eval_grouped_for(ForCtx* fc, AstLoopNode* loop,
        bool result_demanded, Scratch& out_slot, Scratch& key_slot) {
    InterpFrame* f = fc->f;
    AstForNode* for_node = fc->node;
    if (!loop || loop->next || !for_node->group || !for_node->group->entry) {
        log_error("interp: grouped for requires one source and an into binding");
        return ItemError;
    }

    Array* groups = interp_for_collect_groups(fc, loop);
    if (interp_frame_pending(f)) return ItemNull;
    Scratch groups_slot(f);
    groups_slot.set(interp_ptr_item(groups));
    int64_t group_count = groups ? groups->length : 0;
    bool ok = true;
    for (int64_t i = 0; i < group_count && ok; i++) {
        groups = (Array*)(uintptr_t)groups_slot.get().item;
        if (!groups || i >= groups->length) break;
        Scratch group_slot(f);
        group_slot.set(groups->items[i]);
        interp_write_binding(f, for_node->group->entry, group_slot.get());
        ok = interp_for_emit_value(fc);
        if (f->signal == EvalSignal::BROKE) { interp_clear_loop_signal(f); break; }
        if (f->signal == EvalSignal::CONTINUED) { interp_clear_loop_signal(f); ok = true; }
        if (ok && i + 1 < group_count) interp_note_backedge(f);
    }
    if (!result_demanded) return ItemNull;

    return interp_for_finalize_output(f, for_node, out_slot.home(), key_slot.home());
}

static Item interp_join_name_item(String* name) {
    return name ? (Item){.item = s2it(heap_create_name(name->chars))} : ItemNull;
}

// Join keys use the identical scalar-or-tuple representation that the shared
// hash helper consumes. The relevant source/tuple bindings are already in
// frame slots before this runs, matching the two JIT key-evaluation sites.
static bool interp_join_make_key(InterpFrame* f, AstLoopNode* loop,
        bool use_new_side, Scratch& key_slot) {
    if (!loop || loop->join_key_count <= 0) {
        key_slot.set(ItemNull);
        return true;
    }
    if (loop->join_key_count == 1) {
        AstJoinKey* key = loop->join_keys;
        AstNode* expr = key ? (use_new_side ? key->new_expr : key->prior_expr) : NULL;
        key_slot.set(expr ? eval_expr(f, expr) : ItemNull);
        return !interp_frame_pending(f);
    }
    key_slot.set(interp_ptr_item(array_plain()));
    for (AstJoinKey* key = loop->join_keys; key;
            key = (AstJoinKey*)((AstNode*)key)->next) {
        Scratch part_slot(f);
        AstNode* expr = use_new_side ? key->new_expr : key->prior_expr;
        part_slot.set(expr ? eval_expr(f, expr) : ItemNull);
        if (interp_frame_pending(f)) return false;
        Array* tuple = (Array*)(uintptr_t)key_slot.get().item;
        if (tuple) array_push(tuple, part_slot.get());
    }
    return true;
}

// Collect a join source once, including its source index/key stream. The row
// binding is live while its new-side key is evaluated, so a key expression can
// use the current source exactly as MIR's mir_join_collect_source does.
static bool interp_join_collect_source(ForCtx* fc, AstLoopNode* loop,
        bool collect_keys, Scratch& rows_slot, Scratch& row_keys_slot,
        Scratch& indices_slot) {
    InterpFrame* f = fc->f;
    Item collection = eval_expr(f, loop->as);
    if (interp_frame_pending(f)) return false;
    Scratch collection_slot(f);
    collection_slot.set(collection);
    SymbolKeyList* keys = item_keys(collection_slot.get());
    int key_filter = (int)loop->key_filter;
    int64_t length = iter_len(collection_slot.get(), keys, key_filter);
    NameEntry* value_entry = interp_scope_lookup(fc->node->vars, loop->name);
    NameEntry* index_entry = loop->index_name
        ? interp_scope_lookup(fc->node->vars, loop->index_name) : NULL;

    for (int64_t i = 0; i < length; i++) {
        Scratch row_slot(f);
        row_slot.set(interp_iter_val_at(collection_slot.get(), keys, i,
            key_filter, loop->key_only));
        interp_write_binding(f, value_entry, row_slot.get());
        Scratch source_index(f);
        if (index_entry) {
            source_index.set(iter_key_at(collection_slot.get(), keys, i, key_filter));
            interp_write_binding(f, index_entry, source_index.get());
        }
        Array* rows = (Array*)(uintptr_t)rows_slot.get().item;
        if (rows) array_push(rows, row_slot.get());
        if (index_entry) {
            Array* indices = (Array*)(uintptr_t)indices_slot.get().item;
            if (indices) array_push(indices, source_index.get());
        }
        if (collect_keys) {
            Scratch key_slot(f);
            if (!interp_join_make_key(f, loop, true, key_slot)) break;
            Array* row_keys = (Array*)(uintptr_t)row_keys_slot.get().item;
            if (row_keys) array_push(row_keys, key_slot.get());
        }
        if (interp_frame_pending(f)) break;
        if (i + 1 < length) interp_note_backedge(f);
    }
    if (keys) symbol_key_list_free(keys);
    return !interp_frame_pending(f);
}

static void interp_join_bind_tuple(ForCtx* fc, AstLoopNode* first,
        AstLoopNode* stop_before, Item tuple) {
    InterpFrame* f = fc->f;
    for (AstLoopNode* loop = first; loop && loop != stop_before;
            loop = (AstLoopNode*)((AstNode*)loop)->next) {
        NameEntry* value_entry = interp_scope_lookup(fc->node->vars, loop->name);
        interp_write_binding(f, value_entry,
            loop->name ? item_attr(tuple, loop->name->chars) : ItemNull);
        if (loop->index_name) {
            NameEntry* index_entry = interp_scope_lookup(fc->node->vars, loop->index_name);
            interp_write_binding(f, index_entry,
                item_attr(tuple, loop->index_name->chars));
        }
    }
}

static Item interp_eval_join_for(ForCtx* fc, AstLoopNode* first,
        bool result_demanded, Scratch& out_slot, Scratch& key_slot) {
    InterpFrame* f = fc->f;
    AstForNode* for_node = fc->node;
    if (!first) return ItemNull;

    Scratch tuples_slot(f);
    tuples_slot.set(ItemNull);
    {
        Scratch seed_rows(f);
        Scratch seed_keys(f);
        Scratch seed_indices(f);
        seed_rows.set(interp_ptr_item(array_plain()));
        seed_keys.set(interp_ptr_item(array_plain()));
        seed_indices.set(first->index_name ? interp_ptr_item(array_plain()) : ItemNull);
        if (!interp_join_collect_source(fc, first, false, seed_rows, seed_keys, seed_indices)) {
            return ItemNull;
        }
        Scratch name_slot(f);
        Scratch index_name_slot(f);
        name_slot.set(interp_join_name_item(first->name));
        index_name_slot.set(interp_join_name_item(first->index_name));
        Array* tuples = fn_join_seed_tuples(seed_rows.get(), name_slot.get(),
            index_name_slot.get(), seed_indices.get());
        tuples_slot.set(interp_ptr_item(tuples));
    }

    for (AstLoopNode* cur = (AstLoopNode*)((AstNode*)first)->next; cur;
            cur = (AstLoopNode*)((AstNode*)cur)->next) {
        Scratch rows_slot(f);
        Scratch row_keys_slot(f);
        Scratch indices_slot(f);
        rows_slot.set(interp_ptr_item(array_plain()));
        row_keys_slot.set(interp_ptr_item(array_plain()));
        indices_slot.set(cur->index_name ? interp_ptr_item(array_plain()) : ItemNull);
        if (!interp_join_collect_source(fc, cur, cur->on != NULL, rows_slot,
                row_keys_slot, indices_slot)) return ItemNull;

        Scratch name_slot(f);
        Scratch index_name_slot(f);
        name_slot.set(interp_join_name_item(cur->name));
        index_name_slot.set(interp_join_name_item(cur->index_name));
        if (!cur->on) {
            Array* tuples = fn_cross_join_tuples(tuples_slot.get(), rows_slot.get(),
                name_slot.get(), index_name_slot.get(), indices_slot.get());
            tuples_slot.set(interp_ptr_item(tuples));
            continue;
        }

        Scratch prior_keys_slot(f);
        prior_keys_slot.set(interp_ptr_item(array_plain()));
        Array* prior_tuples = (Array*)(uintptr_t)tuples_slot.get().item;
        int64_t tuple_count = prior_tuples ? prior_tuples->length : 0;
        for (int64_t i = 0; i < tuple_count; i++) {
            prior_tuples = (Array*)(uintptr_t)tuples_slot.get().item;
            if (!prior_tuples || i >= prior_tuples->length) break;
            Scratch tuple_slot(f);
            tuple_slot.set(prior_tuples->items[i]);
            interp_join_bind_tuple(fc, first, cur, tuple_slot.get());
            Scratch prior_key(f);
            if (!interp_join_make_key(f, cur, false, prior_key)) return ItemNull;
            Array* prior_keys = (Array*)(uintptr_t)prior_keys_slot.get().item;
            if (prior_keys) array_push(prior_keys, prior_key.get());
        }
        if (interp_frame_pending(f)) return ItemNull;
        Array* tuples = fn_hash_join_tuples(tuples_slot.get(), prior_keys_slot.get(),
            rows_slot.get(), row_keys_slot.get(), name_slot.get(), cur->optional ? 1 : 0,
            index_name_slot.get(), indices_slot.get());
        tuples_slot.set(interp_ptr_item(tuples));
    }

    Array* tuples = (Array*)(uintptr_t)tuples_slot.get().item;
    if (for_node->group) {
        // Join rows are tuple Elements, so grouping must collect the already
        // joined row carrier before switching to the `into` scope.  Running
        // the ordinary one-source collector here would re-evaluate the first
        // source and lose the join cardinality (S10.1.3).
        Scratch group_rows_slot(f);
        Scratch group_keys_slot(f);
        group_rows_slot.set(interp_ptr_item(array_plain()));
        group_keys_slot.set(interp_ptr_item(array_plain()));
        int64_t source_count = tuples ? tuples->length : 0;
        for (int64_t i = 0; i < source_count; i++) {
            tuples = (Array*)(uintptr_t)tuples_slot.get().item;
            if (!tuples || i >= tuples->length) break;
            Scratch tuple_slot(f);
            tuple_slot.set(tuples->items[i]);
            interp_join_bind_tuple(fc, first, NULL, tuple_slot.get());
            bool keep = true;
            if (!interp_for_apply_row_clauses(fc, &keep)) break;
            if (keep) {
                Array* rows = (Array*)(uintptr_t)group_rows_slot.get().item;
                if (rows) array_push(rows, tuple_slot.get());
                if (!interp_for_append_group_key(fc, group_keys_slot.home())) break;
            }
            if (i + 1 < source_count) interp_note_backedge(f);
        }
        if (interp_frame_pending(f)) return ItemNull;

        Scratch aliases_slot(f);
        aliases_slot.set(interp_ptr_item(array_plain()));
        for (AstGroupKey* spec = for_node->group->keys; spec;
                spec = (AstGroupKey*)((AstNode*)spec)->next) {
            Scratch alias_slot(f);
            alias_slot.set((Item){.item = s2it(heap_create_name(
                spec->alias ? spec->alias->chars : ""))});
            Array* aliases = (Array*)(uintptr_t)aliases_slot.get().item;
            if (aliases) array_push(aliases, alias_slot.get());
        }
        Scratch groups_slot(f);
        groups_slot.set(interp_ptr_item(fn_group_by_keys_items(
            group_rows_slot.get(), group_keys_slot.get(), aliases_slot.get())));
        Array* groups = (Array*)(uintptr_t)groups_slot.get().item;
        int64_t group_count = groups ? groups->length : 0;
        bool grouped_ok = true;
        for (int64_t i = 0; i < group_count && grouped_ok; i++) {
            groups = (Array*)(uintptr_t)groups_slot.get().item;
            if (!groups || i >= groups->length) break;
            Scratch group_slot(f);
            group_slot.set(groups->items[i]);
            interp_write_binding(f, for_node->group->entry, group_slot.get());
            grouped_ok = interp_for_emit_value(fc);
            if (f->signal == EvalSignal::BROKE) {
                interp_clear_loop_signal(f);
                break;
            }
            if (f->signal == EvalSignal::CONTINUED) {
                interp_clear_loop_signal(f);
                grouped_ok = true;
            }
            if (grouped_ok && i + 1 < group_count) interp_note_backedge(f);
        }
        if (!result_demanded) return ItemNull;
        return interp_for_finalize_output(f, for_node,
            out_slot.home(), key_slot.home());
    }

    int64_t tuple_count = tuples ? tuples->length : 0;
    bool ok = true;
    for (int64_t i = 0; i < tuple_count && ok; i++) {
        tuples = (Array*)(uintptr_t)tuples_slot.get().item;
        if (!tuples || i >= tuples->length) break;
        Scratch tuple_slot(f);
        tuple_slot.set(tuples->items[i]);
        interp_join_bind_tuple(fc, first, NULL, tuple_slot.get());
        ok = interp_for_emit(fc);
        if (f->signal == EvalSignal::BROKE) { interp_clear_loop_signal(f); break; }
        if (f->signal == EvalSignal::CONTINUED) { interp_clear_loop_signal(f); ok = true; }
        if (ok && i + 1 < tuple_count) interp_note_backedge(f);
    }
    if (!result_demanded) return ItemNull;
    return interp_for_finalize_output(f, for_node, out_slot.home(), key_slot.home());
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
    Scratch key_slot(f);
    if (result_demanded && for_node->order) {
        key_slot.set(interp_ptr_item(array_plain()));
        fc.keys = key_slot.home();
    }

    for (AstLoopNode* join = loop; join; join = (AstLoopNode*)((AstNode*)join)->next) {
        if (join->on || join->join_keys || join->optional) {
            return interp_eval_join_for(&fc, loop, result_demanded, out_slot, key_slot);
        }
    }

    if (for_node->group) {
        return interp_eval_grouped_for(&fc, loop, result_demanded,
            out_slot, key_slot);
    }

    interp_for_level(&fc, loop);
    if (!result_demanded) return ItemNull;

    return interp_for_finalize_output(f, for_node, out_slot.home(), key_slot.home());
}

// Mirrors transpile_element: allocate the element, fill its attributes from a
// rooted span (same order as the map literal — values first, container after),
// then push content through list_push_spread and finalize with list_end. The
// element pointer itself is the value; list_end only closes its content frame.
static Item eval_element(InterpFrame* f, AstElementNode* node) {
    TypeElmt* type = (TypeElmt*)node->type;
    if (!type) return ItemError;

    Scratch acc(f);
    if (node->has_computed_key) {
        acc.set(interp_ptr_item(elmt_literal_begin(type)));
        if (get_type_id(acc.get()) == LMD_TYPE_ERROR || !acc.get().item) {
            interp_signal(f, EvalSignal::RETURNED, ItemError);
            return ItemError;
        }
        for (AstNode* item = node->item; item; item = item->next) {
            AstNamedNode* named = item->node_type == AST_NODE_KEY_EXPR
                ? (AstNamedNode*)item : NULL;
            if (!named) return ItemError;
            if (named->is_spread) {
                Scratch source(f);
                source.set(named->as ? eval_expr(f, named->as) : ItemNull);
                if (interp_frame_pending(f)) return ItemNull;
                Item result = map_literal_spread(acc.get(), source.get());
                if (get_type_id(result) == LMD_TYPE_ERROR) {
                    interp_signal(f, EvalSignal::RETURNED, result);
                    return result;
                }
                continue;
            }
            Scratch key(f);
            key.set(named->key ? eval_expr(f, named->key) : (Item){.item = s2it(
                heap_strcpy(named->name ? named->name->chars : "",
                    named->name ? named->name->len : 0))});
            if (interp_frame_pending(f)) return ItemNull;
            Scratch value(f);
            value.set(named->as ? eval_expr(f, named->as) : ItemNull);
            if (interp_frame_pending(f)) return ItemNull;
            if (named->as && ast_expr_insertion_needs_capture(named->as)) {
                cow_capture_value(value.get());
            }
            if (get_type_id(map_literal_put(acc.get(), key.get(), value.get())) ==
                    LMD_TYPE_ERROR) {
                interp_signal(f, EvalSignal::RETURNED, ItemError);
                return ItemError;
            }
        }
    } else {
        int attr_count = 0;
        for (AstNode* a = node->item; a; a = a->next) attr_count++;
        RootSpan attrs((size_t)(attr_count > 0 ? attr_count : 1));
        uint64_t* attr_words = attrs.words();
        int ai = 0;
        for (AstNode* a = node->item; a; a = a->next) {
            AstNode* value_node = a->node_type == AST_NODE_KEY_EXPR
                ? ((AstNamedNode*)a)->as : a;
            Item value = value_node ? eval_expr(f, value_node) : ItemNull;
            // S9.3.1: a named attribute value is captured into the element.
            if (value_node && ast_expr_insertion_needs_capture(value_node)) {
                cow_capture_value(value);
            }
            attr_words[ai++] = value.item;
            if (interp_frame_pending(f)) return ItemNull;
        }

        Element* fresh = elmt_with_tl(type->type_index, f->module->type_list);
        if (!fresh) return ItemError;
        acc.set(interp_ptr_item(fresh));
        if (attr_count > 0) {
            elmt_fill_items((Element*)(uintptr_t)acc.get().item,
                (const Item*)(void*)attr_words, ai);
        }
    }

    if (node->content) {
        // AstElementNode::content is the list wrapper, not its first child.
        // Evaluating that wrapper once collapses a multi-child element to its
        // final value before COW ever sees it, so iterate its item chain just
        // as MIR's element lowering does.
        AstListNode* content_list = (AstListNode*)node->content;
        for (AstNode* c = content_list ? content_list->item : NULL; c; c = c->next) {
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
                    item->node_type == AST_NODE_LOOP ||
                    ast_for_discards_result(item)) {
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
         last_value->node_type != AST_NODE_FOR_EXPR);

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
            } else if (ast_for_discards_result(item)) {
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
        if (decl->node_type == AST_NODE_VARIABLE_DECLARATOR) {
            AstDeclaratorNode* named = (AstDeclaratorNode*)decl;
            Item bound = eval_expr(f, named->init);
            if (interp_frame_pending(f)) return bound;
            if (!interp_bind_declared_value(f, named, bound)) {
                return interp_signal_payload(f);
            }
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
            : interp_item_at(source.get(), i);
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
            if (decl->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
            AstDeclaratorNode* named = (AstDeclaratorNode*)decl;
            Item value = eval_expr(f, named->init);
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
            AstNode* init = ast_unwrap_primary(named->init);
            if (init && init->node_type == AST_NODE_IDENT) {
                NameEntry* src = ((AstIdentNode*)init)->entry;
                TypeId init_tid = named->init->type
                    ? named->init->type->type_id : LMD_TYPE_ANY;
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
                        ast_expr_may_return_container(named->init, init_tid, var_tid))) {
                    // cow_bind_var may detach a copy, so it is a safepoint: the
                    // operand has to be reachable from a frame slot, not a C++
                    // local, or a collection during the clone frees it.
                    Scratch alias_slot(f);
                    alias_slot.set(value);
                    value = cow_bind_var(alias_slot.get());
                }
            }
            if (!interp_bind_declared_value(f, named, value)) return;
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
        // Type/object/pattern definitions publish through their expression or
        // compile-time carriers; no slab value is needed for this declaration
        // arm. View/state declarations remain outside the T0 plan boundary.
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

typedef struct InterpFastIntValue {
    int64_t value;
    bool boolean;
} InterpFastIntValue;

typedef struct InterpFastIntCache {
    const AstNode* nodes[32];
    int64_t values[32];
    int count;
} InterpFastIntCache;

static bool interp_fast_int_expr_shape(AstNode* node);
static bool interp_fast_int_stmt_shape(AstNode* node);

static bool interp_fast_int_expr_shape(AstNode* node) {
    if (!node) return false;
    switch (node->node_type) {
    case AST_NODE_PRIMARY:
        return ((AstPrimaryNode*)node)->expr
            ? interp_fast_int_expr_shape(((AstPrimaryNode*)node)->expr)
            : node->type && node->type->type_id == LMD_TYPE_INT;
    case AST_NODE_IDENT:
        return ((AstIdentNode*)node)->entry && node->type &&
            node->type->type_id == LMD_TYPE_INT;
    case AST_NODE_UNARY:
        return ((AstUnaryNode*)node)->op == OPERATOR_NEG &&
            interp_fast_int_expr_shape(((AstUnaryNode*)node)->operand);
    case AST_NODE_BINARY: {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        switch (binary->op) {
        case OPERATOR_ADD: case OPERATOR_SUB: case OPERATOR_MUL:
        case OPERATOR_MOD: case OPERATOR_EQ: case OPERATOR_NE:
        case OPERATOR_LT: case OPERATOR_LE: case OPERATOR_GT: case OPERATOR_GE:
            return interp_fast_int_expr_shape(binary->left) &&
                interp_fast_int_expr_shape(binary->right);
        default:
            return false;
        }
    }
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)node;
        AstNode* callee = ast_unwrap_primary(call->function);
        if (!callee || callee->node_type != AST_NODE_SYS_FUNC ||
                !((AstSysFuncNode*)callee)->fn_info ||
                ((AstSysFuncNode*)callee)->fn_info->fn != SYSFUNC_SHR) return false;
        AstNode* first = call->argument;
        AstNode* second = first ? first->next : NULL;
        return first && second && !second->next &&
            interp_fast_int_expr_shape(first) &&
            interp_fast_int_expr_shape(second);
    }
    default:
        return false;
    }
}

static bool interp_fast_int_stmt_shape(AstNode* node) {
    if (!node) return false;
    if (node->node_type == AST_NODE_CONTENT) {
        for (AstNode* item = ((AstListNode*)node)->item; item; item = item->next) {
            if (!interp_fast_int_stmt_shape(item)) return false;
        }
        return true;
    }
    if (node->node_type == AST_NODE_ASSIGN_STAM) {
        AstAssignStamNode* assign = (AstAssignStamNode*)node;
        NameEntry* target = assign->target_entry;
        if (!target && assign->left && assign->left->node_type == AST_NODE_IDENT) {
            target = ((AstIdentNode*)assign->left)->entry;
        }
        return target && target->declared_type &&
            target->declared_type->type_id == LMD_TYPE_INT &&
            interp_fast_int_expr_shape(assign->value);
    }
    if (node->node_type == AST_NODE_IF_EXPR) {
        AstIfNode* branch = (AstIfNode*)node;
        return interp_fast_int_expr_shape(branch->cond) &&
            interp_fast_int_stmt_shape(branch->then) &&
            (!branch->otherwise || interp_fast_int_stmt_shape(branch->otherwise));
    }
    return false;
}

static void interp_fast_int_cache_node(InterpFastIntCache* cache, AstNode* node,
        InterpFastIntCache* owner) {
    (void)owner;
    if (!cache || !node || cache->count >= 32) return;
    if (node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* primary = (AstPrimaryNode*)node;
        if (primary->expr) {
            interp_fast_int_cache_node(cache, primary->expr, owner);
        } else if (node->type && node->type->type_id == LMD_TYPE_INT) {
            for (int i = 0; i < cache->count; i++) {
                if (cache->nodes[i] == node) return;
            }
            cache->nodes[cache->count] = node;
            cache->values[cache->count] = 0;
            cache->count++;
        }
        return;
    }
    if (node->node_type == AST_NODE_BINARY) {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        interp_fast_int_cache_node(cache, binary->left, owner);
        interp_fast_int_cache_node(cache, binary->right, owner);
    } else if (node->node_type == AST_NODE_UNARY) {
        interp_fast_int_cache_node(cache, ((AstUnaryNode*)node)->operand, owner);
    } else if (node->node_type == AST_NODE_CALL_EXPR) {
        for (AstNode* arg = ((AstCallNode*)node)->argument; arg; arg = arg->next) {
            interp_fast_int_cache_node(cache, arg, owner);
        }
    } else if (node->node_type == AST_NODE_IF_EXPR) {
        AstIfNode* branch = (AstIfNode*)node;
        interp_fast_int_cache_node(cache, branch->cond, owner);
        interp_fast_int_cache_node(cache, branch->then, owner);
        interp_fast_int_cache_node(cache, branch->otherwise, owner);
    } else if (node->node_type == AST_NODE_CONTENT) {
        for (AstNode* item = ((AstListNode*)node)->item; item; item = item->next) {
            interp_fast_int_cache_node(cache, item, owner);
        }
    } else if (node->node_type == AST_NODE_ASSIGN_STAM) {
        interp_fast_int_cache_node(cache, ((AstAssignStamNode*)node)->value, owner);
    }
}

static bool interp_fast_int_cache_fill(InterpFastIntCache* cache,
        InterpFrame* frame, AstNode* root) {
    if (!cache || !frame) return false;
    interp_fast_int_cache_node(cache, root, cache);
    for (int i = 0; i < cache->count; i++) {
        Item literal = eval_literal(frame, (AstNode*)cache->nodes[i]);
        if (get_type_id(literal) != LMD_TYPE_INT) return false;
        cache->values[i] = lambda_int_item_to_i64(literal);
    }
    return true;
}

static bool interp_fast_int_cache_read(const InterpFastIntCache* cache,
        const AstNode* node, int64_t* value) {
    if (!cache || !node || !value) return false;
    for (int i = 0; i < cache->count; i++) {
        if (cache->nodes[i] == node) {
            *value = cache->values[i];
            return true;
        }
    }
    return false;
}

// The compact int lane is the exact finite integer band exposed by `i2it`.
// Fast arithmetic must leave the specialized loop when a result would leave
// that band; the ordinary evaluator then applies the language's total numeric
// conversion instead of allowing signed C++ overflow to change the result.
static bool interp_fast_int_apply(Operator op, int64_t left, int64_t right,
        int64_t* result) {
    if (!result) return false;
    __int128 value = 0;
    switch (op) {
    case OPERATOR_ADD: value = (__int128)left + (__int128)right; break;
    case OPERATOR_SUB: value = (__int128)left - (__int128)right; break;
    case OPERATOR_MUL: value = (__int128)left * (__int128)right; break;
    case OPERATOR_MOD:
        if (right == 0) return false;
        value = (__int128)left % (__int128)right;
        break;
    default: return false;
    }
    if (value < (__int128)INT53_MIN || value > (__int128)INT53_MAX) return false;
    *result = (int64_t)value;
    return true;
}

static bool interp_fast_int_read_binding(InterpFrame* frame, NameEntry* entry,
        int64_t* value) {
    if (!frame || !entry || !value ||
            entry->binding_storage != BINDING_STORAGE_REGISTER ||
            entry->slot < 0 || (uint32_t)entry->slot >= frame->scratch_base ||
            (frame->st && frame->st->view_bindings) ||
            (frame->fn && frame->fn->captures)) return false;
    Item item = (Item){.item = frame->slots[entry->slot]};
    if (get_type_id(item) != LMD_TYPE_INT) return false;
    *value = lambda_int_item_to_i64(item);
    return true;
}

static bool interp_fast_int_eval(InterpFrame* frame, AstNode* node,
        const InterpFastIntCache* cache, InterpFastIntValue* out) {
    if (!frame || !node || !out) return false;
    switch (node->node_type) {
    case AST_NODE_PRIMARY: {
        AstPrimaryNode* primary = (AstPrimaryNode*)node;
        if (primary->expr) return interp_fast_int_eval(frame, primary->expr, cache, out);
        int64_t value = 0;
        if (!interp_fast_int_cache_read(cache, node, &value)) return false;
        out->value = value;
        out->boolean = false;
        return true;
    }
    case AST_NODE_IDENT: {
        NameEntry* entry = ((AstIdentNode*)node)->entry;
        if (!interp_fast_int_read_binding(frame, entry, &out->value)) return false;
        out->boolean = false;
        return true;
    }
    case AST_NODE_UNARY: {
        InterpFastIntValue operand;
        if (!interp_fast_int_eval(frame, ((AstUnaryNode*)node)->operand, cache, &operand) ||
                operand.boolean || !interp_fast_int_apply(OPERATOR_SUB, 0,
                    operand.value, &out->value)) return false;
        out->boolean = false;
        return true;
    }
    case AST_NODE_BINARY: {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        InterpFastIntValue left, right;
        if (!interp_fast_int_eval(frame, binary->left, cache, &left) ||
                !interp_fast_int_eval(frame, binary->right, cache, &right) ||
                left.boolean || right.boolean) return false;
        switch (binary->op) {
        case OPERATOR_ADD: case OPERATOR_SUB: case OPERATOR_MUL: case OPERATOR_MOD:
            if (!interp_fast_int_apply(binary->op, left.value, right.value,
                    &out->value)) return false;
            out->boolean = false;
            return true;
        case OPERATOR_EQ: out->value = left.value == right.value; out->boolean = true; return true;
        case OPERATOR_NE: out->value = left.value != right.value; out->boolean = true; return true;
        case OPERATOR_LT: out->value = left.value < right.value; out->boolean = true; return true;
        case OPERATOR_LE: out->value = left.value <= right.value; out->boolean = true; return true;
        case OPERATOR_GT: out->value = left.value > right.value; out->boolean = true; return true;
        case OPERATOR_GE: out->value = left.value >= right.value; out->boolean = true; return true;
        default: return false;
        }
    }
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)node;
        AstNode* callee = ast_unwrap_primary(call->function);
        AstNode* first = call->argument;
        AstNode* second = first ? first->next : NULL;
        InterpFastIntValue left, shift;
        if (!callee || callee->node_type != AST_NODE_SYS_FUNC || !first || !second ||
                second->next || ((AstSysFuncNode*)callee)->fn_info->fn != SYSFUNC_SHR ||
                !interp_fast_int_eval(frame, first, cache, &left) ||
                !interp_fast_int_eval(frame, second, cache, &shift) ||
                left.boolean || shift.boolean || shift.value < 0 || shift.value >= 64) return false;
        out->value = left.value >> shift.value;
        out->boolean = false;
        return true;
    }
    default:
        return false;
    }
}

static bool interp_fast_int_exec(InterpFrame* frame, AstNode* node,
        const InterpFastIntCache* cache) {
    if (!frame || !node) return false;
    if (node->node_type == AST_NODE_CONTENT) {
        for (AstNode* item = ((AstListNode*)node)->item; item; item = item->next) {
            if (!interp_fast_int_exec(frame, item, cache)) return false;
        }
        return true;
    }
    if (node->node_type == AST_NODE_ASSIGN_STAM) {
        AstAssignStamNode* assign = (AstAssignStamNode*)node;
        NameEntry* target = assign->target_entry;
        if (!target && assign->left && assign->left->node_type == AST_NODE_IDENT) {
            target = ((AstIdentNode*)assign->left)->entry;
        }
        InterpFastIntValue value;
        if (!target || !interp_fast_int_eval(frame, assign->value, cache, &value) ||
                value.boolean) return false;
        if (target->binding_storage != BINDING_STORAGE_REGISTER ||
                target->slot < 0 || (uint32_t)target->slot >= frame->scratch_base ||
                (frame->st && frame->st->view_bindings) ||
                (frame->fn && frame->fn->captures)) return false;
        frame->slots[target->slot] = i2it(value.value);
        return true;
    }
    if (node->node_type == AST_NODE_IF_EXPR) {
        AstIfNode* branch = (AstIfNode*)node;
        InterpFastIntValue condition;
        if (!interp_fast_int_eval(frame, branch->cond, cache, &condition)) return false;
        bool truth = condition.boolean ? condition.value != 0 : condition.value != 0;
        return truth ? interp_fast_int_exec(frame, branch->then, cache) :
            (!branch->otherwise || interp_fast_int_exec(frame, branch->otherwise, cache));
    }
    return false;
}

typedef struct InterpFastIntOperand {
    NameEntry* entry;
    int64_t constant;
    bool is_constant;
} InterpFastIntOperand;

typedef struct InterpFastIntLinearOp {
    NameEntry* target;
    InterpFastIntOperand left;
    InterpFastIntOperand right;
    Operator op;
} InterpFastIntLinearOp;

static bool interp_fast_int_linear_operand(AstNode* node,
        const InterpFastIntCache* cache, InterpFastIntOperand* out) {
    if (!node || !out) return false;
    // Keep literal primaries intact for the cache lookup; unwrapping first
    // would turn the constant into its leaf and make an otherwise linear
    // accumulator loop fall back to the boxed evaluator.
    if (node->node_type == AST_NODE_PRIMARY && !((AstPrimaryNode*)node)->expr &&
            node->type && node->type->type_id == LMD_TYPE_INT) {
        out->entry = NULL;
        out->is_constant = true;
        return interp_fast_int_cache_read(cache, node, &out->constant);
    }
    node = ast_unwrap_primary(node);
    if (!node) return false;
    if (node->node_type == AST_NODE_IDENT) {
        out->entry = ((AstIdentNode*)node)->entry;
        out->is_constant = false;
        return out->entry && out->entry->declared_type &&
            out->entry->declared_type->type_id == LMD_TYPE_INT;
    }
    return false;
}

static bool interp_fast_int_linear_binding_ok(InterpFrame* frame,
        NameEntry* entry) {
    return frame && entry && entry->binding_storage == BINDING_STORAGE_REGISTER &&
        entry->slot >= 0 && (uint32_t)entry->slot < frame->scratch_base &&
        (!frame->st || !frame->st->view_bindings) &&
        (!frame->fn || !frame->fn->captures);
}

static bool interp_fast_int_linear_read(InterpFrame* frame,
        const InterpFastIntOperand* operand, int64_t* value) {
    if (!frame || !operand || !value) return false;
    if (operand->is_constant) {
        *value = operand->constant;
        return true;
    }
    if (!interp_fast_int_linear_binding_ok(frame, operand->entry)) return false;
    Item item = (Item){.item = frame->slots[operand->entry->slot]};
    if (get_type_id(item) != LMD_TYPE_INT) return false;
    *value = lambda_int_item_to_i64(item);
    return true;
}

static bool interp_fast_int_linear_operand_same(const InterpFastIntOperand* left,
        const InterpFastIntOperand* right) {
    if (!left || !right || left->is_constant != right->is_constant) return false;
    return left->is_constant ? left->constant == right->constant :
        left->entry == right->entry;
}

static bool interp_fast_int_linear_while(InterpFrame* frame, AstWhileNode* loop,
        const InterpFastIntCache* cache, Item* result) {
    if (!frame || !loop || !cache || !result ||
            loop->cond->node_type != AST_NODE_BINARY ||
            loop->body->node_type != AST_NODE_CONTENT) return false;
    AstBinaryNode* condition = (AstBinaryNode*)loop->cond;
    switch (condition->op) {
    case OPERATOR_EQ: case OPERATOR_NE: case OPERATOR_LT: case OPERATOR_LE:
    case OPERATOR_GT: case OPERATOR_GE: break;
    default: return false;
    }
    InterpFastIntOperand cond_left, cond_right;
    if (!interp_fast_int_linear_operand(condition->left, cache, &cond_left) ||
            !interp_fast_int_linear_operand(condition->right, cache, &cond_right)) return false;

    InterpFastIntLinearOp ops[8] = {};
    int op_count = 0;
    for (AstNode* item = ((AstListNode*)loop->body)->item; item; item = item->next) {
        if (item->node_type != AST_NODE_ASSIGN_STAM || op_count >= 8) return false;
        AstAssignStamNode* assign = (AstAssignStamNode*)item;
        NameEntry* target = assign->target_entry;
        if (!target && assign->left && assign->left->node_type == AST_NODE_IDENT) {
            target = ((AstIdentNode*)assign->left)->entry;
        }
        AstNode* value_node = ast_unwrap_primary(assign->value);
        if (!target || !value_node || value_node->node_type != AST_NODE_BINARY ||
                !interp_fast_int_linear_binding_ok(frame, target)) return false;
        AstBinaryNode* value = (AstBinaryNode*)value_node;
        switch (value->op) {
        case OPERATOR_ADD: case OPERATOR_SUB: case OPERATOR_MUL: case OPERATOR_MOD: break;
        default: return false;
        }
        if (!interp_fast_int_linear_operand(value->left, cache, &ops[op_count].left) ||
                !interp_fast_int_linear_operand(value->right, cache, &ops[op_count].right)) return false;
        ops[op_count].target = target;
        ops[op_count].op = value->op;
        op_count++;
    }
    if (op_count == 0) return false;

    // A positive repeated-subtraction loop has a closed form. This is still a
    // structural integer-loop rule: it admits only `x >= y` with `x = x - y`
    // and an optional independent `q = q + constant`, preserving ordinary
    // evaluation for every other loop shape (S4.4.2).
    if (condition->op == OPERATOR_GE && !cond_left.is_constant &&
            cond_left.entry &&
            (!cond_right.is_constant || cond_right.entry != cond_left.entry)) {
        int subtract_index = -1;
        int accumulator_index = -1;
        for (int i = 0; i < op_count; i++) {
            InterpFastIntLinearOp* op = &ops[i];
            if (op->target == cond_left.entry && op->op == OPERATOR_SUB &&
                    !op->left.is_constant && op->left.entry == cond_left.entry &&
                    interp_fast_int_linear_operand_same(&op->right, &cond_right)) {
                subtract_index = i;
                continue;
            }
            if (op->op == OPERATOR_ADD && !op->left.is_constant &&
                    op->left.entry == op->target && op->right.is_constant &&
                    op->target != cond_left.entry &&
                    (!cond_right.is_constant || op->target != cond_right.entry)) {
                if (accumulator_index >= 0) {
                    accumulator_index = -2;
                    break;
                }
                accumulator_index = i;
                continue;
            }
            subtract_index = -2;
            break;
        }
        if (subtract_index >= 0 && accumulator_index >= -1) {
            int64_t left = 0, right = 0;
            if (!interp_fast_int_linear_read(frame, &cond_left, &left) ||
                    !interp_fast_int_linear_read(frame, &cond_right, &right) ||
                    right <= 0) {
                return false;
            }
            if (left >= right) {
                __int128 iterations = (__int128)left / (__int128)right;
                __int128 remainder = (__int128)left - iterations * (__int128)right;
                if (remainder < (__int128)INT53_MIN || remainder > (__int128)INT53_MAX) {
                    return false;
                }
                if (accumulator_index >= 0) {
                    InterpFastIntLinearOp* accumulator = &ops[accumulator_index];
                    int64_t current = 0;
                    InterpFastIntOperand accumulator_operand = {
                        accumulator->target, 0, false};
                    if (!interp_fast_int_linear_read(frame, &accumulator_operand,
                            &current)) {
                        return false;
                    }
                    __int128 next = (__int128)current + iterations *
                        (__int128)accumulator->right.constant;
                    if (next < (__int128)INT53_MIN || next > (__int128)INT53_MAX) return false;
                    frame->slots[accumulator->target->slot] = i2it((int64_t)next);
                }
                frame->slots[cond_left.entry->slot] = i2it((int64_t)remainder);
                *result = ItemNull;
                return true;
            }
        }
    }

    for (;;) {
        int64_t left = 0, right = 0;
        if (!interp_fast_int_linear_read(frame, &cond_left, &left) ||
                !interp_fast_int_linear_read(frame, &cond_right, &right)) return false;
        bool truth = condition->op == OPERATOR_EQ ? left == right :
            condition->op == OPERATOR_NE ? left != right :
            condition->op == OPERATOR_LT ? left < right :
            condition->op == OPERATOR_LE ? left <= right :
            condition->op == OPERATOR_GT ? left > right : left >= right;
        if (!truth) break;

        int64_t next_values[8];
        for (int i = 0; i < op_count; i++) {
            int64_t a = 0, b = 0;
            if (!interp_fast_int_linear_read(frame, &ops[i].left, &a) ||
                    !interp_fast_int_linear_read(frame, &ops[i].right, &b)) return false;
            if (!interp_fast_int_apply(ops[i].op, a, b, &next_values[i])) return false;
        }
        for (int i = 0; i < op_count; i++) {
            frame->slots[ops[i].target->slot] = i2it(next_values[i]);
        }
    }
    *result = ItemNull;
    return true;
}

static bool interp_fast_int_while(InterpFrame* frame, AstWhileNode* loop,
        Item* result) {
    if (!frame || !loop || !result ||
            !interp_fast_int_expr_shape(loop->cond) ||
            !interp_fast_int_stmt_shape(loop->body)) return false;
    InterpFastIntCache cache = {};
    if (!interp_fast_int_cache_fill(&cache, frame, loop->cond) ||
            !interp_fast_int_cache_fill(&cache, frame, loop->body)) return false;
    if (interp_fast_int_linear_while(frame, loop, &cache, result)) return true;
    for (;;) {
        InterpFastIntValue condition;
        if (!interp_fast_int_eval(frame, loop->cond, &cache, &condition)) return false;
        bool truth = condition.boolean ? condition.value != 0 : condition.value != 0;
        if (!truth) break;
        if (!interp_fast_int_exec(frame, loop->body, &cache)) return false;
    }
    *result = ItemNull;
    return true;
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
        if (decl && decl->node_type == AST_NODE_VARIABLE_DECLARATOR &&
                ((AstDeclaratorNode*)decl)->is_type_definition) {
            Type* declared = decl->type;
            TypeId tid = LMD_TYPE_ANY;
            TypeType* singleton = lambda_type_node_singleton(declared, &tid);
            if (singleton) return interp_ptr_item(singleton);
            return interp_ptr_item(declared ? declared : base_type(tid));
        }
        if (decl && decl->node_type == AST_NODE_OBJECT_TYPE) {
            AstObjectTypeNode* object = (AstObjectTypeNode*)decl;
            TypeType* type = object->type && object->type->type_id == LMD_TYPE_TYPE
                ? (TypeType*)object->type : NULL;
            TypeObject* object_type = type && type->type_id == LMD_TYPE_TYPE
                ? (TypeObject*)type->type : NULL;
            return interp_ptr_item(object_type
                ? const_type_with_tl(object_type->type_index, f->module->type_list)
                : &LIT_TYPE_ERROR);
        }
        if (decl && (decl->node_type == AST_NODE_STRING_PATTERN ||
                decl->node_type == AST_NODE_SYMBOL_PATTERN)) {
            TypePattern* pattern = (TypePattern*)decl->type;
            // Direct multi-declaration reductions can leave a later pattern
            // unindexed even though its TypePattern AST is valid. Materialize
            // that deferred identity at first use so T0 and MIR share the
            // same module-local type-list carrier.
            if (pattern && pattern->pattern_index < 0 &&
                    !compile_runtime_pattern(f->module->pool, f->module->type_list,
                        pattern, ((AstPatternDefNode*)decl)->as,
                        ((AstPatternDefNode*)decl)->is_symbol)) {
                return ItemError;
            }
            // Pattern definitions never own a slab binding: the shared
            // prepass registers their TypePattern in this module's type list,
            // which is the same const-pattern carrier MIR materializes.
            return interp_ptr_item(pattern && pattern->pattern_index >= 0
                ? const_pattern_with_tl(pattern->pattern_index, f->module->type_list)
                : NULL);
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
    case AST_NODE_PIPE_FILE_STAM: {
        // legacy pipe-to-file AST nodes are statement-shaped, but their runtime
        // contract is still the same boxed source/target call as MIR lowering;
        // evaluate the source first so a target allocation cannot observe a
        // stale unrooted source (D5.3.3).
        AstBinaryNode* pipe = (AstBinaryNode*)node;
        Scratch source(f);
        source.set(eval_expr(f, pipe->left));
        if (interp_frame_pending(f)) return source.get();
        Scratch target(f);
        target.set(eval_expr(f, pipe->right));
        if (interp_frame_pending(f)) return target.get();
        return pipe->op == OPERATOR_PIPE_APPEND
            ? pn_output_append_mir(source.get(), target.get())
            : pn_output2_mir(source.get(), target.get());
    }
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
        // `last` indexes CONTENT: an IntKey subscript reaches children only.
        return int2it_i64(fn_seq_count((Item){.item = *f->st->last_index_item}) - 1);
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
        if (field->field && field->field->next) {
            int64_t indices[AST_COW_PATH_MAX] = {};
            int ndim = 0;
            if (!interp_eval_ndim_indices(f, field->field, indices, &ndim)) {
                return interp_frame_pending(f) ? ItemNull : ItemError;
            }
            if (get_type_id(obj.get()) != LMD_TYPE_ARRAY_NUM) {
                log_error("interp: planned N-D index target is not an ArrayNum");
                return ItemError;
            }
            return array_num_at_nd(obj.get().array_num, ndim, indices);
        }
        Item index_value = eval_expr(f, field->field);
        Scratch index_slot(f);
        index_slot.set(index_value);
        int64_t plain_index = 0;
        if (lambda_item_to_int64_exact(index_slot.get(), &plain_index) &&
                get_type_id(obj.get()) == LMD_TYPE_ARRAY) {
            return interp_item_at(obj.get(), plain_index);
        }
        return fn_index(obj.get(), index_slot.get());
    }
    case AST_NODE_ARRAY:
        return eval_array(f, (AstArrayNode*)node);
    case AST_NODE_MAP:
        return eval_map(f, (AstMapNode*)node);
    case AST_NODE_OBJECT_LITERAL:
        return eval_object_literal(f, (AstObjectLiteralNode*)node);
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
    case AST_NODE_VARIABLE_DECLARATOR:
        return eval_expr(f, ((AstDeclaratorNode*)node)->init);
    case AST_NODE_KEY_EXPR:
        return eval_expr(f, ((AstNamedNode*)node)->as);
    case AST_NODE_NAMED_ARG:
        // MIR passes a pure system call's named operand as its value in source
        // order. Evaluating the wrapper as a standalone expression returned
        // ItemError and broke the same positional system ABI after a pipe.
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
        value = interp_coerce_declared_binding(f, value, target->declared_type,
            "declared assignment binding");
        if (!fresh_rhs_error && item_is_error(value) && target->declared_type &&
                !lambda_type_accepts_error(target->declared_type)) {
            // A fresh checked-assignment failure returns before publishing the
            // rejected value, matching MIR's emit_return_if_item_error edge.
            interp_signal(f, EvalSignal::RETURNED, value);
            return value;
        }
        interp_write_binding(f, target, value);
        AstNode* body = f->fn ? ast_unwrap_primary(f->fn->body) : NULL;
        // A direct assignment body is the function result in MIR. Other
        // assignment statements complete as null so a handler's `x = ^`
        // consumes its caught error instead of re-propagating it.
        return fresh_rhs_error || body == node ? value : ItemNull;
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
            // An untyped COW path validates no enclosing contract; a nested
            // typed-map write must validate its rebuilt root before publish.
            //
            // NM-O8: a `var` parameter's root was detached by the caller, and a
            // plain `pn` parameter writes through to the caller under the
            // current pn ABI -- the same rule the FLAT store above applies via
            // the in-place setter for `var` roots. Without it the nested store
            // detached the callee's own root and published the replacement
            // into the callee's binding, so `b.xs[0] = v` was visible inside
            // the procedure and lost at the caller while `b.cur = v` was not.
            //
            // The TYPED arm stays transactional even for those roots: its
            // publish runs `lambda_type_check` over the whole candidate, which
            // CONVERTS (a 3.5 admitted into an int field becomes 2). An
            // in-place write has no candidate to convert, so applying this
            // there silently skipped the coercion —
            // proc_type_numeric_structural_admission caught it. Typed nested
            // writes through a parameter therefore still need the explicit
            // read-modify-write-back spelling.
            // CW29: only `var` borrows write through; a plain param's write
            // stays local to the callee (S9.1.3).
            bool writes_through_caller = root->is_var_param;
            Item replacement = ast_declared_type_is_map(root->declared_type)
                ? lambda_map_path_set_checked(owner_slot.get(), path_slot.get(),
                    value_slot.get(), root->declared_type,
                    "typed nested map assignment")
                : (writes_through_caller
                    ? cow_path_set_inplace(owner_slot.get(), path_slot.get(),
                        value_slot.get())
                    : cow_path_set(owner_slot.get(), path_slot.get(), value_slot.get()));
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
        // S9.3.1: a named value stored into a container is captured, so later
        // writes through the source binding detach instead of aliasing the slot.
        // The setters below are shared with raw/host paths and carry no policy.
        if (ast_expr_insertion_needs_capture(ca->value)) {
            cow_capture_value(value_slot.get());
        }

        if (ca->key && ca->key->next) {
            int64_t indices[AST_COW_PATH_MAX] = {};
            int ndim = 0;
            if (!interp_eval_ndim_indices(f, ca->key, indices, &ndim)) {
                return interp_frame_pending(f) ? ItemNull : ItemError;
            }
            Scratch owner(f);
            owner.set(interp_read_binding(f, root));
            if (get_type_id(owner.get()) != LMD_TYPE_ARRAY_NUM) {
                log_error("interp: planned N-D assignment target is not an ArrayNum");
                return ItemError;
            }
            Item write_result = array_num_set_nd(owner.get().array_num, ndim, indices,
                value_slot.get());
            return item_is_error(write_result) ? write_result : ItemNull;
        }

        Scratch key_slot(f);
        key_slot.set(interp_eval_cow_path_key(f, ca->key,
            node->node_type == AST_NODE_MEMBER_ASSIGN_STAM));
        if (interp_frame_pending(f)) return ItemNull;

        Scratch owner(f);
        owner.set(interp_read_binding(f, root));

        if (ast_is_direct_numeric_mask_assignment(node)) {
            // CW32v2: alias boundaries no longer eagerly detach ArrayNum, so
            // a mask store's owner may be shared. The _cow wrapper prepares
            // (one packed memcpy when shared) and returns the owner, which
            // MUST be republished into the binding.
            Item owner_result = index_assign_cow(owner.get(), key_slot.get(),
                value_slot.get());
            if (item_is_error(owner_result)) return owner_result;
            interp_write_binding(f, root, owner_result);
            return ItemNull;
        }

        // Dispatch on the owner's runtime type, not the syntax: `m["k"] = v`
        // is an INDEX_ASSIGN over a map, and lowering picks the setter by owner
        // type too. Each *_cow entry rejects a mismatched owner itself.
        Item replacement;
        Type* array_element = ast_declared_array_element(root->declared_type);
        if (path.count == 0 && array_element &&
                array_element->type_id != LMD_TYPE_ANY) {
            // SI3v2: the two tiers must word one diagnostic identically, or a
            // golden pins whichever tier happened to run. MIR Direct says
            // "element" here (transpile-mir.cpp), so T0 matches it.
            const char* boundary = "typed array element assignment";
            // SI3v2 again, on the write side: MIR Direct selects the in-place
            // setter for `var` roots
            // (emit_typed_array_store_fallback / writes_through_caller). T0 read
            // only is_var_param, so a plain `pn` parameter's typed write was
            // validated on a DETACHED candidate and republished to the callee's
            // own slot -- visible inside the procedure, lost to the caller.
            replacement = root->is_var_param
                ? lambda_array_set_checked_inplace_item(owner.get(), key_slot.get(),
                    value_slot.get(), root->declared_type, boundary)
                : lambda_array_set_checked_item(owner.get(), key_slot.get(),
                    value_slot.get(), root->declared_type, boundary);
        } else if (ast_declared_type_is_map(root->declared_type)) {
            // a typed map write validates a detached candidate before it is
            // visible. Explicit `var` parameters were detached at the caller
            // boundary, so their private root can use the in-place contract.
            const char* boundary = node->node_type == AST_NODE_MEMBER_ASSIGN_STAM
                ? "typed map member assignment" : "typed map computed assignment";
            // Same rule as the array arm above, and the one that made typed
            // json2 parse to `{jt: 3, sv: ""}` on T0 while MIR returned the
            // object: the parser threads its state through `p: Parser`, a plain
            // pn parameter, so every `p.cur = ...` was published to a detached
            // copy and the caller kept reading the initial value.
            replacement = root->is_var_param
                ? lambda_map_set_checked_inplace(owner.get(), key_slot.get(),
                    value_slot.get(), root->declared_type, boundary)
                : lambda_map_set_checked(owner.get(), key_slot.get(),
                    value_slot.get(), root->declared_type, boundary);
        } else {
            // One checked boundary covers arrays, maps, elements, and VMAPs;
            // invalid key domains must return ItemError instead of selecting a
            // different container face or being coerced to index zero.
            replacement = member_set_cow(owner.get(), key_slot.get(), value_slot.get());
        }
        if (item_is_error(replacement)) return replacement;
        // Publish the (possibly new) owner back at its binding.
        interp_write_binding(f, root, replacement);
        return ItemNull;
    }
    case AST_NODE_LOOP: {
        AstLoopControlNode* loop = (AstLoopControlNode*)node;
        bool test_first = loop->form != LOOP_FORM_DO_WHILE;
        if (test_first) {
            Item fast_result = ItemNull;
            if (interp_fast_int_while(f, loop, &fast_result)) return fast_result;
        }
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
    case AST_NODE_OBJECT_TYPE: {
        AstObjectTypeNode* object = (AstObjectTypeNode*)node;
        TypeType* type = object->type && object->type->type_id == LMD_TYPE_TYPE
            ? (TypeType*)object->type : NULL;
        TypeObject* object_type = type && type->type_id == LMD_TYPE_TYPE
            ? (TypeObject*)type->type : NULL;
        return interp_ptr_item(object_type
            ? const_type_with_tl(object_type->type_index, f->module->type_list)
            : &LIT_TYPE_ERROR);
    }
    case AST_NODE_PATTERN_ISLAND: {
        AstPatternIslandNode* island = (AstPatternIslandNode*)node;
        TypePattern* pattern = (TypePattern*)island->type;
        // An inline island carries its AST until first materialization. Route
        // it through the shared registry so T0 and MIR expose one TypePattern
        // identity to `is`, match, and partial string operations.
        if (!compile_runtime_pattern(f->module->pool, f->module->type_list,
                pattern, island->pattern, island->is_symbol)) {
            log_error("interp: inline pattern has no compiled type index");
            return ItemError;
        }
        island->pattern_index = pattern->pattern_index;
        return interp_ptr_item(const_pattern_with_tl(pattern->pattern_index,
            f->module->type_list));
    }
    case AST_NODE_STRING_PATTERN:
    case AST_NODE_SYMBOL_PATTERN:
        // Definitions are compiled by the shared prepass and bind no runtime
        // slab value, so evaluating a declaration has the same null result as
        // MIR's root pass.
        return ItemNull;
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

// general loop counters belong to the active definition, but promotion is
// deferred until its next entry: only a direct self-tail boundary has the
// entry-equivalent state needed for a safe T1 handoff (D8.1.1v5).
static void interp_note_backedge(InterpFrame* frame) {
    if (!frame || !frame->fn || lambda_tier_selected() != LAMBDA_TIER_AUTO ||
            !frame->fn->analysis) return;
    FnPromotionCell* cell = &frame->fn->analysis->promotion;
    if (cell->state == FN_PROMOTION_INTERP && cell->backedge_count != UINT32_MAX) {
        cell->backedge_count++;
    }
}

// a direct self-tail boundary starts a semantically fresh activation even
// though TCO reuses the frame. It is the only active-frame point whose live
// state is exactly the next entry's argument vector (D8.1.1v5).
static bool interp_tail_handoff_candidate(const InterpFrame* frame) {
    if (!frame || !frame->fn || lambda_tier_selected() != LAMBDA_TIER_AUTO ||
            !frame->fn->analysis) return false;
    const FnPromotionCell* cell = &frame->fn->analysis->promotion;
    if (cell->state != FN_PROMOTION_INTERP) return false;
    uint32_t threshold = interp_jit_threshold();
    return cell->tail_edge_count == UINT32_MAX ||
        cell->tail_edge_count + 1 >= threshold;
}

static bool interp_note_tail_call(InterpFrame* frame) {
    if (!frame || !frame->fn || lambda_tier_selected() != LAMBDA_TIER_AUTO ||
            !frame->fn->analysis) return false;
    FnPromotionCell* cell = &frame->fn->analysis->promotion;
    if (cell->state != FN_PROMOTION_INTERP) return false;
    if (cell->call_count != UINT32_MAX) cell->call_count++;
    if (cell->tail_edge_count != UINT32_MAX) cell->tail_edge_count++;
    return cell->tail_edge_count >= interp_jit_threshold();
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

static bool interp_parameter_rejects_error(const AstNamedNode* parameter,
        Item value) {
    return parameter && parameter->declared_type && item_is_error(value) &&
        !lambda_type_accepts_error(parameter->declared_type);
}

// Keep T0's deferred parameter diagnostics at the call boundary, matching the
// MIR wrapper. The generic declaration label used here before made the same
// rejected argument report different provenance by execution tier.
static void interp_format_parameter_boundary(char* boundary, size_t capacity,
        const AstFuncNode* fn_node, const char* fallback_name, int index) {
    if (!boundary || capacity == 0) return;
    boundary[0] = '\0';
    StrBuf* function_name = strbuf_new_cap(96);
    if (function_name && fn_node) {
        write_fn_name(function_name, (AstFuncNode*)fn_node, NULL);
    }
    const char* display_name = function_name && function_name->str
        ? function_name->str : (fallback_name ? fallback_name : "<anonymous>");
    snprintf(boundary, capacity, "argument %d of %s", index + 1, display_name);
    if (function_name) strbuf_free(function_name);
}

typedef struct InterpBorrowedCall {
    InterpFrame* caller;
    // Legacy write-back channel: resolve the caller binding by entry at
    // return. Kept only for homes plain stores cannot fully update --
    // view-state overlays (their writes must run tmpl_state_set) and
    // object-field entries (no slot of their own).
    NameEntry* entries[LAMBDA_MAX_FUNCTION_ARGS];
    // CW33 (COW §11.10): the address of the caller binding's Item home. The
    // callee prologue prepares THROUGH the home (un-share-at-borrow moves to
    // the one site that always runs), and the final param value is stored
    // straight back through it -- no entry resolution, no caller-side
    // pre-detach gate.
    uint64_t* homes[LAMBDA_MAX_FUNCTION_ARGS];
} InterpBorrowedCall;

// Borrowed `var` arguments are uncommon, but keeping their publication
// buffers on the C stack made deeply recursive interpreted calls consume a
// fixed block per activation. Allocate those escape buffers independently so
// recursion depth is bounded by the language frame/side-root budget rather
// than native stack frame size (D5.3.3).
class InterpBorrowedScratch {
public:
    Item* values;
    TypeId* scalar_types;
    uint64_t* scalar_payloads;

    explicit InterpBorrowedScratch(bool needed)
            : values(NULL), scalar_types(NULL), scalar_payloads(NULL) {
        if (!needed) return;
        values = (Item*)mem_calloc(LAMBDA_MAX_FUNCTION_ARGS, sizeof(Item), MEM_CAT_EVAL);
        scalar_types = (TypeId*)mem_calloc(LAMBDA_MAX_FUNCTION_ARGS,
            sizeof(TypeId), MEM_CAT_EVAL);
        scalar_payloads = (uint64_t*)mem_calloc(LAMBDA_MAX_FUNCTION_ARGS,
            sizeof(uint64_t), MEM_CAT_EVAL);
        if (!values || !scalar_types || !scalar_payloads) {
            mem_free(values);
            mem_free(scalar_types);
            mem_free(scalar_payloads);
            values = NULL;
            scalar_types = NULL;
            scalar_payloads = NULL;
        }
    }

    ~InterpBorrowedScratch() {
        mem_free(values);
        mem_free(scalar_types);
        mem_free(scalar_payloads);
    }

    bool valid(bool needed) const {
        return !needed || (values && scalar_types && scalar_payloads);
    }

    InterpBorrowedScratch(const InterpBorrowedScratch&) = delete;
    InterpBorrowedScratch& operator=(const InterpBorrowedScratch&) = delete;
};

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
    Item method_self = ItemNull;
    if (fn->method) {
        if (!fn->closure_env || fn->closure_field_count != 1) {
            log_error("interp: bound object method has no receiver environment");
            return ItemError;
        }
        method_self = owned_item_slot_read((Item*)fn->closure_env, 1, 0, true);
        if (!lambda_value_nominal(get_type_id(method_self),
                (const void*)(uintptr_t)method_self.item)) {
            log_error("interp: bound object method receiver is invalid");
            return ItemError;
        }
    }
    Item rejected = interp_rejected_parameter_error(signature, args, argc);
    if (item_is_error(rejected)) {
        // An incoming ItemError is not a failed coercion. MIR returns it as
        // the call expression's value, so the caller's `or`/handler can
        // consume it; only a fresh conversion error below exits the caller.
        return rejected;
    }
    if (st->depth == 0) {
        // The interpreter budget is a language/runtime completion, not the
        // native stack-fault carve-out. Return it through this call frame so a
        // caller handler can consume it without a non-local jump.
        st->depth_exhausted = true;
        log_error("interp: recursion depth budget %u exhausted in '%s'",
            st->depth_limit, fn->name ? fn->name : "<anonymous>");
        LambdaError* error = err_create_heap(ERR_STACK_OVERFLOW,
            "Stack overflow", NULL);
        if (error && st->ctx) {
            // recursion-budget errors are ordinary rich completions; publish
            // the diagnostic mirror before the handler can inspect .code/.message
            // (S7.6.1, REH-D3).
            if (st->ctx->last_error && st->ctx->last_error != error) {
                err_free(st->ctx->last_error);
            }
            st->ctx->last_error = error;
        }
        return error ? err2it(error) : ItemError;
    }

    st->depth--;
    Item result = ItemNull;
    TypeId escaped_scalar_type = LMD_TYPE_NULL;
    uint64_t escaped_scalar_payload = 0;
    InterpBorrowedScratch borrowed_scratch(borrowed && borrowed->caller);
    if (!borrowed_scratch.valid(borrowed && borrowed->caller)) {
        st->depth++;
        log_error("interp: could not allocate borrowed argument scratch");
        return ItemError;
    }
    {
        InterpFrameGuard guard(st, fn_node, module, &fn_node->analysis->frame_plan,
            (Item*)fn->closure_env, fn->closure_field_count, fn->method, method_self, fn);
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
            Item source = value;
            if (borrowed && borrowed->homes[index] &&
                    borrowed->entries[index] &&
                    !borrowed->entries[index]->is_var_param) {
                // A `var` parameter re-borrows its caller's prepared root.
                // Every other binding, including a mutated place copy, is an
                // S9.1.2 snapshot and must detach before its `var` callee writes.
                // cow_prepare_write is a byte-test no-op for a unique value.
                Item prepared = cow_prepare_write((Item){.item = *borrowed->homes[index]});
                if (item_is_error(prepared)) {
                    interp_signal(frame, EvalSignal::RETURNED, prepared);
                    if (frame->caller) interp_signal(frame->caller,
                        EvalSignal::RETURNED, prepared);
                    break;
                }
                *borrowed->homes[index] = prepared.item;
                value = prepared;
                source = prepared;
            }
            char boundary[192];
            interp_format_parameter_boundary(boundary, sizeof(boundary), fn_node,
                fn->name, index);
            value = interp_coerce_parameter_binding(frame, value, p, boundary);
            // CW29/S9.1.3 (gated): a plain param the body writes is a snapshot
            // -- one share-mark; its first write detaches a private copy and
            // the caller's value is never touched. Non-mutating callees skip
            // this entirely (cow_param_mutated stays false).
            if (p->entry && p->entry->cow_param_mutated) {
                cow_mark_shared(value);
            }
            frame->slots[index] = value.item;
            if (interp_parameter_rejects_error(p, value)) {
                // Direct MIR enters neither body for a rejected parameter.
                // Only a fresh coercion failure exits the caller; an incoming
                // error must stay at the call-expression boundary for `or`
                // and handler recovery.
                interp_signal(frame, EvalSignal::RETURNED, value);
                if (!item_is_error(source) && frame->caller) interp_signal(frame->caller,
                    EvalSignal::RETURNED, value);
                break;
            }
        }

        if (!interp_frame_pending(frame)) {
            uint64_t method_index = ITEM_NULL;
            InterpContextGuard method_context(frame->st, frame->method_self,
                &method_index, NULL, frame->method_self);
            uint64_t iterations = 0;
            for (;;) {
                result = eval_expr(frame, fn_node->body);
                if (frame->signal == EvalSignal::TAIL_CALL_JIT) {
                    Function* callable = frame->callable_slot
                        ? (Function*)(uintptr_t)*frame->callable_slot : NULL;
                    if (!callable) {
                        log_error("interp-tier: tail handoff lost its callable root");
                        result = ItemError;
                        break;
                    }
                    // the old interpreter frame is intentionally left rooted
                    // until this scope closes, but it must not be an active
                    // caller while the native entry can call back into T0.
                    List tail_args = {.length = params, .items = (Item*)(void*)frame->slots};
                    frame->st->top = frame->caller;
                    st->depth++;
                    return fn_call(callable, &tail_args);
                }
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
        }
        // An explicit `return` unwinds to exactly this boundary and its payload
        // is the call's value; the signal never escapes the activation.
        if (frame->signal == EvalSignal::RETURNED ||
                frame->signal == EvalSignal::ERROR_SKIP) {
            result = interp_signal_payload(frame);
        }
        if (signature && signature->has_explicit_return_contract &&
                signature->return_contract && !item_is_error(result)) {
            // The MIR return firewall checks a dynamic body at every declared
            // return site. T0 must perform the same check before the callee
            // frame closes, otherwise `fn f() int { any_value() }` leaks its
            // runtime value to the caller without the required E201 (S7.7.2).
            Item checked = interp_coerce_declared_binding(frame, result,
                signature->return_contract, "function return");
            result = checked;
        }
        if (borrowed && borrowed->caller) {
            for (int index = 0; index < (int)params; index++) {
                if (!borrowed->entries[index] && !borrowed->homes[index]) continue;
                Item value = (Item){.item = frame->slots[index]};
                TypeId type = get_type_id(value);
                borrowed_scratch.scalar_types[index] = type;
                if (type == LMD_TYPE_INT64) {
                    borrowed_scratch.scalar_payloads[index] = (uint64_t)value.get_int64();
                    borrowed_scratch.values[index] = ItemNull;
                } else if (type == LMD_TYPE_UINT64) {
                    borrowed_scratch.scalar_payloads[index] = value.get_uint64();
                    borrowed_scratch.values[index] = ItemNull;
                } else {
                    borrowed_scratch.values[index] = scalar_storage_read(value, false);
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
            uint64_t* home = borrowed->homes[index];
            if (!entry && !home) continue;
            Item value = borrowed_scratch.scalar_types[index] == LMD_TYPE_INT64
                ? box_int64_value((int64_t)borrowed_scratch.scalar_payloads[index])
                : borrowed_scratch.scalar_types[index] == LMD_TYPE_UINT64
                    ? box_uint64_value(borrowed_scratch.scalar_payloads[index])
                    : borrowed_scratch.values[index];
            // The callee frame owns its param slot, but a `var` argument is
            // the caller's mutable root. Publish after the callee extent
            // closes so a wide scalar cannot retain a dead number-stack home.
            if (home) {
                // CW33: the final param value goes straight through the
                // caller's home -- in the common in-place case this stores
                // back the pointer already there; it also covers rebinds and
                // mid-body detaches with no entry resolution at all.
                *home = value.item;
            } else {
                interp_write_binding(borrowed->caller, entry, value);
            }
        }
    }
    st->depth++;
    return result;
}

static Item interp_call_with_borrowed(Function* fn, const Item* args, int argc,
        InterpFrame* caller, NameEntry* const* borrowed_entries,
        uint64_t* const* borrow_homes) {
    InterpBorrowedCall borrowed = {};
    borrowed.caller = caller;
    for (int index = 0; index < LAMBDA_MAX_FUNCTION_ARGS; index++) {
        borrowed.entries[index] = borrowed_entries ? borrowed_entries[index] : NULL;
        borrowed.homes[index] = borrow_homes ? borrow_homes[index] : NULL;
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
static uint32_t interp_depth_budget(void);

// Retained DOM events execute after the script runner has unwound its T0
// state. Borrow the event's EvalContext for one activation instead of treating
// the absent runner state as a semantic failure; nested script execution still
// keeps its existing state untouched.
class InterpStateGuard {
    InterpState state_;
    InterpState* saved_;
    bool owns_;
public:
    explicit InterpStateGuard(Context* host)
            : state_{}, saved_(g_interp_state), owns_(false) {
        if (g_interp_state || !host) return;
        EvalContext* eval_context = (EvalContext*)host;
        state_.ctx = eval_context;
        state_.runtime = eval_context->runtime;
        state_.mode = EvalMode::RUNTIME;
        state_.depth_limit = interp_depth_budget();
        state_.depth = state_.depth_limit;
        g_interp_state = &state_;
        owns_ = true;
    }

    ~InterpStateGuard() {
        if (owns_) g_interp_state = saved_;
    }

    InterpState* get() const { return g_interp_state; }
    InterpStateGuard(const InterpStateGuard&) = delete;
    InterpStateGuard& operator=(const InterpStateGuard&) = delete;
};

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
    return interp_promotion_threshold("LAMBDA_JIT_THRESHOLD", 5);
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

// This is an intentionally opt-in experiment. The normal AUTO policy remains
// per-function satellites; the POC compiles one complete MIR image only after
// the first threshold trigger (D8.1.1v4/P2).
static bool interp_whole_script_poc_enabled(void) {
    const char* value = getenv("LAMBDA_AUTO_WHOLE_SCRIPT");
    return value && (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
}

static void* interp_whole_script_entry(Script* script, AstFuncNode* def) {
    if (!script || !script->jit_context || !def) return NULL;
    StrBuf* name = strbuf_new_cap(96);
    if (!name) return NULL;
    write_fn_name_ex(name, def, NULL, "_b");
    void* entry = find_func(script->jit_context, name->str);
    strbuf_free(name);
    return entry;
}

static bool interp_whole_script_publish_function(Script* script,
        AstFuncNode* def, Function* known_fn) {
    if (!script || !script->interp_whole_script_poc_active || !def ||
            !def->analysis || def->captures || def->is_generator ||
            def->is_async || def->analysis->may_await ||
            def->analysis->needs_task_context ||
            !interp_satellite_supported(def)) {
        // Whole-module lowering still exposes each function through the same
        // boxed ABI as a satellite. Keep the established aggregate/mutable
        // parameter admission gate or a valid MIR image can silently narrow
        // an Item container at the wrapper boundary (D8.1.1v4/D5.2).
        return false;
    }

    Function* fn = known_fn;
    if (!fn) {
        NameEntry* entry = def->analysis->decl_entry;
        LambdaModuleState* state = interp_module_state(script);
        if (!entry || !state || !entry->storage_assigned ||
                entry->binding_storage != BINDING_STORAGE_MODULE || entry->slot < 0) {
            return false;
        }
        Item value = lambda_module_var_at(state, (uint32_t)entry->slot);
        if (get_type_id(value) != LMD_TYPE_FUNC) return false;
        fn = value.function;
    }
    if (!fn || fn->def != def || fn->def_module != script || fn->closure_env ||
            fn->method) return false;
    if (fn->entry_abi != FN_ENTRY_ABI_LAMBDA_INTERPRETED) return true;

    void* entry = interp_whole_script_entry(script, def);
    if (!entry) return false;
    interp_upgrade_function_entry(fn, def, entry);
    def->analysis->promotion.state = FN_PROMOTION_COMPILED;
    def->analysis->promotion.boxed_entry = entry;
    return true;
}

static int interp_whole_script_publish_module_functions(Script* script) {
    if (!script || !script->interp_whole_script_poc_active || !script->ast_root) {
        return 0;
    }
    AstScript* root = (AstScript*)script->ast_root;
    int published = 0;
    for (NameEntry* entry = root->global_vars ? root->global_vars->first : NULL;
            entry; entry = entry->next) {
        AstNode* node = entry->node;
        if (!node || (node->node_type != AST_NODE_FUNC &&
                node->node_type != AST_NODE_FUNC_EXPR &&
                node->node_type != AST_NODE_PROC)) continue;
        AstFuncNode* def = (AstFuncNode*)node;
        if (interp_whole_script_publish_function(script, def, NULL)) published++;
    }
    return published;
}

static bool interp_whole_script_bind_module_image(Script* script) {
    if (!script || !script->jit_context) return false;
    MIR_item_t layout_item = find_import(script->jit_context, "_mod_layout");
    LambdaModuleLayout* layout = layout_item && layout_item->addr
        ? (LambdaModuleLayout*)layout_item->addr : NULL;
    if (!layout) return false;
    // The POC compiler emits every module binding through the owner's T0 slab;
    // preparing the eager MIR layout here would replace that live slab and
    // violate the interpreter's persistent-root invariant (D7.2.1).
    if (!lambda_module_state_bind_static(script->module_state_id,
            script->const_list ? script->const_list->data : NULL,
            script->type_list)) {
        log_error("interp-tier: whole-script POC module-state bind failed file=%s",
            script->reference ? script->reference : "<unknown>");
        return false;
    }
    return lambda_module_state_link_property_keys(script->module_state_id,
        layout->property_key_specs, layout->property_key_count,
        layout->property_key_bytes_size);
}

static bool interp_whole_script_compile(InterpState* st, Script* script,
        Function* trigger) {
    if (!st || !st->runtime || !script ||
            !interp_whole_script_poc_enabled() ||
            script->interp_whole_script_poc_attempted) return false;

    script->interp_whole_script_poc_attempted = true;
    if (!trigger || !trigger->def ||
            !interp_satellite_supported((AstFuncNode*)trigger->def)) {
        // The whole image still publishes functions through the satellite
        // boxed ABI. Reject an unsupported first trigger before paying for a
        // module compile that cannot safely become active (D8.1.1v4/D5.2).
        log_notice("interp-tier: whole-script POC skipped file=%s reason=trigger-boundary",
            script->reference ? script->reference : "<unknown>");
        return false;
    }
    if (script->jit_context) {
        // An async closure may have installed a satellite before the first
        // synchronous trigger; replacing that live MIR context would orphan
        // its entry, so this POC deliberately stays on the normal path.
        log_notice("interp-tier: whole-script POC skipped file=%s reason=existing-mir-context",
            script->reference ? script->reference : "<unknown>");
        return false;
    }
    if (script->direct_imports && script->direct_imports->length > 0) {
        // Full-module MIR linking requires compiled import symbols. Keep the
        // POC on the import-free cone until dependency images can be promoted
        // as one transaction (D7.2.2).
        log_notice("interp-tier: whole-script POC skipped file=%s reason=imports",
            script->reference ? script->reference : "<unknown>");
        return false;
    }
    if (script->direct_imports) {
        arraylist_free(script->direct_imports);
        script->direct_imports = NULL;
    }

    log_notice("interp-tier: whole-script POC trigger file=%s function='%s'",
        script->reference ? script->reference : "<unknown>",
        trigger && trigger->name ? trigger->name : "<anonymous>");

    Transpiler tp = {};
    memcpy(&tp, script, sizeof(Script));
    tp.script_owner = script;
    tp.runtime = st->runtime;
    tp.preserve_ast_index = true;
    tp.compile_against_interp_slab = true;
    tp.whole_script_poc = true;
    compile_script_as_mir_direct(&tp, script, script->reference,
        NULL, NULL, NULL, NULL, NULL, NULL);
    if (!script->jit_context || !script->main_func) {
        if (script->jit_context) {
            jit_cleanup_mode(script->jit_context,
                script->mir_gen_initialized ? 1 : 0);
        }
        script->jit_context = NULL;
        script->main_func = NULL;
        script->mir_gen_initialized = false;
        log_error("interp-tier: whole-script POC compile failed file=%s",
            script->reference ? script->reference : "<unknown>");
        return false;
    }
    if (!interp_whole_script_bind_module_image(script)) {
        jit_cleanup_mode(script->jit_context,
            script->mir_gen_initialized ? 1 : 0);
        script->jit_context = NULL;
        script->main_func = NULL;
        script->mir_gen_initialized = false;
        log_error("interp-tier: whole-script POC module-state bind failed file=%s",
            script->reference ? script->reference : "<unknown>");
        return false;
    }

    script->interp_whole_script_poc_active = true;
    bool trigger_published = interp_whole_script_publish_function(
        script, (AstFuncNode*)trigger->def, trigger);
    int published = interp_whole_script_publish_module_functions(script);
    if (!trigger_published) {
        script->interp_whole_script_poc_active = false;
        jit_cleanup_mode(script->jit_context,
            script->mir_gen_initialized ? 1 : 0);
        script->jit_context = NULL;
        script->main_func = NULL;
        script->mir_gen_initialized = false;
        log_error("interp-tier: whole-script POC could not publish trigger file=%s",
            script->reference ? script->reference : "<unknown>");
        return false;
    }
    log_notice("interp-tier: whole-script POC compiled file=%s functions=%d",
        script->reference ? script->reference : "<unknown>", published);
    return true;
}

static bool interp_promote_function(Function* fn, bool count_entry) {
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
    if (script->interp_whole_script_poc_active) {
        if (interp_whole_script_publish_function(script, (AstFuncNode*)def, fn)) {
            return true;
        }
        cell->state = FN_PROMOTION_PINNED_INTERP;
        return false;
    }
    if (count_entry && cell->call_count != UINT32_MAX) cell->call_count++;
    if (cell->call_count < interp_jit_threshold() &&
            cell->backedge_count < interp_jit_backedge_threshold() &&
            cell->tail_edge_count < interp_jit_threshold()) return false;
    if (interp_whole_script_poc_enabled()) {
        if (interp_whole_script_compile(st, script, fn)) return true;
        if (script->interp_whole_script_poc_active) {
            cell->state = FN_PROMOTION_PINNED_INTERP;
            return false;
        }
    }
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

bool interp_promote_function_if_hot(Function* fn) {
    return interp_promote_function(fn, true);
}

// a tail edge was counted before the caller reached this point. Reusing the
// publication path without another entry increment keeps the definition-site
// call counter equal to the source-level invocation count (D8.1.1v5).
static bool interp_promote_function_from_tail(Function* fn) {
    return interp_promote_function(fn, false);
}

static uint32_t interp_depth_budget(void) {
    const char* env = getenv("LAMBDA_INTERP_DEPTH");
    if (!env || !*env) return INTERP_DEFAULT_DEPTH;
    long value = strtol(env, NULL, 10);
    if (value < 16 || value > 10000000L) return INTERP_DEFAULT_DEPTH;
    return (uint32_t)value;
}

static void interp_register_view_template(Script* script, AstViewNode* view,
        int ordinal) {
    if (!script || !view || !g_template_registry || !view->body) return;

    TemplateSpecificity specificity = TMPL_SPEC_CATCHALL;
    TypeId match_type = LMD_TYPE_ANY;
    const char* match_tag = NULL;
    int match_tag_len = 0;
    const TypeElmt* match_elmt = NULL;
    AstNode* pattern = view->pattern;
    if (pattern && pattern->type) {
        TypeId tid = pattern->type->type_id;
        if (tid == LMD_TYPE_TYPE) {
            TypeType* type_value = (TypeType*)pattern->type;
            if (type_value->type && type_value->type->type_id != LMD_TYPE_ANY) {
                match_type = type_value->type->type_id;
                specificity = TMPL_SPEC_SIMPLE_TYPE;
                if (match_type == LMD_TYPE_ELEMENT) {
                    TypeElmt* element_type = (TypeElmt*)type_value->type;
                    if (element_type->name.str && element_type->name.length > 0) {
                        match_tag = element_type->name.str;
                        match_tag_len = (int)element_type->name.length;
                        match_elmt = element_type;
                        specificity = element_type->length > 0
                            ? TMPL_SPEC_ELMT_ATTR : TMPL_SPEC_ELMT_TAG;
                    }
                }
            }
        } else if (tid != LMD_TYPE_ANY) {
            match_type = tid;
            specificity = TMPL_SPEC_SIMPLE_TYPE;
        }
    }
    if (view->name) specificity = TMPL_SPEC_NAMED;

    template_registry_add(g_template_registry,
        view->name ? view->name->chars : NULL, view->is_edit, NULL, specificity,
        match_type, match_tag, match_tag_len, 0, 0);
    TemplateEntry* entry = g_template_registry->last;
    if (!entry) return;
    template_registry_set_element_pattern(entry, match_elmt);
    entry->interp_body_func = interp_eval_view_template;
    const char* generated_ref = view->name ? view->name->chars : NULL;
    if (!generated_ref) {
        char ref[48];
        snprintf(ref, sizeof(ref), "_interp_view_%d", ordinal);
        generated_ref = name_pool_create_len(script->name_pool, ref,
            strlen(ref))->chars;
    }
    entry->template_ref = generated_ref;
    entry->interp_view = view;
    entry->interp_module = script;
    for (AstEventHandler* handler = view->handler; handler;
            handler = handler->next_handler) {
        if (handler->event) {
            template_entry_add_interp_handler(entry, handler->event->chars,
                handler, view, script);
        }
    }
    log_debug("interp: registered view ref=%s type=%d state=%d handlers=%d",
        generated_ref, (int)match_type, view->state ? 1 : 0,
        view->handler ? 1 : 0);
}

static void interp_register_view_templates(Script* script) {
    if (!script || script->interp_views_registered || !g_template_registry ||
            !script->ast_root) return;
    AstNode* top = ((AstScript*)script->ast_root)->child;
    int ordinal = 0;
    for (AstNode* item = top; item; item = item->next) {
        AstNode* view_item = item;
        if (item->node_type == AST_NODE_CONTENT) {
            view_item = ((AstListNode*)item)->item;
        }
        while (view_item) {
            if (view_item->node_type == AST_NODE_VIEW) {
                interp_register_view_template(script, (AstViewNode*)view_item,
                    ordinal++);
            }
            view_item = item->node_type == AST_NODE_CONTENT
                ? view_item->next : NULL;
        }
    }
    script->interp_views_registered = true;
}

static NameEntry* interp_view_scope_entry(NameScope* scope, String* name) {
    if (!scope || !name) return NULL;
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        if (entry->name == name || (entry->name &&
                entry->name->len == name->len &&
                memcmp(entry->name->chars, name->chars, name->len) == 0)) {
            return entry;
        }
    }
    return interp_view_scope_entry(scope->parent, name);
}

static const char* interp_view_template_ref(AstViewNode* view) {
    if (!view) return NULL;
    if (view->name) return view->name->chars;
    if (!g_template_registry) return NULL;
    for (TemplateEntry* entry = g_template_registry->first; entry;
            entry = entry->next) {
        if (entry->interp_view == view) return entry->template_ref;
    }
    return NULL;
}

static int interp_view_state_count(AstViewNode* view, AstEventHandler* handler) {
    int count = 0;
    if (view) {
        for (AstStateEntry* state = view->state; state;
                state = state->next_state) count++;
    }
    if (handler && handler->param) count++;
    return count;
}

static Item interp_eval_view_activation(Context* host, Script* module,
        AstViewNode* view, AstNode* body, AstEventHandler* handler,
        Item model, Item event) {
    if (!host || !module || !view || !body) {
        log_error("interp: view invocation has no active interpreter");
        return ItemError;
    }
    InterpStateGuard state_guard(host);
    InterpState* st = state_guard.get();
    if (!st || !st->ctx) {
        log_error("interp: view invocation has no EvalContext");
        return ItemError;
    }
    const FnFramePlan* activation_plan = &module->interp_plan;
    if (handler && handler->interp_planned) {
        activation_plan = &handler->interp_plan;
    }
    InterpFrameGuard guard(st, NULL, module, activation_plan, NULL, 0);
    if (!guard.valid()) return ItemError;
    InterpFrame* frame = guard.frame();
    void** saved_consts = st->ctx->consts;
    void* saved_type_list = st->ctx->type_list;
    st->ctx->consts = module->const_list ? module->const_list->data : NULL;
    st->ctx->type_list = module->type_list;
    Scratch model_slot(frame);
    model_slot.set(model);
    InterpContextGuard occurrence(st, model_slot.home(), NULL);
    const char* template_ref = interp_view_template_ref(view);
    if (!template_ref) {
        log_error("interp: view has no registered template reference");
        st->ctx->consts = saved_consts;
        st->ctx->type_list = saved_type_list;
        return ItemError;
    }

    int binding_count = interp_view_state_count(view, handler);
    RootSpan value_roots((size_t)binding_count);
    InterpViewBinding* bindings = binding_count
        ? (InterpViewBinding*)mem_calloc((size_t)binding_count,
            sizeof(InterpViewBinding), MEM_CAT_EVAL) : NULL;
    InterpViewBinding* saved_bindings = st->view_bindings;
    st->view_bindings = saved_bindings;
    Item result = ItemError;
    int binding_index = 0;
    bool setup_ok = binding_count == 0 || (value_roots.valid() && bindings);
    if (setup_ok) {
        for (AstStateEntry* state = view->state; state && setup_ok;
                state = state->next_state) {
            NameEntry* entry = state->entry;
            if (!entry) {
                log_error("interp: view state '%s' has no binding entry",
                    state->name ? state->name->chars : "<unnamed>");
                setup_ok = false;
                break;
            }
            Item default_value = state->value ? eval_expr(frame, state->value) : ItemNull;
            if (interp_frame_pending(frame)) {
                setup_ok = false;
                break;
            }
            value_roots.words()[binding_index] = default_value.item;
            Item initial = tmpl_state_get_or_init(model_slot.get(), template_ref,
                state->name->chars,
                (Item){.item = value_roots.words()[binding_index]});
            value_roots.words()[binding_index] = initial.item;
            bindings[binding_index] = {entry, &value_roots.words()[binding_index],
                model_slot.home(), template_ref, state->name->chars, true,
                st->view_bindings};
            st->view_bindings = &bindings[binding_index++];
        }
        if (setup_ok && handler && handler->param) {
            NameEntry* entry = handler->param->entry;
            if (!entry) entry = interp_view_scope_entry(handler->vars,
                handler->param->name);
            if (!entry) {
                log_error("interp: view handler parameter has no binding entry");
                setup_ok = false;
            } else {
                value_roots.words()[binding_index] = event.item;
                bindings[binding_index] = {entry, &value_roots.words()[binding_index],
                    model_slot.home(), template_ref, NULL, false,
                    st->view_bindings};
                st->view_bindings = &bindings[binding_index++];
            }
        }
    }
    if (setup_ok) result = eval_expr(frame, body);
    st->view_bindings = saved_bindings;
    if (bindings) mem_free(bindings);
    st->ctx->consts = saved_consts;
    st->ctx->type_list = saved_type_list;
    if (interp_frame_pending(frame)) return interp_signal_payload(frame);
    return result;
}

extern "C" Item interp_eval_view_template(Context* host, Script* module,
        AstViewNode* view, Item model) {
    return interp_eval_view_activation(host, module, view,
        view ? view->body : NULL, NULL, model, ItemNull);
}

extern "C" Item interp_eval_view_handler(Context* host, Script* module,
        AstViewNode* view, AstEventHandler* handler, Item model, Item event) {
    return interp_eval_view_activation(host, module, view,
        handler ? handler->body : NULL, handler, model, event);
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
            if (frame->signal == EvalSignal::RETURNED ||
                    frame->signal == EvalSignal::ERROR_SKIP) {
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
    RuntimeCurrentFileScope current_file(runner ? runner->context : NULL,
        script->reference);
    RuntimeModuleStateScope module_state(runner ? runner->context : NULL);
    if (!lambda_module_state_prepare(script->module_state_id,
            script->interp_slab_count)) {
        log_error("interp: could not prepare module slab for '%s'", script->reference);
        return ItemError;
    }
    // T0 itself reads the AST pools directly, but any task-backed MIR satellite
    // published from this module resolves literals through the same
    // context-owned const/type image. Bind that image before the first closure
    // can enter its generated resumable wrapper (D7.2.1).
    if (!lambda_module_state_bind_static(script->module_state_id,
            script->const_list ? script->const_list->data : NULL,
            script->type_list)) {
        log_error("interp: could not bind static module image for '%s'",
            script->reference ? script->reference : "<none>");
        return ItemError;
    }
    if (!module_state.activate(script->module_state_id)) {
        log_error("interp: could not activate module slab for '%s'", script->reference);
        return ItemError;
    }
    runner->context->consts = script->const_list ? script->const_list->data : NULL;
    runner->context->type_list = script->type_list;
    interp_register_view_templates(script);
    return interp_execute_top_level_nodes(runner, st, script, root->child, run_main);
}

static Item interp_execute_repl_fragment(Runner* runner, InterpState* st,
        AstNode* fragment) {
    Script* script = runner ? runner->script : NULL;
    if (!script || !fragment) return ItemError;
    RuntimeModuleStateScope module_state(runner->context);
    if (!lambda_module_state_prepare(script->module_state_id,
            script->interp_slab_count)) {
        log_error("interp: could not prepare REPL module slab for '%s'", script->reference);
        return ItemError;
    }
    if (!lambda_module_state_bind_static(script->module_state_id,
            script->const_list ? script->const_list->data : NULL,
            script->type_list)) {
        log_error("interp: could not bind static REPL module image");
        return ItemError;
    }
    if (!module_state.activate(script->module_state_id)) {
        log_error("interp: could not activate REPL module slab");
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
    RuntimeExecutionScope execution_scope(runner->context);

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
    result = runtime_publish_result(runner->context, result);
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
