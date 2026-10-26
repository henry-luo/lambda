#include "render.hpp"
#include "render_row_jobs.hpp"
#include "raster_sampling.hpp"
#include "../lib/log.h"

#include <algorithm>
#include <math.h>

ScaleMode render_image_scale_mode(const DomElement* element, bool repeating) {
    CssEnum rendering = element && element->in_line
        ? element->inl()->image_rendering : CSS_VALUE_AUTO;
    if (rendering == CSS_VALUE_PIXELATED) return SCALE_MODE_PIXELATED;
    if (rendering == CSS_VALUE_CRISP_EDGES ||
        rendering == CSS_VALUE_OPTIMIZE_SPEED) return SCALE_MODE_NEAREST;
    return repeating ? SCALE_MODE_LINEAR_WRAP : SCALE_MODE_LINEAR;
}

static void raster_fill_row(uint8_t* pixels, int x, int wd, uint32_t color, ImageAlphaMode alpha_mode) {
    uint32_t* pixel = (uint32_t*)pixels + x;
    uint32_t* end = pixel + wd;
    uint8_t src_a = (color >> 24) & 0xFF;
    if (src_a == 0) return;
    while (pixel < end) {
        // Isolated effect regions start transparent even on a window surface.
        *pixel = src_a == 255 ? color : alpha_mode == IMAGE_ALPHA_PREMULTIPLIED
            ? render_pixel_source_over_premultiplied_pair(*pixel, render_pixel_premultiply_abgr(color))
            : render_pixel_source_over_straight(*pixel, color, 255);
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
    bool need_shape_clip = !clip_shapes_pixel_box_inside(clip_shapes, clip_depth, left, top, right, bottom);

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
        raster_fill_row(row_pixels, rl, rr - rl, color, surface->alpha_mode);
    }
}

static uint32_t raster_area_average(ImageSurface* src, float x0, float y0, float x1, float y1,
                                    bool premultiply_samples) {
    int src_w = (src->decoded_width > 0) ? src->decoded_width : src->width;
    int src_h = (src->decoded_height > 0) ? src->decoded_height : src->height;
    float sum_r = 0;
    float sum_g = 0;
    float sum_b = 0;
    float sum_a = 0;
    float total_weight = raster_visit_sample_area(src_w, src_h, x0, y0, x1, y1,
        [&](int y, int x, float w) {
            uint8_t* row = (uint8_t*)src->pixels + y * src->pitch;
            uint32_t pixel = *((uint32_t*)(row + x * 4));
            if (premultiply_samples) pixel = render_pixel_premultiply_abgr(pixel);
            sum_r += (pixel & 0xFF) * w;
            sum_g += ((pixel >> 8) & 0xFF) * w;
            sum_b += ((pixel >> 16) & 0xFF) * w;
            sum_a += ((pixel >> 24) & 0xFF) * w;
        });

    if (total_weight <= 0) return 0;

    float inv = 1.0f / total_weight;
    uint8_t r = (uint8_t)std::min(255.0f, sum_r * inv + 0.5f);
    uint8_t g = (uint8_t)std::min(255.0f, sum_g * inv + 0.5f);
    uint8_t b = (uint8_t)std::min(255.0f, sum_b * inv + 0.5f);
    uint8_t a = (uint8_t)std::min(255.0f, sum_a * inv + 0.5f);

    return r | (g << 8) | (b << 16) | (a << 24);
}

static double raster_pixelated_multiple(double ratio) {
    return fmax(1.0, floor(ratio + 0.5));
}

static int raster_pixelated_source_index(double intermediate_index, double multiple,
                                         float source_origin, float source_extent,
                                         int source_limit) {
    double source_index = source_origin + floor(intermediate_index / multiple);
    double first = fmax(0.0, floor(source_origin));
    double last = fmin((double)(source_limit - 1),
                       ceil((double)source_origin + source_extent) - 1.0);
    return (int)fmax(first, fmin(source_index, last)); // INT_CAST_OK: bounded image sample index
}

static uint32_t raster_pixelated_sample(ImageSurface* src, const Rect* src_rect,
                                        const Rect* dst_rect, float dst_x, float dst_y,
                                        int src_w, int src_h,
                                        double x_multiple, double y_multiple, bool premultiply = false) {
    // Sample the virtual nearest-neighbor integer-multiple image through the
    // shared bilinear mixer, so noninteger final scales blend only at pixel edges.
    // virtual integer-sized images need no allocation or float-sized pixel index.
    double ix = ((double)dst_x - dst_rect->x + 0.5) / dst_rect->width *
        src_rect->width * x_multiple - 0.5;
    double iy = ((double)dst_y - dst_rect->y + 0.5) / dst_rect->height *
        src_rect->height * y_multiple - 0.5;
    double ix0 = floor(ix);
    double iy0 = floor(iy);
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
                                     ix - ix0, iy - iy0, true, premultiply);
}

