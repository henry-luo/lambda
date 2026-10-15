#include "render.hpp"
#include "glyph_sampling.hpp"
#include "view.hpp"
#include "render_effect_raster_fallback.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/str.h"
#include "../lambda/input/css/css_value.hpp"
#include <assert.h>
#include <limits.h>
#include <math.h>

/**
 * Main background rendering dispatch
 */

static RdtPath* background_image_clip_path(RasterRenderContext* rdcon, ViewBlock* view);
static void render_background_image(RasterRenderContext* rdcon, ViewBlock* view,
                                    BackgroundLayer* bg, Rect position_rect, Rect paint_rect);
static int background_layer_count(const BackgroundProp* background) {
    return background->layer_count > 0 ? background->layer_count : 1;
}

static const BackgroundLayer* background_layer_at(const BackgroundProp* background, int index) {
    return background->layer_count > 0 ? &background->layers[index] : background;
}

static BackgroundLayer* background_layer_at(BackgroundProp* background, int index) {
    return const_cast<BackgroundLayer*>(background_layer_at(static_cast<const BackgroundProp*>(background), index));
}

struct BackgroundGlyphMask {
    const GlyphBitmap* bitmap;
    float x, y;
    RasterRenderContext* context;
    mutable lam::ArrayList<ImageSurface*> leases{MEM_CAT_RENDER, 0};
    ~BackgroundGlyphMask() { for (ImageSurface* surface : leases) image_surface_snapshot_release(surface); }
};
static bool background_record_masked_layer(PaintList* destination, const PaintList& layer,
    const BackgroundGlyphMask& mask, float density);
static bool record_background(PaintList* paint, UiContext* ui, ViewBlock* view, Rect rect,
                              float pixel_ratio, const RdtMatrix* transform, RasterRenderContext* raster,
                              bool text_only = false, RdtPath* text_shape = nullptr, const BackgroundGlyphMask* glyph_mask = nullptr);
static lam::Temp<RdtGradientStop> background_repeat_linear_stops(
    const RdtGradientStop* stops, int count, float physical_length, int* expanded_count);

