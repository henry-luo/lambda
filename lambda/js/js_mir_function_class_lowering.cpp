#include "js_mir_internal.hpp"
#include "js_exec_profile.h"

// ============================================================================
// Function definition transpiler
// ============================================================================

static bool jm_function_arguments_are_aliased(JsMirTranspiler* mt, JsFuncCollected* fc, JsFunctionNode* fn) {
    return !fc->has_non_simple_params &&
           !mt->is_module &&
           !mt->is_global_strict &&
           !jm_has_use_strict_directive(fn);
}

static void jm_activate_arguments_aliasing(JsMirTranspiler* mt, JsFuncCollected* fc, JsFunctionNode* fn, MIR_reg_t args_reg) {
    if (jm_function_arguments_are_aliased(mt, fc, fn)) {
        mt->arguments_reg = args_reg;
        mt->arguments_param_count = 0;
        JsAstNode* ap = fn->params;
        while (ap && mt->arguments_param_count < 16) {
            char apname[128];
            jm_get_param_name(ap, mt->arguments_param_count, apname, sizeof(apname));
            snprintf(mt->arguments_param_names[mt->arguments_param_count], 128, "%s", apname);
            mt->arguments_param_count++;
            ap = ap->next;
        }
    } else {
        mt->arguments_reg = args_reg;
        mt->arguments_param_count = 0;
    }
}

static bool jm_function_has_formal_arguments_binding(JsFunctionNode* fn) {
    if (!fn) return false;
    JsAstNode* param = fn->params;
    for (int i = 0; param; i++, param = param->next) {
        char pname[128];
        jm_get_param_name(param, i, pname, sizeof(pname));
        if (strcmp(pname, "_js_arguments") == 0) return true;
    }
    return false;
}

static JsMirVarEntry* jm_function_find_current_scope_var(JsMirTranspiler* mt, const char* name) {
    if (!mt || !name || mt->scope_depth < 0 || mt->scope_depth >= 64 ||
        !mt->var_scopes[mt->scope_depth]) {
        return NULL;
    }
    JsVarScopeEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    JsVarScopeEntry* found = (JsVarScopeEntry*)hashmap_get(mt->var_scopes[mt->scope_depth], &key);
    return found ? &found->var : NULL;
}

static bool jm_function_has_direct_body_function_binding(JsFunctionNode* fn, const char* vname) {
    if (!fn || !vname || !fn->body ||
        fn->body->node_type != JS_AST_NODE_BLOCK_STATEMENT) {
        return false;
    }
    JsBlockNode* body = (JsBlockNode*)fn->body;
    for (JsAstNode* stmt = body->statements; stmt; stmt = stmt->next) {
        if (stmt->node_type != JS_AST_NODE_FUNCTION_DECLARATION) continue;
        JsFunctionNode* decl = (JsFunctionNode*)stmt;
        if (!decl->name) continue;
        char name[128];
        snprintf(name, sizeof(name), "_js_%.*s",
            (int)decl->name->len, decl->name->chars);
        if (strcmp(name, vname) == 0) return true;
    }
    return false;
}

static JsFunctionNode* jm_function_find_direct_body_function_binding(JsFunctionNode* fn, const char* vname) {
    if (!fn || !vname || !fn->body ||
        fn->body->node_type != JS_AST_NODE_BLOCK_STATEMENT) {
        return NULL;
    }
    JsBlockNode* body = (JsBlockNode*)fn->body;
    for (JsAstNode* stmt = body->statements; stmt; stmt = stmt->next) {
        if (stmt->node_type != JS_AST_NODE_FUNCTION_DECLARATION) continue;
        JsFunctionNode* decl = (JsFunctionNode*)stmt;
        if (!decl->name) continue;
        char name[128];
        snprintf(name, sizeof(name), "_js_%.*s",
            (int)decl->name->len, decl->name->chars);
        if (strcmp(name, vname) == 0) return decl;
    }
    return NULL;
}

static bool jm_param_tree_has_assignment_pattern(JsAstNode* node) {
    for (JsAstNode* cur = node; cur; cur = cur->next) {
        switch (cur->node_type) {
        case JS_AST_NODE_ASSIGNMENT_PATTERN: {
            JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)cur;
            if (ap->left && jm_param_tree_has_assignment_pattern(ap->left)) return true;
            return true;
        }
        case JS_AST_NODE_OBJECT_PATTERN: {
            JsObjectPatternNode* op = (JsObjectPatternNode*)cur;
            for (JsAstNode* prop = op->properties; prop; prop = prop->next) {
                if (prop->node_type == JS_AST_NODE_PROPERTY) {
                    JsPropertyNode* p = (JsPropertyNode*)prop;
                    if (p->value && jm_param_tree_has_assignment_pattern(p->value)) return true;
                } else if (prop->node_type == JS_AST_NODE_REST_PROPERTY ||
                           prop->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                    JsSpreadElementNode* sp = (JsSpreadElementNode*)prop;
                    if (sp->argument && jm_param_tree_has_assignment_pattern(sp->argument)) return true;
                }
            }
            break;
        }
        case JS_AST_NODE_ARRAY_PATTERN: {
            JsArrayPatternNode* ap = (JsArrayPatternNode*)cur;
            if (jm_param_tree_has_assignment_pattern(ap->elements)) return true;
            break;
        }
        case JS_AST_NODE_REST_ELEMENT:
        case JS_AST_NODE_SPREAD_ELEMENT: {
            JsSpreadElementNode* sp = (JsSpreadElementNode*)cur;
            if (sp->argument && jm_param_tree_has_assignment_pattern(sp->argument)) return true;
            break;
        }
        default:
            break;
        }
    }
    return false;
}

static bool jm_is_ascii_ident_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '$';
}

static bool jm_is_ascii_ident_part(char c) {
    return jm_is_ascii_ident_start(c) || (c >= '0' && c <= '9');
}

static bool jm_eval_source_declares_var_name(String* source, const char* name, int name_len) {
    if (!source || !name || name_len <= 0) return false;
    const char* src = source->chars;
    int len = (int)source->len;
    for (int i = 0; i + 3 <= len; i++) {
        if (src[i] != 'v' || src[i + 1] != 'a' || src[i + 2] != 'r') continue;
        if (i > 0 && jm_is_ascii_ident_part(src[i - 1])) continue;
        if (i + 3 < len && jm_is_ascii_ident_part(src[i + 3])) continue;

        int pos = i + 3;
        while (pos < len && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\n' || src[pos] == '\r')) pos++;
        while (pos < len) {
            if (!jm_is_ascii_ident_start(src[pos])) break;
            int ident_start = pos++;
            while (pos < len && jm_is_ascii_ident_part(src[pos])) pos++;
            int ident_len = pos - ident_start;
            if (ident_len == name_len && strncmp(src + ident_start, name, name_len) == 0) return true;
            while (pos < len && src[pos] != ',' && src[pos] != ';' && src[pos] != '\n' && src[pos] != '\r') pos++;
            if (pos >= len || src[pos] != ',') break;
            pos++;
            while (pos < len && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\n' || src[pos] == '\r')) pos++;
        }
    }
    return false;
}

static bool jm_eval_source_conflicts_with_param(JsFunctionNode* fn, String* source) {
    if (!fn || !source) return false;
    int param_index = 0;
    for (JsAstNode* param = fn->params; param; param = param->next) {
        char param_name[128];
        jm_get_param_name(param, param_index, param_name, sizeof(param_name));
        const char* bare = param_name;
        if (strncmp(bare, "_js_", 4) == 0) bare += 4;
        if (jm_eval_source_declares_var_name(source, bare, (int)strlen(bare))) return true;
        param_index++;
    }
    return false;
}

static bool jm_default_param_has_conflicting_direct_eval(JsFunctionNode* fn, JsAstNode* expr) {
    if (!fn || !expr) return false;
    if (expr->node_type == JS_AST_NODE_CALL_EXPRESSION) {
        JsCallNode* call = (JsCallNode*)expr;
        if (call->callee && call->callee->node_type == JS_AST_NODE_IDENTIFIER && call->arguments) {
            JsIdentifierNode* callee = (JsIdentifierNode*)call->callee;
            if (callee->name && callee->name->len == 4 && strncmp(callee->name->chars, "eval", 4) == 0 &&
                call->arguments->node_type == JS_AST_NODE_LITERAL) {
                JsLiteralNode* lit = (JsLiteralNode*)call->arguments;
                if (lit->literal_type == JS_LITERAL_STRING && lit->value.string_value &&
                    jm_eval_source_conflicts_with_param(fn, lit->value.string_value)) {
                    return true;
                }
            }
        }
    }
    return false;
}

static void jm_collect_lexical_decl_names(JsAstNode* node, struct hashmap* names);

static void jm_collect_lexical_decl_statements(JsAstNode* stmt, struct hashmap* names) {
    for (; stmt; stmt = stmt->next) {
        jm_collect_lexical_decl_names(stmt, names);
    }
}

static void jm_collect_lexical_decl_names(JsAstNode* node, struct hashmap* names) {
    if (!node || !names) return;
    switch (node->node_type) {
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)node;
        if (vd->kind != JS_VAR_LET && vd->kind != JS_VAR_CONST) return;
        for (JsAstNode* decl = vd->declarations; decl; decl = decl->next) {
            if (decl->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
            jm_collect_pattern_names(((JsVariableDeclaratorNode*)decl)->id, names);
        }
        break;
    }
    case JS_AST_NODE_CLASS_DECLARATION: {
        JsClassNode* cls = (JsClassNode*)node;
        if (cls->name) {
            char name[128];
            snprintf(name, sizeof(name), "_js_%.*s", (int)cls->name->len, cls->name->chars);
            jm_name_set_add(names, name);
        }
        break;
    }
    case JS_AST_NODE_BLOCK_STATEMENT:
        jm_collect_lexical_decl_statements(((JsBlockNode*)node)->statements, names);
        break;
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* in = (JsIfNode*)node;
        jm_collect_lexical_decl_names(in->consequent, names);
        jm_collect_lexical_decl_names(in->alternate, names);
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT:
        jm_collect_lexical_decl_names(((JsWhileNode*)node)->body, names);
        break;
    case JS_AST_NODE_DO_WHILE_STATEMENT:
        jm_collect_lexical_decl_names(((JsDoWhileNode*)node)->body, names);
        break;
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* fn = (JsForNode*)node;
        jm_collect_lexical_decl_names(fn->init, names);
        jm_collect_lexical_decl_names(fn->body, names);
        break;
    }
    case JS_AST_NODE_FOR_IN_STATEMENT:
    case JS_AST_NODE_FOR_OF_STATEMENT: {
        JsForOfNode* fo = (JsForOfNode*)node;
        jm_collect_lexical_decl_names(fo->left, names);
        jm_collect_lexical_decl_names(fo->body, names);
        break;
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* sw = (JsSwitchNode*)node;
        for (JsAstNode* c = sw->cases; c; c = c->next) {
            if (c->node_type == JS_AST_NODE_SWITCH_CASE) {
                jm_collect_lexical_decl_statements(((JsSwitchCaseNode*)c)->consequent, names);
            }
        }
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* tn = (JsTryNode*)node;
        jm_collect_lexical_decl_names(tn->block, names);
        jm_collect_lexical_decl_names(tn->handler, names);
        jm_collect_lexical_decl_names(tn->finalizer, names);
        break;
    }
    case JS_AST_NODE_CATCH_CLAUSE:
        jm_collect_lexical_decl_names(((JsCatchNode*)node)->body, names);
        break;
    case JS_AST_NODE_LABELED_STATEMENT:
        jm_collect_lexical_decl_names(((JsLabeledStatementNode*)node)->body, names);
        break;
    default:
        break;
    }
}

static bool jm_capture_is_nfe_binding(JsMirTranspiler* mt, JsFuncCollected* fc, const char* name) {
    if (!mt || !fc || !name) return false;
    int idx = (int)(fc - mt->func_entries);
    while (idx >= 0 && idx < mt->func_count) {
        JsFuncCollected* cur = &mt->func_entries[idx];
        JsFunctionNode* fn = cur->node;
        if (fn && fn->base.node_type == JS_AST_NODE_FUNCTION_EXPRESSION && fn->name) {
            char self_name[128];
            snprintf(self_name, sizeof(self_name), "_js_%.*s", (int)fn->name->len, fn->name->chars);
            if (strcmp(name, self_name) == 0) return true;
        }
        idx = cur->parent_index;
    }
    return false;
}

static MIR_reg_t jm_transpile_default_param_value(JsMirTranspiler* mt, JsFunctionNode* fn, JsAstNode* expr) {
    if (jm_default_param_has_conflicting_direct_eval(fn, expr)) {
        MIR_reg_t msg = jm_box_string_literal(mt, "Invalid direct eval var declaration in parameter initializer", 56);
        jm_call_void_2(mt, "js_throw_named_error",
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, msg));
        jm_emit_exc_propagate_check(mt);
        return jm_emit_undefined(mt);
    }
    return jm_transpile_box_item(mt, expr);
}

