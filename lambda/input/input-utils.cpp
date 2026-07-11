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
#include <ctype.h>
#include <errno.h>
#include "../../lib/mem.h"
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
        if (!isxdigit((unsigned char)(*pos)[i])) return 0xFFFFFFFF;
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
