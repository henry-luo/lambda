#include "js_mir_internal.hpp"

// ============================================================================
// Statement transpilers
// ============================================================================

static JsMirVarEntry* jm_find_var_in_scope_depth(JsMirTranspiler* mt, const char* name, int depth) {
    if (!mt || !name || depth < 0 || depth > mt->scope_depth || depth >= 64) return NULL;
    if (!mt->var_scopes[depth]) return NULL;
    JsVarScopeEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    JsVarScopeEntry* found = (JsVarScopeEntry*)hashmap_get(mt->var_scopes[depth], &key);
    return found ? &found->var : NULL;
}

static JsMirVarEntry* jm_find_nearest_catch_param_var(JsMirTranspiler* mt, const char* name) {
    if (!mt || !name) return NULL;
    int start_depth = mt->scope_depth;
    if (start_depth >= 64) start_depth = 63;
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
    char capture_names[JS_MIR_LAST_CLOSURE_CAPTURE_MAX][128];
    int capture_slots[JS_MIR_LAST_CLOSURE_CAPTURE_MAX];
    bool capture_is_transitive[JS_MIR_LAST_CLOSURE_CAPTURE_MAX];
    bool capture_is_nfe[JS_MIR_LAST_CLOSURE_CAPTURE_MAX];
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
        snprintf(snapshot->capture_names[i], sizeof(snapshot->capture_names[i]),
            "%s", mt->last_closure_capture_names[i]);
        snapshot->capture_slots[i] = mt->last_closure_capture_slots[i];
        snapshot->capture_is_transitive[i] = mt->last_closure_capture_is_transitive[i];
        snapshot->capture_is_nfe[i] = mt->last_closure_capture_is_nfe[i];
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
        snprintf(mt->last_closure_capture_names[i],
            sizeof(mt->last_closure_capture_names[i]), "%s", snapshot->capture_names[i]);
        mt->last_closure_capture_slots[i] = snapshot->capture_slots[i];
        mt->last_closure_capture_is_transitive[i] = snapshot->capture_is_transitive[i];
        mt->last_closure_capture_is_nfe[i] = snapshot->capture_is_nfe[i];
    }
}

void jm_write_last_closure_capture_if_matching(JsMirTranspiler* mt,
        const char* name, MIR_reg_t val_reg, TypeId type_id) {
    if (!mt || !name || !mt->last_closure_has_env || mt->last_closure_env_reg == 0) return;
    int capture_count = jm_last_closure_capture_count_clamped(mt->last_closure_capture_count);
    for (int i = 0; i < capture_count; i++) {
        if (mt->last_closure_capture_is_nfe[i]) continue;
        if (strcmp(mt->last_closure_capture_names[i], name) != 0) continue;
        int slot = mt->last_closure_capture_slots[i] >= 0 ? mt->last_closure_capture_slots[i] : i;
        MIR_reg_t val = val_reg;
        if (jm_is_native_type(type_id)) {
            val = jm_box_native(mt, val_reg, type_id);
        }
        // closures created before a same-scope let/const initializer copy the
        // TDZ value. Write the initialized value into that fresh env so the
        // closure observes JS's by-reference lexical binding semantics.
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_mem_op(mt->ctx, MIR_T_I64, slot * (int)sizeof(uint64_t),
                mt->last_closure_env_reg, 0, 1),
            MIR_new_reg_op(mt->ctx, val)));
        return;
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

static bool jm_has_outer_block_func_binding(JsMirTranspiler* mt, const char* name) {
    if (!mt || !name) return false;
    for (int depth = 2; depth < mt->scope_depth && depth < 64; depth++) {
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
    TSNode fn_node = fn->base.node;
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
    TSNode body = ts_node_child_by_field_name(grandparent, "body", 4);
    return !ts_node_is_null(body) &&
        ts_node_start_byte(body) == ts_node_start_byte(parent) &&
        ts_node_end_byte(body) == ts_node_end_byte(parent);
}

static bool jm_assignment_targets_name(JsAstNode* left, const char* bare_name, int bare_len) {
    if (!left || !bare_name || bare_len <= 0) return false;
    if (left->node_type != JS_AST_NODE_IDENTIFIER) return false;
    JsIdentifierNode* id = (JsIdentifierNode*)left;
    return id->name && id->name->len == (size_t)bare_len &&
        strncmp(id->name->chars, bare_name, bare_len) == 0;
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
    if (decl->kind != JS_VAR_VAR || !mt->in_main || mt->is_module) return;
    MIR_reg_t key_reg = jm_box_string_literal(mt, id->name->chars, (int)id->name->len);
    jm_call_void_3(mt, "js_define_global_property_v",
        MIR_T_I64, MIR_new_int_op(mt->ctx, 0),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, value));
    jm_call_void_2(mt, "js_set_global_var_property_fast",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, value));
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
    MIR_reg_t key_reg = jm_box_string_literal(mt, id->name->chars, (int)id->name->len);
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
        char vname[128];
        snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
        JsModuleConstEntry lookup;
        memset(&lookup, 0, sizeof(lookup));
        snprintf(lookup.name, sizeof(lookup.name), "%s", vname);
        JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup);
        if (!mc || mc->const_type != MCONST_MODVAR || (int)mc->int_val < 0) return false;
        decl = decl->next;
    }
    return true;
}

void jm_transpile_var_decl(JsMirTranspiler* mt, JsVariableDeclarationNode* var) {
    // JS spec: 'var' is function-scoped. Redirect variable creation to scope 1
    // (the function body scope after jm_push_scope) so vars survive after block scopes pop.
    int saved_hoist = mt->var_hoist_depth;
    if (var->kind == JS_VAR_VAR && mt->scope_depth > 1 && mt->var_hoist_depth < 0) {
        mt->var_hoist_depth = 1;
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
                char vname[128];
                snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);

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
                    snprintf(mclookup.name, sizeof(mclookup.name), "%s", vname);
                    JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                    bool at_top = (mt->scope_depth <= 1) ||
                        (var->kind == JS_VAR_VAR && mt->var_hoist_depth <= 1);
                    bool local_var_hoist = (var->kind == JS_VAR_VAR && mt->var_hoist_depth > 1);
                    if (mc && mc->const_type == MCONST_MODVAR && at_top && !local_var_hoist &&
                        (mt->in_main || (mc->is_iife_var && mt->current_fc && mt->current_fc->is_iife_body))) {
                        is_modvar = true;
                        modvar_index = (int)mc->int_val;
                    }
                }

                bool with_var_init_handled = false;
                if (var->kind == JS_VAR_VAR && d->init && mt->with_depth > 0) {
                    MIR_reg_t key_reg = jm_box_string_literal(mt, id->name->chars, (int)id->name->len);
                    MIR_reg_t has_with = jm_call_1(mt, "js_capture_with_binding", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg));
                    jm_emit_exc_propagate_check(mt);
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
                    jm_emit_exc_propagate_check(mt);
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
                        if (mt->in_main) {
                            jm_call_void_3(mt, "js_define_global_property_v",
                                MIR_T_I64, MIR_new_int_op(mt->ctx, 0),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
                            jm_call_void_2(mt, "js_set_global_var_property_fast",
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
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
                        if (var->kind == JS_VAR_VAR && mt->in_main) {
                            MIR_reg_t key_reg = jm_box_string_literal(mt, id->name->chars, (int)id->name->len);
                            jm_call_void_3(mt, "js_define_global_property_v",
                                MIR_T_I64, MIR_new_int_op(mt->ctx, 0),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
                            jm_call_void_2(mt, "js_set_global_var_property_fast",
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
                        }
                        if (mt->is_eval_direct && (var->kind == JS_VAR_LET || var->kind == JS_VAR_CONST)) {
                            MIR_reg_t eval_env_active = jm_call_0(mt, "js_262_eval_script_is_active", MIR_T_I64);
                            MIR_label_t skip_global_lex = jm_new_label(mt);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
                                MIR_new_label_op(mt->ctx, skip_global_lex),
                                MIR_new_reg_op(mt->ctx, eval_env_active)));
                            MIR_reg_t key_reg = jm_box_string_literal(mt, id->name->chars, (int)id->name->len);
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
                                    snprintf(p7_lookup.name, sizeof(p7_lookup.name), "%s", vname);
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
                            MIR_reg_t key_reg = jm_box_string_literal(mt, id->name->chars, (int)id->name->len);
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
                            MIR_reg_t val = jm_transpile_box_item(mt, d->init);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, existing_var->reg),
                                MIR_new_reg_op(mt->ctx, val)));
                            jm_write_env_backing_if_needed(mt, existing_var, val, LMD_TYPE_ANY);
                            jm_scope_env_mark_and_writeback(mt, vname, existing_var->reg);
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
                        d->ts_type->type_expr->base.node_type == (int)TS_AST_NODE_PREDEFINED_TYPE) {
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
                                if (strcmp(vname, fc->scope_env_names[s]) == 0) {
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
                        jm_scope_env_mark_and_writeback(mt, vname, reg, LMD_TYPE_INT);
                        jm_write_last_closure_capture_if_matching(mt, vname, reg, LMD_TYPE_INT);
                        if (var->kind == JS_VAR_LET || var->kind == JS_VAR_CONST) {
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
                        jm_scope_env_mark_and_writeback(mt, vname, reg, LMD_TYPE_FLOAT);
                        jm_write_last_closure_capture_if_matching(mt, vname, reg, LMD_TYPE_FLOAT);
                        if (var->kind == JS_VAR_LET || var->kind == JS_VAR_CONST) {
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
                        jm_scope_env_mark_and_writeback(mt, vname, reg, init_type);
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
                            d->ts_type->type_expr->base.node_type != (int)TS_AST_NODE_PREDEFINED_TYPE) {
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

                        // P9: Track typed array type for direct memory access
                        if (d->init->node_type == JS_AST_NODE_NEW_EXPRESSION) {
                            JsCallNode* new_call = (JsCallNode*)d->init;
                            if (new_call->callee && new_call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
                                JsIdentifierNode* ctor = (JsIdentifierNode*)new_call->callee;
                                int ta_type = -1;
                                if (ctor->name->len == 10 && strncmp(ctor->name->chars, "Int32Array", 10) == 0) ta_type = JS_TYPED_INT32;
                                else if (ctor->name->len == 10 && strncmp(ctor->name->chars, "Int16Array", 10) == 0) ta_type = JS_TYPED_INT16;
                                else if (ctor->name->len == 9 && strncmp(ctor->name->chars, "Int8Array", 9) == 0) ta_type = JS_TYPED_INT8;
                                else if (ctor->name->len == 11 && strncmp(ctor->name->chars, "Uint32Array", 11) == 0) ta_type = JS_TYPED_UINT32;
                                else if (ctor->name->len == 11 && strncmp(ctor->name->chars, "Uint16Array", 11) == 0) ta_type = JS_TYPED_UINT16;
                                else if (ctor->name->len == 10 && strncmp(ctor->name->chars, "Uint8Array", 10) == 0) ta_type = JS_TYPED_UINT8;
                                else if (ctor->name->len == 17 && strncmp(ctor->name->chars, "Uint8ClampedArray", 17) == 0) ta_type = JS_TYPED_UINT8_CLAMPED;
                                else if (ctor->name->len == 12 && strncmp(ctor->name->chars, "Float64Array", 12) == 0) ta_type = JS_TYPED_FLOAT64;
                                else if (ctor->name->len == 12 && strncmp(ctor->name->chars, "Float32Array", 12) == 0) ta_type = JS_TYPED_FLOAT32;
                                if (ta_type >= 0) {
                                    JsMirVarEntry* var_entry = jm_find_var(mt, vname);
                                    if (var_entry) {
                                        var_entry->typed_array_type = ta_type;
                                    }
                                }
                                // A2: Detect new Array(n) — mark as regular JS array
                                if (ta_type < 0 && ctor->name->len == 5 &&
                                    strncmp(ctor->name->chars, "Array", 5) == 0) {
                                    JsMirVarEntry* var_entry = jm_find_var(mt, vname);
                                    if (var_entry) {
                                        var_entry->is_js_array = true;
                                        log_debug("A2: var '%s' is regular JS array (new Array)", vname);
                                    }
                                }
                                // P4: Detect known class instance for direct shaped property reads.
                                // Only for classes with pre-shaped constructors (ctor_prop_count > 0).
                                if (ta_type < 0) {
                                    JsClassEntry* p4_ce = jm_find_class(mt, ctor->name->chars, (int)ctor->name->len);
                                    if (p4_ce && p4_ce->constructor && p4_ce->constructor->fc &&
                                        p4_ce->constructor->fc->ctor_prop_count > 0) {
                                        JsMirVarEntry* var_entry = jm_find_var(mt, vname);
                                        if (var_entry) {
                                            var_entry->class_entry = p4_ce;
                                            log_debug("P4: var '%s' is instance of '%.*s' (%d slots)",
                                                      vname, (int)ctor->name->len, ctor->name->chars,
                                                      p4_ce->constructor->fc->ctor_prop_count);
                                        }
                                    }
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

                        // A2: Detect Array.from(...): let x = Array.from(...)
                        if (d->init->node_type == JS_AST_NODE_CALL_EXPRESSION) {
                            JsCallNode* call = (JsCallNode*)d->init;
                            if (call->callee && call->callee->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
                                JsMemberNode* cm = (JsMemberNode*)call->callee;
                                if (cm->object && cm->object->node_type == JS_AST_NODE_IDENTIFIER &&
                                    cm->property && cm->property->node_type == JS_AST_NODE_IDENTIFIER) {
                                    JsIdentifierNode* obj = (JsIdentifierNode*)cm->object;
                                    JsIdentifierNode* prop = (JsIdentifierNode*)cm->property;
                                    if (obj->name->len == 5 && strncmp(obj->name->chars, "Array", 5) == 0 &&
                                        prop->name->len == 4 && strncmp(prop->name->chars, "from", 4) == 0) {
                                        JsMirVarEntry* var_entry = jm_find_var(mt, vname);
                                        if (var_entry) {
                                            var_entry->is_js_array = true;
                                            log_debug("A2: var '%s' is regular JS array (Array.from)", vname);
                                        }
                                    }
                                }
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

                        // P4b: Infer class type for variables from subscript access (arr[i]).
                        // Pattern: const iBody = expr[i] → scan function body for iBody.field accesses
                        // → find unique class whose constructor has all accessed fields → tag variable.
                        if (d->init->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
                            JsMemberNode* sub = (JsMemberNode*)d->init;
                            if (sub->computed) {
                                JsMirVarEntry* var_entry = jm_find_var(mt, vname);
                                if (var_entry && !var_entry->class_entry) {
                                    // get the original variable name (without _js_ prefix)
                                    const char* orig_name = id->name->chars;
                                    int orig_len = (int)id->name->len;
                                    // scan the current function body for field accesses
                                    JsAstNode* scan_root = NULL;
                                    if (mt->current_fc && mt->current_fc->node && mt->current_fc->node->body)
                                        scan_root = mt->current_fc->node->body;
                                    if (scan_root) {
                                        char p4b_fields[16][64];
                                        int p4b_count = 0;
                                        jm_collect_var_fields_walk(scan_root, orig_name, orig_len,
                                                                   p4b_fields, &p4b_count, 16);
                                        if (p4b_count >= 2) {
                                            JsClassEntry* p4b_ce = jm_match_class_from_fields(mt, p4b_fields, p4b_count);
                                            if (p4b_ce) {
                                                var_entry->class_entry = p4b_ce;
                                                log_debug("P4b: var '%s' inferred as '%.*s' from %d field accesses",
                                                          vname,
                                                          (int)(p4b_ce->name ? p4b_ce->name->len : 0),
                                                          p4b_ce->name ? p4b_ce->name->chars : "<anon>",
                                                          p4b_count);
                                            }
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
                        jm_scope_env_mark_and_writeback(mt, vname, reg);
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
                    snprintf(mclookup.name, sizeof(mclookup.name), "%s", vname);
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
                // v28: Write destructured bindings to scope_env for closure capture (no reload marking)
                if (mt->scope_env_reg != 0 && mt->current_fc && mt->current_fc->has_scope_env) {
                    JsFuncCollected* se_fc = mt->current_fc;
                    struct hashmap* se_names = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
                        jm_name_hash, jm_name_cmp, NULL, NULL);
                    jm_collect_pattern_names(d->id, se_names);
                    size_t si = 0; void* sitem;
                    while (hashmap_iter(se_names, &si, &sitem)) {
                        JsNameSetEntry* ne = (JsNameSetEntry*)sitem;
                        for (int se_s = 0; se_s < se_fc->scope_env_count; se_s++) {
                            if (strcmp(ne->name, se_fc->scope_env_names[se_s]) == 0) {
                                JsMirVarEntry* ve = jm_find_var(mt, ne->name);
                                if (ve) {
                                    MIR_reg_t val = ve->reg;
                                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                        MIR_new_mem_op(mt->ctx, MIR_T_I64, se_s * (int)sizeof(uint64_t), mt->scope_env_reg, 0, 1),
                                        MIR_new_reg_op(mt->ctx, val)));
                                }
                                break;
                            }
                        }
                    }
                    hashmap_free(se_names);
                }
                // writeback destructured bindings to module vars
                bool pattern_at_top_for_writeback = (mt->scope_depth <= 1) ||
                    (var->kind == JS_VAR_VAR && mt->var_hoist_depth <= 1);
                if (pattern_at_top_for_writeback &&
                    (mt->in_main || (mt->current_fc && mt->current_fc->is_iife_body)) && mt->module_consts) {
                    struct hashmap* pat_names = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
                        jm_name_hash, jm_name_cmp, NULL, NULL);
                    jm_collect_pattern_names(d->id, pat_names);
                    size_t piter = 0; void* pitem;
                    while (hashmap_iter(pat_names, &piter, &pitem)) {
                        JsNameSetEntry* ne = (JsNameSetEntry*)pitem;
                        JsModuleConstEntry mlookup;
                        snprintf(mlookup.name, sizeof(mlookup.name), "%s", ne->name);
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
            } else if (d->id && d->id->node_type == JS_AST_NODE_OBJECT_PATTERN) {
                // v20: object destructuring via recursive helper
                MIR_reg_t src = d->init ? jm_transpile_box_item(mt, d->init) : jm_emit_null(mt);
                jm_emit_object_destructure(mt, d->id, src);
                // v28: Write destructured bindings to scope_env for closure capture.
                // Unlike jm_scope_env_mark_and_writeback, we do NOT set in_scope_env=true
                // on the variable because that would cause jm_scope_env_reload_vars to
                // reload the variable from scope_env after calls — but inner blocks may
                // shadow the variable name and overwrite the scope_env slot.
                if (mt->scope_env_reg != 0 && mt->current_fc && mt->current_fc->has_scope_env) {
                    JsFuncCollected* se_fc = mt->current_fc;
                    struct hashmap* se_names = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
                        jm_name_hash, jm_name_cmp, NULL, NULL);
                    jm_collect_pattern_names(d->id, se_names);
                    size_t si = 0; void* sitem;
                    while (hashmap_iter(se_names, &si, &sitem)) {
                        JsNameSetEntry* ne = (JsNameSetEntry*)sitem;
                        for (int se_s = 0; se_s < se_fc->scope_env_count; se_s++) {
                            if (strcmp(ne->name, se_fc->scope_env_names[se_s]) == 0) {
                                JsMirVarEntry* ve = jm_find_var(mt, ne->name);
                                if (ve) {
                                    MIR_reg_t val = ve->reg;
                                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                        MIR_new_mem_op(mt->ctx, MIR_T_I64, se_s * (int)sizeof(uint64_t), mt->scope_env_reg, 0, 1),
                                        MIR_new_reg_op(mt->ctx, val)));
                                }
                                break;
                            }
                        }
                    }
                    hashmap_free(se_names);
                }
                // writeback destructured bindings to module vars
                bool pattern_at_top_for_writeback = (mt->scope_depth <= 1) ||
                    (var->kind == JS_VAR_VAR && mt->var_hoist_depth <= 1);
                if (pattern_at_top_for_writeback &&
                    (mt->in_main || (mt->current_fc && mt->current_fc->is_iife_body)) && mt->module_consts) {
                    struct hashmap* pat_names = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
                        jm_name_hash, jm_name_cmp, NULL, NULL);
                    jm_collect_pattern_names(d->id, pat_names);
                    size_t piter = 0; void* pitem;
                    while (hashmap_iter(pat_names, &piter, &pitem)) {
                        JsNameSetEntry* ne = (JsNameSetEntry*)pitem;
                        JsModuleConstEntry mlookup;
                        snprintf(mlookup.name, sizeof(mlookup.name), "%s", ne->name);
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
    char vname[128];
    snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
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
    char vname[128];
    snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
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
    char vname[128];
    snprintf(vname, sizeof(vname), "_js_%.*s", (int)fn->name->len, fn->name->chars);
    MIR_reg_t fn_reg = jm_create_func_or_closure(mt, fc);
    jm_set_var(mt, vname, fn_reg);
    JsMirVarEntry* ve = jm_find_var(mt, vname);
    if (ve) ve->from_block_func_decl = true;
    jm_scope_env_mark_and_writeback(mt, vname, fn_reg);
}

// transpile one if-branch body with the same scope/TDZ handling as the inline
// consequent/alternate paths below (used by the constant-folded dead-branch path).
static void jm_transpile_if_branch(JsMirTranspiler* mt, JsAstNode* branch) {
    if (!branch) return;
    if (branch->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
        jm_push_scope(mt);
        jm_init_block_tdz(mt, branch);
        JsBlockNode* blk = (JsBlockNode*)branch;
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

void jm_transpile_if(JsMirTranspiler* mt, JsIfNode* if_node) {
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
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    // Alternate
    jm_emit_label(mt, l_else);
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
    jm_emit_label(mt, l_end);
}

// Reload all in-scope-env variables from the shared scope env into their local registers.
// Emitted at the top of each while-loop test to ensure the outer function sees changes
// made by inner-function (closure) calls during the loop body.
void jm_scope_env_reload_vars(JsMirTranspiler* mt) {
    bool reload_iife_modvars = mt->module_consts && mt->current_fc && mt->current_fc->is_iife_body;
    if (mt->scope_env_reg == 0 && !reload_iife_modvars) return;
    for (int sd = 0; sd <= mt->scope_depth; sd++) {
        if (!mt->var_scopes[sd]) continue;
        size_t iter = 0; void* entry_ptr;
        while (hashmap_iter(mt->var_scopes[sd], &iter, &entry_ptr)) {
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
                    if (e->var.type_id == LMD_TYPE_INT) {
                        MIR_reg_t unboxed = jm_emit_unbox_int(mt, boxed);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, e->var.reg),
                            MIR_new_reg_op(mt->ctx, unboxed)));
                    } else if (e->var.type_id == LMD_TYPE_FLOAT) {
                        MIR_reg_t unboxed = jm_emit_unbox_float(mt, boxed);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                            MIR_new_reg_op(mt->ctx, e->var.reg),
                            MIR_new_reg_op(mt->ctx, unboxed)));
                    } else {
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, e->var.reg),
                            MIR_new_reg_op(mt->ctx, boxed)));
                    }
                }
            }
            if (reload_iife_modvars && e->var.reg != 0) {
                JsModuleConstEntry lookup;
                memset(&lookup, 0, sizeof(lookup));
                snprintf(lookup.name, sizeof(lookup.name), "%s", e->name);
                JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup);
                if (mc && mc->const_type == MCONST_MODVAR && mc->is_iife_var) {
                    MIR_reg_t boxed = jm_call_1(mt, "js_get_module_var", MIR_T_I64,
                        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mc->int_val));
                    if (e->var.type_id == LMD_TYPE_INT) {
                        MIR_reg_t unboxed = jm_emit_unbox_int(mt, boxed);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, e->var.reg),
                            MIR_new_reg_op(mt->ctx, unboxed)));
                    } else if (e->var.type_id == LMD_TYPE_FLOAT) {
                        MIR_reg_t unboxed = jm_emit_unbox_float(mt, boxed);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                            MIR_new_reg_op(mt->ctx, e->var.reg),
                            MIR_new_reg_op(mt->ctx, unboxed)));
                    } else {
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, e->var.reg),
                            MIR_new_reg_op(mt->ctx, boxed)));
                    }
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
        if (!mt->var_scopes[sd]) continue;
        size_t iter = 0; void* entry_ptr;
        while (hashmap_iter(mt->var_scopes[sd], &iter, &entry_ptr)) {
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

// Exception propagation check: emit a check for pending exceptions and branch appropriately.
// - Inside a try block (try_ctx_depth > 0): jump to catch/finally label
// - Outside a try block: jump to func_except_label (return null from function)
// This enables proper exception propagation through nested function calls, loops, and if/else
// even outside of explicit try/catch blocks.
void jm_emit_exc_propagate_check(JsMirTranspiler* mt) {
    // Find topmost non-yield_state_only ctx for throw routing.
    int d = mt->try_ctx_depth - 1;
    while (d >= 0 && mt->try_ctx_stack[d].yield_state_only) d--;
    if (d >= 0) {
        // Inside a try block: jump to catch/finally on exception
        JsTryContext* tc = &mt->try_ctx_stack[d];
        MIR_label_t target = tc->has_catch ? tc->catch_label :
                             (tc->has_finally ? tc->finally_label : (MIR_label_t)0);
        if (target) {
            MIR_reg_t exc_check = jm_call_0(mt, "js_check_exception", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                MIR_new_label_op(mt->ctx, target),
                MIR_new_reg_op(mt->ctx, exc_check)));
        }
    } else {
        // Outside try block: propagate exception by returning from function
        if (mt->func_except_label == 0) {
            mt->func_except_label = jm_new_label(mt);
        }
        MIR_reg_t exc_check = jm_call_0(mt, "js_check_exception", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
            MIR_new_label_op(mt->ctx, mt->func_except_label),
            MIR_new_reg_op(mt->ctx, exc_check)));
    }
}

