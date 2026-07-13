#pragma once
// ==========================================================================
// DisplayList — Serialised draw-command buffer for Radiant's rendering pipeline.
//
// Phase 1 of the multi-threaded rendering proposal.
// Decouples the recording pass (main thread walks the view tree) from the
// rasterisation pass (replay through rdt_* calls, eventually per-tile).
// ==========================================================================

#include "rdt_vector.hpp"
#include "clip_shape.h"
#include "../lib/scratch_arena.h"
#include "../lib/font/font.h"
#include "view.hpp"          // Bound, ImageSurface
#include "state_store.hpp"   // DirtyTracker

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
