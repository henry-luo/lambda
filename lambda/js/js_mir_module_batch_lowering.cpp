#include "js_mir_internal.hpp"

#include <limits.h>
#include "../../lib/file.h"
#include "../runtime/lambda-error.h"
#include "../jube/jube_registry.h"

extern "C" void js_dynfunc_cache_reset(void);

static NameEntry* jm_annex_b_publish_binding(NameEntry* binding) {
    if (binding && binding->annex_b_outer_binding) {
        return binding->annex_b_outer_binding;
    }
    return binding;
}

static NameEntry* jm_hoisted_var_binding(const JsNameSetEntry* entry) {
    if (!entry || !entry->entry) return NULL;
    // A sloppy block function owns a lexical declaration cell, while Annex B
    // publishes through its resolver-linked var companion (D8.2.5).
    return entry->from_func_decl
        ? jm_annex_b_publish_binding(entry->entry) : entry->entry;
}

static JsModuleConstEntry* jm_register_module_var(JsMirTranspiler* mt,
        const char* name, int var_kind, TypeId modvar_type,
        bool is_nested_func_hoist, const char* log_kind,
        NameEntry* binding = NULL) {
    if (!mt || !mt->module_consts || !name ||
            mt->module_var_count >= JS_MAX_MODULE_VARS) return NULL;
    JsModuleConstEntry lookup;
    memset(&lookup, 0, sizeof(lookup));
    lookup.name = jm_persist_name(name);
    JsModuleConstEntry* existing = (JsModuleConstEntry*)hashmap_get(
        mt->module_consts, &lookup);
    if (existing) {
        // A preliminary var-hoist scan is spelling-keyed, but its entry can
        // describe a nested same-named declaration. The later direct source
        // declaration is the authoritative binding for this module slot.
        // Direct IIFE declaration promotion has already selected the function
        // declaration binding. A later Annex B scan names its distinct outer
        // companion; replacing the promoted binding would make ordinary
        // reads miss the slot and fall through to global lookup (D8.2.5).
        if (binding && !existing->is_iife_func_decl) existing->binding = binding;
        return existing;
    }

    JsModuleConstEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.name = lookup.name;
    entry.binding = binding;
    entry.const_type = MCONST_MODVAR;
    entry.int_val = mt->module_var_count++;
    entry.var_kind = var_kind;
    entry.modvar_type = modvar_type;
    entry.is_nested_func_hoist = is_nested_func_hoist;
    hashmap_set(mt->module_consts, &entry);
    log_debug("js-mir: %s '%s' → module_var[%d]", log_kind ? log_kind : "module var",
        entry.name, (int)entry.int_val);
    return (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup);
}

static void jm_emit_function_decl_runtime_bindings(JsMirTranspiler* mt,
        JsFunctionNode* fn, MIR_reg_t var_reg) {
    // Function declarations share module persistence and sloppy-eval export rules regardless of closure shape.
    NameEntry* publish_binding = fn
        ? jm_annex_b_publish_binding(fn->entry) : NULL;
    JsModuleConstEntry* pmc = publish_binding
        ? jm_find_module_const_by_binding(mt, publish_binding) : NULL;
    if (pmc && pmc->const_type == MCONST_MODVAR) {
        jm_store_module_var(mt, (uint32_t)pmc->int_val, var_reg);
    }
    if (mt->is_eval_direct && !mt->is_global_strict) {
        MIR_reg_t fk = jm_box_property_name_literal(mt, fn->name->chars, fn->name->len);
        MIR_reg_t eval_env_active = jm_call_0(mt, "js_eval_env_is_active", MIR_T_I64);
        MIR_label_t global_export = jm_new_label(mt);
        MIR_label_t export_done = jm_new_label(mt);
        jm_emit_branch(mt, MIR_BF, global_export, eval_env_active);
        jm_callr_void_2(mt, "js_eval_local_export_var", fk, var_reg);
        MIR_reg_t evalscript_local_active = jm_call_0(mt, "js_262_eval_script_is_active", MIR_T_I64);
        MIR_label_t skip_evalscript_global = jm_new_label(mt);
        jm_emit_branch(mt, MIR_BF, skip_evalscript_global, evalscript_local_active);
        // evalScript executes as a Script, so its function binding is global even inside an eval frame.
        jm_call_void_3(mt, "js_define_global_property_v",
            MIR_T_I64, MIR_new_int_op(mt->ctx, 2),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, fk),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, var_reg));
        jm_emit_label(mt, skip_evalscript_global);
        jm_emit_jmp(mt, export_done);
        jm_emit_label(mt, global_export);
        MIR_reg_t evalscript_active = jm_call_0(mt, "js_262_eval_script_is_active", MIR_T_I64);
        MIR_label_t ordinary_eval_export = jm_new_label(mt);
        MIR_label_t global_define_done = jm_new_label(mt);
        jm_emit_branch(mt, MIR_BF, ordinary_eval_export, evalscript_active);
        // $262.evalScript creates non-configurable globals; direct eval keeps configurable bindings.
        jm_call_void_3(mt, "js_define_global_property_v",
            MIR_T_I64, MIR_new_int_op(mt->ctx, 2),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, fk),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, var_reg));
        jm_emit_jmp(mt, global_define_done);
        jm_emit_label(mt, ordinary_eval_export);
        jm_call_void_3(mt, "js_set_global_property",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, fk),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, var_reg),
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
        jm_emit_label(mt, global_define_done);
        jm_emit_label(mt, export_done);
    }
    if (!mt->is_module && !mt->is_eval_direct) {
        MIR_reg_t fk = jm_box_property_name_literal(mt, fn->name->chars, fn->name->len);
        jm_call_void_3(mt, "js_define_global_property_v",
            MIR_T_I64, MIR_new_int_op(mt->ctx, 2),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, fk),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, var_reg));
    }
}

static bool jm_is_undefined_module_var_batch_entry(JsMirTranspiler* mt,
        const JsModuleConstEntry* entry, int preamble_var_limit,
        bool define_global_var_properties) {
    if (!mt || !entry || mt->is_eval_direct ||
            entry->const_type != MCONST_MODVAR ||
            entry->var_kind != JS_VAR_VAR ||
            entry->is_iife_func_decl ||
            (int)entry->int_val < preamble_var_limit) {
        return false;
    }
    bool entry_defines_global = !mt->is_module && !entry->is_iife_var;
    return entry_defines_global == define_global_var_properties;
}

static bool jm_emit_undefined_module_var_batch(JsMirTranspiler* mt,
        int preamble_var_limit, bool define_global_var_properties) {
    if (!mt || !mt->module_consts || mt->is_eval_direct) return true;
    int count = 0;
    size_t count_iter = 0;
    void* count_item = NULL;
    while (hashmap_iter(mt->module_consts, &count_iter, &count_item)) {
        JsModuleConstEntry* entry = (JsModuleConstEntry*)count_item;
        if (jm_is_undefined_module_var_batch_entry(mt, entry,
                preamble_var_limit, define_global_var_properties)) {
            count++;
        }
    }
    if (count == 0) return true;

    int* indices = (int*)mem_alloc(sizeof(int) * (size_t)count, MEM_CAT_TEMP);
    uint32_t* module_name_indices = define_global_var_properties
        ? (uint32_t*)mem_alloc(sizeof(uint32_t) * (size_t)count, MEM_CAT_TEMP)
        : NULL;
    NameId* direct_name_ids = define_global_var_properties
        ? (NameId*)mem_alloc(sizeof(NameId) * (size_t)count, MEM_CAT_TEMP)
        : NULL;
    if (!indices || (define_global_var_properties &&
            (!module_name_indices || !direct_name_ids))) {
        mem_free(indices);
        mem_free(module_name_indices);
        mem_free(direct_name_ids);
        return false;
    }

    int index = 0;
    size_t fill_iter = 0;
    void* fill_item = NULL;
    while (hashmap_iter(mt->module_consts, &fill_iter, &fill_item)) {
        JsModuleConstEntry* entry = (JsModuleConstEntry*)fill_item;
        if (!jm_is_undefined_module_var_batch_entry(mt, entry,
                preamble_var_limit, define_global_var_properties)) {
            continue;
        }
        indices[index] = (int)entry->int_val;
        if (define_global_var_properties) {
            const char* js_name = entry->name;
            if (strncmp(js_name, "_js_", 4) == 0) js_name += 4;
            uint32_t name_len = (uint32_t)strlen(js_name);
            NameId direct_name_id = well_known_name_id({js_name, name_len});
            direct_name_ids[index] = direct_name_id;
            module_name_indices[index] = direct_name_id == NAME_ID_NONE
                ? jm_module_name_index(mt, js_name, name_len) : UINT32_MAX;
        }
        index++;
    }

    int table_id = mt->generated_data_counter++;
    char indices_name[64];
    char module_names_name[64];
    char direct_names_name[64];
    snprintf(indices_name, sizeof(indices_name), "js_undef_indices_%d", table_id);
    snprintf(module_names_name, sizeof(module_names_name), "js_undef_module_names_%d", table_id);
    snprintf(direct_names_name, sizeof(direct_names_name), "js_undef_direct_names_%d", table_id);
    MIR_item_t indices_data = MIR_new_data(mt->ctx, indices_name, MIR_T_I32,
        (size_t)count, indices);
    MIR_item_t module_names_data = define_global_var_properties
        ? MIR_new_data(mt->ctx, module_names_name, MIR_T_U32, (size_t)count,
            module_name_indices)
        : NULL;
    MIR_item_t direct_names_data = define_global_var_properties
        ? MIR_new_data(mt->ctx, direct_names_name, MIR_T_U32, (size_t)count,
            direct_name_ids)
        : NULL;
    mem_free(indices);
    mem_free(module_name_indices);
    mem_free(direct_name_ids);

    // D8.4.3: emitting one table-driven instantiation call avoids four MIR
    // call sites per global `var` while preserving the runtime binding lane.
    jm_call_void_5(mt, "js_init_module_vars_undefined_bulk",
        MIR_T_P, MIR_new_ref_op(mt->ctx, indices_data),
        MIR_T_P, module_names_data
            ? MIR_new_ref_op(mt->ctx, module_names_data)
            : MIR_new_int_op(mt->ctx, 0),
        MIR_T_P, direct_names_data
            ? MIR_new_ref_op(mt->ctx, direct_names_data)
            : MIR_new_int_op(mt->ctx, 0),
        MIR_T_I64, MIR_new_int_op(mt->ctx, count),
        MIR_T_I64, MIR_new_int_op(mt->ctx,
            define_global_var_properties ? 1 : 0));
    return true;
}

// ============================================================================
// ES Module support: deferred MIR cleanup and path resolution
// ============================================================================

static int js_debug_func_name_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(*(const char**)a, *(const char**)b);
}

static uint64_t js_debug_func_name_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const char* name = *(const char**)item;
    return hashmap_sip(name, strlen(name), seed0, seed1);
}

static void js_debug_func_name_entry_free(void* item) {
    char** entry = (char**)item;
    if (entry[0]) mem_free(entry[0]);
    if (entry[1]) mem_free(entry[1]);
}

static char* js_debug_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)mem_alloc(len, MEM_CAT_JS_RUNTIME);
    if (copy) memcpy(copy, s, len);
    return copy;
}

static char* js_debug_display_name(JsFuncCollected* fc) {
    if (!fc) return js_debug_strdup("<anonymous>");
    if (fc->node && fc->node->name && fc->node->name->len > 0) {
        int len = (int)fc->node->name->len;
        char* name = (char*)mem_alloc((size_t)len + 1, MEM_CAT_JS_RUNTIME);
        if (!name) return NULL;
        memcpy(name, fc->node->name->chars, (size_t)len);
        name[len] = '\0';
        return name;
    }
    const char* raw = fc->name;
    if (!raw) return js_debug_strdup("<anonymous>");
    if (strncmp(raw, "_js_", 4) == 0) raw += 4;
    int len = (int)strlen(raw);
    int end = len;
    while (end > 0 && raw[end - 1] >= '0' && raw[end - 1] <= '9') end--;
    if (end > 0 && end < len && raw[end - 1] == '_') len = end - 1;
    if (len <= 0) return js_debug_strdup("<anonymous>");
    char* name = (char*)mem_alloc((size_t)len + 1, MEM_CAT_JS_RUNTIME);
    if (!name) return NULL;
    memcpy(name, raw, (size_t)len);
    name[len] = '\0';
    return name;
}

static void js_debug_map_set(struct hashmap* map, const char* mir_name, const char* display_name) {
    if (!map || !mir_name || !display_name) return;
    char* entry[2] = { js_debug_strdup(mir_name), js_debug_strdup(display_name) };
    if (!entry[0] || !entry[1]) {
        if (entry[0]) mem_free(entry[0]);
        if (entry[1]) mem_free(entry[1]);
        return;
    }
    hashmap_set(map, entry);
}

void* jm_build_js_debug_info(JsMirTranspiler* mt, const char* filename) {
    (void)filename;
    if (!mt || !mt->ctx) return NULL;
    struct hashmap* name_map = hashmap_new(sizeof(char*[2]), 64, 0, 0,
        js_debug_func_name_hash, js_debug_func_name_cmp, js_debug_func_name_entry_free, NULL);
    if (!name_map) return build_debug_info_table(mt->ctx, NULL);

    for (int i = 0; i < mt->func_count; i++) {
        JsFuncCollected* fc = &mt->func_entries[i];
        char* display_name = js_debug_display_name(fc);
        if (!display_name) continue;
        js_debug_map_set(name_map, fc->name, display_name);
        if (JM_JS_FACT(fc, native_return_kind) != NATIVE_RETURN_NONE) {
            char native_name[160];
            snprintf(native_name, sizeof(native_name), "%s_n", fc->name);
            js_debug_map_set(name_map, native_name, display_name);
        }
        mem_free(display_name);
    }
    js_debug_map_set(name_map, "js_main", "<module>");

    void* debug_info = build_debug_info_table(mt->ctx, name_map);
    hashmap_free(name_map);
    return debug_info;
}

// Track compilation ownership for timeout recovery on the owning JS realm.
// Compilation is cold; this capsule is never consulted by generated JS code.
JsMirCompileRecoveryState* jm_compile_recovery_state_ensure(void) {
    if (!js_active_runtime_state) return NULL;
    if (!js_runtime_state.mir_compile_recovery_state) {
        js_runtime_state.mir_compile_recovery_state = mem_calloc(1,
            sizeof(JsMirCompileRecoveryState), MEM_CAT_JS_RUNTIME);
    }
    return (JsMirCompileRecoveryState*)js_runtime_state.mir_compile_recovery_state;
}

JsMirCompileRecoveryState* jm_compile_recovery_state_current(void) {
    return js_active_runtime_state ?
        (JsMirCompileRecoveryState*)js_runtime_state.mir_compile_recovery_state : NULL;
}

static JsMirCompileRecoveryState* jm_compile_recovery_state_required(void) {
    JsMirCompileRecoveryState* state = jm_compile_recovery_state_ensure();
    if (!state) {
        log_error("js-mir-recovery: compilation without a bound JS realm");
    }
    return state;
}

static void jm_sync_active_js_transpile_top(JsMirCompileRecoveryState* state) {
    if (!state) return;
    if (state->count <= 0) {
        g_active_js_transpiler = NULL;
        g_active_mir_transpiler = NULL;
        g_active_js_owned_source = NULL;
        return;
    }
    ActiveJsTranspileOwner* top = &state->stack[state->count - 1];
    g_active_js_transpiler = top->tp;
    g_active_mir_transpiler = top->mt;
    g_active_js_owned_source = top->owned_source;
}

static void jm_pop_empty_active_js_transpile_owners(JsMirCompileRecoveryState* state) {
    if (!state) return;
    while (state->count > 0) {
        ActiveJsTranspileOwner* top = &state->stack[state->count - 1];
        if (top->tp || top->mt || top->owned_source) break;
        state->count--;
    }
    jm_sync_active_js_transpile_top(state);
}

void jm_track_active_js_transpile(JsTranspiler* tp, JsMirTranspiler* mt, char* owned_source) {
    if (!tp && !mt && !owned_source) return;
    JsMirCompileRecoveryState* state = jm_compile_recovery_state_required();
    if (!state) return;
    if (state->count <= 0) {
        memset(&state->stack[0], 0, sizeof(state->stack[0]));
        state->count = 1;
    }
    ActiveJsTranspileOwner* top = &state->stack[state->count - 1];
    bool starts_nested_owner =
        (tp && top->tp && top->tp != tp) ||
        (mt && top->mt && top->mt != mt) ||
        (owned_source && (top->tp || top->mt) && top->owned_source != owned_source);
    if (starts_nested_owner) {
        if (state->count >= JS_ACTIVE_TRANSPILE_MAX) {
            log_error("js-mir-recovery: active transpile owner stack overflow");
            return;
        }
        top = &state->stack[state->count++];
        memset(top, 0, sizeof(*top));
    }
    if (tp) top->tp = tp;
    if (mt) top->mt = mt;
    if (owned_source) top->owned_source = owned_source;
    jm_sync_active_js_transpile_top(state);
}

void jm_clear_active_js_transpile(JsTranspiler* tp, JsMirTranspiler* mt, char* owned_source) {
    JsMirCompileRecoveryState* state = jm_compile_recovery_state_required();
    if (!state) return;
    for (int i = state->count - 1; i >= 0; i--) {
        ActiveJsTranspileOwner* owner = &state->stack[i];
        bool matched = false;
        if (tp && owner->tp == tp) {
            owner->tp = NULL;
            matched = true;
        }
        if (mt && owner->mt == mt) {
            owner->mt = NULL;
            matched = true;
        }
        if (owned_source && owner->owned_source == owned_source) {
            owner->owned_source = NULL;
            matched = true;
        }
        if (matched) break;
    }
    jm_pop_empty_active_js_transpile_owners(state);
}

static int jm_parent_link_slot_after_captures(JsFuncCollected* child,
        int shared_slot_count) {
    if (!child) return shared_slot_count;
    int link_slot = shared_slot_count;
    // A copied env uses dense capture indices for unremapped captures, so the
    // parent link must live after both dense and remapped slots. Reusing a dense
    // slot turns that captured Item into a pointer during transitive readback.
    if (JM_CAPTURE_COUNT(child) > link_slot) link_slot = JM_CAPTURE_COUNT(child);
    for (int k = 0; k < JM_CAPTURE_COUNT(child); k++) {
        int capture_slot = JM_CAPTURE_ARRAY(child)[k].scope_env_slot;
        if (capture_slot >= link_slot) link_slot = capture_slot + 1;
        int private_slot = JM_CAPTURE_ARRAY(child)[k].private_env_slot;
        if (private_slot >= link_slot) link_slot = private_slot + 1;
    }
    return link_slot;
}

struct LoopBindingProbe {
    JsAstNode* closure_node;
    uint32_t binding_start;
};

static bool jm_ast_loop_owns_binding(JsAstNode* root, JsAstNode* closure_node,
        uint32_t binding_start);

static JsAstNode* jm_loop_binding_body(JsAstNode* node) {
    if (!node) return NULL;
    if (node->node_type == AST_NODE_LOOP) {
        AstLoopControlNode* loop = (AstLoopControlNode*)node;
        return loop->form == LOOP_FORM_FOR_C ? loop->body : NULL;
    }
    if (node->node_type == JS_AST_NODE_FOR_IN_STATEMENT ||
            node->node_type == JS_AST_NODE_FOR_OF_STATEMENT) {
        return ((JsForOfNode*)node)->body;
    }
    return NULL;
}

static bool jm_ast_loop_owns_binding_child(JsAstNode* child, void* data) {
    LoopBindingProbe* probe = (LoopBindingProbe*)data;
    return jm_ast_loop_owns_binding(child, probe->closure_node, probe->binding_start);
}

static bool jm_ast_loop_owns_binding(JsAstNode* root, JsAstNode* closure_node,
        uint32_t binding_start) {
    if (!root || !closure_node) return false;
    JsAstNode* body = jm_loop_binding_body(root);
    if (body) {
        uint32_t body_start = body->source_span.start_byte;
        uint32_t body_end = body->source_span.end_byte;
        uint32_t closure_start = closure_node->source_span.start_byte;
        bool closure_in_body = closure_start >= body_start && closure_start < body_end;
        bool binding_in_body = binding_start >= body_start && binding_start < body_end;
        bool binding_in_header = binding_start >= root->source_span.start_byte &&
            binding_start < body_start;
        if (closure_in_body && (binding_in_body || binding_in_header)) return true;
    }
    if (root != closure_node && (root->node_type == JS_AST_NODE_FUNCTION_DECLARATION ||
            root->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
            root->node_type == JS_AST_NODE_ARROW_FUNCTION ||
            root->node_type == JS_AST_NODE_METHOD_DEFINITION ||
            root->node_type == JS_AST_NODE_CLASS_DECLARATION ||
            root->node_type == JS_AST_NODE_CLASS_EXPRESSION)) return false;
    LoopBindingProbe probe = {closure_node, binding_start};
    return js_ast_any_child(root, jm_ast_loop_owns_binding_child, &probe);
}

static bool jm_capture_is_loop_private_in_root(JsAstNode* root,
        JsFuncCollected* child, const FnCapture* cap) {
    if (!cap || !cap->force_env_capture || !cap->is_let_const ||
            !cap->entry || !cap->entry->node || !child || !child->node) return false;
    // Capture analysis retains the resolved binding, so loop ownership must
    // read its source span directly instead of reparsing the layout-only key.
    uint32_t binding_start = cap->entry->node->source_span.start_byte;
    return jm_ast_loop_owns_binding(root, (JsAstNode*)child->node, binding_start);
}

static bool jm_capture_is_loop_private(JsFuncCollected* child,
        JsFuncCollected* parent, const FnCapture* cap) {
    return parent && parent->node && jm_capture_is_loop_private_in_root(
        parent->node->body, child, cap);
}

static void jm_mark_mixed_loop_parent_link(JsFuncCollected* child, JsFuncCollected* parent) {
    if (!child || !parent || parent->scope_env_count <= 0) return;
    bool has_loop_private = false;
    bool has_shared_parent = false;
    for (int k = 0; k < JM_CAPTURE_COUNT(child); k++) {
        FnCapture* cap = &JM_CAPTURE_ARRAY(child)[k];
        bool loop_private = jm_capture_is_loop_private(child, parent, cap);
        if (loop_private) {
            has_loop_private = true;
        } else if (cap->scope_env_slot >= 0) {
            has_shared_parent = true;
        }
    }
    if (!has_loop_private || !has_shared_parent) {
        return;
    }
    JM_JS_FACT(child, closure_env_has_parent_link) = true;
    JM_JS_FACT(child, closure_env_parent_link_slot) =
        jm_parent_link_slot_after_captures(child, parent->scope_env_count);
    for (int k = 0; k < JM_CAPTURE_COUNT(child); k++) {
        FnCapture* cap = &JM_CAPTURE_ARRAY(child)[k];
        bool loop_private = jm_capture_is_loop_private(child, parent, cap);
        if (!loop_private && cap->scope_env_slot >= 0) {
            cap->grandparent_slot = cap->scope_env_slot;
        }
    }
    log_debug("js-mir: mixed loop closure '%s' keeps shared parent captures via env slot %d",
        child->name, JM_JS_FACT(child, closure_env_parent_link_slot));
}

static bool jm_entry_requires_distinct_lexical_slot(NameEntry* entry) {
    return entry && (entry->is_lexical || entry->is_parameter ||
        (entry->node && entry->node->node_type == JS_AST_NODE_FUNCTION_DECLARATION));
}

static bool jm_parent_owns_capture(JsMirTranspiler* mt,
        JsFuncCollected* parent, const FnCapture* capture) {
    if (!parent || !capture || !capture->name) return false;
    if (capture->entry && jm_entry_is_owned_by_function(parent->node, capture->entry)) {
        JsModuleConstEntry* module_entry = mt && mt->module_consts
            ? jm_find_module_const_by_binding(mt, capture->entry) : NULL;
        if (!((module_entry && module_entry->const_type == MCONST_MODVAR &&
                (module_entry->is_iife_var || module_entry->is_iife_func_decl)) &&
                JM_JS_FACT(parent, is_iife_body))) return true;
    }
    for (int i = 0; i < JM_CAPTURE_COUNT(parent); i++) {
        FnCapture* parent_capture = &JM_CAPTURE_ARRAY(parent)[i];
        if (capture->entry && parent_capture->entry) {
            if (capture->entry == parent_capture->entry) return true;
        } else if (!capture->entry && !parent_capture->entry &&
                strcmp(capture->name, parent_capture->name) == 0) {
            return true;
        }
    }
    return strcmp(capture->name, "_js_arguments") == 0 &&
        JM_JS_FACT(parent, uses_arguments);
}

enum {
    JM_SCOPE_SLOT_HAS_LEXICAL = 1 << 0,
    JM_SCOPE_SLOT_HAS_FUNCTION_VAR = 1 << 1,
    JM_SCOPE_SLOT_HAS_MULTIPLE_LEXICALS = 1 << 2,
};

