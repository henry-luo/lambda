#include "render_filter.hpp"
#include "render_background.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include <math.h>
#include <algorithm>

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

// Helper: Clamp value to 0-255 range
static inline uint8_t clamp_byte(float v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)(v + 0.5f);  // Round to nearest
}

// Helper: Clamp value to 0-1 range
static inline float clamp_01(float v) {
    if (v < 0) return 0;
    if (v > 1) return 1;
    return v;
}

/**
 * grayscale(amount)
 * Converts to grayscale. amount=0 is no effect, amount=1 is full grayscale.
 * Uses luminance formula: 0.2126*R + 0.7152*G + 0.0722*B
 */
void filter_grayscale(uint8_t* r, uint8_t* g, uint8_t* b, float amount) {
    amount = clamp_01(amount);
    if (amount == 0) return;

    float gray = 0.2126f * (*r) + 0.7152f * (*g) + 0.0722f * (*b);

    // Interpolate between original and grayscale
    *r = clamp_byte(*r + amount * (gray - *r));
    *g = clamp_byte(*g + amount * (gray - *g));
    *b = clamp_byte(*b + amount * (gray - *b));
}

/**
 * brightness(amount)
 * Adjusts brightness. amount=1 is no effect, <1 is darker, >1 is brighter.
 * Linear multiplication of RGB values.
 */
void filter_brightness(uint8_t* r, uint8_t* g, uint8_t* b, float amount) {
    if (amount < 0) amount = 0;  // Clamp negative to 0

    *r = clamp_byte(*r * amount);
    *g = clamp_byte(*g * amount);
    *b = clamp_byte(*b * amount);
}

/**
 * contrast(amount)
 * Adjusts contrast. amount=1 is no effect, <1 is less contrast, >1 is more contrast.
 * Formula: (value - 0.5) * amount + 0.5
 */
void filter_contrast(uint8_t* r, uint8_t* g, uint8_t* b, float amount) {
    if (amount < 0) amount = 0;

    float rf = (*r / 255.0f - 0.5f) * amount + 0.5f;
    float gf = (*g / 255.0f - 0.5f) * amount + 0.5f;
    float bf = (*b / 255.0f - 0.5f) * amount + 0.5f;

    *r = clamp_byte(rf * 255.0f);
    *g = clamp_byte(gf * 255.0f);
    *b = clamp_byte(bf * 255.0f);
}

/**
 * sepia(amount)
 * Applies sepia tone. amount=0 is no effect, amount=1 is full sepia.
 * Uses standard sepia transformation matrix.
 */
void filter_sepia(uint8_t* r, uint8_t* g, uint8_t* b, float amount) {
    amount = clamp_01(amount);
    if (amount == 0) return;

    float rf = *r, gf = *g, bf = *b;

    // Sepia transformation matrix (from CSS Filter Effects spec)
    float sr = 0.393f * rf + 0.769f * gf + 0.189f * bf;
    float sg = 0.349f * rf + 0.686f * gf + 0.168f * bf;
    float sb = 0.272f * rf + 0.534f * gf + 0.131f * bf;

    // Interpolate between original and sepia
    *r = clamp_byte(rf + amount * (sr - rf));
    *g = clamp_byte(gf + amount * (sg - gf));
    *b = clamp_byte(bf + amount * (sb - bf));
}

/**
 * hue-rotate(angle)
 * Rotates hue by the specified angle (in radians).
 * Uses rotation in the RGB color space.
 */
void filter_hue_rotate(uint8_t* r, uint8_t* g, uint8_t* b, float angle) {
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

    *r = clamp_byte(new_r * 255.0f);
    *g = clamp_byte(new_g * 255.0f);
    *b = clamp_byte(new_b * 255.0f);
}

/**
 * invert(amount)
 * Inverts colors. amount=0 is no effect, amount=1 is full inversion.
 */
void filter_invert(uint8_t* r, uint8_t* g, uint8_t* b, float amount) {
    amount = clamp_01(amount);
    if (amount == 0) return;

    // Interpolate between original and inverted
    *r = clamp_byte(*r + amount * (255 - 2 * (*r)));
    *g = clamp_byte(*g + amount * (255 - 2 * (*g)));
    *b = clamp_byte(*b + amount * (255 - 2 * (*b)));
}

