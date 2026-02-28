#include "html5_tokenizer.h"
#include "html5_token.h"
#include "../../../lib/log.h"
#include "../../../lib/str.h"
#include "../../mark_builder.hpp"
#include "../html_entities.h"
#include "../input-utils.h"
#include <string.h>
#include <ctype.h>

// ============================================================================
// FAST TEXT SCANNING - Batch ASCII Processing
// ============================================================================

// Scan a run of ASCII text characters that don't need special handling.
// Returns the number of bytes that can be consumed as a single text token.
// Stops at: '<', '&', '\0', EOF, or non-ASCII bytes (>= 0x80 for UTF-8)
static size_t html5_scan_text_run(Html5Parser* parser) {
    const char* start = parser->html + parser->pos;
    const char* end = parser->html + parser->length;
    const char* p = start;

    while (p < end) {
        unsigned char c = (unsigned char)*p;
        // Stop at delimiters or non-ASCII (UTF-8 continuation)
        if (c == '<' || c == '&' || c == '\0' || c >= 0x80) {
            break;
        }
        p++;
    }
    return p - start;
}

// Scan RCDATA text (stops at '<' and '&' only, allows NULL with error)
static size_t html5_scan_rcdata_run(Html5Parser* parser) {
    const char* start = parser->html + parser->pos;
    const char* end = parser->html + parser->length;
    const char* p = start;

    while (p < end) {
        unsigned char c = (unsigned char)*p;
        if (c == '<' || c == '&' || c == '\0' || c >= 0x80) {
            break;
        }
        p++;
    }
    return p - start;
}

// Scan RAWTEXT (stops at '<' only)
static size_t html5_scan_rawtext_run(Html5Parser* parser) {
    const char* start = parser->html + parser->pos;
    const char* end = parser->html + parser->length;
    const char* p = start;

    while (p < end) {
        unsigned char c = (unsigned char)*p;
        if (c == '<' || c == '\0' || c >= 0x80) {
            break;
        }
        p++;
    }
    return p - start;
}

// ============================================================================
// UTF-8 ITERATOR IMPLEMENTATION
// Based on Bjoern Hoehrmann's DFA-based UTF-8 decoder
// http://bjoern.hoehrmann.de/utf-8/decoder/dfa/
// ============================================================================

#define UTF8_ACCEPT 0
#define UTF8_REJECT 12

// UTF-8 DFA state transition table
static const uint8_t utf8d[] = {
    // Character class lookup table (first 256 entries)
    // Maps bytes to character classes to reduce transition table size
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

    // State transition table (12 states x 12 classes)
    0,12,24,36,60,96,84,12,12,12,48,72,  12,12,12,12,12,12,12,12,12,12,12,12,
    12,0,12,12,12,12,12,0,12,0,12,12,    12,24,12,12,12,12,12,24,12,24,12,12,
    12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
    12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
    12,36,12,12,12,12,12,12,12,12,12,12,
};

// Decode one byte of UTF-8 sequence
static inline uint32_t utf8_decode(uint32_t* state, uint32_t* codep, uint32_t byte) {
    uint32_t type = utf8d[byte];
    *codep = (*state != UTF8_ACCEPT)
        ? (byte & 0x3fu) | (*codep << 6)
        : (0xff >> type) & byte;
    *state = utf8d[256 + *state + type];
    return *state;
}

// Check if codepoint is invalid per HTML5 spec
static bool html5_is_invalid_codepoint(int c) {
    // Control characters that are parse errors
    return (c >= 0x01 && c <= 0x08) ||
           c == 0x0B ||
           (c >= 0x0E && c <= 0x1F) ||
           (c >= 0x7F && c <= 0x9F) ||
           (c >= 0xFDD0 && c <= 0xFDEF) ||
           ((c & 0xFFFF) == 0xFFFE) ||
           ((c & 0xFFFF) == 0xFFFF);
}

// Read the next UTF-8 character from iterator
static void html5_utf8iter_read_char(Html5Utf8Iterator* iter) {
    if (iter->start >= iter->end) {
        // EOF
        iter->current = -1;
        iter->width = 0;
        return;
    }

    uint32_t codepoint = 0;
    uint32_t state = UTF8_ACCEPT;

    for (const char* c = iter->start; c < iter->end; ++c) {
        utf8_decode(&state, &codepoint, (uint32_t)(unsigned char)(*c));

        if (state == UTF8_ACCEPT) {
            iter->width = (int)(c - iter->start + 1);

            // HTML5 spec: normalize CR and CRLF to LF
            if (codepoint == '\r') {
                const char* next = c + 1;
                if (next < iter->end && *next == '\n') {
                    // Skip CR in CRLF sequence
                    iter->start++;
                    iter->pos.offset++;
                }
                codepoint = '\n';
            }

            // Check for invalid codepoints (parse error, but continue)
            if (html5_is_invalid_codepoint((int)codepoint)) {
                codepoint = HTML5_REPLACEMENT_CHAR;
            }

            iter->current = (int)codepoint;
            return;
        } else if (state == UTF8_REJECT) {
            // Invalid UTF-8 sequence - emit replacement character
            iter->width = (int)(c - iter->start + (c == iter->start ? 1 : 0));
            iter->current = HTML5_REPLACEMENT_CHAR;
            return;
        }
    }

    // Truncated UTF-8 sequence at end of input
    iter->current = HTML5_REPLACEMENT_CHAR;
    iter->width = (int)(iter->end - iter->start);
}

// Update position after consuming a character
static void html5_utf8iter_update_position(Html5Utf8Iterator* iter) {
    iter->pos.offset += iter->width;
    if (iter->current == '\n') {
        iter->pos.line++;
        iter->pos.column = 1;
    } else if (iter->current == '\t') {
        // Tab stop at every 8 columns (configurable)
        iter->pos.column = ((iter->pos.column / 8) + 1) * 8;
    } else if (iter->current != -1) {
        iter->pos.column++;
    }
}

// Initialize UTF-8 iterator
void html5_utf8iter_init(Html5Utf8Iterator* iter, const char* input, size_t length) {
    iter->start = input;
    iter->end = input + length;
    iter->mark = input;
    iter->pos.line = 1;
    iter->pos.column = 1;
    iter->pos.offset = 0;
    iter->mark_pos = iter->pos;
    html5_utf8iter_read_char(iter);
}

// Advance to next character
void html5_utf8iter_next(Html5Utf8Iterator* iter) {
    html5_utf8iter_update_position(iter);
    iter->start += iter->width;
    html5_utf8iter_read_char(iter);
}

// Get current codepoint
int html5_utf8iter_current(const Html5Utf8Iterator* iter) {
    return iter->current;
}

// Mark current position for potential backtracking
void html5_utf8iter_mark(Html5Utf8Iterator* iter) {
    iter->mark = iter->start;
    iter->mark_pos = iter->pos;
}

// Reset to marked position
void html5_utf8iter_reset(Html5Utf8Iterator* iter) {
    iter->start = iter->mark;
    iter->pos = iter->mark_pos;
    html5_utf8iter_read_char(iter);
}

// Get pointer to start of current character
const char* html5_utf8iter_get_char_pointer(const Html5Utf8Iterator* iter) {
    return iter->start;
}

// Try to consume a string match (case-sensitive or insensitive)
bool html5_utf8iter_maybe_consume_match(Html5Utf8Iterator* iter, const char* prefix,
                                        size_t length, bool case_sensitive) {
    if (iter->start + length > iter->end) {
        return false;
    }

    bool matched;
    if (case_sensitive) {
        matched = (strncmp(iter->start, prefix, length) == 0);
    } else {
        matched = (str_ieq(iter->start, length, prefix, length));
    }

    if (matched) {
        // Consume the matched bytes
        for (size_t i = 0; i < length; ++i) {
            html5_utf8iter_next(iter);
        }
        return true;
    }
    return false;
}

// ============================================================================
// ERROR LIST IMPLEMENTATION
// ============================================================================

// Error type names for debugging/reporting
static const char* error_type_names[] = {
    "unexpected-null-character",
    "control-character-in-input-stream",
    "noncharacter-in-input-stream",
    "surrogate-in-input-stream",
    "unexpected-character-in-attribute-name",
    "unexpected-equals-sign-before-attribute-name",
    "unexpected-character-in-unquoted-attribute-value",
    "missing-whitespace-between-attributes",
    "unexpected-solidus-in-tag",
    "eof-before-tag-name",
    "eof-in-tag",
    "eof-in-script-html-comment-like-text",
    "invalid-first-character-of-tag-name",
    "missing-end-tag-name",
    "abrupt-closing-of-empty-comment",
    "eof-in-comment",
    "nested-comment",
    "incorrectly-closed-comment",
    "missing-doctype-name",
    "missing-whitespace-before-doctype-name",
    "missing-doctype-public-identifier",
    "missing-doctype-system-identifier",
    "eof-in-doctype",
    "unknown-named-character-reference",
    "missing-semicolon-after-character-reference",
    "absence-of-digits-in-numeric-character-reference",
    "null-character-reference",
    "character-reference-outside-unicode-range",
    "surrogate-character-reference",
    "noncharacter-character-reference",
    "control-character-reference",
    "unexpected-start-tag",
    "unexpected-end-tag",
    "missing-required-end-tag",
    "non-void-html-element-start-tag-with-trailing-solidus",
};

