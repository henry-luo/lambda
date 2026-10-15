#include "render.hpp"

#include "../lib/mem_factory.h"
#include "../lib/hashmap_typed.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/arena.h"
#include "../lib/arraylist.h"

#include <math.h>
#include <string.h>
#include "../lib/generation.h"

struct RetainedDisplayListFragment {
    uint32_t view_id;
    Bound bounds;
    Bound marker_bounds;
    // Each fragment owns its list's arena: re-capturing one fragment rewinds
    // only its own payloads, which a shared arena could not do in batch.
    Arena* arena;
    DisplayList list;
    bool initialized;
    uint32_t last_stored_epoch;
};

typedef struct RetainedDisplayListEntry {
    uint32_t view_id;
    RetainedDisplayListFragment* fragment;
} RetainedDisplayListEntry;

typedef TypedHashMap<RetainedDisplayListEntry,
    HashMapIntegralMemberKeyOps<RetainedDisplayListEntry,
        &RetainedDisplayListEntry::view_id>> RetainedDisplayListMap;

struct RetainedDisplayListCache {
    Pool* pool;
    RetainedDisplayListMap map;
    uint32_t epoch;
    RetainedDisplayListStats stats;

    bool init(Pool* owner_pool);
    void destroy();
    void begin_frame();
    RetainedDisplayListStats snapshot_stats() const;
};

RetainedDisplayListCache* retained_dl_cache_create(Pool* pool) {
    if (!pool) return nullptr;

    lam::Temp<RetainedDisplayListCache> cache =
        lam::temp_array_zero<RetainedDisplayListCache>(1, MEM_CAT_RENDER);
    if (!cache) return nullptr;
    if (!cache->init(pool)) return nullptr;
    return cache.release();
}

// Fragments live in the cache's pool (the owner's node); a cache built
// without a pool keeps them on the memtracked heap.
static RetainedDisplayListFragment* retained_dl_fragment_alloc(Pool* pool) {
    void* storage = pool ? pool_calloc(pool, sizeof(RetainedDisplayListFragment))
                         : mem_calloc(1, sizeof(RetainedDisplayListFragment), MEM_CAT_RENDER);
    return storage ? new (storage) RetainedDisplayListFragment() : nullptr;
}

static void retained_dl_fragment_free(Pool* pool, RetainedDisplayListFragment* fragment) {
    if (!fragment) return;
    if (fragment->initialized) dl_destroy(&fragment->list);
    // the list's scratch releases its arena ownership before the arena goes
    if (fragment->arena) mem_arena_destroy(fragment->arena);
    fragment->~RetainedDisplayListFragment();
    if (pool) pool_free(pool, fragment);
    else lam::Temp<RetainedDisplayListFragment> storage(fragment);
}

bool RetainedDisplayListCache::init(Pool* owner_pool) {
    pool = owner_pool;
    if (!map.init(128)) {
        destroy();
        return false;
    }
    epoch = 1;
    return true;
}

void RetainedDisplayListCache::destroy() {
    if (map.initialized()) {
        size_t iter = 0;
        RetainedDisplayListEntry* entry = nullptr;
        while (map.next(&iter, &entry)) {
            if (entry && entry->fragment) {
                retained_dl_fragment_free(pool, entry->fragment);
            }
        }
        map.destroy();
    }
}

void retained_dl_cache_destroy(RetainedDisplayListCache* cache) {
    if (!cache) return;
    lam::Temp<RetainedDisplayListCache> owned(cache);  // the owner hands the cache over
    cache->destroy();
}

void RetainedDisplayListCache::begin_frame() {
    epoch++;
    if (epoch == 0) epoch = 1;
    memset(&stats, 0, sizeof(stats));
}

void retained_dl_cache_begin_frame(RetainedDisplayListCache* cache) {
    if (cache) cache->begin_frame();
}

RetainedDisplayListStats RetainedDisplayListCache::snapshot_stats() const {
    return stats;
}

RetainedDisplayListStats retained_dl_cache_stats(const RetainedDisplayListCache* cache) {
    RetainedDisplayListStats stats = {};
    return cache ? cache->snapshot_stats() : stats;
}

void retained_dl_cache_note_reuse_miss(RetainedDisplayListCache* cache) {
    if (!cache) return;
    cache->stats.reuse_misses++;
}

void retained_dl_cache_note_reuse_rejected_resources(RetainedDisplayListCache* cache) {
    if (!cache) return;
    cache->stats.reuse_rejected_resources++;
}

void retained_dl_cache_note_reuse_rejected_dirty(RetainedDisplayListCache* cache) {
    if (!cache) return;
    cache->stats.reuse_rejected_dirty++;
}

void retained_dl_cache_note_reuse_hit(RetainedDisplayListCache* cache) {
    if (!cache) return;
    cache->stats.reuse_hits++;
}

