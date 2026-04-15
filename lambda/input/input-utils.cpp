/**
 * @file input-utils.cpp
 * @brief Implementation of shared utilities for Lambda input parsers
 */

#include "input-utils.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/str.h"
#include "../../lib/utf.h"
#include <ctype.h>
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
        if (!isspace(*line)) return false;
        line++;
    }
    return true;
}

int input_count_leading_chars(const char* str, char ch) {
    int count = 0;
    while (str[count] == ch) count++;
    return count;
}

char* input_trim_whitespace(const char* str) {
    if (!str) return NULL;

    // find start
    while (isspace(*str)) str++;

    if (*str == '\0') return mem_strdup("", MEM_CAT_INPUT_OTHER);

    // find end
    const char* end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) end--;

    // create trimmed copy
    int len = end - str + 1;
    char* result = (char*)mem_alloc(len + 1, MEM_CAT_INPUT_OTHER);
    strncpy(result, str, len);
    result[len] = '\0';

    return result;
}

char** input_split_lines(const char* text, int* line_count) {
    *line_count = 0;

    if (!text) {
        return NULL;
    }

    // count lines
    const char* ptr = text;
    while (*ptr) {
        if (*ptr == '\n') (*line_count)++;
        ptr++;
    }
    if (ptr > text && *(ptr-1) != '\n') {
        (*line_count)++; // last line without \n
    }

    if (*line_count == 0) {
        return NULL;
    }

    // allocate array
    char** lines = (char**)mem_alloc(*line_count * sizeof(char*), MEM_CAT_INPUT_OTHER);

    // split into lines
    int line_index = 0;
    const char* line_start = text;
    ptr = text;

    while (*ptr && line_index < *line_count) {
        if (*ptr == '\n') {
            int len = ptr - line_start;
            lines[line_index] = (char*)mem_alloc(len + 1, MEM_CAT_INPUT_OTHER);
            strncpy(lines[line_index], line_start, len);
            lines[line_index][len] = '\0';
            line_index++;
            line_start = ptr + 1;
        }
        ptr++;
    }

    // handle last line if it doesn't end with newline
    if (line_index < *line_count && line_start < ptr) {
        int len = ptr - line_start;
        lines[line_index] = (char*)mem_alloc(len + 1, MEM_CAT_INPUT_OTHER);
        strncpy(lines[line_index], line_start, len);
        lines[line_index][len] = '\0';
        line_index++;
    }

    // adjust line count to actual lines created
    *line_count = line_index;

    return lines;
}

void input_free_lines(char** lines, int line_count) {
    if (!lines) return;
    for (int i = 0; i < line_count; i++) {
        mem_free(lines[i]);
    }
    mem_free(lines);
}
