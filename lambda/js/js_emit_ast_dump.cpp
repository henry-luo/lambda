#include "../emit_sexpr.h"
#include "js_transpiler.hpp"
#include "../ts/ts_ast.hpp"
#include "../../lib/file.h"
#include "../../lib/mem.h"
#include <stdio.h>
#include <string.h>

static inline const char* dump_node_src(const char* source, TSNode node, int* out_len) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    *out_len = (int)(end - start);
    return source + start;
}

static void dump_escaped_string(const char* str, int len) {
    putchar('"');
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        switch (c) {
            case '"':  printf("\\\""); break;
            case '\\': printf("\\\\"); break;
            case '\n': printf("\\n"); break;
            case '\r': printf("\\r"); break;
            case '\t': printf("\\t"); break;
            case '\0': printf("\\0"); break;
            default:
                if (c < 0x20) {
                    printf("\\x%02x", c);
                } else {
                    putchar(c);
                }
        }
    }
    putchar('"');
}

static void dump_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
}

static const char* js_dump_kind_name(int type) {
    switch (type) {
        case JS_AST_NODE_NULL: return "JS_AST_NODE_NULL";
        case JS_AST_NODE_PROGRAM: return "JS_AST_NODE_PROGRAM";
        case JS_AST_NODE_FUNCTION_DECLARATION: return "JS_AST_NODE_FUNCTION_DECLARATION";
        case JS_AST_NODE_VARIABLE_DECLARATION: return "JS_AST_NODE_VARIABLE_DECLARATION";
        case JS_AST_NODE_EXPRESSION_STATEMENT: return "JS_AST_NODE_EXPRESSION_STATEMENT";
        case JS_AST_NODE_BLOCK_STATEMENT: return "JS_AST_NODE_BLOCK_STATEMENT";
        case JS_AST_NODE_IF_STATEMENT: return "JS_AST_NODE_IF_STATEMENT";
        case JS_AST_NODE_WHILE_STATEMENT: return "JS_AST_NODE_WHILE_STATEMENT";
        case JS_AST_NODE_FOR_STATEMENT: return "JS_AST_NODE_FOR_STATEMENT";
        case JS_AST_NODE_RETURN_STATEMENT: return "JS_AST_NODE_RETURN_STATEMENT";
        case JS_AST_NODE_BREAK_STATEMENT: return "JS_AST_NODE_BREAK_STATEMENT";
        case JS_AST_NODE_CONTINUE_STATEMENT: return "JS_AST_NODE_CONTINUE_STATEMENT";
        case JS_AST_NODE_IDENTIFIER: return "JS_AST_NODE_IDENTIFIER";
        case JS_AST_NODE_LITERAL: return "JS_AST_NODE_LITERAL";
        case JS_AST_NODE_BINARY_EXPRESSION: return "JS_AST_NODE_BINARY_EXPRESSION";
        case JS_AST_NODE_UNARY_EXPRESSION: return "JS_AST_NODE_UNARY_EXPRESSION";
        case JS_AST_NODE_ASSIGNMENT_EXPRESSION: return "JS_AST_NODE_ASSIGNMENT_EXPRESSION";
        case JS_AST_NODE_CALL_EXPRESSION: return "JS_AST_NODE_CALL_EXPRESSION";
        case JS_AST_NODE_MEMBER_EXPRESSION: return "JS_AST_NODE_MEMBER_EXPRESSION";
        case JS_AST_NODE_ARRAY_EXPRESSION: return "JS_AST_NODE_ARRAY_EXPRESSION";
        case JS_AST_NODE_OBJECT_EXPRESSION: return "JS_AST_NODE_OBJECT_EXPRESSION";
        case JS_AST_NODE_FUNCTION_EXPRESSION: return "JS_AST_NODE_FUNCTION_EXPRESSION";
        case JS_AST_NODE_SPREAD_ELEMENT: return "JS_AST_NODE_SPREAD_ELEMENT";
        case JS_AST_NODE_CLASS_DECLARATION: return "JS_AST_NODE_CLASS_DECLARATION";
        case JS_AST_NODE_FIELD_DEFINITION: return "JS_AST_NODE_FIELD_DEFINITION";
        case JS_AST_NODE_THROW_STATEMENT: return "JS_AST_NODE_THROW_STATEMENT";
        case JS_AST_NODE_PARAMETER: return "JS_AST_NODE_PARAMETER";
        case JS_AST_NODE_NEW_EXPRESSION: return "JS_AST_NODE_NEW_EXPRESSION";
        case JS_AST_NODE_SEQUENCE_EXPRESSION: return "JS_AST_NODE_SEQUENCE_EXPRESSION";
        case JS_AST_NODE_YIELD_EXPRESSION: return "JS_AST_NODE_YIELD_EXPRESSION";
        case JS_AST_NODE_AWAIT_EXPRESSION: return "JS_AST_NODE_AWAIT_EXPRESSION";
        case JS_AST_NODE_IMPORT_DECLARATION: return "JS_AST_NODE_IMPORT_DECLARATION";
        case JS_AST_NODE_EXPORT_DECLARATION: return "JS_AST_NODE_EXPORT_DECLARATION";
        case JS_AST_NODE_ARROW_FUNCTION: return "JS_AST_NODE_ARROW_FUNCTION";
        case JS_AST_NODE_CONDITIONAL_EXPRESSION: return "JS_AST_NODE_CONDITIONAL_EXPRESSION";
        case JS_AST_NODE_TEMPLATE_LITERAL: return "JS_AST_NODE_TEMPLATE_LITERAL";
        case JS_AST_NODE_TEMPLATE_ELEMENT: return "JS_AST_NODE_TEMPLATE_ELEMENT";
        case JS_AST_NODE_CLASS_EXPRESSION: return "JS_AST_NODE_CLASS_EXPRESSION";
        case JS_AST_NODE_METHOD_DEFINITION: return "JS_AST_NODE_METHOD_DEFINITION";
        case JS_AST_NODE_STATIC_BLOCK: return "JS_AST_NODE_STATIC_BLOCK";
        case JS_AST_NODE_TRY_STATEMENT: return "JS_AST_NODE_TRY_STATEMENT";
        case JS_AST_NODE_CATCH_CLAUSE: return "JS_AST_NODE_CATCH_CLAUSE";
        case JS_AST_NODE_FINALLY_CLAUSE: return "JS_AST_NODE_FINALLY_CLAUSE";
        case JS_AST_NODE_ASSIGNMENT_PATTERN: return "JS_AST_NODE_ASSIGNMENT_PATTERN";
        case JS_AST_NODE_ARRAY_PATTERN: return "JS_AST_NODE_ARRAY_PATTERN";
        case JS_AST_NODE_OBJECT_PATTERN: return "JS_AST_NODE_OBJECT_PATTERN";
        case JS_AST_NODE_VARIABLE_DECLARATOR: return "JS_AST_NODE_VARIABLE_DECLARATOR";
        case JS_AST_NODE_PROPERTY: return "JS_AST_NODE_PROPERTY";
        case JS_AST_NODE_REST_ELEMENT: return "JS_AST_NODE_REST_ELEMENT";
        case JS_AST_NODE_REST_PROPERTY: return "JS_AST_NODE_REST_PROPERTY";
        case JS_AST_NODE_SWITCH_STATEMENT: return "JS_AST_NODE_SWITCH_STATEMENT";
        case JS_AST_NODE_SWITCH_CASE: return "JS_AST_NODE_SWITCH_CASE";
        case JS_AST_NODE_DO_WHILE_STATEMENT: return "JS_AST_NODE_DO_WHILE_STATEMENT";
        case JS_AST_NODE_FOR_OF_STATEMENT: return "JS_AST_NODE_FOR_OF_STATEMENT";
        case JS_AST_NODE_FOR_IN_STATEMENT: return "JS_AST_NODE_FOR_IN_STATEMENT";
        case JS_AST_NODE_LABELED_STATEMENT: return "JS_AST_NODE_LABELED_STATEMENT";
        case JS_AST_NODE_REGEX: return "JS_AST_NODE_REGEX";
        case JS_AST_NODE_IMPORT_SPECIFIER: return "JS_AST_NODE_IMPORT_SPECIFIER";
        case JS_AST_NODE_EXPORT_SPECIFIER: return "JS_AST_NODE_EXPORT_SPECIFIER";
        case JS_AST_NODE_WITH_STATEMENT: return "JS_AST_NODE_WITH_STATEMENT";
        case JS_AST_NODE_TAGGED_TEMPLATE: return "JS_AST_NODE_TAGGED_TEMPLATE";
        case TS_AST_NODE_TYPE_ANNOTATION: return "TS_AST_NODE_TYPE_ANNOTATION";
        case TS_AST_NODE_TYPE_ALIAS: return "TS_AST_NODE_TYPE_ALIAS";
        case TS_AST_NODE_INTERFACE: return "TS_AST_NODE_INTERFACE";
        case TS_AST_NODE_TYPE_PARAMETER: return "TS_AST_NODE_TYPE_PARAMETER";
        case TS_AST_NODE_PREDEFINED_TYPE: return "TS_AST_NODE_PREDEFINED_TYPE";
        case TS_AST_NODE_TYPE_REFERENCE: return "TS_AST_NODE_TYPE_REFERENCE";
        case TS_AST_NODE_UNION_TYPE: return "TS_AST_NODE_UNION_TYPE";
        case TS_AST_NODE_INTERSECTION_TYPE: return "TS_AST_NODE_INTERSECTION_TYPE";
        case TS_AST_NODE_ARRAY_TYPE: return "TS_AST_NODE_ARRAY_TYPE";
        case TS_AST_NODE_AS_EXPRESSION: return "TS_AST_NODE_AS_EXPRESSION";
        case TS_AST_NODE_NON_NULL_EXPRESSION: return "TS_AST_NODE_NON_NULL_EXPRESSION";
        case TS_AST_NODE_SATISFIES_EXPRESSION: return "TS_AST_NODE_SATISFIES_EXPRESSION";
        case TS_AST_NODE_ENUM_DECLARATION: return "TS_AST_NODE_ENUM_DECLARATION";
        case TS_AST_NODE_ENUM_MEMBER: return "TS_AST_NODE_ENUM_MEMBER";
        case TS_AST_NODE_NAMESPACE_DECLARATION: return "TS_AST_NODE_NAMESPACE_DECLARATION";
        case TS_AST_NODE_DECORATOR: return "TS_AST_NODE_DECORATOR";
        case TS_AST_NODE_PARAMETER: return "TS_AST_NODE_PARAMETER";
        default: return "JS_AST_NODE_UNKNOWN";
    }
}

