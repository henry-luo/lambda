#include "js_mir_internal.hpp"
#include "../../lib/file.h"
#include "../runtime/lambda-error.h"
#include "../jube/jube_registry.h"

extern "C" void js_dynfunc_cache_reset(void);
static bool jm_module_phase_progress_is_enabled(void);
static void jm_log_module_phase_progress(const char* filename, const char* phase);

static void jm_emit_function_decl_runtime_bindings(JsMirTranspiler* mt,
        JsFunctionNode* fn, MIR_reg_t var_reg, const char* vname) {
    // Function declarations share module persistence and sloppy-eval export rules regardless of closure shape.
    JsModuleConstEntry pmlookup;
    pmlookup.name = jm_persist_name(vname);
    JsModuleConstEntry* pmc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &pmlookup);
    if (pmc && pmc->const_type == MCONST_MODVAR) {
        jm_call_void_2(mt, "js_set_module_var",
            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)pmc->int_val),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, var_reg));
    }
    if (mt->is_eval_direct && !mt->is_global_strict) {
        MIR_reg_t fk = jm_box_property_name_literal(mt, fn->name->chars, fn->name->len);
        MIR_reg_t eval_env_active = jm_call_0(mt, "js_eval_env_is_active", MIR_T_I64);
        MIR_label_t global_export = jm_new_label(mt);
        MIR_label_t export_done = jm_new_label(mt);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
            MIR_new_label_op(mt->ctx, global_export),
            MIR_new_reg_op(mt->ctx, eval_env_active)));
        jm_call_void_2(mt, "js_eval_local_export_var",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, fk),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, var_reg));
        MIR_reg_t evalscript_local_active = jm_call_0(mt, "js_262_eval_script_is_active", MIR_T_I64);
        MIR_label_t skip_evalscript_global = jm_new_label(mt);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
            MIR_new_label_op(mt->ctx, skip_evalscript_global),
            MIR_new_reg_op(mt->ctx, evalscript_local_active)));
        // evalScript executes as a Script, so its function binding is global even inside an eval frame.
        jm_call_void_3(mt, "js_define_global_property_v",
            MIR_T_I64, MIR_new_int_op(mt->ctx, 2),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, fk),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, var_reg));
        jm_emit_label(mt, skip_evalscript_global);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, export_done)));
        jm_emit_label(mt, global_export);
        MIR_reg_t evalscript_active = jm_call_0(mt, "js_262_eval_script_is_active", MIR_T_I64);
        MIR_label_t ordinary_eval_export = jm_new_label(mt);
        MIR_label_t global_define_done = jm_new_label(mt);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
            MIR_new_label_op(mt->ctx, ordinary_eval_export),
            MIR_new_reg_op(mt->ctx, evalscript_active)));
        // $262.evalScript creates non-configurable globals; direct eval keeps configurable bindings.
        jm_call_void_3(mt, "js_define_global_property_v",
            MIR_T_I64, MIR_new_int_op(mt->ctx, 2),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, fk),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, var_reg));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, global_define_done)));
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

static JsClassEntry* jm_find_class_entry_by_ast_node(JsMirTranspiler* mt,
        JsAstNode* class_node) {
    if (!mt || !class_node) return NULL;
    for (int ci = 0; ci < mt->class_count; ci++) {
        if ((JsAstNode*)mt->class_entries[ci].node == class_node) {
            return &mt->class_entries[ci];
        }
    }
    return NULL;
}

static JsClassEntry* jm_find_class_for_superclass_binding(JsMirTranspiler* mt,
        JsIdentifierNode* identifier, int depth) {
    if (!mt || !identifier || !identifier->entry || !identifier->entry->node || depth > 8) {
        return NULL;
    }
    JsAstNode* binding = (JsAstNode*)identifier->entry->node;
    if (binding->node_type == JS_AST_NODE_CLASS_DECLARATION ||
        binding->node_type == JS_AST_NODE_CLASS_EXPRESSION) {
        return jm_find_class_entry_by_ast_node(mt, binding);
    }
    if (binding->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
        JsVariableDeclarationNode* declaration = (JsVariableDeclarationNode*)binding;
        for (JsAstNode* item = declaration->declarations; item; item = item->next) {
            if (item->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
            JsVariableDeclaratorNode* candidate = (JsVariableDeclaratorNode*)item;
            if (!candidate->id || candidate->id->node_type != JS_AST_NODE_IDENTIFIER) continue;
            JsIdentifierNode* candidate_id = (JsIdentifierNode*)candidate->id;
            if (!identifier->name || !candidate_id->name ||
                identifier->name->len != candidate_id->name->len ||
                strncmp(identifier->name->chars, candidate_id->name->chars,
                    identifier->name->len) != 0) {
                continue;
            }
            binding = (JsAstNode*)candidate;
            break;
        }
    }
    if (binding->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) return NULL;

    JsVariableDeclaratorNode* declarator = (JsVariableDeclaratorNode*)binding;
    if (!declarator->init) return NULL;
    if (declarator->init->node_type == JS_AST_NODE_CLASS_DECLARATION ||
        declarator->init->node_type == JS_AST_NODE_CLASS_EXPRESSION) {
        return jm_find_class_entry_by_ast_node(mt, declarator->init);
    }
    if (declarator->init->node_type == JS_AST_NODE_IDENTIFIER) {
        return jm_find_class_for_superclass_binding(mt,
            (JsIdentifierNode*)declarator->init, depth + 1);
    }
    return NULL;
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
        if (fc->has_native_version) {
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

// Js57 Track A (P7a): walk the AST collecting names of let/const variables that
// CANNOT be promoted to the module-level scope env because they need
// per-iteration binding semantics. Two categories qualify:
//   1. for-/for-of-/for-in-init lexical bindings (the loop variable itself),
//   2. ANY let/const declared inside a loop body — closures created inside a
//      loop body capture the current iteration's binding, and the shared
//      module env would unify them across iterations (regression observed on
//      built_ins/Array/prototype/toLocaleString/user-provided-tolocalestring-
//      shrink and the TypedArray twin).
// Function/class bodies are skipped — they have their own scope env.
//
// The `in_loop` flag is sticky once set on a statement subtree, so a let
// inside `for { if (cond) { let X = …; } }` still counts as inside-loop.
static void jm_collect_for_init_lexical_names(JsAstNode* node, struct hashmap* names, bool in_loop);

static void jm_note_module_block_lexical_name(struct hashmap* seen, struct hashmap* duplicate_consts,
        const char* name, int var_kind) {
    (void)var_kind;
    if (!name || !seen || !duplicate_consts) return;
    JsNameSetEntry key;
    memset(&key, 0, sizeof(key));
    key.name = jm_persist_name(name);
    JsNameSetEntry* existing = (JsNameSetEntry*)hashmap_get(seen, &key);
    if (existing) {
        jm_name_set_add(duplicate_consts, name);
    } else {
        jm_name_set_add_kind(seen, name, var_kind);
    }
}

static bool jm_child_can_use_parent_scope_env(JsFuncCollected* parent, JsFuncCollected* child) {
    (void)parent;
    return child != NULL;
}

static void jm_collect_function_private_self_name(JsFunctionNode* fn,
        struct hashmap* locals) {
    if (!fn || !locals || fn->node_type != JS_AST_NODE_FUNCTION_EXPRESSION || !fn->name) return;
    const char* self_name = jm_format_name("_js_%.*s",
        (int)fn->name->len, fn->name->chars);
    jm_name_set_add(locals, self_name);
}

static int jm_parent_link_slot_after_captures(JsFuncCollected* child,
        int shared_slot_count) {
    if (!child) return shared_slot_count;
    int link_slot = shared_slot_count;
    // A copied env uses dense capture indices for unremapped captures, so the
    // parent link must live after both dense and remapped slots. Reusing a dense
    // slot turns that captured Item into a pointer during transitive readback.
    if (child->capture_count > link_slot) link_slot = child->capture_count;
    for (int k = 0; k < child->capture_count; k++) {
        int capture_slot = child->captures[k].scope_env_slot;
        if (capture_slot >= link_slot) link_slot = capture_slot + 1;
        int private_slot = child->captures[k].private_env_slot;
        if (private_slot >= link_slot) link_slot = private_slot + 1;
    }
    return link_slot;
}

static bool jm_scope_env_key_binding_start(const FnCapture* cap, uint32_t* out_start) {
    if (!cap || !out_start) return false;
    if (!cap->scope_env_key) return false;
    const char* at = strchr(cap->scope_env_key, '@');
    if (!at || !at[1] || at[1] < '0' || at[1] > '9') return false;
    uint32_t start = 0;
    for (const char* cursor = at + 1; *cursor >= '0' && *cursor <= '9'; cursor++) {
        start = start * 10u + (uint32_t)(*cursor - '0');
    }
    *out_start = start;
    return true;
}

static bool jm_ts_loop_owns_binding(TSNode closure_node, uint32_t binding_start) {
    if (ts_node_is_null(closure_node)) return false;
    uint32_t closure_start = ts_node_start_byte(closure_node);
    for (TSNode node = ts_node_parent(closure_node);
         !ts_node_is_null(node); node = ts_node_parent(node)) {
        const char* type = ts_node_type(node);
        if (strcmp(type, "for_statement") != 0 &&
                strcmp(type, "for_in_statement") != 0) continue;
        TSNode body = ts_node_child_by_field_name(node, "body", 4);
        if (ts_node_is_null(body)) continue;
        bool closure_in_body = closure_start >= ts_node_start_byte(body) &&
            closure_start < ts_node_end_byte(body);
        bool binding_in_body = binding_start >= ts_node_start_byte(body) &&
            binding_start < ts_node_end_byte(body);
        // per-iteration bindings can be declared either in the loop body or
        // in a for/for-in header immediately before that body. A forced
        // capture alone is not evidence of loop ownership: normal nested
        // callbacks force their parent lexical cell too.
        bool binding_in_header = binding_start >= ts_node_start_byte(node) &&
            binding_start < ts_node_start_byte(body);
        if (closure_in_body && (binding_in_body || binding_in_header)) return true;
    }
    return false;
}

static bool jm_capture_is_loop_private(JsFuncCollected* child,
        JsFuncCollected* parent, const FnCapture* cap) {
    if (!cap || !cap->force_env_capture || !cap->is_let_const) return false;
    uint32_t binding_start = 0;
    if (!jm_scope_env_key_binding_start(cap, &binding_start)) {
        // binding identity is retained from the AST when no source-keyed
        // capture suffix was produced. This keeps loop ownership independent
        // of copied names and their former fixed buffers.
        if (!cap->entry || !cap->entry->node || !child || !child->node ||
                !parent || !parent->node) return false;
        JsAstNode* binding = (JsAstNode*)cap->entry->node;
        if (ts_node_is_null(binding->node)) return false;
        binding_start = ts_node_start_byte(binding->node);
    }
    if (!child || !child->node || !parent || !parent->node) return false;
    // A lexical declared in a loop body needs one closure cell per iteration;
    // a function-local lexical merely carries a source key to disambiguate it
    // from a same-named module binding and must remain in the shared parent env.
    return jm_ts_loop_owns_binding(child->node->node, binding_start);
}

static void jm_mark_mixed_loop_parent_link(JsFuncCollected* child, JsFuncCollected* parent) {
    if (!child || !parent || parent->scope_env_count <= 0) return;
    bool has_loop_private = false;
    bool has_shared_parent = false;
    for (int k = 0; k < child->capture_count; k++) {
        FnCapture* cap = &child->captures[k];
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
    child->closure_env_has_parent_link = true;
    child->closure_env_parent_link_slot =
        jm_parent_link_slot_after_captures(child, parent->scope_env_count);
    for (int k = 0; k < child->capture_count; k++) {
        FnCapture* cap = &child->captures[k];
        bool loop_private = jm_capture_is_loop_private(child, parent, cap);
        if (!loop_private && cap->scope_env_slot >= 0) {
            cap->grandparent_slot = cap->scope_env_slot;
        }
    }
    log_debug("js-mir: mixed loop closure '%s' keeps shared parent captures via env slot %d",
        child->name, child->closure_env_parent_link_slot);
}

static void jm_count_lexical_pattern_name_for_slot(JsAstNode* pat, const char* name, int* count) {
    if (!pat || !name || !count) return;
    if (pat->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)pat;
        if (!id->name) return;
        const char* vname = jm_format_name("_js_%.*s",
            (int)id->name->len, id->name->chars);
        if (strcmp(vname, name) == 0) (*count)++;
        return;
    }
    if (pat->node_type == JS_AST_NODE_ARRAY_PATTERN || pat->node_type == JS_AST_NODE_ARRAY_EXPRESSION) {
        JsArrayNode* arr = (JsArrayNode*)pat;
        for (JsAstNode* e = arr->elements; e; e = e->next) {
            jm_count_lexical_pattern_name_for_slot(e, name, count);
        }
        return;
    }
    if (pat->node_type == JS_AST_NODE_OBJECT_PATTERN || pat->node_type == JS_AST_NODE_OBJECT_EXPRESSION) {
        JsObjectNode* obj = (JsObjectNode*)pat;
        for (JsAstNode* p = obj->properties; p; p = p->next) {
            jm_count_lexical_pattern_name_for_slot(p, name, count);
        }
        return;
    }
    if (pat->node_type == JS_AST_NODE_PROPERTY) {
        jm_count_lexical_pattern_name_for_slot(((JsPropertyNode*)pat)->value, name, count);
        return;
    }
    if (pat->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
        jm_count_lexical_pattern_name_for_slot(((JsAssignmentPatternNode*)pat)->left, name, count);
        return;
    }
    if (pat->node_type == JS_AST_NODE_REST_ELEMENT ||
        pat->node_type == JS_AST_NODE_REST_PROPERTY ||
        pat->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
        jm_count_lexical_pattern_name_for_slot(((JsSpreadElementNode*)pat)->argument, name, count);
    }
}

static void jm_count_lexical_binding_name_for_slot(JsAstNode* node, const char* name, int* count) {
    if (!node || !name || !count) return;
    switch (node->node_type) {
    case JS_AST_NODE_BLOCK_STATEMENT: {
        for (JsAstNode* s = ((JsBlockNode*)node)->statements; s; s = s->next) {
            jm_count_lexical_binding_name_for_slot(s, name, count);
        }
        return;
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)node;
        if (vd->kind != JS_VAR_LET && vd->kind != JS_VAR_CONST) return;
        for (JsAstNode* d = vd->declarations; d; d = d->next) {
            if (d->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                jm_count_lexical_pattern_name_for_slot(((JsVariableDeclaratorNode*)d)->id, name, count);
            }
        }
        return;
    }
    case JS_AST_NODE_FUNCTION_DECLARATION: {
        JsFunctionNode* fn = (JsFunctionNode*)node;
        if (!fn->name) return;
        const char* fn_name = jm_format_name("_js_%.*s",
            (int)fn->name->len, fn->name->chars);
        if (strcmp(fn_name, name) == 0) (*count)++;
        return;
    }
    case JS_AST_NODE_IF_STATEMENT:
        jm_count_lexical_binding_name_for_slot(((JsIfNode*)node)->consequent, name, count);
        jm_count_lexical_binding_name_for_slot(((JsIfNode*)node)->alternate, name, count);
        return;
    case JS_AST_NODE_FOR_STATEMENT:
        jm_count_lexical_binding_name_for_slot(((JsForNode*)node)->init, name, count);
        jm_count_lexical_binding_name_for_slot(((JsForNode*)node)->body, name, count);
        return;
    case JS_AST_NODE_FOR_OF_STATEMENT:
    case JS_AST_NODE_FOR_IN_STATEMENT:
        jm_count_lexical_binding_name_for_slot(((JsForOfNode*)node)->left, name, count);
        jm_count_lexical_binding_name_for_slot(((JsForOfNode*)node)->body, name, count);
        return;
    case JS_AST_NODE_WHILE_STATEMENT:
        jm_count_lexical_binding_name_for_slot(((JsWhileNode*)node)->body, name, count);
        return;
    case JS_AST_NODE_DO_WHILE_STATEMENT:
        jm_count_lexical_binding_name_for_slot(((JsDoWhileNode*)node)->body, name, count);
        return;
    case JS_AST_NODE_TRY_STATEMENT:
        jm_count_lexical_binding_name_for_slot(((JsTryNode*)node)->block, name, count);
        jm_count_lexical_binding_name_for_slot(((JsTryNode*)node)->handler, name, count);
        jm_count_lexical_binding_name_for_slot(((JsTryNode*)node)->finalizer, name, count);
        return;
    case JS_AST_NODE_CATCH_CLAUSE:
        jm_count_lexical_pattern_name_for_slot(((JsCatchNode*)node)->param, name, count);
        jm_count_lexical_binding_name_for_slot(((JsCatchNode*)node)->body, name, count);
        return;
    case JS_AST_NODE_SWITCH_STATEMENT:
        for (JsAstNode* c = ((JsSwitchNode*)node)->cases; c; c = c->next) {
            jm_count_lexical_binding_name_for_slot(c, name, count);
        }
        return;
    case JS_AST_NODE_SWITCH_CASE:
        for (JsAstNode* s = ((JsSwitchCaseNode*)node)->consequent; s; s = s->next) {
            jm_count_lexical_binding_name_for_slot(s, name, count);
        }
        return;
    case JS_AST_NODE_LABELED_STATEMENT:
        jm_count_lexical_binding_name_for_slot(((JsLabeledStatementNode*)node)->body, name, count);
        return;
    default:
        return;
    }
}

static bool jm_parent_declares_function_var(JsFuncCollected* parent, const char* name) {
    if (!parent || !parent->node || !parent->node->body || !name) return false;
    struct hashmap* vars = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    jm_collect_body_locals(parent->node->body, vars, /*var_only=*/true);
    JsNameSetEntry lookup;
    memset(&lookup, 0, sizeof(lookup));
    lookup.name = jm_persist_name(name);
    JsNameSetEntry* entry = (JsNameSetEntry*)hashmap_get(vars, &lookup);
    bool is_var = entry && !entry->from_func_decl;
    hashmap_free(vars);
    return is_var;
}

static bool jm_parent_var_has_lexical_slot_collision(JsFuncCollected* parent, const char* name) {
    if (!parent || !parent->node || !parent->node->body || !name) return false;
    int count = 0;
    for (JsAstNode* param = parent->node->params; param; param = param->next) {
        jm_count_lexical_pattern_name_for_slot(param, name, &count);
    }
    jm_count_lexical_binding_name_for_slot(parent->node->body, name, &count);
    // Existing lexical/parameter collisions already need keyed slots. A
    // captured var and any same-named nested lexical require them too;
    // distinct cells; otherwise the lexical assignment overwrites the value
    // observed by a callback created after that nested block has exited.
    return count > 1 || (count > 0 && jm_parent_declares_function_var(parent, name));
}

static bool jm_modvar_is_iife_scope_binding(JsModuleConstEntry* mc) {
    return mc && mc->const_type == MCONST_MODVAR &&
        (mc->is_iife_var || mc->is_iife_func_decl);
}

static bool jm_capture_binding_starts_after_function(JsFuncCollected* parent, FnCapture* cap) {
    if (!parent || !parent->node || !cap || !cap->scope_env_key ||
            !cap->scope_env_key[0]) return false;
    const char* at = strchr(cap->scope_env_key, '@');
    if (!at || !at[1]) return false;
    uint32_t binding_start = 0;
    const char* cursor = at + 1;
    if (*cursor < '0' || *cursor > '9') return false;
    while (*cursor >= '0' && *cursor <= '9') {
        binding_start = binding_start * 10u + (uint32_t)(*cursor - '0');
        cursor++;
    }
    // A nested closure can outlive a factory before an outer `var` initializer
    // runs; only that source order needs a second, immediate-parent cell link.
    return binding_start >= ts_node_end_byte(parent->node->node);
}

static bool jm_find_enclosing_lexical_key_for_target(JsAstNode* node, JsAstNode* target,
    const char* name, const char** out_key);

static const char* jm_capture_scope_env_slot_key(JsFuncCollected* parent, JsFuncCollected* child,
        FnCapture* cap) {
    if (!cap) return "";
    // Loop-private captures share their ordinary slot name with loop writeback.
    // Only class methods need a source-keyed forced capture to distinguish an
    // IIFE lexical from its promoted same-named module binding; keying every
    // forced capture disconnects ordinary closures from their parent writes.
    bool needs_binding_key = jm_parent_var_has_lexical_slot_collision(parent, cap->name) ||
        (cap->force_env_capture && child && child->is_class_method);
    if (needs_binding_key) {
        if (!cap->scope_env_key || !cap->scope_env_key[0] ||
                strcmp(cap->scope_env_key, cap->name) == 0) {
            const char* derived_key = NULL;
            JsAstNode* root = parent && parent->node ? parent->node->body : NULL;
            JsAstNode* target = child && child->node ? (JsAstNode*)child->node : NULL;
            if (root && target &&
                jm_find_enclosing_lexical_key_for_target(root, target, cap->name, &derived_key)) {
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

static void jm_note_module_block_lexical_pattern(struct hashmap* seen, struct hashmap* duplicate_consts,
        JsAstNode* pat, int var_kind) {
    if (!pat) return;
    struct hashmap* names = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    jm_collect_pattern_names(pat, names);
    size_t iter = 0;
    void* item = NULL;
    while (hashmap_iter(names, &iter, &item)) {
        JsNameSetEntry* e = (JsNameSetEntry*)item;
        jm_note_module_block_lexical_name(seen, duplicate_consts, e->name, var_kind);
    }
    hashmap_free(names);
}

static void jm_note_module_block_lexical_decl(struct hashmap* seen, struct hashmap* duplicate_consts,
        JsVariableDeclarationNode* vd) {
    if (!vd || (vd->kind != JS_VAR_LET && vd->kind != JS_VAR_CONST)) return;
    for (JsAstNode* d = vd->declarations; d; d = d->next) {
        if (d->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
        JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)d;
        jm_note_module_block_lexical_pattern(seen, duplicate_consts, decl->id, (int)vd->kind);
    }
}

static void jm_note_module_direct_var_decl(struct hashmap* seen, struct hashmap* duplicate_consts,
        JsVariableDeclarationNode* vd) {
    if (!vd) return;
    for (JsAstNode* d = vd->declarations; d; d = d->next) {
        if (d->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
        JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)d;
        jm_note_module_block_lexical_pattern(seen, duplicate_consts, decl->id, (int)vd->kind);
    }
}

static void jm_collect_duplicate_module_block_lexicals(JsAstNode* node,
        struct hashmap* seen, struct hashmap* duplicate_consts, bool direct_program) {
    if (!node) return;
    switch (node->node_type) {
    case JS_AST_NODE_PROGRAM: {
        JsProgramNode* prog = (JsProgramNode*)node;
        for (JsAstNode* s = prog->body; s; s = s->next)
            jm_collect_duplicate_module_block_lexicals(s, seen, duplicate_consts, true);
        return;
    }
    case JS_AST_NODE_VARIABLE_DECLARATION:
        // top-level let/const names seed duplicate detection so a nested block
        // lexical with the same name does not collapse onto the module slot.
        if (direct_program) {
            jm_note_module_direct_var_decl(seen, duplicate_consts, (JsVariableDeclarationNode*)node);
        } else {
            jm_note_module_block_lexical_decl(seen, duplicate_consts, (JsVariableDeclarationNode*)node);
        }
        return;
    case JS_AST_NODE_CLASS_DECLARATION: {
        if (!direct_program) {
            JsClassNode* cls = (JsClassNode*)node;
            if (cls->name) {
                const char* name = jm_format_name("_js_%.*s",
                    (int)cls->name->len, cls->name->chars);
                jm_note_module_block_lexical_name(seen, duplicate_consts, name, (int)JS_VAR_CONST);
            }
        }
        return;
    }
    case JS_AST_NODE_FUNCTION_DECLARATION: {
        if (!direct_program) {
            JsFunctionNode* fn = (JsFunctionNode*)node;
            if (fn->name) {
                const char* name = jm_format_name("_js_%.*s",
                    (int)fn->name->len, fn->name->chars);
                jm_note_module_block_lexical_name(seen, duplicate_consts, name, (int)JS_VAR_LET);
            }
        }
        return;
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        for (JsAstNode* s = blk->statements; s; s = s->next)
            jm_collect_duplicate_module_block_lexicals(s, seen, duplicate_consts, false);
        return;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* n = (JsIfNode*)node;
        jm_collect_duplicate_module_block_lexicals(n->consequent, seen, duplicate_consts, false);
        jm_collect_duplicate_module_block_lexicals(n->alternate, seen, duplicate_consts, false);
        return;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* n = (JsWhileNode*)node;
        jm_collect_duplicate_module_block_lexicals(n->body, seen, duplicate_consts, false);
        return;
    }
    case JS_AST_NODE_DO_WHILE_STATEMENT: {
        JsDoWhileNode* n = (JsDoWhileNode*)node;
        jm_collect_duplicate_module_block_lexicals(n->body, seen, duplicate_consts, false);
        return;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* n = (JsForNode*)node;
        if (n->init && n->init->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
            jm_note_module_block_lexical_decl(seen, duplicate_consts, (JsVariableDeclarationNode*)n->init);
        } else {
            jm_collect_duplicate_module_block_lexicals(n->init, seen, duplicate_consts, false);
        }
        jm_collect_duplicate_module_block_lexicals(n->body, seen, duplicate_consts, false);
        return;
    }
    case JS_AST_NODE_FOR_OF_STATEMENT:
    case JS_AST_NODE_FOR_IN_STATEMENT: {
        JsForOfNode* n = (JsForOfNode*)node;
        if (n->left) {
            if (n->left->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                jm_note_module_block_lexical_decl(seen, duplicate_consts, (JsVariableDeclarationNode*)n->left);
            } else if (n->kind == JS_VAR_LET || n->kind == JS_VAR_CONST) {
                // The AST stores destructuring for-of heads directly as a
                // pattern. Counting identifiers only missed shadowing cells
                // such as `for (let [x] of xs)` and captured the outer x.
                jm_note_module_block_lexical_pattern(
                    seen, duplicate_consts, n->left, (int)n->kind);
            } else {
                jm_collect_duplicate_module_block_lexicals(n->left, seen, duplicate_consts, false);
            }
        }
        jm_collect_duplicate_module_block_lexicals(n->body, seen, duplicate_consts, false);
        return;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* n = (JsTryNode*)node;
        jm_collect_duplicate_module_block_lexicals(n->block, seen, duplicate_consts, false);
        if (n->handler) jm_collect_duplicate_module_block_lexicals(n->handler, seen, duplicate_consts, false);
        jm_collect_duplicate_module_block_lexicals(n->finalizer, seen, duplicate_consts, false);
        return;
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* n = (JsCatchNode*)node;
        jm_note_module_block_lexical_pattern(seen, duplicate_consts, n->param, (int)JS_VAR_LET);
        jm_collect_duplicate_module_block_lexicals(n->body, seen, duplicate_consts, false);
        return;
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* n = (JsSwitchNode*)node;
        for (JsAstNode* c = n->cases; c; c = c->next)
            jm_collect_duplicate_module_block_lexicals(c, seen, duplicate_consts, false);
        return;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* n = (JsSwitchCaseNode*)node;
        for (JsAstNode* s = n->consequent; s; s = s->next)
            jm_collect_duplicate_module_block_lexicals(s, seen, duplicate_consts, false);
        return;
    }
    case JS_AST_NODE_LABELED_STATEMENT: {
        JsLabeledStatementNode* n = (JsLabeledStatementNode*)node;
        jm_collect_duplicate_module_block_lexicals(n->body, seen, duplicate_consts, false);
        return;
    }
    case JS_AST_NODE_EXPORT_DECLARATION: {
        JsExportNode* n = (JsExportNode*)node;
        jm_collect_duplicate_module_block_lexicals(n->declaration, seen, duplicate_consts, direct_program);
        return;
    }
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
        return;
    default:
        return;
    }
}

static void jm_collect_for_init_lexical_pattern(JsAstNode* pat, struct hashmap* names) {
    if (!pat) return;
    if (pat->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)pat;
        if (id->name) {
            const char* fname = jm_format_name("_js_%.*s",
                (int)id->name->len, id->name->chars);
            jm_name_set_add(names, fname);
        }
        return;
    }
    struct hashmap* tmp = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    jm_collect_pattern_names(pat, tmp);
    size_t it = 0; void* item = NULL;
    while (hashmap_iter(tmp, &it, &item)) {
        JsNameSetEntry* ne = (JsNameSetEntry*)item;
        jm_name_set_add(names, ne->name);
    }
    hashmap_free(tmp);
}

static void jm_collect_for_init_lexical_from_decl(JsVariableDeclarationNode* vd, struct hashmap* names) {
    if (!vd) return;
    if (vd->kind != JS_VAR_LET && vd->kind != JS_VAR_CONST) return;
    JsAstNode* d = vd->declarations;
    while (d) {
        if (d->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
            JsVariableDeclaratorNode* vdec = (JsVariableDeclaratorNode*)d;
            jm_collect_for_init_lexical_pattern(vdec->id, names);
        }
        d = d->next;
    }
}

static void jm_collect_for_init_lexical_names(JsAstNode* node, struct hashmap* names, bool in_loop) {
    if (!node) return;
    switch (node->node_type) {
    case JS_AST_NODE_PROGRAM: {
        JsProgramNode* prog = (JsProgramNode*)node;
        for (JsAstNode* s = prog->body; s; s = s->next)
            jm_collect_for_init_lexical_names(s, names, in_loop);
        return;
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        // Only counts when this declaration appears inside a loop body — top-
        // level let/consts (outside any loop) keep their normal promotion.
        if (in_loop) {
            JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)node;
            if (vd->kind == JS_VAR_LET || vd->kind == JS_VAR_CONST) {
                jm_collect_for_init_lexical_from_decl(vd, names);
            }
        }
        return;
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        for (JsAstNode* s = blk->statements; s; s = s->next)
            jm_collect_for_init_lexical_names(s, names, in_loop);
        return;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* n = (JsIfNode*)node;
        jm_collect_for_init_lexical_names(n->consequent, names, in_loop);
        jm_collect_for_init_lexical_names(n->alternate, names, in_loop);
        return;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* n = (JsWhileNode*)node;
        jm_collect_for_init_lexical_names(n->body, names, /*in_loop=*/true);
        return;
    }
    case JS_AST_NODE_DO_WHILE_STATEMENT: {
        JsDoWhileNode* n = (JsDoWhileNode*)node;
        jm_collect_for_init_lexical_names(n->body, names, /*in_loop=*/true);
        return;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* n = (JsForNode*)node;
        if (n->init && n->init->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
            jm_collect_for_init_lexical_from_decl((JsVariableDeclarationNode*)n->init, names);
        }
        jm_collect_for_init_lexical_names(n->body, names, /*in_loop=*/true);
        return;
    }
    case JS_AST_NODE_FOR_OF_STATEMENT:
    case JS_AST_NODE_FOR_IN_STATEMENT: {
        JsForOfNode* n = (JsForOfNode*)node;
        if (n->left && n->left->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
            jm_collect_for_init_lexical_from_decl((JsVariableDeclarationNode*)n->left, names);
        } else if ((n->kind == 1 || n->kind == 2) && n->left) {
            jm_collect_for_init_lexical_pattern(n->left, names);
        }
        jm_collect_for_init_lexical_names(n->body, names, /*in_loop=*/true);
        return;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* n = (JsTryNode*)node;
        jm_collect_for_init_lexical_names(n->block, names, in_loop);
        if (n->handler) jm_collect_for_init_lexical_names(n->handler, names, in_loop);
        jm_collect_for_init_lexical_names(n->finalizer, names, in_loop);
        return;
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* n = (JsCatchNode*)node;
        jm_collect_for_init_lexical_names(n->body, names, in_loop);
        return;
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* n = (JsSwitchNode*)node;
        for (JsAstNode* c = n->cases; c; c = c->next)
            jm_collect_for_init_lexical_names(c, names, in_loop);
        return;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* n = (JsSwitchCaseNode*)node;
        for (JsAstNode* s = n->consequent; s; s = s->next)
            jm_collect_for_init_lexical_names(s, names, in_loop);
        return;
    }
    case JS_AST_NODE_LABELED_STATEMENT: {
        JsLabeledStatementNode* n = (JsLabeledStatementNode*)node;
        jm_collect_for_init_lexical_names(n->body, names, in_loop);
        return;
    }
    case JS_AST_NODE_EXPORT_DECLARATION: {
        JsExportNode* n = (JsExportNode*)node;
        jm_collect_for_init_lexical_names(n->declaration, names, in_loop);
        return;
    }
    case JS_AST_NODE_FUNCTION_DECLARATION:
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
    case JS_AST_NODE_CLASS_DECLARATION:
        // Functions and classes have their own lexical environments — for-init
        // lets inside them are handled by the function's own scope_env analysis.
        return;
    default:
        return;
    }
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

static void jm_collect_direct_statement_let_const_names(JsAstNode* stmt, struct hashmap* names) {
    if (!stmt || !names) return;
    if (stmt->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
        JsVariableDeclarationNode* v = (JsVariableDeclarationNode*)stmt;
        if (v->kind == JS_VAR_LET || v->kind == JS_VAR_CONST) {
            JsAstNode* d = v->declarations;
            while (d) {
                if (d->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                    JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)d;
                    if (decl->id) jm_collect_pattern_names(decl->id, names);
                }
                d = d->next;
            }
        }
        return;
    }
}

static struct hashmap* jm_collect_annexb_suppressed_names(JsAstNode* body, bool is_strict) {
    if (!body || is_strict) return NULL;
    struct hashmap* body_hoists = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    struct hashmap* lex_collisions = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    struct hashmap* suppressed = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);

    jm_collect_body_locals(body, body_hoists, true);
    jm_collect_all_let_const_names_recursive(body, lex_collisions);

    size_t iter = 0;
    void* item;
    while (hashmap_iter(body_hoists, &iter, &item)) {
        JsNameSetEntry* e = (JsNameSetEntry*)item;
        if (e->from_func_decl && jm_name_set_has(lex_collisions, e->name)) {
            jm_name_set_add(suppressed, e->name);
        }
    }

    hashmap_free(body_hoists);
    hashmap_free(lex_collisions);
    if (hashmap_count(suppressed) == 0) {
        hashmap_free(suppressed);
        return NULL;
    }
    return suppressed;
}

static void jm_collect_visible_function_scope_names(JsAstNode* body, bool is_strict,
        struct hashmap* names, bool include_direct_lexicals) {
    if (!body || !names) return;
    struct hashmap* hoists = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    struct hashmap* suppressed = jm_collect_annexb_suppressed_names(body, is_strict);

    jm_collect_body_locals(body, hoists, true);
    size_t iter = 0;
    void* item;
    while (hashmap_iter(hoists, &iter, &item)) {
        JsNameSetEntry* e = (JsNameSetEntry*)item;
        if (suppressed && jm_name_set_has(suppressed, e->name)) continue;
        jm_name_set_add(names, e->name);
    }

    if (include_direct_lexicals && body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
        jm_collect_let_const_names(body, names);
    }

    if (suppressed) hashmap_free(suppressed);
    hashmap_free(hoists);
}

static bool jm_ast_node_contains_target(JsAstNode* node, JsAstNode* target) {
    if (!node || !target || ts_node_is_null(node->node) || ts_node_is_null(target->node)) return false;
    uint32_t ns = ts_node_start_byte(node->node);
    uint32_t ne = ts_node_end_byte(node->node);
    uint32_t ts = ts_node_start_byte(target->node);
    uint32_t te = ts_node_end_byte(target->node);
    return ns <= ts && te <= ne;
}

static bool jm_lexical_pattern_matches_name_key(JsAstNode* pat, const char* name,
        const char** out_key) {
    if (!pat || !name || !out_key) return false;
    switch (pat->node_type) {
    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)pat;
        if (!id->name) return false;
        const char* vname = jm_format_name("_js_%.*s",
            (int)id->name->len, id->name->chars);
        if (strcmp(vname, name) != 0) return false;
        uint32_t start = ts_node_is_null(pat->node) ? 0 : ts_node_start_byte(pat->node);
        uint32_t end = ts_node_is_null(pat->node) ? 0 : ts_node_end_byte(pat->node);
        *out_key = jm_format_name("%s@%u:%u", name, start, end);
        return true;
    }
    case JS_AST_NODE_ARRAY_PATTERN:
    case JS_AST_NODE_ARRAY_EXPRESSION:
        for (JsAstNode* e = ((JsArrayNode*)pat)->elements; e; e = e->next) {
            if (jm_lexical_pattern_matches_name_key(e, name, out_key)) return true;
        }
        return false;
    case JS_AST_NODE_OBJECT_PATTERN:
    case JS_AST_NODE_OBJECT_EXPRESSION:
        for (JsAstNode* p = ((JsObjectNode*)pat)->properties; p; p = p->next) {
            if (jm_lexical_pattern_matches_name_key(p, name, out_key)) return true;
        }
        return false;
    case JS_AST_NODE_PROPERTY:
        return jm_lexical_pattern_matches_name_key(((JsPropertyNode*)pat)->value, name, out_key);
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
        return jm_lexical_pattern_matches_name_key(((JsAssignmentPatternNode*)pat)->left, name, out_key);
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
    case JS_AST_NODE_SPREAD_ELEMENT:
        return jm_lexical_pattern_matches_name_key(((JsSpreadElementNode*)pat)->argument, name, out_key);
    default:
        return false;
    }
}

static bool jm_lexical_decl_matches_name(JsAstNode* stmt, const char* name,
        const char** out_key) {
    if (!stmt || !name || !out_key) return false;
    if (stmt->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
        JsFunctionNode* fn = (JsFunctionNode*)stmt;
        if (!fn->name || ts_node_is_null(stmt->node)) return false;
        const char* fn_name = jm_format_name("_js_%.*s",
            (int)fn->name->len, fn->name->chars);
        if (strcmp(fn_name, name) != 0) return false;
        *out_key = jm_format_name("%s@%u:%u", name,
            ts_node_start_byte(stmt->node), ts_node_end_byte(stmt->node));
        return true;
    }
    if (stmt->node_type != JS_AST_NODE_VARIABLE_DECLARATION) return false;
    JsVariableDeclarationNode* var = (JsVariableDeclarationNode*)stmt;
    if (var->kind != JS_VAR_LET && var->kind != JS_VAR_CONST) return false;
    for (JsAstNode* d = var->declarations; d; d = d->next) {
        if (d->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
        JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)d;
        if (jm_lexical_pattern_matches_name_key(decl->id, name, out_key)) return true;
    }
    return false;
}

static bool jm_switch_lexical_decl_matches_name(JsSwitchNode* sw, const char* name,
        const char** out_key) {
    if (!sw || !name || !out_key) return false;
    for (JsAstNode* c = sw->cases; c; c = c->next) {
        if (c->node_type != JS_AST_NODE_SWITCH_CASE) continue;
        JsSwitchCaseNode* sc = (JsSwitchCaseNode*)c;
        for (JsAstNode* s = sc->consequent; s; s = s->next) {
            if (jm_lexical_decl_matches_name(s, name, out_key)) return true;
        }
    }
    return false;
}

static bool jm_find_enclosing_lexical_key_for_target(JsAstNode* node, JsAstNode* target,
        const char* name, const char** out_key) {
    if (!node || !target || !name || !out_key) return false;
    if (node == target) return false;
    if (!jm_ast_node_contains_target(node, target)) {
        return jm_find_enclosing_lexical_key_for_target(node->next, target, name, out_key);
    }

    bool found_here = false;
    switch (node->node_type) {
    case JS_AST_NODE_PROGRAM: {
        JsProgramNode* program = (JsProgramNode*)node;
        for (JsAstNode* s = program->body; s; s = s->next) {
            if (jm_ast_node_contains_target(s, target) &&
                jm_find_enclosing_lexical_key_for_target(s, target, name, out_key)) return true;
        }
        return found_here;
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* block = (JsBlockNode*)node;
        for (JsAstNode* s = block->statements; s; s = s->next) {
            if (jm_lexical_decl_matches_name(s, name, out_key)) found_here = true;
            if (jm_ast_node_contains_target(s, target) &&
                jm_find_enclosing_lexical_key_for_target(s, target, name, out_key)) return true;
        }
        return found_here;
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* var = (JsVariableDeclarationNode*)node;
        for (JsAstNode* d = var->declarations; d; d = d->next) {
            if (jm_find_enclosing_lexical_key_for_target(d, target, name, out_key)) return true;
        }
        return false;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)node;
        return jm_find_enclosing_lexical_key_for_target(decl->init, target, name, out_key);
    }
    case JS_AST_NODE_CLASS_DECLARATION:
    case JS_AST_NODE_CLASS_EXPRESSION: {
        JsClassNode* cls = (JsClassNode*)node;
        if (jm_find_enclosing_lexical_key_for_target(cls->superclass, target, name, out_key)) return true;
        return jm_find_enclosing_lexical_key_for_target(cls->body, target, name, out_key);
    }
    case JS_AST_NODE_FIELD_DEFINITION: {
        JsFieldDefinitionNode* field = (JsFieldDefinitionNode*)node;
        if (field->computed &&
            jm_find_enclosing_lexical_key_for_target(field->key, target, name, out_key)) return true;
        return jm_find_enclosing_lexical_key_for_target(field->value, target, name, out_key);
    }
    case JS_AST_NODE_METHOD_DEFINITION: {
        JsMethodDefinitionNode* method = (JsMethodDefinitionNode*)node;
        if (method->computed &&
            jm_find_enclosing_lexical_key_for_target(method->key, target, name, out_key)) return true;
        return jm_find_enclosing_lexical_key_for_target(method->body, target, name, out_key);
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* try_node = (JsTryNode*)node;
        if (jm_find_enclosing_lexical_key_for_target(try_node->block, target, name, out_key)) return true;
        if (jm_find_enclosing_lexical_key_for_target(try_node->handler, target, name, out_key)) return true;
        return jm_find_enclosing_lexical_key_for_target(try_node->finalizer, target, name, out_key);
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* catch_node = (JsCatchNode*)node;
        if (catch_node->param && jm_ast_node_contains_target(catch_node->param, target)) {
            return jm_lexical_pattern_matches_name_key(catch_node->param, name, out_key) ||
                jm_find_enclosing_lexical_key_for_target(catch_node->param, target, name, out_key);
        }
        if (catch_node->body && jm_ast_node_contains_target(catch_node->body, target)) {
            if (jm_find_enclosing_lexical_key_for_target(catch_node->body, target, name, out_key)) return true;
            // catch parameter bindings are visible throughout the catch body;
            // fall back to them only after body lets/consts fail to match.
            return jm_lexical_pattern_matches_name_key(catch_node->param, name, out_key);
        }
        return false;
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* sw = (JsSwitchNode*)node;
        if (sw->discriminant && jm_ast_node_contains_target(sw->discriminant, target)) {
            return jm_find_enclosing_lexical_key_for_target(sw->discriminant, target, name, out_key);
        }
        if (jm_switch_lexical_decl_matches_name(sw, name, out_key)) found_here = true;
        for (JsAstNode* c = sw->cases; c; c = c->next) {
            if (jm_ast_node_contains_target(c, target) &&
                jm_find_enclosing_lexical_key_for_target(c, target, name, out_key)) return true;
        }
        return found_here;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* sc = (JsSwitchCaseNode*)node;
        if (sc->test && jm_ast_node_contains_target(sc->test, target)) {
            return jm_find_enclosing_lexical_key_for_target(sc->test, target, name, out_key);
        }
        for (JsAstNode* s = sc->consequent; s; s = s->next) {
            if (jm_lexical_decl_matches_name(s, name, out_key)) found_here = true;
            if (jm_ast_node_contains_target(s, target) &&
                jm_find_enclosing_lexical_key_for_target(s, target, name, out_key)) return true;
        }
        return found_here;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* for_node = (JsForNode*)node;
        if (for_node->init && for_node->init->node_type == JS_AST_NODE_VARIABLE_DECLARATION &&
            jm_lexical_decl_matches_name(for_node->init, name, out_key)) {
            found_here = true;
        }
        if (for_node->init && jm_ast_node_contains_target(for_node->init, target) &&
            jm_find_enclosing_lexical_key_for_target(for_node->init, target, name, out_key)) return true;
        if (for_node->test && jm_ast_node_contains_target(for_node->test, target) &&
            jm_find_enclosing_lexical_key_for_target(for_node->test, target, name, out_key)) return true;
        if (for_node->update && jm_ast_node_contains_target(for_node->update, target) &&
            jm_find_enclosing_lexical_key_for_target(for_node->update, target, name, out_key)) return true;
        if (for_node->body && jm_ast_node_contains_target(for_node->body, target) &&
            jm_find_enclosing_lexical_key_for_target(for_node->body, target, name, out_key)) return true;
        return found_here;
    }
    case JS_AST_NODE_FOR_IN_STATEMENT:
    case JS_AST_NODE_FOR_OF_STATEMENT: {
        JsForOfNode* for_node = (JsForOfNode*)node;
        if (for_node->left) {
            if (for_node->left->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                if (jm_lexical_decl_matches_name(for_node->left, name, out_key)) found_here = true;
            } else if (for_node->kind == JS_VAR_LET || for_node->kind == JS_VAR_CONST) {
                if (jm_lexical_pattern_matches_name_key(for_node->left, name, out_key)) found_here = true;
            }
        }
        if (for_node->left && jm_ast_node_contains_target(for_node->left, target) &&
            jm_find_enclosing_lexical_key_for_target(for_node->left, target, name, out_key)) return true;
        if (for_node->right && jm_ast_node_contains_target(for_node->right, target) &&
            jm_find_enclosing_lexical_key_for_target(for_node->right, target, name, out_key)) return true;
        if (for_node->body && jm_ast_node_contains_target(for_node->body, target) &&
            jm_find_enclosing_lexical_key_for_target(for_node->body, target, name, out_key)) return true;
        return found_here;
    }
    default:
        return false;
    }
}

