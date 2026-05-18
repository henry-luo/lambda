#pragma once
#include "view.hpp"
#include "state_store.hpp"
#include "rdt_vector.hpp"
#include "clip_shape.h"
#include "display_list.h"
#include "../lib/scratch_arena.h"

// format to SDL_PIXELFORMAT_ARGB8888
#define RDT_PIXELFORMAT_RGB(r, g, b)    ((uint32_t)((r << 16) | (g << 8) | b))

typedef struct {
    FontBox font;  // current font style
    BlockBlot block;
    ListBlot list;
    Color color;
    RdtVector vec;      // platform-agnostic vector renderer

    UiContext* ui_context;

    // Display list for deferred rendering (Phase 1)
    // When non-NULL, render functions record to dl instead of drawing directly.
    DisplayList* dl;

    // Transform state
    RdtMatrix transform;           // Current combined transform matrix
    bool has_transform;            // True if non-identity transform is active

    // HiDPI scaling: CSS logical pixels -> physical surface pixels
    float scale;                   // pixel_ratio (1.0 for standard, 2.0 for Retina, etc.)
    
    // Phase 18: Dirty-region tracking for render tree clipping
    DirtyTracker* dirty_tracker;   // NULL = full repaint (no clipping)
    Bound dirty_union;             // union bbox of all dirty rects (CSS pixels, valid when dirty_tracker != NULL)
    bool has_dirty_union;          // true when dirty_union is valid

    // LIFO scratch allocator for scoped temporary buffers (pixel buffers, clip masks, etc.)
    ScratchArena scratch;

    // Vector clip shape stack for overflow:hidden with border-radius and CSS clip-path
    ClipShape* clip_shapes[RDT_MAX_CLIP_SHAPES];
    int clip_shape_depth;
} RenderContext;

// Function declarations
void render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file);

// Shut down the render pool (must be called before rdt_engine_term)
void render_pool_shutdown();

// Tile-based PNG rendering for large pages that would OOM with a single surface.
// Only used for PNG output.  total_width/total_height are in physical pixels.
void render_html_doc_tiled(UiContext* uicon, ViewTree* view_tree, const char* output_file,
                           int total_width, int total_height);

// UI overlay rendering (focus, caret, selection)
void render_focus_outline(RenderContext* rdcon, DocState* state);
void render_caret(RenderContext* rdcon, DocState* state);
void render_selection(RenderContext* rdcon, DocState* state);
void render_ui_overlays(RenderContext* rdcon, DocState* state);

// ---------------------------------------------------------------------------
// rc_* — Render-context drawing wrappers (dispatch to display list or rdt_*)
// When rdcon->dl is set, record to the display list.
// Otherwise, draw directly through the rdt_* vector API.
// ---------------------------------------------------------------------------

static inline void rc_fill_rect(RenderContext* rdcon, float x, float y, float w, float h, Color color) {
    if (rdcon->dl) dl_fill_rect(rdcon->dl, x, y, w, h, color);
    else rdt_fill_rect(&rdcon->vec, x, y, w, h, color);
}

static inline void rc_fill_rounded_rect(RenderContext* rdcon, float x, float y, float w, float h,
                                        float rx, float ry, Color color) {
    if (rdcon->dl) dl_fill_rounded_rect(rdcon->dl, x, y, w, h, rx, ry, color);
    else rdt_fill_rounded_rect(&rdcon->vec, x, y, w, h, rx, ry, color);
}

static inline void rc_fill_path(RenderContext* rdcon, RdtPath* path, Color color,
                                RdtFillRule rule, const RdtMatrix* transform) {
    if (rdcon->dl) dl_fill_path(rdcon->dl, path, color, rule, transform);
    else rdt_fill_path(&rdcon->vec, path, color, rule, transform);
}

static inline void rc_stroke_path(RenderContext* rdcon, RdtPath* path, Color color, float width,
                                  RdtStrokeCap cap, RdtStrokeJoin join,
                                  const float* dash_array, int dash_count,
                                  const RdtMatrix* transform, float dash_phase = 0) {
    if (rdcon->dl) dl_stroke_path(rdcon->dl, path, color, width, cap, join, dash_array, dash_count, dash_phase, transform);
    else rdt_stroke_path(&rdcon->vec, path, color, width, cap, join, dash_array, dash_count, dash_phase, transform);
}

static inline void rc_fill_linear_gradient(RenderContext* rdcon, RdtPath* path,
                                           float x1, float y1, float x2, float y2,
                                           const RdtGradientStop* stops, int stop_count,
                                           RdtFillRule rule, const RdtMatrix* transform) {
    if (rdcon->dl) dl_fill_linear_gradient(rdcon->dl, path, x1, y1, x2, y2, stops, stop_count, rule, transform);
    else rdt_fill_linear_gradient(&rdcon->vec, path, x1, y1, x2, y2, stops, stop_count, rule, transform);
}

static inline void rc_fill_radial_gradient(RenderContext* rdcon, RdtPath* path,
                                           float cx, float cy, float r,
                                           const RdtGradientStop* stops, int stop_count,
                                           RdtFillRule rule, const RdtMatrix* transform) {
    if (rdcon->dl) dl_fill_radial_gradient(rdcon->dl, path, cx, cy, r, stops, stop_count, rule, transform);
    else rdt_fill_radial_gradient(&rdcon->vec, path, cx, cy, r, stops, stop_count, rule, transform);
}

