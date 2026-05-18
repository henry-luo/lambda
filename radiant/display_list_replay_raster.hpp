#pragma once

#include "display_list.h"
#include "display_list_replay_state.hpp"

void dl_replay_fill_surface_rect(ImageSurface* surface,
                                 const DisplayReplayDirtyClip* dirty_clip,
                                 const DlFillSurfaceRect* fill);
void dl_replay_blit_surface_scaled(ImageSurface* surface,
                                   const DisplayReplayDirtyClip* dirty_clip,
                                   const DlBlitSurfaceScaled* blit);
void dl_replay_webview_layer_placeholder(ImageSurface* surface,
                                         const DlWebviewLayerPlaceholder* placeholder);
