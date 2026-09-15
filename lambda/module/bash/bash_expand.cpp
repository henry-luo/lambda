// bash_expand.cpp — Word Expansion Engine (Phase A — Module 1)
//
// Implements IFS word splitting, quote removal, ANSI-C escape processing,
// and the unified expansion pipeline for dynamic contexts.

#include "bash_expand.h"
#include "bash_runtime.h"
#include "../../runtime/transpiler.hpp"
#include "../../../lib/escape.h"
#include "../../../lib/strbuf.h"
#include "../../../lib/log.h"
#include "../../../lib/utf.h"

#include <string.h>
#include <ctype.h>
#include "../../../lib/mem.h"

// local helper: convert Item to C string (mirrors bash_runtime.cpp static helper)
static const char* item_to_cstr(Item value) {
    Item str = bash_to_string(value);
    String* s = it2s(str);
    if (!s || s->len == 0) return "";
    return s->chars;
}

// ========================================================================
// IFS Word Splitting
// ========================================================================

extern "C" Item bash_word_split(Item str, Item ifs) {
    Item arr = bash_array_new();
    return bash_word_split_into(arr, str, ifs);
}

extern "C" Item bash_word_split_into(Item arr, Item str, Item ifs) {
    const char* s = item_to_cstr(str);
    if (!s || !*s) return arr;

    // determine IFS value
    const char* ifs_val = NULL;
    int ifs_len = 0;
    String* ifs_str = it2s(ifs);
    if (ifs_str) {
        ifs_val = ifs_str->chars;
        ifs_len = ifs_str->len;
    }

    // NULL IFS means use default: space/tab/newline
    if (!ifs_val) {
        ifs_val = " \t\n";
        ifs_len = 3;
    }

    // empty IFS means no splitting: return whole string
    if (ifs_len == 0) {
        bash_array_append(arr, str);
        return arr;
    }

    int slen = (int)strlen(s);

    // build IFS character classification maps
    bool ifs_map[256] = {};
    bool ifs_ws_map[256] = {};
    for (int i = 0; i < ifs_len; i++) {
        unsigned char c = (unsigned char)ifs_val[i];
        ifs_map[c] = true;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ifs_ws_map[c] = true;
        }
    }

    // POSIX IFS splitting algorithm
    StrBuf* cur = strbuf_new();
    int i = 0;

    // step 1: skip leading IFS whitespace
    while (i < slen && ifs_ws_map[(unsigned char)s[i]]) i++;

    if (i >= slen) {
        strbuf_free(cur);
        return arr;
    }

    bool pending_empty = false;

    while (i < slen) {
        unsigned char c = (unsigned char)s[i];

        if (!ifs_map[c]) {
            // non-IFS char: accumulate into field
            strbuf_append_char(cur, (char)c);
            pending_empty = false;
            i++;
        } else if (ifs_ws_map[c]) {
            // IFS whitespace: skip run, check if non-ws IFS follows
            while (i < slen && ifs_ws_map[(unsigned char)s[i]]) i++;

            if (i >= slen) break; // trailing IFS ws: stop

            if (ifs_map[(unsigned char)s[i]] && !ifs_ws_map[(unsigned char)s[i]]) {
                // non-ws IFS follows: combined separator
                Item word = (Item){.item = s2it(heap_create_name(cur->str, (int)cur->length))};
                bash_array_append(arr, word);
                strbuf_reset(cur);
                pending_empty = true;
                i++; // consume non-ws IFS
                while (i < slen && ifs_ws_map[(unsigned char)s[i]]) i++;
            } else {
                // IFS ws alone: emit field if non-empty
                if (cur->length > 0) {
                    Item word = (Item){.item = s2it(heap_create_name(cur->str, (int)cur->length))};
                    bash_array_append(arr, word);
                    strbuf_reset(cur);
                    pending_empty = false;
                }
            }
        } else {
            // non-whitespace IFS: always a separator, emit field (even if empty)
            Item word = (Item){.item = s2it(heap_create_name(cur->str, (int)cur->length))};
            bash_array_append(arr, word);
            strbuf_reset(cur);
            pending_empty = true;
            i++;
            while (i < slen && ifs_ws_map[(unsigned char)s[i]]) i++;
        }
    }

    // emit final field only if non-empty (trailing empty from non-ws IFS is dropped)
    if (cur->length > 0) {
        Item word = (Item){.item = s2it(heap_create_name(cur->str, (int)cur->length))};
        bash_array_append(arr, word);
    }

    strbuf_free(cur);
    (void)pending_empty;
    return arr;
}

