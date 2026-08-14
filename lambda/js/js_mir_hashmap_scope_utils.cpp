#include "js_mir_internal.hpp"
#include "js_exec_profile.h"
#include <limits.h>
#include <stdarg.h>

__thread NamePool* g_js_mir_name_pool_override = NULL;

void jm_set_name_pool_override(NamePool* pool) {
    g_js_mir_name_pool_override = pool;
}

static NamePool* jm_active_name_pool(void) {
    if (g_js_mir_name_pool_override) return g_js_mir_name_pool_override;
    JsMirTranspiler* mt = g_active_mir_transpiler;
    // parallel module workers own distinct transpiler pools; using the shared
    // runtime pool here races its hashmap during concurrent name interning.
    if (mt && mt->tp && mt->tp->name_pool) return mt->tp->name_pool;
    if (context && context->name_pool) return context->name_pool;
    return NULL;
}

const char* jm_persist_name(const char* name) {
    if (!name) return NULL;
    NamePool* pool = jm_active_name_pool();
    if (!pool) return name;
    String* stable = name_pool_create_name(pool, name);
    return stable ? stable->chars : name;
}

const char* jm_format_name(const char* format, ...) {
    if (!format) return NULL;
    NamePool* pool = jm_active_name_pool();
    if (!pool) return NULL;
    va_list ap;
    va_start(ap, format);
    va_list copy;
    va_copy(copy, ap);
    int length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (length < 0) {
        va_end(ap);
        return NULL;
    }
    char* buffer = (char*)mem_alloc((size_t)length + 1, MEM_CAT_JS_RUNTIME);
    if (!buffer) {
        va_end(ap);
        return NULL;
    }
    vsnprintf(buffer, (size_t)length + 1, format, ap);
    va_end(ap);
    String* stable = name_pool_create_len(pool, buffer, (size_t)length);
    mem_free(buffer);
    return stable ? stable->chars : NULL;
}

// ============================================================================
// Scope helpers
// ============================================================================

static bool jm_stack_ensure_slot(ArrayList* stack, int index) {
    if (!stack || index < 0) return false;
    while (stack->length <= index) {
        if (!arraylist_append(stack, NULL)) return false;
    }
    return true;
}

struct hashmap* jm_var_scope_at(JsMirTranspiler* mt, int depth) {
    if (!mt || !mt->var_scopes || depth < 0 || depth >= mt->var_scopes->length) return NULL;
    return (struct hashmap*)arraylist_get(mt->var_scopes, depth);
}

bool jm_var_scope_set(JsMirTranspiler* mt, int depth, struct hashmap* scope) {
    if (!mt || !mt->var_scopes || !jm_stack_ensure_slot(mt->var_scopes, depth)) return false;
    arraylist_set(mt->var_scopes, depth, scope);
    return true;
}

JsLoopLabels* jm_loop_label_at(JsMirTranspiler* mt, int index) {
    if (!mt || !mt->loop_stack || index < 0 || !jm_stack_ensure_slot(mt->loop_stack, index)) {
        return NULL;
    }
    JsLoopLabels* labels = (JsLoopLabels*)arraylist_get(mt->loop_stack, index);
    if (!labels) {
        labels = (JsLoopLabels*)mem_calloc(1, sizeof(JsLoopLabels), MEM_CAT_JS_RUNTIME);
        if (!labels) return NULL;
        arraylist_set(mt->loop_stack, index, labels);
    }
    return labels;
}

static void* jm_stack_item_at(ArrayList* stack, int index, size_t item_size) {
    if (!stack || index < 0 || !jm_stack_ensure_slot(stack, index)) {
        return NULL;
    }
    void* item = arraylist_get(stack, index);
    if (!item) {
        item = mem_calloc(1, item_size, MEM_CAT_JS_RUNTIME);
        if (!item) return NULL;
        arraylist_set(stack, index, item);
    }
    return item;
}

JsMirIteratorFrame* jm_for_of_iterator_at(JsMirTranspiler* mt, int index) {
    return mt ? (JsMirIteratorFrame*)jm_stack_item_at(mt->for_of_iterators,
        index, sizeof(JsMirIteratorFrame)) : NULL;
}

JsTryContext* jm_try_context_at(JsMirTranspiler* mt, int index) {
    return mt ? (JsTryContext*)jm_stack_item_at(mt->try_ctx_stack,
        index, sizeof(JsTryContext)) : NULL;
}

JsTryContext* jm_try_context_push(JsMirTranspiler* mt) {
    if (!mt) return NULL;
    JsTryContext* context = jm_try_context_at(mt, mt->try_ctx_depth);
    if (!context) return NULL;
    memset(context, 0, sizeof(*context));
    context->end_label_error_lane_state = JS_ERROR_LANE_UNREACHABLE;
    mt->try_ctx_depth++;
    return context;
}

int js_local_func_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(((JsLocalFuncEntry*)a)->name, ((JsLocalFuncEntry*)b)->name);
}
uint64_t js_local_func_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((JsLocalFuncEntry*)item)->name,
        strlen(((JsLocalFuncEntry*)item)->name), seed0, seed1);
}

int js_module_const_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(((JsModuleConstEntry*)a)->name, ((JsModuleConstEntry*)b)->name);
}
uint64_t js_module_const_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((JsModuleConstEntry*)item)->name,
        strlen(((JsModuleConstEntry*)item)->name), seed0, seed1);
}

bool jm_capture_uses_live_module_var(JsMirTranspiler* mt, FnCapture* capture) {
    if (!mt || !capture || !mt->module_consts || capture->force_env_capture) return false;
    JsModuleConstEntry lookup;
    memset(&lookup, 0, sizeof(lookup));
    lookup.name = jm_persist_name(capture->name);
    JsModuleConstEntry* entry =
        (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup);
    return entry && entry->const_type == MCONST_MODVAR;
}

bool jm_capture_is_lexical_meta_binding(const char* name) {
    return name && (strcmp(name, "_js_this") == 0 ||
        strcmp(name, "_js_new.target") == 0 ||
        strcmp(name, "_js_arguments") == 0);
}

