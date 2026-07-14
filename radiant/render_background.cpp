#include "render.hpp"
#include "view.hpp"
#include "../lib/log.h"
#include "../lib/lambda_alloca.h"
#include "../lib/memtrack.h"
#include "../lib/str.h"
#include "../lambda/input/css/css_value.hpp"
#include <assert.h>
#include <math.h>

/**
 * Main background rendering dispatch
 */

static RdtPath* background_image_clip_path(RenderContext* rdcon, ViewBlock* view);
static void render_linear_gradient_layer(RenderContext* rdcon, ViewBlock* view,
                                         BackgroundProp* bg, LinearGradient* gradient,
                                         Rect position_rect, Rect paint_rect);
static void render_radial_gradient_layer(RenderContext* rdcon, ViewBlock* view,
                                         BackgroundProp* bg, RadialGradient* gradient,
                                         Rect position_rect, Rect paint_rect);
static void render_background_color(RenderContext* rdcon, ViewBlock* view,
                                    Color color, Rect rect);
static void render_background_gradient(RenderContext* rdcon, ViewBlock* view,
                                       BackgroundProp* bg, Rect rect);
static void render_background_image(RenderContext* rdcon, ViewBlock* view,
                                    BackgroundProp* bg, Rect rect);
static void render_linear_gradient(RenderContext* rdcon, ViewBlock* view,
                                   LinearGradient* gradient, Rect rect);
static void render_radial_gradient(RenderContext* rdcon, ViewBlock* view,
                                   RadialGradient* gradient, Rect rect);
static void render_conic_gradient(RenderContext* rdcon, ViewBlock* view,
                                  ConicGradient* gradient, Rect rect);

