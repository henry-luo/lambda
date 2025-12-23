#include "html5_tokenizer.h"
#include "html5_token.h"
#include "../../../lib/log.h"
#include "../../mark_builder.hpp"
#include <string.h>
#include <ctype.h>

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
        attr_value = builder.createString("");
    }

    // Add attribute to token
    html5_token_add_attribute(parser->current_token, attr_name, attr_value, parser->input);

    // Clear for next attribute
    html5_clear_attr_name(parser);
    html5_clear_temp_buffer(parser);
}

// helper: check if string matches (case insensitive)
static bool html5_match_string_ci(const char* str1, const char* str2, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (tolower(str1[i]) != tolower(str2[i])) {
            return false;
        }
    }
    return true;
}

// Named character entity table (common entities)
struct NamedEntity {
    const char* name;
    const char* replacement;
};

static const NamedEntity named_entities[] = {
    {"amp", "&"},
    {"lt", "<"},
    {"gt", ">"},
    {"quot", "\""},
    {"apos", "'"},
    {"nbsp", "\xC2\xA0"},  // UTF-8 for non-breaking space
    {"copy", "\xC2\xA9"},  // ©
    {"reg", "\xC2\xAE"},   // ®
    {"deg", "\xC2\xB0"},   // °
    {"plusmn", "\xC2\xB1"}, // ±
    {"times", "\xC3\x97"}, // ×
    {"divide", "\xC3\xB7"}, // ÷
    {"mdash", "\xE2\x80\x94"}, // —
    {"ndash", "\xE2\x80\x93"}, // –
    {"lsquo", "\xE2\x80\x98"}, // '
    {"rsquo", "\xE2\x80\x99"}, // '
    {"ldquo", "\xE2\x80\x9C"}, // "
    {"rdquo", "\xE2\x80\x9D"}, // "
    {"bull", "\xE2\x80\xA2"}, // •
    {"hellip", "\xE2\x80\xA6"}, // …
    {"trade", "\xE2\x84\xA2"}, // ™
    {"larr", "\xE2\x86\x90"}, // ←
    {"rarr", "\xE2\x86\x92"}, // →
    {"uarr", "\xE2\x86\x91"}, // ↑
    {"darr", "\xE2\x86\x93"}, // ↓
    {"euro", "\xE2\x82\xAC"}, // €
    {"pound", "\xC2\xA3"}, // £
    {"yen", "\xC2\xA5"},   // ¥
    {"cent", "\xC2\xA2"},  // ¢
    {"sect", "\xC2\xA7"},  // §
    {"para", "\xC2\xB6"},  // ¶
    {"middot", "\xC2\xB7"}, // ·
    {"frac12", "\xC2\xBD"}, // ½
    {"frac14", "\xC2\xBC"}, // ¼
    {"frac34", "\xC2\xBE"}, // ¾
    {"iexcl", "\xC2\xA1"}, // ¡
    {"iquest", "\xC2\xBF"}, // ¿
    {"laquo", "\xC2\xAB"}, // «
    {"raquo", "\xC2\xBB"}, // »
    {"not", "\xC2\xAC"},   // ¬
    {"shy", "\xC2\xAD"},   // soft hyphen
    {"macr", "\xC2\xAF"},  // ¯
    {"acute", "\xC2\xB4"}, // ´
    {"micro", "\xC2\xB5"}, // µ
    {"cedil", "\xC2\xB8"}, // ¸
    {"Agrave", "\xC3\x80"}, // À
    {"Aacute", "\xC3\x81"}, // Á
    {"Acirc", "\xC3\x82"},  // Â
    {"Atilde", "\xC3\x83"}, // Ã
    {"Auml", "\xC3\x84"},   // Ä
    {"Aring", "\xC3\x85"},  // Å
    {"AElig", "\xC3\x86"},  // Æ
    {"Ccedil", "\xC3\x87"}, // Ç
    {"Egrave", "\xC3\x88"}, // È
    {"Eacute", "\xC3\x89"}, // É
    {"Ecirc", "\xC3\x8A"},  // Ê
    {"Euml", "\xC3\x8B"},   // Ë
    {"Igrave", "\xC3\x8C"}, // Ì
    {"Iacute", "\xC3\x8D"}, // Í
    {"Icirc", "\xC3\x8E"},  // Î
    {"Iuml", "\xC3\x8F"},   // Ï
    {"ETH", "\xC3\x90"},    // Ð
    {"Ntilde", "\xC3\x91"}, // Ñ
    {"Ograve", "\xC3\x92"}, // Ò
    {"Oacute", "\xC3\x93"}, // Ó
    {"Ocirc", "\xC3\x94"},  // Ô
    {"Otilde", "\xC3\x95"}, // Õ
    {"Ouml", "\xC3\x96"},   // Ö
    {"Oslash", "\xC3\x98"}, // Ø
    {"Ugrave", "\xC3\x99"}, // Ù
    {"Uacute", "\xC3\x9A"}, // Ú
    {"Ucirc", "\xC3\x9B"},  // Û
    {"Uuml", "\xC3\x9C"},   // Ü
    {"Yacute", "\xC3\x9D"}, // Ý
    {"THORN", "\xC3\x9E"},  // Þ
    {"szlig", "\xC3\x9F"},  // ß
    {"agrave", "\xC3\xA0"}, // à
    {"aacute", "\xC3\xA1"}, // á
    {"acirc", "\xC3\xA2"},  // â
    {"atilde", "\xC3\xA3"}, // ã
    {"auml", "\xC3\xA4"},   // ä
    {"aring", "\xC3\xA5"},  // å
    {"aelig", "\xC3\xA6"},  // æ
    {"ccedil", "\xC3\xA7"}, // ç
    {"egrave", "\xC3\xA8"}, // è
    {"eacute", "\xC3\xA9"}, // é
    {"ecirc", "\xC3\xAA"},  // ê
    {"euml", "\xC3\xAB"},   // ë
    {"igrave", "\xC3\xAC"}, // ì
    {"iacute", "\xC3\xAD"}, // í
    {"icirc", "\xC3\xAE"},  // î
    {"iuml", "\xC3\xAF"},   // ï
    {"eth", "\xC3\xB0"},    // ð
    {"ntilde", "\xC3\xB1"}, // ñ
    {"ograve", "\xC3\xB2"}, // ò
    {"oacute", "\xC3\xB3"}, // ó
    {"ocirc", "\xC3\xB4"},  // ô
    {"otilde", "\xC3\xB5"}, // õ
    {"ouml", "\xC3\xB6"},   // ö
    {"oslash", "\xC3\xB8"}, // ø
    {"ugrave", "\xC3\xB9"}, // ù
    {"uacute", "\xC3\xBA"}, // ú
    {"ucirc", "\xC3\xBB"},  // û
    {"uuml", "\xC3\xBC"},   // ü
    {"yacute", "\xC3\xBD"}, // ý
    {"thorn", "\xC3\xBE"},  // þ
    {"yuml", "\xC3\xBF"},   // ÿ
    {nullptr, nullptr}
};

