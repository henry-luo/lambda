#include "js_transpiler.hpp"
#include "../ts/ts_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include <cstring>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <cstdlib>

// forward declarations
static char js_decode_escape_char(char c);

// External Tree-sitter TypeScript parser (unified: handles both JS and TS)
extern "C" {
    const TSLanguage *tree_sitter_typescript(void);
}

// Temporary stubs for missing Tree-sitter symbols until library is integrated
#ifndef sym_number
#define sym_number 1
#define sym_undefined 2
#define sym_subscript_expression 3
#define sym_arrow_function 4
#define sym_statement_block 5
#define sym_variable_declarator 6
#define sym_binary_expression 7
#define sym_unary_expression 8
#define sym_call_expression 9
#define sym_member_expression 10
#define sym_object 11
#define sym_function_expression 12
#define sym_ternary_expression 13
#define sym_template_string 14
#define sym_variable_declaration 15
#define sym_lexical_declaration 16
#define sym_function_declaration 17
#define sym_identifier 18
#define sym_if_statement 19
#define sym_while_statement 20
#define sym_for_statement 21
#define sym_return_statement 22
#define sym_break_statement 23
#define sym_continue_statement 24
#define sym_try_statement 25
#define sym_throw_statement 26
#define sym_class_declaration 27
#define sym_expression_statement 28
#define sym_template_chars 29
#define sym_program 30
#define field_arguments 33
#define field_property 34
#define field_value 35
#define field_parameters 36
#define field_condition 37
#define field_consequence 38
#define field_alternative 39
#define field_key 40
#endif

// Utility function to get Tree-sitter node source
#define js_node_source(transpiler, node) {.str = (transpiler)->source + ts_node_start_byte(node), \
     .length = ts_node_end_byte(node) - ts_node_start_byte(node) }

// Forward declarations
JsAstNode* build_js_block_statement(JsTranspiler* tp, TSNode block_node);
JsAstNode* build_js_expression(JsTranspiler* tp, TSNode expr_node);
JsAstNode* build_js_statement(JsTranspiler* tp, TSNode stmt_node);
JsAstNode* build_js_template_literal(JsTranspiler* tp, TSNode template_node);
JsAstNode* build_js_try_statement(JsTranspiler* tp, TSNode try_node);
JsAstNode* build_js_throw_statement(JsTranspiler* tp, TSNode throw_node);
JsAstNode* build_js_class_declaration(JsTranspiler* tp, TSNode class_node);
JsAstNode* build_js_class_body(JsTranspiler* tp, TSNode body_node);
JsAstNode* build_js_method_definition(JsTranspiler* tp, TSNode method_node);
JsAstNode* build_js_new_expression(JsTranspiler* tp, TSNode new_node);
JsAstNode* build_js_switch_statement(JsTranspiler* tp, TSNode switch_node);
JsAstNode* build_js_do_while_statement(JsTranspiler* tp, TSNode do_node);
JsAstNode* build_js_for_in_statement(JsTranspiler* tp, TSNode for_node);
JsAstNode* build_js_import_statement(JsTranspiler* tp, TSNode import_node);
JsAstNode* build_js_export_statement(JsTranspiler* tp, TSNode export_node);

// TS-specific builders (used when !tp->strict_js — unified JS/TS AST builder)
static const char* ts_node_text_util(JsTranspiler* tp, TSNode node, int* out_len);
static String* ts_pool_string_util(JsTranspiler* tp, const char* src, int len);
static TsTypeNode* build_ts_type_expr_u(JsTranspiler* tp, TSNode node);
static TsTypeNode* build_ts_type_annotation_u(JsTranspiler* tp, TSNode node);
static JsAstNode* build_ts_interface_decl_u(JsTranspiler* tp, TSNode node);
static JsAstNode* build_ts_type_alias_decl_u(JsTranspiler* tp, TSNode node);
static JsAstNode* build_ts_enum_decl_u(JsTranspiler* tp, TSNode node);
static JsAstNode* build_ts_namespace_decl_u(JsTranspiler* tp, TSNode node);
static JsAstNode* build_ts_decorator_u(JsTranspiler* tp, TSNode node);
static JsAstNode* build_ts_function_u(JsTranspiler* tp, TSNode func_node);
static JsAstNode* build_ts_parameter_u(JsTranspiler* tp, TSNode param_node, bool is_optional);
static JsAstNode* build_ts_class_decl_u(JsTranspiler* tp, TSNode class_node);
static JsAstNode* build_ts_class_body_u(JsTranspiler* tp, TSNode body_node);
static JsAstNode* build_ts_variable_decl_u(JsTranspiler* tp, TSNode var_node);

// Allocate JavaScript AST node
JsAstNode* alloc_js_ast_node(JsTranspiler* tp, JsAstNodeType node_type, TSNode node, size_t size) {
    JsAstNode* ast_node = (JsAstNode*)pool_alloc(tp->ast_pool, size);
    memset(ast_node, 0, size);
    ast_node->node_type = node_type;
    ast_node->node = node;
    return ast_node;
}

// Convert Tree-sitter operator string to JsOperator enum
JsOperator js_operator_from_string(const char* op_str, size_t len) {
    if (len == 1) {
        switch (op_str[0]) {
            case '+': return JS_OP_ADD;
            case '-': return JS_OP_SUB;
            case '*': return JS_OP_MUL;
            case '/': return JS_OP_DIV;
            case '%': return JS_OP_MOD;
            case '<': return JS_OP_LT;
            case '>': return JS_OP_GT;
            case '!': return JS_OP_NOT;
            case '~': return JS_OP_BIT_NOT;
            case '&': return JS_OP_BIT_AND;
            case '|': return JS_OP_BIT_OR;
            case '^': return JS_OP_BIT_XOR;
            case '=': return JS_OP_ASSIGN;
        }
    } else if (len == 2) {
        if (strncmp(op_str, "==", 2) == 0) return JS_OP_EQ;
        if (strncmp(op_str, "!=", 2) == 0) return JS_OP_NE;
        if (strncmp(op_str, "<=", 2) == 0) return JS_OP_LE;
        if (strncmp(op_str, ">=", 2) == 0) return JS_OP_GE;
        if (strncmp(op_str, "&&", 2) == 0) return JS_OP_AND;
        if (strncmp(op_str, "||", 2) == 0) return JS_OP_OR;
        if (strncmp(op_str, "<<", 2) == 0) return JS_OP_BIT_LSHIFT;
        if (strncmp(op_str, ">>", 2) == 0) return JS_OP_BIT_RSHIFT;
        if (strncmp(op_str, "**", 2) == 0) return JS_OP_EXP;
        if (strncmp(op_str, "++", 2) == 0) return JS_OP_INCREMENT;
        if (strncmp(op_str, "--", 2) == 0) return JS_OP_DECREMENT;
        if (strncmp(op_str, "+=", 2) == 0) return JS_OP_ADD_ASSIGN;
        if (strncmp(op_str, "-=", 2) == 0) return JS_OP_SUB_ASSIGN;
        if (strncmp(op_str, "*=", 2) == 0) return JS_OP_MUL_ASSIGN;
        if (strncmp(op_str, "/=", 2) == 0) return JS_OP_DIV_ASSIGN;
        if (strncmp(op_str, "%=", 2) == 0) return JS_OP_MOD_ASSIGN;
        if (strncmp(op_str, "&=", 2) == 0) return JS_OP_BIT_AND_ASSIGN;
        if (strncmp(op_str, "|=", 2) == 0) return JS_OP_BIT_OR_ASSIGN;
        if (strncmp(op_str, "^=", 2) == 0) return JS_OP_BIT_XOR_ASSIGN;
        if (strncmp(op_str, "??", 2) == 0) return JS_OP_NULLISH_COALESCE;
        if (strncmp(op_str, "in", 2) == 0) return JS_OP_IN;
    } else if (len == 3) {
        if (strncmp(op_str, "===", 3) == 0) return JS_OP_STRICT_EQ;
        if (strncmp(op_str, "!==", 3) == 0) return JS_OP_STRICT_NE;
        if (strncmp(op_str, ">>>", 3) == 0) return JS_OP_BIT_URSHIFT;
        if (strncmp(op_str, "**=", 3) == 0) return JS_OP_EXP_ASSIGN;
        if (strncmp(op_str, "<<=", 3) == 0) return JS_OP_LSHIFT_ASSIGN;
        if (strncmp(op_str, ">>=", 3) == 0) return JS_OP_RSHIFT_ASSIGN;
        if (strncmp(op_str, "??=", 3) == 0) return JS_OP_NULLISH_ASSIGN;
        if (strncmp(op_str, "&&=", 3) == 0) return JS_OP_AND_ASSIGN;
        if (strncmp(op_str, "||=", 3) == 0) return JS_OP_OR_ASSIGN;
    } else if (len == 4) {
        if (strncmp(op_str, "void", 4) == 0) return JS_OP_VOID;
        if (strncmp(op_str, ">>>=", 4) == 0) return JS_OP_URSHIFT_ASSIGN;
    } else if (len == 6) {
        if (strncmp(op_str, "typeof", 6) == 0) return JS_OP_TYPEOF;
        if (strncmp(op_str, "delete", 6) == 0) return JS_OP_DELETE;
    } else if (len == 10) {
        if (strncmp(op_str, "instanceof", 10) == 0) return JS_OP_INSTANCEOF;
    }

    log_error("Unknown JavaScript operator: %.*s", (int)len, op_str);
    return JS_OP_ADD; // Default fallback
}

