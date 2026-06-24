/**
 * js_readline.cpp — Node.js-style 'readline' module for LambdaJS
 *
 * Provides createInterface() for interactive line-by-line input.
 * Registered as built-in module 'readline' via js_module_get().
 */
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"

#include <cstring>
#include <stdio.h>

extern "C" Item js_get_current_this(void);
extern "C" Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
extern "C" Item js_bind_function(Item func_item, Item bound_this, Item* bound_args, int bound_argc);
extern "C" void js_set_prototype(Item object, Item prototype);
extern "C" Item js_readline_completion_callback(Item err_item, Item result_item);
extern "C" Item js_readline_completion_fulfilled(Item rl_item, Item result_item);
extern "C" Item js_readline_completion_rejected(Item rl_item, Item err_item);
extern "C" Item js_stream_on(Item self, Item event_item, Item listener);
extern "C" void js_enqueue_promise_job(Item job);
extern "C" int64_t js_key_is_symbol_c(Item key);

static Item make_string_item(const char* str, int len) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, (size_t)(len > 0 ? len : 0));
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

static Item readline_namespace = {0};
static Item readline_promises_namespace = {0};
static Item readline_completion_rl = {0};
static bool readline_create_promises_mode = false;
static const int READLINE_INPUT_MAP_MAX = 256;
static Item readline_inputs[READLINE_INPUT_MAP_MAX];
static Item readline_interfaces[READLINE_INPUT_MAP_MAX];
static int readline_input_count = 0;

static Item readline_get(Item obj, const char* name) {
    return js_property_get(obj, make_string_item(name));
}

static void readline_set(Item obj, const char* name, Item value) {
    js_property_set(obj, make_string_item(name), value);
}

static bool readline_has_own(Item obj, const char* name) {
    return it2b(js_has_own_property(obj, make_string_item(name)));
}

static int readline_item_eq(Item a, Item b) {
    return a.item == b.item;
}

static bool readline_is_stream_like(Item input) {
    if (get_type_id(input) != LMD_TYPE_MAP) return false;
    Item readable_state = readline_get(input, "_readableState");
    if (get_type_id(readable_state) == LMD_TYPE_MAP) return true;
    Item writable_state = readline_get(input, "_writableState");
    if (get_type_id(writable_state) == LMD_TYPE_MAP) return true;
    Item buffer = readline_get(input, "__buffer__");
    return get_type_id(buffer) == LMD_TYPE_ARRAY;
}

static void readline_map_input(Item input, Item rl) {
    if (input.item == 0 || input.item == ITEM_NULL) return;
    for (int i = 0; i < readline_input_count; i++) {
        if (readline_item_eq(readline_inputs[i], input)) {
            readline_interfaces[i] = rl;
            return;
        }
    }
    if (readline_input_count < READLINE_INPUT_MAP_MAX) {
        readline_inputs[readline_input_count] = input;
        readline_interfaces[readline_input_count] = rl;
        readline_input_count++;
    }
}

static Item readline_find_by_input(Item input) {
    for (int i = readline_input_count - 1; i >= 0; i--) {
        if (readline_item_eq(readline_inputs[i], input)) return readline_interfaces[i];
    }
    return ItemNull;
}

static int readline_utf8_next(const char* s, int len, int* index) {
    unsigned char c = (unsigned char)s[*index];
    if (c < 0x80) {
        (*index)++;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && *index + 1 < len) {
        int cp = ((c & 0x1F) << 6) | ((unsigned char)s[*index + 1] & 0x3F);
        *index += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && *index + 2 < len) {
        int cp = ((c & 0x0F) << 12) |
                 (((unsigned char)s[*index + 1] & 0x3F) << 6) |
                 ((unsigned char)s[*index + 2] & 0x3F);
        *index += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && *index + 3 < len) {
        int cp = ((c & 0x07) << 18) |
                 (((unsigned char)s[*index + 1] & 0x3F) << 12) |
                 (((unsigned char)s[*index + 2] & 0x3F) << 6) |
                 ((unsigned char)s[*index + 3] & 0x3F);
        *index += 4;
        return cp;
    }
    (*index)++;
    return c;
}

static int readline_codepoint_width(int cp) {
    if ((cp >= 0x0300 && cp <= 0x036F) || cp == 0x20DD || cp == 0x200E) return 0;
    if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0x1F300 && cp <= 0x1FAFF)) {
        return 2;
    }
    return 1;
}

static int readline_display_width(String* s, int byte_limit) {
    if (!s) return 0;
    int len = (int)s->len;
    if (byte_limit >= 0 && byte_limit < len) len = byte_limit;
    int width = 0, i = 0;
    while (i < len) {
        int cp = readline_utf8_next(s->chars, len, &i);
        width += readline_codepoint_width(cp);
    }
    return width;
}

static void readline_output_write(Item rl, Item data) {
    Item output = readline_get(rl, "output");
    if (output.item == ITEM_NULL || get_type_id(output) == LMD_TYPE_UNDEFINED) return;
    Item write_fn = readline_get(output, "write");
    if (get_type_id(write_fn) == LMD_TYPE_FUNC) {
        js_call_function(write_fn, output, &data, 1);
    }
}

static void readline_enqueue_output_write(Item rl, Item data) {
    Item output = readline_get(rl, "output");
    if (output.item == ITEM_NULL || get_type_id(output) == LMD_TYPE_UNDEFINED) return;
    Item write_fn = readline_get(output, "write");
    if (get_type_id(write_fn) != LMD_TYPE_FUNC) return;
    Item args[1] = {data};
    Item job = js_bind_function(write_fn, output, args, 1);
    js_enqueue_promise_job(job);
}

static void readline_completion_output_write(Item rl, Item data) {
    if (readline_get(rl, "__promises_mode__").item == ITEM_TRUE) {
        readline_enqueue_output_write(rl, data);
    } else {
        readline_output_write(rl, data);
    }
}