int jm_capture_env_slot(FnCapture* capture, int dense_slot) {
    if (!capture) return dense_slot;
    if (capture->private_env_slot >= 0) return capture->private_env_slot;
    if (capture->scope_env_slot >= 0) return capture->scope_env_slot;
    return dense_slot;
}

static void js_call_root_value(void* owner, MIR_reg_t reg) {
    JsMirTranspiler* mt = (JsMirTranspiler*)owner;
    if (mt && mt->em.frame.active && reg) jm_create_gc_root_slot(mt, reg);
}

static void jm_note_call_error_lane(void* owner, JitExceptionEffect effect) {
    JsMirTranspiler* mt = (JsMirTranspiler*)owner;
    if (!mt) return;
    jm_error_lane_note_call(mt, effect);
}

JsMirTranspiler* jm_create_mir_transpiler(
    JsTranspiler* tp, MIR_context_t ctx, const char* filename, bool is_module,
    int import_capacity, int local_func_capacity, int var_scope_capacity,
    const char* log_prefix)
{
    JsMirTranspiler* mt = (JsMirTranspiler*)mem_alloc(sizeof(JsMirTranspiler), MEM_CAT_JS_RUNTIME);
    if (!mt) {
        log_error("%s: failed to allocate JsMirTranspiler", log_prefix ? log_prefix : "js-mir");
        return NULL;
    }
    memset(mt, 0, sizeof(JsMirTranspiler));
    mt->tp = tp;
    mt->ctx = ctx;
    mt->em.ctx = ctx;
    mt->em.name_pool = tp ? tp->name_pool : NULL;
    mt->em.note_mir_call = js_exec_profile_note_mir_call;
#if JS_EXEC_PROFILE_ENABLED
    // profile builds also rank helpers by dynamic call count (env-gated at emit)
    mt->em.helper_call_counter = js_exec_profile_helper_call_counter;
#endif
    mt->em.call_owner = mt;
    mt->em.root_call_value = js_call_root_value;
    mt->em.note_call_exception = jm_note_call_error_lane;
    mt->em.convert_rep = jm_convert_rep;
    mt->em.lookup_import_metadata = jit_import_get_metadata;
    mt->is_module = is_module;
    mt->filename = filename;
    mt->cascade_debug_site_counter = 100;
    mt->em.import_cache = em_import_cache_new(import_capacity);
    mt->local_funcs = hashmap_new(sizeof(JsLocalFuncEntry), local_func_capacity, 0, 0,
        js_local_func_hash, js_local_func_cmp, NULL, NULL);
    mt->var_scopes = arraylist_new(8);
    mt->loop_stack = arraylist_new(8);
    mt->for_of_iterators = arraylist_new(8);
    mt->try_ctx_stack = arraylist_new(8);
    if (!mt->var_scopes || !mt->loop_stack || !mt->for_of_iterators ||
            !mt->try_ctx_stack) {
        jm_destroy_mir_transpiler(mt);
        return NULL;
    }
    if (!arraylist_append(mt->var_scopes, em_var_scope_new(var_scope_capacity))) {
        jm_destroy_mir_transpiler(mt);
        return NULL;
    }
    mt->scope_depth = 0;
    mt->var_hoist_depth = -1;
    mt->loop_scope_depth = -1;
    mt->collect_parent_func_index = -1;
    mt->scope_env_reg = 0;
    mt->scope_env_slot_count = 0;
    mt->current_func_index = -1;
    return mt;
}

void jm_destroy_mir_transpiler(JsMirTranspiler* mt) {
    jm_cleanup_mir_transpiler_state(mt);
    mem_free(mt);
}

// Forward declarations
MIR_reg_t jm_create_func_or_closure(JsMirTranspiler* mt, JsFuncCollected* fc);
Type* jm_get_full_type(JsMirTranspiler* mt, JsAstNode* node);
JsFuncCollected* jm_find_collected_func_for_call(JsMirTranspiler* mt, JsCallNode* call);

// ============================================================================
// Basic MIR helpers
// ============================================================================

MIR_reg_t jm_new_reg(JsMirTranspiler* mt, const char* prefix, MIR_type_t type) {
    MIR_reg_t reg = em_new_reg(&mt->em, prefix, type);
    return reg;
}

MIR_label_t jm_new_label(JsMirTranspiler* mt) {
    MIR_label_t label = em_new_label(&mt->em);
    return label;
}

static void jm_clear_boxed_float_const_cache(JsMirTranspiler* mt) {
    if (!mt) return;
    mt->boxed_float_const_cache_count = mt->boxed_float_const_cache_seed_count;
    // Property-name Items are immutable NameId values.  They are still
    // block-local here: a first definition in one branch must not be reused
    // from a sibling branch that does not dominate it.
    mt->property_name_cache_count = 0;
    mt->module_name_id_cache_count = 0;
    mt->module_ic_cache_count = 0;
}

static void jm_ensure_index_map(int** map, int* capacity, int key) {
    if (!map || !capacity || key < 0 || key < *capacity) return;
    if (!em_root_ensure_index_map(map, capacity, key)) {
        // Losing a home-to-register binding would make the exact root frame
        // incomplete, so compilation must fail-stop on metadata OOM.
        log_error("js-mir-root-bindings: index-map allocation failed");
        abort();
    }
}

