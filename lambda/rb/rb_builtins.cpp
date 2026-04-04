// rb_builtins.cpp — Ruby built-in method dispatchers for Phase 3
// Handles method calls on String, Array, Hash, Integer, Float.
// Dispatcher signature: Item rb_X_method(Item self, Item method_name, Item* args, int argc)
// Returns ITEM_ERROR when method is not found (sentinel for transpiler dispatch chain).

#include "rb_runtime.h"
#include "rb_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cctype>

// forward-declare 2-arg heap_create_name (declared in transpiler.hpp, defined in lambda-mem.cpp)
String* heap_create_name(const char* name, size_t len);

// external map helpers
extern Item _map_read_field(ShapeEntry* field, void* map_data);
extern Item _map_get(TypeMap* map_type, void* map_data, char* key, bool* is_found);
extern TypeMap EmptyMap;

// sentinel: method not found
#define RB_METHOD_NOT_FOUND ((Item){.item = ITEM_ERROR})

// helper: push to array (avoids linker issues with array_push linkage)
static inline void rb_arr_push(Array* arr, Item value) {
    if (arr->length >= arr->capacity) {
        int64_t new_cap = arr->capacity < 8 ? 8 : arr->capacity * 2;
        arr->items = (Item*)realloc(arr->items, new_cap * sizeof(Item));
        arr->capacity = new_cap;
    }
    arr->items[arr->length++] = value;
}

// helper: make Item from inline int
static inline Item rb_iitem(int64_t v) { return (Item){.item = i2it(v)}; }
// helper: make Item from double (must use push_d which allocates in GC nursery)
extern "C" Item push_d(double dval);
static inline Item rb_ditem(double v) { return push_d(v); }
// helper: make Item from C string
static inline Item rb_sitem(const char* s) { return (Item){.item = s2it(heap_create_name(s, strlen(s)))}; }
static inline Item rb_sitem_n(const char* s, int64_t len) { return (Item){.item = s2it(heap_create_name(s, len))}; }
// helper: make true/false
static inline Item rb_bitem(bool v) { return (Item){.item = v ? ITEM_TRUE : ITEM_FALSE}; }

// ============================================================================
// String methods
// ============================================================================

