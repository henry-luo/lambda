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

} // namespace markup
} // namespace lambda
