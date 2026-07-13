// ==========================================================================
// PaintIR — recording (PaintBuilder) and raster lowering (PaintIR -> DisplayList).
//
// See paint_ir.h for the model. Step 1 lowers the vector primitive ops 1:1 to
// the matching dl_* command so raster output is byte-for-byte identical to
// recording those dl_* calls directly.
// ==========================================================================

#include "render.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/mem_grow.hpp"
#include "../lib/escape.h"
#include "../lib/str.h"
#include <math.h>
#include <string.h>

#define PAINT_LIST_INITIAL_CAPACITY 1024

static PaintSvgSubsceneRasterLowerFn g_svg_subscene_raster_lowerer = nullptr;
static PaintSvgSubsceneSvgLowerFn g_svg_subscene_svg_lowerer = nullptr;
static PaintGlyphRunRasterLowerFn g_glyph_run_raster_lowerer = nullptr;

void paint_ir_register_svg_subscene_lowerers(PaintSvgSubsceneRasterLowerFn raster_lower,
                                             PaintSvgSubsceneSvgLowerFn svg_lower) {
    g_svg_subscene_raster_lowerer = raster_lower;
    g_svg_subscene_svg_lowerer = svg_lower;
}

void paint_ir_register_glyph_run_raster_lowerer(PaintGlyphRunRasterLowerFn lowerer) {
    g_glyph_run_raster_lowerer = lowerer;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void paint_list_init(PaintList* pl, Arena* backing_arena) {
    memset(pl, 0, sizeof(PaintList));
    pl->arena = backing_arena;
}

static void paint_free_owned_path(RdtPath** path, bool* owns_path) {
    if (!path || !owns_path || !*owns_path || !*path) return;
    rdt_path_free(*path);
    *path = nullptr;
    *owns_path = false;
}

static void paint_free_owned_gradient_stops(const RdtGradientStop** stops,
                                            bool* owns_stops) {
    if (!stops || !owns_stops || !*owns_stops || !*stops) return;
    mem_free((void*)*stops);
    *stops = nullptr;
    *owns_stops = false;
}

static void paint_free_owned_gradient_payload(RdtPath** path, bool* owns_path,
                                              const RdtGradientStop** stops,
                                              bool* owns_stops) {
    paint_free_owned_path(path, owns_path);
    paint_free_owned_gradient_stops(stops, owns_stops);
}

static void paint_free_owned_glyph_run_text(PaintGlyphRun* run) {
    if (!run || !run->owns_text || !run->text) return;
    mem_free((void*)run->text);
    run->text = nullptr;
    run->owns_text = false;
}

static void paint_cmd_free_owned_payload(PaintCmd* cmd) {
    if (!cmd) return;
    // deferred lowerers can transfer heap payloads into PaintList commands; central cleanup prevents backend-specific ownership switches from drifting.
    switch (cmd->op) {
    case PAINT_FILL_PATH:
        paint_free_owned_path(&cmd->fill_path.path, &cmd->fill_path.owns_path);
        break;
    case PAINT_STROKE_PATH:
        paint_free_owned_path(&cmd->stroke_path.path, &cmd->stroke_path.owns_path);
        break;
    case PAINT_FILL_LINEAR_GRADIENT:
        paint_free_owned_gradient_payload(&cmd->fill_linear_gradient.path,
                                          &cmd->fill_linear_gradient.owns_path,
                                          &cmd->fill_linear_gradient.stops,
                                          &cmd->fill_linear_gradient.owns_stops);
        break;
    case PAINT_FILL_RADIAL_GRADIENT:
        paint_free_owned_gradient_payload(&cmd->fill_radial_gradient.path,
                                          &cmd->fill_radial_gradient.owns_path,
                                          &cmd->fill_radial_gradient.stops,
                                          &cmd->fill_radial_gradient.owns_stops);
        break;
    case PAINT_GLYPH_RUN:
        paint_free_owned_glyph_run_text(&cmd->glyph_run);
        break;
    default:
        break;
    }
}

void paint_list_clear(PaintList* pl) {
    if (!pl) return;
    for (int i = 0; i < pl->count; i++) {
        if (paint_op_has_flags(pl->cmds[i].op, PAINT_OP_FLAG_OWNED_PAYLOAD)) {
            paint_cmd_free_owned_payload(&pl->cmds[i]);
        }
    }
    pl->count = 0;
}

void paint_list_destroy(PaintList* pl) {
    if (!pl) return;
    paint_list_clear(pl);
    if (pl->cmds) {
        mem_free(pl->cmds);
        pl->cmds = nullptr;
    }
    pl->count = 0;
    pl->capacity = 0;
}

int paint_list_count(const PaintList* pl) {
    return pl ? pl->count : 0;
}

static void paint_ir_validation_set(PaintIrValidationResult* result, bool valid,
                                    int first_error_index, const char* message,
                                    int clip_depth, int backdrop_depth,
                                    int shadow_clip_depth, int effect_depth) {
    if (!result) return;
    result->valid = valid;
    result->first_error_index = first_error_index;
    result->message = message;
    result->clip_depth = clip_depth;
    result->backdrop_depth = backdrop_depth;
    result->shadow_clip_depth = shadow_clip_depth;
    result->effect_depth = effect_depth;
}

typedef struct {
    int clip_depth;
    int backdrop_depth;
    int shadow_clip_depth;
    int effect_depth;
    int transform_depth;
} PaintIrValidationStack;

typedef struct PaintOpDescriptor {
    const char* name;
    unsigned flags;
} PaintOpDescriptor;

static const PaintOpDescriptor* paint_op_descriptor(PaintOp op) {
    static const PaintOpDescriptor descriptors[] = {
#define PAINT_OP_DESCRIPTOR_ENTRY(op, flags) { #op, flags },
        PAINT_OP_LIST(PAINT_OP_DESCRIPTOR_ENTRY)
#undef PAINT_OP_DESCRIPTOR_ENTRY
    };
    static_assert((int)(sizeof(descriptors) / sizeof(descriptors[0])) == (int)PAINT_OP_COUNT,
                  "PaintOp descriptor table must match PAINT_OP_LIST");
    int op_index = (int)op;
    if (op_index < 0 || op_index >= (int)PAINT_OP_COUNT) return nullptr;
    return &descriptors[op_index];
}

const char* paint_op_name(PaintOp op) {
    const PaintOpDescriptor* descriptor = paint_op_descriptor(op);
    return descriptor ? descriptor->name : "PAINT_UNKNOWN";
}

bool paint_op_has_flags(PaintOp op, unsigned flags) {
    const PaintOpDescriptor* descriptor = paint_op_descriptor(op);
    return descriptor && (descriptor->flags & flags) == flags;
}

bool paint_list_has_op_flags(const PaintList* pl, unsigned flags) {
    if (!pl) return false;
    for (int i = 0; i < pl->count; i++) {
        if (paint_op_has_flags(pl->cmds[i].op, flags)) return true;
    }
    return false;
}

static bool paint_ir_validation_fail_at(PaintIrValidationResult* result, int index,
                                        const char* message,
                                        const PaintIrValidationStack* stack) {
    PaintIrValidationStack empty = {};
    const PaintIrValidationStack* s = stack ? stack : &empty;
    paint_ir_validation_set(result, false, index, message,
                            s->clip_depth, s->backdrop_depth,
                            s->shadow_clip_depth, s->effect_depth);
    return false;
}

static bool paint_ir_validate_positive_image(const PaintDrawImage* image) {
    return image && image->pixels &&
           image->src_w > 0 && image->src_h > 0 && image->src_stride > 0 &&
           image->dst_w >= 0.0f && image->dst_h >= 0.0f;
}

static bool paint_ir_has_negative_size(float w, float h) {
    return w < 0.0f || h < 0.0f;
}

static bool paint_ir_validate_resource_size(const void* resource, float w, float h) {
    return resource && !paint_ir_has_negative_size(w, h);
}

static bool paint_ir_validate_gradient_payload(RdtPath* path,
                                               const RdtGradientStop* stops,
                                               int stop_count) {
    return path && stops && stop_count > 0;
}

static bool paint_ir_validate_glyph_bitmap(const GlyphBitmap* bitmap) {
    return bitmap && bitmap->buffer &&
           bitmap->width > 0 && bitmap->height > 0 && bitmap->pitch > 0;
}

static bool paint_ir_validate_stroke_path(const PaintStrokePath* stroke) {
    return stroke && stroke->path &&
           stroke->width >= 0.0f &&
           stroke->dash_count >= 0 &&
           (stroke->dash_count == 0 || stroke->dash_array);
}

static bool paint_ir_validate_effect_group(const PaintEffectGroup* group) {
    return group &&
           group->bounds.right >= group->bounds.left &&
           group->bounds.bottom >= group->bounds.top &&
           group->opacity >= 0.0f &&
           group->opacity <= 1.0f;
}

static bool paint_ir_validate_glyph_run(const PaintGlyphRun* run) {
    if (!run) return false;
    if (run->text) {
        return run->text_len >= -1 && run->font_size >= 0.0f;
    }
    return run->glyph_ids && run->xs && run->ys && run->count > 0;
}

static bool paint_ir_image_resource_pixels(ImageSurface* image,
                                           float dst_w, float dst_h,
                                           const uint32_t** pixels,
                                           int* src_w, int* src_h,
                                           int* src_stride) {
    if (!image || !pixels || !src_w || !src_h || !src_stride ||
        dst_w <= 0.0f || dst_h <= 0.0f) {
        return false;
    }

    int width = image->decoded_width > 0 ? image->decoded_width : image->width;
    int height = image->decoded_height > 0 ? image->decoded_height : image->height;
    // display-list image commands store uint32_t row stride; ImageSurface::pitch is bytes.
    int stride = image->pitch > 0 ? image->pitch / 4 : width;
    if (!image->pixels || width <= 0 || height <= 0 || stride <= 0) {
        return false;
    }

    *pixels = (const uint32_t*)image->pixels;
    *src_w = width;
    *src_h = height;
    *src_stride = stride;
    return true;
}

bool paint_ir_validate(const PaintList* pl, PaintIrValidationResult* result) {
    if (!pl) {
        return paint_ir_validation_fail_at(result, -1, "paint list is null", NULL);
    }

    PaintIrValidationStack stack = {};

    for (int i = 0; i < pl->count; i++) {
        const PaintCmd* cmd = &pl->cmds[i];
        auto fail = [&](const char* message) -> bool {
            return paint_ir_validation_fail_at(result, i, message, &stack);
        };
        const PaintOpDescriptor* descriptor = paint_op_descriptor(cmd->op);
        if (!descriptor) return fail("unknown paint op");
        auto has_descriptor_flags = [&](unsigned flags) -> bool {
            return (descriptor->flags & flags) == flags;
        };
        auto pop_balanced = [&](int* depth, const char* message) -> bool {
            if (*depth <= 0) return fail(message);
            (*depth)--;
            return true;
        };
        auto pop_region = [&](int* depth, float w, float h,
                              const char* missing_message,
                              const char* negative_message) -> bool {
            if (*depth <= 0) return fail(missing_message);
            if (paint_ir_has_negative_size(w, h)) return fail(negative_message);
            (*depth)--;
            return true;
        };
        auto require_non_negative_size = [&](float w, float h, const char* message) -> bool {
            return !paint_ir_has_negative_size(w, h) || fail(message);
        };
        if (has_descriptor_flags(PAINT_OP_FLAG_TRANSFORM_STACK | PAINT_OP_FLAG_STACK_PUSH)) {
            stack.transform_depth++;
            continue;
        }
        if (has_descriptor_flags(PAINT_OP_FLAG_TRANSFORM_STACK | PAINT_OP_FLAG_STACK_POP)) {
            if (!pop_balanced(&stack.transform_depth, "transform pop without matching push")) return false;
            continue;
        }
        if (has_descriptor_flags(PAINT_OP_FLAG_EFFECT_STACK | PAINT_OP_FLAG_STACK_PUSH)) {
            if (!paint_ir_validate_effect_group(&cmd->effect_group)) {
                return fail("effect group payload is invalid");
            }
            stack.effect_depth++;
            continue;
        }
        if (has_descriptor_flags(PAINT_OP_FLAG_EFFECT_STACK | PAINT_OP_FLAG_STACK_POP)) {
            if (!pop_balanced(&stack.effect_depth, "effect group end without begin")) return false;
            continue;
        }
        if (has_descriptor_flags(PAINT_OP_FLAG_CLIP_STACK | PAINT_OP_FLAG_STACK_PUSH)) {
            if (!cmd->push_clip.clip_path) return fail("clip path is null");
            stack.clip_depth++;
            continue;
        }
        if (has_descriptor_flags(PAINT_OP_FLAG_CLIP_STACK | PAINT_OP_FLAG_STACK_POP)) {
            if (!pop_balanced(&stack.clip_depth, "clip pop without matching push")) return false;
            continue;
        }
        switch (cmd->op) {
        case PAINT_FILL_RECT:
            if (!require_non_negative_size(cmd->fill_rect.w, cmd->fill_rect.h,
                                           "fill rect has negative size")) return false;
            break;
        case PAINT_FILL_ROUNDED_RECT:
            if (!require_non_negative_size(cmd->fill_rounded_rect.w, cmd->fill_rounded_rect.h,
                                           "rounded rect has negative size")) return false;
            if (!require_non_negative_size(cmd->fill_rounded_rect.rx, cmd->fill_rounded_rect.ry,
                                           "rounded rect has negative size")) return false;
            break;
        case PAINT_FILL_PATH:
            if (!cmd->fill_path.path) {
                return fail("fill path is null");
            }
            break;
        case PAINT_STROKE_PATH:
            if (!paint_ir_validate_stroke_path(&cmd->stroke_path)) {
                return fail("stroke path payload is invalid");
            }
            break;
        case PAINT_FILL_LINEAR_GRADIENT:
            if (!paint_ir_validate_gradient_payload(cmd->fill_linear_gradient.path,
                                                    cmd->fill_linear_gradient.stops,
                                                    cmd->fill_linear_gradient.stop_count)) {
                return fail("linear gradient payload is invalid");
            }
            break;
        case PAINT_FILL_RADIAL_GRADIENT:
            if (!paint_ir_validate_gradient_payload(cmd->fill_radial_gradient.path,
                                                    cmd->fill_radial_gradient.stops,
                                                    cmd->fill_radial_gradient.stop_count) ||
                cmd->fill_radial_gradient.r < 0.0f) {
                return fail("radial gradient payload is invalid");
            }
            break;
        case PAINT_DRAW_IMAGE:
            if (!paint_ir_validate_positive_image(&cmd->draw_image)) {
                return fail("draw image payload is invalid");
            }
            break;
        case PAINT_DRAW_IMAGE_RESOURCE:
            if (!paint_ir_validate_resource_size(cmd->draw_image_resource.image,
                                                 cmd->draw_image_resource.dst_w,
                                                 cmd->draw_image_resource.dst_h)) {
                return fail("image resource payload is invalid");
            }
            break;
        case PAINT_DRAW_GLYPH:
            if (!paint_ir_validate_glyph_bitmap(&cmd->draw_glyph.bitmap)) {
                return fail("glyph payload is invalid");
            }
            break;
        case PAINT_DRAW_PICTURE:
            if (!cmd->draw_picture.picture) {
                return fail("picture payload is invalid");
            }
            break;
        case PAINT_VIDEO_PLACEHOLDER:
            if (!paint_ir_validate_resource_size(cmd->video_placeholder.video,
                                                 cmd->video_placeholder.dst_w,
                                                 cmd->video_placeholder.dst_h)) {
                return fail("video placeholder payload is invalid");
            }
            break;
        case PAINT_WEBVIEW_LAYER_PLACEHOLDER:
            if (!paint_ir_validate_resource_size(cmd->webview_layer_placeholder.surface,
                                                 cmd->webview_layer_placeholder.dst_w,
                                                 cmd->webview_layer_placeholder.dst_h)) {
                return fail("webview placeholder payload is invalid");
            }
            break;
        case PAINT_PUSH_CLIP:
        case PAINT_POP_CLIP:
        case PAINT_PUSH_TRANSFORM:
        case PAINT_POP_TRANSFORM:
        case PAINT_BEGIN_EFFECT_GROUP:
        case PAINT_END_EFFECT_GROUP:
            // descriptor flags own stack validation; reaching this switch means PAINT_OP_LIST drifted.
            return fail("stack op missing descriptor stack flags");
        case PAINT_SAVE_BACKDROP:
            if (!require_non_negative_size(cmd->save_backdrop.w, cmd->save_backdrop.h,
                                           "saved backdrop has negative region")) return false;
            stack.backdrop_depth++;
            break;
        case PAINT_COMPOSITE_OPACITY:
            if (!pop_region(&stack.backdrop_depth,
                            cmd->composite_opacity.w, cmd->composite_opacity.h,
                            "opacity composite without saved backdrop",
                            "opacity composite has negative region")) return false;
            break;
        case PAINT_APPLY_BLEND_MODE:
            if (!pop_region(&stack.backdrop_depth,
                            cmd->apply_blend_mode.w, cmd->apply_blend_mode.h,
                            "blend mode without saved backdrop",
                            "blend mode has negative region")) return false;
            break;
        case PAINT_APPLY_FILTER:
            if (!cmd->apply_filter.filter ||
                !require_non_negative_size(cmd->apply_filter.w, cmd->apply_filter.h,
                                           "filter payload is invalid")) {
                return fail("filter payload is invalid");
            }
            break;
        case PAINT_BOX_BLUR_REGION:
            if (!require_non_negative_size(cmd->box_blur_region.rw, cmd->box_blur_region.rh,
                                           "box blur region payload is invalid") ||
                cmd->box_blur_region.blur_radius < 0.0f) {
                return fail("box blur region payload is invalid");
            }
            break;
        case PAINT_BOX_BLUR_INSET:
            if (!require_non_negative_size(cmd->box_blur_inset.rw, cmd->box_blur_inset.rh,
                                           "inset blur payload is invalid") ||
                cmd->box_blur_inset.pad < 0 ||
                cmd->box_blur_inset.blur_radius < 0.0f) {
                return fail("inset blur payload is invalid");
            }
            break;
        case PAINT_SHADOW_CLIP_SAVE:
            if (!require_non_negative_size(cmd->shadow_clip_save.rw, cmd->shadow_clip_save.rh,
                                           "shadow clip save has negative region")) return false;
            stack.shadow_clip_depth++;
            break;
        case PAINT_SHADOW_CLIP_RESTORE:
            if (!pop_region(&stack.shadow_clip_depth,
                            cmd->shadow_clip_restore.save_rw,
                            cmd->shadow_clip_restore.save_rh,
                            "shadow clip restore without save",
                            "shadow clip restore has negative region")) return false;
            break;
        case PAINT_OUTER_SHADOW:
            if (!require_non_negative_size(cmd->outer_shadow.shadow_w, cmd->outer_shadow.shadow_h,
                                           "outer shadow payload is invalid") ||
                cmd->outer_shadow.blur_radius < 0.0f) {
                return fail("outer shadow payload is invalid");
            }
            break;
        case PAINT_FILL_SURFACE_RECT:
            if (!require_non_negative_size(cmd->fill_surface_rect.w, cmd->fill_surface_rect.h,
                                           "surface fill has negative size")) return false;
            break;
        case PAINT_BLIT_SURFACE_SCALED:
            if (!paint_ir_validate_resource_size(cmd->blit_surface_scaled.src_surface,
                                                 cmd->blit_surface_scaled.dst_w,
                                                 cmd->blit_surface_scaled.dst_h)) {
                return fail("surface blit payload is invalid");
            }
            break;
        case PAINT_GLYPH_RUN:
            if (!paint_ir_validate_glyph_run(&cmd->glyph_run)) {
                return fail("glyph run payload is invalid");
            }
            break;
        case PAINT_SVG_SUBSCENE:
            if (!cmd->svg_subscene.svg_root ||
                cmd->svg_subscene.viewport_width <= 0.0f ||
                cmd->svg_subscene.viewport_height <= 0.0f) {
                return fail("svg subscene root is null");
            }
            break;
        case PAINT_OP_COUNT:
            return fail("unknown paint op");
        }
    }

    auto require_balanced = [&](int depth, const char* message) -> bool {
        return depth == 0 || paint_ir_validation_fail_at(result, pl->count, message, &stack);
    };
    if (!require_balanced(stack.clip_depth, "clip stack is unbalanced")) return false;
    if (!require_balanced(stack.backdrop_depth, "backdrop stack is unbalanced")) return false;
    if (!require_balanced(stack.shadow_clip_depth, "shadow clip stack is unbalanced")) return false;
    if (!require_balanced(stack.effect_depth, "effect group stack is unbalanced")) return false;
    if (!require_balanced(stack.transform_depth, "transform stack is unbalanced")) return false;

    paint_ir_validation_set(result, true, -1, "ok", 0, 0, 0, 0);
    return true;
}

bool paint_ir_validate_or_log(const PaintList* pl, const char* context) {
    PaintIrValidationResult result = {};
    if (paint_ir_validate(pl, &result)) return true;
    log_error("[PAINT_IR_VALIDATE] %s: invalid PaintIR at item %d: %s (clip=%d backdrop=%d shadow_clip=%d effect=%d)",
              context ? context : "unknown",
              result.first_error_index,
              result.message ? result.message : "invalid",
              result.clip_depth,
              result.backdrop_depth,
              result.shadow_clip_depth,
              result.effect_depth);
    return false;
}

static PaintCmd* paint_alloc_cmd(PaintList* pl, PaintOp op) {
    if (!pl) return nullptr;
    if (pl->count >= pl->capacity) {
        // keep the PaintList unchanged if growth fails; callers already handle null commands
        if (!lam::mem_grow_array(&pl->cmds, &pl->capacity, pl->count + 1,
                                 PAINT_LIST_INITIAL_CAPACITY, MEM_CAT_RENDER)) return nullptr;
    }
    PaintCmd* cmd = &pl->cmds[pl->count++];
    memset(cmd, 0, sizeof(PaintCmd));
    cmd->op = op;
    return cmd;
}

static void paint_copy_effect_params(float* dst, int type, const float* params) {
    if (type && params) {
        memcpy(dst, params, 8 * sizeof(float));
    } else {
        memset(dst, 0, 8 * sizeof(float));
    }
}

static void paint_assign_optional_transform(bool* has_transform,
                                            RdtMatrix* dst,
                                            const RdtMatrix* transform) {
    *has_transform = transform != nullptr;
    if (transform) *dst = *transform;
}

static void paint_assign_optional_clip(bool* has_clip, Bound* dst, const Bound* clip) {
    *has_clip = clip != nullptr;
    if (clip) *dst = *clip;
}

// ---------------------------------------------------------------------------
// PaintBuilder — recording API
// ---------------------------------------------------------------------------

void paint_fill_rect(PaintList* pl, float x, float y, float w, float h, Color color) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_FILL_RECT);
    if (!cmd) return;
    cmd->fill_rect = { x, y, w, h, color };
}