void jm_transpile_while(JsMirTranspiler* mt, JsWhileNode* wh) {
    MIR_label_t l_test = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    // Push loop labels
    jm_push_loop_labels(mt, l_test, l_end);
    mt->iteration_depth++;

    // Eval completion: Let V = undefined (spec §14.7.3.2)
    jm_eval_cptn_reset(mt);

    // --- P4h: Hoist array metadata before while loop ---
    JsMirVarEntry* p4h_hoisted_vars[16];
    int p4h_hoisted_count = 0;
    if (wh->body) {
        char arr_names[16][64];
        bool arr_unsafe[16];
        int arr_count = 0;
        jm_scan_subscript_arrays(wh->test, arr_names, arr_unsafe, &arr_count, 16);
        jm_scan_subscript_arrays(wh->body, arr_names, arr_unsafe, &arr_count, 16);

        for (int ai = 0; ai < arr_count && p4h_hoisted_count < 16; ai++) {
            if (arr_unsafe[ai]) continue;
            char vname[128];
            snprintf(vname, sizeof(vname), "_js_%s", arr_names[ai]);
            JsMirVarEntry* var = jm_find_var(mt, vname);
            if (!var) continue;
            if (var->in_scope_env) continue;
            if (var->hoisted_data_reg) continue;

            if (var->typed_array_type >= 0) {
                // Js54 P0/P3: route data + length loads through the runtime
                // helpers so the hoist is correct for upgraded Map layouts and
                // for length-tracking / resize-aware views. Note: this snapshots
                // the data ptr + length once before the loop, so a resize that
                // happens INSIDE the loop is not reflected here; a future phase
                // can introduce per-iteration reload when needed.
                MIR_reg_t h_len = jm_call_1(mt, "js_typed_array_length", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, var->reg));
                MIR_reg_t h_data = jm_call_1(mt, "js_typed_array_current_data_ptr", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, var->reg));
                var->hoisted_data_reg = h_data;
                var->hoisted_len_reg = h_len;
                p4h_hoisted_vars[p4h_hoisted_count++] = var;
                log_debug("P4h: hoisted typed array data+len before while loop");
            }
        }
    }

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

    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_test)));
    jm_emit_label(mt, l_end);

    // P4h: clear hoisted array metadata
    for (int hi = 0; hi < p4h_hoisted_count; hi++) {
        p4h_hoisted_vars[hi]->hoisted_data_reg = 0;
        p4h_hoisted_vars[hi]->hoisted_len_reg = 0;
    }

    if (mt->iteration_depth > 0) mt->iteration_depth--;
    if (mt->loop_depth > 0) mt->loop_depth--;
}

void jm_transpile_for(JsMirTranspiler* mt, JsForNode* for_node) {
    // JS spec: 'var' declarations in for-init are function-scoped — they must be
    // visible after the loop ends. Only push a new scope for 'let'/'const' inits.
    bool init_is_var = false;
    bool init_is_lexical_decl = false;
    char for_var_init_name[128];
    for_var_init_name[0] = 0;
    char for_lexical_init_name[128];
    for_lexical_init_name[0] = 0;
    if (for_node->init && for_node->init->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
        JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)for_node->init;
        if (vd->kind == JS_VAR_VAR) {
            init_is_var = true;
            if (vd->declarations && vd->declarations->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)vd->declarations;
                if (d->id && d->id->node_type == JS_AST_NODE_IDENTIFIER) {
                    JsIdentifierNode* id = (JsIdentifierNode*)d->id;
                    snprintf(for_var_init_name, sizeof(for_var_init_name),
                        "_js_%.*s", (int)id->name->len, id->name->chars);
                }
            }
        }
        else {
            init_is_lexical_decl = true;
            if (vd->declarations && vd->declarations->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)vd->declarations;
                if (d->id && d->id->node_type == JS_AST_NODE_IDENTIFIER) {
                    JsIdentifierNode* id = (JsIdentifierNode*)d->id;
                    snprintf(for_lexical_init_name, sizeof(for_lexical_init_name),
                        "_js_%.*s", (int)id->name->len, id->name->chars);
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

    // --- For-loop specialization: detect and cache loop bound ---
    // Three tiers of test optimization:
    //   1. full_native:  both sides typed numeric → native compare + branch (existing)
    //   2. semi_native:  one side typed, other untyped but identifier/literal →
    //                    cache bound before loop, native compare each iteration (new)
    //   3. boxed:        no type info → boxed runtime comparison (fallback)
    bool semi_native_test = false;
    MIR_reg_t cached_bound = 0;
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

                    // Cache the bound ONCE before the loop.
                    cached_bound = jm_transpile_as_native(mt, bound_expr, bound_type, cached_cmp_target);
                    semi_native_test = true;
                }
            }
        }
    }

    // --- P4h: Hoist array metadata (data pointer + length) before the loop ---
    // Scan the loop body for subscript array accesses (arr[idx]), then hoist
    // the data pointer and length loads for typed arrays and regular arrays.
    // This avoids reloading them from memory every iteration.
    JsMirVarEntry* p4h_hoisted_vars[16];
    int p4h_hoisted_count = 0;
    if (for_node->body) {
        char arr_names[16][64];
        bool arr_unsafe[16];
        int arr_count = 0;
        // also scan test and update since they may reference arrays
        jm_scan_subscript_arrays(for_node->test, arr_names, arr_unsafe, &arr_count, 16);
        jm_scan_subscript_arrays(for_node->update, arr_names, arr_unsafe, &arr_count, 16);
        jm_scan_subscript_arrays(for_node->body, arr_names, arr_unsafe, &arr_count, 16);

        for (int ai = 0; ai < arr_count && p4h_hoisted_count < 16; ai++) {
            if (arr_unsafe[ai]) continue; // variable is reassigned in loop body
            char vname[128];
            snprintf(vname, sizeof(vname), "_js_%s", arr_names[ai]);
            JsMirVarEntry* var = jm_find_var(mt, vname);
            if (!var) continue;
            if (var->in_scope_env) continue; // captured by closure — reloaded each iteration
            if (var->hoisted_data_reg) continue; // already hoisted by outer loop

            if (var->typed_array_type >= 0) {
                // Js54 P0/P3: route through runtime helpers so the hoist is
                // correct for upgraded Map layouts and for length-tracking /
                // resize-aware views. Snapshots once before the loop.
                MIR_reg_t h_len = jm_call_1(mt, "js_typed_array_length", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, var->reg));
                MIR_reg_t h_data = jm_call_1(mt, "js_typed_array_current_data_ptr", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, var->reg));
                var->hoisted_data_reg = h_data;
                var->hoisted_len_reg = h_len;
                p4h_hoisted_vars[p4h_hoisted_count++] = var;
                log_debug("P4h: hoisted typed array '%s' data+len before for loop", arr_names[ai]);
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

    // If exception pending (e.g., from init destructuring or loop body), exit loop
    {
        MIR_reg_t for_exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_end),
            MIR_new_reg_op(mt->ctx, for_exc)));
    }

    // Reload scope-env variables so the loop condition sees values updated by
    // inner-function (closure) calls made during the previous iteration.
    jm_scope_env_reload_vars(mt);

    // Test
    if (for_node->test) {
        if (semi_native_test) {
            // Semi-native: read counter as native, compare with cached bound
            TypeId ct = jm_get_effective_type(mt, cached_counter_node);
            MIR_reg_t counter_reg = jm_transpile_as_native(mt, cached_counter_node, ct, cached_cmp_target);

            MIR_reg_t left_cmp  = cached_bound_on_right ? counter_reg  : cached_bound;
            MIR_reg_t right_cmp = cached_bound_on_right ? cached_bound : counter_reg;

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
        // v23c: check for pending exceptions after update expression
        // Only when inside try/catch so thrown exceptions reach the catch handler.
        // Outside try/catch, stale exceptions would cause spurious loop exits.
        if (mt->try_ctx_depth > 0 && !jm_is_native_type(jm_get_effective_type(mt, for_node->update))) {
            jm_emit_exc_propagate_check(mt);
        }
        if (init_is_lexical_decl && for_lexical_init_name[0] && mt->last_closure_has_env) {
            jm_scope_env_reload_vars(mt);
            JsMirVarEntry* loop_var = jm_find_var(mt, for_lexical_init_name);
            if (loop_var && loop_var->reg) {
                jm_write_last_closure_capture_if_matching(mt, for_lexical_init_name,
                    loop_var->reg, loop_var->type_id);
            }
        }
        jm_restore_last_closure_snapshot(mt, &saved_last_closure);
    }

    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_test)));
    jm_emit_label(mt, l_end);

    // P4h: clear hoisted array metadata set by this loop
    for (int hi = 0; hi < p4h_hoisted_count; hi++) {
        p4h_hoisted_vars[hi]->hoisted_data_reg = 0;
        p4h_hoisted_vars[hi]->hoisted_len_reg = 0;
    }

    if (init_is_var && for_var_init_name[0]) {
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

static bool jm_class_has_private_instance_brands(JsClassEntry* ce) {
    if (!ce) return false;
    for (int fi = 0; fi < ce->instance_field_count; fi++) {
        JsInstanceFieldEntry* inf = &ce->instance_fields[fi];
        if (!inf->computed && inf->name && jm_is_private_name(inf->name)) return true;
    }
    for (int mi = 0; mi < ce->method_count; mi++) {
        JsClassMethodEntry* me = &ce->methods[mi];
        if (!me->is_static && !me->is_constructor && me->name && jm_is_private_name(me->name)) return true;
    }
    return false;
}

static MIR_reg_t jm_emit_class_object_for_entry(JsMirTranspiler* mt, JsClassEntry* ce) {
    if (!mt || !ce || !ce->name) return 0;
    JsIdentifierNode tmp_id;
    memset(&tmp_id, 0, sizeof(tmp_id));
    tmp_id.base.node_type = JS_AST_NODE_IDENTIFIER;
    tmp_id.name = ce->name;
    return jm_transpile_box_item(mt, (JsAstNode*)&tmp_id);
}

static void jm_emit_class_instance_field_metadata_for_decl(JsMirTranspiler* mt, MIR_reg_t cls_obj, JsClassEntry* ce) {
    if (!mt || !ce) return;
    int metadata_count = 0;
    for (int fi = 0; fi < ce->instance_field_count; fi++) {
        JsInstanceFieldEntry* inf = &ce->instance_fields[fi];
        if (!inf->computed && inf->name) metadata_count++;
    }
    for (int mi = 0; mi < ce->method_count; mi++) {
        JsClassMethodEntry* me = &ce->methods[mi];
        if (!me->is_static && !me->is_constructor && me->name && jm_is_private_name(me->name)) {
            bool seen = false;
            for (int pi = 0; pi < mi; pi++) {
                JsClassMethodEntry* prev = &ce->methods[pi];
                if (prev->is_static || prev->is_constructor || !prev->name || !jm_is_private_name(prev->name)) continue;
                if (prev->name->len == me->name->len &&
                    memcmp(prev->name->chars, me->name->chars, (size_t)me->name->len) == 0) {
                    seen = true;
                    break;
                }
            }
            if (seen) continue;
            metadata_count++;
        }
    }
    if (metadata_count <= 0) return;

    MIR_reg_t count_key = jm_box_string_literal(mt, "__if_count__", 12);
    MIR_reg_t count_val = jm_box_int_const(mt, metadata_count);
    jm_call_3(mt, "js_property_set", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, count_key),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, count_val));

    int metadata_index = 0;
    for (int fi = 0; fi < ce->instance_field_count; fi++) {
        JsInstanceFieldEntry* inf = &ce->instance_fields[fi];
        if (inf->computed || !inf->name) continue;

        char key_slot[32];
        int key_slot_len = snprintf(key_slot, sizeof(key_slot), "__if_key_%d", metadata_index);
        MIR_reg_t key_slot_reg = jm_box_string_literal(mt, key_slot, key_slot_len);
        String* field_name = jm_class_private_name(mt, ce, inf->name);
        MIR_reg_t key_val = jm_box_string_literal(mt, field_name->chars, (int)field_name->len);
        jm_call_3(mt, "js_property_set", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key_slot_reg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key_val));

        char val_slot[32];
        int val_slot_len = snprintf(val_slot, sizeof(val_slot), "__if_val_%d", metadata_index);
        MIR_reg_t val_slot_reg = jm_box_string_literal(mt, val_slot, val_slot_len);
        MIR_reg_t field_val = jm_emit_undefined(mt);
        if (inf->initializer && inf->initializer->node_type == JS_AST_NODE_LITERAL) {
            field_val = jm_transpile_box_item(mt, inf->initializer);
        }
        jm_call_3(mt, "js_property_set", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val_slot_reg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, field_val));
        metadata_index++;
    }
    for (int mi = 0; mi < ce->method_count; mi++) {
        JsClassMethodEntry* me = &ce->methods[mi];
        if (me->is_static || me->is_constructor || !me->name || !jm_is_private_name(me->name)) continue;
        bool seen = false;
        for (int pi = 0; pi < mi; pi++) {
            JsClassMethodEntry* prev = &ce->methods[pi];
            if (prev->is_static || prev->is_constructor || !prev->name || !jm_is_private_name(prev->name)) continue;
            if (prev->name->len == me->name->len &&
                memcmp(prev->name->chars, me->name->chars, (size_t)me->name->len) == 0) {
                seen = true;
                break;
            }
        }
        if (seen) continue;

        char key_slot[32];
        int key_slot_len = snprintf(key_slot, sizeof(key_slot), "__if_key_%d", metadata_index);
        MIR_reg_t key_slot_reg = jm_box_string_literal(mt, key_slot, key_slot_len);
        String* method_name = jm_class_private_name(mt, ce, me->name);
        MIR_reg_t key_val = jm_box_string_literal(mt, method_name->chars, (int)method_name->len);
        jm_call_3(mt, "js_property_set", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key_slot_reg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key_val));

        char kind_slot[32];
        int kind_slot_len = snprintf(kind_slot, sizeof(kind_slot), "__if_kind_%d", metadata_index);
        MIR_reg_t kind_slot_reg = jm_box_string_literal(mt, kind_slot, kind_slot_len);
        MIR_reg_t kind_val = jm_box_int_const(mt, 1);
        jm_call_3(mt, "js_property_set", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, kind_slot_reg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, kind_val));
        metadata_index++;
    }
}

