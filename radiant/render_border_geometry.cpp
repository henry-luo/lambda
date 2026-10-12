// backend-neutral border geometry is shared by PaintIR and raster painters.
#include "render.hpp"
#include <math.h>

Color render_border_darken_color(Color c, float factor) {
    Color out;
    out.r = (uint8_t)(c.r * factor);
    out.g = (uint8_t)(c.g * factor);
    out.b = (uint8_t)(c.b * factor);
    out.a = c.a;
    return out;
}

Color render_border_lighten_color(Color c, float factor) {
    Color out;
    out.r = (uint8_t)min(255.0f, c.r + (255.0f - c.r) * factor);
    out.g = (uint8_t)min(255.0f, c.g + (255.0f - c.g) * factor);
    out.b = (uint8_t)min(255.0f, c.b + (255.0f - c.b) * factor);
    out.a = c.a;
    return out;
}

// For inset/outset: compute per-side dark/light colors from base color.
// CSS spec: inset → top+left dark (shadowed), bottom+right lighter (lit)
//           outset → top+left lighter (lit), bottom+right dark (shadowed)
void render_border_inset_outset_colors(Color base, CssEnum style,
    Color* out_top, Color* out_right, Color* out_bottom, Color* out_left) {
    Color dark  = render_border_darken_color(base, BORDER_DARKEN_FACTOR);
    Color light = render_border_lighten_color(base, BORDER_LIGHTEN_FACTOR);
    if (style == CSS_VALUE_INSET) {
        if (out_top) *out_top = dark;
        if (out_left) *out_left = dark;
        if (out_bottom) *out_bottom = light;
        if (out_right) *out_right = light;
    } else { // CSS_VALUE_OUTSET
        if (out_top) *out_top = light;
        if (out_left) *out_left = light;
        if (out_bottom) *out_bottom = dark;
        if (out_right) *out_right = dark;
    }
}

RdtPath* render_border_create_centered_stroke_path(const BorderProp* border,
                                                          Rect rect,
                                                          float width) {
    float half_w = width / 2.0f;
    Rect stroke_rect = {rect.x + half_w, rect.y + half_w,
                        rect.width - width, rect.height - width};
    Corner stroke_radius = radiant_corner_inset(&border->radius, half_w, half_w);
    return render_path_create_rounded_rect(stroke_rect, &stroke_radius);
}

/**
 * Render each border side independently using trapezoid fills.
 */

/**
 * Get dash pattern for dotted/dashed borders. Returns dash count (0 if none).
 * Caller provides a float[2] array. Also sets the cap style.
 */
int render_border_dash_pattern(CssEnum style, float width, float* out_dash, RdtStrokeCap* out_cap) {
    if (style == CSS_VALUE_DOTTED) {
        // Zero-length dash with round cap produces a circle of diameter = stroke_width.
        // Gap = 2*width so visual gap (gap - width due to caps) = width.
        out_dash[0] = 0;
        out_dash[1] = width * 2;
        *out_cap = RDT_CAP_ROUND;
        return 2;
    } else if (style == CSS_VALUE_DASHED) {
        out_dash[0] = width * 2;
        out_dash[1] = width;
        *out_cap = RDT_CAP_BUTT;
        return 2;
    }
    *out_cap = RDT_CAP_BUTT;
    return 0;
}

bool render_border_style_supported(CssEnum style) {
    switch (style) {
        case CSS_VALUE_NONE: case CSS_VALUE_HIDDEN: case CSS_VALUE_SOLID:
        case CSS_VALUE_DOUBLE: case CSS_VALUE_GROOVE: case CSS_VALUE_RIDGE:
        case CSS_VALUE_INSET: case CSS_VALUE_OUTSET: case CSS_VALUE_DASHED: case CSS_VALUE_DOTTED:
            return true;
        default: return false;
    }
}

