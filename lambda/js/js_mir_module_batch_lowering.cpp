#include "js_mir_internal.hpp"

extern "C" void js_dynfunc_cache_reset(void);

// ============================================================================
// ES Module support: deferred MIR cleanup and path resolution
// ============================================================================

// Module MIR contexts must survive until main program finishes
// (exported functions have JIT-compiled pointers that must remain valid)
#define MAX_MODULE_CONTEXTS 4096
MIR_context_t module_mir_contexts[MAX_MODULE_CONTEXTS];
NamePool* module_mir_name_pools[MAX_MODULE_CONTEXTS];
Pool* module_mir_ast_pools[MAX_MODULE_CONTEXTS];
char* module_mir_source_buffers[MAX_MODULE_CONTEXTS];
int module_mir_context_count = 0;

// Runtime context saved for use by js_new_function_from_string (new Function(...) support)
Runtime* js_source_runtime = NULL;
int js_dynamic_func_counter = 0;

// Track the active MIR context during compilation/execution so that
// batch timeout recovery (longjmp from SIGALRM) can finish the leaked context.
// Set before execution, cleared after normal cleanup in transpile_js_to_mir_core.
MIR_context_t g_active_mir_ctx = NULL;

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

static void jm_collect_for_init_lexical_pattern(JsAstNode* pat, struct hashmap* names) {
    if (!pat) return;
    if (pat->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)pat;
        if (id->name) {
            char fname[128];
            snprintf(fname, sizeof(fname), "_js_%.*s", (int)id->name->len, id->name->chars);
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

static void jm_emit_set_private_class_index(JsMirTranspiler* mt, MIR_reg_t cls_obj, JsClassEntry* ce) {
    if (!mt || !cls_obj || !ce || mt->class_count <= 0) return;
    int class_index = (int)(ce - mt->class_entries);
    jm_call_void_2(mt, "js_set_private_class_index",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
        MIR_T_I64, MIR_new_int_op(mt->ctx, class_index));
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

void jm_cleanup_active_mir(void) {
    if (g_active_mir_ctx) {
        MIR_finish(g_active_mir_ctx);
        g_active_mir_ctx = NULL;
    }
}

void jm_defer_mir_cleanup(MIR_context_t ctx) {
    if (module_mir_context_count < MAX_MODULE_CONTEXTS) {
        module_mir_name_pools[module_mir_context_count] = NULL;
        module_mir_ast_pools[module_mir_context_count] = NULL;
        module_mir_source_buffers[module_mir_context_count] = NULL;
        module_mir_contexts[module_mir_context_count++] = ctx;
    } else {
        // Cannot MIR_finish — JIT-compiled function pointers still live and
        // would crash on call. Just leak the context (rare path; survives
        // until process exit anyway).
        log_error("module: exceeded max deferred MIR contexts (%d) — leaking ctx", MAX_MODULE_CONTEXTS);
    }
}

void jm_cleanup_deferred_mir() {
    js_dynfunc_cache_reset();
    for (int i = 0; i < module_mir_context_count; i++) {
        MIR_finish(module_mir_contexts[i]);
        if (module_mir_name_pools[i]) name_pool_release(module_mir_name_pools[i]);
        if (module_mir_ast_pools[i]) pool_destroy(module_mir_ast_pools[i]);
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
        if (module_mir_name_pools[module_mir_context_count]) {
            name_pool_release(module_mir_name_pools[module_mir_context_count]);
            module_mir_name_pools[module_mir_context_count] = NULL;
        }
        if (module_mir_ast_pools[module_mir_context_count]) {
            pool_destroy(module_mir_ast_pools[module_mir_context_count]);
            module_mir_ast_pools[module_mir_context_count] = NULL;
        }
        if (module_mir_source_buffers[module_mir_context_count]) {
            mem_free(module_mir_source_buffers[module_mir_context_count]);
            module_mir_source_buffers[module_mir_context_count] = NULL;
        }
    }
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
        // Bare specifier — try npm_resolve_module for node_modules lookup
        char spec_buf[512];
        snprintf(spec_buf, sizeof(spec_buf), "%.*s", spec_len, specifier);

        // skip node: builtins (handled by js_module_get)
        bool has_node_prefix = (spec_len >= 5 && strncmp(specifier, "node:", 5) == 0);

        // skip known built-in module names (prefer engine built-ins over npm polyfills)
        static const char* builtin_names[] = {
            "fs", "fs/promises", "child_process", "path", "path/posix", "path/win32",
            "os", "url", "util", "util/types",
            "process", "querystring", "events", "buffer",
            "crypto", "dns", "dns/promises", "zlib", "readline",
            "stream", "stream/promises", "stream/web", "stream/consumers", "stream/iter",
            "net", "tls", "http", "https",
            "string_decoder", "assert", "assert/strict",
            "timers", "timers/promises", "console", "module",
            "worker_threads", "cluster", "vm", "v8", "tty", "perf_hooks",
            "diagnostics_channel", "async_hooks", "domain",
            "internal/util", "internal/test/binding", "internal/streams/add-abort-signal", NULL
        };
        bool is_builtin = has_node_prefix;
        if (!is_builtin) {
            for (int i = 0; builtin_names[i]; i++) {
                if (strcmp(spec_buf, builtin_names[i]) == 0) {
                    is_builtin = true;
                    break;
                }
            }
        }

        if (!is_builtin) {
            // get the directory of the importing file
            char from_dir[512];
            if (dir_len > 0) {
                snprintf(from_dir, sizeof(from_dir), "%.*s", dir_len - 1, base_file); // strip trailing /
            } else {
                from_dir[0] = '.'; from_dir[1] = '\0';
            }

            const char* conditions[] = { "lambda", "node", "import", "default" };
            NpmModuleResolution res = npm_resolve_module(spec_buf, from_dir, conditions, 4);
            if (res.found && res.resolved_path) {
                snprintf(out, out_size, "%s", res.resolved_path);
                npm_module_resolution_free(&res);
                return; // already fully resolved with extension
            }
            npm_module_resolution_free(&res);
        }

        // fallback: use as-is (will be checked as builtin by js_module_get)
        snprintf(out, out_size, "%.*s", spec_len, specifier);
        if (is_builtin) return;  // builtins don't need .js extension
    }

    // If doesn't end in a known JS extension, try adding .js
    // Recognized extensions: .js, .mjs, .cjs, .json, .ls
    int len = (int)strlen(out);
    bool has_node_prefix = (len >= 5 && strncmp(out, "node:", 5) == 0);
    if (!has_node_prefix) {
        bool has_ext = (len >= 3 && strcmp(out + len - 3, ".js") == 0) ||
                       (len >= 4 && strcmp(out + len - 4, ".mjs") == 0) ||
                       (len >= 4 && strcmp(out + len - 4, ".cjs") == 0) ||
                       (len >= 5 && strcmp(out + len - 5, ".json") == 0) ||
                       (len >= 3 && strcmp(out + len - 3, ".ls") == 0);
        if (!has_ext && len + 3 < out_size) {
            strcat(out, ".js");
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
    temp_id.base.node_type = JS_AST_NODE_IDENTIFIER;
    temp_id.name = name_pool_create_len(mt->tp->name_pool, name, name_len);

    MIR_reg_t val = jm_transpile_box_item(mt, (JsAstNode*)&temp_id);
    const char* export_key = is_default ? "default" : name;
    int export_key_len = is_default ? 7 : name_len;
    MIR_reg_t key = jm_box_string_literal(mt, export_key, export_key_len);
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
    temp_id.base.node_type = JS_AST_NODE_IDENTIFIER;
    temp_id.name = name_pool_create_len(mt->tp->name_pool, local_name, local_len);

    MIR_reg_t val = jm_transpile_box_item(mt, (JsAstNode*)&temp_id);
    MIR_reg_t key = jm_box_string_literal(mt, export_name, export_len);
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

// Resolve expression type given known param types and a simple local type map.
// local_names/local_types track declared locals (let x , etc).
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

TypeId jm_p6_expr_type(JsAstNode* expr,
                               const char param_names[][128], TypeId* param_types, int param_count,
                               const char local_names[][128], TypeId* local_types, int local_count) {
    if (!expr) return LMD_TYPE_ANY;
    if (expr->node_type == JS_AST_NODE_LITERAL) {
        JsLiteralNode* lit = (JsLiteralNode*)expr;
        if (lit->literal_type == JS_LITERAL_NUMBER)
            return lit->has_decimal ? LMD_TYPE_FLOAT : LMD_TYPE_INT;
        if (lit->literal_type == JS_LITERAL_BOOLEAN) return LMD_TYPE_INT;
        if (lit->literal_type == JS_LITERAL_STRING) return LMD_TYPE_STRING;
        return LMD_TYPE_ANY;
    }
    if (expr->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)expr;
        char name[128];
        snprintf(name, sizeof(name), "_js_%.*s", (int)id->name->len, id->name->chars);
        for (int i = 0; i < param_count; i++)
            if (strcmp(name, param_names[i]) == 0) return param_types[i];
        for (int i = 0; i < local_count; i++)
            if (strcmp(name, local_names[i]) == 0) return local_types[i];
        return LMD_TYPE_ANY;
    }
    if (expr->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
        JsBinaryNode* bin = (JsBinaryNode*)expr;
        switch (bin->op) {
        case JS_OP_LT: case JS_OP_LE: case JS_OP_GT: case JS_OP_GE:
        case JS_OP_EQ: case JS_OP_NE: case JS_OP_STRICT_EQ: case JS_OP_STRICT_NE:
            return LMD_TYPE_BOOL;
        case JS_OP_BIT_AND: case JS_OP_BIT_OR: case JS_OP_BIT_XOR:
        case JS_OP_BIT_LSHIFT: case JS_OP_BIT_RSHIFT: case JS_OP_BIT_URSHIFT:
            return LMD_TYPE_INT;
        case JS_OP_DIV: case JS_OP_EXP:
            return LMD_TYPE_FLOAT;
        default: {
            TypeId lt = jm_p6_expr_type(bin->left, param_names, param_types, param_count,
                                         local_names, local_types, local_count);
            TypeId rt = jm_p6_expr_type(bin->right, param_names, param_types, param_count,
                                         local_names, local_types, local_count);
            if (bin->op == JS_OP_ADD) {
                if (lt == LMD_TYPE_STRING || rt == LMD_TYPE_STRING) return LMD_TYPE_STRING;
                if (lt == LMD_TYPE_FLOAT || rt == LMD_TYPE_FLOAT) return LMD_TYPE_FLOAT;
                if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) return LMD_TYPE_INT;
                return LMD_TYPE_ANY;
            }
            // SUB, MUL, MOD
            if (lt == LMD_TYPE_FLOAT || rt == LMD_TYPE_FLOAT) return LMD_TYPE_FLOAT;
            if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) return LMD_TYPE_INT;
            return LMD_TYPE_ANY;
        }}
    }
    if (expr->node_type == JS_AST_NODE_UNARY_EXPRESSION) {
        JsUnaryNode* un = (JsUnaryNode*)expr;
        if (un->op == JS_OP_BIT_NOT) return LMD_TYPE_INT;
        if (un->op == JS_OP_NOT) return LMD_TYPE_BOOL;
        if (un->op == JS_OP_TYPEOF) return LMD_TYPE_STRING;
        if (un->op == JS_OP_MINUS || un->op == JS_OP_PLUS)
            return jm_p6_expr_type(un->operand, param_names, param_types, param_count,
                                    local_names, local_types, local_count);
        if (un->op == JS_OP_INCREMENT || un->op == JS_OP_DECREMENT)
            return jm_p6_expr_type(un->operand, param_names, param_types, param_count,
                                    local_names, local_types, local_count);
    }
    if (expr->node_type == JS_AST_NODE_CONDITIONAL_EXPRESSION) {
        JsConditionalNode* cond = (JsConditionalNode*)expr;
        TypeId ct = jm_p6_expr_type(cond->consequent, param_names, param_types, param_count,
                                     local_names, local_types, local_count);
        TypeId at = jm_p6_expr_type(cond->alternate, param_names, param_types, param_count,
                                     local_names, local_types, local_count);
        if (ct == at) return ct;
        if ((ct == LMD_TYPE_INT && at == LMD_TYPE_FLOAT) || (ct == LMD_TYPE_FLOAT && at == LMD_TYPE_INT))
            return LMD_TYPE_FLOAT;
        return LMD_TYPE_ANY;
    }
    return LMD_TYPE_ANY;
}

// Collect local variable types by scanning declarations in a block.
// For `let x = 0`, type is INT. For `let x = param`, type is param type.
// For compound assignments/updates, the type stays the same.
void jm_p6_collect_locals(JsAstNode* body,
                                  const char param_names[][128], TypeId* param_types, int param_count,
                                  char local_names[][128], TypeId* local_types, int* local_count, int max_locals) {
    if (!body || body->node_type != JS_AST_NODE_BLOCK_STATEMENT) return;
    JsBlockNode* blk = (JsBlockNode*)body;
    JsAstNode* stmt = blk->statements;
    while (stmt && *local_count < max_locals) {
        if (stmt->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
            JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)stmt;
            JsAstNode* decl = vd->declarations;
            while (decl && *local_count < max_locals) {
                if (decl->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                    JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)decl;
                    if (d->id && d->id->node_type == JS_AST_NODE_IDENTIFIER && d->init) {
                        JsIdentifierNode* id = (JsIdentifierNode*)d->id;
                        TypeId init_type = jm_p6_expr_type(d->init,
                            param_names, param_types, param_count,
                            local_names, local_types, *local_count);
                        if (init_type == LMD_TYPE_INT || init_type == LMD_TYPE_FLOAT) {
                            int li = *local_count;
                            snprintf(local_names[li], 128, "_js_%.*s",
                                (int)id->name->len, id->name->chars);
                            local_types[li] = init_type;
                            (*local_count)++;
                        }
                    }
                }
                decl = decl->next;
            }
        }
        stmt = stmt->next;
    }
}

