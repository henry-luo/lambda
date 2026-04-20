// transpile_ts_mir.cpp — TypeScript MIR transpiler
//
// Entry point for TypeScript → MIR compilation. Follows the same pipeline as
// transpile_js_mir.cpp but adds type awareness from TS annotations.
// This initial implementation delegates most of the heavy lifting to the
// JS transpiler infrastructure while adding TS-specific handling.

#include "ts_transpiler.hpp"
#include "ts_runtime.h"
#include "../js/js_transpiler.hpp"
#include "../js/js_runtime.h"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/hashmap.h"
#include "../../lib/mempool.h"
#include <cstring>
#include <cstdio>
#include "../../lib/mem.h"

extern "C" {
    const TSLanguage* tree_sitter_typescript(void);
}

// ============================================================================
// TS transpiler creation — uses unified JsTranspiler with TS mode
// ============================================================================

// local alloc helper for lowering code (same as alloc_js_ast_node but takes int node_type)
static JsAstNode* alloc_ts_ast_node(JsTranspiler* tp, int node_type, TSNode node, size_t size) {
    return alloc_js_ast_node(tp, (JsAstNodeType)node_type, node, size);
}

static TsTranspiler* ts_transpiler_create(Runtime* runtime) {
    // use the unified JS transpiler, then configure for TS mode
    TsTranspiler* tp = js_transpiler_create(runtime);
    if (!tp) return NULL;

    // Re-set parser language to TypeScript (js_transpiler_create defaults to JavaScript)
    const TSLanguage* ts_lang = tree_sitter_typescript();
    if (ts_lang) {
        ts_parser_set_language(tp->parser, ts_lang);
    }

    tp->strict_js = false;           // allow TS syntax
    tp->strict_mode = true;          // TS always implies strict mode
    tp->emit_runtime_checks = false;
    tp->global_scope->strict_mode = true;

    // initialize type registry
    ts_type_registry_init(tp);

    return tp;
}

static void ts_transpiler_destroy(TsTranspiler* tp) {
    js_transpiler_destroy(tp);
}

// ============================================================================
// Lower enum declarations to JS-compatible AST (object literal)
// ============================================================================

