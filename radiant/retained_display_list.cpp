#include "render_metrics.hpp"
#include "render.hpp"

#include "../lib/mem_factory.h"
#include "../lib/hashmap_typed.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/arena.h"

#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "../lib/generation.h"

struct RetainedPaintChunk;
enum RetainedResourceKind { RETAINED_GLYPH_EPOCH, RETAINED_VIDEO_EPOCH, RETAINED_EPOCH_COUNT,
    RETAINED_SURFACE = RETAINED_EPOCH_COUNT, RETAINED_NO_RESOURCE };
struct RetainedSurfaceStamp {
    lam::Handle<ImageSurface> resource;
    uint64_t generation;
};
struct RetainedResourceStamp {
    RetainedResourceKind kind = RETAINED_NO_RESOURCE;
    RetainedSurfaceStamp surface{};
};
struct DisplayResourceSummary {
    uint64_t epochs[RETAINED_EPOCH_COUNT]{};
    uint8_t epoch_mask = 0;
    bool consistent = true;
    lam::ArrayList<RetainedSurfaceStamp> surfaces{MEM_CAT_RENDER, 0};
    // ordered spans already pin these children; only image-bearing branches need traversal.
    lam::ArrayList<lam::Up<RetainedPaintChunk>> surface_children{MEM_CAT_RENDER, 0};
};
struct RetainedStorageBudget {
    RefCount refs;
    size_t used, limit;
    MemContext* memory;
};
static void retained_budget_release(RetainedStorageBudget* budget) {
    if (budget && ref_count_release(&budget->refs) == REF_COUNT_LAST) {
        mem_context_destroy(budget->memory);
        mem_free(budget);
    }
}
using RetainedPaintSpan = DisplayCommandSpan;
struct RetainedPlacementValidation {
    size_t span_index;
    DisplayChunkValidation validation;
};
using RetainedPlacementList = lam::ArrayList<RetainedPlacementValidation>;
struct RetainedPaintChunk : DisplayPayloadOwner {
    Arena* arena = nullptr;
    DisplayList own;
    lam::ArrayList<RetainedPaintSpan> spans{MEM_CAT_RENDER, 0};
    lam::Own<RetainedPlacementList> placements{};
    lam::Own<DisplayResourceSummary> resources{};
    DisplayChunkValidation validation{};
    bool validation_ready = false;
    // cache-only bookkeeping; the fragment clears this before releasing its publication lease.
    RetainedDisplayListFragment* indexed_fragment = nullptr;
    size_t membership_nodes = 1; // logical subtree visits, including repeated child placements
    uint32_t parent_count = 0; // same-budget edges only; foreign readers must not hide producer eviction.
    RetainedStorageBudget* budget = nullptr;
    size_t charged_bytes = 0;
};

static RetainedResourceStamp retained_item_resource_stamp(const DisplayItem* item) {
    RetainedResourceStamp stamp;
    switch (item->op) {
        case DL_DRAW_IMAGE:
            if (!item->draw_image.resource.is_null())
                stamp = {RETAINED_SURFACE, {item->draw_image.resource, item->draw_image.resource_generation}};
            break;
        case DL_BLIT_SURFACE_SCALED:
            stamp = {RETAINED_SURFACE, {item->blit_surface_scaled.src_resource, item->blit_surface_scaled.src_generation}};
            break;
        case DL_WEBVIEW_LAYER_PLACEHOLDER:
            stamp = {RETAINED_SURFACE, {item->webview_layer_placeholder.resource, item->webview_layer_placeholder.surface_generation}};
            break;
        case DL_DRAW_GLYPH:
            if (item->draw_glyph.bitmap.buffer)
                stamp = {RETAINED_GLYPH_EPOCH, {{}, item->draw_glyph.resource_generation}};
            break;
        case DL_VIDEO_PLACEHOLDER:
            if (item->video_placeholder.video)
                stamp = {RETAINED_VIDEO_EPOCH, {{}, item->video_placeholder.video_generation}};
            break;
        default: break;
    }
    return stamp;
}

static DisplayResourceSummary* retained_resources_ensure(lam::Own<DisplayResourceSummary>& owner) {
    if (!owner) {
        void* storage = mem_alloc(sizeof(DisplayResourceSummary), MEM_CAT_RENDER);
        if (!storage) return nullptr;
        owner = lam::own(new (storage) DisplayResourceSummary());
    }
    return owner;
}

static void retained_resources_merge_epoch(DisplayResourceSummary* summary,
    unsigned epoch, uint64_t generation) {
    unsigned bit = 1u << epoch;
    if (!generation || ((summary->epoch_mask & bit) && summary->epochs[epoch] != generation))
        summary->consistent = false;
    summary->epoch_mask |= bit;
    summary->epochs[epoch] = generation;
}

static void retained_resources_merge_epochs(DisplayResourceSummary* summary,
    const DisplayResourceSummary* source) {
    summary->consistent &= source->consistent;
    for (unsigned epoch = 0; epoch < RETAINED_EPOCH_COUNT; epoch++)
        if (source->epoch_mask & (1u << epoch))
            retained_resources_merge_epoch(summary, epoch, source->epochs[epoch]);
}

static bool retained_resources_add_item(lam::Own<DisplayResourceSummary>& owner, const DisplayItem* item) {
    RetainedResourceStamp stamp = retained_item_resource_stamp(item);
    if (stamp.kind == RETAINED_NO_RESOURCE) return true;
    DisplayResourceSummary* summary = retained_resources_ensure(owner);
    if (!summary) return false;
    if (stamp.kind < RETAINED_EPOCH_COUNT) {
        retained_resources_merge_epoch(summary, stamp.kind, stamp.surface.generation);
        return true;
    }
    // adjacent repeats are common; the final sort coalesces interleaved resource uses.
    if (!summary->surfaces.empty() && summary->surfaces.back().resource == stamp.surface.resource &&
        summary->surfaces.back().generation == stamp.surface.generation) return true;
    return summary->surfaces.append(stamp.surface);
}

