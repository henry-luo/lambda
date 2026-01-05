#include "render_filter.hpp"
#include "../lib/log.h"
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
void apply_css_filters(ImageSurface* surface, FilterProp* filter, Rect* rect, Bound* clip) {
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
                        // Blur requires ThorVG C++ API (SceneEffect::GaussianBlur)
                        // Log and skip
                        if (x == left && y == top) {
                            log_debug("[FILTER] blur(%.1fpx) not supported (requires ThorVG C++ API)",
                                      func->params.blur_radius);
                        }
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
}