// Build JavaScript literal node
JsAstNode* build_js_literal(JsTranspiler* tp, TSNode literal_node) {
    const char* node_type = ts_node_type(literal_node);
    JsLiteralNode* literal = (JsLiteralNode*)alloc_js_ast_node(tp, JS_AST_NODE_LITERAL, literal_node, sizeof(JsLiteralNode));

    StrView source = js_node_source(tp, literal_node);

    if (strcmp(node_type, "number") == 0) {
        literal->literal_type = JS_LITERAL_NUMBER;
        // Check if source text contains '.' or 'e'/'E' (fractional/scientific hint)
        literal->has_decimal = false;
        for (size_t i = 0; i < source.length; i++) {
            if (source.str[i] == '.' || source.str[i] == 'e' || source.str[i] == 'E') {
                literal->has_decimal = true;
                break;
            }
        }
        // Create null-terminated string, stripping numeric separators (_)
        char* temp_str = (char*)malloc(source.length + 1);
        if (temp_str) {
            size_t j = 0;
            for (size_t i = 0; i < source.length; i++) {
                if (source.str[i] != '_') temp_str[j++] = source.str[i];
            }
            temp_str[j] = '\0';
            char* endptr;
            // strtod handles decimal and 0x hex, but not 0b binary or 0o octal
            if (j > 2 && temp_str[0] == '0' && (temp_str[1] == 'b' || temp_str[1] == 'B')) {
                literal->value.number_value = (double)strtoull(temp_str + 2, &endptr, 2);
            } else if (j > 2 && temp_str[0] == '0' && (temp_str[1] == 'o' || temp_str[1] == 'O')) {
                literal->value.number_value = (double)strtoull(temp_str + 2, &endptr, 8);
            } else {
                literal->value.number_value = strtod(temp_str, &endptr);
            }
            free(temp_str);
        } else {
            literal->value.number_value = 0.0;
        }
        literal->base.type = &TYPE_FLOAT; // All JS numbers are float64
    } else if (strcmp(node_type, "string") == 0) {
        literal->literal_type = JS_LITERAL_STRING;
        // Remove quotes and handle escape sequences
        if (source.length >= 2) {
            size_t content_len = source.length - 2;
            const char* src = source.str + 1;
            // Process escape sequences in-place
            char* temp_str = (char*)malloc(content_len + 1);
            if (temp_str) {
                size_t out = 0;
                for (size_t i = 0; i < content_len; i++) {
                    if (src[i] == '\\' && i + 1 < content_len) {
                        char next = src[i + 1];
                        if (next == 'u') {
                            // Unicode escape: \uXXXX or \u{XXXXX}
                            if (i + 2 < content_len && src[i + 2] == '{') {
                                // \u{XXXXX} — braced Unicode code point
                                size_t hex_start = i + 3;
                                size_t hex_end = hex_start;
                                while (hex_end < content_len && src[hex_end] != '}') hex_end++;
                                size_t hex_len = hex_end - hex_start;
                                if (hex_len > 0 && hex_len <= 6) {
                                    char hex[7] = {0};
                                    memcpy(hex, src + hex_start, hex_len);
                                    uint32_t cp = (uint32_t)strtoul(hex, NULL, 16);
                                    if (cp < 0x80) {
                                        temp_str[out++] = (char)cp;
                                    } else if (cp < 0x800) {
                                        temp_str[out++] = (char)(0xC0 | (cp >> 6));
                                        temp_str[out++] = (char)(0x80 | (cp & 0x3F));
                                    } else if (cp <= 0xFFFF) {
                                        temp_str[out++] = (char)(0xE0 | (cp >> 12));
                                        temp_str[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                        temp_str[out++] = (char)(0x80 | (cp & 0x3F));
                                    } else if (cp <= 0x10FFFF) {
                                        temp_str[out++] = (char)(0xF0 | (cp >> 18));
                                        temp_str[out++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                                        temp_str[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                        temp_str[out++] = (char)(0x80 | (cp & 0x3F));
                                    }
                                    i = hex_end; // skip past closing }
                                } else {
                                    temp_str[out++] = src[i]; // keep as-is
                                }
                            } else if (i + 5 < content_len) {
                                char hex[5] = {src[i+2], src[i+3], src[i+4], src[i+5], 0};
                                uint32_t cp = (uint32_t)strtoul(hex, NULL, 16);
                                if (cp < 0x80) {
                                    temp_str[out++] = (char)cp;
                                } else if (cp < 0x800) {
                                    temp_str[out++] = (char)(0xC0 | (cp >> 6));
                                    temp_str[out++] = (char)(0x80 | (cp & 0x3F));
                                } else {
                                    temp_str[out++] = (char)(0xE0 | (cp >> 12));
                                    temp_str[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                    temp_str[out++] = (char)(0x80 | (cp & 0x3F));
                                }
                                i += 5; // skip \uXXXX
                            } else {
                                temp_str[out++] = src[i]; // keep as-is
                            }
                        } else if (next == 'x') {
                            // Hex escape: \xHH → encode as UTF-8
                            if (i + 3 < content_len) {
                                char hex[3] = {src[i+2], src[i+3], 0};
                                uint32_t cp = (uint32_t)strtoul(hex, NULL, 16);
                                if (cp < 0x80) {
                                    temp_str[out++] = (char)cp;
                                } else {
                                    // 0x80-0xFF: 2-byte UTF-8
                                    temp_str[out++] = (char)(0xC0 | (cp >> 6));
                                    temp_str[out++] = (char)(0x80 | (cp & 0x3F));
                                }
                                i += 3;
                            } else {
                                temp_str[out++] = src[i];
                            }
                        } else {
                            temp_str[out++] = js_decode_escape_char(next);
                            i++; // skip the escaped char
                        }
                    } else {
                        temp_str[out++] = src[i];
                    }
                }
                temp_str[out] = '\0';
                literal->value.string_value = name_pool_create_len(tp->name_pool, temp_str, out);
                free(temp_str);
            } else {
                literal->value.string_value = name_pool_create_len(tp->name_pool, "", 0);
            }
        } else {
            literal->value.string_value = name_pool_create_len(tp->name_pool, "", 0);
        }
        literal->base.type = &TYPE_STRING;
    } else if (strcmp(node_type, "true") == 0) {
        literal->literal_type = JS_LITERAL_BOOLEAN;
        literal->value.boolean_value = true;
        literal->base.type = &TYPE_BOOL;
    } else if (strcmp(node_type, "false") == 0) {
        literal->literal_type = JS_LITERAL_BOOLEAN;
        literal->value.boolean_value = false;
        literal->base.type = &TYPE_BOOL;
    } else if (strcmp(node_type, "null") == 0) {
        literal->literal_type = JS_LITERAL_NULL;
        literal->base.type = &TYPE_NULL;
    } else if (strcmp(node_type, "undefined") == 0) {
        literal->literal_type = JS_LITERAL_UNDEFINED;
        literal->base.type = &TYPE_NULL; // Map undefined to null in Lambda
    }

    return (JsAstNode*)literal;
}

// Build JavaScript identifier node
JsAstNode* build_js_identifier(JsTranspiler* tp, TSNode id_node) {
    if (ts_node_is_null(id_node)) {
        log_error("Cannot build identifier from null node");
        return NULL;
    }

    // Check actual tree-sitter node type for debugging
    const char* actual_type = ts_node_type(id_node);

    JsIdentifierNode* identifier = (JsIdentifierNode*)alloc_js_ast_node(tp, JS_AST_NODE_IDENTIFIER, id_node, sizeof(JsIdentifierNode));

    StrView source = js_node_source(tp, id_node);
    if (source.length == 0) {
        log_error("Empty identifier source");
        return NULL;
    }

    // Create a null-terminated string for the identifier
    char* temp_str = (char*)malloc(source.length + 1);
    if (!temp_str) {
        log_error("Failed to allocate memory for identifier");
        return NULL;
    }
    memcpy(temp_str, source.str, source.length);
    temp_str[source.length] = '\0';

    identifier->name = name_pool_create_len(tp->name_pool, temp_str, source.length);
    free(temp_str);

    if (!identifier->name) {
        log_error("Failed to create identifier name");
        return NULL;
    }

    // Look up in symbol table
    identifier->entry = js_scope_lookup(tp, identifier->name);

    if (identifier->entry) {
        log_debug("id-lookup: '%.*s' found, entry->node=%p, entry->node->type=%p",
            (int)identifier->name->len, identifier->name->chars,
            identifier->entry->node, identifier->entry->node->type);
        identifier->base.type = identifier->entry->node->type;
    } else {
        // Undefined identifier - could be global or error
        identifier->base.type = &TYPE_ANY;
        log_debug("Undefined identifier: %.*s (ts_type=%s)", (int)identifier->name->len, identifier->name->chars, actual_type);
    }

    return (JsAstNode*)identifier;
}

// Build JavaScript binary expression node
JsAstNode* build_js_binary_expression(JsTranspiler* tp, TSNode binary_node) {
    JsBinaryNode* binary = (JsBinaryNode*)alloc_js_ast_node(tp, JS_AST_NODE_BINARY_EXPRESSION, binary_node, sizeof(JsBinaryNode));

    // Use field names to avoid issues with comment nodes shifting child indices
    TSNode left_node = ts_node_child_by_field_name(binary_node, "left", 4);
    TSNode right_node = ts_node_child_by_field_name(binary_node, "right", 5);
    TSNode op_node = ts_node_child_by_field_name(binary_node, "operator", 8);

    binary->left = build_js_expression(tp, left_node);
    binary->right = build_js_expression(tp, right_node);

    if (!ts_node_is_null(op_node)) {
        StrView op_source = js_node_source(tp, op_node);
        binary->op = js_operator_from_string(op_source.str, op_source.length);
    } else {
        binary->op = JS_OP_ADD; // fallback
    }

    binary->base.type = &TYPE_FLOAT;

    return (JsAstNode*)binary;
}

// Build JavaScript unary expression node
JsAstNode* build_js_unary_expression(JsTranspiler* tp, TSNode unary_node) {
    JsUnaryNode* unary = (JsUnaryNode*)alloc_js_ast_node(tp, JS_AST_NODE_UNARY_EXPRESSION, unary_node, sizeof(JsUnaryNode));

    // Get operand
    TSNode operand_node = ts_node_child_by_field_name(unary_node, "argument", strlen("argument"));
    unary->operand = build_js_expression(tp, operand_node);

    // Get operator
    TSNode op_node = ts_node_child_by_field_name(unary_node, "operator", strlen("operator"));
    StrView op_source = js_node_source(tp, op_node);
    unary->op = js_operator_from_string(op_source.str, op_source.length);

    // Determine if prefix or postfix
    unary->prefix = (ts_node_start_byte(op_node) < ts_node_start_byte(operand_node));

    // Infer result type
    switch (unary->op) {
        case JS_OP_NOT:
            unary->base.type = &TYPE_BOOL;
            break;
        case JS_OP_TYPEOF:
            unary->base.type = &TYPE_STRING;
            break;
        case JS_OP_PLUS:
        case JS_OP_MINUS:
        case JS_OP_BIT_NOT:
            unary->base.type = &TYPE_FLOAT;
            break;
        case JS_OP_INCREMENT:
        case JS_OP_DECREMENT:
            unary->base.type = unary->operand->type; // Same as operand
            break;
        case JS_OP_DELETE:
            unary->base.type = &TYPE_BOOL;
            break;
        case JS_OP_VOID:
            unary->base.type = &TYPE_NULL; // void always returns undefined
            break;
        default:
            unary->base.type = &TYPE_ANY;
    }

    return (JsAstNode*)unary;
}

// Build JavaScript call expression node
JsAstNode* build_js_call_expression(JsTranspiler* tp, TSNode call_node) {
    JsCallNode* call = (JsCallNode*)alloc_js_ast_node(tp, JS_AST_NODE_CALL_EXPRESSION, call_node, sizeof(JsCallNode));

    // Get callee (function being called) - use field name instead of ID
    TSNode callee_node = ts_node_child_by_field_name(call_node, "function", strlen("function"));
    if (ts_node_is_null(callee_node)) {
        // Fallback: try getting first child
        callee_node = ts_node_named_child(call_node, 0);
        if (ts_node_is_null(callee_node)) {
            log_error("Call expression has no function node");
            return NULL;
        }
    }

    call->callee = build_js_expression(tp, callee_node);
    if (!call->callee) {
        log_error("Failed to build callee expression");
        return NULL;
    }

    // Get arguments
    TSNode args_node = ts_node_child_by_field_name(call_node, "arguments", strlen("arguments"));
    if (ts_node_is_null(args_node)) {
        // Fallback: look for arguments node by type
        uint32_t child_count = ts_node_child_count(call_node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(call_node, i);
            const char* child_type = ts_node_type(child);
            if (strcmp(child_type, "arguments") == 0) {
                args_node = child;
                break;
            }
        }
    }
    if (!ts_node_is_null(args_node)) {
        uint32_t arg_count = ts_node_named_child_count(args_node);
        JsAstNode* prev_arg = NULL;

        for (uint32_t i = 0; i < arg_count; i++) {
            TSNode arg_node = ts_node_named_child(args_node, i);
            const char* arg_type = ts_node_type(arg_node);
            if (strcmp(arg_type, "comment") == 0) continue;
            JsAstNode* arg = build_js_expression(tp, arg_node);
            if (!arg) continue;

            if (!prev_arg) {
                call->arguments = arg;
            } else {
                prev_arg->next = arg;
            }
            prev_arg = arg;
        }
    }

    // Function calls return ANY type by default
    call->base.type = &TYPE_ANY;

    // Detect optional chaining (obj?.method())
    TSNode opt_chain = ts_node_child_by_field_name(call_node, "optional_chain", strlen("optional_chain"));
    call->optional = !ts_node_is_null(opt_chain);

    return (JsAstNode*)call;
}

// Build JavaScript member expression node
JsAstNode* build_js_member_expression(JsTranspiler* tp, TSNode member_node) {
    JsMemberNode* member = (JsMemberNode*)alloc_js_ast_node(tp, JS_AST_NODE_MEMBER_EXPRESSION, member_node, sizeof(JsMemberNode));

    // Get object
    TSNode object_node = ts_node_child_by_field_name(member_node, "object", strlen("object"));
    member->object = build_js_expression(tp, object_node);

    // Detect optional chaining (obj?.prop or obj?.[prop])
    TSNode opt_chain = ts_node_child_by_field_name(member_node, "optional_chain", strlen("optional_chain"));
    member->optional = !ts_node_is_null(opt_chain);

    // Determine if computed (obj[prop]) or not (obj.prop) using node type string
    const char* node_type = ts_node_type(member_node);
    member->computed = (strcmp(node_type, "subscript_expression") == 0);

    // Get property - field name is "property" for member_expression, "index" for subscript_expression
    TSNode property_node;
    if (member->computed) {
        property_node = ts_node_child_by_field_name(member_node, "index", strlen("index"));
        // Debug: check if we got a valid node
        if (ts_node_is_null(property_node)) {
            log_error("subscript_expression: 'index' field is null, trying child iteration");
            // Fall back: iterate children to find the index
            uint32_t child_count = ts_node_child_count(member_node);
            for (uint32_t i = 0; i < child_count; i++) {
                TSNode child = ts_node_child(member_node, i);
                const char* child_type = ts_node_type(child);
                log_debug("  subscript child %d: %s", i, child_type);
                // Look for the index (non-bracket child after the first '[')
                if (strcmp(child_type, "[") == 0 && i + 1 < child_count) {
                    property_node = ts_node_child(member_node, i + 1);
                    break;
                }
            }
        }
    } else {
        property_node = ts_node_child_by_field_name(member_node, "property", strlen("property"));
    }

    if (ts_node_is_null(property_node)) {
        log_error("build_js_member_expression: property node is null for %s", node_type);
        return NULL;
    }
    member->property = build_js_expression(tp, property_node);

    // Property access returns ANY type by default
    member->base.type = &TYPE_ANY;

    return (JsAstNode*)member;
}

// Build JavaScript array expression node
JsAstNode* build_js_array_expression(JsTranspiler* tp, TSNode array_node) {
    JsArrayNode* array = (JsArrayNode*)alloc_js_ast_node(tp, JS_AST_NODE_ARRAY_EXPRESSION, array_node, sizeof(JsArrayNode));

    // v18: Use all children (including unnamed commas) to correctly handle elisions [1,,3]
    uint32_t total_children = ts_node_child_count(array_node);

    JsAstNode* prev_element = NULL;
    uint32_t actual_count = 0;
    bool expect_elem = true; // true = next non-comma token should be an element
    for (uint32_t i = 0; i < total_children; i++) {
        TSNode child = ts_node_child(array_node, i);
        const char* child_type = ts_node_type(child);
        if (strcmp(child_type, "[") == 0 || strcmp(child_type, "]") == 0) continue;
        if (strcmp(child_type, ",") == 0) {
            if (expect_elem) {
                // consecutive comma or leading comma = elision: insert undefined hole
                JsAstNode* elision = (JsAstNode*)alloc_js_ast_node(
                    tp, JS_AST_NODE_NULL, child, sizeof(JsAstNode));
                elision->type = &TYPE_ANY;
                if (!prev_element) array->elements = elision;
                else prev_element->next = elision;
                prev_element = elision;
                actual_count++;
            } else {
                expect_elem = true;
            }
            continue;
        }
        // skip comment nodes inside array literals
        if (strcmp(child_type, "comment") == 0) continue;
        expect_elem = false;
        JsAstNode* element = build_js_expression(tp, child);
        if (!element) continue;

        if (!prev_element) {
            array->elements = element;
        } else {
            prev_element->next = element;
        }
        prev_element = element;
        actual_count++;
    }
    array->length = actual_count;

    array->base.type = &TYPE_ARRAY;

    return (JsAstNode*)array;
}

// Build JavaScript object expression node
JsAstNode* build_js_object_expression(JsTranspiler* tp, TSNode object_node) {
    JsObjectNode* object = (JsObjectNode*)alloc_js_ast_node(tp, JS_AST_NODE_OBJECT_EXPRESSION, object_node, sizeof(JsObjectNode));

    uint32_t property_count = ts_node_named_child_count(object_node);

    JsAstNode* prev_property = NULL;
    for (uint32_t i = 0; i < property_count; i++) {
        TSNode property_node = ts_node_named_child(object_node, i);

        // Skip comment nodes inside object literals
        const char* child_type = ts_node_type(property_node);
        if (strcmp(child_type, "comment") == 0) continue;

        // Handle spread element: { ...expr } in object literal
        if (strcmp(child_type, "spread_element") == 0) {
            JsSpreadElementNode* spread = (JsSpreadElementNode*)alloc_js_ast_node(
                tp, JS_AST_NODE_SPREAD_ELEMENT, property_node, sizeof(JsSpreadElementNode));
            TSNode inner = ts_node_named_child(property_node, 0);
            if (!ts_node_is_null(inner)) {
                spread->argument = build_js_expression(tp, inner);
            }
            spread->base.type = &TYPE_ANY;
            if (!prev_property) {
                object->properties = (JsAstNode*)spread;
            } else {
                prev_property->next = (JsAstNode*)spread;
            }
            prev_property = (JsAstNode*)spread;
            continue;
        }

        // Handle shorthand_property_identifier: { Vector } -> { Vector: Vector }
        if (strcmp(child_type, "shorthand_property_identifier") == 0) {
            JsPropertyNode* property = (JsPropertyNode*)alloc_js_ast_node(tp, JS_AST_NODE_PROPERTY, property_node, sizeof(JsPropertyNode));
            // The node itself is an identifier — use it for both key and value
            JsAstNode* ident = build_js_expression(tp, property_node);
            property->key = ident;
            // Create a separate identifier node for value (same name)
            property->value = build_js_expression(tp, property_node);
            property->base.type = &TYPE_ANY;
            if (!prev_property) {
                object->properties = (JsAstNode*)property;
            } else {
                prev_property->next = (JsAstNode*)property;
            }
            prev_property = (JsAstNode*)property;
            continue;
        }

        // Build property node
        JsPropertyNode* property = (JsPropertyNode*)alloc_js_ast_node(tp, JS_AST_NODE_PROPERTY, property_node, sizeof(JsPropertyNode));

        // Handle method_definition nodes (methods, getters, setters in object literals)
        if (strcmp(child_type, "method_definition") == 0) {
            // Check if this is a getter: first child is "get" keyword
            bool is_getter = false;
            uint32_t mchild_count = ts_node_child_count(property_node);
            for (uint32_t mi = 0; mi < mchild_count; mi++) {
                TSNode mc = ts_node_child(property_node, mi);
                const char* mc_type = ts_node_type(mc);
                if (strcmp(mc_type, "get") == 0) { is_getter = true; break; }
            }

            TSNode name_node = ts_node_child_by_field_name(property_node, "name", strlen("name"));
            TSNode body_node = ts_node_child_by_field_name(property_node, "body", strlen("body"));

            if (is_getter && !ts_node_is_null(name_node) && !ts_node_is_null(body_node)) {
                // Getter: store as __get_<name> key with a function expression value
                StrView getter_name = js_node_source(tp, name_node);
                char get_key[256];
                snprintf(get_key, sizeof(get_key), "__get_%.*s", (int)getter_name.length, getter_name.str);
                JsIdentifierNode* key_id = (JsIdentifierNode*)alloc_js_ast_node(tp, JS_AST_NODE_IDENTIFIER, name_node, sizeof(JsIdentifierNode));
                key_id->name = name_pool_create_len(tp->name_pool, get_key, strlen(get_key));
                property->key = (JsAstNode*)key_id;

                // Build as function expression (no params, body from getter body)
                JsFunctionNode* func = (JsFunctionNode*)alloc_js_ast_node(tp, JS_AST_NODE_FUNCTION_EXPRESSION, property_node, sizeof(JsFunctionNode));
                func->name = NULL;
                func->params = NULL;
                func->is_arrow = false;
                func->is_async = false;
                func->is_generator = false;
                const char* body_type = ts_node_type(body_node);
                if (strcmp(body_type, "statement_block") == 0) {
                    func->body = build_js_block_statement(tp, body_node);
                } else {
                    func->body = build_js_expression(tp, body_node);
                }
                property->value = (JsAstNode*)func;
            } else if (!ts_node_is_null(name_node)) {
                const char* name_type = ts_node_type(name_node);
                if (strcmp(name_type, "computed_property_name") == 0) {
                    // Computed method: { [$sym](args) { } } — key is the expression inside [...]
                    property->computed = true;
                    property->key = build_js_expression(tp, name_node); // unwraps computed_property_name
                } else {
                    // Regular method: method_definition without get/set prefix
                    StrView method_name = js_node_source(tp, name_node);
                    JsIdentifierNode* key_id = (JsIdentifierNode*)alloc_js_ast_node(tp, JS_AST_NODE_IDENTIFIER, name_node, sizeof(JsIdentifierNode));
                    key_id->name = name_pool_create_strview(tp->name_pool, method_name);
                    property->key = (JsAstNode*)key_id;
                }
                // Build the full method as a function expression
                property->value = build_js_function(tp, property_node);
            }

            property->base.type = &TYPE_ANY;
            if (!prev_property) {
                object->properties = (JsAstNode*)property;
            } else {
                prev_property->next = (JsAstNode*)property;
            }
            prev_property = (JsAstNode*)property;
            continue;
        }

        // Get key and value
        TSNode key_node = ts_node_child_by_field_name(property_node, "key", strlen("key"));
        TSNode value_node = ts_node_child_by_field_name(property_node, "value", strlen("value"));

        if (!ts_node_is_null(key_node)) {
            property->key = build_js_expression(tp, key_node);
        }
        if (!ts_node_is_null(value_node)) {
            property->value = build_js_expression(tp, value_node);
        } else {
            // Shorthand property: { key } is equivalent to { key: key }
            property->value = property->key;
        }
        property->base.type = &TYPE_ANY;

        if (!prev_property) {
            object->properties = (JsAstNode*)property;
        } else {
            prev_property->next = (JsAstNode*)property;
        }
        prev_property = (JsAstNode*)property;
    }

    object->base.type = &TYPE_MAP; // Objects are maps in Lambda

    return (JsAstNode*)object;
}

// Build JavaScript function node
JsAstNode* build_js_function(JsTranspiler* tp, TSNode func_node) {
    // In TS mode, use TS function builder (allocates TsFunctionNode, handles return_type/type_params)
    if (!tp->strict_js) {
        return build_ts_function_u(tp, func_node);
    }

    const char* node_type = ts_node_type(func_node);
    bool is_arrow = (strcmp(node_type, "arrow_function") == 0);
    bool is_generator = (strcmp(node_type, "generator_function") == 0 ||
                         strcmp(node_type, "generator_function_declaration") == 0);
    // For method_definition, check for "*" child indicating a generator method
    if (strcmp(node_type, "method_definition") == 0 && !is_generator) {
        uint32_t ccount = ts_node_child_count(func_node);
        for (uint32_t ci = 0; ci < ccount; ci++) {
            TSNode child = ts_node_child(func_node, ci);
            const char* ctype = ts_node_type(child);
            if (strcmp(ctype, "*") == 0) { is_generator = true; break; }
            // stop once we hit the parameters or body
            if (strcmp(ctype, "formal_parameters") == 0 || strcmp(ctype, "statement_block") == 0) break;
        }
    }

    bool is_expression = is_arrow || (strcmp(node_type, "function_expression") == 0) ||
                         strcmp(node_type, "generator_function") == 0;

    JsAstNodeType ast_type = is_arrow ? JS_AST_NODE_ARROW_FUNCTION :
                             is_expression ? JS_AST_NODE_FUNCTION_EXPRESSION :
                             JS_AST_NODE_FUNCTION_DECLARATION;

    JsFunctionNode* func = (JsFunctionNode*)alloc_js_ast_node(tp, ast_type, func_node, sizeof(JsFunctionNode));

    func->is_arrow = is_arrow;
    func->is_generator = is_generator;

    // Detect async: check for "async" anonymous child before "function" keyword or "=>"
    func->is_async = false;
    {
        uint32_t ccount = ts_node_child_count(func_node);
        for (uint32_t ci = 0; ci < ccount; ci++) {
            TSNode child = ts_node_child(func_node, ci);
            const char* ctype = ts_node_type(child);
            if (strcmp(ctype, "async") == 0) { func->is_async = true; break; }
            if (strcmp(ctype, "function") == 0 || strcmp(ctype, "=>") == 0) break;
        }
    }

    // Get function name (optional for expressions)
    TSNode name_node = ts_node_child_by_field_name(func_node, "name", strlen("name"));
    if (!ts_node_is_null(name_node)) {
        StrView name_source = js_node_source(tp, name_node);
        func->name = name_pool_create_strview(tp->name_pool, name_source);
    }

    // Get parameters - arrow functions can have "parameter" (singular) for single-param without parens
    // or "parameters" (plural) for multiple params or parens
    TSNode params_node = ts_node_child_by_field_name(func_node, "parameters", strlen("parameters"));
    if (!ts_node_is_null(params_node)) {
        uint32_t param_count = ts_node_named_child_count(params_node);
        JsAstNode* prev_param = NULL;

        for (uint32_t i = 0; i < param_count; i++) {
            TSNode param_node = ts_node_named_child(params_node, i);
            const char* ptype = ts_node_type(param_node);
            JsAstNode* param = NULL;
            if (strcmp(ptype, "identifier") == 0) {
                param = build_js_identifier(tp, param_node);
            } else if (strcmp(ptype, "required_parameter") == 0 ||
                       strcmp(ptype, "optional_parameter") == 0) {
                // TS parser wraps params: required_parameter pattern: (identifier)
                // Extract the inner pattern/name and any default value
                TSNode pat_node = ts_node_child_by_field_name(param_node, "pattern", 7);
                if (ts_node_is_null(pat_node)) {
                    pat_node = ts_node_child_by_field_name(param_node, "name", 4);
                }
                TSNode default_node = ts_node_child_by_field_name(param_node, "value", 5);
                if (!ts_node_is_null(default_node) && !ts_node_is_null(pat_node)) {
                    // param with default: build as assignment_pattern
                    JsAssignmentPatternNode* assign_pat = (JsAssignmentPatternNode*)alloc_js_ast_node(
                        tp, JS_AST_NODE_ASSIGNMENT_PATTERN, param_node, sizeof(JsAssignmentPatternNode));
                    assign_pat->left = build_js_expression(tp, pat_node);
                    assign_pat->right = build_js_expression(tp, default_node);
                    assign_pat->base.type = &TYPE_ANY;
                    param = (JsAstNode*)assign_pat;
                } else if (!ts_node_is_null(pat_node)) {
                    param = build_js_expression(tp, pat_node);
                }
            } else if (strcmp(ptype, "rest_pattern") == 0) {
                // ...args rest parameter — build as REST_ELEMENT wrapping inner identifier
                JsSpreadElementNode* rest = (JsSpreadElementNode*)alloc_js_ast_node(
                    tp, JS_AST_NODE_REST_ELEMENT, param_node, sizeof(JsSpreadElementNode));
                if (ts_node_named_child_count(param_node) > 0) {
                    TSNode inner = ts_node_named_child(param_node, 0);
                    rest->argument = build_js_expression(tp, inner);
                }
                rest->base.type = &TYPE_ARRAY;
                param = (JsAstNode*)rest;
            } else {
                param = build_js_expression(tp, param_node);
            }

            if (param) {
                if (!prev_param) {
                    func->params = param;
                } else {
                    prev_param->next = param;
                }
                prev_param = param;
            }
        }
    } else {
        // Check for single parameter (arrow function without parens: x => x * 2)
        TSNode param_node = ts_node_child_by_field_name(func_node, "parameter", strlen("parameter"));
        if (!ts_node_is_null(param_node)) {
            const char* ptype = ts_node_type(param_node);
            if (strcmp(ptype, "identifier") == 0) {
                func->params = build_js_identifier(tp, param_node);
            } else {
                func->params = build_js_expression(tp, param_node);
            }
        }
    }

    // Get function body
    TSNode body_node = ts_node_child_by_field_name(func_node, "body", strlen("body"));
    if (!ts_node_is_null(body_node)) {
        const char* body_type = ts_node_type(body_node);
        if (strcmp(body_type, "statement_block") == 0) {
            func->body = build_js_block_statement(tp, body_node);
        } else {
            // Arrow function with expression body
            func->body = build_js_expression(tp, body_node);
        }

        if (!func->body) {
            uint32_t start = ts_node_start_byte(body_node);
            StrView body_src = js_node_source(tp, body_node);
            log_error("Failed to build function body (body_type=%s, start=%u, src=%.*s)", body_type, start, (int)(body_src.length > 60 ? 60 : body_src.length), body_src.str);
            return NULL;
        }
    }

    func->base.type = &TYPE_FUNC;

    // Add function to scope if it has a name — but NOT for class method definitions,
    // which should not pollute the enclosing scope with their method names.
    bool is_method_def = (strcmp(node_type, "method_definition") == 0);
    if (func->name && !is_method_def) {
        js_scope_define(tp, func->name, (JsAstNode*)func, JS_VAR_VAR);
    }

    return (JsAstNode*)func;
}

// Build JavaScript if statement node
JsAstNode* build_js_if_statement(JsTranspiler* tp, TSNode if_node) {
    JsIfNode* if_stmt = (JsIfNode*)alloc_js_ast_node(tp, JS_AST_NODE_IF_STATEMENT, if_node, sizeof(JsIfNode));

    // Get condition
    TSNode test_node = ts_node_child_by_field_name(if_node, "condition", strlen("condition"));
    if (!ts_node_is_null(test_node)) {
        if_stmt->test = build_js_expression(tp, test_node);
    }

    // Get consequent (then branch)
    TSNode consequent_node = ts_node_child_by_field_name(if_node, "consequence", strlen("consequence"));
    if (!ts_node_is_null(consequent_node)) {
        if_stmt->consequent = build_js_statement(tp, consequent_node);
    }

    // Get alternate (else branch) - optional
    TSNode alternate_node = ts_node_child_by_field_name(if_node, "alternative", strlen("alternative"));
    if (!ts_node_is_null(alternate_node)) {
        if_stmt->alternate = build_js_statement(tp, alternate_node);
    }

    if_stmt->base.type = &TYPE_NULL; // if statements don't have a value

    return (JsAstNode*)if_stmt;
}

// Build JavaScript while statement node
JsAstNode* build_js_while_statement(JsTranspiler* tp, TSNode while_node) {
    JsWhileNode* while_stmt = (JsWhileNode*)alloc_js_ast_node(tp, JS_AST_NODE_WHILE_STATEMENT, while_node, sizeof(JsWhileNode));

    // Get condition
    TSNode test_node = ts_node_child_by_field_name(while_node, "condition", strlen("condition"));
    if (!ts_node_is_null(test_node)) {
        while_stmt->test = build_js_expression(tp, test_node);
    }

    // Get body
    TSNode body_node = ts_node_child_by_field_name(while_node, "body", strlen("body"));
    if (!ts_node_is_null(body_node)) {
        while_stmt->body = build_js_statement(tp, body_node);
    }

    while_stmt->base.type = &TYPE_NULL;

    return (JsAstNode*)while_stmt;
}

// Build JavaScript for statement node
JsAstNode* build_js_for_statement(JsTranspiler* tp, TSNode for_node) {
    JsForNode* for_stmt = (JsForNode*)alloc_js_ast_node(tp, JS_AST_NODE_FOR_STATEMENT, for_node, sizeof(JsForNode));

    // Push a block scope for the for-loop header (let/const in init are scoped to this loop)
    JsScope* for_scope = js_scope_create(tp, JS_SCOPE_BLOCK, tp->current_scope);
    js_scope_push(tp, for_scope);

    // Get init (optional) - field name is "initializer" in tree-sitter-javascript
    TSNode init_node = ts_node_child_by_field_name(for_node, "initializer", strlen("initializer"));
    if (!ts_node_is_null(init_node)) {
        for_stmt->init = build_js_statement(tp, init_node);
    }

    // Get test condition (optional) - field name is "condition"
    TSNode test_node = ts_node_child_by_field_name(for_node, "condition", strlen("condition"));
    if (!ts_node_is_null(test_node)) {
        for_stmt->test = build_js_expression(tp, test_node);
    }

    // Get update (optional) - field name is "increment" in tree-sitter-javascript
    TSNode update_node = ts_node_child_by_field_name(for_node, "increment", strlen("increment"));
    if (!ts_node_is_null(update_node)) {
        for_stmt->update = build_js_expression(tp, update_node);
    }

    // Get body
    TSNode body_node = ts_node_child_by_field_name(for_node, "body", strlen("body"));
    if (!ts_node_is_null(body_node)) {
        for_stmt->body = build_js_statement(tp, body_node);
    }

    // Pop for-loop scope
    js_scope_pop(tp);

    for_stmt->base.type = &TYPE_NULL;

    return (JsAstNode*)for_stmt;
}

// Build JavaScript return statement node
JsAstNode* build_js_return_statement(JsTranspiler* tp, TSNode return_node) {
    JsReturnNode* return_stmt = (JsReturnNode*)alloc_js_ast_node(tp, JS_AST_NODE_RETURN_STATEMENT, return_node, sizeof(JsReturnNode));

    // Get argument (optional)
    uint32_t child_count = ts_node_named_child_count(return_node);
    if (child_count > 0) {
        TSNode arg_node = ts_node_named_child(return_node, 0);
        return_stmt->argument = build_js_expression(tp, arg_node);
        return_stmt->base.type = return_stmt->argument->type;
    } else {
        return_stmt->base.type = &TYPE_NULL; // return undefined
    }

    return (JsAstNode*)return_stmt;
}

// Build JavaScript block statement node
JsAstNode* build_js_block_statement(JsTranspiler* tp, TSNode block_node) {
    JsBlockNode* block = (JsBlockNode*)alloc_js_ast_node(tp, JS_AST_NODE_BLOCK_STATEMENT, block_node, sizeof(JsBlockNode));

    // Create new block scope
    JsScope* block_scope = js_scope_create(tp, JS_SCOPE_BLOCK, tp->current_scope);
    js_scope_push(tp, block_scope);

    uint32_t stmt_count = ts_node_named_child_count(block_node);
    JsAstNode* prev_stmt = NULL;

    for (uint32_t i = 0; i < stmt_count; i++) {
        TSNode stmt_node = ts_node_named_child(block_node, i);
        JsAstNode* stmt = build_js_statement(tp, stmt_node);

        if (stmt) {
            if (!prev_stmt) {
                block->statements = stmt;
            } else {
                prev_stmt->next = stmt;
            }
            // walk to end of chain (TS builders may return linked lists)
            while (stmt->next) { stmt = stmt->next; }
            prev_stmt = stmt;
        }
    }

    // Pop block scope
    js_scope_pop(tp);

    block->base.type = &TYPE_NULL;

    return (JsAstNode*)block;
}

// Build JavaScript variable declaration node
JsAstNode* build_js_variable_declaration(JsTranspiler* tp, TSNode var_node) {
    // In TS mode, use TS variable declaration builder (handles type annotations)
    if (!tp->strict_js) {
        return build_ts_variable_decl_u(tp, var_node);
    }

    JsVariableDeclarationNode* var_decl = (JsVariableDeclarationNode*)alloc_js_ast_node(tp, JS_AST_NODE_VARIABLE_DECLARATION, var_node, sizeof(JsVariableDeclarationNode));

    // Determine variable kind (var, let, const)
    TSNode first_child = ts_node_child(var_node, 0);
    StrView kind_source = js_node_source(tp, first_child);

    if (strncmp(kind_source.str, "var", 3) == 0) {
        var_decl->kind = JS_VAR_VAR;
    } else if (strncmp(kind_source.str, "let", 3) == 0) {
        var_decl->kind = JS_VAR_LET;
    } else if (strncmp(kind_source.str, "const", 5) == 0) {
        var_decl->kind = JS_VAR_CONST;
    }

    // Build declarators
    uint32_t child_count = ts_node_named_child_count(var_node);
    JsAstNode* prev_declarator = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode declarator_node = ts_node_named_child(var_node, i);
        const char* declarator_type = ts_node_type(declarator_node);

        if (strcmp(declarator_type, "variable_declarator") == 0) {
            log_debug("Found variable_declarator");

            uint32_t declarator_child_count = ts_node_child_count(declarator_node);

            JsVariableDeclaratorNode* declarator = (JsVariableDeclaratorNode*)alloc_js_ast_node(tp, JS_AST_NODE_VARIABLE_DECLARATOR, declarator_node, sizeof(JsVariableDeclaratorNode));

            // Get identifier (child 0) — may be an identifier or a destructuring pattern
            TSNode id_node = ts_node_child(declarator_node, 0);

            if (!ts_node_is_null(id_node)) {
                const char* id_type = ts_node_type(id_node);
                if (strcmp(id_type, "array_pattern") == 0 || strcmp(id_type, "object_pattern") == 0) {
                    declarator->id = build_js_expression(tp, id_node);
                } else {
                    declarator->id = build_js_identifier(tp, id_node);
                }
            } else {
                declarator->id = NULL;
            }

            // Get initializer: look past any comment nodes for the '=' sign and value.
            // For `var x = /* @__PURE__ */ expr`, child 2 may be a comment node.
            TSNode init_node;
            bool has_initializer = false;
            for (uint32_t ci = 2; ci < declarator_child_count; ci++) {
                TSNode cand = ts_node_child(declarator_node, ci);
                if (ts_node_is_null(cand)) break;
                const char* cand_type = ts_node_type(cand);
                if (strcmp(cand_type, "comment") == 0) continue; // skip /* @__PURE__ */ etc.
                if (strcmp(cand_type, "=") == 0) continue;       // skip the '=' operator
                init_node = cand;
                has_initializer = true;
                break;
            }
            if (has_initializer) {
                declarator->init = build_js_expression(tp, init_node);
                if (declarator->init) {
                    declarator->base.type = declarator->init->type;
                } else {
                    declarator->base.type = &TYPE_ANY;
                }
            } else {
                declarator->init = NULL;
                declarator->base.type = &TYPE_NULL; // undefined
            }

            // Add to scope (only for simple identifiers; array/object patterns
            // have their elements registered individually by the transpiler)
            if (declarator->id && declarator->id->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* id = (JsIdentifierNode*)declarator->id;
                log_debug("var-decl-scope: defining '%.*s', declarator=%p, base.type=%p", 
                    (int)id->name->len, id->name->chars, declarator, declarator->base.type);
                js_scope_define(tp, id->name, (JsAstNode*)declarator, (JsVarKind)var_decl->kind);
            }

            if (!prev_declarator) {
                var_decl->declarations = (JsAstNode*)declarator;
            } else {
                prev_declarator->next = (JsAstNode*)declarator;
            }
            prev_declarator = (JsAstNode*)declarator;
        }
    }

    var_decl->base.type = &TYPE_NULL; // Variable declarations don't have a value

    return (JsAstNode*)var_decl;
}

// Build JavaScript expression from Tree-sitter node
JsAstNode* build_js_expression(JsTranspiler* tp, TSNode expr_node) {
    const char* node_type = ts_node_type(expr_node);

    if (strcmp(node_type, "identifier") == 0 || strcmp(node_type, "property_identifier") == 0 ||
        strcmp(node_type, "shorthand_property_identifier") == 0 ||
        strcmp(node_type, "type_identifier") == 0) {
        return build_js_identifier(tp, expr_node);
    } else if (strcmp(node_type, "private_property_identifier") == 0) {
        // Transform #field → __private_field
        StrView source = js_node_source(tp, expr_node);
        // Skip the '#' prefix
        if (source.length > 1 && source.str[0] == '#') {
            char buf[256];
            int len = snprintf(buf, sizeof(buf), "__private_%.*s", (int)(source.length - 1), source.str + 1);
            JsIdentifierNode* identifier = (JsIdentifierNode*)alloc_js_ast_node(tp, JS_AST_NODE_IDENTIFIER, expr_node, sizeof(JsIdentifierNode));
            identifier->name = name_pool_create_len(tp->name_pool, buf, len);
            identifier->entry = NULL;
            identifier->base.type = &TYPE_ANY;
            return (JsAstNode*)identifier;
        }
        return build_js_identifier(tp, expr_node);
    } else if (strcmp(node_type, "this") == 0) {
        // Handle 'this' keyword
        JsIdentifierNode* this_node = (JsIdentifierNode*)alloc_js_ast_node(tp, JS_AST_NODE_IDENTIFIER, expr_node, sizeof(JsIdentifierNode));
        this_node->name = name_pool_create_len(tp->name_pool, "this", 4);
        this_node->base.type = &TYPE_ANY;
        return (JsAstNode*)this_node;
    } else if (strcmp(node_type, "super") == 0) {
        // Handle 'super' keyword — create identifier with name "super"
        JsIdentifierNode* super_node = (JsIdentifierNode*)alloc_js_ast_node(tp, JS_AST_NODE_IDENTIFIER, expr_node, sizeof(JsIdentifierNode));
        super_node->name = name_pool_create_len(tp->name_pool, "super", 5);
        super_node->base.type = &TYPE_ANY;
        return (JsAstNode*)super_node;
    } else if (strcmp(node_type, "number") == 0 || strcmp(node_type, "string") == 0 ||
               strcmp(node_type, "true") == 0 || strcmp(node_type, "false") == 0 ||
               strcmp(node_type, "null") == 0 || strcmp(node_type, "undefined") == 0) {
        return build_js_literal(tp, expr_node);
    } else if (strcmp(node_type, "binary_expression") == 0) {
        return build_js_binary_expression(tp, expr_node);
    } else if (strcmp(node_type, "unary_expression") == 0) {
        return build_js_unary_expression(tp, expr_node);
    } else if (strcmp(node_type, "update_expression") == 0) {
        // ++i, i++, --i, i-- — reuse unary expression node
        JsUnaryNode* unary = (JsUnaryNode*)alloc_js_ast_node(tp, JS_AST_NODE_UNARY_EXPRESSION, expr_node, sizeof(JsUnaryNode));

        TSNode arg_node = ts_node_child_by_field_name(expr_node, "argument", 8);
        TSNode op_node = ts_node_child_by_field_name(expr_node, "operator", 8);

        unary->operand = build_js_expression(tp, arg_node);

        if (!ts_node_is_null(op_node)) {
            StrView op_source = js_node_source(tp, op_node);
            unary->op = js_operator_from_string(op_source.str, op_source.length);
            unary->prefix = (ts_node_start_byte(op_node) < ts_node_start_byte(arg_node));
        } else {
            unary->op = JS_OP_INCREMENT;
            unary->prefix = true;
        }

        unary->base.type = &TYPE_FLOAT;
        return (JsAstNode*)unary;
    } else if (strcmp(node_type, "call_expression") == 0) {
        return build_js_call_expression(tp, expr_node);
    } else if (strcmp(node_type, "new_expression") == 0) {
        return build_js_new_expression(tp, expr_node);
    } else if (strcmp(node_type, "member_expression") == 0 || strcmp(node_type, "subscript_expression") == 0) {
        return build_js_member_expression(tp, expr_node);
    } else if (strcmp(node_type, "array") == 0) {
        return build_js_array_expression(tp, expr_node);
    } else if (strcmp(node_type, "object") == 0) {
        return build_js_object_expression(tp, expr_node);
    } else if (strcmp(node_type, "function_expression") == 0 || strcmp(node_type, "arrow_function") == 0 ||
               strcmp(node_type, "generator_function") == 0) {
        return build_js_function(tp, expr_node);
    } else if (strcmp(node_type, "class") == 0) {
        // class expression: var X = class _X { ... }
        return build_js_class_declaration(tp, expr_node);
    } else if (strcmp(node_type, "yield_expression") == 0) {
        // yield / yield expr / yield* expr
        JsYieldNode* yield_node = (JsYieldNode*)alloc_js_ast_node(tp, JS_AST_NODE_YIELD_EXPRESSION, expr_node, sizeof(JsYieldNode));
        yield_node->delegate = false;
        yield_node->argument = NULL;

        // Check for '*' (delegate) among anonymous children
        uint32_t ccount = ts_node_child_count(expr_node);
        for (uint32_t ci = 0; ci < ccount; ci++) {
            TSNode child = ts_node_child(expr_node, ci);
            if (!ts_node_is_named(child)) {
                StrView src = js_node_source(tp, child);
                if (src.length == 1 && src.str[0] == '*') { yield_node->delegate = true; break; }
            }
        }

        // The named child (if any) is the argument expression
        if (ts_node_named_child_count(expr_node) > 0) {
            TSNode arg = ts_node_named_child(expr_node, 0);
            yield_node->argument = build_js_expression(tp, arg);
        }
        yield_node->base.type = &TYPE_ANY;
        return (JsAstNode*)yield_node;
    } else if (strcmp(node_type, "await_expression") == 0) {
        JsAwaitNode* await_node = (JsAwaitNode*)alloc_js_ast_node(tp, JS_AST_NODE_AWAIT_EXPRESSION, expr_node, sizeof(JsAwaitNode));
        if (ts_node_named_child_count(expr_node) > 0) {
            TSNode arg = ts_node_named_child(expr_node, 0);
            await_node->argument = build_js_expression(tp, arg);
        }
        await_node->base.type = &TYPE_ANY;
        return (JsAstNode*)await_node;
    } else if (strcmp(node_type, "assignment_expression") == 0 ||
               strcmp(node_type, "augmented_assignment_expression") == 0) {
        // Handle both plain (=) and augmented (+=, -=, etc.) assignment expressions
        JsAssignmentNode* assign = (JsAssignmentNode*)alloc_js_ast_node(tp, JS_AST_NODE_ASSIGNMENT_EXPRESSION, expr_node, sizeof(JsAssignmentNode));

        // Get left and right operands
        TSNode left_node = ts_node_child_by_field_name(expr_node, "left", strlen("left"));
        TSNode right_node = ts_node_child_by_field_name(expr_node, "right", strlen("right"));

        assign->left = build_js_expression(tp, left_node);
        assign->right = build_js_expression(tp, right_node);

        // Parse the actual operator
        // augmented_assignment_expression has 'operator' field; assignment_expression uses literal '='
        TSNode op_node = ts_node_child_by_field_name(expr_node, "operator", 8);
        if (!ts_node_is_null(op_node)) {
            StrView op_source = js_node_source(tp, op_node);
            assign->op = js_operator_from_string(op_source.str, op_source.length);
        } else {
            assign->op = JS_OP_ASSIGN;
        }
        assign->base.type = assign->right ? assign->right->type : &TYPE_ANY;

        return (JsAstNode*)assign;
    } else if (strcmp(node_type, "parenthesized_expression") == 0) {
        // Handle parenthesized expressions - skip comment children, return first real expression
        uint32_t nc = ts_node_named_child_count(expr_node);
        for (uint32_t i = 0; i < nc; i++) {
            TSNode inner_node = ts_node_named_child(expr_node, i);
            if (strcmp(ts_node_type(inner_node), "comment") == 0) continue;
            return build_js_expression(tp, inner_node);
        }
        return NULL;
    } else if (strcmp(node_type, "sequence_expression") == 0) {
        // v11: Comma operator — evaluate all, return last
        JsSequenceNode* seq = (JsSequenceNode*)alloc_js_ast_node(
            tp, JS_AST_NODE_SEQUENCE_EXPRESSION, expr_node, sizeof(JsSequenceNode));
        uint32_t count = ts_node_named_child_count(expr_node);
        JsAstNode* prev = NULL;
        for (uint32_t i = 0; i < count; i++) {
            TSNode child = ts_node_named_child(expr_node, i);
            JsAstNode* expr = build_js_expression(tp, child);
            if (expr) {
                if (!prev) seq->expressions = expr;
                else prev->next = expr;
                prev = expr;
            }
        }
        seq->base.type = &TYPE_ANY;
        return (JsAstNode*)seq;
    } else if (strcmp(node_type, "computed_property_name") == 0) {
        // [expr] — computed key in class/object. Unwrap the inner expression.
        // The inner expression is the first named child.
        if (ts_node_named_child_count(expr_node) > 0) {
            TSNode inner = ts_node_named_child(expr_node, 0);
            return build_js_expression(tp, inner);
        }
        // Empty computed property — return null identifier
        JsIdentifierNode* id = (JsIdentifierNode*)alloc_js_ast_node(tp, JS_AST_NODE_IDENTIFIER, expr_node, sizeof(JsIdentifierNode));
        id->name = name_pool_create_len(tp->name_pool, "__computed__", 12);
        id->base.type = &TYPE_ANY;
        return (JsAstNode*)id;
    } else if (strcmp(node_type, "ternary_expression") == 0) {
        JsConditionalNode* cond = (JsConditionalNode*)alloc_js_ast_node(tp, JS_AST_NODE_CONDITIONAL_EXPRESSION, expr_node, sizeof(JsConditionalNode));
        log_debug("ternary: building conditional expression");

        // Get test condition
        TSNode test_node = ts_node_child_by_field_name(expr_node, "condition", strlen("condition"));
        if (!ts_node_is_null(test_node)) {
            cond->test = build_js_expression(tp, test_node);
        }

        // Get consequent (true branch)
        TSNode consequent_node = ts_node_child_by_field_name(expr_node, "consequence", strlen("consequence"));
        if (!ts_node_is_null(consequent_node)) {
            cond->consequent = build_js_expression(tp, consequent_node);
        }

        // Get alternate (false branch)
        TSNode alternate_node = ts_node_child_by_field_name(expr_node, "alternative", strlen("alternative"));
        if (!ts_node_is_null(alternate_node)) {
            cond->alternate = build_js_expression(tp, alternate_node);
        }

        // Type is union of consequent and alternate types
        if (cond->consequent && cond->alternate) {
            if (cond->consequent->type->type_id == cond->alternate->type->type_id) {
                cond->base.type = cond->consequent->type;
            } else {
                cond->base.type = &TYPE_ANY;
            }
        } else {
            cond->base.type = &TYPE_ANY;
        }

        return (JsAstNode*)cond;
    } else if (strcmp(node_type, "template_string") == 0 || strcmp(node_type, "template_literal") == 0) {
        return build_js_template_literal(tp, expr_node);
    } else if (strcmp(node_type, "regex") == 0) {
        // v11: regex literal /pattern/flags
        JsRegexNode* re = (JsRegexNode*)alloc_js_ast_node(tp, JS_AST_NODE_REGEX, expr_node, sizeof(JsRegexNode));
        uint32_t start = ts_node_start_byte(expr_node);
        uint32_t end = ts_node_end_byte(expr_node);
        const char* text = tp->source + start;
        int text_len = end - start;
        // parse /pattern/flags: find last '/'
        re->pattern = NULL;
        re->pattern_len = 0;
        re->flags = NULL;
        re->flags_len = 0;
        if (text_len > 1 && text[0] == '/') {
            // find closing '/' (last one)
            int last_slash = -1;
            for (int i = text_len - 1; i > 0; i--) {
                if (text[i] == '/') { last_slash = i; break; }
            }
            if (last_slash > 0) {
                re->pattern = text + 1;
                re->pattern_len = last_slash - 1;
                if (last_slash + 1 < text_len) {
                    re->flags = text + last_slash + 1;
                    re->flags_len = text_len - last_slash - 1;
                }
            }
        }
        re->base.type = &TYPE_ANY;
        return (JsAstNode*)re;
    } else if (strcmp(node_type, "spread_element") == 0) {
        JsSpreadElementNode* spread = (JsSpreadElementNode*)alloc_js_ast_node(
            tp, JS_AST_NODE_SPREAD_ELEMENT, expr_node, sizeof(JsSpreadElementNode));
        TSNode arg_node = ts_node_named_child(expr_node, 0);
        if (!ts_node_is_null(arg_node)) {
            spread->argument = build_js_expression(tp, arg_node);
        }
        spread->base.type = &TYPE_ARRAY;
        return (JsAstNode*)spread;
    } else if (strcmp(node_type, "assignment_pattern") == 0) {
        // function parameter with default: (x = defaultVal)
        JsAssignmentPatternNode* assign_pat = (JsAssignmentPatternNode*)alloc_js_ast_node(
            tp, JS_AST_NODE_ASSIGNMENT_PATTERN, expr_node, sizeof(JsAssignmentPatternNode));
        TSNode left = ts_node_child_by_field_name(expr_node, "left", 4);
        TSNode right = ts_node_child_by_field_name(expr_node, "right", 5);
        if (!ts_node_is_null(left)) assign_pat->left = build_js_expression(tp, left);
        if (!ts_node_is_null(right)) assign_pat->right = build_js_expression(tp, right);
        assign_pat->base.type = &TYPE_ANY;
        return (JsAstNode*)assign_pat;
    } else if (strcmp(node_type, "array_pattern") == 0) {
        // destructuring pattern: [a, b, ...rest] or [, b] (with elisions)
        JsArrayPatternNode* pattern = (JsArrayPatternNode*)alloc_js_ast_node(
            tp, JS_AST_NODE_ARRAY_PATTERN, expr_node, sizeof(JsArrayPatternNode));
        // Use all children (including unnamed) to correctly track elisions like [, b]
        uint32_t count = ts_node_child_count(expr_node);
        JsAstNode* prev = NULL;
        bool expect_elem = true; // true = next non-comma is an element; after comma, elision if another comma
        for (uint32_t i = 0; i < count; i++) {
            TSNode child = ts_node_child(expr_node, i);
            const char* child_type = ts_node_type(child);
            if (strcmp(child_type, "[") == 0 || strcmp(child_type, "]") == 0) {
                continue;
            }
            if (strcmp(child_type, ",") == 0) {
                if (expect_elem) {
                    // consecutive comma (or leading comma) = elision: insert null node
                    JsAstNode* elision = (JsAstNode*)alloc_js_ast_node(
                        tp, JS_AST_NODE_NULL, child, sizeof(JsAstNode));
                    elision->type = &TYPE_ANY;
                    if (!prev) pattern->elements = elision;
                    else prev->next = elision;
                    prev = elision;
                } else {
                    // normal comma separator after an element — next expected is an element or elision
                    expect_elem = true;
                }
                continue;
            }
            // Real element
            expect_elem = false;
            JsAstNode* elem = NULL;
            if (strcmp(child_type, "identifier") == 0) {
                elem = build_js_identifier(tp, child);
            } else if (strcmp(child_type, "rest_pattern") == 0) {
                // ...rest — build as spread element with the inner identifier
                JsSpreadElementNode* rest = (JsSpreadElementNode*)alloc_js_ast_node(
                    tp, JS_AST_NODE_SPREAD_ELEMENT, child, sizeof(JsSpreadElementNode));
                TSNode inner = ts_node_named_child(child, 0);
                if (!ts_node_is_null(inner)) {
                    rest->argument = build_js_expression(tp, inner);
                }
                rest->base.type = &TYPE_ARRAY;
                elem = (JsAstNode*)rest;
            } else if (strcmp(child_type, "assignment_pattern") == 0) {
                // default value: a = defaultVal
                JsAssignmentPatternNode* assign_pat = (JsAssignmentPatternNode*)alloc_js_ast_node(
                    tp, JS_AST_NODE_ASSIGNMENT_PATTERN, child, sizeof(JsAssignmentPatternNode));
                TSNode left = ts_node_child_by_field_name(child, "left", 4);
                TSNode right = ts_node_child_by_field_name(child, "right", 5);
                if (!ts_node_is_null(left)) assign_pat->left = build_js_expression(tp, left);
                if (!ts_node_is_null(right)) assign_pat->right = build_js_expression(tp, right);
                assign_pat->base.type = &TYPE_ANY;
                elem = (JsAstNode*)assign_pat;
            } else {
                elem = build_js_expression(tp, child);
            }
            if (elem) {
                if (!prev) pattern->elements = elem;
                else prev->next = elem;
                prev = elem;
            }
        }
        pattern->base.type = &TYPE_ARRAY;
        return (JsAstNode*)pattern;
    } else if (strcmp(node_type, "object_pattern") == 0) {
        // destructuring pattern: {a, b, c: d, ...rest}
        JsObjectPatternNode* pattern = (JsObjectPatternNode*)alloc_js_ast_node(
            tp, JS_AST_NODE_OBJECT_PATTERN, expr_node, sizeof(JsObjectPatternNode));
        uint32_t count = ts_node_named_child_count(expr_node);
        JsAstNode* prev = NULL;
        for (uint32_t i = 0; i < count; i++) {
            TSNode child = ts_node_named_child(expr_node, i);
            const char* child_type = ts_node_type(child);
            JsAstNode* elem = NULL;
            if (strcmp(child_type, "shorthand_property_identifier_pattern") == 0 ||
                strcmp(child_type, "shorthand_property_identifier") == 0) {
                // {x} shorthand — build as property with key=value=x
                JsPropertyNode* prop = (JsPropertyNode*)alloc_js_ast_node(
                    tp, JS_AST_NODE_PROPERTY, child, sizeof(JsPropertyNode));
                prop->key = build_js_identifier(tp, child);
                prop->value = build_js_identifier(tp, child);
                prop->computed = false;
                prop->method = false;
                prop->base.type = &TYPE_ANY;
                elem = (JsAstNode*)prop;
            } else if (strcmp(child_type, "pair_pattern") == 0) {
                // {a: b} or {a: b = default}
                JsPropertyNode* prop = (JsPropertyNode*)alloc_js_ast_node(
                    tp, JS_AST_NODE_PROPERTY, child, sizeof(JsPropertyNode));
                TSNode key_node = ts_node_child_by_field_name(child, "key", 3);
                TSNode value_node = ts_node_child_by_field_name(child, "value", 5);
                if (!ts_node_is_null(key_node)) prop->key = build_js_expression(tp, key_node);
                if (!ts_node_is_null(value_node)) prop->value = build_js_expression(tp, value_node);
                prop->computed = false;
                prop->method = false;
                prop->base.type = &TYPE_ANY;
                elem = (JsAstNode*)prop;
            } else if (strcmp(child_type, "rest_pattern") == 0) {
                // ...rest
                JsSpreadElementNode* rest = (JsSpreadElementNode*)alloc_js_ast_node(
                    tp, JS_AST_NODE_REST_PROPERTY, child, sizeof(JsSpreadElementNode));
                TSNode inner = ts_node_named_child(child, 0);
                if (!ts_node_is_null(inner)) {
                    rest->argument = build_js_expression(tp, inner);
                }
                rest->base.type = &TYPE_ANY;
                elem = (JsAstNode*)rest;
            } else if (strcmp(child_type, "object_assignment_pattern") == 0) {
                // {x = defaultVal} shorthand with default
                JsPropertyNode* prop = (JsPropertyNode*)alloc_js_ast_node(
                    tp, JS_AST_NODE_PROPERTY, child, sizeof(JsPropertyNode));
                TSNode left = ts_node_child_by_field_name(child, "left", 4);
                TSNode right = ts_node_child_by_field_name(child, "right", 5);
                // key is the left identifier
                if (!ts_node_is_null(left)) prop->key = build_js_expression(tp, left);
                // value is an assignment pattern wrapping left = right
                JsAssignmentPatternNode* assign_pat = (JsAssignmentPatternNode*)alloc_js_ast_node(
                    tp, JS_AST_NODE_ASSIGNMENT_PATTERN, child, sizeof(JsAssignmentPatternNode));
                if (!ts_node_is_null(left)) assign_pat->left = build_js_expression(tp, left);
                if (!ts_node_is_null(right)) assign_pat->right = build_js_expression(tp, right);
                assign_pat->base.type = &TYPE_ANY;
                prop->value = (JsAstNode*)assign_pat;
                prop->computed = false;
                prop->method = false;
                prop->base.type = &TYPE_ANY;
                elem = (JsAstNode*)prop;
            } else {
                elem = build_js_expression(tp, child);
            }
            if (elem) {
                if (!prev) pattern->properties = elem;
                else prev->next = elem;
                prev = elem;
            }
        }
        pattern->base.type = &TYPE_ANY;
        return (JsAstNode*)pattern;
    } else if (!tp->strict_js && strcmp(node_type, "as_expression") == 0) {
        // TS: expr as Type — build as type expression wrapper
        TsTypeExprNode* as_node = (TsTypeExprNode*)alloc_js_ast_node(tp,
            (JsAstNodeType)TS_AST_NODE_AS_EXPRESSION, expr_node, sizeof(TsTypeExprNode));
        uint32_t cc = ts_node_named_child_count(expr_node);
        if (cc >= 2) {
            as_node->inner = build_js_expression(tp, ts_node_named_child(expr_node, 0));
            as_node->target_type = build_ts_type_expr_u(tp, ts_node_named_child(expr_node, 1));
        }
        return (JsAstNode*)as_node;
    } else if (!tp->strict_js && strcmp(node_type, "satisfies_expression") == 0) {
        // TS: expr satisfies Type
        TsTypeExprNode* sat_node = (TsTypeExprNode*)alloc_js_ast_node(tp,
            (JsAstNodeType)TS_AST_NODE_SATISFIES_EXPRESSION, expr_node, sizeof(TsTypeExprNode));
        uint32_t cc = ts_node_named_child_count(expr_node);
        if (cc >= 2) {
            sat_node->inner = build_js_expression(tp, ts_node_named_child(expr_node, 0));
            sat_node->target_type = build_ts_type_expr_u(tp, ts_node_named_child(expr_node, 1));
        }
        return (JsAstNode*)sat_node;
    } else if (!tp->strict_js && strcmp(node_type, "non_null_expression") == 0) {
        // TS: expr! — non-null assertion
        TsNonNullNode* nn = (TsNonNullNode*)alloc_js_ast_node(tp,
            (JsAstNodeType)TS_AST_NODE_NON_NULL_EXPRESSION, expr_node, sizeof(TsNonNullNode));
        if (ts_node_named_child_count(expr_node) > 0) {
            nn->inner = build_js_expression(tp, ts_node_named_child(expr_node, 0));
        }
        return (JsAstNode*)nn;
    } else {
        // Handle nodes that return numeric symbol IDs instead of type names
        TSSymbol symbol = ts_node_symbol(expr_node);

        // Check if this is a literal by examining the node content
        StrView source = js_node_source(tp, expr_node);
        if (source.length > 0) {
            char first_char = source.str[0];

            // Check if it's a number literal
            if ((first_char >= '0' && first_char <= '9') || first_char == '.' || first_char == '-') {
                return build_js_literal(tp, expr_node);
            }
            // Check if it's a string literal
            else if (first_char == '"' || first_char == '\'' || first_char == '`') {
                return build_js_literal(tp, expr_node);
            }
            // Check if it's a boolean literal
            else if (source.length >= 4 && strncmp(source.str, "true", 4) == 0) {
                return build_js_literal(tp, expr_node);
            }
            else if (source.length >= 5 && strncmp(source.str, "false", 5) == 0) {
                return build_js_literal(tp, expr_node);
            }
            // Check if it's null or undefined
            else if (source.length >= 4 && strncmp(source.str, "null", 4) == 0) {
                return build_js_literal(tp, expr_node);
            }
            else if (source.length >= 9 && strncmp(source.str, "undefined", 9) == 0) {
                return build_js_literal(tp, expr_node);
            }
            // Check if it's an identifier (starts with letter, $, or _)
            else if ((first_char >= 'a' && first_char <= 'z') ||
                     (first_char >= 'A' && first_char <= 'Z') ||
                     first_char == '$' || first_char == '_') {
                return build_js_identifier(tp, expr_node);
            }
        }

        // skip comments inside expressions
        if (strcmp(node_type, "comment") == 0) return NULL;

        log_error("Unsupported JavaScript expression type: %s (symbol: %d, content: %.*s)",
                  node_type, symbol, (int)source.length, source.str);
        return NULL;
    }
}

// Build JavaScript statement from Tree-sitter node
JsAstNode* build_js_statement(JsTranspiler* tp, TSNode stmt_node) {
    const char* node_type = ts_node_type(stmt_node);

    if (strcmp(node_type, "variable_declaration") == 0 || strcmp(node_type, "lexical_declaration") == 0) {
        return build_js_variable_declaration(tp, stmt_node);
    } else if (strcmp(node_type, "function_declaration") == 0 ||
               strcmp(node_type, "generator_function_declaration") == 0) {
        return build_js_function(tp, stmt_node);
    } else if (strcmp(node_type, "if_statement") == 0) {
        return build_js_if_statement(tp, stmt_node);
    } else if (strcmp(node_type, "while_statement") == 0) {
        return build_js_while_statement(tp, stmt_node);
    } else if (strcmp(node_type, "for_statement") == 0) {
        return build_js_for_statement(tp, stmt_node);
    } else if (strcmp(node_type, "return_statement") == 0) {
        return build_js_return_statement(tp, stmt_node);
    } else if (strcmp(node_type, "statement_block") == 0) {
        return build_js_block_statement(tp, stmt_node);
    } else if (strcmp(node_type, "break_statement") == 0) {
        JsBreakContinueNode* break_stmt = (JsBreakContinueNode*)alloc_js_ast_node(tp, JS_AST_NODE_BREAK_STATEMENT, stmt_node, sizeof(JsBreakContinueNode));
        break_stmt->base.type = &TYPE_NULL;
        break_stmt->label = NULL;
        break_stmt->label_len = 0;
        // check for optional label child
        TSNode label_node = ts_node_child_by_field_name(stmt_node, "label", strlen("label"));
        if (!ts_node_is_null(label_node)) {
            uint32_t start = ts_node_start_byte(label_node);
            uint32_t end = ts_node_end_byte(label_node);
            break_stmt->label = tp->source + start;
            break_stmt->label_len = end - start;
        }
        return (JsAstNode*)break_stmt;
    } else if (strcmp(node_type, "continue_statement") == 0) {
        JsBreakContinueNode* continue_stmt = (JsBreakContinueNode*)alloc_js_ast_node(tp, JS_AST_NODE_CONTINUE_STATEMENT, stmt_node, sizeof(JsBreakContinueNode));
        continue_stmt->base.type = &TYPE_NULL;
        continue_stmt->label = NULL;
        continue_stmt->label_len = 0;
        // check for optional label child
        TSNode label_node = ts_node_child_by_field_name(stmt_node, "label", strlen("label"));
        if (!ts_node_is_null(label_node)) {
            uint32_t start = ts_node_start_byte(label_node);
            uint32_t end = ts_node_end_byte(label_node);
            continue_stmt->label = tp->source + start;
            continue_stmt->label_len = end - start;
        }
        return (JsAstNode*)continue_stmt;
    } else if (strcmp(node_type, "switch_statement") == 0) {
        return build_js_switch_statement(tp, stmt_node);
    } else if (strcmp(node_type, "do_statement") == 0) {
        return build_js_do_while_statement(tp, stmt_node);
    } else if (strcmp(node_type, "for_in_statement") == 0) {
        return build_js_for_in_statement(tp, stmt_node);
    } else if (strcmp(node_type, "try_statement") == 0) {
        return build_js_try_statement(tp, stmt_node);
    } else if (strcmp(node_type, "throw_statement") == 0) {
        return build_js_throw_statement(tp, stmt_node);
    } else if (strcmp(node_type, "class_declaration") == 0) {
        return build_js_class_declaration(tp, stmt_node);
    } else if (!tp->strict_js && strcmp(node_type, "interface_declaration") == 0) {
        return build_ts_interface_decl_u(tp, stmt_node);
    } else if (!tp->strict_js && strcmp(node_type, "type_alias_declaration") == 0) {
        return build_ts_type_alias_decl_u(tp, stmt_node);
    } else if (!tp->strict_js && strcmp(node_type, "enum_declaration") == 0) {
        return build_ts_enum_decl_u(tp, stmt_node);
    } else if (!tp->strict_js && (strcmp(node_type, "internal_module") == 0 ||
                                   strcmp(node_type, "module") == 0)) {
        return build_ts_namespace_decl_u(tp, stmt_node);
    } else if (!tp->strict_js && strcmp(node_type, "decorator") == 0) {
        return build_ts_decorator_u(tp, stmt_node);
    } else if (!tp->strict_js && strcmp(node_type, "ambient_declaration") == 0) {
        // declare ... — parse for type info but emit no code
        return NULL;
    } else if (strcmp(node_type, "import_statement") == 0) {
        return build_js_import_statement(tp, stmt_node);
    } else if (strcmp(node_type, "export_statement") == 0) {
        return build_js_export_statement(tp, stmt_node);
    } else if (strcmp(node_type, "with_statement") == 0) {
        // v17: build minimal with-statement node for early error rejection in strict mode
        JsAstNode* with_stmt = alloc_js_ast_node(tp, JS_AST_NODE_WITH_STATEMENT, stmt_node, sizeof(JsAstNode));
        with_stmt->type = &TYPE_NULL;
        return with_stmt;
    } else if (strcmp(node_type, "labeled_statement") == 0) {
        JsLabeledStatementNode* labeled = (JsLabeledStatementNode*)alloc_js_ast_node(tp, JS_AST_NODE_LABELED_STATEMENT, stmt_node, sizeof(JsLabeledStatementNode));
        labeled->base.type = &TYPE_NULL;
        labeled->label = NULL;
        labeled->label_len = 0;
        labeled->body = NULL;
        // get label name
        TSNode label_node = ts_node_child_by_field_name(stmt_node, "label", strlen("label"));
        if (!ts_node_is_null(label_node)) {
            uint32_t start = ts_node_start_byte(label_node);
            uint32_t end = ts_node_end_byte(label_node);
            labeled->label = tp->source + start;
            labeled->label_len = end - start;
        }
        // get body statement
        TSNode body_node = ts_node_child_by_field_name(stmt_node, "body", strlen("body"));
        if (!ts_node_is_null(body_node)) {
            labeled->body = build_js_statement(tp, body_node);
        }
        return (JsAstNode*)labeled;
    } else if (strcmp(node_type, "else_clause") == 0) {
        // Handle else clause - return the statement inside
        TSNode inner_node = ts_node_named_child(stmt_node, 0);
        return build_js_statement(tp, inner_node);
    } else if (strcmp(node_type, "expression_statement") == 0) {
        // TS: check if wrapping a namespace declaration
        if (!tp->strict_js) {
            TSNode inner = ts_node_named_child(stmt_node, 0);
            const char* inner_type = ts_node_type(inner);
            if (strcmp(inner_type, "internal_module") == 0 ||
                strcmp(inner_type, "module") == 0) {
                return build_ts_namespace_decl_u(tp, inner);
            }
        }

        JsExpressionStatementNode* expr_stmt = (JsExpressionStatementNode*)alloc_js_ast_node(tp, JS_AST_NODE_EXPRESSION_STATEMENT, stmt_node, sizeof(JsExpressionStatementNode));
        TSNode expr_node = ts_node_named_child(stmt_node, 0);

        expr_stmt->expression = build_js_expression(tp, expr_node);

        if (expr_stmt->expression && expr_stmt->expression->type) {
            expr_stmt->base.type = expr_stmt->expression->type;
        } else {
            expr_stmt->base.type = &TYPE_NULL;
        }
        return (JsAstNode*)expr_stmt;
    } else if (strcmp(node_type, "comment") == 0) {
        // Skip comments - they don't generate any code
        return NULL;
    } else if (strcmp(node_type, "empty_statement") == 0) {
        // Skip empty statements (standalone semicolons) - they don't generate any code
        return NULL;
    } else {
        // Treat any other expression types as expression statements (e.g., standalone
        // assignment_expression, call_expression, update_expression)
        JsAstNode* expr = build_js_expression(tp, stmt_node);
        if (expr) {
            JsExpressionStatementNode* expr_stmt = (JsExpressionStatementNode*)alloc_js_ast_node(
                tp, JS_AST_NODE_EXPRESSION_STATEMENT, stmt_node, sizeof(JsExpressionStatementNode));
            expr_stmt->expression = expr;
            expr_stmt->base.type = expr->type ? expr->type : &TYPE_NULL;
            return (JsAstNode*)expr_stmt;
        }
        log_error("Unsupported JavaScript statement type: %s", node_type);
        return NULL;
    }
}

// Build JavaScript import statement
// import X from 'module'  |  import { a, b } from 'module'  |  import * as X from 'module'
JsAstNode* build_js_import_statement(JsTranspiler* tp, TSNode import_node) {
    JsImportNode* node = (JsImportNode*)alloc_js_ast_node(tp, JS_AST_NODE_IMPORT_DECLARATION, import_node, sizeof(JsImportNode));
    node->source = NULL;
    node->specifiers = NULL;
    node->default_name = NULL;
    node->namespace_name = NULL;

    // Get source (module path string)
    TSNode source_node = ts_node_child_by_field_name(import_node, "source", strlen("source"));
    if (!ts_node_is_null(source_node)) {
        StrView src = js_node_source(tp, source_node);
        // Strip quotes from the string literal
        if (src.length >= 2) {
            node->source = name_pool_create_len(tp->name_pool, src.str + 1, src.length - 2);
        }
    }

    // Process children: import_clause contains identifiers, named_imports, namespace_import
    uint32_t ccount = ts_node_named_child_count(import_node);
    JsAstNode* prev_spec = NULL;

    for (uint32_t i = 0; i < ccount; i++) {
        TSNode child = ts_node_named_child(import_node, i);
        const char* ctype = ts_node_type(child);

        if (strcmp(ctype, "import_clause") == 0) {
            uint32_t clause_count = ts_node_named_child_count(child);
            for (uint32_t j = 0; j < clause_count; j++) {
                TSNode clause_child = ts_node_named_child(child, j);
                const char* cc_type = ts_node_type(clause_child);

                if (strcmp(cc_type, "identifier") == 0) {
                    // Default import: import X from 'module'
                    StrView name = js_node_source(tp, clause_child);
                    node->default_name = name_pool_create_len(tp->name_pool, name.str, name.length);
                } else if (strcmp(cc_type, "namespace_import") == 0) {
                    // import * as X from 'module' — the identifier inside namespace_import
                    if (ts_node_named_child_count(clause_child) > 0) {
                        TSNode ns_id = ts_node_named_child(clause_child, 0);
                        StrView name = js_node_source(tp, ns_id);
                        node->namespace_name = name_pool_create_len(tp->name_pool, name.str, name.length);
                    }
                } else if (strcmp(cc_type, "named_imports") == 0) {
                    // import { a, b as c } from 'module'
                    uint32_t spec_count = ts_node_named_child_count(clause_child);
                    for (uint32_t k = 0; k < spec_count; k++) {
                        TSNode spec = ts_node_named_child(clause_child, k);
                        const char* spec_type = ts_node_type(spec);
                        if (strcmp(spec_type, "import_specifier") == 0) {
                            TSNode name_node = ts_node_child_by_field_name(spec, "name", 4);
                            TSNode alias_node = ts_node_child_by_field_name(spec, "alias", 5);
                            if (!ts_node_is_null(name_node)) {
                                StrView remote = js_node_source(tp, name_node);
                                StrView local = remote; // default: local = remote
                                if (!ts_node_is_null(alias_node)) {
                                    local = js_node_source(tp, alias_node);
                                }
                                JsImportSpecifierNode* ispec = (JsImportSpecifierNode*)alloc_js_ast_node(
                                    tp, JS_AST_NODE_IMPORT_SPECIFIER, spec, sizeof(JsImportSpecifierNode));
                                ispec->remote_name = name_pool_create_len(tp->name_pool, remote.str, remote.length);
                                ispec->local_name = name_pool_create_len(tp->name_pool, local.str, local.length);
                                JsAstNode* spec_node = (JsAstNode*)ispec;
                                if (!prev_spec) {
                                    node->specifiers = spec_node;
                                } else {
                                    prev_spec->next = spec_node;
                                }
                                prev_spec = spec_node;
                            }
                        }
                    }
                }
            }
        }
    }

    node->base.type = &TYPE_NULL;
    return (JsAstNode*)node;
}

// Build JavaScript export statement
// export default X  |  export function f() {}  |  export { a, b }  |  export { a } from 'module'
JsAstNode* build_js_export_statement(JsTranspiler* tp, TSNode export_node) {
    JsExportNode* node = (JsExportNode*)alloc_js_ast_node(tp, JS_AST_NODE_EXPORT_DECLARATION, export_node, sizeof(JsExportNode));
    node->declaration = NULL;
    node->specifiers = NULL;
    node->source = NULL;
    node->is_default = false;

    // Check for "default" keyword among anonymous children
    uint32_t ccount = ts_node_child_count(export_node);
    for (uint32_t ci = 0; ci < ccount; ci++) {
        TSNode child = ts_node_child(export_node, ci);
        if (!ts_node_is_named(child)) {
            StrView src = js_node_source(tp, child);
            if (src.length == 7 && memcmp(src.str, "default", 7) == 0) {
                node->is_default = true;
                break;
            }
        }
    }

    // Get declaration (function, class, variable, or expression for default)
    TSNode decl_node = ts_node_child_by_field_name(export_node, "declaration", strlen("declaration"));
    if (!ts_node_is_null(decl_node)) {
        const char* dtype = ts_node_type(decl_node);
        if (strcmp(dtype, "function_declaration") == 0 ||
            strcmp(dtype, "generator_function_declaration") == 0 ||
            strcmp(dtype, "class_declaration") == 0 ||
            strcmp(dtype, "lexical_declaration") == 0 ||
            strcmp(dtype, "variable_declaration") == 0 ||
            (!tp->strict_js && (strcmp(dtype, "interface_declaration") == 0 ||
                                strcmp(dtype, "type_alias_declaration") == 0 ||
                                strcmp(dtype, "enum_declaration") == 0))) {
            node->declaration = build_js_statement(tp, decl_node);
        } else {
            node->declaration = build_js_expression(tp, decl_node);
        }
    }

    // Get value (for export default <expr>)
    TSNode value_node = ts_node_child_by_field_name(export_node, "value", strlen("value"));
    if (!ts_node_is_null(value_node) && !node->declaration) {
        node->declaration = build_js_expression(tp, value_node);
    }

    // Get source (for re-exports: export { a } from 'module')
    TSNode source_node = ts_node_child_by_field_name(export_node, "source", strlen("source"));
    if (!ts_node_is_null(source_node)) {
        StrView src = js_node_source(tp, source_node);
        if (src.length >= 2) {
            node->source = name_pool_create_len(tp->name_pool, src.str + 1, src.length - 2);
        }
    }

    // Get export specifiers (for export { a, b })
    JsAstNode* prev_spec = NULL;
    uint32_t ncount = ts_node_named_child_count(export_node);
    for (uint32_t i = 0; i < ncount; i++) {
        TSNode child = ts_node_named_child(export_node, i);
        const char* ctype = ts_node_type(child);
        if (strcmp(ctype, "export_clause") == 0) {
            uint32_t spec_count = ts_node_named_child_count(child);
            for (uint32_t k = 0; k < spec_count; k++) {
                TSNode spec = ts_node_named_child(child, k);
                if (strcmp(ts_node_type(spec), "export_specifier") == 0) {
                    TSNode name_node = ts_node_child_by_field_name(spec, "name", 4);
                    if (!ts_node_is_null(name_node)) {
                        JsAstNode* ident = build_js_identifier(tp, name_node);
                        if (ident) {
                            if (!prev_spec) {
                                node->specifiers = ident;
                            } else {
                                prev_spec->next = ident;
                            }
                            prev_spec = ident;
                        }
                    }
                }
            }
        }
    }

    node->base.type = &TYPE_NULL;
    return (JsAstNode*)node;
}

// Build JavaScript program (root node)
JsAstNode* build_js_program(JsTranspiler* tp, TSNode program_node) {
    JsProgramNode* program = (JsProgramNode*)alloc_js_ast_node(tp, JS_AST_NODE_PROGRAM, program_node, sizeof(JsProgramNode));

    uint32_t child_count = ts_node_named_child_count(program_node);
    JsAstNode* prev_stmt = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child_node = ts_node_named_child(program_node, i);
        JsAstNode* stmt = build_js_statement(tp, child_node);

        if (stmt) {
            if (!prev_stmt) {
                program->body = stmt;
            } else {
                prev_stmt->next = stmt;
            }
            // walk to end of chain (TS builders may return linked lists,
            // e.g. decorator → decorator → class_declaration)
            while (stmt->next) { stmt = stmt->next; }
            prev_stmt = stmt;
        }
    }

    program->base.type = &TYPE_ANY;

    return (JsAstNode*)program;
}

// Decode a template literal escape sequence (e.g., \t → tab, \n → newline)
static char js_decode_escape_char(char c) {
    switch (c) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case '\\': return '\\';
    case '\'': return '\'';
    case '"': return '"';
    case '0': return '\0';
    case 'b': return '\b';
    case 'f': return '\f';
    case 'v': return '\v';
    default: return c;
    }
}

// Build JavaScript template literal node
// Handles interleaving of quasis (text parts) and expressions (substitutions).
// Escape sequences (e.g., \t, \n) are merged into adjacent quasi text.
JsAstNode* build_js_template_literal(JsTranspiler* tp, TSNode template_node) {
    JsTemplateLiteralNode* template_lit = (JsTemplateLiteralNode*)alloc_js_ast_node(tp, JS_AST_NODE_TEMPLATE_LITERAL, template_node, sizeof(JsTemplateLiteralNode));

    uint32_t child_count = ts_node_named_child_count(template_node);
    JsAstNode* prev_quasi = NULL;
    JsAstNode* prev_expr = NULL;

    // Accumulate text for the current quasi (between substitutions).
    // Multiple string_fragment and escape_sequence nodes may appear between
    // two template_substitution nodes; they must be merged into one quasi.
    char quasi_buf[4096];
    int quasi_len = 0;

    // Helper: flush accumulated quasi text into a quasi node
    #define FLUSH_QUASI() do { \
        JsTemplateElementNode* element = (JsTemplateElementNode*)alloc_js_ast_node( \
            tp, JS_AST_NODE_TEMPLATE_ELEMENT, template_node, sizeof(JsTemplateElementNode)); \
        element->cooked = name_pool_create_len(tp->name_pool, quasi_buf, quasi_len); \
        element->raw = element->cooked; \
        element->tail = false; \
        element->base.type = &TYPE_STRING; \
        if (!prev_quasi) { template_lit->quasis = (JsAstNode*)element; } \
        else { prev_quasi->next = (JsAstNode*)element; } \
        prev_quasi = (JsAstNode*)element; \
        quasi_len = 0; \
    } while(0)

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(template_node, i);
        const char* child_type = ts_node_type(child);
        TSSymbol symbol = ts_node_symbol(child);

        if (strcmp(child_type, "string_fragment") == 0 || symbol == sym_template_chars) {
            // Append text to current quasi buffer
            StrView source = js_node_source(tp, child);
            int copy_len = (int)source.length;
            if (quasi_len + copy_len > (int)sizeof(quasi_buf) - 1)
                copy_len = (int)sizeof(quasi_buf) - 1 - quasi_len;
            if (copy_len > 0) {
                memcpy(quasi_buf + quasi_len, source.str, copy_len);
                quasi_len += copy_len;
            }
        } else if (strcmp(child_type, "escape_sequence") == 0) {
            // Decode escape sequence and append to current quasi buffer
            StrView source = js_node_source(tp, child);
            if (source.length >= 2 && source.str[0] == '\\') {
                char esc = source.str[1];
                if (esc == 'u' && source.length >= 6 && source.str[2] != '{') {
                    // \uXXXX — 4-hex-digit Unicode escape, encode as UTF-8
                    char hex[5] = {source.str[2], source.str[3], source.str[4], source.str[5], 0};
                    uint32_t code = (uint32_t)strtoul(hex, NULL, 16);
                    if (code <= 0x7F) {
                        if (quasi_len < (int)sizeof(quasi_buf) - 1)
                            quasi_buf[quasi_len++] = (char)code;
                    } else if (code <= 0x7FF) {
                        if (quasi_len < (int)sizeof(quasi_buf) - 2) {
                            quasi_buf[quasi_len++] = (char)(0xC0 | (code >> 6));
                            quasi_buf[quasi_len++] = (char)(0x80 | (code & 0x3F));
                        }
                    } else {
                        if (quasi_len < (int)sizeof(quasi_buf) - 3) {
                            quasi_buf[quasi_len++] = (char)(0xE0 | (code >> 12));
                            quasi_buf[quasi_len++] = (char)(0x80 | ((code >> 6) & 0x3F));
                            quasi_buf[quasi_len++] = (char)(0x80 | (code & 0x3F));
                        }
                    }
                } else if (esc == 'u' && source.length >= 4 && source.str[2] == '{') {
                    // \u{XXXXX} — Unicode code point escape
                    int hex_end = 3;
                    while (hex_end < (int)source.length && source.str[hex_end] != '}') hex_end++;
                    char hex[16] = {0};
                    int hex_len = hex_end - 3;
                    if (hex_len > 0 && hex_len < 16) {
                        memcpy(hex, source.str + 3, hex_len);
                        uint32_t code = (uint32_t)strtoul(hex, NULL, 16);
                        if (code <= 0x7F) {
                            if (quasi_len < (int)sizeof(quasi_buf) - 1)
                                quasi_buf[quasi_len++] = (char)code;
                        } else if (code <= 0x7FF) {
                            if (quasi_len < (int)sizeof(quasi_buf) - 2) {
                                quasi_buf[quasi_len++] = (char)(0xC0 | (code >> 6));
                                quasi_buf[quasi_len++] = (char)(0x80 | (code & 0x3F));
                            }
                        } else if (code <= 0xFFFF) {
                            if (quasi_len < (int)sizeof(quasi_buf) - 3) {
                                quasi_buf[quasi_len++] = (char)(0xE0 | (code >> 12));
                                quasi_buf[quasi_len++] = (char)(0x80 | ((code >> 6) & 0x3F));
                                quasi_buf[quasi_len++] = (char)(0x80 | (code & 0x3F));
                            }
                        } else if (code <= 0x10FFFF) {
                            if (quasi_len < (int)sizeof(quasi_buf) - 4) {
                                quasi_buf[quasi_len++] = (char)(0xF0 | (code >> 18));
                                quasi_buf[quasi_len++] = (char)(0x80 | ((code >> 12) & 0x3F));
                                quasi_buf[quasi_len++] = (char)(0x80 | ((code >> 6) & 0x3F));
                                quasi_buf[quasi_len++] = (char)(0x80 | (code & 0x3F));
                            }
                        }
                    }
                } else if (esc == 'x' && source.length >= 4) {
                    // \xXX — 2-hex-digit escape
                    char hex[3] = {source.str[2], source.str[3], 0};
                    uint32_t code = (uint32_t)strtoul(hex, NULL, 16);
                    if (code <= 0x7F) {
                        if (quasi_len < (int)sizeof(quasi_buf) - 1)
                            quasi_buf[quasi_len++] = (char)code;
                    } else {
                        if (quasi_len < (int)sizeof(quasi_buf) - 2) {
                            quasi_buf[quasi_len++] = (char)(0xC0 | (code >> 6));
                            quasi_buf[quasi_len++] = (char)(0x80 | (code & 0x3F));
                        }
                    }
                } else {
                    char decoded = js_decode_escape_char(esc);
                    if (quasi_len < (int)sizeof(quasi_buf) - 1) {
                        quasi_buf[quasi_len++] = decoded;
                    }
                }
            }
        } else if (strcmp(child_type, "template_substitution") == 0) {
            // Flush accumulated text as a quasi, then add the expression
            FLUSH_QUASI();

            TSNode expr_node = ts_node_named_child(child, 0);
            JsAstNode* expr = build_js_expression(tp, expr_node);
            if (expr) {
                if (!prev_expr) { template_lit->expressions = expr; }
                else { prev_expr->next = expr; }
                prev_expr = expr;
            }
        }
        // Ignore other child types (comments, etc.)
    }

    // Flush remaining quasi text (the tail)
    FLUSH_QUASI();
    if (prev_quasi) {
        ((JsTemplateElementNode*)prev_quasi)->tail = true;
    }

    #undef FLUSH_QUASI

    template_lit->base.type = &TYPE_STRING;
    return (JsAstNode*)template_lit;
}

