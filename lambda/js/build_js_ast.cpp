#include "js_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <string.h>
#include <stdio.h>

// External Tree-sitter JavaScript parser
extern "C" {
    const TSLanguage *tree_sitter_javascript(void);
}

// Utility function to get Tree-sitter node source
#define js_node_source(transpiler, node) {.str = (transpiler)->source + ts_node_start_byte(node), \
     .length = ts_node_end_byte(node) - ts_node_start_byte(node) }

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
    TSSymbol symbol = ts_node_symbol(literal_node);
    JsLiteralNode* literal = (JsLiteralNode*)alloc_js_ast_node(tp, JS_AST_NODE_LITERAL, literal_node, sizeof(JsLiteralNode));
    
    StrView source = js_node_source(tp, literal_node);
    
    if (symbol == JS_SYM_NUMBER) {
        literal->literal_type = JS_LITERAL_NUMBER;
        char* endptr;
        literal->value.number_value = strtod(source.str, &endptr);
        literal->base.type = &TYPE_FLOAT; // All JS numbers are float64
    } else if (symbol == JS_SYM_STRING) {
        literal->literal_type = JS_LITERAL_STRING;
        // Remove quotes and handle escape sequences
        String* str_val = name_pool_create_string(tp->name_pool, source.str + 1, source.length - 2);
        literal->value.string_value = str_val;
        literal->base.type = &TYPE_STRING;
    } else if (symbol == JS_SYM_TRUE) {
        literal->literal_type = JS_LITERAL_BOOLEAN;
        literal->value.boolean_value = true;
        literal->base.type = &TYPE_BOOL;
    } else if (symbol == JS_SYM_FALSE) {
        literal->literal_type = JS_LITERAL_BOOLEAN;
        literal->value.boolean_value = false;
        literal->base.type = &TYPE_BOOL;
    } else if (symbol == JS_SYM_NULL) {
        literal->literal_type = JS_LITERAL_NULL;
        literal->base.type = &TYPE_NULL;
    } else if (symbol == JS_SYM_UNDEFINED) {
        literal->literal_type = JS_LITERAL_UNDEFINED;
        literal->base.type = &TYPE_NULL; // Map undefined to null in Lambda
    }
    
    return (JsAstNode*)literal;
}

// Build JavaScript identifier node
JsAstNode* build_js_identifier(JsTranspiler* tp, TSNode id_node) {
    JsIdentifierNode* identifier = (JsIdentifierNode*)alloc_js_ast_node(tp, JS_AST_NODE_IDENTIFIER, id_node, sizeof(JsIdentifierNode));
    
    StrView source = js_node_source(tp, id_node);
    identifier->name = name_pool_create_strview(tp->name_pool, source);
    
    // Look up in symbol table
    identifier->entry = js_scope_lookup(tp, identifier->name);
    
    if (identifier->entry) {
        identifier->base.type = identifier->entry->node->type;
    } else {
        // Undefined identifier - could be global or error
        identifier->base.type = &TYPE_ANY;
        log_debug("Undefined identifier: %.*s", (int)identifier->name->len, identifier->name->chars);
    }
    
    return (JsAstNode*)identifier;
}

// Build JavaScript binary expression node
JsAstNode* build_js_binary_expression(JsTranspiler* tp, TSNode binary_node) {
    JsBinaryNode* binary = (JsBinaryNode*)alloc_js_ast_node(tp, JS_AST_NODE_BINARY_EXPRESSION, binary_node, sizeof(JsBinaryNode));
    
    // Get left operand
    TSNode left_node = ts_node_child_by_field_id(binary_node, JS_FIELD_LEFT);
    binary->left = build_js_expression(tp, left_node);
    
    // Get right operand
    TSNode right_node = ts_node_child_by_field_id(binary_node, JS_FIELD_RIGHT);
    binary->right = build_js_expression(tp, right_node);
    
    // Get operator
    TSNode op_node = ts_node_child_by_field_id(binary_node, JS_FIELD_OPERATOR);
    StrView op_source = js_node_source(tp, op_node);
    binary->op = js_operator_from_string(op_source.str, op_source.length);
    
    // Infer result type based on operator and operands
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
    }
    
    return (JsAstNode*)binary;
}

