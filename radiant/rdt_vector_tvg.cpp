// rdt_vector_tvg.cpp — ThorVG backend for RdtVector
// All ThorVG C API calls are isolated in this file.
// Radiant rendering code calls only rdt_* functions.

#include "rdt_vector.hpp"
#include "../lib/log.h"
#include <thorvg_capi.h>
#include "../lib/mem.h"
#include <string.h>

// ============================================================================
// Internal types
// ============================================================================

struct RdtVectorImpl {
    Tvg_Canvas canvas;
    uint32_t* pixels;
    int width;
    int height;
    int stride;
};

// Path stores ThorVG path commands for deferred replay
struct RdtPath {
    // store raw path commands; replay into Tvg_Paint at draw time
    enum Cmd { CMD_MOVE, CMD_LINE, CMD_CUBIC, CMD_CLOSE, CMD_RECT, CMD_CIRCLE };
    struct Entry {
        Cmd cmd;
        float args[8]; // max args: cubic(6), rect(6), circle(4)
    };
    Entry* entries;
    int count;
    int capacity;
};

struct RdtPicture {
    Tvg_Paint paint;  // tvg_picture
    float width;
    float height;
};

// ============================================================================
// Helpers
// ============================================================================

static void path_ensure_capacity(RdtPath* p) {
    if (p->count >= p->capacity) {
        int new_cap = p->capacity ? p->capacity * 2 : 16;
        p->entries = (RdtPath::Entry*)mem_realloc(p->entries, new_cap * sizeof(RdtPath::Entry), MEM_CAT_RENDER);
        p->capacity = new_cap;
    }
}

// Replay a path's commands onto a ThorVG shape
static void path_replay(RdtPath* p, Tvg_Paint shape) {
    for (int i = 0; i < p->count; i++) {
        RdtPath::Entry* e = &p->entries[i];
        switch (e->cmd) {
            case RdtPath::CMD_MOVE:
                tvg_shape_move_to(shape, e->args[0], e->args[1]);
                break;
            case RdtPath::CMD_LINE:
                tvg_shape_line_to(shape, e->args[0], e->args[1]);
                break;
            case RdtPath::CMD_CUBIC:
                tvg_shape_cubic_to(shape, e->args[0], e->args[1],
                                   e->args[2], e->args[3],
                                   e->args[4], e->args[5]);
                break;
            case RdtPath::CMD_CLOSE:
                tvg_shape_close(shape);
                break;
            case RdtPath::CMD_RECT:
                tvg_shape_append_rect(shape, e->args[0], e->args[1],
                                      e->args[2], e->args[3],
                                      e->args[4], e->args[5], true);
                break;
            case RdtPath::CMD_CIRCLE:
                tvg_shape_append_circle(shape, e->args[0], e->args[1],
                                        e->args[2], e->args[3], true);
                break;
        }
    }
}

// Central draw-and-remove: resets target, pushes, draws, syncs, removes.
// This encapsulates the ThorVG immediate-mode workaround.
static void tvg_push_draw_remove(RdtVectorImpl* impl, Tvg_Paint shape) {
    // reset target to prevent ThorVG from clearing previously-drawn content
    tvg_swcanvas_set_target(impl->canvas, impl->pixels, impl->stride,
                            impl->width, impl->height, TVG_COLORSPACE_ABGR8888);
    tvg_canvas_push(impl->canvas, shape);
    tvg_canvas_draw(impl->canvas, false);
    tvg_canvas_sync(impl->canvas);
    tvg_canvas_remove(impl->canvas, NULL);
}

// Apply optional transform to a shape
static void apply_transform(Tvg_Paint shape, const RdtMatrix* transform) {
    if (!transform) return;
    Tvg_Matrix m = { transform->e11, transform->e12, transform->e13,
                     transform->e21, transform->e22, transform->e23,
                     transform->e31, transform->e32, transform->e33 };
    tvg_paint_set_transform(shape, &m);
}

// Create a clip mask shape from a path (solid black fill for alpha masking)
static Tvg_Paint create_clip_mask(RdtPath* clip_path, const RdtMatrix* transform) {
    Tvg_Paint clip = tvg_shape_new();
    path_replay(clip_path, clip);
    tvg_shape_set_fill_color(clip, 0, 0, 0, 255);
    apply_transform(clip, transform);
    return clip;
}

