#include "render_background.hpp"
#include "render_border.hpp"
#include "../lib/log.h"
#include <math.h>

/**
 * Main background rendering dispatch
 */
void render_background(RenderContext* rdcon, ViewBlock* view, Rect rect) {
    if (!view->bound || !view->bound->background) return;
    
    BackgroundProp* bg = view->bound->background;
    
    // Check if we have gradient
    if (bg->gradient_type != GRADIENT_NONE && bg->linear_gradient) {
        render_background_gradient(rdcon, view, bg, rect);
    } else if (bg->color.a > 0) {
        // Solid color background
        render_background_color(rdcon, view, bg->color, rect);
    }
}

/**
 * Create a rounded clip shape for ThorVG based on the render context's clip region
 */
static Tvg_Paint* create_clip_shape(RenderContext* rdcon) {
    Tvg_Paint* clip_rect = tvg_shape_new();
    
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
        
        tvg_shape_append_rect(clip_rect, clip_x, clip_y, clip_w, clip_h, r, r);
        log_debug("[CLIP] Using rounded clip: (%.0f,%.0f) %.0fx%.0f r=%.0f", 
                  clip_x, clip_y, clip_w, clip_h, r);
    } else {
        // Use rectangular clipping
        tvg_shape_append_rect(clip_rect, 
            rdcon->block.clip.left, rdcon->block.clip.top,
            rdcon->block.clip.right - rdcon->block.clip.left,
            rdcon->block.clip.bottom - rdcon->block.clip.top, 0, 0);
    }
    
    tvg_shape_set_fill_color(clip_rect, 0, 0, 0, 255);
    return clip_rect;
}

/**
 * Bezier control point constant for approximating circular arcs
 */
#define KAPPA 0.5522847498f

/**
 * Build rounded rectangle path with Bezier curves for 4 different corner radii
 */
static void build_rounded_rect_path(Tvg_Paint* shape, float x, float y, float w, float h,
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
        Tvg_Canvas* canvas = rdcon->canvas;
        Tvg_Paint* shape = tvg_shape_new();
        
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
        Tvg_Paint* clip_rect = create_clip_shape(rdcon);
        tvg_paint_set_mask_method(shape, clip_rect, TVG_MASK_METHOD_ALPHA);
        
        tvg_canvas_remove(canvas, NULL);  // clear previous shapes
        tvg_canvas_push(canvas, shape);
        tvg_canvas_draw(canvas, false);
        tvg_canvas_sync(canvas);
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
    
    Tvg_Canvas* canvas = rdcon->canvas;
    Tvg_Paint* shape = tvg_shape_new();
    
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
        tvg_shape_append_rect(shape, rect.x, rect.y, rect.width, rect.height, 0, 0);
    }
    
    // Create linear gradient
    Tvg_Gradient* grad = tvg_linear_gradient_new();
    
    // Calculate gradient line
    float x1, y1, x2, y2;
    calc_linear_gradient_points(gradient->angle, rect, &x1, &y1, &x2, &y2);
    tvg_linear_gradient_set(grad, x1, y1, x2, y2);
    
    // Add color stops
    // ThorVG color stops are in RGBA format with position 0.0-1.0
    Tvg_Color_Stop* stops = (Tvg_Color_Stop*)malloc(sizeof(Tvg_Color_Stop) * gradient->stop_count);
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
    free(stops);
    
    // Apply gradient fill to shape
    tvg_shape_set_gradient(shape, grad);
    
    // Set clipping (may be rounded if parent has border-radius with overflow:hidden)
    Tvg_Paint* clip_rect = create_clip_shape(rdcon);
    tvg_paint_set_mask_method(shape, clip_rect, TVG_MASK_METHOD_ALPHA);
    
    tvg_canvas_remove(canvas, NULL);  // clear previous shapes
    tvg_canvas_push(canvas, shape);
    tvg_canvas_draw(canvas, false);
    tvg_canvas_sync(canvas);
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
            // TODO: Implement radial gradient rendering
            log_debug("[GRADIENT] Radial gradients not yet implemented");
            break;
        case GRADIENT_CONIC:
            // TODO: Implement conic gradient rendering
            log_debug("[GRADIENT] Conic gradients not yet implemented");
            break;
        default:
            log_debug("[GRADIENT] Unknown gradient type");
            break;
    }
}
