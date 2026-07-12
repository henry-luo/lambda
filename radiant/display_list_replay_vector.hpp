#pragma once

#include "display_list.h"

typedef enum DisplayReplayVectorResult {
    DL_REPLAY_VECTOR_NOT_HANDLED = 0,
    DL_REPLAY_VECTOR_HANDLED,
    DL_REPLAY_VECTOR_DREW,
} DisplayReplayVectorResult;

DisplayReplayVectorResult dl_replay_vector_item(RdtVector* vec,
                                                DisplayItem* item,
                                                bool duplicate_picture);
bool dl_replay_vector_clip_item(RdtVector* vec, DisplayItem* item);
