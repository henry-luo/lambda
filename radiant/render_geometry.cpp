#include "render_geometry.hpp"

static int render_geometry_pixel_coord(float value) {
    return (int)value; // INT_CAST_OK: raster surfaces are indexed with integer pixel coordinates.
}

Rect render_geometry_adjust_box_rect(Rect rect, CssEnum box, float scale,
                                     const BorderProp* border,
                                     const Spacing* padding) {
    Rect out = rect;
    if (box == CSS_VALUE_PADDING_BOX || box == CSS_VALUE_CONTENT_BOX) {
        float border_top = border ? border->width.top * scale : 0.0f;
        float border_right = border ? border->width.right * scale : 0.0f;
        float border_bottom = border ? border->width.bottom * scale : 0.0f;
        float border_left = border ? border->width.left * scale : 0.0f;
        out.x += border_left;
        out.y += border_top;
        out.width -= border_left + border_right;
        out.height -= border_top + border_bottom;
    }
    if (box == CSS_VALUE_CONTENT_BOX && padding) {
        float padding_top = padding->top * scale;
        float padding_right = padding->right * scale;
        float padding_bottom = padding->bottom * scale;
        float padding_left = padding->left * scale;
        out.x += padding_left;
        out.y += padding_top;
        out.width -= padding_left + padding_right;
        out.height -= padding_top + padding_bottom;
    }
    if (out.width < 0.0f) out.width = 0.0f;
    if (out.height < 0.0f) out.height = 0.0f;
    return out;
}

Bound render_geometry_intersect_bound_rect(Bound bound, Rect rect) {
    Bound out = bound;
    out.left = max(out.left, rect.x);
    out.top = max(out.top, rect.y);
    out.right = min(out.right, rect.x + rect.width);
    out.bottom = min(out.bottom, rect.y + rect.height);
    return out;
}

RenderPixelBounds render_geometry_clip_to_pixel_bounds(Bound clip,
                                                       const ImageSurface* surface) {
    int left = max(0, render_geometry_pixel_coord(clip.left));
    int top = max(0, render_geometry_pixel_coord(clip.top));
    int right = render_geometry_pixel_coord(clip.right);
    int bottom = render_geometry_pixel_coord(clip.bottom);
    if (surface) {
        right = min(surface->width, right);
        bottom = min(surface->height, bottom);
    }

    RenderPixelBounds bounds = {left, top, right - left, bottom - top};
    if (bounds.width < 0) bounds.width = 0;
    if (bounds.height < 0) bounds.height = 0;
    return bounds;
}

bool render_geometry_pixel_bounds_empty(RenderPixelBounds bounds) {
    return bounds.width <= 0 || bounds.height <= 0;
}