static RetainedDisplayListFragment* retained_dl_fragment_get_or_create(
    RetainedDisplayListCache* cache, uint32_t view_id) {
    if (!cache || !cache->map.initialized() || view_id == 0) return nullptr;

    RetainedDisplayListEntry query = { view_id, nullptr };
    RetainedDisplayListEntry* found = cache->map.get(query);
    if (found) return found->fragment;

    RetainedDisplayListFragment* fragment = retained_dl_fragment_alloc(cache->pool);
    if (!fragment) return nullptr;

    fragment->view_id = view_id;
    // Factory-created fragment arenas outlive frame scratch resets; they are
    // destroyed with the fragment.
    // the fragment arena sits under the context that owns the cache's pool
    fragment->arena = mem_arena_create(
        cache->pool ? mem_node_owner((MemNode*)pool_get_mem_node(cache->pool)) : nullptr,
        MEM_ROLE_RENDER, "retained_dl.fragment");
    if (!fragment->arena) {
        retained_dl_fragment_free(cache->pool, fragment);
        return nullptr;
    }
    dl_init(&fragment->list, fragment->arena);
    fragment->initialized = true;

    RetainedDisplayListEntry entry = { view_id, fragment };
    cache->map.set(entry);
    if (cache->map.oom()) {
        retained_dl_fragment_free(cache->pool, fragment);
        return nullptr;
    }
    return fragment;
}

static bool retained_dl_range_retainable(const DisplayList* source,
                                         int start, int end) {
    if (!source || start < 0 || end < start || end >= source->item_count()) return false;
    for (int i = start; i <= end; i++) {
        if (!dl_item_is_retainable_for_fragment(&source->data()[i])) {
            return false;
        }
    }
    return true;
}

static void retained_dl_cache_store_marker(RetainedDisplayListCache* cache,
                                           const DisplayList* source,
                                           int begin_index) {
    if (!cache || !source || begin_index < 0 || begin_index >= source->item_count()) return;

    const DisplayItem* begin = &source->data()[begin_index];
    if (begin->op != DL_BEGIN_ELEMENT) return;
    int end_index = begin->element_marker.matching_index;
    if (end_index <= begin_index || end_index >= source->item_count()) return;

    uint32_t view_id = begin->element_marker.view_id;
    if (view_id == 0) return;
    cache->stats.capture_candidates++;

    RetainedDisplayListFragment* fragment =
        retained_dl_fragment_get_or_create(cache, view_id);
    if (!fragment || !fragment->initialized) return;

    // Fragment lists keep their arena across captures; clear rewinds it and keeps its chunks.
    dl_clear(&fragment->list);
    if (!retained_dl_range_retainable(source, begin_index, end_index)) {
        cache->stats.skipped_non_retainable++;
        log_debug("[RETAINED_DL] skipped non-retainable view %u", view_id);
        return;
    }
    if (!dl_copy_range(&fragment->list, source, begin_index, end_index)) {
        cache->stats.copy_failed++;
        dl_clear(&fragment->list);
        return;
    }

    fragment->bounds = dl_item_bounds(begin);
    fragment->marker_bounds = {
        begin->element_marker.marker_x,
        begin->element_marker.marker_y,
        begin->element_marker.marker_x + begin->element_marker.marker_w,
        begin->element_marker.marker_y + begin->element_marker.marker_h
    };
    fragment->last_stored_epoch = cache->epoch;
    cache->stats.captured++;
}

void retained_dl_cache_capture(RetainedDisplayListCache* cache, const DisplayList* source) {
    if (!cache || !source) return;

    for (int i = 0; i < source->item_count(); i++) {
        const DisplayItem* item = &source->data()[i];
        if (item->op != DL_BEGIN_ELEMENT || item->element_marker.matching_index <= i) {
            continue;
        }
        retained_dl_cache_store_marker(cache, source, i);
    }

    // replaced view IDs cannot be reused; release their arenas each frame
    // instead of retaining every reactive redraw until document teardown.
    ArrayList* stale = nullptr;
    size_t iter = 0;
    RetainedDisplayListEntry* entry = nullptr;
    while (cache->map.next(&iter, &entry)) {
        if (!entry->fragment || entry->fragment->last_stored_epoch == cache->epoch) continue;
        if (!stale) stale = arraylist_new(16);
        if (!stale || !arraylist_append(stale, entry->fragment)) {
            log_error("retained_dl_prune: could not collect expired fragments");
            break;
        }
    }
    if (!stale) return;
    // hash-map erasure rearranges buckets, so collect before mutating the map.
    for (int i = 0; i < stale->length; i++) {
        RetainedDisplayListFragment* fragment = (RetainedDisplayListFragment*)stale->data[i];
        cache->map.erase({fragment->view_id, nullptr});
        retained_dl_fragment_free(cache->pool, fragment);
    }
    arraylist_free(stale);
}

const RetainedDisplayListFragment* retained_dl_cache_get(RetainedDisplayListCache* cache,
                                                         uint32_t view_id) {
    if (!cache || !cache->map.initialized() || view_id == 0) return nullptr;
    RetainedDisplayListEntry query = { view_id, nullptr };
    RetainedDisplayListEntry* found = cache->map.get(query);
    if (!found || !found->fragment || found->fragment->list.item_count() <= 0) return nullptr;
    return found->fragment;
}

