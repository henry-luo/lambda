#include "display_list_replay_raster.hpp"

#include "display_list_storage.hpp"
#include "render_raster.hpp"

void dl_replay_fill_surface_rect(ImageSurface* surface,
                                 const DisplayReplayDirtyClip* dirty_clip,
                                 const DlFillSurfaceRect* fill) {
    if (!fill) return;
    Rect rect = {fill->x, fill->y, fill->w, fill->h};
    Bound bound = fill->clip;
    dl_replay_intersect_dirty_clip(dirty_clip, &bound);

    ClipShape shapes[RDT_MAX_CLIP_SHAPES];
    ClipShape* shape_ptrs[RDT_MAX_CLIP_SHAPES];
    int clip_depth = dl_restore_clip_shapes(&fill->clip_shapes, shapes, shape_ptrs);
    RasterPaintContext raster = {surface, &bound, shape_ptrs, clip_depth};
    raster_fill_rect(&raster, &rect, fill->color);
}

void dl_replay_blit_surface_scaled(ImageSurface* surface,
                                   const DisplayReplayDirtyClip* dirty_clip,
                                   const DlBlitSurfaceScaled* blit) {
    if (!blit) return;
    Rect dst_rect = {blit->dst_x, blit->dst_y, blit->dst_w, blit->dst_h};
    Bound bound = blit->clip;
    dl_replay_intersect_dirty_clip(dirty_clip, &bound);

    ClipShape shapes[RDT_MAX_CLIP_SHAPES];
    ClipShape* shape_ptrs[RDT_MAX_CLIP_SHAPES];
    int clip_depth = dl_restore_clip_shapes(&blit->clip_shapes, shapes, shape_ptrs);
    RasterPaintContext raster = {surface, &bound, shape_ptrs, clip_depth};
    raster_blit_surface_scaled(&raster, (ImageSurface*)blit->src_surface, nullptr,
                               &dst_rect, (ScaleMode)blit->scale_mode, blit->opacity);
}

void dl_replay_webview_layer_placeholder(ImageSurface* surface,
                                         const DlWebviewLayerPlaceholder* placeholder) {
    if (!placeholder) return;
    ImageSurface* src = (ImageSurface*)placeholder->surface;
    if (!src || !src->pixels) return;

    Rect dst_rect = {placeholder->dst_x, placeholder->dst_y,
                     placeholder->dst_w, placeholder->dst_h};
    Bound bound = placeholder->clip;
    RasterPaintContext raster = {surface, &bound, nullptr, 0};
    raster_blit_surface_scaled(&raster, src, nullptr, &dst_rect, SCALE_MODE_LINEAR);
}
