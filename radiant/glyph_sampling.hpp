#pragma once

#include "display_list.h"

#include <math.h>

typedef struct GlyphColorSample {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} GlyphColorSample;

static inline GlyphColorSample glyph_sample_bgra_bilinear(const GlyphBitmap* bitmap,
                                                          float src_x, float src_y);

static inline uint32_t glyph_sample_coverage_pixel(const GlyphBitmap* bitmap, int src_y, int src_x) {
    if (!bitmap || !bitmap->buffer || src_y < 0 || src_y >= bitmap->height ||
        src_x < 0 || src_x >= bitmap->width) return 0;
    if (bitmap->pixel_mode == GLYPH_PIXEL_MONO) {
        int byte_index = src_x / 8;
        int bit_index = 7 - (src_x % 8);
        uint8_t byte_val = bitmap->buffer[src_y * bitmap->pitch + byte_index];
        return (byte_val & (1 << bit_index)) ? 255 : 0;
    }
    if (bitmap->pixel_mode == GLYPH_PIXEL_GRAY) {
        return bitmap->buffer[src_y * bitmap->pitch + src_x];
    }
    if (bitmap->pixel_mode == GLYPH_PIXEL_LCD) {
        const uint8_t* subpixel = bitmap->buffer + src_y * bitmap->pitch + src_x * 3;
        return ((uint32_t)subpixel[0] + (uint32_t)subpixel[1] + (uint32_t)subpixel[2] + 1) / 3;
    }
    return 0;
}

static inline uint32_t glyph_sample_coverage_bilinear(const GlyphBitmap* bitmap,
                                                      float src_y, float src_x) {
    if (!bitmap || !bitmap->buffer) return 0;
    if (bitmap->pixel_mode == GLYPH_PIXEL_MONO) {
        return glyph_sample_coverage_pixel(bitmap,
            (int)floorf(src_y + 0.5f), (int)floorf(src_x + 0.5f));
    }
    if (bitmap->pixel_mode != GLYPH_PIXEL_GRAY && bitmap->pixel_mode != GLYPH_PIXEL_LCD) return 0;

    int sx0 = (int)floorf(src_x);
    int sy0 = (int)floorf(src_y);
    float tx = src_x - (float)sx0;
    float ty = src_y - (float)sy0;
    int sx1 = sx0 + 1;
    int sy1 = sy0 + 1;
    if (sx0 < 0) { sx0 = 0; tx = 0.0f; }
    if (sy0 < 0) { sy0 = 0; ty = 0.0f; }
    if (sx1 >= bitmap->width) sx1 = bitmap->width - 1;
    if (sy1 >= bitmap->height) sy1 = bitmap->height - 1;
    if (sx0 >= bitmap->width || sy0 >= bitmap->height) return 0;

    float c00 = (float)glyph_sample_coverage_pixel(bitmap, sy0, sx0);
    float c10 = (float)glyph_sample_coverage_pixel(bitmap, sy0, sx1);
    float c01 = (float)glyph_sample_coverage_pixel(bitmap, sy1, sx0);
    float c11 = (float)glyph_sample_coverage_pixel(bitmap, sy1, sx1);
    float top = c00 * (1.0f - tx) + c10 * tx;
    float bottom = c01 * (1.0f - tx) + c11 * tx;
    return (uint32_t)(top * (1.0f - ty) + bottom * ty + 0.5f);
}

static inline void glyph_blend_coverage_pixel(uint8_t* p, Color color, uint32_t coverage) {
    uint32_t src_a = (coverage * color.a + 127) / 255;
    if (src_a == 0) return;
    uint32_t inv_a = 255 - src_a;

    if (p[3] == 255) {
        if (color.c == 0xFF000000) {
            p[0] = p[0] * inv_a / 255;
            p[1] = p[1] * inv_a / 255;
            p[2] = p[2] * inv_a / 255;
            p[3] = 0xFF;
        } else {
            p[0] = (p[0] * inv_a + color.r * src_a) / 255;
            p[1] = (p[1] * inv_a + color.g * src_a) / 255;
            p[2] = (p[2] * inv_a + color.b * src_a) / 255;
            p[3] = 0xFF;
        }
        return;
    }

    uint32_t dst_a = p[3];
    uint32_t out_a = src_a + (dst_a * inv_a + 127) / 255;
    if (out_a == 0) {
        p[0] = p[1] = p[2] = p[3] = 0;
        return;
    }

    // glyph replay surfaces are premultiplied; transparent fringes must not store straight white.
    uint32_t out_r = (color.r * src_a + 127) / 255 + (p[0] * inv_a + 127) / 255;
    uint32_t out_g = (color.g * src_a + 127) / 255 + (p[1] * inv_a + 127) / 255;
    uint32_t out_b = (color.b * src_a + 127) / 255 + (p[2] * inv_a + 127) / 255;
    p[0] = (uint8_t)(out_r > 255 ? 255 : out_r);
    p[1] = (uint8_t)(out_g > 255 ? 255 : out_g);
    p[2] = (uint8_t)(out_b > 255 ? 255 : out_b);
    p[3] = (uint8_t)(out_a > 255 ? 255 : out_a);
}

