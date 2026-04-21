#include "render_border.hpp"
#include "../lib/log.h"
#include <math.h>
#include <string.h>

// Bezier control point constant for circular arc approximation
// (4/3) * tan(π/8) ≈ 0.5522847498
#define KAPPA 0.5522847498f

// ---------------------------------------------------------------------------
// Color helpers — Chrome-compatible 3D border color computation
// Chrome: DarkenColor() multiplies RGB by 2/3, LightenColor() blends 1/3 toward white
// ---------------------------------------------------------------------------

static constexpr float BORDER_DARKEN_FACTOR  = 2.0f / 3.0f;
static constexpr float BORDER_LIGHTEN_FACTOR = 1.0f / 3.0f;

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

// Get transform pointer for rdt_* calls (NULL if identity)
static const RdtMatrix* get_transform(RenderContext* rdcon) {
    if (!rdcon->has_transform) return nullptr;
    return &rdcon->transform;
}

static Corner corner_scaled(const Corner* radius, float scale) {
    Corner out = *radius;
    out.top_left *= scale;
    out.top_right *= scale;
    out.bottom_right *= scale;
    out.bottom_left *= scale;
    out.top_left_y *= scale;
    out.top_right_y *= scale;
    out.bottom_right_y *= scale;
    out.bottom_left_y *= scale;
    return out;
}

static Corner corner_inset(const Corner* radius, float inset_x, float inset_y) {
    Corner out = *radius;
    out.top_left = max(0.0f, out.top_left - inset_x);
    out.top_right = max(0.0f, out.top_right - inset_x);
    out.bottom_right = max(0.0f, out.bottom_right - inset_x);
    out.bottom_left = max(0.0f, out.bottom_left - inset_x);
    out.top_left_y = max(0.0f, out.top_left_y - inset_y);
    out.top_right_y = max(0.0f, out.top_right_y - inset_y);
    out.bottom_right_y = max(0.0f, out.bottom_right_y - inset_y);
    out.bottom_left_y = max(0.0f, out.bottom_left_y - inset_y);
    return out;
}

static Corner corner_expand(const Corner* radius, float expand_x, float expand_y) {
    Corner out = *radius;
    out.top_left = max(0.0f, out.top_left + expand_x);
    out.top_right = max(0.0f, out.top_right + expand_x);
    out.bottom_right = max(0.0f, out.bottom_right + expand_x);
    out.bottom_left = max(0.0f, out.bottom_left + expand_x);
    out.top_left_y = max(0.0f, out.top_left_y + expand_y);
    out.top_right_y = max(0.0f, out.top_right_y + expand_y);
    out.bottom_right_y = max(0.0f, out.bottom_right_y + expand_y);
    out.bottom_left_y = max(0.0f, out.bottom_left_y + expand_y);
    return out;
}

// Create a clip path from the render context's clip region
static RdtPath* create_border_clip_path(RenderContext* rdcon) {
    if (rdcon->block.has_clip_radius) {
        float clip_x = rdcon->block.clip.left;
        float clip_y = rdcon->block.clip.top;
        float clip_w = rdcon->block.clip.right - rdcon->block.clip.left;
        float clip_h = rdcon->block.clip.bottom - rdcon->block.clip.top;
        Corner clip_radius = rdcon->block.clip_radius;
        constrain_corner_radii(&clip_radius, clip_w, clip_h);
        Rect clip_rect = {clip_x, clip_y, clip_w, clip_h};
        return build_rounded_rect_path(clip_rect, &clip_radius);
    }
    RdtPath* clip = rdt_path_new();
    rdt_path_add_rect(clip, rdcon->block.clip.left, rdcon->block.clip.top,
        rdcon->block.clip.right - rdcon->block.clip.left,
        rdcon->block.clip.bottom - rdcon->block.clip.top, 0, 0);
    return clip;
}

/**
 * Render an inset/outset border as two trapezoid-filled polygons.
 */