static bool background_rounded_radius(ViewBlock* view, Rect rect, Corner* out_radius) {
    BorderProp* border = (view && view->bound) ? view->boundary()->border : nullptr;
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

static Corner background_corner_inset_box(const Corner* radius, CssEnum box,
                                          const BorderProp* border,
                                          const Spacing* padding,
                                          float scale) {
    Corner out = radiant_corner_scaled(radius, scale);
    float insets[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (box == CSS_VALUE_PADDING_BOX || box == CSS_VALUE_CONTENT_BOX) {
        for (int i = 0; i < 4; i++) {
            insets[i] += border ? border->width.values[i] * scale : 0.0f;
        }
    }
    if (box == CSS_VALUE_CONTENT_BOX && padding) {
        for (int i = 0; i < 4; i++) insets[i] += padding->values[i] * scale;
    }
    return radiant_corner_inset_edges(&out, insets[0], insets[1], insets[2], insets[3]);
}

void render_background_layer_rects(UiContext* ui, ViewBlock* view, BackgroundGeometry* bg,
        Rect rect, float s, Rect* position_rect, Rect* paint_rect) {
    BorderProp* border = view->boundary()->border;
    const Spacing* padding = &view->boundary()->padding;
    // Determine positioning area (bg-origin, default: padding-box)
    // Controls where background-position and background-size are calculated relative to.
    CssEnum origin = bg->bg_origin ? bg->bg_origin : CSS_VALUE_PADDING_BOX;
    *position_rect = render_geometry_adjust_box_rect(rect, origin, s, border, padding);
    if (bg->bg_attachment == CSS_VALUE_FIXED && ui) {
        // Fixed image layers use the initial containing block as their positioning area.
        (*position_rect) = {0.0f, 0.0f,
            ui->viewport_width * s,
            ui->viewport_height * s};
    } else if (bg->bg_attachment == CSS_VALUE_LOCAL && view->scroller &&
               view->scroll()->pane && ui &&
               ui->document) {
        float scroll_x = 0.0f, scroll_y = 0.0f;
        float max_x = 0.0f, max_y = 0.0f;
        scroll_state_get_position_for_view(
            (DocState*)ui->document->state, (View*)view,
            view->scroll()->pane, &scroll_x, &scroll_y, &max_x, &max_y);
        // Local layers size against scrollable overflow and move with its content.
        (*position_rect).x -= scroll_x * s;
        (*position_rect).y -= scroll_y * s;
        (*position_rect).width += max_x * s;
        (*position_rect).height += max_y * s;
    }

    // Determine paint area (bg-clip, default: border-box)
    // Controls where the background is visually clipped.
    CssEnum clip_box = bg->bg_clip ? bg->bg_clip : CSS_VALUE_BORDER_BOX;
    *paint_rect = render_geometry_adjust_box_rect(rect, clip_box, s, border, padding);

}

void render_background(RasterRenderContext* rdcon, ViewBlock* view, Rect rect) {
    if (!rdcon || !rdcon->paint_list || !rdcon->dl || !view || !view->bound ||
            !view->boundary()->background) return;
    float scale = rdcon->raster_scale;
    if (scale <= 0.0f) return;
    Rect logical = {rect.x / scale, rect.y / scale, rect.width / scale, rect.height / scale};
    RdtMatrix density = rdt_matrix_scale(scale, scale);
    const RdtMatrix* active = render_state_current_transform(rdcon);
    RdtMatrix transform = active ? rdt_matrix_multiply(active, &density) : density;
    RdtPath* clip = render_path_create_clip_path(rdcon);
    rc_push_clip(rdcon, clip, nullptr);
    // background descriptors stay logical; only this raster adapter adds output density.
    record_background(rdcon->paint_list, rdcon->ui_context, view, logical, scale, &transform, rdcon);
    paint_ir_lower_raster_fragment(rdcon->paint_list, rdcon->dl);
    paint_list_clear(rdcon->paint_list);
    rc_pop_clip(rdcon);
    rdt_path_free(clip);
}

static Color gradient_image_average_color(const RdtGradientStop* stops, int count) {
    double extent = (double)stops[count - 1].offset - stops[0].offset;
    double red = 0.0, green = 0.0, blue = 0.0, alpha = 0.0;
    for (int index = 1; index < count; index++) {
        const RdtGradientStop& before = stops[index - 1];
        const RdtGradientStop& after = stops[index];
        // CSS Images 3 §3.3 spaces zero-period stops equally before premultiplied averaging.
        double weight = extent > 0.0 ? ((double)after.offset - before.offset) / (2.0 * extent)
            : 0.5 / (count - 1);
        alpha += weight * (before.a + after.a);
        red += weight * ((double)before.r * before.a + (double)after.r * after.a);
        green += weight * ((double)before.g * before.a + (double)after.g * after.a);
        blue += weight * ((double)before.b * before.a + (double)after.b * after.a);
    }
    return Color{render_pixel_pack_abgr(
        clamp_byte_round(alpha > 0.0 ? (float)(red / alpha) : 0.0f),
        clamp_byte_round(alpha > 0.0 ? (float)(green / alpha) : 0.0f),
        clamp_byte_round(alpha > 0.0 ? (float)(blue / alpha) : 0.0f),
        clamp_byte_round((float)alpha))};
}

static lam::Temp<RdtGradientStop> background_repeat_linear_stops(
        const RdtGradientStop* stops, int count, float physical_length, int* expanded_count) {
    if (!stops || count < 2 || !expanded_count) return {};
    float first = stops[0].offset, period = stops[count - 1].offset - first;
    if (!isfinite(first) || !isfinite(period)) return {};
    // a period shorter than one output pixel cannot be resolved by that device.
    if (period <= 0.0f || (isfinite(physical_length) && period * physical_length < 1.0f)) {
        lam::Temp<RdtGradientStop> solid = lam::temp_array<RdtGradientStop>(2, MEM_CAT_RENDER);
        if (!solid) return {};
        Color average = gradient_image_average_color(stops, count);
        render_set_uniform_gradient_stops(solid.get(), average);
        *expanded_count = 2;
        return solid;
    }
    double begin = floor(-(double)first / period);
    double end = ceil((1.0 - (double)first) / period);
    double capacity = (end - begin + 1.0) * count + 2.0;
    if (!isfinite(capacity) || capacity < 2.0 || capacity > INT_MAX) return {};
    size_t bytes = 0;
    if (!math_checked_mul((size_t)capacity, sizeof(RdtGradientStop), &bytes) ||
        !render_memory_allow_allocation(nullptr, bytes)) return {};
    lam::Temp<RdtGradientStop> expanded = lam::temp_array<RdtGradientStop>((size_t)capacity, MEM_CAT_RENDER);
    if (!expanded) return {};
    auto endpoint = [&](float position) {
        Color color = render_sample_gradient_stops(stops, count,
            first + math_wrap_positive_f(position - first, period), 1.0f);
        return RdtGradientStop{position, color.r, color.g, color.b, color.a};
    };
    int used = 0;
    expanded[used++] = endpoint(0.0f);
    // ordinals are counts; double arithmetic avoids cancellation for a distant first stop.
    for (double repeat = begin; repeat <= end; repeat += 1.0) {
        for (int index = 0; index < count; index++) {
            float offset = (float)((double)stops[index].offset + repeat * period);
            if (offset < 0.0f || offset > 1.0f) continue;
            expanded[used] = stops[index]; expanded[used++].offset = offset;
        }
    }
    expanded[used++] = endpoint(1.0f);
    *expanded_count = used;
    return expanded;
}

static void gradient_image_sample_pixels(uint32_t* pixels, int width, int height, int stride,
        float density, float cx, float cy, float rx, float ry, float from,
        bool conic, bool repeating, const RdtGradientStop* stops, int count) {
    // radial repeats wrap in used length space, avoiding cancellation around coincident px stops.
    float stop_scale = conic ? 1.0f : rx;
    float first = stops[0].offset * stop_scale, period = stops[count - 1].offset * stop_scale - first;
    Color average = repeating && period <= 0.0f ? gradient_image_average_color(stops, count) : Color{};
    for (int row = 0; row < height; row++) {
        for (int column = 0; column < width; column++) {
            float dx = (float)column / density - cx;
            float dy = (float)row / density - cy;
            float position = conic
                ? math_wrap_positive_f(atan2f(dy, dx) - from, math_tau_f()) / math_tau_f()
                : rx > 0.0f && ry > 0.0f ? hypotf(dx, dy * (rx / ry)) : rx;
            if (repeating && period > 0.0f)
                position = first + math_wrap_positive_f(position - first, period);
            Color color = repeating && period <= 0.0f
                ? average
                : render_sample_gradient_stops(stops, count, position, stop_scale);
            pixels[(size_t)row * (size_t)stride + (size_t)column] = render_pixel_premultiply_abgr(color.c);
        }
    }
}

static bool gradient_image_record_pixels(PaintList* paint, const ListStyleImage* image,
        Rect rect, float density, const RdtGradientStop* stops, int count,
        const RdtMatrix* transform) {
    float pixel_width = ceilf(rect.width * density);
    float pixel_height = ceilf(rect.height * density);
    size_t bytes;
    if (!render_surface_allocation_size(pixel_width, pixel_height, &bytes)) return false;
    // INT_CAST_OK: the checked sample extents index a raster image buffer.
    int width = (int)pixel_width, height = (int)pixel_height;
    ImageSurface* surface = image_surface_create(width, height);
    if (!surface) return false;
    const ConicGradient* conic = image->conic_gradient;
    const RadialGradient* radial = image->radial_gradient;
    RadiantRadialGeometry geometry = radial ? radiant_radial_gradient_geometry(radial, rect)
        : RadiantRadialGeometry{};
    RadiantGradientCenter center = conic ? radiant_gradient_center(conic, rect)
        : RadiantGradientCenter{geometry.cx, geometry.cy};
    float cx = center.x - rect.x, cy = center.y - rect.y;
    float from = conic ? math_degrees_to_radians(conic->from_angle - 90.0f) : 0.0f;
    bool repeating = conic ? conic->is_repeating : radial->is_repeating;
    gradient_image_sample_pixels((uint32_t*)surface->pixels, width, height, surface->pitch / 4,
        density, cx, cy, geometry.rx, geometry.ry, from, conic != nullptr, repeating, stops, count);
    int index = paint_list_count(paint);
    // semantic commands own generated pixels until every deferred lowerer has consumed them.
    paint_draw_image_resource(paint, surface, rect.x, rect.y, rect.width, rect.height,
        255, transform, image_surface_destroy);
    return paint_list_count(paint) == index + 1;
}

static bool gradient_stops_uniform_color(const GradientStop* stops, int count, Color* color) {
    if (!stops || count < 2) return false;
    for (int index = 1; index < count; index++)
        if (stops[index].color.c != stops[0].color.c) return false;
    if (color) *color = stops[0].color;
    return true;
}

template<typename G> static bool gradient_image_needs_sampling(const G* gradient) {
    return gradient && !gradient_stops_uniform_color(gradient->stops, gradient->stop_count, nullptr);
}

struct GradientImageParts {
    const LinearGradient* linear;
    const RadialGradient* radial;
    const ConicGradient* conic;
    const GradientStop* stops;
    int count;
};

static GradientImageParts gradient_image_parts(const ListStyleImage* image) {
    GradientImageParts parts = {};
    parts.linear = image->gradient_type == GRADIENT_LINEAR ? image->linear_gradient : nullptr;
    parts.radial = image->gradient_type == GRADIENT_RADIAL ? image->radial_gradient : nullptr;
    parts.conic = image->gradient_type == GRADIENT_CONIC ? image->conic_gradient : nullptr;
    parts.stops = parts.linear ? parts.linear->stops : parts.radial ? parts.radial->stops : parts.conic ? parts.conic->stops : nullptr;
    parts.count = parts.linear ? parts.linear->stop_count : parts.radial ? parts.radial->stop_count : parts.conic ? parts.conic->stop_count : 0;
    return parts;
}

static bool gradient_interval_is_uniform(const RdtGradientStop* stops, int count,
        float first, float last, Color* color) {
    for (int index = 0; index < count; index++) if (!isfinite(stops[index].offset)) return false;
    for (int index = 1; index < count; index++) {
        const RdtGradientStop& before = stops[index - 1];
        const RdtGradientStop& after = stops[index];
        bool intersects = after.offset > before.offset && after.offset > first && before.offset < last;
        if (intersects && render_gradient_stop_color(before).c != render_gradient_stop_color(after).c) return false;
    }
    *color = render_sample_gradient_stops(stops, count, first, 1.0f);
    for (int index = 0; index < count; index++)
        if (stops[index].offset > first && stops[index].offset < last &&
            render_gradient_stop_color(stops[index]).c != color->c) return false;
    return true;
}

bool render_paint_solid_shape(PaintList* paint, RdtPath* shape, Color color,
        const RdtMatrix* transform) {
    RdtPath* path = rdt_path_clone(shape);
    if (!path) return false;
    int index = paint_list_count(paint);
    paint_fill_path(paint, path, color, RDT_FILL_WINDING, transform);
    if (paint_list_take_path_payload(paint, index, PAINT_FILL_PATH)) return true;
    rdt_path_free(path);
    return false;
}

// constant opaque intervals use the solid contour consumer, avoiding gradient color rounding at glyph edges.
static bool gradient_shape_has_constant_intervals(const RdtGradientStop* stops, int count) {
    for (int i = 0; i < count; i++) {
        if (stops[i].a != 255) return false;
        if (i && stops[i].offset > stops[i - 1].offset &&
            render_gradient_stop_color(stops[i]).c != render_gradient_stop_color(stops[i - 1]).c) return false;
    }
    return true;
}

static bool gradient_shape_record_constant_intervals(PaintList* paint, RdtPath* shape,
        Rect rect, const RadiantGradientLine& line, const RdtGradientStop* stops, int count,
        const RdtMatrix* transform) {
    float length = hypotf(line.x2 - line.x1, line.y2 - line.y1);
    if (!(length > 0.0f)) return false;
    float ux = (line.x2 - line.x1) / length, uy = (line.y2 - line.y1) / length;
    float vx = -uy, vy = ux;
    float min_u = INFINITY, max_u = -INFINITY, min_v = INFINITY, max_v = -INFINITY;
    for (int corner = 0; corner < 4; corner++) {
        float x = rect.x + ((corner & 1) ? rect.width : 0.0f) - line.x1;
        float y = rect.y + ((corner & 2) ? rect.height : 0.0f) - line.y1;
        float u = x * ux + y * uy, v = x * vx + y * vy;
        min_u = fminf(min_u, u); max_u = fmaxf(max_u, u);
        min_v = fminf(min_v, v); max_v = fmaxf(max_v, v);
    }
    RdtMatrix basis = {ux, vx, line.x1, uy, vy, line.y1, 0, 0, 1};
    RdtMatrix mapped = transform ? rdt_matrix_multiply(transform, &basis) : basis;
    size_t checkpoint = paint->size();
    RdtPath* box = background_rect_path(rect);
    if (!box) return false;
    paint_push_clip(paint, box, transform); rdt_path_free(box);
    if (paint->size() != checkpoint + 1) return false;
    // shared clip edges paint in owner coordinate order even when the gradient axis is reversed.
    bool reverse = ux < 0.0f || (ux == 0.0f && uy < 0.0f);
    for (int step = 0; step < count; step++) {
        int i = reverse ? count - 1 - step : step;
        float begin = i == 0 ? min_u : fmaxf(min_u, stops[i].offset * length);
        float end = i + 1 == count ? max_u : fminf(max_u, stops[i + 1].offset * length);
        if (end <= begin) continue;
        Rect band = {begin, min_v, end - begin, max_v - min_v};
        const RdtMatrix* band_transform = &mapped;
        if (ux == 0.0f || uy == 0.0f) {
            float left, top, right, bottom;
            if (!rdt_matrix_project_rect_bounds(&basis, band.x, band.y, band.x + band.width,
                    band.y + band.height, &left, &top, &right, &bottom)) {
                paint->rewind(checkpoint); return false;
            }
            // preserve compact rectangles when the stripe is axis aligned, including reversed directions.
            band = {left, top, right - left, bottom - top}; band_transform = transform;
        }
        RdtPath* stripe = background_rect_path(band);
        if (!stripe) { paint->rewind(checkpoint); return false; }
        int index = paint_list_count(paint);
        paint_push_clip(paint, stripe, band_transform); rdt_path_free(stripe);
        if (paint_list_count(paint) != index + 1) { paint->rewind(checkpoint); return false; }
        index = paint_list_count(paint);
        if (!render_paint_solid_shape(paint, shape, render_gradient_stop_color(stops[i]), transform)) {
            paint->rewind(checkpoint); return false;
        }
        paint_pop_clip(paint);
        if (paint_list_count(paint) != index + 2) { paint->rewind(checkpoint); return false; }
    }
    size_t end = paint->size(); paint_pop_clip(paint);
    if (paint->size() != end + 1) { paint->rewind(checkpoint); return false; }
    return true;
}

static bool gradient_image_record_shape(PaintList* paint, const ListStyleImage* image, Rect rect,
    float pixel_ratio, const RdtMatrix* transform, RdtPath* shape) {
    if (!paint || !image || !isfinite(rect.x) || !isfinite(rect.y) ||
        !isfinite(rect.width) || !isfinite(rect.height) || rect.width <= 0.0f || rect.height <= 0.0f ||
        !isfinite(pixel_ratio) || pixel_ratio <= 0.0f) return false;
    GradientImageParts parts = gradient_image_parts(image);
    const LinearGradient* linear = parts.linear;
    const RadialGradient* radial = parts.radial;
    const ConicGradient* conic = parts.conic;
    const GradientStop* source = parts.stops;
    int count = parts.count;
    if (!source || count < 2) return false;
    RadiantGradientLine line = linear ? radiant_linear_gradient_line(rect,
        radiant_linear_gradient_used_angle(linear, rect)) : RadiantGradientLine{};
    RadiantRadialGeometry geometry = radial ? radiant_radial_gradient_geometry(radial, rect)
        : RadiantRadialGeometry{};
    float length = linear ? hypotf(line.x2 - line.x1, line.y2 - line.y1) : radial ? geometry.rx : 1.0f;
    lam::Temp<RdtGradientStop> stops = lam::temp_array<RdtGradientStop>((size_t)count, MEM_CAT_RENDER);
    if (!stops || !render_copy_gradient_stops(source, count, length, stops.get(), count, nullptr)) return false;
    if (conic || (radial && radial->is_repeating)) {
        int start = paint_list_count(paint);
        if (shape) {
            paint_push_clip(paint, shape, transform);
            if (paint_list_count(paint) != start + 1) return false;
        }
        bool recorded = gradient_image_record_pixels(paint, image, rect, pixel_ratio, stops.get(), count, transform);
        if (shape) paint_pop_clip(paint);
        return recorded;
    }
    Color uniform_color = {};
    bool uniform_image = false;
    if ((linear && !linear->is_repeating) || (radial && geometry.rx > 0.0f && geometry.ry > 0.0f)) {
        float last = radial ? hypotf(
            fmaxf(fabsf(rect.x - geometry.cx), fabsf(rect.x + rect.width - geometry.cx)) / geometry.rx,
            fmaxf(fabsf(rect.y - geometry.cy), fabsf(rect.y + rect.height - geometry.cy)) / geometry.ry) : 1.0f;
        // stops outside the painted interval can make a multicolor source a solid image.
        uniform_image = gradient_interval_is_uniform(stops.get(), count, 0.0f, last, &uniform_color);
    }
    if (uniform_image) {
        // retain gradient alpha lowering; a solid path uses different edge/blend quantization.
        render_set_uniform_gradient_stops(stops.get(), uniform_color);
        count = 2;
    }
    RdtPath* path = shape ? rdt_path_clone(shape) : background_rect_path(rect);
    if (!path) return false;
    int index = paint_list_count(paint);
    PaintOp op = linear ? PAINT_FILL_LINEAR_GRADIENT : PAINT_FILL_RADIAL_GRADIENT;
    if (radial && (geometry.rx <= 0.0f || geometry.ry <= 0.0f)) {
        // a degenerate radial image has no invertible ellipse.
        op = PAINT_FILL_PATH;
        paint_fill_path(paint, path, render_gradient_stop_color(stops[count - 1]),
            RDT_FILL_WINDING, transform);
    } else if (linear) {
        if (linear->is_repeating) {
            float physical_x1 = line.x1 * pixel_ratio, physical_y1 = line.y1 * pixel_ratio;
            float physical_x2 = line.x2 * pixel_ratio, physical_y2 = line.y2 * pixel_ratio;
            if (transform) {
                rdt_matrix_transform_point(transform, line.x1, line.y1, &physical_x1, &physical_y1);
                rdt_matrix_transform_point(transform, line.x2, line.y2, &physical_x2, &physical_y2);
            }
            lam::Temp<RdtGradientStop> repeated = background_repeat_linear_stops(stops.get(), count,
                hypotf(physical_x2 - physical_x1, physical_y2 - physical_y1), &count);
            if (!repeated) { rdt_path_free(path); return false; }
            stops.reset(repeated.release());
        }
        if (!render_normalize_linear_gradient_range(&line, stops.get(), count)) { rdt_path_free(path); return false; }
        if (shape && gradient_shape_has_constant_intervals(stops.get(), count)) {
            rdt_path_free(path);
            return gradient_shape_record_constant_intervals(paint, shape, rect, line, stops.get(), count, transform);
        }
        paint_fill_linear_gradient(paint, path, line.x1, line.y1, line.x2, line.y2,
            stops.get(), count, RDT_FILL_WINDING, transform, nullptr);
    } else {
        if (!render_normalize_radial_gradient_range(&geometry, stops.get(), &count)) { rdt_path_free(path); return false; }
        float ratio = geometry.rx > 0.0f ? geometry.ry / geometry.rx : 1.0f;
        RdtMatrix ellipse = {1, 0, 0, 0, ratio, geometry.cy * (1.0f - ratio), 0, 0, 1};
        paint_fill_radial_gradient(paint, path, geometry.cx, geometry.cy, geometry.rx,
            stops.get(), count, RDT_FILL_WINDING, transform, &ellipse);
    }
    if (paint_list_take_path_payload(paint, index, op, op == PAINT_FILL_PATH ? nullptr : stops.get())) {
        if (op != PAINT_FILL_PATH) stops.release();
        return true;
    }
    rdt_path_free(path);
    return false;
}

bool render_paint_gradient_image(PaintList* paint, const ListStyleImage* image, Rect rect,
    float pixel_ratio, const RdtMatrix* transform) {
    return gradient_image_record_shape(paint, image, rect, pixel_ratio, transform, nullptr);
}

bool render_paint_gradient_shape(PaintList* paint, const ListStyleImage* image, Rect rect,
    float pixel_ratio, const RdtMatrix* transform, RdtPath* shape) {
    return shape && gradient_image_record_shape(paint, image, rect, pixel_ratio, transform, shape);
}

static uint32_t* background_gradient_pixel_buffer(RasterRenderContext* rdcon, Rect rect,
                                                   int* width, int* height) {
    if (!rdcon || !rdcon->dl || !width || !height) return nullptr;
    // INT_CAST_OK: raster image indexing requires integer pixel dimensions.
    int w = (int)(rect.width + 0.5f);
    // INT_CAST_OK: raster image indexing requires integer pixel dimensions.
    int h = (int)(rect.height + 0.5f);
    if (w <= 0 || h <= 0 || (size_t)w > SIZE_MAX / (size_t)h / sizeof(uint32_t)) {
        return nullptr;
    }
    // Display-list replay borrows the generated image after recording completes.
    uint32_t* pixels = (uint32_t*)scratch_alloc(
        &rdcon->dl->arena, (size_t)w * (size_t)h * sizeof(uint32_t));
    if (!pixels) return nullptr;
    *width = w;
    *height = h;
    return pixels;
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
    int box_r = (int)render_box_blur_half_width(blur_radius); // INT_CAST_OK: discrete convolution kernel half-width

    uint32_t* pixels = (uint32_t*)surface->pixels;
    int stride = surface->pitch / 4;  // pitch is in bytes, pixels are 4 bytes

    // Allocate temporary buffer for one pass
    size_t buf_size = (size_t)rw * rh;
    ScratchScope scope(sa);
    uint32_t* temp = scope.array<uint32_t>(buf_size);
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
                    radiant_blur_adjust(src_row[sx], true,
                        &sum_r, &sum_g, &sum_b, &sum_a);
                    count++;
                }
            }

            for (int col = 0; col < rw; col++) {
                if (count > 0) {
                    temp[row * rw + col] = radiant_blur_average(
                        sum_r, sum_g, sum_b, sum_a, count);
                } else {
                    temp[row * rw + col] = 0;
                }

                // Slide window: remove leftmost, add new rightmost
                int remove_x = rx + col - box_r;
                int add_x = rx + col + box_r + 1;

                if (remove_x >= rx && remove_x < rx + rw) {
                    radiant_blur_adjust(src_row[remove_x], false,
                        &sum_r, &sum_g, &sum_b, &sum_a);
                    count--;
                }
                if (add_x >= rx && add_x < rx + rw) {
                    radiant_blur_adjust(src_row[add_x], true,
                        &sum_r, &sum_g, &sum_b, &sum_a);
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
                    radiant_blur_adjust(temp[tr * rw + col], true,
                        &sum_r, &sum_g, &sum_b, &sum_a);
                    count++;
                }
            }

            for (int row = 0; row < rh; row++) {
                if (count > 0) {
                    pixels[(ry + row) * stride + (rx + col)] = radiant_blur_average(
                        sum_r, sum_g, sum_b, sum_a, count);
                }

                int remove_r = row - box_r;
                int add_r = row + box_r + 1;

                if (remove_r >= 0 && remove_r < rh) {
                    radiant_blur_adjust(temp[remove_r * rw + col], false,
                        &sum_r, &sum_g, &sum_b, &sum_a);
                    count--;
                }
                if (add_r >= 0 && add_r < rh) {
                    radiant_blur_adjust(temp[add_r * rw + col], true,
                        &sum_r, &sum_g, &sum_b, &sum_a);
                    count++;
                }
            }
        }
    }

}

