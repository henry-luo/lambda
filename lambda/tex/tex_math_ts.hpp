// tex_math_ts.hpp - Tree-sitter based LaTeX math typesetter
//
// Parses LaTeX math using tree-sitter-latex-math grammar and
// directly produces TexNode trees with proper TFM metrics.
//
// This replaces the hand-written parser in parse_latex_math_internal()
// with a cleaner grammar-based approach while preserving all the
// TeX typesetting infrastructure (TFM metrics, atom spacing, etc.).
//
// Usage:
//   TexNode* result = typeset_latex_math_ts("x^2 + \\frac{a}{b}", len, ctx);
//
// Or from pre-parsed AST (avoids re-parsing):
//   TexNode* result = typeset_math_from_ast(math_ast, ctx);

#ifndef TEX_MATH_TS_HPP
#define TEX_MATH_TS_HPP

#include "tex_math_bridge.hpp"
#include "tex_node.hpp"
#include "tex_tfm.hpp"
#include "../mark_reader.hpp"

// Forward declare tree-sitter types (avoid including full header)
struct TSNode;

namespace tex {

// ============================================================================
// Main API
// ============================================================================

// Parse LaTeX math string and produce TexNode tree
// This is the tree-sitter based replacement for parse_latex_math_internal()
TexNode* typeset_latex_math_ts(const char* latex_str, size_t len, MathContext& ctx);

// Typeset math from pre-parsed Mark AST (produced by input-latex-ts.cpp)
// This avoids re-parsing when the math AST is already available
TexNode* typeset_math_from_ast(const ItemReader& math_ast, MathContext& ctx);

} // namespace tex

#endif // TEX_MATH_TS_HPP