static void jm_register_root_binding(JsMirTranspiler* mt, MIR_reg_t reg,
        int slot, int home_id) {
    if (!mt || !reg || slot < 0) return;
    int reg_key = (int)reg;
    jm_ensure_index_map(&mt->em.frame.root_binding_by_reg,
        &mt->em.frame.root_binding_by_reg_capacity, reg_key);
    if (home_id > 0) {
        jm_ensure_index_map(&mt->em.frame.root_binding_by_home,
            &mt->em.frame.root_binding_by_home_capacity, home_id);
    }
    int binding_index = home_id > 0
        ? mt->em.frame.root_binding_by_home[home_id]
        : mt->em.frame.root_binding_by_reg[reg_key];
    if (binding_index >= 0 && binding_index < mt->em.frame.root_binding_count) {
        JsMirRootBinding* binding = &mt->em.frame.root_bindings[binding_index];
        if (binding->reg != reg && binding->reg > 0 &&
                binding->reg < (MIR_reg_t)mt->em.frame.root_binding_by_reg_capacity &&
                mt->em.frame.root_binding_by_reg[binding->reg] == binding_index) {
            mt->em.frame.root_binding_by_reg[binding->reg] = -1;
        }
        binding->reg = reg;
        binding->slot = slot;
        if (mt->em.frame.root_binding_by_reg[reg_key] < 0) {
            mt->em.frame.root_binding_by_reg[reg_key] = binding_index;
        }
        return;
    }
    if (mt->em.frame.root_binding_count >= mt->em.frame.root_binding_capacity) {
        int next_capacity = mt->em.frame.root_binding_capacity
            ? mt->em.frame.root_binding_capacity * 2 : 32;
        mt->em.frame.root_bindings = (JsMirRootBinding*)mem_realloc(
            mt->em.frame.root_bindings,
            (size_t)next_capacity * sizeof(JsMirRootBinding), MEM_CAT_JS_RUNTIME);
        mt->em.frame.root_binding_capacity = next_capacity;
    }
    binding_index = mt->em.frame.root_binding_count++;
    JsMirRootBinding* binding = &mt->em.frame.root_bindings[binding_index];
    binding->reg = reg;
    binding->slot = slot;
    binding->home_id = home_id;
    if (mt->em.frame.root_binding_by_reg[reg_key] < 0) {
        mt->em.frame.root_binding_by_reg[reg_key] = binding_index;
    }
    if (home_id > 0) {
        mt->em.frame.root_binding_by_home[home_id] = binding_index;
    }
}

static void jm_note_gc_candidate(JsMirTranspiler* mt, MIR_reg_t reg,
        JitValueClass value_class, int home_id) {
    if (!mt || !reg) return;
    if (!em_root_note_candidate(&mt->em.frame.gc_candidates,
            &mt->em.frame.gc_candidate_count, &mt->em.frame.gc_candidate_capacity,
            &mt->em.frame.gc_candidate_by_reg,
            &mt->em.frame.gc_candidate_by_reg_capacity, reg, value_class,
            home_id)) {
        log_error("js-mir-root-candidates: unable to record reg=%u",
            (unsigned)reg);
        // A missing semantic candidate cannot be repaired by the later
        // write-through fallback because its identity has already been lost.
        abort();
    }
}

static void jm_unbind_root_home(JsMirTranspiler* mt, int home_id) {
    if (!mt || home_id <= 0) return;
    if (home_id >= mt->em.frame.root_binding_by_home_capacity) return;
    int binding_index = mt->em.frame.root_binding_by_home[home_id];
    if (binding_index < 0 || binding_index >= mt->em.frame.root_binding_count) return;
    JsMirRootBinding* binding = &mt->em.frame.root_bindings[binding_index];
    if (binding->reg > 0 &&
            binding->reg < (MIR_reg_t)mt->em.frame.root_binding_by_reg_capacity &&
            mt->em.frame.root_binding_by_reg[binding->reg] == binding_index) {
        mt->em.frame.root_binding_by_reg[binding->reg] = -1;
    }
    binding->reg = 0;
}

void jm_register_owned_env(JsMirTranspiler* mt, MIR_reg_t reg) {
    if (!mt || !mt->em.frame.active || !reg) return;
    for (int i = 0; i < mt->em.frame.env_binding_count; i++) {
        if (mt->em.frame.env_bindings[i].source_reg == reg) return;
    }
    if (mt->em.frame.env_binding_count >= mt->em.frame.env_binding_capacity) {
        int next_capacity = mt->em.frame.env_binding_capacity
            ? mt->em.frame.env_binding_capacity * 2 : 8;
        mt->em.frame.env_bindings = (JsMirEnvBinding*)mem_realloc(
            mt->em.frame.env_bindings,
            (size_t)next_capacity * sizeof(JsMirEnvBinding), MEM_CAT_JS_RUNTIME);
        mt->em.frame.env_binding_capacity = next_capacity;
    }
    // MIR name reuse can overwrite an allocation-result register before the
    // unified epilogue. Preserve the environment pointer in a dedicated SSA-like
    // register so scalar rehoming never receives a later raw state value.
    MIR_reg_t stable_reg = jm_new_reg(mt, "js_owned_env", MIR_T_I64);
    em_emit_insn(&mt->em, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, stable_reg), MIR_new_reg_op(mt->ctx, reg)));
    JsMirEnvBinding* binding = &mt->em.frame.env_bindings[mt->em.frame.env_binding_count++];
    binding->source_reg = reg;
    binding->reg = stable_reg;
    // The epilogue uses this copied register after the allocation-result
    // register may have been overwritten. It is therefore its own semantic
    // raw-GC-pointer lifetime, not merely a machine-level MOV temporary.
    jm_note_gc_candidate(mt, stable_reg, JIT_VALUE_RAW_GC_POINTER, 0);
}

void jm_emit_loop_backedge_frame_reload(JsMirTranspiler* mt) {
    if (!mt || !mt->em.frame.active || !mt->em.frame.root_base) return;
    // The enclosing generated entry already owns this register.  A loop
    // backedge is hot, so reloading a process-global runtime pointer here
    // would both violate context ownership and add avoidable work per loop.
    MIR_reg_t runtime = mt->em.frame.runtime;
    MIR_reg_t top = jm_new_reg(mt, "js_root_top_backedge", MIR_T_I64);
    em_emit_insn(&mt->em, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, top),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, offsetof(Context, side_root_top),
            runtime, 0, 1)));
    MIR_insn_t reload = MIR_new_insn(mt->ctx, MIR_SUB,
        MIR_new_reg_op(mt->ctx, mt->em.frame.root_base),
        MIR_new_reg_op(mt->ctx, top), MIR_new_int_op(mt->ctx, 0));
    em_emit_insn(&mt->em, reload);
    if (mt->em.frame.root_backedge_reload_count >= mt->em.frame.root_backedge_reload_capacity) {
        int next_capacity = mt->em.frame.root_backedge_reload_capacity
            ? mt->em.frame.root_backedge_reload_capacity * 2 : 8;
        mt->em.frame.root_backedge_reloads = (MIR_insn_t*)mem_realloc(
            mt->em.frame.root_backedge_reloads,
            (size_t)next_capacity * sizeof(MIR_insn_t), MEM_CAT_JS_RUNTIME);
        mt->em.frame.root_backedge_reload_capacity = next_capacity;
    }
    mt->em.frame.root_backedge_reloads[mt->em.frame.root_backedge_reload_count++] = reload;
}

