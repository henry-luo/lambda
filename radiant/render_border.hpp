#pragma once
#include "render.hpp"
#include "view.hpp"

// Border rendering functions
void render_border(RenderContext* rdcon, ViewBlock* view, Rect rect);
void render_straight_border(RenderContext* rdcon, ViewBlock* view, Rect rect);
void render_rounded_border(RenderContext* rdcon, ViewBlock* view, Rect rect);
void constrain_border_radii(BorderProp* border, float width, float height);