static bool clip_surface_region(ImageSurface* surface, int* rx, int* ry, int* rw, int* rh) {
    if (*rx < 0) { *rw += *rx; *rx = 0; }
    if (*ry < 0) { *rh += *ry; *ry = 0; }
    if (*rx + *rw > surface->width) *rw = surface->width - *rx;
    if (*ry + *rh > surface->height) *rh = surface->height - *ry;
    return *rw > 0 && *rh > 0;
}

// The shadow list lives in the caller's scope.
static int render_collect_box_shadows(ScratchScope* scope, ViewBlock* view,
                                      bool inset, BoxShadow*** out_shadows) {
    if (out_shadows) *out_shadows = nullptr;
    if (!scope || !view || !view->bound || !view->boundary()->box_shadow || !out_shadows) {
        return 0;
    }
    int count = 0;
    for (BoxShadow* shadow = view->boundary()->box_shadow; shadow; shadow = shadow->next) {
        if (shadow->inset == inset) count++;
    }
    if (count == 0) return 0;
    BoxShadow** shadows = scope->array_zero<BoxShadow*>((size_t)count);
    if (!shadows) {
        log_error("[BOX-SHADOW] failed to allocate shadow list for %d shadow(s)", count);
        return 0;
    }
    int index = 0;
    for (BoxShadow* shadow = view->boundary()->box_shadow; shadow; shadow = shadow->next) {
        if (shadow->inset == inset) shadows[index++] = shadow;
    }
    *out_shadows = shadows;
    return count;
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
            dst[col] = render_pixel_pack_abgr(r, g, b, a);
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
    ScratchScope scope(sa);
    uint32_t* buf = scope.array<uint32_t>(buf_size);
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
// the same isolated source, or their edge falloff diverges. With a scope the
// buffer belongs to it; without one it stays with `sa` (the retained display
// list's arena, released with the list).
static bool build_outer_shadow_image(
    ScratchArena* sa, ScratchMark* scope, ImageSurface* surface,
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
    uint32_t* shadow_buf = scope
        ? (uint32_t*)scratch_scope_alloc(sa, scope, buf_n * sizeof(uint32_t))
        : (uint32_t*)scratch_alloc(sa, buf_n * sizeof(uint32_t));
    if (!shadow_buf) return false;
    memset(shadow_buf, 0, buf_n * sizeof(uint32_t));

    uint32_t sa_b = shadow_color.a;
    uint32_t sr_b = ((uint32_t)shadow_color.r * sa_b + 127) / 255;
    uint32_t sg_b = ((uint32_t)shadow_color.g * sa_b + 127) / 255;
    uint32_t sb_b = ((uint32_t)shadow_color.b * sa_b + 127) / 255;
    uint32_t shadow_px = render_pixel_pack_abgr(sr_b, sg_b, sb_b, sa_b);

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
    ScratchScope scope(sa);
    if (!build_outer_shadow_image(sa, &scope.mark, surface,
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
    render_composite_apply_region(surface, shadow_buf, br_x0, br_y0, br_w, br_h,
                                  RENDER_COMPOSITE_REGION_PREMULTIPLIED_FULL,
                                  CSS_VALUE_NORMAL, 255,
                                  has_exclude ? &exclude_cs : nullptr,
                                  has_clip ? &clip_cs : nullptr);
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
    if (!build_outer_shadow_image(sa, nullptr, surface,
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
void render_box_shadow(RasterRenderContext* rdcon, ViewBlock* view, Rect rect) {
    if (!view->bound || !view->boundary()->box_shadow) return;

    ScratchScope scope(&rdcon->scratch);
    BoxShadow** shadows = nullptr;
    int shadow_count = render_collect_box_shadows(&scope, view, false, &shadows);
    if (shadow_count == 0) return;

    // Scale factor: rect is in physical pixels but shadow props and border radii
    // are in CSS pixels. Multiply by raster_scale at the lowering boundary.
    float sc = rdcon->raster_scale;

    // Get border radius if present (scaled to physical pixels)
    float r_tl = 0, r_tr = 0, r_br = 0, r_bl = 0;
    if (view->bound && view->boundary_mut()->border) {
        BorderProp* border = view->boundary()->border;
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

        Rect shadow_rect = render_geometry_outer_shadow_rect(
            rect, s_offset_x, s_offset_y, s_spread);
        if (shadow_rect.width <= 0.0f || shadow_rect.height <= 0.0f) continue;
        float shadow_x = shadow_rect.x, shadow_y = shadow_rect.y;
        float shadow_w = shadow_rect.width, shadow_h = shadow_rect.height;

        Corner radius = render_path_uniform_corner(r_tl, r_tr, r_br, r_bl);
        Corner shadow_radius = render_geometry_outset_corners(&radius, rect.width, rect.height,
            s_spread, s_spread, s_spread, s_spread);
        float sr_tl = shadow_radius.top_left, sr_tr = shadow_radius.top_right;
        float sr_br = shadow_radius.bottom_right, sr_bl = shadow_radius.bottom_left;


        // Serialize CSS clip-path shape (if active) for masking the shadow
        int clip_type = 0;
        float clip_params[RDT_CLIP_PARAM_COUNT] = {};
        if (rdcon->clip_shape_depth > 0) {
            clip_shape_to_params(rdcon->clip_shapes[rdcon->clip_shape_depth - 1],
                                 &clip_type, clip_params);
        }

        // Element border-box (exclude shape: per CSS spec, outer shadow is not
        // visible inside the element border-box).
        int exclude_type = CLIP_SHAPE_ROUNDED_RECT;
        float exclude_params[RDT_CLIP_PARAM_COUNT] = {};
        exclude_params[0] = rect.x;     exclude_params[1] = rect.y;
        exclude_params[2] = rect.width; exclude_params[3] = rect.height;
        exclude_params[4] = r_tl;       exclude_params[5] = r_tr;
        exclude_params[6] = r_br;       exclude_params[7] = r_bl;

        // hard shadows also exclude the border-box, even for transparent square boxes.
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
    }
}

/**
 * Blur the inverse padding-box cutout in isolation, then clip and composite it.
 * Blurring painted backgrounds smears gradients and cannot model the shadow
 * outside the padding edge; CSS inset shadows extend from that outside region.
 */
void render_box_shadow_inset(RasterRenderContext* rdcon, ViewBlock* view, Rect rect) {
    if (!view->bound || !view->boundary()->box_shadow) return;

    ScratchScope scope(&rdcon->scratch);
    BoxShadow** shadows = nullptr;
    int shadow_count = render_collect_box_shadows(&scope, view, true, &shadows);
    if (shadow_count == 0) return;

    float scale = rdcon->raster_scale;
    BorderProp* border = view->boundary()->border;
    Corner radius = border ? border->radius : Corner{};
    // reduce the border-box radii before deriving the padding-box and cutout radii.
    constrain_corner_radii(&radius, rect.width / scale, rect.height / scale);
    radius = background_corner_inset_box(&radius, CSS_VALUE_PADDING_BOX, border, nullptr, scale);
    Rect padding_rect = render_geometry_adjust_box_rect(rect, CSS_VALUE_PADDING_BOX, scale, border, nullptr);
    if (padding_rect.width <= 0.0f || padding_rect.height <= 0.0f) return;
    constrain_corner_radii(&radius, padding_rect.width, padding_rect.height);
    const RdtMatrix* transform = render_state_current_transform(rdcon);
    Rect visible_rect = padding_rect;
    if (!transform) {
        // keep scrolled/offscreen boxes from allocating masks beyond the visible paint region.
        Bound visible = view_geometry_intersect_bound_rect(rdcon->block.clip, padding_rect);
        visible_rect = {visible.left, visible.top,
            visible.right - visible.left, visible.bottom - visible.top};
        if (visible_rect.width <= 0.0f || visible_rect.height <= 0.0f) return;
    }

    for (int i = shadow_count - 1; i >= 0; i--) {
        BoxShadow* shadow = shadows[i];
        if (shadow->color.a == 0) continue;
        float spread = shadow->spread_radius * scale;
        float sigma = shadow->blur_radius * scale * 0.5f;
        float pad = ceilf(sigma * 3.0f);
        float left = floorf(visible_rect.x - pad);
        float top = floorf(visible_rect.y - pad);
        float width = ceilf(visible_rect.x + visible_rect.width + pad) - left;
        float height = ceilf(visible_rect.y + visible_rect.height + pad) - top;
        size_t bytes = 0;
        if (!render_surface_allocation_size(width, height, &bytes) ||
            !render_memory_allow_allocation(nullptr, bytes)) return;
        int pixel_width = (int)width; // INT_CAST_OK: validated physical raster buffer extent
        int pixel_height = (int)height; // INT_CAST_OK: validated physical raster buffer extent
        bytes = (size_t)pixel_width * (size_t)pixel_height * sizeof(uint32_t);
        // recorded image pixels must survive until the display list is released.
        ScratchArena* image_arena = rdcon->dl ? &rdcon->dl->arena : &rdcon->scratch;
        uint32_t* pixels = (uint32_t*)scratch_alloc(image_arena, bytes);
        if (!pixels) return;
        memset(pixels, 0, bytes);
        ImageSurface image = {};
        image.pixels = pixels; image.width = pixel_width; image.height = pixel_height;
        image.pitch = pixel_width * 4;

        RdtPath* mask = background_rect_path({0.0f, 0.0f, width, height});
        Rect cutout = {padding_rect.x + shadow->offset_x * scale + spread - left,
            padding_rect.y + shadow->offset_y * scale + spread - top,
            padding_rect.width - 2.0f * spread, padding_rect.height - 2.0f * spread};
        if (cutout.width > 0.0f && cutout.height > 0.0f) {
            Corner cutout_radius = radiant_corner_inset(&radius, spread, spread);
            constrain_corner_radii(&cutout_radius, cutout.width, cutout.height);
            render_path_append_rounded_rect(mask, cutout, &cutout_radius, false);
        }
        RdtVector vector = {};
        Color mask_color = {};
        mask_color.r = mask_color.g = mask_color.b = 255;
        mask_color.a = shadow->color.a;
        int saved_depth = rdt_clip_save_depth();
        rdt_vector_init(&vector, pixels, pixel_width, pixel_height, pixel_width);
        rdt_fill_path(&vector, mask, mask_color, RDT_FILL_EVEN_ODD, nullptr);
        rdt_vector_destroy(&vector);
        rdt_clip_restore_depth(saved_depth);
        rdt_path_free(mask);

        if (sigma > 0.0f) {
            ScratchScope blur_scope(&rdcon->scratch);
            uint32_t* temporary = blur_scope.array<uint32_t>(bytes / sizeof(uint32_t));
            if (!temporary) return;
            ImageSurface intermediate = image;
            intermediate.pixels = temporary;
            if (!render_gaussian_blur_axis(&rdcon->scratch, &image, &intermediate,
                    sigma, 0, 0, pixel_width, 1) ||
                !render_gaussian_blur_axis(&rdcon->scratch, &intermediate, &image,
                    sigma, 1, 0, pixel_height, 1)) return;
        }
        tint_premultiplied_surface_region(&image, 0, 0, pixel_width, pixel_height, shadow->color);
        RdtPath* clip = render_path_create_rounded_rect(padding_rect, &radius);
        rc_push_clip(rdcon, clip, transform);
        rc_draw_image(rdcon, pixels, pixel_width, pixel_height, pixel_width,
            left, top, width, height, 255, transform);
        rc_pop_clip(rdcon);
        rdt_path_free(clip);
    }
}

/**
 * Compute background image dimensions based on background-size property.
 * Returns the computed width and height of the background image in physical pixels.
 */
static bool background_size_unspecified(BackgroundGeometry* bg);

static void compute_bg_image_size(BackgroundGeometry* bg, float img_w, float img_h,
                                   float box_w, float box_h, float* out_w, float* out_h,
                                   float raster_scale, bool intrinsic_ratio) {
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
            w = background_size_dimension(bg, true, box_w, raster_scale);
        }
        if (!h_auto) {
            h = background_size_dimension(bg, false, box_h, raster_scale);
        }

        if (w_auto && !h_auto) {
            // auto <height>: width scales proportionally
            *out_h = h;
            *out_w = intrinsic_ratio ? h * aspect : box_w;
        } else if (!w_auto && h_auto) {
            // <width> auto: height scales proportionally
            *out_w = w;
            *out_h = intrinsic_ratio ? w / aspect : box_h;
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
static void compute_bg_image_position(BackgroundGeometry* bg, float img_w, float img_h,
                                       float box_w, float box_h, float* out_x, float* out_y, float raster_scale) {
    if (!bg->bg_position_set) {
        // Default: 0% 0% (top-left)
        *out_x = 0.0f;
        *out_y = 0.0f;
        return;
    }

    *out_x = background_position_offset(bg, true, box_w - img_w, raster_scale);
    *out_y = background_position_offset(bg, false, box_h - img_h, raster_scale);
}

static bool background_size_unspecified(BackgroundGeometry* bg) {
    return !bg->bg_size_set && !bg->bg_size_type &&
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

static bool compute_background_tile_plan(BackgroundGeometry* bg, Rect position_rect, Rect coverage_rect,
                                         float intrinsic_w, float intrinsic_h,
                                         BackgroundTilePlan* plan, float raster_scale, bool intrinsic_ratio = true) {
    if (!bg || !plan ||
        position_rect.width <= 0.0f || position_rect.height <= 0.0f ||
        coverage_rect.width <= 0.0f || coverage_rect.height <= 0.0f) {
        return false;
    }

    compute_bg_image_size(bg, intrinsic_w, intrinsic_h,
                          position_rect.width, position_rect.height,
                          &plan->tile_w, &plan->tile_h, raster_scale, intrinsic_ratio);
    if (plan->tile_w <= 0.0f || plan->tile_h <= 0.0f) return false;

    float pos_x = 0.0f;
    float pos_y = 0.0f;
    compute_bg_image_position(bg, plan->tile_w, plan->tile_h,
                              position_rect.width, position_rect.height, &pos_x, &pos_y, raster_scale);

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
    // space respects the authored position on an axis where fewer than two tiles fit.
    if (plan->repeat_x == CSS_VALUE_SPACE && plan->end_col > 0) {
        tile_rect->x = rect.x + col * (plan->tile_w + plan->space_gap_x);
    } else {
        tile_rect->x = plan->origin_x + col * plan->tile_w;
    }
    if (plan->repeat_y == CSS_VALUE_SPACE && plan->end_row > 0) {
        tile_rect->y = rect.y + row * (plan->tile_h + plan->space_gap_y);
    } else {
        tile_rect->y = plan->origin_y + row * plan->tile_h;
    }
    tile_rect->width = plan->tile_w;
    tile_rect->height = plan->tile_h;
}

static void background_merge_solid_axis(float begin, float end, float gap, CssEnum* repeat,
        float* origin, float* length, int* first, int* last) {
    if (*first == *last || (*repeat == CSS_VALUE_SPACE && gap != 0.0f)) return;
    *origin = begin; *length = end - begin;
    *first = *last = 0; *repeat = CSS_VALUE_NO_REPEAT;
}

static void background_merge_solid_tiles(BackgroundTilePlan* plan, Rect basis) {
    Rect first, last;
    background_tile_rect(plan, basis, plan->start_row, plan->start_col, &first);
    background_tile_rect(plan, basis, plan->end_row, plan->end_col, &last);
    // touching uniform tiles form one continuous surface; separate draws expose antialiased seams.
    background_merge_solid_axis(first.x, last.x + last.width, plan->space_gap_x, &plan->repeat_x,
        &plan->origin_x, &plan->tile_w, &plan->start_col, &plan->end_col);
    background_merge_solid_axis(first.y, last.y + last.height, plan->space_gap_y, &plan->repeat_y,
        &plan->origin_y, &plan->tile_h, &plan->start_row, &plan->end_row);
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

struct BackgroundPaintContext {
    PaintList* paint;
    UiContext* ui;
    ViewBlock* view;
    RasterRenderContext* raster;
    float pixel_ratio;
    const RdtMatrix* transform;
    ImageSurface* image;
    ListStyleImage gradient;
    Rect clip;
    Color solid_color;
    bool solid_tiles;
    bool rectangular_tiles;
    RdtPath* text_shape;
};

static bool background_record_solid_rect(PaintList* paint, Rect rect, Color color, const RdtMatrix* transform) {
    int before = paint_list_count(paint);
    if (transform) paint_push_transform(paint, transform);
    // keep the rectangle primitive's exact source-over and canonical vector export; image consumers retain their image/gradient operation.
    paint_fill_rect(paint, rect.x, rect.y, rect.width, rect.height, color);
    if (transform) paint_pop_transform(paint);
    return paint_list_count(paint) == before + (transform ? 3 : 1);
}

static void paint_background_tile(const Rect* tile, void* data) {
    BackgroundPaintContext* context = (BackgroundPaintContext*)data;
    if (context->solid_tiles) {
        Rect visible = *tile;
        if (context->rectangular_tiles) {
            Bound bounds = view_geometry_intersect_bound_rect(view_geometry_rect_to_bound(context->clip), *tile);
            visible = {bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top};
        }
        // rectangular clipping is exact geometry, avoiding a second coverage operation at the same edge.
        background_record_solid_rect(context->paint, visible, context->solid_color, context->transform);
        return;
    }
    if (context->gradient.gradient_type != GRADIENT_NONE) {
        if (context->text_shape) render_paint_gradient_shape(context->paint, &context->gradient,
            *tile, context->pixel_ratio, context->transform, context->text_shape);
        else render_paint_gradient_image(context->paint, &context->gradient, *tile,
            context->pixel_ratio, context->transform);
        return;
    }
    if (context->text_shape) paint_push_clip(context->paint, context->text_shape, context->transform);
    if (!render_media_paint_svg_image(context->paint, context->image, *tile, &context->clip,
            ui_context_raster_scale(context->ui), context->ui->font_ctx, context->ui)) {
        paint_draw_image_resource(context->paint, context->image, tile->x, tile->y,
            tile->width, tile->height, 255, context->transform);
    }
    if (context->text_shape) paint_pop_clip(context->paint);
}

static RdtPath* background_layer_clip(ViewBlock* view, const BackgroundGeometry* layer, Rect clip) {
    const BorderProp* border = view->boundary()->border;
    RdtPath* path = nullptr;
    if (border && corner_has_radius(&border->radius)) {
        CssEnum box = layer->bg_clip ? layer->bg_clip : CSS_VALUE_BORDER_BOX;
        Corner radius = background_corner_inset_box(&border->radius, box, border,
            &view->boundary()->padding, 1.0f);
        constrain_corner_radii(&radius, clip.width, clip.height);
        path = render_path_create_rounded_rect(clip, &radius);
    } else path = background_rect_path(clip);
    return path;
}

static bool background_push_layer_clip(BackgroundPaintContext* context,
                                      const BackgroundGeometry* layer, Rect clip) {
    RdtPath* path = background_layer_clip(context->view, layer, clip);
    if (!path) return false;
    if (context->raster) rc_push_clip(context->raster, path, context->transform);
    else paint_push_clip(context->paint, path, context->transform);
    rdt_path_free(path);
    return true;
}

static void background_pop_layer_clip(BackgroundPaintContext* context) {
    if (context->raster) rc_pop_clip(context->raster);
    else paint_pop_clip(context->paint);
}

static IRect background_blend_bounds(BackgroundPaintContext* context, Rect clip) {
    float left = clip.x, top = clip.y, right = clip.x + clip.width, bottom = clip.y + clip.height;
    if (context->transform && !rdt_matrix_project_rect_bounds(context->transform,
            left, top, right, bottom, &left, &top, &right, &bottom)) return {};
    return render_geometry_clip_to_pixel_bounds({floorf(left), floorf(top), ceilf(right), ceilf(bottom)},
        context->raster ? context->ui->surface : nullptr);
}

static bool background_has_blend(const BackgroundProp* background) {
    int count = background_layer_count(background);
    for (int index = 0; index < count; index++) {
        const BackgroundLayer* layer = background_layer_at(background, index);
        if (layer->blend_mode && layer->blend_mode != CSS_VALUE_NORMAL) return true;
    }
    return false;
}

static bool record_background(PaintList* paint, UiContext* ui, ViewBlock* view, Rect rect,
                              float pixel_ratio, const RdtMatrix* transform, RasterRenderContext* raster,
                              bool text_only, RdtPath* text_shape, const BackgroundGlyphMask* glyph_mask) {
    if (!paint || !ui || !view || !view->bound || !view->boundary()->background) return false;
    BackgroundProp* background = view->boundary()->background;
    BackgroundPaintContext context = {paint, ui, view, raster, pixel_ratio, transform, nullptr, {}, {}};
    context.text_shape = text_shape;
    int count = background_layer_count(background);
    BackgroundLayer* bottom = background_layer_at(background, count - 1);
    bool recorded = false;
    bool isolated = background_has_blend(background);
    if (isolated) {
        IRect bounds = background_blend_bounds(&context, rect);
        PaintEffectGroup group = {};
        group.bounds = {(float)bounds.x, (float)bounds.y, (float)(bounds.x + bounds.w), (float)(bounds.y + bounds.h)};
        group.opacity = 1.0f; group.isolation = true;
        // background blending sees only this box's layers and color, independently of page content.
        paint_begin_effect_group(paint, &group);
    }
    Rect position = {}, clip = {};
    if (background->color.a && (bottom->bg_clip == CSS_VALUE_TEXT) == text_only) {
        PaintList color_layer;
        PaintList* color_target = glyph_mask ? &color_layer : paint;
        context.paint = color_target;
        render_background_layer_rects(ui, view, bottom, rect, 1.0f, &position, &clip);
        const BorderProp* border = view->boundary()->border;
        if (text_shape) {
            if (background_push_layer_clip(&context, bottom, clip)) {
                recorded = render_paint_solid_shape(color_target, text_shape, background->color, transform);
                background_pop_layer_clip(&context);
            }
        } else if (!border || !corner_has_radius(&border->radius)) {
            recorded = background_record_solid_rect(color_target, clip, background->color, transform);
        } else {
            RdtPath* path = background_layer_clip(view, bottom, clip);
            if (path) {
                int index = paint_list_count(color_target);
                paint_fill_path(color_target, path, background->color, RDT_FILL_WINDING, transform);
                if (!paint_list_take_path_payload(color_target, index, PAINT_FILL_PATH)) rdt_path_free(path);
                recorded = true;
            }
        }
        if (glyph_mask && recorded) recorded = background_record_masked_layer(paint, color_layer, *glyph_mask, pixel_ratio);
    }
    // one authored-order walk feeds native raster and both vector backends (CI-18).
    for (int index = count - 1; index >= 0; index--) {
        BackgroundLayer* layer = background_layer_at(background, index);
        if ((layer->bg_clip == CSS_VALUE_TEXT) != text_only ||
            (!layer->image && layer->gradient_type == GRADIENT_NONE)) continue;
        PaintList image_layer;
        context.paint = glyph_mask ? &image_layer : paint;
        render_background_layer_rects(ui, view, layer, rect, 1.0f, &position, &clip);
        context.image = layer->image ? load_document_image(ui->document, ui, layer->image) : nullptr;
        if (layer->image && !context.image) continue;
        context.gradient = {};
        context.gradient.gradient_type = layer->gradient_type;
        context.gradient.linear_gradient = lam::shared(layer->linear_gradient.get());
        context.gradient.radial_gradient = lam::shared(layer->radial_gradient.get());
        context.gradient.conic_gradient = lam::shared(layer->conic_gradient.get());
        context.clip = clip;
        float density = context.image ? layout_css_image_density(context.image, layer->image_resolution) : 1.0f;
        BackgroundTilePlan plan = {};
        if (!compute_background_tile_plan(layer, position, clip,
                context.image ? (float)context.image->width / density : position.width,
                context.image ? (float)context.image->height / density : position.height, &plan, 1.0f,
                context.image != nullptr)) continue;
        GradientImageParts parts = gradient_image_parts(&context.gradient);
        context.solid_tiles = !text_shape && gradient_stops_uniform_color(parts.stops, parts.count, &context.solid_color);
        const BorderProp* border = view->boundary()->border;
        bool rounded = border && corner_has_radius(&border->radius);
        context.rectangular_tiles = context.solid_tiles && !rounded;
        if (context.solid_tiles) background_merge_solid_tiles(&plan, position);
        Rect single_tile = {};
        bool one_tile = plan.start_col == plan.end_col && plan.start_row == plan.end_row;
        if (one_tile) background_tile_rect(&plan, position, plan.start_row, plan.start_col, &single_tile);
        bool needs_clip = text_shape || (!context.rectangular_tiles && (!one_tile || rounded ||
            !view_geometry_rect_contains_rect(clip, single_tile, 0.0f)));
        // a coincident rectangular clip compounds PDF coverage already supplied by the tile.
        if (needs_clip && !background_push_layer_clip(&context, layer, clip)) continue;
        bool blend = layer->blend_mode && layer->blend_mode != CSS_VALUE_NORMAL;
        IRect blend_bounds = blend ? background_blend_bounds(&context, clip) : IRect{};
        if (blend && !render_geometry_pixel_bounds_empty(blend_bounds)) {
            PaintEffectGroup group = {};
            group.bounds = {(float)blend_bounds.x, (float)blend_bounds.y,
                (float)(blend_bounds.x + blend_bounds.w), (float)(blend_bounds.y + blend_bounds.h)};
            group.opacity = 1.0f; group.blend_mode = layer->blend_mode;
            paint_begin_effect_group(paint, &group);
        }
        if (raster && context.image) {
            float scale = raster->raster_scale;
            Rect physical_position = {position.x * scale, position.y * scale,
                position.width * scale, position.height * scale};
            Rect physical_clip = {clip.x * scale, clip.y * scale, clip.width * scale, clip.height * scale};
            render_background_image(raster, view, layer, physical_position, physical_clip);
        } else background_for_each_tile(&plan, position, clip, paint_background_tile, &context);
        if (needs_clip) background_pop_layer_clip(&context);
        if (glyph_mask) background_record_masked_layer(paint, image_layer, *glyph_mask, pixel_ratio);
        if (blend && !render_geometry_pixel_bounds_empty(blend_bounds)) paint_end_effect_group(paint);
        recorded = true;
    }
    if (isolated) paint_end_effect_group(paint);
    return recorded;
}

bool render_paint_background_requires_composite(const BackgroundProp* background, RenderExportTargetKind target) {
    if (!background) return false;
    bool composite = background_has_blend(background);
    if (target == RENDER_EXPORT_TARGET_PDF && background->layer_count > 0) {
        for (int index = 0; index < background->layer_count; index++) {
            const BackgroundLayer& layer = background->layers[index];
            composite |= layer.image || gradient_image_needs_sampling(layer.linear_gradient.get()) ||
                gradient_image_needs_sampling(layer.radial_gradient.get()) ||
                gradient_image_needs_sampling(layer.conic_gradient.get());
        }
    }
    return composite;
}

bool render_paint_background(PaintList* paint, UiContext* ui, ViewBlock* view, Rect rect,
                             RenderExportTargetKind target) {
    if (!paint || !ui || !view || !view->bound || !view->boundary()->background) return false;
    const BackgroundProp* background = view->boundary()->background;
    float density = ui_context_raster_scale(ui);
    bool composite = render_paint_background_requires_composite(background, target);
    if (!composite) return record_background(paint, ui, view, rect, density, nullptr, nullptr);
    PaintList layers;
    if (!record_background(&layers, ui, view, rect, density, nullptr, nullptr)) return false;
    RenderEffectRasterImage image = {};
    Bound bounds = {rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
    // PDF gradient fallbacks must composite the layers before the viewer reconstructs edge coverage.
    if (!render_effect_rasterize_paint_list(&layers, nullptr, &bounds, rect.width, rect.height,
            false, density, &image, "[BACKGROUND_COMPOSITE]") || !image.surface) return false;
    int index = paint_list_count(paint);
    paint_draw_image_resource(paint, image.surface, image.x, image.y, image.width, image.height,
        255, nullptr, image_surface_destroy);
    return paint_list_count(paint) == index + 1;
}

bool render_paint_boundary_layers(PaintList* paint, UiContext* ui, ViewBlock* view, Rect rect,
        RenderExportTargetKind target, bool outer_shadows, const Rect* background_area) {
    if (outer_shadows) render_paint_boundary_emit_outer_shadows(paint, view, rect.x, rect.y);
    // slice only the image positioning area; border edges retain their single coverage operation.
    bool sliced = background_area && !render_paint_background_is_simple(view->boundary()->background);
    if (sliced) {
        RdtPath* clip = background_rect_path(rect);
        if (!clip) return false;
        paint_push_clip(paint, clip, nullptr); rdt_path_free(clip);
    }
    render_paint_background(paint, ui, view, sliced ? *background_area : rect, target);
    if (sliced) paint_pop_clip(paint);
    return render_paint_border_image(paint, ui, view->boundary()->border, rect);
}

bool render_paint_boundary_box(PaintList* paint, UiContext* ui, ViewBlock* view, Rect rect,
        RenderExportTargetKind target, const Rect* background_area) {
    if (!paint || !view || !view->bound) return false;
    BoundaryProp boundary = *view->boundary();
    BorderProp border = {};
    if (boundary.border) { border = *boundary.border; boundary.border = lam::own(&border); }
    if (render_paint_boundary_emit_box(paint, &boundary, rect)) return true;
    ViewBlock box = *view;
    box.bound = lam::view_prop(&boundary);
    box.width = rect.width; box.height = rect.height;
    if (boundary.border) {
        resolve_border_radius_percentages(&border.radius, rect.width, rect.height);
        constrain_corner_radii(&border.radius, rect.width, rect.height);
    }
    bool image = render_paint_boundary_layers(paint, ui, &box, rect, target, true, background_area);
    if (!image && boundary.border) {
        bool rounded = radiant_corner_has_radius(&border.radius);
        if (!rounded || !render_paint_boundary_emit_solid_border(paint, &border, rect)) {
            RdtPath* clip = rounded ? render_path_create_rounded_rect(rect, &border.radius) : nullptr;
            if (clip) { paint_push_clip(paint, clip, nullptr); rdt_path_free(clip); }
            for (size_t side = 0; side < 4; side++)
                if (!render_paint_boundary_emit_border_side(paint, &border, rect, side)) return false;
            if (clip) paint_pop_clip(paint);
        }
    }
    const OutlineProp* outline = boundary.outline;
    if (outline && outline->width > 0.0f && outline->color.a) {
        BorderProp ring = {};
        for (size_t side = 0; side < 4; side++) {
            ring.width.values[side] = outline->width;
            ring.styles[side] = outline->style; ring.colors[side] = outline->color;
        }
        float outset = outline->offset + outline->width;
        Rect outer = {rect.x - outset, rect.y - outset, rect.width + 2.0f * outset, rect.height + 2.0f * outset};
        for (size_t side = 0; side < 4; side++)
            if (!render_paint_boundary_emit_border_side(paint, &ring, outer, side)) return false;
    }
    return true;
}

/**
 * Render a single tile of a background image using the raster blit path.
 */
static void blit_bg_tile(RasterRenderContext* rdcon, ImageSurface* img, ImageSurface* dst, Rect* tile_rect, Bound* clip,
                         ScaleMode mode = SCALE_MODE_LINEAR,
                         ClipShape** clip_shapes = nullptr, int clip_depth = 0) {
    render_painter_blit_surface_scaled(rdcon, img, NULL, dst, tile_rect, clip, mode, clip_shapes, clip_depth);
}

static int background_image_clip_shapes(RasterRenderContext* rdcon, ViewBlock* view,
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

static RdtPath* background_image_clip_path(RasterRenderContext* rdcon, ViewBlock* view) {
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
static void render_bg_tile_tvg(RasterRenderContext* rdcon, ViewBlock* view, ImageSurface* img, Rect* tile_rect) {
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
    RasterRenderContext* rdcon;
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
static void render_background_image(RasterRenderContext* rdcon, ViewBlock* view, BackgroundLayer* bg, Rect rect, Rect paint_rect) {
    const char* image_url = bg->image;

    // Load image via the image cache
    ImageSurface* img = load_document_image(rdcon->ui_context->document, rdcon->ui_context, image_url);
    if (!img) {
        // Background images are optional paint layers; network-managed pages
        // may skip late/blocking fetches and keep rendering the base layer.
        log_warn("[BG-IMAGE] Image unavailable, skipping paint layer '%s'", image_url);
        return;
    }

    float density = layout_css_image_density(img, bg->image_resolution);
    float img_w = (float)img->width / density;
    float img_h = (float)img->height / density;
    if (img_w <= 0 || img_h <= 0) {
        log_error("[BG-IMAGE] Image has zero dimensions");
        return;
    }

    float s = rdcon->raster_scale;

    BackgroundTilePlan plan = {};
    if (!compute_background_tile_plan(bg, rect, paint_rect, img_w * s, img_h * s, &plan, s)) {
        return;
    }


    bool is_repeating = (plan.repeat_x != CSS_VALUE_NO_REPEAT && plan.repeat_x != CSS_VALUE_SPACE) ||
                        (plan.repeat_y != CSS_VALUE_NO_REPEAT && plan.repeat_y != CSS_VALUE_SPACE);
    ScaleMode tile_scale_mode = render_image_scale_mode(view, is_repeating);

    // Render tiles
    bool is_svg = (img->format == IMAGE_FORMAT_SVG);
    if (!is_svg) {
        if (rdcon->has_transform || rdcon->css3d_context || tile_scale_mode == SCALE_MODE_NEAREST ||
            tile_scale_mode == SCALE_MODE_PIXELATED) {
            image_surface_ensure_decoded(img, img->width, img->height);
        } else {
            // Smooth sampling may decode at the displayed tile size.
            image_surface_ensure_decoded(img,
                (int)plan.tile_w,  // INT_CAST_OK: decode API accepts pixel dimensions.
                (int)plan.tile_h); // INT_CAST_OK: decode API accepts pixel dimensions.
        }
    }
    if (is_svg && tile_scale_mode == SCALE_MODE_LINEAR_WRAP)
        tile_scale_mode = SCALE_MODE_LINEAR;
    if (!is_svg && rdcon->dl && rdcon->has_transform &&
        (plan.repeat_x == CSS_VALUE_REPEAT || plan.repeat_x == CSS_VALUE_ROUND) &&
        (plan.repeat_y == CSS_VALUE_REPEAT || plan.repeat_y == CSS_VALUE_ROUND)) {
        ImageSurfaceReadScope read_scope;
        Rect tile = {plan.origin_x, plan.origin_y, plan.tile_w, plan.tile_h};
        uint32_t* projected = nullptr;
        Rect bounds;
        bool ok = render_image_project_pixels((const uint32_t*)img->pixels,
            img->decoded_width > 0 ? img->decoded_width : img->width,
            img->decoded_height > 0 ? img->decoded_height : img->height, img->pitch / 4,
            rect, &rdcon->transform,
            render_painter_projection_viewport(rdcon),
            tile_scale_mode, img->alpha_mode == IMAGE_ALPHA_STRAIGHT, &projected, &bounds, &tile);
        lam::Temp<uint32_t> owned_projection(projected);
        if (!ok) {
            rdcon->paint_failed = true;
            log_error("[BG_PATTERN_PROJECT] cannot project repeated background");
        } else if (projected) {
            int width = 0, height = 0;
            uint32_t* pixels = background_gradient_pixel_buffer(rdcon, bounds, &width, &height);
            if (!pixels) rdcon->paint_failed = true;
            else {
                // Unowned PaintIR pixels are premultiplied and belong to the recording.
                memcpy(pixels, projected, (size_t)width * height * sizeof(uint32_t));
                rc_draw_image(rdcon, pixels, width, height, width, bounds.x, bounds.y,
                    bounds.width, bounds.height, 255, nullptr, nullptr, SCALE_MODE_LINEAR);
            }
        }
        return;
    }
    ClipShape rounded_clip_shape = {};
    ClipShape* clip_shape_stack[RDT_MAX_CLIP_SHAPES];
    int clip_shape_depth = background_image_clip_shapes(rdcon, view, &rounded_clip_shape, clip_shape_stack);

    BackgroundImageTileContext tile_ctx = {
        rdcon, view, img, is_svg, tile_scale_mode, clip_shape_stack, clip_shape_depth
    };
    background_for_each_tile(&plan, rect, paint_rect, render_background_image_tile_cb, &tile_ctx);

}


static bool background_has_text_clip(const DomElement* parent) {
    if (!parent || !parent->bound || !parent->boundary()->background) return false;
    const BackgroundProp* background = parent->boundary()->background;
    int count = background_layer_count(background);
    for (int i = 0; i < count; i++) {
        const BackgroundLayer* layer = background_layer_at(background, i);
        if (layer->bg_clip == CSS_VALUE_TEXT) return true;
    }
    return false;
}

bool render_paint_text_background(PaintList* paint, UiContext* ui, DomElement* parent,
        const PaintGlyphRun* run, Rect border) {
    if (!paint || !run || !background_has_text_clip(parent)) return false;
    RdtPath* mask = render_path_create_paint_glyph_run(run, ui ? ui->font_ctx.get() : nullptr);
    if (!mask) return false;
    bool painted = record_background(paint, ui, static_cast<ViewBlock*>(parent), border,
        ui_context_raster_scale(ui), nullptr, nullptr, true, mask);
    rdt_path_free(mask);
    return painted;
}

static bool background_record_masked_layer(PaintList* destination, const PaintList& layer,
        const BackgroundGlyphMask& mask, float density) {
    OffscreenRenderArenas arenas;
    if (!arenas.init("render.text.background", "render.text.background.list", "render.text.background.scratch")) return false;
    DisplayList list = {}; dl_init(&list, arenas.list_arena);
    paint_ir_lower_raster(&layer, &list);
    const GlyphBitmap* bitmap = mask.bitmap;
    Bound bounds = {mask.x, mask.y, mask.x + (float)bitmap->width, mask.y + (float)bitmap->height};
    ImageSurface* surface = render_display_list_snapshot(&list,
        mem_context_process(MEM_ROLE_RENDER), bounds, density);
    dl_destroy(&list); arenas.destroy();
    if (!surface) return false;
    for (int row = 0; row < bitmap->height; row++) {
        uint32_t* pixels = (uint32_t*)((uint8_t*)surface->pixels + row * surface->pitch);
        for (int column = 0; column < bitmap->width; column++) {
            Color color = {.c = pixels[column]};
            color.a = render_pixel_premultiply_channel(color.a,
                (uint8_t)glyph_sample_coverage_pixel(bitmap, row, column));
            pixels[column] = render_pixel_premultiply_abgr(color.c);
        }
    }
    surface->alpha_mode = IMAGE_ALPHA_PREMULTIPLIED;
    size_t before = destination->size();
    if (render_painter_can_blit_surface(mask.context)) {
        if (!mask.leases.append(surface)) { image_surface_destroy(surface); return false; }
        image_surface_snapshot_publish(surface);
        // native glyph pixels share CPU source-over and captured clips with foreground glyphs.
        paint_blit_surface_scaled(destination, surface, mask.x, mask.y,
            (float)bitmap->width, (float)bitmap->height, SCALE_MODE_NEAREST,
            &mask.context->block.clip, mask.context->clip_shapes, mask.context->clip_shape_depth,
            255, surface->generation);
    } else {
        paint_draw_image_resource(destination, surface, mask.x, mask.y,
            (float)bitmap->width, (float)bitmap->height, 255, nullptr, image_surface_destroy);
    }
    return destination->size() == before + 1;
}

bool render_paint_text_background_glyph(RasterRenderContext* context, DomElement* parent,
        Rect physical_border, const GlyphBitmap* bitmap, float x, float y) {
    if (!context || !background_has_text_clip(parent) || !bitmap || !bitmap->buffer ||
        bitmap->width <= 0 || bitmap->height <= 0 || context->raster_scale <= 0.0f) return false;
    PaintRecordTarget target = rc_record_target(context);
    if (!paint_record_ready(&target)) return false;
    float scale = context->raster_scale;
    Rect border = {physical_border.x / scale, physical_border.y / scale,
        physical_border.width / scale, physical_border.height / scale};
    RdtMatrix density = rdt_matrix_scale(scale, scale);
    PaintList paint;
    // each background layer gets text coverage before the existing compositor combines the layers.
    BackgroundGlyphMask mask = {bitmap, x, y, context};
    if (!record_background(&paint, context->ui_context, static_cast<ViewBlock*>(parent),
            border, scale, &density, nullptr, true, nullptr, &mask)) return false;
    // retain masked layers directly; a composite snapshot would quantize alpha before the foreground paint.
    paint_record_lower_pending(&target);
    paint_ir_lower_raster_transformed(&paint, target.display_list,
        context->has_transform ? &context->transform : nullptr);
    return true;
}
