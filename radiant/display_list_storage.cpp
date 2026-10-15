// ==========================================================================
// DisplayList storage and lifecycle helpers.
// ==========================================================================

#include "render.hpp"
#include "../lib/log.h"
#include "../lib/mem_factory.h"
#include "../lib/memtrack.h"
#include "../lib/math_checked.hpp"
#include <string.h>
#include <limits.h>
#include <math.h>
#include "../lib/generation.h"

#define DL_INITIAL_CAPACITY 2048
#define DL_VALIDATE_ELEMENT_STACK_LIMIT 1024

static const DisplayOpDescriptor DISPLAY_OP_DESCRIPTORS[] = {
#define DL_OP_DESCRIPTOR(name, flags, owned_kind, owned_offset) \
    { name, flags, owned_kind, owned_offset },
    DISPLAY_OP_LIST(DL_OP_DESCRIPTOR)
#undef DL_OP_DESCRIPTOR
};
static_assert(sizeof(DISPLAY_OP_DESCRIPTORS) / sizeof(DISPLAY_OP_DESCRIPTORS[0]) == DL_OP_COUNT,
              "DisplayOp descriptor table must match DISPLAY_OP_LIST");

const DisplayOpDescriptor* dl_op_descriptor(DisplayOp op) {
    if (op < DL_FILL_RECT || op >= DL_OP_COUNT) return nullptr;
    return &DISPLAY_OP_DESCRIPTORS[(int)op];
}

bool dl_op_has_flags(DisplayOp op, uint32_t flags) {
    const DisplayOpDescriptor* desc = dl_op_descriptor(op);
    return desc && (desc->flags & flags) == flags;
}

DisplayItem* dl_alloc_item(DisplayList* dl) {
    if (!dl) return nullptr;
    if (dl->capacity() == 0 && !dl->reserve(DL_INITIAL_CAPACITY)) return nullptr;
    DisplayItem item = {};
    if (!dl->append(item)) return nullptr;
    return &dl->back();
}

RdtGradientStop* dl_copy_stops(DisplayList* dl, const RdtGradientStop* stops, int count) {
    if (!dl || !stops || count <= 0) return nullptr;
    size_t sz = count * sizeof(RdtGradientStop);
    RdtGradientStop* copy = (RdtGradientStop*)scratch_alloc(&dl->arena, sz);
    if (!copy) return nullptr;
    memcpy(copy, stops, sz);
    return copy;
}

float* dl_copy_dashes(DisplayList* dl, const float* dashes, int count) {
    if (!dl || !dashes || count <= 0) return nullptr;
    size_t sz = count * sizeof(float);
    float* copy = (float*)scratch_alloc(&dl->arena, sz);
    if (!copy) return nullptr;
    memcpy(copy, dashes, sz);
    return copy;
}

bool dl_copy_semantic_group(DisplayList* dl, RenderSemanticGroup* out,
                            const RenderSemanticGroup* source) {
    if (!dl || !out || !source || source->attribute_count < 0 ||
        (source->attribute_count && !source->attributes)) return false;
    RenderSemanticGroup copy = {};
    auto copy_string = [&](const char* value) -> const char* {
        if (!value) return nullptr;
        size_t bytes = strlen(value) + 1;
        char* result = (char*)scratch_alloc(&dl->arena, bytes);
        if (result) memcpy(result, value, bytes);
        return result;
    };
    if (source->attribute_count) {
        RenderSemanticAttribute* attrs = (RenderSemanticAttribute*)scratch_calloc(
            &dl->arena, (size_t)source->attribute_count * sizeof(RenderSemanticAttribute));
        if (!attrs) return false;
        for (int i = 0; i < source->attribute_count; i++) {
            attrs[i] = {lam::up(copy_string(source->attributes[i].name)), lam::up(copy_string(source->attributes[i].value))};
            if (!attrs[i].name || !attrs[i].value) return false;
        }
        copy.attributes = lam::up(attrs);
        copy.attribute_count = source->attribute_count;
    }
    copy.title = lam::up(copy_string(source->title));
    if (source->title && !copy.title) return false;
    *out = copy;
    return true;
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
        clip_shape_to_params(shape, &dst->type[i], dst->params[i]);
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
            dst->polygon_vx[i] = lam::own_arr(vx);
            dst->polygon_vy[i] = lam::own_arr(vy);
            continue;
        }
    }
}