void paint_fill_rounded_rect(PaintList* pl, float x, float y, float w, float h,
                             float rx, float ry, Color color) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_FILL_ROUNDED_RECT);
    if (!cmd) return;
    cmd->fill_rounded_rect = { x, y, w, h, rx, ry, color };
}

void paint_fill_path(PaintList* pl, RdtPath* path, Color color,
                     RdtFillRule rule, const RdtMatrix* transform) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_FILL_PATH);
    if (!cmd) return;
    cmd->fill_path.path = path;
    cmd->fill_path.color = color;
    cmd->fill_path.rule = rule;
    paint_assign_optional_transform(&cmd->fill_path.has_transform,
                                    &cmd->fill_path.transform, transform);
}

void paint_stroke_path(PaintList* pl, RdtPath* path, Color color, float width,
                       RdtStrokeCap cap, RdtStrokeJoin join,
                       const float* dash_array, int dash_count, float dash_phase,
                       const RdtMatrix* transform) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_STROKE_PATH);
    if (!cmd) return;
    cmd->stroke_path.path = path;
    cmd->stroke_path.color = color;
    cmd->stroke_path.width = width;
    cmd->stroke_path.cap = cap;
    cmd->stroke_path.join = join;
    cmd->stroke_path.dash_array = dash_array;
    cmd->stroke_path.dash_count = dash_count;
    cmd->stroke_path.dash_phase = dash_phase;
    paint_assign_optional_transform(&cmd->stroke_path.has_transform,
                                    &cmd->stroke_path.transform, transform);
}

