#include "js_mir_internal.hpp"
#include "js_builtin_catalog.hpp"

// ============================================================================
// Statement transpilers
// ============================================================================

static void jm_scope_env_mark_pattern_bindings(JsMirTranspiler* mt, JsAstNode* pat);

static bool jm_discardable_literal_statement(JsAstNode* expression) {
    if (!expression || expression->node_type != JS_AST_NODE_LITERAL) return false;
    JsLiteralNode* literal = (JsLiteralNode*)expression;
    // Primitive literal expression statements have no evaluation effects once
    // completion values are not requested; emitting a boxed temporary here was
    // pure MIR churn, especially for library directive prologues.
    return literal->literal_type == JS_LITERAL_NUMBER ||
        literal->literal_type == JS_LITERAL_STRING ||
        literal->literal_type == JS_LITERAL_BOOLEAN ||
        literal->literal_type == JS_LITERAL_NULL ||
        literal->literal_type == JS_LITERAL_UNDEFINED;
}

static void jm_bind_catch_destructure(JsMirTranspiler* mt,
        JsAstNode* pattern, MIR_reg_t thrown_val, bool is_array) {
    struct hashmap* catch_names = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    jm_collect_pattern_names(pattern, catch_names);
    MIR_reg_t undef_val = jm_emit_undefined(mt);
    size_t name_iter = 0; void* name_item;
    while (hashmap_iter(catch_names, &name_iter, &name_item)) {
        JsNameSetEntry* ne = (JsNameSetEntry*)name_item;
        MIR_reg_t preg = jm_new_reg(mt, ne->name, MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, preg),
            MIR_new_reg_op(mt->ctx, undef_val)));
        jm_set_var(mt, ne->name, preg);
        JsMirVarEntry* ve = jm_find_var(mt, ne->name);
        if (ve) {
            ve->is_let_const = true;
            ve->from_catch_param = true;
        }
        jm_scope_env_mark_and_writeback(mt, ne->name, preg);
    }
    hashmap_free(catch_names);
    if (is_array) jm_emit_array_destructure(mt, pattern, thrown_val);
    else jm_emit_object_destructure(mt, pattern, thrown_val);
    // destructuring defaults can create closures before the pattern finishes.
    jm_scope_env_mark_pattern_bindings(mt, pattern);
}

static JsMirVarEntry* jm_find_var_in_scope_depth(JsMirTranspiler* mt, const char* name, int depth) {
    if (!mt || !name || depth < 0 || depth > mt->scope_depth) return NULL;
    struct hashmap* scope = jm_var_scope_at(mt, depth);
    if (!scope) return NULL;
    JsVarScopeEntry key;
    memset(&key, 0, sizeof(key));
    key.name = name;
    JsVarScopeEntry* found = (JsVarScopeEntry*)hashmap_get(scope, &key);
    return found ? &found->var : NULL;
}

static JsMirVarEntry* jm_find_nearest_catch_param_var(JsMirTranspiler* mt, const char* name) {
    if (!mt || !name) return NULL;
    int start_depth = mt->scope_depth;
    for (int depth = start_depth; depth >= 0; depth--) {
        JsMirVarEntry* var = jm_find_var_in_scope_depth(mt, name, depth);
        if (var && var->from_catch_param) return var;
    }
    return NULL;
}

typedef struct JsMirLastClosureSnapshot {
    bool has_env;
    MIR_reg_t env_reg;
    int capture_count;
    bool preserve_after_readback;
    const char* capture_names[JS_MIR_LAST_CLOSURE_CAPTURE_MAX];
    int capture_slots[JS_MIR_LAST_CLOSURE_CAPTURE_MAX];
    bool capture_is_transitive[JS_MIR_LAST_CLOSURE_CAPTURE_MAX];
    bool capture_is_nfe[JS_MIR_LAST_CLOSURE_CAPTURE_MAX];
    bool capture_is_assigned[JS_MIR_LAST_CLOSURE_CAPTURE_MAX];
} JsMirLastClosureSnapshot;

static int jm_last_closure_capture_count_clamped(int count) {
    if (count < 0) return 0;
    if (count > JS_MIR_LAST_CLOSURE_CAPTURE_MAX) return JS_MIR_LAST_CLOSURE_CAPTURE_MAX;
    return count;
}

static void jm_save_last_closure_snapshot(JsMirTranspiler* mt, JsMirLastClosureSnapshot* snapshot) {
    if (!mt || !snapshot) return;
    snapshot->has_env = mt->last_closure_has_env;
    snapshot->env_reg = mt->last_closure_env_reg;
    snapshot->capture_count = jm_last_closure_capture_count_clamped(mt->last_closure_capture_count);
    snapshot->preserve_after_readback = mt->preserve_last_closure_env_after_readback;
    for (int i = 0; i < snapshot->capture_count; i++) {
        snapshot->capture_names[i] = jm_persist_name(mt->last_closure_capture_names[i]);
        snapshot->capture_slots[i] = mt->last_closure_capture_slots[i];
        snapshot->capture_is_transitive[i] = mt->last_closure_capture_is_transitive[i];
        snapshot->capture_is_nfe[i] = mt->last_closure_capture_is_nfe[i];
        snapshot->capture_is_assigned[i] = mt->last_closure_capture_is_assigned[i];
    }
}

static void jm_clear_last_closure_snapshot(JsMirTranspiler* mt) {
    if (!mt) return;
    mt->last_closure_has_env = false;
    mt->last_closure_env_reg = 0;
    mt->last_closure_capture_count = 0;
}

static void jm_restore_last_closure_snapshot(JsMirTranspiler* mt,
        const JsMirLastClosureSnapshot* snapshot) {
    if (!mt || !snapshot) return;
    mt->last_closure_has_env = snapshot->has_env;
    mt->last_closure_env_reg = snapshot->env_reg;
    mt->last_closure_capture_count = snapshot->capture_count;
    mt->preserve_last_closure_env_after_readback = snapshot->preserve_after_readback;
    for (int i = 0; i < snapshot->capture_count; i++) {
        mt->last_closure_capture_names[i] = jm_persist_name(snapshot->capture_names[i]);
        mt->last_closure_capture_slots[i] = snapshot->capture_slots[i];
        mt->last_closure_capture_is_transitive[i] = snapshot->capture_is_transitive[i];
        mt->last_closure_capture_is_nfe[i] = snapshot->capture_is_nfe[i];
        mt->last_closure_capture_is_assigned[i] = snapshot->capture_is_assigned[i];
    }
}

void jm_write_last_closure_capture_if_matching(JsMirTranspiler* mt,
        const char* name, MIR_reg_t val_reg, TypeId type_id) {
    if (!mt || !name) return;
    MIR_reg_t val = jm_is_native_type(type_id) ? jm_box_native(mt, val_reg, type_id) : val_reg;
    MIR_reg_t last_env = 0;
    int last_slot = -1;
    if (mt->last_closure_has_env && mt->last_closure_env_reg != 0) {
        int capture_count = jm_last_closure_capture_count_clamped(mt->last_closure_capture_count);
        for (int i = 0; i < capture_count; i++) {
            if (mt->last_closure_capture_is_nfe[i]) continue;
            if (strcmp(mt->last_closure_capture_names[i], name) != 0) continue;
            int slot = mt->last_closure_capture_slots[i] >= 0 ? mt->last_closure_capture_slots[i] : i;
            MIR_reg_t target_env = mt->last_closure_env_reg;
            if (mt->last_closure_capture_is_transitive[i]) {
                JsMirVarEntry* var = jm_find_var(mt, name);
                if (!jm_resolve_transitive_capture_env(var, &target_env, &slot)) continue;
            }
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64, slot * (int)sizeof(uint64_t),
                    target_env, 0, 1),
                MIR_new_reg_op(mt->ctx, val)));
            last_env = target_env;
            last_slot = slot;
            break;
        }
    }

    for (int i = 0; i < mt->tdz_closure_capture_count; i++) {
        JsMirTdzClosureCapture* tracked = &mt->tdz_closure_captures[i];
        if (tracked->binding_scope_depth != mt->scope_depth ||
            strcmp(tracked->name, name) != 0) continue;
        MIR_reg_t target_env = tracked->env_reg;
        int slot = tracked->slot;
        // A transitive child resolves through this hoisted parent's copied
        // slot. Redirecting the TDZ initializer to the source cell leaves
        // that copied slot at ItemTdz after the lexical declaration runs.
        if (target_env == last_env && slot == last_slot) continue;
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_mem_op(mt->ctx, MIR_T_I64, slot * (int)sizeof(uint64_t),
                target_env, 0, 1),
            MIR_new_reg_op(mt->ctx, val)));
    }
}

static void jm_write_env_backing_if_needed(JsMirTranspiler* mt, JsMirVarEntry* var,
        MIR_reg_t val_reg, TypeId type_id) {
    if (!mt || !var || !var->from_env || var->env_reg == 0 || var->env_slot < 0) return;
    MIR_reg_t val = val_reg;
    if (jm_is_native_type(type_id)) {
        val = jm_box_native(mt, val_reg, type_id);
    }
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_mem_op(mt->ctx, MIR_T_I64,
            var->env_slot * (int)sizeof(uint64_t), var->env_reg, 0, 1),
        MIR_new_reg_op(mt->ctx, val)));
}

static void jm_emit_for_loop_var_writeback(JsMirTranspiler* mt,
        const char* var_name, int var_len, bool is_let_const_loop,
        MIR_reg_t loop_var) {
    if (!var_name || is_let_const_loop) return;
    const char* wb_vname = jm_format_name("_js_%.*s", var_len, var_name);
    if (mt->module_consts) {
        JsModuleConstEntry lookup;
        lookup.name = jm_persist_name(wb_vname);
        JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup);
        JsMirVarEntry* local_var = jm_find_var(mt, wb_vname);
        bool is_function_local = local_var && mt->current_func_index >= 0;
        if (!is_function_local && mc && mc->const_type == MCONST_MODVAR) {
            jm_call_void_2(mt, "js_set_module_var",
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mc->int_val),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, loop_var));
        } else if (!mc && !is_function_local) {
            MIR_reg_t name_reg = jm_box_property_name_literal(mt, var_name, var_len);
            jm_call_3(mt, "js_set_global_property", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, name_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, loop_var),
                MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
            jm_emit_error_lane_propagate_check(mt);
        }
    }
    jm_scope_env_mark_and_writeback(mt, wb_vname, loop_var);
}

static void jm_emit_for_loop_destructure(JsMirTranspiler* mt,
        JsAstNode* destr_pattern, JsAstNode* obj_destr_pattern,
        MIR_reg_t loop_var, bool left_creates_bindings,
        MIR_label_t exception_label) {
    if (destr_pattern) {
        bool prev_dstr_assignment = mt->destructure_assignment_mode;
        mt->destructure_assignment_mode = !left_creates_bindings;
        jm_emit_array_destructure(mt, destr_pattern, loop_var);
        mt->destructure_assignment_mode = prev_dstr_assignment;
        if (exception_label) jm_emit_error_lane_guard(mt, exception_label);
    }
    if (obj_destr_pattern) {
        bool prev_dstr_assignment = mt->destructure_assignment_mode;
        mt->destructure_assignment_mode = !left_creates_bindings;
        jm_emit_object_destructure(mt, obj_destr_pattern, loop_var);
        mt->destructure_assignment_mode = prev_dstr_assignment;
        if (exception_label) jm_emit_error_lane_guard(mt, exception_label);
    }
}

static void jm_emit_for_of_step_error_lane_check(JsMirTranspiler* mt,
        bool pushed_try) {
    // IteratorStep/IteratorComplete abrupt completion is returned directly;
    // IteratorClose applies to abrupt completion of the loop body, not to an
    // exception raised while asking the iterator for its next result.
    if (pushed_try) mt->try_ctx_depth--;
    jm_emit_error_lane_propagate_check(mt);
    if (pushed_try) mt->try_ctx_depth++;
}

static bool jm_has_outer_block_func_binding(JsMirTranspiler* mt, const char* name) {
    if (!mt || !name) return false;
    for (int depth = 2; depth < mt->scope_depth; depth++) {
        JsMirVarEntry* var = jm_find_var_in_scope_depth(mt, name, depth);
        if (var && var->from_block_func_decl) return true;
    }
    return false;
}

static JsMirVarEntry* jm_find_enclosing_var_env_binding(JsMirTranspiler* mt, const char* name) {
    if (!mt || !name) return NULL;
    int start_depth = mt->scope_depth - 1;
    if (start_depth >= 64) start_depth = 63;
    for (int depth = start_depth; depth >= 0; depth--) {
        JsMirVarEntry* var = jm_find_var_in_scope_depth(mt, name, depth);
        if (!var) continue;
        if (var->is_let_const || var->from_block_func_decl || var->from_catch_param) continue;
        return var;
    }
    return NULL;
}

static bool jm_statement_function_decl_is_direct_binding(JsFunctionNode* fn) {
    if (!fn) return false;
    TSNode fn_node = fn->node;
    if (ts_node_is_null(fn_node)) return false;
    TSNode parent = ts_node_parent(fn_node);
    if (ts_node_is_null(parent)) return false;
    const char* parent_type = ts_node_type(parent);
    if (parent_type && strcmp(parent_type, "program") == 0) return true;
    if (!parent_type || strcmp(parent_type, "statement_block") != 0) return false;
    TSNode grandparent = ts_node_parent(parent);
    if (ts_node_is_null(grandparent)) return false;
    const char* grandparent_type = ts_node_type(grandparent);
    if (!grandparent_type) return false;
    bool function_body_parent = strcmp(grandparent_type, "function_declaration") == 0 ||
        strcmp(grandparent_type, "function_expression") == 0 ||
        strcmp(grandparent_type, "generator_function_declaration") == 0 ||
        strcmp(grandparent_type, "generator_function") == 0 ||
        strcmp(grandparent_type, "arrow_function") == 0;
    if (!function_body_parent) return false;
    if (strcmp(grandparent_type, "arrow_function") == 0) {
        // Tree-sitter does not expose arrow block bodies through the same body
        // field shape; direct arrow-body declarations still create local names.
        return true;
    }
    TSNode body = ts_node_child_by_field_name(grandparent, "body", 4);
    return !ts_node_is_null(body) &&
        ts_node_start_byte(body) == ts_node_start_byte(parent) &&
        ts_node_end_byte(body) == ts_node_end_byte(parent);
}

static bool jm_current_function_has_direct_body_function_binding(JsFunctionNode* fn, const char* vname) {
    if (!fn || !vname || !fn->body ||
        fn->body->node_type != JS_AST_NODE_BLOCK_STATEMENT) {
        return false;
    }
    JsBlockNode* body = (JsBlockNode*)fn->body;
    for (JsAstNode* stmt = body->statements; stmt; stmt = stmt->next) {
        if (stmt->node_type != JS_AST_NODE_FUNCTION_DECLARATION) continue;
        JsFunctionNode* decl = (JsFunctionNode*)stmt;
        if (!decl->name) continue;
        const char* name = jm_format_name("_js_%.*s", (int)decl->name->len, decl->name->chars);
        if (strcmp(name, vname) == 0) return true;
    }
    return false;
}

static bool jm_assignment_targets_name(JsAstNode* left, const char* bare_name, int bare_len) {
    if (!left || !bare_name || bare_len <= 0) return false;
    // Pattern assignments rebind their leaf identifiers; missing those leaves
    // lets boxed destructured Items flow into native numeric MIR registers.
    switch (left->node_type) {
    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)left;
        return id->name && id->name->len == (size_t)bare_len &&
            strncmp(id->name->chars, bare_name, bare_len) == 0;
    }
    case JS_AST_NODE_ARRAY_PATTERN:
    case JS_AST_NODE_ARRAY_EXPRESSION:
        for (JsAstNode* item = ((JsArrayNode*)left)->elements; item; item = item->next) {
            if (jm_assignment_targets_name(item, bare_name, bare_len)) return true;
        }
        return false;
    case JS_AST_NODE_OBJECT_PATTERN:
    case JS_AST_NODE_OBJECT_EXPRESSION:
        for (JsAstNode* prop = ((JsObjectNode*)left)->properties; prop; prop = prop->next) {
            if (jm_assignment_targets_name(prop, bare_name, bare_len)) return true;
        }
        return false;
    case JS_AST_NODE_PROPERTY:
        // Destructuring rebinds the property's value target, never its key.
        return jm_assignment_targets_name(((JsPropertyNode*)left)->value,
            bare_name, bare_len);
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
        return jm_assignment_targets_name(((JsAssignmentPatternNode*)left)->left,
            bare_name, bare_len);
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
    case JS_AST_NODE_SPREAD_ELEMENT:
        return jm_assignment_targets_name(((JsSpreadElementNode*)left)->argument,
            bare_name, bare_len);
    default:
        return false;
    }
}

static bool jm_scope_env_name_matches_binding_in_statement(const char* scope_name,
        const char* name, JsAstNode* binding_node) {
    if (!scope_name || !name) return false;
    if (strcmp(scope_name, name) == 0) return true;
    const char* at = strchr(scope_name, '@');
    if (!at) return false;
    size_t base_len = (size_t)(at - scope_name);
    if (strlen(name) != base_len || strncmp(scope_name, name, base_len) != 0) return false;
    if (!binding_node || ts_node_is_null(binding_node->node)) return false;
    const char* key = jm_format_name("%s@%u:%u", name,
        ts_node_start_byte(binding_node->node), ts_node_end_byte(binding_node->node));
    return strcmp(scope_name, key) == 0;
}

static void jm_scope_env_mark_pattern_bindings(JsMirTranspiler* mt, JsAstNode* pat) {
    if (!mt || !pat) return;
    switch (pat->node_type) {
    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)pat;
        if (!id->name) return;
        const char* vname = jm_format_name("_js_%.*s", (int)id->name->len, id->name->chars);
        JsMirVarEntry* ve = jm_find_var(mt, vname);
        if (ve) {
            jm_scope_env_mark_and_writeback_binding(mt, vname, pat, ve->reg, ve->type_id);
        }
        return;
    }
    case JS_AST_NODE_ARRAY_PATTERN:
    case JS_AST_NODE_ARRAY_EXPRESSION: {
        for (JsAstNode* e = ((JsArrayNode*)pat)->elements; e; e = e->next) {
            jm_scope_env_mark_pattern_bindings(mt, e);
        }
        return;
    }
    case JS_AST_NODE_OBJECT_PATTERN:
    case JS_AST_NODE_OBJECT_EXPRESSION: {
        for (JsAstNode* p = ((JsObjectNode*)pat)->properties; p; p = p->next) {
            jm_scope_env_mark_pattern_bindings(mt, p);
        }
        return;
    }
    case JS_AST_NODE_PROPERTY:
        jm_scope_env_mark_pattern_bindings(mt, ((JsPropertyNode*)pat)->value);
        return;
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
        jm_scope_env_mark_pattern_bindings(mt, ((JsAssignmentPatternNode*)pat)->left);
        return;
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
    case JS_AST_NODE_SPREAD_ELEMENT:
        jm_scope_env_mark_pattern_bindings(mt, ((JsSpreadElementNode*)pat)->argument);
        return;
    default:
        return;
    }
}

static bool jm_mutable_native_var_needs_boxing_walk(JsMirTranspiler* mt,
        JsAstNode* node, const char* bare_name, int bare_len, TypeId native_type) {
    if (!node || !bare_name || bare_len <= 0) return false;

    switch (node->node_type) {
    case JS_AST_NODE_FUNCTION_DECLARATION:
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
    case JS_AST_NODE_CLASS_DECLARATION:
    case JS_AST_NODE_CLASS_EXPRESSION:
        return false;

    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* asgn = (JsAssignmentNode*)node;
        if (jm_assignment_targets_name(asgn->left, bare_name, bare_len)) {
            TypeId rhs_type = jm_get_effective_type(mt, asgn->right);
            if (asgn->op == JS_OP_ASSIGN) {
                return rhs_type != native_type;
            }
            return rhs_type != native_type && rhs_type != LMD_TYPE_ANY;
        }
        return jm_mutable_native_var_needs_boxing_walk(mt, asgn->left, bare_name, bare_len, native_type) ||
            jm_mutable_native_var_needs_boxing_walk(mt, asgn->right, bare_name, bare_len, native_type);
    }

    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)node;
        for (JsAstNode* d = vd->declarations; d; d = d->next) {
            if (jm_mutable_native_var_needs_boxing_walk(mt, d, bare_name, bare_len, native_type)) return true;
        }
        return false;
    }

    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* vd = (JsVariableDeclaratorNode*)node;
        if (vd->id && vd->id->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)vd->id;
            if (id->name && id->name->len == (size_t)bare_len &&
                    strncmp(id->name->chars, bare_name, bare_len) == 0) {
                return false;
            }
        }
        return jm_mutable_native_var_needs_boxing_walk(mt, vd->init, bare_name, bare_len, native_type);
    }

    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* es = (JsExpressionStatementNode*)node;
        return jm_mutable_native_var_needs_boxing_walk(mt, es->expression, bare_name, bare_len, native_type);
    }

    case JS_AST_NODE_SEQUENCE_EXPRESSION: {
        JsSequenceNode* seq = (JsSequenceNode*)node;
        for (JsAstNode* expr = seq->expressions; expr; expr = expr->next) {
            if (jm_mutable_native_var_needs_boxing_walk(
                    mt, expr, bare_name, bare_len, native_type)) return true;
        }
        return false;
    }

    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* binary = (JsBinaryNode*)node;
        return jm_mutable_native_var_needs_boxing_walk(
                   mt, binary->left, bare_name, bare_len, native_type) ||
            jm_mutable_native_var_needs_boxing_walk(
                   mt, binary->right, bare_name, bare_len, native_type);
    }

    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* conditional = (JsConditionalNode*)node;
        return jm_mutable_native_var_needs_boxing_walk(
                   mt, conditional->test, bare_name, bare_len, native_type) ||
            jm_mutable_native_var_needs_boxing_walk(
                   mt, conditional->consequent, bare_name, bare_len, native_type) ||
            jm_mutable_native_var_needs_boxing_walk(
                   mt, conditional->alternate, bare_name, bare_len, native_type);
    }

    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        for (JsAstNode* s = blk->statements; s; s = s->next) {
            if (jm_mutable_native_var_needs_boxing_walk(mt, s, bare_name, bare_len, native_type)) return true;
        }
        return false;
    }

    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* ifn = (JsIfNode*)node;
        return jm_mutable_native_var_needs_boxing_walk(mt, ifn->test, bare_name, bare_len, native_type) ||
            jm_mutable_native_var_needs_boxing_walk(mt, ifn->consequent, bare_name, bare_len, native_type) ||
            jm_mutable_native_var_needs_boxing_walk(mt, ifn->alternate, bare_name, bare_len, native_type);
    }

    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* wn = (JsWhileNode*)node;
        return jm_mutable_native_var_needs_boxing_walk(mt, wn->test, bare_name, bare_len, native_type) ||
            jm_mutable_native_var_needs_boxing_walk(mt, wn->body, bare_name, bare_len, native_type);
    }

    case JS_AST_NODE_DO_WHILE_STATEMENT: {
        JsDoWhileNode* dn = (JsDoWhileNode*)node;
        return jm_mutable_native_var_needs_boxing_walk(mt, dn->body, bare_name, bare_len, native_type) ||
            jm_mutable_native_var_needs_boxing_walk(mt, dn->test, bare_name, bare_len, native_type);
    }

    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* fn = (JsForNode*)node;
        return jm_mutable_native_var_needs_boxing_walk(mt, fn->init, bare_name, bare_len, native_type) ||
            jm_mutable_native_var_needs_boxing_walk(mt, fn->test, bare_name, bare_len, native_type) ||
            jm_mutable_native_var_needs_boxing_walk(mt, fn->update, bare_name, bare_len, native_type) ||
            jm_mutable_native_var_needs_boxing_walk(mt, fn->body, bare_name, bare_len, native_type);
    }

    case JS_AST_NODE_FOR_IN_STATEMENT:
    case JS_AST_NODE_FOR_OF_STATEMENT: {
        JsForOfNode* fo = (JsForOfNode*)node;
        return jm_mutable_native_var_needs_boxing_walk(mt, fo->left, bare_name, bare_len, native_type) ||
            jm_mutable_native_var_needs_boxing_walk(mt, fo->right, bare_name, bare_len, native_type) ||
            jm_mutable_native_var_needs_boxing_walk(mt, fo->body, bare_name, bare_len, native_type);
    }

    default:
        return false;
    }
}

static bool jm_mutable_native_var_needs_boxing(JsMirTranspiler* mt,
        JsVariableDeclarationNode* decl, JsIdentifierNode* id, TypeId native_type) {
    if (!mt || !decl || !id || !id->name) return false;
    if (decl->kind == JS_VAR_CONST) return false;
    if (!jm_is_native_type(native_type)) return false;
    if (!mt->current_fc || !mt->current_fc->node || !mt->current_fc->node->body) return false;
    return jm_mutable_native_var_needs_boxing_walk(mt, mt->current_fc->node->body,
        id->name->chars, (int)id->name->len, native_type);
}

static void jm_define_global_var_property_for_main_var(JsMirTranspiler* mt,
        JsVariableDeclarationNode* decl, JsIdentifierNode* id, MIR_reg_t value) {
    if (!mt || !decl || !id || !id->name || !value) return;
    if (decl->kind != JS_VAR_VAR || !mt->in_main || mt->is_module || mt->is_eval_direct) return;
    MIR_reg_t key_reg = jm_box_property_name_literal(mt,
        id->name->chars, id->name->len);
    jm_call_void_3(mt, "js_define_global_property_v",
        MIR_T_I64, MIR_new_int_op(mt->ctx, 0),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, value));
    jm_call_2(mt, "js_set_global_var_property_fast", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, value));
    jm_emit_error_lane_propagate_check(mt);
}

