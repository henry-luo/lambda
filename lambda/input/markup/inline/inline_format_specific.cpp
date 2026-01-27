/**
 * inline_format_specific.cpp - Format-specific inline parsers
 *
 * Parses format-specific inline elements:
 * - RST: ``literal``, reference_
 * - AsciiDoc: inline formatting
 *
 * Phase 3 of Markup Parser Refactoring:
 * Extracted from input-markup.cpp RST/AsciiDoc inline functions
 */
#include "inline_common.hpp"
#include <cstdlib>
#include <cstring>
#include <cctype>

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
 * parse_rst_double_backtick_literal - Parse RST literal text
 *
 * Handles: ``literal text``
 *
 * RST uses double backticks for inline literals (similar to code spans).
 */
Item parse_rst_double_backtick_literal(MarkupParser* parser, const char** text) {
    const char* pos = *text;

    // Must start with ``
    if (*pos != '`' || *(pos + 1) != '`') {
        return Item{.item = ITEM_UNDEFINED};
    }

    const char* start = pos + 2; // Skip opening ``
    pos = start;
    const char* end = nullptr;

    // Find closing ``
    while (*pos != '\0' && *(pos + 1) != '\0') {
        if (*pos == '`' && *(pos + 1) == '`') {
            end = pos;
            break;
        }
        pos++;
    }

    if (!end) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create code element
    Element* code_elem = create_element(parser, "code");
    if (!code_elem) {
        *text = end + 2;
        return Item{.item = ITEM_ERROR};
    }

    // Add RST-specific attribute
    add_attribute_to_element(parser, code_elem, "type", "literal");

    // Extract content between markers
    size_t content_len = end - start;
    char* content = (char*)malloc(content_len + 1);
    if (content) {
        strncpy(content, start, content_len);
        content[content_len] = '\0';

        String* code_str = create_string(parser, content);
        if (code_str) {
            list_push((List*)code_elem, Item{.item = s2it(code_str)});
            increment_element_content_length(code_elem);
        }
        free(content);
    }

    *text = end + 2; // Skip closing ``
    return Item{.item = (uint64_t)code_elem};
}

/**
 * parse_rst_trailing_underscore_reference - Parse RST references
 *
 * Handles: reference_ (trailing underscore indicates a reference)
 *
 * In RST, a word followed by underscore is a reference to a target.
 */
