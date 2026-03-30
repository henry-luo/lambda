#include "render_border.hpp"
#include "../lib/log.h"
#include <math.h>

// Bezier control point constant for circular arc approximation
// (4/3) * tan(π/8) ≈ 0.5522847498
#define KAPPA 0.5522847498f

/**
 * Helper function to apply transform and push paint to canvas
 */
static void push_with_transform(RenderContext* rdcon, Tvg_Paint paint) {
    if (rdcon->has_transform) {
        tvg_paint_set_transform(paint, &rdcon->transform);
    }
    tvg_canvas_push(rdcon->canvas, paint);
}

/**
 * Create a clip shape for ThorVG based on the render context's clip region
 */
static Tvg_Paint create_border_clip_shape(RenderContext* rdcon) {
    Tvg_Paint clip_rect = tvg_shape_new();

    if (rdcon->block.has_clip_radius) {
        // Use rounded clipping
        float clip_x = rdcon->block.clip.left;
        float clip_y = rdcon->block.clip.top;
        float clip_w = rdcon->block.clip.right - rdcon->block.clip.left;
        float clip_h = rdcon->block.clip.bottom - rdcon->block.clip.top;

        float r = rdcon->block.clip_radius.top_left;
        if (rdcon->block.clip_radius.top_right > 0) r = max(r, rdcon->block.clip_radius.top_right);
        if (rdcon->block.clip_radius.bottom_left > 0) r = max(r, rdcon->block.clip_radius.bottom_left);
        if (rdcon->block.clip_radius.bottom_right > 0) r = max(r, rdcon->block.clip_radius.bottom_right);

        tvg_shape_append_rect(clip_rect, clip_x, clip_y, clip_w, clip_h, r, r, true);
    } else {
        tvg_shape_append_rect(clip_rect,
            rdcon->block.clip.left, rdcon->block.clip.top,
            rdcon->block.clip.right - rdcon->block.clip.left,
            rdcon->block.clip.bottom - rdcon->block.clip.top, 0, 0, true);
    }

    tvg_shape_set_fill_color(clip_rect, 0, 0, 0, 255);
    return clip_rect;
}

/**
 * Constrain border radii to prevent overlapping per CSS Backgrounds Level 3 §5.5
 *
 * Algorithm:
 * 1. Let f = min(width / (r_left + r_right), height / (r_top + r_bottom))
 * 2. If f < 1, scale all radii by f
 */
void constrain_border_radii(BorderProp* border, float width, float height) {
    if (!border) return;

    // Calculate horizontal and vertical scale factors
    float horizontal_sum_top = border->radius.top_left + border->radius.top_right;
    float horizontal_sum_bottom = border->radius.bottom_left + border->radius.bottom_right;
    float vertical_sum_left = border->radius.top_left + border->radius.bottom_left;
    float vertical_sum_right = border->radius.top_right + border->radius.bottom_right;

    float f = 1.0f;

    // Check horizontal constraints
    if (horizontal_sum_top > width) {
        f = min(f, width / horizontal_sum_top);
    }
    if (horizontal_sum_bottom > width) {
        f = min(f, width / horizontal_sum_bottom);
    }

    // Check vertical constraints
    if (vertical_sum_left > height) {
        f = min(f, height / vertical_sum_left);
    }
    if (vertical_sum_right > height) {
        f = min(f, height / vertical_sum_right);
    }

    // Scale all radii if needed
    if (f < 1.0f) {
        log_debug("[BORDER RADIUS] Constraining radii by factor %.2f", f);
        border->radius.top_left *= f;
        border->radius.top_right *= f;
        border->radius.bottom_right *= f;
        border->radius.bottom_left *= f;
    }
}

/**
 * Check if any border radius is set
 */
static inline bool has_border_radius(BorderProp* border) {
    return border->radius.top_left > 0 || border->radius.top_right > 0 ||
           border->radius.bottom_right > 0 || border->radius.bottom_left > 0;
}

/**
 * Check if border style requires ThorVG rendering
 */
