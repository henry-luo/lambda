#include "js_transpiler.hpp"
#include "../../lib/log.h"
#include <cstdio>

static void print_js_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
}

static void print_js_label(int indent, const char* label) {
    print_js_indent(indent);
    printf("%s\n", label);
}

static const char* js_node_type_name(JsAstNodeType type) {
    switch (type) {
        case JS_AST_NODE_NULL: return "null";
        case JS_AST_NODE_PROGRAM: return "program";
        case JS_AST_NODE_FUNCTION_DECLARATION: return "function_declaration";
        case JS_AST_NODE_VARIABLE_DECLARATION: return "variable_declaration";
        case JS_AST_NODE_EXPRESSION_STATEMENT: return "expression_statement";
        case JS_AST_NODE_BLOCK_STATEMENT: return "block_statement";
        case JS_AST_NODE_IF_STATEMENT: return "if_statement";
        case JS_AST_NODE_WHILE_STATEMENT: return "while_statement";
        case JS_AST_NODE_FOR_STATEMENT: return "for_statement";
        case JS_AST_NODE_RETURN_STATEMENT: return "return_statement";
        case JS_AST_NODE_BREAK_STATEMENT: return "break_statement";
        case JS_AST_NODE_CONTINUE_STATEMENT: return "continue_statement";
        case JS_AST_NODE_IDENTIFIER: return "identifier";
        case JS_AST_NODE_LITERAL: return "literal";
        case JS_AST_NODE_BINARY_EXPRESSION: return "binary_expression";
        case JS_AST_NODE_UNARY_EXPRESSION: return "unary_expression";
        case JS_AST_NODE_ASSIGNMENT_EXPRESSION: return "assignment_expression";
        case JS_AST_NODE_CALL_EXPRESSION: return "call_expression";
        case JS_AST_NODE_MEMBER_EXPRESSION: return "member_expression";
        case JS_AST_NODE_ARRAY_EXPRESSION: return "array_expression";
        case JS_AST_NODE_OBJECT_EXPRESSION: return "object_expression";
        case JS_AST_NODE_FUNCTION_EXPRESSION: return "function_expression";
        case JS_AST_NODE_ARROW_FUNCTION: return "arrow_function";
        case JS_AST_NODE_CONDITIONAL_EXPRESSION: return "conditional_expression";
        case JS_AST_NODE_TEMPLATE_LITERAL: return "template_literal";
        case JS_AST_NODE_TEMPLATE_ELEMENT: return "template_element";
        case JS_AST_NODE_SPREAD_ELEMENT: return "spread_element";
        case JS_AST_NODE_CLASS_DECLARATION: return "class_declaration";
        case JS_AST_NODE_CLASS_EXPRESSION: return "class_expression";
        case JS_AST_NODE_METHOD_DEFINITION: return "method_definition";
        case JS_AST_NODE_TRY_STATEMENT: return "try_statement";
        case JS_AST_NODE_CATCH_CLAUSE: return "catch_clause";
        case JS_AST_NODE_FINALLY_CLAUSE: return "finally_clause";
        case JS_AST_NODE_THROW_STATEMENT: return "throw_statement";
        case JS_AST_NODE_ASSIGNMENT_PATTERN: return "assignment_pattern";
        case JS_AST_NODE_ARRAY_PATTERN: return "array_pattern";
        case JS_AST_NODE_OBJECT_PATTERN: return "object_pattern";
        case JS_AST_NODE_VARIABLE_DECLARATOR: return "variable_declarator";
        case JS_AST_NODE_PROPERTY: return "property";
        case JS_AST_NODE_PARAMETER: return "parameter";
        case JS_AST_NODE_REST_ELEMENT: return "rest_element";
        case JS_AST_NODE_REST_PROPERTY: return "rest_property";
        default: return "unknown";
    }
}

void print_js_ast_node(JsAstNode* node, int indent) {
    if (!node) {
        print_js_indent(indent);
        printf("(null)\n");
        return;
    }
    
    const char* type_name = js_node_type_name(node->node_type);
    print_js_indent(indent);
    printf("[%s]\n", type_name);
    
    switch (node->node_type) {
        case JS_AST_NODE_PROGRAM: {
            JsProgramNode* program = (JsProgramNode*)node;
            print_js_label(indent + 1, "body:");
            JsAstNode* stmt = program->body;
            while (stmt) {
                print_js_ast_node(stmt, indent + 2);
                stmt = stmt->next;
            }
            break;
        }
        case JS_AST_NODE_VARIABLE_DECLARATION: {
            JsVariableDeclarationNode* var_decl = (JsVariableDeclarationNode*)node;
            print_js_label(indent + 1, "kind:");
            print_js_indent(indent + 2);
            printf("%s\n", var_decl->kind == JS_VAR_VAR ? "var" : 
                          var_decl->kind == JS_VAR_LET ? "let" : "const");
            print_js_label(indent + 1, "declarations:");
            JsAstNode* decl = var_decl->declarations;
            while (decl) {
                print_js_ast_node(decl, indent + 2);
                decl = decl->next;
            }
            break;
        }
        case JS_AST_NODE_VARIABLE_DECLARATOR: {
            JsVariableDeclaratorNode* declarator = (JsVariableDeclaratorNode*)node;
            print_js_label(indent + 1, "id:");
            print_js_ast_node(declarator->id, indent + 2);
            if (declarator->init) {
                print_js_label(indent + 1, "init:");
                print_js_ast_node(declarator->init, indent + 2);
            }
            break;
        }
        case JS_AST_NODE_IDENTIFIER: {
            JsIdentifierNode* id = (JsIdentifierNode*)node;
            print_js_indent(indent + 1);
            printf("name: %s\n", id->name ? id->name->chars : "(null)");
            break;
        }
        case JS_AST_NODE_LITERAL: {
            JsLiteralNode* literal = (JsLiteralNode*)node;
            print_js_indent(indent + 1);
            switch (literal->literal_type) {
                case JS_LITERAL_NUMBER:
                    printf("number: %g\n", literal->value.number_value);
                    break;
                case JS_LITERAL_STRING:
                    printf("string: \"%s\"\n", literal->value.string_value ? literal->value.string_value->chars : "(null)");
                    break;
                case JS_LITERAL_BOOLEAN:
                    printf("boolean: %s\n", literal->value.boolean_value ? "true" : "false");
                    break;
                case JS_LITERAL_NULL:
                    printf("null\n");
                    break;
                case JS_LITERAL_UNDEFINED:
                    printf("undefined\n");
                    break;
            }
            break;
        }
        case JS_AST_NODE_BINARY_EXPRESSION: {
            JsBinaryNode* binary = (JsBinaryNode*)node;
            print_js_label(indent + 1, "operator:");
            print_js_indent(indent + 2);
            printf("%d\n", binary->op);
            print_js_label(indent + 1, "left:");
            print_js_ast_node(binary->left, indent + 2);
            print_js_label(indent + 1, "right:");
            print_js_ast_node(binary->right, indent + 2);
            break;
        }
        case JS_AST_NODE_EXPRESSION_STATEMENT: {
            JsExpressionStatementNode* expr_stmt = (JsExpressionStatementNode*)node;
            print_js_label(indent + 1, "expression:");
            print_js_ast_node(expr_stmt->expression, indent + 2);
            break;
        }
        default:
            print_js_indent(indent + 1);
            printf("(not implemented for printing)\n");
            break;
    }
}
