#include "render.hpp"

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

static void dl_offset_clip_shape(ClipShape* cs, float offset_x, float offset_y) {
    if (!cs) return;
    switch (cs->type) {
        case CLIP_SHAPE_CIRCLE:
            cs->circle.cx -= offset_x;
            cs->circle.cy -= offset_y;
            break;
        case CLIP_SHAPE_ELLIPSE:
            cs->ellipse.cx -= offset_x;
            cs->ellipse.cy -= offset_y;
            break;
        case CLIP_SHAPE_INSET:
            cs->inset.x -= offset_x;
            cs->inset.y -= offset_y;
            break;
        case CLIP_SHAPE_ROUNDED_RECT:
            cs->rounded_rect.x -= offset_x;
            cs->rounded_rect.y -= offset_y;
            break;
        default:
            break;
    }
}

static int dl_restore_clip_shapes_at_offset(const DlClipShapeStack* src, ClipShape* shapes,
                                            ClipShape** shape_ptrs, ScratchArena* scratch,
                                            float offset_x, float offset_y) {
    if (!src || !shapes || !shape_ptrs || src->depth <= 0) return 0;
    int depth = src->depth;
    if (depth > RDT_MAX_CLIP_SHAPES) {
        log_warn("[RAD_CAP_TILE_CLIP_RESTORE] truncating raster clip stack from %d to %d shapes",
                 depth, RDT_MAX_CLIP_SHAPES);
        depth = RDT_MAX_CLIP_SHAPES;
    }
    int out_depth = 0;
    for (int i = 0; i < depth; i++) {
        if (src->type[i] == CLIP_SHAPE_NONE) continue;
        if (src->type[i] == CLIP_SHAPE_POLYGON) {
            int count = src->polygon_count[i];
            if (!scratch || count < 3 || !src->polygon_vx[i] || !src->polygon_vy[i]) continue;
            float* vx = (float*)scratch_alloc(scratch, count * sizeof(float));
            float* vy = (float*)scratch_alloc(scratch, count * sizeof(float));
            if (!vx || !vy) continue;
            for (int pi = 0; pi < count; pi++) {
                vx[pi] = src->polygon_vx[i][pi] - offset_x;
                vy[pi] = src->polygon_vy[i][pi] - offset_y;
            }
            shapes[out_depth].type = CLIP_SHAPE_POLYGON;
            shapes[out_depth].polygon = {vx, vy, count};
        } else {
            shapes[out_depth] = clip_shape_from_params(src->type[i], src->params[i]);
            dl_offset_clip_shape(&shapes[out_depth], offset_x, offset_y);
        }
        shape_ptrs[out_depth] = &shapes[out_depth];
        out_depth++;
    }
    return out_depth;
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
    int clip_depth = dl_restore_clip_shapes_at_offset(&fill->clip_shapes, shapes, shape_ptrs,
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
    int clip_depth = dl_restore_clip_shapes_at_offset(&blit->clip_shapes, shapes, shape_ptrs,
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
