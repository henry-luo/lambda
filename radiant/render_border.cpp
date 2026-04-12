#include "render_border.hpp"
#include "../lib/log.h"
#include <math.h>
#include <string.h>

// Bezier control point constant for circular arc approximation
// (4/3) * tan(π/8) ≈ 0.5522847498
#define KAPPA 0.5522847498f

// ---------------------------------------------------------------------------
// Color helpers
// ---------------------------------------------------------------------------

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
    Color dark  = color_darken(base, 0.5f);
    Color light = color_lighten(base, 0.35f);
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

// Create a clip path from the render context's clip region
static RdtPath* create_border_clip_path(RenderContext* rdcon) {
    RdtPath* clip = rdt_path_new();
    if (rdcon->block.has_clip_radius) {
        float clip_x = rdcon->block.clip.left;
        float clip_y = rdcon->block.clip.top;
        float clip_w = rdcon->block.clip.right - rdcon->block.clip.left;
        float clip_h = rdcon->block.clip.bottom - rdcon->block.clip.top;
        float r = rdcon->block.clip_radius.top_left;
        if (rdcon->block.clip_radius.top_right > 0) r = max(r, rdcon->block.clip_radius.top_right);
        if (rdcon->block.clip_radius.bottom_left > 0) r = max(r, rdcon->block.clip_radius.bottom_left);
        if (rdcon->block.clip_radius.bottom_right > 0) r = max(r, rdcon->block.clip_radius.bottom_right);
        rdt_path_add_rect(clip, clip_x, clip_y, clip_w, clip_h, r, r);
    } else {
        rdt_path_add_rect(clip, rdcon->block.clip.left, rdcon->block.clip.top,
            rdcon->block.clip.right - rdcon->block.clip.left,
            rdcon->block.clip.bottom - rdcon->block.clip.top, 0, 0);
    }
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
    rdt_push_clip(vec, clip, NULL);

    // top-left polygon: covers top side + left side
    RdtPath* tl_path = rdt_path_new();
    rdt_path_move_to(tl_path, x, y);
    rdt_path_line_to(tl_path, x + W, y);
    rdt_path_line_to(tl_path, x + W - bw_right, y + bw_top);
    rdt_path_line_to(tl_path, x + bw_left, y + bw_top);
    rdt_path_line_to(tl_path, x + bw_left, y + H - bw_bottom);
    rdt_path_line_to(tl_path, x, y + H);
    rdt_path_close(tl_path);
    rdt_fill_path(vec, tl_path, tl_color, RDT_FILL_WINDING, xform);
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
    rdt_fill_path(vec, br_path, br_color, RDT_FILL_WINDING, xform);
    rdt_path_free(br_path);

    rdt_pop_clip(vec);
    rdt_path_free(clip);

    log_debug("[BORDER] inset/outset trapezoid tl=#%02x%02x%02x br=#%02x%02x%02x",
              tl_color.r, tl_color.g, tl_color.b, br_color.r, br_color.g, br_color.b);
}

/**
 * Render each border side independently using trapezoid fills.
 */