static void render_inset_outset_trapezoid(RenderContext* rdcon, Rect rect,
    float bw_top, float bw_right, float bw_bottom, float bw_left,
    Color tl_color, Color br_color) {

    RdtVector* vec = &rdcon->vec;
    const RdtMatrix* xform = get_transform(rdcon);
    float x = rect.x, y = rect.y, W = rect.width, H = rect.height;

    RdtPath* clip = create_border_clip_path(rdcon);
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

// Forward declaration (defined later in file)
static int get_dash_pattern(CssEnum style, float width, float* out_dash, RdtStrokeCap* out_cap);

static void render_per_side_borders(RenderContext* rdcon, Rect rect, BorderProp* border) {
    float x = rect.x, y = rect.y, W = rect.width, H = rect.height;
    float bwt = border->width.top, bwr = border->width.right;
    float bwb = border->width.bottom, bwl = border->width.left;

    RdtVector* vec = &rdcon->vec;
    const RdtMatrix* xform = get_transform(rdcon);

    // If border-radius is present, clip all per-side trapezoids to the outer rounded rect
    bool has_radius = corner_has_radius(&border->radius);
    RdtPath* radius_clip = nullptr;
    if (has_radius) {
        radius_clip = build_rounded_rect_path(rect, &border->radius);
        rc_push_clip(rdcon, radius_clip, NULL);
    }

    // Helper lambda (as inline struct) for rendering one side's trapezoid with a color
    struct SideDraw {
        static void top(RenderContext* rdcon, Rect rect, float bwt, float bwr, float bwl, Color c) {
            if (bwt <= 0 || c.a == 0) return;
            RdtVector* vec = &rdcon->vec;
            const RdtMatrix* xform = get_transform(rdcon);
            RdtPath* clip = create_border_clip_path(rdcon);
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
            RdtVector* vec = &rdcon->vec;
            const RdtMatrix* xform = get_transform(rdcon);
            RdtPath* clip = create_border_clip_path(rdcon);
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
            RdtVector* vec = &rdcon->vec;
            const RdtMatrix* xform = get_transform(rdcon);
            RdtPath* clip = create_border_clip_path(rdcon);
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
            RdtVector* vec = &rdcon->vec;
            const RdtMatrix* xform = get_transform(rdcon);
            RdtPath* clip = create_border_clip_path(rdcon);
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
            const RdtMatrix* xform = get_transform(rdcon);

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

            RdtPath* clip = create_border_clip_path(rdcon);
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

    float horizontal_sum_top = radius->top_left + radius->top_right;
    float horizontal_sum_bottom = radius->bottom_left + radius->bottom_right;
    float vertical_sum_left = radius->top_left_y + radius->bottom_left_y;
    float vertical_sum_right = radius->top_right_y + radius->bottom_right_y;

    float f = 1.0f;
    if (horizontal_sum_top > width) f = min(f, width / horizontal_sum_top);
    if (horizontal_sum_bottom > width) f = min(f, width / horizontal_sum_bottom);
    if (vertical_sum_left > height) f = min(f, height / vertical_sum_left);
    if (vertical_sum_right > height) f = min(f, height / vertical_sum_right);

    if (f < 1.0f) {
        log_debug("[BORDER RADIUS] Constraining radii by factor %.2f", f);
        radius->top_left *= f;
        radius->top_right *= f;
        radius->bottom_right *= f;
        radius->bottom_left *= f;
        radius->top_left_y *= f;
        radius->top_right_y *= f;
        radius->bottom_right_y *= f;
        radius->bottom_left_y *= f;
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
    if (radius->tl_percent) {
        radius->top_left = radius->top_left * width / 100.0f;
        radius->tl_percent = false;
    }
    if (radius->tr_percent) {
        radius->top_right = radius->top_right * width / 100.0f;
        radius->tr_percent = false;
    }
    if (radius->br_percent) {
        radius->bottom_right = radius->bottom_right * width / 100.0f;
        radius->br_percent = false;
    }
    if (radius->bl_percent) {
        radius->bottom_left = radius->bottom_left * width / 100.0f;
        radius->bl_percent = false;
    }
    if (radius->tl_percent_y) {
        radius->top_left_y = radius->top_left_y * height / 100.0f;
        radius->tl_percent_y = false;
    }
    if (radius->tr_percent_y) {
        radius->top_right_y = radius->top_right_y * height / 100.0f;
        radius->tr_percent_y = false;
    }
    if (radius->br_percent_y) {
        radius->bottom_right_y = radius->bottom_right_y * height / 100.0f;
        radius->br_percent_y = false;
    }
    if (radius->bl_percent_y) {
        radius->bottom_left_y = radius->bottom_left_y * height / 100.0f;
        radius->bl_percent_y = false;
    }
}

bool corner_has_radius(const Corner* radius) {
    return radius && (radius->top_left > 0 || radius->top_right > 0 ||
           radius->bottom_right > 0 || radius->bottom_left > 0 ||
           radius->top_left_y > 0 || radius->top_right_y > 0 ||
           radius->bottom_right_y > 0 || radius->bottom_left_y > 0);
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
    if (!view->bound || !view->bound->border) return;

    BorderProp* border = view->bound->border;
    float s = rdcon->scale;

    Corner scaled_radius = corner_scaled(&border->radius, s);
    Corner orig_radius = border->radius;
    border->radius = scaled_radius;
    constrain_border_radii(border, rect.width, rect.height);

    bool has_radius = has_border_radius(border);
    bool non_uniform = (border->width.top != border->width.right ||
                        border->width.right != border->width.bottom ||
                        border->width.bottom != border->width.left);
    bool needs_vector = has_radius || non_uniform ||
                        needs_vector_rendering(border->top_style) ||
                        needs_vector_rendering(border->right_style) ||
                        needs_vector_rendering(border->bottom_style) ||
                        needs_vector_rendering(border->left_style);

    Spacing orig_width = border->width;
    border->width.top *= s;
    border->width.right *= s;
    border->width.bottom *= s;
    border->width.left *= s;

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

/**
 * Render straight borders (optimized path for rectangular borders)
 */
void render_straight_border(RenderContext* rdcon, ViewBlock* view, Rect rect) {
    BorderProp* border = view->bound->border;
    ImageSurface* surface = rdcon->ui_context->surface;

    if (border->width.left > 0 && border->left_style != CSS_VALUE_NONE &&
        border->left_style != CSS_VALUE_HIDDEN && border->left_color.a > 0) {
        Rect border_rect = {rect.x, rect.y, border->width.left, rect.height};
        rc_fill_surface_rect(rdcon, surface, &border_rect, border->left_color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
    }

    if (border->width.right > 0 && border->right_style != CSS_VALUE_NONE &&
        border->right_style != CSS_VALUE_HIDDEN && border->right_color.a > 0) {
        Rect border_rect = {
            rect.x + rect.width - border->width.right, rect.y,
            border->width.right, rect.height
        };
        rc_fill_surface_rect(rdcon, surface, &border_rect, border->right_color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
    }

    if (border->width.top > 0 && border->top_style != CSS_VALUE_NONE &&
        border->top_style != CSS_VALUE_HIDDEN && border->top_color.a > 0) {
        Rect border_rect = {rect.x, rect.y, rect.width, border->width.top};
        rc_fill_surface_rect(rdcon, surface, &border_rect, border->top_color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
    }

    if (border->width.bottom > 0 && border->bottom_style != CSS_VALUE_NONE &&
        border->bottom_style != CSS_VALUE_HIDDEN && border->bottom_color.a > 0) {
        Rect border_rect = {
            rect.x, rect.y + rect.height - border->width.bottom,
            rect.width, border->width.bottom
        };
        rc_fill_surface_rect(rdcon, surface, &border_rect, border->bottom_color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
    }
}

/**
 * Build rounded rectangle path with per-corner elliptical radii.
 */
RdtPath* build_rounded_rect_path(Rect rect, const Corner* radius) {
    RdtPath* p = rdt_path_new();

    float x = rect.x;
    float y = rect.y;
    float w = rect.width;
    float h = rect.height;

    float rx_tl = radius->top_left;
    float rx_tr = radius->top_right;
    float rx_br = radius->bottom_right;
    float rx_bl = radius->bottom_left;
    float ry_tl = radius->top_left_y;
    float ry_tr = radius->top_right_y;
    float ry_br = radius->bottom_right_y;
    float ry_bl = radius->bottom_left_y;

    // Start from top-left corner (after the radius)
    rdt_path_move_to(p, x + rx_tl, y);

    // Top edge
    rdt_path_line_to(p, x + w - rx_tr, y);

    // Top-right corner (Bezier curve)
    if (rx_tr > 0 || ry_tr > 0) {
        rdt_path_cubic_to(p,
            x + w - rx_tr + rx_tr * KAPPA, y,
            x + w, y + ry_tr - ry_tr * KAPPA,
            x + w, y + ry_tr);
    }

    // Right edge
    rdt_path_line_to(p, x + w, y + h - ry_br);

    // Bottom-right corner (Bezier curve)
    if (rx_br > 0 || ry_br > 0) {
        rdt_path_cubic_to(p,
            x + w, y + h - ry_br + ry_br * KAPPA,
            x + w - rx_br + rx_br * KAPPA, y + h,
            x + w - rx_br, y + h);
    }

    // Bottom edge
    rdt_path_line_to(p, x + rx_bl, y + h);

    // Bottom-left corner (Bezier curve)
    if (rx_bl > 0 || ry_bl > 0) {
        rdt_path_cubic_to(p,
            x + rx_bl - rx_bl * KAPPA, y + h,
            x, y + h - ry_bl + ry_bl * KAPPA,
            x, y + h - ry_bl);
    }

    // Left edge
    rdt_path_line_to(p, x, y + ry_tl);

    // Top-left corner (Bezier curve)
    if (rx_tl > 0 || ry_tl > 0) {
        rdt_path_cubic_to(p,
            x, y + ry_tl - ry_tl * KAPPA,
            x + rx_tl - rx_tl * KAPPA, y,
            x + rx_tl, y);
    }

    rdt_path_close(p);
    return p;
}

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

/**
 * Render border with vector rendering (supports rounded corners and styled borders)
 */
void render_rounded_border(RenderContext* rdcon, ViewBlock* view, Rect rect) {
    BorderProp* border = view->bound->border;
    RdtVector* vec = &rdcon->vec;
    const RdtMatrix* xform = get_transform(rdcon);

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

    // Groove/ridge need per-side color variation (top/left vs bottom/right),
    // so they must always use per-side rendering even when uniform.
    // Dashed/dotted without border-radius also use per-side rendering so that
    // each side gets an independently adjusted dash pattern (matches browsers).
    bool has_radius = corner_has_radius(&border->radius);
    bool needs_per_side = (border->top_style == CSS_VALUE_GROOVE ||
                           border->top_style == CSS_VALUE_RIDGE ||
                           (!has_radius && (border->top_style == CSS_VALUE_DASHED ||
                                            border->top_style == CSS_VALUE_DOTTED)));

    if (uniform_width && uniform_style && uniform_color && !needs_per_side &&
        border->width.top > 0 &&
        border->top_style != CSS_VALUE_NONE && border->top_style != CSS_VALUE_HIDDEN) {

        CssEnum style = border->top_style;
        float w = border->width.top;
        Color c = border->top_color;

        RdtPath* clip = create_border_clip_path(rdcon);
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
            Corner outer_radius = corner_inset(&orig_r, half_lw, half_lw);
            RdtPath* outer = build_rounded_rect_path(outer_rect, &outer_radius);
            rc_stroke_path(rdcon, outer, c, line_w, RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, xform);
            rdt_path_free(outer);

            // Inner border: path centered at (w - half_lw) from outer edge
            float inner_inset = w - half_lw;
            Rect inner_rect = {rect.x + inner_inset, rect.y + inner_inset,
                               rect.width - inner_inset * 2, rect.height - inner_inset * 2};
            Corner inner_radius = corner_inset(&orig_r, inner_inset, inner_inset);
            RdtPath* inner = build_rounded_rect_path(inner_rect, &inner_radius);
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
            Corner outer_radius = corner_inset(&orig_r, quarter_w, quarter_w);
            RdtPath* outer = build_rounded_rect_path(outer_rect, &outer_radius);
            rc_stroke_path(rdcon, outer, outer_c, half_w, RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, xform);
            rdt_path_free(outer);

            // Inner half: stroke width=half_w, path centered at 3*quarter_w from outer edge
            float inner_inset = quarter_w * 3.0f;
            Rect inner_rect = {rect.x + inner_inset, rect.y + inner_inset,
                               rect.width - inner_inset * 2, rect.height - inner_inset * 2};
            Corner inner_radius = corner_inset(&orig_r, inner_inset, inner_inset);
            RdtPath* inner = build_rounded_rect_path(inner_rect, &inner_radius);
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
                // Inset path by w/2 for centered stroke
                float half_w = w / 2.0f;
                Rect stroke_rect = {rect.x + half_w, rect.y + half_w,
                                    rect.width - w, rect.height - w};
                Corner orig_r = border->radius;
                Corner stroke_radius = corner_inset(&orig_r, half_w, half_w);
                RdtPath* shape = build_rounded_rect_path(stroke_rect, &stroke_radius);
                rc_stroke_path(rdcon, shape, tl_color, w, RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, xform);
                rdt_path_free(shape);
            }

        } else {
            // Default: solid, dotted, dashed
            // Inset path by w/2 for centered stroke
            float half_w = w / 2.0f;
            Rect stroke_rect = {rect.x + half_w, rect.y + half_w,
                                rect.width - w, rect.height - w};
            Corner orig_r = border->radius;
            Corner stroke_radius = corner_inset(&orig_r, half_w, half_w);
            RdtPath* shape = build_rounded_rect_path(stroke_rect, &stroke_radius);
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

    RdtVector* vec = &rdcon->vec;
    const RdtMatrix* xform = get_transform(rdcon);
    RdtPath* p = nullptr;

    // If border-radius exists, use rounded outline path
    bool has_radius = view->bound->border && corner_has_radius(&view->bound->border->radius);

    if (has_radius) {
        BorderProp* border = view->bound->border;
        Corner scaled_radius = corner_scaled(&border->radius, s);
        Corner outline_radius = corner_expand(&scaled_radius, expand, expand);
        constrain_corner_radii(&outline_radius, outline_rect.width, outline_rect.height);
        p = build_rounded_rect_path(outline_rect, &outline_radius);
    } else {
        p = rdt_path_new();
        rdt_path_add_rect(p, outline_rect.x, outline_rect.y,
            outline_rect.width, outline_rect.height, 0, 0);
    }

    float dash[2];
    RdtStrokeCap cap;
    int dash_count = get_dash_pattern(outline->style, w, dash, &cap);

    RdtPath* clip = create_border_clip_path(rdcon);
    rc_push_clip(rdcon, clip, NULL);
    rc_stroke_path(rdcon, p, outline->color, w, cap, RDT_JOIN_MITER,
                    dash_count > 0 ? dash : NULL, dash_count, xform);
    rc_pop_clip(rdcon);
    rdt_path_free(clip);
    rdt_path_free(p);

    log_debug("[OUTLINE] Rendered outline: width=%.1f offset=%.1f style=%d color=#%02x%02x%02x%02x",
              outline->width, outline->offset, outline->style,
              outline->color.r, outline->color.g, outline->color.b, outline->color.a);
}