const char* html5_error_type_name(Html5ErrorType type) {
    if (type >= 0 && type < HTML5_ERR_COUNT) {
        return error_type_names[type];
    }
    return "unknown-error";
}

void html5_error_list_init(Html5ErrorList* list, Arena* arena) {
    list->errors = nullptr;
    list->count = 0;
    list->capacity = 0;
    list->arena = arena;
}

static void html5_error_list_grow(Html5ErrorList* list) {
    size_t new_capacity = list->capacity == 0 ? 16 : list->capacity * 2;
    Html5Error* new_errors = (Html5Error*)arena_alloc(list->arena,
                                                       new_capacity * sizeof(Html5Error));
    if (list->errors && list->count > 0) {
        memcpy(new_errors, list->errors, list->count * sizeof(Html5Error));
    }
    list->errors = new_errors;
    list->capacity = new_capacity;
}

void html5_error_list_add(Html5ErrorList* list, Html5ErrorType type,
                          Html5SourcePosition pos, const char* original_text) {
    if (list->count >= list->capacity) {
        html5_error_list_grow(list);
    }
    Html5Error* err = &list->errors[list->count++];
    err->type = type;
    err->position = pos;
    err->original_text = original_text;
    err->v.codepoint = 0;
}

void html5_error_list_add_codepoint(Html5ErrorList* list, Html5ErrorType type,
                                    Html5SourcePosition pos, int codepoint) {
    if (list->count >= list->capacity) {
        html5_error_list_grow(list);
    }
    Html5Error* err = &list->errors[list->count++];
    err->type = type;
    err->position = pos;
    err->original_text = nullptr;
    err->v.codepoint = codepoint;
}

void html5_error_list_add_tag(Html5ErrorList* list, Html5ErrorType type,
                              Html5SourcePosition pos, const char* tag_name) {
    if (list->count >= list->capacity) {
        html5_error_list_grow(list);
    }
    Html5Error* err = &list->errors[list->count++];
    err->type = type;
    err->position = pos;
    err->original_text = nullptr;
    err->v.tag_name = tag_name;
}

// ============================================================================
// CHARACTER CONSUMPTION HELPERS
// ============================================================================

// helper: consume next character from input
char html5_consume_next_char(Html5Parser* parser) {
    if (parser->pos >= parser->length) {
        return '\0';  // EOF
    }
    return parser->html[parser->pos++];
}

// helper: peek at character without consuming
char html5_peek_char(Html5Parser* parser, size_t offset) {
    size_t peek_pos = parser->pos + offset;
    if (peek_pos >= parser->length) {
        return '\0';
    }
    return parser->html[peek_pos];
}

// helper: check if at EOF
bool html5_is_eof(Html5Parser* parser) {
    return parser->pos >= parser->length;
}

// helper: reconsume current character (move back one position)
static void html5_reconsume(Html5Parser* parser) {
    if (parser->pos > 0) {
        parser->pos--;
    }
}

// helper: switch tokenizer state
void html5_switch_tokenizer_state(Html5Parser* parser, Html5TokenizerState new_state) {
    parser->tokenizer_state = new_state;
}

// helper: create string from temp buffer
static String* html5_create_string_from_temp_buffer(Html5Parser* parser) {
    String* str = (String*)arena_alloc(parser->arena, sizeof(String) + parser->temp_buffer_len + 1);
    str->len = parser->temp_buffer_len;
    memcpy(str->chars, parser->temp_buffer, parser->temp_buffer_len);
    str->chars[parser->temp_buffer_len] = '\0';
    return str;
}

// helper: create lowercase string from temp buffer (for tag names)
static String* html5_create_lowercase_string_from_temp_buffer(Html5Parser* parser) {
    String* str = (String*)arena_alloc(parser->arena, sizeof(String) + parser->temp_buffer_len + 1);
    str->len = parser->temp_buffer_len;
    for (size_t i = 0; i < parser->temp_buffer_len; i++) {
        char c = parser->temp_buffer[i];
        if (c >= 'A' && c <= 'Z') c += 0x20;
        str->chars[i] = c;
    }
    str->chars[parser->temp_buffer_len] = '\0';
    return str;
}

// helper: append character to temp buffer
static void html5_append_to_temp_buffer(Html5Parser* parser, char c) {
    if (parser->temp_buffer_len >= parser->temp_buffer_capacity) {
        // resize temp buffer
        size_t new_capacity = parser->temp_buffer_capacity * 2;
        char* new_buffer = (char*)arena_alloc(parser->arena, new_capacity);
        memcpy(new_buffer, parser->temp_buffer, parser->temp_buffer_len);
        parser->temp_buffer = new_buffer;
        parser->temp_buffer_capacity = new_capacity;
    }
    parser->temp_buffer[parser->temp_buffer_len++] = c;
}

// helper: clear temp buffer
static void html5_clear_temp_buffer(Html5Parser* parser) {
    parser->temp_buffer_len = 0;
}

// helper: append to attribute name buffer
static void html5_append_to_attr_name(Html5Parser* parser, char c) {
    if (parser->current_attr_name == nullptr) {
        parser->current_attr_name_capacity = 32;
        parser->current_attr_name = (char*)arena_alloc(parser->arena, parser->current_attr_name_capacity);
        parser->current_attr_name_len = 0;
    }
    if (parser->current_attr_name_len >= parser->current_attr_name_capacity - 1) {
        size_t new_capacity = parser->current_attr_name_capacity * 2;
        char* new_buffer = (char*)arena_alloc(parser->arena, new_capacity);
        memcpy(new_buffer, parser->current_attr_name, parser->current_attr_name_len);
        parser->current_attr_name = new_buffer;
        parser->current_attr_name_capacity = new_capacity;
    }
    parser->current_attr_name[parser->current_attr_name_len++] = c;
    parser->current_attr_name[parser->current_attr_name_len] = '\0';
}

// helper: clear attribute name buffer
static void html5_clear_attr_name(Html5Parser* parser) {
    parser->current_attr_name_len = 0;
    if (parser->current_attr_name) {
        parser->current_attr_name[0] = '\0';
    }
}

// helper: commit current attribute to token
static void html5_commit_attribute(Html5Parser* parser) {
    if (parser->current_token == nullptr || parser->current_attr_name_len == 0) {
        log_debug("html5: commit_attribute - skipping (no token or no attr name)");
        return;
    }

    log_debug("html5: commit_attribute - name='%s' value_len=%zu",
              parser->current_attr_name, parser->temp_buffer_len);

    // Create String for attribute name
    MarkBuilder builder(parser->input);
    String* attr_name = builder.createString(parser->current_attr_name, parser->current_attr_name_len);

    // Create Item for attribute value (ITEM_NULL for empty, tagged string otherwise)
    Item attr_value;
    if (parser->temp_buffer_len > 0) {
        parser->temp_buffer[parser->temp_buffer_len] = '\0';
        String* val_str = builder.createString(parser->temp_buffer, parser->temp_buffer_len);
        attr_value = Item{.item = s2it(val_str)};
    } else {
        // Empty attribute value (e.g., content="" or boolean <input disabled>)
        // Use ITEM_NULL to match Lambda's semantics where "" literal compiles to ITEM_NULL
        attr_value = Item{.item = ITEM_NULL};
    }

    // Add attribute to token
    html5_token_add_attribute(parser->current_token, attr_name, attr_value, parser->input);

    // Clear for next attribute
    html5_clear_attr_name(parser);
    html5_clear_temp_buffer(parser);
}

// helper: save the last start tag name (for RCDATA/RAWTEXT end tag matching)
static void html5_save_last_start_tag(Html5Parser* parser, const char* name, size_t len) {
    // Allocate or reuse buffer
    if (parser->last_start_tag_name == nullptr || len > parser->last_start_tag_name_len) {
        parser->last_start_tag_name = (char*)arena_alloc(parser->arena, len + 1);
    }
    memcpy(parser->last_start_tag_name, name, len);
    parser->last_start_tag_name[len] = '\0';
    parser->last_start_tag_name_len = len;
}

