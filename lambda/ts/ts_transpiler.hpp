#pragma once

// ts_transpiler.hpp — TypeScript transpiler declarations
//
// TsTranspiler is now a typedef alias for JsTranspiler. The unified
// JsTranspiler struct contains all fields needed for both JS and TS:
//   - strict_js: true = reject TS syntax (pure JS mode)
//   - type_registry: TS type name → Type* mapping
//   - emit_runtime_checks: emit assertion calls

#include "ts_ast.hpp"
#include "../js/js_transpiler.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// TsTranspiler is now JsTranspiler — the struct has been unified
typedef JsTranspiler TsTranspiler;

// Type registry entry: maps a type name to a resolved Lambda Type*
typedef struct TsTypeRegistryEntry {
    char name[128];
    Type* type;
} TsTypeRegistryEntry;

// Type registry functions
void ts_type_registry_init(TsTranspiler* tp);
void ts_type_registry_add(TsTranspiler* tp, const char* name, Type* type);
Type* ts_type_registry_lookup(TsTranspiler* tp, const char* name);

// Type resolution (ts_type_builder.cpp)
Type* ts_resolve_type(TsTranspiler* tp, TsTypeNode* node);
void ts_resolve_all_types(TsTranspiler* tp, JsAstNode* root);
TypeId ts_predefined_name_to_type_id(const char* name, int len);

// Error handling functions (build_js_ast.cpp — merged)
void ts_error(TsTranspiler* tp, TSNode node, const char* format, ...);
void ts_warning(TsTranspiler* tp, TSNode node, const char* format, ...);

// Source preprocessing: strip TS type annotations to produce valid JS
// Returns a malloc'd buffer (caller must free). out_len receives the length.
char* ts_preprocess_source(const char* src, size_t len, size_t* out_len);

#ifdef __cplusplus
}
#endif

// Direct MIR transpilation entry point
Item transpile_ts_to_mir(Runtime* runtime, const char* ts_source, const char* filename);
