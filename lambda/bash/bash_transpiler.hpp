#pragma once

#include "bash_ast.hpp"
#include "../transpiler.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct BashTranspiler BashTranspiler;
typedef struct BashScope BashScope;

// Bash variable kinds (compile-time annotation)
typedef enum BashVarKind {
    BASH_VAR_GLOBAL,        // default: global scope
    BASH_VAR_LOCAL,         // declared with `local`
    BASH_VAR_EXPORT,        // declared with `export`
} BashVarKind;

// Bash scope types
typedef enum BashScopeType {
    BASH_SCOPE_GLOBAL,      // top-level script scope
    BASH_SCOPE_FUNCTION,    // function body
    BASH_SCOPE_SUBSHELL,    // ( ... ) subshell scope
} BashScopeType;

// Bash scope structure (compile-time symbol table)
typedef struct BashScope {
    BashScopeType scope_type;
    NameEntry* first;
    NameEntry* last;
    struct BashScope* parent;
    BashFunctionDefNode* function;  // associated function (if function scope)
} BashScope;

// Bash transpiler context
typedef struct BashTranspiler {
    // core transpiler components
    Pool* ast_pool;                 // AST memory pool
    NamePool* name_pool;            // string interning pool
    StrBuf* code_buf;               // MIR code generation buffer
    const char* source;             // Bash source code
    size_t source_length;

    // scoping and symbol management
    BashScope* current_scope;
    BashScope* global_scope;        // top-level scope (Bash = global by default)

    // compilation state
    int function_counter;           // unique function name counter
    int temp_var_counter;           // temporary variable counter
    int label_counter;              // label counter for control flow
    int loop_depth;                 // current loop nesting depth
    bool in_function;               // currently inside a function definition
    bool in_subshell;               // currently inside a subshell

    // error handling
    bool has_errors;
    StrBuf* error_buf;

    // runtime integration
    Runtime* runtime;
} BashTranspiler;

// ============================================================================
// Scope management functions (bash_scope.cpp)
// ============================================================================
BashScope* bash_scope_create(BashTranspiler* tp, BashScopeType scope_type, BashScope* parent);
void bash_ct_scope_push(BashTranspiler* tp, BashScope* scope);
void bash_ct_scope_pop(BashTranspiler* tp);
NameEntry* bash_scope_lookup(BashTranspiler* tp, String* name);
NameEntry* bash_scope_lookup_current(BashTranspiler* tp, String* name);
void bash_scope_define(BashTranspiler* tp, String* name, BashAstNode* node, BashVarKind kind);

// ============================================================================
// AST building functions (build_bash_ast.cpp)
// ============================================================================
BashAstNode* build_bash_ast(BashTranspiler* tp, TSNode root);
BashAstNode* build_bash_program(BashTranspiler* tp, TSNode program_node);
BashAstNode* build_bash_statement(BashTranspiler* tp, TSNode stmt_node);
BashAstNode* build_bash_command(BashTranspiler* tp, TSNode cmd_node);
BashAstNode* build_bash_expression(BashTranspiler* tp, TSNode expr_node);

// ============================================================================
// AST utility functions
// ============================================================================
BashAstNode* alloc_bash_ast_node(BashTranspiler* tp, BashAstNodeType node_type, TSNode node, size_t size);
BashOperator bash_operator_from_string(const char* op_str, size_t len);
BashTestOp bash_test_op_from_string(const char* op_str, size_t len);

// ============================================================================
// Error handling functions (bash_scope.cpp)
// ============================================================================
void bash_error(BashTranspiler* tp, TSNode node, const char* format, ...);
void bash_warning(BashTranspiler* tp, TSNode node, const char* format, ...);

// ============================================================================
// Transpiler lifecycle functions (bash_scope.cpp)
// ============================================================================
BashTranspiler* bash_transpiler_create(Runtime* runtime);
void bash_transpiler_destroy(BashTranspiler* tp);

#ifdef __cplusplus
}
#endif

// ============================================================================
// MIR transpilation entry point (transpile_bash_mir.cpp)
// ============================================================================
Item transpile_bash_to_mir(Runtime* runtime, const char* bash_source, const char* filename);