static inline bool needs_thorvg_rendering(CssEnum style) {
    return style == CSS_VALUE_DOTTED || style == CSS_VALUE_DASHED ||
           style == CSS_VALUE_DOUBLE || style == CSS_VALUE_GROOVE ||
           style == CSS_VALUE_RIDGE || style == CSS_VALUE_INSET ||
           style == CSS_VALUE_OUTSET;
}

/**
 * Main border rendering dispatch
 */
void render_border(RenderContext* rdcon, ViewBlock* view, Rect rect) {
    if (!view->bound || !view->bound->border) return;

    BorderProp* border = view->bound->border;
    float s = rdcon->scale;

    // Constrain border radii (scaled)
    Corner scaled_radius = border->radius;  // copy
    scaled_radius.top_left *= s;
    scaled_radius.top_right *= s;
    scaled_radius.bottom_left *= s;
    scaled_radius.bottom_right *= s;
    Corner orig_radius = border->radius;
    border->radius = scaled_radius;
    constrain_border_radii(border, rect.width, rect.height);

    // Check if we need ThorVG rendering
    bool has_radius = has_border_radius(border);
    bool needs_thorvg = has_radius ||
                        needs_thorvg_rendering(border->top_style) ||
                        needs_thorvg_rendering(border->right_style) ||
                        needs_thorvg_rendering(border->bottom_style) ||
                        needs_thorvg_rendering(border->left_style);

    // Scale border widths for rendering
    Spacing orig_width = border->width;
    border->width.top *= s;
    border->width.right *= s;
    border->width.bottom *= s;
    border->width.left *= s;

    if (needs_thorvg) {
        render_rounded_border(rdcon, view, rect);
    } else {
        render_straight_border(rdcon, view, rect);
    }
    
    // Restore original values
    border->width = orig_width;
    border->radius = orig_radius;
}

/**
 * Render straight borders (optimized path for rectangular borders)
 */
void render_straight_border(RenderContext* rdcon, ViewBlock* view, Rect rect) {
    BorderProp* border = view->bound->border;
    ImageSurface* surface = rdcon->ui_context->surface;

    // Left border
    if (border->width.left > 0 && border->left_style != CSS_VALUE_NONE &&
        border->left_style != CSS_VALUE_HIDDEN && border->left_color.a > 0) {
        Rect border_rect = {rect.x, rect.y, border->width.left, rect.height};
        fill_surface_rect(surface, &border_rect, border->left_color.c, &rdcon->block.clip);
    }

    // Right border
    if (border->width.right > 0 && border->right_style != CSS_VALUE_NONE &&
        border->right_style != CSS_VALUE_HIDDEN && border->right_color.a > 0) {
        Rect border_rect = {
            rect.x + rect.width - border->width.right,
            rect.y,
            border->width.right,
            rect.height
        };
        fill_surface_rect(surface, &border_rect, border->right_color.c, &rdcon->block.clip);
    }

    // Top border
    if (border->width.top > 0 && border->top_style != CSS_VALUE_NONE &&
        border->top_style != CSS_VALUE_HIDDEN && border->top_color.a > 0) {
        Rect border_rect = {rect.x, rect.y, rect.width, border->width.top};
        fill_surface_rect(surface, &border_rect, border->top_color.c, &rdcon->block.clip);
    }

    // Bottom border
    if (border->width.bottom > 0 && border->bottom_style != CSS_VALUE_NONE &&
        border->bottom_style != CSS_VALUE_HIDDEN && border->bottom_color.a > 0) {
        Rect border_rect = {
            rect.x,
            rect.y + rect.height - border->width.bottom,
            rect.width,
            border->width.bottom
        };
        fill_surface_rect(surface, &border_rect, border->bottom_color.c, &rdcon->block.clip);
    }
}

/**
 * Build rounded rectangle path with Bezier curves for ThorVG
 */
