#pragma once

#include "display_list.h"
#include "../lib/mempool.h"

typedef struct RetainedDisplayListCache RetainedDisplayListCache;
typedef struct RetainedDisplayListFragment RetainedDisplayListFragment;

typedef struct RetainedDisplayListStats {
    int capture_candidates;
    int captured;
    int skipped_non_retainable;
    int copy_failed;
    int reuse_hits;
    int reuse_misses;
    int reuse_rejected_resources;
    int reuse_rejected_dirty;
} RetainedDisplayListStats;

RetainedDisplayListCache* retained_dl_cache_create(Pool* pool);
void retained_dl_cache_destroy(RetainedDisplayListCache* cache);

void retained_dl_cache_begin_frame(RetainedDisplayListCache* cache);
void retained_dl_cache_capture(RetainedDisplayListCache* cache, const DisplayList* source);
RetainedDisplayListStats retained_dl_cache_stats(const RetainedDisplayListCache* cache);
void retained_dl_cache_note_reuse_miss(RetainedDisplayListCache* cache);
void retained_dl_cache_note_reuse_rejected_resources(RetainedDisplayListCache* cache);
void retained_dl_cache_note_reuse_rejected_dirty(RetainedDisplayListCache* cache);
void retained_dl_cache_note_reuse_hit(RetainedDisplayListCache* cache);

const RetainedDisplayListFragment* retained_dl_cache_get(RetainedDisplayListCache* cache,
                                                         uint32_t view_id);
Bound retained_dl_fragment_bounds(const RetainedDisplayListFragment* fragment);
Bound retained_dl_fragment_marker_bounds(const RetainedDisplayListFragment* fragment);
int retained_dl_fragment_item_count(const RetainedDisplayListFragment* fragment);
bool retained_dl_fragment_resources_valid(const RetainedDisplayListFragment* fragment,
                                          uint64_t current_video_generation,
                                          uint64_t current_glyph_generation);
typedef bool (*RetainedDisplayListContainsViewFn)(void* userdata, uint32_t source_view_id);
bool retained_dl_append_fragment_for_dirty(DisplayList* dst,
                                           const RetainedDisplayListFragment* fragment,
                                           Bound current_marker_bounds,
                                           DirtyTracker* tracker,
                                           float scale,
                                           RetainedDisplayListContainsViewFn contains_view,
                                           void* contains_userdata);
bool retained_dl_append_fragment(DisplayList* dst,
                                 const RetainedDisplayListFragment* fragment);
