#include "display_list_replay_effects.hpp"

#include "render_background.hpp"
#include "render_filter.hpp"
#include <string.h>

static int dl_replay_effect_max_i(int a, int b) {
    return a > b ? a : b;
}

static int dl_replay_effect_min_i(int a, int b) {
    return a < b ? a : b;
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
        int sw = surface->width;
        int sh = surface->height;
        int x0 = dl_replay_effect_max_i(0, blur->rx);
        int y0 = dl_replay_effect_max_i(0, blur->ry);
        int x1 = dl_replay_effect_min_i(sw, blur->rx + blur->rw);
        int y1 = dl_replay_effect_min_i(sh, blur->ry + blur->rh);
        int w = x1 - x0;
        int h = y1 - y0;
        uint32_t* saved = nullptr;
        if (w > 0 && h > 0) {
            saved = (uint32_t*)scratch_alloc(scratch, (size_t)w * h * sizeof(uint32_t));
            uint32_t* px = (uint32_t*)surface->pixels;
            int pitch = surface->pitch / 4;
            for (int row = 0; row < h; row++) {
                memcpy(saved + row * w,
                       px + (y0 + row) * pitch + x0,
                       w * sizeof(uint32_t));
            }
        }
        box_blur_region(scratch, surface, blur->rx, blur->ry, blur->rw, blur->rh, blur->blur_radius);
        if (saved) {
            ClipShape cs = clip_shape_from_params(blur->clip_type, blur->clip_params);
            uint32_t* px = (uint32_t*)surface->pixels;
            int pitch = surface->pitch / 4;
            for (int row = 0; row < h; row++) {
                for (int col = 0; col < w; col++) {
                    float fx = (float)(x0 + col) + 0.5f;
                    float fy = (float)(y0 + row) + 0.5f;
                    if (!clip_point_in_shape(&cs, fx, fy)) {
                        px[(y0 + row) * pitch + (x0 + col)] = saved[row * w + col];
                    }
                }
            }
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
