#include "js_mir_internal.hpp"

uint64_t jm_name_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const JsNameSetEntry* e = (const JsNameSetEntry*)item;
    return hashmap_sip(e->name, strlen(e->name), seed0, seed1);
}

int jm_name_cmp(const void* a, const void* b, void* udata) {
    return strcmp(((const JsNameSetEntry*)a)->name, ((const JsNameSetEntry*)b)->name);
}

static bool jm_analysis_function_decl_is_direct_binding(JsFunctionNode* fn) {
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
    TSNode body = ts_node_child_by_field_name(grandparent, "body", 4);
    return !ts_node_is_null(body) &&
        ts_node_start_byte(body) == ts_node_start_byte(parent) &&
        ts_node_end_byte(body) == ts_node_end_byte(parent);
}

// Forward declare
void jm_collect_body_refs(JsAstNode* node, struct hashmap* refs);
void jm_collect_body_locals(JsAstNode* node, struct hashmap* locals, bool var_only);
void jm_collect_pattern_names(JsAstNode* pat, struct hashmap* names);

void jm_name_set_add(struct hashmap* set, const char* name) {
    JsNameSetEntry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.name, sizeof(e.name), "%s", name);
    // preserve existing var_kind if already in set
    JsNameSetEntry* existing = (JsNameSetEntry*)hashmap_get(set, &e);
    if (existing) return;  // already added
    hashmap_set(set, &e);
}

void jm_name_set_add_kind(struct hashmap* set, const char* name, int kind) {
    JsNameSetEntry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.name, sizeof(e.name), "%s", name);
    e.var_kind = kind;
    hashmap_set(set, &e);
}

static bool jm_binding_is_inside_range(uint32_t binding_start, uint32_t binding_end,
                                       uint32_t body_start, uint32_t body_end) {
    return (binding_start != 0 || binding_end != 0) &&
        binding_start >= body_start && binding_end <= body_end;
}

static void jm_name_set_add_ref(struct hashmap* set, const char* name, JsIdentifierNode* id,
                                uint32_t body_start, uint32_t body_end) {
    JsNameSetEntry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.name, sizeof(e.name), "%s", name);
    if (id && id->entry && id->entry->node) {
        JsAstNode* def = (JsAstNode*)id->entry->node;
        if (!ts_node_is_null(def->node)) {
            e.binding_start = ts_node_start_byte(def->node);
            e.binding_end = ts_node_end_byte(def->node);
        }
        e.var_kind = id->entry->is_const ? JS_VAR_CONST :
            (id->entry->is_lexical ? JS_VAR_LET : 0);
    }
    JsNameSetEntry* existing = (JsNameSetEntry*)hashmap_get(set, &e);
    if (existing) {
        bool existing_is_local = jm_binding_is_inside_range(
            existing->binding_start, existing->binding_end, body_start, body_end);
        bool candidate_is_local = jm_binding_is_inside_range(
            e.binding_start, e.binding_end, body_start, body_end);
        if (body_end > body_start && existing_is_local && !candidate_is_local) {
            // A name-keyed reference set must retain the free binding when a
            // nested lexical reuses its spelling; otherwise the lexical masks
            // the closure capture solely because it appears first in source.
            existing->binding_start = e.binding_start;
            existing->binding_end = e.binding_end;
            existing->var_kind = e.var_kind;
            return;
        }
        if ((existing->binding_start == 0 && existing->binding_end == 0) &&
            (e.binding_start != 0 || e.binding_end != 0)) {
            existing->binding_start = e.binding_start;
            existing->binding_end = e.binding_end;
        }
        if (existing->var_kind == 0 && e.var_kind != 0) existing->var_kind = e.var_kind;
        return;
    }
    hashmap_set(set, &e);
}

static void jm_name_set_add_binding(struct hashmap* set, const char* name, JsAstNode* binding_node) {
    JsNameSetEntry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.name, sizeof(e.name), "%s", name);
    if (binding_node && !ts_node_is_null(binding_node->node)) {
        e.binding_start = ts_node_start_byte(binding_node->node);
        e.binding_end = ts_node_end_byte(binding_node->node);
    }
    JsNameSetEntry* existing = (JsNameSetEntry*)hashmap_get(set, &e);
    if (existing) {
        if ((existing->binding_start == 0 && existing->binding_end == 0) &&
            (e.binding_start != 0 || e.binding_end != 0)) {
            existing->binding_start = e.binding_start;
            existing->binding_end = e.binding_end;
        }
        return;
    }
    hashmap_set(set, &e);
}

static void jm_name_set_add_existing(struct hashmap* set, JsNameSetEntry* entry) {
    if (!set || !entry) return;
    JsNameSetEntry* existing = (JsNameSetEntry*)hashmap_get(set, entry);
    if (existing) {
        if ((existing->binding_start == 0 && existing->binding_end == 0) &&
            (entry->binding_start != 0 || entry->binding_end != 0)) {
            existing->binding_start = entry->binding_start;
            existing->binding_end = entry->binding_end;
        }
        return;
    }
    hashmap_set(set, entry);
}

bool jm_name_set_has(struct hashmap* set, const char* name) {
    JsNameSetEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    return hashmap_get(set, &key) != NULL;
}

static bool jm_ref_is_local_binding(struct hashmap* locals, JsNameSetEntry* ref) {
    if (!locals || !ref) return false;
    JsNameSetEntry* local = (JsNameSetEntry*)hashmap_get(locals, ref);
    if (!local) return false;
    if (ref->binding_start == 0 && ref->binding_end == 0) return true;
    // A name-only local entry cannot shadow a reference already resolved to a
    // different binding; the function-range check handles true local bindings.
    if (local->binding_start == 0 && local->binding_end == 0) return false;
    return local->binding_start == ref->binding_start &&
        local->binding_end == ref->binding_end;
}

static bool jm_ref_binding_is_inside_function(JsFunctionNode* fn,
                                               JsNameSetEntry* ref) {
    if (!fn || !fn->body || !ref ||
        (ref->binding_start == 0 && ref->binding_end == 0) ||
        ts_node_is_null(fn->body->node)) {
        return false;
    }
    uint32_t body_start = ts_node_start_byte(fn->body->node);
    uint32_t body_end = ts_node_end_byte(fn->body->node);
    return ref->binding_start >= body_start && ref->binding_end <= body_end;
}

static bool jm_for_head_declares_lexical_names(JsForOfNode* fo, struct hashmap* names) {
    if (!fo || !names) return false;
    bool lexical_head = false;
    if (fo->left && fo->left->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
        JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)fo->left;
        if (vd->kind == JS_VAR_LET || vd->kind == JS_VAR_CONST) {
            lexical_head = true;
            for (JsAstNode* d = vd->declarations; d; d = d->next) {
                if (d->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
                jm_collect_pattern_names(((JsVariableDeclaratorNode*)d)->id, names);
            }
        }
    } else if (fo->left && (fo->kind == JS_VAR_LET || fo->kind == JS_VAR_CONST)) {
        lexical_head = true;
        jm_collect_pattern_names(fo->left, names);
    }
    return lexical_head;
}

