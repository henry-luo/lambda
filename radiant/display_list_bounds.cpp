// ==========================================================================
// DisplayList bounds helpers.
// ==========================================================================

#include "display_list.h"

Bound dl_item_bounds(const DisplayItem* item) {
    if (!item) return {0, 0, 0, 0};
    float x = item->bounds[0];
    float y = item->bounds[1];
    return {x, y, x + item->bounds[2], y + item->bounds[3]};
}

static bool dl_item_preserves_replay_state(const DisplayItem* item) {
    if (!item) return false;
    switch (item->op) {
        case DL_PUSH_CLIP:
        case DL_POP_CLIP:
        case DL_SAVE_CLIP_DEPTH:
        case DL_RESTORE_CLIP_DEPTH:
        case DL_SAVE_BACKDROP:
        case DL_APPLY_BLEND_MODE:
        case DL_COMPOSITE_OPACITY:
        case DL_SHADOW_CLIP_SAVE:
        case DL_SHADOW_CLIP_RESTORE:
        case DL_BEGIN_ELEMENT:
        case DL_END_ELEMENT:
            return true;
        default:
            return false;
    }
}

bool dl_item_intersects_rect(const DisplayItem* item,
                             float x, float y, float w, float h) {
    if (!item) return false;

    float ix = item->bounds[0];
    float iy = item->bounds[1];
    float iw = item->bounds[2];
    float ih = item->bounds[3];
    if (iw <= 0 || ih <= 0) return dl_item_preserves_replay_state(item);

    return !(ix >= x + w || ix + iw <= x ||
             iy >= y + h || iy + ih <= y);
}
