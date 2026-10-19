#include "render.hpp"
#include "rdt_path_internal.hpp"
#include "render_metrics.hpp"
#include "../lib/hash.h"
#include "../lib/mem_grow.hpp"
#include <limits.h>

static void path_storage_release(RdtPathStorage* storage);

static RdtPath::Entry* path_append_entry(RdtPath* path, RdtPath::Cmd command) {
    if (!path || path->count == INT_MAX) return nullptr;
    RdtPathStorage* old = path->storage;
    bool shared = old && ref_count_get(&old->refs) > 1;
    if (shared || path->count == path->capacity) {
        int capacity;
        if (!lam::grow_capacity(shared ? path->count : path->capacity, path->count + 1, 16, &capacity)) return nullptr;
        size_t bytes = sizeof(RdtPathStorage) + (size_t)capacity * sizeof(RdtPath::Entry);
        RdtPathStorage* data = (RdtPathStorage*)(shared ? mem_alloc(bytes, MEM_CAT_RENDER) : mem_realloc(old, bytes, MEM_CAT_RENDER));
        if (!data) return nullptr;
        if (!old || shared) {
            memset(data, 0, sizeof(*data)); ref_count_init(&data->refs);
        }
        RdtPath::Entry* entries = (RdtPath::Entry*)(data + 1);
        if (shared) {
            memcpy(entries, path->entries, (size_t)path->count * sizeof(RdtPath::Entry));
            RENDER_METRIC_ADD(path_copy_bytes, (size_t)path->count * sizeof(RdtPath::Entry));
            path_storage_release(old);
        }
        path->storage = data; path->entries = entries; path->capacity = capacity;
    }
    path->storage->hash_cached = path->storage->bounds_cached = false;
    RdtPath::Entry* entry = &path->entries[path->count++];
    // unused arguments must be deterministic for exact geometry comparisons and hashes.
    *entry = {}; entry->cmd = command;
    return entry;
}

uint64_t rdt_path_hash(const RdtPath* path) {
    uint64_t h = HASH_FNV1A_64_OFFSET_BASIS;
    if (!path) return h;
    if (path->storage && path->storage->hash_cached) return path->storage->hash;
    RENDER_METRIC_ADD(path_hash_bytes, (size_t)path->count * sizeof(RdtPath::Entry));
    h = hash_fnv1a_64_extend(h, &path->count, sizeof(path->count));
    for (int i = 0; i < path->count; i++) {
        const RdtPath::Entry* e = &path->entries[i];
        h = hash_fnv1a_64_extend(h, &e->cmd, sizeof(e->cmd));
        h = hash_fnv1a_64_extend(h, e->args, sizeof(e->args));
    }
    if (path->storage) { path->storage->hash = h; path->storage->hash_cached = true; }
    return h;
}

RdtPath* rdt_path_new(void) {
    RdtPath* p = (RdtPath*)mem_calloc(1, sizeof(RdtPath), MEM_CAT_RENDER); // OBJ_HEAP_OK: the caller owns the path; rdt_path_free releases it
    return p;
}

void rdt_path_move_to(RdtPath* p, float x, float y) {
    RdtPath::Entry* e = path_append_entry(p, RdtPath::CMD_MOVE);
    if (!e) return;
    e->args[0] = x; e->args[1] = y;
}

void rdt_path_line_to(RdtPath* p, float x, float y) {
    RdtPath::Entry* e = path_append_entry(p, RdtPath::CMD_LINE);
    if (!e) return;
    e->args[0] = x; e->args[1] = y;
}

void rdt_path_cubic_to(RdtPath* p, float cx1, float cy1,
                       float cx2, float cy2, float x, float y) {
    RdtPath::Entry* e = path_append_entry(p, RdtPath::CMD_CUBIC);
    if (!e) return;
    e->args[0] = cx1; e->args[1] = cy1;
    e->args[2] = cx2; e->args[3] = cy2;
    e->args[4] = x; e->args[5] = y;
}

void rdt_path_close(RdtPath* p) {
    path_append_entry(p, RdtPath::CMD_CLOSE);
}

