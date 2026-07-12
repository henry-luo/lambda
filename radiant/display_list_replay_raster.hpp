#pragma once

#include "display_list.h"
#include "display_list_replay_state.hpp"
#include "../lib/scratch_arena.h"

void dl_replay_fill_surface_rect(ImageSurface* surface,
                                 const DisplayReplayDirtyClip* dirty_clip,
                                 const DlFillSurfaceRect* fill);
void dl_replay_fill_surface_rect_at_offset(ImageSurface* surface, ScratchArena* scratch,
                                           const DlFillSurfaceRect* fill,
                                           float offset_x, float offset_y);
void dl_replay_blit_surface_scaled(ImageSurface* surface,
                                   const DisplayReplayDirtyClip* dirty_clip,
                                   const DlBlitSurfaceScaled* blit);
void dl_replay_blit_surface_scaled_at_offset(ImageSurface* surface, ScratchArena* scratch,
                                             const DlBlitSurfaceScaled* blit,
                                             float offset_x, float offset_y);
void dl_replay_webview_layer_placeholder(ImageSurface* surface,
                                         const DlWebviewLayerPlaceholder* placeholder);
void dl_replay_webview_layer_placeholder_at_offset(ImageSurface* surface,
                                                   const DlWebviewLayerPlaceholder* placeholder,
                                                   float offset_x, float offset_y);
