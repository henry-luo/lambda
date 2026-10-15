#include "render.hpp"
#include "../lambda/input/css/css_value.hpp"
#include "../lib/math_utils.h"
#include <math.h>
#include <string.h>

static bool boundary_has_radius(const BorderProp* border) {
    return border && radiant_corner_has_radius(&border->radius);
}

static bool boundary_uniform_circular_radius(const BorderProp* border,
                                             float width, float height,
                                             float* out_radius) {
    if (!boundary_has_radius(border)) {
        if (out_radius) *out_radius = 0.0f;
        return true;
    }

    const Corner* radius = &border->radius;
    float value = radius->horizontal[0];
    if (value <= 0.0f) return false;
    for (int i = 0; i < 4; i++) {
        if (radius->horizontal[i] != value || radius->vertical[i] != value) return false;
    }
    if (value * 2.0f > width || value * 2.0f > height) {
        return false;
    }
    if (out_radius) *out_radius = value;
    return true;
}

bool render_paint_background_is_simple(const BackgroundProp* bg) {
    if (!bg) return true;
    // other clip boxes need their shared background destination instead of a full border-box fill.
    if (bg->bg_clip && bg->bg_clip != CSS_VALUE_BORDER_BOX) return false;
    if (bg->image) return false;
    if (bg->gradient_type != GRADIENT_NONE) return false;
    if (bg->linear_gradient || bg->radial_gradient || bg->conic_gradient) return false;
    if (bg->layer_count > 0) {
        if (!bg->layers) return false;
        const BackgroundLayer& bottom = bg->layers[bg->layer_count - 1];
        if (bottom.bg_clip && bottom.bg_clip != CSS_VALUE_BORDER_BOX) return false;
        for (int index = 0; index < bg->layer_count; index++) {
            const BackgroundLayer& layer = bg->layers[index];
            if (layer.image || layer.gradient_type != GRADIENT_NONE ||
                layer.linear_gradient || layer.radial_gradient || layer.conic_gradient) return false;
        }
    }
    if (bg->blend_mode && bg->blend_mode != CSS_VALUE_NORMAL) return false;
    return true;
}

static bool boundary_border_side_visible(float width, CssEnum style, Color color) {
    return width > 0.0f && color.a > 0 &&
           style != CSS_VALUE_NONE && style != CSS_VALUE_HIDDEN;
}

static bool boundary_border_side_simple(float width, CssEnum style, Color color) {
    if (!boundary_border_side_visible(width, style, color)) return true;
    return render_border_style_supported(style);
}

static bool boundary_border_has_visible_side(const BorderProp* border) {
    if (!border) return false;
    for (int i = 0; i < 4; i++) {
        if (boundary_border_side_visible(border->width.values[i], border->styles[i],
                                         border->colors[i])) return true;
    }
    return false;
}

static bool boundary_border_simple(const BorderProp* border) {
    if (!border) return true;
    if (boundary_has_radius(border) && boundary_border_has_visible_side(border)) return false;
    for (int i = 0; i < 4; i++) {
        if (!boundary_border_side_simple(border->width.values[i], border->styles[i],
                                         border->colors[i])) return false;
    }

    return true;
}

static bool boundary_border_uniform_color(const BorderProp* border) {
    bool have_color = false;
    Color color = {};
    for (int i = 0; i < 4; i++) {
        if (!boundary_border_side_visible(border->width.values[i], border->styles[i],
                                          border->colors[i])) continue;
        if (have_color && border->colors[i].c != color.c) return false;
        color = border->colors[i];
        have_color = true;
    }
    return true;
}

bool render_border_is_uniform_solid(const BorderProp* border) {
    if (!border || border->width.values[0] <= 0.0f || border->colors[0].a == 0) return false;
    for (int i = 0; i < 4; i++) {
        if (border->width.values[i] != border->width.values[0] ||
            border->styles[i] != CSS_VALUE_SOLID ||
            border->colors[i].c != border->colors[0].c) return false;
    }
    return true;
}

