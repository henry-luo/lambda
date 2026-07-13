#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>
#include <math.h>
#include <string.h>
#include <thorvg_capi.h>
#include "view.hpp"
#include "state_store.hpp"
#include "clip_shape.h"
#include "animation.h"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lib/arena.h"
#include "../lib/arraylist.h"
#include "../lib/font/font.h"
#include "../lib/hashmap.h"
#include "../lib/image.h"
#include "../lib/log.h"
#include "../lib/math_utils.h"
#include "../lib/mem_factory.h"
#include "../lib/mempool.h"
#include "../lib/ownership.hpp"
#include "../lib/scratch_arena.h"
#include "../lib/str.h"
#include "../lib/strbuf.h"

// consolidated Radiant render API (DD4); declarations below retain their source-file section names for history lookup.

// ===== rdt_vector.hpp =====
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

// ===== display_list.h =====
// ==========================================================================
// DisplayList — Serialised draw-command buffer for Radiant's rendering pipeline.
//
// Phase 1 of the multi-threaded rendering proposal.
// Decouples the recording pass (main thread walks the view tree) from the
// rasterisation pass (replay through rdt_* calls, eventually per-tile).
// ==========================================================================


#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Display list op codes
// ---------------------------------------------------------------------------

#define DISPLAY_OP_LIST(X) \
    X(DL_FILL_RECT, DL_OP_FLAG_DIRTY_SKIPPABLE | DL_OP_FLAG_REPLAY_NOOP_AFTER_VECTOR) \
    X(DL_FILL_ROUNDED_RECT, DL_OP_FLAG_DIRTY_SKIPPABLE | DL_OP_FLAG_REPLAY_NOOP_AFTER_VECTOR) \
    X(DL_FILL_PATH, DL_OP_FLAG_DIRTY_SKIPPABLE | DL_OP_FLAG_REPLAY_NOOP_AFTER_VECTOR) \
    X(DL_STROKE_PATH, DL_OP_FLAG_DIRTY_SKIPPABLE | DL_OP_FLAG_REPLAY_NOOP_AFTER_VECTOR) \
    X(DL_FILL_LINEAR_GRADIENT, DL_OP_FLAG_DIRTY_SKIPPABLE | DL_OP_FLAG_REPLAY_NOOP_AFTER_VECTOR) \
    X(DL_FILL_RADIAL_GRADIENT, DL_OP_FLAG_DIRTY_SKIPPABLE | DL_OP_FLAG_REPLAY_NOOP_AFTER_VECTOR) \
    X(DL_DRAW_IMAGE, DL_OP_FLAG_DIRTY_SKIPPABLE | DL_OP_FLAG_REPLAY_NOOP_AFTER_VECTOR) \
    X(DL_DRAW_GLYPH, DL_OP_FLAG_DIRTY_SKIPPABLE | DL_OP_FLAG_FLUSHES_VECTOR_BATCH) \
    X(DL_DRAW_PICTURE, DL_OP_FLAG_DIRTY_SKIPPABLE | DL_OP_FLAG_REPLAY_NOOP_AFTER_VECTOR) \
    X(DL_PUSH_CLIP, DL_OP_FLAG_PRESERVES_REPLAY_STATE | DL_OP_FLAG_REPLAY_NOOP_AFTER_VECTOR) \
    X(DL_POP_CLIP, DL_OP_FLAG_PRESERVES_REPLAY_STATE | DL_OP_FLAG_REPLAY_NOOP_AFTER_VECTOR) \
    X(DL_FILL_SURFACE_RECT, DL_OP_FLAG_DIRTY_SKIPPABLE | DL_OP_FLAG_FLUSHES_VECTOR_BATCH) \
    X(DL_BLIT_SURFACE_SCALED, DL_OP_FLAG_DIRTY_SKIPPABLE | DL_OP_FLAG_FLUSHES_VECTOR_BATCH) \
    X(DL_COMPOSITE_OPACITY, DL_OP_FLAG_PRESERVES_REPLAY_STATE | DL_OP_FLAG_FLUSHES_VECTOR_BATCH) \
    X(DL_SAVE_BACKDROP, DL_OP_FLAG_PRESERVES_REPLAY_STATE | DL_OP_FLAG_FLUSHES_VECTOR_BATCH) \
    X(DL_APPLY_BLEND_MODE, DL_OP_FLAG_PRESERVES_REPLAY_STATE | DL_OP_FLAG_FLUSHES_VECTOR_BATCH) \
    X(DL_APPLY_FILTER, DL_OP_FLAG_DIRTY_SKIPPABLE | DL_OP_FLAG_FLUSHES_VECTOR_BATCH) \
    X(DL_VIDEO_PLACEHOLDER, DL_OP_FLAG_DIRTY_SKIPPABLE | DL_OP_FLAG_REPLAY_NOOP_AFTER_VECTOR) \
    X(DL_WEBVIEW_LAYER_PLACEHOLDER, DL_OP_FLAG_DIRTY_SKIPPABLE | DL_OP_FLAG_FLUSHES_VECTOR_BATCH) \
    X(DL_BOX_BLUR_REGION, DL_OP_FLAG_DIRTY_SKIPPABLE | DL_OP_FLAG_FLUSHES_VECTOR_BATCH) \
    X(DL_BOX_BLUR_INSET, DL_OP_FLAG_DIRTY_SKIPPABLE | DL_OP_FLAG_FLUSHES_VECTOR_BATCH) \
    X(DL_SHADOW_CLIP_SAVE, DL_OP_FLAG_PRESERVES_REPLAY_STATE | DL_OP_FLAG_FLUSHES_VECTOR_BATCH) \
    X(DL_SHADOW_CLIP_RESTORE, DL_OP_FLAG_PRESERVES_REPLAY_STATE | DL_OP_FLAG_FLUSHES_VECTOR_BATCH) \
    X(DL_OUTER_SHADOW, DL_OP_FLAG_DIRTY_SKIPPABLE | DL_OP_FLAG_FLUSHES_VECTOR_BATCH) \
    X(DL_BEGIN_ELEMENT, DL_OP_FLAG_PRESERVES_REPLAY_STATE | DL_OP_FLAG_REPLAY_NOOP_AFTER_VECTOR) \
    X(DL_END_ELEMENT, DL_OP_FLAG_PRESERVES_REPLAY_STATE | DL_OP_FLAG_REPLAY_NOOP_AFTER_VECTOR)

enum {
    DL_OP_FLAG_PRESERVES_REPLAY_STATE = 1u << 0,
    DL_OP_FLAG_DIRTY_SKIPPABLE = 1u << 1,
    DL_OP_FLAG_REPLAY_NOOP_AFTER_VECTOR = 1u << 2,
    DL_OP_FLAG_FLUSHES_VECTOR_BATCH = 1u << 3,
};

typedef enum {
#define DL_OP_ENUM(name, flags) name,
    DISPLAY_OP_LIST(DL_OP_ENUM)
#undef DL_OP_ENUM
} DisplayOp;

// ---------------------------------------------------------------------------
// Per-op payload structures
// ---------------------------------------------------------------------------

typedef struct {
    float x, y, w, h;
    Color color;
} DlFillRect;

typedef struct {
    float x, y, w, h;
    float rx, ry;
    Color color;
} DlFillRoundedRect;

typedef struct {
    RdtPath* path;       // cloned path, owned by display list
    Color color;
    RdtFillRule rule;
    bool has_transform;
    RdtMatrix transform;
} DlFillPath;

typedef struct {
    RdtPath* path;       // cloned path, owned by display list
    Color color;
    float width;
    RdtStrokeCap cap;
    RdtStrokeJoin join;
    float* dash_array;   // arena-allocated copy, NULL if no dashes
    int dash_count;
    float dash_phase;
    bool has_transform;
    RdtMatrix transform;
} DlStrokePath;

typedef struct {
    RdtPath* path;       // cloned path, owned by display list
    float x1, y1, x2, y2;
    RdtGradientStop* stops;  // arena-allocated copy
    int stop_count;
    RdtFillRule rule;
    bool has_transform;
    RdtMatrix transform;
    bool has_gradient_transform;
    RdtMatrix gradient_transform;
} DlFillLinearGradient;

typedef struct {
    RdtPath* path;       // cloned path, owned by display list
    float cx, cy, r;
    RdtGradientStop* stops;  // arena-allocated copy
    int stop_count;
    RdtFillRule rule;
    bool has_transform;
    RdtMatrix transform;
    bool has_gradient_transform;
    RdtMatrix gradient_transform;
} DlFillRadialGradient;

typedef struct {
    void* resource_owner;    // optional ImageSurface* owner for generation checks
    uint64_t resource_generation;
    const uint32_t* pixels;  // borrowed — image lifetime must exceed display list
    int src_w, src_h, src_stride;
    float dst_x, dst_y, dst_w, dst_h;
    uint8_t opacity;
    bool has_transform;
    RdtMatrix transform;
} DlDrawImage;

typedef struct {
    uint64_t resource_generation; // 0 means not safe for retained reuse
    GlyphBitmap bitmap;      // copy of bitmap descriptor (buffer pointer borrowed)
    int x, y;                // destination pixel position
    Color color;             // text color at recording time
    bool is_color_emoji;     // BGRA color emoji (no tint)
    bool has_transform;
    RdtMatrix transform;
    Bound clip;              // rectangular clip bounds at recording time
} DlDrawGlyph;

typedef struct {
    RdtPicture* picture;     // owned — display list frees on clear
    uint8_t opacity;
    bool has_transform;
    RdtMatrix transform;
} DlDrawPicture;

typedef struct {
    RdtPath* path;       // cloned path, owned by display list
    bool has_transform;
    RdtMatrix transform;
} DlPushClip;

typedef struct {
    int depth;
    int type[RDT_MAX_CLIP_SHAPES];
    float params[RDT_MAX_CLIP_SHAPES][8];
    int polygon_count[RDT_MAX_CLIP_SHAPES];
    float* polygon_vx[RDT_MAX_CLIP_SHAPES];
    float* polygon_vy[RDT_MAX_CLIP_SHAPES];
} DlClipShapeStack;

// Direct-pixel fill (selection highlights, surface clear, etc.)
typedef struct {
    float x, y, w, h;
    uint32_t color;          // ABGR8888
    Bound clip;              // rectangular clip bounds at recording time
    DlClipShapeStack clip_shapes;
} DlFillSurfaceRect;

// Direct-pixel scaled blit (raster images via blit_surface_scaled)
typedef struct {
    void* src_surface;       // ImageSurface* — borrowed
    uint64_t src_generation;
    float dst_x, dst_y, dst_w, dst_h;
    int scale_mode;
    uint8_t opacity;
    Bound clip;              // rectangular clip bounds at recording time
    DlClipShapeStack clip_shapes;
} DlBlitSurfaceScaled;

// CSS opacity stacking context: composite element group over saved backdrop at opacity.
// Uses DL_SAVE_BACKDROP (pushed earlier) for the backdrop pixels.
typedef struct {
    int x0, y0, w, h;       // physical pixel region
    float opacity;
    bool premultiplied_source;
} DlCompositeOpacity;

// Save backdrop pixels before element with mix-blend-mode renders.
// During replay, the replay function saves a copy and clears the region.
typedef struct {
    int x0, y0, w, h;       // physical pixel region
} DlSaveBackdrop;

// Post-processing: composite with saved backdrop
typedef struct {
    int x0, y0, w, h;       // physical pixel region
    int blend_mode;          // CssEnum
} DlApplyBlendMode;

// Post-processing: apply CSS filter chain
typedef struct {
    float x, y, w, h;
    void* filter;            // FilterProp* — borrowed
    Bound clip;              // rectangular clip bounds at recording time
} DlApplyFilter;

// Box blur on a pixel region (for box-shadow blur deferred to replay)
typedef struct {
    int rx, ry, rw, rh;      // pixel region to blur
    float blur_radius;        // CSS blur radius in pixels
    bool premultiply_source;  // convert straight alpha source pixels before blur
    bool tint_source;         // recolor isolated source from alpha before blur
    Color tint_color;
    int clip_type;            // ClipShapeType (0 = none, clips blur to CSS clip-path)
    float clip_params[8];    // serialized clip shape parameters
    int exclude_type;         // ClipShapeType for element border-box exclusion (outer box-shadow)
    float exclude_params[8]; // serialized exclude shape: restore pixels INSIDE this shape after blur
} DlBoxBlurRegion;

// Inset box-shadow blur: blur expanded region, restore pixels outside inner rect
typedef struct {
    int rx, ry, rw, rh;      // inner write region (element rect)
    int pad;                  // read padding (blur extends this far beyond inner rect)
    float blur_radius;        // CSS blur radius in pixels
    uint32_t bg_color;        // element background color (surface pixel format)
} DlBoxBlurInset;

// Outer box-shadow clip save: saves a rectangular pixel region BEFORE the shadow
// fill so that pixels inside the element border-box can be restored after blur.
// Per CSS spec, outer box-shadow is never visible inside the border-box.
typedef struct {
    int rx, ry, rw, rh;      // pixel region to save (element border-box)
} DlShadowClipSave;

// Box-shadow clip restore: restores pixels from the preceding DL_SHADOW_CLIP_SAVE.
// For outer shadows (restore_inside=1): restores pixels INSIDE the shape (element border-box).
// For inset shadows (restore_inside=0): restores pixels OUTSIDE the shape (rounded corners).
typedef struct {
    int exclude_type;         // ClipShapeType for element border-box
    float exclude_params[8]; // serialized shape parameters
    int save_rx, save_ry, save_rw, save_rh;  // must match the save region
    int restore_inside;       // 1 = restore inside shape (outer shadow), 0 = restore outside (inset)
} DlShadowClipRestore;

// Self-contained outer box-shadow op: at replay, rasterise the shadow rounded
// rect into a private temp buffer, apply 3-pass box blur to that buffer, then
// composite the result over the surface (skipping pixels inside exclude shape).
// Replaces the SAVE+FILL+BLUR_REGION+RESTORE sequence — avoids smearing
// neighbouring sibling element pixels into surrounding areas.
typedef struct {
    float shadow_x, shadow_y, shadow_w, shadow_h;  // shadow rect in surface coords
    float sr_tl, sr_tr, sr_br, sr_bl;              // per-corner radii
    Color color;                                    // shadow colour (with alpha)
    float blur_radius;                              // CSS blur radius (physical px)
    int exclude_type;          // element border-box shape (skip composite inside)
    float exclude_params[8];
    int clip_type;             // optional CSS clip-path
    float clip_params[8];
} DlOuterShadow;

// Video frame placeholder: records the layout rect and clip for post-composite blit.
// The actual video frame pixels are blitted after tile compositing in the render loop.
typedef struct {
    void* video;             // RdtVideo* — borrowed, lifetime managed by view tree
    uint64_t video_generation;
    float dst_x, dst_y, dst_w, dst_h;  // physical pixel coordinates
    Bound clip;              // rectangular clip bounds at recording time
    int object_fit;          // CssEnum
} DlVideoPlaceholder;

// Webview layer placeholder: records the layout rect and clip for post-composite blit.
typedef struct {
    void* surface;           // ImageSurface* — borrowed, lifetime managed by WebViewProp
    uint64_t surface_generation;
    float dst_x, dst_y, dst_w, dst_h;
    Bound clip;
} DlWebviewLayerPlaceholder;

// Element group marker: records a matched display-list item range for subtree
// culling and future retained display-list reuse.
typedef struct {
    uint32_t view_id;
    int matching_index;      // begin -> end, end -> begin; -1 while open
    float marker_x, marker_y, marker_w, marker_h; // original layout marker before visual-union tightening
} DlElementMarker;

// ---------------------------------------------------------------------------
// DisplayItem — tagged union of all draw commands
// ---------------------------------------------------------------------------

typedef struct DisplayItem {
    DisplayOp op;
    float bounds[4];  // x, y, w, h for tile culling

    union {
        DlFillRect           fill_rect;
        DlFillRoundedRect    fill_rounded_rect;
        DlFillPath           fill_path;
        DlStrokePath         stroke_path;
        DlFillLinearGradient fill_linear_gradient;
        DlFillRadialGradient fill_radial_gradient;
        DlDrawImage          draw_image;
        DlDrawGlyph          draw_glyph;
        DlDrawPicture        draw_picture;
        DlPushClip           push_clip;
        DlFillSurfaceRect    fill_surface_rect;
        DlBlitSurfaceScaled  blit_surface_scaled;
        DlCompositeOpacity   composite_opacity;
        DlSaveBackdrop       save_backdrop;
        DlApplyBlendMode     apply_blend_mode;
        DlApplyFilter        apply_filter;
        DlBoxBlurRegion      box_blur_region;
        DlBoxBlurInset       box_blur_inset;
        DlShadowClipSave     shadow_clip_save;
        DlShadowClipRestore  shadow_clip_restore;
        DlOuterShadow        outer_shadow;
        DlVideoPlaceholder   video_placeholder;
        DlWebviewLayerPlaceholder webview_layer_placeholder;
        DlElementMarker      element_marker;
    };
} DisplayItem;

