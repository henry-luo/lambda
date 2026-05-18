#include "display_list_replay_glyph.hpp"

#include <algorithm>
#include <math.h>

static uint32_t dl_glyph_sample_pixel(const GlyphBitmap* bitmap, int src_y, int src_x) {
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
        const uint8_t* p = bitmap->buffer + src_y * bitmap->pitch + src_x * 3;
        return ((uint32_t)p[0] + (uint32_t)p[1] + (uint32_t)p[2] + 1) / 3;
    }
    return 0;
}

static uint32_t dl_glyph_sample_coverage(const GlyphBitmap* bitmap, float src_y, float src_x) {
    if (!bitmap || !bitmap->buffer) return 0;
    if (bitmap->pixel_mode == GLYPH_PIXEL_MONO) {
        return dl_glyph_sample_pixel(bitmap, (int)floorf(src_y + 0.5f), (int)floorf(src_x + 0.5f));
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
    float c00 = (float)dl_glyph_sample_pixel(bitmap, sy0, sx0);
    float c10 = (float)dl_glyph_sample_pixel(bitmap, sy0, sx1);
    float c01 = (float)dl_glyph_sample_pixel(bitmap, sy1, sx0);
    float c11 = (float)dl_glyph_sample_pixel(bitmap, sy1, sx1);
    float top = c00 * (1.0f - tx) + c10 * tx;
    float bottom = c01 * (1.0f - tx) + c11 * tx;
    return (uint32_t)(top * (1.0f - ty) + bottom * ty + 0.5f);
}

void dl_replay_draw_glyph(ImageSurface* surface, const DlDrawGlyph* glyph) {
    GlyphBitmap* bitmap = (GlyphBitmap*)&glyph->bitmap;
    int x = glyph->x, y = glyph->y;
    Color color = glyph->color;
    if (color.a == 0) return;
    const Bound* clip = &glyph->clip;

    if (glyph->has_transform && !glyph->is_color_emoji) {
        {
            const RdtMatrix* m = &glyph->transform;
            float sx0 = (float)x;
            float sy0 = (float)y;
            float sx1 = (float)(x + bitmap->width);
            float sy1 = (float)(y + bitmap->height);
            float px[4] = {
                m->e11 * sx0 + m->e12 * sy0 + m->e13,
                m->e11 * sx1 + m->e12 * sy0 + m->e13,
                m->e11 * sx1 + m->e12 * sy1 + m->e13,
                m->e11 * sx0 + m->e12 * sy1 + m->e13
            };
            float py[4] = {
                m->e21 * sx0 + m->e22 * sy0 + m->e23,
                m->e21 * sx1 + m->e22 * sy0 + m->e23,
                m->e21 * sx1 + m->e22 * sy1 + m->e23,
                m->e21 * sx0 + m->e22 * sy1 + m->e23
            };
            float min_x = std::min(std::min(px[0], px[1]), std::min(px[2], px[3]));
            float max_x = std::max(std::max(px[0], px[1]), std::max(px[2], px[3]));
            float min_y = std::min(std::min(py[0], py[1]), std::min(py[2], py[3]));
            float max_y = std::max(std::max(py[0], py[1]), std::max(py[2], py[3]));

            int left = std::max((int)clip->left, (int)floorf(min_x));
            int right = std::min((int)clip->right, (int)ceilf(max_x));
            int top = std::max((int)clip->top, (int)floorf(min_y));
            int bottom = std::min((int)clip->bottom, (int)ceilf(max_y));
            left = std::max(left, 0);
            right = std::min(right, surface->width);
            top = std::max(top, surface->tile_offset_y);
            bottom = std::min(bottom, surface->tile_offset_y + surface->height);
            if (left >= right || top >= bottom) return;

            float det = m->e11 * m->e22 - m->e12 * m->e21;
            if (fabsf(det) < 0.000001f) return;
            float inv_det = 1.0f / det;

            for (int dst_y = top; dst_y < bottom; dst_y++) {
                uint8_t* row_pixels = (uint8_t*)surface->pixels +
                    (dst_y - surface->tile_offset_y) * surface->pitch;
                for (int dst_x = left; dst_x < right; dst_x++) {
                    float dx = (float)dst_x + 0.5f - m->e13;
                    float dy = (float)dst_y + 0.5f - m->e23;
                    float src_abs_x = ( m->e22 * dx - m->e12 * dy) * inv_det;
                    float src_abs_y = (-m->e21 * dx + m->e11 * dy) * inv_det;
                    float local_x = src_abs_x - (float)x - 0.5f;
                    float local_y = src_abs_y - (float)y - 0.5f;
                    uint32_t intensity = dl_glyph_sample_coverage(bitmap, local_y, local_x);
                    if (intensity == 0) continue;

                    uint8_t* p = row_pixels + dst_x * 4;
                    intensity = (intensity * color.a + 127) / 255;
                    uint32_t v = 255 - intensity;
                    if (color.c == 0xFF000000) {
                        p[0] = p[0] * v / 255;
                        p[1] = p[1] * v / 255;
                        p[2] = p[2] * v / 255;
                        p[3] = 0xFF;
                    } else {
                        p[0] = (p[0] * v + color.r * intensity) / 255;
                        p[1] = (p[1] * v + color.g * intensity) / 255;
                        p[2] = (p[2] * v + color.b * intensity) / 255;
                        p[3] = 0xFF;
                    }
                }
            }
            return;
        }

        for (int i = 0; i < (int)bitmap->height; i++) {
            for (int j = 0; j < (int)bitmap->width; j++) {
                uint32_t intensity = dl_glyph_sample_pixel(bitmap, i, j);
                if (intensity == 0) continue;

                float src_x = (float)(x + j) + 0.5f;
                float src_y = (float)(y + i) + 0.5f;
                float dst_xf = glyph->transform.e11 * src_x + glyph->transform.e12 * src_y + glyph->transform.e13;
                float dst_yf = glyph->transform.e21 * src_x + glyph->transform.e22 * src_y + glyph->transform.e23;
                int dst_x = (int)floorf(dst_xf);
                int dst_y = (int)floorf(dst_yf);
                if (dst_x < (int)clip->left || dst_x >= (int)clip->right ||
                    dst_y < (int)clip->top || dst_y >= (int)clip->bottom ||
                    dst_x < 0 || dst_x >= surface->width ||
                    dst_y < surface->tile_offset_y || dst_y >= surface->tile_offset_y + surface->height) {
                    continue;
                }

                uint8_t* p = (uint8_t*)surface->pixels + (dst_y - surface->tile_offset_y) * surface->pitch + dst_x * 4;
                intensity = (intensity * color.a + 127) / 255;
                uint32_t v = 255 - intensity;
                if (color.c == 0xFF000000) {
                    p[0] = p[0] * v / 255;
                    p[1] = p[1] * v / 255;
                    p[2] = p[2] * v / 255;
                    p[3] = 0xFF;
                } else {
                    p[0] = (p[0] * v + color.r * intensity) / 255;
                    p[1] = (p[1] * v + color.g * intensity) / 255;
                    p[2] = (p[2] * v + color.b * intensity) / 255;
                    p[3] = 0xFF;
                }
            }
        }
        return;
    }

    if (glyph->is_color_emoji) {
        float bscale = bitmap->bitmap_scale;
        if (bscale <= 0.0f) bscale = 1.0f;
        int target_w = (int)(bitmap->width  * bscale + 0.5f);
        int target_h = (int)(bitmap->height * bscale + 0.5f);
        if (target_w <= 0 || target_h <= 0) return;

        int left   = std::max((int)clip->left,  x);
        int right  = std::min((int)clip->right,  x + target_w);
        int top    = std::max((int)clip->top,    y);
        int bottom = std::min((int)clip->bottom, y + target_h);
        if (left >= right || top >= bottom) return;

        float inv_scale = 1.0f / bscale;
        for (int dy = top - y; dy < bottom - y; dy++) {
            uint8_t* row_pixels = (uint8_t*)surface->pixels + (y + dy - surface->tile_offset_y) * surface->pitch;
            float src_y = dy * inv_scale;
            int sy0 = (int)src_y;
            int sy1 = sy0 + 1;
            float fy = src_y - sy0;
            if (sy0 >= (int)bitmap->height) sy0 = bitmap->height - 1;
            if (sy1 >= (int)bitmap->height) sy1 = bitmap->height - 1;

            for (int dx = left - x; dx < right - x; dx++) {
                if (x + dx < 0 || x + dx >= surface->width) continue;
                float src_x = dx * inv_scale;
                int sx0 = (int)src_x;
                int sx1 = sx0 + 1;
                float fx = src_x - sx0;
                if (sx0 >= (int)bitmap->width) sx0 = bitmap->width - 1;
                if (sx1 >= (int)bitmap->width) sx1 = bitmap->width - 1;

                uint8_t* s00 = bitmap->buffer + sy0 * bitmap->pitch + sx0 * 4;
                uint8_t* s10 = bitmap->buffer + sy0 * bitmap->pitch + sx1 * 4;
                uint8_t* s01 = bitmap->buffer + sy1 * bitmap->pitch + sx0 * 4;
                uint8_t* s11 = bitmap->buffer + sy1 * bitmap->pitch + sx1 * 4;

                float w00 = (1 - fx) * (1 - fy);
                float w10 = fx * (1 - fy);
                float w01 = (1 - fx) * fy;
                float w11 = fx * fy;

                uint8_t src_b = (uint8_t)(s00[0]*w00 + s10[0]*w10 + s01[0]*w01 + s11[0]*w11 + 0.5f);
                uint8_t src_g = (uint8_t)(s00[1]*w00 + s10[1]*w10 + s01[1]*w01 + s11[1]*w11 + 0.5f);
                uint8_t src_r = (uint8_t)(s00[2]*w00 + s10[2]*w10 + s01[2]*w01 + s11[2]*w11 + 0.5f);
                uint8_t src_a = (uint8_t)(s00[3]*w00 + s10[3]*w10 + s01[3]*w01 + s11[3]*w11 + 0.5f);

                if (src_a > 0) {
                    uint8_t* dst = (uint8_t*)(row_pixels + (x + dx) * 4);
                    if (src_a == 255) {
                        dst[0] = src_r; dst[1] = src_g; dst[2] = src_b; dst[3] = 255;
                    } else {
                        uint32_t inv_alpha = 255 - src_a;
                        dst[0] = (dst[0] * inv_alpha + src_r * src_a) / 255;
                        dst[1] = (dst[1] * inv_alpha + src_g * src_a) / 255;
                        dst[2] = (dst[2] * inv_alpha + src_b * src_a) / 255;
                        dst[3] = 255;
                    }
                }
            }
        }
        return;
    }

    int left   = std::max((int)clip->left,  x);
    int right  = std::min((int)clip->right,  x + (int)bitmap->width);
    int top    = std::max((int)clip->top,    y);
    int bottom = std::min((int)clip->bottom, y + (int)bitmap->height);
    if (left >= right || top >= bottom) return;

    for (int i = top - y; i < bottom - y; i++) {
        uint8_t* row_pixels = (uint8_t*)surface->pixels + (y + i - surface->tile_offset_y) * surface->pitch;
        for (int j = left - x; j < right - x; j++) {
            if (x + j < 0 || x + j >= surface->width) continue;

            uint32_t intensity = dl_glyph_sample_pixel(bitmap, i, j);

            if (intensity > 0) {
                uint8_t* p = (uint8_t*)(row_pixels + (x + j) * 4);
                intensity = (intensity * color.a + 127) / 255;
                uint32_t v = 255 - intensity;
                if (color.c == 0xFF000000) {
                    p[0] = p[0] * v / 255;
                    p[1] = p[1] * v / 255;
                    p[2] = p[2] * v / 255;
                    p[3] = 0xFF;
                } else {
                    p[0] = (p[0] * v + color.r * intensity) / 255;
                    p[1] = (p[1] * v + color.g * intensity) / 255;
                    p[2] = (p[2] * v + color.b * intensity) / 255;
                    p[3] = 0xFF;
                }
            }
        }
    }
}