void paint_fill_linear_gradient(PaintList* pl, RdtPath* path,
                                float x1, float y1, float x2, float y2,
                                const RdtGradientStop* stops, int stop_count,
                                RdtFillRule rule, const RdtMatrix* transform,
                                const RdtMatrix* gradient_transform) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_FILL_LINEAR_GRADIENT);
    if (!cmd) return;
    cmd->fill_linear_gradient.path = path;
    cmd->fill_linear_gradient.x1 = x1;
    cmd->fill_linear_gradient.y1 = y1;
    cmd->fill_linear_gradient.x2 = x2;
    cmd->fill_linear_gradient.y2 = y2;
    cmd->fill_linear_gradient.stops = stops;
    cmd->fill_linear_gradient.stop_count = stop_count;
    cmd->fill_linear_gradient.rule = rule;
    paint_assign_optional_transform(&cmd->fill_linear_gradient.has_transform,
                                    &cmd->fill_linear_gradient.transform, transform);
    paint_assign_optional_transform(&cmd->fill_linear_gradient.has_gradient_transform,
                                    &cmd->fill_linear_gradient.gradient_transform, gradient_transform);
}

void paint_fill_radial_gradient(PaintList* pl, RdtPath* path,
                                float cx, float cy, float r,
                                const RdtGradientStop* stops, int stop_count,
                                RdtFillRule rule, const RdtMatrix* transform,
                                const RdtMatrix* gradient_transform) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_FILL_RADIAL_GRADIENT);
    if (!cmd) return;
    cmd->fill_radial_gradient.path = path;
    cmd->fill_radial_gradient.cx = cx;
    cmd->fill_radial_gradient.cy = cy;
    cmd->fill_radial_gradient.r = r;
    cmd->fill_radial_gradient.stops = stops;
    cmd->fill_radial_gradient.stop_count = stop_count;
    cmd->fill_radial_gradient.rule = rule;
    paint_assign_optional_transform(&cmd->fill_radial_gradient.has_transform,
                                    &cmd->fill_radial_gradient.transform, transform);
    paint_assign_optional_transform(&cmd->fill_radial_gradient.has_gradient_transform,
                                    &cmd->fill_radial_gradient.gradient_transform, gradient_transform);
}

void paint_draw_image(PaintList* pl, const uint32_t* pixels,
                      int src_w, int src_h, int src_stride,
                      float dst_x, float dst_y, float dst_w, float dst_h,
                      uint8_t opacity, const RdtMatrix* transform,
                      void* resource_owner) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_DRAW_IMAGE);
    if (!cmd) return;
    cmd->draw_image.pixels = pixels;
    cmd->draw_image.src_w = src_w;
    cmd->draw_image.src_h = src_h;
    cmd->draw_image.src_stride = src_stride;
    cmd->draw_image.dst_x = dst_x;
    cmd->draw_image.dst_y = dst_y;
    cmd->draw_image.dst_w = dst_w;
    cmd->draw_image.dst_h = dst_h;
    cmd->draw_image.opacity = opacity;
    paint_assign_optional_transform(&cmd->draw_image.has_transform,
                                    &cmd->draw_image.transform, transform);
    cmd->draw_image.resource_owner = resource_owner;
}

void paint_draw_image_resource(PaintList* pl, ImageSurface* image,
                               float dst_x, float dst_y,
                               float dst_w, float dst_h,
                               uint8_t opacity,
                               const RdtMatrix* transform) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_DRAW_IMAGE_RESOURCE);
    if (!cmd) return;
    cmd->draw_image_resource.image = image;
    cmd->draw_image_resource.dst_x = dst_x;
    cmd->draw_image_resource.dst_y = dst_y;
    cmd->draw_image_resource.dst_w = dst_w;
    cmd->draw_image_resource.dst_h = dst_h;
    cmd->draw_image_resource.opacity = opacity;
    paint_assign_optional_transform(&cmd->draw_image_resource.has_transform,
                                    &cmd->draw_image_resource.transform, transform);
}

void paint_draw_glyph(PaintList* pl, GlyphBitmap* bitmap, int x, int y,
                      Color color, bool is_color_emoji, const Bound* clip,
                      const RdtMatrix* transform, uint64_t resource_generation) {
    if (!bitmap) return;
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_DRAW_GLYPH);
    if (!cmd) return;
    cmd->draw_glyph.bitmap = *bitmap;
    cmd->draw_glyph.x = x;
    cmd->draw_glyph.y = y;
    cmd->draw_glyph.color = color;
    cmd->draw_glyph.is_color_emoji = is_color_emoji;
    paint_assign_optional_clip(&cmd->draw_glyph.has_clip, &cmd->draw_glyph.clip, clip);
    paint_assign_optional_transform(&cmd->draw_glyph.has_transform,
                                    &cmd->draw_glyph.transform, transform);
    cmd->draw_glyph.resource_generation = resource_generation;
}

void paint_draw_picture(PaintList* pl, RdtPicture* picture,
                        uint8_t opacity, const RdtMatrix* transform) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_DRAW_PICTURE);
    if (!cmd) return;
    cmd->draw_picture.picture = picture;
    cmd->draw_picture.opacity = opacity;
    paint_assign_optional_transform(&cmd->draw_picture.has_transform,
                                    &cmd->draw_picture.transform, transform);
}

void paint_video_placeholder(PaintList* pl, void* video,
                             float dst_x, float dst_y, float dst_w, float dst_h,
                             int object_fit, const Bound* clip,
                             uint64_t video_generation) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_VIDEO_PLACEHOLDER);
    if (!cmd) return;
    cmd->video_placeholder.video = video;
    cmd->video_placeholder.dst_x = dst_x;
    cmd->video_placeholder.dst_y = dst_y;
    cmd->video_placeholder.dst_w = dst_w;
    cmd->video_placeholder.dst_h = dst_h;
    cmd->video_placeholder.object_fit = object_fit;
    paint_assign_optional_clip(&cmd->video_placeholder.has_clip,
                               &cmd->video_placeholder.clip, clip);
    cmd->video_placeholder.video_generation = video_generation;
}

void paint_webview_layer_placeholder(PaintList* pl, void* surface,
                                     float dst_x, float dst_y, float dst_w, float dst_h,
                                     const Bound* clip,
                                     uint64_t surface_generation) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_WEBVIEW_LAYER_PLACEHOLDER);
    if (!cmd) return;
    cmd->webview_layer_placeholder.surface = surface;
    cmd->webview_layer_placeholder.dst_x = dst_x;
    cmd->webview_layer_placeholder.dst_y = dst_y;
    cmd->webview_layer_placeholder.dst_w = dst_w;
    cmd->webview_layer_placeholder.dst_h = dst_h;
    paint_assign_optional_clip(&cmd->webview_layer_placeholder.has_clip,
                               &cmd->webview_layer_placeholder.clip, clip);
    cmd->webview_layer_placeholder.surface_generation = surface_generation;
}

void paint_push_clip(PaintList* pl, RdtPath* clip_path, const RdtMatrix* transform) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_PUSH_CLIP);
    if (!cmd) return;
    cmd->push_clip.clip_path = clip_path;
    paint_assign_optional_transform(&cmd->push_clip.has_transform,
                                    &cmd->push_clip.transform, transform);
}

void paint_pop_clip(PaintList* pl) {
    paint_alloc_cmd(pl, PAINT_POP_CLIP);
}