// ---------------------------------------------------------------------------
// DisplayList — growable array of DisplayItem
// ---------------------------------------------------------------------------

typedef struct DisplayList {
    DisplayItem* items;
    int count;
    int capacity;
    ScratchArena arena;      // all variable-length data (paths, stops, dashes)
} DisplayList;

typedef struct DisplayListValidationResult {
    bool valid;
    int first_error_index;
    const char* message;
    int clip_depth;
    int backdrop_depth;
    int shadow_clip_depth;
    int element_depth;
} DisplayListValidationResult;

typedef struct DisplayOpDescriptor {
    DisplayOp op;
    uint32_t flags;
} DisplayOpDescriptor;

const DisplayOpDescriptor* dl_op_descriptor(DisplayOp op);
bool dl_op_has_flags(DisplayOp op, uint32_t flags);
void dl_item_free_owned_payload(DisplayItem* item);

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Initialise a display list.  backing_arena is used for variable-length data
// (path copies, gradient stops, dash arrays).
void dl_init(DisplayList* dl, Arena* backing_arena);

// Reset the display list for re-recording (rewinds arena, zeroes count).
void dl_clear(DisplayList* dl);

// Free the items array (arena memory is managed by the caller).
void dl_destroy(DisplayList* dl);

// ---------------------------------------------------------------------------
// Recording API — mirrors rdt_* functions
// ---------------------------------------------------------------------------

void dl_fill_rect(DisplayList* dl, float x, float y, float w, float h, Color color);

void dl_fill_rounded_rect(DisplayList* dl, float x, float y, float w, float h,
                          float rx, float ry, Color color);

void dl_fill_path(DisplayList* dl, RdtPath* path, Color color,
                  RdtFillRule rule, const RdtMatrix* transform);

void dl_stroke_path(DisplayList* dl, RdtPath* path, Color color, float width,
                    RdtStrokeCap cap, RdtStrokeJoin join,
                    const float* dash_array, int dash_count, float dash_phase,
                    const RdtMatrix* transform);

void dl_fill_linear_gradient(DisplayList* dl, RdtPath* path,
                             float x1, float y1, float x2, float y2,
                             const RdtGradientStop* stops, int stop_count,
                             RdtFillRule rule, const RdtMatrix* transform,
                             const RdtMatrix* gradient_transform);

void dl_fill_radial_gradient(DisplayList* dl, RdtPath* path,
                             float cx, float cy, float r,
                             const RdtGradientStop* stops, int stop_count,
                             RdtFillRule rule, const RdtMatrix* transform,
                             const RdtMatrix* gradient_transform);

void dl_draw_image(DisplayList* dl, const uint32_t* pixels,
                   int src_w, int src_h, int src_stride,
                   float dst_x, float dst_y, float dst_w, float dst_h,
                   uint8_t opacity, const RdtMatrix* transform,
                   void* resource_owner = nullptr,
                   uint64_t resource_generation = 0);

// Record a glyph draw command.  bitmap buffer is borrowed (must outlive display list).
void dl_draw_glyph(DisplayList* dl, GlyphBitmap* bitmap, int x, int y,
                   Color color, bool is_color_emoji, const Bound* clip,
                   const RdtMatrix* transform = nullptr,
                   uint64_t resource_generation = 0);

void dl_draw_picture(DisplayList* dl, RdtPicture* picture,
                     uint8_t opacity, const RdtMatrix* transform);

void dl_push_clip(DisplayList* dl, RdtPath* clip_path, const RdtMatrix* transform);
void dl_pop_clip(DisplayList* dl);

// Direct-pixel operations
void dl_fill_surface_rect(DisplayList* dl, float x, float y, float w, float h,
                          uint32_t color, const Bound* clip,
                          ClipShape** clip_shapes = nullptr, int clip_depth = 0);

void dl_blit_surface_scaled(DisplayList* dl, void* src_surface,
                            float dst_x, float dst_y, float dst_w, float dst_h,
                            int scale_mode, const Bound* clip,
                            ClipShape** clip_shapes = nullptr, int clip_depth = 0,
                            uint8_t opacity = 255,
                            uint64_t src_generation = 0);

// Post-processing operations (coordinates already in physical pixels)
void dl_composite_opacity(DisplayList* dl, int x0, int y0, int w, int h,
                          float opacity, bool premultiplied_source = false);

void dl_save_backdrop(DisplayList* dl, int x0, int y0, int w, int h);

void dl_apply_blend_mode(DisplayList* dl, int x0, int y0, int w, int h,
                         int blend_mode);

void dl_apply_filter(DisplayList* dl, float x, float y, float w, float h,
                     void* filter, const Bound* clip);

void dl_box_blur_region(DisplayList* dl, int rx, int ry, int rw, int rh, float blur_radius,
                        int clip_type, const float* clip_params,
                        int exclude_type = 0, const float* exclude_params = nullptr,
                        bool premultiply_source = false,
                        bool tint_source = false, Color tint_color = Color{});

void dl_box_blur_inset(DisplayList* dl, int rx, int ry, int rw, int rh, int pad, float blur_radius, uint32_t bg_color);

void dl_shadow_clip_save(DisplayList* dl, int rx, int ry, int rw, int rh);
void dl_shadow_clip_restore(DisplayList* dl, int exclude_type, const float* exclude_params,
                            int save_rx, int save_ry, int save_rw, int save_rh,
                            int restore_inside);

// Self-contained outer box-shadow (rasterise + blur in temp buffer + composite)
void dl_outer_shadow(DisplayList* dl,
                     float shadow_x, float shadow_y, float shadow_w, float shadow_h,
                     float sr_tl, float sr_tr, float sr_br, float sr_bl,
                     Color color, float blur_radius,
                     int exclude_type, const float* exclude_params,
                     int clip_type, const float* clip_params);

// Video placeholder (rect + clip only; actual blit is post-composite)
void dl_video_placeholder(DisplayList* dl, void* video,
                          float dst_x, float dst_y, float dst_w, float dst_h,
                          int object_fit, const Bound* clip,
                          uint64_t video_generation = 0);

// Webview layer placeholder (rect + clip only; actual blit is post-composite)
void dl_webview_layer_placeholder(DisplayList* dl, void* surface,
                                  float dst_x, float dst_y, float dst_w, float dst_h,
                                  const Bound* clip,
                                  uint64_t surface_generation = 0);

// Element group markers.  dl_begin_element() returns the begin item index,
// which must be passed to dl_end_element() after the subtree has been recorded.
int dl_begin_element(DisplayList* dl, uint32_t view_id,
                     float x, float y, float w, float h);
void dl_end_element(DisplayList* dl, int begin_index);

// ---------------------------------------------------------------------------
// Debug / stats
// ---------------------------------------------------------------------------

int dl_item_count(const DisplayList* dl);
bool dl_contains_glyphs(const DisplayList* dl);
bool dl_validate(const DisplayList* dl, DisplayListValidationResult* result);
bool dl_validate_or_log(const DisplayList* dl, const char* context);
bool dl_item_is_retainable_for_fragment(const DisplayItem* item);

#ifdef __cplusplus
}
#endif

// ===== render_backend_caps.hpp =====
typedef struct FilterProp FilterProp;
typedef RdtVectorCaps RenderBackendCaps;

typedef enum RenderExportTargetKind {
    RENDER_EXPORT_TARGET_SVG,
    RENDER_EXPORT_TARGET_PDF,
} RenderExportTargetKind;

typedef struct RenderExportTargetCaps {
    RenderExportTargetKind target;
    const char* target_name;
    bool rects;
    bool rounded_rects;
    bool paths;
    bool strokes;
    bool gradients;
    bool images;
    bool glyph_runs;
    bool clips;
    bool transforms;
    bool opacity_groups;
    bool blend_modes;
    bool filters;
    bool shadows;
} RenderExportTargetCaps;

const RenderBackendCaps* render_backend_get_caps(const RdtVector* vec);
bool render_backend_supports_filter_chain(const RenderBackendCaps* caps,
                                          const FilterProp* filter);

static inline const RenderExportTargetCaps* render_export_target_get_caps(RenderExportTargetKind target) {
    static const RenderExportTargetCaps svg_caps = {
        RENDER_EXPORT_TARGET_SVG,
        "svg",
        true,   // rects
        true,   // rounded_rects
        true,   // paths
        true,   // strokes
        true,   // gradients
        true,   // images
        true,   // glyph_runs
        true,   // clips
        true,   // transforms
        true,   // opacity_groups
        false,  // blend_modes
        false,  // filters
        false,  // shadows
    };

    static const RenderExportTargetCaps pdf_caps = {
        RENDER_EXPORT_TARGET_PDF,
        "pdf",
        true,   // rects
        true,   // rounded_rects
        true,   // paths
        true,   // strokes
        true,   // gradients (raster fallback in PDF lowering)
        true,   // images
        true,   // glyph_runs
        true,   // clips
        true,   // transforms
        true,   // opacity_groups (flattened color fallback in PDF lowering)
        false,  // blend_modes
        false,  // filters
        false,  // shadows
    };

    switch (target) {
    case RENDER_EXPORT_TARGET_PDF:
        return &pdf_caps;
    case RENDER_EXPORT_TARGET_SVG:
    default:
        return &svg_caps;
    }
}

// ===== paint_ir.h =====
// ==========================================================================
// PaintIR — Semantic paint intermediate representation.
//
// Phase C of the render-paths unification (see vibe/radiant/
// Radiant_Design_Render_Paths.md). PaintIR is the *target-neutral* paint
// layer that sits ABOVE the raster DisplayList. Today's DisplayList is a
// raster lowering (pixel-domain ops, rasterised glyph bitmaps, premultiplied
// compositing); PaintIR is the higher, shared layer that every backend
// (raster, SVG, PDF) is meant to consume so the per-element paint algorithm
// lives in exactly one place.
//
// Two-level model:
//   - PaintIR (this file): semantic, target-neutral commands.
//   - DisplayList (display_list.h): ONE lowering of PaintIR for raster.
//       paint_ir_lower_raster() turns PaintIR -> DisplayList. Tiled replay
//       re-replays that DisplayList; SVG/PDF lowerings consume PaintIR directly
//       as command families are migrated.
//
// Migration status (Phase C step 1):
//   - The vector primitive ops below mirror the rc_* painter gateway 1:1 and
//     lower to the matching dl_* command, so raster output stays byte-for-byte
//     identical. This is the "thin layer above DisplayList" the design doc's
//     pragmatic migration note describes.
//   - The higher-level semantic ops (glyph runs, effect groups, SVG subscene)
//     are the canonical contract. Lowering support is deliberately incremental:
//     SVG already handles text runs and opacity-only groups; richer effects and
//     nested SVG subscenes still expand in later phases.
// ==========================================================================


#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// PaintIR op codes
// ---------------------------------------------------------------------------

typedef enum {
    PAINT_OP_FLAG_NONE = 0,
    PAINT_OP_FLAG_TRANSFORM_STACK = 1 << 0,
    PAINT_OP_FLAG_EFFECT_STACK = 1 << 1,
    PAINT_OP_FLAG_RASTER_NOOP = 1 << 2,
    PAINT_OP_FLAG_OWNED_PAYLOAD = 1 << 3,
    PAINT_OP_FLAG_STACK_PUSH = 1 << 4,
    PAINT_OP_FLAG_STACK_POP = 1 << 5,
    PAINT_OP_FLAG_CLIP_STACK = 1 << 6
} PaintOpFlags;

// keep the op set in one source list so enum values and diagnostics cannot drift.
#define PAINT_OP_LIST(X) \
    X(PAINT_FILL_RECT, PAINT_OP_FLAG_NONE) \
    X(PAINT_FILL_ROUNDED_RECT, PAINT_OP_FLAG_NONE) \
    X(PAINT_FILL_PATH, PAINT_OP_FLAG_OWNED_PAYLOAD) \
    X(PAINT_STROKE_PATH, PAINT_OP_FLAG_OWNED_PAYLOAD) \
    X(PAINT_FILL_LINEAR_GRADIENT, PAINT_OP_FLAG_OWNED_PAYLOAD) \
    X(PAINT_FILL_RADIAL_GRADIENT, PAINT_OP_FLAG_OWNED_PAYLOAD) \
    X(PAINT_DRAW_IMAGE, PAINT_OP_FLAG_NONE) \
    X(PAINT_DRAW_IMAGE_RESOURCE, PAINT_OP_FLAG_NONE) \
    X(PAINT_DRAW_GLYPH, PAINT_OP_FLAG_NONE) \
    X(PAINT_DRAW_PICTURE, PAINT_OP_FLAG_NONE) \
    X(PAINT_VIDEO_PLACEHOLDER, PAINT_OP_FLAG_NONE) \
    X(PAINT_WEBVIEW_LAYER_PLACEHOLDER, PAINT_OP_FLAG_NONE) \
    X(PAINT_PUSH_CLIP, PAINT_OP_FLAG_CLIP_STACK | PAINT_OP_FLAG_STACK_PUSH) \
    X(PAINT_POP_CLIP, PAINT_OP_FLAG_CLIP_STACK | PAINT_OP_FLAG_STACK_POP) \
    X(PAINT_PUSH_TRANSFORM, PAINT_OP_FLAG_TRANSFORM_STACK | PAINT_OP_FLAG_RASTER_NOOP | PAINT_OP_FLAG_STACK_PUSH) \
    X(PAINT_POP_TRANSFORM, PAINT_OP_FLAG_TRANSFORM_STACK | PAINT_OP_FLAG_RASTER_NOOP | PAINT_OP_FLAG_STACK_POP) \
    X(PAINT_SAVE_BACKDROP, PAINT_OP_FLAG_NONE) \
    X(PAINT_COMPOSITE_OPACITY, PAINT_OP_FLAG_NONE) \
    X(PAINT_APPLY_BLEND_MODE, PAINT_OP_FLAG_NONE) \
    X(PAINT_APPLY_FILTER, PAINT_OP_FLAG_NONE) \
    X(PAINT_BOX_BLUR_REGION, PAINT_OP_FLAG_NONE) \
    X(PAINT_BOX_BLUR_INSET, PAINT_OP_FLAG_NONE) \
    X(PAINT_SHADOW_CLIP_SAVE, PAINT_OP_FLAG_NONE) \
    X(PAINT_SHADOW_CLIP_RESTORE, PAINT_OP_FLAG_NONE) \
    X(PAINT_OUTER_SHADOW, PAINT_OP_FLAG_NONE) \
    X(PAINT_FILL_SURFACE_RECT, PAINT_OP_FLAG_NONE) \
    X(PAINT_BLIT_SURFACE_SCALED, PAINT_OP_FLAG_NONE) \
    X(PAINT_GLYPH_RUN, PAINT_OP_FLAG_OWNED_PAYLOAD) \
    X(PAINT_BEGIN_EFFECT_GROUP, PAINT_OP_FLAG_EFFECT_STACK | PAINT_OP_FLAG_STACK_PUSH) \
    X(PAINT_END_EFFECT_GROUP, PAINT_OP_FLAG_EFFECT_STACK | PAINT_OP_FLAG_STACK_POP) \
    X(PAINT_SVG_SUBSCENE, PAINT_OP_FLAG_NONE)

typedef enum {
#define PAINT_OP_ENUM(op, flags) op,
    PAINT_OP_LIST(PAINT_OP_ENUM)
#undef PAINT_OP_ENUM
    PAINT_OP_COUNT
} PaintOp;

// ---------------------------------------------------------------------------
// Per-op payloads (vector primitives mirror the dl_* parameter shapes)
// ---------------------------------------------------------------------------

typedef struct {
    float x, y, w, h;
    Color color;
} PaintFillRect;

typedef struct {
    float x, y, w, h;
    float rx, ry;
    Color color;
} PaintFillRoundedRect;

typedef struct {
    RdtPath* path;          // borrowed unless owns_path is set by deferred lowerer
    bool owns_path;         // path must be freed by the owning PaintList cleanup
    Color color;
    RdtFillRule rule;
    bool has_transform;
    RdtMatrix transform;
} PaintFillPath;