// v5: Build JavaScript new expression node
JsAstNode* build_js_new_expression(JsTranspiler* tp, TSNode new_node) {
    JsCallNode* call = (JsCallNode*)alloc_js_ast_node(tp, JS_AST_NODE_NEW_EXPRESSION, new_node, sizeof(JsCallNode));

    // Get constructor - tree-sitter uses "constructor" field for new expressions
    TSNode callee_node = ts_node_child_by_field_name(new_node, "constructor", strlen("constructor"));
    if (ts_node_is_null(callee_node)) {
        // Fallback: try first named child
        callee_node = ts_node_named_child(new_node, 0);
        if (ts_node_is_null(callee_node)) {
            log_error("new expression has no constructor node");
            return NULL;
        }
    }

    call->callee = build_js_expression(tp, callee_node);
    if (!call->callee) {
        log_error("Failed to build new expression constructor");
        return NULL;
    }

    // Get arguments
    TSNode args_node = ts_node_child_by_field_name(new_node, "arguments", strlen("arguments"));
    if (!ts_node_is_null(args_node)) {
        uint32_t arg_count = ts_node_named_child_count(args_node);
        JsAstNode* prev_arg = NULL;

        for (uint32_t i = 0; i < arg_count; i++) {
            TSNode arg_node = ts_node_named_child(args_node, i);
            JsAstNode* arg = build_js_expression(tp, arg_node);
            if (!arg) continue;

            if (!prev_arg) {
                call->arguments = arg;
            } else {
                prev_arg->next = arg;
            }
            prev_arg = arg;
        }
    }

    call->base.type = &TYPE_ANY;
    return (JsAstNode*)call;
}

