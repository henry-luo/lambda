#pragma once

// Lambda-side parser for a complete static path token. The scanner finds the
// token extent; this parser owns the interior grammar and emits the same path
// AST consumed by the interpreter and MIR lowering.

#include "ast.hpp"

// Build a static path AST from already-classified segments.
AstNode* build_static_path_ast_from_span(Transpiler* tp, SourceSpan span,
        PathScheme scheme, String* authority, ArrayList* segments, int first_segment);

// Parse `[begin, end)` as `path_static_expr` (Reduce5 §0.3).
AstNode* parse_path_expr_text_span(Transpiler* tp, const char* begin,
        const char* end, SourceSpan span);
AstNode* try_parse_path_expr_text_span(Transpiler* tp, const char* begin,
        const char* end, SourceSpan span);