static void jm_collect_pattern_names_kind(JsAstNode* pat, struct hashmap* names, int var_kind) {
    if (!pat || !names) return;
    struct hashmap* tmp = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    jm_collect_pattern_names(pat, tmp);
    size_t iter = 0;
    void* item = NULL;
    while (hashmap_iter(tmp, &iter, &item)) {
        JsNameSetEntry* e = (JsNameSetEntry*)item;
        jm_name_set_add_kind(names, e->name, var_kind);
    }
    hashmap_free(tmp);
}

static void jm_collect_var_decl_names_kind(JsVariableDeclarationNode* var, struct hashmap* names) {
    if (!var || !names || (var->kind != JS_VAR_LET && var->kind != JS_VAR_CONST)) return;
    JsAstNode* decl_node = var->declarations;
    while (decl_node) {
        if (decl_node->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
            JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)decl_node;
            jm_collect_pattern_names_kind(decl->id, names, (int)var->kind);
        }
        decl_node = decl_node->next;
    }
}

static void jm_collect_enclosing_lexicals_for_target(JsAstNode* node,
        JsAstNode* target, struct hashmap* names) {
    if (!node || !target || !names) return;
    if (node == target) return;
    if (!jm_ast_node_contains_target(node, target)) return;

    switch (node->node_type) {
    case JS_AST_NODE_PROGRAM: {
        JsProgramNode* prog = (JsProgramNode*)node;
        for (JsAstNode* s = prog->body; s; s = s->next)
            jm_collect_enclosing_lexicals_for_target(s, target, names);
        break;
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        jm_collect_let_const_names(node, names);
        JsBlockNode* block = (JsBlockNode*)node;
        for (JsAstNode* s = block->statements; s; s = s->next)
            jm_collect_enclosing_lexicals_for_target(s, target, names);
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* var = (JsVariableDeclarationNode*)node;
        for (JsAstNode* d = var->declarations; d; d = d->next)
            jm_collect_enclosing_lexicals_for_target(d, target, names);
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)node;
        jm_collect_enclosing_lexicals_for_target(decl->init, target, names);
        break;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* expr = (JsExpressionStatementNode*)node;
        jm_collect_enclosing_lexicals_for_target(expr->expression, target, names);
        break;
    }
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* ret = (JsReturnNode*)node;
        jm_collect_enclosing_lexicals_for_target(ret->argument, target, names);
        break;
    }
    case JS_AST_NODE_ARRAY_EXPRESSION:
    case JS_AST_NODE_ARRAY_PATTERN: {
        JsArrayNode* arr = (JsArrayNode*)node;
        for (JsAstNode* e = arr->elements; e; e = e->next)
            jm_collect_enclosing_lexicals_for_target(e, target, names);
        break;
    }
    case JS_AST_NODE_OBJECT_EXPRESSION:
    case JS_AST_NODE_OBJECT_PATTERN: {
        JsObjectNode* obj = (JsObjectNode*)node;
        for (JsAstNode* p = obj->properties; p; p = p->next)
            jm_collect_enclosing_lexicals_for_target(p, target, names);
        break;
    }
    case JS_AST_NODE_PROPERTY: {
        JsPropertyNode* prop = (JsPropertyNode*)node;
        if (prop->computed) jm_collect_enclosing_lexicals_for_target(prop->key, target, names);
        jm_collect_enclosing_lexicals_for_target(prop->value, target, names);
        break;
    }
    case JS_AST_NODE_SPREAD_ELEMENT:
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY: {
        JsSpreadElementNode* spread = (JsSpreadElementNode*)node;
        jm_collect_enclosing_lexicals_for_target(spread->argument, target, names);
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_PATTERN: {
        JsAssignmentPatternNode* pat = (JsAssignmentPatternNode*)node;
        jm_collect_enclosing_lexicals_for_target(pat->left, target, names);
        jm_collect_enclosing_lexicals_for_target(pat->right, target, names);
        break;
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        jm_collect_enclosing_lexicals_for_target(bin->left, target, names);
        jm_collect_enclosing_lexicals_for_target(bin->right, target, names);
        break;
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* un = (JsUnaryNode*)node;
        jm_collect_enclosing_lexicals_for_target(un->operand, target, names);
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* assign = (JsAssignmentNode*)node;
        jm_collect_enclosing_lexicals_for_target(assign->left, target, names);
        jm_collect_enclosing_lexicals_for_target(assign->right, target, names);
        break;
    }
    case JS_AST_NODE_CALL_EXPRESSION:
    case JS_AST_NODE_NEW_EXPRESSION: {
        JsCallNode* call = (JsCallNode*)node;
        jm_collect_enclosing_lexicals_for_target(call->callee, target, names);
        for (JsAstNode* arg = call->arguments; arg; arg = arg->next)
            jm_collect_enclosing_lexicals_for_target(arg, target, names);
        break;
    }
    case JS_AST_NODE_MEMBER_EXPRESSION: {
        JsMemberNode* member = (JsMemberNode*)node;
        jm_collect_enclosing_lexicals_for_target(member->object, target, names);
        if (member->computed)
            jm_collect_enclosing_lexicals_for_target(member->property, target, names);
        break;
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* cond = (JsConditionalNode*)node;
        jm_collect_enclosing_lexicals_for_target(cond->test, target, names);
        jm_collect_enclosing_lexicals_for_target(cond->consequent, target, names);
        jm_collect_enclosing_lexicals_for_target(cond->alternate, target, names);
        break;
    }
    case JS_AST_NODE_SEQUENCE_EXPRESSION: {
        JsSequenceNode* seq = (JsSequenceNode*)node;
        for (JsAstNode* e = seq->expressions; e; e = e->next)
            jm_collect_enclosing_lexicals_for_target(e, target, names);
        break;
    }
    case JS_AST_NODE_TEMPLATE_LITERAL: {
        JsTemplateLiteralNode* tmpl = (JsTemplateLiteralNode*)node;
        for (JsAstNode* e = tmpl->expressions; e; e = e->next)
            jm_collect_enclosing_lexicals_for_target(e, target, names);
        break;
    }
    case JS_AST_NODE_TAGGED_TEMPLATE: {
        JsTaggedTemplateNode* tag = (JsTaggedTemplateNode*)node;
        jm_collect_enclosing_lexicals_for_target(tag->tag, target, names);
        jm_collect_enclosing_lexicals_for_target((JsAstNode*)tag->quasi, target, names);
        break;
    }
    case JS_AST_NODE_YIELD_EXPRESSION: {
        JsYieldNode* yield_node = (JsYieldNode*)node;
        jm_collect_enclosing_lexicals_for_target(yield_node->argument, target, names);
        break;
    }
    case JS_AST_NODE_AWAIT_EXPRESSION: {
        JsAwaitNode* await_node = (JsAwaitNode*)node;
        jm_collect_enclosing_lexicals_for_target(await_node->argument, target, names);
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* ifn = (JsIfNode*)node;
        jm_collect_enclosing_lexicals_for_target(ifn->test, target, names);
        jm_collect_enclosing_lexicals_for_target(ifn->consequent, target, names);
        jm_collect_enclosing_lexicals_for_target(ifn->alternate, target, names);
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* for_node = (JsForNode*)node;
        if (for_node->init && for_node->init->node_type == JS_AST_NODE_VARIABLE_DECLARATION)
            jm_collect_var_decl_names_kind((JsVariableDeclarationNode*)for_node->init, names);
        jm_collect_enclosing_lexicals_for_target(for_node->init, target, names);
        jm_collect_enclosing_lexicals_for_target(for_node->test, target, names);
        jm_collect_enclosing_lexicals_for_target(for_node->update, target, names);
        jm_collect_enclosing_lexicals_for_target(for_node->body, target, names);
        break;
    }
    case JS_AST_NODE_FOR_IN_STATEMENT:
    case JS_AST_NODE_FOR_OF_STATEMENT: {
        JsForOfNode* for_node = (JsForOfNode*)node;
        if (for_node->left && for_node->left->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
            jm_collect_var_decl_names_kind((JsVariableDeclarationNode*)for_node->left, names);
        } else if (for_node->left &&
                   (for_node->kind == JS_VAR_LET || for_node->kind == JS_VAR_CONST)) {
            jm_collect_pattern_names_kind(for_node->left, names, for_node->kind);
        }
        jm_collect_enclosing_lexicals_for_target(for_node->left, target, names);
        jm_collect_enclosing_lexicals_for_target(for_node->right, target, names);
        jm_collect_enclosing_lexicals_for_target(for_node->body, target, names);
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* while_node = (JsWhileNode*)node;
        jm_collect_enclosing_lexicals_for_target(while_node->test, target, names);
        jm_collect_enclosing_lexicals_for_target(while_node->body, target, names);
        break;
    }
    case JS_AST_NODE_DO_WHILE_STATEMENT: {
        JsDoWhileNode* do_node = (JsDoWhileNode*)node;
        jm_collect_enclosing_lexicals_for_target(do_node->body, target, names);
        jm_collect_enclosing_lexicals_for_target(do_node->test, target, names);
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* try_node = (JsTryNode*)node;
        jm_collect_enclosing_lexicals_for_target(try_node->block, target, names);
        jm_collect_enclosing_lexicals_for_target(try_node->handler, target, names);
        jm_collect_enclosing_lexicals_for_target(try_node->finalizer, target, names);
        break;
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* catch_node = (JsCatchNode*)node;
        // catch creates a parameter environment before evaluating the catch
        // body. Defaults inside a destructuring catch parameter must capture
        // that parameter environment, but must not see body lexical bindings.
        if (catch_node->param && jm_ast_node_contains_target(catch_node->param, target)) {
            jm_collect_pattern_names_kind(catch_node->param, names, (int)JS_VAR_LET);
            jm_collect_enclosing_lexicals_for_target(catch_node->param, target, names);
        }
        if (catch_node->body && jm_ast_node_contains_target(catch_node->body, target)) {
            jm_collect_pattern_names_kind(catch_node->param, names, (int)JS_VAR_LET);
            jm_collect_enclosing_lexicals_for_target(catch_node->body, target, names);
        }
        break;
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* switch_node = (JsSwitchNode*)node;
        jm_collect_enclosing_lexicals_for_target(switch_node->discriminant, target, names);
        jm_collect_switch_lexical_names(node, names);
        for (JsAstNode* c = switch_node->cases; c; c = c->next)
            jm_collect_enclosing_lexicals_for_target(c, target, names);
        break;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* switch_case = (JsSwitchCaseNode*)node;
        jm_collect_enclosing_lexicals_for_target(switch_case->test, target, names);
        for (JsAstNode* s = switch_case->consequent; s; s = s->next)
            jm_collect_enclosing_lexicals_for_target(s, target, names);
        break;
    }
    case JS_AST_NODE_LABELED_STATEMENT: {
        JsLabeledStatementNode* labeled = (JsLabeledStatementNode*)node;
        jm_collect_enclosing_lexicals_for_target(labeled->body, target, names);
        break;
    }
    case JS_AST_NODE_WITH_STATEMENT: {
        JsWithStatementNode* with_node = (JsWithStatementNode*)node;
        jm_collect_enclosing_lexicals_for_target(with_node->object, target, names);
        jm_collect_enclosing_lexicals_for_target(with_node->body, target, names);
        break;
    }
    default:
        break;
    }
}

static void jm_cleanup_active_mir_state(JsMirCompileRecoveryState* state) {
    if (!state) return;
    for (int i = state->count - 1; i >= 0; i--) {
        ActiveJsTranspileOwner* owner = &state->stack[i];
        if (owner->mt) {
            jm_destroy_mir_transpiler(owner->mt);
            owner->mt = NULL;
        }
    }
    if (state->active_mir_ctx) {
        MIR_finish(state->active_mir_ctx);
        state->active_mir_ctx = NULL;
    }
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

static void jm_abandon_active_mir_after_signal_state(JsMirCompileRecoveryState* state) {
    if (!state) return;
    for (int i = state->count - 1; i >= 0; i--) {
        ActiveJsTranspileOwner* owner = &state->stack[i];
        if (owner->mt) {
            jm_destroy_mir_transpiler(owner->mt);
            owner->mt = NULL;
        }
    }
    if (state->active_mir_ctx) {
        // A recovered SIGSEGV/SIGBUS may leave MIR's import/module lists
        // inconsistent; re-entering MIR_finish can fault while formatting errors.
        state->active_mir_ctx = NULL;
    }
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
    jm_cleanup_active_mir_state(jm_compile_recovery_state_current());
}

void jm_abandon_active_mir_after_signal(void) {
    jm_abandon_active_mir_after_signal_state(jm_compile_recovery_state_current());
}

void jm_compile_recovery_state_destroy_context(JsRuntimeState* runtime_state) {
    if (!runtime_state || !runtime_state->mir_compile_recovery_state) return;
    JsMirCompileRecoveryState* state =
        (JsMirCompileRecoveryState*)runtime_state->mir_compile_recovery_state;
    // Context teardown is a cold ownership boundary. Finish any interrupted
    // compilation before dropping the capsule so no MIR owner crosses realms.
    jm_cleanup_active_mir_state(state);
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
        MIR_finish(module_mir_contexts[i]);
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

// Finish and remove the most recently deferred MIR context.
// Used by eval() to eagerly free MIR contexts for one-shot compiled code
// that is called once and then discarded.
void jm_finish_last_deferred_mir() {
    if (module_mir_context_count > 0) {
        module_mir_context_count--;
        MIR_finish(module_mir_contexts[module_mir_context_count]);
        if (module_mir_source_buffers[module_mir_context_count]) {
            mem_free(module_mir_source_buffers[module_mir_context_count]);
            module_mir_source_buffers[module_mir_context_count] = NULL;
        }
    }
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
                                  bool is_default) {
    // Resolve the value through box_item (handles native-typed variables)
    JsIdentifierNode temp_id;
    memset(&temp_id, 0, sizeof(temp_id));
    temp_id.node_type = JS_AST_NODE_IDENTIFIER;
    temp_id.name = name_pool_create_len(mt->tp->name_pool, name, name_len);

    MIR_reg_t val = jm_transpile_box_item(mt, (JsAstNode*)&temp_id);
    const char* export_key = is_default ? "default" : name;
    int export_key_len = is_default ? 7 : name_len;
    MIR_reg_t key = jm_box_property_name_literal(mt, export_key, export_key_len);
    jm_call_3(mt, "js_property_set", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, mt->namespace_reg),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
}

// Js52 P1: aliased export — resolve the value via local_name, publish under export_name.
// When the two names match, behaves identically to jm_emit_module_export(..., false).
void jm_emit_module_export_aliased(JsMirTranspiler* mt,
                                          const char* local_name, int local_len,
                                          const char* export_name, int export_len) {
    JsIdentifierNode temp_id;
    memset(&temp_id, 0, sizeof(temp_id));
    temp_id.node_type = JS_AST_NODE_IDENTIFIER;
    temp_id.name = name_pool_create_len(mt->tp->name_pool, local_name, local_len);

    MIR_reg_t val = jm_transpile_box_item(mt, (JsAstNode*)&temp_id);
    MIR_reg_t key = jm_box_property_name_literal(mt, export_name, export_len);
    jm_call_3(mt, "js_property_set", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, mt->namespace_reg),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
}

// ============================================================================
// P6: Return type resolver with local variable tracing
// When param types are known, trace local variables back through their
// declarations and assignments to resolve return expression types.
// ============================================================================

// Resolve expression types from formal types and AST-owned local declaration facts.
static bool jm_p6_expr_has_bigint_literal(JsAstNode* node) {
    if (!node) return false;
    switch (node->node_type) {
    case JS_AST_NODE_LITERAL: {
        JsLiteralNode* lit = (JsLiteralNode*)node;
        return lit->literal_type == JS_LITERAL_NUMBER && lit->is_bigint;
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        return jm_p6_expr_has_bigint_literal(bin->left) || jm_p6_expr_has_bigint_literal(bin->right);
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* un = (JsUnaryNode*)node;
        return jm_p6_expr_has_bigint_literal(un->operand);
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* cond = (JsConditionalNode*)node;
        return jm_p6_expr_has_bigint_literal(cond->test) ||
               jm_p6_expr_has_bigint_literal(cond->consequent) ||
               jm_p6_expr_has_bigint_literal(cond->alternate);
    }
    case JS_AST_NODE_CALL_EXPRESSION: {
        JsCallNode* call = (JsCallNode*)node;
        if (jm_p6_expr_has_bigint_literal(call->callee)) return true;
        JsAstNode* arg = call->arguments;
        while (arg) {
            if (jm_p6_expr_has_bigint_literal(arg)) return true;
            arg = arg->next;
        }
        return false;
    }
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* ret = (JsReturnNode*)node;
        return jm_p6_expr_has_bigint_literal(ret->argument);
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)node;
        JsAstNode* decl = vd->declarations;
        while (decl) {
            if (jm_p6_expr_has_bigint_literal(decl)) return true;
            decl = decl->next;
        }
        return false;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* vd = (JsVariableDeclaratorNode*)node;
        return jm_p6_expr_has_bigint_literal(vd->init);
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* es = (JsExpressionStatementNode*)node;
        return jm_p6_expr_has_bigint_literal(es->expression);
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* ifn = (JsIfNode*)node;
        return jm_p6_expr_has_bigint_literal(ifn->test) ||
               jm_p6_expr_has_bigint_literal(ifn->consequent) ||
               jm_p6_expr_has_bigint_literal(ifn->alternate);
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        JsAstNode* stmt = blk->statements;
        while (stmt) {
            if (jm_p6_expr_has_bigint_literal(stmt)) return true;
            stmt = stmt->next;
        }
        return false;
    }
    default:
        return false;
    }
}

