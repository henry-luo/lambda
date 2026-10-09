#include "render.hpp"

#include "../lib/math_utils.h"
#include <math.h>
#include <string.h>

// --- CSS Blend Mode Functions (CSS Compositing and Blending Level 1) ---
// All operate on normalized [0,1] channel values.
static inline float render_blend_multiply(float Cb, float Cs) { return Cb * Cs; }
static inline float render_blend_screen(float Cb, float Cs) { return Cb + Cs - Cb * Cs; }
static inline float render_blend_overlay(float Cb, float Cs) {
    return Cb <= 0.5f ? 2.0f * Cb * Cs : 1.0f - 2.0f * (1.0f - Cb) * (1.0f - Cs);
}
static inline float render_blend_darken(float Cb, float Cs) { return Cb < Cs ? Cb : Cs; }
static inline float render_blend_lighten(float Cb, float Cs) { return Cb > Cs ? Cb : Cs; }
static inline float render_blend_color_dodge(float Cb, float Cs) {
    if (Cb <= 0.0f) return 0.0f;
    if (Cs >= 1.0f) return 1.0f;
    float value = Cb / (1.0f - Cs);
    return value < 1.0f ? value : 1.0f;
}
static inline float render_blend_color_burn(float Cb, float Cs) {
    if (Cb >= 1.0f) return 1.0f;
    if (Cs <= 0.0f) return 0.0f;
    float value = 1.0f - (1.0f - Cb) / Cs;
    return value > 0.0f ? value : 0.0f;
}
static inline float render_blend_hard_light(float Cb, float Cs) {
    return Cs <= 0.5f ? 2.0f * Cb * Cs : 1.0f - 2.0f * (1.0f - Cb) * (1.0f - Cs);
}
static inline float render_blend_soft_light(float Cb, float Cs) {
    if (Cs <= 0.5f) return Cb - (1.0f - 2.0f * Cs) * Cb * (1.0f - Cb);
    float D = Cb <= 0.25f ? ((16.0f * Cb - 12.0f) * Cb + 4.0f) * Cb : sqrtf(Cb);
    return Cb + (2.0f * Cs - 1.0f) * (D - Cb);
}
static inline float render_blend_difference(float Cb, float Cs) { return fabsf(Cb - Cs); }
static inline float render_blend_exclusion(float Cb, float Cs) { return Cb + Cs - 2.0f * Cb * Cs; }

static inline uint8_t render_composite_blend_channel(uint8_t Cb_byte, uint8_t Cs_byte, CssEnum mode) {
    float Cb = Cb_byte / 255.0f;
    float Cs = Cs_byte / 255.0f;
    float result;
    switch (mode) {
        case CSS_VALUE_MULTIPLY:    result = render_blend_multiply(Cb, Cs); break;
        case CSS_VALUE_SCREEN:      result = render_blend_screen(Cb, Cs); break;
        case CSS_VALUE_OVERLAY:     result = render_blend_overlay(Cb, Cs); break;
        case CSS_VALUE_DARKEN:      result = render_blend_darken(Cb, Cs); break;
        case CSS_VALUE_LIGHTEN:     result = render_blend_lighten(Cb, Cs); break;
        case CSS_VALUE_COLOR_DODGE: result = render_blend_color_dodge(Cb, Cs); break;
        case CSS_VALUE_COLOR_BURN:  result = render_blend_color_burn(Cb, Cs); break;
        case CSS_VALUE_HARD_LIGHT:  result = render_blend_hard_light(Cb, Cs); break;
        case CSS_VALUE_SOFT_LIGHT:  result = render_blend_soft_light(Cb, Cs); break;
        case CSS_VALUE_DIFFERENCE:  result = render_blend_difference(Cb, Cs); break;
        case CSS_VALUE_EXCLUSION:   result = render_blend_exclusion(Cb, Cs); break;
        default:                    result = Cs; break;
    }
    int value = (int)(result * 255.0f + 0.5f);
    return clamp_byte(value);
}

static float render_blend_luminosity(const float color[3]) {
    return .3f * color[0] + .59f * color[1] + .11f * color[2];
}

static float render_blend_saturation(const float color[3]) {
    return fmaxf(color[0], fmaxf(color[1], color[2])) - fminf(color[0], fminf(color[1], color[2]));
}

static void render_blend_set_luminosity(float color[3], float target) {
    float shift = target - render_blend_luminosity(color);
    for (unsigned channel = 0; channel < 3; channel++) color[channel] += shift;
    float low = fminf(color[0], fminf(color[1], color[2])), high = fmaxf(color[0], fmaxf(color[1], color[2]));
    // clip all channels around luminosity so gamut clipping preserves the selected component.
    for (unsigned channel = 0; channel < 3; channel++) {
        if (low < 0.0f) color[channel] = target + (color[channel] - target) * target / (target - low);
        if (high > 1.0f) color[channel] = target + (color[channel] - target) * (1.0f - target) / (high - target);
    }
}

