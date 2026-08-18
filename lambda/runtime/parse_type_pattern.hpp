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
// Design: vibe/Lambda_Impl_Type_Scanner.md, vibe/Lambda_Type_Pattern.md §3.

#include "ast.hpp"

// Parse `[begin, end)` as a full type pattern (unions, occurrences, containers,
// fn types, islands). `origin` locates diagnostics and becomes each node's
// TSNode. Returns NULL on a syntax error, after recording it against `origin`.
AstNode* parse_type_pattern_text(Transpiler* tp, const char* begin, const char* end, TSNode origin);

// Parse a single primary type — the `?T` query operand and view-pattern atoms.
// Never consumes a top-level `|`, so a following union stays a value union.
AstNode* parse_primary_type_text(Transpiler* tp, const char* begin, const char* end, TSNode origin);