int jm_create_gc_root_slot(JsMirTranspiler* mt, MIR_reg_t value) {
    if (!mt || !mt->em.frame.active || !value) return -1;
    jm_note_gc_candidate(mt, value, JIT_VALUE_UNKNOWN, 0);
    if (value < (MIR_reg_t)mt->em.frame.root_binding_by_reg_capacity) {
        int binding_index = mt->em.frame.root_binding_by_reg[value];
        if (binding_index >= 0 && binding_index < mt->em.frame.root_binding_count) {
            JsMirRootBinding* binding = &mt->em.frame.root_bindings[binding_index];
            return binding->slot;
        }
    }
    for (int i = 0; i < mt->em.frame.root_binding_count; i++) {
        JsMirRootBinding* binding = &mt->em.frame.root_bindings[i];
        if (binding->reg == value) {
            return binding->slot;
        }
    }
    int slot = mt->em.frame.root_slot_count++;
    jm_register_root_binding(mt, value, slot, 0);
    return slot;
}

static bool jm_should_gc_root_var(MIR_type_t mir_type, TypeId type_id) {
    if (mir_type == MIR_T_P) return true;
    // Manually installed capture/state bindings predate typed VarEntry fields;
    // an unset MIR type therefore means boxed JS Item, not an unrootable scalar.
    if (mir_type == MIR_T_UNDEF) return true;
    switch (type_id) {
    case LMD_TYPE_DECIMAL:
    case LMD_TYPE_DTIME:
    case LMD_TYPE_SYMBOL:
    case LMD_TYPE_STRING:
    case LMD_TYPE_BINARY:
    case LMD_TYPE_PATH:
    case LMD_TYPE_RANGE:
    case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_MAP:
    case LMD_TYPE_VMAP:
    case LMD_TYPE_ELEMENT:
    case LMD_TYPE_OBJECT:
    case LMD_TYPE_TYPE:
    case LMD_TYPE_FUNC:
    case LMD_TYPE_ANY:
    case LMD_TYPE_ERROR:
        return true;
    default:
        return false;
    }
}

static JitValueClass jm_gc_value_class(MIR_type_t mir_type, TypeId type_id) {
    if (!jm_should_gc_root_var(mir_type, type_id)) {
        return JIT_VALUE_NON_GC_SCALAR;
    }
    return mir_type == MIR_T_P ? JIT_VALUE_RAW_GC_POINTER
        : JIT_VALUE_BOXED_ITEM;
}

void jm_update_gc_root_slot(JsMirTranspiler* mt, JsMirVarEntry* var) {
    if (!mt || !var || !mt->em.frame.active) return;
    if (!jm_should_gc_root_var(var->mir_type, var->type_id)) {
        if (var->root_slot >= 0) {
            // A stable binding can change representation. Clear its canonical
            // home instead of moving a double/scalar through an Item slot or
            // retaining the prior managed pointer.
            jm_unbind_root_home(mt, var->gc_home_id);
        }
        return;
    }
    if (var->root_slot < 0) {
        var->root_slot = mt->em.frame.root_slot_count++;
    }
    jm_note_gc_candidate(mt, var->reg,
        jm_gc_value_class(var->mir_type, var->type_id), var->gc_home_id);
    jm_register_root_binding(mt, var->reg, var->root_slot, var->gc_home_id);
}

void jm_begin_function_frame(JsMirTranspiler* mt, MIR_type_t return_type,
        bool item_return, MirScalarReturnMode scalar_return_mode,
        MIR_reg_t runtime_reg, bool clean_error_lane_entry) {
    if (!mt) return;
    // Function entry starts unknown because the preceding native return may
    // contain either a value or a returned ERROR Item; no ambient state exists.
    mt->error_lane_track = JS_ERROR_LANE_UNKNOWN;
    // Frame-local result registers cannot cross a function boundary.  A clean
    // entry used to hide this stale register until a generator resume label
    // reopened the lane, producing MIR that referenced another function's reg.
    mt->last_call_result_reg = 0;
    mt->func_error_lane_value_reg = 0;
    mt->arg_stack_scope = NULL;
    mt->arg_frame_base = 0;
    mt->arg_frame_base_add = NULL;
    mt->arg_frame_depth = 0;
    mt->arg_frame_slot_count = 0;
    em_frame_dispose(&mt->em);
    mt->em.frame.return_type = return_type;
    mt->em.frame.item_return = item_return;
    mt->em.frame.scalar_return_mode = scalar_return_mode;
    if (!runtime_reg) {
        // Every compiled entry has an explicit context parameter. Falling back
        // to `_lambda_rt` would make the generated hot path process-global.
        log_error("js-mir-frame: missing explicit runtime register");
        abort();
    }
    mt->em.frame.runtime = runtime_reg;
    mt->em.frame.root_base = jm_new_reg(mt, "js_root_frame", MIR_T_I64);
    mt->em.frame.number_base = jm_new_reg(mt, "js_number_frame", MIR_T_I64);
    mt->em.frame.anchor = jm_new_label(mt);
    mt->em.frame.return_label = jm_new_label(mt);
    mt->em.frame.return_reg = jm_new_reg(mt, "js_return_value", return_type);
    mt->em.frame.plan.entry_kind = FN_ENTRY_PUBLIC_WRAPPER;
    mt->em.frame.plan.entry_mode = MIR_ENTRY_CHECKED;
    mt->em.frame.active = true;
    jm_emit_label(mt, mt->em.frame.anchor);
    if (clean_error_lane_entry) {
        jm_error_lane_set_state(mt, JS_ERROR_LANE_CLEAN);
    }
}

