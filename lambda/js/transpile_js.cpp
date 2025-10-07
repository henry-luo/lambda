#include "js_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include <stdio.h>
#include <stdarg.h>

// Utility functions for code generation
void write_js_fn_name(StrBuf* buf, JsFunctionNode* func_node, int counter) {
    strbuf_append_str(buf, "_js_");
    if (func_node->name && func_node->name->chars) {
        strbuf_append_str_n(buf, func_node->name->chars, func_node->name->len);
    } else {
        strbuf_append_str(buf, "anon");
        strbuf_append_int(buf, counter);
    }
    strbuf_append_int(buf, ts_node_start_byte(func_node->base.node));
}

void write_js_var_name(StrBuf* buf, String* name) {
    strbuf_append_str(buf, "_js_");
    strbuf_append_str_n(buf, name->chars, name->len);
}

void write_js_temp_var(StrBuf* buf, int counter) {
    strbuf_append_str(buf, "_js_temp");
    strbuf_append_int(buf, counter);
}

String* js_create_temp_var_name(JsTranspiler* tp) {
    char temp_name[64];
    snprintf(temp_name, sizeof(temp_name), "_js_temp%d", tp->temp_var_counter++);
    return name_pool_create_len(tp->name_pool, temp_name, strlen(temp_name));
}

// Boxing function for JavaScript values
void transpile_js_box_item(JsTranspiler* tp, JsAstNode* item) {
    printf("DEBUG: transpile_js_box_item called with item: %p\n", item);
    fflush(stdout);
    
    if (!item) {
        printf("DEBUG: Item is NULL, returning ITEM_NULL\n");
        fflush(stdout);
        log_debug("transpile_js_box_item: NULL item");
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
        return;
    }
    
    printf("DEBUG: Item type: %p, node_type: %d\n", item->type, item->node_type);
    fflush(stdout);
    
    if (!item->type) {
        printf("DEBUG: Item type is NULL, returning ITEM_NULL\n");
        fflush(stdout);
        log_debug("transpile_js_box_item: NULL type");
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
        return;
    }
    
    // Special handling for identifiers to avoid type corruption issues
    if (item->node_type == JS_AST_NODE_IDENTIFIER) {
        printf("DEBUG: Handling identifier node, avoiding type access\n");
        fflush(stdout);
        JsIdentifierNode* id = (JsIdentifierNode*)item;
        if (id->name) {
            strbuf_append_char(tp->code_buf, '_');
            strbuf_append_str(tp->code_buf, "js_");
            strbuf_append_str(tp->code_buf, id->name->chars);
        } else {
            strbuf_append_str(tp->code_buf, "_js_unknown");
        }
        return;
    }
    
    printf("DEBUG: Accessing type_id for type: %p\n", item->type);
    fflush(stdout);
    
    switch (item->type->type_id) {
        case LMD_TYPE_NULL:
            strbuf_append_str(tp->code_buf, "ITEM_NULL");
            break;
        case LMD_TYPE_BOOL:
            strbuf_append_str(tp->code_buf, "b2it(");
            transpile_js_expression(tp, item);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case LMD_TYPE_INT:
            strbuf_append_str(tp->code_buf, "i2it(");
            transpile_js_expression(tp, item);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case LMD_TYPE_FLOAT:
            strbuf_append_str(tp->code_buf, "d2it(");
            transpile_js_expression(tp, item);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case LMD_TYPE_STRING:
            if (item->type->is_literal) {
                strbuf_append_str(tp->code_buf, "const_s2it(");
                // TODO: Handle string constants properly
                strbuf_append_str(tp->code_buf, "0"); // Placeholder
                strbuf_append_char(tp->code_buf, ')');
            } else {
                strbuf_append_str(tp->code_buf, "s2it(");
                transpile_js_expression(tp, item);
                strbuf_append_char(tp->code_buf, ')');
            }
            break;
        case LMD_TYPE_ARRAY:
        case LMD_TYPE_MAP:
        case LMD_TYPE_FUNC:
            strbuf_append_str(tp->code_buf, "(Item)(");
            transpile_js_expression(tp, item);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case LMD_TYPE_ANY:
            transpile_js_expression(tp, item);  // Already boxed
            break;
        default:
            log_debug("Unknown box item type: %d", item->type->type_id);
            strbuf_append_str(tp->code_buf, "ITEM_NULL");
    }
}

// Transpile JavaScript literal
void transpile_js_literal(JsTranspiler* tp, JsLiteralNode* literal_node) {
    switch (literal_node->literal_type) {
        case JS_LITERAL_NUMBER:
            strbuf_append_format(tp->code_buf, "%.17g", literal_node->value.number_value);
            break;
        case JS_LITERAL_STRING:
            strbuf_append_char(tp->code_buf, '"');
            // TODO: Proper string escaping
            strbuf_append_str_n(tp->code_buf, literal_node->value.string_value->chars, 
                               literal_node->value.string_value->len);
            strbuf_append_char(tp->code_buf, '"');
            break;
        case JS_LITERAL_BOOLEAN:
            strbuf_append_str(tp->code_buf, literal_node->value.boolean_value ? "true" : "false");
            break;
        case JS_LITERAL_NULL:
        case JS_LITERAL_UNDEFINED:
            strbuf_append_str(tp->code_buf, "null");
            break;
    }
}

