/**
 * @file input-utils.cpp
 * @brief Implementation of shared utilities for Lambda input parsers
 */

#include "input-utils.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/str.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// ── Unicode Utilities ──────────────────────────────────────────────

int codepoint_to_utf8(uint32_t codepoint, char out[5]) {
    if (!out) return 0;

    if (codepoint < 0x80) {
        out[0] = (char)codepoint;
        out[1] = '\0';
        return 1;
    } else if (codepoint < 0x800) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        out[2] = '\0';
        return 2;
    } else if (codepoint < 0x10000) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        out[3] = '\0';
        return 3;
    } else if (codepoint < 0x110000) {
        out[0] = (char)(0xF0 | (codepoint >> 18));
        out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[3] = (char)(0x80 | (codepoint & 0x3F));
        out[4] = '\0';
        return 4;
    }

    // invalid codepoint
    out[0] = '\0';
    return 0;
}

uint32_t decode_surrogate_pair(uint16_t high, uint16_t low) {
    if (high < 0xD800 || high > 0xDBFF) return 0;
    if (low  < 0xDC00 || low  > 0xDFFF) return 0;
    return 0x10000 + ((uint32_t)(high - 0xD800) << 10) + (low - 0xDC00);
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

// ── Numeric Parsing ────────────────────────────────────────────────

bool try_parse_int64(const char* str, size_t len, int64_t* out) {
    if (!str || len == 0 || !out) return false;

    // stack buffer for null termination (avoid heap allocation)
    char buf[64];
    if (len >= sizeof(buf)) return false;
    memcpy(buf, str, len);
    buf[len] = '\0';

    char* end;
    long long val = strtoll(buf, &end, 10);
    if (end != buf + len) return false;

    *out = (int64_t)val;
    return true;
}

bool try_parse_double(const char* str, size_t len, double* out) {
    if (!str || len == 0 || !out) return false;

    char buf[128];
    if (len >= sizeof(buf)) return false;
    memcpy(buf, str, len);
    buf[len] = '\0';

    char* end;
    double val = strtod(buf, &end);
    if (end != buf + len) return false;

    *out = val;
    return true;
}

// ── String Classification ──────────────────────────────────────────

int input_strncasecmp(const char* s1, const char* s2, size_t n) {
    return str_icmp(s1, n, s2, n) != 0;
}

// ── C++ Typed Value Parsing ────────────────────────────────────────

namespace lambda {

Item parse_typed_value(InputContext& ctx, const char* str, size_t len) {
    if (!str || len == 0) {
        return {.item = ITEM_NULL};
    }

    Input* input = ctx.input();

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

    // check if it looks like a number
    bool is_number = true;
    bool has_dot = false;
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (i == 0 && (c == '-' || c == '+')) continue;
        if (c == '.' && !has_dot) { has_dot = true; continue; }
        if (c == 'e' || c == 'E') {
            if (i + 1 < len && (str[i + 1] == '+' || str[i + 1] == '-')) i++;
            continue;
        }
        if (!isdigit((unsigned char)c)) { is_number = false; break; }
    }

    if (is_number && len > 0) {
        if (has_dot || memchr(str, 'e', len) || memchr(str, 'E', len)) {
            double dval;
            if (try_parse_double(str, len, &dval)) {
                double* dval_ptr = (double*)pool_calloc(input->pool, sizeof(double));
                if (dval_ptr) {
                    *dval_ptr = dval;
                    return {.item = d2it(dval_ptr)};
                }
            }
        } else {
            int64_t lval;
            if (try_parse_int64(str, len, &lval)) {
                int64_t* lval_ptr = (int64_t*)pool_calloc(input->pool, sizeof(int64_t));
                if (lval_ptr) {
                    *lval_ptr = lval;
                    return {.item = l2it(lval_ptr)};
                }
            }
        }
    }

    // fallback: return as string
    String* s = ctx.builder.createString(str, len);
    return {.item = s2it(s)};
}

} // namespace lambda
