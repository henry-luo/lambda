#ifdef __APPLE__
#define Rect MacOSRect
#include <Accelerate/Accelerate.h>
#undef Rect
#endif

#include "render_filter.hpp"
#include "render_background.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/math_utils.h"
#include <math.h>
#include <algorithm>
#include <string.h>

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
    *r = clamp_byte((int)(*r + amount * (gray - *r) + 0.5f));
    *g = clamp_byte((int)(*g + amount * (gray - *g) + 0.5f));
    *b = clamp_byte((int)(*b + amount * (gray - *b) + 0.5f));
}

/**
 * brightness(amount)
 * Adjusts brightness. amount=1 is no effect, <1 is darker, >1 is brighter.
 * Linear multiplication of RGB values.
 */
static void filter_brightness(uint8_t* r, uint8_t* g, uint8_t* b, float amount) {
    if (amount < 0) amount = 0;  // Clamp negative to 0

    *r = clamp_byte((int)(*r * amount + 0.5f));
    *g = clamp_byte((int)(*g * amount + 0.5f));
    *b = clamp_byte((int)(*b * amount + 0.5f));
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

    *r = clamp_byte((int)(rf * 255.0f + 0.5f));
    *g = clamp_byte((int)(gf * 255.0f + 0.5f));
    *b = clamp_byte((int)(bf * 255.0f + 0.5f));
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
    *r = clamp_byte((int)(rf + amount * (sr - rf) + 0.5f));
    *g = clamp_byte((int)(gf + amount * (sg - gf) + 0.5f));
    *b = clamp_byte((int)(bf + amount * (sb - bf) + 0.5f));
}

/**
 * hue-rotate(angle)
 * Rotates hue by the specified angle (in radians).
 * Uses rotation in the RGB color space.
 */
static void filter_hue_rotate(uint8_t* r, uint8_t* g, uint8_t* b, float angle) {
    // Normalize angle to [0, 2π)
    while (angle < 0) angle += 2.0f * M_PI;
    while (angle >= 2.0f * M_PI) angle -= 2.0f * M_PI;

    float cos_a = cosf(angle);
    float sin_a = sinf(angle);

    float rf = *r / 255.0f, gf = *g / 255.0f, bf = *b / 255.0f;

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

    float new_r = mat[0][0] * rf + mat[0][1] * gf + mat[0][2] * bf;
    float new_g = mat[1][0] * rf + mat[1][1] * gf + mat[1][2] * bf;
    float new_b = mat[2][0] * rf + mat[2][1] * gf + mat[2][2] * bf;

    *r = clamp_byte((int)(new_r * 255.0f + 0.5f));
    *g = clamp_byte((int)(new_g * 255.0f + 0.5f));
    *b = clamp_byte((int)(new_b * 255.0f + 0.5f));
}

/**
 * invert(amount)
 * Inverts colors. amount=0 is no effect, amount=1 is full inversion.
 */
static void filter_invert(uint8_t* r, uint8_t* g, uint8_t* b, float amount) {
    amount = clamp_unit(amount);
    if (amount == 0) return;

    // Interpolate between original and inverted
    *r = clamp_byte((int)(*r + amount * (255 - 2 * (*r)) + 0.5f));
    *g = clamp_byte((int)(*g + amount * (255 - 2 * (*g)) + 0.5f));
    *b = clamp_byte((int)(*b + amount * (255 - 2 * (*b)) + 0.5f));
}

/**
 * saturate(amount)
 * Adjusts saturation. amount=1 is no effect, 0 is desaturated, >1 is oversaturated.
 */