// helper: check if temp buffer matches last start tag (for appropriate end tag)
// Per WHATWG spec, this is case-insensitive - temp_buffer has original case
static bool html5_is_appropriate_end_tag(Html5Parser* parser) {
    if (!parser->last_start_tag_name || parser->last_start_tag_name_len == 0) {
        return false;
    }
    if (parser->temp_buffer_len != parser->last_start_tag_name_len) {
        return false;
    }
    // Case-insensitive comparison
    return str_ieq(parser->temp_buffer, parser->temp_buffer_len,
                   parser->last_start_tag_name, parser->last_start_tag_name_len);
}

// helper: check if string matches (case insensitive) — delegates to str_ieq
static bool html5_match_string_ci(const char* str1, const char* str2, size_t len) {
    return str_ieq(str1, len, str2, len);
}

// Legacy named character references that can be used without semicolon
// Per HTML5 spec: https://html.spec.whatwg.org/multipage/parsing.html#named-character-reference-state
static const char* legacy_entities[] = {
    "AElig", "AMP", "Aacute", "Acirc", "Agrave", "Aring", "Atilde", "Auml",
    "COPY", "Ccedil", "ETH", "Eacute", "Ecirc", "Egrave", "Euml",
    "GT", "Iacute", "Icirc", "Igrave", "Iuml", "LT", "Ntilde",
    "Oacute", "Ocirc", "Ograve", "Oslash", "Otilde", "Ouml",
    "QUOT", "REG", "THORN", "Uacute", "Ucirc", "Ugrave", "Uuml", "Yacute",
    "aacute", "acirc", "acute", "aelig", "agrave", "amp", "aring", "atilde", "auml",
    "brvbar", "ccedil", "cedil", "cent", "copy", "curren",
    "deg", "divide", "eacute", "ecirc", "egrave", "eth", "euml",
    "frac12", "frac14", "frac34", "gt",
    "iacute", "icirc", "iexcl", "igrave", "iquest", "iuml",
    "laquo", "lt", "macr", "micro", "middot",
    "nbsp", "not", "ntilde",
    "oacute", "ocirc", "ograve", "ordf", "ordm", "oslash", "otilde", "ouml",
    "para", "plusmn", "pound", "quot", "raquo", "reg",
    "sect", "shy", "sup1", "sup2", "sup3", "szlig",
    "thorn", "times", "uacute", "ucirc", "ugrave", "uml", "uuml",
    "yacute", "yen", "yuml",
    nullptr
};

// Check if entity name is a legacy entity (can be used without semicolon)
static bool html5_is_legacy_entity(const char* name, size_t len) {
    for (const char** p = legacy_entities; *p != nullptr; p++) {
        if (strlen(*p) == len && memcmp(*p, name, len) == 0) {
            return true;
        }
    }
    return false;
}

// Windows-1252 character reference replacements per HTML5 spec
// For codepoints 0x80-0x9F, map to their proper Unicode equivalents
static uint32_t html5_windows1252_replacement(uint32_t codepoint) {
    static const uint32_t replacements[32] = {
        0x20AC, // 0x80 -> €
        0x0081, // 0x81 (keep as is)
        0x201A, // 0x82 -> ‚
        0x0192, // 0x83 -> ƒ
        0x201E, // 0x84 -> „
        0x2026, // 0x85 -> …
        0x2020, // 0x86 -> †
        0x2021, // 0x87 -> ‡
        0x02C6, // 0x88 -> ˆ
        0x2030, // 0x89 -> ‰
        0x0160, // 0x8A -> Š
        0x2039, // 0x8B -> ‹
        0x0152, // 0x8C -> Œ
        0x008D, // 0x8D (keep as is)
        0x017D, // 0x8E -> Ž
        0x008F, // 0x8F (keep as is)
        0x0090, // 0x90 (keep as is)
        0x2018, // 0x91 -> '
        0x2019, // 0x92 -> '
        0x201C, // 0x93 -> "
        0x201D, // 0x94 -> "
        0x2022, // 0x95 -> •
        0x2013, // 0x96 -> –
        0x2014, // 0x97 -> —
        0x02DC, // 0x98 -> ˜
        0x2122, // 0x99 -> ™
        0x0161, // 0x9A -> š
        0x203A, // 0x9B -> ›
        0x0153, // 0x9C -> œ
        0x009D, // 0x9D (keep as is)
        0x017E, // 0x9E -> ž
        0x0178, // 0x9F -> Ÿ
    };
    if (codepoint >= 0x80 && codepoint <= 0x9F) {
        return replacements[codepoint - 0x80];
    }
    return codepoint;
}

// Try to consume and decode a character reference starting after '&'
// Returns number of characters to emit (stored in out_chars), or 0 if not a valid reference
// in_attribute: true if we're in an attribute value context (affects entity boundary rules)
static int html5_try_decode_char_reference(Html5Parser* parser, char* out_chars, int* out_len, bool in_attribute) {
    size_t start_pos = parser->pos;
    char c = html5_peek_char(parser, 0);

    // Numeric reference: &#
    if (c == '#') {
        parser->pos++;  // consume '#'
        c = html5_peek_char(parser, 0);

        bool is_hex = false;
        if (c == 'x' || c == 'X') {
            is_hex = true;
            parser->pos++;  // consume 'x'
        }

        // Parse digits
        uint32_t codepoint = 0;
        int digits = 0;
        bool overflowed = false;
        while (true) {
            c = html5_peek_char(parser, 0);
            if (is_hex) {
                if (c >= '0' && c <= '9') {
                    if (!overflowed) {
                        uint32_t digit = c - '0';
                        if (codepoint > (0x10FFFF / 16)) {
                            overflowed = true;
                        } else {
                            codepoint = codepoint * 16 + digit;
                            if (codepoint > 0x10FFFF) overflowed = true;
                        }
                    }
                    digits++;
                    parser->pos++;
                } else if (c >= 'a' && c <= 'f') {
                    if (!overflowed) {
                        uint32_t digit = c - 'a' + 10;
                        if (codepoint > (0x10FFFF / 16)) {
                            overflowed = true;
                        } else {
                            codepoint = codepoint * 16 + digit;
                            if (codepoint > 0x10FFFF) overflowed = true;
                        }
                    }
                    digits++;
                    parser->pos++;
                } else if (c >= 'A' && c <= 'F') {
                    if (!overflowed) {
                        uint32_t digit = c - 'A' + 10;
                        if (codepoint > (0x10FFFF / 16)) {
                            overflowed = true;
                        } else {
                            codepoint = codepoint * 16 + digit;
                            if (codepoint > 0x10FFFF) overflowed = true;
                        }
                    }
                    digits++;
                    parser->pos++;
                } else {
                    break;
                }
            } else {
                if (c >= '0' && c <= '9') {
                    if (!overflowed) {
                        uint32_t digit = c - '0';
                        if (codepoint > (0x10FFFF / 10)) {
                            overflowed = true;
                        } else {
                            codepoint = codepoint * 10 + digit;
                            if (codepoint > 0x10FFFF) overflowed = true;
                        }
                    }
                    digits++;
                    parser->pos++;
                } else {
                    break;
                }
            }
        }

        if (overflowed) {
            codepoint = 0xFFFD;
        }

        if (digits == 0) {
            // No digits found, not a valid reference
            parser->pos = start_pos;
            return 0;
        }

        // Check for optional semicolon
        if (html5_peek_char(parser, 0) == ';') {
            parser->pos++;  // consume ';'
        }

        // Validate and convert codepoint
        // Replace null and surrogate codepoints
        if (codepoint == 0 || (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF) {
            codepoint = 0xFFFD;  // replacement character
        }

        // Apply Windows-1252 replacements per HTML5 spec
        codepoint = html5_windows1252_replacement(codepoint);

        *out_len = codepoint_to_utf8(codepoint, out_chars);
        return 1;
    }

    // Named reference
    char entity_name[32];
    size_t name_len = 0;
    while (name_len < 31) {
        c = html5_peek_char(parser, 0);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            entity_name[name_len++] = c;
            parser->pos++;
        } else {
            break;
        }
    }
    entity_name[name_len] = '\0';

    if (name_len == 0) {
        // No name found
        parser->pos = start_pos;
        return 0;
    }

    // Check for semicolon and what follows
    char next_char = html5_peek_char(parser, 0);
    bool has_semicolon = (next_char == ';');

    // First try: exact match with semicolon
    if (has_semicolon) {
        const char* replacement = html_entity_lookup(entity_name, name_len);
        if (replacement != nullptr) {
            parser->pos++;  // consume ';'
            size_t rep_len = strlen(replacement);
            memcpy(out_chars, replacement, rep_len);
            *out_len = (int)rep_len;
            return 1;
        }
    }

    // Second try: look for longest legacy entity prefix match
    // This handles cases like "&notit;" -> "&not" + "it;"
    size_t save_pos = parser->pos;
    for (size_t try_len = name_len; try_len > 0; try_len--) {
        entity_name[try_len] = '\0';
        if (html5_is_legacy_entity(entity_name, try_len)) {
            const char* replacement = html_entity_lookup(entity_name, try_len);
            if (replacement != nullptr) {
                // Found a legacy entity prefix match
                // Rewind position to just after the matched entity
                // start_pos is position of first char after '&', so start_pos + try_len is position after entity
                parser->pos = start_pos + try_len;

                // For legacy entities in attribute context, check if followed by
                // alphanumeric or '=' - don't decode in that case
                if (in_attribute) {
                    char after_entity = html5_peek_char(parser, 0);
                    if ((after_entity >= 'a' && after_entity <= 'z') ||
                        (after_entity >= 'A' && after_entity <= 'Z') ||
                        (after_entity >= '0' && after_entity <= '9') ||
                        after_entity == '=') {
                        // Don't decode - restore position and return failure
                        parser->pos = start_pos;
                        return 0;
                    }
                }

                size_t rep_len = strlen(replacement);
                memcpy(out_chars, replacement, rep_len);
                *out_len = (int)rep_len;
                return 1;
            }
        }
    }

    // No match found - restore position and emit '&' as literal
    parser->pos = start_pos;
    return 0;
}