static bool retained_resources_add_child(RetainedPaintChunk* chunk, RetainedPaintChunk* child) {
    const DisplayResourceSummary* source = child->resources;
    if (!source) return true;
    DisplayResourceSummary* summary = retained_resources_ensure(chunk->resources);
    if (!summary) return false;
    retained_resources_merge_epochs(summary, source);
    return (source->surfaces.empty() && source->surface_children.empty()) ||
        summary->surface_children.append(lam::up(child));
}

static int retained_surface_stamp_compare(const void* left, const void* right) {
    const auto* a = (const RetainedSurfaceStamp*)left;
    const auto* b = (const RetainedSurfaceStamp*)right;
    int handle_order = memcmp(&a->resource, &b->resource, sizeof(a->resource));
    return handle_order ? handle_order : (a->generation > b->generation) - (a->generation < b->generation);
}

static void retained_resources_finish(DisplayResourceSummary* summary) {
    if (!summary) return;
    auto& surfaces = summary->surfaces;
    if (surfaces.size() < 2) return;
    qsort(surfaces.data(), surfaces.size(), sizeof(RetainedSurfaceStamp), retained_surface_stamp_compare);
    size_t count = 1;
    for (size_t i = 1; i < surfaces.size(); i++)
        if (retained_surface_stamp_compare(&surfaces[count - 1], &surfaces[i]) != 0)
            surfaces[count++] = surfaces[i];
    surfaces.remove_range(count, surfaces.size() - count);
}

void dl_resource_summary_destroy(DisplayResourceSummary* summary) {
    if (!summary) return;
    summary->~DisplayResourceSummary(); mem_free(summary);
}

size_t dl_resource_summary_bytes(const DisplayResourceSummary* summary) {
    return summary ? sizeof(*summary) + summary->surfaces.capacity() * sizeof(RetainedSurfaceStamp) +
        summary->surface_children.capacity() * sizeof(lam::Up<RetainedPaintChunk>) : 0;
}

static bool retained_resources_copy_flat(lam::Own<DisplayResourceSummary>& owner,
    const DisplayResourceSummary* source) {
    RENDER_METRIC_ADD(resource_capture_chunks, 1);
    if (!source) return true;
    DisplayResourceSummary* summary = retained_resources_ensure(owner);
    if (!summary) return false;
    retained_resources_merge_epochs(summary, source);
    for (size_t i = 0; i < source->surfaces.size(); i++)
        if (!summary->surfaces.append(source->surfaces[i])) return false;
    // copy values from image-bearing branches; the snapshot must not borrow publication storage.
    for (size_t i = 0; i < source->surface_children.size(); i++)
        if (!retained_resources_copy_flat(owner, source->surface_children[i]->resources)) return false;
    return true;
}

bool dl_resource_summary_capture(const DisplayList* list, DisplayResourceSummary** snapshot,
    size_t storage_budget) {
    if (!snapshot) return false;
    *snapshot = nullptr;
    if (!list) return false;
    lam::Own<DisplayResourceSummary> summary{};
    DisplayListCursor cursor(list);
    while (const DisplayItem* item = cursor.next()) {
        if (cursor.owner && cursor.index == 0) {
            if (const auto* validation = retained_dl_chunk_validation(cursor.owner)) {
                const auto* chunk = (const RetainedPaintChunk*)cursor.owner;
                // foreign wrappers still consume graph depth even when a private subtree is skipped.
                if (validation->max_fragment_depth > DL_VALIDATE_ELEMENT_STACK_LIMIT - cursor.depth + 1 ||
                    !retained_resources_copy_flat(summary, chunk->resources)) {
                    dl_resource_summary_destroy(summary);
                    return false;
                }
                cursor.skip_element();
                continue;
            }
        }
        RENDER_METRIC_ADD(resource_capture_commands, 1);
        // copy dependency values so the snapshot never borrows command or chunk storage.
        if (!retained_resources_add_item(summary, item)) {
            dl_resource_summary_destroy(summary);
            return false;
        }
    }
    if (cursor.failed || dl_resource_summary_bytes(summary) > storage_budget) {
        dl_resource_summary_destroy(summary);
        return false;
    }
    retained_resources_finish(summary);
    *snapshot = summary;
    return true;
}
struct RetainedDisplayListFragment {
    uint32_t view_id;
    Bound bounds;
    Bound marker_bounds;
    RetainedPaintChunk* chunk;
    uint32_t last_stored_epoch;
    uint64_t last_used;
    RetainedDisplayListFragment* previous = nullptr;
    RetainedDisplayListFragment* next = nullptr;
    RetainedDisplayListFragment* parent = nullptr;
    RetainedDisplayListFragment* first_child = nullptr;
    RetainedDisplayListFragment* last_child = nullptr;
    bool complete = false;
};

static void retained_chunk_destroy(DisplayPayloadOwner* owner) {
    RetainedPaintChunk* chunk = (RetainedPaintChunk*)owner;
    for (size_t i = 0; i < chunk->spans.size(); i++) {
        RetainedPaintChunk* child = (RetainedPaintChunk*)chunk->spans[i].child;
        if (child && child->budget == chunk->budget)
            __atomic_fetch_sub(&child->parent_count, 1u, __ATOMIC_RELAXED);
        dl_payload_owner_release(child);
    }
    dl_destroy(&chunk->own);
    if (chunk->placements) {
        chunk->placements->~RetainedPlacementList(); mem_free(chunk->placements);
    }
    dl_resource_summary_destroy(chunk->resources);
    if (chunk->arena) mem_arena_destroy(chunk->arena);
    if (chunk->budget) {
        __atomic_fetch_sub(&chunk->budget->used, chunk->charged_bytes, __ATOMIC_RELAXED);
        retained_budget_release(chunk->budget);
    }
    chunk->~RetainedPaintChunk(); mem_free(chunk);
}