static void jm_declare_evalscript_global_lexical_if_needed(JsMirTranspiler* mt,
        JsVariableDeclarationNode* decl, JsIdentifierNode* id, MIR_reg_t boxed_value) {
    if (!mt || !decl || !id || !id->name || !boxed_value) return;
    if (!mt->is_eval_direct || (decl->kind != JS_VAR_LET && decl->kind != JS_VAR_CONST)) return;
    MIR_reg_t evalscript_active = jm_call_0(mt, "js_262_eval_script_is_active", MIR_T_I64);
    MIR_label_t skip_global_lex = jm_new_label(mt);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
        MIR_new_label_op(mt->ctx, skip_global_lex),
        MIR_new_reg_op(mt->ctx, evalscript_active)));
    MIR_reg_t key_reg = jm_box_property_name_literal(mt,
        id->name->chars, id->name->len);
    // evalScript uses Script global lexical bindings. They persist for later
    // identifier resolution but are not properties of globalThis.
    jm_call_void_3(mt, "js_global_lexical_declare",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_value),
        MIR_T_I64, MIR_new_int_op(mt->ctx, decl->kind == JS_VAR_CONST ? 1 : 0));
    jm_emit_label(mt, skip_global_lex);
}

static bool jm_can_skip_plain_top_level_var_decl_without_init(
        JsMirTranspiler* mt, JsVariableDeclarationNode* var) {
    if (!mt || !var || var->kind != JS_VAR_VAR || !mt->in_main ||
            mt->is_eval_direct || !mt->module_consts) {
        return false;
    }
    bool at_module_var_scope = (mt->scope_depth <= 1) || (mt->var_hoist_depth <= 1);
    if (!at_module_var_scope) return false;
    JsAstNode* decl = var->declarations;
    if (!decl) return false;
    while (decl) {
        if (decl->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) return false;
        JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)decl;
        if (d->init || !d->id || d->id->node_type != JS_AST_NODE_IDENTIFIER) return false;
        JsIdentifierNode* id = (JsIdentifierNode*)d->id;
        const char* vname = jm_format_name("_js_%.*s", (int)id->name->len, id->name->chars);
        JsModuleConstEntry lookup;
        memset(&lookup, 0, sizeof(lookup));
        lookup.name = jm_persist_name(vname);
        JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup);
        if (!mc || mc->const_type != MCONST_MODVAR || (int)mc->int_val < 0) return false;
        decl = decl->next;
    }
    return true;
}

static void jm_writeback_pattern_bindings(JsMirTranspiler* mt,
                                          JsVariableDeclarationNode* var,
                                          JsAstNode* pattern) {
    jm_scope_env_mark_pattern_bindings(mt, pattern);
    if (mt->scope_env_reg != 0 && mt->current_fc && mt->current_fc->has_scope_env) {
        JsFuncCollected* se_fc = mt->current_fc;
        struct hashmap* se_names = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        jm_collect_pattern_names(pattern, se_names);
        size_t si = 0; void* sitem;
        while (hashmap_iter(se_names, &si, &sitem)) {
            JsNameSetEntry* ne = (JsNameSetEntry*)sitem;
            for (int se_s = 0; se_s < se_fc->scope_env_count; se_s++) {
                if (strcmp(ne->name, se_fc->scope_env_names[se_s]) == 0) {
                    JsMirVarEntry* ve = jm_find_var(mt, ne->name);
                    if (ve) {
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_mem_op(mt->ctx, MIR_T_I64,
                                se_s * (int)sizeof(uint64_t), mt->scope_env_reg, 0, 1),
                            MIR_new_reg_op(mt->ctx, ve->reg)));
                    }
                    break;
                }
            }
        }
        hashmap_free(se_names);
    }

    bool pattern_at_top_for_writeback = (mt->scope_depth <= 1) ||
        (var->kind == JS_VAR_VAR && mt->var_hoist_depth <= 1);
    if (pattern_at_top_for_writeback &&
        (mt->in_main || (mt->current_fc && mt->current_fc->is_iife_body)) && mt->module_consts) {
        struct hashmap* pat_names = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        jm_collect_pattern_names(pattern, pat_names);
        size_t piter = 0; void* pitem;
        while (hashmap_iter(pat_names, &piter, &pitem)) {
            JsNameSetEntry* ne = (JsNameSetEntry*)pitem;
            JsModuleConstEntry mlookup;
            mlookup.name = jm_persist_name(ne->name);
            JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mlookup);
            bool in_modvar_scope = mt->in_main ||
                (mc && mc->is_iife_var && mt->current_fc && mt->current_fc->is_iife_body);
            if (in_modvar_scope && mc && mc->const_type == MCONST_MODVAR) {
                JsMirVarEntry* ve = jm_find_var(mt, ne->name);
                if (ve) {
                    jm_call_void_2(mt, "js_set_module_var",
                        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mc->int_val),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, ve->reg));
                }
            }
        }
        hashmap_free(pat_names);
    }
}

void jm_transpile_var_decl(JsMirTranspiler* mt, JsVariableDeclarationNode* var) {
    // JS spec: 'var' is function-scoped. Redirect variable creation to scope 1
    // (the function body scope after jm_push_scope) so vars survive after block scopes pop.
    int saved_hoist = mt->var_hoist_depth;
    if (var->kind == JS_VAR_VAR && mt->scope_depth > 1 && mt->var_hoist_depth < 0) {
        mt->var_hoist_depth = 1;
    } else if (var->kind == JS_VAR_LET || var->kind == JS_VAR_CONST) {
        // Lexical declarations always bind in the current scope. Inheriting a
        // surrounding function's var-hoist target loses a shadowing for-loop
        // binding and makes its references fall through to the global object.
        mt->var_hoist_depth = -1;
    }
    if (jm_can_skip_plain_top_level_var_decl_without_init(mt, var)) {
        mt->var_hoist_depth = saved_hoist;
        return;
    }
    JsAstNode* decl = var->declarations;
    while (decl) {
        if (decl->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
            JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)decl;
            if (d->id && d->id->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* id = (JsIdentifierNode*)d->id;
                const char* vname = jm_format_name("_js_%.*s", (int)id->name->len, id->name->chars);

                JsMirVarEntry* catch_param_var = NULL;
                if (var->kind == JS_VAR_VAR && d->init && mt->scope_depth >= 0) {
                    // catch parameters live in their own environment, with the
                    // catch body as a nested block. Sloppy Annex B var writes
                    // still update the nearest catch parameter binding.
                    catch_param_var = jm_find_nearest_catch_param_var(mt, vname);
                    if (catch_param_var && catch_param_var->from_catch_param && catch_param_var->reg) {
                        MIR_reg_t boxed_val = jm_transpile_box_item(mt, d->init);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, catch_param_var->reg),
                            MIR_new_reg_op(mt->ctx, boxed_val)));
                        jm_scope_env_mark_and_writeback(mt, vname, catch_param_var->reg);
                        decl = decl->next;
                        continue;
                    }
                }

                // For mutable (let/var) module vars in __main__, do NOT create a local variable.
                // All access goes through js_get/set_module_var so functions can share state.
                // const module vars keep their locals since they are never mutated by functions.
                //
                // BUT: only the TOP-LEVEL declaration is the module var. Nested let/const
                // declarations (inside a block, for-init, etc.) shadow the outer name and
                // must be local — otherwise `{ let x = ... }` would clobber the module-level
                // `let x = ...`. Top-level main scope is scope_depth == 1 (after the entry
                // jm_push_scope). For 'var', function-scoping means scope_depth > 1
                // declarations are still hoisted to scope 1 (handled separately below), so
                // the modvar path is still appropriate when var is hoisted to top.
                bool is_modvar = false;
                int modvar_index = -1;
                if (mt->module_consts && var->kind != JS_VAR_CONST) {
                    JsModuleConstEntry mclookup;
                    mclookup.name = jm_persist_name(vname);
                    JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                    bool at_top = (mt->scope_depth <= 1) ||
                        (var->kind == JS_VAR_VAR && mt->var_hoist_depth <= 1);
                    bool local_var_hoist = (var->kind == JS_VAR_VAR && mt->var_hoist_depth > 1);
                    if (mc && mc->const_type == MCONST_MODVAR && at_top && !local_var_hoist &&
                        (mt->in_main || (mc->is_iife_var && mt->current_fc && mt->current_fc->is_iife_body))) {
                        is_modvar = true;
                        modvar_index = (int)mc->int_val;
                    }
                    if (is_modvar && mc->is_iife_var) {
                        JsMirVarEntry* direct_binding = jm_find_var(mt, vname);
                        if (direct_binding) {
                            // A later `for (let name ...)` is a distinct binding;
                            // only this direct IIFE binding may reload from the
                            // promoted module slot.
                            direct_binding->is_iife_module_var_binding = true;
                        }
                    }
                }

                bool with_var_init_handled = false;
                if (var->kind == JS_VAR_VAR && d->init && mt->with_depth > 0) {
                    MIR_reg_t key_reg = jm_box_property_name_literal(mt,
                        id->name->chars, id->name->len);
                    MIR_reg_t has_with_item = jm_call_1(mt, "js_capture_with_binding", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg));
                    jm_emit_error_lane_propagate_check(mt);
                    MIR_reg_t has_with = jm_emit_is_truthy(mt, has_with_item, NULL);
                    MIR_reg_t with_base = jm_call_1(mt, "js_get_last_with_binding_base_or_undefined", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg));
                    const char* saved_assign_target = mt->assign_target_vname;
                    mt->assign_target_vname = vname;
                    // Resolve the object-environment binding before the initializer.
                    // The initializer may delete or replace the property, but the
                    // assignment target remains the pre-resolved with base.
                    MIR_reg_t boxed_val = jm_transpile_box_item(mt, d->init);
                    mt->assign_target_vname = saved_assign_target;
                    MIR_label_t normal_init = jm_new_label(mt);
                    MIR_label_t init_done = jm_new_label(mt);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
                        MIR_new_label_op(mt->ctx, normal_init),
                        MIR_new_reg_op(mt->ctx, has_with)));
                    jm_call_4(mt, "js_set_with_binding_base", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, with_base),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
                    jm_emit_error_lane_propagate_check(mt);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, init_done)));
                    jm_emit_label(mt, normal_init);
                    if (is_modvar) {
                        jm_call_void_2(mt, "js_set_module_var",
                            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)modvar_index),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
                        JsMirVarEntry* existing_modvar_local = jm_find_var(mt, vname);
                        if (existing_modvar_local && existing_modvar_local->reg &&
                            existing_modvar_local->mir_type == MIR_T_I64) {
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, existing_modvar_local->reg),
                                MIR_new_reg_op(mt->ctx, boxed_val)));
                        }
                        if (mt->in_main && !mt->is_eval_direct) {
                            // direct eval exports vars after executing the snippet so
                            // caller-local eval frames do not leak initializer writes.
                            jm_call_void_3(mt, "js_define_global_property_v",
                                MIR_T_I64, MIR_new_int_op(mt->ctx, 0),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
                            jm_call_2(mt, "js_set_global_var_property_fast", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
                            jm_emit_error_lane_propagate_check(mt);
                        }
                        jm_scope_env_mark_and_writeback(mt, vname, boxed_val);
                    } else {
                        JsMirVarEntry* existing_var = jm_find_var(mt, vname);
                        if (existing_var && existing_var->reg && existing_var->from_hoist) {
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, existing_var->reg),
                                MIR_new_reg_op(mt->ctx, boxed_val)));
                            jm_write_env_backing_if_needed(mt, existing_var, boxed_val, LMD_TYPE_ANY);
                            jm_scope_env_mark_and_writeback(mt, vname, existing_var->reg);
                            jm_define_global_var_property_for_main_var(mt, var, id, boxed_val);
                        } else {
                            MIR_reg_t reg = jm_new_reg(mt, vname, MIR_T_I64);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, reg),
                                MIR_new_reg_op(mt->ctx, boxed_val)));
                            jm_set_var(mt, vname, reg, MIR_T_I64, LMD_TYPE_ANY);
                            jm_write_env_backing_if_needed(mt, jm_find_var(mt, vname), reg, LMD_TYPE_ANY);
                            jm_scope_env_mark_and_writeback(mt, vname, reg);
                            jm_define_global_var_property_for_main_var(mt, var, id, boxed_val);
                        }
                    }
                    if (d->init->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
                        d->init->node_type == JS_AST_NODE_ARROW_FUNCTION) {
                        JsFunctionNode* fn_node = (JsFunctionNode*)d->init;
                        if (!fn_node->name && id->name) {
                            jm_emit_set_function_name(mt, boxed_val, id->name->chars);
                        }
                    }
                    jm_emit_label(mt, init_done);
                    with_var_init_handled = true;
                }

                if (!with_var_init_handled && is_modvar) {
                    // Module var: evaluate init and store directly to module var table.
                    // var redeclaration without initializer (e.g. `var x;` when x already exists)
                    // is a no-op in JS — do NOT reset to undefined.
                    if (d->init) {
                        const char* saved_assign_target = mt->assign_target_vname;
                        mt->assign_target_vname = vname;
                        MIR_reg_t boxed_val = jm_transpile_box_item(mt, d->init);
                        mt->assign_target_vname = saved_assign_target;
                        jm_call_void_2(mt, "js_set_module_var",
                            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)modvar_index),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
                        JsMirVarEntry* existing_modvar_local = jm_find_var(mt, vname);
                        if (existing_modvar_local && existing_modvar_local->reg &&
                            existing_modvar_local->mir_type == MIR_T_I64) {
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, existing_modvar_local->reg),
                                MIR_new_reg_op(mt->ctx, boxed_val)));
                        }
                        if (var->kind == JS_VAR_VAR && mt->in_main && !mt->is_eval_direct) {
                            // direct eval var bindings are exported by the eval epilogue;
                            // eager global writes break function-local eval scoping.
                            MIR_reg_t key_reg = jm_box_property_name_literal(mt,
                                id->name->chars, id->name->len);
                            jm_call_void_3(mt, "js_define_global_property_v",
                                MIR_T_I64, MIR_new_int_op(mt->ctx, 0),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
                            jm_call_2(mt, "js_set_global_var_property_fast", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
                            jm_emit_error_lane_propagate_check(mt);
                        }
                        if (mt->is_eval_direct && (var->kind == JS_VAR_LET || var->kind == JS_VAR_CONST)) {
                            MIR_reg_t eval_env_active = jm_call_0(mt, "js_262_eval_script_is_active", MIR_T_I64);
                            MIR_label_t skip_global_lex = jm_new_label(mt);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
                                MIR_new_label_op(mt->ctx, skip_global_lex),
                                MIR_new_reg_op(mt->ctx, eval_env_active)));
                            MIR_reg_t key_reg = jm_box_property_name_literal(mt,
                                id->name->chars, id->name->len);
                            // $262.evalScript creates Script global lexical
                            // bindings: they are visible to identifiers but
                            // intentionally not own properties of globalThis.
                            jm_call_void_3(mt, "js_global_lexical_declare",
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val),
                                MIR_T_I64, MIR_new_int_op(mt->ctx, var->kind == JS_VAR_CONST ? 1 : 0));
                            jm_emit_label(mt, skip_global_lex);
                        }
                        // v18: function name inference for module-level vars
                        if (d->init->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
                            d->init->node_type == JS_AST_NODE_ARROW_FUNCTION) {
                            JsFunctionNode* fn_node = (JsFunctionNode*)d->init;
                            if (!fn_node->name && id->name) {
                                jm_emit_set_function_name(mt, boxed_val, id->name->chars);
                            }
                        }
                        // Write back to scope env if this var is captured by child closures
                        jm_scope_env_mark_and_writeback(mt, vname, boxed_val);
                        // P7: detect new ClassName(...) and record class_entry in module_consts
                        if (d->init->node_type == JS_AST_NODE_NEW_EXPRESSION && mt->module_consts) {
                            JsCallNode* p7_nc = (JsCallNode*)d->init;
                            if (p7_nc->callee && p7_nc->callee->node_type == JS_AST_NODE_IDENTIFIER) {
                                JsIdentifierNode* p7_ctor = (JsIdentifierNode*)p7_nc->callee;
                                JsClassEntry* p7_ce = jm_find_class(mt, p7_ctor->name->chars, (int)p7_ctor->name->len);
                                if (p7_ce && p7_ce->constructor && p7_ce->constructor->fc &&
                                    p7_ce->constructor->fc->ctor_prop_count > 0) {
                                    JsModuleConstEntry p7_lookup;
                                    memset(&p7_lookup, 0, sizeof(p7_lookup));
                                    p7_lookup.name = jm_persist_name(vname);
                                    JsModuleConstEntry* p7_mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &p7_lookup);
                                    if (p7_mc) {
                                        p7_mc->class_entry = p7_ce;
                                        log_debug("P7: modvar '%s' is instance of '%.*s' — class_entry recorded",
                                                  vname, (int)p7_ctor->name->len, p7_ctor->name->chars);
                                    }
                                }
                            }
                        }
                    } else if (var->kind == JS_VAR_LET || var->kind == JS_VAR_CONST) {
                        // let/const without initializer: set to undefined (exits TDZ)
                        MIR_reg_t undef_reg = jm_new_reg(mt, "undef_init", MIR_T_I64);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, undef_reg),
                            MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED)));
                        jm_call_void_2(mt, "js_set_module_var",
                            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)modvar_index),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, undef_reg));
                        if (mt->is_eval_direct) {
                            MIR_reg_t eval_env_active = jm_call_0(mt, "js_262_eval_script_is_active", MIR_T_I64);
                            MIR_label_t skip_global_lex = jm_new_label(mt);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
                                MIR_new_label_op(mt->ctx, skip_global_lex),
                                MIR_new_reg_op(mt->ctx, eval_env_active)));
                            MIR_reg_t key_reg = jm_box_property_name_literal(mt,
                                id->name->chars, id->name->len);
                            // $262.evalScript creates Script global lexical
                            // bindings without adding global object properties.
                            jm_call_void_3(mt, "js_global_lexical_declare",
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, undef_reg),
                                MIR_T_I64, MIR_new_int_op(mt->ctx, var->kind == JS_VAR_CONST ? 1 : 0));
                            jm_emit_label(mt, skip_global_lex);
                        }
                    } else {
                        // var redeclaration without init: no-op (don't reset to undefined)
                    }
                } else if (!with_var_init_handled && d->init) {
                    log_debug("var-decl: '%s' init node_type=%d", vname, d->init->node_type);

                    // v50: For 'var' redeclarations (variable already exists from hoisting),
                    // reuse the existing register. Creating a new register would break
                    // references compiled before this point (e.g. reads in a while-loop
                    // condition that precede the var declaration inside an if body).
                    bool var_reused = false;
                    if (var->kind == JS_VAR_VAR) {
                        JsMirVarEntry* existing_var = jm_find_var(mt, vname);
                        if (existing_var && existing_var->reg && existing_var->from_hoist) {
                            const char* saved_assign_target = mt->assign_target_vname;
                            // Hoisted initializers still define this binding; anonymous class
                            // expressions use the target name to recover their class metadata.
                            mt->assign_target_vname = vname;
                            MIR_reg_t val = jm_transpile_box_item(mt, d->init);
                            mt->assign_target_vname = saved_assign_target;
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, existing_var->reg),
                                MIR_new_reg_op(mt->ctx, val)));
                            jm_write_env_backing_if_needed(mt, existing_var, val, LMD_TYPE_ANY);
                            // Hoisted `var` initializers may shadow same-named
                            // outer helpers; update the slot for this declarator.
                            jm_scope_env_mark_and_writeback_binding(mt, vname, d->id,
                                existing_var->reg);
                            jm_define_global_var_property_for_main_var(mt, var, id, val);
                            // v18: function name inference for anonymous function expressions
                            if (d->init->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
                                d->init->node_type == JS_AST_NODE_ARROW_FUNCTION) {
                                JsFunctionNode* fn_node = (JsFunctionNode*)d->init;
                                if (!fn_node->name && id->name) {
                                    jm_emit_set_function_name(mt, val, id->name->chars);
                                }
                            }
                            var_reused = true;
                        }
                    }

                    if (!var_reused) {

                    TypeId init_type = jm_get_effective_type(mt, d->init);

                    // Phase 3.4: override with TS type annotation if present
                    if (d->ts_type && d->ts_type->type_expr &&
                        d->ts_type->type_expr->node_type == (int)TS_AST_NODE_PREDEFINED_TYPE) {
                        TsPredefinedTypeNode* pt = (TsPredefinedTypeNode*)d->ts_type->type_expr;
                        TypeId ann_type = pt->predefined_id;
                        if (ann_type == LMD_TYPE_FLOAT || ann_type == LMD_TYPE_INT ||
                            ann_type == LMD_TYPE_STRING || ann_type == LMD_TYPE_BOOL) {
                            log_debug("var-decl P3.4: '%s' annotation type overrides inference", vname);
                            init_type = ann_type;
                        }
                    }

                    TypeId orig_type = init_type;

                    // v15: In generators, force boxed types for consistent env save/load
                    if (mt->in_generator) {
                        init_type = LMD_TYPE_ANY;
                    }

                    // v24: Scope-env captured vars must stay boxed (ANY) because a child
                    // closure can assign any type to them. If we keep a native type
                    // (e.g. FLOAT from `-Infinity` init), the scope_env reload after a
                    // call will misinterpret the boxed value written by the closure.
                    if (jm_is_native_type(init_type) && !mt->in_generator) {
                        // Js57 Track A: current_fc covers both the function-body case
                        // and js_main when the module-level scope env is active.
                        JsFuncCollected* fc = mt->current_fc;
                        if (fc && fc->has_scope_env && fc->scope_env_names) {
                            for (int s = 0; s < fc->scope_env_count; s++) {
                                if (jm_scope_env_name_matches_binding_in_statement(
                                        fc->scope_env_names[s], vname, d->id)) {
                                    log_debug("v24: widening scope-env var '%s' from %d to ANY", vname, init_type);
                                    init_type = LMD_TYPE_ANY;
                                    break;
                                }
                            }
                        }
                    }

                    // P9: Widen INT to FLOAT if pre-scan detected float usage
                    if (init_type == LMD_TYPE_INT && jm_should_widen_to_float(mt, vname)) {
                        init_type = LMD_TYPE_FLOAT;
                        log_debug("P9: widening var '%s' from INT to FLOAT", vname);
                    }

                    if (jm_mutable_native_var_needs_boxing(mt, var, id, init_type)) {
                        log_debug("P9: boxing mutable native var '%s' because later assignments are not native", vname);
                        init_type = LMD_TYPE_ANY;
                    }

                    // Stage 4C: a user function's numeric (INT/FLOAT) return type is a
                    // SPECULATIVE inference, not a language guarantee — the callee may
                    // return a non-number in some branch, and the return-type inference
                    // itself can be unsound (it inferred numeric for functions that
                    // actually return objects, e.g. the editor's stepMap chain). The
                    // native var path below unboxes the initializer with an UNCHECKED
                    // it2d/d2i, which silently collapses a boxed object to 0. So keep a
                    // call-initialized binding boxed unless its callee identity is proven.
                    // Property spelling (including Math.*) is mutable and cannot prove
                    // a numeric result under D6.2.2v2.
                    if (d->init && d->init->node_type == JS_AST_NODE_CALL_EXPRESSION &&
                        (init_type == LMD_TYPE_INT || init_type == LMD_TYPE_FLOAT)) {
                        log_debug("Stage 4C: boxing var '%s' — call result numeric type is speculative, not guaranteed", vname);
                        init_type = LMD_TYPE_ANY;
                    }

                    if (init_type == LMD_TYPE_INT) {
                        // native int variable
                        MIR_reg_t reg = jm_new_reg(mt, vname, MIR_T_I64);
                        MIR_reg_t native_val = jm_transpile_as_native(mt, d->init, init_type, LMD_TYPE_INT);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, reg),
                            MIR_new_reg_op(mt->ctx, native_val)));
                        jm_set_var(mt, vname, reg, MIR_T_I64, LMD_TYPE_INT);
                        if (var->kind == JS_VAR_LET || var->kind == JS_VAR_CONST) {
                            JsMirVarEntry* cv = jm_find_var(mt, vname);
                            if (cv) {
                                cv->is_let_const = true;
                                cv->is_const = (var->kind == JS_VAR_CONST);
                                cv->tdz_active = false;
                            }
                        }
                        jm_write_env_backing_if_needed(mt, jm_find_var(mt, vname), reg, LMD_TYPE_INT);
                        jm_scope_env_mark_and_writeback_binding(mt, vname, d->id, reg, LMD_TYPE_INT);
                        jm_write_last_closure_capture_if_matching(mt, vname, reg, LMD_TYPE_INT);
                        if ((var->kind == JS_VAR_LET || var->kind == JS_VAR_CONST) &&
                            mt->is_eval_direct) {
                            // Only direct-eval scripts export top-level lexical
                            // bindings; boxing here otherwise creates a dead Item.
                            MIR_reg_t boxed_reg = jm_box_int_reg(mt, reg);
                            jm_declare_evalscript_global_lexical_if_needed(mt, var, id, boxed_reg);
                        }
                        if (var->kind == JS_VAR_VAR && mt->in_main && !mt->is_module) {
                            MIR_reg_t boxed_reg = jm_box_int_reg(mt, reg);
                            jm_define_global_var_property_for_main_var(mt, var, id, boxed_reg);
                        }
                    } else if (init_type == LMD_TYPE_FLOAT) {
                        // native double variable
                        MIR_reg_t reg = jm_new_reg(mt, vname, MIR_T_D);
                        // Use original type as source so INT→FLOAT conversion happens
                        MIR_reg_t native_val = jm_transpile_as_native(mt, d->init, orig_type, LMD_TYPE_FLOAT);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                            MIR_new_reg_op(mt->ctx, reg),
                            MIR_new_reg_op(mt->ctx, native_val)));
                        jm_set_var(mt, vname, reg, MIR_T_D, LMD_TYPE_FLOAT);
                        if (var->kind == JS_VAR_LET || var->kind == JS_VAR_CONST) {
                            JsMirVarEntry* cv = jm_find_var(mt, vname);
                            if (cv) {
                                cv->is_let_const = true;
                                cv->is_const = (var->kind == JS_VAR_CONST);
                                cv->tdz_active = false;
                            }
                        }
                        jm_write_env_backing_if_needed(mt, jm_find_var(mt, vname), reg, LMD_TYPE_FLOAT);
                        jm_scope_env_mark_and_writeback_binding(mt, vname, d->id, reg, LMD_TYPE_FLOAT);
                        jm_write_last_closure_capture_if_matching(mt, vname, reg, LMD_TYPE_FLOAT);
                        if ((var->kind == JS_VAR_LET || var->kind == JS_VAR_CONST) &&
                            mt->is_eval_direct) {
                            // Preserve native doubles unless direct eval needs
                            // an observable boxed global lexical binding.
                            MIR_reg_t boxed_reg = jm_box_float(mt, reg);
                            jm_declare_evalscript_global_lexical_if_needed(mt, var, id, boxed_reg);
                        }
                        if (var->kind == JS_VAR_VAR && mt->in_main && !mt->is_module) {
                            MIR_reg_t boxed_reg = jm_box_float(mt, reg);
                            jm_define_global_var_property_for_main_var(mt, var, id, boxed_reg);
                        }
                    } else {
                        // boxed (string, object, array, any, etc.)
                        MIR_reg_t reg = jm_new_reg(mt, vname, MIR_T_I64);
                        // Set assignment target hint for closure self-capture detection
                        mt->assign_target_vname = vname;
                        MIR_reg_t val = jm_transpile_box_item(mt, d->init);
                        mt->assign_target_vname = NULL;
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, reg),
                            MIR_new_reg_op(mt->ctx, val)));
                        jm_set_var(mt, vname, reg, MIR_T_I64, init_type);
                        if (var->kind == JS_VAR_LET || var->kind == JS_VAR_CONST) {
                            JsMirVarEntry* cv = jm_find_var(mt, vname);
                            if (cv) {
                                cv->is_let_const = true;
                                cv->is_const = (var->kind == JS_VAR_CONST);
                                cv->tdz_active = false;
                            }
                        }
                        jm_write_env_backing_if_needed(mt, jm_find_var(mt, vname), reg, init_type);
                        jm_scope_env_mark_and_writeback_binding(mt, vname, d->id, reg, init_type);
                        jm_write_last_closure_capture_if_matching(mt, vname, reg, init_type);
                        jm_declare_evalscript_global_lexical_if_needed(mt, var, id, val);
                        jm_define_global_var_property_for_main_var(mt, var, id, val);

                        // v18: function name inference for anonymous function expressions
                        if (d->init && (d->init->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
                                        d->init->node_type == JS_AST_NODE_ARROW_FUNCTION)) {
                            JsFunctionNode* fn_node = (JsFunctionNode*)d->init;
                            if (!fn_node->name && id->name) {
                                jm_emit_set_function_name(mt, val, id->name->chars);
                            }
                        }

                        // Phase 3.4: if annotated with a non-predefined TS type (e.g. interface/type alias),
                        // resolve it and store TypeMap in full_type for member access inference.
                        if (d->ts_type && d->ts_type->type_expr && mt->tp &&
                            d->ts_type->type_expr->node_type != (int)TS_AST_NODE_PREDEFINED_TYPE) {
                            Type* resolved = ts_resolve_type((TsTranspiler*)mt->tp, d->ts_type->type_expr);
                            if (resolved && resolved->type_id == LMD_TYPE_MAP) {
                                JsMirVarEntry* var_entry = jm_find_var(mt, vname);
                                if (var_entry) {
                                    var_entry->full_type = resolved;
                                    log_debug("P3.4: var '%s' full_type=TypeMap (%d fields)", vname,
                                        ((TypeMap*)resolved)->length);
                                }
                            }
                        }

                        // A2: Detect array literals: let x = [...]
                        if (d->init->node_type == JS_AST_NODE_ARRAY_EXPRESSION) {
                            JsMirVarEntry* var_entry = jm_find_var(mt, vname);
                            if (var_entry) {
                                var_entry->is_js_array = true;
                                log_debug("A2: var '%s' is regular JS array (literal)", vname);
                            }
                        }

                        // propagate typed array type from this.prop in class methods
                        if (d->init->node_type == JS_AST_NODE_MEMBER_EXPRESSION && mt->current_class) {
                            JsMemberNode* im = (JsMemberNode*)d->init;
                            if (!im->computed && im->object && im->property &&
                                im->object->node_type == JS_AST_NODE_IDENTIFIER &&
                                im->property->node_type == JS_AST_NODE_IDENTIFIER) {
                                JsIdentifierNode* obj_id = (JsIdentifierNode*)im->object;
                                if (obj_id->name->len == 4 && strncmp(obj_id->name->chars, "this", 4) == 0) {
                                    JsIdentifierNode* prop_id = (JsIdentifierNode*)im->property;
                                    int ta_type = jm_class_field_ta_type(mt->current_class,
                                        prop_id->name->chars, (int)prop_id->name->len);
                                    if (ta_type >= 0) {
                                        JsMirVarEntry* var_entry = jm_find_var(mt, vname);
                                        if (var_entry) {
                                            var_entry->typed_array_type = ta_type;
                                            log_debug("P9b: var '%s' is typed array type %d (from this.%.*s)",
                                                      vname, ta_type, (int)prop_id->name->len, prop_id->name->chars);
                                        }
                                    }
                                }
                            }
                        }

                        }
                    } // end if (!var_reused)
                } else if (!with_var_init_handled) {
                    // No initializer. For `var` redeclarations, this is a no-op.
                    // For `let`/`const`, this initializes to undefined (exits TDZ).
                    bool skip_init = (var->kind == JS_VAR_VAR) && jm_find_var(mt, vname);
                    if (!skip_init) {
                        MIR_reg_t reg = jm_new_reg(mt, vname, MIR_T_I64);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, reg),
                            MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED)));
                        jm_set_var(mt, vname, reg);
                        if (var->kind == JS_VAR_LET || var->kind == JS_VAR_CONST) {
                            JsMirVarEntry* cv = jm_find_var(mt, vname);
                            if (cv) {
                                cv->is_let_const = true;
                                cv->is_const = (var->kind == JS_VAR_CONST);
                                cv->tdz_active = false;
                            }
                        }
                        jm_write_env_backing_if_needed(mt, jm_find_var(mt, vname), reg, LMD_TYPE_ANY);
                        jm_scope_env_mark_and_writeback_binding(mt, vname, d->id, reg);
                        if (var->kind == JS_VAR_LET || var->kind == JS_VAR_CONST) {
                            jm_write_last_closure_capture_if_matching(mt, vname, reg, LMD_TYPE_ANY);
                        }
                    }
                }

                // For const MCONST_MODVAR in __main__ or IIFE body, store local value to module var table
                // so functions can access it via js_get_module_var.
                // Same shadowing rule as the is_modvar branch above: only the top-level
                // declaration writes to the module var slot. Nested let/const shadows
                // must keep the module var slot intact.
                bool at_top_for_writeback = (mt->scope_depth <= 1) ||
                    (var->kind == JS_VAR_VAR && mt->var_hoist_depth <= 1);
                if (!with_var_init_handled && !is_modvar && at_top_for_writeback && mt->module_consts) {
                    JsModuleConstEntry mclookup;
                    mclookup.name = jm_persist_name(vname);
                    JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                    bool in_modvar_scope = mt->in_main ||
                        (mc && mc->is_iife_var && mt->current_fc && mt->current_fc->is_iife_body);
                    if (in_modvar_scope && mc && mc->const_type == MCONST_MODVAR) {
                        JsMirVarEntry* ve = jm_find_var(mt, vname);
                        if (ve) {
                            MIR_reg_t boxed_val = ve->reg;
                            if (jm_is_native_type(ve->type_id)) {
                                boxed_val = jm_box_native(mt, ve->reg, ve->type_id);
                            }
                            jm_call_void_2(mt, "js_set_module_var",
                                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mc->int_val),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
                        }
                    }
                    // Store class object to module var so closures/methods can access it
                    if (in_modvar_scope && mc && mc->const_type == MCONST_CLASS) {
                        JsMirVarEntry* ve = jm_find_var(mt, vname);
                        if (ve) {
                            jm_call_void_2(mt, "js_set_module_var",
                                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mc->int_val),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, ve->reg));
                        }
                    }
                }
            } else if (d->id && d->id->node_type == JS_AST_NODE_ARRAY_PATTERN) {
                // v20: array destructuring via recursive helper
                MIR_reg_t src = d->init ? jm_transpile_box_item(mt, d->init) : jm_emit_null(mt);
                jm_emit_array_destructure(mt, d->id, src);
                jm_writeback_pattern_bindings(mt, var, d->id);
            } else if (d->id && d->id->node_type == JS_AST_NODE_OBJECT_PATTERN) {
                // v20: object destructuring via recursive helper
                MIR_reg_t src = d->init ? jm_transpile_box_item(mt, d->init) : jm_emit_null(mt);
                jm_emit_object_destructure(mt, d->id, src);
                jm_writeback_pattern_bindings(mt, var, d->id);
            }
        }
        decl = decl->next;
    }
    mt->var_hoist_depth = saved_hoist;
}

