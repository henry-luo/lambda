// format-math.cpp - Math expression formatter
//
// Formats MathNode trees to various output formats (LaTeX, Typst, ASCII, MathML).
//
// NOTE: The MathNode-based formatting (format-math2.cpp) has been removed.
// These functions are temporarily stubbed out. Math formatting will be
// reimplemented using the unified TeX/TexNode pipeline.

#include "format.h"
#include "../../lib/log.h"
#include <string.h>

// Format math to LaTeX (default format)
String* format_math_latex(Pool* pool, Item root_item) {
    (void)pool;
    (void)root_item;
    log_debug("format_math_latex: MathNode formatting disabled - to be reimplemented with TexNode");
    return nullptr;
}

// Format math to Typst
String* format_math_typst(Pool* pool, Item root_item) {
    (void)pool;
    (void)root_item;
    log_debug("format_math_typst: MathNode formatting disabled - to be reimplemented with TexNode");
    return nullptr;
}

// Format math to ASCII
String* format_math_ascii(Pool* pool, Item root_item) {
    (void)pool;
    (void)root_item;
    log_debug("format_math_ascii: MathNode formatting disabled - to be reimplemented with TexNode");
    return nullptr;
}

// Format math to MathML
String* format_math_mathml(Pool* pool, Item root_item) {
    (void)pool;
    (void)root_item;
    log_debug("format_math_mathml: MathNode formatting disabled - to be reimplemented with TexNode");
    return nullptr;
}

// Format math to default format (LaTeX)
String* format_math(Pool* pool, Item root_item) {
    return format_math_latex(pool, root_item);
}