const DisplayChunkValidation* retained_dl_chunk_validation(const DisplayPayloadOwner* owner) {
    if (!owner || owner->destroy != retained_chunk_destroy) return nullptr;
    const auto* chunk = (const RetainedPaintChunk*)owner;
    return chunk->validation_ready ? &chunk->validation : nullptr;
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
    RetainedDisplayListFragment* oldest_index;
    RetainedDisplayListFragment* newest_index;

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

static void retained_dl_index_unlink(RetainedDisplayListCache* cache, RetainedDisplayListFragment* fragment) {
    auto& first = fragment->parent ? fragment->parent->first_child : cache->oldest_index;
    auto& last = fragment->parent ? fragment->parent->last_child : cache->newest_index;
    if (fragment->previous) fragment->previous->next = fragment->next;
    else if (first == fragment) first = fragment->next;
    if (fragment->next) fragment->next->previous = fragment->previous;
    else if (last == fragment) last = fragment->previous;
    fragment->previous = fragment->next = nullptr;
    fragment->parent = nullptr;
}

static void retained_dl_index_link(RetainedDisplayListCache* cache, RetainedDisplayListFragment* fragment,
    RetainedDisplayListFragment* parent, bool append) {
    auto& first = parent ? parent->first_child : cache->oldest_index;
    auto& last = parent ? parent->last_child : cache->newest_index;
    fragment->parent = parent;
    fragment->previous = append ? last : nullptr;
    fragment->next = append ? nullptr : first;
    if (fragment->previous) fragment->previous->next = fragment;
    else first = fragment;
    if (fragment->next) fragment->next->previous = fragment;
    else last = fragment;
}

static bool retained_dl_index_current(const RetainedDisplayListCache* cache,
    const RetainedDisplayListFragment* fragment) {
    for (; fragment; fragment = fragment->parent)
        if (fragment->last_stored_epoch == cache->epoch) return true;
    return false;
}

static void retained_dl_index_detach(RetainedDisplayListCache* cache, RetainedDisplayListFragment* fragment) {
    // materialize inherited membership before removing its only path to a painted group.
    for (auto* parent = fragment->parent; parent; parent = parent->parent) {
        if (parent->last_stored_epoch > fragment->last_stored_epoch)
            fragment->last_stored_epoch = parent->last_stored_epoch;
        if (parent->last_used > fragment->last_used) fragment->last_used = parent->last_used;
        parent->complete = false;
    }
    retained_dl_index_unlink(cache, fragment);
}

static void retained_dl_index_touch(RetainedDisplayListCache* cache, RetainedDisplayListFragment* fragment,
    size_t logical_visits = 1) {
    retained_dl_index_detach(cache, fragment);
    fragment->last_stored_epoch = cache->epoch;
    cache->clock += logical_visits;
    fragment->last_used = cache->clock;
    retained_dl_index_link(cache, fragment, nullptr, true);
}

static void retained_dl_fragment_clear(RetainedDisplayListCache* cache, RetainedDisplayListFragment* fragment) {
    retained_dl_index_detach(cache, fragment);
    while (auto* child = fragment->first_child) {
        retained_dl_index_detach(cache, child);
        // expired groups stay ahead of painted roots, including when a binding is replaced mid-capture.
        retained_dl_index_link(cache, child, nullptr, child->last_stored_epoch == cache->epoch);
    }
    fragment->complete = false;
    retained_dl_index_link(cache, fragment, nullptr, fragment->last_stored_epoch == cache->epoch);
    RetainedPaintChunk* chunk = fragment->chunk;
    // pinned frames can outlive replacement, pruning or the cache itself.
    if (chunk && chunk->indexed_fragment == fragment) chunk->indexed_fragment = nullptr;
    fragment->chunk = nullptr;
    dl_payload_owner_release(chunk);
}

static void retained_dl_fragment_free(RetainedDisplayListCache* cache, RetainedDisplayListFragment* fragment) {
    if (!fragment) return;
    retained_dl_fragment_clear(cache, fragment);
    retained_dl_index_unlink(cache, fragment);
    fragment->~RetainedDisplayListFragment();
    if (cache->pool) pool_free(cache->pool, fragment);
    else lam::Temp<RetainedDisplayListFragment> storage(fragment);
}

bool RetainedDisplayListCache::init(Pool* owner_pool) {
    pool = owner_pool;
    budget = (RetainedStorageBudget*)mem_calloc(1, sizeof(RetainedStorageBudget), MEM_CAT_RENDER);
    if (!budget) return false;
    ref_count_init(&budget->refs); budget->limit = (size_t)32 << 20;
    // publication readers pin this owner independently of the source document's teardown.
    budget->memory = mem_context_create(mem_context_process(MEM_ROLE_RENDER), MEM_ROLE_RENDER, "retained_dl.storage");
    if (!budget->memory) { retained_budget_release(budget); budget = nullptr; return false; }
    mem_context_set_doc_id(budget->memory,
        mem_context_doc_id(mem_node_owner((MemNode*)pool_get_mem_node(pool))));
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
        while (oldest_index) retained_dl_fragment_free(this, oldest_index);
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
    if (epoch == 0) {
        epoch = 1;
        // wrapped epochs must not revive index entries left idle by whole-frame reuse.
        size_t cursor = 0; RetainedDisplayListEntry* entry;
        while (map.next(&cursor, &entry)) entry->fragment->last_stored_epoch = 0;
    }
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
        retained_dl_fragment_free(cache, fragment);
        return nullptr;
    }
    return fragment;
}

