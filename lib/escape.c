#include "escape.h"
#include "utf.h"
#include "str.h"

#include <stdio.h>

const EscapeRule ESCAPE_RULES_JSON[] = {
    {'"', "\\\""},
    {'\\', "\\\\"},
    {'\b', "\\b"},
    {'\f', "\\f"},
    {'\n', "\\n"},
    {'\r', "\\r"},
    {'\t', "\\t"}
};
const int ESCAPE_RULES_JSON_COUNT = (int)(sizeof(ESCAPE_RULES_JSON) / sizeof(ESCAPE_RULES_JSON[0]));

const EscapeRule ESCAPE_RULES_HTML_TEXT[] = {
    {'&', "&amp;"},
    {'<', "&lt;"},
    {'>', "&gt;"}
};
const int ESCAPE_RULES_HTML_TEXT_COUNT = (int)(sizeof(ESCAPE_RULES_HTML_TEXT) / sizeof(ESCAPE_RULES_HTML_TEXT[0]));

const EscapeRule ESCAPE_RULES_HTML_ATTR[] = {
    {'&', "&amp;"},
    {'<', "&lt;"},
    {'>', "&gt;"},
    {'"', "&quot;"},
    {'\'', "&#39;"}
};
const int ESCAPE_RULES_HTML_ATTR_COUNT = (int)(sizeof(ESCAPE_RULES_HTML_ATTR) / sizeof(ESCAPE_RULES_HTML_ATTR[0]));

const EscapeRule ESCAPE_RULES_XML_ATTR[] = {
    {'&', "&amp;"},
    {'<', "&lt;"},
    {'>', "&gt;"},
    {'"', "&quot;"},
    {'\'', "&apos;"}
};
const int ESCAPE_RULES_XML_ATTR_COUNT = (int)(sizeof(ESCAPE_RULES_XML_ATTR) / sizeof(ESCAPE_RULES_XML_ATTR[0]));

const EscapeRule ESCAPE_RULES_LATEX[] = {
    {'\\', "\\textbackslash{}"},
    {'{', "\\{"},
    {'}', "\\}"},
    {'$', "\\$"},
    {'&', "\\&"},
    {'%', "\\%"},
    {'#', "\\#"},
    {'_', "\\_"},
    {'^', "\\^{}"},
    {'~', "\\~{}"}
};
const int ESCAPE_RULES_LATEX_COUNT = (int)(sizeof(ESCAPE_RULES_LATEX) / sizeof(ESCAPE_RULES_LATEX[0]));

const EscapeRule ESCAPE_RULES_YAML[] = {
    {'"', "\\\""},
    {'\\', "\\\\"},
    {'\n', "\\n"},
    {'\r', "\\r"},
    {'\t', "\\t"}
};
const int ESCAPE_RULES_YAML_COUNT = (int)(sizeof(ESCAPE_RULES_YAML) / sizeof(ESCAPE_RULES_YAML[0]));

const EscapeRule ESCAPE_RULES_JSX_TEXT[] = {
    {'&', "&amp;"},
    {'<', "&lt;"},
    {'>', "&gt;"},
    {'{', "&#123;"},
    {'}', "&#125;"}
};
const int ESCAPE_RULES_JSX_TEXT_COUNT = (int)(sizeof(ESCAPE_RULES_JSX_TEXT) / sizeof(ESCAPE_RULES_JSX_TEXT[0]));

const EscapeRule ESCAPE_RULES_JSX_ATTR[] = {
    {'"', "&quot;"},
    {'&', "&amp;"},
    {'<', "&lt;"},
    {'>', "&gt;"}
};
const int ESCAPE_RULES_JSX_ATTR_COUNT = (int)(sizeof(ESCAPE_RULES_JSX_ATTR) / sizeof(ESCAPE_RULES_JSX_ATTR[0]));

const EscapeRule ESCAPE_RULES_GRAPH_DOT[] = {
    {'"', "\\\""},
    {'\\', "\\\\"}
};
const int ESCAPE_RULES_GRAPH_DOT_COUNT = (int)(sizeof(ESCAPE_RULES_GRAPH_DOT) / sizeof(ESCAPE_RULES_GRAPH_DOT[0]));

