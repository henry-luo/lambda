/**
 * block_divider.cpp - Horizontal rule/divider parser
 *
 * Handles parsing of thematic breaks (horizontal rules) for all formats:
 * - Markdown: ---, ***, ___ (3+ chars, optionally with spaces)
 * - RST: Transition lines (4+ chars of =, -, etc.)
 * - MediaWiki: ---- (4+ hyphens)
 * - AsciiDoc: ''' or ---
 * - Textile: --- or ___
 */
#include "block_common.hpp"

namespace lambda {
namespace markup {

/**
 * is_thematic_break - Check if a line is a thematic break
 *
 * Thematic breaks are horizontal rules made of 3+ of the same character
 * (-, *, _) optionally separated by spaces.
 */
bool is_thematic_break(const char* line) {
    if (!line) return false;

    const char* pos = line;
    skip_whitespace(&pos);

    char marker = *pos;
    if (marker != '-' && marker != '*' && marker != '_') {
        return false;
    }

    int count = 0;
    while (*pos) {
        if (*pos == marker) {
            count++;
        } else if (*pos != ' ' && *pos != '\t') {
            // Non-marker, non-space character
            return false;
        }
        pos++;
    }

    return count >= 3;
}

/**
 * parse_divider - Parse a horizontal rule element
 *
 * Creates an <hr> element for thematic breaks.
 */
Item parse_divider(MarkupParser* parser) {
    if (!parser) {
        return Item{.item = ITEM_ERROR};
    }

    Element* hr = create_element(parser, "hr");
    if (!hr) {
        return Item{.item = ITEM_ERROR};
    }

    // Advance past the divider line
    parser->current_line++;

    return Item{.item = (uint64_t)hr};
}

} // namespace markup
} // namespace lambda
