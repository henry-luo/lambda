#pragma once

// Type-pattern hand parser (SC4).
//
// The production grammar hands the whole type sub-language to the parser as one
// opaque token (see lambda/tree-sitter-lambda/pending-sc4/). This turns such a
// token's source text into the SAME AstNode shapes the CST type builders used
// to produce — with the `Type*` graph attached — in a single pass.
//
// Why AST nodes and not bare `Type*`: value-position types are consumed by
// node KIND. The MIR transpiler routes `AST_NODE_ELMT_TYPE`/`MAP_TYPE`/… to
// `const_type(type_index)`, walks `AST_NODE_BINARY_TYPE` children for match
// or-patterns, and emits literal arms from literal-typed `AST_NODE_PRIMARY`
// nodes. A bare `AST_NODE_TYPE` wrapper sent an element pattern down the
// base-type path, which dropped its tag name — `?<p>` then matched every
// element. So the parser produces the tier the consumers already understand.
//
// Grammar reference: lambda/tree-sitter-lambda/grammar-lambda.js is normative.
// Design: vibe/Lambda_Grammar_Reduce5.md, vibe/Lambda_Type_Pattern.md §3.

#include "ast.hpp"

// Parse `[begin, end)` as a full type pattern (unions, occurrences, containers,
// fn types, islands). The legacy overload accepts a CST origin; the `_span`
// entry point is the direct-parser seam and creates only span-backed AST nodes.
AstNode* parse_type_pattern_text(Transpiler* tp, const char* begin, const char* end, TSNode origin);
AstNode* parse_type_pattern_text_span(Transpiler* tp, const char* begin,
        const char* end, LambdaSourceSpan span);

// Parse a single primary type — the `?T` query operand and view-pattern primaries.
// Never consumes a top-level `|`, so a following union stays a value union.
AstNode* parse_primary_type_text(Transpiler* tp, const char* begin, const char* end, TSNode origin);
AstNode* parse_primary_type_text_span(Transpiler* tp, const char* begin,
        const char* end, LambdaSourceSpan span);

// Parse the restricted declaration return contract: `T`, `T | U`, `T^`, or
// `T^E`. It returns the same AST_NODE_FUNC_TYPE wrapper as build_return_type.
AstNode* parse_return_type_text(Transpiler* tp, const char* begin, const char* end, TSNode origin);
AstNode* parse_return_type_text_span(Transpiler* tp, const char* begin,
        const char* end, LambdaSourceSpan span);

// Parse a view/edit model pattern: an element, name/base type, or `|` union.
AstNode* parse_view_pattern_text(Transpiler* tp, const char* begin, const char* end, TSNode origin);
AstNode* parse_view_pattern_text_span(Transpiler* tp, const char* begin,
        const char* end, LambdaSourceSpan span);