// v5: Build JavaScript switch statement node
JsAstNode* build_js_switch_statement(JsTranspiler* tp, TSNode switch_node) {
    JsSwitchNode* sw = (JsSwitchNode*)alloc_js_ast_node(tp, JS_AST_NODE_SWITCH_STATEMENT, switch_node, sizeof(JsSwitchNode));

    // Get discriminant (the value being switched on)
    TSNode value_node = ts_node_child_by_field_name(switch_node, "value", strlen("value"));
    if (!ts_node_is_null(value_node)) {
        sw->discriminant = build_js_expression(tp, value_node);
    }

    // Get switch body - iterate children looking for switch_case and switch_default
    TSNode body_node = ts_node_child_by_field_name(switch_node, "body", strlen("body"));
    if (ts_node_is_null(body_node)) {
        // Fallback: the body might be the second named child
        body_node = switch_node;
    }

    JsAstNode* prev_case = NULL;
    uint32_t child_count = ts_node_named_child_count(body_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(body_node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "switch_case") == 0 || strcmp(child_type, "switch_default") == 0) {
            JsSwitchCaseNode* case_node = (JsSwitchCaseNode*)alloc_js_ast_node(
                tp, JS_AST_NODE_SWITCH_CASE, child, sizeof(JsSwitchCaseNode));

            // Get test expression (NULL for default)
            if (strcmp(child_type, "switch_case") == 0) {
                TSNode value = ts_node_child_by_field_name(child, "value", strlen("value"));
                if (!ts_node_is_null(value)) {
                    case_node->test = build_js_expression(tp, value);
                }
            }

            // Get consequent statements
            JsAstNode* prev_stmt = NULL;
            uint32_t stmt_count = ts_node_named_child_count(child);
            // Determine position of the value node so we can skip it in the body
            TSNode case_value = ts_node_child_by_field_name(child, "value", strlen("value"));
            uint32_t value_start = ts_node_is_null(case_value) ? UINT32_MAX : ts_node_start_byte(case_value);
            for (uint32_t j = 0; j < stmt_count; j++) {
                TSNode stmt_child = ts_node_named_child(child, j);
                // Skip the value node itself (any expression type)
                if (ts_node_start_byte(stmt_child) == value_start) {
                    continue;
                }
                JsAstNode* stmt = build_js_statement(tp, stmt_child);
                if (!stmt) continue;
                if (!prev_stmt) {
                    case_node->consequent = stmt;
                } else {
                    prev_stmt->next = stmt;
                }
                prev_stmt = stmt;
            }

            case_node->base.type = &TYPE_NULL;

            if (!prev_case) {
                sw->cases = (JsAstNode*)case_node;
            } else {
                prev_case->next = (JsAstNode*)case_node;
            }
            prev_case = (JsAstNode*)case_node;
        }
    }

    sw->base.type = &TYPE_NULL;
    return (JsAstNode*)sw;
}

