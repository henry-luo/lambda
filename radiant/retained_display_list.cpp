#include "retained_display_list.hpp"

#include "display_list_bounds.hpp"
#include "display_list_storage.hpp"
#include "../lib/hashmap.h"
#include "../lib/mem_factory.h"
#include "../lib/hashmap_helpers.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/arena.h"

#include <math.h>
#include <string.h>

struct RetainedDisplayListFragment {
    uint32_t view_id;
    Bound bounds;
    Bound marker_bounds;
    DisplayList list;
    bool initialized;
    uint32_t last_stored_epoch;
};

typedef struct RetainedDisplayListEntry {
    uint32_t view_id;
    RetainedDisplayListFragment* fragment;
} RetainedDisplayListEntry;

struct RetainedDisplayListCache {
    Pool* pool;
    Arena* arena;
    HashMap* map;
    uint32_t epoch;
    RetainedDisplayListStats stats;
};

HASHMAP_DEFINE_INTKEY(retained_dl_entry, RetainedDisplayListEntry, view_id)

static void retained_dl_copy_clip_shape_stack(DisplayList* dst,
                                              DlClipShapeStack* out,
                                              const DlClipShapeStack* in) {
    if (!out) return;
    memset(out, 0, sizeof(DlClipShapeStack));
    if (!dst || !in || in->depth <= 0) return;

    *out = *in;
    for (int i = 0; i < out->depth && i < RDT_MAX_CLIP_SHAPES; i++) {
        if (in->type[i] != CLIP_SHAPE_POLYGON ||
            in->polygon_count[i] <= 0 ||
            !in->polygon_vx[i] || !in->polygon_vy[i]) {
            continue;
        }
        size_t sz = (size_t)in->polygon_count[i] * sizeof(float);
        float* vx = (float*)scratch_alloc(&dst->arena, sz);
        float* vy = (float*)scratch_alloc(&dst->arena, sz);
        if (!vx || !vy) {
            out->type[i] = CLIP_SHAPE_NONE;
            out->polygon_count[i] = 0;
            out->polygon_vx[i] = nullptr;
            out->polygon_vy[i] = nullptr;
            continue;
        }
        memcpy(vx, in->polygon_vx[i], sz);
        memcpy(vy, in->polygon_vy[i], sz);
        out->polygon_vx[i] = vx;
        out->polygon_vy[i] = vy;
    }
}

static bool retained_dl_clone_item_payload(DisplayList* dst,
                                           DisplayItem* out,
                                           const DisplayItem* in,
                                           int source_start,
                                           int dest_start) {
    if (!dst || !out || !in) return false;

    switch (in->op) {
        case DL_FILL_PATH:
            out->fill_path.path = in->fill_path.path ? rdt_path_clone(in->fill_path.path) : nullptr;
            if (in->fill_path.path && !out->fill_path.path) return false;
            break;
        case DL_STROKE_PATH:
            out->stroke_path.path = in->stroke_path.path ? rdt_path_clone(in->stroke_path.path) : nullptr;
            if (in->stroke_path.path && !out->stroke_path.path) return false;
            out->stroke_path.dash_array =
                dl_copy_dashes(dst, in->stroke_path.dash_array, in->stroke_path.dash_count);
            if (in->stroke_path.dash_array && in->stroke_path.dash_count > 0 &&
                !out->stroke_path.dash_array) {
                return false;
            }
            break;
        case DL_FILL_LINEAR_GRADIENT:
            out->fill_linear_gradient.path = in->fill_linear_gradient.path ?
                rdt_path_clone(in->fill_linear_gradient.path) : nullptr;
            if (in->fill_linear_gradient.path && !out->fill_linear_gradient.path) return false;
            out->fill_linear_gradient.stops =
                dl_copy_stops(dst, in->fill_linear_gradient.stops,
                              in->fill_linear_gradient.stop_count);
            if (in->fill_linear_gradient.stops && in->fill_linear_gradient.stop_count > 0 &&
                !out->fill_linear_gradient.stops) {
                return false;
            }
            break;
        case DL_FILL_RADIAL_GRADIENT:
            out->fill_radial_gradient.path = in->fill_radial_gradient.path ?
                rdt_path_clone(in->fill_radial_gradient.path) : nullptr;
            if (in->fill_radial_gradient.path && !out->fill_radial_gradient.path) return false;
            out->fill_radial_gradient.stops =
                dl_copy_stops(dst, in->fill_radial_gradient.stops,
                              in->fill_radial_gradient.stop_count);
            if (in->fill_radial_gradient.stops && in->fill_radial_gradient.stop_count > 0 &&
                !out->fill_radial_gradient.stops) {
                return false;
            }
            break;
        case DL_DRAW_PICTURE:
            out->draw_picture.picture = in->draw_picture.picture ?
                rdt_picture_dup(in->draw_picture.picture) : nullptr;
            if (in->draw_picture.picture && !out->draw_picture.picture) return false;
            break;
        case DL_PUSH_CLIP:
            out->push_clip.path = in->push_clip.path ? rdt_path_clone(in->push_clip.path) : nullptr;
            if (in->push_clip.path && !out->push_clip.path) return false;
            break;
        case DL_FILL_SURFACE_RECT:
            retained_dl_copy_clip_shape_stack(dst, &out->fill_surface_rect.clip_shapes,
                                              &in->fill_surface_rect.clip_shapes);
            break;
        case DL_BLIT_SURFACE_SCALED:
            retained_dl_copy_clip_shape_stack(dst, &out->blit_surface_scaled.clip_shapes,
                                              &in->blit_surface_scaled.clip_shapes);
            break;
        case DL_BEGIN_ELEMENT:
        case DL_END_ELEMENT:
            if (in->element_marker.matching_index >= source_start) {
                out->element_marker.matching_index =
                    dest_start + (in->element_marker.matching_index - source_start);
            }
            break;
        default:
            break;
    }
    return true;
}