static JsScope* jm_nearest_function_scope(JsScope* scope) {
    for (; scope; scope = scope->parent) {
        if (scope->kind == SCOPE_KIND_FUNCTION) return scope;
    }
    return NULL;
}

static void jm_note_scope_slot_collision(FnAnalysis* analysis,
        JsScope* scope, JsScope* function_scope, NameEntry* binding) {
    if (!analysis || !scope || !function_scope || !binding || !binding->name) return;
    if (!analysis->js_cached_scope_slot_collisions) {
        analysis->js_cached_scope_slot_collisions = hashmap_new(
            sizeof(JsNameSetEntry), 16, 0, 0, jm_name_hash, jm_name_cmp,
            NULL, NULL);
        if (!analysis->js_cached_scope_slot_collisions) {
            log_error("js-mir: failed to allocate indexed scope-slot collision cache");
            abort();
        }
    }
    JsNameSetEntry key = {};
    key.name = jm_persist_name(jm_var_name(binding->name));
    JsNameSetEntry* cached = (JsNameSetEntry*)hashmap_get(
        analysis->js_cached_scope_slot_collisions, &key);
    if (!cached) {
        hashmap_set(analysis->js_cached_scope_slot_collisions, &key);
        cached = (JsNameSetEntry*)hashmap_get(
            analysis->js_cached_scope_slot_collisions, &key);
        if (!cached) {
            log_error("js-mir: failed to publish indexed scope-slot collision fact");
            abort();
        }
    }
    if (jm_entry_requires_distinct_lexical_slot(binding)) {
        if ((cached->var_kind & JM_SCOPE_SLOT_HAS_LEXICAL) &&
                cached->entry != binding) {
            cached->var_kind |= JM_SCOPE_SLOT_HAS_MULTIPLE_LEXICALS;
        } else {
            cached->entry = binding;
        }
        cached->var_kind |= JM_SCOPE_SLOT_HAS_LEXICAL;
    } else if (scope == function_scope) {
        cached->var_kind |= JM_SCOPE_SLOT_HAS_FUNCTION_VAR;
    }
}

static void jm_prepare_scope_slot_collisions(JsMirTranspiler* mt) {
    if (!mt || mt->scope_slot_collisions_prepared) return;
    mt->scope_slot_collisions_prepared = true;
    if (!mt->tp) return;
    AstIndex* index = &mt->tp->ast_index;
    if (!index->scope_count) return;
    JsFuncCollected** functions_by_scope = (JsFuncCollected**)mem_calloc(
        index->scope_count, sizeof(JsFuncCollected*), MEM_CAT_JS_RUNTIME);
    if (!functions_by_scope) {
        log_error("js-mir: failed to allocate indexed function-scope table");
        abort();
    }
    for (int i = 0; i < mt->func_count; i++) {
        JsFuncCollected* function = &mt->func_entries[i];
        JsScope* scope = function->node ? function->node->vars : NULL;
        if (scope && scope->scope_id < index->scope_count) {
            functions_by_scope[scope->scope_id] = function;
        }
    }
    for (uint32_t scope_id = 0; scope_id < index->scope_count; scope_id++) {
        JsScope* scope = index->scopes[scope_id];
        JsScope* function_scope = jm_nearest_function_scope(scope);
        if (!function_scope || function_scope->scope_id >= index->scope_count) continue;
        JsFuncCollected* function = functions_by_scope[function_scope->scope_id];
        FnAnalysis* analysis = jm_function_analysis(function);
        if (!function || !analysis) continue;
        for (NameEntry* binding = scope->first; binding; binding = binding->next) {
            jm_note_scope_slot_collision(analysis, scope, function_scope, binding);
        }
    }
    mem_free(functions_by_scope);
}

static bool jm_parent_has_slot_identity_collision(JsMirTranspiler* mt,
        JsFuncCollected* parent, const FnCapture* cap) {
    if (!mt || !mt->tp || !parent || !parent->node || !parent->node->vars ||
            !cap || !cap->name) return false;
    // The indexed prepass visits each binding once; repeated closure captures
    // only perform this name-keyed lookup, not another full scope traversal.
    jm_prepare_scope_slot_collisions(mt);
    FnAnalysis* analysis = jm_function_analysis(parent);
    if (!analysis || !analysis->js_cached_scope_slot_collisions) return false;
    JsNameSetEntry key = {};
    key.name = jm_persist_name(cap->name);
    JsNameSetEntry* cached = (JsNameSetEntry*)hashmap_get(
        analysis->js_cached_scope_slot_collisions, &key);
    if (!cached) return false;
    int flags = cached->var_kind;
    return (flags & JM_SCOPE_SLOT_HAS_MULTIPLE_LEXICALS) ||
        ((flags & (JM_SCOPE_SLOT_HAS_LEXICAL | JM_SCOPE_SLOT_HAS_FUNCTION_VAR)) ==
            (JM_SCOPE_SLOT_HAS_LEXICAL | JM_SCOPE_SLOT_HAS_FUNCTION_VAR));
}

static bool jm_capture_binding_starts_after_function(JsFuncCollected* parent, FnCapture* cap) {
    if (!parent || !parent->node || !cap || !cap->entry || !cap->entry->node) return false;
    // A nested closure can outlive a factory before an outer `var` initializer
    // runs; only that source order needs a second, immediate-parent cell link.
    return cap->entry->node->source_span.start_byte >= parent->node->source_span.end_byte;
}

static const char* jm_capture_enclosing_lexical_scope_key(JsAstNode* root,
        JsAstNode* target, const FnCapture* cap) {
    if (!target || !cap || !cap->name || !cap->entry ||
            !cap->entry->is_lexical || !cap->entry->node) return NULL;
    JsScope* scope = NULL;
    switch (target->node_type) {
    case JS_AST_NODE_FUNCTION_DECLARATION:
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
    case JS_AST_NODE_METHOD_DEFINITION:
        scope = ((JsFunctionNode*)target)->vars;
        break;
    case JS_AST_NODE_CLASS_EXPRESSION:
        scope = ((JsClassNode*)target)->expression_scope;
        break;
    default:
        return NULL;
    }
    JsScope* boundary = NULL;
    if (root && root->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
        JsScope* body_scope = ((JsBlockNode*)root)->vars;
        boundary = body_scope ? body_scope->parent : NULL;
    }
    for (scope = scope ? scope->parent : NULL;
            scope && scope != boundary && scope->kind != SCOPE_KIND_GLOBAL &&
            scope->kind != SCOPE_KIND_MODULE; scope = scope->parent) {
        if (!scope->is_function_name_scope && scope == cap->entry->scope) {
            AstNode* binding = cap->entry->node;
            return jm_format_name("%s@%u:%u", cap->name,
                binding->source_span.start_byte, binding->source_span.end_byte);
        }
    }
    return NULL;
}

static const char* jm_capture_scope_env_slot_key(JsMirTranspiler* mt,
        JsFuncCollected* parent, JsFuncCollected* child,
        FnCapture* cap) {
    if (!cap) return "";
    // Loop-private captures share their ordinary slot name with loop writeback.
    // Only class methods need a source-keyed forced capture to distinguish an
    // IIFE lexical from its promoted same-named module binding; keying every
    // forced capture disconnects ordinary closures from their parent writes.
    bool needs_binding_key = jm_parent_has_slot_identity_collision(mt, parent, cap) ||
        (cap->force_env_capture && child && JM_JS_FACT(child, is_class_method));
    if (needs_binding_key) {
        if (!cap->scope_env_key || !cap->scope_env_key[0] ||
                strcmp(cap->scope_env_key, cap->name) == 0) {
            JsAstNode* root = parent && parent->node ? parent->node->body : NULL;
            JsAstNode* target = child ? (JsAstNode*)child->node : NULL;
            const char* derived_key = jm_capture_enclosing_lexical_scope_key(
                root, target, cap);
            if (derived_key) {
                // A function-local lexical can shadow an IIFE-promoted module binding;
                // its source identity must survive scope-env layout, not just its name.
                cap->scope_env_key = derived_key;
            }
        }
        if (cap->scope_env_key && cap->scope_env_key[0] &&
                strcmp(cap->scope_env_key, cap->name) != 0) {
            return cap->scope_env_key;
        }
    }
    return cap->name;
}

static bool jm_captures_same_binding(const FnCapture* left,
        const FnCapture* right) {
    if (!left || !right) return false;
    if (left->entry || right->entry) return left->entry == right->entry;
    return left->name && right->name && strcmp(left->name, right->name) == 0;
}

static bool jm_is_direct_program_class_decl(JsProgramNode* program, JsClassNode* class_node) {
    if (!program || !class_node) return false;
    for (JsAstNode* stmt = program->body; stmt; stmt = stmt->next) {
        JsAstNode* actual = stmt;
        if (stmt->node_type == JS_AST_NODE_EXPORT_DECLARATION) {
            JsExportNode* exp = (JsExportNode*)stmt;
            if (exp->declaration) actual = exp->declaration;
        }
        if (actual == (JsAstNode*)class_node) return true;
    }
    return false;
}

static JsFunctionNode* jm_find_iife_function_expr(JsAstNode* expr) {
    if (!expr) return NULL;
    if (expr->node_type != JS_AST_NODE_CALL_EXPRESSION) return NULL;
    JsCallNode* call = (JsCallNode*)expr;
    if (call->callee &&
        (call->callee->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
         call->callee->node_type == JS_AST_NODE_ARROW_FUNCTION)) {
        return (JsFunctionNode*)call->callee;
    }
    return jm_find_iife_function_expr(call->callee);
}

static void jm_cleanup_active_mir_state(JsMirCompileRecoveryState* state,
        bool skip_mir_finish) {
    if (!state) return;
    for (int i = state->count - 1; i >= 0; i--) {
        ActiveJsTranspileOwner* owner = &state->stack[i];
        if (owner->mt) {
            jm_destroy_mir_transpiler(owner->mt);
            owner->mt = NULL;
        }
    }
    if (state->active_mir_ctx && !skip_mir_finish) {
        MIR_finish(state->active_mir_ctx);
    }
    state->active_mir_ctx = NULL;
    while (state->count > 0) {
        ActiveJsTranspileOwner* owner = &state->stack[state->count - 1];
        JsTranspiler* tp = owner->tp;
        char* owned_source = owner->owned_source;
        owner->mt = NULL;
        owner->tp = NULL;
        owner->owned_source = NULL;
        jm_pop_empty_active_js_transpile_owners(state);
        if (tp) js_transpiler_destroy(tp);
        if (owned_source) mem_free(owned_source);
    }
}

void jm_cleanup_active_mir(void) {
    jm_cleanup_active_mir_state(jm_compile_recovery_state_current(), false);
}

void jm_abandon_active_mir_after_signal(void) {
    // A recovered SIGSEGV/SIGBUS may leave MIR's import/module lists
    // inconsistent; re-entering MIR_finish can fault while formatting errors.
    jm_cleanup_active_mir_state(jm_compile_recovery_state_current(), true);
}

void jm_compile_recovery_state_destroy_context(JsRuntimeState* runtime_state) {
    if (!runtime_state || !runtime_state->mir_compile_recovery_state) return;
    JsMirCompileRecoveryState* state =
        (JsMirCompileRecoveryState*)runtime_state->mir_compile_recovery_state;
    // Context teardown is a cold ownership boundary. Finish any interrupted
    // compilation before dropping the capsule so no MIR owner crosses realms.
    jm_cleanup_active_mir_state(state, false);
    mem_free(state);
    runtime_state->mir_compile_recovery_state = NULL;
}

void jm_defer_mir_cleanup(MIR_context_t ctx) {
    if (module_mir_context_count < JS_DEFERRED_MIR_MAX) {
        module_mir_source_buffers[module_mir_context_count] = NULL;
        module_mir_contexts[module_mir_context_count++] = ctx;
    } else {
        // Cannot MIR_finish — JIT-compiled function pointers still live and
        // would crash on call. Just leak the context (rare path; survives
        // until process exit anyway).
        log_error("module: exceeded max deferred MIR contexts (%d) — leaking ctx", JS_DEFERRED_MIR_MAX);
    }
}

void jm_cleanup_deferred_mir() {
    // Deferred MIR storage is inside the active JS capsule.  Once its owner
    // has been released there is no remaining per-context entry to finish.
    if (!js_active_runtime_state) return;
    js_dynfunc_cache_reset();
    for (int i = 0; i < module_mir_context_count; i++) {
        // Deferred eval units use the same JIT generator as ordinary units;
        // finishing only MIR leaves the generator arena and native code live.
        jit_cleanup_mode(module_mir_contexts[i], !g_mir_interp_mode);
        if (module_mir_source_buffers[i]) mem_free(module_mir_source_buffers[i]);
    }
    module_mir_context_count = 0;
}

void* jm_get_last_deferred_mir_ctx() {
    if (module_mir_context_count > 0) {
        return module_mir_contexts[module_mir_context_count - 1];
    }
    return NULL;
}

static bool jm_path_has_lambda_ext(const char* path) {
    int len = path ? (int)strlen(path) : 0;
    return len >= 3 && strcmp(path + len - 3, ".ls") == 0;
}

static bool jm_path_has_known_js_ext(const char* path) {
    int len = path ? (int)strlen(path) : 0;
    return (len >= 3 && strcmp(path + len - 3, ".js") == 0) ||
           (len >= 4 && strcmp(path + len - 4, ".mjs") == 0) ||
           (len >= 4 && strcmp(path + len - 4, ".cjs") == 0) ||
           (len >= 5 && strcmp(path + len - 5, ".json") == 0) ||
           jm_path_has_lambda_ext(path);
}

static bool jm_path_is_lambda_source(const char* path) {
    int len = path ? (int)strlen(path) : 0;
    return len >= 3 && strcmp(path + len - 3, ".ls") == 0;
}

// Resolve a module specifier relative to the importing file's directory
void jm_resolve_module_path(const char* base_file, const char* specifier, int spec_len,
                                   char* out, int out_size) {
    const char* last_slash = strrchr(base_file, '/');
    int dir_len = last_slash ? (int)(last_slash - base_file + 1) : 0;

    if (spec_len >= 2 && specifier[0] == '.' && specifier[1] == '/') {
        // Relative: ./utils.js → dir/utils.js
        snprintf(out, out_size, "%.*s%.*s", dir_len, base_file, spec_len - 2, specifier + 2);
    } else if (spec_len >= 3 && specifier[0] == '.' && specifier[1] == '.' && specifier[2] == '/') {
        // Parent: ../utils.js
        snprintf(out, out_size, "%.*s%.*s", dir_len, base_file, spec_len, specifier);
    } else if (spec_len >= 1 && specifier[0] == '/') {
        // Absolute path
        snprintf(out, out_size, "%.*s", spec_len, specifier);
    } else {
        // Bare specifiers remain unchanged here; external package resolution
        // is not part of the Lambda host link closure.
        char spec_buf[512];
        snprintf(spec_buf, sizeof(spec_buf), "%.*s", spec_len, specifier);

        // skip node: builtins (handled by js_module_get)
        bool has_node_prefix = (spec_len >= 5 && strncmp(specifier, "node:", 5) == 0);

        bool is_builtin = has_node_prefix || jube_specifier_is_builtin(spec_buf);

        // Built-ins are checked by js_module_get; non-built-in package names
        // fall through to the normal file and directory probes below.
        snprintf(out, out_size, "%.*s", spec_len, specifier);
        if (is_builtin) return;  // builtins don't need .js extension
    }

    // If doesn't end in a known JS extension, use Node-style file and
    // directory fallbacks for static literal require/import resolution.
    // Recognized extensions: .js, .mjs, .cjs, .json, .ls
    int len = (int)strlen(out);
    bool has_node_prefix = (len >= 5 && strncmp(out, "node:", 5) == 0);
    if (!has_node_prefix) {
        bool has_ext = jm_path_has_known_js_ext(out);
        if (!has_ext) {
            if (file_is_file(out)) return;

            char js_candidate[512];
            if (len + 3 < (int)sizeof(js_candidate)) {
                snprintf(js_candidate, sizeof(js_candidate), "%s.js", out);
                if (file_is_file(js_candidate) && (int)strlen(js_candidate) < out_size) {
                    snprintf(out, out_size, "%s", js_candidate);
                    return;
                }
            }

            char index_candidate[512];
            const char* sep = (len > 0 && out[len - 1] == '/') ? "" : "/";
            if (len + (int)strlen(sep) + 8 < (int)sizeof(index_candidate)) {
                snprintf(index_candidate, sizeof(index_candidate), "%s%sindex.js", out, sep);
                if (file_is_file(index_candidate) && (int)strlen(index_candidate) < out_size) {
                    snprintf(out, out_size, "%s", index_candidate);
                    return;
                }
            }

            if (len + 3 < (int)out_size) {
                snprintf(out + len, out_size - (size_t)len, "%s", ".js");
            }
        }
    }
}

// Forward declarations for module loading
Item transpile_js_module_to_mir(Runtime* runtime, const char* js_source, const char* filename);
void jm_load_imports(Runtime* runtime, JsAstNode* ast, const char* filename);

// Helper: emit code to store an exported identifier value into module namespace
void jm_emit_module_export(JsMirTranspiler* mt, const char* name, int name_len,
        NameEntry* binding, bool is_default) {
    // Resolve the value through box_item (handles native-typed variables)
    JsIdentifierNode temp_id;
    memset(&temp_id, 0, sizeof(temp_id));
    temp_id.node_type = JS_AST_NODE_IDENTIFIER;
    temp_id.name = name_pool_create_len(mt->tp->name_pool, name, name_len);
    temp_id.entry = binding;

    MIR_reg_t val = jm_transpile_box_item(mt, (JsAstNode*)&temp_id);
    const char* export_key = is_default ? "default" : name;
    int export_key_len = is_default ? 7 : name_len;
    MIR_reg_t key = jm_box_property_name_literal(mt, export_key, export_key_len);
    jm_callr_3(mt, "js_set_key_default", MIR_T_I64, mt->namespace_reg, key, val);
}

// Js52 P1: aliased export — resolve the value via local_name, publish under export_name.
// When the two names match, behaves identically to jm_emit_module_export(..., false).
void jm_emit_module_export_aliased(JsMirTranspiler* mt,
                                          const char* local_name, int local_len,
                                          NameEntry* local_binding,
                                          const char* export_name, int export_len) {
    JsIdentifierNode temp_id;
    memset(&temp_id, 0, sizeof(temp_id));
    temp_id.node_type = JS_AST_NODE_IDENTIFIER;
    temp_id.name = name_pool_create_len(mt->tp->name_pool, local_name, local_len);
    temp_id.entry = local_binding;

    MIR_reg_t val = jm_transpile_box_item(mt, (JsAstNode*)&temp_id);
    MIR_reg_t key = jm_box_property_name_literal(mt, export_name, export_len);
    jm_callr_3(mt, "js_set_key_default", MIR_T_I64, mt->namespace_reg, key, val);
}


// Phase 3.5: Call-site type propagation
// A contradictory argument shape no longer revokes an inferred native body: its
// boxed entry guards the raw call and runs complete boxed lowering on a miss.
// Callback function expressions remain an exclusion because their receiver and
// callback context are not represented by the scalar raw ABI.
// ============================================================================

void jm_callsite_propagate(JsMirTranspiler* mt, JsAstNode* program_body) {
    (void)program_body;
    if (!mt || !mt->tp) return;
    // The indexed node table already contains every call exactly once. It
    // replaces the former per-function and top-level recursive scans.
    AstIndex* index = &mt->tp->ast_index;
    for (uint32_t i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        if (!node || node->node_type != JS_AST_NODE_CALL_EXPRESSION) continue;
        JsCallNode* call = (JsCallNode*)node;
        for (JsAstNode* arg = call->arguments; arg; arg = arg->next) {
            if (arg->node_type != JS_AST_NODE_FUNCTION_EXPRESSION &&
                    arg->node_type != JS_AST_NODE_ARROW_FUNCTION) continue;
            JsFuncCollected* callback = jm_find_collected_func(mt,
                (JsFunctionNode*)arg);
            if (callback && JM_JS_FACT(callback, native_return_kind) != NATIVE_RETURN_NONE) {
                log_debug("js-mir P3.5 callsite: callback '%s' stays boxed for dynamic receiver context",
                    callback->name);
                JM_JS_FACT(callback, native_return_kind) = NATIVE_RETURN_NONE;
            }
        }
    }
}

static void jm_emit_evalscript_global_decl_check_name(JsMirTranspiler* mt, String* name, bool is_func) {
    if (!name || name->len <= 0) return;
    MIR_reg_t key_reg = jm_box_property_name_literal(mt, name->chars, name->len);
    jm_callr_1(mt, is_func ? "js_evalscript_check_global_function_decl" : "js_evalscript_check_global_var_decl", MIR_T_I64, key_reg);
    jm_emit_error_lane_propagate_check(mt);
}

static void jm_emit_evalscript_global_decl_check_prefixed(JsMirTranspiler* mt, const char* name) {
    if (!name) return;
    if (strncmp(name, "_js_", 4) == 0) name += 4;
    if (!name[0]) return;
    MIR_reg_t key_reg = jm_box_property_name_literal(mt, name, strlen(name));
    jm_callr_1(mt, "js_evalscript_check_global_var_decl", MIR_T_I64, key_reg);
    jm_emit_error_lane_propagate_check(mt);
}

static void jm_emit_evalscript_global_lex_decl_check_name(JsMirTranspiler* mt, String* name) {
    if (!name || name->len <= 0) return;
    MIR_reg_t key_reg = jm_box_property_name_literal(mt, name->chars, name->len);
    jm_callr_1(mt, "js_evalscript_check_global_lex_decl", MIR_T_I64, key_reg);
    jm_emit_error_lane_propagate_check(mt);
}

static void jm_emit_evalscript_global_lex_decl_precheck(JsMirTranspiler* mt, JsAstNode* node) {
    if (!node) return;
    if (node->node_type == JS_AST_NODE_CLASS_DECLARATION) {
        JsClassNode* cls = (JsClassNode*)node;
        jm_emit_evalscript_global_lex_decl_check_name(mt, cls->name);
        return;
    }
    if (node->node_type != JS_AST_NODE_VARIABLE_DECLARATION) return;
    JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)node;
    if (vd->kind != JS_VAR_LET && vd->kind != JS_VAR_CONST) return;
    for (JsAstNode* d = vd->declarations; d; d = d->next) {
        if (d->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
        JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)d;
        if (!decl->id) continue;
        if (decl->id->node_type == JS_AST_NODE_IDENTIFIER) {
            jm_emit_evalscript_global_lex_decl_check_name(mt, ((JsIdentifierNode*)decl->id)->name);
        } else if (decl->id->node_type == JS_AST_NODE_OBJECT_PATTERN ||
                   decl->id->node_type == JS_AST_NODE_ARRAY_PATTERN) {
            struct hashmap* names = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            jm_collect_pattern_names(decl->id, names);
            size_t iter = 0; void* item;
            while (hashmap_iter(names, &iter, &item)) {
                JsNameSetEntry* entry = (JsNameSetEntry*)item;
                const char* name = entry->name;
                if (strncmp(name, "_js_", 4) == 0) name += 4;
                MIR_reg_t key_reg = jm_box_property_name_literal(mt, name, strlen(name));
                jm_callr_1(mt, "js_evalscript_check_global_lex_decl", MIR_T_I64, key_reg);
                jm_emit_error_lane_propagate_check(mt);
            }
            hashmap_free(names);
        }
    }
}

