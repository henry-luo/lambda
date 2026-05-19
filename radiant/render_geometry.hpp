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

Rect render_geometry_block_border_rect(const BlockBlot* parent_block,
                                       const ViewBlock* block,
                                       float scale);
Rect render_geometry_expand_rect(Rect rect, float expand);
Bound render_geometry_rect_to_bound(Rect rect);
bool render_geometry_bounds_intersect(Bound a, Bound b);
float render_geometry_filter_effect_expand(const FilterProp* filter);
float render_geometry_block_visual_overflow(const ViewBlock* block);
