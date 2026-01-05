#pragma once
#include "render.hpp"
#include "view.hpp"

/**
 * CSS Filter Rendering
 *
 * Implements CSS filter effects that can be applied to elements.
 * Color manipulation filters (grayscale, brightness, contrast, etc.) are applied
 * to the rendered pixel data after the element and its children are rendered.
 *
 * Note: blur() filter requires ThorVG C++ API (SceneEffect::GaussianBlur) which
 * is not available in the C API. Blur is logged but not applied.
 */

/**
 * Apply CSS filter effects to a rendered region
 *
 * @param surface The image surface containing rendered pixels
 * @param filter The filter property chain to apply
 * @param rect The rectangular region to apply filters to
 * @param clip The clipping bounds
 */
void apply_css_filters(ImageSurface* surface, FilterProp* filter, Rect* rect, Bound* clip);

/**
 * Individual filter effect functions
 * These operate on a single pixel's RGBA values
 */

// grayscale(amount) - 0=no effect, 1=full grayscale
void filter_grayscale(uint8_t* r, uint8_t* g, uint8_t* b, float amount);

// brightness(amount) - 1=no effect, <1=darker, >1=brighter
void filter_brightness(uint8_t* r, uint8_t* g, uint8_t* b, float amount);

// contrast(amount) - 1=no effect, <1=less contrast, >1=more contrast
void filter_contrast(uint8_t* r, uint8_t* g, uint8_t* b, float amount);

// sepia(amount) - 0=no effect, 1=full sepia
void filter_sepia(uint8_t* r, uint8_t* g, uint8_t* b, float amount);

// hue-rotate(angle) - angle in radians
void filter_hue_rotate(uint8_t* r, uint8_t* g, uint8_t* b, float angle);

// invert(amount) - 0=no effect, 1=full inversion
void filter_invert(uint8_t* r, uint8_t* g, uint8_t* b, float amount);

// saturate(amount) - 1=no effect, 0=desaturated, >1=oversaturated
void filter_saturate(uint8_t* r, uint8_t* g, uint8_t* b, float amount);

// opacity(amount) - 1=no effect, 0=transparent
void filter_opacity(uint8_t* a, float amount);
