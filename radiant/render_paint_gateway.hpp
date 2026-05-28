#pragma once

#include "display_list.h"
#include "paint_ir.h"
#include "../lib/log.h"

typedef struct PaintRecordTarget {
    PaintList* paint_list;
    DisplayList* display_list;
    const char* log_prefix;
} PaintRecordTarget;

static inline bool paint_record_uses_ir(PaintRecordTarget* target) {
    return target && target->paint_list && target->display_list;
}

static inline bool paint_record_has_display_list(PaintRecordTarget* target) {
    return target && target->display_list;
}

static inline void paint_record_lower_pending(PaintRecordTarget* target) {
    paint_ir_lower_raster_fragment(target->paint_list, target->display_list);
    paint_list_clear(target->paint_list);
}

static inline void paint_record_missing(PaintRecordTarget* target, const char* op) {
    log_error("[%s] %s called without display list",
              target && target->log_prefix ? target->log_prefix : "PAINT_GATEWAY",
              op ? op : "paint op");
}

static inline void paint_record_fill_rect(PaintRecordTarget* target, const char* op,
                                          float x, float y, float w, float h, Color color) {
    if (paint_record_uses_ir(target)) {
        paint_fill_rect(target->paint_list, x, y, w, h, color);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_fill_rect(target->display_list, x, y, w, h, color);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_fill_rounded_rect(PaintRecordTarget* target, const char* op,
                                                  float x, float y, float w, float h,
                                                  float rx, float ry, Color color) {
    if (paint_record_uses_ir(target)) {
        paint_fill_rounded_rect(target->paint_list, x, y, w, h, rx, ry, color);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_fill_rounded_rect(target->display_list, x, y, w, h, rx, ry, color);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_fill_path(PaintRecordTarget* target, const char* op,
                                          RdtPath* path, Color color,
                                          RdtFillRule rule, const RdtMatrix* transform) {
    if (paint_record_uses_ir(target)) {
        paint_fill_path(target->paint_list, path, color, rule, transform);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_fill_path(target->display_list, path, color, rule, transform);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_stroke_path(PaintRecordTarget* target, const char* op,
                                            RdtPath* path, Color color, float width,
                                            RdtStrokeCap cap, RdtStrokeJoin join,
                                            const float* dash_array, int dash_count,
                                            float dash_phase, const RdtMatrix* transform) {
    if (paint_record_uses_ir(target)) {
        paint_stroke_path(target->paint_list, path, color, width, cap, join,
                          dash_array, dash_count, dash_phase, transform);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_stroke_path(target->display_list, path, color, width, cap, join,
                       dash_array, dash_count, dash_phase, transform);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_fill_linear_gradient(PaintRecordTarget* target, const char* op,
                                                     RdtPath* path,
                                                     float x1, float y1, float x2, float y2,
                                                     const RdtGradientStop* stops, int stop_count,
                                                     RdtFillRule rule,
                                                     const RdtMatrix* transform) {
    if (paint_record_uses_ir(target)) {
        paint_fill_linear_gradient(target->paint_list, path, x1, y1, x2, y2,
                                   stops, stop_count, rule, transform);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_fill_linear_gradient(target->display_list, path, x1, y1, x2, y2,
                                stops, stop_count, rule, transform);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_fill_radial_gradient(PaintRecordTarget* target, const char* op,
                                                     RdtPath* path, float cx, float cy, float r,
                                                     const RdtGradientStop* stops, int stop_count,
                                                     RdtFillRule rule,
                                                     const RdtMatrix* transform) {
    if (paint_record_uses_ir(target)) {
        paint_fill_radial_gradient(target->paint_list, path, cx, cy, r,
                                   stops, stop_count, rule, transform);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_fill_radial_gradient(target->display_list, path, cx, cy, r,
                                stops, stop_count, rule, transform);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_draw_image(PaintRecordTarget* target, const char* op,
                                           const uint32_t* pixels,
                                           int src_w, int src_h, int src_stride,
                                           float dst_x, float dst_y, float dst_w, float dst_h,
                                           uint8_t opacity, const RdtMatrix* transform,
                                           ImageSurface* resource_owner) {
    uint64_t generation = resource_owner ? resource_owner->generation : 0;
    if (paint_record_uses_ir(target)) {
        paint_draw_image(target->paint_list, pixels, src_w, src_h, src_stride,
                         dst_x, dst_y, dst_w, dst_h, opacity, transform,
                         resource_owner);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_draw_image(target->display_list, pixels, src_w, src_h, src_stride,
                      dst_x, dst_y, dst_w, dst_h, opacity, transform,
                      resource_owner, generation);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_draw_glyph(PaintRecordTarget* target, const char* op,
                                           GlyphBitmap* bitmap, int x, int y,
                                           Color color, bool is_color_emoji,
                                           const Bound* clip, const RdtMatrix* transform,
                                           uint64_t resource_generation) {
    if (paint_record_uses_ir(target)) {
        paint_draw_glyph(target->paint_list, bitmap, x, y, color, is_color_emoji,
                         clip, transform, resource_generation);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_draw_glyph(target->display_list, bitmap, x, y, color, is_color_emoji,
                      clip, transform, resource_generation);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_draw_picture(PaintRecordTarget* target, const char* op,
                                             RdtPicture* picture, uint8_t opacity,
                                             const RdtMatrix* transform) {
    if (paint_record_uses_ir(target)) {
        paint_draw_picture(target->paint_list, picture, opacity, transform);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_draw_picture(target->display_list, picture, opacity, transform);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_video_placeholder(PaintRecordTarget* target, const char* op,
                                                  void* video,
                                                  float dst_x, float dst_y,
                                                  float dst_w, float dst_h,
                                                  int object_fit, const Bound* clip,
                                                  uint64_t video_generation) {
    if (paint_record_uses_ir(target)) {
        paint_video_placeholder(target->paint_list, video, dst_x, dst_y, dst_w, dst_h,
                                object_fit, clip, video_generation);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_video_placeholder(target->display_list, video, dst_x, dst_y, dst_w, dst_h,
                             object_fit, clip, video_generation);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_webview_layer_placeholder(PaintRecordTarget* target,
                                                         const char* op, void* surface,
                                                         float dst_x, float dst_y,
                                                         float dst_w, float dst_h,
                                                         const Bound* clip,
                                                         uint64_t surface_generation) {
    if (paint_record_uses_ir(target)) {
        paint_webview_layer_placeholder(target->paint_list, surface, dst_x, dst_y,
                                        dst_w, dst_h, clip, surface_generation);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_webview_layer_placeholder(target->display_list, surface, dst_x, dst_y,
                                     dst_w, dst_h, clip, surface_generation);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_push_clip(PaintRecordTarget* target, const char* op,
                                          RdtPath* path, const RdtMatrix* transform) {
    if (paint_record_uses_ir(target)) {
        paint_push_clip(target->paint_list, path, transform);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_push_clip(target->display_list, path, transform);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_pop_clip(PaintRecordTarget* target, const char* op) {
    if (paint_record_uses_ir(target)) {
        paint_pop_clip(target->paint_list);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_pop_clip(target->display_list);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_save_backdrop(PaintRecordTarget* target, const char* op,
                                              int x0, int y0, int w, int h) {
    if (paint_record_uses_ir(target)) {
        paint_save_backdrop(target->paint_list, x0, y0, w, h);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_save_backdrop(target->display_list, x0, y0, w, h);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_composite_opacity(PaintRecordTarget* target, const char* op,
                                                  int x0, int y0, int w, int h,
                                                  float opacity,
                                                  bool premultiplied_source) {
    if (paint_record_uses_ir(target)) {
        paint_composite_opacity(target->paint_list, x0, y0, w, h,
                                opacity, premultiplied_source);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_composite_opacity(target->display_list, x0, y0, w, h,
                             opacity, premultiplied_source);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_apply_blend_mode(PaintRecordTarget* target, const char* op,
                                                 int x0, int y0, int w, int h,
                                                 int blend_mode) {
    if (paint_record_uses_ir(target)) {
        paint_apply_blend_mode(target->paint_list, x0, y0, w, h, blend_mode);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_apply_blend_mode(target->display_list, x0, y0, w, h, blend_mode);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_apply_filter(PaintRecordTarget* target, const char* op,
                                             float x, float y, float w, float h,
                                             void* filter, const Bound* clip) {
    if (paint_record_uses_ir(target)) {
        paint_apply_filter(target->paint_list, x, y, w, h, filter, clip);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_apply_filter(target->display_list, x, y, w, h, filter, clip);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_box_blur_region(PaintRecordTarget* target, const char* op,
                                                int rx, int ry, int rw, int rh,
                                                float blur_radius,
                                                int clip_type, const float* clip_params,
                                                int exclude_type, const float* exclude_params,
                                                bool premultiply_source,
                                                bool tint_source, Color tint_color) {
    if (paint_record_uses_ir(target)) {
        paint_box_blur_region(target->paint_list, rx, ry, rw, rh, blur_radius,
                              clip_type, clip_params, exclude_type, exclude_params,
                              premultiply_source, tint_source, tint_color);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_box_blur_region(target->display_list, rx, ry, rw, rh, blur_radius,
                           clip_type, clip_params, exclude_type, exclude_params,
                           premultiply_source, tint_source, tint_color);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_box_blur_inset(PaintRecordTarget* target, const char* op,
                                               int rx, int ry, int rw, int rh,
                                               int pad, float blur_radius,
                                               uint32_t bg_color) {
    if (paint_record_uses_ir(target)) {
        paint_box_blur_inset(target->paint_list, rx, ry, rw, rh, pad,
                             blur_radius, bg_color);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_box_blur_inset(target->display_list, rx, ry, rw, rh, pad,
                          blur_radius, bg_color);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_shadow_clip_save(PaintRecordTarget* target, const char* op,
                                                 int rx, int ry, int rw, int rh) {
    if (paint_record_uses_ir(target)) {
        paint_shadow_clip_save(target->paint_list, rx, ry, rw, rh);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_shadow_clip_save(target->display_list, rx, ry, rw, rh);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_shadow_clip_restore(PaintRecordTarget* target, const char* op,
                                                    int exclude_type,
                                                    const float* exclude_params,
                                                    int save_rx, int save_ry,
                                                    int save_rw, int save_rh,
                                                    int restore_inside) {
    if (paint_record_uses_ir(target)) {
        paint_shadow_clip_restore(target->paint_list, exclude_type, exclude_params,
                                  save_rx, save_ry, save_rw, save_rh,
                                  restore_inside);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_shadow_clip_restore(target->display_list, exclude_type, exclude_params,
                               save_rx, save_ry, save_rw, save_rh,
                               restore_inside);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_outer_shadow(PaintRecordTarget* target, const char* op,
                                             float shadow_x, float shadow_y,
                                             float shadow_w, float shadow_h,
                                             float sr_tl, float sr_tr,
                                             float sr_br, float sr_bl,
                                             Color color, float blur_radius,
                                             int exclude_type, const float* exclude_params,
                                             int clip_type, const float* clip_params) {
    if (paint_record_uses_ir(target)) {
        paint_outer_shadow(target->paint_list,
                           shadow_x, shadow_y, shadow_w, shadow_h,
                           sr_tl, sr_tr, sr_br, sr_bl,
                           color, blur_radius,
                           exclude_type, exclude_params,
                           clip_type, clip_params);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_outer_shadow(target->display_list,
                        shadow_x, shadow_y, shadow_w, shadow_h,
                        sr_tl, sr_tr, sr_br, sr_bl,
                        color, blur_radius,
                        exclude_type, exclude_params,
                        clip_type, clip_params);
    } else {
        paint_record_missing(target, op);
    }
}

static inline void paint_record_fill_surface_rect(PaintRecordTarget* target, const char* op,
                                                  float x, float y, float w, float h,
                                                  uint32_t color, const Bound* clip,
                                                  ClipShape** clip_shapes,
                                                  int clip_depth) {
    if (paint_record_uses_ir(target)) {
        paint_fill_surface_rect(target->paint_list, x, y, w, h,
                                color, clip, clip_shapes, clip_depth);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_fill_surface_rect(target->display_list, x, y, w, h,
                             color, clip, clip_shapes, clip_depth);
    } else {
        paint_record_missing(target, op);
    }
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
    if (paint_record_uses_ir(target)) {
        paint_blit_surface_scaled(target->paint_list, src, dst_x, dst_y,
                                  dst_w, dst_h, scale_mode, clip, clip_shapes,
                                  clip_depth, opacity, generation);
        paint_record_lower_pending(target);
    } else if (paint_record_has_display_list(target)) {
        dl_blit_surface_scaled(target->display_list, src, dst_x, dst_y,
                               dst_w, dst_h, scale_mode, clip, clip_shapes,
                               clip_depth, opacity, generation);
    } else {
        paint_record_missing(target, op);
    }
}