static void retained_dl_publish(RetainedDisplayListCache* cache, RetainedPaintChunk* chunk, uint32_t key = 0) {
    const DisplayItem* begin = &chunk->own[0];
    uint32_t view_id = key ? key : begin->element_marker.view_id;
    if (!view_id) return;
    RENDER_METRIC_ADD(retained_index_publications, 1);
    if (!ref_count_retain(&chunk->refs)) return;
    RetainedDisplayListFragment* fragment = retained_dl_fragment_get_or_create(cache, view_id);
    if (!fragment) { dl_payload_owner_release(chunk); return; }
    retained_dl_fragment_clear(cache, fragment);
    fragment->chunk = chunk;
    chunk->indexed_fragment = fragment;
    fragment->bounds = dl_item_bounds(begin);
    fragment->marker_bounds = {begin->element_marker.marker_x, begin->element_marker.marker_y,
        begin->element_marker.marker_x + begin->element_marker.marker_w,
        begin->element_marker.marker_y + begin->element_marker.marker_h};
    retained_dl_index_touch(cache, fragment);
}

static void retained_dl_index_group(RetainedDisplayListCache* cache, RetainedPaintChunk* chunk) {
    auto* fragment = chunk->indexed_fragment;
    if (!fragment) return;
    for (size_t i = 0; i < chunk->spans.size(); i++) if (chunk->spans[i].child) {
        auto* child_chunk = (RetainedPaintChunk*)chunk->spans[i].child;
        if (child_chunk->budget != cache->budget) continue;
        auto* child = child_chunk->indexed_fragment;
        if (!child || child == fragment || child->parent == fragment) continue;
        retained_dl_index_detach(cache, child);
        retained_dl_index_link(cache, child, fragment, true);
    }
    fragment->complete = true;
    // a later shared child can invalidate an earlier branch while groups are attached.
    for (size_t i = 0; i < chunk->spans.size(); i++) if (chunk->spans[i].child) {
        auto* child_chunk = (RetainedPaintChunk*)chunk->spans[i].child;
        if (child_chunk->budget != cache->budget) continue;
        auto* child = child_chunk->indexed_fragment;
        if (!child || child == fragment || child->parent != fragment || !child->complete)
            fragment->complete = false;
    }
}