// v5: Build JavaScript do...while statement node
JsAstNode* build_js_do_while_statement(JsTranspiler* tp, TSNode do_node) {
    JsDoWhileNode* do_while = (JsDoWhileNode*)alloc_js_ast_node(
        tp, JS_AST_NODE_DO_WHILE_STATEMENT, do_node, sizeof(JsDoWhileNode));

    // Get body
    TSNode body_node = ts_node_child_by_field_name(do_node, "body", strlen("body"));
    if (!ts_node_is_null(body_node)) {
        do_while->body = build_js_statement(tp, body_node);
    }

    // Get condition
    TSNode condition_node = ts_node_child_by_field_name(do_node, "condition", strlen("condition"));
    if (!ts_node_is_null(condition_node)) {
        do_while->test = build_js_expression(tp, condition_node);
    }

    do_while->base.type = &TYPE_NULL;
    return (JsAstNode*)do_while;
}

// v5: Build JavaScript for...in statement node (also handles for...of)
JsAstNode* build_js_for_in_statement(JsTranspiler* tp, TSNode for_node) {
    // Tree-sitter uses "for_in_statement" for both for-in and for-of.
    // The "operator" field distinguishes them: "in" vs "of"
    bool is_for_of = false;
    TSNode op_node = ts_node_child_by_field_name(for_node, "operator", strlen("operator"));
    if (!ts_node_is_null(op_node)) {
        const char* op_text = ts_node_type(op_node);
        is_for_of = (strcmp(op_text, "of") == 0);
    }

    JsForOfNode* for_of = (JsForOfNode*)alloc_js_ast_node(
        tp, is_for_of ? JS_AST_NODE_FOR_OF_STATEMENT : JS_AST_NODE_FOR_IN_STATEMENT,
        for_node, sizeof(JsForOfNode));

    // Get the variable declaration (left side)
    // Tree-sitter structure: for (const x of arr) → "left" field contains the variable
    TSNode left_node = ts_node_child_by_field_name(for_node, "left", strlen("left"));
    if (!ts_node_is_null(left_node)) {
        const char* left_type = ts_node_type(left_node);
        if (strcmp(left_type, "identifier") == 0) {
            for_of->left = build_js_identifier(tp, left_node);
        } else {
            // Could be a variable declaration or pattern
            for_of->left = build_js_expression(tp, left_node);
        }
    }

    // Determine the variable kind from the first child (var/let/const keyword)
    for_of->kind = 0; // default: var
    uint32_t child_count = ts_node_child_count(for_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(for_node, i);
        const char* child_type = ts_node_type(child);
        if (strcmp(child_type, "var") == 0) { for_of->kind = 0; break; }
        if (strcmp(child_type, "let") == 0) { for_of->kind = 1; break; }
        if (strcmp(child_type, "const") == 0) { for_of->kind = 2; break; }
        if (strcmp(child_type, "of") == 0 || strcmp(child_type, "in") == 0) break;
    }

    // Get the iterable expression (right side)
    TSNode right_node = ts_node_child_by_field_name(for_node, "right", strlen("right"));
    if (!ts_node_is_null(right_node)) {
        for_of->right = build_js_expression(tp, right_node);
    }

    // Get the body
    TSNode body_node = ts_node_child_by_field_name(for_node, "body", strlen("body"));
    if (!ts_node_is_null(body_node)) {
        for_of->body = build_js_statement(tp, body_node);
    }

    for_of->base.type = &TYPE_NULL;
    return (JsAstNode*)for_of;
}