void paint_push_transform(PaintList* pl, const RdtMatrix* transform) {
    if (!transform) return;
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_PUSH_TRANSFORM);
    if (!cmd) return;
    cmd->push_transform.transform = *transform;
}

void paint_pop_transform(PaintList* pl) {
    paint_alloc_cmd(pl, PAINT_POP_TRANSFORM);
}

void paint_save_backdrop(PaintList* pl, int x0, int y0, int w, int h) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_SAVE_BACKDROP);
    if (!cmd) return;
    cmd->save_backdrop = { x0, y0, w, h };
}

void paint_composite_opacity(PaintList* pl, int x0, int y0, int w, int h,
                             float opacity, bool premultiplied_source) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_COMPOSITE_OPACITY);
    if (!cmd) return;
    cmd->composite_opacity = { x0, y0, w, h, opacity, premultiplied_source };
}

void paint_apply_blend_mode(PaintList* pl, int x0, int y0, int w, int h, int blend_mode) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_APPLY_BLEND_MODE);
    if (!cmd) return;
    cmd->apply_blend_mode = { x0, y0, w, h, blend_mode };
}

void paint_apply_filter(PaintList* pl, float x, float y, float w, float h,
                        void* filter, const Bound* clip) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_APPLY_FILTER);
    if (!cmd) return;
    cmd->apply_filter.x = x;
    cmd->apply_filter.y = y;
    cmd->apply_filter.w = w;
    cmd->apply_filter.h = h;
    cmd->apply_filter.filter = filter;
    paint_assign_optional_clip(&cmd->apply_filter.has_clip, &cmd->apply_filter.clip, clip);
}

void paint_box_blur_region(PaintList* pl, int rx, int ry, int rw, int rh,
                           float blur_radius, int clip_type, const float* clip_params,
                           int exclude_type, const float* exclude_params,
                           bool premultiply_source, bool tint_source, Color tint_color) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_BOX_BLUR_REGION);
    if (!cmd) return;
    cmd->box_blur_region.rx = rx;
    cmd->box_blur_region.ry = ry;
    cmd->box_blur_region.rw = rw;
    cmd->box_blur_region.rh = rh;
    cmd->box_blur_region.blur_radius = blur_radius;
    cmd->box_blur_region.clip_type = clip_type;
    paint_copy_effect_params(cmd->box_blur_region.clip_params, clip_type, clip_params);
    cmd->box_blur_region.exclude_type = exclude_type;
    paint_copy_effect_params(cmd->box_blur_region.exclude_params, exclude_type, exclude_params);
    cmd->box_blur_region.premultiply_source = premultiply_source;
    cmd->box_blur_region.tint_source = tint_source;
    cmd->box_blur_region.tint_color = tint_color;
}

void paint_box_blur_inset(PaintList* pl, int rx, int ry, int rw, int rh,
                          int pad, float blur_radius, uint32_t bg_color) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_BOX_BLUR_INSET);
    if (!cmd) return;
    cmd->box_blur_inset = { rx, ry, rw, rh, pad, blur_radius, bg_color };
}

void paint_shadow_clip_save(PaintList* pl, int rx, int ry, int rw, int rh) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_SHADOW_CLIP_SAVE);
    if (!cmd) return;
    cmd->shadow_clip_save = { rx, ry, rw, rh };
}

void paint_shadow_clip_restore(PaintList* pl, int exclude_type, const float* exclude_params,
                               int save_rx, int save_ry, int save_rw, int save_rh,
                               int restore_inside) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_SHADOW_CLIP_RESTORE);
    if (!cmd) return;
    cmd->shadow_clip_restore.exclude_type = exclude_type;
    paint_copy_effect_params(cmd->shadow_clip_restore.exclude_params,
                             exclude_type, exclude_params);
    cmd->shadow_clip_restore.save_rx = save_rx;
    cmd->shadow_clip_restore.save_ry = save_ry;
    cmd->shadow_clip_restore.save_rw = save_rw;
    cmd->shadow_clip_restore.save_rh = save_rh;
    cmd->shadow_clip_restore.restore_inside = restore_inside;
}

void paint_outer_shadow(PaintList* pl,
                        float shadow_x, float shadow_y, float shadow_w, float shadow_h,
                        float sr_tl, float sr_tr, float sr_br, float sr_bl,
                        Color color, float blur_radius,
                        int exclude_type, const float* exclude_params,
                        int clip_type, const float* clip_params) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_OUTER_SHADOW);
    if (!cmd) return;
    cmd->outer_shadow.shadow_x = shadow_x;
    cmd->outer_shadow.shadow_y = shadow_y;
    cmd->outer_shadow.shadow_w = shadow_w;
    cmd->outer_shadow.shadow_h = shadow_h;
    cmd->outer_shadow.sr_tl = sr_tl;
    cmd->outer_shadow.sr_tr = sr_tr;
    cmd->outer_shadow.sr_br = sr_br;
    cmd->outer_shadow.sr_bl = sr_bl;
    cmd->outer_shadow.color = color;
    cmd->outer_shadow.blur_radius = blur_radius;
    cmd->outer_shadow.exclude_type = exclude_type;
    paint_copy_effect_params(cmd->outer_shadow.exclude_params, exclude_type, exclude_params);
    cmd->outer_shadow.clip_type = clip_type;
    paint_copy_effect_params(cmd->outer_shadow.clip_params, clip_type, clip_params);
}

void paint_fill_surface_rect(PaintList* pl, float x, float y, float w, float h,
                             uint32_t color, const Bound* clip,
                             ClipShape** clip_shapes, int clip_depth) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_FILL_SURFACE_RECT);
    if (!cmd) return;
    cmd->fill_surface_rect.x = x;
    cmd->fill_surface_rect.y = y;
    cmd->fill_surface_rect.w = w;
    cmd->fill_surface_rect.h = h;
    cmd->fill_surface_rect.color = color;
    paint_assign_optional_clip(&cmd->fill_surface_rect.has_clip,
                               &cmd->fill_surface_rect.clip, clip);
    cmd->fill_surface_rect.clip_shapes = clip_shapes;
    cmd->fill_surface_rect.clip_depth = clip_depth;
}

void paint_blit_surface_scaled(PaintList* pl, void* src_surface,
                               float dst_x, float dst_y, float dst_w, float dst_h,
                               int scale_mode, const Bound* clip,
                               ClipShape** clip_shapes, int clip_depth,
                               uint8_t opacity, uint64_t src_generation) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_BLIT_SURFACE_SCALED);
    if (!cmd) return;
    cmd->blit_surface_scaled.src_surface = src_surface;
    cmd->blit_surface_scaled.src_generation = src_generation;
    cmd->blit_surface_scaled.dst_x = dst_x;
    cmd->blit_surface_scaled.dst_y = dst_y;
    cmd->blit_surface_scaled.dst_w = dst_w;
    cmd->blit_surface_scaled.dst_h = dst_h;
    cmd->blit_surface_scaled.scale_mode = scale_mode;
    cmd->blit_surface_scaled.opacity = opacity;
    paint_assign_optional_clip(&cmd->blit_surface_scaled.has_clip,
                               &cmd->blit_surface_scaled.clip, clip);
    cmd->blit_surface_scaled.clip_shapes = clip_shapes;
    cmd->blit_surface_scaled.clip_depth = clip_depth;
}

void paint_begin_effect_group(PaintList* pl, const PaintEffectGroup* group) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_BEGIN_EFFECT_GROUP);
    if (!cmd) return;
    if (group) {
        cmd->effect_group = *group;
    } else {
        cmd->effect_group.opacity = 1.0f;
    }
}

void paint_end_effect_group(PaintList* pl) {
    paint_alloc_cmd(pl, PAINT_END_EFFECT_GROUP);
}

void paint_svg_subscene(PaintList* pl, const PaintSvgSubscene* subscene) {
    if (!subscene) return;
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_SVG_SUBSCENE);
    if (!cmd) return;
    cmd->svg_subscene = *subscene;
}

void paint_glyph_run(PaintList* pl, const PaintGlyphRun* glyph_run) {
    if (!glyph_run) return;
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_GLYPH_RUN);
    if (!cmd) return;
    cmd->glyph_run = *glyph_run;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

typedef struct PaintIrRasterEffectFrame {
    PaintEffectGroup group;
} PaintIrRasterEffectFrame;

#define PAINT_IR_RASTER_EFFECT_STACK_MAX 64

static const RdtMatrix* paint_optional_transform(bool has_transform,
                                                 const RdtMatrix* transform) {
    return has_transform ? transform : nullptr;
}

static const Bound* paint_optional_clip(bool has_clip, const Bound* clip) {
    return has_clip ? clip : nullptr;
}

static bool paint_ir_effect_bounds_to_region(const Bound* bounds,
                                             int* x0, int* y0,
                                             int* w, int* h) {
    if (!bounds || !x0 || !y0 || !w || !h) return false;
    float left = floorf(bounds->left);
    float top = floorf(bounds->top);
    float right = ceilf(bounds->right);
    float bottom = ceilf(bounds->bottom);
    if (right <= left || bottom <= top) return false;
    *x0 = (int)left; // INT_CAST_OK: effect bounds lower to integer display-list pixel regions.
    *y0 = (int)top; // INT_CAST_OK: effect bounds lower to integer display-list pixel regions.
    *w = (int)(right - left); // INT_CAST_OK: effect bounds lower to integer display-list pixel regions.
    *h = (int)(bottom - top); // INT_CAST_OK: effect bounds lower to integer display-list pixel regions.
    return *w > 0 && *h > 0;
}

static void paint_ir_lower_raster_effect_begin(const PaintEffectGroup* group,
                                               DisplayList* dl) {
    if (!group || !dl) return;
    int x0 = 0, y0 = 0, w = 0, h = 0;
    if (!paint_ir_effect_bounds_to_region(&group->bounds, &x0, &y0, &w, &h)) {
        return;
    }
    if (group->backdrop_filter) {
        Bound clip = group->has_clip ? group->bounds : Bound{};
        dl_apply_filter(dl,
                        group->bounds.left,
                        group->bounds.top,
                        group->bounds.right - group->bounds.left,
                        group->bounds.bottom - group->bounds.top,
                        group->backdrop_filter,
                        paint_optional_clip(group->has_clip, &clip));
    }
    if (group->blend_mode) {
        dl_save_backdrop(dl, x0, y0, w, h);
    }
    if (group->opacity < 0.9995f) {
        dl_save_backdrop(dl, x0, y0, w, h);
    }
    if (group->backdrop) {
        dl_save_backdrop(dl, x0, y0, w, h);
    }
}