// ============================================================================
// Lifecycle
// ============================================================================

void rdt_vector_init(RdtVector* vec, uint32_t* pixels, int w, int h, int stride) {
    RdtVectorImpl* impl = (RdtVectorImpl*)mem_calloc(1, sizeof(RdtVectorImpl), MEM_CAT_RENDER);
    impl->canvas = tvg_swcanvas_create(TVG_ENGINE_OPTION_DEFAULT);
    impl->pixels = pixels;
    impl->width = w;
    impl->height = h;
    impl->stride = stride;

    Tvg_Result result = tvg_swcanvas_set_target(impl->canvas, pixels, stride,
                                                 w, h, TVG_COLORSPACE_ABGR8888);
    if (result != TVG_RESULT_SUCCESS) {
        log_error("rdt_vector_init: tvg_swcanvas_set_target failed result=%d", result);
    }

    vec->impl = impl;
    log_debug("rdt_vector_init: ThorVG backend ready %dx%d stride=%d", w, h, stride);
}

void rdt_vector_destroy(RdtVector* vec) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* impl = vec->impl;
    if (impl->canvas) tvg_canvas_destroy(impl->canvas);
    mem_free(impl);
    vec->impl = nullptr;
}

void rdt_vector_set_target(RdtVector* vec, uint32_t* pixels, int w, int h, int stride) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* impl = vec->impl;
    impl->pixels = pixels;
    impl->width = w;
    impl->height = h;
    impl->stride = stride;
    tvg_swcanvas_set_target(impl->canvas, pixels, stride, w, h, TVG_COLORSPACE_ABGR8888);
}

// ============================================================================
// Path construction
// ============================================================================

RdtPath* rdt_path_new(void) {
    RdtPath* p = (RdtPath*)mem_calloc(1, sizeof(RdtPath), MEM_CAT_RENDER);
    return p;
}

void rdt_path_move_to(RdtPath* p, float x, float y) {
    path_ensure_capacity(p);
    RdtPath::Entry* e = &p->entries[p->count++];
    e->cmd = RdtPath::CMD_MOVE;
    e->args[0] = x; e->args[1] = y;
}

void rdt_path_line_to(RdtPath* p, float x, float y) {
    path_ensure_capacity(p);
    RdtPath::Entry* e = &p->entries[p->count++];
    e->cmd = RdtPath::CMD_LINE;
    e->args[0] = x; e->args[1] = y;
}

void rdt_path_cubic_to(RdtPath* p, float cx1, float cy1,
                       float cx2, float cy2, float x, float y) {
    path_ensure_capacity(p);
    RdtPath::Entry* e = &p->entries[p->count++];
    e->cmd = RdtPath::CMD_CUBIC;
    e->args[0] = cx1; e->args[1] = cy1;
    e->args[2] = cx2; e->args[3] = cy2;
    e->args[4] = x; e->args[5] = y;
}

void rdt_path_close(RdtPath* p) {
    path_ensure_capacity(p);
    RdtPath::Entry* e = &p->entries[p->count++];
    e->cmd = RdtPath::CMD_CLOSE;
}

void rdt_path_add_rect(RdtPath* p, float x, float y, float w, float h,
                       float rx, float ry) {
    path_ensure_capacity(p);
    RdtPath::Entry* e = &p->entries[p->count++];
    e->cmd = RdtPath::CMD_RECT;
    e->args[0] = x; e->args[1] = y;
    e->args[2] = w; e->args[3] = h;
    e->args[4] = rx; e->args[5] = ry;
}

void rdt_path_add_circle(RdtPath* p, float cx, float cy, float rx, float ry) {
    path_ensure_capacity(p);
    RdtPath::Entry* e = &p->entries[p->count++];
    e->cmd = RdtPath::CMD_CIRCLE;
    e->args[0] = cx; e->args[1] = cy;
    e->args[2] = rx; e->args[3] = ry;
}

void rdt_path_free(RdtPath* p) {
    if (!p) return;
    mem_free(p->entries);
    mem_free(p);
}

// ============================================================================
// Fill
// ============================================================================

// forward declare clip-aware draw helper (defined in Clipping section below)
static void tvg_push_draw_remove_clipped(RdtVectorImpl* impl, Tvg_Paint shape);

