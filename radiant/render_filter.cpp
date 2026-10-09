#ifdef __APPLE__
#define Rect MacOSRect
#include <Accelerate/Accelerate.h>
#undef Rect
#endif

#include "render.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/math_utils.h"
#include <math.h>
#include <algorithm>
#include <string.h>

bool render_filter_is_color_only(const FilterFunction* functions) {
    if (!functions) return false;
    for (const FilterFunction* function = functions; function; function = function->next) {
        switch (function->type) {
            case FILTER_BRIGHTNESS: case FILTER_CONTRAST: case FILTER_GRAYSCALE:
            case FILTER_HUE_ROTATE: case FILTER_INVERT: case FILTER_OPACITY:
            case FILTER_SATURATE: case FILTER_SEPIA: break;
            default: return false;
        }
    }
    return true;
}
void radiant::release_filter_snapshots(FilterFunction* functions) {
    for (; functions; functions = functions->next) {
        if (!functions->svg) continue;
        render_svg_filter_program_release(functions->svg->program);
        mem_free(functions->svg.get());
        functions->svg = nullptr;
    }
}

void radiant::destroy_filter_list(Pool* pool, FilterFunction* functions) {
    release_filter_snapshots(functions);
    while (functions) {
        FilterFunction* next = functions->next;
        if (functions->type == FILTER_URL && functions->params.url) pool_free(pool, (void*)functions->params.url.get());
        pool_free(pool, functions);
        functions = next;
    }
}

FilterFunction* radiant::clone_filter_list(Pool* pool, const FilterFunction* source) {
    FilterFunction* head = nullptr;
    FilterFunction* tail = nullptr;
    for (; source; source = source->next) {
        auto* copy = (FilterFunction*)pool_alloc(pool, sizeof(FilterFunction));
        if (!copy) { destroy_filter_list(pool, head); return nullptr; }
        *copy = *source; copy->next = nullptr; copy->svg = nullptr;
        if (source->type == FILTER_URL) {
            copy->params.url = lam::own(source->params.url ? pool_strdup(pool, source->params.url) : nullptr);
            if (source->params.url && !copy->params.url) { pool_free(pool, copy); destroy_filter_list(pool, head); return nullptr; }
        }
        if (tail) tail->next = lam::own(copy); else head = copy;
        tail = copy;
    }
    return head;
}

void render_filter_prepare_urls(RasterRenderContext* context, FilterProp* filter) {
    if (!context || !filter) return;
    for (FilterFunction* function = filter->functions; function; function = function->next) {
        if (function->type != FILTER_URL) continue;
        DomDocument* document = function->source_document;
        DomNode* owner = dom_node_ref_validate(document, function->source_owner);
        if (!owner || !owner->is_element()) continue;
        if (!function->svg) function->svg = lam::own((CssSvgFilter*)mem_calloc(1, sizeof(CssSvgFilter), MEM_CAT_RENDER));
        if (!function->svg) continue;
        auto* snapshot = function->svg.get();
        RdtSvgFilterProgram* program = render_css_svg_filter_compile(document, function->params.url, &context->scratch);
        render_svg_filter_program_release(snapshot->program);
        snapshot->program = lam::up(program);
        snapshot->lengths = dom_svg_length_context((DomElement*)owner);
        snapshot->lengths.fonts = nullptr; // replay consumes frozen metrics, never a font-context mutation
        snapshot->memory = lam::up((MemContext*)document->services.mem_ctx);
        snapshot->density = context->raster_scale;
    }
}

static RdtSvgFilterRun css_svg_filter_run(const CssSvgFilter* filter) {
    RdtSvgFilterRun run = {};
    if (!filter) return run;
    run.geometry = filter->geometry; run.frame = filter->frame;
    run.lengths = filter->lengths; run.memory = filter->memory;
    run.density = filter->density;
    memtrack_get_limits(nullptr, nullptr, &run.work_limit);
    return run;
}