// v15: Count yield points in a generator function body (not recursing into nested functions)
int jm_count_yields(JsAstNode* node) {
    if (!node) return 0;
    switch (node->node_type) {
    case JS_AST_NODE_YIELD_EXPRESSION: {
        JsYieldNode* y = (JsYieldNode*)node;
        return 1 + jm_count_yields(y->argument);
    }
    // Don't count yields inside nested functions
    case JS_AST_NODE_FUNCTION_DECLARATION:
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
        return 0;
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        int count = 0;
        JsAstNode* s = blk->statements;
        while (s) { count += jm_count_yields(s); s = s->next; }
        return count;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* es = (JsExpressionStatementNode*)node;
        return jm_count_yields(es->expression);
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* v = (JsVariableDeclarationNode*)node;
        int count = 0;
        JsAstNode* d = v->declarations;
        while (d) { count += jm_count_yields(d); d = d->next; }
        return count;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)node;
        return jm_count_yields(d->init);
    }
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* r = (JsReturnNode*)node;
        return jm_count_yields(r->argument);
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* ifn = (JsIfNode*)node;
        return jm_count_yields(ifn->test) + jm_count_yields(ifn->consequent) + jm_count_yields(ifn->alternate);
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* w = (JsWhileNode*)node;
        return jm_count_yields(w->test) + jm_count_yields(w->body);
    }
    case JS_AST_NODE_DO_WHILE_STATEMENT: {
        JsDoWhileNode* dw = (JsDoWhileNode*)node;
        return jm_count_yields(dw->body) + jm_count_yields(dw->test);
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* f = (JsForNode*)node;
        return jm_count_yields(f->init) + jm_count_yields(f->test) + jm_count_yields(f->update) + jm_count_yields(f->body);
    }
    case JS_AST_NODE_FOR_OF_STATEMENT:
    case JS_AST_NODE_FOR_IN_STATEMENT: {
        JsForOfNode* fo = (JsForOfNode*)node;
        return jm_count_yields(fo->left) + jm_count_yields(fo->right) + jm_count_yields(fo->body);
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* sw = (JsSwitchNode*)node;
        int count = jm_count_yields(sw->discriminant);
        JsAstNode* c = sw->cases;
        while (c) { count += jm_count_yields(c); c = c->next; }
        return count;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* sc = (JsSwitchCaseNode*)node;
        int count = jm_count_yields(sc->test);
        JsAstNode* s = sc->consequent;
        while (s) { count += jm_count_yields(s); s = s->next; }
        return count;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* t = (JsTryNode*)node;
        return jm_count_yields(t->block) + jm_count_yields(t->handler) + jm_count_yields(t->finalizer);
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* cc = (JsCatchNode*)node;
        return jm_count_yields(cc->body);
    }
    // Binary/unary/call expressions: recurse into their sub-expressions
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        return jm_count_yields(bin->left) + jm_count_yields(bin->right);
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* un = (JsUnaryNode*)node;
        return jm_count_yields(un->operand);
    }
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* a = (JsAssignmentNode*)node;
        return jm_count_yields(a->left) + jm_count_yields(a->right);
    }
    case JS_AST_NODE_CALL_EXPRESSION:
    case JS_AST_NODE_NEW_EXPRESSION: {
        JsCallNode* call = (JsCallNode*)node;
        int count = jm_count_yields(call->callee);
        JsAstNode* arg = call->arguments;
        while (arg) { count += jm_count_yields(arg); arg = arg->next; }
        return count;
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* c = (JsConditionalNode*)node;
        return jm_count_yields(c->test) + jm_count_yields(c->consequent) + jm_count_yields(c->alternate);
    }
    case JS_AST_NODE_MEMBER_EXPRESSION: {
        JsMemberNode* m = (JsMemberNode*)node;
        return jm_count_yields(m->object) + jm_count_yields(m->property);
    }
    case JS_AST_NODE_TEMPLATE_LITERAL: {
        JsTemplateLiteralNode* tl = (JsTemplateLiteralNode*)node;
        int count = 0;
        JsAstNode* e = tl->expressions;
        while (e) { count += jm_count_yields(e); e = e->next; }
        return count;
    }
    case JS_AST_NODE_TAGGED_TEMPLATE: {
        JsTaggedTemplateNode* tt = (JsTaggedTemplateNode*)node;
        int count = jm_count_yields(tt->tag);
        if (tt->quasi) { JsAstNode* e = tt->quasi->expressions; while (e) { count += jm_count_yields(e); e = e->next; } }
        return count;
    }
    case JS_AST_NODE_ARRAY_EXPRESSION: {
        JsArrayNode* ae = (JsArrayNode*)node;
        int count = 0;
        JsAstNode* e = ae->elements;
        while (e) { count += jm_count_yields(e); e = e->next; }
        return count;
    }
    case JS_AST_NODE_OBJECT_EXPRESSION: {
        JsObjectNode* oe = (JsObjectNode*)node;
        int count = 0;
        JsAstNode* p = oe->properties;
        while (p) { count += jm_count_yields(p); p = p->next; }
        return count;
    }
    case JS_AST_NODE_PROPERTY: {
        JsPropertyNode* prop = (JsPropertyNode*)node;
        int count = prop->computed ? jm_count_yields(prop->key) : 0;
        return count + jm_count_yields(prop->value);
    }
    case JS_AST_NODE_SPREAD_ELEMENT: {
        JsSpreadElementNode* sp = (JsSpreadElementNode*)node;
        return jm_count_yields(sp->argument);
    }
    case JS_AST_NODE_SEQUENCE_EXPRESSION: {
        JsSequenceNode* seq = (JsSequenceNode*)node;
        int count = 0;
        JsAstNode* e = seq->expressions;
        while (e) { count += jm_count_yields(e); e = e->next; }
        return count;
    }
    case JS_AST_NODE_LABELED_STATEMENT: {
        JsLabeledStatementNode* ls = (JsLabeledStatementNode*)node;
        return jm_count_yields(ls->body);
    }
    case JS_AST_NODE_WITH_STATEMENT: {
        JsWithStatementNode* ws = (JsWithStatementNode*)node;
        return jm_count_yields(ws->object) + jm_count_yields(ws->body);
    }
    // destructuring patterns: yield can appear in default values
    case JS_AST_NODE_ASSIGNMENT_PATTERN: {
        JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)node;
        return jm_count_yields(ap->left) + jm_count_yields(ap->right);
    }
    case JS_AST_NODE_ARRAY_PATTERN: {
        JsArrayPatternNode* arrp = (JsArrayPatternNode*)node;
        int count = 0;
        JsAstNode* e = arrp->elements;
        while (e) {
            int elem_count = jm_count_yields(e);
            // array destructuring emits targets in more than one branch:
            // regular elements have a value path and an exhausted-iterator
            // undefined path; rest elements have a collected-rest path and an
            // already-exhausted empty-array path. A default initializer
            // containing yield therefore needs one resume label per emitted
            // branch, not merely per source AST occurrence.
            if (e->node_type != JS_AST_NODE_NULL) {
                elem_count *= 2;
            }
            count += elem_count;
            e = e->next;
        }
        return count;
    }
    case JS_AST_NODE_OBJECT_PATTERN: {
        JsObjectPatternNode* objp = (JsObjectPatternNode*)node;
        int count = 0;
        JsAstNode* p = objp->properties;
        while (p) { count += jm_count_yields(p); p = p->next; }
        return count;
    }
    case JS_AST_NODE_REST_ELEMENT: {
        JsSpreadElementNode* sp = (JsSpreadElementNode*)node;
        return jm_count_yields(sp->argument);
    }
    case JS_AST_NODE_THROW_STATEMENT: {
        JsThrowNode* thr = (JsThrowNode*)node;
        return jm_count_yields(thr->argument);
    }
    case JS_AST_NODE_CLASS_DECLARATION: {
        // Count yields in computed property names of class members (they're in outer generator scope)
        JsClassNode* cls = (JsClassNode*)node;
        int count = jm_count_yields(cls->superclass);
        JsAstNode* m = cls->body;
        while (m) { count += jm_count_yields(m); m = m->next; }
        return count;
    }
    case JS_AST_NODE_METHOD_DEFINITION: {
        JsMethodDefinitionNode* md = (JsMethodDefinitionNode*)node;
        // Only count yields in computed key (value is a nested function, excluded by recursion)
        return md->computed ? jm_count_yields(md->key) : 0;
    }
    case JS_AST_NODE_FIELD_DEFINITION: {
        JsFieldDefinitionNode* fd = (JsFieldDefinitionNode*)node;
        return fd->computed ? jm_count_yields(fd->key) : 0;
    }
    default:
        return 0;
    }
}

// Generator yield spill: save a temporary register to an env slot before a yield-containing
// sub-expression, so that its value survives the yield suspend/resume cycle.
// Returns the allocated env slot index.
int jm_gen_spill_save(JsMirTranspiler* mt, MIR_reg_t reg) {
    int slot = mt->gen_spill_slot_next++;
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_mem_op(mt->ctx, MIR_T_I64,
            slot * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1),
        MIR_new_reg_op(mt->ctx, reg)));
    return slot;
}

// Generator yield spill: restore a register from an env slot after a yield-containing
// sub-expression has been evaluated.
void jm_gen_spill_load(JsMirTranspiler* mt, MIR_reg_t reg, int slot) {
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, reg),
        MIR_new_mem_op(mt->ctx, MIR_T_I64,
            slot * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1)));
}

// Check if an expression subtree contains a yield (for generator spill decisions)
bool jm_has_yield(JsAstNode* node) {
    return jm_count_yields(node) > 0;
}

// Check if an expression subtree contains an optional chain (?.),
// meaning the result may be undefined due to short-circuiting.
bool jm_has_optional_chain(JsAstNode* node) {
    if (!node) return false;
    if (node->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* m = (JsMemberNode*)node;
        if (m->optional) return true;
        return jm_has_optional_chain(m->object);
    }
    if (node->node_type == JS_AST_NODE_CALL_EXPRESSION) {
        JsCallNode* c = (JsCallNode*)node;
        if (c->optional) return true;
        return jm_has_optional_chain(c->callee);
    }
    return false;
}

// Phase 6: Count await expressions in an async function body (mirrors jm_count_yields)
int jm_count_awaits(JsAstNode* node) {
    if (!node) return 0;
    switch (node->node_type) {
    case JS_AST_NODE_AWAIT_EXPRESSION: {
        JsAwaitNode* a = (JsAwaitNode*)node;
        return 1 + jm_count_awaits(a->argument);
    }
    // Don't count awaits inside nested functions
    case JS_AST_NODE_FUNCTION_DECLARATION:
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
        return 0;
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        int count = 0;
        JsAstNode* s = blk->statements;
        while (s) { count += jm_count_awaits(s); s = s->next; }
        return count;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* es = (JsExpressionStatementNode*)node;
        return jm_count_awaits(es->expression);
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* v = (JsVariableDeclarationNode*)node;
        int count = 0;
        JsAstNode* d = v->declarations;
        while (d) { count += jm_count_awaits(d); d = d->next; }
        return count;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)node;
        return jm_count_awaits(d->init);
    }
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* r = (JsReturnNode*)node;
        return jm_count_awaits(r->argument);
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* ifn = (JsIfNode*)node;
        return jm_count_awaits(ifn->test) + jm_count_awaits(ifn->consequent) + jm_count_awaits(ifn->alternate);
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* w = (JsWhileNode*)node;
        return jm_count_awaits(w->test) + jm_count_awaits(w->body);
    }
    case JS_AST_NODE_DO_WHILE_STATEMENT: {
        JsDoWhileNode* dw = (JsDoWhileNode*)node;
        return jm_count_awaits(dw->body) + jm_count_awaits(dw->test);
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* f = (JsForNode*)node;
        return jm_count_awaits(f->init) + jm_count_awaits(f->test) + jm_count_awaits(f->update) + jm_count_awaits(f->body);
    }
    case JS_AST_NODE_FOR_OF_STATEMENT:
    case JS_AST_NODE_FOR_IN_STATEMENT: {
        JsForOfNode* fo = (JsForOfNode*)node;
        int implicit_await = (node->node_type == JS_AST_NODE_FOR_OF_STATEMENT && fo->is_await) ? 2 : 0;
        return implicit_await + jm_count_awaits(fo->left) + jm_count_awaits(fo->right) + jm_count_awaits(fo->body);
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* sw = (JsSwitchNode*)node;
        int count = jm_count_awaits(sw->discriminant);
        JsAstNode* c = sw->cases;
        while (c) { count += jm_count_awaits(c); c = c->next; }
        return count;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* sc = (JsSwitchCaseNode*)node;
        int count = jm_count_awaits(sc->test);
        JsAstNode* s = sc->consequent;
        while (s) { count += jm_count_awaits(s); s = s->next; }
        return count;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* t = (JsTryNode*)node;
        return jm_count_awaits(t->block) + jm_count_awaits(t->handler) + jm_count_awaits(t->finalizer);
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* cc = (JsCatchNode*)node;
        return jm_count_awaits(cc->body);
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        return jm_count_awaits(bin->left) + jm_count_awaits(bin->right);
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* un = (JsUnaryNode*)node;
        return jm_count_awaits(un->operand);
    }
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* a = (JsAssignmentNode*)node;
        return jm_count_awaits(a->left) + jm_count_awaits(a->right);
    }
    case JS_AST_NODE_CALL_EXPRESSION:
    case JS_AST_NODE_NEW_EXPRESSION: {
        JsCallNode* call = (JsCallNode*)node;
        int count = jm_count_awaits(call->callee);
        JsAstNode* arg = call->arguments;
        while (arg) { count += jm_count_awaits(arg); arg = arg->next; }
        return count;
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* c = (JsConditionalNode*)node;
        return jm_count_awaits(c->test) + jm_count_awaits(c->consequent) + jm_count_awaits(c->alternate);
    }
    case JS_AST_NODE_MEMBER_EXPRESSION: {
        JsMemberNode* m = (JsMemberNode*)node;
        return jm_count_awaits(m->object) + jm_count_awaits(m->property);
    }
    case JS_AST_NODE_TEMPLATE_LITERAL: {
        JsTemplateLiteralNode* tl = (JsTemplateLiteralNode*)node;
        int count = 0;
        JsAstNode* e = tl->expressions;
        while (e) { count += jm_count_awaits(e); e = e->next; }
        return count;
    }
    case JS_AST_NODE_TAGGED_TEMPLATE: {
        JsTaggedTemplateNode* tt = (JsTaggedTemplateNode*)node;
        int count = jm_count_awaits(tt->tag);
        if (tt->quasi) { JsAstNode* e = tt->quasi->expressions; while (e) { count += jm_count_awaits(e); e = e->next; } }
        return count;
    }
    case JS_AST_NODE_ARRAY_EXPRESSION: {
        JsArrayNode* ae = (JsArrayNode*)node;
        int count = 0;
        JsAstNode* e = ae->elements;
        while (e) { count += jm_count_awaits(e); e = e->next; }
        return count;
    }
    case JS_AST_NODE_OBJECT_EXPRESSION: {
        JsObjectNode* oe = (JsObjectNode*)node;
        int count = 0;
        JsAstNode* p = oe->properties;
        while (p) { count += jm_count_awaits(p); p = p->next; }
        return count;
    }
    case JS_AST_NODE_PROPERTY: {
        JsPropertyNode* prop = (JsPropertyNode*)node;
        int count = prop->computed ? jm_count_awaits(prop->key) : 0;
        return count + jm_count_awaits(prop->value);
    }
    case JS_AST_NODE_SPREAD_ELEMENT: {
        JsSpreadElementNode* sp = (JsSpreadElementNode*)node;
        return jm_count_awaits(sp->argument);
    }
    case JS_AST_NODE_SEQUENCE_EXPRESSION: {
        JsSequenceNode* seq = (JsSequenceNode*)node;
        int count = 0;
        JsAstNode* e = seq->expressions;
        while (e) { count += jm_count_awaits(e); e = e->next; }
        return count;
    }
    case JS_AST_NODE_LABELED_STATEMENT: {
        JsLabeledStatementNode* ls = (JsLabeledStatementNode*)node;
        return jm_count_awaits(ls->body);
    }
    case JS_AST_NODE_WITH_STATEMENT: {
        JsWithStatementNode* ws = (JsWithStatementNode*)node;
        return jm_count_awaits(ws->object) + jm_count_awaits(ws->body);
    }
    // destructuring patterns: await can appear in default values
    case JS_AST_NODE_ASSIGNMENT_PATTERN: {
        JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)node;
        return jm_count_awaits(ap->left) + jm_count_awaits(ap->right);
    }
    case JS_AST_NODE_ARRAY_PATTERN: {
        JsArrayPatternNode* arrp = (JsArrayPatternNode*)node;
        int count = 0;
        JsAstNode* e = arrp->elements;
        while (e) { count += jm_count_awaits(e); e = e->next; }
        return count;
    }
    case JS_AST_NODE_OBJECT_PATTERN: {
        JsObjectPatternNode* objp = (JsObjectPatternNode*)node;
        int count = 0;
        JsAstNode* p = objp->properties;
        while (p) { count += jm_count_awaits(p); p = p->next; }
        return count;
    }
    case JS_AST_NODE_REST_ELEMENT: {
        JsSpreadElementNode* sp = (JsSpreadElementNode*)node;
        return jm_count_awaits(sp->argument);
    }
    case JS_AST_NODE_THROW_STATEMENT: {
        JsThrowNode* thr = (JsThrowNode*)node;
        return jm_count_awaits(thr->argument);
    }
    case JS_AST_NODE_CLASS_DECLARATION: {
        JsClassNode* cls = (JsClassNode*)node;
        int count = jm_count_awaits(cls->superclass);
        JsAstNode* m = cls->body;
        while (m) { count += jm_count_awaits(m); m = m->next; }
        return count;
    }
    case JS_AST_NODE_METHOD_DEFINITION: {
        JsMethodDefinitionNode* md = (JsMethodDefinitionNode*)node;
        return md->computed ? jm_count_awaits(md->key) : 0;
    }
    default:
        return 0;
    }
}

