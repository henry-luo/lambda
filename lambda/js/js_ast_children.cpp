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
#include "../runtime/ast-core.hpp"
#include "../ts/ts_ast.hpp"

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
    { AST_NODE_LOOP,                     4, { N(AstLoopControlNode, init), N(AstLoopControlNode, test),
                                              N(AstLoopControlNode, update), N(AstLoopControlNode, body) } },
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

typedef bool (*ChildAction)(JsAstNode* child, void* ctx);

static bool walk_child_row(JsAstNode* node, const ChildRow* row,
        ChildAction action, void* ctx) {
    if (node->node_type == AST_NODE_LOOP &&
            ((AstLoopControlNode*)node)->form == LOOP_FORM_DO_WHILE) {
        JsAstNode* body = child_at(node, row->slots[3]);
        JsAstNode* test = child_at(node, row->slots[1]);
        return (body && action(body, ctx)) || (test && action(test, ctx));
    }
    for (uint8_t i = 0; i < row->count; i++) {
        JsAstNode* child = child_at(node, row->slots[i]);
        if (row->slots[i].kind == CHILD_LIST) {
            for (JsAstNode* item = child; item; item = item->next) {
                if (action(item, ctx)) return true;
            }
        } else if (child && action(child, ctx)) {
            return true;
        }
    }
    return false;
}

struct VisitContext {
    JsAstChildVisit visit;
    void* ctx;
};

static bool visit_child(JsAstNode* child, void* opaque) {
    VisitContext* context = (VisitContext*)opaque;
    context->visit(child, context->ctx);
    return false;
}

}  // namespace

bool js_ast_publish_extension_facts(AstNode* node, struct AstIndex* index) {
    if (!node || !index) return true;
    switch (node->node_type) {
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* n = (JsIfNode*)node;
        return ast_index_publish_scope(index, n->consequent_vars) &&
            ast_index_publish_scope(index, n->alternate_vars);
    }
    case JS_AST_NODE_CLASS_EXPRESSION: return ast_index_publish_scope(index, ((JsClassNode*)node)->expression_scope);
    case JS_AST_NODE_CATCH_CLAUSE: return ast_index_publish_scope(index, ((JsCatchNode*)node)->vars);
    case JS_AST_NODE_SWITCH_STATEMENT: return ast_index_publish_scope(index, ((JsSwitchNode*)node)->vars);
    case JS_AST_NODE_FOR_OF_STATEMENT: case JS_AST_NODE_FOR_IN_STATEMENT:
        return ast_index_publish_scope(index, ((JsForOfNode*)node)->vars);
    default:
        return true;
    }
}

void js_ast_visit_children(JsAstNode* node, JsAstChildVisit visit, void* ctx) {
    if (!node || !visit) return;
    const ChildRow* row = row_for(node->node_type);
    if (!row) return;
    VisitContext context = {visit, ctx};
    walk_child_row(node, row, visit_child, &context);
}

bool js_ast_any_child(JsAstNode* node, JsAstChildPredicate predicate, void* ctx) {
    if (!node || !predicate) return false;
    const ChildRow* row = row_for(node->node_type);
    if (!row) return false;
    return walk_child_row(node, row, predicate, ctx);
}

