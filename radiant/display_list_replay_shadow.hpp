#pragma once

#include "display_list.h"

typedef struct DisplayReplayShadowClip {
    uint32_t* saved;
    int region[4];
} DisplayReplayShadowClip;

void dl_replay_shadow_clip_init(DisplayReplayShadowClip* clip);
void dl_replay_shadow_clip_save(DisplayReplayShadowClip* clip,
                                ImageSurface* surface,
                                ScratchArena* scratch,
                                const DlShadowClipSave* save);
void dl_replay_shadow_clip_restore(DisplayReplayShadowClip* clip,
                                   ImageSurface* surface,
                                   const DlShadowClipRestore* restore);
