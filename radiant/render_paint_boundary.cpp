#include "render.hpp"
#include "../lambda/input/css/css_value.hpp"
#include <math.h>

static bool boundary_has_radius(const BorderProp* border) {
    if (!border) return false;
    const Corner* radius = &border->radius;
    return radius->top_left > 0.0f || radius->top_right > 0.0f ||
           radius->bottom_right > 0.0f || radius->bottom_left > 0.0f ||
           radius->top_left_y > 0.0f || radius->top_right_y > 0.0f ||
           radius->bottom_right_y > 0.0f || radius->bottom_left_y > 0.0f;
}

static bool boundary_uniform_circular_radius(const BorderProp* border,
                                             float width, float height,
                                             float* out_radius) {
    if (!boundary_has_radius(border)) {
        if (out_radius) *out_radius = 0.0f;
        return true;
    }

    const Corner* radius = &border->radius;
    float value = radius->top_left;
    if (value <= 0.0f ||
        radius->top_right != value ||
        radius->bottom_right != value ||
        radius->bottom_left != value ||
        radius->top_left_y != value ||
        radius->top_right_y != value ||
        radius->bottom_right_y != value ||
        radius->bottom_left_y != value) {
        return false;
    }
    if (value * 2.0f > width || value * 2.0f > height) {
        return false;
    }
    if (out_radius) *out_radius = value;
    return true;
}

static bool boundary_background_simple(const BackgroundProp* bg) {
    if (!bg) return true;
    if (bg->image) return false;
    if (bg->gradient_type != GRADIENT_NONE) return false;
    if (bg->linear_gradient || bg->radial_gradient || bg->conic_gradient) return false;
    if (bg->linear_layer_count > 0 || bg->radial_layer_count > 0) return false;
    if (bg->blend_mode && bg->blend_mode != CSS_VALUE_NORMAL) return false;
    return true;
}

static bool boundary_border_side_visible(float width, CssEnum style, Color color) {
    return width > 0.0f && color.a > 0 &&
           style != CSS_VALUE_NONE && style != CSS_VALUE_HIDDEN;
}

static bool boundary_border_side_simple(float width, CssEnum style, Color color) {
    if (!boundary_border_side_visible(width, style, color)) return true;
    return style == CSS_VALUE_SOLID;
}

static bool boundary_border_has_visible_side(const BorderProp* border) {
    if (!border) return false;
    return boundary_border_side_visible(border->width.top, border->top_style, border->top_color) ||
           boundary_border_side_visible(border->width.right, border->right_style, border->right_color) ||
           boundary_border_side_visible(border->width.bottom, border->bottom_style, border->bottom_color) ||
           boundary_border_side_visible(border->width.left, border->left_style, border->left_color);
}

static bool boundary_border_all_sides_visible(const BorderProp* border) {
    if (!border) return false;
    return boundary_border_side_visible(border->width.top, border->top_style, border->top_color) &&
           boundary_border_side_visible(border->width.right, border->right_style, border->right_color) &&
           boundary_border_side_visible(border->width.bottom, border->bottom_style, border->bottom_color) &&
           boundary_border_side_visible(border->width.left, border->left_style, border->left_color);
}

static bool boundary_border_simple(const BorderProp* border) {
    if (!border) return true;
    if (boundary_has_radius(border) && boundary_border_has_visible_side(border)) return false;
    if (!boundary_border_side_simple(border->width.top, border->top_style, border->top_color) ||
        !boundary_border_side_simple(border->width.right, border->right_style, border->right_color) ||
        !boundary_border_side_simple(border->width.bottom, border->bottom_style, border->bottom_color) ||
        !boundary_border_side_simple(border->width.left, border->left_style, border->left_color)) {
        return false;
    }

    bool have_color = false;
    Color color = {};
    if (boundary_border_side_visible(border->width.top, border->top_style, border->top_color)) {
        color = border->top_color;
        have_color = true;
    }
    if (boundary_border_side_visible(border->width.right, border->right_style, border->right_color)) {
        if (have_color && border->right_color.c != color.c) return false;
        color = border->right_color;
        have_color = true;
    }
    if (boundary_border_side_visible(border->width.bottom, border->bottom_style, border->bottom_color)) {
        if (have_color && border->bottom_color.c != color.c) return false;
        color = border->bottom_color;
        have_color = true;
    }
    if (boundary_border_side_visible(border->width.left, border->left_style, border->left_color)) {
        if (have_color && border->left_color.c != color.c) return false;
    }
    return true;
}

static bool boundary_rounded_border_fill_supported(const BoundaryProp* bound) {
    if (!bound || !bound->border || !bound->background) return false;
    const BorderProp* border = bound->border;
    const BackgroundProp* bg = bound->background;
    if (bg->color.a != 255) return false;
    if (!boundary_border_all_sides_visible(border)) return false;
    if (border->top_style != CSS_VALUE_SOLID ||
        border->right_style != CSS_VALUE_SOLID ||
        border->bottom_style != CSS_VALUE_SOLID ||
        border->left_style != CSS_VALUE_SOLID) {
        return false;
    }
    if (border->top_color.a != 255 ||
        border->right_color.a != 255 ||
        border->bottom_color.a != 255 ||
        border->left_color.a != 255) {
        return false;
    }
    return border->width.top == border->width.right &&
           border->width.right == border->width.bottom &&
           border->width.bottom == border->width.left &&
           border->top_color.c == border->right_color.c &&
           border->right_color.c == border->bottom_color.c &&
           border->bottom_color.c == border->left_color.c;
}

