#include "render.hpp"
#include "../lib/log.h"
#include "../lib/lambda_alloca.h"
#include <math.h>

// ---------------------------------------------------------------------------
// Color helpers — Chrome-compatible 3D border color computation
// Chrome: DarkenColor() multiplies RGB by 2/3, LightenColor() blends 1/3 toward white
// ---------------------------------------------------------------------------

static constexpr float BORDER_DARKEN_FACTOR  = 2.0f / 3.0f;
static constexpr float BORDER_LIGHTEN_FACTOR = 1.0f / 3.0f;

static void render_straight_border(RenderContext* rdcon, ViewBlock* view, Rect rect);
static void render_rounded_border(RenderContext* rdcon, ViewBlock* view, Rect rect);
static bool render_border_image_gradient(RenderContext* rdcon, BorderProp* border, Rect rect);

static inline Color color_darken(Color c, float factor) {
    Color out;
    out.r = (uint8_t)(c.r * factor);
    out.g = (uint8_t)(c.g * factor);
    out.b = (uint8_t)(c.b * factor);
    out.a = c.a;
    return out;
}

static inline Color color_lighten(Color c, float factor) {
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
static void inset_outset_side_colors(Color base, CssEnum style,
    Color* out_top, Color* out_right, Color* out_bottom, Color* out_left) {
    Color dark  = color_darken(base, BORDER_DARKEN_FACTOR);
    Color light = color_lighten(base, BORDER_LIGHTEN_FACTOR);
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

static RdtPath* render_border_create_centered_stroke_path(BorderProp* border,
                                                          Rect rect,
                                                          float width) {
    float half_w = width / 2.0f;
    Rect stroke_rect = {rect.x + half_w, rect.y + half_w,
                        rect.width - width, rect.height - width};
    Corner stroke_radius = radiant_corner_inset(&border->radius, half_w, half_w);
    return render_path_create_rounded_rect(stroke_rect, &stroke_radius);
}

/**
 * Render an inset/outset border as two trapezoid-filled polygons.
 */
static void render_inset_outset_trapezoid(RenderContext* rdcon, Rect rect,
    float bw_top, float bw_right, float bw_bottom, float bw_left,
    Color tl_color, Color br_color) {

    const RdtMatrix* xform = render_state_current_transform(rdcon);
    float x = rect.x, y = rect.y, W = rect.width, H = rect.height;

    RdtPath* clip = render_path_create_clip_path(rdcon);
    rc_push_clip(rdcon, clip, NULL);

    // top-left polygon: covers top side + left side
    RdtPath* tl_path = rdt_path_new();
    rdt_path_move_to(tl_path, x, y);
    rdt_path_line_to(tl_path, x + W, y);
    rdt_path_line_to(tl_path, x + W - bw_right, y + bw_top);
    rdt_path_line_to(tl_path, x + bw_left, y + bw_top);
    rdt_path_line_to(tl_path, x + bw_left, y + H - bw_bottom);
    rdt_path_line_to(tl_path, x, y + H);
    rdt_path_close(tl_path);
    rc_fill_path(rdcon, tl_path, tl_color, RDT_FILL_WINDING, xform);
    rdt_path_free(tl_path);

    // bottom-right polygon: covers bottom side + right side
    RdtPath* br_path = rdt_path_new();
    rdt_path_move_to(br_path, x + W, y);
    rdt_path_line_to(br_path, x + W, y + H);
    rdt_path_line_to(br_path, x, y + H);
    rdt_path_line_to(br_path, x + bw_left, y + H - bw_bottom);
    rdt_path_line_to(br_path, x + W - bw_right, y + H - bw_bottom);
    rdt_path_line_to(br_path, x + W - bw_right, y + bw_top);
    rdt_path_close(br_path);
    rc_fill_path(rdcon, br_path, br_color, RDT_FILL_WINDING, xform);
    rdt_path_free(br_path);

    rc_pop_clip(rdcon);
    rdt_path_free(clip);

    log_debug("[BORDER] inset/outset trapezoid tl=#%02x%02x%02x br=#%02x%02x%02x",
              tl_color.r, tl_color.g, tl_color.b, br_color.r, br_color.g, br_color.b);
}

/**
 * Render each border side independently using trapezoid fills.
 */

/**
 * Get dash pattern for dotted/dashed borders. Returns dash count (0 if none).
 * Caller provides a float[2] array. Also sets the cap style.
 */
static int get_dash_pattern(CssEnum style, float width, float* out_dash, RdtStrokeCap* out_cap) {
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

static void render_per_side_borders(RenderContext* rdcon, Rect rect, BorderProp* border) {
    float x = rect.x, y = rect.y, W = rect.width, H = rect.height;
    float bwt = border->width.top, bwr = border->width.right;
    float bwb = border->width.bottom, bwl = border->width.left;

    // If border-radius is present, clip all per-side trapezoids to the outer rounded rect
    bool has_radius = corner_has_radius(&border->radius);
    RdtPath* radius_clip = nullptr;
    if (has_radius) {
        radius_clip = render_path_create_rounded_rect(rect, &border->radius);
        rc_push_clip(rdcon, radius_clip, NULL);
    }

    // Helper lambda (as inline struct) for rendering one side's trapezoid with a color
    struct SideDraw {
        static void top(RenderContext* rdcon, Rect rect, float bwt, float bwr, float bwl, Color c) {
            if (bwt <= 0 || c.a == 0) return;
            const RdtMatrix* xform = render_state_current_transform(rdcon);
            RdtPath* clip = render_path_create_clip_path(rdcon);
            rc_push_clip(rdcon, clip, NULL);
            RdtPath* p = rdt_path_new();
            rdt_path_move_to(p, rect.x, rect.y);
            rdt_path_line_to(p, rect.x + rect.width, rect.y);
            rdt_path_line_to(p, rect.x + rect.width - bwr, rect.y + bwt);
            rdt_path_line_to(p, rect.x + bwl, rect.y + bwt);
            rdt_path_close(p);
            rc_fill_path(rdcon, p, c, RDT_FILL_WINDING, xform);
            rdt_path_free(p);
            rc_pop_clip(rdcon);
            rdt_path_free(clip);
        }
        static void bottom(RenderContext* rdcon, Rect rect, float bwb, float bwr, float bwl, Color c) {
            if (bwb <= 0 || c.a == 0) return;
            const RdtMatrix* xform = render_state_current_transform(rdcon);
            RdtPath* clip = render_path_create_clip_path(rdcon);
            rc_push_clip(rdcon, clip, NULL);
            RdtPath* p = rdt_path_new();
            float bot = rect.y + rect.height;
            rdt_path_move_to(p, rect.x + bwl, bot - bwb);
            rdt_path_line_to(p, rect.x + rect.width - bwr, bot - bwb);
            rdt_path_line_to(p, rect.x + rect.width, bot);
            rdt_path_line_to(p, rect.x, bot);
            rdt_path_close(p);
            rc_fill_path(rdcon, p, c, RDT_FILL_WINDING, xform);
            rdt_path_free(p);
            rc_pop_clip(rdcon);
            rdt_path_free(clip);
        }
        static void left(RenderContext* rdcon, Rect rect, float bwl, float bwt, float bwb, Color c) {
            if (bwl <= 0 || c.a == 0) return;
            const RdtMatrix* xform = render_state_current_transform(rdcon);
            RdtPath* clip = render_path_create_clip_path(rdcon);
            rc_push_clip(rdcon, clip, NULL);
            RdtPath* p = rdt_path_new();
            rdt_path_move_to(p, rect.x, rect.y);
            rdt_path_line_to(p, rect.x + bwl, rect.y + bwt);
            rdt_path_line_to(p, rect.x + bwl, rect.y + rect.height - bwb);
            rdt_path_line_to(p, rect.x, rect.y + rect.height);
            rdt_path_close(p);
            rc_fill_path(rdcon, p, c, RDT_FILL_WINDING, xform);
            rdt_path_free(p);
            rc_pop_clip(rdcon);
            rdt_path_free(clip);
        }
        static void right(RenderContext* rdcon, Rect rect, float bwr, float bwt, float bwb, Color c) {
            if (bwr <= 0 || c.a == 0) return;
            const RdtMatrix* xform = render_state_current_transform(rdcon);
            RdtPath* clip = render_path_create_clip_path(rdcon);
            rc_push_clip(rdcon, clip, NULL);
            RdtPath* p = rdt_path_new();
            float rg = rect.x + rect.width;
            rdt_path_move_to(p, rg - bwr, rect.y + bwt);
            rdt_path_line_to(p, rg, rect.y);
            rdt_path_line_to(p, rg, rect.y + rect.height);
            rdt_path_line_to(p, rg - bwr, rect.y + rect.height - bwb);
            rdt_path_close(p);
            rc_fill_path(rdcon, p, c, RDT_FILL_WINDING, xform);
            rdt_path_free(p);
            rc_pop_clip(rdcon);
            rdt_path_free(clip);
        }
    };

    // Render each side with its style
    // For double: render outer and inner thin sides; for groove/ridge: two half-sides;
    // for inset/outset: per-side computed color; for solid/dashed/dotted: standard fill

    struct SideInfo {
        CssEnum style;
        Color color;
        float width;
        int side; // 0=top, 1=right, 2=bottom, 3=left
    };

    SideInfo sides[4] = {
        {border->top_style,    border->top_color,    bwt, 0},
        {border->right_style,  border->right_color,  bwr, 1},
        {border->bottom_style, border->bottom_color, bwb, 2},
        {border->left_style,   border->left_color,   bwl, 3},
    };

    for (int i = 0; i < 4; i++) {
        CssEnum st = sides[i].style;
        Color c = sides[i].color;
        float w = sides[i].width;
        int side = sides[i].side;

        if (w <= 0 || st == CSS_VALUE_NONE || st == CSS_VALUE_HIDDEN || c.a == 0) continue;

        if (st == CSS_VALUE_DOUBLE && w >= 3) {
            // Two thin trapezoids with a gap
            float lw = floorf(w / 3.0f);
            if (lw < 1) lw = 1;
            // Outer pass (at border edge)
            float ow = lw, iw = lw, gap = w - 2 * lw;
            (void)gap;

            // Outer thin side
            switch (side) {
                case 0:
                    SideDraw::top(rdcon, rect, ow, bwr > 0 ? ow : 0, bwl > 0 ? ow : 0, c);
                    break;
                case 1: SideDraw::right(rdcon, rect, ow, bwt > 0 ? ow : 0, bwb > 0 ? ow : 0, c); break;
                case 2: SideDraw::bottom(rdcon, rect, ow, bwr > 0 ? ow : 0, bwl > 0 ? ow : 0, c); break;
                case 3: SideDraw::left(rdcon, rect, ow, bwt > 0 ? ow : 0, bwb > 0 ? ow : 0, c); break;
            }
            // Inner thin side (inset by w - iw)
            float inset = w - iw;
            Rect inner = {x + (side == 3 ? inset : 0), y + (side == 0 ? inset : 0),
                          W - (side == 1 || side == 3 ? inset : 0) * 2,
                          H - (side == 0 || side == 2 ? inset : 0) * 2};
            if (side == 1) { inner.x = x; inner.width = W - inset; }
            if (side == 2) { inner.y = y; inner.height = H - inset; }
            if (inner.width > 0 && inner.height > 0) {
                switch (side) {
                    case 0: SideDraw::top(rdcon, inner, iw, bwr > 0 ? iw : 0, bwl > 0 ? iw : 0, c); break;
                    case 1: SideDraw::right(rdcon, inner, iw, bwt > 0 ? iw : 0, bwb > 0 ? iw : 0, c); break;
                    case 2: SideDraw::bottom(rdcon, inner, iw, bwr > 0 ? iw : 0, bwl > 0 ? iw : 0, c); break;
                    case 3: SideDraw::left(rdcon, inner, iw, bwt > 0 ? iw : 0, bwb > 0 ? iw : 0, c); break;
                }
            }

        } else if (st == CSS_VALUE_GROOVE || st == CSS_VALUE_RIDGE) {
            float hw = w / 2.0f;
            // Chrome groove: dark = color × 0.5, light = original color (unchanged)
            Color dark = color_darken(c, 0.5f);
            // CSS groove: top/left outer=dark, inner=original; bottom/right outer=original, inner=dark
            // CSS ridge: opposite of groove
            bool is_top_left = (side == 0 || side == 3);
            Color outer_c, inner_c;
            if (st == CSS_VALUE_GROOVE) {
                outer_c = is_top_left ? dark : c;
                inner_c = is_top_left ? c : dark;
            } else {
                outer_c = is_top_left ? c : dark;
                inner_c = is_top_left ? dark : c;
            }
            // Outer half
            switch (side) {
                case 0: SideDraw::top(rdcon, rect, hw, bwr > 0 ? hw : 0, bwl > 0 ? hw : 0, outer_c); break;
                case 1: SideDraw::right(rdcon, rect, hw, bwt > 0 ? hw : 0, bwb > 0 ? hw : 0, outer_c); break;
                case 2: SideDraw::bottom(rdcon, rect, hw, bwr > 0 ? hw : 0, bwl > 0 ? hw : 0, outer_c); break;
                case 3: SideDraw::left(rdcon, rect, hw, bwt > 0 ? hw : 0, bwb > 0 ? hw : 0, outer_c); break;
            }
            // Inner half — inset by hw
            Rect inner = {x + (side == 3 ? hw : 0), y + (side == 0 ? hw : 0),
                          W, H};
            if (side == 1) { inner.width = W - hw; }
            else if (side == 3) { inner.width = W - hw; }
            if (side == 2) { inner.height = H - hw; }
            else if (side == 0) { inner.height = H - hw; }
            if (inner.width > 0 && inner.height > 0) {
                switch (side) {
                    case 0: SideDraw::top(rdcon, inner, hw, bwr > 0 ? hw : 0, bwl > 0 ? hw : 0, inner_c); break;
                    case 1: SideDraw::right(rdcon, inner, hw, bwt > 0 ? hw : 0, bwb > 0 ? hw : 0, inner_c); break;
                    case 2: SideDraw::bottom(rdcon, inner, hw, bwr > 0 ? hw : 0, bwl > 0 ? hw : 0, inner_c); break;
                    case 3: SideDraw::left(rdcon, inner, hw, bwt > 0 ? hw : 0, bwb > 0 ? hw : 0, inner_c); break;
                }
            }

        } else if (st == CSS_VALUE_INSET || st == CSS_VALUE_OUTSET) {
            // inset: top/left dark, bottom/right light
            // outset: top/left light, bottom/right dark
            Color dark  = color_darken(c, BORDER_DARKEN_FACTOR);
            Color light = color_lighten(c, BORDER_LIGHTEN_FACTOR);
            Color side_c;
            if (st == CSS_VALUE_INSET)
                side_c = (side == 0 || side == 3) ? dark : light;
            else
                side_c = (side == 0 || side == 3) ? light : dark;
            switch (side) {
                case 0: SideDraw::top(rdcon, rect, w, bwr, bwl, side_c); break;
                case 1: SideDraw::right(rdcon, rect, w, bwt, bwb, side_c); break;
                case 2: SideDraw::bottom(rdcon, rect, w, bwr, bwl, side_c); break;
                case 3: SideDraw::left(rdcon, rect, w, bwt, bwb, side_c); break;
            }

        } else if (st == CSS_VALUE_DASHED || st == CSS_VALUE_DOTTED) {
            // Dashed/dotted: stroke a line along the center of each side
            float dash[2];
            RdtStrokeCap cap;
            int dash_count = get_dash_pattern(st, w, dash, &cap);
            float half_w = w / 2.0f;
            const RdtMatrix* xform = render_state_current_transform(rdcon);

            // For non-radiused boxes, adjust gap so dashes appear at both ends
            // of the side and use phase=0 (matches browser per-side rendering).
            // For radiused boxes, keep the original phase=half_w.
            float phase = half_w;
            if (!has_radius && dash_count == 2 && st == CSS_VALUE_DASHED) {
                float side_len = (side == 0 || side == 2) ? W : H;
                if (side_len > 0) {
                    float base_dash = dash[0];
                    float period = dash[0] + dash[1];
                    int n_dashes = (int)roundf(side_len / period);
                    if (n_dashes < 1) n_dashes = 1;
                    if (n_dashes > 1) {
                        float adj_gap = (side_len - n_dashes * base_dash) / (float)(n_dashes - 1);
                        if (adj_gap > 0) dash[1] = adj_gap;
                    }
                }
                phase = 0;
            }

            RdtPath* clip = render_path_create_clip_path(rdcon);
            rc_push_clip(rdcon, clip, NULL);

            RdtPath* p = rdt_path_new();
            switch (side) {
                case 0: // top
                    rdt_path_move_to(p, x, y + half_w);
                    rdt_path_line_to(p, x + W, y + half_w);
                    break;
                case 1: // right
                    rdt_path_move_to(p, x + W - half_w, y);
                    rdt_path_line_to(p, x + W - half_w, y + H);
                    break;
                case 2: // bottom
                    rdt_path_move_to(p, x, y + H - half_w);
                    rdt_path_line_to(p, x + W, y + H - half_w);
                    break;
                case 3: // left
                    rdt_path_move_to(p, x + half_w, y);
                    rdt_path_line_to(p, x + half_w, y + H);
                    break;
            }
            rc_stroke_path(rdcon, p, c, w, cap, RDT_JOIN_MITER,
                            dash, dash_count, xform, phase);
            rdt_path_free(p);

            rc_pop_clip(rdcon);
            rdt_path_free(clip);

        } else {
            // solid — render as filled trapezoid
            switch (side) {
                case 0: SideDraw::top(rdcon, rect, w, bwr, bwl, c); break;
                case 1: SideDraw::right(rdcon, rect, w, bwt, bwb, c); break;
                case 2: SideDraw::bottom(rdcon, rect, w, bwr, bwl, c); break;
                case 3: SideDraw::left(rdcon, rect, w, bwt, bwb, c); break;
            }
        }
    }

    if (radius_clip) {
        rc_pop_clip(rdcon);
        rdt_path_free(radius_clip);
    }
}

/**
 * Constrain border radii to prevent overlapping per CSS Backgrounds Level 3 §5.5
 */
void constrain_corner_radii(Corner* radius, float width, float height) {
    if (!radius) return;

    float horizontal_sum_top = radius->horizontal[0] + radius->horizontal[1];
    float horizontal_sum_bottom = radius->horizontal[3] + radius->horizontal[2];
    float vertical_sum_left = radius->vertical[0] + radius->vertical[3];
    float vertical_sum_right = radius->vertical[1] + radius->vertical[2];

    float f = 1.0f;
    if (horizontal_sum_top > width) f = min(f, width / horizontal_sum_top);
    if (horizontal_sum_bottom > width) f = min(f, width / horizontal_sum_bottom);
    if (vertical_sum_left > height) f = min(f, height / vertical_sum_left);
    if (vertical_sum_right > height) f = min(f, height / vertical_sum_right);

    if (f < 1.0f) {
        log_debug("[BORDER RADIUS] Constraining radii by factor %.2f", f);
        for (int i = 0; i < 4; i++) {
            radius->horizontal[i] *= f;
            radius->vertical[i] *= f;
        }
    }
}

void constrain_border_radii(BorderProp* border, float width, float height) {
    if (!border) return;
    constrain_corner_radii(&border->radius, width, height);
}

/**
 * Resolve percentage border-radius values to pixels.
 * CSS Backgrounds 3 §5.3: percentages resolve against element dimensions.
 */
void resolve_border_radius_percentages(Corner* radius, float width, float height) {
    if (!radius) return;
    for (int i = 0; i < 4; i++) {
        if (radius->horizontal_percent[i]) {
            radius->horizontal[i] = radius->horizontal[i] * width / 100.0f;
            radius->horizontal_percent[i] = false;
        }
        if (radius->vertical_percent[i]) {
            radius->vertical[i] = radius->vertical[i] * height / 100.0f;
            radius->vertical_percent[i] = false;
        }
    }
}

bool corner_has_radius(const Corner* radius) {
    return radiant_corner_has_radius(radius);
}

static inline bool has_border_radius(BorderProp* border) {
    return border && corner_has_radius(&border->radius);
}

static inline bool needs_vector_rendering(CssEnum style) {
    return style == CSS_VALUE_DOTTED || style == CSS_VALUE_DASHED ||
           style == CSS_VALUE_DOUBLE || style == CSS_VALUE_GROOVE ||
           style == CSS_VALUE_RIDGE || style == CSS_VALUE_INSET ||
           style == CSS_VALUE_OUTSET;
}

/**
 * Main border rendering dispatch
 */
void render_border(RenderContext* rdcon, ViewBlock* view, Rect rect) {
    if (!view->bound || !view->boundary()->border) return;

    BorderProp* border = view->boundary()->border;
    float s = rdcon->raster_scale;

    Corner scaled_radius = radiant_corner_scaled(&border->radius, s);
    Corner orig_radius = border->radius;
    border->radius = scaled_radius;
    constrain_border_radii(border, rect.width, rect.height);

    bool has_radius = has_border_radius(border);
    bool non_uniform = false;
    bool needs_vector = has_radius;
    for (int i = 1; i < 4; i++) {
        non_uniform |= border->width.values[i] != border->width.values[0];
    }
    for (int i = 0; i < 4; i++) needs_vector |= needs_vector_rendering(border->styles[i]);
    needs_vector |= non_uniform;

    Spacing orig_width = border->width;
    for (int i = 0; i < 4; i++) border->width.values[i] *= s;

    if (render_border_image_gradient(rdcon, border, rect)) {
        border->width = orig_width;
        border->radius = orig_radius;
        return;
    }

    // Force vector path when CSS clip-path is active — the direct-pixel path
    // (render_straight_border) bypasses the ThorVG clip stack, so clip-path
    // shapes would not be applied to the borders.
    if (needs_vector || rdcon->has_transform || rdcon->clip_shape_depth > 0) {
        render_rounded_border(rdcon, view, rect);
    } else {
        render_straight_border(rdcon, view, rect);
    }
    
    border->width = orig_width;
    border->radius = orig_radius;
}

static bool render_border_image_gradient(RenderContext* rdcon, BorderProp* border, Rect rect) {
    if (!rdcon || !border ||
        border->border_image_type != GRADIENT_LINEAR ||
        !border->border_image_linear_gradient ||
        border->border_image_linear_gradient->stop_count < 2) {
        return false;
    }

    LinearGradient* gradient = border->border_image_linear_gradient;
    float edge_width = border->has_border_image_width
        ? border->border_image_width * rdcon->raster_scale : 0.0f;
    float top = border->has_border_image_width ? edge_width : border->width.values[0];
    float right = border->has_border_image_width ? edge_width : border->width.values[1];
    float bottom = border->has_border_image_width ? edge_width : border->width.values[2];
    float left = border->has_border_image_width ? edge_width : border->width.values[3];
    top = min(top, rect.height * 0.5f);
    bottom = min(bottom, rect.height * 0.5f);
    left = min(left, rect.width * 0.5f);
    right = min(right, rect.width * 0.5f);
    if (top <= 0.0f && right <= 0.0f && bottom <= 0.0f && left <= 0.0f) {
        return true;
    }

    RdtPath* ring = rdt_path_new();
    if (!ring) return false;
    rdt_path_add_rect(ring, rect.x, rect.y, rect.width, rect.height, 0.0f, 0.0f);
    rdt_path_add_rect(ring,
        rect.x + left, rect.y + top,
        max(0.0f, rect.width - left - right),
        max(0.0f, rect.height - top - bottom),
        0.0f, 0.0f);

    int stop_count = gradient->stop_count;
    RdtGradientStop* stops = LAMBDA_ALLOCA(stop_count, RdtGradientStop);
    for (int i = 0; i < stop_count; i++) {
        GradientStop* stop = &gradient->stops[i];
        float pos = stop->position >= 0.0f
            ? stop->position
            : (stop_count > 1 ? (float)i / (float)(stop_count - 1) : 0.0f);
        stops[i] = {pos, stop->color.r, stop->color.g, stop->color.b, stop->color.a};
    }

    RadiantGradientLine line = radiant_linear_gradient_line(rect, gradient->angle);

    RdtPath* clip = render_path_create_clip_path(rdcon);
    rc_push_clip(rdcon, clip, NULL);
    rc_fill_linear_gradient(rdcon, ring,
        line.x1, line.y1, line.x2, line.y2,
        stops, stop_count, RDT_FILL_EVEN_ODD,
        render_state_current_transform(rdcon));
    rc_pop_clip(rdcon);
    rdt_path_free(clip);
    rdt_path_free(ring);
    log_debug("[BORDER IMAGE] rendered linear-gradient border-image with %d stops", stop_count);
    return true;
}

/**
 * Render straight borders (optimized path for rectangular borders)
 */
static void render_straight_border(RenderContext* rdcon, ViewBlock* view, Rect rect) {
    BorderProp* border = view->boundary()->border;
    ImageSurface* surface = rdcon->ui_context->surface;

    const float* widths = border->width.values;
    const float side_x[4] = {rect.x, rect.x + rect.width - widths[1], rect.x,
                             rect.x};
    const float side_y[4] = {rect.y, rect.y, rect.y + rect.height - widths[2], rect.y};
    const float side_w[4] = {rect.width, widths[1], rect.width, widths[3]};
    const float side_h[4] = {widths[0], rect.height, widths[2], rect.height};
    for (int i = 0; i < 4; i++) {
        if (widths[i] <= 0 || border->styles[i] == CSS_VALUE_NONE ||
            border->styles[i] == CSS_VALUE_HIDDEN || border->colors[i].a <= 0) continue;
        Rect border_rect = {side_x[i], side_y[i], side_w[i], side_h[i]};
        render_painter_fill_surface_rect(rdcon, surface, &border_rect,
            border->colors[i].c, &rdcon->block.clip, rdcon->clip_shapes,
            rdcon->clip_shape_depth);
    }
}

/**
 * Render border with vector rendering (supports rounded corners and styled borders)
 */
static void render_rounded_border(RenderContext* rdcon, ViewBlock* view, Rect rect) {
    BorderProp* border = view->boundary()->border;
    const RdtMatrix* xform = render_state_current_transform(rdcon);

    // For uniform borders, we can render as a single shape
    bool uniform_width = true, uniform_style = true, uniform_color = true;
    for (int i = 1; i < 4; i++) {
        uniform_width &= border->width.values[i] == border->width.values[0];
        uniform_style &= border->styles[i] == border->styles[0];
        uniform_color &= border->colors[i].c == border->colors[0].c;
    }

    // Groove/ridge need per-side color variation (top/left vs bottom/right),
    // so they must always use per-side rendering even when uniform.
    // Dashed/dotted without border-radius also use per-side rendering so that
    // each side gets an independently adjusted dash pattern (matches browsers).
    bool has_radius = corner_has_radius(&border->radius);
    bool needs_per_side = (border->styles[0] == CSS_VALUE_GROOVE ||
                           border->styles[0] == CSS_VALUE_RIDGE ||
                           (!has_radius && (border->styles[0] == CSS_VALUE_DASHED ||
                                            border->styles[0] == CSS_VALUE_DOTTED)));

    if (uniform_width && uniform_style && uniform_color && !needs_per_side &&
        border->width.top > 0 &&
        border->top_style != CSS_VALUE_NONE && border->top_style != CSS_VALUE_HIDDEN) {

        CssEnum style = border->top_style;
        float w = border->width.top;
        Color c = border->top_color;

        RdtPath* clip = render_path_create_clip_path(rdcon);
        rc_push_clip(rdcon, clip, NULL);

        // Stroke-based border rendering: the path must be inset by half the stroke
        // width from the border-box outer edge so that the stroke (which extends
        // equally in both directions from the path) fills exactly the border area.
        // CSS borders occupy [outer_edge, outer_edge + border_width] inward.

        if (style == CSS_VALUE_DOUBLE && w >= 3) {
            float line_w = floorf(w / 3.0f);
            if (line_w < 1) line_w = 1;
            float half_lw = line_w / 2.0f;

            // Outer border: path centered at half_lw from outer edge
            Corner orig_r = border->radius;
            Rect outer_rect = {rect.x + half_lw, rect.y + half_lw,
                               rect.width - line_w, rect.height - line_w};
            Corner outer_radius = radiant_corner_inset(&orig_r, half_lw, half_lw);
            RdtPath* outer = render_path_create_rounded_rect(outer_rect, &outer_radius);
            rc_stroke_path(rdcon, outer, c, line_w, RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, xform);
            rdt_path_free(outer);

            // Inner border: path centered at (w - half_lw) from outer edge
            float inner_inset = w - half_lw;
            Rect inner_rect = {rect.x + inner_inset, rect.y + inner_inset,
                               rect.width - inner_inset * 2, rect.height - inner_inset * 2};
            Corner inner_radius = radiant_corner_inset(&orig_r, inner_inset, inner_inset);
            RdtPath* inner = render_path_create_rounded_rect(inner_rect, &inner_radius);
            rc_stroke_path(rdcon, inner, c, line_w, RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, xform);
            rdt_path_free(inner);

        } else if (style == CSS_VALUE_GROOVE || style == CSS_VALUE_RIDGE) {
            float half_w = w / 2.0f;
            float quarter_w = w / 4.0f;

            Color dark_c  = color_darken(c, BORDER_DARKEN_FACTOR);
            Color light_c = color_lighten(c, BORDER_LIGHTEN_FACTOR);

            bool groove = (style == CSS_VALUE_GROOVE);
            Color outer_c = groove ? dark_c : light_c;
            Color inner_c = groove ? light_c : dark_c;

            // Outer half: stroke width=half_w, path centered at quarter_w from outer edge
            Corner orig_r = border->radius;
            Rect outer_rect = {rect.x + quarter_w, rect.y + quarter_w,
                               rect.width - half_w, rect.height - half_w};
            Corner outer_radius = radiant_corner_inset(&orig_r, quarter_w, quarter_w);
            RdtPath* outer = render_path_create_rounded_rect(outer_rect, &outer_radius);
            rc_stroke_path(rdcon, outer, outer_c, half_w, RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, xform);
            rdt_path_free(outer);

            // Inner half: stroke width=half_w, path centered at 3*quarter_w from outer edge
            float inner_inset = quarter_w * 3.0f;
            Rect inner_rect = {rect.x + inner_inset, rect.y + inner_inset,
                               rect.width - inner_inset * 2, rect.height - inner_inset * 2};
            Corner inner_radius = radiant_corner_inset(&orig_r, inner_inset, inner_inset);
            RdtPath* inner = render_path_create_rounded_rect(inner_rect, &inner_radius);
            rc_stroke_path(rdcon, inner, inner_c, half_w, RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, xform);
            rdt_path_free(inner);

        } else if (style == CSS_VALUE_INSET || style == CSS_VALUE_OUTSET) {
            Color tl_color, br_color;
            inset_outset_side_colors(c, style, &tl_color, NULL, &br_color, NULL);

            bool has_radius = corner_has_radius(&border->radius);

            if (!has_radius) {
                rc_pop_clip(rdcon);
                rdt_path_free(clip);
                render_inset_outset_trapezoid(rdcon, rect, w, w, w, w, tl_color, br_color);
                return;
            } else {
                RdtPath* shape = render_border_create_centered_stroke_path(border, rect, w);
                rc_stroke_path(rdcon, shape, tl_color, w, RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, xform);
                rdt_path_free(shape);
            }

        } else {
            // Default: solid, dotted, dashed
            RdtPath* shape = render_border_create_centered_stroke_path(border, rect, w);
            float half_w = w / 2.0f;
            float dash[2];
            RdtStrokeCap cap;
            int dash_count = get_dash_pattern(style, w, dash, &cap);
            // Dash phase = w/2 to align dashes with border-box outer edge
            // (the path is inset by w/2, so we advance the pattern by w/2)
            float phase = (dash_count > 0) ? half_w : 0;
            rc_stroke_path(rdcon, shape, c, w, cap, RDT_JOIN_MITER,
                            dash_count > 0 ? dash : NULL, dash_count, xform, phase);
            rdt_path_free(shape);
        }

        rc_pop_clip(rdcon);
        rdt_path_free(clip);
    } else {
        // Non-uniform borders: render each side with its own style/color/width
        log_debug("[BORDER] Non-uniform border — rendering per-side with style variants");
        render_per_side_borders(rdcon, rect, border);
    }
}

/**
 * Render CSS outline (CSS UI Level 3)
 * Outline is drawn outside the border-box, offset by outline-offset.
 * Does not affect layout. Uses border-radius if present.
 */
void render_outline(RenderContext* rdcon, ViewBlock* view, Rect rect) {
    if (!view->bound || !view->boundary()->outline) return;

    OutlineProp* outline = view->boundary()->outline;
    if (outline->width <= 0 || outline->style == CSS_VALUE_NONE || outline->style == CSS_VALUE_HIDDEN) return;
    if (outline->color.a == 0) return;

    float s = rdcon->raster_scale;
    float w = outline->width * s;
    float offset = outline->offset * s;

    // Outline rect is expanded outward from border-box by (outline-width/2 + outline-offset)
    float expand = w * 0.5f + offset;
    Rect outline_rect;
    outline_rect.x = rect.x - expand;
    outline_rect.y = rect.y - expand;
    outline_rect.width = rect.width + expand * 2;
    outline_rect.height = rect.height + expand * 2;

    const RdtMatrix* xform = render_state_current_transform(rdcon);
    RdtPath* p = nullptr;

    // If border-radius exists, use rounded outline path
    bool has_radius = view->boundary_mut()->border && corner_has_radius(&view->boundary_mut()->border->radius);

    if (has_radius) {
        BorderProp* border = view->boundary()->border;
        Corner scaled_radius = radiant_corner_scaled(&border->radius, s);
        Corner outline_radius = radiant_corner_expand(&scaled_radius, expand, expand);
        constrain_corner_radii(&outline_radius, outline_rect.width, outline_rect.height);
        p = render_path_create_rounded_rect(outline_rect, &outline_radius);
    } else {
        p = rdt_path_new();
        rdt_path_add_rect(p, outline_rect.x, outline_rect.y,
            outline_rect.width, outline_rect.height, 0, 0);
    }

    float dash[2];
    RdtStrokeCap cap;
    int dash_count = get_dash_pattern(outline->style, w, dash, &cap);

    // For outlines, adjust dash/gap to match browser proportions.
    // Browser uses approximately dash=2.5w, gap=2w for dashed outlines.
    // (Borders use per-side rendering with adjusted gaps, but outlines stroke
    // the full perimeter as a single path.)
    if (dash_count > 0 && outline->style == CSS_VALUE_DASHED) {
        dash[0] = w * 2.5f;
        dash[1] = w * 2;
    }

    RdtPath* clip = render_path_create_clip_path(rdcon);
    rc_push_clip(rdcon, clip, NULL);
    float phase = (dash_count > 0) ? w * 0.75f : 0;
    rc_stroke_path(rdcon, p, outline->color, w, cap, RDT_JOIN_MITER,
                    dash_count > 0 ? dash : NULL, dash_count, xform, phase);
    rc_pop_clip(rdcon);
    rdt_path_free(clip);
    rdt_path_free(p);

    log_debug("[OUTLINE] Rendered outline: width=%.1f offset=%.1f style=%d color=#%02x%02x%02x%02x",
              outline->width, outline->offset, outline->style,
              outline->color.r, outline->color.g, outline->color.b, outline->color.a);
}