// Collect assignment target identifiers within a single function body.
// Does NOT recurse into nested function bodies — only collects assignments
// at the current function level.
void jm_collect_func_assignments(JsAstNode* node, struct hashmap* names) {
    if (!node) return;
    switch (node->node_type) {
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* a = (JsAssignmentNode*)node;
        if (a->left && a->left->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)a->left;
            if (id->name) {
                char name[128];
                snprintf(name, sizeof(name), "_js_%.*s", (int)id->name->len, id->name->chars);
                jm_name_set_add(names, name);
            }
        }
        jm_collect_func_assignments(a->left, names);
        jm_collect_func_assignments(a->right, names);
        break;
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* un = (JsUnaryNode*)node;
        // i++ / i-- are also implicit assignments
        if (un->operand && un->operand->node_type == JS_AST_NODE_IDENTIFIER) {
            if (un->op == JS_OP_INCREMENT || un->op == JS_OP_DECREMENT) {
                JsIdentifierNode* id = (JsIdentifierNode*)un->operand;
                if (id->name) {
                    char name[128];
                    snprintf(name, sizeof(name), "_js_%.*s", (int)id->name->len, id->name->chars);
                    jm_name_set_add(names, name);
                }
            }
        }
        jm_collect_func_assignments(un->operand, names);
        break;
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        JsAstNode* s = blk->statements;
        while (s) { jm_collect_func_assignments(s, names); s = s->next; }
        break;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* es = (JsExpressionStatementNode*)node;
        jm_collect_func_assignments(es->expression, names);
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* ifn = (JsIfNode*)node;
        jm_collect_func_assignments(ifn->test, names);
        jm_collect_func_assignments(ifn->consequent, names);
        jm_collect_func_assignments(ifn->alternate, names);
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* f = (JsForNode*)node;
        jm_collect_func_assignments(f->init, names);
        jm_collect_func_assignments(f->test, names);
        jm_collect_func_assignments(f->update, names);
        jm_collect_func_assignments(f->body, names);
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* w = (JsWhileNode*)node;
        jm_collect_func_assignments(w->test, names);
        jm_collect_func_assignments(w->body, names);
        break;
    }
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* r = (JsReturnNode*)node;
        jm_collect_func_assignments(r->argument, names);
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* v = (JsVariableDeclarationNode*)node;
        JsAstNode* d = v->declarations;
        while (d) { jm_collect_func_assignments(d, names); d = d->next; }
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)node;
        if (d->init) jm_collect_func_assignments(d->init, names);
        break;
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        jm_collect_func_assignments(bin->left, names);
        jm_collect_func_assignments(bin->right, names);
        break;
    }
    case JS_AST_NODE_CALL_EXPRESSION:
    case JS_AST_NODE_NEW_EXPRESSION: {
        JsCallNode* c = (JsCallNode*)node;
        jm_collect_func_assignments(c->callee, names);
        JsAstNode* arg = c->arguments;
        while (arg) { jm_collect_func_assignments(arg, names); arg = arg->next; }
        break;
    }
    case JS_AST_NODE_MEMBER_EXPRESSION: {
        JsMemberNode* m = (JsMemberNode*)node;
        jm_collect_func_assignments(m->object, names);
        if (m->computed) jm_collect_func_assignments(m->property, names);
        break;
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* cond = (JsConditionalNode*)node;
        jm_collect_func_assignments(cond->test, names);
        jm_collect_func_assignments(cond->consequent, names);
        jm_collect_func_assignments(cond->alternate, names);
        break;
    }
    case JS_AST_NODE_ARRAY_EXPRESSION: {
        JsArrayNode* arr = (JsArrayNode*)node;
        JsAstNode* el = arr->elements;
        while (el) { jm_collect_func_assignments(el, names); el = el->next; }
        break;
    }
    case JS_AST_NODE_OBJECT_EXPRESSION: {
        JsObjectNode* obj = (JsObjectNode*)node;
        JsAstNode* prop = obj->properties;
        while (prop) { jm_collect_func_assignments(prop, names); prop = prop->next; }
        break;
    }
    case JS_AST_NODE_PROPERTY: {
        JsPropertyNode* p = (JsPropertyNode*)node;
        jm_collect_func_assignments(p->value, names);
        break;
    }
    // Do NOT recurse into nested functions — their locals are separate
    case JS_AST_NODE_FUNCTION_DECLARATION:
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
        break;
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* sw = (JsSwitchNode*)node;
        jm_collect_func_assignments(sw->discriminant, names);
        JsAstNode* c = sw->cases;
        while (c) { jm_collect_func_assignments(c, names); c = c->next; }
        break;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* sc = (JsSwitchCaseNode*)node;
        jm_collect_func_assignments(sc->test, names);
        JsAstNode* s = sc->consequent;
        while (s) { jm_collect_func_assignments(s, names); s = s->next; }
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* t = (JsTryNode*)node;
        jm_collect_func_assignments(t->block, names);
        jm_collect_func_assignments(t->handler, names);
        jm_collect_func_assignments(t->finalizer, names);
        break;
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* cc = (JsCatchNode*)node;
        jm_collect_func_assignments(cc->body, names);
        break;
    }
    case JS_AST_NODE_THROW_STATEMENT: {
        JsThrowNode* th = (JsThrowNode*)node;
        jm_collect_func_assignments(th->argument, names);
        break;
    }
    case JS_AST_NODE_FOR_IN_STATEMENT:
    case JS_AST_NODE_FOR_OF_STATEMENT: {
        JsForInNode* fi = (JsForInNode*)node;
        jm_collect_func_assignments(fi->left, names);
        jm_collect_func_assignments(fi->body, names);
        break;
    }
    case JS_AST_NODE_SEQUENCE_EXPRESSION: {
        JsSequenceNode* seq = (JsSequenceNode*)node;
        JsAstNode* child = seq->expressions;
        while (child) { jm_collect_func_assignments(child, names); child = child->next; }
        break;
    }
    case JS_AST_NODE_LABELED_STATEMENT: {
        JsLabeledStatementNode* ls = (JsLabeledStatementNode*)node;
        jm_collect_func_assignments(ls->body, names);
        break;
    }
    case JS_AST_NODE_WITH_STATEMENT: {
        // Do NOT recurse into 'with' bodies — assignments inside 'with' may resolve
        // to the scope object at runtime, so they are not implicit globals
        break;
    }
    case JS_AST_NODE_DO_WHILE_STATEMENT: {
        JsDoWhileNode* dw = (JsDoWhileNode*)node;
        jm_collect_func_assignments(dw->body, names);
        jm_collect_func_assignments(dw->test, names);
        break;
    }
    default:
        break;
    }
}

// Walk a node tree collecting only arrow lexical pseudo-references.
// Used to propagate lexical this/arguments/new.target requirements from nested arrow
// functions up to enclosing arrows. Stops at non-arrow function boundaries
// (those introduce fresh non-lexical bindings) and only adds the
// pseudo-refs (no other identifiers) so it does not pollute the
// closure-capture analysis with the nested arrow's own free variables.
void jm_collect_arrow_lexical_refs(JsAstNode* node, struct hashmap* refs) {
    if (!node) return;
    // Use jm_collect_body_refs to walk the immediate body (it already
    // covers all statement/expression node types, but stops at any nested
    // function boundary). Capture into a temp set so we can extract only
    // the two pseudo-refs we care about.
    struct hashmap* tmp = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    jm_collect_body_refs(node, tmp);
    if (jm_name_set_has(tmp, "_js_this")) jm_name_set_add(refs, "_js_this");
    if (jm_name_set_has(tmp, "_js_arguments")) jm_name_set_add(refs, "_js_arguments");
    if (jm_name_set_has(tmp, "_js_new.target")) jm_name_set_add(refs, "_js_new.target");
    hashmap_free(tmp);
    // jm_collect_body_refs already invokes us recursively for nested
    // ARROW bodies (via the FUNCTION_*/ARROW case), so transitive arrows
    // are covered. Non-arrow nested functions introduce their own
    // this/arguments and are correctly skipped.
}

