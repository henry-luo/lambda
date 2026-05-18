// ==========================================================================
// DisplayList — Recording and replay implementation.
// ==========================================================================

#include "display_list.h"
#include "render_filter.hpp"
#include "render_background.hpp"
#include "render_raster.hpp"
#include "display_list_storage.hpp"
#include "display_list_replay_glyph.hpp"
#include "display_list_replay_state.hpp"
#include "display_list_replay_backdrop.hpp"
#include "clip_shape.h"
#include "../lib/log.h"
#include <string.h>
#include <math.h>
#include <algorithm>

// ---------------------------------------------------------------------------
// Recording: rdt_* mirrors
// ---------------------------------------------------------------------------

void dl_fill_rect(DisplayList* dl, float x, float y, float w, float h, Color color) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_FILL_RECT;
    item->bounds[0] = x; item->bounds[1] = y; item->bounds[2] = w; item->bounds[3] = h;
    item->fill_rect = {x, y, w, h, color};
}

void dl_fill_rounded_rect(DisplayList* dl, float x, float y, float w, float h,
                          float rx, float ry, Color color) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_FILL_ROUNDED_RECT;
    item->bounds[0] = x; item->bounds[1] = y; item->bounds[2] = w; item->bounds[3] = h;
    item->fill_rounded_rect = {x, y, w, h, rx, ry, color};
}

void dl_fill_path(DisplayList* dl, RdtPath* path, Color color,
                  RdtFillRule rule, const RdtMatrix* transform) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_FILL_PATH;
    // bounds not computed for paths — set to zero (full-page for now)
    item->fill_path.path = rdt_path_clone(path);
    item->fill_path.color = color;
    item->fill_path.rule = rule;
    item->fill_path.has_transform = (transform != nullptr);
    if (transform) item->fill_path.transform = *transform;
}

void dl_stroke_path(DisplayList* dl, RdtPath* path, Color color, float width,
                    RdtStrokeCap cap, RdtStrokeJoin join,
                    const float* dash_array, int dash_count, float dash_phase,
                    const RdtMatrix* transform) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_STROKE_PATH;
    item->stroke_path.path = rdt_path_clone(path);
    item->stroke_path.color = color;
    item->stroke_path.width = width;
    item->stroke_path.cap = cap;
    item->stroke_path.join = join;
    item->stroke_path.dash_array = dl_copy_dashes(dl, dash_array, dash_count);
    item->stroke_path.dash_count = dash_count;
    item->stroke_path.dash_phase = dash_phase;
    item->stroke_path.has_transform = (transform != nullptr);
    if (transform) item->stroke_path.transform = *transform;
}

void dl_fill_linear_gradient(DisplayList* dl, RdtPath* path,
                             float x1, float y1, float x2, float y2,
                             const RdtGradientStop* stops, int stop_count,
                             RdtFillRule rule, const RdtMatrix* transform) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_FILL_LINEAR_GRADIENT;
    item->fill_linear_gradient.path = rdt_path_clone(path);
    item->fill_linear_gradient.x1 = x1;
    item->fill_linear_gradient.y1 = y1;
    item->fill_linear_gradient.x2 = x2;
    item->fill_linear_gradient.y2 = y2;
    item->fill_linear_gradient.stops = dl_copy_stops(dl, stops, stop_count);
    item->fill_linear_gradient.stop_count = stop_count;
    item->fill_linear_gradient.rule = rule;
    item->fill_linear_gradient.has_transform = (transform != nullptr);
    if (transform) item->fill_linear_gradient.transform = *transform;
}

void dl_fill_radial_gradient(DisplayList* dl, RdtPath* path,
                             float cx, float cy, float r,
                             const RdtGradientStop* stops, int stop_count,
                             RdtFillRule rule, const RdtMatrix* transform) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_FILL_RADIAL_GRADIENT;
    item->fill_radial_gradient.path = rdt_path_clone(path);
    item->fill_radial_gradient.cx = cx;
    item->fill_radial_gradient.cy = cy;
    item->fill_radial_gradient.r = r;
    item->fill_radial_gradient.stops = dl_copy_stops(dl, stops, stop_count);
    item->fill_radial_gradient.stop_count = stop_count;
    item->fill_radial_gradient.rule = rule;
    item->fill_radial_gradient.has_transform = (transform != nullptr);
    if (transform) item->fill_radial_gradient.transform = *transform;
}