bool render_paint_boundary_emit_solid_border(PaintList* paint, BorderProp* border, Rect rect) {
    if (!paint || !render_border_is_uniform_solid(border) ||
        rect.width <= border->width.top || rect.height <= border->width.top) return false;
    resolve_border_radius_percentages(&border->radius, rect.width, rect.height);
    constrain_corner_radii(&border->radius, rect.width, rect.height);
    // a centered rounded stroke fills the curved border even with a transparent background.
    RdtPath* path = render_border_create_centered_stroke_path(border, rect, border->width.top);
    if (!path) return false;
    int index = paint_list_count(paint);
    paint_stroke_path(paint, path, border->top_color, border->width.top,
        RDT_CAP_BUTT, RDT_JOIN_MITER, nullptr, 0, 0.0f, nullptr);
    if (paint_list_take_path_payload(paint, index, PAINT_STROKE_PATH)) return true;
    rdt_path_free(path);
    return false;
}

static bool boundary_rounded_border_fill_supported(const BoundaryProp* bound) {
    if (!bound || !bound->border || !bound->background) return false;
    const BorderProp* border = bound->border;
    const BackgroundProp* bg = bound->background;
    if (bg->color.a != 255) return false;
    return render_border_is_uniform_solid(border) && border->colors[0].a == 255;
}

static void boundary_emit_border_side(PaintList* paint_list, float x, float y,
                                      float w, float h, Color color) {
    if (w <= 0.0f || h <= 0.0f || color.a == 0) return;
    paint_fill_rect(paint_list, x, y, w, h, color);
}

static bool boundary_emit_styled_path(void* context, RdtPath* path, Color color, const RenderBorderStroke* stroke) {
    PaintList* paint = (PaintList*)context;
    int index = paint_list_count(paint);
    if (stroke) paint_stroke_path(paint, path, color, stroke->width, stroke->cap, RDT_JOIN_MITER,
                                 stroke->dashes, stroke->dash_count, stroke->phase, nullptr);
    else paint_fill_path(paint, path, color, RDT_FILL_WINDING, nullptr);
    // paths and copied dash arrays must survive the caller's stack (D4.5.1v4).
    if (paint_list_take_path_payload(paint, index, stroke ? PAINT_STROKE_PATH : PAINT_FILL_PATH)) return true;
    rdt_path_free(path); return false;
}

bool render_paint_boundary_emit_border_side(PaintList* paint, const BorderProp* border, Rect rect, size_t side) {
    return render_border_emit_side(rect, border, side, false, boundary_emit_styled_path, paint);
}

bool render_paint_boundary_emit_simple(PaintList* paint_list, ViewBlock* view, Rect rect) {
    if (!paint_list || !view || !view->bound) return false;
    if (render_paint_boundary_emit_fragmented_simple(paint_list, view, rect)) return true;
    // continuous SVG/PDF retain their direct per-side painter beyond the uniform-color fast path.
    if (view->bound->border && !boundary_border_uniform_color(view->bound->border)) return false;
    return render_paint_boundary_emit_box(paint_list, view->bound, rect);
}

bool render_paint_boundary_emit_fragmented_simple(PaintList* paint_list, ViewBlock* view, Rect rect) {
    if (!paint_list || !view || !view->bound || view->width <= 0.0f) return false;
    // boundary painting also accepts retained blocks without a DOM node tag.
    LayoutFragmentBox* first = view->layout_fragment_list();
    if (!first || !first->next) return false;
    const BoundaryProp* bound = view->boundary();
    if (!render_paint_background_is_simple(bound->background) ||
        boundary_border_has_visible_side(bound->border) || boundary_has_radius(bound->border) ||
        bound->box_shadow || bound->outline ||
        (bound->border && (bound->border->border_image_url ||
            bound->border->border_image_type != GRADIENT_NONE))) return false;
    float scale = rect.width / view->width;
    Color color = bound->background ? bound->background->color : Color{};
    // CSS Break §5.4 paints each fragment's decoration; the DOMRect union
    // includes gutters that do not belong to the box's background.
    for (LayoutFragmentBox* fragment = first; fragment; fragment = fragment->next) {
        if (color.a && fragment->width > 0.0f && fragment->height > 0.0f) {
            paint_fill_rect(paint_list, rect.x + fragment->x * scale,
                rect.y + fragment->y * scale, fragment->width * scale,
                fragment->height * scale, color);
        }
    }
    return true;
}