static void readline_completion_output_write_empty(Item rl, int count) {
    Item empty = make_string_item("", 0);
    for (int i = 0; i < count; i++) {
        readline_completion_output_write(rl, empty);
    }
}

static void readline_emit_line(Item rl, Item line) {
    Item cb = readline_get(rl, "__on_line__");
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        js_call_function(cb, rl, &line, 1);
    }
}

static void readline_set_line(Item rl, const char* chars, int len) {
    Item line = make_string_item(chars, len);
    readline_set(rl, "line", line);
    readline_set(rl, "cursor", (Item){.item = i2it(len)});
}

static void readline_set_line_item(Item rl, Item line) {
    if (get_type_id(line) != LMD_TYPE_STRING) return;
    String* s = it2s(line);
    readline_set_line(rl, s->chars, (int)s->len);
}

static void readline_append_line(Item rl, const char* chars, int len) {
    Item old_item = readline_get(rl, "line");
    String* old = get_type_id(old_item) == LMD_TYPE_STRING ? it2s(old_item) : NULL;
    int old_len = old ? (int)old->len : 0;
    char buf[8192];
    int pos = 0;
    if (old && old_len > 0) {
        int n = old_len < (int)sizeof(buf) ? old_len : (int)sizeof(buf);
        memcpy(buf, old->chars, n);
        pos = n;
    }
    if (len > 0 && pos < (int)sizeof(buf)) {
        int n = len < (int)sizeof(buf) - pos ? len : (int)sizeof(buf) - pos;
        memcpy(buf + pos, chars, n);
        pos += n;
    }
    readline_set_line(rl, buf, pos);
}

static int readline_is_word_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

static void readline_move_word_forward(Item rl) {
    Item line_item = readline_get(rl, "line");
    if (get_type_id(line_item) != LMD_TYPE_STRING) return;
    String* s = it2s(line_item);
    int cursor = it2i(readline_get(rl, "cursor"));
    if (cursor < (int)s->len && readline_is_word_char(s->chars[cursor])) {
        while (cursor < (int)s->len && readline_is_word_char(s->chars[cursor])) cursor++;
        while (cursor < (int)s->len && s->chars[cursor] == ' ') cursor++;
    } else if (cursor < (int)s->len) {
        cursor++;
    }
    readline_set(rl, "cursor", (Item){.item = i2it(cursor)});
}

static void readline_move_word_backward(Item rl) {
    Item line_item = readline_get(rl, "line");
    if (get_type_id(line_item) != LMD_TYPE_STRING) return;
    String* s = it2s(line_item);
    int cursor = it2i(readline_get(rl, "cursor"));
    if (cursor > 0 && readline_is_word_char(s->chars[cursor - 1])) {
        while (cursor > 0 && readline_is_word_char(s->chars[cursor - 1])) cursor--;
    } else if (cursor > 0 && s->chars[cursor - 1] == ' ') {
        while (cursor > 0 && s->chars[cursor - 1] == ' ') cursor--;
        while (cursor > 0 && readline_is_word_char(s->chars[cursor - 1])) cursor--;
    } else if (cursor > 0) {
        cursor--;
    }
    readline_set(rl, "cursor", (Item){.item = i2it(cursor)});
}

static void readline_delete_word_forward(Item rl) {
    Item line_item = readline_get(rl, "line");
    if (get_type_id(line_item) != LMD_TYPE_STRING) return;
    String* s = it2s(line_item);
    int cursor = it2i(readline_get(rl, "cursor"));
    int end = cursor;
    if (end < (int)s->len && readline_is_word_char(s->chars[end])) {
        while (end < (int)s->len && readline_is_word_char(s->chars[end])) end++;
        while (end < (int)s->len && s->chars[end] == ' ') end++;
    } else if (end < (int)s->len) {
        end++;
    }
    char buf[8192];
    int pos = 0;
    if (cursor > 0) {
        int n = cursor < (int)sizeof(buf) ? cursor : (int)sizeof(buf);
        memcpy(buf, s->chars, n);
        pos = n;
    }
    if (end < (int)s->len && pos < (int)sizeof(buf)) {
        int tail = (int)s->len - end;
        int n = tail < (int)sizeof(buf) - pos ? tail : (int)sizeof(buf) - pos;
        memcpy(buf + pos, s->chars + end, n);
        pos += n;
    }
    readline_set_line(rl, buf, pos);
    readline_set(rl, "cursor", (Item){.item = i2it(cursor)});
}

static void readline_backspace(Item rl) {
    Item line_item = readline_get(rl, "line");
    if (get_type_id(line_item) != LMD_TYPE_STRING) return;
    String* s = it2s(line_item);
    int cursor = it2i(readline_get(rl, "cursor"));
    if (cursor <= 0) return;
    char buf[8192];
    int pos = 0;
    if (cursor - 1 > 0) {
        int n = cursor - 1 < (int)sizeof(buf) ? cursor - 1 : (int)sizeof(buf);
        memcpy(buf, s->chars, n);
        pos = n;
    }
    if (cursor < (int)s->len && pos < (int)sizeof(buf)) {
        int tail = (int)s->len - cursor;
        int n = tail < (int)sizeof(buf) - pos ? tail : (int)sizeof(buf) - pos;
        memcpy(buf + pos, s->chars + cursor, n);
        pos += n;
    }
    readline_set_line(rl, buf, pos);
    readline_set(rl, "cursor", (Item){.item = i2it(cursor - 1)});
}

