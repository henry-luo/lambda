/**
 * inline_link.cpp - Link parser
 *
 * Parses inline links:
 * - [text](url) - inline link
 * - [text](url "title") - link with title
 * - [text][ref] - reference link (future)
 *
 * Phase 3 of Markup Parser Refactoring:
 * Extracted from input-markup.cpp parse_link() (lines 2142-2224)
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
 * parse_link - Parse inline links
 *
 * Handles: [text](url), [text](url "title")
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing link element, or ITEM_UNDEFINED if not matched
 */
Item parse_link(MarkupParser* parser, const char** text) {
    const char* pos = *text;

    // Must start with [
    if (*pos != '[') {
        return Item{.item = ITEM_UNDEFINED};
    }

    pos++; // Skip [

    // Find closing ]
    const char* text_start = pos;
    const char* text_end = nullptr;
    int bracket_depth = 1;

    while (*pos && bracket_depth > 0) {
        if (*pos == '\\' && *(pos + 1)) {
            // Skip escaped character
            pos += 2;
            continue;
        }
        if (*pos == '[') bracket_depth++;
        else if (*pos == ']') bracket_depth--;

        if (bracket_depth == 0) {
            text_end = pos;
        }
        pos++;
    }

    if (!text_end) {
        // No closing ]
        return Item{.item = ITEM_UNDEFINED};
    }

    // Check for (url) after ]
    if (*pos != '(') {
        // Might be reference-style link [text][ref], but skip for now
        return Item{.item = ITEM_UNDEFINED};
    }

    pos++; // Skip (

    // Find closing )
    const char* url_start = pos;
    const char* url_end = nullptr;
    const char* title_start = nullptr;
    const char* title_end = nullptr;
    int paren_depth = 1;

    // Skip leading whitespace in URL
    while (*pos == ' ' || *pos == '\t') pos++;
    url_start = pos;

    // Parse URL and optional title
    while (*pos && paren_depth > 0) {
        if (*pos == '\\' && *(pos + 1)) {
            // Skip escaped character
            pos += 2;
            continue;
        }

        // Check for title (quoted string)
        if ((*pos == '"' || *pos == '\'') && !title_start) {
            url_end = pos - 1;
            // Trim trailing whitespace from URL
            while (url_end > url_start && (*url_end == ' ' || *url_end == '\t')) {
                url_end--;
            }
            url_end++; // Point past last URL char

            char quote = *pos;
            pos++; // Skip opening quote
            title_start = pos;

            // Find closing quote
            while (*pos && *pos != quote) {
                if (*pos == '\\' && *(pos + 1)) {
                    pos += 2;
                } else {
                    pos++;
                }
            }
            if (*pos == quote) {
                title_end = pos;
                pos++; // Skip closing quote
            }
            continue;
        }

        if (*pos == '(') paren_depth++;
        else if (*pos == ')') paren_depth--;

        if (paren_depth == 0) {
            if (!url_end) {
                url_end = pos;
                // Trim trailing whitespace
                while (url_end > url_start && (*(url_end-1) == ' ' || *(url_end-1) == '\t')) {
                    url_end--;
                }
            }
        }
        pos++;
    }

    if (paren_depth > 0) {
        // Unmatched parenthesis
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create link element
    Element* link = create_element(parser, "a");
    if (!link) {
        *text = pos;
        return Item{.item = ITEM_ERROR};
    }

    // Add href attribute
    if (url_end > url_start) {
        size_t url_len = url_end - url_start;
        char* url = (char*)malloc(url_len + 1);
        if (url) {
            strncpy(url, url_start, url_len);
            url[url_len] = '\0';
            add_attribute_to_element(parser, link, "href", url);
            free(url);
        }
    }

    // Add title attribute if present
    if (title_start && title_end && title_end > title_start) {
        size_t title_len = title_end - title_start;
        char* title = (char*)malloc(title_len + 1);
        if (title) {
            strncpy(title, title_start, title_len);
            title[title_len] = '\0';
            add_attribute_to_element(parser, link, "title", title);
            free(title);
        }
    }

    // Parse link text content (can contain inline elements)
    if (text_end > text_start) {
        size_t text_len = text_end - text_start;
        char* link_text = (char*)malloc(text_len + 1);
        if (link_text) {
            strncpy(link_text, text_start, text_len);
            link_text[text_len] = '\0';

            // Recursively parse inline content
            Item inner_content = parse_inline_spans(parser, link_text);
            if (inner_content.item != ITEM_ERROR && inner_content.item != ITEM_UNDEFINED) {
                list_push((List*)link, inner_content);
                increment_element_content_length(link);
            }

            free(link_text);
        }
    }

    *text = pos;
    return Item{.item = (uint64_t)link};
}

} // namespace markup
} // namespace lambda
