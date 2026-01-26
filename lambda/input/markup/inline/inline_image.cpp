/**
 * inline_image.cpp - Image parser
 *
 * Parses inline images:
 * - ![alt](src) - basic image
 * - ![alt](src "title") - image with title
 *
 * Phase 3 of Markup Parser Refactoring:
 * Extracted from input-markup.cpp parse_image() (lines 2226-2290)
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
 * extract_alt_text - Extract plain text from alt attribute, stripping markdown
 *
 * Handles:
 * - Emphasis: *foo* → foo, **bar** → bar, ***baz*** → baz
 * - Links: [text](/url) → text
 * - Images: ![alt](/url) → alt
 * - Backslash escapes: \* → *
 *
 * Returns newly allocated string with plain text.
 * Caller must free the result.
 */
static char* extract_alt_text(const char* start, size_t len) {
    char* result = (char*)malloc(len + 1);
    if (!result) return nullptr;

    char* out = result;
    const char* pos = start;
    const char* end = start + len;

    while (pos < end) {
        // Handle backslash escapes
        if (*pos == '\\' && pos + 1 < end) {
            pos++;  // Skip backslash
            *out++ = *pos++;  // Copy escaped char
            continue;
        }

        // Skip emphasis markers but keep content
        if (*pos == '*' || *pos == '_') {
            pos++;  // Skip marker
            continue;
        }

        // Handle links [text](url) or [text][ref]
        if (*pos == '[') {
            pos++;  // Skip [
            // Extract text until ]
            while (pos < end && *pos != ']') {
                if (*pos == '\\' && pos + 1 < end) {
                    pos++;
                    *out++ = *pos++;
                } else {
                    *out++ = *pos++;
                }
            }
            if (pos < end && *pos == ']') {
                pos++;  // Skip ]
                // Skip (url) or [ref] part
                if (pos < end && *pos == '(') {
                    int depth = 1;
                    pos++;
                    while (pos < end && depth > 0) {
                        if (*pos == '\\' && pos + 1 < end) pos += 2;
                        else if (*pos == '(') { depth++; pos++; }
                        else if (*pos == ')') { depth--; pos++; }
                        else pos++;
                    }
                } else if (pos < end && *pos == '[') {
                    pos++;
                    while (pos < end && *pos != ']') pos++;
                    if (pos < end) pos++;  // Skip ]
                }
            }
            continue;
        }

        // Handle images ![alt](url) or ![alt][ref]
        if (*pos == '!' && pos + 1 < end && *(pos + 1) == '[') {
            pos += 2;  // Skip ![
            // Extract alt text until ]
            while (pos < end && *pos != ']') {
                if (*pos == '\\' && pos + 1 < end) {
                    pos++;
                    *out++ = *pos++;
                } else {
                    *out++ = *pos++;
                }
            }
            if (pos < end && *pos == ']') {
                pos++;  // Skip ]
                // Skip (url) or [ref] part
                if (pos < end && *pos == '(') {
                    int depth = 1;
                    pos++;
                    while (pos < end && depth > 0) {
                        if (*pos == '\\' && pos + 1 < end) pos += 2;
                        else if (*pos == '(') { depth++; pos++; }
                        else if (*pos == ')') { depth--; pos++; }
                        else pos++;
                    }
                } else if (pos < end && *pos == '[') {
                    pos++;
                    while (pos < end && *pos != ']') pos++;
                    if (pos < end) pos++;  // Skip ]
                }
            }
            continue;
        }

        // Regular character
        *out++ = *pos++;
    }

    *out = '\0';
    return result;
}

/**
 * parse_image - Parse inline images
 *
 * Handles: ![alt](src), ![alt](src "title")
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing image element, or ITEM_UNDEFINED if not matched
 */
