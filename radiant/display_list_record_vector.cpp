#include "display_list.h"

#include "display_list_storage.hpp"
#include <math.h>

static Bound dl_default_vector_clip() {
    Bound clip = {0, 0, 99999, 99999};
    return clip;
}

static float dl_record_min_f(float a, float b) {
    return a < b ? a : b;
}

static float dl_record_max_f(float a, float b) {
    return a > b ? a : b;
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
    // bounds not computed for paths: set to zero, full-page for now
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
        float x0 = (float)x;
        float y0 = (float)y;
        float x1 = (float)x + (float)bitmap->width;
        float y1 = y0;
        float x2 = x1;
        float y2 = (float)y + (float)bitmap->height;
        float x3 = x0;
        float y3 = y2;
        float tx0 = transform->e11 * x0 + transform->e12 * y0 + transform->e13;
        float ty0 = transform->e21 * x0 + transform->e22 * y0 + transform->e23;
        float tx1 = transform->e11 * x1 + transform->e12 * y1 + transform->e13;
        float ty1 = transform->e21 * x1 + transform->e22 * y1 + transform->e23;
        float tx2 = transform->e11 * x2 + transform->e12 * y2 + transform->e13;
        float ty2 = transform->e21 * x2 + transform->e22 * y2 + transform->e23;
        float tx3 = transform->e11 * x3 + transform->e12 * y3 + transform->e13;
        float ty3 = transform->e21 * x3 + transform->e22 * y3 + transform->e23;
        float min_x = dl_record_min_f(dl_record_min_f(tx0, tx1), dl_record_min_f(tx2, tx3));
        float max_x = dl_record_max_f(dl_record_max_f(tx0, tx1), dl_record_max_f(tx2, tx3));
        float min_y = dl_record_min_f(dl_record_min_f(ty0, ty1), dl_record_min_f(ty2, ty3));
        float max_y = dl_record_max_f(dl_record_max_f(ty0, ty1), dl_record_max_f(ty2, ty3));
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
    item->draw_glyph.clip = clip ? *clip : dl_default_vector_clip();
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
    // saved_depth filled by caller or during replay
}

void dl_restore_clip_depth(DisplayList* dl, int saved_depth) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_RESTORE_CLIP_DEPTH;
    item->clip_depth.saved_depth = saved_depth;
}
