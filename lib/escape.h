#ifndef LIB_ESCAPE_H
#define LIB_ESCAPE_H

#include <stddef.h>
#include <stdbool.h>

#include "strbuf.h"
#include "stringbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char from;
    const char* to;
} EscapeRule;

typedef enum {
    ESCAPE_CTRL_NONE = 0,
    ESCAPE_CTRL_JSON_UNICODE,
    ESCAPE_CTRL_XML_NUMERIC,
    ESCAPE_CTRL_DROP
} EscapeCtrlMode;

typedef void (*EscapeAppendCharFn)(void* out, char c);
typedef void (*EscapeAppendStrFn)(void* out, const char* s);

void escape_append(StrBuf* out, const char* s, size_t len,
                   const EscapeRule* rules, int rule_count,
                   EscapeCtrlMode ctrl_mode);
void escape_append_stringbuf(StringBuf* out, const char* s, size_t len,
                             const EscapeRule* rules, int rule_count,
                             EscapeCtrlMode ctrl_mode);
void escape_append_json_string(StrBuf* out, const char* s, size_t len,
                               bool quote, bool escape_utf8_surrogates);
void escape_append_json_stringbuf(StringBuf* out, const char* s, size_t len,
                                  bool quote, bool escape_utf8_surrogates);
void escape_append_json_to(void* out, const char* s, size_t len,
                           bool quote, bool escape_utf8_surrogates,
                           EscapeAppendCharFn append_char, EscapeAppendStrFn append_str);
void escape_append_js_quoted(StrBuf* out, const char* s, size_t len, char quote);
void escape_append_html_text(StrBuf* out, const char* s, size_t len);
void escape_append_xml_attr(StrBuf* out, const char* s, size_t len);

extern const EscapeRule ESCAPE_RULES_JSON[];
extern const int ESCAPE_RULES_JSON_COUNT;
extern const EscapeRule ESCAPE_RULES_HTML_TEXT[];
extern const int ESCAPE_RULES_HTML_TEXT_COUNT;
extern const EscapeRule ESCAPE_RULES_HTML_ATTR[];
extern const int ESCAPE_RULES_HTML_ATTR_COUNT;
extern const EscapeRule ESCAPE_RULES_XML_ATTR[];
extern const int ESCAPE_RULES_XML_ATTR_COUNT;
extern const EscapeRule ESCAPE_RULES_LATEX[];
extern const int ESCAPE_RULES_LATEX_COUNT;
extern const EscapeRule ESCAPE_RULES_YAML[];
extern const int ESCAPE_RULES_YAML_COUNT;
extern const EscapeRule ESCAPE_RULES_JSX_TEXT[];
extern const int ESCAPE_RULES_JSX_TEXT_COUNT;
extern const EscapeRule ESCAPE_RULES_JSX_ATTR[];
extern const int ESCAPE_RULES_JSX_ATTR_COUNT;
extern const EscapeRule ESCAPE_RULES_GRAPH_DOT[];
extern const int ESCAPE_RULES_GRAPH_DOT_COUNT;
extern const EscapeRule ESCAPE_RULES_GRAPH_QUOTED[];
extern const int ESCAPE_RULES_GRAPH_QUOTED_COUNT;

#ifdef __cplusplus
}
#endif

#endif