typedef struct {
    RdtPath* path;          // borrowed unless owns_path is set by deferred lowerer
    bool owns_path;         // path must be freed by the owning PaintList cleanup
    Color color;
    float width;
    RdtStrokeCap cap;
    RdtStrokeJoin join;
    const float* dash_array; // borrowed
    int dash_count;
    float dash_phase;
    bool has_transform;
    RdtMatrix transform;
} PaintStrokePath;

typedef struct {
    RdtPath* path;          // borrowed unless owns_path is set by deferred lowerer
    bool owns_path;         // path must be freed by the owning PaintList cleanup
    float x1, y1, x2, y2;
    const RdtGradientStop* stops;  // borrowed unless owns_stops is set
    bool owns_stops;        // stops must be freed by the owning PaintList cleanup
    int stop_count;
    RdtFillRule rule;
    bool has_transform;
    RdtMatrix transform;
    bool has_gradient_transform;
    RdtMatrix gradient_transform;
} PaintFillLinearGradient;

typedef struct {
    RdtPath* path;          // borrowed unless owns_path is set by deferred lowerer
    bool owns_path;         // path must be freed by the owning PaintList cleanup
    float cx, cy, r;
    const RdtGradientStop* stops;  // borrowed unless owns_stops is set
    bool owns_stops;        // stops must be freed by the owning PaintList cleanup
    int stop_count;
    RdtFillRule rule;
    bool has_transform;
    RdtMatrix transform;
    bool has_gradient_transform;
    RdtMatrix gradient_transform;
} PaintFillRadialGradient;

typedef struct {
    const uint32_t* pixels; // borrowed
    int src_w, src_h, src_stride;
    float dst_x, dst_y, dst_w, dst_h;
    uint8_t opacity;
    bool has_transform;
    RdtMatrix transform;
    void* resource_owner;   // optional ImageSurface* owner for generation checks
} PaintDrawImage;

typedef struct {
    ImageSurface* image;     // borrowed; lowerers decode or reference URL as needed
    float dst_x, dst_y, dst_w, dst_h;
    uint8_t opacity;
    bool has_transform;
    RdtMatrix transform;
} PaintDrawImageResource;

typedef struct {
    GlyphBitmap bitmap;      // descriptor copy; bitmap buffer borrowed
    int x, y;
    Color color;
    bool is_color_emoji;
    bool has_clip;
    Bound clip;
    bool has_transform;
    RdtMatrix transform;
    uint64_t resource_generation;
} PaintDrawGlyph;

typedef struct {
    RdtPicture* picture;    // borrowed
    uint8_t opacity;
    bool has_transform;
    RdtMatrix transform;
} PaintDrawPicture;

typedef struct {
    void* video;             // borrowed video handle
    float dst_x, dst_y, dst_w, dst_h;
    int object_fit;
    bool has_clip;
    Bound clip;
    uint64_t video_generation;
} PaintVideoPlaceholder;

typedef struct {
    void* surface;           // ImageSurface* — borrowed
    float dst_x, dst_y, dst_w, dst_h;
    bool has_clip;
    Bound clip;
    uint64_t surface_generation;
} PaintWebviewLayerPlaceholder;

typedef struct {
    RdtPath* clip_path;     // borrowed
    bool has_transform;
    RdtMatrix transform;
} PaintPushClip;

typedef struct {
    RdtMatrix transform;
} PaintPushTransform;

// ── Raster-lowering tier payloads (mirror the pixel-domain Dl* structs) ────

typedef struct {
    int x0, y0, w, h;       // physical pixel region
} PaintSaveBackdrop;

typedef struct {
    int x0, y0, w, h;       // physical pixel region
    float opacity;
    bool premultiplied_source;
} PaintCompositeOpacity;

typedef struct {
    int x0, y0, w, h;       // physical pixel region
    int blend_mode;          // CssEnum
} PaintApplyBlendMode;

typedef struct {
    float x, y, w, h;
    void* filter;            // FilterProp* — borrowed
    bool has_clip;
    Bound clip;              // rectangular clip bounds at recording time
} PaintApplyFilter;

typedef struct {
    int rx, ry, rw, rh;
    float blur_radius;
    int clip_type;
    float clip_params[8];
    int exclude_type;
    float exclude_params[8];
    bool premultiply_source;
    bool tint_source;
    Color tint_color;
} PaintBoxBlurRegion;

typedef struct {
    int rx, ry, rw, rh;
    int pad;
    float blur_radius;
    uint32_t bg_color;
} PaintBoxBlurInset;

typedef struct {
    int rx, ry, rw, rh;
} PaintShadowClipSave;

typedef struct {
    int exclude_type;
    float exclude_params[8];
    int save_rx, save_ry, save_rw, save_rh;
    int restore_inside;
} PaintShadowClipRestore;

typedef struct {
    float shadow_x, shadow_y, shadow_w, shadow_h;
    float sr_tl, sr_tr, sr_br, sr_bl;
    Color color;
    float blur_radius;
    int exclude_type;
    float exclude_params[8];
    int clip_type;
    float clip_params[8];
} PaintOuterShadow;

typedef struct {
    float x, y, w, h;
    uint32_t color;          // ABGR8888
    bool has_clip;
    Bound clip;
    ClipShape** clip_shapes; // borrowed; raster lowering clones into DisplayList
    int clip_depth;
} PaintFillSurfaceRect;

typedef struct {
    void* src_surface;       // ImageSurface* — borrowed
    uint64_t src_generation;
    float dst_x, dst_y, dst_w, dst_h;
    int scale_mode;
    uint8_t opacity;
    bool has_clip;
    Bound clip;
    ClipShape** clip_shapes; // borrowed; raster lowering clones into DisplayList
    int clip_depth;
} PaintBlitSurfaceScaled;

// ── Higher-level semantic payloads (incrementally lowered by target) ────────

// Effect group descriptor. Mirrors the CSS stacking effect inputs so every
// backend can decide native-vs-fallback (see design doc §6).
typedef struct PaintEffectGroup {
    Bound bounds;            // visual bounds of the grouped subtree
    bool has_clip;
    bool has_transform;
    RdtMatrix transform;
    float opacity;           // 1.0 = none
    int blend_mode;          // CssEnum; 0 = normal
    void* filter;            // FilterProp*; null = none
    bool backdrop;           // backdrop-filter present
    void* backdrop_filter;   // FilterProp* for backdrop-filter; null = none
    bool shadow;             // box-shadow present
    bool isolation;          // forced isolation
} PaintEffectGroup;

// A nested SVG subscene (Phase F). Carries the inheritance + geometry that
// must survive lowering so inline SVG renders identically on every target.
typedef struct PaintSvgSubscene {
    void* svg_root;          // Element* SVG DOM root (inline native or picture)
    void* pool;              // Pool* used by the owning document when available
    void* font_context;      // FontContext* for SVG text resolution
    float viewport_width;
    float viewport_height;
    float pixel_ratio;
    float opacity;
    bool has_color;          // inherited currentColor present
    Color color;             // inherited currentColor
    bool has_fill;           // cascaded fill present
    Color fill;
    bool fill_none;
    bool has_stroke;
    Color stroke;
    bool stroke_none;
    float stroke_width;
    RdtMatrix transform;     // base transform; SVG lowering composes viewBox/PAR
    Bound content_clip;      // clip established for the SVG box
    const char* source_path; // for resolving nested refs + recursion guard
    uint64_t resource_generation; // immutable parsed DOM generation (retain-safe)
} PaintSvgSubscene;

typedef void (*PaintSvgSubsceneRasterLowerFn)(const PaintSvgSubscene* subscene,
                                              DisplayList* dl);
typedef bool (*PaintSvgSubsceneSvgLowerFn)(const PaintSvgSubscene* subscene,
                                           StrBuf* out, int indent_level);

void paint_ir_register_svg_subscene_lowerers(PaintSvgSubsceneRasterLowerFn raster_lower,
                                             PaintSvgSubsceneSvgLowerFn svg_lower);

// A semantic glyph run. Positions/text/font, not rasterised bitmaps.
typedef struct {
    void* font;              // FontBox* / font handle
    Color color;
    const char* text;        // optional native text payload; UTF-8, borrowed unless owns_text
    int text_len;            // bytes; 0 means empty, negative means strlen(text)
    bool owns_text;          // text must be freed by the owning PaintList cleanup
    const char* font_family; // borrowed; optional for vector text lowering
    float font_size;
    float x, baseline_y;
    float word_spacing;
    int font_weight;         // CSS numeric weight; 0 = omit
    bool italic;
    const uint32_t* glyph_ids;   // borrowed
    const float* xs;             // borrowed pen positions
    const float* ys;
    int count;
    bool has_transform;
    RdtMatrix transform;
    Bound clip;
} PaintGlyphRun;

typedef void (*PaintGlyphRunRasterLowerFn)(const PaintGlyphRun* run,
                                           DisplayList* dl);
void paint_ir_register_glyph_run_raster_lowerer(PaintGlyphRunRasterLowerFn lowerer);

// ---------------------------------------------------------------------------
// PaintCmd — tagged union of all paint commands
// ---------------------------------------------------------------------------

typedef struct PaintCmd {
    PaintOp op;
    union {
        PaintFillRect           fill_rect;
        PaintFillRoundedRect    fill_rounded_rect;
        PaintFillPath           fill_path;
        PaintStrokePath         stroke_path;
        PaintFillLinearGradient fill_linear_gradient;
        PaintFillRadialGradient fill_radial_gradient;
        PaintDrawImage          draw_image;
        PaintDrawImageResource  draw_image_resource;
        PaintDrawGlyph          draw_glyph;
        PaintDrawPicture        draw_picture;
        PaintVideoPlaceholder   video_placeholder;
        PaintWebviewLayerPlaceholder webview_layer_placeholder;
        PaintPushClip           push_clip;
        PaintPushTransform      push_transform;
        PaintSaveBackdrop       save_backdrop;
        PaintCompositeOpacity   composite_opacity;
        PaintApplyBlendMode     apply_blend_mode;
        PaintApplyFilter        apply_filter;
        PaintBoxBlurRegion      box_blur_region;
        PaintBoxBlurInset       box_blur_inset;
        PaintShadowClipSave     shadow_clip_save;
        PaintShadowClipRestore  shadow_clip_restore;
        PaintOuterShadow        outer_shadow;
        PaintFillSurfaceRect    fill_surface_rect;
        PaintBlitSurfaceScaled  blit_surface_scaled;
        PaintEffectGroup        effect_group;
        PaintSvgSubscene        svg_subscene;
        PaintGlyphRun           glyph_run;
    };
} PaintCmd;

// ---------------------------------------------------------------------------
// PaintList — growable array of PaintCmd (the recorded semantic IR)
// ---------------------------------------------------------------------------

typedef struct PaintList {
    PaintCmd* cmds;
    int count;
    int capacity;
    Arena* arena;            // backing arena for the cmds array
} PaintList;

typedef struct PaintIrValidationResult {
    bool valid;
    int first_error_index;
    const char* message;
    int clip_depth;
    int backdrop_depth;
    int shadow_clip_depth;
    int effect_depth;
} PaintIrValidationResult;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void paint_list_init(PaintList* pl, Arena* backing_arena);
void paint_list_clear(PaintList* pl);   // rewind count (arena owned by caller)
void paint_list_destroy(PaintList* pl);
int  paint_list_count(const PaintList* pl);
bool paint_ir_validate(const PaintList* pl, PaintIrValidationResult* result);
bool paint_ir_validate_or_log(const PaintList* pl, const char* context);
const char* paint_op_name(PaintOp op);
bool paint_op_has_flags(PaintOp op, unsigned flags);
bool paint_list_has_op_flags(const PaintList* pl, unsigned flags);

// ---------------------------------------------------------------------------
// PaintBuilder — recording API (mirrors the rc_* painter gateway)
// ---------------------------------------------------------------------------

void paint_fill_rect(PaintList* pl, float x, float y, float w, float h, Color color);
void paint_fill_rounded_rect(PaintList* pl, float x, float y, float w, float h,
                             float rx, float ry, Color color);
void paint_fill_path(PaintList* pl, RdtPath* path, Color color,
                     RdtFillRule rule, const RdtMatrix* transform);
void paint_stroke_path(PaintList* pl, RdtPath* path, Color color, float width,
                       RdtStrokeCap cap, RdtStrokeJoin join,
                       const float* dash_array, int dash_count, float dash_phase,
                       const RdtMatrix* transform);
void paint_fill_linear_gradient(PaintList* pl, RdtPath* path,
                                float x1, float y1, float x2, float y2,
                                const RdtGradientStop* stops, int stop_count,
                                RdtFillRule rule, const RdtMatrix* transform,
                                const RdtMatrix* gradient_transform);
void paint_fill_radial_gradient(PaintList* pl, RdtPath* path,
                                float cx, float cy, float r,
                                const RdtGradientStop* stops, int stop_count,
                                RdtFillRule rule, const RdtMatrix* transform,
                                const RdtMatrix* gradient_transform);
void paint_draw_image(PaintList* pl, const uint32_t* pixels,
                      int src_w, int src_h, int src_stride,
                      float dst_x, float dst_y, float dst_w, float dst_h,
                      uint8_t opacity, const RdtMatrix* transform,
                      void* resource_owner);
void paint_draw_image_resource(PaintList* pl, ImageSurface* image,
                               float dst_x, float dst_y,
                               float dst_w, float dst_h,
                               uint8_t opacity,
                               const RdtMatrix* transform);
void paint_draw_glyph(PaintList* pl, GlyphBitmap* bitmap, int x, int y,
                      Color color, bool is_color_emoji, const Bound* clip,
                      const RdtMatrix* transform, uint64_t resource_generation);
void paint_draw_picture(PaintList* pl, RdtPicture* picture,
                        uint8_t opacity, const RdtMatrix* transform);
void paint_video_placeholder(PaintList* pl, void* video,
                             float dst_x, float dst_y, float dst_w, float dst_h,
                             int object_fit, const Bound* clip,
                             uint64_t video_generation);
void paint_webview_layer_placeholder(PaintList* pl, void* surface,
                                     float dst_x, float dst_y, float dst_w, float dst_h,
                                     const Bound* clip,
                                     uint64_t surface_generation);
void paint_push_clip(PaintList* pl, RdtPath* clip_path, const RdtMatrix* transform);
void paint_pop_clip(PaintList* pl);
void paint_push_transform(PaintList* pl, const RdtMatrix* transform);
void paint_pop_transform(PaintList* pl);

// Raster-lowering tier (pixel-domain effect ops; see enum comment).
void paint_save_backdrop(PaintList* pl, int x0, int y0, int w, int h);
void paint_composite_opacity(PaintList* pl, int x0, int y0, int w, int h,
                             float opacity, bool premultiplied_source);
void paint_apply_blend_mode(PaintList* pl, int x0, int y0, int w, int h, int blend_mode);
void paint_apply_filter(PaintList* pl, float x, float y, float w, float h,
                        void* filter, const Bound* clip);
void paint_box_blur_region(PaintList* pl, int rx, int ry, int rw, int rh,
                           float blur_radius, int clip_type, const float* clip_params,
                           int exclude_type, const float* exclude_params,
                           bool premultiply_source, bool tint_source, Color tint_color);
void paint_box_blur_inset(PaintList* pl, int rx, int ry, int rw, int rh,
                          int pad, float blur_radius, uint32_t bg_color);
void paint_shadow_clip_save(PaintList* pl, int rx, int ry, int rw, int rh);
void paint_shadow_clip_restore(PaintList* pl, int exclude_type, const float* exclude_params,
                               int save_rx, int save_ry, int save_rw, int save_rh,
                               int restore_inside);
void paint_outer_shadow(PaintList* pl,
                        float shadow_x, float shadow_y, float shadow_w, float shadow_h,
                        float sr_tl, float sr_tr, float sr_br, float sr_bl,
                        Color color, float blur_radius,
                        int exclude_type, const float* exclude_params,
                        int clip_type, const float* clip_params);
void paint_fill_surface_rect(PaintList* pl, float x, float y, float w, float h,
                             uint32_t color, const Bound* clip,
                             ClipShape** clip_shapes, int clip_depth);
void paint_blit_surface_scaled(PaintList* pl, void* src_surface,
                               float dst_x, float dst_y, float dst_w, float dst_h,
                               int scale_mode, const Bound* clip,
                               ClipShape** clip_shapes, int clip_depth,
                               uint8_t opacity, uint64_t src_generation);

// Higher-level semantic ops (target lowerings are intentionally incremental).
void paint_begin_effect_group(PaintList* pl, const PaintEffectGroup* group);
void paint_end_effect_group(PaintList* pl);
void paint_svg_subscene(PaintList* pl, const PaintSvgSubscene* subscene);
void paint_glyph_run(PaintList* pl, const PaintGlyphRun* glyph_run);