float render_filter_reference_box(FilterProp* filter, Rect border, const RdtMatrix* transform) {
    float expand = 0;
    for (FilterFunction* function = filter ? filter->functions.get() : nullptr; function; function = function->next) {
        auto* snapshot = function->svg.get();
        if (!snapshot || !snapshot->program || snapshot->density <= 0) continue;
        snapshot->geometry = {0, 0, border.width / snapshot->density, border.height / snapshot->density};
        snapshot->frame = rdt_matrix_scale(snapshot->density, snapshot->density);
        snapshot->frame.e13 = border.x; snapshot->frame.e23 = border.y;
        if (transform) snapshot->frame = rdt_matrix_multiply(transform, &snapshot->frame);
        RdtSvgFilterRun run = css_svg_filter_run(snapshot);
        Bound region;
        if (render_svg_filter_region(snapshot->program, &run, &region)) {
            expand = fmaxf(expand, fmaxf(fmaxf(-region.left, -region.top),
                fmaxf(region.right - run.geometry.right, region.bottom - run.geometry.bottom)) * run.density);
        }
    }
    return fmaxf(0, expand);
}

bool render_css_svg_filter_apply(ScratchArena* scratch, ImageSurface* surface,
    const CssSvgFilter* filter, const Rect* rect, const Bound* clip) {
    if (!scratch || !surface || !surface->pixels || !filter || !filter->program || !rect) return false;
    RdtSvgFilterRun run = css_svg_filter_run(filter);
    run.scratch = lam::up(scratch); run.source = lam::up(surface);
    run.source_bounds = {0, 0, (float)surface->width, (float)surface->height};
    run.frame.e13 += rect->x - filter->paint_x; run.frame.e23 += rect->y - filter->paint_y;
    size_t work_used = 0; run.work_used = lam::up(&work_used);
    ImageSurface* output = nullptr; Bound bounds; RdtMatrix placement;
    if (!render_svg_filter_execute(filter->program, &run, &output, &bounds, &placement) || !output) return false;
    Bound area = {0, 0, (float)surface->width, (float)surface->height};
    if (clip) area = *clip;
    area.left = fmaxf(area.left, fmaxf(0, rect->x));
    area.top = fmaxf(area.top, fmaxf(0, rect->y));
    area.right = fminf(area.right, fminf((float)surface->width, rect->x + rect->width));
    area.bottom = fminf(area.bottom, fminf((float)surface->height, rect->y + rect->height));
    Rect projected_rect;
    uint32_t* projected = nullptr;
    bool valid = render_image_project_pixels((uint32_t*)output->pixels, output->width, output->height,
        output->pitch / 4, {bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top},
        &placement, {area.left, area.top, area.right - area.left, area.bottom - area.top},
        SCALE_MODE_LINEAR, false, &projected, &projected_rect);
    image_surface_destroy(output);
    if (!valid) return false;
    // inverse projection writes every destination pixel once, including enlarged and tilted filters.
    IRect region = render_geometry_clip_to_pixel_bounds(area, surface);
    for (int row = region.y; row < region.y + region.h; row++)
        memset((uint8_t*)surface->pixels + (size_t)row * surface->pitch + (size_t)region.x * 4, 0, (size_t)region.w * 4);
    int width = (int)projected_rect.width, height = (int)projected_rect.height; // INT_CAST_OK: bounded projected raster dimensions
    int left = (int)projected_rect.x, top = (int)projected_rect.y; // INT_CAST_OK: bounded projected raster origin
    for (int row = 0; row < height; row++) for (int column = 0; column < width; column++) {
        uint32_t pixel = projected[(size_t)row * width + column];
        if (surface->alpha_mode == IMAGE_ALPHA_STRAIGHT) pixel = render_pixel_unpremultiply_abgr(pixel);
        ((uint32_t*)surface->pixels)[(size_t)(top + row) * (surface->pitch / 4) + left + column] = pixel;
    }
    mem_free(projected);
    return true;
}