static void jm_emit_private_brand_add(JsMirTranspiler* mt, MIR_reg_t obj, MIR_reg_t cls_obj, String* private_name) {
    if (!mt || !obj || !cls_obj || !jm_is_private_name(private_name)) return;
    MIR_reg_t key = jm_box_string_literal(mt, private_name->chars, (int)private_name->len);
    jm_call_void_3(mt, "js_private_brand_add",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
    jm_emit_exc_propagate_check(mt);
}

static bool jm_private_instance_method_brand_seen(JsClassEntry* ce, int method_index) {
    if (!ce || method_index < 0 || method_index >= ce->method_count) return false;
    JsClassMethodEntry* me = &ce->methods[method_index];
    if (!me->name) return false;
    for (int pi = 0; pi < method_index; pi++) {
        JsClassMethodEntry* prev = &ce->methods[pi];
        if (prev->is_static || prev->is_constructor || !prev->name || !jm_is_private_name(prev->name)) continue;
        if (prev->name->len == me->name->len &&
            memcmp(prev->name->chars, me->name->chars, (size_t)me->name->len) == 0) {
            return true;
        }
    }
    return false;
}

static bool jm_private_static_method_brand_seen(JsClassEntry* ce, int method_index) {
    if (!ce || method_index < 0 || method_index >= ce->method_count) return false;
    JsClassMethodEntry* me = &ce->methods[method_index];
    if (!me->is_static || me->is_constructor || !me->name || !jm_is_private_name(me->name)) return false;
    for (int pi = 0; pi < method_index; pi++) {
        JsClassMethodEntry* prev = &ce->methods[pi];
        if (!prev->is_static || prev->is_constructor || !prev->name || !jm_is_private_name(prev->name)) continue;
        if (prev->name->len == me->name->len &&
            memcmp(prev->name->chars, me->name->chars, (size_t)me->name->len) == 0) {
            return true;
        }
    }
    return false;
}

static void jm_emit_private_instance_method_brands(JsMirTranspiler* mt, MIR_reg_t obj, MIR_reg_t cls_obj, JsClassEntry* ce) {
    if (!mt || !ce || !cls_obj) return;
    for (int mi = 0; mi < ce->method_count; mi++) {
        JsClassMethodEntry* me = &ce->methods[mi];
        if (me->is_static || me->is_constructor || !me->name || !jm_is_private_name(me->name)) continue;
        if (jm_private_instance_method_brand_seen(ce, mi)) continue;
        String* method_name = jm_class_private_name(mt, ce, me->name);
        jm_emit_private_brand_add(mt, obj, cls_obj, method_name);
    }
}

static void jm_emit_own_instance_fields_on_object(JsMirTranspiler* mt, JsClassEntry* ce, MIR_reg_t obj, MIR_reg_t cls_obj, bool include_private) {
    if (!mt || !ce || !obj) return;
    JsClassEntry* saved_current_class = mt->current_class;
    mt->current_class = ce;
    // js_private_field_init_begin keeps the field-initializer early-error context
    // (eval restrictions) on. The brand-check bypass is NOT taken from this flag:
    // each field's *declaration* set goes through js_private_field_define (scoped
    // bypass), while initializer *expressions* run brand-checked, so referencing a
    // not-yet-installed private member throws per spec.
    jm_call_void_0(mt, "js_private_field_init_begin");
    if (include_private && cls_obj) {
        jm_emit_private_instance_method_brands(mt, obj, cls_obj, ce);
    }
    for (int fi = 0; fi < ce->instance_field_count; fi++) {
        JsInstanceFieldEntry* inf = &ce->instance_fields[fi];
        MIR_reg_t key = 0;
        String* private_field_name = NULL;
        if (inf->computed && inf->key_expr) {
            if (inf->key_module_var_index >= 0) {
                key = jm_call_1(mt, "js_get_module_var", MIR_T_I64,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)inf->key_module_var_index));
            } else {
                key = jm_transpile_box_item(mt, inf->key_expr);
                key = jm_call_1(mt, "js_to_property_key", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
            }
        } else if (inf->name) {
            String* field_name = jm_class_private_name(mt, ce, inf->name);
            if (jm_is_private_name(field_name)) private_field_name = field_name;
            key = jm_box_string_literal(mt, field_name->chars, (int)field_name->len);
        } else if (inf->key_expr) {
            key = jm_transpile_box_item(mt, inf->key_expr);
            key = jm_call_1(mt, "js_to_property_key", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
        } else {
            continue;
        }
        if (private_field_name && !include_private) continue;

        MIR_reg_t val = inf->initializer ? jm_transpile_box_item(mt, inf->initializer) : jm_emit_undefined(mt);
        if (private_field_name) {
            jm_call_3(mt, "js_private_field_define", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            if (cls_obj) jm_emit_private_brand_add(mt, obj, cls_obj, private_field_name);
        } else {
            jm_call_3(mt, "js_create_data_property", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        }
        jm_emit_exc_propagate_check(mt);
    }
    jm_call_void_0(mt, "js_private_field_init_end");
    mt->current_class = saved_current_class;
}

static void jm_emit_set_function_home_class(JsMirTranspiler* mt, MIR_reg_t fn_item, MIR_reg_t cls_obj) {
    if (!mt || !fn_item || !cls_obj) return;
    MIR_reg_t home_key = jm_box_string_literal(mt, "__home_class__", 14);
    jm_call_3(mt, "js_property_set", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_item),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, home_key),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
}

static void jm_emit_set_private_class_index(JsMirTranspiler* mt, MIR_reg_t cls_obj, JsClassEntry* ce) {
    if (!mt || !cls_obj || !ce || mt->class_count <= 0) return;
    int class_index = (int)(ce - mt->class_entries);
    jm_call_void_2(mt, "js_set_private_class_index",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
        MIR_T_I64, MIR_new_int_op(mt->ctx, class_index));
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
            jm_call_void_1(mt, "js_check_class_static_field_key",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
            jm_emit_exc_propagate_check(mt);
        }
        MIR_reg_t val = sf->initializer ? jm_transpile_box_item(mt, sf->initializer) : jm_emit_undefined(mt);
        jm_emit_exc_propagate_check(mt);
        jm_call_void_2(mt, "js_set_function_name_if_anonymous",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
        jm_call_3(mt, "js_create_data_property", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        if (sf->name && jm_is_private_name(sf->name)) {
            jm_call_void_3(mt, "js_private_brand_add",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
        }
        return;
    }

    MIR_reg_t val = sf->initializer ? jm_transpile_box_item(mt, sf->initializer) : jm_emit_undefined(mt);
    jm_emit_exc_propagate_check(mt);
    if (sf->module_var_index >= 0) {
        jm_call_void_2(mt, "js_set_module_var",
            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)sf->module_var_index),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
    }
    if (sf->name) {
        char display_buf[256];
        const char* display_name = sf->name->chars;
        if (strncmp(display_name, "__private_", 10) == 0) {
            int len = snprintf(display_buf, sizeof(display_buf), "#%s", display_name + 10);
            display_name = display_buf;
            (void)len;
        }
        MIR_reg_t fn_name = jm_box_string_literal(mt, display_name, (int)strlen(display_name));
        jm_call_void_2(mt, "js_set_function_name_if_anonymous",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_name));
        MIR_reg_t key = jm_box_string_literal(mt, sf->name->chars, (int)sf->name->len);
        jm_call_void_1(mt, "js_check_class_static_field_key",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
        jm_emit_exc_propagate_check(mt);
        jm_call_3(mt, "js_create_data_property", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        if (jm_is_private_name(sf->name)) {
            jm_call_void_3(mt, "js_private_brand_add",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
        }
    } else if (sf->key_expr) {
        MIR_reg_t key = jm_transpile_box_item(mt, sf->key_expr);
        key = jm_call_1(mt, "js_to_property_key", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
        jm_call_void_1(mt, "js_check_class_static_field_key",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
        jm_emit_exc_propagate_check(mt);
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

void jm_emit_class_static_block(JsMirTranspiler* mt, JsClassEntry* ce, JsAstNode* block) {
    if (!mt || !block) return;
    JsClassEntry* saved_current_class = mt->current_class;
    int saved_hoist = mt->var_hoist_depth;
    mt->current_class = ce ? ce : mt->current_class;
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
    mt->current_class = saved_current_class;
}

// new expression: new TypedArray(len), new Array(len), new Object()
static bool jm_class_name_is_unique(JsMirTranspiler* mt, const char* name, int len) {
    if (!mt || !name || len <= 0) return false;
    int found = 0;
    for (int ci = 0; ci < mt->class_count; ci++) {
        JsClassEntry* ce = &mt->class_entries[ci];
        if (!ce->name || (int)ce->name->len != len) continue;
        if (strncmp(ce->name->chars, name, len) != 0) continue;
        found++;
        if (found > 1) return false;
    }
    return found == 1;
}

static bool jm_ctor_name_is_builtin_collision(const char* name, int len) {
    if (!name || len <= 0) return false;
    switch (len) {
        case 3:
            return strncmp(name, "Map", 3) == 0 ||
                   strncmp(name, "Set", 3) == 0 ||
                   strncmp(name, "URL", 3) == 0;
        case 4:
            return strncmp(name, "Date", 4) == 0;
        case 5:
            return strncmp(name, "Array", 5) == 0 ||
                   strncmp(name, "Error", 5) == 0 ||
                   strncmp(name, "Image", 5) == 0 ||
                   strncmp(name, "Proxy", 5) == 0;
        case 6:
            return strncmp(name, "Object", 6) == 0 ||
                   strncmp(name, "Number", 6) == 0 ||
                   strncmp(name, "String", 6) == 0 ||
                   strncmp(name, "BigInt", 6) == 0 ||
                   strncmp(name, "RegExp", 6) == 0 ||
                   strncmp(name, "Buffer", 6) == 0;
        case 7:
            return strncmp(name, "Boolean", 7) == 0 ||
                   strncmp(name, "WeakMap", 7) == 0 ||
                   strncmp(name, "WeakSet", 7) == 0 ||
                   strncmp(name, "Promise", 7) == 0;
        case 8:
            return strncmp(name, "URIError", 8) == 0 ||
                   strncmp(name, "Function", 8) == 0 ||
                   strncmp(name, "DataView", 8) == 0;
        case 9:
            return strncmp(name, "TypeError", 9) == 0 ||
                   strncmp(name, "EvalError", 9) == 0 ||
                   strncmp(name, "Int8Array", 9) == 0;
        case 10:
            return strncmp(name, "RangeError", 10) == 0 ||
                   strncmp(name, "Int32Array", 10) == 0 ||
                   strncmp(name, "Int16Array", 10) == 0 ||
                   strncmp(name, "Uint8Array", 10) == 0;
        case 11:
            return strncmp(name, "SyntaxError", 11) == 0 ||
                   strncmp(name, "Uint32Array", 11) == 0 ||
                   strncmp(name, "Uint16Array", 11) == 0 ||
                   strncmp(name, "ArrayBuffer", 11) == 0 ||
                   strncmp(name, "TextEncoder", 11) == 0 ||
                   strncmp(name, "TextDecoder", 11) == 0;
        case 12:
            return strncmp(name, "Float64Array", 12) == 0 ||
                   strncmp(name, "Float32Array", 12) == 0;
        case 14:
            return strncmp(name, "ReferenceError", 14) == 0 ||
                   strncmp(name, "AggregateError", 14) == 0 ||
                   strncmp(name, "ReadableStream", 14) == 0 ||
                   strncmp(name, "WritableStream", 14) == 0 ||
                   strncmp(name, "XMLHttpRequest", 14) == 0;
        case 15:
            return strncmp(name, "OffscreenCanvas", 15) == 0;
        case 17:
            return strncmp(name, "Uint8ClampedArray", 17) == 0 ||
                   strncmp(name, "SharedArrayBuffer", 17) == 0;
        default:
            return false;
    }
}

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
        return jm_call_2(mt, "js_apply_constructor", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, callee),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, args_arr));
    }
    MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
    return jm_call_3(mt, "js_new_from_class_object", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, callee),
        MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
        MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
}

MIR_reg_t jm_transpile_new_expr(JsMirTranspiler* mt, JsCallNode* call) {
    if (!call->callee) return jm_emit_null(mt);

    const char* ctor_name = NULL;
    int ctor_len = 0;
    if (call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
        ctor_name = id->name->chars;
        ctor_len = (int)id->name->len;
    }

    if (!ctor_name) {
        // Non-identifier callee (e.g., new (expr)(), new obj.method(), etc.)
        // Use dynamic dispatch which handles type checking and TypeError for non-constructors
        int arg_count = jm_count_args(call->arguments);
        return jm_emit_dynamic_new_expr(mt, call, arg_count);
    }

    // Count arguments (but DON'T evaluate yet — evaluation happens in the specific path)
    int arg_count = jm_count_args(call->arguments);
    bool new_has_spread = false;
    for (JsAstNode* chk = call->arguments; chk; chk = chk->next) {
        if (chk->node_type == JS_AST_NODE_SPREAD_ELEMENT) { new_has_spread = true; break; }
    }

    if (new_has_spread && call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
        MIR_reg_t callee = jm_transpile_box_item(mt, call->callee);
        MIR_reg_t args_arr = jm_build_spread_args_array(mt, call->arguments);
        return jm_call_2(mt, "js_apply_constructor", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, callee),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, args_arr));
    }

    if (ctor_len == 7 && strncmp(ctor_name, "Promise", 7) == 0) {
        return jm_emit_dynamic_new_expr(mt, call, arg_count);
    }

    // Check if it's a built-in type that needs early first-arg evaluation
    bool is_builtin = false;
    int typed_array_type = -1;
    bool is_arraybuffer = false;
    bool is_sharedarraybuffer = false;
    bool is_dataview = false;
    bool is_buffer = false;
    if (ctor_len == 10 && strncmp(ctor_name, "Int32Array", 10) == 0) { typed_array_type = 4; is_builtin = true; }
    else if (ctor_len == 10 && strncmp(ctor_name, "Int16Array", 10) == 0) { typed_array_type = 2; is_builtin = true; }
    else if (ctor_len == 9 && strncmp(ctor_name, "Int8Array", 9) == 0) { typed_array_type = 0; is_builtin = true; }
    else if (ctor_len == 11 && strncmp(ctor_name, "Uint32Array", 11) == 0) { typed_array_type = 5; is_builtin = true; }
    else if (ctor_len == 11 && strncmp(ctor_name, "Uint16Array", 11) == 0) { typed_array_type = 3; is_builtin = true; }
    else if (ctor_len == 10 && strncmp(ctor_name, "Uint8Array", 10) == 0) { typed_array_type = 1; is_builtin = true; }
    else if (ctor_len == 17 && strncmp(ctor_name, "Uint8ClampedArray", 17) == 0) { typed_array_type = JS_TYPED_UINT8_CLAMPED; is_builtin = true; }
    else if (ctor_len == 12 && strncmp(ctor_name, "Float64Array", 12) == 0) { typed_array_type = 7; is_builtin = true; }
    else if (ctor_len == 12 && strncmp(ctor_name, "Float32Array", 12) == 0) { typed_array_type = 6; is_builtin = true; }
    else if (ctor_len == 11 && strncmp(ctor_name, "ArrayBuffer", 11) == 0) { is_arraybuffer = true; is_builtin = true; }
    else if (ctor_len == 17 && strncmp(ctor_name, "SharedArrayBuffer", 17) == 0) { is_sharedarraybuffer = true; is_builtin = true; }
    else if (ctor_len == 8 && strncmp(ctor_name, "DataView", 8) == 0) { is_dataview = true; is_builtin = true; }
    else if (ctor_len == 5 && strncmp(ctor_name, "Array", 5) == 0) is_builtin = true;
    else if (ctor_len == 6 && strncmp(ctor_name, "Object", 6) == 0) is_builtin = true;
    else if (ctor_len == 6 && strncmp(ctor_name, "Number", 6) == 0) is_builtin = true;
    else if (ctor_len == 7 && strncmp(ctor_name, "Boolean", 7) == 0) is_builtin = true;
    else if (ctor_len == 6 && strncmp(ctor_name, "String", 6) == 0) is_builtin = true;
    else if (ctor_len == 5 && strncmp(ctor_name, "Error", 5) == 0) is_builtin = true;
    // v11: Error subclasses
    else if (ctor_len == 9 && strncmp(ctor_name, "TypeError", 9) == 0) is_builtin = true;
    else if (ctor_len == 10 && strncmp(ctor_name, "RangeError", 10) == 0) is_builtin = true;
    else if (ctor_len == 11 && strncmp(ctor_name, "SyntaxError", 11) == 0) is_builtin = true;
    else if (ctor_len == 14 && strncmp(ctor_name, "ReferenceError", 14) == 0) is_builtin = true;
    else if (ctor_len == 8 && strncmp(ctor_name, "URIError", 8) == 0) is_builtin = true;
    else if (ctor_len == 9 && strncmp(ctor_name, "EvalError", 9) == 0) is_builtin = true;
    else if (ctor_len == 14 && strncmp(ctor_name, "AggregateError", 14) == 0) is_builtin = true;
    // v11: Map/Set
    else if (ctor_len == 3 && strncmp(ctor_name, "Map", 3) == 0) is_builtin = true;
    else if (ctor_len == 3 && strncmp(ctor_name, "Set", 3) == 0) is_builtin = true;
    // WeakMap/WeakSet
    else if (ctor_len == 7 && strncmp(ctor_name, "WeakMap", 7) == 0) is_builtin = true;
    else if (ctor_len == 7 && strncmp(ctor_name, "WeakSet", 7) == 0) is_builtin = true;
    // v14: Promise
    else if (ctor_len == 7 && strncmp(ctor_name, "Promise", 7) == 0) is_builtin = true;
    // RegExp constructor
    else if (ctor_len == 6 && strncmp(ctor_name, "RegExp", 6) == 0) is_builtin = true;
    // URL constructor
    else if (ctor_len == 3 && strncmp(ctor_name, "URL", 3) == 0) is_builtin = true;
    // Date constructor
    else if (ctor_len == 4 && strncmp(ctor_name, "Date", 4) == 0) is_builtin = true;
    // Proxy constructor (pass-through)
    else if (ctor_len == 5 && strncmp(ctor_name, "Proxy", 5) == 0) is_builtin = true;
    // XMLHttpRequest constructor (Radiant browser context)
    else if (ctor_len == 14 && strncmp(ctor_name, "XMLHttpRequest", 14) == 0) is_builtin = true;
    // Image constructor — `new Image(width?, height?)` (HTML §4.8.4)
    else if (ctor_len == 5 && strncmp(ctor_name, "Image", 5) == 0) is_builtin = true;
    // Buffer constructor (deprecated new Buffer(size/string/array))
    else if (ctor_len == 6 && strncmp(ctor_name, "Buffer", 6) == 0) { is_buffer = true; is_builtin = true; }
    // OffscreenCanvas constructor (Canvas text measurement)
    else if (ctor_len == 15 && strncmp(ctor_name, "OffscreenCanvas", 15) == 0) is_builtin = true;

    if (is_builtin && call->callee->node_type == JS_AST_NODE_IDENTIFIER &&
            jm_ctor_name_is_builtin_collision(ctor_name, ctor_len)) {
        JsClassEntry* user_ce = jm_find_class(mt, ctor_name, ctor_len);
        if (user_ce && jm_class_name_is_unique(mt, ctor_name, ctor_len)) {
            is_builtin = false;
        }
    }

    // new BigInt() — BigInt is not a constructor (must be called as function)
    if (ctor_len == 6 && strncmp(ctor_name, "BigInt", 6) == 0) {
        return jm_call_0(mt, "js_bigint_not_constructor", MIR_T_I64);
    }

    // Only evaluate first arg eagerly for built-in types
    MIR_reg_t first_arg = 0;
    if (is_builtin && call->arguments &&
        !(ctor_len == 4 && strncmp(ctor_name, "Date", 4) == 0 && arg_count >= 2)) {
        first_arg = jm_transpile_box_item(mt, call->arguments);
    }

    // new ArrayBuffer(byteLength)
    if (is_arraybuffer) {
        MIR_reg_t len_arg = first_arg ? first_arg : jm_emit_undefined(mt);
        MIR_reg_t options_arg = 0;
        JsAstNode* arg2 = call->arguments ? call->arguments->next : NULL;
        if (arg2) options_arg = jm_transpile_box_item(mt, arg2);
        else options_arg = jm_emit_undefined(mt);
        return jm_call_2(mt, "js_arraybuffer_construct_resizable", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, len_arg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, options_arg));
    }

    // new SharedArrayBuffer(byteLength)
    if (is_sharedarraybuffer) {
        MIR_reg_t len_arg = first_arg ? first_arg : jm_emit_undefined(mt);
        MIR_reg_t options_arg = 0;
        JsAstNode* arg2 = call->arguments ? call->arguments->next : NULL;
        if (arg2) options_arg = jm_transpile_box_item(mt, arg2);
        else options_arg = jm_emit_undefined(mt);
        return jm_call_2(mt, "js_sharedarraybuffer_construct_with_options", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, len_arg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, options_arg));
    }

    // new DataView(buffer [, byteOffset [, byteLength]])
    if (is_dataview) {
        MIR_reg_t buf_arg = first_arg ? first_arg : jm_emit_null(mt);
        MIR_reg_t offset_arg = 0;
        MIR_reg_t len_arg = 0;
        JsAstNode* arg2 = call->arguments ? call->arguments->next : NULL;
        if (arg2) {
            offset_arg = jm_transpile_box_item(mt, arg2);
            JsAstNode* arg3 = arg2->next;
            if (arg3) len_arg = jm_transpile_box_item(mt, arg3);
        }
        MIR_op_t off_op = offset_arg ? MIR_new_reg_op(mt->ctx, offset_arg) : MIR_new_int_op(mt->ctx, (int64_t)i2it(0));
        MIR_op_t len_op = len_arg ? MIR_new_reg_op(mt->ctx, len_arg) : MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED);
        return jm_call_3(mt, "js_dataview_new", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, buf_arg),
            MIR_T_I64, off_op,
            MIR_T_I64, len_op);
    }

    // new Buffer(arg [, encoding]) — deprecated constructor
    if (is_buffer) {
        MIR_reg_t arg_val = first_arg ? first_arg : jm_emit_undefined(mt);
        MIR_reg_t enc_arg = 0;
        JsAstNode* arg2 = call->arguments ? call->arguments->next : NULL;
        if (arg2) enc_arg = jm_transpile_box_item(mt, arg2);
        MIR_reg_t enc_val = enc_arg ? enc_arg : jm_emit_undefined(mt);
        return jm_call_2(mt, "js_buffer_construct", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, arg_val),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, enc_val));
    }

    // new TypedArray(arg [, byteOffset [, length]])
    if (typed_array_type >= 0) {
        MIR_reg_t arg_val = first_arg ? first_arg : jm_emit_null(mt);
        MIR_reg_t offset_arg = 0;
        MIR_reg_t len_arg_r = 0;
        JsAstNode* arg2 = call->arguments ? call->arguments->next : NULL;
        if (arg2) {
            offset_arg = jm_transpile_box_item(mt, arg2);
            JsAstNode* arg3 = arg2->next;
            if (arg3) len_arg_r = jm_transpile_box_item(mt, arg3);
        }
        MIR_op_t off_op = offset_arg ? MIR_new_reg_op(mt->ctx, offset_arg) : MIR_new_int_op(mt->ctx, 0);
        MIR_op_t len_op = len_arg_r ? MIR_new_reg_op(mt->ctx, len_arg_r) : MIR_new_int_op(mt->ctx, -1);
        return jm_call_5(mt, "js_typed_array_construct", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, typed_array_type),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, arg_val),
            MIR_T_I64, off_op,
            MIR_T_I64, len_op,
            MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
    }

    // new Array(len) or new Array(a,b,c)
    if (ctor_len == 5 && strncmp(ctor_name, "Array", 5) == 0) {
        if (arg_count == 0) {
            // new Array() → empty array
            return jm_call_1(mt, "js_array_new", MIR_T_I64,
                MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
        } else if (arg_count == 1) {
            // new Array(x) → js_array_new_from_item handles the JS spec:
            // integer → sparse array, anything else → [x]
            return jm_call_1(mt, "js_array_new_from_item", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, first_arg));
        } else {
            // new Array(a,b,c): create array from elements
            MIR_reg_t array = jm_call_1(mt, "js_array_new", MIR_T_I64,
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)arg_count));
            // first_arg is already evaluated; set it at index 0
            MIR_reg_t bidx0 = jm_box_int_const(mt, 0);
            jm_call_3(mt, "js_array_set", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, array),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, bidx0),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, first_arg));
            // evaluate and set remaining args
            JsAstNode* arg = call->arguments->next;
            for (int idx = 1; arg; idx++, arg = arg->next) {
                MIR_reg_t bidx = jm_box_int_const(mt, idx);
                MIR_reg_t val = jm_transpile_box_item(mt, arg);
                jm_call_3(mt, "js_array_set", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, array),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, bidx),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            }
            return array;
        }
    }

    // new Object() / new Object(arg) — ToObject conversion
    if (ctor_len == 6 && strncmp(ctor_name, "Object", 6) == 0) {
        if (!first_arg) return jm_call_0(mt, "js_new_object", MIR_T_I64);
        return jm_call_1(mt, "js_to_object", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, first_arg));
    }

    // new Number(arg) — wrapper object with __primitiveValue__ (checks symbol)
    if (ctor_len == 6 && strncmp(ctor_name, "Number", 6) == 0) {
        MIR_reg_t arg_val = first_arg ? first_arg : jm_box_int_const(mt, 0);
        return jm_call_1(mt, "js_new_number_checked", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, arg_val));
    }

    // new Boolean(arg) — wrapper object with __primitiveValue__
    if (ctor_len == 7 && strncmp(ctor_name, "Boolean", 7) == 0) {
        MIR_reg_t arg_val = first_arg ? first_arg : jm_emit_null(mt);
        return jm_call_1(mt, "js_new_boolean_wrapper", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, arg_val));
    }

    // new String(arg) — wrapper object with __primitiveValue__
    if (ctor_len == 6 && strncmp(ctor_name, "String", 6) == 0) {
        MIR_reg_t arg_val = first_arg ? first_arg : jm_box_string_literal(mt, "", 0);
        return jm_call_1(mt, "js_new_string_wrapper", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, arg_val));
    }

    // new Function(param1, param2, ..., body) — dynamic function compilation
    if (ctor_len == 8 && strncmp(ctor_name, "Function", 8) == 0) {
        MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
        return jm_call_2(mt, "js_new_function_from_string", MIR_T_I64,
            MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
            MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
    }

    // new Error(message, options) — built-in Error constructor with compile-time stack trace
    if (ctor_len == 5 && strncmp(ctor_name, "Error", 5) == 0) {
        MIR_reg_t msg_arg = first_arg ? first_arg : jm_emit_undefined(mt);
        MIR_reg_t stack_arg = jm_build_error_stack_string(mt, "Error");
        MIR_reg_t err = jm_call_2(mt, "js_new_error_with_stack", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, msg_arg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, stack_arg));
        // ES2022: Error.cause from options object
        if (arg_count >= 2 && call->arguments && call->arguments->next) {
            MIR_reg_t opts = jm_transpile_box_item(mt, call->arguments->next);
            err = jm_call_2(mt, "js_error_set_cause", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, err),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, opts));
        }
        return err;
    }

    // v11: new TypeError/RangeError/SyntaxError/ReferenceError/URIError/EvalError(message, options)
    if ((ctor_len == 9 && strncmp(ctor_name, "TypeError", 9) == 0) ||
        (ctor_len == 10 && strncmp(ctor_name, "RangeError", 10) == 0) ||
        (ctor_len == 11 && strncmp(ctor_name, "SyntaxError", 11) == 0) ||
        (ctor_len == 14 && strncmp(ctor_name, "ReferenceError", 14) == 0) ||
        (ctor_len == 8 && strncmp(ctor_name, "URIError", 8) == 0) ||
        (ctor_len == 9 && strncmp(ctor_name, "EvalError", 9) == 0)) {
        MIR_reg_t name_arg = jm_box_string_literal(mt, ctor_name, ctor_len);
        MIR_reg_t msg_arg = first_arg ? first_arg : jm_emit_undefined(mt);
        MIR_reg_t stack_arg = jm_build_error_stack_string(mt, ctor_name);
        MIR_reg_t err = jm_call_3(mt, "js_new_error_with_name_stack", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, name_arg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, msg_arg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, stack_arg));
        // ES2022: Error.cause from options object
        if (arg_count >= 2 && call->arguments && call->arguments->next) {
            MIR_reg_t opts = jm_transpile_box_item(mt, call->arguments->next);
            err = jm_call_2(mt, "js_error_set_cause", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, err),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, opts));
        }
        return err;
    }

    // new AggregateError(errors, message)
    if (ctor_len == 14 && strncmp(ctor_name, "AggregateError", 14) == 0) {
        MIR_reg_t errors_arg = first_arg ? first_arg : jm_emit_null(mt);
        MIR_reg_t msg_arg;
        if (arg_count >= 2 && call->arguments && call->arguments->next) {
            msg_arg = jm_transpile_box_item(mt, call->arguments->next);
        } else {
            msg_arg = jm_emit_undefined(mt);
        }
        MIR_reg_t err = jm_call_2(mt, "js_new_aggregate_error", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, errors_arg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, msg_arg));
        // ES2022: Error.cause from options object
        if (arg_count >= 3 && call->arguments && call->arguments->next && call->arguments->next->next) {
            MIR_reg_t opts = jm_transpile_box_item(mt, call->arguments->next->next);
            err = jm_call_2(mt, "js_error_set_cause", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, err),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, opts));
        }
        return err;
    }

    // new XMLHttpRequest() — returns XHR object with open/send/etc methods
    if (ctor_len == 14 && strncmp(ctor_name, "XMLHttpRequest", 14) == 0) {
        return jm_call_0(mt, "js_xhr_new", MIR_T_I64);
    }

    // new Image(width?, height?) — Phase 8C: HTMLImageElement constructor.
    if (ctor_len == 5 && strncmp(ctor_name, "Image", 5) == 0) {
        MIR_reg_t w_arg = first_arg ? first_arg : jm_emit_undefined(mt);
        MIR_reg_t h_arg = 0;
        JsAstNode* arg2 = call->arguments ? call->arguments->next : NULL;
        if (arg2) h_arg = jm_transpile_box_item(mt, arg2);
        MIR_op_t h_op = h_arg ? MIR_new_reg_op(mt->ctx, h_arg)
                              : MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED);
        return jm_call_3(mt, "js_image_construct", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, w_arg),
            MIR_T_I64, h_op,
            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)arg_count));
    }

    // new Date() — returns a Date object with getTime() method
    // Used by raytrace3d: var startDate = new Date().getTime();
    if (ctor_len == 4 && strncmp(ctor_name, "Date", 4) == 0) {
        if (arg_count >= 2) {
            // v20: new Date(year, month, day, hours, min, sec, ms) - multi-arg
            MIR_reg_t args_arr = jm_build_spread_args_array(mt, call->arguments);
            return jm_call_1(mt, "js_date_new_multi", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args_arr));
        }
        if (first_arg) {
            return jm_call_1(mt, "js_date_new_from", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, first_arg));
        }
        return jm_call_0(mt, "js_date_new", MIR_T_I64);
    }

    // v11: new Map() / new Set()
    if (is_builtin && ctor_len == 3 && strncmp(ctor_name, "Map", 3) == 0) {
        if (first_arg) {
            return jm_call_1(mt, "js_map_collection_new_from", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, first_arg));
        }
        return jm_call_0(mt, "js_map_collection_new", MIR_T_I64);
    }
    if (is_builtin && ctor_len == 3 && strncmp(ctor_name, "Set", 3) == 0) {
        if (first_arg) {
            return jm_call_1(mt, "js_set_collection_new_from", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, first_arg));
        }
        return jm_call_0(mt, "js_set_collection_new", MIR_T_I64);
    }

    // v14: new Promise(executor)
    if (ctor_len == 7 && strncmp(ctor_name, "Promise", 7) == 0) {
        MIR_reg_t executor_arg = first_arg ? first_arg : jm_emit_null(mt);
        return jm_call_1(mt, "js_promise_create", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, executor_arg));
    }

    // Phase 3: new TextEncoder() / new TextDecoder()
    if (ctor_len == 11 && strncmp(ctor_name, "TextEncoder", 11) == 0) {
        return jm_call_0(mt, "js_text_encoder_new", MIR_T_I64);
    }
    if (ctor_len == 11 && strncmp(ctor_name, "TextDecoder", 11) == 0) {
        MIR_reg_t enc_arg = call->arguments ? jm_transpile_box_item(mt, call->arguments) : jm_emit_null(mt);
        return jm_call_1(mt, "js_text_decoder_new", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, enc_arg));
    }

    // OffscreenCanvas(width, height)
    if (ctor_len == 15 && strncmp(ctor_name, "OffscreenCanvas", 15) == 0) {
        MIR_reg_t w_arg = first_arg ? first_arg : jm_box_int_const(mt, 300);
        MIR_reg_t h_arg = 0;
        JsAstNode* arg2 = call->arguments ? call->arguments->next : NULL;
        if (arg2) h_arg = jm_transpile_box_item(mt, arg2);
        else h_arg = jm_box_int_const(mt, 150);
        return jm_call_2(mt, "js_offscreen_canvas_new", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, w_arg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, h_arg));
    }

    // Phase 3: new WeakMap() / new WeakSet()
    if (ctor_len == 7 && strncmp(ctor_name, "WeakMap", 7) == 0) {
        if (first_arg) {
            return jm_call_1(mt, "js_weakmap_new_with_iter", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, first_arg));
        }
        return jm_call_0(mt, "js_weakmap_new", MIR_T_I64);
    }
    if (ctor_len == 7 && strncmp(ctor_name, "WeakSet", 7) == 0) {
        if (first_arg) {
            return jm_call_1(mt, "js_weakset_new_with_iter", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, first_arg));
        }
        return jm_call_0(mt, "js_weakset_new", MIR_T_I64);
    }

    // new RegExp(pattern [, flags])
    if (ctor_len == 6 && strncmp(ctor_name, "RegExp", 6) == 0) {
        MIR_reg_t pattern_arg = first_arg ? first_arg : jm_emit_undefined(mt);
        MIR_reg_t flags_arg;
        JsAstNode* arg2 = call->arguments ? call->arguments->next : NULL;
        if (arg2) {
            flags_arg = jm_transpile_box_item(mt, arg2);
        } else {
            flags_arg = jm_emit_undefined(mt);
        }
        return jm_call_2(mt, "js_regexp_construct", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, pattern_arg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, flags_arg));
    }

    // new URL(input [, base])
    if (ctor_len == 3 && strncmp(ctor_name, "URL", 3) == 0) {
        MIR_reg_t input_arg = first_arg ? first_arg : jm_emit_null(mt);
        JsAstNode* arg2 = call->arguments ? call->arguments->next : NULL;
        if (arg2) {
            MIR_reg_t base_arg = jm_transpile_box_item(mt, arg2);
            return jm_call_2(mt, "js_url_construct_with_base", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, input_arg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, base_arg));
        }
        return jm_call_1(mt, "js_url_construct", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, input_arg));
    }

    // new ReadableStream([underlyingSource [, queuingStrategy]])
    if (ctor_len == 14 && strncmp(ctor_name, "ReadableStream", 14) == 0) {
        return jm_call_0(mt, "js_readable_stream_new", MIR_T_I64);
    }
    // new WritableStream([underlyingSink [, queuingStrategy]])
    if (ctor_len == 14 && strncmp(ctor_name, "WritableStream", 14) == 0) {
        return jm_call_0(mt, "js_writable_stream_new", MIR_T_I64);
    }

    // new Proxy(target, handler) — create proxy with handler traps
    if (ctor_len == 5 && strncmp(ctor_name, "Proxy", 5) == 0) {
        MIR_reg_t target_arg = first_arg ? first_arg : jm_emit_null(mt);
        JsAstNode* arg2 = call->arguments ? call->arguments->next : NULL;
        MIR_reg_t handler_arg = arg2 ? jm_transpile_box_item(mt, arg2) : jm_emit_null(mt);
        return jm_call_2(mt, "js_proxy_new", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, target_arg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, handler_arg));
    }

    // User-defined class instantiation: new ClassName(args)
    JsClassEntry* ce = jm_find_class(mt, ctor_name, ctor_len);
    if (ce && !jm_class_name_is_unique(mt, ctor_name, ctor_len)) {
        return jm_emit_dynamic_new_expr(mt, call, arg_count);
    }
    if (ce) {
        // Create new object — use pre-shaped allocation when constructor has this.prop assigns
        // so that P3 (js_set_shaped_slot) and P4 (js_get_shaped_slot) can use slot-indexed access.
        // Skip pre-shaping when instance fields are present: field inits run before ctor body
        // and use js_property_set which manages the shape dynamically.
        // Note: P3 is disabled for constructors of superclasses (see superclass resolution pass)
        // so that super() calls use js_property_set which works on any object shape.
        MIR_reg_t obj;

        // Find ctor_fc with ctor_props: check own constructor first
        JsFuncCollected* ctor_fc = NULL;
        if (ce->constructor && ce->constructor->fc &&
            ce->constructor->fc->ctor_prop_count > 0) {
            ctor_fc = ce->constructor->fc;
        }

        if (ctor_fc && ctor_fc->ctor_prop_count > 0) {
            // Use js_alloc_env instead of MIR_ALLOCA to avoid MIR inlining ALLOCA bug on ARM64.
            MIR_reg_t names_arr = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
                MIR_T_I64, MIR_new_int_op(mt->ctx, ctor_fc->ctor_prop_count));
            MIR_reg_t lens_arr = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
                MIR_T_I64, MIR_new_int_op(mt->ctx, ctor_fc->ctor_prop_count));
            for (int i = 0; i < ctor_fc->ctor_prop_count; i++) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, i * (int)sizeof(void*), names_arr, 0, 1),
                    MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)ctor_fc->ctor_prop_ptrs[i])));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I32, i * (int)sizeof(int), lens_arr, 0, 1),
                    MIR_new_int_op(mt->ctx, ctor_fc->ctor_prop_lens[i])));
            }
            // Pass ItemNull as callee — class path doesn't need prototype from callee
            MIR_reg_t null_callee = jm_emit_null(mt);
            if (ce->shape_cache_ptr) {
                // §7: Use cached version that captures shape on first call
                obj = jm_call_5(mt, "js_constructor_create_object_shaped_cached", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, null_callee),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, names_arr),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, lens_arr),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, ctor_fc->ctor_prop_count),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)ce->shape_cache_ptr));
                log_debug("§7: new %.*s using shape-cached pre-shaped object with %d props",
                          ctor_len, ctor_name, ctor_fc->ctor_prop_count);
            } else {
                obj = jm_call_4(mt, "js_constructor_create_object_shaped", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, null_callee),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, names_arr),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, lens_arr),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, ctor_fc->ctor_prop_count));
                log_debug("A5-class: new %.*s using pre-shaped object with %d props",
                          ctor_len, ctor_name, ctor_fc->ctor_prop_count);
            }
        } else {
            // Check if this class extends a builtin (Array, etc.)
            bool extends_builtin_array = false;
            {
                // Walk the superclass chain — if any ancestor is a builtin Array, use array
                JsClassEntry* p = ce;
                while (p) {
                    if (!p->superclass && p->node && p->node->superclass &&
                        p->node->superclass->node_type == JS_AST_NODE_IDENTIFIER) {
                        JsIdentifierNode* sid = (JsIdentifierNode*)p->node->superclass;
                        if (sid->name && sid->name->len == 5 &&
                            strncmp(sid->name->chars, "Array", 5) == 0) {
                            extends_builtin_array = true;
                        }
                        break;
                    }
                    p = p->superclass;
                }
            }
            if (extends_builtin_array) {
                obj = jm_call_1(mt, "js_array_new", MIR_T_I64,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
            } else {
                obj = jm_call_0(mt, "js_new_object", MIR_T_I64);
            }
        }

        // v20: Set __proto__ to the class's prototype object (which already has all
        // methods and superclass __proto__ chain from class declaration).
        // This avoids copying methods onto each instance — methods live on the prototype
        // and are accessed via the prototype chain, matching ES spec behavior.
        {
            MIR_reg_t cls_val = jm_transpile_box_item(mt, call->callee);
            MIR_reg_t proto_key = jm_box_string_literal(mt, "prototype", 9);
            MIR_reg_t class_proto = jm_call_2(mt, "js_property_get", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_val),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_key));
            jm_call_void_2(mt, "js_set_prototype",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, class_proto));
        }

        // Emit instance field initializations (before constructor, per JS semantics)
        // Walk inheritance chain base-first, then own fields
        {
            // Bind 'this' to the new object for field initializer expressions
            // (e.g. `y = this.x * 10` must see the partially-constructed instance)
            MIR_reg_t prev_this_fi = jm_call_0(mt, "js_get_this", MIR_T_I64);
            jm_call_void_1(mt, "js_set_this",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj));
            // Also update _js_this if it exists (for field inits inside arrow contexts)
            JsMirVarEntry* js_this_var = jm_find_var(mt, "_js_this");
            MIR_reg_t prev_js_this_val = 0;
            if (js_this_var) {
                prev_js_this_val = jm_new_reg(mt, "prev_jt", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, prev_js_this_val),
                    MIR_new_reg_op(mt->ctx, js_this_var->reg)));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, js_this_var->reg),
                    MIR_new_reg_op(mt->ctx, obj)));
            }
            // Collect chain: chain[0] = most-base, chain[n-1] = immediate parent
            JsClassEntry* field_chain[32];
            int field_chain_len = 0;
            {
                JsClassEntry* p = ce->superclass;
                while (p && field_chain_len < 32) {
                    field_chain[field_chain_len++] = p;
                    p = p->superclass;
                }
            }
            // Emit base-first, then own class. For explicit derived constructors,
            // own public fields run after the first successful super() call.
            // begin/end keep the field-initializer early-error (eval) context on;
            // the brand-check bypass is scoped to each declaration set via
            // js_private_field_define, so initializer expressions stay brand-checked
            // (a private access to a not-yet-installed field throws per spec).
            jm_call_void_0(mt, "js_private_field_init_begin");
            bool defer_private_instance_init = ce->node && ce->node->superclass &&
                ce->constructor && ce->constructor->fc;
            for (int ci = field_chain_len - 1; ci >= 0; ci--) {
                JsClassEntry* fc_ce = field_chain[ci];
                JsClassEntry* saved_current_class = mt->current_class;
                mt->current_class = fc_ce;
                MIR_reg_t fc_cls_val = jm_class_has_private_instance_brands(fc_ce)
                    ? jm_emit_class_object_for_entry(mt, fc_ce) : 0;
                if (!defer_private_instance_init) {
                    jm_emit_private_instance_method_brands(mt, obj, fc_cls_val, fc_ce);
                }
                for (int fi = 0; fi < fc_ce->instance_field_count; fi++) {
                    JsInstanceFieldEntry* inf = &fc_ce->instance_fields[fi];
                    MIR_reg_t key;
                    String* private_field_name = NULL;
                    if (inf->computed && inf->key_expr) {
                        int obj_spill = -1;
                        if (inf->key_module_var_index < 0 && mt->in_generator && jm_has_yield(inf->key_expr)) {
                            obj_spill = jm_gen_spill_save(mt, obj);
                        }
                        if (inf->key_module_var_index >= 0) {
                            key = jm_call_1(mt, "js_get_module_var", MIR_T_I64,
                                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)inf->key_module_var_index));
                        } else {
                            key = jm_transpile_box_item(mt, inf->key_expr);
                        }
                        if (obj_spill >= 0) {
                            jm_gen_spill_load(mt, obj, obj_spill);
                        }
                        if (inf->key_module_var_index < 0) {
                            key = jm_call_1(mt, "js_to_property_key", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                        }
                    } else if (inf->name) {
                        String* field_name = jm_class_private_name(mt, fc_ce, inf->name);
                        if (jm_is_private_name(field_name)) private_field_name = field_name;
                        key = jm_box_string_literal(mt, field_name->chars, (int)field_name->len);
                    } else if (inf->key_expr) {
                        key = jm_transpile_box_item(mt, inf->key_expr);
                    } else {
                        continue;
                    }
                    if (defer_private_instance_init && private_field_name) continue;
                    int obj_val_spill = -1, key_spill = -1;
                    if (mt->in_generator && inf->initializer && jm_has_yield(inf->initializer)) {
                        obj_val_spill = jm_gen_spill_save(mt, obj);
                        key_spill = jm_gen_spill_save(mt, key);
                    }
                    MIR_reg_t val;
                    if (inf->initializer) {
                        val = jm_transpile_box_item(mt, inf->initializer);
                    } else {
                        val = jm_new_reg(mt, "fld_undef", MIR_T_I64);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, val),
                            MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED)));
                    }
                    if (obj_val_spill >= 0) {
                        jm_gen_spill_load(mt, obj, obj_val_spill);
                        jm_gen_spill_load(mt, key, key_spill);
                    }
                    if (private_field_name) {
                        jm_call_3(mt, "js_private_field_define", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                    } else {
                        jm_call_3(mt, "js_create_data_property", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                    }
                    if (private_field_name && fc_cls_val) {
                        jm_emit_private_brand_add(mt, obj, fc_cls_val, private_field_name);
                    }
                }
                mt->current_class = saved_current_class;
            }
            // Own class instance fields
            JsClassEntry* saved_current_class = mt->current_class;
            mt->current_class = ce;
            MIR_reg_t own_cls_val = jm_class_has_private_instance_brands(ce)
                ? jm_transpile_box_item(mt, call->callee) : 0;
            bool defer_own_instance_fields = ce->node && ce->node->superclass;
            if (!defer_private_instance_init && !defer_own_instance_fields) {
                jm_emit_private_instance_method_brands(mt, obj, own_cls_val, ce);
            }
            for (int fi = 0; fi < ce->instance_field_count; fi++) {
                JsInstanceFieldEntry* inf = &ce->instance_fields[fi];
                if (defer_own_instance_fields) continue;
                MIR_reg_t key;
                String* private_field_name = NULL;
                if (inf->computed && inf->key_expr) {
                    int obj_spill = -1;
                    if (inf->key_module_var_index < 0 && mt->in_generator && jm_has_yield(inf->key_expr)) {
                        obj_spill = jm_gen_spill_save(mt, obj);
                    }
                    if (inf->key_module_var_index >= 0) {
                        key = jm_call_1(mt, "js_get_module_var", MIR_T_I64,
                            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)inf->key_module_var_index));
                    } else {
                        key = jm_transpile_box_item(mt, inf->key_expr);
                    }
                    if (obj_spill >= 0) {
                        jm_gen_spill_load(mt, obj, obj_spill);
                    }
                    if (inf->key_module_var_index < 0) {
                        key = jm_call_1(mt, "js_to_property_key", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                    }
                } else if (inf->name) {
                    String* field_name = jm_class_private_name(mt, ce, inf->name);
                    if (jm_is_private_name(field_name)) private_field_name = field_name;
                    key = jm_box_string_literal(mt, field_name->chars, (int)field_name->len);
                } else if (inf->key_expr) {
                    key = jm_transpile_box_item(mt, inf->key_expr);
                } else {
                    continue;
                }
                if (defer_private_instance_init && private_field_name) continue;
                int obj_val_spill = -1, key_spill = -1;
                if (mt->in_generator && inf->initializer && jm_has_yield(inf->initializer)) {
                    obj_val_spill = jm_gen_spill_save(mt, obj);
                    key_spill = jm_gen_spill_save(mt, key);
                }
                MIR_reg_t val;
                if (inf->initializer) {
                    val = jm_transpile_box_item(mt, inf->initializer);
                } else {
                    val = jm_new_reg(mt, "fld_undef", MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, val),
                        MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED)));
                }
                if (obj_val_spill >= 0) {
                    jm_gen_spill_load(mt, obj, obj_val_spill);
                    jm_gen_spill_load(mt, key, key_spill);
                }
                if (private_field_name) {
                    jm_call_3(mt, "js_private_field_define", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                } else {
                    jm_call_3(mt, "js_create_data_property", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                }
                if (private_field_name && own_cls_val) {
                    jm_emit_private_brand_add(mt, obj, own_cls_val, private_field_name);
                }
            }
            mt->current_class = saved_current_class;
            jm_call_void_0(mt, "js_private_field_init_end");
            // Restore 'this' after field initialization
            jm_call_void_1(mt, "js_set_this",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, prev_this_fi));
            if (js_this_var) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, js_this_var->reg),
                    MIR_new_reg_op(mt->ctx, prev_js_this_val)));
            }
        }

        // v20: constructor property is inherited from prototype (prototype.constructor = Class)
        // so we don't set it on the instance directly.

        // Call constructor with this = obj
        // If this class has no explicit constructor, walk up parent chain
        // (JS: implicit constructor(...args) { super(...args); })
        JsClassMethodEntry* active_ctor = NULL;
        if (ce->constructor && ce->constructor->fc && ce->constructor->fc->func_item) {
            active_ctor = ce->constructor;
        } else if (ce->superclass) {
            // No explicit constructor — find inherited constructor
            JsClassEntry* p = ce->superclass;
            while (p && !active_ctor) {
                if (p->constructor && p->constructor->fc && p->constructor->fc->func_item) {
                    active_ctor = p->constructor;
                }
                p = p->superclass;
            }
        }
        if (active_ctor) {
            MIR_reg_t ctor_fn;
            // Js52 P3/P4: when the constructor captures variables from an enclosing
            // scope (e.g. `function factory(arg) { class C { constructor(){ ...arg... } } }`),
            // rebuilding the closure at the `new` call site loses those captures because
            // the captured names are looked up via jm_find_var in the CURRENT scope.
            // The correctly-captured closure was already built at class-definition time
            // and stored on the class object as `__ctor__` — fetch that instead.
            MIR_reg_t cls_for_nt = jm_transpile_box_item(mt, call->callee);
            if (active_ctor->fc->capture_count > 0) {
                MIR_reg_t ctor_key = jm_box_string_literal(mt, "__ctor__", 8);
                ctor_fn = jm_call_2(mt, "js_property_get", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_for_nt),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, ctor_key));
            } else {
                ctor_fn = jm_create_method_function(mt, active_ctor->fc, active_ctor->param_count);
            }
            MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
            // Set pending new.target to the class (picked up by js_call_function)
            jm_emit_set_function_home_class(mt, ctor_fn, cls_for_nt);
            jm_call_void_1(mt, "js_set_new_target",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_for_nt));
            MIR_reg_t ctor_result = jm_call_4(mt, "js_call_function", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, ctor_fn),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
                MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
            // Per ES spec: if constructor returns an object, use that instead
            MIR_reg_t final_obj = jm_call_2(mt, "js_new_check_constructor_return", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, ctor_result));
            if (ce->node && ce->node->superclass && active_ctor == ce->constructor &&
                jm_class_has_private_instance_brands(ce)) {
                jm_call_void_2(mt, "js_init_class_instance_fields",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_for_nt),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, final_obj));
            }
            if (ce->node && ce->node->superclass && active_ctor != ce->constructor) {
                MIR_reg_t prev_this_post = jm_call_0(mt, "js_get_this", MIR_T_I64);
                jm_call_void_1(mt, "js_set_this",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, final_obj));
                jm_emit_own_instance_fields_on_object(mt, ce, final_obj, cls_for_nt, true);
                jm_call_void_1(mt, "js_set_this",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, prev_this_post));
            }
            return final_obj;
        }

        // No explicit constructor: if class extends a builtin (not resolved as a user-defined
        // class), call the builtin constructor with the arguments (implicit super(...args)).
        // The builtin constructor (e.g., Array) creates and returns a new object,
        // so we replace obj with the returned value and set the subclass prototype.
        if (!active_ctor && !ce->superclass && ce->node && ce->node->superclass &&
            ce->node->superclass->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* sid = (JsIdentifierNode*)ce->node->superclass;
            if (sid->name) {
                MIR_reg_t super_ctor = jm_transpile_box_item(mt, ce->node->superclass);
                MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
                MIR_reg_t cls_for_nt = jm_transpile_box_item(mt, call->callee);
                jm_call_void_1(mt, "js_set_new_target",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_for_nt));
                // Call builtin as new: result = new Array(args) etc.
                MIR_reg_t result = jm_call_3(mt, "js_new_from_class_object", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, super_ctor),
                    MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
                // Set __proto__ to the subclass's prototype (e.g., MyArray.prototype)
                MIR_reg_t cls_val = jm_transpile_box_item(mt, call->callee);
                MIR_reg_t proto_key = jm_box_string_literal(mt, "prototype", 9);
                MIR_reg_t class_proto = jm_call_2(mt, "js_property_get", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_val),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_key));
                jm_call_void_2(mt, "js_set_prototype",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, result),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, class_proto));
                // NOTE: do NOT stamp __class_name__ on the instance here. The base
                // [[Construct]] (js_new_from_class_object) already tagged the
                // instance's class on its TypeMap (e.g. JS_CLASS_BOOLEAN); adding a
                // property would transition the shape and drop that tag, breaking
                // brand checks (Boolean/String/Number .valueOf). Subclass identity
                // comes from the prototype chain. (instanceof uses the proto chain.)
                jm_emit_own_instance_fields_on_object(mt, ce, result, cls_val, true);
                return result;
            }
        }
        // v21: Handle member-expression superclass with no explicit constructor
        // e.g. class Foo extends obj.Bar {} → implicit super(...args)
        if (!active_ctor && !ce->superclass && ce->node && ce->node->superclass &&
            ce->node->superclass->node_type != JS_AST_NODE_IDENTIFIER) {
            MIR_reg_t super_ctor = jm_transpile_box_item(mt, ce->node->superclass);
            MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
            MIR_reg_t cls_for_nt = jm_transpile_box_item(mt, call->callee);
            jm_call_void_1(mt, "js_set_new_target",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_for_nt));
            MIR_reg_t result = jm_call_3(mt, "js_new_from_class_object", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, super_ctor),
                MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
                MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
            MIR_reg_t cls_val2 = jm_transpile_box_item(mt, call->callee);
            MIR_reg_t proto_key2 = jm_box_string_literal(mt, "prototype", 9);
            MIR_reg_t class_proto2 = jm_call_2(mt, "js_property_get", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_val2),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_key2));
            jm_call_void_2(mt, "js_set_prototype",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, result),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, class_proto2));
            if (ce->name) {
                MIR_reg_t cn_val = jm_box_string_literal(mt, ce->name->chars, (int)ce->name->len);
                jm_call_void_2(mt, "js_set_internal_class_name",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, result),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, cn_val));
            }
            jm_emit_own_instance_fields_on_object(mt, ce, result, cls_val2, true);
            return result;
        }

        if (ce->node && ce->node->superclass) {
            MIR_reg_t cls_for_fields = jm_transpile_box_item(mt, call->callee);
            jm_emit_own_instance_fields_on_object(mt, ce, obj, cls_for_fields, true);
        }
        return obj;
    }

    if (call->callee->node_type != JS_AST_NODE_IDENTIFIER) {
        MIR_reg_t callee_value = jm_transpile_box_item(mt, call->callee);
        if (new_has_spread) {
            MIR_reg_t args_arr = jm_build_spread_args_array(mt, call->arguments);
            return jm_call_2(mt, "js_apply_constructor", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, callee_value),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args_arr));
        }
        MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
        return jm_call_3(mt, "js_new_from_class_object", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, callee_value),
            MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
            MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
    }

    // Fallback: user-defined constructor function (new FunctionName(args))
    // 1. Create empty object
    // 2. Look up constructor function via scope resolution (same as regular calls)
    // 3. Call constructor with this = object (with prototype chain set up)
    // 4. Return object
    log_info("js-mir: new %.*s — treating as constructor function", ctor_len, ctor_name);

    // Use scope-based resolution (same pattern as jm_transpile_call)
    JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
    NameEntry* entry = js_scope_lookup(mt->tp, id->name);
    if (!entry && id->entry) entry = id->entry;

    // For constructors, always use the actual variable value (jm_transpile_box_item)
    // rather than creating a fresh js_new_function wrapper. This preserves
    // .prototype that was set on the function object (e.g., Foo.prototype = {...}).
    MIR_reg_t callee_value = jm_transpile_box_item(mt, call->callee);
    MIR_reg_t callee = jm_new_reg(mt, "new_callee", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, callee),
        MIR_new_reg_op(mt->ctx, callee_value)));

    // Create object with prototype chain: obj.__proto__ = callee.prototype
    // A5: If the constructor has known this.prop patterns, use pre-shaped object.
    // Look up the function's JsFuncCollected to check for ctor_props.
    JsFuncCollected* ctor_fc = NULL;
    if (entry && entry->node) {
        JsAstNodeType ntype = ((JsAstNode*)entry->node)->node_type;
        JsFunctionNode* fn = NULL;
        if (ntype == JS_AST_NODE_FUNCTION_DECLARATION) {
            fn = (JsFunctionNode*)entry->node;
        } else if (ntype == JS_AST_NODE_VARIABLE_DECLARATOR) {
            JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)entry->node;
            if (decl->init && (decl->init->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
                               decl->init->node_type == JS_AST_NODE_ARROW_FUNCTION))
                fn = (JsFunctionNode*)decl->init;
        }
        if (fn) ctor_fc = jm_find_collected_func(mt, fn);
    }

    if (!ctor_fc || ctor_fc->ctor_prop_count <= 0) {
        MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
        return jm_call_3(mt, "js_new_from_class_object", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, callee),
            MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
            MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
    }

    // the early return above guarantees ctor_fc has shaped properties here,
    // but current clang does not carry that fact through the second guard in
    // this reverted code path.
    MIR_reg_t obj = 0;
    if (ctor_fc && ctor_fc->ctor_prop_count > 0) {
        // Emit static arrays of property name pointers and lengths.
        // Use js_alloc_env instead of MIR_ALLOCA to avoid MIR inlining ALLOCA bug on ARM64.
        MIR_reg_t names_arr = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, ctor_fc->ctor_prop_count));
        MIR_reg_t lens_arr = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, ctor_fc->ctor_prop_count));

        for (int i = 0; i < ctor_fc->ctor_prop_count; i++) {
            // Store pointer to pool-stable property name string
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64, i * (int)sizeof(void*), names_arr, 0, 1),
                MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)ctor_fc->ctor_prop_ptrs[i])));
            // Store length
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I32, i * (int)sizeof(int), lens_arr, 0, 1),
                MIR_new_int_op(mt->ctx, ctor_fc->ctor_prop_lens[i])));
        }

        obj = jm_call_4(mt, "js_constructor_create_object_shaped", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, callee),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, names_arr),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, lens_arr),
            MIR_T_I64, MIR_new_int_op(mt->ctx, ctor_fc->ctor_prop_count));
        log_debug("A5: new %.*s using pre-shaped object with %d props",
                  ctor_len, ctor_name, ctor_fc->ctor_prop_count);
    }

    MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
    // Set pending new.target to the constructor function (picked up by js_call_function)
    jm_call_void_1(mt, "js_set_new_target",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, callee));
    MIR_reg_t ctor_result = jm_call_4(mt, "js_call_function", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, callee),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
        MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
        MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
    // Per ES spec: if constructor returns an object, use that instead
    return jm_call_2(mt, "js_new_check_constructor_return", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, ctor_result));
}

