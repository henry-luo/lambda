/**
 * block_utils.cpp - Shared utility functions for block parsers
 *
 * Provides common element creation and manipulation utilities
 * used by all block parsers.
 */
#include "block_common.hpp"

namespace lambda {
namespace markup {

/**
 * add_attribute_to_element - Add a named attribute to an element
 *
 * Uses the parser's MarkBuilder to properly intern the attribute.
 */
void add_attribute_to_element(MarkupParser* parser, Element* elem,
                              const char* name, const char* value) {
    if (!parser || !elem || !name || !value) {
        return;
    }

    // Create key and value strings
    String* key = parser->builder.createString(name);
    String* val = parser->builder.createString(value);
    if (!key || !val) return;

    // Add attribute using putToElement
    Item lambda_value = {.item = s2it(val)};
    parser->builder.putToElement(elem, key, lambda_value);
}

/**
 * detect_math_flavor - Detect math notation flavor from content
 *
 * Examines the content to determine if it's LaTeX, AsciiMath, or plain.
 */
const char* detect_math_flavor(const char* content) {
    if (!content) return "latex";

    // Look for LaTeX-specific patterns
    bool has_latex = false;
    bool has_ascii = false;

    // LaTeX indicators
    if (strstr(content, "\\frac") ||
        strstr(content, "\\sqrt") ||
        strstr(content, "\\sum") ||
        strstr(content, "\\int") ||
        strstr(content, "\\alpha") ||
        strstr(content, "\\begin") ||
        strstr(content, "\\left") ||
        strstr(content, "\\right") ||
        strstr(content, "\\cdot") ||
        strstr(content, "\\times") ||
        strstr(content, "\\over") ||
        strstr(content, "_{") ||
        strstr(content, "^{")) {
        has_latex = true;
    }

    // AsciiMath indicators (without LaTeX patterns)
    if (!has_latex) {
        if (strstr(content, "sqrt(") ||
            strstr(content, "frac(") ||
            strstr(content, "sum_(") ||
            strstr(content, "int_") ||
            strstr(content, "->") ||
            strstr(content, "=>") ||
            strstr(content, "<=") ||
            strstr(content, ">=") ||
            strstr(content, "!=")) {
            has_ascii = true;
        }
    }

    if (has_latex) return "latex";
    if (has_ascii) return "ascii";

    // Default to LaTeX
    return "latex";
}

} // namespace markup
} // namespace lambda