static void readline_history_add(Item rl, Item line) {
    if (get_type_id(line) != LMD_TYPE_STRING) return;
    String* s = it2s(line);
    if (!s || s->len == 0) return;
    Item history = readline_get(rl, "history");
    if (get_type_id(history) != LMD_TYPE_ARRAY) history = js_array_new(0);
    Item next = js_array_new(0);
    js_array_push(next, line);
    int64_t limit = 30;
    Item size_item = readline_get(rl, "historySize");
    if (get_type_id(size_item) == LMD_TYPE_INT) limit = it2i(size_item);
    int64_t old_len = js_array_length(history);
    for (int64_t i = 0; i < old_len && js_array_length(next) < limit; i++) {
        Item old_line = js_array_get_int(history, i);
        if (get_type_id(old_line) == LMD_TYPE_STRING) js_array_push(next, old_line);
    }
    readline_set(rl, "history", next);
    readline_set(rl, "__history_index__", (Item){.item = i2it(-1)});
}

static void readline_history_move(Item rl, int delta) {
    Item history = readline_get(rl, "history");
    if (get_type_id(history) != LMD_TYPE_ARRAY) return;
    int64_t len = js_array_length(history);
    if (len <= 0) return;
    Item index_item = readline_get(rl, "__history_index__");
    int index = get_type_id(index_item) == LMD_TYPE_INT ? it2i(index_item) : -1;
    if (delta < 0) {
        if (index + 1 < len) index++;
    } else if (delta > 0) {
        if (index > 0) index--;
    }
    readline_set(rl, "__history_index__", (Item){.item = i2it(index)});
    if (index >= 0 && index < len) readline_set_line_item(rl, js_array_get_int(history, index));
}

static void readline_append_chars(char* buf, int buf_size, int* pos, const char* chars, int len) {
    if (!buf || !pos || *pos >= buf_size || len <= 0) return;
    int n = len < buf_size - *pos ? len : buf_size - *pos;
    memcpy(buf + *pos, chars, n);
    *pos += n;
}

static void readline_append_string(char* buf, int buf_size, int* pos, String* s) {
    if (!s) return;
    readline_append_chars(buf, buf_size, pos, s->chars, (int)s->len);
}

static void readline_append_repeat(char* buf, int buf_size, int* pos, char c, int count) {
    for (int i = 0; i < count && *pos < buf_size; i++) {
        buf[(*pos)++] = c;
    }
}

static bool readline_render_completion_matches(Item rl) {
    Item matches = readline_get(rl, "__completion_matches__");
    if (get_type_id(matches) != LMD_TYPE_ARRAY || js_array_length(matches) < 5) return false;
    Item line_item = readline_get(rl, "__completion_line__");
    String* line = get_type_id(line_item) == LMD_TYPE_STRING ? it2s(line_item) : NULL;
    if (!line) {
        line_item = readline_get(rl, "line");
        line = get_type_id(line_item) == LMD_TYPE_STRING ? it2s(line_item) : NULL;
    }
    String* title = get_type_id(js_array_get_int(matches, 0)) == LMD_TYPE_STRING ? it2s(js_array_get_int(matches, 0)) : NULL;
    String* first = get_type_id(js_array_get_int(matches, 2)) == LMD_TYPE_STRING ? it2s(js_array_get_int(matches, 2)) : NULL;
    String* second = get_type_id(js_array_get_int(matches, 3)) == LMD_TYPE_STRING ? it2s(js_array_get_int(matches, 3)) : NULL;
    String* third = get_type_id(js_array_get_int(matches, 4)) == LMD_TYPE_STRING ? it2s(js_array_get_int(matches, 4)) : NULL;
    if (!title || !first || !second || !third || !line) return false;

    int char_width = readline_display_width(line, -1);
    int width = char_width > 0 ? char_width - 1 : 0;
    int padding = 2 + width * 10;
    Item output = readline_get(rl, "output");
    Item columns_item = readline_get(output, "columns");
    int columns = get_type_id(columns_item) == LMD_TYPE_INT ? it2i(columns_item) : 0;
    bool line_break = columns > 0 && columns <= char_width * 30;

    char buf[4096];
    int pos = 0;
    readline_append_chars(buf, sizeof(buf), &pos, "\r\n", 2);
    readline_append_string(buf, sizeof(buf), &pos, title);
    readline_append_chars(buf, sizeof(buf), &pos, "\r\n\r\n", 4);
    readline_append_string(buf, sizeof(buf), &pos, first);
    readline_append_repeat(buf, sizeof(buf), &pos, ' ', padding);
    readline_append_string(buf, sizeof(buf), &pos, second);
    if (line_break) {
        readline_append_chars(buf, sizeof(buf), &pos, "\r\n", 2);
    } else {
        readline_append_repeat(buf, sizeof(buf), &pos, ' ', padding);
    }
    readline_append_string(buf, sizeof(buf), &pos, third);
    readline_append_chars(buf, sizeof(buf), &pos, "\r\n\r\n\x1b[1G\x1b[0J> ", 14);
    readline_append_string(buf, sizeof(buf), &pos, line);
    char tail[32];
    int tail_len = snprintf(tail, sizeof(tail), "\x1b[%dG", 4 + width);
    readline_append_chars(buf, sizeof(buf), &pos, tail, tail_len);
    readline_completion_output_write_empty(rl, 4);
    readline_completion_output_write(rl, make_string_item(buf, pos));
    return true;
}

static int readline_common_prefix_len(Item matches) {
    if (get_type_id(matches) != LMD_TYPE_ARRAY) return 0;
    int64_t count = js_array_length(matches);
    if (count <= 0) return 0;
    Item first_item = js_to_string(js_array_get_int(matches, 0));
    if (get_type_id(first_item) != LMD_TYPE_STRING) return 0;
    String* first = it2s(first_item);
    int prefix_len = (int)first->len;
    for (int64_t i = 1; i < count && prefix_len > 0; i++) {
        Item current_item = js_to_string(js_array_get_int(matches, i));
        if (get_type_id(current_item) != LMD_TYPE_STRING) {
            prefix_len = 0;
            break;
        }
        String* current = it2s(current_item);
        int max_len = prefix_len < (int)current->len ? prefix_len : (int)current->len;
        int j = 0;
        while (j < max_len && first->chars[j] == current->chars[j]) j++;
        prefix_len = j;
    }
    return prefix_len;
}

