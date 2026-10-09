#include "render_css3d.hpp"
#include "layout.hpp"
#include "../lib/tagged.hpp"
#include <math.h>

static const float CSS3D_NEAR_W = 0.00001f;
static const float CSS3D_PLANE_EPSILON = 0.0001f;

struct Css3dPlane { float x, y, z, d; };

static Css3dVertex css3d_interpolate_homogeneous(const Css3dVertex& a,
    const Css3dVertex& b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t,
        a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t};
}

static Css3dVertex css3d_interpolate_projected(const Css3dVertex& a,
    const Css3dVertex& b, float t) {
    // A BSP split is linear after projection. Interpolate 1/w and attributes/w
    // there, then reconstruct homogeneous coordinates to preserve texture UVs.
    float a_weight = (1.0f - t) / a.w;
    float b_weight = t / b.w;
    float w = 1.0f / (a_weight + b_weight);
    return {(a.x * a_weight + b.x * b_weight) * w,
        (a.y * a_weight + b.y * b_weight) * w,
        (a.z * a_weight + b.z * b_weight) * w, w,
        (a.u * a_weight + b.u * b_weight) * w,
        (a.v * a_weight + b.v * b_weight) * w};
}

bool css3d_project_quad(const RdtMatrix4* matrix, Rect rect, size_t fragment,
    uint64_t paint_order, ScratchScope* scratch, Css3dPolygon* polygon) {
    if (!matrix || !scratch || !polygon) return false;
    *polygon = {};
    Css3dVertex corners[4] = {};
    const float xs[] = {rect.x, rect.x + rect.width, rect.x + rect.width, rect.x};
    const float ys[] = {rect.y, rect.y, rect.y + rect.height, rect.y + rect.height};
    for (size_t i = 0; i < 4; i++) {
        rdt_matrix4_transform_point(matrix, xs[i], ys[i], 0.0f,
            &corners[i].x, &corners[i].y, &corners[i].z, &corners[i].w);
        corners[i].u = xs[i]; corners[i].v = ys[i];
    }
    Css3dVertex* vertices = scratch->array<Css3dVertex>(8);
    if (!vertices) return false;
    size_t written = 0;
    Css3dVertex previous = corners[3];
    for (const Css3dVertex& current : corners) {
        bool previous_inside = previous.w >= CSS3D_NEAR_W;
        bool current_inside = current.w >= CSS3D_NEAR_W;
        if (previous_inside != current_inside) {
            float t = (CSS3D_NEAR_W - previous.w) / (current.w - previous.w);
            Css3dVertex intersection = css3d_interpolate_homogeneous(previous, current, t);
            intersection.w = CSS3D_NEAR_W;
            vertices[written++] = intersection;
        }
        if (current_inside) vertices[written++] = current;
        previous = current;
    }
    *polygon = {lam::up(vertices), written >= 3 ? written : 0, fragment, paint_order};
    return true;
}

static float css3d_viewport_distance(const Css3dVertex& vertex, const RdtMatrix& paint,
    Rect viewport, unsigned edge) {
    float w = paint.e31 * vertex.u + paint.e32 * vertex.v + paint.e33;
    float x = paint.e11 * vertex.u + paint.e12 * vertex.v + paint.e13;
    float y = paint.e21 * vertex.u + paint.e22 * vertex.v + paint.e23;
    switch (edge) {
        case 0: return w - CSS3D_NEAR_W;
        case 1: return x - viewport.x * w;
        case 2: return (viewport.x + viewport.width) * w - x;
        case 3: return y - viewport.y * w;
        default: return (viewport.y + viewport.height) * w - y;
    }
}

bool css3d_clip_to_viewport(Css3dPolygon* polygon, const RdtMatrix* paint,
    Rect viewport, ScratchScope* scratch) {
    if (!polygon || !paint || !scratch) return false;
    // Clip before division: viewer-plane vertices can project millions of
    // pixels away, destabilizing plane equations and vector raster bounds.
    for (unsigned edge = 0; edge < 5 && polygon->count; edge++) {
        if (polygon->count > SIZE_MAX / 2) return false;
        Css3dVertex* vertices = scratch->array<Css3dVertex>(polygon->count * 2);
        if (!vertices) return false;
        size_t written = 0;
        Css3dVertex previous = polygon->vertices[polygon->count - 1];
        float previous_distance = css3d_viewport_distance(previous, *paint, viewport, edge);
        for (size_t i = 0; i < polygon->count; i++) {
            const Css3dVertex& current = polygon->vertices[i];
            float distance = css3d_viewport_distance(current, *paint, viewport, edge);
            if ((previous_distance >= 0.0f) != (distance >= 0.0f))
                vertices[written++] = css3d_interpolate_homogeneous(previous, current,
                    previous_distance / (previous_distance - distance));
            if (distance >= 0.0f) vertices[written++] = current;
            previous = current; previous_distance = distance;
        }
        polygon->vertices = lam::up(vertices);
        polygon->count = written >= 3 ? written : 0;
    }
    return true;
}