static void render_blend_set_saturation(float color[3], float target) {
    unsigned order[3] = {0, 1, 2};
    for (unsigned index = 1; index < 3; index++) for (unsigned prior = index; prior && color[order[prior - 1]] > color[order[prior]]; prior--) {
        unsigned swap = order[prior]; order[prior] = order[prior - 1]; order[prior - 1] = swap;
    }
    float range = color[order[2]] - color[order[0]];
    color[order[1]] = range > 0.0f ? (color[order[1]] - color[order[0]]) * target / range : 0.0f;
    color[order[2]] = range > 0.0f ? target : 0.0f; color[order[0]] = 0.0f;
}

void render_composite_blend_rgb(uint32_t backdrop, uint32_t source, CssEnum mode, float result[3]) {
    float background[3], foreground[3];
    for (unsigned channel = 0; channel < 3; channel++) {
        background[channel] = (float)((backdrop >> (channel * 8)) & 255u) / 255.0f;
        foreground[channel] = (float)((source >> (channel * 8)) & 255u) / 255.0f;
        result[channel] = (mode == CSS_VALUE_SATURATION_BLEND || mode == CSS_VALUE_LUMINOSITY_BLEND) ? background[channel] : foreground[channel];
    }
    switch (mode) {
    case CSS_VALUE_HUE_BLEND: render_blend_set_saturation(result, render_blend_saturation(background)); break;
    case CSS_VALUE_SATURATION_BLEND: render_blend_set_saturation(result, render_blend_saturation(foreground)); break;
    case CSS_VALUE_COLOR_BLEND: case CSS_VALUE_LUMINOSITY_BLEND: break;
    default:
        for (unsigned channel = 0; channel < 3; channel++) result[channel] = (float)render_composite_blend_channel(
            (uint8_t)(backdrop >> (channel * 8)), (uint8_t)(source >> (channel * 8)), mode) / 255.0f;
        return;
    }
    render_blend_set_luminosity(result, render_blend_luminosity(mode == CSS_VALUE_LUMINOSITY_BLEND ? foreground : background));
}

uint32_t render_composite_blend_pixel(uint32_t backdrop, uint32_t source, CssEnum blend_mode) {
    uint8_t sa = (source >> 24) & 0xFF;
    if (sa == 0) return backdrop;
    uint8_t ba = (backdrop >> 24) & 0xFF;
    if (ba == 0) return source;

    uint8_t sr = source & 0xFF, sg = (source >> 8) & 0xFF, sb = (source >> 16) & 0xFF;
    uint8_t br = backdrop & 0xFF, bg = (backdrop >> 8) & 0xFF, bb = (backdrop >> 16) & 0xFF;

    float mixed[3]; render_composite_blend_rgb(backdrop, source, blend_mode, mixed);
    if (sa == 255 && ba == 255) {
        uint8_t rr = clamp_byte_round(mixed[0] * 255.0f);
        uint8_t rg = clamp_byte_round(mixed[1] * 255.0f);
        uint8_t rb = clamp_byte_round(mixed[2] * 255.0f);
        return render_pixel_pack_abgr(rr, rg, rb, 255u);
    }

    float fa = ba / 255.0f;
    float fsa = sa / 255.0f;
    float ra = fa + fsa - fa * fsa;
    if (ra < 0.001f) return 0;

    float p = (1.0f - fa) * fsa;
    float q = (1.0f - fsa) * fa;
    float t = fa * fsa;
    auto blendch = [&](uint8_t Cb_b, uint8_t Cs_b, unsigned channel) -> uint8_t {
        float Bb = mixed[channel];
        float Co = (p * (Cs_b / 255.0f) + q * (Cb_b / 255.0f) + t * Bb) / ra;
        int value = (int)(Co * 255.0f + 0.5f);
        return clamp_byte(value);
    };
    uint8_t rr = blendch(br, sr, 0);
    uint8_t rg = blendch(bg, sg, 1);
    uint8_t rb = blendch(bb, sb, 2);
    uint8_t new_a = clamp_byte_round(ra * 255.0f);
    return render_pixel_pack_abgr(rr, rg, rb, new_a);
}

