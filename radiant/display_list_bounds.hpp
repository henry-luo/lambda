#pragma once

#include "display_list.h"

Bound dl_item_bounds(const DisplayItem* item);
bool dl_item_intersects_rect(const DisplayItem* item,
                             float x, float y, float w, float h);