// switch statement
void jm_transpile_switch(JsMirTranspiler* mt, JsSwitchNode* sw) {
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
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_body),
            MIR_new_reg_op(mt->ctx, truthy)));
    }

    jm_emit_label(mt, l_end);
    if (mt->iteration_depth > 0) mt->iteration_depth--;
    if (mt->loop_depth > 0) mt->loop_depth--;
}

static MIR_reg_t jm_emit_await_value_reg(JsMirTranspiler* mt, MIR_reg_t promise_val) {
    if (mt->in_generator && mt->in_async) {
        int next_state = ++mt->gen_yield_index;
        if (next_state > mt->gen_yield_count || next_state >= 64) {
            log_error("js-mir: implicit await index %d exceeds allocated state labels (%d)",
                next_state, mt->gen_yield_count);
            return promise_val;
        }

        MIR_label_t suspend_label = jm_new_label(mt);
        MIR_label_t after_await_label = jm_new_label(mt);

        MIR_reg_t must_suspend = jm_call_1(mt, "js_async_must_suspend", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, promise_val));

        {
            int d = mt->try_ctx_depth - 1;
            while (d >= 0 && mt->try_ctx_stack[d].yield_state_only) d--;
            if (d >= 0 && mt->try_ctx_stack[d].catch_label) {
                MIR_reg_t exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                    MIR_new_label_op(mt->ctx, mt->try_ctx_stack[d].catch_label),
                    MIR_new_reg_op(mt->ctx, exc)));
            }
        }

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
        for (int sd = 1; sd <= mt->scope_depth; sd++) {
            if (!mt->var_scopes[sd]) continue;
            size_t iter = 0; void* item;
            while (hashmap_iter(mt->var_scopes[sd], &iter, &item)) {
                JsVarScopeEntry* e = (JsVarScopeEntry*)item;
                if (e->var.env_slot >= 0 && e->var.from_env) {
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64,
                            e->var.env_slot * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1),
                        MIR_new_reg_op(mt->ctx, e->var.reg)));
                }
            }
        }
        MIR_reg_t await_target = jm_call_0(mt, "js_async_get_resolved", MIR_T_I64);
        MIR_reg_t suspend_result = jm_call_2(mt, "js_gen_yield_result", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, await_target),
            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)next_state));
        jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, suspend_result)));

        jm_emit_label(mt, mt->gen_state_labels[next_state]);
        for (int sd = 1; sd <= mt->scope_depth; sd++) {
            if (!mt->var_scopes[sd]) continue;
            size_t iter = 0; void* item;
            while (hashmap_iter(mt->var_scopes[sd], &iter, &item)) {
                JsVarScopeEntry* e = (JsVarScopeEntry*)item;
                if (e->var.env_slot >= 0 && e->var.from_env) {
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, e->var.reg),
                        MIR_new_mem_op(mt->ctx, MIR_T_I64,
                            e->var.env_slot * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1)));
                }
            }
        }
        for (int td = 0; td < mt->try_ctx_depth; td++) {
            JsTryContext* tc = &mt->try_ctx_stack[td];
            if (tc->has_return_reg) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, tc->has_return_reg),
                    MIR_new_int_op(mt->ctx, 0)));
            }
            if (tc->return_val_reg) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, tc->return_val_reg),
                    MIR_new_int_op(mt->ctx, 0)));
            }
            if (tc->saved_exc_flag_reg) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, tc->saved_exc_flag_reg),
                    MIR_new_int_op(mt->ctx, 0)));
            }
            if (tc->saved_exc_val_reg) {
                MIR_reg_t null_val = jm_emit_null(mt);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, tc->saved_exc_val_reg),
                    MIR_new_reg_op(mt->ctx, null_val)));
            }
        }
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, await_result),
            MIR_new_reg_op(mt->ctx, mt->gen_input_reg)));
        jm_scope_env_reload_vars(mt);
        jm_env_reload_shared_captures(mt);

        {
            int d = mt->try_ctx_depth - 1;
            while (d >= 0 && mt->try_ctx_stack[d].yield_state_only) d--;
            if (d >= 0 && mt->try_ctx_stack[d].catch_label) {
                MIR_reg_t resume_exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                    MIR_new_label_op(mt->ctx, mt->try_ctx_stack[d].catch_label),
                    MIR_new_reg_op(mt->ctx, resume_exc)));
            }
        }

        jm_emit_label(mt, after_await_label);
        return await_result;
    }

    return jm_call_1(mt, "js_await_sync", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, promise_val));
}

