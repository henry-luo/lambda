// ==========================================================================
// DisplayList storage and lifecycle helpers.
// ==========================================================================

#include "display_list.h"
#include "../lib/memtrack.h"
#include <string.h>

#define DL_INITIAL_CAPACITY 2048

DisplayItem* dl_alloc_item(DisplayList* dl) {
    if (!dl) return nullptr;
    if (dl->count >= dl->capacity) {
        int new_cap = dl->capacity ? dl->capacity * 2 : DL_INITIAL_CAPACITY;
        dl->items = (DisplayItem*)mem_realloc(dl->items, new_cap * sizeof(DisplayItem), MEM_CAT_RENDER);
        dl->capacity = new_cap;
    }
    DisplayItem* item = &dl->items[dl->count++];
    memset(item, 0, sizeof(DisplayItem));
    return item;
}

RdtGradientStop* dl_copy_stops(DisplayList* dl, const RdtGradientStop* stops, int count) {
    if (!dl || !stops || count <= 0) return nullptr;
    size_t sz = count * sizeof(RdtGradientStop);
    RdtGradientStop* copy = (RdtGradientStop*)scratch_alloc(&dl->arena, sz);
    memcpy(copy, stops, sz);
    return copy;
}

float* dl_copy_dashes(DisplayList* dl, const float* dashes, int count) {
    if (!dl || !dashes || count <= 0) return nullptr;
    size_t sz = count * sizeof(float);
    float* copy = (float*)scratch_alloc(&dl->arena, sz);
    memcpy(copy, dashes, sz);
    return copy;
}

void dl_store_clip_shapes(DisplayList* dl, DlClipShapeStack* dst,
                          ClipShape** clip_shapes, int clip_depth) {
    if (!dst) return;
    memset(dst, 0, sizeof(DlClipShapeStack));
    if (!dl || !clip_shapes || clip_depth <= 0) return;
    if (clip_depth > RDT_MAX_CLIP_SHAPES) clip_depth = RDT_MAX_CLIP_SHAPES;
    dst->depth = clip_depth;
    for (int i = 0; i < clip_depth; i++) {
        ClipShape* shape = clip_shapes[i];
        if (shape && shape->type == CLIP_SHAPE_POLYGON &&
            shape->polygon.count > 0 && shape->polygon.vx && shape->polygon.vy) {
            int count = shape->polygon.count;
            size_t sz = count * sizeof(float);
            float* vx = (float*)scratch_alloc(&dl->arena, sz);
            float* vy = (float*)scratch_alloc(&dl->arena, sz);
            if (!vx || !vy) {
                dst->type[i] = CLIP_SHAPE_NONE;
                continue;
            }
            memcpy(vx, shape->polygon.vx, sz);
            memcpy(vy, shape->polygon.vy, sz);
            dst->type[i] = CLIP_SHAPE_POLYGON;
            dst->polygon_count[i] = count;
            dst->polygon_vx[i] = vx;
            dst->polygon_vy[i] = vy;
            continue;
        }
        clip_shape_to_params(shape, &dst->type[i], dst->params[i]);
    }
}

int dl_restore_clip_shapes(const DlClipShapeStack* src, ClipShape* shapes,
                           ClipShape** shape_ptrs) {
    if (!src || !shapes || !shape_ptrs || src->depth <= 0) return 0;
    int depth = src->depth;
    if (depth > RDT_MAX_CLIP_SHAPES) depth = RDT_MAX_CLIP_SHAPES;
    int out_depth = 0;
    for (int i = 0; i < depth; i++) {
        if (src->type[i] == CLIP_SHAPE_NONE) continue;
        if (src->type[i] == CLIP_SHAPE_POLYGON) {
            if (src->polygon_count[i] < 3 || !src->polygon_vx[i] || !src->polygon_vy[i]) continue;
            shapes[out_depth].type = CLIP_SHAPE_POLYGON;
            shapes[out_depth].polygon = {src->polygon_vx[i], src->polygon_vy[i], src->polygon_count[i]};
        } else {
            shapes[out_depth] = clip_shape_from_params(src->type[i], src->params[i]);
        }
        shape_ptrs[out_depth] = &shapes[out_depth];
        out_depth++;
    }
    return out_depth;
}

void dl_init(DisplayList* dl, Arena* backing_arena) {
    memset(dl, 0, sizeof(DisplayList));
    scratch_init(&dl->arena, backing_arena);
}

void dl_clear(DisplayList* dl) {
    if (!dl) return;

    for (int i = 0; i < dl->count; i++) {
        DisplayItem* item = &dl->items[i];
        switch (item->op) {
            case DL_FILL_PATH:
                rdt_path_free(item->fill_path.path);
                break;
            case DL_STROKE_PATH:
                rdt_path_free(item->stroke_path.path);
                break;
            case DL_FILL_LINEAR_GRADIENT:
                rdt_path_free(item->fill_linear_gradient.path);
                break;
            case DL_FILL_RADIAL_GRADIENT:
                rdt_path_free(item->fill_radial_gradient.path);
                break;
            case DL_PUSH_CLIP:
                rdt_path_free(item->push_clip.path);
                break;
            case DL_DRAW_PICTURE:
                rdt_picture_free(item->draw_picture.picture);
                break;
            default:
                break;
        }
    }
    dl->count = 0;
}

void dl_destroy(DisplayList* dl) {
    if (!dl) return;

    dl_clear(dl);
    if (dl->items) {
        mem_free(dl->items);
        dl->items = nullptr;
    }
    dl->capacity = 0;
    scratch_release(&dl->arena);
}

int dl_item_count(const DisplayList* dl) {
    return dl ? dl->count : 0;
}

bool dl_contains_glyphs(const DisplayList* dl) {
    if (!dl) return false;
    for (int i = 0; i < dl->count; i++) {
        if (dl->items[i].op == DL_DRAW_GLYPH) return true;
    }
    return false;
}