// Collect all identifier references in a function body (excluding nested function bodies)
static void jm_collect_body_refs_impl(JsAstNode* node, struct hashmap* refs,
                                      uint32_t body_start, uint32_t body_end) {
    if (!node) return;

    switch (node->node_type) {
    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)node;
        if (id->name) {
            char name[128];
            snprintf(name, sizeof(name), "_js_%.*s", (int)id->name->len, id->name->chars);
            jm_name_set_add_ref(refs, name, id, body_start, body_end);
        }
        break;
    }
    // Don't recurse into nested function bodies (they have their own scope)
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
    case JS_AST_NODE_FUNCTION_DECLARATION: {
        // Exception: arrow functions inherit `this`/`arguments`/`new.target` lexically.
        // If a nested arrow (or chain of nested arrows) references `this`
        // or those lexical meta-bindings, the enclosing function (which may itself be an
        // arrow) needs to propagate the capture upward. So when we see a
        // nested ARROW, recurse into its body collecting only those
        // pseudo-refs; transparently pass
        // through further nested arrows; STOP at non-arrow function nodes
        // (those introduce a fresh `this`/`arguments` binding).
        JsFunctionNode* nested = (JsFunctionNode*)node;
        if (nested->is_arrow && nested->body) {
            jm_collect_arrow_lexical_refs(nested->body, refs);
        }
        break;
    }

    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        JsAstNode* s = blk->statements;
        while (s) { jm_collect_body_refs_impl(s, refs, body_start, body_end); s = s->next; }
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* v = (JsVariableDeclarationNode*)node;
        JsAstNode* d = v->declarations;
        while (d) { jm_collect_body_refs_impl(d, refs, body_start, body_end); d = d->next; }
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)node;
        // Recurse into init (may reference outer vars)
        if (d->init) jm_collect_body_refs_impl(d->init, refs, body_start, body_end);
        break;
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        jm_collect_body_refs_impl(bin->left, refs, body_start, body_end);
        jm_collect_body_refs_impl(bin->right, refs, body_start, body_end);
        break;
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* un = (JsUnaryNode*)node;
        jm_collect_body_refs_impl(un->operand, refs, body_start, body_end);
        break;
    }
    // Note: JS_AST_NODE_UNARY_EXPRESSION covers both unary ops and update (++/--)
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* a = (JsAssignmentNode*)node;
        jm_collect_body_refs_impl(a->left, refs, body_start, body_end);
        jm_collect_body_refs_impl(a->right, refs, body_start, body_end);
        break;
    }
    case JS_AST_NODE_CALL_EXPRESSION:
    case JS_AST_NODE_NEW_EXPRESSION: {
        JsCallNode* c = (JsCallNode*)node;
        bool is_super_call = false;
        if (c->callee && c->callee->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)c->callee;
            is_super_call = id->name && id->name->len == 5 &&
                strncmp(id->name->chars, "super", 5) == 0;
        }
        if (is_super_call) {
            jm_name_set_add(refs, "_js_this");
        } else {
            jm_collect_body_refs_impl(c->callee, refs, body_start, body_end);
        }
        JsAstNode* arg = c->arguments;
        while (arg) { jm_collect_body_refs_impl(arg, refs, body_start, body_end); arg = arg->next; }
        break;
    }
    case JS_AST_NODE_MEMBER_EXPRESSION: {
        JsMemberNode* m = (JsMemberNode*)node;
        bool is_super = false;
        if (m->object && m->object->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* obj_id = (JsIdentifierNode*)m->object;
            is_super = obj_id->name && obj_id->name->len == 5 &&
                strncmp(obj_id->name->chars, "super", 5) == 0;
        }
        if (is_super) {
            jm_name_set_add(refs, "_js_this");
        } else {
            jm_collect_body_refs_impl(m->object, refs, body_start, body_end);
        }
        if (m->computed) jm_collect_body_refs_impl(m->property, refs, body_start, body_end);
        break;
    }
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* r = (JsReturnNode*)node;
        jm_collect_body_refs_impl(r->argument, refs, body_start, body_end);
        break;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* es = (JsExpressionStatementNode*)node;
        jm_collect_body_refs_impl(es->expression, refs, body_start, body_end);
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* ifn = (JsIfNode*)node;
        jm_collect_body_refs_impl(ifn->test, refs, body_start, body_end);
        jm_collect_body_refs_impl(ifn->consequent, refs, body_start, body_end);
        jm_collect_body_refs_impl(ifn->alternate, refs, body_start, body_end);
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* f = (JsForNode*)node;
        jm_collect_body_refs_impl(f->init, refs, body_start, body_end);
        jm_collect_body_refs_impl(f->test, refs, body_start, body_end);
        jm_collect_body_refs_impl(f->update, refs, body_start, body_end);
        jm_collect_body_refs_impl(f->body, refs, body_start, body_end);
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* w = (JsWhileNode*)node;
        jm_collect_body_refs_impl(w->test, refs, body_start, body_end);
        jm_collect_body_refs_impl(w->body, refs, body_start, body_end);
        break;
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* cond = (JsConditionalNode*)node;
        jm_collect_body_refs_impl(cond->test, refs, body_start, body_end);
        jm_collect_body_refs_impl(cond->consequent, refs, body_start, body_end);
        jm_collect_body_refs_impl(cond->alternate, refs, body_start, body_end);
        break;
    }
    // Note: logical expressions use JS_AST_NODE_BINARY_EXPRESSION (already handled above)
    case JS_AST_NODE_ARRAY_EXPRESSION: {
        JsArrayNode* arr = (JsArrayNode*)node;
        JsAstNode* el = arr->elements;
        while (el) { jm_collect_body_refs_impl(el, refs, body_start, body_end); el = el->next; }
        break;
    }
    case JS_AST_NODE_OBJECT_EXPRESSION: {
        JsObjectNode* obj = (JsObjectNode*)node;
        JsAstNode* prop = obj->properties;
        while (prop) { jm_collect_body_refs_impl(prop, refs, body_start, body_end); prop = prop->next; }
        break;
    }
    case JS_AST_NODE_PROPERTY: {
        JsPropertyNode* p = (JsPropertyNode*)node;
        if (p->computed) jm_collect_body_refs_impl(p->key, refs, body_start, body_end);
        jm_collect_body_refs_impl(p->value, refs, body_start, body_end);
        break;
    }
    case JS_AST_NODE_TEMPLATE_LITERAL: {
        JsTemplateLiteralNode* tl = (JsTemplateLiteralNode*)node;
        JsAstNode* expr = tl->expressions;
        while (expr) { jm_collect_body_refs_impl(expr, refs, body_start, body_end); expr = expr->next; }
        break;
    }
    case JS_AST_NODE_TAGGED_TEMPLATE: {
        JsTaggedTemplateNode* tt = (JsTaggedTemplateNode*)node;
        jm_collect_body_refs_impl(tt->tag, refs, body_start, body_end);
        if (tt->quasi) { JsAstNode* e = tt->quasi->expressions; while (e) { jm_collect_body_refs_impl(e, refs, body_start, body_end); e = e->next; } }
        break;
    }
    case JS_AST_NODE_SPREAD_ELEMENT: {
        JsSpreadElementNode* spread = (JsSpreadElementNode*)node;
        jm_collect_body_refs_impl(spread->argument, refs, body_start, body_end);
        break;
    }
    case JS_AST_NODE_YIELD_EXPRESSION: {
        JsYieldNode* yield_node = (JsYieldNode*)node;
        jm_collect_body_refs_impl(yield_node->argument, refs, body_start, body_end);
        break;
    }
    case JS_AST_NODE_AWAIT_EXPRESSION: {
        JsAwaitNode* await_node = (JsAwaitNode*)node;
        jm_collect_body_refs_impl(await_node->argument, refs, body_start, body_end);
        break;
    }
    case JS_AST_NODE_SEQUENCE_EXPRESSION: {
        JsSequenceNode* seq = (JsSequenceNode*)node;
        JsAstNode* child = seq->expressions;
        while (child) { jm_collect_body_refs_impl(child, refs, body_start, body_end); child = child->next; }
        break;
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* sw = (JsSwitchNode*)node;
        jm_collect_body_refs_impl(sw->discriminant, refs, body_start, body_end);
        JsAstNode* c = sw->cases;
        while (c) { jm_collect_body_refs_impl(c, refs, body_start, body_end); c = c->next; }
        break;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* sc = (JsSwitchCaseNode*)node;
        jm_collect_body_refs_impl(sc->test, refs, body_start, body_end);
        JsAstNode* s = sc->consequent;
        while (s) { jm_collect_body_refs_impl(s, refs, body_start, body_end); s = s->next; }
        break;
    }
    case JS_AST_NODE_DO_WHILE_STATEMENT: {
        JsDoWhileNode* dw = (JsDoWhileNode*)node;
        jm_collect_body_refs_impl(dw->test, refs, body_start, body_end);
        jm_collect_body_refs_impl(dw->body, refs, body_start, body_end);
        break;
    }
    case JS_AST_NODE_FOR_OF_STATEMENT:
    case JS_AST_NODE_FOR_IN_STATEMENT: {
        JsForOfNode* fo = (JsForOfNode*)node;
        struct hashmap* loop_head_lexicals = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
        bool lexical_head = jm_for_head_declares_lexical_names(fo, loop_head_lexicals);
        if (!lexical_head) {
            jm_collect_body_refs_impl(fo->left, refs, body_start, body_end);
        } else if (fo->left && fo->left->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
            // a lexical for-of/in head declares the body binding; only any
            // initializer references belong in the outer capture scan.
            jm_collect_body_refs_impl(fo->left, refs, body_start, body_end);
        }
        jm_collect_body_refs_impl(fo->right, refs, body_start, body_end);
        if (lexical_head) {
            struct hashmap* body_refs = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            jm_collect_body_refs_impl(fo->body, body_refs, body_start, body_end);
            size_t biter = 0; void* bitem = NULL;
            while (hashmap_iter(body_refs, &biter, &bitem)) {
                JsNameSetEntry* ref = (JsNameSetEntry*)bitem;
                if (!jm_name_set_has(loop_head_lexicals, ref->name)) {
                    jm_name_set_add_existing(refs, ref);
                }
            }
            hashmap_free(body_refs);
        } else {
            jm_collect_body_refs_impl(fo->body, refs, body_start, body_end);
        }
        hashmap_free(loop_head_lexicals);
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* t = (JsTryNode*)node;
        jm_collect_body_refs_impl(t->block, refs, body_start, body_end);
        if (t->handler) jm_collect_body_refs_impl(t->handler, refs, body_start, body_end);
        if (t->finalizer) jm_collect_body_refs_impl(t->finalizer, refs, body_start, body_end);
        break;
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* cc = (JsCatchNode*)node;
        // Note: cc->param is a DECLARATION (catch parameter), not a reference — don't add to refs
        jm_collect_body_refs_impl(cc->body, refs, body_start, body_end);
        break;
    }
    case JS_AST_NODE_THROW_STATEMENT: {
        JsThrowNode* th = (JsThrowNode*)node;
        jm_collect_body_refs_impl(th->argument, refs, body_start, body_end);
        break;
    }
    case JS_AST_NODE_LABELED_STATEMENT: {
        JsLabeledStatementNode* ls = (JsLabeledStatementNode*)node;
        jm_collect_body_refs_impl(ls->body, refs, body_start, body_end);
        break;
    }
    case JS_AST_NODE_WITH_STATEMENT: {
        JsWithStatementNode* ws = (JsWithStatementNode*)node;
        jm_collect_body_refs_impl(ws->body, refs, body_start, body_end);
        break;
    }
    default:
        // For unhandled node types, we may miss some references
        // but that's OK — we'll just not capture those variables
        break;
    }
}