void jm_define_function(JsMirTranspiler* mt, JsFuncCollected* fc) {
    JsFunctionNode* fn = fc->node;
    int param_count = jm_count_params(fn);
    bool has_captures = (fc->capture_count > 0);

    // Phase 4: Check if this function qualifies for a native version.
    // Requirements: no captures, all params inferred as INT or FLOAT,
    //               return type is INT or FLOAT, param_count <= 16.
    // Also respect has_native_version flag (Phase 1.76 may have revoked it).
    bool generate_native = false;
    if (fc->has_native_version &&
        !has_captures && !fc->has_non_simple_params &&
        param_count > 0 && param_count <= 16 &&
        (fc->return_type == LMD_TYPE_INT || fc->return_type == LMD_TYPE_FLOAT)) {
        generate_native = true;
        for (int i = 0; i < param_count; i++) {
            if (fc->param_types[i] != LMD_TYPE_INT && fc->param_types[i] != LMD_TYPE_FLOAT) {
                generate_native = false;
                break;
            }
        }
    }

    // --- Generate native version if eligible ---
    if (generate_native) {
        // Create native function: <name>_n(native_p1, native_p2, ...) → native_ret
        char native_name[140];
        snprintf(native_name, sizeof(native_name), "%s_n", fc->name);

        MIR_var_t* n_params = (MIR_var_t*)alloca(param_count * sizeof(MIR_var_t));
        char** n_param_names = (char**)alloca(param_count * sizeof(char*));
        JsAstNode* param_node = fn->params;
        for (int i = 0; i < param_count; i++) {
            n_param_names[i] = (char*)alloca(128);
            jm_get_param_name(param_node, i, n_param_names[i], 128);
            MIR_type_t mtype = (fc->param_types[i] == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
            n_params[i] = {mtype, n_param_names[i], 0};
            param_node = param_node ? param_node->next : NULL;
        }

        MIR_type_t native_ret_type = (fc->return_type == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
        MIR_item_t native_item = MIR_new_func_arr(mt->ctx, native_name, 1, &native_ret_type, param_count, n_params);
        MIR_func_t native_func = MIR_get_item_func(mt->ctx, native_item);

        fc->native_func_item = native_item;
        fc->has_native_version = true;
        jm_register_local_func(mt, native_name, native_item);

        // Save transpiler state
        MIR_item_t saved_item = mt->current_func_item;
        MIR_func_t saved_func = mt->current_func;
        int saved_scope_depth = mt->scope_depth;
        int saved_loop_depth = mt->loop_depth;
        bool saved_in_native = mt->in_native_func;
        bool saved_in_main = mt->in_main;
        JsFuncCollected* saved_fc = mt->current_fc;
        JsClassEntry* saved_class_n = mt->current_class;
        MIR_reg_t saved_eval_completion_n = mt->eval_completion_reg;
        // Save TCO state
        JsFuncCollected* saved_tco_func = mt->tco_func;
        MIR_label_t saved_tco_label = mt->tco_label;
        MIR_reg_t saved_tco_count = mt->tco_count_reg;
        bool saved_tail_pos = mt->in_tail_position;
        // Save exception label — must not leak from outer function into native version
        MIR_label_t saved_except_label = mt->func_except_label;

        if (jm_has_use_strict_directive(fn)) {
            fc->is_strict = true;
        }

        mt->current_func_item = native_item;
        mt->current_func = native_func;
        mt->loop_depth = 0;
        mt->for_of_depth = 0;
        mt->pending_label_name = NULL;
        mt->pending_label_len = 0;
        mt->in_native_func = true;
        mt->in_main = false;
        mt->current_fc = fc;
        mt->eval_completion_reg = 0;  // disable in native functions
        mt->tco_func = NULL;
        mt->in_tail_position = false;
        mt->tco_jumped = false;
        mt->func_except_label = 0;    // reset — native func needs its own exception label

        jm_push_scope(mt);

        // Register parameters with their inferred native types
        param_node = fn->params;
        for (int i = 0; i < param_count; i++) {
            if (param_node) {
                char vname[128];
                jm_get_param_name(param_node, i, vname, sizeof(vname));
                MIR_reg_t preg = MIR_reg(mt->ctx, vname, native_func);
                MIR_type_t mtype = (fc->param_types[i] == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
                jm_set_var(mt, vname, preg, mtype, fc->param_types[i]);
            }
            param_node = param_node ? param_node->next : NULL;
        }

        // TCO setup: wrap body in a loop if this function has tail-recursive calls
        if (fc->is_tco_eligible) {
            log_debug("js-mir TCO: enabling loop transform for %s", fc->name);
            mt->tco_func = fc;

            // Create iteration counter, init to 0
            mt->tco_count_reg = jm_new_reg(mt, "tco_count", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, mt->tco_count_reg),
                MIR_new_int_op(mt->ctx, 0)));

            // TCO loop label
            mt->tco_label = jm_new_label(mt);
            jm_emit_label(mt, mt->tco_label);

            // Increment: tco_count += 1
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                MIR_new_reg_op(mt->ctx, mt->tco_count_reg),
                MIR_new_reg_op(mt->ctx, mt->tco_count_reg),
                MIR_new_int_op(mt->ctx, 1)));

            // Guard: if (tco_count <= 1000000) goto ok
            MIR_label_t ok_label = jm_new_label(mt);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BLE, MIR_new_label_op(mt->ctx, ok_label),
                MIR_new_reg_op(mt->ctx, mt->tco_count_reg),
                MIR_new_int_op(mt->ctx, 1000000)));

            // Overflow: return 0 (safety net — should never trigger for correct code)
            if (native_ret_type == MIR_T_D) {
                jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_double_op(mt->ctx, 0.0)));
            } else {
                jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_int_op(mt->ctx, 0)));
            }

            jm_emit_label(mt, ok_label);
        }

        // P9: Pre-scan variable types before transpiling native body
        jm_prescan_float_widening(mt, fn->body);

        // Hoist var declarations: register all var-declared names initialized
        // to null/undefined BEFORE function hoisting (mirrors JS var hoisting)
        if (fn->body && fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            struct hashmap* body_locals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            bool effective_strict = mt->is_global_strict || mt->is_module ||
                (fc && fc->is_strict) || jm_has_use_strict_directive(fn);
            struct hashmap* annexb_lex_collisions = NULL;
            if (!effective_strict) {
                annexb_lex_collisions = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                    jm_name_hash, jm_name_cmp, NULL, NULL);
                jm_collect_all_let_const_names_recursive(fn->body, annexb_lex_collisions);
            }
            jm_collect_body_locals(fn->body, body_locals, true);  // var_only: only hoist var declarations
            size_t viter = 0; void* vitem;
            while (hashmap_iter(body_locals, &viter, &vitem)) {
                JsNameSetEntry* e = (JsNameSetEntry*)vitem;
                if (e->from_func_decl && effective_strict) {
                    log_debug("js-mir: strict skip nested function hoist '%s'", e->name);
                    continue;
                }
                if (e->from_func_decl && annexb_lex_collisions &&
                    jm_name_set_has(annexb_lex_collisions, e->name)) {
                    log_debug("js-mir: AnnexB skip function hoist '%s' (lexical collision)", e->name);
                    continue;
                }
                if (e->from_func_decl && fc && fc->uses_arguments &&
                    strcmp(e->name, "_js_arguments") == 0 &&
                    jm_function_has_formal_arguments_binding(fn)) {
                    log_debug("js-mir: AnnexB skip function hoist '%s' (arguments binding)", e->name);
                    continue;
                }
                if (!jm_find_var(mt, e->name)) {
                    // Skip hoisting vars that are module vars in IIFE body functions
                    // — these are accessed via js_get/set_module_var, not local registers
                    if (mt->current_fc && mt->current_fc->is_iife_body && mt->module_consts) {
                        JsModuleConstEntry mclookup;
                        snprintf(mclookup.name, sizeof(mclookup.name), "%s", e->name);
                        JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                        if (mc && mc->const_type == MCONST_MODVAR && mc->is_iife_var) continue;
                    }
                    MIR_reg_t vr = jm_new_reg(mt, e->name, MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, vr),
                        MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEF_VAL)));
                    jm_set_var(mt, e->name, vr);
                    // v50: mark as hoisted so var_reused path can distinguish from parameters
                    {
                        JsMirVarEntry* hvar = jm_find_var(mt, e->name);
                        if (hvar) hvar->from_hoist = true;
                    }
                }
            }
            if (annexb_lex_collisions) hashmap_free(annexb_lex_collisions);
            hashmap_free(body_locals);

            // v20 TDZ: Reinitialize let/const variables to TDZ sentinel
            struct hashmap* let_consts = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            jm_collect_let_const_names(fn->body, let_consts);
            size_t lciter = 0; void* lcitem;
            while (hashmap_iter(let_consts, &lciter, &lcitem)) {
                JsNameSetEntry* lce = (JsNameSetEntry*)lcitem;
                JsMirVarEntry* ve = jm_function_find_current_scope_var(mt, lce->name);
                if (!ve) {
                    // Create register for let/const (no longer hoisted by jm_collect_body_locals)
                    MIR_reg_t vr = jm_new_reg(mt, lce->name, MIR_T_I64);
                    jm_set_var(mt, lce->name, vr);
                    ve = jm_function_find_current_scope_var(mt, lce->name);
                }
                if (ve) {
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, ve->reg),
                        MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_TDZ)));
                    ve->is_let_const = true;
                    ve->is_const = (lce->var_kind == 2);
                    ve->tdz_active = true;
                }
            }
            hashmap_free(let_consts);
        }

        // Hoist inner function declarations (same as boxed path)
        if (fn->body && fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* hblk = (JsBlockNode*)fn->body;
            JsAstNode* hs = hblk->statements;
            while (hs) {
                if (hs->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
                    JsFunctionNode* inner_fn = (JsFunctionNode*)hs;
                    if (inner_fn->name) {
                        JsFuncCollected* inner_fc = jm_find_collected_func(mt, inner_fn);
                        if (inner_fc && inner_fc->func_item) {
                            char hvname[128];
                            snprintf(hvname, sizeof(hvname), "_js_%.*s",
                                (int)inner_fn->name->len, inner_fn->name->chars);
                            MIR_reg_t hvar = jm_new_reg(mt, hvname, MIR_T_I64);
                            MIR_reg_t fn_item = jm_create_func_or_closure(mt, inner_fc);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, hvar),
                                MIR_new_reg_op(mt->ctx, fn_item)));
                            jm_set_var(mt, hvname, hvar);
                        }
                    }
                }
                hs = hs->next;
            }
        }

        // Transpile body (same as original, but params are native-typed)
        if (fn->body) {
            if (fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
                JsBlockNode* blk = (JsBlockNode*)fn->body;
                JsAstNode* s = blk->statements;
                while (s) { jm_transpile_statement(mt, s); s = s->next; }
            } else {
                // Expression body (arrow function): return native value
                MIR_reg_t val = jm_transpile_as_native(mt, fn->body,
                    jm_get_effective_type(mt, fn->body), fc->return_type);
                jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, val)));
                goto finish_native;
            }
        }

        // Implicit return 0 (native)
        {
            MIR_reg_t zero = jm_new_reg(mt, "ret0", native_ret_type);
            if (native_ret_type == MIR_T_D) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                    MIR_new_reg_op(mt->ctx, zero), MIR_new_double_op(mt->ctx, 0.0)));
            } else {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, zero), MIR_new_int_op(mt->ctx, 0)));
            }
            jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, zero)));
        }

    finish_native:
        // Exception landing pad for native function (return 0/0.0 on exception)
        if (mt->func_except_label) {
            jm_emit_label(mt, mt->func_except_label);
            MIR_reg_t exc_ret = jm_new_reg(mt, "exc_ret", native_ret_type);
            if (native_ret_type == MIR_T_D) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                    MIR_new_reg_op(mt->ctx, exc_ret), MIR_new_double_op(mt->ctx, 0.0)));
            } else {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, exc_ret), MIR_new_int_op(mt->ctx, 0)));
            }
            jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, exc_ret)));
        }
        jm_pop_scope(mt);
        MIR_finish_func(mt->ctx);

        // Restore state
        mt->current_func_item = saved_item;
        mt->current_func = saved_func;
        mt->scope_depth = saved_scope_depth;
        mt->loop_depth = saved_loop_depth;
        mt->in_native_func = saved_in_native;
        mt->in_main = saved_in_main;
        mt->current_fc = saved_fc;
        mt->current_class = saved_class_n;
        mt->eval_completion_reg = saved_eval_completion_n;
        mt->tco_func = saved_tco_func;
        mt->tco_label = saved_tco_label;
        mt->tco_count_reg = saved_tco_count;
        mt->in_tail_position = saved_tail_pos;
        mt->tco_jumped = false;
        mt->func_except_label = saved_except_label;  // restore outer function's exception label

        log_debug("js-mir P4: generated native version %s (params: %d, ret: %s%s)",
            native_name, param_count,
            fc->return_type == LMD_TYPE_INT ? "INT" : "FLOAT",
            fc->is_tco_eligible ? ", TCO" : "");
    }

    // --- v15: Generate generator state machine function if is_generator ---
    MIR_item_t gen_sm_func_item = NULL;
    int gen_env_total_slots = 0;
    int gen_this_slot = -1;  // env slot for 'this' in generator/async state machines
    int gen_args_slot = -1;  // env slot for 'arguments' object in generator/async state machines
    int gen_active_iterator_slot = -1;  // env slot for iterator cleanup on generator.return()

    if (fn->is_generator) {
        // Count suspension points to determine number of states. Async
        // generators share the generator state machine, so hidden awaits from
        // `for await` need resume labels alongside source `yield` expressions.
        // +1 for implicit "param binding" yield that separates param destructuring
        // from body execution (ES spec: FunctionDeclarationInstantiation is eager)
        int yield_count = jm_count_yields(fn->body) + (fn->is_async ? jm_count_awaits(fn->body) : 0) + 1;
        if (yield_count > 63) yield_count = 63;  // safety cap matching gen_state_labels size

        // Collect local variable names for env slot assignment
        struct hashmap* gen_locals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        if (fn->body) jm_collect_body_locals(fn->body, gen_locals);  // generators need all locals for state machine
        struct hashmap* gen_lexicals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        if (fn->body) jm_collect_lexical_decl_names(fn->body, gen_lexicals);

        // Also collect destructured param variable names so they get env slots.
        // These variables are created during param destructuring in state 0 and
        // must survive across the implicit param-binding yield.
        {
            JsAstNode* dp = fn->params;
            while (dp) {
                JsAstNode* pat = dp;
                if (pat->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN)
                    pat = ((JsAssignmentPatternNode*)pat)->left;
                if (pat->node_type == JS_AST_NODE_REST_ELEMENT ||
                    pat->node_type == JS_AST_NODE_SPREAD_ELEMENT)
                    pat = ((JsSpreadElementNode*)pat)->argument;
                if (pat->node_type == JS_AST_NODE_OBJECT_PATTERN ||
                    pat->node_type == JS_AST_NODE_ARRAY_PATTERN) {
                    jm_collect_pattern_names(pat, gen_locals);
                }
                dp = dp->next;
            }
        }

        // Count distinct locals
        int local_count = 0;
        {
            size_t iter = 0; void* item;
            while (hashmap_iter(gen_locals, &iter, &item)) local_count++;
        }

        // Env layout: [captures... | params... | locals... | this | arguments | padding(32 for for-of internals)]
        int cap_offset = 0;
        int param_offset = fc->capture_count;
        int local_offset = fc->capture_count + param_count;
        gen_env_total_slots = local_offset + local_count;
        gen_this_slot = gen_env_total_slots;  // reserve slot for 'this'
        gen_env_total_slots += 1;
        if (fc->uses_arguments) {
            gen_args_slot = gen_env_total_slots;  // reserve slot for 'arguments'
            gen_env_total_slots += 1;
        }
        gen_env_total_slots += 32;  // padding for dynamically allocated for-of/for-in loop vars
        int gen_spill_start = gen_env_total_slots;  // spill slots start here
        gen_env_total_slots += 128;  // padding for generator yield spill slots (temporaries across yields)
        gen_active_iterator_slot = gen_env_total_slots++;

        // Create state machine function: gen_sm_<name>(Item* env, Item input, int64_t state) -> Item
        char sm_name[160];
        snprintf(sm_name, sizeof(sm_name), "gen_sm_%s_%d", fc->name, mt->label_counter++);

        MIR_var_t sm_params[3] = {
            {MIR_T_I64, "gen_env", 0},   // Item* env (passed as i64)
            {MIR_T_I64, "gen_input", 0},  // Item sent_value
            {MIR_T_I64, "gen_state", 0}   // int64_t state
        };
        MIR_type_t sm_ret = MIR_T_I64;
        gen_sm_func_item = MIR_new_func_arr(mt->ctx, sm_name, 1, &sm_ret, 3, sm_params);
        MIR_func_t sm_func = MIR_get_item_func(mt->ctx, gen_sm_func_item);
        jm_register_local_func(mt, sm_name, gen_sm_func_item);

        // Save transpiler state
        MIR_item_t saved_item_sm = mt->current_func_item;
        MIR_func_t saved_func_sm = mt->current_func;
        int saved_scope_depth_sm = mt->scope_depth;
        int saved_loop_depth_sm = mt->loop_depth;
        bool saved_in_native_sm = mt->in_native_func;
        bool saved_in_main_sm = mt->in_main;
        JsFuncCollected* saved_fc_sm = mt->current_fc;
        JsClassEntry* saved_class_sm = mt->current_class;
        MIR_reg_t saved_scope_env_reg_sm = mt->scope_env_reg;
        int saved_scope_env_slot_sm = mt->scope_env_slot_count;
        int saved_func_index_sm = mt->current_func_index;
        MIR_reg_t saved_eval_local_frame_reg_sm = mt->eval_local_frame_reg;
        bool saved_in_generator = mt->in_generator;

        if (jm_has_use_strict_directive(fn)) {
            fc->is_strict = true;
        }

        mt->current_func_item = gen_sm_func_item;
        mt->current_func = sm_func;
        mt->loop_depth = 0;
        mt->for_of_depth = 0;
        mt->pending_label_name = NULL;
        mt->pending_label_len = 0;
        mt->in_native_func = false;
        mt->in_main = false;
        mt->current_fc = fc;
        mt->current_class = NULL;
        mt->scope_env_reg = 0;
        mt->scope_env_slot_count = 0;
        mt->current_func_index = (int)(fc - mt->func_entries);
        mt->func_except_label = 0;  // reset for generator state machine
        mt->eval_local_frame_reg = 0;

        // Set up generator state
        mt->in_generator = true;
        mt->gen_yield_index = 0;
        mt->gen_yield_count = yield_count;
        mt->gen_capture_offset = cap_offset;
        mt->gen_param_offset = param_offset;
        mt->gen_local_offset = local_offset;
        mt->gen_local_slot_count = (gen_args_slot >= 0 ? gen_args_slot : gen_this_slot) + 1;  // next available slot (within padding area)
        mt->gen_spill_slot_next = gen_spill_start;  // spill slots start at beginning of spill padding area
        mt->gen_active_iterator_slot = gen_active_iterator_slot;

        jm_push_scope(mt);
        mt->eval_local_frame_reg = jm_new_reg(mt, "eval_local_frame", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, mt->eval_local_frame_reg),
            MIR_new_int_op(mt->ctx, 0)));

        // Get parameters from function signature
        mt->gen_env_reg = MIR_reg(mt->ctx, "gen_env", sm_func);
        mt->gen_input_reg = MIR_reg(mt->ctx, "gen_input", sm_func);
        mt->gen_state_reg = MIR_reg(mt->ctx, "gen_state", sm_func);

        // Create state labels
        for (int si = 0; si <= yield_count; si++) {
            mt->gen_state_labels[si] = jm_new_label(mt);
        }
        mt->gen_done_label = jm_new_label(mt);

        // Emit state dispatch: switch on state
        for (int si = 0; si <= yield_count; si++) {
            MIR_reg_t cmp = jm_new_reg(mt, "scmp", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQS, MIR_new_reg_op(mt->ctx, cmp),
                MIR_new_reg_op(mt->ctx, mt->gen_state_reg),
                MIR_new_int_op(mt->ctx, (int64_t)si)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                MIR_new_label_op(mt->ctx, mt->gen_state_labels[si]),
                MIR_new_reg_op(mt->ctx, cmp)));
        }
        // Unknown state → done
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, mt->gen_done_label)));

        // State 0 label (initial entry)
        jm_emit_label(mt, mt->gen_state_labels[0]);

        // Pre-register ONLY destructured param variable names with env slots.
        // This must happen before param destructuring so that the variables created
        // by jm_emit_object_destructure/jm_emit_array_destructure get env-backed
        // entries that survive across the implicit param-binding yield.
        // Body locals are hoisted LATER (after the implicit yield) to avoid
        // prematurely shadowing outer-scope variables during param default evaluation.
        int gen_dstr_param_count = 0;
        {
            struct hashmap* dstr_names = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            JsAstNode* dp = fn->params;
            while (dp) {
                JsAstNode* pat = dp;
                if (pat->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN)
                    pat = ((JsAssignmentPatternNode*)pat)->left;
                if (pat->node_type == JS_AST_NODE_REST_ELEMENT ||
                    pat->node_type == JS_AST_NODE_SPREAD_ELEMENT)
                    pat = ((JsSpreadElementNode*)pat)->argument;
                if (pat->node_type == JS_AST_NODE_OBJECT_PATTERN ||
                    pat->node_type == JS_AST_NODE_ARRAY_PATTERN) {
                    jm_collect_pattern_names(pat, dstr_names);
                }
                dp = dp->next;
            }
            int li = 0;
            size_t diter = 0; void* ditem;
            while (hashmap_iter(dstr_names, &diter, &ditem)) {
                JsNameSetEntry* ns = (JsNameSetEntry*)ditem;
                if (!jm_find_var(mt, ns->name)) {
                    MIR_reg_t vr = jm_new_reg(mt, ns->name, MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, vr),
                        MIR_new_uint_op(mt->ctx, (uint64_t)ITEM_JS_UNDEFINED)));
                    JsVarScopeEntry entry;
                    memset(&entry, 0, sizeof(entry));
                    snprintf(entry.name, sizeof(entry.name), "%s", ns->name);
                    entry.var.reg = vr;
                    entry.var.from_env = true;
                    entry.var.env_slot = local_offset + li;
                    entry.var.env_reg = mt->gen_env_reg;
                    entry.var.typed_array_type = -1;
                    entry.var.from_hoist = true;
                    hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
                    li++;
                }
            }
            gen_dstr_param_count = li;
            hashmap_free(dstr_names);
        }

        // `arguments` is available during FunctionDeclarationInstantiation, so
        // destructuring defaults in generator parameters must be able to read it
        // in the state-0 parameter binding phase.
        if (gen_args_slot >= 0 && fc->uses_arguments) {
            MIR_reg_t args_reg = jm_new_reg(mt, "_js_arguments", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, args_reg),
                MIR_new_mem_op(mt->ctx, MIR_T_I64,
                    gen_args_slot * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1)));
            JsVarScopeEntry args_entry;
            memset(&args_entry, 0, sizeof(args_entry));
            snprintf(args_entry.name, sizeof(args_entry.name), "_js_arguments");
            args_entry.var.reg = args_reg;
            args_entry.var.from_env = true;
            args_entry.var.env_slot = gen_args_slot;
            args_entry.var.env_reg = mt->gen_env_reg;
            args_entry.var.typed_array_type = -1;
            hashmap_set(mt->var_scopes[mt->scope_depth], &args_entry);
            jm_activate_arguments_aliasing(mt, fc, fn, args_reg);
        }

        // Captures must be visible during generator parameter initialization.
        // Default parameter expressions can create closures that reference outer
        // captures or the named generator expression's private self binding.
        char gen_self_capture_name[128] = {0};
        if (fn->name) {
            snprintf(gen_self_capture_name, sizeof(gen_self_capture_name), "_js_%.*s",
                (int)fn->name->len, fn->name->chars);
        }
        for (int ci = 0; ci < fc->capture_count; ci++) {
            // Skip MCONST_MODVAR captures only for non-scope-env captures:
            // Per-closure envs have stale snapshots, so module vars read live.
            // Scope_env captures are live (shared env), so always load from env.
            bool has_scope_slot = (fc->captures[ci].scope_env_slot >= 0);
            bool is_self_capture = (gen_self_capture_name[0] &&
                strcmp(fc->captures[ci].name, gen_self_capture_name) == 0);
            if (!has_scope_slot && !is_self_capture && mt->module_consts) {
                JsModuleConstEntry mclookup;
                snprintf(mclookup.name, sizeof(mclookup.name), "%s", fc->captures[ci].name);
                JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                if (mc && mc->const_type == MCONST_MODVAR && !fc->captures[ci].force_env_capture) {
                    continue;
                }
            }

            MIR_reg_t cap_reg = jm_new_reg(mt, fc->captures[ci].name, MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, cap_reg),
                MIR_new_mem_op(mt->ctx, MIR_T_I64,
                    (cap_offset + ci) * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1)));
            JsVarScopeEntry entry;
            memset(&entry, 0, sizeof(entry));
            snprintf(entry.name, sizeof(entry.name), "%s", fc->captures[ci].name);
            entry.var.reg = cap_reg;
            entry.var.from_env = true;
            entry.var.env_slot = cap_offset + ci;
            entry.var.env_reg = mt->gen_env_reg;
            entry.var.typed_array_type = -1;
            entry.var.is_nfe_binding = fc->captures[ci].is_nfe_binding ||
                jm_capture_is_nfe_binding(mt, fc, fc->captures[ci].name);
            if (fc->captures[ci].is_let_const) {
                entry.var.tdz_active = true;
                entry.var.is_let_const = true;
                entry.var.is_const = fc->captures[ci].is_const;
            }
            hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
        }

        // Load parameters from env (stored there during generator creation)
        JsAstNode* sm_param_node = fn->params;
        for (int i = 0; i < param_count; i++) {
            if (sm_param_node) {
                char vname[128];
                jm_get_param_name(sm_param_node, i, vname, sizeof(vname));
                MIR_reg_t preg = jm_new_reg(mt, vname, MIR_T_I64);
                // Load from env[param_offset + i]
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, preg),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64,
                        (param_offset + i) * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1)));
                JsVarScopeEntry entry;
                memset(&entry, 0, sizeof(entry));
                snprintf(entry.name, sizeof(entry.name), "%s", vname);
                entry.var.reg = preg;
                entry.var.from_env = true;
                entry.var.env_slot = param_offset + i;
                entry.var.env_reg = mt->gen_env_reg;
                entry.var.typed_array_type = -1;
                hashmap_set(mt->var_scopes[mt->scope_depth], &entry);

                // Default parameter handling (assignment pattern)
                if (sm_param_node->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
                    JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)sm_param_node;
                    if (ap->right) {
                        MIR_label_t skip_label = jm_new_label(mt);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BNE,
                            MIR_new_label_op(mt->ctx, skip_label),
                            MIR_new_reg_op(mt->ctx, preg),
                            MIR_new_uint_op(mt->ctx, (uint64_t)ITEM_JS_UNDEFINED)));
                        MIR_reg_t def_val = jm_transpile_default_param_value(mt, fn, ap->right);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, preg),
                            MIR_new_reg_op(mt->ctx, def_val)));
                        jm_emit_label(mt, skip_label);
                    }
                }

                // Unwrap assignment pattern for destructured default params
                JsAstNode* destr_pat = sm_param_node;
                if (destr_pat->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN)
                    destr_pat = ((JsAssignmentPatternNode*)destr_pat)->left;
                if (destr_pat->node_type == JS_AST_NODE_REST_ELEMENT ||
                    destr_pat->node_type == JS_AST_NODE_SPREAD_ELEMENT)
                    destr_pat = ((JsSpreadElementNode*)destr_pat)->argument;

                // Object destructuring with RequireObjectCoercible check
                if (destr_pat->node_type == JS_AST_NODE_OBJECT_PATTERN) {
                    jm_call_void_1(mt, "js_require_object_coercible",
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, preg));
                    MIR_reg_t param_exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
                    MIR_label_t skip_param_destr = jm_new_label(mt);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, skip_param_destr),
                        MIR_new_reg_op(mt->ctx, param_exc)));
                    jm_emit_object_destructure(mt, destr_pat, preg);
                    jm_emit_label(mt, skip_param_destr);
                }

                // Array destructuring
                if (destr_pat->node_type == JS_AST_NODE_ARRAY_PATTERN) {
                    jm_emit_array_destructure(mt, destr_pat, preg);
                }
            }
            sm_param_node = sm_param_node ? sm_param_node->next : NULL;
        }

        // === Implicit "param binding" yield ===
        // ES spec: FunctionDeclarationInstantiation (parameter binding including
        // destructuring) must happen synchronously at call time, not lazily on
        // first .next(). We emit an implicit yield here that separates param
        // binding from body execution. js_generator_create eagerly runs state 0
        // to execute param destructuring, and the generator starts at state 1.
        {
            // Check for pending exception from param destructuring
            MIR_reg_t param_exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                MIR_new_label_op(mt->ctx, mt->gen_done_label),
                MIR_new_reg_op(mt->ctx, param_exc)));

            // Save all env-backed variables (params + destructured locals) to env
            for (int sd = 1; sd <= mt->scope_depth; sd++) {
                if (!mt->var_scopes[sd]) continue;
                size_t iter2 = 0; void* item2;
                while (hashmap_iter(mt->var_scopes[sd], &iter2, &item2)) {
                    JsVarScopeEntry* e = (JsVarScopeEntry*)item2;
                    if (e->var.env_slot >= 0 && e->var.from_env) {
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_mem_op(mt->ctx, MIR_T_I64,
                                e->var.env_slot * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1),
                            MIR_new_reg_op(mt->ctx, e->var.reg)));
                    }
                }
            }

            // Return [undefined, 1] — implicit yield to separate param binding from body
            MIR_reg_t pundef = jm_emit_undefined(mt);
            MIR_reg_t pb_result = jm_call_2(mt, "js_gen_yield_result", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, pundef),
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)1));
            jm_emit_eval_local_pop_if_needed(mt);
            jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, pb_result)));

            // State 1 label: body execution starts here (on first .next() call)
            mt->gen_yield_index = 1;
            jm_emit_label(mt, mt->gen_state_labels[1]);

            // Reload all env-backed variables after resume
            for (int sd = 1; sd <= mt->scope_depth; sd++) {
                if (!mt->var_scopes[sd]) continue;
                size_t iter2 = 0; void* item2;
                while (hashmap_iter(mt->var_scopes[sd], &iter2, &item2)) {
                    JsVarScopeEntry* e = (JsVarScopeEntry*)item2;
                    if (e->var.env_slot >= 0 && e->var.from_env) {
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, e->var.reg),
                            MIR_new_mem_op(mt->ctx, MIR_T_I64,
                                e->var.env_slot * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1)));
                    }
                }
            }
        }

        // Load 'this' from env[this_slot] and register as _js_this variable
        if (gen_this_slot >= 0) {
            MIR_reg_t this_reg = jm_new_reg(mt, "_js_this", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, this_reg),
                MIR_new_mem_op(mt->ctx, MIR_T_I64,
                    gen_this_slot * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1)));
            JsVarScopeEntry this_entry;
            memset(&this_entry, 0, sizeof(this_entry));
            snprintf(this_entry.name, sizeof(this_entry.name), "_js_this");
            this_entry.var.reg = this_reg;
            this_entry.var.from_env = true;
            this_entry.var.env_slot = gen_this_slot;
            this_entry.var.env_reg = mt->gen_env_reg;
            this_entry.var.typed_array_type = -1;
            hashmap_set(mt->var_scopes[mt->scope_depth], &this_entry);
        }

        // Load 'arguments' from env[args_slot] if this generator uses arguments
        if (gen_args_slot >= 0 && fc->uses_arguments) {
            MIR_reg_t args_reg = jm_new_reg(mt, "_js_arguments", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, args_reg),
                MIR_new_mem_op(mt->ctx, MIR_T_I64,
                    gen_args_slot * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1)));
            JsVarScopeEntry args_entry;
            memset(&args_entry, 0, sizeof(args_entry));
            snprintf(args_entry.name, sizeof(args_entry.name), "_js_arguments");
            args_entry.var.reg = args_reg;
            args_entry.var.from_env = true;
            args_entry.var.env_slot = gen_args_slot;
            args_entry.var.env_reg = mt->gen_env_reg;
            args_entry.var.typed_array_type = -1;
            hashmap_set(mt->var_scopes[mt->scope_depth], &args_entry);
            jm_activate_arguments_aliasing(mt, fc, fn, args_reg);
        }

        // Hoist body-local var declarations with env slots.
        // Destructured param names were already pre-registered before state 0;
        // skip those and continue env slot assignment from gen_dstr_param_count.
        if (fn->body && fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            int li = gen_dstr_param_count;
            size_t liter = 0; void* litem;
            while (hashmap_iter(gen_locals, &liter, &litem)) {
                JsNameSetEntry* ns = (JsNameSetEntry*)litem;
                if (!jm_find_var(mt, ns->name)) {
                    bool is_lexical = hashmap_get(gen_lexicals, ns) != NULL;
                    uint64_t hoist_init = is_lexical ? ITEM_NULL_VAL : ITEM_JS_UNDEF_VAL;
                    MIR_reg_t vr = jm_new_reg(mt, ns->name, MIR_T_I64);
                    // Generator locals are env-backed so resumes see the same value.
                    // A var hoist must start as JS undefined before its declaration runs.
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, vr),
                        MIR_new_int_op(mt->ctx, (int64_t)hoist_init)));
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64,
                            (local_offset + li) * (int)sizeof(uint64_t),
                            mt->gen_env_reg, 0, 1),
                        MIR_new_reg_op(mt->ctx, vr)));
                    JsVarScopeEntry entry;
                    memset(&entry, 0, sizeof(entry));
                    snprintf(entry.name, sizeof(entry.name), "%s", ns->name);
                    entry.var.reg = vr;
                    entry.var.from_env = true;
                    entry.var.env_slot = local_offset + li;
                    entry.var.env_reg = mt->gen_env_reg;
                    entry.var.typed_array_type = -1;
                    entry.var.from_hoist = true;
                    if (is_lexical) {
                        entry.var.is_let_const = true;
                    }
                    hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
                    li++;
                }
            }
        }

        // Allocate shared scope env for child closures inside this generator.
        // Must be done inside the state machine (not the wrapper) so closures
        // created after yields still have access to mutable captures.
        if (fc->has_scope_env && fc->scope_env_count > 0) {
            int scope_env_alloc_count = fc->scope_env_count;
            if (fc->reuse_parent_env && fc->reuse_env_slot_count > scope_env_alloc_count) {
                scope_env_alloc_count = fc->reuse_env_slot_count;
            }
            int scope_env_slot = mt->gen_local_slot_count++;
            mt->scope_env_reg = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
                MIR_T_I64, MIR_new_int_op(mt->ctx, scope_env_alloc_count));
            mt->scope_env_slot_count = scope_env_alloc_count;

            // Store scope env pointer in gen_env so it persists across yields
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64,
                    scope_env_slot * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1),
                MIR_new_reg_op(mt->ctx, mt->scope_env_reg)));

            // Register as a scoped variable so it gets saved/loaded across yields
            JsVarScopeEntry senv_entry;
            memset(&senv_entry, 0, sizeof(senv_entry));
            snprintf(senv_entry.name, sizeof(senv_entry.name), "_scope_env");
            senv_entry.var.reg = mt->scope_env_reg;
            senv_entry.var.from_env = true;
            senv_entry.var.env_slot = scope_env_slot;
            senv_entry.var.env_reg = mt->gen_env_reg;
            senv_entry.var.typed_array_type = -1;
            hashmap_set(mt->var_scopes[mt->scope_depth], &senv_entry);

            // Populate scope env with current values and mark vars for write-back
            // Only iterate normal slots — NFE extra slots are populated by self-patch code
            int gen_populate_limit = fc->scope_env_normal_count > 0 ? fc->scope_env_normal_count : fc->scope_env_count;
            for (int s = 0; s < gen_populate_limit; s++) {
                const char* sname = fc->scope_env_names[s];
                int target_slot = s;
                if (fc->reuse_parent_env) {
                    for (int c = 0; c < fc->capture_count; c++) {
                        if (strcmp(sname, fc->captures[c].name) == 0 &&
                            fc->captures[c].scope_env_slot >= 0) {
                            target_slot = fc->captures[c].scope_env_slot;
                            break;
                        }
                    }
                }
                JsMirVarEntry* svar = jm_find_var(mt, sname);
                MIR_reg_t val;
                if (svar) {
                    val = svar->reg;
                    if (jm_is_native_type(svar->type_id))
                        val = jm_box_native(mt, svar->reg, svar->type_id);
                    svar->in_scope_env = true;
                    svar->scope_env_slot = target_slot;
                    svar->scope_env_reg = mt->scope_env_reg;
                } else if (strcmp(sname, "_js_this") == 0) {
                    val = jm_call_0(mt, "js_get_lexical_this_binding", MIR_T_I64);
                    JsVarScopeEntry this_entry;
                    memset(&this_entry, 0, sizeof(this_entry));
                    snprintf(this_entry.name, sizeof(this_entry.name), "_js_this");
                    this_entry.var.reg = val;
                    this_entry.var.mir_type = MIR_T_I64;
                    this_entry.var.type_id = LMD_TYPE_ANY;
                    this_entry.var.in_scope_env = true;
                    this_entry.var.scope_env_slot = target_slot;
                    this_entry.var.scope_env_reg = mt->scope_env_reg;
                    this_entry.var.typed_array_type = -1;
                    hashmap_set(mt->var_scopes[mt->scope_depth], &this_entry);
                } else if (strcmp(sname, "_js_new.target") == 0) {
                    // Child arrows read new.target lexically from this shared
                    // env; store the creation-context value before direct calls
                    // get a chance to clear the runtime slot.
                    val = jm_call_0(mt, "js_get_new_target", MIR_T_I64);
                    JsVarScopeEntry nt_entry;
                    memset(&nt_entry, 0, sizeof(nt_entry));
                    snprintf(nt_entry.name, sizeof(nt_entry.name), "_js_new.target");
                    nt_entry.var.reg = val;
                    nt_entry.var.mir_type = MIR_T_I64;
                    nt_entry.var.type_id = LMD_TYPE_ANY;
                    nt_entry.var.in_scope_env = true;
                    nt_entry.var.scope_env_slot = target_slot;
                    nt_entry.var.scope_env_reg = mt->scope_env_reg;
                    nt_entry.var.typed_array_type = -1;
                    hashmap_set(mt->var_scopes[mt->scope_depth], &nt_entry);
                } else if (mt->module_consts) {
                    JsModuleConstEntry mclookup;
                    snprintf(mclookup.name, sizeof(mclookup.name), "%s", sname);
                    JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                    if (mc && mc->const_type == MCONST_MODVAR) {
                        val = jm_call_1(mt, "js_get_module_var", MIR_T_I64,
                            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mc->int_val));
                    } else if (mc && mc->const_type == MCONST_FUNC) {
                        int fi = (int)mc->int_val;
                        if (fi >= 0 && fi < mt->func_count && mt->func_entries[fi].func_item) {
                            val = jm_create_func_or_closure(mt, &mt->func_entries[fi]);
                        } else {
                            val = jm_emit_null(mt);
                        }
                    } else {
                        val = jm_emit_null(mt);
                    }
                } else {
                    val = jm_emit_null(mt);
                }
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, target_slot * (int)sizeof(uint64_t), mt->scope_env_reg, 0, 1),
                    MIR_new_reg_op(mt->ctx, val)));
            }
            log_debug("js-mir: generator '%s' allocated scope env with %d slots at gen_env[%d]",
                fc->name, scope_env_alloc_count, scope_env_slot);
        }

        // Transpile generator body
        if (fn->body) {
            if (fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
                JsBlockNode* blk = (JsBlockNode*)fn->body;
                JsAstNode* s = blk->statements;
                while (s) {
                    jm_transpile_statement(mt, s);
                    jm_emit_exc_propagate_check(mt);
                    s = s->next;
                }
            } else {
                MIR_reg_t val = jm_transpile_box_item(mt, fn->body);
                // Arrow-body generator (unusual, but handle it)
                MIR_reg_t done_result = jm_call_2(mt, "js_gen_yield_result", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)-1));
                jm_emit_eval_local_pop_if_needed(mt);
                jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, done_result)));
                goto gen_sm_finish;
            }
        }

        // Emit any state labels that were not emitted during body transpilation.
        // This prevents MIR_link crashes from dangling label references when
        // jm_count_yields over-counts the number of actual yield resume points.
        for (int si = mt->gen_yield_index + 1; si <= yield_count; si++) {
            jm_emit_label(mt, mt->gen_state_labels[si]);
        }

        // Implicit return at end of generator → done
        jm_emit_label(mt, mt->gen_done_label);
        {
            MIR_reg_t undef_val = jm_emit_undefined(mt);
            MIR_reg_t done_result = jm_call_2(mt, "js_gen_yield_result", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, undef_val),
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)-1));
            jm_emit_eval_local_pop_if_needed(mt);
            jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, done_result)));
        }

    gen_sm_finish:
        // Exception landing pad for generator state machine
        if (mt->func_except_label) {
            jm_emit_label(mt, mt->func_except_label);
            MIR_reg_t exc_ret = jm_emit_null(mt);
            jm_emit_eval_local_pop_if_needed(mt);
            jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, exc_ret)));
        }
        jm_pop_scope(mt);
        MIR_finish_func(mt->ctx);

        // Restore transpiler state
        mt->current_func_item = saved_item_sm;
        mt->current_func = saved_func_sm;
        mt->scope_depth = saved_scope_depth_sm;
        mt->loop_depth = saved_loop_depth_sm;
        mt->in_native_func = saved_in_native_sm;
        mt->in_main = saved_in_main_sm;
        mt->current_fc = saved_fc_sm;
        mt->current_class = saved_class_sm;
        mt->scope_env_reg = saved_scope_env_reg_sm;
        mt->scope_env_slot_count = saved_scope_env_slot_sm;
        mt->current_func_index = saved_func_index_sm;
        mt->eval_local_frame_reg = saved_eval_local_frame_reg_sm;
        mt->in_generator = saved_in_generator;

        hashmap_free(gen_lexicals);
        hashmap_free(gen_locals);

        log_debug("js-mir: generated generator state machine %s (yields: %d, env slots: %d)",
            sm_name, yield_count, gen_env_total_slots);
    }

    // --- Phase 6: Generate async state machine function if is_async with awaits ---
    if (fn->is_async && !fn->is_generator) {
        int await_count = jm_count_awaits(fn->body);
        if (await_count > 0) {
            if (await_count > 63) await_count = 63;  // safety cap matching gen_state_labels size

            // Collect local variable names for env slot assignment
            struct hashmap* async_locals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            if (fn->body) jm_collect_body_locals(fn->body, async_locals);  // async needs all locals for state machine

            int local_count = 0;
            {
                size_t iter = 0; void* item;
                while (hashmap_iter(async_locals, &iter, &item)) local_count++;
            }

            // Env layout: [captures... | params... | locals... | this | arguments | padding(32 for for-of internals)]
            int cap_offset = 0;
            int param_offset_sm = fc->capture_count;
            int local_offset = fc->capture_count + param_count;
            gen_env_total_slots = local_offset + local_count;
            gen_this_slot = gen_env_total_slots;  // reserve slot for 'this'
            gen_env_total_slots += 1;
            if (fc->uses_arguments) {
                gen_args_slot = gen_env_total_slots;  // reserve slot for 'arguments'
                gen_env_total_slots += 1;
            }
            gen_env_total_slots += 32;  // padding for dynamically allocated for-of/for-in loop vars
            int gen_spill_start = gen_env_total_slots;  // spill slots start here
            gen_env_total_slots += 128;  // padding for async yield spill slots
            gen_active_iterator_slot = gen_env_total_slots++;

            // Create state machine function: async_sm_<name>(Item* env, Item input, int64_t state) -> Item
            char sm_name[160];
            snprintf(sm_name, sizeof(sm_name), "async_sm_%s_%d", fc->name, mt->label_counter++);

            MIR_var_t sm_params[3] = {
                {MIR_T_I64, "gen_env", 0},
                {MIR_T_I64, "gen_input", 0},
                {MIR_T_I64, "gen_state", 0}
            };
            MIR_type_t sm_ret = MIR_T_I64;
            gen_sm_func_item = MIR_new_func_arr(mt->ctx, sm_name, 1, &sm_ret, 3, sm_params);
            MIR_func_t sm_func = MIR_get_item_func(mt->ctx, gen_sm_func_item);
            jm_register_local_func(mt, sm_name, gen_sm_func_item);

            // Save transpiler state
            MIR_item_t saved_item_sm = mt->current_func_item;
            MIR_func_t saved_func_sm = mt->current_func;
            int saved_scope_depth_sm = mt->scope_depth;
            int saved_loop_depth_sm = mt->loop_depth;
            bool saved_in_native_sm = mt->in_native_func;
            bool saved_in_main_sm = mt->in_main;
            JsFuncCollected* saved_fc_sm = mt->current_fc;
            JsClassEntry* saved_class_sm = mt->current_class;
            MIR_reg_t saved_scope_env_reg_sm = mt->scope_env_reg;
            int saved_scope_env_slot_sm = mt->scope_env_slot_count;
            int saved_func_index_sm = mt->current_func_index;
            bool saved_in_generator = mt->in_generator;
            bool saved_in_async = mt->in_async;
            MIR_label_t saved_except_label_sm = mt->func_except_label;

            if (jm_has_use_strict_directive(fn)) {
                fc->is_strict = true;
            }

            mt->current_func_item = gen_sm_func_item;
            mt->current_func = sm_func;
            mt->loop_depth = 0;
            mt->for_of_depth = 0;
            mt->pending_label_name = NULL;
            mt->pending_label_len = 0;
            mt->in_native_func = false;
            mt->in_main = false;
            mt->current_fc = fc;
            mt->current_class = NULL;
            mt->scope_env_reg = 0;
            mt->scope_env_slot_count = 0;
            mt->current_func_index = (int)(fc - mt->func_entries);
            mt->func_except_label = 0;

            // Set both flags: in_generator reuses gen_* infrastructure, in_async for await handling
            mt->in_generator = true;
            mt->in_async = true;
            mt->gen_yield_index = 0;
            mt->gen_yield_count = await_count;
            mt->gen_capture_offset = cap_offset;
            mt->gen_param_offset = param_offset_sm;
            mt->gen_local_offset = local_offset;
            mt->gen_local_slot_count = (gen_args_slot >= 0 ? gen_args_slot : gen_this_slot) + 1;  // next available slot (within padding area)
            mt->gen_spill_slot_next = gen_spill_start;  // spill slots start at beginning of spill padding area
            mt->gen_active_iterator_slot = gen_active_iterator_slot;

            jm_push_scope(mt);

            mt->gen_env_reg = MIR_reg(mt->ctx, "gen_env", sm_func);
            mt->gen_input_reg = MIR_reg(mt->ctx, "gen_input", sm_func);
            mt->gen_state_reg = MIR_reg(mt->ctx, "gen_state", sm_func);

            // Create state labels (one per await + one for initial entry)
            for (int si = 0; si <= await_count; si++) {
                mt->gen_state_labels[si] = jm_new_label(mt);
            }
            mt->gen_done_label = jm_new_label(mt);
            MIR_label_t async_sm_catch_label = jm_new_label(mt);
            mt->func_except_label = async_sm_catch_label;

            // Emit state dispatch: switch on state
            for (int si = 0; si <= await_count; si++) {
                MIR_reg_t cmp = jm_new_reg(mt, "scmp", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQS, MIR_new_reg_op(mt->ctx, cmp),
                    MIR_new_reg_op(mt->ctx, mt->gen_state_reg),
                    MIR_new_int_op(mt->ctx, (int64_t)si)));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                    MIR_new_label_op(mt->ctx, mt->gen_state_labels[si]),
                    MIR_new_reg_op(mt->ctx, cmp)));
            }
            // Unknown state → done
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, mt->gen_done_label)));

            // State 0 label (initial entry)
            jm_emit_label(mt, mt->gen_state_labels[0]);

            // Load parameters from env
            JsAstNode* sm_param_node = fn->params;
            for (int i = 0; i < param_count; i++) {
                if (sm_param_node) {
                    char vname[128];
                    jm_get_param_name(sm_param_node, i, vname, sizeof(vname));
                    MIR_reg_t preg = jm_new_reg(mt, vname, MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, preg),
                        MIR_new_mem_op(mt->ctx, MIR_T_I64,
                            (param_offset_sm + i) * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1)));
                    JsVarScopeEntry entry;
                    memset(&entry, 0, sizeof(entry));
                    snprintf(entry.name, sizeof(entry.name), "%s", vname);
                    entry.var.reg = preg;
                    entry.var.from_env = true;
                    entry.var.env_slot = param_offset_sm + i;
                    entry.var.env_reg = mt->gen_env_reg;
                    entry.var.typed_array_type = -1;
                    hashmap_set(mt->var_scopes[mt->scope_depth], &entry);

                    // Default parameter handling (assignment pattern)
                    if (sm_param_node->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
                        JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)sm_param_node;
                        if (ap->right) {
                            MIR_label_t skip_label = jm_new_label(mt);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BNE,
                                MIR_new_label_op(mt->ctx, skip_label),
                                MIR_new_reg_op(mt->ctx, preg),
                                MIR_new_uint_op(mt->ctx, (uint64_t)ITEM_JS_UNDEFINED)));
                            MIR_reg_t def_val = jm_transpile_default_param_value(mt, fn, ap->right);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, preg),
                                MIR_new_reg_op(mt->ctx, def_val)));
                            jm_emit_label(mt, skip_label);
                        }
                    }

                    // Unwrap assignment pattern for destructured default params
                    JsAstNode* destr_pat = sm_param_node;
                    if (destr_pat->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN)
                        destr_pat = ((JsAssignmentPatternNode*)destr_pat)->left;
                    if (destr_pat->node_type == JS_AST_NODE_REST_ELEMENT ||
                        destr_pat->node_type == JS_AST_NODE_SPREAD_ELEMENT)
                        destr_pat = ((JsSpreadElementNode*)destr_pat)->argument;

                    // Object destructuring with RequireObjectCoercible check
                    if (destr_pat->node_type == JS_AST_NODE_OBJECT_PATTERN) {
                        jm_call_void_1(mt, "js_require_object_coercible",
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, preg));
                        MIR_reg_t param_exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
                        MIR_label_t skip_param_destr = jm_new_label(mt);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, skip_param_destr),
                            MIR_new_reg_op(mt->ctx, param_exc)));
                        jm_emit_object_destructure(mt, destr_pat, preg);
                        jm_emit_label(mt, skip_param_destr);
                    }

                    // Array destructuring
                    if (destr_pat->node_type == JS_AST_NODE_ARRAY_PATTERN) {
                        jm_emit_array_destructure(mt, destr_pat, preg);
                    }
                }
                sm_param_node = sm_param_node ? sm_param_node->next : NULL;
            }

            // Build self-capture name for detecting self-references
            char async_self_capture_name[128] = {0};
            if (fn->name) {
                snprintf(async_self_capture_name, sizeof(async_self_capture_name), "_js_%.*s",
                    (int)fn->name->len, fn->name->chars);
            }

            // Load captured variables from env
            for (int ci = 0; ci < fc->capture_count; ci++) {
                // Skip MCONST_MODVAR captures only for non-scope-env captures.
                bool has_scope_slot = (fc->captures[ci].scope_env_slot >= 0);
                bool is_self_capture = (async_self_capture_name[0] &&
                    strcmp(fc->captures[ci].name, async_self_capture_name) == 0);
                if (!has_scope_slot && !is_self_capture && mt->module_consts) {
                    JsModuleConstEntry mclookup;
                    snprintf(mclookup.name, sizeof(mclookup.name), "%s", fc->captures[ci].name);
                    JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                    if (mc && mc->const_type == MCONST_MODVAR && !fc->captures[ci].force_env_capture) {
                        continue;
                    }
                }

                MIR_reg_t cap_reg = jm_new_reg(mt, fc->captures[ci].name, MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, cap_reg),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64,
                        (cap_offset + ci) * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1)));
                JsVarScopeEntry entry;
                memset(&entry, 0, sizeof(entry));
                snprintf(entry.name, sizeof(entry.name), "%s", fc->captures[ci].name);
                entry.var.reg = cap_reg;
                entry.var.from_env = true;
                entry.var.env_slot = cap_offset + ci;
                entry.var.env_reg = mt->gen_env_reg;
                entry.var.typed_array_type = -1;
                if (fc->captures[ci].is_let_const) {
                    entry.var.tdz_active = true;
                    entry.var.is_let_const = true;
                    entry.var.is_const = fc->captures[ci].is_const;
                }
                hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
            }

            // Load 'this' from env[this_slot] and register as _js_this variable
            if (gen_this_slot >= 0) {
                MIR_reg_t this_reg = jm_new_reg(mt, "_js_this", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, this_reg),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64,
                        gen_this_slot * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1)));
                JsVarScopeEntry this_entry;
                memset(&this_entry, 0, sizeof(this_entry));
                snprintf(this_entry.name, sizeof(this_entry.name), "_js_this");
                this_entry.var.reg = this_reg;
                this_entry.var.from_env = true;
                this_entry.var.env_slot = gen_this_slot;
                this_entry.var.env_reg = mt->gen_env_reg;
                this_entry.var.typed_array_type = -1;
                hashmap_set(mt->var_scopes[mt->scope_depth], &this_entry);
            }

            // Load 'arguments' from env[args_slot] if this async function uses arguments
            if (gen_args_slot >= 0 && fc->uses_arguments) {
                MIR_reg_t args_reg = jm_new_reg(mt, "_js_arguments", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, args_reg),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64,
                        gen_args_slot * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1)));
                JsVarScopeEntry args_entry;
                memset(&args_entry, 0, sizeof(args_entry));
                snprintf(args_entry.name, sizeof(args_entry.name), "_js_arguments");
                args_entry.var.reg = args_reg;
                args_entry.var.from_env = true;
                args_entry.var.env_slot = gen_args_slot;
                args_entry.var.env_reg = mt->gen_env_reg;
                args_entry.var.typed_array_type = -1;
                hashmap_set(mt->var_scopes[mt->scope_depth], &args_entry);
                jm_activate_arguments_aliasing(mt, fc, fn, args_reg);
            }

            // Hoist var declarations with env slots
            if (fn->body && fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
                int li = 0;
                size_t liter = 0; void* litem;
                while (hashmap_iter(async_locals, &liter, &litem)) {
                    JsNameSetEntry* ns = (JsNameSetEntry*)litem;
                    if (!jm_find_var(mt, ns->name)) {
                        MIR_reg_t vr = jm_new_reg(mt, ns->name, MIR_T_I64);
                        MIR_reg_t initial = 0;
                        JsFunctionNode* direct_fn =
                            jm_function_find_direct_body_function_binding(fn, ns->name);
                        if (direct_fn) {
                            JsFuncCollected* direct_fc = jm_find_collected_func(mt, direct_fn);
                            if (direct_fc && direct_fc->func_item) {
                                initial = jm_create_func_or_closure(mt, direct_fc);
                            }
                        }
                        if (!initial) {
                            initial = jm_emit_null(mt);
                        }
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, vr),
                            MIR_new_reg_op(mt->ctx, initial)));
                        JsVarScopeEntry entry;
                        memset(&entry, 0, sizeof(entry));
                        snprintf(entry.name, sizeof(entry.name), "%s", ns->name);
                        entry.var.reg = vr;
                        entry.var.from_env = true;
                        entry.var.env_slot = local_offset + li;
                        entry.var.env_reg = mt->gen_env_reg;
                        entry.var.typed_array_type = -1;
                        entry.var.from_hoist = true;  // v50: mark as hoisted
                        hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
                        li++;
                    }
                }
            }

            // Allocate shared scope env for child closures inside this async function.
            if (fc->has_scope_env && fc->scope_env_count > 0) {
                int scope_env_alloc_count = fc->scope_env_count;
                if (fc->reuse_parent_env && fc->reuse_env_slot_count > scope_env_alloc_count) {
                    scope_env_alloc_count = fc->reuse_env_slot_count;
                }
                int scope_env_slot = mt->gen_local_slot_count++;
                mt->scope_env_reg = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, scope_env_alloc_count));
                mt->scope_env_slot_count = scope_env_alloc_count;

                // Store scope env pointer in gen_env so it persists across awaits
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64,
                        scope_env_slot * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1),
                    MIR_new_reg_op(mt->ctx, mt->scope_env_reg)));

                // Register as a scoped variable so it gets saved/loaded across awaits
                JsVarScopeEntry senv_entry;
                memset(&senv_entry, 0, sizeof(senv_entry));
                snprintf(senv_entry.name, sizeof(senv_entry.name), "_scope_env");
                senv_entry.var.reg = mt->scope_env_reg;
                senv_entry.var.from_env = true;
                senv_entry.var.env_slot = scope_env_slot;
                senv_entry.var.env_reg = mt->gen_env_reg;
                senv_entry.var.typed_array_type = -1;
                hashmap_set(mt->var_scopes[mt->scope_depth], &senv_entry);

                // Populate scope env with current values and mark vars for write-back
                // Only iterate normal slots — NFE extra slots are populated by self-patch code
                int async_populate_limit = fc->scope_env_normal_count > 0 ? fc->scope_env_normal_count : fc->scope_env_count;
                for (int s = 0; s < async_populate_limit; s++) {
                    const char* sname = fc->scope_env_names[s];
                    int target_slot = s;
                    if (fc->reuse_parent_env) {
                        for (int c = 0; c < fc->capture_count; c++) {
                            if (strcmp(sname, fc->captures[c].name) == 0 &&
                                fc->captures[c].scope_env_slot >= 0) {
                                target_slot = fc->captures[c].scope_env_slot;
                                break;
                            }
                        }
                    }
                    JsMirVarEntry* svar = jm_find_var(mt, sname);
                    MIR_reg_t val;
                    if (svar) {
                        val = svar->reg;
                        if (jm_is_native_type(svar->type_id))
                            val = jm_box_native(mt, svar->reg, svar->type_id);
                        svar->in_scope_env = true;
                        svar->scope_env_slot = target_slot;
                        svar->scope_env_reg = mt->scope_env_reg;
                    } else if (strcmp(sname, "_js_this") == 0) {
                        val = jm_call_0(mt, "js_get_lexical_this_binding", MIR_T_I64);
                        JsVarScopeEntry this_entry;
                        memset(&this_entry, 0, sizeof(this_entry));
                        snprintf(this_entry.name, sizeof(this_entry.name), "_js_this");
                        this_entry.var.reg = val;
                        this_entry.var.mir_type = MIR_T_I64;
                        this_entry.var.type_id = LMD_TYPE_ANY;
                        this_entry.var.in_scope_env = true;
                        this_entry.var.scope_env_slot = target_slot;
                        this_entry.var.scope_env_reg = mt->scope_env_reg;
                        this_entry.var.typed_array_type = -1;
                        hashmap_set(mt->var_scopes[mt->scope_depth], &this_entry);
                    } else if (strcmp(sname, "_js_new.target") == 0) {
                        // Async child arrows still need lexical new.target after
                        // the call frame advances, so persist it in the async scope env.
                        val = jm_call_0(mt, "js_get_new_target", MIR_T_I64);
                        JsVarScopeEntry nt_entry;
                        memset(&nt_entry, 0, sizeof(nt_entry));
                        snprintf(nt_entry.name, sizeof(nt_entry.name), "_js_new.target");
                        nt_entry.var.reg = val;
                        nt_entry.var.mir_type = MIR_T_I64;
                        nt_entry.var.type_id = LMD_TYPE_ANY;
                        nt_entry.var.in_scope_env = true;
                        nt_entry.var.scope_env_slot = target_slot;
                        nt_entry.var.scope_env_reg = mt->scope_env_reg;
                        nt_entry.var.typed_array_type = -1;
                        hashmap_set(mt->var_scopes[mt->scope_depth], &nt_entry);
                    } else if (mt->module_consts) {
                        JsModuleConstEntry mclookup;
                        snprintf(mclookup.name, sizeof(mclookup.name), "%s", sname);
                        JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                        if (mc && mc->const_type == MCONST_MODVAR) {
                            val = jm_call_1(mt, "js_get_module_var", MIR_T_I64,
                                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mc->int_val));
                        } else if (mc && mc->const_type == MCONST_FUNC) {
                            int fi = (int)mc->int_val;
                            if (fi >= 0 && fi < mt->func_count && mt->func_entries[fi].func_item) {
                                val = jm_create_func_or_closure(mt, &mt->func_entries[fi]);
                            } else {
                                val = jm_emit_null(mt);
                            }
                        } else {
                            val = jm_emit_null(mt);
                        }
                    } else {
                        val = jm_emit_null(mt);
                    }
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, target_slot * (int)sizeof(uint64_t), mt->scope_env_reg, 0, 1),
                        MIR_new_reg_op(mt->ctx, val)));
                }
                log_debug("js-mir: async '%s' allocated scope env with %d slots at gen_env[%d]",
                    fc->name, scope_env_alloc_count, scope_env_slot);
            }

            // Push implicit try context for async exception handling
            if (mt->try_ctx_depth < 16) {
                JsTryContext* tc = &mt->try_ctx_stack[mt->try_ctx_depth++];
                tc->catch_label = async_sm_catch_label;
                tc->finally_label = 0;
                tc->end_label = mt->gen_done_label;
                tc->return_val_reg = jm_new_reg(mt, "_asm_ret", MIR_T_I64);
                tc->has_return_reg = jm_new_reg(mt, "_asm_has_ret", MIR_T_I64);
                tc->has_catch = true;
                tc->has_finally = false;
                tc->inlining_finally = false;
                tc->yield_state_only = false;
                tc->finally_body = NULL;
                tc->saved_exc_flag_reg = 0;
                tc->saved_exc_val_reg = 0;
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, tc->return_val_reg),
                    MIR_new_int_op(mt->ctx, 0)));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, tc->has_return_reg),
                    MIR_new_int_op(mt->ctx, 0)));
            }

            // Transpile async body with exception checking after each statement
            if (fn->body && fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
                JsBlockNode* blk = (JsBlockNode*)fn->body;
                JsAstNode* s = blk->statements;
                while (s) {
                    jm_transpile_statement(mt, s);
                    // Check for exception after each statement
                    MIR_reg_t exc_check = jm_call_0(mt, "js_check_exception", MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                        MIR_new_label_op(mt->ctx, async_sm_catch_label),
                        MIR_new_reg_op(mt->ctx, exc_check)));
                    s = s->next;
                }
            } else if (fn->body) {
                // Arrow-body async: evaluate expression, return [result, -1]
                MIR_reg_t val = jm_transpile_box_item(mt, fn->body);
                MIR_reg_t done_result = jm_call_2(mt, "js_gen_yield_result", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)-1));
                jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, done_result)));
            }

            // Pop try context
            if (mt->try_ctx_depth > 0) mt->try_ctx_depth--;

            // Emit any state labels that were not emitted during body transpilation.
            // This happens when jm_count_awaits over-counts (e.g. "await using" counts
            // an await that the transpiler doesn't actually emit a resume point for).
            // Without this, MIR_link crashes on dangling label references.
            for (int si = mt->gen_yield_index + 1; si <= await_count; si++) {
                jm_emit_label(mt, mt->gen_state_labels[si]);
            }

            // Done label: implicit return → [undefined, -1] (fulfilled)
            jm_emit_label(mt, mt->gen_done_label);
            {
                MIR_reg_t undef_val = jm_emit_null(mt);
                MIR_reg_t done_result = jm_call_2(mt, "js_gen_yield_result", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, undef_val),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)-1));
                jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, done_result)));
            }

            // Catch label: exception → [error, -2] (rejected)
            jm_emit_label(mt, async_sm_catch_label);
            {
                MIR_reg_t error = jm_call_0(mt, "js_clear_exception", MIR_T_I64);
                MIR_reg_t reject_result = jm_call_2(mt, "js_gen_yield_result", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, error),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)-2));
                jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, reject_result)));
            }

            jm_pop_scope(mt);
            MIR_finish_func(mt->ctx);

            // Restore transpiler state
            mt->current_func_item = saved_item_sm;
            mt->current_func = saved_func_sm;
            mt->scope_depth = saved_scope_depth_sm;
            mt->loop_depth = saved_loop_depth_sm;
            mt->in_native_func = saved_in_native_sm;
            mt->in_main = saved_in_main_sm;
            mt->current_fc = saved_fc_sm;
            mt->current_class = saved_class_sm;
            mt->scope_env_reg = saved_scope_env_reg_sm;
            mt->scope_env_slot_count = saved_scope_env_slot_sm;
            mt->current_func_index = saved_func_index_sm;
            mt->func_except_label = saved_except_label_sm;
            mt->in_generator = saved_in_generator;
            mt->in_async = saved_in_async;

            hashmap_free(async_locals);

            log_debug("js-mir P6: generated async state machine %s (awaits: %d, env slots: %d)",
                sm_name, await_count, gen_env_total_slots);
        }
    }

    // --- Generate boxed version (original or wrapper) ---
    int total_params = param_count + (has_captures ? 1 : 0);
    MIR_var_t* params = (MIR_var_t*)alloca(total_params * sizeof(MIR_var_t));
    char** param_names_arr = (char**)alloca(total_params * sizeof(char*));

    int pi = 0;
    if (has_captures) {
        param_names_arr[pi] = (char*)alloca(128);
        snprintf(param_names_arr[pi], 128, "_js_env");
        params[pi] = {MIR_T_I64, param_names_arr[pi], 0};
        pi++;
    }

    JsAstNode* param_node = fn->params;
    for (int i = 0; i < param_count; i++) {
        param_names_arr[pi] = (char*)alloca(128);
        jm_get_param_name(param_node, i, param_names_arr[pi], 128);
        params[pi] = {MIR_T_I64, param_names_arr[pi], 0};
        param_node = param_node ? param_node->next : NULL;
        pi++;
    }

    // Handle duplicate parameter names (valid in non-strict JS): rename earlier
    // occurrences so MIR gets unique register names. Last parameter wins per spec.
    {
        int env_offset = has_captures ? 1 : 0;
        for (int i = env_offset; i < pi; i++) {
            for (int j = i + 1; j < pi; j++) {
                if (strcmp(param_names_arr[i], param_names_arr[j]) == 0) {
                    // Rename earlier duplicate — it's shadowed by the later one
                    char* renamed = (char*)alloca(128);
                    snprintf(renamed, 128, "%s__dup%d", param_names_arr[i], i);
                    param_names_arr[i] = renamed;
                    params[i].name = renamed;
                    break; // only need to rename once per earlier occurrence
                }
            }
        }
    }

    MIR_type_t ret_type = MIR_T_I64;
    MIR_item_t func_item = MIR_new_func_arr(mt->ctx, fc->name, 1, &ret_type, total_params, params);
    MIR_func_t func = MIR_get_item_func(mt->ctx, func_item);

    jm_register_local_func(mt, fc->name, func_item);

    // Save transpiler state
    MIR_item_t saved_item = mt->current_func_item;
    MIR_func_t saved_func = mt->current_func;
    int saved_scope_depth = mt->scope_depth;
    int saved_loop_depth = mt->loop_depth;
    bool saved_in_native = mt->in_native_func;
    bool saved_in_main = mt->in_main;
    JsFuncCollected* saved_fc = mt->current_fc;
    JsClassEntry* saved_class = mt->current_class;
    MIR_reg_t saved_scope_env_reg = mt->scope_env_reg;
    int saved_scope_env_slot_count = mt->scope_env_slot_count;
    int saved_func_index = mt->current_func_index;
    MIR_reg_t saved_eval_completion_reg = mt->eval_completion_reg;
    MIR_reg_t saved_eval_local_frame_reg = mt->eval_local_frame_reg;

    // Set current function index for scope env lookups
    mt->current_func_index = (int)(fc - mt->func_entries);
    mt->scope_env_reg = 0;
    mt->scope_env_slot_count = 0;
    mt->eval_completion_reg = 0;  // disable completion tracking in function bodies
    mt->eval_local_frame_reg = 0;
    mt->last_closure_has_env = false;  // clear stale closure env from previous function

    // Determine if this function is a class method and set current_class
    mt->current_class = NULL;
    for (int ci = 0; ci < mt->class_count; ci++) {
        JsClassEntry* ce = &mt->class_entries[ci];
        for (int mi = 0; mi < ce->method_count; mi++) {
            if (ce->methods[mi].fc == fc) {
                mt->current_class = ce;
                goto found_class;
            }
        }
    }
    found_class:

    if (jm_has_use_strict_directive(fn)) {
        fc->is_strict = true;
    }

    mt->current_func_item = func_item;
    mt->current_func = func;
    mt->loop_depth = 0;
    mt->for_of_depth = 0;
    mt->pending_label_name = NULL;
    mt->pending_label_len = 0;
    mt->in_native_func = false;
    mt->in_main = false;
    mt->current_fc = fc;
    mt->func_except_label = 0;  // reset for this function

    jm_push_scope(mt);
    mt->eval_local_frame_reg = jm_new_reg(mt, "eval_local_frame", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, mt->eval_local_frame_reg),
        MIR_new_int_op(mt->ctx, 0)));

    {
        struct hashmap* dstr_param_names = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        JsAstNode* dstr_param = fn->params;
        while (dstr_param) {
            JsAstNode* pat = dstr_param;
            if (pat->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN)
                pat = ((JsAssignmentPatternNode*)pat)->left;
            if (pat->node_type == JS_AST_NODE_REST_ELEMENT ||
                pat->node_type == JS_AST_NODE_SPREAD_ELEMENT)
                pat = ((JsSpreadElementNode*)pat)->argument;
            if (pat->node_type == JS_AST_NODE_OBJECT_PATTERN ||
                pat->node_type == JS_AST_NODE_ARRAY_PATTERN) {
                jm_collect_pattern_names(pat, dstr_param_names);
            }
            dstr_param = dstr_param->next;
        }
        size_t dstr_iter = 0; void* dstr_item;
        while (hashmap_iter(dstr_param_names, &dstr_iter, &dstr_item)) {
            JsNameSetEntry* ns = (JsNameSetEntry*)dstr_item;
            MIR_reg_t local_reg = jm_new_reg(mt, ns->name, MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, local_reg),
                MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED)));
            JsVarScopeEntry entry;
            memset(&entry, 0, sizeof(entry));
            snprintf(entry.name, sizeof(entry.name), "%s", ns->name);
            entry.var.reg = local_reg;
            entry.var.mir_type = MIR_T_I64;
            entry.var.type_id = LMD_TYPE_ANY;
            entry.var.typed_array_type = -1;
            hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
        }
        hashmap_free(dstr_param_names);
    }

    // If we have a native version, the boxed version becomes a thin wrapper:
    // unbox params → call native → box result
    if (generate_native) {
        // Build native call: result = native_func(unboxed_p1, unboxed_p2, ...)
        char proto_name[160];
        snprintf(proto_name, sizeof(proto_name), "%s_n_wp%d", fc->name, mt->label_counter++);

        MIR_var_t* np_args = (MIR_var_t*)alloca(param_count * sizeof(MIR_var_t));
        for (int i = 0; i < param_count; i++) {
            MIR_type_t mtype = (fc->param_types[i] == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
            np_args[i] = {mtype, "a", 0};
        }
        MIR_type_t native_ret_type = (fc->return_type == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
        MIR_item_t proto = MIR_new_proto_arr(mt->ctx, proto_name, 1, &native_ret_type, param_count, np_args);

        int nops = 3 + param_count;
        MIR_op_t* ops = (MIR_op_t*)alloca(nops * sizeof(MIR_op_t));
        int oi = 0;
        ops[oi++] = MIR_new_ref_op(mt->ctx, proto);
        ops[oi++] = MIR_new_ref_op(mt->ctx, fc->native_func_item);
        MIR_reg_t native_result = jm_new_reg(mt, "nret", native_ret_type);
        ops[oi++] = MIR_new_reg_op(mt->ctx, native_result);

        // Unbox each parameter (with default-param handling for native wrapper)
        param_node = fn->params;
        for (int i = 0; i < param_count; i++) {
            char vname[128];
            jm_get_param_name(param_node, i, vname, sizeof(vname));
            MIR_reg_t preg = MIR_reg(mt->ctx, vname, func);

            // Handle default parameters: if preg == ITEM_JS_UNDEFINED, apply default
            if (param_node && param_node->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
                JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)param_node;
                if (ap->right) {
                    MIR_label_t skip_label = jm_new_label(mt);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BNE,
                        MIR_new_label_op(mt->ctx, skip_label),
                        MIR_new_reg_op(mt->ctx, preg),
                        MIR_new_uint_op(mt->ctx, (uint64_t)ITEM_JS_UNDEFINED)));
                    MIR_reg_t def_val = jm_transpile_default_param_value(mt, fn, ap->right);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, preg),
                        MIR_new_reg_op(mt->ctx, def_val)));
                    jm_emit_label(mt, skip_label);
                }
            }

            if (fc->param_types[i] == LMD_TYPE_FLOAT) {
                MIR_reg_t unboxed = jm_emit_unbox_float(mt, preg);
                ops[oi++] = MIR_new_reg_op(mt->ctx, unboxed);
            } else {
                // Use it2i (runtime type-checking unbox) instead of jm_emit_unbox_int
                // because callers may pass FLOAT Items for INT-typed params (e.g. 36e5).
                // it2i handles INT, INT64, FLOAT → int64_t conversion correctly.
                MIR_reg_t unboxed = jm_call_1(mt, JS_PROFILED_IT2I_NAME, MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, preg));
                ops[oi++] = MIR_new_reg_op(mt->ctx, unboxed);
            }
            param_node = param_node ? param_node->next : NULL;
        }

        jm_emit(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, nops, ops));

        // Box the result and return
        MIR_reg_t boxed_result = jm_box_native(mt, native_result, fc->return_type);
        jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, boxed_result)));

        goto finish_boxed;
    }

    // --- v15: Generator wrapper (creates generator object instead of running body) ---
    if (fn->is_generator && gen_sm_func_item) {
        bool gen_has_default_params = jm_param_tree_has_assignment_pattern(fn->params);
        if (gen_has_default_params) {
            JsAstNode* seed_param = fn->params;
            for (int pi = 0; pi < param_count; pi++) {
                if (seed_param) {
                    JsAstNode* seed_binding = seed_param;
                    if (seed_binding->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN)
                        seed_binding = ((JsAssignmentPatternNode*)seed_binding)->left;
                    if (seed_binding && seed_binding->node_type == JS_AST_NODE_IDENTIFIER) {
                        char seed_vname[128];
                        jm_get_param_name(seed_param, pi, seed_vname, sizeof(seed_vname));
                        MIR_reg_t seed_reg = jm_new_reg(mt, seed_vname, MIR_T_I64);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, seed_reg),
                            MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_TDZ)));
                        jm_set_var(mt, seed_vname, seed_reg);
                        JsMirVarEntry* seed_var = jm_find_var(mt, seed_vname);
                        if (seed_var) seed_var->tdz_active = true;
                    }
                }
                seed_param = seed_param ? seed_param->next : NULL;
            }
            if (fc->uses_arguments) {
                jm_call_void_1(mt, "js_set_arguments_info",
                    MIR_T_I64, MIR_new_int_op(mt->ctx, 1));
                MIR_reg_t args_obj = jm_call_0(mt, "js_build_arguments_object", MIR_T_I64);
                jm_set_var(mt, "_js_arguments", args_obj);
                jm_scope_env_mark_and_writeback(mt, "_js_arguments", args_obj);
            }

            if (has_captures) {
                MIR_reg_t wrapper_env = MIR_reg(mt->ctx, "_js_env", func);
                char wrapper_self_capture_name[128] = {0};
                if (fn->name) {
                    snprintf(wrapper_self_capture_name, sizeof(wrapper_self_capture_name), "_js_%.*s",
                        (int)fn->name->len, fn->name->chars);
                }
                for (int ci = 0; ci < fc->capture_count; ci++) {
                    int src_slot = fc->captures[ci].scope_env_slot >= 0 ? fc->captures[ci].scope_env_slot : ci;
                    MIR_reg_t cap_reg = jm_new_reg(mt, fc->captures[ci].name, MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, cap_reg),
                        MIR_new_mem_op(mt->ctx, MIR_T_I64,
                            src_slot * (int)sizeof(uint64_t), wrapper_env, 0, 1)));
                    JsVarScopeEntry cap_entry;
                    memset(&cap_entry, 0, sizeof(cap_entry));
                    snprintf(cap_entry.name, sizeof(cap_entry.name), "%s", fc->captures[ci].name);
                    cap_entry.var.reg = cap_reg;
                    cap_entry.var.from_env = true;
                    cap_entry.var.env_slot = src_slot;
                    cap_entry.var.env_reg = wrapper_env;
                    cap_entry.var.typed_array_type = -1;
                    cap_entry.var.is_nfe_binding = fc->captures[ci].is_nfe_binding ||
                        (wrapper_self_capture_name[0] &&
                         strcmp(fc->captures[ci].name, wrapper_self_capture_name) == 0);
                    if (fc->captures[ci].is_let_const) {
                        cap_entry.var.tdz_active = true;
                        cap_entry.var.is_let_const = true;
                        cap_entry.var.is_const = fc->captures[ci].is_const;
                    }
                    // Generator wrappers evaluate default parameters before creating
                    // the state-machine env, so captured names must already resolve here.
                    hashmap_set(mt->var_scopes[mt->scope_depth], &cap_entry);
                }
            }

            JsAstNode* gen_param = fn->params;
            for (int pi = 0; pi < param_count; pi++) {
                if (gen_param) {
                    char gen_vname[128];
                    jm_get_param_name(gen_param, pi, gen_vname, sizeof(gen_vname));
                    MIR_reg_t preg = MIR_reg(mt->ctx, gen_vname, func);
                    JsAstNode* param_binding = gen_param;
                    if (param_binding->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN)
                        param_binding = ((JsAssignmentPatternNode*)param_binding)->left;
                    if (gen_param->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
                        JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)gen_param;
                        if (ap->right) {
                            MIR_label_t skip_label = jm_new_label(mt);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BNE,
                                MIR_new_label_op(mt->ctx, skip_label),
                                MIR_new_reg_op(mt->ctx, preg),
                                MIR_new_uint_op(mt->ctx, (uint64_t)ITEM_JS_UNDEFINED)));
                            MIR_reg_t def_val = jm_transpile_default_param_value(mt, fn, ap->right);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, preg),
                                MIR_new_reg_op(mt->ctx, def_val)));
                            jm_emit_label(mt, skip_label);
                        }
                    }
                    if (param_binding && param_binding->node_type == JS_AST_NODE_IDENTIFIER) {
                        JsMirVarEntry* pvar = jm_find_var(mt, gen_vname);
                        if (pvar) {
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, pvar->reg),
                                MIR_new_reg_op(mt->ctx, preg)));
                            pvar->tdz_active = false;
                        }
                    }
                }
                gen_param = gen_param ? gen_param->next : NULL;
            }
        }
        // Allocate env array for the generator's state machine
        MIR_reg_t gen_env = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, gen_env_total_slots));
        if (gen_active_iterator_slot >= 0) {
            MIR_reg_t null_iter = jm_emit_null(mt);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64, gen_active_iterator_slot * (int)sizeof(uint64_t), gen_env, 0, 1),
                MIR_new_reg_op(mt->ctx, null_iter)));
        }

        // Store captured variables into env[0..capture_count-1]
        if (has_captures) {
            MIR_reg_t outer_env = MIR_reg(mt->ctx, "_js_env", func);
            for (int ci = 0; ci < fc->capture_count; ci++) {
                int src_slot = fc->captures[ci].scope_env_slot >= 0 ? fc->captures[ci].scope_env_slot : ci;
                MIR_reg_t cap_val = jm_new_reg(mt, "gcap", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, cap_val),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, src_slot * (int)sizeof(uint64_t), outer_env, 0, 1)));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, ci * (int)sizeof(uint64_t), gen_env, 0, 1),
                    MIR_new_reg_op(mt->ctx, cap_val)));
            }
        }

        // Store parameters into env[capture_count..capture_count+param_count-1]
        JsAstNode* gp_node = fn->params;
        for (int i = 0; i < param_count; i++) {
            char vname[128];
            jm_get_param_name(gp_node, i, vname, sizeof(vname));
            MIR_reg_t preg = MIR_reg(mt->ctx, vname, func);
            int env_slot = fc->capture_count + i;
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64, env_slot * (int)sizeof(uint64_t), gen_env, 0, 1),
                MIR_new_reg_op(mt->ctx, preg)));
            gp_node = gp_node ? gp_node->next : NULL;
        }

        // ES spec: FunctionDeclarationInstantiation — parameter binding must happen
        // at call time, not lazily on .next(). Eagerly validate destructured params
        // so TypeError is thrown from the call site, not from .next().
        {
            JsAstNode* ep_node = fn->params;
            for (int i = 0; i < param_count; i++) {
                if (ep_node) {
                    JsAstNode* pat = ep_node;
                    // unwrap assignment pattern to get the destructuring target
                    if (pat->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
                        // for default params like ({a} = {}) — only validate when
                        // the raw param is NOT undefined (otherwise default applies)
                        JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)pat;
                        pat = ap->left;
                        if (pat->node_type == JS_AST_NODE_OBJECT_PATTERN ||
                            pat->node_type == JS_AST_NODE_ARRAY_PATTERN) {
                            char vn[128];
                            jm_get_param_name(ep_node, i, vn, sizeof(vn));
                            MIR_reg_t preg = MIR_reg(mt->ctx, vn, func);
                            MIR_label_t skip = jm_new_label(mt);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BEQ,
                                MIR_new_label_op(mt->ctx, skip),
                                MIR_new_reg_op(mt->ctx, preg),
                                MIR_new_uint_op(mt->ctx, (uint64_t)ITEM_JS_UNDEFINED)));
                            if (pat->node_type == JS_AST_NODE_OBJECT_PATTERN) {
                                jm_call_void_1(mt, "js_require_object_coercible",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, preg));
                            } else {
                                // array pattern: null/undefined not iterable
                                jm_call_void_1(mt, "js_require_object_coercible",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, preg));
                            }
                            jm_emit_label(mt, skip);
                        }
                    } else if (pat->node_type == JS_AST_NODE_OBJECT_PATTERN) {
                        char vn[128];
                        jm_get_param_name(ep_node, i, vn, sizeof(vn));
                        MIR_reg_t preg = MIR_reg(mt->ctx, vn, func);
                        jm_call_void_1(mt, "js_require_object_coercible",
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, preg));
                    } else if (pat->node_type == JS_AST_NODE_ARRAY_PATTERN) {
                        char vn[128];
                        jm_get_param_name(ep_node, i, vn, sizeof(vn));
                        MIR_reg_t preg = MIR_reg(mt->ctx, vn, func);
                        // array pattern: null/undefined not iterable
                        jm_call_void_1(mt, "js_require_object_coercible",
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, preg));
                    }
                    ep_node = ep_node->next;
                }
            }
        }

        // Store 'this' into env[this_slot] so the state machine can access it
        if (gen_this_slot >= 0) {
            MIR_reg_t this_val = jm_call_0(mt, "js_get_this", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64, gen_this_slot * (int)sizeof(uint64_t), gen_env, 0, 1),
                MIR_new_reg_op(mt->ctx, this_val)));
        }

        // Build 'arguments' object and store into env[args_slot] (js_pending_call_args still valid here)
        if (gen_args_slot >= 0 && fc->uses_arguments) {
            bool args_aliased = !fc->has_non_simple_params &&
                                !mt->is_module &&
                                !mt->is_global_strict &&
                                !fc->is_strict &&
                                !jm_has_use_strict_directive(fn);
            jm_call_void_1(mt, "js_set_arguments_info",
                MIR_T_I64, MIR_new_int_op(mt->ctx, args_aliased ? 0 : 1));
            MIR_reg_t args_obj = jm_call_0(mt, "js_build_arguments_object", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64, gen_args_slot * (int)sizeof(uint64_t), gen_env, 0, 1),
                MIR_new_reg_op(mt->ctx, args_obj)));
        }

        // Call js_generator_create(state_machine_fn_ptr, env, env_size, is_async)
        MIR_reg_t sm_fn_ptr = jm_new_reg(mt, "smfn", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, sm_fn_ptr),
            MIR_new_ref_op(mt->ctx, gen_sm_func_item)));
        MIR_reg_t gen_obj = jm_call_4(mt, "js_generator_create", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, sm_fn_ptr),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, gen_env),
            MIR_T_I64, MIR_new_int_op(mt->ctx, gen_env_total_slots),
            MIR_T_I64, MIR_new_int_op(mt->ctx, fn->is_async ? 1 : 0));

        jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, gen_obj)));
        if (mt->func_except_label != 0) {
            jm_emit_label(mt, mt->func_except_label);
            MIR_reg_t exc_ret = jm_emit_null(mt);
            jm_emit_eval_local_pop_if_needed(mt);
            jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, exc_ret)));
        }
        goto finish_boxed;
    }

    // --- Phase 6: Async wrapper (creates async context + promise instead of running body) ---
    if (fn->is_async && gen_sm_func_item) {
        // Allocate env array for the async state machine
        MIR_reg_t async_env = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, gen_env_total_slots));
        if (gen_active_iterator_slot >= 0) {
            MIR_reg_t null_iter = jm_emit_null(mt);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64, gen_active_iterator_slot * (int)sizeof(uint64_t), async_env, 0, 1),
                MIR_new_reg_op(mt->ctx, null_iter)));
        }

        // Store captured variables into env[0..capture_count-1]
        if (has_captures) {
            MIR_reg_t outer_env = MIR_reg(mt->ctx, "_js_env", func);
            for (int ci = 0; ci < fc->capture_count; ci++) {
                int src_slot = fc->captures[ci].scope_env_slot >= 0 ? fc->captures[ci].scope_env_slot : ci;
                MIR_reg_t cap_val = jm_new_reg(mt, "acap", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, cap_val),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, src_slot * (int)sizeof(uint64_t), outer_env, 0, 1)));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, ci * (int)sizeof(uint64_t), async_env, 0, 1),
                    MIR_new_reg_op(mt->ctx, cap_val)));
            }
        }

        // Store parameters into env[capture_count..capture_count+param_count-1]
        JsAstNode* ap_node = fn->params;
        for (int i = 0; i < param_count; i++) {
            char vname[128];
            jm_get_param_name(ap_node, i, vname, sizeof(vname));
            MIR_reg_t preg = MIR_reg(mt->ctx, vname, func);
            int env_slot = fc->capture_count + i;
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64, env_slot * (int)sizeof(uint64_t), async_env, 0, 1),
                MIR_new_reg_op(mt->ctx, preg)));
            ap_node = ap_node ? ap_node->next : NULL;
        }

        // ES spec: Eagerly validate destructured params for async generators
        // so TypeError is thrown at call time, not on first .next()
        if (fn->is_generator) {
            JsAstNode* ep_node = fn->params;
            for (int i = 0; i < param_count; i++) {
                if (ep_node) {
                    JsAstNode* pat = ep_node;
                    if (pat->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
                        JsAssignmentPatternNode* ap2 = (JsAssignmentPatternNode*)pat;
                        pat = ap2->left;
                        if (pat->node_type == JS_AST_NODE_OBJECT_PATTERN ||
                            pat->node_type == JS_AST_NODE_ARRAY_PATTERN) {
                            char vn[128];
                            jm_get_param_name(ep_node, i, vn, sizeof(vn));
                            MIR_reg_t preg = MIR_reg(mt->ctx, vn, func);
                            MIR_label_t skip = jm_new_label(mt);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BEQ,
                                MIR_new_label_op(mt->ctx, skip),
                                MIR_new_reg_op(mt->ctx, preg),
                                MIR_new_uint_op(mt->ctx, (uint64_t)ITEM_JS_UNDEFINED)));
                            jm_call_void_1(mt, "js_require_object_coercible",
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, preg));
                            jm_emit_label(mt, skip);
                        }
                    } else if (pat->node_type == JS_AST_NODE_OBJECT_PATTERN ||
                               pat->node_type == JS_AST_NODE_ARRAY_PATTERN) {
                        char vn[128];
                        jm_get_param_name(ep_node, i, vn, sizeof(vn));
                        MIR_reg_t preg = MIR_reg(mt->ctx, vn, func);
                        jm_call_void_1(mt, "js_require_object_coercible",
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, preg));
                    }
                    ep_node = ep_node->next;
                }
            }
        }

        // Store 'this' into env[this_slot] so the async state machine can access it
        if (gen_this_slot >= 0) {
            MIR_reg_t this_val = jm_call_0(mt, "js_get_this", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64, gen_this_slot * (int)sizeof(uint64_t), async_env, 0, 1),
                MIR_new_reg_op(mt->ctx, this_val)));
        }

        // Build 'arguments' object and store into env[args_slot] (js_pending_call_args still valid here)
        if (gen_args_slot >= 0 && fc->uses_arguments) {
            bool args_aliased = !fc->has_non_simple_params &&
                                !mt->is_module &&
                                !mt->is_global_strict &&
                                !fc->is_strict &&
                                !jm_has_use_strict_directive(fn);
            jm_call_void_1(mt, "js_set_arguments_info",
                MIR_T_I64, MIR_new_int_op(mt->ctx, args_aliased ? 0 : 1));
            MIR_reg_t args_obj = jm_call_0(mt, "js_build_arguments_object", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64, gen_args_slot * (int)sizeof(uint64_t), async_env, 0, 1),
                MIR_new_reg_op(mt->ctx, args_obj)));
        }

        // Create async context: js_async_context_create(sm_fn_ptr, env, env_size, this) → ctx_idx
        MIR_reg_t sm_fn_ptr = jm_new_reg(mt, "asmfn", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, sm_fn_ptr),
            MIR_new_ref_op(mt->ctx, gen_sm_func_item)));
        MIR_reg_t async_this_val = jm_call_0(mt, "js_get_this", MIR_T_I64);
        MIR_reg_t ctx_idx = jm_call_4(mt, "js_async_context_create", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, sm_fn_ptr),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, async_env),
            MIR_T_I64, MIR_new_int_op(mt->ctx, gen_env_total_slots),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, async_this_val));

        // Start execution (runs synchronously until suspend or completion)
        jm_call_1(mt, "js_async_start", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, ctx_idx));

        // Return the async function's promise
        MIR_reg_t promise = jm_call_1(mt, "js_async_get_promise", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, ctx_idx));
        jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, promise)));
        goto finish_boxed;
    }

    // --- Full boxed version (no native available) ---

    // For closures: get env register and load captured variables from env slots
    {
        MIR_reg_t env_reg = 0;
        // Build self-capture name for detecting self-references
        char self_capture_name[128] = {0};
        if (fn->name) {
            snprintf(self_capture_name, sizeof(self_capture_name), "_js_%.*s",
                (int)fn->name->len, fn->name->chars);
        }
            if (has_captures) {
            env_reg = MIR_reg(mt->ctx, "_js_env", func);
            for (int i = 0; i < fc->capture_count; i++) {
                // Skip captures that are MCONST_MODVAR (IIFE-promoted vars):
                // For PER-CLOSURE envs (scope_env_slot < 0), the env holds stale
                // snapshots, so module vars should be read live via js_get_module_var.
                // For SHARED scope_envs (scope_env_slot >= 0), the env is live
                // (updated by jm_scope_env_mark_and_writeback on assignment), so
                // always load from env — the module var table may be stale or wrong
                // for IIFE-internal function declarations.
                bool has_scope_slot = (fc->captures[i].scope_env_slot >= 0);
                bool is_self_capture = (self_capture_name[0] &&
                    strcmp(fc->captures[i].name, self_capture_name) == 0);
                if (!has_scope_slot && !is_self_capture) {
                    // When parent uses shared scope env but this capture didn't get a
                    // slot (scope_env overflow), skip loading — the dense index 'i'
                    // would read the wrong slot. Let identifier resolution handle it
                    // (via id->entry for function decls, or module_consts for MODVARs).
                    int pi = fc->parent_index;
                    if (pi >= 0 && pi < mt->func_count && mt->func_entries[pi].has_scope_env) {
                        continue;
                    }
                    // For per-closure envs, also skip MODVAR captures
                    if (mt->module_consts) {
                        JsModuleConstEntry mclookup;
                        snprintf(mclookup.name, sizeof(mclookup.name), "%s", fc->captures[i].name);
                        JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                        if (mc && mc->const_type == MCONST_MODVAR && !fc->captures[i].force_env_capture) {
                            continue;  // read via js_get_module_var at use site
                        }
                    }
                }

                // Use scope_env_slot if remapped, otherwise use dense index
                int slot = fc->captures[i].scope_env_slot >= 0 ? fc->captures[i].scope_env_slot : i;
                MIR_reg_t cap_reg = jm_new_reg(mt, fc->captures[i].name, MIR_T_I64);

                // v29: For transitive captures with grandparent_slot, read through the
                // parent env link (stored in last slot of env) to get the live grandparent value.
                int gp_slot = fc->captures[i].grandparent_slot;
                if (gp_slot >= 0) {
                    // Find the parent env link slot (last slot in parent's scope env)
                    int pi = fc->parent_index;
                    int parent_env_link_slot = -1;
                    if (pi >= 0 && pi < mt->func_count && mt->func_entries[pi].has_parent_env_link) {
                        parent_env_link_slot = mt->func_entries[pi].scope_env_count - 1;
                    }
                    if (parent_env_link_slot >= 0) {
                        // Load parent env pointer from env[last_slot]
                        MIR_reg_t parent_env = jm_new_reg(mt, "gp_env", MIR_T_I64);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, parent_env),
                            MIR_new_mem_op(mt->ctx, MIR_T_I64, parent_env_link_slot * (int)sizeof(uint64_t), env_reg, 0, 1)));
                        // Guard: if parent env link is null (0), fall back to env[slot] directly.
                        // This can happen when the closure uses a per-closure env that doesn't
                        // have the parent env link populated, or when scope env was not yet set up.
                        MIR_label_t gp_ok = jm_new_label(mt);
                        MIR_label_t gp_done = jm_new_label(mt);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BNE,
                            MIR_new_label_op(mt->ctx, gp_ok),
                            MIR_new_reg_op(mt->ctx, parent_env),
                            MIR_new_int_op(mt->ctx, 0)));
                        // Null: fall back to reading from env[slot]
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, cap_reg),
                            MIR_new_mem_op(mt->ctx, MIR_T_I64, slot * (int)sizeof(uint64_t), env_reg, 0, 1)));
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                            MIR_new_label_op(mt->ctx, gp_done)));
                        // Ok: read variable from grandparent env at grandparent_slot
                        jm_emit_label(mt, gp_ok);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, cap_reg),
                            MIR_new_mem_op(mt->ctx, MIR_T_I64, gp_slot * (int)sizeof(uint64_t), parent_env, 0, 1)));
                        jm_emit_label(mt, gp_done);
                    } else {
                        // Fallback: read from env directly
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, cap_reg),
                            MIR_new_mem_op(mt->ctx, MIR_T_I64, slot * (int)sizeof(uint64_t), env_reg, 0, 1)));
                    }
                } else {
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, cap_reg),
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, slot * (int)sizeof(uint64_t), env_reg, 0, 1)));
                }
                JsVarScopeEntry entry;
                memset(&entry, 0, sizeof(entry));
                snprintf(entry.name, sizeof(entry.name), "%s", fc->captures[i].name);
                entry.var.reg = cap_reg;
                entry.var.from_env = true;
                entry.var.from_shared_env = (has_scope_slot && gp_slot < 0);
                // v29: For grandparent reads, store the grandparent env info
                // so scope_env_reload_vars and write-back use the correct env
                if (gp_slot >= 0) {
                    int pi2 = fc->parent_index;
                    int pel_slot = -1;
                    if (pi2 >= 0 && pi2 < mt->func_count && mt->func_entries[pi2].has_parent_env_link)
                        pel_slot = mt->func_entries[pi2].scope_env_count - 1;
                    if (pel_slot >= 0) {
                        // Load grandparent env reg for this var, with null guard.
                        // If parent env link is null, coalesce to env_reg for write-back safety.
                        MIR_reg_t gp_env_reg = jm_new_reg(mt, "gp_envr", MIR_T_I64);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, gp_env_reg),
                            MIR_new_mem_op(mt->ctx, MIR_T_I64, pel_slot * (int)sizeof(uint64_t), env_reg, 0, 1)));
                        MIR_label_t wb_ok = jm_new_label(mt);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BNE,
                            MIR_new_label_op(mt->ctx, wb_ok),
                            MIR_new_reg_op(mt->ctx, gp_env_reg),
                            MIR_new_int_op(mt->ctx, 0)));
                        // Null: coalesce to env_reg so writes don't crash
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, gp_env_reg),
                            MIR_new_reg_op(mt->ctx, env_reg)));
                        jm_emit_label(mt, wb_ok);
                        entry.var.env_slot = gp_slot;
                        entry.var.env_reg = gp_env_reg;
                    } else {
                        entry.var.env_slot = slot;
                        entry.var.env_reg = env_reg;
                    }
                } else {
                    entry.var.env_slot = slot;
                    entry.var.env_reg = env_reg;
                }
                entry.var.typed_array_type = -1;  // not a typed array by default
                entry.var.is_nfe_binding = fc->captures[i].is_nfe_binding ||
                    jm_capture_is_nfe_binding(mt, fc, fc->captures[i].name);
                // v29 TDZ: Mark captured let/const variables so js_check_tdz is
                // emitted when reading them. The TDZ sentinel may be in the env
                // if the variable hasn't been initialized yet.
                if (fc->captures[i].is_let_const) {
                    entry.var.tdz_active = true;
                    entry.var.is_let_const = true;
                    entry.var.is_const = fc->captures[i].is_const;
                }
                hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
            }
        }

        bool has_default_params = jm_param_tree_has_assignment_pattern(fn->params);
        bool has_formal_arguments_binding = jm_function_has_formal_arguments_binding(fn);

        bool arguments_object_materialized = false;
        if (has_default_params && fc->uses_arguments && !has_formal_arguments_binding) {
            bool args_aliased = false;
            jm_call_void_1(mt, "js_set_arguments_info",
                MIR_T_I64, MIR_new_int_op(mt->ctx, args_aliased ? 0 : 1));
            MIR_reg_t args_arr = jm_call_0(mt, "js_build_arguments_object", MIR_T_I64);
            jm_set_var(mt, "_js_arguments", args_arr);
            jm_scope_env_mark_and_writeback(mt, "_js_arguments", args_arr);
            arguments_object_materialized = true;
            mt->arguments_reg = args_arr;
            mt->arguments_param_count = 0;
        }

        if (has_default_params) {
            JsAstNode* seed_param = fn->params;
            for (int pi = 0; pi < param_count; pi++) {
                if (seed_param) {
                    JsAstNode* seed_binding = seed_param;
                    if (seed_binding->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN)
                        seed_binding = ((JsAssignmentPatternNode*)seed_binding)->left;
                    if (seed_binding && seed_binding->node_type == JS_AST_NODE_IDENTIFIER) {
                        char seed_vname[128];
                        jm_get_param_name(seed_param, pi, seed_vname, sizeof(seed_vname));
                        MIR_reg_t seed_reg = jm_new_reg(mt, seed_vname, MIR_T_I64);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, seed_reg),
                            MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_TDZ)));
                        jm_set_var(mt, seed_vname, seed_reg);
                        JsMirVarEntry* seed_var = jm_find_var(mt, seed_vname);
                        if (seed_var) seed_var->tdz_active = true;
                    }
                }
                seed_param = seed_param ? seed_param->next : NULL;
            }
        }

        // Register parameter variables
        param_node = fn->params;
        for (int i = 0; i < param_count; i++) {
            if (param_node) {
                char vname[128];
                jm_get_param_name(param_node, i, vname, sizeof(vname));
                MIR_reg_t preg = MIR_reg(mt->ctx, vname, func);
                bool is_default_param = (param_node->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN);
                JsAstNode* param_binding = param_node;
                if (is_default_param) param_binding = ((JsAssignmentPatternNode*)param_node)->left;
                if (!(has_default_params && param_binding && param_binding->node_type == JS_AST_NODE_IDENTIFIER)) {
                jm_set_var(mt, vname, preg);
                }

                // Phase 3.4: if param has a TypeMap TS annotation, set full_type so member
                // access on this param can resolve field types via jm_get_effective_type.
                if (param_node->node_type == (int)TS_AST_NODE_PARAMETER && mt->tp) {
                    TsParameterNode* tsp = (TsParameterNode*)param_node;
                    if (tsp->ts_type && tsp->ts_type->type_expr &&
                        tsp->ts_type->type_expr->base.node_type != (int)TS_AST_NODE_PREDEFINED_TYPE) {
                        Type* resolved = ts_resolve_type((TsTranspiler*)mt->tp, tsp->ts_type->type_expr);
                        if (resolved && resolved->type_id == LMD_TYPE_MAP) {
                            JsMirVarEntry* pvar = jm_find_var(mt, vname);
                            if (pvar) {
                                pvar->full_type = resolved;
                                log_debug("P3.4: param '%s' full_type=TypeMap (%d fields)", vname,
                                    ((TypeMap*)resolved)->length);
                            }
                        }
                    }
                }

                // For default params (ASSIGNMENT_PATTERN): if the arg is undefined, eval and assign default
                if (is_default_param) {
                    JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)param_node;
                    if (ap->right) {
                        // emit: if (preg != undefined) goto skip_default;
                        // NOTE: must use MIR_BNE (64-bit) not MIR_BNES (32-bit signed) because
                        // ITEM_JS_UNDEFINED has type tag in high bits; MIR_BNES compares only low 32
                        // bits which would wrongly match INT 0 (0x0300000000000000) and BOOL false.
                        MIR_label_t skip_label = jm_new_label(mt);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BNE,
                            MIR_new_label_op(mt->ctx, skip_label),
                            MIR_new_reg_op(mt->ctx, preg),
                            MIR_new_uint_op(mt->ctx, (uint64_t)ITEM_JS_UNDEFINED)));
                        // emit default value and store into param reg
                        MIR_reg_t def_val = jm_transpile_default_param_value(mt, fn, ap->right);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, preg),
                            MIR_new_reg_op(mt->ctx, def_val)));
                        jm_emit_label(mt, skip_label);
                    }
                }

                if (has_default_params && param_binding && param_binding->node_type == JS_AST_NODE_IDENTIFIER) {
                    JsMirVarEntry* pvar = jm_find_var(mt, vname);
                    if (pvar) {
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, pvar->reg),
                            MIR_new_reg_op(mt->ctx, preg)));
                        pvar->tdz_active = false;
                    }
                }

                // Unwrap assignment pattern for destructured default params: f({ x = 1 } = {})
                // After applying the outer default above, also destructure the inner pattern.
                JsAstNode* destr_pat = param_node;
                if (destr_pat->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN)
                    destr_pat = ((JsAssignmentPatternNode*)destr_pat)->left;
                if (destr_pat->node_type == JS_AST_NODE_REST_ELEMENT ||
                    destr_pat->node_type == JS_AST_NODE_SPREAD_ELEMENT)
                    destr_pat = ((JsSpreadElementNode*)destr_pat)->argument;

                // For object-destructured params: function f({ a, b = 0 }) → extract a, b from preg
                if (destr_pat->node_type == JS_AST_NODE_OBJECT_PATTERN) {
                    // Throw TypeError if arg is null/undefined (RequireObjectCoercible)
                    jm_call_void_1(mt, "js_require_object_coercible",
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, preg));
                    MIR_reg_t param_exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
                    MIR_label_t skip_param_destr = jm_new_label(mt);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, skip_param_destr),
                        MIR_new_reg_op(mt->ctx, param_exc)));

                    JsObjectPatternNode* op = (JsObjectPatternNode*)destr_pat;
                    log_debug("js-mir: destructuring object param %d in %s", i, fc->name);
                    JsAstNode* prop = op->properties;
                    while (prop) {
                        if (prop->node_type == JS_AST_NODE_PROPERTY) {
                            JsPropertyNode* p = (JsPropertyNode*)prop;
                            // determine key to extract
                            MIR_reg_t key;
                            if (p->computed) {
                                key = jm_transpile_box_item(mt, p->key);
                            } else if (p->key && p->key->node_type == JS_AST_NODE_IDENTIFIER) {
                                JsIdentifierNode* kid = (JsIdentifierNode*)p->key;
                                key = jm_box_string_literal(mt, kid->name->chars, kid->name->len);
                            } else {
                                key = jm_transpile_box_item(mt, p->key);
                            }
                            MIR_reg_t val = jm_call_2(mt, "js_property_access", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, preg),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                            // determine target variable (value of property, or key for shorthand)
                            JsAstNode* target = p->value ? p->value : p->key;
                            // handle default: { x = def }
                            if (target && target->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
                                JsAssignmentPatternNode* dp = (JsAssignmentPatternNode*)target;
                                MIR_label_t use_val = MIR_new_label(mt->ctx);
                                MIR_label_t done_lbl = MIR_new_label(mt->ctx);
                                MIR_reg_t res = jm_new_reg(mt, "_dpar", MIR_T_I64);
                                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BNE, MIR_new_label_op(mt->ctx, use_val),
                                    MIR_new_reg_op(mt->ctx, val),
                                    MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED)));
                                MIR_reg_t def_v = jm_transpile_box_item(mt, dp->right);
                                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                    MIR_new_reg_op(mt->ctx, res), MIR_new_reg_op(mt->ctx, def_v)));
                                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, done_lbl)));
                                jm_emit(mt, use_val);
                                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                    MIR_new_reg_op(mt->ctx, res), MIR_new_reg_op(mt->ctx, val)));
                                jm_emit(mt, done_lbl);
                                val = res;
                                target = dp->left;
                            }
                            if (target && target->node_type == JS_AST_NODE_IDENTIFIER) {
                                JsIdentifierNode* tid = (JsIdentifierNode*)target;
                                char tname[128];
                                snprintf(tname, sizeof(tname), "_js_%.*s", (int)tid->name->len, tid->name->chars);
                                MIR_reg_t treg = jm_find_var(mt, tname) ? jm_find_var(mt, tname)->reg
                                                                         : jm_new_reg(mt, tname, MIR_T_I64);
                                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                    MIR_new_reg_op(mt->ctx, treg),
                                    MIR_new_reg_op(mt->ctx, val)));
                                jm_set_var(mt, tname, treg);
                            }
                        }
                        prop = prop->next;
                    }
                    jm_emit_label(mt, skip_param_destr);
                }

                // For array-destructured params: function f([a, b]) → extract by index
                if (destr_pat->node_type == JS_AST_NODE_ARRAY_PATTERN) {
                    // v20: use recursive destructuring helper
                    jm_emit_array_destructure(mt, destr_pat, preg);
                }
                // v20: object-destructured params: function f({a, b})
                if (destr_pat->node_type == JS_AST_NODE_OBJECT_PATTERN) {
                    jm_emit_object_destructure(mt, destr_pat, preg);
                }
            }
            param_node = param_node ? param_node->next : NULL;
        }

        // P9: Pre-scan variable types before transpiling body
        jm_prescan_float_widening(mt, fn->body);

        // Hoist var declarations: register all var-declared names initialized
        // to null/undefined BEFORE function hoisting (mirrors JS var hoisting)
        if (fn->body && fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            struct hashmap* body_locals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            bool effective_strict = mt->is_global_strict || mt->is_module ||
                (fc && fc->is_strict) || jm_has_use_strict_directive(fn);
            struct hashmap* annexb_lex_collisions = NULL;
            if (!effective_strict) {
                annexb_lex_collisions = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                    jm_name_hash, jm_name_cmp, NULL, NULL);
                jm_collect_all_let_const_names_recursive(fn->body, annexb_lex_collisions);
            }
            jm_collect_body_locals(fn->body, body_locals, true);  // var_only: only hoist var declarations
            size_t viter = 0; void* vitem;
            while (hashmap_iter(body_locals, &viter, &vitem)) {
                JsNameSetEntry* e = (JsNameSetEntry*)vitem;
                if (e->from_func_decl && effective_strict) {
                    log_debug("js-mir: strict skip nested function hoist '%s'", e->name);
                    continue;
                }
                if (e->from_func_decl && annexb_lex_collisions &&
                    jm_name_set_has(annexb_lex_collisions, e->name)) {
                    log_debug("js-mir: AnnexB skip function hoist '%s' (lexical collision)", e->name);
                    continue;
                }
                if (e->from_func_decl && fc && fc->uses_arguments &&
                    strcmp(e->name, "_js_arguments") == 0 &&
                    jm_function_has_formal_arguments_binding(fn)) {
                    log_debug("js-mir: AnnexB skip function hoist '%s' (arguments binding)", e->name);
                    continue;
                }
                if (!jm_find_var(mt, e->name)) {
                    // Skip hoisting vars that are module vars in IIFE body functions
                    if (mt->current_fc && mt->current_fc->is_iife_body && mt->module_consts) {
                        JsModuleConstEntry mclookup;
                        snprintf(mclookup.name, sizeof(mclookup.name), "%s", e->name);
                        JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                        if (mc && mc->const_type == MCONST_MODVAR && mc->is_iife_var) continue;
                    }
                    MIR_reg_t vr = jm_new_reg(mt, e->name, MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, vr),
                        MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEF_VAL)));
                    jm_set_var(mt, e->name, vr);
                    // v50: mark as hoisted so var_reused path can distinguish from parameters
                    {
                        JsMirVarEntry* hvar = jm_find_var(mt, e->name);
                        if (hvar) hvar->from_hoist = true;
                    }
                }
            }
            if (annexb_lex_collisions) hashmap_free(annexb_lex_collisions);
            hashmap_free(body_locals);

            // v20 TDZ: Reinitialize let/const variables to TDZ sentinel
            struct hashmap* let_consts = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            jm_collect_let_const_names(fn->body, let_consts);
            size_t lciter = 0; void* lcitem;
            while (hashmap_iter(let_consts, &lciter, &lcitem)) {
                JsNameSetEntry* lce = (JsNameSetEntry*)lcitem;
                JsMirVarEntry* ve = jm_function_find_current_scope_var(mt, lce->name);
                if (!ve) {
                    MIR_reg_t vr = jm_new_reg(mt, lce->name, MIR_T_I64);
                    jm_set_var(mt, lce->name, vr);
                    ve = jm_function_find_current_scope_var(mt, lce->name);
                }
                if (ve) {
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, ve->reg),
                        MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_TDZ)));
                    ve->is_let_const = true;
                    ve->is_const = (lce->var_kind == 2);
                    ve->tdz_active = true;
                }
            }
            hashmap_free(let_consts);
        }

        // The scope env is a single heap-allocated Item array shared by all child closures,
        // enabling mutable capture semantics (JS captures by reference, not by value).
        if (fc->has_scope_env && fc->scope_env_count > 0) {
            // v16 safety check: verify reuse_parent_env assumption at compile time.
            // If any scope_env var is locally declared (not from_env), the var shadows
            // a same-named capture and reuse is unsafe (would write to wrong parent slot).
            bool reuse_valid = fc->reuse_parent_env && has_captures && env_reg != 0;
            if (reuse_valid) {
                for (int s = 0; s < fc->scope_env_count; s++) {
                    const char* sname = fc->scope_env_names[s];
                    JsMirVarEntry* svar = jm_find_var(mt, sname);
                    if (svar && !svar->from_env) {
                        log_debug("js-mir: reuse_parent_env aborted for '%s': scope_env var '%s' is locally declared (shadows capture)",
                            fc->name, sname);
                        reuse_valid = false;
                        break;
                    }
                }
            }
            if (reuse_valid) {
                // v16: Reuse parent env as scope_env — no allocation needed.
                // All scope_env vars are transitive captures, so children can
                // read/write directly from the grandparent's scope_env.
                // Child captures were already remapped in Phase 1.7b.
                mt->scope_env_reg = env_reg;
                mt->scope_env_slot_count = fc->reuse_env_slot_count;

                // Mark vars for scope_env write-back using parent env slots.
                // Look up the correct slot from the captures array (not the var entry),
                // because a local var declaration (param or var) may have overwritten the
                // capture entry, losing the env_slot information.
                for (int s = 0; s < fc->scope_env_count; s++) {
                    const char* sname = fc->scope_env_names[s];
                    int parent_slot = -1;
                    for (int c = 0; c < fc->capture_count; c++) {
                        if (strcmp(sname, fc->captures[c].name) == 0) {
                            parent_slot = fc->captures[c].scope_env_slot;
                            break;
                        }
                    }
                    JsMirVarEntry* svar = jm_find_var(mt, sname);
                    if (svar && parent_slot >= 0) {
                        svar->in_scope_env = true;
                        svar->scope_env_slot = parent_slot;
                        svar->scope_env_reg = mt->scope_env_reg;
                    }
                }
                log_debug("js-mir: reusing parent env as scope env for '%s' (slot_count=%d)", fc->name, mt->scope_env_slot_count);
            } else {
            mt->scope_env_reg = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
                MIR_T_I64, MIR_new_int_op(mt->ctx, fc->scope_env_count));
            mt->scope_env_slot_count = fc->scope_env_count;

            // v29: If this function has a parent env link, store parent env ptr in last slot
            if (fc->has_parent_env_link && has_captures && env_reg != 0) {
                int parent_env_slot = fc->scope_env_count - 1; // last slot
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, parent_env_slot * (int)sizeof(uint64_t), mt->scope_env_reg, 0, 1),
                    MIR_new_reg_op(mt->ctx, env_reg)));
                log_debug("js-mir: stored parent env link in scope_env[%d] for '%s'", parent_env_slot, fc->name);
            }

            // Populate scope env with current values and mark vars for write-back
            // Only iterate normal slots — NFE extra slots are populated by self-patch code
            int populate_limit = fc->scope_env_normal_count > 0 ? fc->scope_env_normal_count : fc->scope_env_count;
            for (int s = 0; s < populate_limit; s++) {
                const char* sname = fc->scope_env_names[s];
                // v29: Skip __parent_env__ slot — already populated above
                if (fc->has_parent_env_link && s == fc->scope_env_count - 1) continue;
                JsMirVarEntry* svar = jm_find_var(mt, sname);
                MIR_reg_t val;
                if (svar) {
                    if (svar->from_env) {
                        // v16: Re-read LIVE from parent's env instead of stale register.
                        // This ensures mutations by sibling closures (e.g. beforeAll)
                        // are visible to grandchild closures (e.g. it() in nested describe).
                        val = jm_new_reg(mt, "senv_live", MIR_T_I64);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, val),
                            MIR_new_mem_op(mt->ctx, MIR_T_I64, svar->env_slot * (int)sizeof(uint64_t), svar->env_reg, 0, 1)));
                    } else {
                        val = svar->reg;
                        if (jm_is_native_type(svar->type_id))
                            val = jm_box_native(mt, svar->reg, svar->type_id);
                    }
                    // Mark var for scope_env write-back on future assignments
                    svar->in_scope_env = true;
                    svar->scope_env_slot = s;
                    svar->scope_env_reg = mt->scope_env_reg;
                } else if (strcmp(sname, "_js_this") == 0) {
                    val = jm_call_0(mt, "js_get_lexical_this_binding", MIR_T_I64);
                    JsVarScopeEntry this_entry;
                    memset(&this_entry, 0, sizeof(this_entry));
                    snprintf(this_entry.name, sizeof(this_entry.name), "_js_this");
                    this_entry.var.reg = val;
                    this_entry.var.mir_type = MIR_T_I64;
                    this_entry.var.type_id = LMD_TYPE_ANY;
                    this_entry.var.in_scope_env = true;
                    this_entry.var.scope_env_slot = s;
                    this_entry.var.scope_env_reg = mt->scope_env_reg;
                    this_entry.var.typed_array_type = -1;
                    hashmap_set(mt->var_scopes[mt->scope_depth], &this_entry);
                } else if (strcmp(sname, "_js_new.target") == 0) {
                    // Normal functions seed shared env slots for child arrows;
                    // otherwise a direct call to the arrow observes undefined
                    // after the runtime new.target slot is cleared.
                    val = jm_call_0(mt, "js_get_new_target", MIR_T_I64);
                    JsVarScopeEntry nt_entry;
                    memset(&nt_entry, 0, sizeof(nt_entry));
                    snprintf(nt_entry.name, sizeof(nt_entry.name), "_js_new.target");
                    nt_entry.var.reg = val;
                    nt_entry.var.mir_type = MIR_T_I64;
                    nt_entry.var.type_id = LMD_TYPE_ANY;
                    nt_entry.var.in_scope_env = true;
                    nt_entry.var.scope_env_slot = s;
                    nt_entry.var.scope_env_reg = mt->scope_env_reg;
                    nt_entry.var.typed_array_type = -1;
                    hashmap_set(mt->var_scopes[mt->scope_depth], &nt_entry);
                } else if (strcmp(sname, "_js_arguments") == 0 && fc->uses_arguments &&
                           !has_formal_arguments_binding) {
                    bool args_aliased = !fc->has_non_simple_params &&
                                        !mt->is_module &&
                                        !mt->is_global_strict &&
                                        !fc->is_strict &&
                                        !jm_has_use_strict_directive(fn);
                    jm_call_void_1(mt, "js_set_arguments_info",
                        MIR_T_I64, MIR_new_int_op(mt->ctx, args_aliased ? 0 : 1));
                    val = jm_call_0(mt, "js_build_arguments_object", MIR_T_I64);
                    jm_set_var(mt, "_js_arguments", val);
                    arguments_object_materialized = true;
                    if (args_aliased) {
                        mt->arguments_reg = val;
                        mt->arguments_param_count = 0;
                        JsAstNode* ap = fn->params;
                        while (ap && mt->arguments_param_count < 16) {
                            char apname[128];
                            jm_get_param_name(ap, mt->arguments_param_count, apname, sizeof(apname));
                            snprintf(mt->arguments_param_names[mt->arguments_param_count], 128, "%s", apname);
                            mt->arguments_param_count++;
                            ap = ap->next;
                        }
                    } else {
                        mt->arguments_reg = val;
                        mt->arguments_param_count = 0;
                    }
                } else {
                    // Variable not found locally. Try transitive capture from parent's scope env.
                    bool found_transitive = false;
                    if (env_reg != 0 && fc->parent_index >= 0 && fc->parent_index < mt->func_count) {
                        JsFuncCollected* parent_fc2 = &mt->func_entries[fc->parent_index];
                        for (int ps2 = 0; ps2 < parent_fc2->scope_env_count; ps2++) {
                            if (parent_fc2->scope_env_names && strcmp(parent_fc2->scope_env_names[ps2], sname) == 0) {
                                val = jm_new_reg(mt, "senv_trans", MIR_T_I64);
                                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                    MIR_new_reg_op(mt->ctx, val),
                                    MIR_new_mem_op(mt->ctx, MIR_T_I64, ps2 * (int)sizeof(uint64_t), env_reg, 0, 1)));
                                found_transitive = true;
                                break;
                            }
                        }
                    }
                    if (!found_transitive) {
                    if (mt->module_consts) {
                    // Check if var is a module var (e.g., IIFE var from parent scope)
                    JsModuleConstEntry mclookup;
                    snprintf(mclookup.name, sizeof(mclookup.name), "%s", sname);
                    JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                    if (mc && mc->const_type == MCONST_MODVAR) {
                        val = jm_call_1(mt, "js_get_module_var", MIR_T_I64,
                            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mc->int_val));
                    } else if (mc && mc->const_type == MCONST_FUNC) {
                        int fi = (int)mc->int_val;
                        if (fi >= 0 && fi < mt->func_count && mt->func_entries[fi].func_item) {
                            val = jm_create_func_or_closure(mt, &mt->func_entries[fi]);
                        } else {
                            val = jm_emit_null(mt);
                        }
                    } else {
                        val = jm_emit_null(mt);
                    }
                } else {
                    // Var hasn't been created yet (e.g., declared later in body).
                    // Write null for now; will be updated when the var is assigned.
                    val = jm_emit_null(mt);
                }
                    } // end if (!found_transitive)
                } // end else (not found locally)
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, s * (int)sizeof(uint64_t), mt->scope_env_reg, 0, 1),
                    MIR_new_reg_op(mt->ctx, val)));
            }
            log_debug("js-mir: allocated scope env for '%s' with %d slots", fc->name, fc->scope_env_count);
            }
        } else {
            mt->scope_env_reg = 0;
            mt->scope_env_slot_count = 0;
        }

        // Hoist inner function declarations: create variable bindings so nested
        // calls can resolve them (mirrors Phase 3 hoisting for top-level functions)
        if (fn->body && fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* hblk = (JsBlockNode*)fn->body;
            JsAstNode* hs = hblk->statements;
            while (hs) {
                if (hs->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
                    JsFunctionNode* inner_fn = (JsFunctionNode*)hs;
                    if (inner_fn->name) {
                        JsFuncCollected* inner_fc = jm_find_collected_func(mt, inner_fn);
                        if (inner_fc && inner_fc->func_item) {
                            char hvname[128];
                            snprintf(hvname, sizeof(hvname), "_js_%.*s",
                                (int)inner_fn->name->len, inner_fn->name->chars);
                            MIR_reg_t hvar = jm_new_reg(mt, hvname, MIR_T_I64);
                            MIR_reg_t fn_item = jm_create_func_or_closure(mt, inner_fc);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, hvar),
                                MIR_new_reg_op(mt->ctx, fn_item)));
                            jm_set_var(mt, hvname, hvar);
                            jm_scope_env_mark_and_writeback(mt, hvname, hvar);
                        }
                    }
                }
                hs = hs->next;
            }
        }

        // Phase 5: Set in_async flag for async function bodies
        bool saved_in_async = mt->in_async;
        if (fn->is_async) {
            mt->in_async = true;
        }

        // Phase 5: For async functions, wrap body in implicit try/catch
        // so exceptions become Promise.reject(error) instead of propagating
        MIR_label_t async_catch_label = 0;
        MIR_label_t async_end_label = 0;
        MIR_reg_t async_return_val_reg = 0;
        MIR_reg_t async_has_return_reg = 0;
        if (fn->is_async) {
            async_catch_label = jm_new_label(mt);
            async_end_label = jm_new_label(mt);
            // Push try context so that exceptions in the body jump to catch
            if (mt->try_ctx_depth < 16) {
                JsTryContext* tc = &mt->try_ctx_stack[mt->try_ctx_depth++];
                tc->catch_label = async_catch_label;
                tc->finally_label = 0;
                tc->end_label = async_end_label;
                tc->return_val_reg = jm_new_reg(mt, "_async_ret", MIR_T_I64);
                tc->has_return_reg = jm_new_reg(mt, "_async_has_ret", MIR_T_I64);
                tc->has_catch = true;
                tc->has_finally = false;
                tc->inlining_finally = false;
                tc->yield_state_only = false;
                tc->finally_body = NULL;
                tc->saved_exc_flag_reg = 0;
                tc->saved_exc_val_reg = 0;
                // Save register refs before try context could be popped
                async_return_val_reg = tc->return_val_reg;
                async_has_return_reg = tc->has_return_reg;
                // Initialize return tracking
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, tc->return_val_reg),
                    MIR_new_int_op(mt->ctx, 0)));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, tc->has_return_reg),
                    MIR_new_int_op(mt->ctx, 0)));
            }
        }

        // v18q: Create 'arguments' array-like object for non-arrow functions
        bool has_direct_arguments_function_binding =
            jm_function_has_direct_body_function_binding(fn, "_js_arguments");
        if (fc->uses_arguments && !has_formal_arguments_binding &&
            !has_direct_arguments_function_binding) {
            // v20: Set up arguments aliasing for formal params, but only in sloppy mode
            // with simple parameters. Strict mode, default/rest/destructuring params
            // → arguments is "unmapped" (no aliasing).
            bool args_aliased = !fc->has_non_simple_params &&
                                !mt->is_module &&
                                !mt->is_global_strict &&
                                !fc->is_strict &&
                                !jm_has_use_strict_directive(fn);
            JsMirVarEntry* existing_args = jm_find_var(mt, "_js_arguments");
            MIR_reg_t args_arr;
            if (arguments_object_materialized && existing_args) {
                args_arr = existing_args->reg;
            } else {
                // v29: Tell runtime whether this is strict arguments (for callee/caller TypeError)
                jm_call_void_1(mt, "js_set_arguments_info",
                    MIR_T_I64, MIR_new_int_op(mt->ctx, args_aliased ? 0 : 1));
                // Build arguments object from the actual call-site args (stored by js_invoke_fn)
                args_arr = jm_call_0(mt, "js_build_arguments_object", MIR_T_I64);
                jm_set_var(mt, "_js_arguments", args_arr);
                jm_scope_env_mark_and_writeback(mt, "_js_arguments", args_arr);
            }
            if (args_aliased) {
                mt->arguments_reg = args_arr;
                mt->arguments_param_count = 0;
                JsAstNode* ap = fn->params;
                while (ap && mt->arguments_param_count < 16) {
                    char apname[128];
                    jm_get_param_name(ap, mt->arguments_param_count, apname, sizeof(apname));
                    snprintf(mt->arguments_param_names[mt->arguments_param_count], 128, "%s", apname);
                    mt->arguments_param_count++;
                    ap = ap->next;
                }
            } else {
                mt->arguments_reg = args_arr;
                mt->arguments_param_count = 0;
            }
        } else {
            mt->arguments_reg = 0;
            mt->arguments_param_count = 0;
        }

        // Transpile body
        if (fn->body) {
            if (fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
                JsBlockNode* blk = (JsBlockNode*)fn->body;
                JsAstNode* s = blk->statements;
                while (s) {
                    jm_transpile_statement(mt, s);
                    // Phase 5: After each statement in async body, check for exception
                    if (fn->is_async && async_catch_label) {
                        MIR_reg_t exc_check = jm_call_0(mt, "js_check_exception", MIR_T_I64);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                            MIR_new_label_op(mt->ctx, async_catch_label),
                            MIR_new_reg_op(mt->ctx, exc_check)));
                    } else if (!fn->is_async) {
                        // Non-async: propagate exception to caller by checking after each statement
                        jm_emit_exc_propagate_check(mt);
                    }
                    s = s->next;
                }
            } else {
                MIR_reg_t val = jm_transpile_box_item(mt, fn->body);
                if (fn->is_async) {
                    val = jm_call_1(mt, "js_promise_resolve", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                }
                jm_emit_eval_local_pop_if_needed(mt);
                jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, val)));
                // Emit exception propagation landing pad if any exception checks
                // were emitted during expression transpilation (e.g. runtime calls).
                // Without this, the BT→func_except_label branch targets an
                // uninserted label, causing a NULL label crash during MIR inlining.
                if (mt->func_except_label != 0) {
                    jm_emit_label(mt, mt->func_except_label);
                    MIR_reg_t exc_ret;
                    if (fn->is_async) {
                        MIR_reg_t error = jm_call_0(mt, "js_clear_exception", MIR_T_I64);
                        exc_ret = jm_call_1(mt, "js_promise_reject", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, error));
                    } else {
                        exc_ret = jm_emit_null(mt);
                    }
                    jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, exc_ret)));
                }
                // Emit async catch/end labels for arrow-body async functions.
                // The try context emits bt→async_catch_label during expression
                // transpilation, so the label must exist in the function body.
                if (fn->is_async && async_catch_label) {
                    if (mt->try_ctx_depth > 0) mt->try_ctx_depth--;
                    jm_emit_label(mt, async_catch_label);
                    {
                        MIR_reg_t error = jm_call_0(mt, "js_clear_exception", MIR_T_I64);
                        MIR_reg_t rejected = jm_call_1(mt, "js_promise_reject", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, error));
                        jm_emit_eval_local_pop_if_needed(mt);
                        jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, rejected)));
                    }
                    if (async_end_label) {
                        jm_emit_label(mt, async_end_label);
                    }
                    mt->in_async = saved_in_async;
                    goto finish_boxed;
                }
                if (fn->is_async && mt->try_ctx_depth > 0) mt->try_ctx_depth--;
                mt->in_async = saved_in_async;
                goto finish_boxed;
            }
        }

        // Phase 5: Async implicit return → Promise.resolve(undefined)
        if (fn->is_async) {
            // Normal exit: jump past catch block
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, async_end_label)));

            // Catch block: convert exception to Promise.reject(error)
            jm_emit_label(mt, async_catch_label);
            if (mt->try_ctx_depth > 0) mt->try_ctx_depth--;
            {
                MIR_reg_t error = jm_call_0(mt, "js_clear_exception", MIR_T_I64);
                MIR_reg_t rejected = jm_call_1(mt, "js_promise_reject", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, error));
                jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, rejected)));
            }

            // Normal end label: check if delayed return, otherwise return Promise.resolve(undefined)
            jm_emit_label(mt, async_end_label);
            {
                // Check if a return statement was hit (stored in async_has_return_reg)
                MIR_label_t async_no_return_label = jm_new_label(mt);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
                    MIR_new_label_op(mt->ctx, async_no_return_label),
                    MIR_new_reg_op(mt->ctx, async_has_return_reg)));
                // Has return: value already wrapped in Promise.resolve by return handler
                jm_emit_eval_local_pop_if_needed(mt);
                jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
                    MIR_new_reg_op(mt->ctx, async_return_val_reg)));

                // No explicit return: return Promise.resolve(undefined)
                jm_emit_label(mt, async_no_return_label);
                MIR_reg_t undef_val = jm_new_reg(mt, "_async_undef", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, undef_val),
                    MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED)));
                MIR_reg_t resolved = jm_call_1(mt, "js_promise_resolve", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, undef_val));
                jm_emit_eval_local_pop_if_needed(mt);
                jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, resolved)));
            }

            mt->in_async = saved_in_async;
        } else {
            // v18: Implicit return undefined (not null) at end of function
            MIR_reg_t undef_val = jm_emit_undefined(mt);
            jm_emit_eval_local_pop_if_needed(mt);
            jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, undef_val)));
        }
    }

    // Emit exception propagation landing pad: if any call inside this function
    // set the exception flag (outside a try block), control jumps here and we
    // return null so the caller's exception check picks it up.
    if (mt->func_except_label != 0) {
        jm_emit_label(mt, mt->func_except_label);
        MIR_reg_t exc_ret;
        if (fn->is_async) {
            MIR_reg_t error = jm_call_0(mt, "js_clear_exception", MIR_T_I64);
            exc_ret = jm_call_1(mt, "js_promise_reject", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, error));
        } else {
            exc_ret = jm_emit_null(mt);
        }
        jm_emit_eval_local_pop_if_needed(mt);
        jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, exc_ret)));
    }

