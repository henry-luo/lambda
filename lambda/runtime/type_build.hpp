#pragma once

// Shared Type* construction surface.
//
// These helpers are shared by the direct AST builder and the type-pattern hand
// parser. Keep the common type construction here instead of copying it.

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

// Append one field to a map/element shape from its already-resolved name/type.
ShapeEntry* append_shape_entry_typed(Transpiler* tp, String* name, Type* field_type,
        ShapeEntry** shape, ShapeEntry** prev_entry, int byte_offset);

// Allocate the TypeParam an AST_NODE_PARAM binding owns: `carrier`'s compact
// prefix (`any` when NULL) marked TYPE_KIND_PARAM; contract and full_type unset.
TypeParam* alloc_type_param(Pool* pool, const Type* carrier);

// Fold a declared type into a TypeParam (compact prefix, retained contract,
// full_type selection). Used for `fn(a: T)` parameters.
void apply_declared_param_type(Transpiler* tp, TypeParam* param_type, Type* declared);

// Declare a fn type's return contract.
void set_fn_return_contract(TypeFunc* fn_type, Type* contract, bool is_explicit);

// Register a binary type with raw TypeBinary operands over the node's already
// resolved operands. Declaration return types use this rather than the
// general pattern binary constructor.
void register_binary_type(Transpiler* tp, AstBinaryNode* binary);

// Allocate an AST node. Defined in build_ast.cpp; promoted because pattern
// islands are the one type form whose AST must survive to MIR transpilation,
// so the hand parser has to build real nodes for them.
AstNode* alloc_ast_node_from_span(Transpiler* tp, AstNodeType node_type,
        SourceSpan span, size_t size);

// Evaluate a literal AST node to the Item it denotes (compile-time constants
// only). Used for bracket-type positions and range bounds.
bool ast_static_literal_item(Transpiler* tp, AstNode* node, Item* out);

// The `-1` / `(((2.5)))` shape: PRIMARY wrappers, at most one leading sign,
// then the literal token itself. Returns that token (a childless PRIMARY when
// the source really is a literal) and reports whether a `-` was consumed; NULL
// when the chain lands on anything computed. One authority for a walk that had
// started to accumulate copies (rule 13) -- callers pair it with
// `ast_static_literal_item` and apply the sign themselves, because the numeric
// kinds each caller admits differ.
AstNode* ast_signed_literal_operand(AstNode* node, bool* negated);

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

// Record a diagnostic against a source span. Defined in build_ast.cpp so the
// hand parser can report through the same channel rather than inventing one.
void record_semantic_error_span(Transpiler* tp, SourceSpan span,
        LambdaErrorCode code, const char* format, ...);