void jm_collect_body_refs(JsAstNode* node, struct hashmap* refs) {
    jm_collect_body_refs_impl(node, refs, 0, 0);
}

// Collect all locally declared variable names in a function body.
// When var_only=true, only 'var' declarations are collected (for function prologue hoisting).
// When var_only=false (default), all var/let/const are collected (for implicit global detection).
void jm_collect_body_locals(JsAstNode* node, struct hashmap* locals, bool var_only ) {
    if (!node) return;

    switch (node->node_type) {
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* v = (JsVariableDeclarationNode*)node;
        // v20 TDZ: In var_only mode, skip let/const (block-scoped, handled by jm_init_block_tdz).
        // In full mode, collect everything (needed for implicit global detection).
        if (var_only && v->kind != JS_VAR_VAR) break;
        {
            JsAstNode* d = v->declarations;
            while (d) { jm_collect_body_locals(d, locals, var_only); d = d->next; }
        }
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)node;
        if (d->id) {
            jm_collect_pattern_names(d->id, locals);
        }
        break;
    }
    // Don't recurse into nested functions, but DO collect their names as locals
    // (function declarations are hoisted and should be available in the parent scope
    // for capture analysis — closures that reference hoisted functions need to capture them)
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
        break;
    case JS_AST_NODE_FUNCTION_DECLARATION: {
        JsFunctionNode* fn = (JsFunctionNode*)node;
        if (var_only && (fn->is_generator || fn->is_async)) {
            // AnnexB var-style block function hoisting applies to ordinary
            // function declarations, not generator or async-generator declarations.
            break;
        }
        if (fn->name) {
            char name[128];
            snprintf(name, sizeof(name), "_js_%.*s", (int)fn->name->len, fn->name->chars);
            JsNameSetEntry e;
            memset(&e, 0, sizeof(e));
            snprintf(e.name, sizeof(e.name), "%s", name);
            e.from_func_decl = true;
            JsNameSetEntry* existing = (JsNameSetEntry*)hashmap_get(locals, &e);
            if (!existing) hashmap_set(locals, &e);
        }
        break;
    }
    case JS_AST_NODE_CLASS_DECLARATION: {
        if (var_only) break;
        JsClassNode* cls = (JsClassNode*)node;
        if (cls->name) {
            char name[128];
            snprintf(name, sizeof(name), "_js_%.*s", (int)cls->name->len, cls->name->chars);
            jm_name_set_add(locals, name);
        }
        break;
    }

    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        JsAstNode* s = blk->statements;
        while (s) { jm_collect_body_locals(s, locals, var_only); s = s->next; }
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* ifn = (JsIfNode*)node;
        jm_collect_body_locals(ifn->consequent, locals, var_only);
        jm_collect_body_locals(ifn->alternate, locals, var_only);
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* f = (JsForNode*)node;
        jm_collect_body_locals(f->init, locals, var_only);
        jm_collect_body_locals(f->body, locals, var_only);
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* w = (JsWhileNode*)node;
        jm_collect_body_locals(w->body, locals, var_only);
        break;
    }
    case JS_AST_NODE_DO_WHILE_STATEMENT: {
        JsDoWhileNode* dw = (JsDoWhileNode*)node;
        jm_collect_body_locals(dw->body, locals, var_only);
        break;
    }
    case JS_AST_NODE_FOR_OF_STATEMENT:
    case JS_AST_NODE_FOR_IN_STATEMENT: {
        // for (const/let/var x of/in iterable) — collect the loop variable as a local
        // Note: Tree-sitter gives the left as a plain identifier (e.g. for const s of arr → left=IDENTIFIER 's')
        JsForOfNode* fo = (JsForOfNode*)node;
        if (fo->left) {
            if (fo->left->node_type == JS_AST_NODE_IDENTIFIER) {
                // plain identifier: for (s of arr) or for (let/const s of arr)
                // When var_only, skip let/const loop variables (they're block-scoped)
                if (!var_only || fo->kind == 0) {
                    if (fo->kind == JS_VAR_LET || fo->kind == JS_VAR_CONST) {
                        JsIdentifierNode* id = (JsIdentifierNode*)fo->left;
                        char name[128];
                        snprintf(name, sizeof(name), "_js_%.*s",
                            (int)id->name->len, id->name->chars);
                        jm_name_set_add_binding(locals, name, fo->left);
                    } else {
                        jm_collect_pattern_names(fo->left, locals);
                    }
                }
            } else if (fo->left->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)fo->left;
                // Respect var_only: skip let/const loop variables (they're block-scoped)
                if (!var_only || vd->kind == JS_VAR_VAR) {
                    JsAstNode* d = vd->declarations;
                    while (d) { jm_collect_body_locals(d, locals, var_only); d = d->next; }
                }
            } else {
                // Assignment-form heads do not create locals. A var pattern
                // has kind zero, so the explicit marker distinguishes it from
                // `for ([a] of arr)` and keeps function-local bindings visible.
                if (fo->declares_binding && (!var_only || fo->kind == JS_VAR_VAR)) {
                    jm_collect_pattern_names(fo->left, locals);
                }
            }
        }
        jm_collect_body_locals(fo->body, locals, var_only);
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* t = (JsTryNode*)node;
        jm_collect_body_locals(t->block, locals, var_only);
        if (t->handler) jm_collect_body_locals(t->handler, locals, var_only);
        if (t->finalizer) jm_collect_body_locals(t->finalizer, locals, var_only);
        break;
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* cc = (JsCatchNode*)node;
        // Catch parameter is block-scoped (like let) — do NOT add to function-level
        // locals, otherwise it prevents correct capture of same-named outer variables.
        // The catch param is handled at runtime via jm_push_scope/jm_set_var.
        jm_collect_body_locals(cc->body, locals, var_only);
        break;
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* sw = (JsSwitchNode*)node;
        JsAstNode* c = sw->cases;
        while (c) { jm_collect_body_locals(c, locals, var_only); c = c->next; }
        break;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* sc = (JsSwitchCaseNode*)node;
        JsAstNode* s = sc->consequent;
        while (s) { jm_collect_body_locals(s, locals, var_only); s = s->next; }
        break;
    }
    case JS_AST_NODE_LABELED_STATEMENT: {
        JsLabeledStatementNode* ls = (JsLabeledStatementNode*)node;
        jm_collect_body_locals(ls->body, locals, var_only);
        break;
    }
    case JS_AST_NODE_WITH_STATEMENT: {
        JsWithStatementNode* ws = (JsWithStatementNode*)node;
        jm_collect_body_locals(ws->body, locals, var_only);
        break;
    }
    default:
        break;
    }
}