static float render_radial_mask_alpha(const RadialMaskPaint* mask, float x, float y) {
    if (!mask || !mask->gradient) return 1.0f;
    if (!mask->invertible || !rdt_matrix_project_point(&mask->inverse, x, y, &x, &y)) return 0.0f;
    Rect rect = mask->rect;
    if (x < rect.x || y < rect.y || x >= rect.x + rect.width || y >= rect.y + rect.height) return 0.0f;
    RadialGradient* gradient = mask->gradient;
    if (gradient->stop_count < 2 || !gradient->stops) return 0.0f;
    RadiantRadialGeometry geometry = radiant_radial_gradient_geometry(gradient, rect);
    if (geometry.rx <= 0.0f || geometry.ry <= 0.0f) return gradient->stops[gradient->stop_count - 1].color.a / 255.0f;
    float distance = hypotf((x - geometry.cx) / geometry.rx, (y - geometry.cy) / geometry.ry);
    auto position = [&](int index) {
        const GradientStop& stop = gradient->stops[index];
        return stop.position_is_px ? stop.position / geometry.rx : stop.position;
    };
    float first = position(0), last = position(gradient->stop_count - 1);
    if (gradient->is_repeating && last > first) distance = first + (distance - first - floorf((distance - first) / (last - first)) * (last - first));
    float previous = first;
    float alpha = gradient->stops[0].color.a / 255.0f;
    if (distance < first) return alpha;
    for (int index = 1; index < gradient->stop_count; index++) {
        float next = fmaxf(previous, position(index));
        float target = gradient->stops[index].color.a / 255.0f;
        if (distance < next) return alpha + (target - alpha) * (distance - previous) / (next - previous);
        previous = next; alpha = target;
    }
    return alpha;
}

void render_composite_apply_region(ImageSurface* surface, const uint32_t* backdrop,
                                   int x0, int y0, int width, int height,
                                   RenderCompositeRegionMode mode,
                                   CssEnum blend_mode, uint8_t opacity,
                                   const ClipShape* exclude_shape,
                                   const ClipShape* include_shape,
                                   const RadialMaskPaint* mask, float offset_x, float offset_y) {
    if (!surface || !surface->pixels || !backdrop || width <= 0 || height <= 0) return;
    uint32_t* pixels = (uint32_t*)surface->pixels;
    int pitch = surface->pitch / 4;
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            if (exclude_shape || include_shape) {
                float px = (float)(x0 + col) + 0.5f;
                float py = (float)(y0 + row) + 0.5f;
                if ((exclude_shape && clip_point_in_shape(
                        const_cast<ClipShape*>(exclude_shape), px, py)) ||
                    (include_shape && !clip_point_in_shape(
                        const_cast<ClipShape*>(include_shape), px, py))) continue;
            }
            uint32_t* pixel = &pixels[(y0 + row) * pitch + (x0 + col)];
            uint32_t source = backdrop[row * width + col];
            uint8_t coverage = mask && mask->gradient ? clamp_byte_round(opacity * render_radial_mask_alpha(mask,
                x0 + col + offset_x + 0.5f, y0 + row + offset_y + 0.5f)) : opacity;
            if (mode == RENDER_COMPOSITE_REGION_BLEND) {
                // blend functions use straight channels; native opacity groups retain premultiplied pixels.
                bool premultiplied = surface->alpha_mode == IMAGE_ALPHA_PREMULTIPLIED;
                uint32_t result = render_composite_blend_pixel(
                    premultiplied ? render_pixel_unpremultiply_abgr(source) : source,
                    premultiplied ? render_pixel_unpremultiply_abgr(*pixel) : *pixel, blend_mode);
                *pixel = premultiplied ? render_pixel_premultiply_abgr(result) : result;
            } else if (mode == RENDER_COMPOSITE_REGION_PREMULTIPLIED) {
                // full coverage already has the required premultiplied representation.
                *pixel = render_pixel_source_over_premultiplied_pair(source,
                    coverage == 255 ? *pixel : render_pixel_scale_premultiplied(*pixel, coverage));
            } else if (mode == RENDER_COMPOSITE_REGION_PREMULTIPLIED_FULL) {
                *pixel = render_pixel_source_over_premultiplied(*pixel, source);
            } else {
                *pixel = render_pixel_source_over_straight(
                    source, *pixel, coverage);
            }
        }
    }
}

void render_composite_blend_surface(ImageSurface* surface, const uint32_t* backdrop,
                                    int x0, int y0, int width, int height,
                                    CssEnum blend_mode) {
    render_composite_apply_region(surface, backdrop, x0, y0, width, height,
                                  RENDER_COMPOSITE_REGION_BLEND, blend_mode, 255);
}

void render_composite_source_over_premul(ImageSurface* surface, const uint32_t* backdrop,
                                         int x0, int y0, int width, int height, uint8_t opacity) {
    render_composite_apply_region(surface, backdrop, x0, y0, width, height,
                                  RENDER_COMPOSITE_REGION_PREMULTIPLIED,
                                  CSS_VALUE_NORMAL, opacity);
}

void render_composite_opacity(ImageSurface* surface, const uint32_t* backdrop,
                              int x0, int y0, int width, int height,
                              float opacity) {
    int opacity_i = (int)(opacity * 255.0f + 0.5f);
    render_composite_apply_region(surface, backdrop, x0, y0, width, height,
                                  RENDER_COMPOSITE_REGION_OPACITY, CSS_VALUE_NORMAL,
                                  clamp_byte(opacity_i));
}