static bool border_emit_band(Rect rect, size_t side, float width, float before, float after,
                             Color color, RenderBorderPathCallback emit, void* context) {
    if (rect.width <= 0.0f || rect.height <= 0.0f || width <= 0.0f || !color.a) return true;
    RdtPath* path = render_path_create_border_side(rect, side, width, before, after);
    return path && emit(context, path, color, nullptr);
}

bool render_border_emit_side(Rect rect, const BorderProp* border, size_t side, bool rounded,
                             RenderBorderPathCallback emit, void* context) {
    if (!border || side >= 4 || !emit) return false;
    CssEnum style = border->styles[side]; Color color = border->colors[side];
    float width = border->width.values[side];
    if (!render_border_style_supported(style)) return false;
    if (width <= 0.0f || !color.a || style == CSS_VALUE_NONE || style == CSS_VALUE_HIDDEN) return true;
    float before = border->width.values[side == 0 || side == 2 ? 3 : 0];
    float after = border->width.values[side == 0 || side == 2 ? 1 : 2];
    if ((style == CSS_VALUE_DOUBLE && width >= 3.0f) || style == CSS_VALUE_GROOVE || style == CSS_VALUE_RIDGE) {
        float band = style == CSS_VALUE_DOUBLE ? floorf(width / 3.0f) : width * 0.5f;
        Color outer = color, inner = color;
        if (style != CSS_VALUE_DOUBLE) {
            Color dark = render_border_darken_color(color, 0.5f);
            bool dark_outer = (side == 0 || side == 3) == (style == CSS_VALUE_GROOVE);
            outer = dark_outer ? dark : color; inner = dark_outer ? color : dark;
        }
        if (!border_emit_band(rect, side, band, before > 0 ? band : 0, after > 0 ? band : 0, outer, emit, context)) return false;
        float inset = width - band;
        // inset only the painted edge: a narrow left/top rule must retain its inner band.
        Rect inside = rect;
        if (side == 0 || side == 2) { inside.y += side == 0 ? inset : 0; inside.height -= inset; }
        else { inside.x += side == 3 ? inset : 0; inside.width -= inset; }
        return border_emit_band(inside, side, band, before > 0 ? band : 0, after > 0 ? band : 0, inner, emit, context);
    }
    if (style == CSS_VALUE_DASHED || style == CSS_VALUE_DOTTED) {
        RenderBorderStroke stroke = {}; stroke.width = width; stroke.phase = width * 0.5f;
        stroke.dash_count = render_border_dash_pattern(style, width, stroke.dashes, &stroke.cap);
        if (!rounded && style == CSS_VALUE_DASHED) {
            float length = side == 0 || side == 2 ? rect.width : rect.height;
            float count = fmaxf(1.0f, roundf(length / (stroke.dashes[0] + stroke.dashes[1])));
            if (count > 1.0f) {
                float gap = (length - count * stroke.dashes[0]) / (count - 1.0f);
                if (gap > 0.0f) stroke.dashes[1] = gap;
            }
            stroke.phase = 0.0f;
        }
        float x = rect.x, y = rect.y, half = width * 0.5f;
        RdtPath* path = rdt_path_new(); if (!path) return false;
        if (side == 0 || side == 2) {
            y += side == 0 ? half : rect.height - half;
            rdt_path_move_to(path, x, y); rdt_path_line_to(path, x + rect.width, y);
        } else {
            x += side == 3 ? half : rect.width - half;
            rdt_path_move_to(path, x, y); rdt_path_line_to(path, x, y + rect.height);
        }
        return emit(context, path, color, &stroke);
    }
    if (style == CSS_VALUE_INSET || style == CSS_VALUE_OUTSET) {
        Color colors[4];
        render_border_inset_outset_colors(color, style, &colors[0], &colors[1], &colors[2], &colors[3]);
        color = colors[side];
    }
    return border_emit_band(rect, side, width, before, after, color, emit, context);
}

