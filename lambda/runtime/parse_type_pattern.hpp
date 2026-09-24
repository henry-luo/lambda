#pragma once

// Type-pattern hand parser (SC4).
//
// The production grammar hands the whole type sub-language to the parser as one
// opaque token. This turns that token's source text into the retained AstNode
// shapes, with the `Type*` graph attached.
//
// Why AST nodes and not bare `Type*`: value-position types are consumed by
// node KIND. The MIR transpiler routes `AST_NODE_ELMT_TYPE`/`MAP_TYPE`/… to
// `const_type(type_index)`, walks `AST_NODE_BINARY_TYPE` children for match
// or-patterns, and emits literal arms from literal-typed `AST_NODE_PRIMARY`
// nodes. A bare `AST_NODE_TYPE` wrapper sent an element pattern down the
// base-type path, which dropped its tag name — `?<p>` then matched every
// element. So the parser produces the tier the consumers already understand.
//
// The work is split like the rest of the Lambda front end: the syntax half
// builds those nodes while the source parses, and the resolve half attaches
// the Type graph, binds names, registers constants/types, and reports
// semantic diagnostics when the resolve pass reaches the slot.
//
// Grammar reference: lambda/tree-sitter-lambda/grammar.js is normative.
// Design: vibe/Lambda_Grammar_Reduce5.md, vibe/Lambda_Type_Pattern.md §3.

#include "ast.hpp"
#include "lambda-error.h"

// Which grammar entry a type slot is parsed with.
typedef enum TypePatternMode {
    // unions, occurrences, containers, fn types, islands
    TYPE_PATTERN_FULL,
    // a single primary type — the `?T` query operand; a following `|` stays a
    // value union
    TYPE_PATTERN_PRIMARY,
    // the restricted declaration return contract `T`, `T | U`, `T^`, `T^E`,
    // wrapped in the same AST_NODE_FUNC_TYPE as build_return_type
    TYPE_PATTERN_RETURN_CONTRACT,
    // the declaration return pattern itself
    TYPE_PATTERN_RETURN_VALUE,
    // a view/edit model pattern: an element, name/base type, or `|` union
    TYPE_PATTERN_VIEW,
} TypePatternMode;

// The first syntax error in a type slot. It is reported when the slot is
// resolved, which keeps every diagnostic in source order.
typedef struct TypePatternFailure {
    LambdaErrorCode code;
    const char* message;
} TypePatternFailure;

// Syntax half: build the retained node shapes, tagged with LSF_TP_* forms,
// with no Type, name lookup, registration, or diagnostic. Returns NULL on a
// syntax error and describes it in *failure.
AstNode* parse_type_pattern_syntax(Transpiler* tp, const char* begin,
        const char* end, SourceSpan span, TypePatternMode mode,
        TypePatternFailure* failure);

// Resolve half: attach the Type graph, bind type and pattern names, and
// register constants/types in the order the productions completed. A node
// that is not an unresolved type-pattern node is left untouched.
void resolve_type_pattern(Transpiler* tp, AstNode* node);

// Combined entry points: the syntax half then the resolve half, reporting a
// syntax error immediately and returning NULL for it.
AstNode* parse_type_pattern_text_span(Transpiler* tp, const char* begin,
        const char* end, SourceSpan span);
AstNode* parse_primary_type_text_span(Transpiler* tp, const char* begin,
        const char* end, SourceSpan span);
AstNode* parse_return_type_text_span(Transpiler* tp, const char* begin,
    const char* end, SourceSpan span);
AstNode* parse_return_value_type_text_span(Transpiler* tp, const char* begin,
    const char* end, SourceSpan span);
AstNode* parse_view_pattern_text_span(Transpiler* tp, const char* begin,
        const char* end, SourceSpan span);