/**
 * CSS Filter Implementation
 *
 * Filters are applied in the order they appear in the filter property.
 * Color manipulation is done in RGB space with standard CSS filter algorithms.
 *
 * References:
 * - CSS Filter Effects Module Level 1: https://www.w3.org/TR/filter-effects-1/
 * - SVG Filter Effects: https://www.w3.org/TR/SVG11/filters.html
 */

/**
 * grayscale(amount)
 * Converts to grayscale. amount=0 is no effect, amount=1 is full grayscale.
 * Uses luminance formula: 0.2126*R + 0.7152*G + 0.0722*B
 */
static void filter_grayscale(uint8_t* r, uint8_t* g, uint8_t* b, float amount) {
    amount = clamp_unit(amount);
    if (amount == 0) return;

    float gray = 0.2126f * (*r) + 0.7152f * (*g) + 0.0722f * (*b);

    // Interpolate between original and grayscale
    *r = clamp_byte_round(*r + amount * (gray - *r));
    *g = clamp_byte_round(*g + amount * (gray - *g));
    *b = clamp_byte_round(*b + amount * (gray - *b));
}

/**
 * brightness(amount)
 * Adjusts brightness. amount=1 is no effect, <1 is darker, >1 is brighter.
 * Linear multiplication of RGB values.
 */
static void filter_brightness(uint8_t* r, uint8_t* g, uint8_t* b, float amount) {
    if (amount < 0) amount = 0;  // Clamp negative to 0

    *r = clamp_byte_round(*r * amount);
    *g = clamp_byte_round(*g * amount);
    *b = clamp_byte_round(*b * amount);
}

/**
 * contrast(amount)
 * Adjusts contrast. amount=1 is no effect, <1 is less contrast, >1 is more contrast.
 * Formula: (value - 0.5) * amount + 0.5
 */
static void filter_contrast(uint8_t* r, uint8_t* g, uint8_t* b, float amount) {
    if (amount < 0) amount = 0;

    float rf = (*r / 255.0f - 0.5f) * amount + 0.5f;
    float gf = (*g / 255.0f - 0.5f) * amount + 0.5f;
    float bf = (*b / 255.0f - 0.5f) * amount + 0.5f;

    *r = clamp_byte_round(rf * 255.0f);
    *g = clamp_byte_round(gf * 255.0f);
    *b = clamp_byte_round(bf * 255.0f);
}

/**
 * sepia(amount)
 * Applies sepia tone. amount=0 is no effect, amount=1 is full sepia.
 * Uses standard sepia transformation matrix.
 */
static void filter_sepia(uint8_t* r, uint8_t* g, uint8_t* b, float amount) {
    amount = clamp_unit(amount);
    if (amount == 0) return;

    float rf = *r, gf = *g, bf = *b;

    // Sepia transformation matrix (from CSS Filter Effects spec)
    float sr = 0.393f * rf + 0.769f * gf + 0.189f * bf;
    float sg = 0.349f * rf + 0.686f * gf + 0.168f * bf;
    float sb = 0.272f * rf + 0.534f * gf + 0.131f * bf;

    // Interpolate between original and sepia
    *r = clamp_byte_round(rf + amount * (sr - rf));
    *g = clamp_byte_round(gf + amount * (sg - gf));
    *b = clamp_byte_round(bf + amount * (sb - bf));
}

static void filter_apply_rgb_matrix(uint8_t* r, uint8_t* g, uint8_t* b,
                                    const float matrix[3][3]) {
    float rf = *r / 255.0f, gf = *g / 255.0f, bf = *b / 255.0f;
    float new_r = matrix[0][0] * rf + matrix[0][1] * gf + matrix[0][2] * bf;
    float new_g = matrix[1][0] * rf + matrix[1][1] * gf + matrix[1][2] * bf;
    float new_b = matrix[2][0] * rf + matrix[2][1] * gf + matrix[2][2] * bf;
    *r = clamp_byte_round(new_r * 255.0f);
    *g = clamp_byte_round(new_g * 255.0f);
    *b = clamp_byte_round(new_b * 255.0f);
}

