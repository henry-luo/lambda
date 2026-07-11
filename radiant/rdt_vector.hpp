#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "../lambda/input/css/dom_node.hpp"  // Color
#include "../lib/math_utils.h"

// ============================================================================
// RdtVector — Radiant's Immediate-Mode Vector Rendering API
// ============================================================================
//
// All Radiant rendering code calls rdt_* functions — never tvg_* directly.
// ThorVG is isolated behind this API in rdt_vector_tvg.cpp and render_svg_inline.cpp.
//
// Current backend: rdt_vector_tvg.cpp (ThorVG C API wrapper, all platforms)
// Future backends: Core Graphics (macOS), Direct2D (Windows), extracted sw_engine (Linux)

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

// 3x3 affine transform matrix (same layout as Tvg_Matrix)
typedef struct {
    float e11, e12, e13;   // row 1: scale-x, shear-x, translate-x
    float e21, e22, e23;   // row 2: shear-y, scale-y, translate-y
    float e31, e32, e33;   // row 3: 0, 0, 1
} RdtMatrix;

typedef enum {
    RDT_CAP_BUTT   = 0,
    RDT_CAP_ROUND  = 1,
    RDT_CAP_SQUARE = 2,
} RdtStrokeCap;

typedef enum {
    RDT_JOIN_MITER = 0,
    RDT_JOIN_ROUND = 1,
    RDT_JOIN_BEVEL = 2,
} RdtStrokeJoin;

typedef enum {
    RDT_FILL_WINDING  = 0,
    RDT_FILL_EVEN_ODD = 1,
} RdtFillRule;

typedef enum {
    RDT_PATH_MOVE,
    RDT_PATH_LINE,
    RDT_PATH_QUAD,
    RDT_PATH_CUBIC,
    RDT_PATH_CLOSE,
    RDT_PATH_RECT,
    RDT_PATH_CIRCLE,
} RdtPathCommand;

typedef bool (*RdtPathVisitFn)(void* context, RdtPathCommand command,
                               const float* args, int arg_count);

typedef struct {
    float offset;          // 0.0 – 1.0
    uint8_t r, g, b, a;
} RdtGradientStop;

// opaque path handle — each backend defines the concrete type
typedef struct RdtPath RdtPath;

// opaque backend handle
typedef struct RdtVectorImpl RdtVectorImpl;

typedef struct RdtVector {
    RdtVectorImpl* impl;
} RdtVector;

typedef struct RdtVectorTarget {
    uint32_t* pixels;
    int width;
    int height;
    int stride;
    float tile_offset_x;
    float tile_offset_y;
} RdtVectorTarget;

typedef enum {
    RDT_VECTOR_BACKEND_UNKNOWN = 0,
    RDT_VECTOR_BACKEND_THORVG,
    RDT_VECTOR_BACKEND_CORE_GRAPHICS,
} RdtVectorBackend;

typedef struct RdtVectorCaps {
    RdtVectorBackend backend;
    const char* backend_name;
    bool vector_paths;
    bool rounded_rects;
    bool gradients;
    bool nested_clips;
    bool image_scaling;
    bool picture_svg;
    bool picture_duplication;
    bool svg_dom_pictures;
    bool opacity_group;
    bool blend_modes;
    bool gaussian_blur;
    bool color_matrix_filters;
    bool native_text_runs;
    bool vector_batching;
    bool premultiplied_surface;
    bool tile_offsets;
    bool clip_depth_save_restore;
} RdtVectorCaps;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// create a vector context bound to a caller-owned pixel buffer
// pixels: ABGR8888 format, stride in pixels (not bytes)
void rdt_vector_init(RdtVector* vec, uint32_t* pixels, int w, int h, int stride);
void rdt_vector_destroy(RdtVector* vec);

// re-bind to a (possibly different) pixel buffer of the same size
void rdt_vector_set_target(RdtVector* vec, uint32_t* pixels, int w, int h, int stride);

// set a Y-pixel offset for tiled rendering (0 = normal full-page mode)
// all subsequent draw calls are translated upward by offset_y physical pixels
void rdt_vector_set_tile_offset_y(RdtVector* vec, float offset_y);
void rdt_vector_set_tile_offset_x(RdtVector* vec, float offset_x);

// Return immutable capability metadata for the active vector backend.
const RdtVectorCaps* rdt_vector_get_caps(const RdtVector* vec);

// Return the currently bound pixel target. Used by record/replay bridges that
// need an ImageSurface wrapper around the backend-owned target.
bool rdt_vector_get_target(const RdtVector* vec, RdtVectorTarget* out);

// Batch consecutive vector paints into one backend submission when supported.
// Software/raster callers should flush before reading or directly mutating the
// target pixels.
void rdt_vector_begin_batch(RdtVector* vec);
void rdt_vector_flush_batch(RdtVector* vec);
void rdt_vector_end_batch(RdtVector* vec);