static void jm_emit_evalscript_global_decl_prechecks(JsMirTranspiler* mt, JsAstNode* node) {
    if (!node) return;
    switch (node->node_type) {
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)node;
        if (vd->kind != JS_VAR_VAR) return;
        for (JsAstNode* d = vd->declarations; d; d = d->next) {
            if (d->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
            JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)d;
            if (!decl->id) continue;
            if (decl->id->node_type == JS_AST_NODE_IDENTIFIER) {
                jm_emit_evalscript_global_decl_check_name(mt, ((JsIdentifierNode*)decl->id)->name, false);
            } else if (decl->id->node_type == JS_AST_NODE_OBJECT_PATTERN ||
                       decl->id->node_type == JS_AST_NODE_ARRAY_PATTERN) {
                struct hashmap* names = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
                    jm_name_hash, jm_name_cmp, NULL, NULL);
                jm_collect_pattern_names(decl->id, names);
                size_t iter = 0; void* item;
                while (hashmap_iter(names, &iter, &item)) {
                    JsNameSetEntry* entry = (JsNameSetEntry*)item;
                    jm_emit_evalscript_global_decl_check_prefixed(mt, entry->name);
                }
                hashmap_free(names);
            }
        }
        break;
    }
    case JS_AST_NODE_FUNCTION_DECLARATION: {
        JsFunctionNode* fn = (JsFunctionNode*)node;
        jm_emit_evalscript_global_decl_check_name(mt, fn->name, true);
        break;
    }
    case JS_AST_NODE_EXPORT_DECLARATION: {
        JsExportNode* exp = (JsExportNode*)node;
        jm_emit_evalscript_global_decl_prechecks(mt, exp->declaration);
        break;
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* block = (JsBlockNode*)node;
        for (JsAstNode* s = block->statements; s; s = s->next)
            jm_emit_evalscript_global_decl_prechecks(mt, s);
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* ifn = (JsIfNode*)node;
        jm_emit_evalscript_global_decl_prechecks(mt, ifn->consequent);
        jm_emit_evalscript_global_decl_prechecks(mt, ifn->alternate);
        break;
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* sw = (JsSwitchNode*)node;
        for (JsAstNode* c = sw->cases; c; c = c->next)
            jm_emit_evalscript_global_decl_prechecks(mt, c);
        break;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* sc = (JsSwitchCaseNode*)node;
        for (JsAstNode* s = sc->consequent; s; s = s->next)
            jm_emit_evalscript_global_decl_prechecks(mt, s);
        break;
    }
    case AST_NODE_LOOP: {
        AstLoopControlNode* loop = (AstLoopControlNode*)node;
        jm_emit_evalscript_global_decl_prechecks(mt, loop->init);
        jm_emit_evalscript_global_decl_prechecks(mt, loop->body);
        break;
    }
    case JS_AST_NODE_FOR_IN_STATEMENT:
    case JS_AST_NODE_FOR_OF_STATEMENT: {
        JsForInNode* for_node = (JsForInNode*)node;
        jm_emit_evalscript_global_decl_prechecks(mt, for_node->left);
        jm_emit_evalscript_global_decl_prechecks(mt, for_node->body);
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* try_node = (JsTryNode*)node;
        jm_emit_evalscript_global_decl_prechecks(mt, try_node->block);
        jm_emit_evalscript_global_decl_prechecks(mt, try_node->handler);
        jm_emit_evalscript_global_decl_prechecks(mt, try_node->finalizer);
        break;
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* catch_node = (JsCatchNode*)node;
        jm_emit_evalscript_global_decl_prechecks(mt, catch_node->body);
        break;
    }
    case JS_AST_NODE_LABELED_STATEMENT: {
        JsLabeledStatementNode* label = (JsLabeledStatementNode*)node;
        jm_emit_evalscript_global_decl_prechecks(mt, label->body);
        break;
    }
    default:
        break;
    }
}

static bool jm_is_plain_script_module_var_decl_without_init(JsMirTranspiler* mt, JsAstNode* node) {
    if (!mt || !node || mt->is_module || mt->is_eval_direct || !mt->module_consts) return false;
    if (node->node_type != JS_AST_NODE_VARIABLE_DECLARATION) return false;
    JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)node;
    if (vd->kind != JS_VAR_VAR || !vd->declarations) return false;
    for (JsAstNode* d = vd->declarations; d; d = d->next) {
        if (d->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) return false;
        JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)d;
        if (decl->init || !decl->id || decl->id->node_type != JS_AST_NODE_IDENTIFIER) return false;
        JsIdentifierNode* id = (JsIdentifierNode*)decl->id;
        JsModuleConstEntry* mc = jm_find_module_const_by_binding(mt, id->entry);
        if (!mc || mc->const_type != MCONST_MODVAR || mc->var_kind != JS_VAR_VAR ||
                (int)mc->int_val < 0) {
            return false;
        }
    }
    return true;
}

