#include "../runtime/emit_ast_dump.h"
#include "../runtime/type_contract.hpp"
#include "js_transpiler.hpp"
#include "../ts/ts_ast.hpp"
#include "../../lib/file.h"
#include "../../lib/mem.h"
#include <stdio.h>
#include <string.h>

static const char* js_dump_kind_name(int type) {
    switch (type) {
        case AST_NODE_NULL: return "AST_NODE_NULL";
        case AST_SCRIPT: return "AST_SCRIPT";
        case AST_NODE_FUNC: return "AST_NODE_FUNC";
        case AST_NODE_VAR_STAM: return "AST_NODE_VAR_STAM";
        case AST_NODE_EXPR_STMT: return "AST_NODE_EXPR_STMT";
        case AST_NODE_BLOCK: return "AST_NODE_BLOCK";
        case AST_NODE_IF_EXPR: return "AST_NODE_IF_EXPR";
        case AST_NODE_LOOP: return "JS_AST_NODE_LOOP";
        case AST_NODE_RETURN_STAM: return "AST_NODE_RETURN_STAM";
        case AST_NODE_BREAK_STAM: return "AST_NODE_BREAK_STAM";
        case AST_NODE_CONTINUE_STAM: return "AST_NODE_CONTINUE_STAM";
        case AST_NODE_IDENT: return "AST_NODE_IDENT";
        case AST_NODE_LITERAL: return "AST_NODE_LITERAL";
        case AST_NODE_BINARY: return "AST_NODE_BINARY";
        case AST_NODE_UNARY: return "AST_NODE_UNARY";
        case AST_NODE_ASSIGN: return "AST_NODE_ASSIGN";
        case AST_NODE_CALL_EXPR: return "AST_NODE_CALL_EXPR";
        case AST_NODE_MEMBER_EXPR: return "AST_NODE_MEMBER_EXPR";
        case AST_NODE_ARRAY: return "AST_NODE_ARRAY";
        case AST_NODE_MAP: return "AST_NODE_MAP";
        case AST_NODE_FUNC_EXPR: return "AST_NODE_FUNC_EXPR";
        case AST_NODE_SPREAD: return "AST_NODE_SPREAD";
        case AST_NODE_CLASS: return "AST_NODE_CLASS";
        case AST_NODE_FIELD: return "AST_NODE_FIELD";
        case AST_NODE_RAISE_STAM: return "AST_NODE_RAISE_STAM";
        case AST_NODE_PARAM: return "AST_NODE_PARAM";
        case AST_NODE_NEW_EXPR: return "AST_NODE_NEW_EXPR";
        case AST_NODE_SEQ: return "AST_NODE_SEQ";
        case AST_NODE_YIELD: return "AST_NODE_YIELD";
        case AST_NODE_AWAIT: return "AST_NODE_AWAIT";
        case AST_NODE_IMPORT: return "AST_NODE_IMPORT";
        case AST_NODE_EXPORT: return "AST_NODE_EXPORT";
        case AST_NODE_ARROW_FUNC: return "AST_NODE_ARROW_FUNC";
        case AST_NODE_CONDITIONAL_EXPR: return "AST_NODE_CONDITIONAL_EXPR";
        case JS_AST_NODE_TEMPLATE_LITERAL: return "JS_AST_NODE_TEMPLATE_LITERAL";
        case JS_AST_NODE_TEMPLATE_ELEMENT: return "JS_AST_NODE_TEMPLATE_ELEMENT";
        case AST_NODE_CLASS_EXPR: return "AST_NODE_CLASS_EXPR";
        case AST_NODE_METHOD: return "AST_NODE_METHOD";
        case JS_AST_NODE_STATIC_BLOCK: return "JS_AST_NODE_STATIC_BLOCK";
        case AST_NODE_TRY_STAM: return "AST_NODE_TRY_STAM";
        case AST_NODE_CATCH_CLAUSE: return "AST_NODE_CATCH_CLAUSE";
        case JS_AST_NODE_FINALLY_CLAUSE: return "JS_AST_NODE_FINALLY_CLAUSE";
        case AST_NODE_ASSIGN_PATTERN: return "AST_NODE_ASSIGN_PATTERN";
        case AST_NODE_ARRAY_PATTERN: return "AST_NODE_ARRAY_PATTERN";
        case AST_NODE_MAP_PATTERN: return "AST_NODE_MAP_PATTERN";
        case AST_NODE_VARIABLE_DECLARATOR: return "AST_NODE_VARIABLE_DECLARATOR";
        case AST_NODE_PROPERTY: return "AST_NODE_PROPERTY";
        case AST_NODE_REST_ELEMENT: return "AST_NODE_REST_ELEMENT";
        case AST_NODE_REST_PROPERTY: return "AST_NODE_REST_PROPERTY";
        case AST_NODE_MATCH_EXPR: return "AST_NODE_MATCH_EXPR";
        case AST_NODE_MATCH_ARM: return "AST_NODE_MATCH_ARM";
        case AST_NODE_FOR_OF_STAM: return "AST_NODE_FOR_OF_STAM";
        case AST_NODE_FOR_IN_STAM: return "AST_NODE_FOR_IN_STAM";
        case JS_AST_NODE_LABELED_STATEMENT: return "JS_AST_NODE_LABELED_STATEMENT";
        case JS_AST_NODE_REGEX: return "JS_AST_NODE_REGEX";
        case AST_NODE_IMPORT_SPECIFIER: return "AST_NODE_IMPORT_SPECIFIER";
        case AST_NODE_EXPORT_SPECIFIER: return "AST_NODE_EXPORT_SPECIFIER";
        case JS_AST_NODE_WITH_STATEMENT: return "JS_AST_NODE_WITH_STATEMENT";
        case JS_AST_NODE_TAGGED_TEMPLATE: return "JS_AST_NODE_TAGGED_TEMPLATE";
        case TS_AST_NODE_PARAMETER: return "TS_AST_NODE_PARAMETER";
        default: return "JS_AST_NODE_UNKNOWN";
    }
}