static const char* js_dump_operator_name(JsOperator op) {
    switch (op) {
        case JS_OP_ADD: return "add";
        case JS_OP_SUB: return "sub";
        case JS_OP_MUL: return "mul";
        case JS_OP_DIV: return "div";
        case JS_OP_MOD: return "mod";
        case JS_OP_EXP: return "exp";
        case JS_OP_EQ: return "eq";
        case JS_OP_NE: return "ne";
        case JS_OP_STRICT_EQ: return "strict_eq";
        case JS_OP_STRICT_NE: return "strict_ne";
        case JS_OP_LT: return "lt";
        case JS_OP_LE: return "le";
        case JS_OP_GT: return "gt";
        case JS_OP_GE: return "ge";
        case JS_OP_AND: return "and";
        case JS_OP_OR: return "or";
        case JS_OP_BIT_AND: return "bit_and";
        case JS_OP_BIT_OR: return "bit_or";
        case JS_OP_BIT_XOR: return "bit_xor";
        case JS_OP_BIT_LSHIFT: return "bit_lshift";
        case JS_OP_BIT_RSHIFT: return "bit_rshift";
        case JS_OP_BIT_URSHIFT: return "bit_urshift";
        case JS_OP_NOT: return "not";
        case JS_OP_BIT_NOT: return "bit_not";
        case JS_OP_TYPEOF: return "typeof";
        case JS_OP_VOID: return "void";
        case JS_OP_DELETE: return "delete";
        case JS_OP_PLUS: return "plus";
        case JS_OP_MINUS: return "minus";
        case JS_OP_INCREMENT: return "increment";
        case JS_OP_DECREMENT: return "decrement";
        case JS_OP_ASSIGN: return "assign";
        case JS_OP_ADD_ASSIGN: return "add_assign";
        case JS_OP_SUB_ASSIGN: return "sub_assign";
        case JS_OP_MUL_ASSIGN: return "mul_assign";
        case JS_OP_DIV_ASSIGN: return "div_assign";
        case JS_OP_MOD_ASSIGN: return "mod_assign";
        case JS_OP_EXP_ASSIGN: return "exp_assign";
        case JS_OP_INSTANCEOF: return "instanceof";
        case JS_OP_IN: return "in";
        case JS_OP_NULLISH_COALESCE: return "nullish_coalesce";
        case JS_OP_NULLISH_ASSIGN: return "nullish_assign";
        case JS_OP_AND_ASSIGN: return "and_assign";
        case JS_OP_OR_ASSIGN: return "or_assign";
        default: return "unknown";
    }
}

