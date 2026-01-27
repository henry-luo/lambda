#pragma once

#include "js_ast.hpp"
#include "../transpiler.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct JsTranspiler JsTranspiler;
typedef struct JsScope JsScope;

// JavaScript variable declaration types
typedef enum JsVarKind {
    JS_VAR_VAR,     // var - function scoped
    JS_VAR_LET,     // let - block scoped
    JS_VAR_CONST    // const - block scoped, immutable
} JsVarKind;

// JavaScript scope types
typedef enum JsScopeType {
    JS_SCOPE_GLOBAL,
    JS_SCOPE_FUNCTION,
    JS_SCOPE_BLOCK,
    JS_SCOPE_MODULE
} JsScopeType;

// JavaScript scope structure
typedef struct JsScope {
    JsScopeType scope_type;
    NameEntry* first;               // First name entry in scope
    NameEntry* last;                // Last name entry in scope
    struct JsScope* parent;         // Parent scope
    bool strict_mode;               // Strict mode flag
    JsFunctionNode* function;       // Associated function (if function scope)
} JsScope;

// JavaScript transpiler context
typedef struct JsTranspiler {
    // Core transpiler components
    Pool* ast_pool;                 // AST memory pool
    NamePool* name_pool;            // String interning pool
    StrBuf* code_buf;               // Generated C code buffer
    StrBuf* func_buf;               // Buffer for function definitions (for nested/expression functions)
    const char* source;             // JavaScript source code
    size_t source_length;           // Source code length
    
    // Scoping and symbol management
    JsScope* current_scope;         // Current lexical scope
    JsScope* global_scope;          // Global scope
    
    // Compilation state
    bool strict_mode;               // Global strict mode
    int function_counter;           // Counter for anonymous functions
    int temp_var_counter;           // Counter for temporary variables
    int label_counter;              // Counter for labels
    bool in_expression;             // True when transpiling inside an expression (for function expressions)
    
    // Error handling
    bool has_errors;                // Error flag
    StrBuf* error_buf;              // Error messages
    
    // Tree-sitter integration
    TSParser* parser;               // Tree-sitter parser
    TSTree* tree;                   // Parse tree
    
    // Runtime integration
    Runtime* runtime;               // Lambda runtime context
} JsTranspiler;

// JavaScript type mapping functions
Type* js_type_to_lambda_type(JsTranspiler* tp, JsAstNode* node);
TypeId infer_js_expression_type(JsTranspiler* tp, JsAstNode* expr);
bool is_js_truthy_type(TypeId type_id);

// Scope management functions
JsScope* js_scope_create(JsTranspiler* tp, JsScopeType scope_type, JsScope* parent);
void js_scope_push(JsTranspiler* tp, JsScope* scope);
void js_scope_pop(JsTranspiler* tp);
NameEntry* js_scope_lookup(JsTranspiler* tp, String* name);
NameEntry* js_scope_lookup_current(JsTranspiler* tp, String* name);
void js_scope_define(JsTranspiler* tp, String* name, JsAstNode* node, JsVarKind kind);

// AST building functions (build_js_ast.cpp)
JsAstNode* build_js_ast(JsTranspiler* tp, TSNode root);
JsAstNode* build_js_program(JsTranspiler* tp, TSNode program_node);
JsAstNode* build_js_statement(JsTranspiler* tp, TSNode stmt_node);
JsAstNode* build_js_expression(JsTranspiler* tp, TSNode expr_node);
JsAstNode* build_js_function(JsTranspiler* tp, TSNode func_node);
JsAstNode* build_js_variable_declaration(JsTranspiler* tp, TSNode var_node);
JsAstNode* build_js_binary_expression(JsTranspiler* tp, TSNode binary_node);
JsAstNode* build_js_unary_expression(JsTranspiler* tp, TSNode unary_node);
JsAstNode* build_js_call_expression(JsTranspiler* tp, TSNode call_node);
JsAstNode* build_js_member_expression(JsTranspiler* tp, TSNode member_node);
JsAstNode* build_js_array_expression(JsTranspiler* tp, TSNode array_node);
JsAstNode* build_js_object_expression(JsTranspiler* tp, TSNode object_node);
JsAstNode* build_js_identifier(JsTranspiler* tp, TSNode id_node);
JsAstNode* build_js_literal(JsTranspiler* tp, TSNode literal_node);

