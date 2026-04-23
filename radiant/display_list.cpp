// ==========================================================================
// DisplayList — Recording and replay implementation.
// ==========================================================================

#include "display_list.h"
#include "render_filter.hpp"
#include "render_background.hpp"
#include "clip_shape.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include <string.h>
#include <math.h>
#include <algorithm>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

#define DL_INITIAL_CAPACITY 2048

static DisplayItem* dl_alloc_item(DisplayList* dl) {
    if (dl->count >= dl->capacity) {
        int new_cap = dl->capacity ? dl->capacity * 2 : DL_INITIAL_CAPACITY;
        dl->items = (DisplayItem*)mem_realloc(dl->items, new_cap * sizeof(DisplayItem), MEM_CAT_RENDER);
        dl->capacity = new_cap;
    }
    DisplayItem* item = &dl->items[dl->count++];
    memset(item, 0, sizeof(DisplayItem));
    return item;
}

// Copy gradient stops into the display list's arena
static RdtGradientStop* dl_copy_stops(DisplayList* dl, const RdtGradientStop* stops, int count) {
    if (!stops || count <= 0) return nullptr;
    size_t sz = count * sizeof(RdtGradientStop);
    RdtGradientStop* copy = (RdtGradientStop*)scratch_alloc(&dl->arena, sz);
    memcpy(copy, stops, sz);
    return copy;
}

// Copy dash array into the display list's arena
static float* dl_copy_dashes(DisplayList* dl, const float* dashes, int count) {
    if (!dashes || count <= 0) return nullptr;
    size_t sz = count * sizeof(float);
    float* copy = (float*)scratch_alloc(&dl->arena, sz);
    memcpy(copy, dashes, sz);
    return copy;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void dl_init(DisplayList* dl, Arena* backing_arena) {
    memset(dl, 0, sizeof(DisplayList));
    scratch_init(&dl->arena, backing_arena);
}

void dl_clear(DisplayList* dl) {
    // Free cloned paths
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
    // Arena memory is reclaimed by the backing arena's lifecycle
}

void dl_destroy(DisplayList* dl) {
    dl_clear(dl);
    if (dl->items) {
        mem_free(dl->items);
        dl->items = nullptr;
    }
    dl->capacity = 0;
    scratch_release(&dl->arena);
}

int dl_item_count(const DisplayList* dl) {
    return dl->count;
}

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
                   Color color, bool is_color_emoji, const Bound* clip) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_DRAW_GLYPH;
    item->bounds[0] = (float)x; item->bounds[1] = (float)y;
    item->bounds[2] = (float)bitmap->width; item->bounds[3] = (float)bitmap->height;
    item->draw_glyph.bitmap = *bitmap;  // copy descriptor, buffer pointer borrowed
    item->draw_glyph.x = x;
    item->draw_glyph.y = y;
    item->draw_glyph.color = color;
    item->draw_glyph.is_color_emoji = is_color_emoji;
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
                          uint32_t color, const Bound* clip) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_FILL_SURFACE_RECT;
    item->bounds[0] = x; item->bounds[1] = y; item->bounds[2] = w; item->bounds[3] = h;
    item->fill_surface_rect.x = x;
    item->fill_surface_rect.y = y;
    item->fill_surface_rect.w = w;
    item->fill_surface_rect.h = h;
    item->fill_surface_rect.color = color;
    item->fill_surface_rect.clip = clip ? *clip : (Bound){0, 0, 99999, 99999};
}