void dl_draw_image(DisplayList* dl, const uint32_t* pixels,
                   int src_w, int src_h, int src_stride,
                   float dst_x, float dst_y, float dst_w, float dst_h,
                   uint8_t opacity, const RdtMatrix* transform) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_DRAW_IMAGE;
    item->bounds[0] = dst_x; item->bounds[1] = dst_y;
    item->bounds[2] = dst_w; item->bounds[3] = dst_h;
    item->draw_image.pixels = pixels;
    item->draw_image.src_w = src_w;
    item->draw_image.src_h = src_h;
    item->draw_image.src_stride = src_stride;
    item->draw_image.dst_x = dst_x;
    item->draw_image.dst_y = dst_y;
    item->draw_image.dst_w = dst_w;
    item->draw_image.dst_h = dst_h;
    item->draw_image.opacity = opacity;
    item->draw_image.has_transform = (transform != nullptr);
    if (transform) item->draw_image.transform = *transform;
}

void dl_draw_glyph(DisplayList* dl, GlyphBitmap* bitmap, int x, int y,
                   Color color, bool is_color_emoji, const Bound* clip,
                   const RdtMatrix* transform) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_DRAW_GLYPH;
    item->bounds[0] = (float)x; item->bounds[1] = (float)y;
    item->bounds[2] = (float)bitmap->width; item->bounds[3] = (float)bitmap->height;
    if (transform) {
        float x0 = (float)x, y0 = (float)y;
        float x1 = (float)(x + (int)bitmap->width), y1 = y0;
        float x2 = x1, y2 = (float)(y + (int)bitmap->height);
        float x3 = x0, y3 = y2;
        float tx0 = transform->e11 * x0 + transform->e12 * y0 + transform->e13;
        float ty0 = transform->e21 * x0 + transform->e22 * y0 + transform->e23;
        float tx1 = transform->e11 * x1 + transform->e12 * y1 + transform->e13;
        float ty1 = transform->e21 * x1 + transform->e22 * y1 + transform->e23;
        float tx2 = transform->e11 * x2 + transform->e12 * y2 + transform->e13;
        float ty2 = transform->e21 * x2 + transform->e22 * y2 + transform->e23;
        float tx3 = transform->e11 * x3 + transform->e12 * y3 + transform->e13;
        float ty3 = transform->e21 * x3 + transform->e22 * y3 + transform->e23;
        float min_x = std::min(std::min(tx0, tx1), std::min(tx2, tx3));
        float max_x = std::max(std::max(tx0, tx1), std::max(tx2, tx3));
        float min_y = std::min(std::min(ty0, ty1), std::min(ty2, ty3));
        float max_y = std::max(std::max(ty0, ty1), std::max(ty2, ty3));
        item->bounds[0] = floorf(min_x) - 1.0f;
        item->bounds[1] = floorf(min_y) - 1.0f;
        item->bounds[2] = ceilf(max_x - min_x) + 2.0f;
        item->bounds[3] = ceilf(max_y - min_y) + 2.0f;
    }
    item->draw_glyph.bitmap = *bitmap;  // copy descriptor, buffer pointer borrowed
    item->draw_glyph.x = x;
    item->draw_glyph.y = y;
    item->draw_glyph.color = color;
    item->draw_glyph.is_color_emoji = is_color_emoji;
    item->draw_glyph.has_transform = (transform != nullptr);
    if (transform) {
        item->draw_glyph.transform = *transform;
    }
    item->draw_glyph.clip = clip ? *clip : (Bound){0, 0, 99999, 99999};
}

void dl_draw_picture(DisplayList* dl, RdtPicture* picture,
                     uint8_t opacity, const RdtMatrix* transform) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_DRAW_PICTURE;
    item->draw_picture.picture = picture;  // ownership transferred to display list
    item->draw_picture.opacity = opacity;
    item->draw_picture.has_transform = (transform != nullptr);
    if (transform) item->draw_picture.transform = *transform;
}

void dl_push_clip(DisplayList* dl, RdtPath* clip_path, const RdtMatrix* transform) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_PUSH_CLIP;
    item->push_clip.path = rdt_path_clone(clip_path);
    item->push_clip.has_transform = (transform != nullptr);
    if (transform) item->push_clip.transform = *transform;
}

