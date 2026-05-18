#include "display_list_replay_state.hpp"

#include "../lib/log.h"

DisplayReplayDirtyClip dl_replay_push_dirty_clip(RdtVector* vec,
                                                 DirtyTracker* dirty_tracker,
                                                 float scale) {
    DisplayReplayDirtyClip clip = {};
    if (!vec || !dirty_tracker || !dirty_tracker->dirty_list || dirty_tracker->full_repaint) {
        return clip;
    }

    DirtyRect* dirty = dirty_tracker->dirty_list;
    float left = dirty->x * scale;
    float top = dirty->y * scale;
    float right = (dirty->x + dirty->width) * scale;
    float bottom = (dirty->y + dirty->height) * scale;
    dirty = dirty->next;
    while (dirty) {
        float rect_left = dirty->x * scale;
        float rect_top = dirty->y * scale;
        float rect_right = (dirty->x + dirty->width) * scale;
        float rect_bottom = (dirty->y + dirty->height) * scale;
        if (rect_left < left) left = rect_left;
        if (rect_top < top) top = rect_top;
        if (rect_right > right) right = rect_right;
        if (rect_bottom > bottom) bottom = rect_bottom;
        dirty = dirty->next;
    }

    clip.bounds = {left, top, right, bottom};
    clip.active = true;

    clip.path = rdt_path_new();
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
