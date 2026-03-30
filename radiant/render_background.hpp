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
void box_blur_region(ImageSurface* surface, int rx, int ry, int rw, int rh, float blur_radius);