static Tvg_Paint build_rounded_border_path(Rect rect, BorderProp* border) {
    Tvg_Paint shape = tvg_shape_new();

    float x = rect.x;
    float y = rect.y;
    float w = rect.width;
    float h = rect.height;

    float r_tl = border->radius.top_left;
    float r_tr = border->radius.top_right;
    float r_br = border->radius.bottom_right;
    float r_bl = border->radius.bottom_left;

    // Start from top-left corner (after the radius)
    tvg_shape_move_to(shape, x + r_tl, y);

    // Top edge
    tvg_shape_line_to(shape, x + w - r_tr, y);

    // Top-right corner (Bezier curve)
    if (r_tr > 0) {
        float cp1_x = x + w - r_tr + r_tr * KAPPA;
        float cp1_y = y;
        float cp2_x = x + w;
        float cp2_y = y + r_tr - r_tr * KAPPA;
        float end_x = x + w;
        float end_y = y + r_tr;
        tvg_shape_cubic_to(shape, cp1_x, cp1_y, cp2_x, cp2_y, end_x, end_y);
    }

    // Right edge
    tvg_shape_line_to(shape, x + w, y + h - r_br);

    // Bottom-right corner (Bezier curve)
    if (r_br > 0) {
        float cp1_x = x + w;
        float cp1_y = y + h - r_br + r_br * KAPPA;
        float cp2_x = x + w - r_br + r_br * KAPPA;
        float cp2_y = y + h;
        float end_x = x + w - r_br;
        float end_y = y + h;
        tvg_shape_cubic_to(shape, cp1_x, cp1_y, cp2_x, cp2_y, end_x, end_y);
    }

    // Bottom edge
    tvg_shape_line_to(shape, x + r_bl, y + h);

    // Bottom-left corner (Bezier curve)
    if (r_bl > 0) {
        float cp1_x = x + r_bl - r_bl * KAPPA;
        float cp1_y = y + h;
        float cp2_x = x;
        float cp2_y = y + h - r_bl + r_bl * KAPPA;
        float end_x = x;
        float end_y = y + h - r_bl;
        tvg_shape_cubic_to(shape, cp1_x, cp1_y, cp2_x, cp2_y, end_x, end_y);
    }

    // Left edge
    tvg_shape_line_to(shape, x, y + r_tl);

    // Top-left corner (Bezier curve)
    if (r_tl > 0) {
        float cp1_x = x;
        float cp1_y = y + r_tl - r_tl * KAPPA;
        float cp2_x = x + r_tl - r_tl * KAPPA;
        float cp2_y = y;
        float end_x = x + r_tl;
        float end_y = y;
        tvg_shape_cubic_to(shape, cp1_x, cp1_y, cp2_x, cp2_y, end_x, end_y);
    }

    tvg_shape_close(shape);
    return shape;
}

/**
 * Apply dash pattern for dotted/dashed borders
 */
static void apply_dash_pattern(Tvg_Paint shape, CssEnum style, float width) {
    if (style == CSS_VALUE_DOTTED) {
        // Dotted: round dots with spacing
        float dash_pattern[] = {width, width * 2};
        tvg_shape_set_stroke_dash(shape, dash_pattern, 2, 0);
        tvg_shape_set_stroke_cap(shape, TVG_STROKE_CAP_ROUND);
    } else if (style == CSS_VALUE_DASHED) {
        // Dashed: longer dashes with spacing
        float dash_pattern[] = {width * 3, width * 3};
        tvg_shape_set_stroke_dash(shape, dash_pattern, 2, 0);
        tvg_shape_set_stroke_cap(shape, TVG_STROKE_CAP_BUTT);
    }
}

/**
 * Render border with ThorVG (supports rounded corners and styled borders)
 */
