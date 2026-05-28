#pragma once
#include "render.hpp"
#include "view.hpp"

// Border rendering functions
void render_border(RenderContext* rdcon, ViewBlock* view, Rect rect);
bool corner_has_radius(const Corner* radius);
void constrain_corner_radii(Corner* radius, float width, float height);
void constrain_border_radii(BorderProp* border, float width, float height);
void resolve_border_radius_percentages(Corner* radius, float width, float height);

// Outline rendering
void render_outline(RenderContext* rdcon, ViewBlock* view, Rect rect);