static void jm_finalize_side_root_prologue(JsMirTranspiler* mt) {
    if (!mt) return;
    em_finalize_frame_prologue(&mt->em, mt->em.frame.plan.entry_mode,
        offsetof(Context, side_root_top), offsetof(Context, side_root_limit),
        offsetof(Context, side_number_top), offsetof(Context, side_number_limit),
        offsetof(Context, side_root_commit_limit),
        offsetof(Context, side_number_commit_limit));
    jm_call_void_1(mt, "lambda_stack_overflow_error", MIR_T_P,
        MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)"js-side-stack"));
    MIR_op_t failure = mt->em.frame.return_type == MIR_T_D
        ? MIR_new_double_op(mt->ctx, 0.0)
        : mt->em.frame.item_return
            ? MIR_new_uint_op(mt->ctx, ITEM_NULL_VAL)
            : MIR_new_int_op(mt->ctx, 0);
    em_emit_insn(&mt->em, MIR_new_ret_insn(mt->ctx, 1, failure));
}

static void jm_finalize_write_back_roots(JsMirTranspiler* mt) {
    if (!mt) return;
    MirRootWriteBackResult result = {};
    em_finalize_semantic_root_write_back(&mt->em,
        mt->em.frame.root_base, mt->em.frame.anchor, false, 0,
        &mt->em.frame.gc_candidates, &mt->em.frame.gc_candidate_count,
        &mt->em.frame.gc_candidate_capacity, &mt->em.frame.gc_candidate_by_reg,
        &mt->em.frame.gc_candidate_by_reg_capacity, mt->em.frame.gc_call_sites,
        mt->em.frame.gc_call_site_count, &result, "LambdaJS");
    mt->em.frame.root_slot_count = result.stable_slots + result.scratch_slots;
    mt->em.frame.root_store_count = result.inserted_stores;
}

void jm_finish_function_frame(JsMirTranspiler* mt, const char* function_name) {
    if (!mt || !mt->em.frame.active) return;
    mt->em.frame.plan.debug_name = function_name;
    jm_emit_label(mt, mt->em.frame.return_label);
    for (int i = 0; i < mt->em.frame.env_binding_count; i++) {
        MIR_type_t arg_type = MIR_T_P;
        MIR_op_t arg = MIR_new_reg_op(mt->ctx, mt->em.frame.env_bindings[i].reg);
        em_call_void_with_args(&mt->em, "js_env_rehome_scalars", 1,
            &arg_type, &arg, true);
    }
    if (mt->em.frame.item_return) {
        MIR_reg_t rehomed = mt->em.frame.return_reg;
        if (mt->em.frame.incoming_scalar_home) {
            rehomed = em_adopt_scalar_item(&mt->em,
                mt->em.frame.scalar_return_mode, mt->em.frame.return_reg,
                mt->em.frame.runtime, offsetof(Context, side_number_top),
                mt->em.frame.number_base,
                mt->em.frame.incoming_scalar_home);
        } else {
            if (mt->em.frame.scalar_return_mode != MIR_SCALAR_RETURN_NONE) {
                // js_main hands its result to an outer entrypoint while this
                // context is still alive; that boundary adopts the provided
                // result home before restoring the module number extent.
            } else {
                em_store_frame_top(&mt->em, mt->em.frame.runtime,
                    offsetof(Context, side_number_top), mt->em.frame.number_base);
            }
        }
        if (rehomed != mt->em.frame.return_reg) {
            em_emit_insn(&mt->em, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, mt->em.frame.return_reg),
                MIR_new_reg_op(mt->ctx, rehomed)));
        }
    } else {
        em_store_frame_top(&mt->em, mt->em.frame.runtime,
            offsetof(Context, side_number_top), mt->em.frame.number_base);
    }
    if (mt->em.frame.root_slot_count > 0 || mt->arg_frame_slot_count > 0) {
        em_store_frame_top(&mt->em, mt->em.frame.runtime,
            offsetof(Context, side_root_top), mt->em.frame.root_base);
    }
    em_emit_insn(&mt->em, MIR_new_ret_insn(mt->ctx, 1,
        MIR_new_reg_op(mt->ctx, mt->em.frame.return_reg)));
    jm_finalize_write_back_roots(mt);
    em_finalize_scalar_homes(&mt->em);
    if (mt->arg_frame_slot_count > 0) {
        if (!mt->arg_frame_base_add ||
                mt->arg_frame_base_add->nops < 3 ||
                mt->arg_frame_base_add->ops[2].mode != MIR_OP_INT) {
            log_error("js-mir arg-frame invariant: missing base fixup");
            abort();
        }
        // Semantic roots are colored first; argument roots form one fixed
        // suffix so every call site can use a stable frame-relative address.
        mt->arg_frame_base_add->ops[2].u.i =
            (int64_t)mt->em.frame.root_slot_count *
            (int64_t)sizeof(uint64_t);
        mt->em.frame.root_slot_count += mt->arg_frame_slot_count;
    }
    // Scratch coloring fixes the physical frame size only after the complete
    // function, including cleanup calls, has been analyzed.
    int64_t root_frame_bytes =
        (int64_t)mt->em.frame.root_slot_count * (int64_t)sizeof(uint64_t);
    for (int i = 0; i < mt->em.frame.root_backedge_reload_count; i++) {
        MIR_insn_t reload = mt->em.frame.root_backedge_reloads[i];
        if (reload && reload->nops >= 3 && reload->ops[2].mode == MIR_OP_INT) {
            reload->ops[2].u.i = root_frame_bytes;
        }
    }
    mt->em.frame.active = false;
    jm_finalize_side_root_prologue(mt);
    em_finalize_function_metadata(&mt->em);
    em_frame_dispose(&mt->em);
}