static bool css3d_project_vertex(const Css3dVertex& vertex, const RdtMatrix& paint,
    float scale, float* x, float* y) {
    float w = paint.e31 * vertex.u + paint.e32 * vertex.v + paint.e33;
    if (w <= 0.0f) return false;
    *x = (paint.e11 * vertex.u + paint.e12 * vertex.v + paint.e13) * scale / w;
    *y = (paint.e21 * vertex.u + paint.e22 * vertex.v + paint.e23) * scale / w;
    return isfinite(*x) && isfinite(*y);
}

bool render_path_project_to_viewport(RdtPath* destination, const RdtPath* source,
    const RdtMatrix* transform, Rect viewport, bool fill, ScratchScope* scratch) {
    if (!destination || !source || !transform || !scratch) return false;
    RdtPathMetrics metrics = {};
    if (!render_path_metrics_build(&metrics, source, nullptr, 0.125f)) return true;
    bool valid = [&]() {
        for (size_t start = 0; start < metrics.count;) {
            size_t end = start + 1;
            while (end < metrics.count && metrics.segments[end].subpath == metrics.segments[start].subpath) end++;
            Css3dVertex* vertices = scratch->array<Css3dVertex>(end - start + 1);
            if (!vertices) return false;
            const auto& first = metrics.segments[start];
            vertices[0] = {first.x0, first.y0, 0.0f, 1.0f, first.x0, first.y0};
            for (size_t i = start; i < end; i++) {
                const auto& segment = metrics.segments[i];
                vertices[i - start + 1] = {segment.x1, segment.y1, 0.0f, 1.0f, segment.x1, segment.y1};
            }
            if (fill) {
                Css3dPolygon polygon = {lam::up(vertices), end - start + 1, 0, 0};
                if (!css3d_clip_to_viewport(&polygon, transform, viewport, scratch)) return false;
                for (size_t i = 0; i < polygon.count; i++) {
                    float x, y;
                    if (!css3d_project_vertex(polygon.vertices[i], *transform, 1.0f, &x, &y)) return false;
                    if (i == 0) rdt_path_move_to(destination, x, y);
                    else rdt_path_line_to(destination, x, y);
                }
                if (polygon.count) rdt_path_close(destination);
            } else {
                // Stroke contours retain open ends: clip each segment without
                // introducing the boundary edges required by filled polygons.
                for (size_t i = 1; i <= end - start; i++) {
                    Css3dVertex a = vertices[i - 1], b = vertices[i];
                    bool visible = true;
                    for (unsigned edge = 0; edge < 5 && visible; edge++) {
                        float da = css3d_viewport_distance(a, *transform, viewport, edge);
                        float db = css3d_viewport_distance(b, *transform, viewport, edge);
                        if (da < 0.0f && db < 0.0f) visible = false;
                        else if ((da >= 0.0f) != (db >= 0.0f)) {
                            Css3dVertex intersection = css3d_interpolate_homogeneous(a, b, da / (da - db));
                            if (da < 0.0f) a = intersection; else b = intersection;
                        }
                    }
                    if (!visible) continue;
                    float x0, y0, x1, y1;
                    if (!css3d_project_vertex(a, *transform, 1.0f, &x0, &y0) ||
                        !css3d_project_vertex(b, *transform, 1.0f, &x1, &y1)) return false;
                    rdt_path_move_to(destination, x0, y0); rdt_path_line_to(destination, x1, y1);
                }
            }
            start = end;
        }
        return true;
    }();
    render_path_metrics_destroy(&metrics);
    return valid;
}

