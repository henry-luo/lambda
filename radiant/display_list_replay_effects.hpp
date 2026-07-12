#pragma once

#include "display_list.h"
#include "display_list_replay_state.hpp"
#include "render_backend_caps.hpp"

void dl_replay_apply_filter(ScratchArena* scratch,
                            ImageSurface* surface,
                            const RenderBackendCaps* caps,
                            const DisplayReplayDirtyClip* dirty_clip,
                            const DlApplyFilter* filter);
void dl_replay_apply_filter_at_offset(ScratchArena* scratch,
                                      ImageSurface* surface,
                                      const RenderBackendCaps* caps,
                                      const DlApplyFilter* filter,
                                      float offset_x, float offset_y);
void dl_replay_box_blur_region(ScratchArena* scratch,
                               ImageSurface* surface,
                               const DlBoxBlurRegion* blur);
void dl_replay_box_blur_region_at_offset(ScratchArena* scratch,
                                         ImageSurface* surface,
                                         const DlBoxBlurRegion* blur,
                                         float offset_x, float offset_y);
void dl_replay_box_blur_inset(ScratchArena* scratch,
                              ImageSurface* surface,
                              const DlBoxBlurInset* blur);
void dl_replay_box_blur_inset_at_offset(ScratchArena* scratch,
                                        ImageSurface* surface,
                                        const DlBoxBlurInset* blur,
                                        float offset_x, float offset_y);
void dl_replay_outer_shadow(ScratchArena* scratch,
                            ImageSurface* surface,
                            const DlOuterShadow* shadow);
void dl_replay_outer_shadow_at_offset(ScratchArena* scratch,
                                      ImageSurface* surface,
                                      const DlOuterShadow* shadow,
                                      float offset_x, float offset_y);