static void boundary_emit_border_side(PaintList* paint_list, float x, float y,
                                      float w, float h, Color color) {
    if (w <= 0.0f || h <= 0.0f || color.a == 0) return;
    paint_fill_rect(paint_list, x, y, w, h, color);
}

bool render_paint_boundary_emit_simple(PaintList* paint_list, ViewBlock* view,
                                       float x, float y) {
    if (!paint_list || !view || !view->bound) return false;

    BoundaryProp* bound = view->bound;
    if (bound->box_shadow || bound->outline) return false;
    if (!boundary_background_simple(bound->background)) return false;

    float width = view->width;
    float height = view->height;
    if (width < 0.0f || height < 0.0f) return false;

    float radius = 0.0f;
    if (!boundary_uniform_circular_radius(bound->border, width, height, &radius)) {
        return false;
    }
    bool rounded_visible_border =
        radius > 0.0f && boundary_border_has_visible_side(bound->border);
    if (rounded_visible_border) {
        if (!boundary_rounded_border_fill_supported(bound)) return false;
    } else if (!boundary_border_simple(bound->border)) {
        return false;
    }

    BorderProp* border = bound->border;
    if (rounded_visible_border) {
        float bw = border->width.top;
        paint_fill_rounded_rect(paint_list, x, y, width, height,
                                radius, radius, border->top_color);
        float inner_w = width - bw * 2.0f;
        float inner_h = height - bw * 2.0f;
        if (inner_w > 0.0f && inner_h > 0.0f) {
            float inner_radius = radius - bw;
            if (inner_radius < 0.0f) inner_radius = 0.0f;
            if (inner_radius > 0.0f) {
                paint_fill_rounded_rect(paint_list, x + bw, y + bw,
                                        inner_w, inner_h,
                                        inner_radius, inner_radius,
                                        bound->background->color);
            } else {
                paint_fill_rect(paint_list, x + bw, y + bw,
                                inner_w, inner_h, bound->background->color);
            }
        }
        return true;
    }

    BackgroundProp* bg = bound->background;
    if (bg && bg->color.a > 0) {
        if (radius > 0.0f) {
            paint_fill_rounded_rect(paint_list, x, y, width, height,
                                    radius, radius, bg->color);
        } else {
            paint_fill_rect(paint_list, x, y, width, height, bg->color);
        }
    }

    if (!border) return true;

    if (boundary_border_side_visible(border->width.top, border->top_style, border->top_color)) {
        boundary_emit_border_side(paint_list, x, y, width,
                                  border->width.top, border->top_color);
    }
    if (boundary_border_side_visible(border->width.right, border->right_style,
                                     border->right_color)) {
        boundary_emit_border_side(paint_list, x + width - border->width.right, y,
                                  border->width.right, height, border->right_color);
    }
    if (boundary_border_side_visible(border->width.bottom, border->bottom_style,
                                     border->bottom_color)) {
        boundary_emit_border_side(paint_list, x, y + height - border->width.bottom,
                                  width, border->width.bottom, border->bottom_color);
    }
    if (boundary_border_side_visible(border->width.left, border->left_style,
                                     border->left_color)) {
        boundary_emit_border_side(paint_list, x, y,
                                  border->width.left, height, border->left_color);
    }
    return true;
}

bool render_paint_boundary_emit_outer_shadows(PaintList* paint_list, ViewBlock* view,
                                              float x, float y) {
    if (!paint_list || !view || !view->bound || !view->bound->box_shadow) {
        return false;
    }

    float width = view->width;
    float height = view->height;
    if (width <= 0.0f || height <= 0.0f) return false;

    float r_tl = 0.0f;
    float r_tr = 0.0f;
    float r_br = 0.0f;
    float r_bl = 0.0f;
    if (view->bound->border) {
        BorderProp* border = view->bound->border;
        r_tl = border->radius.top_left;
        r_tr = border->radius.top_right;
        r_br = border->radius.bottom_right;
        r_bl = border->radius.bottom_left;
    }

    int shadow_count = 0;
    for (BoxShadow* shadow = view->bound->box_shadow; shadow; shadow = shadow->next) {
        shadow_count++;
    }

    bool emitted = false;
    for (int shadow_index = shadow_count - 1; shadow_index >= 0; shadow_index--) {
        BoxShadow* shadow = view->bound->box_shadow;
        for (int i = 0; i < shadow_index && shadow; i++) {
            shadow = shadow->next;
        }
        if (!shadow || shadow->inset || shadow->color.a == 0) {
            continue;
        }

        float spread = shadow->spread_radius;
        float shadow_x = x + shadow->offset_x - spread;
        float shadow_y = y + shadow->offset_y - spread;
        float shadow_w = width + 2.0f * spread;
        float shadow_h = height + 2.0f * spread;
        if (shadow_w <= 0.0f || shadow_h <= 0.0f) {
            continue;
        }

        float sr_tl = fmaxf(0.0f, r_tl + spread);
        float sr_tr = fmaxf(0.0f, r_tr + spread);
        float sr_br = fmaxf(0.0f, r_br + spread);
        float sr_bl = fmaxf(0.0f, r_bl + spread);

        float exclude_params[8] = {
            x, y, width, height,
            r_tl, r_tr, r_br, r_bl
        };

        paint_outer_shadow(paint_list,
                           shadow_x, shadow_y, shadow_w, shadow_h,
                           sr_tl, sr_tr, sr_br, sr_bl,
                           shadow->color,
                           shadow->blur_radius,
                           CLIP_SHAPE_ROUNDED_RECT, exclude_params,
                           0, nullptr);
        emitted = true;
    }
    return emitted;
}

