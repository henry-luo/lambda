#include "render_glyph.hpp"

#include "../lib/log.h"

// draw a color glyph bitmap (BGRA format, used for color emoji) into the doc surface
// supports bitmap_scale for fixed-size bitmap fonts (e.g. emoji at 109ppem -> 16px)
static void draw_color_glyph(RenderContext* rdcon, GlyphBitmap* bitmap, int x, int y) {
    float bscale = bitmap->bitmap_scale;
    if (bscale <= 0.0f) bscale = 1.0f;

    int target_w = (int)(bitmap->width * bscale + 0.5f);
    int target_h = (int)(bitmap->height * bscale + 0.5f);
    if (target_w <= 0 || target_h <= 0) return;

    int left = max(rdcon->block.clip.left, x);
    int right = min(rdcon->block.clip.right, x + target_w);
    int top = max(rdcon->block.clip.top, y);
    int bottom = min(rdcon->block.clip.bottom, y + target_h);
    if (left >= right || top >= bottom) return;

    bool need_shape_clip = (rdcon->clip_shape_depth > 0) &&
        !clip_shapes_rect_inside(rdcon->clip_shapes, rdcon->clip_shape_depth,
            (float)left + 0.5f, (float)top + 0.5f,
            (float)(right - left - 1), (float)(bottom - top - 1));

    ImageSurface* surface = rdcon->ui_context->surface;
    float inv_scale = 1.0f / bscale;

    for (int dy = top - y; dy < bottom - y; dy++) {
        int row_left = left;
        int row_right = right;
        if (need_shape_clip) {
            float py = (float)(y + dy) + 0.5f;
            clip_shapes_scanline_bounds(rdcon->clip_shapes, rdcon->clip_shape_depth,
                py, left, right, &row_left, &row_right);
            if (row_left >= row_right) continue;
        }
        uint8_t* row_pixels = (uint8_t*)surface->pixels +
            (y + dy - surface->tile_offset_y) * surface->pitch;
        float src_y = dy * inv_scale;
        int sy0 = (int)src_y;
        int sy1 = sy0 + 1;
        float fy = src_y - sy0;
        if (sy0 >= (int)bitmap->height) sy0 = bitmap->height - 1;
        if (sy1 >= (int)bitmap->height) sy1 = bitmap->height - 1;

        for (int dx = row_left - x; dx < row_right - x; dx++) {
            if (x + dx < 0 || x + dx >= surface->width) continue;

            float src_x = dx * inv_scale;
            int sx0 = (int)src_x;
            int sx1 = sx0 + 1;
            float fx = src_x - sx0;
            if (sx0 >= (int)bitmap->width) sx0 = bitmap->width - 1;
            if (sx1 >= (int)bitmap->width) sx1 = bitmap->width - 1;

            const uint8_t* s00 = bitmap->buffer + sy0 * bitmap->pitch + sx0 * 4;
            const uint8_t* s10 = bitmap->buffer + sy0 * bitmap->pitch + sx1 * 4;
            const uint8_t* s01 = bitmap->buffer + sy1 * bitmap->pitch + sx0 * 4;
            const uint8_t* s11 = bitmap->buffer + sy1 * bitmap->pitch + sx1 * 4;

            float w00 = (1 - fx) * (1 - fy);
            float w10 = fx * (1 - fy);
            float w01 = (1 - fx) * fy;
            float w11 = fx * fy;

            uint8_t src_b = (uint8_t)(s00[0] * w00 + s10[0] * w10 + s01[0] * w01 + s11[0] * w11 + 0.5f);
            uint8_t src_g = (uint8_t)(s00[1] * w00 + s10[1] * w10 + s01[1] * w01 + s11[1] * w11 + 0.5f);
            uint8_t src_r = (uint8_t)(s00[2] * w00 + s10[2] * w10 + s01[2] * w01 + s11[2] * w11 + 0.5f);
            uint8_t src_a = (uint8_t)(s00[3] * w00 + s10[3] * w10 + s01[3] * w01 + s11[3] * w11 + 0.5f);

            if (src_a > 0) {
                uint8_t* dst = (uint8_t*)(row_pixels + (x + dx) * 4);
                if (src_a == 255) {
                    dst[0] = src_r;
                    dst[1] = src_g;
                    dst[2] = src_b;
                    dst[3] = 255;
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
}

static uint32_t glyph_bitmap_sample_pixel(const GlyphBitmap* bitmap, int src_y, int src_x) {
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

void draw_glyph(RenderContext* rdcon, GlyphBitmap* bitmap, int x, int y) {
    if (rdcon->color.a == 0) return;
    if (rdcon->dl) {
        bool is_color = (bitmap->pixel_mode == GLYPH_PIXEL_BGRA);
        uint64_t glyph_generation = rdcon->ui_context ?
            font_context_glyph_cache_generation(rdcon->ui_context->font_ctx) : 0;
        dl_draw_glyph(rdcon->dl, bitmap, x, y, rdcon->color, is_color, &rdcon->block.clip,
            rdcon->has_transform ? &rdcon->transform : nullptr, glyph_generation);
        return;
    }
    if (bitmap->pixel_mode == GLYPH_PIXEL_BGRA) {
        draw_color_glyph(rdcon, bitmap, x, y);
        return;
    }

    int left = max(rdcon->block.clip.left, x);
    int right = min(rdcon->block.clip.right, x + (int)bitmap->width);
    int top = max(rdcon->block.clip.top, y);
    int bottom = min(rdcon->block.clip.bottom, y + (int)bitmap->height);
    if (left >= right || top >= bottom) {
        log_debug("glyph clipped: x=%d, y=%d, bitmap=%dx%d, clip=[%.0f,%.0f,%.0f,%.0f]",
            x, y, bitmap->width, bitmap->height,
            rdcon->block.clip.left, rdcon->block.clip.top, rdcon->block.clip.right, rdcon->block.clip.bottom);
        return;
    }
    log_debug("[GLYPH RENDER] drawing glyph at x=%d y=%d size=%dx%d color=#%02x%02x%02x (c=0x%08x) pixel_mode=%d",
        x, y, bitmap->width, bitmap->height,
        rdcon->color.r, rdcon->color.g, rdcon->color.b, rdcon->color.c, bitmap->pixel_mode);

    ImageSurface* surface = rdcon->ui_context->surface;
    bool need_shape_clip = (rdcon->clip_shape_depth > 0) &&
        !clip_shapes_rect_inside(rdcon->clip_shapes, rdcon->clip_shape_depth,
            (float)left + 0.5f, (float)top + 0.5f,
            (float)(right - left - 1), (float)(bottom - top - 1));

    for (int i = top - y; i < bottom - y; i++) {
        int j_start = left - x;
        int j_end = right - x;
        if (need_shape_clip) {
            float py = (float)(y + i) + 0.5f;
            int rl = left;
            int rr = right;
            clip_shapes_scanline_bounds(rdcon->clip_shapes, rdcon->clip_shape_depth,
                py, left, right, &rl, &rr);
            if (rl >= rr) continue;
            j_start = rl - x;
            j_end = rr - x;
        }
        uint8_t* row_pixels = (uint8_t*)surface->pixels +
            (y + i - surface->tile_offset_y) * surface->pitch;
        for (int j = j_start; j < j_end; j++) {
            if (x + j < 0 || x + j >= surface->width) continue;

            uint32_t intensity = glyph_bitmap_sample_pixel(bitmap, i, j);
            if (intensity > 0) {
                uint8_t* p = (uint8_t*)(row_pixels + (x + j) * 4);
                intensity = (intensity * rdcon->color.a + 127) / 255;
                uint32_t v = 255 - intensity;
                if (rdcon->color.c == 0xFF000000) {
                    p[0] = p[0] * v / 255;
                    p[1] = p[1] * v / 255;
                    p[2] = p[2] * v / 255;
                    p[3] = 0xFF;
                } else {
                    p[0] = (p[0] * v + rdcon->color.r * intensity) / 255;
                    p[1] = (p[1] * v + rdcon->color.g * intensity) / 255;
                    p[2] = (p[2] * v + rdcon->color.b * intensity) / 255;
                    p[3] = 0xFF;
                }
            }
        }
    }
}