extern "C" Item rb_string_method(Item self, Item method_name, Item* args, int argc) {
    if (get_type_id(self) != LMD_TYPE_STRING) return RB_METHOD_NOT_FOUND;
    if (get_type_id(method_name) != LMD_TYPE_STRING) return RB_METHOD_NOT_FOUND;

    String* s = it2s(self);
    String* method = it2s(method_name);
    if (!s || !method) return RB_METHOD_NOT_FOUND;

    const char* m = method->chars;

    // .length / .size
    if (strcmp(m, "length") == 0 || strcmp(m, "size") == 0) {
        return rb_iitem(s->len);
    }

    // .upcase
    if (strcmp(m, "upcase") == 0) {
        char* buf = (char*)malloc(s->len + 1);
        for (int64_t i = 0; i < s->len; i++) buf[i] = toupper((unsigned char)s->chars[i]);
        buf[s->len] = '\0';
        Item result = rb_sitem_n(buf, s->len);
        free(buf);
        return result;
    }

    // .downcase
    if (strcmp(m, "downcase") == 0) {
        char* buf = (char*)malloc(s->len + 1);
        for (int64_t i = 0; i < s->len; i++) buf[i] = tolower((unsigned char)s->chars[i]);
        buf[s->len] = '\0';
        Item result = rb_sitem_n(buf, s->len);
        free(buf);
        return result;
    }

    // .capitalize
    if (strcmp(m, "capitalize") == 0) {
        if (s->len == 0) return self;
        char* buf = (char*)malloc(s->len + 1);
        buf[0] = toupper((unsigned char)s->chars[0]);
        for (int64_t i = 1; i < s->len; i++) buf[i] = tolower((unsigned char)s->chars[i]);
        buf[s->len] = '\0';
        Item result = rb_sitem_n(buf, s->len);
        free(buf);
        return result;
    }

    // .strip
    if (strcmp(m, "strip") == 0) {
        int64_t start = 0, end = s->len;
        while (start < end && isspace((unsigned char)s->chars[start])) start++;
        while (end > start && isspace((unsigned char)s->chars[end - 1])) end--;
        return rb_sitem_n(s->chars + start, end - start);
    }

    // .lstrip
    if (strcmp(m, "lstrip") == 0) {
        int64_t start = 0;
        while (start < s->len && isspace((unsigned char)s->chars[start])) start++;
        return rb_sitem_n(s->chars + start, s->len - start);
    }

    // .rstrip
    if (strcmp(m, "rstrip") == 0) {
        int64_t end = s->len;
        while (end > 0 && isspace((unsigned char)s->chars[end - 1])) end--;
        return rb_sitem_n(s->chars, end);
    }

    // .reverse
    if (strcmp(m, "reverse") == 0) {
        char* buf = (char*)malloc(s->len + 1);
        for (int64_t i = 0; i < s->len; i++) buf[i] = s->chars[s->len - 1 - i];
        buf[s->len] = '\0';
        Item result = rb_sitem_n(buf, s->len);
        free(buf);
        return result;
    }

    // .include?(substr)
    if ((strcmp(m, "include?") == 0 || strcmp(m, "include") == 0) && argc >= 1) {
        String* sub = it2s(args[0]);
        if (!sub) return rb_bitem(false);
        if (sub->len == 0) return rb_bitem(true);
        if (sub->len > s->len) return rb_bitem(false);
        return rb_bitem(strstr(s->chars, sub->chars) != NULL);
    }

    // .start_with?(prefix)
    if ((strcmp(m, "start_with?") == 0 || strcmp(m, "start_with") == 0) && argc >= 1) {
        String* prefix = it2s(args[0]);
        if (!prefix) return rb_bitem(false);
        if (prefix->len > s->len) return rb_bitem(false);
        return rb_bitem(strncmp(s->chars, prefix->chars, prefix->len) == 0);
    }

    // .end_with?(suffix)
    if ((strcmp(m, "end_with?") == 0 || strcmp(m, "end_with") == 0) && argc >= 1) {
        String* suffix = it2s(args[0]);
        if (!suffix) return rb_bitem(false);
        if (suffix->len > s->len) return rb_bitem(false);
        return rb_bitem(strncmp(s->chars + s->len - suffix->len, suffix->chars, suffix->len) == 0);
    }

    // .split(sep=nil)
    if (strcmp(m, "split") == 0) {
        Array* result = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;

        const char* sep = NULL;
        int64_t sep_len = 0;
        if (argc >= 1 && get_type_id(args[0]) == LMD_TYPE_STRING) {
            String* sep_s = it2s(args[0]);
            if (sep_s && sep_s->len > 0) { sep = sep_s->chars; sep_len = sep_s->len; }
        }

        if (!sep) {
            // split on whitespace, skip leading/trailing
            int64_t i = 0;
            while (i < s->len) {
                while (i < s->len && isspace((unsigned char)s->chars[i])) i++;
                if (i >= s->len) break;
                int64_t start = i;
                while (i < s->len && !isspace((unsigned char)s->chars[i])) i++;
                rb_arr_push(result, rb_sitem_n(s->chars + start, i - start));
            }
        } else {
            int64_t i = 0;
            while (i <= s->len) {
                const char* found = (i < s->len) ? strstr(s->chars + i, sep) : NULL;
                if (!found || found >= s->chars + s->len) {
                    rb_arr_push(result, rb_sitem_n(s->chars + i, s->len - i));
                    break;
                }
                int64_t pos = found - s->chars;
                rb_arr_push(result, rb_sitem_n(s->chars + i, pos - i));
                i = pos + sep_len;
            }
        }
        return (Item){.array = result};
    }

    // .replace(old, new)
    if (strcmp(m, "replace") == 0 && argc >= 2) {
        // Ruby's replace replaces content in place; we create a new string
        // Actually, Ruby replace(other_string) replaces entire content
        // For gsub-like behavior use gsub. But let's support replace(old, new) too.
        String* old_s = it2s(args[0]);
        String* new_s = it2s(args[1]);
        if (!old_s || !new_s || old_s->len == 0) return self;

        StrBuf* buf = strbuf_new_cap(s->len);
        int64_t i = 0;
        while (i < s->len) {
            const char* found = strstr(s->chars + i, old_s->chars);
            if (!found || found >= s->chars + s->len) {
                strbuf_append_str_n(buf, s->chars + i, s->len - i);
                break;
            }
            int64_t pos = found - s->chars;
            strbuf_append_str_n(buf, s->chars + i, pos - i);
            strbuf_append_str_n(buf, new_s->chars, new_s->len);
            i = pos + old_s->len;
        }
        Item result = rb_sitem(buf->str);
        strbuf_free(buf);
        return result;
    }

    // .gsub(pattern, replacement) — supports regex and string patterns
    if (strcmp(m, "gsub") == 0 && argc >= 2) {
        // if pattern is a regex, delegate to rb_regex_gsub
        if (rb_is_regex(args[0])) {
            return rb_regex_gsub(args[0], self, args[1]);
        }
        String* pat = it2s(args[0]);
        String* rep = it2s(args[1]);
        if (!pat || !rep || pat->len == 0) return self;

        StrBuf* buf = strbuf_new_cap(s->len);
        int64_t i = 0;
        while (i < s->len) {
            const char* found = strstr(s->chars + i, pat->chars);
            if (!found || found >= s->chars + s->len) {
                strbuf_append_str_n(buf, s->chars + i, s->len - i);
                break;
            }
            int64_t pos = found - s->chars;
            strbuf_append_str_n(buf, s->chars + i, pos - i);
            strbuf_append_str_n(buf, rep->chars, rep->len);
            i = pos + pat->len;
        }
        Item result = rb_sitem(buf->str);
        strbuf_free(buf);
        return result;
    }

    // .sub(pattern, replacement) — first occurrence only, supports regex
    if (strcmp(m, "sub") == 0 && argc >= 2) {
        // if pattern is a regex, delegate to rb_regex_sub
        if (rb_is_regex(args[0])) {
            return rb_regex_sub(args[0], self, args[1]);
        }
        String* pat = it2s(args[0]);
        String* rep = it2s(args[1]);
        if (!pat || !rep || pat->len == 0) return self;

        const char* found = strstr(s->chars, pat->chars);
        if (!found) return self;

        int64_t pos = found - s->chars;
        StrBuf* buf = strbuf_new_cap(s->len);
        strbuf_append_str_n(buf, s->chars, pos);
        strbuf_append_str_n(buf, rep->chars, rep->len);
        strbuf_append_str_n(buf, s->chars + pos + pat->len, s->len - pos - pat->len);
        Item result = rb_sitem(buf->str);
        strbuf_free(buf);
        return result;
    }

    // .match(regex_or_str) — returns matched string or nil
    if (strcmp(m, "match") == 0 && argc >= 1) {
        if (rb_is_regex(args[0])) {
            return rb_regex_match(args[0], self);
        }
        // plain string: create regex from string pattern
        Item regex = rb_regex_new(args[0]);
        return rb_regex_match(regex, self);
    }

    // .scan(regex_or_str) — returns array of all matches
    if (strcmp(m, "scan") == 0 && argc >= 1) {
        if (rb_is_regex(args[0])) {
            return rb_regex_scan(args[0], self);
        }
        Item regex = rb_regex_new(args[0]);
        return rb_regex_scan(regex, self);
    }

    // .count(substr)
    if (strcmp(m, "count") == 0 && argc >= 1) {
        String* sub = it2s(args[0]);
        if (!sub || sub->len == 0) return rb_iitem(0);
        int64_t count = 0;
        const char* p = s->chars;
        while ((p = strstr(p, sub->chars)) != NULL) {
            count++;
            p += sub->len;
        }
        return rb_iitem(count);
    }

    // .index(substr)
    if (strcmp(m, "index") == 0 && argc >= 1) {
        String* sub = it2s(args[0]);
        if (!sub) return (Item){.item = ITEM_NULL};
        const char* found = strstr(s->chars, sub->chars);
        if (!found) return (Item){.item = ITEM_NULL};
        return rb_iitem(found - s->chars);
    }

    // .chars
    if (strcmp(m, "chars") == 0) {
        Array* result = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        for (int64_t i = 0; i < s->len; i++) {
            rb_arr_push(result, rb_sitem_n(s->chars + i, 1));
        }
        return (Item){.array = result};
    }

    // .bytes
    if (strcmp(m, "bytes") == 0) {
        Array* result = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        for (int64_t i = 0; i < s->len; i++) {
            rb_arr_push(result, rb_iitem((unsigned char)s->chars[i]));
        }
        return (Item){.array = result};
    }

    // .empty?
    if (strcmp(m, "empty?") == 0 || strcmp(m, "empty") == 0) {
        return rb_bitem(s->len == 0);
    }

    // .chomp
    if (strcmp(m, "chomp") == 0) {
        int64_t end = s->len;
        if (end > 0 && s->chars[end - 1] == '\n') {
            end--;
            if (end > 0 && s->chars[end - 1] == '\r') end--;
        }
        return rb_sitem_n(s->chars, end);
    }

    // .chop
    if (strcmp(m, "chop") == 0) {
        if (s->len == 0) return self;
        int64_t end = s->len - 1;
        if (end > 0 && s->chars[end] == '\n' && s->chars[end - 1] == '\r') end--;
        return rb_sitem_n(s->chars, end);
    }

    // .freeze (no-op — Lambda strings are immutable)
    if (strcmp(m, "freeze") == 0) return self;

    // .frozen? (always true — Lambda strings are immutable)
    if (strcmp(m, "frozen?") == 0 || strcmp(m, "frozen") == 0) return rb_bitem(true);

    // .to_s (identity)
    if (strcmp(m, "to_s") == 0) return self;

    // .to_i
    if (strcmp(m, "to_i") == 0) return rb_to_i(self);

    // .to_f
    if (strcmp(m, "to_f") == 0) return rb_to_f(self);

    // .to_sym
    if (strcmp(m, "to_sym") == 0) {
        return (Item){.item = s2it(heap_create_name(s->chars, s->len))};
    }

    // .join(sep) — only meaningful if called on string, but Ruby doesn't have this
    // .concat(other)
    if (strcmp(m, "concat") == 0 && argc >= 1) {
        return rb_string_concat(self, rb_to_str(args[0]));
    }

    // .center(width, padstr=" ")
    if (strcmp(m, "center") == 0 && argc >= 1) {
        int64_t width = it2i(args[0]);
        if (width <= s->len) return self;
        char pad = ' ';
        if (argc >= 2 && get_type_id(args[1]) == LMD_TYPE_STRING) {
            String* ps = it2s(args[1]);
            if (ps && ps->len > 0) pad = ps->chars[0];
        }
        int64_t total_pad = width - s->len;
        int64_t left_pad = total_pad / 2;
        int64_t right_pad = total_pad - left_pad;
        StrBuf* buf = strbuf_new_cap(width);
        for (int64_t i = 0; i < left_pad; i++) strbuf_append_char(buf, pad);
        strbuf_append_str_n(buf, s->chars, s->len);
        for (int64_t i = 0; i < right_pad; i++) strbuf_append_char(buf, pad);
        Item result = rb_sitem(buf->str);
        strbuf_free(buf);
        return result;
    }

    // .ljust(width, padstr=" ")
    if (strcmp(m, "ljust") == 0 && argc >= 1) {
        int64_t width = it2i(args[0]);
        if (width <= s->len) return self;
        char pad = ' ';
        if (argc >= 2 && get_type_id(args[1]) == LMD_TYPE_STRING) {
            String* ps = it2s(args[1]);
            if (ps && ps->len > 0) pad = ps->chars[0];
        }
        StrBuf* buf = strbuf_new_cap(width);
        strbuf_append_str_n(buf, s->chars, s->len);
        for (int64_t i = s->len; i < width; i++) strbuf_append_char(buf, pad);
        Item result = rb_sitem(buf->str);
        strbuf_free(buf);
        return result;
    }

    // .rjust(width, padstr=" ")
    if (strcmp(m, "rjust") == 0 && argc >= 1) {
        int64_t width = it2i(args[0]);
        if (width <= s->len) return self;
        char pad = ' ';
        if (argc >= 2 && get_type_id(args[1]) == LMD_TYPE_STRING) {
            String* ps = it2s(args[1]);
            if (ps && ps->len > 0) pad = ps->chars[0];
        }
        StrBuf* buf = strbuf_new_cap(width);
        for (int64_t i = 0; i < width - s->len; i++) strbuf_append_char(buf, pad);
        strbuf_append_str_n(buf, s->chars, s->len);
        Item result = rb_sitem(buf->str);
        strbuf_free(buf);
        return result;
    }

    // .swapcase
    if (strcmp(m, "swapcase") == 0) {
        char* buf = (char*)malloc(s->len + 1);
        for (int64_t i = 0; i < s->len; i++) {
            unsigned char c = (unsigned char)s->chars[i];
            buf[i] = isupper(c) ? tolower(c) : (islower(c) ? toupper(c) : c);
        }
        buf[s->len] = '\0';
        Item result = rb_sitem_n(buf, s->len);
        free(buf);
        return result;
    }

    // .tr(from, to) — translate characters
    if (strcmp(m, "tr") == 0 && argc >= 2) {
        String* from = it2s(args[0]);
        String* to = it2s(args[1]);
        if (!from || !to) return self;
        char* buf = (char*)malloc(s->len + 1);
        for (int64_t i = 0; i < s->len; i++) {
            buf[i] = s->chars[i];
            for (int64_t j = 0; j < from->len; j++) {
                if (s->chars[i] == from->chars[j]) {
                    buf[i] = (j < to->len) ? to->chars[j] : to->chars[to->len - 1];
                    break;
                }
            }
        }
        buf[s->len] = '\0';
        Item result = rb_sitem_n(buf, s->len);
        free(buf);
        return result;
    }

    // .squeeze — remove consecutive duplicates
    if (strcmp(m, "squeeze") == 0) {
        if (s->len == 0) return self;
        StrBuf* buf = strbuf_new_cap(s->len);
        strbuf_append_char(buf, s->chars[0]);
        for (int64_t i = 1; i < s->len; i++) {
            if (s->chars[i] != s->chars[i - 1]) strbuf_append_char(buf, s->chars[i]);
        }
        Item result = rb_sitem(buf->str);
        strbuf_free(buf);
        return result;
    }

    // .delete(chars) — remove all occurrences of characters
    if (strcmp(m, "delete") == 0 && argc >= 1) {
        String* del = it2s(args[0]);
        if (!del || del->len == 0) return self;
        StrBuf* buf = strbuf_new_cap(s->len);
        for (int64_t i = 0; i < s->len; i++) {
            bool found = false;
            for (int64_t j = 0; j < del->len; j++) {
                if (s->chars[i] == del->chars[j]) { found = true; break; }
            }
            if (!found) strbuf_append_char(buf, s->chars[i]);
        }
        Item result = rb_sitem(buf->str);
        strbuf_free(buf);
        return result;
    }

    // .slice(index) / .slice(start, length)
    if (strcmp(m, "slice") == 0 && argc >= 1) {
        int64_t idx = it2i(args[0]);
        if (idx < 0) idx += s->len;
        if (idx < 0 || idx >= s->len) return (Item){.item = ITEM_NULL};
        if (argc >= 2) {
            int64_t len = it2i(args[1]);
            if (len <= 0) return rb_sitem("");
            if (idx + len > s->len) len = s->len - idx;
            return rb_sitem_n(s->chars + idx, len);
        }
        return rb_sitem_n(s->chars + idx, 1);
    }

    log_debug("rb: string.%s() not implemented", m);
    return RB_METHOD_NOT_FOUND;
}

