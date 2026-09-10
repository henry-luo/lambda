#include "render.hpp"

#include <algorithm>
#include <math.h>

// shared pixel primitives use packed ABGR values; premultiplied variants state
// their alpha contract explicitly so callers do not repeat channel arithmetic.
static inline uint32_t raster_pack_pixel(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    return (LMB_MIN(a, 255u) << 24) | (LMB_MIN(b, 255u) << 16) |
           (LMB_MIN(g, 255u) << 8) | LMB_MIN(r, 255u);
}

uint32_t render_pixel_pack_abgr(uint32_t red, uint32_t green,
                                uint32_t blue, uint32_t alpha) {
    return raster_pack_pixel(red, green, blue, alpha);
}

uint8_t render_pixel_premultiply_channel(uint8_t channel, uint8_t alpha) {
    return (uint8_t)(((uint32_t)channel * alpha + 127u) / 255u);
}

uint8_t render_pixel_unpremultiply_channel(uint8_t channel, uint8_t alpha) {
    if (alpha == 0) return 0;
    if (alpha == 255) return channel;
    uint32_t value = ((uint32_t)channel * 255u + alpha / 2u) / alpha;
    return (uint8_t)(value > 255u ? 255u : value);
}

uint32_t render_pixel_source_over_opaque(uint32_t destination, uint32_t source) {
    uint32_t sa = source >> 24;
    if (sa == 0) return destination;
    if (sa == 255) return source | 0xFF000000u;
    uint32_t inv = 255u - sa;
    return raster_pack_pixel(
        ((source & 0xFFu) * sa + (destination & 0xFFu) * inv) / 255u,
        (((source >> 8) & 0xFFu) * sa + ((destination >> 8) & 0xFFu) * inv) / 255u,
        (((source >> 16) & 0xFFu) * sa + ((destination >> 16) & 0xFFu) * inv) / 255u,
        255u);
}

uint32_t render_pixel_source_over_straight(uint32_t destination, uint32_t source,
                                           uint8_t opacity) {
    uint32_t source_a = ((source >> 24) * (uint32_t)opacity + 127u) / 255u;
    if (source_a == 0) return destination;
    if (source_a == 255) return source;
    uint32_t destination_a = destination >> 24;
    uint32_t inv = 255u - source_a;
    uint32_t result_a = source_a + (destination_a * inv + 127u) / 255u;
    if (result_a == 0) return 0;
    uint32_t sr = source & 0xFFu, sg = (source >> 8) & 0xFFu, sb = (source >> 16) & 0xFFu;
    uint32_t dr = destination & 0xFFu, dg = (destination >> 8) & 0xFFu, db = (destination >> 16) & 0xFFu;
    uint32_t drp = (dr * destination_a + 127u) / 255u;
    uint32_t dgp = (dg * destination_a + 127u) / 255u;
    uint32_t dbp = (db * destination_a + 127u) / 255u;
    uint32_t rp = (sr * source_a + 127u) / 255u + (drp * inv + 127u) / 255u;
    uint32_t gp = (sg * source_a + 127u) / 255u + (dgp * inv + 127u) / 255u;
    uint32_t bp = (sb * source_a + 127u) / 255u + (dbp * inv + 127u) / 255u;
    return raster_pack_pixel((rp * 255u + result_a / 2u) / result_a,
        (gp * 255u + result_a / 2u) / result_a,
        (bp * 255u + result_a / 2u) / result_a, result_a);
}

uint32_t render_pixel_source_over_premultiplied(uint32_t destination, uint32_t source) {
    uint32_t sa = source >> 24;
    if (sa == 0) return destination;
    uint32_t da = destination >> 24;
    uint32_t inv = 255u - sa;
    uint32_t result_a = sa + (da * inv + 127u) / 255u;
    if (result_a == 0) return 0;
    uint32_t drp = ((destination & 0xFFu) * da + 127u) / 255u;
    uint32_t dgp = (((destination >> 8) & 0xFFu) * da + 127u) / 255u;
    uint32_t dbp = (((destination >> 16) & 0xFFu) * da + 127u) / 255u;
    uint32_t rp = (source & 0xFFu) + (drp * inv + 127u) / 255u;
    uint32_t gp = ((source >> 8) & 0xFFu) + (dgp * inv + 127u) / 255u;
    uint32_t bp = ((source >> 16) & 0xFFu) + (dbp * inv + 127u) / 255u;
    return raster_pack_pixel((rp * 255u + result_a / 2u) / result_a,
        (gp * 255u + result_a / 2u) / result_a,
        (bp * 255u + result_a / 2u) / result_a, result_a);
}

uint32_t render_pixel_source_over_premultiplied_opaque(uint32_t destination, uint32_t source) {
    uint32_t sa = source >> 24;
    if (sa == 0) return destination;
    if (sa == 255) return source | 0xFF000000u;
    uint32_t inv = 255u - sa;
    return raster_pack_pixel(
        (source & 0xFFu) + ((destination & 0xFFu) * inv + 127u) / 255u,
        ((source >> 8) & 0xFFu) + (((destination >> 8) & 0xFFu) * inv + 127u) / 255u,
        ((source >> 16) & 0xFFu) + (((destination >> 16) & 0xFFu) * inv + 127u) / 255u,
        sa + ((destination >> 24) * inv + 127u) / 255u);
}

uint32_t render_pixel_destination_over_premultiplied(uint32_t destination, uint32_t source) {
    uint32_t destination_a = destination >> 24;
    if (destination_a == 255) return destination;
    uint32_t inv = 255u - destination_a;
    return raster_pack_pixel(
        (destination & 0xFFu) + ((source & 0xFFu) * inv + 127u) / 255u,
        ((destination >> 8) & 0xFFu) + (((source >> 8) & 0xFFu) * inv + 127u) / 255u,
        ((destination >> 16) & 0xFFu) + (((source >> 16) & 0xFFu) * inv + 127u) / 255u,
        destination_a + ((source >> 24) * inv + 127u) / 255u);
}