bool render_paint_boundary_emit_box(PaintList* paint_list, BoundaryProp* bound, Rect rect) {
    if (!paint_list || !bound) return false;
    // image borders replace the solid ring; the image producer resolves load fallback.
    if (bound->border && (bound->border->border_image_url ||
            bound->border->border_image_type != GRADIENT_NONE)) return false;
    float x = rect.x, y = rect.y, width = rect.width, height = rect.height;
    if (width < 0.0f || height < 0.0f) return false;

    // resolve before selecting the simple or fallback SVG/PDF boundary painter.
    if (bound->border) {
        resolve_border_radius_percentages(&bound->border->radius, width, height);
        constrain_corner_radii(&bound->border->radius, width, height);
    }
    if (bound->box_shadow || bound->outline) return false;
    if (!render_paint_background_is_simple(bound->background)) return false;
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

    const float* widths = border->width.values;
    const float side_x[4] = {x, x + width - widths[1], x, x};
    const float side_y[4] = {y, y, y + height - widths[2], y};
    const float side_w[4] = {width, widths[1], width, widths[3]};
    const float side_h[4] = {widths[0], height, widths[2], height};
    bool uniform_color = boundary_border_uniform_color(border);
    for (int i = 0; i < 4; i++) {
        if (boundary_border_side_visible(widths[i], border->styles[i], border->colors[i])) {
            if (uniform_color && border->styles[i] == CSS_VALUE_SOLID) {
                boundary_emit_border_side(paint_list, side_x[i], side_y[i], side_w[i], side_h[i], border->colors[i]);
            } else if (!render_paint_boundary_emit_border_side(paint_list, border, rect, i)) return false;
        }
    }
    return true;
}