// Lower: enum Direction { Up, Down = 5, Left }
// Into:  const Direction = { Up: 0, Down: 5, Left: 6, 0: "Up", 5: "Down", 6: "Left" };
static JsAstNode* ts_lower_enum_to_js(TsTranspiler* tp, TsEnumDeclarationNode* enum_decl) {
    Pool* pool = tp->ast_pool;
    TSNode dummy = enum_decl->base.node;

    // build object literal with forward (name → value) and reverse (value → name) mappings
    JsObjectNode* obj = (JsObjectNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_OBJECT_EXPRESSION, dummy, sizeof(JsObjectNode));
    obj->base.type = &TYPE_MAP;

    JsAstNode* prev_prop = NULL;
    int numeric_count = 0; // count numeric members for reverse mappings

    // first pass: count numeric members for allocation
    for (int i = 0; i < enum_decl->member_count; i++) {
        TsEnumMemberNode* em = (TsEnumMemberNode*)enum_decl->members[i];
        if (em->auto_value >= 0) numeric_count++;
    }

    // forward mappings: Name → value
    for (int i = 0; i < enum_decl->member_count; i++) {
        TsEnumMemberNode* em = (TsEnumMemberNode*)enum_decl->members[i];
        if (!em->name) continue;

        JsPropertyNode* prop = (JsPropertyNode*)alloc_ts_ast_node(tp,
            JS_AST_NODE_PROPERTY, dummy, sizeof(JsPropertyNode));
        prop->computed = false;
        prop->method = false;

        // key: identifier with member name
        JsIdentifierNode* key = (JsIdentifierNode*)alloc_ts_ast_node(tp,
            JS_AST_NODE_IDENTIFIER, dummy, sizeof(JsIdentifierNode));
        key->name = em->name;
        key->base.type = &TYPE_STRING;
        prop->key = (JsAstNode*)key;

        // value: numeric literal from auto_value (or string initializer)
        if (em->initializer && em->auto_value < 0) {
            // string enum member — use the initializer expression directly
            prop->value = em->initializer;
        } else {
            JsLiteralNode* val = (JsLiteralNode*)alloc_ts_ast_node(tp,
                JS_AST_NODE_LITERAL, dummy, sizeof(JsLiteralNode));
            val->literal_type = JS_LITERAL_NUMBER;
            val->has_decimal = false;
            val->value.number_value = (double)em->auto_value;
            val->base.type = &TYPE_INT;
            prop->value = (JsAstNode*)val;
        }

        prop->base.type = prop->value ? prop->value->type : &TYPE_ANY;

        if (!prev_prop) {
            obj->properties = (JsAstNode*)prop;
        } else {
            prev_prop->next = (JsAstNode*)prop;
        }
        prev_prop = (JsAstNode*)prop;
    }

    // reverse mappings: numeric value → "Name" (TypeScript enum bidirectional mapping)
    for (int i = 0; i < enum_decl->member_count; i++) {
        TsEnumMemberNode* em = (TsEnumMemberNode*)enum_decl->members[i];
        if (!em->name || em->auto_value < 0) continue; // skip string-valued members

        JsPropertyNode* rprop = (JsPropertyNode*)alloc_ts_ast_node(tp,
            JS_AST_NODE_PROPERTY, dummy, sizeof(JsPropertyNode));
        rprop->computed = false;
        rprop->method = false;

        // key: numeric literal
        JsLiteralNode* rkey = (JsLiteralNode*)alloc_ts_ast_node(tp,
            JS_AST_NODE_LITERAL, dummy, sizeof(JsLiteralNode));
        rkey->literal_type = JS_LITERAL_NUMBER;
        rkey->has_decimal = false;
        rkey->value.number_value = (double)em->auto_value;
        rkey->base.type = &TYPE_INT;
        rprop->key = (JsAstNode*)rkey;

        // value: string literal with member name
        JsLiteralNode* rval = (JsLiteralNode*)alloc_ts_ast_node(tp,
            JS_AST_NODE_LITERAL, dummy, sizeof(JsLiteralNode));
        rval->literal_type = JS_LITERAL_STRING;
        rval->value.string_value = em->name;
        rval->base.type = &TYPE_STRING;
        rprop->value = (JsAstNode*)rval;

        rprop->base.type = &TYPE_STRING;

        if (!prev_prop) {
            obj->properties = (JsAstNode*)rprop;
        } else {
            prev_prop->next = (JsAstNode*)rprop;
        }
        prev_prop = (JsAstNode*)rprop;
    }

    // wrap in: const EnumName = { ... };
    JsVariableDeclaratorNode* declarator = (JsVariableDeclaratorNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_VARIABLE_DECLARATOR, dummy, sizeof(JsVariableDeclaratorNode));
    JsIdentifierNode* id = (JsIdentifierNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_IDENTIFIER, dummy, sizeof(JsIdentifierNode));
    id->name = enum_decl->name;
    id->base.type = &TYPE_MAP;
    declarator->id = (JsAstNode*)id;
    declarator->init = (JsAstNode*)obj;
    declarator->base.type = &TYPE_MAP;

    JsVariableDeclarationNode* var_decl = (JsVariableDeclarationNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_VARIABLE_DECLARATION, dummy, sizeof(JsVariableDeclarationNode));
    var_decl->kind = JS_VAR_CONST;
    var_decl->declarations = (JsAstNode*)declarator;
    var_decl->base.type = &TYPE_NULL;

    // register in scope
    if (enum_decl->name) {
        js_scope_define(tp, enum_decl->name, (JsAstNode*)declarator, JS_VAR_CONST);
    }

    return (JsAstNode*)var_decl;
}

// ============================================================================
// Recursive expression lowering: unwrap TS expression wrappers (as, !, satisfies)
// throughout the entire expression tree so the JS transpiler only sees JS nodes.
// ============================================================================

static JsAstNode* ts_lower_expr_tree(TsTranspiler* tp, JsAstNode* node);

// lower a linked list of sibling nodes
static JsAstNode* ts_lower_expr_list(TsTranspiler* tp, JsAstNode* head) {
    JsAstNode* first = NULL;
    JsAstNode* last = NULL;
    for (JsAstNode* n = head; n; ) {
        JsAstNode* next = n->next;
        JsAstNode* lowered = ts_lower_expr_tree(tp, n);
        if (lowered) {
            lowered->next = NULL;
            if (!first) { first = lowered; last = lowered; }
            else { last->next = lowered; last = lowered; }
        }
        n = next;
    }
    return first;
}

