#pragma once

// ts_type_parser.hpp — Recursive descent parser for opaque TS type text
//
// Phase B of external type parser architecture. Parses the raw text captured
// by the external scanner's _ts_type token into proper TsTypeNode* AST trees.

#include "ts_ast.hpp"
#include "../js/js_transpiler.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// from ts_type_builder.cpp
TypeId ts_predefined_name_to_type_id(const char* name, int len);

// parse a type expression from raw text, returning a TsTypeNode* tree
TsTypeNode* ts_parse_type_text(JsTranspiler* tp, const char* text, int len);

#ifdef __cplusplus
}
#endif
