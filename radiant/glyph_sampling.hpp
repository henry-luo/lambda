#pragma once

#include "display_list.h"

typedef struct GlyphColorSample {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} GlyphColorSample;

static inline GlyphColorSample glyph_sample_bgra_bilinear(const GlyphBitmap* bitmap,
                                                          float src_x, float src_y) {
    GlyphColorSample out = {};
    if (!bitmap || !bitmap->buffer || bitmap->width <= 0 || bitmap->height <= 0) return out;

    int sx0 = (int)src_x;
    int sy0 = (int)src_y;
    int sx1 = sx0 + 1;
    int sy1 = sy0 + 1;
    float fx = src_x - sx0;
    float fy = src_y - sy0;

    if (sx0 < 0) { sx0 = 0; fx = 0.0f; }
    if (sy0 < 0) { sy0 = 0; fy = 0.0f; }
    if (sx0 >= (int)bitmap->width) sx0 = bitmap->width - 1;
    if (sy0 >= (int)bitmap->height) sy0 = bitmap->height - 1;
    if (sx1 >= (int)bitmap->width) sx1 = bitmap->width - 1;
    if (sy1 >= (int)bitmap->height) sy1 = bitmap->height - 1;

    const uint8_t* s00 = bitmap->buffer + sy0 * bitmap->pitch + sx0 * 4;
    const uint8_t* s10 = bitmap->buffer + sy0 * bitmap->pitch + sx1 * 4;
    const uint8_t* s01 = bitmap->buffer + sy1 * bitmap->pitch + sx0 * 4;
    const uint8_t* s11 = bitmap->buffer + sy1 * bitmap->pitch + sx1 * 4;

    float w00 = (1.0f - fx) * (1.0f - fy);
    float w10 = fx * (1.0f - fy);
    float w01 = (1.0f - fx) * fy;
    float w11 = fx * fy;

    out.b = (uint8_t)(s00[0] * w00 + s10[0] * w10 + s01[0] * w01 + s11[0] * w11 + 0.5f);
    out.g = (uint8_t)(s00[1] * w00 + s10[1] * w10 + s01[1] * w01 + s11[1] * w11 + 0.5f);
    out.r = (uint8_t)(s00[2] * w00 + s10[2] * w10 + s01[2] * w01 + s11[2] * w11 + 0.5f);
    out.a = (uint8_t)(s00[3] * w00 + s10[3] * w10 + s01[3] * w01 + s11[3] * w11 + 0.5f);
    return out;
}
