/**
 * inline_link.cpp - Link parser
 *
 * Parses inline links:
 * - [text](url) - inline link
 * - [text](url "title") - link with title
 * - [text][ref] - reference link
 * - [text][] - collapsed reference link
 * - [text] - shortcut reference link
 *
 * Phase 3 of Markup Parser Refactoring:
 * Extracted from input-markup.cpp parse_link() (lines 2142-2224)
 */
#include "inline_common.hpp"
#include "../../html_entities.h"
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

// CommonMark escapable punctuation characters
static bool is_escapable_char(char c) {
    return c == '!' || c == '"' || c == '#' || c == '$' || c == '%' ||
           c == '&' || c == '\'' || c == '(' || c == ')' || c == '*' ||
           c == '+' || c == ',' || c == '-' || c == '.' || c == '/' ||
           c == ':' || c == ';' || c == '<' || c == '=' || c == '>' ||
           c == '?' || c == '@' || c == '[' || c == '\\' || c == ']' ||
           c == '^' || c == '_' || c == '`' || c == '{' || c == '|' ||
           c == '}' || c == '~';
}

/**
 * unescape_string - Process backslash escapes and entity references in a string
 *
 * Returns newly allocated string with escapes processed.
 * Caller must free the result.
 */
static char* unescape_string(const char* start, size_t len) {
    char* result = (char*)malloc(len * 4 + 1); // Worst case: all entities expand
    if (!result) return nullptr;

    char* out = result;
    const char* pos = start;
    const char* end = start + len;

    while (pos < end) {
        if (*pos == '\\' && pos + 1 < end && is_escapable_char(*(pos + 1))) {
            // Backslash escape - skip backslash, copy escaped char
            pos++;
            *out++ = *pos++;
        } else if (*pos == '&') {
            // Try to parse entity reference
            const char* entity_start = pos + 1;
            const char* entity_pos = entity_start;

            if (*entity_pos == '#') {
                // Numeric entity
                entity_pos++;
                uint32_t codepoint = 0;
                bool valid = false;

                if (*entity_pos == 'x' || *entity_pos == 'X') {
                    // Hex
                    entity_pos++;
                    const char* num_start = entity_pos;
                    while (entity_pos < end &&
                           ((*entity_pos >= '0' && *entity_pos <= '9') ||
                            (*entity_pos >= 'a' && *entity_pos <= 'f') ||
                            (*entity_pos >= 'A' && *entity_pos <= 'F'))) {
                        codepoint *= 16;
                        if (*entity_pos >= '0' && *entity_pos <= '9')
                            codepoint += *entity_pos - '0';
                        else if (*entity_pos >= 'a' && *entity_pos <= 'f')
                            codepoint += *entity_pos - 'a' + 10;
                        else
                            codepoint += *entity_pos - 'A' + 10;
                        entity_pos++;
                        if (codepoint > 0x10FFFF) break;
                    }
                    if (entity_pos > num_start && entity_pos < end && *entity_pos == ';' && codepoint <= 0x10FFFF) {
                        valid = true;
                    }
                } else {
                    // Decimal
                    const char* num_start = entity_pos;
                    while (entity_pos < end && *entity_pos >= '0' && *entity_pos <= '9') {
                        codepoint = codepoint * 10 + (*entity_pos - '0');
                        entity_pos++;
                        if (codepoint > 0x10FFFF) break;
                    }
                    if (entity_pos > num_start && entity_pos < end && *entity_pos == ';' && codepoint <= 0x10FFFF) {
                        valid = true;
                    }
                }

                if (valid) {
                    if (codepoint == 0) codepoint = 0xFFFD;
                    int utf8_len = unicode_to_utf8(codepoint, out);
                    if (utf8_len > 0) {
                        out += utf8_len;
                        pos = entity_pos + 1;
                        continue;
                    }
                }
            } else {
                // Named entity
                while (entity_pos < end &&
                       ((*entity_pos >= 'a' && *entity_pos <= 'z') ||
                        (*entity_pos >= 'A' && *entity_pos <= 'Z') ||
                        (*entity_pos >= '0' && *entity_pos <= '9'))) {
                    entity_pos++;
                }

                if (entity_pos > entity_start && entity_pos < end && *entity_pos == ';') {
                    size_t name_len = entity_pos - entity_start;
                    EntityResult result = html_entity_resolve(entity_start, name_len);

                    if (result.type == ENTITY_ASCII_ESCAPE) {
                        size_t decoded_len = strlen(result.decoded);
                        memcpy(out, result.decoded, decoded_len);
                        out += decoded_len;
                        pos = entity_pos + 1;
                        continue;
                    } else if (result.type == ENTITY_UNICODE_SPACE || result.type == ENTITY_NAMED) {
                        int utf8_len = unicode_to_utf8(result.named.codepoint, out);
                        if (utf8_len > 0) {
                            out += utf8_len;
                            pos = entity_pos + 1;
                            continue;
                        }
                    }
                }
            }

            // Not a valid entity, copy & literally
            *out++ = *pos++;
        } else {
            *out++ = *pos++;
        }
    }

    *out = '\0';
    return result;
}