static JsAstNode* ts_lower_expr_tree(TsTranspiler* tp, JsAstNode* node) {
    if (!node) return NULL;
    int nt = node->node_type;

    // unwrap TS expression wrappers: as, satisfies → inner expression
    if (nt == (int)TS_AST_NODE_AS_EXPRESSION || nt == (int)TS_AST_NODE_SATISFIES_EXPRESSION) {
        TsTypeExprNode* te = (TsTypeExprNode*)node;
        JsAstNode* inner = ts_lower_expr_tree(tp, te->inner);
        if (inner) inner->next = node->next;
        return inner;
    }
    // non-null assertion → inner expression
    if (nt == (int)TS_AST_NODE_NON_NULL_EXPRESSION) {
        TsNonNullNode* nn = (TsNonNullNode*)node;
        JsAstNode* inner = ts_lower_expr_tree(tp, nn->inner);
        if (inner) inner->next = node->next;
        return inner;
    }

    // recurse into child expression slots for each JS node type
    switch (nt) {
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* es = (JsExpressionStatementNode*)node;
        es->expression = ts_lower_expr_tree(tp, es->expression);
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)node;
        vd->declarations = ts_lower_expr_list(tp, vd->declarations);
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)node;
        d->init = ts_lower_expr_tree(tp, d->init);
        break;
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* b = (JsBinaryNode*)node;
        b->left = ts_lower_expr_tree(tp, b->left);
        b->right = ts_lower_expr_tree(tp, b->right);
        break;
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* u = (JsUnaryNode*)node;
        u->operand = ts_lower_expr_tree(tp, u->operand);
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* a = (JsAssignmentNode*)node;
        a->left = ts_lower_expr_tree(tp, a->left);
        a->right = ts_lower_expr_tree(tp, a->right);
        break;
    }
    case JS_AST_NODE_CALL_EXPRESSION:
    case JS_AST_NODE_NEW_EXPRESSION: {
        JsCallNode* c = (JsCallNode*)node;
        c->callee = ts_lower_expr_tree(tp, c->callee);
        c->arguments = ts_lower_expr_list(tp, c->arguments);
        break;
    }
    case JS_AST_NODE_MEMBER_EXPRESSION: {
        JsMemberNode* m = (JsMemberNode*)node;
        m->object = ts_lower_expr_tree(tp, m->object);
        if (m->computed) m->property = ts_lower_expr_tree(tp, m->property);
        break;
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* c = (JsConditionalNode*)node;
        c->test = ts_lower_expr_tree(tp, c->test);
        c->consequent = ts_lower_expr_tree(tp, c->consequent);
        c->alternate = ts_lower_expr_tree(tp, c->alternate);
        break;
    }
    case JS_AST_NODE_ARRAY_EXPRESSION: {
        JsArrayNode* a = (JsArrayNode*)node;
        a->elements = ts_lower_expr_list(tp, a->elements);
        break;
    }
    case JS_AST_NODE_OBJECT_EXPRESSION: {
        JsObjectNode* o = (JsObjectNode*)node;
        o->properties = ts_lower_expr_list(tp, o->properties);
        break;
    }
    case JS_AST_NODE_PROPERTY: {
        JsPropertyNode* p = (JsPropertyNode*)node;
        if (p->computed) p->key = ts_lower_expr_tree(tp, p->key);
        p->value = ts_lower_expr_tree(tp, p->value);
        break;
    }
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* r = (JsReturnNode*)node;
        r->argument = ts_lower_expr_tree(tp, r->argument);
        break;
    }
    case JS_AST_NODE_THROW_STATEMENT: {
        JsThrowNode* t = (JsThrowNode*)node;
        t->argument = ts_lower_expr_tree(tp, t->argument);
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* i = (JsIfNode*)node;
        i->test = ts_lower_expr_tree(tp, i->test);
        i->consequent = ts_lower_expr_tree(tp, i->consequent);
        i->alternate = ts_lower_expr_tree(tp, i->alternate);
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* w = (JsWhileNode*)node;
        w->test = ts_lower_expr_tree(tp, w->test);
        w->body = ts_lower_expr_tree(tp, w->body);
        break;
    }
    case JS_AST_NODE_DO_WHILE_STATEMENT: {
        JsDoWhileNode* dw = (JsDoWhileNode*)node;
        dw->body = ts_lower_expr_tree(tp, dw->body);
        dw->test = ts_lower_expr_tree(tp, dw->test);
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* f = (JsForNode*)node;
        f->init = ts_lower_expr_tree(tp, f->init);
        f->test = ts_lower_expr_tree(tp, f->test);
        f->update = ts_lower_expr_tree(tp, f->update);
        f->body = ts_lower_expr_tree(tp, f->body);
        break;
    }
    case JS_AST_NODE_FOR_OF_STATEMENT:
    case JS_AST_NODE_FOR_IN_STATEMENT: {
        JsForOfNode* fo = (JsForOfNode*)node;
        fo->left = ts_lower_expr_tree(tp, fo->left);
        fo->right = ts_lower_expr_tree(tp, fo->right);
        fo->body = ts_lower_expr_tree(tp, fo->body);
        break;
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* bl = (JsBlockNode*)node;
        bl->statements = ts_lower_expr_list(tp, bl->statements);
        break;
    }
    case JS_AST_NODE_FUNCTION_DECLARATION:
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION: {
        JsFunctionNode* fn = (JsFunctionNode*)node;
        fn->body = ts_lower_expr_tree(tp, fn->body);
        fn->params = ts_lower_expr_list(tp, fn->params);
        break;
    }
    case JS_AST_NODE_SPREAD_ELEMENT: {
        JsSpreadElementNode* sp = (JsSpreadElementNode*)node;
        sp->argument = ts_lower_expr_tree(tp, sp->argument);
        break;
    }
    case JS_AST_NODE_TEMPLATE_LITERAL: {
        JsTemplateLiteralNode* tl = (JsTemplateLiteralNode*)node;
        tl->expressions = ts_lower_expr_list(tp, tl->expressions);
        break;
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* sw = (JsSwitchNode*)node;
        sw->discriminant = ts_lower_expr_tree(tp, sw->discriminant);
        sw->cases = ts_lower_expr_list(tp, sw->cases);
        break;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* sc = (JsSwitchCaseNode*)node;
        sc->test = ts_lower_expr_tree(tp, sc->test);
        sc->consequent = ts_lower_expr_list(tp, sc->consequent);
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* t = (JsTryNode*)node;
        t->block = ts_lower_expr_tree(tp, t->block);
        t->handler = ts_lower_expr_tree(tp, t->handler);
        t->finalizer = ts_lower_expr_tree(tp, t->finalizer);
        break;
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* cc = (JsCatchNode*)node;
        cc->body = ts_lower_expr_tree(tp, cc->body);
        break;
    }
    case JS_AST_NODE_SEQUENCE_EXPRESSION: {
        JsSequenceNode* sq = (JsSequenceNode*)node;
        sq->expressions = ts_lower_expr_list(tp, sq->expressions);
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_PATTERN: {
        JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)node;
        ap->left = ts_lower_expr_tree(tp, ap->left);
        ap->right = ts_lower_expr_tree(tp, ap->right);
        break;
    }
    case JS_AST_NODE_YIELD_EXPRESSION:
    case JS_AST_NODE_AWAIT_EXPRESSION: {
        // these reuse JsUnaryNode layout
        JsUnaryNode* u = (JsUnaryNode*)node;
        u->operand = ts_lower_expr_tree(tp, u->operand);
        break;
    }
    case JS_AST_NODE_CLASS_DECLARATION:
    case JS_AST_NODE_CLASS_EXPRESSION: {
        JsClassNode* cls = (JsClassNode*)node;
        cls->superclass = ts_lower_expr_tree(tp, cls->superclass);
        cls->body = ts_lower_expr_list(tp, cls->body);
        break;
    }
    case JS_AST_NODE_METHOD_DEFINITION: {
        JsMethodDefinitionNode* md = (JsMethodDefinitionNode*)node;
        md->value = ts_lower_expr_tree(tp, md->value);
        break;
    }
    case JS_AST_NODE_FIELD_DEFINITION: {
        JsFieldDefinitionNode* fd = (JsFieldDefinitionNode*)node;
        fd->value = ts_lower_expr_tree(tp, fd->value);
        break;
    }
    case JS_AST_NODE_LABELED_STATEMENT: {
        JsLabeledStatementNode* ls = (JsLabeledStatementNode*)node;
        ls->body = ts_lower_expr_tree(tp, ls->body);
        break;
    }
    default:
        // leaf nodes (identifier, literal, etc.) — no children to recurse
        break;
    }
    return node;
}