bool render_image_project_pixels(const uint32_t* pixels, int width, int height, int stride,
    Rect destination, const RdtMatrix* transform, Rect viewport, ScaleMode mode,
    bool straight_alpha, uint32_t** output, Rect* output_rect, const Rect* repeated_tile) {
    if (!output || !output_rect) return false;
    *output = nullptr; *output_rect = {};
    if (!pixels || width <= 0 || height <= 0 || stride < width ||
        destination.width <= 0 || destination.height <= 0 ||
        (repeated_tile && (repeated_tile->width <= 0 || repeated_tile->height <= 0))) return false;
    RdtMatrix matrix = transform ? *transform : rdt_matrix_identity(), inverse;
    if (!rdt_matrix_inverse(&matrix, &inverse)) return true;
    float left, top, right, bottom;
    if (!rdt_matrix_project_rect_bounds(&matrix, destination.x, destination.y,
        destination.x + destination.width, destination.y + destination.height, &left, &top, &right, &bottom)) {
        // A viewer-plane crossing is unbounded before clipping. Inverse-map
        // only the viewport and reject samples on the hidden side below.
        left = viewport.x; top = viewport.y;
        right = viewport.x + viewport.width; bottom = viewport.y + viewport.height;
    }
    // clip to the viewport first, then snap outward to whole pixels: a scaled
    // ancestor gives the viewport fractional edges, and snapping before the
    // clip left a sub-pixel span that truncated to an empty buffer.
    left = floorf(fmaxf(viewport.x, left)); top = floorf(fmaxf(viewport.y, top));
    right = ceilf(fminf(viewport.x + viewport.width, right));
    bottom = ceilf(fminf(viewport.y + viewport.height, bottom));
    if (right <= left || bottom <= top) return true;
    int out_w = (int)(right - left), out_h = (int)(bottom - top); // INT_CAST_OK: bounded raster buffer dimensions
    if ((size_t)out_w > SIZE_MAX / sizeof(uint32_t) / (size_t)out_h) return false;
    uint32_t* projected = (uint32_t*)mem_calloc((size_t)out_w * out_h, sizeof(uint32_t), MEM_CAT_RENDER);
    if (!projected) return false;
    lam::Temp<uint32_t> prepared;
    if (straight_alpha && (size_t)width <= SIZE_MAX / sizeof(uint32_t) / (size_t)height &&
        (size_t)width * height <= (size_t)out_w * out_h * 4) {
        // Expand alpha once per source texel when projection will reuse it.
        // This preserves the sampler's byte rounding and avoids four conversions per pixel.
        prepared.reset((uint32_t*)mem_alloc((size_t)width * height * sizeof(uint32_t), MEM_CAT_RENDER));
        if (prepared) {
            for (int row = 0; row < height; row++)
                memcpy(prepared.get() + (size_t)row * width, pixels + (size_t)row * stride,
                    (size_t)width * sizeof(uint32_t));
            ImageSurface source_copy = {};
            source_copy.width = width; source_copy.height = height;
            source_copy.pitch = width * 4; source_copy.pixels = prepared.get();
            premultiply_surface_region(&source_copy, 0, 0, width, height);
            pixels = prepared.get(); stride = width; straight_alpha = false;
        }
    }
    ImageSurface source = {};
    source.width = width; source.height = height; source.pitch = stride * 4; source.pixels = (void*)pixels;
    Rect source_rect = {0, 0, (float)width, (float)height};
    Rect sampling_rect = repeated_tile ? *repeated_tile : destination;
    // affine axis-aligned minification has the same pixel footprint as an ordinary CPU blit.
    float footprint_x = fabsf(inverse.e11 / inverse.e33) * width / sampling_rect.width;
    float footprint_y = fabsf(inverse.e22 / inverse.e33) * height / sampling_rect.height;
    // edge mode: the destination quad's edges carry their own coverage, so a projected
    // layer needs no antialiasing clip; fringe pixels sample the clamped edge texel
    bool edge_coverage = mode == SCALE_MODE_LINEAR_EDGE && !repeated_tile;
    float edges[4][3] = {};
    if (edge_coverage) {
        float xs[4], ys[4];
        const float cu[4] = {destination.x, destination.x + destination.width, destination.x + destination.width, destination.x};
        const float cv[4] = {destination.y, destination.y, destination.y + destination.height, destination.y + destination.height};
        for (int i = 0; i < 4 && edge_coverage; i++)
            edge_coverage = rdt_matrix_project_point(&matrix, cu[i], cv[i], &xs[i], &ys[i]);
        float area = 0.0f;
        for (int i = 0; i < 4 && edge_coverage; i++) area += xs[i] * ys[(i + 1) % 4] - xs[(i + 1) % 4] * ys[i];
        edge_coverage = edge_coverage && fabsf(area) > 0.01f;
        for (int i = 0; i < 4 && edge_coverage; i++) {
            float dx = xs[(i + 1) % 4] - xs[i], dy = ys[(i + 1) % 4] - ys[i];
            float length = sqrtf(dx * dx + dy * dy);
            if (length < 0.0001f) { edge_coverage = false; break; }
            // signed distance to the edge line, positive inside whatever the winding
            float sign = area > 0.0f ? 1.0f : -1.0f;
            edges[i][0] = -dy / length * sign; edges[i][1] = dx / length * sign;
            edges[i][2] = -(edges[i][0] * xs[i] + edges[i][1] * ys[i]);
        }
    }
    bool area_sample = (mode == SCALE_MODE_LINEAR || mode == SCALE_MODE_LINEAR_EDGE) && !repeated_tile &&
        inverse.e12 == 0.0f && inverse.e21 == 0.0f && inverse.e31 == 0.0f && inverse.e32 == 0.0f &&
        (footprint_x > 1.0f || footprint_y > 1.0f);
    double x_scale = 1.0, y_scale = 1.0;
    if (mode == SCALE_MODE_PIXELATED && matrix.e31 == 0.0f &&
        matrix.e32 == 0.0f && matrix.e33 != 0.0f) {
        // affine pixelation follows device-space axis lengths, including raster density.
        x_scale = hypot((double)matrix.e11, matrix.e21) / fabs((double)matrix.e33);
        y_scale = hypot((double)matrix.e12, matrix.e22) / fabs((double)matrix.e33);
    }
    double multiple_x = raster_pixelated_multiple((double)sampling_rect.width / width * x_scale);
    double multiple_y = raster_pixelated_multiple((double)sampling_rect.height / height * y_scale);
    auto project_rows = [&](int row_begin, int row_end) {
        for (int row = row_begin; row < row_end; row++) {
            for (int column = 0; column < out_w; column++) {
                float x = left + column + 0.5f, y = top + row + 0.5f;
                float coverage = 1.0f;
                if (edge_coverage) {
                    float distance = INFINITY;
                    for (int i = 0; i < 4; i++)
                        distance = fminf(distance, edges[i][0] * x + edges[i][1] * y + edges[i][2]);
                    if (distance <= -0.5f) continue;
                    coverage = fminf(1.0f, distance + 0.5f);
                }
                float w = inverse.e31 * x + inverse.e32 * y + inverse.e33;
                if (fabsf(w) <= 0.000001f) continue;
                float u = (inverse.e11 * x + inverse.e12 * y + inverse.e13) / w;
                float v = (inverse.e21 * x + inverse.e22 * y + inverse.e23) / w;
                if (edge_coverage && isfinite(u) && isfinite(v)) {
                    u = fminf(fmaxf(u, destination.x), nextafterf(destination.x + destination.width, destination.x));
                    v = fminf(fmaxf(v, destination.y), nextafterf(destination.y + destination.height, destination.y));
                }
                if (!isfinite(u) || !isfinite(v) || u < destination.x || v < destination.y ||
                    u >= destination.x + destination.width || v >= destination.y + destination.height ||
                    matrix.e31 * u + matrix.e32 * v + matrix.e33 <= 0.00001f) continue;
                if (repeated_tile) {
                    // Fold in plane coordinates, before filtering: large repeated
                    // backgrounds need one viewport-sized projection, not one per tile.
                    u = fmodf(u - sampling_rect.x, sampling_rect.width);
                    v = fmodf(v - sampling_rect.y, sampling_rect.height);
                    if (u < 0) u += sampling_rect.width;
                    if (v < 0) v += sampling_rect.height;
                    u += sampling_rect.x; v += sampling_rect.y;
                }
                float sx = (u - sampling_rect.x) * width / sampling_rect.width;
                float sy = (v - sampling_rect.y) * height / sampling_rect.height;
                uint32_t sample;
                if (mode == SCALE_MODE_NEAREST) {
                    int ix = min(width - 1, (int)floorf(sx)), iy = min(height - 1, (int)floorf(sy)); // INT_CAST_OK: validated source pixel indices
                    sample = pixels[(size_t)iy * stride + ix];
                    if (straight_alpha) sample = render_pixel_premultiply_abgr(sample);
                } else if (mode == SCALE_MODE_PIXELATED) {
                    sample = raster_pixelated_sample(&source, &source_rect, &sampling_rect, u - 0.5f, v - 0.5f,
                        width, height, multiple_x, multiple_y, straight_alpha);
                } else if (area_sample) {
                    sample = raster_area_average(&source, sx - footprint_x * 0.5f, sy - footprint_y * 0.5f,
                        sx + footprint_x * 0.5f, sy + footprint_y * 0.5f, straight_alpha);
                } else {
                    sample = render_pixel_sample_bilinear((const uint8_t*)pixels, width, height, stride * 4,
                        sx - 0.5f, sy - 0.5f, mode == SCALE_MODE_LINEAR_WRAP, true, straight_alpha);
                }
                if (coverage < 1.0f) {
                    // premultiplied: every channel scales with coverage
                    uint8_t* channel = (uint8_t*)&sample;
                    for (int c = 0; c < 4; c++) channel[c] = (uint8_t)lroundf(channel[c] * coverage);
                }
                projected[(size_t)row * out_w + column] = sample;
            }
    }
    };
    render_parallel_rows(out_w, out_h, project_rows);
    *output = projected; *output_rect = {left, top, (float)out_w, (float)out_h};
    return true;
}