// ============================================================================
// Array methods
// ============================================================================

extern "C" Item rb_array_method(Item self, Item method_name, Item* args, int argc) {
    if (get_type_id(self) != LMD_TYPE_ARRAY) return RB_METHOD_NOT_FOUND;
    if (get_type_id(method_name) != LMD_TYPE_STRING) return RB_METHOD_NOT_FOUND;

    Array* arr = it2arr(self);
    String* method = it2s(method_name);
    if (!arr || !method) return RB_METHOD_NOT_FOUND;

    const char* m = method->chars;

    // .length / .size / .count (no-arg)
    if (strcmp(m, "length") == 0 || strcmp(m, "size") == 0 ||
        (strcmp(m, "count") == 0 && argc == 0)) {
        return rb_iitem(arr->length);
    }

    // .count(value) — count occurrences
    if (strcmp(m, "count") == 0 && argc >= 1) {
        int64_t count = 0;
        for (int64_t i = 0; i < arr->length; i++) {
            if (it2b(rb_eq(arr->items[i], args[0]))) count++;
        }
        return rb_iitem(count);
    }

    // .push / .append / .<<
    if ((strcmp(m, "push") == 0 || strcmp(m, "append") == 0) && argc >= 1) {
        for (int i = 0; i < argc; i++) rb_array_push(self, args[i]);
        return self;
    }

    // .pop
    if (strcmp(m, "pop") == 0) {
        return rb_array_pop(self);
    }

    // .shift — remove and return first element
    if (strcmp(m, "shift") == 0) {
        if (arr->length == 0) return (Item){.item = ITEM_NULL};
        Item val = arr->items[0];
        for (int64_t i = 0; i < arr->length - 1; i++) arr->items[i] = arr->items[i + 1];
        arr->length--;
        return val;
    }

    // .unshift / .prepend — add to front
    if ((strcmp(m, "unshift") == 0 || strcmp(m, "prepend") == 0) && argc >= 1) {
        // expand capacity
        int64_t new_len = arr->length + argc;
        if (new_len > arr->capacity) {
            int64_t new_cap = new_len < 8 ? 8 : new_len * 2;
            arr->items = (Item*)realloc(arr->items, new_cap * sizeof(Item));
            arr->capacity = new_cap;
        }
        // shift right
        for (int64_t i = arr->length - 1; i >= 0; i--) arr->items[i + argc] = arr->items[i];
        for (int i = 0; i < argc; i++) arr->items[i] = args[i];
        arr->length = new_len;
        return self;
    }

    // .first
    if (strcmp(m, "first") == 0) {
        if (argc >= 1) {
            int64_t n = it2i(args[0]);
            Array* result = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
            result->type_id = LMD_TYPE_ARRAY;
            for (int64_t i = 0; i < n && i < arr->length; i++) rb_arr_push(result, arr->items[i]);
            return (Item){.array = result};
        }
        return (arr->length > 0) ? arr->items[0] : (Item){.item = ITEM_NULL};
    }

    // .last
    if (strcmp(m, "last") == 0) {
        if (argc >= 1) {
            int64_t n = it2i(args[0]);
            Array* result = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
            result->type_id = LMD_TYPE_ARRAY;
            int64_t start = arr->length - n;
            if (start < 0) start = 0;
            for (int64_t i = start; i < arr->length; i++) rb_arr_push(result, arr->items[i]);
            return (Item){.array = result};
        }
        return (arr->length > 0) ? arr->items[arr->length - 1] : (Item){.item = ITEM_NULL};
    }

    // .flatten
    if (strcmp(m, "flatten") == 0) {
        Array* result = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        for (int64_t i = 0; i < arr->length; i++) {
            if (get_type_id(arr->items[i]) == LMD_TYPE_ARRAY) {
                Array* sub = it2arr(arr->items[i]);
                for (int64_t j = 0; j < sub->length; j++) rb_arr_push(result, sub->items[j]);
            } else {
                rb_arr_push(result, arr->items[i]);
            }
        }
        return (Item){.array = result};
    }

    // .compact — remove nils
    if (strcmp(m, "compact") == 0) {
        Array* result = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        for (int64_t i = 0; i < arr->length; i++) {
            if (get_type_id(arr->items[i]) != LMD_TYPE_NULL) rb_arr_push(result, arr->items[i]);
        }
        return (Item){.array = result};
    }

    // .uniq — unique elements
    if (strcmp(m, "uniq") == 0) {
        Array* result = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        for (int64_t i = 0; i < arr->length; i++) {
            bool found = false;
            for (int64_t j = 0; j < result->length; j++) {
                if (it2b(rb_eq(arr->items[i], result->items[j]))) { found = true; break; }
            }
            if (!found) rb_arr_push(result, arr->items[i]);
        }
        return (Item){.array = result};
    }

    // .sort — insertion sort (returns new array)
    if (strcmp(m, "sort") == 0) {
        Array* result = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        for (int64_t i = 0; i < arr->length; i++) rb_arr_push(result, arr->items[i]);
        for (int64_t i = 1; i < result->length; i++) {
            Item key = result->items[i];
            int64_t j = i - 1;
            while (j >= 0 && it2b(rb_gt(result->items[j], key))) {
                result->items[j + 1] = result->items[j];
                j--;
            }
            result->items[j + 1] = key;
        }
        return (Item){.array = result};
    }

    // .reverse
    if (strcmp(m, "reverse") == 0) {
        Array* result = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        for (int64_t i = arr->length - 1; i >= 0; i--) rb_arr_push(result, arr->items[i]);
        return (Item){.array = result};
    }

    // .min
    if (strcmp(m, "min") == 0) {
        if (arr->length == 0) return (Item){.item = ITEM_NULL};
        Item min = arr->items[0];
        for (int64_t i = 1; i < arr->length; i++) {
            if (it2b(rb_lt(arr->items[i], min))) min = arr->items[i];
        }
        return min;
    }

    // .max
    if (strcmp(m, "max") == 0) {
        if (arr->length == 0) return (Item){.item = ITEM_NULL};
        Item max = arr->items[0];
        for (int64_t i = 1; i < arr->length; i++) {
            if (it2b(rb_gt(arr->items[i], max))) max = arr->items[i];
        }
        return max;
    }

    // .sum
    if (strcmp(m, "sum") == 0) {
        if (arr->length == 0) return rb_iitem(0);
        Item sum = arr->items[0];
        for (int64_t i = 1; i < arr->length; i++) sum = rb_add(sum, arr->items[i]);
        return sum;
    }

    // .include?(value)
    if (strcmp(m, "include?") == 0 || strcmp(m, "include") == 0) {
        if (argc < 1) return rb_bitem(false);
        for (int64_t i = 0; i < arr->length; i++) {
            if (it2b(rb_eq(arr->items[i], args[0]))) return rb_bitem(true);
        }
        return rb_bitem(false);
    }

    // .index(value)
    if (strcmp(m, "index") == 0 && argc >= 1) {
        for (int64_t i = 0; i < arr->length; i++) {
            if (it2b(rb_eq(arr->items[i], args[0]))) return rb_iitem(i);
        }
        return (Item){.item = ITEM_NULL};
    }

    // .join(sep="")
    if (strcmp(m, "join") == 0) {
        const char* sep = "";
        int64_t sep_len = 0;
        if (argc >= 1 && get_type_id(args[0]) == LMD_TYPE_STRING) {
            String* ss = it2s(args[0]);
            if (ss) { sep = ss->chars; sep_len = ss->len; }
        }
        StrBuf* buf = strbuf_new_cap(64);
        for (int64_t i = 0; i < arr->length; i++) {
            if (i > 0) strbuf_append_str_n(buf, sep, sep_len);
            Item s = rb_to_str(arr->items[i]);
            String* str = it2s(s);
            if (str) strbuf_append_str_n(buf, str->chars, str->len);
        }
        Item result = rb_sitem(buf->str);
        strbuf_free(buf);
        return result;
    }

    // .zip(other_array)
    if (strcmp(m, "zip") == 0 && argc >= 1 && get_type_id(args[0]) == LMD_TYPE_ARRAY) {
        Array* other = it2arr(args[0]);
        Array* result = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        int64_t max_len = arr->length > other->length ? arr->length : other->length;
        for (int64_t i = 0; i < max_len; i++) {
            Array* pair = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
            pair->type_id = LMD_TYPE_ARRAY;
            rb_arr_push(pair, (i < arr->length) ? arr->items[i] : (Item){.item = ITEM_NULL});
            rb_arr_push(pair, (i < other->length) ? other->items[i] : (Item){.item = ITEM_NULL});
            rb_arr_push(result, (Item){.array = pair});
        }
        return (Item){.array = result};
    }

    // .take(n)
    if (strcmp(m, "take") == 0 && argc >= 1) {
        int64_t n = it2i(args[0]);
        Array* result = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        for (int64_t i = 0; i < n && i < arr->length; i++) rb_arr_push(result, arr->items[i]);
        return (Item){.array = result};
    }

    // .drop(n)
    if (strcmp(m, "drop") == 0 && argc >= 1) {
        int64_t n = it2i(args[0]);
        Array* result = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        for (int64_t i = n; i < arr->length; i++) rb_arr_push(result, arr->items[i]);
        return (Item){.array = result};
    }

    // .rotate(n=1)
    if (strcmp(m, "rotate") == 0) {
        int64_t n = (argc >= 1) ? it2i(args[0]) : 1;
        if (arr->length == 0) return self;
        n = ((n % arr->length) + arr->length) % arr->length;
        Array* result = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        for (int64_t i = 0; i < arr->length; i++) {
            rb_arr_push(result, arr->items[(i + n) % arr->length]);
        }
        return (Item){.array = result};
    }

    // .sample — random element
    if (strcmp(m, "sample") == 0) {
        if (arr->length == 0) return (Item){.item = ITEM_NULL};
        return arr->items[rand() % arr->length];
    }

    // .shuffle — Fisher-Yates
    if (strcmp(m, "shuffle") == 0) {
        Array* result = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        for (int64_t i = 0; i < arr->length; i++) rb_arr_push(result, arr->items[i]);
        for (int64_t i = result->length - 1; i > 0; i--) {
            int64_t j = rand() % (i + 1);
            Item tmp = result->items[i];
            result->items[i] = result->items[j];
            result->items[j] = tmp;
        }
        return (Item){.array = result};
    }

    // .empty?
    if (strcmp(m, "empty?") == 0 || strcmp(m, "empty") == 0) {
        return rb_bitem(arr->length == 0);
    }

    // .none? (no block, check all nil/false)
    if ((strcmp(m, "none?") == 0 || strcmp(m, "none") == 0) && argc == 0) {
        for (int64_t i = 0; i < arr->length; i++) {
            if (rb_is_truthy(arr->items[i])) return rb_bitem(false);
        }
        return rb_bitem(true);
    }

    // .tally — count occurrences of each element
    if (strcmp(m, "tally") == 0) {
        Item result = rb_new_object();
        for (int64_t i = 0; i < arr->length; i++) {
            Item key = rb_to_str(arr->items[i]);
            Item existing = rb_getattr(result, key);
            int64_t count = (get_type_id(existing) == LMD_TYPE_INT) ? it2i(existing) + 1 : 1;
            rb_setattr(result, key, rb_iitem(count));
        }
        return result;
    }

    // .flat_map needs a block — handled by transpiler
    // .to_s
    if (strcmp(m, "to_s") == 0) return rb_to_str(self);

    log_debug("rb: array.%s() not implemented", m);
    return RB_METHOD_NOT_FOUND;
}