Bound retained_dl_fragment_bounds(const RetainedDisplayListFragment* fragment) {
    if (!fragment) return {0, 0, 0, 0};
    return fragment->bounds;
}

Bound retained_dl_fragment_marker_bounds(const RetainedDisplayListFragment* fragment) {
    if (!fragment) return {0, 0, 0, 0};
    return fragment->marker_bounds;
}

int retained_dl_fragment_item_count(const RetainedDisplayListFragment* fragment) {
    if (!fragment) return 0;
    return fragment->list.item_count();
}

static bool retained_dl_bounds_match(Bound cached, Bound current) {
    float tolerance = 0.5f;
    return fabsf(cached.left - current.left) <= tolerance &&
           fabsf(cached.top - current.top) <= tolerance &&
           fabsf(cached.right - current.right) <= tolerance &&
           fabsf(cached.bottom - current.bottom) <= tolerance;
}

static bool retained_dl_dirty_rect_intersects_bound(const DirtyRect* dirty,
                                                    Bound visual_bound,
                                                    float scale) {
    if (!dirty) return false;
    float s = scale > 0 ? scale : 1.0f;
    Bound dirty_bound = {
        dirty->x * s,
        dirty->y * s,
        (dirty->x + dirty->width) * s,
        (dirty->y + dirty->height) * s
    };
    return view_geometry_bounds_intersect(visual_bound, dirty_bound);
}

// Resolves the handle through the slot table first, so a destroyed surface is
// detected without reading its memory.
static bool retained_dl_surface_generation_current(lam::Handle<ImageSurface> resource,
                                                   uint64_t generation) {
    ImageSurface* surface = image_surface_lookup(resource);
    return surface && generation_current(generation, surface->generation);
}

static bool retained_dl_generation_current(const void* resource, uint64_t generation, uint64_t current_generation) {
    return !resource || generation_current(generation, current_generation);
}

bool retained_dl_fragment_resources_valid(const RetainedDisplayListFragment* fragment,
                                          uint64_t current_video_generation,
                                          uint64_t current_glyph_generation) {
    if (!fragment) return false;
    // owners are read for their generation
    ImageSurfaceReadScope read_scope;
    const DisplayList* list = &fragment->list;
    for (int i = 0; i < list->item_count(); i++) {
        const DisplayItem* item = &list->data()[i];
        switch (item->op) {
            case DL_DRAW_IMAGE: {
                if (!item->draw_image.resource.is_null() && !retained_dl_surface_generation_current(
                        item->draw_image.resource, item->draw_image.resource_generation)) return false;
                break;
            }
            case DL_DRAW_GLYPH:
                if (!retained_dl_generation_current(item->draw_glyph.bitmap.buffer,
                        item->draw_glyph.resource_generation, current_glyph_generation)) return false;
                break;
            case DL_BLIT_SURFACE_SCALED: {
                if (!retained_dl_surface_generation_current(item->blit_surface_scaled.src_resource,
                        item->blit_surface_scaled.src_generation)) return false;
                break;
            }
            case DL_VIDEO_PLACEHOLDER:
                if (!retained_dl_generation_current(item->video_placeholder.video,
                        item->video_placeholder.video_generation, current_video_generation)) return false;
                break;
            case DL_WEBVIEW_LAYER_PLACEHOLDER: {
                if (!retained_dl_surface_generation_current(item->webview_layer_placeholder.resource,
                        item->webview_layer_placeholder.surface_generation)) return false;
                break;
            }
            default:
                break;
        }
    }
    return true;
}

bool retained_dl_append_fragment_for_dirty(DisplayList* dst,
                                           const RetainedDisplayListFragment* fragment,
                                           Bound current_marker_bounds,
                                           DirtyTracker* tracker,
                                           float scale,
                                           RetainedDisplayListContainsViewFn contains_view,
                                           void* contains_userdata) {
    if (!dst || !fragment || !tracker || tracker->full_repaint || !tracker->dirty_list) {
        return false;
    }
    if (!retained_dl_bounds_match(fragment->marker_bounds, current_marker_bounds)) {
        return false;
    }

    Bound visual_bound = fragment->bounds;
    for (DirtyRect* dirty = tracker->dirty_list; dirty; dirty = dirty->next) {
        if (!retained_dl_dirty_rect_intersects_bound(dirty, visual_bound, scale)) {
            continue;
        }
        if (dirty->source_view_id == 0 || !contains_view ||
            contains_view(contains_userdata, dirty->source_view_id)) {
            return false;
        }
    }

    return retained_dl_append_fragment(dst, fragment);
}

bool retained_dl_append_fragment(DisplayList* dst,
                                 const RetainedDisplayListFragment* fragment) {
    if (!dst || !fragment || fragment->list.item_count() <= 0) return false;
    return dl_copy_range(dst, &fragment->list, 0, fragment->list.item_count() - 1);
}
