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

// Helper: Create symbol from parser
static inline Symbol* create_symbol(MarkupParser* parser, const char* text) {
    return parser->builder.createSymbol(text);
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

// ============================================================================
// Emoji Shortcode Mapping Table
// ============================================================================

struct EmojiEntry {
    const char* shortcode;
    const char* emoji;
};

// Common emoji mappings
static const EmojiEntry emoji_map[] = {
    {":smile:", "😄"},
    {":grinning:", "😀"},
    {":laughing:", "😆"},
    {":heart:", "❤️"},
    {":+1:", "👍"},
    {":thumbsup:", "👍"},
    {":thumbsdown:", "👎"},
    {":star:", "⭐"},
    {":fire:", "🔥"},
    {":rocket:", "🚀"},
    {":warning:", "⚠️"},
    {":check:", "✓"},
    {":x:", "✗"},
    {":info:", "ℹ️"},
    {":question:", "❓"},
    {":exclamation:", "❗"},
    {":eyes:", "👀"},
    {":wave:", "👋"},
    {":clap:", "👏"},
    {":muscle:", "💪"},
    {":thinking:", "🤔"},
    {":sunglasses:", "😎"},
    {":tada:", "🎉"},
    {":sparkles:", "✨"},
    {":coffee:", "☕"},
    {":beer:", "🍺"},
    {":pizza:", "🍕"},
    {":bug:", "🐛"},
    {":memo:", "📝"},
    {":bulb:", "💡"},
    {":zap:", "⚡"},
    {":lock:", "🔒"},
    {":key:", "🔑"},
    {":gear:", "⚙️"},
    {":link:", "🔗"},
    {":hammer:", "🔨"},
    {":wrench:", "🔧"},
    {":package:", "📦"},
    {":calendar:", "📅"},
    {":clock:", "🕐"},
    {":hourglass:", "⏳"},
    {nullptr, nullptr}  // Sentinel
};

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

    // Create content string (simple text, no nested parsing for safety)
    char* content = (char*)malloc(content_len + 1);
    if (content) {
        strncpy(content, content_start, content_len);
        content[content_len] = '\0';

        String* content_str = create_string(parser, content);
        if (content_str) {
            Item content_item = {.item = s2it(content_str)};
            list_push((List*)del_elem, content_item);
            increment_element_content_length(del_elem);
        }
        free(content);
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
    while (*pos && *pos != '^' && !isspace(*pos)) {
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
    char* content = (char*)malloc(content_len + 1);
    if (content) {
        strncpy(content, content_start, content_len);
        content[content_len] = '\0';

        String* content_str = create_string(parser, content);
        if (content_str) {
            Item text_item = {.item = s2it(content_str)};
            list_push((List*)sup_elem, text_item);
            increment_element_content_length(sup_elem);
        }
        free(content);
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
    while (*pos && *pos != '~' && !isspace(*pos)) {
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

    char* content = (char*)malloc(content_len + 1);
    if (content) {
        strncpy(content, content_start, content_len);
        content[content_len] = '\0';

        String* content_str = create_string(parser, content);
        if (content_str) {
            Item text_item = {.item = s2it(content_str)};
            list_push((List*)sub_elem, text_item);
            increment_element_content_length(sub_elem);
        }
        free(content);
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
    while (*pos && (isalnum(*pos) || *pos == '_' || *pos == '+' || *pos == '-')) {
        pos++;
    }

    if (*pos != ':' || pos == name_start) {
        // No closing : or empty name
        return Item{.item = ITEM_UNDEFINED};
    }

    // Extract shortcode name
    size_t name_len = pos - name_start;
    char* shortcode_name = (char*)malloc(name_len + 1);
    if (!shortcode_name) {
        return Item{.item = ITEM_ERROR};
    }
    strncpy(shortcode_name, name_start, name_len);
    shortcode_name[name_len] = '\0';

    // Build full shortcode with colons for lookup
    char* full_shortcode = (char*)malloc(name_len + 3);
    if (!full_shortcode) {
        free(shortcode_name);
        return Item{.item = ITEM_ERROR};
    }
    full_shortcode[0] = ':';
    strncpy(full_shortcode + 1, shortcode_name, name_len);
    full_shortcode[name_len + 1] = ':';
    full_shortcode[name_len + 2] = '\0';

    // Look up emoji in table
    const char* emoji_char = nullptr;
    for (int i = 0; emoji_map[i].shortcode; i++) {
        if (strcmp(full_shortcode, emoji_map[i].shortcode) == 0) {
            emoji_char = emoji_map[i].emoji;
            break;
        }
    }

    free(full_shortcode);

    if (!emoji_char) {
        // Unknown emoji shortcode
        free(shortcode_name);
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create Symbol with the shortcode name
    Symbol* symbol_str = create_symbol(parser, shortcode_name);
    free(shortcode_name);

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
    char* id = (char*)malloc(id_len + 1);
    if (id) {
        strncpy(id, id_start, id_len);
        id[id_len] = '\0';
        add_attribute_to_element(parser, ref, "ref", id);
        free(id);
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
    char* key = (char*)malloc(key_len + 1);
    if (key) {
        strncpy(key, key_start, key_len);
        key[key_len] = '\0';
        add_attribute_to_element(parser, citation, "key", key);
        free(key);
    }

    // Check for additional citation info (page numbers, etc.)
    if (*pos == ',' || *pos == ' ') {
        // Skip to info part
        while (*pos == ' ' || *pos == ',') pos++;

        const char* info_start = pos;
        while (*pos && *pos != ']') pos++;

        if (pos > info_start) {
            size_t info_len = pos - info_start;
            char* info = (char*)malloc(info_len + 1);
            if (info) {
                strncpy(info, info_start, info_len);
                info[info_len] = '\0';
                add_attribute_to_element(parser, citation, "info", info);
                free(info);
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

#include "../../html_entities.h"

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
    const char* pos = *text;

    // Must start with &
    if (*pos != '&') {
        return Item{.item = ITEM_UNDEFINED};
    }

    pos++; // Skip &

    char decoded[8] = {0}; // UTF-8 can be up to 4 bytes + null
    bool valid = false;

    if (*pos == '#') {
        // Numeric character reference
        pos++; // Skip #

        uint32_t codepoint = 0;
        const char* num_start = pos;

        if (*pos == 'x' || *pos == 'X') {
            // Hexadecimal: &#xHHHH;
            pos++; // Skip x
            num_start = pos;

            while ((*pos >= '0' && *pos <= '9') ||
                   (*pos >= 'a' && *pos <= 'f') ||
                   (*pos >= 'A' && *pos <= 'F')) {
                codepoint *= 16;
                if (*pos >= '0' && *pos <= '9') {
                    codepoint += *pos - '0';
                } else if (*pos >= 'a' && *pos <= 'f') {
                    codepoint += *pos - 'a' + 10;
                } else {
                    codepoint += *pos - 'A' + 10;
                }
                pos++;

                // Prevent overflow
                if (codepoint > 0x10FFFF) {
                    return Item{.item = ITEM_UNDEFINED};
                }
            }

            if (pos > num_start && *pos == ';') {
                valid = true;
                pos++; // Skip ;
            }
        } else {
            // Decimal: &#NNNN;
            while (*pos >= '0' && *pos <= '9') {
                codepoint = codepoint * 10 + (*pos - '0');
                pos++;

                // Prevent overflow
                if (codepoint > 0x10FFFF) {
                    return Item{.item = ITEM_UNDEFINED};
                }
            }

            if (pos > num_start && *pos == ';') {
                valid = true;
                pos++; // Skip ;
            }
        }

        if (valid) {
            // Convert codepoint to UTF-8
            // Handle special case: codepoint 0 becomes replacement character
            if (codepoint == 0) {
                codepoint = 0xFFFD; // Unicode replacement character
            }

            int len = unicode_to_utf8(codepoint, decoded);
            if (len == 0) {
                return Item{.item = ITEM_UNDEFINED};
            }
        }
    } else {
        // Named entity: &name;
        const char* name_start = pos;

        // Entity names are alphanumeric
        while ((*pos >= 'a' && *pos <= 'z') ||
               (*pos >= 'A' && *pos <= 'Z') ||
               (*pos >= '0' && *pos <= '9')) {
            pos++;
        }

        if (pos > name_start && *pos == ';') {
            size_t name_len = pos - name_start;
            EntityResult result = html_entity_resolve(name_start, name_len);

            if (result.type == ENTITY_ASCII_ESCAPE || result.type == ENTITY_UNICODE_MULTI) {
                // Direct decode for lt, gt, amp, quot, apos and multi-codepoint entities
                strncpy(decoded, result.decoded, sizeof(decoded) - 1);
                valid = true;
                pos++; // Skip ;
            } else if (result.type == ENTITY_UNICODE_SPACE || result.type == ENTITY_NAMED) {
                // Decode Unicode codepoint
                uint32_t cp = result.named.codepoint;
                int len = unicode_to_utf8(cp, decoded);
                if (len > 0) {
                    valid = true;
                    pos++; // Skip ;
                }
            }
        }
    }

    if (!valid) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create string with decoded character
    String* str = create_string(parser, decoded);
    if (!str) {
        return Item{.item = ITEM_ERROR};
    }

    *text = pos;
    return Item{.item = s2it(str)};
}

} // namespace markup
} // namespace lambda
