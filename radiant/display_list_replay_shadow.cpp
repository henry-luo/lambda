#include "render.hpp"
#include <string.h>

void dl_replay_shadow_clip_init(DisplayReplayShadowClip* clip) {
    if (!clip) return;
    memset(clip, 0, sizeof(DisplayReplayShadowClip));
}

void dl_replay_shadow_clip_save(DisplayReplayShadowClip* clip,
                                ImageSurface* surface,
                                ScratchArena* scratch,
                                const DlShadowClipSave* save) {
    dl_replay_shadow_clip_save_at_offset(clip, surface, scratch, save, 0.0f, 0.0f);
}

void dl_replay_shadow_clip_save_at_offset(DisplayReplayShadowClip* clip,
                                          ImageSurface* surface,
                                          ScratchArena* scratch,
                                          const DlShadowClipSave* save,
                                          float origin_x, float origin_y) {
    if (!clip) return;
    clip->saved = nullptr;
    if (!surface || !surface->pixels || !scratch || !save) return;

    clip->saved = surface_region_save(surface, scratch,
                                      save->rx - (int)origin_x,
                                      save->ry - (int)origin_y,
                                      save->rw, save->rh,
                                      &clip->region);
}

void dl_replay_shadow_clip_restore(DisplayReplayShadowClip* clip,
                                   ImageSurface* surface,
                                   const DlShadowClipRestore* restore) {
    dl_replay_shadow_clip_restore_at_offset(clip, surface, restore, 0.0f, 0.0f);
}

void dl_replay_shadow_clip_restore_at_offset(DisplayReplayShadowClip* clip,
                                             ImageSurface* surface,
                                             const DlShadowClipRestore* restore,
                                             float origin_x, float origin_y) {
    if (!clip) return;
    if (clip->saved && surface && surface->pixels && restore && restore->exclude_type) {
        float params[8];
        dl_replay_offset_clip_params(restore->exclude_type, restore->exclude_params,
                                     params, origin_x, origin_y);
        ClipShape ex = clip_shape_from_params(restore->exclude_type, params);
        surface_region_restore_masked(surface, clip->saved, &clip->region,
                                      &ex, restore->restore_inside);
    }
    clip->saved = nullptr;
}

void dl_replay_shadow_clip_discard(DisplayReplayShadowClip* clip) {
    if (!clip) return;
    clip->saved = nullptr;
}
