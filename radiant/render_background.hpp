#pragma once
#include "render.hpp"
#include "view.hpp"

// Background rendering functions
void render_background(RenderContext* rdcon, ViewBlock* view, Rect rect);
void render_background_color(RenderContext* rdcon, ViewBlock* view, Color color, Rect rect);
void render_background_gradient(RenderContext* rdcon, ViewBlock* view, BackgroundProp* bg, Rect rect);
void render_linear_gradient(RenderContext* rdcon, ViewBlock* view, LinearGradient* gradient, Rect rect);
void render_radial_gradient(RenderContext* rdcon, ViewBlock* view, RadialGradient* gradient, Rect rect);
void render_conic_gradient(RenderContext* rdcon, ViewBlock* view, ConicGradient* gradient, Rect rect);

// Box shadow rendering
void render_box_shadow(RenderContext* rdcon, ViewBlock* view, Rect rect);