void dl_pop_clip(DisplayList* dl) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_POP_CLIP;
}

void dl_save_clip_depth(DisplayList* dl) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_SAVE_CLIP_DEPTH;
    // saved_depth filled by caller (or during replay)
}

void dl_restore_clip_depth(DisplayList* dl, int saved_depth) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_RESTORE_CLIP_DEPTH;
    item->clip_depth.saved_depth = saved_depth;
}

// ---------------------------------------------------------------------------
// Recording: direct-pixel operations
// ---------------------------------------------------------------------------

void dl_fill_surface_rect(DisplayList* dl, float x, float y, float w, float h,
                          uint32_t color, const Bound* clip,
                          ClipShape** clip_shapes, int clip_depth) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_FILL_SURFACE_RECT;
    item->bounds[0] = x; item->bounds[1] = y; item->bounds[2] = w; item->bounds[3] = h;
    item->fill_surface_rect.x = x;
    item->fill_surface_rect.y = y;
    item->fill_surface_rect.w = w;
    item->fill_surface_rect.h = h;
    item->fill_surface_rect.color = color;
    item->fill_surface_rect.clip = clip ? *clip : (Bound){0, 0, 99999, 99999};
    dl_store_clip_shapes(dl, &item->fill_surface_rect.clip_shapes, clip_shapes, clip_depth);
}

void dl_blit_surface_scaled(DisplayList* dl, void* src_surface,
                            float dst_x, float dst_y, float dst_w, float dst_h,
                            int scale_mode, const Bound* clip,
                            ClipShape** clip_shapes, int clip_depth, uint8_t opacity) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_BLIT_SURFACE_SCALED;
    item->bounds[0] = dst_x; item->bounds[1] = dst_y;
    item->bounds[2] = dst_w; item->bounds[3] = dst_h;
    item->blit_surface_scaled.src_surface = src_surface;
    item->blit_surface_scaled.dst_x = dst_x;
    item->blit_surface_scaled.dst_y = dst_y;
    item->blit_surface_scaled.dst_w = dst_w;
    item->blit_surface_scaled.dst_h = dst_h;
    item->blit_surface_scaled.scale_mode = scale_mode;
    item->blit_surface_scaled.opacity = opacity;
    item->blit_surface_scaled.clip = clip ? *clip : (Bound){0, 0, 99999, 99999};
    dl_store_clip_shapes(dl, &item->blit_surface_scaled.clip_shapes, clip_shapes, clip_depth);
}

// ---------------------------------------------------------------------------
// Recording: post-processing operations
// ---------------------------------------------------------------------------

void dl_apply_opacity(DisplayList* dl, int x0, int y0, int x1, int y1,
                      float opacity) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_APPLY_OPACITY;
    item->bounds[0] = (float)x0; item->bounds[1] = (float)y0;
    item->bounds[2] = (float)(x1 - x0); item->bounds[3] = (float)(y1 - y0);
    item->apply_opacity.x0 = x0;
    item->apply_opacity.y0 = y0;
    item->apply_opacity.x1 = x1;
    item->apply_opacity.y1 = y1;
    item->apply_opacity.opacity = opacity;
}

void dl_composite_opacity(DisplayList* dl, int x0, int y0, int w, int h,
                          float opacity) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_COMPOSITE_OPACITY;
    item->bounds[0] = (float)x0; item->bounds[1] = (float)y0;
    item->bounds[2] = (float)w; item->bounds[3] = (float)h;
    item->composite_opacity.x0 = x0;
    item->composite_opacity.y0 = y0;
    item->composite_opacity.w = w;
    item->composite_opacity.h = h;
    item->composite_opacity.opacity = opacity;
}

void dl_save_backdrop(DisplayList* dl, int x0, int y0, int w, int h) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_SAVE_BACKDROP;
    item->bounds[0] = (float)x0; item->bounds[1] = (float)y0;
    item->bounds[2] = (float)w; item->bounds[3] = (float)h;
    item->save_backdrop.x0 = x0;
    item->save_backdrop.y0 = y0;
    item->save_backdrop.w = w;
    item->save_backdrop.h = h;
}

