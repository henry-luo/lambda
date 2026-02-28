/**
 * @file input-utils.h
 * @brief Shared utility functions for Lambda input parsers (C API)
 *
 * Centralises common operations duplicated across parsers:
 *   - Unicode codepoint → UTF-8 encoding
 *   - UTF-16 surrogate pair decoding
 *   - Hex-digit → codepoint parsing
 *   - Numeric string parsing helpers
 */

#ifndef LAMBDA_INPUT_UTILS_H
#define LAMBDA_INPUT_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Unicode Utilities ──────────────────────────────────────────────

/**
 * Encode a Unicode codepoint as UTF-8 into out[].
 * @param codepoint  Unicode codepoint (0 – 0x10FFFF)
 * @param out        Buffer of at least 5 bytes (null-terminated on success)
 * @return Number of bytes written (1–4), or 0 on invalid codepoint.
 */
int codepoint_to_utf8(uint32_t codepoint, char out[5]);

/**
 * Decode a UTF-16 surrogate pair to a Unicode codepoint.
 * @return Full codepoint (≥ 0x10000), or 0 if the pair is invalid.
 */
uint32_t decode_surrogate_pair(uint16_t high, uint16_t low);

/**
 * Parse exactly @p ndigits hex characters from *pos into a codepoint.
 * Advances *pos past the digits on success.
 * @return Parsed codepoint, or 0xFFFFFFFF on failure.
 */
uint32_t parse_hex_codepoint(const char** pos, int ndigits);

// ── Numeric Parsing ────────────────────────────────────────────────

/**
 * Try to parse a decimal integer from str[0..len-1].
 * @return true on success (value written to *out).
 */
bool try_parse_int64(const char* str, size_t len, int64_t* out);

/**
 * Try to parse a decimal floating-point number from str[0..len-1].
 * @return true on success (value written to *out).
 */
bool try_parse_double(const char* str, size_t len, double* out);

// ── String Classification ──────────────────────────────────────────

/**
 * Case-insensitive comparison of exactly @p n bytes.
 * @return 0 if equal, non-zero otherwise.
 */
int input_strncasecmp(const char* s1, const char* s2, size_t n);

// ── Whitespace & Line Utilities ────────────────────────────────────

// NOTE: skip_whitespace() and skip_tab_pace() are declared in input.hpp
// rather than here, because the name "skip_whitespace" conflicts with a
// different-signature version in markup_common.hpp (returns int, tabs→4).

/** Return true if @p c is space, tab, newline or carriage return. */
bool input_is_whitespace_char(char c);

/** Return true if @p line consists entirely of whitespace. */
bool input_is_empty_line(const char* line);

/** Count consecutive leading occurrences of @p ch in @p str. */
int input_count_leading_chars(const char* str, char ch);

/** Return a malloc'd trimmed copy of @p str (caller must free). */
char* input_trim_whitespace(const char* str);

/** Split @p text into malloc'd array of lines. Sets *line_count. Caller must free via input_free_lines(). */
char** input_split_lines(const char* text, int* line_count);

/** Free line array returned by input_split_lines(). */
void input_free_lines(char** lines, int line_count);

// ── Line-Oriented Parsing ──────────────────────────────────────────

/** Skip horizontal whitespace (space and tab) only. */
static inline void skip_line_whitespace(const char** p) {
    while (**p && (**p == ' ' || **p == '\t')) {
        (*p)++;
    }
}

/**
 * Skip to end of current line and past the line terminator.
 * Handles \n, \r, and \r\n.
 */
static inline void skip_to_newline(const char** p) {
    while (**p && **p != '\n' && **p != '\r') {
        (*p)++;
    }
    if (**p == '\r' && *(*p + 1) == '\n') {
        (*p) += 2; // skip \r\n
    } else if (**p == '\n' || **p == '\r') {
        (*p)++; // skip \n or \r
    }
}

/**
 * Return true if position starts a folded (continuation) line —
 * i.e. the first character is a space or tab (RFC 5322 / RFC 6350 / RFC 5545).
 */
static inline bool is_folded_line(const char* p) {
    return *p == ' ' || *p == '\t';
}

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_INPUT_UTILS_H
