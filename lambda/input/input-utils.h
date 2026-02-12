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

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_INPUT_UTILS_H