void dl_apply_blend_mode(DisplayList* dl, int x0, int y0, int w, int h,
                         int blend_mode) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_APPLY_BLEND_MODE;
    item->bounds[0] = (float)x0; item->bounds[1] = (float)y0;
    item->bounds[2] = (float)w; item->bounds[3] = (float)h;
    item->apply_blend_mode.x0 = x0;
    item->apply_blend_mode.y0 = y0;
    item->apply_blend_mode.w = w;
    item->apply_blend_mode.h = h;
    item->apply_blend_mode.blend_mode = blend_mode;
}

void dl_apply_filter(DisplayList* dl, float x, float y, float w, float h,
                     void* filter, const Bound* clip) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_APPLY_FILTER;
    item->bounds[0] = x; item->bounds[1] = y; item->bounds[2] = w; item->bounds[3] = h;
    item->apply_filter.x = x;
    item->apply_filter.y = y;
    item->apply_filter.w = w;
    item->apply_filter.h = h;
    item->apply_filter.filter = filter;
    item->apply_filter.clip = clip ? *clip : (Bound){0, 0, 99999, 99999};
}

void dl_box_blur_region(DisplayList* dl, int rx, int ry, int rw, int rh, float blur_radius,
                        int clip_type, const float* clip_params,
                        int exclude_type, const float* exclude_params) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_BOX_BLUR_REGION;
    item->bounds[0] = (float)rx; item->bounds[1] = (float)ry;
    item->bounds[2] = (float)rw; item->bounds[3] = (float)rh;
    item->box_blur_region.rx = rx;
    item->box_blur_region.ry = ry;
    item->box_blur_region.rw = rw;
    item->box_blur_region.rh = rh;
    item->box_blur_region.blur_radius = blur_radius;
    item->box_blur_region.clip_type = clip_type;
    if (clip_type && clip_params) {
        memcpy(item->box_blur_region.clip_params, clip_params, 8 * sizeof(float));
    } else {
        memset(item->box_blur_region.clip_params, 0, 8 * sizeof(float));
    }
    item->box_blur_region.exclude_type = exclude_type;
    if (exclude_type && exclude_params) {
        memcpy(item->box_blur_region.exclude_params, exclude_params, 8 * sizeof(float));
    } else {
        memset(item->box_blur_region.exclude_params, 0, 8 * sizeof(float));
    }
}

void dl_box_blur_inset(DisplayList* dl, int rx, int ry, int rw, int rh, int pad, float blur_radius, uint32_t bg_color) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_BOX_BLUR_INSET;
    // bounds cover the expanded region for tile culling
    item->bounds[0] = (float)(rx - pad); item->bounds[1] = (float)(ry - pad);
    item->bounds[2] = (float)(rw + 2 * pad); item->bounds[3] = (float)(rh + 2 * pad);
    item->box_blur_inset.rx = rx;
    item->box_blur_inset.ry = ry;
    item->box_blur_inset.rw = rw;
    item->box_blur_inset.rh = rh;
    item->box_blur_inset.pad = pad;
    item->box_blur_inset.blur_radius = blur_radius;
    item->box_blur_inset.bg_color = bg_color;
}

void dl_shadow_clip_save(DisplayList* dl, int rx, int ry, int rw, int rh) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_SHADOW_CLIP_SAVE;
    item->bounds[0] = (float)rx; item->bounds[1] = (float)ry;
    item->bounds[2] = (float)rw; item->bounds[3] = (float)rh;
    item->shadow_clip_save.rx = rx;
    item->shadow_clip_save.ry = ry;
    item->shadow_clip_save.rw = rw;
    item->shadow_clip_save.rh = rh;
}

void dl_shadow_clip_restore(DisplayList* dl, int exclude_type, const float* exclude_params,
                            int save_rx, int save_ry, int save_rw, int save_rh,
                            int restore_inside) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_SHADOW_CLIP_RESTORE;
    item->bounds[0] = (float)save_rx; item->bounds[1] = (float)save_ry;
    item->bounds[2] = (float)save_rw; item->bounds[3] = (float)save_rh;
    item->shadow_clip_restore.exclude_type = exclude_type;
    if (exclude_type && exclude_params) {
        memcpy(item->shadow_clip_restore.exclude_params, exclude_params, 8 * sizeof(float));
    } else {
        memset(item->shadow_clip_restore.exclude_params, 0, 8 * sizeof(float));
    }
    item->shadow_clip_restore.save_rx = save_rx;
    item->shadow_clip_restore.save_ry = save_ry;
    item->shadow_clip_restore.save_rw = save_rw;
    item->shadow_clip_restore.save_rh = save_rh;
    item->shadow_clip_restore.restore_inside = restore_inside;
}