// ---------------------------------------------------------------------------
// Raster lowering: PaintIR -> DisplayList
//
// Lowers the vector primitive ops 1:1 onto the matching dl_* commands. The
// resulting DisplayList is identical to recording those dl_* calls directly,
// which keeps raster output byte-for-byte unchanged. Higher-level semantic ops
// that lack raster expansion here are ignored by this lowering.
// ---------------------------------------------------------------------------

void paint_ir_lower_raster(const PaintList* pl, DisplayList* dl);
void paint_ir_lower_raster_fragment(const PaintList* pl, DisplayList* dl);

// ---------------------------------------------------------------------------
// SVG lowering: PaintIR -> SVG fragment
//
// Phase D foothold. This lowering intentionally starts with the primitive
// commands whose SVG representation is exact and target-neutral. Unsupported
// commands are counted explicitly so callers can choose native support,
// raster fallback, or diagnostic handling as the capability table grows.
// ---------------------------------------------------------------------------

typedef struct {
    int indent_level;
    bool emit_unsupported_comments;
    const RenderExportTargetCaps* caps;
    int resource_id_base;
} PaintSvgLoweringOptions;

typedef struct {
    int command_count;
    int emitted_count;
    int fallback_count;
    int unsupported_count;
} PaintSvgLoweringStats;

typedef struct {
    int indent_level;
    int open_clip_depth;
    int skipped_clip_depth;
    int open_transform_depth;
    int skipped_transform_depth;
    int open_effect_depth;
    int skipped_effect_depth;
} PaintSvgLoweringState;

void paint_svg_lowering_state_init(PaintSvgLoweringState* state, int indent_level);
void paint_svg_color_to_string(Color color, char* result, int result_cap);
void paint_svg_append_color(StrBuf* out, Color color);

void paint_ir_lower_svg(const PaintList* pl, StrBuf* out,
                        const PaintSvgLoweringOptions* options,
                        PaintSvgLoweringStats* stats);

void paint_ir_lower_svg_stream(const PaintList* pl, StrBuf* out,
                               const PaintSvgLoweringOptions* options,
                               PaintSvgLoweringState* state,
                               PaintSvgLoweringStats* stats);

#ifdef __cplusplus
}
#endif

// ===== retained_display_list.hpp =====
typedef struct RetainedDisplayListCache RetainedDisplayListCache;
typedef struct RetainedDisplayListFragment RetainedDisplayListFragment;

typedef struct RetainedDisplayListStats {
    int capture_candidates;
    int captured;
    int skipped_non_retainable;
    int copy_failed;
    int reuse_hits;
    int reuse_misses;
    int reuse_rejected_resources;
    int reuse_rejected_dirty;
} RetainedDisplayListStats;

RetainedDisplayListCache* retained_dl_cache_create(Pool* pool);
void retained_dl_cache_destroy(RetainedDisplayListCache* cache);

void retained_dl_cache_begin_frame(RetainedDisplayListCache* cache);
void retained_dl_cache_capture(RetainedDisplayListCache* cache, const DisplayList* source);
RetainedDisplayListStats retained_dl_cache_stats(const RetainedDisplayListCache* cache);
void retained_dl_cache_note_reuse_miss(RetainedDisplayListCache* cache);
void retained_dl_cache_note_reuse_rejected_resources(RetainedDisplayListCache* cache);
void retained_dl_cache_note_reuse_rejected_dirty(RetainedDisplayListCache* cache);
void retained_dl_cache_note_reuse_hit(RetainedDisplayListCache* cache);

const RetainedDisplayListFragment* retained_dl_cache_get(RetainedDisplayListCache* cache,
                                                         uint32_t view_id);
Bound retained_dl_fragment_bounds(const RetainedDisplayListFragment* fragment);
Bound retained_dl_fragment_marker_bounds(const RetainedDisplayListFragment* fragment);
int retained_dl_fragment_item_count(const RetainedDisplayListFragment* fragment);
bool retained_dl_fragment_resources_valid(const RetainedDisplayListFragment* fragment,
                                          uint64_t current_video_generation,
                                          uint64_t current_glyph_generation);
typedef bool (*RetainedDisplayListContainsViewFn)(void* userdata, uint32_t source_view_id);
bool retained_dl_append_fragment_for_dirty(DisplayList* dst,
                                           const RetainedDisplayListFragment* fragment,
                                           Bound current_marker_bounds,
                                           DirtyTracker* tracker,
                                           float scale,
                                           RetainedDisplayListContainsViewFn contains_view,
                                           void* contains_userdata);
bool retained_dl_append_fragment(DisplayList* dst,
                                 const RetainedDisplayListFragment* fragment);

// ===== render.hpp =====
// format to SDL_PIXELFORMAT_ARGB8888
#define RDT_PIXELFORMAT_RGB(r, g, b)    ((uint32_t)((r << 16) | (g << 8) | b))

struct RenderProfiler;
typedef struct RenderProfiler RenderProfiler;

// Semantic paint IR recording target (paint_ir.h). A RenderContext that records
// painter commands must provide both paint_list and dl; the rc_* gateway emits
// through the PaintBuilder and lowers to the display list, so the live raster
// path flows through the semantic IR.
struct PaintList;
typedef struct PaintList PaintList;

typedef struct RenderContext {
    FontBox font;  // current font style
    BlockBlot block;
    ListBlot list;
    Color color;
    RdtVector vec;      // platform-agnostic vector renderer

    UiContext* ui_context;

    // Display list for deferred rendering (Phase 1)
    // When non-NULL, render functions record to dl instead of drawing directly.
    DisplayList* dl;
    // Semantic paint IR recording target (Phase C). Must be non-NULL alongside
    // dl for painter commands; the rc_* gateway records primitives through the
    // PaintBuilder and lowers them to dl (byte-identical to direct dl_*).
    PaintList* paint_list;
    RetainedDisplayListCache* retained_dl_cache;

    // Transform state
    RdtMatrix transform;           // Current combined transform matrix
    bool has_transform;            // True if non-identity transform is active
    float perspective_distance;    // Active CSS perspective from ancestor, 0 = none
    float perspective_origin_x;
    float perspective_origin_y;

    // HiDPI scaling: CSS logical pixels -> physical surface pixels
    float scale;                   // pixel_ratio (1.0 for standard, 2.0 for Retina, etc.)
    
    // Phase 18: Dirty-region tracking for render tree clipping
    DirtyTracker* dirty_tracker;   // NULL = full repaint (no clipping)
    Bound dirty_union;             // union bbox of all dirty rects (CSS pixels, valid when dirty_tracker != NULL)
    bool has_dirty_union;          // true when dirty_union is valid

    // LIFO scratch allocator for scoped temporary buffers (pixel buffers, clip masks, etc.)
    ScratchArena scratch;

    // Per-render profiling counters and timers.
    RenderProfiler* profiler;

    // Vector clip shape stack for overflow:hidden with border-radius and CSS clip-path
    ClipShape* clip_shapes[RDT_MAX_CLIP_SHAPES];
    int clip_shape_depth;

    // Suppresses automatic per-block display-list markers while a caller records
    // a wider element subtree marker around replaced/layer content.
    int element_marker_suppression_depth;
} RenderContext;

// Function declarations
void render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file);

typedef struct RenderElementMarkerScope {
    int begin_index;
} RenderElementMarkerScope;

bool render_block_dirty_misses(RenderContext* rdcon, ViewBlock* block);
bool render_block_viewport_misses(RenderContext* rdcon, ViewBlock* block);
bool render_block_try_retained_fragment(RenderContext* rdcon, ViewBlock* block);
void render_block_view(RenderContext* rdcon, ViewBlock* view_block);
void render_embed_doc(RenderContext* rdcon, ViewBlock* block);
void render_inline_view(RenderContext* rdcon, ViewSpan* view_span);
void render_bound(RenderContext* rdcon, ViewBlock* view);
void render_outline_deferred(RenderContext* rdcon, ViewBlock* view);
void render_children(RenderContext* rdcon, View* view);
void render_raster_positioned_children(RenderContext* rdcon, ViewBlock* block);
void render_raster_positive_z_descendants(RenderContext* rdcon, View* view);
void render_raster_view_tree(RenderContext* rdcon, ViewTree* view_tree);
RenderElementMarkerScope render_element_marker_begin(RenderContext* rdcon, ViewBlock* block);
void render_element_marker_end(RenderContext* rdcon, RenderElementMarkerScope* scope);

// Shut down the render pool (must be called before rdt_engine_term)
void render_pool_shutdown();

// Tile-based PNG rendering for large pages that would OOM with a single surface.
// Only used for PNG output.  total_width/total_height are in physical pixels.
void render_html_doc_tiled(UiContext* uicon, ViewTree* view_tree, const char* output_file,
                           int total_width, int total_height);

// ===== render_rect.hpp =====
typedef struct IRect {
    int x;
    int y;
    int w;
    int h;
} IRect;

// ===== render_backend.h =====
/**
 * RenderBackend — Abstract render dispatch interface.
 *
 * A vtable of function pointers that each output backend (SVG, PDF, …)
 * implements. The shared tree walker (render_walk.cpp) traverses the
 * View tree once and dispatches drawing through these callbacks.
 *
 * The raster backend uses the shared walker through full-node override
 * callbacks while its richer block state, HiDPI scaling, pixel-level
 * post-processing, scrollbars, and display-list markers are migrated in
 * staged slices.
 *
 * All coordinates are in CSS logical pixels. Each callback receives
 * the backend's own opaque context pointer (ctx).
 */

typedef struct RenderBackend RenderBackend;
typedef struct PaintEffectGroup PaintEffectGroup;

struct RenderBackend {
    void* ctx;   // backend-specific context (SvgRenderContext*, PdfRenderContext*, etc.)

    // Optional full-node overrides. Raster screen rendering uses these for
    // specialized block and inline paths behind the shared walker.
    // Backends that leave these null use the generic callbacks below.
    void (*render_block)(void* ctx, ViewBlock* block, float abs_x, float abs_y,
                         FontBox* font, Color color);
    void (*render_inline)(void* ctx, ViewSpan* span, float abs_x, float abs_y,
                          FontBox* font, Color color);

    // ── Boundary rendering (background, borders, shadow, outline) ──────
    // Called for every block/inline element that has a BoundaryProp.
    // abs_x/abs_y = absolute CSS-px position of the element on the page.
    void (*render_bound)(void* ctx, ViewBlock* view, float abs_x, float abs_y);

    // ── Text rendering ─────────────────────────────────────────────────
    // Called for every ViewText node.
    // abs_x/abs_y = absolute position of the text's containing block.
    void (*render_text)(void* ctx, ViewText* text, float abs_x, float abs_y,
                        FontBox* font, Color color);

    // ── Image rendering ────────────────────────────────────────────────
    // Called for blocks with embed->img.
    void (*render_image)(void* ctx, ViewBlock* block, float abs_x, float abs_y);

    // ── Inline SVG subscene ────────────────────────────────────────────
    // Called for HTM_TAG_SVG blocks. If NULL, skipped.
    void (*render_inline_svg)(void* ctx, ViewBlock* block, float abs_x, float abs_y,
                              FontBox* font, Color color);

    // ── Children group wrappers ────────────────────────────────────────
    // Emits container markup around a block's children (e.g. <g class="block"> in SVG).
    // begin returns an opaque cookie; end receives it for matched close.
    void (*begin_block_children)(void* ctx, ViewBlock* block);
    void (*end_block_children)(void* ctx, ViewBlock* block);
    void (*begin_inline_children)(void* ctx, ViewSpan* span);
    void (*end_inline_children)(void* ctx, ViewSpan* span);

    // ── Semantic effect wrapper ────────────────────────────────────────
    // Called around content affected by CSS stacking effects.
    void (*begin_effect_group)(void* ctx, const PaintEffectGroup* group);
    void (*end_effect_group)(void* ctx);

    // ── Transform wrapper ──────────────────────────────────────────────
    // Called around a block's self-paint and contents when CSS transforms are present.
    void (*begin_transform)(void* ctx, ViewBlock* block, float abs_x, float abs_y);
    void (*end_transform)(void* ctx);

    // ── List marker rendering ───────────────────────────────────────────
    // Called for RDT_VIEW_MARKER nodes (::marker pseudo-elements).
    // Renders bullets (disc/circle/square), numbers (decimal/roman/alpha),
    // or disclosure triangles (for <summary>).
    void (*render_marker)(void* ctx, ViewSpan* marker, float abs_x, float abs_y,
                          FontBox* font, Color color);

    // ── Column rules ───────────────────────────────────────────────────
    void (*render_column_rules)(void* ctx, ViewBlock* block, float abs_x, float abs_y);

    // ── Font setup ─────────────────────────────────────────────────────
    // Called when a view node specifies a font. Backend can update its
    // font state (e.g. PDF needs to call HPDF_Page_SetFontAndSize).
    void (*on_font_change)(void* ctx, FontProp* font_prop);
};

// Shared tree-walk state (managed by render_walk.cpp, passed to callbacks via ctx).
typedef struct {
    float x, y;           // accumulated absolute position (CSS logical px)
    FontBox font;          // inherited font
    Color color;           // inherited color
    UiContext* ui_context;
} RenderWalkState;

// ── Shared tree walker API ────────────────────────────────────────────
// Traverses the View tree and dispatches through `backend` callbacks.
void render_walk_block(RenderBackend* backend, RenderWalkState* state, ViewBlock* block);
void render_walk_inline(RenderBackend* backend, RenderWalkState* state, ViewSpan* span);
void render_walk_children(RenderBackend* backend, RenderWalkState* state, View* first_child);
void render_walk_positioned_children(RenderBackend* backend, RenderWalkState* state, ViewBlock* block);
void render_walk_positive_z_descendants(RenderBackend* backend, RenderWalkState* state, View* first_child);

// ===== render_geometry.hpp =====
Rect render_geometry_adjust_box_rect(Rect rect, CssEnum box, float scale,
                                     const BorderProp* border,
                                     const Spacing* padding);
Bound render_geometry_intersect_bound_rect(Bound bound, Rect rect);
IRect render_geometry_clip_to_pixel_bounds(Bound clip,
                                           const ImageSurface* surface);
bool render_geometry_pixel_bounds_empty(IRect bounds);

Rect render_geometry_block_border_rect(const BlockBlot* parent_block,
                                       const ViewBlock* block,
                                       float scale);
Rect render_geometry_block_content_rect(const BlockBlot* parent_block,
                                        const ViewBlock* block,
                                        float scale);
Rect render_geometry_expand_rect(Rect rect, float expand);
Bound render_geometry_rect_to_bound(Rect rect);
bool render_geometry_bounds_intersect(Bound a, Bound b);
float render_geometry_filter_effect_expand(const FilterProp* filter);
float render_geometry_block_visual_overflow(const ViewBlock* block);

// ===== display_list_bounds.hpp =====
static const float DL_UNBOUNDED_EXTENT = 99999.0f;
static const Bound DL_UNBOUNDED_CLIP = {
    0.0f, 0.0f, DL_UNBOUNDED_EXTENT, DL_UNBOUNDED_EXTENT
};

static inline Bound dl_unbounded_clip() {
    return DL_UNBOUNDED_CLIP;
}

static inline void dl_set_bounds_xyxy(DisplayItem* item,
                                      float left, float top,
                                      float right, float bottom) {
    if (!item) return;
    if (right <= left || bottom <= top) {
        item->bounds[0] = left;
        item->bounds[1] = top;
        item->bounds[2] = 0.0f;
        item->bounds[3] = 0.0f;
        return;
    }
    item->bounds[0] = left;
    item->bounds[1] = top;
    item->bounds[2] = right - left;
    item->bounds[3] = bottom - top;
}

static inline void dl_set_clipped_rect_bounds(DisplayItem* item,
                                              float x, float y,
                                              float w, float h,
                                              const Bound* clip) {
    float left = x;
    float top = y;
    float right = x + w;
    float bottom = y + h;
    if (clip) {
        left = LMB_MAX(left, clip->left);
        top = LMB_MAX(top, clip->top);
        right = LMB_MIN(right, clip->right);
        bottom = LMB_MIN(bottom, clip->bottom);
    }
    dl_set_bounds_xyxy(item, left, top, right, bottom);
}

Bound dl_item_bounds(const DisplayItem* item);
bool dl_item_intersects_rect(const DisplayItem* item,
                             float x, float y, float w, float h);