static bool jm_p6_type_is_numeric(TypeId type) {
    return type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT;
}

static bool jm_p6_call_matches_name(JsCallNode* call, const char* name) {
    if (!call || !name || !name[0] || !call->callee ||
            call->callee->node_type != JS_AST_NODE_IDENTIFIER) {
        return false;
    }
    JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
    if (!id->name) return false;
    const char* cname = jm_format_name("_js_%.*s",
        (int)id->name->len, id->name->chars);
    return strcmp(cname, name) == 0;
}

static bool jm_p6_expr_has_self_call(JsAstNode* expr, const char* self_name) {
    if (!expr || !self_name || !self_name[0]) return false;
    switch (expr->node_type) {
    case JS_AST_NODE_CALL_EXPRESSION: {
        JsCallNode* call = (JsCallNode*)expr;
        if (jm_p6_call_matches_name(call, self_name)) return true;
        if (jm_p6_expr_has_self_call(call->callee, self_name)) return true;
        JsAstNode* arg = call->arguments;
        while (arg) {
            if (jm_p6_expr_has_self_call(arg, self_name)) return true;
            arg = arg->next;
        }
        return false;
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)expr;
        return jm_p6_expr_has_self_call(bin->left, self_name) ||
            jm_p6_expr_has_self_call(bin->right, self_name);
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* un = (JsUnaryNode*)expr;
        return jm_p6_expr_has_self_call(un->operand, self_name);
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* cond = (JsConditionalNode*)expr;
        return jm_p6_expr_has_self_call(cond->test, self_name) ||
            jm_p6_expr_has_self_call(cond->consequent, self_name) ||
            jm_p6_expr_has_self_call(cond->alternate, self_name);
    }
    default:
        return false;
    }
}

typedef struct JsP6InferenceContext {
    JsFunctionNode* fn;
    const String** param_bindings;
    TypeId* param_types;
    int param_count;
    const char* self_name;
    TypeId self_return_type;
} JsP6InferenceContext;

// p6 only tracks declarations directly in the function body.  Keep the
// binding lookup on the AST so repeated P6 passes cannot retain a copied name
// or accidentally confuse equal long-name prefixes.
static TypeId jm_p6_local_type(const JsP6InferenceContext* p6,
        JsIdentifierNode* identifier) {
    if (!p6 || !p6->fn || !identifier || !identifier->entry ||
            !p6->fn->body ||
            p6->fn->body->node_type != JS_AST_NODE_BLOCK_STATEMENT) {
        return LMD_TYPE_ANY;
    }
    uint32_t use_start = ts_node_is_null(identifier->node)
        ? UINT32_MAX : ts_node_start_byte(identifier->node);
    JsBlockNode* body = (JsBlockNode*)p6->fn->body;
    TypeId found = LMD_TYPE_ANY;
    for (JsAstNode* stmt = body->statements; stmt; stmt = stmt->next) {
        if (stmt->node_type != JS_AST_NODE_VARIABLE_DECLARATION) continue;
        JsVariableDeclarationNode* declaration = (JsVariableDeclarationNode*)stmt;
        for (JsAstNode* node = declaration->declarations; node; node = node->next) {
            if (node->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
            JsVariableDeclaratorNode* declarator = (JsVariableDeclaratorNode*)node;
            if (!declarator->id || declarator->id->node_type != JS_AST_NODE_IDENTIFIER) continue;
            JsIdentifierNode* binding = (JsIdentifierNode*)declarator->id;
            if (binding->entry != identifier->entry) continue;
            if (!ts_node_is_null(declarator->node) &&
                    ts_node_start_byte(declarator->node) > use_start) {
                continue;
            }
            if (jm_p6_type_is_numeric(declarator->p6_type)) {
                found = declarator->p6_type;
            }
        }
    }
    return found;
}

static TypeId jm_p6_expr_type(JsAstNode* expr, const JsP6InferenceContext* p6) {
    if (!expr) return LMD_TYPE_ANY;
    if (expr->node_type == JS_AST_NODE_LITERAL) {
        JsLiteralNode* lit = (JsLiteralNode*)expr;
        if (lit->literal_type == JS_LITERAL_NUMBER)
            return lit->is_bigint ? LMD_TYPE_DECIMAL : LMD_TYPE_FLOAT;
        // A boolean is LMD_TYPE_BOOL, not INT. Reporting INT here opted `let
        // flag = false` into P6's native *numeric* local lane (jm_p6_local_walk
        // admits only INT/FLOAT), so a later `flag = true` stored 1 and the
        // value came back an INT-tagged number: `typeof` said "number" and
        // `=== true` was false. Every other literal-typing site in the JS
        // front end already answers BOOL.
        if (lit->literal_type == JS_LITERAL_BOOLEAN) return LMD_TYPE_BOOL;
        if (lit->literal_type == JS_LITERAL_STRING) return LMD_TYPE_STRING;
        return LMD_TYPE_ANY;
    }
    if (expr->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)expr;
        for (int i = 0; i < p6->param_count; i++)
            if (jm_js_name_equal(id->name, p6->param_bindings[i])) return p6->param_types[i];
        return jm_p6_local_type(p6, id);
    }
    if (expr->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
        JsBinaryNode* bin = (JsBinaryNode*)expr;
        switch (bin->op) {
        case JS_OP_LT: case JS_OP_LE: case JS_OP_GT: case JS_OP_GE:
        case JS_OP_EQ: case JS_OP_NE: case JS_OP_STRICT_EQ: case JS_OP_STRICT_NE:
            return LMD_TYPE_BOOL;
        case JS_OP_DIV: case JS_OP_EXP:
            return LMD_TYPE_FLOAT;
        default: {
            TypeId lt = jm_p6_expr_type(bin->left, p6);
            TypeId rt = jm_p6_expr_type(bin->right, p6);
            if (bin->op == JS_OP_BIT_AND || bin->op == JS_OP_BIT_OR || bin->op == JS_OP_BIT_XOR ||
                bin->op == JS_OP_BIT_LSHIFT || bin->op == JS_OP_BIT_RSHIFT || bin->op == JS_OP_BIT_URSHIFT) {
                // bigint bitwise/shift operators stay boxed; treating them as Number loses the BigInt lane.
                if (jm_p6_type_is_numeric(lt) && jm_p6_type_is_numeric(rt)) return LMD_TYPE_FLOAT;
                return LMD_TYPE_ANY;
            }
            if (bin->op == JS_OP_ADD) {
                if (lt == LMD_TYPE_STRING || rt == LMD_TYPE_STRING) return LMD_TYPE_STRING;
                if (jm_p6_type_is_numeric(lt) && jm_p6_type_is_numeric(rt))
                    return LMD_TYPE_FLOAT;
                return LMD_TYPE_ANY;
            }
            // SUB, MUL, MOD produce JS Number values.
            if (jm_p6_type_is_numeric(lt) && jm_p6_type_is_numeric(rt)) return LMD_TYPE_FLOAT;
            return LMD_TYPE_ANY;
        }}
    }
    if (expr->node_type == JS_AST_NODE_UNARY_EXPRESSION) {
        JsUnaryNode* un = (JsUnaryNode*)expr;
        if (un->op == JS_OP_BIT_NOT) return LMD_TYPE_FLOAT;
        if (un->op == JS_OP_NOT) return LMD_TYPE_BOOL;
        if (un->op == JS_OP_TYPEOF) return LMD_TYPE_STRING;
        if (un->op == JS_OP_MINUS || un->op == JS_OP_PLUS)
            return jm_p6_expr_type(un->operand, p6);
        if (un->op == JS_OP_INCREMENT || un->op == JS_OP_DECREMENT)
            return jm_p6_expr_type(un->operand, p6);
    }
    if (expr->node_type == JS_AST_NODE_CONDITIONAL_EXPRESSION) {
        JsConditionalNode* cond = (JsConditionalNode*)expr;
        TypeId ct = jm_p6_expr_type(cond->consequent, p6);
        TypeId at = jm_p6_expr_type(cond->alternate, p6);
        if (ct == at) return ct;
        if ((ct == LMD_TYPE_INT && at == LMD_TYPE_FLOAT) || (ct == LMD_TYPE_FLOAT && at == LMD_TYPE_INT))
            return LMD_TYPE_FLOAT;
        return LMD_TYPE_ANY;
    }
    if (expr->node_type == JS_AST_NODE_CALL_EXPRESSION) {
        JsCallNode* call = (JsCallNode*)expr;
        if (jm_p6_call_matches_name(call, p6->self_name)) return p6->self_return_type;
    }
    return LMD_TYPE_ANY;
}

static void jm_p6_collect_locals(JsP6InferenceContext* p6) {
    if (!p6 || !p6->fn || !p6->fn->body ||
            p6->fn->body->node_type != JS_AST_NODE_BLOCK_STATEMENT) return;
    JsBlockNode* blk = (JsBlockNode*)p6->fn->body;

    // clear every direct local before recomputing: P6 runs again after
    // call-site narrowing, and a later declaration must not leak an old fact
    // into an earlier initializer on the next pass.
    for (JsAstNode* stmt = blk->statements; stmt; stmt = stmt->next) {
        if (stmt->node_type != JS_AST_NODE_VARIABLE_DECLARATION) continue;
        JsVariableDeclarationNode* declaration = (JsVariableDeclarationNode*)stmt;
        for (JsAstNode* node = declaration->declarations; node; node = node->next) {
            if (node->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                ((JsVariableDeclaratorNode*)node)->p6_type = LMD_TYPE_ANY;
            }
        }
    }

    JsAstNode* stmt = blk->statements;
    while (stmt) {
        if (stmt->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
            JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)stmt;
            JsAstNode* decl = vd->declarations;
            while (decl) {
                if (decl->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                    JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)decl;
                    if (d->id && d->id->node_type == JS_AST_NODE_IDENTIFIER && d->init) {
                        TypeId init_type = jm_p6_expr_type(d->init, p6);
                        if (init_type == LMD_TYPE_INT || init_type == LMD_TYPE_FLOAT) {
                            d->p6_type = init_type;
                        }
                    }
                }
                decl = decl->next;
            }
        }
        stmt = stmt->next;
    }
}

// Walk return statements and resolve their types from AST-owned binding facts.
static void jm_p6_return_walk(JsAstNode* node, const JsP6InferenceContext* p6,
        TypeId* collected, int* count, int max_count, bool skip_self_unknown) {
    if (!node || *count >= max_count) return;
    switch (node->node_type) {
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* ret = (JsReturnNode*)node;
        if (!ret->argument) { collected[(*count)++] = LMD_TYPE_NULL; return; }
        TypeId t = jm_p6_expr_type(ret->argument, p6);
        if (skip_self_unknown && t == LMD_TYPE_ANY &&
                jm_p6_expr_has_self_call(ret->argument, p6->self_name)) {
            return;
        }
        collected[(*count)++] = t;
        return;
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        JsAstNode* s = blk->statements;
        while (s) { jm_p6_return_walk(s, p6, collected, count, max_count,
                        skip_self_unknown); s = s->next; }
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* n = (JsIfNode*)node;
        jm_p6_return_walk(n->consequent, p6, collected, count, max_count, skip_self_unknown);
        jm_p6_return_walk(n->alternate, p6, collected, count, max_count, skip_self_unknown);
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* n = (JsWhileNode*)node;
        jm_p6_return_walk(n->body, p6, collected, count, max_count, skip_self_unknown);
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* n = (JsForNode*)node;
        jm_p6_return_walk(n->body, p6, collected, count, max_count, skip_self_unknown);
        break;
    }
    case JS_AST_NODE_DO_WHILE_STATEMENT: {
        JsDoWhileNode* n = (JsDoWhileNode*)node;
        jm_p6_return_walk(n->body, p6, collected, count, max_count, skip_self_unknown);
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* n = (JsTryNode*)node;
        jm_p6_return_walk(n->block, p6, collected, count, max_count, skip_self_unknown);
        jm_p6_return_walk(n->handler, p6, collected, count, max_count, skip_self_unknown);
        break;
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* n = (JsCatchNode*)node;
        jm_p6_return_walk(n->body, p6, collected, count, max_count, skip_self_unknown);
        break;
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* n = (JsSwitchNode*)node;
        JsAstNode* c = n->cases;
        while (c) { jm_p6_return_walk(c, p6, collected, count, max_count,
            skip_self_unknown); c = c->next; }
        break;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* n = (JsSwitchCaseNode*)node;
        JsAstNode* s = n->consequent;
        while (s) { jm_p6_return_walk(s, p6, collected, count, max_count,
            skip_self_unknown); s = s->next; }
        break;
    }
    default: break;
    }
}

static TypeId jm_p6_unify_return_types(TypeId* collected, int count, bool* ok) {
    TypeId unified = LMD_TYPE_ANY;
    bool has_concrete = false;
    bool has_any = false;
    if (ok) *ok = true;

    for (int i = 0; i < count; i++) {
        if (collected[i] == LMD_TYPE_ANY) { has_any = true; continue; }
        if (collected[i] == LMD_TYPE_NULL) continue;
        if (!has_concrete) {
            unified = collected[i];
            has_concrete = true;
        } else if (collected[i] != unified) {
            if ((unified == LMD_TYPE_INT && collected[i] == LMD_TYPE_FLOAT) ||
                (unified == LMD_TYPE_FLOAT && collected[i] == LMD_TYPE_INT)) {
                unified = LMD_TYPE_FLOAT;
            } else {
                if (ok) *ok = false;
                return LMD_TYPE_ANY;
            }
        }
    }

    if (has_concrete && !has_any) return unified;
    return LMD_TYPE_ANY;
}

// P6: Re-infer the return type of a function using param types and local variable tracing.
void jm_p6_reinfer_return_type(JsFuncCollected* fc) {
    JsFunctionNode* fn = fc->node;
    if (!fn || !fn->body) return;
    if (jm_p6_expr_has_bigint_literal(fn->body)) {
        fc->return_type = LMD_TYPE_ANY;
        return;
    }

    // Keep formal bindings as AST-owned names; fixed C-string copies made
    // return inference sensitive to source-name length.
    const String** param_bindings = (const String**)mem_calloc(
        (size_t)fc->param_count, sizeof(*param_bindings), MEM_CAT_JS_RUNTIME);
    int param_count = fc->param_count;
    TypeId* param_types = (TypeId*)mem_calloc((size_t)param_count,
        sizeof(*param_types), MEM_CAT_JS_RUNTIME);
    if ((param_count > 0 && !param_bindings) ||
            (param_count > 0 && !param_types)) {
        if (param_bindings) mem_free(param_bindings);
        if (param_types) mem_free(param_types);
        return;
    }
    JsAstNode* pn = fn->params;
    for (int i = 0; i < param_count; i++) {
        param_bindings[i] = jm_param_binding_name(pn);
        param_types[i] = jm_param_type(fc, i);
        pn = pn ? pn->next : NULL;
    }

    JsP6InferenceContext p6 = {};
    p6.fn = fn;
    p6.param_bindings = param_bindings;
    p6.param_types = param_types;
    p6.param_count = param_count;
    jm_p6_collect_locals(&p6);

    const char* self_name = NULL;
    if (fn->name) {
        self_name = jm_format_name("_js_%.*s",
            (int)fn->name->len, fn->name->chars);
    }
    p6.self_name = self_name;

    // seed recursive return inference from concrete non-recursive returns, then
    // re-walk all returns with that self type. This keeps `+` numeric only after
    // recursive calls and the other operand are both proven numeric.
    TypeId collected[32];
    int count = 0;
    p6.self_return_type = LMD_TYPE_ANY;
    jm_p6_return_walk(fn->body, &p6, collected, &count, 32, true);

    bool ok = true;
    TypeId inferred = jm_p6_unify_return_types(collected, count, &ok);
    if (!ok) {
        mem_free(param_bindings);
        mem_free(param_types);
        return;
    }

    if (inferred != LMD_TYPE_ANY && self_name && self_name[0]) {
        for (int pass = 0; pass < 4; pass++) {
            count = 0;
            p6.self_return_type = inferred;
            jm_p6_return_walk(fn->body, &p6, collected, &count, 32, false);
            if (count == 0) {
                fc->return_type = LMD_TYPE_NULL;
                mem_free(param_bindings);
                mem_free(param_types);
                return;
            }
            TypeId next = jm_p6_unify_return_types(collected, count, &ok);
            if (!ok || next == LMD_TYPE_ANY) {
                mem_free(param_bindings);
                mem_free(param_types);
                return;
            }
            if (next == inferred) break;
            inferred = next;
        }
    } else {
        count = 0;
        p6.self_return_type = LMD_TYPE_ANY;
        jm_p6_return_walk(fn->body, &p6, collected, &count, 32, false);
        if (count == 0) {
            fc->return_type = LMD_TYPE_NULL;
            mem_free(param_bindings);
            mem_free(param_types);
            return;
        }
        inferred = jm_p6_unify_return_types(collected, count, &ok);
        if (!ok || inferred == LMD_TYPE_ANY) {
            mem_free(param_bindings);
            mem_free(param_types);
            return;
        }
    }

    if (inferred != LMD_TYPE_ANY) {
        fc->return_type = inferred;
        log_info("P6 re-inferred return type for %s: %s",
                 fc->name, inferred == LMD_TYPE_INT ? "INT" : inferred == LMD_TYPE_FLOAT ? "FLOAT" : "OTHER");
    }
    mem_free(param_bindings);
    mem_free(param_types);
}

// ============================================================================
// P6: Call-site type narrowing
// After body-scan inference (Phase 1.75) and widening (Phase 1.76),
// narrow ANY params to INT/FLOAT when ALL call sites agree on the type.
// ============================================================================

static TypeId jm_p6_binary_result_type(JsOperator op, TypeId left, TypeId right) {
    switch (op) {
    case JS_OP_LT: case JS_OP_LE: case JS_OP_GT: case JS_OP_GE:
    case JS_OP_EQ: case JS_OP_NE: case JS_OP_STRICT_EQ: case JS_OP_STRICT_NE:
        return LMD_TYPE_BOOL;
    default:
        break;
    }
    if (left == LMD_TYPE_DECIMAL || right == LMD_TYPE_DECIMAL) return LMD_TYPE_ANY;
    switch (op) {
    case JS_OP_ADD:
        if (left == LMD_TYPE_STRING || right == LMD_TYPE_STRING) return LMD_TYPE_STRING;
        if (jm_p6_type_is_numeric(left) && jm_p6_type_is_numeric(right)) return LMD_TYPE_FLOAT;
        return LMD_TYPE_ANY;
    case JS_OP_SUB: case JS_OP_MUL: case JS_OP_MOD:
        if (jm_p6_type_is_numeric(left) && jm_p6_type_is_numeric(right)) return LMD_TYPE_FLOAT;
        return LMD_TYPE_ANY;
    case JS_OP_DIV: case JS_OP_EXP:
        return LMD_TYPE_FLOAT;
    case JS_OP_BIT_AND: case JS_OP_BIT_OR: case JS_OP_BIT_XOR:
    case JS_OP_BIT_LSHIFT: case JS_OP_BIT_RSHIFT: case JS_OP_BIT_URSHIFT:
        // bigint bitwise/shift operators stay boxed; treating them as Number loses the BigInt lane.
        if (jm_p6_type_is_numeric(left) && jm_p6_type_is_numeric(right)) return LMD_TYPE_FLOAT;
        return LMD_TYPE_ANY;
    default:
        return LMD_TYPE_ANY;
    }
}

// Determine argument type statically from AST (no compiled scope needed).
TypeId jm_p6_static_arg_type(JsMirTranspiler* mt, JsAstNode* arg) {
    if (!arg) return LMD_TYPE_ANY;
    if (arg->node_type == JS_AST_NODE_LITERAL) {
        JsLiteralNode* lit = (JsLiteralNode*)arg;
        if (lit->literal_type == JS_LITERAL_NUMBER) {
            if (lit->is_bigint) return LMD_TYPE_DECIMAL;
            return LMD_TYPE_FLOAT;
        }
        if (lit->literal_type == JS_LITERAL_STRING) return LMD_TYPE_STRING;
        if (lit->literal_type == JS_LITERAL_BOOLEAN) return LMD_TYPE_BOOL;
        return LMD_TYPE_ANY;
    }
    if (arg->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)arg;
        // check module constants
        if (mt->module_consts) {
            JsModuleConstEntry lookup;
            lookup.name = jm_format_name("_js_%.*s",
                (int)id->name->len, id->name->chars);
            JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup);
            if (mc) {
                if (mc->const_type == MCONST_INT) return LMD_TYPE_INT;
                if (mc->const_type == MCONST_FLOAT) return LMD_TYPE_FLOAT;
                if (mc->const_type == MCONST_MODVAR) {
                    if (mc->modvar_type == LMD_TYPE_INT) return LMD_TYPE_INT;
                    if (mc->modvar_type == LMD_TYPE_FLOAT) return LMD_TYPE_FLOAT;
                    if (mc->modvar_type == LMD_TYPE_DECIMAL) return LMD_TYPE_DECIMAL;
                }
            }
        }
        return LMD_TYPE_ANY;
    }
    if (arg->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
        JsBinaryNode* bin = (JsBinaryNode*)arg;
        TypeId lt = jm_p6_static_arg_type(mt, bin->left);
        TypeId rt = jm_p6_static_arg_type(mt, bin->right);
        return jm_p6_binary_result_type(bin->op, lt, rt);
    }
    if (arg->node_type == JS_AST_NODE_UNARY_EXPRESSION) {
        JsUnaryNode* un = (JsUnaryNode*)arg;
        if (un->op == JS_OP_MINUS || un->op == JS_OP_SUB ||
            un->op == JS_OP_PLUS || un->op == JS_OP_ADD)
            return jm_p6_static_arg_type(mt, un->operand);
        if (un->op == JS_OP_BIT_NOT) return LMD_TYPE_FLOAT;
        if (un->op == JS_OP_TYPEOF) return LMD_TYPE_STRING;
        if (un->op == JS_OP_NOT) return LMD_TYPE_BOOL;
        return LMD_TYPE_ANY;
    }
    if (arg->node_type == JS_AST_NODE_CALL_EXPRESSION) {
        // check if the callee has a known return type
        JsCallNode* call = (JsCallNode*)arg;
        JsFuncCollected* callee_fc = jm_find_collected_func_for_call(mt, call);
        if (callee_fc && callee_fc->return_type != LMD_TYPE_ANY)
            return callee_fc->return_type;
    }
    return LMD_TYPE_ANY;
}

static int jm_p6_param_index_for_identifier(JsAstNode* arg, JsFuncCollected* fc) {
    if (!arg || arg->node_type != JS_AST_NODE_IDENTIFIER || !fc || !fc->node) return -1;
    JsIdentifierNode* id = (JsIdentifierNode*)arg;
    JsAstNode* p = fc->node->params;
    for (int i = 0; p && i < fc->param_count; i++, p = p->next) {
        if (jm_js_name_equal(id->name, jm_param_binding_name(p))) return i;
    }
    return -1;
}

static TypeId jm_p6_evidence_type(FnParamEvidence* e) {
    if (!e || e->other_evidence > 0) return LMD_TYPE_ANY;
    if (e->float_evidence > 0) return LMD_TYPE_FLOAT;
    if (e->int_evidence > 0) return LMD_TYPE_FLOAT;
    return LMD_TYPE_ANY;
}

static bool jm_p6_function_has_duplicate_param_names(JsFunctionNode* fn) {
    if (!fn) return false;
    int count = 0;
    for (JsAstNode* p = fn->params; p; p = p->next) {
        const String* pname = jm_param_binding_name(p);
        JsAstNode* prior = fn->params;
        for (int i = 0; prior && i < count; i++, prior = prior->next) {
            if (jm_js_name_equal(pname, jm_param_binding_name(prior))) return true;
        }
        count++;
    }
    return false;
}

static bool jm_p6_function_allows_native_specialization(JsFuncCollected* fc) {
    if (!fc || !fc->node) return false;
    JsFunctionNode* fn = fc->node;
    if (jm_p6_function_has_duplicate_param_names(fn)) return false;
    if (fn->is_arrow && fn->body &&
        fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) return false;
    return true;
}

static TypeId jm_p6_arg_type_with_evidence(JsMirTranspiler* mt, JsAstNode* arg,
                                           JsFuncCollected* fc,
                                           FnParamEvidence* evidence_for_func) {
    int param_index = jm_p6_param_index_for_identifier(arg, fc);
    if (param_index >= 0) {
        TypeId evidence_type = jm_p6_evidence_type(&evidence_for_func[param_index]);
        if (evidence_type != LMD_TYPE_ANY) return evidence_type;
    }
    if (!arg || arg->node_type != JS_AST_NODE_BINARY_EXPRESSION) {
        return jm_p6_static_arg_type(mt, arg);
    }

    JsBinaryNode* bin = (JsBinaryNode*)arg;
    TypeId lt = jm_p6_arg_type_with_evidence(mt, bin->left, fc, evidence_for_func);
    TypeId rt = jm_p6_arg_type_with_evidence(mt, bin->right, fc, evidence_for_func);
    TypeId result = jm_p6_binary_result_type(bin->op, lt, rt);
    return result != LMD_TYPE_ANY ? result : jm_p6_static_arg_type(mt, arg);
}

// Per-function, per-param call-site evidence
// Walk AST collecting call-site argument types for narrowing
void jm_p6_narrow_walk(JsMirTranspiler* mt, JsAstNode* node,
                               FnParamEvidence** evidence) {
    if (!node || !evidence) return;
    switch (node->node_type) {
    case JS_AST_NODE_CALL_EXPRESSION: {
        JsCallNode* call = (JsCallNode*)node;
        JsFuncCollected* callee_fc = jm_find_collected_func_for_call(mt, call);
        if (callee_fc) {
            int fi = (int)(callee_fc - mt->func_entries);
            if (fi >= 0 && fi < mt->func_count && evidence[fi]) {
                JsAstNode* arg = call->arguments;
                for (int pi = 0; pi < callee_fc->param_count; pi++) {
                    TypeId at = arg ? jm_p6_arg_type_with_evidence(mt, arg, callee_fc, evidence[fi]) : LMD_TYPE_ANY;
                    // boolean arguments must stay boxed; treating them as INT makes
                    // native conditions read boxed boolean tags as nonzero numbers.
                    if (at == LMD_TYPE_INT || at == LMD_TYPE_FLOAT)
                        evidence[fi][pi].float_evidence++;
                    else
                        evidence[fi][pi].other_evidence++;
                    if (arg) arg = arg->next;
                }
            }
        }
        // recurse into arguments
        JsAstNode* a = call->arguments;
        while (a) { jm_p6_narrow_walk(mt, a, evidence); a = a->next; }
        jm_p6_narrow_walk(mt, call->callee, evidence);
        break;
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        jm_p6_narrow_walk(mt, bin->left, evidence);
        jm_p6_narrow_walk(mt, bin->right, evidence);
        break;
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* un = (JsUnaryNode*)node;
        jm_p6_narrow_walk(mt, un->operand, evidence);
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* asgn = (JsAssignmentNode*)node;
        jm_p6_narrow_walk(mt, asgn->right, evidence);
        jm_p6_narrow_walk(mt, asgn->left, evidence);
        break;
    }
    case JS_AST_NODE_MEMBER_EXPRESSION: {
        JsMemberNode* mem = (JsMemberNode*)node;
        jm_p6_narrow_walk(mt, mem->object, evidence);
        if (mem->computed) jm_p6_narrow_walk(mt, mem->property, evidence);
        break;
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* cond = (JsConditionalNode*)node;
        jm_p6_narrow_walk(mt, cond->test, evidence);
        jm_p6_narrow_walk(mt, cond->consequent, evidence);
        jm_p6_narrow_walk(mt, cond->alternate, evidence);
        break;
    }
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* ret = (JsReturnNode*)node;
        jm_p6_narrow_walk(mt, ret->argument, evidence);
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)node;
        JsAstNode* d = vd->declarations;
        while (d) { jm_p6_narrow_walk(mt, d, evidence); d = d->next; }
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* vd = (JsVariableDeclaratorNode*)node;
        jm_p6_narrow_walk(mt, vd->init, evidence);
        break;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* es = (JsExpressionStatementNode*)node;
        jm_p6_narrow_walk(mt, es->expression, evidence);
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* ifn = (JsIfNode*)node;
        jm_p6_narrow_walk(mt, ifn->test, evidence);
        jm_p6_narrow_walk(mt, ifn->consequent, evidence);
        jm_p6_narrow_walk(mt, ifn->alternate, evidence);
        break;
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        JsAstNode* s = blk->statements;
        while (s) { jm_p6_narrow_walk(mt, s, evidence); s = s->next; }
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* w = (JsWhileNode*)node;
        jm_p6_narrow_walk(mt, w->test, evidence);
        jm_p6_narrow_walk(mt, w->body, evidence);
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* f = (JsForNode*)node;
        jm_p6_narrow_walk(mt, f->init, evidence);
        jm_p6_narrow_walk(mt, f->test, evidence);
        jm_p6_narrow_walk(mt, f->update, evidence);
        jm_p6_narrow_walk(mt, f->body, evidence);
        break;
    }
    case JS_AST_NODE_FOR_IN_STATEMENT:
    case JS_AST_NODE_FOR_OF_STATEMENT: {
        JsForInNode* fin = (JsForInNode*)node;
        jm_p6_narrow_walk(mt, fin->right, evidence);
        jm_p6_narrow_walk(mt, fin->body, evidence);
        break;
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* sw = (JsSwitchNode*)node;
        jm_p6_narrow_walk(mt, sw->discriminant, evidence);
        JsAstNode* c = sw->cases;
        while (c) { jm_p6_narrow_walk(mt, c, evidence); c = c->next; }
        break;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* sc = (JsSwitchCaseNode*)node;
        jm_p6_narrow_walk(mt, sc->test, evidence);
        JsAstNode* s = sc->consequent;
        while (s) { jm_p6_narrow_walk(mt, s, evidence); s = s->next; }
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* t = (JsTryNode*)node;
        jm_p6_narrow_walk(mt, t->block, evidence);
        jm_p6_narrow_walk(mt, t->handler, evidence);
        jm_p6_narrow_walk(mt, t->finalizer, evidence);
        break;
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* cc = (JsCatchNode*)node;
        jm_p6_narrow_walk(mt, cc->body, evidence);
        break;
    }
    case JS_AST_NODE_DO_WHILE_STATEMENT: {
        JsDoWhileNode* dw = (JsDoWhileNode*)node;
        jm_p6_narrow_walk(mt, dw->body, evidence);
        jm_p6_narrow_walk(mt, dw->test, evidence);
        break;
    }
    case JS_AST_NODE_ARRAY_EXPRESSION: {
        JsArrayNode* arr = (JsArrayNode*)node;
        JsAstNode* e = arr->elements;
        while (e) { jm_p6_narrow_walk(mt, e, evidence); e = e->next; }
        break;
    }
    case JS_AST_NODE_OBJECT_EXPRESSION: {
        JsObjectNode* obj = (JsObjectNode*)node;
        JsAstNode* p = obj->properties;
        while (p) { jm_p6_narrow_walk(mt, p, evidence); p = p->next; }
        break;
    }
    case JS_AST_NODE_PROPERTY: {
        JsPropertyNode* prop = (JsPropertyNode*)node;
        jm_p6_narrow_walk(mt, prop->value, evidence);
        break;
    }
    case JS_AST_NODE_TEMPLATE_LITERAL: {
        JsTemplateLiteralNode* tl = (JsTemplateLiteralNode*)node;
        if (tl->expressions) {
            JsAstNode* e = tl->expressions;
            while (e) { jm_p6_narrow_walk(mt, e, evidence); e = e->next; }
        }
        break;
    }
    case JS_AST_NODE_NEW_EXPRESSION: {
        JsCallNode* ne = (JsCallNode*)node;
        JsAstNode* a = ne->arguments;
        while (a) { jm_p6_narrow_walk(mt, a, evidence); a = a->next; }
        break;
    }
    case JS_AST_NODE_THROW_STATEMENT: {
        JsThrowNode* th = (JsThrowNode*)node;
        jm_p6_narrow_walk(mt, th->argument, evidence);
        break;
    }
    case JS_AST_NODE_SPREAD_ELEMENT: {
        JsSpreadElementNode* sp = (JsSpreadElementNode*)node;
        jm_p6_narrow_walk(mt, sp->argument, evidence);
        break;
    }
    default:
        break;
    }
}

