#include "display_list_replay_effects.hpp"

#include "display_list_surface_region.hpp"
#include "render_background.hpp"
#include "render_filter.hpp"
#include <string.h>

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
        int region[4] = {};
        uint32_t* saved = surface_region_save(surface, scratch,
                                              blur->rx, blur->ry, blur->rw, blur->rh,
                                              region);
        box_blur_region(scratch, surface, blur->rx, blur->ry, blur->rw, blur->rh, blur->blur_radius);
        if (saved) {
            ClipShape cs = clip_shape_from_params(blur->clip_type, blur->clip_params);
            surface_region_restore_masked(surface, saved, region, &cs, false);
        }
        return;
    }

    box_blur_region(scratch, surface, blur->rx, blur->ry, blur->rw, blur->rh, blur->blur_radius);
}

void dl_replay_box_blur_inset(ScratchArena* scratch,
                              ImageSurface* surface,
                              const DlBoxBlurInset* blur) {
    if (!blur) return;
    box_blur_region_inset(scratch, surface, blur->rx, blur->ry, blur->rw, blur->rh,
                          blur->pad, blur->blur_radius, blur->bg_color);
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