static int js_mir_analyze_and_plan(void* opaque) {
    JsMirTranspiler* mt = (JsMirTranspiler*)opaque;
    JsAstNode* root = mt && mt->tp ? (JsAstNode*)mt->tp->ast_root : NULL;
    if (!root || root->node_type != JS_AST_NODE_PROGRAM) {
        log_error("js-mir: expected program node");
        return 0;
    }
    JsProgramNode* program = (JsProgramNode*)root;

    // v20: Detect program-level "use strict" directive
    mt->is_global_strict = (mt->tp && mt->tp->strict_mode) || program->has_use_strict_directive;

    // The shared index owns source function and class identity. Only instance
    // field initializers add synthetic functions during collection.
    int field_initializer_count = jm_indexed_synthetic_field_initializer_count(
        &mt->tp->ast_index);
    if (field_initializer_count < 0 ||
            mt->tp->ast_index.function_count > (uint32_t)(INT_MAX - field_initializer_count) ||
            mt->tp->ast_index.class_count > INT_MAX) {
        log_error("js-mir: indexed function/class metadata exceeds collector capacity");
        return false;
    }
    mt->func_capacity = (int)mt->tp->ast_index.function_count + field_initializer_count;
    mt->class_capacity = (int)mt->tp->ast_index.class_count;
    // Collection records retain AST/name pointers and generated code can retain
    // metadata derived from them, so allocate them with the transpiler pools.
    mt->func_entries = (JsFuncCollected*)pool_calloc(
        mt->tp->pool, (size_t)mt->func_capacity * sizeof(JsFuncCollected));
    mt->class_entries = (JsClassEntry*)pool_calloc(
        mt->tp->pool, (size_t)mt->class_capacity * sizeof(JsClassEntry));
    if ((mt->func_capacity && !mt->func_entries) ||
        (mt->class_capacity && !mt->class_entries)) {
        log_error("js-mir: failed to allocate exact function/class metadata");
        mt->collection_failed = true;
        return false;
    }
    jm_collect_indexed_functions(mt);
    if (mt->collection_failed ||
            mt->func_count != mt->func_capacity ||
            mt->class_count != mt->class_capacity) {
        // A mismatch means indexed identity and collection disagree.
        log_error("js-mir: indexed collection mismatch functions=%d/%d classes=%d/%d",
            mt->func_count, mt->func_capacity, mt->class_count, mt->class_capacity);
        mt->collection_failed = true;
        return false;
    }
    log_debug("js-mir: collected %d functions, %d classes", mt->func_count, mt->class_count);

    // publish module constants and variable slots.
    mt->module_consts = hashmap_new(sizeof(JsModuleConstEntry), 16, 0, 0,
        js_module_const_hash, js_module_const_cmp, NULL, NULL);

    // Pre-seed module_consts from preamble (batch mode: test inherits harness definitions)
    if (mt->preamble_entries && mt->preamble_entry_count > 0) {
        for (int i = 0; i < mt->preamble_entry_count; i++) {
            JsModuleConstEntry inherited = mt->preamble_entries[i];
            // The preamble's direct-scope entries are owned by its separate
            // parse unit. Relink public globals when this test's resolver
            // supplies its own NameEntry instead of retaining a stale pointer.
            inherited.binding = NULL;
            inherited.binding_node = NULL;
            inherited.is_preamble_external = true;
            hashmap_set(mt->module_consts, &inherited);
        }
        log_debug("js-mir: pre-seeded %d preamble entries (var_count=%d)",
            mt->preamble_entry_count, mt->preamble_var_count);
    }

    // Assign module var indices for non-literal top-level declarations.
    // These are runtime-computed values (const som = {...}, const X = new Y(), etc.)
    // that need to be accessible from class method closures via js_get_module_var().
    // A filtered realm preamble can retain no names while still reserving
    // sparse caller-slot indices; dropping that count undersizes the fresh
    // eval slab before its prefix copy.
    mt->module_var_count = mt->preamble_var_count;
    {
        JsAstNode* s = program->body;
        while (s) {
            // Unwrap export declarations to reach inner variable declarations
            JsAstNode* actual = s;
            if (s->node_type == JS_AST_NODE_EXPORT_DECLARATION) {
                JsExportNode* exp = (JsExportNode*)s;
                if (exp->declaration) actual = exp->declaration;
            }
            if (actual->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                JsVariableDeclarationNode* v = (JsVariableDeclarationNode*)actual;
                JsAstNode* d = v->declarations;
                while (d) {
                    if (d->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                        JsVariableDeclaratorNode* vd = (JsVariableDeclaratorNode*)d;
                        if (vd->id && vd->id->node_type == JS_AST_NODE_IDENTIFIER) {
                            JsIdentifierNode* vid = (JsIdentifierNode*)vd->id;
                            const char* vname = jm_var_name(vid->name);
                            TypeId modvar_type = (TypeId)0;
                            if (vd->init && vd->init->node_type == JS_AST_NODE_LITERAL) {
                                JsLiteralNode* mlit = (JsLiteralNode*)vd->init;
                                if (mlit->literal_type == JS_LITERAL_NUMBER) {
                                    modvar_type = mlit->is_bigint
                                        ? LMD_TYPE_DECIMAL : LMD_TYPE_FLOAT;
                                }
                            }
                            jm_register_module_var(mt, vname, (int)v->kind,
                                modvar_type, false, "module var", vid->entry);
                        } else if (vd->id && (vd->id->node_type == JS_AST_NODE_OBJECT_PATTERN ||
                                               vd->id->node_type == JS_AST_NODE_ARRAY_PATTERN)) {
                            // destructured binding: collect all names from the pattern
                            struct hashmap* pat_names = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
                                jm_name_hash, jm_name_cmp, NULL, NULL);
                            jm_collect_pattern_names(vd->id, pat_names);
                            size_t piter = 0; void* pitem;
                            while (hashmap_iter(pat_names, &piter, &pitem)) {
                                JsNameSetEntry* ne = (JsNameSetEntry*)pitem;
                                jm_register_module_var(mt, ne->name, (int)v->kind,
                                    (TypeId)0, false, "module var (destructured)",
                                    ne->entry);
                            }
                            hashmap_free(pat_names);
                        }
                    }
                    d = d->next;
                }
            }
            s = s->next;
        }
    }

    // hoist nested var declarations to module scope.
    {
        struct hashmap* hoisted_vars = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        JsAstNode* s = program->body;
        while (s) {
            JsAstNode* actual = s;
            if (s->node_type == JS_AST_NODE_EXPORT_DECLARATION) {
                JsExportNode* exp = (JsExportNode*)s;
                if (exp->declaration) actual = exp->declaration;
            }
            // Skip top-level variable declarations (already handled above)
            // Also skip function/class declarations (handled below as module bindings).
            if (actual->node_type != JS_AST_NODE_VARIABLE_DECLARATION &&
                actual->node_type != JS_AST_NODE_FUNCTION_DECLARATION &&
                actual->node_type != JS_AST_NODE_CLASS_DECLARATION) {
                jm_collect_indexed_body_locals(mt, actual, hoisted_vars, true);  // var_only: only hoist var
            }
            s = s->next;
        }
        // Register any newly found var names as module vars
        size_t iter = 0; void* item;
        while (hashmap_iter(hoisted_vars, &iter, &item)) {
            JsNameSetEntry* e = (JsNameSetEntry*)item;
            if (e->from_func_decl && (mt->is_global_strict || mt->is_module)) {
                log_debug("js-mir: suppress strict nested func hoist '%s'", e->name);
                continue;
            }
            if (jm_function_decl_annex_b_disallowed(e)) {
                log_debug("js-mir: suppress AnnexB nested func hoist '%s' (lexical collision)", e->name);
                continue;
            }
            // The planner records the binding written by the var environment;
            // a block function's lexical cell is deliberately distinct.
            jm_register_module_var(mt, e->name, 0, (TypeId)0,
                e->from_func_decl, "hoisted var", jm_hoisted_var_binding(e));
        }
        hashmap_free(hoisted_vars);
    }

    // publish import bindings for closure/module-slot reads.
    {
        JsAstNode* s = program->body;
        while (s) {
            if (s->node_type == JS_AST_NODE_IMPORT_DECLARATION) {
                JsImportNode* imp = (JsImportNode*)s;

                // Default import: import X from 'module'
                if (imp->default_name) {
                    const char* vname = jm_var_name(imp->default_name);
                    // Js57 P3 (Track B2): detect self-import so the module_consts
                    // entry can carry the live-binding marker. Closures and
                    // module-level reads then route through the live-binding
                    // runtime call instead of the snapshot path.
                    char resolved_pp[512] = {0};
                    if (imp->source) {
                        if (mt->filename) {
                            jm_resolve_module_path(mt->filename, imp->source->chars,
                                (int)imp->source->len, resolved_pp, sizeof(resolved_pp));
                        } else {
                            snprintf(resolved_pp, sizeof(resolved_pp), "%.*s",
                                (int)imp->source->len, imp->source->chars);
                        }
                    }
                    bool is_self_import = (mt->filename != NULL && resolved_pp[0] != '\0' &&
                        strcmp(resolved_pp, mt->filename) == 0);
                    JsModuleConstEntry* mce = jm_register_module_var(mt, vname,
                        0, (TypeId)0, false, "import default",
                        imp->default_entry);
                    if (mce) mce->binding_node = (JsAstNode*)imp;
                    if (mce && is_self_import) {
                        mce->is_live_default_binding = true;
                        mce->live_binding_specifier = name_pool_create_len(
                            mt->tp->name_pool, resolved_pp, (int)strlen(resolved_pp))->chars;
                    }
                }

                // Namespace import: import * as X from 'module'
                if (imp->namespace_name) {
                    const char* vname = jm_var_name(imp->namespace_name);
                    JsModuleConstEntry* mce = jm_register_module_var(mt, vname,
                        0, (TypeId)0, false, "import namespace",
                        imp->namespace_entry);
                    if (mce) mce->binding_node = (JsAstNode*)imp;
                }

                // Named imports: import { a, b as c } from 'module'
                JsAstNode* spec = imp->specifiers;
                while (spec) {
                    if (spec->node_type == JS_AST_NODE_IMPORT_SPECIFIER) {
                        JsImportSpecifierNode* isp = (JsImportSpecifierNode*)spec;
                        const char* vname = jm_var_name(isp->local_name);
                        JsModuleConstEntry* mce = jm_register_module_var(mt,
                            vname, 0, (TypeId)0, false, "import named",
                            isp->local_entry);
                        if (mce) mce->binding_node = spec;
                    }
                    spec = spec->next;
                }
            }
            s = s->next;
        }
    }

    // Detect function declarations that self-reassign (Babel _typeof pattern etc.).
    // Only mark a function as reassigned if its OWN body contains an assignment
    // to its own name. This avoids false positives from unrelated short-named
    // variables across webpack modules.
    {
        for (int fi = 0; fi < mt->func_count; fi++) {
            JsFunctionNode* fn = mt->func_entries[fi].node;
            if (!fn || !fn->name || !fn->body) continue;
            struct hashmap* self_assigned = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                jm_name_hash, jm_binding_cmp, NULL, NULL);
            jm_collect_indexed_func_assignments(mt, fn->body, self_assigned);
            if (jm_binding_set_has(self_assigned, fn->entry)) {
                JM_JS_FACT(&mt->func_entries[fi], is_reassigned) = true;
                log_debug("js-mir: function '%.*s' is self-reassigned — skipping direct call optimization",
                    (int)fn->name->len, fn->name->chars);
            }
            hashmap_free(self_assigned);
        }
    }

    // Add top-level function declarations as module-level identifiers
    {
        JsAstNode* s = program->body;
        while (s) {
            if (s->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
                JsFunctionNode* fn = (JsFunctionNode*)s;
                if (fn->name) {
                    JsFuncCollected* fc = jm_find_collected_func(mt, fn);
                    if (fc) {
                        JsModuleConstEntry mce;
                        memset(&mce, 0, sizeof(mce));
                        mce.name = jm_var_name(fn->name);
                        mce.binding = fn->entry;
                        // Only add if not already in module_consts
                        JsModuleConstEntry lookup;
                        lookup.name = jm_persist_name(mce.name);
                        if (!hashmap_get(mt->module_consts, &lookup)) {
                            // Store as MCONST_MODVAR so value persists in js_module_vars[].
                            // Direct call optimization still works independently via
                            // jm_find_collected_func() in the call expression handler.
                            // This also allows eval()/new Function() to access the function
                            // via the shared module_vars array.
                            mce.const_type = MCONST_MODVAR;
                            mce.int_val = mt->module_var_count++;
                            hashmap_set(mt->module_consts, &mce);
                            log_debug("js-mir: module func '%s' → module_var[%d]",
                                mce.name, (int)mce.int_val);
                        }
                    }
                }
            }
            s = s->next;
        }
    }

    // Add IIFE-local function declarations as module-level identifiers.
    // Pattern: top-level (() => { ... })() or (function() { ... })()
    // All named function declarations inside the IIFE need to be reachable as module consts
    // so that class methods defined inside the IIFE can capture them.
    {
        auto find_promotable_iife = [&](JsAstNode* stmt) -> JsFunctionNode* {
            if (!stmt) return NULL;
            JsAstNode* expr = NULL;
            if (stmt->node_type == JS_AST_NODE_EXPRESSION_STATEMENT) {
                expr = ((JsExpressionStatementNode*)stmt)->expression;
            } else if (stmt->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)stmt;
                for (JsAstNode* d = vd->declarations; d; d = d->next) {
                    if (d->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
                    JsFunctionNode* iife = jm_find_iife_function_expr(
                        ((JsVariableDeclaratorNode*)d)->init);
                    if (iife) {
                        expr = ((JsVariableDeclaratorNode*)d)->init;
                        break;
                    }
                }
            }
            if (!expr || expr->node_type != JS_AST_NODE_CALL_EXPRESSION) return NULL;
            JsFunctionNode* iife_fn = jm_find_iife_function_expr(expr);
            if (!iife_fn || !iife_fn->body || iife_fn->is_async || iife_fn->is_generator) return NULL;
            if (!iife_fn->name) return iife_fn;

            struct hashmap* refs = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                jm_name_hash, jm_binding_cmp, NULL, NULL);
            jm_collect_indexed_body_refs(mt, iife_fn, refs);
            bool self_referencing = jm_binding_set_has(refs, iife_fn->entry);
            hashmap_free(refs);
            return self_referencing ? NULL : iife_fn;
        };

        // The IIFE-promotion table is name keyed. A minifier legitimately reuses
        // a short name in sibling wrappers, so only promote names owned by one
        // wrapper; otherwise distinct lexical bindings would share one module slot.
        struct hashmap* iife_binding_counts = hashmap_new(sizeof(JsNameSetEntry), 256, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        auto note_iife_binding = [&](const char* name) {
            JsNameSetEntry lookup;
            memset(&lookup, 0, sizeof(lookup));
            lookup.name = jm_persist_name(name);
            JsNameSetEntry* existing = (JsNameSetEntry*)hashmap_get(iife_binding_counts, &lookup);
            if (existing) {
                existing->var_kind++;
                return;
            }
            lookup.var_kind = 1;
            hashmap_set(iife_binding_counts, &lookup);
        };
        auto iife_binding_is_unique = [&](const char* name) -> bool {
            JsNameSetEntry lookup;
            memset(&lookup, 0, sizeof(lookup));
            lookup.name = jm_persist_name(name);
            JsNameSetEntry* entry = (JsNameSetEntry*)hashmap_get(iife_binding_counts, &lookup);
            return entry && entry->var_kind == 1;
        };
        for (JsAstNode* count_stmt = program->body; count_stmt; count_stmt = count_stmt->next) {
            JsFunctionNode* iife_fn = find_promotable_iife(count_stmt);
            if (!iife_fn || iife_fn->body->node_type != JS_AST_NODE_BLOCK_STATEMENT) continue;
            struct hashmap* wrapper_bindings = hashmap_new(sizeof(JsNameSetEntry), 32, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            JsBlockNode* block = (JsBlockNode*)iife_fn->body;
            for (JsAstNode* s = block->statements; s; s = s->next) {
                if (s->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
                    JsFunctionNode* fn = (JsFunctionNode*)s;
                    if (fn->name) {
                        const char* name = jm_var_name(fn->name);
                        jm_name_set_add(wrapper_bindings, name);
                    }
                } else if (s->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                    JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)s;
                    for (JsAstNode* d = vd->declarations; d; d = d->next) {
                        if (d->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
                        JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)d;
                        if (!decl->id || decl->id->node_type != JS_AST_NODE_IDENTIFIER) continue;
                        JsIdentifierNode* id = (JsIdentifierNode*)decl->id;
                        const char* name = jm_var_name(id->name);
                        jm_name_set_add(wrapper_bindings, name);
                    }
                }
            }
            struct hashmap* function_hoists = hashmap_new(sizeof(JsNameSetEntry), 32, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            jm_collect_indexed_body_locals(mt, iife_fn->body, function_hoists, true);
            size_t hoist_iter = 0;
            void* hoist_item = NULL;
            while (hashmap_iter(function_hoists, &hoist_iter, &hoist_item)) {
                JsNameSetEntry* entry = (JsNameSetEntry*)hoist_item;
                if (entry->from_func_decl) jm_name_set_add(wrapper_bindings, entry->name);
            }
            hashmap_free(function_hoists);
            size_t binding_iter = 0;
            void* binding_item = NULL;
            while (hashmap_iter(wrapper_bindings, &binding_iter, &binding_item)) {
                note_iife_binding(((JsNameSetEntry*)binding_item)->name);
            }
            hashmap_free(wrapper_bindings);
        }

        auto top_level_declares_name = [&](const char* candidate, JsAstNode* ignored_stmt) -> bool {
            if (!candidate) return false;
            JsAstNode* top = program->body;
            while (top) {
                if (top == ignored_stmt) { top = top->next; continue; }
                if (top->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                    JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)top;
                    for (JsAstNode* d = vd->declarations; d; d = d->next) {
                        if (d->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
                        JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)d;
                        if (!decl->id || decl->id->node_type != JS_AST_NODE_IDENTIFIER) continue;
                        JsIdentifierNode* id = (JsIdentifierNode*)decl->id;
                        const char* top_name = jm_var_name(id->name);
                        if (strcmp(top_name, candidate) == 0) return true;
                    }
                } else if (top->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
                    JsFunctionNode* fn = (JsFunctionNode*)top;
                    if (fn->name) {
                        const char* top_name = jm_var_name(fn->name);
                        if (strcmp(top_name, candidate) == 0) return true;
                    }
                } else if (top->node_type == JS_AST_NODE_CLASS_DECLARATION) {
                    JsClassNode* cls = (JsClassNode*)top;
                    if (cls->name) {
                        const char* top_name = jm_var_name(cls->name);
                        if (strcmp(top_name, candidate) == 0) return true;
                    }
                }
                top = top->next;
            }
            return false;
        };

        auto register_fn_as_module_const = [&](JsFunctionNode* fn) {
            if (!fn->name) return;
            JsFuncCollected* fc = jm_find_collected_func(mt, fn);
            if (!fc) return;
            JsModuleConstEntry mce;
            memset(&mce, 0, sizeof(mce));
            mce.name = jm_var_name(fn->name);
            mce.binding = fn->entry;
            JsModuleConstEntry lookup;
            lookup.name = jm_persist_name(mce.name);
            if (!iife_binding_is_unique(mce.name)) return;
            if (!hashmap_get(mt->module_consts, &lookup)) {
                mce.const_type = MCONST_MODVAR;
                // Direct IIFE function declarations are promoted out of the
                // wrapper frame; capture analysis must not mistake the original
                // wrapper-local declaration for a normal ancestor capture.
                mce.is_iife_func_decl = true;
                JM_JS_FACT(fc, is_iife_func_decl) = true;
                mce.int_val = mt->module_var_count++;
                hashmap_set(mt->module_consts, &mce);
                log_debug("js-mir: iife func '%s' → module_var[%d]", mce.name, (int)mce.int_val);
            }
        };

        // Scan top-level statements for IIFE patterns
        JsAstNode* stmt = program->body;
        while (stmt) {
            JsFunctionNode* iife_fn = find_promotable_iife(stmt);
            if (!iife_fn) { stmt = stmt->next; continue; }

            // Mark this IIFE body function so its var decls use module vars
            JsFuncCollected* iife_fc = jm_find_collected_func(mt, iife_fn);
            if (iife_fc) JM_JS_FACT(iife_fc, is_iife_body) = true;

            // Scan IIFE body for function declarations and var declarations
            if (iife_fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
                JsBlockNode* blk = (JsBlockNode*)iife_fn->body;
                JsAstNode* s = blk->statements;
                while (s) {
                    if (s->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
                        register_fn_as_module_const((JsFunctionNode*)s);
                    } else if (s->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                        // var/let/const inside IIFE — register non-literal vars as module vars
                        JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)s;
                        JsAstNode* d = vd->declarations;
                        while (d) {
                            if (d->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                                JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)d;
                                if (decl->id && decl->id->node_type == JS_AST_NODE_IDENTIFIER) {
                                    JsIdentifierNode* vid = (JsIdentifierNode*)decl->id;
                                    const char* vname = jm_var_name(vid->name);
                                    if (top_level_declares_name(vname, stmt)) {
                                        d = d->next;
                                        continue;
                                    }
                                    if (!iife_binding_is_unique(vname)) {
                                        d = d->next;
                                        continue;
                                    }
                                    JsModuleConstEntry* mce = jm_register_module_var(mt,
                                        vname, (int)vd->kind, (TypeId)0, false,
                                        "iife var", vid->entry);
                                    if (mce) mce->is_iife_var = true;
                                }
                            }
                            d = d->next;
                        }
                    }
                    s = s->next;
                }
                struct hashmap* iife_func_hoists = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                    jm_name_hash, jm_name_cmp, NULL, NULL);
                bool iife_effective_strict = mt->is_global_strict || mt->is_module ||
                    (iife_fc && JM_JS_FACT(iife_fc, is_strict));
            jm_collect_indexed_body_locals(mt, iife_fn->body, iife_func_hoists, true);
                size_t fh_iter = 0; void* fh_item;
                while (hashmap_iter(iife_func_hoists, &fh_iter, &fh_item)) {
                    JsNameSetEntry* e = (JsNameSetEntry*)fh_item;
                    if (!e->from_func_decl) continue;
                    if (iife_effective_strict) continue;
                    if (top_level_declares_name(e->name, stmt)) continue;
                    if (jm_function_decl_annex_b_disallowed(e)) continue;
                    if (!iife_binding_is_unique(e->name)) continue;
                    JsModuleConstEntry* mce = jm_register_module_var(mt,
                        e->name, 0, (TypeId)0, true, "nested iife func",
                        jm_hoisted_var_binding(e));
                    if (mce) mce->is_iife_var = true;
                }
                hashmap_free(iife_func_hoists);
            }
            stmt = stmt->next;
        }
        hashmap_free(iife_binding_counts);
    }

    // Add class names as module-level identifiers so they can be captured.
    // Each class gets a module_var_index so the class object can be stored/retrieved
    // at runtime (needed for __publicField, passing classes as values, etc.)
    for (int ci = 0; ci < mt->class_count; ci++) {
        JsClassEntry* ce = &mt->class_entries[ci];
        if (ce->name) {
            bool direct_program_class = ce->is_declaration &&
                jm_is_direct_program_class_decl(program, ce->node);
            if (ce->is_declaration && !direct_program_class) {
                ce->inner_module_var_index = mt->module_var_count++;
                log_debug("js-mir: nested class inner binding '%.*s' module_var[%d]",
                    (int)ce->name->len, ce->name->chars, ce->inner_module_var_index);
                continue;
            }
            JsModuleConstEntry mce;
            memset(&mce, 0, sizeof(mce));
            mce.name = jm_var_name(ce->name);
            mce.binding = ce->node ? ce->node->outer_entry : NULL;
            if (ce->is_declaration) {
                // Check if this class name already has a MCONST_MODVAR (iife_var) entry.
                // If so, reuse the same module_var index so the static getter 'this' lookup
                // reads from the same slot that the variable assignment writes to.
                JsModuleConstEntry* existing = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mce);
                if (existing && existing->const_type == MCONST_MODVAR) {
                    int reused_index = (int)existing->int_val;
                    mce.const_type = MCONST_CLASS;
                    mce.int_val = reused_index;
                    hashmap_set(mt->module_consts, &mce);
                    log_debug("js-mir: module class '%s' reusing module_var[%d] from iife_var", mce.name, reused_index);
                } else {
                    mce.const_type = MCONST_CLASS;
                    mce.int_val = mt->module_var_count++;
                    hashmap_set(mt->module_consts, &mce);
                    log_debug("js-mir: module class '%s' module_var[%d]", mce.name, (int)mce.int_val);
                }
            }
            ce->inner_module_var_index = mt->module_var_count++;
            log_debug("js-mir: class inner binding '%s' module_var[%d]",
                mce.name, ce->inner_module_var_index);
        }
    }

    // Resolve superclass pointers for class inheritance
    for (int i = 0; i < mt->class_count; i++) {
        JsClassEntry* ce = &mt->class_entries[i];
        ce->superclass = NULL;
        if (ce->node && ce->node->superclass &&
            ce->node->superclass->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* super_id = (JsIdentifierNode*)ce->node->superclass;
            if (super_id->name) {
                if (ce->node->entry && ce->node->entry == super_id->entry) {
                    // ClassDefinitionEvaluation evaluates heritage before its
                    // inner class-name binding is initialized. This is a TDZ
                    // read even when an outer var has the same spelling.
                    ce->has_self_extends = true;
                    continue;
                }
                ce->superclass = jm_find_class_for_binding(mt, super_id->entry);
                // Detect self-referential extends (class x extends x {}):
                // Per ES spec, the class name is in TDZ during the extends clause.
                // At compile time, we simply clear the superclass to prevent infinite
                // loops in inheritance chain walkers. The runtime will throw ReferenceError
                // because the class binding doesn't exist yet when extends is evaluated.
                if (ce->superclass == ce) {
                    ce->superclass = NULL;
                    ce->has_self_extends = true;
                    if (ce->name) {
                        log_debug("js-mir: class '%.*s' has self-referential extends (TDZ)",
                            (int)ce->name->len, ce->name->chars);
                    }
                }
                if (ce->superclass) {
                    if (ce->name && ce->superclass->name) {
                        log_debug("js-mir: class '%.*s' extends '%.*s'",
                            (int)ce->name->len, ce->name->chars,
                            (int)ce->superclass->name->len, ce->superclass->name->chars);
                    }
                }
            }
        }
    }

    // Assign module variable indexes for static class fields
    for (int ci = 0; ci < mt->class_count; ci++) {
        JsClassEntry* ce = &mt->class_entries[ci];
        for (int fi = 0; fi < ce->static_field_count; fi++) {
            JsStaticFieldEntry* sf = &ce->static_fields[fi];
            if (sf->name && ce->name && mt->module_var_count < JS_MAX_MODULE_VARS) {
                sf->module_var_index = mt->module_var_count;
                // Register as module const for ClassName.fieldName access pattern
                JsModuleConstEntry mce;
                memset(&mce, 0, sizeof(mce));
                mce.name = jm_format_name("_js_%.*s_%.*s",
                    (int)ce->name->len, ce->name->chars,
                    (int)sf->name->len, sf->name->chars);
                mce.const_type = MCONST_MODVAR;
                mce.int_val = mt->module_var_count++;
                hashmap_set(mt->module_consts, &mce);
                log_debug("js-mir: static field '%.*s.%.*s' → module_var[%d]",
                    (int)ce->name->len, ce->name->chars,
                    (int)sf->name->len, sf->name->chars,
                    (int)mce.int_val);
            }
        }
        for (int fi = 0; fi < ce->static_field_count; fi++) {
            JsStaticFieldEntry* sf = &ce->static_fields[fi];
            if (sf->computed && sf->key_expr && mt->module_var_count < JS_MAX_MODULE_VARS) {
                sf->key_module_var_index = mt->module_var_count++;
                log_debug("js-mir: static field computed key slot class=%.*s field=%d module_var[%d]",
                    ce->name ? (int)ce->name->len : 0, ce->name ? ce->name->chars : "",
                    fi, sf->key_module_var_index);
            }
        }
        for (int fi = 0; fi < ce->instance_field_count; fi++) {
            JsInstanceFieldEntry* inf = &ce->instance_fields[fi];
            if (inf->computed && inf->key_expr && mt->module_var_count < JS_MAX_MODULE_VARS) {
                inf->key_module_var_index = mt->module_var_count++;
                log_debug("js-mir: instance field computed key slot class=%.*s field=%d module_var[%d]",
                    ce->name ? (int)ce->name->len : 0, ce->name ? ce->name->chars : "",
                    fi, inf->key_module_var_index);
            }
        }
    }

    for (int i = 0; i < mt->class_count; i++) {
        JsClassEntry* ce = &mt->class_entries[i];
        log_debug("js-mir: class '%.*s' with %d methods, ctor=%p",
            ce->name ? (int)ce->name->len : 0, ce->name ? ce->name->chars : "",
            ce->method_count, (void*)ce->constructor);
        for (int mi = 0; mi < ce->method_count; mi++) {
            JsClassMethodEntry* me = &ce->methods[mi];
            log_debug("js-mir:   method[%d]: '%.*s' static=%d ctor=%d",
                mi, me->name ? (int)me->name->len : 0, me->name ? me->name->chars : "(null)",
                me->is_static, me->is_constructor);
        }
    }

    // Phase 1.5: Capture analysis
    // Direct scope resolution assigns each identifier its exact binding.
    {
        // Analyze each collected function for captures.
        for (int i = 0; i < mt->func_count; i++) {
            JsFuncCollected* fc = &mt->func_entries[i];

            bool captures_with_scope = jm_ast_node_has_with_ancestor(mt, root,
                (JsAstNode*)fc->node);
            jm_analyze_captures(mt, fc, mt->module_consts,
                captures_with_scope);
        }

        // Phase 1.6: Transitive capture propagation for multi-level closures.
        // If function G captures variable V from grandparent scope, then G's parent
        // function F must also capture V (even if F doesn't reference V directly).
        // This ensures V is available in F's scope at emit time when creating G's closure.
        // Process only functions whose parent gained a new edge; the queue is
        // the fixed-point worklist for the indexed function graph.
        {
            ArrayList* worklist = arraylist_new(mt->func_count + 1);
            bool* queued = (bool*)mem_calloc((size_t)mt->func_count,
                sizeof(bool), MEM_CAT_JS_RUNTIME);
            if (!worklist || (mt->func_count > 0 && !queued)) {
                if (worklist) arraylist_free(worklist);
                mem_free(queued);
                log_error("js-mir: capture propagation worklist allocation failed");
                return 0;
            }
            for (int i = 0; i < mt->func_count; i++) {
                arraylist_append(worklist, &mt->func_entries[i]);
                queued[mt->func_entries[i].function_id] = true;
            }
            for (int work = 0; work < worklist->length; work++) {
                JsFuncCollected* child = (JsFuncCollected*)arraylist_get(worklist, work);
                queued[child->function_id] = false;
                if (JM_CAPTURE_COUNT(child) == 0) continue;
                JsFuncCollected* parent = jm_parent_collected_func(mt, child);
                if (!parent) continue;

                    // A parent either owns the exact resolved binding or already
                    // captures that same entry. Name sets lose this distinction
                    // when minified scopes reuse one spelling.
                    for (int ci = 0; ci < JM_CAPTURE_COUNT(child); ci++) {
                        FnCapture* child_capture = &JM_CAPTURE_ARRAY(child)[ci];
                        const char* cap_name = child_capture->name;
                        bool cap_is_lexical_this = strcmp(cap_name, "_js_this") == 0;
                        if (cap_is_lexical_this && (!parent->node || !parent->node->is_arrow)) {
                            // A normal function supplies the lexical binding when it
                            // creates its direct arrow child. Only arrow ancestors
                            // must forward the binding through their closure env.
                            continue;
                        }
                        if (jm_parent_owns_capture(mt, parent, child_capture)) continue;

                        // Skip self-reference captures: a named function expression's name
                        // is only visible inside its own body (JS spec), not in the parent scope.
                        // Don't propagate it upward — the function resolves it from its own closure env.
                        if (child->node && child->node->name) {
                            const char* child_self_name = jm_var_name(child->node->name);
                            if (strcmp(cap_name, child_self_name) == 0) continue;
                        }

                        // A direct module binding was excluded during capture analysis.
                        // A remaining capture with the same spelling carries the resolved
                        // shadow-binding fact, so no ancestor name scan is needed here.
                        if (mt->module_consts) {
                            JsModuleConstEntry* mc_prop = jm_find_module_const_by_binding(mt,
                                child_capture->entry);
                            if (mc_prop && !JM_CAPTURE_ARRAY(child)[ci].force_env_capture) {
                                if (ci < JM_CAPTURE_COUNT(child) - 1) {
                                    memmove(&JM_CAPTURE_ARRAY(child)[ci], &JM_CAPTURE_ARRAY(child)[ci + 1],
                                        (JM_CAPTURE_COUNT(child) - ci - 1) * sizeof(JM_CAPTURE_ARRAY(child)[0]));
                                }
                                JM_CAPTURE_COUNT(child)--;
                                ci--;
                                continue;
                            }
                        }

                        if (strcmp(cap_name, "_js_new.target") == 0) {
                            // new.target is lexical for arrows but is not a real
                            // variable in the enclosing function. Keep it as the
                            // child's direct pseudo-capture so Phase 1.7 can seed a
                            // scope-env slot from the parent's runtime new.target,
                            // but do not propagate it as a parent closure capture.
                            continue;
                        }

                        bool cap_is_parent_nfe = false;
                        if (parent->node && parent->node->node_type == JS_AST_NODE_FUNCTION_EXPRESSION &&
                            parent->node->name) {
                            const char* parent_self_name = jm_var_name(parent->node->name);
                            cap_is_parent_nfe = (strcmp(cap_name, parent_self_name) == 0);
                        }

                        // Add as capture to parent
                        jm_ensure_captures_capacity(parent);
                        JM_CAPTURE_ARRAY(parent)[JM_CAPTURE_COUNT(parent)].name = jm_persist_name(cap_name);
                        JM_CAPTURE_ARRAY(parent)[JM_CAPTURE_COUNT(parent)].scope_env_key = jm_persist_name(
                            JM_CAPTURE_ARRAY(child)[ci].scope_env_key &&
                            JM_CAPTURE_ARRAY(child)[ci].scope_env_key &&
                            JM_CAPTURE_ARRAY(child)[ci].scope_env_key[0]
                                ? JM_CAPTURE_ARRAY(child)[ci].scope_env_key : cap_name);
                        JM_CAPTURE_ARRAY(parent)[JM_CAPTURE_COUNT(parent)].scope_env_slot = -1;
                        JM_CAPTURE_ARRAY(parent)[JM_CAPTURE_COUNT(parent)].private_env_slot = -1;
                        JM_CAPTURE_ARRAY(parent)[JM_CAPTURE_COUNT(parent)].grandparent_slot = -1;
                        JM_CAPTURE_ARRAY(parent)[JM_CAPTURE_COUNT(parent)].parent_env_link_slot_override = -1;
                        JM_CAPTURE_ARRAY(parent)[JM_CAPTURE_COUNT(parent)].entry = JM_CAPTURE_ARRAY(child)[ci].entry;
                        JM_CAPTURE_ARRAY(parent)[JM_CAPTURE_COUNT(parent)].is_let_const = JM_CAPTURE_ARRAY(child)[ci].is_let_const;
                        JM_CAPTURE_ARRAY(parent)[JM_CAPTURE_COUNT(parent)].is_const = JM_CAPTURE_ARRAY(child)[ci].is_const;
                        // A child closure can reference its enclosing named function
                        // expression's private name. Preserve that as an NFE binding
                        // so creation patches a private env slot instead of falling
                        // through to an outer same-named var.
                        JM_CAPTURE_ARRAY(parent)[JM_CAPTURE_COUNT(parent)].is_nfe_binding =
                            JM_CAPTURE_ARRAY(child)[ci].is_nfe_binding || cap_is_parent_nfe;
                        JM_CAPTURE_ARRAY(parent)[JM_CAPTURE_COUNT(parent)].force_env_capture = JM_CAPTURE_ARRAY(child)[ci].force_env_capture;
                        JM_CAPTURE_COUNT(parent)++;
                        if (!queued[parent->function_id]) {
                            arraylist_append(worklist, parent);
                            queued[parent->function_id] = true;
                        }
                        log_debug("js-mir: propagated capture '%s' [%s] from '%s' to parent '%s'",
                            cap_name, JM_CAPTURE_ARRAY(parent)[JM_CAPTURE_COUNT(parent) - 1].scope_env_key,
                            child->name, parent->name);
                    }
            }
            arraylist_free(worklist);
            mem_free(queued);
        }
    }

    // Phase 1.7: Compute shared scope envs for parent functions.
    // For each function F, the scope env contains the union of all variables
    // captured by F's direct child closures. All child closures share the same
    // scope env, enabling mutable capture semantics (JS captures by reference).
    //
    // NFE (named function expression) self-captures are excluded from the shared
    // pool and each gets a dedicated extra slot appended at the end of the scope
    // env. This prevents:
    // 1. Self-patch overwriting parent params/locals with the same name
    // 2. Multiple NFEs with the same name overwriting each other's self-references
    // Function declarations are NOT excluded — the parent manages their binding.
    {
        for (int fi = 0; fi < mt->func_count; fi++) {
            JsFuncCollected* parent_fc = &mt->func_entries[fi];
            parent_fc->has_scope_env = false;
            parent_fc->scope_env_count = 0;
            parent_fc->scope_env_normal_count = 0;

            // Collect union of all captures from direct children,
            // EXCLUDING true NFE self-captures (those get dedicated extra slots).
            // Function declaration self-captures are kept in the normal pool.
            struct hashmap* scope_vars = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);

            int nfe_extra_count = 0;
            for (int ci = 0; ci < mt->func_count; ci++) {
                JsFuncCollected* child = &mt->func_entries[ci];
                if (jm_parent_function_id(mt, child) != parent_fc->function_id) continue;
                if (JM_CAPTURE_COUNT(child) == 0) continue;
                // Determine child's NFE self-name (if any)
                const char* child_self_name = child->node && child->node->name
                    ? jm_var_name(child->node->name) : NULL;

                bool is_child_nfe = (child->node && child->node->node_type == JS_AST_NODE_FUNCTION_EXPRESSION);
                bool has_nfe_self_capture = false;
                for (int k = 0; k < JM_CAPTURE_COUNT(child); k++) {
                    const char* cname = JM_CAPTURE_ARRAY(child)[k].name;
                    const char* slot_key = jm_capture_scope_env_slot_key(mt, parent_fc, child, &JM_CAPTURE_ARRAY(child)[k]);
                    // Skip true NFE self-captures (child is a function expression, not declaration).
                    // Name alone is not enough: minified bundles often have an
                    // outer binding and an NFE self binding with the same name.
                    if (child_self_name && child_self_name[0] && strcmp(cname, child_self_name) == 0
                        && is_child_nfe && JM_CAPTURE_ARRAY(child)[k].is_nfe_binding) {
                        has_nfe_self_capture = true;
                        continue;
                    }
                    jm_name_set_add(scope_vars, slot_key);
                }
                if (has_nfe_self_capture) nfe_extra_count++;
            }

            int base_count = (int)hashmap_count(scope_vars);
            int total_needed = base_count + nfe_extra_count;

            if (total_needed > 0) {
                // Allocate scope_env_names (+2 for potential __parent_env__ and safety)
                parent_fc->scope_env_names = (const char**)mem_calloc(
                    total_needed + 2, sizeof(const char*), MEM_CAT_JS_RUNTIME);
                parent_fc->scope_env_bindings = (NameEntry**)mem_calloc(
                    total_needed + 2, sizeof(NameEntry*), MEM_CAT_JS_RUNTIME);

                // Re-iterate children in original order to fill names deterministically
                int fill_idx = 0;
                if (base_count > 0) {
                    hashmap_clear(scope_vars, false);
                    for (int ci = 0; ci < mt->func_count; ci++) {
                        JsFuncCollected* child = &mt->func_entries[ci];
                        if (jm_parent_function_id(mt, child) != parent_fc->function_id) continue;
                        if (JM_CAPTURE_COUNT(child) == 0) continue;
                        const char* child_self_name2 = child->node && child->node->name
                            ? jm_var_name(child->node->name) : NULL;

                        bool is_child_nfe2 = (child->node && child->node->node_type == JS_AST_NODE_FUNCTION_EXPRESSION);
                        for (int k = 0; k < JM_CAPTURE_COUNT(child); k++) {
                            const char* cname = JM_CAPTURE_ARRAY(child)[k].name;
                            const char* slot_key = jm_capture_scope_env_slot_key(mt, parent_fc, child, &JM_CAPTURE_ARRAY(child)[k]);
                            // Same skip as first pass: true NFE self-captures only.
                            if (child_self_name2 && child_self_name2[0] && strcmp(cname, child_self_name2) == 0
                                && is_child_nfe2 && JM_CAPTURE_ARRAY(child)[k].is_nfe_binding) {
                                continue;
                            }
                            if (!jm_name_set_has(scope_vars, slot_key)) {
                                jm_name_set_add(scope_vars, slot_key);
                                parent_fc->scope_env_names[fill_idx] = jm_persist_name(slot_key);
                                parent_fc->scope_env_bindings[fill_idx] =
                                    JM_CAPTURE_ARRAY(child)[k].entry;
                                fill_idx++;
                            }
                        }
                    }
                }
                int normal_slot_count = fill_idx;

                // Assign dedicated extra slots for true NFE self-captures.
                // Each NFE gets its own slot so self-patches don't conflict.
                // Function declarations are NOT given extra slots (parent manages them).
                int extra_slot = normal_slot_count;
                for (int ci = 0; ci < mt->func_count; ci++) {
                    JsFuncCollected* child = &mt->func_entries[ci];
                    if (jm_parent_function_id(mt, child) != parent_fc->function_id) continue;
                    if (!child->node || !child->node->name) continue;
                    const char* csn = jm_var_name(child->node->name);
                    // Only true NFEs (not function declarations) get extra slots
                    if (child->node->node_type != JS_AST_NODE_FUNCTION_EXPRESSION) continue;
                    bool assigned_nfe_slot = false;
                    for (int k = 0; k < JM_CAPTURE_COUNT(child); k++) {
                        if (strcmp(JM_CAPTURE_ARRAY(child)[k].name, csn) == 0 &&
                            JM_CAPTURE_ARRAY(child)[k].is_nfe_binding) {
                            JM_CAPTURE_ARRAY(child)[k].scope_env_slot = extra_slot;
                            assigned_nfe_slot = true;
                        }
                    }
                    if (assigned_nfe_slot) {
                        parent_fc->scope_env_names[extra_slot] = jm_persist_name(csn);
                        parent_fc->scope_env_bindings[extra_slot] = child->node->entry;
                        extra_slot++;
                    }
                }
                int slot_count = extra_slot;

                if (slot_count > 0) {
                    parent_fc->has_scope_env = true;
                    parent_fc->scope_env_count = slot_count;
                    parent_fc->scope_env_normal_count = normal_slot_count;
                    log_debug("js-mir: scope env for '%s': %d vars (%d normal + %d nfe extra)",
                        parent_fc->name, slot_count, normal_slot_count, slot_count - normal_slot_count);
                    for (int ds = 0; ds < slot_count; ds++) {
                        log_debug("js-mir:   scope_env[%d] = '%s'", ds, parent_fc->scope_env_names[ds]);
                    }

                    // Remap child capture indices to scope env slots
                    for (int ci = 0; ci < mt->func_count; ci++) {
                        JsFuncCollected* child = &mt->func_entries[ci];
                        if (jm_parent_function_id(mt, child) != parent_fc->function_id) continue;
                        if (JM_CAPTURE_COUNT(child) == 0) continue;
                        // Build child's NFE self-name to skip during remap
                        const char* child_self_remap = child->node && child->node->name
                            ? jm_var_name(child->node->name) : NULL;

                        bool is_child_nfe_remap = (child->node && child->node->node_type == JS_AST_NODE_FUNCTION_EXPRESSION);
                        for (int k = 0; k < JM_CAPTURE_COUNT(child); k++) {
                            // Skip true NFE self-captures — already assigned dedicated slots
                            if (child_self_remap && child_self_remap[0] &&
                                strcmp(JM_CAPTURE_ARRAY(child)[k].name, child_self_remap) == 0 &&
                                is_child_nfe_remap && JM_CAPTURE_ARRAY(child)[k].is_nfe_binding) {
                                continue;
                            }
                            int slot = jm_scope_env_slot_for_capture(parent_fc,
                                &JM_CAPTURE_ARRAY(child)[k]);
                            if (slot >= 0 && slot < normal_slot_count) {
                                JM_CAPTURE_ARRAY(child)[k].scope_env_slot = slot;
                            }
                        }
                    }
                }
            }

            hashmap_free(scope_vars);
        }
    }

    for (int ci = 0; ci < mt->func_count; ci++) {
        JsFuncCollected* child = &mt->func_entries[ci];
        JsFuncCollected* parent_fc = jm_parent_collected_func(mt, child);
        if (!parent_fc) continue;
        if (!parent_fc->has_scope_env || parent_fc->scope_env_count <= 0) continue;
        jm_mark_mixed_loop_parent_link(child, parent_fc);
    }

    // Phase 1.7.5: Js57 Track A — module-level scope env.
    // Top-level closures with no parent FunctionId can share captured block-lets via a
    // synthetic scope env allocated at js_main entry. Without this, each top-level
    // closure gets a per-closure env snapshot and mutations don't propagate between
    // siblings (regression: built_ins/ArrayBuffer/.../coerced-new-length-detach.js).
    //
    // Must run BEFORE Phase 1.7b so reuse_parent_env can see the remapped slots
    // — without that ordering, a single top-level arrow that contains a nested
    // valueOf both capturing the same block-let cannot collapse to a shared env.
    //
    // Filter:
    //   * only let/const captures (var bindings are function-scoped and hoisted
    //     into js_module_vars[]);
    //   * exclude module-level top-level let/const stored in module_consts —
    //     those already share state via js_get/set_module_var;
    //   * loop lexical bindings may still receive slot numbers for copied-env
    //     layout, but they are not shared through the module env;
    //   * exclude NFE self-bindings (private to the closure they live in).
    // The existing closure-creation guard (iteration_depth > 0 + is_let_const →
    // fall back to per-closure env) protects loop-body block-lets even if they
    // pass these filters.
    memset(&mt->module_fc, 0, sizeof(mt->module_fc));
    // The module carrier is never an AST function. Keep its sentinel identity
    // valid even when the program needs no shared module environment.
    mt->module_fc.function_id = AST_FUNCTION_ID_INVALID;
    {
        struct hashmap* scope_vars = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);

        auto capture_qualifies = [&](JsFuncCollected* child, FnCapture* cap) -> bool {
            if (!cap) return false;
            const char* name = cap->name;
            if (!cap->is_let_const) return false;
            if (cap->is_nfe_binding) return false;
            if (mt->module_consts) {
                JsModuleConstEntry* mc = jm_find_module_const_by_binding(mt,
                    cap->entry);
                if (mc && jm_capture_uses_live_module_var(mt, cap)) return false;
            }
            if (strcmp(name, "_js_this") == 0 ||
                strcmp(name, "_js_new.target") == 0 ||
                strcmp(name, "_js_arguments") == 0) return false;
            return true;
        };

        auto capture_slot_key = [&](FnCapture* cap) -> const char* {
            if (!cap) return "";
            if (cap->scope_env_key && cap->scope_env_key[0] &&
                    strcmp(cap->scope_env_key, cap->name) != 0 &&
                    cap->force_env_capture) return cap->scope_env_key;
            return cap->name;
        };

        auto capture_is_shared_module_binding = [&](JsFuncCollected* child, FnCapture* cap) -> bool {
            if (!cap) return false;
            if (!capture_qualifies(child, cap)) {
                return false;
            }
            // Per-iteration bindings cannot use the module carrier: each
            // closure must retain its own current loop cell.
            return !jm_capture_is_loop_private_in_root((JsAstNode*)program,
                child, cap);
        };

        auto capture_needs_private_module_slot = [&](JsFuncCollected* child,
                FnCapture* cap) -> bool {
            return !capture_is_shared_module_binding(child, cap) &&
                !jm_capture_uses_live_module_var(mt, cap);
        };

        auto closure_needs_mixed_module_env = [&](JsFuncCollected* child) -> bool {
            if (!child) return false;
            bool has_private = false;
            bool has_shared = false;
            for (int k = 0; k < JM_CAPTURE_COUNT(child); k++) {
                FnCapture* cap = &JM_CAPTURE_ARRAY(child)[k];
                if (capture_is_shared_module_binding(child, cap)) {
                    has_shared = true;
                } else if (capture_needs_private_module_slot(child, cap)) {
                    has_private = true;
                }
            }
            return has_private && has_shared;
        };

        for (int ci = 0; ci < mt->func_count; ci++) {
            JsFuncCollected* child = &mt->func_entries[ci];
            if (jm_parent_function_id(mt, child) != AST_FUNCTION_ID_INVALID) continue;
            if (JM_CAPTURE_COUNT(child) == 0) continue;
            for (int k = 0; k < JM_CAPTURE_COUNT(child); k++) {
                if (!capture_qualifies(child, &JM_CAPTURE_ARRAY(child)[k])) continue;
                jm_name_set_add(scope_vars, capture_slot_key(&JM_CAPTURE_ARRAY(child)[k]));
            }
        }

        int total = (int)hashmap_count(scope_vars);
        if (total > 0) {
            int scope_env_capacity = total + 2;
            for (int ci = 0; ci < mt->func_count; ci++) {
                JsFuncCollected* child = &mt->func_entries[ci];
                if (jm_parent_function_id(mt, child) != AST_FUNCTION_ID_INVALID ||
                        !closure_needs_mixed_module_env(child)) continue;
                int private_count = 0;
                for (int k = 0; k < JM_CAPTURE_COUNT(child); k++) {
                    if (capture_needs_private_module_slot(child, &JM_CAPTURE_ARRAY(child)[k])) {
                        private_count++;
                    }
                }
                int required = total + private_count + 1;
                if (JM_CAPTURE_COUNT(child) + 1 > required) required = JM_CAPTURE_COUNT(child) + 1;
                if (required > scope_env_capacity) scope_env_capacity = required;
            }
            mt->module_fc.has_scope_env = true;
            mt->module_fc.scope_env_count = total;
            mt->module_fc.scope_env_normal_count = total;
            mt->module_fc.scope_env_names = (const char**)mem_calloc(
                scope_env_capacity, sizeof(const char*), MEM_CAT_JS_RUNTIME);
            mt->module_fc.scope_env_bindings = (NameEntry**)mem_calloc(
                scope_env_capacity, sizeof(NameEntry*), MEM_CAT_JS_RUNTIME);

            // Deterministic fill: iterate children in collection order
            hashmap_clear(scope_vars, false);
            int fill_idx = 0;
            for (int ci = 0; ci < mt->func_count; ci++) {
                JsFuncCollected* child = &mt->func_entries[ci];
                if (jm_parent_function_id(mt, child) != AST_FUNCTION_ID_INVALID) continue;
                for (int k = 0; k < JM_CAPTURE_COUNT(child); k++) {
                    if (!capture_qualifies(child, &JM_CAPTURE_ARRAY(child)[k])) continue;
                    const char* key = capture_slot_key(&JM_CAPTURE_ARRAY(child)[k]);
                    if (!jm_name_set_has(scope_vars, key)) {
                        jm_name_set_add(scope_vars, key);
                        mt->module_fc.scope_env_names[fill_idx] = jm_persist_name(key);
                        mt->module_fc.scope_env_bindings[fill_idx] =
                            JM_CAPTURE_ARRAY(child)[k].entry;
                        fill_idx++;
                    }
                }
            }

            // Remap child capture slots to point at module scope env positions.
            // Slots stay -1 for closures that didn't qualify (any in-loop /
            // for-init capture disqualifies the whole closure) so the existing
            // per-closure-env fallback handles them.
            for (int ci = 0; ci < mt->func_count; ci++) {
                JsFuncCollected* child = &mt->func_entries[ci];
                if (jm_parent_function_id(mt, child) != AST_FUNCTION_ID_INVALID) continue;
                for (int k = 0; k < JM_CAPTURE_COUNT(child); k++) {
                    if (!capture_qualifies(child, &JM_CAPTURE_ARRAY(child)[k])) continue;
                    int slot = jm_scope_env_slot_for_capture(&mt->module_fc,
                        &JM_CAPTURE_ARRAY(child)[k]);
                    if (slot >= 0 && slot < total) {
                        JM_CAPTURE_ARRAY(child)[k].scope_env_slot = slot;
                    }
                }
                if (closure_needs_mixed_module_env(child)) {
                    // A closure that also has private captures cannot reuse the
                    // module env directly; link its copied env back so shared
                    // lexical cells remain live instead of freezing their TDZ value.
                    int private_slot = total;
                    for (int k = 0; k < JM_CAPTURE_COUNT(child); k++) {
                        FnCapture* cap = &JM_CAPTURE_ARRAY(child)[k];
                        if (capture_needs_private_module_slot(child, cap)) {
                            cap->private_env_slot = private_slot++;
                        }
                    }
                    JM_JS_FACT(child, closure_env_has_parent_link) = true;
                    JM_JS_FACT(child, closure_env_parent_link_slot) =
                        jm_parent_link_slot_after_captures(child, total);
                    if (JM_JS_FACT(child, closure_env_parent_link_slot) >= mt->module_fc.scope_env_count) {
                        // Generated transitive loads address the parent-link tail
                        // by layout, so reserve that tail in the module env too.
                        mt->module_fc.has_parent_env_link = true;
                        mt->module_fc.scope_env_count =
                            JM_JS_FACT(child, closure_env_parent_link_slot) + 1;
                        mt->module_fc.scope_env_names[
                            JM_JS_FACT(child, closure_env_parent_link_slot)] = jm_persist_name("__parent_env__");
                    }
                    for (int k = 0; k < JM_CAPTURE_COUNT(child); k++) {
                        FnCapture* cap = &JM_CAPTURE_ARRAY(child)[k];
                        if (capture_is_shared_module_binding(child, cap) && cap->scope_env_slot >= 0) {
                            // copied envs keep loop lets private; outer module
                            // lets must still mutate the shared parent slot.
                            cap->grandparent_slot = cap->scope_env_slot;
                        }
                    }
                    log_debug("js-mir: module mixed closure '%s' uses parent env slot %d",
                        child->name, JM_JS_FACT(child, closure_env_parent_link_slot));
                }
            }

            log_debug("js-mir: Phase 1.7.5: module scope env with %d slots", total);
            for (int s = 0; s < total; s++) {
                log_debug("js-mir:   module_scope_env[%d] = '%s'", s, mt->module_fc.scope_env_names[s]);
            }
        }

        hashmap_free(scope_vars);
    }

    // Phase 1.7b: Detect parent env reuse for transitively captured scope envs.
    // If ALL scope_env variables of a function are also in that function's own
    // captures (i.e., they are transitive captures from the grandparent), the
    // function can skip allocating a new scope_env and reuse the parent env.
    // Children's capture slots are remapped to the grandparent env slots.
    //
    // IMPORTANT: Iterate in REVERSE order (outermost functions first).
    // func_entries has inner closures at lower indices than their parents.
    // Phase 1.7b for a function reads its captures' scope_env_slots, which
    // are set by Phase 1.7b of its PARENT. Processing parents first ensures
    // the captures are already remapped to grandparent slots before children
    // try to use them as "grandparent" slots for their own grandchildren.
    for (int fi = mt->func_count - 1; fi >= 0; fi--) {
        JsFuncCollected* parent_fc = &mt->func_entries[fi];
        JM_JS_FACT(parent_fc, reuse_parent_env) = false;
        JM_JS_FACT(parent_fc, reuse_env_slot_count) = 0;
        if (!parent_fc->has_scope_env || parent_fc->scope_env_count == 0) continue;
        if (JM_CAPTURE_COUNT(parent_fc) == 0) continue;  // not a closure, can't reuse

        // Check if ALL scope_env vars are also in this function's own captures
        bool all_transitive = true;
        for (int s = 0; s < parent_fc->scope_env_count; s++) {
            bool found_in_captures = false;
            for (int c = 0; c < JM_CAPTURE_COUNT(parent_fc); c++) {
                if (jm_scope_env_slot_matches_capture(parent_fc, s,
                        &JM_CAPTURE_ARRAY(parent_fc)[c])) {
                    found_in_captures = true;
                    break;
                }
            }
            if (!found_in_captures) {
                all_transitive = false;
                break;
            }
        }

        if (!all_transitive) continue;

        // All scope_env vars are transitive captures. Remap children's captures
        // to use the grandparent env slots instead of this function's local scope_env slots.
        JM_JS_FACT(parent_fc, reuse_parent_env) = true;
        int max_slot = 0;
        for (int s = 0; s < parent_fc->scope_env_count; s++) {
            // Find this scope_env var in parent_fc's own captures to get grandparent slot
            for (int c = 0; c < JM_CAPTURE_COUNT(parent_fc); c++) {
                if (jm_scope_env_slot_matches_capture(parent_fc, s,
                        &JM_CAPTURE_ARRAY(parent_fc)[c])) {
                    // Propagated captures in a mixed module closure can live in
                    // private tail slots. Its nested closures reuse that actual
                    // incoming layout, not the original module scope slot.
                    int grandparent_slot = jm_capture_env_slot(
                        &JM_CAPTURE_ARRAY(parent_fc)[c], c);
                    if (grandparent_slot < 0) {
                        // Can't remap — grandparent doesn't use scope_env for this var
                        JM_JS_FACT(parent_fc, reuse_parent_env) = false;
                        break;
                    }
                    if (grandparent_slot + 1 > max_slot) max_slot = grandparent_slot + 1;

                    // Remap all children's captures of this var
                    for (int ci = 0; ci < mt->func_count; ci++) {
                        JsFuncCollected* child = &mt->func_entries[ci];
                        if (jm_parent_function_id(mt, child) != parent_fc->function_id) continue;
                        for (int k = 0; k < JM_CAPTURE_COUNT(child); k++) {
                            if (jm_captures_same_binding(&JM_CAPTURE_ARRAY(child)[k],
                                    &JM_CAPTURE_ARRAY(parent_fc)[c])) {
                                JM_CAPTURE_ARRAY(child)[k].scope_env_slot = grandparent_slot;
                                // Mixed loop closures record this same binding as
                                // grandparent_slot; leaving its pre-reuse slot here
                                // makes nested arrows read an unrelated parent value.
                                if (JM_CAPTURE_ARRAY(child)[k].grandparent_slot >= 0) {
                                    JM_CAPTURE_ARRAY(child)[k].grandparent_slot = grandparent_slot;
                                }
                                break;
                            }
                        }
                    }
                    break;
                }
            }
            if (!JM_JS_FACT(parent_fc, reuse_parent_env)) break;  // aborted
        }

        if (JM_JS_FACT(parent_fc, reuse_parent_env)) {
            JM_JS_FACT(parent_fc, reuse_env_slot_count) = max_slot;
            log_debug("js-mir: Phase 1.7b: '%s' will reuse parent env (all %d scope_env vars are transitive captures, slot_count=%d)",
                parent_fc->name, parent_fc->scope_env_count, max_slot);
        }
    }

    // Phase 1.7c: Parent env link for mixed scope envs.
    // When a function's scope env has BOTH local vars AND transitive captures,
    // the transitive captures become stale after the function returns (the grandparent
    // may modify them later). Fix: store the parent env pointer in slot 0 of the scope env,
    // shift all other slots by 1, and mark transitive captures so children read them
    // from the grandparent env (via the parent env link) instead of from the stale copy.
    for (int fi = mt->func_count - 1; fi >= 0; fi--) {
        JsFuncCollected* parent_fc = &mt->func_entries[fi];
        parent_fc->has_parent_env_link = false;
        JM_JS_FACT(parent_fc, parent_env_link_uses_grandparent) = false;
        JM_JS_FACT(parent_fc, has_immediate_parent_env_link) = false;
        JM_JS_FACT(parent_fc, immediate_parent_env_link_slot) = -1;
        if (!parent_fc->has_scope_env || parent_fc->scope_env_count == 0) continue;
        if (JM_JS_FACT(parent_fc, reuse_parent_env)) continue;  // Phase 1.7b already handles pure-transitive
        if (JM_CAPTURE_COUNT(parent_fc) == 0) continue; // no captures = no transitive vars possible

        JsFunctionNode* parent_fn = parent_fc->node;

        // Check if scope env has any transitive captures (vars that are also in parent_fc's captures)
        // Only count captures that the parent reads from its own parent's scope env
        // (scope_env_slot >= 0), NOT module vars read via js_get_module_var.
        // Also exclude vars that are LOCAL to the parent (shadowing the capture).
        bool has_transitive = false;
        bool has_local = false;
        bool parent_link_uses_grandparent = false;
        for (int s = 0; s < parent_fc->scope_env_count; s++) {
            bool is_capture = false;
            NameEntry* binding = jm_scope_env_binding_at(parent_fc, s);
            // A resolved binding tells whether the parent owns this cell; a
            // source-name set cannot distinguish an outer capture from a local
            // shadow with the same spelling.
            bool is_parent_local = binding && parent_fn &&
                jm_entry_is_owned_by_function(parent_fn, binding);
            if (!is_parent_local) {
                for (int c = 0; c < JM_CAPTURE_COUNT(parent_fc); c++) {
                    if (jm_scope_env_slot_matches_capture(parent_fc, s,
                            &JM_CAPTURE_ARRAY(parent_fc)[c])) {
                        // any parent capture is transitive for the child; if it is
                        // not backed by a shared scope_env slot, the parent's dense
                        // closure-env capture slot is still the live binding cell.
                        is_capture = true;
                        if (JM_CAPTURE_ARRAY(parent_fc)[c].grandparent_slot >= 0) {
                            parent_link_uses_grandparent = true;
                        }
                        break;
                    }
                }
            }
            if (is_capture) has_transitive = true;
            else has_local = true;
        }

        if (!has_transitive) {
            continue; // pure-local scope envs do not need a parent link
        }
        // a non-reused env with a transitive capture must preserve the original
        // parent binding cell; local-name collection can miss mixed callback
        // shapes, and copying the transitive value makes sibling closures mutate
        // independent cells (e.g. captured --waiting in nested event callbacks).
        (void)has_local;

        bool needs_immediate_parent_link = false;
        if (parent_link_uses_grandparent) {
            for (int ci = 0; ci < mt->func_count && !needs_immediate_parent_link; ci++) {
                JsFuncCollected* child = &mt->func_entries[ci];
                if (jm_parent_function_id(mt, child) != parent_fc->function_id) continue;
                for (int k = 0; k < JM_CAPTURE_COUNT(child) && !needs_immediate_parent_link; k++) {
                    for (int pc = 0; pc < JM_CAPTURE_COUNT(parent_fc); pc++) {
                        FnCapture* parent_cap = &JM_CAPTURE_ARRAY(parent_fc)[pc];
                        if (!jm_captures_same_binding(&JM_CAPTURE_ARRAY(child)[k],
                                parent_cap) ||
                            parent_cap->grandparent_slot >= 0 ||
                            parent_cap->scope_env_slot < 0) continue;
                        if (jm_capture_binding_starts_after_function(parent_fc, parent_cap)) {
                            needs_immediate_parent_link = true;
                            break;
                        }
                    }
                }
            }
        }

        // Mixed scope env: add parent env link at the LAST slot (no shifting needed).
        parent_fc->has_parent_env_link = true;
        JM_JS_FACT(parent_fc, parent_env_link_uses_grandparent) = parent_link_uses_grandparent;
        int immediate_parent_env_link_slot = -1;
        if (needs_immediate_parent_link) {
            JM_JS_FACT(parent_fc, has_immediate_parent_env_link) = true;
            immediate_parent_env_link_slot = parent_fc->scope_env_count;
            JM_JS_FACT(parent_fc, immediate_parent_env_link_slot) = immediate_parent_env_link_slot;
            parent_fc->scope_env_names[parent_fc->scope_env_count] =
                jm_persist_name("__immediate_parent_env__");
            parent_fc->scope_env_count++;
        }
        int parent_env_link_slot = parent_fc->scope_env_count; // last slot = parent env pointer
        // scope_env_names was allocated with +2 extra slots for this
        parent_fc->scope_env_names[parent_fc->scope_env_count] =
            jm_persist_name("__parent_env__");
        parent_fc->scope_env_count++;

        // For transitive captures in direct children, set grandparent_slot
        // NO slot shifting needed — existing slots remain unchanged
        for (int ci = 0; ci < mt->func_count; ci++) {
            JsFuncCollected* child = &mt->func_entries[ci];
            if (jm_parent_function_id(mt, child) != parent_fc->function_id) continue;
            if (JM_CAPTURE_COUNT(child) == 0) continue;

            for (int k = 0; k < JM_CAPTURE_COUNT(child); k++) {
                bool field_initializer_arrow = false;
                if (child->node && child->node->is_arrow &&
                    JM_JS_FACT(parent_fc, is_class_field_initializer)) {
                    field_initializer_arrow = true;
                }
                if (field_initializer_arrow &&
                    jm_capture_is_lexical_meta_binding(JM_CAPTURE_ARRAY(child)[k].name)) {
                    // field initializer arrows own a snapshot of lexical this;
                    // following the enclosing closure's parent link loses the instance.
                    JM_CAPTURE_ARRAY(child)[k].grandparent_slot = -1;
                    JM_CAPTURE_ARRAY(child)[k].parent_env_link_slot_override = -1;
                    continue;
                }
                // A local parent binding has its own scope cell, not the
                // inherited link that a transitive capture needs.
                if (parent_fn && JM_CAPTURE_ARRAY(child)[k].entry &&
                        jm_entry_is_owned_by_function(parent_fn,
                            JM_CAPTURE_ARRAY(child)[k].entry)) continue;

                // Check if this capture is a transitive capture (also in parent_fc's captures)
                // Only for captures the parent reads from its own parent's scope env
                for (int pc = 0; pc < JM_CAPTURE_COUNT(parent_fc); pc++) {
                    if (jm_captures_same_binding(&JM_CAPTURE_ARRAY(child)[k],
                            &JM_CAPTURE_ARRAY(parent_fc)[pc])) {
                        int grandparent_slot = JM_CAPTURE_ARRAY(parent_fc)[pc].grandparent_slot;
                        if (grandparent_slot >= 0) {
                            JM_CAPTURE_ARRAY(child)[k].grandparent_slot = grandparent_slot;
                        } else if (JM_CAPTURE_ARRAY(parent_fc)[pc].scope_env_slot >= 0) {
                            if (!parent_link_uses_grandparent) {
                                // the parent link names the immediate parent env here;
                                // reusing scope_env_slot would collide with the child's
                                // own scope-env layout in mixed callback closures.
                                JM_CAPTURE_ARRAY(child)[k].grandparent_slot = JM_CAPTURE_ARRAY(parent_fc)[pc].scope_env_slot;
                            } else {
                                if (immediate_parent_env_link_slot >= 0) {
                                    // The default link skips to the grandparent, but this
                                    // capture is owned by the immediate parent. Preserve
                                    // its late-initialized cell through the direct link.
                                    JM_CAPTURE_ARRAY(child)[k].grandparent_slot =
                                        JM_CAPTURE_ARRAY(parent_fc)[pc].scope_env_slot;
                                    JM_CAPTURE_ARRAY(child)[k].parent_env_link_slot_override =
                                        immediate_parent_env_link_slot;
                                } else {
                                    JM_CAPTURE_ARRAY(child)[k].scope_env_slot = -1;
                                    JM_CAPTURE_ARRAY(child)[k].grandparent_slot = -1;
                                    break;
                                }
                            }
                        } else if (!parent_link_uses_grandparent) {
                            // parent closures that do not use a shared scope env
                            // store captures densely by capture index.
                            JM_CAPTURE_ARRAY(child)[k].grandparent_slot = pc;
                        } else {
                            break;
                        }
                        log_debug("js-mir: Phase 1.7c: capture '%s' in '%s' → grandparent slot %d (parent env at slot %d)",
                            JM_CAPTURE_ARRAY(child)[k].name, child->name, JM_CAPTURE_ARRAY(child)[k].grandparent_slot, parent_env_link_slot);
                        break;
                    }
                }
            }
        }

        log_debug("js-mir: Phase 1.7c: '%s' has parent env link at slot %d (mixed scope env, %d slots)",
            parent_fc->name, parent_env_link_slot, parent_fc->scope_env_count);
    }

    // Phase 1.7d: A function cannot reuse a direct parent's mixed scope env.
    // Mixed scope envs contain local slots plus a parent-env link. Reusing that
    // env as if it were the grandparent env makes grandchildren read stale or
    // unrelated slots for later-initialized lexical captures.
    for (int fi = 0; fi < mt->func_count; fi++) {
        JsFuncCollected* fc = &mt->func_entries[fi];
        if (!JM_JS_FACT(fc, reuse_parent_env)) continue;
        JsFuncCollected* parent_fc = jm_parent_collected_func(mt, fc);
        if (!parent_fc) continue;
        if (!parent_fc->has_parent_env_link) continue;

        JM_JS_FACT(fc, reuse_parent_env) = false;
        JM_JS_FACT(fc, reuse_env_slot_count) = 0;
        fc->has_parent_env_link = true;
        JM_JS_FACT(fc, parent_env_link_uses_grandparent) = true;
        // A mixed parent needs two distinct links: the inherited link preserves
        // transitive captures, while this direct link keeps parent-local cells
        // shared with sibling closures instead of copying stale values.
        JM_JS_FACT(fc, has_immediate_parent_env_link) = true;
        JM_JS_FACT(fc, immediate_parent_env_link_slot) = fc->scope_env_count;
        fc->scope_env_names[fc->scope_env_count] = jm_persist_name(
            "__immediate_parent_env__");
        fc->scope_env_count++;
        int parent_env_link_slot = fc->scope_env_count;
        fc->scope_env_names[fc->scope_env_count] = jm_persist_name("__parent_env__");
        fc->scope_env_count++;
        for (int ci = 0; ci < mt->func_count; ci++) {
            JsFuncCollected* child = &mt->func_entries[ci];
            if (jm_parent_function_id(mt, child) != fc->function_id) continue;
            for (int k = 0; k < JM_CAPTURE_COUNT(child); k++) {
                for (int s = 0; s < fc->scope_env_count; s++) {
                    if (jm_scope_env_slot_matches_capture(fc, s,
                            &JM_CAPTURE_ARRAY(child)[k])) {
                        JM_CAPTURE_ARRAY(child)[k].scope_env_slot = s;
                        JM_CAPTURE_ARRAY(child)[k].grandparent_slot = -1;
                        JM_CAPTURE_ARRAY(child)[k].parent_env_link_slot_override = -1;
                        for (int c = 0; c < JM_CAPTURE_COUNT(fc); c++) {
                            if (!jm_captures_same_binding(&JM_CAPTURE_ARRAY(child)[k],
                                    &JM_CAPTURE_ARRAY(fc)[c])) continue;
                            if (JM_CAPTURE_ARRAY(fc)[c].grandparent_slot >= 0) {
                                JM_CAPTURE_ARRAY(child)[k].grandparent_slot =
                                    JM_CAPTURE_ARRAY(fc)[c].grandparent_slot;
                            } else if (JM_CAPTURE_ARRAY(fc)[c].scope_env_slot >= 0) {
                                // This binding belongs to the immediate mixed
                                // parent. A copied compact slot would freeze its
                                // pre-assignment value and split sibling closures.
                                JM_CAPTURE_ARRAY(child)[k].grandparent_slot =
                                    JM_CAPTURE_ARRAY(fc)[c].scope_env_slot;
                                JM_CAPTURE_ARRAY(child)[k].parent_env_link_slot_override =
                                    JM_JS_FACT(fc, immediate_parent_env_link_slot);
                            }
                            break;
                        }
                        break;
                    }
                }
            }
            // if this direct child also has a scope env, its own children should
            // use the child's compact slots for these bindings. Phase 1.7c may
            // have already marked them as grandparent reads before 1.7d changed
            // this function from parent-env reuse to an owned compact env.
            if (child->has_scope_env) {
                // A child that still reuses its parent has no independent cell
                // layout: its children must follow this newly owned environment.
                // Remapping them through the child's pre-reuse layout aliases
                // unrelated siblings when the slot orders differ.
                JsFuncCollected* capture_env_owner = JM_JS_FACT(child, reuse_parent_env) ? fc : child;
                for (int gi = 0; gi < mt->func_count; gi++) {
                    JsFuncCollected* grandchild = &mt->func_entries[gi];
                    if (jm_parent_function_id(mt, grandchild) != child->function_id) continue;
                    for (int gk = 0; gk < JM_CAPTURE_COUNT(grandchild); gk++) {
                        for (int s = 0; s < capture_env_owner->scope_env_count; s++) {
                            if (jm_scope_env_slot_matches_capture(capture_env_owner,
                                    s, &JM_CAPTURE_ARRAY(grandchild)[gk])) {
                                JM_CAPTURE_ARRAY(grandchild)[gk].scope_env_slot = s;
                                JM_CAPTURE_ARRAY(grandchild)[gk].grandparent_slot = -1;
                                JM_CAPTURE_ARRAY(grandchild)[gk].parent_env_link_slot_override = -1;
                                if (capture_env_owner == child) {
                                    for (int c = 0; c < JM_CAPTURE_COUNT(child); c++) {
                                        if (jm_captures_same_binding(
                                                &JM_CAPTURE_ARRAY(grandchild)[gk],
                                                &JM_CAPTURE_ARRAY(child)[c]) &&
                                            JM_CAPTURE_ARRAY(child)[c].scope_env_slot >= 0 &&
                                            JM_CAPTURE_ARRAY(child)[c].grandparent_slot < 0) {
                                            // A capture owned by the mixed direct
                                            // parent must not become a stale copy
                                            // in this compact env (Splide's sibling
                                            // transition callback assignment).
                                            JM_CAPTURE_ARRAY(grandchild)[gk].grandparent_slot =
                                                JM_CAPTURE_ARRAY(child)[c].scope_env_slot;
                                            JM_CAPTURE_ARRAY(grandchild)[gk].parent_env_link_slot_override =
                                                JM_JS_FACT(child, immediate_parent_env_link_slot);
                                            break;
                                        }
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
        log_debug("js-mir: Phase 1.7d: '%s' will allocate own scope env with parent link at slot %d because parent '%s' has mixed scope env",
            fc->name, parent_env_link_slot, parent_fc->name);
    }

    // Phase 1.75: Infer parameter and return types for each function
    for (int i = 0; i < mt->func_count; i++) {
        JsFuncCollected* fc = &mt->func_entries[i];
        jm_infer_param_types(mt, fc);
        jm_infer_return_type(mt, fc);
        // P1: Compute native eligibility here (Phase 1.75) rather than lazily in jm_define_function.
        // This lets jm_resolve_native_call() see the selected native ABI
        // when transpiling earlier functions that call later-defined native functions, enabling
        // `let x = f(...)` to propagate f's return type into x's variable type.
        // Native specialization cannot use duplicate MIR param names, and arrow
        // block bodies still need boxed statement-completion return handling.
        bool eligible = (JM_CAPTURE_COUNT(fc) == 0 && JM_PARAM_COUNT(fc) > 0 &&
                         !JM_JS_FACT(fc, uses_arguments) &&
                         !JM_JS_FACT(fc, has_duplicate_param_names) &&
                         !(fc->node->is_arrow && fc->node->body &&
                           fc->node->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) &&
                         !JM_JS_FACT(fc, has_non_simple_params) &&
                         (JM_JS_FACT(fc, return_type) == LMD_TYPE_INT || JM_JS_FACT(fc, return_type) == LMD_TYPE_FLOAT));
        bool has_native_param = false;
        if (eligible) {
            for (int j = 0; j < JM_PARAM_COUNT(fc); j++) {
                TypeId param_type = jm_param_type(fc, j);
                if (param_type == LMD_TYPE_INT || param_type == LMD_TYPE_FLOAT) {
                    has_native_param = true;
                    continue;
                }
                if (param_type != LMD_TYPE_ANY) {
                    eligible = false;
                    break;
                }
            }
        }
        if (!has_native_param) eligible = false;
        JM_JS_FACT(fc, native_return_kind) = !eligible ? NATIVE_RETURN_NONE :
            JM_JS_FACT(fc, return_type) == LMD_TYPE_FLOAT ? NATIVE_RETURN_FLOAT : NATIVE_RETURN_INT;
        if (eligible) {
            log_debug("js-mir P1/P4: %s eligible for native version (params: %d, ret: %s)",
                fc->name, JM_PARAM_COUNT(fc),
                JM_JS_FACT(fc, return_type) == LMD_TYPE_INT ? "INT" : "FLOAT");
        }

        // Mixed native/Item entries keep the Item formal stable across every
        // tail iteration. Retain TCO for all-native signatures only.
        JM_JS_FACT(fc, is_tco_eligible) = false;
        if (eligible && has_native_param) {
            for (int j = 0; j < JM_PARAM_COUNT(fc); j++) {
                if (jm_param_type(fc, j) == LMD_TYPE_ANY) {
                    has_native_param = false;
                    break;
                }
            }
        }
        if (eligible && has_native_param && jm_has_tail_call(mt, fc)) {
            JM_JS_FACT(fc, is_tco_eligible) = true;
            log_debug("js-mir TCO: %s eligible for tail-call optimization", fc->name);
        }
    }

    // Phase 1.9: Create forward declarations for all functions.
    // This ensures func_item is set for all functions before any body is compiled,
    // so forward references (e.g., a class method calling a free function declared
    // later in the source) resolve through live module bindings and direct calls.

    // Phase 1.76: Call-site propagation — scan all function bodies for call
    // expressions that pass literal arguments contradicting inferred param types.
    // Widen mismatched params to ANY and revoke native eligibility.
    jm_callsite_propagate(mt, program->body);


    for (int i = 0; i < mt->func_count; i++) {
        JsFuncCollected* fc = &mt->func_entries[i];
        JM_JS_FACT(fc, boxed_return_scalar_class) = jm_infer_boxed_return_scalar_class(mt, fc);
        FnAnalysis* analysis = jm_function_analysis(fc);
        analysis->variant_count = 0;
        FnVariantAnalysis* public_entry =
            &analysis->variants[analysis->variant_count++];
        memset(public_entry, 0, sizeof(*public_entry));
        public_entry->entry = {FN_ENTRY_PUBLIC_WRAPPER, false,
            JM_JS_FACT(fc, has_direct_eval), JM_JS_FACT(fc, uses_arguments), true};
        public_entry->effects = {true, true, true, false,
            fc->node->is_async || fc->node->is_generator, true};
        ScalarReturnClass scalar_class = JM_JS_FACT(fc, boxed_return_scalar_class);
        public_entry->result.normal = {JM_JS_FACT(fc, return_type), VALUE_REP_ITEM,
            scalar_class};
        // The public wrapper is C-reachable, so its companion travels through
        // Context rather than an ABI argument.  Keep the semantic scalar fact
        // in `normal`; the scalar-home mask is reserved for the retired v2
        // caller-donated transport.
        public_entry->result.shape = em_return_shape(false, false,
            em_scalar_return_mode_for_class(scalar_class));
        public_entry->result.companion = em_companion_transport(
            public_entry->result.shape, /*c_reachable=*/true);
        int env_param_count = JM_CAPTURE_COUNT(fc) > 0 ? 1 : 0;
        int physical_param_count = JM_PARAM_COUNT(fc) + env_param_count;
        public_entry->param_count = physical_param_count + 1;
        if (physical_param_count > 0) {
            public_entry->params = (FnParamAnalysis*)pool_calloc(
                mt->tp->pool, sizeof(FnParamAnalysis) * (size_t)physical_param_count);
            for (int p = 0; p < physical_param_count; p++) {
                bool env = env_param_count && p == 0;
                // the conditional mixes an enum constant with TypeId; make the ABI-width field explicit for Clang.
                TypeId param_type = env ? (TypeId)LMD_TYPE_ANY :
                    jm_param_type(fc, p - env_param_count);
                public_entry->params[p] = {param_type,
                    env ? VALUE_REP_RAW_GC_POINTER : VALUE_REP_ITEM, 0};
            }
        }

        FnVariantAnalysis* body =
            &analysis->variants[analysis->variant_count++];
        memset(body, 0, sizeof(*body));
        body->entry = {FN_ENTRY_BOXED_BODY, true, JM_JS_FACT(fc, has_direct_eval),
            JM_JS_FACT(fc, uses_arguments), false};
        body->effects = public_entry->effects;
        body->result.normal = {JM_JS_FACT(fc, return_type), VALUE_REP_ITEM,
            scalar_class};
        body->result.shape = public_entry->result.shape;
        // Internal boxed bodies keep lane 2 in a MIR result register.  This
        // avoids a number-home copy on every generated JS-to-JS call.
        body->result.companion = em_companion_transport(body->result.shape,
            /*c_reachable=*/false);
        body->param_count = physical_param_count;
        if (physical_param_count > 0) {
            body->params = (FnParamAnalysis*)pool_calloc(
            mt->tp->pool, sizeof(FnParamAnalysis) * (size_t)physical_param_count);
            for (int p = 0; p < physical_param_count; p++) {
                bool env = env_param_count && p == 0;
                TypeId param_type = env ? (TypeId)LMD_TYPE_ANY :
                    jm_param_type(fc, p - env_param_count);
                body->params[p] = {param_type,
                    env ? VALUE_REP_RAW_GC_POINTER : VALUE_REP_ITEM, 0};
            }
        }

        if (JM_JS_FACT(fc, native_return_kind) != NATIVE_RETURN_NONE) {
            FnVariantAnalysis* native =
                &analysis->variants[analysis->variant_count++];
            memset(native, 0, sizeof(*native));
            native->entry = {FN_ENTRY_NATIVE_BODY, true, false, false, false};
            native->effects = body->effects;
            native->result.normal = {JM_JS_FACT(fc, return_type),
                JM_JS_FACT(fc, native_return_kind) == NATIVE_RETURN_FLOAT
                    ? VALUE_REP_F64 : VALUE_REP_I64,
                SCALAR_RETURN_NONE};
            native->param_count = JM_PARAM_COUNT(fc);
            if (JM_PARAM_COUNT(fc) > 0) {
                native->params = (FnParamAnalysis*)pool_calloc(
                    mt->tp->pool, sizeof(FnParamAnalysis) * (size_t)JM_PARAM_COUNT(fc));
                for (int p = 0; p < JM_PARAM_COUNT(fc); p++) {
                    TypeId param_type = jm_param_type(fc, p);
                    ValueRep rep = param_type == LMD_TYPE_FLOAT
                        ? VALUE_REP_F64 : param_type == LMD_TYPE_ANY
                            ? VALUE_REP_ITEM : VALUE_REP_I64;
                    native->params[p] = {param_type, rep, 0};
                }
            }
        }
        if ((fc->node->is_async || fc->node->is_generator) &&
                analysis->variant_count < 4) {
            FnVariantAnalysis* resume =
                &analysis->variants[analysis->variant_count++];
            memset(resume, 0, sizeof(*resume));
            resume->entry = {FN_ENTRY_RESUME, false, false, false, true};
            resume->effects = public_entry->effects;
            resume->result.normal = {LMD_TYPE_ANY, VALUE_REP_ITEM,
                SCALAR_RETURN_NONE};
        }
        fc->body_name = jm_format_name("%s_body", fc->name);
        if (!fc->func_item) {
            MIR_item_t fwd = MIR_new_forward(mt->ctx, fc->name);
            fc->func_item = fwd;
            jm_register_local_func(mt, fc->name, fwd);
        }
        if (!fc->body_func_item) {
            MIR_item_t body_fwd = MIR_new_forward(mt->ctx, fc->body_name);
            fc->body_func_item = body_fwd;
            jm_register_local_func(mt, fc->body_name, body_fwd);
        }
        // P1: Also pre-declare native function version so call sites emitted before
        // a function is defined can use fc->native_func_item.  The actual native
        // function replaces this forward reference when jm_define_function runs.
        if (JM_JS_FACT(fc, native_return_kind) != NATIVE_RETURN_NONE && !fc->native_func_item) {
            char native_fwd_name[140];
            snprintf(native_fwd_name, sizeof(native_fwd_name), "%s_n", fc->name);
            MIR_item_t fwd_native = MIR_new_forward(mt->ctx, native_fwd_name);
            fc->native_func_item = fwd_native;
            jm_register_local_func(mt, native_fwd_name, fwd_native);
        }
    }

    return 1;
}

static int js_mir_lower(void* opaque) {
    JsMirTranspiler* mt = (JsMirTranspiler*)opaque;
    JsAstNode* root = mt && mt->tp ? (JsAstNode*)mt->tp->ast_root : NULL;
    if (!root || root->node_type != JS_AST_NODE_PROGRAM) return 0;
    JsProgramNode* program = (JsProgramNode*)root;

    // Phase 2: Define all collected functions (innermost first)
    for (int i = 0; i < mt->func_count; i++) {
        jm_define_function(mt, &mt->func_entries[i]);
    }

    // Phase 3: Create js_main(Context* ctx) -> Item
    MIR_var_t main_vars[] = {{MIR_T_P, "ctx", 0}};
    MIR_type_t main_ret = MIR_T_I64;
    MIR_item_t main_item = MIR_new_func_arr(mt->ctx, "js_main", 1, &main_ret, 1, main_vars);
    MIR_func_t main_func = MIR_get_item_func(mt->ctx, main_item);
    mt->em.func_item = main_item;
    mt->em.func = main_func;
    mt->current_fc = NULL;
    mt->current_class = NULL;
    mt->scope_env_reg = 0;
    mt->scope_env_slot_count = 0;
    mt->eval_local_frame_reg = 0;
    mt->last_closure_has_env = false;
    mt->last_closure_env_reg = 0;
    mt->last_closure_capture_count = 0;
    mt->in_main = true;
    mt->func_error_lane_label = 0;  // reset for js_main

    jm_begin_function_frame(mt, main_ret, true, MIR_SCALAR_RETURN_DYNAMIC,
        MIR_reg(mt->ctx, "ctx", main_func), true);
    jm_push_scope(mt);

    // Initialize result register to undefined (JS completion value default)
    MIR_reg_t result = jm_new_reg(mt, "result", MIR_T_I64);
    jm_emit_reg_op(mt, MIR_MOV, result, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEF_VAL));

    // Enable eval completion value tracking: expression statements inside
    // control flow (for/while/if/switch/try) will update this register,
    // so eval() returns the last evaluated expression value per ES spec.
    mt->eval_completion_reg = result;
    // Js57 Track A: allocate the module-level scope env when any top-level
    // closure captures a non-modvar block-let. Mirrors the function-body path
    // at js_mir_function_class_lowering.cpp's "fc->has_scope_env" branch.
    // Slots are pre-seeded with TDZ; the actual let/const declaration will
    // call jm_scope_env_mark_and_writeback to publish the initial value.
    if (mt->module_fc.has_scope_env && mt->module_fc.scope_env_count > 0) {
        mt->scope_env_reg = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, mt->module_fc.scope_env_count));
        jm_register_owned_env(mt, mt->scope_env_reg);
        mt->scope_env_slot_count = mt->module_fc.scope_env_count;
        mt->current_fc = &mt->module_fc;

        // Pre-fill all slots with TDZ sentinel so unobserved captures hit the
        // ReferenceError path rather than picking up a stale undefined.
        for (int s = 0; s < mt->module_fc.scope_env_count; s++) {
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64,
                    s * (int)sizeof(uint64_t), mt->scope_env_reg, 0, 1),
                MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_TDZ)));
        }
        if (mt->module_fc.has_parent_env_link) {
            for (int s = mt->module_fc.scope_env_normal_count;
                    s < mt->module_fc.scope_env_count; s++) {
                // The module env is its own live parent binding store. The tail
                // makes the shared layout match copied mixed-loop environments.
                jm_emit_store_i64(mt, s * (int)sizeof(uint64_t), mt->scope_env_reg, mt->scope_env_reg);
            }
        }
        log_debug("js-mir: js_main allocated module scope env (%d slots)",
            mt->module_fc.scope_env_count);
    }

    // v20 TDZ: Initialize let/const module vars to TDZ sentinel
    // Skip preamble-inherited entries from outer scope (e.g. eval)
    int preamble_var_limit = (mt->preamble_entries && mt->preamble_entry_count > 0)
        ? mt->preamble_var_count : 0;
    if (mt->module_consts) {
        size_t tdz_iter = 0; void* tdz_item;
        while (hashmap_iter(mt->module_consts, &tdz_iter, &tdz_item)) {
            JsModuleConstEntry* mce = (JsModuleConstEntry*)tdz_item;
            if (mce->const_type == MCONST_MODVAR &&
                (mce->var_kind == JS_VAR_LET || mce->var_kind == JS_VAR_CONST) &&
                (int)mce->int_val >= preamble_var_limit) {
                if (!mt->is_module && !mt->is_eval_direct && !mce->is_iife_var) {
                    const char* js_name = mce->name;
                    if (strncmp(js_name, "_js_", 4) == 0) js_name += 4;
                    MIR_reg_t key_reg = jm_box_property_name_literal(mt, js_name, strlen(js_name));
                    // Global lexical declarations are checked during script
                    // declaration instantiation and tracked separately from
                    // globalThis properties for later evalScript collision checks.
                    jm_callr_1(mt, "js_evalscript_check_global_lex_decl", MIR_T_I64, key_reg);
                    jm_emit_error_lane_propagate_check(mt);
                    MIR_reg_t undef_lex = jm_new_reg(mt, "global_lex_undef", MIR_T_I64);
                    jm_emit_reg_op(mt, MIR_MOV, undef_lex, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEF_VAL));
                    jm_call_void_3(mt, "js_global_lexical_declare",
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, undef_lex),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, mce->var_kind == JS_VAR_CONST ? 1 : 0));
                }
                MIR_reg_t tdz_val = jm_new_reg(mt, "tdz_init", MIR_T_I64);
                jm_emit_reg_op(mt, MIR_MOV, tdz_val, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_TDZ));
                jm_store_module_var(mt, (uint32_t)mce->int_val, tdz_val);
            }
        }
    }

    // Initialize declared var module vars to undefined, excluding preamble-inherited entries.
    if (mt->module_consts) {
        bool global_var_batch_emitted = jm_emit_undefined_module_var_batch(mt,
            preamble_var_limit, true);
        bool private_var_batch_emitted = jm_emit_undefined_module_var_batch(mt,
            preamble_var_limit, false);
        size_t var_iter = 0; void* var_item;
        while (hashmap_iter(mt->module_consts, &var_iter, &var_item)) {
        JsModuleConstEntry* mce = (JsModuleConstEntry*)var_item;
            // The promoted declaration writes its module cell before any
            // IIFE-body consumer can observe it; do not emit an unused undef.
            if (mce->is_iife_func_decl) continue;
            if (mce->const_type == MCONST_MODVAR &&
                mce->var_kind == JS_VAR_VAR &&
                (int)mce->int_val >= preamble_var_limit) {
                bool should_define_global = !mt->is_module && !mt->is_eval_direct &&
                    !mce->is_iife_var;
                bool batch_emitted = should_define_global
                    ? global_var_batch_emitted : private_var_batch_emitted;
                if (!mt->is_eval_direct && batch_emitted) continue;
                MIR_reg_t init_val = 0;
                if (mt->is_eval_direct && mce->is_nested_func_hoist) {
                    const char* js_name = mce->name;
                    if (strncmp(js_name, "_js_", 4) == 0) js_name += 4;
                    MIR_reg_t key_reg = jm_box_property_name_literal(mt, js_name, strlen(js_name));
                    MIR_reg_t bridged_reg = jm_callr_1(mt, "js_eval_env_has_binding", MIR_T_I64, key_reg);
                    MIR_label_t use_undef = jm_new_label(mt);
                    MIR_label_t init_done = jm_new_label(mt);
                    init_val = jm_new_reg(mt, "var_init", MIR_T_I64);
                    jm_emit_branch(mt, MIR_BF, use_undef, bridged_reg);
                    MIR_reg_t bridged_val = jm_callr_1(mt, "js_get_global_property", MIR_T_I64, key_reg);
                    jm_emit_mov(mt, init_val, bridged_val);
                    jm_emit_jmp(mt, init_done);
                    jm_emit_label(mt, use_undef);
                    jm_emit_reg_op(mt, MIR_MOV, init_val, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEF_VAL));
                    jm_emit_label(mt, init_done);
                } else {
                    init_val = jm_new_reg(mt, "var_init", MIR_T_I64);
                    jm_emit_reg_op(mt, MIR_MOV, init_val, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEF_VAL));
                }
                jm_store_module_var(mt, (uint32_t)mce->int_val, init_val);
                if (!mt->is_module && !mt->is_eval_direct && !mce->is_iife_var) {
                    const char* js_name = mce->name;
                    if (strncmp(js_name, "_js_", 4) == 0) js_name += 4;
                    MIR_reg_t key_reg = jm_box_property_name_literal(mt, js_name, strlen(js_name));
                    jm_call_void_3(mt, "js_define_global_property_v",
                        MIR_T_I64, MIR_new_int_op(mt->ctx, 0),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, init_val));
                    // Top-level `var` is an object-environment binding. Register
                    // the optimized module slot so `globalThis.x = v` keeps
                    // identifier reads coherent without a property lookup per read.
                    jm_call_void_2(mt, "js_register_global_var_module_binding",
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mce->int_val));
                }
            }
        }
    }

    // v24: Set strict mode flag in runtime — always emit to reset from previous test in batch mode
    jm_call_void_1(mt, "js_set_strict_mode",
        MIR_T_I64, MIR_new_int_op(mt->ctx, (mt->is_global_strict || mt->is_module) ? 1 : 0));

    if (!mt->is_global_strict && !mt->is_module && mt->is_eval_direct) {
        JsAstNode* precheck_stmt = program->body;
        while (precheck_stmt) {
            jm_emit_evalscript_global_lex_decl_precheck(mt, precheck_stmt);
            jm_emit_evalscript_global_decl_prechecks(mt, precheck_stmt);
            precheck_stmt = precheck_stmt->next;
        }
    }

    // AnnexB B.3.3: For sloppy-mode scripts/eval, pre-initialize
    // globalThis.<name> = undefined for nested function declarations that
    // qualify for the web-compat extension. This ensures the binding is
    // observable BEFORE the function declaration statement executes.
    if (!mt->is_global_strict && !mt->is_module && mt->module_consts) {
        size_t aiter = 0; void* aitem;
        while (hashmap_iter(mt->module_consts, &aiter, &aitem)) {
            JsModuleConstEntry* mce = (JsModuleConstEntry*)aitem;
            if (mce->const_type != MCONST_MODVAR) continue;
            if (!mce->is_nested_func_hoist) continue;
            if (mce->is_iife_var) continue;
            const char* js_name = mce->name;
            if (strncmp(js_name, "_js_", 4) == 0) js_name += 4;
            MIR_reg_t key_reg = jm_box_property_name_literal(mt, js_name, strlen(js_name));
            MIR_label_t skip_preinit = jm_new_label(mt);
            if (mt->is_eval_direct) {
                MIR_reg_t bridged_reg = jm_callr_1(mt, "js_eval_env_has_binding", MIR_T_I64, key_reg);
                jm_emit_branch(mt, MIR_BT, skip_preinit, bridged_reg);
            }
            MIR_reg_t undef_reg = jm_new_reg(mt, "annexb_undef", MIR_T_I64);
            jm_emit_reg_op(mt, MIR_MOV, undef_reg, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEF_VAL));
            if (mt->is_eval_direct) {
                MIR_reg_t eval_env_active = jm_call_0(mt, "js_eval_env_is_active", MIR_T_I64);
                MIR_label_t global_preinit = jm_new_label(mt);
                MIR_label_t preinit_done = jm_new_label(mt);
                jm_emit_branch(mt, MIR_BF, global_preinit, eval_env_active);
                jm_callr_void_2(mt, "js_eval_local_export_var", key_reg, undef_reg);
                jm_emit_jmp(mt, preinit_done);
                jm_emit_label(mt, global_preinit);
                jm_callr_void_1(mt, "js_eval_env_track_global_binding", key_reg);
                jm_call_void_3(mt, "js_define_global_property_v",
                    MIR_T_I64, MIR_new_int_op(mt->ctx, 1),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, undef_reg));
                jm_emit_label(mt, preinit_done);
            } else {
                jm_call_void_3(mt, "js_define_global_property_v",
                    MIR_T_I64, MIR_new_int_op(mt->ctx, 0),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, undef_reg));
            }
            log_debug("js-mir: AnnexB pre-init globalThis.%s = undefined", js_name);
            jm_emit_label(mt, skip_preinit);
        }
    }

    // Module mode: create namespace object to hold exports
    if (mt->is_module) {
        mt->namespace_reg = jm_call_0(mt, "js_get_active_module_namespace", MIR_T_I64);

        // Js57 P3 (Track B2): no namespace pre-init is needed. The live-binding
        // runtime helper detects "default not yet exported" via the absence of
        // the `default` own property (see js_get_live_binding_default). The
        // existing `js_set_key_default` at the `export default <expr>` site is
        // what publishes the binding.
    }

    // Js57 P7d-C: detect TLA in module body so the body emission can install
    // a state-dispatch right before the main statement loop and the split
    // sequence at the first top-level ExpressionStatement(AwaitExpression).
    // Static entry modules retain their synchronous top-level-ticks behavior,
    // but a dynamic import must expose the module's pending evaluation promise
    // until its top-level await settles (ECMA-262 ContinueDynamicImport).
    bool p7d_has_tla = false;
    MIR_label_t p7d_post_await_label = NULL;
    {
        extern int js_dynamic_import_suppress_module_drain;
        if (mt->is_module && mt->in_main && mt->filename &&
                (js_tla_module_depth_get() >= 2 ||
                 js_dynamic_import_suppress_module_drain > 0)) {
            int p7d_tla_count = 0;
            for (JsAstNode* s = program->body; s; s = s->next) {
                p7d_tla_count += jm_count_awaits(mt, s);
                if (p7d_tla_count > 0) break;
            }
            if (p7d_tla_count > 0) {
                p7d_has_tla = true;
                p7d_post_await_label = jm_new_label(mt);
            }
        }
    }

    // Emit variable bindings for named function declarations (so they can be
    // used as first-class values, e.g., passed as callbacks).
    // Non-capturing function declarations are hoisted (bound before any statements).
    // Capturing function declarations are deferred to their source position
    // (bound inline with statements, after preceding const/let are in scope).
    JsAstNode* stmt = program->body;
    while (stmt) {
        // Unwrap export declarations to hoist exported function declarations
        JsAstNode* actual_stmt = stmt;
        if (stmt->node_type == JS_AST_NODE_EXPORT_DECLARATION) {
            JsExportNode* exp = (JsExportNode*)stmt;
            if (exp->declaration) actual_stmt = exp->declaration;
        }
        if (actual_stmt->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
            JsFunctionNode* fn = (JsFunctionNode*)actual_stmt;
            if (fn->name) {
                JsFuncCollected* fc = jm_find_collected_func(mt, fn);
                if (fc && fc->func_item && JM_CAPTURE_COUNT(fc) == 0) {
                    // Non-capturing: hoist normally
                    int pc = ast_linked_node_count(fn->params);
                    if (JM_JS_FACT(fc, has_rest_param)) pc = -pc;  // negative signals rest params
                    const char* vname = jm_var_name(fn->name);
                    MIR_reg_t var_reg = jm_new_reg(mt, vname, MIR_T_I64);
                    MIR_reg_t fn_item = jm_call_2(mt, "js_new_function_mir", MIR_T_I64,
                        MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, pc));
                    // Keep hoisted declarations on the same atomic metadata path as
                    // closures so no partially initialized function can escape.
                    jm_emit_finalize_function(mt, fn_item, fc, fn);
                    jm_emit_mov(mt, var_reg, fn_item);
                    // For reassigned functions, do NOT create a local register;
                    // all reads must go through js_get_module_var to see updates
                    // from self-reassignment inside the function body.
                    if (!JM_JS_FACT(fc, is_reassigned))
                        jm_set_var(mt, vname, var_reg, MIR_T_I64,
                            LMD_TYPE_ANY, fn->entry);
                    jm_emit_function_decl_runtime_bindings(mt, fn, var_reg);
                }
            }
        }
        stmt = stmt->next;
    }

    // Bind class names as hoisted variables (needed for captures and shorthand properties).
    // Only DIRECT program-level class *declarations* bind a name in the enclosing
    // scope. Nested declarations and named *class expressions* (whose name is an
    // immutable binding scoped to the class body via inner_module_var_index) must
    // NOT leak a hoisted var into the surrounding scope.
    for (int ci = 0; ci < mt->class_count; ci++) {
        JsClassEntry* ce = &mt->class_entries[ci];
        if (ce->name) {
            if (!ce->is_declaration || !jm_is_direct_program_class_decl(program, ce->node)) {
                continue;
            }
            const char* vname = jm_var_name(ce->name);
            // Create a variable holding null placeholder.
            // Actual class instantiation is handled by jm_emit_new_expression.
            MIR_reg_t var_reg = jm_new_reg(mt, vname, MIR_T_I64);
            jm_emit_reg_op(mt, MIR_MOV, var_reg, MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL));
            jm_set_var(mt, vname, var_reg, MIR_T_I64, LMD_TYPE_ANY,
                ce->node->outer_entry);
            // Also store null to module var so closures see the initial value
            JsModuleConstEntry* mc = jm_find_module_const_by_binding(mt,
                ce->node->outer_entry);
            if (mc && mc->const_type == MCONST_CLASS) {
                jm_store_module_var(mt, (uint32_t)mc->int_val, var_reg);
            }
        }
    }

    // Transpile top-level statements in source order.
    // Function declarations with captures are bound at their source position.

    // AstIndex owns the program body, so the widening prepass needs no
    // synthetic block wrapper or unindexed traversal path.
    jm_prescan_float_widening(mt, (JsAstNode*)program);

    // Js57 P7d-C: emit body-state dispatch right before user statements. On
    // re-entry (deferred drain calling js_main again with body_state == 1),
    // skip past pre-await statements to POST_AWAIT.
    if (p7d_has_tla && p7d_post_await_label) {
        MIR_reg_t p7d_spec = jm_box_string_literal(mt, mt->filename,
            (int)strlen(mt->filename));
        MIR_reg_t p7d_state = jm_callr_1(mt, "js_module_get_body_state", MIR_T_I64, p7d_spec);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BNES,
            MIR_new_label_op(mt->ctx, p7d_post_await_label),
            MIR_new_reg_op(mt->ctx, p7d_state),
            MIR_new_int_op(mt->ctx, 0)));
    }

    stmt = program->body;
    while (stmt) {
        // Unwrap export declarations to reach the inner declaration
        JsAstNode* actual_stmt = stmt;
        JsExportNode* current_export = NULL;
        if (stmt->node_type == JS_AST_NODE_EXPORT_DECLARATION) {
            current_export = (JsExportNode*)stmt;
            if (current_export->declaration) {
                actual_stmt = current_export->declaration;
            } else if (current_export->specifiers && mt->is_module) {
                // export { a, b as c } — emit exports for each specifier.
                // Js52 P1: support aliased exports via JsExportSpecifierNode.
                // Value is resolved by local_name; published under export_name.
                JsAstNode* spec = current_export->specifiers;
                while (spec) {
                    if (spec->node_type == JS_AST_NODE_EXPORT_SPECIFIER) {
                        JsExportSpecifierNode* es = (JsExportSpecifierNode*)spec;
                        jm_emit_module_export_aliased(mt,
                            es->local_name->chars,  (int)es->local_name->len,
                            es->local_entry,
                            es->export_name->chars, (int)es->export_name->len);
                    }
                    spec = spec->next;
                }
                stmt = stmt->next;
                continue;
            } else {
                stmt = stmt->next;
                continue;
            }
        }

        if (actual_stmt->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
            JsFunctionNode* fn = (JsFunctionNode*)actual_stmt;
            if (fn->name) {
                JsFuncCollected* fc = jm_find_collected_func(mt, fn);
                if (fc && fc->func_item && JM_CAPTURE_COUNT(fc) > 0) {
                    // Capturing function declaration: bind as closure at this position
                    int pc = ast_linked_node_count(fn->params);
                    if (JM_JS_FACT(fc, has_rest_param)) pc = -pc;  // negative signals rest params
                    const char* vname = jm_var_name(fn->name);
                    MIR_reg_t var_reg = jm_new_reg(mt, vname, MIR_T_I64);
                    MIR_reg_t env = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
                        MIR_T_I64, MIR_new_int_op(mt->ctx, JM_CAPTURE_COUNT(fc)));
                    // D5.2/D5.3.3: copied scalar captures initially point into
                    // this activation; retain and rehome the env at epilogue.
                    jm_register_owned_env(mt, env);

                    // Track which env slot is the self-reference (for recursive fn decls)
                    int self_ref_slot = -1;

                    for (int ci = 0; ci < JM_CAPTURE_COUNT(fc); ci++) {
                        // Check if this capture is the function's own name (self-reference)
                        if (strcmp(JM_CAPTURE_ARRAY(fc)[ci].name, vname) == 0) {
                            self_ref_slot = ci;
                            // Will be filled after closure creation below
                            continue;
                        }
                        JsMirVarEntry* var = jm_find_var_by_binding(mt,
                            JM_CAPTURE_ARRAY(fc)[ci].entry);
                        if (var) {
                            // Box native-typed variables before storing in env
                            MIR_reg_t value_to_store = var->reg;
                            if (jm_is_native_type(var->type_id)) {
                                value_to_store = jm_box_native(mt, var->reg, var->type_id);
                            }
                            jm_emit_store_i64(mt, ci * (int)sizeof(uint64_t), env, value_to_store);
                        } else {
                            // fallback: load the live module binding.
                            bool found_mc = false;
                            if (mt->module_consts) {
                                JsModuleConstEntry* mc =
                                    jm_find_module_const_by_binding(mt,
                                        JM_CAPTURE_ARRAY(fc)[ci].entry);
                                if (mc) {
                                    found_mc = true;
                                    MIR_reg_t const_val = jm_emit_module_const_value(mt, mc);
                                    jm_emit_store_i64(mt, ci * (int)sizeof(uint64_t), env, const_val);
                                }
                            }
                            if (!found_mc) {
                                log_error("js-mir: captured var '%s' not found for fn decl '%.*s'",
                                    JM_CAPTURE_ARRAY(fc)[ci].name, (int)fn->name->len, fn->name->chars);
                            }
                        }
                    }
                    MIR_reg_t fn_item = jm_call_4(mt, "js_new_closure_mir", MIR_T_I64,
                        MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, pc),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, env),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, JM_CAPTURE_COUNT(fc)));
                    // Capturing declarations need the same metadata invariants as
                    // ordinary function expressions before their binding is visible.
                    jm_emit_finalize_function(mt, fn_item, fc, fn);
                    jm_emit_mov(mt, var_reg, fn_item);
                    jm_set_var(mt, vname, var_reg, MIR_T_I64, LMD_TYPE_ANY,
                        fn->entry);

                    // Patch self-reference: update env slot to point to the closure itself
                    if (self_ref_slot >= 0) {
                        jm_emit_store_i64(mt, self_ref_slot * (int)sizeof(uint64_t), env, var_reg);
                    }

                    jm_emit_function_decl_runtime_bindings(mt, fn, var_reg);
                }
            }
            // Non-capturing function declarations already handled above
            // Module mode: export the function to namespace
            if (current_export && mt->is_module && fn->name) {
                jm_emit_module_export(mt, fn->name->chars, (int)fn->name->len,
                    fn->entry,
                    current_export->is_default);
            }
            stmt = stmt->next;
            continue;
        }

        if (actual_stmt->node_type == JS_AST_NODE_CLASS_DECLARATION) {
            // Create the class object and store it in the module var so it can be
            // accessed by closures/methods (e.g., __publicField(ClassName, ...))
            JsClassNode* cls_node = (JsClassNode*)actual_stmt;
            if (cls_node->name) {
                JsClassEntry* ce = jm_find_collected_class(mt, cls_node);
                if (ce) {
                    // TDZ: class x extends x {} → throw ReferenceError
                    jm_emit_class_self_extends_check(mt, ce, cls_node->name);
                    MIR_reg_t cls_obj = jm_call_0(mt, "js_new_class_function", MIR_T_I64);
                    // Class initialization performs allocating metadata and
                    // method setup before its lexical binding is authoritative.
                    jm_create_gc_root_slot(mt, cls_obj);
                    jm_emit_set_class_source(mt, cls_obj, cls_node);
                    // Update the declaration's resolved local binding.
                    JsMirVarEntry* ve = jm_find_var_by_binding(mt,
                        cls_node->outer_entry);
                    if (ve) {
                        jm_emit_mov(mt, ve->reg, cls_obj);
                    }
                    // Store class object in module var
                    JsModuleConstEntry* mc = jm_find_module_const_by_binding(mt,
                        cls_node->outer_entry);
                    if (mc && mc->const_type == MCONST_CLASS) {
                        jm_store_module_var(mt, (uint32_t)mc->int_val, cls_obj);
                    }
                    if (ce->inner_module_var_index >= 0) {
                        jm_store_module_var(mt, (uint32_t)ce->inner_module_var_index, cls_obj);
                    }
                    if (!mt->is_module) {
                        MIR_reg_t class_key = jm_box_property_name_literal(mt,
                            cls_node->name->chars, cls_node->name->len);
                        if (mt->is_eval_direct) {
                            MIR_reg_t evalscript_active = jm_call_0(mt, "js_262_eval_script_is_active", MIR_T_I64);
                            MIR_label_t skip_global_class_lex = jm_new_label(mt);
                            jm_emit_branch(mt, MIR_BF, skip_global_class_lex, evalscript_active);
                            // evalScript class declarations are global lexical
                            // bindings, not globalThis properties.
                            jm_call_void_3(mt, "js_global_lexical_declare",
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, class_key),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                                MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
                            jm_emit_label(mt, skip_global_class_lex);
                        } else {
                            // Track top-level class declarations for later
                            // evalScript collision checks and global lexical reads.
                            jm_call_void_3(mt, "js_global_lexical_declare",
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, class_key),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                                MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
                        }
                    }
                    jm_emit_class_members(mt, cls_obj, ce, (JsAstNode*)cls_node, false);
                }
            }
            stmt = stmt->next;
            continue;
        }

        // Module mode: handle export default <expression>
        if (current_export && current_export->is_default && mt->is_module) {
            MIR_reg_t val = jm_transpile_box_item(mt, actual_stmt);
            MIR_reg_t key = jm_box_property_name_literal(mt, "default", 7);
            jm_callr_3(mt, "js_set_key_default", MIR_T_I64, mt->namespace_reg, key, val);
            stmt = stmt->next;
            continue;
        }

        if (actual_stmt->node_type == JS_AST_NODE_EXPRESSION_STATEMENT) {
            JsExpressionStatementNode* es = (JsExpressionStatementNode*)actual_stmt;
            // Js57 P7d-C: split at first top-level ExpressionStatement(Await).
            // We evaluate the await argument (so side effects + P5 publish run),
            // mark body_state=1, set post_await_pending, and return the
            // namespace early. The label below catches the re-entry from the
            // depth-0 AEO drain so post-await statements run.
            bool p7d_split_now = (p7d_has_tla && p7d_post_await_label && es->expression &&
                                  es->expression->node_type == JS_AST_NODE_AWAIT_EXPRESSION);
            if (p7d_split_now) {
                JsAwaitNode* aw = (JsAwaitNode*)es->expression;
                MIR_reg_t arg_val = jm_new_reg(mt, "p7d_aw_arg", MIR_T_I64);
                if (aw->argument) {
                    arg_val = jm_transpile_box_item(mt, aw->argument);
                } else {
                    jm_emit_reg_op(mt, MIR_MOV, arg_val, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED));
                }
                MIR_reg_t p7d_spec_split = jm_box_string_literal(mt, mt->filename,
                    (int)strlen(mt->filename));
                // Pass through P5 publish so pending-Promise awaits chain as
                // before (settled/non-Promise values fall through to js_await_sync).
                MIR_reg_t p7d_await_result = jm_callr_2(mt, "js_p5_module_await", MIR_T_I64, p7d_spec_split, arg_val);
                (void)p7d_await_result;
                // A rejected settled promise becomes an ERROR lane at the
                // synchronous await boundary; route it before the split marks
                // the module as pending, or the failed evaluation is erased.
                jm_emit_error_lane_route(mt, JS_MIR_COMPLETION_THROW);
                // Flip body_state and mark post-await as pending so the AEO
                // drain at depth-0 knows to fire this module's continuation.
                jm_call_void_2(mt, "js_module_set_body_state",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, p7d_spec_split),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, 1));
                jm_callr_void_1(mt, "js_module_mark_post_await_pending", p7d_spec_split);
                // Assign this module an AEO slot (idempotent — only set on
                // first call). Importers register with us as a parent before
                // we hit the drain, so AEO needs to be defined first.
                jm_callr_1(mt, "js_module_assign_async_eval_order", MIR_T_I64, p7d_spec_split);
                // Return the namespace immediately; post-await statements run
                // on re-entry via the dispatch label.
                jm_emit_ret(mt, mt->namespace_reg);
                // Emit POST_AWAIT label — subsequent statements land here on
                // the second call.
                jm_emit_label(mt, p7d_post_await_label);
                p7d_post_await_label = NULL;  // single-shot split
                stmt = stmt->next;
                continue;
            }
            if (es->expression) {
                MIR_reg_t val = jm_transpile_box_item(mt, es->expression);
                jm_emit_mov(mt, result, val);
            }
        } else {
            if (!current_export && jm_is_plain_script_module_var_decl_without_init(mt, actual_stmt)) {
                stmt = stmt->next;
                continue;
            }
            jm_transpile_statement(mt, actual_stmt);
            // Module mode: after transpiling exported variable declarations,
            // emit exports for each declared name
            if (current_export && mt->is_module &&
                actual_stmt->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                JsVariableDeclarationNode* v = (JsVariableDeclarationNode*)actual_stmt;
                JsAstNode* d = v->declarations;
                while (d) {
                    if (d->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                        JsVariableDeclaratorNode* vd = (JsVariableDeclaratorNode*)d;
                        if (vd->id && vd->id->node_type == JS_AST_NODE_IDENTIFIER) {
                            JsIdentifierNode* vid = (JsIdentifierNode*)vd->id;
                            jm_emit_module_export(mt, vid->name->chars, (int)vid->name->len,
                                vid->entry,
                                current_export->is_default);
                        } else if (vd->id && (vd->id->node_type == JS_AST_NODE_OBJECT_PATTERN ||
                                              vd->id->node_type == JS_AST_NODE_ARRAY_PATTERN)) {
                            // Js56 H2: `export const { resolve, reject } = expr;` /
                            // `export let [a, b] = expr;` — walk the pattern and
                            // export each bound name so cross-module imports
                            // (e.g. Promise.withResolvers fixtures in TLA tests)
                            // can resolve them.
                            struct hashmap* names = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
                                jm_name_hash, jm_name_cmp, NULL, NULL);
                            jm_collect_pattern_names(vd->id, names);
                            size_t iter = 0; void* item;
                            while (hashmap_iter(names, &iter, &item)) {
                                JsNameSetEntry* ne = (JsNameSetEntry*)item;
                                // names from jm_collect_pattern_names have "_js_" prefix
                                const char* js_name = ne->name;
                                int js_name_len = (int)strlen(ne->name);
                                if (strncmp(js_name, "_js_", 4) == 0) {
                                    js_name += 4;
                                    js_name_len -= 4;
                                }
                                jm_emit_module_export(mt, js_name, js_name_len,
                                    ne->entry,
                                    current_export->is_default);
                            }
                            hashmap_free(names);
                        }
                    }
                    d = d->next;
                }
            }
        }
        // top-level exception propagation: if any statement causes an
        // uncaught exception, stop executing further statements
        jm_emit_error_lane_propagate_check(mt);

        // Js57 P4 reverted in P6: the post-await body-break broke any nested
        // module that emits exports after a top-level await (e.g. fixtures
        // shaped like `await 1; export default await Promise.resolve(42);`).
        // The narrow win it bought on
        // `async-module-does-not-block-sibling-modules.js` is given up here
        // because that test's spec-correct fix requires real TLA suspension —
        // out of scope for the current change set. P5's
        // `js_p5_module_await` does still publish the awaited target so the
        // fulfillment/rejection-order dynamic-import chain works.

        stmt = stmt->next;
    }

    // Sloppy-mode eval: export var/function declarations to globalThis
    // so they're visible in the calling scope after eval() returns.
    // Only for global-scope direct eval (not strict mode, not modules).
    if (mt->is_eval_direct && !mt->is_global_strict && !mt->is_module && mt->module_consts) {
        int preamble_limit = (mt->preamble_entries && mt->preamble_entry_count > 0)
            ? mt->preamble_var_count : 0;
        size_t ev_iter = 0; void* ev_item;
        while (hashmap_iter(mt->module_consts, &ev_iter, &ev_item)) {
            JsModuleConstEntry* mce = (JsModuleConstEntry*)ev_item;
            if (mce->const_type != MCONST_MODVAR) continue;
            // Skip preamble entries (inherited from outer scope)
            if ((int)mce->int_val < preamble_limit) continue;
            log_debug("js-mir: eval export checking '%s' var_kind=%d nested_func=%d preamble_limit=%d idx=%d",
                mce->name, mce->var_kind, mce->is_nested_func_hoist, preamble_limit, (int)mce->int_val);
            // Skip let/const (only var declarations leak from eval)
            if (mce->var_kind == JS_VAR_LET || mce->var_kind == JS_VAR_CONST) continue;
            // AnnexB B.3.3.3: nested function declarations DO propagate to globalThis
            // (was previously skipped). The propagation writes the current module_var
            // value (undefined if function decl never executed, or the function value
            // otherwise) to globalThis with default EWC descriptor.
            // Strip _js_ prefix to get the original JS name
            const char* js_name = mce->name;
            if (strncmp(js_name, "_js_", 4) == 0) js_name += 4;
            MIR_reg_t key_reg = jm_box_property_name_literal(mt, js_name, strlen(js_name));
            MIR_reg_t val_reg = jm_load_module_var(mt, (uint32_t)mce->int_val);
            MIR_reg_t eval_env_active = jm_call_0(mt, "js_eval_env_is_active", MIR_T_I64);
            MIR_label_t global_export = jm_new_label(mt);
            MIR_label_t export_done = jm_new_label(mt);
            jm_emit_branch(mt, MIR_BF, global_export, eval_env_active);
            jm_callr_void_2(mt, "js_eval_local_export_var", key_reg, val_reg);
            MIR_reg_t evalscript_local_active = jm_call_0(mt, "js_262_eval_script_is_active", MIR_T_I64);
            MIR_label_t skip_evalscript_global = jm_new_label(mt);
            jm_emit_branch(mt, MIR_BF, skip_evalscript_global, evalscript_local_active);
            // evalScript var declarations use Script global binding semantics
            // even when the harness has an eval-local frame active.
            jm_call_void_3(mt, "js_define_global_property_v",
                MIR_T_I64, MIR_new_int_op(mt->ctx, 0),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val_reg));
            jm_emit_label(mt, skip_evalscript_global);
            jm_emit_jmp(mt, export_done);
            jm_emit_label(mt, global_export);
            MIR_reg_t evalscript_active = jm_call_0(mt, "js_262_eval_script_is_active", MIR_T_I64);
            MIR_label_t ordinary_eval_export = jm_new_label(mt);
            MIR_label_t global_define_done = jm_new_label(mt);
            jm_emit_branch(mt, MIR_BF, ordinary_eval_export, evalscript_active);
            // $262.evalScript runs script-level global declaration instantiation;
            // var declarations create non-configurable bindings, unlike ordinary eval.
            jm_call_void_3(mt, "js_define_global_property_v",
                MIR_T_I64, MIR_new_int_op(mt->ctx, 0),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val_reg));
            jm_emit_jmp(mt, global_define_done);
            jm_emit_label(mt, ordinary_eval_export);
            jm_call_void_3(mt, "js_set_global_property",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val_reg),
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
            jm_emit_label(mt, global_define_done);
            jm_callr_void_2(mt, "js_eval_local_export_var", key_reg, val_reg);
            jm_emit_label(mt, export_done);
            log_debug("js-mir: eval export var '%s' to globalThis", js_name);
        }
    }

    // Js57 P7d-C: emit the post-await label if we set up the split machinery
    // but never hit an ExpressionStatement(AwaitExpression) (e.g. the only
    // top-level await is inside an export-default expression or a variable
    // declarator initializer). The label still needs a landing site so the
    // dispatch branch is valid; nothing else needs to happen here, the
    // existing return-namespace path follows.
    if (p7d_has_tla && p7d_post_await_label) {
        jm_emit_label(mt, p7d_post_await_label);
        p7d_post_await_label = NULL;
    }
    // Js57 P7d-C: every module that ran to the end of its body (TLA-post or
    // sync) notifies the module registry so its async parents get their
    // pending counters decremented and the AEO ready queue gets drained.
    if (mt->is_module && mt->in_main && mt->filename) {
        MIR_reg_t p7d_complete_spec = jm_box_string_literal(mt, mt->filename,
            (int)strlen(mt->filename));
        jm_callr_void_1(mt, "js_module_complete_tla_body", p7d_complete_spec);
    }

    // Module mode: return namespace instead of result
    if (mt->is_module) {
        jm_emit_ret(mt, mt->namespace_reg);
    } else {
        jm_emit_ret(mt, result);
    }

    // Main error exit returns the routed D8.4.3 ERROR Item unchanged.
    if (mt->func_error_lane_label) {
        jm_emit_label(mt, mt->func_error_lane_label);
        MIR_reg_t exc_ret = jm_emit_error_lane_return(mt);
        jm_emit_ret(mt, exc_ret);
    }

    return 1;
}