// ============================================================================
// Phase 3.5: Call-site type propagation
// A contradictory argument shape no longer revokes an inferred native body: its
// boxed entry guards the raw call and runs complete boxed lowering on a miss.
// Callback function expressions remain an exclusion because their receiver and
// callback context are not represented by the scalar raw ABI.
// ============================================================================

void jm_callsite_scan_node(JsMirTranspiler* mt, JsAstNode* node) {
    if (!node) return;
    switch (node->node_type) {
    case JS_AST_NODE_CALL_EXPRESSION: {
        JsCallNode* call = (JsCallNode*)node;
        // Recurse into arguments first (depth-first)
        JsAstNode* a = call->arguments;
        while (a) { jm_callsite_scan_node(mt, a); a = a->next; }
        // Check callee arguments against collected function's param types
        JsFuncCollected* callee_fc = jm_find_collected_func_for_call(mt, call);
        if (callee_fc && callee_fc->has_native_version) {
            JsAstNode* arg = call->arguments;
            for (int i = 0; i < callee_fc->param_count; i++) {
                if (!arg) break;
                if (arg->node_type == JS_AST_NODE_LITERAL) {
                    JsLiteralNode* lit = (JsLiteralNode*)arg;
                    TypeId arg_type = LMD_TYPE_ANY;
                    if (lit->literal_type == JS_LITERAL_NUMBER) {
                        if (lit->is_bigint) arg_type = LMD_TYPE_DECIMAL;
                        else arg_type = LMD_TYPE_FLOAT;
                    }
                    else if (lit->literal_type == JS_LITERAL_STRING)
                        arg_type = LMD_TYPE_STRING;
                    else if (lit->literal_type == JS_LITERAL_BOOLEAN)
                        arg_type = LMD_TYPE_BOOL;
                    TypeId expected = jm_param_type(callee_fc, i);
                    bool ok = true;
                    if (expected == LMD_TYPE_INT)
                        ok = (arg_type == LMD_TYPE_INT || arg_type == LMD_TYPE_BOOL || arg_type == LMD_TYPE_ANY);
                    else if (expected == LMD_TYPE_FLOAT)
                        ok = (arg_type == LMD_TYPE_FLOAT || arg_type == LMD_TYPE_INT || arg_type == LMD_TYPE_ANY);
                    if (!ok) {
                        log_debug("js-mir P3.5 callsite: preserving native shape for %s param %d; boxed entry handles literal mismatch",
                            callee_fc->name, i);
                    }
                }
                arg = arg->next;
            }
        }
        // A callback can receive a dynamic receiver/context that the scalar
        // raw ABI does not carry. Its argument guard alone cannot prove that
        // hidden calling convention, so leave callback bodies boxed until the
        // native entry also models it.
        {
            JsAstNode* cb_arg = call->arguments;
            while (cb_arg) {
                if (cb_arg->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
                    cb_arg->node_type == JS_AST_NODE_ARROW_FUNCTION) {
                    JsFuncCollected* cb_fc = jm_find_collected_func(mt, (JsFunctionNode*)cb_arg);
                    if (cb_fc && cb_fc->has_native_version) {
                        log_debug("js-mir P3.5 callsite: callback '%s' stays boxed for dynamic receiver context",
                            cb_fc->name);
                        cb_fc->has_native_version = false;
                    }
                }
                cb_arg = cb_arg->next;
            }
        }
        // Recurse into callee (for method calls)
        jm_callsite_scan_node(mt, call->callee);
        break;
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        jm_callsite_scan_node(mt, bin->left);
        jm_callsite_scan_node(mt, bin->right);
        break;
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* un = (JsUnaryNode*)node;
        jm_callsite_scan_node(mt, un->operand);
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* asgn = (JsAssignmentNode*)node;
        jm_callsite_scan_node(mt, asgn->right);
        break;
    }
    case JS_AST_NODE_MEMBER_EXPRESSION: {
        JsMemberNode* mem = (JsMemberNode*)node;
        jm_callsite_scan_node(mt, mem->object);
        break;
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* cond = (JsConditionalNode*)node;
        jm_callsite_scan_node(mt, cond->test);
        jm_callsite_scan_node(mt, cond->consequent);
        jm_callsite_scan_node(mt, cond->alternate);
        break;
    }
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* ret = (JsReturnNode*)node;
        jm_callsite_scan_node(mt, ret->argument);
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* decl = (JsVariableDeclarationNode*)node;
        JsAstNode* d = decl->declarations;
        while (d) {
            JsVariableDeclaratorNode* vd = (JsVariableDeclaratorNode*)d;
            jm_callsite_scan_node(mt, vd->init);
            d = d->next;
        }
        break;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* es = (JsExpressionStatementNode*)node;
        jm_callsite_scan_node(mt, es->expression);
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* ifn = (JsIfNode*)node;
        jm_callsite_scan_node(mt, ifn->test);
        jm_callsite_scan_node(mt, ifn->consequent);
        jm_callsite_scan_node(mt, ifn->alternate);
        break;
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        JsAstNode* s = blk->statements;
        while (s) { jm_callsite_scan_node(mt, s); s = s->next; }
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* f = (JsForNode*)node;
        jm_callsite_scan_node(mt, f->init);
        jm_callsite_scan_node(mt, f->test);
        jm_callsite_scan_node(mt, f->update);
        jm_callsite_scan_node(mt, f->body);
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* w = (JsWhileNode*)node;
        jm_callsite_scan_node(mt, w->test);
        jm_callsite_scan_node(mt, w->body);
        break;
    }
    case JS_AST_NODE_FOR_IN_STATEMENT:
    case JS_AST_NODE_FOR_OF_STATEMENT: {
        JsForInNode* fi = (JsForInNode*)node;
        jm_callsite_scan_node(mt, fi->right);
        jm_callsite_scan_node(mt, fi->body);
        break;
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* sw = (JsSwitchNode*)node;
        jm_callsite_scan_node(mt, sw->discriminant);
        JsAstNode* c = sw->cases;
        while (c) { jm_callsite_scan_node(mt, c); c = c->next; }
        break;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* sc = (JsSwitchCaseNode*)node;
        jm_callsite_scan_node(mt, sc->test);
        JsAstNode* s = sc->consequent;
        while (s) { jm_callsite_scan_node(mt, s); s = s->next; }
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* t = (JsTryNode*)node;
        jm_callsite_scan_node(mt, t->block);
        jm_callsite_scan_node(mt, t->handler);
        jm_callsite_scan_node(mt, t->finalizer);
        break;
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* cc = (JsCatchNode*)node;
        jm_callsite_scan_node(mt, cc->body);
        break;
    }
    default:
        break;
    }
}

void jm_callsite_propagate(JsMirTranspiler* mt, JsAstNode* program_body) {
    for (int i = 0; i < mt->func_count; i++) {
        JsFuncCollected* fc = &mt->func_entries[i];
        if (fc->node && fc->node->body)
            jm_callsite_scan_node(mt, (JsAstNode*)fc->node->body);
    }
    // v18l: Also scan top-level program statements (not inside any function)
    if (program_body) {
        JsAstNode* s = program_body;
        while (s) { jm_callsite_scan_node(mt, s); s = s->next; }
    }
}

static void jm_emit_evalscript_global_decl_check_name(JsMirTranspiler* mt, String* name, bool is_func) {
    if (!name || name->len <= 0) return;
    MIR_reg_t key_reg = jm_box_property_name_literal(mt, name->chars, name->len);
    jm_call_1(mt,
        is_func ? "js_evalscript_check_global_function_decl" : "js_evalscript_check_global_var_decl",
        MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg));
    jm_emit_error_lane_propagate_check(mt);
}

static void jm_emit_evalscript_global_decl_check_prefixed(JsMirTranspiler* mt, const char* name) {
    if (!name) return;
    if (strncmp(name, "_js_", 4) == 0) name += 4;
    if (!name[0]) return;
    MIR_reg_t key_reg = jm_box_property_name_literal(mt, name, strlen(name));
    jm_call_1(mt, "js_evalscript_check_global_var_decl", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg));
    jm_emit_error_lane_propagate_check(mt);
}

static void jm_emit_evalscript_global_lex_decl_check_name(JsMirTranspiler* mt, String* name) {
    if (!name || name->len <= 0) return;
    MIR_reg_t key_reg = jm_box_property_name_literal(mt, name->chars, name->len);
    jm_call_1(mt, "js_evalscript_check_global_lex_decl", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg));
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
                jm_call_1(mt, "js_evalscript_check_global_lex_decl", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg));
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
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* for_node = (JsForNode*)node;
        jm_emit_evalscript_global_decl_prechecks(mt, for_node->init);
        jm_emit_evalscript_global_decl_prechecks(mt, for_node->body);
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
        const char* vname = jm_format_name("_js_%.*s",
            (int)id->name->len, id->name->chars);
        JsModuleConstEntry lookup;
        memset(&lookup, 0, sizeof(lookup));
        lookup.name = jm_persist_name(vname);
        JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup);
        if (!mc || mc->const_type != MCONST_MODVAR || mc->var_kind != JS_VAR_VAR ||
                mc->is_implicit_global || (int)mc->int_val < 0) {
            return false;
        }
    }
    return true;
}