// Build JavaScript try statement node
JsAstNode* build_js_try_statement(JsTranspiler* tp, TSNode try_node) {
    JsTryNode* try_stmt = (JsTryNode*)alloc_js_ast_node(tp, JS_AST_NODE_TRY_STATEMENT, try_node, sizeof(JsTryNode));

    // Get try block
    TSNode block_node = ts_node_child_by_field_name(try_node, "body", 4);
    if (!ts_node_is_null(block_node)) {
        try_stmt->block = build_js_block_statement(tp, block_node);
    }

    // Get catch clause (optional)
    TSNode handler_node = ts_node_child_by_field_name(try_node, "handler", 7);
    if (!ts_node_is_null(handler_node)) {
        JsCatchNode* catch_clause = (JsCatchNode*)alloc_js_ast_node(tp, JS_AST_NODE_CATCH_CLAUSE, handler_node, sizeof(JsCatchNode));

        // Get catch parameter (optional in modern JS)
        TSNode param_node = ts_node_child_by_field_name(handler_node, "parameter", 9);
        if (!ts_node_is_null(param_node)) {
            catch_clause->param = build_js_identifier(tp, param_node);
        }

        // Get catch body
        TSNode catch_body_node = ts_node_child_by_field_name(handler_node, "body", 4);
        if (!ts_node_is_null(catch_body_node)) {
            catch_clause->body = build_js_block_statement(tp, catch_body_node);
        }

        catch_clause->base.type = &TYPE_NULL;
        try_stmt->handler = (JsAstNode*)catch_clause;
    }

    // Get finally block (optional)
    TSNode finalizer_node = ts_node_child_by_field_name(try_node, "finalizer", 9);
    if (!ts_node_is_null(finalizer_node)) {
        try_stmt->finalizer = build_js_block_statement(tp, finalizer_node);
    }

    try_stmt->base.type = &TYPE_NULL;
    return (JsAstNode*)try_stmt;
}

// Build JavaScript throw statement node
JsAstNode* build_js_throw_statement(JsTranspiler* tp, TSNode throw_node) {
    JsThrowNode* throw_stmt = (JsThrowNode*)alloc_js_ast_node(tp, JS_AST_NODE_THROW_STATEMENT, throw_node, sizeof(JsThrowNode));

    // Get argument
    uint32_t child_count = ts_node_named_child_count(throw_node);
    if (child_count > 0) {
        TSNode arg_node = ts_node_named_child(throw_node, 0);
        throw_stmt->argument = build_js_expression(tp, arg_node);
    }

    throw_stmt->base.type = &TYPE_NULL;
    return (JsAstNode*)throw_stmt;
}

// Build JavaScript class declaration node
JsAstNode* build_js_class_declaration(JsTranspiler* tp, TSNode class_node) {
    // In TS mode, use TS class builder (handles decorators, constructor param properties)
    if (!tp->strict_js) {
        return build_ts_class_decl_u(tp, class_node);
    }

    JsClassNode* class_decl = (JsClassNode*)alloc_js_ast_node(tp, JS_AST_NODE_CLASS_DECLARATION, class_node, sizeof(JsClassNode));

    // Get class name
    TSNode name_node = ts_node_child_by_field_name(class_node, "name", strlen("name"));
    if (!ts_node_is_null(name_node)) {
        StrView name_source = js_node_source(tp, name_node);
        class_decl->name = name_pool_create_strview(tp->name_pool, name_source);
    }

    // Get superclass (optional) — tree-sitter puts it inside a class_heritage
    // or extends_clause child node depending on grammar version
    uint32_t child_count_cls = ts_node_named_child_count(class_node);
    for (uint32_t i = 0; i < child_count_cls; i++) {
        TSNode child = ts_node_named_child(class_node, i);
        const char* child_type = ts_node_type(child);
        if (strcmp(child_type, "class_heritage") == 0) {
            // class_heritage may contain extends_clause or direct superclass expression
            TSNode super_node = ts_node_named_child(child, 0);
            if (!ts_node_is_null(super_node)) {
                // if the child is extends_clause, get the expression inside it
                if (strcmp(ts_node_type(super_node), "extends_clause") == 0) {
                    TSNode expr_node = ts_node_named_child(super_node, 0);
                    if (!ts_node_is_null(expr_node)) {
                        class_decl->superclass = build_js_expression(tp, expr_node);
                    }
                } else {
                    class_decl->superclass = build_js_expression(tp, super_node);
                }
            }
            break;
        } else if (strcmp(child_type, "extends_clause") == 0) {
            // some grammar versions put extends_clause directly on the class node
            TSNode super_expr = ts_node_named_child(child, 0);
            if (!ts_node_is_null(super_expr)) {
                class_decl->superclass = build_js_expression(tp, super_expr);
            }
            break;
        }
    }

    // Get class body
    TSNode body_node = ts_node_child_by_field_name(class_node, "body", strlen("body"));
    if (!ts_node_is_null(body_node)) {
        class_decl->body = build_js_class_body(tp, body_node);
    }

    class_decl->base.type = &TYPE_FUNC; // Classes are constructor functions

    // Add class to scope
    if (class_decl->name) {
        js_scope_define(tp, class_decl->name, (JsAstNode*)class_decl, JS_VAR_VAR);
    }

    return (JsAstNode*)class_decl;
}

// Build JavaScript static/instance field definition (class field)
JsAstNode* build_js_field_definition(JsTranspiler* tp, TSNode field_node) {
    JsFieldDefinitionNode* field = (JsFieldDefinitionNode*)alloc_js_ast_node(
        tp, JS_AST_NODE_FIELD_DEFINITION, field_node, sizeof(JsFieldDefinitionNode));
    field->is_static = false;
    field->is_private = false;
    field->computed = false;
    field->key = NULL;
    field->value = NULL;

    // check for 'static' keyword
    uint32_t child_count = ts_node_child_count(field_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(field_node, i);
        const char* child_type = ts_node_type(child);
        if (strcmp(child_type, "static") == 0) {
            field->is_static = true;
            break;
        }
    }

    // get property name — tree-sitter uses "property" for field_definition,
    // "name" for public_field_definition
    TSNode prop_node = ts_node_child_by_field_name(field_node, "property", strlen("property"));
    if (ts_node_is_null(prop_node)) {
        prop_node = ts_node_child_by_field_name(field_node, "name", strlen("name"));
    }
    if (!ts_node_is_null(prop_node)) {
        const char* prop_type = ts_node_type(prop_node);
        if (strcmp(prop_type, "private_property_identifier") == 0) {
            field->is_private = true;
        }
        if (strcmp(prop_type, "computed_property_name") == 0) {
            field->computed = true;
        }
        field->key = build_js_expression(tp, prop_node);
    }

    // get initializer value
    TSNode value_node = ts_node_child_by_field_name(field_node, "value", strlen("value"));
    if (!ts_node_is_null(value_node)) {
        field->value = build_js_expression(tp, value_node);
    }

    return (JsAstNode*)field;
}

// Build JavaScript class body
JsAstNode* build_js_class_body(JsTranspiler* tp, TSNode body_node) {
    JsBlockNode* body = (JsBlockNode*)alloc_js_ast_node(tp, JS_AST_NODE_BLOCK_STATEMENT, body_node, sizeof(JsBlockNode));

    uint32_t method_count = ts_node_named_child_count(body_node);
    JsAstNode* prev_method = NULL;

    for (uint32_t i = 0; i < method_count; i++) {
        TSNode child_node = ts_node_named_child(body_node, i);
        const char* child_type = ts_node_type(child_node);

        JsAstNode* method = NULL;
        if (strcmp(child_type, "field_definition") == 0 ||
            strcmp(child_type, "public_field_definition") == 0) {
            method = build_js_field_definition(tp, child_node);
        } else if (strcmp(child_type, "class_static_block") == 0) {
            // static { ... } block
            JsStaticBlockNode* sb = (JsStaticBlockNode*)alloc_js_ast_node(
                tp, JS_AST_NODE_STATIC_BLOCK, child_node, sizeof(JsStaticBlockNode));
            sb->body = NULL;
            TSNode body_node = ts_node_child_by_field_name(child_node, "body", strlen("body"));
            if (!ts_node_is_null(body_node)) {
                sb->body = build_js_block_statement(tp, body_node);
            }
            method = (JsAstNode*)sb;
        } else {
            method = build_js_method_definition(tp, child_node);
        }

        if (method) {
            if (!prev_method) {
                body->statements = method;
            } else {
                prev_method->next = method;
            }
            prev_method = method;
        }
    }

    body->base.type = &TYPE_NULL;
    return (JsAstNode*)body;
}

// Build JavaScript method definition
JsAstNode* build_js_method_definition(JsTranspiler* tp, TSNode method_node) {
    JsMethodDefinitionNode* method = (JsMethodDefinitionNode*)alloc_js_ast_node(tp, JS_AST_NODE_METHOD_DEFINITION, method_node, sizeof(JsMethodDefinitionNode));

    // Initialize method properties
    method->computed = false;
    method->static_method = false;
    method->kind = JsMethodDefinitionNode::JS_METHOD_METHOD;

    // Check for 'static', 'get', 'set' keywords
    uint32_t child_count = ts_node_child_count(method_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(method_node, i);
        const char* child_type = ts_node_type(child);
        if (strcmp(child_type, "static") == 0) {
            method->static_method = true;
        } else if (strcmp(child_type, "get") == 0) {
            method->kind = JsMethodDefinitionNode::JS_METHOD_GET;
        } else if (strcmp(child_type, "set") == 0) {
            method->kind = JsMethodDefinitionNode::JS_METHOD_SET;
        }
    }

    // Get method key
    TSNode key_node = ts_node_child_by_field_name(method_node, "name", strlen("name"));
    if (!ts_node_is_null(key_node)) {
        // Check if this is a computed property name [expr]
        const char* key_type = ts_node_type(key_node);
        if (strcmp(key_type, "computed_property_name") == 0) {
            method->computed = true;
        }
        method->key = build_js_expression(tp, key_node);
    }

    // Get method value (function) - method_definition has parameters and body directly
    // so we pass the method node itself to build_js_function
    method->value = build_js_function(tp, method_node);

    // TODO: Parse method modifiers (constructor, getter, setter, static)

    method->base.type = &TYPE_FUNC;
    return (JsAstNode*)method;
}

// ============================================================================
// TS-specific builders (merged from build_ts_ast.cpp)
// These are called when !tp->strict_js (TS mode) for unified AST building.
// ============================================================================

// Utility: get source text for a tree-sitter node (TS version)
static const char* ts_node_text_util(JsTranspiler* tp, TSNode node, int* out_len) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    *out_len = (int)(end - start);
    return tp->source + start;
}

// allocate a String* from the AST pool containing a copy of (src, len)
static String* ts_pool_string_util(JsTranspiler* tp, const char* src, int len) {
    String* s = (String*)pool_alloc(tp->ast_pool, sizeof(String) + len + 1);
    s->len = len;
    s->is_ascii = 1;
    memcpy(s->chars, src, len);
    s->chars[len] = '\0';
    return s;
}

// ============================================================================
// TS type node builders
// ============================================================================

static TsTypeNode* build_ts_predefined_type_u(JsTranspiler* tp, TSNode node) {
    int len;
    const char* text = ts_node_text_util(tp, node, &len);
    TsPredefinedTypeNode* pn = (TsPredefinedTypeNode*)alloc_js_ast_node(tp,
        (JsAstNodeType)TS_AST_NODE_PREDEFINED_TYPE, node, sizeof(TsPredefinedTypeNode));
    pn->predefined_id = ts_predefined_name_to_type_id(text, len);
    return (TsTypeNode*)pn;
}

static TsTypeNode* build_ts_type_reference_u(JsTranspiler* tp, TSNode node) {
    TsTypeReferenceNode* rn = (TsTypeReferenceNode*)alloc_js_ast_node(tp,
        (JsAstNodeType)TS_AST_NODE_TYPE_REFERENCE, node, sizeof(TsTypeReferenceNode));

    uint32_t child_count = ts_node_named_child_count(node);
    if (child_count > 0) {
        TSNode name_node = ts_node_named_child(node, 0);
        int len;
        const char* text = ts_node_text_util(tp, name_node, &len);
        rn->name = ts_pool_string_util(tp, text, len);
        if (child_count > 1) {
            TSNode args_node = ts_node_named_child(node, 1);
            uint32_t arg_count = ts_node_named_child_count(args_node);
            if (arg_count > 0) {
                rn->type_args = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * arg_count);
                rn->type_arg_count = (int)arg_count;
                for (uint32_t i = 0; i < arg_count; i++) {
                    rn->type_args[i] = build_ts_type_expr_u(tp, ts_node_named_child(args_node, i));
                }
            }
        }
    } else {
        int len;
        const char* text = ts_node_text_util(tp, node, &len);
        rn->name = ts_pool_string_util(tp, text, len);
    }
    return (TsTypeNode*)rn;
}

static TsTypeNode* build_ts_union_type_u(JsTranspiler* tp, TSNode node) {
    TsUnionTypeNode* un = (TsUnionTypeNode*)alloc_js_ast_node(tp,
        (JsAstNodeType)TS_AST_NODE_UNION_TYPE, node, sizeof(TsUnionTypeNode));
    uint32_t child_count = ts_node_named_child_count(node);
    un->types = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * child_count);
    un->type_count = (int)child_count;
    for (uint32_t i = 0; i < child_count; i++) {
        un->types[i] = build_ts_type_expr_u(tp, ts_node_named_child(node, i));
    }
    return (TsTypeNode*)un;
}

static TsTypeNode* build_ts_intersection_type_u(JsTranspiler* tp, TSNode node) {
    TsIntersectionTypeNode* in_node = (TsIntersectionTypeNode*)alloc_js_ast_node(tp,
        (JsAstNodeType)TS_AST_NODE_INTERSECTION_TYPE, node, sizeof(TsIntersectionTypeNode));
    uint32_t child_count = ts_node_named_child_count(node);
    in_node->types = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * child_count);
    in_node->type_count = (int)child_count;
    for (uint32_t i = 0; i < child_count; i++) {
        in_node->types[i] = build_ts_type_expr_u(tp, ts_node_named_child(node, i));
    }
    return (TsTypeNode*)in_node;
}

static TsTypeNode* build_ts_array_type_u(JsTranspiler* tp, TSNode node) {
    TsArrayTypeNode* an = (TsArrayTypeNode*)alloc_js_ast_node(tp,
        (JsAstNodeType)TS_AST_NODE_ARRAY_TYPE, node, sizeof(TsArrayTypeNode));
    if (ts_node_named_child_count(node) > 0) {
        an->element_type = build_ts_type_expr_u(tp, ts_node_named_child(node, 0));
    }
    return (TsTypeNode*)an;
}

static TsTypeNode* build_ts_tuple_type_u(JsTranspiler* tp, TSNode node) {
    TsTupleTypeNode* tn = (TsTupleTypeNode*)alloc_js_ast_node(tp,
        (JsAstNodeType)TS_AST_NODE_TUPLE_TYPE, node, sizeof(TsTupleTypeNode));
    uint32_t child_count = ts_node_named_child_count(node);
    tn->element_types = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * child_count);
    tn->element_count = (int)child_count;
    for (uint32_t i = 0; i < child_count; i++) {
        tn->element_types[i] = build_ts_type_expr_u(tp, ts_node_named_child(node, i));
    }
    return (TsTypeNode*)tn;
}

