#pragma once

// Shared Type* construction surface.
//
// These were private to build_ast.cpp's CST-driven builders. The type-pattern
// hand parser (parse_type_pattern.cpp) builds the same `Type*` graphs from
// source text rather than from CST nodes, so the pieces that do not depend on a
// Shared type-construction helpers are promoted here instead of being copied.

#include "ast.hpp"
#include "lambda-error.h"

// Map a base-type keyword (`int`, `string`, `u8`, …) to its literal Type.
// Returns NULL when the name is not a base type — the caller decides whether
// that is a type reference or an error. `any` needs the transpiler because it
// records explicit-any provenance.
Type* lookup_base_type_name(Transpiler* tp, StrView name);

// Parse an occurrence count body — `[]`, `[n]`, `[n, m]`, `[n+]` — into the
// inclusive bounds a TypeUnary carries (max_count -1 means unbounded).
void parse_occurrence_count(StrView op_str, int* min_count, int* max_count);

// Append one field to a map/element shape. The CST path passes an AstNode and
// reads its name/type; this is that core, taking the two values directly.
ShapeEntry* append_shape_entry_typed(Transpiler* tp, String* name, Type* field_type,
        ShapeEntry** shape, ShapeEntry** prev_entry, int byte_offset);

// Fold a declared type into a TypeParam (compact prefix, retained contract,
// full_type selection). Used for `fn(a: T)` params on both the CST and the
// hand-parser path.
void apply_declared_param_type(Transpiler* tp, TypeParam* param_type, Type* declared);

// Declare a fn type's return contract.
void set_fn_return_contract(TypeFunc* fn_type, Type* contract, bool is_explicit);

// Construct the declaration-level return contract wrapper used by the
// external-token parser.
AstNode* build_function_return_contract_node_from_span(Transpiler* tp,
        SourceSpan span, Type* returned, Type* error_type, bool can_raise);

// Construct a registered binary type with raw TypeBinary operands. Return
// contracts use this rather than the general pattern binary constructor.
AstBinaryNode* build_registered_binary_type_from_span(Transpiler* tp,
        SourceSpan span, AstNode* left, AstNode* right, Type* left_type,
        Type* right_type, Operator op, StrView op_str);

// Allocate an AST node. Defined in build_ast.cpp; promoted because pattern
// islands are the one type form whose AST must survive to MIR transpilation,
// so the hand parser has to build real nodes for them.
AstNode* alloc_ast_node_from_span(Transpiler* tp, AstNodeType node_type,
        SourceSpan span, size_t size);

// Evaluate a literal AST node to the Item it denotes (compile-time constants
// only). Used for bracket-type positions and range bounds.
bool ast_static_literal_item(Transpiler* tp, AstNode* node, Item* out);

// True when a pattern body is nothing but literals (and unions of them). Such
// an island is an ordinary literal union rather than a compiled pattern.
bool pattern_ast_literal_set(AstNode* node);

// True when a pattern body contains a symbol literal. Pattern bodies are
// content-only (S11.1.2): the domain comes from the island's tag, not from the
// quoting inside it.
bool pattern_ast_has_symbol_literal(AstNode* node);

// Conceptual base-type spellings (int64, float32, ...) map to the defined
// canonical name; NULL when the name is not such a spelling.
const char* base_type_alias_suggestion(StrView name);

// Record the unknown-base-type diagnostic, with the alias suggestion when one
// exists ("unknown type 'int64'; did you mean 'i64'?").
void record_unknown_base_type_span(Transpiler* tp, SourceSpan span,
        StrView type_name);

// Record a diagnostic against a CST node. Defined in build_ast.cpp; promoted
// here so the hand parser can report through the same channel rather than
// inventing a second one.
void record_semantic_error_span(Transpiler* tp, SourceSpan span,
        LambdaErrorCode code, const char* format, ...);
