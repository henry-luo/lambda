#include "mvp_lmd_runtime.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/heap_api.h"
#include "../../../lib/utf.h"
#include "../../../lib/str.h"
#include <math.h>

static int64_t string_bound(double value, int64_t length, bool relative) {
    if (isnan(value)) return 0;
    value = trunc(value);
    if (relative && value < 0) value += (double)length;
    return value <= 0 ? 0 : value >= (double)length ? length : (int64_t)value;
}
extern "C" Item mvp_lmd_string_range(Item value, double start, double end, int64_t slice) {
    String* string = value.get_string();
    int64_t length = string->is_ascii ? string->len : utf8_to_utf16_length(string->chars, string->len);
    int64_t first = string_bound(start, length, slice), last = string_bound(end, length, slice);
    if (!slice && first > last) { int64_t swap = first; first = last; last = swap; }
    if (last < first) last = first;
    if (string->is_ascii) return fn_substring(value, Item{.item = i2it(first)}, Item{.item = i2it(last)});
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> source(roots, value);
    size_t size = utf8_canonical_slice(string->chars, string->len, first, last - first, NULL);
    if (size > INT_MAX - sizeof(String) - 1) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
    String* result = (String*)heap_alloc(sizeof(String) + size + 1, LMD_TYPE_STRING);
    if (!result) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    string = source.get().get_string();
    utf8_canonical_slice(string->chars, string->len, first, last - first, result->chars);
    result->len = size; result->flags = 0;
    result->is_ascii = str_is_ascii(result->chars, size); result->chars[size] = 0;
    return Item{.item = s2it(result)};
}
extern "C" int64_t mvp_lmd_string_search(Item value, Item needle, double start, int64_t prefix) {
    String* string = value.get_string(); String* pattern = needle.get_string();
    int64_t length = string->is_ascii ? string->len : utf8_to_utf16_length(string->chars, string->len);
    int64_t first = string_bound(start, length, false);
    if (prefix == 2) {
        first -= pattern->is_ascii ? pattern->len : utf8_to_utf16_length(pattern->chars, pattern->len);
        if (first < 0) return false;
    }
    int64_t result;
    if (string->is_ascii && pattern->is_ascii) {
        if (!first) result = fn_index_of_raw(value, needle);
        else {
            size_t found = str_find(string->chars + first, string->len - first, pattern->chars, pattern->len);
            result = found == STR_NPOS ? -1 : first + found;
        }
    } else result = utf16_find(string->chars, string->len, pattern->chars, pattern->len, first);
    return prefix ? result == first : result;
}
extern "C" Item mvp_lmd_string_pad(Item string, double length, Item filler) {
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> source(roots, string), fill(roots, filler), repeated(roots, ItemNull);
    int64_t current = utf8_to_utf16_length(string.get_string()->chars, string.get_string()->len);
    length = isnan(length) || length < 0 ? 0 : trunc(length);
    if (length <= current) return source.get();
    fill.set(filler.item == ITEM_JS_UNDEFINED ? Item{.item = s2it(get_ascii_char_string(' '))} : mvp_lmd_primitive_to_string(filler));
    if (item_is_error(fill.get())) return fill.get();
    int64_t width = utf8_to_utf16_length(fill.get().get_string()->chars, fill.get().get_string()->len);
    if (!width) return source.get();
    if (!isfinite(length) || length > INT_MAX - sizeof(String) - 1) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
    int64_t missing = (int64_t)length - current;
    String* text = str_repeat(fill.get().get_string(), (missing + width - 1) / width);
    if (!text) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    repeated.set(Item{.item = s2it(text)});
    repeated.set(mvp_lmd_string_range(repeated.get(), 0, missing, 1));
    return item_is_error(repeated.get()) ? repeated.get() : mvp_lmd_string_concat(repeated.get(), source.get());
}
extern "C" Item mvp_lmd_string_split(Item value, Item separator, int64_t limit) {
    String* string = value.get_string();
    bool absent = separator.item == ITEM_JS_UNDEFINED;
    String* delimiter = absent ? NULL : separator.get_string();
    if (limit == UINT32_MAX && delimiter && string->is_ascii && delimiter->is_ascii &&
            string->len && delimiter->len) return fn_split_literal_items(value, separator);
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> source(roots, value), sep(roots, separator), result(roots, ItemNull);
    result.set(Item{.array = array()});
    if (!result.get().array) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    if (!limit) return result.get();
    if (absent) { array_push(result.get().array, source.get()); return result.get(); }
    int64_t size = utf8_to_utf16_length(string->chars, string->len);
    int64_t width = utf8_to_utf16_length(delimiter->chars, delimiter->len);
    int64_t first = 0;
    while (result.get().array->length < limit) {
        if (!width && first == size) break;
        int64_t found = width ? mvp_lmd_string_search(source.get(), sep.get(), first, 0) : first + 1;
        Item part = mvp_lmd_string_range(source.get(), first, found < 0 ? size : found, true);
        if (item_is_error(part)) return part;
        array_push(result.get().array, part);
        if (found < 0) break;
        first = found + width;
    }
    return result.get();
}