/**
 * create_link_from_definition - Create link element from a LinkDefinition
 */
static Item create_link_from_definition(MarkupParser* parser,
                                         const LinkDefinition* def,
                                         const char* link_text, size_t text_len) {
    Element* link = create_element(parser, "a");
    if (!link) {
        return Item{.item = ITEM_ERROR};
    }

    // Add href attribute
    add_attribute_to_element(parser, link, "href", def->url);

    // Add title attribute if present
    if (def->has_title && def->title[0]) {
        add_attribute_to_element(parser, link, "title", def->title);
    }

    // Parse link text content
    if (text_len > 0) {
        char* text_copy = (char*)malloc(text_len + 1);
        if (text_copy) {
            memcpy(text_copy, link_text, text_len);
            text_copy[text_len] = '\0';

            Item inner_content = parse_inline_spans(parser, text_copy);
            if (inner_content.item != ITEM_ERROR && inner_content.item != ITEM_UNDEFINED) {
                list_push((List*)link, inner_content);
                increment_element_content_length(link);
            }
            free(text_copy);
        }
    }

    return Item{.item = (uint64_t)link};
}

/**
 * parse_reference_link - Parse reference-style links
 *
 * Handles: [text][ref], [text][], [text]
 */
static Item parse_reference_link(MarkupParser* parser, const char** text,
                                  const char* text_start, const char* text_end) {
    const char* pos = text_end + 1; // after closing ]
    size_t link_text_len = text_end - text_start;

    const char* ref_start = nullptr;
    const char* ref_end = nullptr;

    // Check what follows the first ]
    if (*pos == '[') {
        // [text][ref] or [text][] form
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

        // If ref is empty [], use link text as ref
        if (ref_end == ref_start) {
            ref_start = text_start;
            ref_end = text_end;
        }
    } else {
        // Shortcut reference link [text]
        ref_start = text_start;
        ref_end = text_end;
    }

    // Look up the reference
    const LinkDefinition* def = parser->getLinkDefinition(ref_start, ref_end - ref_start);
    if (!def) {
        // Not a valid reference link
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create link from definition
    Item result = create_link_from_definition(parser, def, text_start, link_text_len);
    if (result.item != ITEM_ERROR && result.item != ITEM_UNDEFINED) {
        *text = pos;
    }
    return result;
}

/**
 * try_parse_inline_link_syntax - Try to parse (url "title") syntax
 *
 * Returns true if successful and sets url/title output parameters.
 * Returns false if syntax invalid (caller should try reference link).
 */
static bool try_parse_inline_link_syntax(const char* start, const char** out_end,
                                          const char** url_start_out, const char** url_end_out,
                                          const char** title_start_out, const char** title_end_out) {
    const char* pos = start;

    // Must start with (
    if (*pos != '(') return false;
    pos++; // Skip (

    const char* url_start = pos;
    const char* url_end = nullptr;
    const char* title_start = nullptr;
    const char* title_end = nullptr;
    int paren_depth = 1;

    // Skip leading whitespace in URL
    while (*pos == ' ' || *pos == '\t') pos++;

    // Check for angle-bracketed URL: <url>
    if (*pos == '<') {
        pos++; // Skip <
        url_start = pos;

        // Find closing >
        while (*pos && *pos != '>' && *pos != '\n') {
            if (*pos == '\\' && *(pos + 1)) {
                pos += 2;
            } else if (*pos == '<') {
                // Unescaped < not allowed
                return false;
            } else {
                pos++;
            }
        }

        if (*pos != '>') return false;
        url_end = pos;
        pos++; // Skip >

        // Skip whitespace after URL
        while (*pos == ' ' || *pos == '\t') pos++;

        // Check for optional title
        if (*pos == '"' || *pos == '\'') {
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
        }

        // Skip trailing whitespace and expect )
        while (*pos == ' ' || *pos == '\t') pos++;
        if (*pos != ')') return false;
        pos++; // Skip )
    } else {
        url_start = pos;

        // Parse URL and optional title
        while (*pos && paren_depth > 0) {
            if (*pos == '\\' && *(pos + 1)) {
                pos += 2;
                continue;
            }

            if (*pos == '(') paren_depth++;
            else if (*pos == ')') paren_depth--;

            // For non-angle-bracket URLs, space terminates URL and starts potential title
            if (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') {
                if (!url_end) url_end = pos;
                // Skip whitespace
                while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;
                // After whitespace, check if it's a valid continuation
                if (*pos == ')') {
                    paren_depth--;
                    break;
                } else if (*pos == '"' || *pos == '\'' || *pos == '(') {
                    // Title starts
                    char quote = *pos;
                    char close_quote = (quote == '(') ? ')' : quote;
                    pos++; // Skip opening quote
                    title_start = pos;

                    // Find closing quote
                    while (*pos && *pos != close_quote) {
                        if (*pos == '\\' && *(pos + 1)) {
                            pos += 2;
                        } else {
                            pos++;
                        }
                    }
                    if (*pos == close_quote) {
                        title_end = pos;
                        pos++; // Skip closing quote
                    }
                    // Skip whitespace after title
                    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;
                    if (*pos != ')') return false;
                    pos++;  // Skip final )
                    paren_depth--;
                    break;
                } else {
                    // Space inside URL without angle brackets - invalid
                    return false;
                }
            }

            if (paren_depth == 0) {
                if (!url_end) url_end = pos;
                pos++;  // Skip final )
                break;
            }
            pos++;
        }

        if (paren_depth > 0) return false;
    }

    // Success - set output parameters
    *out_end = pos;
    *url_start_out = url_start;
    *url_end_out = url_end;
    *title_start_out = title_start;
    *title_end_out = title_end;
    return true;
}

/**
 * parse_link - Parse inline links
 *
 * Handles: [text](url), [text](url "title"), [text][ref], [text][], [text]
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

    // Check for (url) after ] for inline link
    if (*pos == '(') {
        // Try inline link syntax
        const char* url_start = nullptr;
        const char* url_end = nullptr;
        const char* title_start = nullptr;
        const char* title_end = nullptr;
        const char* new_pos = nullptr;

        if (try_parse_inline_link_syntax(pos, &new_pos, &url_start, &url_end, &title_start, &title_end)) {
            // Inline link syntax succeeded - create link element
            Element* link = create_element(parser, "a");
            if (!link) {
                *text = new_pos;
                return Item{.item = ITEM_ERROR};
            }

            // Add href attribute (unescape backslash escapes and entities)
            if (url_end && url_end > url_start) {
                size_t url_len = url_end - url_start;
                char* url = unescape_string(url_start, url_len);
                if (url) {
                    add_attribute_to_element(parser, link, "href", url);
                    free(url);
                }
            } else {
                // Empty URL
                add_attribute_to_element(parser, link, "href", "");
            }

            // Add title attribute if present (unescape backslash escapes and entities)
            if (title_start && title_end && title_end > title_start) {
                size_t title_len = title_end - title_start;
                char* title = unescape_string(title_start, title_len);
                if (title) {
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

            *text = new_pos;
            return Item{.item = (uint64_t)link};
        }
        // Inline link syntax failed - fall through to try reference link
    }

    // Try reference-style link: [text][ref], [text][], or [text]
    return parse_reference_link(parser, text, text_start, text_end);
}

} // namespace markup
} // namespace lambda