static TsTypeNode* build_ts_function_type_u(JsTranspiler* tp, TSNode node) {
    TsFunctionTypeNode* fn = (TsFunctionTypeNode*)alloc_js_ast_node(tp,
        (JsAstNodeType)TS_AST_NODE_FUNCTION_TYPE, node, sizeof(TsFunctionTypeNode));
    uint32_t child_count = ts_node_named_child_count(node);
    if (child_count > 0) {
        fn->return_type = build_ts_type_expr_u(tp, ts_node_named_child(node, child_count - 1));
        if (child_count > 1) {
            TSNode params_node = ts_node_named_child(node, 0);
            uint32_t param_count = ts_node_named_child_count(params_node);
            fn->param_count = (int)param_count;
            fn->param_types = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * param_count);
            fn->param_names = (String**)pool_calloc(tp->ast_pool, sizeof(String*) * param_count);
            for (uint32_t i = 0; i < param_count; i++) {
                TSNode param = ts_node_named_child(params_node, i);
                uint32_t pc = ts_node_named_child_count(param);
                fn->param_types[i] = NULL;
                for (uint32_t j = 0; j < pc; j++) {
                    TSNode child = ts_node_named_child(param, j);
                    const char* child_type = ts_node_type(child);
                    if (strcmp(child_type, "identifier") == 0) {
                        int len;
                        const char* text = ts_node_text_util(tp, child, &len);
                        fn->param_names[i] = ts_pool_string_util(tp, text, len);
                    } else if (strcmp(child_type, "type_annotation") == 0) {
                        if (ts_node_named_child_count(child) > 0) {
                            fn->param_types[i] = build_ts_type_expr_u(tp, ts_node_named_child(child, 0));
                        }
                    }
                }
                if (!fn->param_types[i]) {
                    fn->param_types[i] = (TsTypeNode*)alloc_js_ast_node(tp,
                        (JsAstNodeType)TS_AST_NODE_PREDEFINED_TYPE, param, sizeof(TsPredefinedTypeNode));
                    ((TsPredefinedTypeNode*)fn->param_types[i])->predefined_id = LMD_TYPE_ANY;
                }
            }
        }
    }
    return (TsTypeNode*)fn;
}

static TsTypeNode* build_ts_object_type_u(JsTranspiler* tp, TSNode node) {
    TsObjectTypeNode* on = (TsObjectTypeNode*)alloc_js_ast_node(tp,
        (JsAstNodeType)TS_AST_NODE_OBJECT_TYPE, node, sizeof(TsObjectTypeNode));
    uint32_t child_count = ts_node_named_child_count(node);
    on->member_count = (int)child_count;
    on->member_types = (TsTypeNode**)pool_calloc(tp->ast_pool, sizeof(TsTypeNode*) * child_count);
    on->member_names = (String**)pool_calloc(tp->ast_pool, sizeof(String*) * child_count);
    on->member_optional = (bool*)pool_calloc(tp->ast_pool, sizeof(bool) * child_count);
    on->member_readonly = (bool*)pool_calloc(tp->ast_pool, sizeof(bool) * child_count);

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode member = ts_node_named_child(node, i);
        uint32_t mc = ts_node_named_child_count(member);
        for (uint32_t j = 0; j < mc; j++) {
            TSNode child = ts_node_named_child(member, j);
            const char* child_type = ts_node_type(child);
            if (strcmp(child_type, "property_identifier") == 0 ||
                strcmp(child_type, "identifier") == 0) {
                int len;
                const char* text = ts_node_text_util(tp, child, &len);
                on->member_names[i] = ts_pool_string_util(tp, text, len);
            } else if (strcmp(child_type, "type_annotation") == 0) {
                if (ts_node_named_child_count(child) > 0) {
                    on->member_types[i] = build_ts_type_expr_u(tp, ts_node_named_child(child, 0));
                }
            }
        }
        uint32_t total_children = ts_node_child_count(member);
        for (uint32_t j = 0; j < total_children; j++) {
            TSNode child = ts_node_child(member, j);
            if (!ts_node_is_named(child)) {
                int len;
                const char* text = ts_node_text_util(tp, child, &len);
                if (len == 1 && text[0] == '?') {
                    on->member_optional[i] = true;
                }
            }
        }
        for (uint32_t j = 0; j < total_children; j++) {
            TSNode child = ts_node_child(member, j);
            int len;
            const char* text = ts_node_text_util(tp, child, &len);
            if (len == 8 && memcmp(text, "readonly", 8) == 0) {
                on->member_readonly[i] = true;
            }
        }
        if (!on->member_types[i]) {
            on->member_types[i] = (TsTypeNode*)alloc_js_ast_node(tp,
                (JsAstNodeType)TS_AST_NODE_PREDEFINED_TYPE, member, sizeof(TsPredefinedTypeNode));
            ((TsPredefinedTypeNode*)on->member_types[i])->predefined_id = LMD_TYPE_ANY;
        }
    }
    return (TsTypeNode*)on;
}

static TsTypeNode* build_ts_parenthesized_type_u(JsTranspiler* tp, TSNode node) {
    TsParenthesizedTypeNode* pn = (TsParenthesizedTypeNode*)alloc_js_ast_node(tp,
        (JsAstNodeType)TS_AST_NODE_PARENTHESIZED_TYPE, node, sizeof(TsParenthesizedTypeNode));
    if (ts_node_named_child_count(node) > 0) {
        pn->inner = build_ts_type_expr_u(tp, ts_node_named_child(node, 0));
    }
    return (TsTypeNode*)pn;
}

static TsTypeNode* build_ts_conditional_type_u(JsTranspiler* tp, TSNode node) {
    TsConditionalTypeNode* cn = (TsConditionalTypeNode*)alloc_js_ast_node(tp,
        (JsAstNodeType)TS_AST_NODE_CONDITIONAL_TYPE, node, sizeof(TsConditionalTypeNode));
    uint32_t cc = ts_node_named_child_count(node);
    if (cc >= 4) {
        cn->check_type   = build_ts_type_expr_u(tp, ts_node_named_child(node, 0));
        cn->extends_type = build_ts_type_expr_u(tp, ts_node_named_child(node, 1));
        cn->true_type    = build_ts_type_expr_u(tp, ts_node_named_child(node, 2));
        cn->false_type   = build_ts_type_expr_u(tp, ts_node_named_child(node, 3));
    }
    return (TsTypeNode*)cn;
}

// dispatch for all type expression nodes
static TsTypeNode* build_ts_type_expr_u(JsTranspiler* tp, TSNode node) {
    const char* type_str = ts_node_type(node);

    if (strcmp(type_str, "predefined_type") == 0)
        return build_ts_predefined_type_u(tp, node);
    if (strcmp(type_str, "type_identifier") == 0 || strcmp(type_str, "identifier") == 0)
        return build_ts_type_reference_u(tp, node);
    if (strcmp(type_str, "generic_type") == 0)
        return build_ts_type_reference_u(tp, node);
    if (strcmp(type_str, "union_type") == 0)
        return build_ts_union_type_u(tp, node);
    if (strcmp(type_str, "intersection_type") == 0)
        return build_ts_intersection_type_u(tp, node);
    if (strcmp(type_str, "array_type") == 0)
        return build_ts_array_type_u(tp, node);
    if (strcmp(type_str, "tuple_type") == 0)
        return build_ts_tuple_type_u(tp, node);
    if (strcmp(type_str, "function_type") == 0)
        return build_ts_function_type_u(tp, node);
    if (strcmp(type_str, "object_type") == 0)
        return build_ts_object_type_u(tp, node);
    if (strcmp(type_str, "parenthesized_type") == 0)
        return build_ts_parenthesized_type_u(tp, node);
    if (strcmp(type_str, "conditional_type") == 0)
        return build_ts_conditional_type_u(tp, node);
    if (strcmp(type_str, "literal_type") == 0) {
        TsLiteralTypeNode* ln = (TsLiteralTypeNode*)alloc_js_ast_node(tp,
            (JsAstNodeType)TS_AST_NODE_LITERAL_TYPE, node, sizeof(TsLiteralTypeNode));
        if (ts_node_named_child_count(node) > 0) {
            TSNode child = ts_node_named_child(node, 0);
            const char* ct = ts_node_type(child);
            if (strcmp(ct, "number") == 0) ln->literal_type = JS_LITERAL_NUMBER;
            else if (strcmp(ct, "string") == 0) ln->literal_type = JS_LITERAL_STRING;
            else if (strcmp(ct, "true") == 0) ln->literal_type = JS_LITERAL_BOOLEAN;
            else if (strcmp(ct, "false") == 0) ln->literal_type = JS_LITERAL_BOOLEAN;
            else if (strcmp(ct, "null") == 0) ln->literal_type = JS_LITERAL_NULL;
        }
        return (TsTypeNode*)ln;
    }

    log_debug("ts ast: unhandled type node '%s'", type_str);
    TsPredefinedTypeNode* pn = (TsPredefinedTypeNode*)alloc_js_ast_node(tp,
        (JsAstNodeType)TS_AST_NODE_PREDEFINED_TYPE, node, sizeof(TsPredefinedTypeNode));
    pn->predefined_id = LMD_TYPE_ANY;
    return (TsTypeNode*)pn;
}

// ============================================================================
// TS type annotation builder
// ============================================================================

static TsTypeNode* build_ts_type_annotation_u(JsTranspiler* tp, TSNode node) {
    TsTypeAnnotationNode* an = (TsTypeAnnotationNode*)alloc_js_ast_node(tp,
        (JsAstNodeType)TS_AST_NODE_TYPE_ANNOTATION, node, sizeof(TsTypeAnnotationNode));
    if (ts_node_named_child_count(node) > 0) {
        an->type_expr = build_ts_type_expr_u(tp, ts_node_named_child(node, 0));
    }
    return (TsTypeNode*)an;
}

// ============================================================================
// TS declaration builders
// ============================================================================

static JsAstNode* build_ts_interface_decl_u(JsTranspiler* tp, TSNode node) {
    TsInterfaceNode* iface = (TsInterfaceNode*)alloc_js_ast_node(tp,
        (JsAstNodeType)TS_AST_NODE_INTERFACE, node, sizeof(TsInterfaceNode));

    uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);
        if (strcmp(child_type, "type_identifier") == 0 || strcmp(child_type, "identifier") == 0) {
            int len;
            const char* text = ts_node_text_util(tp, child, &len);
            iface->name = ts_pool_string_util(tp, text, len);
        } else if (strcmp(child_type, "object_type") == 0 || strcmp(child_type, "interface_body") == 0) {
            iface->body = (TsObjectTypeNode*)build_ts_object_type_u(tp, child);
        } else if (strcmp(child_type, "extends_type_clause") == 0 ||
                   strcmp(child_type, "extends_clause") == 0) {
            uint32_t ext_count = ts_node_named_child_count(child);
            iface->extends_types = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * ext_count);
            iface->extends_count = (int)ext_count;
            for (uint32_t j = 0; j < ext_count; j++) {
                iface->extends_types[j] = build_ts_type_expr_u(tp, ts_node_named_child(child, j));
            }
        }
    }

    if (iface->name && iface->body) {
        Type* resolved = ts_resolve_type(tp, (TsTypeNode*)iface->body);
        iface->resolved_type = resolved;
        if (resolved && resolved->type_id == LMD_TYPE_MAP) {
            ((TypeMap*)resolved)->struct_name = iface->name->chars;
        }
        ts_type_registry_add(tp, iface->name->chars, resolved);
    }

    return (JsAstNode*)iface;
}

static JsAstNode* build_ts_type_alias_decl_u(JsTranspiler* tp, TSNode node) {
    TsTypeAliasNode* alias = (TsTypeAliasNode*)alloc_js_ast_node(tp,
        (JsAstNodeType)TS_AST_NODE_TYPE_ALIAS, node, sizeof(TsTypeAliasNode));

    uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);
        if (strcmp(child_type, "type_identifier") == 0 || strcmp(child_type, "identifier") == 0) {
            int len;
            const char* text = ts_node_text_util(tp, child, &len);
            alias->name = ts_pool_string_util(tp, text, len);
        } else {
            if (!alias->type_expr) {
                alias->type_expr = build_ts_type_expr_u(tp, child);
            }
        }
    }

    if (alias->name && alias->type_expr) {
        Type* resolved = ts_resolve_type(tp, alias->type_expr);
        alias->resolved_type = resolved;
        ts_type_registry_add(tp, alias->name->chars, resolved);
    }

    return (JsAstNode*)alias;
}

static JsAstNode* build_ts_enum_decl_u(JsTranspiler* tp, TSNode node) {
    TsEnumDeclarationNode* enum_node = (TsEnumDeclarationNode*)alloc_js_ast_node(tp,
        (JsAstNodeType)TS_AST_NODE_ENUM_DECLARATION, node, sizeof(TsEnumDeclarationNode));

    uint32_t total_children = ts_node_child_count(node);
    for (uint32_t i = 0; i < total_children; i++) {
        TSNode child = ts_node_child(node, i);
        if (!ts_node_is_named(child)) {
            int len;
            const char* text = ts_node_text_util(tp, child, &len);
            if (len == 5 && memcmp(text, "const", 5) == 0) {
                enum_node->is_const = true;
            }
        }
    }

    uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* child_type = ts_node_type(child);
        if (strcmp(child_type, "identifier") == 0) {
            int len;
            const char* text = ts_node_text_util(tp, child, &len);
            enum_node->name = ts_pool_string_util(tp, text, len);
        } else if (strcmp(child_type, "enum_body") == 0) {
            uint32_t mc = ts_node_named_child_count(child);
            enum_node->members = (JsAstNode**)pool_alloc(tp->ast_pool, sizeof(JsAstNode*) * mc);
            enum_node->member_count = (int)mc;
            int auto_val = 0;
            bool auto_val_valid = true;
            for (uint32_t j = 0; j < mc; j++) {
                TSNode mem = ts_node_named_child(child, j);
                const char* mem_type = ts_node_type(mem);
                TsEnumMemberNode* em = (TsEnumMemberNode*)alloc_js_ast_node(tp,
                    (JsAstNodeType)TS_AST_NODE_ENUM_MEMBER, mem, sizeof(TsEnumMemberNode));

                if (strcmp(mem_type, "enum_assignment") == 0) {
                    uint32_t mem_cc = ts_node_named_child_count(mem);
                    if (mem_cc > 0) {
                        TSNode name_node = ts_node_named_child(mem, 0);
                        int nlen;
                        const char* ntext = ts_node_text_util(tp, name_node, &nlen);
                        em->name = ts_pool_string_util(tp, ntext, nlen);
                    }
                    if (mem_cc > 1) {
                        TSNode init_node = ts_node_named_child(mem, 1);
                        em->initializer = build_js_expression(tp, init_node);
                        const char* init_type = ts_node_type(init_node);
                        if (strcmp(init_type, "number") == 0) {
                            int ilen;
                            const char* itext = ts_node_text_util(tp, init_node, &ilen);
                            char buf[64];
                            int copy_len = ilen < 63 ? ilen : 63;
                            memcpy(buf, itext, copy_len);
                            buf[copy_len] = '\0';
                            auto_val = (int)strtol(buf, NULL, 0);
                            em->auto_value = auto_val;
                            auto_val++;
                            auto_val_valid = true;
                        } else if (strcmp(init_type, "string") == 0 ||
                                   strcmp(init_type, "template_string") == 0) {
                            em->auto_value = -1;
                            auto_val_valid = false;
                        } else {
                            em->auto_value = auto_val_valid ? auto_val++ : -1;
                        }
                    }
                } else {
                    if (ts_node_named_child_count(mem) > 0) {
                        TSNode name_node = ts_node_named_child(mem, 0);
                        int nlen;
                        const char* ntext = ts_node_text_util(tp, name_node, &nlen);
                        em->name = ts_pool_string_util(tp, ntext, nlen);
                    } else {
                        int nlen;
                        const char* ntext = ts_node_text_util(tp, mem, &nlen);
                        em->name = ts_pool_string_util(tp, ntext, nlen);
                    }
                    em->auto_value = auto_val_valid ? auto_val++ : -1;
                }

                enum_node->members[j] = (JsAstNode*)em;
            }
        }
    }

    return (JsAstNode*)enum_node;
}

static JsAstNode* build_ts_namespace_decl_u(JsTranspiler* tp, TSNode node) {
    TsNamespaceDeclarationNode* ns = (TsNamespaceDeclarationNode*)alloc_js_ast_node(tp,
        (JsAstNodeType)TS_AST_NODE_NAMESPACE_DECLARATION, node, sizeof(TsNamespaceDeclarationNode));

    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(name_node)) {
        int len;
        const char* text = ts_node_text_util(tp, name_node, &len);
        ns->name = name_pool_create_len(tp->name_pool, text, len);
    }

    TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body_node)) {
        uint32_t child_count = ts_node_named_child_count(body_node);
        if (child_count > 0) {
            ns->body = (JsAstNode**)pool_alloc(tp->ast_pool, sizeof(JsAstNode*) * child_count);
            ns->body_count = 0;
            for (uint32_t i = 0; i < child_count; i++) {
                TSNode child = ts_node_named_child(body_node, i);
                // use unified builder for namespace body statements
                JsAstNode* stmt = build_js_statement(tp, child);
                if (stmt) {
                    ns->body[ns->body_count++] = stmt;
                }
            }
        }
    }

    return (JsAstNode*)ns;
}

static JsAstNode* build_ts_decorator_u(JsTranspiler* tp, TSNode node) {
    TsDecoratorNode* dec = (TsDecoratorNode*)alloc_js_ast_node(tp,
        (JsAstNodeType)TS_AST_NODE_DECORATOR, node, sizeof(TsDecoratorNode));
    if (ts_node_named_child_count(node) > 0) {
        TSNode expr_node = ts_node_named_child(node, 0);
        dec->expression = build_js_expression(tp, expr_node);
    }
    return (JsAstNode*)dec;
}

// ============================================================================
// TS function builder — handles TsFunctionNode, return_type, type_params
// ============================================================================

static JsAstNode* build_ts_parameter_u(JsTranspiler* tp, TSNode param_node, bool is_optional) {
    const char* ptype = ts_node_type(param_node);

    if (strcmp(ptype, "rest_pattern") == 0) {
        JsSpreadElementNode* rest = (JsSpreadElementNode*)alloc_js_ast_node(tp,
            JS_AST_NODE_REST_ELEMENT, param_node, sizeof(JsSpreadElementNode));
        if (ts_node_named_child_count(param_node) > 0) {
            TSNode inner = ts_node_named_child(param_node, 0);
            rest->argument = build_js_expression(tp, inner);
        }
        rest->base.type = &TYPE_ARRAY;
        return (JsAstNode*)rest;
    }

    if (strcmp(ptype, "identifier") == 0) {
        return build_js_identifier(tp, param_node);
    }

    if (strcmp(ptype, "required_parameter") == 0 || strcmp(ptype, "optional_parameter") == 0) {
        TSNode name_node = ts_node_child_by_field_name(param_node, "pattern", 7);
        if (ts_node_is_null(name_node)) {
            name_node = ts_node_child_by_field_name(param_node, "name", 4);
        }

        // build type annotation and capture it for type-driven codegen
        TSNode type_node_ts = ts_node_child_by_field_name(param_node, "type", 4);
        TsTypeAnnotationNode* ts_type = NULL;
        if (!ts_node_is_null(type_node_ts)) {
            ts_type = (TsTypeAnnotationNode*)build_ts_type_annotation_u(tp, type_node_ts);
        }

        TSNode value_node = ts_node_child_by_field_name(param_node, "value", 5);
        JsAstNode* default_value = NULL;
        if (!ts_node_is_null(value_node)) {
            default_value = build_js_expression(tp, value_node);
        }

        JsAstNode* name_ast = NULL;
        if (!ts_node_is_null(name_node)) {
            const char* ntype = ts_node_type(name_node);
            if (strcmp(ntype, "identifier") == 0) {
                name_ast = build_js_identifier(tp, name_node);
            } else {
                name_ast = build_js_expression(tp, name_node);
            }
        }

        // if no type annotation and no default: return plain identifier (avoids overhead)
        if (!ts_type && !default_value) {
            return name_ast;
        }

        if (default_value && !ts_type) {
            // default_value only — use cheap assignment_pattern
            JsAssignmentPatternNode* assign = (JsAssignmentPatternNode*)alloc_js_ast_node(tp,
                JS_AST_NODE_ASSIGNMENT_PATTERN, param_node, sizeof(JsAssignmentPatternNode));
            assign->left = name_ast;
            assign->right = default_value;
            assign->base.type = &TYPE_ANY;
            return (JsAstNode*)assign;
        }

        // build TsParameterNode to carry type annotation
        TsParameterNode* ts_param = (TsParameterNode*)alloc_js_ast_node(tp,
            (JsAstNodeType)TS_AST_NODE_PARAMETER, param_node, sizeof(TsParameterNode));
        ts_param->pattern = name_ast;
        ts_param->ts_type = ts_type;
        ts_param->default_value = default_value;
        ts_param->optional = (strcmp(ptype, "optional_parameter") == 0);
        ts_param->base.type = &TYPE_ANY;
        return (JsAstNode*)ts_param;
    }

    if (strcmp(ptype, "assignment_pattern") == 0) {
        return build_js_expression(tp, param_node);
    }

    return build_js_expression(tp, param_node);
}