// v20 TDZ: Collect names of let/const variables declared in a block statement.
// Only scans direct children (non-recursive into nested blocks/functions).
// Adds names with _js_ prefix to the set, with var_kind set to 1 (let) or 2 (const).
void jm_collect_let_const_names(JsAstNode* block, struct hashmap* names) {
    if (!block || block->node_type != JS_AST_NODE_BLOCK_STATEMENT) return;
    JsBlockNode* blk = (JsBlockNode*)block;
    JsAstNode* stmt = blk->statements;
    while (stmt) {
        if (stmt->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
            JsVariableDeclarationNode* v = (JsVariableDeclarationNode*)stmt;
            if (v->kind == JS_VAR_LET || v->kind == JS_VAR_CONST) {
                JsAstNode* d = v->declarations;
                while (d) {
                    if (d->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                        JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)d;
                        if (decl->id) {
                            if (decl->id->node_type == JS_AST_NODE_IDENTIFIER) {
                                JsIdentifierNode* id = (JsIdentifierNode*)decl->id;
                                char name[128];
                                snprintf(name, sizeof(name), "_js_%.*s", (int)id->name->len, id->name->chars);
                                JsNameSetEntry entry;
                                memset(&entry, 0, sizeof(entry));
                                snprintf(entry.name, sizeof(entry.name), "%s", name);
                                entry.var_kind = (int)v->kind;
                                JsAstNode* binding_node = id->entry && id->entry->node ?
                                    (JsAstNode*)id->entry->node : decl->id;
                                if (binding_node && !ts_node_is_null(binding_node->node)) {
                                    entry.binding_start = ts_node_start_byte(binding_node->node);
                                    entry.binding_end = ts_node_end_byte(binding_node->node);
                                }
                                hashmap_set(names, &entry);
                            } else {
                                struct hashmap* pat_names = hashmap_new(sizeof(JsNameSetEntry), 8, 0, 0,
                                    jm_name_hash, jm_name_cmp, NULL, NULL);
                                jm_collect_pattern_names(decl->id, pat_names);
                                size_t piter = 0;
                                void* pitem = NULL;
                                while (hashmap_iter(pat_names, &piter, &pitem)) {
                                    JsNameSetEntry* ne = (JsNameSetEntry*)pitem;
                                    jm_name_set_add_kind(names, ne->name, (int)v->kind);
                                }
                                hashmap_free(pat_names);
                            }
                        }
                    }
                    d = d->next;
                }
            }
        } else if (stmt->node_type == JS_AST_NODE_CLASS_DECLARATION) {
            JsClassNode* c = (JsClassNode*)stmt;
            if (c->name) {
                char name[128];
                snprintf(name, sizeof(name), "_js_%.*s", (int)c->name->len, c->name->chars);
                jm_name_set_add_kind(names, name, (int)JS_VAR_LET);
            }
        } else if (stmt->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
            JsFunctionNode* fn = (JsFunctionNode*)stmt;
            if (fn->name) {
                char name[128];
                snprintf(name, sizeof(name), "_js_%.*s", (int)fn->name->len, fn->name->chars);
                jm_name_set_add_kind(names, name, (int)JS_VAR_LET);
                JsNameSetEntry lookup;
                memset(&lookup, 0, sizeof(lookup));
                snprintf(lookup.name, sizeof(lookup.name), "%s", name);
                JsNameSetEntry* e = (JsNameSetEntry*)hashmap_get(names, &lookup);
                if (e) e->from_func_decl = true;
            }
        }
        stmt = stmt->next;
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

void jm_collect_switch_lexical_names(JsAstNode* switch_node, struct hashmap* names) {
    if (!switch_node || switch_node->node_type != JS_AST_NODE_SWITCH_STATEMENT || !names) return;
    JsSwitchNode* sw = (JsSwitchNode*)switch_node;
    for (JsAstNode* c = sw->cases; c; c = c->next) {
        if (c->node_type != JS_AST_NODE_SWITCH_CASE) continue;
        JsSwitchCaseNode* sc = (JsSwitchCaseNode*)c;
        for (JsAstNode* stmt = sc->consequent; stmt; stmt = stmt->next) {
            if (stmt->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                JsVariableDeclarationNode* v = (JsVariableDeclarationNode*)stmt;
                if (v->kind != JS_VAR_LET && v->kind != JS_VAR_CONST) continue;
                for (JsAstNode* d = v->declarations; d; d = d->next) {
                    if (d->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
                    JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)d;
                    jm_collect_pattern_names_kind(decl->id, names, (int)v->kind);
                }
            } else if (stmt->node_type == JS_AST_NODE_CLASS_DECLARATION) {
                JsClassNode* cls = (JsClassNode*)stmt;
                if (cls->name) {
                    char name[128];
                    snprintf(name, sizeof(name), "_js_%.*s", (int)cls->name->len, cls->name->chars);
                    jm_name_set_add_kind(names, name, (int)JS_VAR_LET);
                }
            } else if (stmt->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
                JsFunctionNode* fn = (JsFunctionNode*)stmt;
                if (fn->name) {
                    char name[128];
                    snprintf(name, sizeof(name), "_js_%.*s", (int)fn->name->len, fn->name->chars);
                    jm_name_set_add_kind(names, name, (int)JS_VAR_LET);
                }
            }
        }
    }
}

// AnnexB B.3.3.3 helper: recursively scan a subtree for any let/const declared
// names (at any depth, in any nested block or for-init).  Used to suppress
// AnnexB nested-function-decl propagation when the same name is bound by a
// lexical declaration in the eval program.
void jm_collect_all_let_const_names_recursive(JsAstNode* node, struct hashmap* names) {
    if (!node) return;
    switch (node->node_type) {
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* v = (JsVariableDeclarationNode*)node;
        if (v->kind == JS_VAR_LET || v->kind == JS_VAR_CONST) {
            JsAstNode* d = v->declarations;
            while (d) {
                if (d->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                    JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)d;
                    if (decl->id && decl->id->node_type == JS_AST_NODE_IDENTIFIER) {
                        JsIdentifierNode* id = (JsIdentifierNode*)decl->id;
                        char nm[128];
                        snprintf(nm, sizeof(nm), "_js_%.*s", (int)id->name->len, id->name->chars);
                        jm_name_set_add(names, nm);
                    }
                }
                d = d->next;
            }
        }
        break;
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        JsAstNode* s = blk->statements;
        while (s) { jm_collect_all_let_const_names_recursive(s, names); s = s->next; }
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* ifn = (JsIfNode*)node;
        jm_collect_all_let_const_names_recursive(ifn->consequent, names);
        jm_collect_all_let_const_names_recursive(ifn->alternate, names);
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* f = (JsForNode*)node;
        jm_collect_all_let_const_names_recursive(f->init, names);
        jm_collect_all_let_const_names_recursive(f->body, names);
        break;
    }
    case JS_AST_NODE_FOR_IN_STATEMENT:
    case JS_AST_NODE_FOR_OF_STATEMENT: {
        JsForInNode* fi = (JsForInNode*)node;
        // Tree-sitter sometimes builds `for (let x in ...)` as fi->left = identifier
        // with fi->kind indicating let/const (instead of nesting a VariableDeclaration).
        if (fi->left && (fi->kind == JS_VAR_LET || fi->kind == JS_VAR_CONST)) {
            if (fi->left->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* id = (JsIdentifierNode*)fi->left;
                char nm[128];
                snprintf(nm, sizeof(nm), "_js_%.*s", (int)id->name->len, id->name->chars);
                jm_name_set_add(names, nm);
            }
        }
        jm_collect_all_let_const_names_recursive(fi->left, names);
        jm_collect_all_let_const_names_recursive(fi->body, names);
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* w = (JsWhileNode*)node;
        jm_collect_all_let_const_names_recursive(w->body, names);
        break;
    }
    case JS_AST_NODE_DO_WHILE_STATEMENT: {
        JsDoWhileNode* dw = (JsDoWhileNode*)node;
        jm_collect_all_let_const_names_recursive(dw->body, names);
        break;
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* sw = (JsSwitchNode*)node;
        JsAstNode* c = sw->cases;
        while (c) { jm_collect_all_let_const_names_recursive(c, names); c = c->next; }
        break;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* sc = (JsSwitchCaseNode*)node;
        JsAstNode* s = sc->consequent;
        while (s) { jm_collect_all_let_const_names_recursive(s, names); s = s->next; }
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* t = (JsTryNode*)node;
        jm_collect_all_let_const_names_recursive(t->block, names);
        jm_collect_all_let_const_names_recursive(t->handler, names);
        jm_collect_all_let_const_names_recursive(t->finalizer, names);
        break;
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* c = (JsCatchNode*)node;
        if (c->param && c->param->node_type != JS_AST_NODE_IDENTIFIER) {
            jm_collect_pattern_names(c->param, names);
        }
        jm_collect_all_let_const_names_recursive(c->body, names);
        break;
    }
    case JS_AST_NODE_LABELED_STATEMENT: {
        JsLabeledStatementNode* ls = (JsLabeledStatementNode*)node;
        jm_collect_all_let_const_names_recursive(ls->body, names);
        break;
    }
    case JS_AST_NODE_CLASS_DECLARATION: {
        // class X — class name is a lexical binding
        JsClassNode* c = (JsClassNode*)node;
        if (c->name) {
            char nm[128];
            snprintf(nm, sizeof(nm), "_js_%.*s", (int)c->name->len, c->name->chars);
            jm_name_set_add(names, nm);
        }
        break;
    }
    default:
        break;
    }
}

// v20 TDZ: Initialize let/const variables in a block to TDZ sentinel.
// Call at block entry (after jm_push_scope) before transpiling block statements.
static JsMirVarEntry* jm_set_current_scope_var_fresh(JsMirTranspiler* mt, const char* name,
        MIR_reg_t reg, MIR_type_t mir_type, TypeId type_id) {
    if (!mt || !name || mt->scope_depth < 0 || mt->scope_depth >= 64 || !mt->var_scopes[mt->scope_depth]) {
        return NULL;
    }
    JsVarScopeEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.var.reg = reg;
    entry.var.mir_type = mir_type;
    entry.var.type_id = type_id;
    entry.var.typed_array_type = -1;
    jm_install_fresh_var_entry(mt, mt->scope_depth, &entry);

    JsVarScopeEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    JsVarScopeEntry* found = (JsVarScopeEntry*)hashmap_get(mt->var_scopes[mt->scope_depth], &key);
    return found ? &found->var : NULL;
}

static JsAstNode* jm_find_pattern_binding_node(JsAstNode* pattern, const char* name) {
    if (!pattern || !name) return NULL;
    switch (pattern->node_type) {
    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)pattern;
        if (!id->name) return NULL;
        char binding_name[128];
        snprintf(binding_name, sizeof(binding_name), "_js_%.*s",
            (int)id->name->len, id->name->chars);
        if (strcmp(binding_name, name) != 0) return NULL;
        // Capture analysis keys identifiers by their defining declarator range,
        // which can be wider than the identifier token itself.
        return id->entry && id->entry->node ? (JsAstNode*)id->entry->node : pattern;
    }
    case JS_AST_NODE_ARRAY_PATTERN:
    case JS_AST_NODE_ARRAY_EXPRESSION:
        for (JsAstNode* element = ((JsArrayNode*)pattern)->elements;
                element; element = element->next) {
            JsAstNode* found = jm_find_pattern_binding_node(element, name);
            if (found) return found;
        }
        return NULL;
    case JS_AST_NODE_OBJECT_PATTERN:
    case JS_AST_NODE_OBJECT_EXPRESSION:
        for (JsAstNode* property = ((JsObjectNode*)pattern)->properties;
                property; property = property->next) {
            JsAstNode* found = jm_find_pattern_binding_node(property, name);
            if (found) return found;
        }
        return NULL;
    case JS_AST_NODE_PROPERTY:
        return jm_find_pattern_binding_node(((JsPropertyNode*)pattern)->value, name);
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
        return jm_find_pattern_binding_node(((JsAssignmentPatternNode*)pattern)->left, name);
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
    case JS_AST_NODE_SPREAD_ELEMENT:
        return jm_find_pattern_binding_node(((JsSpreadElementNode*)pattern)->argument, name);
    default:
        return NULL;
    }
}

static JsAstNode* jm_find_block_lexical_binding_node(JsAstNode* block, const char* name) {
    if (!block || block->node_type != JS_AST_NODE_BLOCK_STATEMENT || !name) return NULL;
    for (JsAstNode* stmt = ((JsBlockNode*)block)->statements; stmt; stmt = stmt->next) {
        if (stmt->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
            JsVariableDeclarationNode* declaration = (JsVariableDeclarationNode*)stmt;
            if (declaration->kind != JS_VAR_LET && declaration->kind != JS_VAR_CONST) continue;
            for (JsAstNode* item = declaration->declarations; item; item = item->next) {
                if (item->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
                JsAstNode* found = jm_find_pattern_binding_node(
                    ((JsVariableDeclaratorNode*)item)->id, name);
                if (found) return found;
            }
        } else if (stmt->node_type == JS_AST_NODE_CLASS_DECLARATION) {
            JsClassNode* cls = (JsClassNode*)stmt;
            if (cls->name) {
                char binding_name[128];
                snprintf(binding_name, sizeof(binding_name), "_js_%.*s",
                    (int)cls->name->len, cls->name->chars);
                if (strcmp(binding_name, name) == 0) return stmt;
            }
        }
    }
    return NULL;
}

void jm_init_block_tdz(JsMirTranspiler* mt, JsAstNode* block) {
    if (!block || block->node_type != JS_AST_NODE_BLOCK_STATEMENT) return;
    struct hashmap* let_consts = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    jm_collect_let_const_names(block, let_consts);
    size_t iter = 0; void* item;
    while (hashmap_iter(let_consts, &iter, &item)) {
        JsNameSetEntry* e = (JsNameSetEntry*)item;
        if (e->from_func_decl) {
            continue;
        }
        MIR_reg_t tdz_reg = jm_new_reg(mt, e->name, MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, tdz_reg),
            MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_TDZ)));
        jm_set_var(mt, e->name, tdz_reg);
        JsAstNode* binding_node = jm_find_block_lexical_binding_node(block, e->name);
        JsMirVarEntry* ve = jm_find_var(mt, e->name);
        if (ve) {
            ve->is_let_const = true;
            ve->is_const = (e->var_kind == 2);  // JS_VAR_CONST
            ve->tdz_active = true;
            if (binding_node && !ts_node_is_null(binding_node->node)) {
                ve->binding_start = ts_node_start_byte(binding_node->node);
                ve->binding_end = ts_node_end_byte(binding_node->node);
            }
        }
        // A block lexical can shadow a parameter with the same source name.
        // Preserve the declaration range here so its TDZ sentinel cannot claim
        // the parameter's plain-name scope-env cell before initialization.
        if (binding_node) {
            jm_scope_env_mark_and_writeback_binding(mt, e->name, binding_node, tdz_reg);
        } else {
            jm_scope_env_mark_and_writeback(mt, e->name, tdz_reg);
        }
    }
    hashmap_free(let_consts);

    JsBlockNode* blk = (JsBlockNode*)block;
    JsAstNode* stmt = blk->statements;
    while (stmt) {
        if (stmt->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
            JsFunctionNode* fn = (JsFunctionNode*)stmt;
            if (fn->name) {
                JsFuncCollected* fc = jm_find_collected_func(mt, fn);
                if (fc && fc->func_item) {
                    char vname[128];
                    snprintf(vname, sizeof(vname), "_js_%.*s",
                        (int)fn->name->len, fn->name->chars);
                    MIR_reg_t binding_reg = jm_new_reg(mt, vname, MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, binding_reg),
                        MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED)));
                    JsMirVarEntry* ve = jm_set_current_scope_var_fresh(mt, vname, binding_reg, MIR_T_I64, LMD_TYPE_ANY);
                    if (ve) {
                        ve->is_let_const = true;
                        ve->tdz_active = false;
                        ve->from_block_func_decl = true;
                    }
                    MIR_reg_t fn_reg = jm_create_func_or_closure(mt, fc);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, binding_reg),
                        MIR_new_reg_op(mt->ctx, fn_reg)));
                    jm_scope_env_mark_and_writeback(mt, vname, fn_reg);
                }
            }
        }
        stmt = stmt->next;
    }
}

