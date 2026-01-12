#include "render_background.hpp"
#include "render_border.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include <math.h>

/**
 * Helper function to apply transform and push paint to canvas
 * If a transform is active in rdcon, applies it to the paint before pushing
 */
static void push_with_transform(RenderContext* rdcon, Tvg_Paint paint) {
    if (rdcon->has_transform) {
        tvg_paint_set_transform(paint, &rdcon->transform);
    }
    tvg_canvas_push(rdcon->canvas, paint);
}

/**
 * Main background rendering dispatch
 */
void render_background(RenderContext* rdcon, ViewBlock* view, Rect rect) {
    if (!view->bound || !view->bound->background) return;

    BackgroundProp* bg = view->bound->background;

    log_debug("[RENDER BG] Element <%s>: color=#%08x gradient_type=%d linear=%p radial=%p",
              view->node_name(), bg->color.c, bg->gradient_type,
              (void*)bg->linear_gradient, (void*)bg->radial_gradient);

    // Render base color first (if any)
    if (bg->color.a > 0) {
        render_background_color(rdcon, view, bg->color, rect);
    }

    // Render all radial gradient layers (stacked bottom-to-top)
    if (bg->radial_layers && bg->radial_layer_count > 0) {
        for (int i = 0; i < bg->radial_layer_count; i++) {
            if (bg->radial_layers[i]) {
                log_debug("[GRADIENT] Rendering radial gradient layer %d/%d", i + 1, bg->radial_layer_count);
                render_radial_gradient(rdcon, view, bg->radial_layers[i], rect);
            }
        }
    }

    // Render main gradient (if any)
    if (bg->gradient_type != GRADIENT_NONE &&
        (bg->linear_gradient || bg->radial_gradient || bg->conic_gradient)) {
        log_debug("[GRADIENT] Rendering gradient type=%d", bg->gradient_type);
        render_background_gradient(rdcon, view, bg, rect);
    }
}

/**
 * Create a rounded clip shape for ThorVG based on the render context's clip region
 */
static Tvg_Paint create_clip_shape(RenderContext* rdcon) {
    Tvg_Paint clip_rect = tvg_shape_new();

    if (rdcon->block.has_clip_radius) {
        // Use rounded clipping
        float clip_x = rdcon->block.clip.left;
        float clip_y = rdcon->block.clip.top;
        float clip_w = rdcon->block.clip.right - rdcon->block.clip.left;
        float clip_h = rdcon->block.clip.bottom - rdcon->block.clip.top;

        // Use the first radius value for all corners (simplified approach)
        // ThorVG's append_rect only supports uniform x/y radius
        float r = rdcon->block.clip_radius.top_left;
        if (rdcon->block.clip_radius.top_right > 0) r = max(r, rdcon->block.clip_radius.top_right);
        if (rdcon->block.clip_radius.bottom_left > 0) r = max(r, rdcon->block.clip_radius.bottom_left);
        if (rdcon->block.clip_radius.bottom_right > 0) r = max(r, rdcon->block.clip_radius.bottom_right);

        tvg_shape_append_rect(clip_rect, clip_x, clip_y, clip_w, clip_h, r, r, true);
        log_debug("[CLIP] Using rounded clip: (%.0f,%.0f) %.0fx%.0f r=%.0f",
                  clip_x, clip_y, clip_w, clip_h, r);
    } else {
        // Use rectangular clipping
        tvg_shape_append_rect(clip_rect,
            rdcon->block.clip.left, rdcon->block.clip.top,
            rdcon->block.clip.right - rdcon->block.clip.left,
            rdcon->block.clip.bottom - rdcon->block.clip.top, 0, 0, true);
    }

    tvg_shape_set_fill_color(clip_rect, 0, 0, 0, 255);
    log_debug("[CLIP SHAPE] clip_rect created: clip=%.0f,%.0f to %.0f,%.0f has_radius=%d", 
              rdcon->block.clip.left, rdcon->block.clip.top, 
              rdcon->block.clip.right, rdcon->block.clip.bottom,
              rdcon->block.has_clip_radius);
    return clip_rect;
}

/**
 * Bezier control point constant for approximating circular arcs
 */
#define KAPPA 0.5522847498f

/**
 * Build rounded rectangle path with Bezier curves for 4 different corner radii
 */