static void paint_ir_lower_raster_effect_finish(const PaintEffectGroup* group,
                                                DisplayList* dl) {
    if (!group || !dl) return;

    if (group->filter) {
        Bound clip = group->has_clip ? group->bounds : Bound{};
        dl_apply_filter(dl,
                        group->bounds.left,
                        group->bounds.top,
                        group->bounds.right - group->bounds.left,
                        group->bounds.bottom - group->bounds.top,
                        group->filter,
                        paint_optional_clip(group->has_clip, &clip));
    }

    int x0 = 0, y0 = 0, w = 0, h = 0;
    if (!paint_ir_effect_bounds_to_region(&group->bounds, &x0, &y0, &w, &h)) {
        return;
    }
    if (group->backdrop) {
        dl_composite_opacity(dl, x0, y0, w, h, 1.0f, false);
    }
    if (group->opacity < 0.9995f) {
        dl_composite_opacity(dl, x0, y0, w, h, group->opacity, false);
    }
    if (group->blend_mode) {
        dl_apply_blend_mode(dl, x0, y0, w, h, group->blend_mode);
    }
}

static bool paint_ir_lower_raster_effect_stack_command(const PaintCmd* cmd, DisplayList* dl, PaintIrRasterEffectFrame* effect_stack, int* effect_depth, int command_index) {
    if (!paint_op_has_flags(cmd->op, PAINT_OP_FLAG_EFFECT_STACK)) return false;
    if (paint_op_has_flags(cmd->op, PAINT_OP_FLAG_STACK_PUSH)) {
        const PaintEffectGroup* p = &cmd->effect_group;
        if (*effect_depth >= PAINT_IR_RASTER_EFFECT_STACK_MAX) {
            log_error("[PAINT_IR_EFFECT] raster effect stack overflow at command %d", command_index);
            return true;
        }
        effect_stack[(*effect_depth)++].group = *p;
        paint_ir_lower_raster_effect_begin(p, dl);
        return true;
    }
    if (*effect_depth <= 0) {
        log_error("[PAINT_IR_EFFECT] raster effect stack underflow at command %d", command_index);
        return true;
    }
    PaintIrRasterEffectFrame* frame = &effect_stack[--(*effect_depth)];
    paint_ir_lower_raster_effect_finish(&frame->group, dl);
    return true;
}

static void paint_ir_lower_raster_internal(const PaintList* pl, DisplayList* dl) {
    if (!pl || !dl) return;

    PaintIrRasterEffectFrame effect_stack[PAINT_IR_RASTER_EFFECT_STACK_MAX];
    int effect_depth = 0;

    for (int i = 0; i < pl->count; i++) {
        const PaintCmd* cmd = &pl->cmds[i];
        if (paint_op_has_flags(cmd->op, PAINT_OP_FLAG_RASTER_NOOP)) {
            continue;
        }
        if (paint_ir_lower_raster_effect_stack_command(cmd, dl, effect_stack, &effect_depth, i)) continue;
        switch (cmd->op) {
        case PAINT_FILL_RECT: {
            const PaintFillRect* p = &cmd->fill_rect;
            dl_fill_rect(dl, p->x, p->y, p->w, p->h, p->color);
            break;
        }
        case PAINT_FILL_ROUNDED_RECT: {
            const PaintFillRoundedRect* p = &cmd->fill_rounded_rect;
            dl_fill_rounded_rect(dl, p->x, p->y, p->w, p->h, p->rx, p->ry, p->color);
            break;
        }
        case PAINT_FILL_PATH: {
            const PaintFillPath* p = &cmd->fill_path;
            dl_fill_path(dl, p->path, p->color, p->rule,
                         paint_optional_transform(p->has_transform, &p->transform));
            break;
        }
        case PAINT_STROKE_PATH: {
            const PaintStrokePath* p = &cmd->stroke_path;
            dl_stroke_path(dl, p->path, p->color, p->width, p->cap, p->join,
                           p->dash_array, p->dash_count, p->dash_phase,
                           paint_optional_transform(p->has_transform, &p->transform));
            break;
        }
        case PAINT_FILL_LINEAR_GRADIENT: {
            const PaintFillLinearGradient* p = &cmd->fill_linear_gradient;
            dl_fill_linear_gradient(dl, p->path, p->x1, p->y1, p->x2, p->y2,
                                    p->stops, p->stop_count, p->rule,
                                    paint_optional_transform(p->has_transform, &p->transform),
                                    paint_optional_transform(p->has_gradient_transform,
                                                             &p->gradient_transform));
            break;
        }
        case PAINT_FILL_RADIAL_GRADIENT: {
            const PaintFillRadialGradient* p = &cmd->fill_radial_gradient;
            dl_fill_radial_gradient(dl, p->path, p->cx, p->cy, p->r,
                                    p->stops, p->stop_count, p->rule,
                                    paint_optional_transform(p->has_transform, &p->transform),
                                    paint_optional_transform(p->has_gradient_transform,
                                                             &p->gradient_transform));
            break;
        }
        case PAINT_DRAW_IMAGE: {
            const PaintDrawImage* p = &cmd->draw_image;
            ImageSurface* owner = (ImageSurface*)p->resource_owner;
            dl_draw_image(dl, p->pixels, p->src_w, p->src_h, p->src_stride,
                          p->dst_x, p->dst_y, p->dst_w, p->dst_h, p->opacity,
                          paint_optional_transform(p->has_transform, &p->transform),
                          owner, owner ? owner->generation : 0);
            break;
        }
        case PAINT_DRAW_IMAGE_RESOURCE: {
            const PaintDrawImageResource* p = &cmd->draw_image_resource;
            const uint32_t* pixels = nullptr;
            int src_w = 0;
            int src_h = 0;
            int src_stride = 0;
            if (paint_ir_image_resource_pixels(p->image, p->dst_w, p->dst_h,
                                               &pixels, &src_w, &src_h,
                                               &src_stride)) {
                dl_draw_image(dl, pixels, src_w, src_h, src_stride,
                              p->dst_x, p->dst_y, p->dst_w, p->dst_h,
                              p->opacity,
                              paint_optional_transform(p->has_transform, &p->transform),
                              p->image, p->image->generation);
            }
            break;
        }
        case PAINT_DRAW_GLYPH: {
            const PaintDrawGlyph* p = &cmd->draw_glyph;
            dl_draw_glyph(dl, (GlyphBitmap*)&p->bitmap, p->x, p->y,
                          p->color, p->is_color_emoji,
                          paint_optional_clip(p->has_clip, &p->clip),
                          paint_optional_transform(p->has_transform, &p->transform),
                          p->resource_generation);
            break;
        }
        case PAINT_DRAW_PICTURE: {
            const PaintDrawPicture* p = &cmd->draw_picture;
            dl_draw_picture(dl, p->picture, p->opacity,
                            paint_optional_transform(p->has_transform, &p->transform));
            break;
        }
        case PAINT_VIDEO_PLACEHOLDER: {
            const PaintVideoPlaceholder* p = &cmd->video_placeholder;
            dl_video_placeholder(dl, p->video, p->dst_x, p->dst_y, p->dst_w, p->dst_h,
                                 p->object_fit, paint_optional_clip(p->has_clip, &p->clip),
                                 p->video_generation);
            break;
        }
        case PAINT_WEBVIEW_LAYER_PLACEHOLDER: {
            const PaintWebviewLayerPlaceholder* p = &cmd->webview_layer_placeholder;
            dl_webview_layer_placeholder(dl, p->surface,
                                         p->dst_x, p->dst_y, p->dst_w, p->dst_h,
                                         paint_optional_clip(p->has_clip, &p->clip),
                                         p->surface_generation);
            break;
        }
        case PAINT_PUSH_CLIP: {
            const PaintPushClip* p = &cmd->push_clip;
            dl_push_clip(dl, p->clip_path,
                         paint_optional_transform(p->has_transform, &p->transform));
            break;
        }
        case PAINT_POP_CLIP:
            dl_pop_clip(dl);
            break;
        case PAINT_SAVE_BACKDROP: {
            const PaintSaveBackdrop* p = &cmd->save_backdrop;
            dl_save_backdrop(dl, p->x0, p->y0, p->w, p->h);
            break;
        }
        case PAINT_COMPOSITE_OPACITY: {
            const PaintCompositeOpacity* p = &cmd->composite_opacity;
            dl_composite_opacity(dl, p->x0, p->y0, p->w, p->h,
                                 p->opacity, p->premultiplied_source);
            break;
        }
        case PAINT_APPLY_BLEND_MODE: {
            const PaintApplyBlendMode* p = &cmd->apply_blend_mode;
            dl_apply_blend_mode(dl, p->x0, p->y0, p->w, p->h, p->blend_mode);
            break;
        }
        case PAINT_APPLY_FILTER: {
            const PaintApplyFilter* p = &cmd->apply_filter;
            dl_apply_filter(dl, p->x, p->y, p->w, p->h, p->filter,
                            paint_optional_clip(p->has_clip, &p->clip));
            break;
        }
        case PAINT_BOX_BLUR_REGION: {
            const PaintBoxBlurRegion* p = &cmd->box_blur_region;
            dl_box_blur_region(dl, p->rx, p->ry, p->rw, p->rh, p->blur_radius,
                               p->clip_type, p->clip_params,
                               p->exclude_type, p->exclude_params,
                               p->premultiply_source, p->tint_source, p->tint_color);
            break;
        }
        case PAINT_BOX_BLUR_INSET: {
            const PaintBoxBlurInset* p = &cmd->box_blur_inset;
            dl_box_blur_inset(dl, p->rx, p->ry, p->rw, p->rh,
                              p->pad, p->blur_radius, p->bg_color);
            break;
        }
        case PAINT_SHADOW_CLIP_SAVE: {
            const PaintShadowClipSave* p = &cmd->shadow_clip_save;
            dl_shadow_clip_save(dl, p->rx, p->ry, p->rw, p->rh);
            break;
        }
        case PAINT_SHADOW_CLIP_RESTORE: {
            const PaintShadowClipRestore* p = &cmd->shadow_clip_restore;
            dl_shadow_clip_restore(dl, p->exclude_type, p->exclude_params,
                                   p->save_rx, p->save_ry, p->save_rw, p->save_rh,
                                   p->restore_inside);
            break;
        }
        case PAINT_OUTER_SHADOW: {
            const PaintOuterShadow* p = &cmd->outer_shadow;
            dl_outer_shadow(dl,
                            p->shadow_x, p->shadow_y, p->shadow_w, p->shadow_h,
                            p->sr_tl, p->sr_tr, p->sr_br, p->sr_bl,
                            p->color, p->blur_radius,
                            p->exclude_type, p->exclude_params,
                            p->clip_type, p->clip_params);
            break;
        }
        case PAINT_FILL_SURFACE_RECT: {
            const PaintFillSurfaceRect* p = &cmd->fill_surface_rect;
            dl_fill_surface_rect(dl, p->x, p->y, p->w, p->h,
                                 p->color, paint_optional_clip(p->has_clip, &p->clip),
                                 p->clip_shapes, p->clip_depth);
            break;
        }
        case PAINT_BLIT_SURFACE_SCALED: {
            const PaintBlitSurfaceScaled* p = &cmd->blit_surface_scaled;
            dl_blit_surface_scaled(dl, p->src_surface,
                                   p->dst_x, p->dst_y, p->dst_w, p->dst_h,
                                   p->scale_mode, paint_optional_clip(p->has_clip, &p->clip),
                                   p->clip_shapes, p->clip_depth,
                                   p->opacity, p->src_generation);
            break;
        }

        case PAINT_SVG_SUBSCENE: {
            const PaintSvgSubscene* p = &cmd->svg_subscene;
            if (g_svg_subscene_raster_lowerer) {
                g_svg_subscene_raster_lowerer(p, dl);
            } else {
                log_error("[PAINT_IR_SVG_SUBSCENE] raster lowerer is not registered");
            }
            break;
        }

        case PAINT_GLYPH_RUN:
            if (g_glyph_run_raster_lowerer) {
                g_glyph_run_raster_lowerer(&cmd->glyph_run, dl);
            }
            break;
        case PAINT_OP_COUNT:
        default:
            log_error("[PAINT_IR_RASTER] unsupported paint op %d at command %d",
                      (int)cmd->op, i);
            break;
        }
    }
}