// Code generation functions (transpile_js.cpp)
void transpile_js_ast_root(JsTranspiler* tp, JsAstNode* root);
void transpile_js_statement(JsTranspiler* tp, JsAstNode* stmt);
void transpile_js_expression(JsTranspiler* tp, JsAstNode* expr);
void transpile_js_function(JsTranspiler* tp, JsFunctionNode* func_node);
void transpile_js_variable_declaration(JsTranspiler* tp, JsVariableDeclarationNode* var_node);
void transpile_js_binary_expression(JsTranspiler* tp, JsBinaryNode* binary_node);
void transpile_js_unary_expression(JsTranspiler* tp, JsUnaryNode* unary_node);
void transpile_js_call_expression(JsTranspiler* tp, JsCallNode* call_node);
void transpile_js_member_expression(JsTranspiler* tp, JsMemberNode* member_node);
void transpile_js_array_expression(JsTranspiler* tp, JsArrayNode* array_node);
void transpile_js_object_expression(JsTranspiler* tp, JsObjectNode* object_node);
void transpile_js_identifier(JsTranspiler* tp, JsIdentifierNode* id_node);
void transpile_js_literal(JsTranspiler* tp, JsLiteralNode* literal_node);

// Boxing and type conversion functions
void transpile_js_box_item(JsTranspiler* tp, JsAstNode* item);
void write_js_type_conversion(JsTranspiler* tp, JsAstNode* expr, TypeId target_type);

// Utility functions
JsAstNode* alloc_js_ast_node(JsTranspiler* tp, JsAstNodeType node_type, TSNode node, size_t size);
JsOperator js_operator_from_string(const char* op_str, size_t len);
const char* js_operator_to_string(JsOperator op);
void write_js_fn_name(StrBuf* buf, JsFunctionNode* func_node, int counter);
void write_js_var_name(StrBuf* buf, String* name);
void write_js_temp_var(StrBuf* buf, int counter);
String* js_create_temp_var_name(JsTranspiler* tp);
String* js_create_label_name(JsTranspiler* tp);

// Error handling functions
void js_error(JsTranspiler* tp, TSNode node, const char* format, ...);
void js_warning(JsTranspiler* tp, TSNode node, const char* format, ...);

// Debug functions
void print_js_ast_node(JsAstNode* node, int indent);

// Transpiler lifecycle functions
JsTranspiler* js_transpiler_create(Runtime* runtime);
void js_transpiler_destroy(JsTranspiler* tp);
bool js_transpiler_parse(JsTranspiler* tp, const char* source, size_t length);
Item js_transpiler_compile(JsTranspiler* tp, Runtime* runtime);

#ifdef __cplusplus
}
#endif

// Main entry point (C++ linkage)
Item transpile_js_to_c(Runtime* runtime, const char* js_source, const char* filename);

// JavaScript runtime function declarations (js_runtime.cpp)
#ifdef __cplusplus
extern "C" {
#endif

// JavaScript runtime functions
Item js_typeof(Item value);
Item js_add(Item left, Item right);
Item js_subtract(Item left, Item right);
Item js_multiply(Item left, Item right);
Item js_divide(Item left, Item right);
Item js_modulo(Item left, Item right);
Item js_power(Item left, Item right);

// Comparison operators
Item js_equal(Item left, Item right);
Item js_not_equal(Item left, Item right);
Item js_strict_equal(Item left, Item right);
Item js_strict_not_equal(Item left, Item right);
Item js_less_than(Item left, Item right);
Item js_less_equal(Item left, Item right);
Item js_greater_than(Item left, Item right);
Item js_greater_equal(Item left, Item right);

// Logical operators
Item js_logical_and(Item left, Item right);
Item js_logical_or(Item left, Item right);
Item js_logical_not(Item operand);

// Bitwise operators
Item js_bitwise_and(Item left, Item right);
Item js_bitwise_or(Item left, Item right);
Item js_bitwise_xor(Item left, Item right);
Item js_bitwise_not(Item operand);
Item js_left_shift(Item left, Item right);
Item js_right_shift(Item left, Item right);
Item js_unsigned_right_shift(Item left, Item right);

// Unary operators
Item js_unary_plus(Item operand);
Item js_unary_minus(Item operand);
Item js_increment(Item operand, bool prefix);
Item js_decrement(Item operand, bool prefix);

// Type conversion functions
Item js_to_primitive(Item value, const char* hint);
Item js_to_number(Item value);
Item js_to_string(Item value);
Item js_to_boolean(Item value);
Item js_to_object(Item value);
bool js_is_truthy(Item value);

// Object and property functions
Item js_new_object();
Item js_new_array(int length);
Item js_property_access(Item object, Item key);
Item js_property_set(Item object, Item key, Item value);
Item js_property_delete(Item object, Item key);
bool js_property_has(Item object, Item key);

// Function call functions
Item js_call_function(Item func, Item this_binding, Item* args, int arg_count);
Item js_new_function(void* func_ptr, int param_count);

// Array functions
Item js_array_get(Item array, Item index);
Item js_array_set(Item array, Item index, Item value);
int js_array_length(Item array);
Item js_array_push(Item array, Item value);
Item js_array_pop(Item array);

// Prototype and inheritance
Item js_prototype_lookup(Item object, Item property);
Item js_get_prototype(Item object);
void js_set_prototype(Item object, Item prototype);

// Global object and built-ins
Item js_get_global();
void js_init_global_object();

#ifdef __cplusplus
}
#endif