bool transpile_js_mir_ast(JsMirTranspiler* mt, JsAstNode* root) {
    if (!root || root->node_type != JS_AST_NODE_PROGRAM) {
        log_error("js-mir: expected program node");
        return false;
    }
    mt->root_node = root;

    JsProgramNode* program = (JsProgramNode*)root;

    // v20: Detect program-level "use strict" directive
    mt->is_global_strict = (mt->tp && mt->tp->strict_mode) || program->has_use_strict_directive;

    // Phase 1: Use the collector itself as the count pass so class-method
    // eligibility cannot drift from fill and invalidate published pointers.
    mt->collection_count_only = true;
    jm_collect_functions(mt, root);
    if (mt->collection_failed) {
        log_error("js-mir: failed to count function/class metadata");
        return false;
    }
    mt->func_capacity = mt->func_count;
    mt->class_capacity = mt->class_count;
    mt->func_count = 0;
    mt->class_count = 0;
    // Collection records retain AST/name pointers and generated code can retain
    // metadata derived from them, so allocate them with the transpiler pools.
    mt->func_entries = (JsFuncCollected*)pool_calloc(
        mt->tp->ast_pool, (size_t)mt->func_capacity * sizeof(JsFuncCollected));
    mt->class_entries = (JsClassEntry*)pool_calloc(
        mt->tp->ast_pool, (size_t)mt->class_capacity * sizeof(JsClassEntry));
    if ((mt->func_capacity && !mt->func_entries) ||
        (mt->class_capacity && !mt->class_entries)) {
        log_error("js-mir: failed to allocate exact function/class metadata");
        mt->collection_failed = true;
        return false;
    }
    mt->collection_count_only = false;
    jm_collect_functions(mt, root);
    if (mt->collection_failed ||
        mt->func_count != mt->func_capacity ||
        mt->class_count != mt->class_capacity) {
        // A mismatch means the shared traversal was state-dependent and exact storage is unsafe.
        log_error("js-mir: collection mismatch functions=%d/%d classes=%d/%d",
            mt->func_count, mt->func_capacity, mt->class_count, mt->class_capacity);
        mt->collection_failed = true;
        return false;
    }
    log_debug("js-mir: collected %d functions, %d classes", mt->func_count, mt->class_count);

    // Phase 1.0b: Determine strict mode for each collected function.
    // A function is strict if: (a) it has "use strict" directive, (b) global/module is strict,
    // (c) it's a class method, or (d) its parent is strict (strict propagates down).
    {
        // Step 1: mark functions with own "use strict" directive or global/module strict
        for (int fi = 0; fi < mt->func_count; fi++) {
            JsFuncCollected* e = &mt->func_entries[fi];
            if (mt->is_global_strict || mt->is_module) {
                e->is_strict = true;
            } else if (e->node && jm_has_use_strict_directive(e->node)) {
                e->is_strict = true;
            } else if (e->is_constructor) {
                e->is_strict = true; // class constructors are strict
            }
        }
        // Step 2: mark class methods as strict (class bodies are implicitly strict)
        for (int ci = 0; ci < mt->class_count; ci++) {
            JsClassEntry* ce = &mt->class_entries[ci];
            for (int mi = 0; mi < ce->method_count; mi++) {
                JsClassMethodEntry* me = &ce->methods[mi];
                if (me->fc) {
                    me->fc->is_class_method = true;
                    me->fc->is_strict = true;
                }
            }
        }
        // Step 3: propagate strict from parent to child (func_entries are post-order,
        // so parent_index > child index; iterate in reverse to propagate top-down)
        for (int fi = mt->func_count - 1; fi >= 0; fi--) {
            JsFuncCollected* e = &mt->func_entries[fi];
            if (e->is_strict) {
                // mark all direct children
                for (int ci = 0; ci < fi; ci++) {
                    if (mt->func_entries[ci].parent_index == fi) {
                        mt->func_entries[ci].is_strict = true;
                    }
                }
            }
        }
    }

    // Phase 1.1: Pre-scan top-level const declarations with literal values
    // These become module-level constants accessible from any function scope
    mt->module_consts = hashmap_new(sizeof(JsModuleConstEntry), 16, 0, 0,
        js_module_const_hash, js_module_const_cmp, NULL, NULL);

    // Pre-seed module_consts from preamble (batch mode: test inherits harness definitions)
    if (mt->preamble_entries && mt->preamble_entry_count > 0) {
        for (int i = 0; i < mt->preamble_entry_count; i++) {
            hashmap_set(mt->module_consts, &mt->preamble_entries[i]);
        }
        log_debug("js-mir: pre-seeded %d preamble entries (var_count=%d)",
            mt->preamble_entry_count, mt->preamble_var_count);
    }

    // Assign module var indices for non-literal top-level declarations.
    // These are runtime-computed values (const som = {...}, const X = new Y(), etc.)
    // that need to be accessible from class method closures via js_get_module_var().
    mt->module_var_count = (mt->preamble_entries && mt->preamble_entry_count > 0)
        ? mt->preamble_var_count : 0;
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
                            const char* vname = jm_format_name("_js_%.*s",
                                (int)vid->name->len, vid->name->chars);
                            JsModuleConstEntry lookup;
                            lookup.name = jm_persist_name(vname);
                            if (!hashmap_get(mt->module_consts, &lookup) && mt->module_var_count < JS_MAX_MODULE_VARS) {
                                JsModuleConstEntry mce;
                                memset(&mce, 0, sizeof(mce));
                                mce.name = jm_persist_name(vname);
                                mce.const_type = MCONST_MODVAR;
                                mce.int_val = mt->module_var_count++;
                                mce.var_kind = (int)v->kind;  // v20 TDZ: track let/const/var
                                // Track initial type for module-var inference. JS Number
                                // literals are boxed binary64 values even when integer-looking.
                                mce.modvar_type = 0;  // default: unknown (0 = LMD_TYPE_RAW_POINTER = not tracked)
                                if (vd->init && vd->init->node_type == JS_AST_NODE_LITERAL) {
                                    JsLiteralNode* mlit = (JsLiteralNode*)vd->init;
                                    if (mlit->literal_type == JS_LITERAL_NUMBER) {
                                        if (mlit->is_bigint) {
                                            mce.modvar_type = LMD_TYPE_DECIMAL;
                                        } else {
                                            mce.modvar_type = LMD_TYPE_FLOAT;
                                        }
                                    }
                                }
                                hashmap_set(mt->module_consts, &mce);
                                log_debug("js-mir: module var '%s' index=%d modvar_type=%d",
                                    mce.name, (int)mce.int_val, mce.modvar_type);
                            }
                        } else if (vd->id && (vd->id->node_type == JS_AST_NODE_OBJECT_PATTERN ||
                                               vd->id->node_type == JS_AST_NODE_ARRAY_PATTERN)) {
                            // destructured binding: collect all names from the pattern
                            struct hashmap* pat_names = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
                                jm_name_hash, jm_name_cmp, NULL, NULL);
                            jm_collect_pattern_names(vd->id, pat_names);
                            size_t piter = 0; void* pitem;
                            while (hashmap_iter(pat_names, &piter, &pitem)) {
                                JsNameSetEntry* ne = (JsNameSetEntry*)pitem;
                                JsModuleConstEntry lookup;
                                lookup.name = jm_persist_name(ne->name);
                                if (!hashmap_get(mt->module_consts, &lookup) && mt->module_var_count < JS_MAX_MODULE_VARS) {
                                    JsModuleConstEntry mce;
                                    memset(&mce, 0, sizeof(mce));
                                    mce.name = jm_persist_name(ne->name);
                                    mce.const_type = MCONST_MODVAR;
                                    mce.int_val = mt->module_var_count++;
                                    mce.var_kind = (int)v->kind;
                                    mce.modvar_type = 0;
                                    hashmap_set(mt->module_consts, &mce);
                                    log_debug("js-mir: module var (destructured) '%s' index=%d",
                                        mce.name, (int)mce.int_val);
                                }
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

    // Third pass (b): hoist var declarations from nested positions (for-inits,
    // labeled statements, etc.) to module scope.  In JS, `var` is function-scoped,
    // so `for (var i = 0; ...)` at the top level hoists `i` to module scope.
    // The previous scan only finds top-level VariableDeclaration nodes; this
    // additional scan uses jm_collect_body_locals to find vars recursively.
    {
        struct hashmap* hoisted_vars = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        struct hashmap* eval_lex_collisions = NULL;
        if (!mt->is_global_strict && !mt->is_module) {
            eval_lex_collisions = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            JsAstNode* ls = program->body;
            while (ls) {
                jm_collect_all_let_const_names_recursive(ls, eval_lex_collisions);
                ls = ls->next;
            }
        }
        JsAstNode* s = program->body;
        while (s) {
            JsAstNode* actual = s;
            if (s->node_type == JS_AST_NODE_EXPORT_DECLARATION) {
                JsExportNode* exp = (JsExportNode*)s;
                if (exp->declaration) actual = exp->declaration;
            }
            // Skip top-level variable declarations (already handled above)
            // Also skip function/class declarations (handled below as MCONST_FUNC/MCONST_CLASS)
            if (actual->node_type != JS_AST_NODE_VARIABLE_DECLARATION &&
                actual->node_type != JS_AST_NODE_FUNCTION_DECLARATION &&
                actual->node_type != JS_AST_NODE_CLASS_DECLARATION) {
                jm_collect_body_locals(actual, hoisted_vars, true);  // var_only: only hoist var
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
            if (eval_lex_collisions && e->from_func_decl) {
                JsNameSetEntry lex_lookup;
                memset(&lex_lookup, 0, sizeof(lex_lookup));
                lex_lookup.name = jm_persist_name(e->name);
                if (hashmap_get(eval_lex_collisions, &lex_lookup)) {
                    log_debug("js-mir: suppress AnnexB nested func hoist '%s' (let/const collision)", e->name);
                    continue;
                }
            }
            JsModuleConstEntry lookup;
            lookup.name = jm_persist_name(e->name);
            if (!hashmap_get(mt->module_consts, &lookup) && mt->module_var_count < JS_MAX_MODULE_VARS) {
                JsModuleConstEntry mce;
                memset(&mce, 0, sizeof(mce));
                mce.name = jm_persist_name(e->name);
                mce.const_type = MCONST_MODVAR;
                mce.int_val = mt->module_var_count++;
                mce.modvar_type = 0;
                mce.is_nested_func_hoist = e->from_func_decl;
                hashmap_set(mt->module_consts, &mce);
                log_debug("js-mir: hoisted var '%s' → module_var[%d]%s", mce.name, (int)mce.int_val,
                    e->from_func_decl ? " (nested func decl)" : "");
            }
        }
        if (eval_lex_collisions) hashmap_free(eval_lex_collisions);
        hashmap_free(hoisted_vars);
    }

    // Third pass (c): assign module var indices for import bindings
    // so closures can access imported names via js_get_module_var()
    {
        JsAstNode* s = program->body;
        while (s) {
            if (s->node_type == JS_AST_NODE_IMPORT_DECLARATION) {
                JsImportNode* imp = (JsImportNode*)s;

                // Default import: import X from 'module'
                if (imp->default_name) {
                    const char* vname = jm_format_name("_js_%.*s",
                        (int)imp->default_name->len, imp->default_name->chars);
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
                    JsModuleConstEntry lookup;
                    lookup.name = jm_persist_name(vname);
                    if (!hashmap_get(mt->module_consts, &lookup) && mt->module_var_count < JS_MAX_MODULE_VARS) {
                        JsModuleConstEntry mce;
                        memset(&mce, 0, sizeof(mce));
                        mce.name = jm_persist_name(vname);
                        mce.const_type = MCONST_MODVAR;
                        mce.int_val = mt->module_var_count++;
                        if (is_self_import) {
                            mce.is_live_default_binding = true;
                            mce.live_binding_specifier = name_pool_create_len(
                                mt->tp->name_pool, resolved_pp, (int)strlen(resolved_pp))->chars;
                        }
                        hashmap_set(mt->module_consts, &mce);
                        log_debug("js-mir: import default '%s' → module_var[%d] live=%d",
                            vname, (int)mce.int_val, is_self_import);
                    }
                }

                // Namespace import: import * as X from 'module'
                if (imp->namespace_name) {
                    const char* vname = jm_format_name("_js_%.*s",
                        (int)imp->namespace_name->len, imp->namespace_name->chars);
                    JsModuleConstEntry lookup;
                    lookup.name = jm_persist_name(vname);
                    if (!hashmap_get(mt->module_consts, &lookup) && mt->module_var_count < JS_MAX_MODULE_VARS) {
                        JsModuleConstEntry mce;
                        memset(&mce, 0, sizeof(mce));
                        mce.name = jm_persist_name(vname);
                        mce.const_type = MCONST_MODVAR;
                        mce.int_val = mt->module_var_count++;
                        hashmap_set(mt->module_consts, &mce);
                        log_debug("js-mir: import namespace '%s' → module_var[%d]", vname, (int)mce.int_val);
                    }
                }

                // Named imports: import { a, b as c } from 'module'
                JsAstNode* spec = imp->specifiers;
                while (spec) {
                    if (spec->node_type == JS_AST_NODE_IMPORT_SPECIFIER) {
                        JsImportSpecifierNode* isp = (JsImportSpecifierNode*)spec;
                        const char* vname = jm_format_name("_js_%.*s",
                            (int)isp->local_name->len, isp->local_name->chars);
                        JsModuleConstEntry lookup;
                        lookup.name = jm_persist_name(vname);
                        if (!hashmap_get(mt->module_consts, &lookup) && mt->module_var_count < JS_MAX_MODULE_VARS) {
                            JsModuleConstEntry mce;
                            memset(&mce, 0, sizeof(mce));
                            mce.name = jm_persist_name(vname);
                            mce.const_type = MCONST_MODVAR;
                            mce.int_val = mt->module_var_count++;
                            hashmap_set(mt->module_consts, &mce);
                            log_debug("js-mir: import named '%s' → module_var[%d]", vname, (int)mce.int_val);
                        }
                    }
                    spec = spec->next;
                }
            }
            s = s->next;
        }
    }

    // Third pass (d): detect implicit globals — variables assigned but never declared
    // in their enclosing function. In JS sloppy mode, assigning to an undeclared
    // variable creates a global. We do per-function analysis: for each function
    // (declaration or expression), collect assignments and declarations, and any
    // assigned name that lacks a var/let/const/param declaration in that function
    // is a candidate implicit global.
    //
    // IMPORTANT: A variable assigned-but-not-declared in one function may be a
    // legitimate closure capture if it IS declared in an ANCESTOR function.
    // For example:
    //   function makeRunningSum() {
    //       let n = 0;
    //       return function(x) { n = n + x; return n; };  // n is NOT an implicit global
    //   }
    // So for each candidate, we check if it's declared in an ancestor function
    // (via parent_index chain) or at the top level. Only if it's NOT declared
    // in any ancestor scope is it a true implicit global.
    {
        struct hashmap* implicit_globals = hashmap_new(sizeof(JsNameSetEntry), 64, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);

        // Collect top-level declarations
        struct hashmap* top_declarations = hashmap_new(sizeof(JsNameSetEntry), 64, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);

        // Build per-function declaration sets for ancestor checking
        // func_decl_sets[fi] = set of names declared (var/let/const/param) in function fi
        // IMPORTANT: build ALL decl sets first, then do ancestor checks in a second pass.
        // Functions are collected in post-order (children before parents), so children have
        // lower indices than parents. A single-pass approach would check ancestors before
        // their decl sets are built.
        struct hashmap** func_decl_sets = (struct hashmap**)mem_calloc(mt->func_count, sizeof(struct hashmap*), MEM_CAT_JS_RUNTIME);

        // Pass 1: build declaration sets for all functions
        for (int fi = 0; fi < mt->func_count; fi++) {
            JsFunctionNode* fn = mt->func_entries[fi].node;
            if (!fn || !fn->body) {
                func_decl_sets[fi] = NULL;
                continue;
            }

            struct hashmap* func_declared = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            JsAstNode* param = fn->params;
            while (param) {
                jm_collect_pattern_names(param, func_declared);
                param = param->next;
            }
            jm_collect_body_locals(fn->body, func_declared);
            func_decl_sets[fi] = func_declared;
        }

        // Pass 2: check each function's assignments against ancestors
        for (int fi = 0; fi < mt->func_count; fi++) {
            JsFunctionNode* fn = mt->func_entries[fi].node;
            if (!fn || !fn->body) continue;

            // Collect assignment targets within this function
            struct hashmap* func_assigned = hashmap_new(sizeof(JsNameSetEntry), 64, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            jm_collect_func_assignments(fn->body, func_assigned);

            // assigned - declared = undeclared → candidate implicit globals
            // But only if not declared in an ancestor function (closure capture)
            size_t iter = 0; void* item;
            while (hashmap_iter(func_assigned, &iter, &item)) {
                JsNameSetEntry* e = (JsNameSetEntry*)item;
                if (jm_name_set_has(func_decl_sets[fi], e->name)) continue;  // declared locally

                // Check ancestor chain: if declared in any ancestor, it's a capture
                bool in_ancestor = false;
                int anc_idx = mt->func_entries[fi].parent_index;
                while (anc_idx >= 0 && anc_idx < mt->func_count) {
                    if (func_decl_sets[anc_idx] && jm_name_set_has(func_decl_sets[anc_idx], e->name)) {
                        in_ancestor = true;
                        break;
                    }
                    anc_idx = mt->func_entries[anc_idx].parent_index;
                }
                if (!in_ancestor) {
                    jm_name_set_add(implicit_globals, e->name);
                }
            }

            hashmap_free(func_assigned);
        }

        // Also check top-level assignments (not inside any function)
        struct hashmap* top_assigned = hashmap_new(sizeof(JsNameSetEntry), 64, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        JsAstNode* s = program->body;
        while (s) {
            // Collect top-level declarations
            if (s->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                jm_collect_body_locals(s, top_declarations);
            } else if (s->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
                JsFunctionNode* fn = (JsFunctionNode*)s;
                if (fn->name) {
                    const char* name = jm_format_name("_js_%.*s",
                        (int)fn->name->len, fn->name->chars);
                    jm_name_set_add(top_declarations, name);
                }
            } else if (s->node_type == JS_AST_NODE_CLASS_DECLARATION) {
                JsClassNode* cls = (JsClassNode*)s;
                if (cls->name) {
                    const char* name = jm_format_name("_js_%.*s",
                        (int)cls->name->len, cls->name->chars);
                    jm_name_set_add(top_declarations, name);
                }
            } else {
                jm_collect_body_locals(s, top_declarations);
            }
            // Collect top-level assignments
            if (s->node_type != JS_AST_NODE_FUNCTION_DECLARATION &&
                s->node_type != JS_AST_NODE_FUNCTION_EXPRESSION &&
                s->node_type != JS_AST_NODE_ARROW_FUNCTION) {
                jm_collect_func_assignments(s, top_assigned);
            }
            s = s->next;
        }
        // top assigned - top declared → top-level implicit globals
        {
            size_t iter = 0; void* item;
            while (hashmap_iter(top_assigned, &iter, &item)) {
                JsNameSetEntry* e = (JsNameSetEntry*)item;
                if (!jm_name_set_has(top_declarations, e->name)) {
                    jm_name_set_add(implicit_globals, e->name);
                }
            }
        }
        hashmap_free(top_assigned);

        // Implicit globals no longer create module_vars — reads fall through to
        // js_get_global_property, writes emit js_set_global_property. This avoids
        // shadowing properties set via this.X = val on the global object.
        // Log implicit globals for debugging but don't register them.
        {
            size_t iter = 0; void* item;
            while (hashmap_iter(implicit_globals, &iter, &item)) {
                JsNameSetEntry* e = (JsNameSetEntry*)item;
                if (jm_name_set_has(top_declarations, e->name)) continue;
                JsModuleConstEntry lookup;
                lookup.name = jm_persist_name(e->name);
                if (hashmap_get(mt->module_consts, &lookup)) continue;
                log_info("js-mir: implicit global '%s' (no modvar — uses global property)", e->name);
            }
        }

        hashmap_free(top_declarations);
        for (int fi = 0; fi < mt->func_count; fi++) {
            if (func_decl_sets[fi]) hashmap_free(func_decl_sets[fi]);
        }
        mem_free(func_decl_sets);
        hashmap_free(implicit_globals);
    }

    // Detect function declarations that self-reassign (Babel _typeof pattern etc.).
    // Only mark a function as reassigned if its OWN body contains an assignment
    // to its own name. This avoids false positives from unrelated short-named
    // variables across webpack modules.
    {
        for (int fi = 0; fi < mt->func_count; fi++) {
            JsFunctionNode* fn = mt->func_entries[fi].node;
            if (!fn || !fn->name || !fn->body) continue;
            const char* name = jm_format_name("_js_%.*s",
                (int)fn->name->len, fn->name->chars);
            struct hashmap* self_assigned = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            jm_collect_func_assignments(fn->body, self_assigned);
            if (jm_name_set_has(self_assigned, name)) {
                mt->func_entries[fi].is_reassigned = true;
                log_debug("js-mir: function '%.*s' is self-reassigned — skipping direct call optimization",
                    (int)fn->name->len, fn->name->chars);
            }
            hashmap_free(self_assigned);
        }
    }

    // Detect function declarations whose name collides with another function
    // declaration in the same enclosing scope (e.g., AnnexB B.3.3.3 nested function
    // var-hoisted into the same scope as a top-level function with the same name,
    // or two top-level `function f` decls).  In such cases, the binding is mutable
    // and direct-call dispatch must NOT be used (the runtime register holds the
    // last-written value).
    {
        for (int fi = 0; fi < mt->func_count; fi++) {
            JsFunctionNode* fn_a = mt->func_entries[fi].node;
            if (!fn_a || !fn_a->name) continue;
            if (mt->func_entries[fi].is_reassigned) continue;
            for (int fj = 0; fj < mt->func_count; fj++) {
                if (fi == fj) continue;
                JsFunctionNode* fn_b = mt->func_entries[fj].node;
                if (!fn_b || !fn_b->name) continue;
                if (fn_b->node_type != JS_AST_NODE_FUNCTION_DECLARATION) continue;
                if (fn_a->node_type != JS_AST_NODE_FUNCTION_DECLARATION) break;
                if (mt->func_entries[fi].parent_index != mt->func_entries[fj].parent_index) continue;
                if (fn_a->name->len != fn_b->name->len) continue;
                if (memcmp(fn_a->name->chars, fn_b->name->chars, fn_a->name->len) != 0) continue;
                mt->func_entries[fi].is_reassigned = true;
                log_debug("js-mir: function '%.*s' has duplicate decl in same scope — skipping direct call optimization",
                    (int)fn_a->name->len, fn_a->name->chars);
                break;
            }
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
                        mce.name = jm_format_name("_js_%.*s",
                            (int)fn->name->len, fn->name->chars);
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

            const char* self_name = jm_format_name("_js_%.*s",
                (int)iife_fn->name->len, iife_fn->name->chars);
            struct hashmap* refs = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            jm_collect_body_refs(iife_fn->body, refs);
            bool self_referencing = jm_name_set_has(refs, self_name);
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
                        const char* name = jm_format_name("_js_%.*s",
                            (int)fn->name->len, fn->name->chars);
                        jm_name_set_add(wrapper_bindings, name);
                    }
                } else if (s->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                    JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)s;
                    for (JsAstNode* d = vd->declarations; d; d = d->next) {
                        if (d->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
                        JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)d;
                        if (!decl->id || decl->id->node_type != JS_AST_NODE_IDENTIFIER) continue;
                        JsIdentifierNode* id = (JsIdentifierNode*)decl->id;
                        const char* name = jm_format_name("_js_%.*s",
                            (int)id->name->len, id->name->chars);
                        jm_name_set_add(wrapper_bindings, name);
                    }
                }
            }
            struct hashmap* function_hoists = hashmap_new(sizeof(JsNameSetEntry), 32, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            jm_collect_body_locals(iife_fn->body, function_hoists, true);
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
                        const char* top_name = jm_format_name("_js_%.*s",
                            (int)id->name->len, id->name->chars);
                        if (strcmp(top_name, candidate) == 0) return true;
                    }
                } else if (top->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
                    JsFunctionNode* fn = (JsFunctionNode*)top;
                    if (fn->name) {
                        const char* top_name = jm_format_name("_js_%.*s",
                            (int)fn->name->len, fn->name->chars);
                        if (strcmp(top_name, candidate) == 0) return true;
                    }
                } else if (top->node_type == JS_AST_NODE_CLASS_DECLARATION) {
                    JsClassNode* cls = (JsClassNode*)top;
                    if (cls->name) {
                        const char* top_name = jm_format_name("_js_%.*s",
                            (int)cls->name->len, cls->name->chars);
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
            mce.name = jm_format_name("_js_%.*s",
                (int)fn->name->len, fn->name->chars);
            JsModuleConstEntry lookup;
            lookup.name = jm_persist_name(mce.name);
            if (!iife_binding_is_unique(mce.name)) return;
            if (!hashmap_get(mt->module_consts, &lookup)) {
                mce.const_type = MCONST_MODVAR;
                // Direct IIFE function declarations are promoted out of the
                // wrapper frame; capture analysis must not mistake the original
                // wrapper-local declaration for a normal ancestor capture.
                mce.is_iife_func_decl = true;
                fc->is_iife_func_decl = true;
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
            if (iife_fc) iife_fc->is_iife_body = true;

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
                                    const char* vname = jm_format_name("_js_%.*s",
                                        (int)vid->name->len, vid->name->chars);
                                    if (top_level_declares_name(vname, stmt)) {
                                        d = d->next;
                                        continue;
                                    }
                                    if (!iife_binding_is_unique(vname)) {
                                        d = d->next;
                                        continue;
                                    }
                                    JsModuleConstEntry lookup;
                                    lookup.name = jm_persist_name(vname);
                                    if (!hashmap_get(mt->module_consts, &lookup) && mt->module_var_count < JS_MAX_MODULE_VARS) {
                                        JsModuleConstEntry mce;
                                        memset(&mce, 0, sizeof(mce));
                                        mce.name = jm_persist_name(vname);
                                        mce.const_type = MCONST_MODVAR;
                                        mce.is_iife_var = true;
                                        mce.int_val = mt->module_var_count++;
                                        mce.var_kind = (int)vd->kind;
                                        hashmap_set(mt->module_consts, &mce);
                                        log_debug("js-mir: iife var '%s' → module_var[%d]", vname, (int)mce.int_val);
                                    }
                                }
                            }
                            d = d->next;
                        }
                    }
                    s = s->next;
                }
                struct hashmap* iife_func_hoists = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                    jm_name_hash, jm_name_cmp, NULL, NULL);
                struct hashmap* iife_lex_collisions = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                    jm_name_hash, jm_name_cmp, NULL, NULL);
                bool iife_effective_strict = mt->is_global_strict || mt->is_module ||
                    (iife_fc && iife_fc->is_strict) || jm_has_use_strict_directive(iife_fn);
                jm_collect_all_let_const_names_recursive(iife_fn->body, iife_lex_collisions);
                jm_collect_body_locals(iife_fn->body, iife_func_hoists, true);
                size_t fh_iter = 0; void* fh_item;
                while (hashmap_iter(iife_func_hoists, &fh_iter, &fh_item)) {
                    JsNameSetEntry* e = (JsNameSetEntry*)fh_item;
                    if (!e->from_func_decl) continue;
                    if (iife_effective_strict) continue;
                    if (top_level_declares_name(e->name, stmt)) continue;
                    if (jm_name_set_has(iife_lex_collisions, e->name)) continue;
                    if (!iife_binding_is_unique(e->name)) continue;
                    JsModuleConstEntry lookup;
                    lookup.name = jm_persist_name(e->name);
                    if (!hashmap_get(mt->module_consts, &lookup) && mt->module_var_count < JS_MAX_MODULE_VARS) {
                        JsModuleConstEntry mce;
                        memset(&mce, 0, sizeof(mce));
                        mce.name = jm_persist_name(e->name);
                        mce.const_type = MCONST_MODVAR;
                        mce.int_val = mt->module_var_count++;
                        mce.is_nested_func_hoist = true;
                        mce.is_iife_var = true;
                        hashmap_set(mt->module_consts, &mce);
                        log_debug("js-mir: nested iife func '%s' → module_var[%d]",
                            mce.name, (int)mce.int_val);
                    }
                }
                hashmap_free(iife_lex_collisions);
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
            mce.name = jm_format_name("_js_%.*s",
                (int)ce->name->len, ce->name->chars);
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
                if (ce->name && ce->name->len == super_id->name->len &&
                    strncmp(ce->name->chars, super_id->name->chars, ce->name->len) == 0) {
                    // ClassDefinitionEvaluation evaluates heritage before its
                    // inner class-name binding is initialized. This is a TDZ
                    // read even when an outer var has the same spelling.
                    ce->has_self_extends = true;
                    continue;
                }
                // A minified nested function can reuse a class name as a local
                // alias. Resolve `extends` through the parser's lexical binding
                // before consulting spelling-only class metadata.
                ce->superclass = jm_find_class_for_superclass_binding(mt, super_id, 0);
                if (!ce->superclass && (!super_id->entry || !super_id->entry->node)) {
                    ce->superclass = jm_find_class(mt, super_id->name->chars,
                        (int)super_id->name->len);
                }
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

    // Tune11 P5 composes inherited constructor-shape metadata after type
    // propagation below. Until then, keep parent/child constructor metadata
    // intact so the composed shape can preserve base-first slot order.

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
    // For each function, determine which variables it captures from outer scopes.
    // We build an outer_scope_names set from: top-level variable declarations,
    // function declaration names, and each function's parameters and locals.
    // Then we analyze each function expression/arrow for captures.
    {
        // Build set of all variable names visible at the top level and in enclosing functions
        struct hashmap* all_names = hashmap_new(sizeof(JsNameSetEntry), 64, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        struct hashmap* module_lexical_names = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        struct hashmap* module_shadow_lexicals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        jm_collect_duplicate_module_block_lexicals((JsAstNode*)program,
            module_lexical_names, module_shadow_lexicals, true);

        // Add top-level variable declarations and function names from program body
        // Use jm_collect_body_locals to also capture variables from for-of/for-in
        // loops, try/catch blocks, etc. at the top level
        {
            struct hashmap* top_hoists = hashmap_new(sizeof(JsNameSetEntry), 32, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            struct hashmap* top_lex_collisions = NULL;
            if (!mt->is_global_strict && !mt->is_module) {
                top_lex_collisions = hashmap_new(sizeof(JsNameSetEntry), 32, 0, 0,
                    jm_name_hash, jm_name_cmp, NULL, NULL);
            }
            JsAstNode* s = program->body;
            while (s) {
                jm_collect_body_locals(s, top_hoists, true);
                if (top_lex_collisions) jm_collect_all_let_const_names_recursive(s, top_lex_collisions);
                jm_collect_direct_statement_let_const_names(s, all_names);
                s = s->next;
            }
            size_t th_iter = 0;
            void* th_item;
            while (hashmap_iter(top_hoists, &th_iter, &th_item)) {
                JsNameSetEntry* e = (JsNameSetEntry*)th_item;
                if (e->from_func_decl && (mt->is_global_strict || mt->is_module)) {
                    continue;
                }
                if (e->from_func_decl && top_lex_collisions &&
                    jm_name_set_has(top_lex_collisions, e->name)) {
                    continue;
                }
                jm_name_set_add(all_names, e->name);
            }
            if (top_lex_collisions) hashmap_free(top_lex_collisions);
            hashmap_free(top_hoists);
        }

        // Add class method params and locals (for closures nested inside methods)
        for (int ci = 0; ci < mt->class_count; ci++) {
            JsClassEntry* ce = &mt->class_entries[ci];
            // Add class name itself
            if (ce->name) {
                const char* cname = jm_format_name("_js_%.*s",
                    (int)ce->name->len, ce->name->chars);
                jm_name_set_add(all_names, cname);
            }
            for (int mi = 0; mi < ce->method_count; mi++) {
                JsClassMethodEntry* me = &ce->methods[mi];
                if (!me->fc || !me->fc->node) continue;
                // NOTE: Do NOT add method params/locals to all_names — they are
                // method-scoped. The per-function ancestor chain walk will handle them.
            }
        }

        // Note: We no longer add params/locals from ALL collected functions to all_names.
        // Instead, per-function ancestor scope names are built when analyzing captures.
        // This prevents false captures from variables in unrelated function scopes.

        // Analyze each collected function for captures
        // Instead of passing the flat all_names (which causes false captures from
        // unrelated scopes), build per-function ancestor scope names by walking
        // the parent_index chain. This implements proper lexical scoping.
        for (int i = 0; i < mt->func_count; i++) {
            JsFuncCollected* fc = &mt->func_entries[i];

            // Build ancestor_names: all_names (top-level) + params/locals from ancestor chain
            struct hashmap* ancestor_names = hashmap_new(sizeof(JsNameSetEntry), 64, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);

            // Copy top-level names (module scope variables/functions/constants)
            // These are always visible from any function
            size_t copy_iter = 0; void* copy_item;
            while (hashmap_iter(all_names, &copy_iter, &copy_item)) {
                JsNameSetEntry* e = (JsNameSetEntry*)copy_item;
                jm_name_set_add(ancestor_names, e->name);
            }
            jm_collect_enclosing_lexicals_for_target((JsAstNode*)program,
                (JsAstNode*)fc->node, ancestor_names);

            // Now REMOVE function-level names from all_names that were added from
            // ALL functions indiscriminately (the loop at lines 13118+).
            // Instead, only add names from the actual ancestor chain.
            // Strategy: walk parent_index chain and add params+locals from each ancestor.
            // Also build a separate set of ancestor function-local names (not module-level)
            // so we can detect when a parent function's local shadows a module constant.
            struct hashmap* ancestor_func_locals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            // A nested module block/catch/loop lexical shadows any same-named
            // module cell and must be captured from that lexical environment.
            // Direct module/CommonJS bindings remain live module cells. Only a
            // nested lexical that shadows another module binding needs a private
            // closure cell; copying ordinary wrapper locals freezes their TDZ value.
            struct hashmap* program_lexicals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            jm_collect_enclosing_lexicals_for_target((JsAstNode*)program,
                (JsAstNode*)fc->node, program_lexicals);
            size_t pl_iter = 0;
            void* pl_item = NULL;
            while (hashmap_iter(program_lexicals, &pl_iter, &pl_item)) {
                JsNameSetEntry* lexical = (JsNameSetEntry*)pl_item;
                if (jm_name_set_has(module_shadow_lexicals, lexical->name)) {
                    jm_name_set_add(ancestor_func_locals, lexical->name);
                }
            }
            hashmap_free(program_lexicals);
            int ancestor_idx = fc->parent_index;
            while (ancestor_idx >= 0 && ancestor_idx < mt->func_count) {
                JsFuncCollected* anc = &mt->func_entries[ancestor_idx];
                if (!anc->node) break;
                JsFunctionNode* afn = anc->node;
                // Add ancestor's params
                JsAstNode* ap = afn->params;
                while (ap) {
                    jm_collect_pattern_names(ap, ancestor_names);
                    jm_collect_pattern_names(ap, ancestor_func_locals);
                    ap = ap->next;
                }
                // Add ancestor's function name (for recursive references)
                if (afn->name) {
                    const char* aname = jm_format_name("_js_%.*s",
                        (int)afn->name->len, afn->name->chars);
                    jm_name_set_add(ancestor_names, aname);
                    jm_name_set_add(ancestor_func_locals, aname);
                }
                // Add ancestor's body locals
                if (afn->body) {
                    struct hashmap* anc_locals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                        jm_name_hash, jm_name_cmp, NULL, NULL);
                    jm_collect_visible_function_scope_names(afn->body, anc->is_strict, anc_locals, true);
                    jm_collect_enclosing_lexicals_for_target(afn->body,
                        (JsAstNode*)fc->node, anc_locals);
                    size_t al_iter = 0; void* al_item;
                    while (hashmap_iter(anc_locals, &al_iter, &al_item)) {
                        JsNameSetEntry* e = (JsNameSetEntry*)al_item;
                        jm_name_set_add(ancestor_names, e->name);
                        bool is_iife_promoted_module_var = false;
                        if (mt->module_consts) {
                            JsModuleConstEntry mclookup;
                            mclookup.name = jm_persist_name(e->name);
                            JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                            is_iife_promoted_module_var =
                                jm_modvar_is_iife_scope_binding(mc) && anc->is_iife_body;
                        }
                        if (!is_iife_promoted_module_var) {
                            jm_name_set_add(ancestor_func_locals, e->name);
                        }
                    }
                    hashmap_free(anc_locals);
                }
                ancestor_idx = anc->parent_index;
            }

            jm_analyze_captures(fc, ancestor_names, mt->module_consts, ancestor_func_locals);

            // v29 TDZ: Mark captures that reference let/const variables.
            // Collect let/const names from the enclosing scope(s) and check each capture.
            if (fc->capture_count > 0) {
                struct hashmap* let_const_names = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                    jm_name_hash, jm_name_cmp, NULL, NULL);
                // Collect from program body (top-level let/const)
                {
                    JsAstNode* s = program->body;
                    while (s) {
                        // Also check top-level variable declarations
                        if (s->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                            JsVariableDeclarationNode* v = (JsVariableDeclarationNode*)s;
                            if (v->kind == JS_VAR_LET || v->kind == JS_VAR_CONST) {
                                JsAstNode* d = v->declarations;
                                while (d) {
                                    if (d->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                                        JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)d;
                                        if (decl->id && decl->id->node_type == JS_AST_NODE_IDENTIFIER) {
                                            JsIdentifierNode* id = (JsIdentifierNode*)decl->id;
                                            const char* lname = jm_format_name("_js_%.*s",
                                                (int)id->name->len, id->name->chars);
                                            jm_name_set_add_kind(let_const_names, lname, (int)v->kind);
                                        }
                                    }
                                    d = d->next;
                                }
                            }
                        }
                        s = s->next;
                    }
                    jm_collect_enclosing_lexicals_for_target((JsAstNode*)program,
                        (JsAstNode*)fc->node, let_const_names);
                }
                // Collect from ancestor function bodies
                int anc_idx = fc->parent_index;
                while (anc_idx >= 0 && anc_idx < mt->func_count) {
                    JsFuncCollected* anc = &mt->func_entries[anc_idx];
                    if (anc->node && anc->node->body) {
                        jm_collect_let_const_names(anc->node->body, let_const_names);
                        jm_collect_enclosing_lexicals_for_target(anc->node->body,
                            (JsAstNode*)fc->node, let_const_names);
                    }
                    anc_idx = anc->parent_index;
                }
                // Mark captures
                for (int ci = 0; ci < fc->capture_count; ci++) {
                    if (fc->captures[ci].is_let_const ||
                        strchr(fc->captures[ci].scope_env_key, '@') != NULL) {
                        // A ranged key came from the resolver and can identify
                        // a nearer var binding; do not overwrite it from a
                        // same-named lexical in the ancestor-name fallback.
                        continue;
                    }
                    JsNameSetEntry lookup;
                    memset(&lookup, 0, sizeof(lookup));
                    lookup.name = jm_persist_name(fc->captures[ci].name);
                    int nearest_var_kind = 0;
                    // Resolve the nearest function-scope lexical first. A
                    // minified outer const and inner let commonly share a name;
                    // merging all ancestors into one set lets the outer kind win.
                    int capture_parent = fc->parent_index;
                    while (capture_parent >= 0 && capture_parent < mt->func_count) {
                        JsFuncCollected* parent_fc = &mt->func_entries[capture_parent];
                        struct hashmap* direct_lexicals = hashmap_new(
                            sizeof(JsNameSetEntry), 16, 0, 0,
                            jm_name_hash, jm_name_cmp, NULL, NULL);
                        if (parent_fc->node && parent_fc->node->body) {
                            jm_collect_let_const_names(parent_fc->node->body, direct_lexicals);
                        }
                        JsNameSetEntry* direct =
                            (JsNameSetEntry*)hashmap_get(direct_lexicals, &lookup);
                        if (direct) nearest_var_kind = direct->var_kind;
                        hashmap_free(direct_lexicals);
                        if (nearest_var_kind != 0) break;
                        capture_parent = parent_fc->parent_index;
                    }
                    JsNameSetEntry* lce = nearest_var_kind == 0 ?
                        (JsNameSetEntry*)hashmap_get(let_const_names, &lookup) : NULL;
                    int capture_var_kind = nearest_var_kind != 0 ?
                        nearest_var_kind : (lce ? lce->var_kind : 0);
                    if (capture_var_kind != 0) {
                        fc->captures[ci].is_let_const = true;
                        fc->captures[ci].is_const = (capture_var_kind == JS_VAR_CONST);
                    }
                }
                hashmap_free(let_const_names);
            }

            hashmap_free(ancestor_func_locals);
            hashmap_free(ancestor_names);
        }

        hashmap_free(module_shadow_lexicals);
        hashmap_free(module_lexical_names);

        // Phase 1.6: Transitive capture propagation for multi-level closures.
        // If function G captures variable V from grandparent scope, then G's parent
        // function F must also capture V (even if F doesn't reference V directly).
        // This ensures V is available in F's scope at emit time when creating G's closure.
        // Iterate until no new captures are added (fixed-point).
        {
            bool changed = true;
            int propagation_rounds = 0;
            while (changed && propagation_rounds < 10) {
                changed = false;
                propagation_rounds++;
                for (int i = 0; i < mt->func_count; i++) {
                    JsFuncCollected* child = &mt->func_entries[i];
                    if (child->capture_count == 0) continue;
                    int parent_idx = child->parent_index;
                    if (parent_idx < 0 || parent_idx >= mt->func_count) continue;
                    JsFuncCollected* parent = &mt->func_entries[parent_idx];

                    // Build set of parent's params + locals for quick lookup
                    struct hashmap* parent_own = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                        jm_name_hash, jm_name_cmp, NULL, NULL);
                    JsFunctionNode* pfn = parent->node;
                    JsAstNode* pp = pfn->params;
                    while (pp) {
                        // use jm_collect_pattern_names to handle identifiers, rest params, destructuring
                        jm_collect_pattern_names(pp, parent_own);
                        pp = pp->next;
                    }
                    if (pfn->body) {
                        // Collect body locals.  Only IIFE-promoted module vars are omitted:
                        // ordinary function-local declarations still shadow same-named
                        // module constants and must stop capture propagation.
                        struct hashmap* body_locals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                            jm_name_hash, jm_name_cmp, NULL, NULL);
                        jm_collect_visible_function_scope_names(pfn->body, parent->is_strict, body_locals, true);
                        size_t bl_iter = 0;
                        void* bl_item;
                        while (hashmap_iter(body_locals, &bl_iter, &bl_item)) {
                            JsNameSetEntry* bl_entry = (JsNameSetEntry*)bl_item;
                            bool skip_local_binding = false;
                            if (mt->module_consts) {
                                JsModuleConstEntry mclookup;
                                mclookup.name = jm_persist_name(bl_entry->name);
                                JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                                if (jm_modvar_is_iife_scope_binding(mc) && parent->is_iife_body) {
                                    skip_local_binding = true;
                                }
                            }
                            if (!skip_local_binding) {
                                jm_name_set_add(parent_own, bl_entry->name);
                            }
                        }
                        hashmap_free(body_locals);
                    }
                    if (pfn->body && child->node) {
                        jm_collect_enclosing_lexicals_for_target(pfn->body,
                            (JsAstNode*)child->node, parent_own);
                    }
                    // Also add parent's existing captures as "own" (already available)
                    for (int ci = 0; ci < parent->capture_count; ci++) {
                        jm_name_set_add(parent_own, parent->captures[ci].name);
                    }
                    if (parent->uses_arguments) {
                        jm_name_set_add(parent_own, "_js_arguments");
                    }

                    // Check each capture of child: if it's not in parent's own scope,
                    // parent must also capture it
                    for (int ci = 0; ci < child->capture_count; ci++) {
                        const char* cap_name = child->captures[ci].name;
                        bool cap_is_lexical_this = strcmp(cap_name, "_js_this") == 0;
                        if (cap_is_lexical_this && (!parent->node || !parent->node->is_arrow)) {
                            // A normal function supplies the lexical binding when it
                            // creates its direct arrow child. Only arrow ancestors
                            // must forward the binding through their closure env.
                            continue;
                        }
                        if (jm_name_set_has(parent_own, cap_name)) continue; // parent already has it

                        // Skip self-reference captures: a named function expression's name
                        // is only visible inside its own body (JS spec), not in the parent scope.
                        // Don't propagate it upward — the function resolves it from its own closure env.
                        if (child->node && child->node->name) {
                            const char* child_self_name = jm_format_name("_js_%.*s",
                                (int)child->node->name->len, child->node->name->chars);
                            if (strcmp(cap_name, child_self_name) == 0) continue;
                        }

                        // Check module_consts — no need to propagate compile-time constants.
                        // For MCONST_MODVAR (IIFE-promoted vars), an ancestor function may
                        // define a param with the same name that shadows the module var.
                        // If shadowed, the capture MUST propagate so the local binding
                        // is used rather than the stale module-level value.
                        if (mt->module_consts) {
                            JsModuleConstEntry lookup;
                            lookup.name = jm_persist_name(cap_name);
                            JsModuleConstEntry* mc_prop = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup);
                            if (mc_prop) {
                                // For ALL module_const types (CLASS, FUNC, MODVAR, etc.),
                                // check if an ancestor function declares a local, param, or
                                // function name that shadows this module-level constant.
                                // If shadowed, keep the capture — the local binding takes
                                // precedence over the module constant.
                                bool shadowed_by_ancestor = false;
                                for (int ai = parent_idx; ai >= 0 && ai < mt->func_count;
                                     ai = mt->func_entries[ai].parent_index) {
                                    JsFuncCollected* anc = &mt->func_entries[ai];
                                    if (!anc->node) break;
                                    // Check ancestor's function name (NFE self-reference)
                                    if (anc->node->name) {
                                        const char* aname = jm_format_name("_js_%.*s",
                                            (int)anc->node->name->len, anc->node->name->chars);
                                        if (strcmp(aname, cap_name) == 0) {
                                            shadowed_by_ancestor = true;
                                            break;
                                        }
                                    }
                                    // Check params
                                    {
                                        struct hashmap* anc_params = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                                            jm_name_hash, jm_name_cmp, NULL, NULL);
                                        JsAstNode* ap = anc->node->params;
                                        while (ap) {
                                            jm_collect_pattern_names(ap, anc_params);
                                            ap = ap->next;
                                        }
                                        if (jm_name_set_has(anc_params, cap_name)) {
                                            shadowed_by_ancestor = true;
                                        }
                                        hashmap_free(anc_params);
                                    }
                                    if (shadowed_by_ancestor) break;
                                    // Check body locals
                                    if (anc->node->body) {
                                        struct hashmap* anc_locals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                                            jm_name_hash, jm_name_cmp, NULL, NULL);
                                        jm_collect_body_locals(anc->node->body, anc_locals);
                                        if (jm_name_set_has(anc_locals, cap_name)) {
                                            bool is_iife_promoted_module_var =
                                                jm_modvar_is_iife_scope_binding(mc_prop) && anc->is_iife_body;
                                            if (!is_iife_promoted_module_var) {
                                                shadowed_by_ancestor = true;
                                            }
                                        }
                                        hashmap_free(anc_locals);
                                    }
                                    if (shadowed_by_ancestor) break;
                                }
                                if (!shadowed_by_ancestor) {
                                    // No ancestor shadows this module_const — safe to remove
                                    // the capture. The identifier will be resolved at the use
                                    // site via module_consts (MCONST_CLASS → js_get_module_var,
                                    // MCONST_FUNC → js_new_function, etc.)
                                    if (ci < child->capture_count - 1) {
                                        memmove(&child->captures[ci], &child->captures[ci + 1],
                                            (child->capture_count - ci - 1) * sizeof(child->captures[0]));
                                    }
                                    child->capture_count--;
                                    ci--;
                                    continue;
                                }
                                // Shadowed — keep the capture and propagate to parent
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
                            const char* parent_self_name = jm_format_name("_js_%.*s",
                                (int)parent->node->name->len, parent->node->name->chars);
                            cap_is_parent_nfe = (strcmp(cap_name, parent_self_name) == 0);
                        }

                        // Add as capture to parent
                        jm_ensure_captures_capacity(parent);
                        parent->captures[parent->capture_count].name = jm_persist_name(cap_name);
                        parent->captures[parent->capture_count].scope_env_key = jm_persist_name(
                            child->captures[ci].scope_env_key &&
                            child->captures[ci].scope_env_key &&
                            child->captures[ci].scope_env_key[0]
                                ? child->captures[ci].scope_env_key : cap_name);
                        parent->captures[parent->capture_count].scope_env_slot = -1;
                        parent->captures[parent->capture_count].private_env_slot = -1;
                        parent->captures[parent->capture_count].grandparent_slot = -1;
                        parent->captures[parent->capture_count].parent_env_link_slot_override = -1;
                        parent->captures[parent->capture_count].entry = child->captures[ci].entry;
                        parent->captures[parent->capture_count].is_let_const = child->captures[ci].is_let_const;
                        parent->captures[parent->capture_count].is_const = child->captures[ci].is_const;
                        // A child closure can reference its enclosing named function
                        // expression's private name. Preserve that as an NFE binding
                        // so creation patches a private env slot instead of falling
                        // through to an outer same-named var.
                        parent->captures[parent->capture_count].is_nfe_binding =
                            child->captures[ci].is_nfe_binding || cap_is_parent_nfe;
                        parent->captures[parent->capture_count].force_env_capture = child->captures[ci].force_env_capture;
                        parent->capture_count++;
                        changed = true;
                        log_debug("js-mir: propagated capture '%s' [%s] from '%s' to parent '%s'",
                            cap_name, parent->captures[parent->capture_count - 1].scope_env_key,
                            child->name, parent->name);
                    }
                    hashmap_free(parent_own);
                }
            }
            if (propagation_rounds > 1) {
                log_debug("js-mir: capture propagation completed in %d rounds", propagation_rounds);
            }
        }

        hashmap_free(all_names);
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

            // Build set of function declaration names in parent's body.
            // Function declarations are hoisted and assigned by the parent — their
            // self-captures should stay in the normal pool (parent manages the slot).
            struct hashmap* parent_func_decls = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            JsFunctionNode* parent_fn = parent_fc->node;
            if (parent_fn && parent_fn->body) {
                struct hashmap* body_locals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                    jm_name_hash, jm_name_cmp, NULL, NULL);
                jm_collect_body_locals(parent_fn->body, body_locals);
                size_t bl_iter = 0;
                void* bl_item;
                while (hashmap_iter(body_locals, &bl_iter, &bl_item)) {
                    JsNameSetEntry* e = (JsNameSetEntry*)bl_item;
                    if (e->from_func_decl) {
                        jm_name_set_add(parent_func_decls, e->name);
                    }
                }
                hashmap_free(body_locals);
            }

            // Collect union of all captures from direct children,
            // EXCLUDING true NFE self-captures (those get dedicated extra slots).
            // Function declaration self-captures are kept in the normal pool.
            struct hashmap* scope_vars = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);

            int nfe_extra_count = 0;
            for (int ci = 0; ci < mt->func_count; ci++) {
                JsFuncCollected* child = &mt->func_entries[ci];
                if (child->parent_index != fi) continue;
                if (child->capture_count == 0) continue;
                if (!jm_child_can_use_parent_scope_env(parent_fc, child)) continue;

                // Determine child's NFE self-name (if any)
                const char* child_self_name = child->node && child->node->name
                    ? jm_format_name("_js_%.*s", (int)child->node->name->len,
                        child->node->name->chars) : NULL;

                bool is_child_nfe = (child->node && child->node->node_type == JS_AST_NODE_FUNCTION_EXPRESSION);
                bool has_nfe_self_capture = false;
                for (int k = 0; k < child->capture_count; k++) {
                    const char* cname = child->captures[k].name;
                    const char* slot_key = jm_capture_scope_env_slot_key(parent_fc, child, &child->captures[k]);
                    // Skip true NFE self-captures (child is a function expression, not declaration).
                    // Name alone is not enough: minified bundles often have an
                    // outer binding and an NFE self binding with the same name.
                    if (child_self_name && child_self_name[0] && strcmp(cname, child_self_name) == 0
                        && is_child_nfe && child->captures[k].is_nfe_binding) {
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

                // Re-iterate children in original order to fill names deterministically
                int fill_idx = 0;
                if (base_count > 0) {
                    hashmap_clear(scope_vars, false);
                    for (int ci = 0; ci < mt->func_count; ci++) {
                        JsFuncCollected* child = &mt->func_entries[ci];
                        if (child->parent_index != fi) continue;
                        if (child->capture_count == 0) continue;
                        if (!jm_child_can_use_parent_scope_env(parent_fc, child)) continue;

                        const char* child_self_name2 = child->node && child->node->name
                            ? jm_format_name("_js_%.*s", (int)child->node->name->len,
                                child->node->name->chars) : NULL;

                        bool is_child_nfe2 = (child->node && child->node->node_type == JS_AST_NODE_FUNCTION_EXPRESSION);
                        for (int k = 0; k < child->capture_count; k++) {
                            const char* cname = child->captures[k].name;
                            const char* slot_key = jm_capture_scope_env_slot_key(parent_fc, child, &child->captures[k]);
                            // Same skip as first pass: true NFE self-captures only.
                            if (child_self_name2 && child_self_name2[0] && strcmp(cname, child_self_name2) == 0
                                && is_child_nfe2 && child->captures[k].is_nfe_binding) {
                                continue;
                            }
                            if (!jm_name_set_has(scope_vars, slot_key)) {
                                jm_name_set_add(scope_vars, slot_key);
                                parent_fc->scope_env_names[fill_idx] = jm_persist_name(slot_key);
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
                    if (child->parent_index != fi) continue;
                    if (!jm_child_can_use_parent_scope_env(parent_fc, child)) continue;
                    if (!child->node || !child->node->name) continue;
                    const char* csn = jm_format_name("_js_%.*s",
                        (int)child->node->name->len, child->node->name->chars);
                    // Only true NFEs (not function declarations) get extra slots
                    if (child->node->node_type != JS_AST_NODE_FUNCTION_EXPRESSION) continue;
                    bool assigned_nfe_slot = false;
                    for (int k = 0; k < child->capture_count; k++) {
                        if (strcmp(child->captures[k].name, csn) == 0 &&
                            child->captures[k].is_nfe_binding) {
                            child->captures[k].scope_env_slot = extra_slot;
                            assigned_nfe_slot = true;
                        }
                    }
                    if (assigned_nfe_slot) {
                        parent_fc->scope_env_names[extra_slot] = jm_persist_name(csn);
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
                        if (child->parent_index != fi) continue;
                        if (child->capture_count == 0) continue;
                        if (!jm_child_can_use_parent_scope_env(parent_fc, child)) continue;

                        // Build child's NFE self-name to skip during remap
                        const char* child_self_remap = child->node && child->node->name
                            ? jm_format_name("_js_%.*s", (int)child->node->name->len,
                                child->node->name->chars) : NULL;

                        bool is_child_nfe_remap = (child->node && child->node->node_type == JS_AST_NODE_FUNCTION_EXPRESSION);
                        for (int k = 0; k < child->capture_count; k++) {
                            // Skip true NFE self-captures — already assigned dedicated slots
                            if (child_self_remap && child_self_remap[0] &&
                                strcmp(child->captures[k].name, child_self_remap) == 0 &&
                                is_child_nfe_remap && child->captures[k].is_nfe_binding) {
                                continue;
                            }
                            // Find this capture's slot in the normal portion of scope env
                            const char* slot_key = jm_capture_scope_env_slot_key(parent_fc, child, &child->captures[k]);
                            for (int s = 0; s < normal_slot_count; s++) {
                                if (strcmp(slot_key, parent_fc->scope_env_names[s]) == 0) {
                                    child->captures[k].scope_env_slot = s;
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            hashmap_free(parent_func_decls);
            hashmap_free(scope_vars);
        }
    }

    for (int ci = 0; ci < mt->func_count; ci++) {
        JsFuncCollected* child = &mt->func_entries[ci];
        int parent_index = child->parent_index;
        if (parent_index < 0 || parent_index >= mt->func_count) continue;
        JsFuncCollected* parent_fc = &mt->func_entries[parent_index];
        if (!parent_fc->has_scope_env || parent_fc->scope_env_count <= 0) continue;
        jm_mark_mixed_loop_parent_link(child, parent_fc);
    }

    // Phase 1.7.5: Js57 Track A — module-level scope env.
    // Top-level closures (parent_index == -1) can share captured block-lets via a
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
    mt->module_scope_env_active = false;
    {
        struct hashmap* for_init_lets = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        jm_collect_for_init_lexical_names((JsAstNode*)program, for_init_lets, /*in_loop=*/false);
        struct hashmap* module_block_lexicals_seen = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        struct hashmap* duplicate_module_block_const_lexicals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        jm_collect_duplicate_module_block_lexicals((JsAstNode*)program,
            module_block_lexicals_seen, duplicate_module_block_const_lexicals, true);

        struct hashmap* scope_vars = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);

        // P7a: helper that decides whether a capture name qualifies for a
        // module-level slot. Loop bindings use those slots only inside copied
        // closure envs so they keep per-iteration semantics.
        auto capture_is_module_shadow_lexical = [&](JsFuncCollected* child,
                FnCapture* cap) -> bool {
            if (!child || !cap) return false;
            if (!jm_name_set_has(duplicate_module_block_const_lexicals, cap->name)) return false;
            const char* derived_key = NULL;
            JsAstNode* target = child->node ? (JsAstNode*)child->node : NULL;
            bool found_key = jm_find_enclosing_lexical_key_for_target((JsAstNode*)program,
                target, cap->name, &derived_key);
            if (!found_key || strcmp(derived_key, cap->name) == 0) return false;
            // A block lexical that shadows a top-level module var must use the
            // scope-env cell, otherwise the function body resolves the module var.
            cap->scope_env_key = derived_key;
            return true;
        };

        auto capture_qualifies = [&](JsFuncCollected* child, FnCapture* cap) -> bool {
            if (!cap) return false;
            const char* name = cap->name;
            if (!cap->is_let_const) return false;
            if (cap->is_nfe_binding) return false;
            bool module_shadow_lexical = capture_is_module_shadow_lexical(child, cap);
            if (mt->module_consts) {
                JsModuleConstEntry mclookup;
                memset(&mclookup, 0, sizeof(mclookup));
                mclookup.name = jm_persist_name(name);
                JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                if (mc && mc->const_type == MCONST_MODVAR && !module_shadow_lexical) return false;
            }
            if (strcmp(name, "_js_this") == 0 ||
                strcmp(name, "_js_new.target") == 0 ||
                strcmp(name, "_js_arguments") == 0) return false;
            return true;
        };

        auto capture_slot_key = [&](JsFuncCollected* child, FnCapture* cap) -> const char* {
            if (!cap) return "";
            if (cap->scope_env_key && cap->scope_env_key[0] &&
                    strcmp(cap->scope_env_key, cap->name) != 0) {
                JsModuleConstEntry mclookup;
                memset(&mclookup, 0, sizeof(mclookup));
                mclookup.name = jm_persist_name(cap->name);
                JsModuleConstEntry* mc = mt->module_consts ?
                    (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup) : NULL;
                if (mc && mc->const_type == MCONST_MODVAR) return cap->scope_env_key;
            }
            if (jm_name_set_has(duplicate_module_block_const_lexicals, cap->name) &&
                cap->scope_env_key && cap->scope_env_key[0]) {
                const char* derived_key = NULL;
                JsAstNode* target = child && child->node ? (JsAstNode*)child->node : NULL;
                bool found_key = jm_find_enclosing_lexical_key_for_target((JsAstNode*)program,
                    target, cap->name, &derived_key);
                if (!found_key && target) {
                    for (int ci = 0; ci < mt->class_count; ci++) {
                        JsClassEntry* ce = &mt->class_entries[ci];
                        if (!ce->node || !jm_ast_node_contains_target((JsAstNode*)ce->node, target)) continue;
                        found_key = jm_find_enclosing_lexical_key_for_target((JsAstNode*)program,
                            (JsAstNode*)ce->node, cap->name, &derived_key);
                        if (found_key) break;
                    }
                }
                if (found_key) {
                    cap->scope_env_key = derived_key;
                }
                return cap->scope_env_key;
            }
            return cap->name;
        };

        auto closure_qualifies = [&](JsFuncCollected* child) -> bool {
            (void)child;
            return true;
        };

        auto capture_is_shared_module_binding = [&](JsFuncCollected* child, FnCapture* cap) -> bool {
            if (!cap) return false;
            if (!capture_qualifies(child, cap)) {
                return false;
            }
            if (cap->force_env_capture) return false;
            if (jm_name_set_has(for_init_lets, cap->name)) return false;
            return true;
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
            for (int k = 0; k < child->capture_count; k++) {
                FnCapture* cap = &child->captures[k];
                if (capture_is_shared_module_binding(child, cap)) {
                    has_shared = true;
                } else if (capture_needs_private_module_slot(child, cap)) {
                    has_private = true;
                }
            }
            return has_private && has_shared;
        };

        auto include_capture = [&](JsFuncCollected* child, int k) -> bool {
            FnCapture* cap = &child->captures[k];
            return capture_qualifies(child, cap);
        };

        for (int ci = 0; ci < mt->func_count; ci++) {
            JsFuncCollected* child = &mt->func_entries[ci];
            if (child->parent_index != -1) continue;
            if (child->capture_count == 0) continue;
                if (!closure_qualifies(child)) continue;
                for (int k = 0; k < child->capture_count; k++) {
                    if (!include_capture(child, k)) continue;
                    jm_name_set_add(scope_vars, capture_slot_key(child, &child->captures[k]));
                }
            }

        int total = (int)hashmap_count(scope_vars);
        if (total > 0) {
            int scope_env_capacity = total + 2;
            for (int ci = 0; ci < mt->func_count; ci++) {
                JsFuncCollected* child = &mt->func_entries[ci];
                if (child->parent_index != -1 || !closure_qualifies(child) ||
                    !closure_needs_mixed_module_env(child)) continue;
                int private_count = 0;
                for (int k = 0; k < child->capture_count; k++) {
                    if (capture_needs_private_module_slot(child, &child->captures[k])) {
                        private_count++;
                    }
                }
                int required = total + private_count + 1;
                if (child->capture_count + 1 > required) required = child->capture_count + 1;
                if (required > scope_env_capacity) scope_env_capacity = required;
            }
            mt->module_fc.has_scope_env = true;
            mt->module_fc.scope_env_count = total;
            mt->module_fc.scope_env_normal_count = total;
            mt->module_fc.parent_index = -2;  // sentinel: module body
            mt->module_fc.scope_env_names = (const char**)mem_calloc(
                scope_env_capacity, sizeof(const char*), MEM_CAT_JS_RUNTIME);

            // Deterministic fill: iterate children in collection order
            hashmap_clear(scope_vars, false);
            int fill_idx = 0;
            for (int ci = 0; ci < mt->func_count; ci++) {
                JsFuncCollected* child = &mt->func_entries[ci];
                if (child->parent_index != -1) continue;
                if (!closure_qualifies(child)) continue;
                for (int k = 0; k < child->capture_count; k++) {
                    if (!include_capture(child, k)) continue;
                    const char* key = capture_slot_key(child, &child->captures[k]);
                    if (!jm_name_set_has(scope_vars, key)) {
                        jm_name_set_add(scope_vars, key);
                        mt->module_fc.scope_env_names[fill_idx] = jm_persist_name(key);
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
                if (child->parent_index != -1) continue;
                if (!closure_qualifies(child)) continue;
                for (int k = 0; k < child->capture_count; k++) {
                    if (!include_capture(child, k)) continue;
                    const char* key = capture_slot_key(child, &child->captures[k]);
                    for (int s = 0; s < total; s++) {
                        if (strcmp(key, mt->module_fc.scope_env_names[s]) == 0) {
                            child->captures[k].scope_env_slot = s;
                            break;
                        }
                    }
                }
                if (closure_needs_mixed_module_env(child)) {
                    // A closure that also has private captures cannot reuse the
                    // module env directly; link its copied env back so shared
                    // lexical cells remain live instead of freezing their TDZ value.
                    int private_slot = total;
                    for (int k = 0; k < child->capture_count; k++) {
                        FnCapture* cap = &child->captures[k];
                        if (capture_needs_private_module_slot(child, cap)) {
                            cap->private_env_slot = private_slot++;
                        }
                    }
                    child->closure_env_has_parent_link = true;
                    child->closure_env_parent_link_slot =
                        jm_parent_link_slot_after_captures(child, total);
                    if (child->closure_env_parent_link_slot >= mt->module_fc.scope_env_count) {
                        // Generated transitive loads address the parent-link tail
                        // by layout, so reserve that tail in the module env too.
                        mt->module_fc.has_parent_env_link = true;
                        mt->module_fc.scope_env_count =
                            child->closure_env_parent_link_slot + 1;
                        mt->module_fc.scope_env_names[
                            child->closure_env_parent_link_slot] = jm_persist_name("__parent_env__");
                    }
                    for (int k = 0; k < child->capture_count; k++) {
                        FnCapture* cap = &child->captures[k];
                        if (capture_is_shared_module_binding(child, cap) && cap->scope_env_slot >= 0) {
                            // copied envs keep loop lets private; outer module
                            // lets must still mutate the shared parent slot.
                            cap->grandparent_slot = cap->scope_env_slot;
                        }
                    }
                    log_debug("js-mir: module mixed closure '%s' uses parent env slot %d",
                        child->name, child->closure_env_parent_link_slot);
                }
            }

            log_debug("js-mir: Phase 1.7.5: module scope env with %d slots", total);
            for (int s = 0; s < total; s++) {
                log_debug("js-mir:   module_scope_env[%d] = '%s'", s, mt->module_fc.scope_env_names[s]);
            }
        }

        hashmap_free(scope_vars);
        hashmap_free(module_block_lexicals_seen);
        hashmap_free(duplicate_module_block_const_lexicals);
        hashmap_free(for_init_lets);
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
        parent_fc->reuse_parent_env = false;
        parent_fc->reuse_env_slot_count = 0;
        if (!parent_fc->has_scope_env || parent_fc->scope_env_count == 0) continue;
        if (parent_fc->capture_count == 0) continue;  // not a closure, can't reuse

        // Check if ALL scope_env vars are also in this function's own captures
        bool all_transitive = true;
        for (int s = 0; s < parent_fc->scope_env_count; s++) {
            bool found_in_captures = false;
            for (int c = 0; c < parent_fc->capture_count; c++) {
                if (strcmp(parent_fc->scope_env_names[s], parent_fc->captures[c].name) == 0) {
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
        parent_fc->reuse_parent_env = true;
        int max_slot = 0;
        for (int s = 0; s < parent_fc->scope_env_count; s++) {
            const char* sname = parent_fc->scope_env_names[s];
            // Find this scope_env var in parent_fc's own captures to get grandparent slot
            for (int c = 0; c < parent_fc->capture_count; c++) {
                if (strcmp(sname, parent_fc->captures[c].name) == 0) {
                    // Propagated captures in a mixed module closure can live in
                    // private tail slots. Its nested closures reuse that actual
                    // incoming layout, not the original module scope slot.
                    int grandparent_slot = jm_capture_env_slot(
                        &parent_fc->captures[c], c);
                    if (grandparent_slot < 0) {
                        // Can't remap — grandparent doesn't use scope_env for this var
                        parent_fc->reuse_parent_env = false;
                        break;
                    }
                    if (grandparent_slot + 1 > max_slot) max_slot = grandparent_slot + 1;

                    // Remap all children's captures of this var
                    for (int ci = 0; ci < mt->func_count; ci++) {
                        JsFuncCollected* child = &mt->func_entries[ci];
                        if (child->parent_index != fi) continue;
                        for (int k = 0; k < child->capture_count; k++) {
                            if (strcmp(child->captures[k].name, sname) == 0) {
                                child->captures[k].scope_env_slot = grandparent_slot;
                                // Mixed loop closures record this same binding as
                                // grandparent_slot; leaving its pre-reuse slot here
                                // makes nested arrows read an unrelated parent value.
                                if (child->captures[k].grandparent_slot >= 0) {
                                    child->captures[k].grandparent_slot = grandparent_slot;
                                }
                                break;
                            }
                        }
                    }
                    break;
                }
            }
            if (!parent_fc->reuse_parent_env) break;  // aborted
        }

        if (parent_fc->reuse_parent_env) {
            parent_fc->reuse_env_slot_count = max_slot;
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
        parent_fc->parent_env_link_uses_grandparent = false;
        parent_fc->has_immediate_parent_env_link = false;
        parent_fc->immediate_parent_env_link_slot = -1;
        if (!parent_fc->has_scope_env || parent_fc->scope_env_count == 0) continue;
        if (parent_fc->reuse_parent_env) continue;  // Phase 1.7b already handles pure-transitive
        if (parent_fc->capture_count == 0) continue; // no captures = no transitive vars possible

        // Collect body locals for this function to distinguish locals from transitive captures
        struct hashmap* parent_locals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        JsFunctionNode* parent_fn = parent_fc->node;
        if (parent_fn && parent_fn->body) {
            // parent captures can include names propagated from child closures;
            // body locals still belong to this activation, not the parent link.
            jm_collect_body_locals(parent_fn->body, parent_locals, false);
            // Also add parameters as locals
            JsAstNode* pp = parent_fn->params;
            while (pp) {
                const char* pname = jm_get_param_name(pp, 0);
                if (pname && pname[0]) {
                    JsNameSetEntry pentry;
                    pentry.name = jm_format_name("_js_%s", pname);
                    hashmap_set(parent_locals, &pentry);
                }
                pp = pp->next;
            }
            jm_collect_function_private_self_name(parent_fn, parent_locals);
        }

        // Check if scope env has any transitive captures (vars that are also in parent_fc's captures)
        // Only count captures that the parent reads from its own parent's scope env
        // (scope_env_slot >= 0), NOT module vars read via js_get_module_var.
        // Also exclude vars that are LOCAL to the parent (shadowing the capture).
        bool has_transitive = false;
        bool has_local = false;
        bool parent_link_uses_grandparent = false;
        for (int s = 0; s < parent_fc->scope_env_count; s++) {
            bool is_capture = false;
            // Check if this scope env var is a local of the parent (including function declarations)
            JsNameSetEntry local_lookup;
            local_lookup.name = jm_persist_name(parent_fc->scope_env_names[s]);
            bool is_parent_local = (hashmap_get(parent_locals, &local_lookup) != NULL);
            if (!is_parent_local) {
                for (int c = 0; c < parent_fc->capture_count; c++) {
                    if (strcmp(parent_fc->scope_env_names[s], parent_fc->captures[c].name) == 0) {
                        // any parent capture is transitive for the child; if it is
                        // not backed by a shared scope_env slot, the parent's dense
                        // closure-env capture slot is still the live binding cell.
                        is_capture = true;
                        if (parent_fc->captures[c].grandparent_slot >= 0) {
                            parent_link_uses_grandparent = true;
                        }
                        break;
                    }
                }
            }
            if (is_capture) has_transitive = true;
            else has_local = true;
        }

        hashmap_free(parent_locals);

        if (!has_transitive) continue; // pure-local scope envs do not need a parent link
        // a non-reused env with a transitive capture must preserve the original
        // parent binding cell; local-name collection can miss mixed callback
        // shapes, and copying the transitive value makes sibling closures mutate
        // independent cells (e.g. captured --waiting in nested event callbacks).
        (void)has_local;

        bool needs_immediate_parent_link = false;
        if (parent_link_uses_grandparent) {
            for (int ci = 0; ci < mt->func_count && !needs_immediate_parent_link; ci++) {
                JsFuncCollected* child = &mt->func_entries[ci];
                if (child->parent_index != fi) continue;
                for (int k = 0; k < child->capture_count && !needs_immediate_parent_link; k++) {
                    for (int pc = 0; pc < parent_fc->capture_count; pc++) {
                        FnCapture* parent_cap = &parent_fc->captures[pc];
                        if (strcmp(child->captures[k].name, parent_cap->name) != 0 ||
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
        parent_fc->parent_env_link_uses_grandparent = parent_link_uses_grandparent;
        int immediate_parent_env_link_slot = -1;
        if (needs_immediate_parent_link) {
            parent_fc->has_immediate_parent_env_link = true;
            immediate_parent_env_link_slot = parent_fc->scope_env_count;
            parent_fc->immediate_parent_env_link_slot = immediate_parent_env_link_slot;
            parent_fc->scope_env_names[parent_fc->scope_env_count] =
                jm_persist_name("__immediate_parent_env__");
            parent_fc->scope_env_count++;
        }
        int parent_env_link_slot = parent_fc->scope_env_count; // last slot = parent env pointer
        // scope_env_names was allocated with +2 extra slots for this
        parent_fc->scope_env_names[parent_fc->scope_env_count] =
            jm_persist_name("__parent_env__");
        parent_fc->scope_env_count++;

        // Re-collect locals for grandparent_slot assignment (reuse same logic)
        struct hashmap* parent_locals2 = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        if (parent_fn && parent_fn->body) {
            // parent-local cells must stay in the direct env even when the
            // function also carries transitive captures from its own parent.
            jm_collect_body_locals(parent_fn->body, parent_locals2, false);
            JsAstNode* pp2 = parent_fn->params;
            while (pp2) {
                const char* pname2 = jm_get_param_name(pp2, 0);
                if (pname2[0]) {
                    JsNameSetEntry pe2;
                    pe2.name = jm_format_name("_js_%s", pname2);
                    hashmap_set(parent_locals2, &pe2);
                }
                pp2 = pp2->next;
            }
            jm_collect_function_private_self_name(parent_fn, parent_locals2);
        }

        // For transitive captures in direct children, set grandparent_slot
        // NO slot shifting needed — existing slots remain unchanged
        for (int ci = 0; ci < mt->func_count; ci++) {
            JsFuncCollected* child = &mt->func_entries[ci];
            if (child->parent_index != fi) continue;
            if (child->capture_count == 0) continue;

            for (int k = 0; k < child->capture_count; k++) {
                bool field_initializer_arrow = false;
                if (child->node && child->node->is_arrow &&
                    !ts_node_is_null(child->node->node)) {
                    for (TSNode enclosing = ts_node_parent(child->node->node);
                         !ts_node_is_null(enclosing);
                         enclosing = ts_node_parent(enclosing)) {
                        const char* enclosing_type = ts_node_type(enclosing);
                        if (!enclosing_type) break;
                        if (strcmp(enclosing_type, "field_definition") == 0 ||
                            strcmp(enclosing_type, "public_field_definition") == 0) {
                            field_initializer_arrow = true;
                            break;
                        }
                        if (strcmp(enclosing_type, "function_declaration") == 0 ||
                            strcmp(enclosing_type, "function") == 0 ||
                            strcmp(enclosing_type, "method_definition") == 0) {
                            break;
                        }
                    }
                }
                if (field_initializer_arrow &&
                    jm_capture_is_lexical_meta_binding(child->captures[k].name)) {
                    // field initializer arrows own a snapshot of lexical this;
                    // following the enclosing closure's parent link loses the instance.
                    child->captures[k].grandparent_slot = -1;
                    child->captures[k].parent_env_link_slot_override = -1;
                    continue;
                }
                // Check if this capture name is a LOCAL of the parent — if so, skip
                JsNameSetEntry ll;
                ll.name = jm_persist_name(child->captures[k].name);
                if (hashmap_get(parent_locals2, &ll)) continue;

                // Check if this capture is a transitive capture (also in parent_fc's captures)
                // Only for captures the parent reads from its own parent's scope env
                for (int pc = 0; pc < parent_fc->capture_count; pc++) {
                    if (strcmp(child->captures[k].name, parent_fc->captures[pc].name) == 0) {
                        int grandparent_slot = parent_fc->captures[pc].grandparent_slot;
                        if (grandparent_slot >= 0) {
                            child->captures[k].grandparent_slot = grandparent_slot;
                        } else if (parent_fc->captures[pc].scope_env_slot >= 0) {
                            if (!parent_link_uses_grandparent) {
                                // the parent link names the immediate parent env here;
                                // reusing scope_env_slot would collide with the child's
                                // own scope-env layout in mixed callback closures.
                                child->captures[k].grandparent_slot = parent_fc->captures[pc].scope_env_slot;
                            } else {
                                if (immediate_parent_env_link_slot >= 0) {
                                    // The default link skips to the grandparent, but this
                                    // capture is owned by the immediate parent. Preserve
                                    // its late-initialized cell through the direct link.
                                    child->captures[k].grandparent_slot =
                                        parent_fc->captures[pc].scope_env_slot;
                                    child->captures[k].parent_env_link_slot_override =
                                        immediate_parent_env_link_slot;
                                } else {
                                    child->captures[k].scope_env_slot = -1;
                                    child->captures[k].grandparent_slot = -1;
                                    break;
                                }
                            }
                        } else if (!parent_link_uses_grandparent) {
                            // parent closures that do not use a shared scope env
                            // store captures densely by capture index.
                            child->captures[k].grandparent_slot = pc;
                        } else {
                            break;
                        }
                        log_debug("js-mir: Phase 1.7c: capture '%s' in '%s' → grandparent slot %d (parent env at slot %d)",
                            child->captures[k].name, child->name, child->captures[k].grandparent_slot, parent_env_link_slot);
                        break;
                    }
                }
            }
        }

        hashmap_free(parent_locals2);

        log_debug("js-mir: Phase 1.7c: '%s' has parent env link at slot %d (mixed scope env, %d slots)",
            parent_fc->name, parent_env_link_slot, parent_fc->scope_env_count);
    }

    // Phase 1.7d: A function cannot reuse a direct parent's mixed scope env.
    // Mixed scope envs contain local slots plus a parent-env link. Reusing that
    // env as if it were the grandparent env makes grandchildren read stale or
    // unrelated slots for later-initialized lexical captures.
    for (int fi = 0; fi < mt->func_count; fi++) {
        JsFuncCollected* fc = &mt->func_entries[fi];
        if (!fc->reuse_parent_env) continue;
        int parent_index = fc->parent_index;
        if (parent_index < 0 || parent_index >= mt->func_count) continue;
        JsFuncCollected* parent_fc = &mt->func_entries[parent_index];
        if (!parent_fc->has_parent_env_link) continue;

        fc->reuse_parent_env = false;
        fc->reuse_env_slot_count = 0;
        fc->has_parent_env_link = true;
        fc->parent_env_link_uses_grandparent = true;
        // A mixed parent needs two distinct links: the inherited link preserves
        // transitive captures, while this direct link keeps parent-local cells
        // shared with sibling closures instead of copying stale values.
        fc->has_immediate_parent_env_link = true;
        fc->immediate_parent_env_link_slot = fc->scope_env_count;
        fc->scope_env_names[fc->scope_env_count] = jm_persist_name(
            "__immediate_parent_env__");
        fc->scope_env_count++;
        int parent_env_link_slot = fc->scope_env_count;
        fc->scope_env_names[fc->scope_env_count] = jm_persist_name("__parent_env__");
        fc->scope_env_count++;
        for (int ci = 0; ci < mt->func_count; ci++) {
            JsFuncCollected* child = &mt->func_entries[ci];
            if (child->parent_index != fi) continue;
            for (int k = 0; k < child->capture_count; k++) {
                for (int s = 0; s < fc->scope_env_count; s++) {
                    if (strcmp(child->captures[k].name, fc->scope_env_names[s]) == 0) {
                        child->captures[k].scope_env_slot = s;
                        child->captures[k].grandparent_slot = -1;
                        child->captures[k].parent_env_link_slot_override = -1;
                        for (int c = 0; c < fc->capture_count; c++) {
                            if (strcmp(child->captures[k].name, fc->captures[c].name) != 0) continue;
                            if (fc->captures[c].grandparent_slot >= 0) {
                                child->captures[k].grandparent_slot =
                                    fc->captures[c].grandparent_slot;
                            } else if (fc->captures[c].scope_env_slot >= 0) {
                                // This binding belongs to the immediate mixed
                                // parent. A copied compact slot would freeze its
                                // pre-assignment value and split sibling closures.
                                child->captures[k].grandparent_slot =
                                    fc->captures[c].scope_env_slot;
                                child->captures[k].parent_env_link_slot_override =
                                    fc->immediate_parent_env_link_slot;
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
            if (child->has_scope_env && child->scope_env_names) {
                // A child that still reuses its parent has no independent cell
                // layout: its children must follow this newly owned environment.
                // Remapping them through the child's pre-reuse layout aliases
                // unrelated siblings when the slot orders differ.
                JsFuncCollected* capture_env_owner = child->reuse_parent_env ? fc : child;
                for (int gi = 0; gi < mt->func_count; gi++) {
                    JsFuncCollected* grandchild = &mt->func_entries[gi];
                    if (grandchild->parent_index != ci) continue;
                    for (int gk = 0; gk < grandchild->capture_count; gk++) {
                        for (int s = 0; s < capture_env_owner->scope_env_count; s++) {
                            if (strcmp(grandchild->captures[gk].name,
                                    capture_env_owner->scope_env_names[s]) == 0) {
                                grandchild->captures[gk].scope_env_slot = s;
                                grandchild->captures[gk].grandparent_slot = -1;
                                grandchild->captures[gk].parent_env_link_slot_override = -1;
                                if (capture_env_owner == child) {
                                    for (int c = 0; c < child->capture_count; c++) {
                                        if (strcmp(grandchild->captures[gk].name,
                                                child->captures[c].name) == 0 &&
                                            child->captures[c].scope_env_slot >= 0 &&
                                            child->captures[c].grandparent_slot < 0) {
                                            // A capture owned by the mixed direct
                                            // parent must not become a stale copy
                                            // in this compact env (Splide's sibling
                                            // transition callback assignment).
                                            grandchild->captures[gk].grandparent_slot =
                                                child->captures[c].scope_env_slot;
                                            grandchild->captures[gk].parent_env_link_slot_override =
                                                child->immediate_parent_env_link_slot;
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
        jm_infer_param_types(fc);
        jm_infer_return_type(fc);
        // P6: If return type is still ANY but some params are typed, try deeper
        // local variable tracing to resolve the return type.
        if (fc->return_type == LMD_TYPE_ANY) {
            bool has_typed_param = false;
            for (int j = 0; j < fc->param_count; j++) {
                if (jm_param_type(fc, j) == LMD_TYPE_INT || jm_param_type(fc, j) == LMD_TYPE_FLOAT) {
                    has_typed_param = true; break;
                }
            }
            if (has_typed_param) jm_p6_reinfer_return_type(fc);
        }
        // P1: Compute native eligibility here (Phase 1.75) rather than lazily in jm_define_function.
        // This allows jm_resolve_native_call() (which checks has_native_version) to see the flag
        // when transpiling earlier functions that call later-defined native functions, enabling
        // `let x = f(...)` to propagate f's return type into x's variable type.
        // Native specialization cannot use duplicate MIR param names, and arrow
        // block bodies still need boxed statement-completion return handling.
        bool eligible = (fc->capture_count == 0 && fc->param_count > 0 &&
                         !fc->uses_arguments &&
                         jm_p6_function_allows_native_specialization(fc) &&
                         !fc->has_non_simple_params &&
                         (fc->return_type == LMD_TYPE_INT || fc->return_type == LMD_TYPE_FLOAT));
        bool has_native_param = false;
        if (eligible) {
            for (int j = 0; j < fc->param_count; j++) {
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
        fc->has_native_version = eligible;
        fc->native_return_kind = !eligible ? NATIVE_RETURN_NONE :
            fc->return_type == LMD_TYPE_FLOAT ? NATIVE_RETURN_FLOAT : NATIVE_RETURN_INT;
        if (eligible) {
            log_debug("js-mir P1/P4: %s eligible for native version (params: %d, ret: %s)",
                fc->name, fc->param_count,
                fc->return_type == LMD_TYPE_INT ? "INT" : "FLOAT");
        }

        // Mixed native/Item entries keep the Item formal stable across every
        // tail iteration. Retain TCO for all-native signatures only.
        fc->is_tco_eligible = false;
        if (eligible && has_native_param) {
            for (int j = 0; j < fc->param_count; j++) {
                if (jm_param_type(fc, j) == LMD_TYPE_ANY) {
                    has_native_param = false;
                    break;
                }
            }
        }
        if (eligible && has_native_param && jm_has_tail_call(fc->node->body, fc)) {
            fc->is_tco_eligible = true;
            log_debug("js-mir TCO: %s eligible for tail-call optimization", fc->name);
        }
    }

    // Phase 1.9: Create forward declarations for all functions.
    // This ensures func_item is set for all functions before any body is compiled,
    // so forward references (e.g., a class method calling a free function declared
    // later in the source) resolve correctly via MCONST_FUNC and direct call paths.

    // Phase 1.76: Call-site propagation — scan all function bodies for call
    // expressions that pass literal arguments contradicting inferred param types.
    // Widen mismatched params to ANY and revoke native eligibility.
    jm_callsite_propagate(mt, program->body);

    // Phase 1.77: P6 call-site narrowing — for params still ANY after body-scan,
    // narrow to INT/FLOAT when ALL call sites pass compatible types.
    if (mt->func_count > 0) {
        // allocate evidence rows to each function's actual formal count;
        // the old 16-column matrix was only a transient optimization cap.
        FnParamEvidence** evi = (FnParamEvidence**)mem_calloc(
            (size_t)mt->func_count, sizeof(*evi), MEM_CAT_JS_RUNTIME);
        if (evi) {
            for (int i = 0; i < mt->func_count; i++) {
                if (mt->func_entries[i].param_count > 0) {
                    evi[i] = (FnParamEvidence*)mem_calloc(
                        (size_t)mt->func_entries[i].param_count,
                        sizeof(FnParamEvidence), MEM_CAT_JS_RUNTIME);
                }
            }
        }
        // Program bodies are linked statement lists; walking only the head
        // misses later top-level calls that seed recursive parameter types.
        for (JsAstNode* top = (JsAstNode*)program->body; top; top = top->next) {
            jm_p6_narrow_walk(mt, top, evi);
        }
        // walk all function bodies
        for (int i = 0; i < mt->func_count; i++) {
            JsFuncCollected* fc = &mt->func_entries[i];
            if (fc->node && fc->node->body)
                jm_p6_narrow_walk(mt, (JsAstNode*)fc->node->body, evi);
        }
        // apply narrowing
        for (int i = 0; i < mt->func_count; i++) {
            JsFuncCollected* fc = &mt->func_entries[i];
            if (fc->node && (fc->node->is_generator || fc->node->is_async)) continue;
            if (fc->has_scope_env) continue; // params may be captured by child closures — don't narrow
            bool narrowed = false;
            for (int p = 0; p < fc->param_count; p++) {
                if (jm_param_type(fc, p) != LMD_TYPE_ANY || !evi || !evi[i]) continue;
                FnParamEvidence* e = &evi[i][p];
                int total = e->int_evidence + e->float_evidence + e->other_evidence;
                if (total == 0) continue; // never called
                if (e->other_evidence > 0) continue; // something non-numeric passed
                if (e->float_evidence > 0 && e->int_evidence == 0) {
                    jm_set_param_type(fc, p, LMD_TYPE_FLOAT);
                    narrowed = true;
                    log_info("P6 narrow %s param[%d] → FLOAT (calls: %d int, %d float, %d other)",
                             fc->name, p, e->int_evidence, e->float_evidence, e->other_evidence);
                } else {
                    // mixed int+float → narrow to FLOAT (int is promotable)
                    jm_set_param_type(fc, p, LMD_TYPE_FLOAT);
                    narrowed = true;
                    log_info("P6 narrow %s param[%d] → FLOAT (mixed: %d int, %d float)",
                             fc->name, p, e->int_evidence, e->float_evidence);
                }
            }
            if (narrowed) {
                // re-infer return type now that params are typed
                jm_p6_reinfer_return_type(fc);
                // recompute native eligibility
                bool eligible = (fc->capture_count == 0 &&
                                 !(fc->node && fc->node->is_generator) &&
                                 !(fc->node && fc->node->is_async) &&
                                 // arguments object setup lives in the boxed prologue;
                                 // P6 must not re-enable native after the first gate vetoed it.
                                 !fc->uses_arguments &&
                                 jm_p6_function_allows_native_specialization(fc) &&
                                 !fc->has_non_simple_params);
                bool has_native_param = false;
                if (eligible) {
                    for (int p = 0; p < fc->param_count; p++) {
                        TypeId pt = jm_param_type(fc, p);
                        if (pt == LMD_TYPE_INT || pt == LMD_TYPE_FLOAT) {
                            has_native_param = true;
                            continue;
                        }
                        if (pt != LMD_TYPE_ANY) {
                            eligible = false; break;
                        }
                    }
                    if (eligible) {
                        TypeId rt = fc->return_type;
                        if (rt != LMD_TYPE_INT && rt != LMD_TYPE_FLOAT)
                            eligible = false;
                    }
                }
                if (!has_native_param) eligible = false;
                if (eligible && !fc->has_native_version) {
                    fc->has_native_version = true;
                    fc->native_return_kind = fc->return_type == LMD_TYPE_FLOAT
                        ? NATIVE_RETURN_FLOAT : NATIVE_RETURN_INT;
                    log_info("P6 enabled native version for %s (return_type=%d)", fc->name, fc->return_type);
                } else if (!eligible) {
                    fc->has_native_version = false;
                    fc->native_func_item = 0;
                    fc->native_return_kind = NATIVE_RETURN_NONE;
                }
                // P6 can make recursive accumulator functions native-eligible;
                // recompute TCO after narrowing so deep tail calls stay loops.
                bool all_native_params = has_native_param;
                for (int p = 0; p < fc->param_count; p++) {
                    if (jm_param_type(fc, p) == LMD_TYPE_ANY) {
                        all_native_params = false;
                        break;
                    }
                }
                fc->is_tco_eligible = eligible && all_native_params &&
                    jm_has_tail_call(fc->node->body, fc);
            }
        }
        if (evi) {
            for (int i = 0; i < mt->func_count; i++) {
                if (evi[i]) mem_free(evi[i]);
            }
            mem_free(evi);
        }
    }

    for (int i = 0; i < mt->func_count; i++) {
        JsFuncCollected* fc = &mt->func_entries[i];
        fc->boxed_return_scalar_class = jm_infer_boxed_return_scalar_class(fc);
        FnAnalysis* analysis = &fc->analysis;
        analysis->variant_count = 0;
        FnVariantAnalysis* public_entry =
            &analysis->variants[analysis->variant_count++];
        memset(public_entry, 0, sizeof(*public_entry));
        public_entry->entry = {FN_ENTRY_PUBLIC_WRAPPER, false,
            fc->has_direct_eval, fc->uses_arguments, true};
        public_entry->effects = {true, true, true, false,
            fc->node->is_async || fc->node->is_generator, true};
        ScalarReturnClass scalar_class = fc->boxed_return_scalar_class;
        public_entry->result.normal = {fc->return_type, VALUE_REP_ITEM,
            scalar_class, scalar_class != SCALAR_RETURN_NONE};
        public_entry->result.scalar_home_lane_mask =
            scalar_class != SCALAR_RETURN_NONE ? FN_RETURN_HOME_NORMAL : 0;
        int env_param_count = fc->capture_count > 0 ? 1 : 0;
        int physical_param_count = fc->param_count + env_param_count;
        public_entry->param_count = physical_param_count + 1;
        if (physical_param_count > 0) {
            public_entry->params = (FnParamAnalysis*)pool_calloc(
                mt->tp->ast_pool, sizeof(FnParamAnalysis) * (size_t)physical_param_count);
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
        body->entry = {FN_ENTRY_BOXED_BODY, true, fc->has_direct_eval,
            fc->uses_arguments, false};
        body->effects = public_entry->effects;
        body->result.normal = {fc->return_type, VALUE_REP_ITEM,
            scalar_class, scalar_class != SCALAR_RETURN_NONE};
        body->result.scalar_home_lane_mask =
            scalar_class != SCALAR_RETURN_NONE ? FN_RETURN_HOME_NORMAL : 0;
        body->param_count = physical_param_count;
        if (physical_param_count > 0) {
            body->params = (FnParamAnalysis*)pool_calloc(
            mt->tp->ast_pool, sizeof(FnParamAnalysis) * (size_t)physical_param_count);
            for (int p = 0; p < physical_param_count; p++) {
                bool env = env_param_count && p == 0;
                TypeId param_type = env ? (TypeId)LMD_TYPE_ANY :
                    jm_param_type(fc, p - env_param_count);
                body->params[p] = {param_type,
                    env ? VALUE_REP_RAW_GC_POINTER : VALUE_REP_ITEM, 0};
            }
        }

        if (fc->has_native_version) {
            FnVariantAnalysis* native =
                &analysis->variants[analysis->variant_count++];
            memset(native, 0, sizeof(*native));
            native->entry = {FN_ENTRY_NATIVE_BODY, true, false, false, false};
            native->effects = body->effects;
            native->result.normal = {fc->return_type,
                fc->native_return_kind == NATIVE_RETURN_FLOAT
                    ? VALUE_REP_F64 : VALUE_REP_I64,
                SCALAR_RETURN_NONE, false};
            native->param_count = fc->param_count;
            if (fc->param_count > 0) {
                native->params = (FnParamAnalysis*)pool_calloc(
                    mt->tp->ast_pool, sizeof(FnParamAnalysis) * (size_t)fc->param_count);
                for (int p = 0; p < fc->param_count; p++) {
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
                SCALAR_RETURN_NONE, false};
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
        if (fc->has_native_version && !fc->native_func_item) {
            char native_fwd_name[140];
            snprintf(native_fwd_name, sizeof(native_fwd_name), "%s_n", fc->name);
            MIR_item_t fwd_native = MIR_new_forward(mt->ctx, native_fwd_name);
            fc->native_func_item = fwd_native;
            jm_register_local_func(mt, native_fwd_name, fwd_native);
        }
    }

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
    mt->current_func_index = -1;
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
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEF_VAL)));

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
        mt->module_scope_env_active = true;

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
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64,
                        s * (int)sizeof(uint64_t), mt->scope_env_reg, 0, 1),
                    MIR_new_reg_op(mt->ctx, mt->scope_env_reg)));
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
                if (!mt->is_module && !mt->is_eval_direct && !mce->is_iife_var && !mce->is_implicit_global) {
                    const char* js_name = mce->name;
                    if (strncmp(js_name, "_js_", 4) == 0) js_name += 4;
                    MIR_reg_t key_reg = jm_box_property_name_literal(mt, js_name, strlen(js_name));
                    // Global lexical declarations are checked during script
                    // declaration instantiation and tracked separately from
                    // globalThis properties for later evalScript collision checks.
                    jm_call_1(mt, "js_evalscript_check_global_lex_decl", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg));
                    jm_emit_error_lane_propagate_check(mt);
                    MIR_reg_t undef_lex = jm_new_reg(mt, "global_lex_undef", MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, undef_lex),
                        MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEF_VAL)));
                    jm_call_void_3(mt, "js_global_lexical_declare",
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, undef_lex),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, mce->var_kind == JS_VAR_CONST ? 1 : 0));
                }
                MIR_reg_t tdz_val = jm_new_reg(mt, "tdz_init", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, tdz_val),
                    MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_TDZ)));
                jm_call_void_2(mt, "js_set_module_var",
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mce->int_val),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, tdz_val));
            }
        }
    }

    // Initialize declared var module vars to undefined (not implicit globals,
    // and not preamble-inherited entries from outer scope e.g. eval)
    if (mt->module_consts) {
        size_t var_iter = 0; void* var_item;
        while (hashmap_iter(mt->module_consts, &var_iter, &var_item)) {
            JsModuleConstEntry* mce = (JsModuleConstEntry*)var_item;
            if (mce->const_type == MCONST_MODVAR &&
                mce->var_kind == JS_VAR_VAR && !mce->is_implicit_global &&
                (int)mce->int_val >= preamble_var_limit) {
                bool needs_eval_bridge = mt->is_eval_direct && mce->is_nested_func_hoist;
                bool should_define_global = !mt->is_module && !mt->is_eval_direct &&
                    !mce->is_iife_var && !mce->is_implicit_global;
                MIR_reg_t init_val = 0;
                if (mt->is_eval_direct && mce->is_nested_func_hoist) {
                    const char* js_name = mce->name;
                    if (strncmp(js_name, "_js_", 4) == 0) js_name += 4;
                    MIR_reg_t key_reg = jm_box_property_name_literal(mt, js_name, strlen(js_name));
                    MIR_reg_t bridged_reg = jm_call_1(mt, "js_eval_env_has_binding", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg));
                    MIR_label_t use_undef = jm_new_label(mt);
                    MIR_label_t init_done = jm_new_label(mt);
                    init_val = jm_new_reg(mt, "var_init", MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
                        MIR_new_label_op(mt->ctx, use_undef),
                        MIR_new_reg_op(mt->ctx, bridged_reg)));
                    MIR_reg_t bridged_val = jm_call_1(mt, "js_get_global_property", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg));
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, init_val),
                        MIR_new_reg_op(mt->ctx, bridged_val)));
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, init_done)));
                    jm_emit_label(mt, use_undef);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, init_val),
                        MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEF_VAL)));
                    jm_emit_label(mt, init_done);
                } else {
                    init_val = jm_new_reg(mt, "var_init", MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, init_val),
                        MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEF_VAL)));
                }
                jm_call_void_2(mt, "js_set_module_var",
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mce->int_val),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, init_val));
                if (!mt->is_module && !mt->is_eval_direct &&
                    !mce->is_iife_var && !mce->is_implicit_global) {
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
    // Suppression: skip if any let/const declaration in the eval program has the
    // same name (B.3.3.3 step 1.b — would be an early SyntaxError otherwise).
    struct hashmap* annexb_lex_collisions = NULL;
    if (!mt->is_global_strict && !mt->is_module && mt->module_consts) {
        annexb_lex_collisions = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        JsAstNode* s = program->body;
        while (s) { jm_collect_all_let_const_names_recursive(s, annexb_lex_collisions); s = s->next; }
        size_t aiter = 0; void* aitem;
        while (hashmap_iter(mt->module_consts, &aiter, &aitem)) {
            JsModuleConstEntry* mce = (JsModuleConstEntry*)aitem;
            if (mce->const_type != MCONST_MODVAR) continue;
            if (!mce->is_nested_func_hoist) continue;
            if (mce->is_iife_var) continue;
            // Suppress if a let/const in the program shadows this name
            JsNameSetEntry lex_lookup;
            memset(&lex_lookup, 0, sizeof(lex_lookup));
            lex_lookup.name = jm_persist_name(mce->name);
            if (hashmap_get(annexb_lex_collisions, &lex_lookup)) {
                log_debug("js-mir: AnnexB suppress globalThis pre-init for %s (let/const collision)", mce->name);
                mce->annexb_suppressed = true;
                MIR_reg_t unresolved_reg = jm_new_reg(mt, "annexb_unres", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, unresolved_reg),
                    MIR_new_int_op(mt->ctx, (int64_t)ITEM_ERROR)));
                jm_call_void_2(mt, "js_set_module_var",
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mce->int_val),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, unresolved_reg));
                continue;
            }
            const char* js_name = mce->name;
            if (strncmp(js_name, "_js_", 4) == 0) js_name += 4;
            MIR_reg_t key_reg = jm_box_property_name_literal(mt, js_name, strlen(js_name));
            MIR_label_t skip_preinit = jm_new_label(mt);
            if (mt->is_eval_direct) {
                MIR_reg_t bridged_reg = jm_call_1(mt, "js_eval_env_has_binding", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                    MIR_new_label_op(mt->ctx, skip_preinit),
                    MIR_new_reg_op(mt->ctx, bridged_reg)));
            }
            MIR_reg_t undef_reg = jm_new_reg(mt, "annexb_undef", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, undef_reg),
                MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEF_VAL)));
            if (mt->is_eval_direct) {
                MIR_reg_t eval_env_active = jm_call_0(mt, "js_eval_env_is_active", MIR_T_I64);
                MIR_label_t global_preinit = jm_new_label(mt);
                MIR_label_t preinit_done = jm_new_label(mt);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
                    MIR_new_label_op(mt->ctx, global_preinit),
                    MIR_new_reg_op(mt->ctx, eval_env_active)));
                jm_call_void_2(mt, "js_eval_local_export_var",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, undef_reg));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, preinit_done)));
                jm_emit_label(mt, global_preinit);
                jm_call_void_1(mt, "js_eval_env_track_global_binding",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg));
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
        hashmap_free(annexb_lex_collisions);
    }

    // Module mode: create namespace object to hold exports
    if (mt->is_module) {
        mt->namespace_reg = jm_call_0(mt, "js_get_active_module_namespace", MIR_T_I64);

        // Js57 P3 (Track B2): no namespace pre-init is needed. The live-binding
        // runtime helper detects "default not yet exported" via the absence of
        // the `default` own property (see js_get_live_binding_default). The
        // existing `js_property_set` at the `export default <expr>` site is
        // what publishes the binding.
    }

    // Js57 P7d-C: detect TLA in module body so the body emission can install
    // a state-dispatch right before the main statement loop and the split
    // sequence at the first top-level ExpressionStatement(AwaitExpression).
    // Only applies to nested-load modules (depth >= 2). Entry modules
    // (top_level_await tests like top-level-ticks.js) need the original
    // sync-with-microtask-drain semantics so the test's own ticks ordering
    // stays observable.
    bool p7d_has_tla = false;
    MIR_label_t p7d_post_await_label = NULL;
    {
        extern int js_dynamic_import_suppress_module_drain;
        // Body split applies only to statically loaded nested modules. The
        // entry module (depth == 1) keeps its existing sync-with-microtask
        // semantics so the top-level-ticks family stays observable. Modules
        // loaded via js_dynamic_import (suppress > 0) also keep the sync path
        // so `await import('…')` callers see the fully-evaluated namespace.
        if (mt->is_module && mt->in_main && mt->filename && js_tla_module_depth_get() >= 2 &&
            js_dynamic_import_suppress_module_drain == 0) {
            int p7d_tla_count = 0;
            for (JsAstNode* s = program->body; s; s = s->next) {
                p7d_tla_count += jm_count_awaits(s);
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
                if (fc && fc->func_item && fc->capture_count == 0) {
                    // Non-capturing: hoist normally
                    int pc = jm_count_params(fn);
                    if (fc->has_rest_param) pc = -pc;  // negative signals rest params
                    const char* vname = jm_format_name("_js_%.*s",
                        (int)fn->name->len, fn->name->chars);
                    MIR_reg_t var_reg = jm_new_reg(mt, vname, MIR_T_I64);
                    MIR_reg_t fn_item = jm_call_2(mt, "js_new_function_mir", MIR_T_I64,
                        MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, pc));
                    // Keep hoisted declarations on the same atomic metadata path as
                    // closures so no partially initialized function can escape.
                    jm_emit_finalize_function(mt, fn_item, fc, fn);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, var_reg),
                        MIR_new_reg_op(mt->ctx, fn_item)));
                    // For reassigned functions, do NOT create a local register;
                    // all reads must go through js_get_module_var to see updates
                    // from self-reassignment inside the function body.
                    if (!fc->is_reassigned)
                        jm_set_var(mt, vname, var_reg);
                    jm_emit_function_decl_runtime_bindings(mt, fn, var_reg, vname);
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
            const char* vname = jm_format_name("_js_%.*s",
                (int)ce->name->len, ce->name->chars);
            // Create a variable holding null placeholder.
            // Actual class instantiation is handled by jm_transpile_new_expr.
            MIR_reg_t var_reg = jm_new_reg(mt, vname, MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, var_reg),
                MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
            jm_set_var(mt, vname, var_reg);
            // Also store null to module var so closures see the initial value
            JsModuleConstEntry mclookup;
            mclookup.name = jm_persist_name(vname);
            JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
            if (mc && mc->const_type == MCONST_CLASS) {
                jm_call_void_2(mt, "js_set_module_var",
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mc->int_val),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, var_reg));
            }
        }
    }

    // Transpile top-level statements in source order.
    // Function declarations with captures are bound at their source position.

    // P9: Pre-scan top-level body for float widening (compound assignments like /=, +=)
    if (program->body) {
        JsAstNode wrapper;
        memset(&wrapper, 0, sizeof(wrapper));
        wrapper.node_type = JS_AST_NODE_BLOCK_STATEMENT;
        // Temporarily wrap program body as a block for prescan
        JsBlockNode blk_wrapper;
        memset(&blk_wrapper, 0, sizeof(blk_wrapper));
        blk_wrapper.node_type = JS_AST_NODE_BLOCK_STATEMENT;
        blk_wrapper.statements = program->body;
        jm_prescan_float_widening(mt, (JsAstNode*)&blk_wrapper);
    }

    // Js57 P7d-C: emit body-state dispatch right before user statements. On
    // re-entry (deferred drain calling js_main again with body_state == 1),
    // skip past pre-await statements to POST_AWAIT.
    if (p7d_has_tla && p7d_post_await_label) {
        MIR_reg_t p7d_spec = jm_box_string_literal(mt, mt->filename,
            (int)strlen(mt->filename));
        MIR_reg_t p7d_state = jm_call_1(mt, "js_module_get_body_state", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, p7d_spec));
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
                            es->export_name->chars, (int)es->export_name->len);
                    } else if (spec->node_type == JS_AST_NODE_IDENTIFIER) {
                        // Back-compat path — kept for safety if any caller still
                        // emits bare identifiers (current AST builder always emits
                        // JsExportSpecifierNode).
                        JsIdentifierNode* id = (JsIdentifierNode*)spec;
                        jm_emit_module_export(mt, id->name->chars, (int)id->name->len, false);
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
                if (fc && fc->func_item && fc->capture_count > 0) {
                    // Capturing function declaration: bind as closure at this position
                    int pc = jm_count_params(fn);
                    if (fc->has_rest_param) pc = -pc;  // negative signals rest params
                    const char* vname = jm_format_name("_js_%.*s",
                        (int)fn->name->len, fn->name->chars);
                    MIR_reg_t var_reg = jm_new_reg(mt, vname, MIR_T_I64);
                    MIR_reg_t env = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
                        MIR_T_I64, MIR_new_int_op(mt->ctx, fc->capture_count));

                    // Track which env slot is the self-reference (for recursive fn decls)
                    int self_ref_slot = -1;

                    for (int ci = 0; ci < fc->capture_count; ci++) {
                        // Check if this capture is the function's own name (self-reference)
                        if (strcmp(fc->captures[ci].name, vname) == 0) {
                            self_ref_slot = ci;
                            // Will be filled after closure creation below
                            continue;
                        }
                        JsMirVarEntry* var = jm_find_var(mt, fc->captures[ci].name);
                        if (var) {
                            // Box native-typed variables before storing in env
                            MIR_reg_t value_to_store = var->reg;
                            if (jm_is_native_type(var->type_id)) {
                                value_to_store = jm_box_native(mt, var->reg, var->type_id);
                            }
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_mem_op(mt->ctx, MIR_T_I64, ci * (int)sizeof(uint64_t), env, 0, 1),
                                MIR_new_reg_op(mt->ctx, value_to_store)));
                        } else {
                            // fallback: check module_consts (implicit globals, module vars, etc.)
                            bool found_mc = false;
                            if (mt->module_consts) {
                                JsModuleConstEntry mclookup;
                                mclookup.name = jm_persist_name(fc->captures[ci].name);
                                JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                                if (mc) {
                                    found_mc = true;
                                    MIR_reg_t const_val = jm_emit_module_const_value(mt, mc);
                                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                        MIR_new_mem_op(mt->ctx, MIR_T_I64, ci * (int)sizeof(uint64_t), env, 0, 1),
                                        MIR_new_reg_op(mt->ctx, const_val)));
                                }
                            }
                            if (!found_mc) {
                                log_error("js-mir: captured var '%s' not found for fn decl '%.*s'",
                                    fc->captures[ci].name, (int)fn->name->len, fn->name->chars);
                            }
                        }
                    }
                    MIR_reg_t fn_item = jm_call_4(mt, "js_new_closure_mir", MIR_T_I64,
                        MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, pc),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, env),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, fc->capture_count));
                    // Capturing declarations need the same metadata invariants as
                    // ordinary function expressions before their binding is visible.
                    jm_emit_finalize_function(mt, fn_item, fc, fn);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, var_reg),
                        MIR_new_reg_op(mt->ctx, fn_item)));
                    jm_set_var(mt, vname, var_reg);

                    // Patch self-reference: update env slot to point to the closure itself
                    if (self_ref_slot >= 0) {
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_mem_op(mt->ctx, MIR_T_I64, self_ref_slot * (int)sizeof(uint64_t), env, 0, 1),
                            MIR_new_reg_op(mt->ctx, var_reg)));
                    }

                    jm_emit_function_decl_runtime_bindings(mt, fn, var_reg, vname);
                }
            }
            // Non-capturing function declarations already handled above
            // Module mode: export the function to namespace
            if (current_export && mt->is_module && fn->name) {
                jm_emit_module_export(mt, fn->name->chars, (int)fn->name->len,
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
                JsClassEntry* ce = jm_find_class(mt, cls_node->name->chars, (int)cls_node->name->len);
                if (ce) {
                    // TDZ: class x extends x {} → throw ReferenceError
                    jm_emit_class_self_extends_check(mt, ce, cls_node->name);
                    MIR_reg_t cls_obj = jm_call_0(mt, "js_new_object", MIR_T_I64);
                    // Class initialization performs allocating metadata and
                    // method setup before its lexical binding is authoritative.
                    jm_create_gc_root_slot(mt, cls_obj);
                    jm_emit_set_private_class_index(mt, cls_obj, ce);
                    jm_emit_set_class_source(mt, cls_obj, cls_node);
                    // Update local variable
                    const char* vname = jm_format_name("_js_%.*s",
                        (int)cls_node->name->len, cls_node->name->chars);
                    JsMirVarEntry* ve = jm_find_var(mt, vname);
                    if (ve) {
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, ve->reg),
                            MIR_new_reg_op(mt->ctx, cls_obj)));
                    }
                    // Store class object in module var
                    JsModuleConstEntry mclookup;
                    mclookup.name = jm_persist_name(vname);
                    JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                    if (mc && mc->const_type == MCONST_CLASS) {
                        jm_call_void_2(mt, "js_set_module_var",
                            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mc->int_val),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
                    }
                    if (ce->inner_module_var_index >= 0) {
                        jm_call_void_2(mt, "js_set_module_var",
                            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)ce->inner_module_var_index),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
                    }
                    if (!mt->is_module) {
                        MIR_reg_t class_key = jm_box_property_name_literal(mt,
                            cls_node->name->chars, cls_node->name->len);
                        if (mt->is_eval_direct) {
                            MIR_reg_t evalscript_active = jm_call_0(mt, "js_262_eval_script_is_active", MIR_T_I64);
                            MIR_label_t skip_global_class_lex = jm_new_label(mt);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
                                MIR_new_label_op(mt->ctx, skip_global_class_lex),
                                MIR_new_reg_op(mt->ctx, evalscript_active)));
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
                    JsMirClassSetup class_setup;
                    jm_emit_class_setup(mt, cls_obj, ce, (JsAstNode*)cls_node, false, &class_setup);
                    MIR_reg_t ctor_super_val = class_setup.ctor_super_val;
                    MIR_reg_t class_proto_obj = class_setup.class_proto_obj;
                    JsAstNode* heritage = class_setup.heritage;
                    JsClassEntry* static_superclass = class_setup.static_superclass;

                    // Create __instance_proto__ with all instance methods
                    {
                        MIR_reg_t proto_obj = class_proto_obj;
                        bool heritage_is_null = false;
                        jm_call_void_2(mt, "js_set_default_constructor_property",
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_obj),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
                        ctor_super_val = jm_emit_class_prototype_chain(mt, ce, heritage,
                            static_superclass, proto_obj, 0, &heritage_is_null);
                        jm_emit_class_instance_setup_tail(mt, cls_obj, ce, proto_obj,
                            ctor_super_val, heritage_is_null);
                    }
                }
            }
            stmt = stmt->next;
            continue;
        }

        // Module mode: handle export default <expression>
        if (current_export && current_export->is_default && mt->is_module) {
            MIR_reg_t val = jm_transpile_box_item(mt, actual_stmt);
            MIR_reg_t key = jm_box_property_name_literal(mt, "default", 7);
            jm_call_3(mt, "js_property_set", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, mt->namespace_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
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
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, arg_val),
                        MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED)));
                }
                MIR_reg_t p7d_spec_split = jm_box_string_literal(mt, mt->filename,
                    (int)strlen(mt->filename));
                // Pass through P5 publish so pending-Promise awaits chain as
                // before (settled/non-Promise values fall through to js_await_sync).
                MIR_reg_t p7d_await_result = jm_call_2(mt, "js_p5_module_await", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, p7d_spec_split),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, arg_val));
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
                jm_call_void_1(mt, "js_module_mark_post_await_pending",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, p7d_spec_split));
                // Assign this module an AEO slot (idempotent — only set on
                // first call). Importers register with us as a parent before
                // we hit the drain, so AEO needs to be defined first.
                jm_call_1(mt, "js_module_assign_async_eval_order", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, p7d_spec_split));
                // Return the namespace immediately; post-await statements run
                // on re-entry via the dispatch label.
                jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
                    MIR_new_reg_op(mt->ctx, mt->namespace_reg)));
                // Emit POST_AWAIT label — subsequent statements land here on
                // the second call.
                jm_emit_label(mt, p7d_post_await_label);
                p7d_post_await_label = NULL;  // single-shot split
                stmt = stmt->next;
                continue;
            }
            if (es->expression) {
                MIR_reg_t val = jm_transpile_box_item(mt, es->expression);
                if (es->expression->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
                    jm_call_void_1(mt, "js_discard_value",
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                }
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, result),
                    MIR_new_reg_op(mt->ctx, val)));
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
            // Suppression: skip if AnnexB conditions disqualified this entry
            // (let/const collision, catch param, existing fn).
            if (mce->is_nested_func_hoist && mce->annexb_suppressed) continue;
            // Strip _js_ prefix to get the original JS name
            const char* js_name = mce->name;
            if (strncmp(js_name, "_js_", 4) == 0) js_name += 4;
            MIR_reg_t key_reg = jm_box_property_name_literal(mt, js_name, strlen(js_name));
            MIR_reg_t val_reg = jm_call_1(mt, "js_get_module_var", MIR_T_I64,
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mce->int_val));
            MIR_reg_t eval_env_active = jm_call_0(mt, "js_eval_env_is_active", MIR_T_I64);
            MIR_label_t global_export = jm_new_label(mt);
            MIR_label_t export_done = jm_new_label(mt);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
                MIR_new_label_op(mt->ctx, global_export),
                MIR_new_reg_op(mt->ctx, eval_env_active)));
            jm_call_void_2(mt, "js_eval_local_export_var",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val_reg));
            MIR_reg_t evalscript_local_active = jm_call_0(mt, "js_262_eval_script_is_active", MIR_T_I64);
            MIR_label_t skip_evalscript_global = jm_new_label(mt);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
                MIR_new_label_op(mt->ctx, skip_evalscript_global),
                MIR_new_reg_op(mt->ctx, evalscript_local_active)));
            // evalScript var declarations use Script global binding semantics
            // even when the harness has an eval-local frame active.
            jm_call_void_3(mt, "js_define_global_property_v",
                MIR_T_I64, MIR_new_int_op(mt->ctx, 0),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val_reg));
            jm_emit_label(mt, skip_evalscript_global);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, export_done)));
            jm_emit_label(mt, global_export);
            MIR_reg_t evalscript_active = jm_call_0(mt, "js_262_eval_script_is_active", MIR_T_I64);
            MIR_label_t ordinary_eval_export = jm_new_label(mt);
            MIR_label_t global_define_done = jm_new_label(mt);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
                MIR_new_label_op(mt->ctx, ordinary_eval_export),
                MIR_new_reg_op(mt->ctx, evalscript_active)));
            // $262.evalScript runs script-level global declaration instantiation;
            // var declarations create non-configurable bindings, unlike ordinary eval.
            jm_call_void_3(mt, "js_define_global_property_v",
                MIR_T_I64, MIR_new_int_op(mt->ctx, 0),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val_reg));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, global_define_done)));
            jm_emit_label(mt, ordinary_eval_export);
            jm_call_void_3(mt, "js_set_global_property",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val_reg),
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
            jm_emit_label(mt, global_define_done);
            jm_call_void_2(mt, "js_eval_local_export_var",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val_reg));
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
        jm_call_void_1(mt, "js_module_complete_tla_body",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, p7d_complete_spec));
    }

    // Module mode: return namespace instead of result
    if (mt->is_module) {
        jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, mt->namespace_reg)));
    } else {
        jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, result)));
    }

    // Main error exit returns the routed D8.4.3 ERROR Item unchanged.
    if (mt->func_error_lane_label) {
        jm_emit_label(mt, mt->func_error_lane_label);
        MIR_reg_t exc_ret = jm_emit_error_lane_return(mt);
        jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
            MIR_new_reg_op(mt->ctx, exc_ret)));
    }

    jm_pop_scope(mt);
    jm_finish_function_frame(mt, "js_main");
    MIR_finish_func(mt->ctx);
    MIR_finish_module(mt->ctx);

    // Load module for linking
    MIR_load_module(mt->ctx, mt->module);
    return true;
}

