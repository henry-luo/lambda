#pragma once

// Lambda-side parser for a complete static path token. The scanner finds the
// token extent; this parser owns the interior grammar and emits the same path
// AST consumed by the interpreter and MIR lowering.

#include "ast.hpp"

// Build a static path AST from already-classified segments. The legacy CST
// adapter and the external-token parser share this finalizer so scheme,
// authority, and segment ownership cannot drift apart during the migration.
AstNode* build_static_path_ast(Transpiler* tp, TSNode origin, PathScheme scheme,
        String* authority, ArrayList* segments, int first_segment);
AstNode* build_static_path_ast_from_span(Transpiler* tp, LambdaSourceSpan span,
        PathScheme scheme, String* authority, ArrayList* segments, int first_segment);

// Parse `[begin, end)` as `path_static_expr` (Reduce5 §0.3). The legacy
// overload accepts the external CST token; the `_span` entry point uses the
// direct parser's committed range and reads no inner CST nodes.
AstNode* parse_path_expr_text(Transpiler* tp, const char* begin, const char* end,
        TSNode origin);
AstNode* try_parse_path_expr_text(Transpiler* tp, const char* begin,
        const char* end, TSNode origin);
AstNode* parse_path_expr_text_span(Transpiler* tp, const char* begin,
        const char* end, LambdaSourceSpan span);
AstNode* try_parse_path_expr_text_span(Transpiler* tp, const char* begin,
        const char* end, LambdaSourceSpan span);