const EscapeRule ESCAPE_RULES_GRAPH_QUOTED[] = {
    {'"', "\\\""}
};
const int ESCAPE_RULES_GRAPH_QUOTED_COUNT = (int)(sizeof(ESCAPE_RULES_GRAPH_QUOTED) / sizeof(ESCAPE_RULES_GRAPH_QUOTED[0]));

static const char* escape_find_rule(char c, const EscapeRule* rules, int rule_count) {
    for (int i = 0; i < rule_count; i++) {
        if (rules[i].from == c) return rules[i].to;
    }
    return NULL;
}

char escape_decode_c_char(char c) {
    switch (c) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case '\\': return '\\';
    case '\'': return '\'';
    case '"': return '"';
    case '0': return '\0';
    case 'a': return '\a';
    case 'b': return '\b';
    case 'f': return '\f';
    case 'v': return '\v';
    default: return c;
    }
}

bool escape_decode_utf16_escape(const char* s, size_t len, bool replacement,
                                uint32_t* codepoint, size_t* consumed) {
    if (!s || !codepoint || !consumed || len < 4) return false;

    uint32_t value = 0;
    for (size_t i = 0; i < 4; i++) {
        if (!str_is_hex(s[i])) return false;
        value = (value << 4) | (uint32_t)str_hex_val(s[i]);
    }
    *consumed = 4;

    if (utf_is_high_surrogate(value)) {
        if (len >= 10 && s[4] == '\\' && s[5] == 'u') {
            uint32_t low = 0;
            for (size_t i = 0; i < 4; i++) {
                if (!str_is_hex(s[6 + i])) break;
                low = (low << 4) | (uint32_t)str_hex_val(s[6 + i]);
                if (i == 3 && utf_is_low_surrogate(low)) {
                    *codepoint = utf16_decode_pair((uint16_t)value, (uint16_t)low);
                    *consumed = 10;
                    return true;
                }
            }
        }
        *codepoint = replacement ? 0xFFFD : value;
        return true;
    }

    *codepoint = replacement && utf_is_low_surrogate(value) ? 0xFFFD : value;
    return true;
}

static void escape_append_char_strbuf(void* out, char c) {
    strbuf_append_char((StrBuf*)out, c);
}

static void escape_append_str_strbuf(void* out, const char* s) {
    strbuf_append_str((StrBuf*)out, s);
}

static void escape_append_char_stringbuf(void* out, char c) {
    stringbuf_append_char((StringBuf*)out, c);
}

static void escape_append_str_stringbuf(void* out, const char* s) {
    stringbuf_append_str((StringBuf*)out, s);
}

static void escape_append_json_common(void* out, const char* s, size_t len,
        bool quote, bool escape_utf8_surrogates,
        EscapeAppendCharFn append_char, EscapeAppendStrFn append_str) {
    if (!out || !s || !append_char || !append_str) return;

    if (quote) append_char(out, '"');
    char tmp[16];
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        const char* replacement = escape_find_rule((char)c, ESCAPE_RULES_JSON, ESCAPE_RULES_JSON_COUNT);
        if (replacement) {
            append_str(out, replacement);
            continue;
        }
        if (c < 0x20) {
            snprintf(tmp, sizeof(tmp), "\\u%04x", c);
            append_str(out, tmp);
            continue;
        }
        if (escape_utf8_surrogates && c >= 0xED && c <= 0xEF && i + 2 < len) {
            // ES2019 JSON.stringify requires lone surrogate code points to be
            // emitted as escapes instead of ill-formed UTF-8.
            unsigned char c2 = (unsigned char)s[i + 1];
            unsigned char c3 = (unsigned char)s[i + 2];
            if ((c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80) {
                unsigned int cp = ((unsigned int)(c & 0x0F) << 12) |
                                  ((unsigned int)(c2 & 0x3F) << 6) |
                                  (unsigned int)(c3 & 0x3F);
                if (cp >= 0xD800 && cp <= 0xDFFF) {
                    // a WTF-8 pair is one valid character, not two lone surrogates.
                    if (utf_is_high_surrogate(cp) && i + 5 < len &&
                            (unsigned char)s[i + 3] == 0xED &&
                            ((unsigned char)s[i + 4] & 0xF0) == 0xB0 &&
                            ((unsigned char)s[i + 5] & 0xC0) == 0x80) {
                        uint16_t low = (uint16_t)(0xD000 |
                            (((unsigned char)s[i + 4] & 0x3F) << 6) |
                            ((unsigned char)s[i + 5] & 0x3F));
                        utf8_encode_z(utf16_decode_pair((uint16_t)cp, low), tmp);
                        append_str(out, tmp);
                        i += 5;
                        continue;
                    }
                    snprintf(tmp, sizeof(tmp), "\\u%04x", cp);
                    append_str(out, tmp);
                    i += 2;
                    continue;
                }
            }
        }
        append_char(out, (char)c);
    }
    if (quote) append_char(out, '"');
}

