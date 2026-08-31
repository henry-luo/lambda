#pragma once

// ts_transpiler.hpp — TypeScript transpiler declarations
//
// TsTranspiler is now a typedef alias for JsTranspiler. The unified
// JsTranspiler struct contains all fields needed for both JS and TS:
//   - strict_js: true = reject TS syntax (pure JS mode)
//   - type_registry: TS type name → Type* mapping

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

TypeId ts_predefined_name_to_type_id(const char* name, int len);

#ifdef __cplusplus
}
#endif