static Corner background_corner_scaled(const Corner* radius, float scale) {
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

static bool background_rounded_radius(ViewBlock* view, Rect rect, Corner* out_radius) {
    BorderProp* border = (view && view->bound) ? view->bound->border : nullptr;
    if (!border || !corner_has_radius(&border->radius)) return false;
    *out_radius = border->radius;
    constrain_corner_radii(out_radius, rect.width, rect.height);
    return true;
}

static RdtPath* background_rect_path(Rect rect) {
    RdtPath* path = rdt_path_new();
    rdt_path_add_rect(path, rect.x, rect.y, rect.width, rect.height, 0, 0);
    return path;
}

static RdtPath* background_rounded_rect_path(ViewBlock* view, Rect rect) {
    Corner radius;
    if (background_rounded_radius(view, rect, &radius)) {
        return render_path_create_rounded_rect(rect, &radius);
    }
    return background_rect_path(rect);
}

static RdtPath* background_border_rect_path(ViewBlock* view, Rect rect) {
    BorderProp* border = (view && view->bound) ? view->bound->border : nullptr;
    if (border && corner_has_radius(&border->radius)) {
        constrain_border_radii(border, rect.width, rect.height);
        return render_path_create_rounded_rect(rect, &border->radius);
    }
    return background_rect_path(rect);
}

static Corner background_corner_inset_box(const Corner* radius, CssEnum box,
                                          const BorderProp* border,
                                          const Spacing* padding,
                                          float scale) {
    Corner out = background_corner_scaled(radius, scale);
    float top = 0.0f, right = 0.0f, bottom = 0.0f, left = 0.0f;
    if (box == CSS_VALUE_PADDING_BOX || box == CSS_VALUE_CONTENT_BOX) {
        top += border ? border->width.top * scale : 0.0f;
        right += border ? border->width.right * scale : 0.0f;
        bottom += border ? border->width.bottom * scale : 0.0f;
        left += border ? border->width.left * scale : 0.0f;
    }
    if (box == CSS_VALUE_CONTENT_BOX && padding) {
        top += padding->top * scale;
        right += padding->right * scale;
        bottom += padding->bottom * scale;
        left += padding->left * scale;
    }

    out.top_left = max(0.0f, out.top_left - left);
    out.top_right = max(0.0f, out.top_right - right);
    out.bottom_right = max(0.0f, out.bottom_right - right);
    out.bottom_left = max(0.0f, out.bottom_left - left);
    out.top_left_y = max(0.0f, out.top_left_y - top);
    out.top_right_y = max(0.0f, out.top_right_y - top);
    out.bottom_right_y = max(0.0f, out.bottom_right_y - bottom);
    out.bottom_left_y = max(0.0f, out.bottom_left_y - bottom);
    return out;
}

void render_background(RenderContext* rdcon, ViewBlock* view, Rect rect) {
    if (!view->bound || !view->bound->background) return;

    BackgroundProp* bg = view->bound->background;

    log_debug("[RENDER BG] Element <%s>: color=#%08x gradient_type=%d linear=%p radial=%p",
              view->node_name(), bg->color.c, bg->gradient_type,
              (void*)bg->linear_gradient, (void*)bg->radial_gradient);

    if (bg->bg_clip == CSS_VALUE_TEXT) {
        log_debug("[RENDER BG] background-clip:text defers background paint to glyph fill");
        return;
    }

    float s = rdcon->scale;
    BorderProp* border  = view->bound->border;
    Spacing*    padding = &view->bound->padding;

    // Determine positioning area (bg-origin, default: padding-box)
    // Controls where background-position and background-size are calculated relative to.
    CssEnum origin = bg->bg_origin ? bg->bg_origin : CSS_VALUE_PADDING_BOX;
    Rect pos_rect = render_geometry_adjust_box_rect(rect, origin, s, border, padding);

    // Determine paint area (bg-clip, default: border-box)
    // Controls where the background is visually clipped.
    CssEnum clip_box = bg->bg_clip ? bg->bg_clip : CSS_VALUE_BORDER_BOX;
    Rect paint_rect = render_geometry_adjust_box_rect(rect, clip_box, s, border, padding);

    // Narrow the active clip to the paint area (intersect with existing viewport clip).
    // Skip clip tightening when a CSS transform is active — the transform displaces
    // rendered content beyond the static layout box, so tightening to the untransformed
    // paint area would clip the transformed content (e.g. translateX causes half-circles).
    Bound orig_clip = rdcon->block.clip;
    if (!rdcon->has_transform) {
        rdcon->block.clip = render_geometry_intersect_bound_rect(orig_clip, paint_rect);
    }

    Corner orig_radius = {};
    bool swapped_radius = false;
    if (border && corner_has_radius(&border->radius)) {
        orig_radius = border->radius;
        border->radius = background_corner_inset_box(&orig_radius, clip_box, border, padding, s);
        constrain_corner_radii(&border->radius, paint_rect.width, paint_rect.height);
        swapped_radius = true;
    }

    // Render base color first (if any), clipped to paint area
    if (bg->color.a > 0) {
        render_background_color(rdcon, view, bg->color, paint_rect);
    }

    // Check if background-blend-mode requires special compositing
    bool has_blend = (bg->blend_mode != 0 && bg->blend_mode != CSS_VALUE_NORMAL);
    bool has_upper_layers = bg->image ||
        (bg->radial_layers && bg->radial_layer_count > 0) ||
        (bg->linear_layers && bg->linear_layer_count > 0) ||
        (bg->gradient_type != GRADIENT_NONE &&
         (bg->linear_gradient || bg->radial_gradient || bg->conic_gradient));

    // Save backdrop (background-color) before rendering upper layers if blend mode is active.
    // In display-list mode, use DL_SAVE_BACKDROP/DL_APPLY_BLEND_MODE so the blending
    // happens during replay when surface pixels are available.
    ImageSurface* surface = rdcon->ui_context->surface;
    int blend_x0 = 0, blend_y0 = 0, blend_w = 0, blend_h = 0;
    if (has_blend && has_upper_layers) {
        IRect blend_bounds = render_geometry_clip_to_pixel_bounds(rdcon->block.clip, surface);
        blend_x0 = blend_bounds.x;
        blend_y0 = blend_bounds.y;
        blend_w = blend_bounds.w;
        blend_h = blend_bounds.h;
        if (!render_geometry_pixel_bounds_empty(blend_bounds)) {
            rc_save_backdrop(rdcon, blend_x0, blend_y0, blend_w, blend_h);
        }
    }

    // Render background image positioned within pos_rect, painted within paint_rect (via clip)
    if (bg->image) {
        render_background_image(rdcon, view, bg, pos_rect);
    }

    // Render all linear gradient layers (stacked bottom-to-top)
    if (bg->linear_layers && bg->linear_layer_count > 0) {
        for (int i = 0; i < bg->linear_layer_count; i++) {
            if (bg->linear_layers[i]) {
                log_debug("[GRADIENT] Rendering linear gradient layer %d/%d", i + 1, bg->linear_layer_count);
                render_linear_gradient_layer(rdcon, view, bg, bg->linear_layers[i], pos_rect, paint_rect);
            }
        }
    }

    // Render all radial gradient layers (stacked bottom-to-top)
    if (bg->radial_layers && bg->radial_layer_count > 0) {
        for (int i = 0; i < bg->radial_layer_count; i++) {
            if (bg->radial_layers[i]) {
                log_debug("[GRADIENT] Rendering radial gradient layer %d/%d", i + 1, bg->radial_layer_count);
                render_radial_gradient_layer(rdcon, view, bg, bg->radial_layers[i], pos_rect, paint_rect);
            }
        }
    }

    // Render main gradient (if any), clipped to paint area
    if (bg->gradient_type != GRADIENT_NONE &&
        (bg->linear_gradient || bg->radial_gradient || bg->conic_gradient)) {
        log_debug("[GRADIENT] Rendering gradient type=%d", bg->gradient_type);
        if (bg->gradient_type == GRADIENT_LINEAR && bg->linear_gradient) {
            render_linear_gradient_layer(rdcon, view, bg, bg->linear_gradient, pos_rect, paint_rect);
        } else if (bg->gradient_type == GRADIENT_RADIAL && bg->radial_gradient) {
            render_radial_gradient_layer(rdcon, view, bg, bg->radial_gradient, pos_rect, paint_rect);
        } else {
            render_background_gradient(rdcon, view, bg, paint_rect);
        }
    }

    // Apply background-blend-mode: composite upper layers onto saved backdrop
    if (has_blend && has_upper_layers && blend_w > 0 && blend_h > 0) {
        rc_apply_blend_mode(rdcon, blend_x0, blend_y0, blend_w, blend_h,
                            bg->blend_mode);
        log_debug("[BLEND] Recorded DL background-blend-mode=%d for %dx%d region",
                  bg->blend_mode, blend_w, blend_h);
    }

    // Restore original clip
    if (swapped_radius) {
        border->radius = orig_radius;
    }
    rdcon->block.clip = orig_clip;
}

/**
 * Render solid color background
 * Handles border-radius by using ThorVG if needed
 */
static void render_background_color(RenderContext* rdcon, ViewBlock* view, Color color, Rect rect) {
    bool has_radius = false;
    BorderProp* border = nullptr;
    if (view->bound && view->bound->border) {
        border = view->bound->border;
        has_radius = corner_has_radius(&border->radius);
    }

    bool needs_rounded_clip = rdcon->block.has_clip_radius;

    if (has_radius || needs_rounded_clip || rdcon->has_transform || rdcon->clip_shape_depth > 0) {
        const RdtMatrix* xform = render_state_current_transform(rdcon);

        RdtPath* p = nullptr;
        if (has_radius && border) {
            constrain_border_radii(border, rect.width, rect.height);
            p = render_path_create_rounded_rect(rect, &border->radius);
        } else {
            p = rdt_path_new();
            rdt_path_add_rect(p, rect.x, rect.y, rect.width, rect.height, 0, 0);
        }

        RdtPath* clip = render_path_create_clip_path(rdcon);
        rc_push_clip(rdcon, clip, NULL);
        rc_fill_path(rdcon, p, color, RDT_FILL_WINDING, xform);
        rc_pop_clip(rdcon);
        rdt_path_free(clip);
        rdt_path_free(p);
    } else {
        render_painter_fill_surface_rect(rdcon, rdcon->ui_context->surface, &rect, color.c, &rdcon->block.clip,
            rdcon->clip_shapes, rdcon->clip_shape_depth);
    }
}

/**
 * Convert angle in degrees to radians
 */
static inline float deg_to_rad(float degrees) {
    return degrees * M_PI / 180.0f;
}

/**
 * Calculate linear gradient start and end points from angle
 * CSS angle: 0deg = to top, 90deg = to right, 180deg = to bottom, 270deg = to left
 */
static void calc_linear_gradient_points(float angle, Rect rect,
                                        float* x1, float* y1, float* x2, float* y2) {
    // Convert CSS angle to standard math angle (90deg offset)
    float rad = deg_to_rad(angle - 90.0f);

    float w = rect.width;
    float h = rect.height;
    float cx = rect.x + w / 2;
    float cy = rect.y + h / 2;

    // Calculate gradient line length (diagonal)
    float gradient_length = fabs(w * cosf(rad)) + fabs(h * sinf(rad));

    // Calculate start and end points
    float dx = cosf(rad) * gradient_length / 2;
    float dy = sinf(rad) * gradient_length / 2;

    *x1 = cx - dx;
    *y1 = cy - dy;
    *x2 = cx + dx;
    *y2 = cy + dy;

    log_debug("[GRADIENT] Linear gradient angle=%.1f°, line=(%.1f,%.1f)-(%.1f,%.1f)",
              angle, *x1, *y1, *x2, *y2);
}

/**
 * Render linear gradient
 */
static RdtPath* background_gradient_clip_path(ViewBlock* view, Rect clip_rect) {
    return background_rounded_rect_path(view, clip_rect);
}

static void render_linear_gradient_tile(RenderContext* rdcon, ViewBlock* view,
                                        LinearGradient* gradient, Rect tile_rect,
                                        Rect clip_rect) {
    if (!gradient || gradient->stop_count < 2) {
        log_debug("[GRADIENT] Invalid gradient (need at least 2 stops)");
        return;
    }

    log_debug("[GRADIENT] render_linear_gradient <%s> rect=(%.0f,%.0f,%.0f,%.0f)",
              view->node_name(), tile_rect.x, tile_rect.y, tile_rect.width, tile_rect.height);

    const RdtMatrix* xform = render_state_current_transform(rdcon);

    bool same_rect = fabsf(tile_rect.x - clip_rect.x) < 0.001f &&
                     fabsf(tile_rect.y - clip_rect.y) < 0.001f &&
                     fabsf(tile_rect.width - clip_rect.width) < 0.001f &&
                     fabsf(tile_rect.height - clip_rect.height) < 0.001f;
    RdtPath* p = same_rect ? background_rounded_rect_path(view, tile_rect)
                           : background_rect_path(tile_rect);

    // Calculate gradient line
    float x1, y1, x2, y2;
    calc_linear_gradient_points(gradient->angle, tile_rect, &x1, &y1, &x2, &y2);

    // Build gradient stops
    int stop_count = gradient->stop_count;
    // gradient stop counts come from parsed CSS; use render scratch to avoid attacker-sized stack frames.
    RdtGradientStop* stops = (RdtGradientStop*)scratch_calloc(&rdcon->scratch, (size_t)stop_count * sizeof(RdtGradientStop));
    if (!stops) {
        log_error("[GRADIENT] failed to allocate %d linear gradient stops", stop_count);
        rdt_path_free(p);
        return;
    }
    for (int i = 0; i < gradient->stop_count; i++) {
        GradientStop* gs = &gradient->stops[i];
        stops[i].offset = gs->position >= 0 ? gs->position : (float)i / (gradient->stop_count - 1);
        stops[i].r = gs->color.r;
        stops[i].g = gs->color.g;
        stops[i].b = gs->color.b;
        stops[i].a = gs->color.a;
        log_debug("[GRADIENT] Stop %d: pos=%.2f color=#%02x%02x%02x%02x",
                  i, stops[i].offset, stops[i].r, stops[i].g, stops[i].b, stops[i].a);
    }

    if (gradient->stops_in_px) {
        float grad_len = sqrtf((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
        if (grad_len > 0.0f) {
            for (int i = 0; i < stop_count; i++) {
                if (gradient->stops[i].position >= 0.0f) {
                    stops[i].offset = gradient->stops[i].position / grad_len;
                }
            }
        }
    }

    // Handle repeating-linear-gradient: convert px positions and replicate stops
    RdtGradientStop* final_stops = stops;
    int final_stop_count = stop_count;
    if (gradient->is_repeating && stop_count >= 2) {
        float first_pos = stops[0].offset;
        float last_pos = stops[stop_count - 1].offset;
        float unit = last_pos - first_pos;
        if (unit > 0.001f && unit < 0.999f) {
            // replicate stops to fill 0.0 - 1.0 range
            int start_rep = (int)floorf((0.0f - first_pos) / unit);
            int end_rep = (int)ceilf((1.0f - first_pos) / unit);
            int num_reps = end_rep - start_rep;
            int max_stops = num_reps * stop_count;
            RdtGradientStop* rep_stops = LAMBDA_ALLOCA(max_stops, RdtGradientStop);
            int idx = 0;
            for (int r = start_rep; r < end_rep; r++) {
                for (int i = 0; i < stop_count; i++) {
                    float offset = stops[i].offset + r * unit;
                    if (offset < -0.001f || offset > 1.001f) continue;
                    rep_stops[idx] = stops[i];
                    rep_stops[idx].offset = fmaxf(0.0f, fminf(1.0f, offset));
                    idx++;
                }
            }
            if (idx >= 2) {
                final_stops = rep_stops;
                final_stop_count = idx;
                log_debug("[GRADIENT] Repeating: replicated %d stops to %d (unit=%.3f)",
                          stop_count, idx, unit);
            }
        }
    }

    RdtPath* clip = same_rect ? render_path_create_clip_path(rdcon)
                              : background_gradient_clip_path(view, clip_rect);
    rc_push_clip(rdcon, clip, NULL);
    rc_fill_linear_gradient(rdcon, p, x1, y1, x2, y2, final_stops, final_stop_count,
                             RDT_FILL_WINDING, xform);
    rc_pop_clip(rdcon);
    rdt_path_free(clip);
    rdt_path_free(p);
    scratch_free(&rdcon->scratch, stops);
}

static void render_linear_gradient(RenderContext* rdcon, ViewBlock* view, LinearGradient* gradient, Rect rect) {
    render_linear_gradient_tile(rdcon, view, gradient, rect, rect);
}

/**
 * Calculate radial gradient radius based on CSS size keyword
 */
static float calc_radial_radius(RadialGradient* gradient, Rect rect, float cx, float cy) {
    float w = rect.width;
    float h = rect.height;

    // Distance to each corner
    float d_tl = sqrtf(cx * cx + cy * cy);
    float d_tr = sqrtf((w - cx) * (w - cx) + cy * cy);
    float d_bl = sqrtf(cx * cx + (h - cy) * (h - cy));
    float d_br = sqrtf((w - cx) * (w - cx) + (h - cy) * (h - cy));

    // Distance to each side
    float d_top = cy;
    float d_bottom = h - cy;
    float d_left = cx;
    float d_right = w - cx;

    float radius = 0;

    switch (gradient->size) {
        case RADIAL_SIZE_CLOSEST_SIDE:
            radius = fminf(fminf(d_top, d_bottom), fminf(d_left, d_right));
            break;
        case RADIAL_SIZE_FARTHEST_SIDE:
            radius = fmaxf(fmaxf(d_top, d_bottom), fmaxf(d_left, d_right));
            break;
        case RADIAL_SIZE_CLOSEST_CORNER:
            radius = fminf(fminf(d_tl, d_tr), fminf(d_bl, d_br));
            break;
        case RADIAL_SIZE_FARTHEST_CORNER:
        default:
            radius = fmaxf(fmaxf(d_tl, d_tr), fmaxf(d_bl, d_br));
            break;
    }

    return radius;
}

/**
 * Render radial gradient
 */
static void render_radial_gradient(RenderContext* rdcon, ViewBlock* view, RadialGradient* gradient, Rect rect) {
    if (!gradient || gradient->stop_count < 2) {
        log_debug("[GRADIENT] Invalid radial gradient (need at least 2 stops)");
        return;
    }

    const RdtMatrix* xform = render_state_current_transform(rdcon);

    RdtPath* p = background_border_rect_path(view, rect);

    float cx = rect.x + rect.width * gradient->cx;
    float cy = rect.y + rect.height * gradient->cy;
    float radius = calc_radial_radius(gradient, rect, rect.width * gradient->cx, rect.height * gradient->cy);

    if (gradient->shape == RADIAL_SHAPE_ELLIPSE) {
        radius = fmaxf(rect.width, rect.height) * 0.5f;
    }

    log_debug("[GRADIENT] Radial gradient center=(%.1f,%.1f) radius=%.1f shape=%d",
              cx, cy, radius, gradient->shape);

    // gradient stop counts come from parsed CSS; use render scratch to avoid attacker-sized stack frames.
    RdtGradientStop* stops = (RdtGradientStop*)scratch_calloc(&rdcon->scratch, (size_t)gradient->stop_count * sizeof(RdtGradientStop));
    if (!stops) {
        log_error("[GRADIENT] failed to allocate %d radial gradient stops", gradient->stop_count);
        rdt_path_free(p);
        return;
    }
    for (int i = 0; i < gradient->stop_count; i++) {
        GradientStop* gs = &gradient->stops[i];
        stops[i].offset = gs->position;
        stops[i].r = gs->color.r;
        stops[i].g = gs->color.g;
        stops[i].b = gs->color.b;
        stops[i].a = gs->color.a;
        log_debug("[GRADIENT] Radial stop %d: pos=%.2f color=#%02x%02x%02x%02x",
                  i, stops[i].offset, stops[i].r, stops[i].g, stops[i].b, stops[i].a);
    }

    RdtPath* clip = render_path_create_clip_path(rdcon);
    rc_push_clip(rdcon, clip, NULL);
    rc_fill_radial_gradient(rdcon, p, cx, cy, radius, stops, gradient->stop_count,
                             RDT_FILL_WINDING, xform);
    rc_pop_clip(rdcon);
    rdt_path_free(clip);
    rdt_path_free(p);
    scratch_free(&rdcon->scratch, stops);
}

/**
 * Interpolate between two colors
 */
static Color lerp_color(Color c1, Color c2, float t) {
    Color result;
    result.r = (uint8_t)(c1.r + (c2.r - c1.r) * t);
    result.g = (uint8_t)(c1.g + (c2.g - c1.g) * t);
    result.b = (uint8_t)(c1.b + (c2.b - c1.b) * t);
    result.a = (uint8_t)(c1.a + (c2.a - c1.a) * t);
    return result;
}

/**
 * Get color at position in gradient stops
 */
static Color get_gradient_color_at(GradientStop* stops, int stop_count, float position) {
    Color transparent;
    transparent.r = 0; transparent.g = 0; transparent.b = 0; transparent.a = 255;
    if (stop_count == 0) return transparent;
    if (stop_count == 1) return stops[0].color;
    if (position <= stops[0].position) return stops[0].color;
    if (position >= stops[stop_count - 1].position) return stops[stop_count - 1].color;

    // Find the two stops we're between
    for (int i = 0; i < stop_count - 1; i++) {
        if (position >= stops[i].position && position <= stops[i + 1].position) {
            float range = stops[i + 1].position - stops[i].position;
            float t = (range > 0) ? (position - stops[i].position) / range : 0;
            return lerp_color(stops[i].color, stops[i + 1].color, t);
        }
    }

    return stops[stop_count - 1].color;
}

/**
 * Render conic gradient using software rendering
 * ThorVG doesn't support conic gradients directly, so we render pixel-by-pixel
 */
static void render_conic_gradient(RenderContext* rdcon, ViewBlock* view, ConicGradient* gradient, Rect rect) {
    if (!gradient || gradient->stop_count < 2) {
        log_debug("[GRADIENT] Invalid conic gradient (need at least 2 stops)");
        return;
    }

    log_debug("[GRADIENT] Rendering conic gradient: from=%.1fdeg center=(%.2f,%.2f) stops=%d",
              gradient->from_angle, gradient->cx, gradient->cy, gradient->stop_count);

    for (int i = 0; i < gradient->stop_count; i++) {
        log_debug("[GRADIENT] Conic stop %d: pos=%.2f color=#%02x%02x%02x",
                  i, gradient->stops[i].position,
                  gradient->stops[i].color.r, gradient->stops[i].color.g, gradient->stops[i].color.b);
    }

    int w = (int)(rect.width + 0.5f);
    int h = (int)(rect.height + 0.5f);
    if (w <= 0 || h <= 0) return;

    // Render gradient pixels to a temporary buffer
    uint32_t* pixels = (uint32_t*)mem_calloc(w * h, sizeof(uint32_t), MEM_CAT_RENDER);

    float cx = w * gradient->cx;
    float cy = h * gradient->cy;
    float from_rad = (gradient->from_angle - 90.0f) * (float)M_PI / 180.0f;

    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            float dx = px - cx;
            float dy = py - cy;
            float angle = atan2f(dy, dx) - from_rad;
            float position = fmodf((angle / (2.0f * (float)M_PI)) + 1.0f, 1.0f);

            Color color = get_gradient_color_at(gradient->stops, gradient->stop_count, position);
            pixels[py * w + px] = color.r | (color.g << 8) | (color.b << 16) | (color.a << 24);
        }
    }

    // Clip to border-radius if present (using the ThorVG clip path, works in DL mode)
    bool pushed_clip = false;
    RdtPath* clip_path = nullptr;
    Corner radius;
    if (background_rounded_radius(view, rect, &radius)) {
        clip_path = render_path_create_rounded_rect(rect, &radius);
        rc_push_clip(rdcon, clip_path, NULL);
        pushed_clip = true;
    }

    // rc_draw_image records uint32_t row stride; the generated buffer is tightly packed.
    rc_draw_image(rdcon, pixels, w, h, w,
                  rect.x, rect.y, rect.width, rect.height, 255, NULL);

    if (pushed_clip) {
        rc_pop_clip(rdcon);
        rdt_path_free(clip_path);
    }

    mem_free(pixels);
}

/**
 * Render background gradient (dispatch to type-specific function)
 */
static void render_background_gradient(RenderContext* rdcon, ViewBlock* view, BackgroundProp* bg, Rect rect) {
    switch (bg->gradient_type) {
        case GRADIENT_LINEAR:
            if (bg->linear_gradient) {
                render_linear_gradient(rdcon, view, bg->linear_gradient, rect);
            }
            break;
        case GRADIENT_RADIAL:
            if (bg->radial_gradient) {
                render_radial_gradient(rdcon, view, bg->radial_gradient, rect);
            }
            break;
        case GRADIENT_CONIC:
            if (bg->conic_gradient) {
                render_conic_gradient(rdcon, view, bg->conic_gradient, rect);
            }
            break;
        default:
            log_debug("[GRADIENT] Unknown gradient type");
            break;
    }
}

/**
 * Apply a 3-pass box blur on a region of the surface pixels.
 * 3 passes of box blur approximate Gaussian blur (per W3C CSS Backgrounds Level 3).
 * The blur_radius is the CSS blur radius (σ); box size = ceil(σ * 2 / 3) * 2 + 1.
 * Operates on pre-multiplied RGBA pixels in-place.
 */
void box_blur_region(ScratchArena* sa, ImageSurface* surface, int rx, int ry, int rw, int rh, float blur_radius) {
    if (blur_radius <= 0 || rw <= 0 || rh <= 0 || !surface || !surface->pixels) return;

    // Sub-pixel blur radii (< 1px) produce no visible result in browsers — skip.
    // Without this guard the box_r min-clamp below would force a ~3px-wide kernel
    // applied 3 times (effective stddev ~1.4px), creating a visible halo for
    // shadows like `box-shadow: 0 0 0.375px ...` that browsers render as no blur.
    if (blur_radius < 1.0f) return;

    // Clamp region to surface bounds
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > surface->width) rw = surface->width - rx;
    if (ry + rh > surface->height) rh = surface->height - ry;
    if (rw <= 0 || rh <= 0) return;

    // Calculate box sizes for 3-pass approximation of Gaussian.
    // Per CSS spec, the CSS blur-radius value `b` targets a Gaussian with
    // standard deviation σ = b/2. Three passes of a box blur with half-width r
    // give total variance r(r+1), so to match σ = b/2 we want
    //   r(r+1) = b²/4   ⇒   r = (-1 + √(1 + b²)) / 2
    // The previous formula `ceilf(b/2)` produced a kernel ~2× wider than the
    // browser for small blur radii (e.g. b=3 → r=2 giving σ≈2.45 vs target 1.5),
    // creating visible halos around shadows that should be tight to the element.
    int box_r = (int)roundf(0.5f * (-1.0f + sqrtf(1.0f + blur_radius * blur_radius)));
    if (box_r < 1) box_r = 1;

    uint32_t* pixels = (uint32_t*)surface->pixels;
    int stride = surface->pitch / 4;  // pitch is in bytes, pixels are 4 bytes

    // Allocate temporary buffer for one pass
    size_t buf_size = (size_t)rw * rh;
    uint32_t* temp = (uint32_t*)scratch_alloc(sa, buf_size * sizeof(uint32_t));
    if (!temp) return;

    // 3-pass box blur (horizontal then vertical each pass)
    for (int pass = 0; pass < 3; pass++) {
        // Horizontal pass: read from surface, write to temp
        for (int row = 0; row < rh; row++) {
            int sy = ry + row;
            uint32_t* src_row = pixels + sy * stride;

            // Running sums for each channel
            uint32_t sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
            int count = 0;

            // Initialize window for first pixel
            for (int k = -box_r; k <= box_r; k++) {
                int sx = rx + 0 + k;
                if (sx >= rx && sx < rx + rw) {
                    uint32_t p = src_row[sx];
                    sum_r += (p >> 24) & 0xFF;
                    sum_g += (p >> 16) & 0xFF;
                    sum_b += (p >> 8) & 0xFF;
                    sum_a += p & 0xFF;
                    count++;
                }
            }

            for (int col = 0; col < rw; col++) {
                if (count > 0) {
                    uint32_t avg_r = sum_r / count;
                    uint32_t avg_g = sum_g / count;
                    uint32_t avg_b = sum_b / count;
                    uint32_t avg_a = sum_a / count;
                    temp[row * rw + col] = (avg_r << 24) | (avg_g << 16) | (avg_b << 8) | avg_a;
                } else {
                    temp[row * rw + col] = 0;
                }

                // Slide window: remove leftmost, add new rightmost
                int remove_x = rx + col - box_r;
                int add_x = rx + col + box_r + 1;

                if (remove_x >= rx && remove_x < rx + rw) {
                    uint32_t p = src_row[remove_x];
                    sum_r -= (p >> 24) & 0xFF;
                    sum_g -= (p >> 16) & 0xFF;
                    sum_b -= (p >> 8) & 0xFF;
                    sum_a -= p & 0xFF;
                    count--;
                }
                if (add_x >= rx && add_x < rx + rw) {
                    uint32_t p = src_row[add_x];
                    sum_r += (p >> 24) & 0xFF;
                    sum_g += (p >> 16) & 0xFF;
                    sum_b += (p >> 8) & 0xFF;
                    sum_a += p & 0xFF;
                    count++;
                }
            }
        }

        // Vertical pass: read from temp, write to surface
        for (int col = 0; col < rw; col++) {
            uint32_t sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
            int count = 0;

            // Initialize window
            for (int k = -box_r; k <= box_r; k++) {
                int tr = 0 + k;
                if (tr >= 0 && tr < rh) {
                    uint32_t p = temp[tr * rw + col];
                    sum_r += (p >> 24) & 0xFF;
                    sum_g += (p >> 16) & 0xFF;
                    sum_b += (p >> 8) & 0xFF;
                    sum_a += p & 0xFF;
                    count++;
                }
            }

            for (int row = 0; row < rh; row++) {
                if (count > 0) {
                    uint32_t avg_r = sum_r / count;
                    uint32_t avg_g = sum_g / count;
                    uint32_t avg_b = sum_b / count;
                    uint32_t avg_a = sum_a / count;
                    pixels[(ry + row) * stride + (rx + col)] = (avg_r << 24) | (avg_g << 16) | (avg_b << 8) | avg_a;
                }

                int remove_r = row - box_r;
                int add_r = row + box_r + 1;

                if (remove_r >= 0 && remove_r < rh) {
                    uint32_t p = temp[remove_r * rw + col];
                    sum_r -= (p >> 24) & 0xFF;
                    sum_g -= (p >> 16) & 0xFF;
                    sum_b -= (p >> 8) & 0xFF;
                    sum_a -= p & 0xFF;
                    count--;
                }
                if (add_r >= 0 && add_r < rh) {
                    uint32_t p = temp[add_r * rw + col];
                    sum_r += (p >> 24) & 0xFF;
                    sum_g += (p >> 16) & 0xFF;
                    sum_b += (p >> 8) & 0xFF;
                    sum_a += p & 0xFF;
                    count++;
                }
            }
        }
    }

    scratch_free(sa, temp);
}

