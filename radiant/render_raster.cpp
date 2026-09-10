#include "render.hpp"
#include "../lib/log.h"

#include <algorithm>
#include <math.h>

static void raster_fill_row(uint8_t* pixels, int x, int wd, uint32_t color) {
    uint32_t* pixel = (uint32_t*)pixels + x;
    uint32_t* end = pixel + wd;
    uint8_t src_a = (color >> 24) & 0xFF;
    if (src_a == 0) return;
    while (pixel < end) {
        *pixel = src_a == 255 ? color :
            render_pixel_source_over_opaque(*pixel, color);
        pixel++;
    }
}

void raster_fill_rect(RasterPaintContext* ctx, Rect* rect, uint32_t color) {
    Rect r;
    if (!ctx) return;
    ImageSurface* surface = ctx->surface;
    Bound* clip = ctx->clip;
    ClipShape** clip_shapes = ctx->clip_shapes;
    int clip_depth = ctx->clip_depth;
    if (!surface || !surface->pixels) return;
    Bound default_clip = {0, 0, (float)surface->width, (float)surface->height};
    if (!clip) clip = &default_clip;
    if (!rect) { r = (Rect){0, 0, (float)surface->width, (float)surface->height};  rect = &r; }
    log_debug("fill rect: x:%.0f, y:%.0f, wd:%.0f, hg:%.0f, color:%x",
        rect->x, rect->y, rect->width, rect->height, color);

    int left = (int)roundf(std::max(clip->left, rect->x));
    int right = (int)roundf(std::min(clip->right, rect->x + rect->width));
    int top = (int)roundf(std::max(clip->top, rect->y));
    int bottom = (int)roundf(std::min(clip->bottom, rect->y + rect->height));
    if (left >= right || top >= bottom) return;

    int y_off = surface->tile_offset_y;
    bool need_shape_clip = clip_depth > 0 &&
        !clip_shapes_rect_inside(clip_shapes, clip_depth,
            (float)left + 0.5f, (float)top + 0.5f,
            (float)(right - left - 1), (float)(bottom - top - 1));

    for (int i = top; i < bottom; i++) {
        int rl = left;
        int rr = right;
        if (need_shape_clip) {
            float py = (float)i + 0.5f;
            clip_shapes_scanline_bounds(clip_shapes, clip_depth, py,
                                        left, right, &rl, &rr);
        }
        if (rl >= rr) continue;
        uint8_t* row_pixels = (uint8_t*)surface->pixels + (i - y_off) * surface->pitch;
        raster_fill_row(row_pixels, rl, rr - rl, color);
    }
}

static uint32_t raster_area_average(ImageSurface* src, float x0, float y0, float x1, float y1) {
    int src_w = (src->decoded_width > 0) ? src->decoded_width : src->width;
    int src_h = (src->decoded_height > 0) ? src->decoded_height : src->height;
    int ix0 = std::max(0, (int)x0);
    int iy0 = std::max(0, (int)y0);
    int ix1 = std::min(src_w, (int)(x1 + 1.0f));
    int iy1 = std::min(src_h, (int)(y1 + 1.0f));

    if (ix0 >= ix1 || iy0 >= iy1) return 0;

    float sum_r = 0;
    float sum_g = 0;
    float sum_b = 0;
    float sum_a = 0;
    float total_weight = 0;

    for (int y = iy0; y < iy1; y++) {
        float wy = 1.0f;
        if (y < y0) wy = 1.0f - (y0 - y);
        if (y + 1 > y1) wy = y1 - y;
        if (wy <= 0) continue;

        uint8_t* row = (uint8_t*)src->pixels + y * src->pitch;
        for (int x = ix0; x < ix1; x++) {
            float wx = 1.0f;
            if (x < x0) wx = 1.0f - (x0 - x);
            if (x + 1 > x1) wx = x1 - x;
            if (wx <= 0) continue;

            float w = wx * wy;
            uint32_t pixel = *((uint32_t*)(row + x * 4));
            sum_r += (pixel & 0xFF) * w;
            sum_g += ((pixel >> 8) & 0xFF) * w;
            sum_b += ((pixel >> 16) & 0xFF) * w;
            sum_a += ((pixel >> 24) & 0xFF) * w;
            total_weight += w;
        }
    }

    if (total_weight <= 0) return 0;

    float inv = 1.0f / total_weight;
    uint8_t r = (uint8_t)std::min(255.0f, sum_r * inv + 0.5f);
    uint8_t g = (uint8_t)std::min(255.0f, sum_g * inv + 0.5f);
    uint8_t b = (uint8_t)std::min(255.0f, sum_b * inv + 0.5f);
    uint8_t a = (uint8_t)std::min(255.0f, sum_a * inv + 0.5f);

    return r | (g << 8) | (b << 16) | (a << 24);
}