void rdt_path_add_rect(RdtPath* p, float x, float y, float w, float h,
                       float rx, float ry) {
    RdtPath::Entry* e = path_append_entry(p, RdtPath::CMD_RECT);
    if (!e) return;
    e->args[0] = x; e->args[1] = y;
    e->args[2] = w; e->args[3] = h;
    e->args[4] = rx; e->args[5] = ry;
}

void rdt_path_add_circle(RdtPath* p, float cx, float cy, float rx, float ry) {
    RdtPath::Entry* e = path_append_entry(p, RdtPath::CMD_CIRCLE);
    if (!e) return;
    e->args[0] = cx; e->args[1] = cy;
    e->args[2] = rx; e->args[3] = ry;
}

static void path_storage_release(RdtPathStorage* storage) {
    if (storage && ref_count_release(&storage->refs) == REF_COUNT_LAST) mem_free(storage);
}

void rdt_path_free(RdtPath* p) {
    if (!p) return;
    path_storage_release(p->storage);
    mem_free(p);
}

RdtPath* rdt_path_clone(const RdtPath* src) {
    if (!src || src->count < 0 || (src->count > 0 && !src->entries)) return nullptr;
    RdtPath* dst = rdt_path_new();
    if (!dst) return nullptr;
    // seed derived facts on the producer before readers share this storage across tile workers.
    float l, t, r, b;
    rdt_path_get_bounds(src, &l, &t, &r, &b);
    rdt_path_hash(src);
    if (src->storage && !ref_count_retain(&src->storage->refs)) { mem_free(dst); return nullptr; }
    *dst = *src;
    RENDER_METRIC_ADD(path_clones, 1);
    return dst;
}

static void path_bounds_include_point(bool* has_point, float* left, float* top,
                                      float* right, float* bottom,
                                      float x, float y) {
    if (!*has_point) {
        *left = *right = x;
        *top = *bottom = y;
        *has_point = true;
        return;
    }
    if (x < *left) *left = x;
    if (x > *right) *right = x;
    if (y < *top) *top = y;
    if (y > *bottom) *bottom = y;
}

static void path_bounds_include_rect(bool* has_point, float* left, float* top,
                                     float* right, float* bottom,
                                     float x, float y, float w, float h) {
    float x0 = w < 0 ? x + w : x;
    float x1 = w < 0 ? x : x + w;
    float y0 = h < 0 ? y + h : y;
    float y1 = h < 0 ? y : y + h;
    path_bounds_include_point(has_point, left, top, right, bottom, x0, y0);
    path_bounds_include_point(has_point, left, top, right, bottom, x1, y1);
}

static void path_bounds_include_cubic_axis(float p0, float p1, float p2, float p3,
                                           float* low, float* high) {
    // paint-server bounds use curve extrema, not the Bezier control-point hull.
    double a = -(double)p0 + 3.0 * p1 - 3.0 * p2 + p3;
    double b = 2.0 * ((double)p0 - 2.0 * p1 + p2);
    double c = (double)p1 - p0;
    double roots[2];
    int count = 0;
    if (a == 0.0) {
        if (b != 0.0) roots[count++] = -c / b;
    } else {
        double discriminant = b * b - 4.0 * a * c;
        if (discriminant >= 0.0) {
            // the stable quadratic form avoids cancellation at a nearly linear curve.
            double q = -0.5 * (b + copysign(sqrt(discriminant), b));
            roots[count++] = q / a;
            if (q != 0.0) roots[count++] = c / q;
        }
    }
    for (int i = 0; i < count; i++) {
        double t = roots[i];
        if (t <= 0.0 || t >= 1.0) continue;
        double u = 1.0 - t;
        float value = (float)(u * u * u * p0 + 3.0 * u * u * t * p1
                              + 3.0 * u * t * t * p2 + t * t * t * p3);
        *low = fminf(*low, value);
        *high = fmaxf(*high, value);
    }
}