uint32_t render_pixel_sample_bilinear(const uint8_t* pixels, int width, int height,
                                      int pitch, float x, float y, bool wrap,
                                      bool round_channels) {
    if (!pixels || width <= 0 || height <= 0 || pitch <= 0) return 0;
    int x1 = (int)floorf(x);
    int y1 = (int)floorf(y);
    int x2 = x1 + 1;
    int y2 = y1 + 1;
    if (wrap) {
        x1 = ((x1 % width) + width) % width;
        y1 = ((y1 % height) + height) % height;
        x2 = ((x2 % width) + width) % width;
        y2 = ((y2 % height) + height) % height;
    } else {
        x1 = std::max(0, std::min(x1, width - 1));
        y1 = std::max(0, std::min(y1, height - 1));
        x2 = std::max(0, std::min(x2, width - 1));
        y2 = std::max(0, std::min(y2, height - 1));
    }
    float fx = x - floorf(x);
    float fy = y - floorf(y);
    const uint8_t* p11 = pixels + y1 * pitch + x1 * 4;
    const uint8_t* p21 = pixels + y1 * pitch + x2 * 4;
    const uint8_t* p12 = pixels + y2 * pitch + x1 * 4;
    const uint8_t* p22 = pixels + y2 * pitch + x2 * 4;
    float w11 = (1.0f - fx) * (1.0f - fy);
    float w21 = fx * (1.0f - fy);
    float w12 = (1.0f - fx) * fy;
    float w22 = fx * fy;
    float rounding = round_channels ? 0.5f : 0.0f;
    uint8_t c0 = (uint8_t)(p11[0] * w11 + p21[0] * w21 + p12[0] * w12 + p22[0] * w22 + rounding);
    uint8_t c1 = (uint8_t)(p11[1] * w11 + p21[1] * w21 + p12[1] * w12 + p22[1] * w22 + rounding);
    uint8_t c2 = (uint8_t)(p11[2] * w11 + p21[2] * w21 + p12[2] * w12 + p22[2] * w22 + rounding);
    uint8_t c3 = (uint8_t)(p11[3] * w11 + p21[3] * w21 + p12[3] * w12 + p22[3] * w22 + rounding);
    return (uint32_t)c0 | ((uint32_t)c1 << 8) |
        ((uint32_t)c2 << 16) | ((uint32_t)c3 << 24);
}

void render_pixel_source_over_coverage(uint8_t* destination, Color color,
                                       uint32_t coverage) {
    if (!destination) return;
    uint32_t source_a = (coverage * color.a + 127u) / 255u;
    if (source_a == 0) return;
    uint32_t inverse = 255u - source_a;
    if (destination[3] == 255) {
        if (color.c == 0xFF000000) {
            destination[0] = destination[0] * inverse / 255u;
            destination[1] = destination[1] * inverse / 255u;
            destination[2] = destination[2] * inverse / 255u;
        } else {
            destination[0] = (destination[0] * inverse + color.r * source_a) / 255u;
            destination[1] = (destination[1] * inverse + color.g * source_a) / 255u;
            destination[2] = (destination[2] * inverse + color.b * source_a) / 255u;
        }
        return;
    }
    uint32_t destination_a = destination[3];
    uint32_t result_a = source_a + (destination_a * inverse + 127u) / 255u;
    if (result_a == 0) {
        destination[0] = destination[1] = destination[2] = destination[3] = 0;
        return;
    }
    uint32_t red = (color.r * source_a + 127u) / 255u +
        (destination[0] * inverse + 127u) / 255u;
    uint32_t green = (color.g * source_a + 127u) / 255u +
        (destination[1] * inverse + 127u) / 255u;
    uint32_t blue = (color.b * source_a + 127u) / 255u +
        (destination[2] * inverse + 127u) / 255u;
    destination[0] = (uint8_t)(red > 255 ? 255 : red);
    destination[1] = (uint8_t)(green > 255 ? 255 : green);
    destination[2] = (uint8_t)(blue > 255 ? 255 : blue);
    destination[3] = (uint8_t)(result_a > 255 ? 255 : result_a);
}

void render_pixel_source_over_opaque_bytes(uint8_t* destination, uint32_t source,
                                           uint8_t opacity) {
    if (!destination) return;
    uint8_t source_alpha = (uint8_t)((source >> 24) & 0xFFu);
    if (opacity < 255 && source_alpha > 0) {
        source_alpha = (uint8_t)((source_alpha * opacity + 127u) / 255u);
    }
    if (source_alpha == 0) return;
    uint8_t source_red = (uint8_t)(source & 0xFFu);
    uint8_t source_green = (uint8_t)((source >> 8) & 0xFFu);
    uint8_t source_blue = (uint8_t)((source >> 16) & 0xFFu);
    if (source_alpha == 255) {
        destination[0] = source_red;
        destination[1] = source_green;
        destination[2] = source_blue;
        destination[3] = 255;
        return;
    }
    float alpha = source_alpha / 255.0f;
    float inverse = 1.0f - alpha;
    destination[0] = (uint8_t)(source_red * alpha + destination[0] * inverse);
    destination[1] = (uint8_t)(source_green * alpha + destination[1] * inverse);
    destination[2] = (uint8_t)(source_blue * alpha + destination[2] * inverse);
    destination[3] = (uint8_t)(source_alpha + destination[3] * inverse);
}