// ============================================================================
// Hash methods
// ============================================================================

extern "C" Item rb_hash_method(Item self, Item method_name, Item* args, int argc) {
    if (get_type_id(self) != LMD_TYPE_MAP) return RB_METHOD_NOT_FOUND;
    if (get_type_id(method_name) != LMD_TYPE_STRING) return RB_METHOD_NOT_FOUND;

    // skip class/instance Maps — those go through dynamic dispatch
    Map* map = it2map(self);
    String* method = it2s(method_name);
    if (!map || !method) return RB_METHOD_NOT_FOUND;

    // check if this is a class or instance (has __rb_class__ or __class__)
    // if so, don't intercept — let dynamic dispatch handle it
    if (map->type && map->type != &EmptyMap) {
        TypeMap* tm = (TypeMap*)map->type;
        ShapeEntry* field = tm->shape;
        while (field) {
            if (field->name && (strcmp(field->name->str, "__rb_class__") == 0 ||
                strcmp(field->name->str, "__class__") == 0)) {
                return RB_METHOD_NOT_FOUND; // let dynamic dispatch handle
            }
            field = field->next;
        }
    }

    const char* m = method->chars;
    TypeMap* tm = map->type ? (TypeMap*)map->type : NULL;

    // .keys
    if (strcmp(m, "keys") == 0) {
        Array* result = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        if (tm) {
            ShapeEntry* field = tm->shape;
            while (field) {
                if (field->name) rb_arr_push(result, rb_sitem(field->name->str));
                field = field->next;
            }
        }
        return (Item){.array = result};
    }

    // .values
    if (strcmp(m, "values") == 0) {
        Array* result = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        if (tm) {
            ShapeEntry* field = tm->shape;
            while (field) {
                rb_arr_push(result, _map_read_field(field, map->data));
                field = field->next;
            }
        }
        return (Item){.array = result};
    }

    // .to_a — returns [[key, value], ...]
    if (strcmp(m, "to_a") == 0) {
        Array* result = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        if (tm) {
            ShapeEntry* field = tm->shape;
            while (field) {
                Array* pair = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
                pair->type_id = LMD_TYPE_ARRAY;
                if (field->name) rb_arr_push(pair, rb_sitem(field->name->str));
                rb_arr_push(pair, _map_read_field(field, map->data));
                rb_arr_push(result, (Item){.array = pair});
                field = field->next;
            }
        }
        return (Item){.array = result};
    }

    // .length / .size
    if (strcmp(m, "length") == 0 || strcmp(m, "size") == 0) {
        int64_t count = 0;
        if (tm) {
            ShapeEntry* field = tm->shape;
            while (field) { count++; field = field->next; }
        }
        return rb_iitem(count);
    }

    // .empty?
    if (strcmp(m, "empty?") == 0 || strcmp(m, "empty") == 0) {
        if (!tm || !tm->shape) return rb_bitem(true);
        return rb_bitem(false);
    }

    // .key?(key) / .has_key?(key) / .include?(key)
    if ((strcmp(m, "key?") == 0 || strcmp(m, "has_key?") == 0 ||
         strcmp(m, "include?") == 0 || strcmp(m, "has_key") == 0 ||
         strcmp(m, "key") == 0) && argc >= 1) {
        if (!tm) return rb_bitem(false);
        String* key = it2s(args[0]);
        if (!key) return rb_bitem(false);
        bool found = false;
        _map_get(tm, map->data, key->chars, &found);
        return rb_bitem(found);
    }

    // .value?(val) / .has_value?(val)
    if ((strcmp(m, "value?") == 0 || strcmp(m, "has_value?") == 0 ||
         strcmp(m, "has_value") == 0 || strcmp(m, "value") == 0) && argc >= 1) {
        if (!tm) return rb_bitem(false);
        ShapeEntry* field = tm->shape;
        while (field) {
            Item val = _map_read_field(field, map->data);
            if (it2b(rb_eq(val, args[0]))) return rb_bitem(true);
            field = field->next;
        }
        return rb_bitem(false);
    }

    // .fetch(key, default=nil)
    if (strcmp(m, "fetch") == 0 && argc >= 1) {
        Item val = rb_getattr(self, args[0]);
        if (get_type_id(val) != LMD_TYPE_NULL) return val;
        return (argc >= 2) ? args[1] : (Item){.item = ITEM_NULL};
    }

    // .merge(other_hash)
    if (strcmp(m, "merge") == 0 && argc >= 1 && get_type_id(args[0]) == LMD_TYPE_MAP) {
        Item result = rb_new_object();
        // copy self
        if (tm) {
            ShapeEntry* field = tm->shape;
            while (field) {
                if (field->name) rb_setattr(result, rb_sitem(field->name->str), _map_read_field(field, map->data));
                field = field->next;
            }
        }
        // merge other
        Map* other = it2map(args[0]);
        if (other && other->type) {
            TypeMap* otm = (TypeMap*)other->type;
            ShapeEntry* field = otm->shape;
            while (field) {
                if (field->name) rb_setattr(result, rb_sitem(field->name->str), _map_read_field(field, other->data));
                field = field->next;
            }
        }
        return result;
    }

    // .delete(key)
    if (strcmp(m, "delete") == 0 && argc >= 1) {
        // Lambda maps don't easily support field removal; return value and set nil
        Item val = rb_getattr(self, args[0]);
        rb_setattr(self, args[0], (Item){.item = ITEM_NULL});
        return val;
    }

    // .each_pair — alias for each (handled by transpiler for block variant)
    // .to_s
    if (strcmp(m, "to_s") == 0) return rb_to_str(self);

    log_debug("rb: hash.%s() not implemented", m);
    return RB_METHOD_NOT_FOUND;
}

