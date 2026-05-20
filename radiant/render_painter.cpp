#include "render.hpp"
#include "render_raster.hpp"

void rc_fill_rect(RenderContext* rdcon, float x, float y, float w, float h, Color color) {
    if (rdcon->dl) dl_fill_rect(rdcon->dl, x, y, w, h, color);
    else rdt_fill_rect(&rdcon->vec, x, y, w, h, color);
}

void rc_fill_rounded_rect(RenderContext* rdcon, float x, float y, float w, float h,
                          float rx, float ry, Color color) {
    if (rdcon->dl) dl_fill_rounded_rect(rdcon->dl, x, y, w, h, rx, ry, color);
    else rdt_fill_rounded_rect(&rdcon->vec, x, y, w, h, rx, ry, color);
}

void rc_fill_path(RenderContext* rdcon, RdtPath* path, Color color,
                  RdtFillRule rule, const RdtMatrix* transform) {
    if (rdcon->dl) dl_fill_path(rdcon->dl, path, color, rule, transform);
    else rdt_fill_path(&rdcon->vec, path, color, rule, transform);
}

void rc_stroke_path(RenderContext* rdcon, RdtPath* path, Color color, float width,
                    RdtStrokeCap cap, RdtStrokeJoin join,
                    const float* dash_array, int dash_count,
                    const RdtMatrix* transform, float dash_phase) {
    if (rdcon->dl) {
        dl_stroke_path(rdcon->dl, path, color, width, cap, join,
                       dash_array, dash_count, dash_phase, transform);
    } else {
        rdt_stroke_path(&rdcon->vec, path, color, width, cap, join,
                        dash_array, dash_count, dash_phase, transform);
    }
}

void rc_fill_linear_gradient(RenderContext* rdcon, RdtPath* path,
                             float x1, float y1, float x2, float y2,
                             const RdtGradientStop* stops, int stop_count,
                             RdtFillRule rule, const RdtMatrix* transform) {
    if (rdcon->dl) {
        dl_fill_linear_gradient(rdcon->dl, path, x1, y1, x2, y2,
                                stops, stop_count, rule, transform);
    } else {
        rdt_fill_linear_gradient(&rdcon->vec, path, x1, y1, x2, y2,
                                 stops, stop_count, rule, transform);
    }
}

void rc_fill_radial_gradient(RenderContext* rdcon, RdtPath* path,
                             float cx, float cy, float r,
                             const RdtGradientStop* stops, int stop_count,
                             RdtFillRule rule, const RdtMatrix* transform) {
    if (rdcon->dl) {
        dl_fill_radial_gradient(rdcon->dl, path, cx, cy, r,
                                stops, stop_count, rule, transform);
    } else {
        rdt_fill_radial_gradient(&rdcon->vec, path, cx, cy, r,
                                 stops, stop_count, rule, transform);
    }
}

void rc_draw_image(RenderContext* rdcon, const uint32_t* pixels,
                   int src_w, int src_h, int src_stride,
                   float dst_x, float dst_y, float dst_w, float dst_h,
                   uint8_t opacity, const RdtMatrix* transform,
                   ImageSurface* resource_owner) {
    if (rdcon->dl) {
        dl_draw_image(rdcon->dl, pixels, src_w, src_h, src_stride,
                      dst_x, dst_y, dst_w, dst_h, opacity, transform,
                      resource_owner,
                      resource_owner ? resource_owner->generation : 0);
    } else {
        rdt_draw_image(&rdcon->vec, pixels, src_w, src_h, src_stride,
                       dst_x, dst_y, dst_w, dst_h, opacity, transform,
                       resource_owner ? resource_owner->generation : 0);
    }
}

void rc_draw_picture(RenderContext* rdcon, RdtPicture* picture,
                     uint8_t opacity, const RdtMatrix* transform) {
    if (rdcon->dl) dl_draw_picture(rdcon->dl, picture, opacity, transform);
    else rdt_picture_draw(&rdcon->vec, picture, opacity, transform);
}

void rc_push_clip(RenderContext* rdcon, RdtPath* clip_path, const RdtMatrix* transform) {
    if (rdcon->dl) dl_push_clip(rdcon->dl, clip_path, transform);
    else rdt_push_clip(&rdcon->vec, clip_path, transform);
}

void rc_pop_clip(RenderContext* rdcon) {
    if (rdcon->dl) dl_pop_clip(rdcon->dl);
    else rdt_pop_clip(&rdcon->vec);
}

void render_painter_begin_vector_batch(RenderContext* rdcon) {
    if (!rdcon || rdcon->dl) return;
    rdt_vector_begin_batch(&rdcon->vec);
}

void render_painter_flush_vector_batch(RenderContext* rdcon) {
    if (!rdcon || rdcon->dl) return;
    rdt_vector_flush_batch(&rdcon->vec);
}

void render_painter_end_vector_batch(RenderContext* rdcon) {
    if (!rdcon || rdcon->dl) return;
    rdt_vector_end_batch(&rdcon->vec);
}

void render_painter_draw_picture_rect(RenderContext* rdcon, RdtPicture* picture,
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
                  opacity, nullptr, resource_owner);

    if (clip) rc_pop_clip(rdcon);
}

void render_painter_fill_surface_rect(RenderContext* rdcon, ImageSurface* surface,
                                      Rect* rect, uint32_t color, Bound* clip,
                                      ClipShape** clip_shapes, int clip_depth) {
    if (rdcon->dl) {
        dl_fill_surface_rect(rdcon->dl, rect->x, rect->y, rect->width, rect->height,
                             color, clip, clip_shapes, clip_depth);
    } else {
        render_painter_flush_vector_batch(rdcon);
        RasterPaintContext raster = {surface, clip, clip_shapes, clip_depth};
        raster_fill_rect(&raster, rect, color);
    }
}

void render_painter_blit_surface_scaled(RenderContext* rdcon,
                                        ImageSurface* src, Rect* src_rect,
                                        ImageSurface* dst, Rect* dst_rect, Bound* clip,
                                        ScaleMode scale_mode,
                                        ClipShape** clip_shapes, int clip_depth,
                                        uint8_t opacity) {
    if (rdcon->dl) {
        dl_blit_surface_scaled(rdcon->dl, src, dst_rect->x, dst_rect->y,
                               dst_rect->width, dst_rect->height, (int)scale_mode,
                               clip, clip_shapes, clip_depth, opacity,
                               src ? src->generation : 0);
    } else {
        render_painter_flush_vector_batch(rdcon);
        RasterPaintContext raster = {dst, clip, clip_shapes, clip_depth};
        raster_blit_surface_scaled(&raster, src, src_rect, dst_rect, scale_mode, opacity);
    }
}

void rc_fill_surface_rect(RenderContext* rdcon, ImageSurface* surface,
                          Rect* rect, uint32_t color, Bound* clip,
                          ClipShape** clip_shapes, int clip_depth) {
    render_painter_fill_surface_rect(rdcon, surface, rect, color, clip, clip_shapes, clip_depth);
}