// Phase 3.5: Detect `typeof x === "number"/"string"/"boolean"` pattern.
// Returns the identifier node for x, or NULL if not matched.
// Sets *narrowed_type to the narrowed TypeId (FLOAT for "number", etc.)
// Sets *negate to true if the comparison is !== (narrowing applies to the else branch)
JsIdentifierNode* jm_detect_typeof_pattern(JsAstNode* test,
                                                    TypeId* narrowed_type, bool* negate) {
    if (!test || test->node_type != JS_AST_NODE_BINARY_EXPRESSION) return NULL;
    JsBinaryNode* bin = (JsBinaryNode*)test;
    bool is_eq  = (bin->op == JS_OP_STRICT_EQ || bin->op == JS_OP_EQ);
    bool is_neq = (bin->op == JS_OP_STRICT_NE || bin->op == JS_OP_NE);
    if (!is_eq && !is_neq) return NULL;
    *negate = is_neq;

    // Find which side is `typeof id` and which is a string literal
    JsAstNode* typeof_side = NULL;
    JsAstNode* literal_side = NULL;
    auto is_typeof_unary = [](JsAstNode* n) -> bool {
        if (!n || n->node_type != JS_AST_NODE_UNARY_EXPRESSION) return false;
        return ((JsUnaryNode*)n)->op == JS_OP_TYPEOF;
    };
    if (is_typeof_unary(bin->left))  { typeof_side = bin->left;  literal_side = bin->right; }
    else if (is_typeof_unary(bin->right)) { typeof_side = bin->right; literal_side = bin->left; }
    if (!typeof_side || !literal_side) return NULL;

    if (literal_side->node_type != JS_AST_NODE_LITERAL) return NULL;
    JsLiteralNode* lit = (JsLiteralNode*)literal_side;
    if (lit->literal_type != JS_LITERAL_STRING || !lit->value.string_value) return NULL;

    const char* s = lit->value.string_value->chars;
    size_t slen   = lit->value.string_value->len;
    if      (slen == 6 && strncmp(s, "number",  6) == 0) *narrowed_type = LMD_TYPE_FLOAT;
    else if (slen == 6 && strncmp(s, "string",  6) == 0) *narrowed_type = LMD_TYPE_STRING;
    else if (slen == 7 && strncmp(s, "boolean", 7) == 0) *narrowed_type = LMD_TYPE_BOOL;
    else return NULL;

    JsUnaryNode* un = (JsUnaryNode*)typeof_side;
    if (!un->operand || un->operand->node_type != JS_AST_NODE_IDENTIFIER) return NULL;
    return (JsIdentifierNode*)un->operand;
}

// Push a narrowed scope entry for a variable after a typeof guard.
// Returns true if narrowing was applied (caller must call jm_pop_scope after the block).
// Only narrows ANY→FLOAT for "number" guards (creates a new unboxed double register).
bool jm_push_typeof_narrow(JsMirTranspiler* mt, JsIdentifierNode* id, TypeId narrowed_type) {
    if (!id) return false;
    const char* vname = jm_format_name("_js_%.*s", (int)id->name->len, id->name->chars);
    JsMirVarEntry* orig = jm_find_var(mt, vname);
    if (!orig || orig->type_id != LMD_TYPE_ANY) return false;

    if (narrowed_type == LMD_TYPE_FLOAT) {
        // Unbox the boxed item to a native double
        MIR_reg_t narrow_reg = jm_new_reg(mt, "typeof_f", MIR_T_D);
        MIR_reg_t unboxed = jm_emit_unbox_float(mt, orig->reg);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
            MIR_new_reg_op(mt->ctx, narrow_reg),
            MIR_new_reg_op(mt->ctx, unboxed)));
        jm_push_scope(mt);
        jm_set_var(mt, vname, narrow_reg, MIR_T_D, LMD_TYPE_FLOAT);
        log_debug("js-mir P3.5 typeof: narrowed %s to FLOAT in branch", vname);
        return true;
    }
    // STRING / BOOL: no native form; just update type_id while keeping the original register
    jm_push_scope(mt);
    jm_set_var(mt, vname, orig->reg, orig->mir_type, narrowed_type);
    log_debug("js-mir P3.5 typeof: narrowed %s type_id to %d in branch", vname, narrowed_type);
    return true;
}

static bool jm_branch_assigns_identifier(JsAstNode* branch, JsIdentifierNode* id) {
    if (!branch || !id || !id->name) return false;
    const char* vname = jm_format_name("_js_%.*s", (int)id->name->len, id->name->chars);
    struct hashmap* assigned = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    jm_collect_func_assignments(branch, assigned);
    bool assigns = jm_name_set_has(assigned, vname);
    hashmap_free(assigned);
    return assigns;
}

static void jm_init_if_clause_function_binding(JsMirTranspiler* mt, JsAstNode* stmt) {
    if (!stmt || stmt->node_type != JS_AST_NODE_FUNCTION_DECLARATION) return;
    JsFunctionNode* fn = (JsFunctionNode*)stmt;
    if (!fn->name) return;
    JsFuncCollected* fc = jm_find_collected_func(mt, fn);
    if (!fc || !fc->func_item) return;
    const char* vname = jm_format_name("_js_%.*s", (int)fn->name->len, fn->name->chars);
    MIR_reg_t fn_reg = jm_create_func_or_closure(mt, fc);
    jm_set_var(mt, vname, fn_reg);
    JsMirVarEntry* ve = jm_find_var(mt, vname);
    if (ve) ve->from_block_func_decl = true;
    jm_scope_env_mark_and_writeback(mt, vname, fn_reg);
}

static void jm_init_block_function_bindings(JsMirTranspiler* mt, JsBlockNode* blk) {
    if (!mt || !blk) return;
    for (JsAstNode* s = blk->statements; s; s = s->next) {
        if (s->node_type != JS_AST_NODE_FUNCTION_DECLARATION) continue;
        // Block function declarations are visible from the start of their
        // block; initializing only at the textual declaration loses callbacks
        // referenced by earlier sibling functions.
        jm_init_if_clause_function_binding(mt, s);
    }
}

// transpile one if-branch body with the same scope/TDZ handling as the inline
// consequent/alternate paths below (used by the constant-folded dead-branch path).
static void jm_transpile_if_branch(JsMirTranspiler* mt, JsAstNode* branch) {
    if (!branch) return;
    if (branch->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
        jm_push_scope(mt);
        jm_init_block_tdz(mt, branch);
        JsBlockNode* blk = (JsBlockNode*)branch;
        jm_init_block_function_bindings(mt, blk);
        JsAstNode* s = blk->statements;
        while (s) { jm_transpile_statement(mt, s); s = s->next; }
        jm_pop_scope(mt);
    } else if (branch->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
        jm_push_scope(mt);
        jm_init_if_clause_function_binding(mt, branch);
        jm_transpile_statement(mt, branch);
        jm_pop_scope(mt);
    } else {
        jm_transpile_statement(mt, branch);
    }
}

// Whether a never-taken branch can be dropped without losing a hoisting side
// effect. var bindings are hoisted by jm_collect_body_locals regardless of
// lowering (and their assignments are runtime-conditional anyway), so the only
// hazard is Annex-B function-declaration hoisting. This whitelist admits only
// statements that cannot hoist a function into the enclosing scope.
static bool jm_branch_dead_safe(JsAstNode* n) {
    if (!n) return true;
    switch (n->node_type) {
    case JS_AST_NODE_THROW_STATEMENT:
    case JS_AST_NODE_EXPRESSION_STATEMENT:
    case JS_AST_NODE_RETURN_STATEMENT:
    case JS_AST_NODE_BREAK_STATEMENT:
    case JS_AST_NODE_CONTINUE_STATEMENT:
        return true;
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)n;
        for (JsAstNode* s = blk->statements; s; s = s->next)
            if (!jm_branch_dead_safe(s)) return false;
        return true;
    }
    default:
        return false;  // conservative: anything else uses the normal lowering path
    }
}

static void jm_emit_annexb_global_export(JsMirTranspiler* mt,
        MIR_reg_t key_reg, MIR_reg_t value_reg) {
    if (!mt || !key_reg || !value_reg) return;
    if (mt->is_eval_direct && !mt->is_global_strict && !mt->is_module) {
        MIR_reg_t eval_env_active = jm_call_0(mt, "js_eval_env_is_active", MIR_T_I64);
        MIR_label_t global_set = jm_new_label(mt);
        MIR_label_t local_set = jm_new_label(mt);
        MIR_label_t set_done = jm_new_label(mt);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
            MIR_new_label_op(mt->ctx, global_set),
            MIR_new_reg_op(mt->ctx, eval_env_active)));
        MIR_reg_t bridged_binding = jm_call_1(mt, "js_eval_env_has_binding", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
            MIR_new_label_op(mt->ctx, local_set),
            MIR_new_reg_op(mt->ctx, bridged_binding)));
        jm_emit_label(mt, global_set);
        jm_call_3(mt, "js_set_global_property", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, value_reg),
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
        jm_emit_error_lane_propagate_check(mt);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, set_done)));
        jm_emit_label(mt, local_set);
        jm_call_void_2(mt, "js_eval_local_export_var",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, value_reg));
        jm_emit_label(mt, set_done);
    } else {
        jm_call_3(mt, "js_set_global_property", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, value_reg),
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
        jm_emit_error_lane_propagate_check(mt);
    }
}

void jm_transpile_if(JsMirTranspiler* mt, JsIfNode* if_node) {
    JsMirLastClosureSnapshot saved_last_closure;
    jm_save_last_closure_snapshot(mt, &saved_last_closure);

    // Tune3 §3: constant-fold the condition and drop the dead branch entirely.
    if (jm_const_fold_enabled()) {
        JsFoldVal fv;
        if (jm_try_fold_const(if_node->test, &fv)) {
            bool cond = (fv.kind == JS_FOLD_BOOL) ? fv.boolean : (fv.num != 0.0);
            JsAstNode* live = cond ? if_node->consequent : if_node->alternate;
            JsAstNode* dead = cond ? if_node->alternate : if_node->consequent;
            if (jm_branch_dead_safe(dead)) {
                jm_eval_cptn_reset(mt);
                jm_transpile_if_branch(mt, live);
                // Constant-folded branches are still path-local for closure
                // readback, so do not let their env register escape.
                jm_restore_last_closure_snapshot(mt, &saved_last_closure);
                return;
            }
        }
    }
    // Phase 3.5: detect typeof narrowing pattern before emitting the test
    TypeId typeof_narrowed_type = LMD_TYPE_ANY;
    bool typeof_negate = false;
    JsIdentifierNode* typeof_id = jm_detect_typeof_pattern(if_node->test,
        &typeof_narrowed_type, &typeof_negate);

    // v23b: use jm_transpile_condition for unified condition handling
    // (covers native numeric comparisons, _raw facades, and fallback)
    MIR_reg_t test_val = jm_transpile_condition(mt, if_node->test);

    MIR_label_t l_else = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    // Eval completion: reset to undefined (spec: if false → NormalCompletion(undefined),
    // if true → UpdateEmpty(body, undefined))
    jm_eval_cptn_reset(mt);

    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_else),
        MIR_new_reg_op(mt->ctx, test_val)));
    JsErrorLaneTrack branch_exc = jm_error_lane_state(mt);
    MIR_reg_t branch_result_reg = mt->last_call_result_reg;

    // Consequent
    if (if_node->consequent) {
        // Phase 3.5: narrow variable type inside the consequent when typeof guard matched
        bool consequent_narrowed = false;
        if (typeof_id && !typeof_negate &&
            !jm_branch_assigns_identifier(if_node->consequent, typeof_id))
            consequent_narrowed = jm_push_typeof_narrow(mt, typeof_id, typeof_narrowed_type);

        if (if_node->consequent->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            jm_push_scope(mt);
            jm_init_block_tdz(mt, if_node->consequent);  // v20 TDZ
            JsBlockNode* blk = (JsBlockNode*)if_node->consequent;
            jm_init_block_function_bindings(mt, blk);
            JsAstNode* s = blk->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
            jm_pop_scope(mt);
        } else if (if_node->consequent->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
            jm_push_scope(mt);
            jm_init_if_clause_function_binding(mt, if_node->consequent);
            jm_transpile_statement(mt, if_node->consequent);
            jm_pop_scope(mt);
        } else {
            jm_transpile_statement(mt, if_node->consequent);
        }

        if (consequent_narrowed) jm_pop_scope(mt);
    }
    // route branch-local fallible results before the merge: the result register
    // produced only on the other branch is not defined on a normal path, so a
    // post-merge exception test would read a previous loop iteration's value.
    jm_emit_error_lane_route(mt, JS_MIR_COMPLETION_THROW);
    JsErrorLaneTrack consequent_exit = jm_error_lane_state(mt);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    // Alternate
    jm_emit_label_with_state(mt, l_else, branch_exc);
    // the alternate arm starts with the condition's result, not the
    // consequent arm's path-local throw result; without this bridge a normal
    // arm can test an Item written only by the sibling arm.
    mt->last_call_result_reg = branch_result_reg;
    if (if_node->alternate) {
        // Phase 3.5: narrow variable type inside the alternate when typeof !== guard matched
        bool alternate_narrowed = false;
        if (typeof_id && typeof_negate &&
            !jm_branch_assigns_identifier(if_node->alternate, typeof_id))
            alternate_narrowed = jm_push_typeof_narrow(mt, typeof_id, typeof_narrowed_type);

        if (if_node->alternate->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            jm_push_scope(mt);
            jm_init_block_tdz(mt, if_node->alternate);  // v20 TDZ
            JsBlockNode* blk = (JsBlockNode*)if_node->alternate;
            jm_init_block_function_bindings(mt, blk);
            JsAstNode* s = blk->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
            jm_pop_scope(mt);
        } else if (if_node->alternate->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
            jm_push_scope(mt);
            jm_init_if_clause_function_binding(mt, if_node->alternate);
            jm_transpile_statement(mt, if_node->alternate);
            jm_pop_scope(mt);
        } else {
            jm_transpile_statement(mt, if_node->alternate);
        }

        if (alternate_narrowed) jm_pop_scope(mt);
    }
    // keep the exception lane path-local until both arms have been checked;
    // otherwise an arm with no throw can inherit the sibling arm's Item.
    jm_emit_error_lane_route(mt, JS_MIR_COMPLETION_THROW);
    JsErrorLaneTrack alternate_exit = jm_error_lane_state(mt);
    jm_emit_label_with_state(mt, l_end,
        jm_error_lane_merge(consequent_exit, alternate_exit));

    // Branch-local closure env registers do not dominate the merge point; keep
    // later callback readback tied to the pre-if env instead of a path-local one.
    jm_restore_last_closure_snapshot(mt, &saved_last_closure);
}

// Reload all in-scope-env variables from the shared scope env into their local registers.
// Emitted at the top of each while-loop test to ensure the outer function sees changes
// made by inner-function (closure) calls during the loop body.
static void jm_reload_typed_scope_value(JsMirTranspiler* mt,
                                        JsVarScopeEntry* entry, MIR_reg_t boxed) {
    if (entry->var.type_id == LMD_TYPE_INT) {
        MIR_reg_t unboxed = jm_emit_unbox_int(mt, boxed);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, entry->var.reg),
            MIR_new_reg_op(mt->ctx, unboxed)));
    } else if (entry->var.type_id == LMD_TYPE_FLOAT) {
        MIR_reg_t unboxed = jm_emit_unbox_float(mt, boxed);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
            MIR_new_reg_op(mt->ctx, entry->var.reg),
            MIR_new_reg_op(mt->ctx, unboxed)));
    } else {
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, entry->var.reg),
            MIR_new_reg_op(mt->ctx, boxed)));
    }
}

void jm_scope_env_reload_vars(JsMirTranspiler* mt) {
    bool reload_iife_modvars = mt->module_consts && mt->current_fc && mt->current_fc->is_iife_body;
    if (mt->scope_env_reg == 0 && !reload_iife_modvars) return;
    for (int sd = 0; sd <= mt->scope_depth; sd++) {
        struct hashmap* scope = jm_var_scope_at(mt, sd);
        if (!scope) continue;
        size_t iter = 0; void* entry_ptr;
        while (hashmap_iter(scope, &iter, &entry_ptr)) {
            JsVarScopeEntry* e = (JsVarScopeEntry*)entry_ptr;
            if (e->var.from_block_func_decl) continue;
            if (strcmp(e->name, "_js_arguments") == 0 &&
                mt->arguments_reg != 0 && e->var.reg == mt->arguments_reg) {
                continue;
            }
            if (e->var.in_scope_env) {
                int slot = e->var.scope_env_slot;
                MIR_reg_t env_reg = e->var.scope_env_reg;
                if (env_reg != 0) {
                    // Load boxed value from scope env
                    MIR_reg_t boxed = jm_new_reg(mt, "se_rdld", MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, boxed),
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, slot * (int)sizeof(uint64_t), env_reg, 0, 1)));
                    jm_reload_typed_scope_value(mt, e, boxed);
                }
            }
            if (reload_iife_modvars && e->var.reg != 0) {
                JsModuleConstEntry lookup;
                memset(&lookup, 0, sizeof(lookup));
                lookup.name = jm_persist_name(e->name);
                JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup);
                if (mc && mc->const_type == MCONST_MODVAR && mc->is_iife_var &&
                    e->var.is_iife_module_var_binding) {
                    MIR_reg_t boxed = jm_call_1(mt, "js_get_module_var", MIR_T_I64,
                        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mc->int_val));
                    jm_reload_typed_scope_value(mt, e, boxed);
                }
            }
        }
    }
}

