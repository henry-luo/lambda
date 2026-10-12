#include "render.hpp"
#include "../lib/mem_grow.h"
#include <string.h>


RdtMatrix rdt_matrix_scale(float sx, float sy) {
    RdtMatrix m = { sx, 0, 0,  0, sy, 0,  0, 0, 1 };
    return m;
}
#define RENDER_PATH_KAPPA 0.5522847498f

bool render_path_border_side_points(Rect rect, size_t side, float width, float before, float after, float points[8]) {
    float right = rect.x + rect.width, bottom = rect.y + rect.height;
    // adjacent sides share the diagonal from each outer corner to its inner corner.
    const float corners[4][8] = {
        {rect.x, rect.y, right, rect.y, right - after, rect.y + width, rect.x + before, rect.y + width},
        {right - width, rect.y + before, right, rect.y, right, bottom, right - width, bottom - after},
        {rect.x + before, bottom - width, right - after, bottom - width, right, bottom, rect.x, bottom},
        {rect.x, rect.y, rect.x + width, rect.y + before, rect.x + width, bottom - after, rect.x, bottom}
    };
    if (side >= 4 || !points) return false;
    memcpy(points, corners[side], sizeof(corners[side])); return true;
}

RdtPath* render_path_create_border_side(Rect rect, size_t side, float width, float before, float after) {
    float points[8];
    if (!render_path_border_side_points(rect, side, width, before, after, points)) return nullptr;
    RdtPath* path = rdt_path_new();
    if (!path) return nullptr;
    rdt_path_move_to(path, points[0], points[1]);
    for (size_t i = 2; i < 8; i += 2) rdt_path_line_to(path, points[i], points[i + 1]);
    rdt_path_close(path); return path;
}

typedef struct {
    RdtPath* destination;
    const RdtMatrix* transform;
    float x, y, start_x, start_y;
} RenderPathTransform;

void render_path_append_quadratic(RdtPath* path, float sx, float sy,
    float qx, float qy, float x, float y) {
    rdt_path_cubic_to(path, sx + 2.0f / 3.0f * (qx - sx), sy + 2.0f / 3.0f * (qy - sy),
        x + 2.0f / 3.0f * (qx - x), y + 2.0f / 3.0f * (qy - y), x, y);
}

static bool render_path_transform_visit(void* context, RdtPathCommand command,
                                         const float* args, int count) {
    RenderPathTransform* writer = (RenderPathTransform*)context;
    if (command == RDT_PATH_CLOSE) {
        rdt_path_close(writer->destination); writer->x = writer->start_x; writer->y = writer->start_y;
        return true;
    }
    if (command == RDT_PATH_RECT || command == RDT_PATH_CIRCLE) {
        Rect rect;
        float rx, ry;
        RdtMatrix mapping = *writer->transform;
        const RdtMatrix* saved_transform = writer->transform;
        if (command == RDT_PATH_RECT) {
            rect = {args[0], args[1], args[2], args[3]}; rx = args[4]; ry = args[5];
        } else {
            // SVG's canonical ellipse starts at the rightmost point, proceeding clockwise.
            rect = {-1.0f, -1.0f, 2.0f, 2.0f}; rx = ry = 1.0f;
            RdtMatrix ellipse = {0.0f, -args[2], args[0], args[3], 0.0f, args[1], 0.0f, 0.0f, 1.0f};
            mapping = rdt_matrix_multiply(saved_transform, &ellipse);
        }
        Corner radius = {};
        for (size_t i = 0; i < 4; i++) { radius.horizontal[i] = rx; radius.vertical[i] = ry; }
        RdtPath* expanded = rdt_path_new();
        if (!expanded) return false;
        // compact primitives need explicit curves before a general affine transform.
        render_path_append_rounded_rect(expanded, rect, &radius, true);
        writer->transform = &mapping;
        bool result = rdt_path_visit(expanded, render_path_transform_visit, context);
        writer->transform = saved_transform;
        rdt_path_free(expanded);
        return result;
    }
    if (command != RDT_PATH_MOVE && command != RDT_PATH_LINE &&
        command != RDT_PATH_QUAD && command != RDT_PATH_CUBIC) return false;
    float values[6];
    for (int i = 0; i < count; i += 2)
        rdt_matrix_transform_point(writer->transform, args[i], args[i + 1], &values[i], &values[i + 1]);
    if (command == RDT_PATH_MOVE) {
        rdt_path_move_to(writer->destination, values[0], values[1]);
        writer->start_x = values[0]; writer->start_y = values[1];
    }
    else if (command == RDT_PATH_LINE) rdt_path_line_to(writer->destination, values[0], values[1]);
    else if (command == RDT_PATH_QUAD) render_path_append_quadratic(writer->destination,
        writer->x, writer->y, values[0], values[1], values[2], values[3]);
    else rdt_path_cubic_to(writer->destination, values[0], values[1], values[2], values[3], values[4], values[5]);
    writer->x = values[count - 2]; writer->y = values[count - 1];
    return true;
}

