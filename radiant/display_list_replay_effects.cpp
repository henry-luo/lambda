#include "display_list_replay_effects.hpp"

#include "display_list_surface_region.hpp"
#include "render_background.hpp"
#include "render_filter.hpp"
#include <string.h>

static Bound dl_replay_clip_at_offset(const Bound* clip, const ImageSurface* surface,
                                      float offset_x, float offset_y) {
    float max_w = surface ? (float)surface->width : 0.0f;
    float max_h = surface ? (float)surface->height : 0.0f;
    Bound bound = {};
    if (!clip) {
        bound.right = max_w;
        bound.bottom = max_h;
        return bound;
    }
    bound.left = clip->left - offset_x;
    if (bound.left < 0.0f) bound.left = 0.0f;
    bound.top = clip->top - offset_y;
    if (bound.top < 0.0f) bound.top = 0.0f;
    bound.right = clip->right - offset_x;
    if (bound.right > max_w) bound.right = max_w;
    bound.bottom = clip->bottom - offset_y;
    if (bound.bottom > max_h) bound.bottom = max_h;
    return bound;
}

static void dl_replay_offset_clip_params(int clip_type, const float* src, float* dst,
                                         float offset_x, float offset_y) {
    if (!src || !dst) return;
    memcpy(dst, src, 8 * sizeof(float));
    switch ((ClipShapeType)clip_type) {
        case CLIP_SHAPE_CIRCLE:
        case CLIP_SHAPE_ELLIPSE:
        case CLIP_SHAPE_INSET:
        case CLIP_SHAPE_ROUNDED_RECT:
            dst[0] -= offset_x;
            dst[1] -= offset_y;
            break;
        default:
            break;
    }
}

void dl_replay_apply_filter(ScratchArena* scratch,
                            ImageSurface* surface,
                            const RenderBackendCaps* caps,
                            const DisplayReplayDirtyClip* dirty_clip,
                            const DlApplyFilter* filter) {
    if (!filter) return;
    Rect rect = {filter->x, filter->y, filter->w, filter->h};
    Bound bound = filter->clip;
    dl_replay_intersect_dirty_clip(dirty_clip, &bound);
    render_filter_apply_with_backend(caps, scratch, surface, (FilterProp*)filter->filter,
                                     &rect, &bound);
}

void dl_replay_apply_filter_at_offset(ScratchArena* scratch,
                                      ImageSurface* surface,
                                      const RenderBackendCaps* caps,
                                      const DlApplyFilter* filter,
                                      float offset_x, float offset_y) {
    if (!filter) return;
    Rect rect = {filter->x - offset_x, filter->y - offset_y, filter->w, filter->h};
    Bound bound = dl_replay_clip_at_offset(&filter->clip, surface, offset_x, offset_y);
    render_filter_apply_with_backend(caps, scratch, surface, (FilterProp*)filter->filter,
                                     &rect, &bound);
}

void dl_replay_box_blur_region(ScratchArena* scratch,
                               ImageSurface* surface,
                               const DlBoxBlurRegion* blur) {
    if (!blur) return;
    if (blur->premultiply_source) {
        premultiply_surface_region(surface, blur->rx, blur->ry, blur->rw, blur->rh);
    }
    if (blur->tint_source) {
        tint_premultiplied_surface_region(surface, blur->rx, blur->ry,
                                          blur->rw, blur->rh, blur->tint_color);
    }
    if (blur->clip_type && surface && surface->pixels) {
        IRect region = {};
        uint32_t* saved = surface_region_save(surface, scratch,
                                              blur->rx, blur->ry, blur->rw, blur->rh,
                                              &region);
        box_blur_region(scratch, surface, blur->rx, blur->ry, blur->rw, blur->rh, blur->blur_radius);
        if (saved) {
            ClipShape cs = clip_shape_from_params(blur->clip_type, blur->clip_params);
            surface_region_restore_masked(surface, saved, &region, &cs, false);
        }
        return;
    }

    box_blur_region(scratch, surface, blur->rx, blur->ry, blur->rw, blur->rh, blur->blur_radius);
}

void dl_replay_box_blur_region_at_offset(ScratchArena* scratch,
                                         ImageSurface* surface,
                                         const DlBoxBlurRegion* blur,
                                         float offset_x, float offset_y) {
    if (!blur) return;
    DlBoxBlurRegion shifted = *blur;
    shifted.rx = blur->rx - (int)offset_x;
    shifted.ry = blur->ry - (int)offset_y;
    if (blur->clip_type) {
        dl_replay_offset_clip_params(blur->clip_type, blur->clip_params,
                                     shifted.clip_params, offset_x, offset_y);
    }
    dl_replay_box_blur_region(scratch, surface, &shifted);
}

void dl_replay_box_blur_inset(ScratchArena* scratch,
                              ImageSurface* surface,
                              const DlBoxBlurInset* blur) {
    if (!blur) return;
    box_blur_region_inset(scratch, surface, blur->rx, blur->ry, blur->rw, blur->rh,
                          blur->pad, blur->blur_radius, blur->bg_color);
}

void dl_replay_box_blur_inset_at_offset(ScratchArena* scratch,
                                        ImageSurface* surface,
                                        const DlBoxBlurInset* blur,
                                        float offset_x, float offset_y) {
    if (!blur) return;
    DlBoxBlurInset shifted = *blur;
    shifted.rx = blur->rx - (int)offset_x;
    shifted.ry = blur->ry - (int)offset_y;
    dl_replay_box_blur_inset(scratch, surface, &shifted);
}

void dl_replay_outer_shadow(ScratchArena* scratch,
                            ImageSurface* surface,
                            const DlOuterShadow* shadow) {
    if (!shadow) return;
    render_outer_shadow_blur_composite(
        scratch, surface,
        shadow->shadow_x, shadow->shadow_y, shadow->shadow_w, shadow->shadow_h,
        shadow->sr_tl, shadow->sr_tr, shadow->sr_br, shadow->sr_bl,
        shadow->color, shadow->blur_radius,
        shadow->exclude_type, shadow->exclude_params,
        shadow->clip_type, shadow->clip_params);
}

void dl_replay_outer_shadow_at_offset(ScratchArena* scratch,
                                      ImageSurface* surface,
                                      const DlOuterShadow* shadow,
                                      float offset_x, float offset_y) {
    if (!shadow) return;
    DlOuterShadow shifted = *shadow;
    shifted.shadow_x = shadow->shadow_x - offset_x;
    shifted.shadow_y = shadow->shadow_y - offset_y;
    if (shadow->exclude_type) {
        dl_replay_offset_clip_params(shadow->exclude_type, shadow->exclude_params,
                                     shifted.exclude_params, offset_x, offset_y);
    }
    if (shadow->clip_type) {
        dl_replay_offset_clip_params(shadow->clip_type, shadow->clip_params,
                                     shifted.clip_params, offset_x, offset_y);
    }
    dl_replay_outer_shadow(scratch, surface, &shifted);
}
