#pragma once

#include "display_list.h"
#include "display_list_surface_region.hpp"

typedef struct DisplayReplayShadowClip {
    uint32_t* saved;
    IRect region;
} DisplayReplayShadowClip;

void dl_replay_shadow_clip_init(DisplayReplayShadowClip* clip);
void dl_replay_shadow_clip_save(DisplayReplayShadowClip* clip,
                                ImageSurface* surface,
                                ScratchArena* scratch,
                                const DlShadowClipSave* save);
void dl_replay_shadow_clip_save_at_offset(DisplayReplayShadowClip* clip,
                                          ImageSurface* surface,
                                          ScratchArena* scratch,
                                          const DlShadowClipSave* save,
                                          float origin_x, float origin_y);
void dl_replay_shadow_clip_restore(DisplayReplayShadowClip* clip,
                                   ImageSurface* surface,
                                   const DlShadowClipRestore* restore);
void dl_replay_shadow_clip_restore_at_offset(DisplayReplayShadowClip* clip,
                                             ImageSurface* surface,
                                             const DlShadowClipRestore* restore,
                                             float origin_x, float origin_y);
void dl_replay_shadow_clip_discard(DisplayReplayShadowClip* clip);
