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

// extended sRGB channels stay unclipped while converting/interpolating CSS colors.
static inline double color_srgb_to_linear(double channel) {
    double magnitude = fabs(channel);
    return magnitude <= 0.04045 ? channel / 12.92
        : copysign(pow((magnitude + 0.055) / 1.055, 2.4), channel);
}

static inline double color_linear_to_srgb(double channel) {
    double magnitude = fabs(channel);
    return magnitude <= 0.0031308 ? channel * 12.92
        : copysign(1.055 * pow(magnitude, 1.0 / 2.4) - 0.055, channel);
}

// CIELab uses its declared reference white; Bradford adapts it to sRGB's D65.
static inline void color_lab_to_rgb(double lightness, double a, double b,
                                    const double white[3], double rgb[3]) {
    const double bradford[3][3] = {{0.8951,0.2664,-0.1614},
        {-0.7502,1.7135,0.0367},{0.0389,-0.0685,1.0296}};
    const double inverse[3][3] = {{0.9869929,-0.1470543,0.1599627},
        {0.4323053,0.5183603,0.0492912},{-0.0085287,0.0400428,0.9684867}};
    const double d65[3] = {0.9504559271,1,1.0890577508};
    const double srgb[3][3] = {{3.24096994,-1.53738318,-0.49861076},
        {-0.96924364,1.8759675,0.04155506},{0.05563008,-0.20397696,1.05697151}};
    double y = (lightness + 16) / 116;
    double lab[3] = {y + a / 500, y, y - b / 200}, xyz[3], lms[3] = {};
    for (int c = 0; c < 3; c++) xyz[c] = white[c] * (lab[c] > 6.0 / 29
        ? lab[c] * lab[c] * lab[c] : (lab[c] - 4.0 / 29) * 108.0 / 841);
    for (int c = 0; c < 3; c++) {
        double source = 0, destination = 0;
        for (int j = 0; j < 3; j++) {
            lms[c] += bradford[c][j] * xyz[j];
            source += bradford[c][j] * white[j]; destination += bradford[c][j] * d65[j];
        }
        lms[c] *= destination / source;
    }
    for (int c = 0; c < 3; c++) {
        xyz[c] = 0;
        for (int j = 0; j < 3; j++) xyz[c] += inverse[c][j] * lms[j];
    }
    for (int c = 0; c < 3; c++) rgb[c] = color_linear_to_srgb(
        srgb[c][0] * xyz[0] + srgb[c][1] * xyz[1] + srgb[c][2] * xyz[2]);
}

// CSS Color 4 §19: Oklab's D65 LMS transform composed with linear sRGB.
static inline void color_oklab_to_rgb(double lightness, double a, double b,
                                      double* red, double* green, double* blue) {
    double l = lightness + 0.3963377773761749 * a + 0.2158037573099136 * b;
    double m = lightness - 0.1055613458156586 * a - 0.0638541728258133 * b;
    double s = lightness - 0.0894841775298119 * a - 1.2914855480194092 * b;
    l = l * l * l; m = m * m * m; s = s * s * s;
    *red = color_linear_to_srgb(4.076741661347994 * l - 3.3077115913 * m + 0.230969929981 * s);
    *green = color_linear_to_srgb(-1.2684380046 * l + 2.6097574007 * m - 0.3413193961 * s);
    *blue = color_linear_to_srgb(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s);
}

static inline void color_rgb_to_oklab(double red, double green, double blue,
                                      double* lightness, double* a, double* b) {
    red = color_srgb_to_linear(red); green = color_srgb_to_linear(green);
    blue = color_srgb_to_linear(blue);
    double l = cbrt(0.4122214708 * red + 0.5363325363 * green + 0.0514459929 * blue);
    double m = cbrt(0.2119034982 * red + 0.6806995451 * green + 0.1073969566 * blue);
    double s = cbrt(0.0883024619 * red + 0.2817188376 * green + 0.6299787005 * blue);
    *lightness = 0.2104542683093140 * l + 0.7936177747023054 * m - 0.0040720430116193 * s;
    *a = 1.9779985324311684 * l - 2.4285922420485799 * m + 0.4505937096174110 * s;
    *b = 0.0259040424655478 * l + 0.7827717124575296 * m - 0.8086757549230774 * s;
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