/**
 * saturate(amount)
 * Adjusts saturation. amount=1 is no effect, 0 is desaturated, >1 is oversaturated.
 */
void filter_saturate(uint8_t* r, uint8_t* g, uint8_t* b, float amount) {
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

    *r = clamp_byte(new_r * 255.0f);
    *g = clamp_byte(new_g * 255.0f);
    *b = clamp_byte(new_b * 255.0f);
}

/**
 * opacity(amount)
 * Adjusts opacity. amount=1 is no effect, 0 is transparent.
 */
void filter_opacity(uint8_t* a, float amount) {
    amount = clamp_01(amount);
    *a = clamp_byte(*a * amount);
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
            int pad = (int)ceilf(br);
            int blur_x = std::max(0, left - pad);
            int blur_y = std::max(0, top - pad);
            int blur_r = std::min((int)surface->width, right + pad);
            int blur_b = std::min((int)surface->height, bottom + pad);
            int blur_w = blur_r - blur_x;
            int blur_h = blur_b - blur_y;
            if (blur_w > 0 && blur_h > 0) {
                box_blur_region(sa, surface, blur_x, blur_y, blur_w, blur_h, br);
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

            // Fill shadow buffer: shadow color with alpha = elem_alpha * color.a / 255
            // ABGR layout: A=bits24-31, B=bits16-23, G=bits8-15, R=bits0-7
            for (int row = 0; row < eh; row++) {
                for (int col = 0; col < ew; col++) {
                    uint32_t ep = pixels[(top + row) * pitch + (left + col)];
                    uint8_t elem_a = (ep >> 24) & 0xFF;
                    uint8_t sha = (uint8_t)((int)elem_a * sc.a / 255);
                    shadow_px[row * ew + col] = ((uint32_t)sha  << 24)
                                              | ((uint32_t)sc.b << 16)
                                              | ((uint32_t)sc.g <<  8)
                                              |  (uint32_t)sc.r;
                }
            }

            // Blur the shadow alpha (and RGB, though RGB is constant across the shadow)
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

                    // Porter-Duff destination-over: src=shadow (behind), dst=existing (in front)
                    // result.a = dst.a + src.a * (1 - dst.a)
                    // result.rgb = (dst.rgb * dst.a + src.rgb * src.a * (1 - dst.a)) / result.a
                    float fa  = ea  / 255.0f;
                    float fsa = shadow_a  / 255.0f;
                    float res_af = fa + fsa * (1.0f - fa);
                    uint8_t new_a = (uint8_t)(res_af * 255.0f + 0.5f);
                    if (new_a == 0) continue;

                    float sc_contrib = fsa * (1.0f - fa);
                    uint8_t er = ep & 0xFF,          sr = sp & 0xFF;
                    uint8_t eg = (ep >> 8) & 0xFF,   sg = (sp >> 8) & 0xFF;
                    uint8_t eb = (ep >> 16) & 0xFF,  sb = (sp >> 16) & 0xFF;

                    uint8_t new_r = (uint8_t)(((float)er * fa + (float)sr * sc_contrib) / res_af + 0.5f);
                    uint8_t new_g = (uint8_t)(((float)eg * fa + (float)sg * sc_contrib) / res_af + 0.5f);
                    uint8_t new_b = (uint8_t)(((float)eb * fa + (float)sb * sc_contrib) / res_af + 0.5f);

                    *dst = ((uint32_t)new_a << 24) | ((uint32_t)new_b << 16) | ((uint32_t)new_g << 8) | new_r;
                }
            }

            scratch_free(sa, shadow_px);
            log_debug("[FILTER] Applied drop-shadow(%d,%d,%.1fpx rgba(%d,%d,%d,%d)) to region (%d,%d,%d,%d)",
                      dx, dy, blur_r, sc.r, sc.g, sc.b, sc.a, left, top, ew, eh);
        }
        ds_func = ds_func->next;
    }
}