/**
 * hue-rotate(angle)
 * Rotates hue by the specified angle (in radians).
 * Uses rotation in the RGB color space.
 */
void render_filter_hue_matrix(float angle, float matrix[3][3]) {
    // normalize angle to [0, 2π).
    angle = math_wrap_positive_f(angle, math_tau_f());

    float cos_a = cosf(angle);
    float sin_a = sinf(angle);

    // Hue rotation matrix (from CSS Filter Effects spec)
    // This rotates colors around the gray axis (1,1,1) in RGB space
    float mat[3][3] = {
        {0.213f + 0.787f * cos_a - 0.213f * sin_a,
         0.715f - 0.715f * cos_a - 0.715f * sin_a,
         0.072f - 0.072f * cos_a + 0.928f * sin_a},
        {0.213f - 0.213f * cos_a + 0.143f * sin_a,
         0.715f + 0.285f * cos_a + 0.140f * sin_a,
         0.072f - 0.072f * cos_a - 0.283f * sin_a},
        {0.213f - 0.213f * cos_a - 0.787f * sin_a,
         0.715f - 0.715f * cos_a + 0.715f * sin_a,
         0.072f + 0.928f * cos_a + 0.072f * sin_a}
    };

    memcpy(matrix, mat, sizeof(mat));
}

static void filter_hue_rotate(uint8_t* r, uint8_t* g, uint8_t* b, float angle) {
    float matrix[3][3]; render_filter_hue_matrix(angle, matrix); filter_apply_rgb_matrix(r, g, b, matrix);
}

/**
 * invert(amount)
 * Inverts colors. amount=0 is no effect, amount=1 is full inversion.
 */
static void filter_invert(uint8_t* r, uint8_t* g, uint8_t* b, float amount) {
    amount = clamp_unit(amount);
    if (amount == 0) return;

    // Interpolate between original and inverted
    *r = clamp_byte_round(*r + amount * (255 - 2 * (*r)));
    *g = clamp_byte_round(*g + amount * (255 - 2 * (*g)));
    *b = clamp_byte_round(*b + amount * (255 - 2 * (*b)));
}

/**
 * saturate(amount)
 * Adjusts saturation. amount=1 is no effect, 0 is desaturated, >1 is oversaturated.
 */
void render_filter_saturate_matrix(float amount, float matrix[3][3]) {
    // Saturation matrix (from CSS Filter Effects spec)
    float s = amount;
    float mat[3][3] = {
        {0.213f + 0.787f * s, 0.715f - 0.715f * s, 0.072f - 0.072f * s},
        {0.213f - 0.213f * s, 0.715f + 0.285f * s, 0.072f - 0.072f * s},
        {0.213f - 0.213f * s, 0.715f - 0.715f * s, 0.072f + 0.928f * s}
    };

    memcpy(matrix, mat, sizeof(mat));
}

static void filter_saturate(uint8_t* r, uint8_t* g, uint8_t* b, float amount) {
    if (amount < 0) amount = 0;
    if (amount == 1.0f) return;
    float matrix[3][3]; render_filter_saturate_matrix(amount, matrix); filter_apply_rgb_matrix(r, g, b, matrix);
}

/**
 * opacity(amount)
 * Adjusts opacity. amount=1 is no effect, 0 is transparent.
 */
static void filter_opacity(uint8_t* a, float amount) {
    amount = clamp_unit(amount);
    *a = clamp_byte_round(*a * amount);
}

typedef struct FilterPixelRegion {
    int left;
    int top;
    int right;
    int bottom;
} FilterPixelRegion;