int dl_restore_clip_shapes(const DlClipShapeStack* src, ClipShape* shapes,
                           ClipShape** shape_ptrs, ScratchArena* scratch,
                           float offset_x, float offset_y) {
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
        shapes[out_depth] = clip_shape_from_params(src->type[i], src->params[i]);
        if (src->type[i] == CLIP_SHAPE_POLYGON) {
            int count = src->polygon_count[i];
            if (count < 3 || !src->polygon_vx[i] || !src->polygon_vy[i]) continue;
            if ((offset_x != 0.0f || offset_y != 0.0f) && !scratch) continue;
            float* vx = src->polygon_vx[i];
            float* vy = src->polygon_vy[i];
            if (scratch) {
                vx = (float*)scratch_alloc(scratch, count * sizeof(float));
                vy = (float*)scratch_alloc(scratch, count * sizeof(float));
                if (!vx || !vy) continue;
                for (int pi = 0; pi < count; pi++) {
                    vx[pi] = src->polygon_vx[i][pi] - (shapes[out_depth].transformed ? 0.0f : offset_x);
                    vy[pi] = src->polygon_vy[i][pi] - (shapes[out_depth].transformed ? 0.0f : offset_y);
                }
            }
            shapes[out_depth].type = CLIP_SHAPE_POLYGON;
            shapes[out_depth].polygon = {lam::own_arr(vx), lam::own_arr(vy), count};
        }
        clip_shape_offset(&shapes[out_depth], offset_x, offset_y);
        shape_ptrs[out_depth] = &shapes[out_depth];
        out_depth++;
    }
    return out_depth;
}

void DisplayList::init(Arena* backing_arena) {
    destroy();
    mem_scratch_init(NULL, &arena, backing_arena, MEM_ROLE_RENDER, "display_list.scratch");
}

void dl_item_free_owned_payload(DisplayItem* item) {
    if (!item) return;
    if (item->op == DL_DRAW_IMAGE && item->draw_image.snapshot_lease) {
        image_surface_snapshot_release(item->draw_image.snapshot_lease);item->draw_image.snapshot_lease=nullptr;
    }
    if (item->op == DL_BLIT_SURFACE_SCALED && item->blit_surface_scaled.snapshot_lease) {
        image_surface_snapshot_release(item->blit_surface_scaled.snapshot_lease);
        item->blit_surface_scaled.snapshot_lease = nullptr;
    }
    const DisplayOpDescriptor* descriptor = dl_op_descriptor(item->op);
    if (!descriptor || descriptor->owned_payload_kind == DL_OWNED_PAYLOAD_NONE) return;
    // Ownership kind and offset live in the op row so new owned payloads cannot omit cleanup.
    void* field = (char*)item + descriptor->owned_payload_offset;
    if (descriptor->owned_payload_kind == DL_OWNED_PAYLOAD_PATH) {
        RdtPath** path = (RdtPath**)field;
        if (*path) { rdt_path_free(*path); *path = nullptr; }
    } else if (descriptor->owned_payload_kind == DL_OWNED_PAYLOAD_PICTURE) {
        RdtPicture** picture = (RdtPicture**)field;
        if (*picture) { rdt_picture_free(*picture); *picture = nullptr; }
    }
}

void DisplayList::clear() {
    DisplayList* dl = this;

    for (size_t i = 0; i < dl->size(); i++) {
        dl_item_free_owned_payload(&dl->data()[i]);
    }
    lam::ArrayList<DisplayItem>::clear();
    Arena* backing_arena = dl->arena.arena;
    scratch_release(&dl->arena);
    if (backing_arena) {
        // reusable display lists release scratch nodes on clear; re-register so
        // retained-fragment captures keep mem-dump accounting current.
        mem_scratch_init(NULL, &dl->arena, backing_arena, MEM_ROLE_RENDER,
                         "display_list.scratch");
    }
}

void DisplayList::destroy() {
    clear();
    lam::ArrayList<DisplayItem>::release();
    scratch_release(&arena);
}

int DisplayList::item_count() const {
    if (size() > (size_t)INT_MAX) {
        log_error("display_list_item_count_overflow: count=%zu", size());
        return INT_MAX;
    }
    return (int)size(); // INT_CAST_OK: DisplayList public count API is int.
}

bool DisplayList::contains_glyphs() const {
    for (size_t i = 0; i < size(); i++) {
        const DisplayItem& item = data()[i];
        if (item.op == DL_DRAW_GLYPH) return true;
        // ThorVG text replays by re-shaping each duplicate through its font
        // loader's shared glyph cache, which tile workers must not race on.
        if (item.op == DL_DRAW_PICTURE && rdt_picture_is_text(item.draw_picture.picture)) return true;
    }
    return false;
}

