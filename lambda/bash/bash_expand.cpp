// bash_expand.cpp — Word Expansion Engine (Phase A — Module 1)
//
// Implements IFS word splitting, quote removal, ANSI-C escape processing,
// and the unified expansion pipeline for dynamic contexts.

#include "bash_expand.h"
#include "bash_runtime.h"
#include "../transpiler.hpp"
#include "../../lib/strbuf.h"
#include "../../lib/log.h"

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

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

// parse up to max_digits hex digits from s, return char value
static int parse_hex(const char* s, int max_digits, int* consumed) {
    int val = 0;
    int i = 0;
    while (i < max_digits && s[i] && isxdigit((unsigned char)s[i])) {
        val = val * 16;
        char c = s[i];
        if (c >= '0' && c <= '9') val += c - '0';
        else if (c >= 'a' && c <= 'f') val += 10 + c - 'a';
        else if (c >= 'A' && c <= 'F') val += 10 + c - 'A';
        i++;
    }
    *consumed = i;
    return val;
}

// parse up to max_digits octal digits from s, return char value
static int parse_octal(const char* s, int max_digits, int* consumed) {
    int val = 0;
    int i = 0;
    while (i < max_digits && s[i] >= '0' && s[i] <= '7') {
        val = val * 8 + (s[i] - '0');
        i++;
    }
    *consumed = i;
    return val;
}

// encode a Unicode codepoint as UTF-8 into buf, returns number of bytes written
static int utf8_encode(int codepoint, char* buf) {
    if (codepoint < 0) return 0;
    if (codepoint < 0x80) {
        buf[0] = (char)codepoint;
        return 1;
    }
    if (codepoint < 0x800) {
        buf[0] = (char)(0xC0 | (codepoint >> 6));
        buf[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint < 0x10000) {
        buf[0] = (char)(0xE0 | (codepoint >> 12));
        buf[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    if (codepoint < 0x110000) {
        buf[0] = (char)(0xF0 | (codepoint >> 18));
        buf[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    return 0; // invalid codepoint
}

extern "C" Item bash_process_ansi_escapes(Item str) {
    const char* s = item_to_cstr(str);
    if (!s || !*s) return str;

    int len = (int)strlen(s);
    StrBuf* buf = strbuf_new();

    for (int i = 0; i < len; i++) {
        if (s[i] != '\\') {
            strbuf_append_char(buf, s[i]);
            continue;
        }

        i++; // skip backslash
        if (i >= len) {
            strbuf_append_char(buf, '\\');
            break;
        }

        switch (s[i]) {
            case 'a': strbuf_append_char(buf, '\a'); break;
            case 'b': strbuf_append_char(buf, '\b'); break;
            case 'e': case 'E': strbuf_append_char(buf, '\x1B'); break;
            case 'f': strbuf_append_char(buf, '\f'); break;
            case 'n': strbuf_append_char(buf, '\n'); break;
            case 'r': strbuf_append_char(buf, '\r'); break;
            case 't': strbuf_append_char(buf, '\t'); break;
            case 'v': strbuf_append_char(buf, '\v'); break;
            case '\\': strbuf_append_char(buf, '\\'); break;
            case '\'': strbuf_append_char(buf, '\''); break;
            case '"': strbuf_append_char(buf, '"'); break;
            case '?': strbuf_append_char(buf, '?'); break;

            case '0': {
                // \0NNN: octal (up to 3 digits after the 0)
                int consumed = 0;
                int val = parse_octal(s + i + 1, 3, &consumed);
                strbuf_append_char(buf, (char)(val & 0xFF));
                i += consumed;
                break;
            }

            case '1': case '2': case '3':
            case '4': case '5': case '6': case '7': {
                // \NNN: octal (up to 3 digits including the first)
                int consumed = 0;
                int val = parse_octal(s + i, 3, &consumed);
                strbuf_append_char(buf, (char)(val & 0xFF));
                i += consumed - 1; // -1 because loop will advance past first digit
                break;
            }

            case 'x': {
                // \xHH: hex (1 or 2 digits)
                int consumed = 0;
                int val = parse_hex(s + i + 1, 2, &consumed);
                if (consumed > 0) {
                    strbuf_append_char(buf, (char)(val & 0xFF));
                    i += consumed;
                } else {
                    strbuf_append_char(buf, '\\');
                    strbuf_append_char(buf, 'x');
                }
                break;
            }

            case 'u': {
                // \uHHHH: Unicode 4-digit hex
                int consumed = 0;
                int cp = parse_hex(s + i + 1, 4, &consumed);
                if (consumed > 0) {
                    char u8buf[4];
                    int u8len = utf8_encode(cp, u8buf);
                    strbuf_append_str_n(buf, u8buf, u8len);
                    i += consumed;
                } else {
                    strbuf_append_char(buf, '\\');
                    strbuf_append_char(buf, 'u');
                }
                break;
            }

            case 'U': {
                // \UHHHHHHHH: Unicode 8-digit hex
                int consumed = 0;
                int cp = parse_hex(s + i + 1, 8, &consumed);
                if (consumed > 0) {
                    char u8buf[4];
                    int u8len = utf8_encode(cp, u8buf);
                    strbuf_append_str_n(buf, u8buf, u8len);
                    i += consumed;
                } else {
                    strbuf_append_char(buf, '\\');
                    strbuf_append_char(buf, 'U');
                }
                break;
            }

            case 'c': {
                // \cX: control character (X & 0x1F)
                if (i + 1 < len) {
                    i++;
                    strbuf_append_char(buf, (char)(s[i] & 0x1F));
                } else {
                    strbuf_append_char(buf, '\\');
                    strbuf_append_char(buf, 'c');
                }
                break;
            }

            default:
                // unknown escape: keep backslash + char
                strbuf_append_char(buf, '\\');
                strbuf_append_char(buf, s[i]);
                break;
        }
    }

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