// ===== display_list_storage.hpp =====
DisplayItem* dl_alloc_item(DisplayList* dl);
RdtGradientStop* dl_copy_stops(DisplayList* dl, const RdtGradientStop* stops, int count);
float* dl_copy_dashes(DisplayList* dl, const float* dashes, int count);
void dl_store_clip_shapes(DisplayList* dl, DlClipShapeStack* dst,
                          ClipShape** clip_shapes, int clip_depth);
int dl_restore_clip_shapes(const DlClipShapeStack* src, ClipShape* shapes,
                           ClipShape** shape_ptrs);

// ===== display_list_surface_region.hpp =====
static inline bool surface_region_clip(ImageSurface* surface,
                                       int rx, int ry, int rw, int rh,
                                       IRect* out_region) {
    if (!surface || !surface->pixels || !out_region) return false;
    int x0 = LMB_MAX(0, rx);
    int y0 = LMB_MAX(0, ry);
    int x1 = LMB_MIN(surface->width, rx + rw);
    int y1 = LMB_MIN(surface->height, ry + rh);
    int w = x1 - x0;
    int h = y1 - y0;
    out_region->x = x0;
    out_region->y = y0;
    out_region->w = w > 0 ? w : 0;
    out_region->h = h > 0 ? h : 0;
    return w > 0 && h > 0;
}

static inline uint32_t* surface_region_save(ImageSurface* surface,
                                            ScratchArena* scratch,
                                            int rx, int ry, int rw, int rh,
                                            IRect* out_region) {
    if (!scratch || !surface_region_clip(surface, rx, ry, rw, rh, out_region)) return nullptr;
    int x0 = out_region->x;
    int y0 = out_region->y;
    int w = out_region->w;
    int h = out_region->h;
    uint32_t* saved = (uint32_t*)scratch_alloc(scratch, (size_t)w * h * sizeof(uint32_t));
    if (!saved) return nullptr;
    uint32_t* px = (uint32_t*)surface->pixels;
    int pitch = surface->pitch / 4;
    for (int row = 0; row < h; row++) {
        memcpy(saved + row * w,
               px + (y0 + row) * pitch + x0,
               (size_t)w * sizeof(uint32_t));
    }
    return saved;
}

static inline void surface_region_clear(ImageSurface* surface, const IRect* region) {
    if (!surface || !surface->pixels || !region) return;
    int x0 = region->x;
    int y0 = region->y;
    int w = region->w;
    int h = region->h;
    if (w <= 0 || h <= 0) return;
    uint32_t* px = (uint32_t*)surface->pixels;
    int pitch = surface->pitch / 4;
    for (int row = 0; row < h; row++) {
        memset(px + (y0 + row) * pitch + x0, 0, (size_t)w * sizeof(uint32_t));
    }
}

static inline void surface_region_restore_masked(ImageSurface* surface,
                                                 const uint32_t* saved,
                                                 const IRect* region,
                                                 ClipShape* mask,
                                                 bool restore_inside) {
    if (!surface || !surface->pixels || !saved || !region || !mask) return;
    int x0 = region->x;
    int y0 = region->y;
    int w = region->w;
    int h = region->h;
    if (w <= 0 || h <= 0) return;
    uint32_t* px = (uint32_t*)surface->pixels;
    int pitch = surface->pitch / 4;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            float fx = (float)(x0 + col) + 0.5f;
            float fy = (float)(y0 + row) + 0.5f;
            bool inside = clip_point_in_shape(mask, fx, fy);
            if (restore_inside ? inside : !inside) {
                px[(y0 + row) * pitch + (x0 + col)] = saved[row * w + col];
            }
        }
    }
}

// ===== display_list_replay_state.hpp =====
typedef struct DisplayReplayDirtyClip {
    Bound bounds;
    RdtPath* path;
    bool active;
} DisplayReplayDirtyClip;

DisplayReplayDirtyClip dl_replay_push_dirty_clip(RdtVector* vec,
                                                 DirtyTracker* dirty_tracker,
                                                 float scale);
void dl_replay_pop_dirty_clip(RdtVector* vec, DisplayReplayDirtyClip* clip);
void dl_replay_intersect_dirty_clip(const DisplayReplayDirtyClip* clip, Bound* bounds);

// ===== display_list_replay_vector.hpp =====
typedef enum DisplayReplayVectorResult {
    DL_REPLAY_VECTOR_NOT_HANDLED = 0,
    DL_REPLAY_VECTOR_HANDLED,
    DL_REPLAY_VECTOR_DREW,
} DisplayReplayVectorResult;

DisplayReplayVectorResult dl_replay_vector_item(RdtVector* vec,
                                                DisplayItem* item,
                                                bool duplicate_picture);
bool dl_replay_vector_clip_item(RdtVector* vec, DisplayItem* item);

// ===== display_list_replay_glyph.hpp =====
void dl_replay_draw_glyph(ImageSurface* surface, const DlDrawGlyph* glyph);
void dl_replay_draw_glyph_at_offset(ImageSurface* surface, const DlDrawGlyph* glyph,
                                    float offset_x, float offset_y);

// ===== display_list_replay_raster.hpp =====
void dl_replay_fill_surface_rect(ImageSurface* surface,
                                 const DisplayReplayDirtyClip* dirty_clip,
                                 const DlFillSurfaceRect* fill);
void dl_replay_fill_surface_rect_at_offset(ImageSurface* surface, ScratchArena* scratch,
                                           const DlFillSurfaceRect* fill,
                                           float offset_x, float offset_y);
void dl_replay_blit_surface_scaled(ImageSurface* surface,
                                   const DisplayReplayDirtyClip* dirty_clip,
                                   const DlBlitSurfaceScaled* blit);
void dl_replay_blit_surface_scaled_at_offset(ImageSurface* surface, ScratchArena* scratch,
                                             const DlBlitSurfaceScaled* blit,
                                             float offset_x, float offset_y);
void dl_replay_webview_layer_placeholder(ImageSurface* surface,
                                         const DlWebviewLayerPlaceholder* placeholder);
void dl_replay_webview_layer_placeholder_at_offset(ImageSurface* surface,
                                                   const DlWebviewLayerPlaceholder* placeholder,
                                                   float offset_x, float offset_y);

// ===== display_list_replay_backdrop.hpp =====
#define DL_REPLAY_MAX_BACKDROP_DEPTH 16

typedef struct DisplayReplayBackdropStack {
    uint32_t* stack[DL_REPLAY_MAX_BACKDROP_DEPTH];
    IRect region[DL_REPLAY_MAX_BACKDROP_DEPTH];
    int sp;
} DisplayReplayBackdropStack;

void dl_replay_backdrop_init(DisplayReplayBackdropStack* stack);
int dl_replay_backdrop_depth(const DisplayReplayBackdropStack* stack);
void dl_replay_backdrop_save(DisplayReplayBackdropStack* stack,
                             ImageSurface* surface,
                             ScratchArena* scratch,
                             const DlSaveBackdrop* backdrop);
void dl_replay_backdrop_save_at_offset(DisplayReplayBackdropStack* stack,
                                       ImageSurface* surface,
                                       ScratchArena* scratch,
                                       const DlSaveBackdrop* backdrop,
                                       float origin_x, float origin_y);
void dl_replay_backdrop_push_empty(DisplayReplayBackdropStack* stack);
void dl_replay_backdrop_discard(DisplayReplayBackdropStack* stack,
                                ScratchArena* scratch);
void dl_replay_backdrop_composite_opacity(DisplayReplayBackdropStack* stack,
                                          ImageSurface* surface,
                                          ScratchArena* scratch,
                                          const DlCompositeOpacity* opacity);
void dl_replay_backdrop_apply_blend_mode(DisplayReplayBackdropStack* stack,
                                         ImageSurface* surface,
                                         ScratchArena* scratch,
                                         const DlApplyBlendMode* blend);
bool dl_replay_backdrop_skip_item(DisplayReplayBackdropStack* stack,
                                  ScratchArena* scratch,
                                  const DisplayItem* item);

// ===== display_list_replay_shadow.hpp =====
typedef struct DisplayReplayShadowClip {
    uint32_t* saved;
    IRect region;
} DisplayReplayShadowClip;

void dl_replay_shadow_clip_init(DisplayReplayShadowClip* clip);
void dl_replay_shadow_clip_save(DisplayReplayShadowClip* clip,
                                ImageSurface* surface,
                                ScratchArena* scratch,
                                const DlShadowClipSave* save);
void dl_replay_shadow_clip_save_at_offset(DisplayReplayShadowClip* clip,
                                          ImageSurface* surface,
                                          ScratchArena* scratch,
                                          const DlShadowClipSave* save,
                                          float origin_x, float origin_y);
void dl_replay_shadow_clip_restore(DisplayReplayShadowClip* clip,
                                   ImageSurface* surface,
                                   const DlShadowClipRestore* restore);
void dl_replay_shadow_clip_restore_at_offset(DisplayReplayShadowClip* clip,
                                             ImageSurface* surface,
                                             const DlShadowClipRestore* restore,
                                             float origin_x, float origin_y);
void dl_replay_shadow_clip_discard(DisplayReplayShadowClip* clip);

// ===== display_list_replay_effects.hpp =====
void dl_replay_apply_filter(ScratchArena* scratch,
                            ImageSurface* surface,
                            const RenderBackendCaps* caps,
                            const DisplayReplayDirtyClip* dirty_clip,
                            const DlApplyFilter* filter);
void dl_replay_apply_filter_at_offset(ScratchArena* scratch,
                                      ImageSurface* surface,
                                      const RenderBackendCaps* caps,
                                      const DlApplyFilter* filter,
                                      float offset_x, float offset_y);
void dl_replay_box_blur_region(ScratchArena* scratch,
                               ImageSurface* surface,
                               const DlBoxBlurRegion* blur);
void dl_replay_box_blur_region_at_offset(ScratchArena* scratch,
                                         ImageSurface* surface,
                                         const DlBoxBlurRegion* blur,
                                         float offset_x, float offset_y);
void dl_replay_box_blur_inset(ScratchArena* scratch,
                              ImageSurface* surface,
                              const DlBoxBlurInset* blur);
void dl_replay_box_blur_inset_at_offset(ScratchArena* scratch,
                                        ImageSurface* surface,
                                        const DlBoxBlurInset* blur,
                                        float offset_x, float offset_y);
void dl_replay_outer_shadow(ScratchArena* scratch,
                            ImageSurface* surface,
                            const DlOuterShadow* shadow);
void dl_replay_outer_shadow_at_offset(ScratchArena* scratch,
                                      ImageSurface* surface,
                                      const DlOuterShadow* shadow,
                                      float offset_x, float offset_y);

// ===== display_list_replay.hpp =====
// Replay the entire display list to the given vector context.
// surface is needed for direct-pixel operations (glyph, blit, opacity, etc.).
// dirty_tracker clips rendering to dirty regions for selective repaint.
void dl_replay(DisplayList* dl, RdtVector* vec,
               ImageSurface* surface, Bound* clip,
               ScratchArena* scratch, float scale,
               DirtyTracker* dirty_tracker);

// ===== tile_pool.h =====
// ==========================================================================
// TilePool — Tile grid partitioning and parallel rasterization pool.
//
// Phase 2 of the multi-threaded rendering proposal.
// Divides the page surface into fixed-size tiles and dispatches display list
// replay to a worker pool for parallel CPU rasterization.
// ==========================================================================


#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Tile size: 256 CSS pixels (512 physical at 2x).
// Chrome uses 256×256 — good balance of parallelism and overhead.
// ---------------------------------------------------------------------------

#define TILE_SIZE_CSS 256

// ---------------------------------------------------------------------------
// Tile — one rectangular region of the output surface
// ---------------------------------------------------------------------------

typedef struct Tile {
    int col, row;               // grid position
    float x, y, w, h;          // bounds in physical pixels (scaled)
    uint32_t* pixels;           // tile pixel buffer (owned, ABGR8888)
    int pixel_w, pixel_h;      // physical pixel dimensions
    int stride;                 // row stride in pixels (== pixel_w)
} Tile;

// ---------------------------------------------------------------------------
// TileGrid — the full grid of tiles covering the surface
// ---------------------------------------------------------------------------

typedef struct TileGrid {
    Tile* tiles;
    int cols, rows;
    int total;
    float scale;                // pixel_ratio (1.0, 2.0, etc.)
    int surface_w, surface_h;  // full surface dimensions in physical pixels
} TileGrid;

// Create a tile grid covering the given surface dimensions.
// scale = pixel_ratio (e.g. 2.0 for Retina).
void tile_grid_init(TileGrid* grid, int surface_w, int surface_h, float scale);

// Free all tile pixel buffers and the grid array.
void tile_grid_destroy(TileGrid* grid);

// Clear all tile pixels to a solid color (ABGR8888).
void tile_grid_clear(TileGrid* grid, uint32_t color);

// Composite all tiles into the final surface (memcpy rows).
void tile_grid_composite(TileGrid* grid, ImageSurface* surface);

// ---------------------------------------------------------------------------
// RenderPool — pthread-based worker pool for tile rasterization
// ---------------------------------------------------------------------------

typedef struct TileJob {
    Tile* tile;
    DisplayList* display_list;  // shared, read-only
    float scale;
    uint32_t bg_color;          // tile background clear color (ABGR8888)
} TileJob;

// Per-worker state (thread-local resources created once, reused across frames)
typedef struct WorkerState {
    RdtVector vec;              // thread-local ThorVG canvas
    ScratchArena scratch;       // thread-local scratch allocator
    Pool* pool;                 // thread-local memory pool (backing for arena)
    Arena* arena;               // thread-local arena (backing for scratch)
    bool initialized;
} WorkerState;

typedef struct RenderPool {
    pthread_t* threads;
    int thread_count;

    // job queue (simple array — all jobs submitted before workers start)
    TileJob* jobs;
    int job_count;

    // synchronisation: barrier-style — main thread signals start, waits for all done
    pthread_mutex_t mutex;
    pthread_cond_t  work_available;
    pthread_cond_t  all_done;
    int next_job;               // index into jobs array (guarded by mutex)
    int completed_jobs;
    bool shutdown;
} RenderPool;

// Create the pool with the given number of worker threads.
// threads=0 means auto-detect (use hardware_concurrency, cap at 8).
void render_pool_init(RenderPool* pool, int threads);

// Destroy the pool and join all threads.
void render_pool_destroy(RenderPool* pool);

// Submit a batch of tile jobs and wait for all to complete.
// The display list must be fully recorded and immutable.
void render_pool_dispatch(RenderPool* pool, TileJob* jobs, int count);

// ---------------------------------------------------------------------------
// Tile-aware replay — replays display list items intersecting a tile
// ---------------------------------------------------------------------------

// Replay the display list for a single tile.
// vec must be bound to the tile's pixel buffer.
// Coordinates are translated from page-absolute to tile-local.
void dl_replay_tile(DisplayList* dl, RdtVector* vec,
                    ImageSurface* tile_surface, ScratchArena* scratch,
                    float tile_x, float tile_y, float tile_w, float tile_h,
                    float scale);

#ifdef __cplusplus
}
#endif

// ===== retained_fields.hpp =====
inline void radiant_retain_font_family(FontProp* font, lam::PoolPtr<char> family) {
    lam::PersistentFieldRef<char, lam::PoolDomain> field(font->family);
    field.set(family);
}

inline void radiant_retain_font_family(FontProp* font, lam::GcPtr<char> family) {
    lam::PersistentFieldRef<char, lam::PoolDomain> field(font->family);
    field.set(family);
}

inline void radiant_clear_font_family(FontProp* font) {
    lam::PersistentFieldRef<char, lam::PoolDomain> field(font->family);
    field.clear();
}

inline void radiant_retain_background_image(BackgroundProp* background, lam::PoolPtr<char> image) {
    lam::PersistentFieldRef<char, lam::PoolDomain> field(background->image);
    field.set(image);
}

inline void radiant_clear_background_image(BackgroundProp* background) {
    lam::PersistentFieldRef<char, lam::PoolDomain> field(background->image);
    field.clear();
}

inline void radiant_retain_marker_text_content(MarkerProp* marker, lam::PoolPtr<char> text_content) {
    lam::PersistentFieldRef<char, lam::PoolDomain> field(marker->text_content);
    field.set(text_content);
}

inline void radiant_retain_image_source_path(ImageSurface* surface, lam::PoolPtr<char> source_path) {
    lam::PersistentFieldRef<char, lam::PoolDomain> field(surface->source_path);
    field.set(source_path);
}

inline void radiant_take_image_source_path(ImageSurface* surface, lam::SessionPtr<char>& source_path) {
    surface->source_path = lam::detach_session_buffer(source_path);
}

