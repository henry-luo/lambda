#pragma once

// ts_type_parser.hpp — direct-parser admitted TS type reducer

#include "ts_ast.hpp"
#include "../js/js_transpiler.hpp"

#ifdef __cplusplus
extern "C" {
#endif

TypeId ts_predefined_name_to_type_id(const char* name, int len);

// Build a Lambda Type* fact from a type span accepted by the C parser.
TsTypeFactNode* ts_parse_type_text(JsTranspiler* tp, const char* text, int len);

#ifdef __cplusplus
}
#endif
