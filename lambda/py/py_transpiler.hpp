#pragma once

#include "py_ast.hpp"
#include "../transpiler.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct PyTranspiler PyTranspiler;
typedef struct PyScope PyScope;

// Python variable kinds
typedef enum PyVarKind {
    PY_VAR_LOCAL,       // assigned in current scope
    PY_VAR_GLOBAL,      // declared with `global`
    PY_VAR_NONLOCAL,    // declared with `nonlocal`
    PY_VAR_FREE,        // captured from enclosing scope (closure cell)
    PY_VAR_CELL,        // local but captured by inner function
    PY_VAR_MODULE,      // top-level module variable
} PyVarKind;

// Python scope types
typedef enum PyScopeType {
    PY_SCOPE_MODULE,
    PY_SCOPE_FUNCTION,
    PY_SCOPE_CLASS,
    PY_SCOPE_COMPREHENSION,
} PyScopeType;

// Python scope structure
typedef struct PyScope {
    PyScopeType scope_type;
    NameEntry* first;
    NameEntry* last;
    struct PyScope* parent;
    PyFunctionDefNode* function;    // associated function (if function scope)
} PyScope;

// Python transpiler context
typedef struct PyTranspiler {
    // core transpiler components
    Pool* ast_pool;                 // AST memory pool
    NamePool* name_pool;            // string interning pool
    StrBuf* code_buf;               // code generation buffer
    const char* source;             // Python source code
    size_t source_length;

    // scoping and symbol management
    PyScope* current_scope;
    PyScope* module_scope;

    // compilation state
    int function_counter;
    int temp_var_counter;
    int label_counter;

    // error handling
    bool has_errors;
    StrBuf* error_buf;

    // Tree-sitter integration
    TSParser* parser;
    TSTree* tree;

    // runtime integration
    Runtime* runtime;
} PyTranspiler;

// Scope management functions
PyScope* py_scope_create(PyTranspiler* tp, PyScopeType scope_type, PyScope* parent);
void py_scope_push(PyTranspiler* tp, PyScope* scope);
void py_scope_pop(PyTranspiler* tp);
NameEntry* py_scope_lookup(PyTranspiler* tp, String* name);
NameEntry* py_scope_lookup_current(PyTranspiler* tp, String* name);
void py_scope_define(PyTranspiler* tp, String* name, PyAstNode* node, PyVarKind kind);

// AST building functions (build_py_ast.cpp)
PyAstNode* build_py_ast(PyTranspiler* tp, TSNode root);
PyAstNode* build_py_module(PyTranspiler* tp, TSNode module_node);
PyAstNode* build_py_statement(PyTranspiler* tp, TSNode stmt_node);
PyAstNode* build_py_expression(PyTranspiler* tp, TSNode expr_node);
PyAstNode* build_py_function_def(PyTranspiler* tp, TSNode func_node);

// AST utility functions
PyAstNode* alloc_py_ast_node(PyTranspiler* tp, PyAstNodeType node_type, TSNode node, size_t size);
PyOperator py_operator_from_string(const char* op_str, size_t len);
PyOperator py_augmented_operator_from_string(const char* op_str, size_t len);

// Error handling functions
void py_error(PyTranspiler* tp, TSNode node, const char* format, ...);
void py_warning(PyTranspiler* tp, TSNode node, const char* format, ...);

// Debug functions
void print_py_ast_node(PyAstNode* node, int indent);

// Transpiler lifecycle functions
PyTranspiler* py_transpiler_create(Runtime* runtime);
void py_transpiler_destroy(PyTranspiler* tp);
bool py_transpiler_parse(PyTranspiler* tp, const char* source, size_t length);

#ifdef __cplusplus
}
#endif

// Direct MIR transpilation entry point
Item transpile_py_to_mir(Runtime* runtime, const char* py_source, const char* filename);