static int js_mir_finalize(void* opaque) {
    JsMirTranspiler* mt = (JsMirTranspiler*)opaque;
    if (!mt || !mt->ctx || !mt->module) return 0;
    jm_pop_scope(mt);
    jm_finish_function_frame(mt, "js_main");
    MIR_finish_func(mt->ctx);
    MIR_finish_module(mt->ctx);

    // Load module for linking
    MIR_load_module(mt->ctx, mt->module);
    return 1;
}

static int js_mir_prelink(void* opaque) {
    JsMirTranspiler* mt = (JsMirTranspiler*)opaque;
    if (mt && js_prelink_compiled_name_table(mt)) return 1;
    log_error("js-mir: failed to prelink compiled property-name table");
    return 0;
}

bool transpile_js_mir_ast(JsMirTranspiler* mt) {
    if (!mt || !mt->tp || !mt->tp->ast_root) return false;
    CompilerPassManager* pass_manager = &mt->tp->pass_manager;
    uint32_t indexed_facts = COMPILER_FACT_FRONTEND | COMPILER_FACT_INDEXED;
    if ((pass_manager->facts & indexed_facts) != indexed_facts ||
            pass_manager->next_pass != pass_manager->pass_count) return false;
    CompilerPassSpec analyze_plan_pass = {"analyze-plan", COMPILER_FACT_INDEXED,
        COMPILER_FACT_ANALYZED | COMPILER_FACT_PLANNED, js_mir_analyze_and_plan, mt};
    CompilerPassSpec lower_pass = {"mir-lower", COMPILER_FACT_ANALYZED |
        COMPILER_FACT_PLANNED, COMPILER_FACT_MIR_LOWERED, js_mir_lower, mt};
    CompilerPassSpec finalize_pass = {"mir-finalize-load", COMPILER_FACT_MIR_LOWERED,
        COMPILER_FACT_FINALIZED, js_mir_finalize, mt};
    CompilerPassSpec prelink_pass = {"prelink", COMPILER_FACT_FINALIZED,
        COMPILER_FACT_PRELINKED, js_mir_prelink, mt};
    return compiler_pass_manager_add(pass_manager, &analyze_plan_pass) &&
        compiler_pass_manager_add(pass_manager, &lower_pass) &&
        compiler_pass_manager_add(pass_manager, &finalize_pass) &&
        compiler_pass_manager_add(pass_manager, &prelink_pass) &&
        compiler_pass_manager_run(pass_manager, NULL);
}