static bool clip_surface_region(ImageSurface* surface, int* rx, int* ry, int* rw, int* rh) {
    if (*rx < 0) { *rw += *rx; *rx = 0; }
    if (*ry < 0) { *rh += *ry; *ry = 0; }
    if (*rx + *rw > surface->width) *rw = surface->width - *rx;
    if (*ry + *rh > surface->height) *rh = surface->height - *ry;
    return *rw > 0 && *rh > 0;
}

static void transform_surface_region(ImageSurface* surface, int rx, int ry, int rw, int rh,
                                     const Color* tint) {
    if (!surface || !surface->pixels || rw <= 0 || rh <= 0) return;
    if (!clip_surface_region(surface, &rx, &ry, &rw, &rh)) return;
    uint32_t* pixels = (uint32_t*)surface->pixels;
    int stride = surface->pitch / 4;
    for (int row = 0; row < rh; row++) {
        uint32_t* dst = pixels + (ry + row) * stride + rx;
        for (int col = 0; col < rw; col++) {
            uint32_t p = dst[col];
            uint32_t a = (p >> 24) & 0xFF;
            if (a == 0) {
                dst[col] = 0;
                continue;
            }
            if (!tint && a == 255) continue;
            uint32_t r = tint ? tint->r : p & 0xFF;
            uint32_t g = tint ? tint->g : (p >> 8) & 0xFF;
            uint32_t b = tint ? tint->b : (p >> 16) & 0xFF;
            r = (r * a + 127u) / 255u;
            g = (g * a + 127u) / 255u;
            b = (b * a + 127u) / 255u;
            dst[col] = (a << 24) | (b << 16) | (g << 8) | r;
        }
    }
}

