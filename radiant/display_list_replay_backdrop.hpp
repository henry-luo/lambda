#pragma once

#include "display_list.h"

#define DL_REPLAY_MAX_BACKDROP_DEPTH 16

typedef struct DisplayReplayBackdropStack {
    uint32_t* stack[DL_REPLAY_MAX_BACKDROP_DEPTH];
    int region[DL_REPLAY_MAX_BACKDROP_DEPTH][4];
    int sp;
} DisplayReplayBackdropStack;

void dl_replay_backdrop_init(DisplayReplayBackdropStack* stack);
int dl_replay_backdrop_depth(const DisplayReplayBackdropStack* stack);
void dl_replay_backdrop_save(DisplayReplayBackdropStack* stack,
                             ImageSurface* surface,
                             ScratchArena* scratch,
                             const DlSaveBackdrop* backdrop);
void dl_replay_backdrop_composite_opacity(DisplayReplayBackdropStack* stack,
                                          ImageSurface* surface,
                                          const DlCompositeOpacity* opacity);
void dl_replay_backdrop_apply_blend_mode(DisplayReplayBackdropStack* stack,
                                         ImageSurface* surface,
                                         const DlApplyBlendMode* blend);
