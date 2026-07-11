#include "display_list_replay_glyph.hpp"
#include "glyph_sampling.hpp"

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

static inline void dl_blend_glyph_coverage_pixel(uint8_t* p, Color color, uint32_t coverage) {
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
    uint32_t out_r = (color.r * src_a + 127) / 255 + (p[0] * inv_a + 127) / 255;
    uint32_t out_g = (color.g * src_a + 127) / 255 + (p[1] * inv_a + 127) / 255;
    uint32_t out_b = (color.b * src_a + 127) / 255 + (p[2] * inv_a + 127) / 255;
    p[0] = (uint8_t)(out_r > 255 ? 255 : out_r);
    p[1] = (uint8_t)(out_g > 255 ? 255 : out_g);
    p[2] = (uint8_t)(out_b > 255 ? 255 : out_b);
    p[3] = (uint8_t)(out_a > 255 ? 255 : out_a);
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
            float min_x, min_y, max_x, max_y;
            rdt_matrix_transform_rect_bounds(m, sx0, sy0, sx1, sy1,
                                             &min_x, &min_y, &max_x, &max_y);

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
                    dl_blend_glyph_coverage_pixel(p, color, intensity);
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
                dl_blend_glyph_coverage_pixel(p, color, intensity);
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

            for (int dx = left - x; dx < right - x; dx++) {
                if (x + dx < 0 || x + dx >= surface->width) continue;
                float src_x = dx * inv_scale;
                GlyphColorSample sample = glyph_sample_bgra_bilinear(bitmap, src_x, src_y);

                if (sample.a > 0) {
                    uint8_t* dst = (uint8_t*)(row_pixels + (x + dx) * 4);
                    if (sample.a == 255) {
                        dst[0] = sample.r; dst[1] = sample.g; dst[2] = sample.b; dst[3] = 255;
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
                dl_blend_glyph_coverage_pixel(p, color, intensity);
            }
        }
    }
}