bool render_paint_boundary_emit_outer_shadows(PaintList* paint_list, ViewBlock* view,
                                              float x, float y) {
    if (!paint_list || !view || !view->bound || !view->boundary()->box_shadow) {
        return false;
    }

    float width = view->width;
    float height = view->height;
    if (width <= 0.0f || height <= 0.0f) return false;

    float radii[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (view->boundary()->border) {
        BorderProp* border = view->boundary()->border;
        for (int i = 0; i < 4; i++) radii[i] = border->radius.horizontal[i];
    }

    int shadow_count = 0;
    for (BoxShadow* shadow = view->boundary()->box_shadow; shadow; shadow = shadow->next) {
        shadow_count++;
    }

    bool emitted = false;
    for (int shadow_index = shadow_count - 1; shadow_index >= 0; shadow_index--) {
        BoxShadow* shadow = view->boundary()->box_shadow;
        for (int i = 0; i < shadow_index && shadow; i++) {
            shadow = shadow->next;
        }
        if (!shadow || shadow->inset || shadow->color.a == 0) {
            continue;
        }

        float spread = shadow->spread_radius;
        Rect shadow_rect = render_geometry_outer_shadow_rect(
            {x, y, width, height}, shadow->offset_x, shadow->offset_y, spread);
        float shadow_x = shadow_rect.x, shadow_y = shadow_rect.y;
        float shadow_w = shadow_rect.width, shadow_h = shadow_rect.height;
        if (shadow_w <= 0.0f || shadow_h <= 0.0f) {
            continue;
        }

        Corner radius = render_path_uniform_corner(radii[0], radii[1], radii[2], radii[3]);
        Corner shadow_radius = render_geometry_outset_corners(&radius, width, height,
            spread, spread, spread, spread);
        const float* shadow_radii = shadow_radius.horizontal;

        float exclude_params[RDT_CLIP_PARAM_COUNT] = {
            x, y, width, height,
            radii[0], radii[1], radii[2], radii[3]
        };

        paint_outer_shadow(paint_list,
                           shadow_x, shadow_y, shadow_w, shadow_h,
                           shadow_radii[0], shadow_radii[1], shadow_radii[2], shadow_radii[3],
                           shadow->color,
                           shadow->blur_radius,
                           CLIP_SHAPE_ROUNDED_RECT, exclude_params,
                           0, nullptr);
        emitted = true;
    }
    return emitted;
}

Color render_gradient_stop_color(const RdtGradientStop& stop) {
    return Color{render_pixel_pack_abgr(stop.r, stop.g, stop.b, stop.a)};
}

void render_set_uniform_gradient_stops(RdtGradientStop* stops, Color color) {
    stops[0] = {0.0f, color.r, color.g, color.b, color.a};
    stops[1] = stops[0]; stops[1].offset = 1.0f;
}

Color render_sample_gradient_stops(const RdtGradientStop* stops, int count, float position, float stop_scale) {
    if (!stops || count < 1) return Color{};
    if (position < stops[0].offset * stop_scale) return render_gradient_stop_color(stops[0]);
    // fixed-up stops are ordered; upper-bound lookup preserves the following hard-stop color.
    int first = 1, last = count;
    while (first < last) {
        int middle = first + (last - first) / 2;
        if (position < stops[middle].offset * stop_scale) last = middle;
        else first = middle + 1;
    }
    if (first < count) {
        const RdtGradientStop& before = stops[first - 1];
        const RdtGradientStop& after = stops[first];
        float start = before.offset * stop_scale, extent = (after.offset - before.offset) * stop_scale;
        float fraction = extent > 0.0f ? (position - start) / extent : 0.0f;
        return css_interpolate_color(render_gradient_stop_color(before), render_gradient_stop_color(after), fraction);
    }
    return render_gradient_stop_color(stops[count - 1]);
}

bool render_normalize_linear_gradient_range(RadiantGradientLine* line,
        RdtGradientStop* stops, int count) {
    if (!line || !stops || count < 2) return false;
    for (int index = 0; index < count; index++) if (!isfinite(stops[index].offset)) return false;
    double first = fmin(0.0, stops[0].offset);
    double last = fmax(1.0, stops[count - 1].offset);
    if (first == 0.0 && last == 1.0) return true;
    double vx = (double)line->x2 - line->x1, vy = (double)line->y2 - line->y1;
    RadiantGradientLine expanded = {
        (float)(line->x1 + first * vx), (float)(line->y1 + first * vy),
        (float)(line->x1 + last * vx), (float)(line->y1 + last * vy)};
    if (!isfinite(expanded.x1) || !isfinite(expanded.y1) ||
        !isfinite(expanded.x2) || !isfinite(expanded.y2)) return false;
    // css stops extend beyond the box; native offsets must use the extended line.
    for (int index = 0; index < count; index++)
        stops[index].offset = (float)(((double)stops[index].offset - first) / (last - first));
    *line = expanded;
    return true;
}

bool render_normalize_radial_gradient_range(RadiantRadialGeometry* geometry,
        RdtGradientStop* stops, int* count) {
    if (!geometry || !stops || !count || *count < 2) return false;
    for (int index = 0; index < *count; index++) if (!isfinite(stops[index].offset)) return false;
    float extent = fmaxf(1.0f, stops[*count - 1].offset);
    float rx = geometry->rx * extent, ry = geometry->ry * extent;
    if (!isfinite(rx) || !isfinite(ry)) return false;
    int first = 0;
    while (first < *count && stops[first].offset < 0.0f) first++;
    if (first == *count) {
        render_set_uniform_gradient_stops(stops, render_gradient_stop_color(stops[*count - 1]));
        *count = 2;
    } else if (first > 0) {
        // negative radial positions contribute to the center through interpolation.
        Color center = render_sample_gradient_stops(stops, *count, 0.0f, 1.0f);
        memmove(stops + 1, stops + first, (size_t)(*count - first) * sizeof(RdtGradientStop));
        *count -= first - 1;
        stops[0] = {0.0f, center.r, center.g, center.b, center.a};
    }
    for (int index = 0; index < *count; index++) stops[index].offset /= extent;
    geometry->rx = rx; geometry->ry = ry;
    return true;
}

bool render_copy_gradient_stops(const GradientStop* src, int src_count, float gradient_length,
                                         RdtGradientStop* stops, int stop_capacity,
                                         int* out_count) {
    if (!src || src_count < 2 || !stops || stop_capacity < src_count) return false;
    for (int i = 0; i < src_count; i++) {
        const GradientStop* stop = &src[i];
        // positions remain unresolved until both computed owner units and the used line are available.
        float pos = stop->position;
        if (stop->position_expression) {
            float used = radiant::resolve_computed_length_percentage(stop->position_expression.get(), gradient_length);
            if (!isfinite(used)) return false;
            pos = gradient_length > 0.0f ? used / gradient_length : 0.0f;
        } else if (stop->position_is_px) {
            pos = gradient_length > 0.0f ? pos / gradient_length : 0.0f;
        }
        stops[i] = {pos, stop->color.r, stop->color.g, stop->color.b, stop->color.a};
    }
    // CSS Images 3 fixup: endpoints, monotonic specified stops, then each omitted run.
    if (isnan(stops[0].offset)) stops[0].offset = 0.0f;
    if (isnan(stops[src_count - 1].offset)) stops[src_count - 1].offset = 1.0f;
    float previous = stops[0].offset;
    for (int index = 1; index < src_count; index++) {
        if (isnan(stops[index].offset)) continue;
        stops[index].offset = fmaxf(previous, stops[index].offset);
        previous = stops[index].offset;
    }
    for (int index = 1; index < src_count - 1; index++) {
        if (!isnan(stops[index].offset)) continue;
        int next = index + 1;
        while (next < src_count && isnan(stops[next].offset)) next++;
        float first = stops[index - 1].offset;
        float step = (stops[next].offset - first) / (float)(next - index + 1);
        for (int fill = index; fill < next; fill++) stops[fill].offset = first + step * (float)(fill - index + 1);
        index = next - 1;
    }
    if (out_count) *out_count = src_count;
    return true;
}

static bool boundary_prepare_gradient(ViewBlock* view, float x, float y,
                                      GradientStop* source_stops, int source_count,
                                      float gradient_length,
                                      RdtGradientStop* stops, int stop_capacity,
                                      RdtPath** path, int* stop_count) {
    if (view->width <= 0.0f || view->height <= 0.0f ||
        !render_copy_gradient_stops(source_stops, source_count, gradient_length,
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
    if (!view || !view->bound || !view->boundary()->background || !out) return false;
    BackgroundProp* bg = view->boundary()->background;
    LinearGradient* gradient = bg->linear_gradient;
    if (bg->gradient_type != GRADIENT_LINEAR || !gradient) return false;

    Rect rect = {x, y, view->width, view->height};
    RadiantGradientLine line = radiant_linear_gradient_line(
        rect, radiant_linear_gradient_used_angle(gradient, rect));
    float gradient_length = hypotf(line.x2 - line.x1, line.y2 - line.y1);
    int stop_count = 0;
    RdtPath* path;
    if (!boundary_prepare_gradient(view, x, y, gradient->stops, gradient->stop_count,
                                   gradient_length, stops, stop_capacity, &path, &stop_count)) return false;

    if (!render_normalize_linear_gradient_range(&line, stops, stop_count)) { rdt_path_free(path); return false; }
    out->path = lam::up(path);
    out->x1 = line.x1;
    out->y1 = line.y1;
    out->x2 = line.x2;
    out->y2 = line.y2;
    out->stops = lam::up(stops);
    out->stop_count = stop_count;
    return true;
}

bool render_paint_boundary_build_radial_gradient(ViewBlock* view, float x, float y,
                                                 RdtGradientStop* stops,
                                                 int stop_capacity,
                                                 BoundaryRadialGradientPaint* out) {
    if (!view || !view->bound || !view->boundary()->background || !out) return false;
    BackgroundProp* bg = view->boundary()->background;
    RadialGradient* gradient = bg->radial_gradient;
    if (bg->gradient_type != GRADIENT_RADIAL || !gradient) return false;

    float radius = fminf(view->width, view->height) * 0.5f;
    int stop_count = 0;
    RdtPath* path;
    if (!boundary_prepare_gradient(view, x, y, gradient->stops, gradient->stop_count,
                                   radius, stops, stop_capacity, &path, &stop_count)) return false;

    RadiantRadialGeometry geometry = {
        x + (gradient->cx_set ? gradient->cx * view->width : view->width * 0.5f),
        y + (gradient->cy_set ? gradient->cy * view->height : view->height * 0.5f), radius, radius};
    if (!render_normalize_radial_gradient_range(&geometry, stops, &stop_count)) { rdt_path_free(path); return false; }
    out->path = lam::up(path);
    out->cx = geometry.cx; out->cy = geometry.cy; out->r = geometry.rx;
    out->stops = lam::up(stops);
    out->stop_count = stop_count;
    return true;
}