inline void radiant_clear_image_source_path(ImageSurface* surface) {
    surface->source_path = nullptr;
}

inline void radiant_retain_image_source_data(ImageSurface* surface, lam::PoolPtr<unsigned char> source_data, size_t len) {
    lam::PersistentFieldRef<unsigned char, lam::PoolDomain> field(surface->source_data);
    field.set(source_data);
    surface->source_data_len = len;
}

inline void radiant_take_image_source_data(ImageSurface* surface, lam::SessionPtr<unsigned char>& source_data, size_t len) {
    surface->source_data = lam::detach_session_buffer(source_data);
    surface->source_data_len = len;
}

inline void radiant_clear_image_source_data(ImageSurface* surface) {
    surface->source_data = nullptr;
    surface->source_data_len = 0;
}

// ===== rdt_video.h =====
// rdt_video.h — Platform-agnostic video playback API for Radiant
//
// Three-tier threading model:
//   Decode thread  → demux + decode + colour convert
//   Playback thread → PTS scheduling + audio output + A/V sync
//   Render thread   → polls latest frame via rdt_video_get_frame()
//
// macOS:  AVFoundation manages decode + playback internally.
// Windows: Media Foundation decode thread + WASAPI playback thread.
// Linux:  FFmpeg decode thread + PulseAudio/ALSA playback thread.

#ifndef RADIANT_RDT_VIDEO_API
#define RADIANT_RDT_VIDEO_API

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RdtVideo RdtVideo;

typedef enum {
    RDT_VIDEO_STATE_IDLE,
    RDT_VIDEO_STATE_LOADING,
    RDT_VIDEO_STATE_READY,
    RDT_VIDEO_STATE_PLAYING,
    RDT_VIDEO_STATE_PAUSED,
    RDT_VIDEO_STATE_ENDED,
    RDT_VIDEO_STATE_ERROR,
} RdtVideoState;

typedef struct {
    uint8_t*    pixels;     // RGBA 32bpp, caller-owned buffer
    int         width;
    int         height;
    int         stride;     // bytes per row
    double      pts;        // presentation timestamp (seconds)
} RdtVideoFrame;

typedef struct {
    void (*on_state_changed)(RdtVideo* video, RdtVideoState state, void* userdata);
    void (*on_frame_ready)(RdtVideo* video, void* userdata);
    void (*on_duration_known)(RdtVideo* video, double seconds, void* userdata);
    void (*on_video_size_known)(RdtVideo* video, int width, int height, void* userdata);
} RdtVideoCallbacks;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

RdtVideo*       rdt_video_create(const RdtVideoCallbacks* cb, void* userdata);
void            rdt_video_destroy(RdtVideo* video);

// ---------------------------------------------------------------------------
// Source — local file path only (web URLs deferred to future)
// ---------------------------------------------------------------------------

int             rdt_video_open_file(RdtVideo* video, const char* file_path);

// ---------------------------------------------------------------------------
// Layout rect — decode resolution capped to this size to limit memory.
// Call on layout change. Width/height in physical pixels.
// ---------------------------------------------------------------------------

void            rdt_video_set_layout_rect(RdtVideo* video, int width, int height);

// ---------------------------------------------------------------------------
// Playback control
// ---------------------------------------------------------------------------

void            rdt_video_play(RdtVideo* video);
void            rdt_video_pause(RdtVideo* video);
void            rdt_video_seek(RdtVideo* video, double seconds);
void            rdt_video_set_loop(RdtVideo* video, bool loop);

// ---------------------------------------------------------------------------
// Audio control
// ---------------------------------------------------------------------------

void            rdt_video_set_volume(RdtVideo* video, float volume);  // 0.0–1.0
void            rdt_video_set_muted(RdtVideo* video, bool muted);

// ---------------------------------------------------------------------------
// Query — all thread-safe, lock-free reads
// ---------------------------------------------------------------------------

RdtVideoState   rdt_video_get_state(RdtVideo* video);
double          rdt_video_get_current_time(RdtVideo* video);
double          rdt_video_get_duration(RdtVideo* video);
int             rdt_video_get_width(RdtVideo* video);   // intrinsic video width
int             rdt_video_get_height(RdtVideo* video);  // intrinsic video height
bool            rdt_video_has_audio(RdtVideo* video);
float           rdt_video_get_volume(RdtVideo* video);  // 0.0–1.0

// ---------------------------------------------------------------------------
// Frame retrieval — returns the latest decoded frame.
// Copies into caller-owned buffer. Returns 0 on success, -1 if no frame.
// The playback thread manages PTS scheduling; this always returns the current frame.
// ---------------------------------------------------------------------------

int             rdt_video_get_frame(RdtVideo* video, RdtVideoFrame* frame);

#ifdef __cplusplus
}
#endif

#endif // RADIANT_RDT_VIDEO_API

// ===== gif_player.h =====
#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct ImageSurface;
struct DirtyTracker;

// ============================================================================
// GIF Animation Player
// ============================================================================

typedef struct GifAnimation {
    GifFrames* frames;           // decoded frame data (owned, freed on destroy)
    int current_frame;           // index of currently displayed frame
    double frame_end_time;       // when to advance to next frame (absolute seconds)
    int loop_count;              // 0 = infinite (from GIF NETSCAPE extension)
    int loops_completed;         // number of loops finished so far

    // Target surface — pixel pointer is swapped on frame change
    struct ImageSurface* surface;
} GifAnimation;

// Create a GifAnimation from decoded frames and register with scheduler.
// Takes ownership of gif_frames (freed on destroy).
// Returns the animation instance, or NULL on failure.
AnimationInstance* gif_animation_create(AnimationScheduler* scheduler,
                                         struct ImageSurface* surface,
                                         GifFrames* gif_frames,
                                         double start_time,
                                         Pool* pool);

// Tick callback for GIF animation (called by scheduler).
void gif_animation_tick(AnimationInstance* anim, float t);

// Finish callback for GIF animation.
void gif_animation_finish(AnimationInstance* anim);

// Check if an image source (file path or URL) is an animated GIF (>1 frame).
// If so, loads all frames and returns the GifFrames*. Returns NULL if static.
GifFrames* gif_detect_animated(const char* path);

// Check if in-memory image data is an animated GIF (>1 frame).
// If so, loads all frames and returns the GifFrames*. Returns NULL if static.
GifFrames* gif_detect_animated_from_memory(const unsigned char* data, size_t length);

#ifdef __cplusplus
}
#endif

// ===== lottie_player.h =====
#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct ImageSurface;
struct DirtyTracker;

// ============================================================================
// Lottie Animation Player
// ============================================================================

typedef struct LottiePlayer {
    void* tvg_animation;        // Tvg_Animation (opaque, managed by ThorVG)
    void* tvg_canvas;           // Tvg_Canvas for rasterization (opaque)

    float total_frames;
    float frame_rate;
    float duration;             // seconds

    // Rendering target
    uint32_t* pixels;           // ABGR8888 buffer (owned by this player)
    int width, height;

    // Target image surface — pixel pointer is swapped on frame change
    struct ImageSurface* surface;

    bool loop;
    bool playing;
} LottiePlayer;

// Create a LottiePlayer from a file path and register with scheduler.
// Returns the animation instance, or NULL if the file is not a valid Lottie.
AnimationInstance* lottie_player_create_from_file(AnimationScheduler* scheduler,
                                                   struct ImageSurface* surface,
                                                   const char* path,
                                                   int render_width, int render_height,
                                                   double start_time,
                                                   Pool* pool);

// Create a LottiePlayer from in-memory data and register with scheduler.
// Returns the animation instance, or NULL if the data is not a valid Lottie.
AnimationInstance* lottie_player_create_from_data(AnimationScheduler* scheduler,
                                                   struct ImageSurface* surface,
                                                   const char* data, size_t length,
                                                   int render_width, int render_height,
                                                   double start_time,
                                                   Pool* pool);

// Tick callback for Lottie animation (called by scheduler).
void lottie_animation_tick(AnimationInstance* anim, float t);

// Finish callback for Lottie animation.
void lottie_animation_finish(AnimationInstance* anim);

// Detect if a file path looks like a Lottie file (by extension).
bool lottie_detect_by_path(const char* path);

// Detect if in-memory data is a Lottie JSON file (quick heuristic check).
bool lottie_detect_by_content(const unsigned char* data, size_t length);

#ifdef __cplusplus
}
#endif

// ===== video_frame_wake.h =====
struct DocState;

typedef void (*RadiantVideoWakeCallback)(void* user_data);

void radiant_video_set_wake_callback(RadiantVideoWakeCallback callback, void* user_data);
void radiant_video_notify_frame_ready(DocState* state);

// ===== stacking_order.hpp =====
bool radiant_stack_is_positive_z_positioned(View* view);
bool radiant_stack_is_out_of_flow_positioned(View* view);
bool radiant_stack_is_deferred_from_normal_flow(View* view);

ArrayList* radiant_stack_collect_positive_z_descendants(View* first_child, const char* log_prefix);
ArrayList* radiant_stack_collect_positioned_children(ViewBlock* block, const char* log_prefix);
void radiant_stack_sort_in_paint_order(ArrayList* views);

// ===== render_paint_block.hpp =====
struct ViewBlock;

typedef struct RenderPaintBlockOps {
    void* ctx;
    bool (*begin)(void* ctx, ViewBlock* block, void** phase);
    bool (*paint_self)(void* ctx, ViewBlock* block, void* phase);
    double (*paint_children)(void* ctx, ViewBlock* block, void* phase);
    void (*finish)(void* ctx, ViewBlock* block, void* phase);
} RenderPaintBlockOps;

typedef struct RenderPaintBlockResult {
    double children_time;
    bool painted;
} RenderPaintBlockResult;

RenderPaintBlockResult render_paint_block_run(RenderPaintBlockOps* ops,
                                              ViewBlock* block);

// ===== render_paint_boundary.hpp =====
// Emits a complete simple CSS boundary into PaintIR.
// Returns false when the boundary needs a richer backend-specific fallback.
bool render_paint_boundary_emit_simple(PaintList* paint_list, ViewBlock* view,
                                       float x, float y);
bool render_paint_boundary_emit_outer_shadows(PaintList* paint_list, ViewBlock* view,
                                              float x, float y);

typedef struct BoundaryLinearGradientPaint {
    RdtPath* path;
    float x1;
    float y1;
    float x2;
    float y2;
    RdtGradientStop* stops;
    int stop_count;
} BoundaryLinearGradientPaint;

typedef struct BoundaryRadialGradientPaint {
    RdtPath* path;
    float cx;
    float cy;
    float r;
    RdtGradientStop* stops;
    int stop_count;
} BoundaryRadialGradientPaint;

bool render_paint_boundary_build_linear_gradient(ViewBlock* view, float x, float y,
                                                 RdtGradientStop* stops,
                                                 int stop_capacity,
                                                 BoundaryLinearGradientPaint* out);
bool render_paint_boundary_build_radial_gradient(ViewBlock* view, float x, float y,
                                                 RdtGradientStop* stops,
                                                 int stop_capacity,
                                                 BoundaryRadialGradientPaint* out);

// ===== render_paint_gateway.hpp =====
typedef struct PaintRecordTarget {
    PaintList* paint_list;
    DisplayList* display_list;
    const char* log_prefix;
} PaintRecordTarget;

static inline bool paint_record_ready(PaintRecordTarget* target) {
    return target && target->paint_list && target->display_list;
}

static inline void paint_record_lower_pending(PaintRecordTarget* target) {
    paint_ir_lower_raster_fragment(target->paint_list, target->display_list);
    paint_list_clear(target->paint_list);
}

static inline void paint_record_missing(PaintRecordTarget* target, const char* op) {
    log_error("[%s] %s called without PaintIR/display-list targets",
              target && target->log_prefix ? target->log_prefix : "PAINT_GATEWAY",
              op ? op : "paint op");
}

static inline void paint_record_fill_rect(PaintRecordTarget* target, const char* op,
                                          float x, float y, float w, float h, Color color) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_fill_rect(target->paint_list, x, y, w, h, color);
    paint_record_lower_pending(target);
}

static inline void paint_record_fill_rounded_rect(PaintRecordTarget* target, const char* op,
                                                  float x, float y, float w, float h,
                                                  float rx, float ry, Color color) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_fill_rounded_rect(target->paint_list, x, y, w, h, rx, ry, color);
    paint_record_lower_pending(target);
}

static inline void paint_record_fill_path(PaintRecordTarget* target, const char* op,
                                          RdtPath* path, Color color,
                                          RdtFillRule rule, const RdtMatrix* transform) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_fill_path(target->paint_list, path, color, rule, transform);
    paint_record_lower_pending(target);
}

static inline void paint_record_stroke_path(PaintRecordTarget* target, const char* op,
                                            RdtPath* path, Color color, float width,
                                            RdtStrokeCap cap, RdtStrokeJoin join,
                                            const float* dash_array, int dash_count,
                                            float dash_phase, const RdtMatrix* transform) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_stroke_path(target->paint_list, path, color, width, cap, join,
                      dash_array, dash_count, dash_phase, transform);
    paint_record_lower_pending(target);
}

static inline void paint_record_fill_linear_gradient(PaintRecordTarget* target, const char* op,
                                                     RdtPath* path,
                                                     float x1, float y1, float x2, float y2,
                                                     const RdtGradientStop* stops, int stop_count,
                                                     RdtFillRule rule,
                                                     const RdtMatrix* transform,
                                                     const RdtMatrix* gradient_transform) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_fill_linear_gradient(target->paint_list, path, x1, y1, x2, y2,
                               stops, stop_count, rule, transform,
                               gradient_transform);
    paint_record_lower_pending(target);
}

static inline void paint_record_fill_radial_gradient(PaintRecordTarget* target, const char* op,
                                                     RdtPath* path, float cx, float cy, float r,
                                                     const RdtGradientStop* stops, int stop_count,
                                                     RdtFillRule rule,
                                                     const RdtMatrix* transform,
                                                     const RdtMatrix* gradient_transform) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_fill_radial_gradient(target->paint_list, path, cx, cy, r,
                               stops, stop_count, rule, transform,
                               gradient_transform);
    paint_record_lower_pending(target);
}

static inline void paint_record_draw_image(PaintRecordTarget* target, const char* op,
                                           const uint32_t* pixels,
                                           int src_w, int src_h, int src_stride,
                                           float dst_x, float dst_y, float dst_w, float dst_h,
                                           uint8_t opacity, const RdtMatrix* transform,
                                           ImageSurface* resource_owner) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_draw_image(target->paint_list, pixels, src_w, src_h, src_stride,
                     dst_x, dst_y, dst_w, dst_h, opacity, transform,
                     resource_owner);
    paint_record_lower_pending(target);
}

static inline void paint_record_draw_glyph(PaintRecordTarget* target, const char* op,
                                           GlyphBitmap* bitmap, int x, int y,
                                           Color color, bool is_color_emoji,
                                           const Bound* clip, const RdtMatrix* transform,
                                           uint64_t resource_generation) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_draw_glyph(target->paint_list, bitmap, x, y, color, is_color_emoji,
                     clip, transform, resource_generation);
    paint_record_lower_pending(target);
}

static inline void paint_record_draw_picture(PaintRecordTarget* target, const char* op,
                                             RdtPicture* picture, uint8_t opacity,
                                             const RdtMatrix* transform) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_draw_picture(target->paint_list, picture, opacity, transform);
    paint_record_lower_pending(target);
}

static inline void paint_record_video_placeholder(PaintRecordTarget* target, const char* op,
                                                  void* video,
                                                  float dst_x, float dst_y,
                                                  float dst_w, float dst_h,
                                                  int object_fit, const Bound* clip,
                                                  uint64_t video_generation) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_video_placeholder(target->paint_list, video, dst_x, dst_y, dst_w, dst_h,
                            object_fit, clip, video_generation);
    paint_record_lower_pending(target);
}

static inline void paint_record_webview_layer_placeholder(PaintRecordTarget* target,
                                                         const char* op, void* surface,
                                                         float dst_x, float dst_y,
                                                         float dst_w, float dst_h,
                                                         const Bound* clip,
                                                         uint64_t surface_generation) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_webview_layer_placeholder(target->paint_list, surface, dst_x, dst_y,
                                    dst_w, dst_h, clip, surface_generation);
    paint_record_lower_pending(target);
}

static inline void paint_record_push_clip(PaintRecordTarget* target, const char* op,
                                          RdtPath* path, const RdtMatrix* transform) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_push_clip(target->paint_list, path, transform);
    paint_record_lower_pending(target);
}