void dl_blit_surface_scaled(DisplayList* dl, void* src_surface,
                            float dst_x, float dst_y, float dst_w, float dst_h,
                            int scale_mode, const Bound* clip) {
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
    item->blit_surface_scaled.clip = clip ? *clip : (Bound){0, 0, 99999, 99999};
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
// Replay: glyph drawing (standalone, no RenderContext dependency)
// ---------------------------------------------------------------------------

static void replay_draw_glyph(ImageSurface* surface, const DlDrawGlyph* g) {
    GlyphBitmap* bitmap = (GlyphBitmap*)&g->bitmap;
    int x = g->x, y = g->y;
    Color color = g->color;
    const Bound* clip = &g->clip;

    if (g->is_color_emoji) {
        // color emoji replay
        float bscale = bitmap->bitmap_scale;
        if (bscale <= 0.0f) bscale = 1.0f;
        int target_w = (int)(bitmap->width  * bscale + 0.5f);
        int target_h = (int)(bitmap->height * bscale + 0.5f);
        if (target_w <= 0 || target_h <= 0) return;

        int left   = std::max((int)clip->left,  x);
        int right  = std::min((int)clip->right,  x + target_w);
        int top    = std::max((int)clip->top,    y);
        int bottom = std::min((int)clip->bottom, y + target_h);
        if (left >= right || top >= bottom) return;

        float inv_scale = 1.0f / bscale;
        for (int dy = top - y; dy < bottom - y; dy++) {
            uint8_t* row_pixels = (uint8_t*)surface->pixels + (y + dy - surface->tile_offset_y) * surface->pitch;
            float src_y = dy * inv_scale;
            int sy0 = (int)src_y;
            int sy1 = sy0 + 1;
            float fy = src_y - sy0;
            if (sy0 >= (int)bitmap->height) sy0 = bitmap->height - 1;
            if (sy1 >= (int)bitmap->height) sy1 = bitmap->height - 1;

            for (int dx = left - x; dx < right - x; dx++) {
                if (x + dx < 0 || x + dx >= surface->width) continue;
                float src_x = dx * inv_scale;
                int sx0 = (int)src_x;
                int sx1 = sx0 + 1;
                float fx = src_x - sx0;
                if (sx0 >= (int)bitmap->width) sx0 = bitmap->width - 1;
                if (sx1 >= (int)bitmap->width) sx1 = bitmap->width - 1;

                uint8_t* s00 = bitmap->buffer + sy0 * bitmap->pitch + sx0 * 4;
                uint8_t* s10 = bitmap->buffer + sy0 * bitmap->pitch + sx1 * 4;
                uint8_t* s01 = bitmap->buffer + sy1 * bitmap->pitch + sx0 * 4;
                uint8_t* s11 = bitmap->buffer + sy1 * bitmap->pitch + sx1 * 4;

                float w00 = (1 - fx) * (1 - fy);
                float w10 = fx * (1 - fy);
                float w01 = (1 - fx) * fy;
                float w11 = fx * fy;

                uint8_t src_b = (uint8_t)(s00[0]*w00 + s10[0]*w10 + s01[0]*w01 + s11[0]*w11 + 0.5f);
                uint8_t src_g = (uint8_t)(s00[1]*w00 + s10[1]*w10 + s01[1]*w01 + s11[1]*w11 + 0.5f);
                uint8_t src_r = (uint8_t)(s00[2]*w00 + s10[2]*w10 + s01[2]*w01 + s11[2]*w11 + 0.5f);
                uint8_t src_a = (uint8_t)(s00[3]*w00 + s10[3]*w10 + s01[3]*w01 + s11[3]*w11 + 0.5f);

                if (src_a > 0) {
                    uint8_t* dst = (uint8_t*)(row_pixels + (x + dx) * 4);
                    if (src_a == 255) {
                        dst[0] = src_r; dst[1] = src_g; dst[2] = src_b; dst[3] = 255;
                    } else {
                        uint32_t inv_alpha = 255 - src_a;
                        dst[0] = (dst[0] * inv_alpha + src_r * src_a) / 255;
                        dst[1] = (dst[1] * inv_alpha + src_g * src_a) / 255;
                        dst[2] = (dst[2] * inv_alpha + src_b * src_a) / 255;
                        dst[3] = 255;
                    }
                }
            }
        }
        return;
    }

    // grayscale / monochrome glyph
    int left   = std::max((int)clip->left,  x);
    int right  = std::min((int)clip->right,  x + (int)bitmap->width);
    int top    = std::max((int)clip->top,    y);
    int bottom = std::min((int)clip->bottom, y + (int)bitmap->height);
    if (left >= right || top >= bottom) return;

    bool is_mono = (bitmap->pixel_mode == GLYPH_PIXEL_MONO);

    for (int i = top - y; i < bottom - y; i++) {
        uint8_t* row_pixels = (uint8_t*)surface->pixels + (y + i - surface->tile_offset_y) * surface->pitch;
        for (int j = left - x; j < right - x; j++) {
            if (x + j < 0 || x + j >= surface->width) continue;

            uint32_t intensity;
            if (is_mono) {
                int byte_index = j / 8;
                int bit_index = 7 - (j % 8);
                uint8_t byte_val = bitmap->buffer[i * bitmap->pitch + byte_index];
                intensity = (byte_val & (1 << bit_index)) ? 255 : 0;
            } else {
                intensity = bitmap->buffer[i * bitmap->pitch + j];
            }

            if (intensity > 0) {
                uint8_t* p = (uint8_t*)(row_pixels + (x + j) * 4);
                uint32_t v = 255 - intensity;
                if (color.c == 0xFF000000) {
                    p[0] = p[0] * v / 255;
                    p[1] = p[1] * v / 255;
                    p[2] = p[2] * v / 255;
                    p[3] = 0xFF;
                } else {
                    p[0] = (p[0] * v + color.r * intensity) / 255;
                    p[1] = (p[1] * v + color.g * intensity) / 255;
                    p[2] = (p[2] * v + color.b * intensity) / 255;
                    p[3] = 0xFF;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Replay: execute all recorded commands
// ---------------------------------------------------------------------------

void dl_replay(DisplayList* dl, RdtVector* vec,
               ImageSurface* surface, Bound* clip,
               ScratchArena* scratch, float scale,
               DirtyTracker* dirty_tracker) {
    log_debug("[DL_REPLAY] replaying %d items", dl->count);

    // Phase 19: When replaying during selective repaint, clip all rendering to
    // the union bounding box of dirty regions.  This prevents parent-block
    // backgrounds (recorded at full-viewport clip) from overwriting preserved
    // surface content outside the dirty areas.
    Bound dirty_union = {};
    bool has_dirty_clip = false;
    RdtPath* dirty_clip_path = nullptr;
    if (dirty_tracker && dirty_tracker->dirty_list && !dirty_tracker->full_repaint) {
        // compute union bounding box of all dirty rects (in physical pixels)
        DirtyRect* dr = dirty_tracker->dirty_list;
        float dl = dr->x * scale, dt = dr->y * scale;
        float dr_right = (dr->x + dr->width) * scale;
        float dr_bottom = (dr->y + dr->height) * scale;
        dr = dr->next;
        while (dr) {
            float rx = dr->x * scale;
            float ry = dr->y * scale;
            float rr = (dr->x + dr->width) * scale;
            float rb = (dr->y + dr->height) * scale;
            if (rx < dl) dl = rx;
            if (ry < dt) dt = ry;
            if (rr > dr_right) dr_right = rr;
            if (rb > dr_bottom) dr_bottom = rb;
            dr = dr->next;
        }
        dirty_union = {dl, dt, dr_right, dr_bottom};
        has_dirty_clip = true;

        // push a ThorVG clip path covering the dirty union (clips vector ops)
        dirty_clip_path = rdt_path_new();
        rdt_path_move_to(dirty_clip_path, dl, dt);
        rdt_path_line_to(dirty_clip_path, dr_right, dt);
        rdt_path_line_to(dirty_clip_path, dr_right, dr_bottom);
        rdt_path_line_to(dirty_clip_path, dl, dr_bottom);
        rdt_path_close(dirty_clip_path);
        rdt_push_clip(vec, dirty_clip_path, nullptr);

        log_debug("[DL_REPLAY] dirty clip: (%.0f,%.0f)-(%.0f,%.0f)",
                  dl, dt, dr_right, dr_bottom);
    }

    // backdrop stack for mix-blend-mode (DL_SAVE_BACKDROP / DL_APPLY_BLEND_MODE pairs)
    #define DL_MAX_BACKDROP_DEPTH 16
    uint32_t* backdrop_stack[DL_MAX_BACKDROP_DEPTH];
    int backdrop_region[DL_MAX_BACKDROP_DEPTH][4];  // x0, y0, w, h
    int backdrop_sp = 0;

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
            if (has_dirty_clip) {
                // tighten glyph clip to dirty union
                DlDrawGlyph tightened = item->draw_glyph;
                tightened.clip.left   = std::max(tightened.clip.left,   dirty_union.left);
                tightened.clip.top    = std::max(tightened.clip.top,    dirty_union.top);
                tightened.clip.right  = std::min(tightened.clip.right,  dirty_union.right);
                tightened.clip.bottom = std::min(tightened.clip.bottom, dirty_union.bottom);
                replay_draw_glyph(surface, &tightened);
            } else {
                replay_draw_glyph(surface, &item->draw_glyph);
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
            if (has_dirty_clip) {
                bound.left   = std::max(bound.left,   dirty_union.left);
                bound.top    = std::max(bound.top,    dirty_union.top);
                bound.right  = std::min(bound.right,  dirty_union.right);
                bound.bottom = std::min(bound.bottom, dirty_union.bottom);
            }
            fill_surface_rect(surface, &rect, r->color, &bound, nullptr, 0);
            break;
        }

        case DL_BLIT_SURFACE_SCALED: {
            DlBlitSurfaceScaled* r = &item->blit_surface_scaled;
            Rect dst_rect = {r->dst_x, r->dst_y, r->dst_w, r->dst_h};
            Bound bound = r->clip;
            if (has_dirty_clip) {
                bound.left   = std::max(bound.left,   dirty_union.left);
                bound.top    = std::max(bound.top,    dirty_union.top);
                bound.right  = std::min(bound.right,  dirty_union.right);
                bound.bottom = std::min(bound.bottom, dirty_union.bottom);
            }
            blit_surface_scaled((ImageSurface*)r->src_surface, nullptr,
                                surface, &dst_rect, &bound,
                                (ScaleMode)r->scale_mode, nullptr, 0);
            break;
        }

        case DL_APPLY_OPACITY: {
            DlApplyOpacity* r = &item->apply_opacity;
            if (surface && surface->pixels) {
                for (int y = r->y0; y < r->y1; y++) {
                    uint8_t* row = (uint8_t*)surface->pixels + y * surface->pitch;
                    for (int x = r->x0; x < r->x1; x++) {
                        uint8_t* pixel = row + x * 4;
                        pixel[3] = (uint8_t)(pixel[3] * r->opacity + 0.5f);
                    }
                }
            }
            break;
        }

        case DL_COMPOSITE_OPACITY: {
            DlCompositeOpacity* r = &item->composite_opacity;
            if (surface && surface->pixels && backdrop_sp > 0) {
                backdrop_sp--;
                uint32_t* backdrop = backdrop_stack[backdrop_sp];
                int bx = backdrop_region[backdrop_sp][0];
                int by = backdrop_region[backdrop_sp][1];
                int bw = backdrop_region[backdrop_sp][2];
                int bh = backdrop_region[backdrop_sp][3];
                uint32_t* px = (uint32_t*)surface->pixels;
                int pitch = surface->pitch / 4;
                int opacity_i = (int)(r->opacity * 256 + 0.5f);
                for (int row = 0; row < bh; row++) {
                    for (int col = 0; col < bw; col++) {
                        uint32_t src = px[(by + row) * pitch + (bx + col)];
                        uint32_t dst = backdrop[row * bw + col];
                        if (src == 0) {
                            // source fully transparent — restore backdrop
                            px[(by + row) * pitch + (bx + col)] = dst;
                            continue;
                        }
                        // scale source by opacity (premultiplied alpha)
                        uint32_t sa = (((src >> 24) & 0xFF) * opacity_i + 128) >> 8;
                        uint32_t sr = ((src & 0xFF) * opacity_i + 128) >> 8;
                        uint32_t sg = (((src >> 8) & 0xFF) * opacity_i + 128) >> 8;
                        uint32_t sb = (((src >> 16) & 0xFF) * opacity_i + 128) >> 8;
                        // Porter-Duff source over: result = src' + dst * (1 - src'_a)
                        uint32_t inv_sa = 255 - sa;
                        uint32_t da = (dst >> 24) & 0xFF;
                        uint32_t dr = dst & 0xFF;
                        uint32_t dg = (dst >> 8) & 0xFF;
                        uint32_t db = (dst >> 16) & 0xFF;
                        uint32_t ra = sa + (da * inv_sa + 128) / 255;
                        uint32_t rr = sr + (dr * inv_sa + 128) / 255;
                        uint32_t rg = sg + (dg * inv_sa + 128) / 255;
                        uint32_t rb = sb + (db * inv_sa + 128) / 255;
                        if (ra > 255) ra = 255;
                        if (rr > 255) rr = 255;
                        if (rg > 255) rg = 255;
                        if (rb > 255) rb = 255;
                        px[(by + row) * pitch + (bx + col)] =
                            (ra << 24) | (rb << 16) | (rg << 8) | rr;
                    }
                }
            }
            break;
        }

        case DL_SAVE_BACKDROP: {
            DlSaveBackdrop* r = &item->save_backdrop;
            if (surface && surface->pixels && backdrop_sp < DL_MAX_BACKDROP_DEPTH) {
                int pitch = surface->pitch / 4;
                int sz = r->w * r->h;
                uint32_t* buf = (uint32_t*)scratch_alloc(scratch, sz * sizeof(uint32_t));
                uint32_t* px = (uint32_t*)surface->pixels;
                for (int row = 0; row < r->h; row++) {
                    memcpy(buf + row * r->w,
                           px + (r->y0 + row) * pitch + r->x0,
                           r->w * sizeof(uint32_t));
                }
                // clear the region so children render on transparent
                for (int row = 0; row < r->h; row++) {
                    memset(px + (r->y0 + row) * pitch + r->x0, 0, r->w * sizeof(uint32_t));
                }
                backdrop_stack[backdrop_sp] = buf;
                backdrop_region[backdrop_sp][0] = r->x0;
                backdrop_region[backdrop_sp][1] = r->y0;
                backdrop_region[backdrop_sp][2] = r->w;
                backdrop_region[backdrop_sp][3] = r->h;
                backdrop_sp++;
            }
            break;
        }

        case DL_APPLY_BLEND_MODE: {
            DlApplyBlendMode* r = &item->apply_blend_mode;
            if (surface && surface->pixels && backdrop_sp > 0) {
                backdrop_sp--;
                uint32_t* backdrop = backdrop_stack[backdrop_sp];
                int bx = backdrop_region[backdrop_sp][0];
                int by = backdrop_region[backdrop_sp][1];
                int bw = backdrop_region[backdrop_sp][2];
                int bh = backdrop_region[backdrop_sp][3];
                uint32_t* px = (uint32_t*)surface->pixels;
                int pitch = surface->pitch / 4;
                for (int row = 0; row < bh; row++) {
                    for (int col = 0; col < bw; col++) {
                        uint32_t bd = backdrop[row * bw + col];
                        uint32_t source = px[(by + row) * pitch + (bx + col)];
                        px[(by + row) * pitch + (bx + col)] =
                            composite_blend_pixel(bd, source, (CssEnum)r->blend_mode);
                    }
                }
            }
            break;
        }

        case DL_APPLY_FILTER: {
            DlApplyFilter* r = &item->apply_filter;
            Rect rect = {r->x, r->y, r->w, r->h};
            Bound bound = r->clip;
            if (has_dirty_clip) {
                bound.left   = std::max(bound.left,   dirty_union.left);
                bound.top    = std::max(bound.top,    dirty_union.top);
                bound.right  = std::min(bound.right,  dirty_union.right);
                bound.bottom = std::min(bound.bottom, dirty_union.bottom);
            }
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
                blit_surface_scaled(src, nullptr, surface, &dst_rect,
                                    &r->clip, SCALE_MODE_LINEAR, nullptr, 0);
            }
            break;
        }
        }
    }

    if (backdrop_sp > 0) {
        log_error("[DL_REPLAY] unbalanced backdrop stack: %d entries left", backdrop_sp);
    }

    // pop the dirty-region clip pushed at the start of selective replay
    if (dirty_clip_path) {
        rdt_pop_clip(vec);
        rdt_path_free(dirty_clip_path);
    }

    log_debug("[DL_REPLAY] done");
}