void rdt_fill_path(RdtVector* vec, RdtPath* p, Color color,
                   RdtFillRule rule, const RdtMatrix* transform) {
    if (!vec || !vec->impl || !p) return;
    RdtVectorImpl* impl = vec->impl;

    Tvg_Paint shape = tvg_shape_new();
    path_replay(p, shape);
    tvg_shape_set_fill_color(shape, color.r, color.g, color.b, color.a);
    if (rule == RDT_FILL_EVEN_ODD) {
        tvg_shape_set_fill_rule(shape, TVG_FILL_RULE_EVEN_ODD);
    }
    apply_transform(shape, transform);
    tvg_push_draw_remove_clipped(impl, shape);
}

void rdt_fill_rect(RdtVector* vec, float x, float y, float w, float h,
                   Color color) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* impl = vec->impl;

    Tvg_Paint shape = tvg_shape_new();
    tvg_shape_append_rect(shape, x, y, w, h, 0, 0, true);
    tvg_shape_set_fill_color(shape, color.r, color.g, color.b, color.a);
    tvg_push_draw_remove_clipped(impl, shape);
}

void rdt_fill_rounded_rect(RdtVector* vec, float x, float y, float w, float h,
                           float rx, float ry, Color color) {
    if (!vec || !vec->impl) return;
    RdtVectorImpl* impl = vec->impl;

    Tvg_Paint shape = tvg_shape_new();
    tvg_shape_append_rect(shape, x, y, w, h, rx, ry, true);
    tvg_shape_set_fill_color(shape, color.r, color.g, color.b, color.a);
    tvg_push_draw_remove_clipped(impl, shape);
}

// ============================================================================
// Stroke
// ============================================================================

void rdt_stroke_path(RdtVector* vec, RdtPath* p, Color color, float width,
                     RdtStrokeCap cap, RdtStrokeJoin join,
                     const float* dash_array, int dash_count,
                     const RdtMatrix* transform) {
    if (!vec || !vec->impl || !p) return;
    RdtVectorImpl* impl = vec->impl;

    Tvg_Paint shape = tvg_shape_new();
    path_replay(p, shape);

    tvg_shape_set_stroke_color(shape, color.r, color.g, color.b, color.a);
    tvg_shape_set_stroke_width(shape, width);

    // map cap
    Tvg_Stroke_Cap tvg_cap;
    switch (cap) {
        case RDT_CAP_ROUND:  tvg_cap = TVG_STROKE_CAP_ROUND; break;
        case RDT_CAP_SQUARE: tvg_cap = TVG_STROKE_CAP_SQUARE; break;
        default:             tvg_cap = TVG_STROKE_CAP_BUTT; break;
    }
    tvg_shape_set_stroke_cap(shape, tvg_cap);

    // map join
    Tvg_Stroke_Join tvg_join;
    switch (join) {
        case RDT_JOIN_ROUND: tvg_join = TVG_STROKE_JOIN_ROUND; break;
        case RDT_JOIN_BEVEL: tvg_join = TVG_STROKE_JOIN_BEVEL; break;
        default:             tvg_join = TVG_STROKE_JOIN_MITER; break;
    }
    tvg_shape_set_stroke_join(shape, tvg_join);

    // dash pattern
    if (dash_array && dash_count > 0) {
        tvg_shape_set_stroke_dash(shape, dash_array, dash_count, 0);
    }

    apply_transform(shape, transform);
    tvg_push_draw_remove_clipped(impl, shape);
}

// ============================================================================
// Gradient fill
// ============================================================================

