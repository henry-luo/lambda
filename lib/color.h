// lib/color.h - CSS hex color parse/format (header-only).
//
// Byte-level (r,g,b,a) in/out so each subsystem can adapt into its own color
// struct (CSS CssColor, Radiant Color, graph theme ints, ...). Centralizes the
// #rgb / #rgba / #rrggbb / #rrggbbaa digit handling that was hand-rolled in 4+
// places with subtly different digit-count support.

#ifndef LIB_COLOR_H
#define LIB_COLOR_H

#include <stdbool.h>
#include <stdint.h>
#include "hex.h"

#ifdef __cplusplus
extern "C" {
#endif

// two hex digits -> 0..255, or -1 if either digit is invalid
static inline int color__hex2(const char* s) {
    int hi = hex_decode_byte(s[0]);
    int lo = hex_decode_byte(s[1]);
    if (hi < 0 || lo < 0) return -1;
    return (hi << 4) | lo;
}

// one hex digit -> nibble-doubled byte (f -> 0xFF), or -1 if invalid
static inline int color__hex1(char c) {
    int v = hex_decode_byte(c);
    if (v < 0) return -1;
    return (v << 4) | v;
}

// Parse a CSS hex color. Accepts an optional leading '#' followed by exactly
// 3 (#rgb), 4 (#rgba), 6 (#rrggbb), or 8 (#rrggbbaa) hex digits; short forms are
// nibble-doubled. Alpha defaults to 255 when absent. Any other length or a
// non-hex digit yields false (outputs untouched). All four outputs required.
static inline bool color_parse_hex(const char* str, uint8_t* r, uint8_t* g,
                                   uint8_t* b, uint8_t* a) {
    if (!str || !r || !g || !b || !a) return false;
    if (*str == '#') str++;
    size_t n = 0;
    while (str[n]) n++;

    int R, G, B, A;
    if (n == 6) {
        R = color__hex2(str);     G = color__hex2(str + 2);
        B = color__hex2(str + 4); A = 255;
    } else if (n == 3) {
        R = color__hex1(str[0]);  G = color__hex1(str[1]);
        B = color__hex1(str[2]);  A = 255;
    } else if (n == 8) {
        R = color__hex2(str);     G = color__hex2(str + 2);
        B = color__hex2(str + 4); A = color__hex2(str + 6);
    } else if (n == 4) {
        R = color__hex1(str[0]);  G = color__hex1(str[1]);
        B = color__hex1(str[2]);  A = color__hex1(str[3]);
    } else {
        return false;
    }
    if (R < 0 || G < 0 || B < 0 || A < 0) return false;
    *r = (uint8_t)R; *g = (uint8_t)G; *b = (uint8_t)B; *a = (uint8_t)A;
    return true;
}

// Format "#rrggbb" (lowercase, 7 chars + NUL) into out (must hold >= 8 bytes).
static inline void color_format_hex(uint8_t r, uint8_t g, uint8_t b, char* out) {
    out[0] = '#';
    out[1] = hex_encode_nibble((unsigned)(r >> 4)); out[2] = hex_encode_nibble((unsigned)(r & 0x0F));
    out[3] = hex_encode_nibble((unsigned)(g >> 4)); out[4] = hex_encode_nibble((unsigned)(g & 0x0F));
    out[5] = hex_encode_nibble((unsigned)(b >> 4)); out[6] = hex_encode_nibble((unsigned)(b & 0x0F));
    out[7] = '\0';
}

#ifdef __cplusplus
}
#endif

#endif // LIB_COLOR_H
