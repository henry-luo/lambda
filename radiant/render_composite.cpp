#include "render_composite.hpp"

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
    return (uint8_t)(value < 0 ? 0 : (value > 255 ? 255 : value));
}

uint32_t render_composite_blend_pixel(uint32_t backdrop, uint32_t source, CssEnum blend_mode) {
    uint8_t sa = (source >> 24) & 0xFF;
    if (sa == 0) return backdrop;
    uint8_t ba = (backdrop >> 24) & 0xFF;
    if (ba == 0) return source;

    uint8_t sr = source & 0xFF, sg = (source >> 8) & 0xFF, sb = (source >> 16) & 0xFF;
    uint8_t br = backdrop & 0xFF, bg = (backdrop >> 8) & 0xFF, bb = (backdrop >> 16) & 0xFF;

    if (sa == 255 && ba == 255) {
        uint8_t rr = render_composite_blend_channel(br, sr, blend_mode);
        uint8_t rg = render_composite_blend_channel(bg, sg, blend_mode);
        uint8_t rb = render_composite_blend_channel(bb, sb, blend_mode);
        return (255u << 24) | ((uint32_t)rb << 16) | ((uint32_t)rg << 8) | rr;
    }

    float fa = ba / 255.0f;
    float fsa = sa / 255.0f;
    float ra = fa + fsa - fa * fsa;
    if (ra < 0.001f) return 0;

    float p = (1.0f - fa) * fsa;
    float q = (1.0f - fsa) * fa;
    float t = fa * fsa;
    auto blendch = [&](uint8_t Cb_b, uint8_t Cs_b) -> uint8_t {
        float Bb = render_composite_blend_channel(Cb_b, Cs_b, blend_mode) / 255.0f;
        float Co = (p * (Cs_b / 255.0f) + q * (Cb_b / 255.0f) + t * Bb) / ra;
        int value = (int)(Co * 255.0f + 0.5f);
        return (uint8_t)(value < 0 ? 0 : (value > 255 ? 255 : value));
    };
    uint8_t rr = blendch(br, sr);
    uint8_t rg = blendch(bg, sg);
    uint8_t rb = blendch(bb, sb);
    uint8_t new_a = (uint8_t)(ra * 255.0f + 0.5f);
    return ((uint32_t)new_a << 24) | ((uint32_t)rb << 16) | ((uint32_t)rg << 8) | rr;
}

bool render_composite_copy_backdrop(ImageSurface* surface, uint32_t* backdrop,
                                    int x0, int y0, int width, int height,
                                    bool clear_surface) {
    if (!surface || !surface->pixels || !backdrop || width <= 0 || height <= 0) {
        return false;
    }
    uint32_t* pixels = (uint32_t*)surface->pixels;
    int pitch = surface->pitch / 4;
    for (int row = 0; row < height; row++) {
        memcpy(backdrop + row * width,
               pixels + (y0 + row) * pitch + x0,
               (size_t)width * sizeof(uint32_t));
    }
    if (clear_surface) {
        for (int row = 0; row < height; row++) {
            memset(pixels + (y0 + row) * pitch + x0, 0, (size_t)width * sizeof(uint32_t));
        }
    }
    return true;
}

void render_composite_apply_blend(ImageSurface* surface, const uint32_t* backdrop,
                                  int x0, int y0, int width, int height,
                                  CssEnum blend_mode) {
    if (!surface || !surface->pixels || !backdrop || width <= 0 || height <= 0) {
        return;
    }
    uint32_t* pixels = (uint32_t*)surface->pixels;
    int pitch = surface->pitch / 4;
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            uint32_t source = pixels[(y0 + row) * pitch + (x0 + col)];
            uint32_t dst = backdrop[row * width + col];
            pixels[(y0 + row) * pitch + (x0 + col)] =
                render_composite_blend_pixel(dst, source, blend_mode);
        }
    }
}