// Build JavaScript unary expression node
JsAstNode* build_js_unary_expression(JsTranspiler* tp, TSNode unary_node) {
    JsUnaryNode* unary = (JsUnaryNode*)alloc_js_ast_node(tp, JS_AST_NODE_UNARY_EXPRESSION, unary_node, sizeof(JsUnaryNode));
    
    // Get operand
    TSNode operand_node = ts_node_child_by_field_id(unary_node, JS_FIELD_OPERAND);
    unary->operand = build_js_expression(tp, operand_node);
    
    // Get operator
    TSNode op_node = ts_node_child_by_field_id(unary_node, JS_FIELD_OPERATOR);
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
    
    // Get callee (function being called)
    TSNode callee_node = ts_node_child_by_field_id(call_node, JS_FIELD_FUNCTION);
    call->callee = build_js_expression(tp, callee_node);
    
    // Get arguments
    TSNode args_node = ts_node_child_by_field_id(call_node, JS_FIELD_ARGUMENTS);
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
    TSNode object_node = ts_node_child_by_field_id(member_node, JS_FIELD_OBJECT);
    member->object = build_js_expression(tp, object_node);
    
    // Get property
    TSNode property_node = ts_node_child_by_field_id(member_node, JS_FIELD_PROPERTY);
    member->property = build_js_expression(tp, property_node);
    
    // Determine if computed (obj[prop]) or not (obj.prop)
    TSSymbol symbol = ts_node_symbol(member_node);
    member->computed = (symbol == JS_SYM_SUBSCRIPT_EXPRESSION);
    
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
        TSNode key_node = ts_node_child_by_field_id(property_node, JS_FIELD_NAME);
        TSNode value_node = ts_node_child_by_field_id(property_node, JS_FIELD_VALUE);
        
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
    JsFunctionNode* func = (JsFunctionNode*)alloc_js_ast_node(tp, JS_AST_NODE_FUNCTION_DECLARATION, func_node, sizeof(JsFunctionNode));
    
    TSSymbol symbol = ts_node_symbol(func_node);
    func->is_arrow = (symbol == JS_SYM_ARROW_FUNCTION);
    func->is_async = false; // TODO: Check for async keyword
    func->is_generator = false; // TODO: Check for generator
    
    // Get function name (optional for expressions)
    TSNode name_node = ts_node_child_by_field_id(func_node, JS_FIELD_NAME);
    if (!ts_node_is_null(name_node)) {
        StrView name_source = js_node_source(tp, name_node);
        func->name = name_pool_create_strview(tp->name_pool, name_source);
    }
    
    // Get parameters
    TSNode params_node = ts_node_child_by_field_id(func_node, JS_FIELD_PARAMETERS);
    if (!ts_node_is_null(params_node)) {
        uint32_t param_count = ts_node_named_child_count(params_node);
        JsAstNode* prev_param = NULL;
        
        for (uint32_t i = 0; i < param_count; i++) {
            TSNode param_node = ts_node_named_child(params_node, i);
            JsAstNode* param = build_js_identifier(tp, param_node);
            
            if (!prev_param) {
                func->params = param;
            } else {
                prev_param->next = param;
            }
            prev_param = param;
        }
    }
    
    // Get function body
    TSNode body_node = ts_node_child_by_field_id(func_node, JS_FIELD_BODY);
    if (!ts_node_is_null(body_node)) {
        TSSymbol body_symbol = ts_node_symbol(body_node);
        if (body_symbol == JS_SYM_BLOCK_STATEMENT) {
            func->body = build_js_block_statement(tp, body_node);
        } else {
            // Arrow function with expression body
            func->body = build_js_expression(tp, body_node);
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
    TSNode test_node = ts_node_child_by_field_id(if_node, JS_FIELD_CONDITION);
    if (!ts_node_is_null(test_node)) {
        if_stmt->test = build_js_expression(tp, test_node);
    }
    
    // Get consequent (then branch)
    TSNode consequent_node = ts_node_child_by_field_id(if_node, JS_FIELD_CONSEQUENCE);
    if (!ts_node_is_null(consequent_node)) {
        if_stmt->consequent = build_js_statement(tp, consequent_node);
    }
    
    // Get alternate (else branch) - optional
    TSNode alternate_node = ts_node_child_by_field_id(if_node, JS_FIELD_ALTERNATIVE);
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
    TSNode test_node = ts_node_child_by_field_id(while_node, JS_FIELD_CONDITION);
    if (!ts_node_is_null(test_node)) {
        while_stmt->test = build_js_expression(tp, test_node);
    }
    
    // Get body
    TSNode body_node = ts_node_child_by_field_id(while_node, JS_FIELD_BODY);
    if (!ts_node_is_null(body_node)) {
        while_stmt->body = build_js_statement(tp, body_node);
    }
    
    while_stmt->base.type = &TYPE_NULL;
    
    return (JsAstNode*)while_stmt;
}

// Build JavaScript for statement node
JsAstNode* build_js_for_statement(JsTranspiler* tp, TSNode for_node) {
    JsForNode* for_stmt = (JsForNode*)alloc_js_ast_node(tp, JS_AST_NODE_FOR_STATEMENT, for_node, sizeof(JsForNode));
    
    // Get init (optional)
    TSNode init_node = ts_node_child_by_field_name(for_node, "init", 4);
    if (!ts_node_is_null(init_node)) {
        for_stmt->init = build_js_statement(tp, init_node);
    }
    
    // Get test condition (optional)
    TSNode test_node = ts_node_child_by_field_name(for_node, "condition", 9);
    if (!ts_node_is_null(test_node)) {
        for_stmt->test = build_js_expression(tp, test_node);
    }
    
    // Get update (optional)
    TSNode update_node = ts_node_child_by_field_name(for_node, "update", 6);
    if (!ts_node_is_null(update_node)) {
        for_stmt->update = build_js_expression(tp, update_node);
    }
    
    // Get body
    TSNode body_node = ts_node_child_by_field_id(for_node, JS_FIELD_BODY);
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
        TSSymbol symbol = ts_node_symbol(declarator_node);
        
        if (symbol == sym_variable_declarator) {
            JsVariableDeclaratorNode* declarator = (JsVariableDeclaratorNode*)alloc_js_ast_node(tp, JS_AST_NODE_VARIABLE_DECLARATOR, declarator_node, sizeof(JsVariableDeclaratorNode));
            
            // Get identifier
            TSNode id_node = ts_node_child_by_field_id(declarator_node, JS_FIELD_NAME);
            declarator->id = build_js_identifier(tp, id_node);
            
            // Get initializer (optional)
            TSNode init_node = ts_node_child_by_field_id(declarator_node, JS_FIELD_VALUE);
            if (!ts_node_is_null(init_node)) {
                declarator->init = build_js_expression(tp, init_node);
                declarator->base.type = declarator->init->type;
            } else {
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
    TSSymbol symbol = ts_node_symbol(expr_node);
    
    switch (symbol) {
        case JS_SYM_IDENTIFIER:
            return build_js_identifier(tp, expr_node);
        case JS_SYM_NUMBER:
        case JS_SYM_STRING:
        case JS_SYM_TRUE:
        case JS_SYM_FALSE:
        case JS_SYM_NULL:
        case JS_SYM_UNDEFINED:
            return build_js_literal(tp, expr_node);
        case JS_SYM_BINARY_EXPRESSION:
            return build_js_binary_expression(tp, expr_node);
        case JS_SYM_UNARY_EXPRESSION:
            return build_js_unary_expression(tp, expr_node);
        case JS_SYM_CALL_EXPRESSION:
            return build_js_call_expression(tp, expr_node);
        case JS_SYM_MEMBER_EXPRESSION:
        case JS_SYM_SUBSCRIPT_EXPRESSION:
            return build_js_member_expression(tp, expr_node);
        case JS_SYM_ARRAY_EXPRESSION:
            return build_js_array_expression(tp, expr_node);
        case JS_SYM_OBJECT_EXPRESSION:
            return build_js_object_expression(tp, expr_node);
        case JS_SYM_FUNCTION_EXPRESSION:
        case JS_SYM_ARROW_FUNCTION:
            return build_js_function(tp, expr_node);
        case JS_SYM_CONDITIONAL_EXPRESSION: {
            JsConditionalNode* cond = (JsConditionalNode*)alloc_js_ast_node(tp, JS_AST_NODE_CONDITIONAL_EXPRESSION, expr_node, sizeof(JsConditionalNode));
            
            // Get test condition
            TSNode test_node = ts_node_child_by_field_name(expr_node, "condition", 9);
            if (!ts_node_is_null(test_node)) {
                cond->test = build_js_expression(tp, test_node);
            }
            
            // Get consequent (true branch)
            TSNode consequent_node = ts_node_child_by_field_name(expr_node, "consequence", 11);
            if (!ts_node_is_null(consequent_node)) {
                cond->consequent = build_js_expression(tp, consequent_node);
            }
            
            // Get alternate (false branch)
            TSNode alternate_node = ts_node_child_by_field_name(expr_node, "alternative", 11);
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
        }
        case sym_template_string: {
            return build_js_template_literal(tp, expr_node);
        }
        default:
            log_error("Unsupported JavaScript expression type: %d", symbol);
            return NULL;
    }
}

// Build JavaScript statement from Tree-sitter node
JsAstNode* build_js_statement(JsTranspiler* tp, TSNode stmt_node) {
    TSSymbol symbol = ts_node_symbol(stmt_node);
    
    switch (symbol) {
        case JS_SYM_VARIABLE_DECLARATION:
        case JS_SYM_LEXICAL_DECLARATION:
            return build_js_variable_declaration(tp, stmt_node);
        case JS_SYM_FUNCTION_DECLARATION:
            return build_js_function(tp, stmt_node);
        case JS_SYM_IF_STATEMENT:
            return build_js_if_statement(tp, stmt_node);
        case JS_SYM_WHILE_STATEMENT:
            return build_js_while_statement(tp, stmt_node);
        case JS_SYM_FOR_STATEMENT:
            return build_js_for_statement(tp, stmt_node);
        case JS_SYM_RETURN_STATEMENT:
            return build_js_return_statement(tp, stmt_node);
        case JS_SYM_BLOCK_STATEMENT:
            return build_js_block_statement(tp, stmt_node);
        case JS_SYM_BREAK_STATEMENT: {
            JsAstNode* break_stmt = alloc_js_ast_node(tp, JS_AST_NODE_BREAK_STATEMENT, stmt_node, sizeof(JsAstNode));
            break_stmt->type = &TYPE_NULL;
            return break_stmt;
        }
        case JS_SYM_CONTINUE_STATEMENT: {
            JsAstNode* continue_stmt = alloc_js_ast_node(tp, JS_AST_NODE_CONTINUE_STATEMENT, stmt_node, sizeof(JsAstNode));
            continue_stmt->type = &TYPE_NULL;
            return continue_stmt;
        }
        case sym_try_statement:
            return build_js_try_statement(tp, stmt_node);
        case sym_throw_statement:
            return build_js_throw_statement(tp, stmt_node);
        case sym_class_declaration:
            return build_js_class_declaration(tp, stmt_node);
        case JS_SYM_EXPRESSION_STATEMENT: {
            JsExpressionStatementNode* expr_stmt = (JsExpressionStatementNode*)alloc_js_ast_node(tp, JS_AST_NODE_EXPRESSION_STATEMENT, stmt_node, sizeof(JsExpressionStatementNode));
            TSNode expr_node = ts_node_named_child(stmt_node, 0);
            expr_stmt->expression = build_js_expression(tp, expr_node);
            expr_stmt->base.type = expr_stmt->expression->type;
            return (JsAstNode*)expr_stmt;
        }
        default:
            log_error("Unsupported JavaScript statement type: %d", symbol);
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
        
        if (symbol == sym_template_chars) {
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
        } else {
            // Template expression
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
    TSNode name_node = ts_node_child_by_field_id(class_node, JS_FIELD_NAME);
    if (!ts_node_is_null(name_node)) {
        StrView name_source = js_node_source(tp, name_node);
        class_decl->name = name_pool_create_strview(tp->name_pool, name_source);
    }
    
    // Get superclass (optional)
    TSNode superclass_node = ts_node_child_by_field_name(class_node, "superclass", 10);
    if (!ts_node_is_null(superclass_node)) {
        class_decl->superclass = build_js_expression(tp, superclass_node);
    }
    
    // Get class body
    TSNode body_node = ts_node_child_by_field_id(class_node, JS_FIELD_BODY);
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
    
    // Get method key
    TSNode key_node = ts_node_child_by_field_id(method_node, JS_FIELD_NAME);
    if (!ts_node_is_null(key_node)) {
        method->key = build_js_expression(tp, key_node);
    }
    
    // Get method value (function)
    TSNode value_node = ts_node_child_by_field_id(method_node, JS_FIELD_VALUE);
    if (!ts_node_is_null(value_node)) {
        method->value = build_js_function(tp, value_node);
    }
    
    // Determine method kind
    method->kind = JS_METHOD_METHOD; // Default
    method->computed = false;
    method->static_method = false;
    
    // TODO: Check for constructor, getter, setter, static modifiers
    
    method->base.type = &TYPE_FUNC;
    return (JsAstNode*)method;
}

// Main AST building entry point
JsAstNode* build_js_ast(JsTranspiler* tp, TSNode root) {
    TSSymbol symbol = ts_node_symbol(root);
    
    if (symbol == JS_SYM_PROGRAM) {
        return build_js_program(tp, root);
    } else {
        log_error("Expected program node, got: %d", symbol);
        return NULL;
    }
}