// Look up named entity (case-sensitive)
static const char* html5_lookup_named_entity(const char* name, size_t len) {
    for (const NamedEntity* e = named_entities; e->name != nullptr; e++) {
        if (strlen(e->name) == len && memcmp(e->name, name, len) == 0) {
            return e->replacement;
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
static int html5_try_decode_char_reference(Html5Parser* parser, char* out_chars, int* out_len) {
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
        while (true) {
            c = html5_peek_char(parser, 0);
            if (is_hex) {
                if (c >= '0' && c <= '9') {
                    codepoint = codepoint * 16 + (c - '0');
                    digits++;
                    parser->pos++;
                } else if (c >= 'a' && c <= 'f') {
                    codepoint = codepoint * 16 + (c - 'a' + 10);
                    digits++;
                    parser->pos++;
                } else if (c >= 'A' && c <= 'F') {
                    codepoint = codepoint * 16 + (c - 'A' + 10);
                    digits++;
                    parser->pos++;
                } else {
                    break;
                }
            } else {
                if (c >= '0' && c <= '9') {
                    codepoint = codepoint * 10 + (c - '0');
                    digits++;
                    parser->pos++;
                } else {
                    break;
                }
            }
            // Prevent overflow
            if (codepoint > 0x10FFFF) codepoint = 0xFFFD;
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
    
    // Check for optional semicolon
    bool has_semicolon = (html5_peek_char(parser, 0) == ';');
    if (has_semicolon) {
        parser->pos++;  // consume ';'
    }
    
    // Look up entity
    const char* replacement = html5_lookup_named_entity(entity_name, name_len);
    if (replacement != nullptr) {
        size_t rep_len = strlen(replacement);
        memcpy(out_chars, replacement, rep_len);
        *out_len = (int)rep_len;
        return 1;
    }
    
    // Entity not found - restore position and emit '&' as literal
    parser->pos = start_pos;
    return 0;
}

// main tokenizer function - returns next token
Html5Token* html5_tokenize_next(Html5Parser* parser) {
    while (true) {
        char c = html5_consume_next_char(parser);

        switch (parser->tokenizer_state) {
            case HTML5_TOK_DATA: {
                if (c == '&') {
                    // Try to decode character reference
                    char decoded[8];
                    int decoded_len = 0;
                    if (html5_try_decode_char_reference(parser, decoded, &decoded_len)) {
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
                } else {
                    // parse error: invalid first character of tag name
                    log_error("html5: invalid first character of tag name");
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
                    log_error("html5: eof before tag name");
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    return html5_token_create_character(parser->pool, parser->arena, '<');
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
                    // TODO: character reference in attribute value
                    html5_append_to_temp_buffer(parser, c);
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
                    // TODO: character reference in attribute value
                    html5_append_to_temp_buffer(parser, c);
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
                    // TODO: character reference in attribute value
                    html5_append_to_temp_buffer(parser, c);
                } else if (c == '>') {
                    // commit and emit tag
                    html5_commit_attribute(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
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
                    html5_reconsume(parser);
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
                } else if (html5_is_eof(parser)) {
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
                    // check for PUBLIC or SYSTEM keywords - for now, treat as bogus
                    log_error("html5: invalid character after doctype name");
                    parser->current_token->force_quirks = true;
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