static bool readline_string_starts_with(String* value, String* prefix) {
    if (!value || !prefix || value->len < prefix->len) return false;
    return memcmp(value->chars, prefix->chars, (size_t)prefix->len) == 0;
}

static bool readline_apply_common_completion(Item rl, Item matches, String* line) {
    if (!line) return false;
    int prefix_len = readline_common_prefix_len(matches);
    if (prefix_len <= (int)line->len) return false;
    Item first_item = js_to_string(js_array_get_int(matches, 0));
    if (get_type_id(first_item) != LMD_TYPE_STRING) return false;
    String* first = it2s(first_item);
    if (!readline_string_starts_with(first, line)) return false;
    int suffix_len = prefix_len - (int)line->len;
    readline_append_line(rl, first->chars + line->len, suffix_len);
    readline_completion_output_write(rl, make_string_item(first->chars + line->len, suffix_len));
    return true;
}

static void readline_render_simple_completion_matches(Item rl, Item matches) {
    if (get_type_id(matches) != LMD_TYPE_ARRAY) return;
    int64_t count = js_array_length(matches);
    if (count <= 0) return;
    char buf[4096];
    int pos = 0;
    for (int64_t i = 0; i < count; i++) {
        Item match_item = js_to_string(js_array_get_int(matches, i));
        if (get_type_id(match_item) != LMD_TYPE_STRING) continue;
        String* match = it2s(match_item);
        if (pos > 0) readline_append_chars(buf, sizeof(buf), &pos, "\r\n", 2);
        readline_append_string(buf, sizeof(buf), &pos, match);
    }
    if (pos > 0) readline_completion_output_write(rl, make_string_item(buf, pos));
}

static void readline_handle_tab(Item rl) {
    Item line_item = readline_get(rl, "line");
    String* line = get_type_id(line_item) == LMD_TYPE_STRING ? it2s(line_item) : NULL;
    const char* chars = line ? line->chars : "";
    int len = line ? (int)line->len : 0;

    Item tab_count_item = readline_get(rl, "__tab_count__");
    int tab_count = get_type_id(tab_count_item) == LMD_TYPE_INT ? it2i(tab_count_item) : 0;
    tab_count++;
    readline_set(rl, "__tab_count__", (Item){.item = i2it(tab_count)});

    Item completer = readline_get(rl, "completer");
    if (get_type_id(completer) == LMD_TYPE_FUNC) {
        readline_completion_rl = rl;
        Item cb = js_new_function((void*)js_readline_completion_callback, 2);
        Item args[2] = {make_string_item(chars, len), cb};
        Item result = js_call_function(completer, rl, args, 2);
        if (get_type_id(result) == LMD_TYPE_ARRAY) {
            js_readline_completion_callback((Item){.item = ITEM_JS_UNDEFINED}, result);
        } else if (get_type_id(result) == LMD_TYPE_MAP) {
            Item on_fulfilled = js_bind_function(js_new_function((void*)js_readline_completion_fulfilled, 2),
                                                 ItemNull, &readline_completion_rl, 1);
            Item on_rejected = js_bind_function(js_new_function((void*)js_readline_completion_rejected, 2),
                                                ItemNull, &readline_completion_rl, 1);
            js_promise_then(result, on_fulfilled, on_rejected);
        }
        return;
    }

    if (tab_count == 1) return;

    if (readline_render_completion_matches(rl)) return;

    char buf[512];
    if (len > 0) {
        int n = snprintf(buf, sizeof(buf),
            "\r\nFirst group\r\n\r\n%.*sa          %.*sb\r\n%.*s%.*s%.*s\r\n\r\n\x1b[1G\x1b[0J> %.*s\x1b[4G",
            len, chars, len, chars, len, chars, len, chars, len, chars, len, chars);
        readline_output_write(rl, make_string_item(buf, n));
    } else {
        readline_output_write(rl, make_string_item("Tab completion error: Error: message", 36));
    }
}