bool render_path_append_transformed(RdtPath* destination, const RdtPath* source,
                                     const RdtMatrix* transform) {
    if (!destination || !source || !transform || destination == source) return false;
    RenderPathTransform writer = {destination, transform, 0, 0, 0, 0};
    return rdt_path_visit(source, render_path_transform_visit, &writer);
}

RdtPath* render_path_create_rounded_rect(Rect rect, const Corner* radius) {
    RdtPath* path = rdt_path_new();
    render_path_append_rounded_rect(path, rect, radius, true);
    return path;
}

void render_path_append_rounded_rect(RdtPath* path, Rect rect,
                                     const Corner* radius, bool clockwise) {
    if (!path) return;
    if (!radius) {
        if (clockwise) {
            rdt_path_add_rect(path, rect.x, rect.y, rect.width, rect.height, 0, 0);
        } else {
            rdt_path_move_to(path, rect.x, rect.y);
            rdt_path_line_to(path, rect.x, rect.y + rect.height);
            rdt_path_line_to(path, rect.x + rect.width, rect.y + rect.height);
            rdt_path_line_to(path, rect.x + rect.width, rect.y);
            rdt_path_close(path);
        }
        return;
    }

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

    if (clockwise) {
        rdt_path_move_to(path, x + rx_tl, y);
        rdt_path_line_to(path, x + w - rx_tr, y);
        if (rx_tr > 0.0f || ry_tr > 0.0f) {
            rdt_path_cubic_to(path,
                x + w - rx_tr + rx_tr * RENDER_PATH_KAPPA, y,
                x + w, y + ry_tr - ry_tr * RENDER_PATH_KAPPA,
                x + w, y + ry_tr);
        }

        rdt_path_line_to(path, x + w, y + h - ry_br);
        if (rx_br > 0.0f || ry_br > 0.0f) {
            rdt_path_cubic_to(path,
                x + w, y + h - ry_br + ry_br * RENDER_PATH_KAPPA,
                x + w - rx_br + rx_br * RENDER_PATH_KAPPA, y + h,
                x + w - rx_br, y + h);
        }

        rdt_path_line_to(path, x + rx_bl, y + h);
        if (rx_bl > 0.0f || ry_bl > 0.0f) {
            rdt_path_cubic_to(path,
                x + rx_bl - rx_bl * RENDER_PATH_KAPPA, y + h,
                x, y + h - ry_bl + ry_bl * RENDER_PATH_KAPPA,
                x, y + h - ry_bl);
        }

        rdt_path_line_to(path, x, y + ry_tl);
        if (rx_tl > 0.0f || ry_tl > 0.0f) {
            rdt_path_cubic_to(path,
                x, y + ry_tl - ry_tl * RENDER_PATH_KAPPA,
                x + rx_tl - rx_tl * RENDER_PATH_KAPPA, y,
                x + rx_tl, y);
        }
    } else {
        rdt_path_move_to(path, x + rx_tl, y);
        if (rx_tl > 0.0f || ry_tl > 0.0f) {
            rdt_path_cubic_to(path,
                x + rx_tl - rx_tl * RENDER_PATH_KAPPA, y,
                x, y + ry_tl - ry_tl * RENDER_PATH_KAPPA,
                x, y + ry_tl);
        }
        rdt_path_line_to(path, x, y + h - ry_bl);
        if (rx_bl > 0.0f || ry_bl > 0.0f) {
            rdt_path_cubic_to(path,
                x, y + h - ry_bl + ry_bl * RENDER_PATH_KAPPA,
                x + rx_bl - rx_bl * RENDER_PATH_KAPPA, y + h,
                x + rx_bl, y + h);
        }
        rdt_path_line_to(path, x + w - rx_br, y + h);
        if (rx_br > 0.0f || ry_br > 0.0f) {
            rdt_path_cubic_to(path,
                x + w - rx_br + rx_br * RENDER_PATH_KAPPA, y + h,
                x + w, y + h - ry_br + ry_br * RENDER_PATH_KAPPA,
                x + w, y + h - ry_br);
        }
        rdt_path_line_to(path, x + w, y + ry_tr);
        if (rx_tr > 0.0f || ry_tr > 0.0f) {
            rdt_path_cubic_to(path,
                x + w, y + ry_tr - ry_tr * RENDER_PATH_KAPPA,
                x + w - rx_tr + rx_tr * RENDER_PATH_KAPPA, y,
                x + w - rx_tr, y);
        }
    }

    rdt_path_close(path);
}

