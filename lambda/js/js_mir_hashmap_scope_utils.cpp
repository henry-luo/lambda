#include "js_mir_internal.hpp"
#include "js_exec_profile.h"
#include <limits.h>

static bool jm_lookup_import_metadata(const char* name,
        JitImportMetadata* metadata) {
    return jit_import_get_metadata(name, metadata);
}

// ============================================================================
// Hashmap helpers
// ============================================================================

int js_var_scope_cmp(const void *a, const void *b, void *udata) {
    return em_var_scope_cmp(a, b, udata);
}
uint64_t js_var_scope_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    return em_var_scope_hash(item, seed0, seed1);
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
    snprintf(lookup.name, sizeof(lookup.name), "%s", capture->name);
    JsModuleConstEntry* entry =
        (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup);
    return entry && entry->const_type == MCONST_MODVAR;
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
    mt->em.note_mir_call = js_exec_profile_note_mir_call;
    mt->em.call_owner = mt;
    mt->em.root_call_value = js_call_root_value;
    mt->em.convert_rep = jm_convert_rep;
    mt->em.lookup_import_metadata = jm_lookup_import_metadata;
    mt->is_module = is_module;
    mt->filename = filename;
    mt->em.import_cache = em_import_cache_new(import_capacity);
    mt->local_funcs = hashmap_new(sizeof(JsLocalFuncEntry), local_func_capacity, 0, 0,
        js_local_func_hash, js_local_func_cmp, NULL, NULL);
    mt->var_scopes[0] = em_var_scope_new(var_scope_capacity);
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

static void jm_emit_raw(JsMirTranspiler* mt, MIR_insn_t insn) {
    em_emit_insn(&mt->em, insn);
}

static MIR_reg_t jm_load_side_stack_runtime(JsMirTranspiler* mt) {
    JsMirImportEntry* rt = jm_ensure_import(mt, "_lambda_rt", MIR_T_I64, 0, NULL, 0);
    MIR_reg_t address = jm_new_reg(mt, "side_rt_addr", MIR_T_I64);
    jm_emit_raw(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, address), MIR_new_ref_op(mt->ctx, rt->import)));
    MIR_reg_t runtime = jm_new_reg(mt, "side_rt", MIR_T_I64);
    jm_emit_raw(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, runtime),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, address, 0, 1)));
    return runtime;
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
    jm_emit_raw(mt, MIR_new_insn(mt->ctx, MIR_MOV,
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
    MIR_reg_t runtime = jm_load_side_stack_runtime(mt);
    MIR_reg_t top = jm_new_reg(mt, "js_root_top_backedge", MIR_T_I64);
    jm_emit_raw(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, top),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, offsetof(Context, side_root_top),
            runtime, 0, 1)));
    MIR_insn_t reload = MIR_new_insn(mt->ctx, MIR_SUB,
        MIR_new_reg_op(mt->ctx, mt->em.frame.root_base),
        MIR_new_reg_op(mt->ctx, top), MIR_new_int_op(mt->ctx, 0));
    jm_emit_raw(mt, reload);
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
        MIR_reg_t runtime_reg) {
    if (!mt) return;
    // Poll dedup is local to one MIR function; retaining another function's
    // tail pointer would suppress a required first exception observation.
    mt->last_exception_poll_branch = NULL;
    mt->last_exception_poll_target = 0;
    mt->arg_stack_scope = NULL;
    em_frame_dispose(&mt->em);
    mt->em.frame.return_type = return_type;
    mt->em.frame.item_return = item_return;
    mt->em.frame.scalar_return_mode = scalar_return_mode;
    mt->em.frame.runtime = runtime_reg ? runtime_reg : jm_load_side_stack_runtime(mt);
    mt->em.frame.root_base = jm_new_reg(mt, "js_root_frame", MIR_T_I64);
    mt->em.frame.number_base = jm_new_reg(mt, "js_number_frame", MIR_T_I64);
    mt->em.frame.anchor = jm_new_label(mt);
    mt->em.frame.return_label = jm_new_label(mt);
    mt->em.frame.return_reg = jm_new_reg(mt, "js_return_value", return_type);
    mt->em.frame.plan.entry_kind = FN_ENTRY_PUBLIC_WRAPPER;
    mt->em.frame.plan.entry_mode = MIR_ENTRY_CHECKED;
    mt->em.frame.active = true;
    jm_emit_label(mt, mt->em.frame.anchor);
}