void jm_init_switch_tdz(JsMirTranspiler* mt, JsAstNode* switch_node) {
    if (!switch_node || switch_node->node_type != JS_AST_NODE_SWITCH_STATEMENT) return;
    struct hashmap* let_consts = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    jm_collect_switch_lexical_names(switch_node, let_consts);
    int slot = 0;
    size_t iter = 0; void* item;
    while (hashmap_iter(let_consts, &iter, &item)) {
        JsNameSetEntry* e = (JsNameSetEntry*)item;
        MIR_reg_t tdz_reg = jm_new_reg(mt, e->name, MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, tdz_reg),
            MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_TDZ)));
        jm_set_var(mt, e->name, tdz_reg);
        JsMirVarEntry* ve = jm_find_var(mt, e->name);
        if (ve) {
            ve->is_let_const = true;
            ve->is_const = (e->var_kind == JS_VAR_CONST);
            ve->tdz_active = true;
            if (mt->scope_env_reg != 0) {
                ve->in_scope_env = true;
                ve->scope_env_slot = slot;
                ve->scope_env_reg = mt->scope_env_reg;
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, slot * (int)sizeof(uint64_t),
                        mt->scope_env_reg, 0, 1),
                    MIR_new_reg_op(mt->ctx, tdz_reg)));
            }
        }
        slot++;
    }
    hashmap_free(let_consts);

    JsSwitchNode* sw = (JsSwitchNode*)switch_node;
    for (JsAstNode* c = sw->cases; c; c = c->next) {
        if (c->node_type != JS_AST_NODE_SWITCH_CASE) continue;
        JsSwitchCaseNode* sc = (JsSwitchCaseNode*)c;
        for (JsAstNode* stmt = sc->consequent; stmt; stmt = stmt->next) {
            if (stmt->node_type != JS_AST_NODE_FUNCTION_DECLARATION) continue;
            JsFunctionNode* fn = (JsFunctionNode*)stmt;
            if (!fn->name) continue;
            JsFuncCollected* fc = jm_find_collected_func(mt, fn);
            if (!fc || !fc->func_item) continue;
            char vname[128];
            snprintf(vname, sizeof(vname), "_js_%.*s", (int)fn->name->len, fn->name->chars);
            MIR_reg_t binding_reg = jm_new_reg(mt, vname, MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, binding_reg),
                MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED)));
            JsMirVarEntry* ve = jm_set_current_scope_var_fresh(mt, vname, binding_reg, MIR_T_I64, LMD_TYPE_ANY);
            if (ve) {
                ve->is_let_const = true;
                ve->tdz_active = false;
                ve->from_block_func_decl = true;
            }
            MIR_reg_t fn_reg = jm_create_func_or_closure(mt, fc);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, binding_reg),
                MIR_new_reg_op(mt->ctx, fn_reg)));
            jm_scope_env_mark_and_writeback(mt, vname, fn_reg);
        }
    }
}

// Analyze captures for a function: find identifiers referenced but not locally declared
// Recursively collect variable names from a destructuring pattern into a name set.
// Handles: identifier, assignment_pattern (x ), object_pattern, array_pattern.
void jm_collect_pattern_names(JsAstNode* pat, struct hashmap* names) {
    if (!pat) return;
    switch (pat->node_type) {
    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)pat;
        char name[128];
        snprintf(name, sizeof(name), "_js_%.*s", (int)id->name->len, id->name->chars);
        jm_name_set_add(names, name);
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_PATTERN: {
        // x = default: the name is the left-hand side
        JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)pat;
        jm_collect_pattern_names(ap->left, names);
        break;
    }
    case JS_AST_NODE_OBJECT_PATTERN: {
        // { a, b: c, ...rest }
        JsObjectPatternNode* op = (JsObjectPatternNode*)pat;
        JsAstNode* prop = op->properties;
        while (prop) {
            if (prop->node_type == JS_AST_NODE_PROPERTY) {
                JsPropertyNode* p = (JsPropertyNode*)prop;
                // The binding is the value (for renaming: {a: b} → b is the param)
                jm_collect_pattern_names(p->value ? p->value : p->key, names);
            } else if (prop->node_type == JS_AST_NODE_REST_PROPERTY || prop->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                JsSpreadElementNode* sp = (JsSpreadElementNode*)prop;
                jm_collect_pattern_names(sp->argument, names);
            } else {
                jm_collect_pattern_names(prop, names);
            }
            prop = prop->next;
        }
        break;
    }
    case JS_AST_NODE_ARRAY_PATTERN: {
        // [a, b, ...rest]
        JsArrayPatternNode* ap = (JsArrayPatternNode*)pat;
        JsAstNode* elem = ap->elements;
        while (elem) {
            jm_collect_pattern_names(elem, names);
            elem = elem->next;
        }
        break;
    }
    case JS_AST_NODE_SPREAD_ELEMENT:
    case JS_AST_NODE_REST_ELEMENT: {
        JsSpreadElementNode* sp = (JsSpreadElementNode*)pat;
        jm_collect_pattern_names(sp->argument, names);
        break;
    }
    default:
        break;
    }
}

// Collect identifier references from default parameter expressions.
// E.g., function f(x, t) — F is a reference that needs to be captured.
// Handles nested destructuring defaults like ({a, b) and [a, b=F].
void jm_collect_param_default_refs(JsAstNode* params, struct hashmap* refs) {
    JsAstNode* p = params;
    while (p) {
        if (p->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
            JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)p;
            if (ap->right) jm_collect_body_refs(ap->right, refs);
            // Also recurse into the left side for nested destructuring defaults
            if (ap->left) jm_collect_param_default_refs(ap->left, refs);
        } else if (p->node_type == JS_AST_NODE_OBJECT_PATTERN) {
            JsObjectPatternNode* op = (JsObjectPatternNode*)p;
            JsAstNode* prop = op->properties;
            while (prop) {
                if (prop->node_type == JS_AST_NODE_PROPERTY) {
                    JsPropertyNode* pp = (JsPropertyNode*)prop;
                    if (pp->value) jm_collect_param_default_refs(pp->value, refs);
                } else if (prop->node_type == JS_AST_NODE_REST_PROPERTY || prop->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                    JsSpreadElementNode* sp = (JsSpreadElementNode*)prop;
                    if (sp->argument) jm_collect_param_default_refs(sp->argument, refs);
                }
                prop = prop->next;
            }
        } else if (p->node_type == JS_AST_NODE_ARRAY_PATTERN) {
            JsArrayPatternNode* ap = (JsArrayPatternNode*)p;
            JsAstNode* elem = ap->elements;
            while (elem) {
                jm_collect_param_default_refs(elem, refs);
                elem = elem->next;
            }
        }
        p = p->next;
    }
}

static bool jm_analysis_function_is_method_syntax(JsFunctionNode* fn) {
    if (!fn || ts_node_is_null(fn->node)) return false;
    TSNode parent = ts_node_parent(fn->node);
    if (ts_node_is_null(parent)) return false;
    const char* parent_type = ts_node_type(parent);
    if (!parent_type) return false;
    return strcmp(parent_type, "method_definition") == 0;
}

static bool jm_analysis_ts_has_with(TSNode node, bool root) {
    if (ts_node_is_null(node)) return false;
    const char* type = ts_node_type(node);
    if (type && strcmp(type, "with_statement") == 0) return true;
    if (!root && type && (strcmp(type, "function_declaration") == 0 ||
            strcmp(type, "function_expression") == 0 ||
            strcmp(type, "arrow_function") == 0 ||
            strcmp(type, "method_definition") == 0 ||
            strcmp(type, "class_declaration") == 0 ||
            strcmp(type, "class") == 0)) {
        return false;
    }
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        if (jm_analysis_ts_has_with(ts_node_named_child(node, i), false)) return true;
    }
    return false;
}

