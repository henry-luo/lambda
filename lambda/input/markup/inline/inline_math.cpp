/**
 * inline_math.cpp - Inline math expression parser
 *
 * Parses inline math expressions:
 * - $expression$ - inline math
 *
 * Phase 3 of Markup Parser Refactoring:
 * Extracted from input-markup.cpp parse_inline_math() (lines 3593-3660)
 */
#include "inline_common.hpp"
#include <cstdlib>
#include <cstring>

namespace lambda {
namespace markup {

// Helper: Create element from parser
static inline Element* create_element(MarkupParser* parser, const char* tag) {
    return parser->builder.element(tag).final().element;
}

// Helper: Create string from parser
static inline String* create_string(MarkupParser* parser, const char* text) {
    return parser->builder.createString(text);
}

// Helper: Increment element content length
static inline void increment_element_content_length(Element* elem) {
    TypeElmt* elmt_type = (TypeElmt*)elem->type;
    elmt_type->content_length++;
}

// Helper: Add attribute to element
static inline void add_attribute_to_element(MarkupParser* parser, Element* elem,
                                            const char* key, const char* val) {
    String* k = parser->builder.createString(key);
    String* v = parser->builder.createString(val);
    if (k && v) {
        parser->builder.putToElement(elem, k, Item{.item = s2it(v)});
    }
}

/**
 * parse_inline_math - Parse inline math expressions
 *
 * Handles: $expression$
 *
 * Note: This creates a math element with the raw content. Full math
 * parsing (LaTeX/AsciiMath) is handled by the math parser in a separate pass.
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing math element, or ITEM_UNDEFINED if not matched
 */
Item parse_inline_math(MarkupParser* parser, const char** text) {
    const char* start = *text;

    // Must start with $
    if (*start != '$') {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Check it's not a display math ($$)
    if (*(start + 1) == '$') {
        // This is display math, not inline - let block parser handle it
        return Item{.item = ITEM_UNDEFINED};
    }

    const char* pos = start + 1;
    const char* content_start = pos;

    // Find closing $
    while (*pos && *pos != '$') {
        // Skip escaped $ (\$)
        if (*pos == '\\' && *(pos + 1) == '$') {
            pos += 2;
            continue;
        }
        pos++;
    }

    if (*pos != '$' || pos == content_start) {
        // No closing $ or empty content
        return Item{.item = ITEM_UNDEFINED};
    }

    // Extract content between $
    size_t content_len = pos - content_start;

    // Create math element
    Element* math_elem = create_element(parser, "math");
    if (!math_elem) {
        return Item{.item = ITEM_ERROR};
    }

    // Add type attribute for inline math
    add_attribute_to_element(parser, math_elem, "type", "inline");

    // Create content string
    char* content = (char*)malloc(content_len + 1);
    if (!content) {
        return Item{.item = ITEM_ERROR};
    }
    strncpy(content, content_start, content_len);
    content[content_len] = '\0';

    // Add math content as string
    // Note: Full math parsing is done later by the math parser
    String* math_str = create_string(parser, content);
    if (math_str) {
        Item math_item = {.item = s2it(math_str)};
        list_push((List*)math_elem, math_item);
        increment_element_content_length(math_elem);
    }

    free(content);
    *text = pos + 1; // Skip closing $

    return Item{.item = (uint64_t)math_elem};
}

} // namespace markup
} // namespace lambda
