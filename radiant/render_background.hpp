#pragma once
#include "render.hpp"
#include "view.hpp"

// Background rendering functions
void render_background(RenderContext* rdcon, ViewBlock* view, Rect rect);
void render_background_color(RenderContext* rdcon, ViewBlock* view, Color color, Rect rect);
void render_background_gradient(RenderContext* rdcon, ViewBlock* view, BackgroundProp* bg, Rect rect);
void render_background_image(RenderContext* rdcon, ViewBlock* view, BackgroundProp* bg, Rect rect);
void render_linear_gradient(RenderContext* rdcon, ViewBlock* view, LinearGradient* gradient, Rect rect);
void render_radial_gradient(RenderContext* rdcon, ViewBlock* view, RadialGradient* gradient, Rect rect);
void render_conic_gradient(RenderContext* rdcon, ViewBlock* view, ConicGradient* gradient, Rect rect);

// Box shadow rendering
void render_box_shadow(RenderContext* rdcon, ViewBlock* view, Rect rect);
void render_box_shadow_inset(RenderContext* rdcon, ViewBlock* view, Rect rect);

// Software Gaussian blur (3-pass box blur approximation)
// Can be used by box-shadow, text-shadow, and filter:blur()
void box_blur_region(ScratchArena* sa, ImageSurface* surface, int rx, int ry, int rw, int rh, float blur_radius);

// Inset box-shadow blur: blur in temp buffer with bg_color in padding area,
// then copy inner rect back. Surface outside element is never modified.
void box_blur_region_inset(ScratchArena* sa, ImageSurface* surface,
                           int rx, int ry, int rw, int rh,
                           int pad, float blur_radius, uint32_t bg_color);

// Outer box-shadow rendering: rasterise the shadow rounded rect into a private
// temp buffer (premultiplied), apply 3-pass box blur to that buffer, then
// composite over the surface using src-over.  Pixels inside the element's
// border-box (exclude_shape) are skipped per CSS spec; the surface outside the
// shadow path is never read by the blur kernel, so sibling element pixels are
// not contaminated by shadow blur.
void render_outer_shadow_blur_composite(
    ScratchArena* sa, ImageSurface* surface,
    float shadow_x, float shadow_y, float shadow_w, float shadow_h,
    float sr_tl, float sr_tr, float sr_br, float sr_bl,
    Color shadow_color, float blur_radius,
    int exclude_type, const float* exclude_params,
    int clip_type, const float* clip_params);

// CSS blend mode compositing: blend source pixel onto backdrop
// pixel format: ABGR (A=bits24-31, B=bits16-23, G=bits8-15, R=bits0-7)
uint32_t composite_blend_pixel(uint32_t backdrop, uint32_t source, CssEnum blend_mode);