void jm_emit(JsMirTranspiler* mt, MIR_insn_t insn) {
    if (mt->em.frame.active && insn->code == MIR_RET) {
        if (insn->nops != 1) {
            log_error("js-mir frame: expected one return operand, got %u", insn->nops);
            em_emit_insn(&mt->em, insn);
            return;
        }
        MIR_insn_code_t move = mt->em.frame.return_type == MIR_T_D ? MIR_DMOV : MIR_MOV;
        em_emit_insn(&mt->em, MIR_new_insn(mt->ctx, move,
            MIR_new_reg_op(mt->ctx, mt->em.frame.return_reg), insn->ops[0]));
        em_emit_insn(&mt->em, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, mt->em.frame.return_label)));
        jm_error_lane_set_state(mt, JS_ERROR_LANE_UNREACHABLE);
        _MIR_free_insn(mt->ctx, insn);
        return;
    }
    em_emit_insn(&mt->em, insn);
    if (!insn) return;
    if (insn->code == MIR_JMP || insn->code == MIR_RET)
        jm_clear_boxed_float_const_cache(mt);
    if (insn->code == MIR_JMP || insn->code == MIR_RET) {
        jm_error_lane_set_state(mt, JS_ERROR_LANE_UNREACHABLE);
    }
}

void jm_emit_label(JsMirTranspiler* mt, MIR_label_t label) {
    if (!label) {
        log_error("js-mir: attempt to emit NULL label — skipping");
        return;
    }
    jm_clear_boxed_float_const_cache(mt);
    // Async state-machine labels merge distinct resume activations, so the
    // prior call result cannot dominate the label. Ordinary labels can be
    // deliberate exception-rethrow targets and must retain their Item carrier.
    if (mt->in_async && !mt->in_generator) mt->last_call_result_reg = 0;
    jm_error_lane_set_state(mt, JS_ERROR_LANE_UNKNOWN);
    em_emit_label(&mt->em, label);
}

void jm_emit_label_with_state(JsMirTranspiler* mt, MIR_label_t label, JsErrorLaneTrack state) {
    if (!label) {
        log_error("js-mir: attempt to emit NULL structured label — skipping");
        return;
    }
    // Structured labels can be entered from an abrupt edge.  A cached property
    // key register defined in an unreachable predecessor therefore does not
    // dominate the handler (for example `throw x; p = 1;` followed by `catch`
    // reading `p`).  Re-materialize the key at every merged label so MIR never
    // consumes an uninitialized root slot (D8.4.3).
    jm_clear_boxed_float_const_cache(mt);
    if (mt->in_async && !mt->in_generator && state != JS_ERROR_LANE_SET)
        mt->last_call_result_reg = 0;
    jm_error_lane_set_state(mt, state == JS_ERROR_LANE_UNREACHABLE ? JS_ERROR_LANE_UNKNOWN : state);
    em_emit_label(&mt->em, label);
}

static int jm_find_current_scope_env_slot(JsMirTranspiler* mt, const char* name) {
    if (!mt || !name || !mt->current_fc || !mt->current_fc->scope_env_names) return -1;
    for (int i = 0; i < mt->current_fc->scope_env_count; i++) {
        if (strcmp(mt->current_fc->scope_env_names[i], name) == 0) return i;
    }
    return -1;
}

void jm_emit_begin_lexical_this_rebind(JsMirTranspiler* mt, MIR_reg_t value,
        JsMirLexicalThisRebind* state, bool restore_binding) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->scope_env_slot = -1;
    if (!mt || !value) return;

    state->saved_force_closure_env_copy = mt->force_closure_env_copy;
    state->restore_binding = restore_binding;
    // field-initializer arrows must capture the instance/class `this`, not the
    // enclosing function's shared `_js_this` cell that is restored after init.
    mt->force_closure_env_copy = true;

    JsMirVarEntry* js_this_var = jm_find_var(mt, "_js_this");
    if (js_this_var && js_this_var->reg != 0) {
        state->var_reg = js_this_var->reg;
        if (restore_binding) {
            state->saved_var_reg = jm_new_reg(mt, "prev_jt", MIR_T_I64);
            jm_emit_mov(mt, state->saved_var_reg, state->var_reg);
        }
        jm_emit_mov(mt, state->var_reg, value);
    }

    int scope_slot = -1;
    MIR_reg_t scope_reg = 0;
    if (mt->scope_env_reg != 0) {
        scope_slot = jm_find_current_scope_env_slot(mt, "_js_this");
        if (scope_slot >= 0) scope_reg = mt->scope_env_reg;
    }
    if (scope_slot < 0 && js_this_var && js_this_var->scope_env_reg != 0 &&
            js_this_var->scope_env_slot >= 0) {
        scope_slot = js_this_var->scope_env_slot;
        scope_reg = js_this_var->scope_env_reg;
    }
    if (scope_reg != 0 && scope_slot >= 0) {
        state->scope_env_reg = scope_reg;
        state->scope_env_slot = scope_slot;
        if (restore_binding) {
            state->saved_scope_env_value_reg = jm_new_reg(mt, "prev_jt_env", MIR_T_I64);
            jm_emit_load_i64(mt, state->saved_scope_env_value_reg, scope_slot * (int)sizeof(uint64_t), scope_reg);
        }
        jm_emit_store_i64(mt, scope_slot * (int)sizeof(uint64_t), scope_reg, value);
    }
}

void jm_emit_end_lexical_this_rebind(JsMirTranspiler* mt,
        const JsMirLexicalThisRebind* state) {
    if (!mt || !state) return;
    if (state->restore_binding) {
        if (state->scope_env_reg != 0 && state->scope_env_slot >= 0 &&
                state->saved_scope_env_value_reg != 0) {
            jm_emit_store_i64(mt, state->scope_env_slot * (int)sizeof(uint64_t), state->scope_env_reg, state->saved_scope_env_value_reg);
        }
        if (state->var_reg != 0 && state->saved_var_reg != 0) {
            jm_emit_mov(mt, state->var_reg, state->saved_var_reg);
        }
    }
    mt->force_closure_env_copy = state->saved_force_closure_env_copy;
}

