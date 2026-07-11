#pragma once

#include "display_list.h"
#include "../lib/math_utils.h"

static const float DL_UNBOUNDED_EXTENT = 99999.0f;
static const Bound DL_UNBOUNDED_CLIP = {
    0.0f, 0.0f, DL_UNBOUNDED_EXTENT, DL_UNBOUNDED_EXTENT
};

static inline Bound dl_unbounded_clip() {
    return DL_UNBOUNDED_CLIP;
}

static inline void dl_set_bounds_xyxy(DisplayItem* item,
                                      float left, float top,
                                      float right, float bottom) {
    if (!item) return;
    if (right <= left || bottom <= top) {
        item->bounds[0] = left;
        item->bounds[1] = top;
        item->bounds[2] = 0.0f;
        item->bounds[3] = 0.0f;
        return;
    }
    item->bounds[0] = left;
    item->bounds[1] = top;
    item->bounds[2] = right - left;
    item->bounds[3] = bottom - top;
}

static inline void dl_set_clipped_rect_bounds(DisplayItem* item,
                                              float x, float y,
                                              float w, float h,
                                              const Bound* clip) {
    float left = x;
    float top = y;
    float right = x + w;
    float bottom = y + h;
    if (clip) {
        left = LMB_MAX(left, clip->left);
        top = LMB_MAX(top, clip->top);
        right = LMB_MIN(right, clip->right);
        bottom = LMB_MIN(bottom, clip->bottom);
    }
    dl_set_bounds_xyxy(item, left, top, right, bottom);
}

Bound dl_item_bounds(const DisplayItem* item);
bool dl_item_intersects_rect(const DisplayItem* item,
                             float x, float y, float w, float h);