static const char* js_dump_operator_name(JsOperator op) {
    switch (op) {
        case OPERATOR_ADD: return "add";
        case OPERATOR_SUB: return "sub";
        case OPERATOR_MUL: return "mul";
        case OPERATOR_DIV: return "div";
        case OPERATOR_MOD: return "mod";
        case OPERATOR_JS_EXP: return "exp";
        case OPERATOR_EQ: return "eq";
        case OPERATOR_NE: return "ne";
        case OPERATOR_JS_STRICT_EQ: return "strict_eq";
        case OPERATOR_JS_STRICT_NE: return "strict_ne";
        case OPERATOR_LT: return "lt";
        case OPERATOR_LE: return "le";
        case OPERATOR_GT: return "gt";
        case OPERATOR_GE: return "ge";
        case OPERATOR_AND: return "and";
        case OPERATOR_OR: return "or";
        case OPERATOR_JS_BIT_AND: return "bit_and";
        case OPERATOR_JS_BIT_OR: return "bit_or";
        case OPERATOR_JS_BIT_XOR: return "bit_xor";
        case OPERATOR_JS_LSHIFT: return "bit_lshift";
        case OPERATOR_JS_RSHIFT: return "bit_rshift";
        case OPERATOR_JS_URSHIFT: return "bit_urshift";
        case OPERATOR_NOT: return "not";
        case OPERATOR_JS_BIT_NOT: return "bit_not";
        case OPERATOR_JS_TYPEOF: return "typeof";
        case OPERATOR_JS_VOID: return "void";
        case OPERATOR_JS_DELETE: return "delete";
        case OPERATOR_POS: return "plus";
        case OPERATOR_NEG: return "minus";
        case OPERATOR_JS_INCREMENT: return "increment";
        case OPERATOR_JS_DECREMENT: return "decrement";
        case OPERATOR_ASSIGN: return "assign";
        case OPERATOR_JS_ADD_ASSIGN: return "add_assign";
        case OPERATOR_JS_SUB_ASSIGN: return "sub_assign";
        case OPERATOR_JS_MUL_ASSIGN: return "mul_assign";
        case OPERATOR_JS_DIV_ASSIGN: return "div_assign";
        case OPERATOR_JS_MOD_ASSIGN: return "mod_assign";
        case OPERATOR_JS_EXP_ASSIGN: return "exp_assign";
        case OPERATOR_JS_INSTANCEOF: return "instanceof";
        case OPERATOR_IN: return "in";
        case OPERATOR_JS_NULLISH_COALESCE: return "nullish_coalesce";
        case OPERATOR_JS_NULLISH_ASSIGN: return "nullish_assign";
        case OPERATOR_JS_AND_ASSIGN: return "and_assign";
        case OPERATOR_JS_OR_ASSIGN: return "or_assign";
        default: return "unknown";
    }
}

static void emit_js_dump_node(const char* source, JsAstNode* node, int indent);