void render_rounded_border(RenderContext* rdcon, ViewBlock* view, Rect rect) {
    BorderProp* border = view->bound->border;
    Tvg_Canvas canvas = rdcon->canvas;

    // For uniform borders, we can render as a single shape
    bool uniform_width = (border->width.top == border->width.right &&
                          border->width.right == border->width.bottom &&
                          border->width.bottom == border->width.left);
    bool uniform_style = (border->top_style == border->right_style &&
                          border->right_style == border->bottom_style &&
                          border->bottom_style == border->left_style);
    bool uniform_color = (border->top_color.c == border->right_color.c &&
                          border->right_color.c == border->bottom_color.c &&
                          border->bottom_color.c == border->left_color.c);

    if (uniform_width && uniform_style && uniform_color && border->width.top > 0 &&
        border->top_style != CSS_VALUE_NONE && border->top_style != CSS_VALUE_HIDDEN) {

        CssEnum style = border->top_style;
        float w = border->width.top;
        Color c = border->top_color;

        if (style == CSS_VALUE_DOUBLE && w >= 3) {
            // CSS double border: two lines with a gap between them
            // Outer line width = floor(w/3), inner line width = floor(w/3), gap = w - 2*line_w
            float line_w = floorf(w / 3.0f);
            if (line_w < 1) line_w = 1;

            // Outer border (inset by half line width from the border rect)
            Tvg_Paint outer = build_rounded_border_path(rect, border);
            tvg_shape_set_stroke_width(outer, line_w);
            tvg_shape_set_stroke_color(outer, c.r, c.g, c.b, c.a);
            tvg_shape_set_stroke_join(outer, TVG_STROKE_JOIN_MITER);

            // Inner border (inset by w - line_w/2 from rect)
            float inset = w - line_w;
            Rect inner_rect = {rect.x + inset, rect.y + inset,
                               rect.width - inset * 2, rect.height - inset * 2};
            // Build inner path using adjusted rect (radii shrink by inset)
            Corner orig_r = border->radius;
            border->radius.top_left = max(0.0f, orig_r.top_left - inset);
            border->radius.top_right = max(0.0f, orig_r.top_right - inset);
            border->radius.bottom_right = max(0.0f, orig_r.bottom_right - inset);
            border->radius.bottom_left = max(0.0f, orig_r.bottom_left - inset);
            Tvg_Paint inner = build_rounded_border_path(inner_rect, border);
            border->radius = orig_r;
            tvg_shape_set_stroke_width(inner, line_w);
            tvg_shape_set_stroke_color(inner, c.r, c.g, c.b, c.a);
            tvg_shape_set_stroke_join(inner, TVG_STROKE_JOIN_MITER);

            Tvg_Paint clip1 = create_border_clip_shape(rdcon);
            tvg_paint_set_mask_method(outer, clip1, TVG_MASK_METHOD_ALPHA);
            Tvg_Paint clip2 = create_border_clip_shape(rdcon);
            tvg_paint_set_mask_method(inner, clip2, TVG_MASK_METHOD_ALPHA);

            tvg_canvas_remove(canvas, NULL);
            push_with_transform(rdcon, outer);
            push_with_transform(rdcon, inner);
            tvg_canvas_reset_and_draw(rdcon, false);
            tvg_canvas_remove(canvas, NULL);

        } else if (style == CSS_VALUE_GROOVE || style == CSS_VALUE_RIDGE) {
            // Groove: outer half dark, inner half light (3D effect)
            // Ridge: outer half light, inner half dark (opposite of groove)
            float half_w = w / 2.0f;

            // Calculate light and dark colors
            uint8_t dark_r = (uint8_t)(c.r * 0.5f);
            uint8_t dark_g = (uint8_t)(c.g * 0.5f);
            uint8_t dark_b = (uint8_t)(c.b * 0.5f);
            uint8_t light_r = (uint8_t)min(255.0f, c.r * 1.5f);
            uint8_t light_g = (uint8_t)min(255.0f, c.g * 1.5f);
            uint8_t light_b = (uint8_t)min(255.0f, c.b * 1.5f);

            bool groove = (style == CSS_VALUE_GROOVE);

            // Outer half
            Tvg_Paint outer = build_rounded_border_path(rect, border);
            tvg_shape_set_stroke_width(outer, half_w);
            if (groove)
                tvg_shape_set_stroke_color(outer, dark_r, dark_g, dark_b, c.a);
            else
                tvg_shape_set_stroke_color(outer, light_r, light_g, light_b, c.a);
            tvg_shape_set_stroke_join(outer, TVG_STROKE_JOIN_MITER);

            // Inner half
            float inset = half_w;
            Rect inner_rect = {rect.x + inset, rect.y + inset,
                               rect.width - inset * 2, rect.height - inset * 2};
            Corner orig_r = border->radius;
            border->radius.top_left = max(0.0f, orig_r.top_left - inset);
            border->radius.top_right = max(0.0f, orig_r.top_right - inset);
            border->radius.bottom_right = max(0.0f, orig_r.bottom_right - inset);
            border->radius.bottom_left = max(0.0f, orig_r.bottom_left - inset);
            Tvg_Paint inner = build_rounded_border_path(inner_rect, border);
            border->radius = orig_r;
            tvg_shape_set_stroke_width(inner, half_w);
            if (groove)
                tvg_shape_set_stroke_color(inner, light_r, light_g, light_b, c.a);
            else
                tvg_shape_set_stroke_color(inner, dark_r, dark_g, dark_b, c.a);
            tvg_shape_set_stroke_join(inner, TVG_STROKE_JOIN_MITER);

            Tvg_Paint clip1 = create_border_clip_shape(rdcon);
            tvg_paint_set_mask_method(outer, clip1, TVG_MASK_METHOD_ALPHA);
            Tvg_Paint clip2 = create_border_clip_shape(rdcon);
            tvg_paint_set_mask_method(inner, clip2, TVG_MASK_METHOD_ALPHA);

            tvg_canvas_remove(canvas, NULL);
            push_with_transform(rdcon, outer);
            push_with_transform(rdcon, inner);
            tvg_canvas_reset_and_draw(rdcon, false);
            tvg_canvas_remove(canvas, NULL);

        } else if (style == CSS_VALUE_INSET || style == CSS_VALUE_OUTSET) {
            // Inset: top+left dark, bottom+right light (pressed appearance)
            // Outset: top+left light, bottom+right dark (raised appearance)
            // Rendered as a single stroke with the base color
            // (full per-side coloring requires non-uniform border path)
            uint8_t dark_r = (uint8_t)(c.r * 0.6f);
            uint8_t dark_g = (uint8_t)(c.g * 0.6f);
            uint8_t dark_b = (uint8_t)(c.b * 0.6f);

            Tvg_Paint shape = build_rounded_border_path(rect, border);
            tvg_shape_set_stroke_width(shape, w);
            if (style == CSS_VALUE_INSET)
                tvg_shape_set_stroke_color(shape, dark_r, dark_g, dark_b, c.a);
            else
                tvg_shape_set_stroke_color(shape, c.r, c.g, c.b, c.a);
            tvg_shape_set_stroke_join(shape, TVG_STROKE_JOIN_MITER);

            Tvg_Paint clip_rect = create_border_clip_shape(rdcon);
            tvg_paint_set_mask_method(shape, clip_rect, TVG_MASK_METHOD_ALPHA);

            tvg_canvas_remove(canvas, NULL);
            push_with_transform(rdcon, shape);
            tvg_canvas_reset_and_draw(rdcon, false);
            tvg_canvas_remove(canvas, NULL);

        } else {
            // Default: solid, dotted, dashed
            Tvg_Paint shape = build_rounded_border_path(rect, border);
            tvg_shape_set_stroke_width(shape, w);
            tvg_shape_set_stroke_color(shape, c.r, c.g, c.b, c.a);
            tvg_shape_set_stroke_join(shape, TVG_STROKE_JOIN_MITER);
            apply_dash_pattern(shape, style, w);

            Tvg_Paint clip_rect = create_border_clip_shape(rdcon);
            tvg_paint_set_mask_method(shape, clip_rect, TVG_MASK_METHOD_ALPHA);

            tvg_canvas_remove(canvas, NULL);
            push_with_transform(rdcon, shape);
            tvg_canvas_reset_and_draw(rdcon, false);
            tvg_canvas_remove(canvas, NULL);
        }
    } else {
        // Render each side separately for non-uniform borders
        // TODO: Implement per-side rendering with proper corner handling
        log_debug("[BORDER] Non-uniform borders not fully implemented yet");

        // Fall back to straight border rendering for now
        render_straight_border(rdcon, view, rect);
    }
}

