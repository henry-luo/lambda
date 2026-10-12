// format-math.cpp - Math expression formatter
//
// Formats parsed math ASTs back to source text. LaTeX and ASCII delegate to
// the MarkReader-based formatters in format-math-latex.cpp and
// format-math-ascii.cpp. Typst and MathML output are not implemented and
// return null; their former MathNode/TexNode paths were removed.

#include "format.h"
#include "../../lib/log.h"
#include <string.h>

// Format math to LaTeX (default format)
String* format_math_latex(Pool* pool, Item root_item) {
    return format_math_latex_standalone(pool, root_item);
}

// Format math to Typst
String* format_math_typst(Pool* pool, Item root_item) {
    (void)pool;
    (void)root_item;
    log_debug("format_math_typst: Typst math output is not implemented");
    return nullptr;
}

// Format math to ASCII - delegates to the standalone MarkReader-based formatter
String* format_math_ascii(Pool* pool, Item root_item) {
    return format_math_ascii_standalone(pool, root_item);
}

// Format math to MathML
String* format_math_mathml(Pool* pool, Item root_item) {
    (void)pool;
    (void)root_item;
    log_debug("format_math_mathml: MathML math output is not implemented");
    return nullptr;
}

// Format math to default format (LaTeX)
String* format_math(Pool* pool, Item root_item) {
    return format_math_latex(pool, root_item);
}