static bool css3d_plane(const Css3dPolygon& polygon, Css3dPlane* plane) {
    if (polygon.count < 3) return false;
    const Css3dVertex& a = polygon.vertices[0];
    float ax = a.x / a.w, ay = a.y / a.w, az = a.z / a.w;
    for (size_t i = 1; i + 1 < polygon.count; i++) {
        const Css3dVertex& b = polygon.vertices[i];
        const Css3dVertex& c = polygon.vertices[i + 1];
        float bx = b.x / b.w - ax, by = b.y / b.w - ay, bz = b.z / b.w - az;
        float cx = c.x / c.w - ax, cy = c.y / c.w - ay, cz = c.z / c.w - az;
        float nx = by * cz - bz * cy, ny = bz * cx - bx * cz, nz = bx * cy - by * cx;
        float length = sqrtf(nx * nx + ny * ny + nz * nz);
        if (length > CSS3D_PLANE_EPSILON) {
            *plane = {nx / length, ny / length, nz / length,
                -(nx * ax + ny * ay + nz * az) / length};
            return true;
        }
    }
    return false;
}

static float css3d_distance(const Css3dVertex& vertex, const Css3dPlane& plane) {
    return (plane.x * vertex.x + plane.y * vertex.y + plane.z * vertex.z) / vertex.w + plane.d;
}

static unsigned css3d_classify(const Css3dPolygon& polygon, const Css3dPlane& plane) {
    unsigned sides = 0;
    for (size_t i = 0; i < polygon.count; i++) {
        float distance = css3d_distance(polygon.vertices[i], plane);
        if (distance > CSS3D_PLANE_EPSILON) sides |= 1;
        if (distance < -CSS3D_PLANE_EPSILON) sides |= 2;
    }
    return sides;
}

static bool css3d_split(const Css3dPolygon& polygon, const Css3dPlane& plane,
    ScratchScope* scratch, Css3dPolygon* positive, Css3dPolygon* negative) {
    size_t capacity = polygon.count + 2;
    Css3dVertex* front = scratch->array<Css3dVertex>(capacity);
    Css3dVertex* back = scratch->array<Css3dVertex>(capacity);
    if (!front || !back) return false;
    size_t front_count = 0, back_count = 0;
    Css3dVertex previous = polygon.vertices[polygon.count - 1];
    float previous_distance = css3d_distance(previous, plane);
    for (size_t i = 0; i < polygon.count; i++) {
        const Css3dVertex& current = polygon.vertices[i];
        float distance = css3d_distance(current, plane);
        if ((previous_distance > CSS3D_PLANE_EPSILON && distance < -CSS3D_PLANE_EPSILON) ||
            (previous_distance < -CSS3D_PLANE_EPSILON && distance > CSS3D_PLANE_EPSILON)) {
            Css3dVertex intersection = css3d_interpolate_projected(previous, current,
                previous_distance / (previous_distance - distance));
            front[front_count++] = intersection;
            back[back_count++] = intersection;
        }
        if (distance >= -CSS3D_PLANE_EPSILON) front[front_count++] = current;
        if (distance <= CSS3D_PLANE_EPSILON) back[back_count++] = current;
        previous = current; previous_distance = distance;
    }
    *positive = {lam::up(front), front_count, polygon.fragment, polygon.paint_order};
    *negative = {lam::up(back), back_count, polygon.fragment, polygon.paint_order};
    return true;
}

static bool css3d_partition_plane(const Css3dPolygon* polygons, size_t count,
    Css3dPlane* plane, size_t* pivot) {
    size_t best_score = SIZE_MAX;
    bool found = false;
    size_t samples = count < 12 ? count : 12;
    for (size_t sample = 0; sample < samples; sample++) {
        Css3dPlane candidate;
        if (!css3d_plane(polygons[sample * count / samples], &candidate)) continue;
        size_t positive = 0, negative = 0, split = 0;
        for (size_t i = 0; i < count; i++) {
            unsigned sides = css3d_classify(polygons[i], candidate);
            positive += sides == 1; negative += sides == 2; split += sides == 3;
        }
        size_t score = split * 8 + (positive > negative ? positive - negative : negative - positive);
        if (!found || score < best_score) {
            *plane = candidate; *pivot = sample * count / samples;
            best_score = score; found = true;
        }
    }
    if (!found) {
        // Sampled degenerate/fully clipped polygons must not hide a valid plane
        // between the samples.
        for (size_t i = 0; i < count; i++) if (css3d_plane(polygons[i], plane)) {
            *pivot = i; return true;
        }
    }
    return found;
}