// ---------------------------------------------------------------------------
// Path construction
// ---------------------------------------------------------------------------

RdtPath* rdt_path_new(void);
void     rdt_path_move_to(RdtPath* p, float x, float y);
void     rdt_path_line_to(RdtPath* p, float x, float y);
void     rdt_path_cubic_to(RdtPath* p, float cx1, float cy1,
                           float cx2, float cy2, float x, float y);
void     rdt_path_close(RdtPath* p);
void     rdt_path_add_rect(RdtPath* p, float x, float y, float w, float h,
                           float rx, float ry);
void     rdt_path_add_circle(RdtPath* p, float cx, float cy, float rx, float ry);
void     rdt_path_free(RdtPath* p);

// Deep-copy a path (entries array is duplicated).
RdtPath* rdt_path_clone(const RdtPath* src);

// Compute a conservative axis-aligned path bound in path-local coordinates.
// Returns false when the path has no drawable geometry.
bool rdt_path_get_bounds(const RdtPath* p, float* left, float* top,
                         float* right, float* bottom);

// Visit a path's portable command stream. Returns false when the path cannot
// be inspected by the active backend or when the callback aborts traversal.
bool rdt_path_visit(const RdtPath* p, RdtPathVisitFn fn, void* context);

// ---------------------------------------------------------------------------
// Fill
// ---------------------------------------------------------------------------

void rdt_fill_path(RdtVector* vec, RdtPath* p, Color color,
                   RdtFillRule rule, const RdtMatrix* transform);

// convenience: axis-aligned filled rectangle (no path allocation)
void rdt_fill_rect(RdtVector* vec, float x, float y, float w, float h,
                   Color color);

// convenience: axis-aligned filled rounded rectangle
void rdt_fill_rounded_rect(RdtVector* vec, float x, float y, float w, float h,
                           float rx, float ry, Color color);

// ---------------------------------------------------------------------------
// Stroke
// ---------------------------------------------------------------------------

void rdt_stroke_path(RdtVector* vec, RdtPath* p, Color color, float width,
                     RdtStrokeCap cap, RdtStrokeJoin join,
                     const float* dash_array, int dash_count, float dash_phase,
                     const RdtMatrix* transform);

// ---------------------------------------------------------------------------
// Gradient fill
// ---------------------------------------------------------------------------

void rdt_fill_linear_gradient(RdtVector* vec, RdtPath* p,
                              float x1, float y1, float x2, float y2,
                              const RdtGradientStop* stops, int stop_count,
                              RdtFillRule rule,
                              const RdtMatrix* transform,
                              const RdtMatrix* gradient_transform);

void rdt_fill_radial_gradient(RdtVector* vec, RdtPath* p,
                              float cx, float cy, float r,
                              const RdtGradientStop* stops, int stop_count,
                              RdtFillRule rule,
                              const RdtMatrix* transform,
                              const RdtMatrix* gradient_transform);

// ---------------------------------------------------------------------------
// Clipping (alpha mask)
// ---------------------------------------------------------------------------

// clip_path filled black(255 alpha) = visible region; outside = clipped.
// clips nest (push multiple, pop in reverse).
void rdt_push_clip(RdtVector* vec, RdtPath* clip_path, const RdtMatrix* transform);
void rdt_pop_clip(RdtVector* vec);

// Save and restore clip stack depth for isolated rendering contexts.
// rdt_clip_save_depth returns the current depth and resets to 0.
// rdt_clip_restore_depth restores a previously saved depth.
int rdt_clip_save_depth();
void rdt_clip_restore_depth(int saved_depth);

// ---------------------------------------------------------------------------
// Image drawing (replaces tvg_picture_load_raw + push + draw)
// ---------------------------------------------------------------------------

// blit a caller-owned ABGR8888 pixel buffer into the target surface
// with optional transform, clipping, and opacity
void rdt_draw_image(RdtVector* vec, const uint32_t* pixels, int src_w, int src_h,
                    int src_stride, float dst_x, float dst_y, float dst_w, float dst_h,
                    uint8_t opacity, const RdtMatrix* transform,
                    uint64_t resource_generation = 0);

// ---------------------------------------------------------------------------
// SVG picture (load from file/data, render at given rect)
// ---------------------------------------------------------------------------

typedef struct RdtPicture RdtPicture;
struct Element;
struct Pool;

RdtPicture* rdt_picture_load(const char* path);
RdtPicture* rdt_picture_load_data(const char* data, int size, const char* mime_type);
RdtPicture* rdt_picture_dup(RdtPicture* pic);
Element*    rdt_picture_get_svg_root(RdtPicture* pic);
Element*    rdt_picture_find_svg_element_by_id(RdtPicture* pic, const char* id);
Pool*       rdt_picture_get_pool(RdtPicture* pic);
const char* rdt_picture_get_source_path(RdtPicture* pic);
void        rdt_picture_get_size(RdtPicture* pic, float* w, float* h);
void        rdt_picture_set_size(RdtPicture* pic, float w, float h);
bool        rdt_picture_get_transform(RdtPicture* pic, RdtMatrix* out);
void        rdt_picture_set_transform(RdtPicture* pic, const RdtMatrix* m);
void        rdt_picture_draw(RdtVector* vec, RdtPicture* pic,
                             uint8_t opacity, const RdtMatrix* transform);