static void render_per_side_borders(RenderContext* rdcon, Rect rect, BorderProp* border) {
    float x = rect.x, y = rect.y, W = rect.width, H = rect.height;
    float bwt = border->width.top, bwr = border->width.right;
    float bwb = border->width.bottom, bwl = border->width.left;

    RdtVector* vec = &rdcon->vec;
    const RdtMatrix* xform = get_transform(rdcon);

    // Helper lambda (as inline struct) for rendering one side's trapezoid with a color
    struct SideDraw {
        static void top(RenderContext* rdcon, Rect rect, float bwt, float bwr, float bwl, Color c) {
            if (bwt <= 0 || c.a == 0) return;
            RdtVector* vec = &rdcon->vec;
            const RdtMatrix* xform = get_transform(rdcon);
            RdtPath* clip = create_border_clip_path(rdcon);
            rdt_push_clip(vec, clip, NULL);
            RdtPath* p = rdt_path_new();
            rdt_path_move_to(p, rect.x, rect.y);
            rdt_path_line_to(p, rect.x + rect.width, rect.y);
            rdt_path_line_to(p, rect.x + rect.width - bwr, rect.y + bwt);
            rdt_path_line_to(p, rect.x + bwl, rect.y + bwt);
            rdt_path_close(p);
            rdt_fill_path(vec, p, c, RDT_FILL_WINDING, xform);
            rdt_path_free(p);
            rdt_pop_clip(vec);
            rdt_path_free(clip);
        }
        static void bottom(RenderContext* rdcon, Rect rect, float bwb, float bwr, float bwl, Color c) {
            if (bwb <= 0 || c.a == 0) return;
            RdtVector* vec = &rdcon->vec;
            const RdtMatrix* xform = get_transform(rdcon);
            RdtPath* clip = create_border_clip_path(rdcon);
            rdt_push_clip(vec, clip, NULL);
            RdtPath* p = rdt_path_new();
            float bot = rect.y + rect.height;
            rdt_path_move_to(p, rect.x + bwl, bot - bwb);
            rdt_path_line_to(p, rect.x + rect.width - bwr, bot - bwb);
            rdt_path_line_to(p, rect.x + rect.width, bot);
            rdt_path_line_to(p, rect.x, bot);
            rdt_path_close(p);
            rdt_fill_path(vec, p, c, RDT_FILL_WINDING, xform);
            rdt_path_free(p);
            rdt_pop_clip(vec);
            rdt_path_free(clip);
        }
        static void left(RenderContext* rdcon, Rect rect, float bwl, float bwt, float bwb, Color c) {
            if (bwl <= 0 || c.a == 0) return;
            RdtVector* vec = &rdcon->vec;
            const RdtMatrix* xform = get_transform(rdcon);
            RdtPath* clip = create_border_clip_path(rdcon);
            rdt_push_clip(vec, clip, NULL);
            RdtPath* p = rdt_path_new();
            rdt_path_move_to(p, rect.x, rect.y);
            rdt_path_line_to(p, rect.x + bwl, rect.y + bwt);
            rdt_path_line_to(p, rect.x + bwl, rect.y + rect.height - bwb);
            rdt_path_line_to(p, rect.x, rect.y + rect.height);
            rdt_path_close(p);
            rdt_fill_path(vec, p, c, RDT_FILL_WINDING, xform);
            rdt_path_free(p);
            rdt_pop_clip(vec);
            rdt_path_free(clip);
        }
        static void right(RenderContext* rdcon, Rect rect, float bwr, float bwt, float bwb, Color c) {
            if (bwr <= 0 || c.a == 0) return;
            RdtVector* vec = &rdcon->vec;
            const RdtMatrix* xform = get_transform(rdcon);
            RdtPath* clip = create_border_clip_path(rdcon);
            rdt_push_clip(vec, clip, NULL);
            RdtPath* p = rdt_path_new();
            float rg = rect.x + rect.width;
            rdt_path_move_to(p, rg - bwr, rect.y + bwt);
            rdt_path_line_to(p, rg, rect.y);
            rdt_path_line_to(p, rg, rect.y + rect.height);
            rdt_path_line_to(p, rg - bwr, rect.y + rect.height - bwb);
            rdt_path_close(p);
            rdt_fill_path(vec, p, c, RDT_FILL_WINDING, xform);
            rdt_path_free(p);
            rdt_pop_clip(vec);
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
            Color dark  = color_darken(c, 0.5f);
            Color light = color_lighten(c, 0.35f);
            Color outer_c = (st == CSS_VALUE_GROOVE) ? dark : light;
            Color inner_c = (st == CSS_VALUE_GROOVE) ? light : dark;
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
            Color dark  = color_darken(c, 0.5f);
            Color light = color_lighten(c, 0.35f);
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

        } else {
            // solid / dashed / dotted — render as filled trapezoid (ignore dash for non-uniform)
            switch (side) {
                case 0: SideDraw::top(rdcon, rect, w, bwr, bwl, c); break;
                case 1: SideDraw::right(rdcon, rect, w, bwt, bwb, c); break;
                case 2: SideDraw::bottom(rdcon, rect, w, bwr, bwl, c); break;
                case 3: SideDraw::left(rdcon, rect, w, bwt, bwb, c); break;
            }
        }
    }
}

/**
 * Constrain border radii to prevent overlapping per CSS Backgrounds Level 3 §5.5
 */
