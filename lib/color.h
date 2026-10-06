// lib/color.h - CSS color conversion and hex parse/format (header-only).
//
// Byte-level (r,g,b,a) in/out so each subsystem can adapt into its own color
// struct (CSS CssColor, Radiant Color, graph theme ints, ...). Centralizes the
// #rgb / #rgba / #rrggbb / #rrggbbaa digit handling that was hand-rolled in 4+
// places with subtly different digit-count support.

#ifndef LIB_COLOR_H
#define LIB_COLOR_H

#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include "hex.h"
#ifdef __cplusplus
extern "C++" {
#endif
#include "math_utils.h"
#ifdef __cplusplus
}
#endif

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

// CSS Color: conversion keeps floating components until a consumer needs bytes.
static inline void color_hsl_to_rgb(double h, double s, double l,
                                   double* r, double* g, double* b) {
    h = isfinite(h) ? fmod(h, 360.0) : 0.0;
    if (h < 0.0) h += 360.0;
    s = fmax(0.0, fmin(1.0, s)); l = fmax(0.0, fmin(1.0, l));
    double c = (1.0 - fabs(2.0 * l - 1.0)) * s;
    double x = c * (1.0 - fabs(fmod(h / 60.0, 2.0) - 1.0));
    double m = l - c * 0.5;
    double r1, g1, b1;
    if (h < 60.0)       { r1 = c; g1 = x; b1 = 0.0; }
    else if (h < 120.0) { r1 = x; g1 = c; b1 = 0.0; }
    else if (h < 180.0) { r1 = 0.0; g1 = c; b1 = x; }
    else if (h < 240.0) { r1 = 0.0; g1 = x; b1 = c; }
    else if (h < 300.0) { r1 = x; g1 = 0.0; b1 = c; }
    else               { r1 = c; g1 = 0.0; b1 = x; }
    *r = r1 + m; *g = g1 + m; *b = b1 + m;
}

static inline void color_hwb_to_rgb(double h, double w, double black,
                                   double* r, double* g, double* b) {
    w = fmax(0.0, fmin(1.0, w)); black = fmax(0.0, fmin(1.0, black));
    double sum = w + black;
    if (sum >= 1.0) {
        *r = *g = *b = w / sum;
    } else {
        color_hsl_to_rgb(h, 1.0, 0.5, r, g, b);
        *r = *r * (1.0 - sum) + w;
        *g = *g * (1.0 - sum) + w;
        *b = *b * (1.0 - sum) + w;
    }
}

static inline void color_hsl_to_rgba(float h, float s, float l, float a,
                                      uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* alpha) {
    double red, green, blue;
    color_hsl_to_rgb(h, s, l, &red, &green, &blue);
    *r = clamp_byte_round((float)(red * 255.0));
    *g = clamp_byte_round((float)(green * 255.0));
    *b = clamp_byte_round((float)(blue * 255.0));
    *alpha = clamp_byte_round(clamp_unit(a) * 255.0f);
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
