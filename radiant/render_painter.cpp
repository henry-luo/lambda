#include "render.hpp"
#include "render_state.hpp"
#include "paint_ir.h"
#include "../lib/log.h"

// ---------------------------------------------------------------------------
// Painter gateway (rc_*).
//
// Primary recording mode per primitive:
//   1. Semantic IR (rdcon->paint_list && rdcon->dl): record through the
//      PaintBuilder, then lower to the display list immediately so command
//      order is preserved alongside the non-IR display-list ops. This routes
//      the live raster path through the semantic paint IR (Phase C) and is
//      byte-identical to direct dl_* recording (proven by PaintIrParityTest).
//   2. Compatibility only (rdcon->dl without paint_list): direct dl_* recording
//      for transitional/manual contexts. The live render path sets PaintList.
//
// rc_paint_active() selects mode 1; rc_lower_pending() flushes the single
// recorded command and rewinds the reusable PaintList.
// ---------------------------------------------------------------------------

static inline bool rc_paint_active(RenderContext* rdcon) {
    return rdcon && rdcon->paint_list && rdcon->dl;
}

static inline bool rc_dl_active(RenderContext* rdcon) {
    return rdcon && rdcon->dl;
}

static inline void rc_lower_pending(RenderContext* rdcon) {
    paint_ir_lower_raster(rdcon->paint_list, rdcon->dl);
    paint_list_clear(rdcon->paint_list);
}

static inline void rc_missing_dl(const char* op) {
    log_error("[PAINTER] %s called without display list", op ? op : "paint op");
}

void rc_fill_rect(RenderContext* rdcon, float x, float y, float w, float h, Color color) {
    if (rc_paint_active(rdcon)) {
        paint_fill_rect(rdcon->paint_list, x, y, w, h, color);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) dl_fill_rect(rdcon->dl, x, y, w, h, color);
    else rc_missing_dl("rc_fill_rect");
}

void rc_fill_rounded_rect(RenderContext* rdcon, float x, float y, float w, float h,
                          float rx, float ry, Color color) {
    if (rc_paint_active(rdcon)) {
        paint_fill_rounded_rect(rdcon->paint_list, x, y, w, h, rx, ry, color);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) dl_fill_rounded_rect(rdcon->dl, x, y, w, h, rx, ry, color);
    else rc_missing_dl("rc_fill_rounded_rect");
}

void rc_fill_path(RenderContext* rdcon, RdtPath* path, Color color,
                  RdtFillRule rule, const RdtMatrix* transform) {
    if (rc_paint_active(rdcon)) {
        paint_fill_path(rdcon->paint_list, path, color, rule, transform);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) dl_fill_path(rdcon->dl, path, color, rule, transform);
    else rc_missing_dl("rc_fill_path");
}

void rc_stroke_path(RenderContext* rdcon, RdtPath* path, Color color, float width,
                    RdtStrokeCap cap, RdtStrokeJoin join,
                    const float* dash_array, int dash_count,
                    const RdtMatrix* transform, float dash_phase) {
    if (rc_paint_active(rdcon)) {
        paint_stroke_path(rdcon->paint_list, path, color, width, cap, join,
                          dash_array, dash_count, dash_phase, transform);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) {
        dl_stroke_path(rdcon->dl, path, color, width, cap, join,
                       dash_array, dash_count, dash_phase, transform);
    } else {
        rc_missing_dl("rc_stroke_path");
    }
}

void rc_fill_linear_gradient(RenderContext* rdcon, RdtPath* path,
                             float x1, float y1, float x2, float y2,
                             const RdtGradientStop* stops, int stop_count,
                             RdtFillRule rule, const RdtMatrix* transform) {
    if (rc_paint_active(rdcon)) {
        paint_fill_linear_gradient(rdcon->paint_list, path, x1, y1, x2, y2,
                                   stops, stop_count, rule, transform);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) {
        dl_fill_linear_gradient(rdcon->dl, path, x1, y1, x2, y2,
                                stops, stop_count, rule, transform);
    } else {
        rc_missing_dl("rc_fill_linear_gradient");
    }
}

