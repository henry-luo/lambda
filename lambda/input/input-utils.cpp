/**
 * @file input-utils.cpp
 * @brief Implementation of shared utilities for Lambda input parsers
 */

#include "input-utils.hpp"
#include "../lambda-data.hpp"
#include "../lambda-decimal.hpp"
#include "../../lib/log.h"
#include "../../lib/str.h"
#include "../../lib/utf.h"
#include <errno.h>
#include "../../lib/mem.h"
#include <limits.h>
#include <string.h>

// ── Unicode Utilities ──────────────────────────────────────────────

int codepoint_to_utf8(uint32_t codepoint, char out[5]) {
    if (!out) return 0;
    size_t n = utf8_encode_z(codepoint, out);
    return (int)n;
}

uint32_t decode_surrogate_pair(uint16_t high, uint16_t low) {
    return utf16_decode_pair(high, low);
}

uint32_t parse_hex_codepoint(const char** pos, int ndigits) {
    if (!pos || !*pos) return 0xFFFFFFFF;

    for (int i = 0; i < ndigits; i++) {
        if (!str_is_hex((*pos)[i])) return 0xFFFFFFFF;
    }

    char buf[9] = {0};
    int n = ndigits > 8 ? 8 : ndigits;
    memcpy(buf, *pos, n);
    uint32_t val = (uint32_t)strtoul(buf, NULL, 16);
    *pos += n;
    return val;
}

// ── C++ Typed Value Parsing ────────────────────────────────────────

namespace lambda {

bool scanned_number_has_float_marker(const char* str, size_t len) {
    if (!str) return false;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '.' || str[i] == 'e' || str[i] == 'E') return true;
    }
    return false;
}

Item parse_prefixed_integer_value(InputContext& ctx, const char* str, int base,
                                  const char* kind, SourceLocation loc,
                                  const char** end_out, bool report_errors,
                                  bool force_long) {
    if (end_out) *end_out = str;
    if (!str || !str[0] || !str[1]) return ItemNull;

    const char* digits = str + 2;
    char* end = nullptr;
    errno = 0;
    int64_t val = strtoll(digits, &end, base);
    if (end_out) *end_out = end;
    if (end == digits) {
        if (report_errors) {
            ctx.addError(loc, "Invalid %s number: no digits after 0%c", kind, str[1]);
        }
        return report_errors ? (Item){.item = ITEM_ERROR} : ItemNull;
    }
    if (errno == ERANGE) {
        if (report_errors) {
            ctx.addError(loc, "Invalid %s number: value out of int64 range", kind);
        }
        return report_errors ? (Item){.item = ITEM_ERROR} : ItemNull;
    }

    if (force_long || val < INT56_MIN || val > INT56_MAX) {
        return ctx.builder.createLong(val);
    }
    return ctx.builder.createInt(val);
}

static bool scanned_number_is_negative_zero(const char* str, size_t len) {
    if (!str || len < 2 || str[0] != '-') return false;
    for (size_t i = 1; i < len; i++) {
        if (str[i] == '_') continue;
        if (str[i] != '0') return false;
    }
    return true;
}

static bool copy_scanned_number_token(const char* str, size_t len, bool allow_underscores,
                                      char** out, size_t* out_len, bool* heap_buf) {
    if (!str || !out || !out_len || !heap_buf) return false;
    *out = nullptr;
    *out_len = 0;
    *heap_buf = false;

    bool needs_copy = allow_underscores;
    if (!needs_copy) {
        *out = (char*)str;
        *out_len = len;
        return true;
    }

    char* clean = (char*)mem_alloc(len + 1, MEM_CAT_INPUT_OTHER);
    if (!clean) return false;
    size_t clean_len = 0;
    for (size_t i = 0; i < len; i++) {
        if (str[i] != '_') clean[clean_len++] = str[i];
    }
    clean[clean_len] = '\0';
    *out = clean;
    *out_len = clean_len;
    *heap_buf = true;
    return true;
}