void premultiply_surface_region(ImageSurface* surface, int rx, int ry, int rw, int rh) {
    transform_surface_region(surface, rx, ry, rw, rh, nullptr);
}

void tint_premultiplied_surface_region(ImageSurface* surface, int rx, int ry, int rw, int rh,
                                       Color color) {
    transform_surface_region(surface, rx, ry, rw, rh, &color);
}

/**
 * Inset box-shadow blur: blur in a temporary buffer with the element background
 * color painted in the padding area, then copy the inner rect back to the surface.
 *
 * This solves the edge-clamping problem: instead of clamping at the element boundary
 * (which keeps edges at 100% intensity), the blur kernel samples from the element's
 * background color outside the element, producing a correct ~50% Gaussian falloff
 * at edges.  The surface outside the element is never modified.
 *
 * @param bg_color  Element background color in surface pixel format (RGBA8888).
 *                  Used to fill the padding area in the temp buffer so the blur
 *                  sees the correct base color (not the parent's background).
 */
void box_blur_region_inset(ScratchArena* sa, ImageSurface* surface,
                           int rx, int ry, int rw, int rh,
                           int pad, float blur_radius, uint32_t bg_color) {
    if (blur_radius <= 0 || rw <= 0 || rh <= 0 || !surface || !surface->pixels) return;
    if (pad <= 0) {
        box_blur_region(sa, surface, rx, ry, rw, rh, blur_radius);
        return;
    }

    // Expanded region (clamped to surface bounds)
    int ex = (0 > rx - pad) ? 0 : rx - pad;
    int ey = (0 > ry - pad) ? 0 : ry - pad;
    int ex2 = (surface->width < rx + rw + pad) ? surface->width : rx + rw + pad;
    int ey2 = (surface->height < ry + rh + pad) ? surface->height : ry + rh + pad;
    int ew = ex2 - ex;
    int eh = ey2 - ey;
    if (ew <= 0 || eh <= 0) return;

    uint32_t* pixels = (uint32_t*)surface->pixels;
    int stride = surface->pitch / 4;

    // Copy expanded region from surface to temp buffer
    size_t buf_size = (size_t)ew * eh;
    uint32_t* buf = (uint32_t*)scratch_alloc(sa, buf_size * sizeof(uint32_t));
    if (!buf) return;

    for (int row = 0; row < eh; row++) {
        memcpy(buf + row * ew,
               pixels + (ey + row) * stride + ex,
               ew * sizeof(uint32_t));
    }

    // Fill outer pixels (outside inner rect) with element background color.
    // This replaces the parent's background (e.g. white) so the blur kernel
    // at the element edges sees the correct base color.
    int ix0 = rx - ex;  // inner rect offset within temp buffer
    int iy0 = ry - ey;
    int ix1 = ix0 + rw;
    int iy1 = iy0 + rh;

    for (int row = 0; row < eh; row++) {
        if (row < iy0 || row >= iy1) {
            // Entire row is outside inner rect
            for (int col = 0; col < ew; col++)
                buf[row * ew + col] = bg_color;
        } else {
            // Fill left and right portions
            for (int col = 0; col < ix0; col++)
                buf[row * ew + col] = bg_color;
            for (int col = ix1; col < ew; col++)
                buf[row * ew + col] = bg_color;
        }
    }

    // Blur the temp buffer using a temporary ImageSurface wrapper
    ImageSurface tmp_surface;
    tmp_surface.pixels = (uint8_t*)buf;
    tmp_surface.width = ew;
    tmp_surface.height = eh;
    tmp_surface.pitch = ew * 4;
    box_blur_region(sa, &tmp_surface, 0, 0, ew, eh, blur_radius);

    // Copy inner rect from blurred temp back to surface (clamped to surface bounds).
    // When called from tiled rendering, (rx, ry) can be negative after tile-local
    // translation, so we must only copy the intersection with the surface.
    int cpy_x = (rx > 0) ? rx : 0;
    int cpy_y = (ry > 0) ? ry : 0;
    int cpy_x2 = ((rx + rw) < surface->width) ? (rx + rw) : surface->width;
    int cpy_y2 = ((ry + rh) < surface->height) ? (ry + rh) : surface->height;
    int cpy_w = cpy_x2 - cpy_x;
    int cpy_h = cpy_y2 - cpy_y;

    if (cpy_w > 0 && cpy_h > 0) {
        int buf_x = cpy_x - ex;
        int buf_y = cpy_y - ey;
        for (int row = 0; row < cpy_h; row++) {
            memcpy(pixels + (cpy_y + row) * stride + cpy_x,
                   buf + (buf_y + row) * ew + buf_x,
                   cpy_w * sizeof(uint32_t));
        }
    }

    scratch_free(sa, buf);
}

/**
 * Software-rasterise a rounded rect into a uint32 buffer at a fixed pixel value.
 * - buf is ew x eh, with origin (ox, oy) in surface coordinates.
 * - Shadow rect (sx, sy, sw, sh) in surface coords, per-corner radii.
 * - Inside pixels are written with `pixel`; outside pixels are left untouched.
 * Coverage is binary (no anti-aliasing) — fine because the buffer is then
 * box-blurred which produces the soft edge falloff naturally.
 */
static void rasterise_rounded_rect_into_buffer(
    uint32_t* buf, int ew, int eh, int ox, int oy,
    float sx, float sy, float sw, float sh,
    float sr_tl, float sr_tr, float sr_br, float sr_bl,
    uint32_t pixel)
{
    float x0 = sx, y0 = sy;
    float x1 = sx + sw, y1 = sy + sh;
    // clamp radii to half the side lengths (per CSS spec)
    float maxr_x = sw * 0.5f, maxr_y = sh * 0.5f;
    if (sr_tl > maxr_x) sr_tl = maxr_x;  if (sr_tl > maxr_y) sr_tl = maxr_y;
    if (sr_tr > maxr_x) sr_tr = maxr_x;  if (sr_tr > maxr_y) sr_tr = maxr_y;
    if (sr_br > maxr_x) sr_br = maxr_x;  if (sr_br > maxr_y) sr_br = maxr_y;
    if (sr_bl > maxr_x) sr_bl = maxr_x;  if (sr_bl > maxr_y) sr_bl = maxr_y;
    for (int row = 0; row < eh; row++) {
        float py = (float)(oy + row) + 0.5f;
        if (py < y0 || py >= y1) continue;
        for (int col = 0; col < ew; col++) {
            float px = (float)(ox + col) + 0.5f;
            if (px < x0 || px >= x1) continue;
            bool inside = true;
            // corner-region tests
            if (sr_tl > 0 && px < x0 + sr_tl && py < y0 + sr_tl) {
                float dx = (x0 + sr_tl) - px;
                float dy = (y0 + sr_tl) - py;
                inside = (dx * dx + dy * dy) <= (sr_tl * sr_tl);
            } else if (sr_tr > 0 && px > x1 - sr_tr && py < y0 + sr_tr) {
                float dx = px - (x1 - sr_tr);
                float dy = (y0 + sr_tr) - py;
                inside = (dx * dx + dy * dy) <= (sr_tr * sr_tr);
            } else if (sr_br > 0 && px > x1 - sr_br && py > y1 - sr_br) {
                float dx = px - (x1 - sr_br);
                float dy = py - (y1 - sr_br);
                inside = (dx * dx + dy * dy) <= (sr_br * sr_br);
            } else if (sr_bl > 0 && px < x0 + sr_bl && py > y1 - sr_bl) {
                float dx = (x0 + sr_bl) - px;
                float dy = py - (y1 - sr_bl);
                inside = (dx * dx + dy * dy) <= (sr_bl * sr_bl);
            }
            if (inside) buf[row * ew + col] = pixel;
        }
    }
}

/**
 * Outer box-shadow rendering with isolated blur buffer.
 *
 * Why this is needed:
 *   The previous implementation filled the shadow path directly on the surface
 *   then applied a 3-pass box blur in-place over the blur region.  That blur
 *   reads ALL pixels in the region — including any neighbouring sibling
 *   elements (e.g. a Box A sitting just above Box B's shadow).  The averaging
 *   smears those sibling pixels and writes blurred values back, visibly
 *   darkening / clipping the siblings.
 *
 * New algorithm (matches CSS Backgrounds Level 3 §7 model):
 *   1. Allocate a private uint32 buffer the size of the blur region, cleared
 *      to (0, 0, 0, 0) (transparent).
 *   2. Rasterise the shadow rounded rect into that buffer at the shadow's
 *      premultiplied colour.
 *   3. Apply the 3-pass box blur INSIDE the temp buffer — the kernel only
 *      averages shadow content with transparent surrounds, producing a clean
 *      Gaussian falloff.
 *   4. Composite the buffer over the surface with src-over (premultiplied src
 *      on straight dst), skipping pixels inside the element border-box
 *      (exclude shape) per CSS spec.
 *
 * The surface OUTSIDE the shadow path is never read by the blur kernel, so
 * sibling element pixels remain untouched except for the legitimate shadow
 * tint that they receive via composite.
 */
typedef struct OuterShadowImage {
    uint32_t* pixels;
    int x;
    int y;
    int width;
    int height;
} OuterShadowImage;

// Both retained-image and immediate-composite shadows must rasterize and blur
// the same isolated source, or their edge falloff diverges.
static bool build_outer_shadow_image(
    ScratchArena* sa, ImageSurface* surface,
    float shadow_x, float shadow_y, float shadow_w, float shadow_h,
    float sr_tl, float sr_tr, float sr_br, float sr_bl,
    Color shadow_color, float blur_radius, OuterShadowImage* image)
{
    if (!surface || shadow_color.a == 0) return false;

    float pad = blur_radius < 0 ? 0 : blur_radius;
    int br_x0 = (int)floorf(shadow_x - pad);
    int br_y0 = (int)floorf(shadow_y - pad);
    int br_x1 = (int)ceilf(shadow_x + shadow_w + pad);
    int br_y1 = (int)ceilf(shadow_y + shadow_h + pad);
    if (br_x0 < 0) br_x0 = 0;
    if (br_y0 < 0) br_y0 = 0;
    if (br_x1 > surface->width) br_x1 = surface->width;
    if (br_y1 > surface->height) br_y1 = surface->height;
    int br_w = br_x1 - br_x0;
    int br_h = br_y1 - br_y0;
    if (br_w <= 0 || br_h <= 0) return false;

    size_t buf_n = (size_t)br_w * br_h;
    uint32_t* shadow_buf = (uint32_t*)scratch_alloc(sa, buf_n * sizeof(uint32_t));
    if (!shadow_buf) return false;
    memset(shadow_buf, 0, buf_n * sizeof(uint32_t));

    uint32_t sa_b = shadow_color.a;
    uint32_t sr_b = ((uint32_t)shadow_color.r * sa_b + 127) / 255;
    uint32_t sg_b = ((uint32_t)shadow_color.g * sa_b + 127) / 255;
    uint32_t sb_b = ((uint32_t)shadow_color.b * sa_b + 127) / 255;
    uint32_t shadow_px = (sa_b << 24) | (sb_b << 16) | (sg_b << 8) | sr_b;

    rasterise_rounded_rect_into_buffer(shadow_buf, br_w, br_h, br_x0, br_y0,
                                        shadow_x, shadow_y, shadow_w, shadow_h,
                                        sr_tl, sr_tr, sr_br, sr_bl, shadow_px);

    if (blur_radius >= 1.0f) {
        ImageSurface tmp = {};
        tmp.pixels = (void*)shadow_buf;
        tmp.width = br_w;
        tmp.height = br_h;
        tmp.pitch = br_w * 4;
        box_blur_region(sa, &tmp, 0, 0, br_w, br_h, blur_radius);
    }

    image->pixels = shadow_buf;
    image->x = br_x0;
    image->y = br_y0;
    image->width = br_w;
    image->height = br_h;
    return true;
}