// ============================================================================
// Parallel JS Module Compilation
// ============================================================================
// Pre-discovers all import dependencies via Tree-sitter shallow parse, then
// compiles modules in parallel (per topological depth level) and executes
// serially in dependency order.  Mirrors Lambda's precompile_imports() design.
// Enabled only on non-Windows platforms with >=2 imported modules.

#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>

// Hashmap entry for path->index dedup
typedef struct {
    const char* path;
    int index;
} JsPathIndexEntry;

uint64_t js_path_index_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const JsPathIndexEntry* e = (const JsPathIndexEntry*)item;
    return hashmap_sip(e->path, strlen(e->path), seed0, seed1);
}

int js_path_index_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    return strcmp(((const JsPathIndexEntry*)a)->path, ((const JsPathIndexEntry*)b)->path);
}

// Add dependency edge from parent to dep
void jm_add_dep(JsImportGraphNode* nodes, int parent_idx, int dep_idx) {
    JsImportGraphNode* parent = &nodes[parent_idx];
    if (parent->dep_count >= parent->dep_cap) {
        parent->dep_cap = parent->dep_cap ? parent->dep_cap * 2 : 4;
        parent->deps = (int*)mem_realloc(parent->deps, sizeof(int) * parent->dep_cap, MEM_CAT_JS_RUNTIME);
    }
    parent->deps[parent->dep_count++] = dep_idx;
}