void js_ast_visit_extension_children(AstNode* node, AstChildVisitor visitor,
                                     void* ctx) {
    if (!node || !visitor) return;

    // Core-shaped JS nodes retain a few TypeScript edges outside their shared
    // child table. Visit those edges here without repeating their core fields.
    if (node->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
        JsVariableDeclaratorNode* declarator =
            (JsVariableDeclaratorNode*)node;
        if (declarator->ts_type) {
            visitor((AstNode*)declarator->ts_type, node, ctx);
        }
        return;
    }
    if (node->node_type == JS_AST_NODE_FUNCTION_DECLARATION ||
            node->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
            node->node_type == JS_AST_NODE_ARROW_FUNCTION ||
            node->node_type == JS_AST_NODE_METHOD_DEFINITION) {
        JsFunctionNode* function = (JsFunctionNode*)node;
        if (function->ts_return_type) {
            visitor((AstNode*)function->ts_return_type, node, ctx);
        }
        return;
    }

    // AstNodeType owns the storage field while TypeScript reserves values in
    // the same numeric domain. Compare through the shared scalar domain so
    // the TS extension range does not mix unrelated enum types.
    int node_type = node->node_type;
    if (node_type >= TS_AST_NODE_TYPE_ANNOTATION &&
            node_type < TS_AST_NODE__MAX) {
        switch (node_type) {
        case TS_AST_NODE_TYPE_ANNOTATION:
            if (((TsTypeAnnotationNode*)node)->type_expr) {
                visitor((AstNode*)((TsTypeAnnotationNode*)node)->type_expr,
                    node, ctx);
            }
            break;
        case TS_AST_NODE_TYPE_ALIAS: {
            TsTypeAliasNode* alias = (TsTypeAliasNode*)node;
            for (int i = 0; i < alias->type_param_count; i++) {
                if (alias->type_params && alias->type_params[i]) {
                    visitor((AstNode*)alias->type_params[i], node, ctx);
                }
            }
            if (alias->type_expr) visitor((AstNode*)alias->type_expr,
                node, ctx);
            break;
        }
        case TS_AST_NODE_INTERFACE: {
            TsInterfaceNode* iface = (TsInterfaceNode*)node;
            for (int i = 0; i < iface->type_param_count; i++) {
                if (iface->type_params && iface->type_params[i]) {
                    visitor((AstNode*)iface->type_params[i], node, ctx);
                }
            }
            for (int i = 0; i < iface->extends_count; i++) {
                if (iface->extends_types && iface->extends_types[i]) {
                    visitor((AstNode*)iface->extends_types[i], node, ctx);
                }
            }
            if (iface->body) visitor((AstNode*)iface->body, node, ctx);
            break;
        }
        case TS_AST_NODE_TYPE_PARAMETER: {
            TsTypeParamNode* parameter = (TsTypeParamNode*)node;
            if (parameter->constraint) visitor((AstNode*)parameter->constraint,
                node, ctx);
            if (parameter->default_type) visitor(
                (AstNode*)parameter->default_type, node, ctx);
            break;
        }
        case TS_AST_NODE_TYPE_REFERENCE: {
            TsTypeReferenceNode* reference = (TsTypeReferenceNode*)node;
            for (int i = 0; i < reference->type_arg_count; i++) {
                if (reference->type_args && reference->type_args[i]) {
                    visitor((AstNode*)reference->type_args[i], node, ctx);
                }
            }
            break;
        }
        case TS_AST_NODE_UNION_TYPE: {
            TsUnionTypeNode* union_type = (TsUnionTypeNode*)node;
            for (int i = 0; i < union_type->type_count; i++) {
                if (union_type->types && union_type->types[i]) {
                    visitor((AstNode*)union_type->types[i], node, ctx);
                }
            }
            break;
        }
        case TS_AST_NODE_INTERSECTION_TYPE: {
            TsIntersectionTypeNode* intersection =
                (TsIntersectionTypeNode*)node;
            for (int i = 0; i < intersection->type_count; i++) {
                if (intersection->types && intersection->types[i]) {
                    visitor((AstNode*)intersection->types[i], node, ctx);
                }
            }
            break;
        }
        case TS_AST_NODE_TUPLE_TYPE: {
            TsTupleTypeNode* tuple = (TsTupleTypeNode*)node;
            for (int i = 0; i < tuple->element_count; i++) {
                if (tuple->element_types && tuple->element_types[i]) {
                    visitor((AstNode*)tuple->element_types[i], node, ctx);
                }
            }
            break;
        }
        case TS_AST_NODE_ARRAY_TYPE:
            if (((TsArrayTypeNode*)node)->element_type) {
                visitor((AstNode*)((TsArrayTypeNode*)node)->element_type,
                    node, ctx);
            }
            break;
        case TS_AST_NODE_FUNCTION_TYPE: {
            TsFunctionTypeNode* function_type = (TsFunctionTypeNode*)node;
            for (int i = 0; i < function_type->param_count; i++) {
                if (function_type->param_types &&
                        function_type->param_types[i]) {
                    visitor((AstNode*)function_type->param_types[i], node,
                        ctx);
                }
            }
            if (function_type->return_type) {
                visitor((AstNode*)function_type->return_type, node, ctx);
            }
            break;
        }
        case TS_AST_NODE_OBJECT_TYPE:
        case TS_AST_NODE_MAPPED_TYPE: {
            TsObjectTypeNode* object_type = (TsObjectTypeNode*)node;
            for (int i = 0; i < object_type->member_count; i++) {
                if (object_type->member_types && object_type->member_types[i]) {
                    visitor((AstNode*)object_type->member_types[i], node, ctx);
                }
            }
            break;
        }
        case TS_AST_NODE_CONDITIONAL_TYPE: {
            TsConditionalTypeNode* conditional =
                (TsConditionalTypeNode*)node;
            if (conditional->check_type) visitor(
                (AstNode*)conditional->check_type, node, ctx);
            if (conditional->extends_type) visitor(
                (AstNode*)conditional->extends_type, node, ctx);
            if (conditional->true_type) visitor(
                (AstNode*)conditional->true_type, node, ctx);
            if (conditional->false_type) visitor(
                (AstNode*)conditional->false_type, node, ctx);
            break;
        }
        case TS_AST_NODE_PARENTHESIZED_TYPE:
        case TS_AST_NODE_KEYOF_TYPE:
            if (((TsParenthesizedTypeNode*)node)->inner) visitor(
                (AstNode*)((TsParenthesizedTypeNode*)node)->inner, node, ctx);
            break;
        case TS_AST_NODE_AS_EXPRESSION:
        case TS_AST_NODE_SATISFIES_EXPRESSION:
        case TS_AST_NODE_TYPE_ASSERTION: {
            TsTypeExprNode* expression = (TsTypeExprNode*)node;
            if (expression->inner) visitor((AstNode*)expression->inner, node,
                ctx);
            if (expression->target_type) visitor(
                (AstNode*)expression->target_type, node, ctx);
            break;
        }
        case TS_AST_NODE_NON_NULL_EXPRESSION:
            if (((TsNonNullNode*)node)->inner) visitor(
                (AstNode*)((TsNonNullNode*)node)->inner, node, ctx);
            break;
        case TS_AST_NODE_ENUM_DECLARATION: {
            TsEnumDeclarationNode* enumeration =
                (TsEnumDeclarationNode*)node;
            for (int i = 0; i < enumeration->member_count; i++) {
                if (enumeration->members && enumeration->members[i]) {
                    visitor((AstNode*)enumeration->members[i], node, ctx);
                }
            }
            break;
        }
        case TS_AST_NODE_ENUM_MEMBER:
            if (((TsEnumMemberNode*)node)->initializer) visitor(
                (AstNode*)((TsEnumMemberNode*)node)->initializer, node, ctx);
            break;
        case TS_AST_NODE_NAMESPACE_DECLARATION: {
            TsNamespaceDeclarationNode* namespace_node =
                (TsNamespaceDeclarationNode*)node;
            for (int i = 0; i < namespace_node->body_count; i++) {
                if (namespace_node->body && namespace_node->body[i]) {
                    visitor((AstNode*)namespace_node->body[i], node, ctx);
                }
            }
            break;
        }
        case TS_AST_NODE_DECORATOR:
            if (((TsDecoratorNode*)node)->expression) visitor(
                (AstNode*)((TsDecoratorNode*)node)->expression, node, ctx);
            break;
        case TS_AST_NODE_PARAMETER: {
            TsParameterNode* parameter = (TsParameterNode*)node;
            if (parameter->pattern) visitor((AstNode*)parameter->pattern, node,
                ctx);
            if (parameter->ts_type) visitor((AstNode*)parameter->ts_type, node,
                ctx);
            if (parameter->default_value) visitor(
                (AstNode*)parameter->default_value, node, ctx);
            break;
        }
        default:
            // The remaining TS leaves use scalar metadata or a reference name.
            break;
        }
        return;
    }

    if (node->node_type < JS_AST_NODE_TEMPLATE_LITERAL) return;
    const ChildRow* row = row_for(node->node_type);
    if (!row) return;
    for (uint8_t i = 0; i < row->count; i++) {
        JsAstNode* child = child_at((JsAstNode*)node, row->slots[i]);
        // AstIndex's visitor descends through each child's `next` link. Pass
        // only the list head here so the common walker stays the sole sibling
        // traversal authority.
        if (child) visitor((AstNode*)child, node, ctx);
    }
}
