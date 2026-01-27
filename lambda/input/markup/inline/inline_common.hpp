/**
 * inline_common.hpp - Shared interface for inline parsers
 *
 * This header provides common types and function declarations
 * used by inline-level parsers (emphasis, code, link, image, etc.).
 *
 * Phase 3 of Markup Parser Refactoring:
 * Extracted from input-markup.cpp to enable format-agnostic inline parsing.
 */
#ifndef INLINE_COMMON_HPP
#define INLINE_COMMON_HPP

#include "../markup_parser.hpp"
#include "../format_adapter.hpp"
#include <cstdlib>

namespace lambda {
namespace markup {

// ============================================================================
// Forward Declarations (defined in markup_parser.hpp)
// ============================================================================

// parse_inline_spans is declared in markup_parser.hpp

// ============================================================================
// Inline Parser Functions
// ============================================================================

/**
 * parse_emphasis - Parse bold/italic text
 *
 * Handles Markdown-style: **bold**, *italic*, __bold__, _italic_
 * Uses adapter for format-specific delimiters.
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @param text_start Start of the full text (for flanking context), or nullptr
 * @return Item containing emphasis element, or ITEM_UNDEFINED if not matched
 */
Item parse_emphasis(MarkupParser* parser, const char** text, const char* text_start = nullptr);

/**
 * parse_code_span - Parse inline code spans
 *
 * Handles: `code`, ``code with `backticks` ``
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing code element, or ITEM_UNDEFINED if not matched
 */
Item parse_code_span(MarkupParser* parser, const char** text);

/**
 * parse_link - Parse links
 *
 * Handles: [text](url), [text](url "title"), [text][ref]
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing link element, or ITEM_UNDEFINED if not matched
 */
Item parse_link(MarkupParser* parser, const char** text);

/**
 * parse_image - Parse images
 *
 * Handles: ![alt](src), ![alt](src "title")
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing image element, or ITEM_UNDEFINED if not matched
 */
Item parse_image(MarkupParser* parser, const char** text);

/**
 * parse_inline_math - Parse inline math expressions
 *
 * Handles: $expression$
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing math element, or ITEM_UNDEFINED if not matched
 */
Item parse_inline_math(MarkupParser* parser, const char** text);

/**
 * parse_strikethrough - Parse strikethrough text
 *
 * Handles: ~~text~~
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing strikethrough element, or ITEM_UNDEFINED if not matched
 */
Item parse_strikethrough(MarkupParser* parser, const char** text);

/**
 * parse_superscript - Parse superscript
 *
 * Handles: ^text^
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing sup element, or ITEM_UNDEFINED if not matched
 */
Item parse_superscript(MarkupParser* parser, const char** text);

/**
 * parse_subscript - Parse subscript
 *
 * Handles: ~text~
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing sub element, or ITEM_UNDEFINED if not matched
 */
Item parse_subscript(MarkupParser* parser, const char** text);

/**
 * parse_emoji_shortcode - Parse emoji shortcodes
 *
 * Handles: :smile:, :heart:, etc.
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing emoji Symbol, or ITEM_UNDEFINED if not matched
 */
Item parse_emoji_shortcode(MarkupParser* parser, const char** text);

/**
 * parse_footnote_reference - Parse footnote references
 *
 * Handles: [^1], [^ref]
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing footnote-ref element, or ITEM_UNDEFINED if not matched
 */
Item parse_footnote_reference(MarkupParser* parser, const char** text);

/**
 * parse_citation - Parse citations
 *
 * Handles: [@key], [@key, p. 123]
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing citation element, or ITEM_UNDEFINED if not matched
 */
Item parse_citation(MarkupParser* parser, const char** text);

/**
 * parse_entity_reference - Parse HTML entity and numeric character references
 *
 * Handles: &amp; &lt; &gt; &#35; &#x23; &copy; etc.
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing decoded string, or ITEM_UNDEFINED if not matched
 */
Item parse_entity_reference(MarkupParser* parser, const char** text);

/**
 * parse_raw_html - Parse inline raw HTML tags
 *
 * Handles HTML tags, comments, processing instructions, CDATA, and declarations.
 * Tags pass through without markdown processing.
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing raw-html element, or ITEM_UNDEFINED if not matched
 */
Item parse_raw_html(MarkupParser* parser, const char** text);

/**
 * parse_autolink - Parse autolinks
 *
 * Handles: <http://example.com>, <email@example.com>
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing link element, or ITEM_UNDEFINED if not matched
 */
Item parse_autolink(MarkupParser* parser, const char** text);

// ============================================================================
// Format-Specific Inline Parsers
// ============================================================================

/**
 * parse_wiki_link - Parse MediaWiki-style links
 *
 * Handles: [[Page]], [[Page|display]], [[File:image.png]]
 */
Item parse_wiki_link(MarkupParser* parser, const char** text);

/**
 * parse_wiki_external_link - Parse MediaWiki external links
 *
 * Handles: [http://example.com text]
 */
Item parse_wiki_external_link(MarkupParser* parser, const char** text);

/**
 * parse_wiki_bold_italic - Parse MediaWiki-style emphasis
 *
 * Handles: ''italic'', '''bold''', '''''bolditalic'''''
 */
Item parse_wiki_bold_italic(MarkupParser* parser, const char** text);

/**
 * parse_wiki_template - Parse MediaWiki templates
 *
 * Handles: {{template}}, {{template|arg}}
 */
Item parse_wiki_template(MarkupParser* parser, const char** text);

/**
 * parse_asciidoc_inline - Parse AsciiDoc inline elements
 *
 * Handles AsciiDoc-specific inline syntax
 */
Item parse_asciidoc_inline(MarkupParser* parser, const char* text);

/**
 * parse_rst_double_backtick_literal - Parse RST literal text
 *
 * Handles: ``literal``
 */
Item parse_rst_double_backtick_literal(MarkupParser* parser, const char** text);

/**
 * parse_rst_trailing_underscore_reference - Parse RST references
 *
 * Handles: text_
 */
Item parse_rst_trailing_underscore_reference(MarkupParser* parser, const char** text);

/**
 * parse_org_emphasis - Parse Org-mode emphasis
 *
 * Handles: /italic/, =code=, ~verbatim~, +strikethrough+
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @param text_start Start of the full text (for boundary context)
 * @return Item containing formatted element, or ITEM_UNDEFINED if not matched
 */
Item parse_org_emphasis(MarkupParser* parser, const char** text, const char* text_start = nullptr);

/**
 * parse_org_link - Parse Org-mode links
 *
 * Handles: [[url]] or [[url][description]]
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing link element, or ITEM_UNDEFINED if not matched
 */
Item parse_org_link(MarkupParser* parser, const char** text);

/**
 * parse_man_font_escape - Parse man page font escapes
 *
 * Handles: \fB (bold), \fI (italic), \fR/\fP (roman/previous)
 *
 * Man pages use troff font escapes for inline formatting:
 * - \fBbold text\fR → <strong>bold text</strong>
 * - \fIitalic text\fR → <em>italic text</em>
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing formatted element, or ITEM_UNDEFINED if not matched
 */
Item parse_man_font_escape(MarkupParser* parser, const char** text);

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Create an inline element with given tag name
 */
inline Element* create_inline_element(MarkupParser* parser, const char* tag) {
    return parser->builder.element(tag).final().element;
}

/**
 * Add text content to an inline element
 */
inline void add_text_to_element(MarkupParser* parser, Element* elem, const char* text, size_t len) {
    String* str = parser->builder.createString(text, len);
    if (str) {
        Item item = {.item = s2it(str)};
        list_push((List*)elem, item);
        TypeElmt* elmt_type = (TypeElmt*)elem->type;
        elmt_type->content_length++;
    }
}

/**
 * Add child item to an inline element
 */
inline void add_child_to_element(Element* elem, Item child) {
    if (child.item != ITEM_UNDEFINED && child.item != ITEM_ERROR) {
        list_push((List*)elem, child);
        TypeElmt* elmt_type = (TypeElmt*)elem->type;
        elmt_type->content_length++;
    }
}

/**
 * Add attribute to an inline element
 */
inline void add_inline_attribute(MarkupParser* parser, Element* elem,
                                 const char* key, const char* val) {
    String* k = parser->builder.createString(key);
    String* v = parser->builder.createString(val);
    if (k && v) {
        parser->builder.putToElement(elem, k, Item{.item = s2it(v)});
    }
}

/**
 * Check if character at position is escaped
 */
inline bool is_escaped(const char* start, const char* pos) {
    if (pos <= start) return false;
    int backslashes = 0;
    const char* p = pos - 1;
    while (p >= start && *p == '\\') {
        backslashes++;
        p--;
    }
    return (backslashes % 2) == 1;
}

/**
 * Find closing delimiter, respecting escapes
 */
const char* find_closing(const char* start, const char* delimiter);

/**
 * Find the end of an inline element, handling nesting
 */
const char* find_inline_end(const char* start, const char* open, const char* close);

/**
 * Count consecutive occurrences of a character
 */
inline int count_consecutive(const char* pos, char c) {
    int count = 0;
    while (*pos == c) {
        count++;
        pos++;
    }
    return count;
}

/**
 * Find matching closing delimiter with same count
 */
inline const char* find_matching_delimiter(const char* start, char marker, int count) {
    const char* pos = start;
    while (*pos) {
        if (*pos == marker) {
            int found = 0;
            const char* match_start = pos;
            while (*pos == marker) {
                found++;
                pos++;
            }
            if (found >= count) {
                return match_start;
            }
        } else {
            pos++;
        }
    }
    return nullptr;
}

} // namespace markup
} // namespace lambda

#endif // INLINE_COMMON_HPP
