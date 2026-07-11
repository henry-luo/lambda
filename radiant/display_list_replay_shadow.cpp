#include "display_list_replay_shadow.hpp"

#include "display_list_surface_region.hpp"
#include <string.h>

void dl_replay_shadow_clip_init(DisplayReplayShadowClip* clip) {
    if (!clip) return;
    memset(clip, 0, sizeof(DisplayReplayShadowClip));
}

void dl_replay_shadow_clip_save(DisplayReplayShadowClip* clip,
                                ImageSurface* surface,
                                ScratchArena* scratch,
                                const DlShadowClipSave* save) {
    if (!clip) return;
    clip->saved = nullptr;
    if (!surface || !surface->pixels || !scratch || !save) return;

    clip->saved = surface_region_save(surface, scratch,
                                      save->rx, save->ry, save->rw, save->rh,
                                      clip->region);
}

void dl_replay_shadow_clip_restore(DisplayReplayShadowClip* clip,
                                   ImageSurface* surface,
                                   const DlShadowClipRestore* restore) {
    if (!clip) return;
    if (clip->saved && surface && surface->pixels && restore && restore->exclude_type) {
        ClipShape ex = clip_shape_from_params(restore->exclude_type, restore->exclude_params);
        surface_region_restore_masked(surface, clip->saved, clip->region,
                                      &ex, restore->restore_inside);
    }
    clip->saved = nullptr;
}