void jm_analyze_captures(JsFuncCollected* fc, struct hashmap* outer_scope_names,
                         struct hashmap* module_consts,
                         struct hashmap* ancestor_func_locals) {
    JsFunctionNode* fn = fc->node;
    fc->capture_count = 0;
    fc->analysis.captures = fc->captures;
    fc->analysis.capture_count = 0;

    // Collect parameter names (handles simple ids, default params, and destructuring)
    struct hashmap* params = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    JsAstNode* param = fn->params;
    while (param) {
        jm_collect_pattern_names(param, params);
        param = param->next;
    }

    // Collect local variable declarations
    struct hashmap* locals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    if (fn->body) jm_collect_body_locals(fn->body, locals);

    // Collect all identifier references in the body
    struct hashmap* refs = hashmap_new(sizeof(JsNameSetEntry), 64, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    if (fn->body && !ts_node_is_null(fn->body->node)) {
        jm_collect_body_refs_impl(fn->body, refs,
            ts_node_start_byte(fn->body->node), ts_node_end_byte(fn->body->node));
    }
    fc->observes_this = jm_name_set_has(refs, "_js_this") || fc->has_direct_eval;
    fc->observes_new_target = jm_name_set_has(refs, "_js_new.target") ||
        fc->has_direct_eval;
    fc->uses_with = fc->has_direct_eval || (fn->body &&
        jm_analysis_ts_has_with(fn->body->node, true));

    // Also collect refs from default parameter expressions (e.g., function f(x, t=F) — F is a ref)
    jm_collect_param_default_refs(fn->params, refs);

    // Find captures: referenced identifiers that are not params/locals but ARE in outer scope
    // Track self-references separately — if the function has other captures (and thus
    // becomes a closure), it also needs to capture itself for recursive calls.
    char self_name[128] = {0};
    bool has_self_ref = false;
    if (fn->name) {
        snprintf(self_name, sizeof(self_name), "_js_%.*s", (int)fn->name->len, fn->name->chars);
    }
    bool is_method_syntax = jm_analysis_function_is_method_syntax(fn);
    bool is_func_expr = fn->node_type == JS_AST_NODE_FUNCTION_EXPRESSION;

    size_t iter = 0;
    void* item;
    while (hashmap_iter(refs, &iter, &item)) {
        JsNameSetEntry* ref = (JsNameSetEntry*)item;
        // A parameter shadows every outer/self binding. Testing the function
        // name first made `function r(t, e, r)` overwrite the outer `r` cell
        // with its numeric argument and later attempt to call that number.
        if (jm_name_set_has(params, ref->name)) continue;    // local param
        // The AST now resolves an NFE self name to its private function scope,
        // but MIR still represents recursion through the closure environment.
        if (!fc->is_class_method && !is_method_syntax &&
            self_name[0] && strcmp(ref->name, self_name) == 0) {
            has_self_ref = true;
            continue;
        }
        // A function-wide name set cannot distinguish an outer capture from a
        // same-named lexical declared later in a nested for/block. Binding
        // ranges identify declarations owned by this function without making
        // the earlier outer reference disappear from capture analysis.
        if (jm_ref_binding_is_inside_function(fn, ref)) continue;
        // for-of/in lexical heads are block-scoped; a same-named loop variable
        // must not mask an earlier outer binding captured before that block.
        if (jm_ref_is_local_binding(locals, ref)) continue;  // local var
        if (strcmp(ref->name, "_js_new.target") == 0) continue; // handled by arrow lexical capture below
        if (fc->owner_class && fc->owner_class->name &&
            strlen(ref->name) == fc->owner_class->name->len + 4 &&
            strncmp(ref->name, "_js_", 4) == 0 &&
            strncmp(ref->name + 4, fc->owner_class->name->chars,
                fc->owner_class->name->len) == 0) {
            // A named class's private self-name belongs to the class lexical
            // environment. Treating it as an outer capture makes propagation
            // demand a nonexistent binding from the enclosing expression scope.
            continue;
        }
        if (!jm_name_set_has(outer_scope_names, ref->name)) continue;  // not in outer scope
        // Skip module-level bindings; identifier lowering resolves them via
        // module_consts (and MCONST_MODVAR uses live js_get_module_var reads).
        // If a parent function declares a local with the same name, that local
        // shadows the module binding, so we still capture the parent binding.
        if (module_consts && !(ancestor_func_locals && jm_name_set_has(ancestor_func_locals, ref->name))) {
            JsModuleConstEntry lookup;
            snprintf(lookup.name, sizeof(lookup.name), "%s", ref->name);
            JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(module_consts, &lookup);
            if (mc) continue;  // resolved via module_consts, no capture needed
        }
        // A parent-local binding shadows an IIFE-promoted module binding with
        // the same minified name. Force the closure cell path or later lowering
        // will incorrectly read/write the unrelated module const.
        // Direct Program declarations never enter ancestor_func_locals. Any
        // matching entry is therefore a real enclosing block/catch/loop or
        // function binding that shadows the same-named module cell, including
        // for a top-level closure whose parent_index is -1.
        bool force_env_capture = ancestor_func_locals &&
            jm_name_set_has(ancestor_func_locals, ref->name);

        // This is a capture
        {
            jm_ensure_captures_capacity(fc);
            snprintf(fc->captures[fc->capture_count].name, 128, "%s", ref->name);
            if (ref->binding_start != 0 || ref->binding_end != 0) {
                snprintf(fc->captures[fc->capture_count].scope_env_key, 128,
                    "%s@%u:%u", ref->name, ref->binding_start, ref->binding_end);
            } else {
                snprintf(fc->captures[fc->capture_count].scope_env_key, 128, "%s", ref->name);
            }
            fc->captures[fc->capture_count].scope_env_slot = -1;
            fc->captures[fc->capture_count].private_env_slot = -1;
            fc->captures[fc->capture_count].grandparent_slot = -1;
            fc->captures[fc->capture_count].parent_env_link_slot_override = -1;
            // Binding metadata is authoritative when scope analysis resolved
            // the reference; name-only ancestor scans can confuse an outer
            // const with a nearer same-named var in minified code.
            fc->captures[fc->capture_count].is_let_const = ref->var_kind != 0;
            fc->captures[fc->capture_count].is_const = ref->var_kind == JS_VAR_CONST;
            fc->captures[fc->capture_count].is_nfe_binding = false;
            fc->captures[fc->capture_count].force_env_capture = force_env_capture;
            for (int li = 0; li < fn->lexical_for_head_capture_count; li++) {
                if (strcmp(fn->lexical_for_head_capture_names[li], ref->name) == 0) {
                    fc->captures[fc->capture_count].is_let_const = true;
                    fc->captures[fc->capture_count].force_env_capture = true;
                    break;
                }
            }
            const char* capture_key = fc->captures[fc->capture_count].scope_env_key;
            fc->capture_count++;
            log_debug("js-mir: capture '%s' [%s] in function '%s'",
                ref->name, capture_key, fc->name);
        }
    }

    // If the function references itself (e.g., recursive calls, or Box2D constructor
    // pattern where F.method.apply(this, arguments) needs to find F), it must capture
    // itself so the reference resolves to the correct function at runtime.
    // This is critical when multiple IIFEs define functions with the same minified name
    // (e.g., 'r', 'K') — without self-capture, the module_consts table would conflate them.
    // Only add self-capture for non-top-level functions (parent_index >= 0) — top-level
    // function declarations are hoisted and uniquely resolve via module var table.
    // Keeping top-level functions capture-free preserves tail-call optimization.
    // Exception: function EXPRESSIONS always need self-capture for NFE name binding,
    // since their name is not in the module var table even when top-level.
    bool is_block_func_decl = fn->node_type == JS_AST_NODE_FUNCTION_DECLARATION &&
        !jm_analysis_function_decl_is_direct_binding(fn);
    if (has_self_ref && self_name[0] && (fc->parent_index >= 0 || is_func_expr || is_block_func_decl)) {
        jm_ensure_captures_capacity(fc);
        snprintf(fc->captures[fc->capture_count].name, 128, "%s", self_name);
        snprintf(fc->captures[fc->capture_count].scope_env_key, 128, "%s", self_name);
        fc->captures[fc->capture_count].scope_env_slot = -1;
        fc->captures[fc->capture_count].private_env_slot = -1;
        fc->captures[fc->capture_count].grandparent_slot = -1;
        fc->captures[fc->capture_count].parent_env_link_slot_override = -1;
        fc->captures[fc->capture_count].is_let_const = false;
        fc->captures[fc->capture_count].is_const = false;
        fc->captures[fc->capture_count].is_nfe_binding = is_func_expr;
        // Annex B exposes a separate outer var binding for a block function.
        // Its self-reference must stay in the private block closure cell;
        // resolving it through the same-named module var lets `f = 123` inside
        // the function overwrite the callable outer binding.
        fc->captures[fc->capture_count].force_env_capture = is_block_func_decl;
        fc->capture_count++;
        log_debug("js-mir: self-capture '%s' in closure '%s'", self_name, fc->name);
    }

    // Arrow functions: capture 'this' from enclosing lexical scope.
    // In JS, arrow functions do NOT have their own 'this'; they inherit from the parent.
    if (fn->is_arrow && jm_name_set_has(refs, "_js_this")) {
        jm_ensure_captures_capacity(fc);
        snprintf(fc->captures[fc->capture_count].name, 128, "_js_this");
        snprintf(fc->captures[fc->capture_count].scope_env_key, 128, "_js_this");
        fc->captures[fc->capture_count].scope_env_slot = -1;
        fc->captures[fc->capture_count].private_env_slot = -1;
        fc->captures[fc->capture_count].grandparent_slot = -1;
        fc->captures[fc->capture_count].parent_env_link_slot_override = -1;
        fc->captures[fc->capture_count].is_let_const = false;
        fc->captures[fc->capture_count].is_const = false;
        fc->captures[fc->capture_count].is_nfe_binding = false;
        fc->captures[fc->capture_count].force_env_capture = false;
        fc->capture_count++;
        log_debug("js-mir: arrow capture '_js_this' in function '%s'", fc->name);
    }

    // Arrow functions also capture new.target lexically.  A normal direct call
    // clears the dynamic runtime new.target, so arrows must keep a closure slot
    // for the value visible where the arrow was created.
    if (fn->is_arrow && jm_name_set_has(refs, "_js_new.target")) {
        jm_ensure_captures_capacity(fc);
        snprintf(fc->captures[fc->capture_count].name, 128, "_js_new.target");
        snprintf(fc->captures[fc->capture_count].scope_env_key, 128, "_js_new.target");
        fc->captures[fc->capture_count].scope_env_slot = -1;
        fc->captures[fc->capture_count].private_env_slot = -1;
        fc->captures[fc->capture_count].grandparent_slot = -1;
        fc->captures[fc->capture_count].parent_env_link_slot_override = -1;
        fc->captures[fc->capture_count].is_let_const = false;
        fc->captures[fc->capture_count].is_const = false;
        fc->captures[fc->capture_count].is_nfe_binding = false;
        fc->captures[fc->capture_count].force_env_capture = false;
        fc->capture_count++;
        log_debug("js-mir: arrow capture '_js_new.target' in function '%s'", fc->name);
    }

    if (fn->is_arrow && jm_name_set_has(refs, "_js_arguments")) {
        jm_ensure_captures_capacity(fc);
        snprintf(fc->captures[fc->capture_count].name, 128, "_js_arguments");
        snprintf(fc->captures[fc->capture_count].scope_env_key, 128, "_js_arguments");
        fc->captures[fc->capture_count].scope_env_slot = -1;
        fc->captures[fc->capture_count].private_env_slot = -1;
        fc->captures[fc->capture_count].grandparent_slot = -1;
        fc->captures[fc->capture_count].parent_env_link_slot_override = -1;
        fc->captures[fc->capture_count].is_let_const = false;
        fc->captures[fc->capture_count].is_const = false;
        fc->captures[fc->capture_count].is_nfe_binding = false;
        fc->captures[fc->capture_count].force_env_capture = false;
        fc->capture_count++;
        log_debug("js-mir: arrow capture '_js_arguments' in function '%s'", fc->name);
    }

    // v18q: Check if function uses 'arguments' keyword
    fc->uses_arguments = !fn->is_arrow && jm_name_set_has(refs, "_js_arguments");
    fc->analysis.captures = fc->captures;
    fc->analysis.capture_count = fc->capture_count;
    if (fn) {
        fn->analysis = &fc->analysis;
    }

    hashmap_free(params);
    hashmap_free(locals);
    hashmap_free(refs);
}
