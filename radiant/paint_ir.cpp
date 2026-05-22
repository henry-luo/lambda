// ==========================================================================
// PaintIR — recording (PaintBuilder) and raster lowering (PaintIR -> DisplayList).
//
// See paint_ir.h for the model. Step 1 lowers the vector primitive ops 1:1 to
// the matching dl_* command so raster output is byte-for-byte identical to
// recording those dl_* calls directly.
// ==========================================================================

#include "paint_ir.h"
#include "../lib/memtrack.h"
#include <string.h>

#define PAINT_LIST_INITIAL_CAPACITY 1024

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

static PaintCmd* paint_alloc_cmd(PaintList* pl, PaintOp op) {
    if (!pl) return nullptr;
    if (pl->count >= pl->capacity) {
        int new_cap = pl->capacity ? pl->capacity * 2 : PAINT_LIST_INITIAL_CAPACITY;
        pl->cmds = (PaintCmd*)mem_realloc(pl->cmds, new_cap * sizeof(PaintCmd), MEM_CAT_RENDER);
        pl->capacity = new_cap;
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
                                RdtFillRule rule, const RdtMatrix* transform) {
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
}

void paint_fill_radial_gradient(PaintList* pl, RdtPath* path,
                                float cx, float cy, float r,
                                const RdtGradientStop* stops, int stop_count,
                                RdtFillRule rule, const RdtMatrix* transform) {
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

// ---------------------------------------------------------------------------
// Raster lowering: PaintIR -> DisplayList
// ---------------------------------------------------------------------------

void paint_ir_lower_raster(const PaintList* pl, DisplayList* dl) {
    if (!pl || !dl) return;

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
                                    p->has_transform ? &p->transform : nullptr);
            break;
        }
        case PAINT_FILL_RADIAL_GRADIENT: {
            const PaintFillRadialGradient* p = &cmd->fill_radial_gradient;
            dl_fill_radial_gradient(dl, p->path, p->cx, p->cy, p->r,
                                    p->stops, p->stop_count, p->rule,
                                    p->has_transform ? &p->transform : nullptr);
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

        // Higher-level semantic ops are lowered in later phases (E: effects,
        // F: inline SVG). They are not yet emitted by the live render path.
        case PAINT_GLYPH_RUN:
        case PAINT_BEGIN_EFFECT_GROUP:
        case PAINT_END_EFFECT_GROUP:
        case PAINT_SVG_SUBSCENE:
            break;
        }
    }
}