static uint32_t raster_image_source_over(uint32_t destination, uint32_t source,
        uint8_t opacity, bool straight_source, bool premultiplied_destination) {
    if (straight_source) source = render_pixel_premultiply_abgr(source);
    source = render_pixel_scale_premultiplied(source, opacity);
    return premultiplied_destination
        ? render_pixel_source_over_premultiplied_pair(destination, source)
        : render_pixel_source_over_premultiplied(destination, source);
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
    double pixelated_x_multiple = raster_pixelated_multiple((double)dst_rect->width / src_rect->width);
    double pixelated_y_multiple = raster_pixelated_multiple((double)dst_rect->height / src_rect->height);
    bool downscaling = (x_ratio > 1.5f || y_ratio > 1.5f);
    int left = (int)std::max(clip->left, dst_rect->x);
    int right = (int)std::min(clip->right, dst_rect->x + dst_rect->width);
    int top = (int)std::max(clip->top, dst_rect->y);
    int bottom = (int)std::min(clip->bottom, dst_rect->y + dst_rect->height);
    if (left >= right || top >= bottom) return;
    // Filter premultiplied texels and preserve the destination's alpha contract.
    bool straight_source = src->alpha_mode == IMAGE_ALPHA_STRAIGHT;
    bool premultiplied_destination = dst->alpha_mode == IMAGE_ALPHA_PREMULTIPLIED;

    bool need_shape_clip = !clip_shapes_pixel_box_inside(clip_shapes, clip_depth, left, top, right, bottom);

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
                dst_row[j] = raster_image_source_over(dst_row[j], source, opacity,
                    straight_source, premultiplied_destination);
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
                    pixelated_x_multiple, pixelated_y_multiple, straight_source);
            } else if ((scale_mode == SCALE_MODE_LINEAR || scale_mode == SCALE_MODE_LINEAR_EDGE) && downscaling) {
                float box_x0 = src_rect->x + (j - dst_rect->x) * x_ratio;
                float box_y0 = src_rect->y + (i - dst_rect->y) * y_ratio;
                float box_x1 = box_x0 + x_ratio;
                float box_y1 = box_y0 + y_ratio;
                src_color = raster_area_average(src, box_x0, box_y0, box_x1, box_y1, straight_source);
            } else if (scale_mode == SCALE_MODE_LINEAR || scale_mode == SCALE_MODE_LINEAR_EDGE) {
                float bx = src_rect->x + (j - dst_rect->x + 0.5f) * x_ratio - 0.5f;
                float by = src_rect->y + (i - dst_rect->y + 0.5f) * y_ratio - 0.5f;
                src_color = render_pixel_sample_bilinear(
                    (const uint8_t*)src->pixels, src_w, src_h, src->pitch, bx, by, false, true, straight_source);
            } else if (scale_mode == SCALE_MODE_LINEAR_WRAP) {
                float bx = src_rect->x + (j - dst_rect->x + 0.5f) * x_ratio - 0.5f;
                float by = src_rect->y + (i - dst_rect->y + 0.5f) * y_ratio - 0.5f;
                src_color = render_pixel_sample_bilinear(
                    (const uint8_t*)src->pixels, src_w, src_h, src->pitch, bx, by, true, true, straight_source);
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
                if (straight_source) src_color = render_pixel_premultiply_abgr(src_color);
            }

            uint32_t destination;
            memcpy(&destination, dst_pixel, sizeof(destination));
            destination = raster_image_source_over(destination, src_color, opacity,
                false, premultiplied_destination);
            memcpy(dst_pixel, &destination, sizeof(destination));
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