static bool css3d_order_partition(const Css3dPolygon* polygons, size_t count,
    ScratchScope* scratch, lam::ArrayList<Css3dPolygon>* ordered) {
    if (count == 0) return true;
    Css3dPlane plane;
    size_t pivot;
    if (!css3d_partition_plane(polygons, count, &plane, &pivot)) return true;
    lam::ArrayList<Css3dPolygon> positive(MEM_CAT_RENDER, 0), negative(MEM_CAT_RENDER, 0), coplanar(MEM_CAT_RENDER, 0);
    for (size_t i = 0; i < count; i++) {
        const Css3dPolygon& polygon = polygons[i];
        if (polygon.count < 3) continue;
        // The defining polygon is coplanar by construction. Reclassifying it
        // after near-plane projection can amplify roundoff and recurse forever.
        unsigned sides = i == pivot ? 0 : css3d_classify(polygon, plane);
        if (sides == 0) { if (!coplanar.append(polygon)) return false; }
        else if (sides == 1) { if (!positive.append(polygon)) return false; }
        else if (sides == 2) { if (!negative.append(polygon)) return false; }
        else {
            Css3dPolygon front, back;
            if (!css3d_split(polygon, plane, scratch, &front, &back)) return false;
            if (front.count >= 3 && !positive.append(front)) return false;
            if (back.count >= 3 && !negative.append(back)) return false;
        }
    }
    coplanar.sort([](const Css3dPolygon& a, const Css3dPolygon& b) {
        return a.paint_order < b.paint_order ? -1 : a.paint_order > b.paint_order ? 1 : 0;
    });
    // Perspective maps the viewer to infinity on +Z. The sign of the partition
    // normal selects its far half-space; coplanar ties keep CSS paint order.
    const auto& far = plane.z >= 0.0f ? negative : positive;
    const auto& near = plane.z >= 0.0f ? positive : negative;
    if (!css3d_order_partition(far.data(), far.size(), scratch, ordered)) return false;
    for (const Css3dPolygon& polygon : coplanar) if (!ordered->append(polygon)) return false;
    return css3d_order_partition(near.data(), near.size(), scratch, ordered);
}

bool css3d_order_planes(const Css3dPolygon* polygons, size_t count,
    ScratchScope* scratch, lam::ArrayList<Css3dPolygon>* ordered) {
    if ((!polygons && count) || !scratch || !ordered) return false;
    ordered->clear();
    return css3d_order_partition(polygons, count, scratch, ordered);
}

Css3dPaintContext::Css3dPaintContext(RasterRenderContext* context, ViewBlock* block)
    : rdcon(lam::up(context)), root(lam::up(block)), runs(MEM_CAT_RENDER, 0), current{},
      start(context->dl->item_count()), run_start(start), failed(false) {
    enter(block);
}

void Css3dPaintContext::flush() {
    int end = rdcon->dl->item_count();
    if (end > run_start && current.view &&
        !runs.append({current, run_start, end, 0, 0})) failed = true;
    run_start = end;
}

Css3dPaintPlane Css3dPaintContext::enter(ViewBlock* block) {
    flush();
    Css3dPaintPlane previous = current;
    current = {};
    current.view = lam::up(block);
    current.depth = view_accumulated_transform_3d(block, true);
    // The matrix is projected by the ordinary painter once its scope is pushed.
    // Flat packets are captured with the same basis even when collection pauses.
    RdtMatrix4 complete = view_accumulated_transform_3d(block, false);
    current.paint = radiant::matrix4_project_to_2d(&complete);
    float x = rdcon->block.x / rdcon->raster_scale + block->x;
    float y = rdcon->block.y / rdcon->raster_scale + block->y;
    float min_x = 0.0f, min_y = 0.0f, max_x = block->width, max_y = block->height;
    if (block->is_element()) {
        float low, high;
        layout_in_flow_content_bounds_cached(lam::view_require_element(block), LAYOUT_AXIS_X,
            true, rdcon->content_bounds_cache, &low, &high);
        min_x = fminf(min_x, low); max_x = fmaxf(max_x, high);
        layout_in_flow_content_bounds_cached(lam::view_require_element(block), LAYOUT_AXIS_Y,
            true, rdcon->content_bounds_cache, &low, &high);
        min_y = fminf(min_y, low); max_y = fmaxf(max_y, high);
    }
    float overflow = render_geometry_block_visual_overflow(block);
    current.rect = {x + min_x - overflow, y + min_y - overflow,
        max_x - min_x + 2.0f * overflow, max_y - min_y + 2.0f * overflow};
    return previous;
}

void Css3dPaintContext::restore(const Css3dPaintPlane& plane) {
    flush();
    current = plane;
}

void Css3dPaintContext::geometry(ViewBlock* block) {
    if (current.view != block) return;
    current.paint = rdcon->transform;
    float scale = rdcon->raster_scale;
    current.paint.e13 /= scale; current.paint.e23 /= scale;
    current.paint.e31 *= scale; current.paint.e32 *= scale;
}