static void emit_js_dump_list(const char* source, const char* label, JsAstNode* node, int indent) {
    printf("\n");
    emit_dump_indent(indent);
    printf("(%s", label);
    while (node) {
        printf("\n");
        emit_js_dump_node(source, node, indent + 1);
        node = node->next;
    }
    printf(")");
}

// Publish the node's inferred static type so IP6 fixtures can assert it, the
// same way the Lambda dump does [Type_Infer TIG13/TIG14].
static void js_dump_value_type(JsAstNode* node) {
    if (!node || !node->type) return;
    char name[128];
    lambda_type_format_name(node->type, name, sizeof(name));
    printf(" (value_type \"%s\")", name);
}

static void emit_js_dump_field(const char* source, const char* label, JsAstNode* node, int indent) {
    if (!node) return;
    printf("\n");
    emit_dump_indent(indent);
    printf("(%s\n", label);
    emit_js_dump_node(source, node, indent + 1);
    printf(")");
}

static void emit_js_dump_node(const char* source, JsAstNode* node, int indent) {
    emit_dump_indent(indent);
    if (!node) {
        printf("(null)");
        return;
    }
    printf("(%s", js_dump_kind_name(node->node_type));

    switch (node->node_type) {
        case AST_SCRIPT:
            emit_js_dump_list(source, "body", ((JsProgramNode*)node)->body, indent + 1);
            break;
        case AST_NODE_VAR_STAM: {
            JsVariableDeclarationNode* var_decl = (JsVariableDeclarationNode*)node;
            printf(" (kind %s)", var_decl->kind == JS_VAR_VAR ? "var" : var_decl->kind == JS_VAR_LET ? "let" : "const");
            emit_js_dump_list(source, "declarations", var_decl->declarations, indent + 1);
            break;
        }
        case AST_NODE_VARIABLE_DECLARATOR: {
            JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)node;
            emit_js_dump_field(source, "id", decl->id, indent + 1);
            emit_js_dump_field(source, "init", decl->init, indent + 1);
            break;
        }
        case AST_NODE_IDENT:
            emit_dump_string_field("name", ((JsIdentifierNode*)node)->name);
            break;
        case AST_NODE_LITERAL: {
            JsLiteralNode* lit = (JsLiteralNode*)node;
            if (lit->literal_type == AST_LITERAL_STRING) {
                printf(" (literal string)");
                emit_dump_string_field("value", lit->value.string_value);
            } else if (lit->literal_type == AST_LITERAL_BOOLEAN) {
                printf(" (literal %s)", lit->value.boolean_value ? "true" : "false");
            } else if (lit->literal_type == AST_LITERAL_NULL) {
                printf(" (literal null)");
            } else if (lit->literal_type == AST_LITERAL_UNDEFINED) {
                printf(" (literal undefined)");
            } else {
                printf(" (literal number)");
                emit_dump_source_field(source, node->source_span);
            }
            if (lit->is_bigint) emit_dump_string_field("bigint", lit->bigint_str);
            break;
        }
        case AST_NODE_BINARY: {
            JsBinaryNode* bin = (JsBinaryNode*)node;
            printf(" (op %s)", js_dump_operator_name(bin->op));
            js_dump_value_type(node);
            emit_js_dump_field(source, "left", bin->left, indent + 1);
            emit_js_dump_field(source, "right", bin->right, indent + 1);
            break;
        }
        case AST_NODE_UNARY: {
            JsUnaryNode* un = (JsUnaryNode*)node;
            printf(" (op %s)", js_dump_operator_name(un->op));
            js_dump_value_type(node);
            emit_js_dump_field(source, "operand", un->operand, indent + 1);
            break;
        }
        case AST_NODE_ASSIGN:
        case AST_NODE_ASSIGN_PATTERN: {
            JsAssignmentNode* assign = (JsAssignmentNode*)node;
            printf(" (op %s)", js_dump_operator_name(assign->op));
            emit_js_dump_field(source, "left", assign->left, indent + 1);
            emit_js_dump_field(source, "right", assign->right, indent + 1);
            break;
        }
        case AST_NODE_FUNC:
        case AST_NODE_FUNC_EXPR:
        case AST_NODE_ARROW_FUNC: {
            JsFunctionNode* fn = (JsFunctionNode*)node;
            emit_dump_string_field("name", fn->name);
            emit_js_dump_list(source, "params", fn->params, indent + 1);
            emit_js_dump_field(source, "body", fn->body, indent + 1);
            break;
        }
        case AST_NODE_CALL_EXPR:
        case AST_NODE_NEW_EXPR: {
            JsCallNode* call = (JsCallNode*)node;
            emit_js_dump_field(source, "callee", call->callee, indent + 1);
            emit_js_dump_list(source, "arguments", call->arguments, indent + 1);
            break;
        }
        case AST_NODE_MEMBER_EXPR: {
            JsMemberNode* mem = (JsMemberNode*)node;
            emit_js_dump_field(source, "object", mem->object, indent + 1);
            emit_js_dump_field(source, "property", mem->property, indent + 1);
            break;
        }
        case AST_NODE_ARRAY:
        case AST_NODE_ARRAY_PATTERN:
            emit_js_dump_list(source, "elements", ((JsArrayNode*)node)->elements, indent + 1);
            break;
        case AST_NODE_MAP:
        case AST_NODE_MAP_PATTERN:
            emit_js_dump_list(source, "properties", ((JsObjectNode*)node)->properties, indent + 1);
            break;
        case AST_NODE_PROPERTY: {
            JsPropertyNode* prop = (JsPropertyNode*)node;
            emit_js_dump_field(source, "key", prop->key, indent + 1);
            emit_js_dump_field(source, "value", prop->value, indent + 1);
            break;
        }
        case AST_NODE_EXPR_STMT:
            emit_js_dump_field(source, "expression", ((JsExpressionStatementNode*)node)->expression, indent + 1);
            break;
        case AST_NODE_BLOCK:
            emit_js_dump_list(source, "statements", ((JsBlockNode*)node)->statements, indent + 1);
            break;
        case AST_NODE_FOR_OF_STAM:
        case AST_NODE_FOR_IN_STAM: {
            JsForOfNode* iteration = (JsForOfNode*)node;
            emit_js_dump_field(source, "left", iteration->left, indent + 1);
            emit_js_dump_field(source, "init", iteration->init, indent + 1);
            emit_js_dump_field(source, "right", iteration->right, indent + 1);
            emit_js_dump_field(source, "body", iteration->body, indent + 1);
            break;
        }
        case AST_NODE_IF_EXPR: {
            JsIfNode* if_node = (JsIfNode*)node;
            emit_js_dump_field(source, "test", if_node->test, indent + 1);
            emit_js_dump_field(source, "consequent", if_node->consequent, indent + 1);
            emit_js_dump_field(source, "alternate", if_node->alternate, indent + 1);
            break;
        }
        case AST_NODE_LOOP: {
            AstLoopControlNode* loop = (AstLoopControlNode*)node;
            emit_js_dump_field(source, "init", loop->init, indent + 1);
            emit_js_dump_field(source, "test", loop->test, indent + 1);
            emit_js_dump_field(source, "update", loop->update, indent + 1);
            emit_js_dump_field(source, "body", loop->body, indent + 1);
            break;
        }
        case AST_NODE_RETURN_STAM:
            emit_js_dump_field(source, "argument", ((JsReturnNode*)node)->argument, indent + 1);
            break;
        case AST_NODE_CONDITIONAL_EXPR: {
            JsConditionalNode* cond = (JsConditionalNode*)node;
            emit_js_dump_field(source, "test", cond->test, indent + 1);
            emit_js_dump_field(source, "consequent", cond->consequent, indent + 1);
            emit_js_dump_field(source, "alternate", cond->alternate, indent + 1);
            break;
        }
        case AST_NODE_SPREAD:
        case AST_NODE_REST_ELEMENT:
        case AST_NODE_REST_PROPERTY:
            emit_js_dump_field(source, "argument", ((JsSpreadElementNode*)node)->argument, indent + 1);
            break;
        case AST_NODE_CLASS:
        case AST_NODE_CLASS_EXPR: {
            JsClassNode* cls = (JsClassNode*)node;
            emit_dump_string_field("name", cls->name);
            emit_js_dump_field(source, "superclass", cls->superclass, indent + 1);
            emit_js_dump_field(source, "body", cls->body, indent + 1);
            break;
        }
        case AST_NODE_METHOD: {
            JsMethodDefinitionNode* method = (JsMethodDefinitionNode*)node;
            emit_dump_string_field("name", method->name);
            emit_js_dump_field(source, "key", method->key, indent + 1);
            emit_js_dump_list(source, "params", method->params, indent + 1);
            emit_js_dump_field(source, "body", method->body, indent + 1);
            break;
        }
        case AST_NODE_FIELD: {
            JsFieldDefinitionNode* field = (JsFieldDefinitionNode*)node;
            emit_js_dump_field(source, "key", field->key, indent + 1);
            emit_js_dump_field(source, "value", field->value, indent + 1);
            break;
        }
        case JS_AST_NODE_STATIC_BLOCK:
            emit_js_dump_field(source, "body", ((JsStaticBlockNode*)node)->body, indent + 1);
            break;
        case AST_NODE_TRY_STAM: {
            JsTryNode* tr = (JsTryNode*)node;
            emit_js_dump_field(source, "block", tr->block, indent + 1);
            emit_js_dump_field(source, "handler", tr->handler, indent + 1);
            emit_js_dump_field(source, "finalizer", tr->finalizer, indent + 1);
            break;
        }
        case AST_NODE_CATCH_CLAUSE: {
            JsCatchNode* catch_node = (JsCatchNode*)node;
            emit_js_dump_field(source, "param", catch_node->param, indent + 1);
            emit_js_dump_field(source, "body", catch_node->body, indent + 1);
            break;
        }
        case AST_NODE_RAISE_STAM:
            emit_js_dump_field(source, "argument", ((JsThrowNode*)node)->argument, indent + 1);
            break;
        case AST_NODE_SEQ:
            emit_js_dump_list(source, "expressions", ((JsSequenceNode*)node)->expressions, indent + 1);
            break;
        case AST_NODE_YIELD:
            emit_js_dump_field(source, "argument", ((JsYieldNode*)node)->argument, indent + 1);
            break;
        case AST_NODE_AWAIT:
            emit_js_dump_field(source, "argument", ((JsAwaitNode*)node)->argument, indent + 1);
            break;
        case AST_NODE_IMPORT: {
            JsImportNode* imp = (JsImportNode*)node;
            emit_dump_string_field("source", imp->source);
            emit_dump_string_field("default", imp->default_name);
            emit_dump_string_field("namespace", imp->namespace_name);
            emit_js_dump_list(source, "specifiers", imp->specifiers, indent + 1);
            break;
        }
        case AST_NODE_IMPORT_SPECIFIER: {
            JsImportSpecifierNode* spec = (JsImportSpecifierNode*)node;
            emit_dump_string_field("local", spec->local_name);
            emit_dump_string_field("remote", spec->remote_name);
            break;
        }
        case AST_NODE_EXPORT: {
            JsExportNode* exp = (JsExportNode*)node;
            emit_dump_string_field("source", exp->source);
            emit_js_dump_field(source, "declaration", exp->declaration, indent + 1);
            emit_js_dump_list(source, "specifiers", exp->specifiers, indent + 1);
            break;
        }
        case AST_NODE_EXPORT_SPECIFIER: {
            JsExportSpecifierNode* spec = (JsExportSpecifierNode*)node;
            emit_dump_string_field("local", spec->local_name);
            emit_dump_string_field("export", spec->export_name);
            break;
        }
        default:
            emit_dump_source_field(source, node->source_span);
            break;
    }

    printf(")");
}