static void retained_dl_rollback(DisplayList* dl, int target_count) {
    if (!dl || target_count < 0 || target_count > dl->count) return;
    for (int i = target_count; i < dl->count; i++) {
        dl_item_free_owned_payload(&dl->items[i]);
    }
    dl->count = target_count;
}

static bool retained_dl_copy_range(DisplayList* dst, const DisplayList* src,
                                   int start, int end) {
    if (!dst || !src || start < 0 || end < start || end >= src->count) return false;

    int dest_start = dst->count;
    for (int i = start; i <= end; i++) {
        DisplayItem* out = dl_alloc_item(dst);
        if (!out) {
            retained_dl_rollback(dst, dest_start);
            return false;
        }
        DisplayItem copy = src->items[i];
        *out = copy;
        if (!retained_dl_clone_item_payload(dst, out, &src->items[i], start, dest_start)) {
            retained_dl_rollback(dst, dest_start);
            return false;
        }
    }
    return true;
}

RetainedDisplayListCache* retained_dl_cache_create(Pool* pool) {
    if (!pool) return nullptr;

    RetainedDisplayListCache* cache =
        (RetainedDisplayListCache*)mem_calloc(1, sizeof(RetainedDisplayListCache), MEM_CAT_RENDER);
    if (!cache) return nullptr;

    cache->pool = pool;
    cache->arena = mem_arena_create(NULL, pool, MEM_ROLE_RENDER, "retained_dl.arena");
    cache->map = retained_dl_entry_new(128);
    if (!cache->arena || !cache->map) {
        retained_dl_cache_destroy(cache);
        return nullptr;
    }
    cache->epoch = 1;
    return cache;
}

void retained_dl_cache_destroy(RetainedDisplayListCache* cache) {
    if (!cache) return;

    if (cache->map) {
        size_t iter = 0;
        void* item = nullptr;
        while (hashmap_iter(cache->map, &iter, &item)) {
            RetainedDisplayListEntry* entry = (RetainedDisplayListEntry*)item;
            if (entry && entry->fragment) {
                dl_destroy(&entry->fragment->list);
                mem_free(entry->fragment);
            }
        }
        hashmap_free(cache->map);
        cache->map = nullptr;
    }
    if (cache->arena) {
        arena_destroy(cache->arena);
        cache->arena = nullptr;
    }
    mem_free(cache);
}

void retained_dl_cache_begin_frame(RetainedDisplayListCache* cache) {
    if (!cache) return;
    cache->epoch++;
    if (cache->epoch == 0) cache->epoch = 1;
    memset(&cache->stats, 0, sizeof(cache->stats));
}

RetainedDisplayListStats retained_dl_cache_stats(const RetainedDisplayListCache* cache) {
    RetainedDisplayListStats stats = {};
    if (!cache) return stats;
    return cache->stats;
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
    if (!cache || !cache->map || view_id == 0) return nullptr;

    RetainedDisplayListEntry query = { view_id, nullptr };
    RetainedDisplayListEntry* found =
        (RetainedDisplayListEntry*)hashmap_get(cache->map, &query);
    if (found) return found->fragment;

    RetainedDisplayListFragment* fragment =
        (RetainedDisplayListFragment*)mem_calloc(1, sizeof(RetainedDisplayListFragment), MEM_CAT_RENDER);
    if (!fragment) return nullptr;

    fragment->view_id = view_id;
    dl_init(&fragment->list, cache->arena);
    fragment->initialized = true;

    RetainedDisplayListEntry entry = { view_id, fragment };
    hashmap_set(cache->map, &entry);
    if (hashmap_oom(cache->map)) {
        dl_destroy(&fragment->list);
        mem_free(fragment);
        return nullptr;
    }
    return fragment;
}

