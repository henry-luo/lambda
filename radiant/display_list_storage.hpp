#pragma once

#include "display_list.h"

DisplayItem* dl_alloc_item(DisplayList* dl);
RdtGradientStop* dl_copy_stops(DisplayList* dl, const RdtGradientStop* stops, int count);
float* dl_copy_dashes(DisplayList* dl, const float* dashes, int count);
void dl_store_clip_shapes(DisplayList* dl, DlClipShapeStack* dst,
                          ClipShape** clip_shapes, int clip_depth);
int dl_restore_clip_shapes(const DlClipShapeStack* src, ClipShape* shapes,
                           ClipShape** shape_ptrs);