// main tokenizer function - returns next token
Html5Token* html5_tokenize_next(Html5Parser* parser) {
    while (true) {
        // OPTIMIZATION: Try batch scanning for text-heavy states first
        if (parser->tokenizer_state == HTML5_TOK_DATA) {
            // Fast path: scan run of plain ASCII text
            size_t run_len = html5_scan_text_run(parser);
            if (run_len > 0) {
                // Emit entire text run as single token
                const char* text_start = parser->html + parser->pos;
                parser->pos += run_len;
                return html5_token_create_character_string(parser->pool, parser->arena, text_start, (int)run_len);
            }
            // Fall through to character-by-character for special chars
        } else if (parser->tokenizer_state == HTML5_TOK_RCDATA) {
            // Fast path for RCDATA (title, textarea content)
            size_t run_len = html5_scan_rcdata_run(parser);
            if (run_len > 0) {
                const char* text_start = parser->html + parser->pos;
                parser->pos += run_len;
                return html5_token_create_character_string(parser->pool, parser->arena, text_start, (int)run_len);
            }
        } else if (parser->tokenizer_state == HTML5_TOK_RAWTEXT) {
            // Fast path for RAWTEXT (style, script content)
            size_t run_len = html5_scan_rawtext_run(parser);
            if (run_len > 0) {
                const char* text_start = parser->html + parser->pos;
                parser->pos += run_len;
                return html5_token_create_character_string(parser->pool, parser->arena, text_start, (int)run_len);
            }
        }

        char c = html5_consume_next_char(parser);

        switch (parser->tokenizer_state) {
            case HTML5_TOK_DATA: {
                if (c == '&') {
                    // Try to decode character reference
                    char decoded[8];
                    int decoded_len = 0;
                    if (html5_try_decode_char_reference(parser, decoded, &decoded_len, false)) {
                        // Emit decoded characters
                        return html5_token_create_character_string(parser->pool, parser->arena, decoded, decoded_len);
                    } else {
                        // Not a valid entity, emit '&' literally
                        return html5_token_create_character(parser->pool, parser->arena, c);
                    }
                } else if (c == '<') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_TAG_OPEN);
                } else if (c == '\0') {
                    if (html5_is_eof(parser)) {
                        return html5_token_create_eof(parser->pool, parser->arena);
                    } else {
                        // parse error: unexpected null
                        log_error("html5: unexpected null character in data state");
                        return html5_token_create_character(parser->pool, parser->arena, 0xFFFD);
                    }
                } else {
                    // Single character (non-ASCII or after batch scan)
                    return html5_token_create_character(parser->pool, parser->arena, c);
                }
                break;
            }

            case HTML5_TOK_RCDATA: {
                // RCDATA state - for <title>, <textarea>
                // Only recognizes & for entities and < for potential end tag
                if (c == '&') {
                    // Try to decode character reference
                    char decoded[8];
                    int decoded_len = 0;
                    if (html5_try_decode_char_reference(parser, decoded, &decoded_len, false)) {
                        return html5_token_create_character_string(parser->pool, parser->arena, decoded, decoded_len);
                    } else {
                        return html5_token_create_character(parser->pool, parser->arena, c);
                    }
                } else if (c == '<') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_RCDATA_LESS_THAN_SIGN);
                } else if (c == '\0') {
                    if (html5_is_eof(parser)) {
                        return html5_token_create_eof(parser->pool, parser->arena);
                    } else {
                        log_error("html5: unexpected null in RCDATA");
                        return html5_token_create_character(parser->pool, parser->arena, 0xFFFD);
                    }
                } else {
                    return html5_token_create_character(parser->pool, parser->arena, c);
                }
                break;
            }

            case HTML5_TOK_RCDATA_LESS_THAN_SIGN: {
                // Saw '<' in RCDATA, looking for '/' to start potential end tag
                if (c == '/') {
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_RCDATA_END_TAG_OPEN);
                } else {
                    // Not an end tag, emit '<' and reconsume
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_RCDATA);
                    return html5_token_create_character(parser->pool, parser->arena, '<');
                }
                break;
            }

            case HTML5_TOK_RCDATA_END_TAG_OPEN: {
                // Saw '</' in RCDATA, check if this starts a valid end tag
                if (isalpha(c)) {
                    parser->current_token = html5_token_create_end_tag(parser->pool, parser->arena, nullptr);
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_RCDATA_END_TAG_NAME);
                } else {
                    // Not a valid end tag, emit '</' and reconsume
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_RCDATA);
                    return html5_token_create_character_string(parser->pool, parser->arena, "</", 2);
                }
                break;
            }

            case HTML5_TOK_RCDATA_END_TAG_NAME: {
                // Building end tag name in RCDATA
                // Per WHATWG spec: temp_buffer stores ORIGINAL case for text emission
                // Tag name is created with lowercase
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    // Check if this is the appropriate end tag
                    if (html5_is_appropriate_end_tag(parser)) {
                        parser->current_token->tag_name = html5_create_lowercase_string_from_temp_buffer(parser);
                        html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_ATTRIBUTE_NAME);
                    } else {
                        // Not appropriate, emit as text
                        goto rcdata_emit_as_text;
                    }
                } else if (c == '/') {
                    if (html5_is_appropriate_end_tag(parser)) {
                        parser->current_token->tag_name = html5_create_lowercase_string_from_temp_buffer(parser);
                        html5_switch_tokenizer_state(parser, HTML5_TOK_SELF_CLOSING_START_TAG);
                    } else {
                        goto rcdata_emit_as_text;
                    }
                } else if (c == '>') {
                    if (html5_is_appropriate_end_tag(parser)) {
                        parser->current_token->tag_name = html5_create_lowercase_string_from_temp_buffer(parser);
                        html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                        Html5Token* token = parser->current_token;
                        parser->current_token = nullptr;
                        return token;
                    } else {
                        goto rcdata_emit_as_text;
                    }
                } else if (isalpha(c)) {
                    // Store ORIGINAL case in temp_buffer (for text emission if not valid end tag)
                    html5_append_to_temp_buffer(parser, c);
                } else {
                    // Not a valid end tag name character
                    rcdata_emit_as_text:
                    // Emit '</' + temp_buffer contents as text
                    parser->current_token = nullptr;
                    // Only reconsume if not at EOF - at EOF there's nothing to reconsume
                    if (!html5_is_eof(parser)) {
                        html5_reconsume(parser);
                    }
                    html5_switch_tokenizer_state(parser, HTML5_TOK_RCDATA);
                    // Build the string "</" + temp_buffer
                    size_t len = 2 + parser->temp_buffer_len;
                    char* text = (char*)arena_alloc(parser->arena, len + 1);
                    text[0] = '<';
                    text[1] = '/';
                    memcpy(text + 2, parser->temp_buffer, parser->temp_buffer_len);
                    text[len] = '\0';
                    return html5_token_create_character_string(parser->pool, parser->arena, text, len);
                }
                break;
            }

            case HTML5_TOK_RAWTEXT: {
                // RAWTEXT state - for <style>, <script>, <xmp>, <iframe>, <noembed>, <noframes>
                // Similar to RCDATA but doesn't recognize &
                if (c == '<') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_RAWTEXT_LESS_THAN_SIGN);
                } else if (c == '\0') {
                    if (html5_is_eof(parser)) {
                        return html5_token_create_eof(parser->pool, parser->arena);
                    } else {
                        log_error("html5: unexpected null in RAWTEXT");
                        return html5_token_create_character(parser->pool, parser->arena, 0xFFFD);
                    }
                } else {
                    return html5_token_create_character(parser->pool, parser->arena, c);
                }
                break;
            }

            case HTML5_TOK_RAWTEXT_LESS_THAN_SIGN: {
                if (c == '/') {
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_RAWTEXT_END_TAG_OPEN);
                } else {
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_RAWTEXT);
                    return html5_token_create_character(parser->pool, parser->arena, '<');
                }
                break;
            }

            case HTML5_TOK_RAWTEXT_END_TAG_OPEN: {
                if (isalpha(c)) {
                    parser->current_token = html5_token_create_end_tag(parser->pool, parser->arena, nullptr);
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_RAWTEXT_END_TAG_NAME);
                } else {
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_RAWTEXT);
                    return html5_token_create_character_string(parser->pool, parser->arena, "</", 2);
                }
                break;
            }

            case HTML5_TOK_RAWTEXT_END_TAG_NAME: {
                // Building end tag name in RAWTEXT
                // Per WHATWG spec: temp_buffer stores ORIGINAL case for text emission
                // Tag name is created with lowercase
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    if (html5_is_appropriate_end_tag(parser)) {
                        parser->current_token->tag_name = html5_create_lowercase_string_from_temp_buffer(parser);
                        html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_ATTRIBUTE_NAME);
                    } else {
                        goto rawtext_emit_as_text;
                    }
                } else if (c == '/') {
                    if (html5_is_appropriate_end_tag(parser)) {
                        parser->current_token->tag_name = html5_create_lowercase_string_from_temp_buffer(parser);
                        html5_switch_tokenizer_state(parser, HTML5_TOK_SELF_CLOSING_START_TAG);
                    } else {
                        goto rawtext_emit_as_text;
                    }
                } else if (c == '>') {
                    if (html5_is_appropriate_end_tag(parser)) {
                        parser->current_token->tag_name = html5_create_lowercase_string_from_temp_buffer(parser);
                        html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                        Html5Token* token = parser->current_token;
                        parser->current_token = nullptr;
                        return token;
                    } else {
                        goto rawtext_emit_as_text;
                    }
                } else if (isalpha(c)) {
                    // Store ORIGINAL case in temp_buffer (for text emission if not valid end tag)
                    html5_append_to_temp_buffer(parser, c);
                } else {
                    rawtext_emit_as_text:
                    parser->current_token = nullptr;
                    // Only reconsume if not at EOF - at EOF there's nothing to reconsume
                    if (!html5_is_eof(parser)) {
                        html5_reconsume(parser);
                    }
                    html5_switch_tokenizer_state(parser, HTML5_TOK_RAWTEXT);
                    size_t len = 2 + parser->temp_buffer_len;
                    char* text = (char*)arena_alloc(parser->arena, len + 1);
                    text[0] = '<';
                    text[1] = '/';
                    memcpy(text + 2, parser->temp_buffer, parser->temp_buffer_len);
                    text[len] = '\0';
                    return html5_token_create_character_string(parser->pool, parser->arena, text, len);
                }
                break;
            }

            case HTML5_TOK_PLAINTEXT: {
                // PLAINTEXT state - everything is literal text, no end tag recognized
                // Per WHATWG: keep emitting characters until EOF
                if (c == '\0') {
                    if (html5_is_eof(parser)) {
                        return html5_token_create_eof(parser->pool, parser->arena);
                    } else {
                        log_error("html5: unexpected null in PLAINTEXT");
                        return html5_token_create_character(parser->pool, parser->arena, 0xFFFD);
                    }
                } else {
                    return html5_token_create_character(parser->pool, parser->arena, c);
                }
                break;
            }

            case HTML5_TOK_TAG_OPEN: {
                if (c == '!') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_MARKUP_DECLARATION_OPEN);
                } else if (c == '/') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_END_TAG_OPEN);
                } else if (isalpha(c)) {
                    parser->current_token = html5_token_create_start_tag(parser->pool, parser->arena, nullptr);
                    html5_clear_temp_buffer(parser);
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_TAG_NAME);
                } else if (c == '?') {
                    // parse error: unexpected question mark
                    log_error("html5: unexpected question mark instead of tag name");
                    parser->current_token = html5_token_create_comment(parser->pool, parser->arena, nullptr);
                    html5_clear_temp_buffer(parser);
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BOGUS_COMMENT);
                } else if (c == '\0' && html5_is_eof(parser)) {
                    // parse error: eof before tag name
                    log_error("html5: eof before tag name");
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    // Emit '<' then let DATA handle EOF
                    return html5_token_create_character(parser->pool, parser->arena, '<');
                } else {
                    // parse error: invalid first character of tag name
                    // emit '<' and reconsume current character in DATA state
                    log_error("html5: invalid first character of tag name");
                    html5_reconsume(parser);  // reconsume the invalid character
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    return html5_token_create_character(parser->pool, parser->arena, '<');
                }
                break;
            }

            case HTML5_TOK_END_TAG_OPEN: {
                if (isalpha(c)) {
                    parser->current_token = html5_token_create_end_tag(parser->pool, parser->arena, nullptr);
                    html5_clear_temp_buffer(parser);
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_TAG_NAME);
                } else if (c == '>') {
                    // parse error: missing end tag name
                    log_error("html5: missing end tag name");
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                } else if (c == '\0' && html5_is_eof(parser)) {
                    // parse error: eof before tag name
                    // emit '<' and '/', then switch to DATA state where EOF will be handled
                    log_error("html5: eof before tag name");
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    // Don't back up - next call will emit EOF from DATA state
                    return html5_token_create_character_string(parser->pool, parser->arena, "</", 2);
                } else {
                    // parse error: invalid first character of tag name
                    log_error("html5: invalid first character of tag name");
                    parser->current_token = html5_token_create_comment(parser->pool, parser->arena, nullptr);
                    html5_clear_temp_buffer(parser);
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BOGUS_COMMENT);
                }
                break;
            }

            case HTML5_TOK_TAG_NAME: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    parser->current_token->tag_name = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_ATTRIBUTE_NAME);
                } else if (c == '/') {
                    parser->current_token->tag_name = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_SELF_CLOSING_START_TAG);
                } else if (c == '>') {
                    parser->current_token->tag_name = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    // Save last start tag name for RCDATA/RAWTEXT end tag matching
                    if (token->type == HTML5_TOKEN_START_TAG && token->tag_name) {
                        html5_save_last_start_tag(parser, token->tag_name->chars, token->tag_name->len);
                    }
                    return token;
                } else if (c >= 'A' && c <= 'Z') {
                    // convert uppercase to lowercase
                    html5_append_to_temp_buffer(parser, c + 0x20);
                } else if (c == '\0') {
                    if (html5_is_eof(parser)) {
                        // parse error: eof in tag
                        log_error("html5: eof in tag");
                        html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                        return html5_token_create_eof(parser->pool, parser->arena);
                    } else {
                        // parse error: unexpected null
                        log_error("html5: unexpected null in tag name");
                        html5_append_to_temp_buffer(parser, 0xFFFD);
                    }
                } else {
                    html5_append_to_temp_buffer(parser, c);
                }
                break;
            }

            case HTML5_TOK_BEFORE_ATTRIBUTE_NAME: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    // ignore whitespace
                } else if (c == '/' || c == '>') {
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_AFTER_ATTRIBUTE_NAME);
                } else if (c == '=') {
                    // parse error: unexpected equals sign
                    log_error("html5: unexpected equals sign before attribute name");
                    html5_clear_attr_name(parser);
                    html5_clear_temp_buffer(parser);  // clear value buffer
                    html5_append_to_attr_name(parser, c);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_ATTRIBUTE_NAME);
                } else if (c == '\0' && html5_is_eof(parser)) {
                    log_error("html5: eof in tag");
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    return html5_token_create_eof(parser->pool, parser->arena);
                } else {
                    html5_clear_attr_name(parser);
                    html5_clear_temp_buffer(parser);  // clear value buffer for new attribute
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_ATTRIBUTE_NAME);
                }
                break;
            }

            case HTML5_TOK_ATTRIBUTE_NAME: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ' || c == '/' || c == '>') {
                    // attribute name complete (value-less attribute)
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_AFTER_ATTRIBUTE_NAME);
                } else if (c == '=') {
                    // attribute name complete, value follows
                    html5_clear_temp_buffer(parser);  // prepare for value
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_ATTRIBUTE_VALUE);
                } else if (c >= 'A' && c <= 'Z') {
                    html5_append_to_attr_name(parser, c + 0x20);  // lowercase
                } else if (c == '\0') {
                    if (html5_is_eof(parser)) {
                        // EOF in attribute name - return EOF directly (don't reconsume, it doesn't work at EOF)
                        log_error("html5: eof in tag");
                        html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                        return html5_token_create_eof(parser->pool, parser->arena);
                    } else {
                        // actual null character in input
                        log_error("html5: unexpected null in attribute name");
                        html5_append_to_attr_name(parser, 0xFFFD);
                    }
                } else if (c == '"' || c == '\'' || c == '<') {
                    log_error("html5: unexpected character in attribute name");
                    html5_append_to_attr_name(parser, c);
                } else {
                    html5_append_to_attr_name(parser, c);
                }
                break;
            }

            case HTML5_TOK_AFTER_ATTRIBUTE_NAME: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    // ignore whitespace
                } else if (c == '/') {
                    // commit value-less attribute before self-closing
                    html5_commit_attribute(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_SELF_CLOSING_START_TAG);
                } else if (c == '=') {
                    html5_clear_temp_buffer(parser);  // prepare for value
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_ATTRIBUTE_VALUE);
                } else if (c == '>') {
                    // commit value-less attribute and emit tag
                    html5_commit_attribute(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    // Save last start tag name for RCDATA/RAWTEXT end tag matching
                    if (token->type == HTML5_TOKEN_START_TAG && token->tag_name) {
                        html5_save_last_start_tag(parser, token->tag_name->chars, token->tag_name->len);
                    }
                    return token;
                } else if (c == '\0' && html5_is_eof(parser)) {
                    // EOF in tag - emit eof token (tag is dropped per spec)
                    log_error("html5: eof in tag");
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    return html5_token_create_eof(parser->pool, parser->arena);
                } else {
                    // start new attribute - commit previous value-less attribute first
                    html5_commit_attribute(parser);
                    html5_clear_attr_name(parser);
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_ATTRIBUTE_NAME);
                }
                break;
            }

            case HTML5_TOK_BEFORE_ATTRIBUTE_VALUE: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    // ignore whitespace
                } else if (c == '"') {
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_ATTRIBUTE_VALUE_DOUBLE_QUOTED);
                } else if (c == '\'') {
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_ATTRIBUTE_VALUE_SINGLE_QUOTED);
                } else if (c == '>') {
                    log_error("html5: missing attribute value");
                    html5_commit_attribute(parser);  // commit empty value
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    // Save last start tag name for RCDATA/RAWTEXT end tag matching
                    if (token->type == HTML5_TOKEN_START_TAG && token->tag_name) {
                        html5_save_last_start_tag(parser, token->tag_name->chars, token->tag_name->len);
                    }
                    return token;
                } else if (c == '\0' && html5_is_eof(parser)) {
                    // EOF in tag
                    log_error("html5: eof in tag");
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    return html5_token_create_eof(parser->pool, parser->arena);
                } else {
                    html5_clear_temp_buffer(parser);
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_ATTRIBUTE_VALUE_UNQUOTED);
                }
                break;
            }

            case HTML5_TOK_ATTRIBUTE_VALUE_DOUBLE_QUOTED: {
                if (c == '"') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_AFTER_ATTRIBUTE_VALUE_QUOTED);
                } else if (c == '&') {
                    // character reference in attribute value
                    char decoded[8];
                    int decoded_len = 0;
                    if (html5_try_decode_char_reference(parser, decoded, &decoded_len, true)) {
                        for (int i = 0; i < decoded_len; i++) {
                            html5_append_to_temp_buffer(parser, decoded[i]);
                        }
                    } else {
                        html5_append_to_temp_buffer(parser, c);
                    }
                } else if (c == '\0') {
                    if (html5_is_eof(parser)) {
                        log_error("html5: eof in attribute value");
                        html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                        return html5_token_create_eof(parser->pool, parser->arena);
                    } else {
                        log_error("html5: unexpected null in attribute value");
                        html5_append_to_temp_buffer(parser, 0xFFFD);
                    }
                } else {
                    html5_append_to_temp_buffer(parser, c);
                }
                break;
            }

            case HTML5_TOK_ATTRIBUTE_VALUE_SINGLE_QUOTED: {
                if (c == '\'') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_AFTER_ATTRIBUTE_VALUE_QUOTED);
                } else if (c == '&') {
                    // character reference in attribute value
                    char decoded[8];
                    int decoded_len = 0;
                    if (html5_try_decode_char_reference(parser, decoded, &decoded_len, true)) {
                        for (int i = 0; i < decoded_len; i++) {
                            html5_append_to_temp_buffer(parser, decoded[i]);
                        }
                    } else {
                        html5_append_to_temp_buffer(parser, c);
                    }
                } else if (c == '\0') {
                    if (html5_is_eof(parser)) {
                        log_error("html5: eof in attribute value");
                        html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                        return html5_token_create_eof(parser->pool, parser->arena);
                    } else {
                        log_error("html5: unexpected null in attribute value");
                        html5_append_to_temp_buffer(parser, 0xFFFD);
                    }
                } else {
                    html5_append_to_temp_buffer(parser, c);
                }
                break;
            }

            case HTML5_TOK_ATTRIBUTE_VALUE_UNQUOTED: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    // commit and move to next attribute
                    html5_commit_attribute(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_ATTRIBUTE_NAME);
                } else if (c == '&') {
                    // character reference in attribute value
                    char decoded[8];
                    int decoded_len = 0;
                    if (html5_try_decode_char_reference(parser, decoded, &decoded_len, true)) {
                        for (int i = 0; i < decoded_len; i++) {
                            html5_append_to_temp_buffer(parser, decoded[i]);
                        }
                    } else {
                        html5_append_to_temp_buffer(parser, c);
                    }
                } else if (c == '>') {
                    // commit and emit tag
                    html5_commit_attribute(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    // Save last start tag name for RCDATA/RAWTEXT end tag matching
                    if (token->type == HTML5_TOKEN_START_TAG && token->tag_name) {
                        html5_save_last_start_tag(parser, token->tag_name->chars, token->tag_name->len);
                    }
                    return token;
                } else if (c == '\0') {
                    if (html5_is_eof(parser)) {
                        log_error("html5: eof in attribute value");
                        html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                        return html5_token_create_eof(parser->pool, parser->arena);
                    } else {
                        log_error("html5: unexpected null in attribute value");
                        html5_append_to_temp_buffer(parser, 0xFFFD);
                    }
                } else if (c == '"' || c == '\'' || c == '<' || c == '=' || c == '`') {
                    log_error("html5: unexpected character in unquoted attribute value");
                    html5_append_to_temp_buffer(parser, c);
                } else {
                    html5_append_to_temp_buffer(parser, c);
                }
                break;
            }

            case HTML5_TOK_AFTER_ATTRIBUTE_VALUE_QUOTED: {
                // commit the attribute that was just finished
                html5_commit_attribute(parser);

                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_ATTRIBUTE_NAME);
                } else if (c == '/') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_SELF_CLOSING_START_TAG);
                } else if (c == '>') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    // Save last start tag name for RCDATA/RAWTEXT end tag matching
                    if (token->type == HTML5_TOKEN_START_TAG && token->tag_name) {
                        html5_save_last_start_tag(parser, token->tag_name->chars, token->tag_name->len);
                    }
                    return token;
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof after attribute value");
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    return html5_token_create_eof(parser->pool, parser->arena);
                } else {
                    log_error("html5: missing whitespace between attributes");
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_ATTRIBUTE_NAME);
                }
                break;
            }

            case HTML5_TOK_SELF_CLOSING_START_TAG: {
                if (c == '>') {
                    parser->current_token->self_closing = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    // Save last start tag name for RCDATA/RAWTEXT end tag matching
                    if (token->type == HTML5_TOKEN_START_TAG && token->tag_name) {
                        html5_save_last_start_tag(parser, token->tag_name->chars, token->tag_name->len);
                    }
                    return token;
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in tag");
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    return html5_token_create_eof(parser->pool, parser->arena);
                } else {
                    log_error("html5: unexpected solidus in tag");
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_ATTRIBUTE_NAME);
                }
                break;
            }

            case HTML5_TOK_MARKUP_DECLARATION_OPEN: {
                // check for "--" (comment start)
                if (c == '-' && html5_peek_char(parser, 0) == '-') {
                    html5_consume_next_char(parser);  // consume second dash
                    parser->current_token = html5_token_create_comment(parser->pool, parser->arena, nullptr);
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_START);
                }
                // check for "DOCTYPE" (case-insensitive) - c already has first char
                else if ((c == 'd' || c == 'D') && parser->pos + 6 <= parser->length &&
                         html5_match_string_ci(&parser->html[parser->pos - 1], "doctype", 7)) {
                    parser->pos += 6;  // skip remaining 6 chars (already consumed 'd')
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE);
                }
                // check for "[CDATA["
                else if (c == '[' && parser->pos + 6 <= parser->length &&
                         strncmp(&parser->html[parser->pos], "CDATA[", 6) == 0) {
                    parser->pos += 6;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_CDATA_SECTION);
                }
                else {
                    log_error("html5: incorrectly opened comment");
                    parser->current_token = html5_token_create_comment(parser->pool, parser->arena, nullptr);
                    html5_clear_temp_buffer(parser);
                    // Only reconsume if not at EOF - at EOF there's nothing to reconsume
                    // and reconsuming would cause the previous character to be added
                    // to the comment content incorrectly
                    if (!(c == '\0' && html5_is_eof(parser))) {
                        html5_reconsume(parser);
                    }
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BOGUS_COMMENT);
                }
                break;
            }

            case HTML5_TOK_COMMENT_START: {
                if (c == '-') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_START_DASH);
                } else if (c == '>') {
                    log_error("html5: abrupt closing of empty comment");
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT);
                }
                break;
            }

            case HTML5_TOK_COMMENT_START_DASH: {
                if (c == '-') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_END);
                } else if (c == '>') {
                    log_error("html5: abrupt closing of empty comment");
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in comment");
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    html5_append_to_temp_buffer(parser, '-');
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT);
                }
                break;
            }

            case HTML5_TOK_COMMENT: {
                if (c == '<') {
                    html5_append_to_temp_buffer(parser, c);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_LESS_THAN_SIGN);
                } else if (c == '-') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_END_DASH);
                } else if (c == '\0') {
                    if (html5_is_eof(parser)) {
                        log_error("html5: eof in comment");
                        parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                        html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                        Html5Token* token = parser->current_token;
                        parser->current_token = nullptr;
                        return token;
                    } else {
                        log_error("html5: unexpected null in comment");
                        html5_append_to_temp_buffer(parser, 0xFFFD);
                    }
                } else {
                    html5_append_to_temp_buffer(parser, c);
                }
                break;
            }

            case HTML5_TOK_COMMENT_LESS_THAN_SIGN: {
                if (c == '!') {
                    html5_append_to_temp_buffer(parser, c);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_LESS_THAN_SIGN_BANG);
                } else if (c == '<') {
                    html5_append_to_temp_buffer(parser, c);
                } else {
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT);
                }
                break;
            }

            case HTML5_TOK_COMMENT_LESS_THAN_SIGN_BANG: {
                if (c == '-') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_LESS_THAN_SIGN_BANG_DASH);
                } else {
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT);
                }
                break;
            }

            case HTML5_TOK_COMMENT_LESS_THAN_SIGN_BANG_DASH: {
                if (c == '-') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_LESS_THAN_SIGN_BANG_DASH_DASH);
                } else {
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_END_DASH);
                }
                break;
            }

            case HTML5_TOK_COMMENT_LESS_THAN_SIGN_BANG_DASH_DASH: {
                if (c == '>' || html5_is_eof(parser)) {
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_END);
                } else {
                    log_error("html5: nested comment");
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_END);
                }
                break;
            }

            case HTML5_TOK_COMMENT_END_DASH: {
                if (c == '-') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_END);
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in comment");
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    html5_append_to_temp_buffer(parser, '-');
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT);
                }
                break;
            }

            case HTML5_TOK_COMMENT_END: {
                if (c == '>') {
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (c == '!') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_END_BANG);
                } else if (c == '-') {
                    html5_append_to_temp_buffer(parser, '-');
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in comment");
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    html5_append_to_temp_buffer(parser, '-');
                    html5_append_to_temp_buffer(parser, '-');
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT);
                }
                break;
            }

            case HTML5_TOK_COMMENT_END_BANG: {
                if (c == '-') {
                    html5_append_to_temp_buffer(parser, '-');
                    html5_append_to_temp_buffer(parser, '-');
                    html5_append_to_temp_buffer(parser, '!');
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_END_DASH);
                } else if (c == '>') {
                    log_error("html5: incorrectly closed comment");
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in comment");
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    html5_append_to_temp_buffer(parser, '-');
                    html5_append_to_temp_buffer(parser, '-');
                    html5_append_to_temp_buffer(parser, '!');
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT);
                }
                break;
            }

            case HTML5_TOK_BOGUS_COMMENT: {
                if (c == '>') {
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (c == '\0' && html5_is_eof(parser)) {
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (c == '\0') {
                    html5_append_to_temp_buffer(parser, 0xFFFD);
                } else {
                    html5_append_to_temp_buffer(parser, c);
                }
                break;
            }

            case HTML5_TOK_DOCTYPE: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_DOCTYPE_NAME);
                } else if (c == '>') {
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_DOCTYPE_NAME);
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in doctype");
                    parser->current_token = html5_token_create_doctype(parser->pool, parser->arena);
                    parser->current_token->force_quirks = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    log_error("html5: missing whitespace before doctype name");
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_DOCTYPE_NAME);
                }
                break;
            }

            case HTML5_TOK_BEFORE_DOCTYPE_NAME: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    // ignore whitespace
                } else if (c >= 'A' && c <= 'Z') {
                    parser->current_token = html5_token_create_doctype(parser->pool, parser->arena);
                    html5_clear_temp_buffer(parser);
                    html5_append_to_temp_buffer(parser, c + 0x20);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE_NAME);
                } else if (c == '\0') {
                    if (html5_is_eof(parser)) {
                        log_error("html5: eof in doctype");
                        parser->current_token = html5_token_create_doctype(parser->pool, parser->arena);
                        parser->current_token->force_quirks = true;
                        html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                        Html5Token* token = parser->current_token;
                        parser->current_token = nullptr;
                        return token;
                    } else {
                        log_error("html5: unexpected null in doctype name");
                        parser->current_token = html5_token_create_doctype(parser->pool, parser->arena);
                        html5_clear_temp_buffer(parser);
                        html5_append_to_temp_buffer(parser, 0xFFFD);
                        html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE_NAME);
                    }
                } else if (c == '>') {
                    log_error("html5: missing doctype name");
                    parser->current_token = html5_token_create_doctype(parser->pool, parser->arena);
                    parser->current_token->force_quirks = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    parser->current_token = html5_token_create_doctype(parser->pool, parser->arena);
                    html5_clear_temp_buffer(parser);
                    html5_append_to_temp_buffer(parser, c);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE_NAME);
                }
                break;
            }

            case HTML5_TOK_DOCTYPE_NAME: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    parser->current_token->doctype_name = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_AFTER_DOCTYPE_NAME);
                } else if (c == '>') {
                    parser->current_token->doctype_name = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (c >= 'A' && c <= 'Z') {
                    html5_append_to_temp_buffer(parser, c + 0x20);
                } else if (c == '\0') {
                    if (html5_is_eof(parser)) {
                        log_error("html5: eof in doctype");
                        parser->current_token->doctype_name = html5_create_string_from_temp_buffer(parser);
                        parser->current_token->force_quirks = true;
                        html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                        Html5Token* token = parser->current_token;
                        parser->current_token = nullptr;
                        return token;
                    } else {
                        log_error("html5: unexpected null in doctype name");
                        html5_append_to_temp_buffer(parser, 0xFFFD);
                    }
                } else {
                    html5_append_to_temp_buffer(parser, c);
                }
                break;
            }

            case HTML5_TOK_AFTER_DOCTYPE_NAME: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    // ignore whitespace
                } else if (c == '>') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in doctype");
                    parser->current_token->force_quirks = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    // check for PUBLIC keyword (case-insensitive)
                    if ((c == 'P' || c == 'p') &&
                        parser->pos + 5 <= parser->length &&
                        html5_match_string_ci(&parser->html[parser->pos - 1], "public", 6)) {
                        parser->pos += 5;  // skip "ublic"
                        html5_switch_tokenizer_state(parser, HTML5_TOK_AFTER_DOCTYPE_PUBLIC_KEYWORD);
                    // check for SYSTEM keyword (case-insensitive)
                    } else if ((c == 'S' || c == 's') &&
                               parser->pos + 5 <= parser->length &&
                               html5_match_string_ci(&parser->html[parser->pos - 1], "system", 6)) {
                        parser->pos += 5;  // skip "ystem"
                        html5_switch_tokenizer_state(parser, HTML5_TOK_AFTER_DOCTYPE_SYSTEM_KEYWORD);
                    } else {
                        log_error("html5: invalid character after doctype name");
                        parser->current_token->force_quirks = true;
                        html5_reconsume(parser);
                        html5_switch_tokenizer_state(parser, HTML5_TOK_BOGUS_DOCTYPE_STATE);
                    }
                }
                break;
            }

            case HTML5_TOK_AFTER_DOCTYPE_PUBLIC_KEYWORD: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_DOCTYPE_PUBLIC_IDENTIFIER);
                } else if (c == '"') {
                    log_error("html5: missing whitespace after doctype public keyword");
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED);
                } else if (c == '\'') {
                    log_error("html5: missing whitespace after doctype public keyword");
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED);
                } else if (c == '>') {
                    log_error("html5: missing doctype public identifier");
                    parser->current_token->force_quirks = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in doctype");
                    parser->current_token->force_quirks = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    log_error("html5: missing quote before doctype public identifier");
                    parser->current_token->force_quirks = true;
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BOGUS_DOCTYPE_STATE);
                }
                break;
            }

            case HTML5_TOK_BEFORE_DOCTYPE_PUBLIC_IDENTIFIER: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    // ignore whitespace
                } else if (c == '"') {
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED);
                } else if (c == '\'') {
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED);
                } else if (c == '>') {
                    log_error("html5: missing doctype public identifier");
                    parser->current_token->force_quirks = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in doctype");
                    parser->current_token->force_quirks = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    log_error("html5: missing quote before doctype public identifier");
                    parser->current_token->force_quirks = true;
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BOGUS_DOCTYPE_STATE);
                }
                break;
            }

            case HTML5_TOK_DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED: {
                if (c == '"') {
                    parser->current_token->public_identifier = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_AFTER_DOCTYPE_PUBLIC_IDENTIFIER);
                } else if (c == '\0') {
                    if (html5_is_eof(parser)) {
                        log_error("html5: eof in doctype");
                        parser->current_token->force_quirks = true;
                        parser->current_token->public_identifier = html5_create_string_from_temp_buffer(parser);
                        html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                        Html5Token* token = parser->current_token;
                        parser->current_token = nullptr;
                        return token;
                    } else {
                        log_error("html5: unexpected null in doctype public identifier");
                        html5_append_to_temp_buffer(parser, 0xFFFD);
                    }
                } else if (c == '>') {
                    log_error("html5: abrupt doctype public identifier");
                    parser->current_token->force_quirks = true;
                    parser->current_token->public_identifier = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    html5_append_to_temp_buffer(parser, c);
                }
                break;
            }

            case HTML5_TOK_DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED: {
                if (c == '\'') {
                    parser->current_token->public_identifier = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_AFTER_DOCTYPE_PUBLIC_IDENTIFIER);
                } else if (c == '\0') {
                    if (html5_is_eof(parser)) {
                        log_error("html5: eof in doctype");
                        parser->current_token->force_quirks = true;
                        parser->current_token->public_identifier = html5_create_string_from_temp_buffer(parser);
                        html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                        Html5Token* token = parser->current_token;
                        parser->current_token = nullptr;
                        return token;
                    } else {
                        log_error("html5: unexpected null in doctype public identifier");
                        html5_append_to_temp_buffer(parser, 0xFFFD);
                    }
                } else if (c == '>') {
                    log_error("html5: abrupt doctype public identifier");
                    parser->current_token->force_quirks = true;
                    parser->current_token->public_identifier = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    html5_append_to_temp_buffer(parser, c);
                }
                break;
            }

            case HTML5_TOK_AFTER_DOCTYPE_PUBLIC_IDENTIFIER: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS);
                } else if (c == '>') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (c == '"') {
                    log_error("html5: missing whitespace between doctype public and system identifiers");
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED);
                } else if (c == '\'') {
                    log_error("html5: missing whitespace between doctype public and system identifiers");
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED);
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in doctype");
                    parser->current_token->force_quirks = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    log_error("html5: missing quote before doctype system identifier");
                    parser->current_token->force_quirks = true;
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BOGUS_DOCTYPE_STATE);
                }
                break;
            }

            case HTML5_TOK_BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    // ignore whitespace
                } else if (c == '>') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (c == '"') {
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED);
                } else if (c == '\'') {
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED);
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in doctype");
                    parser->current_token->force_quirks = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    log_error("html5: missing quote before doctype system identifier");
                    parser->current_token->force_quirks = true;
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BOGUS_DOCTYPE_STATE);
                }
                break;
            }

            case HTML5_TOK_AFTER_DOCTYPE_SYSTEM_KEYWORD: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_DOCTYPE_SYSTEM_IDENTIFIER);
                } else if (c == '"') {
                    log_error("html5: missing whitespace after doctype system keyword");
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED);
                } else if (c == '\'') {
                    log_error("html5: missing whitespace after doctype system keyword");
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED);
                } else if (c == '>') {
                    log_error("html5: missing doctype system identifier");
                    parser->current_token->force_quirks = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in doctype");
                    parser->current_token->force_quirks = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    log_error("html5: missing quote before doctype system identifier");
                    parser->current_token->force_quirks = true;
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BOGUS_DOCTYPE_STATE);
                }
                break;
            }

            case HTML5_TOK_BEFORE_DOCTYPE_SYSTEM_IDENTIFIER: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    // ignore whitespace
                } else if (c == '"') {
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED);
                } else if (c == '\'') {
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED);
                } else if (c == '>') {
                    log_error("html5: missing doctype system identifier");
                    parser->current_token->force_quirks = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in doctype");
                    parser->current_token->force_quirks = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    log_error("html5: missing quote before doctype system identifier");
                    parser->current_token->force_quirks = true;
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BOGUS_DOCTYPE_STATE);
                }
                break;
            }

            case HTML5_TOK_DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED: {
                if (c == '"') {
                    parser->current_token->system_identifier = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_AFTER_DOCTYPE_SYSTEM_IDENTIFIER);
                } else if (c == '\0') {
                    if (html5_is_eof(parser)) {
                        log_error("html5: eof in doctype");
                        parser->current_token->force_quirks = true;
                        parser->current_token->system_identifier = html5_create_string_from_temp_buffer(parser);
                        html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                        Html5Token* token = parser->current_token;
                        parser->current_token = nullptr;
                        return token;
                    } else {
                        log_error("html5: unexpected null in doctype system identifier");
                        html5_append_to_temp_buffer(parser, 0xFFFD);
                    }
                } else if (c == '>') {
                    log_error("html5: abrupt doctype system identifier");
                    parser->current_token->force_quirks = true;
                    parser->current_token->system_identifier = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    html5_append_to_temp_buffer(parser, c);
                }
                break;
            }

            case HTML5_TOK_DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED: {
                if (c == '\'') {
                    parser->current_token->system_identifier = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_AFTER_DOCTYPE_SYSTEM_IDENTIFIER);
                } else if (c == '\0') {
                    if (html5_is_eof(parser)) {
                        log_error("html5: eof in doctype");
                        parser->current_token->force_quirks = true;
                        parser->current_token->system_identifier = html5_create_string_from_temp_buffer(parser);
                        html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                        Html5Token* token = parser->current_token;
                        parser->current_token = nullptr;
                        return token;
                    } else {
                        log_error("html5: unexpected null in doctype system identifier");
                        html5_append_to_temp_buffer(parser, 0xFFFD);
                    }
                } else if (c == '>') {
                    log_error("html5: abrupt doctype system identifier");
                    parser->current_token->force_quirks = true;
                    parser->current_token->system_identifier = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    html5_append_to_temp_buffer(parser, c);
                }
                break;
            }

            case HTML5_TOK_AFTER_DOCTYPE_SYSTEM_IDENTIFIER: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    // ignore whitespace
                } else if (c == '>') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in doctype");
                    parser->current_token->force_quirks = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    log_error("html5: unexpected character after doctype system identifier");
                    // Do NOT set force_quirks per spec
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BOGUS_DOCTYPE_STATE);
                }
                break;
            }

            case HTML5_TOK_BOGUS_DOCTYPE_STATE: {
                if (c == '>') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (c == '\0') {
                    if (html5_is_eof(parser)) {
                        html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                        Html5Token* token = parser->current_token;
                        parser->current_token = nullptr;
                        return token;
                    }
                    // non-EOF null is ignored per spec
                }
                // consume and ignore character
                break;
            }

            default: {
                log_error("html5: unimplemented tokenizer state: %d", parser->tokenizer_state);
                return html5_token_create_eof(parser->pool, parser->arena);
            }
        }
    }
}
