// ==========================================================================
// DisplayList storage and lifecycle helpers.
// ==========================================================================

#include "display_list.h"
#include "../lib/log.h"
#include "../lib/mem_factory.h"
#include "../lib/memtrack.h"
#include "../lib/checked_math.hpp"
#include "../lib/mem_grow.hpp"
#include <string.h>

#define DL_INITIAL_CAPACITY 2048
#define DL_VALIDATE_ELEMENT_STACK_LIMIT 1024

DisplayItem* dl_alloc_item(DisplayList* dl) {
    if (!dl) return nullptr;
    if (dl->count >= dl->capacity) {
        // keep list state intact on overflow/OOM so callers never see a half-grown buffer
        if (!lam::mem_grow_array(&dl->items, &dl->capacity, dl->count + 1,
                                 DL_INITIAL_CAPACITY, MEM_CAT_RENDER)) return nullptr;
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
    if (clip_depth > RDT_MAX_CLIP_SHAPES) {
        log_warn("[RAD_CAP_DL_CLIP_SHAPES] truncating clip stack from %d to %d shapes",
                 clip_depth, RDT_MAX_CLIP_SHAPES);
        clip_depth = RDT_MAX_CLIP_SHAPES;
    }
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
    if (depth > RDT_MAX_CLIP_SHAPES) {
        log_warn("[RAD_CAP_DL_CLIP_RESTORE] truncating stored clip stack from %d to %d shapes",
                 depth, RDT_MAX_CLIP_SHAPES);
        depth = RDT_MAX_CLIP_SHAPES;
    }
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
    mem_scratch_init(NULL, &dl->arena, backing_arena, MEM_ROLE_RENDER, "display_list.scratch");
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

static void dl_validation_set(DisplayListValidationResult* result, bool valid,
                              int index, const char* message, int clip_depth,
                              int backdrop_depth, int shadow_clip_depth,
                              int element_depth) {
    if (!result) return;
    result->valid = valid;
    result->first_error_index = index;
    result->message = message;
    result->clip_depth = clip_depth;
    result->backdrop_depth = backdrop_depth;
    result->shadow_clip_depth = shadow_clip_depth;
    result->element_depth = element_depth;
}

static bool dl_validation_fail(DisplayListValidationResult* result, int index,
                               const char* message, int clip_depth,
                               int backdrop_depth, int shadow_clip_depth,
                               int element_depth) {
    dl_validation_set(result, false, index, message, clip_depth, backdrop_depth,
                      shadow_clip_depth, element_depth);
    return false;
}

static bool dl_validate_positive_image(const DlDrawImage* image) {
    return image->pixels && image->src_w > 0 && image->src_h > 0 &&
           image->src_stride > 0 && image->dst_w >= 0.0f && image->dst_h >= 0.0f;
}

bool dl_validate(const DisplayList* dl, DisplayListValidationResult* result) {
    dl_validation_set(result, true, -1, "ok", 0, 0, 0, 0);
    if (!dl) {
        return dl_validation_fail(result, -1, "display list is null", 0, 0, 0, 0);
    }
    if (dl->count < 0 || dl->capacity < 0 || dl->count > dl->capacity) {
        return dl_validation_fail(result, -1, "display list count/capacity is invalid",
                                  0, 0, 0, 0);
    }
    if (dl->count > 0 && !dl->items) {
        return dl_validation_fail(result, -1, "display list has items but no storage",
                                  0, 0, 0, 0);
    }

    int clip_depth = 0;
    int backdrop_depth = 0;
    int shadow_clip_depth = 0;
    int element_depth = 0;
    int element_stack[DL_VALIDATE_ELEMENT_STACK_LIMIT];

    for (int i = 0; i < dl->count; i++) {
        const DisplayItem* item = &dl->items[i];
        if (item->op < DL_FILL_RECT || item->op > DL_END_ELEMENT) {
            return dl_validation_fail(result, i, "unknown display op",
                                      clip_depth, backdrop_depth, shadow_clip_depth,
                                      element_depth);
        }

        switch (item->op) {
            case DL_FILL_RECT:
                if (item->fill_rect.w < 0.0f || item->fill_rect.h < 0.0f) {
                    return dl_validation_fail(result, i, "fill rect has negative size",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                break;
            case DL_FILL_ROUNDED_RECT:
                if (item->fill_rounded_rect.w < 0.0f || item->fill_rounded_rect.h < 0.0f ||
                    item->fill_rounded_rect.rx < 0.0f || item->fill_rounded_rect.ry < 0.0f) {
                    return dl_validation_fail(result, i, "rounded rect has negative size",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                break;
            case DL_FILL_PATH:
                if (!item->fill_path.path) {
                    return dl_validation_fail(result, i, "fill path is null",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                break;
            case DL_STROKE_PATH:
                if (!item->stroke_path.path || item->stroke_path.width < 0.0f ||
                    item->stroke_path.dash_count < 0 ||
                    (item->stroke_path.dash_count > 0 && !item->stroke_path.dash_array)) {
                    return dl_validation_fail(result, i, "stroke path payload is invalid",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                break;
            case DL_FILL_LINEAR_GRADIENT:
                if (!item->fill_linear_gradient.path || !item->fill_linear_gradient.stops ||
                    item->fill_linear_gradient.stop_count <= 0) {
                    return dl_validation_fail(result, i, "linear gradient payload is invalid",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                break;
            case DL_FILL_RADIAL_GRADIENT:
                if (!item->fill_radial_gradient.path || !item->fill_radial_gradient.stops ||
                    item->fill_radial_gradient.stop_count <= 0 ||
                    item->fill_radial_gradient.r < 0.0f) {
                    return dl_validation_fail(result, i, "radial gradient payload is invalid",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                break;
            case DL_DRAW_IMAGE:
                if (!dl_validate_positive_image(&item->draw_image)) {
                    return dl_validation_fail(result, i, "draw image payload is invalid",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                break;
            case DL_DRAW_GLYPH:
                if (!item->draw_glyph.bitmap.buffer || item->draw_glyph.bitmap.width <= 0 ||
                    item->draw_glyph.bitmap.height <= 0 || item->draw_glyph.bitmap.pitch <= 0) {
                    return dl_validation_fail(result, i, "glyph payload is invalid",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                break;
            case DL_DRAW_PICTURE:
                if (!item->draw_picture.picture) {
                    return dl_validation_fail(result, i, "picture payload is invalid",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                break;
            case DL_PUSH_CLIP:
                if (!item->push_clip.path) {
                    return dl_validation_fail(result, i, "clip path is null",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                clip_depth++;
                break;
            case DL_POP_CLIP:
                if (clip_depth <= 0) {
                    return dl_validation_fail(result, i, "clip pop without matching push",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                clip_depth--;
                break;
            case DL_FILL_SURFACE_RECT:
                if (item->fill_surface_rect.w < 0.0f || item->fill_surface_rect.h < 0.0f) {
                    return dl_validation_fail(result, i, "surface fill has negative size",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                break;
            case DL_BLIT_SURFACE_SCALED:
                if (!item->blit_surface_scaled.src_surface ||
                    item->blit_surface_scaled.dst_w < 0.0f ||
                    item->blit_surface_scaled.dst_h < 0.0f) {
                    return dl_validation_fail(result, i, "surface blit payload is invalid",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                break;
            case DL_COMPOSITE_OPACITY:
                if (backdrop_depth <= 0) {
                    return dl_validation_fail(result, i, "opacity composite without saved backdrop",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                if (item->composite_opacity.w < 0 || item->composite_opacity.h < 0) {
                    return dl_validation_fail(result, i, "opacity composite has negative region",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                backdrop_depth--;
                break;
            case DL_SAVE_BACKDROP:
                if (item->save_backdrop.w < 0 || item->save_backdrop.h < 0) {
                    return dl_validation_fail(result, i, "saved backdrop has negative region",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                backdrop_depth++;
                break;
            case DL_APPLY_BLEND_MODE:
                if (backdrop_depth <= 0) {
                    return dl_validation_fail(result, i, "blend mode without saved backdrop",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                if (item->apply_blend_mode.w < 0 || item->apply_blend_mode.h < 0) {
                    return dl_validation_fail(result, i, "blend mode has negative region",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                backdrop_depth--;
                break;
            case DL_APPLY_FILTER:
                if (!item->apply_filter.filter ||
                    item->apply_filter.w < 0.0f || item->apply_filter.h < 0.0f) {
                    return dl_validation_fail(result, i, "filter payload is invalid",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                break;
            case DL_BOX_BLUR_REGION:
                if (item->box_blur_region.rw < 0 || item->box_blur_region.rh < 0 ||
                    item->box_blur_region.blur_radius < 0.0f) {
                    return dl_validation_fail(result, i, "box blur region payload is invalid",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                break;
            case DL_BOX_BLUR_INSET:
                if (item->box_blur_inset.rw < 0 || item->box_blur_inset.rh < 0 ||
                    item->box_blur_inset.pad < 0 ||
                    item->box_blur_inset.blur_radius < 0.0f) {
                    return dl_validation_fail(result, i, "inset blur payload is invalid",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                break;
            case DL_SHADOW_CLIP_SAVE:
                if (item->shadow_clip_save.rw < 0 || item->shadow_clip_save.rh < 0) {
                    return dl_validation_fail(result, i, "shadow clip save has negative region",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                shadow_clip_depth++;
                break;
            case DL_SHADOW_CLIP_RESTORE:
                if (shadow_clip_depth <= 0) {
                    return dl_validation_fail(result, i, "shadow clip restore without save",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                if (item->shadow_clip_restore.save_rw < 0 ||
                    item->shadow_clip_restore.save_rh < 0) {
                    return dl_validation_fail(result, i, "shadow clip restore has negative region",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                shadow_clip_depth--;
                break;
            case DL_OUTER_SHADOW:
                if (item->outer_shadow.shadow_w < 0.0f || item->outer_shadow.shadow_h < 0.0f ||
                    item->outer_shadow.blur_radius < 0.0f) {
                    return dl_validation_fail(result, i, "outer shadow payload is invalid",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                break;
            case DL_VIDEO_PLACEHOLDER:
                if (!item->video_placeholder.video ||
                    item->video_placeholder.dst_w < 0.0f ||
                    item->video_placeholder.dst_h < 0.0f) {
                    return dl_validation_fail(result, i, "video placeholder payload is invalid",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                break;
            case DL_WEBVIEW_LAYER_PLACEHOLDER:
                if (!item->webview_layer_placeholder.surface ||
                    item->webview_layer_placeholder.dst_w < 0.0f ||
                    item->webview_layer_placeholder.dst_h < 0.0f) {
                    return dl_validation_fail(result, i, "webview placeholder payload is invalid",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                break;
            case DL_BEGIN_ELEMENT: {
                int match = item->element_marker.matching_index;
                if (match <= i || match >= dl->count ||
                    dl->items[match].op != DL_END_ELEMENT ||
                    dl->items[match].element_marker.matching_index != i) {
                    return dl_validation_fail(result, i, "element begin marker is not paired",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                if (element_depth >= DL_VALIDATE_ELEMENT_STACK_LIMIT) {
                    return dl_validation_fail(result, i, "element marker nesting is too deep",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                element_stack[element_depth++] = i;
                break;
            }
            case DL_END_ELEMENT: {
                int match = item->element_marker.matching_index;
                if (element_depth <= 0 || match < 0 || match >= i ||
                    dl->items[match].op != DL_BEGIN_ELEMENT ||
                    dl->items[match].element_marker.matching_index != i ||
                    element_stack[element_depth - 1] != match) {
                    return dl_validation_fail(result, i, "element end marker is not paired",
                                              clip_depth, backdrop_depth, shadow_clip_depth,
                                              element_depth);
                }
                element_depth--;
                break;
            }
        }
    }

    if (clip_depth != 0) {
        return dl_validation_fail(result, dl->count, "clip stack is unbalanced",
                                  clip_depth, backdrop_depth, shadow_clip_depth,
                                  element_depth);
    }
    if (backdrop_depth != 0) {
        return dl_validation_fail(result, dl->count, "backdrop stack is unbalanced",
                                  clip_depth, backdrop_depth, shadow_clip_depth,
                                  element_depth);
    }
    if (shadow_clip_depth != 0) {
        return dl_validation_fail(result, dl->count, "shadow clip stack is unbalanced",
                                  clip_depth, backdrop_depth, shadow_clip_depth,
                                  element_depth);
    }
    if (element_depth != 0) {
        return dl_validation_fail(result, dl->count, "element marker stack is unbalanced",
                                  clip_depth, backdrop_depth, shadow_clip_depth,
                                  element_depth);
    }

    dl_validation_set(result, true, -1, "ok", 0, 0, 0, 0);
    return true;
}

bool dl_validate_or_log(const DisplayList* dl, const char* context) {
    DisplayListValidationResult result = {};
    if (dl_validate(dl, &result)) return true;
    log_error("[DL_VALIDATE] %s: invalid display list at item %d: %s (clip=%d backdrop=%d shadow_clip=%d element=%d)",
              context ? context : "unknown",
              result.first_error_index,
              result.message ? result.message : "invalid",
              result.clip_depth,
              result.backdrop_depth,
              result.shadow_clip_depth,
              result.element_depth);
    return false;
}

bool dl_item_is_retainable_for_fragment(const DisplayItem* item) {
    if (!item) return false;

    switch (item->op) {
        case DL_DRAW_IMAGE:
            if (item->draw_image.pixels &&
                (!item->draw_image.resource_owner ||
                 item->draw_image.resource_generation == 0)) {
                return false;
            }
            break;
        case DL_DRAW_GLYPH:
            if (item->draw_glyph.bitmap.buffer &&
                item->draw_glyph.resource_generation == 0) {
                return false;
            }
            break;
        case DL_BLIT_SURFACE_SCALED:
            if (item->blit_surface_scaled.src_surface &&
                item->blit_surface_scaled.src_generation == 0) {
                return false;
            }
            break;
        case DL_VIDEO_PLACEHOLDER:
            if (item->video_placeholder.video &&
                item->video_placeholder.video_generation == 0) {
                return false;
            }
            break;
        case DL_WEBVIEW_LAYER_PLACEHOLDER:
            if (item->webview_layer_placeholder.surface &&
                item->webview_layer_placeholder.surface_generation == 0) {
                return false;
            }
            break;
        case DL_APPLY_FILTER:
            if (item->apply_filter.filter) {
                return false;
            }
            break;
        default:
            break;
    }
    return true;
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
