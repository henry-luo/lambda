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

bool dl_item_intersects_rect(const DisplayItem* item,
                             float x, float y, float w, float h) {
    if (!item) return false;

    float ix = item->bounds[0];
    float iy = item->bounds[1];
    float iw = item->bounds[2];
    float ih = item->bounds[3];
    if (iw <= 0 && ih <= 0) return true;

    return !(ix >= x + w || ix + iw <= x ||
             iy >= y + h || iy + ih <= y);
}
