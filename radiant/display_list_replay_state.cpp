#include "render.hpp"

#include "../lib/log.h"

DisplayReplayDirtyClip dl_replay_push_dirty_clip(RdtVector* vec,
                                                 DirtyTracker* dirty_tracker,
                                                 float scale) {
    DisplayReplayDirtyClip clip = {};
    if (!vec || !dirty_tracker || !dirty_tracker->dirty_list || dirty_tracker->full_repaint) {
        return clip;
    }

    if (!dirty_tracker_bounds(dirty_tracker, &clip.bounds, scale)) return clip;
    clip.active = true;

    clip.path = rdt_path_new();
    float left = clip.bounds.left;
    float top = clip.bounds.top;
    float right = clip.bounds.right;
    float bottom = clip.bounds.bottom;
    rdt_path_move_to(clip.path, left, top);
    rdt_path_line_to(clip.path, right, top);
    rdt_path_line_to(clip.path, right, bottom);
    rdt_path_line_to(clip.path, left, bottom);
    rdt_path_close(clip.path);
    rdt_push_clip(vec, clip.path, nullptr);

    log_debug("[DL_REPLAY] dirty clip: (%.0f,%.0f)-(%.0f,%.0f)",
              left, top, right, bottom);
    return clip;
}

void dl_replay_pop_dirty_clip(RdtVector* vec, DisplayReplayDirtyClip* clip) {
    if (!vec || !clip || !clip->path) return;
    rdt_pop_clip(vec);
    rdt_path_free(clip->path);
    clip->path = nullptr;
    clip->active = false;
}

void dl_replay_intersect_dirty_clip(const DisplayReplayDirtyClip* clip, Bound* bounds) {
    if (!clip || !clip->active || !bounds) return;
    if (bounds->left < clip->bounds.left) bounds->left = clip->bounds.left;
    if (bounds->top < clip->bounds.top) bounds->top = clip->bounds.top;
    if (bounds->right > clip->bounds.right) bounds->right = clip->bounds.right;
    if (bounds->bottom > clip->bounds.bottom) bounds->bottom = clip->bounds.bottom;
}
