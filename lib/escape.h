#ifndef LIB_ESCAPE_H
#define LIB_ESCAPE_H

#include <stddef.h>
#include <stdbool.h>

#include "strbuf.h"
#include "stringbuf.h"
#include "mempool.h"

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

typedef enum {
    ESCAPE_QUOTED_NONE = 0,
    ESCAPE_QUOTED_LINE_BREAKS = 1 << 0,
    ESCAPE_QUOTED_C_CONTROLS = 1 << 1,
    ESCAPE_QUOTED_DROP_CARRIAGE_RETURN = 1 << 2
} EscapeQuotedOptions;

/* Decode the shared single-byte C/Python/Ruby escape set. */
char escape_decode_c_char(char c);

/* Decode four UTF-16 escape digits and an immediately following low surrogate.
 * When replacement is set, lone surrogate code units become U+FFFD. */
bool escape_decode_utf16_escape(const char* s, size_t len, bool replacement,
                                uint32_t* codepoint, size_t* consumed);

typedef enum {
    ESCAPE_CSS_EOF_PRESERVE,
    ESCAPE_CSS_EOF_DROP,
    ESCAPE_CSS_EOF_REPLACEMENT
} EscapeCssEofMode;

typedef enum {
    ESCAPE_BASH_ANSI_LITERAL,
    ESCAPE_BASH_ANSI_RUNTIME,
    ESCAPE_BASH_ANSI_PRINTF
} EscapeBashAnsiMode;

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
/* Escape content for a chosen quote delimiter through a byte-output adapter. */
void escape_append_quoted_to(void* out, const char* s, size_t len, char quote,
                             EscapeQuotedOptions options,
                             EscapeAppendCharFn append_char, EscapeAppendStrFn append_str);
void escape_append_js_quoted(StrBuf* out, const char* s, size_t len, char quote);
void escape_append_c_quoted(StrBuf* out, const char* s, size_t len, char quote);
void escape_append_stringbuf_quoted(StringBuf* out, const char* s, size_t len,
                                    char quote, EscapeQuotedOptions options);
void escape_append_lambda_quoted_drop_cr(StrBuf* out, const char* s, size_t len,
                                         char quote);
/* Append an ASCII JavaScript property key, quoting non-identifiers. */
void escape_append_js_property_key(StrBuf* out, const char* s, size_t len);
void escape_append_html_text(StrBuf* out, const char* s, size_t len);
void escape_append_xml_attr(StrBuf* out, const char* s, size_t len);
/* Decode Bash ANSI-C escapes; an empty braced hex escape stops literal input. */
bool escape_append_bash_ansi_at(StrBuf* out, const char* s, size_t len,
                                size_t* cursor, EscapeBashAnsiMode mode);
bool escape_append_bash_ansi(StrBuf* out, const char* s, size_t len,
                             EscapeBashAnsiMode mode);

/* Decode bounded CSS escapes into pool-owned UTF-8. */
char* escape_css_unescape_pool(Pool* pool, const char* s, size_t len,
                               bool decode_single_character,
                               EscapeCssEofMode eof_mode,
                               bool replace_invalid_codepoint,
                               bool consume_form_feed);

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
