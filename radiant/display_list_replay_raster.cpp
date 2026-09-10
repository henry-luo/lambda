#include "render.hpp"
#include "../lib/log.h"

static Bound dl_replay_offset_clip_to_surface(const Bound* clip, ImageSurface* surface,
                                              float offset_x, float offset_y) {
    Bound bound;
    // Tile replay historically clamps each edge only on the side that can exceed the tile.
    bound.left = clip->left - offset_x;
    if (bound.left < 0) bound.left = 0;
    bound.top = clip->top - offset_y;
    if (bound.top < 0) bound.top = 0;
    bound.right = clip->right - offset_x;
    if (bound.right > surface->width) bound.right = (float)surface->width;
    bound.bottom = clip->bottom - offset_y;
    if (bound.bottom > surface->height) bound.bottom = (float)surface->height;
    return bound;
}

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

void dl_replay_fill_surface_rect_at_offset(ImageSurface* surface, ScratchArena* scratch,
                                           const DlFillSurfaceRect* fill,
                                           float offset_x, float offset_y) {
    if (!fill) return;
    Rect rect = {fill->x - offset_x, fill->y - offset_y, fill->w, fill->h};
    Bound bound = dl_replay_offset_clip_to_surface(&fill->clip, surface, offset_x, offset_y);

    ClipShape shapes[RDT_MAX_CLIP_SHAPES];
    ClipShape* shape_ptrs[RDT_MAX_CLIP_SHAPES];
    ScratchMark clip_mark = {};
    if (scratch) clip_mark = scratch_mark(scratch);
    int clip_depth = dl_restore_clip_shapes(&fill->clip_shapes, shapes, shape_ptrs,
                                            scratch, offset_x, offset_y);
    RasterPaintContext raster = {surface, &bound, shape_ptrs, clip_depth};
    raster_fill_rect(&raster, &rect, fill->color);
    if (scratch) scratch_restore(scratch, clip_mark);
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

void dl_replay_blit_surface_scaled_at_offset(ImageSurface* surface, ScratchArena* scratch,
                                             const DlBlitSurfaceScaled* blit,
                                             float offset_x, float offset_y) {
    if (!blit) return;
    Rect dst_rect = {blit->dst_x - offset_x, blit->dst_y - offset_y,
                     blit->dst_w, blit->dst_h};
    Bound bound = dl_replay_offset_clip_to_surface(&blit->clip, surface, offset_x, offset_y);

    ClipShape shapes[RDT_MAX_CLIP_SHAPES];
    ClipShape* shape_ptrs[RDT_MAX_CLIP_SHAPES];
    ScratchMark clip_mark = {};
    if (scratch) clip_mark = scratch_mark(scratch);
    int clip_depth = dl_restore_clip_shapes(&blit->clip_shapes, shapes, shape_ptrs,
                                            scratch, offset_x, offset_y);
    RasterPaintContext raster = {surface, &bound, shape_ptrs, clip_depth};
    raster_blit_surface_scaled(&raster, (ImageSurface*)blit->src_surface, nullptr,
                               &dst_rect, (ScaleMode)blit->scale_mode, blit->opacity);
    if (scratch) scratch_restore(scratch, clip_mark);
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

void dl_replay_webview_layer_placeholder_at_offset(ImageSurface* surface,
                                                   const DlWebviewLayerPlaceholder* placeholder,
                                                   float offset_x, float offset_y) {
    if (!placeholder) return;
    ImageSurface* src = (ImageSurface*)placeholder->surface;
    if (!src || !src->pixels) return;

    Rect dst_rect = {placeholder->dst_x - offset_x, placeholder->dst_y - offset_y,
                     placeholder->dst_w, placeholder->dst_h};
    Bound bound = dl_replay_offset_clip_to_surface(&placeholder->clip, surface,
                                                  offset_x, offset_y);
    RasterPaintContext raster = {surface, &bound, nullptr, 0};
    raster_blit_surface_scaled(&raster, src, nullptr, &dst_rect, SCALE_MODE_LINEAR);
}