void dl_outer_shadow(DisplayList* dl,
                     float shadow_x, float shadow_y, float shadow_w, float shadow_h,
                     float sr_tl, float sr_tr, float sr_br, float sr_bl,
                     Color color, float blur_radius,
                     int exclude_type, const float* exclude_params,
                     int clip_type, const float* clip_params) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_OUTER_SHADOW;
    // bounds = blur region (used for tile culling)
    float pad = blur_radius < 0 ? 0 : blur_radius;
    item->bounds[0] = shadow_x - pad;
    item->bounds[1] = shadow_y - pad;
    item->bounds[2] = shadow_w + pad * 2;
    item->bounds[3] = shadow_h + pad * 2;
    DlOuterShadow* o = &item->outer_shadow;
    o->shadow_x = shadow_x; o->shadow_y = shadow_y;
    o->shadow_w = shadow_w; o->shadow_h = shadow_h;
    o->sr_tl = sr_tl; o->sr_tr = sr_tr; o->sr_br = sr_br; o->sr_bl = sr_bl;
    o->color = color;
    o->blur_radius = blur_radius;
    o->exclude_type = exclude_type;
    if (exclude_type && exclude_params) {
        memcpy(o->exclude_params, exclude_params, 8 * sizeof(float));
    } else {
        memset(o->exclude_params, 0, 8 * sizeof(float));
    }
    o->clip_type = clip_type;
    if (clip_type && clip_params) {
        memcpy(o->clip_params, clip_params, 8 * sizeof(float));
    } else {
        memset(o->clip_params, 0, 8 * sizeof(float));
    }
}

void dl_video_placeholder(DisplayList* dl, void* video,
                          float dst_x, float dst_y, float dst_w, float dst_h,
                          int object_fit, const Bound* clip) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_VIDEO_PLACEHOLDER;
    item->bounds[0] = dst_x; item->bounds[1] = dst_y;
    item->bounds[2] = dst_w; item->bounds[3] = dst_h;
    item->video_placeholder.video = video;
    item->video_placeholder.dst_x = dst_x;
    item->video_placeholder.dst_y = dst_y;
    item->video_placeholder.dst_w = dst_w;
    item->video_placeholder.dst_h = dst_h;
    item->video_placeholder.object_fit = object_fit;
    item->video_placeholder.clip = clip ? *clip : (Bound){0, 0, 99999, 99999};
}

void dl_webview_layer_placeholder(DisplayList* dl, void* surface,
                                  float dst_x, float dst_y, float dst_w, float dst_h,
                                  const Bound* clip) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_WEBVIEW_LAYER_PLACEHOLDER;
    item->bounds[0] = dst_x; item->bounds[1] = dst_y;
    item->bounds[2] = dst_w; item->bounds[3] = dst_h;
    item->webview_layer_placeholder.surface = surface;
    item->webview_layer_placeholder.dst_x = dst_x;
    item->webview_layer_placeholder.dst_y = dst_y;
    item->webview_layer_placeholder.dst_w = dst_w;
    item->webview_layer_placeholder.dst_h = dst_h;
    item->webview_layer_placeholder.clip = clip ? *clip : (Bound){0, 0, 99999, 99999};
}

// ---------------------------------------------------------------------------
// Replay: execute all recorded commands
// ---------------------------------------------------------------------------