// Reload captured variables from shared parent scope_env after a function call.
// In child closures, variables captured from a parent's scope_env may be modified
// by sibling closures called during function evaluation. Re-read those values
// from the env to avoid stale cached register values.
void jm_env_reload_shared_captures(JsMirTranspiler* mt) {
    for (int sd = 0; sd <= mt->scope_depth; sd++) {
        struct hashmap* scope = jm_var_scope_at(mt, sd);
        if (!scope) continue;
        size_t iter = 0; void* entry_ptr;
        while (hashmap_iter(scope, &iter, &entry_ptr)) {
            JsVarScopeEntry* e = (JsVarScopeEntry*)entry_ptr;
            if (!e->var.from_shared_env || e->var.env_reg == 0) continue;
            int slot = e->var.env_slot;
            if (slot < 0) continue;
            // Load boxed value from env (which IS the parent's shared scope_env)
            MIR_reg_t boxed = jm_new_reg(mt, "ce_rdld", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, boxed),
                MIR_new_mem_op(mt->ctx, MIR_T_I64, slot * (int)sizeof(uint64_t), e->var.env_reg, 0, 1)));
            // Captured vars are always boxed Items (no unboxing needed)
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, e->var.reg),
                MIR_new_reg_op(mt->ctx, boxed)));
        }
    }
}

// Test the last returned Item's ERROR tag and route that same lane: to a
// lexical catch/finally when present, otherwise to the function error exit.
// D8.4.3 forbids a separate pending-exception state or poll result.
void jm_emit_error_lane_propagate_check(JsMirTranspiler* mt) {
    jm_emit_error_lane_route(mt, JS_MIR_COMPLETION_THROW);
}

void jm_transpile_while(JsMirTranspiler* mt, JsWhileNode* wh) {
    MIR_label_t l_test = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    // Push loop labels
    jm_push_loop_labels(mt, l_test, l_end);
    mt->iteration_depth++;

    // Eval completion: Let V = undefined (spec §14.7.3.2)
    jm_eval_cptn_reset(mt);

    jm_emit_label(mt, l_test);

    // Reload scope-env variables so the loop condition sees values updated by
    // inner-function (closure) calls made during the previous loop iteration.
    jm_scope_env_reload_vars(mt);

    // v23b: unified condition handling
    MIR_reg_t test_cond = jm_transpile_condition(mt, wh->test);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
        MIR_new_reg_op(mt->ctx, test_cond)));

    // Body
    if (wh->body) {
        if (wh->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            jm_push_scope(mt);
            jm_init_block_tdz(mt, wh->body);  // v20 TDZ
            JsBlockNode* blk = (JsBlockNode*)wh->body;
            JsAstNode* s = blk->statements;
            while (s) {
                jm_transpile_statement(mt, s);
                s = s->next;
            }
            jm_pop_scope(mt);
        } else {
            jm_transpile_statement(mt, wh->body);
        }
    }

    jm_emit_loop_backedge_frame_reload(mt);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_test)));
    jm_emit_label(mt, l_end);

    if (mt->iteration_depth > 0) mt->iteration_depth--;
    if (mt->loop_depth > 0) mt->loop_depth--;
}

void jm_transpile_for(JsMirTranspiler* mt, JsForNode* for_node) {
    // JS spec: 'var' declarations in for-init are function-scoped — they must be
    // visible after the loop ends. Only push a new scope for 'let'/'const' inits.
    bool init_is_var = false;
    bool init_is_lexical_decl = false;
    const char* for_var_init_name = NULL;
    const char* for_lexical_init_name = NULL;
    if (for_node->init && for_node->init->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
        JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)for_node->init;
        if (vd->kind == JS_VAR_VAR) {
            init_is_var = true;
            if (vd->declarations && vd->declarations->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)vd->declarations;
                if (d->id && d->id->node_type == JS_AST_NODE_IDENTIFIER) {
                    JsIdentifierNode* id = (JsIdentifierNode*)d->id;
                    for_var_init_name = jm_format_name("_js_%.*s",
                        (int)id->name->len, id->name->chars);
                }
            }
        }
        else {
            init_is_lexical_decl = true;
            if (vd->declarations && vd->declarations->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)vd->declarations;
                if (d->id && d->id->node_type == JS_AST_NODE_IDENTIFIER) {
                    JsIdentifierNode* id = (JsIdentifierNode*)d->id;
                    for_lexical_init_name = jm_format_name("_js_%.*s",
                        (int)id->name->len, id->name->chars);
                }
            }
        }
    }

    // Transpile non-lexical init BEFORE pushing scope. `var` declarations and
    // expression initializers such as `i = 0` evaluate in the surrounding
    // environment; only let/const for-inits need the synthetic loop scope.
    if (for_node->init && !init_is_lexical_decl) {
        jm_transpile_statement(mt, for_node->init);
    }

    jm_push_scope(mt);
    int saved_loop_scope_depth = mt->loop_scope_depth;
    // normal for-loops need a lexical boundary too; without it, closures made
    // in the loop treat enclosing block lets as per-iteration loop bindings.
    mt->loop_scope_depth = mt->scope_depth;

    // Init let/const declarations inside the for scope.
    if (for_node->init && init_is_lexical_decl) {
        jm_transpile_statement(mt, for_node->init);
    }

    // Js56 P2: per-iteration binding boundary. Any closure created in INIT
    // captures the init-time binding; subsequent test/body/update assignments
    // must not write through to that closure's env (per-iteration semantics —
    // each iteration has its own logical binding). Resetting last_closure_*
    // here prevents the assignment writeback (added by Js56 P2) from leaking
    // an iteration mutation into a closure that captured the init binding.
    // Regression test: language/statements/for/scope-body-lex-open.js.
    mt->last_closure_has_env = false;
    mt->last_closure_env_reg = 0;
    mt->last_closure_capture_count = 0;

    // Eval completion: ForBodyEvaluation starts with V = undefined (spec §13.7.4.8)
    jm_eval_cptn_reset(mt);

    // --- For-loop specialization: detect a native-compatible loop bound ---
    // Three tiers of test optimization:
    //   1. full_native:  both sides typed numeric → native compare + branch (existing)
    //   2. semi_native:  one side typed, other untyped → coerce the live bound
    //                    and use a native compare each iteration
    //   3. boxed:        no type info → boxed runtime comparison (fallback)
    bool semi_native_test = false;
    JsAstNode* semi_native_bound_node = NULL;
    TypeId semi_native_bound_type = LMD_TYPE_NULL;
    MIR_insn_code_t cached_cmp_insn = MIR_LTS;
    bool cached_bound_on_right = true;
    JsAstNode* cached_counter_node = NULL;
    TypeId cached_cmp_target = LMD_TYPE_INT;

    if (for_node->test && for_node->test->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
        JsBinaryNode* test_bin = (JsBinaryNode*)for_node->test;
        TypeId lt = jm_get_effective_type(mt, test_bin->left);
        TypeId rt = jm_get_effective_type(mt, test_bin->right);
        bool left_num  = (lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT);
        bool right_num = (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT);
        bool full_native = left_num && right_num;

        // Only consider semi-native when one side is typed, other isn't
        if (!full_native && (left_num || right_num)) {
            bool is_cmp = false;
            switch (test_bin->op) {
            case JS_OP_LT: case JS_OP_LE: case JS_OP_GT: case JS_OP_GE:
            case JS_OP_EQ: case JS_OP_NE: case JS_OP_STRICT_EQ: case JS_OP_STRICT_NE:
                is_cmp = true; break;
            default: break;
            }

            if (is_cmp) {
                // Identify the loop counter from the init statement to avoid
                // confusing counter/bound.  The counter is the variable being
                // initialized in for(init; test; update).
                const char* init_var_name = NULL;
                int init_var_len = 0;
                if (for_node->init && for_node->init->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                    JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)for_node->init;
                    if (vd->declarations && vd->declarations->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                        JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)vd->declarations;
                        if (d->id && d->id->node_type == JS_AST_NODE_IDENTIFIER) {
                            JsIdentifierNode* vid = (JsIdentifierNode*)d->id;
                            init_var_name = vid->name->chars;
                            init_var_len = (int)vid->name->len;
                        }
                    }
                } else if (for_node->init && for_node->init->node_type == JS_AST_NODE_ASSIGNMENT_EXPRESSION) {
                    JsAssignmentNode* asgn = (JsAssignmentNode*)for_node->init;
                    if (asgn->op == JS_OP_ASSIGN &&
                        asgn->left && asgn->left->node_type == JS_AST_NODE_IDENTIFIER) {
                        JsIdentifierNode* vid = (JsIdentifierNode*)asgn->left;
                        init_var_name = vid->name->chars;
                        init_var_len = (int)vid->name->len;
                    }
                }

                // Determine which side is the counter (must match init variable)
                bool left_is_counter = false;
                bool right_is_counter = false;
                if (init_var_name) {
                    if (test_bin->left->node_type == JS_AST_NODE_IDENTIFIER) {
                        JsIdentifierNode* lid = (JsIdentifierNode*)test_bin->left;
                        if (lid->name->len == (size_t)init_var_len &&
                            strncmp(lid->name->chars, init_var_name, init_var_len) == 0)
                            left_is_counter = true;
                    }
                    if (test_bin->right->node_type == JS_AST_NODE_IDENTIFIER) {
                        JsIdentifierNode* rid = (JsIdentifierNode*)test_bin->right;
                        if (rid->name->len == (size_t)init_var_len &&
                            strncmp(rid->name->chars, init_var_name, init_var_len) == 0)
                            right_is_counter = true;
                    }
                }

                // Only use semi-native if we can identify counter and the typed
                // side is the counter (so we can unbox it; bound is cached as native)
                bool can_semi = false;
                JsAstNode* bound_expr = NULL;
                TypeId bound_type = LMD_TYPE_NULL;
                bool use_float = false;

                if (left_is_counter && !right_is_counter && left_num) {
                    // Pattern: typed_counter CMP untyped_bound  (e.g. i < n)
                    cached_counter_node = test_bin->left;
                    bound_expr = test_bin->right;
                    bound_type = rt;
                    use_float = (lt == LMD_TYPE_FLOAT);
                    cached_bound_on_right = true;
                    can_semi = true;
                } else if (right_is_counter && !left_is_counter && right_num) {
                    // Pattern: untyped_bound CMP typed_counter  (e.g. 0 <= i)
                    cached_counter_node = test_bin->right;
                    bound_expr = test_bin->left;
                    bound_type = lt;
                    use_float = (rt == LMD_TYPE_FLOAT);
                    cached_bound_on_right = false;
                    can_semi = true;
                }

                if (can_semi) {
                    cached_cmp_target = use_float ? LMD_TYPE_FLOAT : LMD_TYPE_INT;
                    semi_native_bound_node = bound_expr;
                    semi_native_bound_type = bound_type;

                    switch (test_bin->op) {
                    case JS_OP_LT:        cached_cmp_insn = use_float ? MIR_DLT : MIR_LTS; break;
                    case JS_OP_LE:        cached_cmp_insn = use_float ? MIR_DLE : MIR_LES; break;
                    case JS_OP_GT:        cached_cmp_insn = use_float ? MIR_DGT : MIR_GTS; break;
                    case JS_OP_GE:        cached_cmp_insn = use_float ? MIR_DGE : MIR_GES; break;
                    case JS_OP_EQ:
                    case JS_OP_STRICT_EQ: cached_cmp_insn = use_float ? MIR_DEQ : MIR_EQ;  break;
                    case JS_OP_NE:
                    case JS_OP_STRICT_NE: cached_cmp_insn = use_float ? MIR_DNE : MIR_NE;  break;
                    default: break;
                    }

                    // A call in the body may resize an array used by the bound
                    // (for example splice() under `i < array.length`). Re-read
                    // dynamic bounds at the loop test so native comparison does
                    // not change JavaScript's per-iteration evaluation semantics.
                    semi_native_test = true;
                }
            }
        }
    }

    MIR_label_t l_test = jm_new_label(mt);
    MIR_label_t l_update = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    // Push loop labels
    jm_push_loop_labels(mt, l_update, l_end);
    mt->iteration_depth++;

    jm_emit_label(mt, l_test);

    // If the prior fallible operation returned an ERROR Item, leave the loop.
    jm_emit_error_lane_guard(mt, l_end);

    // Reload scope-env variables so the loop condition sees values updated by
    // inner-function (closure) calls made during the previous iteration.
    jm_scope_env_reload_vars(mt);

    // Test
    if (for_node->test) {
        if (semi_native_test) {
            // Semi-native: read the counter and current bound as native values.
            TypeId ct = jm_get_effective_type(mt, cached_counter_node);
            MIR_reg_t counter_reg = jm_transpile_as_native(mt, cached_counter_node, ct, cached_cmp_target);
            MIR_reg_t current_bound = jm_transpile_as_native(mt, semi_native_bound_node,
                semi_native_bound_type, cached_cmp_target);

            MIR_reg_t left_cmp  = cached_bound_on_right ? counter_reg  : current_bound;
            MIR_reg_t right_cmp = cached_bound_on_right ? current_bound : counter_reg;

            MIR_reg_t test_r = jm_new_reg(mt, "fltest", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, cached_cmp_insn,
                MIR_new_reg_op(mt->ctx, test_r),
                MIR_new_reg_op(mt->ctx, left_cmp),
                MIR_new_reg_op(mt->ctx, right_cmp)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
                MIR_new_reg_op(mt->ctx, test_r)));
        } else {
            // v23b: unified condition handling (native numeric + raw facades + fallback)
            MIR_reg_t test_cond = jm_transpile_condition(mt, for_node->test);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
                MIR_new_reg_op(mt->ctx, test_cond)));
        }
    }

    // Body
    if (for_node->body) {
        if (for_node->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            jm_push_scope(mt);
            jm_init_block_tdz(mt, for_node->body);  // v20 TDZ
            JsBlockNode* blk = (JsBlockNode*)for_node->body;
            JsAstNode* s = blk->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
            jm_pop_scope(mt);
        } else {
            jm_transpile_statement(mt, for_node->body);
        }
    }

    // Update — use native path for typed increment/assignment
    jm_emit_label(mt, l_update);
    if (for_node->update) {
        JsMirLastClosureSnapshot saved_last_closure;
        jm_save_last_closure_snapshot(mt, &saved_last_closure);
        jm_clear_last_closure_snapshot(mt);
        mt->preserve_last_closure_env_after_readback = true;

        TypeId upd_type = jm_get_effective_type(mt, for_node->update);
        if (jm_is_native_type(upd_type)) {
            jm_transpile_expression(mt, for_node->update);
        } else {
            jm_transpile_box_item(mt, for_node->update);
        }
        // v23c: route an ERROR Item returned by the update through the active
        // try context. There is no stale ambient exception to inspect.
        if (mt->try_ctx_depth > 0 && !jm_is_native_type(jm_get_effective_type(mt, for_node->update))) {
            jm_emit_error_lane_propagate_check(mt);
        }
        if (init_is_lexical_decl && for_lexical_init_name &&
                for_lexical_init_name[0] && mt->last_closure_has_env) {
            jm_scope_env_reload_vars(mt);
            JsMirVarEntry* loop_var = jm_find_var(mt, for_lexical_init_name);
            if (loop_var && loop_var->reg) {
                jm_write_last_closure_capture_if_matching(mt, for_lexical_init_name,
                    loop_var->reg, loop_var->type_id);
            }
        }
        jm_restore_last_closure_snapshot(mt, &saved_last_closure);
    }

    jm_emit_loop_backedge_frame_reload(mt);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_test)));
    jm_emit_label(mt, l_end);

    if (init_is_var && for_var_init_name && for_var_init_name[0]) {
        JsMirVarEntry* init_var = jm_find_var(mt, for_var_init_name);
        if (init_var && init_var->type_id == LMD_TYPE_INT && !init_var->from_env) {
            MIR_reg_t boxed_counter = jm_box_native(mt, init_var->reg, LMD_TYPE_INT);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, init_var->reg),
                MIR_new_reg_op(mt->ctx, boxed_counter)));
            init_var->type_id = LMD_TYPE_ANY;
            init_var->mir_type = MIR_T_I64;
        }
    }

    if (mt->iteration_depth > 0) mt->iteration_depth--;
    if (mt->loop_depth > 0) mt->loop_depth--;
    mt->loop_scope_depth = saved_loop_scope_depth;
    jm_pop_scope(mt);
}

// Build a closure for a class method that has captures
MIR_reg_t jm_build_closure_for_method(JsMirTranspiler* mt, JsFuncCollected* fc, int param_count) {
    (void)param_count;
    MIR_reg_t closure_reg = jm_create_func_or_closure(mt, fc);
    if (fc->is_derived_constructor) {
        jm_call_void_1(mt, "js_mark_derived_constructor_func",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, closure_reg));
    }
    return closure_reg;
}

static void jm_emit_private_brand_add(JsMirTranspiler* mt, MIR_reg_t obj, MIR_reg_t cls_obj, String* private_name) {
    if (!mt || !obj || !cls_obj || !jm_is_private_name(private_name)) return;
    MIR_reg_t source = jm_box_string_literal(mt, private_name->chars, (int)private_name->len);
    MIR_reg_t key = jm_call_2(mt, "js_private_key_for_class", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, source));
    jm_call_3(mt, "js_private_brand_add", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
    jm_emit_error_lane_propagate_check(mt);
}

void jm_emit_private_instance_method_brands(JsMirTranspiler* mt, MIR_reg_t obj, MIR_reg_t cls_obj, JsClassEntry* ce) {
    if (!mt || !ce || !cls_obj) return;
    for (int mi = 0; mi < ce->method_count; mi++) {
        JsClassMethodEntry* me = &ce->methods[mi];
        if (me->is_static || me->is_constructor || !me->name || !jm_is_private_name(me->name)) continue;
        String* method_name = jm_class_private_name(mt, ce, me->name);
        jm_emit_private_brand_add(mt, obj, cls_obj, method_name);
        // Private methods share a single class brand; a second add would
        // otherwise mask repeated construction of the same receiver.
        return;
    }
}

static MIR_reg_t jm_emit_class_static_field_value(JsMirTranspiler* mt,
        MIR_reg_t cls_obj, JsClassEntry* ce, JsStaticFieldEntry* sf) {
    MIR_reg_t previous_private_home = 0;
    if (sf->initializer && cls_obj && jm_class_or_ancestor_has_private_members(ce)) {
        previous_private_home = jm_call_1(mt, "js_private_home_class_enter", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
        jm_create_gc_root_slot(mt, previous_private_home);
    }
    MIR_reg_t val = sf->initializer ? jm_transpile_box_item(mt, sf->initializer) : jm_emit_undefined(mt);
    if (previous_private_home) {
        val = jm_call_2(mt, "js_private_home_class_leave_result", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, previous_private_home),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
    }
    jm_emit_error_lane_propagate_check(mt);
    return val;
}

void jm_emit_class_static_field(JsMirTranspiler* mt, MIR_reg_t cls_obj, JsClassEntry* ce, JsStaticFieldEntry* sf) {
    if (!mt || !sf) return;
    if (sf->computed && sf->key_expr) {
        MIR_reg_t key;
        if (sf->key_module_var_index >= 0) {
            key = jm_call_1(mt, "js_get_module_var", MIR_T_I64,
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)sf->key_module_var_index));
        } else {
            key = jm_transpile_box_item(mt, sf->key_expr);
            key = jm_call_1(mt, "js_to_property_key", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
            jm_call_1(mt, "js_check_class_static_field_key", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
            jm_emit_error_lane_propagate_check(mt);
        }
        MIR_reg_t val = jm_emit_class_static_field_value(mt, cls_obj, ce, sf);
        jm_call_void_2(mt, "js_set_function_name_if_anonymous",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
        jm_call_3(mt, "js_create_data_property", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        jm_emit_error_lane_propagate_check(mt);
        if (sf->name && jm_is_private_name(sf->name)) {
        jm_call_3(mt, "js_private_brand_add", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
        jm_emit_error_lane_propagate_check(mt);
        }
        return;
    }

    MIR_reg_t val = jm_emit_class_static_field_value(mt, cls_obj, ce, sf);
    if (sf->module_var_index >= 0) {
        jm_call_void_2(mt, "js_set_module_var",
            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)sf->module_var_index),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
    }
    if (sf->name) {
        MIR_reg_t fn_name = jm_box_string_literal(mt, sf->name->chars, (int)sf->name->len);
        jm_call_void_2(mt, "js_set_function_name_if_anonymous",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_name));
        MIR_reg_t key = jm_is_private_name(sf->name)
            ? jm_box_string_literal(mt, sf->name->chars, (int)sf->name->len)
            : jm_box_property_name_literal(mt, sf->name->chars, sf->name->len);
        if (jm_is_private_name(sf->name)) {
            key = jm_call_2(mt, "js_private_key_for_class", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
        }
        jm_call_1(mt, "js_check_class_static_field_key", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
        jm_emit_error_lane_propagate_check(mt);
        jm_call_3(mt, "js_create_data_property", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        jm_emit_error_lane_propagate_check(mt);
        if (jm_is_private_name(sf->name)) {
            jm_call_3(mt, "js_private_brand_add", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
            jm_emit_error_lane_propagate_check(mt);
        }
    } else if (sf->key_expr) {
        MIR_reg_t key = jm_transpile_box_item(mt, sf->key_expr);
        key = jm_call_1(mt, "js_to_property_key", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
        jm_call_1(mt, "js_check_class_static_field_key", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
        jm_emit_error_lane_propagate_check(mt);
        jm_call_void_2(mt, "js_set_function_name_if_anonymous",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
        jm_call_3(mt, "js_create_data_property", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
    }
    (void)ce;
}

void jm_emit_class_static_block(JsMirTranspiler* mt, MIR_reg_t cls_obj,
        JsClassEntry* ce, JsAstNode* block) {
    if (!mt || !block) return;
    JsClassEntry* saved_current_class = mt->current_class;
    MIR_reg_t saved_private_home_class_reg = mt->current_private_home_class_reg;
    int saved_hoist = mt->var_hoist_depth;
    mt->current_class = ce ? ce : mt->current_class;
    mt->current_private_home_class_reg = cls_obj;
    jm_push_scope(mt);
    jm_push_scope(mt);
    mt->var_hoist_depth = mt->scope_depth;
    jm_init_block_tdz(mt, block);
    {
        struct hashmap* static_vars = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        jm_collect_body_locals(block, static_vars, true);
        size_t iter = 0;
        void* item;
        while (hashmap_iter(static_vars, &iter, &item)) {
            JsNameSetEntry* entry = (JsNameSetEntry*)item;
            if (!entry || jm_find_var_in_scope_depth(mt, entry->name, mt->scope_depth)) continue;
            MIR_reg_t undef = jm_emit_undefined(mt);
            jm_set_var(mt, entry->name, undef, MIR_T_I64, LMD_TYPE_ANY);
            JsMirVarEntry* local = jm_find_var_in_scope_depth(mt, entry->name, mt->scope_depth);
            if (local) local->from_hoist = true;
        }
        hashmap_free(static_vars);
    }
    if (block->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
        JsBlockNode* blk = (JsBlockNode*)block;
        for (JsAstNode* s = blk->statements; s; s = s->next) {
            jm_transpile_statement(mt, s);
        }
    } else {
        jm_transpile_statement(mt, block);
    }
    mt->var_hoist_depth = saved_hoist;
    jm_pop_scope(mt);
    jm_pop_scope(mt);
    mt->current_private_home_class_reg = saved_private_home_class_reg;
    mt->current_class = saved_current_class;
}

void jm_emit_class_static_initializers(JsMirTranspiler* mt, MIR_reg_t cls_obj, JsClassEntry* ce,
    MIR_reg_t ctor_super_val) {
    // static initializers temporarily run with the class as this and restore the ambient binding afterward.
    if (ctor_super_val) {
        jm_call_void_2(mt, "js_set_prototype",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, ctor_super_val));
    }
    MIR_reg_t prev_static_this = jm_call_0(mt, "js_get_lexical_this_binding", MIR_T_I64);
    MIR_reg_t prev_static_new_target = jm_call_0(mt, "js_get_new_target", MIR_T_I64);
    jm_call_void_1(mt, "js_set_this", MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
    MIR_reg_t static_new_target = jm_emit_undefined(mt);
    jm_call_void_1(mt, "js_set_direct_new_target",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, static_new_target));
    JsMirLexicalThisRebind static_this_rebind;
    jm_emit_begin_lexical_this_rebind(mt, cls_obj, &static_this_rebind, true);
    jm_call_void_0(mt, "js_private_field_init_begin");
    bool emitted_ordered_static_elements = false;
    if (ce->node && ce->node->body && ce->node->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
        JsBlockNode* body = (JsBlockNode*)ce->node->body;
        int static_field_index = 0;
        int static_block_index = 0;
        for (JsAstNode* elem = body->statements; elem; elem = elem->next) {
            if (elem->node_type == JS_AST_NODE_FIELD_DEFINITION) {
                JsFieldDefinitionNode* fd = (JsFieldDefinitionNode*)elem;
                if (!fd->is_static) continue;
                if (static_field_index >= ce->static_field_count) continue;
                jm_emit_class_static_field(mt, cls_obj, ce, &ce->static_fields[static_field_index++]);
            } else if (elem->node_type == JS_AST_NODE_STATIC_BLOCK) {
                if (static_block_index >= ce->static_block_count) continue;
                jm_emit_class_static_block(mt, cls_obj, ce,
                    ce->static_blocks[static_block_index++]);
            }
        }
        emitted_ordered_static_elements = true;
    }
    if (!emitted_ordered_static_elements) {
        for (int fi = 0; fi < ce->static_field_count; fi++) {
            jm_emit_class_static_field(mt, cls_obj, ce, &ce->static_fields[fi]);
        }
    }
    if (!emitted_ordered_static_elements) for (int si = 0; si < ce->static_block_count; si++) {
        if (ce->static_blocks[si]) {
            jm_emit_class_static_block(mt, cls_obj, ce, ce->static_blocks[si]);
        }
    }
    jm_emit_end_lexical_this_rebind(mt, &static_this_rebind);
    jm_call_void_1(mt, "js_set_this",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, prev_static_this));
    jm_call_void_1(mt, "js_set_direct_new_target",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, prev_static_new_target));
    jm_call_void_0(mt, "js_private_field_init_end");
    if (ctor_super_val) {
        jm_call_void_2(mt, "js_set_prototype",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, ctor_super_val));
    }
}

void jm_emit_class_self_extends_check(JsMirTranspiler* mt, JsClassEntry* ce,
        String* class_name) {
    if (!ce || !ce->has_self_extends) return;
    char msg[256];
    snprintf(msg, sizeof(msg), "Cannot access '%.*s' before initialization",
        class_name ? (int)class_name->len : 1,
        class_name ? class_name->chars : "?");
    MIR_reg_t msg_reg = jm_box_string_literal(mt, msg, (int)strlen(msg));
    jm_call_2(mt, "js_throw_named_error", MIR_T_I64,
        MIR_T_I64, MIR_new_int_op(mt->ctx, 1),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, msg_reg));
    jm_emit_error_lane_propagate_check(mt);
}

MIR_reg_t jm_emit_current_class_prototype(JsMirTranspiler* mt, MIR_reg_t cls_obj,
        MIR_reg_t fallback_proto) {
    if (!cls_obj) return fallback_proto;
    MIR_reg_t proto_key = jm_box_property_name_literal(mt, "prototype", 9);
    MIR_reg_t current = jm_call_2(mt, "js_get_key_default", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_key));
    jm_create_gc_root_slot(mt, current);
    return current;
}

MIR_reg_t jm_emit_class_prototype_chain(JsMirTranspiler* mt, JsClassEntry* ce,
        MIR_reg_t cls_obj, JsAstNode* heritage, JsClassEntry* static_superclass, MIR_reg_t proto_obj,
        MIR_reg_t checked_heritage_val, bool* heritage_is_null_out) {
    MIR_reg_t ctor_super_val = 0;
    if (static_superclass) {
        ctor_super_val = jm_link_static_super_prototype(mt, cls_obj, proto_obj, static_superclass);
    }
    if (!static_superclass && ce && ce->node && ce->node->superclass &&
        ce->node->superclass->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* super_id = (JsIdentifierNode*)ce->node->superclass;
        if (super_id->name) {
            const char* sname = super_id->name->chars;
            int slen = (int)super_id->name->len;
            if (js_builtin_global_has_flag(sname, slen, JS_BUILTIN_GLOBAL_ERROR_CLASS)) {
                // NativeError prototypes are shared singletons, so link the actual constructor prototype.
                JsIdentifierNode tmp_sid;
                memset(&tmp_sid, 0, sizeof(tmp_sid));
                tmp_sid.node_type = JS_AST_NODE_IDENTIFIER;
                tmp_sid.name = super_id->name;
                MIR_reg_t super_ctor = jm_transpile_box_item(mt, (JsAstNode*)&tmp_sid);
                MIR_reg_t sp_key = jm_box_property_name_literal(mt, "prototype", 9);
                MIR_reg_t err_proto = jm_call_2(mt, "js_get_key_default", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, super_ctor),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_key));
                jm_call_1(mt, "js_check_class_prototype_parent", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, err_proto));
                jm_emit_error_lane_propagate_check(mt);
                proto_obj = jm_emit_current_class_prototype(mt, cls_obj, proto_obj);
                jm_call_void_2(mt, "js_set_prototype",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_obj),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, err_proto));
                ctor_super_val = super_ctor;
            } else {
                MIR_reg_t super_val = jm_transpile_box_item(mt, (JsAstNode*)super_id);
                MIR_reg_t sp_key = jm_box_property_name_literal(mt, "prototype", 9);
                MIR_reg_t sp_proto = jm_call_2(mt, "js_get_key_default", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, super_val),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_key));
                jm_call_1(mt, "js_check_class_prototype_parent", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_proto));
                jm_emit_error_lane_propagate_check(mt);
                proto_obj = jm_emit_current_class_prototype(mt, cls_obj, proto_obj);
                jm_call_void_2(mt, "js_set_prototype",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_obj),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_proto));
                ctor_super_val = super_val;
            }
        }
    }
    if (!static_superclass && ce && ce->node && ce->node->superclass &&
        ce->node->superclass->node_type != JS_AST_NODE_IDENTIFIER &&
        ce->node->superclass->node_type != JS_AST_NODE_NULL &&
        !(ce->node->superclass->node_type == JS_AST_NODE_LITERAL &&
          ((JsLiteralNode*)ce->node->superclass)->literal_type == JS_LITERAL_NULL)) {
        MIR_reg_t super_val = checked_heritage_val ? checked_heritage_val :
            jm_transpile_box_item(mt, ce->node->superclass);
        if (!checked_heritage_val) {
            jm_call_1(mt, "js_check_class_heritage_constructor", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, super_val));
            jm_emit_error_lane_propagate_check(mt);
        }
        MIR_reg_t sp_key = jm_box_property_name_literal(mt, "prototype", 9);
        MIR_reg_t sp_proto = jm_call_2(mt, "js_get_key_default", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, super_val),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_key));
        jm_call_1(mt, "js_check_class_prototype_parent", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_proto));
        jm_emit_error_lane_propagate_check(mt);
        proto_obj = jm_emit_current_class_prototype(mt, cls_obj, proto_obj);
        jm_call_void_2(mt, "js_set_prototype",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_proto));
        ctor_super_val = super_val;
    }
    bool heritage_is_null = heritage && (heritage->node_type == JS_AST_NODE_NULL ||
        (heritage->node_type == JS_AST_NODE_LITERAL &&
         ((JsLiteralNode*)heritage)->literal_type == JS_LITERAL_NULL));
    if (!heritage_is_null && heritage && heritage->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* heritage_id = (JsIdentifierNode*)heritage;
        heritage_is_null = heritage_id->name && heritage_id->name->len == 4 &&
            strncmp(heritage_id->name->chars, "null", 4) == 0;
    }
    if (heritage_is_null) {
        MIR_reg_t null_proto = jm_emit_null(mt);
        jm_call_void_2(mt, "js_set_prototype",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, null_proto));
    }
    if (heritage_is_null_out) *heritage_is_null_out = heritage_is_null;
    return ctor_super_val;
}