// ========================================================================
// Quote Removal
// ========================================================================

extern "C" Item bash_quote_remove(Item word) {
    const char* s = item_to_cstr(word);
    if (!s || !*s) return word;

    int len = (int)strlen(s);
    StrBuf* buf = strbuf_new();

    int i = 0;
    while (i < len) {
        char c = s[i];

        if (c == '\\' && i + 1 < len) {
            // backslash escape: emit the next char literally
            strbuf_append_char(buf, s[i + 1]);
            i += 2;
        } else if (c == '\'') {
            // single quote: copy everything until closing quote
            i++; // skip opening quote
            while (i < len && s[i] != '\'') {
                strbuf_append_char(buf, s[i]);
                i++;
            }
            if (i < len) i++; // skip closing quote
        } else if (c == '"') {
            // double quote: copy content, but handle backslash escapes
            // inside double quotes, only \$, \`, \\, \", \newline are special
            i++; // skip opening quote
            while (i < len && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < len) {
                    char next = s[i + 1];
                    if (next == '$' || next == '`' || next == '\\' || next == '"' || next == '\n') {
                        strbuf_append_char(buf, next);
                        i += 2;
                    } else {
                        // backslash is literal inside double quotes for other chars
                        strbuf_append_char(buf, '\\');
                        strbuf_append_char(buf, next);
                        i += 2;
                    }
                } else {
                    strbuf_append_char(buf, s[i]);
                    i++;
                }
            }
            if (i < len) i++; // skip closing quote
        } else {
            strbuf_append_char(buf, c);
            i++;
        }
    }

    Item result = (Item){.item = s2it(heap_create_name(buf->str, (int)buf->length))};
    strbuf_free(buf);
    return result;
}

// ========================================================================
// ANSI-C Escape Processing ($'...')
// ========================================================================

extern "C" Item bash_process_ansi_escapes(Item str) {
    const char* s = item_to_cstr(str);
    if (!s || !*s) return str;

    StrBuf* buf = strbuf_new();
    escape_append_bash_ansi(buf, s, strlen(s), ESCAPE_BASH_ANSI_RUNTIME);

    Item result = (Item){.item = s2it(heap_create_name(buf->str, (int)buf->length))};
    strbuf_free(buf);
    return result;
}

// ========================================================================
// Unified Expansion Pipeline
// ========================================================================

extern "C" Item bash_expand_word(Item word, int flags) {
    // Most expansion stages are handled at transpile time.
    // This runtime function handles dynamic contexts (eval, indirect expansion)
    // by applying IFS splitting and quote removal.

    Item result = word;

    // IFS word splitting (unless suppressed by double-quote context)
    if (!(flags & BASH_EXPAND_NO_SPLIT)) {
        // get current IFS
        Item ifs_name = (Item){.item = s2it(heap_create_name("IFS", 3))};
        Item ifs_val = bash_get_var(ifs_name);
        result = bash_word_split(result, ifs_val);

        // if splitting produced a single-element array, unwrap it
        List* list = it2list(result);
        if (list && list->length == 1) {
            result = list->items[0];
        } else if (list && list->length == 0) {
            result = (Item){.item = s2it(heap_create_name("", 0))};
        }
        // if multi-element, return the array as-is
    }

    return result;
}
