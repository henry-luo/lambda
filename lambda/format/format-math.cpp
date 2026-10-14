// format-math.cpp - Math expression formatter
//
// Formats parsed math ASTs back to source text. LaTeX and ASCII delegate to
// the MarkReader-based formatters in format-math-latex.cpp and
// format-math-ascii.cpp. MathML conversion belongs to the script package;
// this IO entry serializes its tree. Typst output remains unimplemented.

#include "format.h"
#include "../input/input-parsers.h"
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

// Conversion runs in lambda-rt; IO only serializes the resulting MathML tree.
String* format_math_mathml(Pool* pool, Item root_item) {
    if (get_type_id(root_item) != LMD_TYPE_ELEMENT) return nullptr;
    if (!xml_document_element_matches(pool, root_item.element, "math",
            "http://www.w3.org/1998/Math/MathML")) {
        log_error("format-mathml: convert the AST through lambda.doc.math.mathml first");
        return nullptr;
    }
    return format_xml(pool, root_item);
}

// Format math to default format (LaTeX)
String* format_math(Pool* pool, Item root_item) {
    return format_math_latex(pool, root_item);
}
