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
    scratch_release(&dl->arena);
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

static void dl_set_item_rect_bounds(DisplayItem* item,
                                    float x, float y, float w, float h) {
    if (!item) return;
    item->bounds[0] = x;
    item->bounds[1] = y;
    item->bounds[2] = w > 0.0f ? w : 0.0f;
    item->bounds[3] = h > 0.0f ? h : 0.0f;
}

static bool dl_union_item_bounds(float* left, float* top,
                                 float* right, float* bottom,
                                 const DisplayItem* item,
                                 bool has_bounds) {
    if (!item) return has_bounds;
    float w = item->bounds[2];
    float h = item->bounds[3];
    if (w <= 0.0f || h <= 0.0f) return has_bounds;
    float l = item->bounds[0];
    float t = item->bounds[1];
    float r = l + w;
    float b = t + h;
    if (!has_bounds) {
        *left = l;
        *top = t;
        *right = r;
        *bottom = b;
        return true;
    }
    if (l < *left) *left = l;
    if (t < *top) *top = t;
    if (r > *right) *right = r;
    if (b > *bottom) *bottom = b;
    return true;
}

int dl_begin_element(DisplayList* dl, uint32_t view_id,
                     float x, float y, float w, float h) {
    DisplayItem* item = dl_alloc_item(dl);
    if (!item) return -1;
    int index = dl->count - 1;
    item->op = DL_BEGIN_ELEMENT;
    dl_set_item_rect_bounds(item, x, y, w, h);
    item->element_marker.view_id = view_id;
    item->element_marker.matching_index = -1;
    item->element_marker.marker_x = x;
    item->element_marker.marker_y = y;
    item->element_marker.marker_w = w;
    item->element_marker.marker_h = h;
    return index;
}

void dl_end_element(DisplayList* dl, int begin_index) {
    DisplayItem* end = dl_alloc_item(dl);
    if (!end) return;
    int end_index = dl->count - 1;
    end->op = DL_END_ELEMENT;
    end->element_marker.view_id = 0;
    end->element_marker.matching_index = begin_index;

    if (!dl || begin_index < 0 || begin_index >= end_index) {
        return;
    }

    DisplayItem* begin = &dl->items[begin_index];
    if (begin->op != DL_BEGIN_ELEMENT) {
        return;
    }

    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
    bool has_bounds = false;
    has_bounds = dl_union_item_bounds(&left, &top, &right, &bottom, begin, has_bounds);
    for (int i = begin_index + 1; i < end_index; i++) {
        has_bounds = dl_union_item_bounds(&left, &top, &right, &bottom,
                                          &dl->items[i], has_bounds);
    }
    if (has_bounds) {
        dl_set_item_rect_bounds(begin, left, top, right - left, bottom - top);
        dl_set_item_rect_bounds(end, left, top, right - left, bottom - top);
    }
    begin->element_marker.matching_index = end_index;
    end->element_marker.view_id = begin->element_marker.view_id;
    end->element_marker.marker_x = begin->element_marker.marker_x;
    end->element_marker.marker_y = begin->element_marker.marker_y;
    end->element_marker.marker_w = begin->element_marker.marker_w;
    end->element_marker.marker_h = begin->element_marker.marker_h;
}