void escape_append_json_string(StrBuf* out, const char* s, size_t len,
                               bool quote, bool escape_utf8_surrogates) {
    escape_append_json_common(out, s, len, quote, escape_utf8_surrogates,
        escape_append_char_strbuf, escape_append_str_strbuf);
}

void escape_append_json_stringbuf(StringBuf* out, const char* s, size_t len,
                                  bool quote, bool escape_utf8_surrogates) {
    escape_append_json_common(out, s, len, quote, escape_utf8_surrogates,
        escape_append_char_stringbuf, escape_append_str_stringbuf);
}

void escape_append_json_to(void* out, const char* s, size_t len,
                           bool quote, bool escape_utf8_surrogates,
                           EscapeAppendCharFn append_char, EscapeAppendStrFn append_str) {
    escape_append_json_common(out, s, len, quote, escape_utf8_surrogates,
                              append_char, append_str);
}

void escape_append_js_quoted(StrBuf* out, const char* s, size_t len, char quote) {
    if (!out || !s) return;
    for (size_t i = 0; i < len; i++) {
        char ch = s[i];
        if (ch == '\\') strbuf_append_str(out, "\\\\");
        else if (ch == quote) {
            strbuf_append_char(out, '\\');
            strbuf_append_char(out, quote);
        }
        else if (ch == '\n') strbuf_append_str(out, "\\n");
        else if (ch == '\r') strbuf_append_str(out, "\\r");
        else if (ch == '\t') strbuf_append_str(out, "\\t");
        else strbuf_append_char(out, ch);
    }
}

void escape_append_html_text(StrBuf* out, const char* s, size_t len) {
    escape_append(out, s, len, ESCAPE_RULES_HTML_TEXT, ESCAPE_RULES_HTML_TEXT_COUNT,
                  ESCAPE_CTRL_NONE);
}

void escape_append_xml_attr(StrBuf* out, const char* s, size_t len) {
    escape_append(out, s, len, ESCAPE_RULES_XML_ATTR, ESCAPE_RULES_XML_ATTR_COUNT,
                  ESCAPE_CTRL_XML_NUMERIC);
}

static bool escape_css_is_whitespace(char c, bool consume_form_feed) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
        (consume_form_feed && c == '\f');
}

static int escape_hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

static void escape_css_append_codepoint(char* out, size_t* out_pos,
                                        uint32_t codepoint,
                                        bool replace_invalid_codepoint) {
    char encoded[4];
    size_t length = str_utf8_encode(codepoint, encoded, sizeof(encoded));
    if (length == 0 && replace_invalid_codepoint) {
        length = str_utf8_encode(0xFFFD, encoded, sizeof(encoded));
    }
    if (length > 0 && out) memcpy(out + *out_pos, encoded, length);
    *out_pos += length;
}

static size_t escape_css_unescape_write(char* out, const char* s, size_t len,
                                        bool decode_single_character,
                                        EscapeCssEofMode eof_mode,
                                        bool replace_invalid_codepoint,
                                        bool consume_form_feed) {
    size_t out_pos = 0;
    for (size_t i = 0; i < len;) {
        if (s[i] != '\\') {
            if (out) out[out_pos] = s[i];
            out_pos++;
            i++;
            continue;
        }

        i++;
        if (i == len) {
            if (eof_mode == ESCAPE_CSS_EOF_PRESERVE) {
                if (out) out[out_pos] = '\\';
                out_pos++;
            } else if (eof_mode == ESCAPE_CSS_EOF_REPLACEMENT) {
                escape_css_append_codepoint(out, &out_pos, 0xFFFD, true);
            }
            continue;
        }

        if (str_is_hex(s[i])) {
            uint32_t codepoint = 0;
            int digits = 0;
            while (i < len && digits < 6 && str_is_hex(s[i])) {
                codepoint = (codepoint << 4) | (uint32_t)escape_hex_value(s[i]);
                i++;
                digits++;
            }
            if (i < len && escape_css_is_whitespace(s[i], consume_form_feed)) i++;
            escape_css_append_codepoint(out, &out_pos, codepoint,
                                        replace_invalid_codepoint);
            continue;
        }

        if (decode_single_character) {
            if (out) out[out_pos] = s[i];
            out_pos++;
            i++;
        } else {
            if (out) out[out_pos] = '\\';
            out_pos++;
        }
    }
    return out_pos;
}