static void retained_dl_drop_old_subtree(RetainedDisplayListCache* cache, DisplayPayloadOwner* owner) {
    RetainedPaintChunk* chunk = (RetainedPaintChunk*)owner;
    if (chunk->budget != cache->budget) return;
    RetainedDisplayListFragment* fragment = chunk->indexed_fragment;
    // subscene cache keys need not equal semantic marker IDs; retire the actual publication.
    if (fragment && !retained_dl_index_current(cache, fragment)) {
        cache->map.erase({fragment->view_id, nullptr});
        retained_dl_fragment_free(cache, fragment);
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
            // parented chunks cannot reclaim storage; avoid ancestor lookups for those entries.
            if (!fragment || !fragment->chunk ||
                __atomic_load_n(&fragment->chunk->parent_count, __ATOMIC_RELAXED) ||
                retained_dl_index_current(cache, fragment)) continue;
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
    if (chunk->placements) bytes += sizeof(RetainedPlacementList) +
        chunk->placements->capacity() * sizeof(RetainedPlacementValidation);
    bytes += dl_resource_summary_bytes(chunk->resources);
    // a foreign lease pins its whole producer graph; reserve that liability in this cache too.
    for (size_t i = 0; i < chunk->spans.size(); i++) if (chunk->spans[i].child) {
        const auto* child = (const RetainedPaintChunk*)chunk->spans[i].child;
        if (child->budget == cache->budget) continue;
        if (child->retained_bytes > SIZE_MAX - bytes) return false;
        bytes += child->retained_bytes;
    }
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
    if (!retained_dl_trim(cache, bytes)) return false;
    chunk->retained_bytes = bytes;
    for (size_t i = 0; i < chunk->spans.size(); i++) if (chunk->spans[i].child) {
        if (((RetainedPaintChunk*)chunk->spans[i].child)->budget != cache->budget) continue;
        size_t child_bytes = chunk->spans[i].child->retained_bytes;
        chunk->retained_bytes = child_bytes > SIZE_MAX - chunk->retained_bytes ? SIZE_MAX : chunk->retained_bytes + child_bytes;
    }
    chunk->budget = cache->budget; chunk->charged_bytes = bytes;
    __atomic_fetch_add(&cache->budget->used, bytes, __ATOMIC_RELAXED);
    return true;
}

static void retained_dl_touch_chunk(RetainedDisplayListCache* cache, DisplayPayloadOwner* owner) {
    RetainedPaintChunk* chunk = (RetainedPaintChunk*)owner;
    // foreign subscenes belong to another publication index and never refresh its membership here.
    if (chunk->budget != cache->budget) return;
    RENDER_METRIC_ADD(retained_membership_nodes, 1);
    if (chunk->indexed_fragment && chunk->indexed_fragment->complete) {
        retained_dl_index_touch(cache, chunk->indexed_fragment, chunk->membership_nodes);
        cache->stats.reused_captures += chunk->membership_nodes;
        RENDER_METRIC_ADD(retained_membership_groups, 1);
        return;
    }
    if (chunk->indexed_fragment) retained_dl_index_touch(cache, chunk->indexed_fragment);
    else retained_dl_publish(cache, chunk);
    cache->stats.reused_captures++;
    for (size_t i = 0; i < chunk->spans.size(); i++)
        if (chunk->spans[i].child) retained_dl_touch_chunk(cache, chunk->spans[i].child);
    retained_dl_index_group(cache, chunk);
}

static bool retained_dl_add_placement(RetainedPaintChunk* chunk, const DisplayChunkValidation* validation) {
    if (!validation) return true;
    if (!chunk->placements) {
        void* storage = mem_alloc(sizeof(RetainedPlacementList), MEM_CAT_RENDER);
        if (!storage) return false;
        chunk->placements = lam::own(new (storage) RetainedPlacementList(MEM_CAT_RENDER, 0));
        // most captured parents contain one SVG placement; avoid the default sixteen-entry allocation.
        if (!chunk->placements->reserve(1)) return false;
    }
    return chunk->placements->append({chunk->spans.size() - 1, *validation});
}

static bool retained_dl_approved_local_placement(const DisplayListCursor& cursor);

static RetainedPaintChunk* retained_dl_capture_chunk(RetainedDisplayListCache* cache,
    DisplayListCursor* cursor, size_t depth, uint32_t key = 0) {
    bool translated = cursor->translate_x != 0 || cursor->translate_y != 0;
    const DisplayItem marker = *cursor->item;
    uint32_t view_id = key ? key : marker.element_marker.view_id;
    if (depth > DL_VALIDATE_ELEMENT_STACK_LIMIT) { cursor->skip_element(); return nullptr; }
    if (view_id) cache->stats.capture_candidates++;
    const DisplayPayloadOwner* scope = cursor->owner;
    int end = marker.element_marker.matching_index;
    if (scope && cursor->index == 0 && scope->destroy == retained_chunk_destroy) {
        RetainedPaintChunk* existing = (RetainedPaintChunk*)scope;
        bool local = existing->budget == cache->budget;
        // zero-ID leaf subscenes expose no descendant view bindings; their parent budgets the lease.
        bool foreign_leaf = !local && depth > 0 && !key && existing->validation_ready &&
            !marker.element_marker.view_id && existing->span_count == 1 && !existing->command_spans[0].child;
        bool local_placement = local && translated && retained_dl_approved_local_placement(*cursor);
        if (((local && (!translated || local_placement)) || foreign_leaf) && ref_count_retain(&existing->refs)) {
            // immutable publications keep their identity when a mixed frame is captured again.
            if (local) retained_dl_touch_chunk(cache, existing);
            else RENDER_METRIC_ADD(retained_foreign_chunks, 1);
            cursor->skip_element();
            return existing;
        }
    }
    // other placements cannot refresh a semantic publication or copy its unshifted raw descriptors.
    if (translated) { cursor->skip_element(); return nullptr; }
    RetainedPaintChunk* chunk = (RetainedPaintChunk*)mem_alloc(sizeof(RetainedPaintChunk), MEM_CAT_RENDER);
    if (chunk) {
        new (chunk) RetainedPaintChunk();
        ref_count_init(&chunk->refs); chunk->destroy = retained_chunk_destroy;
        chunk->commands = lam::up(&chunk->own);
        // pin the budget before linking children so failure cleanup knows which edges are local.
        if (ref_count_retain(&cache->budget->refs)) {
            chunk->budget = cache->budget;
            chunk->arena = mem_arena_create(cache->budget->memory, MEM_ROLE_RENDER, "retained_dl.chunk");
        }
        if (!chunk->arena) { dl_payload_owner_release(chunk); chunk = nullptr; }
        else dl_init(&chunk->own, chunk->arena);
    }
    bool retainable = true, copied = chunk != nullptr, first = true, closed = false;
    bool placed_children = false;
    int commands = 0;
    do {
        const DisplayItem& item = *cursor->item;
        if (!first && item.op == DL_BEGIN_ELEMENT) {
            float dx = cursor->translate_x, dy = cursor->translate_y;
            const auto* placement = retained_dl_cursor_validation(*cursor);
            RetainedPaintChunk* child = retained_dl_capture_chunk(cache, cursor, depth + 1);
            if (!child) retainable = false;
            if (!chunk || !child || !chunk->spans.append({child, 0, 0, dx, dy})) {
                dl_payload_owner_release(child);
                if (child) copied = false;
            } else {
                placed_children |= dx != 0 || dy != 0;
                if ((dx != 0 || dy != 0) && !retained_dl_add_placement(chunk, placement)) copied = false;
                bool local_child = child->budget == cache->budget;
                if (local_child) __atomic_fetch_add(&child->parent_count, 1u, __ATOMIC_RELAXED);
                if (!retained_resources_add_child(chunk, child)) copied = false;
                if (local_child) {
                    if (child->membership_nodes > SIZE_MAX - chunk->membership_nodes) copied = false;
                    else chunk->membership_nodes += child->membership_nodes;
                }
                if (child->command_count > INT_MAX - commands) copied = false;
                else commands += child->command_count;
            }
        } else {
            retainable = retainable && dl_item_is_retainable_for_fragment(&item);
            if (chunk && copied && retainable) {
                int own_index = chunk->own.item_count();
                const DisplayList* source = cursor->owner ? cursor->owner->commands.get() : cursor->root;
                copied = dl_copy_range(&chunk->own, source, cursor->source_index, cursor->source_index);
                if (copied) copied = retained_resources_add_item(chunk->resources, &item);
                if (copied) {
                    if (!chunk->spans.empty() && !chunk->spans.back().child &&
                        chunk->spans.back().start + chunk->spans.back().count == own_index)
                        chunk->spans.back().count++;
                    else copied = chunk->spans.append({nullptr, own_index, 1});
                }
            }
            if (commands == INT_MAX) copied = false;
            else commands++;
            if (!first && item.op == DL_END_ELEMENT && cursor->owner == scope && cursor->index == end) {
                closed = true; break;
            }
        }
        first = false;
    } while (cursor->next());
    copied = copied && closed && !cursor->failed;
    if (chunk && copied && retainable) {
        chunk->command_count = commands;
        chunk->own.front().element_marker.matching_index = commands - 1;
        chunk->own.back().element_marker.matching_index = 0;
        chunk->command_spans = chunk->spans.data(); chunk->span_count = chunk->spans.size();
        retained_resources_finish(chunk->resources);
        // uncertified ad hoc captures keep the full validation path; child certificates compose here.
        chunk->validation_ready = dl_validate_chunk(chunk, nullptr, &chunk->validation);
        // source certificates cover zero placement; changed transforms/pixel operations need successful validation.
        if (placed_children && !chunk->validation_ready) copied = false;
    }
    bool budget_rejected = chunk && retainable && copied && !retained_dl_admit_chunk(cache, chunk);
    if (budget_rejected) { copied = false; cache->stats.budget_rejections++; }
    if (!retainable || !copied) {
        RetainedDisplayListEntry* previous = cache->map.get({view_id, nullptr});
        if (previous && previous->fragment) {
            retained_dl_fragment_clear(cache, previous->fragment);
        }
        if (view_id) {
            if (!retainable) cache->stats.skipped_non_retainable++;
            else if (!budget_rejected) cache->stats.copy_failed++;
        }
        dl_payload_owner_release(chunk);
        return nullptr;
    }
    retained_dl_publish(cache, chunk, key);
    retained_dl_index_group(cache, chunk);
    if (view_id) cache->stats.captured++;
    cache->stats.captured_commands += chunk->own.size();
    RENDER_METRIC_ADD(retained_command_capacity_bytes, chunk->own.capacity() * sizeof(DisplayItem));
    return chunk;
}

static bool retained_dl_cache_capture_with_snapshot(RetainedDisplayListCache* cache, const DisplayList* source,
    int document_commands, DisplayResourceSummary** snapshot, DisplayList* frame_commands = nullptr) {
    if (snapshot) *snapshot = nullptr;
    if (!cache || !source || (snapshot && (document_commands < 0 || document_commands > source->item_count())))
        return false;
    if (frame_commands && (frame_commands == source || !snapshot ||
        !frame_commands->empty() || frame_commands->arena.arena)) return false;

    lam::Own<DisplayResourceSummary> summary{};
    bool summary_ready = snapshot != nullptr;
    bool frame_ready = frame_commands && frame_commands->reserve(1);
    DisplayListCursor cursor(source);
    while (const DisplayItem* item = cursor.next()) {
        bool summarize = summary_ready && cursor.root_index < document_commands;
        if (item->op != DL_BEGIN_ELEMENT || item->element_marker.matching_index <= cursor.index ||
            item->element_marker.matching_index >= cursor.scope_size()) {
            if (summarize) {
                RENDER_METRIC_ADD(resource_capture_commands, 1);
                summary_ready = retained_resources_add_item(summary, item);
            }
            if (frame_ready && cursor.root_index < document_commands)
                frame_ready = !cursor.owner && dl_item_is_retainable_for_fragment(item) &&
                    dl_copy_range(frame_commands, source, cursor.root_index, cursor.root_index);
            continue;
        }
        // a prefix must contain complete top-level scopes, never part of a mutable element range.
        if (summarize && !cursor.owner && item->element_marker.matching_index >= document_commands)
            summary_ready = summarize = frame_ready = false;
        bool assemble = frame_ready && cursor.root_index < document_commands;
        // a foreign wrapper can expose only part of a root command; never replace it with an inner scope.
        if (assemble && cursor.owner && (cursor.depth != 1 || cursor.index != 0 ||
            item->element_marker.matching_index != cursor.owner->command_count - 1))
            frame_ready = assemble = false;
        RetainedPaintChunk* chunk = retained_dl_capture_chunk(cache, &cursor, 0);
        if (summarize) summary_ready = chunk && chunk->validation_ready &&
            retained_resources_copy_flat(summary, chunk->resources);
        if (assemble) frame_ready = chunk && chunk->validation_ready &&
            dl_reference_fragment(frame_commands, chunk, dl_item_bounds(&chunk->own.front()));
        dl_payload_owner_release(chunk);
    }

    // touches move entries to the tail, so only the expired prefix needs inspection.
    while (RetainedDisplayListFragment* fragment = cache->oldest_index) {
        RENDER_METRIC_ADD(retained_index_prune_checks, 1);
        if (fragment->last_stored_epoch == cache->epoch) break;
        cache->map.erase({fragment->view_id, nullptr});
        retained_dl_fragment_free(cache, fragment);
    }
    if (!snapshot || !summary_ready || cursor.failed || (frame_commands && !frame_ready)) {
        if (frame_commands) dl_clear(frame_commands);
        dl_resource_summary_destroy(summary);
        return false;
    }
    retained_resources_finish(summary);
    *snapshot = summary;
    return true;
}

void retained_dl_cache_capture(RetainedDisplayListCache* cache, const DisplayList* source) {
    retained_dl_cache_capture_with_snapshot(cache, source, 0, nullptr);
}

const RetainedDisplayListFragment* retained_dl_cache_capture_fragment(RetainedDisplayListCache* cache,
    const DisplayList* source, int first, int end, uint32_t key) {
    if (!cache || !source || !key || first < 0 || end <= first || end > source->item_count() ||
        !dl_validate_range(source, first, end, nullptr)) return nullptr;
    const DisplayItem& begin = source->data()[first];
    // cache identity is separate from a subscene marker; it must never impersonate an HTML view.
    if (begin.op != DL_BEGIN_ELEMENT || begin.element_marker.view_id ||
        begin.element_marker.matching_index != end - 1) return nullptr;
    retained_dl_cache_begin_frame(cache);
    DisplayListCursor cursor(source, first, end);
    if (!cursor.next()) return nullptr;
    RetainedPaintChunk* chunk = retained_dl_capture_chunk(cache, &cursor, 0, key);
    const auto* fragment = chunk && chunk->validation_ready ? chunk->indexed_fragment : nullptr;
    dl_payload_owner_release(chunk);
    return fragment;
}

bool retained_dl_cache_capture_resources(RetainedDisplayListCache* cache, const DisplayList* source,
    int document_commands, DisplayResourceSummary** snapshot, DisplayList* frame_commands) {
    return retained_dl_cache_capture_with_snapshot(cache, source, document_commands, snapshot, frame_commands);
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
    return fragment->chunk ? fragment->chunk->command_count : 0;
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
    RENDER_METRIC_ADD(resource_surface_checks, 1);
    ImageSurface* surface = image_surface_lookup(resource);
    return surface && generation_current(generation, surface->generation);
}

static bool retained_resource_stamp_current(const RetainedResourceStamp& stamp,
    uint64_t current_video_generation, uint64_t current_glyph_generation) {
    if (stamp.kind == RETAINED_NO_RESOURCE) return true;
    if (stamp.kind == RETAINED_SURFACE) {
        return retained_dl_surface_generation_current(stamp.surface.resource, stamp.surface.generation);
    }
    RENDER_METRIC_ADD(resource_epoch_checks, 1);
    uint64_t current = stamp.kind == RETAINED_GLYPH_EPOCH ? current_glyph_generation : current_video_generation;
    return generation_current(stamp.surface.generation, current);
}

static bool retained_summary_surfaces_current(const DisplayResourceSummary* summary) {
    if (!summary) return true;
    for (size_t i = 0; i < summary->surfaces.size(); i++) {
        const RetainedSurfaceStamp& stamp = summary->surfaces[i];
        if (!retained_dl_surface_generation_current(stamp.resource, stamp.generation)) return false;
    }
    for (size_t i = 0; i < summary->surface_children.size(); i++) {
        RENDER_METRIC_ADD(resource_child_checks, 1);
        if (!retained_summary_surfaces_current(summary->surface_children[i]->resources)) return false;
    }
    return true;
}

static bool retained_summary_current(const DisplayResourceSummary* summary,
    uint64_t current_video_generation, uint64_t current_glyph_generation) {
    RENDER_METRIC_ADD(resource_summary_checks, 1);
    if (!summary) return true;
    if (!summary->consistent) return false;
    const uint64_t current[] = {current_glyph_generation, current_video_generation};
    for (unsigned epoch = 0; epoch < RETAINED_EPOCH_COUNT; epoch++) if (summary->epoch_mask & (1u << epoch)) {
        RENDER_METRIC_ADD(resource_epoch_checks, 1);
        if (!generation_current(summary->epochs[epoch], current[epoch])) return false;
    }
    return retained_summary_surfaces_current(summary);
}

bool dl_resource_summary_current(const DisplayResourceSummary* summary,
    uint64_t current_video_generation, uint64_t current_glyph_generation) {
    ImageSurfaceReadScope read_scope;
    return retained_summary_current(summary, current_video_generation, current_glyph_generation);
}

bool dl_resources_current(const DisplayList* list,
    uint64_t current_video_generation, uint64_t current_glyph_generation) {
    if (!list) return false;
    ImageSurfaceReadScope read_scope;
    DisplayListCursor cursor(list);
    while (const DisplayItem* item = cursor.next()) {
        if (cursor.owner && cursor.index == 0 && cursor.owner->destroy == retained_chunk_destroy) {
            // published summaries cover the whole chunk, including nested resource-bearing spans.
            if (!retained_summary_current(((const RetainedPaintChunk*)cursor.owner)->resources,
                    current_video_generation, current_glyph_generation)) return false;
            cursor.skip_element();
            continue;
        }
        RENDER_METRIC_ADD(resource_command_checks, 1);
        if (!retained_resource_stamp_current(retained_item_resource_stamp(item),
                current_video_generation, current_glyph_generation)) return false;
    }
    return !cursor.failed;
}

bool retained_dl_fragment_resources_valid(const RetainedDisplayListFragment* fragment,
    uint64_t current_video_generation, uint64_t current_glyph_generation) {
    // one read scope pins all surface lookups until the complete dependency decision.
    ImageSurfaceReadScope read_scope;
    return fragment && fragment->chunk && retained_summary_current(fragment->chunk->resources,
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
            contains_view(contains_userdata, dirty)) {
            return false;
        }
    }

    return retained_dl_append_fragment(dst, fragment);
}

