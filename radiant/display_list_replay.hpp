#pragma once

#include "display_list.h"

// Replay the entire display list to the given vector context.
// surface is needed for direct-pixel operations (glyph, blit, opacity, etc.).
// dirty_tracker clips rendering to dirty regions for selective repaint.
void dl_replay(DisplayList* dl, RdtVector* vec,
               ImageSurface* surface, Bound* clip,
               ScratchArena* scratch, float scale,
               DirtyTracker* dirty_tracker);