void render_outer_shadow_blur_composite(
    ScratchArena* sa, ImageSurface* surface,
    float shadow_x, float shadow_y, float shadow_w, float shadow_h,
    float sr_tl, float sr_tr, float sr_br, float sr_bl,
    Color shadow_color, float blur_radius,
    int exclude_type, const float* exclude_params,
    int clip_type, const float* clip_params)
{
    if (!surface || !surface->pixels || shadow_color.a == 0) return;
    OuterShadowImage image = {};
    if (!build_outer_shadow_image(sa, surface,
            shadow_x, shadow_y, shadow_w, shadow_h,
            sr_tl, sr_tr, sr_br, sr_bl, shadow_color, blur_radius, &image)) return;
    uint32_t* shadow_buf = image.pixels;
    int br_x0 = image.x, br_y0 = image.y;
    int br_w = image.width, br_h = image.height;

    // build clip / exclude shapes for the composite pass
    bool has_exclude = (exclude_type != 0);
    bool has_clip = (clip_type != 0);
    ClipShape exclude_cs = has_exclude
        ? clip_shape_from_params(exclude_type, exclude_params)
        : ClipShape{};
    ClipShape clip_cs = has_clip
        ? clip_shape_from_params(clip_type, clip_params)
        : ClipShape{};

    // composite shadow_buf over surface (src-over, premultiplied src)
    uint32_t* surf_px = (uint32_t*)surface->pixels;
    int stride = surface->pitch / 4;
    for (int row = 0; row < br_h; row++) {
        int sy = br_y0 + row;
        for (int col = 0; col < br_w; col++) {
            uint32_t sp = shadow_buf[row * br_w + col];
            uint32_t sap = (sp >> 24) & 0xFF;
            if (sap == 0) continue;  // no shadow contribution

            int sx = br_x0 + col;
            float fx = (float)sx + 0.5f;
            float fy = (float)sy + 0.5f;
            if (has_exclude && clip_point_in_shape(&exclude_cs, fx, fy)) continue;
            if (has_clip && !clip_point_in_shape(&clip_cs, fx, fy)) continue;

            uint32_t dp = surf_px[sy * stride + sx];
            uint32_t dr = (dp >>  0) & 0xFF;
            uint32_t dg = (dp >>  8) & 0xFF;
            uint32_t db = (dp >> 16) & 0xFF;
            uint32_t da = (dp >> 24) & 0xFF;

            uint32_t srp = (sp >>  0) & 0xFF;
            uint32_t sgp = (sp >>  8) & 0xFF;
            uint32_t sbp = (sp >> 16) & 0xFF;

            uint32_t inv = 255u - sap;  // (1 - src_a) * 255
            // src-over: result_premult = src_p + dst_premult * inv / 255
            //           dst_premult.rgb = dst.rgb * dst.a / 255
            //           result_a       = src_a + dst_a * inv / 255
            uint32_t ra = sap + (da * inv + 127u) / 255u;
            if (ra == 0) {
                surf_px[sy * stride + sx] = 0;
                continue;
            }
            uint32_t rrp = srp + (dr * da * inv + 32512u) / 65025u;
            uint32_t rgp = sgp + (dg * da * inv + 32512u) / 65025u;
            uint32_t rbp = sbp + (db * da * inv + 32512u) / 65025u;
            // convert back to straight alpha
            uint32_t rr = (rrp * 255u + ra / 2u) / ra;
            uint32_t rg = (rgp * 255u + ra / 2u) / ra;
            uint32_t rb = (rbp * 255u + ra / 2u) / ra;
            if (rr > 255) rr = 255;
            if (rg > 255) rg = 255;
            if (rb > 255) rb = 255;
            if (ra > 255) ra = 255;
            surf_px[sy * stride + sx] =
                (ra << 24) | (rb << 16) | (rg << 8) | rr;
        }
    }
}

static uint32_t* render_outer_shadow_blur_image(
    ScratchArena* sa, ImageSurface* surface,
    float shadow_x, float shadow_y, float shadow_w, float shadow_h,
    float sr_tl, float sr_tr, float sr_br, float sr_bl,
    Color shadow_color, float blur_radius,
    int exclude_type, const float* exclude_params,
    int* out_x, int* out_y, int* out_w, int* out_h)
{
    if (out_x) *out_x = 0;
    if (out_y) *out_y = 0;
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (!sa || !surface || shadow_color.a == 0) return nullptr;

    OuterShadowImage image = {};
    if (!build_outer_shadow_image(sa, surface,
            shadow_x, shadow_y, shadow_w, shadow_h,
            sr_tl, sr_tr, sr_br, sr_bl, shadow_color, blur_radius, &image)) return nullptr;
    uint32_t* shadow_buf = image.pixels;
    int br_x0 = image.x, br_y0 = image.y;
    int br_w = image.width, br_h = image.height;

    bool has_exclude = (exclude_type != 0);
    ClipShape exclude_cs = has_exclude
        ? clip_shape_from_params(exclude_type, exclude_params)
        : ClipShape{};
    if (has_exclude) {
        for (int row = 0; row < br_h; row++) {
            int sy = br_y0 + row;
            for (int col = 0; col < br_w; col++) {
                int sx = br_x0 + col;
                float fx = (float)sx + 0.5f;
                float fy = (float)sy + 0.5f;
                if (clip_point_in_shape(&exclude_cs, fx, fy)) {
                    shadow_buf[row * br_w + col] = 0;
                }
            }
        }
    }

    if (out_x) *out_x = br_x0;
    if (out_y) *out_y = br_y0;
    if (out_w) *out_w = br_w;
    if (out_h) *out_h = br_h;
    return shadow_buf;
}

/**
 * Render box-shadow effects for an element (CSS Backgrounds Level 3 §7)
 *
 * Multiple shadows are rendered in reverse order (last specified = bottommost).
 *
 * Algorithm:
 * 1. For each shadow (in reverse order for proper stacking):
 *    - Calculate shadow rectangle (element rect + offsets + spread)
 *    - If blur > 0: render shadow shape then apply 3-pass box blur
 *    - If blur == 0: render sharp shadow rectangle
 *    - Apply border-radius if the element has rounded corners
 * 2. Inset shadows are rendered after background (inside the element)
 */
void render_box_shadow(RenderContext* rdcon, ViewBlock* view, Rect rect) {
    if (!view->bound || !view->bound->box_shadow) return;

    // Count shadows and collect into array for reverse iteration
    int shadow_count = 0;
    BoxShadow* shadow = view->bound->box_shadow;
    while (shadow) {
        shadow_count++;
        shadow = shadow->next;
    }

    if (shadow_count == 0) return;

    // Shadow lists come from parsed CSS, so do not let author input size a stack frame.
    BoxShadow** shadows = (BoxShadow**)scratch_calloc(&rdcon->scratch, (size_t)shadow_count * sizeof(BoxShadow*));
    if (!shadows) {
        log_error("[BOX-SHADOW] failed to allocate shadow list for %d shadow(s)", shadow_count);
        return;
    }
    shadow = view->bound->box_shadow;
    for (int i = 0; i < shadow_count; i++) {
        shadows[i] = shadow;
        shadow = shadow->next;
    }

    // Scale factor: rect is in physical pixels but shadow props and border radii
    // are in CSS pixels.  Multiply by rdcon->scale to convert to physical pixels.
    float sc = rdcon->scale;

    // Get border radius if present (scaled to physical pixels)
    float r_tl = 0, r_tr = 0, r_br = 0, r_bl = 0;
    if (view->bound && view->bound->border) {
        BorderProp* border = view->bound->border;
        r_tl = border->radius.top_left * sc;
        r_tr = border->radius.top_right * sc;
        r_br = border->radius.bottom_right * sc;
        r_bl = border->radius.bottom_left * sc;
    }

    const RdtMatrix* xform = render_state_current_transform(rdcon);

    // Render outer shadows first (in reverse order - last shadow is bottommost)
    for (int i = shadow_count - 1; i >= 0; i--) {
        BoxShadow* s = shadows[i];
        if (s->inset) continue;  // Skip inset shadows for now

        // Scale shadow properties from CSS pixels to physical pixels
        float s_offset_x = s->offset_x * sc;
        float s_offset_y = s->offset_y * sc;
        float s_blur     = s->blur_radius * sc;
        float s_spread   = s->spread_radius * sc;

        // Calculate shadow rectangle
        float shadow_x = rect.x + s_offset_x - s_spread;
        float shadow_y = rect.y + s_offset_y - s_spread;
        float shadow_w = rect.width + 2 * s_spread;
        float shadow_h = rect.height + 2 * s_spread;

        // Adjust border radius for spread
        float spread_factor = (s_spread >= 0) ? 1.0f : 1.0f;
        float sr_tl = max(0.0f, r_tl + s_spread * spread_factor);
        float sr_tr = max(0.0f, r_tr + s_spread * spread_factor);
        float sr_br = max(0.0f, r_br + s_spread * spread_factor);
        float sr_bl = max(0.0f, r_bl + s_spread * spread_factor);

        log_debug("[BOX-SHADOW] Rendering shadow: offset=(%.1f,%.1f) blur=%.1f spread=%.1f scale=%.1f color=#%02x%02x%02x%02x",
                  s_offset_x, s_offset_y, s_blur, s_spread, sc,
                  s->color.r, s->color.g, s->color.b, s->color.a);

        // Serialize CSS clip-path shape (if active) for masking the shadow
        int clip_type = 0;
        float clip_params[8] = {};
        if (rdcon->clip_shape_depth > 0) {
            clip_shape_to_params(rdcon->clip_shapes[rdcon->clip_shape_depth - 1],
                                 &clip_type, clip_params);
        }

        // Element border-box (exclude shape: per CSS spec, outer shadow is not
        // visible inside the element border-box).
        int exclude_type = CLIP_SHAPE_ROUNDED_RECT;
        float exclude_params[8];
        exclude_params[0] = rect.x;     exclude_params[1] = rect.y;
        exclude_params[2] = rect.width; exclude_params[3] = rect.height;
        exclude_params[4] = r_tl;       exclude_params[5] = r_tr;
        exclude_params[6] = r_br;       exclude_params[7] = r_bl;

        if (s_blur >= 1.0f) {
            // Blur > 0: use isolated temp-buffer pipeline (rasterise + blur +
            // composite) so the blur kernel doesn't smear neighbouring pixels.
            if (xform && rdcon->ui_context->surface) {
                assert(rdcon->dl && "transformed box-shadow requires display-list recording");
                ScratchArena* shadow_arena = &rdcon->dl->arena;
                int sh_x = 0, sh_y = 0, sh_w = 0, sh_h = 0;
                uint32_t* shadow_pixels = render_outer_shadow_blur_image(
                    shadow_arena, rdcon->ui_context->surface,
                    shadow_x, shadow_y, shadow_w, shadow_h,
                    sr_tl, sr_tr, sr_br, sr_bl,
                    s->color, s_blur,
                    exclude_type, exclude_params,
                    &sh_x, &sh_y, &sh_w, &sh_h);
                if (shadow_pixels && sh_w > 0 && sh_h > 0) {
                    // rc_draw_image records uint32_t row stride; shadow pixels are tightly packed.
                    rc_draw_image(rdcon, shadow_pixels, sh_w, sh_h, sh_w,
                                  (float)sh_x, (float)sh_y, (float)sh_w, (float)sh_h,
                                  255, xform);
                }
            } else {
                rc_outer_shadow(rdcon,
                                shadow_x, shadow_y, shadow_w, shadow_h,
                                sr_tl, sr_tr, sr_br, sr_bl,
                                s->color, s_blur,
                                exclude_type, exclude_params,
                                clip_type, clip_params);
            }
            log_debug("[BOX-SHADOW] Applied isolated-buffer blur radius=%.1f on shadow rect (%.1f,%.1f,%.1f,%.1f)",
                      s_blur, shadow_x, shadow_y, shadow_w, shadow_h);
        } else {
            // No blur (or sub-pixel blur, treated as 0 per CSS): paint the
            // shadow path directly.  For rounded elements we still need to
            // restore the inside of the border-box because the shadow path
            // covers the same area as the element.
            bool need_shadow_clip = (r_tl > 0 || r_tr > 0 || r_br > 0 || r_bl > 0);
            int save_rx = 0, save_ry = 0, save_rw = 0, save_rh = 0;
            if (need_shadow_clip) {
                save_rx = (int)floorf(rect.x);
                save_ry = (int)floorf(rect.y);
                save_rw = (int)ceilf(rect.x + rect.width) - save_rx;
                save_rh = (int)ceilf(rect.y + rect.height) - save_ry;
                rc_shadow_clip_save(rdcon, save_rx, save_ry, save_rw, save_rh);
            }

            // Build shadow path
            Rect shadow_rect = {shadow_x, shadow_y, shadow_w, shadow_h};
            RdtPath* shadow_path = nullptr;
            if (sr_tl > 0 || sr_tr > 0 || sr_br > 0 || sr_bl > 0) {
                Corner shadow_radius = {};
                shadow_radius.top_left = sr_tl;
                shadow_radius.top_right = sr_tr;
                shadow_radius.bottom_right = sr_br;
                shadow_radius.bottom_left = sr_bl;
                shadow_radius.top_left_y = sr_tl;
                shadow_radius.top_right_y = sr_tr;
                shadow_radius.bottom_right_y = sr_br;
                shadow_radius.bottom_left_y = sr_bl;
                shadow_path = render_path_create_rounded_rect(shadow_rect, &shadow_radius);
            } else {
                shadow_path = render_path_create_rounded_rect(shadow_rect, nullptr);
            }

            RdtPath* clip_path = render_path_create_clip_path(rdcon);
            rc_push_clip(rdcon, clip_path, xform);
            rc_fill_path(rdcon, shadow_path, s->color, RDT_FILL_WINDING, xform);
            rc_pop_clip(rdcon);
            rdt_path_free(shadow_path);
            rdt_path_free(clip_path);

            if (need_shadow_clip) {
                rc_shadow_clip_restore(rdcon, exclude_type, exclude_params,
                                       save_rx, save_ry, save_rw, save_rh, 1);
            }
        }
    }

    scratch_free(&rdcon->scratch, shadows);
    log_debug("[BOX-SHADOW] Rendered %d outer shadow(s)", shadow_count);
}