// ============================================================================
// Namespace IIFE lowering
//
// namespace Foo { export function bar() { ... }; export const x = 1; }
// →
// var Foo; (function(Foo) { Foo.bar = function bar() { ... }; Foo.x = 1; })(Foo || (Foo = {}));
// ============================================================================

static JsAstNode* ts_lower_namespace_to_js(TsTranspiler* tp, TsNamespaceDeclarationNode* ns) {
    Pool* pool = tp->ast_pool;
    TSNode dummy = ns->base.node;

    // create: var Foo;
    JsVariableDeclaratorNode* ns_decl = (JsVariableDeclaratorNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_VARIABLE_DECLARATOR, dummy, sizeof(JsVariableDeclaratorNode));
    JsIdentifierNode* ns_id = (JsIdentifierNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_IDENTIFIER, dummy, sizeof(JsIdentifierNode));
    ns_id->name = ns->name;
    ns_decl->id = (JsAstNode*)ns_id;
    ns_decl->init = NULL;

    JsVariableDeclarationNode* var_stmt = (JsVariableDeclarationNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_VARIABLE_DECLARATION, dummy, sizeof(JsVariableDeclarationNode));
    var_stmt->kind = JS_VAR_VAR;
    var_stmt->declarations = (JsAstNode*)ns_decl;

    // rewrite body: export declarations → Foo.member = value assignments
    // non-exported declarations → keep as-is (local to IIFE)
    JsAstNode* iife_first = NULL;
    JsAstNode* iife_last = NULL;
    for (int i = 0; i < ns->body_count; i++) {
        JsAstNode* stmt = ns->body[i];
        if (!stmt) continue;

        // handle export_statement: unwrap and rewrite as Namespace.member = value
        bool is_exported = false;
        int snt = stmt->node_type;
        if (snt == (int)JS_AST_NODE_EXPORT_DECLARATION) {
            // export wraps a declaration; extract inner via JsExportNode
            JsExportNode* exp = (JsExportNode*)stmt;
            stmt = exp->declaration;
            if (!stmt) continue;
            snt = stmt->node_type;
            is_exported = true;
        }

        if (is_exported && snt == JS_AST_NODE_FUNCTION_DECLARATION) {
            // Foo.bar = function bar() { ... };
            JsFunctionNode* fn = (JsFunctionNode*)stmt;
            JsMemberNode* mem = (JsMemberNode*)alloc_ts_ast_node(tp,
                JS_AST_NODE_MEMBER_EXPRESSION, dummy, sizeof(JsMemberNode));
            JsIdentifierNode* obj = (JsIdentifierNode*)alloc_ts_ast_node(tp,
                JS_AST_NODE_IDENTIFIER, dummy, sizeof(JsIdentifierNode));
            obj->name = ns->name;
            mem->object = (JsAstNode*)obj;
            JsIdentifierNode* prop = (JsIdentifierNode*)alloc_ts_ast_node(tp,
                JS_AST_NODE_IDENTIFIER, dummy, sizeof(JsIdentifierNode));
            prop->name = fn->name;
            mem->property = (JsAstNode*)prop;
            mem->computed = false;

            // change function decl to expression
            fn->base.node_type = JS_AST_NODE_FUNCTION_EXPRESSION;

            JsAssignmentNode* assign = (JsAssignmentNode*)alloc_ts_ast_node(tp,
                JS_AST_NODE_ASSIGNMENT_EXPRESSION, dummy, sizeof(JsAssignmentNode));
            assign->op = JS_OP_ASSIGN;
            assign->left = (JsAstNode*)mem;
            assign->right = (JsAstNode*)fn;

            JsExpressionStatementNode* es = (JsExpressionStatementNode*)alloc_ts_ast_node(tp,
                JS_AST_NODE_EXPRESSION_STATEMENT, dummy, sizeof(JsExpressionStatementNode));
            es->expression = (JsAstNode*)assign;
            stmt = (JsAstNode*)es;
        } else if (is_exported && snt == JS_AST_NODE_VARIABLE_DECLARATION) {
            // rewrite: export const x = 1; → Foo.x = 1;
            JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)stmt;
            JsAstNode* rewritten_first = NULL;
            JsAstNode* rewritten_last = NULL;
            for (JsAstNode* d = vd->declarations; d; d = d->next) {
                JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)d;
                if (!decl->id || !decl->init) continue;

                JsMemberNode* mem = (JsMemberNode*)alloc_ts_ast_node(tp,
                    JS_AST_NODE_MEMBER_EXPRESSION, dummy, sizeof(JsMemberNode));
                JsIdentifierNode* obj = (JsIdentifierNode*)alloc_ts_ast_node(tp,
                    JS_AST_NODE_IDENTIFIER, dummy, sizeof(JsIdentifierNode));
                obj->name = ns->name;
                mem->object = (JsAstNode*)obj;
                mem->property = decl->id;
                mem->computed = false;

                JsAssignmentNode* assign = (JsAssignmentNode*)alloc_ts_ast_node(tp,
                    JS_AST_NODE_ASSIGNMENT_EXPRESSION, dummy, sizeof(JsAssignmentNode));
                assign->op = JS_OP_ASSIGN;
                assign->left = (JsAstNode*)mem;
                assign->right = decl->init;

                JsExpressionStatementNode* es = (JsExpressionStatementNode*)alloc_ts_ast_node(tp,
                    JS_AST_NODE_EXPRESSION_STATEMENT, dummy, sizeof(JsExpressionStatementNode));
                es->expression = (JsAstNode*)assign;

                JsAstNode* s = (JsAstNode*)es;
                if (!rewritten_first) { rewritten_first = s; rewritten_last = s; }
                else { rewritten_last->next = s; rewritten_last = s; }
            }
            // splice the rewritten assignments into the body
            if (rewritten_first) {
                if (!iife_first) { iife_first = rewritten_first; iife_last = rewritten_last; }
                else { iife_last->next = rewritten_first; iife_last = rewritten_last; }
            }
            continue;
        }

        stmt->next = NULL;
        if (!iife_first) { iife_first = stmt; iife_last = stmt; }
        else { iife_last->next = stmt; iife_last = stmt; }
    }

    // build IIFE body block
    JsBlockNode* iife_block = (JsBlockNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_BLOCK_STATEMENT, dummy, sizeof(JsBlockNode));
    iife_block->statements = iife_first;

    // build IIFE function: function(Foo) { ... }
    JsFunctionNode* iife_fn = (JsFunctionNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_FUNCTION_EXPRESSION, dummy, sizeof(JsFunctionNode));
    JsIdentifierNode* param = (JsIdentifierNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_IDENTIFIER, dummy, sizeof(JsIdentifierNode));
    param->name = ns->name;
    iife_fn->params = (JsAstNode*)param;
    iife_fn->body = (JsAstNode*)iife_block;

    // build argument: Foo || (Foo = {})
    JsIdentifierNode* arg_left = (JsIdentifierNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_IDENTIFIER, dummy, sizeof(JsIdentifierNode));
    arg_left->name = ns->name;

    JsIdentifierNode* assign_lhs = (JsIdentifierNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_IDENTIFIER, dummy, sizeof(JsIdentifierNode));
    assign_lhs->name = ns->name;

    JsObjectNode* empty_obj = (JsObjectNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_OBJECT_EXPRESSION, dummy, sizeof(JsObjectNode));

    JsAssignmentNode* fallback_assign = (JsAssignmentNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_ASSIGNMENT_EXPRESSION, dummy, sizeof(JsAssignmentNode));
    fallback_assign->op = JS_OP_ASSIGN;
    fallback_assign->left = (JsAstNode*)assign_lhs;
    fallback_assign->right = (JsAstNode*)empty_obj;

    JsBinaryNode* or_expr = (JsBinaryNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_BINARY_EXPRESSION, dummy, sizeof(JsBinaryNode));
    or_expr->op = JS_OP_OR;
    or_expr->left = (JsAstNode*)arg_left;
    or_expr->right = (JsAstNode*)fallback_assign;

    // build call expression: (function(Foo) { ... })(Foo || (Foo = {}))
    JsCallNode* iife_call = (JsCallNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_CALL_EXPRESSION, dummy, sizeof(JsCallNode));
    iife_call->callee = (JsAstNode*)iife_fn;
    iife_call->arguments = (JsAstNode*)or_expr;

    JsExpressionStatementNode* call_stmt = (JsExpressionStatementNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_EXPRESSION_STATEMENT, dummy, sizeof(JsExpressionStatementNode));
    call_stmt->expression = (JsAstNode*)iife_call;

    // register namespace name in scope
    if (ns->name) {
        js_scope_define(tp, ns->name, (JsAstNode*)ns_decl, JS_VAR_VAR);
    }

    // link: var Foo; → (function(Foo) { ... })(Foo || (Foo = {}));
    var_stmt->base.next = (JsAstNode*)call_stmt;
    return (JsAstNode*)var_stmt;
}

