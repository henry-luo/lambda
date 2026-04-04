// bash_heredoc.cpp — Here-Document Engine (Phase H — Module 11)
//
// Implements here-doc expansion, here-strings, tab stripping,
// and stdin passing for heredoc/herestring content.

#include "bash_heredoc.h"
#include "bash_runtime.h"
#include "bash_arith.h"
#include "bash_errors.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"

#include <cstring>

// ============================================================================
// Here-document expansion
// ============================================================================

// scan a variable name starting at p, return length of name
static int scan_var_name(const char* p, int max) {
    int i = 0;
    // first char: alpha or _
    if (i < max && (p[i] == '_' || (p[i] >= 'a' && p[i] <= 'z') ||
                    (p[i] >= 'A' && p[i] <= 'Z'))) {
        i++;
        while (i < max && (p[i] == '_' || (p[i] >= 'a' && p[i] <= 'z') ||
                           (p[i] >= 'A' && p[i] <= 'Z') ||
                           (p[i] >= '0' && p[i] <= '9'))) {
            i++;
        }
    }
    return i;
}

// check for special single-char variables: $?, $$, $!, $#, $-, $0-$9
static int is_special_var(char c) {
    return c == '?' || c == '$' || c == '!' || c == '#' || c == '-' ||
           c == '@' || c == '*' || c == '_' ||
           (c >= '0' && c <= '9');
}

extern "C" Item bash_heredoc_expand(Item body) {
    Item str_item = bash_to_string(body);
    String* s = it2s(str_item);
    if (!s || s->len == 0) return body;

    StrBuf* result = strbuf_new_cap((size_t)(s->len + 64));

    const char* p = s->chars;
    const char* end = p + s->len;

    while (p < end) {
        // backslash escaping: \$, \`, \\, \newline
        if (*p == '\\' && (p + 1) < end) {
            char next = p[1];
            if (next == '$' || next == '`' || next == '\\') {
                strbuf_append_char(result, next);
                p += 2;
                continue;
            }
            if (next == '\n') {
                // line continuation: skip both
                p += 2;
                continue;
            }
            // other backslash: keep literally
            strbuf_append_char(result, *p);
            p++;
            continue;
        }

        // $(...) command substitution
        if (*p == '$' && (p + 1) < end && p[1] == '(') {
            if ((p + 2) < end && p[2] == '(') {
                // $(( ... )) arithmetic expansion
                const char* start = p + 3;
                int depth = 1;
                const char* q = start;
                while (q < end && depth > 0) {
                    if (q + 1 < end && q[0] == ')' && q[1] == ')') {
                        depth--;
                        if (depth == 0) break;
                    }
                    if (q + 1 < end && q[0] == '(' && q[1] == '(') {
                        depth++;
                    }
                    q++;
                }
                int expr_len = (int)(q - start);
                if (expr_len > 0) {
                    Item expr = (Item){.item = s2it(heap_create_name(start, expr_len))};
                    Item val = bash_arith_eval_string(expr);
                    Item val_str = bash_to_string(val);
                    String* vs = it2s(val_str);
                    if (vs && vs->len > 0) {
                        strbuf_append_str_n(result, vs->chars, vs->len);
                    }
                }
                p = q + 2;  // skip ))
                continue;
            }

            // $( ... ) command substitution
            const char* start = p + 2;
            int depth = 1;
            const char* q = start;
            while (q < end && depth > 0) {
                if (*q == '(') depth++;
                else if (*q == ')') depth--;
                q++;
            }
            int cmd_len = (int)(q - start - 1);
            if (cmd_len > 0) {
                Item cmd = (Item){.item = s2it(heap_create_name(start, cmd_len))};
                Item val = bash_eval_string(cmd);
                Item val_str = bash_to_string(val);
                String* vs = it2s(val_str);
                if (vs && vs->len > 0) {
                    strbuf_append_str_n(result, vs->chars, vs->len);
                }
            }
            p = q;
            continue;
        }

        // ${...} parameter expansion
        if (*p == '$' && (p + 1) < end && p[1] == '{') {
            const char* start = p + 2;
            const char* q = start;
            int depth = 1;
            while (q < end && depth > 0) {
                if (*q == '{') depth++;
                else if (*q == '}') depth--;
                q++;
            }
            int name_len = (int)(q - start - 1);
            if (name_len > 0) {
                Item name = (Item){.item = s2it(heap_create_name(start, name_len))};
                // try simple variable lookup first
                Item val = bash_get_var(name);
                Item val_str = bash_to_string(val);
                String* vs = it2s(val_str);
                if (vs && vs->len > 0) {
                    strbuf_append_str_n(result, vs->chars, vs->len);
                }
            }
            p = q;
            continue;
        }

        // $name or $N (simple variable reference)
        if (*p == '$' && (p + 1) < end) {
            p++;  // skip $
            if (is_special_var(*p)) {
                char c = *p;
                p++;
                char name_buf[2] = {c, '\0'};
                Item name = (Item){.item = s2it(heap_create_name(name_buf, 1))};
                Item val = bash_get_var(name);
                Item val_str = bash_to_string(val);
                String* vs = it2s(val_str);
                if (vs && vs->len > 0) {
                    strbuf_append_str_n(result, vs->chars, vs->len);
                }
                continue;
            }

            int name_len = scan_var_name(p, (int)(end - p));
            if (name_len > 0) {
                Item name = (Item){.item = s2it(heap_create_name(p, name_len))};
                Item val = bash_get_var(name);
                Item val_str = bash_to_string(val);
                String* vs = it2s(val_str);
                if (vs && vs->len > 0) {
                    strbuf_append_str_n(result, vs->chars, vs->len);
                }
                p += name_len;
                continue;
            }

            // bare $ with no valid name after it
            strbuf_append_char(result, '$');
            continue;
        }

        // backtick command substitution: `cmd`
        if (*p == '`') {
            const char* start = p + 1;
            const char* q = start;
            while (q < end && *q != '`') {
                if (*q == '\\' && (q + 1) < end) q++;  // skip escaped char
                q++;
            }
            int cmd_len = (int)(q - start);
            if (cmd_len > 0) {
                Item cmd = (Item){.item = s2it(heap_create_name(start, cmd_len))};
                Item val = bash_eval_string(cmd);
                Item val_str = bash_to_string(val);
                String* vs = it2s(val_str);
                if (vs && vs->len > 0) {
                    strbuf_append_str_n(result, vs->chars, vs->len);
                }
            }
            p = (q < end) ? q + 1 : q;
            continue;
        }

        // literal character
        strbuf_append_char(result, *p);
        p++;
    }

    int len = (int)result->length;
    Item ret = (Item){.item = s2it(heap_create_name(result->str, len))};
    strbuf_free(result);

    log_debug("bash_heredoc_expand: %d -> %d bytes", s->len, len);
    return ret;
}