static void filter_saturate(uint8_t* r, uint8_t* g, uint8_t* b, float amount) {
    if (amount < 0) amount = 0;
    if (amount == 1) return;

    float rf = *r / 255.0f, gf = *g / 255.0f, bf = *b / 255.0f;

    // Saturation matrix (from CSS Filter Effects spec)
    float s = amount;
    float mat[3][3] = {
        {0.213f + 0.787f * s, 0.715f - 0.715f * s, 0.072f - 0.072f * s},
        {0.213f - 0.213f * s, 0.715f + 0.285f * s, 0.072f - 0.072f * s},
        {0.213f - 0.213f * s, 0.715f - 0.715f * s, 0.072f + 0.928f * s}
    };

    float new_r = mat[0][0] * rf + mat[0][1] * gf + mat[0][2] * bf;
    float new_g = mat[1][0] * rf + mat[1][1] * gf + mat[1][2] * bf;
    float new_b = mat[2][0] * rf + mat[2][1] * gf + mat[2][2] * bf;

    *r = clamp_byte((int)(new_r * 255.0f + 0.5f));
    *g = clamp_byte((int)(new_g * 255.0f + 0.5f));
    *b = clamp_byte((int)(new_b * 255.0f + 0.5f));
}

/**
 * opacity(amount)
 * Adjusts opacity. amount=1 is no effect, 0 is transparent.
 */
