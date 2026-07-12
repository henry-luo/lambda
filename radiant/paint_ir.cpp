// ==========================================================================
// PaintIR — recording (PaintBuilder) and raster lowering (PaintIR -> DisplayList).
//
// See paint_ir.h for the model. Step 1 lowers the vector primitive ops 1:1 to
// the matching dl_* command so raster output is byte-for-byte identical to
// recording those dl_* calls directly.
// ==========================================================================

#include "paint_ir.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/mem_grow.hpp"
#include "../lib/escape.h"
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

void paint_list_clear(PaintList* pl) {
    if (!pl) return;
    pl->count = 0;
}

void paint_list_destroy(PaintList* pl) {
    if (!pl) return;
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

static bool paint_ir_validation_fail(PaintIrValidationResult* result, int index,
                                     const char* message,
                                     int clip_depth, int backdrop_depth,
                                     int shadow_clip_depth, int effect_depth) {
    paint_ir_validation_set(result, false, index, message,
                            clip_depth, backdrop_depth,
                            shadow_clip_depth, effect_depth);
    return false;
}

static bool paint_ir_validate_positive_image(const PaintDrawImage* image) {
    return image && image->pixels &&
           image->src_w > 0 && image->src_h > 0 && image->src_stride > 0 &&
           image->dst_w >= 0.0f && image->dst_h >= 0.0f;
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
        return paint_ir_validation_fail(result, -1, "paint list is null",
                                        0, 0, 0, 0);
    }

    int clip_depth = 0;
    int backdrop_depth = 0;
    int shadow_clip_depth = 0;
    int effect_depth = 0;
    int transform_depth = 0;

    for (int i = 0; i < pl->count; i++) {
        const PaintCmd* cmd = &pl->cmds[i];
        switch (cmd->op) {
        case PAINT_FILL_RECT:
            if (cmd->fill_rect.w < 0.0f || cmd->fill_rect.h < 0.0f) {
                return paint_ir_validation_fail(result, i, "fill rect has negative size",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_FILL_ROUNDED_RECT:
            if (cmd->fill_rounded_rect.w < 0.0f ||
                cmd->fill_rounded_rect.h < 0.0f ||
                cmd->fill_rounded_rect.rx < 0.0f ||
                cmd->fill_rounded_rect.ry < 0.0f) {
                return paint_ir_validation_fail(result, i, "rounded rect has negative size",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_FILL_PATH:
            if (!cmd->fill_path.path) {
                return paint_ir_validation_fail(result, i, "fill path is null",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_STROKE_PATH:
            if (!cmd->stroke_path.path || cmd->stroke_path.width < 0.0f ||
                cmd->stroke_path.dash_count < 0 ||
                (cmd->stroke_path.dash_count > 0 && !cmd->stroke_path.dash_array)) {
                return paint_ir_validation_fail(result, i, "stroke path payload is invalid",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_FILL_LINEAR_GRADIENT:
            if (!cmd->fill_linear_gradient.path ||
                !cmd->fill_linear_gradient.stops ||
                cmd->fill_linear_gradient.stop_count <= 0) {
                return paint_ir_validation_fail(result, i, "linear gradient payload is invalid",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_FILL_RADIAL_GRADIENT:
            if (!cmd->fill_radial_gradient.path ||
                !cmd->fill_radial_gradient.stops ||
                cmd->fill_radial_gradient.stop_count <= 0 ||
                cmd->fill_radial_gradient.r < 0.0f) {
                return paint_ir_validation_fail(result, i, "radial gradient payload is invalid",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_DRAW_IMAGE:
            if (!paint_ir_validate_positive_image(&cmd->draw_image)) {
                return paint_ir_validation_fail(result, i, "draw image payload is invalid",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_DRAW_IMAGE_RESOURCE:
            if (!cmd->draw_image_resource.image ||
                cmd->draw_image_resource.dst_w < 0.0f ||
                cmd->draw_image_resource.dst_h < 0.0f) {
                return paint_ir_validation_fail(result, i, "image resource payload is invalid",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_DRAW_GLYPH:
            if (!cmd->draw_glyph.bitmap.buffer ||
                cmd->draw_glyph.bitmap.width <= 0 ||
                cmd->draw_glyph.bitmap.height <= 0 ||
                cmd->draw_glyph.bitmap.pitch <= 0) {
                return paint_ir_validation_fail(result, i, "glyph payload is invalid",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_DRAW_PICTURE:
            if (!cmd->draw_picture.picture) {
                return paint_ir_validation_fail(result, i, "picture payload is invalid",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_VIDEO_PLACEHOLDER:
            if (!cmd->video_placeholder.video ||
                cmd->video_placeholder.dst_w < 0.0f ||
                cmd->video_placeholder.dst_h < 0.0f) {
                return paint_ir_validation_fail(result, i, "video placeholder payload is invalid",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_WEBVIEW_LAYER_PLACEHOLDER:
            if (!cmd->webview_layer_placeholder.surface ||
                cmd->webview_layer_placeholder.dst_w < 0.0f ||
                cmd->webview_layer_placeholder.dst_h < 0.0f) {
                return paint_ir_validation_fail(result, i, "webview placeholder payload is invalid",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_PUSH_CLIP:
            if (!cmd->push_clip.clip_path) {
                return paint_ir_validation_fail(result, i, "clip path is null",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            clip_depth++;
            break;
        case PAINT_POP_CLIP:
            if (clip_depth <= 0) {
                return paint_ir_validation_fail(result, i, "clip pop without matching push",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            clip_depth--;
            break;
        case PAINT_PUSH_TRANSFORM:
            transform_depth++;
            break;
        case PAINT_POP_TRANSFORM:
            if (transform_depth <= 0) {
                return paint_ir_validation_fail(result, i, "transform pop without matching push",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            transform_depth--;
            break;
        case PAINT_SAVE_BACKDROP:
            if (cmd->save_backdrop.w < 0 || cmd->save_backdrop.h < 0) {
                return paint_ir_validation_fail(result, i, "saved backdrop has negative region",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            backdrop_depth++;
            break;
        case PAINT_COMPOSITE_OPACITY:
            if (backdrop_depth <= 0) {
                return paint_ir_validation_fail(result, i, "opacity composite without saved backdrop",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            if (cmd->composite_opacity.w < 0 || cmd->composite_opacity.h < 0) {
                return paint_ir_validation_fail(result, i, "opacity composite has negative region",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            backdrop_depth--;
            break;
        case PAINT_APPLY_BLEND_MODE:
            if (backdrop_depth <= 0) {
                return paint_ir_validation_fail(result, i, "blend mode without saved backdrop",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            if (cmd->apply_blend_mode.w < 0 || cmd->apply_blend_mode.h < 0) {
                return paint_ir_validation_fail(result, i, "blend mode has negative region",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            backdrop_depth--;
            break;
        case PAINT_APPLY_FILTER:
            if (!cmd->apply_filter.filter ||
                cmd->apply_filter.w < 0.0f ||
                cmd->apply_filter.h < 0.0f) {
                return paint_ir_validation_fail(result, i, "filter payload is invalid",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_BOX_BLUR_REGION:
            if (cmd->box_blur_region.rw < 0 ||
                cmd->box_blur_region.rh < 0 ||
                cmd->box_blur_region.blur_radius < 0.0f) {
                return paint_ir_validation_fail(result, i, "box blur region payload is invalid",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_BOX_BLUR_INSET:
            if (cmd->box_blur_inset.rw < 0 ||
                cmd->box_blur_inset.rh < 0 ||
                cmd->box_blur_inset.pad < 0 ||
                cmd->box_blur_inset.blur_radius < 0.0f) {
                return paint_ir_validation_fail(result, i, "inset blur payload is invalid",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_SHADOW_CLIP_SAVE:
            if (cmd->shadow_clip_save.rw < 0 ||
                cmd->shadow_clip_save.rh < 0) {
                return paint_ir_validation_fail(result, i, "shadow clip save has negative region",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            shadow_clip_depth++;
            break;
        case PAINT_SHADOW_CLIP_RESTORE:
            if (shadow_clip_depth <= 0) {
                return paint_ir_validation_fail(result, i, "shadow clip restore without save",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            if (cmd->shadow_clip_restore.save_rw < 0 ||
                cmd->shadow_clip_restore.save_rh < 0) {
                return paint_ir_validation_fail(result, i, "shadow clip restore has negative region",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            shadow_clip_depth--;
            break;
        case PAINT_OUTER_SHADOW:
            if (cmd->outer_shadow.shadow_w < 0.0f ||
                cmd->outer_shadow.shadow_h < 0.0f ||
                cmd->outer_shadow.blur_radius < 0.0f) {
                return paint_ir_validation_fail(result, i, "outer shadow payload is invalid",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_FILL_SURFACE_RECT:
            if (cmd->fill_surface_rect.w < 0.0f ||
                cmd->fill_surface_rect.h < 0.0f) {
                return paint_ir_validation_fail(result, i, "surface fill has negative size",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_BLIT_SURFACE_SCALED:
            if (!cmd->blit_surface_scaled.src_surface ||
                cmd->blit_surface_scaled.dst_w < 0.0f ||
                cmd->blit_surface_scaled.dst_h < 0.0f) {
                return paint_ir_validation_fail(result, i, "surface blit payload is invalid",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_BEGIN_EFFECT_GROUP:
            if (cmd->effect_group.bounds.right < cmd->effect_group.bounds.left ||
                cmd->effect_group.bounds.bottom < cmd->effect_group.bounds.top ||
                cmd->effect_group.opacity < 0.0f ||
                cmd->effect_group.opacity > 1.0f) {
                return paint_ir_validation_fail(result, i, "effect group payload is invalid",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            effect_depth++;
            break;
        case PAINT_END_EFFECT_GROUP:
            if (effect_depth <= 0) {
                return paint_ir_validation_fail(result, i, "effect group end without begin",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            effect_depth--;
            break;
        case PAINT_GLYPH_RUN:
            if (cmd->glyph_run.text) {
                if (cmd->glyph_run.text_len < -1 ||
                    cmd->glyph_run.font_size < 0.0f) {
                    return paint_ir_validation_fail(result, i, "text run payload is invalid",
                                                    clip_depth, backdrop_depth,
                                                    shadow_clip_depth, effect_depth);
                }
            } else if (!cmd->glyph_run.glyph_ids || !cmd->glyph_run.xs ||
                       !cmd->glyph_run.ys || cmd->glyph_run.count <= 0) {
                return paint_ir_validation_fail(result, i, "glyph run payload is invalid",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_SVG_SUBSCENE:
            if (!cmd->svg_subscene.svg_root ||
                cmd->svg_subscene.viewport_width <= 0.0f ||
                cmd->svg_subscene.viewport_height <= 0.0f) {
                return paint_ir_validation_fail(result, i, "svg subscene root is null",
                                                clip_depth, backdrop_depth,
                                                shadow_clip_depth, effect_depth);
            }
            break;
        case PAINT_OP_COUNT:
        default:
            return paint_ir_validation_fail(result, i, "unknown paint op",
                                            clip_depth, backdrop_depth,
                                            shadow_clip_depth, effect_depth);
        }
    }

    if (clip_depth != 0) {
        return paint_ir_validation_fail(result, pl->count, "clip stack is unbalanced",
                                        clip_depth, backdrop_depth,
                                        shadow_clip_depth, effect_depth);
    }
    if (backdrop_depth != 0) {
        return paint_ir_validation_fail(result, pl->count, "backdrop stack is unbalanced",
                                        clip_depth, backdrop_depth,
                                        shadow_clip_depth, effect_depth);
    }
    if (shadow_clip_depth != 0) {
        return paint_ir_validation_fail(result, pl->count, "shadow clip stack is unbalanced",
                                        clip_depth, backdrop_depth,
                                        shadow_clip_depth, effect_depth);
    }
    if (effect_depth != 0) {
        return paint_ir_validation_fail(result, pl->count, "effect group stack is unbalanced",
                                        clip_depth, backdrop_depth,
                                        shadow_clip_depth, effect_depth);
    }
    if (transform_depth != 0) {
        return paint_ir_validation_fail(result, pl->count, "transform stack is unbalanced",
                                        clip_depth, backdrop_depth,
                                        shadow_clip_depth, effect_depth);
    }

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
    cmd->fill_path.has_transform = transform != nullptr;
    if (transform) cmd->fill_path.transform = *transform;
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
    cmd->stroke_path.has_transform = transform != nullptr;
    if (transform) cmd->stroke_path.transform = *transform;
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
    cmd->fill_linear_gradient.has_transform = transform != nullptr;
    if (transform) cmd->fill_linear_gradient.transform = *transform;
    cmd->fill_linear_gradient.has_gradient_transform = gradient_transform != nullptr;
    if (gradient_transform) cmd->fill_linear_gradient.gradient_transform = *gradient_transform;
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
    cmd->fill_radial_gradient.has_transform = transform != nullptr;
    if (transform) cmd->fill_radial_gradient.transform = *transform;
    cmd->fill_radial_gradient.has_gradient_transform = gradient_transform != nullptr;
    if (gradient_transform) cmd->fill_radial_gradient.gradient_transform = *gradient_transform;
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
    cmd->draw_image.has_transform = transform != nullptr;
    if (transform) cmd->draw_image.transform = *transform;
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
    cmd->draw_image_resource.has_transform = transform != nullptr;
    if (transform) cmd->draw_image_resource.transform = *transform;
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
    cmd->draw_glyph.has_clip = clip != nullptr;
    if (clip) cmd->draw_glyph.clip = *clip;
    cmd->draw_glyph.has_transform = transform != nullptr;
    if (transform) cmd->draw_glyph.transform = *transform;
    cmd->draw_glyph.resource_generation = resource_generation;
}

void paint_draw_picture(PaintList* pl, RdtPicture* picture,
                        uint8_t opacity, const RdtMatrix* transform) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_DRAW_PICTURE);
    if (!cmd) return;
    cmd->draw_picture.picture = picture;
    cmd->draw_picture.opacity = opacity;
    cmd->draw_picture.has_transform = transform != nullptr;
    if (transform) cmd->draw_picture.transform = *transform;
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
    cmd->video_placeholder.has_clip = clip != nullptr;
    if (clip) cmd->video_placeholder.clip = *clip;
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
    cmd->webview_layer_placeholder.has_clip = clip != nullptr;
    if (clip) cmd->webview_layer_placeholder.clip = *clip;
    cmd->webview_layer_placeholder.surface_generation = surface_generation;
}

void paint_push_clip(PaintList* pl, RdtPath* clip_path, const RdtMatrix* transform) {
    PaintCmd* cmd = paint_alloc_cmd(pl, PAINT_PUSH_CLIP);
    if (!cmd) return;
    cmd->push_clip.clip_path = clip_path;
    cmd->push_clip.has_transform = transform != nullptr;
    if (transform) cmd->push_clip.transform = *transform;
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
    cmd->apply_filter.has_clip = clip != nullptr;
    if (clip) cmd->apply_filter.clip = *clip;
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
    cmd->fill_surface_rect.has_clip = clip != nullptr;
    if (clip) cmd->fill_surface_rect.clip = *clip;
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
    cmd->blit_surface_scaled.has_clip = clip != nullptr;
    if (clip) cmd->blit_surface_scaled.clip = *clip;
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
// Raster lowering: PaintIR -> DisplayList
// ---------------------------------------------------------------------------

typedef struct PaintIrRasterEffectFrame {
    PaintEffectGroup group;
} PaintIrRasterEffectFrame;

#define PAINT_IR_RASTER_EFFECT_STACK_MAX 64

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
                        group->has_clip ? &clip : nullptr);
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
                        group->has_clip ? &clip : nullptr);
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

static void paint_ir_lower_raster_internal(const PaintList* pl, DisplayList* dl) {
    if (!pl || !dl) return;

    PaintIrRasterEffectFrame effect_stack[PAINT_IR_RASTER_EFFECT_STACK_MAX];
    int effect_depth = 0;

    for (int i = 0; i < pl->count; i++) {
        const PaintCmd* cmd = &pl->cmds[i];
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
                         p->has_transform ? &p->transform : nullptr);
            break;
        }
        case PAINT_STROKE_PATH: {
            const PaintStrokePath* p = &cmd->stroke_path;
            dl_stroke_path(dl, p->path, p->color, p->width, p->cap, p->join,
                           p->dash_array, p->dash_count, p->dash_phase,
                           p->has_transform ? &p->transform : nullptr);
            break;
        }
        case PAINT_FILL_LINEAR_GRADIENT: {
            const PaintFillLinearGradient* p = &cmd->fill_linear_gradient;
            dl_fill_linear_gradient(dl, p->path, p->x1, p->y1, p->x2, p->y2,
                                    p->stops, p->stop_count, p->rule,
                                    p->has_transform ? &p->transform : nullptr,
                                    p->has_gradient_transform ? &p->gradient_transform : nullptr);
            break;
        }
        case PAINT_FILL_RADIAL_GRADIENT: {
            const PaintFillRadialGradient* p = &cmd->fill_radial_gradient;
            dl_fill_radial_gradient(dl, p->path, p->cx, p->cy, p->r,
                                    p->stops, p->stop_count, p->rule,
                                    p->has_transform ? &p->transform : nullptr,
                                    p->has_gradient_transform ? &p->gradient_transform : nullptr);
            break;
        }
        case PAINT_DRAW_IMAGE: {
            const PaintDrawImage* p = &cmd->draw_image;
            ImageSurface* owner = (ImageSurface*)p->resource_owner;
            dl_draw_image(dl, p->pixels, p->src_w, p->src_h, p->src_stride,
                          p->dst_x, p->dst_y, p->dst_w, p->dst_h, p->opacity,
                          p->has_transform ? &p->transform : nullptr,
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
                              p->has_transform ? &p->transform : nullptr,
                              p->image, p->image->generation);
            }
            break;
        }
        case PAINT_DRAW_GLYPH: {
            const PaintDrawGlyph* p = &cmd->draw_glyph;
            dl_draw_glyph(dl, (GlyphBitmap*)&p->bitmap, p->x, p->y,
                          p->color, p->is_color_emoji,
                          p->has_clip ? &p->clip : nullptr,
                          p->has_transform ? &p->transform : nullptr,
                          p->resource_generation);
            break;
        }
        case PAINT_DRAW_PICTURE: {
            const PaintDrawPicture* p = &cmd->draw_picture;
            dl_draw_picture(dl, p->picture, p->opacity,
                            p->has_transform ? &p->transform : nullptr);
            break;
        }
        case PAINT_VIDEO_PLACEHOLDER: {
            const PaintVideoPlaceholder* p = &cmd->video_placeholder;
            dl_video_placeholder(dl, p->video, p->dst_x, p->dst_y, p->dst_w, p->dst_h,
                                 p->object_fit, p->has_clip ? &p->clip : nullptr,
                                 p->video_generation);
            break;
        }
        case PAINT_WEBVIEW_LAYER_PLACEHOLDER: {
            const PaintWebviewLayerPlaceholder* p = &cmd->webview_layer_placeholder;
            dl_webview_layer_placeholder(dl, p->surface,
                                         p->dst_x, p->dst_y, p->dst_w, p->dst_h,
                                         p->has_clip ? &p->clip : nullptr,
                                         p->surface_generation);
            break;
        }
        case PAINT_PUSH_CLIP: {
            const PaintPushClip* p = &cmd->push_clip;
            dl_push_clip(dl, p->clip_path, p->has_transform ? &p->transform : nullptr);
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
                            p->has_clip ? &p->clip : nullptr);
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
                                 p->color, p->has_clip ? &p->clip : nullptr,
                                 p->clip_shapes, p->clip_depth);
            break;
        }
        case PAINT_BLIT_SURFACE_SCALED: {
            const PaintBlitSurfaceScaled* p = &cmd->blit_surface_scaled;
            dl_blit_surface_scaled(dl, p->src_surface,
                                   p->dst_x, p->dst_y, p->dst_w, p->dst_h,
                                   p->scale_mode, p->has_clip ? &p->clip : nullptr,
                                   p->clip_shapes, p->clip_depth,
                                   p->opacity, p->src_generation);
            break;
        }

        case PAINT_BEGIN_EFFECT_GROUP: {
            const PaintEffectGroup* p = &cmd->effect_group;
            if (effect_depth < PAINT_IR_RASTER_EFFECT_STACK_MAX) {
                effect_stack[effect_depth++].group = *p;
                paint_ir_lower_raster_effect_begin(p, dl);
            } else {
                log_error("[PAINT_IR_EFFECT] raster effect stack overflow at command %d", i);
            }
            break;
        }
        case PAINT_END_EFFECT_GROUP: {
            if (effect_depth > 0) {
                PaintIrRasterEffectFrame* frame = &effect_stack[--effect_depth];
                paint_ir_lower_raster_effect_finish(&frame->group, dl);
            } else {
                log_error("[PAINT_IR_EFFECT] raster effect stack underflow at command %d", i);
            }
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

        // Other higher-level semantic ops are target-neutral; the raster lowering
        // only consumes their pixel-domain expansions during this migration.
        case PAINT_PUSH_TRANSFORM:
        case PAINT_POP_TRANSFORM:
            break;
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

static void paint_svg_append_color(StrBuf* out, Color color) {
    if (color.a == 0) {
        strbuf_append_str(out, "transparent");
    } else if (color.a == 255) {
        strbuf_append_format(out, "rgb(%d,%d,%d)", color.r, color.g, color.b);
    } else {
        strbuf_append_format(out, "rgba(%d,%d,%d,%.3f)",
                             color.r, color.g, color.b, color.a / 255.0f);
    }
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

static const char* paint_op_name(PaintOp op) {
    static const char* names[] = {
#define PAINT_OP_NAME_ENTRY(op) #op,
        PAINT_OP_LIST(PAINT_OP_NAME_ENTRY)
#undef PAINT_OP_NAME_ENTRY
    };
    static_assert((int)(sizeof(names) / sizeof(names[0])) == (int)PAINT_OP_COUNT,
                  "PaintOp name table must match PAINT_OP_LIST");
    int op_index = (int)op;
    if (op_index < 0 || op_index >= (int)PAINT_OP_COUNT) return "PAINT_UNKNOWN";
    return names[op_index];
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

    for (int i = 0; i < pl->count; i++) {
        const PaintCmd* cmd = &pl->cmds[i];
        active_stats->command_count++;
        if (skipped_effect_depth > 0 &&
            cmd->op != PAINT_BEGIN_EFFECT_GROUP &&
            cmd->op != PAINT_END_EFFECT_GROUP) {
            paint_svg_note_unsupported(out, indent_level, cmd->op,
                                       emit_unsupported_comments, active_stats);
            continue;
        }
        switch (cmd->op) {
        case PAINT_FILL_RECT: {
            if (!paint_svg_caps_allow_rect(caps)) {
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }
            const PaintFillRect* p = &cmd->fill_rect;
            paint_svg_indent(out, indent_level);
            strbuf_append_format(out,
                "<rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" fill=\"",
                p->x, p->y, p->w, p->h);
            paint_svg_append_color(out, p->color);
            strbuf_append_str(out, "\" />\n");
            active_stats->emitted_count++;
            break;
        }
        case PAINT_FILL_ROUNDED_RECT: {
            if (!paint_svg_caps_allow_rounded_rect(caps)) {
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }
            const PaintFillRoundedRect* p = &cmd->fill_rounded_rect;
            paint_svg_indent(out, indent_level);
            strbuf_append_format(out,
                "<rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" rx=\"%.2f\" ry=\"%.2f\" fill=\"",
                p->x, p->y, p->w, p->h, p->rx, p->ry);
            paint_svg_append_color(out, p->color);
            strbuf_append_str(out, "\" />\n");
            active_stats->emitted_count++;
            break;
        }
        case PAINT_FILL_PATH: {
            const PaintFillPath* p = &cmd->fill_path;
            if (!paint_svg_caps_allow_path(caps, p->has_transform)) {
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }
            StrBuf* path_data = strbuf_new();
            if (!paint_svg_path_to_string(p->path, path_data)) {
                strbuf_free(path_data);
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }

            paint_svg_indent(out, indent_level);
            strbuf_append_format(out, "<path d=\"%s\" fill=\"", path_data->str);
            paint_svg_append_color(out, p->color);
            strbuf_append_char(out, '"');
            if (p->rule == RDT_FILL_EVEN_ODD) {
                strbuf_append_str(out, " fill-rule=\"evenodd\"");
            }
            paint_svg_append_matrix_attr(out, p->has_transform ? &p->transform : nullptr);
            strbuf_append_str(out, " />\n");
            strbuf_free(path_data);
            active_stats->emitted_count++;
            break;
        }
        case PAINT_STROKE_PATH: {
            const PaintStrokePath* p = &cmd->stroke_path;
            if (!paint_svg_caps_allow_stroke(caps, p->has_transform)) {
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }
            StrBuf* path_data = strbuf_new();
            if (!paint_svg_path_to_string(p->path, path_data)) {
                strbuf_free(path_data);
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }

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
            paint_svg_append_matrix_attr(out, p->has_transform ? &p->transform : nullptr);
            strbuf_append_str(out, " />\n");
            strbuf_free(path_data);
            active_stats->emitted_count++;
            break;
        }
        case PAINT_FILL_LINEAR_GRADIENT: {
            const PaintFillLinearGradient* p = &cmd->fill_linear_gradient;
            if (!paint_svg_caps_allow_gradient(caps, p->has_transform || p->has_gradient_transform) ||
                !p->stops || p->stop_count <= 0) {
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }

            StrBuf* path_data = strbuf_new();
            if (!paint_svg_path_to_string(p->path, path_data)) {
                strbuf_free(path_data);
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }

            int gradient_id = resource_id_base + i;
            paint_svg_indent(out, indent_level);
            strbuf_append_format(out,
                "<defs><linearGradient id=\"paint-ir-linear-%d\" gradientUnits=\"userSpaceOnUse\" "
                "x1=\"%.3f\" y1=\"%.3f\" x2=\"%.3f\" y2=\"%.3f\"",
                gradient_id, p->x1, p->y1, p->x2, p->y2);
            paint_svg_append_named_matrix_attr(out, "gradientTransform",
                                               p->has_gradient_transform ? &p->gradient_transform : nullptr);
            strbuf_append_str(out, ">\n");
            paint_svg_append_gradient_stops(out, p->stops, p->stop_count,
                                            indent_level + 1);
            paint_svg_indent(out, indent_level);
            strbuf_append_str(out, "</linearGradient></defs>\n");

            paint_svg_indent(out, indent_level);
            strbuf_append_format(out, "<path d=\"%s\" fill=\"url(#paint-ir-linear-%d)\"",
                                 path_data->str, gradient_id);
            if (p->rule == RDT_FILL_EVEN_ODD) {
                strbuf_append_str(out, " fill-rule=\"evenodd\"");
            }
            paint_svg_append_matrix_attr(out, p->has_transform ? &p->transform : nullptr);
            strbuf_append_str(out, " />\n");
            strbuf_free(path_data);
            active_stats->emitted_count++;
            break;
        }
        case PAINT_FILL_RADIAL_GRADIENT: {
            const PaintFillRadialGradient* p = &cmd->fill_radial_gradient;
            if (!paint_svg_caps_allow_gradient(caps, p->has_transform || p->has_gradient_transform) ||
                !p->stops || p->stop_count <= 0) {
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }

            StrBuf* path_data = strbuf_new();
            if (!paint_svg_path_to_string(p->path, path_data)) {
                strbuf_free(path_data);
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }

            int gradient_id = resource_id_base + i;
            paint_svg_indent(out, indent_level);
            strbuf_append_format(out,
                "<defs><radialGradient id=\"paint-ir-radial-%d\" gradientUnits=\"userSpaceOnUse\" "
                "cx=\"%.3f\" cy=\"%.3f\" r=\"%.3f\"",
                gradient_id, p->cx, p->cy, p->r);
            paint_svg_append_named_matrix_attr(out, "gradientTransform",
                                               p->has_gradient_transform ? &p->gradient_transform : nullptr);
            strbuf_append_str(out, ">\n");
            paint_svg_append_gradient_stops(out, p->stops, p->stop_count,
                                            indent_level + 1);
            paint_svg_indent(out, indent_level);
            strbuf_append_str(out, "</radialGradient></defs>\n");

            paint_svg_indent(out, indent_level);
            strbuf_append_format(out, "<path d=\"%s\" fill=\"url(#paint-ir-radial-%d)\"",
                                 path_data->str, gradient_id);
            if (p->rule == RDT_FILL_EVEN_ODD) {
                strbuf_append_str(out, " fill-rule=\"evenodd\"");
            }
            paint_svg_append_matrix_attr(out, p->has_transform ? &p->transform : nullptr);
            strbuf_append_str(out, " />\n");
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
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
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
            paint_svg_append_matrix_attr(out, p->has_transform ? &p->transform : nullptr);
            strbuf_append_str(out, " />\n");
            active_stats->emitted_count++;
            break;
        }
        case PAINT_PUSH_TRANSFORM: {
            if (skipped_transform_depth > 0) {
                skipped_transform_depth++;
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }
            if (!caps || !caps->transforms) {
                skipped_transform_depth++;
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
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
            if (skipped_transform_depth > 0) {
                skipped_transform_depth--;
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }
            if (open_transform_depth <= 0) {
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }
            open_transform_depth--;
            indent_level--;
            paint_svg_indent(out, indent_level);
            strbuf_append_str(out, "</g>\n");
            active_stats->emitted_count++;
            break;
        }
        case PAINT_PUSH_CLIP: {
            const PaintPushClip* p = &cmd->push_clip;
            if (skipped_clip_depth > 0) {
                skipped_clip_depth++;
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }
            if (!paint_svg_caps_allow_clip(caps, p->has_transform)) {
                skipped_clip_depth++;
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }
            StrBuf* path_data = strbuf_new();
            if (!paint_svg_path_to_string(p->clip_path, path_data)) {
                strbuf_free(path_data);
                skipped_clip_depth++;
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }

            int clip_id = resource_id_base + i;
            paint_svg_indent(out, indent_level);
            strbuf_append_format(out,
                "<defs><clipPath id=\"paint-ir-clip-%d\"><path d=\"%s\"",
                clip_id, path_data->str);
            paint_svg_append_matrix_attr(out, p->has_transform ? &p->transform : nullptr);
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
            if (skipped_clip_depth > 0) {
                skipped_clip_depth--;
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }
            if (open_clip_depth <= 0) {
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }
            open_clip_depth--;
            indent_level--;
            paint_svg_indent(out, indent_level);
            strbuf_append_str(out, "</g>\n");
            active_stats->emitted_count++;
            break;
        }
        case PAINT_GLYPH_RUN: {
            const PaintGlyphRun* p = &cmd->glyph_run;
            if (!paint_svg_caps_allow_glyph_run(caps, p)) {
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
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
            paint_svg_append_matrix_attr(out, p->has_transform ? &p->transform : nullptr);
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
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }
            active_stats->emitted_count++;
            break;
        }
        case PAINT_BEGIN_EFFECT_GROUP: {
            if (skipped_effect_depth > 0) {
                skipped_effect_depth++;
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }
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
            if (skipped_effect_depth > 0) {
                skipped_effect_depth--;
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }
            if (open_effect_depth <= 0) {
                paint_svg_note_unsupported(out, indent_level, cmd->op,
                                           emit_unsupported_comments, active_stats);
                break;
            }
            open_effect_depth--;
            indent_level--;
            paint_svg_indent(out, indent_level);
            strbuf_append_str(out, "</g>\n");
            active_stats->emitted_count++;
            break;
        }
        default:
            paint_svg_note_unsupported(out, indent_level, cmd->op,
                                       emit_unsupported_comments, active_stats);
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