void paint_ir_lower_raster(const PaintList* pl, DisplayList* dl) {
    if (!pl || !dl) return;
    if (!paint_ir_validate_or_log(pl, "paint_ir_lower_raster")) return;
    paint_ir_lower_raster_internal(pl, dl);
}

void paint_ir_lower_raster_fragment(const PaintList* pl, DisplayList* dl) {
    paint_ir_lower_raster_internal(pl, dl);
}

// ---------------------------------------------------------------------------
// SVG lowering: PaintIR -> SVG fragment
// ---------------------------------------------------------------------------

static void paint_svg_indent(StrBuf* out, int indent_level) {
    for (int i = 0; i < indent_level; i++) {
        strbuf_append_str(out, "  ");
    }
}

void paint_svg_color_to_string(Color color, char* result, int result_cap) {
    if (!result || result_cap <= 0) return;
    if (color.a == 0) {
        str_copy(result, result_cap, "transparent", 11);
    } else if (color.a == 255) {
        str_fmt(result, result_cap, "rgb(%d,%d,%d)", color.r, color.g, color.b);
    } else {
        str_fmt(result, result_cap, "rgba(%d,%d,%d,%.3f)",
                color.r, color.g, color.b, color.a / 255.0f);
    }
}

void paint_svg_append_color(StrBuf* out, Color color) {
    char color_str[32];
    paint_svg_color_to_string(color, color_str, sizeof(color_str));
    strbuf_append_str(out, color_str);
}

static void paint_svg_append_attr_escaped(StrBuf* out, const char* value) {
    if (!out || !value) return;
    escape_append(out, value, strlen(value), ESCAPE_RULES_XML_ATTR,
                  ESCAPE_RULES_XML_ATTR_COUNT, ESCAPE_CTRL_XML_NUMERIC);
}

static void paint_svg_append_text_escaped(StrBuf* out, const char* value, int len) {
    if (!out || !value) return;
    if (len < 0) len = (int)strlen(value); // INT_CAST_OK: text run byte length is bounded by source string.
    escape_append(out, value, (size_t)len, ESCAPE_RULES_HTML_TEXT,
                  ESCAPE_RULES_HTML_TEXT_COUNT, ESCAPE_CTRL_NONE);
}

static void paint_svg_append_matrix_attr(StrBuf* out, const RdtMatrix* matrix) {
    if (!matrix) return;
    strbuf_append_format(out, " transform=\"matrix(%.6g %.6g %.6g %.6g %.6g %.6g)\"",
                         matrix->e11, matrix->e21, matrix->e12,
                         matrix->e22, matrix->e13, matrix->e23);
}

static void paint_svg_append_named_matrix_attr(StrBuf* out, const char* name,
                                               const RdtMatrix* matrix) {
    if (!matrix || !name) return;
    strbuf_append_format(out, " %s=\"matrix(%.6g %.6g %.6g %.6g %.6g %.6g)\"",
                         name, matrix->e11, matrix->e21, matrix->e12,
                         matrix->e22, matrix->e13, matrix->e23);
}

static const char* paint_svg_stroke_cap_name(RdtStrokeCap cap) {
    switch (cap) {
    case RDT_CAP_ROUND: return "round";
    case RDT_CAP_SQUARE: return "square";
    case RDT_CAP_BUTT:
    default:
        return "butt";
    }
}

static const char* paint_svg_stroke_join_name(RdtStrokeJoin join) {
    switch (join) {
    case RDT_JOIN_ROUND: return "round";
    case RDT_JOIN_BEVEL: return "bevel";
    case RDT_JOIN_MITER:
    default:
        return "miter";
    }
}

static void paint_svg_append_rounded_rect_path(StrBuf* out,
                                               float x, float y, float w, float h,
                                               float rx, float ry) {
    if (rx < 0.0f) rx = 0.0f;
    if (ry < 0.0f) ry = 0.0f;
    float half_w = w * 0.5f;
    float half_h = h * 0.5f;
    if (rx > half_w) rx = half_w;
    if (ry > half_h) ry = half_h;

    if (rx <= 0.0f && ry <= 0.0f) {
        strbuf_append_format(out,
            "M%.2f,%.2f L%.2f,%.2f L%.2f,%.2f L%.2f,%.2f Z",
            x, y, x + w, y, x + w, y + h, x, y + h);
        return;
    }

    strbuf_append_format(out,
        "M%.2f,%.2f L%.2f,%.2f "
        "A%.2f,%.2f 0 0 1 %.2f,%.2f "
        "L%.2f,%.2f "
        "A%.2f,%.2f 0 0 1 %.2f,%.2f "
        "L%.2f,%.2f "
        "A%.2f,%.2f 0 0 1 %.2f,%.2f "
        "L%.2f,%.2f "
        "A%.2f,%.2f 0 0 1 %.2f,%.2f Z",
        x + rx, y, x + w - rx, y,
        rx, ry, x + w, y + ry,
        x + w, y + h - ry,
        rx, ry, x + w - rx, y + h,
        x + rx, y + h,
        rx, ry, x, y + h - ry,
        x, y + ry,
        rx, ry, x + rx, y);
}

static void paint_svg_append_filled_rect(StrBuf* out, int indent_level,
                                         float x, float y, float w, float h,
                                         float rx, float ry, bool rounded,
                                         Color color) {
    paint_svg_indent(out, indent_level);
    if (rounded) {
        strbuf_append_format(out,
            "<rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" rx=\"%.2f\" ry=\"%.2f\" fill=\"",
            x, y, w, h, rx, ry);
    } else {
        strbuf_append_format(out,
            "<rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" fill=\"",
            x, y, w, h);
    }
    paint_svg_append_color(out, color);
    strbuf_append_str(out, "\" />\n");
}

typedef struct PaintSvgPathContext {
    StrBuf* out;
    bool has_command;
} PaintSvgPathContext;

static bool paint_svg_path_visit(void* context, RdtPathCommand command,
                                 const float* args, int arg_count) {
    PaintSvgPathContext* ctx = (PaintSvgPathContext*)context;
    if (!ctx || !ctx->out) return false;
    ctx->has_command = true;

    switch (command) {
    case RDT_PATH_MOVE:
        if (arg_count < 2) return false;
        strbuf_append_format(ctx->out, "M%.2f,%.2f ", args[0], args[1]);
        break;
    case RDT_PATH_LINE:
        if (arg_count < 2) return false;
        strbuf_append_format(ctx->out, "L%.2f,%.2f ", args[0], args[1]);
        break;
    case RDT_PATH_QUAD:
        if (arg_count < 4) return false;
        strbuf_append_format(ctx->out, "Q%.2f,%.2f %.2f,%.2f ",
                             args[0], args[1], args[2], args[3]);
        break;
    case RDT_PATH_CUBIC:
        if (arg_count < 6) return false;
        strbuf_append_format(ctx->out, "C%.2f,%.2f %.2f,%.2f %.2f,%.2f ",
                             args[0], args[1], args[2], args[3], args[4], args[5]);
        break;
    case RDT_PATH_CLOSE:
        strbuf_append_str(ctx->out, "Z ");
        break;
    case RDT_PATH_RECT:
        if (arg_count < 6) return false;
        paint_svg_append_rounded_rect_path(ctx->out,
                                           args[0], args[1], args[2], args[3],
                                           args[4], args[5]);
        strbuf_append_char(ctx->out, ' ');
        break;
    case RDT_PATH_CIRCLE:
        if (arg_count < 4) return false;
        strbuf_append_format(ctx->out,
            "M%.2f,%.2f A%.2f,%.2f 0 1 0 %.2f,%.2f "
            "A%.2f,%.2f 0 1 0 %.2f,%.2f Z ",
            args[0] - args[2], args[1], args[2], args[3],
            args[0] + args[2], args[1],
            args[2], args[3], args[0] - args[2], args[1]);
        break;
    }
    return true;
}

static bool paint_svg_path_to_string(RdtPath* path, StrBuf* out) {
    if (!path || !out) return false;
    PaintSvgPathContext ctx = {};
    ctx.out = out;
    if (!rdt_path_visit(path, paint_svg_path_visit, &ctx)) return false;
    return ctx.has_command;
}

static void paint_svg_note_unsupported(StrBuf* out, int indent_level, PaintOp op,
                                       bool emit_comment,
                                       PaintSvgLoweringStats* stats) {
    stats->unsupported_count++;
    if (!emit_comment) return;
    paint_svg_indent(out, indent_level);
    strbuf_append_format(out, "<!-- unsupported %s -->\n", paint_op_name(op));
}

static bool paint_svg_caps_allow_rect(const RenderExportTargetCaps* caps) {
    return caps && caps->rects;
}

static bool paint_svg_caps_allow_rounded_rect(const RenderExportTargetCaps* caps) {
    return caps && caps->rounded_rects;
}

static bool paint_svg_caps_allow_path(const RenderExportTargetCaps* caps,
                                      bool has_transform) {
    if (!caps || !caps->paths) return false;
    return !has_transform || caps->transforms;
}

static bool paint_svg_caps_allow_stroke(const RenderExportTargetCaps* caps,
                                        bool has_transform) {
    if (!caps || !caps->paths || !caps->strokes) return false;
    return !has_transform || caps->transforms;
}

