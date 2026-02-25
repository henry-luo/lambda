// input-math.cpp - Math expression parser dispatcher
//
// Routes math parsing to the unified tree-sitter-latex-math grammar.
// Both LaTeX and ASCII math flavors are handled by the same grammar,
// which supports LaTeX commands (\frac, \sum) and ASCII tokens (word,
// ascii_operator, quoted_text) in a single parse.
//
// Note: Actual LaTeX math typesetting happens in the TeX pipeline
// (tex_math_bridge.cpp), which is linked separately for tests and rendering.
// This module provides the Input interface only.

#include "input.hpp"
#include "../lambda-data.hpp"
#include "../mark_builder.hpp"
#include "../../lib/log.h"
#include "../../lib/arena.h"
#include "../../lib/mempool.h"
#include <string.h>

// Parse math expression string and set input root
// This is the Lambda Input interface
void parse_math(Input* input, const char* math_string, const char* flavor_str) {
    if (!math_string || !*math_string) {
        input->root = ItemNull;
        return;
    }

    log_debug("parse_math called with: '%s', flavor: '%s'",
              math_string, flavor_str ? flavor_str : "null");

    // Both ASCII and LaTeX math now use the unified tree-sitter-latex-math grammar.
    // The grammar handles LaTeX commands (\frac{a}{b}) and ASCII tokens (alpha, ->, xx)
    // in a single parse. The AST builder uses MathFlavor to interpret ambiguous tokens.
    const char* flavor_label = "latex";
    if (flavor_str && (strcmp(flavor_str, "ascii") == 0 || strcmp(flavor_str, "asciimath") == 0)) {
        flavor_label = "ascii";
    }

    log_debug("parse_math: routing to unified tree-sitter parser (flavor=%s)", flavor_label);
    size_t math_len = strlen(math_string);
    Item ast = parse_math_latex_to_ast(input, math_string, math_len);
    if (ast.item != ITEM_NULL) {
        input->root = ast;
    } else {
        log_error("parse_math: failed to parse math to AST (flavor=%s)", flavor_label);
        input->root = ItemNull;
    }
}
