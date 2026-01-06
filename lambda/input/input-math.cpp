// input-math.cpp - Math expression parser dispatcher
//
// Routes math parsing to appropriate parser based on flavor:
// - LaTeX/Typst: tree-sitter-based parser (input-math2.cpp)
// - ASCII: custom parser (input-math-ascii.cpp)

#include "input.hpp"
#include "input-math2.hpp"
#include "../lambda.h"
#include "../../lib/log.h"
#include <string.h>

using namespace lambda;

// Math flavor types
typedef enum {
    MATH_FLAVOR_LATEX,
    MATH_FLAVOR_TYPST,
    MATH_FLAVOR_ASCII
} MathFlavor;

// Get math flavor from string
static MathFlavor get_math_flavor(const char* flavor_str) {
    if (!flavor_str || strlen(flavor_str) == 0) {
        return MATH_FLAVOR_LATEX;  // default
    }
    if (strcmp(flavor_str, "ascii") == 0 || strcmp(flavor_str, "asciimath") == 0) {
        return MATH_FLAVOR_ASCII;
    }
    if (strcmp(flavor_str, "typst") == 0) {
        return MATH_FLAVOR_TYPST;
    }
    return MATH_FLAVOR_LATEX;
}

// Parse a math expression string
// This is the main entry point for math parsing
void parse_math(Input* input, const char* math_string, const char* flavor_str) {
    log_debug("parse_math called with: '%s', flavor: '%s'", 
              math_string, flavor_str ? flavor_str : "null");

    if (!math_string || !*math_string) {
        input->root = ItemNull;
        return;
    }

    MathFlavor flavor = get_math_flavor(flavor_str);
    Item result;

    // Route to appropriate parser based on flavor
    if (flavor == MATH_FLAVOR_ASCII) {
        log_debug("parse_math: routing to ASCII math parser");
        result = input_ascii_math(input, math_string);
    } else {
        // Use tree-sitter-based parser for LaTeX and Typst
        log_debug("parse_math: using tree-sitter math parser");
        result = lambda::parse_math(math_string, input);
    }

    if (result.item == ITEM_ERROR || result.item == ITEM_NULL) {
        log_debug("parse_math: result is error or null");
        input->root = ItemNull;
        return;
    }

    input->root = result;
}