void dl_replay(DisplayList* dl, RdtVector* vec,
               ImageSurface* surface, Bound* clip,
               ScratchArena* scratch, float scale,
               DirtyTracker* dirty_tracker) {
    log_debug("[DL_REPLAY] replaying %d items", dl->count);

    DisplayReplayDirtyClip dirty_clip = dl_replay_push_dirty_clip(vec, dirty_tracker, scale);

    DisplayReplayBackdropStack backdrop_stack;
    dl_replay_backdrop_init(&backdrop_stack);

    // shadow clip save buffer for DL_SHADOW_CLIP_SAVE / DL_SHADOW_CLIP_RESTORE pairs
    uint32_t* shadow_clip_saved = nullptr;
    int shadow_clip_region[4] = {};  // x0, y0, w, h (clamped to surface)

    for (int i = 0; i < dl->count; i++) {
        DisplayItem* item = &dl->items[i];

        switch (item->op) {

        case DL_FILL_RECT: {
            DlFillRect* r = &item->fill_rect;
            rdt_fill_rect(vec, r->x, r->y, r->w, r->h, r->color);
            break;
        }

        case DL_FILL_ROUNDED_RECT: {
            DlFillRoundedRect* r = &item->fill_rounded_rect;
            rdt_fill_rounded_rect(vec, r->x, r->y, r->w, r->h, r->rx, r->ry, r->color);
            break;
        }

        case DL_FILL_PATH: {
            DlFillPath* r = &item->fill_path;
            rdt_fill_path(vec, r->path, r->color, r->rule,
                          r->has_transform ? &r->transform : nullptr);
            break;
        }

        case DL_STROKE_PATH: {
            DlStrokePath* r = &item->stroke_path;
            rdt_stroke_path(vec, r->path, r->color, r->width, r->cap, r->join,
                            r->dash_array, r->dash_count, r->dash_phase,
                            r->has_transform ? &r->transform : nullptr);
            break;
        }

        case DL_FILL_LINEAR_GRADIENT: {
            DlFillLinearGradient* r = &item->fill_linear_gradient;
            rdt_fill_linear_gradient(vec, r->path, r->x1, r->y1, r->x2, r->y2,
                                     r->stops, r->stop_count, r->rule,
                                     r->has_transform ? &r->transform : nullptr);
            break;
        }

        case DL_FILL_RADIAL_GRADIENT: {
            DlFillRadialGradient* r = &item->fill_radial_gradient;
            rdt_fill_radial_gradient(vec, r->path, r->cx, r->cy, r->r,
                                     r->stops, r->stop_count, r->rule,
                                     r->has_transform ? &r->transform : nullptr);
            break;
        }

        case DL_DRAW_IMAGE: {
            DlDrawImage* r = &item->draw_image;
            rdt_draw_image(vec, r->pixels, r->src_w, r->src_h, r->src_stride,
                           r->dst_x, r->dst_y, r->dst_w, r->dst_h, r->opacity,
                           r->has_transform ? &r->transform : nullptr);
            break;
        }

        case DL_DRAW_GLYPH: {
            if (dirty_clip.active) {
                DlDrawGlyph tightened = item->draw_glyph;
                dl_replay_intersect_dirty_clip(&dirty_clip, &tightened.clip);
                dl_replay_draw_glyph(surface, &tightened);
            } else {
                dl_replay_draw_glyph(surface, &item->draw_glyph);
            }
            break;
        }

        case DL_DRAW_PICTURE: {
            DlDrawPicture* r = &item->draw_picture;
            rdt_picture_draw(vec, r->picture, r->opacity,
                             r->has_transform ? &r->transform : nullptr);
            break;
        }

        case DL_PUSH_CLIP: {
            DlPushClip* r = &item->push_clip;
            rdt_push_clip(vec, r->path,
                          r->has_transform ? &r->transform : nullptr);
            break;
        }

        case DL_POP_CLIP: {
            rdt_pop_clip(vec);
            break;
        }

        case DL_SAVE_CLIP_DEPTH: {
            item->clip_depth.saved_depth = rdt_clip_save_depth();
            break;
        }

        case DL_RESTORE_CLIP_DEPTH: {
            rdt_clip_restore_depth(item->clip_depth.saved_depth);
            break;
        }

        case DL_FILL_SURFACE_RECT: {
            DlFillSurfaceRect* r = &item->fill_surface_rect;
            Rect rect = {r->x, r->y, r->w, r->h};
            Bound bound = r->clip;
            dl_replay_intersect_dirty_clip(&dirty_clip, &bound);
            ClipShape shapes[RDT_MAX_CLIP_SHAPES];
            ClipShape* shape_ptrs[RDT_MAX_CLIP_SHAPES];
            int clip_depth = dl_restore_clip_shapes(&r->clip_shapes, shapes, shape_ptrs);
            RasterPaintContext raster = {surface, &bound, shape_ptrs, clip_depth};
            raster_fill_rect(&raster, &rect, r->color);
            break;
        }

        case DL_BLIT_SURFACE_SCALED: {
            DlBlitSurfaceScaled* r = &item->blit_surface_scaled;
            Rect dst_rect = {r->dst_x, r->dst_y, r->dst_w, r->dst_h};
            Bound bound = r->clip;
            dl_replay_intersect_dirty_clip(&dirty_clip, &bound);
            ClipShape shapes[RDT_MAX_CLIP_SHAPES];
            ClipShape* shape_ptrs[RDT_MAX_CLIP_SHAPES];
            int clip_depth = dl_restore_clip_shapes(&r->clip_shapes, shapes, shape_ptrs);
            RasterPaintContext raster = {surface, &bound, shape_ptrs, clip_depth};
            raster_blit_surface_scaled(&raster, (ImageSurface*)r->src_surface, nullptr,
                                       &dst_rect, (ScaleMode)r->scale_mode, r->opacity);
            break;
        }

        case DL_APPLY_OPACITY: {
            DlApplyOpacity* r = &item->apply_opacity;
            if (surface && surface->pixels) {
                int x0 = std::max(0, r->x0);
                int y0 = std::max(0, r->y0);
                int x1 = std::min(surface->width, r->x1);
                int y1 = std::min(surface->height, r->y1);
                for (int y = y0; y < y1; y++) {
                    uint8_t* row = (uint8_t*)surface->pixels + y * surface->pitch;
                    for (int x = x0; x < x1; x++) {
                        uint8_t* pixel = row + x * 4;
                        pixel[3] = (uint8_t)(pixel[3] * r->opacity + 0.5f);
                    }
                }
            }
            break;
        }

        case DL_COMPOSITE_OPACITY: {
            DlCompositeOpacity* r = &item->composite_opacity;
            dl_replay_backdrop_composite_opacity(&backdrop_stack, surface, r);
            break;
        }

        case DL_SAVE_BACKDROP: {
            DlSaveBackdrop* r = &item->save_backdrop;
            dl_replay_backdrop_save(&backdrop_stack, surface, scratch, r);
            break;
        }

        case DL_APPLY_BLEND_MODE: {
            DlApplyBlendMode* r = &item->apply_blend_mode;
            dl_replay_backdrop_apply_blend_mode(&backdrop_stack, surface, r);
            break;
        }

        case DL_APPLY_FILTER: {
            DlApplyFilter* r = &item->apply_filter;
            Rect rect = {r->x, r->y, r->w, r->h};
            Bound bound = r->clip;
            dl_replay_intersect_dirty_clip(&dirty_clip, &bound);
            apply_css_filters(scratch, surface, (FilterProp*)r->filter, &rect, &bound);
            break;
        }

        case DL_BOX_BLUR_REGION: {
            DlBoxBlurRegion* r = &item->box_blur_region;
            if (r->clip_type && surface && surface->pixels) {
                // Save pixels before blur, then restore outside clip after blur
                int sw = surface->width, sh = surface->height;
                int x0 = std::max(0, r->rx), y0 = std::max(0, r->ry);
                int x1 = std::min(sw, r->rx + r->rw), y1 = std::min(sh, r->ry + r->rh);
                int w = x1 - x0, h = y1 - y0;
                uint32_t* saved = nullptr;
                if (w > 0 && h > 0) {
                    saved = (uint32_t*)scratch_alloc(scratch, (size_t)w * h * sizeof(uint32_t));
                    uint32_t* px = (uint32_t*)surface->pixels;
                    int pitch = surface->pitch / 4;
                    for (int row = 0; row < h; row++)
                        memcpy(saved + row * w, px + (y0 + row) * pitch + x0, w * sizeof(uint32_t));
                }
                box_blur_region(scratch, surface, r->rx, r->ry, r->rw, r->rh, r->blur_radius);
                if (saved) {
                    ClipShape cs = clip_shape_from_params(r->clip_type, r->clip_params);
                    uint32_t* px = (uint32_t*)surface->pixels;
                    int pitch = surface->pitch / 4;
                    for (int row = 0; row < h; row++) {
                        for (int col = 0; col < w; col++) {
                            float fx = (float)(x0 + col) + 0.5f;
                            float fy = (float)(y0 + row) + 0.5f;
                            if (!clip_point_in_shape(&cs, fx, fy))
                                px[(y0 + row) * pitch + (x0 + col)] = saved[row * w + col];
                        }
                    }
                }
            } else {
                box_blur_region(scratch, surface, r->rx, r->ry, r->rw, r->rh, r->blur_radius);
            }
            break;
        }

        case DL_BOX_BLUR_INSET: {
            DlBoxBlurInset* r = &item->box_blur_inset;
            box_blur_region_inset(scratch, surface, r->rx, r->ry, r->rw, r->rh,
                                  r->pad, r->blur_radius, r->bg_color);
            break;
        }

        case DL_SHADOW_CLIP_SAVE: {
            DlShadowClipSave* r = &item->shadow_clip_save;
            shadow_clip_saved = nullptr;
            if (surface && surface->pixels) {
                int sw = surface->width, sh = surface->height;
                int x0 = std::max(0, r->rx), y0 = std::max(0, r->ry);
                int x1 = std::min(sw, r->rx + r->rw), y1 = std::min(sh, r->ry + r->rh);
                int w = x1 - x0, h = y1 - y0;
                if (w > 0 && h > 0) {
                    shadow_clip_saved = (uint32_t*)scratch_alloc(scratch, (size_t)w * h * sizeof(uint32_t));
                    uint32_t* px = (uint32_t*)surface->pixels;
                    int pitch = surface->pitch / 4;
                    for (int row = 0; row < h; row++)
                        memcpy(shadow_clip_saved + row * w, px + (y0 + row) * pitch + x0, w * sizeof(uint32_t));
                    shadow_clip_region[0] = x0;
                    shadow_clip_region[1] = y0;
                    shadow_clip_region[2] = w;
                    shadow_clip_region[3] = h;
                }
            }
            break;
        }

        case DL_SHADOW_CLIP_RESTORE: {
            DlShadowClipRestore* r = &item->shadow_clip_restore;
            if (shadow_clip_saved && surface && surface->pixels && r->exclude_type) {
                int x0 = shadow_clip_region[0], y0 = shadow_clip_region[1];
                int w = shadow_clip_region[2], h = shadow_clip_region[3];
                ClipShape ex = clip_shape_from_params(r->exclude_type, r->exclude_params);
                uint32_t* px = (uint32_t*)surface->pixels;
                int pitch = surface->pitch / 4;
                for (int row = 0; row < h; row++) {
                    for (int col = 0; col < w; col++) {
                        float fx = (float)(x0 + col) + 0.5f;
                        float fy = (float)(y0 + row) + 0.5f;
                        bool inside = clip_point_in_shape(&ex, fx, fy);
                        if (r->restore_inside ? inside : !inside)
                            px[(y0 + row) * pitch + (x0 + col)] = shadow_clip_saved[row * w + col];
                    }
                }
            }
            shadow_clip_saved = nullptr;
            break;
        }

        case DL_OUTER_SHADOW: {
            DlOuterShadow* o = &item->outer_shadow;
            render_outer_shadow_blur_composite(
                scratch, surface,
                o->shadow_x, o->shadow_y, o->shadow_w, o->shadow_h,
                o->sr_tl, o->sr_tr, o->sr_br, o->sr_bl,
                o->color, o->blur_radius,
                o->exclude_type, o->exclude_params,
                o->clip_type, o->clip_params);
            break;
        }

        case DL_BEGIN_ELEMENT:
        case DL_END_ELEMENT:
            // Phase 2+: element group markers — no-op during flat replay
            break;

        case DL_VIDEO_PLACEHOLDER:
            // no-op during tile replay; video frames are blitted post-composite
            break;

        case DL_WEBVIEW_LAYER_PLACEHOLDER: {
            DlWebviewLayerPlaceholder* r = &item->webview_layer_placeholder;
            ImageSurface* src = (ImageSurface*)r->surface;
            if (src && src->pixels) {
                Rect dst_rect = { r->dst_x, r->dst_y, r->dst_w, r->dst_h };
                RasterPaintContext raster = {surface, &r->clip, nullptr, 0};
                raster_blit_surface_scaled(&raster, src, nullptr, &dst_rect, SCALE_MODE_LINEAR);
            }
            break;
        }
        }
    }

    int backdrop_depth = dl_replay_backdrop_depth(&backdrop_stack);
    if (backdrop_depth > 0) {
        log_error("[DL_REPLAY] unbalanced backdrop stack: %d entries left", backdrop_depth);
    }

    dl_replay_pop_dirty_clip(vec, &dirty_clip);

    log_debug("[DL_REPLAY] done");
}
