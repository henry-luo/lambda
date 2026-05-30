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

static const char* escape_find_rule(char c, const EscapeRule* rules, int rule_count) {
    for (int i = 0; i < rule_count; i++) {
        if (rules[i].from == c) return rules[i].to;
    }
    return NULL;
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