// Discover imports from a JS source using Tree-sitter shallow CST walk.
// Extracts import_statement source specifiers, resolves paths, recurses.
void jm_discover_js_imports_recursive(
    TSParser* parser, int parent_idx,
    JsImportGraphNode** nodes, int* count, int* capacity,
    struct hashmap* path_map)
{
    JsImportGraphNode* parent = &(*nodes)[parent_idx];
    if (!parent->source) return;

    TSTree* tree = ts_parser_parse_string(parser, NULL, parent->source, strlen(parent->source));
    if (!tree) return;

    // save path before potential realloc
    const char* parent_path = parent->path;

    TSNode root = ts_tree_root_node(tree);
    uint32_t child_count = ts_node_named_child_count(root);

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(root, i);
        const char* node_type = ts_node_type(child);

        if (strcmp(node_type, "import_statement") != 0) continue;

        // extract source specifier (string literal)
        TSNode source_node = ts_node_child_by_field_name(child, "source", 6);
        if (ts_node_is_null(source_node)) continue;

        uint32_t start = ts_node_start_byte(source_node);
        uint32_t end = ts_node_end_byte(source_node);
        const char* src_text = (*nodes)[parent_idx].source + start;
        int src_len = (int)(end - start);

        // strip quotes
        if (src_len >= 2 && (src_text[0] == '\'' || src_text[0] == '"')) {
            src_text++;
            src_len -= 2;
        }