static bool retained_dl_range_retainable(const DisplayList* source,
                                         int start, int end) {
    if (!source || start < 0 || end < start || end >= source->count) return false;
    for (int i = start; i <= end; i++) {
        if (!dl_item_is_retainable_for_fragment(&source->items[i])) {
            return false;
        }
    }
    return true;
}

static void retained_dl_cache_store_marker(RetainedDisplayListCache* cache,
                                           const DisplayList* source,
                                           int begin_index) {
    if (!cache || !source || begin_index < 0 || begin_index >= source->count) return;

    const DisplayItem* begin = &source->items[begin_index];
    if (begin->op != DL_BEGIN_ELEMENT) return;
    int end_index = begin->element_marker.matching_index;
    if (end_index <= begin_index || end_index >= source->count) return;

    uint32_t view_id = begin->element_marker.view_id;
    if (view_id == 0) return;
    cache->stats.capture_candidates++;

    RetainedDisplayListFragment* fragment =
        retained_dl_fragment_get_or_create(cache, view_id);
    if (!fragment || !fragment->initialized) return;

    dl_clear(&fragment->list);
    if (!retained_dl_range_retainable(source, begin_index, end_index)) {
        cache->stats.skipped_non_retainable++;
        log_debug("[RETAINED_DL] skipped non-retainable view %u", view_id);
        return;
    }
    if (!retained_dl_copy_range(&fragment->list, source, begin_index, end_index)) {
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

    for (int i = 0; i < source->count; i++) {
        const DisplayItem* item = &source->items[i];
        if (item->op != DL_BEGIN_ELEMENT || item->element_marker.matching_index <= i) {
            continue;
        }
        retained_dl_cache_store_marker(cache, source, i);
    }
}

const RetainedDisplayListFragment* retained_dl_cache_get(RetainedDisplayListCache* cache,
                                                         uint32_t view_id) {
    if (!cache || !cache->map || view_id == 0) return nullptr;
    RetainedDisplayListEntry query = { view_id, nullptr };
    RetainedDisplayListEntry* found =
        (RetainedDisplayListEntry*)hashmap_get(cache->map, &query);
    if (!found || !found->fragment || found->fragment->list.count <= 0) return nullptr;
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
    return fragment->list.count;
}

static bool retained_dl_bounds_match(Bound cached, Bound current) {
    float tolerance = 0.5f;
    return fabsf(cached.left - current.left) <= tolerance &&
           fabsf(cached.top - current.top) <= tolerance &&
           fabsf(cached.right - current.right) <= tolerance &&
           fabsf(cached.bottom - current.bottom) <= tolerance;
}

static bool retained_dl_bounds_intersect(Bound a, Bound b) {
    return !(a.left >= b.right || a.right <= b.left ||
             a.top >= b.bottom || a.bottom <= b.top);
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
    return retained_dl_bounds_intersect(visual_bound, dirty_bound);
}

bool retained_dl_fragment_resources_valid(const RetainedDisplayListFragment* fragment,
                                          uint64_t current_video_generation,
                                          uint64_t current_glyph_generation) {
    if (!fragment) return false;
    const DisplayList* list = &fragment->list;
    for (int i = 0; i < list->count; i++) {
        const DisplayItem* item = &list->items[i];
        switch (item->op) {
            case DL_DRAW_IMAGE: {
                if (!item->draw_image.pixels) break;
                ImageSurface* owner = (ImageSurface*)item->draw_image.resource_owner;
                if (!owner || item->draw_image.resource_generation == 0 ||
                    owner->generation != item->draw_image.resource_generation) {
                    return false;
                }
                break;
            }
            case DL_DRAW_GLYPH:
                if (item->draw_glyph.bitmap.buffer &&
                    (item->draw_glyph.resource_generation == 0 ||
                     item->draw_glyph.resource_generation != current_glyph_generation)) {
                    return false;
                }
                break;
            case DL_BLIT_SURFACE_SCALED: {
                ImageSurface* src = (ImageSurface*)item->blit_surface_scaled.src_surface;
                if (!src || item->blit_surface_scaled.src_generation == 0 ||
                    src->generation != item->blit_surface_scaled.src_generation) {
                    return false;
                }
                break;
            }
            case DL_VIDEO_PLACEHOLDER:
                if (item->video_placeholder.video &&
                    (item->video_placeholder.video_generation == 0 ||
                     item->video_placeholder.video_generation != current_video_generation)) {
                    return false;
                }
                break;
            case DL_WEBVIEW_LAYER_PLACEHOLDER: {
                ImageSurface* surface = (ImageSurface*)item->webview_layer_placeholder.surface;
                if (!surface || item->webview_layer_placeholder.surface_generation == 0 ||
                    surface->generation != item->webview_layer_placeholder.surface_generation) {
                    return false;
                }
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
    if (!dst || !fragment || fragment->list.count <= 0) return false;
    return retained_dl_copy_range(dst, &fragment->list, 0, fragment->list.count - 1);
}