static bool paint_svg_caps_allow_gradient(const RenderExportTargetCaps* caps,
                                          bool has_transform) {
    if (!caps || !caps->paths || !caps->gradients) return false;
    return !has_transform || caps->transforms;
}

static bool paint_svg_caps_allow_image(const RenderExportTargetCaps* caps,
                                       bool has_transform) {
    if (!caps || !caps->images) return false;
    return !has_transform || caps->transforms;
}

static bool paint_svg_caps_allow_clip(const RenderExportTargetCaps* caps,
                                      bool has_transform) {
    if (!caps || !caps->paths || !caps->clips) return false;
    return !has_transform || caps->transforms;
}

static bool paint_svg_caps_allow_glyph_run(const RenderExportTargetCaps* caps,
                                           const PaintGlyphRun* run) {
    if (!caps || !caps->glyph_runs || !run || !run->text) return false;
    return !run->has_transform || caps->transforms;
}

static bool paint_svg_caps_allow_opacity_group(const RenderExportTargetCaps* caps,
                                               const PaintEffectGroup* group) {
    if (!caps || !caps->opacity_groups || !group) return false;
    return !group->has_clip &&
           (!group->has_transform || caps->transforms) &&
           group->blend_mode == 0 &&
           group->filter == nullptr &&
           !group->backdrop &&
           group->backdrop_filter == nullptr &&
           !group->shadow &&
           !group->isolation;
}

static void paint_svg_append_gradient_stops(StrBuf* out,
                                            const RdtGradientStop* stops,
                                            int stop_count,
                                            int indent_level) {
    for (int stop_i = 0; stop_i < stop_count; stop_i++) {
        const RdtGradientStop* stop = &stops[stop_i];
        paint_svg_indent(out, indent_level);
        strbuf_append_format(out,
            "<stop offset=\"%.4f\" stop-color=\"rgb(%d,%d,%d)\"",
            stop->offset, stop->r, stop->g, stop->b);
        if (stop->a < 255) {
            strbuf_append_format(out, " stop-opacity=\"%.4f\"", stop->a / 255.0f);
        }
        strbuf_append_str(out, " />\n");
    }
}

static void paint_svg_append_gradient_fill_path(StrBuf* out,
                                                int indent_level,
                                                const char* path_data,
                                                const char* gradient_name,
                                                int gradient_id,
                                                int fill_rule,
                                                const RdtMatrix* transform) {
    paint_svg_indent(out, indent_level);
    strbuf_append_format(out, "<path d=\"%s\" fill=\"url(#paint-ir-%s-%d)\"",
                         path_data, gradient_name, gradient_id);
    if (fill_rule == RDT_FILL_EVEN_ODD) {
        strbuf_append_str(out, " fill-rule=\"evenodd\"");
    }
    paint_svg_append_matrix_attr(out, transform);
    strbuf_append_str(out, " />\n");
}

void paint_svg_lowering_state_init(PaintSvgLoweringState* state, int indent_level) {
    if (!state) return;
    memset(state, 0, sizeof(PaintSvgLoweringState));
    state->indent_level = indent_level;
}