        // resolve module path
        char resolved[512];
        jm_resolve_module_path(parent_path, src_text, src_len, resolved, sizeof(resolved));

        if (jm_path_has_lambda_ext(resolved)) {
            // Lambda modules use the cross-language loader during the serial
            // import phase; the JS-only graph cannot parse their .ls syntax.
            continue;
        }

        // dedup check
        JsPathIndexEntry key = { .path = resolved, .index = 0 };
        const JsPathIndexEntry* existing = (const JsPathIndexEntry*)hashmap_get(path_map, &key);

        int dep_idx;
        if (existing) {
            dep_idx = existing->index;
        } else {
            // new module discovered
            if (*count >= *capacity) {
                *capacity *= 2;
                *nodes = (JsImportGraphNode*)mem_realloc(*nodes, sizeof(JsImportGraphNode) * (*capacity), MEM_CAT_JS_RUNTIME);
            }
            dep_idx = *count;
            JsImportGraphNode* n = &(*nodes)[dep_idx];
            memset(n, 0, sizeof(JsImportGraphNode));
            n->path = mem_strdup(resolved, MEM_CAT_JS_RUNTIME);
            n->source = read_text_file(resolved);
            n->depth = -1;

            JsPathIndexEntry entry = { .path = n->path, .index = dep_idx };
            hashmap_set(path_map, &entry);
            (*count)++;

            // recurse for transitive deps
            if (n->source) {
                jm_discover_js_imports_recursive(parser, dep_idx, nodes, count, capacity, path_map);
            }
        }
        // record dependency
        jm_add_dep(*nodes, parent_idx, dep_idx);
    }

    ts_tree_delete(tree);
}

// Compute topological depth (0 , max(deps)+1 for others)
int jm_compute_depth(JsImportGraphNode* nodes, int idx) {
    if (nodes[idx].depth >= 0) return nodes[idx].depth;
    nodes[idx].depth = 0;  // mark as computing (breaks cycles)
    int max_dep = -1;
    for (int i = 0; i < nodes[idx].dep_count; i++) {
        int d = jm_compute_depth(nodes, nodes[idx].deps[i]);
        if (d > max_dep) max_dep = d;
    }
    nodes[idx].depth = max_dep + 1;
    return nodes[idx].depth;
}

// Pre-link validation: scan all MIR instructions for NULL label operands.
// Returns true if safe to link, false if NULL labels found (would crash MIR_link).
bool jm_validate_mir_labels(MIR_context_t ctx) {
    bool safe = true;
#ifndef NDEBUG
    int func_count = 0, insn_count = 0;
#endif
    bool trace_validation = getenv("JS_MIR_VALIDATE_TRACE") != NULL;
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
    if (trace_validation || !safe) {
        log_debug("js-mir: validate scanned %d funcs %d insns safe=%d", func_count, insn_count, safe);
    }
    return safe;
}

// Compile a single JS module (parse + AST + MIR transpile + link).
// Does NOT execute the module or call jm_load_imports() — dependencies
// are pre-compiled and will be registered before this module executes.
// Returns true on success; populates node->mir_ctx and node->js_main_func.
bool jm_compile_js_module(Runtime* runtime, JsImportGraphNode* node) {
    jm_log_module_phase_progress(node->path, "parallel-compile-begin");
    JsTranspiler* tp = js_transpiler_create(runtime);
    if (!tp) {
        log_error("js-parallel: failed to create transpiler for '%s'", node->path);
        return false;
    }

    if (!js_transpiler_parse(tp, node->source, strlen(node->source))) {
        log_error("js-parallel: parse failed for '%s'", node->path);
        js_transpiler_destroy(tp);
        return false;
    }

    TSNode root = ts_tree_root_node(tp->tree);
    JsAstNode* js_ast = build_js_ast(tp, root);
    if (!js_ast) {
        log_error("js-parallel: AST build failed for '%s'", node->path);
        js_transpiler_destroy(tp);
        return false;
    }

    // NOTE: No jm_load_imports() — dependencies compiled separately

    MIR_context_t ctx = jit_init(g_js_mir_optimize_level);
    if (!ctx) {
        log_error("js-parallel: MIR init failed for '%s'", node->path);
        js_transpiler_destroy(tp);
        return false;
    }

    // Install batch error handler if set
    if (g_batch_mir_error_handler) {
        MIR_set_error_func(ctx, g_batch_mir_error_handler);
    }

    JsMirTranspiler* mt = jm_create_mir_transpiler(tp, ctx, node->path, true, 64, 32, 16, "js-parallel");
    if (!mt) {
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        return false;
    }

    mt->module = MIR_new_module(ctx, "js_module");

    // parallel workers have no bound JS realm; route compiler-generated names
    // to this worker's private pool instead of racing a shared runtime pool.
    jm_set_name_pool_override(tp->name_pool);
    if (!transpile_js_mir_ast(mt, js_ast)) {
        jm_set_name_pool_override(NULL);
        log_error("js-parallel: collection/allocation failed for '%s'", node->path);
        jm_destroy_mir_transpiler(mt);
        js_transpiler_destroy(tp);
        MIR_finish(ctx);
        return false;
    }
    jm_set_name_pool_override(NULL);
    node->module_var_count = (uint32_t)mt->module_var_count;
    node->ic_count = mt->ic_count;
    if (mt->module_name_specs && mt->module_name_specs->length > 0) {
        if (!jm_build_property_key_image(NULL, 0, 0, mt->module_name_specs,
                &node->module_property_specs, &node->module_property_count,
                &node->module_property_bytes_size)) {
            jm_destroy_mir_transpiler(mt);
            js_transpiler_destroy(tp);
            MIR_finish(ctx);
            return false;
        }
    }

    if (!jm_validate_mir_labels(ctx)) {
        log_error("js-parallel: NULL labels detected for '%s'", node->path);
        jm_destroy_mir_transpiler(mt);
        js_transpiler_destroy(tp);
        MIR_finish(ctx);
        return false;
    }

    MIR_link(ctx, g_mir_interp_mode ? MIR_set_interp_interface : MIR_set_gen_interface, import_resolver);

    typedef Item (*js_main_func_t)(Context*);
    js_main_func_t js_main = (js_main_func_t)find_func(ctx, (char*)"js_main");

    // cleanup transpiler state
    jm_destroy_mir_transpiler(mt);
    js_transpiler_destroy(tp);

    if (!js_main) {
        log_error("js-parallel: failed to find js_main for '%s'", node->path);
        MIR_finish(ctx);
        return false;
    }

    node->mir_ctx = ctx;
    node->js_main_func = (void*)js_main;
    node->compiled = true;
    jm_log_module_phase_progress(node->path, "parallel-compile-end");
    return true;
}

// Worker argument for parallel JS module compilation
typedef struct {
    Runtime* runtime;
    JsImportGraphNode* node;
    bool success;
} JsCompileWorkerArg;

// Worker thread for parallel module compilation
void* jm_compile_js_worker(void* arg) {
    JsCompileWorkerArg* work = (JsCompileWorkerArg*)arg;
    work->success = jm_compile_js_module(work->runtime, work->node);
    return NULL;
}

// Pre-compile all JS import dependencies in parallel, then execute serially.
// Called from transpile_js_to_mir() after heap/context setup.
// Returns the number of modules successfully precompiled and executed.
static bool jm_module_tree_contains_await(TSNode node) {
    TSTreeCursor cursor = ts_tree_cursor_new(node);
    while (true) {
        if (strcmp(ts_node_type(ts_tree_cursor_current_node(&cursor)),
                "await_expression") == 0) {
            ts_tree_cursor_delete(&cursor);
            return true;
        }
        if (ts_tree_cursor_goto_first_child(&cursor)) continue;
        while (!ts_tree_cursor_goto_next_sibling(&cursor)) {
            if (!ts_tree_cursor_goto_parent(&cursor)) {
                ts_tree_cursor_delete(&cursor);
                return false;
            }
        }
    }
}

int jm_precompile_js_imports(Runtime* runtime, const char* js_source, const char* filename) {
    if (!filename) return 0;

    // create JS parser for discovery
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_javascript());

    // initialize graph with main script as sentinel (index 0, not compiled here)
    int capacity = 16;
    int count = 1;
    JsImportGraphNode* nodes = (JsImportGraphNode*)mem_calloc(capacity, sizeof(JsImportGraphNode), MEM_CAT_JS_RUNTIME);
    nodes[0].path = mem_strdup(filename, MEM_CAT_JS_RUNTIME);
    nodes[0].source = mem_strdup(js_source, MEM_CAT_JS_RUNTIME);
    nodes[0].depth = -1;

    struct hashmap* path_map = hashmap_new(sizeof(JsPathIndexEntry), 64, 0, 0,
        js_path_index_hash, js_path_index_compare, NULL, NULL);
    JsPathIndexEntry main_entry = { .path = nodes[0].path, .index = 0 };
    hashmap_set(path_map, &main_entry);

    // discover all imports recursively
    jm_discover_js_imports_recursive(parser, 0, &nodes, &count, &capacity, path_map);
    bool graph_contains_await = false;
    for (int i = 1; i < count && !graph_contains_await; i++) {
        if (!nodes[i].source) continue;
        TSTree* module_tree = ts_parser_parse_string(parser, NULL,
            nodes[i].source, strlen(nodes[i].source));
        if (!module_tree) continue;
        graph_contains_await = jm_module_tree_contains_await(
            ts_tree_root_node(module_tree));
        ts_tree_delete(module_tree);
    }
    ts_parser_delete(parser);
    hashmap_free(path_map);

    int import_count = count - 1;
    if (import_count == 0) {
        for (int i = 0; i < count; i++) {
            mem_free(nodes[i].path);
            mem_free(nodes[i].source);
            mem_free(nodes[i].deps);
        }
        mem_free(nodes);
        return 0;
    }

    log_info("js-parallel: discovered %d JS modules, pre-compiling...", import_count);

    // ensure one-time inits before spawning threads
    ensure_jit_imports_initialized();

    // compute topological depths
    int max_depth = 0;
    for (int i = 1; i < count; i++) {
        int d = jm_compute_depth(nodes, i);
        if (d > max_depth) max_depth = d;
    }

    // The static phase compiles every dependency before any initializer runs.
    // TLA graphs retain their existing recursive execution path after this
    // discovery pass; only non-TLA graphs execute these compiled entries.
    bool parallel_compile = import_count >= 2 && !graph_contains_await;

    for (int level = 0; level <= max_depth; level++) {
        // collect modules at this depth
        int batch_indices[64];
        int batch_count = 0;
        for (int i = 1; i < count && batch_count < 64; i++) {
            if (nodes[i].depth == level && nodes[i].source)
                batch_indices[batch_count++] = i;
        }
        if (batch_count == 0) continue;

        // Workers have no runtime realm and only produce sealed key images.
        if (!parallel_compile || batch_count == 1) {
            for (int i = 0; i < batch_count; i++) {
                jm_compile_js_module(runtime, &nodes[batch_indices[i]]);
            }
        } else {
            JsCompileWorkerArg* args = (JsCompileWorkerArg*)mem_calloc(batch_count, sizeof(JsCompileWorkerArg), MEM_CAT_JS_RUNTIME);
            pthread_t* threads = (pthread_t*)mem_alloc(sizeof(pthread_t) * batch_count, MEM_CAT_JS_RUNTIME);
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);

            for (int i = 0; i < batch_count; i++) {
                args[i].runtime = runtime;
                args[i].node = &nodes[batch_indices[i]];
                args[i].success = false;
                pthread_create(&threads[i], &attr, jm_compile_js_worker, &args[i]);
            }

            pthread_attr_destroy(&attr);
            for (int i = 0; i < batch_count; i++) {
                pthread_join(threads[i], NULL);
            }

            mem_free(threads);
            mem_free(args);
        }
    }

    for (int i = 1; i < count; i++) {
        if (nodes[i].source && !nodes[i].compiled) {
            log_error("js-name-prelink: failed to compile '%s'", nodes[i].path);
            for (int j = 0; j < count; j++) {
                mem_free(nodes[j].path);
                mem_free(nodes[j].source);
                mem_free(nodes[j].deps);
                mem_free(nodes[j].module_property_specs);
                if (nodes[j].mir_ctx) MIR_finish(nodes[j].mir_ctx);
            }
            mem_free(nodes);
            return -1;
        }
        if (nodes[i].compiled && !lambda_property_key_specs_prelink(
                nodes[i].module_property_specs, nodes[i].module_property_count,
                nodes[i].module_property_bytes_size)) {
            log_error("js-name-prelink: invalid property key image for '%s'", nodes[i].path);
            for (int j = 0; j < count; j++) {
                mem_free(nodes[j].path);
                mem_free(nodes[j].source);
                mem_free(nodes[j].deps);
                mem_free(nodes[j].module_property_specs);
                if (nodes[j].mir_ctx) MIR_finish(nodes[j].mir_ctx);
            }
            mem_free(nodes);
            return -1;
        }
    }

    if (graph_contains_await) {
        // TLA evaluation still requires the recursive async-parent protocol.
        // Its key image is now static; jm_load_imports will execute it only
        // after the entry point activates the dynamic child.
        for (int i = 0; i < count; i++) {
            mem_free(nodes[i].path);
            mem_free(nodes[i].source);
            mem_free(nodes[i].deps);
            mem_free(nodes[i].module_property_specs);
            if (nodes[i].mir_ctx) MIR_finish(nodes[i].mir_ctx);
        }
        mem_free(nodes);
        return 0;
    }

    if (!js_activate_runtime_name_pool()) {
        log_error("js-name-prelink: failed to activate dynamic pool");
        for (int i = 0; i < count; i++) {
            mem_free(nodes[i].path);
            mem_free(nodes[i].source);
            mem_free(nodes[i].deps);
            mem_free(nodes[i].module_property_specs);
            if (nodes[i].mir_ctx) MIR_finish(nodes[i].mir_ctx);
        }
        mem_free(nodes);
        return -1;
    }
    // Module initializers can install globals. Realm setup is therefore after
    // the root is sealed, never part of static spelling collection.
    (void)js_get_global_this();

    int precompiled = 0;
    for (int level = 0; level <= max_depth; level++) {
        for (int idx = 1; idx < count; idx++) {
            if (nodes[idx].depth != level || !nodes[idx].compiled) continue;
            typedef Item (*js_main_func_t)(Context*);
            js_main_func_t js_main = (js_main_func_t)nodes[idx].js_main_func;
            uint32_t prev_module_state_id = js_get_active_module_state_id();
            if (!js_activate_module_state(nodes[idx].module_var_count) ||
                    !lambda_module_state_link_property_keys(js_get_active_module_state_id(),
                        nodes[idx].module_property_specs, nodes[idx].module_property_count,
                        nodes[idx].module_property_bytes_size) ||
                    !js_link_module_ic_table(js_get_active_module_state_id(), nodes[idx].ic_count)) {
                js_set_active_module_state_id(prev_module_state_id);
                log_error("js-name-prelink: failed to link compiled module '%s'",
                    nodes[idx].path ? nodes[idx].path : "<module>");
                for (int j = 0; j < count; j++) {
                    mem_free(nodes[j].path);
                    mem_free(nodes[j].source);
                    mem_free(nodes[j].deps);
                    mem_free(nodes[j].module_property_specs);
                    if (nodes[j].mir_ctx) MIR_finish(nodes[j].mir_ctx);
                }
                mem_free(nodes);
                return -1;
            }
            jm_log_module_phase_progress(nodes[idx].path, "parallel-execute-begin");
            Item namespace_obj = js_main((Context*)context);
            jm_log_module_phase_progress(nodes[idx].path, "parallel-execute-end");
            js_set_active_module_state_id(prev_module_state_id);
            String* spec_str = heap_create_name(nodes[idx].path, strlen(nodes[idx].path));
            Item spec_item = (Item){.item = s2it(spec_str)};
            js_module_register(spec_item, namespace_obj);
            jm_defer_mir_cleanup(nodes[idx].mir_ctx);
            nodes[idx].mir_ctx = NULL;
            precompiled++;
            log_debug("js-parallel: module '%s' compiled and executed", nodes[idx].path);
        }
    }

    log_info("js-parallel: pre-compiled and executed %d modules", precompiled);

    // cleanup graph
    for (int i = 0; i < count; i++) {
        mem_free(nodes[i].path);
        mem_free(nodes[i].source);
        mem_free(nodes[i].deps);
        if (nodes[i].module_property_specs) {
            mem_free(nodes[i].module_property_specs);
        }
        if (nodes[i].mir_ctx) MIR_finish(nodes[i].mir_ctx);
    }
    mem_free(nodes);

    return precompiled;
}

#endif // !_WIN32

#ifdef _WIN32
// jm_validate_mir_labels is a no-op on Windows (parallel import not supported)
bool jm_validate_mir_labels(MIR_context_t ctx) { (void)ctx; return true; }
#endif

// ============================================================================
// ES Module loading: compile and execute a module, returning its namespace
// ============================================================================

static bool jm_module_phase_progress_is_enabled(void) {
    const char* value = getenv("LAMBDA_JS_MIR_PHASE_PROGRESS");
    return value && value[0] && strcmp(value, "0") != 0;
}

static void jm_log_module_phase_progress(const char* filename, const char* phase) {
    if (!jm_module_phase_progress_is_enabled()) return;
    log_notice("js-module-phase: file=%s phase=%s",
        filename ? filename : "<module>", phase);
}

#ifndef _WIN32
// windows does not compile the POSIX import-precompile path that consumes this helper.
static bool jm_module_has_static_imports(JsAstNode* ast) {
    if (!ast || ast->node_type != JS_AST_NODE_PROGRAM) return false;
    JsProgramNode* program = (JsProgramNode*)ast;
    for (JsAstNode* statement = program->body; statement; statement = statement->next) {
        if (statement->node_type == JS_AST_NODE_IMPORT_DECLARATION) return true;
    }
    return false;
}
#endif

static bool jm_module_has_top_level_await(JsAstNode* ast) {
    if (!ast || ast->node_type != JS_AST_NODE_PROGRAM) return false;
    JsProgramNode* program = (JsProgramNode*)ast;
    for (JsAstNode* statement = program->body; statement; statement = statement->next) {
        if (jm_count_awaits(statement) > 0) return true;
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
    jm_log_module_phase_progress(filename, "begin");
    // Module compilation bypasses transpile_js_to_mir_core_len(), which normally
    // binds the Context-owned JS state. Test262's hot batch path calls this
    // entrypoint directly; without this bind, TLA state dereferences a null
    // capsule before the module can be parsed.
    if (!runtime || !context || !context->heap ||
            !js_runtime_state_thread_initialize(context)) {
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
        return ItemNull;
    }
    jm_track_active_js_transpile(tp, NULL, NULL);


    jm_log_module_phase_progress(filename, "parse-begin");
    if (!js_transpiler_parse(tp, js_source, strlen(js_source))) {
        // Js57 P7b: parse failure is a SyntaxError. Return ITEM_ERROR (not
        // ItemNull) so the batch driver short-circuits its post-test global
        // probes (async_required check), which SEGV when the heap was never
        // initialized for this test.
        log_error("js-mir: module: parse failed for '%s'", filename);
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        js_tla_exit_module();
        return (Item){.item = ITEM_ERROR};
    }
    jm_log_module_phase_progress(filename, "parse-end");

    TSNode root = ts_tree_root_node(tp->tree);
    jm_log_module_phase_progress(filename, "ast-begin");
    JsAstNode* js_ast = build_js_ast(tp, root);
    if (!js_ast) {
        log_error("js-mir: module: AST build failed for '%s'", filename);
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        js_tla_exit_module();
        return (Item){.item = ITEM_ERROR};
    }
    jm_log_module_phase_progress(filename, "ast-end");

    // Js57 P7b: run early-error checks before any further compilation. The
    // module path previously skipped this and crashed on illegal forms like
    // `await 0;` (escaped await — contextually-reserved keyword written
    // with a unicode escape, which is a SyntaxError per the spec).
    jm_log_module_phase_progress(filename, "early-begin");
    int p7b_early_errors = js_check_early_errors(tp, js_ast);
    if (p7b_early_errors > 0) {
        log_error("js-mir: module: %d early error(s) for '%s'", p7b_early_errors, filename);
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        js_tla_exit_module();
        return (Item){.item = ITEM_ERROR};
    }
    jm_log_module_phase_progress(filename, "early-end");

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

    // Js57 P7d-A: detect top-level await in module body. jm_count_awaits skips
    // nested function/class scopes, so non-zero only when there's a real TLA
    // statement somewhere in the module's top-level. Mark the module so
    // jm_load_imports can wire up the importer's PendingAsyncDeps counter
    // when the importer pulls this dep in. Only gate on depth >= 2 (nested
    // load) — for the entry module the body still runs synchronously through
    // js_main and microtask drains as before; entry-level TLA modules with
    // top-level ticks rely on that semantics. Modules loaded via dynamic
    // import (suppress > 0) also stay on the sync path so `await import('…')`
    // callers see the fully-evaluated namespace.
    extern int js_dynamic_import_suppress_module_drain;
    bool module_has_top_level_await = jm_module_has_top_level_await(js_ast);
    if (js_tla_module_depth_get() >= 2 && js_dynamic_import_suppress_module_drain == 0) {
        if (module_has_top_level_await) {
            js_module_mark_has_tla(p7d_self_spec_item);
            log_debug("P7d-A: module '%s' has TLA (top-level await detected)", filename);
        }
    }

    MIR_context_t ctx = jit_init(g_js_mir_optimize_level);
    if (!ctx) {
        log_error("js-mir: module: MIR context init failed for '%s'", filename);
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        return ItemNull;
    }

    JsMirTranspiler* mt = jm_create_mir_transpiler(tp, ctx, filename, true, 64, 32, 16, "js-mir: module");
    if (!mt) {
        MIR_finish(ctx);
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        return ItemNull;
    }
    jm_track_active_js_transpile(NULL, mt, NULL);
    // A dynamic module compiled while a Test262 preamble is active inherits
    // that sealed name/IC image; local indexes must start after the inherited
    // entries or globalThis member loads resolve to another preamble name.
    mt->module_name_base = g_jm_preamble_in
        ? g_jm_preamble_in->module_property_count : 0;
    mt->module_ic_base = g_jm_preamble_in ? g_jm_preamble_in->ic_count : 0;

    mt->module = MIR_new_module(ctx, "js_module");

    jm_log_module_phase_progress(filename, "mir-begin");
    if (!transpile_js_mir_ast(mt, js_ast)) {
        log_error("js-mir: module: collection/allocation failed for '%s'", filename);
        jm_clear_active_js_transpile(NULL, mt, NULL);
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }
    jm_log_module_phase_progress(filename, "mir-end");

    // Module entry points bypass the script compiler's prelink step.  Its
    // local spelling image must be collected before imports can run: an entry
    // module can be compiled directly while the root is still static.
    if (!js_prelink_compiled_name_table(mt)) {
        log_error("js-mir: module: failed to prelink property-name table for '%s'",
            filename ? filename : "<module>");
        jm_clear_active_js_transpile(NULL, mt, NULL);
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        js_tla_exit_module();
        return (Item){.item = ITEM_ERROR};
    }

    // This mirrors the source-entry static phase.  In particular, do not let
    // jm_load_imports create a dynamic child before this module's own names
    // have joined the static root (D4.6.1v2, D4.6.2v2).
    jm_log_module_phase_progress(filename, "imports-begin");
#ifndef _WIN32
    if (js_tla_module_depth_get() == 1 && js_dynamic_import_suppress_module_drain == 0 &&
            !module_has_top_level_await && jm_module_has_static_imports(js_ast) &&
            jm_precompile_js_imports(runtime, js_source, filename) < 0) {
        log_error("js-mir: module: failed to precompile import closure for '%s'",
            filename ? filename : "<module>");
        jm_clear_active_js_transpile(NULL, mt, NULL);
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        js_tla_exit_module();
        return (Item){.item = ITEM_ERROR};
    }
#endif
    if (!js_activate_runtime_name_pool()) {
        log_error("js-mir: module: failed to activate dynamic NamePool for '%s'",
            filename ? filename : "<module>");
        jm_clear_active_js_transpile(NULL, mt, NULL);
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        js_tla_exit_module();
        return (Item){.item = ITEM_ERROR};
    }
    // Realm construction is runtime work, after the static root is sealed.
    (void)js_get_global_this();
    jm_load_imports(runtime, js_ast, filename);
    jm_log_module_phase_progress(filename, "imports-end");

    RootFrame import_error_roots(1);
    Rooted<Item> imported_error(import_error_roots,
        js_module_get_evaluation_error(p7d_self_spec_item));
    if (get_type_id(imported_error.get()) != LMD_TYPE_NULL) {
        // A static dependency that failed evaluation rejects this module
        // before its body runs; compiling the importer would otherwise turn
        // the failed graph into a successful empty namespace.
        log_debug("js-mir: module '%s' dependency evaluation failed",
            filename ? filename : "<module>");
        jm_clear_active_js_transpile(NULL, mt, NULL);
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        js_tla_exit_module();
        return js_throw_value(imported_error.get());
    }

    if (!jm_validate_mir_labels(ctx)) {
        log_error("js-mir: module: NULL labels detected for '%s'", filename);
        jm_clear_active_js_transpile(NULL, mt, NULL);
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    jm_log_module_phase_progress(filename, "link-begin");
    MIR_link(ctx, g_mir_interp_mode ? MIR_set_interp_interface : MIR_set_gen_interface, import_resolver);
    jm_log_module_phase_progress(filename, "link-end");

    typedef Item (*js_main_func_t)(Context*);
    js_main_func_t js_main = (js_main_func_t)find_func(ctx, (char*)"js_main");

    if (!js_main) {
        log_error("js-mir: module: failed to find js_main for '%s'", filename);
        jm_clear_active_js_transpile(NULL, mt, NULL);
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        jm_clear_active_js_transpile(tp, NULL, NULL);
        js_transpiler_destroy(tp);
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

    // Allocate per-module variable storage and switch to it.
    uint32_t prev_module_state_id = js_get_active_module_state_id();
    Item prev_namespace = js_set_active_module_namespace(namespace_obj);
    if (!js_activate_module_state((uint32_t)mt->module_var_count)) return ItemNull;
    if (!js_link_compiled_name_table(mt)) return ItemNull;
    if (js_dynamic_import_suppress_module_drain <= 0) {
        js_event_loop_init();
    }
    // Js57 P7d: save the module's evaluation context (module-state id +
    // namespace already on JsModule) and stash js_main as the deferred entry.
    // Used by the AEO drain to re-enter js_main with the same module-level
    // state when a deferred body / post-await chunk runs.
    js_module_save_context(spec_item, js_get_active_module_state_id());
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
        jm_log_module_phase_progress(filename, "execute-begin");
        namespace_obj = js_main((Context*)context);
        jm_log_module_phase_progress(filename, "execute-end");
        module_body_threw = item_is_error(namespace_obj);
    }
    // Microtasks retain their function owner context, while module-vars and
    // namespace remain dynamically scoped around this drain.
    if (!module_body_threw && js_dynamic_import_suppress_module_drain <= 0) {
        js_event_loop_drain();
    }
    js_set_active_module_state_id(prev_module_state_id);
    js_set_active_module_namespace(prev_namespace);
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
    module_register(filename, "js", namespace_obj, ctx);

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
                size_t rlen = strlen(resolved);
                bool is_lambda_module = (rlen > 3 && strcmp(resolved + rlen - 3, ".ls") == 0);

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
                        module_register(resolved, "lambda", ns, lambda_script->jit_context);
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