/**
 * Render inset box-shadow effects (CSS Backgrounds Level 3 §7)
 *
 * Inset shadows are rendered AFTER the background, inside the element's padding box.
 *
 * Algorithm:
 * 1. Fill a ring INSIDE the element: outer = element border-box, inner = element
 *    rect inset by (blur_radius + spread) and shifted by offset.  The ring provides
 *    enough material (blur_radius wide) for the Gaussian to produce a smooth gradient.
 * 2. Use box_blur_region_inset: copies expanded region to a temp buffer, fills the
 *    outer padding with the element's background color (so blur kernel sees the
 *    correct base color at element edges), blurs in-place, then copies the inner
 *    rect back to the surface.  This avoids edge-clamping artifacts.
 */
void render_box_shadow_inset(RenderContext* rdcon, ViewBlock* view, Rect rect) {
    if (!view->bound || !view->bound->box_shadow) return;

    // Count inset shadows
    int shadow_count = 0;
    BoxShadow* shadow = view->bound->box_shadow;
    while (shadow) {
        if (shadow->inset) shadow_count++;
        shadow = shadow->next;
    }
    if (shadow_count == 0) return;

    // Shadow lists come from parsed CSS, so do not let author input size a stack frame.
    BoxShadow** shadows = (BoxShadow**)scratch_calloc(&rdcon->scratch, (size_t)shadow_count * sizeof(BoxShadow*));
    if (!shadows) {
        log_error("[BOX-SHADOW INSET] failed to allocate shadow list for %d shadow(s)", shadow_count);
        return;
    }
    shadow = view->bound->box_shadow;
    int idx = 0;
    while (shadow) {
        if (shadow->inset) shadows[idx++] = shadow;
        shadow = shadow->next;
    }

    // Scale factor: rect is in physical pixels but shadow props and border radii
    // are in CSS pixels.  Multiply by rdcon->scale to convert to physical pixels.
    float sc = rdcon->scale;

    // Get border radius if present (scaled to physical pixels)
    float r_tl = 0, r_tr = 0, r_br = 0, r_bl = 0;
    if (view->bound->border) {
        r_tl = view->bound->border->radius.top_left * sc;
        r_tr = view->bound->border->radius.top_right * sc;
        r_br = view->bound->border->radius.bottom_right * sc;
        r_bl = view->bound->border->radius.bottom_left * sc;
    }

    const RdtMatrix* xform = render_state_current_transform(rdcon);

    // Render inset shadows in reverse order (last specified = bottommost)
    for (int i = shadow_count - 1; i >= 0; i--) {
        BoxShadow* s = shadows[i];

        // Scale shadow properties from CSS pixels to physical pixels
        float s_offset_x = s->offset_x * sc;
        float s_offset_y = s->offset_y * sc;
        float s_blur     = s->blur_radius * sc;
        float s_spread   = s->spread_radius * sc;

        // Band = blur/2 + spread.  This provides enough fill material for the
        // Gaussian to produce a smooth gradient while avoiding over-darkening.
        // box_blur_region_inset pads outside the element with bg_color so the
        // blur kernel sees the correct base color.
        float band = s_blur * 0.5f + s_spread;
        float inner_x = rect.x + s_offset_x + band;
        float inner_y = rect.y + s_offset_y + band;
        float inner_w = rect.width  - 2 * band;
        float inner_h = rect.height - 2 * band;

        // Use full shadow color (no alpha reduction needed — box_blur_region_inset
        // handles the edge falloff by painting element bg color in the padding area)
        Color fill_color = s->color;

        if (inner_w <= 0 || inner_h <= 0) {
            // Shadow band consumes entire element — fill with adjusted shadow color
            RdtPath* fill_path = background_rect_path(rect);
            RdtPath* clip_path = background_rect_path(rect);

            rc_push_clip(rdcon, clip_path, xform);
            rc_fill_path(rdcon, fill_path, fill_color, RDT_FILL_WINDING, xform);
            rc_pop_clip(rdcon);
            rdt_path_free(fill_path);
            rdt_path_free(clip_path);
            continue;
        }

        // Adjust border radii for inner cutout (shrink by band width)
        float ir_tl = max(0.0f, r_tl - band);
        float ir_tr = max(0.0f, r_tr - band);
        float ir_br = max(0.0f, r_br - band);
        float ir_bl = max(0.0f, r_bl - band);

        log_debug("[BOX-SHADOW INSET] Rendering inset shadow: offset=(%.1f,%.1f) blur=%.1f spread=%.1f band=%.1f scale=%.1f alpha=%d color=#%02x%02x%02x%02x",
                  s_offset_x, s_offset_y, s_blur, s_spread, band, sc, fill_color.a,
                  fill_color.r, fill_color.g, fill_color.b, fill_color.a);

        // Even-odd fill: outer (CW) = element border-box, inner (CCW) = inset cutout
        RdtPath* shadow_path = rdt_path_new();

        // Outer path (clockwise): element border-box
        if (r_tl > 0 || r_tr > 0 || r_br > 0 || r_bl > 0) {
            #define KAPPA_INSET 0.5522847498f
            rdt_path_move_to(shadow_path, rect.x + r_tl, rect.y);
            rdt_path_line_to(shadow_path, rect.x + rect.width - r_tr, rect.y);
            if (r_tr > 0) rdt_path_cubic_to(shadow_path,
                rect.x + rect.width - r_tr + r_tr * KAPPA_INSET, rect.y,
                rect.x + rect.width, rect.y + r_tr - r_tr * KAPPA_INSET,
                rect.x + rect.width, rect.y + r_tr);
            rdt_path_line_to(shadow_path, rect.x + rect.width, rect.y + rect.height - r_br);
            if (r_br > 0) rdt_path_cubic_to(shadow_path,
                rect.x + rect.width, rect.y + rect.height - r_br + r_br * KAPPA_INSET,
                rect.x + rect.width - r_br + r_br * KAPPA_INSET, rect.y + rect.height,
                rect.x + rect.width - r_br, rect.y + rect.height);
            rdt_path_line_to(shadow_path, rect.x + r_bl, rect.y + rect.height);
            if (r_bl > 0) rdt_path_cubic_to(shadow_path,
                rect.x + r_bl - r_bl * KAPPA_INSET, rect.y + rect.height,
                rect.x, rect.y + rect.height - r_bl + r_bl * KAPPA_INSET,
                rect.x, rect.y + rect.height - r_bl);
            rdt_path_line_to(shadow_path, rect.x, rect.y + r_tl);
            if (r_tl > 0) rdt_path_cubic_to(shadow_path,
                rect.x, rect.y + r_tl - r_tl * KAPPA_INSET,
                rect.x + r_tl - r_tl * KAPPA_INSET, rect.y,
                rect.x + r_tl, rect.y);
            rdt_path_close(shadow_path);
            #undef KAPPA_INSET
        } else {
            rdt_path_move_to(shadow_path, rect.x, rect.y);
            rdt_path_line_to(shadow_path, rect.x + rect.width, rect.y);
            rdt_path_line_to(shadow_path, rect.x + rect.width, rect.y + rect.height);
            rdt_path_line_to(shadow_path, rect.x, rect.y + rect.height);
            rdt_path_close(shadow_path);
        }

        // Inner path (counter-clockwise): the cutout hole
        {
            float ix = inner_x, iy = inner_y, iw = inner_w, ih = inner_h;
            if (ir_tl > 0 || ir_tr > 0 || ir_br > 0 || ir_bl > 0) {
                #define KAPPA_INNER 0.5522847498f
                rdt_path_move_to(shadow_path, ix + ir_tl, iy);
                if (ir_tl > 0) rdt_path_cubic_to(shadow_path,
                    ix + ir_tl - ir_tl * KAPPA_INNER, iy,
                    ix, iy + ir_tl - ir_tl * KAPPA_INNER,
                    ix, iy + ir_tl);
                rdt_path_line_to(shadow_path, ix, iy + ih - ir_bl);
                if (ir_bl > 0) rdt_path_cubic_to(shadow_path,
                    ix, iy + ih - ir_bl + ir_bl * KAPPA_INNER,
                    ix + ir_bl - ir_bl * KAPPA_INNER, iy + ih,
                    ix + ir_bl, iy + ih);
                rdt_path_line_to(shadow_path, ix + iw - ir_br, iy + ih);
                if (ir_br > 0) rdt_path_cubic_to(shadow_path,
                    ix + iw - ir_br + ir_br * KAPPA_INNER, iy + ih,
                    ix + iw, iy + ih - ir_br + ir_br * KAPPA_INNER,
                    ix + iw, iy + ih - ir_br);
                rdt_path_line_to(shadow_path, ix + iw, iy + ir_tr);
                if (ir_tr > 0) rdt_path_cubic_to(shadow_path,
                    ix + iw, iy + ir_tr - ir_tr * KAPPA_INNER,
                    ix + iw - ir_tr + ir_tr * KAPPA_INNER, iy,
                    ix + iw - ir_tr, iy);
                rdt_path_close(shadow_path);
                #undef KAPPA_INNER
            } else {
                rdt_path_move_to(shadow_path, ix, iy);
                rdt_path_line_to(shadow_path, ix, iy + ih);
                rdt_path_line_to(shadow_path, ix + iw, iy + ih);
                rdt_path_line_to(shadow_path, ix + iw, iy);
                rdt_path_close(shadow_path);
            }
        }

        // For rounded elements, the inset blur operates on a rectangular region
        // that extends into the rounded corners. Save corner pixels before fill+blur
        // and restore them after, so the blur doesn't leak outside the rounded border.
        bool inset_need_clip = (r_tl > 0 || r_tr > 0 || r_br > 0 || r_bl > 0) && s_blur > 0;
        int inset_save_rx = 0, inset_save_ry = 0, inset_save_rw = 0, inset_save_rh = 0;
        int inset_clip_type = 0;
        float inset_clip_params[8] = {};
        if (inset_need_clip) {
            inset_save_rx = (int)floorf(rect.x);
            inset_save_ry = (int)floorf(rect.y);
            inset_save_rw = (int)ceilf(rect.x + rect.width) - inset_save_rx;
            inset_save_rh = (int)ceilf(rect.y + rect.height) - inset_save_ry;
            inset_clip_type = CLIP_SHAPE_ROUNDED_RECT;
            inset_clip_params[0] = rect.x;  inset_clip_params[1] = rect.y;
            inset_clip_params[2] = rect.width; inset_clip_params[3] = rect.height;
            inset_clip_params[4] = r_tl; inset_clip_params[5] = r_tr;
            inset_clip_params[6] = r_br; inset_clip_params[7] = r_bl;
            rc_shadow_clip_save(rdcon, inset_save_rx, inset_save_ry, inset_save_rw, inset_save_rh);
        }

        // Clip to element boundary and fill
        RdtPath* clip_path = background_rect_path(rect);
        rc_push_clip(rdcon, clip_path, xform);
        rc_fill_path(rdcon, shadow_path, fill_color, RDT_FILL_EVEN_ODD, xform);
        rc_pop_clip(rdcon);
        rdt_path_free(shadow_path);
        rdt_path_free(clip_path);

        // Apply box blur within element rect
        if (s_blur > 0) {
            float blur_px = s_blur;
            int br_x = (int)floorf(rect.x);
            int br_y = (int)floorf(rect.y);
            int br_w = (int)ceilf(rect.width);
            int br_h = (int)ceilf(rect.height);
            int pad = (int)ceilf(blur_px);

            // Get element background color for the blur padding area.
            // This ensures the blur kernel at element edges sees the correct base
            // color instead of the parent's background.
            Color bg;
            bg.r = 255; bg.g = 255; bg.b = 255; bg.a = 255;  // default white
            if (view->bound && view->bound->background) {
                bg = view->bound->background->color;
            }
            // Convert to surface pixel format (ABGR8888)
            uint32_t bg_pixel = ((uint32_t)bg.a << 24) | ((uint32_t)bg.b << 16) |
                                ((uint32_t)bg.g << 8) | (uint32_t)bg.r;

            rc_box_blur_inset(rdcon, br_x, br_y, br_w, br_h, pad, blur_px, bg_pixel);
            log_debug("[BOX-SHADOW INSET] Applied inset blur radius=%.1f pad=%d bg=#%08x on region (%d,%d,%d,%d)",
                      blur_px, pad, bg_pixel, br_x, br_y, br_w, br_h);
        }

        // Restore corner pixels outside rounded border-box after inset blur
        if (inset_need_clip) {
            rc_shadow_clip_restore(rdcon, inset_clip_type, inset_clip_params,
                                   inset_save_rx, inset_save_ry, inset_save_rw, inset_save_rh, 0);
        }
    }

    scratch_free(&rdcon->scratch, shadows);
    log_debug("[BOX-SHADOW INSET] Rendered %d inset shadow(s)", shadow_count);
}