// Eval completion value: reset completion register to undefined.
// Called at the point where the ES spec says "Let V = undefined" for compound statements.
void jm_eval_cptn_reset(JsMirTranspiler* mt) {
    if (mt->eval_completion_reg) {
        jm_emit_reg_op(mt, MIR_MOV, mt->eval_completion_reg, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED));
    }
}

// v11: push loop labels, consuming any pending label from a labeled statement
void jm_push_loop_labels(JsMirTranspiler* mt, MIR_label_t continue_label, MIR_label_t break_label) {
    JsLoopLabels* labels = jm_loop_label_at(mt, mt->loop_depth);
    if (labels) {
        labels->continue_label = continue_label;
        labels->break_label = break_label;
        labels->iterator_to_close = 0;
        labels->label_name = mt->pending_label_name;
        labels->label_name_len = mt->pending_label_len;
        mt->loop_depth++;
    }
    mt->pending_label_name = NULL;
    mt->pending_label_len = 0;
}

// Zero-extend uint8_t return value to full i64 (needed on Windows x64 ABI
// where upper bits of RAX may contain garbage after a uint8_t-returning call)
MIR_reg_t jm_emit_uext8(JsMirTranspiler* mt, MIR_reg_t r) {
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_UEXT8,
        MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, r)));
    return r;
}

// ============================================================================
// Scope management
// ============================================================================

void jm_push_scope(JsMirTranspiler* mt) {
    if (!mt) return;
    mt->scope_depth++;
    struct hashmap* scope = em_var_scope_new(16);
    if (!scope || !jm_var_scope_set(mt, mt->scope_depth, scope)) {
        if (scope) hashmap_free(scope);
        mt->scope_depth--;
        log_error("js-mir: failed to grow lexical scope stack");
    }
}

static bool jm_arguments_param_matches_vname(JsAstNode* param, const char* vname) {
    JsIdentifierNode* identifier = jm_get_param_identifier(param);
    if (!identifier || !identifier->name || !vname || strncmp(vname, "_js_", 4) != 0) {
        return false;
    }
    const char* source_name = vname + 4;
    size_t source_len = strlen(source_name);
    return identifier->name->len == source_len &&
        memcmp(identifier->name->chars, source_name, source_len) == 0;
}

static JsAstNode* jm_arguments_param_at(JsMirTranspiler* mt, int param_index) {
    if (!mt || !mt->arguments_params || param_index < 0) return NULL;
    JsAstNode* param = mt->arguments_params;
    for (int index = 0; param && index < param_index; index++) param = param->next;
    return param;
}

static JsMirVarEntry* jm_find_var_for_param_identifier(JsMirTranspiler* mt,
        JsIdentifierNode* identifier) {
    if (!mt || !identifier || !identifier->name) return NULL;
    int depth = mt->arguments_param_scope_depth;
    struct hashmap* scope = jm_var_scope_at(mt, depth);
    if (depth < 0 || depth > mt->scope_depth || !scope) return NULL;
    size_t iter = 0;
    void* item = NULL;
    while (hashmap_iter(scope, &iter, &item)) {
        JsVarScopeEntry* entry = (JsVarScopeEntry*)item;
        if (strncmp(entry->name, "_js_", 4) != 0) continue;
        const char* source_name = entry->name + 4;
        if (strlen(source_name) == identifier->name->len &&
            memcmp(source_name, identifier->name->chars,
                identifier->name->len) == 0) {
            return &entry->var;
        }
    }
    return NULL;
}

// v20: Find the formal parameter index for a variable name in arguments aliasing.
// Returns -1 if not found or arguments aliasing is not active.
int jm_arguments_param_index(JsMirTranspiler* mt, const char* vname,
        JsMirVarEntry* resolved_var) {
    if (!mt || !resolved_var || mt->arguments_reg == 0 || !mt->arguments_params) return -1;
    int matched_index = -1;
    int index = 0;
    for (JsAstNode* param = mt->arguments_params; param; param = param->next, index++) {
        if (jm_arguments_param_matches_vname(param, vname)) {
            // Duplicate sloppy formals map only the rightmost argument index.
            matched_index = index;
        }
    }
    if (matched_index < 0) return -1;
    // A same-named inner declaration must not write through to a formal's
    // mapped argument; binding identity, rather than spelling, decides it.
    return jm_arguments_param_var(mt, matched_index) == resolved_var
        ? matched_index : -1;
}

// Return the formal binding paired with arguments[index], if one exists.
JsMirVarEntry* jm_arguments_param_var(JsMirTranspiler* mt, int param_index) {
    JsAstNode* param = jm_arguments_param_at(mt, param_index);
    JsIdentifierNode* identifier = jm_get_param_identifier(param);
    if (!identifier || !identifier->name) return NULL;

    // Earlier duplicate formals are ordinary arguments properties, not mapped
    // bindings.  The final matching formal is the one kept by the JS binding.
    for (JsAstNode* later = param->next; later; later = later->next) {
        JsIdentifierNode* later_identifier = jm_get_param_identifier(later);
        if (later_identifier && later_identifier->name &&
            later_identifier->name->len == identifier->name->len &&
            memcmp(later_identifier->name->chars, identifier->name->chars,
                identifier->name->len) == 0) {
            return NULL;
        }
    }

    return jm_find_var_for_param_identifier(mt, identifier);
}

// v20: Check if a function body starts with "use strict" directive.
bool jm_has_use_strict_directive(JsFunctionNode* fn) {
    return fn && fn->has_use_strict_directive;
}

// v20: Emit writeback from param register to arguments[param_index]
// NOTE: This is a forward declaration stub. Actual implementation uses jm_call_3
// which isn't available until after all helpers are defined. So we use a runtime
// function that takes (arguments, index, value).
void jm_arguments_writeback_param(JsMirTranspiler* mt, int param_index, MIR_reg_t val_reg);