extern "C" int emit_js_ast_dump_file(const char* script_path) {
    char* source = read_text_file(script_path);
    if (!source) {
        fprintf(stderr, "Error: Cannot read '%s'\n", script_path);
        return 1;
    }

    JsTranspiler* tp = js_transpiler_create(NULL);
    if (!tp) {
        fprintf(stderr, "Error: Failed to create JS transpiler\n");
        mem_free(source);
        return 1;
    }

    size_t length = strlen(source);
    if (!js_transpiler_parse_c(tp, source, length, JS_PARSE_AUTO)) {
        fprintf(stderr, "Error: Failed to parse '%s'\n", script_path);
        js_transpiler_destroy(tp);
        mem_free(source);
        return 1;
    }

    JsAstNode* ast = (JsAstNode*)tp->ast_root;
    if (!ast) {
        fprintf(stderr, "Error: Failed to build JS AST for '%s'\n", script_path);
        js_transpiler_destroy(tp);
        mem_free(source);
        return 1;
    }

    if (tp->has_errors) {
        fprintf(stderr, "Error: JS AST build errors for '%s'\n", script_path);
        js_transpiler_destroy(tp);
        mem_free(source);
        return 1;
    }

    printf("(ast-dump js\n");
    emit_js_dump_node(source, ast, 1);
    printf(")\n");

    js_transpiler_destroy(tp);
    mem_free(source);
    return 0;
}