static bool render_filter_pixel_region(ImageSurface* surface, Rect* rect, Bound* clip,
                                       FilterPixelRegion* region) {
    if (!surface || !rect || !clip || !region) return false;
    region->left = std::max(0, (int)fmaxf(rect->x, clip->left));
    region->top = std::max(0, (int)fmaxf(rect->y, clip->top));
    region->right = std::min((int)surface->width,
        (int)fminf(rect->x + rect->width, clip->right));
    region->bottom = std::min((int)surface->height,
        (int)fminf(rect->y + rect->height, clip->bottom));
    return region->left < region->right && region->top < region->bottom;
}

static bool render_filter_apply_native_backend(const RenderBackendCaps* caps,
                                               ScratchArena* sa,
                                               ImageSurface* surface,
                                               FilterProp* filter,
                                               Rect* rect,
                                               Bound* clip) {
#ifndef __APPLE__
    (void)caps; (void)sa; (void)surface; (void)filter; (void)rect; (void)clip;
    return false;
#else
    if (!caps || !caps->gaussian_blur || !sa || !surface || !surface->pixels ||
        !filter || !filter->functions || !rect || !clip) {
        return false;
    }

    FilterFunction* func = filter->functions;
    bool has_blur = false;
    while (func) {
        if (func->type != FILTER_BLUR) {
            return false;
        }
        if (func->params.blur_radius > 0) {
            has_blur = true;
        }
        func = func->next;
    }
    if (!has_blur) {
        return false;
    }

    FilterPixelRegion region;
    if (!render_filter_pixel_region(surface, rect, clip, &region)) {
        return true;
    }
    int left = region.left;
    int top = region.top;
    int right = region.right;
    int bottom = region.bottom;

    uint32_t* pixels = (uint32_t*)surface->pixels;
    int pitch = surface->pitch / (int)sizeof(uint32_t);

    func = filter->functions;
    while (func) {
        float br = func->params.blur_radius;
        if (br > 0) {
            float kernel_b = br * 2.0f;
            int pad = (int)ceilf(br * 2.0f);
            Rect expanded = {(float)(left - pad), (float)(top - pad),
                (float)(right - left + 2 * pad), (float)(bottom - top + 2 * pad)};
            FilterPixelRegion blur_region;
            if (!render_filter_pixel_region(surface, &expanded, clip, &blur_region)) {
                func = func->next;
                continue;
            }
            int blur_x = blur_region.left, blur_y = blur_region.top;
            int blur_w = blur_region.right - blur_x, blur_h = blur_region.bottom - blur_y;

            size_t row_bytes = (size_t)blur_w * sizeof(uint32_t);
            size_t buf_bytes = row_bytes * (size_t)blur_h;
            ScratchScope scope(sa);
            uint32_t* src_px = (uint32_t*)scope.alloc(buf_bytes);
            if (!src_px) {
                return false;
            }
            uint32_t* dst_px = (uint32_t*)scope.alloc(buf_bytes);
            if (!dst_px) {
                return false;
            }

            for (int row = 0; row < blur_h; row++) {
                memcpy((uint8_t*)src_px + (size_t)row * row_bytes,
                       pixels + (blur_y + row) * pitch + blur_x,
                       row_bytes);
            }

            vImage_Buffer src = { src_px, (vImagePixelCount)blur_h,
                                  (vImagePixelCount)blur_w, row_bytes };
            vImage_Buffer dst = { dst_px, (vImagePixelCount)blur_h,
                                  (vImagePixelCount)blur_w, row_bytes };
            uint32_t kernel = (uint32_t)ceilf(kernel_b);
            if (kernel < 1) kernel = 1;
            if ((kernel & 1u) == 0) kernel++;

            vImage_Error error = kvImageNoError;
            for (int pass = 0; pass < 3; pass++) {
                error = vImageBoxConvolve_ARGB8888(&src, &dst, nullptr, 0, 0,
                                                   kernel, kernel, nullptr,
                                                   kvImageEdgeExtend);
                if (error != kvImageNoError) break;
                void* tmp_data = src.data;
                src.data = dst.data;
                dst.data = tmp_data;
            }

            if (error == kvImageNoError) {
                for (int row = 0; row < blur_h; row++) {
                    memcpy(pixels + (blur_y + row) * pitch + blur_x,
                           (uint8_t*)src.data + (size_t)row * row_bytes,
                           row_bytes);
                }
            } else {
                log_debug("[FILTER] vImage blur failed error=%ld; falling back to software",
                          (long)error);
                return false;
            }
        }
        func = func->next;
    }

    return true;
#endif
}