void rc_fill_radial_gradient(RenderContext* rdcon, RdtPath* path,
                             float cx, float cy, float r,
                             const RdtGradientStop* stops, int stop_count,
                             RdtFillRule rule, const RdtMatrix* transform) {
    if (rc_paint_active(rdcon)) {
        paint_fill_radial_gradient(rdcon->paint_list, path, cx, cy, r,
                                   stops, stop_count, rule, transform);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) {
        dl_fill_radial_gradient(rdcon->dl, path, cx, cy, r,
                                stops, stop_count, rule, transform);
    } else {
        rc_missing_dl("rc_fill_radial_gradient");
    }
}

void rc_draw_image(RenderContext* rdcon, const uint32_t* pixels,
                   int src_w, int src_h, int src_stride,
                   float dst_x, float dst_y, float dst_w, float dst_h,
                   uint8_t opacity, const RdtMatrix* transform,
                   ImageSurface* resource_owner) {
    if (rc_paint_active(rdcon)) {
        paint_draw_image(rdcon->paint_list, pixels, src_w, src_h, src_stride,
                         dst_x, dst_y, dst_w, dst_h, opacity, transform,
                         resource_owner);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) {
        dl_draw_image(rdcon->dl, pixels, src_w, src_h, src_stride,
                      dst_x, dst_y, dst_w, dst_h, opacity, transform,
                      resource_owner,
                      resource_owner ? resource_owner->generation : 0);
    } else {
        rc_missing_dl("rc_draw_image");
    }
}

void rc_draw_glyph(RenderContext* rdcon, GlyphBitmap* bitmap, int x, int y,
                   Color color, bool is_color_emoji, const Bound* clip,
                   const RdtMatrix* transform, uint64_t resource_generation) {
    if (rc_paint_active(rdcon)) {
        paint_draw_glyph(rdcon->paint_list, bitmap, x, y, color, is_color_emoji,
                         clip, transform, resource_generation);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) {
        dl_draw_glyph(rdcon->dl, bitmap, x, y, color, is_color_emoji,
                      clip, transform, resource_generation);
    } else {
        rc_missing_dl("rc_draw_glyph");
    }
}

void rc_draw_picture(RenderContext* rdcon, RdtPicture* picture,
                     uint8_t opacity, const RdtMatrix* transform) {
    if (rc_paint_active(rdcon)) {
        paint_draw_picture(rdcon->paint_list, picture, opacity, transform);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) dl_draw_picture(rdcon->dl, picture, opacity, transform);
    else rc_missing_dl("rc_draw_picture");
}

void rc_video_placeholder(RenderContext* rdcon, void* video,
                          float dst_x, float dst_y, float dst_w, float dst_h,
                          int object_fit, const Bound* clip,
                          uint64_t video_generation) {
    if (rc_paint_active(rdcon)) {
        paint_video_placeholder(rdcon->paint_list, video,
                                dst_x, dst_y, dst_w, dst_h,
                                object_fit, clip, video_generation);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) {
        dl_video_placeholder(rdcon->dl, video,
                             dst_x, dst_y, dst_w, dst_h,
                             object_fit, clip, video_generation);
    } else {
        rc_missing_dl("rc_video_placeholder");
    }
}

void rc_webview_layer_placeholder(RenderContext* rdcon, void* surface,
                                  float dst_x, float dst_y, float dst_w, float dst_h,
                                  const Bound* clip,
                                  uint64_t surface_generation) {
    if (rc_paint_active(rdcon)) {
        paint_webview_layer_placeholder(rdcon->paint_list, surface,
                                        dst_x, dst_y, dst_w, dst_h,
                                        clip, surface_generation);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) {
        dl_webview_layer_placeholder(rdcon->dl, surface,
                                     dst_x, dst_y, dst_w, dst_h,
                                     clip, surface_generation);
    } else {
        rc_missing_dl("rc_webview_layer_placeholder");
    }
}

void rc_push_clip(RenderContext* rdcon, RdtPath* clip_path, const RdtMatrix* transform) {
    if (rc_dl_active(rdcon)) dl_push_clip(rdcon->dl, clip_path, transform);
    else rc_missing_dl("rc_push_clip");
}

void rc_pop_clip(RenderContext* rdcon) {
    if (rc_dl_active(rdcon)) dl_pop_clip(rdcon->dl);
    else rc_missing_dl("rc_pop_clip");
}

