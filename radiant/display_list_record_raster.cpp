#include "display_list.h"

#include "display_list_storage.hpp"

static Bound dl_default_record_clip() {
    Bound clip = {0, 0, 99999, 99999};
    return clip;
}

static float dl_raster_min_f(float a, float b) {
    return a < b ? a : b;
}

static float dl_raster_max_f(float a, float b) {
    return a > b ? a : b;
}

static void dl_raster_set_clipped_bounds(DisplayItem* item,
                                         float x, float y, float w, float h,
                                         const Bound* clip) {
    if (!item) return;
    float left = x;
    float top = y;
    float right = x + w;
    float bottom = y + h;
    if (clip) {
        left = dl_raster_max_f(left, clip->left);
        top = dl_raster_max_f(top, clip->top);
        right = dl_raster_min_f(right, clip->right);
        bottom = dl_raster_min_f(bottom, clip->bottom);
    }
    item->bounds[0] = left;
    item->bounds[1] = top;
    item->bounds[2] = right > left ? right - left : 0.0f;
    item->bounds[3] = bottom > top ? bottom - top : 0.0f;
}

// ---------------------------------------------------------------------------
// Recording: direct-pixel and external-layer operations
// ---------------------------------------------------------------------------

void dl_fill_surface_rect(DisplayList* dl, float x, float y, float w, float h,
                          uint32_t color, const Bound* clip,
                          ClipShape** clip_shapes, int clip_depth) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_FILL_SURFACE_RECT;
    dl_raster_set_clipped_bounds(item, x, y, w, h, clip);
    item->fill_surface_rect.x = x;
    item->fill_surface_rect.y = y;
    item->fill_surface_rect.w = w;
    item->fill_surface_rect.h = h;
    item->fill_surface_rect.color = color;
    item->fill_surface_rect.clip = clip ? *clip : dl_default_record_clip();
    dl_store_clip_shapes(dl, &item->fill_surface_rect.clip_shapes, clip_shapes, clip_depth);
}

void dl_blit_surface_scaled(DisplayList* dl, void* src_surface,
                            float dst_x, float dst_y, float dst_w, float dst_h,
                            int scale_mode, const Bound* clip,
                            ClipShape** clip_shapes, int clip_depth, uint8_t opacity) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_BLIT_SURFACE_SCALED;
    dl_raster_set_clipped_bounds(item, dst_x, dst_y, dst_w, dst_h, clip);
    item->blit_surface_scaled.src_surface = src_surface;
    item->blit_surface_scaled.dst_x = dst_x;
    item->blit_surface_scaled.dst_y = dst_y;
    item->blit_surface_scaled.dst_w = dst_w;
    item->blit_surface_scaled.dst_h = dst_h;
    item->blit_surface_scaled.scale_mode = scale_mode;
    item->blit_surface_scaled.opacity = opacity;
    item->blit_surface_scaled.clip = clip ? *clip : dl_default_record_clip();
    dl_store_clip_shapes(dl, &item->blit_surface_scaled.clip_shapes, clip_shapes, clip_depth);
}

void dl_video_placeholder(DisplayList* dl, void* video,
                          float dst_x, float dst_y, float dst_w, float dst_h,
                          int object_fit, const Bound* clip) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_VIDEO_PLACEHOLDER;
    dl_raster_set_clipped_bounds(item, dst_x, dst_y, dst_w, dst_h, clip);
    item->video_placeholder.video = video;
    item->video_placeholder.dst_x = dst_x;
    item->video_placeholder.dst_y = dst_y;
    item->video_placeholder.dst_w = dst_w;
    item->video_placeholder.dst_h = dst_h;
    item->video_placeholder.object_fit = object_fit;
    item->video_placeholder.clip = clip ? *clip : dl_default_record_clip();
}

void dl_webview_layer_placeholder(DisplayList* dl, void* surface,
                                  float dst_x, float dst_y, float dst_w, float dst_h,
                                  const Bound* clip) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_WEBVIEW_LAYER_PLACEHOLDER;
    dl_raster_set_clipped_bounds(item, dst_x, dst_y, dst_w, dst_h, clip);
    item->webview_layer_placeholder.surface = surface;
    item->webview_layer_placeholder.dst_x = dst_x;
    item->webview_layer_placeholder.dst_y = dst_y;
    item->webview_layer_placeholder.dst_w = dst_w;
    item->webview_layer_placeholder.dst_h = dst_h;
    item->webview_layer_placeholder.clip = clip ? *clip : dl_default_record_clip();
}