static void paint_ir_lower_svg_unchecked(const PaintList* pl, StrBuf* out,
                                         const PaintSvgLoweringOptions* options,
                                         PaintSvgLoweringState* state,
                                         PaintSvgLoweringStats* stats) {
    if (stats) {
        memset(stats, 0, sizeof(PaintSvgLoweringStats));
    }
    if (!pl || !out) return;

    PaintSvgLoweringStats local_stats = {};
    PaintSvgLoweringStats* active_stats = stats ? stats : &local_stats;
    PaintSvgLoweringState local_state = {};
    if (!state) {
        paint_svg_lowering_state_init(&local_state,
                                      options ? options->indent_level : 0);
        state = &local_state;
    }

    int indent_level = state->indent_level;
    bool emit_unsupported_comments = options ? options->emit_unsupported_comments : false;
    int resource_id_base = options ? options->resource_id_base : 0;
    const RenderExportTargetCaps* caps = options && options->caps
        ? options->caps
        : render_export_target_get_caps(RENDER_EXPORT_TARGET_SVG);
    int open_clip_depth = state->open_clip_depth;
    int skipped_clip_depth = state->skipped_clip_depth;
    int open_transform_depth = state->open_transform_depth;
    int skipped_transform_depth = state->skipped_transform_depth;
    int open_effect_depth = state->open_effect_depth;
    int skipped_effect_depth = state->skipped_effect_depth;
    auto note_unsupported = [&](PaintOp op) {
        paint_svg_note_unsupported(out, indent_level, op,
                                   emit_unsupported_comments, active_stats);
    };
    auto skip_nested_push = [&](int* skipped_depth, PaintOp op) -> bool {
        if (*skipped_depth <= 0) return false;
        (*skipped_depth)++;
        note_unsupported(op);
        return true;
    };
    auto skip_nested_pop = [&](int* skipped_depth, PaintOp op) -> bool {
        if (*skipped_depth <= 0) return false;
        (*skipped_depth)--;
        note_unsupported(op);
        return true;
    };
    auto close_svg_group = [&](int* open_depth, PaintOp op) -> bool {
        if (*open_depth <= 0) {
            note_unsupported(op);
            return false;
        }
        (*open_depth)--;
        indent_level--;
        paint_svg_indent(out, indent_level);
        strbuf_append_str(out, "</g>\n");
        active_stats->emitted_count++;
        return true;
    };
    auto path_data_or_note = [&](RdtPath* path, PaintOp op) -> StrBuf* {
        StrBuf* path_data = strbuf_new();
        if (!paint_svg_path_to_string(path, path_data)) {
            strbuf_free(path_data);
            note_unsupported(op);
            return nullptr;
        }
        return path_data;
    };

    for (int i = 0; i < pl->count; i++) {
        const PaintCmd* cmd = &pl->cmds[i];
        active_stats->command_count++;
        if (skipped_effect_depth > 0 &&
            !paint_op_has_flags(cmd->op, PAINT_OP_FLAG_EFFECT_STACK)) {
            note_unsupported(cmd->op);
            continue;
        }
        switch (cmd->op) {
        case PAINT_FILL_RECT: {
            if (!paint_svg_caps_allow_rect(caps)) {
                note_unsupported(cmd->op);
                break;
            }
            const PaintFillRect* p = &cmd->fill_rect;
            paint_svg_append_filled_rect(out, indent_level,
                                         p->x, p->y, p->w, p->h,
                                         0.0f, 0.0f, false, p->color);
            active_stats->emitted_count++;
            break;
        }
        case PAINT_FILL_ROUNDED_RECT: {
            if (!paint_svg_caps_allow_rounded_rect(caps)) {
                note_unsupported(cmd->op);
                break;
            }
            const PaintFillRoundedRect* p = &cmd->fill_rounded_rect;
            paint_svg_append_filled_rect(out, indent_level,
                                         p->x, p->y, p->w, p->h,
                                         p->rx, p->ry, true, p->color);
            active_stats->emitted_count++;
            break;
        }
        case PAINT_FILL_PATH: {
            const PaintFillPath* p = &cmd->fill_path;
            if (!paint_svg_caps_allow_path(caps, p->has_transform)) {
                note_unsupported(cmd->op);
                break;
            }
            StrBuf* path_data = path_data_or_note(p->path, cmd->op);
            if (!path_data) break;

            paint_svg_indent(out, indent_level);
            strbuf_append_format(out, "<path d=\"%s\" fill=\"", path_data->str);
            paint_svg_append_color(out, p->color);
            strbuf_append_char(out, '"');
            if (p->rule == RDT_FILL_EVEN_ODD) {
                strbuf_append_str(out, " fill-rule=\"evenodd\"");
            }
            paint_svg_append_matrix_attr(out,
                                         paint_optional_transform(p->has_transform, &p->transform));
            strbuf_append_str(out, " />\n");
            strbuf_free(path_data);
            active_stats->emitted_count++;
            break;
        }
        case PAINT_STROKE_PATH: {
            const PaintStrokePath* p = &cmd->stroke_path;
            if (!paint_svg_caps_allow_stroke(caps, p->has_transform)) {
                note_unsupported(cmd->op);
                break;
            }
            StrBuf* path_data = path_data_or_note(p->path, cmd->op);
            if (!path_data) break;

            paint_svg_indent(out, indent_level);
            strbuf_append_format(out, "<path d=\"%s\" fill=\"none\" stroke=\"", path_data->str);
            paint_svg_append_color(out, p->color);
            strbuf_append_format(out,
                "\" stroke-width=\"%.2f\" stroke-linecap=\"%s\" stroke-linejoin=\"%s\"",
                p->width, paint_svg_stroke_cap_name(p->cap),
                paint_svg_stroke_join_name(p->join));
            if (p->dash_array && p->dash_count > 0) {
                strbuf_append_str(out, " stroke-dasharray=\"");
                for (int dash_i = 0; dash_i < p->dash_count; dash_i++) {
                    if (dash_i > 0) strbuf_append_char(out, ' ');
                    strbuf_append_format(out, "%.2f", p->dash_array[dash_i]);
                }
                strbuf_append_char(out, '"');
                if (p->dash_phase != 0.0f) {
                    strbuf_append_format(out, " stroke-dashoffset=\"%.2f\"", p->dash_phase);
                }
            }
            paint_svg_append_matrix_attr(out,
                                         paint_optional_transform(p->has_transform, &p->transform));
            strbuf_append_str(out, " />\n");
            strbuf_free(path_data);
            active_stats->emitted_count++;
            break;
        }
        case PAINT_FILL_LINEAR_GRADIENT: {
            const PaintFillLinearGradient* p = &cmd->fill_linear_gradient;
            if (!paint_svg_caps_allow_gradient(caps, p->has_transform || p->has_gradient_transform) ||
                !p->stops || p->stop_count <= 0) {
                note_unsupported(cmd->op);
                break;
            }

            StrBuf* path_data = path_data_or_note(p->path, cmd->op);
            if (!path_data) break;

            int gradient_id = resource_id_base + i;
            paint_svg_indent(out, indent_level);
            strbuf_append_format(out,
                "<defs><linearGradient id=\"paint-ir-linear-%d\" gradientUnits=\"userSpaceOnUse\" "
                "x1=\"%.3f\" y1=\"%.3f\" x2=\"%.3f\" y2=\"%.3f\"",
                gradient_id, p->x1, p->y1, p->x2, p->y2);
            paint_svg_append_named_matrix_attr(out, "gradientTransform",
                                               paint_optional_transform(p->has_gradient_transform,
                                                                        &p->gradient_transform));
            strbuf_append_str(out, ">\n");
            paint_svg_append_gradient_stops(out, p->stops, p->stop_count,
                                            indent_level + 1);
            paint_svg_indent(out, indent_level);
            strbuf_append_str(out, "</linearGradient></defs>\n");

            paint_svg_append_gradient_fill_path(
                out, indent_level, path_data->str, "linear", gradient_id, p->rule,
                paint_optional_transform(p->has_transform, &p->transform));
            strbuf_free(path_data);
            active_stats->emitted_count++;
            break;
        }
        case PAINT_FILL_RADIAL_GRADIENT: {
            const PaintFillRadialGradient* p = &cmd->fill_radial_gradient;
            if (!paint_svg_caps_allow_gradient(caps, p->has_transform || p->has_gradient_transform) ||
                !p->stops || p->stop_count <= 0) {
                note_unsupported(cmd->op);
                break;
            }

            StrBuf* path_data = path_data_or_note(p->path, cmd->op);
            if (!path_data) break;

            int gradient_id = resource_id_base + i;
            paint_svg_indent(out, indent_level);
            strbuf_append_format(out,
                "<defs><radialGradient id=\"paint-ir-radial-%d\" gradientUnits=\"userSpaceOnUse\" "
                "cx=\"%.3f\" cy=\"%.3f\" r=\"%.3f\"",
                gradient_id, p->cx, p->cy, p->r);
            paint_svg_append_named_matrix_attr(out, "gradientTransform",
                                               paint_optional_transform(p->has_gradient_transform,
                                                                        &p->gradient_transform));
            strbuf_append_str(out, ">\n");
            paint_svg_append_gradient_stops(out, p->stops, p->stop_count,
                                            indent_level + 1);
            paint_svg_indent(out, indent_level);
            strbuf_append_str(out, "</radialGradient></defs>\n");

            paint_svg_append_gradient_fill_path(
                out, indent_level, path_data->str, "radial", gradient_id, p->rule,
                paint_optional_transform(p->has_transform, &p->transform));
            strbuf_free(path_data);
            active_stats->emitted_count++;
            break;
        }
        case PAINT_DRAW_IMAGE_RESOURCE: {
            const PaintDrawImageResource* p = &cmd->draw_image_resource;
            ImageSurface* image = p->image;
            const char* href = (image && image->url && image->url->href)
                ? image->url->href->chars
                : nullptr;
            if (!href || !paint_svg_caps_allow_image(caps, p->has_transform)) {
                note_unsupported(cmd->op);
                break;
            }

            paint_svg_indent(out, indent_level);
            strbuf_append_format(out,
                "<image x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" href=\"",
                p->dst_x, p->dst_y, p->dst_w, p->dst_h);
            paint_svg_append_attr_escaped(out, href);
            strbuf_append_str(out, "\" preserveAspectRatio=\"none\"");
            if (p->opacity < 255) {
                strbuf_append_format(out, " opacity=\"%.4f\"", p->opacity / 255.0f);
            }
            paint_svg_append_matrix_attr(out,
                                         paint_optional_transform(p->has_transform, &p->transform));
            strbuf_append_str(out, " />\n");
            active_stats->emitted_count++;
            break;
        }
        case PAINT_PUSH_TRANSFORM: {
            if (skip_nested_push(&skipped_transform_depth, cmd->op)) break;
            if (!caps || !caps->transforms) {
                skipped_transform_depth++;
                note_unsupported(cmd->op);
                break;
            }
            const PaintPushTransform* p = &cmd->push_transform;
            paint_svg_indent(out, indent_level);
            strbuf_append_str(out, "<g");
            paint_svg_append_matrix_attr(out, &p->transform);
            strbuf_append_str(out, ">\n");
            open_transform_depth++;
            indent_level++;
            active_stats->emitted_count++;
            break;
        }
        case PAINT_POP_TRANSFORM: {
            if (skip_nested_pop(&skipped_transform_depth, cmd->op)) break;
            close_svg_group(&open_transform_depth, cmd->op);
            break;
        }
        case PAINT_PUSH_CLIP: {
            const PaintPushClip* p = &cmd->push_clip;
            if (skip_nested_push(&skipped_clip_depth, cmd->op)) break;
            if (!paint_svg_caps_allow_clip(caps, p->has_transform)) {
                skipped_clip_depth++;
                note_unsupported(cmd->op);
                break;
            }
            StrBuf* path_data = path_data_or_note(p->clip_path, cmd->op);
            if (!path_data) {
                skipped_clip_depth++;
                break;
            }

            int clip_id = resource_id_base + i;
            paint_svg_indent(out, indent_level);
            strbuf_append_format(out,
                "<defs><clipPath id=\"paint-ir-clip-%d\"><path d=\"%s\"",
                clip_id, path_data->str);
            paint_svg_append_matrix_attr(out,
                                         paint_optional_transform(p->has_transform, &p->transform));
            strbuf_append_str(out, " /></clipPath></defs>\n");
            paint_svg_indent(out, indent_level);
            strbuf_append_format(out, "<g clip-path=\"url(#paint-ir-clip-%d)\">\n", clip_id);
            strbuf_free(path_data);
            open_clip_depth++;
            indent_level++;
            active_stats->emitted_count++;
            break;
        }
        case PAINT_POP_CLIP: {
            if (skip_nested_pop(&skipped_clip_depth, cmd->op)) break;
            close_svg_group(&open_clip_depth, cmd->op);
            break;
        }
        case PAINT_GLYPH_RUN: {
            const PaintGlyphRun* p = &cmd->glyph_run;
            if (!paint_svg_caps_allow_glyph_run(caps, p)) {
                note_unsupported(cmd->op);
                break;
            }
            paint_svg_indent(out, indent_level);
            strbuf_append_format(out,
                "<text x=\"%.2f\" y=\"%.2f\" font-family=\"",
                p->x, p->baseline_y);
            paint_svg_append_attr_escaped(out, p->font_family ? p->font_family : "Arial");
            strbuf_append_format(out, "\" font-size=\"%.2f\" fill=\"",
                                 p->font_size > 0.0f ? p->font_size : 16.0f);
            paint_svg_append_color(out, p->color);
            strbuf_append_char(out, '"');
            if (p->font_weight > 0 && p->font_weight != 400) {
                strbuf_append_format(out, " font-weight=\"%d\"", p->font_weight);
            }
            if (p->italic) {
                strbuf_append_str(out, " font-style=\"italic\"");
            }
            if (p->word_spacing > 0.01f) {
                strbuf_append_format(out, " word-spacing=\"%.2f\"", p->word_spacing);
            }
            paint_svg_append_matrix_attr(out,
                                         paint_optional_transform(p->has_transform, &p->transform));
            strbuf_append_char(out, '>');
            paint_svg_append_text_escaped(out, p->text, p->text_len);
            strbuf_append_str(out, "</text>\n");
            active_stats->emitted_count++;
            break;
        }
        case PAINT_SVG_SUBSCENE: {
            const PaintSvgSubscene* p = &cmd->svg_subscene;
            if (!g_svg_subscene_svg_lowerer ||
                !g_svg_subscene_svg_lowerer(p, out, indent_level)) {
                note_unsupported(cmd->op);
                break;
            }
            active_stats->emitted_count++;
            break;
        }
        case PAINT_BEGIN_EFFECT_GROUP: {
            if (skip_nested_push(&skipped_effect_depth, cmd->op)) break;
            const PaintEffectGroup* p = &cmd->effect_group;
            if (!paint_svg_caps_allow_opacity_group(caps, p)) {
                log_error("[SVG_PAINT_IR] fallback effect group opacity=%.3f blend=%d filter=%p backdrop=%d backdrop_filter=%p shadow=%d isolation=%d",
                          p ? p->opacity : 1.0f,
                          p ? p->blend_mode : 0,
                          p ? p->filter : nullptr,
                          p && p->backdrop ? 1 : 0,
                          p ? p->backdrop_filter : nullptr,
                          p && p->shadow ? 1 : 0,
                          p && p->isolation ? 1 : 0);

                paint_svg_indent(out, indent_level);
                strbuf_append_str(out, "<g data-radiant-fallback=\"effect\"");
                if (p && p->opacity < 0.9995f) {
                    strbuf_append_format(out, " opacity=\"%.4f\"", p->opacity);
                }
                if (p && p->has_transform && caps && caps->transforms) {
                    paint_svg_append_matrix_attr(out, &p->transform);
                }
                strbuf_append_str(out, ">\n");
                open_effect_depth++;
                indent_level++;
                active_stats->emitted_count++;
                active_stats->fallback_count++;
                break;
            }
            paint_svg_indent(out, indent_level);
            strbuf_append_str(out, "<g");
            if (p->opacity < 0.9995f) {
                strbuf_append_format(out, " opacity=\"%.4f\"", p->opacity);
            }
            if (p->has_transform) {
                paint_svg_append_matrix_attr(out, &p->transform);
            }
            strbuf_append_str(out, ">\n");
            open_effect_depth++;
            indent_level++;
            active_stats->emitted_count++;
            break;
        }
        case PAINT_END_EFFECT_GROUP: {
            if (skip_nested_pop(&skipped_effect_depth, cmd->op)) break;
            close_svg_group(&open_effect_depth, cmd->op);
            break;
        }
        default:
            note_unsupported(cmd->op);
            break;
        }
    }

    state->indent_level = indent_level;
    state->open_clip_depth = open_clip_depth;
    state->skipped_clip_depth = skipped_clip_depth;
    state->open_transform_depth = open_transform_depth;
    state->skipped_transform_depth = skipped_transform_depth;
    state->open_effect_depth = open_effect_depth;
    state->skipped_effect_depth = skipped_effect_depth;
}

void paint_ir_lower_svg(const PaintList* pl, StrBuf* out,
                        const PaintSvgLoweringOptions* options,
                        PaintSvgLoweringStats* stats) {
    if (stats) {
        memset(stats, 0, sizeof(PaintSvgLoweringStats));
    }
    if (!pl || !out) return;
    if (!paint_ir_validate_or_log(pl, "paint_ir_lower_svg")) return;

    PaintSvgLoweringState state = {};
    paint_svg_lowering_state_init(&state, options ? options->indent_level : 0);
    paint_ir_lower_svg_unchecked(pl, out, options, &state, stats);
}

void paint_ir_lower_svg_stream(const PaintList* pl, StrBuf* out,
                               const PaintSvgLoweringOptions* options,
                               PaintSvgLoweringState* state,
                               PaintSvgLoweringStats* stats) {
    if (stats) {
        memset(stats, 0, sizeof(PaintSvgLoweringStats));
    }
    if (!pl || !out || !state) return;
    paint_ir_lower_svg_unchecked(pl, out, options, state, stats);
}