void rc_save_backdrop(RenderContext* rdcon, int x0, int y0, int w, int h) {
    if (rc_paint_active(rdcon)) {
        paint_save_backdrop(rdcon->paint_list, x0, y0, w, h);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) dl_save_backdrop(rdcon->dl, x0, y0, w, h);
    else rc_missing_dl("rc_save_backdrop");
}

void rc_composite_opacity(RenderContext* rdcon, int x0, int y0, int w, int h,
                          float opacity, bool premultiplied_source) {
    if (rc_paint_active(rdcon)) {
        paint_composite_opacity(rdcon->paint_list, x0, y0, w, h,
                                opacity, premultiplied_source);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) {
        dl_composite_opacity(rdcon->dl, x0, y0, w, h, opacity, premultiplied_source);
    } else {
        rc_missing_dl("rc_composite_opacity");
    }
}

void rc_apply_blend_mode(RenderContext* rdcon, int x0, int y0, int w, int h,
                         int blend_mode) {
    if (rc_paint_active(rdcon)) {
        paint_apply_blend_mode(rdcon->paint_list, x0, y0, w, h, blend_mode);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) {
        dl_apply_blend_mode(rdcon->dl, x0, y0, w, h, blend_mode);
    } else {
        rc_missing_dl("rc_apply_blend_mode");
    }
}

void rc_apply_filter(RenderContext* rdcon, float x, float y, float w, float h,
                     void* filter, const Bound* clip) {
    if (rc_paint_active(rdcon)) {
        paint_apply_filter(rdcon->paint_list, x, y, w, h, filter, clip);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) {
        dl_apply_filter(rdcon->dl, x, y, w, h, filter, clip);
    } else {
        rc_missing_dl("rc_apply_filter");
    }
}

void rc_box_blur_region(RenderContext* rdcon, int rx, int ry, int rw, int rh,
                        float blur_radius, int clip_type, const float* clip_params,
                        int exclude_type, const float* exclude_params,
                        bool premultiply_source,
                        bool tint_source, Color tint_color) {
    if (rc_paint_active(rdcon)) {
        paint_box_blur_region(rdcon->paint_list, rx, ry, rw, rh, blur_radius,
                              clip_type, clip_params, exclude_type, exclude_params,
                              premultiply_source, tint_source, tint_color);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) {
        dl_box_blur_region(rdcon->dl, rx, ry, rw, rh, blur_radius,
                           clip_type, clip_params, exclude_type, exclude_params,
                           premultiply_source, tint_source, tint_color);
    } else {
        rc_missing_dl("rc_box_blur_region");
    }
}

void rc_box_blur_inset(RenderContext* rdcon, int rx, int ry, int rw, int rh,
                       int pad, float blur_radius, uint32_t bg_color) {
    if (rc_paint_active(rdcon)) {
        paint_box_blur_inset(rdcon->paint_list, rx, ry, rw, rh, pad,
                             blur_radius, bg_color);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) {
        dl_box_blur_inset(rdcon->dl, rx, ry, rw, rh, pad, blur_radius, bg_color);
    } else {
        rc_missing_dl("rc_box_blur_inset");
    }
}

void rc_shadow_clip_save(RenderContext* rdcon, int rx, int ry, int rw, int rh) {
    if (rc_paint_active(rdcon)) {
        paint_shadow_clip_save(rdcon->paint_list, rx, ry, rw, rh);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) {
        dl_shadow_clip_save(rdcon->dl, rx, ry, rw, rh);
    } else {
        rc_missing_dl("rc_shadow_clip_save");
    }
}

void rc_shadow_clip_restore(RenderContext* rdcon, int exclude_type, const float* exclude_params,
                            int save_rx, int save_ry, int save_rw, int save_rh,
                            int restore_inside) {
    if (rc_paint_active(rdcon)) {
        paint_shadow_clip_restore(rdcon->paint_list, exclude_type, exclude_params,
                                  save_rx, save_ry, save_rw, save_rh,
                                  restore_inside);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) {
        dl_shadow_clip_restore(rdcon->dl, exclude_type, exclude_params,
                               save_rx, save_ry, save_rw, save_rh,
                               restore_inside);
    } else {
        rc_missing_dl("rc_shadow_clip_restore");
    }
}

