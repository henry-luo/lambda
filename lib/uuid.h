// lib/uuid.h - RFC 4122 UUID v4 string formatting (header-only).
//
// The caller supplies the 16 random bytes, so the entropy source stays the
// caller's choice (crypto-strength OS entropy for security-sensitive IDs, or a
// cheap PRNG for non-security IDs). This helper only owns the version/variant
// bit-twiddling and the canonical 8-4-4-4-12 hex formatting that was previously
// duplicated.

#ifndef LIB_UUID_H
#define LIB_UUID_H

#include <stdint.h>
#include "hex.h"

#ifdef __cplusplus
extern "C" {
#endif

// 36 hyphenated lowercase hex chars + NUL terminator.
#define UUID_STR_LEN 37

// Set the version (4) and variant (RFC 4122) bits on `bytes` in place, then
// format the canonical "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx" string into
// `out` (must hold at least UUID_STR_LEN bytes). NUL-terminates.
static inline void uuid_v4_format(uint8_t bytes[16], char out[UUID_STR_LEN]) {
    bytes[6] = (uint8_t)((bytes[6] & 0x0F) | 0x40); // version 4
    bytes[8] = (uint8_t)((bytes[8] & 0x3F) | 0x80); // variant 1
    int p = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[p++] = '-';
        out[p++] = hex_encode_nibble((unsigned)(bytes[i] >> 4));
        out[p++] = hex_encode_nibble((unsigned)(bytes[i] & 0x0F));
    }
    out[p] = '\0';
}

#ifdef __cplusplus
}
#endif

#endif // LIB_UUID_H
