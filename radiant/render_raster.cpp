#include "render.hpp"
#include "../lib/log.h"

#include <algorithm>
#include <math.h>

ScaleMode render_image_scale_mode(const ViewSpan* view, bool repeating) {
    CssEnum rendering = view && view->in_line
        ? view->inl()->image_rendering : CSS_VALUE_AUTO;
    if (rendering == CSS_VALUE_PIXELATED) return SCALE_MODE_PIXELATED;
    if (rendering == CSS_VALUE_CRISP_EDGES ||
        rendering == CSS_VALUE_OPTIMIZE_SPEED) return SCALE_MODE_NEAREST;
    return repeating ? SCALE_MODE_LINEAR_WRAP : SCALE_MODE_LINEAR;
}

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

static int raster_pixelated_source_index(float intermediate_index, float multiple,
                                         float source_origin, float source_extent,
                                         int source_limit) {
    float source_index = source_origin + floorf(intermediate_index / multiple);
    float first = std::max(0.0f, floorf(source_origin));
    float last = std::min((float)(source_limit - 1),
                          ceilf(source_origin + source_extent) - 1.0f);
    return (int)std::max(first, std::min(source_index, last)); // INT_CAST_OK: image sample index
}

static uint32_t raster_pixelated_sample(ImageSurface* src, const Rect* src_rect,
                                        const Rect* dst_rect, int dst_x, int dst_y,
                                        int src_w, int src_h,
                                        float x_multiple, float y_multiple) {
    // Sample the virtual nearest-neighbor integer-multiple image through the
    // shared bilinear mixer, so noninteger final scales blend only at pixel edges.
    float ix = (dst_x - dst_rect->x + 0.5f) *
        (src_rect->width * x_multiple / dst_rect->width) - 0.5f;
    float iy = (dst_y - dst_rect->y + 0.5f) *
        (src_rect->height * y_multiple / dst_rect->height) - 0.5f;
    float ix0 = floorf(ix);
    float iy0 = floorf(iy);
    int sx0 = raster_pixelated_source_index(ix0, x_multiple,
        src_rect->x, src_rect->width, src_w);
    int sx1 = raster_pixelated_source_index(ix0 + 1.0f, x_multiple,
        src_rect->x, src_rect->width, src_w);
    int sy0 = raster_pixelated_source_index(iy0, y_multiple,
        src_rect->y, src_rect->height, src_h);
    int sy1 = raster_pixelated_source_index(iy0 + 1.0f, y_multiple,
        src_rect->y, src_rect->height, src_h);
    const uint8_t* pixels = (const uint8_t*)src->pixels;
    const uint8_t* p11 = pixels + (size_t)sy0 * src->pitch + sx0 * 4;
    const uint8_t* p21 = pixels + (size_t)sy0 * src->pitch + sx1 * 4;
    const uint8_t* p12 = pixels + (size_t)sy1 * src->pitch + sx0 * 4;
    const uint8_t* p22 = pixels + (size_t)sy1 * src->pitch + sx1 * 4;
    return render_pixel_bilinear_mix(p11, p21, p12, p22,
                                     ix - ix0, iy - iy0, false);
}

void raster_blit_surface_scaled(RasterPaintContext* ctx, ImageSurface* src, Rect* src_rect,
                                Rect* dst_rect, ScaleMode scale_mode, uint8_t opacity) {
    Rect rect;
    if (!ctx) return;
    ImageSurface* dst = ctx->surface;
    Bound* clip = ctx->clip;
    ClipShape** clip_shapes = ctx->clip_shapes;
    int clip_depth = ctx->clip_depth;
    if (!src || !dst || !dst_rect || dst_rect->width <= 0.0f ||
        dst_rect->height <= 0.0f) return;
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
    if (src_rect->width <= 0.0f || src_rect->height <= 0.0f) return;
    log_debug("blit surface: src(%f, %f, %f, %f) to dst(%f, %f, %f, %f), scale_mode=%d",
        src_rect->x, src_rect->y, src_rect->width, src_rect->height,
        dst_rect->x, dst_rect->y, dst_rect->width, dst_rect->height, scale_mode);

    float x_ratio = (float)src_rect->width / dst_rect->width;
    float y_ratio = (float)src_rect->height / dst_rect->height;
    float pixelated_x_multiple = std::max(1.0f,
        floorf(dst_rect->width / src_rect->width + 0.5f));
    float pixelated_y_multiple = std::max(1.0f,
        floorf(dst_rect->height / src_rect->height + 0.5f));
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
    // 1:1 copy of a whole-pixel-aligned source (cached layers, canvases): integer
    // source-over per row instead of per-pixel float sampling
    if (scale_mode == SCALE_MODE_NEAREST && !need_shape_clip && opacity == 255 &&
        x_ratio == 1.0f && y_ratio == 1.0f &&
        dst_rect->x == floorf(dst_rect->x) && dst_rect->y == floorf(dst_rect->y) &&
        src_rect->x == floorf(src_rect->x) && src_rect->y == floorf(src_rect->y)) {
        int dx0 = (int)dst_rect->x;       // INT_CAST_OK: whole-pixel destination origin
        int dy0 = (int)dst_rect->y;       // INT_CAST_OK: whole-pixel destination origin
        int sx0 = (int)src_rect->x;       // INT_CAST_OK: whole-pixel source origin
        int sy0 = (int)src_rect->y;       // INT_CAST_OK: whole-pixel source origin
        for (int i = top; i < bottom; i++) {
            int sy = sy0 + (i - dy0);
            if (sy < 0 || sy >= src_h) continue;
            const uint32_t* src_row = (const uint32_t*)((const uint8_t*)src->pixels + (size_t)sy * src->pitch);
            uint32_t* dst_row = (uint32_t*)((uint8_t*)dst->pixels + (size_t)(i - y_off) * dst->pitch);
            for (int j = left; j < right; j++) {
                int sx = sx0 + (j - dx0);
                if (sx < 0 || sx >= src_w) continue;
                uint32_t source = src_row[sx];
                uint32_t sa = source >> 24;
                // cached layers are mostly clear or solid: skip or copy those outright
                if (sa == 0) continue;
                if (sa == 255) { dst_row[j] = source | 0xFF000000u; continue; }
                uint32_t destination = dst_row[j];
                uint32_t inv = 255u - sa;
                dst_row[j] = 0xFF000000u |
                    (((source & 0xFFu) * sa + (destination & 0xFFu) * inv) / 255u) |
                    ((((source >> 8) & 0xFFu) * sa + ((destination >> 8) & 0xFFu) * inv) / 255u) << 8 |
                    ((((source >> 16) & 0xFFu) * sa + ((destination >> 16) & 0xFFu) * inv) / 255u) << 16;
            }
        }
        return;
    }
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
            uint8_t* dst_pixel = (uint8_t*)row_pixels + (j * 4);

            uint32_t src_color;
            if (scale_mode == SCALE_MODE_PIXELATED) {
                src_color = raster_pixelated_sample(src, src_rect, dst_rect,
                    j, i, src_w, src_h,
                    pixelated_x_multiple, pixelated_y_multiple);
            } else if (scale_mode == SCALE_MODE_LINEAR && downscaling) {
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
                // Nearest sampling chooses the source pixel under the output
                // pixel center; rounding its left edge shifts a 2x upscale.
                int int_src_x = (int)floorf(src_rect->x +
                    (j - dst_rect->x + 0.5f) * x_ratio); // INT_CAST_OK: image sample index.
                int int_src_y = (int)floorf(src_rect->y +
                    (i - dst_rect->y + 0.5f) * y_ratio); // INT_CAST_OK: image sample index.

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