void jm_emit_class_length_property(JsMirTranspiler* mt, MIR_reg_t cls_obj,
        JsClassEntry* ce) {
    int ctor_len = 0;
    if (ce && ce->constructor && ce->constructor->fc)
        ctor_len = ce->constructor->param_count;
    MIR_reg_t len_key = jm_box_property_name_literal(mt, "length", 6);
    MIR_reg_t len_val = jm_new_reg(mt, "cls_len", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, len_val),
        MIR_new_int_op(mt->ctx, (int64_t)i2it(ctor_len))));
    jm_call_3(mt, "js_set_key_default", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, len_key),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, len_val));
    jm_call_void_2(mt, "js_mark_non_writable",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, len_key));
    jm_call_void_2(mt, "js_mark_non_enumerable",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, len_key));
}

void jm_emit_class_setup(JsMirTranspiler* mt, MIR_reg_t cls_obj, JsClassEntry* ce,
        JsAstNode* class_node, bool computed_key_before_function, JsMirClassSetup* setup) {
    jm_emit_class_length_property(mt, cls_obj, ce);
    if (ce->name) {
        MIR_reg_t name_val = jm_box_string_literal(mt, ce->name->chars, (int)ce->name->len);
        jm_call_void_2(mt, "js_set_class_name",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, name_val));
    }

    setup->ctor_super_val = 0;
    setup->class_proto_obj = jm_call_0(mt, "js_new_object", MIR_T_I64);
    jm_create_gc_root_slot(mt, setup->class_proto_obj);
    MIR_reg_t early_pt_key = jm_box_property_name_literal(mt, "prototype", 9);
    jm_call_3(mt, "js_set_key_default", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, early_pt_key),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, setup->class_proto_obj));
    jm_call_void_2(mt, "js_mark_non_writable",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, early_pt_key));
    jm_call_void_2(mt, "js_mark_non_enumerable",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, early_pt_key));
    jm_call_void_2(mt, "js_mark_non_configurable",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, early_pt_key));

    setup->heritage = ((JsClassNode*)class_node)->superclass ? ((JsClassNode*)class_node)->superclass :
        ((ce->node && ce->node->superclass) ? ce->node->superclass : NULL);
    setup->static_superclass = jm_matching_static_superclass(ce, setup->heritage);
    JsClassEntry* static_chain[32];
    int static_chain_length = 0;
    for (JsClassEntry* parent = setup->static_superclass;
            parent && static_chain_length < 32; parent = parent->superclass) {
        static_chain[static_chain_length++] = parent;
    }
    for (int class_index = static_chain_length - 1; class_index >= 0; class_index--) {
        JsClassEntry* parent = static_chain[class_index];
        MIR_reg_t parent_class = jm_emit_class_object_for_entry(mt, parent);
        if (!parent_class) parent_class = cls_obj;
        for (int method_index = 0; method_index < parent->method_count; method_index++) {
            JsMirClassMethodInstallPolicy policy = {
                cls_obj, parent_class, setup->class_proto_obj, parent, method_index,
                JS_MIR_CLASS_METHOD_INHERITED_STATIC,
                JS_MIR_COMPUTED_KEY_AFTER_FUNCTION
            };
            jm_emit_class_method_install(mt, &policy);
        }
    }
    for (int method_index = 0; method_index < ce->method_count; method_index++) {
        JsMirClassMethodInstallPolicy policy = {
            cls_obj, cls_obj, setup->class_proto_obj, ce, method_index,
            JS_MIR_CLASS_METHOD_OWN_STATIC,
            computed_key_before_function ? JS_MIR_COMPUTED_KEY_BEFORE_FUNCTION : JS_MIR_COMPUTED_KEY_AFTER_FUNCTION
        };
        jm_emit_class_method_install(mt, &policy);
    }
    jm_emit_class_constructor_property(mt, cls_obj, ce, true);
    setup->class_proto_obj = jm_emit_current_class_prototype(mt, cls_obj,
        setup->class_proto_obj);
}

void jm_emit_class_instance_setup_tail(JsMirTranspiler* mt, MIR_reg_t cls_obj,
        JsClassEntry* ce, MIR_reg_t proto_obj, MIR_reg_t ctor_super_val, bool heritage_is_null) {
    proto_obj = jm_emit_current_class_prototype(mt, cls_obj, proto_obj);
    if (heritage_is_null) {
        MIR_reg_t null_proto = jm_emit_null(mt);
        jm_call_void_2(mt, "js_set_prototype",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, null_proto));
    }
    for (int method_index = 0; method_index < ce->method_count; method_index++) {
        JsMirClassMethodInstallPolicy policy = {
            proto_obj, cls_obj, 0, ce, method_index,
            JS_MIR_CLASS_METHOD_OWN_INSTANCE,
            JS_MIR_COMPUTED_KEY_AFTER_FUNCTION
        };
        jm_emit_class_method_install(mt, &policy);
    }
    jm_call_void_2(mt, "js_set_class_instance_prototype",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_obj));
    jm_call_void_2(mt, "js_set_default_constructor_property",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
    jm_call_void_1(mt, "js_mark_all_non_enumerable",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_obj));
    jm_call_void_1(mt, "js_mark_all_non_enumerable",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
    if (ctor_super_val) {
        jm_call_void_2(mt, "js_set_class_superclass",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, ctor_super_val));
    }
    jm_emit_class_instance_field_metadata(mt, cls_obj, ce);
    jm_emit_class_computed_field_module_keys(mt, cls_obj, ce);
    jm_emit_class_instance_computed_field_metadata_keys(mt, cls_obj, ce);
    jm_emit_class_static_initializers(mt, cls_obj, ce, ctor_super_val);
}

// new expression: new TypedArray(len), new Array(len), new Object()
static MIR_reg_t jm_emit_dynamic_new_expr(JsMirTranspiler* mt, JsCallNode* call, int arg_count) {
    MIR_reg_t callee_value = jm_transpile_box_item(mt, call->callee);
    MIR_reg_t callee = jm_new_reg(mt, "new_callee", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, callee),
        MIR_new_reg_op(mt->ctx, callee_value)));
    bool has_spread = false;
    for (JsAstNode* chk = call->arguments; chk; chk = chk->next) {
        if (chk->node_type == JS_AST_NODE_SPREAD_ELEMENT) { has_spread = true; break; }
    }
    if (has_spread) {
        MIR_reg_t args_arr = jm_build_spread_args_array(mt, call->arguments);
        return jm_call_3(mt, "js_construct_array_like", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, callee),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, args_arr),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, callee));
    }
    MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
    // D6.2.2v2 makes newTarget an explicit construct operand; the generated
    // call cannot leak state into an adjacent construction on any exit.
    return jm_construct_value_into(mt, MIR_new_reg_op(mt->ctx, callee),
        args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
        MIR_new_int_op(mt->ctx, arg_count), MIR_new_reg_op(mt->ctx, callee));
}

MIR_reg_t jm_transpile_new_expr(JsMirTranspiler* mt, JsCallNode* call) {
    if (!call->callee) return jm_emit_null(mt);
    // D6.2.2v2: source spelling cannot select construction. Evaluate the
    // actual callee once and route every `new` through its stored capability.
    return jm_emit_dynamic_new_expr(mt, call, jm_count_args(call->arguments));
}
// switch statement
void jm_transpile_switch(JsMirTranspiler* mt, JsSwitchNode* sw) {
    JsMirLastClosureSnapshot saved_last_closure;
    jm_save_last_closure_snapshot(mt, &saved_last_closure);

    MIR_reg_t discriminant = jm_transpile_box_item(mt, sw->discriminant);
    MIR_label_t l_end = jm_new_label(mt);

    jm_push_scope(mt);

    MIR_reg_t saved_scope_env_reg = mt->scope_env_reg;
    int saved_scope_env_slot_count = mt->scope_env_slot_count;
    struct hashmap* switch_lexicals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    jm_collect_switch_lexical_names((JsAstNode*)sw, switch_lexicals);
    int switch_lexical_count = (int)hashmap_count(switch_lexicals);
    hashmap_free(switch_lexicals);
    if (switch_lexical_count > 0) {
        mt->scope_env_reg = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, switch_lexical_count));
        jm_register_owned_env(mt, mt->scope_env_reg);
        mt->scope_env_slot_count = switch_lexical_count;
    }
    jm_init_switch_tdz(mt, (JsAstNode*)sw);

    // Eval completion: reset to undefined (spec §14.12.4)
    jm_eval_cptn_reset(mt);

    // Push break label for the switch (break exits the switch)
    jm_push_loop_labels(mt, 0, l_end);

    // Collect case labels and default
    int case_count = 0;
    JsSwitchCaseNode* cases[128];
    JsAstNode* c = sw->cases;
    while (c && case_count < 128) {
        cases[case_count++] = (JsSwitchCaseNode*)c;
        c = c->next;
    }

    // Generate labels for each case body
    MIR_label_t case_labels[128];
    for (int i = 0; i < case_count; i++) {
        case_labels[i] = jm_new_label(mt);
    }

    // Test phase: for each non-default case, compare discriminant with test value
    // and branch to the corresponding case body label
    int default_idx = -1;
    for (int i = 0; i < case_count; i++) {
        if (!cases[i]->test) {
            default_idx = i;
            continue;
        }
        MIR_reg_t test_val = jm_transpile_box_item(mt, cases[i]->test);
        MIR_reg_t eq = jm_call_2(mt, "js_strict_equal", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, discriminant),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, test_val));
        // v23: js_strict_equal returns boxed boolean — extract low bit directly
        MIR_reg_t truthy = jm_new_reg(mt, "trthy", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_AND,
            MIR_new_reg_op(mt->ctx, truthy),
            MIR_new_reg_op(mt->ctx, eq),
            MIR_new_int_op(mt->ctx, 1)));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, case_labels[i]),
            MIR_new_reg_op(mt->ctx, truthy)));
    }

    // If no case matched, jump to default or end
    if (default_idx >= 0) {
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, case_labels[default_idx])));
    } else {
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    }

    // Body phase: emit each case body with fall-through semantics
    for (int i = 0; i < case_count; i++) {
        jm_emit_label(mt, case_labels[i]);
        JsAstNode* s = cases[i]->consequent;
        while (s) {
            jm_transpile_statement(mt, s);
            s = s->next;
        }
        // Fall through to next case (break will jump to l_end)
    }

    jm_emit_label(mt, l_end);
    // Case-local closure env registers are path-specific; after switch merge,
    // later callback readback must not use an env allocated in only one case.
    jm_restore_last_closure_snapshot(mt, &saved_last_closure);
    if (mt->loop_depth > 0) mt->loop_depth--;
    mt->scope_env_reg = saved_scope_env_reg;
    mt->scope_env_slot_count = saved_scope_env_slot_count;
    jm_pop_scope(mt);
}

// do-while statement
void jm_transpile_do_while(JsMirTranspiler* mt, JsDoWhileNode* dw) {
    MIR_label_t l_body = jm_new_label(mt);
    MIR_label_t l_test = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    jm_push_loop_labels(mt, l_test, l_end);
    mt->iteration_depth++;

    // Eval completion: Let V = undefined (spec §14.7.2.2)
    jm_eval_cptn_reset(mt);

    // Body first
    jm_emit_label(mt, l_body);
    if (dw->body) {
        if (dw->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            jm_push_scope(mt);
            jm_init_block_tdz(mt, dw->body);  // v20 TDZ
            JsBlockNode* blk = (JsBlockNode*)dw->body;
            JsAstNode* s = blk->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
            jm_pop_scope(mt);
        } else {
            jm_transpile_statement(mt, dw->body);
        }
    }

    // Test
    jm_emit_label(mt, l_test);
    jm_scope_env_reload_vars(mt);
    if (dw->test) {
        // v23b: unified condition handling
        MIR_reg_t truthy = jm_transpile_condition(mt, dw->test);
        jm_emit_loop_backedge_frame_reload(mt);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_body),
            MIR_new_reg_op(mt->ctx, truthy)));
    }

    jm_emit_label(mt, l_end);
    if (mt->iteration_depth > 0) mt->iteration_depth--;
    if (mt->loop_depth > 0) mt->loop_depth--;
}

MIR_reg_t jm_emit_await_value_reg(JsMirTranspiler* mt, MIR_reg_t promise_val,
        JsMirSuspendKind kind) {
    if (mt->in_generator && mt->in_async) {
        int next_state = jm_next_resume_state(mt, kind);
        if (next_state < 0) return promise_val;

        MIR_label_t suspend_label = jm_new_label(mt);
        MIR_label_t after_await_label = jm_new_label(mt);

        MIR_reg_t must_suspend_item = jm_call_1(mt, "js_async_must_suspend", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, promise_val));
        jm_emit_error_lane_route(mt, JS_MIR_COMPLETION_AWAIT_REJECTION);
        MIR_reg_t must_suspend = jm_emit_is_truthy(mt, must_suspend_item, NULL);

        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
            MIR_new_label_op(mt->ctx, suspend_label),
            MIR_new_reg_op(mt->ctx, must_suspend)));

        MIR_reg_t await_result = jm_new_reg(mt, "await_res", MIR_T_I64);
        MIR_reg_t fast_val = jm_call_0(mt, "js_async_get_resolved", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, await_result),
            MIR_new_reg_op(mt->ctx, fast_val)));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, after_await_label)));

        jm_emit_label(mt, suspend_label);
        jm_emit_suspend_env_save(mt);
        MIR_reg_t await_target = jm_call_0(mt, "js_async_get_resolved", MIR_T_I64);
        MIR_reg_t suspend_result = jm_call_2(mt, "js_gen_await_result", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, await_target),
            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)next_state));
        jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, suspend_result)));

        jm_emit_label(mt, mt->gen_state_labels[next_state]);
        jm_emit_resume_env_restore(mt);
        jm_emit_try_state_reset(mt);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, await_result),
            MIR_new_reg_op(mt->ctx, mt->gen_input_reg)));
        // Resume input is ordinary data for fulfillment but must re-enter the
        // merged ERROR lane for rejection; the host callback supplies the
        // rejection marker because both resumes share one state label.
        jm_publish_call_result(mt, mt->gen_input_reg);
        jm_emit_async_resume_refresh(mt);
        jm_emit_error_lane_route(mt, JS_MIR_COMPLETION_AWAIT_REJECTION);

        jm_emit_label_with_state(mt, after_await_label, JS_ERROR_LANE_CLEAN);
        return await_result;
    }

    return jm_call_1(mt, "js_await_sync", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, promise_val));
}

static void jm_precreate_loop_binding(JsMirTranspiler* mt, const char* vname,
        bool is_let_const_loop, bool mark_tdz) {
    JsMirVarEntry* existing = is_let_const_loop ? NULL : jm_find_var(mt, vname);
    if (existing && existing->reg) return;
    MIR_reg_t preg = jm_new_reg(mt, vname, MIR_T_I64);
    jm_set_var(mt, vname, preg);
    if (is_let_const_loop) {
        JsMirVarEntry* ve = jm_find_var(mt, vname);
        if (ve) {
            ve->is_let_const = true;
            if (mark_tdz) ve->tdz_active = true;
        }
    }
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, preg),
        MIR_new_int_op(mt->ctx, is_let_const_loop ? (int64_t)ITEM_JS_TDZ : ITEM_NULL_VAL)));
}