bool retained_dl_append_fragment(DisplayList* dst,
                                 const RetainedDisplayListFragment* fragment) {
    return dst && fragment && fragment->chunk &&
        dl_reference_fragment(dst, fragment->chunk, fragment->bounds);
}

struct RetainedPlacedChunk : DisplayPayloadOwner {
    DisplayCommandSpan child;
    DisplayChunkValidation validation{}; // child summary, excluding this shell's graph level.
    bool validation_ready = false;
    bool capture_local = false; // set only after exact solid-box admission; the source stays canonical.
};

static void retained_placed_chunk_destroy(DisplayPayloadOwner* owner) {
    auto* placed = static_cast<RetainedPlacedChunk*>(owner);
    dl_payload_owner_release(placed->child.child);
    placed->~RetainedPlacedChunk(); mem_free(placed);
}

static bool retained_dl_approved_local_placement(const DisplayListCursor& cursor) {
    if (cursor.depth < 2) return false;
    const auto* enclosing = cursor.chunks[cursor.depth - 2].owner;
    if (enclosing->destroy != retained_placed_chunk_destroy) return false;
    const auto* placed = (const RetainedPlacedChunk*)enclosing;
    return placed->capture_local && retained_dl_cursor_validation(cursor);
}

const DisplayChunkValidation* retained_dl_cursor_validation(const DisplayListCursor& cursor) {
    if (!cursor.owner || cursor.index != 0) return nullptr;
    if (cursor.translate_x == 0 && cursor.translate_y == 0)
        return retained_dl_chunk_validation(cursor.owner);
    if (cursor.depth < 2) return nullptr;
    const auto& enclosing = cursor.chunks[cursor.depth - 2];
    const auto* parent = enclosing.owner;
    if (parent->destroy == retained_placed_chunk_destroy) {
        const auto* placed = (const RetainedPlacedChunk*)parent;
        // an outer placement must not reuse the shell's original finite-transform attestation.
        return placed->validation_ready && placed->child.child == cursor.owner &&
            cursor.translate_x == placed->translate_x && cursor.translate_y == placed->translate_y
            ? &placed->validation : nullptr;
    }
    if (parent->destroy != retained_chunk_destroy || !enclosing.span) return nullptr;
    const auto* chunk = (const RetainedPaintChunk*)parent;
    size_t span_index = enclosing.span - 1;
    if (!chunk->placements || span_index >= chunk->spans.size()) return nullptr;
    const auto& span = chunk->spans[span_index];
    if (span.child != cursor.owner || cursor.translate_x != span.translate_x ||
        cursor.translate_y != span.translate_y) return nullptr;
    // capture appends sparse attestations in span order; wide parents avoid a linear search per child.
    const auto& placements = *chunk->placements;
    size_t first = 0, end = placements.size();
    while (first < end) {
        size_t mid = first + (end - first) / 2;
        if (placements[mid].span_index < span_index) first = mid + 1;
        else end = mid;
    }
    return first < placements.size() && placements[first].span_index == span_index
        ? &placements[first].validation : nullptr;
}

