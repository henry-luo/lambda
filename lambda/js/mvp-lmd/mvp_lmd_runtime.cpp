#include "mvp_lmd_runtime.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/lambda-error.h"
#include "../../core/lambda-decimal.hpp"
#include "../../../lib/utf.h"
#include "../../../lib/str.h"

extern "C" Item mvp_lmd_fail(int64_t kind, int64_t site) {
    static const char* messages[] = {"MVP capability error", "ReferenceError",
        "TypeError", "RangeError", "MVP allocation failed"};
    static const LambdaErrorCode codes[] = {ERR_NOT_IMPLEMENTED,
        ERR_UNDEFINED_VARIABLE, ERR_TYPE_MISMATCH, ERR_INDEX_OUT_OF_BOUNDS,
        ERR_POOL_EXHAUSTED};
    SourceLocation location = {};
    location.column = (int)site + 1;
    if (kind < 0 || kind > LMD_MVP_MEMORY) kind = LMD_MVP_CAPABILITY;
    LambdaError* error = err_create_heap(codes[kind], messages[kind], &location);
    return error ? err2it(error) : Item{.item = ITEM_ERROR};
}

static bool number_whitespace(uint16_t c) {
    return c == 0x20 || (c >= 9 && c <= 13) || c == 0xa0 || c == 0xfeff ||
        c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 ||
        c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}

extern "C" double mvp_lmd_string_to_number(String* string) {
    Utf16Iterator iter = {(const unsigned char*)string->chars, string->len, 0, -1};
    int64_t start = -1, end = 0;
    uint16_t unit;
    while (true) {
        int64_t before = iter.pos;
        if (!utf16_iterator_next(&iter, &unit)) break;
        if (!number_whitespace(unit)) {
            if (start < 0) start = before;
            end = iter.pos;
        }
    }
    if (start < 0) return 0.0;
    const char* s = string->chars + start;
    size_t n = (size_t)(end - start), i = 0;
    if (s[i] == '+' || s[i] == '-') i++;
    if (n - i == 8 && memcmp(s + i, "Infinity", 8) == 0)
        return s[0] == '-' ? -INFINITY : INFINITY;
    if (i == 0 && n >= 2 && s[0] == '0') {
        int radix = s[1] == 'x' || s[1] == 'X' ? 16 :
            s[1] == 'o' || s[1] == 'O' ? 8 : s[1] == 'b' || s[1] == 'B' ? 2 : 0;
        if (radix) {
            if (n == 2) return NAN;
            uint64_t significant_bits = 0, prefix = 0;
            bool sticky = false;
            int width = radix == 16 ? 4 : radix == 8 ? 3 : 1;
            for (i = 2; i < n; i++) {
                unsigned char c = (unsigned char)s[i];
                int digit = c >= '0' && c <= '9' ? c - '0' :
                    c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                    c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
                if (digit >= radix) return NAN;
                for (int bit = width - 1; bit >= 0; bit--) {
                    bool one = (digit >> bit) & 1;
                    if (!significant_bits && !one) continue;
                    if (significant_bits < 54) prefix = (prefix << 1) | one;
                    else sticky |= one;
                    significant_bits++;
                }
            }
            if (significant_bits <= 53) return (double)prefix;
            // round once: repeated double accumulation loses guard/sticky bits at radix boundaries.
            uint64_t mantissa = prefix >> 1;
            if ((prefix & 1) && (sticky || (mantissa & 1))) mantissa++;
            if (significant_bits > 1024) return INFINITY;
            return ldexp((double)mantissa, (int)significant_bits - 53);
        }
    }
    size_t digits = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9') { i++; digits++; }
    if (i < n && s[i] == '.') {
        i++;
        while (i < n && s[i] >= '0' && s[i] <= '9') { i++; digits++; }
    }
    if (!digits) return NAN;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-')) i++;
        size_t exponent = i;
        while (i < n && s[i] >= '0' && s[i] <= '9') i++;
        if (i == exponent) return NAN;
    }
    if (i != n) return NAN;
    double result;
    return str_to_double(s, n, &result, NULL) ? result : NAN;
}