// for-of / for-in statement
// Uses fn_len + js_get_reference for arrays, or js_object_keys for objects
void jm_transpile_for_of(JsMirTranspiler* mt, JsForOfNode* fo) {
    // Js55 P19: save and reset last-closure tracking so a prior loop's closure
    // (still referenced via mt->last_closure_env_reg) cannot capture this loop's
    // let/const initializers. Without this, `const rab = ...` inside a second
    // `for (let ctor of ctors) {...}` would write back to the FIRST loop's
    // last evil's env, and reads of the body's bindings would route through
    // that stale env. See §12.14.
    JsMirLastClosureSnapshot saved_last_closure;
    jm_save_last_closure_snapshot(mt, &saved_last_closure);
    jm_clear_last_closure_snapshot(mt);

    jm_push_scope(mt);
    int saved_loop_scope_depth = mt->loop_scope_depth;
    mt->loop_scope_depth = mt->scope_depth;

    // Eval completion: Let V = undefined (spec §14.7.5.8 ForIn/OfBodyEvaluation)
    jm_eval_cptn_reset(mt);

    // Check if the loop variable is a destructuring pattern
    JsArrayPatternNode* destr_pattern = NULL;
    JsObjectPatternNode* obj_destr_pattern = NULL;
    JsAstNode* lhs_ref_node = NULL;
    bool lhs_call_target = false;
    const char* var_name = NULL;
    int var_len = 0;

    bool left_is_decl = fo->left && fo->left->node_type == JS_AST_NODE_VARIABLE_DECLARATION;
    if (left_is_decl) {
        JsVariableDeclarationNode* decl = (JsVariableDeclarationNode*)fo->left;
        if (decl->declarations && decl->declarations->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
            JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)decl->declarations;
            if (d->id && d->id->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* id = (JsIdentifierNode*)d->id;
                var_name = id->name->chars;
                var_len = (int)id->name->len;
            } else if (d->id && d->id->node_type == JS_AST_NODE_ARRAY_PATTERN) {
                destr_pattern = (JsArrayPatternNode*)d->id;
            } else if (d->id && d->id->node_type == JS_AST_NODE_OBJECT_PATTERN) {
                obj_destr_pattern = (JsObjectPatternNode*)d->id;
            }
        }
    } else if (fo->left && fo->left->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)fo->left;
        var_name = id->name->chars;
        var_len = (int)id->name->len;
    } else if (fo->left && (fo->left->node_type == JS_AST_NODE_ARRAY_PATTERN ||
                            fo->left->node_type == JS_AST_NODE_ARRAY_EXPRESSION)) {
        // for (const [a, b] of arr) — left is array_pattern directly
        destr_pattern = (JsArrayPatternNode*)fo->left;
    } else if (fo->left && (fo->left->node_type == JS_AST_NODE_OBJECT_PATTERN ||
                            fo->left->node_type == JS_AST_NODE_OBJECT_EXPRESSION)) {
        obj_destr_pattern = (JsObjectPatternNode*)fo->left;
    } else if (fo->left && fo->left->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        lhs_ref_node = fo->left;
    } else if (fo->left && fo->left->node_type == JS_AST_NODE_CALL_EXPRESSION) {
        lhs_call_target = true;
    }

    if (!var_name && !destr_pattern && !obj_destr_pattern && !lhs_ref_node && !lhs_call_target) {
        log_error("js-mir: for-of/for-in missing loop variable");
        mt->loop_scope_depth = saved_loop_scope_depth;
        jm_pop_scope(mt);
        jm_restore_last_closure_snapshot(mt, &saved_last_closure);
        return;
    }

    // Create loop variable (for simple case) or temp var (for destructuring)
    MIR_reg_t loop_var;
    bool is_for_in = (fo->node_type == JS_AST_NODE_FOR_IN_STATEMENT);
    bool is_for_await = !is_for_in && fo->is_await;
    bool is_let_const_loop = false;
    bool is_const_loop = false;
    if (fo->kind == 1 || fo->kind == 2) {  // 1=let, 2=const (from fo->kind, not fo->left type)
        is_let_const_loop = true;
        is_const_loop = (fo->kind == 2);
    }
    if (!is_let_const_loop && left_is_decl) {
        JsVariableDeclarationNode* decl = (JsVariableDeclarationNode*)fo->left;
        if (decl->kind == JS_VAR_LET || decl->kind == JS_VAR_CONST) {
            is_let_const_loop = true;
            is_const_loop = (decl->kind == JS_VAR_CONST);
        }
    }
    // v90: Also detect let/const via fo->kind when left is IDENTIFIER (e.g., for (let p in x))
    if (!is_let_const_loop && (fo->kind == 1 || fo->kind == 2)) {
        is_let_const_loop = true;
        is_const_loop = (fo->kind == 2);
    }
    if (var_name) {
        const char* vname = jm_format_name("_js_%.*s", var_len, var_name);
        // For var declarations and pre-declared identifiers, reuse the existing
        // register so the outer-scope variable is updated by the loop.
        // Only create a new register for let/const (block-scoped) bindings.
        JsMirVarEntry* existing = is_let_const_loop ? NULL : jm_find_var(mt, vname);
        if (existing && existing->reg) {
            loop_var = existing->reg;
        } else {
            loop_var = jm_new_reg(mt, vname, MIR_T_I64);
            jm_set_var(mt, vname, loop_var);
        }
        // Mark let/const loop variables so closure per-iteration binding works
        if (is_let_const_loop) {
            JsMirVarEntry* ve = jm_find_var(mt, vname);
            if (ve) {
                ve->is_let_const = true;
                // const loop declarations still receive a fresh value each
                // iteration, but writes from the loop body must throw.
                ve->is_const = is_const_loop;
                // async functions predeclare captured locals in a scope env;
                // loop lexical bindings need a fresh per-iteration cell instead.
                ve->in_scope_env = false;
                ve->scope_env_slot = -1;
                ve->scope_env_reg = 0;
                ve->from_env = false;
            }
        }
    } else {
        loop_var = jm_new_reg(mt, "_destr_elem", MIR_T_I64);
    }
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, loop_var),
        MIR_new_int_op(mt->ctx, (var_name && is_let_const_loop) ? (int64_t)ITEM_JS_TDZ : ITEM_NULL_VAL)));
    if (var_name && is_let_const_loop) {
        const char* vname = jm_format_name("_js_%.*s", var_len, var_name);
        JsMirVarEntry* ve = jm_find_var(mt, vname);
        if (ve) ve->tdz_active = true;
    }

    if (is_for_in && var_name && !is_let_const_loop) {
        JsAstNode* init_expr = fo->init;
        if (!init_expr && left_is_decl) {
            JsVariableDeclarationNode* decl = (JsVariableDeclarationNode*)fo->left;
            if (decl->kind == JS_VAR_VAR &&
                decl->declarations &&
                decl->declarations->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)decl->declarations;
                init_expr = d->init;
            }
        }
        if (init_expr) {
            const char* init_vname = jm_format_name("_js_%.*s", var_len, var_name);
            MIR_reg_t init_val = jm_transpile_box_item(mt, init_expr);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, loop_var),
                MIR_new_reg_op(mt->ctx, init_val)));
            jm_scope_env_mark_and_writeback(mt, init_vname, loop_var);
        }
    }

    // Pre-create destructuring variable registers
    // `var` is represented by zero, which also used to mean an assignment
    // head; preserve the parser's declaration marker so pattern names exist.
    bool left_creates_bindings = left_is_decl || fo->declares_binding;
    if (destr_pattern && left_creates_bindings) {
        JsAstNode* pe = destr_pattern->elements;
        while (pe) {
            if (pe->node_type == JS_AST_NODE_NULL) {
                // elision: no variable to pre-create
            } else if (pe->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* pid = (JsIdentifierNode*)pe;
                const char* pvname = jm_format_name("_js_%.*s", (int)pid->name->len, pid->name->chars);
                jm_precreate_loop_binding(mt, pvname, is_let_const_loop, true);
            } else if (pe->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
                // default value: [x = defaultVal, ...]
                JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)pe;
                if (ap->left && ap->left->node_type == JS_AST_NODE_IDENTIFIER) {
                    JsIdentifierNode* pid = (JsIdentifierNode*)ap->left;
                    const char* pvname = jm_format_name("_js_%.*s", (int)pid->name->len, pid->name->chars);
                    jm_precreate_loop_binding(mt, pvname, is_let_const_loop, true);
                }
            } else if (pe->node_type == JS_AST_NODE_OBJECT_PATTERN) {
                // nested object destructure: pre-create each property variable
                JsObjectPatternNode* op = (JsObjectPatternNode*)pe;
                JsAstNode* pprop = op->properties;
                while (pprop) {
                    if (pprop->node_type == JS_AST_NODE_PROPERTY) {
                        JsPropertyNode* pp = (JsPropertyNode*)pprop;
                        String* plocal = NULL;
                        if (pp->value && pp->value->node_type == JS_AST_NODE_IDENTIFIER)
                            plocal = ((JsIdentifierNode*)pp->value)->name;
                        else if (pp->key && pp->key->node_type == JS_AST_NODE_IDENTIFIER)
                            plocal = ((JsIdentifierNode*)pp->key)->name;
                        if (plocal) {
                            const char* pvname = jm_format_name("_js_%.*s",
                                (int)plocal->len, plocal->chars);
                            jm_precreate_loop_binding(mt, pvname, is_let_const_loop, true);
                        }
                    }
                    pprop = pprop->next;
                }
            } else if (pe->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                JsSpreadElementNode* sp = (JsSpreadElementNode*)pe;
                if (sp->argument && sp->argument->node_type == JS_AST_NODE_IDENTIFIER) {
                    JsIdentifierNode* pid = (JsIdentifierNode*)sp->argument;
                    const char* pvname = jm_format_name("_js_%.*s", (int)pid->name->len, pid->name->chars);
                    jm_precreate_loop_binding(mt, pvname, is_let_const_loop, true);
                }
            }
            pe = pe->next;
        }
    }

    // Pre-create object destructuring variable registers
    if (obj_destr_pattern && left_creates_bindings) {
        JsAstNode* prop = obj_destr_pattern->properties;
        while (prop) {
            if (prop->node_type == JS_AST_NODE_PROPERTY) {
                JsPropertyNode* p = (JsPropertyNode*)prop;
                String* local_name = NULL;
                if (p->value && p->value->node_type == JS_AST_NODE_IDENTIFIER) {
                    local_name = ((JsIdentifierNode*)p->value)->name;
                } else if (p->value && p->value->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
                    JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)p->value;
                    if (ap->left && ap->left->node_type == JS_AST_NODE_IDENTIFIER) {
                        local_name = ((JsIdentifierNode*)ap->left)->name;
                    }
                } else if (p->key && p->key->node_type == JS_AST_NODE_IDENTIFIER) {
                    local_name = ((JsIdentifierNode*)p->key)->name;
                }
                if (local_name) {
                    const char* pvname = jm_format_name("_js_%.*s",
                        (int)local_name->len, local_name->chars);
                    jm_precreate_loop_binding(mt, pvname, is_let_const_loop, true);
                }
            }
            prop = prop->next;
        }
    }

    // Evaluate right-hand side (the iterable)
    MIR_reg_t iterable = jm_transpile_box_item(mt, fo->right);

    // For for-in: get keys as array; for for-of: get lazy iterator
    if (is_for_in) {
        // for-in: collect keys into array, iterate by index (existing behavior)
        MIR_reg_t collection = jm_call_1(mt, "js_for_in_keys", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, iterable));

        MIR_reg_t len = jm_call_1(mt, "js_array_length", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, collection));

        MIR_reg_t idx = jm_new_reg(mt, "foridx", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, idx),
            MIR_new_int_op(mt->ctx, 0)));

        MIR_label_t l_test = jm_new_label(mt);
        MIR_label_t l_update = jm_new_label(mt);
        MIR_label_t l_end = jm_new_label(mt);

        jm_push_loop_labels(mt, l_update, l_end);
        mt->iteration_depth++;

        jm_emit_label(mt, l_test);
        // js_for_in_keys returns an engine-owned immutable snapshot. Keeping its
        // initial length avoids a loop-carried raw scalar masquerading as a boxed
        // call result in the precise-root machinery on large function back-edges.
        MIR_reg_t cmp = jm_new_reg(mt, "foricmp", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, cmp),
            MIR_new_reg_op(mt->ctx, idx), MIR_new_reg_op(mt->ctx, len)));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
            MIR_new_reg_op(mt->ctx, cmp)));

        MIR_reg_t idx_item = jm_box_int_reg(mt, idx);
        MIR_reg_t elem = jm_call_2(mt, "js_get_reference", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, collection),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_item));
        MIR_reg_t live_key = jm_call_2(mt, "js_for_in_key_is_live", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, iterable),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, elem));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_update),
            MIR_new_reg_op(mt->ctx, live_key)));
        if (lhs_call_target) {
            jm_transpile_box_item(mt, fo->left);
            jm_emit_error_lane_propagate_check(mt);
            MIR_reg_t msg = jm_box_string_literal(mt, "Invalid left-hand side in assignment", 36);
            jm_call_2(mt, "js_throw_named_error", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, 1),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, msg));
            jm_emit_error_lane_propagate_check(mt);
        } else {
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, loop_var), MIR_new_reg_op(mt->ctx, elem)));
        }

        if (lhs_ref_node) {
            JsMirReference lhs_ref = jm_emit_reference(mt, lhs_ref_node);
            jm_emit_error_lane_propagate_check(mt);
            jm_emit_put_value(mt, &lhs_ref, elem);
            jm_emit_error_lane_propagate_check(mt);
        }

        jm_emit_for_loop_var_writeback(mt, var_name, var_len,
            is_let_const_loop, loop_var);

        jm_emit_for_loop_destructure(mt, destr_pattern, obj_destr_pattern,
            loop_var, left_creates_bindings, 0);

        if (fo->body) {
            if (fo->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
                jm_push_scope(mt);
                jm_init_block_tdz(mt, fo->body);
                JsBlockNode* blk = (JsBlockNode*)fo->body;
                JsAstNode* s = blk->statements;
                while (s) { jm_transpile_statement(mt, s); s = s->next; }
                jm_pop_scope(mt);
            } else {
                jm_transpile_statement(mt, fo->body);
            }
        }

        jm_emit_label(mt, l_update);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, idx),
            MIR_new_reg_op(mt->ctx, idx), MIR_new_int_op(mt->ctx, 1)));
        jm_emit_loop_backedge_frame_reload(mt);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_test)));

        jm_emit_label(mt, l_end);
        if (mt->iteration_depth > 0) mt->iteration_depth--;
        if (mt->loop_depth > 0) mt->loop_depth--;
        mt->loop_scope_depth = saved_loop_scope_depth;
        jm_pop_scope(mt);
        jm_restore_last_closure_snapshot(mt, &saved_last_closure);
        return;
    }

    // for-of: use lazy iterator protocol (v29)
    // Get iterator from iterable
    MIR_reg_t iterator = is_for_await
        ? jm_call_1(mt, "js_get_async_iterator", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, iterable))
        : jm_emit_get_iterator(mt, iterable);
    JsMirIteratorFrame* iterator_frame =
        jm_for_of_iterator_at(mt, mt->for_of_depth);
    if (iterator_frame) {
        iterator_frame->iterator = iterator;
        mt->for_of_depth++;
    }

    // GetIterator runs before the synthetic cleanup context exists, so route
    // its result immediately while the call result still identifies this edge.
    // Delaying it until the shared handler would let later iterator/body calls
    // overwrite the carrier selected by the precise error lane.
    jm_emit_error_lane_propagate_check(mt);

    // In generators: register iterator and loop_var as env-stored variables
    int loop_var_env_slot = -1;
    if (mt->in_generator && mt->gen_env_reg) {
        int iter_slot = mt->gen_local_slot_count++;
        int lv_slot  = mt->gen_local_slot_count++;
        loop_var_env_slot = lv_slot;
        {
            JsVarScopeEntry entry;
            memset(&entry, 0, sizeof(entry));
            char backend_name[32];
            mir_format_backend_name(backend_name, sizeof(backend_name), 'i',
                (uint64_t)mt->em.label_counter);
            entry.name = mir_em_persist_cstr(&mt->em, backend_name).str;
            entry.var.reg = iterator;
            entry.var.from_env = true;
            entry.var.env_slot = iter_slot;
            entry.var.env_reg = mt->gen_env_reg;
            entry.var.typed_array_type = -1;
            jm_install_fresh_var_entry(mt, mt->scope_depth, &entry);
        }
        {
            JsVarScopeEntry entry;
            memset(&entry, 0, sizeof(entry));
            char backend_name[32];
            mir_format_backend_name(backend_name, sizeof(backend_name), 'v',
                (uint64_t)mt->em.label_counter);
            entry.name = mir_em_persist_cstr(&mt->em, backend_name).str;
            entry.var.reg = loop_var;
            entry.var.from_env = true;
            entry.var.env_slot = lv_slot;
            entry.var.env_reg = mt->gen_env_reg;
            entry.var.typed_array_type = -1;
            jm_install_fresh_var_entry(mt, mt->scope_depth, &entry);
        }
        mt->em.label_counter++;
    }

    MIR_label_t l_test = jm_new_label(mt);
    MIR_label_t l_update = jm_new_label(mt);
    MIR_label_t l_break = jm_new_label(mt);  // v29: break target calls iterator close
    MIR_label_t l_end = jm_new_label(mt);

    // v29: Use l_break as the break target so IteratorClose is called
    jm_push_loop_labels(mt, l_update, l_break);
    mt->iteration_depth++;
    if (mt->loop_depth > 0) {
        JsLoopLabels* loop = jm_loop_label_at(mt, mt->loop_depth - 1);
        if (loop) loop->iterator_to_close = iterator;
    }

    // Pre-initialize delayed-return registers BEFORE the loop test label,
    // so they are valid even when the iterable is empty and the loop body
    // never executes (the BT l_end branch skips the body entirely).
    MIR_label_t l_iter_error = jm_new_label(mt);
    bool pushed_try = false;
    MIR_reg_t forit_return_val = 0;
    MIR_reg_t forit_has_return = 0;
    MIR_label_t l_forit_ret = jm_new_label(mt);
    if (JsTryContext* tc = jm_try_context_push(mt)) {
        forit_return_val = jm_new_reg(mt, "_forit_ret", MIR_T_I64);
        forit_has_return = jm_new_reg(mt, "_forit_hret", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, forit_return_val),
            MIR_new_int_op(mt->ctx, 0)));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, forit_has_return),
            MIR_new_int_op(mt->ctx, 0)));
        // In generators: register forit_return_val and forit_has_return as env-stored
        // so they survive yield suspend/resume inside the for-of body
        if (mt->in_generator && mt->gen_env_reg) {
            int ret_slot = mt->gen_local_slot_count++;
            int hret_slot = mt->gen_local_slot_count++;
            {
                JsVarScopeEntry entry;
                memset(&entry, 0, sizeof(entry));
                char backend_name[32];
                mir_format_backend_name(backend_name, sizeof(backend_name), 'i',
                    (uint64_t)mt->em.label_counter);
                entry.name = mir_em_persist_cstr(&mt->em, backend_name).str;
                entry.var.reg = forit_return_val;
                entry.var.from_env = true;
                entry.var.env_slot = ret_slot;
                entry.var.env_reg = mt->gen_env_reg;
                entry.var.typed_array_type = -1;
                jm_install_fresh_var_entry(mt, mt->scope_depth, &entry);
            }
            {
                JsVarScopeEntry entry;
                memset(&entry, 0, sizeof(entry));
                char backend_name[32];
                mir_format_backend_name(backend_name, sizeof(backend_name), 'h',
                    (uint64_t)mt->em.label_counter);
                entry.name = mir_em_persist_cstr(&mt->em, backend_name).str;
                entry.var.reg = forit_has_return;
                entry.var.from_env = true;
                entry.var.env_slot = hret_slot;
                entry.var.env_reg = mt->gen_env_reg;
                entry.var.typed_array_type = -1;
                jm_install_fresh_var_entry(mt, mt->scope_depth, &entry);
            }
            mt->em.label_counter++;
        }
        tc->catch_label = l_iter_error;
        tc->finally_label = 0;
        tc->end_label = l_forit_ret;
        tc->return_val_reg = forit_return_val;
        tc->has_return_reg = forit_has_return;
        tc->has_catch = true;
        tc->has_finally = false;
        tc->inlining_finally = false;
        tc->yield_state_only = false;
        tc->finally_body = NULL;
        tc->saved_error_lane_flag_reg = 0;
        tc->saved_error_lane_val_reg = 0;
        pushed_try = true;
    }

    // Test: call iterator step.  for-await needs the raw iterator result so
    // it can await async-generator .next() promises before checking `done`.
    jm_emit_label(mt, l_test);
    MIR_reg_t step_result = 0;
    MIR_reg_t step_iter_result = 0;
    if (is_for_await) {
        step_iter_result = jm_call_1(mt, "js_async_iterator_step_result", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, iterator));
        jm_emit_for_of_step_error_lane_check(mt, pushed_try);
        step_iter_result = jm_emit_await_value_reg(mt, step_iter_result,
            JS_MIR_SUSPEND_IMPLICIT_AWAIT);
        jm_emit_error_lane_propagate_check(mt);
    } else {
        step_result = jm_emit_iterator_step(mt, iterator);
        jm_emit_for_of_step_error_lane_check(mt, pushed_try);
    }
    // Check if done (JS_ITER_DONE_SENTINEL — unique sentinel that won't collide with null/undefined/false)
    MIR_reg_t is_done = 0;
    if (is_for_await) {
        MIR_reg_t done_item = jm_call_1(mt, "js_iterator_result_done", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, step_iter_result));
        jm_emit_for_of_step_error_lane_check(mt, pushed_try);
        is_done = jm_call_1(mt, "js_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, done_item));
    } else {
        is_done = jm_emit_iterator_done_test(mt, step_result, "forofdone");
    }
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_end),
        MIR_new_reg_op(mt->ctx, is_done)));
    if (is_for_await) {
        step_result = jm_call_1(mt, "js_iterator_result_value", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, step_iter_result));
        step_result = jm_emit_await_value_reg(mt, step_result,
            JS_MIR_SUSPEND_IMPLICIT_AWAIT);
        jm_emit_error_lane_propagate_check(mt);
    }

    // Assign current value to loop variable
    if (lhs_call_target) {
        jm_transpile_box_item(mt, fo->left);
        jm_emit_error_lane_propagate_check(mt);
        MIR_reg_t msg = jm_box_string_literal(mt, "Invalid left-hand side in assignment", 36);
        jm_call_2(mt, "js_throw_named_error", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, 1),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, msg));
        jm_emit_error_lane_propagate_check(mt);
    } else {
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, loop_var), MIR_new_reg_op(mt->ctx, step_result)));
    }
    if (loop_var_env_slot >= 0) {
        // async state-machine closures read env-backed loop variables, so each
        // iteration must publish the freshly assigned value before body closures run.
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_mem_op(mt->ctx, MIR_T_I64,
                loop_var_env_slot * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1),
            MIR_new_reg_op(mt->ctx, loop_var)));
    }

    jm_emit_for_loop_var_writeback(mt, var_name, var_len,
        is_let_const_loop, loop_var);

    jm_emit_for_loop_destructure(mt, destr_pattern, obj_destr_pattern,
        loop_var, left_creates_bindings, 0);

    if (lhs_ref_node) {
        JsMirReference lhs_ref = jm_emit_reference(mt, lhs_ref_node);
        jm_emit_put_value(mt, &lhs_ref, loop_var);
        jm_emit_error_lane_propagate_check(mt);
    }

    // Body — wrapped in synthetic try-catch for IteratorClose on abrupt completion
    // (forit_return_val/forit_has_return already initialized above, before l_test)

    if (fo->body) {
        if (fo->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            jm_push_scope(mt);
            jm_init_block_tdz(mt, fo->body);  // v20 TDZ
            JsBlockNode* blk = (JsBlockNode*)fo->body;
            JsAstNode* s = blk->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
            jm_pop_scope(mt);
        } else {
            jm_transpile_statement(mt, fo->body);
        }
    }

    // Update: jump back to test (no index to increment — iterator handles state)
    jm_emit_label(mt, l_update);
    jm_emit_loop_backedge_frame_reload(mt);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_test)));

    // v29: Break target — call IteratorClose before exiting
    jm_emit_label(mt, l_break);
    // The synthetic body-catch context is only for abrupt completion from the
    // loop body. A close performed by the normal break completion belongs to
    // the enclosing completion context; otherwise a failing return() is
    // mistaken for another body exception and the iterator is closed twice.
    if (pushed_try) mt->try_ctx_depth--;
    jm_emit_iterator_close_checked(mt, iterator);
    if (pushed_try) mt->try_ctx_depth++;
    // fall through to l_end

    MIR_label_t l_after_iter_handlers = jm_new_label(mt);
    jm_emit_label(mt, l_end);
    // Normal loop completion used to fall through into the exception-only
    // iterator handlers, which invalidated the JS_ERROR_LANE_SET proof on clean exits.
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
        MIR_new_label_op(mt->ctx, l_after_iter_handlers)));
    // IteratorClose on exception from body — call return() then re-throw
    {
        MIR_label_t l_iter_error_done = jm_new_label(mt);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_iter_error_done)));
        jm_emit_label_with_state(mt, l_iter_error, JS_ERROR_LANE_SET);
        // Save the routed value, close the iterator, then restore that value;
        // closing may execute user code and overwrite the transition lane.
        MIR_reg_t routed_exc = jm_emit_error_lane_return(mt);
        MIR_reg_t saved_exc = jm_call_1(mt, "js_error_lane_payload", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, routed_exc));
        jm_emit_iterator_close(mt, iterator);
        jm_call_1(mt, "js_throw_value", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, saved_exc));
        // Re-propagate only after IteratorClose, and hide this context for the
        // route so the cleanup handler cannot jump back to itself.  The
        // rethrow helper always returns an ERROR Item; leaving the lane
        // UNKNOWN here would make the clean fallthrough inspect this
        // exception-only call result after an async resume.
        jm_error_lane_set_state(mt, JS_ERROR_LANE_SET);
        if (pushed_try) mt->try_ctx_depth--;
        jm_emit_error_lane_propagate_check(mt);
        if (pushed_try) mt->try_ctx_depth++;
        jm_emit_label(mt, l_iter_error_done);
    }
    // Only the loop's clean completion reaches this join; abrupt body and
    // cleanup paths rethrow directly above.  Preserve that proof for the
    // enclosing async statement sweep so it cannot test the cleanup handler's
    // path-local js_throw_value result on a normal resume.
    jm_emit_label_with_state(mt, l_after_iter_handlers, JS_ERROR_LANE_CLEAN);

    // The synthetic context is needed by the cleanup edge above, but must not
    // remain visible to subsequent loop/function completion lowering.
    if (pushed_try) mt->try_ctx_depth--;

    // Handle delayed return from inside for-of body.
    // jm_transpile_return stores val in forit_return_val, sets forit_has_return=1,
    // and jumps to l_forit_ret. Here we close the iterator and do the actual return.
    if (pushed_try) {
        // A delayed return is a completion, not an exception.  Both its
        // zero-flag fallthrough and the ordinary loop exit therefore remain
        // clean; resetting this join to UNKNOWN would revive the stale
        // cleanup rethrow register in the enclosing async statement sweep.
        jm_emit_label_with_state(mt, l_forit_ret, JS_ERROR_LANE_CLEAN);
        MIR_label_t l_no_delayed_ret = jm_new_label(mt);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
            MIR_new_label_op(mt->ctx, l_no_delayed_ret),
            MIR_new_reg_op(mt->ctx, forit_has_return)));
        // Return completion closes this iterator before the value leaves the
        // loop. The body-catch context was removed above, so return() errors
        // override the source return value and route to the outer context.
        jm_emit_iterator_close_checked(mt, iterator);
        if (!jm_emit_delayed_return_completion(mt, forit_return_val,
                JS_MIR_COMPLETION_RETURN_THROUGH_CLEANUP)) {
            // No outer try — emit actual return
            MIR_reg_t native_ret = jm_native_return_reg(mt, forit_return_val);
            jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
                MIR_new_reg_op(mt->ctx, native_ret)));
        }
        jm_emit_label_with_state(mt, l_no_delayed_ret, JS_ERROR_LANE_CLEAN);
    }

    if (mt->for_of_depth > 0) mt->for_of_depth--;
    if (mt->iteration_depth > 0) mt->iteration_depth--;
    if (mt->loop_depth > 0) mt->loop_depth--;
    mt->loop_scope_depth = saved_loop_scope_depth;
    jm_pop_scope(mt);

    // Js55 P19: restore last-closure tracking saved at entry.
    jm_restore_last_closure_snapshot(mt, &saved_last_closure);
}