static void filter_opacity(uint8_t* a, float amount) {
    amount = clamp_unit(amount);
    *a = clamp_byte((int)(*a * amount + 0.5f));
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

    int left = (int)fmaxf(rect->x, clip->left);
    int top = (int)fmaxf(rect->y, clip->top);
    int right = (int)fminf(rect->x + rect->width, clip->right);
    int bottom = (int)fminf(rect->y + rect->height, clip->bottom);

    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > surface->width) right = surface->width;
    if (bottom > surface->height) bottom = surface->height;

    if (left >= right || top >= bottom) {
        return true;
    }

    uint32_t* pixels = (uint32_t*)surface->pixels;
    int pitch = surface->pitch / (int)sizeof(uint32_t);

    func = filter->functions;
    while (func) {
        float br = func->params.blur_radius;
        if (br > 0) {
            float kernel_b = br * 2.0f;
            int pad = (int)ceilf(br * 2.0f);
            int blur_x = left - pad;
            int blur_y = top - pad;
            int blur_r = right + pad;
            int blur_b = bottom + pad;
            if (blur_x < 0) blur_x = 0;
            if (blur_y < 0) blur_y = 0;
            if (blur_r > surface->width) blur_r = surface->width;
            if (blur_b > surface->height) blur_b = surface->height;

            int blur_w = blur_r - blur_x;
            int blur_h = blur_b - blur_y;
            if (blur_w <= 0 || blur_h <= 0) {
                func = func->next;
                continue;
            }

            size_t row_bytes = (size_t)blur_w * sizeof(uint32_t);
            size_t buf_bytes = row_bytes * (size_t)blur_h;
            uint32_t* src_px = (uint32_t*)scratch_alloc(sa, buf_bytes);
            if (!src_px) {
                return false;
            }
            uint32_t* dst_px = (uint32_t*)scratch_alloc(sa, buf_bytes);
            if (!dst_px) {
                scratch_free(sa, src_px);
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
                log_debug("[FILTER] Applied blur(%.1fpx) via Accelerate/vImage to region (%d,%d,%d,%d)",
                          br, blur_x, blur_y, blur_w, blur_h);
            } else {
                log_debug("[FILTER] vImage blur failed error=%ld; falling back to software",
                          (long)error);
                scratch_free(sa, dst_px);
                scratch_free(sa, src_px);
                return false;
            }

            scratch_free(sa, dst_px);
            scratch_free(sa, src_px);
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
void apply_css_filters(ScratchArena* sa, ImageSurface* surface, FilterProp* filter, Rect* rect, Bound* clip) {
    if (!surface || !surface->pixels || !filter || !filter->functions) {
        return;
    }

    // Calculate the region to process (intersection of rect and clip)
    int left = (int)fmaxf(rect->x, clip->left);
    int top = (int)fmaxf(rect->y, clip->top);
    int right = (int)fminf(rect->x + rect->width, clip->right);
    int bottom = (int)fminf(rect->y + rect->height, clip->bottom);

    // Clamp to surface bounds
    left = std::max(0, left);
    top = std::max(0, top);
    right = std::min((int)surface->width, right);
    bottom = std::min((int)surface->height, bottom);

    if (left >= right || top >= bottom) {
        log_debug("[FILTER] Region outside clip bounds, skipping");
        return;
    }

    log_debug("[FILTER] Applying filters to region (%d,%d)-(%d,%d)", left, top, right, bottom);

    // Process each pixel in the region
    uint32_t* pixels = (uint32_t*)surface->pixels;
    int pitch = surface->pitch / sizeof(uint32_t);  // Pitch in pixels

    for (int y = top; y < bottom; y++) {
        for (int x = left; x < right; x++) {
            uint32_t* pixel = &pixels[y * pitch + x];
            uint32_t color = *pixel;

            // Extract ABGR components (ThorVG ABGR8888 format)
            uint8_t a = (color >> 24) & 0xFF;
            uint8_t b = (color >> 16) & 0xFF;
            uint8_t g = (color >> 8) & 0xFF;
            uint8_t r = color & 0xFF;

            // Apply each filter in the chain
            FilterFunction* func = filter->functions;
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
                            log_debug("[FILTER] drop-shadow not supported yet");
                        }
                        break;

                    case FILTER_URL:
                        // SVG filter reference - not supported
                        if (x == left && y == top) {
                            log_debug("[FILTER] url() SVG filter not supported");
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
            if (a < 255) {
                r = (uint8_t)((r * a + 127) / 255);
                g = (uint8_t)((g * a + 127) / 255);
                b = (uint8_t)((b * a + 127) / 255);
            }

            // Repack ABGR
            *pixel = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
        }
    }

    log_debug("[FILTER] Applied filters to %d pixels", (right - left) * (bottom - top));

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
            int blur_x = std::max(0, left - pad);
            int blur_y = std::max(0, top - pad);
            int blur_r = std::min((int)surface->width, right + pad);
            int blur_b = std::min((int)surface->height, bottom + pad);
            int blur_w = blur_r - blur_x;
            int blur_h = blur_b - blur_y;
            if (blur_w > 0 && blur_h > 0) {
                box_blur_region(sa, surface, blur_x, blur_y, blur_w, blur_h, kernel_b);
                log_debug("[FILTER] Applied blur(%.1fpx) via software box blur to region (%d,%d,%d,%d)",
                          br, blur_x, blur_y, blur_w, blur_h);
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
            uint32_t* shadow_px = (uint32_t*)scratch_alloc(sa, (size_t)ew * eh * sizeof(uint32_t));
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
                    shadow_px[row * ew + col] = ((uint32_t)sha  << 24)
                                              | ((uint32_t)sb << 16)
                                              | ((uint32_t)sg <<  8)
                                              |  (uint32_t)sr;
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
                    uint32_t ep = *dst;
                    uint8_t ea = (ep >> 24) & 0xFF;
                    if (ea == 255) continue;  // fully opaque existing pixel hides shadow

                    uint32_t inv_ea = 255 - ea;
                    uint32_t new_a = ea + (shadow_a * inv_ea + 127) / 255;
                    uint32_t new_r = (ep & 0xFF) + ((sp & 0xFF) * inv_ea + 127) / 255;
                    uint32_t new_g = ((ep >> 8) & 0xFF) + (((sp >> 8) & 0xFF) * inv_ea + 127) / 255;
                    uint32_t new_b = ((ep >> 16) & 0xFF) + (((sp >> 16) & 0xFF) * inv_ea + 127) / 255;

                    *dst = (LMB_MIN(new_a, 255u) << 24) |
                           (LMB_MIN(new_b, 255u) << 16) |
                           (LMB_MIN(new_g, 255u) << 8) |
                           LMB_MIN(new_r, 255u);
                }
            }

            scratch_free(sa, shadow_px);
            log_debug("[FILTER] Applied drop-shadow(%d,%d,%.1fpx rgba(%d,%d,%d,%d)) to region (%d,%d,%d,%d)",
                      dx, dy, blur_r, sc.r, sc.g, sc.b, sc.a, left, top, ew, eh);
        }
        ds_func = ds_func->next;
    }
}