bool js_mir_link_runtime_state(JsMirTranspiler* mt) {
    if (!mt || !mt->tp) return false;
    CompilerPassManager* pass_manager = &mt->tp->pass_manager;
    if (pass_manager->facts & COMPILER_FACT_LINKED) return true;
    // The active module slab is runtime-owned; append its link after activation.
    CompilerPassSpec link_pass = {"runtime-link", COMPILER_FACT_PRELINKED,
        COMPILER_FACT_LINKED, js_mir_runtime_link_pass, mt};
    return (pass_manager->facts & COMPILER_FACT_PRELINKED) &&
        pass_manager->next_pass == pass_manager->pass_count &&
        compiler_pass_manager_add(pass_manager, &link_pass) &&
        compiler_pass_manager_run(pass_manager, NULL) &&
        (pass_manager->facts & COMPILER_FACT_LINKED);
}

// Pre-link validation: scan all MIR instructions for NULL label operands.
// Returns true if safe to link, false if NULL labels found (would crash MIR_link).
bool jm_validate_mir_labels(MIR_context_t ctx) {
    bool safe = true;
#ifndef NDEBUG
    int func_count = 0, insn_count = 0;
#endif
    for (MIR_module_t m = DLIST_HEAD(MIR_module_t, *MIR_get_module_list(ctx)); m != NULL;
         m = DLIST_NEXT(MIR_module_t, m)) {
        for (MIR_item_t item = DLIST_HEAD(MIR_item_t, m->items); item != NULL;
             item = DLIST_NEXT(MIR_item_t, item)) {
            if (item->item_type != MIR_func_item) continue;
            MIR_func_t func = item->u.func;
#ifndef NDEBUG
            func_count++;
#endif
            for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, func->insns); insn != NULL;
                 insn = DLIST_NEXT(MIR_insn_t, insn)) {
#ifndef NDEBUG
                insn_count++;
#endif
                for (size_t i = 0; i < insn->nops; i++) {
                    if (insn->ops[i].mode == MIR_OP_LABEL && insn->ops[i].u.label == NULL) {
                        log_error("js-mir: NULL label in func '%s' insn code=%d op=%zu - aborting link",
                            func->name, insn->code, i);
                        safe = false;
                    }
                }
            }
        }
    }
    if (!safe) {
        log_debug("js-mir: validate scanned %d funcs %d insns safe=%d", func_count, insn_count, safe);
    }
    return safe;
}