Item parse_integer_token_exact(InputContext& ctx, const char* str, size_t len) {
    if (!str || len == 0) return ItemNull;

    char stack_buf[128];
    char* num_str = stack_buf;
    bool heap_buf = false;
    if (len >= sizeof(stack_buf)) {
        num_str = (char*)mem_alloc(len + 1, MEM_CAT_INPUT_OTHER);
        if (!num_str) return ItemNull;
        heap_buf = true;
    }
    memcpy(num_str, str, len);
    num_str[len] = '\0';

    char* end;
    errno = 0;
    int64_t int_value = strtoll(num_str, &end, 10);
    if (errno == 0 && end == num_str + len) {
        // integer input climbs int -> int64 before decimal; avoid double as an intermediate.
        Item result = (int_value >= INT56_MIN && int_value <= INT56_MAX) ?
            ctx.builder.createInt(int_value) : ctx.builder.createLong(int_value);
        if (heap_buf) mem_free(num_str);
        return result;
    }

    Item decimal_item = decimal_from_integer_string_arena(num_str, ctx.builder.arena());
    if (heap_buf) mem_free(num_str);
    return decimal_item;
}

Item parse_scanned_decimal_number(InputContext& ctx, const char* str, size_t len,
                                  bool allow_underscores, bool negative_zero_is_float) {
    if (!str || len == 0) return ItemNull;

    char* clean = nullptr;
    size_t clean_len = 0;
    bool heap_buf = false;
    if (!copy_scanned_number_token(str, len, allow_underscores, &clean, &clean_len, &heap_buf)) {
        return ItemNull;
    }
    if (clean_len == 0) {
        if (heap_buf) mem_free(clean);
        return ItemNull;
    }

    Item result = ItemNull;
    if (scanned_number_has_float_marker(clean, clean_len) ||
        (negative_zero_is_float && scanned_number_is_negative_zero(clean, clean_len))) {
        const char* end = nullptr;
        double value = 0.0;
        if (str_to_double(clean, clean_len, &value, &end) && end == clean + clean_len) {
            result = ctx.builder.createFloat(value);
        }
    } else {
        result = parse_integer_token_exact(ctx, clean, clean_len);
    }

    if (heap_buf) mem_free(clean);
    return result;
}

Item parse_typed_value(InputContext& ctx, const char* str, size_t len) {
    if (!str || len == 0) {
        return {.item = ITEM_NULL};
    }

    // check for boolean values (case insensitive)
    if ((len == 4 && str_ieq_const(str, len, "true")) ||
        (len == 3 && str_ieq_const(str, len, "yes")) ||
        (len == 2 && str_ieq_const(str, len, "on")) ||
        (len == 1 && str[0] == '1')) {
        return {.item = b2it(true)};
    }
    if ((len == 5 && str_ieq_const(str, len, "false")) ||
        (len == 2 && str_ieq_const(str, len, "no")) ||
        (len == 3 && str_ieq_const(str, len, "off")) ||
        (len == 1 && str[0] == '0')) {
        return {.item = b2it(false)};
    }

    // check for null/empty values
    if ((len == 4 && str_ieq_const(str, len, "null")) ||
        (len == 3 && str_ieq_const(str, len, "nil")) ||
        (len == 5 && str_ieq_const(str, len, "empty"))) {
        return {.item = ITEM_NULL};
    }

    const char* end = nullptr;
    int64_t ignored_int = 0;
    if (str_to_int64(str, len, &ignored_int, &end) && end == str + len) {
        Item int_item = parse_integer_token_exact(ctx, str, len);
        if (int_item.item != ITEM_NULL) return int_item;
    }

    double dval = 0.0;
    if (str_to_double(str, len, &dval, &end) && end == str + len) {
        return ctx.builder.createFloat(dval);
    }

    // fallback: return as string
    String* s = ctx.builder.createString(str, len);
    return {.item = s2it(s)};
}

} // namespace lambda

// ── Whitespace & Line Utilities (C linkage) ────────────────────────

extern "C" void skip_whitespace(const char **text) {
    while (**text && (**text == ' ' || **text == '\n' || **text == '\r' || **text == '\t')) {
        (*text)++;
    }
}

extern "C" void skip_tab_pace(const char **text) {
    while (**text && (**text == ' ' || **text == '\t')) {
        (*text)++;
    }
}

bool input_is_whitespace_char(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool input_is_empty_line(const char* line) {
    while (*line) {
        if (!str_char_is_ascii_space(*line)) return false;
        line++;
    }
    return true;
}

int input_count_leading_chars(const char* str, char ch) {
    int count = 0;
    while (str[count] == ch) count++;
    return count;
}