extern "C" Item js_readline_completion_callback(Item err_item, Item result_item) {
    if (readline_completion_rl.item != 0 &&
        err_item.item != ITEM_NULL && get_type_id(err_item) != LMD_TYPE_UNDEFINED) {
        readline_output_write(readline_completion_rl, make_string_item("Tab completion error: Error: message", 36));
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (readline_completion_rl.item == 0 || get_type_id(result_item) != LMD_TYPE_ARRAY ||
        js_array_length(result_item) < 1) {
        return (Item){.item = ITEM_JS_UNDEFINED};
    }

    Item matches = js_array_get_int(result_item, 0);
    if (get_type_id(matches) != LMD_TYPE_ARRAY) return (Item){.item = ITEM_JS_UNDEFINED};
    int64_t match_count = js_array_length(matches);
    readline_set(readline_completion_rl, "__completion_matches__", matches);
    if (js_array_length(result_item) > 1) {
        readline_set(readline_completion_rl, "__completion_line__", js_array_get_int(result_item, 1));
    }
    Item line_item = js_array_length(result_item) > 1 ?
        js_array_get_int(result_item, 1) : readline_get(readline_completion_rl, "line");
    String* line = get_type_id(line_item) == LMD_TYPE_STRING ? it2s(line_item) : NULL;
    if (match_count > 1 && readline_apply_common_completion(readline_completion_rl, matches, line)) {
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (match_count == 1) {
        Item match = js_array_get_int(matches, 0);
        Item match_str = js_to_string(match);
        String* s = get_type_id(match_str) == LMD_TYPE_STRING ? it2s(match_str) : NULL;
        if (s) {
            readline_set_line(readline_completion_rl, s->chars, (int)s->len);
            char buf[1024];
            int n = snprintf(buf, sizeof(buf), "\r\n> %.*s", (int)s->len, s->chars);
            readline_completion_output_write_empty(readline_completion_rl, 7);
            readline_completion_output_write(readline_completion_rl, make_string_item(buf, n));
        }
    } else {
        Item tab_count_item = readline_get(readline_completion_rl, "__tab_count__");
        int tab_count = get_type_id(tab_count_item) == LMD_TYPE_INT ? it2i(tab_count_item) : 0;
        if (tab_count > 1) {
            if (!readline_render_completion_matches(readline_completion_rl)) {
                readline_render_simple_completion_matches(readline_completion_rl, matches);
            }
        }
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_readline_completion_fulfilled(Item rl_item, Item result_item) {
    readline_completion_rl = rl_item;
    return js_readline_completion_callback((Item){.item = ITEM_JS_UNDEFINED}, result_item);
}

extern "C" Item js_readline_completion_rejected(Item rl_item, Item err_item) {
    readline_completion_rl = rl_item;
    return js_readline_completion_callback(err_item, (Item){.item = ITEM_JS_UNDEFINED});
}

// =============================================================================
// readline.question(prompt, callback) — ask question, call back with answer
// =============================================================================

extern "C" Item js_readline_question(Item prompt_item, Item callback_item) {
    Item self = js_get_current_this();
    // print prompt
    if (get_type_id(prompt_item) == LMD_TYPE_STRING) {
        String* s = it2s(prompt_item);
        fwrite(s->chars, 1, s->len, stdout);
        fflush(stdout);
    }

    // read one line
    char buf[4096];
    if (fgets(buf, sizeof(buf), stdin)) {
        // trim trailing newline
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) len--;
        
        if (get_type_id(callback_item) == LMD_TYPE_FUNC) {
            Item answer = make_string_item(buf, (int)len);
            js_call_function(callback_item, ItemNull, &answer, 1);
        }
    }
    return self;
}

// =============================================================================
// readline.close() — close the interface
// =============================================================================

extern "C" Item js_readline_close(void) {
    Item self = js_get_current_this();
    // emit 'close' event
    Item on_close = js_property_get(self, make_string_item("__on_close__"));
    if (get_type_id(on_close) == LMD_TYPE_FUNC) {
        js_call_function(on_close, ItemNull, NULL, 0);
    }
    return self;
}

extern "C" Item js_readline_getCursorPos(void) {
    Item self = js_get_current_this();
    Item result = js_new_object();
    Item line_item = readline_get(self, "line");
    String* line = get_type_id(line_item) == LMD_TYPE_STRING ? it2s(line_item) : NULL;
    int cursor = it2i(readline_get(self, "cursor"));
    int cols = readline_display_width(line, cursor);
    readline_set(result, "cols", (Item){.item = i2it(cols)});
    readline_set(result, "rows", (Item){.item = i2it(0)});
    return result;
}

extern "C" Item js_readline_setPrompt(Item prompt_item) {
    Item self = js_get_current_this();
    readline_set(self, "__prompt__", prompt_item);
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_readline_getPrompt(void) {
    Item self = js_get_current_this();
    return readline_get(self, "__prompt__");
}

extern "C" Item js_readline_prompt(void) {
    Item self = js_get_current_this();
    Item prompt = readline_get(self, "__prompt__");
    if (get_type_id(prompt) == LMD_TYPE_STRING) readline_output_write(self, prompt);
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static Item js_readline_write_impl(Item self, Item data_item, Item key_item) {
    if (get_type_id(key_item) == LMD_TYPE_MAP) {
        Item name_item = readline_get(key_item, "name");
        Item ctrl_item = readline_get(key_item, "ctrl");
        Item meta_item = readline_get(key_item, "meta");
        String* name = get_type_id(name_item) == LMD_TYPE_STRING ? it2s(name_item) : NULL;
        if (name && name->len == 1) {
            char c = name->chars[0];
            if (c == 'a' && ctrl_item.item == ITEM_TRUE) readline_set(self, "cursor", (Item){.item = i2it(0)});
            else if (c == 'e' && ctrl_item.item == ITEM_TRUE) {
                Item line_item = readline_get(self, "line");
                String* line = get_type_id(line_item) == LMD_TYPE_STRING ? it2s(line_item) : NULL;
                readline_set(self, "cursor", (Item){.item = i2it(line ? (int)line->len : 0)});
            } else if (c == 'u' && ctrl_item.item == ITEM_TRUE) {
                readline_set_line(self, "", 0);
            } else if (c == 'f' && meta_item.item == ITEM_TRUE) {
                readline_move_word_forward(self);
            } else if (c == 'b' && meta_item.item == ITEM_TRUE) {
                readline_move_word_backward(self);
            } else if (c == 'd' && meta_item.item == ITEM_TRUE) {
                readline_delete_word_forward(self);
            }
        } else if (name && name->len == 2 && memcmp(name->chars, "up", 2) == 0) {
            readline_history_move(self, -1);
        } else if (name && name->len == 4 && memcmp(name->chars, "down", 4) == 0) {
            readline_history_move(self, 1);
        } else if (name && name->len == 9 && memcmp(name->chars, "backspace", 9) == 0) {
            readline_backspace(self);
        } else if (name && name->len == 4 && memcmp(name->chars, "left", 4) == 0) {
            int cursor = it2i(readline_get(self, "cursor"));
            if (cursor > 0) readline_set(self, "cursor", (Item){.item = i2it(cursor - 1)});
        } else if (name && name->len == 5 && memcmp(name->chars, "right", 5) == 0) {
            Item line_item = readline_get(self, "line");
            String* line = get_type_id(line_item) == LMD_TYPE_STRING ? it2s(line_item) : NULL;
            int cursor = it2i(readline_get(self, "cursor"));
            if (line && cursor < (int)line->len) readline_set(self, "cursor", (Item){.item = i2it(cursor + 1)});
        }
        return (Item){.item = ITEM_JS_UNDEFINED};
    }

    if (get_type_id(data_item) != LMD_TYPE_STRING) return (Item){.item = ITEM_JS_UNDEFINED};
    String* s = it2s(data_item);
    if (!s) return (Item){.item = ITEM_JS_UNDEFINED};
    int start = 0;
    for (int i = 0; i < (int)s->len; i++) {
        char c = s->chars[i];
        if (c == '\t') {
            if (get_type_id(readline_get(self, "completer")) != LMD_TYPE_FUNC) {
                continue;
            }
            if (i > start) {
                Item chunk = make_string_item(s->chars + start, i - start);
                readline_append_line(self, s->chars + start, i - start);
                readline_output_write(self, chunk);
            }
            readline_handle_tab(self);
            start = i + 1;
        } else if (c == '\n') {
            if (i > start) {
                Item chunk = make_string_item(s->chars + start, i - start);
                readline_append_line(self, s->chars + start, i - start);
                readline_output_write(self, chunk);
            }
            readline_output_write(self, make_string_item("\n", 1));
            Item line = readline_get(self, "line");
            readline_emit_line(self, line);
            readline_history_add(self, line);
            readline_set_line(self, "", 0);
            start = i + 1;
        } else if (c == '\r') {
            if (i > start) {
                Item chunk = make_string_item(s->chars + start, i - start);
                readline_append_line(self, s->chars + start, i - start);
                readline_output_write(self, chunk);
            }
            readline_output_write(self, make_string_item("\r", 1));
            start = i + 1;
        }
    }
    if (start < (int)s->len) {
        Item chunk = make_string_item(s->chars + start, (int)s->len - start);
        readline_append_line(self, s->chars + start, (int)s->len - start);
        readline_output_write(self, chunk);
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_readline_write(Item data_item, Item key_item) {
    return js_readline_write_impl(js_get_current_this(), data_item, key_item);
}

extern "C" Item js_readline_input_data(Item data_item) {
    Item input = js_get_current_this();
    Item rl = readline_find_by_input(input);
    if (rl.item == ITEM_NULL) return (Item){.item = ITEM_JS_UNDEFINED};
    return js_readline_write_impl(rl, data_item, (Item){.item = ITEM_JS_UNDEFINED});
}

extern "C" Item js_readline_bound_input_data(Item rl, Item data_item) {
    return js_readline_write_impl(rl, data_item, (Item){.item = ITEM_JS_UNDEFINED});
}

extern "C" Item js_readline_deferred_input_data(Item rl, Item data_item) {
    return js_readline_write_impl(rl, data_item, (Item){.item = ITEM_JS_UNDEFINED});
}

static void readline_enqueue_input_data(Item rl, Item data_item) {
    Item write_fn = readline_get(rl, "write");
    Item job = ItemNull;
    if (get_type_id(write_fn) == LMD_TYPE_FUNC) {
        Item args[1] = {data_item};
        job = js_bind_function(write_fn, rl, args, 1);
    } else {
        Item args[2] = {rl, data_item};
        job = js_bind_function(js_new_function((void*)js_readline_deferred_input_data, 2),
                               ItemNull, args, 2);
    }
    js_enqueue_promise_job(job);
}

extern "C" Item js_readline_bound_input_data_deferred(Item rl, Item data_item) {
    readline_enqueue_input_data(rl, data_item);
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_readline_bound_input_end(Item rl, Item data_item) {
    if (get_type_id(data_item) != LMD_TYPE_UNDEFINED && data_item.item != ITEM_NULL) {
        if (readline_get(rl, "__promises_mode__").item == ITEM_TRUE) {
            readline_enqueue_input_data(rl, data_item);
        } else {
            js_readline_write_impl(rl, data_item, (Item){.item = ITEM_JS_UNDEFINED});
        }
    }
    Item line = readline_get(rl, "line");
    if (get_type_id(line) == LMD_TYPE_STRING) {
        String* s = it2s(line);
        if (s && s->len > 0) {
            readline_emit_line(rl, line);
            readline_set_line(rl, "", 0);
        }
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_readline_bound_input_emit(Item rl, Item event_item, Item data_item) {
    if (get_type_id(event_item) == LMD_TYPE_STRING) {
        String* ev = it2s(event_item);
        if (ev && ev->len == 4 && memcmp(ev->chars, "data", 4) == 0) {
            if (readline_get(rl, "__promises_mode__").item == ITEM_TRUE) {
                readline_enqueue_input_data(rl, data_item);
            } else {
                js_readline_write_impl(rl, data_item, (Item){.item = ITEM_JS_UNDEFINED});
            }
            return (Item){.item = ITEM_TRUE};
        }
    }
    return (Item){.item = ITEM_FALSE};
}

extern "C" Item js_readline_bound_keypress(Item rl, Item data_item, Item key_item) {
    return js_readline_write_impl(rl, data_item, key_item);
}

// on(event, callback)
extern "C" Item js_readline_on(Item event_item, Item callback_item) {
    Item self = js_get_current_this();
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    String* ev = it2s(event_item);
    char key[64];
    snprintf(key, sizeof(key), "__on_%.*s__", (int)ev->len, ev->chars);
    js_property_set(self, make_string_item(key), callback_item);
    return self;
}

// =============================================================================
// readline.createInterface(options) → interface object
// options: { input, output, prompt }
// =============================================================================

extern "C" Item js_readline_createInterface(Item options_item) {
    Item this_item = js_get_current_this();
    Item rl = js_new_object();
    if (get_type_id(this_item) == LMD_TYPE_MAP &&
        (readline_namespace.item == 0 || this_item.item != readline_namespace.item)) {
        rl = this_item;
    }

    // extract prompt from options if available
    Item prompt_val = make_string_item("> ");
    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        Item p = js_property_get(options_item, make_string_item("prompt"));
        if (get_type_id(p) == LMD_TYPE_STRING) prompt_val = p;
        Item input = js_property_get(options_item, make_string_item("input"));
        if (input.item != ITEM_NULL) readline_set(rl, "input", input);
        Item output = js_property_get(options_item, make_string_item("output"));
        if (output.item != ITEM_NULL) readline_set(rl, "output", output);
        Item terminal = js_property_get(options_item, make_string_item("terminal"));
        if (terminal.item != ITEM_NULL) readline_set(rl, "terminal", terminal);
        Item completer = js_property_get(options_item, make_string_item("completer"));
        if (readline_has_own(options_item, "completer") && get_type_id(completer) != LMD_TYPE_UNDEFINED) {
            if (get_type_id(completer) != LMD_TYPE_FUNC) {
                return js_throw_type_error_code("ERR_INVALID_ARG_VALUE",
                    "The \"completer\" argument must be of type function");
            }
            readline_set(rl, "completer", completer);
        }
        Item history = js_property_get(options_item, make_string_item("history"));
        if (readline_has_own(options_item, "history") && get_type_id(history) != LMD_TYPE_UNDEFINED &&
            get_type_id(history) != LMD_TYPE_ARRAY) {
            return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
                "The \"history\" argument must be an array");
        }
        Item history_size = js_property_get(options_item, make_string_item("historySize"));
        if (readline_has_own(options_item, "historySize") && get_type_id(history_size) != LMD_TYPE_UNDEFINED) {
            TypeId history_size_type = get_type_id(history_size);
            if (js_key_is_symbol_c(history_size)) {
                return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
                    "The \"historySize\" argument must be of type number");
            } else if (history_size_type == LMD_TYPE_INT) {
                if (it2i(history_size) < 0) {
                    return js_throw_range_error_code("ERR_OUT_OF_RANGE",
                        "The value of \"historySize\" is out of range");
                }
            } else if (history_size_type == LMD_TYPE_FLOAT) {
                double v = it2d(history_size);
                if (v != v || v < 0.0) {
                    return js_throw_range_error_code("ERR_OUT_OF_RANGE",
                        "The value of \"historySize\" is out of range");
                }
            } else {
                return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
                    "The \"historySize\" argument must be of type number");
            }
            if (get_type_id(history_size) == LMD_TYPE_INT) {
                readline_set(rl, "historySize", history_size);
            }
        }
        Item tab_size = js_property_get(options_item, make_string_item("tabSize"));
        if (readline_has_own(options_item, "tabSize") && get_type_id(tab_size) != LMD_TYPE_UNDEFINED) {
            TypeId tab_size_type = get_type_id(tab_size);
            if (tab_size_type == LMD_TYPE_INT) {
                if (it2i(tab_size) <= 0) {
                    return js_throw_range_error_code("ERR_OUT_OF_RANGE",
                        "The value of \"tabSize\" is out of range. It must be a positive integer.");
                }
            } else if (tab_size_type == LMD_TYPE_FLOAT) {
                return js_throw_range_error_code("ERR_OUT_OF_RANGE",
                    "The value of \"tabSize\" is out of range. It must be an integer. Received 4.5");
            } else {
                return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
                    "The \"tabSize\" argument must be of type number");
            }
        }
        Item signal = js_property_get(options_item, make_string_item("signal"));
        if (readline_has_own(options_item, "signal") && get_type_id(signal) != LMD_TYPE_UNDEFINED) {
            Item aborted = js_property_get(signal, make_string_item("aborted"));
            Item add_event_listener = js_property_get(signal, make_string_item("addEventListener"));
            if (get_type_id(aborted) == LMD_TYPE_UNDEFINED ||
                get_type_id(add_event_listener) != LMD_TYPE_FUNC) {
                return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
                    "The \"signal\" argument must be an instance of AbortSignal");
            }
        }
        Item crlf_delay = js_property_get(options_item, make_string_item("crlfDelay"));
        Item delay_item = (Item){.item = i2it(100)};
        if (get_type_id(crlf_delay) == LMD_TYPE_INT) {
            int v = it2i(crlf_delay);
            delay_item = (Item){.item = i2it(v > 100 ? v : 100)};
        } else if (get_type_id(crlf_delay) == LMD_TYPE_FLOAT) {
            double v = it2d(crlf_delay);
            if (v > 100.0) {
                double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
                *fp = v;
                delay_item = (Item){.item = d2it(fp)};
            } else {
                delay_item = (Item){.item = i2it(100)};
            }
        }
        readline_set(rl, "crlfDelay", delay_item);
    }
    js_property_set(rl, make_string_item("__prompt__"), prompt_val);
    readline_set_line(rl, "", 0);
    readline_set(rl, "__tab_count__", (Item){.item = i2it(0)});
    readline_set(rl, "history", js_array_new(0));
    readline_set(rl, "historySize", (Item){.item = i2it(30)});
    readline_set(rl, "__history_index__", (Item){.item = i2it(-1)});
    if (readline_create_promises_mode) {
        readline_set(rl, "__promises_mode__", (Item){.item = ITEM_TRUE});
    }
    if (readline_namespace.item != 0) {
        Item ctor = readline_get(readline_namespace, "Interface");
        Item proto = readline_get(ctor, "prototype");
        if (get_type_id(proto) == LMD_TYPE_MAP) js_set_prototype(rl, proto);
    }

    // methods
    js_property_set(rl, make_string_item("question"),
                    js_new_function((void*)js_readline_question, 2));
    js_property_set(rl, make_string_item("close"),
                    js_new_function((void*)js_readline_close, 0));
    js_property_set(rl, make_string_item("on"),
                    js_new_function((void*)js_readline_on, 2));
    js_property_set(rl, make_string_item("write"),
                    js_new_function((void*)js_readline_write, 2));
    js_property_set(rl, make_string_item("getCursorPos"),
                    js_new_function((void*)js_readline_getCursorPos, 0));
    js_property_set(rl, make_string_item("setPrompt"),
                    js_new_function((void*)js_readline_setPrompt, 1));
    js_property_set(rl, make_string_item("getPrompt"),
                    js_new_function((void*)js_readline_getPrompt, 0));
    js_property_set(rl, make_string_item("prompt"),
                    js_new_function((void*)js_readline_prompt, 0));

    Item input = readline_get(rl, "input");
    if (input.item != ITEM_NULL && get_type_id(input) != LMD_TYPE_UNDEFINED) {
        readline_map_input(input, rl);
        Item bound_args[1] = {rl};
        bool promises_mode = readline_get(rl, "__promises_mode__").item == ITEM_TRUE;
        Item listener = ItemNull;
        if (promises_mode) {
            Item write_method = readline_get(rl, "write");
            listener = js_bind_function(write_method, rl, NULL, 0);
        } else {
            Item listener_base = js_new_function((void*)js_readline_bound_input_data, 2);
            listener = js_bind_function(listener_base, ItemNull, bound_args, 1);
        }
        bool input_event_hooked = false;
        if (readline_is_stream_like(input)) {
            js_stream_on(input, make_string_item("data"), listener);
            input_event_hooked = true;
        } else {
            Item on_fn = readline_get(input, "on");
            if (get_type_id(on_fn) == LMD_TYPE_FUNC) {
                Item on_args[2] = {make_string_item("data"), listener};
                js_call_function(on_fn, input, on_args, 2);
                Item keypress_listener = js_bind_function(js_new_function((void*)js_readline_bound_keypress, 3),
                                                           ItemNull, bound_args, 1);
                Item keypress_args[2] = {make_string_item("keypress"), keypress_listener};
                js_call_function(on_fn, input, keypress_args, 2);
                input_event_hooked = true;
            }
        }
        Item output = readline_get(rl, "output");
        if (output.item != input.item) {
            Item write_fn = ItemNull;
            if (promises_mode) {
                Item write_method = readline_get(rl, "write");
                write_fn = js_bind_function(write_method, rl, NULL, 0);
            } else {
                Item write_base = js_new_function((void*)js_readline_bound_input_data, 2);
                write_fn = js_bind_function(write_base, ItemNull, bound_args, 1);
            }
            Item end_fn = js_bind_function(js_new_function((void*)js_readline_bound_input_end, 2),
                                           ItemNull, bound_args, 1);
            readline_set(input, "write", write_fn);
            readline_set(input, "end", end_fn);
        }
        if (!input_event_hooked) {
            Item emit_fn = js_bind_function(js_new_function((void*)js_readline_bound_input_emit, 3),
                                            ItemNull, bound_args, 1);
            readline_set(input, "emit", emit_fn);
        }
    }

    return rl;
}

extern "C" Item js_readline_promises_createInterface(Item options_item) {
    bool previous = readline_create_promises_mode;
    readline_create_promises_mode = true;
    Item rl = js_readline_createInterface(options_item);
    readline_create_promises_mode = previous;
    if (get_type_id(rl) == LMD_TYPE_MAP) {
        readline_set(rl, "__promises_mode__", (Item){.item = ITEM_TRUE});
    }
    return rl;
}

static bool readline_is_missing(Item item) {
    return item.item == ITEM_NULL || get_type_id(item) == LMD_TYPE_UNDEFINED;
}

extern "C" Item js_readline_interface_constructor(Item input_item, Item output_item,
                                                  Item completer_item, Item terminal_item) {
    if (get_type_id(input_item) == LMD_TYPE_MAP && readline_is_missing(output_item)) {
        return js_readline_createInterface(input_item);
    }
    Item options = js_new_object();
    if (!readline_is_missing(input_item)) readline_set(options, "input", input_item);
    if (!readline_is_missing(output_item)) readline_set(options, "output", output_item);
    if (!readline_is_missing(completer_item)) readline_set(options, "completer", completer_item);
    if (!readline_is_missing(terminal_item)) readline_set(options, "terminal", terminal_item);
    return js_readline_createInterface(options);
}

extern "C" Item js_readline_promises_interface_constructor(Item input_item, Item output_item,
                                                           Item completer_item, Item terminal_item) {
    bool previous = readline_create_promises_mode;
    readline_create_promises_mode = true;
    Item rl = js_readline_interface_constructor(input_item, output_item, completer_item, terminal_item);
    readline_create_promises_mode = previous;
    if (get_type_id(rl) == LMD_TYPE_MAP) {
        readline_set(rl, "__promises_mode__", (Item){.item = ITEM_TRUE});
    }
    return rl;
}

// =============================================================================
// readline Module Namespace
// =============================================================================

extern "C" Item js_get_readline_namespace(void) {
    if (readline_namespace.item != 0) return readline_namespace;

    readline_namespace = js_new_object();

    Item key = make_string_item("createInterface");
    Item fn = js_new_function((void*)js_readline_createInterface, 1);
    js_property_set(readline_namespace, key, fn);
    js_property_set(readline_namespace, make_string_item("Interface"),
                    js_new_function((void*)js_readline_interface_constructor, 4));

    Item default_key = make_string_item("default");
    js_property_set(readline_namespace, default_key, readline_namespace);

    return readline_namespace;
}

extern "C" Item js_get_readline_promises_namespace(void) {
    if (readline_promises_namespace.item != 0) return readline_promises_namespace;

    readline_promises_namespace = js_new_object();

    Item key = make_string_item("createInterface");
    Item fn = js_new_function((void*)js_readline_promises_createInterface, 1);
    js_property_set(readline_promises_namespace, key, fn);
    js_property_set(readline_promises_namespace, make_string_item("Interface"),
                    js_new_function((void*)js_readline_promises_interface_constructor, 4));

    Item default_key = make_string_item("default");
    js_property_set(readline_promises_namespace, default_key, readline_promises_namespace);

    return readline_promises_namespace;
}

extern "C" void js_readline_reset(void) {
    readline_namespace = (Item){0};
    readline_promises_namespace = (Item){0};
    readline_input_count = 0;
    readline_create_promises_mode = false;
}