void dl_init(DisplayList* dl, Arena* backing_arena) {
    if (dl) dl->init(backing_arena);
}

void dl_clear(DisplayList* dl) {
    if (dl) dl->clear();
}

void dl_destroy(DisplayList* dl) {
    if (dl) dl->destroy();
}

int dl_item_count(const DisplayList* dl) {
    return dl ? dl->item_count() : 0;
}

bool dl_contains_glyphs(const DisplayList* dl) {
    return dl ? dl->contains_glyphs() : false;
}

static void dl_validation_set(DisplayListValidationResult* result, bool valid,
                              int index, const char* message, int clip_depth,
                              int backdrop_depth, int shadow_clip_depth,
                              int element_depth) {
    if (!result) return;
    result->valid = valid;
    result->first_error_index = index;
    result->message = lam::up(message);
    result->clip_depth = clip_depth;
    result->backdrop_depth = backdrop_depth;
    result->shadow_clip_depth = shadow_clip_depth;
    result->element_depth = element_depth;
}

typedef struct {
    int clip_depth;
    int backdrop_depth;
    int shadow_clip_depth;
    int element_depth;
} DisplayListValidationStack;

static bool dl_validation_fail(DisplayListValidationResult* result, int index,
                               const char* message,
                               const DisplayListValidationStack* stack) {
    DisplayListValidationStack empty = {};
    const DisplayListValidationStack* s = stack ? stack : &empty;
    dl_validation_set(result, false, index, message, s->clip_depth, s->backdrop_depth,
                      s->shadow_clip_depth, s->element_depth);
    return false;
}

static bool dl_validate_positive_image(const DlDrawImage* image) {
    return (image->local_pixels || !image->resource.is_null()) && image->src_w > 0 && image->src_h > 0 &&
           image->src_stride > 0 && image->dst_w >= 0.0f && image->dst_h >= 0.0f;
}

static bool dl_has_negative_size(float w, float h) {
    return w < 0.0f || h < 0.0f;
}

static bool dl_validate_resource_size(const void* resource, float w, float h) {
    return resource && !dl_has_negative_size(w, h);
}

static bool dl_retainable_generation_resource(const void* resource, uint64_t generation) {
    return !resource || generation_stamped(generation);
}

// Retained items reference surfaces by handle; an unregistered surface (null
// handle) cannot be checked for liveness later, so it is never retained.
static bool dl_retainable_surface(const void* borrowed, lam::Handle<ImageSurface> owner,
                                  uint64_t generation) {
    return !borrowed || (!owner.is_null() && generation_stamped(generation));
}

