#include "render_metrics.hpp"
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

struct RetainedPaintChunk;
struct RetainedStorageBudget {
    RefCount refs;
    size_t used, limit;
};
static void retained_budget_release(RetainedStorageBudget* budget) {
    if (budget && ref_count_release(&budget->refs) == REF_COUNT_LAST) mem_free(budget);
}
struct RetainedPaintSpan {
    RetainedPaintChunk* child; // owns one child reference, or a range of own commands
    int start, count;
};
struct RetainedPaintChunk : DisplayPayloadOwner {
    Arena* arena = nullptr;
    DisplayList own;
    lam::ArrayList<RetainedPaintSpan> spans{MEM_CAT_RENDER, 0};
    int logical_count = 0;
    uint32_t parent_count = 0;
    RetainedStorageBudget* budget = nullptr;
    size_t charged_bytes = 0;
};
struct RetainedDisplayListFragment {
    uint32_t view_id;
    Bound bounds;
    Bound marker_bounds;
    RetainedPaintChunk* chunk;
    uint32_t last_stored_epoch;
    uint64_t last_used;
};

static void retained_chunk_destroy(DisplayPayloadOwner* owner) {
    RetainedPaintChunk* chunk = (RetainedPaintChunk*)owner;
    for (size_t i = 0; i < chunk->spans.size(); i++) {
        RetainedPaintChunk* child = chunk->spans[i].child;
        if (child) __atomic_fetch_sub(&child->parent_count, 1u, __ATOMIC_RELAXED);
        dl_payload_owner_release(child);
    }
    dl_destroy(&chunk->own);
    if (chunk->arena) mem_arena_destroy(chunk->arena);
    if (chunk->budget) {
        __atomic_fetch_sub(&chunk->budget->used, chunk->charged_bytes, __ATOMIC_RELAXED);
        retained_budget_release(chunk->budget);
    }
    chunk->~RetainedPaintChunk(); mem_free(chunk);
}


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
    uint64_t clock;
    RetainedStorageBudget* budget;
    RetainedDisplayListStats stats;

    bool init(Pool* owner_pool);
    void destroy();
    void begin_frame();
    RetainedDisplayListStats snapshot_stats() const;
};

static bool retained_dl_trim(RetainedDisplayListCache* cache, size_t needed);

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
    dl_payload_owner_release(fragment->chunk);
    fragment->~RetainedDisplayListFragment();
    if (pool) pool_free(pool, fragment);
    else lam::Temp<RetainedDisplayListFragment> storage(fragment);
}

