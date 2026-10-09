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
    char message[96];
    snprintf(message, sizeof(message), "%s at byte %lld", messages[kind], (long long)site);
    LambdaError* error = err_create_heap(codes[kind], message, &location);
    return error ? err2it(error) : Item{.item = ITEM_ERROR};
}

static bool number_whitespace(uint16_t c) {
    return c == 0x20 || (c >= 9 && c <= 13) || c == 0xa0 || c == 0xfeff ||
        c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 ||
        c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}

static int radix_digit(unsigned char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 :
        c >= 'A' && c <= 'Z' ? c - 'A' + 10 : 36;
}
static double parse_radix_digits(const char* text, size_t length, int radix) {
    if (!length) return NAN;
    if (radix & (radix - 1)) {
        double result = 0;
        for (size_t i = 0; i < length; i++) {
            int digit = radix_digit((unsigned char)text[i]);
            if (digit >= radix) return NAN;
            result = result * radix + digit;
        }
        return result;
    }
    int width = 0;
    for (int base = radix; base > 1; base >>= 1) width++;
    uint64_t significant_bits = 0, prefix = 0;
    bool sticky = false;
    for (size_t i = 0; i < length; i++) {
        int digit = radix_digit((unsigned char)text[i]);
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
    uint64_t mantissa = prefix >> 1;
    if ((prefix & 1) && (sticky || (mantissa & 1))) mantissa++;
    return significant_bits > 1024 ? INFINITY : ldexp((double)mantissa, (int)significant_bits - 53);
}

extern "C" double mvp_lmd_parse_integer(String* string, int64_t radix) {
    if (radix && (radix < 2 || radix > 36)) return NAN;
    Utf16Iterator iter = {(const unsigned char*)string->chars, string->len, 0, -1};
    uint16_t unit; size_t start = 0;
    while (utf16_iterator_next(&iter, &unit) && number_whitespace(unit)) start = iter.pos;
    const char* text = string->chars + start;
    size_t length = string->len - start, first = 0;
    bool negative = length && text[0] == '-';
    if (length && (text[0] == '+' || text[0] == '-')) first++;
    if ((!radix || radix == 16) && length - first >= 2 && text[first] == '0' &&
            (text[first + 1] == 'x' || text[first + 1] == 'X')) { first += 2; radix = 16; }
    if (!radix) radix = 10;
    size_t end = first;
    while (end < length && radix_digit((unsigned char)text[end]) < radix) end++;
    if (end == first) return NAN;
    double result;
    if (radix == 10) {
        if (!str_to_double(text + first, end - first, &result, NULL)) return NAN;
    } else result = parse_radix_digits(text + first, end - first, (int)radix);
    return negative ? -result : result;
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
            return parse_radix_digits(s + 2, n - 2, radix);
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

extern "C" Item mvp_lmd_number_to_fixed(double value, double digits) {
    digits = isnan(digits) ? 0 : trunc(digits);
    if (digits < 0 || digits > 100) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
    if (!isfinite(value) || fabs(value) >= 1e21) return mvp_lmd_number_to_string(value);
    char buffer[128];
    if (!lambda_finite_double_to_fixed(value, (int)digits, true, buffer, sizeof(buffer)))
        return mvp_lmd_fail(LMD_MVP_RANGE, 0);
    String* result = heap_strcpy(buffer, strlen(buffer));
    return result ? Item{.item = s2it(result)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}

extern "C" Item mvp_lmd_primitive_to_string(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_STRING) return value;
    if (type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT) return mvp_lmd_number_to_string(it2d(value));
    const char* text = type == LMD_TYPE_NULL ? "null" : type == LMD_TYPE_UNDEFINED ? "undefined" :
        type == LMD_TYPE_BOOL ? (value.bool_val ? "true" : "false") : NULL;
    if (!text) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    String* result = heap_strcpy(text, strlen(text));
    return result ? Item{.item = s2it(result)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}

extern "C" Item mvp_lmd_string_concat(Item left, Item right) {
    // audited MIR callers precisely root both arguments across the shared join's allocation.
    // strings are immutable; the shared buffer join may reuse only exclusive buffers.
    String* joined = fn_strcat(fn_string_freeze(left.get_string()),
        fn_string_freeze(right.get_string()));
    if (!joined || joined == &STR_ERROR) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    return Item{.item = s2it(fn_string_freeze(joined))};
}

extern "C" int64_t mvp_lmd_string_compare(Item left, Item right) {
    String* l = left.get_string();
    String* r = right.get_string();
    if (l == r) return 0;
    // ASCII byte order is UTF-16 order; retain the shared decoder for all other strings.
    if (l->is_ascii && r->is_ascii) return str_cmp(l->chars, l->len, r->chars, r->len);
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
extern "C" Item mvp_lmd_array_resize(Item owner, int64_t length) {
    if (length < 0 || length > UINT32_MAX) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, owner);
    int64_t old_length = owner.array->length;
    if (length > old_length) {
        if (!array_reserve_append_slots(held.get().array, length - old_length))
            return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        // shrinking then regrowing must never resurrect removed elements or scalar homes.
        for (int64_t i = old_length; i < length; i++)
            held.get().array->items[i].item = ITEM_JS_DELETED_SENTINEL;
    }
    held.get().array->length = length;
    return held.get();
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

// keep scalar snapshots, stack checks and roots off immediate in-capacity stores.
__attribute__((noinline))
static Item array_store_owned(Item owner, uint32_t index, Item value) {
    RootFrame roots(2);
    if (!roots.valid()) return Item{.item = ITEM_ERROR};
    // growth may relocate the source array's scalar tail as well as the destination.
    uint64_t home = 0;
    Rooted<Item> a(roots, owner), v(roots, lambda_item_adopt_scalar_home(value, &home));
    Array* array = (Array*)a.get().item;
    bool wide = lambda_item_uses_scalar_home(value);
    if (wide && array->extra > 2 * array->length) {
        // retyping retires homes; a shared owned copy bounds tail storage with amortized linear work.
        Item packed = fn_slice(a.get(), Item{.item = i2it(0)}, Item{.item = i2it(array->length)});
        if (item_is_error(packed)) return packed;
        array = a.get().array;
        array->items = packed.array->items;
        array->capacity = packed.array->capacity;
        array->extra = packed.array->extra;
    }
    int64_t end_offset = 0;
    if (wide && index < (uint64_t)array->length) {
        Item previous = array->items[index];
        if (lambda_item_uses_scalar_home(previous)) {
            uintptr_t pointer = previous.double_ptr;
            uintptr_t end = (uintptr_t)(array->items + array->capacity);
            uintptr_t start = (uintptr_t)(array->items + array->capacity - array->extra);
            // reverse and shared copies move elements independently of their owned scalar homes.
            if (pointer >= start && pointer < end && (end - pointer) % sizeof(Item) == 0)
                end_offset = (end - pointer) / sizeof(Item);
        }
    }
    bool append_home = wide && !end_offset;
    int64_t growth = index >= (uint64_t)array->length ? (int64_t)index + 1 - array->length : 0;
    if (!array_reserve_append_slots(array, append_home + growth))
        return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    array = (Array*)a.get().item;
    for (int64_t i = array->length; i < index; i++) array->items[i].item = ITEM_JS_DELETED_SENTINEL;
    if (append_home) end_offset = ++array->extra;
    Item stored = wide ? lambda_item_adopt_scalar_home(v.get(),
        (uint64_t*)&array->items[array->capacity - end_offset]) : v.get();
    array->items[index] = stored;
    if (index >= array->length) array->length = (int64_t)index + 1;
    return stored;
}

extern "C" Item mvp_lmd_array_store(Item owner, uint32_t index, Item value) {
    Array* immediate = owner.array;
    if (!lambda_item_uses_scalar_home(value) && index <= (uint64_t)immediate->length &&
            index < (uint64_t)(immediate->capacity - immediate->extra)) {
        immediate->items[index] = value;
        if (index == immediate->length) immediate->length++;
        return value;
    }
    return array_store_owned(owner, index, value);
}
