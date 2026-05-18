#pragma once

#include "view.hpp"

typedef struct RenderPixelBounds {
    int x;
    int y;
    int width;
    int height;
} RenderPixelBounds;

Rect render_geometry_adjust_box_rect(Rect rect, CssEnum box, float scale,
                                     const BorderProp* border,
                                     const Spacing* padding);
Bound render_geometry_intersect_bound_rect(Bound bound, Rect rect);
RenderPixelBounds render_geometry_clip_to_pixel_bounds(Bound clip,
                                                       const ImageSurface* surface);
bool render_geometry_pixel_bounds_empty(RenderPixelBounds bounds);