Corner render_path_uniform_corner(float top_left, float top_right,
                                  float bottom_right, float bottom_left) {
    Corner radius = {};
    radius.horizontal[0] = radius.vertical[0] = top_left;
    radius.horizontal[1] = radius.vertical[1] = top_right;
    radius.horizontal[2] = radius.vertical[2] = bottom_right;
    radius.horizontal[3] = radius.vertical[3] = bottom_left;
    return radius;
}

struct RenderPathSvgWriter {StrBuf* out;int precision;};
static bool render_path_svg_visit(void* context, RdtPathCommand command,
                                  const float* args, int arg_count) {
    auto* writer = (RenderPathSvgWriter*)context;
    StrBuf* out = writer->out;
    int precision = writer->precision;
    if (!out) return false;
    if (command == RDT_PATH_CLOSE) {
        strbuf_append_str(out, " Z");
        return true;
    }
    if (!args) return false;
    switch (command) {
        case RDT_PATH_CLOSE:
            return true;
        case RDT_PATH_MOVE:
            if (arg_count < 2) return false;
            strbuf_append_format(out, "M%.*f,%.*f", precision, args[0], precision, args[1]);
            return true;
        case RDT_PATH_LINE:
            if (arg_count < 2) return false;
            strbuf_append_format(out, " L%.*f,%.*f", precision, args[0], precision, args[1]);
            return true;
        case RDT_PATH_CUBIC:
            if (arg_count < 6) return false;
            strbuf_append_format(out, " C%.*f,%.*f %.*f,%.*f %.*f,%.*f",
                precision, args[0], precision, args[1], precision, args[2], precision, args[3], precision, args[4], precision, args[5]);
            return true;
        case RDT_PATH_RECT:
            if (arg_count < 4) return false;
            strbuf_append_format(out, "M%.*f,%.*f L%.*f,%.*f L%.*f,%.*f L%.*f,%.*f Z",
                precision, args[0], precision, args[1], precision, args[0] + args[2], precision, args[1],
                precision, args[0] + args[2], precision, args[1] + args[3], precision, args[0], precision, args[1] + args[3]);
            return true;
        case RDT_PATH_QUAD:
        case RDT_PATH_CIRCLE:
            return false;
    }
    return false;
}

bool render_path_append_svg(StrBuf* out, const RdtPath* path, int precision) {
    RenderPathSvgWriter writer={out,precision};
    return out && path && precision>=0 && precision<=9 && rdt_path_visit(path, render_path_svg_visit, &writer);
}

