/**
 * js_readline.cpp — Node.js-style 'readline' module for LambdaJS
 *
 * Provides createInterface() for interactive line-by-line input.
 * Registered as built-in module 'readline' via js_module_get().
 */
#include "js_runtime.h"
#include "js_typed_array.h"
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
extern "C" Item js_readline_completion_callback_bound(Item rl_item, Item tab_count_item, Item err_item, Item result_item);
extern "C" Item js_readline_completion_fulfilled_bound(Item rl_item, Item tab_count_item, Item result_item);
extern "C" Item js_readline_completion_rejected_bound(Item rl_item, Item tab_count_item, Item err_item);
extern "C" Item js_stream_on(Item self, Item event_item, Item listener);
extern "C" void js_enqueue_promise_job(Item job);
extern "C" int64_t js_key_is_symbol_c(Item key);
extern "C" Item js_ee_emit(Item emitter, Item event_name, Item args_rest);
extern "C" int js_check_exception(void);
extern "C" Item js_promise_with_resolvers(void);
extern "C" Item js_throw_error_with_code(const char* code, const char* message);
extern "C" Item js_new_error_with_name(Item error_name, Item message);
extern "C" void js_set_function_name(Item fn_item, Item name_item);
extern "C" void js_stream_flush_data_now(Item self);
extern Item js_make_number(double d);

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

static int readline_utf8_expected_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static bool readline_utf8_has_continuation(const char* s, int start, int len, int count) {
    if (start + count > len) return false;
    for (int i = 1; i < count; i++) {
        if ((((unsigned char)s[start + i]) & 0xC0) != 0x80) return false;
    }
    return true;
}