static inline void glyph_draw_coverage_bitmap(ImageSurface* surface, const GlyphBitmap* bitmap,
                                              int x, int y, const Bound* clip, Color color) {
    if (!surface || !bitmap || !clip || color.a == 0) return;

    int left = (int)clip->left > x ? (int)clip->left : x;
    int right = (int)clip->right < x + (int)bitmap->width ? (int)clip->right : x + (int)bitmap->width;
    int top = (int)clip->top > y ? (int)clip->top : y;
    int bottom = (int)clip->bottom < y + (int)bitmap->height ? (int)clip->bottom : y + (int)bitmap->height;
    if (left >= right || top >= bottom) return;

    for (int i = top - y; i < bottom - y; i++) {
        uint8_t* row_pixels = (uint8_t*)surface->pixels + (y + i - surface->tile_offset_y) * surface->pitch;
        for (int j = left - x; j < right - x; j++) {
            if (x + j < 0 || x + j >= surface->width) continue;

            uint32_t intensity = glyph_sample_coverage_pixel(bitmap, i, j);
            if (intensity > 0) {
                uint8_t* p = (uint8_t*)(row_pixels + (x + j) * 4);
                glyph_blend_coverage_pixel(p, color, intensity);
            }
        }
    }
}

static inline void glyph_draw_transformed_coverage_bitmap(ImageSurface* surface,
                                                          const GlyphBitmap* bitmap,
                                                          int x, int y, const Bound* clip,
                                                          Color color, const RdtMatrix* transform,
                                                          float surface_origin_x,
                                                          float surface_origin_y) {
    if (!surface || !bitmap || !clip || !transform || color.a == 0) return;

    float min_x, min_y, max_x, max_y;
    rdt_matrix_transform_rect_bounds(transform, (float)x, (float)y,
        (float)(x + bitmap->width), (float)(y + bitmap->height),
        &min_x, &min_y, &max_x, &max_y);

    int left = (int)clip->left > (int)floorf(min_x) ? (int)clip->left : (int)floorf(min_x);
    int right = (int)clip->right < (int)ceilf(max_x) ? (int)clip->right : (int)ceilf(max_x);
    int top = (int)clip->top > (int)floorf(min_y) ? (int)clip->top : (int)floorf(min_y);
    int bottom = (int)clip->bottom < (int)ceilf(max_y) ? (int)clip->bottom : (int)ceilf(max_y);

    int surface_left = (int)surface_origin_x;
    int surface_top = (int)surface_origin_y;
    int surface_right = surface_left + surface->width;
    int surface_bottom = surface_top + surface->height;
    if (left < surface_left) left = surface_left;
    if (right > surface_right) right = surface_right;
    if (top < surface_top) top = surface_top;
    if (bottom > surface_bottom) bottom = surface_bottom;
    if (left >= right || top >= bottom) return;

    float det = transform->e11 * transform->e22 - transform->e12 * transform->e21;
    if (fabsf(det) < 0.000001f) return;
    float inv_det = 1.0f / det;

    // sample destination coverage so full-page and tiled replay rasterize transforms identically.
    for (int dst_y = top; dst_y < bottom; dst_y++) {
        int local_y = (int)((float)dst_y - surface_origin_y);
        uint8_t* row_pixels = (uint8_t*)surface->pixels + local_y * surface->pitch;
        for (int dst_x = left; dst_x < right; dst_x++) {
            float dx = (float)dst_x + 0.5f - transform->e13;
            float dy = (float)dst_y + 0.5f - transform->e23;
            float src_abs_x = ( transform->e22 * dx - transform->e12 * dy) * inv_det;
            float src_abs_y = (-transform->e21 * dx + transform->e11 * dy) * inv_det;
            float local_src_x = src_abs_x - (float)x - 0.5f;
            float local_src_y = src_abs_y - (float)y - 0.5f;
            uint32_t intensity = glyph_sample_coverage_bilinear(bitmap, local_src_y, local_src_x);
            if (intensity == 0) continue;

            int local_x = (int)((float)dst_x - surface_origin_x);
            uint8_t* p = row_pixels + local_x * 4;
            glyph_blend_coverage_pixel(p, color, intensity);
        }
    }
}

static inline void glyph_draw_color_bgra_bitmap(ImageSurface* surface, const GlyphBitmap* bitmap,
                                                int x, int y, const Bound* clip) {
    if (!surface || !bitmap || !clip) return;

    float bscale = bitmap->bitmap_scale;
    if (bscale <= 0.0f) bscale = 1.0f;
    int target_w = (int)(bitmap->width  * bscale + 0.5f);
    int target_h = (int)(bitmap->height * bscale + 0.5f);
    if (target_w <= 0 || target_h <= 0) return;

    int left = (int)clip->left > x ? (int)clip->left : x;
    int right = (int)clip->right < x + target_w ? (int)clip->right : x + target_w;
    int top = (int)clip->top > y ? (int)clip->top : y;
    int bottom = (int)clip->bottom < y + target_h ? (int)clip->bottom : y + target_h;
    if (left >= right || top >= bottom) return;

    float inv_scale = 1.0f / bscale;
    for (int dy = top - y; dy < bottom - y; dy++) {
        uint8_t* row_pixels = (uint8_t*)surface->pixels + (y + dy - surface->tile_offset_y) * surface->pitch;
        float src_y = dy * inv_scale;

        for (int dx = left - x; dx < right - x; dx++) {
            if (x + dx < 0 || x + dx >= surface->width) continue;
            float src_x = dx * inv_scale;
            GlyphColorSample sample = glyph_sample_bgra_bilinear(bitmap, src_x, src_y);

            if (sample.a > 0) {
                uint8_t* dst = (uint8_t*)(row_pixels + (x + dx) * 4);
                if (sample.a == 255) {
                    dst[0] = sample.r;
                    dst[1] = sample.g;
                    dst[2] = sample.b;
                    dst[3] = 255;
                } else {
                    uint32_t inv_alpha = 255 - sample.a;
                    dst[0] = (dst[0] * inv_alpha + sample.r * sample.a) / 255;
                    dst[1] = (dst[1] * inv_alpha + sample.g * sample.a) / 255;
                    dst[2] = (dst[2] * inv_alpha + sample.b * sample.a) / 255;
                    dst[3] = 255;
                }
            }
        }
    }
}

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
