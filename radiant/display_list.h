#pragma once
// ==========================================================================
// DisplayList — Serialised draw-command buffer for Radiant's rendering pipeline.
//
// Phase 1 of the multi-threaded rendering proposal.
// Decouples the recording pass (main thread walks the view tree) from the
// rasterisation pass (replay through rdt_* calls, eventually per-tile).
// ==========================================================================

#include "rdt_vector.hpp"
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

typedef enum {
    DL_FILL_RECT,
    DL_FILL_ROUNDED_RECT,
    DL_FILL_PATH,
    DL_STROKE_PATH,
    DL_FILL_LINEAR_GRADIENT,
    DL_FILL_RADIAL_GRADIENT,
    DL_DRAW_IMAGE,
    DL_DRAW_GLYPH,
    DL_DRAW_PICTURE,
    DL_PUSH_CLIP,
    DL_POP_CLIP,
    DL_SAVE_CLIP_DEPTH,
    DL_RESTORE_CLIP_DEPTH,
    // direct-pixel operations (bypass ThorVG, operate on surface pixels)
    DL_FILL_SURFACE_RECT,
    DL_BLIT_SURFACE_SCALED,
    // opacity / blend / filter layers (post-processing pixel ops)
    DL_APPLY_OPACITY,
    DL_SAVE_BACKDROP,        // save surface pixels before blend-mode element renders
    DL_APPLY_BLEND_MODE,
    DL_APPLY_FILTER,
    // element group markers (for retained sub-trees, Phase 2+)
    DL_BEGIN_ELEMENT,
    DL_END_ELEMENT,
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
} DlFillLinearGradient;

typedef struct {
    RdtPath* path;       // cloned path, owned by display list
    float cx, cy, r;
    RdtGradientStop* stops;  // arena-allocated copy
    int stop_count;
    RdtFillRule rule;
    bool has_transform;
    RdtMatrix transform;
} DlFillRadialGradient;

typedef struct {
    const uint32_t* pixels;  // borrowed — image lifetime must exceed display list
    int src_w, src_h, src_stride;
    float dst_x, dst_y, dst_w, dst_h;
    uint8_t opacity;
    bool has_transform;
    RdtMatrix transform;
} DlDrawImage;

typedef struct {
    GlyphBitmap bitmap;      // copy of bitmap descriptor (buffer pointer borrowed)
    int x, y;                // destination pixel position
    Color color;             // text color at recording time
    bool is_color_emoji;     // BGRA color emoji (no tint)
    Bound clip;              // rectangular clip bounds at recording time
} DlDrawGlyph;

typedef struct {
    RdtPicture* picture;     // borrowed — caller manages lifetime
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
    int saved_depth;
} DlClipDepth;

// Direct-pixel fill (selection highlights, surface clear, etc.)
typedef struct {
    float x, y, w, h;
    uint32_t color;          // ABGR8888
    Bound clip;              // rectangular clip bounds at recording time
} DlFillSurfaceRect;

// Direct-pixel scaled blit (raster images via blit_surface_scaled)
typedef struct {
    void* src_surface;       // ImageSurface* — borrowed
    float dst_x, dst_y, dst_w, dst_h;
    int scale_mode;
    Bound clip;              // rectangular clip bounds at recording time
} DlBlitSurfaceScaled;

// Post-processing: multiply alpha of all pixels in region
typedef struct {
    int x0, y0, x1, y1;     // physical pixel region (already scaled + clamped)
    float opacity;
} DlApplyOpacity;

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
        DlClipDepth          clip_depth;
        DlFillSurfaceRect    fill_surface_rect;
        DlBlitSurfaceScaled  blit_surface_scaled;
        DlApplyOpacity       apply_opacity;
        DlSaveBackdrop       save_backdrop;
        DlApplyBlendMode     apply_blend_mode;
        DlApplyFilter        apply_filter;
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
                    const float* dash_array, int dash_count,
                    const RdtMatrix* transform);

void dl_fill_linear_gradient(DisplayList* dl, RdtPath* path,
                             float x1, float y1, float x2, float y2,
                             const RdtGradientStop* stops, int stop_count,
                             RdtFillRule rule, const RdtMatrix* transform);

void dl_fill_radial_gradient(DisplayList* dl, RdtPath* path,
                             float cx, float cy, float r,
                             const RdtGradientStop* stops, int stop_count,
                             RdtFillRule rule, const RdtMatrix* transform);

void dl_draw_image(DisplayList* dl, const uint32_t* pixels,
                   int src_w, int src_h, int src_stride,
                   float dst_x, float dst_y, float dst_w, float dst_h,
                   uint8_t opacity, const RdtMatrix* transform);

// Record a glyph draw command.  bitmap buffer is borrowed (must outlive display list).
void dl_draw_glyph(DisplayList* dl, GlyphBitmap* bitmap, int x, int y,
                   Color color, bool is_color_emoji, const Bound* clip);

void dl_draw_picture(DisplayList* dl, RdtPicture* picture,
                     uint8_t opacity, const RdtMatrix* transform);

void dl_push_clip(DisplayList* dl, RdtPath* clip_path, const RdtMatrix* transform);
void dl_pop_clip(DisplayList* dl);

void dl_save_clip_depth(DisplayList* dl);
void dl_restore_clip_depth(DisplayList* dl, int saved_depth);

// Direct-pixel operations
void dl_fill_surface_rect(DisplayList* dl, float x, float y, float w, float h,
                          uint32_t color, const Bound* clip);

void dl_blit_surface_scaled(DisplayList* dl, void* src_surface,
                            float dst_x, float dst_y, float dst_w, float dst_h,
                            int scale_mode, const Bound* clip);

// Post-processing operations (coordinates already in physical pixels)
void dl_apply_opacity(DisplayList* dl, int x0, int y0, int x1, int y1,
                      float opacity);

void dl_save_backdrop(DisplayList* dl, int x0, int y0, int w, int h);

void dl_apply_blend_mode(DisplayList* dl, int x0, int y0, int w, int h,
                         int blend_mode);

void dl_apply_filter(DisplayList* dl, float x, float y, float w, float h,
                     void* filter, const Bound* clip);

// ---------------------------------------------------------------------------
// Replay — execute all recorded commands through rdt_* calls
// ---------------------------------------------------------------------------

// Replay the entire display list to the given vector context.
// surface is needed for direct-pixel operations (glyph, blit, opacity, etc.).
// clip is the current clip bounds.  scratch is used for transient allocations.
// dirty_tracker: if non-NULL, clips all rendering to dirty regions only
//   (for selective/incremental repaint — prevents parent backgrounds from
//    overwriting preserved content outside dirty areas).
void dl_replay(DisplayList* dl, RdtVector* vec,
               ImageSurface* surface, Bound* clip,
               ScratchArena* scratch, float scale,
               DirtyTracker* dirty_tracker);

// ---------------------------------------------------------------------------
// Debug / stats
// ---------------------------------------------------------------------------

int dl_item_count(const DisplayList* dl);

#ifdef __cplusplus
}
#endif