void render_path_append_svg_rounded_rect(StrBuf* out, Rect rect,
                                         const Corner* radius) {
    if (!out) return;
    RdtPath* path = render_path_create_rounded_rect(rect, radius);
    if (!path) return;
    render_path_append_svg(out, path);
    rdt_path_free(path);
}

RdtPath* render_path_create_clip_path(RasterRenderContext* rdcon) {
    if (!rdcon) {
        return nullptr;
    }

    float clip_x = rdcon->block.clip.left;
    float clip_y = rdcon->block.clip.top;
    float clip_w = rdcon->block.clip.right - rdcon->block.clip.left;
    float clip_h = rdcon->block.clip.bottom - rdcon->block.clip.top;
    Rect clip_rect = {clip_x, clip_y, clip_w, clip_h};

    if (rdcon->block.has_clip_radius) {
        Corner clip_radius = rdcon->block.clip_radius;
        constrain_corner_radii(&clip_radius, clip_w, clip_h);
        return render_path_create_rounded_rect(clip_rect, &clip_radius);
    }

    return render_path_create_rounded_rect(clip_rect, nullptr);
}

RdtPath* render_path_create_decoration(const Rect* rect, CssEnum style, bool wavy_fill) {
    if (!rect || rect->width <= 0.0f || rect->height <= 0.0f) return nullptr;
    RdtPath* path = rdt_path_new();
    if (!path) return nullptr;
    float left = rect->x, right = left + rect->width, thickness = rect->height;
    if (style == CSS_VALUE_DASHED || style == CSS_VALUE_DOTTED) {
        float dash = thickness * (style == CSS_VALUE_DASHED ? 3.0f : 1.0f);
        for (float x = left; x < right; x += dash * 2.0f)
            rdt_path_add_rect(path, x, rect->y, fminf(dash, right - x), thickness, 0.0f, 0.0f);
    } else if (style == CSS_VALUE_DOUBLE) {
        rdt_path_add_rect(path, left, rect->y, rect->width, thickness, 0.0f, 0.0f);
        rdt_path_add_rect(path, left, rect->y + thickness + fmaxf(1.0f, thickness - 1.0f),
            rect->width, thickness, 0.0f, 0.0f);
    } else if (style == CSS_VALUE_WAVY) {
        float amplitude = thickness * 1.5f, half_wave = thickness * 2.0f;
        float center = rect->y + amplitude;
        float half_stroke = wavy_fill ? fmaxf(1.0f, thickness * 0.5f) * 0.5f : 0.0f;
        rdt_path_move_to(path, left, center - half_stroke);
        int segment = 0;
        for (float x = left; x < right; segment++) {
            float half = fminf(half_wave, right - x);
            float crest = center + (segment % 2 ? amplitude : -amplitude) - half_stroke;
            rdt_path_cubic_to(path, x + half * 0.33f, crest, x + half * 0.67f, crest,
                x + half, center - half_stroke);
            x += half;
        }
        if (wavy_fill) {
            rdt_path_line_to(path, right, center + half_stroke);
            for (segment--; segment >= 0; segment--) {
                float x = left + (float)segment * half_wave;
                float half = fminf(half_wave, right - x);
                float crest = center + (segment % 2 ? amplitude : -amplitude) + half_stroke;
                rdt_path_cubic_to(path, x + half * 0.67f, crest, x + half * 0.33f, crest,
                    x, center + half_stroke);
            }
            rdt_path_close(path);
        }
    } else rdt_path_add_rect(path, left, rect->y, rect->width, thickness, 0.0f, 0.0f);
    return path;
}

static float render_path_point_segment_distance_sq(float point_x, float point_y,
                                                  float start_x, float start_y,
                                                  float end_x, float end_y) {
    float dx = end_x - start_x;
    float dy = end_y - start_y;
    float length_sq = dx * dx + dy * dy;
    if (length_sq <= 0.000001f) {
        float px = point_x - start_x;
        float py = point_y - start_y;
        return px * px + py * py;
    }
    float projection = ((point_x - start_x) * dx + (point_y - start_y) * dy) /
        length_sq;
    projection = clamp_unit(projection);
    float closest_x = start_x + dx * projection;
    float closest_y = start_y + dy * projection;
    float px = point_x - closest_x;
    float py = point_y - closest_y;
    return px * px + py * py;
}