void        rdt_picture_draw_dup(RdtVector* vec, RdtPicture* pic,
                                 uint8_t opacity, const RdtMatrix* transform);
void        rdt_picture_free(RdtPicture* pic);

// ---------------------------------------------------------------------------
// Engine lifecycle (must call init before any rdt_* operations)
// ---------------------------------------------------------------------------

void rdt_engine_init(int threads);
void rdt_engine_term(void);
void rdt_font_load(const char* font_path);

// Set the default FontContext used for SVG text rendering inside pictures
// loaded from file/data via rdt_picture_load*.  Must be called once after
// the application's font context is created (typically from ui_context_init).
struct FontContext;
void rdt_set_font_context(struct FontContext* ctx);

// ---------------------------------------------------------------------------
// Internal bridge for render_svg_inline.cpp (ThorVG text/image → RdtPicture)
// ---------------------------------------------------------------------------

#ifndef LAMBDA_HEADLESS
#include <thorvg_capi.h>

// Wrap an existing Tvg_Paint, taking ownership (no duplicate).
// The caller must NOT use or free the paint after this call.
// Used only by render_svg_inline.cpp for text/image paint bridging.
RdtPicture* rdt_picture_take_tvg_paint(Tvg_Paint paint, float w, float h);

#endif

static inline RdtMatrix rdt_matrix_identity(void) {
    RdtMatrix m = { 1, 0, 0,  0, 1, 0,  0, 0, 1 };
    return m;
}

// multiply two 3x3 affine matrices: result = a * b
static inline RdtMatrix rdt_matrix_multiply(const RdtMatrix* a, const RdtMatrix* b) {
    RdtMatrix r;
    r.e11 = a->e11 * b->e11 + a->e12 * b->e21 + a->e13 * b->e31;
    r.e12 = a->e11 * b->e12 + a->e12 * b->e22 + a->e13 * b->e32;
    r.e13 = a->e11 * b->e13 + a->e12 * b->e23 + a->e13 * b->e33;
    r.e21 = a->e21 * b->e11 + a->e22 * b->e21 + a->e23 * b->e31;
    r.e22 = a->e21 * b->e12 + a->e22 * b->e22 + a->e23 * b->e32;
    r.e23 = a->e21 * b->e13 + a->e22 * b->e23 + a->e23 * b->e33;
    r.e31 = a->e31 * b->e11 + a->e32 * b->e21 + a->e33 * b->e31;
    r.e32 = a->e31 * b->e12 + a->e32 * b->e22 + a->e33 * b->e32;
    r.e33 = a->e31 * b->e13 + a->e32 * b->e23 + a->e33 * b->e33;
    return r;
}

static inline void rdt_matrix_transform_point(const RdtMatrix* m,
                                              float x, float y,
                                              float* out_x, float* out_y) {
    if (!m || !out_x || !out_y) return;
    *out_x = m->e11 * x + m->e12 * y + m->e13;
    *out_y = m->e21 * x + m->e22 * y + m->e23;
}

static inline void rdt_matrix_transform_rect_bounds(const RdtMatrix* m,
                                                    float left, float top,
                                                    float right, float bottom,
                                                    float* out_left,
                                                    float* out_top,
                                                    float* out_right,
                                                    float* out_bottom) {
    if (!m || !out_left || !out_top || !out_right || !out_bottom) return;

    float tx0, ty0, tx1, ty1, tx2, ty2, tx3, ty3;
    rdt_matrix_transform_point(m, left, top, &tx0, &ty0);
    rdt_matrix_transform_point(m, right, top, &tx1, &ty1);
    rdt_matrix_transform_point(m, right, bottom, &tx2, &ty2);
    rdt_matrix_transform_point(m, left, bottom, &tx3, &ty3);

    *out_left = LMB_MIN(LMB_MIN(tx0, tx1), LMB_MIN(tx2, tx3));
    *out_right = LMB_MAX(LMB_MAX(tx0, tx1), LMB_MAX(tx2, tx3));
    *out_top = LMB_MIN(LMB_MIN(ty0, ty1), LMB_MIN(ty2, ty3));
    *out_bottom = LMB_MAX(LMB_MAX(ty0, ty1), LMB_MAX(ty2, ty3));
}

// create a translation matrix
static inline RdtMatrix rdt_matrix_translate(float tx, float ty) {
    RdtMatrix m = { 1, 0, tx,  0, 1, ty,  0, 0, 1 };
    return m;
}