// ============================================================================
// ES Module loading: compile and execute a module, returning its namespace
// ============================================================================

static bool jm_module_has_top_level_await(JsMirTranspiler* mt, JsAstNode* ast) {
    if (!ast || ast->node_type != JS_AST_NODE_PROGRAM) return false;
    JsProgramNode* program = (JsProgramNode*)ast;
    for (JsAstNode* statement = program->body; statement; statement = statement->next) {
        if (jm_count_awaits(mt, statement) > 0) return true;
    }
    return false;
}

static void jm_finish_module_transpile(JsTranspiler* tp, JsMirTranspiler* mt,
                                       MIR_context_t ctx) {
    jm_clear_active_js_transpile(NULL, mt, NULL);
    jm_destroy_mir_transpiler(mt);
    jm_defer_mir_cleanup(ctx);
    jm_clear_active_js_transpile(tp, NULL, NULL);
    js_transpiler_destroy(tp);
}

Item transpile_js_module_to_mir(Runtime* runtime, const char* js_source, const char* filename) {
    log_debug("js-mir: compiling module '%s'", filename ? filename : "<module>");
    // Module compilation bypasses transpile_js_to_mir_core_len(), which normally
    // binds the Context-owned JS state. Test262's hot batch path calls this
    // entrypoint directly; without this bind, TLA state dereferences a null
    // capsule before the module can be parsed.
    if (!runtime || !context || !context->heap ||
            !js_runtime_state_init(context)) {
        log_error("js-mir: module compilation requires a bound runtime context");
        return ItemError;
    }
    context->runtime = runtime;
    // js_get_global_this() can run while precompiled dependencies initialize.
    // It requires an Input owner for its realm-bound names, so install this
    // module's owner before static imports can initialize the realm (D4.6.2v2).
    Input* module_input = Input::create(context->pool);
    if (!module_input) {
        log_error("js-mir: module: failed to create Input for '%s'",
            filename ? filename : "<module>");
        return ItemError;
    }
    js_runtime_set_input(module_input);
    extern int js_dynamic_import_suppress_module_drain;
    // Js57 P4 (Track B3): bump depth at the very start so jm_load_imports
    // nested calls see depth >= 2 while the outermost transpile sits at 1;
    // the matching exit at the end of the function drains continuations only
    // when this is the outermost call.
    js_tla_enter_module();
    JsTranspiler* tp = js_transpiler_create(runtime);
    if (!tp) {
        log_error("js-mir: module: failed to create transpiler for '%s'", filename);
        // keep the TLA depth balanced when parser construction fails before a
        // compile unit exists; otherwise later modules inherit a stale depth.
        js_tla_exit_module();
        return ItemNull;
    }
    jm_track_active_js_transpile(tp, NULL, NULL);


    if (!js_transpiler_parse_c(tp, js_source, strlen(js_source),
            (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_MODULE))) {
        // Js57 P7b: parse failure is a SyntaxError. Return ITEM_ERROR (not
        // ItemNull) so the batch driver short-circuits its post-test global
        // probes (async_required check), which SEGV when the heap was never
        // initialized for this test.
        log_error("js-mir: module: parse failed for '%s'", filename);
        (void)js_mir_compile_unit_fail(NULL, NULL, tp, NULL,
            runtime, context, true);
        js_tla_exit_module();
        return (Item){.item = ITEM_ERROR};
    }

    JsAstNode* js_ast = (JsAstNode*)tp->ast_root;
    if (!js_ast) {
        log_error("js-mir: module: AST build failed for '%s'", filename);
        (void)js_mir_compile_unit_fail(NULL, NULL, tp, NULL,
            runtime, context, true);
        js_tla_exit_module();
        return (Item){.item = ITEM_ERROR};
    }

    if (tp->has_errors) {
        log_error("js-mir: module: early error(s) for '%s'", filename);
        (void)js_mir_compile_unit_fail(NULL, NULL, tp, NULL,
            runtime, context, true);
        js_tla_exit_module();
        return (Item){.item = ITEM_ERROR};
    }

    // Js57 P5: register the current module BEFORE jm_load_imports so the
    // inherit-awaited-target call inside the loader has a registry entry to
    // write to. A throwaway namespace is used; the real one replaces it after
    // js_main runs (existing js_module_register call further down).
    String* p7d_self_spec_str = heap_create_name(filename, strlen(filename));
    Item p7d_self_spec_item = (Item){.item = s2it(p7d_self_spec_str)};
    {
        Item p5_existing = js_module_get(p7d_self_spec_item);
        if (get_type_id(p5_existing) == LMD_TYPE_NULL) {
            js_module_register(p7d_self_spec_item, js_new_object());
        }
    }

    // ES modules own a private zero-based property-name image even when their
    // importer uses a test harness preamble. Sharing the preamble offset here
    // makes globalThis member names resolve against the wrong image (D3.4.4v2).
    MIR_context_t ctx = NULL;
    JsMirTranspiler* mt = js_mir_open_compile_unit(tp, filename, "js_module", true,
        0, g_js_mir_optimize_level, false, "js-mir: module", false, &ctx);
    if (!mt) {
        (void)js_mir_compile_unit_fail(ctx, NULL, tp, NULL,
            runtime, context, true);
        return ItemNull;
    }
    // Detect top-level await after the shared compile unit publishes its index;
    // nested function/class scopes are excluded by the indexed owner relation.
    bool module_has_top_level_await = jm_module_has_top_level_await(mt, js_ast);
    if (js_tla_module_depth_get() >= 2 || js_dynamic_import_suppress_module_drain > 0) {
        if (module_has_top_level_await) {
            js_module_mark_has_tla(p7d_self_spec_item);
            log_debug("P7d-A: module '%s' has TLA (top-level await detected)", filename);
        }
    }
    if (!transpile_js_mir_ast(mt)) {
        log_error("js-mir: module: collection/allocation failed for '%s'", filename);
        (void)js_mir_compile_unit_fail(ctx, mt, tp, NULL,
            runtime, context, true);
        return (Item){.item = ITEM_ERROR};
    }

    // Module entry points bypass the script compiler's prelink step.  Its
    // local spelling image must be collected before imports can run: an entry
    // module can be compiled directly while the root is still static.
    if (!js_prelink_compiled_name_table(mt)) {
        log_error("js-mir: module: failed to prelink property-name table for '%s'",
            filename ? filename : "<module>");
        (void)js_mir_compile_unit_fail(ctx, mt, tp, NULL,
            runtime, context, true);
        js_tla_exit_module();
        return (Item){.item = ITEM_ERROR};
    }

    // This mirrors the source-entry static phase.  In particular, do not let
    // jm_load_imports create a dynamic child before this module's own names
    // have joined the static root (D4.6.1v2, D4.6.2v2).
    if (!js_activate_runtime_name_pool()) {
        log_error("js-mir: module: failed to activate dynamic NamePool for '%s'",
            filename ? filename : "<module>");
        (void)js_mir_compile_unit_fail(ctx, mt, tp, NULL,
            runtime, context, true);
        js_tla_exit_module();
        return (Item){.item = ITEM_ERROR};
    }
    // Realm construction is runtime work, after the static root is sealed.
    (void)js_get_global_this();
    jm_load_imports(runtime, js_ast, filename);

    RootFrame import_error_roots(1);
    Rooted<Item> imported_error(import_error_roots,
        js_module_get_evaluation_error(p7d_self_spec_item));
    if (get_type_id(imported_error.get()) != LMD_TYPE_NULL) {
        // A static dependency that failed evaluation rejects this module
        // before its body runs; compiling the importer would otherwise turn
        // the failed graph into a successful empty namespace.
        log_debug("js-mir: module '%s' dependency evaluation failed",
            filename ? filename : "<module>");
        (void)js_mir_compile_unit_fail(ctx, mt, tp, NULL,
            runtime, context, true);
        js_tla_exit_module();
        return js_throw_value(imported_error.get());
    }

    if (!jm_validate_mir_labels(ctx)) {
        log_error("js-mir: module: NULL labels detected for '%s'", filename);
        (void)js_mir_compile_unit_fail(ctx, mt, tp, NULL,
            runtime, context, true);
        return (Item){.item = ITEM_ERROR};
    }

    JsMirMainFunc js_main = js_mir_link_main(ctx, g_mir_interp_mode,
        MIR_set_gen_interface);

    if (!js_main) {
        log_error("js-mir: module: failed to find js_main for '%s'", filename);
        (void)js_mir_compile_unit_fail(ctx, mt, tp, NULL,
            runtime, context, true);
        return ItemNull;
    }

    // Execute module — js_main returns the namespace object in module mode.
    // Register the namespace before execution so dynamic import(self) and simple
    // circular edges observe the same live namespace object.
    String* spec_str = heap_create_name(filename, strlen(filename));
    Item spec_item = (Item){.item = s2it(spec_str)};
    Item namespace_obj = js_new_object();
    js_module_register(spec_item, namespace_obj);
    // Nested imports may install their own input owner; restore this module's
    // owner before its initializer creates values or property names.
    js_runtime_set_input(module_input);

    // Allocate per-module variable storage and switch to it.  The shared
    // scopes restore importer state across nested evaluation (D7.2.1).
    RuntimeCurrentFileScope current_file(context,
        filename ? filename : context->current_file);
    RuntimeModuleStateScope module_state(context);
    RuntimeExecutionScope execution_scope(context);
    // Dynamic imports use the executing module's filename for relative
    // resolution; the batch entry's previous filename would resolve against
    // the worker instead of the module directory (D7.2.3, D8.5.1).
    JsModuleNamespaceScope module_namespace(namespace_obj, true);
    if (!lambda_module_state_reserve_and_activate((uint32_t)mt->module_var_count) ||
            !js_mir_link_runtime_state(mt)) {
        return ItemNull;
    }
    if (execution_scope.is_outermost() &&
            !js_runtime_state.event_loop.callback_running &&
            js_dynamic_import_suppress_module_drain <= 0) {
        js_event_loop_init();
    }
    // Js57 P7d: save the module's evaluation context (module-state id +
    // namespace already on JsModule) and stash js_main as the deferred entry.
    // Used by the AEO drain to re-enter js_main with the same module-level
    // state when a deferred body / post-await chunk runs.
    js_module_save_context(spec_item, lambda_active_module_state_id());
    js_module_set_deferred_main_ptr(spec_item, (void*)js_main);
    // Modules that already have TLA-transitive deps were registered as async
    // parents during jm_load_imports; their bodies must wait for those deps
    // to settle before running. Sync modules with no pending deps run their
    // body immediately as before.
    int p7d_pending = js_module_pending_async_deps(spec_item);
    int p7d_has_tla = js_module_get_has_tla(spec_item);
    if (p7d_has_tla) {
        // Assign AEO so the drain orders us correctly relative to peer TLA
        // modules and TLA-importers.
        js_module_assign_async_eval_order(spec_item);
    }
    bool module_body_threw = false;
    if (p7d_pending > 0) {
        // Importer with pending TLA deps — skip js_main now; the AEO drain
        // will invoke it once all deps have settled.
        log_debug("P7d: module '%s' pending=%d — deferring body", filename, p7d_pending);
        // namespace stays as the empty/placeholder until deferred run completes.
    } else {
        namespace_obj = js_mir_execute_compiled_entry((void*)js_main);
        module_body_threw = item_is_error(namespace_obj);
    }
    // Microtasks retain their function owner context, while module-vars and
    // namespace remain dynamically scoped around this drain.
    if (!module_body_threw && js_dynamic_import_suppress_module_drain <= 0) {
        js_event_loop_drain();
    }
    // Js57 P4 (Track B3): decrement and (at depth 0) flush queued post-await
    // chunks. Sits AFTER the namespace/module-vars restore so
    // continuations that touch module-level state read whichever active
    // namespace the outer caller had — typically the entry module's.
    js_tla_exit_module();

    if (module_body_threw) {
        // Module body exceptions must remain pending so require() callers can
        // catch them; continuing here made top-level throws print and then
        // return a cached placeholder namespace.
        js_module_record_evaluation_error(spec_item, namespace_obj);
        log_debug("js-mir: module '%s' body threw during evaluation", filename ? filename : "<module>");
        jm_finish_module_transpile(tp, mt, ctx);
        return namespace_obj;
    }

    Item module_evaluation_error = js_module_get_evaluation_error(spec_item);
    if (get_type_id(module_evaluation_error) != LMD_TYPE_NULL) {
        // A deferred TLA continuation can reject after js_main initially
        // returned its namespace; expose that cached rejection to the module
        // importer instead of reporting a successful namespace.
        jm_finish_module_transpile(tp, mt, ctx);
        return js_throw_value(module_evaluation_error);
    }

    // Register the module with its resolved path as key. In normal execution
    // this re-registers the pre-created namespace; if compilation returned a
    // replacement namespace, keep the cache in sync with that result.
    js_module_register(spec_item, namespace_obj);

    // Also register in unified module registry for cross-language access
    module_register_for_runtime(runtime, filename, "js", namespace_obj, ctx);

    log_debug("js-mir: module '%s' loaded successfully", filename);

    // Cleanup transpiler state but DEFER MIR context cleanup
    // (module function pointers must remain alive for the main program)
    jm_finish_module_transpile(tp, mt, ctx);
    return namespace_obj;
}