static bool render_path_flatten_cubic_step(float x0, float y0, float x1, float y1,
    float x2, float y2, float x3, float y3, float flatness_sq, int depth,
    RenderPathLineFn line, void* context) {
    if (depth == 0 || (render_path_point_segment_distance_sq(x1, y1, x0, y0, x3, y3) <= flatness_sq &&
        render_path_point_segment_distance_sq(x2, y2, x0, y0, x3, y3) <= flatness_sq))
        return line(context, x0, y0, x3, y3);
    float ax = (x0 + x1) * .5f, ay = (y0 + y1) * .5f;
    float bx = (x1 + x2) * .5f, by = (y1 + y2) * .5f;
    float cx = (x2 + x3) * .5f, cy = (y2 + y3) * .5f;
    float dx = (ax + bx) * .5f, dy = (ay + by) * .5f;
    float ex = (bx + cx) * .5f, ey = (by + cy) * .5f;
    float mx = (dx + ex) * .5f, my = (dy + ey) * .5f;
    return render_path_flatten_cubic_step(x0, y0, ax, ay, dx, dy, mx, my, flatness_sq, depth - 1, line, context) &&
        render_path_flatten_cubic_step(mx, my, ex, ey, cx, cy, x3, y3, flatness_sq, depth - 1, line, context);
}

bool render_path_flatten_cubic(float x0, float y0, float x1, float y1,
    float x2, float y2, float x3, float y3, float flatness, int max_depth,
    RenderPathLineFn line, void* context) {
    if (!line || !isfinite(flatness) || flatness <= 0.0f || max_depth < 0 || max_depth > 20) return false;
    return render_path_flatten_cubic_step(x0, y0, x1, y1, x2, y2, x3, y3,
        flatness * flatness, max_depth, line, context);
}

struct RenderPathMetricBuilder {
    RdtPathMetrics* metrics;
    float x, y, start_x, start_y, flatness;
    bool pending_subpath;
};

static bool render_path_metric_line(void* data, float x0, float y0, float x1, float y1) {
    RenderPathMetricBuilder* builder = (RenderPathMetricBuilder*)data;
    RdtPathMetrics* metrics = builder->metrics;
    float length = hypotf(x1 - x0, y1 - y0);
    if (!isfinite(length) || !isfinite(metrics->length + length)) return false;
    if (length == 0.0f) return true;
    // pathological contours fail explicitly instead of allocating unbounded subdivision data.
    if (metrics->count >= (1u << 20) || !mem_grow_array_raw_limit((void**)&metrics->segments,
        sizeof(RdtPathMetricSegment), &metrics->capacity, metrics->count + 1, 16, 1u << 20, MEM_CAT_RENDER)) return false;
    if (builder->pending_subpath) { metrics->subpath_count++; builder->pending_subpath = false; }
    metrics->length += length;
    metrics->segments[metrics->count++] = {x0, y0, x1, y1, length, metrics->length, metrics->subpath_count - 1};
    metrics->closed = false;
    return true;
}

static bool render_path_metric_visit(void* data, RdtPathCommand command, const float* args, int) {
    RenderPathMetricBuilder* builder = (RenderPathMetricBuilder*)data;
    bool valid = true;
    if (command == RDT_PATH_MOVE) {
        builder->x = builder->start_x = args[0]; builder->y = builder->start_y = args[1];
        builder->pending_subpath = true;
    } else if (command == RDT_PATH_LINE || command == RDT_PATH_CLOSE) {
        float x = command == RDT_PATH_CLOSE ? builder->start_x : args[0];
        float y = command == RDT_PATH_CLOSE ? builder->start_y : args[1];
        valid = render_path_metric_line(builder, builder->x, builder->y, x, y);
        builder->x = x; builder->y = y;
        if (command == RDT_PATH_CLOSE) builder->metrics->closed = builder->metrics->subpath_count == 1;
    } else if (command == RDT_PATH_CUBIC) {
        valid = render_path_flatten_cubic(builder->x, builder->y, args[0], args[1], args[2], args[3],
            args[4], args[5], builder->flatness, 16, render_path_metric_line, builder);
        builder->x = args[4]; builder->y = args[5];
    } else return false;
    return valid;
}