bool Css3dPaintContext::compose() {
    double started = render_profiler_now_ms();
    flush();
    if (failed || runs.empty()) return !failed;
    OffscreenRenderArenas memory;
    if (!memory.init("css3d.pool", "css3d.packets", "css3d.scratch")) return false;
    DisplayList packets;
    dl_init(&packets, memory.list_arena);
    ScratchArena arena = {};
    scratch_init(&arena, memory.scratch_arena);
    bool success = [&]() {
        ScratchScope scratch(&arena);
        lam::ArrayList<int> clips(MEM_CAT_RENDER, 0);
        lam::ArrayList<Css3dPolygon> polygons(MEM_CAT_RENDER, 0), ordered(MEM_CAT_RENDER, 0);
        DisplayList* original = rdcon->dl;
        float scale = rdcon->raster_scale;
        Bound bounds = rdcon->block.clip;
        Rect viewport = {bounds.left / scale, bounds.top / scale,
            (bounds.right - bounds.left) / scale, (bounds.bottom - bounds.top) / scale};
        for (size_t index = 0; index < runs.size(); index++) {
            Css3dPaintRun& run = runs[index];
            run.packet_start = packets.item_count();
            for (int clip : clips)
                if (!dl_copy_range(&packets, original, clip, clip)) return false;
            bool painted = false;
            for (int item_index = run.start; item_index < run.end; item_index++) {
                DisplayOp op = original->data()[item_index].op;
                // Partial ancestor markers cannot be reordered as subtree
                // ranges. The composed root receives one balanced marker.
                if (op == DL_BEGIN_ELEMENT || op == DL_END_ELEMENT) continue;
                if (op == DL_PUSH_CLIP) {
                    if (!clips.append(item_index)) return false;
                } else if (op == DL_POP_CLIP) {
                    if (clips.empty()) return false;
                    clips.remove_range(clips.size() - 1, 1);
                } else painted = true;
                if (!dl_copy_range(&packets, original, item_index, item_index)) return false;
            }
            for (size_t i = 0; i < clips.size(); i++) dl_pop_clip(&packets);
            run.packet_end = packets.item_count();
            if (!painted) continue;
            Css3dPolygon polygon;
            if (!css3d_project_quad(&run.plane.depth, run.plane.rect, index, index, &scratch, &polygon)) return false;
            if (!css3d_clip_to_viewport(&polygon, &run.plane.paint, viewport, &scratch)) return false;
            if (polygon.count && !polygons.append(polygon)) return false;
        }
        if (!clips.empty() || !css3d_order_planes(polygons.data(), polygons.size(), &scratch, &ordered)) return false;
        if (rdcon->profiler) {
            rdcon->profiler->css3d_plane_count += polygons.size();
            rdcon->profiler->css3d_fragment_count += ordered.size();
        }
        // Assemble separately so a failed capture leaves the original command
        // stream intact and owned payloads are never shared by split fragments.
        int composed_start = packets.item_count();
        int marker = dl_begin_element(&packets, static_cast<View*>(root.get())->id,
            0.0f, 0.0f, 0.0f, 0.0f);
        for (const Css3dPolygon& polygon : ordered) {
            Css3dPaintRun& run = runs[polygon.fragment];
            RdtPath* clip = rdt_path_new();
            if (!clip) return false;
            bool valid = true;
            for (size_t i = 0; i < polygon.count; i++) {
                float x = 0.0f, y = 0.0f;
                const Css3dVertex& vertex = polygon.vertices[i];
                valid = valid && css3d_project_vertex(vertex, run.plane.paint, scale, &x, &y);
                if (i == 0) rdt_path_move_to(clip, x, y);
                else rdt_path_line_to(clip, x, y);
            }
            rdt_path_close(clip);
            if (valid) dl_push_clip(&packets, clip, nullptr);
            rdt_path_free(clip);
            if (!valid || !dl_copy_range(&packets, &packets, run.packet_start, run.packet_end - 1)) return false;
            dl_pop_clip(&packets);
        }
        dl_end_element(&packets, marker);
        if (!dl_validate_or_log(&packets, "css3d packets")) return false;
        return dl_replace_tail(original, start, &packets, composed_start, packets.item_count() - 1);
    }();
    scratch_release(&arena);
    dl_destroy(&packets);
    memory.destroy();
    if (rdcon->profiler) rdcon->profiler->css3d_compose_time += render_profiler_now_ms() - started;
    return success;
}
