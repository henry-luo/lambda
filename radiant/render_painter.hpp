#pragma once

#include "view.hpp"
#include "rdt_vector.hpp"
#include "clip_shape.h"

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
