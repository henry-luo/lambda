#include "js_mir_internal.hpp"

// ============================================================================
// Hashmap helpers
// ============================================================================

int js_import_cache_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(((JsImportCacheEntry*)a)->name, ((JsImportCacheEntry*)b)->name);
}
uint64_t js_import_cache_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((JsImportCacheEntry*)item)->name,
        strlen(((JsImportCacheEntry*)item)->name), seed0, seed1);
}

int js_var_scope_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(((JsVarScopeEntry*)a)->name, ((JsVarScopeEntry*)b)->name);
}
uint64_t js_var_scope_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((JsVarScopeEntry*)item)->name,
        strlen(((JsVarScopeEntry*)item)->name), seed0, seed1);
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
    mt->is_module = is_module;
    mt->filename = filename;
    mt->import_cache = hashmap_new(sizeof(JsImportCacheEntry), import_capacity, 0, 0,
        js_import_cache_hash, js_import_cache_cmp, NULL, NULL);
    mt->local_funcs = hashmap_new(sizeof(JsLocalFuncEntry), local_func_capacity, 0, 0,
        js_local_func_hash, js_local_func_cmp, NULL, NULL);
    mt->var_scopes[0] = hashmap_new(sizeof(JsVarScopeEntry), var_scope_capacity, 0, 0,
        js_var_scope_hash, js_var_scope_cmp, NULL, NULL);
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
    return mir_new_numbered_reg(mt->ctx, mt->current_func, &mt->reg_counter,
                                prefix, type);
}

MIR_label_t jm_new_label(JsMirTranspiler* mt) {
    return mir_new_emit_label(mt->ctx);
}

// Tune6 §3.3: per-opcode emission histogram (env-gated, zero cost when off) to
// find which MIR the lowering emits the most of — drives helper extraction.
#define JM_OPCODE_HIST_SIZE 1024
static long g_jm_opcode_hist[JM_OPCODE_HIST_SIZE];
static bool g_jm_opcode_hist_enabled = false;

void jm_opcode_hist_set_enabled(int enabled) { g_jm_opcode_hist_enabled = (enabled != 0); }
void jm_opcode_hist_reset(void) { memset(g_jm_opcode_hist, 0, sizeof(g_jm_opcode_hist)); }

void jm_opcode_hist_dump(MIR_context_t ctx, const char* label) {
    // collect non-zero, sort by count desc, print top 25
    int idx[JM_OPCODE_HIST_SIZE]; int n = 0;
    long total = 0;
    for (int i = 0; i < JM_OPCODE_HIST_SIZE; i++) {
        if (g_jm_opcode_hist[i] > 0) { idx[n++] = i; total += g_jm_opcode_hist[i]; }
    }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (g_jm_opcode_hist[idx[j]] > g_jm_opcode_hist[idx[i]]) { int t = idx[i]; idx[i] = idx[j]; idx[j] = t; }
    printf("JS_MIR_OPCODE_HIST label=%s total_emitted=%ld distinct_opcodes=%d\n", label ? label : "", total, n); // PRINTF_OK: env-gated JIT opcode-histogram dump.
    int top = n < 25 ? n : 25;
    for (int i = 0; i < top; i++) {
        const char* name = MIR_insn_name(ctx, (MIR_insn_code_t)idx[i]);
        printf("  %-12s %10ld  %5.1f%%\n", name ? name : "?", g_jm_opcode_hist[idx[i]], // PRINTF_OK: env-gated JIT histogram row.
               total ? 100.0 * (double)g_jm_opcode_hist[idx[i]] / (double)total : 0.0);
    }
    fflush(stdout);
}

void jm_emit(JsMirTranspiler* mt, MIR_insn_t insn) {
    if (g_jm_opcode_hist_enabled) {
        unsigned c = (unsigned)insn->code;
        if (c < JM_OPCODE_HIST_SIZE) g_jm_opcode_hist[c]++;
    }
    mir_append_emit_insn(mt->ctx, mt->current_func_item, insn);
}

void jm_emit_label(JsMirTranspiler* mt, MIR_label_t label) {
    if (!label) {
        log_error("js-mir: attempt to emit NULL label — skipping");
        return;
    }
    mir_append_emit_label(mt->ctx, mt->current_func_item, label);
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
    mt->var_scopes[mt->scope_depth] = hashmap_new(sizeof(JsVarScopeEntry), 16, 0, 0,
        js_var_scope_hash, js_var_scope_cmp, NULL, NULL);
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

void jm_set_var(JsMirTranspiler* mt, const char* name, MIR_reg_t reg,
                       MIR_type_t mir_type , TypeId type_id ) {
    int target_depth = (mt->var_hoist_depth >= 0) ? mt->var_hoist_depth : mt->scope_depth;
    JsVarScopeEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.var.reg = reg;
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
        if (existing) {
            // Preserve env slot info from predeclared TDZ/hoisted bindings so
            // closures keep observing the same lexical cell after initialization.
            if (existing->from_env &&
                (existing_in_target_scope || existing->tdz_active || existing->from_hoist)) {
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
            if (existing->in_scope_env &&
                (existing_in_target_scope || existing->tdz_active || existing->from_hoist)) {
                entry.var.in_scope_env = true;
                entry.var.scope_env_slot = existing->scope_env_slot;
                entry.var.scope_env_reg = existing->scope_env_reg;
            }
        }
    }

    hashmap_set(mt->var_scopes[target_depth], &entry);
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
