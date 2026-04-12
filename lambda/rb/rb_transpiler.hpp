// rb_transpiler.hpp — Ruby transpiler context and API declarations
#pragma once

#include "rb_ast.hpp"
#include "../transpiler.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// forward declarations
typedef struct RbTranspiler RbTranspiler;
typedef struct RbScope RbScope;

// Ruby variable kinds
typedef enum RbVarKind {
    RB_VAR_LOCAL,       // local variable
    RB_VAR_IVAR,        // instance variable (@x)
    RB_VAR_CVAR,        // class variable (@@x)
    RB_VAR_GVAR,        // global variable ($x)
    RB_VAR_CONST,       // constant (UPPER or CamelCase)
    RB_VAR_BLOCK,       // block parameter
    RB_VAR_FREE,        // captured from enclosing scope (closure)
    RB_VAR_CELL,        // local but captured by inner block/proc
    RB_VAR_MODULE,      // top-level module variable
} RbVarKind;

// Ruby scope types
typedef enum RbScopeType {
    RB_SCOPE_TOP,       // top-level (main)
    RB_SCOPE_METHOD,    // def method
    RB_SCOPE_CLASS,     // class body
    RB_SCOPE_MODULE,    // module body
    RB_SCOPE_BLOCK,     // block/proc/lambda
} RbScopeType;

// Ruby scope structure
typedef struct RbScope {
    RbScopeType scope_type;
    NameEntry* first;
    NameEntry* last;
    struct RbScope* parent;
    RbMethodDefNode* method;    // associated method (if method scope)
} RbScope;

// Ruby transpiler context
typedef struct RbTranspiler {
    // core transpiler components
    Pool* ast_pool;                 // AST backing pool (for arena chunks)
    Arena* ast_arena;               // AST bump allocator (O(1) alloc, bulk free)
    NamePool* name_pool;            // string interning pool
    StrBuf* code_buf;               // code generation buffer
    const char* source;             // Ruby source code
    size_t source_length;

    // scoping and symbol management
    RbScope* current_scope;
    RbScope* top_scope;

    // compilation state
    int method_counter;
    int temp_var_counter;
    int label_counter;

    // error handling
    bool has_errors;
    StrBuf* error_buf;

    // tree-sitter integration
    TSParser* parser;
    TSTree* tree;

    // runtime integration
    Runtime* runtime;
} RbTranspiler;

// scope management functions
RbScope* rb_scope_create(RbTranspiler* tp, RbScopeType scope_type, RbScope* parent);
void rb_scope_push(RbTranspiler* tp, RbScope* scope);
void rb_scope_pop(RbTranspiler* tp);
NameEntry* rb_scope_lookup(RbTranspiler* tp, String* name);
NameEntry* rb_scope_lookup_current(RbTranspiler* tp, String* name);
void rb_scope_define(RbTranspiler* tp, String* name, RbAstNode* node, RbVarKind kind);

// AST building functions (build_rb_ast.cpp)
RbAstNode* build_rb_ast(RbTranspiler* tp, TSNode root);
RbAstNode* build_rb_statement(RbTranspiler* tp, TSNode stmt_node);
RbAstNode* build_rb_expression(RbTranspiler* tp, TSNode expr_node);

// AST utility functions
RbAstNode* alloc_rb_ast_node(RbTranspiler* tp, RbAstNodeType node_type, TSNode node, size_t size);
RbOperator rb_operator_from_string(const char* op_str, size_t len);

// error handling functions
void rb_error(RbTranspiler* tp, TSNode node, const char* format, ...);
void rb_warning(RbTranspiler* tp, TSNode node, const char* format, ...);

// debug functions
void print_rb_ast_node(RbAstNode* node, int indent);
const char* rb_node_type_name(RbAstNodeType type);
const char* rb_op_name(RbOperator op);

// transpiler lifecycle functions
RbTranspiler* rb_transpiler_create(Runtime* runtime);
void rb_transpiler_destroy(RbTranspiler* tp);
bool rb_transpiler_parse(RbTranspiler* tp, const char* source, size_t length);

// main entry point: transpile Ruby source to MIR and execute
Item transpile_rb_to_mir(Runtime* runtime, const char* rb_source, const char* filename);

#ifdef __cplusplus
}
#endif