static void build_rounded_rect_path(Tvg_Paint shape, float x, float y, float w, float h,
                                     float r_tl, float r_tr, float r_br, float r_bl) {
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
}

/**
 * Render solid color background
 * Handles border-radius by using ThorVG if needed
 */
void render_background_color(RenderContext* rdcon, ViewBlock* view, Color color, Rect rect) {
    // Check if we have border radius on this element OR a parent rounded clip
    bool has_radius = false;
    BorderProp* border = nullptr;
    if (view->bound && view->bound->border) {
        border = view->bound->border;
        has_radius = (border->radius.top_left > 0 || border->radius.top_right > 0 ||
                     border->radius.bottom_right > 0 || border->radius.bottom_left > 0);
    }

    // Also need ThorVG if parent has rounded clip
    bool needs_rounded_clip = rdcon->block.has_clip_radius;

    if (has_radius || needs_rounded_clip) {
        // Use ThorVG for rounded background or rounded clipping
        Tvg_Canvas canvas = rdcon->canvas;
        Tvg_Paint shape = tvg_shape_new();

        float x = rect.x;
        float y = rect.y;
        float w = rect.width;
        float h = rect.height;

        float r_tl = 0, r_tr = 0, r_br = 0, r_bl = 0;
        if (has_radius && border) {
            // Constrain radii FIRST
            constrain_border_radii(border, w, h);
            // Then read the constrained values
            r_tl = border->radius.top_left;
            r_tr = border->radius.top_right;
            r_br = border->radius.bottom_right;
            r_bl = border->radius.bottom_left;
        }

        // Build rounded rect path with all 4 corner radii
        build_rounded_rect_path(shape, x, y, w, h, r_tl, r_tr, r_br, r_bl);
        tvg_shape_set_fill_color(shape, color.r, color.g, color.b, color.a);

        // Set clipping (may be rounded if parent has border-radius with overflow:hidden)
        Tvg_Paint clip_rect = create_clip_shape(rdcon);
        tvg_paint_set_mask_method(shape, clip_rect, TVG_MASK_METHOD_ALPHA);

        tvg_canvas_remove(canvas, NULL);  // clear previous shapes
        push_with_transform(rdcon, shape);
        tvg_canvas_reset_and_draw(rdcon, false);
        tvg_canvas_remove(canvas, NULL);  // clear shapes after rendering
    } else {
        // Simple rectangular fill
        ImageSurface* surface = rdcon->ui_context->surface;
        fill_surface_rect(surface, &rect, color.c, &rdcon->block.clip);
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
 * Render linear gradient using ThorVG
 */
void render_linear_gradient(RenderContext* rdcon, ViewBlock* view, LinearGradient* gradient, Rect rect) {
    if (!gradient || gradient->stop_count < 2) {
        log_debug("[GRADIENT] Invalid gradient (need at least 2 stops)");
        return;
    }

    log_debug("[GRADIENT] render_linear_gradient <%s> rect=(%.0f,%.0f,%.0f,%.0f)",
              view->node_name(), rect.x, rect.y, rect.width, rect.height);

    Tvg_Canvas canvas = rdcon->canvas;
    Tvg_Paint shape = tvg_shape_new();

    // Create rectangle shape for gradient fill
    bool has_radius = false;
    BorderProp* border = nullptr;
    if (view->bound && view->bound->border) {
        border = view->bound->border;
        has_radius = (border->radius.top_left > 0 || border->radius.top_right > 0 ||
                     border->radius.bottom_right > 0 || border->radius.bottom_left > 0);
    }

    if (has_radius) {
        // Constrain radii FIRST
        constrain_border_radii(border, rect.width, rect.height);
        // Then read the constrained values
        float r_tl = border->radius.top_left;
        float r_tr = border->radius.top_right;
        float r_br = border->radius.bottom_right;
        float r_bl = border->radius.bottom_left;
        // Rounded rectangle with all 4 corner radii
        build_rounded_rect_path(shape, rect.x, rect.y, rect.width, rect.height, r_tl, r_tr, r_br, r_bl);
    } else {
        // Simple rectangle
        tvg_shape_append_rect(shape, rect.x, rect.y, rect.width, rect.height, 0, 0, true);
    }

    // Create linear gradient
    Tvg_Gradient grad = tvg_linear_gradient_new();

    // Calculate gradient line
    float x1, y1, x2, y2;
    calc_linear_gradient_points(gradient->angle, rect, &x1, &y1, &x2, &y2);
    tvg_linear_gradient_set(grad, x1, y1, x2, y2);

    // Add color stops
    // ThorVG color stops are in RGBA format with position 0.0-1.0
    Tvg_Color_Stop* stops = (Tvg_Color_Stop*)mem_alloc(sizeof(Tvg_Color_Stop) * gradient->stop_count, MEM_CAT_RENDER);
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

    tvg_gradient_set_color_stops(grad, stops, gradient->stop_count);
    mem_free(stops);

    // Apply gradient fill to shape
    tvg_shape_set_gradient(shape, grad);

    // Set clipping (may be rounded if parent has border-radius with overflow:hidden)
    Tvg_Paint clip_rect = create_clip_shape(rdcon);
    tvg_paint_set_mask_method(shape, clip_rect, TVG_MASK_METHOD_ALPHA);

    tvg_canvas_remove(canvas, NULL);  // clear previous shapes
    push_with_transform(rdcon, shape);
    tvg_canvas_reset_and_draw(rdcon, false);
    tvg_canvas_remove(canvas, NULL);  // clear shapes after rendering
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
 * Render radial gradient using ThorVG
 */
void render_radial_gradient(RenderContext* rdcon, ViewBlock* view, RadialGradient* gradient, Rect rect) {
    if (!gradient || gradient->stop_count < 2) {
        log_debug("[GRADIENT] Invalid radial gradient (need at least 2 stops)");
        return;
    }

    Tvg_Canvas canvas = rdcon->canvas;
    Tvg_Paint shape = tvg_shape_new();

    // Create rectangle shape for gradient fill
    bool has_radius = false;
    BorderProp* border = nullptr;
    if (view->bound && view->bound->border) {
        border = view->bound->border;
        has_radius = (border->radius.top_left > 0 || border->radius.top_right > 0 ||
                     border->radius.bottom_right > 0 || border->radius.bottom_left > 0);
    }

    if (has_radius) {
        constrain_border_radii(border, rect.width, rect.height);
        float r_tl = border->radius.top_left;
        float r_tr = border->radius.top_right;
        float r_br = border->radius.bottom_right;
        float r_bl = border->radius.bottom_left;
        build_rounded_rect_path(shape, rect.x, rect.y, rect.width, rect.height, r_tl, r_tr, r_br, r_bl);
    } else {
        tvg_shape_append_rect(shape, rect.x, rect.y, rect.width, rect.height, 0, 0, true);
    }

    // Calculate center position in absolute coordinates
    float cx = rect.x + rect.width * gradient->cx;
    float cy = rect.y + rect.height * gradient->cy;

    // Calculate radius based on size keyword
    float radius = calc_radial_radius(gradient, rect, rect.width * gradient->cx, rect.height * gradient->cy);

    // For circle shape, use uniform radius
    // For ellipse, we'd need to scale, but ThorVG only supports circular radial gradients
    // So we approximate ellipse as circle using the larger dimension
    if (gradient->shape == RADIAL_SHAPE_ELLIPSE) {
        // Use average of width/height ratio
        radius = fmaxf(rect.width, rect.height) * 0.5f;
    }

    log_debug("[GRADIENT] Radial gradient center=(%.1f,%.1f) radius=%.1f shape=%d",
              cx, cy, radius, gradient->shape);

    // Create radial gradient
    Tvg_Gradient grad = tvg_radial_gradient_new();

    // ThorVG radial gradient: (cx, cy, r, fx, fy, fr)
    // cx, cy, r = end circle (outer), fx, fy, fr = start circle (inner focal point)
    // For CSS radial gradients, focal point is at center with zero radius
    tvg_radial_gradient_set(grad, cx, cy, radius, cx, cy, 0);

    // Add color stops
    Tvg_Color_Stop* stops = (Tvg_Color_Stop*)mem_alloc(sizeof(Tvg_Color_Stop) * gradient->stop_count, MEM_CAT_RENDER);
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

    tvg_gradient_set_color_stops(grad, stops, gradient->stop_count);
    mem_free(stops);

    // Apply gradient fill to shape
    tvg_shape_set_gradient(shape, grad);

    // Set clipping
    Tvg_Paint clip_rect = create_clip_shape(rdcon);
    tvg_paint_set_mask_method(shape, clip_rect, TVG_MASK_METHOD_ALPHA);

    tvg_canvas_remove(canvas, NULL);
    push_with_transform(rdcon, shape);
    tvg_canvas_reset_and_draw(rdcon, false);
    tvg_canvas_remove(canvas, NULL);  // clear shapes after rendering
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
void render_conic_gradient(RenderContext* rdcon, ViewBlock* view, ConicGradient* gradient, Rect rect) {
    if (!gradient || gradient->stop_count < 2) {
        log_debug("[GRADIENT] Invalid conic gradient (need at least 2 stops)");
        return;
    }

    log_debug("[GRADIENT] Rendering conic gradient: from=%.1fdeg center=(%.2f,%.2f) stops=%d",
              gradient->from_angle, gradient->cx, gradient->cy, gradient->stop_count);

    // Debug: log all color stops
    for (int i = 0; i < gradient->stop_count; i++) {
        log_debug("[GRADIENT] Conic stop %d: pos=%.2f color=#%02x%02x%02x",
                  i, gradient->stops[i].position,
                  gradient->stops[i].color.r, gradient->stops[i].color.g, gradient->stops[i].color.b);
    }

    ImageSurface* surface = rdcon->ui_context->surface;

    // Calculate center position in absolute coordinates
    float cx = rect.x + rect.width * gradient->cx;
    float cy = rect.y + rect.height * gradient->cy;

    // From angle in radians (CSS: 0deg = up, clockwise)
    float from_rad = (gradient->from_angle - 90.0f) * M_PI / 180.0f;

    // Get border radius for rounded corners
    float r_tl = 0, r_tr = 0, r_br = 0, r_bl = 0;
    if (view->bound && view->bound->border) {
        BorderProp* border = view->bound->border;
        constrain_border_radii(border, rect.width, rect.height);
        r_tl = border->radius.top_left;
        r_tr = border->radius.top_right;
        r_br = border->radius.bottom_right;
        r_bl = border->radius.bottom_left;
    }
    bool has_radius = (r_tl > 0 || r_tr > 0 || r_br > 0 || r_bl > 0);

    // Get clip bounds
    Bound* clip = &rdcon->block.clip;
    int clip_left = (int)clip->left;
    int clip_top = (int)clip->top;
    int clip_right = (int)clip->right;
    int clip_bottom = (int)clip->bottom;

    // Iterate over each pixel in the rect
    int start_x = (int)fmaxf(rect.x, (float)clip_left);
    int end_x = (int)fminf(rect.x + rect.width, (float)clip_right);
    int start_y = (int)fmaxf(rect.y, (float)clip_top);
    int end_y = (int)fminf(rect.y + rect.height, (float)clip_bottom);

    for (int py = start_y; py < end_y; py++) {
        for (int px = start_x; px < end_x; px++) {
            // Check if pixel is inside rounded corners
            if (has_radius) {
                float lx = px - rect.x;  // local x relative to rect
                float ly = py - rect.y;  // local y relative to rect
                float w = rect.width;
                float h = rect.height;

                // Check each corner
                // Top-left corner
                if (lx < r_tl && ly < r_tl) {
                    float dx = lx - r_tl;
                    float dy = ly - r_tl;
                    if (dx * dx + dy * dy > r_tl * r_tl) continue;
                }
                // Top-right corner
                else if (lx > w - r_tr && ly < r_tr) {
                    float dx = lx - (w - r_tr);
                    float dy = ly - r_tr;
                    if (dx * dx + dy * dy > r_tr * r_tr) continue;
                }
                // Bottom-right corner
                else if (lx > w - r_br && ly > h - r_br) {
                    float dx = lx - (w - r_br);
                    float dy = ly - (h - r_br);
                    if (dx * dx + dy * dy > r_br * r_br) continue;
                }
                // Bottom-left corner
                else if (lx < r_bl && ly > h - r_bl) {
                    float dx = lx - r_bl;
                    float dy = ly - (h - r_bl);
                    if (dx * dx + dy * dy > r_bl * r_bl) continue;
                }
            }

            // Calculate angle from center to this pixel
            float dx = px - cx;
            float dy = py - cy;
            float angle = atan2f(dy, dx) - from_rad;

            // Normalize to 0-1 range (one full rotation)
            float position = fmodf((angle / (2.0f * M_PI)) + 1.0f, 1.0f);

            // Get color at this angle position
            Color color = get_gradient_color_at(gradient->stops, gradient->stop_count, position);

            // Set pixel (surface uses ABGR8888 format - same as ThorVG)
            if (px >= 0 && px < surface->width && py >= 0 && py < surface->height) {
                uint32_t* pixel = (uint32_t*)((uint8_t*)surface->pixels + py * surface->pitch) + px;

                // Alpha blend if not fully opaque
                if (color.a == 255) {
                    // ABGR format: A in high bits, then B, G, R in low bits
                    *pixel = (color.a << 24) | (color.b << 16) | (color.g << 8) | color.r;
                } else if (color.a > 0) {
                    // Simple alpha blend with existing pixel
                    uint32_t existing = *pixel;
                    uint8_t er = existing & 0xFF;           // R is in lowest byte
                    uint8_t eg = (existing >> 8) & 0xFF;    // G
                    uint8_t eb = (existing >> 16) & 0xFF;   // B
                    float alpha = color.a / 255.0f;
                    uint8_t nr = (uint8_t)(color.r * alpha + er * (1 - alpha));
                    uint8_t ng = (uint8_t)(color.g * alpha + eg * (1 - alpha));
                    uint8_t nb = (uint8_t)(color.b * alpha + eb * (1 - alpha));
                    *pixel = (255 << 24) | (nb << 16) | (ng << 8) | nr;
                }
            }
        }
    }
}

/**
 * Render background gradient (dispatch to type-specific function)
 */
void render_background_gradient(RenderContext* rdcon, ViewBlock* view, BackgroundProp* bg, Rect rect) {
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
 * Render CSS box-shadow for an element
 *
 * Box shadows are rendered BEFORE the background (underneath the element).
 * Multiple shadows are rendered in reverse order (last specified = bottommost).
 *
 * Algorithm:
 * 1. For each shadow (in reverse order for proper stacking):
 *    - Calculate shadow rectangle (element rect + offsets + spread)
 *    - If blur > 0: render blurred shadow using ThorVG gaussian blur
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

    // Collect shadows into array
    BoxShadow** shadows = (BoxShadow**)alloca(shadow_count * sizeof(BoxShadow*));
    shadow = view->bound->box_shadow;
    for (int i = 0; i < shadow_count; i++) {
        shadows[i] = shadow;
        shadow = shadow->next;
    }

    // Get border radius if present
    float r_tl = 0, r_tr = 0, r_br = 0, r_bl = 0;
    if (view->bound && view->bound->border) {
        BorderProp* border = view->bound->border;
        r_tl = border->radius.top_left;
        r_tr = border->radius.top_right;
        r_br = border->radius.bottom_right;
        r_bl = border->radius.bottom_left;
    }

    Tvg_Canvas canvas = rdcon->canvas;

    // Render outer shadows first (in reverse order - last shadow is bottommost)
    for (int i = shadow_count - 1; i >= 0; i--) {
        BoxShadow* s = shadows[i];
        if (s->inset) continue;  // Skip inset shadows for now

        // Calculate shadow rectangle
        float shadow_x = rect.x + s->offset_x - s->spread_radius;
        float shadow_y = rect.y + s->offset_y - s->spread_radius;
        float shadow_w = rect.width + 2 * s->spread_radius;
        float shadow_h = rect.height + 2 * s->spread_radius;

        // Adjust border radius for spread
        float spread_factor = (s->spread_radius >= 0) ? 1.0f : 1.0f;
        float sr_tl = max(0.0f, r_tl + s->spread_radius * spread_factor);
        float sr_tr = max(0.0f, r_tr + s->spread_radius * spread_factor);
        float sr_br = max(0.0f, r_br + s->spread_radius * spread_factor);
        float sr_bl = max(0.0f, r_bl + s->spread_radius * spread_factor);

        log_debug("[BOX-SHADOW] Rendering shadow: offset=(%.1f,%.1f) blur=%.1f spread=%.1f color=#%02x%02x%02x%02x",
                  s->offset_x, s->offset_y, s->blur_radius, s->spread_radius,
                  s->color.r, s->color.g, s->color.b, s->color.a);

        // Create shadow shape
        Tvg_Paint shadow_shape = tvg_shape_new();

        if (sr_tl > 0 || sr_tr > 0 || sr_br > 0 || sr_bl > 0) {
            // Rounded shadow - build path with bezier corners
            // Use the same build_rounded_rect_path logic
            float x = shadow_x, y = shadow_y, w = shadow_w, h = shadow_h;

            #define KAPPA_SHADOW 0.5522847498f
            tvg_shape_move_to(shadow_shape, x + sr_tl, y);
            tvg_shape_line_to(shadow_shape, x + w - sr_tr, y);
            if (sr_tr > 0) {
                tvg_shape_cubic_to(shadow_shape,
                    x + w - sr_tr + sr_tr * KAPPA_SHADOW, y,
                    x + w, y + sr_tr - sr_tr * KAPPA_SHADOW,
                    x + w, y + sr_tr);
            }
            tvg_shape_line_to(shadow_shape, x + w, y + h - sr_br);
            if (sr_br > 0) {
                tvg_shape_cubic_to(shadow_shape,
                    x + w, y + h - sr_br + sr_br * KAPPA_SHADOW,
                    x + w - sr_br + sr_br * KAPPA_SHADOW, y + h,
                    x + w - sr_br, y + h);
            }
            tvg_shape_line_to(shadow_shape, x + sr_bl, y + h);
            if (sr_bl > 0) {
                tvg_shape_cubic_to(shadow_shape,
                    x + sr_bl - sr_bl * KAPPA_SHADOW, y + h,
                    x, y + h - sr_bl + sr_bl * KAPPA_SHADOW,
                    x, y + h - sr_bl);
            }
            tvg_shape_line_to(shadow_shape, x, y + sr_tl);
            if (sr_tl > 0) {
                tvg_shape_cubic_to(shadow_shape,
                    x, y + sr_tl - sr_tl * KAPPA_SHADOW,
                    x + sr_tl - sr_tl * KAPPA_SHADOW, y,
                    x + sr_tl, y);
            }
            tvg_shape_close(shadow_shape);
            #undef KAPPA_SHADOW
        } else {
            // Simple rectangle shadow
            tvg_shape_append_rect(shadow_shape, shadow_x, shadow_y, shadow_w, shadow_h, 0, 0, true);
        }

        tvg_shape_set_fill_color(shadow_shape, s->color.r, s->color.g, s->color.b, s->color.a);

        // Apply blur if specified
        if (s->blur_radius > 0) {
            // ThorVG supports gaussian blur via scene effects
            // For now, we approximate blur by rendering multiple layers with decreasing opacity
            // A proper implementation would use tvg_paint_set_composite with blur

            // Simple approximation: render shadow with reduced opacity
            // The blur effect is approximated by expanding the shadow slightly
            float alpha_factor = 0.7f;  // Reduce opacity for blur effect
            uint8_t blurred_alpha = (uint8_t)(s->color.a * alpha_factor);
            tvg_shape_set_fill_color(shadow_shape, s->color.r, s->color.g, s->color.b, blurred_alpha);

            log_debug("[BOX-SHADOW] Applied blur approximation (factor=%.2f)", alpha_factor);
        }

        // Apply clipping
        Tvg_Paint clip_rect = tvg_shape_new();
        Bound* clip = &rdcon->block.clip;
        tvg_shape_append_rect(clip_rect, clip->left, clip->top,
                              clip->right - clip->left, clip->bottom - clip->top, 0, 0, true);
        tvg_shape_set_fill_color(clip_rect, 0, 0, 0, 255);
        tvg_paint_set_mask_method(shadow_shape, clip_rect, TVG_MASK_METHOD_ALPHA);

        push_with_transform(rdcon, shadow_shape);
    }

    // Draw all shadows
    tvg_canvas_reset_and_draw(rdcon, false);
    tvg_canvas_remove(canvas, NULL);  // clear shapes after rendering

    log_debug("[BOX-SHADOW] Rendered %d outer shadow(s)", shadow_count);
}