bool dl_validate(const DisplayList* dl, DisplayListValidationResult* result) {
    dl_validation_set(result, true, -1, "ok", 0, 0, 0, 0);
    if (!dl) {
        return dl_validation_fail(result, -1, "display list is null", NULL);
    }
    if (dl->size() > dl->capacity()) {
        return dl_validation_fail(result, -1, "display list count/capacity is invalid",
                                  NULL);
    }
    if (dl->size() > 0 && !dl->data()) {
        return dl_validation_fail(result, -1, "display list has items but no storage",
                                  NULL);
    }

    DisplayListValidationStack stack = {};
    int element_stack[DL_VALIDATE_ELEMENT_STACK_LIMIT];
    auto fail_at = [&](int index, const char* message) -> bool {
        return dl_validation_fail(result, index, message, &stack);
    };

    for (int i = 0; i < dl->item_count(); i++) {
        const DisplayItem* item = &dl->data()[i];
        auto fail = [&](const char* message) -> bool { return fail_at(i, message); };
        if (item->op < DL_FILL_RECT || item->op >= DL_OP_COUNT) {
            return fail("unknown display op");
        }

        switch (item->op) {
            case DL_FILL_RECT:
                if (dl_has_negative_size(item->fill_rect.w, item->fill_rect.h)) {
                    return fail("fill rect has negative size");
                }
                break;
            case DL_FILL_ROUNDED_RECT:
                if (dl_has_negative_size(item->fill_rounded_rect.w, item->fill_rounded_rect.h) ||
                    dl_has_negative_size(item->fill_rounded_rect.rx, item->fill_rounded_rect.ry)) {
                    return fail("rounded rect has negative size");
                }
                break;
            case DL_FILL_PATH:
                if (!item->fill_path.path) {
                    return fail("fill path is null");
                }
                break;
            case DL_STROKE_PATH:
                if (!item->stroke_path.path || item->stroke_path.width < 0.0f ||
                    item->stroke_path.dash_count < 0 ||
                    (item->stroke_path.dash_count > 0 && !item->stroke_path.dash_array)) {
                    return fail("stroke path payload is invalid");
                }
                break;
            case DL_FILL_LINEAR_GRADIENT:
                if (!item->fill_linear_gradient.path || !item->fill_linear_gradient.stops ||
                    item->fill_linear_gradient.stop_count <= 0 ||
                    !rdt_gradient_options_valid(&item->fill_linear_gradient.options)) {
                    return fail("linear gradient payload is invalid");
                }
                break;
            case DL_FILL_RADIAL_GRADIENT:
                if (!item->fill_radial_gradient.path || !item->fill_radial_gradient.stops ||
                    item->fill_radial_gradient.stop_count <= 0 ||
                    !rdt_gradient_options_valid(&item->fill_radial_gradient.options) ||
                    item->fill_radial_gradient.r < 0.0f) {
                    return fail("radial gradient payload is invalid");
                }
                break;
            case DL_DRAW_IMAGE:
                if (!dl_validate_positive_image(&item->draw_image)) {
                    return fail("draw image payload is invalid");
                }
                break;
            case DL_DRAW_GLYPH:
                if (!item->draw_glyph.bitmap.buffer || item->draw_glyph.bitmap.width <= 0 ||
                    item->draw_glyph.bitmap.height <= 0 || item->draw_glyph.bitmap.pitch <= 0) {
                    return fail("glyph payload is invalid");
                }
                break;
            case DL_DRAW_PICTURE:
                if (!item->draw_picture.picture) {
                    return fail("picture payload is invalid");
                }
                break;
            case DL_PUSH_CLIP:
                if (!item->push_clip.path) {
                    return fail("clip path is null");
                }
                stack.clip_depth++;
                break;
            case DL_POP_CLIP:
                if (stack.clip_depth <= 0) {
                    return fail("clip pop without matching push");
                }
                stack.clip_depth--;
                break;
            case DL_FILL_SURFACE_RECT:
                if (dl_has_negative_size(item->fill_surface_rect.w, item->fill_surface_rect.h)) {
                    return fail("surface fill has negative size");
                }
                break;
            case DL_BLIT_SURFACE_SCALED:
                if ((!item->blit_surface_scaled.local_source && item->blit_surface_scaled.src_resource.is_null()) ||
                    dl_has_negative_size(item->blit_surface_scaled.dst_w, item->blit_surface_scaled.dst_h)) {
                    return fail("surface blit payload is invalid");
                }
                break;
            case DL_COMPOSITE_OPACITY:
                if (stack.backdrop_depth <= 0) {
                    return fail("opacity composite without saved backdrop");
                }
                if (dl_has_negative_size(item->composite_opacity.w, item->composite_opacity.h)) {
                    return fail("opacity composite has negative region");
                }
                stack.backdrop_depth--;
                break;
            case DL_SAVE_BACKDROP:
                if (dl_has_negative_size(item->save_backdrop.w, item->save_backdrop.h)) {
                    return fail("saved backdrop has negative region");
                }
                stack.backdrop_depth++;
                break;
            case DL_APPLY_BLEND_MODE:
                if (stack.backdrop_depth <= 0) {
                    return fail("blend mode without saved backdrop");
                }
                if (dl_has_negative_size(item->apply_blend_mode.w, item->apply_blend_mode.h)) {
                    return fail("blend mode has negative region");
                }
                stack.backdrop_depth--;
                break;
            case DL_APPLY_FILTER:
                if (!dl_validate_resource_size(item->apply_filter.filter,
                                               item->apply_filter.w, item->apply_filter.h)) {
                    return fail("filter payload is invalid");
                }
                break;
            case DL_BOX_BLUR_REGION:
                if (dl_has_negative_size(item->box_blur_region.rw, item->box_blur_region.rh) ||
                    item->box_blur_region.blur_radius < 0.0f) {
                    return fail("box blur region payload is invalid");
                }
                break;
            case DL_BOX_BLUR_INSET:
                if (dl_has_negative_size(item->box_blur_inset.rw, item->box_blur_inset.rh) ||
                    item->box_blur_inset.pad < 0 ||
                    item->box_blur_inset.blur_radius < 0.0f) {
                    return fail("inset blur payload is invalid");
                }
                break;
            case DL_SHADOW_CLIP_SAVE:
                if (dl_has_negative_size(item->shadow_clip_save.rw, item->shadow_clip_save.rh)) {
                    return fail("shadow clip save has negative region");
                }
                stack.shadow_clip_depth++;
                break;
            case DL_SHADOW_CLIP_RESTORE:
                if (stack.shadow_clip_depth <= 0) {
                    return fail("shadow clip restore without save");
                }
                if (dl_has_negative_size(item->shadow_clip_restore.save_rw,
                                         item->shadow_clip_restore.save_rh)) {
                    return fail("shadow clip restore has negative region");
                }
                stack.shadow_clip_depth--;
                break;
            case DL_OUTER_SHADOW:
                if (dl_has_negative_size(item->outer_shadow.shadow_w, item->outer_shadow.shadow_h) ||
                    item->outer_shadow.blur_radius < 0.0f) {
                    return fail("outer shadow payload is invalid");
                }
                break;
            case DL_VIDEO_PLACEHOLDER:
                if (!dl_validate_resource_size(item->video_placeholder.video,
                                               item->video_placeholder.dst_w,
                                               item->video_placeholder.dst_h)) {
                    return fail("video placeholder payload is invalid");
                }
                break;
            case DL_WEBVIEW_LAYER_PLACEHOLDER:
                if (!dl_validate_resource_size(item->webview_layer_placeholder.surface,
                                               item->webview_layer_placeholder.dst_w,
                                               item->webview_layer_placeholder.dst_h)) {
                    return fail("webview placeholder payload is invalid");
                }
                break;
            case DL_BEGIN_ELEMENT: {
                int match = item->element_marker.matching_index;
                if (match <= i || match >= dl->item_count() ||
                    dl->data()[match].op != DL_END_ELEMENT ||
                    dl->data()[match].element_marker.matching_index != i) {
                    return fail("element begin marker is not paired");
                }
                if (stack.element_depth >= DL_VALIDATE_ELEMENT_STACK_LIMIT) {
                    return fail("element marker nesting is too deep");
                }
                element_stack[stack.element_depth++] = i;
                break;
            }
            case DL_END_ELEMENT: {
                int match = item->element_marker.matching_index;
                if (stack.element_depth <= 0 || match < 0 || match >= i ||
                    dl->data()[match].op != DL_BEGIN_ELEMENT ||
                    dl->data()[match].element_marker.matching_index != i ||
                    element_stack[stack.element_depth - 1] != match) {
                    return fail("element end marker is not paired");
                }
                stack.element_depth--;
                break;
            }
        }
    }

    if (stack.clip_depth != 0) {
        return fail_at(dl->item_count(), "clip stack is unbalanced");
    }
    if (stack.backdrop_depth != 0) {
        return fail_at(dl->item_count(), "backdrop stack is unbalanced");
    }
    if (stack.shadow_clip_depth != 0) {
        return fail_at(dl->item_count(), "shadow clip stack is unbalanced");
    }
    if (stack.element_depth != 0) {
        return fail_at(dl->item_count(), "element marker stack is unbalanced");
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
            // frame-local pixels are never retained
            if (item->draw_image.local_pixels ||
                !dl_retainable_surface(item, item->draw_image.resource,
                                       item->draw_image.resource_generation)) {
                return false;
            }
            break;
        case DL_DRAW_GLYPH:
            if (!dl_retainable_generation_resource(item->draw_glyph.bitmap.buffer,
                                                   item->draw_glyph.resource_generation)) {
                return false;
            }
            break;
        case DL_BLIT_SURFACE_SCALED:
            if (item->blit_surface_scaled.local_source ||
                !dl_retainable_surface(item, item->blit_surface_scaled.src_resource,
                                       item->blit_surface_scaled.src_generation)) {
                return false;
            }
            break;
        case DL_VIDEO_PLACEHOLDER:
            if (!dl_retainable_generation_resource(item->video_placeholder.video,
                                                   item->video_placeholder.video_generation)) {
                return false;
            }
            break;
        case DL_WEBVIEW_LAYER_PLACEHOLDER:
            if (!dl_retainable_surface(item->webview_layer_placeholder.surface,
                                       item->webview_layer_placeholder.resource,
                                       item->webview_layer_placeholder.surface_generation)) {
                return false;
            }
            break;
        case DL_APPLY_FILTER:
            if (item->apply_filter.filter) {
                return false;
            }
            break;
        case DL_COMPOSITE_OPACITY:
            if (item->composite_opacity.mask.gradient) return false;
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

Bound dl_content_bounds_range(const DisplayList* dl, int start, int end) {
    Bound bounds = {};
    bool valid = false;
    if (dl) for (int index = max(0, start); index < min(end, dl->item_count()); index++) {
        const DisplayItem* item = &dl->data()[index];
        if (dl_op_has_flags(item->op, DL_OP_FLAG_PRESERVES_REPLAY_STATE)) continue;
        valid = dl_union_item_bounds(&bounds.left, &bounds.top, &bounds.right, &bounds.bottom, item, valid);
    }
    return bounds;
}

Bound dl_content_bounds(const DisplayList* dl) {
    return dl_content_bounds_range(dl, 0, dl ? dl->item_count() : 0);
}

Bound dl_effect_capture_bounds(const DisplayList* dl, int start, int end, Bound bounds, float padding) {
    Bound source = dl_content_bounds_range(dl, start, end);
    if (source.right > source.left && source.bottom > source.top) {
        if (!(bounds.right > bounds.left && bounds.bottom > bounds.top))
            return {source.left - padding, source.top - padding, source.right + padding, source.bottom + padding};
        bounds.left = min(bounds.left, source.left - padding);
        bounds.top = min(bounds.top, source.top - padding);
        bounds.right = max(bounds.right, source.right + padding);
        bounds.bottom = max(bounds.bottom, source.bottom + padding);
    }
    return bounds;
}

bool dl_effect_bounds_to_region(const Bound* bounds, IRect* region) {
    if (!bounds || !region) return false;
    float left = floorf(bounds->left), top = floorf(bounds->top);
    float right = ceilf(bounds->right), bottom = ceilf(bounds->bottom);
    if (!isfinite(left) || !isfinite(top) || !isfinite(right) || !isfinite(bottom) ||
            left < (float)INT_MIN || top < (float)INT_MIN || right >= (float)INT_MAX || bottom >= (float)INT_MAX ||
            !(right > left && bottom > top) || right - left >= (float)INT_MAX || bottom - top >= (float)INT_MAX) return false;
    region->x = (int)left; // INT_CAST_OK: effect bounds cover integer display-list pixel regions.
    region->y = (int)top; // INT_CAST_OK: effect bounds cover integer display-list pixel regions.
    region->w = (int)(right - left); // INT_CAST_OK: effect bounds cover integer display-list pixel regions.
    region->h = (int)(bottom - top); // INT_CAST_OK: effect bounds cover integer display-list pixel regions.
    return true;
}

bool dl_resize_saved_backdrops(DisplayList* dl, int start, int end, const Bound* bounds) {
    IRect region = {};
    if (!dl || !dl_effect_bounds_to_region(bounds, &region)) return false;
    // source recording is complete; resize the saved region before any replay/publication.
    for (int index = max(0, start); index < min(end, dl->item_count()); index++) {
        DisplayItem* item = &dl->data()[index];
        if (item->op != DL_SAVE_BACKDROP) continue;
        item->bounds[0] = (float)region.x; item->bounds[1] = (float)region.y;
        item->bounds[2] = (float)region.w; item->bounds[3] = (float)region.h;
        item->save_backdrop = {region.x, region.y, region.w, region.h};
    }
    return true;
}

int dl_begin_element(DisplayList* dl, uint32_t view_id,
                     float x, float y, float w, float h) {
    DisplayItem* item = dl_alloc_item(dl);
    if (!item) return -1;
    int index = dl->item_count() - 1;
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
    int end_index = dl->item_count() - 1;
    end->op = DL_END_ELEMENT;
    end->element_marker.view_id = 0;
    end->element_marker.matching_index = begin_index;

    if (!dl || begin_index < 0 || begin_index >= end_index) {
        return;
    }

    DisplayItem* begin = &dl->data()[begin_index];
    if (begin->op != DL_BEGIN_ELEMENT) {
        return;
    }

    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
    bool has_bounds = false;
    has_bounds = dl_union_item_bounds(&left, &top, &right, &bottom, begin, has_bounds);
    for (int i = begin_index + 1; i < end_index; i++) {
        has_bounds = dl_union_item_bounds(&left, &top, &right, &bottom,
                                          &dl->data()[i], has_bounds);
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

bool dl_draw_image_resolve(const DlDrawImage* item, DlResolvedImage* out) {
    if (!item || !out) return false;
    *out = {};
    if (item->resource.is_null()) {
        if (!item->local_pixels) return false;
        out->pixels = item->local_pixels;  // same kind: a frame-local borrow
        out->width = item->src_w; out->height = item->src_h; out->stride = item->src_stride;
        out->straight_alpha = item->straight_alpha;
        out->generation = item->resource_generation;
        return true;
    }
    ImageSurface* owner = image_surface_lookup(item->resource);
    if (!owner || !owner->pixels) return false;
    int width = owner->decoded_width > 0 ? owner->decoded_width : owner->width;
    int height = owner->decoded_height > 0 ? owner->decoded_height : owner->height;
    int stride = owner->pitch > 0 ? owner->pitch / 4 : width;
    if (width <= 0 || height <= 0 || stride < width) return false;
    int x = item->src_x, y = item->src_y, w = item->src_w, h = item->src_h;
    // a re-decode at another size keeps the recorded region in proportion
    if (item->surface_w > 0 && item->surface_h > 0 &&
        (width != item->surface_w || height != item->surface_h)) {
        x = (int)((int64_t)x * width / item->surface_w); // INT_CAST_OK: pixel coordinate inside the surface
        y = (int)((int64_t)y * height / item->surface_h); // INT_CAST_OK: pixel coordinate inside the surface
        w = (int)(((int64_t)w * width + item->surface_w / 2) / item->surface_w); // INT_CAST_OK: pixel extent inside the surface
        h = (int)(((int64_t)h * height + item->surface_h / 2) / item->surface_h); // INT_CAST_OK: pixel extent inside the surface
        if (x + w > width) w = width - x;
        if (y + h > height) h = height - y;
    }
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > width || y + h > height) return false;
    out->pixels = lam::up((const uint32_t*)owner->pixels + (size_t)y * (size_t)stride + (size_t)x);
    out->width = w; out->height = h; out->stride = stride;
    out->straight_alpha = owner->alpha_mode == IMAGE_ALPHA_STRAIGHT;
    out->generation = owner->generation;
    out->owner = lam::up(owner);
    return true;
}

ImageSurface* dl_blit_source_resolve(const DlBlitSurfaceScaled* item) {
    if (!item) return nullptr;
    if (item->local_source) return item->local_source;
    return image_surface_lookup(item->src_resource);
}

// shared cloning/replacement preserves payload ownership for retained lists and deferred effects.
static void dl_copy_clip_shape_stack(DisplayList* dst,
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
        out->polygon_vx[i] = lam::own_arr(vx);
        out->polygon_vy[i] = lam::own_arr(vy);
    }
}

static bool dl_clone_path(lam::Own<RdtPath>* out, RdtPath* in) {
    if (!out) return false; *out = lam::own(in ? rdt_path_clone(in) : nullptr); return !in || *out;
}

static bool dl_copy_gradient_stops(DisplayList* dst, lam::OwnArr<RdtGradientStop>* out,
                                            const RdtGradientStop* stops, int count) {
    if (!out) return false; *out = lam::own_arr(dl_copy_stops(dst, stops, count)); return !stops || count <= 0 || *out;
}

static bool dl_copy_dashes(DisplayList* dst, lam::OwnArr<float>* out, const float* dashes, int count) {
    if (!out) return false; *out = lam::own_arr(dl_copy_dashes(dst, dashes, count)); return !dashes || count <= 0 || *out;
}

static bool dl_clone_item_payload(DisplayList* dst,
                                           DisplayItem* out,
                                           const DisplayItem* in,
                                           int source_start,
                                           int dest_start) {
    if (!dst || !out || !in) return false;

    switch (in->op) {
        case DL_DRAW_IMAGE:
            out->draw_image.snapshot_lease=lam::up(image_surface_snapshot_retain(in->draw_image.snapshot_lease));
            break;
        case DL_FILL_PATH:
            if (!dl_clone_path(&out->fill_path.path, in->fill_path.path)) return false;
            break;
        case DL_STROKE_PATH:
            if (!dl_clone_path(&out->stroke_path.path, in->stroke_path.path)) return false;
            if (!dl_copy_dashes(dst, &out->stroke_path.dash_array,
                    in->stroke_path.dash_array, in->stroke_path.dash_count)) return false;
            break;
        case DL_FILL_LINEAR_GRADIENT:
            if (!dl_clone_path(&out->fill_linear_gradient.path,
                    in->fill_linear_gradient.path)) return false;
            if (!dl_copy_gradient_stops(dst, &out->fill_linear_gradient.stops,
                    in->fill_linear_gradient.stops, in->fill_linear_gradient.stop_count)) return false;
            out->fill_linear_gradient.options.dash_array = lam::up(dl_copy_dashes(dst,
                in->fill_linear_gradient.options.dash_array, in->fill_linear_gradient.options.dash_count));
            if (in->fill_linear_gradient.options.dash_count > 0 &&
                !out->fill_linear_gradient.options.dash_array) return false;
            break;
        case DL_FILL_RADIAL_GRADIENT:
            if (!dl_clone_path(&out->fill_radial_gradient.path,
                    in->fill_radial_gradient.path)) return false;
            if (!dl_copy_gradient_stops(dst, &out->fill_radial_gradient.stops,
                    in->fill_radial_gradient.stops, in->fill_radial_gradient.stop_count)) return false;
            out->fill_radial_gradient.options.dash_array = lam::up(dl_copy_dashes(dst,
                in->fill_radial_gradient.options.dash_array, in->fill_radial_gradient.options.dash_count));
            if (in->fill_radial_gradient.options.dash_count > 0 &&
                !out->fill_radial_gradient.options.dash_array) return false;
            break;
        case DL_DRAW_PICTURE:
            out->draw_picture.picture = lam::counted(in->draw_picture.picture ?
                rdt_picture_dup(in->draw_picture.picture) : nullptr);
            if (in->draw_picture.picture && !out->draw_picture.picture) return false;
            break;
        case DL_PUSH_CLIP:
            if (!dl_clone_path(&out->push_clip.path, in->push_clip.path)) return false;
            break;
        case DL_FILL_SURFACE_RECT:
            dl_copy_clip_shape_stack(dst, &out->fill_surface_rect.clip_shapes,
                                              &in->fill_surface_rect.clip_shapes);
            break;
        case DL_BLIT_SURFACE_SCALED:
            out->blit_surface_scaled.snapshot_lease = lam::up(image_surface_snapshot_retain(in->blit_surface_scaled.snapshot_lease));
            dl_copy_clip_shape_stack(dst, &out->blit_surface_scaled.clip_shapes,
                                              &in->blit_surface_scaled.clip_shapes);
            break;
        case DL_BEGIN_ELEMENT:
            if (!dl_copy_semantic_group(dst, &out->element_marker.semantics,
                &in->element_marker.semantics)) return false;
            [[fallthrough]];
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

void dl_truncate(DisplayList* dl, int target_count) {
    if (!dl || target_count < 0 || target_count > dl->item_count()) return;
    int current_count = dl->item_count();
    for (int i = target_count; i < current_count; i++) {
        dl_item_free_owned_payload(&dl->data()[i]);
    }
    dl->remove_range((size_t)target_count, (size_t)(current_count - target_count));
}

bool dl_copy_range(DisplayList* dst, const DisplayList* src,
                                   int start, int end) {
    if (!dst || !src || start < 0 || end < start || end >= src->item_count()) return false;

    int dest_start = dst->item_count();
    size_t required_count = 0;
    // copied fragments know their size; a frame-sized reserve per element exhausts paint budgets.
    if (!math_checked_add(dst->size(), (size_t)(end - start) + 1, &required_count) ||
        required_count > (size_t)INT_MAX ||
        (dst->capacity() == 0 && !dst->reserve(required_count))) return false;
    for (int i = start; i <= end; i++) {
        DisplayItem* out = dl_alloc_item(dst);
        if (!out) {
            dl_truncate(dst, dest_start);
            return false;
        }
        DisplayItem copy = src->data()[i];
        *out = copy;
        if (!dl_clone_item_payload(dst, out, &src->data()[i], start, dest_start)) {
            dl_truncate(dst, dest_start);
            return false;
        }
    }
    return true;
}

bool dl_replace_tail(DisplayList* dst, int start, const DisplayList* src, int first, int last) {
    if (!dst || start < 0 || start > dst->item_count()) return false;
    int old_count = dst->item_count();
    // Clone before retiring the old tail: allocation failure preserves the
    // original stream, including its owned paths, pictures and image leases.
    if (!dl_copy_range(dst, src, first, last)) return false;
    int copied = dst->item_count() - old_count;
    for (int i = start; i < old_count; i++) dl_item_free_owned_payload(&dst->data()[i]);
    for (int i = old_count; i < old_count + copied; i++) {
        DisplayItem* item = &dst->data()[i];
        if ((item->op == DL_BEGIN_ELEMENT || item->op == DL_END_ELEMENT) &&
            item->element_marker.matching_index >= old_count)
            item->element_marker.matching_index += start - old_count;
    }
    memmove(dst->data() + start, dst->data() + old_count, (size_t)copied * sizeof(DisplayItem));
    // payload ownership moved with the commands; discarded trailing slots are
    // stale aliases and must not be passed through dl_truncate's payload free.
    dst->remove_range((size_t)(start + copied), (size_t)(old_count - start));
    return true;
}