void rdt_fill_linear_gradient(RdtVector* vec, RdtPath* p,
                              float x1, float y1, float x2, float y2,
                              const RdtGradientStop* stops, int stop_count,
                              RdtFillRule rule,
                              const RdtMatrix* transform) {
    if (!vec || !vec->impl || !p || !stops || stop_count < 2) return;
    RdtVectorImpl* impl = vec->impl;

    Tvg_Paint shape = tvg_shape_new();
    path_replay(p, shape);

    if (rule == RDT_FILL_EVEN_ODD) {
        tvg_shape_set_fill_rule(shape, TVG_FILL_RULE_EVEN_ODD);
    }

    Tvg_Gradient grad = tvg_linear_gradient_new();
    tvg_linear_gradient_set(grad, x1, y1, x2, y2);

    Tvg_Color_Stop* tvg_stops = (Tvg_Color_Stop*)alloca(stop_count * sizeof(Tvg_Color_Stop));
    for (int i = 0; i < stop_count; i++) {
        tvg_stops[i].offset = stops[i].offset;
        tvg_stops[i].r = stops[i].r;
        tvg_stops[i].g = stops[i].g;
        tvg_stops[i].b = stops[i].b;
        tvg_stops[i].a = stops[i].a;
    }
    tvg_gradient_set_color_stops(grad, tvg_stops, stop_count);
    tvg_shape_set_gradient(shape, grad);

    apply_transform(shape, transform);
    tvg_push_draw_remove_clipped(impl, shape);
}

void rdt_fill_radial_gradient(RdtVector* vec, RdtPath* p,
                              float cx, float cy, float r,
                              const RdtGradientStop* stops, int stop_count,
                              RdtFillRule rule,
                              const RdtMatrix* transform) {
    if (!vec || !vec->impl || !p || !stops || stop_count < 2) return;
    RdtVectorImpl* impl = vec->impl;

    Tvg_Paint shape = tvg_shape_new();
    path_replay(p, shape);

    if (rule == RDT_FILL_EVEN_ODD) {
        tvg_shape_set_fill_rule(shape, TVG_FILL_RULE_EVEN_ODD);
    }

    Tvg_Gradient grad = tvg_radial_gradient_new();
    tvg_radial_gradient_set(grad, cx, cy, r, cx, cy, 0);

    Tvg_Color_Stop* tvg_stops = (Tvg_Color_Stop*)alloca(stop_count * sizeof(Tvg_Color_Stop));
    for (int i = 0; i < stop_count; i++) {
        tvg_stops[i].offset = stops[i].offset;
        tvg_stops[i].r = stops[i].r;
        tvg_stops[i].g = stops[i].g;
        tvg_stops[i].b = stops[i].b;
        tvg_stops[i].a = stops[i].a;
    }
    tvg_gradient_set_color_stops(grad, tvg_stops, stop_count);
    tvg_shape_set_gradient(shape, grad);

    apply_transform(shape, transform);
    tvg_push_draw_remove_clipped(impl, shape);
}

// ============================================================================
// Clipping
// ============================================================================

// ThorVG doesn't have a clip stack. We implement clipping by applying alpha masks
// to each shape at draw time. For rdt_push_clip / rdt_pop_clip, we store the
// clip path and apply it when shapes are drawn.
//
// For this ThorVG backend, push_clip/pop_clip use a simple approach:
// save the clip path, and rdt_fill_*/rdt_stroke_* check for active clips.
// However, since the current usage pattern is: push_clip → draw → pop_clip
// (always bracketed tightly), and ThorVG applies masks per-shape,
// we implement this by storing the clip state and applying it in the
// push_draw_remove helper.

// For simplicity and correctness, we use the same approach as the existing
// render code: create a mask shape for each drawn shape and call
// tvg_paint_set_mask_method.

// But this means we need to thread the clip through all draw calls.
// A cleaner approach for this backend: since clips are always bracketed,
// store the active clip path(s) in the impl and apply in tvg_push_draw_remove.

// Active clip state
#define RDT_MAX_CLIP_DEPTH 8

struct ClipEntry {
    RdtPath* path;
    RdtMatrix transform;
    bool has_transform;
};

// We extend RdtVectorImpl with clip state via a parallel static array
// (avoiding modifying the struct definition visible to other code)
static thread_local ClipEntry s_clip_stack[RDT_MAX_CLIP_DEPTH];
static thread_local int s_clip_depth = 0;

void rdt_push_clip(RdtVector* vec, RdtPath* clip_path, const RdtMatrix* transform) {
    if (!vec || !vec->impl || !clip_path) return;
    if (s_clip_depth >= RDT_MAX_CLIP_DEPTH) {
        log_error("rdt_push_clip: clip stack overflow (depth %d)", s_clip_depth);
        return;
    }

    // copy the path for the duration of the clip
    RdtPath* copy = (RdtPath*)mem_calloc(1, sizeof(RdtPath), MEM_CAT_RENDER);
    if (clip_path->count > 0) {
        copy->entries = (RdtPath::Entry*)mem_alloc(clip_path->count * sizeof(RdtPath::Entry), MEM_CAT_RENDER);
        memcpy(copy->entries, clip_path->entries, clip_path->count * sizeof(RdtPath::Entry));
        copy->count = clip_path->count;
        copy->capacity = clip_path->count;
    }

    ClipEntry* entry = &s_clip_stack[s_clip_depth++];
    entry->path = copy;
    entry->has_transform = (transform != nullptr);
    if (transform) entry->transform = *transform;
}