static JsAstNode* build_ts_function_u(JsTranspiler* tp, TSNode func_node) {
    const char* node_type = ts_node_type(func_node);
    bool is_arrow = (strcmp(node_type, "arrow_function") == 0);
    bool is_generator = (strcmp(node_type, "generator_function") == 0 ||
                         strcmp(node_type, "generator_function_declaration") == 0);
    if (strcmp(node_type, "method_definition") == 0 && !is_generator) {
        uint32_t ccount = ts_node_child_count(func_node);
        for (uint32_t ci = 0; ci < ccount; ci++) {
            TSNode child = ts_node_child(func_node, ci);
            const char* ctype = ts_node_type(child);
            if (strcmp(ctype, "*") == 0) { is_generator = true; break; }
            if (strcmp(ctype, "formal_parameters") == 0 || strcmp(ctype, "statement_block") == 0) break;
        }
    }

    bool is_expression = is_arrow || (strcmp(node_type, "function_expression") == 0) ||
                         strcmp(node_type, "generator_function") == 0;

    JsAstNodeType ast_type = is_arrow ? JS_AST_NODE_ARROW_FUNCTION :
                             is_expression ? JS_AST_NODE_FUNCTION_EXPRESSION :
                             JS_AST_NODE_FUNCTION_DECLARATION;

    // allocate TsFunctionNode (extends JsFunctionNode with return_type, type_params)
    TsFunctionNode* ts_func = (TsFunctionNode*)alloc_js_ast_node(tp,
        ast_type, func_node, sizeof(TsFunctionNode));
    JsFunctionNode* func = &ts_func->base;

    func->is_arrow = is_arrow;
    func->is_generator = is_generator;

    // detect async
    func->is_async = false;
    {
        uint32_t ccount = ts_node_child_count(func_node);
        for (uint32_t ci = 0; ci < ccount; ci++) {
            TSNode child = ts_node_child(func_node, ci);
            const char* ctype = ts_node_type(child);
            if (strcmp(ctype, "async") == 0) { func->is_async = true; break; }
            if (strcmp(ctype, "function") == 0 || strcmp(ctype, "=>") == 0) break;
        }
    }

    // function name
    TSNode name_node = ts_node_child_by_field_name(func_node, "name", 4);
    if (!ts_node_is_null(name_node)) {
        int len;
        const char* text = ts_node_text_util(tp, name_node, &len);
        func->name = name_pool_create_strview(tp->name_pool, {.str = text, .length = (size_t)len});
    }

    // return type annotation (TS-specific)
    TSNode return_type_node = ts_node_child_by_field_name(func_node, "return_type", 11);
    if (!ts_node_is_null(return_type_node)) {
        TsTypeAnnotationNode* ret_ann = (TsTypeAnnotationNode*)build_ts_type_annotation_u(tp, return_type_node);
        ts_func->return_type = ret_ann;
        // also set on the base JsFunctionNode so jm_infer_return_type can detect it
        func->ts_return_type = ret_ann;
    }

    // type parameters (TS-specific: <T, U>)
    TSNode type_params_node = ts_node_child_by_field_name(func_node, "type_parameters", 15);
    if (!ts_node_is_null(type_params_node)) {
        uint32_t tp_count = ts_node_named_child_count(type_params_node);
        ts_func->type_params = (TsTypeParamNode**)pool_alloc(tp->ast_pool,
            sizeof(TsTypeParamNode*) * tp_count);
        ts_func->type_param_count = (int)tp_count;
        for (uint32_t i = 0; i < tp_count; i++) {
            TSNode tpn = ts_node_named_child(type_params_node, i);
            TsTypeParamNode* tpp = (TsTypeParamNode*)alloc_js_ast_node(tp,
                (JsAstNodeType)TS_AST_NODE_TYPE_PARAMETER, tpn, sizeof(TsTypeParamNode));
            if (ts_node_named_child_count(tpn) > 0) {
                TSNode name = ts_node_named_child(tpn, 0);
                int nlen;
                const char* ntext = ts_node_text_util(tp, name, &nlen);
                tpp->name = ts_pool_string_util(tp, ntext, nlen);
            }
            ts_func->type_params[i] = tpp;
        }
    }

    // parameters
    TSNode params_node = ts_node_child_by_field_name(func_node, "parameters", 10);
    if (!ts_node_is_null(params_node)) {
        uint32_t param_count = ts_node_named_child_count(params_node);
        JsAstNode* prev_param = NULL;
        for (uint32_t i = 0; i < param_count; i++) {
            TSNode param_node = ts_node_named_child(params_node, i);
            JsAstNode* param = build_ts_parameter_u(tp, param_node, false);
            if (param) {
                if (!prev_param) { func->params = param; }
                else { prev_param->next = param; }
                prev_param = param;
            }
        }
    } else {
        TSNode param_node = ts_node_child_by_field_name(func_node, "parameter", 9);
        if (!ts_node_is_null(param_node)) {
            func->params = build_ts_parameter_u(tp, param_node, false);
        }
    }

    // function body
    TSNode body_node = ts_node_child_by_field_name(func_node, "body", 4);
    if (!ts_node_is_null(body_node)) {
        const char* body_type = ts_node_type(body_node);
        if (strcmp(body_type, "statement_block") == 0) {
            func->body = build_js_block_statement(tp, body_node);
        } else {
            func->body = build_js_expression(tp, body_node);
        }
    }

    func->base.type = &TYPE_FUNC;

    bool is_method_def = (strcmp(node_type, "method_definition") == 0);
    if (func->name && !is_method_def) {
        js_scope_define(tp, func->name, (JsAstNode*)func, JS_VAR_VAR);
    }

    return (JsAstNode*)func;
}

// ============================================================================
// TS class builders
// ============================================================================

static JsAstNode* make_ts_identifier_u(JsTranspiler* tp, TSNode node, const char* name, int len) {
    JsIdentifierNode* id = (JsIdentifierNode*)alloc_js_ast_node(tp,
        JS_AST_NODE_IDENTIFIER, node, sizeof(JsIdentifierNode));
    id->name = name_pool_create_len(tp->name_pool, name, len);
    id->base.type = &TYPE_ANY;
    return (JsAstNode*)id;
}

static JsAstNode* make_this_assignment_u(JsTranspiler* tp, TSNode node, const char* name, int len) {
    JsMemberNode* member = (JsMemberNode*)alloc_js_ast_node(tp,
        JS_AST_NODE_MEMBER_EXPRESSION, node, sizeof(JsMemberNode));
    member->object = make_ts_identifier_u(tp, node, "this", 4);
    member->property = make_ts_identifier_u(tp, node, name, len);
    member->computed = false;
    member->optional = false;
    member->base.type = &TYPE_ANY;

    JsAssignmentNode* assign = (JsAssignmentNode*)alloc_js_ast_node(tp,
        JS_AST_NODE_ASSIGNMENT_EXPRESSION, node, sizeof(JsAssignmentNode));
    assign->op = JS_OP_ASSIGN;
    assign->left = (JsAstNode*)member;
    assign->right = make_ts_identifier_u(tp, node, name, len);
    assign->base.type = &TYPE_ANY;

    JsExpressionStatementNode* expr_stmt = (JsExpressionStatementNode*)alloc_js_ast_node(tp,
        JS_AST_NODE_EXPRESSION_STATEMENT, node, sizeof(JsExpressionStatementNode));
    expr_stmt->expression = (JsAstNode*)assign;
    expr_stmt->base.type = &TYPE_NULL;

    return (JsAstNode*)expr_stmt;
}

static JsAstNode* build_ts_class_body_u(JsTranspiler* tp, TSNode body_node) {
    JsBlockNode* body = (JsBlockNode*)alloc_js_ast_node(tp,
        JS_AST_NODE_BLOCK_STATEMENT, body_node, sizeof(JsBlockNode));

    uint32_t child_count = ts_node_named_child_count(body_node);
    JsAstNode* prev = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child_node = ts_node_named_child(body_node, i);
        const char* child_type = ts_node_type(child_node);

        JsAstNode* member_node = NULL;
        if (strcmp(child_type, "field_definition") == 0 ||
            strcmp(child_type, "public_field_definition") == 0) {
            member_node = build_js_field_definition(tp, child_node);
        } else if (strcmp(child_type, "method_definition") == 0) {
            JsMethodDefinitionNode* method = (JsMethodDefinitionNode*)alloc_js_ast_node(tp,
                JS_AST_NODE_METHOD_DEFINITION, child_node, sizeof(JsMethodDefinitionNode));
            method->computed = false;
            method->static_method = false;
            method->kind = JsMethodDefinitionNode::JS_METHOD_METHOD;

            uint32_t cc = ts_node_child_count(child_node);
            for (uint32_t ci = 0; ci < cc; ci++) {
                TSNode ch = ts_node_child(child_node, ci);
                const char* ct = ts_node_type(ch);
                if (strcmp(ct, "static") == 0) method->static_method = true;
                else if (strcmp(ct, "get") == 0) method->kind = JsMethodDefinitionNode::JS_METHOD_GET;
                else if (strcmp(ct, "set") == 0) method->kind = JsMethodDefinitionNode::JS_METHOD_SET;
            }

            TSNode key_node = ts_node_child_by_field_name(child_node, "name", 4);
            if (!ts_node_is_null(key_node)) {
                const char* key_type = ts_node_type(key_node);
                if (strcmp(key_type, "computed_property_name") == 0) {
                    method->computed = true;
                }
                method->key = build_js_expression(tp, key_node);

                int klen;
                const char* ktext = ts_node_text_util(tp, key_node, &klen);
                if (klen == 11 && memcmp(ktext, "constructor", 11) == 0) {
                    method->kind = JsMethodDefinitionNode::JS_METHOD_CONSTRUCTOR;
                }
            }

            method->value = build_ts_function_u(tp, child_node);

            // constructor parameter property desugaring
            if (method->kind == JsMethodDefinitionNode::JS_METHOD_CONSTRUCTOR && method->value) {
                JsFunctionNode* ctor_fn = (JsFunctionNode*)method->value;
                TSNode params_node = ts_node_child_by_field_name(child_node, "parameters", 10);
                if (!ts_node_is_null(params_node)) {
                    JsAstNode* assign_first = NULL;
                    JsAstNode* assign_last = NULL;

                    uint32_t param_count = ts_node_named_child_count(params_node);
                    for (uint32_t pi = 0; pi < param_count; pi++) {
                        TSNode param_cst = ts_node_named_child(params_node, pi);
                        const char* pt = ts_node_type(param_cst);

                        if (strcmp(pt, "required_parameter") == 0 ||
                            strcmp(pt, "optional_parameter") == 0) {
                            uint32_t pcc = ts_node_named_child_count(param_cst);
                            bool has_accessibility = false;
                            for (uint32_t pci = 0; pci < pcc; pci++) {
                                TSNode pc = ts_node_named_child(param_cst, pci);
                                if (strcmp(ts_node_type(pc), "accessibility_modifier") == 0) {
                                    has_accessibility = true;
                                    break;
                                }
                            }
                            if (!has_accessibility) {
                                uint32_t ptc = ts_node_child_count(param_cst);
                                for (uint32_t pci = 0; pci < ptc; pci++) {
                                    TSNode pc = ts_node_child(param_cst, pci);
                                    int rlen;
                                    const char* rtxt = ts_node_text_util(tp, pc, &rlen);
                                    if (rlen == 8 && memcmp(rtxt, "readonly", 8) == 0) {
                                        has_accessibility = true;
                                        break;
                                    }
                                }
                            }

                            if (has_accessibility) {
                                TSNode pname = ts_node_child_by_field_name(param_cst, "pattern", 7);
                                if (ts_node_is_null(pname)) {
                                    pname = ts_node_child_by_field_name(param_cst, "name", 4);
                                }
                                if (!ts_node_is_null(pname)) {
                                    int nlen;
                                    const char* ntext = ts_node_text_util(tp, pname, &nlen);
                                    JsAstNode* assign_stmt = make_this_assignment_u(tp, pname, ntext, nlen);
                                    if (!assign_first) {
                                        assign_first = assign_stmt;
                                        assign_last = assign_stmt;
                                    } else {
                                        assign_last->next = assign_stmt;
                                        assign_last = assign_stmt;
                                    }
                                }
                            }
                        }
                    }

                    if (assign_first && ctor_fn->body) {
                        if (ctor_fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
                            JsBlockNode* block = (JsBlockNode*)ctor_fn->body;
                            assign_last->next = block->statements;
                            block->statements = assign_first;
                        }
                    }
                }
            }

            method->base.type = &TYPE_FUNC;
            member_node = (JsAstNode*)method;
        } else {
            member_node = build_js_method_definition(tp, child_node);
        }

        if (member_node) {
            if (!prev) { body->statements = member_node; }
            else { prev->next = member_node; }
            prev = member_node;
        }
    }

    body->base.type = &TYPE_NULL;
    return (JsAstNode*)body;
}

static JsAstNode* build_ts_class_decl_u(JsTranspiler* tp, TSNode class_node) {
    // collect decorators
    TsDecoratorNode* decorators[16];
    int deco_count = 0;
    uint32_t child_count = ts_node_named_child_count(class_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(class_node, i);
        if (strcmp(ts_node_type(child), "decorator") == 0 && deco_count < 16) {
            TsDecoratorNode* dec = (TsDecoratorNode*)alloc_js_ast_node(tp,
                (JsAstNodeType)TS_AST_NODE_DECORATOR, child, sizeof(TsDecoratorNode));
            if (ts_node_named_child_count(child) > 0) {
                dec->expression = build_js_expression(tp, ts_node_named_child(child, 0));
            }
            decorators[deco_count++] = dec;
        }
    }

    JsClassNode* class_decl = (JsClassNode*)alloc_js_ast_node(tp,
        JS_AST_NODE_CLASS_DECLARATION, class_node, sizeof(JsClassNode));

    TSNode name_node = ts_node_child_by_field_name(class_node, "name", 4);
    if (!ts_node_is_null(name_node)) {
        int len;
        const char* text = ts_node_text_util(tp, name_node, &len);
        class_decl->name = name_pool_create_len(tp->name_pool, text, len);
    }

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(class_node, i);
        if (strcmp(ts_node_type(child), "class_heritage") == 0) {
            TSNode super_expr = ts_node_named_child(child, 0);
            if (!ts_node_is_null(super_expr)) {
                class_decl->superclass = build_js_expression(tp, super_expr);
            }
            break;
        }
    }

    TSNode body_node = ts_node_child_by_field_name(class_node, "body", 4);
    if (!ts_node_is_null(body_node)) {
        class_decl->body = build_ts_class_body_u(tp, body_node);
    }

    class_decl->base.type = &TYPE_FUNC;

    if (class_decl->name) {
        js_scope_define(tp, class_decl->name, (JsAstNode*)class_decl, JS_VAR_VAR);
    }

    if (deco_count > 0) {
        for (int i = 0; i < deco_count - 1; i++) {
            decorators[i]->base.next = (JsAstNode*)decorators[i + 1];
        }
        decorators[deco_count - 1]->base.next = NULL;
        JsAstNode* result = (JsAstNode*)decorators[0];
        decorators[deco_count - 1]->base.next = (JsAstNode*)class_decl;
        return result;
    }

    return (JsAstNode*)class_decl;
}

// ============================================================================
// TS variable declaration builder — handles type_annotation on declarators
// ============================================================================

static JsAstNode* build_ts_variable_decl_u(JsTranspiler* tp, TSNode var_node) {
    JsVariableDeclarationNode* var_decl = (JsVariableDeclarationNode*)alloc_js_ast_node(tp,
        JS_AST_NODE_VARIABLE_DECLARATION, var_node, sizeof(JsVariableDeclarationNode));

    TSNode first_child = ts_node_child(var_node, 0);
    int flen;
    const char* ftext = ts_node_text_util(tp, first_child, &flen);
    if (flen >= 3 && memcmp(ftext, "var", 3) == 0) {
        var_decl->kind = JS_VAR_VAR;
    } else if (flen >= 3 && memcmp(ftext, "let", 3) == 0) {
        var_decl->kind = JS_VAR_LET;
    } else if (flen >= 5 && memcmp(ftext, "const", 5) == 0) {
        var_decl->kind = JS_VAR_CONST;
    }

    uint32_t child_count = ts_node_named_child_count(var_node);
    JsAstNode* prev_declarator = NULL;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode declarator_node = ts_node_named_child(var_node, i);
        const char* declarator_type = ts_node_type(declarator_node);
        if (strcmp(declarator_type, "variable_declarator") != 0) continue;

        JsVariableDeclaratorNode* declarator = (JsVariableDeclaratorNode*)alloc_js_ast_node(tp,
            JS_AST_NODE_VARIABLE_DECLARATOR, declarator_node, sizeof(JsVariableDeclaratorNode));

        TSNode name_node = ts_node_child_by_field_name(declarator_node, "name", 4);
        if (!ts_node_is_null(name_node)) {
            const char* id_type = ts_node_type(name_node);
            if (strcmp(id_type, "array_pattern") == 0 || strcmp(id_type, "object_pattern") == 0) {
                declarator->id = build_js_expression(tp, name_node);
            } else {
                declarator->id = build_js_identifier(tp, name_node);
            }
        }

        // type annotation (TS-specific) — stored in declarator for type-driven codegen
        TSNode type_node = ts_node_child_by_field_name(declarator_node, "type", 4);
        if (!ts_node_is_null(type_node)) {
            declarator->ts_type = (TsTypeAnnotationNode*)build_ts_type_annotation_u(tp, type_node);
        }

        TSNode value_node = ts_node_child_by_field_name(declarator_node, "value", 5);
        if (!ts_node_is_null(value_node)) {
            declarator->init = build_js_expression(tp, value_node);
            if (declarator->init) {
                declarator->base.type = declarator->init->type;
            } else {
                declarator->base.type = &TYPE_ANY;
            }
        } else {
            declarator->init = NULL;
            declarator->base.type = &TYPE_NULL;
        }

        if (declarator->id && declarator->id->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)declarator->id;
            js_scope_define(tp, id->name, (JsAstNode*)declarator, (JsVarKind)var_decl->kind);
        }

        if (!prev_declarator) {
            var_decl->declarations = (JsAstNode*)declarator;
        } else {
            prev_declarator->next = (JsAstNode*)declarator;
        }
        prev_declarator = (JsAstNode*)declarator;
    }

    var_decl->base.type = &TYPE_NULL;
    return (JsAstNode*)var_decl;
}

// ============================================================================
// TS error handling
// ============================================================================

void ts_error(JsTranspiler* tp, TSNode node, const char* format, ...) {
    tp->has_errors = true;
    TSPoint pos = ts_node_start_point(node);
    char prefix[128];
    snprintf(prefix, sizeof(prefix), "ts error [%d:%d]: ", pos.row + 1, pos.column + 1);
    if (tp->error_buf) {
        strbuf_append_str(tp->error_buf, prefix);
    }
    log_error("%s", prefix);
    (void)format;
}

void ts_warning(JsTranspiler* tp, TSNode node, const char* format, ...) {
    TSPoint pos = ts_node_start_point(node);
    log_debug("ts warning [%d:%d]", pos.row + 1, pos.column + 1);
    (void)format;
}

// Main AST building entry point
JsAstNode* build_js_ast(JsTranspiler* tp, TSNode root) {
    const char* node_type = ts_node_type(root);

    if (strcmp(node_type, "program") == 0) {
        return build_js_program(tp, root);
    } else {
        log_error("Expected program node, got: %s", node_type);
        return NULL;
    }
}