bool rdt_path_get_bounds(const RdtPath* p, float* left, float* top,
                         float* right, float* bottom) {
    if (!p || !left || !top || !right || !bottom) return false;
    RdtPathStorage* storage = p->storage;
    if (storage && storage->bounds_cached) {
        if (!storage->has_bounds) return false;
        *left = storage->bounds[0]; *top = storage->bounds[1];
        *right = storage->bounds[2]; *bottom = storage->bounds[3];
        return true;
    }

    bool has_point = false;
    float l = 0, t = 0, r = 0, b = 0;
    float x = 0.0f, y = 0.0f, start_x = 0.0f, start_y = 0.0f;
    for (int i = 0; i < p->count; i++) {
        const RdtPath::Entry* e = &p->entries[i];
        switch (e->cmd) {
            case RdtPath::CMD_MOVE:
                x = start_x = e->args[0];
                y = start_y = e->args[1];
                break;
            case RdtPath::CMD_LINE:
                path_bounds_include_point(&has_point, &l, &t, &r, &b, x, y);
                x = e->args[0]; y = e->args[1];
                path_bounds_include_point(&has_point, &l, &t, &r, &b,
                                          x, y);
                break;
            case RdtPath::CMD_CUBIC:
                path_bounds_include_point(&has_point, &l, &t, &r, &b, x, y);
                path_bounds_include_point(&has_point, &l, &t, &r, &b,
                                          e->args[4], e->args[5]);
                path_bounds_include_cubic_axis(x, e->args[0], e->args[2], e->args[4], &l, &r);
                path_bounds_include_cubic_axis(y, e->args[1], e->args[3], e->args[5], &t, &b);
                x = e->args[4]; y = e->args[5];
                break;
            case RdtPath::CMD_RECT:
                path_bounds_include_rect(&has_point, &l, &t, &r, &b,
                                         e->args[0], e->args[1],
                                         e->args[2], e->args[3]);
                // appended closed primitives also establish the next segment's current point.
                x = start_x = e->args[0] + e->args[2];
                y = start_y = e->args[1] + fminf(e->args[5], e->args[3] * 0.5f);
                break;
            case RdtPath::CMD_CIRCLE:
                path_bounds_include_rect(&has_point, &l, &t, &r, &b,
                                         e->args[0] - e->args[2],
                                         e->args[1] - e->args[3],
                                         e->args[2] * 2.0f,
                                         e->args[3] * 2.0f);
                x = start_x = e->args[0];
                y = start_y = e->args[1] - e->args[3];
                break;
            case RdtPath::CMD_CLOSE:
                x = start_x; y = start_y;
                break;
        }
    }
    if (storage) {
        storage->bounds[0] = l; storage->bounds[1] = t;
        storage->bounds[2] = r; storage->bounds[3] = b;
        storage->has_bounds = has_point;
        storage->bounds_cached = true;
    }
    if (!has_point) return false;
    *left = l;
    *top = t;
    *right = r;
    *bottom = b;
    return true;
}

bool rdt_path_visit(const RdtPath* p, RdtPathVisitFn fn, void* context) {
    if (!p || !fn) return false;
    for (int i = 0; i < p->count; i++) {
        const RdtPath::Entry* e = &p->entries[i];
        RdtPathCommand command = RDT_PATH_MOVE;
        int arg_count = 0;
        switch (e->cmd) {
        case RdtPath::CMD_MOVE:
            command = RDT_PATH_MOVE;
            arg_count = 2;
            break;
        case RdtPath::CMD_LINE:
            command = RDT_PATH_LINE;
            arg_count = 2;
            break;
        case RdtPath::CMD_CUBIC:
            command = RDT_PATH_CUBIC;
            arg_count = 6;
            break;
        case RdtPath::CMD_CLOSE:
            command = RDT_PATH_CLOSE;
            arg_count = 0;
            break;
        case RdtPath::CMD_RECT:
            command = RDT_PATH_RECT;
            arg_count = 6;
            break;
        case RdtPath::CMD_CIRCLE:
            command = RDT_PATH_CIRCLE;
            arg_count = 4;
            break;
        }
        if (!fn(context, command, e->args, arg_count)) return false;
    }
    return true;
}
