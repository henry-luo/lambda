/**
 * inline_emphasis.cpp - Emphasis (bold/italic) parser
 *
 * Parses bold and italic text using format-appropriate delimiters:
 * - Markdown: **bold**, *italic*, __bold__, _italic_
 * - MediaWiki: '''bold''', ''italic''
 * - Other formats via adapter delimiters
 *
 * Phase 3 of Markup Parser Refactoring:
 * Extracted from input-markup.cpp parse_bold_italic() (lines 1997-2080)
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

/**
 * parse_emphasis - Parse bold and italic text
 *
 * Handles:
 * - **bold** and __bold__ → <strong>
 * - *italic* and _italic_ → <em>
 * - ***bolditalic*** → nested <strong><em>
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing emphasis element, or ITEM_UNDEFINED if not matched
 */
Item parse_emphasis(MarkupParser* parser, const char** text) {
    const char* start = *text;
    char marker = *start;  // * or _

    // Must be * or _
    if (marker != '*' && marker != '_') {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Count consecutive markers
    int count = 0;
    const char* content_start = start;
    while (*content_start == marker) {
        count++;
        content_start++;
    }

    if (count == 0) {
        (*text)++;
        return Item{.item = ITEM_UNDEFINED};
    }

    // Find closing markers
    const char* pos = content_start;
    const char* end = nullptr;
    int end_count = 0;

    while (*pos) {
        if (*pos == marker) {
            const char* marker_start = pos;
            int marker_count = 0;
            while (*pos == marker) {
                marker_count++;
                pos++;
            }

            if (marker_count >= count) {
                end = marker_start;
                end_count = marker_count;
                break;
            }
        } else {
            pos++;
        }
    }

    if (!end) {
        // No closing marker found, treat as plain text
        (*text)++;
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create appropriate element based on marker count
    Element* elem;
    if (count >= 3) {
        // Bold+italic: create strong with nested em
        elem = create_element(parser, "strong");
        if (!elem) {
            *text = end + count;
            return Item{.item = ITEM_ERROR};
        }

        // Create inner em element
        Element* inner_em = create_element(parser, "em");
        if (inner_em) {
            // Extract content
            size_t content_len = end - content_start;
            char* content = (char*)malloc(content_len + 1);
            if (content) {
                strncpy(content, content_start, content_len);
                content[content_len] = '\0';

                // Recursively parse inner content
                Item inner_content = parse_inline_spans(parser, content);
                if (inner_content.item != ITEM_ERROR && inner_content.item != ITEM_UNDEFINED) {
                    list_push((List*)inner_em, inner_content);
                    increment_element_content_length(inner_em);
                }
                free(content);
            }

            // Add em to strong
            list_push((List*)elem, Item{.item = (uint64_t)inner_em});
            increment_element_content_length(elem);
        }
    } else if (count >= 2) {
        // Bold
        elem = create_element(parser, "strong");
        if (!elem) {
            *text = end + count;
            return Item{.item = ITEM_ERROR};
        }

        // Extract and parse content
        size_t content_len = end - content_start;
        char* content = (char*)malloc(content_len + 1);
        if (content) {
            strncpy(content, content_start, content_len);
            content[content_len] = '\0';

            Item inner_content = parse_inline_spans(parser, content);
            if (inner_content.item != ITEM_ERROR && inner_content.item != ITEM_UNDEFINED) {
                list_push((List*)elem, inner_content);
                increment_element_content_length(elem);
            }
            free(content);
        }
    } else {
        // Italic
        elem = create_element(parser, "em");
        if (!elem) {
            *text = end + count;
            return Item{.item = ITEM_ERROR};
        }

        // Extract and parse content
        size_t content_len = end - content_start;
        char* content = (char*)malloc(content_len + 1);
        if (content) {
            strncpy(content, content_start, content_len);
            content[content_len] = '\0';

            Item inner_content = parse_inline_spans(parser, content);
            if (inner_content.item != ITEM_ERROR && inner_content.item != ITEM_UNDEFINED) {
                list_push((List*)elem, inner_content);
                increment_element_content_length(elem);
            }
            free(content);
        }
    }

    // Move past closing markers (use original count, not end_count)
    *text = end + count;
    return Item{.item = (uint64_t)elem};
}

} // namespace markup
} // namespace lambda