void rc_outer_shadow(RenderContext* rdcon,
                     float shadow_x, float shadow_y, float shadow_w, float shadow_h,
                     float sr_tl, float sr_tr, float sr_br, float sr_bl,
                     Color color, float blur_radius,
                     int exclude_type, const float* exclude_params,
                     int clip_type, const float* clip_params) {
    if (rc_paint_active(rdcon)) {
        paint_outer_shadow(rdcon->paint_list,
                           shadow_x, shadow_y, shadow_w, shadow_h,
                           sr_tl, sr_tr, sr_br, sr_bl,
                           color, blur_radius,
                           exclude_type, exclude_params,
                           clip_type, clip_params);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) {
        dl_outer_shadow(rdcon->dl,
                        shadow_x, shadow_y, shadow_w, shadow_h,
                        sr_tl, sr_tr, sr_br, sr_bl,
                        color, blur_radius,
                        exclude_type, exclude_params,
                        clip_type, clip_params);
    } else {
        rc_missing_dl("rc_outer_shadow");
    }
}

void render_painter_draw_picture_rect(RenderContext* rdcon, RdtPicture* picture,
                                      Rect* dst_rect, Bound* clip,
                                      uint8_t opacity) {
    if (!picture || !dst_rect) return;
    rdt_picture_set_size(picture, dst_rect->width, dst_rect->height);
    RdtMatrix m = rdt_matrix_identity();
    m.e13 = dst_rect->x;
    m.e23 = dst_rect->y;
    const RdtMatrix* current_transform = render_state_current_transform(rdcon);
    if (current_transform) {
        m = rdt_matrix_multiply(current_transform, &m);
    }

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

void render_painter_draw_pixels_rect(RenderContext* rdcon, const uint32_t* pixels,
                                     int src_w, int src_h, int src_stride,
                                     Rect* dst_rect, Bound* clip,
                                     uint8_t opacity,
                                     ImageSurface* resource_owner) {
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
                  opacity, render_state_current_transform(rdcon), resource_owner);

    if (clip) rc_pop_clip(rdcon);
}

void render_painter_fill_surface_rect(RenderContext* rdcon, ImageSurface* surface,
                                      Rect* rect, uint32_t color, Bound* clip,
                                      ClipShape** clip_shapes, int clip_depth) {
    (void)surface;
    if (rc_paint_active(rdcon)) {
        paint_fill_surface_rect(rdcon->paint_list, rect->x, rect->y,
                                rect->width, rect->height,
                                color, clip, clip_shapes, clip_depth);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) {
        dl_fill_surface_rect(rdcon->dl, rect->x, rect->y, rect->width, rect->height,
                             color, clip, clip_shapes, clip_depth);
    } else {
        rc_missing_dl("render_painter_fill_surface_rect");
    }
}

void render_painter_blit_surface_scaled(RenderContext* rdcon,
                                        ImageSurface* src, Rect* src_rect,
                                        ImageSurface* dst, Rect* dst_rect, Bound* clip,
                                        ScaleMode scale_mode,
                                        ClipShape** clip_shapes, int clip_depth,
                                        uint8_t opacity) {
    (void)src_rect;
    (void)dst;
    if (rc_paint_active(rdcon)) {
        paint_blit_surface_scaled(rdcon->paint_list, src,
                                  dst_rect->x, dst_rect->y,
                                  dst_rect->width, dst_rect->height,
                                  (int)scale_mode, clip, clip_shapes, clip_depth,
                                  opacity, src ? src->generation : 0);
        rc_lower_pending(rdcon);
    } else if (rc_dl_active(rdcon)) {
        dl_blit_surface_scaled(rdcon->dl, src, dst_rect->x, dst_rect->y,
                               dst_rect->width, dst_rect->height, (int)scale_mode,
                               clip, clip_shapes, clip_depth, opacity,
                               src ? src->generation : 0);
    } else {
        rc_missing_dl("render_painter_blit_surface_scaled");
    }
}

void rc_fill_surface_rect(RenderContext* rdcon, ImageSurface* surface,
                          Rect* rect, uint32_t color, Bound* clip,
                          ClipShape** clip_shapes, int clip_depth) {
    render_painter_fill_surface_rect(rdcon, surface, rect, color, clip, clip_shapes, clip_depth);
}