// Walk return statements and resolve their types using param + local info.
void jm_p6_return_walk(JsAstNode* node,
                               const char param_names[][128], TypeId* param_types, int param_count,
                               const char local_names[][128], TypeId* local_types, int local_count,
                               TypeId* collected, int* count, int max_count) {
    if (!node || *count >= max_count) return;
    switch (node->node_type) {
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* ret = (JsReturnNode*)node;
        if (!ret->argument) { collected[(*count)++] = LMD_TYPE_NULL; return; }
        TypeId t = jm_p6_expr_type(ret->argument,
            param_names, param_types, param_count,
            local_names, local_types, local_count);
        collected[(*count)++] = t;
        return;
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        JsAstNode* s = blk->statements;
        while (s) { jm_p6_return_walk(s, param_names, param_types, param_count,
                        local_names, local_types, local_count, collected, count, max_count); s = s->next; }
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* n = (JsIfNode*)node;
        jm_p6_return_walk(n->consequent, param_names, param_types, param_count,
            local_names, local_types, local_count, collected, count, max_count);
        jm_p6_return_walk(n->alternate, param_names, param_types, param_count,
            local_names, local_types, local_count, collected, count, max_count);
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* n = (JsWhileNode*)node;
        jm_p6_return_walk(n->body, param_names, param_types, param_count,
            local_names, local_types, local_count, collected, count, max_count);
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* n = (JsForNode*)node;
        jm_p6_return_walk(n->body, param_names, param_types, param_count,
            local_names, local_types, local_count, collected, count, max_count);
        break;
    }
    case JS_AST_NODE_DO_WHILE_STATEMENT: {
        JsDoWhileNode* n = (JsDoWhileNode*)node;
        jm_p6_return_walk(n->body, param_names, param_types, param_count,
            local_names, local_types, local_count, collected, count, max_count);
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* n = (JsTryNode*)node;
        jm_p6_return_walk(n->block, param_names, param_types, param_count,
            local_names, local_types, local_count, collected, count, max_count);
        jm_p6_return_walk(n->handler, param_names, param_types, param_count,
            local_names, local_types, local_count, collected, count, max_count);
        break;
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* n = (JsCatchNode*)node;
        jm_p6_return_walk(n->body, param_names, param_types, param_count,
            local_names, local_types, local_count, collected, count, max_count);
        break;
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* n = (JsSwitchNode*)node;
        JsAstNode* c = n->cases;
        while (c) { jm_p6_return_walk(c, param_names, param_types, param_count,
            local_names, local_types, local_count, collected, count, max_count); c = c->next; }
        break;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* n = (JsSwitchCaseNode*)node;
        JsAstNode* s = n->consequent;
        while (s) { jm_p6_return_walk(s, param_names, param_types, param_count,
            local_names, local_types, local_count, collected, count, max_count); s = s->next; }
        break;
    }
    default: break;
    }
}

// P6: Re-infer the return type of a function using param types and local variable tracing.
void jm_p6_reinfer_return_type(JsFuncCollected* fc) {
    JsFunctionNode* fn = fc->node;
    if (!fn || !fn->body) return;
    if (jm_p6_expr_has_bigint_literal(fn->body)) {
        fc->return_type = LMD_TYPE_ANY;
        return;
    }

    // Build param names array
    char param_names[16][128];
    int param_count = fc->param_count;
    if (param_count > 16) param_count = 16;
    JsAstNode* pn = fn->params;
    for (int i = 0; i < param_count; i++) {
        jm_get_param_name(pn, i, param_names[i], 128);
        pn = pn ? pn->next : NULL;
    }

    // Collect local variable types from declarations
    char local_names[32][128];
    TypeId local_types[32];
    int local_count = 0;
    jm_p6_collect_locals(fn->body, param_names, fc->param_types, param_count,
                          local_names, local_types, &local_count, 32);

    // Walk return statements
    TypeId collected[32];
    int count = 0;
    jm_p6_return_walk(fn->body, param_names, fc->param_types, param_count,
                       local_names, local_types, local_count,
                       collected, &count, 32);

    if (count == 0) { fc->return_type = LMD_TYPE_NULL; return; }

    TypeId unified = LMD_TYPE_ANY;
    bool has_concrete = false;
    bool has_any = false;
    for (int i = 0; i < count; i++) {
        if (collected[i] == LMD_TYPE_ANY) { has_any = true; continue; }
        if (collected[i] == LMD_TYPE_NULL) continue;
        if (!has_concrete) {
            unified = collected[i];
            has_concrete = true;
        } else if (collected[i] != unified) {
            if ((unified == LMD_TYPE_INT && collected[i] == LMD_TYPE_FLOAT) ||
                (unified == LMD_TYPE_FLOAT && collected[i] == LMD_TYPE_INT))
                unified = LMD_TYPE_FLOAT;
            else return; // conflicting → stay ANY
        }
    }

    if (has_concrete && !has_any) {
        fc->return_type = unified;
        log_info("P6 re-inferred return type for %s: %s",
                 fc->name, unified == LMD_TYPE_INT ? "INT" : unified == LMD_TYPE_FLOAT ? "FLOAT" : "OTHER");
    }
}

// ============================================================================
// P6: Call-site type narrowing
// After body-scan inference (Phase 1.75) and widening (Phase 1.76),
// narrow ANY params to INT/FLOAT when ALL call sites agree on the type.
// ============================================================================

// Determine argument type statically from AST (no compiled scope needed).
TypeId jm_p6_static_arg_type(JsMirTranspiler* mt, JsAstNode* arg) {
    if (!arg) return LMD_TYPE_ANY;
    if (arg->node_type == JS_AST_NODE_LITERAL) {
        JsLiteralNode* lit = (JsLiteralNode*)arg;
        if (lit->literal_type == JS_LITERAL_NUMBER) {
            if (lit->is_bigint) return LMD_TYPE_DECIMAL;
            return lit->has_decimal ? LMD_TYPE_FLOAT : LMD_TYPE_INT;
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
            snprintf(lookup.name, sizeof(lookup.name), "_js_%.*s",
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
        // comparison operators → BOOL
        switch (bin->op) {
        case JS_OP_LT: case JS_OP_LE: case JS_OP_GT: case JS_OP_GE:
        case JS_OP_EQ: case JS_OP_NE: case JS_OP_STRICT_EQ: case JS_OP_STRICT_NE:
            return LMD_TYPE_BOOL;
        default: break;
        }
        TypeId lt = jm_p6_static_arg_type(mt, bin->left);
        TypeId rt = jm_p6_static_arg_type(mt, bin->right);
        if (lt == LMD_TYPE_DECIMAL || rt == LMD_TYPE_DECIMAL) return LMD_TYPE_ANY;
        switch (bin->op) {
        case JS_OP_ADD:
            if (lt == LMD_TYPE_STRING || rt == LMD_TYPE_STRING) return LMD_TYPE_STRING;
            if (lt == LMD_TYPE_FLOAT || rt == LMD_TYPE_FLOAT) return LMD_TYPE_FLOAT;
            if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) return LMD_TYPE_INT;
            return LMD_TYPE_ANY;
        case JS_OP_SUB: case JS_OP_MUL: case JS_OP_MOD:
            if (lt == LMD_TYPE_FLOAT || rt == LMD_TYPE_FLOAT) return LMD_TYPE_FLOAT;
            if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) return LMD_TYPE_INT;
            return LMD_TYPE_ANY;
        case JS_OP_DIV: case JS_OP_EXP:
            return LMD_TYPE_FLOAT;
        case JS_OP_BIT_AND: case JS_OP_BIT_OR: case JS_OP_BIT_XOR:
        case JS_OP_BIT_LSHIFT: case JS_OP_BIT_RSHIFT: case JS_OP_BIT_URSHIFT:
            return LMD_TYPE_INT;
        default: return LMD_TYPE_ANY;
        }
    }
    if (arg->node_type == JS_AST_NODE_UNARY_EXPRESSION) {
        JsUnaryNode* un = (JsUnaryNode*)arg;
        if (un->op == JS_OP_MINUS || un->op == JS_OP_SUB ||
            un->op == JS_OP_PLUS || un->op == JS_OP_ADD ||
            un->op == JS_OP_BIT_NOT)
            return jm_p6_static_arg_type(mt, un->operand);
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

// ============================================================================
// Phase 1.78: P4b constructor call-site type propagation
// Walks AST to find new ClassName(args) expressions and accumulates argument
// type evidence per class constructor, enabling typed slot reads for
// parameter-assigned fields (this.x ).
// ============================================================================
void jm_p4b_ctor_walk(JsMirTranspiler* mt, JsAstNode* node,
                              P4bCtorEvidence* evidence) {
    if (!node) return;
    switch (node->node_type) {
    case JS_AST_NODE_NEW_EXPRESSION: {
        JsCallNode* call = (JsCallNode*)node;
        if (call->callee && call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* cid = (JsIdentifierNode*)call->callee;
            for (int ci = 0; ci < mt->class_count; ci++) {
                JsClassEntry* ce = &mt->class_entries[ci];
                if (!ce->name || (int)ce->name->len != (int)cid->name->len) continue;
                if (strncmp(ce->name->chars, cid->name->chars, ce->name->len) != 0) continue;
                if (!ce->constructor || !ce->constructor->fc) break;
                JsFuncCollected* ctor_fc = ce->constructor->fc;
                if (ctor_fc->ctor_prop_count == 0) break;
                JsAstNode* arg = call->arguments;
                for (int pi = 0; arg && pi < 16; pi++, arg = arg->next) {
                    TypeId at = jm_p6_static_arg_type(mt, arg);
                    if (at == LMD_TYPE_INT || at == LMD_TYPE_BOOL)
                        evidence[ci * 16 + pi].int_count++;
                    else if (at == LMD_TYPE_FLOAT)
                        evidence[ci * 16 + pi].float_count++;
                    else
                        evidence[ci * 16 + pi].other_count++;
                }
                break;
            }
        }
        // recurse into arguments (may contain nested new expressions)
        JsAstNode* a = call->arguments;
        while (a) { jm_p4b_ctor_walk(mt, a, evidence); a = a->next; }
        break;
    }
    case JS_AST_NODE_CALL_EXPRESSION: {
        JsCallNode* call = (JsCallNode*)node;
        JsAstNode* a = call->arguments;
        while (a) { jm_p4b_ctor_walk(mt, a, evidence); a = a->next; }
        jm_p4b_ctor_walk(mt, call->callee, evidence);
        break;
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        jm_p4b_ctor_walk(mt, bin->left, evidence);
        jm_p4b_ctor_walk(mt, bin->right, evidence);
        break;
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* un = (JsUnaryNode*)node;
        jm_p4b_ctor_walk(mt, un->operand, evidence);
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* asgn = (JsAssignmentNode*)node;
        jm_p4b_ctor_walk(mt, asgn->right, evidence);
        jm_p4b_ctor_walk(mt, asgn->left, evidence);
        break;
    }
    case JS_AST_NODE_MEMBER_EXPRESSION: {
        JsMemberNode* mem = (JsMemberNode*)node;
        jm_p4b_ctor_walk(mt, mem->object, evidence);
        if (mem->computed) jm_p4b_ctor_walk(mt, mem->property, evidence);
        break;
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* cond = (JsConditionalNode*)node;
        jm_p4b_ctor_walk(mt, cond->test, evidence);
        jm_p4b_ctor_walk(mt, cond->consequent, evidence);
        jm_p4b_ctor_walk(mt, cond->alternate, evidence);
        break;
    }
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* ret = (JsReturnNode*)node;
        jm_p4b_ctor_walk(mt, ret->argument, evidence);
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)node;
        JsAstNode* d = vd->declarations;
        while (d) { jm_p4b_ctor_walk(mt, d, evidence); d = d->next; }
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* vd = (JsVariableDeclaratorNode*)node;
        jm_p4b_ctor_walk(mt, vd->init, evidence);
        break;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* es = (JsExpressionStatementNode*)node;
        jm_p4b_ctor_walk(mt, es->expression, evidence);
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* ifn = (JsIfNode*)node;
        jm_p4b_ctor_walk(mt, ifn->test, evidence);
        jm_p4b_ctor_walk(mt, ifn->consequent, evidence);
        jm_p4b_ctor_walk(mt, ifn->alternate, evidence);
        break;
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        JsAstNode* s = blk->statements;
        while (s) { jm_p4b_ctor_walk(mt, s, evidence); s = s->next; }
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* w = (JsWhileNode*)node;
        jm_p4b_ctor_walk(mt, w->test, evidence);
        jm_p4b_ctor_walk(mt, w->body, evidence);
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* f = (JsForNode*)node;
        jm_p4b_ctor_walk(mt, f->init, evidence);
        jm_p4b_ctor_walk(mt, f->test, evidence);
        jm_p4b_ctor_walk(mt, f->update, evidence);
        jm_p4b_ctor_walk(mt, f->body, evidence);
        break;
    }
    case JS_AST_NODE_FOR_IN_STATEMENT:
    case JS_AST_NODE_FOR_OF_STATEMENT: {
        JsForInNode* fin = (JsForInNode*)node;
        jm_p4b_ctor_walk(mt, fin->right, evidence);
        jm_p4b_ctor_walk(mt, fin->body, evidence);
        break;
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* sw = (JsSwitchNode*)node;
        jm_p4b_ctor_walk(mt, sw->discriminant, evidence);
        JsAstNode* c = sw->cases;
        while (c) { jm_p4b_ctor_walk(mt, c, evidence); c = c->next; }
        break;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* sc = (JsSwitchCaseNode*)node;
        jm_p4b_ctor_walk(mt, sc->test, evidence);
        JsAstNode* s = sc->consequent;
        while (s) { jm_p4b_ctor_walk(mt, s, evidence); s = s->next; }
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* t = (JsTryNode*)node;
        jm_p4b_ctor_walk(mt, t->block, evidence);
        jm_p4b_ctor_walk(mt, t->handler, evidence);
        jm_p4b_ctor_walk(mt, t->finalizer, evidence);
        break;
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* cc = (JsCatchNode*)node;
        jm_p4b_ctor_walk(mt, cc->body, evidence);
        break;
    }
    case JS_AST_NODE_DO_WHILE_STATEMENT: {
        JsDoWhileNode* dw = (JsDoWhileNode*)node;
        jm_p4b_ctor_walk(mt, dw->body, evidence);
        jm_p4b_ctor_walk(mt, dw->test, evidence);
        break;
    }
    case JS_AST_NODE_ARRAY_EXPRESSION: {
        JsArrayNode* arr = (JsArrayNode*)node;
        JsAstNode* e = arr->elements;
        while (e) { jm_p4b_ctor_walk(mt, e, evidence); e = e->next; }
        break;
    }
    case JS_AST_NODE_OBJECT_EXPRESSION: {
        JsObjectNode* obj = (JsObjectNode*)node;
        JsAstNode* p = obj->properties;
        while (p) { jm_p4b_ctor_walk(mt, p, evidence); p = p->next; }
        break;
    }
    case JS_AST_NODE_PROPERTY: {
        JsPropertyNode* prop = (JsPropertyNode*)node;
        jm_p4b_ctor_walk(mt, prop->value, evidence);
        break;
    }
    case JS_AST_NODE_TEMPLATE_LITERAL: {
        JsTemplateLiteralNode* tl = (JsTemplateLiteralNode*)node;
        if (tl->expressions) {
            JsAstNode* e = tl->expressions;
            while (e) { jm_p4b_ctor_walk(mt, e, evidence); e = e->next; }
        }
        break;
    }
    case JS_AST_NODE_THROW_STATEMENT: {
        JsThrowNode* th = (JsThrowNode*)node;
        jm_p4b_ctor_walk(mt, th->argument, evidence);
        break;
    }
    case JS_AST_NODE_SPREAD_ELEMENT: {
        JsSpreadElementNode* sp = (JsSpreadElementNode*)node;
        jm_p4b_ctor_walk(mt, sp->argument, evidence);
        break;
    }
    case JS_AST_NODE_SEQUENCE_EXPRESSION: {
        JsSequenceNode* seq = (JsSequenceNode*)node;
        JsAstNode* e = seq->expressions;
        while (e) { jm_p4b_ctor_walk(mt, e, evidence); e = e->next; }
        break;
    }
    case JS_AST_NODE_LABELED_STATEMENT: {
        JsLabeledStatementNode* lab = (JsLabeledStatementNode*)node;
        jm_p4b_ctor_walk(mt, lab->body, evidence);
        break;
    }
    case JS_AST_NODE_WITH_STATEMENT: {
        JsWithStatementNode* ws = (JsWithStatementNode*)node;
        jm_p4b_ctor_walk(mt, ws->object, evidence);
        jm_p4b_ctor_walk(mt, ws->body, evidence);
        break;
    }
    default:
        break;
    }
}

