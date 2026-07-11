#include "escape.h"

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

typedef void (*EscapeAppendCharFn)(void* out, char c);
typedef void (*EscapeAppendStrFn)(void* out, const char* s);

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