static inline void rc_draw_image(RenderContext* rdcon, const uint32_t* pixels,
                                 int src_w, int src_h, int src_stride,
                                 float dst_x, float dst_y, float dst_w, float dst_h,
                                 uint8_t opacity, const RdtMatrix* transform) {
    if (rdcon->dl) dl_draw_image(rdcon->dl, pixels, src_w, src_h, src_stride, dst_x, dst_y, dst_w, dst_h, opacity, transform);
    else rdt_draw_image(&rdcon->vec, pixels, src_w, src_h, src_stride, dst_x, dst_y, dst_w, dst_h, opacity, transform);
}

static inline void rc_draw_picture(RenderContext* rdcon, RdtPicture* picture,
                                   uint8_t opacity, const RdtMatrix* transform) {
    if (rdcon->dl) dl_draw_picture(rdcon->dl, picture, opacity, transform);
    else rdt_picture_draw(&rdcon->vec, picture, opacity, transform);
}

static inline void rc_push_clip(RenderContext* rdcon, RdtPath* clip_path, const RdtMatrix* transform) {
    if (rdcon->dl) dl_push_clip(rdcon->dl, clip_path, transform);
    else rdt_push_clip(&rdcon->vec, clip_path, transform);
}

static inline void rc_pop_clip(RenderContext* rdcon) {
    if (rdcon->dl) dl_pop_clip(rdcon->dl);
    else rdt_pop_clip(&rdcon->vec);
}

static inline int rc_clip_save_depth(RenderContext* rdcon) {
    if (rdcon->dl) {
        dl_save_clip_depth(rdcon->dl);
        return rdcon->dl->count - 1;  // index of saved-depth item
    }
    return rdt_clip_save_depth();
}

static inline void rc_clip_restore_depth(RenderContext* rdcon, int saved) {
    if (rdcon->dl) dl_restore_clip_depth(rdcon->dl, saved);
    else rdt_clip_restore_depth(saved);
}

static inline void render_painter_draw_picture_rect(RenderContext* rdcon, RdtPicture* picture,
                                                    Rect* dst_rect, Bound* clip,
                                                    uint8_t opacity) {
    if (!picture || !dst_rect) return;
    rdt_picture_set_size(picture, dst_rect->width, dst_rect->height);
    RdtMatrix m = rdt_matrix_identity();
    m.e13 = dst_rect->x;
    m.e23 = dst_rect->y;

    if (clip) {
        RdtPath* clip_path = rdt_path_new();
        rdt_path_add_rect(clip_path, clip->left, clip->top,
                          clip->right - clip->left, clip->bottom - clip->top, 0, 0);
        rc_push_clip(rdcon, clip_path, nullptr);
        rdt_path_free(clip_path);
    }

    rc_draw_picture(rdcon, picture, opacity, &m);

    if (clip) rc_pop_clip(rdcon);
}

static inline void render_painter_draw_pixels_rect(RenderContext* rdcon, const uint32_t* pixels,
                                                   int src_w, int src_h, int src_stride,
                                                   Rect* dst_rect, Bound* clip,
                                                   uint8_t opacity) {
    if (!pixels || !dst_rect) return;
    if (clip) {
        RdtPath* clip_path = rdt_path_new();
        rdt_path_add_rect(clip_path, clip->left, clip->top,
                          clip->right - clip->left, clip->bottom - clip->top, 0, 0);
        rc_push_clip(rdcon, clip_path, nullptr);
        rdt_path_free(clip_path);
    }

    rc_draw_image(rdcon, pixels, src_w, src_h, src_stride,
                  dst_rect->x, dst_rect->y, dst_rect->width, dst_rect->height,
                  opacity, nullptr);

    if (clip) rc_pop_clip(rdcon);
}

// Direct-pixel wrappers
static inline void render_painter_fill_surface_rect(RenderContext* rdcon, ImageSurface* surface,
                                                    Rect* rect, uint32_t color, Bound* clip,
                                                    ClipShape** clip_shapes, int clip_depth) {
    if (rdcon->dl) {
        dl_fill_surface_rect(rdcon->dl, rect->x, rect->y, rect->width, rect->height,
                             color, clip, clip_shapes, clip_depth);
    } else {
        RasterPaintContext raster = {surface, clip, clip_shapes, clip_depth};
        raster_fill_rect(&raster, rect, color);
    }
}

static inline void render_painter_blit_surface_scaled(RenderContext* rdcon,
                                                      ImageSurface* src, Rect* src_rect,
                                                      ImageSurface* dst, Rect* dst_rect, Bound* clip,
                                                      ScaleMode scale_mode,
                                                      ClipShape** clip_shapes, int clip_depth,
                                                      uint8_t opacity = 255) {
    if (rdcon->dl) {
        dl_blit_surface_scaled(rdcon->dl, src, dst_rect->x, dst_rect->y,
                               dst_rect->width, dst_rect->height, (int)scale_mode,
                               clip, clip_shapes, clip_depth, opacity);
    } else {
        RasterPaintContext raster = {dst, clip, clip_shapes, clip_depth};
        raster_blit_surface_scaled(&raster, src, src_rect, dst_rect, scale_mode, opacity);
    }
}

static inline void rc_fill_surface_rect(RenderContext* rdcon, ImageSurface* surface,
                                        Rect* rect, uint32_t color, Bound* clip,
                                        ClipShape** clip_shapes, int clip_depth) {
    render_painter_fill_surface_rect(rdcon, surface, rect, color, clip, clip_shapes, clip_depth);
}

static inline void rc_blit_surface_scaled(RenderContext* rdcon,
                                          ImageSurface* src, Rect* src_rect,
                                          ImageSurface* dst, Rect* dst_rect, Bound* clip,
                                          ScaleMode scale_mode,
                                          ClipShape** clip_shapes, int clip_depth) {
    render_painter_blit_surface_scaled(rdcon, src, src_rect, dst, dst_rect, clip,
                                       scale_mode, clip_shapes, clip_depth);
}
