#include "js_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

// External Tree-sitter JavaScript parser
extern "C" {
    const TSLanguage *tree_sitter_javascript(void);
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
    } else if (len == 3) {
        if (strncmp(op_str, "===", 3) == 0) return JS_OP_STRICT_EQ;
        if (strncmp(op_str, "!==", 3) == 0) return JS_OP_STRICT_NE;
        if (strncmp(op_str, ">>>", 3) == 0) return JS_OP_BIT_URSHIFT;
    } else if (len == 6) {
        if (strncmp(op_str, "typeof", 6) == 0) return JS_OP_TYPEOF;
        if (strncmp(op_str, "delete", 6) == 0) return JS_OP_DELETE;
    } else if (len == 4) {
        if (strncmp(op_str, "void", 4) == 0) return JS_OP_VOID;
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
        // Create null-terminated string for strtod
        char* temp_str = (char*)malloc(source.length + 1);
        if (temp_str) {
            memcpy(temp_str, source.str, source.length);
            temp_str[source.length] = '\0';
            char* endptr;
            literal->value.number_value = strtod(temp_str, &endptr);
            free(temp_str);
        } else {
            literal->value.number_value = 0.0;
        }
        literal->base.type = &TYPE_FLOAT; // All JS numbers are float64
    } else if (strcmp(node_type, "string") == 0) {
        literal->literal_type = JS_LITERAL_STRING;
        // Remove quotes and handle escape sequences
        if (source.length >= 2) {
            // Create null-terminated string without quotes
            size_t content_len = source.length - 2;
            char* temp_str = (char*)malloc(content_len + 1);
            if (temp_str) {
                memcpy(temp_str, source.str + 1, content_len);
                temp_str[content_len] = '\0';
                literal->value.string_value = name_pool_create_len(tp->name_pool, temp_str, content_len);
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
    // printf("DEBUG: Looking up identifier: %.*s\n", (int)identifier->name->len, identifier->name->chars);
    fflush(stdout);
    identifier->entry = js_scope_lookup(tp, identifier->name);
    
    if (identifier->entry) {
        // printf("DEBUG: Found identifier in scope, entry->node: %p\n", identifier->entry->node);
        // printf("DEBUG: Entry node type: %p\n", identifier->entry->node->type);
        fflush(stdout);
        identifier->base.type = identifier->entry->node->type;
        // printf("DEBUG: Set identifier type to: %p\n", identifier->base.type);
        fflush(stdout);
    } else {
        // printf("DEBUG: Identifier not found in scope, using TYPE_ANY\n");
        fflush(stdout);
        // Undefined identifier - could be global or error
        identifier->base.type = &TYPE_ANY;
        log_debug("Undefined identifier: %.*s", (int)identifier->name->len, identifier->name->chars);
    }
    
    return (JsAstNode*)identifier;
}

// Build JavaScript binary expression node
JsAstNode* build_js_binary_expression(JsTranspiler* tp, TSNode binary_node) {
    // printf("DEBUG: build_js_binary_expression called\n");
    fflush(stdout);
    
    // Debug: Print all children of the binary_expression
    uint32_t child_count = ts_node_child_count(binary_node);
    // printf("DEBUG: binary_expression has %u children:\n", child_count);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(binary_node, i);
        const char* child_type = ts_node_type(child);
        // printf("DEBUG:   Child %u: %s\n", i, child_type);
    }
    
    // printf("DEBUG: Allocating binary expression node\n");
    fflush(stdout);
    JsBinaryNode* binary = (JsBinaryNode*)alloc_js_ast_node(tp, JS_AST_NODE_BINARY_EXPRESSION, binary_node, sizeof(JsBinaryNode));
    // printf("DEBUG: Allocated binary expression node: %p\n", binary);
    fflush(stdout);
    
    // Get left operand (child 0)
    // printf("DEBUG: Getting left operand from child 0\n");
    fflush(stdout);
    TSNode left_node = ts_node_child(binary_node, 0);
    // printf("DEBUG: Building left operand\n");
    fflush(stdout);
    binary->left = build_js_expression(tp, left_node);
    // printf("DEBUG: Built left operand successfully\n");
    fflush(stdout);
    
    // Get right operand (child 2)
    // printf("DEBUG: Getting right operand from child 2\n");
    fflush(stdout);
    TSNode right_node = ts_node_child(binary_node, 2);
    // printf("DEBUG: Building right operand\n");
    fflush(stdout);
    binary->right = build_js_expression(tp, right_node);
    // printf("DEBUG: Built right operand successfully\n");
    fflush(stdout);
    
    // Get operator (child 1)
    // printf("DEBUG: Getting operator from child 1\n");
    fflush(stdout);
    TSNode op_node = ts_node_child(binary_node, 1);
    StrView op_source = js_node_source(tp, op_node);
    // printf("DEBUG: Operator source: %.*s\n", (int)op_source.length, op_source.str);
    fflush(stdout);
    binary->op = js_operator_from_string(op_source.str, op_source.length);
    // printf("DEBUG: Parsed operator: %d\n", binary->op);
    fflush(stdout);
    
    // Infer result type based on operator and operands
    // printf("DEBUG: Inferring result type for operator %d\n", binary->op);
    // printf("DEBUG: Left operand type: %p\n", binary->left->type);
    // printf("DEBUG: Right operand type: %p\n", binary->right->type);
    fflush(stdout);
    
    // Check if operands have valid types (simple NULL check for now)
    bool left_type_valid = (binary->left->type != NULL);
    bool right_type_valid = (binary->right->type != NULL);
    
    // printf("DEBUG: Type validity check - Left: %s, Right: %s\n", 
    //        left_type_valid ? "valid" : "null", right_type_valid ? "valid" : "null");
    fflush(stdout);
    
    // For now, always default to FLOAT for arithmetic operations to avoid crashes
    // TODO: Fix the type corruption issue in variable declarators
    // printf("DEBUG: Defaulting to FLOAT type for binary expression (avoiding type corruption)\n");
    fflush(stdout);
    binary->base.type = &TYPE_FLOAT;
    
    if (false) { // Disable the problematic type checking for now
        // printf("DEBUG: Left type_id: %d, Right type_id: %d\n", 
        //        binary->left->type->type_id, binary->right->type->type_id);
        fflush(stdout);
        
        switch (binary->op) {
            case JS_OP_ADD:
                // JavaScript + can be string concatenation or numeric addition
                if ((binary->left->type->type_id == LMD_TYPE_STRING) || 
                    (binary->right->type->type_id == LMD_TYPE_STRING)) {
                    binary->base.type = &TYPE_STRING;
                } else {
                    binary->base.type = &TYPE_FLOAT;
                }
                break;
            case JS_OP_SUB:
            case JS_OP_MUL:
            case JS_OP_DIV:
            case JS_OP_MOD:
            case JS_OP_EXP:
                binary->base.type = &TYPE_FLOAT;
                break;
        case JS_OP_EQ:
        case JS_OP_NE:
        case JS_OP_STRICT_EQ:
        case JS_OP_STRICT_NE:
        case JS_OP_LT:
        case JS_OP_LE:
        case JS_OP_GT:
        case JS_OP_GE:
        case JS_OP_AND:
        case JS_OP_OR:
            binary->base.type = &TYPE_BOOL;
            break;
        case JS_OP_BIT_AND:
        case JS_OP_BIT_OR:
        case JS_OP_BIT_XOR:
        case JS_OP_BIT_LSHIFT:
        case JS_OP_BIT_RSHIFT:
        case JS_OP_BIT_URSHIFT:
            binary->base.type = &TYPE_INT;
            break;
            default:
                binary->base.type = &TYPE_ANY;
                break;
        }
    }
    
    // printf("DEBUG: Binary expression built successfully\n");
    fflush(stdout);
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
            JsAstNode* arg = build_js_expression(tp, arg_node);
            
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
    
    return (JsAstNode*)call;
}

// Build JavaScript member expression node
JsAstNode* build_js_member_expression(JsTranspiler* tp, TSNode member_node) {
    JsMemberNode* member = (JsMemberNode*)alloc_js_ast_node(tp, JS_AST_NODE_MEMBER_EXPRESSION, member_node, sizeof(JsMemberNode));
    
    // Get object
    TSNode object_node = ts_node_child_by_field_name(member_node, "object", strlen("object"));
    member->object = build_js_expression(tp, object_node);
    
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
    
    uint32_t element_count = ts_node_named_child_count(array_node);
    array->length = element_count;
    
    JsAstNode* prev_element = NULL;
    for (uint32_t i = 0; i < element_count; i++) {
        TSNode element_node = ts_node_named_child(array_node, i);
        JsAstNode* element = build_js_expression(tp, element_node);
        
        if (!prev_element) {
            array->elements = element;
        } else {
            prev_element->next = element;
        }
        prev_element = element;
    }
    
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
        
        // Build property node
        JsPropertyNode* property = (JsPropertyNode*)alloc_js_ast_node(tp, JS_AST_NODE_PROPERTY, property_node, sizeof(JsPropertyNode));
        
        // Get key and value
        TSNode key_node = ts_node_child_by_field_name(property_node, "key", strlen("key"));
        TSNode value_node = ts_node_child_by_field_name(property_node, "value", strlen("value"));
        
        property->key = build_js_expression(tp, key_node);
        property->value = build_js_expression(tp, value_node);
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
    const char* node_type = ts_node_type(func_node);
    bool is_arrow = (strcmp(node_type, "arrow_function") == 0);
    bool is_expression = is_arrow || (strcmp(node_type, "function_expression") == 0);
    
    JsAstNodeType ast_type = is_arrow ? JS_AST_NODE_ARROW_FUNCTION :
                             is_expression ? JS_AST_NODE_FUNCTION_EXPRESSION :
                             JS_AST_NODE_FUNCTION_DECLARATION;
    
    JsFunctionNode* func = (JsFunctionNode*)alloc_js_ast_node(tp, ast_type, func_node, sizeof(JsFunctionNode));
    
    func->is_arrow = is_arrow;
    func->is_async = false; // TODO: Check for async keyword
    func->is_generator = false; // TODO: Check for generator
    
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
            JsAstNode* param = build_js_identifier(tp, param_node);
            
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
            func->params = build_js_identifier(tp, param_node);
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
            log_error("Failed to build function body");
            return NULL;
        }
    }
    
    func->base.type = &TYPE_FUNC;
    
    // Add function to scope if it has a name
    if (func->name) {
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
    // // printf("DEBUG: build_js_variable_declaration called\n");
    JsVariableDeclarationNode* var_decl = (JsVariableDeclarationNode*)alloc_js_ast_node(tp, JS_AST_NODE_VARIABLE_DECLARATION, var_node, sizeof(JsVariableDeclarationNode));
    
    // Determine variable kind (var, let, const)
    TSNode first_child = ts_node_child(var_node, 0);
    StrView kind_source = js_node_source(tp, first_child);
    
    // // printf("DEBUG: Variable kind: %.*s\n", (int)kind_source.length, kind_source.str);
    
    if (strncmp(kind_source.str, "var", 3) == 0) {
        var_decl->kind = JS_VAR_VAR;
    } else if (strncmp(kind_source.str, "let", 3) == 0) {
        var_decl->kind = JS_VAR_LET;
    } else if (strncmp(kind_source.str, "const", 5) == 0) {
        var_decl->kind = JS_VAR_CONST;
    }
    
    // Build declarators
    uint32_t child_count = ts_node_named_child_count(var_node);
    // // printf("DEBUG: Variable declaration has %u named children\n", child_count);
    JsAstNode* prev_declarator = NULL;
    
    for (uint32_t i = 0; i < child_count; i++) {
        // // printf("DEBUG: Processing child %u\n", i);
        TSNode declarator_node = ts_node_named_child(var_node, i);
        // // printf("DEBUG: Got declarator node\n");
        const char* declarator_type = ts_node_type(declarator_node);
        // // printf("DEBUG: Declarator type: %s\n", declarator_type);
        
        if (strcmp(declarator_type, "variable_declarator") == 0) {
            log_debug("Found variable_declarator");
            
            // Debug: Print all children of the variable_declarator
            uint32_t declarator_child_count = ts_node_child_count(declarator_node);
            // // printf("DEBUG: variable_declarator has %u children:\n", declarator_child_count);
            for (uint32_t j = 0; j < declarator_child_count; j++) {
                TSNode child = ts_node_child(declarator_node, j);
                const char* child_type = ts_node_type(child);
                // // printf("DEBUG:   Child %u: %s\n", j, child_type);
            }
            
            // // printf("DEBUG: About to call alloc_js_ast_node\n");
            fflush(stdout);
            JsVariableDeclaratorNode* declarator = (JsVariableDeclaratorNode*)alloc_js_ast_node(tp, JS_AST_NODE_VARIABLE_DECLARATOR, declarator_node, sizeof(JsVariableDeclaratorNode));
            // // printf("DEBUG: alloc_js_ast_node returned: %p\n", declarator);
            fflush(stdout);
            
            // Get identifier (child 0)
            // // printf("DEBUG: Getting identifier from child 0\n");
            fflush(stdout);
            TSNode id_node = ts_node_child(declarator_node, 0);
            // // printf("DEBUG: Got identifier node, is_null: %s\n", ts_node_is_null(id_node) ? "true" : "false");
            fflush(stdout);
            
            if (!ts_node_is_null(id_node)) {
                // // printf("DEBUG: Building identifier from node\n");
                fflush(stdout);
                declarator->id = build_js_identifier(tp, id_node);
                // // printf("DEBUG: Built identifier successfully\n");
                fflush(stdout);
            } else {
                // // printf("DEBUG: Identifier node is null!\n");
                declarator->id = NULL;
            }
            
            // Get initializer (child 2, if it exists)
            TSNode init_node;
            bool has_initializer = false;
            if (declarator_child_count >= 3) {
                init_node = ts_node_child(declarator_node, 2);
                has_initializer = !ts_node_is_null(init_node);
                // printf("DEBUG: Got initializer node from child 2, has_initializer: %s\n", has_initializer ? "true" : "false");
            } else {
                // printf("DEBUG: No initializer (child count: %u)\n", declarator_child_count);
                has_initializer = false;
            }
            if (has_initializer) {
                // printf("DEBUG: Building initializer expression\n");
                fflush(stdout);
                declarator->init = build_js_expression(tp, init_node);
                declarator->base.type = declarator->init->type;
                // printf("DEBUG: Built initializer successfully\n");
                fflush(stdout);
            } else {
                // printf("DEBUG: No initializer, setting to NULL\n");
                declarator->init = NULL;
                declarator->base.type = &TYPE_NULL; // undefined
            }
            
            // Add to scope
            JsIdentifierNode* id = (JsIdentifierNode*)declarator->id;
            js_scope_define(tp, id->name, (JsAstNode*)declarator, (JsVarKind)var_decl->kind);
            
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
    
    if (strcmp(node_type, "identifier") == 0 || strcmp(node_type, "property_identifier") == 0) {
        return build_js_identifier(tp, expr_node);
    } else if (strcmp(node_type, "this") == 0) {
        // Handle 'this' keyword
        JsIdentifierNode* this_node = (JsIdentifierNode*)alloc_js_ast_node(tp, JS_AST_NODE_IDENTIFIER, expr_node, sizeof(JsIdentifierNode));
        this_node->name = name_pool_create_len(tp->name_pool, "this", 4);
        this_node->base.type = &TYPE_ANY;
        return (JsAstNode*)this_node;
    } else if (strcmp(node_type, "number") == 0 || strcmp(node_type, "string") == 0 || 
               strcmp(node_type, "true") == 0 || strcmp(node_type, "false") == 0 ||
               strcmp(node_type, "null") == 0 || strcmp(node_type, "undefined") == 0) {
        return build_js_literal(tp, expr_node);
    } else if (strcmp(node_type, "binary_expression") == 0) {
        return build_js_binary_expression(tp, expr_node);
    } else if (strcmp(node_type, "unary_expression") == 0) {
        return build_js_unary_expression(tp, expr_node);
    } else if (strcmp(node_type, "call_expression") == 0 || strcmp(node_type, "new_expression") == 0) {
        return build_js_call_expression(tp, expr_node);
    } else if (strcmp(node_type, "member_expression") == 0 || strcmp(node_type, "subscript_expression") == 0) {
        return build_js_member_expression(tp, expr_node);
    } else if (strcmp(node_type, "array") == 0) {
        return build_js_array_expression(tp, expr_node);
    } else if (strcmp(node_type, "object") == 0) {
        return build_js_object_expression(tp, expr_node);
    } else if (strcmp(node_type, "function_expression") == 0 || strcmp(node_type, "arrow_function") == 0) {
        return build_js_function(tp, expr_node);
    } else if (strcmp(node_type, "assignment_expression") == 0) {
        // Handle assignment expressions
        JsAssignmentNode* assign = (JsAssignmentNode*)alloc_js_ast_node(tp, JS_AST_NODE_ASSIGNMENT_EXPRESSION, expr_node, sizeof(JsAssignmentNode));
        
        // Get left and right operands
        TSNode left_node = ts_node_child_by_field_name(expr_node, "left", strlen("left"));
        TSNode right_node = ts_node_child_by_field_name(expr_node, "right", strlen("right"));
        
        assign->left = build_js_expression(tp, left_node);
        assign->right = build_js_expression(tp, right_node);
        assign->op = JS_OP_ASSIGN; // TODO: Parse actual operator
        assign->base.type = assign->right ? assign->right->type : &TYPE_ANY;
        
        return (JsAstNode*)assign;
    } else if (strcmp(node_type, "parenthesized_expression") == 0) {
        // Handle parenthesized expressions - just return the inner expression
        TSNode inner_node = ts_node_named_child(expr_node, 0);
        return build_js_expression(tp, inner_node);
    } else if (strcmp(node_type, "ternary_expression") == 0) {
        JsConditionalNode* cond = (JsConditionalNode*)alloc_js_ast_node(tp, JS_AST_NODE_CONDITIONAL_EXPRESSION, expr_node, sizeof(JsConditionalNode));
        
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
    } else if (strcmp(node_type, "function_declaration") == 0) {
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
        JsAstNode* break_stmt = alloc_js_ast_node(tp, JS_AST_NODE_BREAK_STATEMENT, stmt_node, sizeof(JsAstNode));
        break_stmt->type = &TYPE_NULL;
        return break_stmt;
    } else if (strcmp(node_type, "continue_statement") == 0) {
        JsAstNode* continue_stmt = alloc_js_ast_node(tp, JS_AST_NODE_CONTINUE_STATEMENT, stmt_node, sizeof(JsAstNode));
        continue_stmt->type = &TYPE_NULL;
        return continue_stmt;
    } else if (strcmp(node_type, "try_statement") == 0) {
        return build_js_try_statement(tp, stmt_node);
    } else if (strcmp(node_type, "throw_statement") == 0) {
        return build_js_throw_statement(tp, stmt_node);
    } else if (strcmp(node_type, "class_declaration") == 0) {
        return build_js_class_declaration(tp, stmt_node);
    } else if (strcmp(node_type, "else_clause") == 0) {
        // Handle else clause - return the statement inside
        TSNode inner_node = ts_node_named_child(stmt_node, 0);
        return build_js_statement(tp, inner_node);
    } else if (strcmp(node_type, "expression_statement") == 0) {
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
        log_error("Unsupported JavaScript statement type: %s", node_type);
        return NULL;
    }
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
            prev_stmt = stmt;
        }
    }
    
    program->base.type = &TYPE_ANY;
    
    return (JsAstNode*)program;
}