// ============================================================================
// Integer methods
// ============================================================================

extern "C" Item rb_int_method(Item self, Item method_name, Item* args, int argc) {
    TypeId type = get_type_id(self);
    if (type != LMD_TYPE_INT && type != LMD_TYPE_INT64) return RB_METHOD_NOT_FOUND;
    if (get_type_id(method_name) != LMD_TYPE_STRING) return RB_METHOD_NOT_FOUND;

    String* method = it2s(method_name);
    if (!method) return RB_METHOD_NOT_FOUND;

    int64_t n = it2i(self);
    const char* m = method->chars;

    // .even?
    if (strcmp(m, "even?") == 0 || strcmp(m, "even") == 0) return rb_bitem(n % 2 == 0);

    // .odd?
    if (strcmp(m, "odd?") == 0 || strcmp(m, "odd") == 0) return rb_bitem(n % 2 != 0);

    // .zero?
    if (strcmp(m, "zero?") == 0 || strcmp(m, "zero") == 0) return rb_bitem(n == 0);

    // .positive?
    if (strcmp(m, "positive?") == 0 || strcmp(m, "positive") == 0) return rb_bitem(n > 0);

    // .negative?
    if (strcmp(m, "negative?") == 0 || strcmp(m, "negative") == 0) return rb_bitem(n < 0);

    // .abs
    if (strcmp(m, "abs") == 0) return rb_iitem(n < 0 ? -n : n);

    // .gcd(other)
    if (strcmp(m, "gcd") == 0 && argc >= 1) {
        int64_t a = n < 0 ? -n : n;
        int64_t b = it2i(args[0]);
        b = b < 0 ? -b : b;
        while (b != 0) { int64_t t = b; b = a % b; a = t; }
        return rb_iitem(a);
    }

    // .lcm(other)
    if (strcmp(m, "lcm") == 0 && argc >= 1) {
        int64_t a = n < 0 ? -n : n;
        int64_t b = it2i(args[0]);
        b = b < 0 ? -b : b;
        if (a == 0 || b == 0) return rb_iitem(0);
        int64_t ga = a, gb = b;
        while (gb != 0) { int64_t t = gb; gb = ga % gb; ga = t; }
        return rb_iitem(a / ga * b);
    }

    // .pow(exp) / .**
    if (strcmp(m, "pow") == 0 && argc >= 1) return rb_power(self, args[0]);

    // .digits — digits array (least significant first)
    if (strcmp(m, "digits") == 0) {
        int64_t v = n < 0 ? -n : n;
        int64_t base = (argc >= 1) ? it2i(args[0]) : 10;
        if (base < 2) base = 10;
        Array* result = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        if (v == 0) { rb_arr_push(result, rb_iitem(0)); }
        else {
            while (v > 0) {
                rb_arr_push(result, rb_iitem(v % base));
                v /= base;
            }
        }
        return (Item){.array = result};
    }

    // .to_s
    if (strcmp(m, "to_s") == 0) return rb_to_str(self);
    // .to_f
    if (strcmp(m, "to_f") == 0) return rb_to_float(self);
    // .to_i (identity)
    if (strcmp(m, "to_i") == 0) return self;

    // .chr — integer to character
    if (strcmp(m, "chr") == 0) {
        char buf[2] = {(char)(n & 0x7F), '\0'};
        return rb_sitem(buf);
    }

    // .between?(min, max)
    if ((strcmp(m, "between?") == 0 || strcmp(m, "between") == 0) && argc >= 2) {
        return rb_bitem(n >= it2i(args[0]) && n <= it2i(args[1]));
    }

    // .clamp(min, max)
    if (strcmp(m, "clamp") == 0 && argc >= 2) {
        int64_t lo = it2i(args[0]), hi = it2i(args[1]);
        if (n < lo) return rb_iitem(lo);
        if (n > hi) return rb_iitem(hi);
        return self;
    }

    log_debug("rb: integer.%s() not implemented", m);
    return RB_METHOD_NOT_FOUND;
}