void raster_blit_surface_scaled(RasterPaintContext* ctx, ImageSurface* src, Rect* src_rect,
                                Rect* dst_rect, ScaleMode scale_mode, uint8_t opacity) {
    Rect rect;
    if (!ctx) return;
    ImageSurface* dst = ctx->surface;
    Bound* clip = ctx->clip;
    ClipShape** clip_shapes = ctx->clip_shapes;
    int clip_depth = ctx->clip_depth;
    if (!src || !dst || !dst_rect) return;
    int src_w = (src->decoded_width > 0) ? src->decoded_width : src->width;
    int src_h = (src->decoded_height > 0) ? src->decoded_height : src->height;
    Bound default_clip = {0, 0, (float)dst->width, (float)dst->height};
    if (!clip) clip = &default_clip;
    if (!src->pixels) {
        log_error("raster_blit_surface_scaled: src->pixels is NULL!");
        return;
    }
    if (!dst->pixels) {
        log_error("raster_blit_surface_scaled: dst->pixels is NULL!");
        return;
    }
    if (!src_rect) {
        int src_w = (src->decoded_width > 0) ? src->decoded_width : src->width;
        int src_h = (src->decoded_height > 0) ? src->decoded_height : src->height;
        rect = (Rect){0, 0, (float)src_w, (float)src_h};
        src_rect = &rect;
    }
    log_debug("blit surface: src(%f, %f, %f, %f) to dst(%f, %f, %f, %f), scale_mode=%d",
        src_rect->x, src_rect->y, src_rect->width, src_rect->height,
        dst_rect->x, dst_rect->y, dst_rect->width, dst_rect->height, scale_mode);

    float x_ratio = (float)src_rect->width / dst_rect->width;
    float y_ratio = (float)src_rect->height / dst_rect->height;
    bool downscaling = (x_ratio > 1.5f || y_ratio > 1.5f);
    int left = (int)std::max(clip->left, dst_rect->x);
    int right = (int)std::min(clip->right, dst_rect->x + dst_rect->width);
    int top = (int)std::max(clip->top, dst_rect->y);
    int bottom = (int)std::min(clip->bottom, dst_rect->y + dst_rect->height);
    if (left >= right || top >= bottom) return;

    bool need_shape_clip = (clip_depth > 0) &&
        !clip_shapes_rect_inside(clip_shapes, clip_depth,
            (float)left + 0.5f, (float)top + 0.5f,
            (float)(right - left - 1), (float)(bottom - top - 1));

    int y_off = dst->tile_offset_y;
    for (int i = top; i < bottom; i++) {
        int row_left = left;
        int row_right = right;
        if (need_shape_clip) {
            float py = (float)i + 0.5f;
            clip_shapes_scanline_bounds(clip_shapes, clip_depth, py, left, right, &row_left, &row_right);
            if (row_left >= row_right) continue;
        }
        uint8_t* row_pixels = (uint8_t*)dst->pixels + (i - y_off) * dst->pitch;
        for (int j = row_left; j < row_right; j++) {
            float src_x = src_rect->x + (j - dst_rect->x) * x_ratio;
            float src_y = src_rect->y + (i - dst_rect->y) * y_ratio;

            uint8_t* dst_pixel = (uint8_t*)row_pixels + (j * 4);

            uint32_t src_color;
            if (scale_mode == SCALE_MODE_LINEAR && downscaling) {
                float box_x0 = src_rect->x + (j - dst_rect->x) * x_ratio;
                float box_y0 = src_rect->y + (i - dst_rect->y) * y_ratio;
                float box_x1 = box_x0 + x_ratio;
                float box_y1 = box_y0 + y_ratio;
                src_color = raster_area_average(src, box_x0, box_y0, box_x1, box_y1);
            } else if (scale_mode == SCALE_MODE_LINEAR) {
                float bx = src_rect->x + (j - dst_rect->x + 0.5f) * x_ratio - 0.5f;
                float by = src_rect->y + (i - dst_rect->y + 0.5f) * y_ratio - 0.5f;
                src_color = render_pixel_sample_bilinear(
                    (const uint8_t*)src->pixels, src_w, src_h, src->pitch, bx, by, false, false);
            } else if (scale_mode == SCALE_MODE_LINEAR_WRAP) {
                float bx = src_rect->x + (j - dst_rect->x + 0.5f) * x_ratio - 0.5f;
                float by = src_rect->y + (i - dst_rect->y + 0.5f) * y_ratio - 0.5f;
                src_color = render_pixel_sample_bilinear(
                    (const uint8_t*)src->pixels, src_w, src_h, src->pitch, bx, by, true, false);
            } else {
                int int_src_x = (int)(src_x + 0.5f);
                int int_src_y = (int)(src_y + 0.5f);

                if (int_src_x < 0 || int_src_x >= src_w || int_src_y < 0 || int_src_y >= src_h) {
                    continue;
                }

                uint8_t* src_pixel = (uint8_t*)src->pixels + (int_src_y * src->pitch) + (int_src_x * 4);
                src_color = *((uint32_t*)src_pixel);
            }

            render_pixel_source_over_opaque_bytes(dst_pixel, src_color, opacity);
        }
    }
}

void raster_blit_pixels_scaled(RasterPaintContext* ctx, const uint32_t* pixels,
                               int src_w, int src_h, int src_stride,
                               Rect* dst_rect, ScaleMode scale_mode, uint8_t opacity) {
    if (!pixels || src_w <= 0 || src_h <= 0 || src_stride <= 0) return;
    ImageSurface src = {};
    src.format = IMAGE_FORMAT_UNKNOWN;
    src.width = src_w;
    src.height = src_h;
    src.encoded_width = src_w;
    src.encoded_height = src_h;
    src.orientation = 1;
    src.has_intrinsic_size = true;
    src.pitch = src_stride * 4;
    src.pixels = (void*)pixels;
    raster_blit_surface_scaled(ctx, &src, NULL, dst_rect, scale_mode, opacity);
}
