#include "retained_display_list.hpp"

#include "display_list_storage.hpp"
#include "../lib/hashmap.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/arena.h"

#include <string.h>

struct RetainedDisplayListFragment {
    uint32_t view_id;
    Bound bounds;
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
};

static uint64_t retained_dl_entry_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const RetainedDisplayListEntry* entry = (const RetainedDisplayListEntry*)item;
    return hashmap_murmur(&entry->view_id, sizeof(uint32_t), seed0, seed1);
}

static int retained_dl_entry_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const RetainedDisplayListEntry* ea = (const RetainedDisplayListEntry*)a;
    const RetainedDisplayListEntry* eb = (const RetainedDisplayListEntry*)b;
    if (ea->view_id == eb->view_id) return 0;
    return ea->view_id < eb->view_id ? -1 : 1;
}

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

static void retained_dl_free_item_payload(DisplayItem* item) {
    if (!item) return;
    switch (item->op) {
        case DL_FILL_PATH:
            rdt_path_free(item->fill_path.path);
            item->fill_path.path = nullptr;
            break;
        case DL_STROKE_PATH:
            rdt_path_free(item->stroke_path.path);
            item->stroke_path.path = nullptr;
            break;
        case DL_FILL_LINEAR_GRADIENT:
            rdt_path_free(item->fill_linear_gradient.path);
            item->fill_linear_gradient.path = nullptr;
            break;
        case DL_FILL_RADIAL_GRADIENT:
            rdt_path_free(item->fill_radial_gradient.path);
            item->fill_radial_gradient.path = nullptr;
            break;
        case DL_DRAW_PICTURE:
            rdt_picture_free(item->draw_picture.picture);
            item->draw_picture.picture = nullptr;
            break;
        case DL_PUSH_CLIP:
            rdt_path_free(item->push_clip.path);
            item->push_clip.path = nullptr;
            break;
        default:
            break;
    }
}

static void retained_dl_rollback(DisplayList* dl, int target_count) {
    if (!dl || target_count < 0 || target_count > dl->count) return;
    for (int i = target_count; i < dl->count; i++) {
        retained_dl_free_item_payload(&dl->items[i]);
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
    cache->arena = arena_create_default(pool);
    cache->map = hashmap_new(sizeof(RetainedDisplayListEntry), 128,
                             0x72646c31, 0x72646c32,
                             retained_dl_entry_hash,
                             retained_dl_entry_compare,
                             NULL, NULL);
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

    RetainedDisplayListFragment* fragment =
        retained_dl_fragment_get_or_create(cache, view_id);
    if (!fragment || !fragment->initialized) return;

    dl_clear(&fragment->list);
    if (!retained_dl_copy_range(&fragment->list, source, begin_index, end_index)) {
        dl_clear(&fragment->list);
        return;
    }

    fragment->bounds = dl_item_bounds(begin);
    fragment->last_stored_epoch = cache->epoch;
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

int retained_dl_fragment_item_count(const RetainedDisplayListFragment* fragment) {
    if (!fragment) return 0;
    return fragment->list.count;
}

bool retained_dl_fragment_resources_valid(const RetainedDisplayListFragment* fragment,
                                          uint64_t current_video_generation) {
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
                // Glyph bitmap buffers are borrowed from the font cache. Until
                // the font cache exposes an eviction generation, text fragments
                // must be re-recorded instead of retained across frames.
                if (item->draw_glyph.bitmap.buffer) return false;
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

bool retained_dl_append_fragment(DisplayList* dst,
                                 const RetainedDisplayListFragment* fragment) {
    if (!dst || !fragment || fragment->list.count <= 0) return false;
    return retained_dl_copy_range(dst, &fragment->list, 0, fragment->list.count - 1);
}
