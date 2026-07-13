#include "render.hpp"

// ---------------------------------------------------------------------------
// Painter gateway (rc_*).
//
// Primary recording mode per primitive:
//   Semantic IR (rdcon->paint_list && rdcon->dl): record through the
//   PaintBuilder, then lower to the display list immediately so command order
//   is preserved. This routes the live raster path through the semantic paint
//   IR and is byte-identical to direct dl_* recording (proven by
//   PaintIrParityTest).
//
// render_paint_gateway.hpp owns the common PaintIR/display-list gateway used
// here and by the inline SVG painter. Fragment lowering skips whole-list stack
// validation because paired display-list effects can span multiple immediate
// flushes while preserving paint order.
// ---------------------------------------------------------------------------

static inline PaintRecordTarget rc_record_target(RenderContext* rdcon) {
    PaintRecordTarget target = {
        rdcon ? rdcon->paint_list : nullptr,
        rdcon ? rdcon->dl : nullptr,
        "PAINTER"
    };
    return target;
}

void rc_fill_rect(RenderContext* rdcon, float x, float y, float w, float h, Color color) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_fill_rect(&target, "rc_fill_rect", x, y, w, h, color);
}

void rc_fill_rounded_rect(RenderContext* rdcon, float x, float y, float w, float h,
                          float rx, float ry, Color color) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_fill_rounded_rect(&target, "rc_fill_rounded_rect",
                                   x, y, w, h, rx, ry, color);
}

void rc_fill_path(RenderContext* rdcon, RdtPath* path, Color color,
                  RdtFillRule rule, const RdtMatrix* transform) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_fill_path(&target, "rc_fill_path", path, color, rule, transform);
}

void rc_stroke_path(RenderContext* rdcon, RdtPath* path, Color color, float width,
                    RdtStrokeCap cap, RdtStrokeJoin join,
                    const float* dash_array, int dash_count,
                    const RdtMatrix* transform, float dash_phase) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_stroke_path(&target, "rc_stroke_path", path, color, width,
                             cap, join, dash_array, dash_count,
                             dash_phase, transform);
}

void rc_fill_linear_gradient(RenderContext* rdcon, RdtPath* path,
                             float x1, float y1, float x2, float y2,
                             const RdtGradientStop* stops, int stop_count,
                             RdtFillRule rule, const RdtMatrix* transform) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_fill_linear_gradient(&target, "rc_fill_linear_gradient",
                                      path, x1, y1, x2, y2,
                                      stops, stop_count, rule, transform, nullptr);
}

void rc_fill_radial_gradient(RenderContext* rdcon, RdtPath* path,
                             float cx, float cy, float r,
                             const RdtGradientStop* stops, int stop_count,
                             RdtFillRule rule, const RdtMatrix* transform) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_fill_radial_gradient(&target, "rc_fill_radial_gradient",
                                      path, cx, cy, r,
                                      stops, stop_count, rule, transform, nullptr);
}

void rc_draw_image(RenderContext* rdcon, const uint32_t* pixels,
                   int src_w, int src_h, int src_stride,
                   float dst_x, float dst_y, float dst_w, float dst_h,
                   uint8_t opacity, const RdtMatrix* transform,
                   ImageSurface* resource_owner) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_draw_image(&target, "rc_draw_image", pixels, src_w, src_h,
                            src_stride, dst_x, dst_y, dst_w, dst_h,
                            opacity, transform, resource_owner);
}

void rc_draw_glyph(RenderContext* rdcon, GlyphBitmap* bitmap, int x, int y,
                   Color color, bool is_color_emoji, const Bound* clip,
                   const RdtMatrix* transform, uint64_t resource_generation) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_draw_glyph(&target, "rc_draw_glyph", bitmap, x, y, color,
                            is_color_emoji, clip, transform, resource_generation);
}

void rc_draw_picture(RenderContext* rdcon, RdtPicture* picture,
                     uint8_t opacity, const RdtMatrix* transform) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_draw_picture(&target, "rc_draw_picture", picture, opacity, transform);
}

void rc_video_placeholder(RenderContext* rdcon, void* video,
                          float dst_x, float dst_y, float dst_w, float dst_h,
                          int object_fit, const Bound* clip,
                          uint64_t video_generation) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_video_placeholder(&target, "rc_video_placeholder", video,
                                   dst_x, dst_y, dst_w, dst_h,
                                   object_fit, clip, video_generation);
}

void rc_webview_layer_placeholder(RenderContext* rdcon, void* surface,
                                  float dst_x, float dst_y, float dst_w, float dst_h,
                                  const Bound* clip,
                                  uint64_t surface_generation) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_webview_layer_placeholder(&target, "rc_webview_layer_placeholder",
                                           surface, dst_x, dst_y, dst_w, dst_h,
                                           clip, surface_generation);
}

