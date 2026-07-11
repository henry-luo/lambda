#include "display_list.h"

#include "display_list_storage.hpp"
#include "display_list_bounds.hpp"

// ---------------------------------------------------------------------------
// Recording: direct-pixel and external-layer operations
// ---------------------------------------------------------------------------

void dl_fill_surface_rect(DisplayList* dl, float x, float y, float w, float h,
                          uint32_t color, const Bound* clip,
                          ClipShape** clip_shapes, int clip_depth) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_FILL_SURFACE_RECT;
    dl_set_clipped_rect_bounds(item, x, y, w, h, clip);
    item->fill_surface_rect.x = x;
    item->fill_surface_rect.y = y;
    item->fill_surface_rect.w = w;
    item->fill_surface_rect.h = h;
    item->fill_surface_rect.color = color;
    item->fill_surface_rect.clip = clip ? *clip : dl_unbounded_clip();
    dl_store_clip_shapes(dl, &item->fill_surface_rect.clip_shapes, clip_shapes, clip_depth);
}

void dl_blit_surface_scaled(DisplayList* dl, void* src_surface,
                            float dst_x, float dst_y, float dst_w, float dst_h,
                            int scale_mode, const Bound* clip,
                            ClipShape** clip_shapes, int clip_depth, uint8_t opacity,
                            uint64_t src_generation) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_BLIT_SURFACE_SCALED;
    dl_set_clipped_rect_bounds(item, dst_x, dst_y, dst_w, dst_h, clip);
    item->blit_surface_scaled.src_surface = src_surface;
    item->blit_surface_scaled.src_generation = src_generation;
    item->blit_surface_scaled.dst_x = dst_x;
    item->blit_surface_scaled.dst_y = dst_y;
    item->blit_surface_scaled.dst_w = dst_w;
    item->blit_surface_scaled.dst_h = dst_h;
    item->blit_surface_scaled.scale_mode = scale_mode;
    item->blit_surface_scaled.opacity = opacity;
    item->blit_surface_scaled.clip = clip ? *clip : dl_unbounded_clip();
    dl_store_clip_shapes(dl, &item->blit_surface_scaled.clip_shapes, clip_shapes, clip_depth);
}

void dl_video_placeholder(DisplayList* dl, void* video,
                          float dst_x, float dst_y, float dst_w, float dst_h,
                          int object_fit, const Bound* clip,
                          uint64_t video_generation) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_VIDEO_PLACEHOLDER;
    dl_set_clipped_rect_bounds(item, dst_x, dst_y, dst_w, dst_h, clip);
    item->video_placeholder.video = video;
    item->video_placeholder.video_generation = video_generation;
    item->video_placeholder.dst_x = dst_x;
    item->video_placeholder.dst_y = dst_y;
    item->video_placeholder.dst_w = dst_w;
    item->video_placeholder.dst_h = dst_h;
    item->video_placeholder.object_fit = object_fit;
    item->video_placeholder.clip = clip ? *clip : dl_unbounded_clip();
}

void dl_webview_layer_placeholder(DisplayList* dl, void* surface,
                                  float dst_x, float dst_y, float dst_w, float dst_h,
                                  const Bound* clip,
                                  uint64_t surface_generation) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_WEBVIEW_LAYER_PLACEHOLDER;
    dl_set_clipped_rect_bounds(item, dst_x, dst_y, dst_w, dst_h, clip);
    item->webview_layer_placeholder.surface = surface;
    item->webview_layer_placeholder.surface_generation = surface_generation;
    item->webview_layer_placeholder.dst_x = dst_x;
    item->webview_layer_placeholder.dst_y = dst_y;
    item->webview_layer_placeholder.dst_w = dst_w;
    item->webview_layer_placeholder.dst_h = dst_h;
    item->webview_layer_placeholder.clip = clip ? *clip : dl_unbounded_clip();
}