static void jm_finalize_side_root_prologue(JsMirTranspiler* mt) {
    if (!mt) return;
    em_finalize_frame_prologue(&mt->em, mt->em.frame.plan.entry_mode,
        offsetof(Context, side_root_top), offsetof(Context, side_root_limit),
        offsetof(Context, side_number_top), offsetof(Context, side_number_limit),
        offsetof(Context, side_root_commit_limit));
    jm_call_void_1(mt, "lambda_stack_overflow_error", MIR_T_P,
        MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)"js-side-stack"));
    MIR_op_t failure = mt->em.frame.return_type == MIR_T_D
        ? MIR_new_double_op(mt->ctx, 0.0)
        : mt->em.frame.item_return
            ? MIR_new_uint_op(mt->ctx, ITEM_NULL_VAL)
            : MIR_new_int_op(mt->ctx, 0);
    jm_emit_raw(mt, MIR_new_ret_insn(mt->ctx, 1, failure));
}

static void jm_epilogue_call_void(JsMirTranspiler* mt, const char* name,
        int nargs, MIR_type_t* arg_types, MIR_op_t* arg_ops) {
    em_call_void_with_args(&mt->em, name, nargs, arg_types, arg_ops, true);
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
        jm_epilogue_call_void(mt, "js_env_rehome_scalars", 1, &arg_type, &arg);
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
            jm_emit_raw(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, mt->em.frame.return_reg),
                MIR_new_reg_op(mt->ctx, rehomed)));
        }
    } else {
        em_store_frame_top(&mt->em, mt->em.frame.runtime,
            offsetof(Context, side_number_top), mt->em.frame.number_base);
    }
    if (mt->em.frame.root_slot_count > 0) {
        em_store_frame_top(&mt->em, mt->em.frame.runtime,
            offsetof(Context, side_root_top), mt->em.frame.root_base);
    }
    jm_emit_raw(mt, MIR_new_ret_insn(mt->ctx, 1,
        MIR_new_reg_op(mt->ctx, mt->em.frame.return_reg)));
    jm_optimize_exception_polls(mt);
    jm_finalize_write_back_roots(mt);
    em_finalize_scalar_homes(&mt->em);
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
            jm_emit_raw(mt, insn);
            return;
        }
        MIR_insn_code_t move = mt->em.frame.return_type == MIR_T_D ? MIR_DMOV : MIR_MOV;
        jm_emit_raw(mt, MIR_new_insn(mt->ctx, move,
            MIR_new_reg_op(mt->ctx, mt->em.frame.return_reg), insn->ops[0]));
        jm_emit_raw(mt, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, mt->em.frame.return_label)));
        _MIR_free_insn(mt->ctx, insn);
        return;
    }
    jm_emit_raw(mt, insn);
}

void jm_emit_label(JsMirTranspiler* mt, MIR_label_t label) {
    if (!label) {
        log_error("js-mir: attempt to emit NULL label — skipping");
        return;
    }
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
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, state->saved_var_reg),
                MIR_new_reg_op(mt->ctx, state->var_reg)));
        }
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, state->var_reg),
            MIR_new_reg_op(mt->ctx, value)));
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
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, state->saved_scope_env_value_reg),
                MIR_new_mem_op(mt->ctx, MIR_T_I64,
                    scope_slot * (int)sizeof(uint64_t), scope_reg, 0, 1)));
        }
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_mem_op(mt->ctx, MIR_T_I64,
                scope_slot * (int)sizeof(uint64_t), scope_reg, 0, 1),
            MIR_new_reg_op(mt->ctx, value)));
    }
}

void jm_emit_end_lexical_this_rebind(JsMirTranspiler* mt,
        const JsMirLexicalThisRebind* state) {
    if (!mt || !state) return;
    if (state->restore_binding) {
        if (state->scope_env_reg != 0 && state->scope_env_slot >= 0 &&
                state->saved_scope_env_value_reg != 0) {
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64,
                    state->scope_env_slot * (int)sizeof(uint64_t),
                    state->scope_env_reg, 0, 1),
                MIR_new_reg_op(mt->ctx, state->saved_scope_env_value_reg)));
        }
        if (state->var_reg != 0 && state->saved_var_reg != 0) {
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, state->var_reg),
                MIR_new_reg_op(mt->ctx, state->saved_var_reg)));
        }
    }
    mt->force_closure_env_copy = state->saved_force_closure_env_copy;
}