static inline void paint_record_pop_clip(PaintRecordTarget* target, const char* op) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_pop_clip(target->paint_list);
    paint_record_lower_pending(target);
}

static inline void paint_record_save_backdrop(PaintRecordTarget* target, const char* op,
                                              int x0, int y0, int w, int h) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_save_backdrop(target->paint_list, x0, y0, w, h);
    paint_record_lower_pending(target);
}

static inline void paint_record_composite_opacity(PaintRecordTarget* target, const char* op,
                                                  int x0, int y0, int w, int h,
                                                  float opacity,
                                                  bool premultiplied_source) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_composite_opacity(target->paint_list, x0, y0, w, h,
                            opacity, premultiplied_source);
    paint_record_lower_pending(target);
}

static inline void paint_record_apply_blend_mode(PaintRecordTarget* target, const char* op,
                                                 int x0, int y0, int w, int h,
                                                 int blend_mode) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_apply_blend_mode(target->paint_list, x0, y0, w, h, blend_mode);
    paint_record_lower_pending(target);
}

static inline void paint_record_apply_filter(PaintRecordTarget* target, const char* op,
                                             float x, float y, float w, float h,
                                             void* filter, const Bound* clip) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_apply_filter(target->paint_list, x, y, w, h, filter, clip);
    paint_record_lower_pending(target);
}

static inline void paint_record_box_blur_region(PaintRecordTarget* target, const char* op,
                                                int rx, int ry, int rw, int rh,
                                                float blur_radius,
                                                int clip_type, const float* clip_params,
                                                int exclude_type, const float* exclude_params,
                                                bool premultiply_source,
                                                bool tint_source, Color tint_color) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_box_blur_region(target->paint_list, rx, ry, rw, rh, blur_radius,
                          clip_type, clip_params, exclude_type, exclude_params,
                          premultiply_source, tint_source, tint_color);
    paint_record_lower_pending(target);
}

static inline void paint_record_box_blur_inset(PaintRecordTarget* target, const char* op,
                                               int rx, int ry, int rw, int rh,
                                               int pad, float blur_radius,
                                               uint32_t bg_color) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_box_blur_inset(target->paint_list, rx, ry, rw, rh, pad,
                         blur_radius, bg_color);
    paint_record_lower_pending(target);
}

static inline void paint_record_shadow_clip_save(PaintRecordTarget* target, const char* op,
                                                 int rx, int ry, int rw, int rh) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_shadow_clip_save(target->paint_list, rx, ry, rw, rh);
    paint_record_lower_pending(target);
}

static inline void paint_record_shadow_clip_restore(PaintRecordTarget* target, const char* op,
                                                    int exclude_type,
                                                    const float* exclude_params,
                                                    int save_rx, int save_ry,
                                                    int save_rw, int save_rh,
                                                    int restore_inside) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_shadow_clip_restore(target->paint_list, exclude_type, exclude_params,
                              save_rx, save_ry, save_rw, save_rh,
                              restore_inside);
    paint_record_lower_pending(target);
}

static inline void paint_record_outer_shadow(PaintRecordTarget* target, const char* op,
                                             float shadow_x, float shadow_y,
                                             float shadow_w, float shadow_h,
                                             float sr_tl, float sr_tr,
                                             float sr_br, float sr_bl,
                                             Color color, float blur_radius,
                                             int exclude_type, const float* exclude_params,
                                             int clip_type, const float* clip_params) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_outer_shadow(target->paint_list,
                       shadow_x, shadow_y, shadow_w, shadow_h,
                       sr_tl, sr_tr, sr_br, sr_bl,
                       color, blur_radius,
                       exclude_type, exclude_params,
                       clip_type, clip_params);
    paint_record_lower_pending(target);
}

static inline void paint_record_fill_surface_rect(PaintRecordTarget* target, const char* op,
                                                  float x, float y, float w, float h,
                                                  uint32_t color, const Bound* clip,
                                                  ClipShape** clip_shapes,
                                                  int clip_depth) {
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_fill_surface_rect(target->paint_list, x, y, w, h,
                            color, clip, clip_shapes, clip_depth);
    paint_record_lower_pending(target);
}

static inline void paint_record_blit_surface_scaled(PaintRecordTarget* target, const char* op,
                                                    ImageSurface* src,
                                                    float dst_x, float dst_y,
                                                    float dst_w, float dst_h,
                                                    int scale_mode,
                                                    const Bound* clip,
                                                    ClipShape** clip_shapes,
                                                    int clip_depth,
                                                    uint8_t opacity) {
    uint64_t generation = src ? src->generation : 0;
    if (!paint_record_ready(target)) {
        paint_record_missing(target, op);
        return;
    }
    paint_blit_surface_scaled(target->paint_list, src, dst_x, dst_y,
                              dst_w, dst_h, scale_mode, clip, clip_shapes,
                              clip_depth, opacity, generation);
    paint_record_lower_pending(target);
}

// ===== render_painter.hpp =====
struct RenderContext;
typedef struct RenderContext RenderContext;

// ---------------------------------------------------------------------------
// rc_* — Render-context drawing wrappers (record through PaintIR/DisplayList)
// ---------------------------------------------------------------------------

void rc_fill_rect(RenderContext* rdcon, float x, float y, float w, float h, Color color);
void rc_fill_rounded_rect(RenderContext* rdcon, float x, float y, float w, float h,
                          float rx, float ry, Color color);
void rc_fill_path(RenderContext* rdcon, RdtPath* path, Color color,
                  RdtFillRule rule, const RdtMatrix* transform);
void rc_stroke_path(RenderContext* rdcon, RdtPath* path, Color color, float width,
                    RdtStrokeCap cap, RdtStrokeJoin join,
                    const float* dash_array, int dash_count,
                    const RdtMatrix* transform, float dash_phase = 0);
void rc_fill_linear_gradient(RenderContext* rdcon, RdtPath* path,
                             float x1, float y1, float x2, float y2,
                             const RdtGradientStop* stops, int stop_count,
                             RdtFillRule rule, const RdtMatrix* transform);
void rc_fill_radial_gradient(RenderContext* rdcon, RdtPath* path,
                             float cx, float cy, float r,
                             const RdtGradientStop* stops, int stop_count,
                             RdtFillRule rule, const RdtMatrix* transform);
void rc_draw_image(RenderContext* rdcon, const uint32_t* pixels,
                   int src_w, int src_h, int src_stride,
                   float dst_x, float dst_y, float dst_w, float dst_h,
                   uint8_t opacity, const RdtMatrix* transform,
                   ImageSurface* resource_owner = nullptr);
void rc_draw_glyph(RenderContext* rdcon, GlyphBitmap* bitmap, int x, int y,
                   Color color, bool is_color_emoji, const Bound* clip,
                   const RdtMatrix* transform, uint64_t resource_generation);
void rc_draw_picture(RenderContext* rdcon, RdtPicture* picture,
                     uint8_t opacity, const RdtMatrix* transform);
void rc_video_placeholder(RenderContext* rdcon, void* video,
                          float dst_x, float dst_y, float dst_w, float dst_h,
                          int object_fit, const Bound* clip,
                          uint64_t video_generation);
void rc_webview_layer_placeholder(RenderContext* rdcon, void* surface,
                                  float dst_x, float dst_y, float dst_w, float dst_h,
                                  const Bound* clip,
                                  uint64_t surface_generation);
void rc_push_clip(RenderContext* rdcon, RdtPath* clip_path, const RdtMatrix* transform);
void rc_pop_clip(RenderContext* rdcon);
void rc_save_backdrop(RenderContext* rdcon, int x0, int y0, int w, int h);
void rc_composite_opacity(RenderContext* rdcon, int x0, int y0, int w, int h,
                          float opacity, bool premultiplied_source = false);
void rc_apply_blend_mode(RenderContext* rdcon, int x0, int y0, int w, int h,
                         int blend_mode);
void rc_apply_filter(RenderContext* rdcon, float x, float y, float w, float h,
                     void* filter, const Bound* clip);
void rc_box_blur_region(RenderContext* rdcon, int rx, int ry, int rw, int rh,
                        float blur_radius, int clip_type, const float* clip_params,
                        int exclude_type = 0, const float* exclude_params = nullptr,
                        bool premultiply_source = false,
                        bool tint_source = false, Color tint_color = Color{});
void rc_box_blur_inset(RenderContext* rdcon, int rx, int ry, int rw, int rh,
                       int pad, float blur_radius, uint32_t bg_color);
void rc_shadow_clip_save(RenderContext* rdcon, int rx, int ry, int rw, int rh);
void rc_shadow_clip_restore(RenderContext* rdcon, int exclude_type, const float* exclude_params,
                            int save_rx, int save_ry, int save_rw, int save_rh,
                            int restore_inside);
void rc_outer_shadow(RenderContext* rdcon,
                     float shadow_x, float shadow_y, float shadow_w, float shadow_h,
                     float sr_tl, float sr_tr, float sr_br, float sr_bl,
                     Color color, float blur_radius,
                     int exclude_type, const float* exclude_params,
                     int clip_type, const float* clip_params);

void rc_fill_surface_rect(RenderContext* rdcon, ImageSurface* surface,
                          Rect* rect, uint32_t color, Bound* clip,
                          ClipShape** clip_shapes, int clip_depth);

// ---------------------------------------------------------------------------
// Feature-facing painter helpers
// ---------------------------------------------------------------------------

void render_painter_draw_picture_rect(RenderContext* rdcon, RdtPicture* picture,
                                      Rect* dst_rect, Bound* clip,
                                      uint8_t opacity);
void render_painter_draw_pixels_rect(RenderContext* rdcon, const uint32_t* pixels,
                                     int src_w, int src_h, int src_stride,
                                     Rect* dst_rect, Bound* clip,
                                     uint8_t opacity,
                                     ImageSurface* resource_owner = nullptr);
void render_painter_fill_surface_rect(RenderContext* rdcon, ImageSurface* surface,
                                      Rect* rect, uint32_t color, Bound* clip,
                                      ClipShape** clip_shapes, int clip_depth);
void render_painter_blit_surface_scaled(RenderContext* rdcon,
                                        ImageSurface* src, Rect* src_rect,
                                        ImageSurface* dst, Rect* dst_rect, Bound* clip,
                                        ScaleMode scale_mode,
                                        ClipShape** clip_shapes, int clip_depth,
                                        uint8_t opacity = 255);

// ===== render_profiler.hpp =====
struct DocState;
struct UiContext;

typedef enum RenderProfileZone {
    RENDER_PROFILE_GLYPH_LOAD,
    RENDER_PROFILE_GLYPH_DRAW,
    RENDER_PROFILE_SETUP_FONT,
    RENDER_PROFILE_BOUND,
    RENDER_PROFILE_TEXT,
    RENDER_PROFILE_IMAGE,
    RENDER_PROFILE_SVG,
    RENDER_PROFILE_FILTER,
    RENDER_PROFILE_CLIP,
    RENDER_PROFILE_OPACITY,
    RENDER_PROFILE_BLEND,
    RENDER_PROFILE_BLOCK,
    RENDER_PROFILE_INLINE,
    RENDER_PROFILE_DISPATCH,
    RENDER_PROFILE_BLOCK_SELF,
    RENDER_PROFILE_CHILDREN,
    RENDER_PROFILE_OVERFLOW_CLIP,
    RENDER_PROFILE_FONT_METRICS,
} RenderProfileZone;

typedef struct RenderProfiler {
    int64_t glyph_count;
    int64_t draw_count;
    double load_glyph_time;
    double draw_glyph_time;
    int64_t setup_font_count;
    double setup_font_time;

    double bound_time;
    int64_t bound_count;
    double text_total_time;
    int64_t text_count;
    double image_time;
    int64_t image_count;
    double svg_time;
    int64_t svg_count;
    double filter_time;
    int64_t filter_count;
    double clip_time;
    int64_t clip_count;
    double opacity_time;
    int64_t opacity_count;
    double blend_time;
    int64_t blend_count;
    int64_t block_count;
    double inline_time;
    int64_t inline_count;
    int64_t dispatch_count;
    double block_self_time;
    double children_time;
    double overflow_clip_time;
    int64_t overflow_clip_count;
    double font_metrics_time;
    int64_t font_metrics_count;
} RenderProfiler;

typedef struct RenderPathTrace {
    const char* target;
    const char* replay_mode;
    const char* backend_name;
    bool display_list_recorded;
    bool paint_ir_enabled;
    bool selective;
    bool tiled_replay;
    bool large_tiled_export;
    bool backend_vector_paths;
    bool backend_gradients;
    bool backend_nested_clips;
    bool backend_picture_svg;
    bool backend_opacity_group;
    bool backend_blend_modes;
    bool backend_gaussian_blur;
    bool backend_color_matrix_filters;
    bool backend_native_text_runs;
    bool backend_vector_batching;
    bool backend_tile_offsets;
    int display_list_items;
    int tile_count;
    int thread_count;
    int surface_width;
    int surface_height;
    int retained_capture_candidates;
    int retained_captured;
    int retained_skipped_non_retainable;
    int retained_copy_failed;
    int retained_reuse_hits;
    int retained_reuse_misses;
    int retained_reuse_rejected_resources;
    int retained_reuse_rejected_dirty;
    int paint_ir_commands;
    int paint_ir_emitted;
    int paint_ir_fallbacks;
    int paint_ir_unsupported;
} RenderPathTrace;

void render_profiler_reset(RenderProfiler* profiler);
double render_profiler_now_ms();
void render_profiler_increment(RenderProfiler* profiler, RenderProfileZone zone);
void render_profiler_add_time(RenderProfiler* profiler, RenderProfileZone zone, double ms);
void render_profiler_add_sample(RenderProfiler* profiler, RenderProfileZone zone, double ms);
void render_profiler_log(RenderProfiler* profiler);
void render_profiler_write_record_stderr(double render_ms, int surface_width,
    int surface_height, int display_list_items);
void render_profiler_write_counters_stderr(RenderProfiler* profiler);
void render_profiler_write_replay_stderr(double replay_ms, int item_count);
void render_profiler_write_tiled_replay_stderr(double replay_ms, int item_count,
    int tile_count, int thread_count);
void render_profiler_emit_event(RenderProfiler* profiler, struct UiContext* uicon,
    struct DocState* state, double record_ms, double replay_ms, double total_ms,
    int item_count, bool selective, bool tiled, int tile_count, int thread_count);
void render_profiler_emit_path_trace(RenderProfiler* profiler, struct UiContext* uicon,
    struct DocState* state, const RenderPathTrace* trace);

// ===== render_state.hpp =====
struct RenderContext;

typedef struct RenderTransformScope {
    RenderContext* context;
    RdtMatrix previous_transform;
    bool previous_has_transform;
    float previous_perspective_distance;
    float previous_perspective_origin_x;
    float previous_perspective_origin_y;
    bool active;
} RenderTransformScope;

RenderTransformScope render_state_push_transform(RenderContext* rdcon, ViewBlock* block,
                                                 const BlockBlot* parent_block);
void render_state_pop_transform(RenderTransformScope* scope);
const RdtMatrix* render_state_current_transform(RenderContext* rdcon);

// ===== render_path.hpp =====
struct RenderContext;
typedef struct RenderContext RenderContext;

RdtPath* render_path_create_rounded_rect(Rect rect, const Corner* radius);
RdtPath* render_path_create_clip_path(RenderContext* rdcon);

// ===== render_clip.hpp =====
struct RenderContext;
struct ViewBlock;
typedef struct Bound Bound;

typedef struct RenderClipScope {
    ClipShape* shape;
    bool active;
    bool pushed_shape;
    bool owns_shape;
} RenderClipScope;

RenderClipScope render_clip_push_css_scope(RenderContext* rdcon, ViewBlock* block,
                                           float parent_x, float parent_y, float scale);
RenderClipScope render_clip_push_rect_scope(RenderContext* rdcon, const Bound* clip);
RenderClipScope render_clip_push_overflow_scope(RenderContext* rdcon);
void render_clip_pop_scope(RenderContext* rdcon, RenderClipScope* scope);

// ===== render_composite.hpp =====
uint32_t render_composite_blend_pixel(uint32_t backdrop, uint32_t source, CssEnum blend_mode);

bool render_composite_copy_backdrop(ImageSurface* surface, uint32_t* backdrop,
                                    int x0, int y0, int width, int height,
                                    bool clear_surface);
void render_composite_apply_blend(ImageSurface* surface, const uint32_t* backdrop,
                                  int x0, int y0, int width, int height,
                                  CssEnum blend_mode);
void render_composite_source_over_premul(ImageSurface* surface, const uint32_t* backdrop,
                                         int x0, int y0, int width, int height);