extern "C" Item mvp_lmd_number_to_string(double value) {
    char buffer[64];
    const char* text = buffer;
    if (isnan(value)) text = "NaN";
    else if (isinf(value)) text = value < 0 ? "-Infinity" : "Infinity";
    else lambda_finite_double_to_shortest(value, buffer, sizeof(buffer));
    String* string = heap_strcpy(text, strlen(text));
    return string ? Item{.item = s2it(string)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}

extern "C" Item mvp_lmd_string_concat(Item left, Item right) {
    RootFrame roots(2);
    if (!roots.valid()) return Item{.item = ITEM_ERROR};
    Rooted<Item> l(roots, left), r(roots, right);
    // strings are immutable; the shared buffer join may reuse only exclusive buffers.
    String* joined = fn_strcat(fn_string_freeze(l.get().get_string()),
        fn_string_freeze(r.get().get_string()));
    if (!joined || joined == &STR_ERROR) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    return Item{.item = s2it(fn_string_freeze(joined))};
}

extern "C" int64_t mvp_lmd_string_compare(Item left, Item right) {
    String* l = left.get_string();
    String* r = right.get_string();
    return utf16_compare(l->chars, l->len, r->chars, r->len);
}

extern "C" Item mvp_lmd_string_at(Item value, double index, int64_t mode) {
    uint16_t unit = 0;
    bool present = mode == LMD_STRING_FROM_CODE;
    if (present) unit = (uint16_t)(uint32_t)index;
    else {
        String* string = value.get_string();
        index = isnan(index) ? 0 : trunc(index);
        if (index >= 0 && index < string->len) {
            if (string->is_ascii) { unit = (uint8_t)string->chars[(uint32_t)index]; present = true; }
            else {
                Utf16Iterator iter = {(const unsigned char*)string->chars, string->len, 0, -1};
                for (uint64_t i = 0; i <= (uint64_t)index; i++) {
                    present = utf16_iterator_next(&iter, &unit);
                    if (!present) break;
                }
            }
        }
    }
    if (mode == LMD_STRING_CODE) return Item{.item = present ? i2it(unit) : LAMBDA_IEEE_NAN_BITS};
    if (!present && mode == LMD_STRING_INDEX) return Item{.item = ITEM_JS_UNDEFINED};
    // share Lambda's immutable character table; UTF-16 non-ASCII units retain WTF-8 allocation.
    if (present && unit < 128) {
        String* shared = get_ascii_char_string((unsigned char)unit);
        if (shared) return Item{.item = s2it(shared)};
    }
    char bytes[4];
    size_t length = present ? utf8_encode_wtf8(unit, bytes) : 0;
    String* result = heap_strcpy(bytes, length);
    return result ? Item{.item = s2it(result)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}

extern "C" Item mvp_lmd_array_new(int64_t length) {
    if (length < 0 || length > UINT32_MAX) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Array*> result(roots, array());
    if (!result.get() || !array_reserve_append_slots(result.get(), length))
        return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    // reuse the shared non-value sentinel; explicit undefined remains an own element.
    for (int64_t i = 0; i < length; i++) result.get()->items[i].item = ITEM_JS_DELETED_SENTINEL;
    result.get()->length = length;
    return Item{.array = result.get()};
}

extern "C" double mvp_lmd_number_pow(double base, double exponent) {
    if (exponent == 0) return 1;
    if (isnan(exponent) || isnan(base)) return NAN;
    if (isinf(exponent) && fabs(base) == 1) return NAN;
    return pow(base, exponent);
}

extern "C" int64_t mvp_lmd_string_key(String* s, int64_t typed) {
    if (s->len == 6 && memcmp(s->chars, "length", 6) == 0) return -1;
    if (typed) {
        if (s->len == 4 && memcmp(s->chars, "fill", 4) == 0) return -4;
        if (s->len == 2 && memcmp(s->chars, "-0", 2) == 0) return -3;
        double value = mvp_lmd_string_to_number(s);
        char buffer[64];
        const char* canonical = buffer;
        if (isnan(value)) canonical = "NaN";
        else if (isinf(value)) canonical = value < 0 ? "-Infinity" : "Infinity";
        else lambda_finite_double_to_shortest(value, buffer, sizeof(buffer));
        if (s->len != strlen(canonical) || memcmp(s->chars, canonical, s->len)) return -2;
        // canonical but invalid indices must not become ordinary named properties.
        return value >= 0 && value <= INT53_MAX && value == trunc(value) ? (int64_t)value : -3;
    }
    if (!s->len || s->len > 10 || (s->len > 1 && s->chars[0] == '0')) return -2;
    uint64_t index = 0;
    for (uint32_t i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->chars[i];
        if (c < '0' || c > '9') return -2;
        index = index * 10 + c - '0';
    }
    return index < UINT32_MAX ? (int64_t)index : -2;
}

extern "C" Item mvp_lmd_array_store(Item owner, uint32_t index, Item value) {
    Array* immediate = owner.array;
    if (!lambda_item_uses_scalar_home(value) && index <= (uint64_t)immediate->length &&
            index < (uint64_t)(immediate->capacity - immediate->extra)) {
        immediate->items[index] = value;
        if (index == immediate->length) immediate->length++;
        return value;
    }
    RootFrame roots(2);
    if (!roots.valid()) return Item{.item = ITEM_ERROR};
    Rooted<Item> a(roots, owner), v(roots, value);
    Array* array = (Array*)a.get().item;
    if (index > (uint64_t)array->length) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    // a slot's tail home is reusable across overwrites; growth rebases it through shared storage.
    bool wide = lambda_item_uses_scalar_home(value);
    int64_t extra = wide && index >= array->extra ? (int64_t)index + 1 : array->extra;
    if (!array_reserve_append_slots(array, extra - array->extra + (index == array->length)))
        return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    array = (Array*)a.get().item;
    array->extra = extra;
    Item stored = wide ? lambda_item_adopt_scalar_home(v.get(),
        (uint64_t*)&array->items[array->capacity - index - 1]) : v.get();
    array->items[index] = stored;
    if (index == array->length) array->length++;
    return v.get();
}
