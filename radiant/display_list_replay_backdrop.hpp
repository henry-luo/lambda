#pragma once

#include "display_list.h"
#include "display_list_surface_region.hpp"

#define DL_REPLAY_MAX_BACKDROP_DEPTH 16

typedef struct DisplayReplayBackdropStack {
    uint32_t* stack[DL_REPLAY_MAX_BACKDROP_DEPTH];
    IRect region[DL_REPLAY_MAX_BACKDROP_DEPTH];
    int sp;
} DisplayReplayBackdropStack;

void dl_replay_backdrop_init(DisplayReplayBackdropStack* stack);
int dl_replay_backdrop_depth(const DisplayReplayBackdropStack* stack);
void dl_replay_backdrop_save(DisplayReplayBackdropStack* stack,
                             ImageSurface* surface,
                             ScratchArena* scratch,
                             const DlSaveBackdrop* backdrop);
void dl_replay_backdrop_save_at_offset(DisplayReplayBackdropStack* stack,
                                       ImageSurface* surface,
                                       ScratchArena* scratch,
                                       const DlSaveBackdrop* backdrop,
                                       float origin_x, float origin_y);
void dl_replay_backdrop_push_empty(DisplayReplayBackdropStack* stack);
void dl_replay_backdrop_discard(DisplayReplayBackdropStack* stack,
                                ScratchArena* scratch);
void dl_replay_backdrop_composite_opacity(DisplayReplayBackdropStack* stack,
                                          ImageSurface* surface,
                                          ScratchArena* scratch,
                                          const DlCompositeOpacity* opacity);
void dl_replay_backdrop_apply_blend_mode(DisplayReplayBackdropStack* stack,
                                         ImageSurface* surface,
                                         ScratchArena* scratch,
                                         const DlApplyBlendMode* blend);
bool dl_replay_backdrop_skip_item(DisplayReplayBackdropStack* stack,
                                  ScratchArena* scratch,
                                  const DisplayItem* item);