static bool boundary_copy_gradient_stops(GradientStop* src, int src_count,
                                         RdtGradientStop* stops, int stop_capacity,
                                         int* out_count) {
    if (!src || src_count < 2 || !stops || stop_capacity < src_count) return false;
    for (int i = 0; i < src_count; i++) {
        GradientStop* stop = &src[i];
        float pos = stop->position >= 0.0f
            ? stop->position
            : (src_count > 1 ? (float)i / (float)(src_count - 1) : 0.0f);
        stops[i] = {pos, stop->color.r, stop->color.g, stop->color.b, stop->color.a};
    }
    if (out_count) *out_count = src_count;
    return true;
}

static bool boundary_prepare_gradient(ViewBlock* view, float x, float y,
                                      GradientStop* source_stops, int source_count,
                                      RdtGradientStop* stops, int stop_capacity,
                                      RdtPath** path, int* stop_count) {
    if (view->width <= 0.0f || view->height <= 0.0f ||
        !boundary_copy_gradient_stops(source_stops, source_count,
                                      stops, stop_capacity, stop_count)) {
        return false;
    }
    *path = rdt_path_new();
    if (!*path) return false;
    rdt_path_add_rect(*path, x, y, view->width, view->height, 0.0f, 0.0f);
    return true;
}

bool render_paint_boundary_build_linear_gradient(ViewBlock* view, float x, float y,
                                                 RdtGradientStop* stops,
                                                 int stop_capacity,
                                                 BoundaryLinearGradientPaint* out) {
    if (!view || !view->bound || !view->bound->background || !out) return false;
    BackgroundProp* bg = view->bound->background;
    LinearGradient* gradient = bg->linear_gradient;
    if (bg->gradient_type != GRADIENT_LINEAR || !gradient) return false;

    int stop_count = 0;
    RdtPath* path;
    if (!boundary_prepare_gradient(view, x, y, gradient->stops, gradient->stop_count,
                                   stops, stop_capacity, &path, &stop_count)) return false;

    float angle_rad = gradient->angle * (float)M_PI / 180.0f;
    float dx = sinf(angle_rad);
    float dy = -cosf(angle_rad);
    float half_w = view->width * 0.5f;
    float half_h = view->height * 0.5f;
    float center_x = x + half_w;
    float center_y = y + half_h;
    float abs_dx = fabsf(dx);
    float abs_dy = fabsf(dy);
    float dist = (abs_dx * view->height < abs_dy * view->width)
        ? (abs_dy > 1e-7f ? half_h / abs_dy : half_w)
        : (abs_dx > 1e-7f ? half_w / abs_dx : half_h);

    out->path = path;
    out->x1 = center_x - dx * dist;
    out->y1 = center_y - dy * dist;
    out->x2 = center_x + dx * dist;
    out->y2 = center_y + dy * dist;
    out->stops = stops;
    out->stop_count = stop_count;
    return true;
}

bool render_paint_boundary_build_radial_gradient(ViewBlock* view, float x, float y,
                                                 RdtGradientStop* stops,
                                                 int stop_capacity,
                                                 BoundaryRadialGradientPaint* out) {
    if (!view || !view->bound || !view->bound->background || !out) return false;
    BackgroundProp* bg = view->bound->background;
    RadialGradient* gradient = bg->radial_gradient;
    if (bg->gradient_type != GRADIENT_RADIAL || !gradient) return false;

    int stop_count = 0;
    RdtPath* path;
    if (!boundary_prepare_gradient(view, x, y, gradient->stops, gradient->stop_count,
                                   stops, stop_capacity, &path, &stop_count)) return false;

    out->path = path;
    out->cx = x + (gradient->cx_set ? gradient->cx * view->width : view->width * 0.5f);
    out->cy = y + (gradient->cy_set ? gradient->cy * view->height : view->height * 0.5f);
    out->r = (view->width < view->height ? view->width : view->height) * 0.5f;
    out->stops = stops;
    out->stop_count = stop_count;
    return true;
}