void rc_push_clip(RenderContext* rdcon, RdtPath* clip_path, const RdtMatrix* transform) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_push_clip(&target, "rc_push_clip", clip_path, transform);
}

void rc_pop_clip(RenderContext* rdcon) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_pop_clip(&target, "rc_pop_clip");
}

void rc_save_backdrop(RenderContext* rdcon, int x0, int y0, int w, int h) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_save_backdrop(&target, "rc_save_backdrop", x0, y0, w, h);
}

void rc_composite_opacity(RenderContext* rdcon, int x0, int y0, int w, int h,
                          float opacity, bool premultiplied_source) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_composite_opacity(&target, "rc_composite_opacity", x0, y0, w, h,
                                   opacity, premultiplied_source);
}

void rc_apply_blend_mode(RenderContext* rdcon, int x0, int y0, int w, int h,
                         int blend_mode) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_apply_blend_mode(&target, "rc_apply_blend_mode",
                                  x0, y0, w, h, blend_mode);
}

void rc_apply_filter(RenderContext* rdcon, float x, float y, float w, float h,
                     void* filter, const Bound* clip) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_apply_filter(&target, "rc_apply_filter", x, y, w, h, filter, clip);
}

void rc_box_blur_region(RenderContext* rdcon, int rx, int ry, int rw, int rh,
                        float blur_radius, int clip_type, const float* clip_params,
                        int exclude_type, const float* exclude_params,
                        bool premultiply_source,
                        bool tint_source, Color tint_color) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_box_blur_region(&target, "rc_box_blur_region",
                                 rx, ry, rw, rh, blur_radius,
                                 clip_type, clip_params, exclude_type, exclude_params,
                                 premultiply_source, tint_source, tint_color);
}

void rc_box_blur_inset(RenderContext* rdcon, int rx, int ry, int rw, int rh,
                       int pad, float blur_radius, uint32_t bg_color) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_box_blur_inset(&target, "rc_box_blur_inset",
                                rx, ry, rw, rh, pad, blur_radius, bg_color);
}

void rc_shadow_clip_save(RenderContext* rdcon, int rx, int ry, int rw, int rh) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_shadow_clip_save(&target, "rc_shadow_clip_save", rx, ry, rw, rh);
}

void rc_shadow_clip_restore(RenderContext* rdcon, int exclude_type, const float* exclude_params,
                            int save_rx, int save_ry, int save_rw, int save_rh,
                            int restore_inside) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_shadow_clip_restore(&target, "rc_shadow_clip_restore",
                                     exclude_type, exclude_params,
                                     save_rx, save_ry, save_rw, save_rh,
                                     restore_inside);
}

void rc_outer_shadow(RenderContext* rdcon,
                     float shadow_x, float shadow_y, float shadow_w, float shadow_h,
                     float sr_tl, float sr_tr, float sr_br, float sr_bl,
                     Color color, float blur_radius,
                     int exclude_type, const float* exclude_params,
                     int clip_type, const float* clip_params) {
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_outer_shadow(&target, "rc_outer_shadow",
                              shadow_x, shadow_y, shadow_w, shadow_h,
                              sr_tl, sr_tr, sr_br, sr_bl,
                              color, blur_radius,
                              exclude_type, exclude_params,
                              clip_type, clip_params);
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
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_fill_surface_rect(&target, "render_painter_fill_surface_rect",
                                   rect->x, rect->y, rect->width, rect->height,
                                   color, clip, clip_shapes, clip_depth);
}

void render_painter_blit_surface_scaled(RenderContext* rdcon,
                                        ImageSurface* src, Rect* src_rect,
                                        ImageSurface* dst, Rect* dst_rect, Bound* clip,
                                        ScaleMode scale_mode,
                                        ClipShape** clip_shapes, int clip_depth,
                                        uint8_t opacity) {
    (void)src_rect;
    (void)dst;
    PaintRecordTarget target = rc_record_target(rdcon);
    paint_record_blit_surface_scaled(&target, "render_painter_blit_surface_scaled",
                                     src, dst_rect->x, dst_rect->y,
                                     dst_rect->width, dst_rect->height,
                                     (int)scale_mode, clip, clip_shapes,
                                     clip_depth, opacity);
}

void rc_fill_surface_rect(RenderContext* rdcon, ImageSurface* surface,
                          Rect* rect, uint32_t color, Bound* clip,
                          ClipShape** clip_shapes, int clip_depth) {
    render_painter_fill_surface_rect(rdcon, surface, rect, color, clip, clip_shapes, clip_depth);
}