// for-of / for-in statement
// Uses fn_len + js_property_access for arrays, or js_object_keys for objects
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
        jm_pop_scope(mt);
        jm_restore_last_closure_snapshot(mt, &saved_last_closure);
        return;
    }

    // Create loop variable (for simple case) or temp var (for destructuring)
    MIR_reg_t loop_var;
    bool is_for_in = (fo->base.node_type == JS_AST_NODE_FOR_IN_STATEMENT);
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
        char vname[128];
        snprintf(vname, sizeof(vname), "_js_%.*s", var_len, var_name);
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
            }
        }
    } else {
        loop_var = jm_new_reg(mt, "_destr_elem", MIR_T_I64);
    }
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, loop_var),
        MIR_new_int_op(mt->ctx, (var_name && is_let_const_loop) ? (int64_t)ITEM_JS_TDZ : ITEM_NULL_VAL)));
    if (var_name && is_let_const_loop) {
        char vname[128];
        snprintf(vname, sizeof(vname), "_js_%.*s", var_len, var_name);
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
            char init_vname[128];
            snprintf(init_vname, sizeof(init_vname), "_js_%.*s", var_len, var_name);
            MIR_reg_t init_val = jm_transpile_box_item(mt, init_expr);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, loop_var),
                MIR_new_reg_op(mt->ctx, init_val)));
            jm_scope_env_mark_and_writeback(mt, init_vname, loop_var);
        }
    }

    // Pre-create destructuring variable registers
    bool left_creates_bindings = left_is_decl || fo->kind == 1 || fo->kind == 2;
    if (destr_pattern && left_creates_bindings) {
        JsAstNode* pe = destr_pattern->elements;
        while (pe) {
            if (pe->node_type == JS_AST_NODE_NULL) {
                // elision: no variable to pre-create
            } else if (pe->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* pid = (JsIdentifierNode*)pe;
                char pvname[128];
                snprintf(pvname, sizeof(pvname), "_js_%.*s", (int)pid->name->len, pid->name->chars);
                // Reuse existing var-scoped variable if present
                JsMirVarEntry* pexist = is_let_const_loop ? NULL : jm_find_var(mt, pvname);
                if (!pexist || !pexist->reg) {
                    MIR_reg_t preg = jm_new_reg(mt, pvname, MIR_T_I64);
                    jm_set_var(mt, pvname, preg);
                    if (is_let_const_loop) {
                        JsMirVarEntry* ve = jm_find_var(mt, pvname);
                        if (ve) {
                            ve->is_let_const = true;
                            ve->tdz_active = true;
                        }
                    }
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, preg),
                        MIR_new_int_op(mt->ctx, is_let_const_loop ? (int64_t)ITEM_JS_TDZ : ITEM_NULL_VAL)));
                }
            } else if (pe->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
                // default value: [x = defaultVal, ...]
                JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)pe;
                if (ap->left && ap->left->node_type == JS_AST_NODE_IDENTIFIER) {
                    JsIdentifierNode* pid = (JsIdentifierNode*)ap->left;
                    char pvname[128];
                    snprintf(pvname, sizeof(pvname), "_js_%.*s", (int)pid->name->len, pid->name->chars);
                    JsMirVarEntry* pexist = is_let_const_loop ? NULL : jm_find_var(mt, pvname);
                    if (!pexist || !pexist->reg) {
                        MIR_reg_t preg = jm_new_reg(mt, pvname, MIR_T_I64);
                        jm_set_var(mt, pvname, preg);
                        if (is_let_const_loop) {
                            JsMirVarEntry* ve = jm_find_var(mt, pvname);
                            if (ve) {
                                ve->is_let_const = true;
                                ve->tdz_active = true;
                            }
                        }
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, preg),
                            MIR_new_int_op(mt->ctx, is_let_const_loop ? (int64_t)ITEM_JS_TDZ : ITEM_NULL_VAL)));
                    }
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
                            char pvname[128];
                            snprintf(pvname, sizeof(pvname), "_js_%.*s", (int)plocal->len, plocal->chars);
                            JsMirVarEntry* pexist = is_let_const_loop ? NULL : jm_find_var(mt, pvname);
                            if (!pexist || !pexist->reg) {
                                MIR_reg_t preg = jm_new_reg(mt, pvname, MIR_T_I64);
                                jm_set_var(mt, pvname, preg);
                                if (is_let_const_loop) {
                                    JsMirVarEntry* ve = jm_find_var(mt, pvname);
                                    if (ve) {
                                        ve->is_let_const = true;
                                        ve->tdz_active = true;
                                    }
                                }
                                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, preg),
                                    MIR_new_int_op(mt->ctx, is_let_const_loop ? (int64_t)ITEM_JS_TDZ : ITEM_NULL_VAL)));
                            }
                        }
                    }
                    pprop = pprop->next;
                }
            } else if (pe->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                JsSpreadElementNode* sp = (JsSpreadElementNode*)pe;
                if (sp->argument && sp->argument->node_type == JS_AST_NODE_IDENTIFIER) {
                    JsIdentifierNode* pid = (JsIdentifierNode*)sp->argument;
                    char pvname[128];
                    snprintf(pvname, sizeof(pvname), "_js_%.*s", (int)pid->name->len, pid->name->chars);
                    JsMirVarEntry* pexist = is_let_const_loop ? NULL : jm_find_var(mt, pvname);
                    if (!pexist || !pexist->reg) {
                        MIR_reg_t preg = jm_new_reg(mt, pvname, MIR_T_I64);
                        jm_set_var(mt, pvname, preg);
                        if (is_let_const_loop) {
                            JsMirVarEntry* ve = jm_find_var(mt, pvname);
                            if (ve) {
                                ve->is_let_const = true;
                                ve->tdz_active = true;
                            }
                        }
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, preg),
                            MIR_new_int_op(mt->ctx, is_let_const_loop ? (int64_t)ITEM_JS_TDZ : ITEM_NULL_VAL)));
                    }
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
                    char pvname[128];
                    snprintf(pvname, sizeof(pvname), "_js_%.*s", (int)local_name->len, local_name->chars);
                    // Reuse existing var-scoped variable if present
                    JsMirVarEntry* pexist = is_let_const_loop ? NULL : jm_find_var(mt, pvname);
                    if (!pexist || !pexist->reg) {
                        MIR_reg_t preg = jm_new_reg(mt, pvname, MIR_T_I64);
                        jm_set_var(mt, pvname, preg);
                        if (is_let_const_loop) {
                            JsMirVarEntry* ve = jm_find_var(mt, pvname);
                            if (ve) {
                                ve->is_let_const = true;
                                ve->tdz_active = true;
                            }
                        }
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, preg),
                            MIR_new_int_op(mt->ctx, is_let_const_loop ? (int64_t)ITEM_JS_TDZ : ITEM_NULL_VAL)));
                    }
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
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, len),
            MIR_new_reg_op(mt->ctx, jm_call_1(mt, "js_array_length", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, collection)))));
        MIR_reg_t cmp = jm_new_reg(mt, "foricmp", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, cmp),
            MIR_new_reg_op(mt->ctx, idx), MIR_new_reg_op(mt->ctx, len)));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
            MIR_new_reg_op(mt->ctx, cmp)));

        MIR_reg_t idx_item = jm_box_int_reg(mt, idx);
        MIR_reg_t elem = jm_call_2(mt, "js_property_access", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, collection),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_item));
        MIR_reg_t live_key = jm_call_2(mt, "js_for_in_key_is_live", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, iterable),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, elem));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_update),
            MIR_new_reg_op(mt->ctx, live_key)));
        if (lhs_call_target) {
            jm_transpile_box_item(mt, fo->left);
            jm_emit_exc_propagate_check(mt);
            MIR_reg_t msg = jm_box_string_literal(mt, "Invalid left-hand side in assignment", 36);
            jm_call_void_2(mt, "js_throw_named_error",
            MIR_T_I64, MIR_new_int_op(mt->ctx, 1),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, msg));
            jm_emit_exc_propagate_check(mt);
        } else {
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, loop_var), MIR_new_reg_op(mt->ctx, elem)));
        }

        if (lhs_ref_node) {
            JsMirReference lhs_ref = jm_emit_reference(mt, lhs_ref_node);
            jm_emit_exc_propagate_check(mt);
            jm_emit_put_value(mt, &lhs_ref, elem);
            jm_emit_exc_propagate_check(mt);
        }

        // Write-back loop variable to module var / global property / scope_env
        if (var_name && !is_let_const_loop) {
            char wb_vname[128];
            snprintf(wb_vname, sizeof(wb_vname), "_js_%.*s", var_len, var_name);
            if (mt->module_consts) {
                JsModuleConstEntry lookup;
                snprintf(lookup.name, sizeof(lookup.name), "%s", wb_vname);
                JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup);
                // If a function-local var shadows the module-level binding,
                // the local MIR_MOV above is the only update needed; do not
                // forward the write to the module var or global object.
                JsMirVarEntry* lv = jm_find_var(mt, wb_vname);
                bool is_function_local = (lv && mt->current_func_index >= 0);
                if (!is_function_local && mc && mc->const_type == MCONST_MODVAR) {
                    jm_call_void_2(mt, "js_set_module_var",
                        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mc->int_val),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, loop_var));
                } else if (!mc && !is_function_local) {
                    // Implicit global only when no function-local binding exists.
                    MIR_reg_t name_reg = jm_box_string_literal(mt, var_name, var_len);
                    jm_call_void_3(mt, "js_set_global_property",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, name_reg),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, loop_var),
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
                }
            }
            jm_scope_env_mark_and_writeback(mt, wb_vname, loop_var);
        }

        if (destr_pattern) {
            bool prev_dstr_assignment = mt->destructure_assignment_mode;
            mt->destructure_assignment_mode = !left_creates_bindings;
            jm_emit_array_destructure(mt, (JsAstNode*)destr_pattern, loop_var);
            mt->destructure_assignment_mode = prev_dstr_assignment;
        }
        if (obj_destr_pattern) {
            bool prev_dstr_assignment = mt->destructure_assignment_mode;
            mt->destructure_assignment_mode = !left_creates_bindings;
            jm_emit_object_destructure(mt, (JsAstNode*)obj_destr_pattern, loop_var);
            mt->destructure_assignment_mode = prev_dstr_assignment;
        }

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
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_test)));

        jm_emit_label(mt, l_end);
        if (mt->iteration_depth > 0) mt->iteration_depth--;
        if (mt->loop_depth > 0) mt->loop_depth--;
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
    if (mt->for_of_depth < 32) {
        mt->for_of_iterators[mt->for_of_depth++] = iterator;
    }

    // Check for exception (e.g. TypeError: null is not iterable)
    MIR_reg_t iter_exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
    MIR_label_t l_iter_err = jm_new_label(mt);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_iter_err),
        MIR_new_reg_op(mt->ctx, iter_exc)));

    // In generators: register iterator and loop_var as env-stored variables
    if (mt->in_generator && mt->gen_env_reg) {
        int iter_slot = mt->gen_local_slot_count++;
        int lv_slot  = mt->gen_local_slot_count++;
        {
            JsVarScopeEntry entry;
            memset(&entry, 0, sizeof(entry));
            snprintf(entry.name, sizeof(entry.name), "_foriter_%d", mt->label_counter);
            entry.var.reg = iterator;
            entry.var.from_env = true;
            entry.var.env_slot = iter_slot;
            entry.var.env_reg = mt->gen_env_reg;
            entry.var.typed_array_type = -1;
            hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
        }
        {
            JsVarScopeEntry entry;
            memset(&entry, 0, sizeof(entry));
            snprintf(entry.name, sizeof(entry.name), "_forlv_%d", mt->label_counter);
            entry.var.reg = loop_var;
            entry.var.from_env = true;
            entry.var.env_slot = lv_slot;
            entry.var.env_reg = mt->gen_env_reg;
            entry.var.typed_array_type = -1;
            hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
        }
        mt->label_counter++;
    }

    MIR_label_t l_test = jm_new_label(mt);
    MIR_label_t l_update = jm_new_label(mt);
    MIR_label_t l_break = jm_new_label(mt);  // v29: break target calls iterator close
    MIR_label_t l_end = jm_new_label(mt);

    // v29: Use l_break as the break target so IteratorClose is called
    jm_push_loop_labels(mt, l_update, l_break);
    mt->iteration_depth++;
    if (mt->loop_depth > 0) {
        mt->loop_stack[mt->loop_depth - 1].iterator_to_close = iterator;
    }

    // Pre-initialize delayed-return registers BEFORE the loop test label,
    // so they are valid even when the iterable is empty and the loop body
    // never executes (the BT l_end branch skips the body entirely).
    MIR_label_t l_iter_exc = jm_new_label(mt);
    bool pushed_try = false;
    MIR_reg_t forit_return_val = 0;
    MIR_reg_t forit_has_return = 0;
    MIR_label_t l_forit_ret = jm_new_label(mt);
    if (mt->try_ctx_depth < 16) {
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
                snprintf(entry.name, sizeof(entry.name), "_forit_ret_%d", mt->label_counter);
                entry.var.reg = forit_return_val;
                entry.var.from_env = true;
                entry.var.env_slot = ret_slot;
                entry.var.env_reg = mt->gen_env_reg;
                entry.var.typed_array_type = -1;
                hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
            }
            {
                JsVarScopeEntry entry;
                memset(&entry, 0, sizeof(entry));
                snprintf(entry.name, sizeof(entry.name), "_forit_hret_%d", mt->label_counter);
                entry.var.reg = forit_has_return;
                entry.var.from_env = true;
                entry.var.env_slot = hret_slot;
                entry.var.env_reg = mt->gen_env_reg;
                entry.var.typed_array_type = -1;
                hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
            }
            mt->label_counter++;
        }
        JsTryContext* tc = &mt->try_ctx_stack[mt->try_ctx_depth++];
        tc->catch_label = l_iter_exc;
        tc->finally_label = 0;
        tc->end_label = l_forit_ret;
        tc->return_val_reg = forit_return_val;
        tc->has_return_reg = forit_has_return;
        tc->has_catch = true;
        tc->has_finally = false;
        tc->inlining_finally = false;
        tc->yield_state_only = false;
        tc->finally_body = NULL;
        tc->saved_exc_flag_reg = 0;
        tc->saved_exc_val_reg = 0;
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
        MIR_reg_t step_call_exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_iter_err),
            MIR_new_reg_op(mt->ctx, step_call_exc)));
        step_iter_result = jm_emit_await_value_reg(mt, step_iter_result);
        MIR_reg_t step_await_exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_iter_exc),
            MIR_new_reg_op(mt->ctx, step_await_exc)));
    } else {
        step_result = jm_emit_iterator_step(mt, iterator);
    }
    MIR_reg_t step_exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_iter_err),
        MIR_new_reg_op(mt->ctx, step_exc)));
    // Check if done (JS_ITER_DONE_SENTINEL — unique sentinel that won't collide with null/undefined/false)
    MIR_reg_t is_done = is_for_await
        ? jm_call_1(mt, "js_iterator_result_done", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, step_iter_result))
        : jm_emit_iterator_done_test(mt, step_result, "forofdone");
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_end),
        MIR_new_reg_op(mt->ctx, is_done)));
    if (is_for_await) {
        step_result = jm_call_1(mt, "js_iterator_result_value", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, step_iter_result));
        step_result = jm_emit_await_value_reg(mt, step_result);
        MIR_reg_t await_exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_iter_exc),
            MIR_new_reg_op(mt->ctx, await_exc)));
    }

    // Assign current value to loop variable
    if (lhs_call_target) {
        jm_transpile_box_item(mt, fo->left);
        MIR_reg_t lhs_call_exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_iter_exc),
            MIR_new_reg_op(mt->ctx, lhs_call_exc)));
        MIR_reg_t msg = jm_box_string_literal(mt, "Invalid left-hand side in assignment", 36);
        jm_call_void_2(mt, "js_throw_named_error",
            MIR_T_I64, MIR_new_int_op(mt->ctx, 1),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, msg));
        MIR_reg_t lhs_ref_exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_iter_exc),
            MIR_new_reg_op(mt->ctx, lhs_ref_exc)));
    } else {
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, loop_var), MIR_new_reg_op(mt->ctx, step_result)));
    }

    // Write-back loop variable to module var / global property / scope_env
    if (var_name && !is_let_const_loop) {
        char wb_vname[128];
        snprintf(wb_vname, sizeof(wb_vname), "_js_%.*s", var_len, var_name);
        // Module variable writeback
        if (mt->module_consts) {
            JsModuleConstEntry lookup;
            snprintf(lookup.name, sizeof(lookup.name), "%s", wb_vname);
            JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup);
            // Skip module var / global writeback if a function-local shadows.
            JsMirVarEntry* lv = jm_find_var(mt, wb_vname);
            bool is_function_local = (lv && mt->current_func_index >= 0);
            if (!is_function_local && mc && mc->const_type == MCONST_MODVAR) {
                jm_call_void_2(mt, "js_set_module_var",
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mc->int_val),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, loop_var));
            } else if (!mc && !is_function_local) {
                MIR_reg_t name_reg = jm_box_string_literal(mt, var_name, var_len);
                jm_call_void_3(mt, "js_set_global_property",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, name_reg),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, loop_var),
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
            }
        }
        jm_scope_env_mark_and_writeback(mt, wb_vname, loop_var);
    }

    // Destructure element into individual variables if array pattern
    if (destr_pattern) {
        bool prev_dstr_assignment = mt->destructure_assignment_mode;
        mt->destructure_assignment_mode = !left_creates_bindings;
        jm_emit_array_destructure(mt, (JsAstNode*)destr_pattern, loop_var);
        mt->destructure_assignment_mode = prev_dstr_assignment;
        // Check for exception from destructuring (e.g. non-iterable value for empty array pattern).
        // If exception is pending, close the for-of iterator and rethrow (l_iter_exc handler).
        MIR_reg_t arr_destr_exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_iter_exc),
            MIR_new_reg_op(mt->ctx, arr_destr_exc)));
    }

    // Destructure element into individual variables if object pattern
    if (obj_destr_pattern) {
        bool prev_dstr_assignment = mt->destructure_assignment_mode;
        mt->destructure_assignment_mode = !left_creates_bindings;
        jm_emit_object_destructure(mt, (JsAstNode*)obj_destr_pattern, loop_var);
        mt->destructure_assignment_mode = prev_dstr_assignment;
        // Check for exception from destructuring
        MIR_reg_t obj_destr_exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_iter_exc),
            MIR_new_reg_op(mt->ctx, obj_destr_exc)));
    }

    if (lhs_ref_node) {
        JsMirReference lhs_ref = jm_emit_reference(mt, lhs_ref_node);
        jm_emit_put_value(mt, &lhs_ref, loop_var);
        MIR_reg_t lhs_put_exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_iter_exc),
            MIR_new_reg_op(mt->ctx, lhs_put_exc)));
    }

    // P4b-of: Infer class type for for-of loop variable from field accesses in body.
    // Pattern: `for (const x of arr) { x.field1 ...; x.field2 ...; }` →
    // if field1, field2 match exactly one class, tag x with that class_entry.
    // Enables P1 native reads and P2 native writes inside the loop body.
    if (var_name && var_len > 0 && fo->body) {
        char p4b_vname[132];
        snprintf(p4b_vname, sizeof(p4b_vname), "_js_%.*s", var_len, var_name);
        JsMirVarEntry* fo_var = jm_find_var(mt, p4b_vname);
        if (fo_var && !fo_var->class_entry) {
            char p4b_fields[16][64];
            int p4b_count = 0;
            jm_collect_var_fields_walk(fo->body, var_name, var_len,
                                       p4b_fields, &p4b_count, 16);
            if (p4b_count >= 2) {
                JsClassEntry* p4b_ce = jm_match_class_from_fields(mt, p4b_fields, p4b_count);
                if (p4b_ce) {
                    fo_var->class_entry = p4b_ce;
                    log_debug("P4b-of: for-of var '%.*s' inferred as '%.*s' from %d field accesses",
                              var_len, var_name,
                              (int)(p4b_ce->name ? p4b_ce->name->len : 0),
                              p4b_ce->name ? p4b_ce->name->chars : "<anon>",
                              p4b_count);
                }
            }
        }
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

    // Pop synthetic try context
    if (pushed_try) mt->try_ctx_depth--;

    // Update: jump back to test (no index to increment — iterator handles state)
    jm_emit_label(mt, l_update);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_test)));

    // v29: Break target — call IteratorClose before exiting
    jm_emit_label(mt, l_break);
    jm_emit_iterator_close(mt, iterator);
    // fall through to l_end

    jm_emit_label(mt, l_end);
    jm_emit_label(mt, l_iter_err);  // exception from js_get_iterator jumps here
    jm_emit_exc_propagate_check(mt);

    // IteratorClose on exception from body — call return() then re-throw
    {
        MIR_label_t l_iter_exc_done = jm_new_label(mt);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_iter_exc_done)));
        jm_emit_label(mt, l_iter_exc);
        // Save exception, close iterator, restore exception and re-throw
        MIR_reg_t saved_exc = jm_call_0(mt, "js_clear_exception", MIR_T_I64);
        jm_emit_iterator_close(mt, iterator);
        jm_call_0(mt, "js_clear_exception", MIR_T_I64);  // clear any exc from iterator close
        jm_call_void_1(mt, "js_throw_value",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, saved_exc));
        // Propagate to outer handler
        jm_emit_exc_propagate_check(mt);
        jm_emit_label(mt, l_iter_exc_done);
    }

    // Handle delayed return from inside for-of body.
    // jm_transpile_return stores val in forit_return_val, sets forit_has_return=1,
    // and jumps to l_forit_ret. Here we close the iterator and do the actual return.
    if (pushed_try) {
        jm_emit_label(mt, l_forit_ret);
        MIR_label_t l_no_delayed_ret = jm_new_label(mt);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
            MIR_new_label_op(mt->ctx, l_no_delayed_ret),
            MIR_new_reg_op(mt->ctx, forit_has_return)));
        // Propagate return to outer try context if any
        if (mt->try_ctx_depth > 0) {
            JsTryContext* outer = &mt->try_ctx_stack[mt->try_ctx_depth - 1];
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, outer->return_val_reg),
                MIR_new_reg_op(mt->ctx, forit_return_val)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, outer->has_return_reg),
                MIR_new_int_op(mt->ctx, 1)));
            MIR_label_t target = outer->has_finally ? outer->finally_label : outer->end_label;
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, target)));
        } else {
            // No outer try — emit actual return
            jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
                MIR_new_reg_op(mt->ctx, forit_return_val)));
        }
        jm_emit_label(mt, l_no_delayed_ret);
    }

    if (mt->for_of_depth > 0) mt->for_of_depth--;
    if (mt->iteration_depth > 0) mt->iteration_depth--;
    if (mt->loop_depth > 0) mt->loop_depth--;
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

    for (int i = mt->loop_depth - 1; i >= 0; i--) {
        if (mt->loop_stack[i].iterator_to_close) {
            jm_emit_iterator_close(mt, mt->loop_stack[i].iterator_to_close);
        }
    }

    // v15: In generator/async state machines, return [value, -1] to signal done.
    // If the return is inside a try/finally, delay it so the finally body runs
    // and can override the completion.
    if (mt->in_generator) {
        int return_d = mt->try_ctx_depth - 1;
        while (return_d >= 0 && mt->try_ctx_stack[return_d].yield_state_only) return_d--;
        if (return_d >= 0 && mt->try_ctx_stack[return_d].has_finally) {
            JsTryContext* tc = &mt->try_ctx_stack[return_d];
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, tc->return_val_reg),
                MIR_new_reg_op(mt->ctx, val)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, tc->has_return_reg),
                MIR_new_int_op(mt->ctx, 1)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, tc->finally_label)));
            return;
        }
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
    if (mt->try_ctx_depth > 0) {
        JsTryContext* tc = &mt->try_ctx_stack[mt->try_ctx_depth - 1];
        // Store the return value and set the return flag
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, tc->return_val_reg),
            MIR_new_reg_op(mt->ctx, val)));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, tc->has_return_reg),
            MIR_new_int_op(mt->ctx, 1)));
        // Jump to finally (or end if no finally)
        MIR_label_t target = tc->has_finally ? tc->finally_label : tc->end_label;
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, target)));
        return;
    }

    jm_emit_eval_local_pop_if_needed(mt);
    jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, val)));
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
            // Check Annex B skip condition: parameter name collision
            JsFunctionNode* enclosing_fn = mt->current_fc ? mt->current_fc->node : NULL;
            bool effective_strict = mt->is_global_strict || mt->is_module ||
                (mt->current_fc && mt->current_fc->is_strict) ||
                (enclosing_fn && jm_has_use_strict_directive(enclosing_fn));
            if (effective_strict && !jm_statement_function_decl_is_direct_binding(fn_decl)) {
                break;
            }
            if (enclosing_fn && jm_func_has_param_named(enclosing_fn,
                    fn_decl->name->chars, (int)fn_decl->name->len)) {
                break;
            }
            char fn_vname[128];
            snprintf(fn_vname, sizeof(fn_vname), "_js_%.*s",
                (int)fn_decl->name->len, fn_decl->name->chars);
            if (mt->current_fc && mt->current_fc->uses_arguments &&
                strcmp(fn_vname, "_js_arguments") == 0 &&
                !jm_statement_function_decl_is_direct_binding(fn_decl)) {
                break;
            }
            JsMirVarEntry* existing = jm_find_var(mt, fn_vname);
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
                snprintf(mclookup.name, sizeof(mclookup.name), "%s", fn_vname);
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
                        MIR_reg_t key_reg = jm_box_string_literal(mt,
                            fn_decl->name->chars, (int)fn_decl->name->len);
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
                            jm_call_void_3(mt, "js_set_global_property",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_reg),
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, set_done)));
                            jm_emit_label(mt, local_set);
                            jm_call_void_2(mt, "js_eval_local_export_var",
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_reg));
                            jm_emit_label(mt, set_done);
                        } else {
                            jm_call_void_3(mt, "js_set_global_property",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_reg),
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
                        }
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
                    snprintf(mvlookup.name, sizeof(mvlookup.name), "%s", fn_vname);
                    JsModuleConstEntry* mvc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mvlookup);
                        if (mvc && mvc->const_type == MCONST_MODVAR && mvc->var_kind == 0 &&
                            !mvc->annexb_suppressed && mvc != annexb_modvar) {
                        jm_call_void_2(mt, "js_set_module_var",
                            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mvc->int_val),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_reg));
                        if (mvc->is_nested_func_hoist && !mvc->is_iife_var) {
                            MIR_reg_t key_reg = jm_box_string_literal(mt,
                                fn_decl->name->chars, (int)fn_decl->name->len);
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
                                jm_call_void_3(mt, "js_set_global_property",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_reg),
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
                                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, set_done)));
                                jm_emit_label(mt, local_set);
                                jm_call_void_2(mt, "js_eval_local_export_var",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_reg));
                                jm_emit_label(mt, set_done);
                            } else {
                                jm_call_void_3(mt, "js_set_global_property",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_reg),
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
                            }
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
                    if (ce->has_self_extends) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "Cannot access '%.*s' before initialization",
                            (int)cls_node->name->len, cls_node->name->chars);
                        MIR_reg_t msg_reg = jm_box_string_literal(mt, msg, (int)strlen(msg));
                        jm_call_void_2(mt, "js_throw_named_error",
            MIR_T_I64, MIR_new_int_op(mt->ctx, 1),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, msg_reg));
                        jm_emit_exc_propagate_check(mt);
                    }
                    MIR_reg_t cls_obj = jm_call_0(mt, "js_new_object", MIR_T_I64);
                    jm_emit_set_private_class_index(mt, cls_obj, ce);
                    jm_emit_set_class_source(mt, cls_obj, cls_node);
                    // Store class object in module var
                    char vname[128];
                    snprintf(vname, sizeof(vname), "_js_%.*s", (int)cls_node->name->len, cls_node->name->chars);
                    JsMirVarEntry* local_class_binding = jm_find_var(mt, vname);
                    bool stored_local_class_binding = false;
                    if (local_class_binding && local_class_binding->is_let_const) {
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, local_class_binding->reg),
                            MIR_new_reg_op(mt->ctx, cls_obj)));
                        local_class_binding->tdz_active = false;
                        local_class_binding->type_id = LMD_TYPE_ANY;
                        local_class_binding->mir_type = MIR_T_I64;
                        jm_scope_env_mark_and_writeback(mt, vname, local_class_binding->reg);
                        stored_local_class_binding = true;
                    }
                    JsModuleConstEntry mclookup;
                    snprintf(mclookup.name, sizeof(mclookup.name), "%s", vname);
                    JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                    if (!stored_local_class_binding && mc && mc->const_type == MCONST_CLASS) {
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
                        MIR_reg_t class_key = jm_box_string_literal(mt, cls_node->name->chars, (int)cls_node->name->len);
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
                        MIR_reg_t class_key = jm_box_string_literal(mt, cls_node->name->chars, (int)cls_node->name->len);
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
                    int ctor_len = 0;
                    if (ce->constructor && ce->constructor->fc)
                        ctor_len = ce->constructor->param_count;
                    MIR_reg_t len_key = jm_box_string_literal(mt, "length", 6);
                    MIR_reg_t len_val = jm_new_reg(mt, "cls_len", MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, len_val),
                        MIR_new_int_op(mt->ctx, (int64_t)i2it(ctor_len))));
                    jm_call_3(mt, "js_property_set", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, len_key),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, len_val));
                    jm_call_void_2(mt, "js_mark_non_writable",
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, len_key));
                    jm_call_void_2(mt, "js_mark_non_enumerable",
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, len_key));
                    if (ce->name) {
                        MIR_reg_t name_val = jm_box_string_literal(mt, ce->name->chars, (int)ce->name->len);
                        jm_call_void_2(mt, "js_set_class_name",
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, name_val));
                    }
                    MIR_reg_t ctor_super_val = 0;
                    MIR_reg_t class_proto_obj = jm_call_0(mt, "js_new_object", MIR_T_I64);
                    MIR_reg_t early_pt_key = jm_box_string_literal(mt, "prototype", 9);
                    jm_call_3(mt, "js_property_set", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, early_pt_key),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, class_proto_obj));
                    jm_call_void_2(mt, "js_mark_non_writable",
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, early_pt_key));
                    jm_call_void_2(mt, "js_mark_non_enumerable",
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, early_pt_key));
                    jm_call_void_2(mt, "js_mark_non_configurable",
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, early_pt_key));
                    // Inherit static methods from parent classes (base-first, then own overrides)
                    {
                        JsClassEntry* s_chain[32];
                        int s_chain_len = 0;
                        {
                            JsClassEntry* p = ce->superclass;
                            while (p && s_chain_len < 32) {
                                s_chain[s_chain_len++] = p;
                                p = p->superclass;
                            }
                        }
                        for (int ci = s_chain_len - 1; ci >= 0; ci--) {
                            JsClassEntry* parent = s_chain[ci];
                            for (int mi = 0; mi < parent->method_count; mi++) {
                                JsClassMethodEntry* me = &parent->methods[mi];
                                if (!me->is_static || me->is_constructor) continue;
                                if (me->name && jm_is_private_name(me->name)) continue;
                                if (!me->fc || !me->fc->func_item) continue;
                                if (!me->name && !(me->computed && me->key_expr)) continue;
                                MIR_reg_t fn_item;
                                if (me->fc->capture_count > 0) {
                                    fn_item = jm_build_closure_for_method(mt, me->fc, me->param_count);
                                } else {
                                    fn_item = jm_call_2(mt, "js_new_method_function", MIR_T_I64,
                                        MIR_T_I64, MIR_new_ref_op(mt->ctx, me->fc->func_item),
                                        MIR_T_I64, MIR_new_int_op(mt->ctx, me->param_count));
                                }
                                if (me->name && !me->computed) {
                                    char fname[256];
                                    if (me->is_getter) snprintf(fname, sizeof(fname), "get %.*s", (int)me->name->len, me->name->chars);
                                    else if (me->is_setter) snprintf(fname, sizeof(fname), "set %.*s", (int)me->name->len, me->name->chars);
                                    else snprintf(fname, sizeof(fname), "%.*s", (int)me->name->len, me->name->chars);
                                    jm_emit_set_function_name(mt, fn_item, fname, me->fc ? me->fc->formal_length : -1);
                                }
                                if (me->fc) jm_emit_set_function_source(mt, fn_item, me->fc->node);
                                jm_call_void_1(mt, "js_mark_method_func", MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_item));
                                MIR_reg_t parent_cls_obj = jm_emit_class_object_for_entry(mt, parent);
                                if (!parent_cls_obj) parent_cls_obj = cls_obj;
                                jm_emit_set_function_home_class(mt, fn_item, parent_cls_obj);
                                MIR_reg_t mk;
                                if (me->computed && me->key_expr) {
                                    mk = jm_transpile_box_item(mt, me->key_expr);
                                    // Phase-5C: no longer wrap key with __get_/__set_.
                                } else if (me->is_getter || me->is_setter) {
                                    mk = jm_box_string_literal(mt, me->name->chars, (int)me->name->len);
                                } else {
                                    mk = jm_box_string_literal(mt, me->name->chars, (int)me->name->len);
                                }
                                jm_emit_install_method_or_accessor(mt, cls_obj, mk, fn_item,
                                    me->is_getter, me->is_setter);
                            }
                        }
                    }
                    // Register own static methods as properties on the class object (overrides parents)
                    for (int mi = 0; mi < ce->method_count; mi++) {
                        JsClassMethodEntry* me = &ce->methods[mi];
                        if (!me->is_static || me->is_constructor) continue;
                        if (!me->fc || !me->fc->func_item) continue;
                        MIR_reg_t mk = 0;
                        if (me->computed && me->key_expr) {
                            int proto_spill = -1, cls_spill = -1;
                            if (mt->in_generator && jm_has_yield(me->key_expr)) {
                                proto_spill = jm_gen_spill_save(mt, class_proto_obj);
                                cls_spill = jm_gen_spill_save(mt, cls_obj);
                            }
                            mk = jm_transpile_box_item(mt, me->key_expr);
                            if (cls_spill >= 0) {
                                jm_gen_spill_load(mt, class_proto_obj, proto_spill);
                                jm_gen_spill_load(mt, cls_obj, cls_spill);
                            }
                        }
                        MIR_reg_t fn_item;
                        if (me->fc->capture_count > 0) {
                            fn_item = jm_build_closure_for_method(mt, me->fc, me->param_count);
                        } else {
                            fn_item = jm_call_2(mt, "js_new_method_function", MIR_T_I64,
                                MIR_T_I64, MIR_new_ref_op(mt->ctx, me->fc->func_item),
                                MIR_T_I64, MIR_new_int_op(mt->ctx, me->param_count));
                        }
                        if (me->name && !me->computed) {
                            char fname[256];
                            if (me->is_getter) snprintf(fname, sizeof(fname), "get %.*s", (int)me->name->len, me->name->chars);
                            else if (me->is_setter) snprintf(fname, sizeof(fname), "set %.*s", (int)me->name->len, me->name->chars);
                            else snprintf(fname, sizeof(fname), "%.*s", (int)me->name->len, me->name->chars);
                            jm_emit_set_function_name(mt, fn_item, fname, me->fc ? me->fc->formal_length : -1);
                        }
                        if (me->fc) jm_emit_set_function_source(mt, fn_item, me->fc->node);
                        jm_call_void_1(mt, "js_mark_method_func", MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_item));
                        jm_emit_set_function_home_class(mt, fn_item, cls_obj);
                        if (!mk && me->name) {
                            mk = jm_box_string_literal(mt, me->name->chars, (int)me->name->len);
                        }
                        if (!mk) {
                            continue;
                        }
                        jm_emit_install_method_or_accessor(mt, cls_obj, mk, fn_item,
                            me->is_getter, me->is_setter);
                        if (me->name && jm_is_private_name(me->name) &&
                            !jm_private_static_method_brand_seen(ce, mi)) {
                            jm_call_void_3(mt, "js_private_brand_add",
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, mk),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
                        }
                    }
                    // Store __ctor__ on class object
                    {
                        JsClassMethodEntry* active_ctor = NULL;
                        if (ce->constructor && ce->constructor->fc && ce->constructor->fc->func_item) {
                            active_ctor = ce->constructor;
                        } else if (ce->superclass) {
                            JsClassEntry* p = ce->superclass;
                            while (p && !active_ctor) {
                                if (p->constructor && p->constructor->fc && p->constructor->fc->func_item) {
                                    active_ctor = p->constructor;
                                }
                                p = p->superclass;
                            }
                        }
                        if (active_ctor) {
                            MIR_reg_t ctor_fn;
                            if (active_ctor->fc->capture_count > 0) {
                                ctor_fn = jm_build_closure_for_method(mt, active_ctor->fc, active_ctor->param_count);
                            } else {
                                ctor_fn = jm_create_method_function(mt, active_ctor->fc, active_ctor->param_count);
                            }
                            jm_emit_set_function_home_class(mt, ctor_fn, cls_obj);
                            MIR_reg_t ctor_key = jm_box_string_literal(mt, "__ctor__", 8);
                            jm_call_3(mt, "js_property_set", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, ctor_key),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, ctor_fn));
                        }
                        jm_emit_class_ctor_shape_metadata(mt, cls_obj, ce);
                    }
                    // Create __instance_proto__ with instance methods and store as prototype
                    {
                        MIR_reg_t proto_obj = class_proto_obj;
                        jm_call_void_2(mt, "js_set_default_constructor_property",
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_obj),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
                        {
                            JsClassEntry* sc = ce->superclass;
                            MIR_reg_t last_proto = proto_obj;
                            if (sc) {
                                JsIdentifierNode tmp_id2;
                                memset(&tmp_id2, 0, sizeof(tmp_id2));
                                tmp_id2.base.node_type = JS_AST_NODE_IDENTIFIER;
                                tmp_id2.name = sc->name;
                                MIR_reg_t super_val = jm_transpile_box_item(mt, (JsAstNode*)&tmp_id2);
                                MIR_reg_t sp_key = jm_box_string_literal(mt, "prototype", 9);
                                MIR_reg_t sp_obj = jm_call_2(mt, "js_property_get", MIR_T_I64,
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, super_val),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_key));
                                jm_call_void_1(mt, "js_check_class_prototype_parent",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_obj));
                                jm_emit_exc_propagate_check(mt);
                                jm_call_void_2(mt, "js_set_prototype",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, last_proto),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_obj));
                                ctor_super_val = super_val;
                                MIR_reg_t super_ctor_key = jm_box_string_literal(mt, "__super_ctor__", 14);
                                jm_call_3(mt, "js_property_set", MIR_T_I64,
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, super_ctor_key),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, super_val));
                            }
                            JsAstNode* heritage = cls_node->superclass ? cls_node->superclass :
                                ((ce->node && ce->node->superclass) ? ce->node->superclass : NULL);
                            bool heritage_is_null = heritage && (heritage->node_type == JS_AST_NODE_NULL ||
                                (heritage->node_type == JS_AST_NODE_LITERAL &&
                                 ((JsLiteralNode*)heritage)->literal_type == JS_LITERAL_NULL));
                            if (!heritage_is_null && heritage && heritage->node_type == JS_AST_NODE_IDENTIFIER) {
                                JsIdentifierNode* heritage_id = (JsIdentifierNode*)heritage;
                                heritage_is_null = heritage_id->name && heritage_id->name->len == 4 &&
                                    strncmp(heritage_id->name->chars, "null", 4) == 0;
                            }
                            if (!sc && heritage && !heritage_is_null) {
                                MIR_reg_t super_val = jm_transpile_box_item(mt, heritage);
                                jm_call_void_1(mt, "js_check_class_heritage_constructor",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, super_val));
                                jm_emit_exc_propagate_check(mt);
                                MIR_reg_t sp_key = jm_box_string_literal(mt, "prototype", 9);
                                MIR_reg_t sp_obj = jm_call_2(mt, "js_property_get", MIR_T_I64,
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, super_val),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_key));
                                jm_call_void_1(mt, "js_check_class_prototype_parent",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_obj));
                                jm_emit_exc_propagate_check(mt);
                                jm_call_void_2(mt, "js_set_prototype",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, last_proto),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_obj));
                                ctor_super_val = super_val;
                                MIR_reg_t super_ctor_key = jm_box_string_literal(mt, "__super_ctor__", 14);
                                jm_call_3(mt, "js_property_set", MIR_T_I64,
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, super_ctor_key),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, super_val));
                            }
                            if (heritage_is_null) {
                                MIR_reg_t null_proto = jm_emit_null(mt);
                                jm_call_void_2(mt, "js_set_prototype",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, last_proto),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, null_proto));
                            }
                        }
                        // Own instance methods
                        for (int mi = 0; mi < ce->method_count; mi++) {
                            JsClassMethodEntry* me = &ce->methods[mi];
                            if (me->is_constructor || me->is_static) continue;
                            if (!me->fc || !me->fc->func_item) continue;
                            if (!me->name && !(me->computed && me->key_expr)) continue;
                            MIR_reg_t fn_item = me->fc->capture_count > 0
                                ? jm_build_closure_for_method(mt, me->fc, me->param_count)
                                : jm_call_2(mt, "js_new_method_function", MIR_T_I64,
                                    MIR_T_I64, MIR_new_ref_op(mt->ctx, me->fc->func_item),
                                    MIR_T_I64, MIR_new_int_op(mt->ctx, me->param_count));
                            if (me->name && !me->computed) {
                                char fname[256];
                                if (me->is_getter) snprintf(fname, sizeof(fname), "get %.*s", (int)me->name->len, me->name->chars);
                                else if (me->is_setter) snprintf(fname, sizeof(fname), "set %.*s", (int)me->name->len, me->name->chars);
                                else snprintf(fname, sizeof(fname), "%.*s", (int)me->name->len, me->name->chars);
                                jm_emit_set_function_name(mt, fn_item, fname, me->fc ? me->fc->formal_length : -1);
                            }
                            if (me->fc) jm_emit_set_function_source(mt, fn_item, me->fc->node);
                            jm_call_void_1(mt, "js_mark_method_func", MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_item));
                            jm_emit_set_function_home_class(mt, fn_item, cls_obj);
                            MIR_reg_t mk = 0;
                            if (me->computed && me->key_expr) {
                                // generator spill: save proto_obj, cls_obj, and fn_item before yield-containing key expr
                                int proto_spill = -1, cls_spill = -1, fn_spill = -1;
                                if (mt->in_generator && jm_has_yield(me->key_expr)) {
                                    proto_spill = jm_gen_spill_save(mt, proto_obj);
                                    cls_spill = jm_gen_spill_save(mt, cls_obj);
                                    fn_spill = jm_gen_spill_save(mt, fn_item);
                                }
                                mk = jm_transpile_box_item(mt, me->key_expr);
                                if (proto_spill >= 0) {
                                    jm_gen_spill_load(mt, proto_obj, proto_spill);
                                    jm_gen_spill_load(mt, cls_obj, cls_spill);
                                    jm_gen_spill_load(mt, fn_item, fn_spill);
                                }
                                // Phase-5C: no key wrap.
                            }
                            else if (me->name) {
                                String* method_name = jm_class_private_name(mt, ce, me->name);
                                mk = jm_box_string_literal(mt, method_name->chars, (int)method_name->len);
                            }
                            if (!mk) continue;
                            jm_emit_install_method_or_accessor(mt, proto_obj, mk, fn_item,
                                me->is_getter, me->is_setter);
                        }
                        MIR_reg_t ip_key = jm_box_string_literal(mt, "__instance_proto__", 18);
                        jm_call_3(mt, "js_property_set", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, ip_key),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_obj));
                        jm_call_void_2(mt, "js_set_default_constructor_property",
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_obj),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
                        jm_call_void_1(mt, "js_mark_all_non_enumerable",
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_obj));
                    }
                    // Mark all static methods on class object as non-enumerable (ES spec)
                    jm_call_void_1(mt, "js_mark_all_non_enumerable",
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));

                    if (ctor_super_val) {
                        MIR_reg_t super_key = jm_box_string_literal(mt, "__super_class__", 15);
                        jm_call_3(mt, "js_property_set", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, super_key),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, ctor_super_val));
                    }

                    jm_emit_class_instance_field_metadata_for_decl(mt, cls_obj, ce);

                    if (ce->node && ce->node->body && ce->node->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
                        JsBlockNode* body = (JsBlockNode*)ce->node->body;
                        int static_field_index = 0;
                        int instance_field_index = 0;
                        for (JsAstNode* elem = body->statements; elem; elem = elem->next) {
                            if (elem->node_type != JS_AST_NODE_FIELD_DEFINITION) continue;
                            JsFieldDefinitionNode* fd = (JsFieldDefinitionNode*)elem;
                            if (fd->is_static) {
                                if (static_field_index >= ce->static_field_count) continue;
                                JsStaticFieldEntry* sf = &ce->static_fields[static_field_index++];
                                if (sf->computed && sf->key_expr && sf->key_module_var_index >= 0) {
                                    // computed class field keys run while defining the class.
                                    // A `yield` in the key suspends the generator before static
                                    // field installation, so spill the class object register.
                                    int cls_key_spill = -1;
                                    if (mt->in_generator && jm_has_yield(sf->key_expr)) {
                                        cls_key_spill = jm_gen_spill_save(mt, cls_obj);
                                    }
                                    MIR_reg_t key = jm_transpile_box_item(mt, sf->key_expr);
                                    if (cls_key_spill >= 0) {
                                        jm_gen_spill_load(mt, cls_obj, cls_key_spill);
                                    }
                                    key = jm_call_1(mt, "js_to_property_key", MIR_T_I64,
                                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                                    jm_call_void_1(mt, "js_check_class_static_field_key",
                                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                                    jm_call_void_2(mt, "js_set_module_var",
                                        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)sf->key_module_var_index),
                                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                                }
                            } else {
                                if (instance_field_index >= ce->instance_field_count) continue;
                                JsInstanceFieldEntry* inf = &ce->instance_fields[instance_field_index++];
                                if (inf->computed && inf->key_expr && inf->key_module_var_index >= 0) {
                                    // keep the class object live across generator suspension from
                                    // instance computed keys; later metadata/static setup still
                                    // uses this register in the same class-definition sequence.
                                    int cls_key_spill = -1;
                                    if (mt->in_generator && jm_has_yield(inf->key_expr)) {
                                        cls_key_spill = jm_gen_spill_save(mt, cls_obj);
                                    }
                                    MIR_reg_t key = jm_transpile_box_item(mt, inf->key_expr);
                                    if (cls_key_spill >= 0) {
                                        jm_gen_spill_load(mt, cls_obj, cls_key_spill);
                                    }
                                    key = jm_call_1(mt, "js_to_property_key", MIR_T_I64,
                                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                                    jm_call_void_2(mt, "js_set_module_var",
                                        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)inf->key_module_var_index),
                                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                                }
                            }
                        }
                    }

                    // Emit static field initializers
                    if (ctor_super_val) {
                        jm_call_void_2(mt, "js_set_prototype",
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, ctor_super_val));
                    }
                    MIR_reg_t prev_static_this = jm_call_0(mt, "js_get_this", MIR_T_I64);
                    MIR_reg_t prev_static_new_target = jm_call_0(mt, "js_get_new_target", MIR_T_I64);
                    jm_call_void_1(mt, "js_set_this",
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
                    MIR_reg_t static_new_target = jm_emit_undefined(mt);
                    jm_call_void_1(mt, "js_set_direct_new_target",
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, static_new_target));
                    JsMirVarEntry* static_js_this_var = jm_find_var(mt, "_js_this");
                    MIR_reg_t prev_static_js_this = 0;
                    if (static_js_this_var) {
                        prev_static_js_this = jm_new_reg(mt, "prev_static_jt", MIR_T_I64);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, prev_static_js_this),
                            MIR_new_reg_op(mt->ctx, static_js_this_var->reg)));
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, static_js_this_var->reg),
                            MIR_new_reg_op(mt->ctx, cls_obj)));
                    }
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
                                jm_emit_class_static_block(mt, ce, ce->static_blocks[static_block_index++]);
                            }
                        }
                        emitted_ordered_static_elements = true;
                    }
                    if (!emitted_ordered_static_elements) for (int fi = 0; fi < ce->static_field_count; fi++) {
                        JsStaticFieldEntry* sf = &ce->static_fields[fi];
                        if (sf->computed && sf->key_expr) {
                            MIR_reg_t key;
                            if (sf->key_module_var_index >= 0) {
                                key = jm_call_1(mt, "js_get_module_var", MIR_T_I64,
                                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)sf->key_module_var_index));
                            } else {
                                key = jm_transpile_box_item(mt, sf->key_expr);
                                key = jm_call_1(mt, "js_to_property_key", MIR_T_I64,
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                                jm_call_void_1(mt, "js_check_class_static_field_key",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                            }
                            MIR_reg_t val;
                            if (sf->initializer) {
                                val = jm_transpile_box_item(mt, sf->initializer);
                            } else {
                                val = jm_new_reg(mt, "sf_undef", MIR_T_I64);
                                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                    MIR_new_reg_op(mt->ctx, val),
                                    MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED)));
                            }
                            jm_call_void_2(mt, "js_set_function_name_if_anonymous",
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                            jm_call_3(mt, "js_create_data_property", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                            if (sf->name && jm_is_private_name(sf->name)) {
                                jm_call_void_3(mt, "js_private_brand_add",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
                            }
                        } else if (sf->module_var_index >= 0) {
                            MIR_reg_t val;
                            if (sf->initializer) {
                                val = jm_transpile_box_item(mt, sf->initializer);
                            } else {
                                val = jm_new_reg(mt, "sf_undef", MIR_T_I64);
                                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                    MIR_new_reg_op(mt->ctx, val),
                                    MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED)));
                            }
                            jm_call_void_2(mt, "js_set_module_var",
                                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)sf->module_var_index),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                            // Also store as own property on class for hasOwnProperty/in/getOwnPropertyDescriptor
                            if (sf->name) {
                                char display_buf[256];
                                const char* display_name = sf->name->chars;
                                if (strncmp(display_name, "__private_", 10) == 0) {
                                    int len = snprintf(display_buf, sizeof(display_buf), "#%s", display_name + 10);
                                    display_name = display_buf;
                                    (void)len;
                                }
                                MIR_reg_t fn_name = jm_box_string_literal(mt, display_name, strlen(display_name));
                                jm_call_void_2(mt, "js_set_function_name_if_anonymous",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_name));
                                MIR_reg_t key = jm_box_string_literal(mt, sf->name->chars, (int)sf->name->len);
                                jm_call_void_1(mt, "js_check_class_static_field_key",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                                jm_call_3(mt, "js_create_data_property", MIR_T_I64,
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                                if (jm_is_private_name(sf->name)) {
                                    jm_call_void_3(mt, "js_private_brand_add",
                                        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                                        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
                                }
                            }
                        } else if (sf->key_expr && !sf->name) {
                            MIR_reg_t key = jm_transpile_box_item(mt, sf->key_expr);
                            key = jm_call_1(mt, "js_to_property_key", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                            jm_call_void_1(mt, "js_check_class_static_field_key",
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                            MIR_reg_t val;
                            if (sf->initializer) {
                                val = jm_transpile_box_item(mt, sf->initializer);
                            } else {
                                val = jm_new_reg(mt, "sf_undef", MIR_T_I64);
                                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                    MIR_new_reg_op(mt->ctx, val),
                                    MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED)));
                            }
                            jm_call_void_2(mt, "js_set_function_name_if_anonymous",
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                            jm_call_3(mt, "js_create_data_property", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                            if (sf->name && jm_is_private_name(sf->name)) {
                                jm_call_void_3(mt, "js_private_brand_add",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
                            }
                        } else if (sf->name) {
                            MIR_reg_t val;
                            if (sf->initializer) {
                                val = jm_transpile_box_item(mt, sf->initializer);
                            } else {
                                val = jm_new_reg(mt, "sf_undef", MIR_T_I64);
                                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                    MIR_new_reg_op(mt->ctx, val),
                                    MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED)));
                            }
                            char display_buf[256];
                            const char* display_name = sf->name->chars;
                            if (strncmp(display_name, "__private_", 10) == 0) {
                                int len = snprintf(display_buf, sizeof(display_buf), "#%s", display_name + 10);
                                display_name = display_buf;
                                (void)len;
                            }
                            MIR_reg_t fn_name = jm_box_string_literal(mt, display_name, strlen(display_name));
                            jm_call_void_2(mt, "js_set_function_name_if_anonymous",
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_name));
                            MIR_reg_t key = jm_box_string_literal(mt, sf->name->chars, (int)sf->name->len);
                            jm_call_void_1(mt, "js_check_class_static_field_key",
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                            jm_call_3(mt, "js_create_data_property", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                            if (jm_is_private_name(sf->name)) {
                                jm_call_void_3(mt, "js_private_brand_add",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
                            }
                        }
                    }
                    // Emit static blocks
                    if (!emitted_ordered_static_elements) for (int si = 0; si < ce->static_block_count; si++) {
                        if (ce->static_blocks[si]) {
                            jm_emit_class_static_block(mt, ce, ce->static_blocks[si]);
                        }
                    }
                    if (static_js_this_var) {
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, static_js_this_var->reg),
                            MIR_new_reg_op(mt->ctx, prev_static_js_this)));
                    }
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
        JsAstNode* s = blk->statements;
        while (s) { jm_transpile_statement(mt, s); s = s->next; }
        jm_pop_scope(mt);

        // Js55 P19: restore prior tracking.
        jm_restore_last_closure_snapshot(mt, &blk_saved_last_closure);
        break;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* es = (JsExpressionStatementNode*)stmt;
        if (es->expression) {
            MIR_reg_t val = jm_transpile_box_item(mt, es->expression);
            if (!mt->eval_completion_reg && es->expression->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
                jm_call_void_1(mt, "js_discard_value",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            }
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

        // Initialize return tracking
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, return_val_reg),
            MIR_new_int_op(mt->ctx, 0)));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, has_return_reg),
            MIR_new_int_op(mt->ctx, 0)));

        // Push try context
        if (mt->try_ctx_depth < 16) {
            JsTryContext* tc = &mt->try_ctx_stack[mt->try_ctx_depth++];
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
            tc->saved_exc_flag_reg = 0;
            tc->saved_exc_val_reg = 0;
        }

        // Save with-scope depth so we can restore it if an exception escapes a 'with' block
        MIR_reg_t saved_with_depth = jm_call_0(mt, "js_with_save_depth", MIR_T_I64);
        int saved_with_depth_spill = -1;
        if (mt->in_generator) {
            saved_with_depth_spill = jm_gen_spill_save(mt, saved_with_depth);
        }

        // Save the transient call-argument stack mark. An exception thrown while
        // evaluating a call's arguments unwinds past that call's restore, leaving
        // its half-built arg frame on the stack; resetting to this mark on the
        // catch/finally path reclaims it (otherwise the stack grows per throw).
        MIR_reg_t saved_args_mark = jm_call_0(mt, "js_args_save", MIR_T_I64);
        int saved_args_mark_spill = -1;
        if (mt->in_generator) {
            saved_args_mark_spill = jm_gen_spill_save(mt, saved_args_mark);
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
                    MIR_reg_t exc_check = jm_call_0(mt, "js_check_exception", MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                        MIR_new_label_op(mt->ctx, catch_label),
                        MIR_new_reg_op(mt->ctx, exc_check)));
                } else if (has_finally) {
                    // try/finally without catch: check and jump to finally, then propagate
                    MIR_reg_t exc_check = jm_call_0(mt, "js_check_exception", MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                        MIR_new_label_op(mt->ctx, finally_label),
                        MIR_new_reg_op(mt->ctx, exc_check)));
                }
                s = s->next;
            }
            jm_pop_scope(mt);  // v20 TDZ: pop try block scope
        }

        // Normal exit from try: jump to finally (or end)
        // Pop the try context so throws in catch propagate to outer handler
        if (mt->try_ctx_depth > 0) mt->try_ctx_depth--;
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, has_finally ? finally_label : end_label)));

        // === Catch block ===
        if (has_catch) {
            jm_emit_label(mt, catch_label);

            // Restore with-scope depth (exception may have escaped a 'with' block)
            if (saved_with_depth_spill >= 0) {
                jm_gen_spill_load(mt, saved_with_depth, saved_with_depth_spill);
            }
            jm_call_void_1(mt, "js_with_restore_depth", MIR_T_I64,
                MIR_new_reg_op(mt->ctx, saved_with_depth));

            // Reclaim any arg frames leaked by a throw during argument evaluation
            if (saved_args_mark_spill >= 0) {
                jm_gen_spill_load(mt, saved_args_mark, saved_args_mark_spill);
            }
            jm_call_void_1(mt, "js_args_restore", MIR_T_I64,
                MIR_new_reg_op(mt->ctx, saved_args_mark));

            JsCatchNode* catch_node = (JsCatchNode*)try_node->handler;

            // Clear exception and get thrown value
            MIR_reg_t thrown_val = jm_call_0(mt, "js_clear_exception", MIR_T_I64);

            // If there's a finally block, push a context for return-in-catch
            // (so return in catch still goes through finally)
            // This context has no catch_label, so throws propagate outward
            bool pushed_catch_ctx = false;
            if (has_finally && mt->try_ctx_depth < 16) {
                JsTryContext* tc = &mt->try_ctx_stack[mt->try_ctx_depth++];
                tc->catch_label = 0;
                tc->finally_label = finally_label;
                tc->end_label = end_label;
                tc->return_val_reg = return_val_reg;
                tc->has_return_reg = has_return_reg;
                tc->has_catch = false;
                tc->has_finally = true;
                tc->yield_state_only = false;
                tc->finally_body = try_node->finalizer; // v18
                tc->saved_exc_flag_reg = 0;
                tc->saved_exc_val_reg = 0;
                pushed_catch_ctx = true;
            } else if (!has_finally && mt->in_generator && mt->try_ctx_depth < 16) {
                // Generator yield inside catch body needs the inner try's state
                // regs (return_val_reg/has_return_reg) re-initialized on resume.
                // Push a synthetic ctx marked yield_state_only so the resume
                // code finds these regs while throw/return routing skips it.
                JsTryContext* tc = &mt->try_ctx_stack[mt->try_ctx_depth++];
                tc->catch_label = 0;
                tc->finally_label = 0;
                tc->end_label = 0;
                tc->return_val_reg = return_val_reg;
                tc->has_return_reg = has_return_reg;
                tc->has_catch = false;
                tc->has_finally = false;
                tc->yield_state_only = true;
                tc->finally_body = NULL;
                tc->saved_exc_flag_reg = 0;
                tc->saved_exc_val_reg = 0;
                pushed_catch_ctx = true;
            }

            // catch has two lexical environments: one for the parameter and a
            // nested one for the body. Keeping them separate lets destructuring
            // defaults capture the parameter without seeing body let/const TDZ.
            jm_push_scope(mt);
            if (catch_node->param && catch_node->param->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* param_id = (JsIdentifierNode*)catch_node->param;
                char vname[128];
                snprintf(vname, sizeof(vname), "_js_%.*s", (int)param_id->name->len, param_id->name->chars);
                jm_set_var(mt, vname, thrown_val);
                JsMirVarEntry* catch_entry = jm_find_var(mt, vname);
                if (catch_entry) catch_entry->from_catch_param = true;
                jm_scope_env_mark_and_writeback(mt, vname, thrown_val);
            } else if (catch_node->param && catch_node->param->node_type == JS_AST_NODE_OBJECT_PATTERN) {
                struct hashmap* catch_names = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
                    jm_name_hash, jm_name_cmp, NULL, NULL);
                jm_collect_pattern_names(catch_node->param, catch_names);
                MIR_reg_t undef_val = jm_emit_undefined(mt);
                size_t cn_iter = 0; void* cn_item;
                while (hashmap_iter(catch_names, &cn_iter, &cn_item)) {
                    JsNameSetEntry* ne = (JsNameSetEntry*)cn_item;
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
                jm_emit_object_destructure(mt, catch_node->param, thrown_val);
            } else if (catch_node->param && catch_node->param->node_type == JS_AST_NODE_ARRAY_PATTERN) {
                struct hashmap* catch_names = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
                    jm_name_hash, jm_name_cmp, NULL, NULL);
                jm_collect_pattern_names(catch_node->param, catch_names);
                MIR_reg_t undef_val = jm_emit_undefined(mt);
                size_t cn_iter = 0; void* cn_item;
                while (hashmap_iter(catch_names, &cn_iter, &cn_item)) {
                    JsNameSetEntry* ne = (JsNameSetEntry*)cn_item;
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
                jm_emit_array_destructure(mt, catch_node->param, thrown_val);
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

            // Jump to finally (or end)
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, has_finally ? finally_label : end_label)));
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

            // Reclaim any arg frames leaked by a throw during argument evaluation
            if (saved_args_mark_spill >= 0) {
                jm_gen_spill_load(mt, saved_args_mark, saved_args_mark_spill);
            }
            jm_call_void_1(mt, "js_args_restore", MIR_T_I64,
                MIR_new_reg_op(mt->ctx, saved_args_mark));

            // In generators, push a minimal try context so that yield inside
            // the finally body can re-initialize has_return_reg/return_val_reg
            // on resume. The main try context was already popped before the
            // finally block (so throws propagate outward), but the generator
            // yield save/restore needs to know about these registers.
            bool pushed_gen_finally_ctx = false;
            if (mt->in_generator && mt->try_ctx_depth < 16) {
                JsTryContext* tc = &mt->try_ctx_stack[mt->try_ctx_depth++];
                tc->catch_label = 0;
                tc->finally_label = 0;
                tc->end_label = 0;
                tc->return_val_reg = return_val_reg;
                tc->has_return_reg = has_return_reg;
                tc->has_catch = false;
                tc->has_finally = true;
                tc->yield_state_only = true;
                tc->finally_body = NULL;
                tc->saved_exc_flag_reg = 0;
                tc->saved_exc_val_reg = 0;
                pushed_gen_finally_ctx = true;
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

            // Save and clear any pending exception so finally body starts
            // with a clean exception state. This lets try/catch inside the
            // finally correctly catch only exceptions thrown within it,
            // not the outer pending exception (ES spec §13.15.8 step 7-8).
            MIR_reg_t saved_exc_flag = jm_call_0(mt, "js_check_exception", MIR_T_I64);
            MIR_reg_t saved_exc_val = jm_call_0(mt, "js_clear_exception", MIR_T_I64);
            int saved_exc_flag_spill = -1;
            int saved_exc_val_spill = -1;
            if (mt->in_generator && jm_has_yield(try_node->finalizer)) {
                saved_exc_flag_spill = jm_gen_spill_save(mt, saved_exc_flag);
                saved_exc_val_spill = jm_gen_spill_save(mt, saved_exc_val);
            }
            if (pushed_gen_finally_ctx && mt->try_ctx_depth > 0) {
                JsTryContext* tc = &mt->try_ctx_stack[mt->try_ctx_depth - 1];
                tc->saved_exc_flag_reg = saved_exc_flag;
                tc->saved_exc_val_reg = saved_exc_val;
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

            if (saved_exc_flag_spill >= 0) {
                jm_gen_spill_load(mt, saved_exc_flag, saved_exc_flag_spill);
                jm_gen_spill_load(mt, saved_exc_val, saved_exc_val_spill);
            }

            // Restore saved exception if finally completed normally.
            // If the finally body threw a new exception, it takes precedence
            // (per spec). Otherwise, re-throw the saved pending exception.
            {
                MIR_reg_t new_exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
                MIR_label_t skip_restore = jm_new_label(mt);
                // if new exception pending, skip restore (new takes precedence)
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                    MIR_new_label_op(mt->ctx, skip_restore),
                    MIR_new_reg_op(mt->ctx, new_exc)));
                // if saved exception was pending, re-throw it
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
                    MIR_new_label_op(mt->ctx, skip_restore),
                    MIR_new_reg_op(mt->ctx, saved_exc_flag)));
                jm_call_void_1(mt, "js_throw_value",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, saved_exc_val));
                jm_emit_label(mt, skip_restore);
            }

            // Pop the generator finally context (pushed for yield re-init)
            if (pushed_gen_finally_ctx && mt->try_ctx_depth > 0) mt->try_ctx_depth--;

            // After finally: check if we had a delayed return
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                MIR_new_label_op(mt->ctx, end_label),
                MIR_new_reg_op(mt->ctx, has_return_reg)));
        }

        // End label: check for delayed return
        jm_emit_label(mt, end_label);

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
            jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
                MIR_new_reg_op(mt->ctx, return_val_reg)));
        }
        jm_emit_label(mt, no_ret_label);

        // If exception is still pending (try/finally without catch, or re-throw),
        // propagate to outer try/catch or return from function
        if (!has_catch || has_finally) {
            MIR_reg_t still_exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
            MIR_label_t no_exc_label = jm_new_label(mt);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
                MIR_new_label_op(mt->ctx, no_exc_label),
                MIR_new_reg_op(mt->ctx, still_exc)));
            // If inside an outer try block, propagate to its handler
            if (mt->try_ctx_depth > 0) {
                JsTryContext* outer = &mt->try_ctx_stack[mt->try_ctx_depth - 1];
                MIR_label_t target = outer->has_catch ? outer->catch_label
                                   : (outer->has_finally ? outer->finally_label : outer->end_label);
                if (target) {
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                        MIR_new_label_op(mt->ctx, target)));
                } else {
                    MIR_reg_t null_ret = jm_emit_null(mt);
                    jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
                        MIR_new_reg_op(mt->ctx, null_ret)));
                }
            } else {
                MIR_reg_t null_ret = jm_emit_null(mt);
                jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
                    MIR_new_reg_op(mt->ctx, null_ret)));
            }
            jm_emit_label(mt, no_exc_label);
        }
        break;
    }
    case JS_AST_NODE_THROW_STATEMENT: {
        JsThrowNode* throw_node = (JsThrowNode*)stmt;
        MIR_reg_t thrown_val = jm_emit_null(mt);
        if (throw_node->argument) {
            thrown_val = jm_transpile_box_item(mt, throw_node->argument);
        }

        // If inside a try block, set the flag and jump to catch/finally.
        // Skip yield_state_only synthetic ctxes (they're for yield-resume re-init only).
        int throw_d = mt->try_ctx_depth - 1;
        while (throw_d >= 0 && mt->try_ctx_stack[throw_d].yield_state_only) throw_d--;
        if (throw_d >= 0) {
            JsTryContext* tc = &mt->try_ctx_stack[throw_d];
            // Store the thrown value in the global exception state
            jm_call_void_1(mt, "js_throw_value",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, thrown_val));
            // Jump to catch (or finally if no catch)
            MIR_label_t target = tc->has_catch ? tc->catch_label
                               : (tc->has_finally ? tc->finally_label : tc->end_label);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, target)));
        } else {
            // Not in a try block: set flag and return from function
            // (the caller's try block will check js_check_exception)
            jm_call_void_1(mt, "js_throw_value",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, thrown_val));
            MIR_reg_t null_ret = jm_emit_null(mt);
            jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
                MIR_new_reg_op(mt->ctx, null_ret)));
        }
        break;
    }
    case JS_AST_NODE_WITH_STATEMENT: {
        JsWithStatementNode* with_node = (JsWithStatementNode*)stmt;
        if (with_node->object) {
            // push with-scope object
            MIR_reg_t obj_reg = jm_transpile_box_item(mt, with_node->object);
            jm_call_void_1(mt, "js_with_push", MIR_T_I64, MIR_new_reg_op(mt->ctx, obj_reg));
            jm_emit_exc_propagate_check(mt);
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
            char vname[128];
            snprintf(vname, sizeof(vname), "_js_%.*s",
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
                MIR_reg_t key = jm_box_string_literal(mt, "default", 7);
                MIR_reg_t val = jm_call_2(mt, "js_property_get", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, ns),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                MIR_reg_t var_reg = jm_new_reg(mt, vname, MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, var_reg),
                    MIR_new_reg_op(mt->ctx, val)));
                jm_set_var(mt, vname, var_reg);
                // Also update module var for closure access
                JsModuleConstEntry lookup;
                snprintf(lookup.name, sizeof(lookup.name), "%s", vname);
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
            char vname[128];
            snprintf(vname, sizeof(vname), "_js_%.*s",
                (int)imp->namespace_name->len, imp->namespace_name->chars);
            MIR_reg_t var_reg = jm_new_reg(mt, vname, MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, var_reg),
                MIR_new_reg_op(mt->ctx, ns)));
            jm_set_var(mt, vname, var_reg);
            JsModuleConstEntry lookup;
            snprintf(lookup.name, sizeof(lookup.name), "%s", vname);
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
                    // Get exported value: val = js_property_get(ns, remote_name)
                    MIR_reg_t key = jm_box_string_literal(mt, isp->remote_name->chars,
                        (int)isp->remote_name->len);
                    MIR_reg_t val = jm_call_2(mt, "js_property_get", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, ns),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                    // Bind to local name
                    char vname[128];
                    snprintf(vname, sizeof(vname), "_js_%.*s",
                        (int)isp->local_name->len, isp->local_name->chars);
                    MIR_reg_t var_reg = jm_new_reg(mt, vname, MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, var_reg),
                        MIR_new_reg_op(mt->ctx, val)));
                    jm_set_var(mt, vname, var_reg);
                    // Also update module var for closure access
                    JsModuleConstEntry lookup;
                    snprintf(lookup.name, sizeof(lookup.name), "%s", vname);
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