void jm_transpile_return(JsMirTranspiler* mt, JsReturnNode* ret) {
    MIR_reg_t val;

    // Phase 4: In native function, return native value directly
    if (mt->in_native_func && mt->current_fc) {
        TypeId ret_type = mt->current_fc->return_type;

        // TCO: set tail position so recursive calls in the argument can be converted to goto
        bool saved_tail = mt->in_tail_position;
        if (mt->tco_func) {
            mt->in_tail_position = true;
            mt->tco_jumped = false;
        }

        if (ret->argument) {
            TypeId expr_type = jm_get_effective_type(mt, ret->argument);
            if (jm_is_native_type(expr_type)) {
                // Expression already returns native — convert to target type
                val = jm_transpile_as_native(mt, ret->argument, expr_type, ret_type);
            } else {
                // Expression returns boxed — unbox to native
                MIR_reg_t boxed = jm_transpile_box_item(mt, ret->argument);
                if (ret_type == LMD_TYPE_FLOAT) {
                    val = jm_emit_unbox_float(mt, boxed);
                } else {
                    val = jm_emit_unbox_int(mt, boxed);
                }
            }
        } else {
            // return; → return 0 (native)
            MIR_type_t mtype = (ret_type == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
            val = jm_new_reg(mt, "ret0", mtype);
            if (ret_type == LMD_TYPE_FLOAT) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                    MIR_new_reg_op(mt->ctx, val), MIR_new_double_op(mt->ctx, 0.0)));
            } else {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, val), MIR_new_int_op(mt->ctx, 0)));
            }
        }

        mt->in_tail_position = saved_tail;

        // If TCO converted the call to a goto, skip the ret — it's dead code
        if (mt->tco_jumped) {
            mt->tco_jumped = false;
            return;
        }

        jm_emit_eval_local_pop_if_needed(mt);
        jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, val)));
        return;
    }

    if (ret->argument) {
        val = jm_transpile_box_item(mt, ret->argument);
    } else {
        // v18: bare return produces undefined, not null
        val = jm_emit_undefined(mt);
    }

    // The nearest active for-of owns a synthetic delayed-return landing pad.
    // Leave its close to that pad so a failing return() can propagate without
    // re-entering the body's exception-only IteratorClose handler.
    bool defer_nearest_iterator_close = mt->for_of_depth > 0;
    for (int i = mt->loop_depth - 1; i >= 0; i--) {
        JsLoopLabels* loop = jm_loop_label_at(mt, i);
        if (loop && loop->iterator_to_close) {
            if (defer_nearest_iterator_close) {
                defer_nearest_iterator_close = false;
                continue;
            }
            jm_emit_iterator_close(mt, loop->iterator_to_close);
        }
    }

    // v15: In generator/async state machines, return [value, -1] to signal done.
    // If the return is inside a try/finally, delay it so the finally body runs
    // and can override the completion.
    if (mt->in_generator) {
        if (jm_emit_delayed_return_completion(mt, val, JS_MIR_COMPLETION_RETURN)) return;
        MIR_reg_t done_result = jm_call_2(mt, "js_gen_yield_result", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)-1));
        jm_emit_eval_local_pop_if_needed(mt);
        jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, done_result)));
        return;
    }

    // Phase 5: In async function, wrap return value in Promise.resolve()
    if (mt->in_async) {
        val = jm_call_1(mt, "js_promise_resolve", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
    }

    // If inside a try block, delay the return and jump to finally/end
    if (jm_emit_delayed_return_completion(mt, val, JS_MIR_COMPLETION_RETURN)) return;

    jm_emit_eval_local_pop_if_needed(mt);
    jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, val)));
}

static bool jm_statement_is_using_decl(JsAstNode* stmt) {
    if (!stmt || stmt->node_type != JS_AST_NODE_VARIABLE_DECLARATION) return false;
    JsVariableDeclarationNode* decl = (JsVariableDeclarationNode*)stmt;
    return decl->is_using;
}

static void jm_emit_using_dispose_decl(JsMirTranspiler* mt, JsVariableDeclarationNode* decl) {
    MIR_reg_t resources[64];
    int resource_count = 0;
    for (JsAstNode* n = decl ? decl->declarations : NULL; n && resource_count < 64; n = n->next) {
        if (n->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
        JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)n;
        if (!d->id || d->id->node_type != JS_AST_NODE_IDENTIFIER) continue;
        JsIdentifierNode* id = (JsIdentifierNode*)d->id;
        const char* vname = jm_format_name("_js_%.*s", (int)id->name->len, id->name->chars);
        JsMirVarEntry* var = jm_find_var(mt, vname);
        if (!var || !var->reg) continue;
        MIR_reg_t value = var->reg;
        if (jm_is_native_type(var->type_id)) {
            value = jm_box_native(mt, var->reg, var->type_id);
        }
        resources[resource_count++] = value;
    }
    for (int i = resource_count - 1; i >= 0; i--) {
        (void)jm_call_1(mt, "js_using_dispose", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, resources[i]));
    }
}

void jm_transpile_statement_list_with_using(JsMirTranspiler* mt, JsAstNode* first);

static void jm_transpile_using_tail(JsMirTranspiler* mt, JsAstNode* tail,
        JsVariableDeclarationNode* using_decl) {
    MIR_label_t finally_label = jm_new_label(mt);
    MIR_label_t end_label = jm_new_label(mt);
    MIR_reg_t return_val_reg = jm_new_reg(mt, "_using_ret", MIR_T_I64);
    MIR_reg_t has_return_reg = jm_new_reg(mt, "_using_has_ret", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, return_val_reg),
        MIR_new_int_op(mt->ctx, 0)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, has_return_reg),
        MIR_new_int_op(mt->ctx, 0)));

    if (JsTryContext* tc = jm_try_context_push(mt)) {
        tc->catch_label = 0;
        tc->finally_label = finally_label;
        tc->end_label = end_label;
        tc->return_val_reg = return_val_reg;
        tc->has_return_reg = has_return_reg;
        tc->has_catch = false;
        tc->has_finally = true;
        tc->inlining_finally = false;
        tc->yield_state_only = false;
        tc->finally_body = NULL;
        tc->saved_error_lane_flag_reg = 0;
        tc->saved_error_lane_val_reg = 0;
    }

    MIR_reg_t saved_with_depth = jm_call_0(mt, "js_with_save_depth", MIR_T_I64);

    jm_transpile_statement_list_with_using(mt, tail);

    jm_emit_error_lane_route(mt, JS_MIR_COMPLETION_THROW);

    if (mt->try_ctx_depth > 0) mt->try_ctx_depth--;
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, finally_label)));

    jm_emit_label(mt, finally_label);
    jm_call_void_1(mt, "js_with_restore_depth", MIR_T_I64,
        MIR_new_reg_op(mt->ctx, saved_with_depth));

    MIR_reg_t saved_error_lane_flag = jm_emit_error_lane_test(mt);
    MIR_reg_t saved_error_lane_source = jm_emit_error_lane_return(mt);
    MIR_reg_t saved_error_lane_val = jm_call_1(mt, "js_error_lane_payload", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, saved_error_lane_source));

    jm_emit_using_dispose_decl(mt, using_decl);

    MIR_reg_t new_exc = jm_emit_error_lane_test(mt);
    MIR_label_t skip_restore = jm_new_label(mt);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
        MIR_new_label_op(mt->ctx, skip_restore),
        MIR_new_reg_op(mt->ctx, new_exc)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
        MIR_new_label_op(mt->ctx, skip_restore),
        MIR_new_reg_op(mt->ctx, saved_error_lane_flag)));
    jm_call_1(mt, "js_throw_value", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, saved_error_lane_val));
    jm_emit_label(mt, skip_restore);

    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
        MIR_new_label_op(mt->ctx, end_label),
        MIR_new_reg_op(mt->ctx, has_return_reg)));

    jm_emit_label(mt, end_label);
    MIR_label_t no_ret_label = jm_new_label(mt);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
        MIR_new_label_op(mt->ctx, no_ret_label),
        MIR_new_reg_op(mt->ctx, has_return_reg)));
    MIR_reg_t native_ret = jm_native_return_reg(mt, return_val_reg);
    jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
        MIR_new_reg_op(mt->ctx, native_ret)));
    jm_emit_label(mt, no_ret_label);

    jm_emit_error_lane_exit(mt);
}

void jm_transpile_statement_list_with_using(JsMirTranspiler* mt, JsAstNode* first) {
    JsAstNode* s = first;
    while (s) {
        if (jm_statement_is_using_decl(s)) {
            JsVariableDeclarationNode* decl = (JsVariableDeclarationNode*)s;
            jm_transpile_var_decl(mt, decl);
            jm_transpile_using_tail(mt, s->next, decl);
            return;
        }
        jm_transpile_statement(mt, s);
        // Expression statements can still carry an ERROR Item even when no
        // lexical try exists; route that lane at every statement boundary so
        // discarded call results cannot turn a throw into normal continuation.
        jm_emit_error_lane_propagate_check(mt);
        if (jm_error_lane_state(mt) == JS_ERROR_LANE_UNREACHABLE) {
            // A routed abrupt completion has no fallthrough edge. Continuing
            // to lower dead statements can reuse the transient call result
            // register and corrupt the next reachable join (D8.4.3).
            break;
        }
        s = s->next;
    }
}