void constrain_border_radii(BorderProp* border, float width, float height) {
    if (!border) return;

    float horizontal_sum_top = border->radius.top_left + border->radius.top_right;
    float horizontal_sum_bottom = border->radius.bottom_left + border->radius.bottom_right;
    float vertical_sum_left = border->radius.top_left + border->radius.bottom_left;
    float vertical_sum_right = border->radius.top_right + border->radius.bottom_right;

    float f = 1.0f;
    if (horizontal_sum_top > width) f = min(f, width / horizontal_sum_top);
    if (horizontal_sum_bottom > width) f = min(f, width / horizontal_sum_bottom);
    if (vertical_sum_left > height) f = min(f, height / vertical_sum_left);
    if (vertical_sum_right > height) f = min(f, height / vertical_sum_right);

    if (f < 1.0f) {
        log_debug("[BORDER RADIUS] Constraining radii by factor %.2f", f);
        border->radius.top_left *= f;
        border->radius.top_right *= f;
        border->radius.bottom_right *= f;
        border->radius.bottom_left *= f;
    }
}

/**
 * Resolve percentage border-radius values to pixels.
 * CSS Backgrounds 3 §5.3: percentages resolve against element dimensions.
 * Uses min(width, height) since we store a single radius per corner.
 */
void resolve_border_radius_percentages(Corner* radius, float width, float height) {
    float dim = min(width, height);
    if (radius->tl_percent) {
        radius->top_left = radius->top_left * dim / 100.0f;
        radius->tl_percent = false;
    }
    if (radius->tr_percent) {
        radius->top_right = radius->top_right * dim / 100.0f;
        radius->tr_percent = false;
    }
    if (radius->br_percent) {
        radius->bottom_right = radius->bottom_right * dim / 100.0f;
        radius->br_percent = false;
    }
    if (radius->bl_percent) {
        radius->bottom_left = radius->bottom_left * dim / 100.0f;
        radius->bl_percent = false;
    }
}

static inline bool has_border_radius(BorderProp* border) {
    return border->radius.top_left > 0 || border->radius.top_right > 0 ||
           border->radius.bottom_right > 0 || border->radius.bottom_left > 0;
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

    Corner scaled_radius = border->radius;
    scaled_radius.top_left *= s;
    scaled_radius.top_right *= s;
    scaled_radius.bottom_right *= s;
    scaled_radius.bottom_left *= s;
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

    if (needs_vector || rdcon->has_transform) {
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
        fill_surface_rect(surface, &border_rect, border->left_color.c, &rdcon->block.clip);
    }

    if (border->width.right > 0 && border->right_style != CSS_VALUE_NONE &&
        border->right_style != CSS_VALUE_HIDDEN && border->right_color.a > 0) {
        Rect border_rect = {
            rect.x + rect.width - border->width.right, rect.y,
            border->width.right, rect.height
        };
        fill_surface_rect(surface, &border_rect, border->right_color.c, &rdcon->block.clip);
    }

    if (border->width.top > 0 && border->top_style != CSS_VALUE_NONE &&
        border->top_style != CSS_VALUE_HIDDEN && border->top_color.a > 0) {
        Rect border_rect = {rect.x, rect.y, rect.width, border->width.top};
        fill_surface_rect(surface, &border_rect, border->top_color.c, &rdcon->block.clip);
    }

    if (border->width.bottom > 0 && border->bottom_style != CSS_VALUE_NONE &&
        border->bottom_style != CSS_VALUE_HIDDEN && border->bottom_color.a > 0) {
        Rect border_rect = {
            rect.x, rect.y + rect.height - border->width.bottom,
            rect.width, border->width.bottom
        };
        fill_surface_rect(surface, &border_rect, border->bottom_color.c, &rdcon->block.clip);
    }
}

/**
 * Build rounded rectangle path with Bezier curves
 */