static bool retained_dl_append_fragment_at(DisplayList* dst,
    const RetainedDisplayListFragment* fragment, float dx, float dy, bool capture_local) {
    if (!dst || !fragment || !fragment->chunk || !isfinite(dx) || !isfinite(dy)) return false;
    if (dx == 0 && dy == 0) return retained_dl_append_fragment(dst, fragment);
    Bound bounds = {fragment->bounds.left + dx, fragment->bounds.top + dy,
        fragment->bounds.right + dx, fragment->bounds.bottom + dy};
    if (!isfinite(bounds.left) || !isfinite(bounds.top) || !isfinite(bounds.right) || !isfinite(bounds.bottom)) return false;
    auto* placed = (RetainedPlacedChunk*)mem_alloc(sizeof(RetainedPlacedChunk), MEM_CAT_RENDER);
    if (!placed) return false;
    new (placed) RetainedPlacedChunk();
    ref_count_init(&placed->refs); placed->destroy = retained_placed_chunk_destroy;
    if (!ref_count_retain(&fragment->chunk->refs)) { mem_free(placed); return false; }
    placed->commands = fragment->chunk->commands;
    placed->child = {fragment->chunk, 0, 0};
    placed->command_spans = &placed->child; placed->span_count = 1;
    placed->command_count = fragment->chunk->command_count;
    placed->translate_x = dx; placed->translate_y = dy;
    placed->capture_local = capture_local;
    placed->retained_bytes = fragment->chunk->retained_bytes > SIZE_MAX - sizeof(*placed)
        ? SIZE_MAX : sizeof(*placed) + fragment->chunk->retained_bytes;
    // translation must validate every command, including finite transforms and unsupported pixel operations.
    bool ok = dl_validate_chunk(placed, nullptr, &placed->validation);
    if (ok) {
        // immutable publication happens only after the complete moved stream passes validation.
        placed->validation.max_fragment_depth--;
        placed->validation_ready = true;
        ok = dl_reference_fragment(dst, placed, bounds);
    }
    dl_payload_owner_release(placed);
    return ok;
}

