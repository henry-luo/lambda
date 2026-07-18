#include "render.hpp"

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

IRect render_geometry_clip_to_pixel_bounds(Bound clip,
                                           const ImageSurface* surface) {
    int left = max(0, render_geometry_pixel_coord(clip.left));
    int top = max(0, render_geometry_pixel_coord(clip.top));
    int right = render_geometry_pixel_coord(clip.right);
    int bottom = render_geometry_pixel_coord(clip.bottom);
    if (surface) {
        right = min(surface->width, right);
        bottom = min(surface->height, bottom);
    }

    IRect bounds = {left, top, right - left, bottom - top};
    if (bounds.w < 0) bounds.w = 0;
    if (bounds.h < 0) bounds.h = 0;
    return bounds;
}

bool render_geometry_pixel_bounds_empty(IRect bounds) {
    return bounds.w <= 0 || bounds.h <= 0;
}

Rect render_geometry_block_border_rect(const BlockBlot* parent_block,
                                       const ViewBlock* block,
                                       float scale) {
    Rect rect = {};
    if (!parent_block || !block) return rect;
    rect.x = parent_block->x + block->x * scale;
    rect.y = parent_block->y + block->y * scale;
    rect.width = block->width * scale;
    rect.height = block->height * scale;
    return rect;
}

Rect render_geometry_block_content_rect(const BlockBlot* parent_block,
                                        const ViewBlock* block,
                                        float scale) {
    Rect rect = render_geometry_block_border_rect(parent_block, block, scale);
    if (!block || !block->bound) return rect;
    return render_geometry_adjust_box_rect(rect, CSS_VALUE_CONTENT_BOX, scale,
                                           block->boundary()->border,
                                           &block->boundary()->padding);
}

Rect render_geometry_expand_rect(Rect rect, float expand) {
    if (expand <= 0.0f) return rect;
    rect.x -= expand;
    rect.y -= expand;
    rect.width += expand * 2.0f;
    rect.height += expand * 2.0f;
    if (rect.width < 0.0f) rect.width = 0.0f;
    if (rect.height < 0.0f) rect.height = 0.0f;
    return rect;
}

Bound render_geometry_rect_to_bound(Rect rect) {
    Bound bound = {rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
    return bound;
}

bool render_geometry_bounds_intersect(Bound a, Bound b) {
    return a.left < b.right && a.right > b.left &&
           a.top < b.bottom && a.bottom > b.top;
}

static float render_geometry_absf(float value) {
    return value < 0.0f ? -value : value;
}

float render_geometry_filter_effect_expand(const FilterProp* filter) {
    if (!filter || !filter->functions) return 0.0f;
    float expand = 0.0f;
    FilterFunction* ff = filter->functions;
    while (ff) {
        if (ff->type == FILTER_BLUR) {
            float blur_expand = ff->params.blur_radius * 2.0f;
            if (blur_expand > expand) expand = blur_expand;
        } else if (ff->type == FILTER_DROP_SHADOW) {
            float shadow_expand =
                render_geometry_absf(ff->params.drop_shadow.offset_x) +
                render_geometry_absf(ff->params.drop_shadow.offset_y) +
                ff->params.drop_shadow.blur_radius + 2.0f;
            if (shadow_expand > expand) expand = shadow_expand;
        }
        ff = ff->next;
    }
    return expand;
}

float render_geometry_block_visual_overflow(const ViewBlock* block) {
    if (!block) return 0.0f;

    float overflow = 0.0f;
    if (block->filter) {
        float filter_overflow = render_geometry_filter_effect_expand(block->filter);
        if (filter_overflow > overflow) overflow = filter_overflow;
    }
    if (block->bound && block->boundary()->box_shadow) {
        BoxShadow* shadow = block->boundary()->box_shadow;
        while (shadow) {
            if (!shadow->inset) {
                float shadow_overflow =
                    render_geometry_absf(shadow->offset_x) +
                    render_geometry_absf(shadow->offset_y) +
                    shadow->blur_radius + render_geometry_absf(shadow->spread_radius) + 2.0f;
                if (shadow_overflow > overflow) overflow = shadow_overflow;
            }
            shadow = shadow->next;
        }
    }
    if (block->bound && block->boundary()->outline) {
        float outline_overflow = block->boundary()->outline->width + block->boundary()->outline->offset + 2.0f;
        if (outline_overflow > overflow) overflow = outline_overflow;
    }
    return overflow;
}

static bool render_geometry_matrix_is_identity(const RdtMatrix* matrix) {
    return matrix &&
        fabsf(matrix->e11 - 1.0f) < 0.00001f && fabsf(matrix->e12) < 0.00001f &&
        fabsf(matrix->e13) < 0.00001f && fabsf(matrix->e21) < 0.00001f &&
        fabsf(matrix->e22 - 1.0f) < 0.00001f && fabsf(matrix->e23) < 0.00001f &&
        fabsf(matrix->e31) < 0.00001f && fabsf(matrix->e32) < 0.00001f &&
        fabsf(matrix->e33 - 1.0f) < 0.00001f;
}

bool render_geometry_transform_matrix(const TransformProp* transform,
                                      float x, float y, float width, float height,
                                      RdtMatrix* out_matrix) {
    if (!transform || !transform->functions || !out_matrix) return false;
    float origin_x = transform->origin_x_percent
        ? x + width * transform->origin_x / 100.0f
        : x + transform->origin_x;
    float origin_y = transform->origin_y_percent
        ? y + height * transform->origin_y / 100.0f
        : y + transform->origin_y;
    // Export backends must use the screen path's origin composition and preserve scale(0).
    *out_matrix = radiant::compute_transform_matrix(
        transform->functions, width, height, origin_x, origin_y);
    return !render_geometry_matrix_is_identity(out_matrix);
}