// ============================================================================
// Float methods
// ============================================================================

extern "C" Item rb_float_method(Item self, Item method_name, Item* args, int argc) {
    if (get_type_id(self) != LMD_TYPE_FLOAT) return RB_METHOD_NOT_FOUND;
    if (get_type_id(method_name) != LMD_TYPE_STRING) return RB_METHOD_NOT_FOUND;

    String* method = it2s(method_name);
    if (!method) return RB_METHOD_NOT_FOUND;

    double val = it2d(self);
    const char* m = method->chars;

    // .round(ndigits=0)
    if (strcmp(m, "round") == 0) {
        int digits = (argc >= 1) ? (int)it2i(args[0]) : 0;
        if (digits == 0) return rb_iitem((int64_t)round(val));
        double factor = pow(10.0, digits);
        return rb_ditem(round(val * factor) / factor);
    }

    // .floor
    if (strcmp(m, "floor") == 0) {
        if (argc >= 1) {
            int digits = (int)it2i(args[0]);
            double factor = pow(10.0, digits);
            return rb_ditem(floor(val * factor) / factor);
        }
        return rb_iitem((int64_t)floor(val));
    }

    // .ceil
    if (strcmp(m, "ceil") == 0) {
        if (argc >= 1) {
            int digits = (int)it2i(args[0]);
            double factor = pow(10.0, digits);
            return rb_ditem(ceil(val * factor) / factor);
        }
        return rb_iitem((int64_t)ceil(val));
    }

    // .truncate
    if (strcmp(m, "truncate") == 0) {
        return rb_iitem((int64_t)val);
    }

    // .abs
    if (strcmp(m, "abs") == 0) return rb_ditem(fabs(val));

    // .nan?
    if (strcmp(m, "nan?") == 0 || strcmp(m, "nan") == 0) return rb_bitem(isnan(val));

    // .infinite?
    if (strcmp(m, "infinite?") == 0 || strcmp(m, "infinite") == 0) {
        if (isinf(val)) return rb_iitem(val > 0 ? 1 : -1);
        return (Item){.item = ITEM_NULL};
    }

    // .zero?
    if (strcmp(m, "zero?") == 0 || strcmp(m, "zero") == 0) return rb_bitem(val == 0.0);

    // .positive?
    if (strcmp(m, "positive?") == 0 || strcmp(m, "positive") == 0) return rb_bitem(val > 0.0);

    // .negative?
    if (strcmp(m, "negative?") == 0 || strcmp(m, "negative") == 0) return rb_bitem(val < 0.0);

    // .to_s
    if (strcmp(m, "to_s") == 0) return rb_to_str(self);
    // .to_f (identity)
    if (strcmp(m, "to_f") == 0) return self;
    // .to_i
    if (strcmp(m, "to_i") == 0) return rb_iitem((int64_t)val);

    // .between?(min, max)
    if ((strcmp(m, "between?") == 0 || strcmp(m, "between") == 0) && argc >= 2) {
        double lo = it2d(rb_to_float(args[0]));
        double hi = it2d(rb_to_float(args[1]));
        return rb_bitem(val >= lo && val <= hi);
    }

    // .clamp(min, max)
    if (strcmp(m, "clamp") == 0 && argc >= 2) {
        double lo = it2d(rb_to_float(args[0]));
        double hi = it2d(rb_to_float(args[1]));
        if (val < lo) return rb_ditem(lo);
        if (val > hi) return rb_ditem(hi);
        return self;
    }

    log_debug("rb: float.%s() not implemented", m);
    return RB_METHOD_NOT_FOUND;
}