// Transpile JavaScript identifier
void transpile_js_identifier(JsTranspiler* tp, JsIdentifierNode* id_node) {
    write_js_var_name(tp->code_buf, id_node->name);
}

// Transpile JavaScript binary expression
void transpile_js_binary_expression(JsTranspiler* tp, JsBinaryNode* binary_node) {
    switch (binary_node->op) {
        case JS_OP_ADD:
            // JavaScript + operator: string concatenation or numeric addition
            strbuf_append_str(tp->code_buf, "js_add(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_SUB:
            strbuf_append_str(tp->code_buf, "js_subtract(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_MUL:
            strbuf_append_str(tp->code_buf, "js_multiply(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_DIV:
            strbuf_append_str(tp->code_buf, "js_divide(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_MOD:
            strbuf_append_str(tp->code_buf, "js_modulo(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_EXP:
            strbuf_append_str(tp->code_buf, "js_power(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_EQ:
            strbuf_append_str(tp->code_buf, "js_equal(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_NE:
            strbuf_append_str(tp->code_buf, "js_not_equal(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_STRICT_EQ:
            strbuf_append_str(tp->code_buf, "js_strict_equal(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_STRICT_NE:
            strbuf_append_str(tp->code_buf, "js_strict_not_equal(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_LT:
            strbuf_append_str(tp->code_buf, "js_less_than(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_LE:
            strbuf_append_str(tp->code_buf, "js_less_equal(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_GT:
            strbuf_append_str(tp->code_buf, "js_greater_than(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_GE:
            strbuf_append_str(tp->code_buf, "js_greater_equal(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_AND:
            strbuf_append_str(tp->code_buf, "js_logical_and(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_OR:
            strbuf_append_str(tp->code_buf, "js_logical_or(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_BIT_AND:
            strbuf_append_str(tp->code_buf, "js_bitwise_and(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_BIT_OR:
            strbuf_append_str(tp->code_buf, "js_bitwise_or(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_BIT_XOR:
            strbuf_append_str(tp->code_buf, "js_bitwise_xor(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_BIT_LSHIFT:
            strbuf_append_str(tp->code_buf, "js_left_shift(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_BIT_RSHIFT:
            strbuf_append_str(tp->code_buf, "js_right_shift(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_BIT_URSHIFT:
            strbuf_append_str(tp->code_buf, "js_unsigned_right_shift(");
            transpile_js_box_item(tp, binary_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_js_box_item(tp, binary_node->right);
            strbuf_append_char(tp->code_buf, ')');
            break;
        default:
            log_error("Unknown JavaScript binary operator: %d", binary_node->op);
            strbuf_append_str(tp->code_buf, "ITEM_NULL");
    }
}

// Transpile JavaScript unary expression
void transpile_js_unary_expression(JsTranspiler* tp, JsUnaryNode* unary_node) {
    switch (unary_node->op) {
        case JS_OP_NOT:
            strbuf_append_str(tp->code_buf, "js_logical_not(");
            transpile_js_box_item(tp, unary_node->operand);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_BIT_NOT:
            strbuf_append_str(tp->code_buf, "js_bitwise_not(");
            transpile_js_box_item(tp, unary_node->operand);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_TYPEOF:
            strbuf_append_str(tp->code_buf, "js_typeof(");
            transpile_js_box_item(tp, unary_node->operand);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_PLUS:
            strbuf_append_str(tp->code_buf, "js_unary_plus(");
            transpile_js_box_item(tp, unary_node->operand);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_MINUS:
            strbuf_append_str(tp->code_buf, "js_unary_minus(");
            transpile_js_box_item(tp, unary_node->operand);
            strbuf_append_char(tp->code_buf, ')');
            break;
        case JS_OP_INCREMENT:
            strbuf_append_str(tp->code_buf, "js_increment(");
            transpile_js_box_item(tp, unary_node->operand);
            strbuf_append_format(tp->code_buf, ", %s)", unary_node->prefix ? "true" : "false");
            break;
        case JS_OP_DECREMENT:
            strbuf_append_str(tp->code_buf, "js_decrement(");
            transpile_js_box_item(tp, unary_node->operand);
            strbuf_append_format(tp->code_buf, ", %s)", unary_node->prefix ? "true" : "false");
            break;
        case JS_OP_VOID:
            // Evaluate operand for side effects, then return undefined
            strbuf_append_str(tp->code_buf, "(");
            transpile_js_box_item(tp, unary_node->operand);
            strbuf_append_str(tp->code_buf, ", ITEM_NULL)");
            break;
        case JS_OP_DELETE:
            // TODO: Implement property deletion
            strbuf_append_str(tp->code_buf, "b2it(true)"); // Simplified
            break;
        default:
            log_error("Unknown JavaScript unary operator: %d", unary_node->op);
            strbuf_append_str(tp->code_buf, "ITEM_NULL");
    }
}

// Transpile JavaScript call expression
void transpile_js_call_expression(JsTranspiler* tp, JsCallNode* call_node) {
    // Count arguments
    int arg_count = 0;
    JsAstNode* arg = call_node->arguments;
    while (arg) {
        arg_count++;
        arg = arg->next;
    }
    
    // Generate argument array
    if (arg_count > 0) {
        strbuf_append_str(tp->code_buf, "({\n");
        strbuf_append_format(tp->code_buf, "  Item args[%d] = {", arg_count);
        
        arg = call_node->arguments;
        for (int i = 0; i < arg_count; i++) {
            if (i > 0) strbuf_append_str(tp->code_buf, ", ");
            transpile_js_box_item(tp, arg);
            arg = arg->next;
        }
        
        strbuf_append_str(tp->code_buf, "};\n");
        strbuf_append_str(tp->code_buf, "  js_call_function(");
        transpile_js_box_item(tp, call_node->callee);
        strbuf_append_format(tp->code_buf, ", ITEM_NULL, args, %d);\n", arg_count);
        strbuf_append_str(tp->code_buf, "})");
    } else {
        strbuf_append_str(tp->code_buf, "js_call_function(");
        transpile_js_box_item(tp, call_node->callee);
        strbuf_append_str(tp->code_buf, ", ITEM_NULL, NULL, 0)");
    }
}

// Transpile JavaScript member expression
void transpile_js_member_expression(JsTranspiler* tp, JsMemberNode* member_node) {
    strbuf_append_str(tp->code_buf, "js_property_access(");
    transpile_js_box_item(tp, member_node->object);
    strbuf_append_char(tp->code_buf, ',');
    
    if (member_node->computed) {
        // obj[key]
        transpile_js_box_item(tp, member_node->property);
    } else {
        // obj.key - convert identifier to string
        if (member_node->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)member_node->property;
            strbuf_append_str(tp->code_buf, "s2it(\"");
            strbuf_append_str_n(tp->code_buf, id->name->chars, id->name->len);
            strbuf_append_str(tp->code_buf, "\")");
        } else {
            transpile_js_box_item(tp, member_node->property);
        }
    }
    
    strbuf_append_char(tp->code_buf, ')');
}

// Transpile JavaScript array expression
void transpile_js_array_expression(JsTranspiler* tp, JsArrayNode* array_node) {
    strbuf_append_format(tp->code_buf, "({\n  Item arr = js_new_array(%d);\n", array_node->length);
    
    JsAstNode* element = array_node->elements;
    int index = 0;
    while (element) {
        strbuf_append_format(tp->code_buf, "  js_array_set(arr, i2it(%d), ", index);
        transpile_js_box_item(tp, element);
        strbuf_append_str(tp->code_buf, ");\n");
        element = element->next;
        index++;
    }
    
    strbuf_append_str(tp->code_buf, "  arr;\n})");
}

// Transpile JavaScript object expression
void transpile_js_object_expression(JsTranspiler* tp, JsObjectNode* object_node) {
    strbuf_append_str(tp->code_buf, "({\n  Item obj = js_new_object();\n");
    
    JsAstNode* property = object_node->properties;
    while (property) {
        if (property->node_type == JS_AST_NODE_PROPERTY) {
            JsPropertyNode* prop = (JsPropertyNode*)property;
            strbuf_append_str(tp->code_buf, "  js_property_set(obj, ");
            
            // Handle property key
            if (prop->computed) {
                transpile_js_box_item(tp, prop->key);
            } else if (prop->key->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* id = (JsIdentifierNode*)prop->key;
                strbuf_append_str(tp->code_buf, "s2it(\"");
                strbuf_append_str_n(tp->code_buf, id->name->chars, id->name->len);
                strbuf_append_str(tp->code_buf, "\")");
            } else {
                transpile_js_box_item(tp, prop->key);
            }
            
            strbuf_append_str(tp->code_buf, ", ");
            transpile_js_box_item(tp, prop->value);
            strbuf_append_str(tp->code_buf, ");\n");
        }
        property = property->next;
    }
    
    strbuf_append_str(tp->code_buf, "  obj;\n})");
}

// Transpile JavaScript function
void transpile_js_function(JsTranspiler* tp, JsFunctionNode* func_node) {
    // Create function scope
    JsScope* func_scope = js_scope_create(tp, JS_SCOPE_FUNCTION, tp->current_scope);
    func_scope->function = func_node;
    js_scope_push(tp, func_scope);
    
    // Generate function signature
    strbuf_append_str(tp->code_buf, "\nItem ");
    write_js_fn_name(tp->code_buf, func_node, tp->function_counter++);
    strbuf_append_str(tp->code_buf, "(");
    
    // Add parameters
    JsAstNode* param = func_node->params;
    bool first_param = true;
    while (param) {
        if (!first_param) {
            strbuf_append_str(tp->code_buf, ", ");
        }
        strbuf_append_str(tp->code_buf, "Item ");
        if (param->node_type == JS_AST_NODE_IDENTIFIER) {
            write_js_var_name(tp->code_buf, ((JsIdentifierNode*)param)->name);
        }
        first_param = false;
        param = param->next;
    }
    
    strbuf_append_str(tp->code_buf, ") {\n");
    
    // Add parameter declarations to scope
    param = func_node->params;
    while (param) {
        if (param->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)param;
            js_scope_define(tp, id->name, param, JS_VAR_VAR);
        }
        param = param->next;
    }
    
    // Transpile function body
    if (func_node->body) {
        if (func_node->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            // Block statement body
            JsBlockNode* block = (JsBlockNode*)func_node->body;
            JsAstNode* stmt = block->statements;
            while (stmt) {
                transpile_js_statement(tp, stmt);
                stmt = stmt->next;
            }
            // Add implicit return undefined if no explicit return
            strbuf_append_str(tp->code_buf, "\n  return ITEM_NULL;");
        } else {
            // Expression body (arrow function)
            strbuf_append_str(tp->code_buf, "\n  return ");
            transpile_js_box_item(tp, func_node->body);
            strbuf_append_char(tp->code_buf, ';');
        }
    }
    
    strbuf_append_str(tp->code_buf, "\n}\n");
    
    // Pop function scope
    js_scope_pop(tp);
}

// Transpile JavaScript if statement
void transpile_js_if_statement(JsTranspiler* tp, JsIfNode* if_node) {
    strbuf_append_str(tp->code_buf, "\n  if (js_is_truthy(");
    transpile_js_box_item(tp, if_node->test);
    strbuf_append_str(tp->code_buf, ")) {");
    
    // Transpile consequent
    if (if_node->consequent) {
        if (if_node->consequent->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* block = (JsBlockNode*)if_node->consequent;
            JsAstNode* stmt = block->statements;
            while (stmt) {
                transpile_js_statement(tp, stmt);
                stmt = stmt->next;
            }
        } else {
            transpile_js_statement(tp, if_node->consequent);
        }
    }
    
    strbuf_append_str(tp->code_buf, "\n  }");
    
    // Transpile alternate (else)
    if (if_node->alternate) {
        strbuf_append_str(tp->code_buf, " else {");
        
        if (if_node->alternate->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* block = (JsBlockNode*)if_node->alternate;
            JsAstNode* stmt = block->statements;
            while (stmt) {
                transpile_js_statement(tp, stmt);
                stmt = stmt->next;
            }
        } else {
            transpile_js_statement(tp, if_node->alternate);
        }
        
        strbuf_append_str(tp->code_buf, "\n  }");
    }
}

// Transpile JavaScript while statement
void transpile_js_while_statement(JsTranspiler* tp, JsWhileNode* while_node) {
    strbuf_append_str(tp->code_buf, "\n  while (js_is_truthy(");
    transpile_js_box_item(tp, while_node->test);
    strbuf_append_str(tp->code_buf, ")) {");
    
    // Transpile body
    if (while_node->body) {
        if (while_node->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* block = (JsBlockNode*)while_node->body;
            JsAstNode* stmt = block->statements;
            while (stmt) {
                transpile_js_statement(tp, stmt);
                stmt = stmt->next;
            }
        } else {
            transpile_js_statement(tp, while_node->body);
        }
    }
    
    strbuf_append_str(tp->code_buf, "\n  }");
}

// Transpile JavaScript for statement
void transpile_js_for_statement(JsTranspiler* tp, JsForNode* for_node) {
    strbuf_append_str(tp->code_buf, "\n  {"); // Create block scope for loop
    
    // Transpile init
    if (for_node->init) {
        transpile_js_statement(tp, for_node->init);
    }
    
    strbuf_append_str(tp->code_buf, "\n    while (");
    
    // Transpile test condition
    if (for_node->test) {
        strbuf_append_str(tp->code_buf, "js_is_truthy(");
        transpile_js_box_item(tp, for_node->test);
        strbuf_append_char(tp->code_buf, ')');
    } else {
        strbuf_append_str(tp->code_buf, "true"); // Infinite loop if no condition
    }
    
    strbuf_append_str(tp->code_buf, ") {");
    
    // Transpile body
    if (for_node->body) {
        if (for_node->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* block = (JsBlockNode*)for_node->body;
            JsAstNode* stmt = block->statements;
            while (stmt) {
                transpile_js_statement(tp, stmt);
                stmt = stmt->next;
            }
        } else {
            transpile_js_statement(tp, for_node->body);
        }
    }
    
    // Transpile update
    if (for_node->update) {
        strbuf_append_str(tp->code_buf, "\n      ");
        transpile_js_box_item(tp, for_node->update);
        strbuf_append_char(tp->code_buf, ';');
    }
    
    strbuf_append_str(tp->code_buf, "\n    }");
    strbuf_append_str(tp->code_buf, "\n  }"); // Close block scope
}

// Transpile JavaScript return statement
void transpile_js_return_statement(JsTranspiler* tp, JsReturnNode* return_node) {
    strbuf_append_str(tp->code_buf, "\n  return ");
    
    if (return_node->argument) {
        transpile_js_box_item(tp, return_node->argument);
    } else {
        strbuf_append_str(tp->code_buf, "ITEM_NULL"); // return undefined
    }
    
    strbuf_append_char(tp->code_buf, ';');
}

// Transpile JavaScript conditional expression (ternary operator)
void transpile_js_conditional_expression(JsTranspiler* tp, JsConditionalNode* cond_node) {
    strbuf_append_str(tp->code_buf, "(js_is_truthy(");
    transpile_js_box_item(tp, cond_node->test);
    strbuf_append_str(tp->code_buf, ") ? ");
    transpile_js_box_item(tp, cond_node->consequent);
    strbuf_append_str(tp->code_buf, " : ");
    transpile_js_box_item(tp, cond_node->alternate);
    strbuf_append_char(tp->code_buf, ')');
}

// Transpile JavaScript template literal
void transpile_js_template_literal(JsTranspiler* tp, JsTemplateLiteralNode* template_node) {
    strbuf_append_str(tp->code_buf, "({\n");
    strbuf_append_str(tp->code_buf, "  StrBuf* template_buf = strbuf_new();\n");
    
    JsAstNode* quasi = template_node->quasis;
    JsAstNode* expr = template_node->expressions;
    
    while (quasi) {
        if (quasi->node_type == JS_AST_NODE_TEMPLATE_ELEMENT) {
            JsTemplateElementNode* element = (JsTemplateElementNode*)quasi;
            strbuf_append_str(tp->code_buf, "  strbuf_append_str(template_buf, \"");
            // TODO: Proper string escaping
            strbuf_append_str_n(tp->code_buf, element->cooked->chars, element->cooked->len);
            strbuf_append_str(tp->code_buf, "\");\n");
        }
        
        if (expr && !((JsTemplateElementNode*)quasi)->tail) {
            strbuf_append_str(tp->code_buf, "  {\n");
            strbuf_append_str(tp->code_buf, "    Item expr_value = ");
            transpile_js_box_item(tp, expr);
            strbuf_append_str(tp->code_buf, ";\n");
            strbuf_append_str(tp->code_buf, "    Item expr_str = js_to_string(expr_value);\n");
            strbuf_append_str(tp->code_buf, "    String* str = it2s(expr_str);\n");
            strbuf_append_str(tp->code_buf, "    strbuf_append_str_n(template_buf, str->chars, str->len);\n");
            strbuf_append_str(tp->code_buf, "  }\n");
            expr = expr->next;
        }
        
        quasi = quasi->next;
    }
    
    strbuf_append_str(tp->code_buf, "  s2it(strbuf_to_string(template_buf));\n");
    strbuf_append_str(tp->code_buf, "})");
}

// Transpile JavaScript try statement
void transpile_js_try_statement(JsTranspiler* tp, JsTryNode* try_node) {
    // JavaScript try/catch/finally using setjmp/longjmp
    strbuf_append_str(tp->code_buf, "\n  {\n");
    strbuf_append_str(tp->code_buf, "    jmp_buf js_exception_buf;\n");
    strbuf_append_str(tp->code_buf, "    Item js_exception_value = ITEM_NULL;\n");
    strbuf_append_str(tp->code_buf, "    int js_exception_code = setjmp(js_exception_buf);\n");
    strbuf_append_str(tp->code_buf, "    \n");
    strbuf_append_str(tp->code_buf, "    if (js_exception_code == 0) {\n");
    strbuf_append_str(tp->code_buf, "      // Try block\n");
    
    // Transpile try block
    if (try_node->block && try_node->block->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
        JsBlockNode* block = (JsBlockNode*)try_node->block;
        JsAstNode* stmt = block->statements;
        while (stmt) {
            transpile_js_statement(tp, stmt);
            stmt = stmt->next;
        }
    }
    
    strbuf_append_str(tp->code_buf, "\n    } else {\n");
    strbuf_append_str(tp->code_buf, "      // Catch block\n");
    
    // Transpile catch block
    if (try_node->handler && try_node->handler->node_type == JS_AST_NODE_CATCH_CLAUSE) {
        JsCatchNode* catch_clause = (JsCatchNode*)try_node->handler;
        
        // Declare catch parameter if present
        if (catch_clause->param && catch_clause->param->node_type == JS_AST_NODE_IDENTIFIER) {
            strbuf_append_str(tp->code_buf, "      Item ");
            transpile_js_identifier(tp, (JsIdentifierNode*)catch_clause->param);
            strbuf_append_str(tp->code_buf, " = js_exception_value;\n");
        }
        
        // Transpile catch body
        if (catch_clause->body && catch_clause->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* catch_block = (JsBlockNode*)catch_clause->body;
            JsAstNode* stmt = catch_block->statements;
            while (stmt) {
                transpile_js_statement(tp, stmt);
                stmt = stmt->next;
            }
        }
    }
    
    strbuf_append_str(tp->code_buf, "\n    }\n");
    
    // Transpile finally block
    if (try_node->finalizer && try_node->finalizer->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
        strbuf_append_str(tp->code_buf, "    \n");
        strbuf_append_str(tp->code_buf, "    // Finally block\n");
        JsBlockNode* finally_block = (JsBlockNode*)try_node->finalizer;
        JsAstNode* stmt = finally_block->statements;
        while (stmt) {
            transpile_js_statement(tp, stmt);
            stmt = stmt->next;
        }
    }
    
    strbuf_append_str(tp->code_buf, "  }\n");
}

// Transpile JavaScript throw statement
void transpile_js_throw_statement(JsTranspiler* tp, JsThrowNode* throw_node) {
    strbuf_append_str(tp->code_buf, "\n  {\n");
    strbuf_append_str(tp->code_buf, "    js_exception_value = ");
    if (throw_node->argument) {
        transpile_js_box_item(tp, throw_node->argument);
    } else {
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
    }
    strbuf_append_str(tp->code_buf, ";\n");
    strbuf_append_str(tp->code_buf, "    longjmp(js_exception_buf, 1);\n");
    strbuf_append_str(tp->code_buf, "  }\n");
}

// Transpile JavaScript class declaration
void transpile_js_class_declaration(JsTranspiler* tp, JsClassNode* class_node) {
    strbuf_append_str(tp->code_buf, "\n// Class: ");
    if (class_node->name) {
        strbuf_append_str_n(tp->code_buf, class_node->name->chars, class_node->name->len);
    } else {
        strbuf_append_str(tp->code_buf, "Anonymous");
    }
    strbuf_append_str(tp->code_buf, "\n");
    
    // Generate constructor function
    strbuf_append_str(tp->code_buf, "Item ");
    if (class_node->name) {
        write_js_var_name(tp->code_buf, class_node->name);
    } else {
        strbuf_append_str(tp->code_buf, "_js_class");
        strbuf_append_int(tp->code_buf, tp->function_counter++);
    }
    strbuf_append_str(tp->code_buf, "_constructor() {\n");
    strbuf_append_str(tp->code_buf, "  Item instance = js_new_object();\n");
    
    // TODO: Process class methods and add them to prototype
    if (class_node->body && class_node->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
        JsBlockNode* class_body = (JsBlockNode*)class_node->body;
        JsAstNode* method = class_body->statements;
        while (method) {
            if (method->node_type == JS_AST_NODE_METHOD_DEFINITION) {
                JsMethodDefinitionNode* method_def = (JsMethodDefinitionNode*)method;
                
                strbuf_append_str(tp->code_buf, "  // Method: ");
                if (method_def->key && method_def->key->node_type == JS_AST_NODE_IDENTIFIER) {
                    JsIdentifierNode* key = (JsIdentifierNode*)method_def->key;
                    strbuf_append_str_n(tp->code_buf, key->name->chars, key->name->len);
                }
                strbuf_append_str(tp->code_buf, "\n");
                
                // TODO: Add method to instance or prototype
            }
            method = method->next;
        }
    }
    
    strbuf_append_str(tp->code_buf, "  return instance;\n");
    strbuf_append_str(tp->code_buf, "}\n");
}

// Transpile JavaScript variable declaration
void transpile_js_variable_declaration(JsTranspiler* tp, JsVariableDeclarationNode* var_node) {
    printf("DEBUG: transpile_js_variable_declaration called\n");
    JsAstNode* declarator = var_node->declarations;
    
    printf("DEBUG: Variable declaration has declarators: %s\n", declarator ? "YES" : "NO");
    
    while (declarator) {
        printf("DEBUG: Processing declarator with node_type: %d\n", declarator->node_type);
        
        if (declarator->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
            JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)declarator;
            
            printf("DEBUG: Generating variable declaration\n");
            strbuf_append_str(tp->code_buf, "\n  Item ");
            transpile_js_identifier(tp, (JsIdentifierNode*)decl->id);
            
            if (decl->init) {
                printf("DEBUG: Variable has initializer\n");
                strbuf_append_str(tp->code_buf, " = ");
                transpile_js_box_item(tp, decl->init);
            } else {
                printf("DEBUG: Variable has no initializer\n");
                strbuf_append_str(tp->code_buf, " = ITEM_NULL"); // undefined
            }
            
            strbuf_append_char(tp->code_buf, ';');
            printf("DEBUG: Variable declaration generated\n");
        }
        declarator = declarator->next;
    }
    printf("DEBUG: transpile_js_variable_declaration completed\n");
}

// Transpile JavaScript expression
void transpile_js_expression(JsTranspiler* tp, JsAstNode* expr) {
    if (!expr) {
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
        return;
    }
    
    switch (expr->node_type) {
        case JS_AST_NODE_LITERAL:
            transpile_js_literal(tp, (JsLiteralNode*)expr);
            break;
        case JS_AST_NODE_IDENTIFIER:
            transpile_js_identifier(tp, (JsIdentifierNode*)expr);
            break;
        case JS_AST_NODE_BINARY_EXPRESSION:
            transpile_js_binary_expression(tp, (JsBinaryNode*)expr);
            break;
        case JS_AST_NODE_UNARY_EXPRESSION:
            transpile_js_unary_expression(tp, (JsUnaryNode*)expr);
            break;
        case JS_AST_NODE_CALL_EXPRESSION:
            transpile_js_call_expression(tp, (JsCallNode*)expr);
            break;
        case JS_AST_NODE_MEMBER_EXPRESSION:
            transpile_js_member_expression(tp, (JsMemberNode*)expr);
            break;
        case JS_AST_NODE_ARRAY_EXPRESSION:
            transpile_js_array_expression(tp, (JsArrayNode*)expr);
            break;
        case JS_AST_NODE_OBJECT_EXPRESSION:
            transpile_js_object_expression(tp, (JsObjectNode*)expr);
            break;
        case JS_AST_NODE_FUNCTION_EXPRESSION:
        case JS_AST_NODE_ARROW_FUNCTION: {
            // For function expressions, generate function and return function pointer
            JsFunctionNode* func = (JsFunctionNode*)expr;
            transpile_js_function(tp, func);
            
            // Return function as Item
            strbuf_append_str(tp->code_buf, "js_new_function((void*)");
            write_js_fn_name(tp->code_buf, func, tp->function_counter - 1);
            strbuf_append_str(tp->code_buf, ", ");
            
            // Count parameters
            int param_count = 0;
            JsAstNode* param = func->params;
            while (param) {
                param_count++;
                param = param->next;
            }
            
            strbuf_append_int(tp->code_buf, param_count);
            strbuf_append_char(tp->code_buf, ')');
            break;
        }
        case JS_AST_NODE_CONDITIONAL_EXPRESSION:
            transpile_js_conditional_expression(tp, (JsConditionalNode*)expr);
            break;
        case JS_AST_NODE_TEMPLATE_LITERAL:
            transpile_js_template_literal(tp, (JsTemplateLiteralNode*)expr);
            break;
        default:
            log_error("Unsupported JavaScript expression type: %d", expr->node_type);
            strbuf_append_str(tp->code_buf, "ITEM_NULL");
    }
}

// Transpile JavaScript statement
void transpile_js_statement(JsTranspiler* tp, JsAstNode* stmt) {
    if (!stmt) return;
    
    printf("DEBUG: Transpiling statement with node_type: %d (JS_AST_NODE_EXPRESSION_STATEMENT=%d)\n", stmt->node_type, JS_AST_NODE_EXPRESSION_STATEMENT);
    
    switch (stmt->node_type) {
        case JS_AST_NODE_VARIABLE_DECLARATION:
            printf("DEBUG: Handling JS_AST_NODE_VARIABLE_DECLARATION\n");
            printf("DEBUG: Current code buffer length: %zu\n", tp->code_buf->length);
            fflush(stdout);
            transpile_js_variable_declaration(tp, (JsVariableDeclarationNode*)stmt);
            printf("DEBUG: After variable declaration, code buffer length: %zu\n", tp->code_buf->length);
            fflush(stdout);
            break;
        case JS_AST_NODE_FUNCTION_DECLARATION:
            transpile_js_function(tp, (JsFunctionNode*)stmt);
            break;
        case JS_AST_NODE_IF_STATEMENT:
            transpile_js_if_statement(tp, (JsIfNode*)stmt);
            break;
        case JS_AST_NODE_WHILE_STATEMENT:
            transpile_js_while_statement(tp, (JsWhileNode*)stmt);
            break;
        case JS_AST_NODE_FOR_STATEMENT:
            transpile_js_for_statement(tp, (JsForNode*)stmt);
            break;
        case JS_AST_NODE_RETURN_STATEMENT:
            transpile_js_return_statement(tp, (JsReturnNode*)stmt);
            break;
        case JS_AST_NODE_BREAK_STATEMENT:
            strbuf_append_str(tp->code_buf, "\n  break;");
            break;
        case JS_AST_NODE_CONTINUE_STATEMENT:
            strbuf_append_str(tp->code_buf, "\n  continue;");
            break;
        case JS_AST_NODE_BLOCK_STATEMENT: {
            JsBlockNode* block = (JsBlockNode*)stmt;
            strbuf_append_str(tp->code_buf, "\n  {");
            JsAstNode* block_stmt = block->statements;
            while (block_stmt) {
                transpile_js_statement(tp, block_stmt);
                block_stmt = block_stmt->next;
            }
            strbuf_append_str(tp->code_buf, "\n  }");
            break;
        }
        case JS_AST_NODE_TRY_STATEMENT:
            transpile_js_try_statement(tp, (JsTryNode*)stmt);
            break;
        case JS_AST_NODE_THROW_STATEMENT:
            transpile_js_throw_statement(tp, (JsThrowNode*)stmt);
            break;
        case JS_AST_NODE_CLASS_DECLARATION:
            transpile_js_class_declaration(tp, (JsClassNode*)stmt);
            break;
        case JS_AST_NODE_EXPRESSION_STATEMENT: {
            printf("DEBUG: Handling JS_AST_NODE_EXPRESSION_STATEMENT\n");
            JsExpressionStatementNode* expr_stmt = (JsExpressionStatementNode*)stmt;
            printf("DEBUG: Expression statement has expression: %s\n", expr_stmt->expression ? "YES" : "NO");
            if (expr_stmt->expression) {
                printf("DEBUG: Expression node_type: %d\n", expr_stmt->expression->node_type);
                fflush(stdout);
            }
            // For the main result, just generate the expression directly
            printf("DEBUG: About to transpile expression\n");
            fflush(stdout);
            transpile_js_box_item(tp, expr_stmt->expression);
            printf("DEBUG: Expression transpiled successfully\n");
            fflush(stdout);
            break;
        }
        default:
            printf("DEBUG: Unhandled statement type: %d (JS_AST_NODE_VARIABLE_DECLARATION=%d)\n", stmt->node_type, JS_AST_NODE_VARIABLE_DECLARATION);
            log_error("Unsupported JavaScript statement type: %d", stmt->node_type);
    }
}

// Transpile JavaScript AST root
void transpile_js_ast_root(JsTranspiler* tp, JsAstNode* root) {
    if (!root || root->node_type != JS_AST_NODE_PROGRAM) {
        log_error("Expected JavaScript program node");
        return;
    }
    
    JsProgramNode* program = (JsProgramNode*)root;
    
    // Generate C code header
    strbuf_append_str(tp->code_buf, "// Generated JavaScript transpiler code\n");
    strbuf_append_str(tp->code_buf, "#include \"../lambda.h\"\n");
    strbuf_append_str(tp->code_buf, "#include \"js/js_runtime.hpp\"\n\n");
    
    // Generate main function
    strbuf_append_str(tp->code_buf, "Item js_main(Context *ctx) {\n");
    strbuf_append_str(tp->code_buf, "  // Initialize JavaScript global object\n");
    strbuf_append_str(tp->code_buf, "  js_init_global_object();\n\n");
    
    // Transpile all statements first
    JsAstNode* stmt = program->body;
    JsAstNode* last_expr_stmt = NULL;
    bool has_content = false;
    
    printf("DEBUG: Program has body: %s\n", stmt ? "YES" : "NO");
    while (stmt) {
        printf("DEBUG: Processing statement node_type: %d\n", stmt->node_type);
        
        if (stmt->node_type == JS_AST_NODE_EXPRESSION_STATEMENT) {
            // Save the last expression statement for the result
            last_expr_stmt = stmt;
        } else {
            // Generate other statements (like variable declarations)
            transpile_js_statement(tp, stmt);
        }
        
        has_content = true;
        stmt = stmt->next;
    }
    printf("DEBUG: Total statements processed, has_content: %s\n", has_content ? "YES" : "NO");
    
    // Generate the final result assignment
    strbuf_append_str(tp->code_buf, "\n  Item result = ");
    
    if (last_expr_stmt) {
        printf("DEBUG: Generating final result from last expression statement\n");
        printf("DEBUG: Current code buffer before final expression: %.*s\n", (int)tp->code_buf->length, tp->code_buf->str);
        fflush(stdout);
        transpile_js_statement(tp, last_expr_stmt);
        printf("DEBUG: Final expression completed\n");
        fflush(stdout);
    } else if (!has_content) {
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
    } else {
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
    }
    
    strbuf_append_str(tp->code_buf, ";\n\n");
    strbuf_append_str(tp->code_buf, "  return result;\n");
    strbuf_append_str(tp->code_buf, "}\n");
}