static int readline_complete_utf8_prefix_len(const char* s, int len) {
    int i = 0;
    int complete = 0;
    while (i < len) {
        int need = readline_utf8_expected_len((unsigned char)s[i]);
        if (need > 1 && !readline_utf8_has_continuation(s, i, len, need)) break;
        i += need;
        complete = i;
    }
    return complete;
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

static Item readline_decode_input_text(Item rl, Item data_item) {
    if (get_type_id(data_item) == LMD_TYPE_STRING) return data_item;
    if (!js_is_typed_array(data_item) && get_type_id(data_item) != LMD_TYPE_ARRAY) {
        return (Item){.item = ITEM_JS_UNDEFINED};
    }

    char buf[8192];
    int pos = 0;
    Item pending = readline_get(rl, "__utf8_pending__");
    Item input = readline_get(rl, "input");
    if (get_type_id(pending) != LMD_TYPE_STRING && get_type_id(input) == LMD_TYPE_MAP) {
        pending = readline_get(input, "__readline_utf8_pending__");
    }
    if (get_type_id(pending) == LMD_TYPE_STRING) {
        String* s = it2s(pending);
        int n = s && s->len < sizeof(buf) ? (int)s->len : (int)sizeof(buf);
        if (s && n > 0) {
            memcpy(buf, s->chars, n);
            pos = n;
        }
    }

    Item length_item = js_property_get(data_item, make_string_item("length"));
    int64_t len = get_type_id(length_item) == LMD_TYPE_INT ? it2i(length_item) : js_array_length(data_item);
    for (int64_t i = 0; i < len && pos < (int)sizeof(buf); i++) {
        Item byte_item = js_array_get_int(data_item, i);
        if (get_type_id(byte_item) != LMD_TYPE_INT) {
            byte_item = js_property_get(data_item, (Item){.item = i2it(i)});
        }
        if (get_type_id(byte_item) != LMD_TYPE_INT) continue;
        buf[pos++] = (char)(it2i(byte_item) & 0xff);
    }

    int complete = readline_complete_utf8_prefix_len(buf, pos);
    if (complete < pos) {
        Item pending_bytes = make_string_item(buf + complete, pos - complete);
        readline_set(rl, "__utf8_pending__", pending_bytes);
        if (get_type_id(input) == LMD_TYPE_MAP) {
            readline_set(input, "__readline_utf8_pending__", pending_bytes);
        }
    } else {
        readline_set(rl, "__utf8_pending__", (Item){.item = ITEM_JS_UNDEFINED});
        if (get_type_id(input) == LMD_TYPE_MAP) {
            readline_set(input, "__readline_utf8_pending__", (Item){.item = ITEM_JS_UNDEFINED});
        }
    }
    if (complete <= 0) return make_string_item("", 0);
    return make_string_item(buf, complete);
}

static void readline_display_position_update(String* s, int byte_limit, int columns,
                                             int tab_size, int* rows, int* line_cols) {
    if (!s || !rows || !line_cols) return;
    int len = (int)s->len;
    if (byte_limit >= 0 && byte_limit < len) len = byte_limit;
    int i = 0;
    while (i < len) {
        char c = s->chars[i];
        if (c == '\n') {
            if (columns > 0 && *line_cols > 0) {
                *rows += (*line_cols - 1) / columns;
            }
            (*rows)++;
            *line_cols = 0;
            i++;
            continue;
        }
        if (c == '\t') {
            int size = tab_size > 0 ? tab_size : 8;
            int add = size - (*line_cols % size);
            *line_cols += add;
            i++;
            continue;
        }
        int before = i;
        int cp = readline_utf8_next(s->chars, len, &i);
        if (i <= before) i = before + 1;
        *line_cols += readline_codepoint_width(cp);
    }
}

static void readline_display_position_finish(int rows, int line_cols, int columns,
                                             int* out_rows, int* out_cols) {
    int cols = line_cols;
    if (columns > 0) {
        rows += line_cols / columns;
        cols = line_cols % columns;
    }
    if (out_rows) *out_rows = rows;
    if (out_cols) *out_cols = cols;
}

static int readline_prev_char_index(String* s, int cursor) {
    if (!s || cursor <= 0) return 0;
    if (cursor > (int)s->len) cursor = (int)s->len;
    int i = cursor - 1;
    while (i > 0 && (((unsigned char)s->chars[i] & 0xC0) == 0x80)) i--;
    return i;
}

static int readline_next_char_index(String* s, int cursor) {
    if (!s) return 0;
    if (cursor < 0) cursor = 0;
    if (cursor >= (int)s->len) return (int)s->len;
    int i = cursor;
    readline_utf8_next(s->chars, (int)s->len, &i);
    return i;
}

static void readline_output_write(Item rl, Item data) {
    Item output = readline_get(rl, "output");
    if (output.item == ITEM_NULL || get_type_id(output) == LMD_TYPE_UNDEFINED) return;
    Item write_fn = readline_get(output, "write");
    if (get_type_id(write_fn) == LMD_TYPE_FUNC) {
        js_call_function(write_fn, output, &data, 1);
        js_stream_flush_data_now(output);
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
    if (readline_get(rl, "__promises_mode__").item == ITEM_TRUE &&
        readline_get(rl, "__completion_direct_output__").item != ITEM_TRUE) {
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

static Item readline_abort_error_with_cause(Item cause) {
    Item err = js_new_error_with_name(make_string_item("AbortError"),
                                      make_string_item("The operation was aborted"));
    if (cause.item != 0 && get_type_id(cause) != LMD_TYPE_UNDEFINED) {
        Item cause_key = make_string_item("cause");
        readline_set(err, "cause", cause);
        js_mark_non_enumerable(err, cause_key);
    }
    return err;
}

static Item readline_abort_error_from_signal(Item signal) {
    Item cause = (Item){.item = ITEM_JS_UNDEFINED};
    if (get_type_id(signal) == LMD_TYPE_MAP) {
        cause = readline_get(signal, "reason");
    }
    return readline_abort_error_with_cause(cause);
}

static Item readline_error_with_code(const char* code, const char* message) {
    Item err = js_new_error_with_name(make_string_item("Error"), make_string_item(message));
    readline_set(err, "code", make_string_item(code));
    return err;
}

static void readline_reject_later(Item reject, Item err) {
    if (get_type_id(reject) != LMD_TYPE_FUNC) return;
    Item args[1] = {err};
    Item job = js_bind_function(reject, ItemNull, args, 1);
    js_enqueue_promise_job(job);
}

static Item readline_rejected_promise(Item err) {
    Item capability = js_promise_with_resolvers();
    Item reject = readline_get(capability, "reject");
    readline_reject_later(reject, err);
    return readline_get(capability, "promise");
}

static void readline_clear_question(Item rl) {
    Item signal = readline_get(rl, "__question_signal__");
    Item listener = readline_get(rl, "__question_abort_listener__");
    if (get_type_id(signal) == LMD_TYPE_MAP && get_type_id(listener) == LMD_TYPE_FUNC) {
        Item remove_fn = readline_get(signal, "removeEventListener");
        if (get_type_id(remove_fn) == LMD_TYPE_FUNC) {
            Item args[2] = {make_string_item("abort"), listener};
            js_call_function(remove_fn, signal, args, 2);
        }
    }
    readline_set(rl, "__question_callback__", (Item){.item = ITEM_JS_UNDEFINED});
    readline_set(rl, "__question_resolve__", (Item){.item = ITEM_JS_UNDEFINED});
    readline_set(rl, "__question_reject__", (Item){.item = ITEM_JS_UNDEFINED});
    readline_set(rl, "__question_prompt__", (Item){.item = ITEM_JS_UNDEFINED});
    readline_set(rl, "__question_signal__", (Item){.item = ITEM_JS_UNDEFINED});
    readline_set(rl, "__question_abort_listener__", (Item){.item = ITEM_JS_UNDEFINED});
}

extern "C" Item js_readline_question_on_abort(Item rl_item) {
    Item reject = readline_get(rl_item, "__question_reject__");
    Item signal = readline_get(rl_item, "__question_signal__");
    readline_clear_question(rl_item);
    if (get_type_id(reject) == LMD_TYPE_FUNC) {
        Item err = readline_abort_error_from_signal(signal);
        js_call_function(reject, ItemNull, &err, 1);
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static bool readline_abort_pending_question(Item rl, bool close_interface) {
    bool pending = get_type_id(readline_get(rl, "__question_callback__")) == LMD_TYPE_FUNC ||
                   get_type_id(readline_get(rl, "__question_resolve__")) == LMD_TYPE_FUNC;
    if (!pending) return false;
    Item reject = readline_get(rl, "__question_reject__");
    Item signal = readline_get(rl, "__question_signal__");
    readline_clear_question(rl);
    if (get_type_id(reject) == LMD_TYPE_FUNC) {
        Item err = readline_abort_error_from_signal(signal);
        js_call_function(reject, ItemNull, &err, 1);
    }
    if (close_interface) {
        readline_set(rl, "closed", (Item){.item = ITEM_TRUE});
    }
    return true;
}

static bool readline_crlf_delay_is_infinite(Item rl) {
    Item delay = readline_get(rl, "crlfDelay");
    if (get_type_id(delay) != LMD_TYPE_FLOAT) return false;
    double v = it2d(delay);
    return v > 1.0e100;
}

static bool readline_register_question_signal(Item rl, Item signal) {
    if (get_type_id(signal) != LMD_TYPE_MAP) return true;
    Item aborted = readline_get(signal, "aborted");
    if (aborted.item == ITEM_TRUE) return false;
    Item add_fn = readline_get(signal, "addEventListener");
    if (get_type_id(add_fn) == LMD_TYPE_FUNC) {
        Item bound_arg = rl;
        Item listener = js_bind_function(js_new_function((void*)js_readline_question_on_abort, 1),
                                         ItemNull, &bound_arg, 1);
        js_set_function_name(listener, make_string_item("onAbort"));
        Item args[2] = {make_string_item("abort"), listener};
        js_call_function(add_fn, signal, args, 2);
        readline_set(rl, "__question_signal__", signal);
        readline_set(rl, "__question_abort_listener__", listener);
    }
    return true;
}

static void readline_emit_line(Item rl, Item line) {
    Item question_cb = readline_get(rl, "__question_callback__");
    if (get_type_id(question_cb) == LMD_TYPE_FUNC) {
        readline_clear_question(rl);
        js_call_function(question_cb, rl, &line, 1);
        return;
    }
    Item question_resolve = readline_get(rl, "__question_resolve__");
    if (get_type_id(question_resolve) == LMD_TYPE_FUNC) {
        readline_clear_question(rl);
        js_call_function(question_resolve, ItemNull, &line, 1);
        return;
    }
    Item cb = readline_get(rl, "__on_line__");
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        js_call_function(cb, rl, &line, 1);
    }
}

static void readline_emit_history(Item rl, Item history) {
    Item cb = readline_get(rl, "__on_history__");
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        js_call_function(cb, rl, &history, 1);
    }
}

static bool readline_string_items_equal(Item a, Item b) {
    if (get_type_id(a) != LMD_TYPE_STRING || get_type_id(b) != LMD_TYPE_STRING) return false;
    String* sa = it2s(a);
    String* sb = it2s(b);
    return sa && sb && sa->len == sb->len && memcmp(sa->chars, sb->chars, (size_t)sa->len) == 0;
}

static Item readline_history_entry_from_line(Item line) {
    if (get_type_id(line) != LMD_TYPE_STRING) return line;
    String* s = it2s(line);
    if (!s || s->len == 0) return line;
    bool multiline = false;
    for (size_t i = 0; i < s->len; i++) {
        if (s->chars[i] == '\n' || s->chars[i] == '\r') {
            multiline = true;
            break;
        }
    }
    if (!multiline) return line;

    char* out = (char*)mem_alloc(s->len + 1, MEM_CAT_JS_RUNTIME);
    int pos = 0;
    int end = (int)s->len;
    while (end >= 0) {
        int start = end;
        while (start > 0 && s->chars[start - 1] != '\n' && s->chars[start - 1] != '\r') {
            start--;
        }
        int seg_len = end - start;
        if (seg_len > 0) {
            if (pos > 0) out[pos++] = '\r';
            memcpy(out + pos, s->chars + start, (size_t)seg_len);
            pos += seg_len;
        }
        if (start == 0) break;
        end = start - 1;
        if (end > 0 && s->chars[end] == '\n' && s->chars[end - 1] == '\r') {
            end--;
        }
    }
    Item result = make_string_item(out, pos);
    mem_free(out);
    return result;
}

static bool readline_string_item_starts_with(Item value_item, Item prefix_item) {
    if (get_type_id(value_item) != LMD_TYPE_STRING || get_type_id(prefix_item) != LMD_TYPE_STRING) return false;
    String* value = it2s(value_item);
    String* prefix = it2s(prefix_item);
    return value && prefix && value->len >= prefix->len &&
           memcmp(value->chars, prefix->chars, (size_t)prefix->len) == 0;
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
    int cursor = it2i(readline_get(rl, "cursor"));
    if (cursor < 0) cursor = 0;
    if (cursor > old_len) cursor = old_len;
    char buf[8192];
    int pos = 0;
    if (old && cursor > 0) {
        int n = cursor < (int)sizeof(buf) ? cursor : (int)sizeof(buf);
        memcpy(buf, old->chars, n);
        pos = n;
    }
    if (len > 0 && pos < (int)sizeof(buf)) {
        int n = len < (int)sizeof(buf) - pos ? len : (int)sizeof(buf) - pos;
        memcpy(buf + pos, chars, n);
        pos += n;
    }
    if (old && cursor < old_len && pos < (int)sizeof(buf)) {
        int tail = old_len - cursor;
        int n = tail < (int)sizeof(buf) - pos ? tail : (int)sizeof(buf) - pos;
        memcpy(buf + pos, old->chars + cursor, n);
        pos += n;
    }
    readline_set_line(rl, buf, pos);
    readline_set(rl, "cursor", (Item){.item = i2it(cursor + len)});
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

static void readline_delete_word_backward(Item rl) {
    Item line_item = readline_get(rl, "line");
    if (get_type_id(line_item) != LMD_TYPE_STRING) return;
    String* s = it2s(line_item);
    int cursor = it2i(readline_get(rl, "cursor"));
    int start = cursor;
    while (start > 0 && s->chars[start - 1] == ' ') start--;
    while (start > 0 && readline_is_word_char(s->chars[start - 1])) start--;
    if (start == cursor) return;
    char buf[8192];
    int pos = 0;
    if (start > 0) {
        int n = start < (int)sizeof(buf) ? start : (int)sizeof(buf);
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
    readline_set(rl, "cursor", (Item){.item = i2it(start)});
}

static void readline_delete_range(Item rl, int start, int end) {
    Item line_item = readline_get(rl, "line");
    if (get_type_id(line_item) != LMD_TYPE_STRING) return;
    String* s = it2s(line_item);
    if (start < 0) start = 0;
    if (end > (int)s->len) end = (int)s->len;
    if (end <= start) return;
    char buf[8192];
    int pos = 0;
    if (start > 0) {
        int n = start < (int)sizeof(buf) ? start : (int)sizeof(buf);
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
    readline_set(rl, "cursor", (Item){.item = i2it(start)});
}

static Item readline_slice_line(Item rl, int start, int end) {
    Item line_item = readline_get(rl, "line");
    if (get_type_id(line_item) != LMD_TYPE_STRING) return make_string_item("", 0);
    String* s = it2s(line_item);
    if (start < 0) start = 0;
    if (end > (int)s->len) end = (int)s->len;
    if (end <= start) return make_string_item("", 0);
    return make_string_item(s->chars + start, end - start);
}

static void readline_store_kill(Item rl, Item kill) {
    Item current = readline_get(rl, "__kill_buffer__");
    if (get_type_id(current) == LMD_TYPE_STRING) {
        readline_set(rl, "__kill_prev__", current);
    }
    readline_set(rl, "__kill_buffer__", kill);
}

static void readline_stack_push(Item rl, const char* name, Item value) {
    Item stack = readline_get(rl, name);
    if (get_type_id(stack) != LMD_TYPE_ARRAY) {
        stack = js_array_new(0);
        readline_set(rl, name, stack);
    }
    js_array_push(stack, value);
}

static Item readline_stack_pop(Item rl, const char* name) {
    Item stack = readline_get(rl, name);
    if (get_type_id(stack) != LMD_TYPE_ARRAY) return (Item){.item = ITEM_JS_UNDEFINED};
    int64_t len = js_array_length(stack);
    if (len <= 0) return (Item){.item = ITEM_JS_UNDEFINED};
    Item value = js_array_get_int(stack, len - 1);
    Item next = js_array_new(0);
    for (int64_t i = 0; i < len - 1; i++) {
        js_array_push(next, js_array_get_int(stack, i));
    }
    readline_set(rl, name, next);
    return value;
}

static void readline_remember_undo(Item rl) {
    Item line = readline_get(rl, "line");
    if (get_type_id(line) == LMD_TYPE_STRING) {
        readline_stack_push(rl, "__undo_stack__", line);
        readline_set(rl, "__redo_stack__", js_array_new(0));
    }
}

static void readline_undo(Item rl) {
    Item previous = readline_stack_pop(rl, "__undo_stack__");
    if (get_type_id(previous) != LMD_TYPE_STRING) return;
    Item current = readline_get(rl, "line");
    if (get_type_id(current) == LMD_TYPE_STRING) {
        readline_stack_push(rl, "__redo_stack__", current);
    }
    readline_set_line_item(rl, previous);
}

static void readline_redo(Item rl) {
    Item next = readline_stack_pop(rl, "__redo_stack__");
    if (get_type_id(next) != LMD_TYPE_STRING) return;
    Item current = readline_get(rl, "line");
    if (get_type_id(current) == LMD_TYPE_STRING) {
        readline_stack_push(rl, "__undo_stack__", current);
    }
    readline_set_line_item(rl, next);
}

static void readline_backspace(Item rl) {
    Item line_item = readline_get(rl, "line");
    if (get_type_id(line_item) != LMD_TYPE_STRING) return;
    String* s = it2s(line_item);
    int cursor = it2i(readline_get(rl, "cursor"));
    if (cursor <= 0) return;
    int start = readline_prev_char_index(s, cursor);
    readline_delete_range(rl, start, cursor);
    readline_set(rl, "__history_index__", (Item){.item = i2it(-1)});
    readline_set(rl, "historyIndex", (Item){.item = i2it(-1)});
    readline_set(rl, "__history_search__", readline_get(rl, "line"));
}

static void readline_delete_right(Item rl) {
    Item line_item = readline_get(rl, "line");
    if (get_type_id(line_item) != LMD_TYPE_STRING) return;
    String* s = it2s(line_item);
    int cursor = it2i(readline_get(rl, "cursor"));
    int end = readline_next_char_index(s, cursor);
    readline_delete_range(rl, cursor, end);
}

static void readline_history_add(Item rl, Item line) {
    if (readline_get(rl, "terminal").item == ITEM_FALSE) return;
    if (get_type_id(line) != LMD_TYPE_STRING) return;
    String* s = it2s(line);
    if (!s || s->len == 0) return;
    Item entry = readline_history_entry_from_line(line);
    Item history = readline_get(rl, "history");
    if (get_type_id(history) != LMD_TYPE_ARRAY) history = js_array_new(0);
    Item next = js_array_new(0);
    js_array_push(next, entry);
    int64_t limit = 30;
    Item size_item = readline_get(rl, "historySize");
    if (get_type_id(size_item) == LMD_TYPE_INT) limit = it2i(size_item);
    if (limit <= 0) return;
    bool remove_duplicates = readline_get(rl, "removeHistoryDuplicates").item == ITEM_TRUE;
    int64_t old_len = js_array_length(history);
    for (int64_t i = 0; i < old_len && js_array_length(next) < limit; i++) {
        Item old_line = js_array_get_int(history, i);
        if (remove_duplicates && readline_string_items_equal(old_line, entry)) continue;
        if (get_type_id(old_line) == LMD_TYPE_STRING) js_array_push(next, old_line);
    }
    readline_set(rl, "history", next);
    readline_set(rl, "__history_index__", (Item){.item = i2it(-1)});
    readline_set(rl, "historyIndex", (Item){.item = i2it(-1)});
    readline_emit_history(rl, next);
}

static void readline_history_move(Item rl, int delta) {
    Item history = readline_get(rl, "history");
    if (get_type_id(history) != LMD_TYPE_ARRAY) return;
    int64_t len = js_array_length(history);
    if (len <= 0) return;
    Item index_item = readline_get(rl, "__history_index__");
    int index = get_type_id(index_item) == LMD_TYPE_INT ? it2i(index_item) : -1;
    Item search_item = readline_get(rl, "__history_search__");
    bool has_search = get_type_id(search_item) == LMD_TYPE_STRING;
    if (has_search && delta < 0) {
        int start = index < 0 ? 0 : index + 1;
        int found = -1;
        for (int i = start; i < len; i++) {
            if (readline_string_item_starts_with(js_array_get_int(history, i), search_item)) {
                found = i;
                break;
            }
        }
        if (found >= 0) {
            index = found;
            readline_set_line_item(rl, js_array_get_int(history, index));
        } else {
            index = (int)len;
            readline_set_line_item(rl, search_item);
        }
        readline_set(rl, "__history_index__", (Item){.item = i2it(index)});
        readline_set(rl, "historyIndex", (Item){.item = i2it(index)});
        return;
    }
    if (has_search && delta > 0 && index < 0) {
        readline_set(rl, "__history_index__", (Item){.item = i2it(-1)});
        readline_set(rl, "historyIndex", (Item){.item = i2it(-1)});
        return;
    }
    if (delta < 0) {
        if (index + 1 < len) index++;
        Item current_line = readline_get(rl, "line");
        while (index + 1 < len &&
               readline_string_items_equal(js_array_get_int(history, index), current_line)) {
            index++;
        }
    } else if (delta > 0) {
        if (index > 0) {
            index--;
        } else if (index == 0) {
            index = -1;
        }
    }
    readline_set(rl, "__history_index__", (Item){.item = i2it(index)});
    readline_set(rl, "historyIndex", (Item){.item = i2it(index)});
    if (index >= 0 && index < len) readline_set_line_item(rl, js_array_get_int(history, index));
}

static bool readline_emit_keypress(Item rl, char c) {
    Item input = readline_get(rl, "input");
    if (get_type_id(input) != LMD_TYPE_MAP) return true;
    Item ch = make_string_item(&c, 1);
    Item key = js_new_object();
    readline_set(key, "name", ch);
    Item args = js_array_new(0);
    js_array_push(args, ch);
    js_array_push(args, key);
    readline_set(rl, "__synth_keypress__", (Item){.item = ITEM_TRUE});
    js_ee_emit(input, make_string_item("keypress"), args);
    readline_set(rl, "__synth_keypress__", (Item){.item = ITEM_FALSE});
    return !js_check_exception();
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
        Item completion_args[2] = {rl, (Item){.item = i2it(tab_count)}};
        Item cb = js_bind_function(js_new_function((void*)js_readline_completion_callback_bound, 4),
                                   ItemNull, completion_args, 2);
        Item args[2] = {make_string_item(chars, len), cb};
        Item result = js_call_function(completer, rl, args, 2);
        if (get_type_id(result) == LMD_TYPE_ARRAY) {
            js_readline_completion_callback_bound(rl, completion_args[1],
                (Item){.item = ITEM_JS_UNDEFINED}, result);
        } else if (get_type_id(result) == LMD_TYPE_MAP) {
            Item on_fulfilled = js_bind_function(js_new_function((void*)js_readline_completion_fulfilled_bound, 3),
                                                 ItemNull, completion_args, 2);
            Item on_rejected = js_bind_function(js_new_function((void*)js_readline_completion_rejected_bound, 3),
                                                ItemNull, completion_args, 2);
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

static Item readline_completion_callback_impl(Item rl, Item err_item, Item result_item, int tab_count) {
    if (rl.item != 0 &&
        err_item.item != ITEM_NULL && get_type_id(err_item) != LMD_TYPE_UNDEFINED) {
        readline_output_write(rl, make_string_item("Tab completion error: Error: message", 36));
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (rl.item == 0 || get_type_id(result_item) != LMD_TYPE_ARRAY ||
        js_array_length(result_item) < 1) {
        return (Item){.item = ITEM_JS_UNDEFINED};
    }

    Item matches = js_array_get_int(result_item, 0);
    if (get_type_id(matches) != LMD_TYPE_ARRAY) return (Item){.item = ITEM_JS_UNDEFINED};
    int64_t match_count = js_array_length(matches);
    readline_set(rl, "__completion_matches__", matches);
    if (js_array_length(result_item) > 1) {
        readline_set(rl, "__completion_line__", js_array_get_int(result_item, 1));
    }
    Item line_item = js_array_length(result_item) > 1 ?
        js_array_get_int(result_item, 1) : readline_get(rl, "line");
    String* line = get_type_id(line_item) == LMD_TYPE_STRING ? it2s(line_item) : NULL;
    if (match_count > 1 && readline_apply_common_completion(rl, matches, line)) {
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (match_count == 1) {
        Item match = js_array_get_int(matches, 0);
        Item match_str = js_to_string(match);
        String* s = get_type_id(match_str) == LMD_TYPE_STRING ? it2s(match_str) : NULL;
        if (s) {
            readline_set_line(rl, s->chars, (int)s->len);
            char buf[1024];
            int n = snprintf(buf, sizeof(buf), "\r\n> %.*s", (int)s->len, s->chars);
            readline_completion_output_write_empty(rl, 7);
            readline_completion_output_write(rl, make_string_item(buf, n));
        }
    } else {
        if (tab_count > 1) {
            if (!readline_render_completion_matches(rl)) {
                readline_render_simple_completion_matches(rl, matches);
            }
        }
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_readline_completion_callback(Item err_item, Item result_item) {
    Item tab_count_item = readline_get(readline_completion_rl, "__tab_count__");
    int tab_count = get_type_id(tab_count_item) == LMD_TYPE_INT ? it2i(tab_count_item) : 0;
    return readline_completion_callback_impl(readline_completion_rl, err_item, result_item, tab_count);
}

extern "C" Item js_readline_completion_callback_bound(Item rl_item, Item tab_count_item, Item err_item, Item result_item) {
    int tab_count = get_type_id(tab_count_item) == LMD_TYPE_INT ? it2i(tab_count_item) : 0;
    return readline_completion_callback_impl(rl_item, err_item, result_item, tab_count);
}

extern "C" Item js_readline_completion_fulfilled(Item rl_item, Item result_item) {
    readline_completion_rl = rl_item;
    readline_set(rl_item, "__completion_direct_output__", (Item){.item = ITEM_TRUE});
    Item result = js_readline_completion_callback((Item){.item = ITEM_JS_UNDEFINED}, result_item);
    readline_set(rl_item, "__completion_direct_output__", (Item){.item = ITEM_JS_UNDEFINED});
    return result;
}

extern "C" Item js_readline_completion_rejected(Item rl_item, Item err_item) {
    readline_completion_rl = rl_item;
    readline_set(rl_item, "__completion_direct_output__", (Item){.item = ITEM_TRUE});
    Item result = js_readline_completion_callback(err_item, (Item){.item = ITEM_JS_UNDEFINED});
    readline_set(rl_item, "__completion_direct_output__", (Item){.item = ITEM_JS_UNDEFINED});
    return result;
}

extern "C" Item js_readline_completion_fulfilled_bound(Item rl_item, Item tab_count_item, Item result_item) {
    readline_set(rl_item, "__completion_direct_output__", (Item){.item = ITEM_TRUE});
    Item result = js_readline_completion_callback_bound(rl_item, tab_count_item,
        (Item){.item = ITEM_JS_UNDEFINED}, result_item);
    readline_set(rl_item, "__completion_direct_output__", (Item){.item = ITEM_JS_UNDEFINED});
    return result;
}

extern "C" Item js_readline_completion_rejected_bound(Item rl_item, Item tab_count_item, Item err_item) {
    readline_set(rl_item, "__completion_direct_output__", (Item){.item = ITEM_TRUE});
    Item result = js_readline_completion_callback_bound(rl_item, tab_count_item,
        err_item, (Item){.item = ITEM_JS_UNDEFINED});
    readline_set(rl_item, "__completion_direct_output__", (Item){.item = ITEM_JS_UNDEFINED});
    return result;
}

// =============================================================================
// readline.question(prompt, callback) — ask question, call back with answer
// =============================================================================

extern "C" Item js_readline_question(Item prompt_item, Item options_or_callback, Item callback_item) {
    Item self = js_get_current_this();
    Item callback = callback_item;
    Item signal = (Item){.item = ITEM_JS_UNDEFINED};
    if (get_type_id(options_or_callback) == LMD_TYPE_FUNC) {
        callback = options_or_callback;
    } else if (get_type_id(options_or_callback) == LMD_TYPE_MAP) {
        signal = readline_get(options_or_callback, "signal");
    }
    if (readline_get(self, "closed").item == ITEM_TRUE) {
        if (readline_get(self, "__promises_mode__").item == ITEM_TRUE) {
            return readline_rejected_promise(readline_error_with_code(
                "ERR_USE_AFTER_CLOSE", "readline interface is closed"));
        }
        return js_throw_error_with_code("ERR_USE_AFTER_CLOSE", "readline interface is closed");
    }

    if (get_type_id(prompt_item) == LMD_TYPE_STRING) {
        readline_output_write(self, prompt_item);
        readline_set(self, "__question_prompt__", prompt_item);
    }

    if (get_type_id(readline_get(self, "__question_callback__")) == LMD_TYPE_FUNC ||
        get_type_id(readline_get(self, "__question_resolve__")) == LMD_TYPE_FUNC) {
        return self;
    }

    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (!readline_register_question_signal(self, signal)) {
            readline_clear_question(self);
            return self;
        }
        readline_set(self, "__question_callback__", callback);
        return self;
    }

    if (readline_get(self, "__promises_mode__").item == ITEM_TRUE) {
        Item capability = js_promise_with_resolvers();
        readline_set(self, "__question_resolve__", readline_get(capability, "resolve"));
        readline_set(self, "__question_reject__", readline_get(capability, "reject"));
        if (!readline_register_question_signal(self, signal)) {
            Item reject = readline_get(capability, "reject");
            readline_clear_question(self);
            Item err = readline_abort_error_from_signal(signal);
            readline_reject_later(reject, err);
        }
        return readline_get(capability, "promise");
    }

    return self;
}

extern "C" Item js_readline_question_promisified(Item prompt_item, Item options_item) {
    Item self = js_get_current_this();
    Item capability = js_promise_with_resolvers();
    Item promise = readline_get(capability, "promise");
    Item resolve = readline_get(capability, "resolve");
    Item reject = readline_get(capability, "reject");
    if (readline_get(self, "closed").item == ITEM_TRUE) {
        Item err = readline_error_with_code("ERR_USE_AFTER_CLOSE", "readline interface is closed");
        readline_reject_later(reject, err);
        return promise;
    }
    if (get_type_id(readline_get(self, "__question_callback__")) == LMD_TYPE_FUNC ||
        get_type_id(readline_get(self, "__question_resolve__")) == LMD_TYPE_FUNC) {
        return promise;
    }
    if (get_type_id(prompt_item) == LMD_TYPE_STRING) {
        readline_output_write(self, prompt_item);
        readline_set(self, "__question_prompt__", prompt_item);
    }
    readline_set(self, "__question_resolve__", resolve);
    readline_set(self, "__question_reject__", reject);
    Item signal = get_type_id(options_item) == LMD_TYPE_MAP ?
        readline_get(options_item, "signal") : (Item){.item = ITEM_JS_UNDEFINED};
    if (!readline_register_question_signal(self, signal)) {
        readline_clear_question(self);
        Item err = readline_abort_error_from_signal(signal);
        readline_reject_later(reject, err);
    }
    return promise;
}

// =============================================================================
// readline.close() — close the interface
// =============================================================================

extern "C" Item js_readline_close(void) {
    Item self = js_get_current_this();
    readline_set(self, "closed", (Item){.item = ITEM_TRUE});
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
    Item columns_item = readline_get(readline_get(self, "output"), "columns");
    int columns = get_type_id(columns_item) == LMD_TYPE_INT ? it2i(columns_item) : 0;
    int rows = 0;
    int line_cols = 0;
    Item tab_size_item = readline_get(self, "tabSize");
    int tab_size = get_type_id(tab_size_item) == LMD_TYPE_INT ? it2i(tab_size_item) : 8;
    Item prompt_item = readline_get(self, "__question_prompt__");
    if (get_type_id(prompt_item) != LMD_TYPE_STRING) {
        prompt_item = readline_get(self, "__prompt__");
    }
    String* prompt = get_type_id(prompt_item) == LMD_TYPE_STRING ? it2s(prompt_item) : NULL;
    readline_display_position_update(prompt, -1, columns, tab_size, &rows, &line_cols);
    readline_display_position_update(line, cursor, columns, tab_size, &rows, &line_cols);
    int cols = 0;
    readline_display_position_finish(rows, line_cols, columns, &rows, &cols);
    readline_set(result, "cols", (Item){.item = i2it(cols)});
    readline_set(result, "rows", (Item){.item = i2it(rows)});
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
    if (get_type_id(prompt) == LMD_TYPE_STRING) {
        if (readline_get(self, "terminal").item == ITEM_TRUE) {
            readline_output_write(self, make_string_item("\x1b[1G", 4));
            readline_output_write(self, make_string_item("\x1b[0J", 4));
            readline_output_write(self, prompt);
            String* prompt_str = it2s(prompt);
            int width = readline_display_width(prompt_str, -1);
            char cursor[32];
            int cursor_len = snprintf(cursor, sizeof(cursor), "\x1b[%dG", width + 1);
            readline_output_write(self, make_string_item(cursor, cursor_len));
        } else {
            readline_output_write(self, prompt);
        }
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static Item js_readline_write_impl(Item self, Item data_item, Item key_item) {
    if (readline_get(self, "closed").item == ITEM_TRUE) {
        return js_throw_error_with_code("ERR_USE_AFTER_CLOSE", "readline interface is closed");
    }
    if (get_type_id(key_item) == LMD_TYPE_MAP) {
        Item name_item = readline_get(key_item, "name");
        Item ctrl_item = readline_get(key_item, "ctrl");
        Item meta_item = readline_get(key_item, "meta");
        Item shift_item = readline_get(key_item, "shift");
        Item sequence_item = readline_get(key_item, "sequence");
        String* sequence = get_type_id(sequence_item) == LMD_TYPE_STRING ? it2s(sequence_item) : NULL;
        if (sequence && sequence->len == 1) {
            unsigned char c = (unsigned char)sequence->chars[0];
            if (c == 0x1F) {
                readline_undo(self);
                return (Item){.item = ITEM_JS_UNDEFINED};
            }
            if (c == 0x1E) {
                readline_redo(self);
                return (Item){.item = ITEM_JS_UNDEFINED};
            }
        }
        String* name = get_type_id(name_item) == LMD_TYPE_STRING ? it2s(name_item) : NULL;
        if (name && name->len == 1) {
            char c = name->chars[0];
            if (c == 'a' && ctrl_item.item == ITEM_TRUE) readline_set(self, "cursor", (Item){.item = i2it(0)});
            else if (c == 'e' && ctrl_item.item == ITEM_TRUE) {
                Item line_item = readline_get(self, "line");
                String* line = get_type_id(line_item) == LMD_TYPE_STRING ? it2s(line_item) : NULL;
                readline_set(self, "cursor", (Item){.item = i2it(line ? (int)line->len : 0)});
            } else if (c == 'u' && ctrl_item.item == ITEM_TRUE) {
                Item line_item = readline_get(self, "line");
                String* line = get_type_id(line_item) == LMD_TYPE_STRING ? it2s(line_item) : NULL;
                readline_store_kill(self, line ? make_string_item(line->chars, (int)line->len) : make_string_item("", 0));
                readline_set_line(self, "", 0);
            } else if (c == 'y' && ctrl_item.item == ITEM_TRUE) {
                Item kill = readline_get(self, "__kill_buffer__");
                if (get_type_id(kill) == LMD_TYPE_STRING) {
                    String* s = it2s(kill);
                    int start = it2i(readline_get(self, "cursor"));
                    readline_append_line(self, s->chars, (int)s->len);
                    readline_set(self, "__last_yank_start__", (Item){.item = i2it(start)});
                    readline_set(self, "__last_yank_end__", (Item){.item = i2it(start + (int)s->len)});
                }
            } else if (c == 'y' && meta_item.item == ITEM_TRUE) {
                Item prev = readline_get(self, "__kill_prev__");
                if (get_type_id(prev) == LMD_TYPE_STRING) {
                    int start = it2i(readline_get(self, "__last_yank_start__"));
                    int end = it2i(readline_get(self, "__last_yank_end__"));
                    readline_delete_range(self, start, end);
                    String* s = it2s(prev);
                    readline_append_line(self, s->chars, (int)s->len);
                    readline_set(self, "__last_yank_end__", (Item){.item = i2it(start + (int)s->len)});
                }
            } else if (c == 'w' && ctrl_item.item == ITEM_TRUE) {
                readline_delete_word_backward(self);
            } else if (c == 'b' && ctrl_item.item == ITEM_TRUE) {
                Item line_item = readline_get(self, "line");
                String* line = get_type_id(line_item) == LMD_TYPE_STRING ? it2s(line_item) : NULL;
                int cursor = it2i(readline_get(self, "cursor"));
                readline_set(self, "cursor", (Item){.item = i2it(readline_prev_char_index(line, cursor))});
            } else if (c == 'f' && ctrl_item.item == ITEM_TRUE) {
                Item line_item = readline_get(self, "line");
                String* line = get_type_id(line_item) == LMD_TYPE_STRING ? it2s(line_item) : NULL;
                int cursor = it2i(readline_get(self, "cursor"));
                readline_set(self, "cursor", (Item){.item = i2it(readline_next_char_index(line, cursor))});
            } else if (c == 'c' && ctrl_item.item == ITEM_TRUE) {
                if (!readline_abort_pending_question(self, true)) {
                    readline_set(self, "closed", (Item){.item = ITEM_TRUE});
                }
            } else if (c == 'k' && ctrl_item.item == ITEM_TRUE) {
                Item line_item = readline_get(self, "line");
                String* line = get_type_id(line_item) == LMD_TYPE_STRING ? it2s(line_item) : NULL;
                int cursor = it2i(readline_get(self, "cursor"));
                int end = line ? (int)line->len : cursor;
                if (cursor < end) {
                    readline_remember_undo(self);
                    readline_store_kill(self, readline_slice_line(self, cursor, end));
                    readline_delete_range(self, cursor, end);
                }
            } else if (c == 'f' && meta_item.item == ITEM_TRUE) {
                readline_move_word_forward(self);
            } else if (c == 'b' && meta_item.item == ITEM_TRUE) {
                readline_move_word_backward(self);
            } else if (c == 'd' && meta_item.item == ITEM_TRUE) {
                readline_delete_word_forward(self);
            } else if (c == 'd' && ctrl_item.item == ITEM_TRUE) {
                if (!readline_abort_pending_question(self, false)) {
                    readline_delete_right(self);
                }
            } else if (c == 'h' && ctrl_item.item == ITEM_TRUE) {
                readline_backspace(self);
            } else if (c == 'n' && ctrl_item.item == ITEM_TRUE) {
                readline_history_move(self, 1);
            } else if (c == 'p' && ctrl_item.item == ITEM_TRUE) {
                readline_history_move(self, -1);
            }
        } else if (name && name->len == 2 && memcmp(name->chars, "up", 2) == 0) {
            readline_history_move(self, -1);
        } else if (name && name->len == 4 && memcmp(name->chars, "down", 4) == 0) {
            readline_history_move(self, 1);
        } else if (name && name->len == 5 && memcmp(name->chars, "enter", 5) == 0) {
            Item line = readline_get(self, "line");
            readline_emit_line(self, line);
            readline_history_add(self, line);
            readline_set(self, "__history_search__", (Item){.item = ITEM_JS_UNDEFINED});
            readline_set_line(self, "", 0);
        } else if (name && name->len == 9 && memcmp(name->chars, "backspace", 9) == 0) {
            if (ctrl_item.item == ITEM_TRUE && shift_item.item == ITEM_TRUE) {
                int cursor = it2i(readline_get(self, "cursor"));
                readline_store_kill(self, readline_slice_line(self, 0, cursor));
                readline_delete_range(self, 0, cursor);
            } else if (ctrl_item.item == ITEM_TRUE || meta_item.item == ITEM_TRUE) readline_delete_word_backward(self);
            else readline_backspace(self);
        } else if (name && name->len == 6 && memcmp(name->chars, "delete", 6) == 0) {
            if (ctrl_item.item == ITEM_TRUE && shift_item.item == ITEM_TRUE) {
                Item line_item = readline_get(self, "line");
                String* line = get_type_id(line_item) == LMD_TYPE_STRING ? it2s(line_item) : NULL;
                int cursor = it2i(readline_get(self, "cursor"));
                int end = line ? (int)line->len : cursor;
                readline_store_kill(self, readline_slice_line(self, cursor, end));
                readline_delete_range(self, cursor, end);
            } else if (ctrl_item.item == ITEM_TRUE || meta_item.item == ITEM_TRUE) readline_delete_word_forward(self);
            else readline_delete_right(self);
        } else if (name && name->len == 4 && memcmp(name->chars, "left", 4) == 0) {
            Item line_item = readline_get(self, "line");
            String* line = get_type_id(line_item) == LMD_TYPE_STRING ? it2s(line_item) : NULL;
            int cursor = it2i(readline_get(self, "cursor"));
            if (ctrl_item.item == ITEM_TRUE || meta_item.item == ITEM_TRUE) readline_move_word_backward(self);
            else if (cursor > 0) readline_set(self, "cursor", (Item){.item = i2it(readline_prev_char_index(line, cursor))});
        } else if (name && name->len == 5 && memcmp(name->chars, "right", 5) == 0) {
            Item line_item = readline_get(self, "line");
            String* line = get_type_id(line_item) == LMD_TYPE_STRING ? it2s(line_item) : NULL;
            int cursor = it2i(readline_get(self, "cursor"));
            if (ctrl_item.item == ITEM_TRUE || meta_item.item == ITEM_TRUE) readline_move_word_forward(self);
            else if (line && cursor < (int)line->len) readline_set(self, "cursor", (Item){.item = i2it(readline_next_char_index(line, cursor))});
            readline_set(self, "__history_index__", (Item){.item = i2it(-1)});
            readline_set(self, "historyIndex", (Item){.item = i2it(-1)});
            readline_set(self, "__history_search__", (Item){.item = ITEM_JS_UNDEFINED});
        }
        return (Item){.item = ITEM_JS_UNDEFINED};
    }

    data_item = readline_decode_input_text(self, data_item);
    if (get_type_id(data_item) != LMD_TYPE_STRING) return (Item){.item = ITEM_JS_UNDEFINED};
    String* s = it2s(data_item);
    if (!s) return (Item){.item = ITEM_JS_UNDEFINED};
    int input_len = (int)s->len;
    char stack_input_chars[4096];
    char* heap_input_chars = NULL;
    char* input_chars = NULL;
    if (input_len > 0 && input_len <= (int)sizeof(stack_input_chars)) {
        input_chars = stack_input_chars;
    } else if (input_len > 0) {
        heap_input_chars = (char*)mem_alloc((size_t)input_len, MEM_CAT_JS_RUNTIME);
        input_chars = heap_input_chars;
    }
    if (input_chars) memcpy(input_chars, s->chars, (size_t)input_len);
    int start = 0;
    bool saw_cr_in_chunk = false;
    for (int i = 0; i < input_len; i++) {
        char c = input_chars[i];
        if (!readline_emit_keypress(self, c)) {
            if (heap_input_chars) mem_free(heap_input_chars);
            return ItemNull;
        }
        if (c == '\t') {
            readline_set(self, "__saw_cr__", (Item){.item = ITEM_FALSE});
            if (get_type_id(readline_get(self, "completer")) != LMD_TYPE_FUNC) {
                continue;
            }
            if (i > start) {
                Item chunk = make_string_item(input_chars + start, i - start);
                readline_append_line(self, input_chars + start, i - start);
                readline_output_write(self, chunk);
            }
            readline_handle_tab(self);
            start = i + 1;
        } else if (c == '\n') {
            bool suppress_lf = saw_cr_in_chunk ||
                (readline_get(self, "__saw_cr__").item == ITEM_TRUE && readline_crlf_delay_is_infinite(self));
            readline_set(self, "__saw_cr__", (Item){.item = ITEM_FALSE});
            if (suppress_lf) {
                start = i + 1;
                saw_cr_in_chunk = false;
                continue;
            }
            if (i > start) {
                Item chunk = make_string_item(input_chars + start, i - start);
                readline_append_line(self, input_chars + start, i - start);
                readline_output_write(self, chunk);
            }
            readline_output_write(self, make_string_item("\n", 1));
            Item line = readline_get(self, "line");
            readline_emit_line(self, line);
            readline_history_add(self, line);
            readline_set(self, "__history_search__", (Item){.item = ITEM_JS_UNDEFINED});
            readline_set_line(self, "", 0);
            start = i + 1;
        } else if (c == '\r') {
            readline_set(self, "__saw_cr__", (Item){.item = ITEM_TRUE});
            saw_cr_in_chunk = true;
            if (i > start) {
                Item chunk = make_string_item(input_chars + start, i - start);
                readline_append_line(self, input_chars + start, i - start);
                readline_output_write(self, chunk);
            }
            readline_output_write(self, make_string_item("\r", 1));
            Item line = readline_get(self, "line");
            readline_emit_line(self, line);
            readline_history_add(self, line);
            readline_set(self, "__history_search__", (Item){.item = ITEM_JS_UNDEFINED});
            readline_set_line(self, "", 0);
            start = i + 1;
        } else {
            readline_set(self, "__saw_cr__", (Item){.item = ITEM_FALSE});
            saw_cr_in_chunk = false;
        }
    }
    if (start < input_len) {
        Item chunk = make_string_item(input_chars + start, input_len - start);
        readline_append_line(self, input_chars + start, input_len - start);
        readline_output_write(self, chunk);
    }
    if (heap_input_chars) mem_free(heap_input_chars);
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
    if (readline_get(rl, "closed").item == ITEM_TRUE) return (Item){.item = ITEM_JS_UNDEFINED};
    return js_readline_write_impl(rl, data_item, (Item){.item = ITEM_JS_UNDEFINED});
}

extern "C" Item js_readline_deferred_input_data(Item rl, Item data_item) {
    if (readline_get(rl, "closed").item == ITEM_TRUE) return (Item){.item = ITEM_JS_UNDEFINED};
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
            if (readline_get(rl, "closed").item == ITEM_TRUE) return (Item){.item = ITEM_TRUE};
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
    if (readline_get(rl, "__synth_keypress__").item == ITEM_TRUE) {
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
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
        if (get_type_id(history) == LMD_TYPE_ARRAY) {
            readline_set(rl, "history", history);
        }
        Item remove_history_duplicates = js_property_get(options_item, make_string_item("removeHistoryDuplicates"));
        if (get_type_id(remove_history_duplicates) == LMD_TYPE_BOOL) {
            readline_set(rl, "removeHistoryDuplicates", remove_history_duplicates);
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
            readline_set(rl, "tabSize", tab_size);
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
                delay_item = js_make_number(v);
            } else {
                delay_item = (Item){.item = i2it(100)};
            }
        }
        readline_set(rl, "crlfDelay", delay_item);
    }
    js_property_set(rl, make_string_item("__prompt__"), prompt_val);
    readline_set_line(rl, "", 0);
    readline_set(rl, "__tab_count__", (Item){.item = i2it(0)});
    if (get_type_id(readline_get(rl, "history")) != LMD_TYPE_ARRAY) {
        readline_set(rl, "history", js_array_new(0));
    }
    if (get_type_id(readline_get(rl, "historySize")) == LMD_TYPE_UNDEFINED) {
        readline_set(rl, "historySize", (Item){.item = i2it(30)});
    }
    if (get_type_id(readline_get(rl, "tabSize")) == LMD_TYPE_UNDEFINED) {
        readline_set(rl, "tabSize", (Item){.item = i2it(8)});
    }
    readline_set(rl, "__history_index__", (Item){.item = i2it(-1)});
    readline_set(rl, "historyIndex", (Item){.item = i2it(-1)});
    if (readline_create_promises_mode) {
        readline_set(rl, "__promises_mode__", (Item){.item = ITEM_TRUE});
    }
    if (readline_namespace.item != 0) {
        Item ctor = readline_get(readline_namespace, "Interface");
        Item proto = readline_get(ctor, "prototype");
        if (get_type_id(proto) == LMD_TYPE_MAP) js_set_prototype(rl, proto);
    }

    // methods
    Item question_fn = js_new_function((void*)js_readline_question, 3);
    js_property_set(question_fn, js_symbol_for(make_string_item("nodejs.util.promisify.custom")),
                    js_new_function((void*)js_readline_question_promisified, 2));
    js_property_set(rl, make_string_item("question"), question_fn);
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
            Item listener_base = js_new_function((void*)js_readline_bound_input_data, 2);
            listener = js_bind_function(listener_base, ItemNull, bound_args, 1);
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
                Item write_base = js_new_function((void*)js_readline_bound_input_data, 2);
                write_fn = js_bind_function(write_base, ItemNull, bound_args, 1);
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