finish_boxed:
    mt->last_closure_has_env = false;
    mt->last_closure_env_reg = 0;
    mt->last_closure_capture_count = 0;
    jm_pop_scope(mt);
    MIR_finish_func(mt->ctx);

    // Restore state
    mt->current_func_item = saved_item;
    mt->current_func = saved_func;
    mt->scope_depth = saved_scope_depth;
    mt->loop_depth = saved_loop_depth;
    mt->in_native_func = saved_in_native;
    mt->in_main = saved_in_main;
    mt->current_fc = saved_fc;
    mt->current_class = saved_class;
    mt->scope_env_reg = saved_scope_env_reg;
    mt->scope_env_slot_count = saved_scope_env_slot_count;
    mt->current_func_index = saved_func_index;
    mt->eval_completion_reg = saved_eval_completion_reg;
    mt->eval_local_frame_reg = saved_eval_local_frame_reg;
}

// ============================================================================
// AST root transpilation
// ============================================================================

// Try to evaluate a JS AST expression as a compile-time numeric constant.
// Returns true if the expression evaluates to a number, and sets *result.
bool jm_try_eval_const_expr(JsMirTranspiler* mt, JsAstNode* node, double* result) {
    if (!node) return false;

    // Literal number
    if (node->node_type == JS_AST_NODE_LITERAL) {
        JsLiteralNode* lit = (JsLiteralNode*)node;
        if (lit->literal_type == JS_LITERAL_NUMBER) { *result = lit->value.number_value; return true; }
        return false;
    }

    // Identifier referencing a known module const
    if (node->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)node;
        char vname[128];
        snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
        JsModuleConstEntry lookup;
        snprintf(lookup.name, sizeof(lookup.name), "%s", vname);
        JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup);
        if (mc) {
            if (mc->const_type == MCONST_INT) { *result = (double)mc->int_val; return true; }
            if (mc->const_type == MCONST_FLOAT) { *result = mc->float_val; return true; }
        }
        return false;
    }

    // Unary minus
    if (node->node_type == JS_AST_NODE_UNARY_EXPRESSION) {
        JsUnaryNode* un = (JsUnaryNode*)node;
        if (un->op == JS_OP_MINUS || un->op == JS_OP_SUB) {
            double v;
            if (jm_try_eval_const_expr(mt, un->operand, &v)) { *result = -v; return true; }
        }
        return false;
    }

    // Binary expression
    if (node->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        double lv, rv;
        if (!jm_try_eval_const_expr(mt, bin->left, &lv)) return false;
        if (!jm_try_eval_const_expr(mt, bin->right, &rv)) return false;
        switch (bin->op) {
        case JS_OP_MUL: *result = lv * rv; return true;
        case JS_OP_ADD: *result = lv + rv; return true;
        case JS_OP_SUB: *result = lv - rv; return true;
        case JS_OP_DIV: if (rv != 0) { *result = lv / rv; return true; } return false;
        default: return false;
        }
    }

    return false;
}
