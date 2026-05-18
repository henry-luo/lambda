#pragma once

#include "display_list.h"

typedef struct DisplayReplayDirtyClip {
    Bound bounds;
    RdtPath* path;
    bool active;
} DisplayReplayDirtyClip;

DisplayReplayDirtyClip dl_replay_push_dirty_clip(RdtVector* vec,
                                                 DirtyTracker* dirty_tracker,
                                                 float scale);
void dl_replay_pop_dirty_clip(RdtVector* vec, DisplayReplayDirtyClip* clip);
void dl_replay_intersect_dirty_clip(const DisplayReplayDirtyClip* clip, Bound* bounds);