static RdtPath* build_rounded_border_path(Rect rect, BorderProp* border) {
    RdtPath* p = rdt_path_new();

    float x = rect.x;
    float y = rect.y;
    float w = rect.width;
    float h = rect.height;

    float r_tl = border->radius.top_left;
    float r_tr = border->radius.top_right;
    float r_br = border->radius.bottom_right;
    float r_bl = border->radius.bottom_left;

    // Start from top-left corner (after the radius)
    rdt_path_move_to(p, x + r_tl, y);

    // Top edge
    rdt_path_line_to(p, x + w - r_tr, y);

    // Top-right corner (Bezier curve)
    if (r_tr > 0) {
        rdt_path_cubic_to(p,
            x + w - r_tr + r_tr * KAPPA, y,
            x + w, y + r_tr - r_tr * KAPPA,
            x + w, y + r_tr);
    }

    // Right edge
    rdt_path_line_to(p, x + w, y + h - r_br);

    // Bottom-right corner (Bezier curve)
    if (r_br > 0) {
        rdt_path_cubic_to(p,
            x + w, y + h - r_br + r_br * KAPPA,
            x + w - r_br + r_br * KAPPA, y + h,
            x + w - r_br, y + h);
    }

    // Bottom edge
    rdt_path_line_to(p, x + r_bl, y + h);

    // Bottom-left corner (Bezier curve)
    if (r_bl > 0) {
        rdt_path_cubic_to(p,
            x + r_bl - r_bl * KAPPA, y + h,
            x, y + h - r_bl + r_bl * KAPPA,
            x, y + h - r_bl);
    }

    // Left edge
    rdt_path_line_to(p, x, y + r_tl);

    // Top-left corner (Bezier curve)
    if (r_tl > 0) {
        rdt_path_cubic_to(p,
            x, y + r_tl - r_tl * KAPPA,
            x + r_tl - r_tl * KAPPA, y,
            x + r_tl, y);
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
        out_dash[0] = width;
        out_dash[1] = width * 2;
        *out_cap = RDT_CAP_ROUND;
        return 2;
    } else if (style == CSS_VALUE_DASHED) {
        out_dash[0] = width * 3;
        out_dash[1] = width * 3;
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

    if (uniform_width && uniform_style && uniform_color && border->width.top > 0 &&
        border->top_style != CSS_VALUE_NONE && border->top_style != CSS_VALUE_HIDDEN) {

        CssEnum style = border->top_style;
        float w = border->width.top;
        Color c = border->top_color;

        RdtPath* clip = create_border_clip_path(rdcon);
        rdt_push_clip(vec, clip, NULL);

        if (style == CSS_VALUE_DOUBLE && w >= 3) {
            float line_w = floorf(w / 3.0f);
            if (line_w < 1) line_w = 1;

            // Outer border
            RdtPath* outer = build_rounded_border_path(rect, border);
            rdt_stroke_path(vec, outer, c, line_w, RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, xform);
            rdt_path_free(outer);

            // Inner border (inset by w - line_w)
            float inset = w - line_w;
            Rect inner_rect = {rect.x + inset, rect.y + inset,
                               rect.width - inset * 2, rect.height - inset * 2};
            Corner orig_r = border->radius;
            border->radius.top_left = max(0.0f, orig_r.top_left - inset);
            border->radius.top_right = max(0.0f, orig_r.top_right - inset);
            border->radius.bottom_right = max(0.0f, orig_r.bottom_right - inset);
            border->radius.bottom_left = max(0.0f, orig_r.bottom_left - inset);
            RdtPath* inner = build_rounded_border_path(inner_rect, border);
            border->radius = orig_r;
            rdt_stroke_path(vec, inner, c, line_w, RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, xform);
            rdt_path_free(inner);

        } else if (style == CSS_VALUE_GROOVE || style == CSS_VALUE_RIDGE) {
            float half_w = w / 2.0f;

            uint8_t dark_r = (uint8_t)(c.r * 0.5f);
            uint8_t dark_g = (uint8_t)(c.g * 0.5f);
            uint8_t dark_b = (uint8_t)(c.b * 0.5f);
            uint8_t light_r = (uint8_t)min(255.0f, c.r * 1.5f);
            uint8_t light_g = (uint8_t)min(255.0f, c.g * 1.5f);
            uint8_t light_b = (uint8_t)min(255.0f, c.b * 1.5f);

            bool groove = (style == CSS_VALUE_GROOVE);
            Color outer_c, inner_c;
            if (groove) {
                outer_c.r = dark_r;  outer_c.g = dark_g;  outer_c.b = dark_b;  outer_c.a = c.a;
                inner_c.r = light_r; inner_c.g = light_g; inner_c.b = light_b; inner_c.a = c.a;
            } else {
                outer_c.r = light_r; outer_c.g = light_g; outer_c.b = light_b; outer_c.a = c.a;
                inner_c.r = dark_r;  inner_c.g = dark_g;  inner_c.b = dark_b;  inner_c.a = c.a;
            }

            // Outer half
            RdtPath* outer = build_rounded_border_path(rect, border);
            rdt_stroke_path(vec, outer, outer_c, half_w, RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, xform);
            rdt_path_free(outer);

            // Inner half
            float inset = half_w;
            Rect inner_rect = {rect.x + inset, rect.y + inset,
                               rect.width - inset * 2, rect.height - inset * 2};
            Corner orig_r = border->radius;
            border->radius.top_left = max(0.0f, orig_r.top_left - inset);
            border->radius.top_right = max(0.0f, orig_r.top_right - inset);
            border->radius.bottom_right = max(0.0f, orig_r.bottom_right - inset);
            border->radius.bottom_left = max(0.0f, orig_r.bottom_left - inset);
            RdtPath* inner = build_rounded_border_path(inner_rect, border);
            border->radius = orig_r;
            rdt_stroke_path(vec, inner, inner_c, half_w, RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, xform);
            rdt_path_free(inner);

        } else if (style == CSS_VALUE_INSET || style == CSS_VALUE_OUTSET) {
            Color tl_color, br_color;
            inset_outset_side_colors(c, style, &tl_color, NULL, &br_color, NULL);

            bool has_radius = (border->radius.top_left > 0 || border->radius.top_right > 0 ||
                               border->radius.bottom_right > 0 || border->radius.bottom_left > 0);

            if (!has_radius) {
                rdt_pop_clip(vec);
                rdt_path_free(clip);
                render_inset_outset_trapezoid(rdcon, rect, w, w, w, w, tl_color, br_color);
                return;
            } else {
                RdtPath* shape = build_rounded_border_path(rect, border);
                rdt_stroke_path(vec, shape, tl_color, w, RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, xform);
                rdt_path_free(shape);
            }

        } else {
            // Default: solid, dotted, dashed
            RdtPath* shape = build_rounded_border_path(rect, border);
            float dash[2];
            RdtStrokeCap cap;
            int dash_count = get_dash_pattern(style, w, dash, &cap);
            rdt_stroke_path(vec, shape, c, w, cap, RDT_JOIN_MITER,
                            dash_count > 0 ? dash : NULL, dash_count, xform);
            rdt_path_free(shape);
        }

        rdt_pop_clip(vec);
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
    RdtPath* p = rdt_path_new();

    // If border-radius exists, use rounded outline path
    bool has_radius = view->bound->border &&
        (view->bound->border->radius.top_left > 0 || view->bound->border->radius.top_right > 0 ||
         view->bound->border->radius.bottom_right > 0 || view->bound->border->radius.bottom_left > 0);

    if (has_radius) {
        BorderProp* border = view->bound->border;
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

        rdt_path_move_to(p, x + r_tl, y);
        rdt_path_line_to(p, x + ow - r_tr, y);
        if (r_tr > 0) {
            rdt_path_cubic_to(p,
                x + ow - r_tr + r_tr * KAPPA, y,
                x + ow, y + r_tr - r_tr * KAPPA,
                x + ow, y + r_tr);
        }
        rdt_path_line_to(p, x + ow, y + oh - r_br);
        if (r_br > 0) {
            rdt_path_cubic_to(p,
                x + ow, y + oh - r_br + r_br * KAPPA,
                x + ow - r_br + r_br * KAPPA, y + oh,
                x + ow - r_br, y + oh);
        }
        rdt_path_line_to(p, x + r_bl, y + oh);
        if (r_bl > 0) {
            rdt_path_cubic_to(p,
                x + r_bl - r_bl * KAPPA, y + oh,
                x, y + oh - r_bl + r_bl * KAPPA,
                x, y + oh - r_bl);
        }
        rdt_path_line_to(p, x, y + r_tl);
        if (r_tl > 0) {
            rdt_path_cubic_to(p,
                x, y + r_tl - r_tl * KAPPA,
                x + r_tl - r_tl * KAPPA, y,
                x + r_tl, y);
        }
        rdt_path_close(p);
    } else {
        rdt_path_add_rect(p, outline_rect.x, outline_rect.y,
            outline_rect.width, outline_rect.height, 0, 0);
    }

    float dash[2];
    RdtStrokeCap cap;
    int dash_count = get_dash_pattern(outline->style, w, dash, &cap);

    RdtPath* clip = create_border_clip_path(rdcon);
    rdt_push_clip(vec, clip, NULL);
    rdt_stroke_path(vec, p, outline->color, w, cap, RDT_JOIN_MITER,
                    dash_count > 0 ? dash : NULL, dash_count, xform);
    rdt_pop_clip(vec);
    rdt_path_free(clip);
    rdt_path_free(p);

    log_debug("[OUTLINE] Rendered outline: width=%.1f offset=%.1f style=%d color=#%02x%02x%02x%02x",
              outline->width, outline->offset, outline->style,
              outline->color.r, outline->color.g, outline->color.b, outline->color.a);
}