// ============================================================================
// Pre-scan AST for imports and recursively load all imported modules
// ============================================================================

static void jm_propagate_import_evaluation_error(Item parent_specifier,
        Item dependency_specifier) {
    Item error = js_module_get_evaluation_error(dependency_specifier);
    if (get_type_id(error) != LMD_TYPE_NULL) {
        js_module_record_evaluation_error(parent_specifier, error);
    }
}

void jm_load_imports(Runtime* runtime, JsAstNode* ast, const char* filename) {
    if (!ast || ast->node_type != JS_AST_NODE_PROGRAM) return;
    JsProgramNode* program = (JsProgramNode*)ast;

    JsAstNode* s = program->body;
    while (s) {
        if (s->node_type == JS_AST_NODE_IMPORT_DECLARATION) {
            JsImportNode* imp = (JsImportNode*)s;
            if (imp->source) {
                // Resolve module path relative to current file
                char resolved[512];
                if (filename) {
                    jm_resolve_module_path(filename, imp->source->chars,
                        (int)imp->source->len, resolved, sizeof(resolved));
                } else {
                    snprintf(resolved, sizeof(resolved), "%.*s",
                        (int)imp->source->len, imp->source->chars);
                }

                // Js57 P3 (Track B2): self-import — skip loading because the
                // current module is its own dependency. The module's namespace
                // gets registered by transpile_js_module_to_mir before js_main
                // runs (sites 6188-6190), so reads of the imported binding go
                // through the live-binding path which observes the in-progress
                // namespace.
                if (filename && strcmp(resolved, filename) == 0) {
                    s = s->next;
                    continue;
                }

                // Check if already loaded (also catches circular imports via placeholder)
                String* spec_str = heap_create_name(resolved, strlen(resolved));
                Item spec_item = (Item){.item = s2it(spec_str)};
                Item existing = js_module_get(spec_item);
                if (get_type_id(existing) != LMD_TYPE_NULL) {
                    // Js57 P5: even cached deps still propagate their awaited
                    // target. This is the common case for the second sibling
                    // in `import "a.js"; import "b.js"` where b was already
                    // pulled in as part of a's subgraph.
                    if (filename) {
                        String* cur_str_c = heap_create_name(filename, strlen(filename));
                        Item cur_item_c = (Item){.item = s2it(cur_str_c)};
                        jm_propagate_import_evaluation_error(cur_item_c, spec_item);
                        js_module_inherit_awaited_target(cur_item_c, spec_item);
                        // Js57 P7d-B: cached dep — if it still hasn't finished
                        // its TLA evaluation, register the importer as a parent
                        // so the post-await drain wakes it up.
                        if (js_module_needs_async_settle(spec_item)) {
                            js_module_register_async_parent(spec_item, cur_item_c);
                        }
                    }
                    s = s->next;
                    continue;
                }

                // Register placeholder namespace to guard against circular imports
                Item placeholder_ns = js_new_object();
                js_module_register(spec_item, placeholder_ns);

                // Detect cross-language import: .ls extension → Lambda module
                bool is_lambda_module = jm_path_is_lambda_source(resolved);

                if (is_lambda_module) {
                    // Cross-language import: JS importing a Lambda module
                    log_info("js-mir: cross-language import of Lambda module '%s'", resolved);
                    runtime->js_runtime_used = true;
                    Script* lambda_script = load_script_mir_direct(runtime, resolved, NULL, true);
                    if (lambda_script && lambda_script->jit_context) {
                        // Build namespace object from Lambda's pub declarations
                        Item ns = module_build_lambda_namespace(lambda_script);
                        // Register in JS module system (replaces placeholder)
                        js_module_register(spec_item, ns);
                        // Register in unified module registry
                        module_register_for_runtime(
                            runtime, resolved, "lambda", ns, lambda_script->jit_context);
                        log_info("js-mir: Lambda module '%s' loaded as JS namespace", resolved);
                    } else {
                        log_error("js-mir: failed to compile Lambda module '%s'", resolved);
                    }
                } else {
                    // Same-language import: read and compile JS module
                    char* mod_source = read_text_file(resolved);
                    if (mod_source) {
                        transpile_js_module_to_mir(runtime, mod_source, resolved);
                        mem_free(mod_source);
                    } else {
                        log_error("js-mir: cannot read module '%s'", resolved);
                    }
                }
                // Js57 P5: propagate any awaited target from the just-loaded
                // dependency to the importer so dynamic imports that hit the
                // importer chain on the same Promise as the underlying TLA.
                if (filename) {
                    String* cur_str = heap_create_name(filename, strlen(filename));
                    Item cur_item = (Item){.item = s2it(cur_str)};
                    jm_propagate_import_evaluation_error(cur_item, spec_item);
                    js_module_inherit_awaited_target(cur_item, spec_item);
                    // Js57 P7d-B: freshly-loaded dep — if it has TLA or
                    // transitively depends on a TLA module, register the
                    // importer as a parent so the post-await drain wakes it up.
                    if (js_module_needs_async_settle(spec_item)) {
                        js_module_register_async_parent(spec_item, cur_item);
                    }
                }
            }
        }
        s = s->next;
    }
}

// eval() preamble: snapshot of the outer script's module_consts so that
// dynamically compiled code (eval / new Function) can resolve outer-scope
// var declarations via the active context-owned module slab.