char* escape_css_unescape_pool(Pool* pool, const char* s, size_t len,
                               bool decode_single_character,
                               EscapeCssEofMode eof_mode,
                               bool replace_invalid_codepoint,
                               bool consume_form_feed) {
    if (!pool) return NULL;
    if (!s) len = 0;
    size_t output_len = escape_css_unescape_write(NULL, s, len,
        decode_single_character, eof_mode, replace_invalid_codepoint,
        consume_form_feed);
    if (output_len == SIZE_MAX) return NULL;
    char* output = (char*)pool_alloc(pool, output_len + 1);
    if (!output) return NULL;
    escape_css_unescape_write(output, s, len, decode_single_character,
        eof_mode, replace_invalid_codepoint, consume_form_feed);
    output[output_len] = '\0';
    return output;
}

static bool escape_bash_append_simple(StrBuf* out, char escape) {
    switch (escape) {
    case 'a': strbuf_append_char(out, '\a'); return true;
    case 'b': strbuf_append_char(out, '\b'); return true;
    case 'e': case 'E': strbuf_append_char(out, '\x1B'); return true;
    case 'f': strbuf_append_char(out, '\f'); return true;
    case 'n': strbuf_append_char(out, '\n'); return true;
    case 'r': strbuf_append_char(out, '\r'); return true;
    case 't': strbuf_append_char(out, '\t'); return true;
    case 'v': strbuf_append_char(out, '\v'); return true;
    case '\\': strbuf_append_char(out, '\\'); return true;
    case '\'': strbuf_append_char(out, '\''); return true;
    case '"': strbuf_append_char(out, '"'); return true;
    default: return false;
    }
}

static uint32_t escape_bash_read_base(const char* s, size_t len, size_t* cursor,
                                      int max_digits, int base, bool* has_digits) {
    uint32_t value = 0;
    int digits = 0;
    while (*cursor < len && digits < max_digits) {
        char c = s[*cursor];
        int digit = base == 16 && str_is_hex(c) ? escape_hex_value(c) :
            (base == 8 && c >= '0' && c <= '7' ? c - '0' : -1);
        if (digit < 0) break;
        value = value * (uint32_t)base + (uint32_t)digit;
        (*cursor)++;
        digits++;
    }
    *has_digits = digits > 0;
    return value;
}