/**
 * Compute background image dimensions based on background-size property.
 * Returns the computed width and height of the background image in physical pixels.
 */
static bool background_size_unspecified(BackgroundProp* bg);

static void compute_bg_image_size(BackgroundProp* bg, float img_w, float img_h,
                                   float box_w, float box_h, float* out_w, float* out_h) {
    float aspect = (img_h > 0) ? img_w / img_h : 1.0f;

    if (background_size_unspecified(bg)) {
        // CSS initial value is `auto auto`: use the image's intrinsic size.
        // BackgroundProp fields are zero-initialized, so an omitted
        // background-size must not fall into the explicit-size branch below.
        *out_w = img_w;
        *out_h = img_h;
    } else if (bg->bg_size_type == CSS_VALUE_COVER) {
        // Scale to cover the entire box, preserving aspect ratio
        float scale_x = box_w / img_w;
        float scale_y = box_h / img_h;
        float scale = (scale_x > scale_y) ? scale_x : scale_y;
        *out_w = img_w * scale;
        *out_h = img_h * scale;
    } else if (bg->bg_size_type == CSS_VALUE_CONTAIN) {
        // Scale to fit within the box, preserving aspect ratio
        float scale_x = box_w / img_w;
        float scale_y = box_h / img_h;
        float scale = (scale_x < scale_y) ? scale_x : scale_y;
        *out_w = img_w * scale;
        *out_h = img_h * scale;
    } else if (bg->bg_size_type == CSS_VALUE_AUTO ||
               (bg->bg_size_width_auto && bg->bg_size_height_auto)) {
        // auto auto: use intrinsic size
        *out_w = img_w;
        *out_h = img_h;
    } else {
        // Explicit size values
        float w, h;
        bool w_auto = bg->bg_size_width_auto;
        bool h_auto = bg->bg_size_height_auto;

        if (!w_auto) {
            w = bg->bg_size_width_is_percent ? (bg->bg_size_width / 100.0f) * box_w : bg->bg_size_width;
        }
        if (!h_auto) {
            h = bg->bg_size_height_is_percent ? (bg->bg_size_height / 100.0f) * box_h : bg->bg_size_height;
        }

        if (w_auto && !h_auto) {
            // auto <height>: width scales proportionally
            *out_h = h;
            *out_w = h * aspect;
        } else if (!w_auto && h_auto) {
            // <width> auto: height scales proportionally
            *out_w = w;
            *out_h = w / aspect;
        } else {
            // Both explicit
            *out_w = w;
            *out_h = h;
        }
    }
}

/**
 * Compute background image position based on background-position property.
 * Per CSS spec: percentage position = (container_size - image_size) * percentage / 100
 */
static void compute_bg_image_position(BackgroundProp* bg, float img_w, float img_h,
                                       float box_w, float box_h, float* out_x, float* out_y) {
    if (!bg->bg_position_set) {
        // Default: 0% 0% (top-left)
        *out_x = 0.0f;
        *out_y = 0.0f;
        return;
    }

    if (bg->bg_position_x_is_percent) {
        *out_x = (box_w - img_w) * bg->bg_position_x / 100.0f;
    } else {
        *out_x = bg->bg_position_x;
    }

    if (bg->bg_position_y_is_percent) {
        *out_y = (box_h - img_h) * bg->bg_position_y / 100.0f;
    } else {
        *out_y = bg->bg_position_y;
    }
}

static bool background_size_unspecified(BackgroundProp* bg) {
    return !bg->bg_size_type &&
           !bg->bg_size_width_auto &&
           !bg->bg_size_height_auto &&
           !bg->bg_size_width_is_percent &&
           !bg->bg_size_height_is_percent &&
           bg->bg_size_width == 0.0f &&
           bg->bg_size_height == 0.0f;
}

typedef struct {
    float origin_x;
    float origin_y;
    float tile_w;
    float tile_h;
    int start_col;
    int end_col;
    int start_row;
    int end_row;
    float space_gap_x;
    float space_gap_y;
    CssEnum repeat_x;
    CssEnum repeat_y;
} BackgroundTilePlan;

static bool compute_background_tile_plan(BackgroundProp* bg, Rect position_rect, Rect coverage_rect,
                                         float intrinsic_w, float intrinsic_h,
                                         BackgroundTilePlan* plan) {
    if (!bg || !plan ||
        position_rect.width <= 0.0f || position_rect.height <= 0.0f ||
        coverage_rect.width <= 0.0f || coverage_rect.height <= 0.0f) {
        return false;
    }

    compute_bg_image_size(bg, intrinsic_w, intrinsic_h,
                          position_rect.width, position_rect.height,
                          &plan->tile_w, &plan->tile_h);
    if (plan->tile_w <= 0.0f || plan->tile_h <= 0.0f) return false;

    float pos_x = 0.0f;
    float pos_y = 0.0f;
    compute_bg_image_position(bg, plan->tile_w, plan->tile_h,
                              position_rect.width, position_rect.height, &pos_x, &pos_y);

    plan->repeat_x = bg->bg_repeat_x ? bg->bg_repeat_x : CSS_VALUE_REPEAT;
    plan->repeat_y = bg->bg_repeat_y ? bg->bg_repeat_y : CSS_VALUE_REPEAT;
    plan->origin_x = position_rect.x + pos_x;
    plan->origin_y = position_rect.y + pos_y;
    plan->space_gap_x = 0.0f;
    plan->space_gap_y = 0.0f;

    if (plan->repeat_x == CSS_VALUE_ROUND && plan->tile_w > 0.0f) {
        int count = (int)(position_rect.width / plan->tile_w + 0.5f);
        if (count < 1) count = 1;
        plan->tile_w = position_rect.width / count;
    }
    if (plan->repeat_y == CSS_VALUE_ROUND && plan->tile_h > 0.0f) {
        int count = (int)(position_rect.height / plan->tile_h + 0.5f);
        if (count < 1) count = 1;
        plan->tile_h = position_rect.height / count;
    }

    if (plan->repeat_x == CSS_VALUE_NO_REPEAT) {
        plan->start_col = 0; plan->end_col = 0;
    } else if (plan->repeat_x == CSS_VALUE_SPACE) {
        plan->start_col = 0;
        plan->end_col = (plan->tile_w > 0.0f) ? (int)(position_rect.width / plan->tile_w) - 1 : 0;
        if (plan->end_col < 0) plan->end_col = 0;
    } else {
        plan->start_col = (int)floorf((coverage_rect.x - plan->origin_x) / plan->tile_w);
        plan->end_col = (int)ceilf((coverage_rect.x + coverage_rect.width - plan->origin_x) / plan->tile_w) - 1;
    }

    if (plan->repeat_y == CSS_VALUE_NO_REPEAT) {
        plan->start_row = 0; plan->end_row = 0;
    } else if (plan->repeat_y == CSS_VALUE_SPACE) {
        plan->start_row = 0;
        plan->end_row = (plan->tile_h > 0.0f) ? (int)(position_rect.height / plan->tile_h) - 1 : 0;
        if (plan->end_row < 0) plan->end_row = 0;
    } else {
        plan->start_row = (int)floorf((coverage_rect.y - plan->origin_y) / plan->tile_h);
        plan->end_row = (int)ceilf((coverage_rect.y + coverage_rect.height - plan->origin_y) / plan->tile_h) - 1;
    }

    if (plan->repeat_x == CSS_VALUE_SPACE && plan->end_col > 0) {
        plan->space_gap_x = (position_rect.width - plan->tile_w * (plan->end_col + 1)) / plan->end_col;
    }
    if (plan->repeat_y == CSS_VALUE_SPACE && plan->end_row > 0) {
        plan->space_gap_y = (position_rect.height - plan->tile_h * (plan->end_row + 1)) / plan->end_row;
    }
    return true;
}