Item parse_rst_trailing_underscore_reference(MarkupParser* parser, const char** text) {
    const char* pos = *text;

    // Must be at underscore
    if (*pos != '_') {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Work backwards to find start of reference word
    // This is tricky since we're called at the underscore position
    // We need access to the full line to look back
    const char* current_line = parser->lines[parser->current_line];
    if (!current_line) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Calculate position relative to line start
    ptrdiff_t offset_in_line = pos - current_line;
    if (offset_in_line <= 0) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Find start of reference (word before underscore)
    const char* ref_start = pos - 1;
    while (ref_start > current_line && !isspace(*(ref_start - 1))) {
        ref_start--;
    }

    if (ref_start >= pos) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Extract reference text
    size_t ref_len = pos - ref_start;
    char* ref_text = (char*)malloc(ref_len + 1);
    if (!ref_text) {
        (*text)++;
        return Item{.item = ITEM_ERROR};
    }

    strncpy(ref_text, ref_start, ref_len);
    ref_text[ref_len] = '\0';

    // Create reference element (rendered as a link)
    Element* ref_elem = create_element(parser, "a");
    if (!ref_elem) {
        free(ref_text);
        (*text)++;
        return Item{.item = ITEM_ERROR};
    }

    // Add href (the reference name, to be resolved later)
    add_attribute_to_element(parser, ref_elem, "href", ref_text);
    add_attribute_to_element(parser, ref_elem, "class", "reference");

    // Add link text
    String* link_text = create_string(parser, ref_text);
    if (link_text) {
        list_push((List*)ref_elem, Item{.item = s2it(link_text)});
        increment_element_content_length(ref_elem);
    }

    free(ref_text);
    (*text)++; // Skip _
    return Item{.item = (uint64_t)ref_elem};
}

/**
 * parse_asciidoc_inline - Parse AsciiDoc inline content
 *
 * Handles AsciiDoc-specific inline syntax:
 * - *bold* and **bold**
 * - _italic_ and __italic__
 * - `monospace`
 * - ^superscript^
 * - ~subscript~
 * - http://url[Link Text]
 *
 * Note: This is a simplified implementation. Full AsciiDoc inline
 * parsing is more complex with constrained/unconstrained formatting.
 */
Item parse_asciidoc_inline(MarkupParser* parser, const char* text) {
    if (!text || strlen(text) == 0) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Safety check for overly long content
    size_t text_len = strlen(text);
    if (text_len > 10000) {
        // For very long content, just return as plain text
        String* plain = create_string(parser, text);
        return Item{.item = s2it(plain)};
    }

    // Check for simple cases without markup
    if (!strpbrk(text, "*_`^~[")) {
        String* plain = create_string(parser, text);
        return Item{.item = s2it(plain)};
    }

    // Create span container
    Element* span = create_element(parser, "span");
    if (!span) {
        String* plain = create_string(parser, text);
        return Item{.item = s2it(plain)};
    }

    // For now, return text as-is in the span
    // Full AsciiDoc inline parsing would require more complex logic
    String* content = create_string(parser, text);
    if (content) {
        list_push((List*)span, Item{.item = s2it(content)});
        increment_element_content_length(span);
    }

    return Item{.item = (uint64_t)span};
}

/**
 * Helper: check if character can be a word boundary for Org emphasis
 *
 * Org-mode emphasis requires markers to be bounded by whitespace or punctuation.
 */
static inline bool is_org_word_boundary(char c) {
    return c == '\0' || isspace((unsigned char)c) ||
           c == '(' || c == ')' || c == '[' || c == ']' ||
           c == '{' || c == '}' || c == '<' || c == '>' ||
           c == ',' || c == '.' || c == ';' || c == ':' ||
           c == '!' || c == '?' || c == '\'' || c == '"' ||
           c == '-' || c == '\n' || c == '\r';
}

/**
 * is_preceded_by_org_boundary - Check if position is preceded by a word boundary
 */
static inline bool is_preceded_by_org_boundary(const char* text, const char* pos) {
    if (pos <= text) return true;  // start of string is a boundary
    return is_org_word_boundary(*(pos - 1));
}

/**
 * parse_org_emphasis - Parse Org-mode emphasis
 *
 * Handles Org-mode inline emphasis:
 * - /italic/ (slashes for italic)
 * - =code= (equals for verbatim/code)
 * - ~verbatim~ (tildes for verbatim)
 * - +strikethrough+ (plus for strikethrough)
 *
 * Org emphasis rules:
 * - Markers must be at word boundaries
 * - Content cannot contain the marker character
 * - Markers cannot be adjacent to whitespace on the inside
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @param text_start Start of the full text (for boundary context)
 * @return Item containing formatted element, or ITEM_UNDEFINED if not matched
 */
Item parse_org_emphasis(MarkupParser* parser, const char** text, const char* text_start) {
    const char* pos = *text;
    const char* full_text = text_start ? text_start : pos;
    char marker = *pos;

    // Org emphasis markers: / = ~ +
    if (marker != '/' && marker != '=' && marker != '~' && marker != '+') {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Check that we're at a word boundary (start of text or after whitespace/punct)
    if (!is_preceded_by_org_boundary(full_text, pos)) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Content starts after the marker
    const char* content_start = pos + 1;

    // Content cannot start with whitespace
    if (*content_start == ' ' || *content_start == '\t' ||
        *content_start == '\n' || *content_start == '\r') {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Find closing marker
    const char* search = content_start;
    const char* close_pos = nullptr;

    while (*search) {
        if (*search == marker) {
            // Check that we're at a word boundary (end of text or before whitespace/punct)
            if (is_org_word_boundary(*(search + 1))) {
                // Content cannot end with whitespace
                if (search > content_start &&
                    *(search - 1) != ' ' && *(search - 1) != '\t' &&
                    *(search - 1) != '\n' && *(search - 1) != '\r') {
                    close_pos = search;
                    break;
                }
            }
        }
        // Org emphasis cannot span multiple lines
        if (*search == '\n' || *search == '\r') {
            return Item{.item = ITEM_UNDEFINED};
        }
        search++;
    }

    if (!close_pos) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Determine element type based on marker
    const char* tag = nullptr;
    switch (marker) {
        case '/': tag = "em"; break;      // italic
        case '=': tag = "code"; break;    // verbatim/code
        case '~': tag = "code"; break;    // verbatim
        case '+': tag = "del"; break;     // strikethrough
        default: return Item{.item = ITEM_UNDEFINED};
    }

    // Create element
    Element* elem = create_element(parser, tag);
    if (!elem) {
        return Item{.item = ITEM_ERROR};
    }

    // Extract content
    size_t content_len = close_pos - content_start;

    // For code/verbatim, don't parse inner content
    if (marker == '=' || marker == '~') {
        char* content = (char*)malloc(content_len + 1);
        if (content) {
            memcpy(content, content_start, content_len);
            content[content_len] = '\0';
            String* content_str = create_string(parser, content);
            if (content_str) {
                list_push((List*)elem, Item{.item = s2it(content_str)});
                increment_element_content_length(elem);
            }
            free(content);
        }
    } else {
        // For emphasis (italic, strikethrough), parse inner content recursively
        char* content = (char*)malloc(content_len + 1);
        if (content) {
            memcpy(content, content_start, content_len);
            content[content_len] = '\0';

            Item inner = parse_inline_spans(parser, content);
            if (inner.item != ITEM_ERROR && inner.item != ITEM_UNDEFINED) {
                list_push((List*)elem, inner);
                increment_element_content_length(elem);
            }
            free(content);
        }
    }

    // Advance position past the closing marker
    *text = close_pos + 1;

    return Item{.item = (uint64_t)elem};
}

/**
 * parse_org_link - Parse Org-mode links
 *
 * Handles: [[url]] or [[url][description]]
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing link element, or ITEM_UNDEFINED if not matched
 */
Item parse_org_link(MarkupParser* parser, const char** text) {
    const char* pos = *text;

    // Must start with [[
    if (pos[0] != '[' || pos[1] != '[') {
        return Item{.item = ITEM_UNDEFINED};
    }

    pos += 2;  // skip [[
    const char* url_start = pos;

    // Find ][ or ]]
    while (*pos && !(pos[0] == ']' && (pos[1] == ']' || pos[1] == '['))) {
        pos++;
    }

    if (!*pos) {
        return Item{.item = ITEM_UNDEFINED};
    }

    const char* url_end = pos;
    size_t url_len = url_end - url_start;

    const char* text_start = nullptr;
    const char* text_end = nullptr;

    if (pos[1] == '[') {
        // Has description: [[url][description]]
        pos += 2;  // skip ][
        text_start = pos;

        // Find closing ]]
        while (*pos && !(pos[0] == ']' && pos[1] == ']')) {
            pos++;
        }

        if (pos[0] != ']' || pos[1] != ']') {
            return Item{.item = ITEM_UNDEFINED};
        }

        text_end = pos;
        pos += 2;  // skip ]]
    } else {
        // No description: [[url]]
        text_start = url_start;
        text_end = url_end;
        pos += 2;  // skip ]]
    }

    // Create link element
    Element* link = create_element(parser, "a");
    if (!link) {
        return Item{.item = ITEM_ERROR};
    }

    // Add href attribute
    char* url = (char*)malloc(url_len + 1);
    if (url) {
        memcpy(url, url_start, url_len);
        url[url_len] = '\0';
        add_attribute_to_element(parser, link, "href", url);
        free(url);
    }

    // Add link text
    size_t text_len = text_end - text_start;
    char* link_text = (char*)malloc(text_len + 1);
    if (link_text) {
        memcpy(link_text, text_start, text_len);
        link_text[text_len] = '\0';

        // Parse link text for inline formatting
        Item inner = parse_inline_spans(parser, link_text);
        if (inner.item != ITEM_ERROR && inner.item != ITEM_UNDEFINED) {
            list_push((List*)link, inner);
            increment_element_content_length(link);
        }
        free(link_text);
    }

    *text = pos;
    return Item{.item = (uint64_t)link};
}

} // namespace markup
} // namespace lambda
