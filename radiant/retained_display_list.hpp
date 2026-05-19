#pragma once

#include "display_list.h"
#include "../lib/mempool.h"

typedef struct RetainedDisplayListCache RetainedDisplayListCache;
typedef struct RetainedDisplayListFragment RetainedDisplayListFragment;

RetainedDisplayListCache* retained_dl_cache_create(Pool* pool);
void retained_dl_cache_destroy(RetainedDisplayListCache* cache);

void retained_dl_cache_begin_frame(RetainedDisplayListCache* cache);
void retained_dl_cache_capture(RetainedDisplayListCache* cache, const DisplayList* source);

const RetainedDisplayListFragment* retained_dl_cache_get(RetainedDisplayListCache* cache,
                                                         uint32_t view_id);
Bound retained_dl_fragment_bounds(const RetainedDisplayListFragment* fragment);
int retained_dl_fragment_item_count(const RetainedDisplayListFragment* fragment);
bool retained_dl_fragment_resources_valid(const RetainedDisplayListFragment* fragment,
                                          uint64_t current_video_generation);
bool retained_dl_append_fragment(DisplayList* dst,
                                 const RetainedDisplayListFragment* fragment);
