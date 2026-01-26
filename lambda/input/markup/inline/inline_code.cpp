/**
 * inline_code.cpp - Inline code span parser
 *
 * Parses inline code spans:
 * - Single backtick: `code`
 * - Double backtick: ``code with `backtick` ``
 *
 * Phase 3 of Markup Parser Refactoring:
 * Extracted from input-markup.cpp parse_code_span() (lines 2082-2140)
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
 * parse_code_span - Parse inline code spans
 *
 * Handles:
 * - `code` - single backtick
 * - ``code`` - double backtick (can contain single backticks)
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing code element, or ITEM_UNDEFINED if not matched
 */
Item parse_code_span(MarkupParser* parser, const char** text) {
    const char* start = *text;

    // Must start with backtick
    if (*start != '`') {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Count opening backticks
    int backticks = 0;
    while (*start == '`') {
        backticks++;
        start++;
    }

    // Find matching closing backticks
    const char* pos = start;
    const char* end = nullptr;

    while (*pos) {
        if (*pos == '`') {
            const char* close_start = pos;
            int close_count = 0;
            while (*pos == '`') {
                close_count++;
                pos++;
            }

            if (close_count == backticks) {
                end = close_start;
                break;
            }
            // Keep searching if count doesn't match
        } else {
            pos++;
        }
    }

    if (!end) {
        // No matching closing backticks
        (*text)++;
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create code element
    Element* code = create_element(parser, "code");
    if (!code) {
        *text = end + backticks;
        return Item{.item = ITEM_ERROR};
    }

    // Add type attribute for inline code
    add_attribute_to_element(parser, code, "type", "inline");

    // Extract code content (no further inline parsing for code)
    size_t content_len = end - start;
    char* content = (char*)malloc(content_len + 1);
    if (content) {
        strncpy(content, start, content_len);
        content[content_len] = '\0';

        // Trim leading/trailing space if using double backticks
        // This handles cases like `` `code` `` → `code`
        char* trimmed = content;
        size_t trimmed_len = content_len;
        if (backticks > 1 && content_len > 0) {
            if (content[0] == ' ') {
                trimmed++;
                trimmed_len--;
            }
            if (trimmed_len > 0 && trimmed[trimmed_len - 1] == ' ') {
                trimmed_len--;
            }
        }

        // Create content string (use trimmed if applicable)
        String* code_text;
        if (trimmed != content || trimmed_len != content_len) {
            char* final_content = (char*)malloc(trimmed_len + 1);
            if (final_content) {
                strncpy(final_content, trimmed, trimmed_len);
                final_content[trimmed_len] = '\0';
                code_text = create_string(parser, final_content);
                free(final_content);
            } else {
                code_text = create_string(parser, content);
            }
        } else {
            code_text = create_string(parser, content);
        }

        if (code_text) {
            Item code_item = {.item = s2it(code_text)};
            list_push((List*)code, code_item);
            increment_element_content_length(code);
        }

        free(content);
    }

    *text = end + backticks;
    return Item{.item = (uint64_t)code};
}

} // namespace markup
} // namespace lambda