// ============================================================================
// Decorator desugaring
//
// @decorator class Foo { }
// → let Foo = class Foo { }; Foo = decorator(Foo) ?? Foo;
// ============================================================================

static JsAstNode* ts_lower_class_with_decorators(TsTranspiler* tp,
    TsDecoratorNode** decorators, int deco_count, JsAstNode* class_node)
{
    Pool* pool = tp->ast_pool;
    JsClassNode* cls = (JsClassNode*)class_node;
    TSNode dummy = class_node->node;
    if (!cls->name) return class_node;

    // convert class declaration to expression: let ClassName = class ClassName { }
    cls->base.node_type = JS_AST_NODE_CLASS_EXPRESSION;

    JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_VARIABLE_DECLARATOR, dummy, sizeof(JsVariableDeclaratorNode));
    JsIdentifierNode* id = (JsIdentifierNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_IDENTIFIER, dummy, sizeof(JsIdentifierNode));
    id->name = cls->name;
    decl->id = (JsAstNode*)id;
    decl->init = class_node;

    JsVariableDeclarationNode* var_decl = (JsVariableDeclarationNode*)alloc_ts_ast_node(tp,
        JS_AST_NODE_VARIABLE_DECLARATION, dummy, sizeof(JsVariableDeclarationNode));
    var_decl->kind = JS_VAR_LET;
    var_decl->declarations = (JsAstNode*)decl;

    JsAstNode* result_last = (JsAstNode*)var_decl;

    // apply decorators in reverse order (innermost first):
    // ClassName = deco(ClassName) ?? ClassName;
    for (int i = deco_count - 1; i >= 0; i--) {
        TsDecoratorNode* dec = decorators[i];
        if (!dec->expression) continue;

        // deco(ClassName)
        JsIdentifierNode* cls_ref = (JsIdentifierNode*)alloc_ts_ast_node(tp,
            JS_AST_NODE_IDENTIFIER, dummy, sizeof(JsIdentifierNode));
        cls_ref->name = cls->name;

        JsCallNode* call = (JsCallNode*)alloc_ts_ast_node(tp,
            JS_AST_NODE_CALL_EXPRESSION, dummy, sizeof(JsCallNode));
        call->callee = dec->expression;
        call->arguments = (JsAstNode*)cls_ref;

        // deco(ClassName) ?? ClassName
        JsIdentifierNode* fallback = (JsIdentifierNode*)alloc_ts_ast_node(tp,
            JS_AST_NODE_IDENTIFIER, dummy, sizeof(JsIdentifierNode));
        fallback->name = cls->name;

        JsBinaryNode* coalesce = (JsBinaryNode*)alloc_ts_ast_node(tp,
            JS_AST_NODE_BINARY_EXPRESSION, dummy, sizeof(JsBinaryNode));
        coalesce->op = JS_OP_NULLISH_COALESCE;
        coalesce->left = (JsAstNode*)call;
        coalesce->right = (JsAstNode*)fallback;

        // ClassName = deco(ClassName) ?? ClassName
        JsIdentifierNode* assign_lhs = (JsIdentifierNode*)alloc_ts_ast_node(tp,
            JS_AST_NODE_IDENTIFIER, dummy, sizeof(JsIdentifierNode));
        assign_lhs->name = cls->name;

        JsAssignmentNode* assign = (JsAssignmentNode*)alloc_ts_ast_node(tp,
            JS_AST_NODE_ASSIGNMENT_EXPRESSION, dummy, sizeof(JsAssignmentNode));
        assign->op = JS_OP_ASSIGN;
        assign->left = (JsAstNode*)assign_lhs;
        assign->right = (JsAstNode*)coalesce;

        JsExpressionStatementNode* es = (JsExpressionStatementNode*)alloc_ts_ast_node(tp,
            JS_AST_NODE_EXPRESSION_STATEMENT, dummy, sizeof(JsExpressionStatementNode));
        es->expression = (JsAstNode*)assign;

        result_last->next = (JsAstNode*)es;
        result_last = (JsAstNode*)es;
    }

    return (JsAstNode*)var_decl;
}