// ============================================================================
// Here-string
// ============================================================================

extern "C" Item bash_herestring_expand(Item word) {
    // expand the word
    Item expanded = bash_heredoc_expand(word);
    String* s = it2s(expanded);

    // append newline
    if (s && s->len > 0) {
        StrBuf* buf = strbuf_new_cap((size_t)(s->len + 2));
        strbuf_append_str_n(buf, s->chars, s->len);
        strbuf_append_char(buf, '\n');
        int len = (int)buf->length;
        Item ret = (Item){.item = s2it(heap_create_name(buf->str, len))};
        strbuf_free(buf);
        return ret;
    }

    return (Item){.item = s2it(heap_create_name("\n", 1))};
}

// ============================================================================
// Tab stripping
// ============================================================================

extern "C" Item bash_heredoc_strip_tabs(Item body) {
    Item str_item = bash_to_string(body);
    String* s = it2s(str_item);
    if (!s || s->len == 0) return body;

    StrBuf* result = strbuf_new_cap((size_t)s->len);

    const char* p = s->chars;
    const char* end = p + s->len;
    int at_line_start = 1;

    while (p < end) {
        if (at_line_start && *p == '\t') {
            // skip leading tab
            p++;
            continue;
        }

        at_line_start = (*p == '\n');
        strbuf_append_char(result, *p);
        p++;
    }

    int len = (int)result->length;
    Item ret = (Item){.item = s2it(heap_create_name(result->str, len))};
    strbuf_free(result);
    return ret;
}

// ============================================================================
// Heredoc stdin passing
// ============================================================================

extern "C" void bash_set_heredoc_stdin(Item content) {
    bash_set_stdin_item(content);
}

extern "C" Item bash_get_heredoc_stdin(void) {
    return bash_get_stdin_item();
}

extern "C" void bash_clear_heredoc_stdin(void) {
    bash_clear_stdin_item();
}
