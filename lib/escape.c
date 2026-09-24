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

char escape_decode_js_char(char c) {
    switch (c) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case '\\': return '\\';
    case '\'': return '\'';
    case '"': return '"';
    case '0': return '\0';
    case 'b': return '\b';
    case 'f': return '\f';
    case 'v': return '\v';
    default: return c;
    }
}

char escape_decode_c_char(char c) {
    // C, Python and Ruby add the BEL escape; JavaScript reads `\a` as `a`.
    return c == 'a' ? '\a' : escape_decode_js_char(c);
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

/* length-aware appends: the typed wrappers bulk-copy runs that need no
 * escaping instead of calling append_char through a pointer for every byte */
typedef void (*EscapeAppendNFn)(void* out, const char* s, size_t n);

static void escape_append_n_strbuf(void* out, const char* s, size_t n) {
    strbuf_append_str_n((StrBuf*)out, s, n);
}

static void escape_append_n_stringbuf(void* out, const char* s, size_t n) {
    stringbuf_append_str_n((StringBuf*)out, s, n);
}

/* Appends s[i..] up to the first byte in `stops` -- the bytes the calling
 * escaper must handle itself -- and returns that byte's index, or len. Clean
 * text then costs one scan and one bulk copy per run instead of a rule lookup
 * and an indirect append per byte. Without a length-aware sink (the public
 * callback entry points) the run still goes out a byte at a time. */
static size_t escape_append_run(void* out, const char* s, size_t i, size_t len,
                                const StrByteSet* stops,
                                EscapeAppendCharFn append_char,
                                EscapeAppendNFn append_n) {
    size_t at = str_find_byteset(s + i, len - i, stops);
    size_t end = at == STR_NPOS ? len : i + at;
    if (append_n) {
        if (end > i) append_n(out, s + i, end - i);
    } else {
        for (size_t k = i; k < end; k++) append_char(out, s[k]);
    }
    return end;
}

size_t escape_append_run_stringbuf(StringBuf* out, const char* s, size_t from,
                                   size_t len, const StrByteSet* stops) {
    return escape_append_run(out, s, from, len, stops,
                             escape_append_char_stringbuf, escape_append_n_stringbuf);
}

/* The bytes the JSON escaper handles: controls, '"' and '\\', plus 0xED -- the
 * only lead byte of a UTF-8 surrogate -- when surrogates are escaped. Word w
 * of a StrByteSet holds bytes 64w..64w+63. */
static const StrByteSet ESCAPE_JSON_STOPS = {{
    0xFFFFFFFFULL | (1ULL << '"'), 1ULL << ('\\' - 64), 0, 0 }};
static const StrByteSet ESCAPE_JSON_SURROGATE_STOPS = {{
    0xFFFFFFFFULL | (1ULL << '"'), 1ULL << ('\\' - 64), 0, 1ULL << (0xED - 192) }};

static void escape_append_json_common(void* out, const char* s, size_t len,
        bool quote, bool escape_utf8_surrogates,
        EscapeAppendCharFn append_char, EscapeAppendStrFn append_str,
        EscapeAppendNFn append_n) {
    if (!out || !s || !append_char || !append_str) return;

    if (quote) append_char(out, '"');
    const StrByteSet* stops = escape_utf8_surrogates ? &ESCAPE_JSON_SURROGATE_STOPS
                                                     : &ESCAPE_JSON_STOPS;
    char tmp[16];
    for (size_t i = 0; i < len; i++) {
        i = escape_append_run(out, s, i, len, stops, append_char, append_n);
        if (i >= len) break;
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
        escape_append_char_strbuf, escape_append_str_strbuf, escape_append_n_strbuf);
}

void escape_append_json_stringbuf(StringBuf* out, const char* s, size_t len,
                                  bool quote, bool escape_utf8_surrogates) {
    escape_append_json_common(out, s, len, quote, escape_utf8_surrogates,
        escape_append_char_stringbuf, escape_append_str_stringbuf, escape_append_n_stringbuf);
}

void escape_append_json_to(void* out, const char* s, size_t len,
                           bool quote, bool escape_utf8_surrogates,
                           EscapeAppendCharFn append_char, EscapeAppendStrFn append_str) {
    escape_append_json_common(out, s, len, quote, escape_utf8_surrogates,
                              append_char, append_str, NULL);
}

static void escape_append_quoted_common(void* out, const char* s, size_t len, char quote,
        EscapeQuotedOptions options, EscapeAppendCharFn append_char,
        EscapeAppendStrFn append_str, EscapeAppendNFn append_n) {
    if (!out || !s || !append_char || !append_str) return;
    /* exactly the bytes the branches below rewrite or drop */
    StrByteSet stops;
    str_byteset_clear(&stops);
    str_byteset_add(&stops, '\\');
    str_byteset_add(&stops, (unsigned char)quote);
    if (options & (ESCAPE_QUOTED_LINE_BREAKS | ESCAPE_QUOTED_DROP_CARRIAGE_RETURN)) {
        str_byteset_add(&stops, '\r');
    }
    if (options & ESCAPE_QUOTED_LINE_BREAKS) {
        str_byteset_add(&stops, '\n');
        str_byteset_add(&stops, '\t');
    }
    if (options & ESCAPE_QUOTED_C_CONTROLS) {
        str_byteset_add(&stops, '\b');
        str_byteset_add(&stops, '\f');
    }
    for (size_t i = 0; i < len; i++) {
        i = escape_append_run(out, s, i, len, &stops, append_char, append_n);
        if (i >= len) break;
        char ch = s[i];
        if (ch == '\\') append_str(out, "\\\\");
        else if (ch == quote) {
            append_char(out, '\\');
            append_char(out, quote);
        }
        else if (ch == '\r' && (options & ESCAPE_QUOTED_DROP_CARRIAGE_RETURN)) continue;
        else if (ch == '\b' && (options & ESCAPE_QUOTED_C_CONTROLS)) append_str(out, "\\b");
        else if (ch == '\f' && (options & ESCAPE_QUOTED_C_CONTROLS)) append_str(out, "\\f");
        else if (ch == '\n' && (options & ESCAPE_QUOTED_LINE_BREAKS)) append_str(out, "\\n");
        else if (ch == '\r' && (options & ESCAPE_QUOTED_LINE_BREAKS)) append_str(out, "\\r");
        else if (ch == '\t' && (options & ESCAPE_QUOTED_LINE_BREAKS)) append_str(out, "\\t");
        else append_char(out, ch);
    }
}

void escape_append_quoted_to(void* out, const char* s, size_t len, char quote,
                             EscapeQuotedOptions options,
                             EscapeAppendCharFn append_char, EscapeAppendStrFn append_str) {
    escape_append_quoted_common(out, s, len, quote, options, append_char, append_str, NULL);
}

void escape_append_js_quoted(StrBuf* out, const char* s, size_t len, char quote) {
    escape_append_quoted_common(out, s, len, quote, ESCAPE_QUOTED_LINE_BREAKS,
        escape_append_char_strbuf, escape_append_str_strbuf, escape_append_n_strbuf);
}

void escape_append_c_quoted(StrBuf* out, const char* s, size_t len, char quote) {
    escape_append_quoted_common(out, s, len, quote,
        (EscapeQuotedOptions)(ESCAPE_QUOTED_LINE_BREAKS | ESCAPE_QUOTED_C_CONTROLS),
        escape_append_char_strbuf, escape_append_str_strbuf, escape_append_n_strbuf);
}

void escape_append_stringbuf_quoted(StringBuf* out, const char* s, size_t len,
                                    char quote, EscapeQuotedOptions options) {
    escape_append_quoted_common(out, s, len, quote, options,
        escape_append_char_stringbuf, escape_append_str_stringbuf, escape_append_n_stringbuf);
}

void escape_append_lambda_quoted_drop_cr(StrBuf* out, const char* s, size_t len,
                                         char quote) {
    escape_append_quoted_common(out, s, len, quote,
        (EscapeQuotedOptions)(ESCAPE_QUOTED_LINE_BREAKS |
                              ESCAPE_QUOTED_DROP_CARRIAGE_RETURN),
        escape_append_char_strbuf, escape_append_str_strbuf, escape_append_n_strbuf);
}

static bool escape_is_js_identifier(const char* s, size_t len) {
    if (!s || len == 0) return false;
    char first = s[0];
    if (!str_is_alpha(first) && first != '_' && first != '$') return false;
    for (size_t i = 1; i < len; i++) {
        char c = s[i];
        if (!str_is_alnum(c) && c != '_' && c != '$') return false;
    }
    return true;
}

void escape_append_js_property_key(StrBuf* out, const char* s, size_t len) {
    if (!out || !s) return;
    if (escape_is_js_identifier(s, len)) {
        strbuf_append_str_n(out, s, len);
        return;
    }
    strbuf_append_char(out, '\'');
    escape_append_js_quoted(out, s, len, '\'');
    strbuf_append_char(out, '\'');
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

static void escape_append_common(void* out, const char* s, size_t len,
                                 const EscapeRule* rules, int rule_count,
                                 EscapeCtrlMode ctrl_mode,
                                 EscapeAppendCharFn append_char,
                                 EscapeAppendStrFn append_str,
                                 EscapeAppendNFn append_n) {
    if (!out || !s || !append_char || !append_str) return;

    /* the rule bytes, plus the controls other than \n \r \t unless ctrl_mode
     * passes them through unchanged (ESCAPE_CTRL_NONE) */
    StrByteSet stops;
    str_byteset_clear(&stops);
    for (int r = 0; rules && r < rule_count; r++) {
        str_byteset_add(&stops, (unsigned char)rules[r].from);
    }
    if (ctrl_mode != ESCAPE_CTRL_NONE) {
        str_byteset_add_range(&stops, 0x00, 0x08);
        str_byteset_add_range(&stops, 0x0B, 0x0C);
        str_byteset_add_range(&stops, 0x0E, 0x1F);
    }
    char tmp[16];
    for (size_t i = 0; i < len; i++) {
        i = escape_append_run(out, s, i, len, &stops, append_char, append_n);
        if (i >= len) break;
        unsigned char c = (unsigned char)s[i];
        const char* replacement = rules ? escape_find_rule((char)c, rules, rule_count) : NULL;
        if (replacement) {
            append_str(out, replacement);
            continue;
        }

        if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') {
            if (ctrl_mode == ESCAPE_CTRL_JSON_UNICODE) {
                snprintf(tmp, sizeof(tmp), "\\u%04x", c);
                append_str(out, tmp);
            } else if (ctrl_mode == ESCAPE_CTRL_XML_NUMERIC) {
                snprintf(tmp, sizeof(tmp), "&#x%02x;", c);
                append_str(out, tmp);
            } else if (ctrl_mode != ESCAPE_CTRL_DROP) {
                append_char(out, (char)c);
            }
            continue;
        }

        append_char(out, (char)c);
    }
}

void escape_append(StrBuf* out, const char* s, size_t len,
                   const EscapeRule* rules, int rule_count,
                   EscapeCtrlMode ctrl_mode) {
    escape_append_common(out, s, len, rules, rule_count, ctrl_mode,
                         escape_append_char_strbuf, escape_append_str_strbuf,
                         escape_append_n_strbuf);
}

void escape_append_stringbuf(StringBuf* out, const char* s, size_t len,
                             const EscapeRule* rules, int rule_count,
                             EscapeCtrlMode ctrl_mode) {
    escape_append_common(out, s, len, rules, rule_count, ctrl_mode,
                         escape_append_char_stringbuf, escape_append_str_stringbuf,
                         escape_append_n_stringbuf);
}