// Build JavaScript template literal node
JsAstNode* build_js_template_literal(JsTranspiler* tp, TSNode template_node) {
    JsTemplateLiteralNode* template_lit = (JsTemplateLiteralNode*)alloc_js_ast_node(tp, JS_AST_NODE_TEMPLATE_LITERAL, template_node, sizeof(JsTemplateLiteralNode));
    
    uint32_t child_count = ts_node_named_child_count(template_node);
    JsAstNode* prev_quasi = NULL;
    JsAstNode* prev_expr = NULL;
    
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(template_node, i);
        TSSymbol symbol = ts_node_symbol(child);
        
        const char* child_type = ts_node_type(child);
        
        if (strcmp(child_type, "string_fragment") == 0 || symbol == sym_template_chars) {
            // Template string part
            JsTemplateElementNode* element = (JsTemplateElementNode*)alloc_js_ast_node(tp, JS_AST_NODE_TEMPLATE_ELEMENT, child, sizeof(JsTemplateElementNode));
            
            StrView source = js_node_source(tp, child);
            element->raw = name_pool_create_strview(tp->name_pool, source);
            element->cooked = element->raw; // TODO: Process escape sequences
            element->tail = (i == child_count - 1);
            element->base.type = &TYPE_STRING;
            
            if (!prev_quasi) {
                template_lit->quasis = (JsAstNode*)element;
            } else {
                prev_quasi->next = (JsAstNode*)element;
            }
            prev_quasi = (JsAstNode*)element;
        } else if (strcmp(child_type, "template_substitution") == 0) {
            // Template substitution - extract the expression inside
            TSNode expr_node = ts_node_named_child(child, 0);
            JsAstNode* expr = build_js_expression(tp, expr_node);
            if (expr) {
                if (!prev_expr) {
                    template_lit->expressions = expr;
                } else {
                    prev_expr->next = expr;
                }
                prev_expr = expr;
            }
        } else {
            // Other template expression
            JsAstNode* expr = build_js_expression(tp, child);
            if (expr) {
                if (!prev_expr) {
                    template_lit->expressions = expr;
                } else {
                    prev_expr->next = expr;
                }
                prev_expr = expr;
            }
        }
    }
    
    template_lit->base.type = &TYPE_STRING;
    return (JsAstNode*)template_lit;
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
    JsClassNode* class_decl = (JsClassNode*)alloc_js_ast_node(tp, JS_AST_NODE_CLASS_DECLARATION, class_node, sizeof(JsClassNode));
    
    // Get class name
    TSNode name_node = ts_node_child_by_field_name(class_node, "name", strlen("name"));
    if (!ts_node_is_null(name_node)) {
        StrView name_source = js_node_source(tp, name_node);
        class_decl->name = name_pool_create_strview(tp->name_pool, name_source);
    }
    
    // Get superclass (optional)
    TSNode superclass_node = ts_node_child_by_field_name(class_node, "superclass", strlen("superclass"));
    if (!ts_node_is_null(superclass_node)) {
        class_decl->superclass = build_js_expression(tp, superclass_node);
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

// Build JavaScript class body
JsAstNode* build_js_class_body(JsTranspiler* tp, TSNode body_node) {
    JsBlockNode* body = (JsBlockNode*)alloc_js_ast_node(tp, JS_AST_NODE_BLOCK_STATEMENT, body_node, sizeof(JsBlockNode));
    
    uint32_t method_count = ts_node_named_child_count(body_node);
    JsAstNode* prev_method = NULL;
    
    for (uint32_t i = 0; i < method_count; i++) {
        TSNode method_node = ts_node_named_child(body_node, i);
        JsAstNode* method = build_js_method_definition(tp, method_node);
        
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
    
    // Get method key
    TSNode key_node = ts_node_child_by_field_name(method_node, "name", strlen("name"));
    if (!ts_node_is_null(key_node)) {
        method->key = build_js_expression(tp, key_node);
    }
    
    // Get method value (function)
    TSNode value_node = ts_node_child_by_field_name(method_node, "value", strlen("value"));
    if (!ts_node_is_null(value_node)) {
        method->value = build_js_function(tp, value_node);
    }
    
    // TODO: Parse method modifiers (constructor, getter, setter, static)
    
    method->base.type = &TYPE_FUNC;
    return (JsAstNode*)method;
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