bool render_filter_apply_with_backend(const RenderBackendCaps* caps,
                                      ScratchArena* sa,
                                      ImageSurface* surface,
                                      FilterProp* filter,
                                      Rect* rect,
                                      Bound* clip) {
    if (!filter || !filter->functions) {
        return false;
    }

    if (render_backend_supports_filter_chain(caps, filter) &&
        render_filter_apply_native_backend(caps, sa, surface, filter, rect, clip)) {
        return true;
    }

    apply_css_filters(sa, surface, filter, rect, clip);
    return true;
}

/**
 * Apply all CSS filters to a rendered region
 *
 * Filters are applied in order to the pixel data.
 * The surface is in ABGR8888 format (ThorVG default).
 */
static void apply_css_filter_step(ScratchArena* sa, ImageSurface* surface, FilterProp* filter, Rect* rect, Bound* clip) {
    if (!surface || !surface->pixels || !filter || !filter->functions) {
        return;
    }

    FilterPixelRegion region;
    if (!render_filter_pixel_region(surface, rect, clip, &region)) {
        return;
    }
    int left = region.left;
    int top = region.top;
    int right = region.right;
    int bottom = region.bottom;


    // Process each pixel in the region
    uint32_t* pixels = (uint32_t*)surface->pixels;
    int pitch = surface->pitch / sizeof(uint32_t);  // Pitch in pixels

    // independent channel functions have only 256 inputs; retain their existing
    // rounding at each authored step while avoiding repeated float arithmetic.
    uint8_t channel_table[256];
    FilterFunction* channel_function = filter->functions;
    bool use_channel_table = !channel_function->next &&
        (size_t)(right - left) * (size_t)(bottom - top) >= 256 &&
        (channel_function->type == FILTER_BRIGHTNESS ||
         channel_function->type == FILTER_CONTRAST || channel_function->type == FILTER_INVERT);
    if (use_channel_table) {
        for (unsigned value = 0; value < 256; value++) {
            uint8_t r = value, g = value, b = value;
            if (channel_function->type == FILTER_BRIGHTNESS)
                filter_brightness(&r, &g, &b, channel_function->params.amount);
            else if (channel_function->type == FILTER_CONTRAST)
                filter_contrast(&r, &g, &b, channel_function->params.amount);
            else filter_invert(&r, &g, &b, channel_function->params.amount);
            channel_table[value] = r;
        }
    }

    for (int y = top; y < bottom; y++) {
        for (int x = left; x < right; x++) {
            uint32_t* pixel = &pixels[y * pitch + x];
            uint32_t color = *pixel;

            // Extract ABGR components (ThorVG ABGR8888 format)
            uint8_t a = (color >> 24) & 0xFF;
            uint8_t b = (color >> 16) & 0xFF;
            uint8_t g = (color >> 8) & 0xFF;
            uint8_t r = color & 0xFF;
            if (a == 0 && surface->alpha_mode == IMAGE_ALPHA_PREMULTIPLIED) {
                *pixel = 0;
                continue;
            }
            if (surface->alpha_mode == IMAGE_ALPHA_PREMULTIPLIED && a > 0 && a < 255) {
                r = clamp_byte_round((float)r * 255.0f / a);
                g = clamp_byte_round((float)g * 255.0f / a);
                b = clamp_byte_round((float)b * 255.0f / a);
            }

            // Apply each filter in the chain
            if (use_channel_table) {
                r = channel_table[r]; g = channel_table[g]; b = channel_table[b];
            }
            FilterFunction* func = use_channel_table ? nullptr : filter->functions.get();
            while (func) {
                switch (func->type) {
                    case FILTER_GRAYSCALE:
                        filter_grayscale(&r, &g, &b, func->params.amount);
                        break;

                    case FILTER_BRIGHTNESS:
                        filter_brightness(&r, &g, &b, func->params.amount);
                        break;

                    case FILTER_CONTRAST:
                        filter_contrast(&r, &g, &b, func->params.amount);
                        break;

                    case FILTER_SEPIA:
                        filter_sepia(&r, &g, &b, func->params.amount);
                        break;

                    case FILTER_HUE_ROTATE:
                        filter_hue_rotate(&r, &g, &b, func->params.angle);
                        break;

                    case FILTER_INVERT:
                        filter_invert(&r, &g, &b, func->params.amount);
                        break;

                    case FILTER_SATURATE:
                        filter_saturate(&r, &g, &b, func->params.amount);
                        break;

                    case FILTER_OPACITY:
                        filter_opacity(&a, func->params.amount);
                        break;

                    case FILTER_BLUR:
                        // Blur is handled as a post-processing step below (not per-pixel)
                        break;

                    case FILTER_DROP_SHADOW:
                        // Drop shadow is similar to box-shadow but follows element shape
                        // Would need separate rendering pass
                        if (x == left && y == top) {
                        }
                        break;

                    default:
                        break;
                }
                func = func->next;
            }

            // Premultiply RGB by alpha so downstream source-over composite
            // (premul formula in render.cpp / DL_COMPOSITE_OPACITY) produces
            // correct results. The filter pipeline above operates on
            // straight-alpha values, but our compositors expect premul.
            if (surface->alpha_mode == IMAGE_ALPHA_PREMULTIPLIED && a < 255) {
                r = (uint8_t)((r * a + 127) / 255);
                g = (uint8_t)((g * a + 127) / 255);
                b = (uint8_t)((b * a + 127) / 255);
            }

            // Repack ABGR
            *pixel = render_pixel_pack_abgr(r, g, b, a);
        }
    }


    // Apply blur filter as a post-processing step (operates on entire region, not per-pixel)
    // CSS filter: blur() extends the visual effect beyond the element's bounding box
    // by the blur radius on each side, so we must expand the blur region.
    FilterFunction* blur_func = filter->functions;
    while (blur_func) {
        if (blur_func->type == FILTER_BLUR && blur_func->params.blur_radius > 0) {
            float br = blur_func->params.blur_radius;
            // CSS filter: blur(<length>) — per spec, the length IS the Gaussian
            // standard deviation. box_blur_region targets σ = b/2 (the CSS
            // box-shadow convention shared by that path), so we pass 2*br to
            // produce the correct σ = br for filter:blur().
            float kernel_b = br * 2.0f;
            int pad = (int)ceilf(br * 2.0f);
            Rect expanded = {(float)(left - pad), (float)(top - pad),
                (float)(right - left + 2 * pad), (float)(bottom - top + 2 * pad)};
            FilterPixelRegion blur_region;
            if (render_filter_pixel_region(surface, &expanded, clip, &blur_region)) {
                box_blur_region(sa, surface, blur_region.left, blur_region.top,
                    blur_region.right - blur_region.left, blur_region.bottom - blur_region.top, kernel_b);
            }
        }
        blur_func = blur_func->next;
    }

    // Apply drop-shadow filter as a post-processing step.
    // Algorithm:
    //  1. Extract the element's alpha channel into a shadow buffer, scaled by shadow color alpha.
    //  2. Box-blur the shadow buffer to simulate the blur radius.
    //  3. Composite the blurred shadow onto the main surface at (elem_x + offset_x, elem_y + offset_y)
    //     using destination-over blending (shadow placed behind existing content).
    FilterFunction* ds_func = filter->functions;
    while (ds_func) {
        if (ds_func->type == FILTER_DROP_SHADOW) {
            Color sc = ds_func->params.drop_shadow.color;
            int dx  = (int)ds_func->params.drop_shadow.offset_x;
            int dy  = (int)ds_func->params.drop_shadow.offset_y;
            float blur_r = ds_func->params.drop_shadow.blur_radius;

            if (sc.a == 0) { ds_func = ds_func->next; continue; }

            int ew = right - left;
            int eh = bottom - top;
            if (ew <= 0 || eh <= 0) { ds_func = ds_func->next; continue; }

            // Allocate shadow buffer same size as element region (ABGR pixel format)
            ScratchScope scope(sa);
            uint32_t* shadow_px = scope.array<uint32_t>((size_t)ew * (size_t)eh);
            if (!shadow_px) { ds_func = ds_func->next; continue; }

            // Fill shadow buffer in premultiplied ABGR. The filter result is
            // composited back with render_composite_source_over_premul().
            for (int row = 0; row < eh; row++) {
                for (int col = 0; col < ew; col++) {
                    uint32_t ep = pixels[(top + row) * pitch + (left + col)];
                    uint8_t elem_a = (ep >> 24) & 0xFF;
                    uint8_t sha = (uint8_t)((int)elem_a * sc.a / 255);
                    uint8_t sr = (uint8_t)(((int)sc.r * sha + 127) / 255);
                    uint8_t sg = (uint8_t)(((int)sc.g * sha + 127) / 255);
                    uint8_t sb = (uint8_t)(((int)sc.b * sha + 127) / 255);
                    shadow_px[row * ew + col] = render_pixel_pack_abgr(
                        sr, sg, sb, sha);
                }
            }

            // Blur premultiplied alpha and color together.
            if (blur_r > 0) {
                ImageSurface shadow_surf;
                memset(&shadow_surf, 0, sizeof(shadow_surf));
                shadow_surf.width  = ew;
                shadow_surf.height = eh;
                shadow_surf.pitch  = ew * 4;
                shadow_surf.pixels = shadow_px;
                box_blur_region(sa, &shadow_surf, 0, 0, ew, eh, blur_r);
            }

            // Composite blurred shadow onto main surface at (left+dx, top+dy), destination-over
            for (int row = 0; row < eh; row++) {
                int sy = top + dy + row;
                if (sy < 0 || sy >= surface->height) continue;
                if ((float)sy < clip->top || (float)sy >= clip->bottom) continue;
                for (int col = 0; col < ew; col++) {
                    int sx = left + dx + col;
                    if (sx < 0 || sx >= surface->width) continue;
                    if ((float)sx < clip->left || (float)sx >= clip->right) continue;

                    uint32_t sp = shadow_px[row * ew + col];
                    uint8_t shadow_a = (sp >> 24) & 0xFF;
                    if (shadow_a == 0) continue;

                    uint32_t* dst = &pixels[sy * pitch + sx];
                    *dst = render_pixel_destination_over_premultiplied(*dst, sp);
                    // Keep shadow color premultiplied while preserving the destination alpha.
                    // The shared primitive applies the same channel rounding as other filters.
                    // No per-channel fallback is needed after the source-over conversion.
                    // This also keeps transparent shadow fringes from storing straight color.
                }
            }
        }
        ds_func = ds_func->next;
    }
}

void apply_css_filters(ScratchArena* scratch, ImageSurface* surface, FilterProp* filter, Rect* rect, Bound* clip) {
    // Spatial and color functions must execute in authored order, including URL graphs.
    for (FilterFunction* function = filter ? filter->functions.get() : nullptr; function; function = function->next) {
        if (function->type == FILTER_URL) {
            if (!render_css_svg_filter_apply(scratch, surface, function->svg, rect, clip))
                log_error("CSS_SVG_FILTER: referenced graph could not be executed");
            continue;
        }
        FilterFunction step = *function; step.next = nullptr;
        FilterProp single = {}; single.functions = lam::own(&step); single.functions_borrowed = true;
        apply_css_filter_step(scratch, surface, &single, rect, clip);
    }
}
