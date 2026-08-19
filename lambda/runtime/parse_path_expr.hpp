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

// Parse `[begin, end)` as `path_static_expr` (Reduce5 §0.3). `origin` is the
// one external token retained for AST source-range diagnostics; no inner CST
// path nodes are read by this parser.
AstNode* parse_path_expr_text(Transpiler* tp, const char* begin, const char* end,
        TSNode origin);
AstNode* try_parse_path_expr_text(Transpiler* tp, const char* begin,
        const char* end, TSNode origin);
