#pragma once

#include "view.hpp"
#include "rdt_vector.hpp"
#include "clip_shape.h"

struct RenderContext;
typedef struct RenderContext RenderContext;

// ---------------------------------------------------------------------------
// rc_* — Render-context drawing wrappers (dispatch to display list or rdt_*)
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
void rc_draw_picture(RenderContext* rdcon, RdtPicture* picture,
                     uint8_t opacity, const RdtMatrix* transform);
void rc_push_clip(RenderContext* rdcon, RdtPath* clip_path, const RdtMatrix* transform);
void rc_pop_clip(RenderContext* rdcon);
int rc_clip_save_depth(RenderContext* rdcon);
void rc_clip_restore_depth(RenderContext* rdcon, int saved);
void render_painter_begin_vector_batch(RenderContext* rdcon);
void render_painter_flush_vector_batch(RenderContext* rdcon);
void render_painter_end_vector_batch(RenderContext* rdcon);

void rc_fill_surface_rect(RenderContext* rdcon, ImageSurface* surface,
                          Rect* rect, uint32_t color, Bound* clip,
                          ClipShape** clip_shapes, int clip_depth);
void rc_blit_surface_scaled(RenderContext* rdcon,
                            ImageSurface* src, Rect* src_rect,
                            ImageSurface* dst, Rect* dst_rect, Bound* clip,
                            ScaleMode scale_mode,
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