bool RetainedDisplayListCache::init(Pool* owner_pool) {
    pool = owner_pool;
    budget = (RetainedStorageBudget*)mem_calloc(1, sizeof(RetainedStorageBudget), MEM_CAT_RENDER);
    if (!budget) return false;
    ref_count_init(&budget->refs); budget->limit = (size_t)32 << 20;
    if (!map.init(128)) {
        destroy();
        retained_budget_release(budget); budget = nullptr;
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
    retained_budget_release(cache->budget);
}

void RetainedDisplayListCache::begin_frame() {
    // reclaim cache ownership between readers; active borrowed frames keep their own chunk leases.
    if (memtrack_get_pressure_level() >= MEM_PRESSURE_HIGH) {
        destroy();
        map.init(128);
    }
    epoch++;
    if (epoch == 0) epoch = 1;
    memset(&stats, 0, sizeof(stats));
    retained_dl_trim(this, 0);
}

void retained_dl_cache_begin_frame(RetainedDisplayListCache* cache) {
    if (cache) cache->begin_frame();
}

RetainedDisplayListStats RetainedDisplayListCache::snapshot_stats() const {
    RetainedDisplayListStats snapshot = stats;
    snapshot.charged_storage_bytes = budget ? __atomic_load_n(&budget->used, __ATOMIC_RELAXED) : 0;
    return snapshot;
}

void retained_dl_cache_set_storage_budget(RetainedDisplayListCache* cache, size_t bytes) {
    if (cache && cache->budget) cache->budget->limit = bytes;
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
    RetainedDisplayListEntry entry = { view_id, fragment };
    cache->map.set(entry);
    if (cache->map.oom()) {
        retained_dl_fragment_free(cache->pool, fragment);
        return nullptr;
    }
    return fragment;
}

static void retained_dl_publish(RetainedDisplayListCache* cache, RetainedPaintChunk* chunk) {
    const DisplayItem* begin = &chunk->own[0];
    uint32_t view_id = begin->element_marker.view_id;
    if (!view_id) return;
    RetainedDisplayListFragment* fragment = retained_dl_fragment_get_or_create(cache, view_id);
    if (!fragment || !ref_count_retain(&chunk->refs)) return;
    dl_payload_owner_release(fragment->chunk);
    fragment->chunk = chunk;
    fragment->bounds = dl_item_bounds(begin);
    fragment->marker_bounds = {begin->element_marker.marker_x, begin->element_marker.marker_y,
        begin->element_marker.marker_x + begin->element_marker.marker_w,
        begin->element_marker.marker_y + begin->element_marker.marker_h};
    fragment->last_stored_epoch = cache->epoch;
    fragment->last_used = ++cache->clock;
}

static void retained_dl_drop_old_subtree(RetainedDisplayListCache* cache, RetainedPaintChunk* chunk) {
    uint32_t view_id = chunk->own[0].element_marker.view_id;
    RetainedDisplayListEntry* entry = cache->map.get({view_id, nullptr});
    if (entry && entry->fragment && entry->fragment->chunk == chunk &&
        entry->fragment->last_stored_epoch != cache->epoch) {
        RetainedDisplayListFragment* fragment = entry->fragment;
        cache->map.erase({view_id, nullptr});
        retained_dl_fragment_free(cache->pool, fragment);
        cache->stats.evictions++;
    }
    for (size_t i = 0; i < chunk->spans.size(); i++)
        if (chunk->spans[i].child) retained_dl_drop_old_subtree(cache, chunk->spans[i].child);
}

static bool retained_dl_trim(RetainedDisplayListCache* cache, size_t needed) {
    if (!cache || !cache->budget || needed > cache->budget->limit) return false;
    while (__atomic_load_n(&cache->budget->used, __ATOMIC_RELAXED) > cache->budget->limit - needed) {
        RetainedDisplayListFragment* oldest = nullptr;
        size_t cursor = 0; RetainedDisplayListEntry* entry;
        while (cache->map.next(&cursor, &entry)) {
            RetainedDisplayListFragment* fragment = entry->fragment;
            if (!fragment || !fragment->chunk || fragment->last_stored_epoch == cache->epoch ||
                __atomic_load_n(&fragment->chunk->parent_count, __ATOMIC_RELAXED)) continue;
            if (!oldest || fragment->last_used < oldest->last_used) oldest = fragment;
        }
        if (!oldest) return false; // current readers or this frame's new chunks retain the remaining charge.
        RetainedPaintChunk* root = oldest->chunk;
        if (!ref_count_retain(&root->refs)) return false;
        // evict one old root and its old descendants together; ancestor references otherwise prevent reclamation.
        retained_dl_drop_old_subtree(cache, root);
        dl_payload_owner_release(root);
    }
    return true;
}

static bool retained_dl_admit_chunk(RetainedDisplayListCache* cache, RetainedPaintChunk* chunk) {
    size_t bytes = sizeof(*chunk) + sizeof(RetainedDisplayListFragment) +
        chunk->own.capacity() * sizeof(DisplayItem) + chunk->spans.capacity() * sizeof(RetainedPaintSpan) +
        arena_total_allocated(chunk->arena) + dl_shared_array_bytes(&chunk->own);
    for (size_t i = 0; i < chunk->own.size(); i++) {
        const DisplayItem* item = &chunk->own[i];
        const DisplayOpDescriptor* descriptor = dl_op_descriptor(item->op);
        if (!descriptor || descriptor->owned_payload_kind != DL_OWNED_PAYLOAD_PATH) continue;
        const RdtPath* path = *(RdtPath* const*)((const char*)item + descriptor->owned_payload_offset);
        size_t path_bytes = rdt_path_storage_bytes(path);
        if (path_bytes > SIZE_MAX - bytes) return false;
        bytes += path_bytes;
    }
    // charge shared resources conservatively per chunk; borrowed frames keep the ledger alive after eviction.
    if (!retained_dl_trim(cache, bytes) || !ref_count_retain(&cache->budget->refs)) return false;
    chunk->budget = cache->budget; chunk->charged_bytes = bytes;
    __atomic_fetch_add(&cache->budget->used, bytes, __ATOMIC_RELAXED);
    return true;
}

static void retained_dl_touch_chunk(RetainedDisplayListCache* cache, RetainedPaintChunk* chunk) {
    retained_dl_publish(cache, chunk);
    cache->stats.reused_captures++;
    for (size_t i = 0; i < chunk->spans.size(); i++)
        if (chunk->spans[i].child) retained_dl_touch_chunk(cache, chunk->spans[i].child);
}

static RetainedPaintChunk* retained_dl_capture_chunk(RetainedDisplayListCache* cache,
    const DisplayList* source, int begin, int end, size_t depth) {
    if (depth > DL_VALIDATE_ELEMENT_STACK_LIMIT) return nullptr;
    const DisplayItem& marker = source->data()[begin];
    if (marker.element_marker.view_id) cache->stats.capture_candidates++;
    DisplayPayloadOwner* owner = marker.payload_owner;
    if (owner && owner->destroy == retained_chunk_destroy) {
        RetainedPaintChunk* existing = (RetainedPaintChunk*)owner;
        if (existing->budget == cache->budget && existing->logical_count == end - begin + 1 &&
            source->data()[end].payload_owner == owner && ref_count_retain(&owner->refs)) {
            // borrowed frame commands still identify their immutable producer; never recapture them.
            retained_dl_touch_chunk(cache, existing);
            return existing;
        }
    }
    RetainedPaintChunk* chunk = (RetainedPaintChunk*)mem_alloc(sizeof(RetainedPaintChunk), MEM_CAT_RENDER);
    if (chunk) {
        new (chunk) RetainedPaintChunk();
        ref_count_init(&chunk->refs); chunk->destroy = retained_chunk_destroy;
        chunk->commands = lam::up(&chunk->own);
        chunk->logical_count = end - begin + 1;
        chunk->arena = mem_arena_create(
            cache->pool ? mem_node_owner((MemNode*)pool_get_mem_node(cache->pool)) : nullptr,
            MEM_ROLE_RENDER, "retained_dl.chunk");
        if (!chunk->arena) { dl_payload_owner_release(chunk); chunk = nullptr; }
        else dl_init(&chunk->own, chunk->arena);
    }
    bool retainable = true, copied = chunk != nullptr;
    for (int i = begin; i <= end;) {
        const DisplayItem& item = source->data()[i];
        if (i != begin && item.op == DL_BEGIN_ELEMENT && item.element_marker.matching_index > i &&
            item.element_marker.matching_index < end) {
            int child_end = item.element_marker.matching_index;
            RetainedPaintChunk* child = retained_dl_capture_chunk(cache, source, i, child_end, depth + 1);
            if (!child) retainable = false;
            if (!chunk || !child || !chunk->spans.append({child, 0, 0})) {
                dl_payload_owner_release(child);
                if (child) copied = false;
            } else __atomic_fetch_add(&child->parent_count, 1u, __ATOMIC_RELAXED);
            i = child_end + 1;
            continue;
        }
        retainable = retainable && dl_item_is_retainable_for_fragment(&item);
        if (chunk && copied && retainable) {
            int own_index = chunk->own.item_count();
            copied = dl_copy_range(&chunk->own, source, i, i);
            if (copied) {
                DisplayItem& copy = chunk->own.data()[own_index];
                if (copy.op == DL_BEGIN_ELEMENT || copy.op == DL_END_ELEMENT)
                    copy.element_marker.matching_index = item.element_marker.matching_index - begin;
                if (!chunk->spans.empty() && !chunk->spans.back().child &&
                    chunk->spans.back().start + chunk->spans.back().count == own_index)
                    chunk->spans.back().count++;
                else copied = chunk->spans.append({nullptr, own_index, 1});
            }
        }
        i++;
    }
    bool budget_rejected = chunk && retainable && copied && !retained_dl_admit_chunk(cache, chunk);
    if (budget_rejected) { copied = false; cache->stats.budget_rejections++; }
    if (!retainable || !copied) {
        RetainedDisplayListEntry* previous = cache->map.get({marker.element_marker.view_id, nullptr});
        if (previous && previous->fragment) {
            dl_payload_owner_release(previous->fragment->chunk);
            previous->fragment->chunk = nullptr; // failed publication invalidates the previous version, even within one epoch.
        }
        if (marker.element_marker.view_id) {
            if (!retainable) cache->stats.skipped_non_retainable++;
            else if (!budget_rejected) cache->stats.copy_failed++;
        }
        dl_payload_owner_release(chunk);
        return nullptr;
    }
    retained_dl_publish(cache, chunk);
    if (marker.element_marker.view_id) cache->stats.captured++;
    cache->stats.captured_commands += chunk->own.size();
    RENDER_METRIC_ADD(retained_command_capacity_bytes, chunk->own.capacity() * sizeof(DisplayItem));
    return chunk;
}

void retained_dl_cache_capture(RetainedDisplayListCache* cache, const DisplayList* source) {
    if (!cache || !source) return;

    for (int i = 0; i < source->item_count(); i++) {
        const DisplayItem& item = source->data()[i];
        if (item.op != DL_BEGIN_ELEMENT || item.element_marker.matching_index <= i ||
            item.element_marker.matching_index >= source->item_count()) continue;
        int end = item.element_marker.matching_index;
        RetainedPaintChunk* chunk = retained_dl_capture_chunk(cache, source, i, end, 0);
        dl_payload_owner_release(chunk);
        i = end;
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
    if (!found || !found->fragment || !found->fragment->chunk) return nullptr;
    found->fragment->last_used = ++cache->clock;
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
    return fragment->chunk ? fragment->chunk->logical_count : 0;
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

bool dl_resources_current(const DisplayList* list,
                                          uint64_t current_video_generation,
                                          uint64_t current_glyph_generation) {
    if (!list) return false;
    // owners are read for their generation
    ImageSurfaceReadScope read_scope;
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

static bool retained_chunk_resources_valid(const RetainedPaintChunk* chunk,
    uint64_t current_video_generation, uint64_t current_glyph_generation) {
    if (!chunk || !dl_resources_current(&chunk->own, current_video_generation, current_glyph_generation)) return false;
    for (size_t i = 0; i < chunk->spans.size(); i++)
        if (chunk->spans[i].child && !retained_chunk_resources_valid(chunk->spans[i].child,
            current_video_generation, current_glyph_generation)) return false;
    return true;
}

bool retained_dl_fragment_resources_valid(const RetainedDisplayListFragment* fragment,
    uint64_t current_video_generation, uint64_t current_glyph_generation) {
    return fragment && retained_chunk_resources_valid(fragment->chunk,
        current_video_generation, current_glyph_generation);
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

static bool retained_dl_borrow_chunk(DisplayList* dst, RetainedPaintChunk* chunk) {
    int placement = dst->item_count();
    if (!dl_pin_payload_owner(dst, chunk)) return false;
    for (size_t i = 0; i < chunk->spans.size(); i++) {
        const RetainedPaintSpan& span = chunk->spans[i];
        if (span.child) {
            if (!retained_dl_borrow_chunk(dst, span.child)) return false;
            continue;
        }
        for (int j = span.start; j < span.start + span.count; j++) {
            DisplayItem* out = dl_alloc_item(dst);
            if (!out) return false;
            RENDER_METRIC_ADD(borrowed_command_bytes, sizeof(DisplayItem));
            *out = chunk->own.data()[j];
            out->payload_owner = lam::up<DisplayPayloadOwner>(chunk);
            if (out->op == DL_BEGIN_ELEMENT || out->op == DL_END_ELEMENT)
                out->element_marker.matching_index += placement;
        }
    }
    return true;
}

bool retained_dl_append_fragment(DisplayList* dst,
                                 const RetainedDisplayListFragment* fragment) {
    if (!dst || !fragment || !fragment->chunk) return false;
    int checkpoint = dst->item_count();
    size_t owner_checkpoint = dst->payload_owners.size();
    if (retained_dl_borrow_chunk(dst, fragment->chunk)) return true;
    dl_truncate(dst, checkpoint);
    // failed assembly retires its leases immediately, preserving the earlier frame.
    for (size_t i = owner_checkpoint; i < dst->payload_owners.size(); i++)
        dl_payload_owner_release(dst->payload_owners[i]);
    dst->payload_owners.remove_range(owner_checkpoint, dst->payload_owners.size() - owner_checkpoint);
    return false;
}
