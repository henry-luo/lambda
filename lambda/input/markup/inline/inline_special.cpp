/**
 * inline_special.cpp - Special inline element parsers
 *
 * Parses special inline elements:
 * - ~~strikethrough~~
 * - ^superscript^
 * - ~subscript~
 * - :emoji:
 * - [^footnote]
 * - [@citation]
 *
 * Phase 3 of Markup Parser Refactoring:
 * Extracted from input-markup.cpp various parse functions
 */
#include "inline_common.hpp"
#include <cstring>
#include "lib/emoji_shortcodes.h"
#include "lib/str.h"

namespace lambda {
namespace markup {

/**
 * parse_strikethrough - Parse strikethrough text
 *
 * Handles: ~~text~~ (double tilde) and ~text~ (single tilde, md4c extension)
 * GFM uses <del> tag for strikethrough output.
 *
 * Rules per GFM spec:
 * - Only 1 or 2 tildes allowed (not 3+)
 * - Opening delimiter must be left-flanking (followed by non-whitespace)
 * - Closing delimiter must be right-flanking (preceded by non-whitespace)
 * - Opening and closing delimiter lengths must match
 */
Item parse_strikethrough(MarkupParser* parser, const char** text) {
    const char* start = *text;

    // Check for opening ~
    if (*start != '~') {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Count consecutive tildes
    int tilde_count = 1;
    while (*(start + tilde_count) == '~') {
        tilde_count++;
    }

    // Only 1 or 2 tildes are valid for strikethrough (not 3+)
    if (tilde_count > 2) {
        return Item{.item = ITEM_UNDEFINED};
    }

    int delim_len = tilde_count;
    const char* pos = start + delim_len;
    const char* content_start = pos;

    // Check left-flanking: opening delimiter must be followed by non-whitespace
    if (!*pos || *pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Find matching closing delimiter
    // Must be:
    // - Same number of tildes as opening
    // - Preceded by non-whitespace (right-flanking)
    // - Either end of string or followed by non-tilde (exact match)
    while (*pos) {
        if (*pos == '~') {
            // Count consecutive tildes at this position
            int close_count = 1;
            while (*(pos + close_count) == '~') {
                close_count++;
            }

            // Check if this matches our opening delimiter length
            if (close_count == delim_len) {
                // Check right-flanking: must be preceded by non-whitespace
                char prev_char = *(pos - 1);
                if (prev_char != ' ' && prev_char != '\t' && prev_char != '\n' && prev_char != '\r') {
                    // Valid closing delimiter found
                    break;
                }
            }
            // Skip all tildes in this run
            pos += close_count;
            continue;
        }
        pos++;
    }

    if (!*pos) {
        // No closing delimiter found
        return Item{.item = ITEM_UNDEFINED};
    }

    // Extract content between delimiters
    size_t content_len = pos - content_start;
    if (content_len == 0) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create strikethrough element (GFM uses <del>)
    Element* del_elem = create_element(parser, "del");
    if (!del_elem) {
        return Item{.item = ITEM_ERROR};
    }

    // Parse inner content (may contain bold, italic, etc.)
    char* content = mem_strndup(content_start, content_len, MEM_CAT_INPUT_MARKUP);
    if (content) {
        Item inner = parse_inline_spans(parser, content);
        if (inner.item != ITEM_ERROR && inner.item != ITEM_UNDEFINED) {
            list_push((List*)del_elem, inner);
        }
        mem_free(content);
    }

    *text = pos + delim_len; // Skip closing delimiter
    return Item{.item = (uint64_t)del_elem};
}

/**
 * parse_superscript - Parse superscript text
 *
 * Handles: ^text^
 */
Item parse_superscript(MarkupParser* parser, const char** text) {
    const char* start = *text;

    // Check for opening ^
    if (*start != '^') {
        return Item{.item = ITEM_UNDEFINED};
    }

    const char* pos = start + 1;
    const char* content_start = pos;

    // Find closing ^ (not at beginning, no whitespace)
    while (*pos && *pos != '^' && !str_char_is_ascii_space(*pos)) {
        pos++;
    }

    if (*pos != '^' || pos == content_start) {
        // No proper closing ^ or empty content
        return Item{.item = ITEM_UNDEFINED};
    }

    // Extract content between ^
    size_t content_len = pos - content_start;

    // Create superscript element
    Element* sup_elem = create_element(parser, "sup");
    if (!sup_elem) {
        return Item{.item = ITEM_ERROR};
    }

    // Create content string
    char* content = mem_strndup(content_start, content_len, MEM_CAT_INPUT_MARKUP);
    if (content) {
        String* content_str = create_string(parser, content);
        if (content_str) {
            Item text_item = {.item = s2it(content_str)};
            list_push((List*)sup_elem, text_item);
        }
        mem_free(content);
    }

    *text = pos + 1; // Skip closing ^
    return Item{.item = (uint64_t)sup_elem};
}

/**
 * parse_subscript - Parse subscript text
 *
 * Handles: ~text~ (single tilde, not double)
 */
Item parse_subscript(MarkupParser* parser, const char** text) {
    const char* start = *text;

    // Check for single ~ (not ~~)
    if (*start != '~' || *(start + 1) == '~') {
        return Item{.item = ITEM_UNDEFINED};
    }

    const char* pos = start + 1;
    const char* content_start = pos;

    // Find closing ~ (not at beginning, no whitespace)
    while (*pos && *pos != '~' && !str_char_is_ascii_space(*pos)) {
        pos++;
    }

    if (*pos != '~' || pos == content_start) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Extract content
    size_t content_len = pos - content_start;

    // Create subscript element
    Element* sub_elem = create_element(parser, "sub");
    if (!sub_elem) {
        return Item{.item = ITEM_ERROR};
    }

    char* content = mem_strndup(content_start, content_len, MEM_CAT_INPUT_MARKUP);
    if (content) {
        String* content_str = create_string(parser, content);
        if (content_str) {
            Item text_item = {.item = s2it(content_str)};
            list_push((List*)sub_elem, text_item);
        }
        mem_free(content);
    }

    *text = pos + 1;
    return Item{.item = (uint64_t)sub_elem};
}

/**
 * parse_emoji_shortcode - Parse emoji shortcodes
 *
 * Handles: :smile:, :heart:, etc.
 */
Item parse_emoji_shortcode(MarkupParser* parser, const char** text) {
    const char* start = *text;

    // Must start with :
    if (*start != ':') {
        return Item{.item = ITEM_UNDEFINED};
    }

    const char* pos = start + 1;
    const char* name_start = pos;

    // Find closing : (alphanumeric and _ only)
    while (*pos && (str_char_is_alnum(*pos) || *pos == '_' || *pos == '+' || *pos == '-')) {
        pos++;
    }

    if (*pos != ':' || pos == name_start) {
        // No closing : or empty name
        return Item{.item = ITEM_UNDEFINED};
    }

    size_t name_len = pos - name_start;
    // Only create a Symbol when the shared renderer can resolve its name.
    if (!emoji_shortcode_lookup(name_start, name_len)) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Extract shortcode name
    char* shortcode_name = mem_strndup(name_start, name_len, MEM_CAT_INPUT_MARKUP);
    if (!shortcode_name) {
        return Item{.item = ITEM_ERROR};
    }

    // Create Symbol with the shortcode name
    Symbol* symbol_str = create_symbol(parser, shortcode_name);
    mem_free(shortcode_name);

    if (!symbol_str) {
        return Item{.item = ITEM_ERROR};
    }

    *text = pos + 1; // Skip closing :
    return Item{.item = y2it(symbol_str)};
}

/**
 * parse_footnote_reference - Parse footnote references
 *
 * Handles: [^1], [^ref]
 */
Item parse_footnote_reference(MarkupParser* parser, const char** text) {
    const char* pos = *text;

    // Check for [^
    if (*pos != '[' || *(pos + 1) != '^') {
        return Item{.item = ITEM_UNDEFINED};
    }

    pos += 2; // Skip [^
    const char* id_start = pos;

    // Find closing ]
    while (*pos && *pos != ']') pos++;

    if (*pos != ']' || pos == id_start) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create footnote-ref element
    Element* ref = create_element(parser, "footnote-ref");
    if (!ref) {
        *text = pos + 1;
        return Item{.item = ITEM_ERROR};
    }

    // Extract and add ID
    size_t id_len = pos - id_start;
    char* id = mem_strndup(id_start, id_len, MEM_CAT_INPUT_MARKUP);
    if (id) {
        add_attribute_to_element(parser, ref, "ref", id);
        mem_free(id);
    }

    *text = pos + 1; // Skip closing ]
    return Item{.item = (uint64_t)ref};
}

/**
 * parse_citation - Parse citations
 *
 * Handles: [@key], [@key, p. 123]
 */
Item parse_citation(MarkupParser* parser, const char** text) {
    const char* pos = *text;

    // Check for [@
    if (*pos != '[' || *(pos + 1) != '@') {
        return Item{.item = ITEM_UNDEFINED};
    }

    pos += 2; // Skip [@
    const char* key_start = pos;

    // Find end of citation key (space, comma, or ])
    while (*pos && *pos != ' ' && *pos != ',' && *pos != ']') pos++;

    if (pos == key_start) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create citation element
    Element* citation = create_element(parser, "citation");
    if (!citation) {
        *text = pos;
        return Item{.item = ITEM_ERROR};
    }

    // Extract citation key
    size_t key_len = pos - key_start;
    char* key = mem_strndup(key_start, key_len, MEM_CAT_INPUT_MARKUP);
    if (key) {
        add_attribute_to_element(parser, citation, "key", key);
        mem_free(key);
    }

    // Check for additional citation info (page numbers, etc.)
    if (*pos == ',' || *pos == ' ') {
        // Skip to info part
        while (*pos == ' ' || *pos == ',') pos++;

        const char* info_start = pos;
        while (*pos && *pos != ']') pos++;

        if (pos > info_start) {
            size_t info_len = pos - info_start;
            char* info = mem_strndup(info_start, info_len, MEM_CAT_INPUT_MARKUP);
            if (info) {
                add_attribute_to_element(parser, citation, "info", info);
                mem_free(info);
            }
        }
    }

    // Skip to closing ]
    while (*pos && *pos != ']') pos++;
    if (*pos == ']') pos++;

    *text = pos;
    return Item{.item = (uint64_t)citation};
}

// ============================================================================
// Entity Reference Parsing
// ============================================================================

#include "../../../../lib/html_entities.h"

/**
 * parse_entity_reference - Parse HTML entity and numeric character references
 *
 * Handles:
 * - Named entities: &amp; &lt; &gt; &copy; &mdash; etc.
 * - Decimal numeric: &#35; &#1234;
 * - Hexadecimal numeric: &#x23; &#X1F600;
 *
 * CommonMark spec: Entities are decoded to their character equivalents.
 * Invalid entities are left as literal text.
 */
Item parse_entity_reference(MarkupParser* parser, const char** text) {
    HtmlEntityDecodeResult decoded;
    if (!text || !*text || !html_entity_decode_reference(
            *text, strlen(*text), &decoded)) {
        return Item{.item = ITEM_UNDEFINED};
    }

    String* str = parser->builder.createString(decoded.chars, decoded.length);
    if (!str) {
        return Item{.item = ITEM_ERROR};
    }

    *text += decoded.consumed;
    return Item{.item = s2it(str)};
}

} // namespace markup
} // namespace lambda
