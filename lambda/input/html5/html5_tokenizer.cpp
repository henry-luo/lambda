#include "html5_tokenizer.h"
#include "html5_token.h"
#include "../../../lib/log.h"
#include "../../../lib/str.h"
#include "../../mark_builder.hpp"
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
    str->ref_cnt = 1;
    str->len = parser->temp_buffer_len;
    memcpy(str->chars, parser->temp_buffer, parser->temp_buffer_len);
    str->chars[parser->temp_buffer_len] = '\0';
    return str;
}

// helper: create lowercase string from temp buffer (for tag names)
static String* html5_create_lowercase_string_from_temp_buffer(Html5Parser* parser) {
    String* str = (String*)arena_alloc(parser->arena, sizeof(String) + parser->temp_buffer_len + 1);
    str->ref_cnt = 1;
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

    // Create String for attribute value (empty or from temp buffer)
    String* attr_value;
    if (parser->temp_buffer_len > 0) {
        parser->temp_buffer[parser->temp_buffer_len] = '\0';
        attr_value = builder.createString(parser->temp_buffer, parser->temp_buffer_len);
    } else {
        // Create actual empty string for boolean HTML attributes (e.g., <input disabled>)
        // We directly allocate a zero-length string since createString("", 0) returns nullptr
        attr_value = (String*)arena_alloc(parser->arena, sizeof(String) + 1);
        attr_value->ref_cnt = 1;
        attr_value->len = 0;
        attr_value->chars[0] = '\0';
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

// Named character entity structure
struct NamedEntity {
    const char* name;
    const char* replacement;
};

// Auto-generated HTML5 named entity table from WHATWG spec
// Source: https://html.spec.whatwg.org/entities.json
// Total entities: 2125
// Generated by utils/generate_html5_entities.py

static const NamedEntity named_entities[] = {
    {"AElig", "\xC3\x86"},
    {"AMP", "&"},
    {"Aacute", "\xC3\x81"},
    {"Abreve", "\xC4\x82"},
    {"Acirc", "\xC3\x82"},
    {"Acy", "\xD0\x90"},
    {"Afr", "\xF0\x9D\x94\x84"},
    {"Agrave", "\xC3\x80"},
    {"Alpha", "\xCE\x91"},
    {"Amacr", "\xC4\x80"},
    {"And", "\xE2\xA9\x93"},
    {"Aogon", "\xC4\x84"},
    {"Aopf", "\xF0\x9D\x94\xB8"},
    {"ApplyFunction", "\xE2\x81\xA1"},
    {"Aring", "\xC3\x85"},
    {"Ascr", "\xF0\x9D\x92\x9C"},
    {"Assign", "\xE2\x89\x94"},
    {"Atilde", "\xC3\x83"},
    {"Auml", "\xC3\x84"},
    {"Backslash", "\xE2\x88\x96"},
    {"Barv", "\xE2\xAB\xA7"},
    {"Barwed", "\xE2\x8C\x86"},
    {"Bcy", "\xD0\x91"},
    {"Because", "\xE2\x88\xB5"},
    {"Bernoullis", "\xE2\x84\xAC"},
    {"Beta", "\xCE\x92"},
    {"Bfr", "\xF0\x9D\x94\x85"},
    {"Bopf", "\xF0\x9D\x94\xB9"},
    {"Breve", "\xCB\x98"},
    {"Bscr", "\xE2\x84\xAC"},
    {"Bumpeq", "\xE2\x89\x8E"},
    {"CHcy", "\xD0\xA7"},
    {"COPY", "\xC2\xA9"},
    {"Cacute", "\xC4\x86"},
    {"Cap", "\xE2\x8B\x92"},
    {"CapitalDifferentialD", "\xE2\x85\x85"},
    {"Cayleys", "\xE2\x84\xAD"},
    {"Ccaron", "\xC4\x8C"},
    {"Ccedil", "\xC3\x87"},
    {"Ccirc", "\xC4\x88"},
    {"Cconint", "\xE2\x88\xB0"},
    {"Cdot", "\xC4\x8A"},
    {"Cedilla", "\xC2\xB8"},
    {"CenterDot", "\xC2\xB7"},
    {"Cfr", "\xE2\x84\xAD"},
    {"Chi", "\xCE\xA7"},
    {"CircleDot", "\xE2\x8A\x99"},
    {"CircleMinus", "\xE2\x8A\x96"},
    {"CirclePlus", "\xE2\x8A\x95"},
    {"CircleTimes", "\xE2\x8A\x97"},
    {"ClockwiseContourIntegral", "\xE2\x88\xB2"},
    {"CloseCurlyDoubleQuote", "\xE2\x80\x9D"},
    {"CloseCurlyQuote", "\xE2\x80\x99"},
    {"Colon", "\xE2\x88\xB7"},
    {"Colone", "\xE2\xA9\xB4"},
    {"Congruent", "\xE2\x89\xA1"},
    {"Conint", "\xE2\x88\xAF"},
    {"ContourIntegral", "\xE2\x88\xAE"},
    {"Copf", "\xE2\x84\x82"},
    {"Coproduct", "\xE2\x88\x90"},
    {"CounterClockwiseContourIntegral", "\xE2\x88\xB3"},
    {"Cross", "\xE2\xA8\xAF"},
    {"Cscr", "\xF0\x9D\x92\x9E"},
    {"Cup", "\xE2\x8B\x93"},
    {"CupCap", "\xE2\x89\x8D"},
    {"DD", "\xE2\x85\x85"},
    {"DDotrahd", "\xE2\xA4\x91"},
    {"DJcy", "\xD0\x82"},
    {"DScy", "\xD0\x85"},
    {"DZcy", "\xD0\x8F"},
    {"Dagger", "\xE2\x80\xA1"},
    {"Darr", "\xE2\x86\xA1"},
    {"Dashv", "\xE2\xAB\xA4"},
    {"Dcaron", "\xC4\x8E"},
    {"Dcy", "\xD0\x94"},
    {"Del", "\xE2\x88\x87"},
    {"Delta", "\xCE\x94"},
    {"Dfr", "\xF0\x9D\x94\x87"},
    {"DiacriticalAcute", "\xC2\xB4"},
    {"DiacriticalDot", "\xCB\x99"},
    {"DiacriticalDoubleAcute", "\xCB\x9D"},
    {"DiacriticalGrave", "`"},
    {"DiacriticalTilde", "\xCB\x9C"},
    {"Diamond", "\xE2\x8B\x84"},
    {"DifferentialD", "\xE2\x85\x86"},
    {"Dopf", "\xF0\x9D\x94\xBB"},
    {"Dot", "\xC2\xA8"},
    {"DotDot", "\xE2\x83\x9C"},
    {"DotEqual", "\xE2\x89\x90"},
    {"DoubleContourIntegral", "\xE2\x88\xAF"},
    {"DoubleDot", "\xC2\xA8"},
    {"DoubleDownArrow", "\xE2\x87\x93"},
    {"DoubleLeftArrow", "\xE2\x87\x90"},
    {"DoubleLeftRightArrow", "\xE2\x87\x94"},
    {"DoubleLeftTee", "\xE2\xAB\xA4"},
    {"DoubleLongLeftArrow", "\xE2\x9F\xB8"},
    {"DoubleLongLeftRightArrow", "\xE2\x9F\xBA"},
    {"DoubleLongRightArrow", "\xE2\x9F\xB9"},
    {"DoubleRightArrow", "\xE2\x87\x92"},
    {"DoubleRightTee", "\xE2\x8A\xA8"},
    {"DoubleUpArrow", "\xE2\x87\x91"},
    {"DoubleUpDownArrow", "\xE2\x87\x95"},
    {"DoubleVerticalBar", "\xE2\x88\xA5"},
    {"DownArrow", "\xE2\x86\x93"},
    {"DownArrowBar", "\xE2\xA4\x93"},
    {"DownArrowUpArrow", "\xE2\x87\xB5"},
    {"DownBreve", "\xCC\x91"},
    {"DownLeftRightVector", "\xE2\xA5\x90"},
    {"DownLeftTeeVector", "\xE2\xA5\x9E"},
    {"DownLeftVector", "\xE2\x86\xBD"},
    {"DownLeftVectorBar", "\xE2\xA5\x96"},
    {"DownRightTeeVector", "\xE2\xA5\x9F"},
    {"DownRightVector", "\xE2\x87\x81"},
    {"DownRightVectorBar", "\xE2\xA5\x97"},
    {"DownTee", "\xE2\x8A\xA4"},
    {"DownTeeArrow", "\xE2\x86\xA7"},
    {"Downarrow", "\xE2\x87\x93"},
    {"Dscr", "\xF0\x9D\x92\x9F"},
    {"Dstrok", "\xC4\x90"},
    {"ENG", "\xC5\x8A"},
    {"ETH", "\xC3\x90"},
    {"Eacute", "\xC3\x89"},
    {"Ecaron", "\xC4\x9A"},
    {"Ecirc", "\xC3\x8A"},
    {"Ecy", "\xD0\xAD"},
    {"Edot", "\xC4\x96"},
    {"Efr", "\xF0\x9D\x94\x88"},
    {"Egrave", "\xC3\x88"},
    {"Element", "\xE2\x88\x88"},
    {"Emacr", "\xC4\x92"},
    {"EmptySmallSquare", "\xE2\x97\xBB"},
    {"EmptyVerySmallSquare", "\xE2\x96\xAB"},
    {"Eogon", "\xC4\x98"},
    {"Eopf", "\xF0\x9D\x94\xBC"},
    {"Epsilon", "\xCE\x95"},
    {"Equal", "\xE2\xA9\xB5"},
    {"EqualTilde", "\xE2\x89\x82"},
    {"Equilibrium", "\xE2\x87\x8C"},
    {"Escr", "\xE2\x84\xB0"},
    {"Esim", "\xE2\xA9\xB3"},
    {"Eta", "\xCE\x97"},
    {"Euml", "\xC3\x8B"},
    {"Exists", "\xE2\x88\x83"},
    {"ExponentialE", "\xE2\x85\x87"},
    {"Fcy", "\xD0\xA4"},
    {"Ffr", "\xF0\x9D\x94\x89"},
    {"FilledSmallSquare", "\xE2\x97\xBC"},
    {"FilledVerySmallSquare", "\xE2\x96\xAA"},
    {"Fopf", "\xF0\x9D\x94\xBD"},
    {"ForAll", "\xE2\x88\x80"},
    {"Fouriertrf", "\xE2\x84\xB1"},
    {"Fscr", "\xE2\x84\xB1"},
    {"GJcy", "\xD0\x83"},
    {"GT", ">"},
    {"Gamma", "\xCE\x93"},
    {"Gammad", "\xCF\x9C"},
    {"Gbreve", "\xC4\x9E"},
    {"Gcedil", "\xC4\xA2"},
    {"Gcirc", "\xC4\x9C"},
    {"Gcy", "\xD0\x93"},
    {"Gdot", "\xC4\xA0"},
    {"Gfr", "\xF0\x9D\x94\x8A"},
    {"Gg", "\xE2\x8B\x99"},
    {"Gopf", "\xF0\x9D\x94\xBE"},
    {"GreaterEqual", "\xE2\x89\xA5"},
    {"GreaterEqualLess", "\xE2\x8B\x9B"},
    {"GreaterFullEqual", "\xE2\x89\xA7"},
    {"GreaterGreater", "\xE2\xAA\xA2"},
    {"GreaterLess", "\xE2\x89\xB7"},
    {"GreaterSlantEqual", "\xE2\xA9\xBE"},
    {"GreaterTilde", "\xE2\x89\xB3"},
    {"Gscr", "\xF0\x9D\x92\xA2"},
    {"Gt", "\xE2\x89\xAB"},
    {"HARDcy", "\xD0\xAA"},
    {"Hacek", "\xCB\x87"},
    {"Hat", "^"},
    {"Hcirc", "\xC4\xA4"},
    {"Hfr", "\xE2\x84\x8C"},
    {"HilbertSpace", "\xE2\x84\x8B"},
    {"Hopf", "\xE2\x84\x8D"},
    {"HorizontalLine", "\xE2\x94\x80"},
    {"Hscr", "\xE2\x84\x8B"},
    {"Hstrok", "\xC4\xA6"},
    {"HumpDownHump", "\xE2\x89\x8E"},
    {"HumpEqual", "\xE2\x89\x8F"},
    {"IEcy", "\xD0\x95"},
    {"IJlig", "\xC4\xB2"},
    {"IOcy", "\xD0\x81"},
    {"Iacute", "\xC3\x8D"},
    {"Icirc", "\xC3\x8E"},
    {"Icy", "\xD0\x98"},
    {"Idot", "\xC4\xB0"},
    {"Ifr", "\xE2\x84\x91"},
    {"Igrave", "\xC3\x8C"},
    {"Im", "\xE2\x84\x91"},
    {"Imacr", "\xC4\xAA"},
    {"ImaginaryI", "\xE2\x85\x88"},
    {"Implies", "\xE2\x87\x92"},
    {"Int", "\xE2\x88\xAC"},
    {"Integral", "\xE2\x88\xAB"},
    {"Intersection", "\xE2\x8B\x82"},
    {"InvisibleComma", "\xE2\x81\xA3"},
    {"InvisibleTimes", "\xE2\x81\xA2"},
    {"Iogon", "\xC4\xAE"},
    {"Iopf", "\xF0\x9D\x95\x80"},
    {"Iota", "\xCE\x99"},
    {"Iscr", "\xE2\x84\x90"},
    {"Itilde", "\xC4\xA8"},
    {"Iukcy", "\xD0\x86"},
    {"Iuml", "\xC3\x8F"},
    {"Jcirc", "\xC4\xB4"},
    {"Jcy", "\xD0\x99"},
    {"Jfr", "\xF0\x9D\x94\x8D"},
    {"Jopf", "\xF0\x9D\x95\x81"},
    {"Jscr", "\xF0\x9D\x92\xA5"},
    {"Jsercy", "\xD0\x88"},
    {"Jukcy", "\xD0\x84"},
    {"KHcy", "\xD0\xA5"},
    {"KJcy", "\xD0\x8C"},
    {"Kappa", "\xCE\x9A"},
    {"Kcedil", "\xC4\xB6"},
    {"Kcy", "\xD0\x9A"},
    {"Kfr", "\xF0\x9D\x94\x8E"},
    {"Kopf", "\xF0\x9D\x95\x82"},
    {"Kscr", "\xF0\x9D\x92\xA6"},
    {"LJcy", "\xD0\x89"},
    {"LT", "<"},
    {"Lacute", "\xC4\xB9"},
    {"Lambda", "\xCE\x9B"},
    {"Lang", "\xE2\x9F\xAA"},
    {"Laplacetrf", "\xE2\x84\x92"},
    {"Larr", "\xE2\x86\x9E"},
    {"Lcaron", "\xC4\xBD"},
    {"Lcedil", "\xC4\xBB"},
    {"Lcy", "\xD0\x9B"},
    {"LeftAngleBracket", "\xE2\x9F\xA8"},
    {"LeftArrow", "\xE2\x86\x90"},
    {"LeftArrowBar", "\xE2\x87\xA4"},
    {"LeftArrowRightArrow", "\xE2\x87\x86"},
    {"LeftCeiling", "\xE2\x8C\x88"},
    {"LeftDoubleBracket", "\xE2\x9F\xA6"},
    {"LeftDownTeeVector", "\xE2\xA5\xA1"},
    {"LeftDownVector", "\xE2\x87\x83"},
    {"LeftDownVectorBar", "\xE2\xA5\x99"},
    {"LeftFloor", "\xE2\x8C\x8A"},
    {"LeftRightArrow", "\xE2\x86\x94"},
    {"LeftRightVector", "\xE2\xA5\x8E"},
    {"LeftTee", "\xE2\x8A\xA3"},
    {"LeftTeeArrow", "\xE2\x86\xA4"},
    {"LeftTeeVector", "\xE2\xA5\x9A"},
    {"LeftTriangle", "\xE2\x8A\xB2"},
    {"LeftTriangleBar", "\xE2\xA7\x8F"},
    {"LeftTriangleEqual", "\xE2\x8A\xB4"},
    {"LeftUpDownVector", "\xE2\xA5\x91"},
    {"LeftUpTeeVector", "\xE2\xA5\xA0"},
    {"LeftUpVector", "\xE2\x86\xBF"},
    {"LeftUpVectorBar", "\xE2\xA5\x98"},
    {"LeftVector", "\xE2\x86\xBC"},
    {"LeftVectorBar", "\xE2\xA5\x92"},
    {"Leftarrow", "\xE2\x87\x90"},
    {"Leftrightarrow", "\xE2\x87\x94"},
    {"LessEqualGreater", "\xE2\x8B\x9A"},
    {"LessFullEqual", "\xE2\x89\xA6"},
    {"LessGreater", "\xE2\x89\xB6"},
    {"LessLess", "\xE2\xAA\xA1"},
    {"LessSlantEqual", "\xE2\xA9\xBD"},
    {"LessTilde", "\xE2\x89\xB2"},
    {"Lfr", "\xF0\x9D\x94\x8F"},
    {"Ll", "\xE2\x8B\x98"},
    {"Lleftarrow", "\xE2\x87\x9A"},
    {"Lmidot", "\xC4\xBF"},
    {"LongLeftArrow", "\xE2\x9F\xB5"},
    {"LongLeftRightArrow", "\xE2\x9F\xB7"},
    {"LongRightArrow", "\xE2\x9F\xB6"},
    {"Longleftarrow", "\xE2\x9F\xB8"},
    {"Longleftrightarrow", "\xE2\x9F\xBA"},
    {"Longrightarrow", "\xE2\x9F\xB9"},
    {"Lopf", "\xF0\x9D\x95\x83"},
    {"LowerLeftArrow", "\xE2\x86\x99"},
    {"LowerRightArrow", "\xE2\x86\x98"},
    {"Lscr", "\xE2\x84\x92"},
    {"Lsh", "\xE2\x86\xB0"},
    {"Lstrok", "\xC5\x81"},
    {"Lt", "\xE2\x89\xAA"},
    {"Map", "\xE2\xA4\x85"},
    {"Mcy", "\xD0\x9C"},
    {"MediumSpace", "\xE2\x81\x9F"},
    {"Mellintrf", "\xE2\x84\xB3"},
    {"Mfr", "\xF0\x9D\x94\x90"},
    {"MinusPlus", "\xE2\x88\x93"},
    {"Mopf", "\xF0\x9D\x95\x84"},
    {"Mscr", "\xE2\x84\xB3"},
    {"Mu", "\xCE\x9C"},
    {"NJcy", "\xD0\x8A"},
    {"Nacute", "\xC5\x83"},
    {"Ncaron", "\xC5\x87"},
    {"Ncedil", "\xC5\x85"},
    {"Ncy", "\xD0\x9D"},
    {"NegativeMediumSpace", "\xE2\x80\x8B"},
    {"NegativeThickSpace", "\xE2\x80\x8B"},
    {"NegativeThinSpace", "\xE2\x80\x8B"},
    {"NegativeVeryThinSpace", "\xE2\x80\x8B"},
    {"NestedGreaterGreater", "\xE2\x89\xAB"},
    {"NestedLessLess", "\xE2\x89\xAA"},
    {"NewLine", "\x0A"},
    {"Nfr", "\xF0\x9D\x94\x91"},
    {"NoBreak", "\xE2\x81\xA0"},
    {"NonBreakingSpace", "\xC2\xA0"},
    {"Nopf", "\xE2\x84\x95"},
    {"Not", "\xE2\xAB\xAC"},
    {"NotCongruent", "\xE2\x89\xA2"},
    {"NotCupCap", "\xE2\x89\xAD"},
    {"NotDoubleVerticalBar", "\xE2\x88\xA6"},
    {"NotElement", "\xE2\x88\x89"},
    {"NotEqual", "\xE2\x89\xA0"},
    {"NotEqualTilde", "\xE2\x89\x82\xCC\xB8"},
    {"NotExists", "\xE2\x88\x84"},
    {"NotGreater", "\xE2\x89\xAF"},
    {"NotGreaterEqual", "\xE2\x89\xB1"},
    {"NotGreaterFullEqual", "\xE2\x89\xA7\xCC\xB8"},
    {"NotGreaterGreater", "\xE2\x89\xAB\xCC\xB8"},
    {"NotGreaterLess", "\xE2\x89\xB9"},
    {"NotGreaterSlantEqual", "\xE2\xA9\xBE\xCC\xB8"},
    {"NotGreaterTilde", "\xE2\x89\xB5"},
    {"NotHumpDownHump", "\xE2\x89\x8E\xCC\xB8"},
    {"NotHumpEqual", "\xE2\x89\x8F\xCC\xB8"},
    {"NotLeftTriangle", "\xE2\x8B\xAA"},
    {"NotLeftTriangleBar", "\xE2\xA7\x8F\xCC\xB8"},
    {"NotLeftTriangleEqual", "\xE2\x8B\xAC"},
    {"NotLess", "\xE2\x89\xAE"},
    {"NotLessEqual", "\xE2\x89\xB0"},
    {"NotLessGreater", "\xE2\x89\xB8"},
    {"NotLessLess", "\xE2\x89\xAA\xCC\xB8"},
    {"NotLessSlantEqual", "\xE2\xA9\xBD\xCC\xB8"},
    {"NotLessTilde", "\xE2\x89\xB4"},
    {"NotNestedGreaterGreater", "\xE2\xAA\xA2\xCC\xB8"},
    {"NotNestedLessLess", "\xE2\xAA\xA1\xCC\xB8"},
    {"NotPrecedes", "\xE2\x8A\x80"},
    {"NotPrecedesEqual", "\xE2\xAA\xAF\xCC\xB8"},
    {"NotPrecedesSlantEqual", "\xE2\x8B\xA0"},
    {"NotReverseElement", "\xE2\x88\x8C"},
    {"NotRightTriangle", "\xE2\x8B\xAB"},
    {"NotRightTriangleBar", "\xE2\xA7\x90\xCC\xB8"},
    {"NotRightTriangleEqual", "\xE2\x8B\xAD"},
    {"NotSquareSubset", "\xE2\x8A\x8F\xCC\xB8"},
    {"NotSquareSubsetEqual", "\xE2\x8B\xA2"},
    {"NotSquareSuperset", "\xE2\x8A\x90\xCC\xB8"},
    {"NotSquareSupersetEqual", "\xE2\x8B\xA3"},
    {"NotSubset", "\xE2\x8A\x82\xE2\x83\x92"},
    {"NotSubsetEqual", "\xE2\x8A\x88"},
    {"NotSucceeds", "\xE2\x8A\x81"},
    {"NotSucceedsEqual", "\xE2\xAA\xB0\xCC\xB8"},
    {"NotSucceedsSlantEqual", "\xE2\x8B\xA1"},
    {"NotSucceedsTilde", "\xE2\x89\xBF\xCC\xB8"},
    {"NotSuperset", "\xE2\x8A\x83\xE2\x83\x92"},
    {"NotSupersetEqual", "\xE2\x8A\x89"},
    {"NotTilde", "\xE2\x89\x81"},
    {"NotTildeEqual", "\xE2\x89\x84"},
    {"NotTildeFullEqual", "\xE2\x89\x87"},
    {"NotTildeTilde", "\xE2\x89\x89"},
    {"NotVerticalBar", "\xE2\x88\xA4"},
    {"Nscr", "\xF0\x9D\x92\xA9"},
    {"Ntilde", "\xC3\x91"},
    {"Nu", "\xCE\x9D"},
    {"OElig", "\xC5\x92"},
    {"Oacute", "\xC3\x93"},
    {"Ocirc", "\xC3\x94"},
    {"Ocy", "\xD0\x9E"},
    {"Odblac", "\xC5\x90"},
    {"Ofr", "\xF0\x9D\x94\x92"},
    {"Ograve", "\xC3\x92"},
    {"Omacr", "\xC5\x8C"},
    {"Omega", "\xCE\xA9"},
    {"Omicron", "\xCE\x9F"},
    {"Oopf", "\xF0\x9D\x95\x86"},
    {"OpenCurlyDoubleQuote", "\xE2\x80\x9C"},
    {"OpenCurlyQuote", "\xE2\x80\x98"},
    {"Or", "\xE2\xA9\x94"},
    {"Oscr", "\xF0\x9D\x92\xAA"},
    {"Oslash", "\xC3\x98"},
    {"Otilde", "\xC3\x95"},
    {"Otimes", "\xE2\xA8\xB7"},
    {"Ouml", "\xC3\x96"},
    {"OverBar", "\xE2\x80\xBE"},
    {"OverBrace", "\xE2\x8F\x9E"},
    {"OverBracket", "\xE2\x8E\xB4"},
    {"OverParenthesis", "\xE2\x8F\x9C"},
    {"PartialD", "\xE2\x88\x82"},
    {"Pcy", "\xD0\x9F"},
    {"Pfr", "\xF0\x9D\x94\x93"},
    {"Phi", "\xCE\xA6"},
    {"Pi", "\xCE\xA0"},
    {"PlusMinus", "\xC2\xB1"},
    {"Poincareplane", "\xE2\x84\x8C"},
    {"Popf", "\xE2\x84\x99"},
    {"Pr", "\xE2\xAA\xBB"},
    {"Precedes", "\xE2\x89\xBA"},
    {"PrecedesEqual", "\xE2\xAA\xAF"},
    {"PrecedesSlantEqual", "\xE2\x89\xBC"},
    {"PrecedesTilde", "\xE2\x89\xBE"},
    {"Prime", "\xE2\x80\xB3"},
    {"Product", "\xE2\x88\x8F"},
    {"Proportion", "\xE2\x88\xB7"},
    {"Proportional", "\xE2\x88\x9D"},
    {"Pscr", "\xF0\x9D\x92\xAB"},
    {"Psi", "\xCE\xA8"},
    {"QUOT", "\x22"},
    {"Qfr", "\xF0\x9D\x94\x94"},
    {"Qopf", "\xE2\x84\x9A"},
    {"Qscr", "\xF0\x9D\x92\xAC"},
    {"RBarr", "\xE2\xA4\x90"},
    {"REG", "\xC2\xAE"},
    {"Racute", "\xC5\x94"},
    {"Rang", "\xE2\x9F\xAB"},
    {"Rarr", "\xE2\x86\xA0"},
    {"Rarrtl", "\xE2\xA4\x96"},
    {"Rcaron", "\xC5\x98"},
    {"Rcedil", "\xC5\x96"},
    {"Rcy", "\xD0\xA0"},
    {"Re", "\xE2\x84\x9C"},
    {"ReverseElement", "\xE2\x88\x8B"},
    {"ReverseEquilibrium", "\xE2\x87\x8B"},
    {"ReverseUpEquilibrium", "\xE2\xA5\xAF"},
    {"Rfr", "\xE2\x84\x9C"},
    {"Rho", "\xCE\xA1"},
    {"RightAngleBracket", "\xE2\x9F\xA9"},
    {"RightArrow", "\xE2\x86\x92"},
    {"RightArrowBar", "\xE2\x87\xA5"},
    {"RightArrowLeftArrow", "\xE2\x87\x84"},
    {"RightCeiling", "\xE2\x8C\x89"},
    {"RightDoubleBracket", "\xE2\x9F\xA7"},
    {"RightDownTeeVector", "\xE2\xA5\x9D"},
    {"RightDownVector", "\xE2\x87\x82"},
    {"RightDownVectorBar", "\xE2\xA5\x95"},
    {"RightFloor", "\xE2\x8C\x8B"},
    {"RightTee", "\xE2\x8A\xA2"},
    {"RightTeeArrow", "\xE2\x86\xA6"},
    {"RightTeeVector", "\xE2\xA5\x9B"},
    {"RightTriangle", "\xE2\x8A\xB3"},
    {"RightTriangleBar", "\xE2\xA7\x90"},
    {"RightTriangleEqual", "\xE2\x8A\xB5"},
    {"RightUpDownVector", "\xE2\xA5\x8F"},
    {"RightUpTeeVector", "\xE2\xA5\x9C"},
    {"RightUpVector", "\xE2\x86\xBE"},
    {"RightUpVectorBar", "\xE2\xA5\x94"},
    {"RightVector", "\xE2\x87\x80"},
    {"RightVectorBar", "\xE2\xA5\x93"},
    {"Rightarrow", "\xE2\x87\x92"},
    {"Ropf", "\xE2\x84\x9D"},
    {"RoundImplies", "\xE2\xA5\xB0"},
    {"Rrightarrow", "\xE2\x87\x9B"},
    {"Rscr", "\xE2\x84\x9B"},
    {"Rsh", "\xE2\x86\xB1"},
    {"RuleDelayed", "\xE2\xA7\xB4"},
    {"SHCHcy", "\xD0\xA9"},
    {"SHcy", "\xD0\xA8"},
    {"SOFTcy", "\xD0\xAC"},
    {"Sacute", "\xC5\x9A"},
    {"Sc", "\xE2\xAA\xBC"},
    {"Scaron", "\xC5\xA0"},
    {"Scedil", "\xC5\x9E"},
    {"Scirc", "\xC5\x9C"},
    {"Scy", "\xD0\xA1"},
    {"Sfr", "\xF0\x9D\x94\x96"},
    {"ShortDownArrow", "\xE2\x86\x93"},
    {"ShortLeftArrow", "\xE2\x86\x90"},
    {"ShortRightArrow", "\xE2\x86\x92"},
    {"ShortUpArrow", "\xE2\x86\x91"},
    {"Sigma", "\xCE\xA3"},
    {"SmallCircle", "\xE2\x88\x98"},
    {"Sopf", "\xF0\x9D\x95\x8A"},
    {"Sqrt", "\xE2\x88\x9A"},
    {"Square", "\xE2\x96\xA1"},
    {"SquareIntersection", "\xE2\x8A\x93"},
    {"SquareSubset", "\xE2\x8A\x8F"},
    {"SquareSubsetEqual", "\xE2\x8A\x91"},
    {"SquareSuperset", "\xE2\x8A\x90"},
    {"SquareSupersetEqual", "\xE2\x8A\x92"},
    {"SquareUnion", "\xE2\x8A\x94"},
    {"Sscr", "\xF0\x9D\x92\xAE"},
    {"Star", "\xE2\x8B\x86"},
    {"Sub", "\xE2\x8B\x90"},
    {"Subset", "\xE2\x8B\x90"},
    {"SubsetEqual", "\xE2\x8A\x86"},
    {"Succeeds", "\xE2\x89\xBB"},
    {"SucceedsEqual", "\xE2\xAA\xB0"},
    {"SucceedsSlantEqual", "\xE2\x89\xBD"},
    {"SucceedsTilde", "\xE2\x89\xBF"},
    {"SuchThat", "\xE2\x88\x8B"},
    {"Sum", "\xE2\x88\x91"},
    {"Sup", "\xE2\x8B\x91"},
    {"Superset", "\xE2\x8A\x83"},
    {"SupersetEqual", "\xE2\x8A\x87"},
    {"Supset", "\xE2\x8B\x91"},
    {"THORN", "\xC3\x9E"},
    {"TRADE", "\xE2\x84\xA2"},
    {"TSHcy", "\xD0\x8B"},
    {"TScy", "\xD0\xA6"},
    {"Tab", "\x09"},
    {"Tau", "\xCE\xA4"},
    {"Tcaron", "\xC5\xA4"},
    {"Tcedil", "\xC5\xA2"},
    {"Tcy", "\xD0\xA2"},
    {"Tfr", "\xF0\x9D\x94\x97"},
    {"Therefore", "\xE2\x88\xB4"},
    {"Theta", "\xCE\x98"},
    {"ThickSpace", "\xE2\x81\x9F\xE2\x80\x8A"},
    {"ThinSpace", "\xE2\x80\x89"},
    {"Tilde", "\xE2\x88\xBC"},
    {"TildeEqual", "\xE2\x89\x83"},
    {"TildeFullEqual", "\xE2\x89\x85"},
    {"TildeTilde", "\xE2\x89\x88"},
    {"Topf", "\xF0\x9D\x95\x8B"},
    {"TripleDot", "\xE2\x83\x9B"},
    {"Tscr", "\xF0\x9D\x92\xAF"},
    {"Tstrok", "\xC5\xA6"},
    {"Uacute", "\xC3\x9A"},
    {"Uarr", "\xE2\x86\x9F"},
    {"Uarrocir", "\xE2\xA5\x89"},
    {"Ubrcy", "\xD0\x8E"},
    {"Ubreve", "\xC5\xAC"},
    {"Ucirc", "\xC3\x9B"},
    {"Ucy", "\xD0\xA3"},
    {"Udblac", "\xC5\xB0"},
    {"Ufr", "\xF0\x9D\x94\x98"},
    {"Ugrave", "\xC3\x99"},
    {"Umacr", "\xC5\xAA"},
    {"UnderBar", "_"},
    {"UnderBrace", "\xE2\x8F\x9F"},
    {"UnderBracket", "\xE2\x8E\xB5"},
    {"UnderParenthesis", "\xE2\x8F\x9D"},
    {"Union", "\xE2\x8B\x83"},
    {"UnionPlus", "\xE2\x8A\x8E"},
    {"Uogon", "\xC5\xB2"},
    {"Uopf", "\xF0\x9D\x95\x8C"},
    {"UpArrow", "\xE2\x86\x91"},
    {"UpArrowBar", "\xE2\xA4\x92"},
    {"UpArrowDownArrow", "\xE2\x87\x85"},
    {"UpDownArrow", "\xE2\x86\x95"},
    {"UpEquilibrium", "\xE2\xA5\xAE"},
    {"UpTee", "\xE2\x8A\xA5"},
    {"UpTeeArrow", "\xE2\x86\xA5"},
    {"Uparrow", "\xE2\x87\x91"},
    {"Updownarrow", "\xE2\x87\x95"},
    {"UpperLeftArrow", "\xE2\x86\x96"},
    {"UpperRightArrow", "\xE2\x86\x97"},
    {"Upsi", "\xCF\x92"},
    {"Upsilon", "\xCE\xA5"},
    {"Uring", "\xC5\xAE"},
    {"Uscr", "\xF0\x9D\x92\xB0"},
    {"Utilde", "\xC5\xA8"},
    {"Uuml", "\xC3\x9C"},
    {"VDash", "\xE2\x8A\xAB"},
    {"Vbar", "\xE2\xAB\xAB"},
    {"Vcy", "\xD0\x92"},
    {"Vdash", "\xE2\x8A\xA9"},
    {"Vdashl", "\xE2\xAB\xA6"},
    {"Vee", "\xE2\x8B\x81"},
    {"Verbar", "\xE2\x80\x96"},
    {"Vert", "\xE2\x80\x96"},
    {"VerticalBar", "\xE2\x88\xA3"},
    {"VerticalLine", "|"},
    {"VerticalSeparator", "\xE2\x9D\x98"},
    {"VerticalTilde", "\xE2\x89\x80"},
    {"VeryThinSpace", "\xE2\x80\x8A"},
    {"Vfr", "\xF0\x9D\x94\x99"},
    {"Vopf", "\xF0\x9D\x95\x8D"},
    {"Vscr", "\xF0\x9D\x92\xB1"},
    {"Vvdash", "\xE2\x8A\xAA"},
    {"Wcirc", "\xC5\xB4"},
    {"Wedge", "\xE2\x8B\x80"},
    {"Wfr", "\xF0\x9D\x94\x9A"},
    {"Wopf", "\xF0\x9D\x95\x8E"},
    {"Wscr", "\xF0\x9D\x92\xB2"},
    {"Xfr", "\xF0\x9D\x94\x9B"},
    {"Xi", "\xCE\x9E"},
    {"Xopf", "\xF0\x9D\x95\x8F"},
    {"Xscr", "\xF0\x9D\x92\xB3"},
    {"YAcy", "\xD0\xAF"},
    {"YIcy", "\xD0\x87"},
    {"YUcy", "\xD0\xAE"},
    {"Yacute", "\xC3\x9D"},
    {"Ycirc", "\xC5\xB6"},
    {"Ycy", "\xD0\xAB"},
    {"Yfr", "\xF0\x9D\x94\x9C"},
    {"Yopf", "\xF0\x9D\x95\x90"},
    {"Yscr", "\xF0\x9D\x92\xB4"},
    {"Yuml", "\xC5\xB8"},
    {"ZHcy", "\xD0\x96"},
    {"Zacute", "\xC5\xB9"},
    {"Zcaron", "\xC5\xBD"},
    {"Zcy", "\xD0\x97"},
    {"Zdot", "\xC5\xBB"},
    {"ZeroWidthSpace", "\xE2\x80\x8B"},
    {"Zeta", "\xCE\x96"},
    {"Zfr", "\xE2\x84\xA8"},
    {"Zopf", "\xE2\x84\xA4"},
    {"Zscr", "\xF0\x9D\x92\xB5"},
    {"aacute", "\xC3\xA1"},
    {"abreve", "\xC4\x83"},
    {"ac", "\xE2\x88\xBE"},
    {"acE", "\xE2\x88\xBE\xCC\xB3"},
    {"acd", "\xE2\x88\xBF"},
    {"acirc", "\xC3\xA2"},
    {"acute", "\xC2\xB4"},
    {"acy", "\xD0\xB0"},
    {"aelig", "\xC3\xA6"},
    {"af", "\xE2\x81\xA1"},
    {"afr", "\xF0\x9D\x94\x9E"},
    {"agrave", "\xC3\xA0"},
    {"alefsym", "\xE2\x84\xB5"},
    {"aleph", "\xE2\x84\xB5"},
    {"alpha", "\xCE\xB1"},
    {"amacr", "\xC4\x81"},
    {"amalg", "\xE2\xA8\xBF"},
    {"amp", "&"},
    {"and", "\xE2\x88\xA7"},
    {"andand", "\xE2\xA9\x95"},
    {"andd", "\xE2\xA9\x9C"},
    {"andslope", "\xE2\xA9\x98"},
    {"andv", "\xE2\xA9\x9A"},
    {"ang", "\xE2\x88\xA0"},
    {"ange", "\xE2\xA6\xA4"},
    {"angle", "\xE2\x88\xA0"},
    {"angmsd", "\xE2\x88\xA1"},
    {"angmsdaa", "\xE2\xA6\xA8"},
    {"angmsdab", "\xE2\xA6\xA9"},
    {"angmsdac", "\xE2\xA6\xAA"},
    {"angmsdad", "\xE2\xA6\xAB"},
    {"angmsdae", "\xE2\xA6\xAC"},
    {"angmsdaf", "\xE2\xA6\xAD"},
    {"angmsdag", "\xE2\xA6\xAE"},
    {"angmsdah", "\xE2\xA6\xAF"},
    {"angrt", "\xE2\x88\x9F"},
    {"angrtvb", "\xE2\x8A\xBE"},
    {"angrtvbd", "\xE2\xA6\x9D"},
    {"angsph", "\xE2\x88\xA2"},
    {"angst", "\xC3\x85"},
    {"angzarr", "\xE2\x8D\xBC"},
    {"aogon", "\xC4\x85"},
    {"aopf", "\xF0\x9D\x95\x92"},
    {"ap", "\xE2\x89\x88"},
    {"apE", "\xE2\xA9\xB0"},
    {"apacir", "\xE2\xA9\xAF"},
    {"ape", "\xE2\x89\x8A"},
    {"apid", "\xE2\x89\x8B"},
    {"apos", "'"},
    {"approx", "\xE2\x89\x88"},
    {"approxeq", "\xE2\x89\x8A"},
    {"aring", "\xC3\xA5"},
    {"ascr", "\xF0\x9D\x92\xB6"},
    {"ast", "*"},
    {"asymp", "\xE2\x89\x88"},
    {"asympeq", "\xE2\x89\x8D"},
    {"atilde", "\xC3\xA3"},
    {"auml", "\xC3\xA4"},
    {"awconint", "\xE2\x88\xB3"},
    {"awint", "\xE2\xA8\x91"},
    {"bNot", "\xE2\xAB\xAD"},
    {"backcong", "\xE2\x89\x8C"},
    {"backepsilon", "\xCF\xB6"},
    {"backprime", "\xE2\x80\xB5"},
    {"backsim", "\xE2\x88\xBD"},
    {"backsimeq", "\xE2\x8B\x8D"},
    {"barvee", "\xE2\x8A\xBD"},
    {"barwed", "\xE2\x8C\x85"},
    {"barwedge", "\xE2\x8C\x85"},
    {"bbrk", "\xE2\x8E\xB5"},
    {"bbrktbrk", "\xE2\x8E\xB6"},
    {"bcong", "\xE2\x89\x8C"},
    {"bcy", "\xD0\xB1"},
    {"bdquo", "\xE2\x80\x9E"},
    {"becaus", "\xE2\x88\xB5"},
    {"because", "\xE2\x88\xB5"},
    {"bemptyv", "\xE2\xA6\xB0"},
    {"bepsi", "\xCF\xB6"},
    {"bernou", "\xE2\x84\xAC"},
    {"beta", "\xCE\xB2"},
    {"beth", "\xE2\x84\xB6"},
    {"between", "\xE2\x89\xAC"},
    {"bfr", "\xF0\x9D\x94\x9F"},
    {"bigcap", "\xE2\x8B\x82"},
    {"bigcirc", "\xE2\x97\xAF"},
    {"bigcup", "\xE2\x8B\x83"},
    {"bigodot", "\xE2\xA8\x80"},
    {"bigoplus", "\xE2\xA8\x81"},
    {"bigotimes", "\xE2\xA8\x82"},
    {"bigsqcup", "\xE2\xA8\x86"},
    {"bigstar", "\xE2\x98\x85"},
    {"bigtriangledown", "\xE2\x96\xBD"},
    {"bigtriangleup", "\xE2\x96\xB3"},
    {"biguplus", "\xE2\xA8\x84"},
    {"bigvee", "\xE2\x8B\x81"},
    {"bigwedge", "\xE2\x8B\x80"},
    {"bkarow", "\xE2\xA4\x8D"},
    {"blacklozenge", "\xE2\xA7\xAB"},
    {"blacksquare", "\xE2\x96\xAA"},
    {"blacktriangle", "\xE2\x96\xB4"},
    {"blacktriangledown", "\xE2\x96\xBE"},
    {"blacktriangleleft", "\xE2\x97\x82"},
    {"blacktriangleright", "\xE2\x96\xB8"},
    {"blank", "\xE2\x90\xA3"},
    {"blk12", "\xE2\x96\x92"},
    {"blk14", "\xE2\x96\x91"},
    {"blk34", "\xE2\x96\x93"},
    {"block", "\xE2\x96\x88"},
    {"bne", "=\xE2\x83\xA5"},
    {"bnequiv", "\xE2\x89\xA1\xE2\x83\xA5"},
    {"bnot", "\xE2\x8C\x90"},
    {"bopf", "\xF0\x9D\x95\x93"},
    {"bot", "\xE2\x8A\xA5"},
    {"bottom", "\xE2\x8A\xA5"},
    {"bowtie", "\xE2\x8B\x88"},
    {"boxDL", "\xE2\x95\x97"},
    {"boxDR", "\xE2\x95\x94"},
    {"boxDl", "\xE2\x95\x96"},
    {"boxDr", "\xE2\x95\x93"},
    {"boxH", "\xE2\x95\x90"},
    {"boxHD", "\xE2\x95\xA6"},
    {"boxHU", "\xE2\x95\xA9"},
    {"boxHd", "\xE2\x95\xA4"},
    {"boxHu", "\xE2\x95\xA7"},
    {"boxUL", "\xE2\x95\x9D"},
    {"boxUR", "\xE2\x95\x9A"},
    {"boxUl", "\xE2\x95\x9C"},
    {"boxUr", "\xE2\x95\x99"},
    {"boxV", "\xE2\x95\x91"},
    {"boxVH", "\xE2\x95\xAC"},
    {"boxVL", "\xE2\x95\xA3"},
    {"boxVR", "\xE2\x95\xA0"},
    {"boxVh", "\xE2\x95\xAB"},
    {"boxVl", "\xE2\x95\xA2"},
    {"boxVr", "\xE2\x95\x9F"},
    {"boxbox", "\xE2\xA7\x89"},
    {"boxdL", "\xE2\x95\x95"},
    {"boxdR", "\xE2\x95\x92"},
    {"boxdl", "\xE2\x94\x90"},
    {"boxdr", "\xE2\x94\x8C"},
    {"boxh", "\xE2\x94\x80"},
    {"boxhD", "\xE2\x95\xA5"},
    {"boxhU", "\xE2\x95\xA8"},
    {"boxhd", "\xE2\x94\xAC"},
    {"boxhu", "\xE2\x94\xB4"},
    {"boxminus", "\xE2\x8A\x9F"},
    {"boxplus", "\xE2\x8A\x9E"},
    {"boxtimes", "\xE2\x8A\xA0"},
    {"boxuL", "\xE2\x95\x9B"},
    {"boxuR", "\xE2\x95\x98"},
    {"boxul", "\xE2\x94\x98"},
    {"boxur", "\xE2\x94\x94"},
    {"boxv", "\xE2\x94\x82"},
    {"boxvH", "\xE2\x95\xAA"},
    {"boxvL", "\xE2\x95\xA1"},
    {"boxvR", "\xE2\x95\x9E"},
    {"boxvh", "\xE2\x94\xBC"},
    {"boxvl", "\xE2\x94\xA4"},
    {"boxvr", "\xE2\x94\x9C"},
    {"bprime", "\xE2\x80\xB5"},
    {"breve", "\xCB\x98"},
    {"brvbar", "\xC2\xA6"},
    {"bscr", "\xF0\x9D\x92\xB7"},
    {"bsemi", "\xE2\x81\x8F"},
    {"bsim", "\xE2\x88\xBD"},
    {"bsime", "\xE2\x8B\x8D"},
    {"bsol", "\x5C"},
    {"bsolb", "\xE2\xA7\x85"},
    {"bsolhsub", "\xE2\x9F\x88"},
    {"bull", "\xE2\x80\xA2"},
    {"bullet", "\xE2\x80\xA2"},
    {"bump", "\xE2\x89\x8E"},
    {"bumpE", "\xE2\xAA\xAE"},
    {"bumpe", "\xE2\x89\x8F"},
    {"bumpeq", "\xE2\x89\x8F"},
    {"cacute", "\xC4\x87"},
    {"cap", "\xE2\x88\xA9"},
    {"capand", "\xE2\xA9\x84"},
    {"capbrcup", "\xE2\xA9\x89"},
    {"capcap", "\xE2\xA9\x8B"},
    {"capcup", "\xE2\xA9\x87"},
    {"capdot", "\xE2\xA9\x80"},
    {"caps", "\xE2\x88\xA9\xEF\xB8\x80"},
    {"caret", "\xE2\x81\x81"},
    {"caron", "\xCB\x87"},
    {"ccaps", "\xE2\xA9\x8D"},
    {"ccaron", "\xC4\x8D"},
    {"ccedil", "\xC3\xA7"},
    {"ccirc", "\xC4\x89"},
    {"ccups", "\xE2\xA9\x8C"},
    {"ccupssm", "\xE2\xA9\x90"},
    {"cdot", "\xC4\x8B"},
    {"cedil", "\xC2\xB8"},
    {"cemptyv", "\xE2\xA6\xB2"},
    {"cent", "\xC2\xA2"},
    {"centerdot", "\xC2\xB7"},
    {"cfr", "\xF0\x9D\x94\xA0"},
    {"chcy", "\xD1\x87"},
    {"check", "\xE2\x9C\x93"},
    {"checkmark", "\xE2\x9C\x93"},
    {"chi", "\xCF\x87"},
    {"cir", "\xE2\x97\x8B"},
    {"cirE", "\xE2\xA7\x83"},
    {"circ", "\xCB\x86"},
    {"circeq", "\xE2\x89\x97"},
    {"circlearrowleft", "\xE2\x86\xBA"},
    {"circlearrowright", "\xE2\x86\xBB"},
    {"circledR", "\xC2\xAE"},
    {"circledS", "\xE2\x93\x88"},
    {"circledast", "\xE2\x8A\x9B"},
    {"circledcirc", "\xE2\x8A\x9A"},
    {"circleddash", "\xE2\x8A\x9D"},
    {"cire", "\xE2\x89\x97"},
    {"cirfnint", "\xE2\xA8\x90"},
    {"cirmid", "\xE2\xAB\xAF"},
    {"cirscir", "\xE2\xA7\x82"},
    {"clubs", "\xE2\x99\xA3"},
    {"clubsuit", "\xE2\x99\xA3"},
    {"colon", ":"},
    {"colone", "\xE2\x89\x94"},
    {"coloneq", "\xE2\x89\x94"},
    {"comma", ","},
    {"commat", "@"},
    {"comp", "\xE2\x88\x81"},
    {"compfn", "\xE2\x88\x98"},
    {"complement", "\xE2\x88\x81"},
    {"complexes", "\xE2\x84\x82"},
    {"cong", "\xE2\x89\x85"},
    {"congdot", "\xE2\xA9\xAD"},
    {"conint", "\xE2\x88\xAE"},
    {"copf", "\xF0\x9D\x95\x94"},
    {"coprod", "\xE2\x88\x90"},
    {"copy", "\xC2\xA9"},
    {"copysr", "\xE2\x84\x97"},
    {"crarr", "\xE2\x86\xB5"},
    {"cross", "\xE2\x9C\x97"},
    {"cscr", "\xF0\x9D\x92\xB8"},
    {"csub", "\xE2\xAB\x8F"},
    {"csube", "\xE2\xAB\x91"},
    {"csup", "\xE2\xAB\x90"},
    {"csupe", "\xE2\xAB\x92"},
    {"ctdot", "\xE2\x8B\xAF"},
    {"cudarrl", "\xE2\xA4\xB8"},
    {"cudarrr", "\xE2\xA4\xB5"},
    {"cuepr", "\xE2\x8B\x9E"},
    {"cuesc", "\xE2\x8B\x9F"},
    {"cularr", "\xE2\x86\xB6"},
    {"cularrp", "\xE2\xA4\xBD"},
    {"cup", "\xE2\x88\xAA"},
    {"cupbrcap", "\xE2\xA9\x88"},
    {"cupcap", "\xE2\xA9\x86"},
    {"cupcup", "\xE2\xA9\x8A"},
    {"cupdot", "\xE2\x8A\x8D"},
    {"cupor", "\xE2\xA9\x85"},
    {"cups", "\xE2\x88\xAA\xEF\xB8\x80"},
    {"curarr", "\xE2\x86\xB7"},
    {"curarrm", "\xE2\xA4\xBC"},
    {"curlyeqprec", "\xE2\x8B\x9E"},
    {"curlyeqsucc", "\xE2\x8B\x9F"},
    {"curlyvee", "\xE2\x8B\x8E"},
    {"curlywedge", "\xE2\x8B\x8F"},
    {"curren", "\xC2\xA4"},
    {"curvearrowleft", "\xE2\x86\xB6"},
    {"curvearrowright", "\xE2\x86\xB7"},
    {"cuvee", "\xE2\x8B\x8E"},
    {"cuwed", "\xE2\x8B\x8F"},
    {"cwconint", "\xE2\x88\xB2"},
    {"cwint", "\xE2\x88\xB1"},
    {"cylcty", "\xE2\x8C\xAD"},
    {"dArr", "\xE2\x87\x93"},
    {"dHar", "\xE2\xA5\xA5"},
    {"dagger", "\xE2\x80\xA0"},
    {"daleth", "\xE2\x84\xB8"},
    {"darr", "\xE2\x86\x93"},
    {"dash", "\xE2\x80\x90"},
    {"dashv", "\xE2\x8A\xA3"},
    {"dbkarow", "\xE2\xA4\x8F"},
    {"dblac", "\xCB\x9D"},
    {"dcaron", "\xC4\x8F"},
    {"dcy", "\xD0\xB4"},
    {"dd", "\xE2\x85\x86"},
    {"ddagger", "\xE2\x80\xA1"},
    {"ddarr", "\xE2\x87\x8A"},
    {"ddotseq", "\xE2\xA9\xB7"},
    {"deg", "\xC2\xB0"},
    {"delta", "\xCE\xB4"},
    {"demptyv", "\xE2\xA6\xB1"},
    {"dfisht", "\xE2\xA5\xBF"},
    {"dfr", "\xF0\x9D\x94\xA1"},
    {"dharl", "\xE2\x87\x83"},
    {"dharr", "\xE2\x87\x82"},
    {"diam", "\xE2\x8B\x84"},
    {"diamond", "\xE2\x8B\x84"},
    {"diamondsuit", "\xE2\x99\xA6"},
    {"diams", "\xE2\x99\xA6"},
    {"die", "\xC2\xA8"},
    {"digamma", "\xCF\x9D"},
    {"disin", "\xE2\x8B\xB2"},
    {"div", "\xC3\xB7"},
    {"divide", "\xC3\xB7"},
    {"divideontimes", "\xE2\x8B\x87"},
    {"divonx", "\xE2\x8B\x87"},
    {"djcy", "\xD1\x92"},
    {"dlcorn", "\xE2\x8C\x9E"},
    {"dlcrop", "\xE2\x8C\x8D"},
    {"dollar", "$"},
    {"dopf", "\xF0\x9D\x95\x95"},
    {"dot", "\xCB\x99"},
    {"doteq", "\xE2\x89\x90"},
    {"doteqdot", "\xE2\x89\x91"},
    {"dotminus", "\xE2\x88\xB8"},
    {"dotplus", "\xE2\x88\x94"},
    {"dotsquare", "\xE2\x8A\xA1"},
    {"doublebarwedge", "\xE2\x8C\x86"},
    {"downarrow", "\xE2\x86\x93"},
    {"downdownarrows", "\xE2\x87\x8A"},
    {"downharpoonleft", "\xE2\x87\x83"},
    {"downharpoonright", "\xE2\x87\x82"},
    {"drbkarow", "\xE2\xA4\x90"},
    {"drcorn", "\xE2\x8C\x9F"},
    {"drcrop", "\xE2\x8C\x8C"},
    {"dscr", "\xF0\x9D\x92\xB9"},
    {"dscy", "\xD1\x95"},
    {"dsol", "\xE2\xA7\xB6"},
    {"dstrok", "\xC4\x91"},
    {"dtdot", "\xE2\x8B\xB1"},
    {"dtri", "\xE2\x96\xBF"},
    {"dtrif", "\xE2\x96\xBE"},
    {"duarr", "\xE2\x87\xB5"},
    {"duhar", "\xE2\xA5\xAF"},
    {"dwangle", "\xE2\xA6\xA6"},
    {"dzcy", "\xD1\x9F"},
    {"dzigrarr", "\xE2\x9F\xBF"},
    {"eDDot", "\xE2\xA9\xB7"},
    {"eDot", "\xE2\x89\x91"},
    {"eacute", "\xC3\xA9"},
    {"easter", "\xE2\xA9\xAE"},
    {"ecaron", "\xC4\x9B"},
    {"ecir", "\xE2\x89\x96"},
    {"ecirc", "\xC3\xAA"},
    {"ecolon", "\xE2\x89\x95"},
    {"ecy", "\xD1\x8D"},
    {"edot", "\xC4\x97"},
    {"ee", "\xE2\x85\x87"},
    {"efDot", "\xE2\x89\x92"},
    {"efr", "\xF0\x9D\x94\xA2"},
    {"eg", "\xE2\xAA\x9A"},
    {"egrave", "\xC3\xA8"},
    {"egs", "\xE2\xAA\x96"},
    {"egsdot", "\xE2\xAA\x98"},
    {"el", "\xE2\xAA\x99"},
    {"elinters", "\xE2\x8F\xA7"},
    {"ell", "\xE2\x84\x93"},
    {"els", "\xE2\xAA\x95"},
    {"elsdot", "\xE2\xAA\x97"},
    {"emacr", "\xC4\x93"},
    {"empty", "\xE2\x88\x85"},
    {"emptyset", "\xE2\x88\x85"},
    {"emptyv", "\xE2\x88\x85"},
    {"emsp", "\xE2\x80\x83"},
    {"emsp13", "\xE2\x80\x84"},
    {"emsp14", "\xE2\x80\x85"},
    {"eng", "\xC5\x8B"},
    {"ensp", "\xE2\x80\x82"},
    {"eogon", "\xC4\x99"},
    {"eopf", "\xF0\x9D\x95\x96"},
    {"epar", "\xE2\x8B\x95"},
    {"eparsl", "\xE2\xA7\xA3"},
    {"eplus", "\xE2\xA9\xB1"},
    {"epsi", "\xCE\xB5"},
    {"epsilon", "\xCE\xB5"},
    {"epsiv", "\xCF\xB5"},
    {"eqcirc", "\xE2\x89\x96"},
    {"eqcolon", "\xE2\x89\x95"},
    {"eqsim", "\xE2\x89\x82"},
    {"eqslantgtr", "\xE2\xAA\x96"},
    {"eqslantless", "\xE2\xAA\x95"},
    {"equals", "="},
    {"equest", "\xE2\x89\x9F"},
    {"equiv", "\xE2\x89\xA1"},
    {"equivDD", "\xE2\xA9\xB8"},
    {"eqvparsl", "\xE2\xA7\xA5"},
    {"erDot", "\xE2\x89\x93"},
    {"erarr", "\xE2\xA5\xB1"},
    {"escr", "\xE2\x84\xAF"},
    {"esdot", "\xE2\x89\x90"},
    {"esim", "\xE2\x89\x82"},
    {"eta", "\xCE\xB7"},
    {"eth", "\xC3\xB0"},
    {"euml", "\xC3\xAB"},
    {"euro", "\xE2\x82\xAC"},
    {"excl", "!"},
    {"exist", "\xE2\x88\x83"},
    {"expectation", "\xE2\x84\xB0"},
    {"exponentiale", "\xE2\x85\x87"},
    {"fallingdotseq", "\xE2\x89\x92"},
    {"fcy", "\xD1\x84"},
    {"female", "\xE2\x99\x80"},
    {"ffilig", "\xEF\xAC\x83"},
    {"fflig", "\xEF\xAC\x80"},
    {"ffllig", "\xEF\xAC\x84"},
    {"ffr", "\xF0\x9D\x94\xA3"},
    {"filig", "\xEF\xAC\x81"},
    {"fjlig", "fj"},
    {"flat", "\xE2\x99\xAD"},
    {"fllig", "\xEF\xAC\x82"},
    {"fltns", "\xE2\x96\xB1"},
    {"fnof", "\xC6\x92"},
    {"fopf", "\xF0\x9D\x95\x97"},
    {"forall", "\xE2\x88\x80"},
    {"fork", "\xE2\x8B\x94"},
    {"forkv", "\xE2\xAB\x99"},
    {"fpartint", "\xE2\xA8\x8D"},
    {"frac12", "\xC2\xBD"},
    {"frac13", "\xE2\x85\x93"},
    {"frac14", "\xC2\xBC"},
    {"frac15", "\xE2\x85\x95"},
    {"frac16", "\xE2\x85\x99"},
    {"frac18", "\xE2\x85\x9B"},
    {"frac23", "\xE2\x85\x94"},
    {"frac25", "\xE2\x85\x96"},
    {"frac34", "\xC2\xBE"},
    {"frac35", "\xE2\x85\x97"},
    {"frac38", "\xE2\x85\x9C"},
    {"frac45", "\xE2\x85\x98"},
    {"frac56", "\xE2\x85\x9A"},
    {"frac58", "\xE2\x85\x9D"},
    {"frac78", "\xE2\x85\x9E"},
    {"frasl", "\xE2\x81\x84"},
    {"frown", "\xE2\x8C\xA2"},
    {"fscr", "\xF0\x9D\x92\xBB"},
    {"gE", "\xE2\x89\xA7"},
    {"gEl", "\xE2\xAA\x8C"},
    {"gacute", "\xC7\xB5"},
    {"gamma", "\xCE\xB3"},
    {"gammad", "\xCF\x9D"},
    {"gap", "\xE2\xAA\x86"},
    {"gbreve", "\xC4\x9F"},
    {"gcirc", "\xC4\x9D"},
    {"gcy", "\xD0\xB3"},
    {"gdot", "\xC4\xA1"},
    {"ge", "\xE2\x89\xA5"},
    {"gel", "\xE2\x8B\x9B"},
    {"geq", "\xE2\x89\xA5"},
    {"geqq", "\xE2\x89\xA7"},
    {"geqslant", "\xE2\xA9\xBE"},
    {"ges", "\xE2\xA9\xBE"},
    {"gescc", "\xE2\xAA\xA9"},
    {"gesdot", "\xE2\xAA\x80"},
    {"gesdoto", "\xE2\xAA\x82"},
    {"gesdotol", "\xE2\xAA\x84"},
    {"gesl", "\xE2\x8B\x9B\xEF\xB8\x80"},
    {"gesles", "\xE2\xAA\x94"},
    {"gfr", "\xF0\x9D\x94\xA4"},
    {"gg", "\xE2\x89\xAB"},
    {"ggg", "\xE2\x8B\x99"},
    {"gimel", "\xE2\x84\xB7"},
    {"gjcy", "\xD1\x93"},
    {"gl", "\xE2\x89\xB7"},
    {"glE", "\xE2\xAA\x92"},
    {"gla", "\xE2\xAA\xA5"},
    {"glj", "\xE2\xAA\xA4"},
    {"gnE", "\xE2\x89\xA9"},
    {"gnap", "\xE2\xAA\x8A"},
    {"gnapprox", "\xE2\xAA\x8A"},
    {"gne", "\xE2\xAA\x88"},
    {"gneq", "\xE2\xAA\x88"},
    {"gneqq", "\xE2\x89\xA9"},
    {"gnsim", "\xE2\x8B\xA7"},
    {"gopf", "\xF0\x9D\x95\x98"},
    {"grave", "`"},
    {"gscr", "\xE2\x84\x8A"},
    {"gsim", "\xE2\x89\xB3"},
    {"gsime", "\xE2\xAA\x8E"},
    {"gsiml", "\xE2\xAA\x90"},
    {"gt", ">"},
    {"gtcc", "\xE2\xAA\xA7"},
    {"gtcir", "\xE2\xA9\xBA"},
    {"gtdot", "\xE2\x8B\x97"},
    {"gtlPar", "\xE2\xA6\x95"},
    {"gtquest", "\xE2\xA9\xBC"},
    {"gtrapprox", "\xE2\xAA\x86"},
    {"gtrarr", "\xE2\xA5\xB8"},
    {"gtrdot", "\xE2\x8B\x97"},
    {"gtreqless", "\xE2\x8B\x9B"},
    {"gtreqqless", "\xE2\xAA\x8C"},
    {"gtrless", "\xE2\x89\xB7"},
    {"gtrsim", "\xE2\x89\xB3"},
    {"gvertneqq", "\xE2\x89\xA9\xEF\xB8\x80"},
    {"gvnE", "\xE2\x89\xA9\xEF\xB8\x80"},
    {"hArr", "\xE2\x87\x94"},
    {"hairsp", "\xE2\x80\x8A"},
    {"half", "\xC2\xBD"},
    {"hamilt", "\xE2\x84\x8B"},
    {"hardcy", "\xD1\x8A"},
    {"harr", "\xE2\x86\x94"},
    {"harrcir", "\xE2\xA5\x88"},
    {"harrw", "\xE2\x86\xAD"},
    {"hbar", "\xE2\x84\x8F"},
    {"hcirc", "\xC4\xA5"},
    {"hearts", "\xE2\x99\xA5"},
    {"heartsuit", "\xE2\x99\xA5"},
    {"hellip", "\xE2\x80\xA6"},
    {"hercon", "\xE2\x8A\xB9"},
    {"hfr", "\xF0\x9D\x94\xA5"},
    {"hksearow", "\xE2\xA4\xA5"},
    {"hkswarow", "\xE2\xA4\xA6"},
    {"hoarr", "\xE2\x87\xBF"},
    {"homtht", "\xE2\x88\xBB"},
    {"hookleftarrow", "\xE2\x86\xA9"},
    {"hookrightarrow", "\xE2\x86\xAA"},
    {"hopf", "\xF0\x9D\x95\x99"},
    {"horbar", "\xE2\x80\x95"},
    {"hscr", "\xF0\x9D\x92\xBD"},
    {"hslash", "\xE2\x84\x8F"},
    {"hstrok", "\xC4\xA7"},
    {"hybull", "\xE2\x81\x83"},
    {"hyphen", "\xE2\x80\x90"},
    {"iacute", "\xC3\xAD"},
    {"ic", "\xE2\x81\xA3"},
    {"icirc", "\xC3\xAE"},
    {"icy", "\xD0\xB8"},
    {"iecy", "\xD0\xB5"},
    {"iexcl", "\xC2\xA1"},
    {"iff", "\xE2\x87\x94"},
    {"ifr", "\xF0\x9D\x94\xA6"},
    {"igrave", "\xC3\xAC"},
    {"ii", "\xE2\x85\x88"},
    {"iiiint", "\xE2\xA8\x8C"},
    {"iiint", "\xE2\x88\xAD"},
    {"iinfin", "\xE2\xA7\x9C"},
    {"iiota", "\xE2\x84\xA9"},
    {"ijlig", "\xC4\xB3"},
    {"imacr", "\xC4\xAB"},
    {"image", "\xE2\x84\x91"},
    {"imagline", "\xE2\x84\x90"},
    {"imagpart", "\xE2\x84\x91"},
    {"imath", "\xC4\xB1"},
    {"imof", "\xE2\x8A\xB7"},
    {"imped", "\xC6\xB5"},
    {"in", "\xE2\x88\x88"},
    {"incare", "\xE2\x84\x85"},
    {"infin", "\xE2\x88\x9E"},
    {"infintie", "\xE2\xA7\x9D"},
    {"inodot", "\xC4\xB1"},
    {"int", "\xE2\x88\xAB"},
    {"intcal", "\xE2\x8A\xBA"},
    {"integers", "\xE2\x84\xA4"},
    {"intercal", "\xE2\x8A\xBA"},
    {"intlarhk", "\xE2\xA8\x97"},
    {"intprod", "\xE2\xA8\xBC"},
    {"iocy", "\xD1\x91"},
    {"iogon", "\xC4\xAF"},
    {"iopf", "\xF0\x9D\x95\x9A"},
    {"iota", "\xCE\xB9"},
    {"iprod", "\xE2\xA8\xBC"},
    {"iquest", "\xC2\xBF"},
    {"iscr", "\xF0\x9D\x92\xBE"},
    {"isin", "\xE2\x88\x88"},
    {"isinE", "\xE2\x8B\xB9"},
    {"isindot", "\xE2\x8B\xB5"},
    {"isins", "\xE2\x8B\xB4"},
    {"isinsv", "\xE2\x8B\xB3"},
    {"isinv", "\xE2\x88\x88"},
    {"it", "\xE2\x81\xA2"},
    {"itilde", "\xC4\xA9"},
    {"iukcy", "\xD1\x96"},
    {"iuml", "\xC3\xAF"},
    {"jcirc", "\xC4\xB5"},
    {"jcy", "\xD0\xB9"},
    {"jfr", "\xF0\x9D\x94\xA7"},
    {"jmath", "\xC8\xB7"},
    {"jopf", "\xF0\x9D\x95\x9B"},
    {"jscr", "\xF0\x9D\x92\xBF"},
    {"jsercy", "\xD1\x98"},
    {"jukcy", "\xD1\x94"},
    {"kappa", "\xCE\xBA"},
    {"kappav", "\xCF\xB0"},
    {"kcedil", "\xC4\xB7"},
    {"kcy", "\xD0\xBA"},
    {"kfr", "\xF0\x9D\x94\xA8"},
    {"kgreen", "\xC4\xB8"},
    {"khcy", "\xD1\x85"},
    {"kjcy", "\xD1\x9C"},
    {"kopf", "\xF0\x9D\x95\x9C"},
    {"kscr", "\xF0\x9D\x93\x80"},
    {"lAarr", "\xE2\x87\x9A"},
    {"lArr", "\xE2\x87\x90"},
    {"lAtail", "\xE2\xA4\x9B"},
    {"lBarr", "\xE2\xA4\x8E"},
    {"lE", "\xE2\x89\xA6"},
    {"lEg", "\xE2\xAA\x8B"},
    {"lHar", "\xE2\xA5\xA2"},
    {"lacute", "\xC4\xBA"},
    {"laemptyv", "\xE2\xA6\xB4"},
    {"lagran", "\xE2\x84\x92"},
    {"lambda", "\xCE\xBB"},
    {"lang", "\xE2\x9F\xA8"},
    {"langd", "\xE2\xA6\x91"},
    {"langle", "\xE2\x9F\xA8"},
    {"lap", "\xE2\xAA\x85"},
    {"laquo", "\xC2\xAB"},
    {"larr", "\xE2\x86\x90"},
    {"larrb", "\xE2\x87\xA4"},
    {"larrbfs", "\xE2\xA4\x9F"},
    {"larrfs", "\xE2\xA4\x9D"},
    {"larrhk", "\xE2\x86\xA9"},
    {"larrlp", "\xE2\x86\xAB"},
    {"larrpl", "\xE2\xA4\xB9"},
    {"larrsim", "\xE2\xA5\xB3"},
    {"larrtl", "\xE2\x86\xA2"},
    {"lat", "\xE2\xAA\xAB"},
    {"latail", "\xE2\xA4\x99"},
    {"late", "\xE2\xAA\xAD"},
    {"lates", "\xE2\xAA\xAD\xEF\xB8\x80"},
    {"lbarr", "\xE2\xA4\x8C"},
    {"lbbrk", "\xE2\x9D\xB2"},
    {"lbrace", "{"},
    {"lbrack", "["},
    {"lbrke", "\xE2\xA6\x8B"},
    {"lbrksld", "\xE2\xA6\x8F"},
    {"lbrkslu", "\xE2\xA6\x8D"},
    {"lcaron", "\xC4\xBE"},
    {"lcedil", "\xC4\xBC"},
    {"lceil", "\xE2\x8C\x88"},
    {"lcub", "{"},
    {"lcy", "\xD0\xBB"},
    {"ldca", "\xE2\xA4\xB6"},
    {"ldquo", "\xE2\x80\x9C"},
    {"ldquor", "\xE2\x80\x9E"},
    {"ldrdhar", "\xE2\xA5\xA7"},
    {"ldrushar", "\xE2\xA5\x8B"},
    {"ldsh", "\xE2\x86\xB2"},
    {"le", "\xE2\x89\xA4"},
    {"leftarrow", "\xE2\x86\x90"},
    {"leftarrowtail", "\xE2\x86\xA2"},
    {"leftharpoondown", "\xE2\x86\xBD"},
    {"leftharpoonup", "\xE2\x86\xBC"},
    {"leftleftarrows", "\xE2\x87\x87"},
    {"leftrightarrow", "\xE2\x86\x94"},
    {"leftrightarrows", "\xE2\x87\x86"},
    {"leftrightharpoons", "\xE2\x87\x8B"},
    {"leftrightsquigarrow", "\xE2\x86\xAD"},
    {"leftthreetimes", "\xE2\x8B\x8B"},
    {"leg", "\xE2\x8B\x9A"},
    {"leq", "\xE2\x89\xA4"},
    {"leqq", "\xE2\x89\xA6"},
    {"leqslant", "\xE2\xA9\xBD"},
    {"les", "\xE2\xA9\xBD"},
    {"lescc", "\xE2\xAA\xA8"},
    {"lesdot", "\xE2\xA9\xBF"},
    {"lesdoto", "\xE2\xAA\x81"},
    {"lesdotor", "\xE2\xAA\x83"},
    {"lesg", "\xE2\x8B\x9A\xEF\xB8\x80"},
    {"lesges", "\xE2\xAA\x93"},
    {"lessapprox", "\xE2\xAA\x85"},
    {"lessdot", "\xE2\x8B\x96"},
    {"lesseqgtr", "\xE2\x8B\x9A"},
    {"lesseqqgtr", "\xE2\xAA\x8B"},
    {"lessgtr", "\xE2\x89\xB6"},
    {"lesssim", "\xE2\x89\xB2"},
    {"lfisht", "\xE2\xA5\xBC"},
    {"lfloor", "\xE2\x8C\x8A"},
    {"lfr", "\xF0\x9D\x94\xA9"},
    {"lg", "\xE2\x89\xB6"},
    {"lgE", "\xE2\xAA\x91"},
    {"lhard", "\xE2\x86\xBD"},
    {"lharu", "\xE2\x86\xBC"},
    {"lharul", "\xE2\xA5\xAA"},
    {"lhblk", "\xE2\x96\x84"},
    {"ljcy", "\xD1\x99"},
    {"ll", "\xE2\x89\xAA"},
    {"llarr", "\xE2\x87\x87"},
    {"llcorner", "\xE2\x8C\x9E"},
    {"llhard", "\xE2\xA5\xAB"},
    {"lltri", "\xE2\x97\xBA"},
    {"lmidot", "\xC5\x80"},
    {"lmoust", "\xE2\x8E\xB0"},
    {"lmoustache", "\xE2\x8E\xB0"},
    {"lnE", "\xE2\x89\xA8"},
    {"lnap", "\xE2\xAA\x89"},
    {"lnapprox", "\xE2\xAA\x89"},
    {"lne", "\xE2\xAA\x87"},
    {"lneq", "\xE2\xAA\x87"},
    {"lneqq", "\xE2\x89\xA8"},
    {"lnsim", "\xE2\x8B\xA6"},
    {"loang", "\xE2\x9F\xAC"},
    {"loarr", "\xE2\x87\xBD"},
    {"lobrk", "\xE2\x9F\xA6"},
    {"longleftarrow", "\xE2\x9F\xB5"},
    {"longleftrightarrow", "\xE2\x9F\xB7"},
    {"longmapsto", "\xE2\x9F\xBC"},
    {"longrightarrow", "\xE2\x9F\xB6"},
    {"looparrowleft", "\xE2\x86\xAB"},
    {"looparrowright", "\xE2\x86\xAC"},
    {"lopar", "\xE2\xA6\x85"},
    {"lopf", "\xF0\x9D\x95\x9D"},
    {"loplus", "\xE2\xA8\xAD"},
    {"lotimes", "\xE2\xA8\xB4"},
    {"lowast", "\xE2\x88\x97"},
    {"lowbar", "_"},
    {"loz", "\xE2\x97\x8A"},
    {"lozenge", "\xE2\x97\x8A"},
    {"lozf", "\xE2\xA7\xAB"},
    {"lpar", "("},
    {"lparlt", "\xE2\xA6\x93"},
    {"lrarr", "\xE2\x87\x86"},
    {"lrcorner", "\xE2\x8C\x9F"},
    {"lrhar", "\xE2\x87\x8B"},
    {"lrhard", "\xE2\xA5\xAD"},
    {"lrm", "\xE2\x80\x8E"},
    {"lrtri", "\xE2\x8A\xBF"},
    {"lsaquo", "\xE2\x80\xB9"},
    {"lscr", "\xF0\x9D\x93\x81"},
    {"lsh", "\xE2\x86\xB0"},
    {"lsim", "\xE2\x89\xB2"},
    {"lsime", "\xE2\xAA\x8D"},
    {"lsimg", "\xE2\xAA\x8F"},
    {"lsqb", "["},
    {"lsquo", "\xE2\x80\x98"},
    {"lsquor", "\xE2\x80\x9A"},
    {"lstrok", "\xC5\x82"},
    {"lt", "<"},
    {"ltcc", "\xE2\xAA\xA6"},
    {"ltcir", "\xE2\xA9\xB9"},
    {"ltdot", "\xE2\x8B\x96"},
    {"lthree", "\xE2\x8B\x8B"},
    {"ltimes", "\xE2\x8B\x89"},
    {"ltlarr", "\xE2\xA5\xB6"},
    {"ltquest", "\xE2\xA9\xBB"},
    {"ltrPar", "\xE2\xA6\x96"},
    {"ltri", "\xE2\x97\x83"},
    {"ltrie", "\xE2\x8A\xB4"},
    {"ltrif", "\xE2\x97\x82"},
    {"lurdshar", "\xE2\xA5\x8A"},
    {"luruhar", "\xE2\xA5\xA6"},
    {"lvertneqq", "\xE2\x89\xA8\xEF\xB8\x80"},
    {"lvnE", "\xE2\x89\xA8\xEF\xB8\x80"},
    {"mDDot", "\xE2\x88\xBA"},
    {"macr", "\xC2\xAF"},
    {"male", "\xE2\x99\x82"},
    {"malt", "\xE2\x9C\xA0"},
    {"maltese", "\xE2\x9C\xA0"},
    {"map", "\xE2\x86\xA6"},
    {"mapsto", "\xE2\x86\xA6"},
    {"mapstodown", "\xE2\x86\xA7"},
    {"mapstoleft", "\xE2\x86\xA4"},
    {"mapstoup", "\xE2\x86\xA5"},
    {"marker", "\xE2\x96\xAE"},
    {"mcomma", "\xE2\xA8\xA9"},
    {"mcy", "\xD0\xBC"},
    {"mdash", "\xE2\x80\x94"},
    {"measuredangle", "\xE2\x88\xA1"},
    {"mfr", "\xF0\x9D\x94\xAA"},
    {"mho", "\xE2\x84\xA7"},
    {"micro", "\xC2\xB5"},
    {"mid", "\xE2\x88\xA3"},
    {"midast", "*"},
    {"midcir", "\xE2\xAB\xB0"},
    {"middot", "\xC2\xB7"},
    {"minus", "\xE2\x88\x92"},
    {"minusb", "\xE2\x8A\x9F"},
    {"minusd", "\xE2\x88\xB8"},
    {"minusdu", "\xE2\xA8\xAA"},
    {"mlcp", "\xE2\xAB\x9B"},
    {"mldr", "\xE2\x80\xA6"},
    {"mnplus", "\xE2\x88\x93"},
    {"models", "\xE2\x8A\xA7"},
    {"mopf", "\xF0\x9D\x95\x9E"},
    {"mp", "\xE2\x88\x93"},
    {"mscr", "\xF0\x9D\x93\x82"},
    {"mstpos", "\xE2\x88\xBE"},
    {"mu", "\xCE\xBC"},
    {"multimap", "\xE2\x8A\xB8"},
    {"mumap", "\xE2\x8A\xB8"},
    {"nGg", "\xE2\x8B\x99\xCC\xB8"},
    {"nGt", "\xE2\x89\xAB\xE2\x83\x92"},
    {"nGtv", "\xE2\x89\xAB\xCC\xB8"},
    {"nLeftarrow", "\xE2\x87\x8D"},
    {"nLeftrightarrow", "\xE2\x87\x8E"},
    {"nLl", "\xE2\x8B\x98\xCC\xB8"},
    {"nLt", "\xE2\x89\xAA\xE2\x83\x92"},
    {"nLtv", "\xE2\x89\xAA\xCC\xB8"},
    {"nRightarrow", "\xE2\x87\x8F"},
    {"nVDash", "\xE2\x8A\xAF"},
    {"nVdash", "\xE2\x8A\xAE"},
    {"nabla", "\xE2\x88\x87"},
    {"nacute", "\xC5\x84"},
    {"nang", "\xE2\x88\xA0\xE2\x83\x92"},
    {"nap", "\xE2\x89\x89"},
    {"napE", "\xE2\xA9\xB0\xCC\xB8"},
    {"napid", "\xE2\x89\x8B\xCC\xB8"},
    {"napos", "\xC5\x89"},
    {"napprox", "\xE2\x89\x89"},
    {"natur", "\xE2\x99\xAE"},
    {"natural", "\xE2\x99\xAE"},
    {"naturals", "\xE2\x84\x95"},
    {"nbsp", "\xC2\xA0"},
    {"nbump", "\xE2\x89\x8E\xCC\xB8"},
    {"nbumpe", "\xE2\x89\x8F\xCC\xB8"},
    {"ncap", "\xE2\xA9\x83"},
    {"ncaron", "\xC5\x88"},
    {"ncedil", "\xC5\x86"},
    {"ncong", "\xE2\x89\x87"},
    {"ncongdot", "\xE2\xA9\xAD\xCC\xB8"},
    {"ncup", "\xE2\xA9\x82"},
    {"ncy", "\xD0\xBD"},
    {"ndash", "\xE2\x80\x93"},
    {"ne", "\xE2\x89\xA0"},
    {"neArr", "\xE2\x87\x97"},
    {"nearhk", "\xE2\xA4\xA4"},
    {"nearr", "\xE2\x86\x97"},
    {"nearrow", "\xE2\x86\x97"},
    {"nedot", "\xE2\x89\x90\xCC\xB8"},
    {"nequiv", "\xE2\x89\xA2"},
    {"nesear", "\xE2\xA4\xA8"},
    {"nesim", "\xE2\x89\x82\xCC\xB8"},
    {"nexist", "\xE2\x88\x84"},
    {"nexists", "\xE2\x88\x84"},
    {"nfr", "\xF0\x9D\x94\xAB"},
    {"ngE", "\xE2\x89\xA7\xCC\xB8"},
    {"nge", "\xE2\x89\xB1"},
    {"ngeq", "\xE2\x89\xB1"},
    {"ngeqq", "\xE2\x89\xA7\xCC\xB8"},
    {"ngeqslant", "\xE2\xA9\xBE\xCC\xB8"},
    {"nges", "\xE2\xA9\xBE\xCC\xB8"},
    {"ngsim", "\xE2\x89\xB5"},
    {"ngt", "\xE2\x89\xAF"},
    {"ngtr", "\xE2\x89\xAF"},
    {"nhArr", "\xE2\x87\x8E"},
    {"nharr", "\xE2\x86\xAE"},
    {"nhpar", "\xE2\xAB\xB2"},
    {"ni", "\xE2\x88\x8B"},
    {"nis", "\xE2\x8B\xBC"},
    {"nisd", "\xE2\x8B\xBA"},
    {"niv", "\xE2\x88\x8B"},
    {"njcy", "\xD1\x9A"},
    {"nlArr", "\xE2\x87\x8D"},
    {"nlE", "\xE2\x89\xA6\xCC\xB8"},
    {"nlarr", "\xE2\x86\x9A"},
    {"nldr", "\xE2\x80\xA5"},
    {"nle", "\xE2\x89\xB0"},
    {"nleftarrow", "\xE2\x86\x9A"},
    {"nleftrightarrow", "\xE2\x86\xAE"},
    {"nleq", "\xE2\x89\xB0"},
    {"nleqq", "\xE2\x89\xA6\xCC\xB8"},
    {"nleqslant", "\xE2\xA9\xBD\xCC\xB8"},
    {"nles", "\xE2\xA9\xBD\xCC\xB8"},
    {"nless", "\xE2\x89\xAE"},
    {"nlsim", "\xE2\x89\xB4"},
    {"nlt", "\xE2\x89\xAE"},
    {"nltri", "\xE2\x8B\xAA"},
    {"nltrie", "\xE2\x8B\xAC"},
    {"nmid", "\xE2\x88\xA4"},
    {"nopf", "\xF0\x9D\x95\x9F"},
    {"not", "\xC2\xAC"},
    {"notin", "\xE2\x88\x89"},
    {"notinE", "\xE2\x8B\xB9\xCC\xB8"},
    {"notindot", "\xE2\x8B\xB5\xCC\xB8"},
    {"notinva", "\xE2\x88\x89"},
    {"notinvb", "\xE2\x8B\xB7"},
    {"notinvc", "\xE2\x8B\xB6"},
    {"notni", "\xE2\x88\x8C"},
    {"notniva", "\xE2\x88\x8C"},
    {"notnivb", "\xE2\x8B\xBE"},
    {"notnivc", "\xE2\x8B\xBD"},
    {"npar", "\xE2\x88\xA6"},
    {"nparallel", "\xE2\x88\xA6"},
    {"nparsl", "\xE2\xAB\xBD\xE2\x83\xA5"},
    {"npart", "\xE2\x88\x82\xCC\xB8"},
    {"npolint", "\xE2\xA8\x94"},
    {"npr", "\xE2\x8A\x80"},
    {"nprcue", "\xE2\x8B\xA0"},
    {"npre", "\xE2\xAA\xAF\xCC\xB8"},
    {"nprec", "\xE2\x8A\x80"},
    {"npreceq", "\xE2\xAA\xAF\xCC\xB8"},
    {"nrArr", "\xE2\x87\x8F"},
    {"nrarr", "\xE2\x86\x9B"},
    {"nrarrc", "\xE2\xA4\xB3\xCC\xB8"},
    {"nrarrw", "\xE2\x86\x9D\xCC\xB8"},
    {"nrightarrow", "\xE2\x86\x9B"},
    {"nrtri", "\xE2\x8B\xAB"},
    {"nrtrie", "\xE2\x8B\xAD"},
    {"nsc", "\xE2\x8A\x81"},
    {"nsccue", "\xE2\x8B\xA1"},
    {"nsce", "\xE2\xAA\xB0\xCC\xB8"},
    {"nscr", "\xF0\x9D\x93\x83"},
    {"nshortmid", "\xE2\x88\xA4"},
    {"nshortparallel", "\xE2\x88\xA6"},
    {"nsim", "\xE2\x89\x81"},
    {"nsime", "\xE2\x89\x84"},
    {"nsimeq", "\xE2\x89\x84"},
    {"nsmid", "\xE2\x88\xA4"},
    {"nspar", "\xE2\x88\xA6"},
    {"nsqsube", "\xE2\x8B\xA2"},
    {"nsqsupe", "\xE2\x8B\xA3"},
    {"nsub", "\xE2\x8A\x84"},
    {"nsubE", "\xE2\xAB\x85\xCC\xB8"},
    {"nsube", "\xE2\x8A\x88"},
    {"nsubset", "\xE2\x8A\x82\xE2\x83\x92"},
    {"nsubseteq", "\xE2\x8A\x88"},
    {"nsubseteqq", "\xE2\xAB\x85\xCC\xB8"},
    {"nsucc", "\xE2\x8A\x81"},
    {"nsucceq", "\xE2\xAA\xB0\xCC\xB8"},
    {"nsup", "\xE2\x8A\x85"},
    {"nsupE", "\xE2\xAB\x86\xCC\xB8"},
    {"nsupe", "\xE2\x8A\x89"},
    {"nsupset", "\xE2\x8A\x83\xE2\x83\x92"},
    {"nsupseteq", "\xE2\x8A\x89"},
    {"nsupseteqq", "\xE2\xAB\x86\xCC\xB8"},
    {"ntgl", "\xE2\x89\xB9"},
    {"ntilde", "\xC3\xB1"},
    {"ntlg", "\xE2\x89\xB8"},
    {"ntriangleleft", "\xE2\x8B\xAA"},
    {"ntrianglelefteq", "\xE2\x8B\xAC"},
    {"ntriangleright", "\xE2\x8B\xAB"},
    {"ntrianglerighteq", "\xE2\x8B\xAD"},
    {"nu", "\xCE\xBD"},
    {"num", "#"},
    {"numero", "\xE2\x84\x96"},
    {"numsp", "\xE2\x80\x87"},
    {"nvDash", "\xE2\x8A\xAD"},
    {"nvHarr", "\xE2\xA4\x84"},
    {"nvap", "\xE2\x89\x8D\xE2\x83\x92"},
    {"nvdash", "\xE2\x8A\xAC"},
    {"nvge", "\xE2\x89\xA5\xE2\x83\x92"},
    {"nvgt", ">\xE2\x83\x92"},
    {"nvinfin", "\xE2\xA7\x9E"},
    {"nvlArr", "\xE2\xA4\x82"},
    {"nvle", "\xE2\x89\xA4\xE2\x83\x92"},
    {"nvlt", "<\xE2\x83\x92"},
    {"nvltrie", "\xE2\x8A\xB4\xE2\x83\x92"},
    {"nvrArr", "\xE2\xA4\x83"},
    {"nvrtrie", "\xE2\x8A\xB5\xE2\x83\x92"},
    {"nvsim", "\xE2\x88\xBC\xE2\x83\x92"},
    {"nwArr", "\xE2\x87\x96"},
    {"nwarhk", "\xE2\xA4\xA3"},
    {"nwarr", "\xE2\x86\x96"},
    {"nwarrow", "\xE2\x86\x96"},
    {"nwnear", "\xE2\xA4\xA7"},
    {"oS", "\xE2\x93\x88"},
    {"oacute", "\xC3\xB3"},
    {"oast", "\xE2\x8A\x9B"},
    {"ocir", "\xE2\x8A\x9A"},
    {"ocirc", "\xC3\xB4"},
    {"ocy", "\xD0\xBE"},
    {"odash", "\xE2\x8A\x9D"},
    {"odblac", "\xC5\x91"},
    {"odiv", "\xE2\xA8\xB8"},
    {"odot", "\xE2\x8A\x99"},
    {"odsold", "\xE2\xA6\xBC"},
    {"oelig", "\xC5\x93"},
    {"ofcir", "\xE2\xA6\xBF"},
    {"ofr", "\xF0\x9D\x94\xAC"},
    {"ogon", "\xCB\x9B"},
    {"ograve", "\xC3\xB2"},
    {"ogt", "\xE2\xA7\x81"},
    {"ohbar", "\xE2\xA6\xB5"},
    {"ohm", "\xCE\xA9"},
    {"oint", "\xE2\x88\xAE"},
    {"olarr", "\xE2\x86\xBA"},
    {"olcir", "\xE2\xA6\xBE"},
    {"olcross", "\xE2\xA6\xBB"},
    {"oline", "\xE2\x80\xBE"},
    {"olt", "\xE2\xA7\x80"},
    {"omacr", "\xC5\x8D"},
    {"omega", "\xCF\x89"},
    {"omicron", "\xCE\xBF"},
    {"omid", "\xE2\xA6\xB6"},
    {"ominus", "\xE2\x8A\x96"},
    {"oopf", "\xF0\x9D\x95\xA0"},
    {"opar", "\xE2\xA6\xB7"},
    {"operp", "\xE2\xA6\xB9"},
    {"oplus", "\xE2\x8A\x95"},
    {"or", "\xE2\x88\xA8"},
    {"orarr", "\xE2\x86\xBB"},
    {"ord", "\xE2\xA9\x9D"},
    {"order", "\xE2\x84\xB4"},
    {"orderof", "\xE2\x84\xB4"},
    {"ordf", "\xC2\xAA"},
    {"ordm", "\xC2\xBA"},
    {"origof", "\xE2\x8A\xB6"},
    {"oror", "\xE2\xA9\x96"},
    {"orslope", "\xE2\xA9\x97"},
    {"orv", "\xE2\xA9\x9B"},
    {"oscr", "\xE2\x84\xB4"},
    {"oslash", "\xC3\xB8"},
    {"osol", "\xE2\x8A\x98"},
    {"otilde", "\xC3\xB5"},
    {"otimes", "\xE2\x8A\x97"},
    {"otimesas", "\xE2\xA8\xB6"},
    {"ouml", "\xC3\xB6"},
    {"ovbar", "\xE2\x8C\xBD"},
    {"par", "\xE2\x88\xA5"},
    {"para", "\xC2\xB6"},
    {"parallel", "\xE2\x88\xA5"},
    {"parsim", "\xE2\xAB\xB3"},
    {"parsl", "\xE2\xAB\xBD"},
    {"part", "\xE2\x88\x82"},
    {"pcy", "\xD0\xBF"},
    {"percnt", "%"},
    {"period", "."},
    {"permil", "\xE2\x80\xB0"},
    {"perp", "\xE2\x8A\xA5"},
    {"pertenk", "\xE2\x80\xB1"},
    {"pfr", "\xF0\x9D\x94\xAD"},
    {"phi", "\xCF\x86"},
    {"phiv", "\xCF\x95"},
    {"phmmat", "\xE2\x84\xB3"},
    {"phone", "\xE2\x98\x8E"},
    {"pi", "\xCF\x80"},
    {"pitchfork", "\xE2\x8B\x94"},
    {"piv", "\xCF\x96"},
    {"planck", "\xE2\x84\x8F"},
    {"planckh", "\xE2\x84\x8E"},
    {"plankv", "\xE2\x84\x8F"},
    {"plus", "+"},
    {"plusacir", "\xE2\xA8\xA3"},
    {"plusb", "\xE2\x8A\x9E"},
    {"pluscir", "\xE2\xA8\xA2"},
    {"plusdo", "\xE2\x88\x94"},
    {"plusdu", "\xE2\xA8\xA5"},
    {"pluse", "\xE2\xA9\xB2"},
    {"plusmn", "\xC2\xB1"},
    {"plussim", "\xE2\xA8\xA6"},
    {"plustwo", "\xE2\xA8\xA7"},
    {"pm", "\xC2\xB1"},
    {"pointint", "\xE2\xA8\x95"},
    {"popf", "\xF0\x9D\x95\xA1"},
    {"pound", "\xC2\xA3"},
    {"pr", "\xE2\x89\xBA"},
    {"prE", "\xE2\xAA\xB3"},
    {"prap", "\xE2\xAA\xB7"},
    {"prcue", "\xE2\x89\xBC"},
    {"pre", "\xE2\xAA\xAF"},
    {"prec", "\xE2\x89\xBA"},
    {"precapprox", "\xE2\xAA\xB7"},
    {"preccurlyeq", "\xE2\x89\xBC"},
    {"preceq", "\xE2\xAA\xAF"},
    {"precnapprox", "\xE2\xAA\xB9"},
    {"precneqq", "\xE2\xAA\xB5"},
    {"precnsim", "\xE2\x8B\xA8"},
    {"precsim", "\xE2\x89\xBE"},
    {"prime", "\xE2\x80\xB2"},
    {"primes", "\xE2\x84\x99"},
    {"prnE", "\xE2\xAA\xB5"},
    {"prnap", "\xE2\xAA\xB9"},
    {"prnsim", "\xE2\x8B\xA8"},
    {"prod", "\xE2\x88\x8F"},
    {"profalar", "\xE2\x8C\xAE"},
    {"profline", "\xE2\x8C\x92"},
    {"profsurf", "\xE2\x8C\x93"},
    {"prop", "\xE2\x88\x9D"},
    {"propto", "\xE2\x88\x9D"},
    {"prsim", "\xE2\x89\xBE"},
    {"prurel", "\xE2\x8A\xB0"},
    {"pscr", "\xF0\x9D\x93\x85"},
    {"psi", "\xCF\x88"},
    {"puncsp", "\xE2\x80\x88"},
    {"qfr", "\xF0\x9D\x94\xAE"},
    {"qint", "\xE2\xA8\x8C"},
    {"qopf", "\xF0\x9D\x95\xA2"},
    {"qprime", "\xE2\x81\x97"},
    {"qscr", "\xF0\x9D\x93\x86"},
    {"quaternions", "\xE2\x84\x8D"},
    {"quatint", "\xE2\xA8\x96"},
    {"quest", "?"},
    {"questeq", "\xE2\x89\x9F"},
    {"quot", "\x22"},
    {"rAarr", "\xE2\x87\x9B"},
    {"rArr", "\xE2\x87\x92"},
    {"rAtail", "\xE2\xA4\x9C"},
    {"rBarr", "\xE2\xA4\x8F"},
    {"rHar", "\xE2\xA5\xA4"},
    {"race", "\xE2\x88\xBD\xCC\xB1"},
    {"racute", "\xC5\x95"},
    {"radic", "\xE2\x88\x9A"},
    {"raemptyv", "\xE2\xA6\xB3"},
    {"rang", "\xE2\x9F\xA9"},
    {"rangd", "\xE2\xA6\x92"},
    {"range", "\xE2\xA6\xA5"},
    {"rangle", "\xE2\x9F\xA9"},
    {"raquo", "\xC2\xBB"},
    {"rarr", "\xE2\x86\x92"},
    {"rarrap", "\xE2\xA5\xB5"},
    {"rarrb", "\xE2\x87\xA5"},
    {"rarrbfs", "\xE2\xA4\xA0"},
    {"rarrc", "\xE2\xA4\xB3"},
    {"rarrfs", "\xE2\xA4\x9E"},
    {"rarrhk", "\xE2\x86\xAA"},
    {"rarrlp", "\xE2\x86\xAC"},
    {"rarrpl", "\xE2\xA5\x85"},
    {"rarrsim", "\xE2\xA5\xB4"},
    {"rarrtl", "\xE2\x86\xA3"},
    {"rarrw", "\xE2\x86\x9D"},
    {"ratail", "\xE2\xA4\x9A"},
    {"ratio", "\xE2\x88\xB6"},
    {"rationals", "\xE2\x84\x9A"},
    {"rbarr", "\xE2\xA4\x8D"},
    {"rbbrk", "\xE2\x9D\xB3"},
    {"rbrace", "}"},
    {"rbrack", "]"},
    {"rbrke", "\xE2\xA6\x8C"},
    {"rbrksld", "\xE2\xA6\x8E"},
    {"rbrkslu", "\xE2\xA6\x90"},
    {"rcaron", "\xC5\x99"},
    {"rcedil", "\xC5\x97"},
    {"rceil", "\xE2\x8C\x89"},
    {"rcub", "}"},
    {"rcy", "\xD1\x80"},
    {"rdca", "\xE2\xA4\xB7"},
    {"rdldhar", "\xE2\xA5\xA9"},
    {"rdquo", "\xE2\x80\x9D"},
    {"rdquor", "\xE2\x80\x9D"},
    {"rdsh", "\xE2\x86\xB3"},
    {"real", "\xE2\x84\x9C"},
    {"realine", "\xE2\x84\x9B"},
    {"realpart", "\xE2\x84\x9C"},
    {"reals", "\xE2\x84\x9D"},
    {"rect", "\xE2\x96\xAD"},
    {"reg", "\xC2\xAE"},
    {"rfisht", "\xE2\xA5\xBD"},
    {"rfloor", "\xE2\x8C\x8B"},
    {"rfr", "\xF0\x9D\x94\xAF"},
    {"rhard", "\xE2\x87\x81"},
    {"rharu", "\xE2\x87\x80"},
    {"rharul", "\xE2\xA5\xAC"},
    {"rho", "\xCF\x81"},
    {"rhov", "\xCF\xB1"},
    {"rightarrow", "\xE2\x86\x92"},
    {"rightarrowtail", "\xE2\x86\xA3"},
    {"rightharpoondown", "\xE2\x87\x81"},
    {"rightharpoonup", "\xE2\x87\x80"},
    {"rightleftarrows", "\xE2\x87\x84"},
    {"rightleftharpoons", "\xE2\x87\x8C"},
    {"rightrightarrows", "\xE2\x87\x89"},
    {"rightsquigarrow", "\xE2\x86\x9D"},
    {"rightthreetimes", "\xE2\x8B\x8C"},
    {"ring", "\xCB\x9A"},
    {"risingdotseq", "\xE2\x89\x93"},
    {"rlarr", "\xE2\x87\x84"},
    {"rlhar", "\xE2\x87\x8C"},
    {"rlm", "\xE2\x80\x8F"},
    {"rmoust", "\xE2\x8E\xB1"},
    {"rmoustache", "\xE2\x8E\xB1"},
    {"rnmid", "\xE2\xAB\xAE"},
    {"roang", "\xE2\x9F\xAD"},
    {"roarr", "\xE2\x87\xBE"},
    {"robrk", "\xE2\x9F\xA7"},
    {"ropar", "\xE2\xA6\x86"},
    {"ropf", "\xF0\x9D\x95\xA3"},
    {"roplus", "\xE2\xA8\xAE"},
    {"rotimes", "\xE2\xA8\xB5"},
    {"rpar", ")"},
    {"rpargt", "\xE2\xA6\x94"},
    {"rppolint", "\xE2\xA8\x92"},
    {"rrarr", "\xE2\x87\x89"},
    {"rsaquo", "\xE2\x80\xBA"},
    {"rscr", "\xF0\x9D\x93\x87"},
    {"rsh", "\xE2\x86\xB1"},
    {"rsqb", "]"},
    {"rsquo", "\xE2\x80\x99"},
    {"rsquor", "\xE2\x80\x99"},
    {"rthree", "\xE2\x8B\x8C"},
    {"rtimes", "\xE2\x8B\x8A"},
    {"rtri", "\xE2\x96\xB9"},
    {"rtrie", "\xE2\x8A\xB5"},
    {"rtrif", "\xE2\x96\xB8"},
    {"rtriltri", "\xE2\xA7\x8E"},
    {"ruluhar", "\xE2\xA5\xA8"},
    {"rx", "\xE2\x84\x9E"},
    {"sacute", "\xC5\x9B"},
    {"sbquo", "\xE2\x80\x9A"},
    {"sc", "\xE2\x89\xBB"},
    {"scE", "\xE2\xAA\xB4"},
    {"scap", "\xE2\xAA\xB8"},
    {"scaron", "\xC5\xA1"},
    {"sccue", "\xE2\x89\xBD"},
    {"sce", "\xE2\xAA\xB0"},
    {"scedil", "\xC5\x9F"},
    {"scirc", "\xC5\x9D"},
    {"scnE", "\xE2\xAA\xB6"},
    {"scnap", "\xE2\xAA\xBA"},
    {"scnsim", "\xE2\x8B\xA9"},
    {"scpolint", "\xE2\xA8\x93"},
    {"scsim", "\xE2\x89\xBF"},
    {"scy", "\xD1\x81"},
    {"sdot", "\xE2\x8B\x85"},
    {"sdotb", "\xE2\x8A\xA1"},
    {"sdote", "\xE2\xA9\xA6"},
    {"seArr", "\xE2\x87\x98"},
    {"searhk", "\xE2\xA4\xA5"},
    {"searr", "\xE2\x86\x98"},
    {"searrow", "\xE2\x86\x98"},
    {"sect", "\xC2\xA7"},
    {"semi", ";"},
    {"seswar", "\xE2\xA4\xA9"},
    {"setminus", "\xE2\x88\x96"},
    {"setmn", "\xE2\x88\x96"},
    {"sext", "\xE2\x9C\xB6"},
    {"sfr", "\xF0\x9D\x94\xB0"},
    {"sfrown", "\xE2\x8C\xA2"},
    {"sharp", "\xE2\x99\xAF"},
    {"shchcy", "\xD1\x89"},
    {"shcy", "\xD1\x88"},
    {"shortmid", "\xE2\x88\xA3"},
    {"shortparallel", "\xE2\x88\xA5"},
    {"shy", "\xC2\xAD"},
    {"sigma", "\xCF\x83"},
    {"sigmaf", "\xCF\x82"},
    {"sigmav", "\xCF\x82"},
    {"sim", "\xE2\x88\xBC"},
    {"simdot", "\xE2\xA9\xAA"},
    {"sime", "\xE2\x89\x83"},
    {"simeq", "\xE2\x89\x83"},
    {"simg", "\xE2\xAA\x9E"},
    {"simgE", "\xE2\xAA\xA0"},
    {"siml", "\xE2\xAA\x9D"},
    {"simlE", "\xE2\xAA\x9F"},
    {"simne", "\xE2\x89\x86"},
    {"simplus", "\xE2\xA8\xA4"},
    {"simrarr", "\xE2\xA5\xB2"},
    {"slarr", "\xE2\x86\x90"},
    {"smallsetminus", "\xE2\x88\x96"},
    {"smashp", "\xE2\xA8\xB3"},
    {"smeparsl", "\xE2\xA7\xA4"},
    {"smid", "\xE2\x88\xA3"},
    {"smile", "\xE2\x8C\xA3"},
    {"smt", "\xE2\xAA\xAA"},
    {"smte", "\xE2\xAA\xAC"},
    {"smtes", "\xE2\xAA\xAC\xEF\xB8\x80"},
    {"softcy", "\xD1\x8C"},
    {"sol", "/"},
    {"solb", "\xE2\xA7\x84"},
    {"solbar", "\xE2\x8C\xBF"},
    {"sopf", "\xF0\x9D\x95\xA4"},
    {"spades", "\xE2\x99\xA0"},
    {"spadesuit", "\xE2\x99\xA0"},
    {"spar", "\xE2\x88\xA5"},
    {"sqcap", "\xE2\x8A\x93"},
    {"sqcaps", "\xE2\x8A\x93\xEF\xB8\x80"},
    {"sqcup", "\xE2\x8A\x94"},
    {"sqcups", "\xE2\x8A\x94\xEF\xB8\x80"},
    {"sqsub", "\xE2\x8A\x8F"},
    {"sqsube", "\xE2\x8A\x91"},
    {"sqsubset", "\xE2\x8A\x8F"},
    {"sqsubseteq", "\xE2\x8A\x91"},
    {"sqsup", "\xE2\x8A\x90"},
    {"sqsupe", "\xE2\x8A\x92"},
    {"sqsupset", "\xE2\x8A\x90"},
    {"sqsupseteq", "\xE2\x8A\x92"},
    {"squ", "\xE2\x96\xA1"},
    {"square", "\xE2\x96\xA1"},
    {"squarf", "\xE2\x96\xAA"},
    {"squf", "\xE2\x96\xAA"},
    {"srarr", "\xE2\x86\x92"},
    {"sscr", "\xF0\x9D\x93\x88"},
    {"ssetmn", "\xE2\x88\x96"},
    {"ssmile", "\xE2\x8C\xA3"},
    {"sstarf", "\xE2\x8B\x86"},
    {"star", "\xE2\x98\x86"},
    {"starf", "\xE2\x98\x85"},
    {"straightepsilon", "\xCF\xB5"},
    {"straightphi", "\xCF\x95"},
    {"strns", "\xC2\xAF"},
    {"sub", "\xE2\x8A\x82"},
    {"subE", "\xE2\xAB\x85"},
    {"subdot", "\xE2\xAA\xBD"},
    {"sube", "\xE2\x8A\x86"},
    {"subedot", "\xE2\xAB\x83"},
    {"submult", "\xE2\xAB\x81"},
    {"subnE", "\xE2\xAB\x8B"},
    {"subne", "\xE2\x8A\x8A"},
    {"subplus", "\xE2\xAA\xBF"},
    {"subrarr", "\xE2\xA5\xB9"},
    {"subset", "\xE2\x8A\x82"},
    {"subseteq", "\xE2\x8A\x86"},
    {"subseteqq", "\xE2\xAB\x85"},
    {"subsetneq", "\xE2\x8A\x8A"},
    {"subsetneqq", "\xE2\xAB\x8B"},
    {"subsim", "\xE2\xAB\x87"},
    {"subsub", "\xE2\xAB\x95"},
    {"subsup", "\xE2\xAB\x93"},
    {"succ", "\xE2\x89\xBB"},
    {"succapprox", "\xE2\xAA\xB8"},
    {"succcurlyeq", "\xE2\x89\xBD"},
    {"succeq", "\xE2\xAA\xB0"},
    {"succnapprox", "\xE2\xAA\xBA"},
    {"succneqq", "\xE2\xAA\xB6"},
    {"succnsim", "\xE2\x8B\xA9"},
    {"succsim", "\xE2\x89\xBF"},
    {"sum", "\xE2\x88\x91"},
    {"sung", "\xE2\x99\xAA"},
    {"sup", "\xE2\x8A\x83"},
    {"sup1", "\xC2\xB9"},
    {"sup2", "\xC2\xB2"},
    {"sup3", "\xC2\xB3"},
    {"supE", "\xE2\xAB\x86"},
    {"supdot", "\xE2\xAA\xBE"},
    {"supdsub", "\xE2\xAB\x98"},
    {"supe", "\xE2\x8A\x87"},
    {"supedot", "\xE2\xAB\x84"},
    {"suphsol", "\xE2\x9F\x89"},
    {"suphsub", "\xE2\xAB\x97"},
    {"suplarr", "\xE2\xA5\xBB"},
    {"supmult", "\xE2\xAB\x82"},
    {"supnE", "\xE2\xAB\x8C"},
    {"supne", "\xE2\x8A\x8B"},
    {"supplus", "\xE2\xAB\x80"},
    {"supset", "\xE2\x8A\x83"},
    {"supseteq", "\xE2\x8A\x87"},
    {"supseteqq", "\xE2\xAB\x86"},
    {"supsetneq", "\xE2\x8A\x8B"},
    {"supsetneqq", "\xE2\xAB\x8C"},
    {"supsim", "\xE2\xAB\x88"},
    {"supsub", "\xE2\xAB\x94"},
    {"supsup", "\xE2\xAB\x96"},
    {"swArr", "\xE2\x87\x99"},
    {"swarhk", "\xE2\xA4\xA6"},
    {"swarr", "\xE2\x86\x99"},
    {"swarrow", "\xE2\x86\x99"},
    {"swnwar", "\xE2\xA4\xAA"},
    {"szlig", "\xC3\x9F"},
    {"target", "\xE2\x8C\x96"},
    {"tau", "\xCF\x84"},
    {"tbrk", "\xE2\x8E\xB4"},
    {"tcaron", "\xC5\xA5"},
    {"tcedil", "\xC5\xA3"},
    {"tcy", "\xD1\x82"},
    {"tdot", "\xE2\x83\x9B"},
    {"telrec", "\xE2\x8C\x95"},
    {"tfr", "\xF0\x9D\x94\xB1"},
    {"there4", "\xE2\x88\xB4"},
    {"therefore", "\xE2\x88\xB4"},
    {"theta", "\xCE\xB8"},
    {"thetasym", "\xCF\x91"},
    {"thetav", "\xCF\x91"},
    {"thickapprox", "\xE2\x89\x88"},
    {"thicksim", "\xE2\x88\xBC"},
    {"thinsp", "\xE2\x80\x89"},
    {"thkap", "\xE2\x89\x88"},
    {"thksim", "\xE2\x88\xBC"},
    {"thorn", "\xC3\xBE"},
    {"tilde", "\xCB\x9C"},
    {"times", "\xC3\x97"},
    {"timesb", "\xE2\x8A\xA0"},
    {"timesbar", "\xE2\xA8\xB1"},
    {"timesd", "\xE2\xA8\xB0"},
    {"tint", "\xE2\x88\xAD"},
    {"toea", "\xE2\xA4\xA8"},
    {"top", "\xE2\x8A\xA4"},
    {"topbot", "\xE2\x8C\xB6"},
    {"topcir", "\xE2\xAB\xB1"},
    {"topf", "\xF0\x9D\x95\xA5"},
    {"topfork", "\xE2\xAB\x9A"},
    {"tosa", "\xE2\xA4\xA9"},
    {"tprime", "\xE2\x80\xB4"},
    {"trade", "\xE2\x84\xA2"},
    {"triangle", "\xE2\x96\xB5"},
    {"triangledown", "\xE2\x96\xBF"},
    {"triangleleft", "\xE2\x97\x83"},
    {"trianglelefteq", "\xE2\x8A\xB4"},
    {"triangleq", "\xE2\x89\x9C"},
    {"triangleright", "\xE2\x96\xB9"},
    {"trianglerighteq", "\xE2\x8A\xB5"},
    {"tridot", "\xE2\x97\xAC"},
    {"trie", "\xE2\x89\x9C"},
    {"triminus", "\xE2\xA8\xBA"},
    {"triplus", "\xE2\xA8\xB9"},
    {"trisb", "\xE2\xA7\x8D"},
    {"tritime", "\xE2\xA8\xBB"},
    {"trpezium", "\xE2\x8F\xA2"},
    {"tscr", "\xF0\x9D\x93\x89"},
    {"tscy", "\xD1\x86"},
    {"tshcy", "\xD1\x9B"},
    {"tstrok", "\xC5\xA7"},
    {"twixt", "\xE2\x89\xAC"},
    {"twoheadleftarrow", "\xE2\x86\x9E"},
    {"twoheadrightarrow", "\xE2\x86\xA0"},
    {"uArr", "\xE2\x87\x91"},
    {"uHar", "\xE2\xA5\xA3"},
    {"uacute", "\xC3\xBA"},
    {"uarr", "\xE2\x86\x91"},
    {"ubrcy", "\xD1\x9E"},
    {"ubreve", "\xC5\xAD"},
    {"ucirc", "\xC3\xBB"},
    {"ucy", "\xD1\x83"},
    {"udarr", "\xE2\x87\x85"},
    {"udblac", "\xC5\xB1"},
    {"udhar", "\xE2\xA5\xAE"},
    {"ufisht", "\xE2\xA5\xBE"},
    {"ufr", "\xF0\x9D\x94\xB2"},
    {"ugrave", "\xC3\xB9"},
    {"uharl", "\xE2\x86\xBF"},
    {"uharr", "\xE2\x86\xBE"},
    {"uhblk", "\xE2\x96\x80"},
    {"ulcorn", "\xE2\x8C\x9C"},
    {"ulcorner", "\xE2\x8C\x9C"},
    {"ulcrop", "\xE2\x8C\x8F"},
    {"ultri", "\xE2\x97\xB8"},
    {"umacr", "\xC5\xAB"},
    {"uml", "\xC2\xA8"},
    {"uogon", "\xC5\xB3"},
    {"uopf", "\xF0\x9D\x95\xA6"},
    {"uparrow", "\xE2\x86\x91"},
    {"updownarrow", "\xE2\x86\x95"},
    {"upharpoonleft", "\xE2\x86\xBF"},
    {"upharpoonright", "\xE2\x86\xBE"},
    {"uplus", "\xE2\x8A\x8E"},
    {"upsi", "\xCF\x85"},
    {"upsih", "\xCF\x92"},
    {"upsilon", "\xCF\x85"},
    {"upuparrows", "\xE2\x87\x88"},
    {"urcorn", "\xE2\x8C\x9D"},
    {"urcorner", "\xE2\x8C\x9D"},
    {"urcrop", "\xE2\x8C\x8E"},
    {"uring", "\xC5\xAF"},
    {"urtri", "\xE2\x97\xB9"},
    {"uscr", "\xF0\x9D\x93\x8A"},
    {"utdot", "\xE2\x8B\xB0"},
    {"utilde", "\xC5\xA9"},
    {"utri", "\xE2\x96\xB5"},
    {"utrif", "\xE2\x96\xB4"},
    {"uuarr", "\xE2\x87\x88"},
    {"uuml", "\xC3\xBC"},
    {"uwangle", "\xE2\xA6\xA7"},
    {"vArr", "\xE2\x87\x95"},
    {"vBar", "\xE2\xAB\xA8"},
    {"vBarv", "\xE2\xAB\xA9"},
    {"vDash", "\xE2\x8A\xA8"},
    {"vangrt", "\xE2\xA6\x9C"},
    {"varepsilon", "\xCF\xB5"},
    {"varkappa", "\xCF\xB0"},
    {"varnothing", "\xE2\x88\x85"},
    {"varphi", "\xCF\x95"},
    {"varpi", "\xCF\x96"},
    {"varpropto", "\xE2\x88\x9D"},
    {"varr", "\xE2\x86\x95"},
    {"varrho", "\xCF\xB1"},
    {"varsigma", "\xCF\x82"},
    {"varsubsetneq", "\xE2\x8A\x8A\xEF\xB8\x80"},
    {"varsubsetneqq", "\xE2\xAB\x8B\xEF\xB8\x80"},
    {"varsupsetneq", "\xE2\x8A\x8B\xEF\xB8\x80"},
    {"varsupsetneqq", "\xE2\xAB\x8C\xEF\xB8\x80"},
    {"vartheta", "\xCF\x91"},
    {"vartriangleleft", "\xE2\x8A\xB2"},
    {"vartriangleright", "\xE2\x8A\xB3"},
    {"vcy", "\xD0\xB2"},
    {"vdash", "\xE2\x8A\xA2"},
    {"vee", "\xE2\x88\xA8"},
    {"veebar", "\xE2\x8A\xBB"},
    {"veeeq", "\xE2\x89\x9A"},
    {"vellip", "\xE2\x8B\xAE"},
    {"verbar", "|"},
    {"vert", "|"},
    {"vfr", "\xF0\x9D\x94\xB3"},
    {"vltri", "\xE2\x8A\xB2"},
    {"vnsub", "\xE2\x8A\x82\xE2\x83\x92"},
    {"vnsup", "\xE2\x8A\x83\xE2\x83\x92"},
    {"vopf", "\xF0\x9D\x95\xA7"},
    {"vprop", "\xE2\x88\x9D"},
    {"vrtri", "\xE2\x8A\xB3"},
    {"vscr", "\xF0\x9D\x93\x8B"},
    {"vsubnE", "\xE2\xAB\x8B\xEF\xB8\x80"},
    {"vsubne", "\xE2\x8A\x8A\xEF\xB8\x80"},
    {"vsupnE", "\xE2\xAB\x8C\xEF\xB8\x80"},
    {"vsupne", "\xE2\x8A\x8B\xEF\xB8\x80"},
    {"vzigzag", "\xE2\xA6\x9A"},
    {"wcirc", "\xC5\xB5"},
    {"wedbar", "\xE2\xA9\x9F"},
    {"wedge", "\xE2\x88\xA7"},
    {"wedgeq", "\xE2\x89\x99"},
    {"weierp", "\xE2\x84\x98"},
    {"wfr", "\xF0\x9D\x94\xB4"},
    {"wopf", "\xF0\x9D\x95\xA8"},
    {"wp", "\xE2\x84\x98"},
    {"wr", "\xE2\x89\x80"},
    {"wreath", "\xE2\x89\x80"},
    {"wscr", "\xF0\x9D\x93\x8C"},
    {"xcap", "\xE2\x8B\x82"},
    {"xcirc", "\xE2\x97\xAF"},
    {"xcup", "\xE2\x8B\x83"},
    {"xdtri", "\xE2\x96\xBD"},
    {"xfr", "\xF0\x9D\x94\xB5"},
    {"xhArr", "\xE2\x9F\xBA"},
    {"xharr", "\xE2\x9F\xB7"},
    {"xi", "\xCE\xBE"},
    {"xlArr", "\xE2\x9F\xB8"},
    {"xlarr", "\xE2\x9F\xB5"},
    {"xmap", "\xE2\x9F\xBC"},
    {"xnis", "\xE2\x8B\xBB"},
    {"xodot", "\xE2\xA8\x80"},
    {"xopf", "\xF0\x9D\x95\xA9"},
    {"xoplus", "\xE2\xA8\x81"},
    {"xotime", "\xE2\xA8\x82"},
    {"xrArr", "\xE2\x9F\xB9"},
    {"xrarr", "\xE2\x9F\xB6"},
    {"xscr", "\xF0\x9D\x93\x8D"},
    {"xsqcup", "\xE2\xA8\x86"},
    {"xuplus", "\xE2\xA8\x84"},
    {"xutri", "\xE2\x96\xB3"},
    {"xvee", "\xE2\x8B\x81"},
    {"xwedge", "\xE2\x8B\x80"},
    {"yacute", "\xC3\xBD"},
    {"yacy", "\xD1\x8F"},
    {"ycirc", "\xC5\xB7"},
    {"ycy", "\xD1\x8B"},
    {"yen", "\xC2\xA5"},
    {"yfr", "\xF0\x9D\x94\xB6"},
    {"yicy", "\xD1\x97"},
    {"yopf", "\xF0\x9D\x95\xAA"},
    {"yscr", "\xF0\x9D\x93\x8E"},
    {"yucy", "\xD1\x8E"},
    {"yuml", "\xC3\xBF"},
    {"zacute", "\xC5\xBA"},
    {"zcaron", "\xC5\xBE"},
    {"zcy", "\xD0\xB7"},
    {"zdot", "\xC5\xBC"},
    {"zeetrf", "\xE2\x84\xA8"},
    {"zeta", "\xCE\xB6"},
    {"zfr", "\xF0\x9D\x94\xB7"},
    {"zhcy", "\xD0\xB6"},
    {"zigrarr", "\xE2\x87\x9D"},
    {"zopf", "\xF0\x9D\x95\xAB"},
    {"zscr", "\xF0\x9D\x93\x8F"},
    {"zwj", "\xE2\x80\x8D"},
    {"zwnj", "\xE2\x80\x8C"},
    {nullptr, nullptr}
};

// Entity count: 2125



// Entity count for binary search
static const size_t NAMED_ENTITY_COUNT = 2125;

// Look up named entity using binary search (case-sensitive)
// Table is sorted alphabetically by name
static const char* html5_lookup_named_entity(const char* name, size_t len) {
    size_t low = 0;
    size_t high = NAMED_ENTITY_COUNT;

    while (low < high) {
        size_t mid = low + (high - low) / 2;
        const char* mid_name = named_entities[mid].name;
        size_t mid_len = strlen(mid_name);

        // Compare by length first, then by content
        int cmp;
        size_t min_len = len < mid_len ? len : mid_len;
        cmp = memcmp(name, mid_name, min_len);
        if (cmp == 0) {
            // If equal up to min_len, shorter string comes first
            if (len < mid_len) cmp = -1;
            else if (len > mid_len) cmp = 1;
        }

        if (cmp < 0) {
            high = mid;
        } else if (cmp > 0) {
            low = mid + 1;
        } else {
            return named_entities[mid].replacement;
        }
    }
    return nullptr;
}

// Encode a Unicode codepoint as UTF-8 into buffer (returns number of bytes written)
static int html5_encode_utf8(uint32_t codepoint, char* out) {
    if (codepoint <= 0x7F) {
        out[0] = (char)codepoint;
        return 1;
    } else if (codepoint <= 0x7FF) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    } else if (codepoint <= 0xFFFF) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    } else if (codepoint <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (codepoint >> 18));
        out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    // Invalid codepoint - use replacement character
    out[0] = (char)0xEF;
    out[1] = (char)0xBF;
    out[2] = (char)0xBD;
    return 3;
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

        *out_len = html5_encode_utf8(codepoint, out_chars);
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
        const char* replacement = html5_lookup_named_entity(entity_name, name_len);
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
            const char* replacement = html5_lookup_named_entity(entity_name, try_len);
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