void render_path_metrics_destroy(RdtPathMetrics* metrics) {
    if (!metrics) return;
    lam::free_owned(metrics->segments); *metrics = {};
}

bool render_path_metrics_build(RdtPathMetrics* metrics, const RdtPath* path,
    const RdtMatrix* transform, float flatness) {
    if (!metrics || !path || !isfinite(flatness) || flatness <= 0.0f) return false;
    render_path_metrics_destroy(metrics);
    RdtPath* expanded = rdt_path_new();
    if (!expanded) return false;
    RdtMatrix identity = rdt_matrix_identity();
    bool valid = render_path_append_transformed(expanded, path, transform ? transform : &identity);
    RenderPathMetricBuilder builder = {metrics, 0.0f, 0.0f, 0.0f, 0.0f, flatness, true};
    if (valid) valid = rdt_path_visit(expanded, render_path_metric_visit, &builder);
    rdt_path_free(expanded);
    if (!valid || metrics->count == 0) { render_path_metrics_destroy(metrics); return false; }
    return true;
}

bool render_path_metrics_sample(const RdtPathMetrics* metrics, float distance,
    float* x, float* y, float* tangent_x, float* tangent_y, bool extend) {
    if (!metrics || !metrics->count || !isfinite(distance) || !x || !y || !tangent_x || !tangent_y ||
        (!extend && (distance < 0.0f || distance > metrics->length))) return false;
    size_t first = 0, end = metrics->count;
    while (first + 1 < end) {
        size_t middle = first + (end - first) / 2;
        if (distance > metrics->segments[middle - 1].end_distance) first = middle; else end = middle;
    }
    const RdtPathMetricSegment* segment = &metrics->segments[first];
    float offset = distance - (segment->end_distance - segment->length);
    *tangent_x = (segment->x1 - segment->x0) / segment->length;
    *tangent_y = (segment->y1 - segment->y0) / segment->length;
    *x = segment->x0 + offset * *tangent_x; *y = segment->y0 + offset * *tangent_y;
    return true;
}

bool render_path_metrics_project(const RdtPathMetrics* metrics, float x, float y,
    float from, float to, float* distance, float* normal, bool wrap) {
    if (!metrics || !metrics->count || !distance || !normal || !isfinite(x) || !isfinite(y) ||
        !isfinite(from) || !isfinite(to) || from > to) return false;
    float cycle_offset = 0.0f;
    if (wrap) {
        if (!metrics->closed || to - from > metrics->length) return false;
        cycle_offset = floorf(from / metrics->length) * metrics->length;
        from -= cycle_offset; to -= cycle_offset;
    }
    float best = INFINITY;
    bool found = false;
    for (size_t index = 0; index < metrics->count; index++) {
        const RdtPathMetricSegment& segment = metrics->segments[index];
        float tx = (segment.x1 - segment.x0) / segment.length;
        float ty = (segment.y1 - segment.y0) / segment.length;
        for (int cycle = 0; cycle < (wrap ? 2 : 1); cycle++) {
            float start = segment.end_distance - segment.length + (float)cycle * metrics->length;
            float lower = !wrap && index == 0 ? from : fmaxf(from, start);
            float upper = !wrap && index + 1 == metrics->count ? to : fminf(to, start + segment.length);
            if (lower > upper) continue;
            float along = start + (x - segment.x0) * tx + (y - segment.y0) * ty;
            along = fminf(fmaxf(along, lower), upper);
            float px = segment.x0 + (along - start) * tx, py = segment.y0 + (along - start) * ty;
            float dx = x - px, dy = y - py, squared = dx * dx + dy * dy;
            if (squared < best) {
                best = squared; found = true;
                *distance = along + cycle_offset; *normal = -dx * ty + dy * tx;
            }
        }
    }
    return found;
}
