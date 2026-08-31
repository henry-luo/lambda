// input-math.cpp - direct math input dispatcher

#include "input.hpp"
#include "input-parsers.h"
#include "../../lib/log.h"
#include <string.h>

void parse_math(Input* input, const char* math_string, const char* flavor_str) {
    if (!input || !math_string || !*math_string) {
        if (input) input->root = ItemNull;
        return;
    }

    const char* flavor_label = "latex";
    if (flavor_str && (strcmp(flavor_str, "ascii") == 0 || strcmp(flavor_str, "asciimath") == 0)) {
        flavor_label = "ascii";
    }

    log_debug("parse_math_direct: parsing flavor=%s", flavor_label);
    Item ast = parse_math_direct_to_ast(input, math_string, strlen(math_string), flavor_label);
    if (ast.item != ITEM_NULL) {
        input->root = ast;
    } else {
        log_error("parse_math_direct: failed to build AST flavor=%s", flavor_label);
        input->root = ItemNull;
    }
}

void cleanup_math_parser() {
    // direct parser state is owned by each InputContext
}