static void dump_string_field(const char* label, String* str) {
    if (!str) return;
    printf(" (%s ", label);
    dump_escaped_string(str->chars, (int)str->len);
    printf(")");
}

static void dump_source_field(const char* source, TSNode node) {
    int len = 0;
    const char* src = dump_node_src(source, node, &len);
    if (len <= 0) return;
    printf(" (source ");
    dump_escaped_string(src, len);
    printf(")");
}

static void emit_js_dump_node(const char* source, JsAstNode* node, int indent);

static void emit_js_dump_list(const char* source, const char* label, JsAstNode* node, int indent) {
    printf("\n");
    dump_indent(indent);
    printf("(%s", label);
    while (node) {
        printf("\n");
        emit_js_dump_node(source, node, indent + 1);
        node = node->next;
    }
    printf(")");
}

static void emit_js_dump_field(const char* source, const char* label, JsAstNode* node, int indent) {
    if (!node) return;
    printf("\n");
    dump_indent(indent);
    printf("(%s\n", label);
    emit_js_dump_node(source, node, indent + 1);
    printf(")");
}

static void emit_js_dump_node(const char* source, JsAstNode* node, int indent) {
    dump_indent(indent);
    if (!node) {
        printf("(null)");
        return;
    }
    printf("(%s", js_dump_kind_name(node->node_type));

    switch (node->node_type) {
        case JS_AST_NODE_PROGRAM:
            emit_js_dump_list(source, "body", ((JsProgramNode*)node)->body, indent + 1);
            break;
        case JS_AST_NODE_VARIABLE_DECLARATION: {
            JsVariableDeclarationNode* var_decl = (JsVariableDeclarationNode*)node;
            printf(" (kind %s)", var_decl->kind == JS_VAR_VAR ? "var" : var_decl->kind == JS_VAR_LET ? "let" : "const");
            emit_js_dump_list(source, "declarations", var_decl->declarations, indent + 1);
            break;
        }
        case JS_AST_NODE_VARIABLE_DECLARATOR: {
            JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)node;
            emit_js_dump_field(source, "id", decl->id, indent + 1);
            emit_js_dump_field(source, "init", decl->init, indent + 1);
            emit_js_dump_field(source, "ts_type", (JsAstNode*)decl->ts_type, indent + 1);
            break;
        }
        case JS_AST_NODE_IDENTIFIER:
            dump_string_field("name", ((JsIdentifierNode*)node)->name);
            break;
        case JS_AST_NODE_LITERAL: {
            JsLiteralNode* lit = (JsLiteralNode*)node;
            if (lit->literal_type == JS_LITERAL_STRING) {
                printf(" (literal string)");
                dump_string_field("value", lit->value.string_value);
            } else if (lit->literal_type == JS_LITERAL_BOOLEAN) {
                printf(" (literal %s)", lit->value.boolean_value ? "true" : "false");
            } else if (lit->literal_type == JS_LITERAL_NULL) {
                printf(" (literal null)");
            } else if (lit->literal_type == JS_LITERAL_UNDEFINED) {
                printf(" (literal undefined)");
            } else {
                printf(" (literal number)");
                dump_source_field(source, node->node);
            }
            if (lit->is_bigint) dump_string_field("bigint", lit->bigint_str);
            break;
        }
        case JS_AST_NODE_BINARY_EXPRESSION: {
            JsBinaryNode* bin = (JsBinaryNode*)node;
            printf(" (op %s)", js_dump_operator_name(bin->op));
            emit_js_dump_field(source, "left", bin->left, indent + 1);
            emit_js_dump_field(source, "right", bin->right, indent + 1);
            break;
        }
        case JS_AST_NODE_UNARY_EXPRESSION: {
            JsUnaryNode* un = (JsUnaryNode*)node;
            printf(" (op %s)", js_dump_operator_name(un->op));
            emit_js_dump_field(source, "operand", un->operand, indent + 1);
            break;
        }
        case JS_AST_NODE_ASSIGNMENT_EXPRESSION:
        case JS_AST_NODE_ASSIGNMENT_PATTERN: {
            JsAssignmentNode* assign = (JsAssignmentNode*)node;
            printf(" (op %s)", js_dump_operator_name(assign->op));
            emit_js_dump_field(source, "left", assign->left, indent + 1);
            emit_js_dump_field(source, "right", assign->right, indent + 1);
            break;
        }
        case JS_AST_NODE_FUNCTION_DECLARATION:
        case JS_AST_NODE_FUNCTION_EXPRESSION:
        case JS_AST_NODE_ARROW_FUNCTION: {
            JsFunctionNode* fn = (JsFunctionNode*)node;
            dump_string_field("name", fn->name);
            emit_js_dump_list(source, "params", fn->params, indent + 1);
            emit_js_dump_field(source, "return_type", (JsAstNode*)fn->ts_return_type, indent + 1);
            emit_js_dump_field(source, "body", fn->body, indent + 1);
            break;
        }
        case JS_AST_NODE_CALL_EXPRESSION:
        case JS_AST_NODE_NEW_EXPRESSION: {
            JsCallNode* call = (JsCallNode*)node;
            emit_js_dump_field(source, "callee", call->callee, indent + 1);
            emit_js_dump_list(source, "arguments", call->arguments, indent + 1);
            break;
        }
        case JS_AST_NODE_MEMBER_EXPRESSION: {
            JsMemberNode* mem = (JsMemberNode*)node;
            emit_js_dump_field(source, "object", mem->object, indent + 1);
            emit_js_dump_field(source, "property", mem->property, indent + 1);
            break;
        }
        case JS_AST_NODE_ARRAY_EXPRESSION:
        case JS_AST_NODE_ARRAY_PATTERN:
            emit_js_dump_list(source, "elements", ((JsArrayNode*)node)->elements, indent + 1);
            break;
        case JS_AST_NODE_OBJECT_EXPRESSION:
        case JS_AST_NODE_OBJECT_PATTERN:
            emit_js_dump_list(source, "properties", ((JsObjectNode*)node)->properties, indent + 1);
            break;
        case JS_AST_NODE_PROPERTY: {
            JsPropertyNode* prop = (JsPropertyNode*)node;
            emit_js_dump_field(source, "key", prop->key, indent + 1);
            emit_js_dump_field(source, "value", prop->value, indent + 1);
            break;
        }
        case JS_AST_NODE_EXPRESSION_STATEMENT:
            emit_js_dump_field(source, "expression", ((JsExpressionStatementNode*)node)->expression, indent + 1);
            break;
        case JS_AST_NODE_BLOCK_STATEMENT:
            emit_js_dump_list(source, "statements", ((JsBlockNode*)node)->statements, indent + 1);
            break;
        case JS_AST_NODE_IF_STATEMENT: {
            JsIfNode* if_node = (JsIfNode*)node;
            emit_js_dump_field(source, "test", if_node->test, indent + 1);
            emit_js_dump_field(source, "consequent", if_node->consequent, indent + 1);
            emit_js_dump_field(source, "alternate", if_node->alternate, indent + 1);
            break;
        }
        case JS_AST_NODE_WHILE_STATEMENT: {
            JsWhileNode* wh = (JsWhileNode*)node;
            emit_js_dump_field(source, "test", wh->test, indent + 1);
            emit_js_dump_field(source, "body", wh->body, indent + 1);
            break;
        }
        case JS_AST_NODE_FOR_STATEMENT: {
            JsForNode* for_node = (JsForNode*)node;
            emit_js_dump_field(source, "init", for_node->init, indent + 1);
            emit_js_dump_field(source, "test", for_node->test, indent + 1);
            emit_js_dump_field(source, "update", for_node->update, indent + 1);
            emit_js_dump_field(source, "body", for_node->body, indent + 1);
            break;
        }
        case JS_AST_NODE_RETURN_STATEMENT:
            emit_js_dump_field(source, "argument", ((JsReturnNode*)node)->argument, indent + 1);
            break;
        case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
            JsConditionalNode* cond = (JsConditionalNode*)node;
            emit_js_dump_field(source, "test", cond->test, indent + 1);
            emit_js_dump_field(source, "consequent", cond->consequent, indent + 1);
            emit_js_dump_field(source, "alternate", cond->alternate, indent + 1);
            break;
        }
        case JS_AST_NODE_SPREAD_ELEMENT:
        case JS_AST_NODE_REST_ELEMENT:
        case JS_AST_NODE_REST_PROPERTY:
            emit_js_dump_field(source, "argument", ((JsSpreadElementNode*)node)->argument, indent + 1);
            break;
        case JS_AST_NODE_CLASS_DECLARATION:
        case JS_AST_NODE_CLASS_EXPRESSION: {
            JsClassNode* cls = (JsClassNode*)node;
            dump_string_field("name", cls->name);
            emit_js_dump_field(source, "superclass", cls->superclass, indent + 1);
            emit_js_dump_field(source, "body", cls->body, indent + 1);
            break;
        }
        case JS_AST_NODE_METHOD_DEFINITION: {
            JsMethodDefinitionNode* method = (JsMethodDefinitionNode*)node;
            dump_string_field("name", method->name);
            emit_js_dump_field(source, "key", method->key, indent + 1);
            emit_js_dump_list(source, "params", method->params, indent + 1);
            emit_js_dump_field(source, "return_type", (JsAstNode*)method->ts_return_type, indent + 1);
            emit_js_dump_field(source, "body", method->body, indent + 1);
            break;
        }
        case JS_AST_NODE_FIELD_DEFINITION: {
            JsFieldDefinitionNode* field = (JsFieldDefinitionNode*)node;
            emit_js_dump_field(source, "key", field->key, indent + 1);
            emit_js_dump_field(source, "value", field->value, indent + 1);
            break;
        }
        case JS_AST_NODE_STATIC_BLOCK:
            emit_js_dump_field(source, "body", ((JsStaticBlockNode*)node)->body, indent + 1);
            break;
        case JS_AST_NODE_TRY_STATEMENT: {
            JsTryNode* tr = (JsTryNode*)node;
            emit_js_dump_field(source, "block", tr->block, indent + 1);
            emit_js_dump_field(source, "handler", tr->handler, indent + 1);
            emit_js_dump_field(source, "finalizer", tr->finalizer, indent + 1);
            break;
        }
        case JS_AST_NODE_CATCH_CLAUSE: {
            JsCatchNode* catch_node = (JsCatchNode*)node;
            emit_js_dump_field(source, "param", catch_node->param, indent + 1);
            emit_js_dump_field(source, "body", catch_node->body, indent + 1);
            break;
        }
        case JS_AST_NODE_THROW_STATEMENT:
            emit_js_dump_field(source, "argument", ((JsThrowNode*)node)->argument, indent + 1);
            break;
        case JS_AST_NODE_SEQUENCE_EXPRESSION:
            emit_js_dump_list(source, "expressions", ((JsSequenceNode*)node)->expressions, indent + 1);
            break;
        case JS_AST_NODE_YIELD_EXPRESSION:
            emit_js_dump_field(source, "argument", ((JsYieldNode*)node)->argument, indent + 1);
            break;
        case JS_AST_NODE_AWAIT_EXPRESSION:
            emit_js_dump_field(source, "argument", ((JsAwaitNode*)node)->argument, indent + 1);
            break;
        case JS_AST_NODE_IMPORT_DECLARATION: {
            JsImportNode* imp = (JsImportNode*)node;
            dump_string_field("source", imp->source);
            dump_string_field("default", imp->default_name);
            dump_string_field("namespace", imp->namespace_name);
            emit_js_dump_list(source, "specifiers", imp->specifiers, indent + 1);
            break;
        }
        case JS_AST_NODE_IMPORT_SPECIFIER: {
            JsImportSpecifierNode* spec = (JsImportSpecifierNode*)node;
            dump_string_field("local", spec->local_name);
            dump_string_field("remote", spec->remote_name);
            break;
        }
        case JS_AST_NODE_EXPORT_DECLARATION: {
            JsExportNode* exp = (JsExportNode*)node;
            dump_string_field("source", exp->source);
            emit_js_dump_field(source, "declaration", exp->declaration, indent + 1);
            emit_js_dump_list(source, "specifiers", exp->specifiers, indent + 1);
            break;
        }
        case JS_AST_NODE_EXPORT_SPECIFIER: {
            JsExportSpecifierNode* spec = (JsExportSpecifierNode*)node;
            dump_string_field("local", spec->local_name);
            dump_string_field("export", spec->export_name);
            break;
        }
        default:
            dump_source_field(source, node->node);
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
    if (!js_transpiler_parse(tp, source, length)) {
        fprintf(stderr, "Error: Failed to parse '%s'\n", script_path);
        js_transpiler_destroy(tp);
        mem_free(source);
        return 1;
    }

    TSNode root = ts_tree_root_node(tp->tree);
    JsAstNode* ast = build_js_ast(tp, root);
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