/**
 * Render CSS outline (CSS UI Level 3)
 * Outline is drawn outside the border-box, offset by outline-offset.
 * Does not affect layout. Uses border-radius if present.
 */
void render_outline(RenderContext* rdcon, ViewBlock* view, Rect rect) {
    if (!view->bound || !view->bound->outline) return;

    OutlineProp* outline = view->bound->outline;
    if (outline->width <= 0 || outline->style == CSS_VALUE_NONE || outline->style == CSS_VALUE_HIDDEN) return;
    if (outline->color.a == 0) return;

    float s = rdcon->scale;
    float w = outline->width * s;
    float offset = outline->offset * s;

    // Outline rect is expanded outward from border-box by (outline-width/2 + outline-offset)
    float expand = w * 0.5f + offset;
    Rect outline_rect;
    outline_rect.x = rect.x - expand;
    outline_rect.y = rect.y - expand;
    outline_rect.width = rect.width + expand * 2;
    outline_rect.height = rect.height + expand * 2;

    Tvg_Paint shape = tvg_shape_new();

    // If border-radius exists, use rounded outline path
    bool has_radius = view->bound->border &&
        (view->bound->border->radius.top_left > 0 || view->bound->border->radius.top_right > 0 ||
         view->bound->border->radius.bottom_right > 0 || view->bound->border->radius.bottom_left > 0);

    if (has_radius) {
        BorderProp* border = view->bound->border;
        // Expand radii by the offset + half width to follow curvature
        float r_tl = (border->radius.top_left * s + expand);
        float r_tr = (border->radius.top_right * s + expand);
        float r_br = (border->radius.bottom_right * s + expand);
        float r_bl = (border->radius.bottom_left * s + expand);
        if (r_tl < 0) r_tl = 0;
        if (r_tr < 0) r_tr = 0;
        if (r_br < 0) r_br = 0;
        if (r_bl < 0) r_bl = 0;

        float x = outline_rect.x, y = outline_rect.y;
        float ow = outline_rect.width, oh = outline_rect.height;

        tvg_shape_move_to(shape, x + r_tl, y);
        tvg_shape_line_to(shape, x + ow - r_tr, y);
        if (r_tr > 0) {
            tvg_shape_cubic_to(shape,
                x + ow - r_tr + r_tr * KAPPA, y,
                x + ow, y + r_tr - r_tr * KAPPA,
                x + ow, y + r_tr);
        }
        tvg_shape_line_to(shape, x + ow, y + oh - r_br);
        if (r_br > 0) {
            tvg_shape_cubic_to(shape,
                x + ow, y + oh - r_br + r_br * KAPPA,
                x + ow - r_br + r_br * KAPPA, y + oh,
                x + ow - r_br, y + oh);
        }
        tvg_shape_line_to(shape, x + r_bl, y + oh);
        if (r_bl > 0) {
            tvg_shape_cubic_to(shape,
                x + r_bl - r_bl * KAPPA, y + oh,
                x, y + oh - r_bl + r_bl * KAPPA,
                x, y + oh - r_bl);
        }
        tvg_shape_line_to(shape, x, y + r_tl);
        if (r_tl > 0) {
            tvg_shape_cubic_to(shape,
                x, y + r_tl - r_tl * KAPPA,
                x + r_tl - r_tl * KAPPA, y,
                x + r_tl, y);
        }
        tvg_shape_close(shape);
    } else {
        tvg_shape_append_rect(shape,
            outline_rect.x, outline_rect.y,
            outline_rect.width, outline_rect.height,
            0, 0, true);
    }

    tvg_shape_set_stroke_width(shape, w);
    tvg_shape_set_stroke_color(shape,
        outline->color.r, outline->color.g,
        outline->color.b, outline->color.a);
    tvg_shape_set_stroke_join(shape, TVG_STROKE_JOIN_MITER);

    apply_dash_pattern(shape, outline->style, w);

    Tvg_Paint clip_rect = create_border_clip_shape(rdcon);
    tvg_paint_set_mask_method(shape, clip_rect, TVG_MASK_METHOD_ALPHA);

    tvg_canvas_remove(rdcon->canvas, NULL);
    push_with_transform(rdcon, shape);
    tvg_canvas_reset_and_draw(rdcon, false);
    tvg_canvas_remove(rdcon->canvas, NULL);

    log_debug("[OUTLINE] Rendered outline: width=%.1f offset=%.1f style=%d color=#%02x%02x%02x%02x",
              outline->width, outline->offset, outline->style,
              outline->color.r, outline->color.g, outline->color.b, outline->color.a);
}