bool retained_dl_append_fragment_translated(DisplayList* dst,
    const RetainedDisplayListFragment* fragment, float dx, float dy) {
    return retained_dl_append_fragment_at(dst, fragment, dx, dy, false);
}

bool retained_dl_append_solid_box(DisplayList* dst, const RetainedDisplayListFragment* fragment,
    Rect current, Color color) {
    const auto* chunk = fragment ? fragment->chunk : nullptr;
    if (!chunk || !chunk->validation_ready || chunk->span_count != 1 ||
        chunk->command_spans[0].child || chunk->own.size() != 3 ||
        chunk->own[1].op != DL_FILL_RECT) return false;
    const auto& marker = chunk->own.front().element_marker;
    const auto& rect = chunk->own[1].fill_rect;
    if (!marker.view_id || marker.semantics.attribute_count || marker.semantics.title ||
        rect.color.c != color.c || rect.x != marker.marker_x || rect.y != marker.marker_y ||
        rect.w != marker.marker_w || rect.h != marker.marker_h ||
        rect.w != current.width || rect.h != current.height) return false;
    float dx = current.x - rect.x, dy = current.y - rect.y;
    // subtraction/addition can lose a small destination when the source position is very large.
    if (rect.x + dx != current.x || rect.y + dy != current.y) return false;
    return retained_dl_append_fragment_at(dst, fragment, dx, dy, true);
}