// Per-function, per-param call-site evidence
// Walk AST collecting call-site argument types for narrowing
void jm_p6_narrow_walk(JsMirTranspiler* mt, JsAstNode* node,
                               P6NarrowEvidence evidence[][16]) {
    if (!node) return;
    switch (node->node_type) {
    case JS_AST_NODE_CALL_EXPRESSION: {
        JsCallNode* call = (JsCallNode*)node;
        JsFuncCollected* callee_fc = jm_find_collected_func_for_call(mt, call);
        if (callee_fc) {
            int fi = (int)(callee_fc - mt->func_entries);
            JsAstNode* arg = call->arguments;
            for (int pi = 0; pi < callee_fc->param_count && pi < 16; pi++) {
                TypeId at = arg ? jm_p6_static_arg_type(mt, arg) : LMD_TYPE_ANY;
                if (at == LMD_TYPE_INT || at == LMD_TYPE_BOOL)
                    evidence[fi][pi].int_count++;
                else if (at == LMD_TYPE_FLOAT)
                    evidence[fi][pi].float_count++;
                else
                    evidence[fi][pi].other_count++;
                if (arg) arg = arg->next;
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
// Scan function bodies for calls with literal arguments that contradict the
// inferred param types. Widen those params to ANY and revoke native eligibility.
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
            for (int i = 0; i < callee_fc->param_count && i < 16; i++) {
                if (!arg) break;
                if (arg->node_type == JS_AST_NODE_LITERAL) {
                    JsLiteralNode* lit = (JsLiteralNode*)arg;
                    TypeId arg_type = LMD_TYPE_ANY;
                    if (lit->literal_type == JS_LITERAL_NUMBER) {
                        if (lit->is_bigint) arg_type = LMD_TYPE_DECIMAL;
                        else
                        arg_type = lit->has_decimal ? LMD_TYPE_FLOAT : LMD_TYPE_INT;
                    }
                    else if (lit->literal_type == JS_LITERAL_STRING)
                        arg_type = LMD_TYPE_STRING;
                    else if (lit->literal_type == JS_LITERAL_BOOLEAN)
                        arg_type = LMD_TYPE_BOOL;
                    TypeId expected = callee_fc->param_types[i];
                    bool ok = true;
                    if (expected == LMD_TYPE_INT)
                        ok = (arg_type == LMD_TYPE_INT || arg_type == LMD_TYPE_BOOL || arg_type == LMD_TYPE_ANY);
                    else if (expected == LMD_TYPE_FLOAT)
                        ok = (arg_type == LMD_TYPE_FLOAT || arg_type == LMD_TYPE_INT || arg_type == LMD_TYPE_ANY);
                    if (!ok) {
                        log_debug("js-mir P3.5 callsite: widening %s param %d from type %d to ANY (literal mismatch)",
                            callee_fc->name, i, expected);
                        callee_fc->param_types[i] = LMD_TYPE_ANY;
                        callee_fc->has_native_version = false;
                    }
                }
                arg = arg->next;
            }
        }
        // v18l: Revoke native version for function expressions passed as callback
        // arguments. The caller (e.g. reduce, map, forEach) may pass any type,
        // so unboxing to native int/float inside the boxed wrapper is unsafe.
        {
            JsAstNode* cb_arg = call->arguments;
            while (cb_arg) {
                if (cb_arg->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
                    cb_arg->node_type == JS_AST_NODE_ARROW_FUNCTION) {
                    JsFuncCollected* cb_fc = jm_find_collected_func(mt, (JsFunctionNode*)cb_arg);
                    if (cb_fc && cb_fc->has_native_version) {
                        log_debug("js-mir P3.5 callsite: revoking native for callback '%s' (passed as argument)",
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
    MIR_reg_t key_reg = jm_box_string_literal(mt, name->chars, (int)name->len);
    jm_call_void_1(mt,
        is_func ? "js_evalscript_check_global_function_decl" : "js_evalscript_check_global_var_decl",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg));
    jm_emit_exc_propagate_check(mt);
}

static void jm_emit_evalscript_global_decl_check_prefixed(JsMirTranspiler* mt, const char* name) {
    if (!name) return;
    if (strncmp(name, "_js_", 4) == 0) name += 4;
    if (!name[0]) return;
    MIR_reg_t key_reg = jm_box_string_literal(mt, name, (int)strlen(name));
    jm_call_void_1(mt, "js_evalscript_check_global_var_decl",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg));
    jm_emit_exc_propagate_check(mt);
}

static void jm_emit_evalscript_global_lex_decl_check_name(JsMirTranspiler* mt, String* name) {
    if (!name || name->len <= 0) return;
    MIR_reg_t key_reg = jm_box_string_literal(mt, name->chars, (int)name->len);
    jm_call_void_1(mt, "js_evalscript_check_global_lex_decl",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg));
    jm_emit_exc_propagate_check(mt);
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
                MIR_reg_t key_reg = jm_box_string_literal(mt, name, (int)strlen(name));
                jm_call_void_1(mt, "js_evalscript_check_global_lex_decl",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg));
                jm_emit_exc_propagate_check(mt);
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
        char vname[128];
        snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
        JsModuleConstEntry lookup;
        memset(&lookup, 0, sizeof(lookup));
        snprintf(lookup.name, sizeof(lookup.name), "%s", vname);
        JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup);
        if (!mc || mc->const_type != MCONST_MODVAR || mc->var_kind != JS_VAR_VAR ||
                mc->is_implicit_global || (int)mc->int_val < 0) {
            return false;
        }
    }
    return true;
}

void transpile_js_mir_ast(JsMirTranspiler* mt, JsAstNode* root) {
    if (!root || root->node_type != JS_AST_NODE_PROGRAM) {
        log_error("js-mir: expected program node");
        return;
    }

    JsProgramNode* program = (JsProgramNode*)root;

    // v20: Detect program-level "use strict" directive
    mt->is_global_strict = (mt->tp && mt->tp->strict_mode) || program->has_use_strict_directive;

    // Phase 1: Collect all functions (post-order: innermost first)
    jm_collect_functions(mt, root);
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

    // First pass: collect simple literal constants (const declarations only).
    // Top-level const declarations are lexical bindings with TDZ and live-binding
    // semantics, so they must stay in module var slots rather than being folded
    // into immediate constants.
    {
        JsAstNode* s = program->body;
        while (s) {
            // Unwrap export declarations to reach inner const declarations
            JsAstNode* actual = s;
            if (s->node_type == JS_AST_NODE_EXPORT_DECLARATION) {
                JsExportNode* exp = (JsExportNode*)s;
                if (exp->declaration) actual = exp->declaration;
            }
            if (actual->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                s = s->next;
                continue;
            }
            s = s->next;
        }
    }

    // Second pass: fold constant expressions (e.g., 4 * PI * PI, 32 * 1024, -3.14)
    // Uses recursive evaluator that handles nested binary expressions and unary minus.
    // Only applies to const declarations (let/var are mutable).
    {
        JsAstNode* s = program->body;
        while (s) {
            // Unwrap export declarations for constant folding
            JsAstNode* actual = s;
            if (s->node_type == JS_AST_NODE_EXPORT_DECLARATION) {
                JsExportNode* exp = (JsExportNode*)s;
                if (exp->declaration) actual = exp->declaration;
            }
            if (actual->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                s = s->next;
                continue;
            }
            s = s->next;
        }
    }

    // Third pass: assign module var indices for non-literal top-level declarations.
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
                            char vname[128];
                            snprintf(vname, sizeof(vname), "_js_%.*s",
                                (int)vid->name->len, vid->name->chars);
                            JsModuleConstEntry lookup;
                            snprintf(lookup.name, sizeof(lookup.name), "%s", vname);
                            if (!hashmap_get(mt->module_consts, &lookup) && mt->module_var_count < JS_MAX_MODULE_VARS) {
                                JsModuleConstEntry mce;
                                memset(&mce, 0, sizeof(mce));
                                snprintf(mce.name, sizeof(mce.name), "%s", vname);
                                mce.const_type = MCONST_MODVAR;
                                mce.int_val = mt->module_var_count++;
                                mce.var_kind = (int)v->kind;  // v20 TDZ: track let/const/var
                                // P5: Track initial type for arithmetic optimization.
                                // Only set for numeric literal initializers — safe because
                                // the JIT will use inline unbox/arithmetic for these variables.
                                mce.modvar_type = 0;  // default: unknown (0 = LMD_TYPE_RAW_POINTER = not tracked)
                                if (vd->init && vd->init->node_type == JS_AST_NODE_LITERAL) {
                                    JsLiteralNode* mlit = (JsLiteralNode*)vd->init;
                                    if (mlit->literal_type == JS_LITERAL_NUMBER) {
                                        double mdv = mlit->value.number_value;
                                        if (mlit->is_bigint) {
                                            mce.modvar_type = LMD_TYPE_DECIMAL;
                                        } else if (!mlit->has_decimal && mdv == (double)(int64_t)mdv &&
                                            mdv >= -36028797018963968.0 && mdv <= 36028797018963967.0) {
                                            mce.modvar_type = LMD_TYPE_INT;
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
                                snprintf(lookup.name, sizeof(lookup.name), "%s", ne->name);
                                if (!hashmap_get(mt->module_consts, &lookup) && mt->module_var_count < JS_MAX_MODULE_VARS) {
                                    JsModuleConstEntry mce;
                                    memset(&mce, 0, sizeof(mce));
                                    snprintf(mce.name, sizeof(mce.name), "%s", ne->name);
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
                snprintf(lex_lookup.name, sizeof(lex_lookup.name), "%s", e->name);
                if (hashmap_get(eval_lex_collisions, &lex_lookup)) {
                    log_debug("js-mir: suppress AnnexB nested func hoist '%s' (let/const collision)", e->name);
                    continue;
                }
            }
            JsModuleConstEntry lookup;
            snprintf(lookup.name, sizeof(lookup.name), "%s", e->name);
            if (!hashmap_get(mt->module_consts, &lookup) && mt->module_var_count < JS_MAX_MODULE_VARS) {
                JsModuleConstEntry mce;
                memset(&mce, 0, sizeof(mce));
                snprintf(mce.name, sizeof(mce.name), "%s", e->name);
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
                    char vname[128];
                    snprintf(vname, sizeof(vname), "_js_%.*s",
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
                    snprintf(lookup.name, sizeof(lookup.name), "%s", vname);
                    if (!hashmap_get(mt->module_consts, &lookup) && mt->module_var_count < JS_MAX_MODULE_VARS) {
                        JsModuleConstEntry mce;
                        memset(&mce, 0, sizeof(mce));
                        snprintf(mce.name, sizeof(mce.name), "%s", vname);
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
                    char vname[128];
                    snprintf(vname, sizeof(vname), "_js_%.*s",
                        (int)imp->namespace_name->len, imp->namespace_name->chars);
                    JsModuleConstEntry lookup;
                    snprintf(lookup.name, sizeof(lookup.name), "%s", vname);
                    if (!hashmap_get(mt->module_consts, &lookup) && mt->module_var_count < JS_MAX_MODULE_VARS) {
                        JsModuleConstEntry mce;
                        memset(&mce, 0, sizeof(mce));
                        snprintf(mce.name, sizeof(mce.name), "%s", vname);
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
                        char vname[128];
                        snprintf(vname, sizeof(vname), "_js_%.*s",
                            (int)isp->local_name->len, isp->local_name->chars);
                        JsModuleConstEntry lookup;
                        snprintf(lookup.name, sizeof(lookup.name), "%s", vname);
                        if (!hashmap_get(mt->module_consts, &lookup) && mt->module_var_count < JS_MAX_MODULE_VARS) {
                            JsModuleConstEntry mce;
                            memset(&mce, 0, sizeof(mce));
                            snprintf(mce.name, sizeof(mce.name), "%s", vname);
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
                    char name[128];
                    snprintf(name, sizeof(name), "_js_%.*s", (int)fn->name->len, fn->name->chars);
                    jm_name_set_add(top_declarations, name);
                }
            } else if (s->node_type == JS_AST_NODE_CLASS_DECLARATION) {
                JsClassNode* cls = (JsClassNode*)s;
                if (cls->name) {
                    char name[128];
                    snprintf(name, sizeof(name), "_js_%.*s", (int)cls->name->len, cls->name->chars);
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
                snprintf(lookup.name, sizeof(lookup.name), "%s", e->name);
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
            char name[128];
            snprintf(name, sizeof(name), "_js_%.*s", (int)fn->name->len, fn->name->chars);
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
                if (fn_b->base.node_type != JS_AST_NODE_FUNCTION_DECLARATION) continue;
                if (fn_a->base.node_type != JS_AST_NODE_FUNCTION_DECLARATION) break;
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
                        snprintf(mce.name, sizeof(mce.name), "_js_%.*s",
                            (int)fn->name->len, fn->name->chars);
                        // Only add if not already in module_consts
                        JsModuleConstEntry lookup;
                        snprintf(lookup.name, sizeof(lookup.name), "%s", mce.name);
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
                        char top_name[128];
                        snprintf(top_name, sizeof(top_name), "_js_%.*s", (int)id->name->len, id->name->chars);
                        if (strcmp(top_name, candidate) == 0) return true;
                    }
                } else if (top->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
                    JsFunctionNode* fn = (JsFunctionNode*)top;
                    if (fn->name) {
                        char top_name[128];
                        snprintf(top_name, sizeof(top_name), "_js_%.*s", (int)fn->name->len, fn->name->chars);
                        if (strcmp(top_name, candidate) == 0) return true;
                    }
                } else if (top->node_type == JS_AST_NODE_CLASS_DECLARATION) {
                    JsClassNode* cls = (JsClassNode*)top;
                    if (cls->name) {
                        char top_name[128];
                        snprintf(top_name, sizeof(top_name), "_js_%.*s", (int)cls->name->len, cls->name->chars);
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
            snprintf(mce.name, sizeof(mce.name), "_js_%.*s",
                (int)fn->name->len, fn->name->chars);
            JsModuleConstEntry lookup;
            snprintf(lookup.name, sizeof(lookup.name), "%s", mce.name);
            if (!hashmap_get(mt->module_consts, &lookup)) {
                mce.const_type = MCONST_MODVAR;
                mce.int_val = mt->module_var_count++;
                hashmap_set(mt->module_consts, &mce);
                log_debug("js-mir: iife func '%s' → module_var[%d]", mce.name, (int)mce.int_val);
            }
        };

        // Scan top-level statements for IIFE patterns
        JsAstNode* stmt = program->body;
        while (stmt) {
            // Unwrap expression statements and bundled `var ns = function(){...}();`
            // initializers. Both forms create an IIFE scope whose local bindings can
            // be captured by nested closures after the initializer has run.
            JsAstNode* expr = NULL;
            if (stmt->node_type == JS_AST_NODE_EXPRESSION_STATEMENT) {
                JsExpressionStatementNode* es = (JsExpressionStatementNode*)stmt;
                expr = es->expression;
            } else if (stmt->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)stmt;
                JsAstNode* d = vd->declarations;
                while (d) {
                    if (d->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                        JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)d;
                        if (jm_find_iife_function_expr(decl->init)) {
                            expr = decl->init;
                            break;
                        }
                    }
                    d = d->next;
                }
            } else {
                stmt = stmt->next;
                continue;
            }
            // Look for CALL_EXPRESSION whose callee is a function literal
            if (!expr || expr->node_type != JS_AST_NODE_CALL_EXPRESSION) {
                stmt = stmt->next;
                continue;
            }
            JsFunctionNode* iife_fn = jm_find_iife_function_expr(expr);
            if (!iife_fn || !iife_fn->body) { stmt = stmt->next; continue; }

            // Js53 P3 Bug C-1 fix: async (and generator) IIFEs have their own
            // state-machine env-slot storage for locals. Promoting their `var`
            // declarations to module-vars (the sync-IIFE optimization below)
            // causes the await fast-path's resolved value to be lost when the
            // module-var slot is written from inside the state machine. Skip
            // the IIFE-modvar promotion for async/generator IIFEs.
            if (iife_fn->is_async || iife_fn->is_generator) { stmt = stmt->next; continue; }

            bool self_referencing_named_iife = false;
            if (iife_fn->name) {
                char self_name[128];
                snprintf(self_name, sizeof(self_name), "_js_%.*s",
                    (int)iife_fn->name->len, iife_fn->name->chars);
                struct hashmap* iife_refs = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                    jm_name_hash, jm_name_cmp, NULL, NULL);
                jm_collect_body_refs(iife_fn->body, iife_refs);
                self_referencing_named_iife = jm_name_set_has(iife_refs, self_name);
                hashmap_free(iife_refs);
            }
            if (self_referencing_named_iife) { stmt = stmt->next; continue; }

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
                                    char vname[128];
                                    snprintf(vname, sizeof(vname), "_js_%.*s",
                                        (int)vid->name->len, vid->name->chars);
                                    if (top_level_declares_name(vname, stmt)) {
                                        d = d->next;
                                        continue;
                                    }
                                    JsModuleConstEntry lookup;
                                    snprintf(lookup.name, sizeof(lookup.name), "%s", vname);
                                    if (!hashmap_get(mt->module_consts, &lookup) && mt->module_var_count < JS_MAX_MODULE_VARS) {
                                        JsModuleConstEntry mce;
                                        memset(&mce, 0, sizeof(mce));
                                        snprintf(mce.name, sizeof(mce.name), "%s", vname);
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
                    JsModuleConstEntry lookup;
                    snprintf(lookup.name, sizeof(lookup.name), "%s", e->name);
                    if (!hashmap_get(mt->module_consts, &lookup) && mt->module_var_count < JS_MAX_MODULE_VARS) {
                        JsModuleConstEntry mce;
                        memset(&mce, 0, sizeof(mce));
                        snprintf(mce.name, sizeof(mce.name), "%s", e->name);
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
            snprintf(mce.name, sizeof(mce.name), "_js_%.*s",
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
                ce->superclass = jm_find_class(mt, super_id->name->chars, (int)super_id->name->len);
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

    // Disable P3 (shaped slot writes) for constructors of classes in inheritance
    // hierarchies where the parent constructor has field assignments.
    // When a child calls super(), the parent constructor's property writes
    // can change the object shape, making the child's P3 slot indices incorrect.
    // However, if the parent has NO constructor fields (e.g., abstract Benchmark base
    // class), the child's shape is self-contained and P3 is safe.
    for (int i = 0; i < mt->class_count; i++) {
        JsClassEntry* ce = &mt->class_entries[i];
        if (ce->superclass) {
            bool parent_has_ctor_fields = ce->superclass->constructor &&
                ce->superclass->constructor->fc &&
                ce->superclass->constructor->fc->ctor_prop_count > 0;
            // Disable P3 for the superclass constructor if it has fields
            // (child's pre-shaped object conflicts with parent's field writes)
            if (parent_has_ctor_fields) {
                log_debug("js-mir: disabling P3 for superclass constructor '%.*s' (parent of '%.*s')",
                    (int)(ce->superclass->name ? ce->superclass->name->len : 0),
                    ce->superclass->name ? ce->superclass->name->chars : "<anon>",
                    (int)(ce->name ? ce->name->len : 0),
                    ce->name ? ce->name->chars : "<anon>");
                ce->superclass->constructor->fc->ctor_prop_count = 0;
            }
            // Disable P3 for the child class constructor ONLY when parent has fields.
            // If parent has no ctor fields (e.g., Benchmark), child's shape indices are
            // self-contained and safe for P1/P2 native slot access.
            if (parent_has_ctor_fields &&
                ce->constructor && ce->constructor->fc &&
                ce->constructor->fc->ctor_prop_count > 0) {
                log_debug("js-mir: disabling P3 for child constructor '%.*s' (extends '%.*s' with fields)",
                    (int)(ce->name ? ce->name->len : 0),
                    ce->name ? ce->name->chars : "<anon>",
                    (int)(ce->superclass->name ? ce->superclass->name->len : 0),
                    ce->superclass->name ? ce->superclass->name->chars : "<anon>");
                ce->constructor->fc->ctor_prop_count = 0;
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
                snprintf(mce.name, sizeof(mce.name), "_js_%.*s_%.*s",
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
                char cname[128];
                snprintf(cname, sizeof(cname), "_js_%.*s", (int)ce->name->len, ce->name->chars);
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
            jm_collect_enclosing_lexicals_for_target((JsAstNode*)program,
                (JsAstNode*)fc->node, ancestor_func_locals);
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
                    char aname[128];
                    snprintf(aname, sizeof(aname), "_js_%.*s", (int)afn->name->len, afn->name->chars);
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
                            snprintf(mclookup.name, sizeof(mclookup.name), "%s", e->name);
                            JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                            is_iife_promoted_module_var = mc && mc->const_type == MCONST_MODVAR &&
                                mc->is_iife_var && anc->is_iife_body;
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
                                            char lname[128];
                                            snprintf(lname, sizeof(lname), "_js_%.*s", (int)id->name->len, id->name->chars);
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
                    JsNameSetEntry lookup;
                    memset(&lookup, 0, sizeof(lookup));
                    snprintf(lookup.name, sizeof(lookup.name), "%s", fc->captures[ci].name);
                    JsNameSetEntry* lce = (JsNameSetEntry*)hashmap_get(let_const_names, &lookup);
                    if (lce) {
                        fc->captures[ci].is_let_const = true;
                        fc->captures[ci].is_const = (lce->var_kind == JS_VAR_CONST);
                    }
                }
                hashmap_free(let_const_names);
            }

            hashmap_free(ancestor_func_locals);
            hashmap_free(ancestor_names);
        }

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
                                snprintf(mclookup.name, sizeof(mclookup.name), "%s", bl_entry->name);
                                JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                                if (mc && mc->const_type == MCONST_MODVAR &&
                                    mc->is_iife_var && parent->is_iife_body) {
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
                        if (strcmp(cap_name, "_js_this") == 0) continue; // handled specially
                        if (jm_name_set_has(parent_own, cap_name)) continue; // parent already has it

                        // Skip self-reference captures: a named function expression's name
                        // is only visible inside its own body (JS spec), not in the parent scope.
                        // Don't propagate it upward — the function resolves it from its own closure env.
                        if (child->node && child->node->name) {
                            char child_self_name[128];
                            snprintf(child_self_name, sizeof(child_self_name), "_js_%.*s",
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
                            snprintf(lookup.name, sizeof(lookup.name), "%s", cap_name);
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
                                        char aname[128];
                                        snprintf(aname, sizeof(aname), "_js_%.*s",
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
                                            bool is_iife_promoted_module_var = mc_prop->const_type == MCONST_MODVAR &&
                                                mc_prop->is_iife_var && anc->is_iife_body;
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
                        if (parent->node && parent->node->base.node_type == JS_AST_NODE_FUNCTION_EXPRESSION &&
                            parent->node->name) {
                            char parent_self_name[128];
                            snprintf(parent_self_name, sizeof(parent_self_name), "_js_%.*s",
                                (int)parent->node->name->len, parent->node->name->chars);
                            cap_is_parent_nfe = (strcmp(cap_name, parent_self_name) == 0);
                        }

                        // Add as capture to parent
                        jm_ensure_captures_capacity(parent);
                        snprintf(parent->captures[parent->capture_count].name, 128, "%s", cap_name);
                        parent->captures[parent->capture_count].scope_env_slot = -1;
                        parent->captures[parent->capture_count].grandparent_slot = -1;
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
                        log_debug("js-mir: propagated capture '%s' from '%s' to parent '%s'",
                            cap_name, child->name, parent->name);
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

                // Determine child's NFE self-name (if any)
                char child_self_name[128] = {0};
                if (child->node && child->node->name) {
                    snprintf(child_self_name, sizeof(child_self_name), "_js_%.*s",
                        (int)child->node->name->len, child->node->name->chars);
                }

                bool is_child_nfe = (child->node && child->node->base.node_type == JS_AST_NODE_FUNCTION_EXPRESSION);
                bool has_nfe_self_capture = false;
                for (int k = 0; k < child->capture_count; k++) {
                    const char* cname = child->captures[k].name;
                    // Skip true NFE self-captures (child is a function expression, not declaration).
                    // Name alone is not enough: minified bundles often have an
                    // outer binding and an NFE self binding with the same name.
                    if (child_self_name[0] && strcmp(cname, child_self_name) == 0
                        && is_child_nfe && child->captures[k].is_nfe_binding) {
                        has_nfe_self_capture = true;
                        continue;
                    }
                    jm_name_set_add(scope_vars, cname);
                }
                if (has_nfe_self_capture) nfe_extra_count++;
            }

            int base_count = (int)hashmap_count(scope_vars);
            int total_needed = base_count + nfe_extra_count;

            if (total_needed > 0) {
                // Allocate scope_env_names (+2 for potential __parent_env__ and safety)
                parent_fc->scope_env_names = (char(*)[64])mem_calloc(total_needed + 2, 64, MEM_CAT_JS_RUNTIME);

                // Re-iterate children in original order to fill names deterministically
                int fill_idx = 0;
                if (base_count > 0) {
                    hashmap_clear(scope_vars, false);
                    for (int ci = 0; ci < mt->func_count; ci++) {
                        JsFuncCollected* child = &mt->func_entries[ci];
                        if (child->parent_index != fi) continue;
                        if (child->capture_count == 0) continue;

                        char child_self_name2[128] = {0};
                        if (child->node && child->node->name) {
                            snprintf(child_self_name2, sizeof(child_self_name2), "_js_%.*s",
                                (int)child->node->name->len, child->node->name->chars);
                        }

                        bool is_child_nfe2 = (child->node && child->node->base.node_type == JS_AST_NODE_FUNCTION_EXPRESSION);
                        for (int k = 0; k < child->capture_count; k++) {
                            const char* cname = child->captures[k].name;
                            // Same skip as first pass: true NFE self-captures only.
                            if (child_self_name2[0] && strcmp(cname, child_self_name2) == 0
                                && is_child_nfe2 && child->captures[k].is_nfe_binding) {
                                continue;
                            }
                            if (!jm_name_set_has(scope_vars, cname)) {
                                jm_name_set_add(scope_vars, cname);
                                snprintf(parent_fc->scope_env_names[fill_idx], 64, "%s", cname);
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
                    if (!child->node || !child->node->name) continue;
                    char csn[128];
                    snprintf(csn, sizeof(csn), "_js_%.*s",
                        (int)child->node->name->len, child->node->name->chars);
                    // Only true NFEs (not function declarations) get extra slots
                    if (child->node->base.node_type != JS_AST_NODE_FUNCTION_EXPRESSION) continue;
                    bool assigned_nfe_slot = false;
                    for (int k = 0; k < child->capture_count; k++) {
                        if (strcmp(child->captures[k].name, csn) == 0 &&
                            child->captures[k].is_nfe_binding) {
                            child->captures[k].scope_env_slot = extra_slot;
                            assigned_nfe_slot = true;
                        }
                    }
                    if (assigned_nfe_slot) {
                        snprintf(parent_fc->scope_env_names[extra_slot], 64, "%s", csn);
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

                        // Build child's NFE self-name to skip during remap
                        char child_self_remap[128] = {0};
                        if (child->node && child->node->name) {
                            snprintf(child_self_remap, sizeof(child_self_remap), "_js_%.*s",
                                (int)child->node->name->len, child->node->name->chars);
                        }

                        bool is_child_nfe_remap = (child->node && child->node->base.node_type == JS_AST_NODE_FUNCTION_EXPRESSION);
                        for (int k = 0; k < child->capture_count; k++) {
                            // Skip true NFE self-captures — already assigned dedicated slots
                            if (child_self_remap[0] &&
                                strcmp(child->captures[k].name, child_self_remap) == 0 &&
                                is_child_nfe_remap && child->captures[k].is_nfe_binding) {
                                continue;
                            }
                            // Find this capture's slot in the normal portion of scope env
                            for (int s = 0; s < normal_slot_count; s++) {
                                if (strcmp(child->captures[k].name, parent_fc->scope_env_names[s]) == 0) {
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
    //   * exclude for-init / for-of / for-in lexical bindings — they need
    //     per-iteration semantics via the existing per-closure-env path
    //     (regression: language/statements/for/scope-body-lex-open.js, found by
    //     two earlier Js56 attempts at this fix);
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

        struct hashmap* scope_vars = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);

        // P7a: helper that decides whether a capture name qualifies for the
        // module-level scope env. Returns false when the binding needs
        // per-iteration semantics (for-init lets, block-lets declared inside a
        // loop body — both collected into `for_init_lets`).
        auto capture_qualifies = [&](const char* name, bool is_let_const,
                                     bool is_nfe_binding) -> bool {
            if (!is_let_const) return false;
            if (is_nfe_binding) return false;
            if (mt->module_consts) {
                JsModuleConstEntry mclookup;
                memset(&mclookup, 0, sizeof(mclookup));
                snprintf(mclookup.name, sizeof(mclookup.name), "%s", name);
                JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                if (mc && mc->const_type == MCONST_MODVAR) return false;
            }
            if (jm_name_set_has(for_init_lets, name)) return false;
            if (strcmp(name, "_js_this") == 0 ||
                strcmp(name, "_js_new.target") == 0 ||
                strcmp(name, "_js_arguments") == 0) return false;
            return true;
        };

        // P7a: a closure participates in the module scope env only when EVERY
        // one of its let/const captures qualifies. If even one capture is
        // disqualified (e.g. a `for (let ctor of …) { … rab, resizeAfter …}`
        // closure mixes for-init lets with in-loop block-lets), routing some
        // captures through the module env and others through per-closure env
        // means the closure body reads the non-env slots out of bounds. Skip
        // the whole closure in that case — its captures stay at slot -1 and
        // jm_transpile_func_expr falls back to the original per-closure path
        // for all of them.
        auto closure_qualifies = [&](JsFuncCollected* child) -> bool {
            for (int k = 0; k < child->capture_count; k++) {
                JsCaptureEntry* cap = &child->captures[k];
                if (!cap->is_let_const) continue;  // non-let captures are unaffected
                if (!capture_qualifies(cap->name, cap->is_let_const, cap->is_nfe_binding)) {
                    return false;
                }
            }
            return true;
        };

        auto include_capture = [&](JsFuncCollected* child, int k) -> bool {
            JsCaptureEntry* cap = &child->captures[k];
            return capture_qualifies(cap->name, cap->is_let_const, cap->is_nfe_binding);
        };

        for (int ci = 0; ci < mt->func_count; ci++) {
            JsFuncCollected* child = &mt->func_entries[ci];
            if (child->parent_index != -1) continue;
            if (child->capture_count == 0) continue;
            if (!closure_qualifies(child)) continue;
            for (int k = 0; k < child->capture_count; k++) {
                if (!include_capture(child, k)) continue;
                jm_name_set_add(scope_vars, child->captures[k].name);
            }
        }

        int total = (int)hashmap_count(scope_vars);
        if (total > 0) {
            mt->module_fc.has_scope_env = true;
            mt->module_fc.scope_env_count = total;
            mt->module_fc.scope_env_normal_count = total;
            mt->module_fc.parent_index = -2;  // sentinel: module body
            mt->module_fc.scope_env_names = (char(*)[64])mem_calloc(
                total + 2, 64, MEM_CAT_JS_RUNTIME);

            // Deterministic fill: iterate children in collection order
            hashmap_clear(scope_vars, false);
            int fill_idx = 0;
            for (int ci = 0; ci < mt->func_count; ci++) {
                JsFuncCollected* child = &mt->func_entries[ci];
                if (child->parent_index != -1) continue;
                if (!closure_qualifies(child)) continue;
                for (int k = 0; k < child->capture_count; k++) {
                    if (!include_capture(child, k)) continue;
                    if (!jm_name_set_has(scope_vars, child->captures[k].name)) {
                        jm_name_set_add(scope_vars, child->captures[k].name);
                        snprintf(mt->module_fc.scope_env_names[fill_idx], 64,
                            "%s", child->captures[k].name);
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
                    for (int s = 0; s < total; s++) {
                        if (strcmp(child->captures[k].name, mt->module_fc.scope_env_names[s]) == 0) {
                            child->captures[k].scope_env_slot = s;
                            break;
                        }
                    }
                }
            }

            log_debug("js-mir: Phase 1.7.5: module scope env with %d slots", total);
            for (int s = 0; s < total; s++) {
                log_debug("js-mir:   module_scope_env[%d] = '%s'", s, mt->module_fc.scope_env_names[s]);
            }
        }

        hashmap_free(scope_vars);
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
                    int grandparent_slot = parent_fc->captures[c].scope_env_slot;
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
    for (int fi = 0; fi < mt->func_count; fi++) {
        JsFuncCollected* parent_fc = &mt->func_entries[fi];
        parent_fc->has_parent_env_link = false;
        if (!parent_fc->has_scope_env || parent_fc->scope_env_count == 0) continue;
        if (parent_fc->reuse_parent_env) continue;  // Phase 1.7b already handles pure-transitive
        if (parent_fc->capture_count == 0) continue; // no captures = no transitive vars possible

        // Collect body locals for this function to distinguish locals from transitive captures
        struct hashmap* parent_locals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        JsFunctionNode* parent_fn = parent_fc->node;
        if (parent_fn && parent_fn->body) {
            jm_collect_visible_function_scope_names(parent_fn->body, parent_fc->is_strict, parent_locals, true);
            // Also add parameters as locals
            JsAstNode* pp = parent_fn->params;
            while (pp) {
                char pname[128];
                jm_get_param_name(pp, 0, pname, sizeof(pname));
                if (pname[0]) {
                    JsNameSetEntry pentry;
                    snprintf(pentry.name, sizeof(pentry.name), "_js_%s", pname);
                    hashmap_set(parent_locals, &pentry);
                }
                pp = pp->next;
            }
        }

        // Check if scope env has any transitive captures (vars that are also in parent_fc's captures)
        // Only count captures that the parent reads from its own parent's scope env
        // (scope_env_slot >= 0), NOT module vars read via js_get_module_var.
        // Also exclude vars that are LOCAL to the parent (shadowing the capture).
        bool has_transitive = false;
        bool has_local = false;
        for (int s = 0; s < parent_fc->scope_env_count; s++) {
            bool is_capture = false;
            // Check if this scope env var is a local of the parent (including function declarations)
            JsNameSetEntry local_lookup;
            snprintf(local_lookup.name, sizeof(local_lookup.name), "%s", parent_fc->scope_env_names[s]);
            bool is_parent_local = (hashmap_get(parent_locals, &local_lookup) != NULL);
            if (!is_parent_local) {
                for (int c = 0; c < parent_fc->capture_count; c++) {
                    if (strcmp(parent_fc->scope_env_names[s], parent_fc->captures[c].name) == 0) {
                        // Only transitive if parent reads this from its own parent's scope env
                        if (parent_fc->captures[c].scope_env_slot >= 0) {
                            is_capture = true;
                        }
                        break;
                    }
                }
            }
            if (is_capture) has_transitive = true;
            else has_local = true;
        }

        hashmap_free(parent_locals);

        if (!has_transitive || !has_local) continue; // pure-local or pure-transitive (handled by 1.7b)

        // Mixed scope env: add parent env link at the LAST slot (no shifting needed)
        parent_fc->has_parent_env_link = true;
        int parent_env_link_slot = parent_fc->scope_env_count; // last slot = parent env pointer
        // scope_env_names was allocated with +2 extra slots for this
        snprintf(parent_fc->scope_env_names[parent_fc->scope_env_count], 64, "__parent_env__");
        parent_fc->scope_env_count++;

        // Re-collect locals for grandparent_slot assignment (reuse same logic)
        struct hashmap* parent_locals2 = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        if (parent_fn && parent_fn->body) {
            jm_collect_visible_function_scope_names(parent_fn->body, parent_fc->is_strict, parent_locals2, true);
            JsAstNode* pp2 = parent_fn->params;
            while (pp2) {
                char pname2[128];
                jm_get_param_name(pp2, 0, pname2, sizeof(pname2));
                if (pname2[0]) {
                    JsNameSetEntry pe2;
                    snprintf(pe2.name, sizeof(pe2.name), "_js_%s", pname2);
                    hashmap_set(parent_locals2, &pe2);
                }
                pp2 = pp2->next;
            }
        }

        // For transitive captures in direct children, set grandparent_slot
        // NO slot shifting needed — existing slots remain unchanged
        for (int ci = 0; ci < mt->func_count; ci++) {
            JsFuncCollected* child = &mt->func_entries[ci];
            if (child->parent_index != fi) continue;
            if (child->capture_count == 0) continue;

            for (int k = 0; k < child->capture_count; k++) {
                // Check if this capture name is a LOCAL of the parent — if so, skip
                JsNameSetEntry ll;
                snprintf(ll.name, sizeof(ll.name), "%s", child->captures[k].name);
                if (hashmap_get(parent_locals2, &ll)) continue;

                // Check if this capture is a transitive capture (also in parent_fc's captures)
                // Only for captures the parent reads from its own parent's scope env
                for (int pc = 0; pc < parent_fc->capture_count; pc++) {
                    if (strcmp(child->captures[k].name, parent_fc->captures[pc].name) == 0) {
                        if (parent_fc->captures[pc].scope_env_slot < 0) break; // module var, skip
                        // This is transitive — child should read from grandparent env
                        child->captures[k].grandparent_slot = parent_fc->captures[pc].scope_env_slot;
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
                if (fc->param_types[j] == LMD_TYPE_INT || fc->param_types[j] == LMD_TYPE_FLOAT) {
                    has_typed_param = true; break;
                }
            }
            if (has_typed_param) jm_p6_reinfer_return_type(fc);
        }
        // P1: Compute native eligibility here (Phase 1.75) rather than lazily in jm_define_function.
        // This allows jm_resolve_native_call() (which checks has_native_version) to see the flag
        // when transpiling earlier functions that call later-defined native functions, enabling
        // `let x = f(...)` to propagate f's return type into x's variable type.
        bool eligible = (fc->capture_count == 0 && fc->param_count > 0 &&
                         fc->param_count <= 16 && !fc->uses_arguments &&
                         !fc->has_non_simple_params &&
                         (fc->return_type == LMD_TYPE_INT || fc->return_type == LMD_TYPE_FLOAT));
        if (eligible) {
            for (int j = 0; j < fc->param_count; j++) {
                if (fc->param_types[j] != LMD_TYPE_INT && fc->param_types[j] != LMD_TYPE_FLOAT) {
                    eligible = false;
                    break;
                }
            }
        }
        fc->has_native_version = eligible;
        if (eligible) {
            log_debug("js-mir P1/P4: %s eligible for native version (params: %d, ret: %s)",
                fc->name, fc->param_count,
                fc->return_type == LMD_TYPE_INT ? "INT" : "FLOAT");
        }

        // TCO eligibility: native-eligible function with at least one tail-recursive call
        fc->is_tco_eligible = false;
        if (eligible && jm_has_tail_call(fc->node->body, fc)) {
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
        // allocate evidence per function per param (max 16 params)
        P6NarrowEvidence (*evi)[16] = (P6NarrowEvidence (*)[16])mem_calloc(
            mt->func_count * 16, sizeof(P6NarrowEvidence), MEM_CAT_JS_RUNTIME);
        // walk program body (top-level calls)
        jm_p6_narrow_walk(mt, (JsAstNode*)program->body, evi);
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
            for (int p = 0; p < fc->param_count && p < 16; p++) {
                if (fc->param_types[p] != LMD_TYPE_ANY) continue;
                P6NarrowEvidence* e = &evi[i][p];
                int total = e->int_count + e->float_count + e->other_count;
                if (total == 0) continue; // never called
                if (e->other_count > 0) continue; // something non-numeric passed
                if (e->int_count > 0 && e->float_count == 0) {
                    fc->param_types[p] = LMD_TYPE_INT;
                    narrowed = true;
                    log_info("P6 narrow %s param[%d] → INT (calls: %d int, %d float, %d other)",
                             fc->name, p, e->int_count, e->float_count, e->other_count);
                } else if (e->float_count > 0 && e->int_count == 0) {
                    fc->param_types[p] = LMD_TYPE_FLOAT;
                    narrowed = true;
                    log_info("P6 narrow %s param[%d] → FLOAT (calls: %d int, %d float, %d other)",
                             fc->name, p, e->int_count, e->float_count, e->other_count);
                } else {
                    // mixed int+float → narrow to FLOAT (int is promotable)
                    fc->param_types[p] = LMD_TYPE_FLOAT;
                    narrowed = true;
                    log_info("P6 narrow %s param[%d] → FLOAT (mixed: %d int, %d float)",
                             fc->name, p, e->int_count, e->float_count);
                }
            }
            if (narrowed) {
                // re-infer return type now that params are typed
                jm_p6_reinfer_return_type(fc);
                // recompute native eligibility
                bool eligible = (fc->capture_count == 0 &&
                                 !(fc->node && fc->node->is_generator) &&
                                 !(fc->node && fc->node->is_async) &&
                                 !fc->has_non_simple_params &&
                                 fc->param_count <= 16);
                if (eligible) {
                    for (int p = 0; p < fc->param_count; p++) {
                        TypeId pt = fc->param_types[p];
                        if (pt != LMD_TYPE_INT && pt != LMD_TYPE_FLOAT) {
                            eligible = false; break;
                        }
                    }
                    if (eligible) {
                        TypeId rt = fc->return_type;
                        if (rt != LMD_TYPE_INT && rt != LMD_TYPE_FLOAT)
                            eligible = false;
                    }
                }
                if (eligible && !fc->has_native_version) {
                    fc->has_native_version = true;
                    log_info("P6 enabled native version for %s (return_type=%d)", fc->name, fc->return_type);
                } else if (!eligible) {
                    fc->has_native_version = false;
                    fc->native_func_item = 0;
                }
            }
        }
        mem_free(evi);
    }

    // Phase 1.78: P4b constructor call-site type propagation.
    // For this.prop = param patterns where the field type is unknown, propagate
    // types from new ClassName(arg1, arg2, ...) call-site argument types.
    if (mt->class_count > 0) {
        // check if any class has untyped param-assigned properties
        bool needs_scan = false;
        for (int ci = 0; ci < mt->class_count && !needs_scan; ci++) {
            JsClassEntry* ce = &mt->class_entries[ci];
            if (!ce->constructor || !ce->constructor->fc) continue;
            JsFuncCollected* ctor_fc = ce->constructor->fc;
            for (int pi = 0; pi < ctor_fc->ctor_prop_count; pi++) {
                if (ctor_fc->ctor_prop_types[pi] == LMD_TYPE_NULL &&
                    ctor_fc->ctor_prop_param_idx[pi] >= 0) {
                    needs_scan = true; break;
                }
            }
        }
        if (needs_scan) {
            P4bCtorEvidence* cevi = (P4bCtorEvidence*)mem_calloc(
                mt->class_count * 16, sizeof(P4bCtorEvidence), MEM_CAT_JS_RUNTIME);
            // walk program body
            jm_p4b_ctor_walk(mt, (JsAstNode*)program->body, cevi);
            // walk all function bodies
            for (int i = 0; i < mt->func_count; i++) {
                JsFuncCollected* fc = &mt->func_entries[i];
                if (fc->node && fc->node->body)
                    jm_p4b_ctor_walk(mt, (JsAstNode*)fc->node->body, cevi);
            }
            // apply: propagate call-site consensus to ctor_prop_types
            for (int ci = 0; ci < mt->class_count; ci++) {
                JsClassEntry* ce = &mt->class_entries[ci];
                if (!ce->constructor || !ce->constructor->fc) continue;
                JsFuncCollected* ctor_fc = ce->constructor->fc;
                for (int pi = 0; pi < ctor_fc->ctor_prop_count; pi++) {
                    if (ctor_fc->ctor_prop_types[pi] != LMD_TYPE_NULL) continue;
                    int param_idx = ctor_fc->ctor_prop_param_idx[pi];
                    if (param_idx < 0) continue;
                    P4bCtorEvidence* e = &cevi[ci * 16 + param_idx];
                    int total = e->int_count + e->float_count + e->other_count;
                    if (total == 0 || e->other_count > 0) continue;
                    if (e->float_count > 0) {
                        ctor_fc->ctor_prop_types[pi] = LMD_TYPE_FLOAT;
                    } else if (e->int_count > 0) {
                        ctor_fc->ctor_prop_types[pi] = LMD_TYPE_INT;
                    }
                    log_info("P4b: propagated %s.%.*s → %s (param[%d], %d call sites: %d int, %d float)",
                        ce->name ? ce->name->chars : "?",
                        ctor_fc->ctor_prop_lens[pi], ctor_fc->ctor_prop_ptrs[pi],
                        get_type_name(ctor_fc->ctor_prop_types[pi]),
                        param_idx, total, e->int_count, e->float_count);
                }
            }
            mem_free(cevi);
        }
    }

    for (int i = 0; i < mt->func_count; i++) {
        JsFuncCollected* fc = &mt->func_entries[i];
        if (!fc->func_item) {
            MIR_item_t fwd = MIR_new_forward(mt->ctx, fc->name);
            fc->func_item = fwd;
            jm_register_local_func(mt, fc->name, fwd);
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
    mt->current_func_item = main_item;
    mt->current_func = main_func;
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
    mt->func_except_label = 0;  // reset for js_main

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
                    MIR_reg_t key_reg = jm_box_string_literal(mt, js_name, strlen(js_name));
                    // Global lexical declarations are checked during script
                    // declaration instantiation and tracked separately from
                    // globalThis properties for later evalScript collision checks.
                    jm_call_void_1(mt, "js_evalscript_check_global_lex_decl",
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg));
                    jm_emit_exc_propagate_check(mt);
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
        int bulk_capacity = (int)hashmap_count(mt->module_consts);
        int* bulk_indices_no_global = NULL;
        int* bulk_indices_global = NULL;
        Item* bulk_keys_global = NULL;
        int bulk_no_global_count = 0;
        int bulk_global_count = 0;
        if (bulk_capacity > 0) {
            bulk_indices_no_global = (int*)pool_calloc(mt->tp->ast_pool, sizeof(int) * bulk_capacity);
            bulk_indices_global = (int*)pool_calloc(mt->tp->ast_pool, sizeof(int) * bulk_capacity);
            bulk_keys_global = (Item*)pool_calloc(mt->tp->ast_pool, sizeof(Item) * bulk_capacity);
        }
        size_t var_iter = 0; void* var_item;
        while (hashmap_iter(mt->module_consts, &var_iter, &var_item)) {
            JsModuleConstEntry* mce = (JsModuleConstEntry*)var_item;
            if (mce->const_type == MCONST_MODVAR &&
                mce->var_kind == JS_VAR_VAR && !mce->is_implicit_global &&
                (int)mce->int_val >= preamble_var_limit) {
                bool needs_eval_bridge = mt->is_eval_direct && mce->is_nested_func_hoist;
                bool should_define_global = !mt->is_module && !mt->is_eval_direct &&
                    !mce->is_iife_var && !mce->is_implicit_global;
                if (!needs_eval_bridge && bulk_capacity > 0) {
                    if (should_define_global) {
                        const char* js_name = mce->name;
                        if (strncmp(js_name, "_js_", 4) == 0) js_name += 4;
                        NamePool* np = (context && context->name_pool) ? context->name_pool : mt->tp->name_pool;
                        String* interned = name_pool_create_len(np, js_name, (int)strlen(js_name));
                        bulk_indices_global[bulk_global_count] = (int)mce->int_val;
                        bulk_keys_global[bulk_global_count] = (Item){.item = s2it(interned)};
                        bulk_global_count++;
                    } else {
                        bulk_indices_no_global[bulk_no_global_count++] = (int)mce->int_val;
                    }
                    continue;
                }
                MIR_reg_t init_val = 0;
                if (mt->is_eval_direct && mce->is_nested_func_hoist) {
                    const char* js_name = mce->name;
                    if (strncmp(js_name, "_js_", 4) == 0) js_name += 4;
                    MIR_reg_t key_reg = jm_box_string_literal(mt, js_name, strlen(js_name));
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
                    MIR_reg_t key_reg = jm_box_string_literal(mt, js_name, strlen(js_name));
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
        if (bulk_no_global_count > 0) {
            jm_call_void_4(mt, "js_init_module_vars_undefined_bulk",
                MIR_T_P, MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)bulk_indices_no_global),
                MIR_T_P, MIR_new_int_op(mt->ctx, 0),
                MIR_T_I64, MIR_new_int_op(mt->ctx, bulk_no_global_count),
                MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
        }
        if (bulk_global_count > 0) {
            jm_call_void_4(mt, "js_init_module_vars_undefined_bulk",
                MIR_T_P, MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)bulk_indices_global),
                MIR_T_P, MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)bulk_keys_global),
                MIR_T_I64, MIR_new_int_op(mt->ctx, bulk_global_count),
                MIR_T_I64, MIR_new_int_op(mt->ctx, 1));
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
            snprintf(lex_lookup.name, sizeof(lex_lookup.name), "%s", mce->name);
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
            MIR_reg_t key_reg = jm_box_string_literal(mt, js_name, strlen(js_name));
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
        extern int g_tla_module_depth;
        extern int js_dynamic_import_suppress_module_drain;
        // Body split applies only to statically loaded nested modules. The
        // entry module (depth == 1) keeps its existing sync-with-microtask
        // semantics so the top-level-ticks family stays observable. Modules
        // loaded via js_dynamic_import (suppress > 0) also keep the sync path
        // so `await import('…')` callers see the fully-evaluated namespace.
        if (mt->is_module && mt->in_main && mt->filename && g_tla_module_depth >= 2 &&
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
                    char vname[128];
                    snprintf(vname, sizeof(vname), "_js_%.*s", (int)fn->name->len, fn->name->chars);
                    MIR_reg_t var_reg = jm_new_reg(mt, vname, MIR_T_I64);
                    MIR_reg_t fn_item = jm_call_2(mt, "js_new_function", MIR_T_I64,
                        MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, pc));
                    jm_emit_set_function_name(mt, fn_item, fn->name->chars, fc->formal_length);
                    jm_emit_set_function_source(mt, fn_item, fn);
                    // v20: Mark generator functions
                    if (fn->is_generator) {
                        if (fn->is_async) {
                            jm_call_void_1(mt, "js_mark_async_generator_func",
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_item));
                        } else {
                            jm_call_void_1(mt, "js_mark_generator_func",
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_item));
                        }
                    } else if (fn->is_async) {
                        jm_call_void_1(mt, "js_mark_async_func",
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_item));
                    }
                    // v30: Mark strict mode functions
                    if (fc->is_strict) {
                        jm_call_void_1(mt, "js_mark_strict_func",
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_item));
                    }
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, var_reg),
                        MIR_new_reg_op(mt->ctx, fn_item)));
                    // For reassigned functions, do NOT create a local register;
                    // all reads must go through js_get_module_var to see updates
                    // from self-reassignment inside the function body.
                    if (!fc->is_reassigned)
                        jm_set_var(mt, vname, var_reg);
                    // Persist function object as module var so it survives after
                    // js_main returns and is accessible by eval()/new Function()
                    {
                        JsModuleConstEntry pmlookup;
                        snprintf(pmlookup.name, sizeof(pmlookup.name), "%s", vname);
                        JsModuleConstEntry* pmc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &pmlookup);
                        if (pmc && pmc->const_type == MCONST_MODVAR) {
                            jm_call_void_2(mt, "js_set_module_var",
                                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)pmc->int_val),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, var_reg));
                        }
                    }
                    // Sloppy-mode eval: export function declarations to the eval var-env.
                    if (mt->is_eval_direct && !mt->is_global_strict) {
                        MIR_reg_t fk = jm_box_string_literal(mt, fn->name->chars, (int)fn->name->len);
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
                        // evalScript executes as a Script, not ordinary eval; even
                        // with an eval-local frame, the function binding is global.
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
                        // $262.evalScript performs Script global declaration instantiation,
                        // so function declarations create non-configurable globals. Ordinary
                        // direct eval keeps the usual configurable eval bindings.
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
                        MIR_reg_t fk = jm_box_string_literal(mt, fn->name->chars, (int)fn->name->len);
                        jm_call_void_3(mt, "js_define_global_property_v",
                            MIR_T_I64, MIR_new_int_op(mt->ctx, 2),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, fk),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, var_reg));
                    }
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
            char vname[128];
            snprintf(vname, sizeof(vname), "_js_%.*s", (int)ce->name->len, ce->name->chars);
            // Create a variable holding null placeholder.
            // Actual class instantiation is handled by jm_transpile_new_expr.
            MIR_reg_t var_reg = jm_new_reg(mt, vname, MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, var_reg),
                MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
            jm_set_var(mt, vname, var_reg);
            // Also store null to module var so closures see the initial value
            JsModuleConstEntry mclookup;
            snprintf(mclookup.name, sizeof(mclookup.name), "%s", vname);
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
        blk_wrapper.base.node_type = JS_AST_NODE_BLOCK_STATEMENT;
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
                    char vname[128];
                    snprintf(vname, sizeof(vname), "_js_%.*s", (int)fn->name->len, fn->name->chars);
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
                                snprintf(mclookup.name, sizeof(mclookup.name), "%s", fc->captures[ci].name);
                                JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mclookup);
                                if (mc) {
                                    found_mc = true;
                                    MIR_reg_t const_val;
                                    switch (mc->const_type) {
                                    case MCONST_INT:
                                        const_val = jm_box_int_const(mt, mc->int_val);
                                        break;
                                    case MCONST_FLOAT: {
                                        MIR_reg_t d = jm_new_reg(mt, "mconst_d", MIR_T_D);
                                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                                            MIR_new_reg_op(mt->ctx, d),
                                            MIR_new_double_op(mt->ctx, mc->float_val)));
                                        const_val = jm_box_float(mt, d);
                                        break;
                                    }
                                    case MCONST_NULL:
                                        const_val = jm_emit_null(mt);
                                        break;
                                    case MCONST_UNDEFINED: {
                                        const_val = jm_new_reg(mt, "mundef", MIR_T_I64);
                                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                            MIR_new_reg_op(mt->ctx, const_val),
                                            MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED)));
                                        break;
                                    }
                                    case MCONST_BOOL: {
                                        const_val = jm_new_reg(mt, "mbool", MIR_T_I64);
                                        uint64_t bval = mc->int_val ? ITEM_TRUE_VAL : ITEM_FALSE_VAL;
                                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                            MIR_new_reg_op(mt->ctx, const_val), MIR_new_int_op(mt->ctx, (int64_t)bval)));
                                        break;
                                    }
                                    case MCONST_CLASS:
                                        const_val = jm_call_1(mt, "js_get_module_var", MIR_T_I64,
                                            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mc->int_val));
                                        break;
                                    case MCONST_FUNC: {
                                        int fii = (int)mc->int_val;
                                        if (fii >= 0 && fii < mt->func_count && mt->func_entries[fii].func_item) {
                                            const_val = jm_create_func_or_closure(mt, &mt->func_entries[fii]);
                                        } else {
                                            const_val = jm_emit_null(mt);
                                        }
                                        break;
                                    }
                                    case MCONST_MODVAR:
                                        const_val = jm_call_1(mt, "js_get_module_var", MIR_T_I64,
                                            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mc->int_val));
                                        break;
                                    }
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
                    MIR_reg_t fn_item = jm_call_4(mt, "js_new_closure", MIR_T_I64,
                        MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, pc),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, env),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, fc->capture_count));
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

                    // Persist to module var table so sibling closures that read
                    // via MCONST_MODVAR see the correct function value
                    {
                        JsModuleConstEntry pmlookup;
                        snprintf(pmlookup.name, sizeof(pmlookup.name), "%s", vname);
                        JsModuleConstEntry* pmc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &pmlookup);
                        if (pmc && pmc->const_type == MCONST_MODVAR) {
                            jm_call_void_2(mt, "js_set_module_var",
                                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)pmc->int_val),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, var_reg));
                        }
                    }
                    // Sloppy-mode eval: export capturing function declarations to the eval var-env.
                    if (mt->is_eval_direct && !mt->is_global_strict) {
                        MIR_reg_t fk = jm_box_string_literal(mt, fn->name->chars, (int)fn->name->len);
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
                        // evalScript executes as a Script, not ordinary eval; even
                        // with an eval-local frame, the function binding is global.
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
                        // $262.evalScript performs Script global declaration instantiation,
                        // so function declarations create non-configurable globals. Ordinary
                        // direct eval keeps the usual configurable eval bindings.
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
                        MIR_reg_t fk = jm_box_string_literal(mt, fn->name->chars, (int)fn->name->len);
                        jm_call_void_3(mt, "js_define_global_property_v",
                            MIR_T_I64, MIR_new_int_op(mt->ctx, 2),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, fk),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, var_reg));
                    }
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
                    // Update local variable
                    char vname[128];
                    snprintf(vname, sizeof(vname), "_js_%.*s", (int)cls_node->name->len, cls_node->name->chars);
                    JsMirVarEntry* ve = jm_find_var(mt, vname);
                    if (ve) {
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, ve->reg),
                            MIR_new_reg_op(mt->ctx, cls_obj)));
                    }
                    // Store class object in module var
                    JsModuleConstEntry mclookup;
                    snprintf(mclookup.name, sizeof(mclookup.name), "%s", vname);
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
                        MIR_reg_t class_key = jm_box_string_literal(mt, cls_node->name->chars, (int)cls_node->name->len);
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
                                MIR_reg_t home_key = jm_box_string_literal(mt, "__home_class__", 14);
                                jm_call_3(mt, "js_property_set", MIR_T_I64,
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_item),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, home_key),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, parent_cls_obj));
                                MIR_reg_t mk;
                                if (me->computed && me->key_expr) {
                                    mk = jm_transpile_box_item(mt, me->key_expr);
                                    // Phase-5C: no key wrap.
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

                    // Register own static methods as properties on the class object
                    // so they can be called dynamically (e.g., ns[$buildXFAObject](name, attrs))
                    for (int mi = 0; mi < ce->method_count; mi++) {
                        JsClassMethodEntry* me = &ce->methods[mi];
                        if (!me->is_static || me->is_constructor) continue;
                        if (!me->fc || !me->fc->func_item) continue;

                        // Build the function value
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
                        MIR_reg_t home_key = jm_box_string_literal(mt, "__home_class__", 14);
                        jm_call_3(mt, "js_property_set", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_item),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, home_key),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));

                        // Determine the property key
                        MIR_reg_t mk;
                        if (me->computed && me->key_expr) {
                            // computed key like [$buildXFAObject] — evaluate the key expression at runtime
                            mk = jm_transpile_box_item(mt, me->key_expr);
                            // Phase-5C: no key wrap.
                        } else if (me->name) {
                            mk = jm_box_string_literal(mt, me->name->chars, (int)me->name->len);
                        } else {
                            continue; // no name and not computed — skip
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

                    // Store __ctor__ on class object for dynamic instantiation (new C())
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
                            MIR_reg_t ctor_key = jm_box_string_literal(mt, "__ctor__", 8);
                            jm_call_3(mt, "js_property_set", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, ctor_key),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, ctor_fn));
                        }
                        jm_emit_class_ctor_shape_metadata(mt, cls_obj, ce);
                    }

                    // Create __instance_proto__ with all instance methods
                    {
                        MIR_reg_t proto_obj = class_proto_obj;
                        jm_call_void_2(mt, "js_set_default_constructor_property",
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_obj),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
                        // Set up prototype's __proto__ chain for instanceof on parent classes
                        {
                            JsClassEntry* sc = ce->superclass;
                            MIR_reg_t last_proto = proto_obj;
                            if (sc) {
                                // Link prototype to parent's actual .prototype for identity correctness
                                JsIdentifierNode tmp_id2;
                                memset(&tmp_id2, 0, sizeof(tmp_id2));
                                tmp_id2.base.node_type = JS_AST_NODE_IDENTIFIER;
                                tmp_id2.name = sc->name;
                                MIR_reg_t super_val = jm_transpile_box_item(mt, (JsAstNode*)&tmp_id2);
                                MIR_reg_t sp_key = jm_box_string_literal(mt, "prototype", 9);
                                MIR_reg_t sp_proto = jm_call_2(mt, "js_property_get", MIR_T_I64,
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, super_val),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_key));
                                jm_call_void_1(mt, "js_check_class_prototype_parent",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_proto));
                                jm_emit_exc_propagate_check(mt);
                                jm_call_void_2(mt, "js_set_prototype",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, last_proto),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_proto));
                                ctor_super_val = super_val;
                            }
                            // v20: Handle builtin superclass (Error, etc.) when no JsClassEntry
                            if (!ce->superclass && ce->node && ce->node->superclass &&
                                ce->node->superclass->node_type == JS_AST_NODE_IDENTIFIER) {
                                JsIdentifierNode* super_id = (JsIdentifierNode*)ce->node->superclass;
                                if (super_id->name) {
                                    const char* sname = super_id->name->chars;
                                    int slen = (int)super_id->name->len;
                                    bool is_error_class =
                                        (slen == 5 && strncmp(sname, "Error", 5) == 0) ||
                                        (slen == 9 && strncmp(sname, "TypeError", 9) == 0) ||
                                        (slen == 9 && strncmp(sname, "EvalError", 9) == 0) ||
                                        (slen == 8 && strncmp(sname, "URIError", 8) == 0) ||
                                        (slen == 10 && strncmp(sname, "RangeError", 10) == 0) ||
                                        (slen == 11 && strncmp(sname, "SyntaxError", 11) == 0) ||
                                        (slen == 14 && strncmp(sname, "ReferenceError", 14) == 0);
                                    if (is_error_class) {
                                        // Use the actual NativeError.prototype singleton
                                        JsIdentifierNode tmp_sid2;
                                        memset(&tmp_sid2, 0, sizeof(tmp_sid2));
                                        tmp_sid2.base.node_type = JS_AST_NODE_IDENTIFIER;
                                        tmp_sid2.name = super_id->name;
                                        MIR_reg_t super_ctor2 = jm_transpile_box_item(mt, (JsAstNode*)&tmp_sid2);
                                        MIR_reg_t sp_key2 = jm_box_string_literal(mt, "prototype", 9);
                                        MIR_reg_t err_proto = jm_call_2(mt, "js_property_get", MIR_T_I64,
                                            MIR_T_I64, MIR_new_reg_op(mt->ctx, super_ctor2),
                                            MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_key2));
                                        jm_call_void_1(mt, "js_check_class_prototype_parent",
                                            MIR_T_I64, MIR_new_reg_op(mt->ctx, err_proto));
                                        jm_emit_exc_propagate_check(mt);
                                        jm_call_void_2(mt, "js_set_prototype",
                                            MIR_T_I64, MIR_new_reg_op(mt->ctx, last_proto),
                                            MIR_T_I64, MIR_new_reg_op(mt->ctx, err_proto));
                                        ctor_super_val = super_ctor2;
                                    } else {
                                        MIR_reg_t super_val = jm_transpile_box_item(mt, (JsAstNode*)super_id);
                                        MIR_reg_t sp_key = jm_box_string_literal(mt, "prototype", 9);
                                        MIR_reg_t sp_proto = jm_call_2(mt, "js_property_get", MIR_T_I64,
                                            MIR_T_I64, MIR_new_reg_op(mt->ctx, super_val),
                                            MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_key));
                                        jm_call_void_1(mt, "js_check_class_prototype_parent",
                                            MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_proto));
                                        jm_emit_exc_propagate_check(mt);
                                        jm_call_void_2(mt, "js_set_prototype",
                                            MIR_T_I64, MIR_new_reg_op(mt->ctx, last_proto),
                                            MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_proto));
                                        ctor_super_val = super_val;
                                    }
                                }
                            }
                            // v21: Handle member-expression superclass in class expressions
                            if (!ce->superclass && ce->node && ce->node->superclass &&
                                ce->node->superclass->node_type != JS_AST_NODE_IDENTIFIER &&
                                                                ce->node->superclass->node_type != JS_AST_NODE_NULL &&
                                                                !(ce->node->superclass->node_type == JS_AST_NODE_LITERAL &&
                                                                    ((JsLiteralNode*)ce->node->superclass)->literal_type == JS_LITERAL_NULL)) {
                                MIR_reg_t super_val = jm_transpile_box_item(mt, ce->node->superclass);
                                jm_call_void_1(mt, "js_check_class_heritage_constructor",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, super_val));
                                jm_emit_exc_propagate_check(mt);
                                MIR_reg_t sp_key = jm_box_string_literal(mt, "prototype", 9);
                                MIR_reg_t sp_proto = jm_call_2(mt, "js_property_get", MIR_T_I64,
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, super_val),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_key));
                                jm_call_void_1(mt, "js_check_class_prototype_parent",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_proto));
                                jm_emit_exc_propagate_check(mt);
                                jm_call_void_2(mt, "js_set_prototype",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, last_proto),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sp_proto));
                                ctor_super_val = super_val;
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
                            if (heritage_is_null) {
                                MIR_reg_t null_proto = jm_emit_null(mt);
                                jm_call_void_2(mt, "js_set_prototype",
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, last_proto),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, null_proto));
                            }
                        }
                        // Add own instance methods
                        for (int mi = 0; mi < ce->method_count; mi++) {
                            JsClassMethodEntry* me = &ce->methods[mi];
                            if (me->is_constructor || me->is_static) continue;
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
                            MIR_reg_t home_key = jm_box_string_literal(mt, "__home_class__", 14);
                            jm_call_3(mt, "js_property_set", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_item),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, home_key),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
                            MIR_reg_t mk = 0;
                            if (me->computed && me->key_expr) {
                                mk = jm_transpile_box_item(mt, me->key_expr);
                                // Phase-5C: no key wrap.
                            } else if (me->name) {
                                String* method_name = jm_class_private_name(mt, ce, me->name);
                                mk = jm_box_string_literal(mt, method_name->chars, (int)method_name->len);
                            }
                            if (!mk) continue;
                            jm_emit_install_method_or_accessor(mt, proto_obj, mk, fn_item,
                                me->is_getter, me->is_setter);
                        }
                        // Store __instance_proto__ on class object
                        MIR_reg_t ip_key = jm_box_string_literal(mt, "__instance_proto__", 18);
                        jm_call_3(mt, "js_property_set", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, ip_key),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_obj));
                        jm_call_void_2(mt, "js_set_default_constructor_property",
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, proto_obj),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj));
                        // Mark all prototype methods as non-enumerable (ES spec)
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
                                    // batch lowering has the same generator hazard as normal
                                    // class lowering: a yielding computed key can suspend before
                                    // subsequent class setup consumes cls_obj again.
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
                                    // preserve cls_obj across `[yield ...]` instance field keys so
                                    // class metadata and static initialization see the real class.
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

                    // Emit static field initializers at the class's source position.
                    // Static fields may reference functions/variables declared before.
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
                            // computed static field: evaluate key and value, set on class object
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
                            log_debug("js-mir: emitting static field init %.*s.%.*s → module_var[%d]",
                                (int)(ce->name ? ce->name->len : 0),
                                ce->name ? ce->name->chars : "<anon>",
                                (int)sf->name->len, sf->name->chars,
                                sf->module_var_index);
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
                    // Emit static block bodies
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
            stmt = stmt->next;
            continue;
        }

        // Module mode: handle export default <expression>
        if (current_export && current_export->is_default && mt->is_module) {
            MIR_reg_t val = jm_transpile_box_item(mt, actual_stmt);
            MIR_reg_t key = jm_box_string_literal(mt, "default", 7);
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
                jm_call_2(mt, "js_p5_module_await", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, p7d_spec_split),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, arg_val));
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
        jm_emit_exc_propagate_check(mt);

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
            MIR_reg_t key_reg = jm_box_string_literal(mt, js_name, strlen(js_name));
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

    // Exception landing pad for js_main: return null if exception is pending
    if (mt->func_except_label) {
        jm_emit_label(mt, mt->func_except_label);
        jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
            MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
    }

    jm_pop_scope(mt);
    MIR_finish_func(mt->ctx);
    MIR_finish_module(mt->ctx);

    // Load module for linking
    MIR_load_module(mt->ctx, mt->module);
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

    transpile_js_mir_ast(mt, js_ast);

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
    // Detach name_pool and ast_pool from the transpiler so they survive cleanup.
    // JIT code embeds raw String* pointers interned in the name pool.
    // Freeing the pool would leave dangling pointers in the generated code.
    tp->name_pool = NULL;
    tp->ast_pool = NULL;
    js_transpiler_destroy(tp);

    if (!js_main) {
        log_error("js-parallel: failed to find js_main for '%s'", node->path);
        MIR_finish(ctx);
        return false;
    }

    node->mir_ctx = ctx;
    node->js_main_func = (void*)js_main;
    node->compiled = true;
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
    ts_parser_delete(parser);
    hashmap_free(path_map);

    int import_count = count - 1;
    if (import_count < 2) {
        // not enough modules to justify parallelism — let serial jm_load_imports handle it
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

    // compile level by level (leaves first), then execute serially at each level
    long ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpus < 1) ncpus = 1;
    if (ncpus > 8) ncpus = 8;

    int precompiled = 0;

    for (int level = 0; level <= max_depth; level++) {
        // collect modules at this depth
        int batch_indices[64];
        int batch_count = 0;
        for (int i = 1; i < count && batch_count < 64; i++) {
            if (nodes[i].depth == level && nodes[i].source)
                batch_indices[batch_count++] = i;
        }
        if (batch_count == 0) continue;

        // parallel compile phase
        if (batch_count == 1) {
            // single module — compile inline without thread overhead
            jm_compile_js_module(runtime, &nodes[batch_indices[0]]);
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

        // serial execute phase: run js_main for each compiled module at this level
        for (int b = 0; b < batch_count; b++) {
            int idx = batch_indices[b];
            if (!nodes[idx].compiled) continue;

            typedef Item (*js_main_func_t)(Context*);
            js_main_func_t js_main = (js_main_func_t)nodes[idx].js_main_func;

            Item* prev_mv = js_get_active_module_vars();
            js_set_active_module_vars(js_alloc_module_vars());
            Item namespace_obj = js_main((Context*)context);
            js_set_active_module_vars(prev_mv);

            // register in module cache
            String* spec_str = heap_create_name(nodes[idx].path, strlen(nodes[idx].path));
            Item spec_item = (Item){.item = s2it(spec_str)};
            js_module_register(spec_item, namespace_obj);

            // defer MIR cleanup (keep function pointers alive)
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

Item transpile_js_module_to_mir(Runtime* runtime, const char* js_source, const char* filename) {
    log_debug("js-mir: compiling module '%s'", filename ? filename : "<module>");
    extern int js_dynamic_import_suppress_module_drain;
    // Js57 P4 (Track B3): bump depth at the very start so jm_load_imports
    // nested calls see depth >= 2 while the outermost transpile sits at 1;
    // the matching exit at the end of the function drains continuations only
    // when this is the outermost call.
    extern void js_tla_enter_module(void);
    js_tla_enter_module();
    Runtime* prev_source_runtime = js_source_runtime;
    js_source_runtime = runtime;

    JsTranspiler* tp = js_transpiler_create(runtime);
    if (!tp) {
        log_error("js-mir: module: failed to create transpiler for '%s'", filename);
        js_source_runtime = prev_source_runtime;
        return ItemNull;
    }

    extern void js_tla_exit_module(void);

    if (!js_transpiler_parse(tp, js_source, strlen(js_source))) {
        // Js57 P7b: parse failure is a SyntaxError. Return ITEM_ERROR (not
        // ItemNull) so the batch driver short-circuits its post-test global
        // probes (async_required check), which SEGV when the heap was never
        // initialized for this test.
        log_error("js-mir: module: parse failed for '%s'", filename);
        js_transpiler_destroy(tp);
        js_source_runtime = prev_source_runtime;
        js_tla_exit_module();
        return (Item){.item = ITEM_ERROR};
    }

    TSNode root = ts_tree_root_node(tp->tree);
    JsAstNode* js_ast = build_js_ast(tp, root);
    if (!js_ast) {
        log_error("js-mir: module: AST build failed for '%s'", filename);
        js_transpiler_destroy(tp);
        js_source_runtime = prev_source_runtime;
        js_tla_exit_module();
        return (Item){.item = ITEM_ERROR};
    }

    // Js57 P7b: run early-error checks before any further compilation. The
    // module path previously skipped this and crashed on illegal forms like
    // `await 0;` (escaped await — contextually-reserved keyword written
    // with a unicode escape, which is a SyntaxError per the spec).
    int p7b_early_errors = js_check_early_errors(tp, js_ast);
    if (p7b_early_errors > 0) {
        log_error("js-mir: module: %d early error(s) for '%s'", p7b_early_errors, filename);
        js_transpiler_destroy(tp);
        js_source_runtime = prev_source_runtime;
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
    extern int g_tla_module_depth;
    extern int js_dynamic_import_suppress_module_drain;
    if (g_tla_module_depth >= 2 && js_dynamic_import_suppress_module_drain == 0) {
        int p7d_tla_count = 0;
        if (js_ast && js_ast->node_type == JS_AST_NODE_PROGRAM) {
            JsProgramNode* prog = (JsProgramNode*)js_ast;
            for (JsAstNode* stmt = prog->body; stmt; stmt = stmt->next) {
                p7d_tla_count += jm_count_awaits(stmt);
                if (p7d_tla_count > 0) break;
            }
        }
        if (p7d_tla_count > 0) {
            js_module_mark_has_tla(p7d_self_spec_item);
            log_debug("P7d-A: module '%s' has TLA (top-level await detected)", filename);
        }
    }

    // Recursively load this module's imports first
    jm_load_imports(runtime, js_ast, filename);

    MIR_context_t ctx = jit_init(g_js_mir_optimize_level);
    if (!ctx) {
        log_error("js-mir: module: MIR context init failed for '%s'", filename);
        js_transpiler_destroy(tp);
        js_source_runtime = prev_source_runtime;
        return ItemNull;
    }

    JsMirTranspiler* mt = jm_create_mir_transpiler(tp, ctx, filename, true, 64, 32, 16, "js-mir: module");
    if (!mt) {
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        js_source_runtime = prev_source_runtime;
        return ItemNull;
    }

    mt->module = MIR_new_module(ctx, "js_module");

    transpile_js_mir_ast(mt, js_ast);

    if (!jm_validate_mir_labels(ctx)) {
        log_error("js-mir: module: NULL labels detected for '%s'", filename);
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        js_source_runtime = prev_source_runtime;
        return (Item){.item = ITEM_ERROR};
    }

    MIR_link(ctx, g_mir_interp_mode ? MIR_set_interp_interface : MIR_set_gen_interface, import_resolver);

    typedef Item (*js_main_func_t)(Context*);
    js_main_func_t js_main = (js_main_func_t)find_func(ctx, (char*)"js_main");

    if (!js_main) {
        log_error("js-mir: module: failed to find js_main for '%s'", filename);
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        js_source_runtime = prev_source_runtime;
        return ItemNull;
    }

    // Execute module — js_main returns the namespace object in module mode.
    // Register the namespace before execution so dynamic import(self) and simple
    // circular edges observe the same live namespace object.
    String* spec_str = heap_create_name(filename, strlen(filename));
    Item spec_item = (Item){.item = s2it(spec_str)};
    Item namespace_obj = js_new_object();
    js_module_register(spec_item, namespace_obj);
    Input* module_input = Input::create(context->pool);
    js_runtime_set_input(module_input);

    // Allocate per-module variable storage and switch to it.
    Item* prev_module_vars = js_get_active_module_vars();
    Item prev_namespace = js_set_active_module_namespace(namespace_obj);
    Item* module_vars = js_alloc_module_vars();
    js_set_active_module_vars(module_vars);
    if (js_dynamic_import_suppress_module_drain <= 0) {
        js_event_loop_init();
    }
    // Set _lambda_rt for the duration of module execution. Template literals
    // (and many other JIT-emitted code paths) read it via the import resolver
    // to obtain the runtime pool. Without this, _lambda_rt stays at whatever
    // the previous test left it as — NULL on first run, stale otherwise —
    // and the template-literal path crashes dereferencing rt->pool. This
    // mirrors transpile_js_to_mir_core_len which sets it before its js_main.
    extern Context* _lambda_rt;
    Context* prev_lambda_rt = _lambda_rt;
    _lambda_rt = (Context*)context;

    // Js57 P7d: save the module's evaluation context (module_vars pointer +
    // namespace already on JsModule) and stash js_main as the deferred entry.
    // Used by the AEO drain to re-enter js_main with the same module-level
    // state when a deferred body / post-await chunk runs.
    js_module_save_context(spec_item, module_vars);
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
    if (p7d_pending > 0) {
        // Importer with pending TLA deps — skip js_main now; the AEO drain
        // will invoke it once all deps have settled.
        log_debug("P7d: module '%s' pending=%d — deferring body", filename, p7d_pending);
        // namespace stays as the empty/placeholder until deferred run completes.
    } else {
        namespace_obj = js_main((Context*)context);
    }
    // Js56 P9 (SIGSEGV fix): keep _lambda_rt set during the microtask drain.
    // Microtasks scheduled by the module (e.g. `Promise.resolve(0).then(...)`
    // chains in top-level-await tests) run inside js_event_loop_drain() and
    // their JIT'd handler bodies dereference _lambda_rt to access the runtime
    // pool. The old order restored _lambda_rt BEFORE the drain, so first-run
    // tests (prev_lambda_rt == NULL) hit a NULL-deref EXC_BAD_ACCESS in the
    // microtask. Restore after the drain instead. Same reasoning for
    // module_vars and module_namespace — handlers may read module-level vars.
    if (js_dynamic_import_suppress_module_drain <= 0) {
        js_event_loop_drain();
    }
    _lambda_rt = prev_lambda_rt;
    js_set_active_module_vars(prev_module_vars);
    js_set_active_module_namespace(prev_namespace);
    // Js57 P4 (Track B3): decrement and (at depth 0) flush queued post-await
    // chunks. Sits AFTER the namespace/module-vars/_lambda_rt restore so
    // continuations that touch module-level state read whichever active
    // namespace the outer caller had — typically the entry module's.
    extern void js_tla_exit_module(void);
    js_tla_exit_module();

    // Register the module with its resolved path as key. In normal execution
    // this re-registers the pre-created namespace; if compilation returned a
    // replacement namespace, keep the cache in sync with that result.
    js_module_register(spec_item, namespace_obj);

    // Also register in unified module registry for cross-language access
    module_register(filename, "js", namespace_obj, ctx);

    log_debug("js-mir: module '%s' loaded successfully", filename);

    // Cleanup transpiler state but DEFER MIR context cleanup
    // (module function pointers must remain alive for the main program)
    jm_destroy_mir_transpiler(mt);
    jm_defer_mir_cleanup(ctx);
    // Attach name_pool and ast_pool to the deferred entry so they are freed
    // when the deferred context is cleaned up.
    if (module_mir_context_count > 0) {
        module_mir_name_pools[module_mir_context_count - 1] = tp->name_pool;
        module_mir_ast_pools[module_mir_context_count - 1] = tp->ast_pool;
    }
    // Detach from transpiler so js_transpiler_destroy doesn't free them.
    tp->name_pool = NULL;
    tp->ast_pool = NULL;
    js_transpiler_destroy(tp);
    js_source_runtime = prev_source_runtime;

    return namespace_obj;
}

// ============================================================================
// Pre-scan AST for imports and recursively load all imported modules
// ============================================================================

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
                        extern void js_module_inherit_awaited_target(Item, Item);
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
                    Script* lambda_script = load_script(runtime, resolved, NULL, true);
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
                    extern void js_module_inherit_awaited_target(Item, Item);
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
// var declarations via the shared static js_module_vars[] array.