// ============================================================================
// Strip type-only nodes from AST before passing to JS transpiler
// ============================================================================

static JsAstNode* ts_strip_type_only_nodes(TsTranspiler* tp, JsAstNode* body) {
    JsAstNode* first = NULL;
    JsAstNode* last = NULL;

    // collect pending decorators for class declarations
    TsDecoratorNode* decorators[16];
    int deco_count = 0;

    for (JsAstNode* node = body; node; ) {
        JsAstNode* next = node->next;
        int nt = node->node_type;

        // skip type-only declarations (already registered in type registry)
        if (nt == (int)TS_AST_NODE_INTERFACE ||
            nt == (int)TS_AST_NODE_TYPE_ALIAS ||
            nt == (int)TS_AST_NODE_AMBIENT_DECLARATION) {
            node = next;
            continue;
        }

        // collect decorators — they precede the class they decorate
        if (nt == (int)TS_AST_NODE_DECORATOR) {
            if (deco_count < 16) {
                decorators[deco_count++] = (TsDecoratorNode*)node;
            }
            node = next;
            continue;
        }

        // lower enum declarations to JS object literals
        if (nt == (int)TS_AST_NODE_ENUM_DECLARATION) {
            TsEnumDeclarationNode* enum_decl = (TsEnumDeclarationNode*)node;
            if (enum_decl->is_const) {
                // const enums are inlined at usage sites; skip the declaration
                node = next;
                continue;
            }
            node = ts_lower_enum_to_js(tp, enum_decl);
            node->next = NULL;
            deco_count = 0;
        }

        // lower namespace declarations to IIFE
        if (nt == (int)TS_AST_NODE_NAMESPACE_DECLARATION) {
            TsNamespaceDeclarationNode* ns = (TsNamespaceDeclarationNode*)node;
            node = ts_lower_namespace_to_js(tp, ns);
            // lower TS expressions inside the generated IIFE body
            node = ts_lower_expr_tree(tp, node);
            // ts_lower_namespace_to_js returns linked nodes (var + call)
            // find the tail
            JsAstNode* tail = node;
            while (tail->next) {
                tail->next = ts_lower_expr_tree(tp, tail->next);
                tail = tail->next;
            }
            // splice: add all linked nodes
            if (!first) { first = node; last = tail; }
            else { last->next = node; last = tail; }
            deco_count = 0;
            node = next;
            continue;
        }

        // apply pending decorators to class declarations
        if (deco_count > 0 && (nt == JS_AST_NODE_CLASS_DECLARATION ||
                                nt == JS_AST_NODE_CLASS_EXPRESSION)) {
            node = ts_lower_class_with_decorators(tp, decorators, deco_count, node);
            // lower TS expressions inside the desugared output
            node = ts_lower_expr_tree(tp, node);
            JsAstNode* tail = node;
            while (tail->next) {
                tail->next = ts_lower_expr_tree(tp, tail->next);
                tail = tail->next;
            }
            if (!first) { first = node; last = tail; }
            else { last->next = node; last = tail; }
            deco_count = 0;
            node = next;
            continue;
        }

        // clear decorators if we hit a non-class statement
        if (deco_count > 0 && nt != JS_AST_NODE_CLASS_DECLARATION &&
            nt != JS_AST_NODE_CLASS_EXPRESSION) {
            deco_count = 0;
        }

        // lower TS expression wrappers (as, !, satisfies) throughout the node tree
        node = ts_lower_expr_tree(tp, node);
        node->next = NULL;

        if (!first) {
            first = node;
            last = node;
        } else {
            last->next = node;
            last = node;
        }
        node = next;
    }
    return first;
}