bool escape_append_bash_ansi_at(StrBuf* out, const char* s, size_t len,
                                size_t* cursor, EscapeBashAnsiMode mode) {
    if (!out || !s || !cursor || *cursor >= len) return true;
    size_t i = *cursor;
    char c = s[i++];
    if (c != '\\' || i == len) {
        strbuf_append_char(out, c);
        *cursor = i;
        return true;
    }

    char escape = s[i++];
    if (escape_bash_append_simple(out, escape)) {
        *cursor = i;
        return true;
    }
    if (mode == ESCAPE_BASH_ANSI_RUNTIME && escape == '?') {
        strbuf_append_char(out, '?');
        *cursor = i;
        return true;
    }
    if (escape == 'c') {
        if (mode == ESCAPE_BASH_ANSI_PRINTF) {
            *cursor = i;
            return false;
        }
        if (i < len) {
            unsigned char control = (unsigned char)s[i++];
            strbuf_append_char(out, (char)(mode == ESCAPE_BASH_ANSI_LITERAL
                ? control ^ 0x40 : control & 0x1F));
        } else if (mode == ESCAPE_BASH_ANSI_RUNTIME) {
            strbuf_append_all(out, 2, "\\", "c");
        }
        *cursor = i;
        return true;
    }
    if (escape == 'x') {
        uint32_t value = 0;
        bool has_digits = false;
        if (mode == ESCAPE_BASH_ANSI_LITERAL && i < len && s[i] == '{') {
            i++;
            while (i < len && s[i] != '}' && str_is_hex(s[i])) {
                value = (value << 4) | (uint32_t)escape_hex_value(s[i++]);
                has_digits = true;
            }
            if (i < len && s[i] == '}') i++;
            if (!has_digits) {
                *cursor = len;
                return true;
            }
        } else {
            value = escape_bash_read_base(s, len, &i, 2, 16, &has_digits);
        }
        if (has_digits) strbuf_append_char(out, (char)value);
        else if (mode != ESCAPE_BASH_ANSI_LITERAL) strbuf_append_all(out, 2, "\\", "x");
        *cursor = i;
        return true;
    }
    if (escape == 'u' || escape == 'U') {
        bool has_digits = false;
        uint32_t value = escape_bash_read_base(s, len, &i,
            escape == 'u' ? 4 : 8, 16, &has_digits);
        if (has_digits) strbuf_append_utf8(out, value);
        else if (mode != ESCAPE_BASH_ANSI_LITERAL) strbuf_append_all(out, 2, "\\",
            escape == 'u' ? "u" : "U");
        *cursor = i;
        return true;
    }
    if (escape >= '0' && escape <= '7' &&
        (mode != ESCAPE_BASH_ANSI_PRINTF || escape == '0')) {
        uint32_t value;
        if (escape == '0' && mode != ESCAPE_BASH_ANSI_LITERAL) {
            bool ignored = false;
            value = escape_bash_read_base(s, len, &i, 3, 8, &ignored);
        } else {
            value = (uint32_t)(escape - '0');
            for (int digits = 0; digits < 2 && i < len &&
                 s[i] >= '0' && s[i] <= '7'; digits++) {
                value = value * 8 + (uint32_t)(s[i++] - '0');
            }
        }
        strbuf_append_char(out, (char)value);
        *cursor = i;
        return true;
    }

    strbuf_append_char(out, '\\');
    strbuf_append_char(out, escape);
    *cursor = i;
    return true;
}

bool escape_append_bash_ansi(StrBuf* out, const char* s, size_t len,
                             EscapeBashAnsiMode mode) {
    size_t cursor = 0;
    while (cursor < len) {
        if (!escape_append_bash_ansi_at(out, s, len, &cursor, mode)) return false;
    }
    return true;
}

void escape_append(StrBuf* out, const char* s, size_t len,
                   const EscapeRule* rules, int rule_count,
                   EscapeCtrlMode ctrl_mode) {
    if (!out || !s) return;

    char tmp[16];
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        const char* replacement = rules ? escape_find_rule((char)c, rules, rule_count) : NULL;
        if (replacement) {
            strbuf_append_str(out, replacement);
            continue;
        }

        if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') {
            if (ctrl_mode == ESCAPE_CTRL_JSON_UNICODE) {
                snprintf(tmp, sizeof(tmp), "\\u%04x", c);
                strbuf_append_str(out, tmp);
            } else if (ctrl_mode == ESCAPE_CTRL_XML_NUMERIC) {
                snprintf(tmp, sizeof(tmp), "&#x%02x;", c);
                strbuf_append_str(out, tmp);
            } else if (ctrl_mode != ESCAPE_CTRL_DROP) {
                strbuf_append_char(out, (char)c);
            }
            continue;
        }

        strbuf_append_char(out, (char)c);
    }
}

void escape_append_stringbuf(StringBuf* out, const char* s, size_t len,
                             const EscapeRule* rules, int rule_count,
                             EscapeCtrlMode ctrl_mode) {
    if (!out || !s) return;

    char tmp[16];
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        const char* replacement = rules ? escape_find_rule((char)c, rules, rule_count) : NULL;
        if (replacement) {
            stringbuf_append_str(out, replacement);
            continue;
        }

        if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') {
            if (ctrl_mode == ESCAPE_CTRL_JSON_UNICODE) {
                snprintf(tmp, sizeof(tmp), "\\u%04x", c);
                stringbuf_append_str(out, tmp);
            } else if (ctrl_mode == ESCAPE_CTRL_XML_NUMERIC) {
                snprintf(tmp, sizeof(tmp), "&#x%02x;", c);
                stringbuf_append_str(out, tmp);
            } else if (ctrl_mode != ESCAPE_CTRL_DROP) {
                stringbuf_append_char(out, (char)c);
            }
            continue;
        }

        stringbuf_append_char(out, (char)c);
    }
}