void jm_pop_scope(JsMirTranspiler* mt) {
    if (mt->scope_depth <= 0) { log_error("js-mir: scope underflow"); return; }
    int kept_tdz_captures = 0;
    for (int i = 0; i < mt->tdz_closure_capture_count; i++) {
        if (mt->tdz_closure_captures[i].binding_scope_depth == mt->scope_depth) continue;
        if (kept_tdz_captures != i) {
            mt->tdz_closure_captures[kept_tdz_captures] = mt->tdz_closure_captures[i];
        }
        kept_tdz_captures++;
    }
    mt->tdz_closure_capture_count = kept_tdz_captures;
    struct hashmap* scope = jm_var_scope_at(mt, mt->scope_depth);
    if (scope) hashmap_free(scope);
    jm_var_scope_set(mt, mt->scope_depth, NULL);
    mt->scope_depth--;
}

JsMirVarEntry* jm_find_var(JsMirTranspiler* mt, const char* name);

JsMirVarEntry* jm_install_fresh_var_entry(JsMirTranspiler* mt, int depth,
        JsVarScopeEntry* entry) {
    struct hashmap* scope = jm_var_scope_at(mt, depth);
    if (!mt || !entry || depth < 0 || depth > mt->scope_depth || !scope) {
        return NULL;
    }
    // Direct scope insertion used to leave root_slot at memset's zero, which
    // falsely looked rooted and bypassed semantic-home registration.
    entry->var.root_slot = -1;
    entry->var.gc_home_id = mt->em.frame.active
        ? em_gc_new_home(&mt->em) : 0;
    hashmap_set(scope, entry);

    JsVarScopeEntry key;
    memset(&key, 0, sizeof(key));
    key.name = entry->name;
    JsVarScopeEntry* inserted = (JsVarScopeEntry*)hashmap_get(
        scope, &key);
    if (!inserted) return NULL;
    jm_update_gc_root_slot(mt, &inserted->var);
    return &inserted->var;
}

void jm_set_var(JsMirTranspiler* mt, const char* name, MIR_reg_t reg,
                       MIR_type_t mir_type , TypeId type_id ) {
    int target_depth = (mt->var_hoist_depth >= 0) ? mt->var_hoist_depth : mt->scope_depth;
    JsVarScopeEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.name = mir_em_persist_cstr(&mt->em, name).str;
    entry.var.reg = reg;
    entry.var.root_slot = -1;
    entry.var.gc_home_id = 0;
    entry.var.async_slot = -1;
    entry.var.mir_type = mir_type;
    entry.var.type_id = type_id;
    entry.var.typed_array_type = -1;  // P9: not a typed array by default

    // Preserve metadata from an existing same-named binding. Prefer the target
    // scope so a nested let/const shadow does not inherit an outer capture slot.
    // When a captured block binding is pre-registered in an enclosing function
    // scope, it is still TDZ-active when the real block binding is initialized;
    // that case may safely inherit the scope-env slot.
    {
        JsMirVarEntry* existing = NULL;
        bool existing_in_target_scope = false;
        struct hashmap* target_scope = jm_var_scope_at(mt, target_depth);
        if (target_depth >= 0 && target_scope) {
            JsVarScopeEntry key;
            memset(&key, 0, sizeof(key));
            key.name = name;
            JsVarScopeEntry* found = (JsVarScopeEntry*)hashmap_get(target_scope, &key);
            if (found) {
                existing = &found->var;
                existing_in_target_scope = true;
            }
        }
        if (!existing) existing = jm_find_var(mt, name);
        bool generator_storage_home = mt->in_generator && existing &&
            existing->from_env && existing->from_hoist;
        if (existing && (existing_in_target_scope || generator_storage_home)) {
            // Generator locals are predeclared in the function scope solely to
            // reserve suspend/resume storage. Their later lexical declaration
            // must retain that env home; ordinary outer lexical/capture bindings
            // remain excluded so real shadows cannot inherit unrelated state.
            if (existing->from_env) {
                entry.var.from_env = true;
                entry.var.env_slot = existing->env_slot;
                entry.var.env_reg = existing->env_reg;
            }
            // Preserve const flag from TDZ init
            if (existing->is_const) {
                entry.var.is_const = true;
            }
            // Preserve let/const flag from TDZ init
            if (existing->is_let_const) {
                entry.var.is_let_const = true;
            }
            if (existing->from_catch_param) {
                entry.var.from_catch_param = true;
            }
            entry.var.binding_start = existing->binding_start;
            entry.var.binding_end = existing->binding_end;
            entry.var.gc_home_id = existing->gc_home_id;
            // The canonical root slot follows the semantic binding across
            // register and representation changes; lexical shadows do not
            // enter this same-scope preservation path.
            entry.var.root_slot = existing->root_slot;
            if (existing->in_scope_env) {
                entry.var.in_scope_env = true;
                entry.var.scope_env_slot = existing->scope_env_slot;
                entry.var.scope_env_reg = existing->scope_env_reg;
            }
        }
    }

    if (mt->em.frame.active && entry.var.gc_home_id <= 0) {
        // Canonical homes belong to semantic bindings. Reassignments in the
        // same scope retain the ID; lexical shadows receive a fresh identity.
        entry.var.gc_home_id = em_gc_new_home(&mt->em);
    }

    struct hashmap* target_scope = jm_var_scope_at(mt, target_depth);
    if (!target_scope) return;
    hashmap_set(target_scope, &entry);
    JsVarScopeEntry key;
    memset(&key, 0, sizeof(key));
    key.name = name;
    JsVarScopeEntry* inserted = (JsVarScopeEntry*)hashmap_get(
        target_scope, &key);
    if (inserted) {
        jm_update_gc_root_slot(mt, &inserted->var);
    }
}

JsMirVarEntry* jm_find_var(JsMirTranspiler* mt, const char* name) {
    if (!mt || !name) return NULL;
    JsVarScopeEntry key;
    memset(&key, 0, sizeof(key));
    key.name = name;
    for (int i = mt->scope_depth; i >= 0; i--) {
        struct hashmap* scope = jm_var_scope_at(mt, i);
        if (!scope) continue;
        JsVarScopeEntry* found = (JsVarScopeEntry*)hashmap_get(scope, &key);
        if (found) return &found->var;
    }
    return NULL;
}

// ============================================================================
// Capture analysis for closures
// ============================================================================

// Simple string set using hashmap
