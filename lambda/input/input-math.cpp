// input-math.cpp - Math expression parser dispatcher
//
// Routes math parsing to appropriate parser based on flavor:
// - LaTeX/Typst: Stores source for later TeX pipeline rendering
// - ASCII: Custom parser (input-math-ascii.cpp)
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

    // ASCII math uses separate parser
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

    // LaTeX math: store source for TeX pipeline rendering
    // Actual typesetting happens when rendering via tex_math_bridge
    log_debug("parse_math: LaTeX math - storing source for TeX pipeline rendering");

    // Create a simple map with the math source for later rendering
    MarkBuilder builder(input);
    MapBuilder mb = builder.map();
    mb.put("type", builder.createSymbol("latex-math"));
    mb.put("source", builder.createString(math_string));
    mb.put("display", builder.createBool(false));  // inline by default
    input->root = mb.final();
}