void render_composite_opacity(ImageSurface* surface, const uint32_t* backdrop,
                              int x0, int y0, int width, int height,
                              float opacity);

// ===== render_filter.hpp =====
/**
 * CSS Filter Rendering
 *
 * Implements CSS filter effects that can be applied to elements.
 * Color manipulation filters (grayscale, brightness, contrast, etc.) are applied
 * to the rendered pixel data after the element and its children are rendered.
 * The blur() filter uses a software 3-pass box blur approximation of Gaussian blur.
 */

/**
 * Apply CSS filter effects to a rendered region
 *
 * @param surface The image surface containing rendered pixels
 * @param filter The filter property chain to apply
 * @param rect The rectangular region to apply filters to
 * @param clip The clipping bounds
 */
void apply_css_filters(ScratchArena* sa, ImageSurface* surface, FilterProp* filter, Rect* rect, Bound* clip);
bool render_filter_apply_with_backend(const RenderBackendCaps* caps,
                                      ScratchArena* sa,
                                      ImageSurface* surface,
                                      FilterProp* filter,
                                      Rect* rect,
                                      Bound* clip);

// ===== render_background.hpp =====
// Background rendering functions
void render_background(RenderContext* rdcon, ViewBlock* view, Rect rect);

// Box shadow rendering
void render_box_shadow(RenderContext* rdcon, ViewBlock* view, Rect rect);
void render_box_shadow_inset(RenderContext* rdcon, ViewBlock* view, Rect rect);

// Software Gaussian blur (3-pass box blur approximation)
// Can be used by box-shadow, text-shadow, and filter:blur()
void box_blur_region(ScratchArena* sa, ImageSurface* surface, int rx, int ry, int rw, int rh, float blur_radius);

// Convert straight-alpha ABGR pixels to premultiplied-alpha ABGR in-place.
// Use before isolated-source blurs that will later composite with premul src-over.
void premultiply_surface_region(ImageSurface* surface, int rx, int ry, int rw, int rh);

// Preserve a known solid SourceGraphic color on an isolated premultiplied-alpha
// region. Used by SVG filters so a blurred solid shape remains colored rather
// than becoming an alpha-only shadow.
void tint_premultiplied_surface_region(ImageSurface* surface, int rx, int ry, int rw, int rh,
                                       Color color);

// Inset box-shadow blur: blur in temp buffer with bg_color in padding area,
// then copy inner rect back. Surface outside element is never modified.
void box_blur_region_inset(ScratchArena* sa, ImageSurface* surface,
                           int rx, int ry, int rw, int rh,
                           int pad, float blur_radius, uint32_t bg_color);

// Outer box-shadow rendering: rasterise the shadow rounded rect into a private
// temp buffer (premultiplied), apply 3-pass box blur to that buffer, then
// composite over the surface using src-over.  Pixels inside the element's
// border-box (exclude_shape) are skipped per CSS spec; the surface outside the
// shadow path is never read by the blur kernel, so sibling element pixels are
// not contaminated by shadow blur.
void render_outer_shadow_blur_composite(
    ScratchArena* sa, ImageSurface* surface,
    float shadow_x, float shadow_y, float shadow_w, float shadow_h,
    float sr_tl, float sr_tr, float sr_br, float sr_bl,
    Color shadow_color, float blur_radius,
    int exclude_type, const float* exclude_params,
    int clip_type, const float* clip_params);

// ===== render_border.hpp =====
// Border rendering functions
void render_border(RenderContext* rdcon, ViewBlock* view, Rect rect);
bool corner_has_radius(const Corner* radius);
void constrain_corner_radii(Corner* radius, float width, float height);
void constrain_border_radii(BorderProp* border, float width, float height);
void resolve_border_radius_percentages(Corner* radius, float width, float height);

// Outline rendering
void render_outline(RenderContext* rdcon, ViewBlock* view, Rect rect);

// ===== render_columns.hpp =====
struct RenderContext;

void render_column_rules(struct RenderContext* rdcon, ViewBlock* block);

// ===== render_effects.hpp =====
typedef struct RenderEffectBackdrop {
    RenderContext* context;
    uint32_t* pixels;
    int x;
    int y;
    int width;
    int height;
    bool active;
} RenderEffectBackdrop;

typedef struct RenderEffectGroup {
    RenderContext* context;
    RenderEffectBackdrop mix_blend_backdrop;
    RenderEffectBackdrop opacity_backdrop;
    RenderEffectBackdrop filter_backdrop;
    Rect filter_rect;
    CssEnum mix_blend_mode;
    float opacity;
    bool has_opacity_group;
    bool has_filter_backdrop;
    bool has_filter;
    bool has_backdrop_filter;
} RenderEffectGroup;

RenderEffectGroup render_effect_group_begin(RenderContext* rdcon,
                                            ViewBlock* block,
                                            const BlockBlot* parent_block);
bool render_effect_group_finish(RenderEffectGroup* group,
                                ViewBlock* block,
                                Bound* clip);

// ===== render_export_support.hpp =====
int ui_context_init(UiContext* uicon, bool headless);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);

void layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
void view_pool_destroy(ViewTree* tree);
void view_pool_reset_retained(ViewTree* tree);
void view_pool_release_detached_subtree(DomNode* root);
void image_cache_cleanup(UiContext* uicon);

void calculate_content_bounds(View* view, int* max_x, int* max_y);

// ===== render_form.hpp =====
struct DocState;
struct RenderContext;

void render_simple_string(RenderContext* rdcon, const char* text, float x, float y,
                          FontProp* font, Color color);
void render_form_control(RenderContext* rdcon, ViewBlock* block);
void render_select_dropdown(RenderContext* rdcon, ViewBlock* select, DocState* state);

// ===== render_glyph.hpp =====
void draw_glyph(RenderContext* rdcon, GlyphBitmap* bitmap, int x, int y);

// ===== render_img.hpp =====
// Function declarations for image rendering
void save_surface_to_png(ImageSurface* surface, const char* filename);
void save_surface_to_jpeg(ImageSurface* surface, const char* filename, int quality);
int render_html_to_png(const char* html_file, const char* png_file,
                       int viewport_width, int viewport_height,
                       float scale = 1.0f, float pixel_ratio = 1.0f);
int render_html_to_jpeg(const char* html_file, const char* jpeg_file, int quality,
                        int viewport_width, int viewport_height,
                        float scale = 1.0f, float pixel_ratio = 1.0f);

// Render existing UiContext with state (caret/selection) to image file
int render_uicontext_to_png(UiContext* uicon, const char* png_file);
int render_uicontext_to_svg(UiContext* uicon, const char* svg_file);

// Batch render command: reads jobs from stdin, shares one UiContext
int cmd_render_batch(int argc, char** argv);

// ===== render_list.hpp =====
struct RenderContext;

void render_marker_view(struct RenderContext* rdcon, ViewSpan* marker);
void render_list_bullet(struct RenderContext* rdcon, ViewBlock* list_item);
void render_litem_view(struct RenderContext* rdcon, ViewBlock* list_item);
void render_list_view(struct RenderContext* rdcon, ViewBlock* view);

// ===== render_media.hpp =====
struct RenderContext;

bool render_media_rasterize_svg_picture(ImageSurface* surface, int target_width,
                                        int target_height);
void render_image_view(struct RenderContext* rdcon, ViewBlock* view);
void render_video_content(struct RenderContext* rdcon, ViewBlock* view);
bool render_media_is_webview_layer(ViewBlock* view);
void render_webview_layer_content(struct RenderContext* rdcon, ViewBlock* view);

// ===== render_output.hpp =====
typedef struct ImageSurface ImageSurface;
typedef struct UiContext UiContext;
typedef struct ViewTree ViewTree;

typedef enum RenderOutputKind {
    RENDER_OUTPUT_SCREEN,
    RENDER_OUTPUT_PNG,
    RENDER_OUTPUT_JPEG,
    RENDER_OUTPUT_TILED_PNG,
    RENDER_OUTPUT_PDF,
    RENDER_OUTPUT_SVG
} RenderOutputKind;

typedef struct RenderOutputTarget {
    RenderOutputKind kind;
    const char* output_file;
    ImageSurface* surface;
    int width;
    int height;
    int viewport_width;
    int viewport_height;
    int jpeg_quality;
    float scale;
    float pixel_ratio;
} RenderOutputTarget;

void render_output_target_init(RenderOutputTarget* target, RenderOutputKind kind,
                               const char* output_file);
int render_output_render_view_tree_to_target(UiContext* uicon, ViewTree* view_tree,
                                             RenderOutputTarget* target);
int render_html_to_output_target(const char* html_file, const char* output_file,
                                 int viewport_width, int viewport_height,
                                 float scale, float pixel_ratio,
                                 int jpeg_quality);

// ===== render_overlay.hpp =====
struct RenderContext;

void render_ui_overlays(struct RenderContext* rdcon, DocState* state);

// ===== render_pdf.hpp =====
// Main function to render HTML to PDF
int render_html_to_pdf(const char* html_file, const char* pdf_file,
                       int viewport_width, int viewport_height,
                       float scale = 1.0f);

// ===== render_raster.hpp =====
typedef struct RasterPaintContext {
    ImageSurface* surface;
    Bound* clip;
    ClipShape** clip_shapes;
    int clip_depth;
} RasterPaintContext;

void raster_fill_rect(RasterPaintContext* ctx, Rect* rect, uint32_t color);
void raster_blit_surface_scaled(RasterPaintContext* ctx, ImageSurface* src, Rect* src_rect,
                                Rect* dst_rect, ScaleMode scale_mode, uint8_t opacity = 255);
void raster_blit_pixels_scaled(RasterPaintContext* ctx, const uint32_t* pixels,
                               int src_w, int src_h, int src_stride,
                               Rect* dst_rect, ScaleMode scale_mode, uint8_t opacity = 255);

// ===== render_selection.hpp =====
bool render_selection_contains_view(DocState* state, View* view);

// ===== render_svg.hpp =====
// Main function to layout HTML and render to SVG
int render_html_to_svg(const char* html_file, const char* svg_file,
                       int viewport_width, int viewport_height,
                       float scale = 1.0f);

// Function to render a view tree to SVG string
char* render_view_tree_to_svg(UiContext* uicon, View* root_view,
                              int width, int height, DocState* state = nullptr);

// Function to save SVG content to file
bool save_svg_to_file(const char* svg_content, const char* filename);

// ===== render_svg_inline.hpp =====
/**
 * render_svg_inline.hpp - Inline SVG Rendering via PaintIR/DisplayList
 *
 * Renders SVG elements embedded in HTML documents by converting SVG
 * element trees into the shared painter recording path.
 *
 * Key functions:
 * - render_svg_build_subscene(): Capture an SVG Element tree for export lowering
 * - render_inline_svg(): Render SVG block in document context
 */


struct FontContext;  // forward declaration from lib/font/font.h
typedef struct RenderContext RenderContext;
typedef const char* (*SvgImageResolverFn)(void* context, int image_id);

// ============================================================================
// SVG Intrinsic Size
// ============================================================================

struct SvgIntrinsicSize {
    float width;
    float height;
    float aspect_ratio;          // width / height
    bool has_intrinsic_width;
    bool has_intrinsic_height;
};

// ============================================================================
// SVG Render Context (internal state during rendering)
// ============================================================================

struct SvgInlineRenderContext {
    Element* svg_root;           // root <svg> element
    Pool* pool;                  // memory pool
    FontContext* font_ctx;       // font context for font resolution (may be nullptr)
    DisplayList* dl;             // required display list target for deferred rendering
    PaintList* paint_list;       // required PaintIR gateway used before lowering to dl
    const char* source_path;      // source SVG path for resolving nested resources
    SvgImageResolverFn image_resolver;  // optional resolver for document-owned image handles
    void* image_resolver_context;
    RdtMatrix transform;         // accumulated transform from root (viewBox × group × element)

    // pixel ratio for text sizing - text font sizes need to be divided by this
    // because the entire SVG scene is scaled by pixel_ratio after building
    float pixel_ratio;

    // viewBox transform state
    float viewbox_x, viewbox_y;
    float viewbox_width, viewbox_height;
    float scale_x, scale_y;      // viewport / viewBox ratio
    float translate_x, translate_y;

    // inherited style state
    Color fill_color;
    Color stroke_color;
    Color current_color;         // CSS 'color' property for currentColor keyword
    float stroke_width;
    float opacity;
    bool fill_none;
    bool stroke_none;

    // inherited text properties (used by <text>/<tspan> when not on element itself)
    const char* inherited_font_family;   // pointer into Element attribute string memory (lifetime of SVG element tree)
    float inherited_font_size;            // 0 means not set
    int inherited_font_weight;            // 0 means not set
    const char* inherited_text_anchor;

    // current viewport size in user-coordinate units (parent for nested <svg>).
    // Used to resolve omitted width/height on a nested <svg> element ("100%").
    float current_viewport_w;
    float current_viewport_h;

    // gradient/pattern definitions from <defs>
    HashMap* defs;               // id → SvgDefTable*

    // lightweight SVG-document stylesheet cache from embedded <style> nodes
    void* style_rules;           // SvgStyleRule* (private to render_svg_inline.cpp)
    int style_rule_count;
    int style_rule_capacity;

    // Internal guard used while repainting a source element through a resolved
    // SVG mask. Prevents recursive mask application on the same element.
    bool suppress_masks;
};

extern "C" void svg_register_pdf_image_resolver(Element* svg_root, Item pdf_root);
extern "C" void svg_unregister_image_resolvers_for_tree(Element* root);
extern "C" bool svg_get_registered_image_resolver(Element* svg_root,
                                                   SvgImageResolverFn* out_resolver,
                                                   void** out_context);

// ============================================================================
// Public API
// ============================================================================

/**
 * Calculate SVG intrinsic size from element attributes
 * Per CSS Images Level 3, SVG intrinsic size is determined by:
 * 1. Explicit width/height attributes
 * 2. viewBox ratio if only one dimension specified
 * 3. viewBox dimensions if no width/height specified
 * 4. Default 300×150 if nothing specified (per HTML spec)
 *
 * @param svg_element The <svg> Element
 * @return Intrinsic size with aspect ratio
 */
SvgIntrinsicSize calculate_svg_intrinsic_size(Element* svg_element);

void render_svg_build_subscene(PaintSvgSubscene* subscene,
                      Element* svg_element,
                      float viewport_width, float viewport_height,
                      Pool* pool, float pixel_ratio,
                      FontContext* font_ctx,
                      const RdtMatrix* base_transform,
                      const Bound* content_clip,
                      const Color* initial_current_color,
                      const Color* initial_fill_color,
                      const char* source_path,
                      float initial_opacity,
                      bool initial_fill_none,
                      const Color* initial_stroke_color,
                      bool initial_stroke_none,
                      float initial_stroke_width);

void render_svg_inline_register_paint_ir_lowerers(void);

/**
 * Render an SVG element tree through DisplayList record/replay into an existing
 * RdtVector target. This is used for offscreen SVG pictures so they follow the
 * same replay path as raster output instead of immediate rdt_* emission.
 */
void render_svg_to_vec_via_display_list(RdtVector* vec, Element* svg_element,
                      float viewport_width, float viewport_height,
                      Pool* pool, float pixel_ratio = 1.0f,
                      FontContext* font_ctx = nullptr,
                      const RdtMatrix* base_transform = nullptr,
                      const Color* initial_current_color = nullptr,
                      const Color* initial_fill_color = nullptr,
                      const char* source_path = nullptr,
                      float initial_opacity = 1.0f,
                      bool initial_fill_none = false,
                      const Color* initial_stroke_color = nullptr,
                      bool initial_stroke_none = true,
                      float initial_stroke_width = -1.0f);

/**
 * Render inline SVG element in document context
 * Called by raster and vector render walkers when element is HTM_TAG_SVG.
 *
 * @param rdcon Render context with canvas, scale, clip, etc.
 * @param view ViewBlock for the SVG element
 */
void render_inline_svg(RenderContext* rdcon, ViewBlock* view);

// ===== render_text.hpp =====
void render_text_view(RenderContext* rdcon, ViewText* text_view);

// ===== render_vector_path.hpp =====
struct RenderContext;

void render_vector_path(struct RenderContext* rdcon, ViewBlock* block);

// ===== render_video.hpp =====
struct DisplayList;
struct DocState;
struct ImageSurface;
struct UiContext;

void render_video_frames(DisplayList* dl, ImageSurface* surface, DocState* rstate, UiContext* uicon);
void render_video_frames_cached(DocState* rstate, ImageSurface* surface, UiContext* uicon);