// Statement dispatcher
void jm_transpile_statement(JsMirTranspiler* mt, JsAstNode* stmt) {
    if (!stmt) return;

    switch (stmt->node_type) {
    case JS_AST_NODE_VARIABLE_DECLARATION:
        jm_transpile_var_decl(mt, (JsVariableDeclarationNode*)stmt);
        break;
    case JS_AST_NODE_FUNCTION_DECLARATION: {
        // Annex B §B.3.3.1: In sloppy mode, function declarations inside blocks/if/switch
        // are var-hoisted with undefined (Phase 2), and when evaluated, the function
        // value is written back to the var-scoped binding.
        // For top-level function declarations (direct children of function body),
        // Phase 3 already hoisted them with the function value. Re-binding here
        // is harmless (same value) but ensures consistent behavior.
        //
        // Skip condition: if enclosing function has a parameter with the same name,
        // do NOT overwrite the parameter binding (B.3.3.1 step 2.ii).
        JsFunctionNode* fn_decl = (JsFunctionNode*)stmt;
        if (fn_decl->name) {
            const char* fn_vname = jm_format_name("_js_%.*s", (int)fn_decl->name->len, fn_decl->name->chars);
            // Check Annex B skip condition: parameter name collision
            JsFunctionNode* enclosing_fn = mt->current_fc ? mt->current_fc->node : NULL;
            bool current_body_direct = enclosing_fn &&
                jm_current_function_has_direct_body_function_binding(enclosing_fn, fn_vname);
            bool effective_strict = mt->is_global_strict || mt->is_module ||
                (mt->current_fc && mt->current_fc->is_strict) ||
                (enclosing_fn && jm_has_use_strict_directive(enclosing_fn));
            if (effective_strict && !jm_statement_function_decl_is_direct_binding(fn_decl) &&
                !current_body_direct) {
                break;
            }
            if (enclosing_fn && jm_func_has_param_named(enclosing_fn,
                    fn_decl->name->chars, (int)fn_decl->name->len)) {
                break;
            }
            if (mt->current_fc && mt->current_fc->uses_arguments &&
                strcmp(fn_vname, "_js_arguments") == 0 &&
                !jm_statement_function_decl_is_direct_binding(fn_decl) &&
                !current_body_direct) {
                break;
            }
            // Direct function-body declarations shadow outer names; Annex B
            // replacement is the only path that should search parent scopes.
            JsMirVarEntry* existing = current_body_direct ?
                jm_find_var_in_scope_depth(mt, fn_vname, mt->scope_depth) :
                jm_find_var(mt, fn_vname);
            // Annex B runtime replacement targets the var/function environment,
            // not an intervening block/catch binding.  In `catch (f) { { function f(){} } }`,
            // the simple catch parameter is intentionally allowed by B.3.5 while
            // the outer function-scoped `var f` binding must receive the function.
            if (mt->current_fc && mt->scope_depth > 1) {
                JsMirVarEntry* var_env_existing = jm_find_enclosing_var_env_binding(mt, fn_vname);
                if (var_env_existing && !var_env_existing->is_let_const &&
                    !jm_has_outer_block_func_binding(mt, fn_vname)) {
                    existing = var_env_existing;
                }
            }
            // Annex B skip: if the existing binding is let/const, don't overwrite
            // (B.3.3.1/B.3.3.3 — would produce early error if replaced with var).
            // A sloppy block-level function declaration also has a block/switch
            // lexical binding; that binding must not suppress the Annex B
            // var/global environment update for the same function declaration.
            // Js55 P19: for block_func_decl bindings, keep `existing` so we can
            // UPDATE the binding_reg here with a freshly-created closure that
            // captures any let/const initializers that came before the textual
            // function-decl position.
            JsMirVarEntry* p19_block_func_existing = NULL;
            if (existing && existing->is_let_const) {
                if (existing->from_block_func_decl) {
                    p19_block_func_existing = existing;
                    existing = NULL;
                } else {
                    break;
                }
            }
            // Also check module_consts for let/const conflict (eval context stores
            // let/const as MCONST_MODVAR, not local MIR vars)
            JsModuleConstEntry* annexb_modvar = NULL;
            bool annexb_suppressed = false;
            if (!existing && mt->module_consts) {
                JsModuleConstEntry mclookup;
                mclookup.name = jm_persist_name(fn_vname);
                JsModuleConstEntry* mvc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                if (mvc && mvc->const_type == MCONST_MODVAR && mvc->var_kind == 0) {
                    annexb_modvar = mvc;
                }
                if (mvc && mvc->annexb_suppressed) {
                    annexb_suppressed = true;
                }
                if (mvc && mvc->const_type == MCONST_MODVAR && (mvc->var_kind == 1 || mvc->var_kind == 2)) {
                    break;
                }
            }
            if (annexb_suppressed) break;
            JsFuncCollected* fc_decl = jm_find_collected_func(mt, fn_decl);
            if (fc_decl && fc_decl->func_item) {
                // Js52 R2: for top-level / direct-binding function declarations,
                // Phase 3 already hoisted the closure value into the existing
                // binding.  Re-creating it here would mint a SECOND JsFunction
                // instance with its own (empty) prototype field; the body's
                // self-reference still points at the hoisted instance, so
                // `module.exports = y` ends up with one instance while
                // `y.prototype.method = ...` mutates the other — observed as
                // ajv's `obj.addMetaSchema` resolving to undefined inside the
                // constructor while it looks defined from outside.
                //
                // The non-direct (block-scoped) Annex B case still needs the
                // recreate-and-rebind because Phase 3 did NOT hoist it.
                bool is_direct = jm_statement_function_decl_is_direct_binding(fn_decl);
                bool async_state_machine_body = mt->in_generator && mt->in_async;
                if (existing && is_direct && !async_state_machine_body) {
                    // Skip recreation; existing already holds the hoisted closure.
                    jm_scope_env_mark_and_writeback(mt, fn_vname, existing->reg);
                    break;
                }
                MIR_reg_t fn_reg = jm_create_func_or_closure(mt, fc_decl);
                // Js55 P19: also refresh the block_func_decl binding so the
                // closure captures any subsequently-initialized let/const vars.
                if (p19_block_func_existing) {
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, p19_block_func_existing->reg),
                        MIR_new_reg_op(mt->ctx, fn_reg)));
                    jm_scope_env_mark_and_writeback(mt, fn_vname, p19_block_func_existing->reg);
                }
                if (existing) {
                    // Update existing var-scoped binding with function value
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, existing->reg),
                        MIR_new_reg_op(mt->ctx, fn_reg)));
                    // Reset type to ANY since the register now holds a function, not its
                    // previous type (e.g. INT from `var f = 42`). Modify the entry
                    // in-place since it lives in the function scope, not the current block.
                    existing->type_id = LMD_TYPE_ANY;
                    existing->mir_type = MIR_T_I64;
                    jm_scope_env_mark_and_writeback(mt, fn_vname, existing->reg);
                } else if (annexb_modvar) {
                    jm_call_void_2(mt, "js_set_module_var",
                        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)annexb_modvar->int_val),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_reg));
                    if (annexb_modvar->is_nested_func_hoist && !annexb_modvar->is_iife_var) {
                        MIR_reg_t key_reg = jm_box_property_name_literal(mt,
                            fn_decl->name->chars, (int)fn_decl->name->len);
                        jm_emit_annexb_global_export(mt, key_reg, fn_reg);
                    }
                } else {
                    MIR_reg_t var_reg = jm_new_reg(mt, fn_vname, MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, var_reg),
                        MIR_new_reg_op(mt->ctx, fn_reg)));
                    jm_set_var(mt, fn_vname, var_reg);
                    jm_scope_env_mark_and_writeback(mt, fn_vname, var_reg);
                }
                // Annex B: also write closure to module_var if hoisted as MCONST_MODVAR
                // (closures that capture let/const vars are resolved via js_get_module_var)
                if (mt->module_consts) {
                    JsModuleConstEntry mvlookup;
                    mvlookup.name = jm_persist_name(fn_vname);
                    JsModuleConstEntry* mvc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mvlookup);
                        if (mvc && mvc->const_type == MCONST_MODVAR && mvc->var_kind == 0 &&
                            !mvc->annexb_suppressed && mvc != annexb_modvar) {
                        jm_call_void_2(mt, "js_set_module_var",
                            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mvc->int_val),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_reg));
                        if (mvc->is_nested_func_hoist && !mvc->is_iife_var) {
                            MIR_reg_t key_reg = jm_box_property_name_literal(mt,
                                fn_decl->name->chars, (int)fn_decl->name->len);
                            jm_emit_annexb_global_export(mt, key_reg, fn_reg);
                        }
                    }
                }
            }
        }
        break;
    }
    case JS_AST_NODE_CLASS_DECLARATION:
        // For top-level script: already handled in Phase 3 pre-pass; skip.
        // For function bodies (IIFE or regular): initialize the class object here
        // since Phase 3 only processes program->body, not function bodies.
        if (mt->module_consts) {
            JsClassNode* cls_node = (JsClassNode*)stmt;
            if (cls_node->name) {
                JsClassEntry* ce = NULL;
                for (int ci = 0; ci < mt->class_count; ci++) {
                    if (mt->class_entries[ci].node == cls_node) {
                        ce = &mt->class_entries[ci];
                        break;
                    }
                }
                if (!ce) {
                    ce = jm_find_class(mt, cls_node->name->chars, (int)cls_node->name->len);
                }
                if (ce) {
                    // TDZ: class x extends x {} → throw ReferenceError
                    jm_emit_class_self_extends_check(mt, ce, cls_node->name);
                    MIR_reg_t cls_obj = jm_call_0(mt, "js_new_class_function", MIR_T_I64);
                    // Class initialization performs allocating metadata and
                    // method setup before its lexical binding is authoritative.
                    jm_create_gc_root_slot(mt, cls_obj);
                    jm_emit_set_private_class_index(mt, cls_obj, ce);
                    jm_emit_set_class_source(mt, cls_obj, cls_node);
                    // Store class object in module var
                    const char* vname = jm_format_name("_js_%.*s", (int)cls_node->name->len, cls_node->name->chars);
                    JsMirVarEntry* local_class_binding = jm_find_var(mt, vname);
                    if (local_class_binding && local_class_binding->is_let_const) {
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, local_class_binding->reg),
                            MIR_new_reg_op(mt->ctx, cls_obj)));
                        local_class_binding->tdz_active = false;
                        local_class_binding->type_id = LMD_TYPE_ANY;
                        local_class_binding->mir_type = MIR_T_I64;
                        // A nested lexical class can shadow a same-named promoted binding;
                        // publish it through the class declaration's keyed env slot.
                        jm_scope_env_mark_and_writeback_binding(mt, vname, stmt,
                            local_class_binding->reg);
                    }
                    JsModuleConstEntry mclookup;
                    mclookup.name = jm_persist_name(vname);
                    JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                    if (mc && (mc->const_type == MCONST_CLASS || mc->const_type == MCONST_MODVAR)) {
                        // Static member lowering reads the class module slot.
                        // Nested lexical classes still need that mirror even
                        // when their authoritative binding is a local register.
                        jm_call_void_2(mt, "js_set_module_var",
                            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mc->int_val),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
                    }
                    if (ce->inner_module_var_index >= 0) {
                        jm_call_void_2(mt, "js_set_module_var",
                            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)ce->inner_module_var_index),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
                    }
                    if (mt->is_eval_direct) {
                        MIR_reg_t evalscript_active = jm_call_0(mt, "js_262_eval_script_is_active", MIR_T_I64);
                        MIR_label_t skip_global_class_lex = jm_new_label(mt);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
                            MIR_new_label_op(mt->ctx, skip_global_class_lex),
                            MIR_new_reg_op(mt->ctx, evalscript_active)));
                        MIR_reg_t class_key = jm_box_property_name_literal(mt,
                            cls_node->name->chars, cls_node->name->len);
                        // Global class declarations are lexical bindings, not
                        // global object properties; evalScript must expose them
                        // to later identifier resolution while hasOwnProperty is false.
                        jm_call_void_3(mt, "js_global_lexical_declare",
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, class_key),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                            MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
                        jm_emit_label(mt, skip_global_class_lex);
                    }
                    if (mt->in_main && !mt->is_module && !mt->is_eval_direct &&
                        mt->scope_depth <= 1) {
                        MIR_reg_t class_key = jm_box_property_name_literal(mt,
                            cls_node->name->chars, cls_node->name->len);
                        // Only a class declared directly at script top level
                        // (main body scope) is a global lexical binding. A class
                        // nested in a block/switch case is block-scoped and must
                        // not leak to the global lexical side table, otherwise it
                        // stays resolvable after the block exits
                        // (switch/scope-lex-class expects a ReferenceError).
                        jm_call_void_3(mt, "js_global_lexical_declare",
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, class_key),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                            MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
                    }
                    JsMirClassSetup class_setup;
                    jm_emit_class_setup(mt, cls_obj, ce, (JsAstNode*)cls_node, true, &class_setup);
                    MIR_reg_t ctor_super_val = class_setup.ctor_super_val;
                    MIR_reg_t class_proto_obj = class_setup.class_proto_obj;
                    JsAstNode* heritage = class_setup.heritage;
                    JsClassEntry* static_superclass = class_setup.static_superclass;
                    // Create the instance prototype with instance methods and store as prototype
                    {
                        MIR_reg_t proto_obj = class_proto_obj;
                        bool heritage_is_null = heritage && (heritage->node_type == JS_AST_NODE_NULL ||
                            (heritage->node_type == JS_AST_NODE_LITERAL &&
                             ((JsLiteralNode*)heritage)->literal_type == JS_LITERAL_NULL));
                        jm_call_void_2(mt, "js_set_default_constructor_property",
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_obj),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
                        {
                            JsClassEntry* sc = static_superclass;
                            MIR_reg_t last_proto = proto_obj;
                            if (sc) {
                                MIR_reg_t super_val = jm_link_static_super_prototype(mt,
                                    cls_obj, last_proto, sc);
                                ctor_super_val = super_val;
                            }
                            heritage_is_null = heritage && (heritage->node_type == JS_AST_NODE_NULL ||
                                (heritage->node_type == JS_AST_NODE_LITERAL &&
                                 ((JsLiteralNode*)heritage)->literal_type == JS_LITERAL_NULL));
                            if (!heritage_is_null && heritage && heritage->node_type == JS_AST_NODE_IDENTIFIER) {
                                JsIdentifierNode* heritage_id = (JsIdentifierNode*)heritage;
                                heritage_is_null = heritage_id->name && heritage_id->name->len == 4 &&
                                    strncmp(heritage_id->name->chars, "null", 4) == 0;
                            }
                            if (!sc && heritage && !heritage_is_null) {
                                MIR_reg_t super_val = jm_transpile_box_item(mt, heritage);
                                jm_call_1(mt, "js_check_class_heritage_constructor", MIR_T_I64,
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, super_val));
                                jm_emit_error_lane_propagate_check(mt);
                                MIR_reg_t sp_key = jm_box_property_name_literal(mt,
                                    "prototype", 9);
                                MIR_reg_t sp_obj = jm_call_2(mt, "js_get_key_default", MIR_T_I64,
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, super_val),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_key));
                                jm_call_1(mt, "js_check_class_prototype_parent", MIR_T_I64,
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_obj));
                                jm_emit_error_lane_propagate_check(mt);
                                jm_call_void_2(mt, "js_set_prototype",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, last_proto),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_obj));
                                ctor_super_val = super_val;
                            }
                        }
                        jm_emit_class_instance_setup_tail(mt, cls_obj, ce, proto_obj,
                            ctor_super_val, heritage_is_null);
                    }
                }
            }
        }
        break;
    case JS_AST_NODE_IF_STATEMENT:
        jm_transpile_if(mt, (JsIfNode*)stmt);
        break;
    case JS_AST_NODE_WHILE_STATEMENT:
        jm_transpile_while(mt, (JsWhileNode*)stmt);
        break;
    case JS_AST_NODE_FOR_STATEMENT:
        jm_transpile_for(mt, (JsForNode*)stmt);
        break;
    case JS_AST_NODE_DO_WHILE_STATEMENT:
        jm_transpile_do_while(mt, (JsDoWhileNode*)stmt);
        break;
    case JS_AST_NODE_SWITCH_STATEMENT:
        jm_transpile_switch(mt, (JsSwitchNode*)stmt);
        break;
    case JS_AST_NODE_FOR_OF_STATEMENT:
    case JS_AST_NODE_FOR_IN_STATEMENT:
        jm_transpile_for_of(mt, (JsForOfNode*)stmt);
        break;
    case JS_AST_NODE_RETURN_STATEMENT:
        jm_transpile_return(mt, (JsReturnNode*)stmt);
        break;
    case JS_AST_NODE_BREAK_STATEMENT: {
        JsBreakContinueNode* brk = (JsBreakContinueNode*)stmt;
        jm_emit_break_completion(mt, brk);
        break;
    }
    case JS_AST_NODE_CONTINUE_STATEMENT: {
        JsBreakContinueNode* cont = (JsBreakContinueNode*)stmt;
        jm_emit_continue_completion(mt, cont);
        break;
    }
    case JS_AST_NODE_LABELED_STATEMENT: {
        JsLabeledStatementNode* labeled = (JsLabeledStatementNode*)stmt;
        if (labeled->body) {
            // check if body is a loop/switch — if so, the loop itself will push to loop_stack
            // and we just need to annotate the label on that entry
            JsAstNodeType body_type = labeled->body->node_type;
            bool is_loop_or_switch = (body_type == JS_AST_NODE_FOR_STATEMENT ||
                                      body_type == JS_AST_NODE_WHILE_STATEMENT ||
                                      body_type == JS_AST_NODE_DO_WHILE_STATEMENT ||
                                      body_type == JS_AST_NODE_FOR_OF_STATEMENT ||
                                      body_type == JS_AST_NODE_FOR_IN_STATEMENT ||
                                      body_type == JS_AST_NODE_SWITCH_STATEMENT);
            if (is_loop_or_switch) {
                // set pending label so the loop's jm_push_loop_labels picks it up
                mt->pending_label_name = labeled->label;
                mt->pending_label_len = labeled->label_len;
                jm_transpile_statement(mt, labeled->body);
            } else {
                // non-loop labeled block: push a label entry with break_label for "break label;"
                MIR_label_t l_end = jm_new_label(mt);
                mt->pending_label_name = labeled->label;
                mt->pending_label_len = labeled->label_len;
                jm_push_loop_labels(mt, 0, l_end);
                jm_transpile_statement(mt, labeled->body);
                if (mt->loop_depth > 0) mt->loop_depth--;
                jm_emit_label(mt, l_end);
            }
        }
        break;
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        // Js55 P19: save and reset last-closure tracking so a prior block's
        // closure cannot capture this block's let/const initializers via
        // jm_write_last_closure_capture_if_matching. See §12.14.
        JsMirLastClosureSnapshot blk_saved_last_closure;
        jm_save_last_closure_snapshot(mt, &blk_saved_last_closure);
        jm_clear_last_closure_snapshot(mt);

        jm_push_scope(mt);
        jm_init_block_tdz(mt, stmt);  // v20 TDZ
        JsBlockNode* blk = (JsBlockNode*)stmt;
        jm_init_block_function_bindings(mt, blk);
        jm_transpile_statement_list_with_using(mt, blk->statements);
        jm_pop_scope(mt);

        // Js55 P19: restore prior tracking.
        jm_restore_last_closure_snapshot(mt, &blk_saved_last_closure);
        break;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* es = (JsExpressionStatementNode*)stmt;
        if (es->expression) {
            if (!mt->eval_completion_reg &&
                    jm_discardable_literal_statement(es->expression)) {
                break;
            }
            JsAstNode* saved_discarded = mt->discarded_expression;
            if (!mt->eval_completion_reg) mt->discarded_expression = es->expression;
            MIR_reg_t val = jm_transpile_box_item(mt, es->expression);
            mt->discarded_expression = saved_discarded;
            // Eval completion value: update the completion register so that
            // expression statements inside control flow (for/while/if/switch)
            // propagate their value as the eval() result.
            if (mt->eval_completion_reg) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, mt->eval_completion_reg),
                    MIR_new_reg_op(mt->ctx, val)));
            }
        }
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* try_node = (JsTryNode*)stmt;
        bool has_catch = (try_node->handler != NULL);
        bool has_finally = (try_node->finalizer != NULL);

        // Eval completion: reset to undefined (spec §14.15.11)
        jm_eval_cptn_reset(mt);

        // Create labels
        MIR_label_t catch_label = has_catch ? jm_new_label(mt) : 0;
        MIR_label_t finally_label = has_finally ? jm_new_label(mt) : 0;
        MIR_label_t end_label = jm_new_label(mt);

        // Create registers for delayed return handling
        MIR_reg_t return_val_reg = jm_new_reg(mt, "_try_ret", MIR_T_I64);
        MIR_reg_t has_return_reg = jm_new_reg(mt, "_try_has_ret", MIR_T_I64);
        MIR_reg_t incoming_error_lane_val_reg = jm_new_reg(mt, "_try_exc", MIR_T_I64);
        MIR_reg_t catch_outgoing_exc_reg = 0;

        // Initialize return tracking
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, return_val_reg),
            MIR_new_int_op(mt->ctx, 0)));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, has_return_reg),
            MIR_new_int_op(mt->ctx, 0)));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, incoming_error_lane_val_reg),
            MIR_new_reg_op(mt->ctx, jm_emit_null(mt))));
        if (has_catch && has_finally) {
            // A caught try exception remains in incoming_error_lane_val_reg even
            // after catch completes normally. Keep catch-body abrupt
            // completions separate so finally can distinguish "caught" from
            // "rethrow from catch" without relying on a transient call lane.
            catch_outgoing_exc_reg = jm_new_reg(mt, "_try_catch_exc", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, catch_outgoing_exc_reg),
                MIR_new_reg_op(mt->ctx, jm_emit_null(mt))));
        }

        // Push try context
        JsTryContext* try_context = jm_try_context_push(mt);
        if (try_context) {
            JsTryContext* tc = try_context;
            tc->catch_label = catch_label;
            tc->finally_label = finally_label;
            tc->end_label = end_label;
            tc->return_val_reg = return_val_reg;
            tc->has_return_reg = has_return_reg;
            tc->has_catch = has_catch;
            tc->has_finally = has_finally;
            tc->inlining_finally = false;
            tc->yield_state_only = false;
            tc->finally_body = has_finally ? try_node->finalizer : NULL; // v18
            tc->saved_error_lane_flag_reg = 0;
            tc->saved_error_lane_val_reg = 0;
            tc->incoming_error_lane_val_reg = incoming_error_lane_val_reg;
        }

        // Save with-scope depth so we can restore it if an exception escapes a 'with' block
        MIR_reg_t saved_with_depth = jm_call_0(mt, "js_with_save_depth", MIR_T_I64);
        int saved_with_depth_spill = -1;
        if (mt->in_generator) {
            saved_with_depth_spill = jm_gen_spill_save(mt, saved_with_depth);
        }

        // === Try body ===
        if (try_node->block && try_node->block->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            jm_push_scope(mt);
            jm_init_block_tdz(mt, try_node->block);  // v20 TDZ
            JsBlockNode* blk = (JsBlockNode*)try_node->block;
            JsAstNode* s = blk->statements;
            while (s) {
                jm_transpile_statement(mt, s);
                // After each statement, check if an exception was thrown
                // (from a called function that set the flag and returned)
                if (has_catch) {
                    jm_emit_error_lane_route(mt, JS_MIR_COMPLETION_THROW);
                } else if (has_finally) {
                    // try/finally without catch: check and jump to finally, then propagate
                    jm_emit_error_lane_route(mt, JS_MIR_COMPLETION_THROW);
                }
                s = s->next;
            }
            jm_pop_scope(mt);  // v20 TDZ: pop try block scope
        }

        // Keep the end join predecessor-aware.  A direct completion already
        // has a target edge, so synthesizing a normal edge after it makes the
        // dead join look UNKNOWN and re-emits an unnecessary D8.4.3 tag test.
        bool end_label_has_edge = try_context && try_context->end_label_has_edge;
        JsErrorLaneTrack end_label_error_lane_state = end_label_has_edge ?
            try_context->end_label_error_lane_state : JS_ERROR_LANE_UNREACHABLE;

        // Normal exit from try: jump to finally (or end)
        // Pop the try context so throws in catch propagate to outer handler
        if (mt->try_ctx_depth > 0) mt->try_ctx_depth--;
        JsErrorLaneTrack try_exit_state = jm_error_lane_state(mt);
        if (try_exit_state != JS_ERROR_LANE_UNREACHABLE) {
            if (!has_finally) {
                end_label_has_edge = true;
                end_label_error_lane_state = jm_error_lane_merge(
                    end_label_error_lane_state, try_exit_state);
            }
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, has_finally ? finally_label : end_label)));
        }

        // === Catch block ===
        if (has_catch) {
            jm_emit_label_with_state(mt, catch_label, JS_ERROR_LANE_SET);

            // Restore with-scope depth (exception may have escaped a 'with' block)
            if (saved_with_depth_spill >= 0) {
                jm_gen_spill_load(mt, saved_with_depth, saved_with_depth_spill);
            }
            jm_call_void_1(mt, "js_with_restore_depth", MIR_T_I64,
                MIR_new_reg_op(mt->ctx, saved_with_depth));

            JsCatchNode* catch_node = (JsCatchNode*)try_node->handler;

            // Consume the state bridge, but bind the routed ERROR Item itself;
            // catch must preserve object identity and primitive throw payloads.
            MIR_reg_t routed_exc = incoming_error_lane_val_reg ? incoming_error_lane_val_reg :
                jm_emit_error_lane_return(mt);
            MIR_reg_t thrown_val = jm_call_1(mt, "js_error_lane_payload", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, routed_exc));
            // catch consumes the routed ERROR lane; the bound thrown value is
            // ordinary data, so later catch statements must not inherit the
            // exceptional state from the edge that entered this handler.
            jm_error_lane_set_state(mt, JS_ERROR_LANE_CLEAN);

            // If there's a finally block, push a context for return-in-catch
            // (so return in catch still goes through finally)
            // This context has no catch_label, so throws propagate outward
            bool pushed_catch_ctx = false;
            if (has_finally) {
                JsTryContext* tc = jm_try_context_push(mt);
                if (tc) {
                tc->catch_label = 0;
                tc->finally_label = finally_label;
                tc->end_label = end_label;
                tc->return_val_reg = return_val_reg;
                tc->has_return_reg = has_return_reg;
                tc->has_catch = false;
                tc->has_finally = true;
                tc->yield_state_only = false;
                tc->finally_body = try_node->finalizer; // v18
                tc->saved_error_lane_flag_reg = 0;
                tc->saved_error_lane_val_reg = 0;
                tc->incoming_error_lane_val_reg = catch_outgoing_exc_reg ?
                    catch_outgoing_exc_reg : incoming_error_lane_val_reg;
                pushed_catch_ctx = true;
                }
            } else if (!has_finally && mt->in_generator) {
                // Generator yield inside catch body needs the inner try's state
                // regs (return_val_reg/has_return_reg) re-initialized on resume.
                // Push a synthetic ctx marked yield_state_only so the resume
                // code finds these regs while throw/return routing skips it.
                JsTryContext* tc = jm_try_context_push(mt);
                if (tc) {
                tc->catch_label = 0;
                tc->finally_label = 0;
                tc->end_label = 0;
                tc->return_val_reg = return_val_reg;
                tc->has_return_reg = has_return_reg;
                tc->has_catch = false;
                tc->has_finally = false;
                tc->yield_state_only = true;
                tc->finally_body = NULL;
                tc->saved_error_lane_flag_reg = 0;
                tc->saved_error_lane_val_reg = 0;
                tc->incoming_error_lane_val_reg = incoming_error_lane_val_reg;
                pushed_catch_ctx = true;
                }
            }

            // catch has two lexical environments: one for the parameter and a
            // nested one for the body. Keeping them separate lets destructuring
            // defaults capture the parameter without seeing body let/const TDZ.
            jm_push_scope(mt);
            if (catch_node->param && catch_node->param->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* param_id = (JsIdentifierNode*)catch_node->param;
                const char* vname = jm_format_name("_js_%.*s", (int)param_id->name->len, param_id->name->chars);
                jm_set_var(mt, vname, thrown_val);
                JsMirVarEntry* catch_entry = jm_find_var(mt, vname);
                if (catch_entry) catch_entry->from_catch_param = true;
                jm_scope_env_mark_and_writeback(mt, vname, thrown_val);
            } else if (catch_node->param &&
                       (catch_node->param->node_type == JS_AST_NODE_OBJECT_PATTERN ||
                        catch_node->param->node_type == JS_AST_NODE_ARRAY_PATTERN)) {
                jm_bind_catch_destructure(mt, catch_node->param, thrown_val,
                    catch_node->param->node_type == JS_AST_NODE_ARRAY_PATTERN);
            }

            // Transpile catch body
            if (catch_node->body && catch_node->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
                jm_push_scope(mt);
                jm_init_block_tdz(mt, catch_node->body);  // v20 TDZ
                JsBlockNode* blk = (JsBlockNode*)catch_node->body;
                JsAstNode* s = blk->statements;
                while (s) { jm_transpile_statement(mt, s); s = s->next; }
                jm_pop_scope(mt);
            }
            jm_pop_scope(mt);

            // Pop catch-finally context if we pushed one
            if (pushed_catch_ctx && mt->try_ctx_depth > 0) mt->try_ctx_depth--;

            // Jump to finally (or end) only from a real fallthrough path.
            JsErrorLaneTrack catch_exit_state = jm_error_lane_state(mt);
            if (catch_exit_state != JS_ERROR_LANE_UNREACHABLE) {
                if (!has_finally) {
                    end_label_has_edge = true;
                    end_label_error_lane_state = jm_error_lane_merge(
                        end_label_error_lane_state, catch_exit_state);
                }
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                    MIR_new_label_op(mt->ctx, has_finally ? finally_label : end_label)));
            }
        }

        // === Finally block ===
        if (has_finally) {
            jm_emit_label(mt, finally_label);

            // Restore with-scope depth (exception or early exit may have escaped a 'with' block)
            if (saved_with_depth_spill >= 0) {
                jm_gen_spill_load(mt, saved_with_depth, saved_with_depth_spill);
            }
            jm_call_void_1(mt, "js_with_restore_depth", MIR_T_I64,
                MIR_new_reg_op(mt->ctx, saved_with_depth));

            // In generators, push a minimal try context so that yield inside
            // the finally body can re-initialize has_return_reg/return_val_reg
            // on resume. The main try context was already popped before the
            // finally block (so throws propagate outward), but the generator
            // yield save/restore needs to know about these registers.
            bool pushed_gen_finally_ctx = false;
            if (mt->in_generator) {
                JsTryContext* tc = jm_try_context_push(mt);
                if (tc) {
                tc->catch_label = 0;
                tc->finally_label = 0;
                tc->end_label = 0;
                tc->return_val_reg = return_val_reg;
                tc->has_return_reg = has_return_reg;
                tc->has_catch = false;
                tc->has_finally = true;
                tc->yield_state_only = true;
                tc->finally_body = NULL;
                tc->saved_error_lane_flag_reg = 0;
                tc->saved_error_lane_val_reg = 0;
                tc->incoming_error_lane_val_reg = incoming_error_lane_val_reg;
                pushed_gen_finally_ctx = true;
                }
            }

            // Eval completion: save completion value before finally body.
            // Per spec, if finally completes normally, the completion value is
            // from the try/catch block, not the finally block.
            MIR_reg_t saved_cptn = 0;
            if (mt->eval_completion_reg) {
                saved_cptn = jm_new_reg(mt, "_fin_cptn", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, saved_cptn),
                    MIR_new_reg_op(mt->ctx, mt->eval_completion_reg)));
                // finally has its own statement-list completion. If it exits
                // abruptly through `break;` or `continue;`, that empty abrupt
                // completion must update to undefined, not inherit try's value.
                jm_eval_cptn_reset(mt);
            }

            // Preserve the routed ERROR Item while the finalizer runs. Reset
            // only compiler proof so handlers inside finally observe failures
            // they produce, not an ambient outer exception (ES §13.15.8).
            MIR_reg_t saved_error_lane_flag = 0;
            MIR_reg_t saved_error_lane_val = incoming_error_lane_val_reg ? incoming_error_lane_val_reg :
                jm_emit_error_lane_return(mt);
            if (has_catch && catch_outgoing_exc_reg) {
                // The catch body has its own completion carrier; the original
                // try throw is intentionally consumed before this point.
                saved_error_lane_val = catch_outgoing_exc_reg;
            }
            if (incoming_error_lane_val_reg || catch_outgoing_exc_reg) {
                // Calls made while entering finally can clear the compiler's
                // transient last-call result, but the routed carrier is the
                // authoritative abrupt-completion state.
                MIR_reg_t saved_error_lane_tag = jm_new_reg(mt, "fin_exc_tag", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_URSH,
                    MIR_new_reg_op(mt->ctx, saved_error_lane_tag),
                    MIR_new_reg_op(mt->ctx, saved_error_lane_val),
                    MIR_new_int_op(mt->ctx, 56)));
                saved_error_lane_flag = jm_new_reg(mt, "fin_exc_set", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ,
                    MIR_new_reg_op(mt->ctx, saved_error_lane_flag),
                    MIR_new_reg_op(mt->ctx, saved_error_lane_tag),
                    MIR_new_int_op(mt->ctx, LMD_TYPE_ERROR)));
            } else {
                saved_error_lane_flag = jm_emit_error_lane_test(mt);
            }
            int saved_error_lane_flag_spill = -1;
            int saved_error_lane_val_spill = -1;
            if (mt->in_generator && jm_has_yield(try_node->finalizer)) {
                saved_error_lane_flag_spill = jm_gen_spill_save(mt, saved_error_lane_flag);
                saved_error_lane_val_spill = jm_gen_spill_save(mt, saved_error_lane_val);
            }
            if (pushed_gen_finally_ctx && mt->try_ctx_depth > 0) {
                JsTryContext* tc = jm_try_context_at(mt, mt->try_ctx_depth - 1);
                tc->saved_error_lane_flag_reg = saved_error_lane_flag;
                tc->saved_error_lane_val_reg = saved_error_lane_val;
                tc->incoming_error_lane_val_reg = incoming_error_lane_val_reg;
            }

            if (try_node->finalizer->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
                jm_push_scope(mt);
                jm_init_block_tdz(mt, try_node->finalizer);  // v20 TDZ
                JsBlockNode* fin = (JsBlockNode*)try_node->finalizer;
                JsAstNode* s = fin->statements;
                while (s) { jm_transpile_statement(mt, s); s = s->next; }
                jm_pop_scope(mt);
            }

            // Eval completion: restore saved value (finally completed normally)
            if (saved_cptn) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, mt->eval_completion_reg),
                    MIR_new_reg_op(mt->ctx, saved_cptn)));
            }

            if (saved_error_lane_flag_spill >= 0) {
                jm_gen_spill_load(mt, saved_error_lane_flag, saved_error_lane_flag_spill);
                jm_gen_spill_load(mt, saved_error_lane_val, saved_error_lane_val_spill);
            }

            // Restore the saved ERROR Item if finally completed normally. A
            // new returned lane from finally takes precedence (per spec).
            {
                MIR_reg_t new_exc = jm_emit_error_lane_test(mt);
                MIR_label_t skip_restore = jm_new_label(mt);
                // A new ERROR lane from finally takes precedence.
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                    MIR_new_label_op(mt->ctx, skip_restore),
                    MIR_new_reg_op(mt->ctx, new_exc)));
                // Re-throw the saved lane only when its tag is ERROR.
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
                    MIR_new_label_op(mt->ctx, skip_restore),
                    MIR_new_reg_op(mt->ctx, saved_error_lane_flag)));
                MIR_reg_t restored_exception = jm_call_1(mt, "js_throw_value", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, saved_error_lane_val));
                JsTryContext* outer_exception_context =
                    jm_find_completion_context(mt, JS_MIR_COMPLETION_THROW);
                if (mt->in_generator && !outer_exception_context) {
                    // The state-machine epilogue has no caller-side try stack
                    // to route through; returning the rethrow carrier keeps
                    // the generator protocol from converting it into done.
                    jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
                        MIR_new_reg_op(mt->ctx, restored_exception)));
                }
                jm_emit_label(mt, skip_restore);
            }

            // Pop the generator finally context (pushed for yield re-init)
            if (pushed_gen_finally_ctx && mt->try_ctx_depth > 0) mt->try_ctx_depth--;

            // After finally: check if we had a delayed return
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                MIR_new_label_op(mt->ctx, end_label),
                MIR_new_reg_op(mt->ctx, has_return_reg)));
        }

        // End label: check for delayed return.  A try/catch with only abrupt
        // paths has no end predecessor, so emitting its join would create dead
        // MIR and cause the enclosing statement sweep to re-test a stale lane.
        if (has_finally || end_label_has_edge) {
            if (has_finally) {
                jm_emit_label(mt, end_label);
            } else {
                jm_emit_label_with_state(mt, end_label, end_label_error_lane_state);
            }

            // If has_return_reg is set, issue the actual return
            MIR_label_t no_ret_label = jm_new_label(mt);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
                MIR_new_label_op(mt->ctx, no_ret_label),
                MIR_new_reg_op(mt->ctx, has_return_reg)));
            if (mt->in_generator) {
                MIR_reg_t done_result = jm_call_2(mt, "js_gen_yield_result", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, return_val_reg),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)-1));
                jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
                    MIR_new_reg_op(mt->ctx, done_result)));
            } else {
                MIR_reg_t native_ret = jm_native_return_reg(mt, return_val_reg);
                jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
                    MIR_new_reg_op(mt->ctx, native_ret)));
            }
            jm_emit_label_with_state(mt, no_ret_label, end_label_error_lane_state);

            // A routed ERROR Item (try/finally without catch, or rethrow) must
            // continue through the enclosing handler or function exit.
            if (!has_catch || has_finally) {
                jm_emit_error_lane_exit(mt);
            }
        }
        break;
    }
    case JS_AST_NODE_THROW_STATEMENT: {
        JsThrowNode* throw_node = (JsThrowNode*)stmt;
        MIR_reg_t thrown_val = jm_emit_null(mt);
        if (throw_node->argument) {
            thrown_val = jm_transpile_box_item(mt, throw_node->argument);
        }

        jm_emit_throw_completion(mt, thrown_val);
        break;
    }
    case JS_AST_NODE_WITH_STATEMENT: {
        JsWithStatementNode* with_node = (JsWithStatementNode*)stmt;
        if (with_node->object) {
            // push with-scope object
            MIR_reg_t obj_reg = jm_transpile_box_item(mt, with_node->object);
            jm_call_1(mt, "js_with_push", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj_reg));
            jm_emit_error_lane_propagate_check(mt);
            jm_eval_cptn_reset(mt);
            mt->with_depth++;
            // transpile body
            if (with_node->body)
                jm_transpile_statement(mt, with_node->body);
            mt->with_depth--;
            // pop with-scope
            jm_call_void_0(mt, "js_with_pop");
        }
        break;
    }
    default:
        log_error("js-mir: unsupported statement type %d", stmt->node_type);
        break;
    case JS_AST_NODE_IMPORT_DECLARATION: {
        // ES module import: resolve module and bind imported names
        JsImportNode* imp = (JsImportNode*)stmt;
        if (!imp->source) break;

        // Resolve module path relative to current file
        char resolved[512];
        if (mt->filename) {
            jm_resolve_module_path(mt->filename, imp->source->chars, (int)imp->source->len,
                resolved, sizeof(resolved));
        } else {
            snprintf(resolved, sizeof(resolved), "%.*s", (int)imp->source->len, imp->source->chars);
        }

        // Get module namespace: ns = js_module_get(specifier_string)
        MIR_reg_t spec = jm_box_string_literal(mt, resolved, (int)strlen(resolved));
        MIR_reg_t ns = jm_call_1(mt, "js_module_get", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, spec));

        // Bind default import: import X from 'module'
        if (imp->default_name) {
            const char* vname = jm_format_name("_js_%.*s",
                (int)imp->default_name->len, imp->default_name->chars);
            // Js57 P3 (Track B2): self-import live binding.
            // When the source resolves to the current module's filename, the
            // import sees a namespace whose `default` slot won't be initialised
            // until `export default <expr>` runs later in this body. Replace the
            // snapshot with a live binding: each read emits a runtime call that
            // re-fetches namespace.default and throws ReferenceError if the slot
            // still holds the TDZ sentinel.
            bool is_self_import = (mt->filename != NULL &&
                strcmp(resolved, mt->filename) == 0);
            if (is_self_import) {
                MIR_reg_t var_reg = jm_new_reg(mt, vname, MIR_T_I64);
                jm_set_var(mt, vname, var_reg);
                JsMirVarEntry* lv = jm_find_var(mt, vname);
                if (lv) {
                    lv->is_live_default_binding = true;
                    lv->live_binding_specifier = name_pool_create_len(
                        mt->tp->name_pool, resolved, (int)strlen(resolved))->chars;
                }
            } else {
                MIR_reg_t key = jm_box_property_name_literal(mt, "default", 7);
                MIR_reg_t val = jm_call_2(mt, "js_get_key_default", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, ns),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                MIR_reg_t var_reg = jm_new_reg(mt, vname, MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, var_reg),
                    MIR_new_reg_op(mt->ctx, val)));
                jm_set_var(mt, vname, var_reg);
                // Also update module var for closure access
                JsModuleConstEntry lookup;
                lookup.name = jm_persist_name(vname);
                JsModuleConstEntry* mce = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup);
                if (mce && mce->const_type == MCONST_MODVAR) {
                    jm_call_void_2(mt, "js_set_module_var",
                        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mce->int_val),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                }
            }
        }

        // Bind namespace import: import * as X from 'module'
        if (imp->namespace_name) {
            const char* vname = jm_format_name("_js_%.*s",
                (int)imp->namespace_name->len, imp->namespace_name->chars);
            MIR_reg_t var_reg = jm_new_reg(mt, vname, MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, var_reg),
                MIR_new_reg_op(mt->ctx, ns)));
            jm_set_var(mt, vname, var_reg);
            JsModuleConstEntry lookup;
            lookup.name = jm_persist_name(vname);
            JsModuleConstEntry* mce = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup);
            if (mce && mce->const_type == MCONST_MODVAR) {
                jm_call_void_2(mt, "js_set_module_var",
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mce->int_val),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, ns));
            }
        }

        // Bind named imports: import { a, b as c } from 'module'
        {
            JsAstNode* spec = imp->specifiers;
            while (spec) {
                if (spec->node_type == JS_AST_NODE_IMPORT_SPECIFIER) {
                    JsImportSpecifierNode* isp = (JsImportSpecifierNode*)spec;
                    // Get exported value: val = js_get_key_default(ns, remote_name)
                    MIR_reg_t key = jm_box_property_name_literal(mt,
                        isp->remote_name->chars, isp->remote_name->len);
                    MIR_reg_t val = jm_call_2(mt, "js_get_key_default", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, ns),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                    // Bind to local name
                    const char* vname = jm_format_name("_js_%.*s",
                        (int)isp->local_name->len, isp->local_name->chars);
                    MIR_reg_t var_reg = jm_new_reg(mt, vname, MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, var_reg),
                        MIR_new_reg_op(mt->ctx, val)));
                    jm_set_var(mt, vname, var_reg);
                    // Also update module var for closure access
                    JsModuleConstEntry lookup;
                    lookup.name = jm_persist_name(vname);
                    JsModuleConstEntry* mce = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup);
                    if (mce && mce->const_type == MCONST_MODVAR) {
                        jm_call_void_2(mt, "js_set_module_var",
                            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mce->int_val),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                    }
                }
                spec = spec->next;
            }
        }
        break;
    }
    case JS_AST_NODE_EXPORT_DECLARATION: {
        // v14: export statement — transpile the declaration normally
        JsExportNode* exp = (JsExportNode*)stmt;
        if (exp->declaration) {
            // export function f() {} or export const x = 1
            // Just transpile the declaration; module namespace is built post-transpile
            jm_transpile_statement(mt, exp->declaration);
        }
        break;
    }
    }
}