void rdt_pop_clip(RdtVector* vec) {
    if (!vec || !vec->impl) return;
    if (s_clip_depth <= 0) {
        log_error("rdt_pop_clip: clip stack underflow");
        return;
    }
    s_clip_depth--;
    ClipEntry* entry = &s_clip_stack[s_clip_depth];
    rdt_path_free(entry->path);
    entry->path = nullptr;
}

int rdt_clip_save_depth() {
    int saved = s_clip_depth;
    s_clip_depth = 0;
    return saved;
}

void rdt_clip_restore_depth(int saved_depth) {
    s_clip_depth = saved_depth;
}

// Apply active clip masks to a shape (called before tvg_push_draw_remove)
static void apply_clip_masks(Tvg_Paint shape) {
    for (int i = 0; i < s_clip_depth; i++) {
        ClipEntry* entry = &s_clip_stack[i];
        if (!entry->path) continue;
        Tvg_Paint mask = create_clip_mask(entry->path,
            entry->has_transform ? &entry->transform : nullptr);
        tvg_paint_set_mask_method(shape, mask, TVG_MASK_METHOD_ALPHA);
    }
}

// Clip-aware version of tvg_push_draw_remove
static void tvg_push_draw_remove_clipped(RdtVectorImpl* impl, Tvg_Paint shape) {
    if (s_clip_depth > 0) {
        apply_clip_masks(shape);
    }
    tvg_push_draw_remove(impl, shape);
}

// ============================================================================
// Image drawing
// ============================================================================

void rdt_draw_image(RdtVector* vec, const uint32_t* pixels, int src_w, int src_h,
                    int src_stride, float dst_x, float dst_y, float dst_w, float dst_h,
                    uint8_t opacity, const RdtMatrix* transform) {
    if (!vec || !vec->impl || !pixels) return;
    RdtVectorImpl* impl = vec->impl;

    Tvg_Paint pic = tvg_picture_new();
    if (!pic) return;

    if (tvg_picture_load_raw(pic, (uint32_t*)pixels, src_w, src_h,
        TVG_COLORSPACE_ABGR8888, false) != TVG_RESULT_SUCCESS) {
        log_debug("rdt_draw_image: tvg_picture_load_raw failed");
        tvg_paint_unref(pic, true);
        return;
    }

    tvg_picture_set_size(pic, dst_w, dst_h);
    tvg_paint_translate(pic, dst_x, dst_y);

    if (opacity < 255) {
        tvg_paint_set_opacity(pic, opacity);
    }

    apply_transform(pic, transform);
    tvg_push_draw_remove_clipped(impl, pic);
}

// ============================================================================
// Picture (SVG / vector image files)
// ============================================================================

RdtPicture* rdt_picture_load(const char* path) {
    if (!path) return nullptr;

    Tvg_Paint pic = tvg_picture_new();
    if (!pic) return nullptr;

    if (tvg_picture_load(pic, path) != TVG_RESULT_SUCCESS) {
        log_error("rdt_picture_load: failed to load %s", path);
        tvg_paint_unref(pic, true);
        return nullptr;
    }

    float w = 0, h = 0;
    tvg_picture_get_size(pic, &w, &h);

    RdtPicture* p = (RdtPicture*)mem_calloc(1, sizeof(RdtPicture), MEM_CAT_RENDER);
    p->paint = pic;
    p->width = w;
    p->height = h;
    return p;
}