void render_composite_source_over_premul(ImageSurface* surface, const uint32_t* backdrop,
                                         int x0, int y0, int width, int height) {
    if (!surface || !surface->pixels || !backdrop || width <= 0 || height <= 0) {
        return;
    }
    uint32_t* pixels = (uint32_t*)surface->pixels;
    int pitch = surface->pitch / 4;
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            uint32_t src = pixels[(y0 + row) * pitch + (x0 + col)];
            uint32_t dst = backdrop[row * width + col];
            if (src == 0) {
                pixels[(y0 + row) * pitch + (x0 + col)] = dst;
                continue;
            }
            uint32_t sa = (src >> 24) & 0xFF;
            if (sa == 255) continue;
            uint32_t inv_sa = 255 - sa;
            uint32_t da = (dst >> 24) & 0xFF;
            uint32_t ra = sa + (da * inv_sa + 127) / 255;
            uint32_t rr = (src & 0xFF) + (((dst & 0xFF) * inv_sa + 127) / 255);
            uint32_t rg = ((src >> 8) & 0xFF) + ((((dst >> 8) & 0xFF) * inv_sa + 127) / 255);
            uint32_t rb = ((src >> 16) & 0xFF) + ((((dst >> 16) & 0xFF) * inv_sa + 127) / 255);
            pixels[(y0 + row) * pitch + (x0 + col)] =
                (LMB_MIN(ra, 255u) << 24) |
                (LMB_MIN(rb, 255u) << 16) |
                (LMB_MIN(rg, 255u) << 8) |
                LMB_MIN(rr, 255u);
        }
    }
}

void render_composite_opacity(ImageSurface* surface, const uint32_t* backdrop,
                              int x0, int y0, int width, int height,
                              float opacity) {
    if (!surface || !surface->pixels || !backdrop || width <= 0 || height <= 0) {
        return;
    }
    uint32_t* pixels = (uint32_t*)surface->pixels;
    int pitch = surface->pitch / 4;
    int opacity_i = (int)(opacity * 255.0f + 0.5f);
    if (opacity_i < 0) opacity_i = 0;
    if (opacity_i > 255) opacity_i = 255;
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            uint32_t src = pixels[(y0 + row) * pitch + (x0 + col)];
            uint32_t dst = backdrop[row * width + col];
            if (src == 0) {
                pixels[(y0 + row) * pitch + (x0 + col)] = dst;
                continue;
            }
            uint32_t src_a = (src >> 24) & 0xFF;
            uint32_t sa = (src_a * (uint32_t)opacity_i + 127u) / 255u;
            if (sa == 0) {
                pixels[(y0 + row) * pitch + (x0 + col)] = dst;
                continue;
            }
            uint32_t src_r = src & 0xFF;
            uint32_t src_g = (src >> 8) & 0xFF;
            uint32_t src_b = (src >> 16) & 0xFF;
            uint32_t inv_sa = 255 - sa;
            uint32_t da = (dst >> 24) & 0xFF;
            uint32_t dr = dst & 0xFF;
            uint32_t dg = (dst >> 8) & 0xFF;
            uint32_t db = (dst >> 16) & 0xFF;
            uint32_t src_rp = (src_r * sa + 127u) / 255u;
            uint32_t src_gp = (src_g * sa + 127u) / 255u;
            uint32_t src_bp = (src_b * sa + 127u) / 255u;
            uint32_t dst_rp = (dr * da + 127u) / 255u;
            uint32_t dst_gp = (dg * da + 127u) / 255u;
            uint32_t dst_bp = (db * da + 127u) / 255u;
            uint32_t ra = sa + (da * inv_sa + 127u) / 255u;
            if (ra == 0) {
                pixels[(y0 + row) * pitch + (x0 + col)] = 0;
                continue;
            }
            uint32_t rrp = src_rp + (dst_rp * inv_sa + 127u) / 255u;
            uint32_t rgp = src_gp + (dst_gp * inv_sa + 127u) / 255u;
            uint32_t rbp = src_bp + (dst_bp * inv_sa + 127u) / 255u;
            uint32_t rr = (rrp * 255u + ra / 2u) / ra;
            uint32_t rg = (rgp * 255u + ra / 2u) / ra;
            uint32_t rb = (rbp * 255u + ra / 2u) / ra;
            pixels[(y0 + row) * pitch + (x0 + col)] =
                (LMB_MIN(ra, 255u) << 24) |
                (LMB_MIN(rb, 255u) << 16) |
                (LMB_MIN(rg, 255u) << 8) |
                LMB_MIN(rr, 255u);
        }
    }
}