// ============================================================================
// Main entry point
// ============================================================================

Item transpile_ts_to_mir(Runtime* runtime, const char* ts_source, const char* filename) {
    log_debug("ts-mir: starting TypeScript transpilation for '%s'", filename ? filename : "<string>");

    size_t ts_len = strlen(ts_source);

    // create TS transpiler
    TsTranspiler* tp = ts_transpiler_create(runtime);
    if (!tp) {
        log_error("ts-mir: failed to create transpiler");
        return (Item){.item = ITEM_ERROR};
    }

    // Phase 1: Parse the TypeScript source with the TS parser (types preserved in CST)
    if (!js_transpiler_parse(tp, ts_source, ts_len)) {
        log_error("ts-mir: parse failed");
        ts_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    TSNode root = ts_tree_root_node(tp->tree);

    // Phase 2: Build AST from the TS CST (unified builder handles both JS and TS nodes)
    log_debug("ts-mir: building AST...");
    JsAstNode* ts_ast = build_js_ast(tp, root);
    if (!ts_ast) {
        log_error("ts-mir: AST build failed");
        ts_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }
    log_debug("ts-mir: AST built, node_type=%d", ts_ast->node_type);

    // Phase 3: Resolve type annotations (from TS-specific AST nodes)
    log_debug("ts-mir: resolving types...");
    ts_resolve_all_types(tp, ts_ast);
    log_debug("ts-mir: types resolved");

    // Strip type-only nodes from program body before passing to JS transpiler
    if (ts_ast->node_type == JS_AST_NODE_PROGRAM) {
        JsProgramNode* prog = (JsProgramNode*)ts_ast;
        prog->body = ts_strip_type_only_nodes(tp, prog->body);
        log_debug("ts-mir: type-only nodes stripped");
    }

    // Phase 4: Delegate to JS MIR transpiler with the pre-built AST
    log_debug("ts-mir: delegating to JS MIR transpiler...");
    Item result = transpile_js_ast_to_mir(runtime, tp, ts_ast, filename);
    log_debug("ts-mir: JS MIR transpiler returned");

    ts_transpiler_destroy(tp);
    log_debug("ts-mir: transpilation completed");
    return result;
}