Item parse_image(MarkupParser* parser, const char** text) {
    const char* pos = *text;

    // Must start with ![
    if (*pos != '!' || *(pos + 1) != '[') {
        return Item{.item = ITEM_UNDEFINED};
    }

    pos += 2; // Skip ![

    // Find closing ]
    const char* alt_start = pos;
    const char* alt_end = nullptr;
    int bracket_depth = 1;

    while (*pos && bracket_depth > 0) {
        if (*pos == '\\' && *(pos + 1)) {
            pos += 2;
            continue;
        }
        if (*pos == '[') bracket_depth++;
        else if (*pos == ']') bracket_depth--;

        if (bracket_depth == 0) {
            alt_end = pos;
        }
        pos++;
    }

    if (!alt_end) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Check for (src) after ] - inline image
    if (*pos == '(') {
        pos++; // Skip (

        // Parse source URL and optional title
        const char* src_start = pos;
        const char* src_end = nullptr;
        const char* title_start = nullptr;
        const char* title_end = nullptr;
        bool angle_bracket_url = false;

        // Skip leading whitespace
        while (*pos == ' ' || *pos == '\t') pos++;

        // Check for angle-bracketed URL: <url>
        if (*pos == '<') {
            angle_bracket_url = true;
            pos++; // Skip <
            src_start = pos;

            // Find closing >
            while (*pos && *pos != '>' && *pos != '\n') {
                if (*pos == '\\' && *(pos + 1)) {
                    pos += 2;
                } else {
                    pos++;
                }
            }

            if (*pos != '>') {
                return Item{.item = ITEM_UNDEFINED};
            }

            src_end = pos;
            pos++; // Skip >

            // Skip whitespace after URL
            while (*pos == ' ' || *pos == '\t') pos++;

            // Check for optional title
            if (*pos == '"' || *pos == '\'') {
                char quote = *pos;
                pos++; // Skip opening quote
                title_start = pos;

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
            }

            // Skip trailing whitespace and expect )
            while (*pos == ' ' || *pos == '\t') pos++;
            if (*pos != ')') {
                return Item{.item = ITEM_UNDEFINED};
            }
            pos++; // Skip )
        } else {
            // Non-angle-bracketed URL
            src_start = pos;

        int paren_depth = 1;
        while (*pos && paren_depth > 0) {
            if (*pos == '\\' && *(pos + 1)) {
                pos += 2;
                continue;
            }

            // Check for title
            if ((*pos == '"' || *pos == '\'') && !title_start) {
                src_end = pos - 1;
                while (src_end > src_start && (*src_end == ' ' || *src_end == '\t')) {
                    src_end--;
                }
                src_end++;

                char quote = *pos;
                pos++;
                title_start = pos;

                while (*pos && *pos != quote) {
                    if (*pos == '\\' && *(pos + 1)) {
                        pos += 2;
                    } else {
                        pos++;
                    }
                }
                if (*pos == quote) {
                    title_end = pos;
                    pos++;
                }
                continue;
            }

            if (*pos == '(') paren_depth++;
            else if (*pos == ')') paren_depth--;

            if (paren_depth == 0) {
                if (!src_end) {
                    src_end = pos;
                    while (src_end > src_start && (*(src_end-1) == ' ' || *(src_end-1) == '\t')) {
                        src_end--;
                    }
                }
            }
            pos++;
        }

        if (paren_depth > 0) {
            return Item{.item = ITEM_UNDEFINED};
        }
        } // end of non-angle-bracketed URL else block

        // Create image element
        Element* img = create_element(parser, "img");
        if (!img) {
            *text = pos;
            return Item{.item = ITEM_ERROR};
        }

        // Add src attribute
        if (src_end > src_start) {
            size_t src_len = src_end - src_start;
            char* src = (char*)malloc(src_len + 1);
            if (src) {
                strncpy(src, src_start, src_len);
                src[src_len] = '\0';
                add_attribute_to_element(parser, img, "src", src);
                free(src);
            }
        }

        // Add alt attribute (extract plain text from markdown)
        if (alt_end > alt_start) {
            size_t alt_len = alt_end - alt_start;
            char* alt = extract_alt_text(alt_start, alt_len);
            if (alt) {
                add_attribute_to_element(parser, img, "alt", alt);
                free(alt);
            }
        }

        // Add title attribute if present
        if (title_start && title_end && title_end > title_start) {
            size_t title_len = title_end - title_start;
            char* title = (char*)malloc(title_len + 1);
            if (title) {
                strncpy(title, title_start, title_len);
                title[title_len] = '\0';
                add_attribute_to_element(parser, img, "title", title);
                free(title);
            }
        }

        *text = pos;
        return Item{.item = (uint64_t)img};
    }

    // Check for reference image: ![alt][ref], ![alt][], or ![alt]
    const char* ref_start = nullptr;
    const char* ref_end = nullptr;

    if (*pos == '[') {
        // ![alt][ref] or ![alt][] form
        pos++; // skip [
        ref_start = pos;

        // Find closing ]
        while (*pos && *pos != ']' && *pos != '\n') {
            if (*pos == '\\' && *(pos+1)) {
                pos += 2;
            } else {
                pos++;
            }
        }

        if (*pos != ']') {
            return Item{.item = ITEM_UNDEFINED};
        }

        ref_end = pos;
        pos++; // skip ]

        // If ref is empty [], use alt text as ref
        if (ref_end == ref_start) {
            ref_start = alt_start;
            ref_end = alt_end;
        }
    } else {
        // Shortcut reference image ![alt]
        ref_start = alt_start;
        ref_end = alt_end;
    }

    // Look up the reference
    const LinkDefinition* def = parser->getLinkDefinition(ref_start, ref_end - ref_start);
    if (!def) {
        // Not a valid reference image
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create image from definition
    Element* img = create_element(parser, "img");
    if (!img) {
        return Item{.item = ITEM_ERROR};
    }

    // Add src attribute from definition URL
    if (def->url[0]) {
        add_attribute_to_element(parser, img, "src", def->url);
    }

    // Add alt attribute (extract plain text from markdown)
    if (alt_end > alt_start) {
        size_t alt_len = alt_end - alt_start;
        char* alt = extract_alt_text(alt_start, alt_len);
        if (alt) {
            add_attribute_to_element(parser, img, "alt", alt);
            free(alt);
        }
    }

    // Add title attribute if definition has one
    if (def->has_title && def->title[0]) {
        add_attribute_to_element(parser, img, "title", def->title);
    }

    *text = pos;
    return Item{.item = (uint64_t)img};
}

} // namespace markup
} // namespace lambda