// Eval completion value: reset completion register to undefined.
// Called at the point where the ES spec says "Let V = undefined" for compound statements.
void jm_eval_cptn_reset(JsMirTranspiler* mt) {
    if (mt->eval_completion_reg) {
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, mt->eval_completion_reg),
            MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED)));
    }
}

// v11: push loop labels, consuming any pending label from a labeled statement
void jm_push_loop_labels(JsMirTranspiler* mt, MIR_label_t continue_label, MIR_label_t break_label) {
    if (mt->loop_depth < 32) {
        mt->loop_stack[mt->loop_depth].continue_label = continue_label;
        mt->loop_stack[mt->loop_depth].break_label = break_label;
        mt->loop_stack[mt->loop_depth].iterator_to_close = 0;
        mt->loop_stack[mt->loop_depth].label_name = mt->pending_label_name;
        mt->loop_stack[mt->loop_depth].label_name_len = mt->pending_label_len;
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
    if (mt->scope_depth >= 63) { log_error("js-mir: scope overflow"); return; }
    mt->scope_depth++;
    mt->var_scopes[mt->scope_depth] = em_var_scope_new(16);
}

// v20: Find the formal parameter index for a variable name in arguments aliasing.
// Returns -1 if not found or arguments aliasing is not active.
int jm_arguments_param_index(JsMirTranspiler* mt, const char* vname) {
    if (mt->arguments_reg == 0 || mt->arguments_param_count <= 0) return -1;
    for (int i = 0; i < mt->arguments_param_count; i++) {
        if (strcmp(mt->arguments_param_names[i], vname) == 0) return i;
    }
    return -1;
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
    hashmap_free(mt->var_scopes[mt->scope_depth]);
    mt->var_scopes[mt->scope_depth] = NULL;
    mt->scope_depth--;
}

JsMirVarEntry* jm_find_var(JsMirTranspiler* mt, const char* name);

JsMirVarEntry* jm_install_fresh_var_entry(JsMirTranspiler* mt, int depth,
        JsVarScopeEntry* entry) {
    if (!mt || !entry || depth < 0 || depth >= 64 || !mt->var_scopes[depth]) {
        return NULL;
    }
    // Direct scope insertion used to leave root_slot at memset's zero, which
    // falsely looked rooted and bypassed semantic-home registration.
    entry->var.root_slot = -1;
    entry->var.gc_home_id = mt->em.frame.active
        ? em_gc_new_home(&mt->em) : 0;
    hashmap_set(mt->var_scopes[depth], entry);

    JsVarScopeEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", entry->name);
    JsVarScopeEntry* inserted = (JsVarScopeEntry*)hashmap_get(
        mt->var_scopes[depth], &key);
    if (!inserted) return NULL;
    jm_update_gc_root_slot(mt, &inserted->var);
    return &inserted->var;
}

void jm_set_var(JsMirTranspiler* mt, const char* name, MIR_reg_t reg,
                       MIR_type_t mir_type , TypeId type_id ) {
    int target_depth = (mt->var_hoist_depth >= 0) ? mt->var_hoist_depth : mt->scope_depth;
    JsVarScopeEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
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
        if (target_depth >= 0 && mt->var_scopes[target_depth]) {
            JsVarScopeEntry key;
            memset(&key, 0, sizeof(key));
            snprintf(key.name, sizeof(key.name), "%s", name);
            JsVarScopeEntry* found = (JsVarScopeEntry*)hashmap_get(mt->var_scopes[target_depth], &key);
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

    hashmap_set(mt->var_scopes[target_depth], &entry);
    JsVarScopeEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    JsVarScopeEntry* inserted = (JsVarScopeEntry*)hashmap_get(
        mt->var_scopes[target_depth], &key);
    if (inserted) {
        jm_update_gc_root_slot(mt, &inserted->var);
    }
}

JsMirVarEntry* jm_find_var(JsMirTranspiler* mt, const char* name) {
    JsVarScopeEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    for (int i = mt->scope_depth; i >= 0; i--) {
        if (!mt->var_scopes[i]) continue;
        JsVarScopeEntry* found = (JsVarScopeEntry*)hashmap_get(mt->var_scopes[i], &key);
        if (found) return &found->var;
    }
    return NULL;
}

// ============================================================================
// Capture analysis for closures
// ============================================================================

// Simple string set using hashmap
