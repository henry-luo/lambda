// lib/hex.h - small hex encode/decode helpers (header-only).
//
// Lowercase hex by convention (matches what `sha256_to_hex` callers in
// network/ and input/ produce). Use these directly to avoid hand-rolled
// loops; mate with mbedtls SHA-256 or any other digest at the call site.
//
// All routines are NULL/0-tolerant where it doesn't change the contract.

#ifndef LIB_HEX_H
#define LIB_HEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 0–15 → '0'..'9','a'..'f'. Returns '?' for out-of-range input.
static inline char hex_encode_nibble(unsigned n) {
    return (n < 10) ? (char)('0' + n) : (n < 16 ? (char)('a' + (n - 10)) : '?');
}

// 0–15 → '0'..'9','A'..'F'. Use for RFC 3986 percent-encoding etc.
static inline char hex_encode_nibble_upper(unsigned n) {
    return (n < 10) ? (char)('0' + n) : (n < 16 ? (char)('A' + (n - 10)) : '?');
}

// '0'..'9','a'..'f','A'..'F' → 0..15. Returns -1 for invalid input.
static inline int hex_decode_byte(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

// Encode `len` bytes to lowercase hex. `out` must have room for at least
// `2*len + 1` chars; result is NUL-terminated.
static inline void hex_encode(const void* bytes, size_t len, char* out) {
    const unsigned char* p = (const unsigned char*)bytes;
    for (size_t i = 0; i < len; ++i) {
        out[2*i + 0] = hex_encode_nibble(p[i] >> 4);
        out[2*i + 1] = hex_encode_nibble(p[i] & 0x0f);
    }
    out[2*len] = '\0';
}

// Encode `len` bytes to uppercase hex. Same buffer contract as hex_encode().
static inline void hex_encode_upper(const void* bytes, size_t len, char* out) {
    const unsigned char* p = (const unsigned char*)bytes;
    for (size_t i = 0; i < len; ++i) {
        out[2*i + 0] = hex_encode_nibble_upper(p[i] >> 4);
        out[2*i + 1] = hex_encode_nibble_upper(p[i] & 0x0f);
    }
    out[2*len] = '\0';
}

// Decode `in_len` hex chars into bytes. `in_len` must be even.
// Writes `in_len / 2` bytes to `out`. Returns true on success, false if
// any character is invalid or `in_len` is odd.
// If `out_len` is non-NULL, it is set to the number of bytes written.
static inline bool hex_decode(const char* in, size_t in_len,
                              void* out, size_t* out_len) {
    if (in_len % 2 != 0) return false;
    unsigned char* p = (unsigned char*)out;
    for (size_t i = 0; i < in_len; i += 2) {
        int hi = hex_decode_byte(in[i]);
        int lo = hex_decode_byte(in[i + 1]);
        if (hi < 0 || lo < 0) return false;
        p[i / 2] = (unsigned char)((hi << 4) | lo);
    }
    if (out_len) *out_len = in_len / 2;
    return true;
}

#ifdef __cplusplus
}
#endif

#endif