RdtPicture* rdt_picture_load_data(const char* data, int size, const char* mime_type) {
    if (!data || size <= 0) return nullptr;

    Tvg_Paint pic = tvg_picture_new();
    if (!pic) return nullptr;

    if (tvg_picture_load_data(pic, data, size, mime_type, NULL, false) != TVG_RESULT_SUCCESS) {
        log_error("rdt_picture_load_data: failed to decode");
        tvg_paint_unref(pic, true);
        return nullptr;
    }

    float w = 0, h = 0;
    tvg_picture_get_size(pic, &w, &h);

    RdtPicture* p = (RdtPicture*)mem_calloc(1, sizeof(RdtPicture), MEM_CAT_RENDER);
    p->paint = pic;
    p->width = w;
    p->height = h;
    return p;
}

RdtPicture* rdt_picture_dup(RdtPicture* pic) {
    if (!pic || !pic->paint) return nullptr;
    Tvg_Paint dup = tvg_paint_duplicate(pic->paint);
    if (!dup) return nullptr;
    RdtPicture* p = (RdtPicture*)mem_calloc(1, sizeof(RdtPicture), MEM_CAT_RENDER);
    p->paint = dup;
    p->width = pic->width;
    p->height = pic->height;
    return p;
}

void rdt_picture_get_size(RdtPicture* pic, float* w, float* h) {
    if (!pic) { if (w) *w = 0; if (h) *h = 0; return; }
    if (w) *w = pic->width;
    if (h) *h = pic->height;
}

void rdt_picture_set_size(RdtPicture* pic, float w, float h) {
    if (!pic) return;
    pic->width = w;
    pic->height = h;
}

void rdt_picture_draw(RdtVector* vec, RdtPicture* pic,
                      uint8_t opacity, const RdtMatrix* transform) {
    if (!vec || !vec->impl || !pic || !pic->paint) return;
    RdtVectorImpl* impl = vec->impl;

    if (pic->width > 0 && pic->height > 0) {
        tvg_picture_set_size(pic->paint, pic->width, pic->height);
    }

    if (opacity < 255) {
        tvg_paint_set_opacity(pic->paint, opacity);
    }

    apply_transform(pic->paint, transform);

    // For pictures, we need to push fresh since tvg_push_draw_remove
    // will remove and destroy the paint. Clone or re-load would be needed
    // for repeated draws. For now, one-shot semantics.
    tvg_push_draw_remove_clipped(impl, pic->paint);
    pic->paint = nullptr; // consumed by canvas remove
}

void rdt_picture_free(RdtPicture* pic) {
    if (!pic) return;
    if (pic->paint) tvg_paint_unref(pic->paint, true);
    mem_free(pic);
}

// ============================================================================
// Bridge: wrap ThorVG paint as RdtPicture (used by render_svg_inline.cpp)
// ============================================================================

RdtPicture* rdt_picture_take_tvg_paint(Tvg_Paint paint, float w, float h) {
    if (!paint) return nullptr;
    RdtPicture* pic = (RdtPicture*)mem_calloc(1, sizeof(RdtPicture), MEM_CAT_RENDER);
    pic->paint = paint;
    pic->width = w;
    pic->height = h;
    return pic;
}

bool rdt_picture_get_transform(RdtPicture* pic, RdtMatrix* out) {
    if (!pic || !pic->paint || !out) return false;
    Tvg_Matrix m;
    if (tvg_paint_get_transform(pic->paint, &m) != TVG_RESULT_SUCCESS) return false;
    out->e11 = m.e11; out->e12 = m.e12; out->e13 = m.e13;
    out->e21 = m.e21; out->e22 = m.e22; out->e23 = m.e23;
    out->e31 = m.e31; out->e32 = m.e32; out->e33 = m.e33;
    return true;
}

void rdt_picture_set_transform(RdtPicture* pic, const RdtMatrix* m) {
    if (!pic || !pic->paint || !m) return;
    Tvg_Matrix tm;
    tm.e11 = m->e11; tm.e12 = m->e12; tm.e13 = m->e13;
    tm.e21 = m->e21; tm.e22 = m->e22; tm.e23 = m->e23;
    tm.e31 = m->e31; tm.e32 = m->e32; tm.e33 = m->e33;
    tvg_paint_set_transform(pic->paint, &tm);
}

// ============================================================================
// Engine lifecycle
// ============================================================================

void rdt_engine_init(int threads) {
    tvg_engine_init(threads);
}

void rdt_engine_term(void) {
    tvg_engine_term();
}

void rdt_font_load(const char* font_path) {
    if (font_path) {
        tvg_font_load(font_path);
    }
}
