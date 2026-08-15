// js_ast_children.cpp — one description of every AST node's child edges.
//
// The MIR walkers each carried their own 40-case switch that spelled out the
// same parent/child relationships. This table states them once; a walker keeps
// only the cases it finds interesting and delegates the rest to
// js_ast_visit_children.
//
// Ordering is part of the contract: children are visited in source order, and
// a walker that deliberately skips a child (e.g. not descending into a nested
// function) must say so as an explicit case rather than relying on the table.

#include "js_ast.hpp"

namespace {

enum ChildKind : uint8_t {
    CHILD_NODE = 0,  // a single child pointer
    CHILD_LIST = 1,  // head of a ->next-linked sibling list
};

// AST node structs use inheritance, so offsetof is not available on them; a
// captureless accessor lambda gives the same table with no layout assumption.
typedef JsAstNode** (*ChildAccessor)(JsAstNode*);

struct ChildSlot {
    ChildAccessor get;
    uint8_t kind;
};

struct ChildRow {
    JsAstNodeType type;
    uint8_t count;
    ChildSlot slots[4];
};

#define N(Type, field) \
    { [](JsAstNode* n) -> JsAstNode** { return (JsAstNode**)&((Type*)n)->field; }, CHILD_NODE }
#define L(Type, field) \
    { [](JsAstNode* n) -> JsAstNode** { return (JsAstNode**)&((Type*)n)->field; }, CHILD_LIST }

// Rows are looked up linearly; the table is small and the walkers that use it
// are compile-time passes, not a hot runtime path.
const ChildRow kChildRows[] = {
    { JS_AST_NODE_PROGRAM,               1, { L(JsProgramNode, body) } },
    { JS_AST_NODE_BLOCK_STATEMENT,       1, { L(JsBlockNode, statements) } },
    { JS_AST_NODE_STATIC_BLOCK,          1, { N(JsStaticBlockNode, body) } },
    { JS_AST_NODE_VARIABLE_DECLARATION,  1, { L(JsVariableDeclarationNode, declarations) } },
    { JS_AST_NODE_VARIABLE_DECLARATOR,   2, { N(JsVariableDeclaratorNode, id),
                                              N(JsVariableDeclaratorNode, init) } },
    { JS_AST_NODE_EXPRESSION_STATEMENT,  1, { N(JsExpressionStatementNode, expression) } },
    { JS_AST_NODE_RETURN_STATEMENT,      1, { N(JsReturnNode, argument) } },
    { JS_AST_NODE_THROW_STATEMENT,       1, { N(JsThrowNode, argument) } },
    { JS_AST_NODE_IF_STATEMENT,          3, { N(JsIfNode, test), N(JsIfNode, consequent),
                                              N(JsIfNode, alternate) } },
    { JS_AST_NODE_CONDITIONAL_EXPRESSION, 3, { N(JsConditionalNode, test),
                                              N(JsConditionalNode, consequent),
                                              N(JsConditionalNode, alternate) } },
    { JS_AST_NODE_WHILE_STATEMENT,       2, { N(JsWhileNode, test), N(JsWhileNode, body) } },
    { JS_AST_NODE_DO_WHILE_STATEMENT,    2, { N(JsDoWhileNode, body), N(JsDoWhileNode, test) } },
    { JS_AST_NODE_FOR_STATEMENT,         4, { N(JsForNode, init), N(JsForNode, test),
                                              N(JsForNode, update), N(JsForNode, body) } },
    { JS_AST_NODE_FOR_OF_STATEMENT,      3, { N(JsForOfNode, left), N(JsForOfNode, right),
                                              N(JsForOfNode, body) } },
    { JS_AST_NODE_FOR_IN_STATEMENT,      3, { N(JsForInNode, left), N(JsForInNode, right),
                                              N(JsForInNode, body) } },
    { JS_AST_NODE_SWITCH_STATEMENT,      2, { N(JsSwitchNode, discriminant),
                                              L(JsSwitchNode, cases) } },
    { JS_AST_NODE_SWITCH_CASE,           2, { N(JsSwitchCaseNode, test),
                                              L(JsSwitchCaseNode, consequent) } },
    { JS_AST_NODE_TRY_STATEMENT,         3, { N(JsTryNode, block), N(JsTryNode, handler),
                                              N(JsTryNode, finalizer) } },
    { JS_AST_NODE_CATCH_CLAUSE,          2, { N(JsCatchNode, param), N(JsCatchNode, body) } },
    { JS_AST_NODE_LABELED_STATEMENT,     1, { N(JsLabeledStatementNode, body) } },
    { JS_AST_NODE_WITH_STATEMENT,        2, { N(JsWithStatementNode, object),
                                              N(JsWithStatementNode, body) } },
    { JS_AST_NODE_ARRAY_EXPRESSION,      1, { L(JsArrayNode, elements) } },
    { JS_AST_NODE_ARRAY_PATTERN,         1, { L(JsArrayPatternNode, elements) } },
    { JS_AST_NODE_SEQUENCE_EXPRESSION,   1, { L(JsSequenceNode, elements) } },
    { JS_AST_NODE_OBJECT_EXPRESSION,     1, { L(JsObjectNode, properties) } },
    { JS_AST_NODE_OBJECT_PATTERN,        1, { L(JsObjectPatternNode, properties) } },
    { JS_AST_NODE_PROPERTY,              2, { N(JsPropertyNode, key), N(JsPropertyNode, value) } },
    { JS_AST_NODE_BINARY_EXPRESSION,     2, { N(JsBinaryNode, left), N(JsBinaryNode, right) } },
    { JS_AST_NODE_ASSIGNMENT_EXPRESSION, 2, { N(JsAssignmentNode, left),
                                              N(JsAssignmentNode, right) } },
    { JS_AST_NODE_ASSIGNMENT_PATTERN,    2, { N(JsAssignmentPatternNode, left),
                                              N(JsAssignmentPatternNode, right) } },
    { JS_AST_NODE_UNARY_EXPRESSION,      1, { N(JsUnaryNode, operand) } },
    { JS_AST_NODE_CALL_EXPRESSION,       2, { N(JsCallNode, callee), L(JsCallNode, arguments) } },
    { JS_AST_NODE_NEW_EXPRESSION,        2, { N(JsCallNode, callee), L(JsCallNode, arguments) } },
    { JS_AST_NODE_MEMBER_EXPRESSION,     2, { N(JsMemberNode, object), N(JsMemberNode, property) } },
    { JS_AST_NODE_SPREAD_ELEMENT,        1, { N(JsSpreadElementNode, argument) } },
    { JS_AST_NODE_REST_ELEMENT,          1, { N(JsSpreadElementNode, argument) } },
    { JS_AST_NODE_REST_PROPERTY,         1, { N(JsSpreadElementNode, argument) } },
    { JS_AST_NODE_YIELD_EXPRESSION,      1, { N(JsYieldNode, argument) } },
    { JS_AST_NODE_AWAIT_EXPRESSION,      1, { N(JsAwaitNode, argument) } },
    { JS_AST_NODE_FUNCTION_DECLARATION,  2, { L(JsFunctionNode, params), N(JsFunctionNode, body) } },
    { JS_AST_NODE_FUNCTION_EXPRESSION,   2, { L(JsFunctionNode, params), N(JsFunctionNode, body) } },
    { JS_AST_NODE_ARROW_FUNCTION,        2, { L(JsFunctionNode, params), N(JsFunctionNode, body) } },
    { JS_AST_NODE_CLASS_DECLARATION,     2, { N(JsClassNode, superclass), N(JsClassNode, body) } },
    { JS_AST_NODE_CLASS_EXPRESSION,      2, { N(JsClassNode, superclass), N(JsClassNode, body) } },
    { JS_AST_NODE_FIELD_DEFINITION,      2, { N(JsFieldDefinitionNode, key),
                                              N(JsFieldDefinitionNode, value) } },
    { JS_AST_NODE_METHOD_DEFINITION,     1, { N(JsMethodDefinitionNode, key) } },
    { JS_AST_NODE_TEMPLATE_LITERAL,      2, { L(JsTemplateLiteralNode, quasis),
                                              L(JsTemplateLiteralNode, expressions) } },
    { JS_AST_NODE_TAGGED_TEMPLATE,       2, { N(JsTaggedTemplateNode, tag),
                                              N(JsTaggedTemplateNode, quasi) } },
    { JS_AST_NODE_IMPORT_DECLARATION,    1, { L(JsImportNode, specifiers) } },
    { JS_AST_NODE_EXPORT_DECLARATION,    2, { N(JsExportNode, declaration),
                                              L(JsExportNode, specifiers) } },
};

#undef N
#undef L

const ChildRow* row_for(JsAstNodeType type) {
    for (size_t i = 0; i < sizeof(kChildRows) / sizeof(kChildRows[0]); i++) {
        if (kChildRows[i].type == type) return &kChildRows[i];
    }
    return NULL;
}

JsAstNode* child_at(JsAstNode* node, const ChildSlot& slot) {
    JsAstNode** location = slot.get(node);
    return location ? *location : NULL;
}

}  // namespace

bool js_ast_has_child_table(JsAstNode* node) {
    return node && row_for(node->node_type) != NULL;
}

void js_ast_visit_children(JsAstNode* node, JsAstChildVisit visit, void* ctx) {
    if (!node || !visit) return;
    const ChildRow* row = row_for(node->node_type);
    if (!row) return;
    for (uint8_t i = 0; i < row->count; i++) {
        JsAstNode* child = child_at(node, row->slots[i]);
        if (row->slots[i].kind == CHILD_LIST) {
            for (JsAstNode* item = child; item; item = item->next) visit(item, ctx);
        } else if (child) {
            visit(child, ctx);
        }
    }
}

bool js_ast_any_child(JsAstNode* node, JsAstChildPredicate predicate, void* ctx) {
    if (!node || !predicate) return false;
    const ChildRow* row = row_for(node->node_type);
    if (!row) return false;
    for (uint8_t i = 0; i < row->count; i++) {
        JsAstNode* child = child_at(node, row->slots[i]);
        if (row->slots[i].kind == CHILD_LIST) {
            for (JsAstNode* item = child; item; item = item->next) {
                if (predicate(item, ctx)) return true;
            }
        } else if (child && predicate(child, ctx)) {
            return true;
        }
    }
    return false;
}
