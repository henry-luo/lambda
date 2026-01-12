// input-math.cpp - Math expression parser dispatcher
//
// Routes math parsing to appropriate parser based on flavor.
// TODO: Reimplement LaTeX/Typst parser using TexNode (currently stubbed)

#include "input.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include <string.h>

// Parse a math expression string
// Currently only supports ASCII math; LaTeX/Typst parsing is stubbed
// TODO: Reimplement LaTeX math parsing using TexNode pipeline
void parse_math(Input* input, const char* math_string, const char* flavor_str) {
    log_debug("parse_math called with: '%s', flavor: '%s'", 
              math_string, flavor_str ? flavor_str : "null");

    if (!math_string || !*math_string) {
        input->root = ItemNull;
        return;
    }

    // Only ASCII math is currently supported
    if (flavor_str && (strcmp(flavor_str, "ascii") == 0 || strcmp(flavor_str, "asciimath") == 0)) {
        log_debug("parse_math: routing to ASCII math parser");
        Item result = input_ascii_math(input, math_string);
        if (result.item == ITEM_ERROR || result.item == ITEM_NULL) {
            input->root = ItemNull;
            return;
        }
        input->root = result;
        return;
    }

    // LaTeX/Typst: stubbed until reimplemented with TexNode
    log_debug("parse_math: LaTeX/Typst parser removed, use TexNode pipeline instead");
    input->root = ItemNull;
}
