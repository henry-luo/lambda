// format-math.cpp - Math expression formatter
//
// Formats MathNode trees to various output formats (LaTeX, Typst, ASCII, MathML).
// Uses the new tree-sitter-based MathNode format (Map with "node" field).

#include "format.h"
#include "format-math2.hpp"
#include "../../lib/log.h"
#include <string.h>

// Format math to LaTeX (default format)
String* format_math_latex(Pool* pool, Item root_item) {
    if (root_item.item == ItemNull.item) {
        return nullptr;
    }
    return format_math2_latex(pool, root_item);
}

// Format math to Typst
String* format_math_typst(Pool* pool, Item root_item) {
    if (root_item.item == ItemNull.item) {
        return nullptr;
    }
    return format_math2_typst(pool, root_item);
}

// Format math to ASCII
String* format_math_ascii(Pool* pool, Item root_item) {
    if (root_item.item == ItemNull.item) {
        return nullptr;
    }
    return format_math2_ascii(pool, root_item);
}

// Format math to MathML
String* format_math_mathml(Pool* pool, Item root_item) {
    if (root_item.item == ItemNull.item) {
        return nullptr;
    }
    return format_math2_mathml(pool, root_item);
}

// Format math to default format (LaTeX)
String* format_math(Pool* pool, Item root_item) {
    return format_math_latex(pool, root_item);
}