static void background_tile_rect(const BackgroundTilePlan* plan, Rect rect,
                                 int row, int col, Rect* tile_rect) {
    if (plan->repeat_x == CSS_VALUE_SPACE) {
        tile_rect->x = rect.x + col * (plan->tile_w + plan->space_gap_x);
    } else {
        tile_rect->x = plan->origin_x + col * plan->tile_w;
    }
    if (plan->repeat_y == CSS_VALUE_SPACE) {
        tile_rect->y = rect.y + row * (plan->tile_h + plan->space_gap_y);
    } else {
        tile_rect->y = plan->origin_y + row * plan->tile_h;
    }
    tile_rect->width = plan->tile_w;
    tile_rect->height = plan->tile_h;
}

static bool background_tile_outside_rect(const Rect* tile_rect, const Rect* rect) {
    return tile_rect->x + tile_rect->width <= rect->x ||
           tile_rect->x >= rect->x + rect->width ||
           tile_rect->y + tile_rect->height <= rect->y ||
           tile_rect->y >= rect->y + rect->height;
}

typedef void (*BackgroundTileCallback)(const Rect* tile_rect, void* userdata);

static void background_for_each_tile(const BackgroundTilePlan* plan,
                                     Rect tile_basis_rect,
                                     Rect paint_rect,
                                     BackgroundTileCallback callback,
                                     void* userdata) {
    if (!plan || !callback) return;
    for (int row = plan->start_row; row <= plan->end_row; row++) {
        for (int col = plan->start_col; col <= plan->end_col; col++) {
            Rect tile_rect;
            background_tile_rect(plan, tile_basis_rect, row, col, &tile_rect);
            if (background_tile_outside_rect(&tile_rect, &paint_rect)) continue;
            callback(&tile_rect, userdata);
        }
    }
}

typedef struct {
    RenderContext* rdcon;
    ViewBlock* view;
    LinearGradient* gradient;
    Rect paint_rect;
} LinearGradientTileContext;

static void render_linear_gradient_tile_cb(const Rect* tile_rect, void* userdata) {
    LinearGradientTileContext* ctx = (LinearGradientTileContext*)userdata;
    render_linear_gradient_tile(ctx->rdcon, ctx->view, ctx->gradient,
                                *tile_rect, ctx->paint_rect);
}

typedef struct {
    RenderContext* rdcon;
    ViewBlock* view;
    RadialGradient* gradient;
} RadialGradientTileContext;

static void render_radial_gradient_tile_cb(const Rect* tile_rect, void* userdata) {
    RadialGradientTileContext* ctx = (RadialGradientTileContext*)userdata;
    render_radial_gradient(ctx->rdcon, ctx->view, ctx->gradient, *tile_rect);
}

static void render_linear_gradient_layer(RenderContext* rdcon, ViewBlock* view,
                                         BackgroundProp* bg, LinearGradient* gradient,
                                         Rect position_rect, Rect paint_rect) {
    if (!gradient) return;
    Rect tile_basis_rect = gradient->is_repeating ? position_rect : paint_rect;
    BackgroundTilePlan plan = {};
    if (!compute_background_tile_plan(bg, tile_basis_rect, paint_rect,
                                      tile_basis_rect.width, tile_basis_rect.height, &plan)) {
        return;
    }

    LinearGradientTileContext tile_ctx = {rdcon, view, gradient, paint_rect};
    background_for_each_tile(&plan, tile_basis_rect, paint_rect,
                             render_linear_gradient_tile_cb, &tile_ctx);
}

static void render_radial_gradient_layer(RenderContext* rdcon, ViewBlock* view,
                                         BackgroundProp* bg, RadialGradient* gradient,
                                         Rect position_rect, Rect paint_rect) {
    if (!gradient) return;
    (void)position_rect;
    BackgroundTilePlan plan = {};
    if (!compute_background_tile_plan(bg, paint_rect, paint_rect,
                                      paint_rect.width, paint_rect.height, &plan)) {
        return;
    }

    RadialGradientTileContext tile_ctx = {rdcon, view, gradient};
    background_for_each_tile(&plan, paint_rect, paint_rect,
                             render_radial_gradient_tile_cb, &tile_ctx);
}

/**
 * Render a single tile of a background image using the raster blit path.
 */
static void blit_bg_tile(RenderContext* rdcon, ImageSurface* img, ImageSurface* dst, Rect* tile_rect, Bound* clip,
                         ScaleMode mode = SCALE_MODE_LINEAR,
                         ClipShape** clip_shapes = nullptr, int clip_depth = 0) {
    render_painter_blit_surface_scaled(rdcon, img, NULL, dst, tile_rect, clip, mode, clip_shapes, clip_depth);
}

static int background_image_clip_shapes(RenderContext* rdcon, ViewBlock* view,
                                        ClipShape* rounded_shape,
                                        ClipShape** out_shapes) {
    int depth = 0;
    if (!rdcon || !out_shapes) return 0;

    for (int i = 0; i < rdcon->clip_shape_depth && depth < RDT_MAX_CLIP_SHAPES; i++) {
        out_shapes[depth++] = rdcon->clip_shapes[i];
    }

    if (depth >= RDT_MAX_CLIP_SHAPES) {
        return depth;
    }

    Bound* clip = &rdcon->block.clip;
    float clip_w = clip->right - clip->left;
    float clip_h = clip->bottom - clip->top;
    if (clip_w <= 0 || clip_h <= 0) return depth;

    Rect clip_rect = {clip->left, clip->top, clip_w, clip_h};
    Corner radius;
    if (!background_rounded_radius(view, clip_rect, &radius)) return depth;
    rounded_shape->type = CLIP_SHAPE_ROUNDED_RECT;
    rounded_shape->rounded_rect = {
        clip->left, clip->top, clip_w, clip_h,
        radius.top_left, radius.top_right, radius.bottom_right, radius.bottom_left
    };
    out_shapes[depth++] = rounded_shape;
    return depth;
}

static RdtPath* background_image_clip_path(RenderContext* rdcon, ViewBlock* view) {
    if (!rdcon) return nullptr;
    Bound* clip = &rdcon->block.clip;
    float clip_w = clip->right - clip->left;
    float clip_h = clip->bottom - clip->top;
    if (clip_w <= 0 || clip_h <= 0) {
        return render_path_create_clip_path(rdcon);
    }

    Rect clip_rect = {clip->left, clip->top, clip_w, clip_h};
    Corner radius;
    if (!background_rounded_radius(view, clip_rect, &radius)) {
        return render_path_create_clip_path(rdcon);
    }
    return render_path_create_rounded_rect(clip_rect, &radius);
}

/**
 * Render a single tile of a background image using the vector API (for SVG images).
 */
static void render_bg_tile_tvg(RenderContext* rdcon, ViewBlock* view, ImageSurface* img, Rect* tile_rect) {
    if (!img->pic) return;

    RdtPicture* pic = rdt_picture_dup(img->pic);
    if (!pic) return;

    // Clip to block clip region (clip path is already in absolute coordinates, no transform needed)
    RdtPath* clip_path = background_image_clip_path(rdcon, view);
    rc_push_clip(rdcon, clip_path, NULL);
    render_painter_draw_picture_rect(rdcon, pic, tile_rect, NULL, 255);
    rc_pop_clip(rdcon);
    rdt_path_free(clip_path);
    // rc_draw_picture transfers ownership to the display list.
}

typedef struct {
    RenderContext* rdcon;
    ViewBlock* view;
    ImageSurface* img;
    bool is_svg;
    ScaleMode tile_scale_mode;
    ClipShape** clip_shape_stack;
    int clip_shape_depth;
} BackgroundImageTileContext;

static void render_background_image_tile_cb(const Rect* tile_rect, void* userdata) {
    BackgroundImageTileContext* ctx = (BackgroundImageTileContext*)userdata;
    Rect tile_copy = *tile_rect;
    if (ctx->is_svg) {
        render_bg_tile_tvg(ctx->rdcon, ctx->view, ctx->img, &tile_copy);
    } else {
        blit_bg_tile(ctx->rdcon, ctx->img, ctx->rdcon->ui_context->surface,
                     &tile_copy, &ctx->rdcon->block.clip,
                     ctx->tile_scale_mode, ctx->clip_shape_stack,
                     ctx->clip_shape_depth);
    }
}

/**
 * Render background image with background-size, background-position, and background-repeat.
 */
static void render_background_image(RenderContext* rdcon, ViewBlock* view, BackgroundProp* bg, Rect rect) {
    const char* image_url = bg->image;
    log_debug("[BG-IMAGE] Rendering background-image '%s' on <%s> (%.0fx%.0f)",
              image_url, view->node_name(), rect.width, rect.height);

    // Load image via the image cache
    ImageSurface* img = load_image(rdcon->ui_context, image_url);
    if (!img) {
        // Background images are optional paint layers; network-managed pages
        // may skip late/blocking fetches and keep rendering the base layer.
        log_warn("[BG-IMAGE] Image unavailable, skipping paint layer '%s'", image_url);
        return;
    }

    float img_w = (float)img->width;
    float img_h = (float)img->height;
    if (img_w <= 0 || img_h <= 0) {
        log_error("[BG-IMAGE] Image has zero dimensions");
        return;
    }

    float s = rdcon->scale;

    BackgroundTilePlan plan = {};
    if (!compute_background_tile_plan(bg, rect, rect, img_w * s, img_h * s, &plan)) {
        return;
    }

    log_debug("[BG-IMAGE] size=%.0fx%.0f pos=(%.0f,%.0f) repeat=(%d,%d)",
              plan.tile_w, plan.tile_h, plan.origin_x - rect.x, plan.origin_y - rect.y,
              plan.repeat_x, plan.repeat_y);

    // Render tiles
    bool is_svg = (img->format == IMAGE_FORMAT_SVG);
    if (!is_svg) {
        // ensure raster image pixels are decoded (lazy loading) at the tile size
        image_surface_ensure_decoded(img,
            (int)plan.tile_w,  // INT_CAST_OK: decode API accepts pixel dimensions.
            (int)plan.tile_h); // INT_CAST_OK: decode API accepts pixel dimensions.
    }

    // Use wrap-around bilinear for repeating raster backgrounds so that
    // tile boundaries blend seamlessly (matching browser behavior).
    bool is_repeating = (plan.repeat_x != CSS_VALUE_NO_REPEAT && plan.repeat_x != CSS_VALUE_SPACE) ||
                        (plan.repeat_y != CSS_VALUE_NO_REPEAT && plan.repeat_y != CSS_VALUE_SPACE);
    ScaleMode tile_scale_mode = (is_repeating && !is_svg) ? SCALE_MODE_LINEAR_WRAP : SCALE_MODE_LINEAR;
    ClipShape rounded_clip_shape = {};
    ClipShape* clip_shape_stack[RDT_MAX_CLIP_SHAPES];
    int clip_shape_depth = background_image_clip_shapes(rdcon, view, &rounded_clip_shape, clip_shape_stack);

    BackgroundImageTileContext tile_ctx = {
        rdcon, view, img, is_svg, tile_scale_mode, clip_shape_stack, clip_shape_depth
    };
    background_for_each_tile(&plan, rect, rect, render_background_image_tile_cb, &tile_ctx);

    log_debug("[BG-IMAGE] Rendered background-image '%s' tiles=(%d-%d)x(%d-%d)",
              image_url, plan.start_col, plan.end_col, plan.start_row, plan.end_row);
}
