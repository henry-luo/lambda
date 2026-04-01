#pragma once

// ts_transpiler.hpp — TypeScript transpiler context and entry point
//
// The TsTranspiler extends the JS transpiler with a type registry
// and type-awareness for TS annotations.

#include "ts_ast.hpp"
#include "../js/js_transpiler.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct TsTranspiler TsTranspiler;
typedef struct TsTypeRegistry TsTypeRegistry;

// Type registry entry: maps a type name to a resolved Lambda Type*
typedef struct TsTypeRegistryEntry {
    char name[128];
    Type* type;
} TsTypeRegistryEntry;

// TypeScript transpiler context
typedef struct TsTranspiler {
    // core transpiler components (mirrors JsTranspiler)
    Pool* ast_pool;                 // AST memory pool
    NamePool* name_pool;            // string interning pool
    StrBuf* code_buf;               // code generation buffer
    StrBuf* func_buf;               // buffer for function definitions
    const char* source;             // TypeScript source code
    size_t source_length;

    // scoping and symbol management
    JsScope* current_scope;
    JsScope* global_scope;

    // compilation state
    bool strict_mode;               // always true for TS
    int function_counter;
    int temp_var_counter;
    int label_counter;
    bool in_expression;

    // error handling
    bool has_errors;
    StrBuf* error_buf;

    // Tree-sitter integration
    TSParser* parser;
    TSTree* tree;

    // runtime integration
    Runtime* runtime;

    // Extension hook: override expression builder for TS node types
    JsAstNode* (*expr_builder_override)(void* tp, TSNode node);

    // --- TS-specific fields ---

    // type registry: name -> Type* (interfaces, aliases, enums)
    struct hashmap* type_registry;

    // mode flags
    bool tsx_mode;                  // true for .tsx files
    bool emit_runtime_checks;      // emit type guard assertions (debug/dev mode)
} TsTranspiler;

// Scope management (reuses JS scope functions)
JsScope* ts_scope_create(TsTranspiler* tp, JsScopeType scope_type, JsScope* parent);
void ts_scope_push(TsTranspiler* tp, JsScope* scope);
void ts_scope_pop(TsTranspiler* tp);
NameEntry* ts_scope_lookup(TsTranspiler* tp, String* name);
void ts_scope_define(TsTranspiler* tp, String* name, JsAstNode* node, JsVarKind kind);

// Type registry functions
void ts_type_registry_init(TsTranspiler* tp);
void ts_type_registry_add(TsTranspiler* tp, const char* name, Type* type);
Type* ts_type_registry_lookup(TsTranspiler* tp, const char* name);

// AST building functions (build_ts_ast.cpp)
JsAstNode* build_ts_ast(TsTranspiler* tp, TSNode root);
JsAstNode* build_ts_program(TsTranspiler* tp, TSNode program_node);
JsAstNode* build_ts_statement(TsTranspiler* tp, TSNode stmt_node);
JsAstNode* build_ts_expression(TsTranspiler* tp, TSNode expr_node);
TsTypeNode* build_ts_type_node(TsTranspiler* tp, TSNode type_node);

// Override hook for build_js_expression — handles TS-specific expression nodes
JsAstNode* ts_expr_override(void* tp, TSNode node);

// Type resolution (ts_type_builder.cpp)
Type* ts_resolve_type(TsTranspiler* tp, TsTypeNode* node);
void ts_resolve_all_types(TsTranspiler* tp, JsAstNode* root);
TypeId ts_predefined_name_to_type_id(const char* name, int len);

// AST utility functions
JsAstNode* alloc_ts_ast_node(TsTranspiler* tp, int node_type, TSNode node, size_t size);

// Error handling functions
void ts_error(TsTranspiler* tp, TSNode node, const char* format, ...);
void ts_warning(TsTranspiler* tp, TSNode node, const char* format, ...);

// Transpiler lifecycle functions
TsTranspiler* ts_transpiler_create(Runtime* runtime);
void ts_transpiler_destroy(TsTranspiler* tp);
bool ts_transpiler_parse(TsTranspiler* tp, const char* source, size_t length);

// Source preprocessing: strip TS type annotations to produce valid JS
// Returns a malloc'd buffer (caller must free). out_len receives the length.
char* ts_preprocess_source(const char* src, size_t len, size_t* out_len);

#ifdef __cplusplus
}
#endif

// Direct MIR transpilation entry point
Item transpile_ts_to_mir(Runtime* runtime, const char* ts_source, const char* filename);
