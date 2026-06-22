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

static void dl_record_set_unbounded(DisplayItem* item) {
    if (!item) return;
    item->bounds[0] = 0.0f;
    item->bounds[1] = 0.0f;
    item->bounds[2] = 99999.0f;
    item->bounds[3] = 99999.0f;
}

static void dl_record_set_bounds_xyxy(DisplayItem* item,
                                      float left, float top,
                                      float right, float bottom,
                                      float pad) {
    if (!item) return;
    if (right < left) {
        float tmp = left;
        left = right;
        right = tmp;
    }
    if (bottom < top) {
        float tmp = top;
        top = bottom;
        bottom = tmp;
    }
    left -= pad;
    top -= pad;
    right += pad;
    bottom += pad;
    if (right <= left || bottom <= top) {
        item->bounds[0] = floorf(left);
        item->bounds[1] = floorf(top);
        item->bounds[2] = 0.0f;
        item->bounds[3] = 0.0f;
        return;
    }
    float x = floorf(left);
    float y = floorf(top);
    item->bounds[0] = x;
    item->bounds[1] = y;
    item->bounds[2] = ceilf(right) - x;
    item->bounds[3] = ceilf(bottom) - y;
}

static void dl_record_set_rect_bounds(DisplayItem* item,
                                      float x, float y, float w, float h,
                                      const RdtMatrix* transform, float pad) {
    float left = w < 0.0f ? x + w : x;
    float right = w < 0.0f ? x : x + w;
    float top = h < 0.0f ? y + h : y;
    float bottom = h < 0.0f ? y : y + h;
    if (!transform) {
        dl_record_set_bounds_xyxy(item, left, top, right, bottom, pad);
        return;
    }

    float x0 = left;
    float y0 = top;
    float x1 = right;
    float y1 = top;
    float x2 = right;
    float y2 = bottom;
    float x3 = left;
    float y3 = bottom;
    float tx0 = transform->e11 * x0 + transform->e12 * y0 + transform->e13;
    float ty0 = transform->e21 * x0 + transform->e22 * y0 + transform->e23;
    float tx1 = transform->e11 * x1 + transform->e12 * y1 + transform->e13;
    float ty1 = transform->e21 * x1 + transform->e22 * y1 + transform->e23;
    float tx2 = transform->e11 * x2 + transform->e12 * y2 + transform->e13;
    float ty2 = transform->e21 * x2 + transform->e22 * y2 + transform->e23;
    float tx3 = transform->e11 * x3 + transform->e12 * y3 + transform->e13;
    float ty3 = transform->e21 * x3 + transform->e22 * y3 + transform->e23;

    left = dl_record_min_f(dl_record_min_f(tx0, tx1), dl_record_min_f(tx2, tx3));
    right = dl_record_max_f(dl_record_max_f(tx0, tx1), dl_record_max_f(tx2, tx3));
    top = dl_record_min_f(dl_record_min_f(ty0, ty1), dl_record_min_f(ty2, ty3));
    bottom = dl_record_max_f(dl_record_max_f(ty0, ty1), dl_record_max_f(ty2, ty3));
    dl_record_set_bounds_xyxy(item, left, top, right, bottom, pad);
}

static void dl_record_set_path_bounds(DisplayItem* item, RdtPath* path,
                                      const RdtMatrix* transform, float pad) {
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
    if (!rdt_path_get_bounds(path, &left, &top, &right, &bottom)) {
        dl_record_set_unbounded(item);
        return;
    }
    dl_record_set_rect_bounds(item, left, top, right - left, bottom - top,
                              transform, pad);
}

static void dl_record_intersect_bounds_with_clip(DisplayItem* item, const Bound* clip) {
    if (!item || !clip) return;
    float left = item->bounds[0];
    float top = item->bounds[1];
    float right = left + item->bounds[2];
    float bottom = top + item->bounds[3];
    left = dl_record_max_f(left, clip->left);
    top = dl_record_max_f(top, clip->top);
    right = dl_record_min_f(right, clip->right);
    bottom = dl_record_min_f(bottom, clip->bottom);
    if (right <= left || bottom <= top) {
        item->bounds[0] = left;
        item->bounds[1] = top;
        item->bounds[2] = 0.0f;
        item->bounds[3] = 0.0f;
        return;
    }
    dl_record_set_bounds_xyxy(item, left, top, right, bottom, 0.0f);
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
    dl_record_set_path_bounds(item, path, transform, 1.0f);
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
    float stroke_pad = width > 0.0f ? width * 4.0f + 2.0f : 2.0f;
    dl_record_set_path_bounds(item, path, transform, stroke_pad);
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
                             RdtFillRule rule, const RdtMatrix* transform,
                             const RdtMatrix* gradient_transform) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_FILL_LINEAR_GRADIENT;
    dl_record_set_path_bounds(item, path, transform, 1.0f);
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
    item->fill_linear_gradient.has_gradient_transform = (gradient_transform != nullptr);
    if (gradient_transform) item->fill_linear_gradient.gradient_transform = *gradient_transform;
}

void dl_fill_radial_gradient(DisplayList* dl, RdtPath* path,
                             float cx, float cy, float r,
                             const RdtGradientStop* stops, int stop_count,
                             RdtFillRule rule, const RdtMatrix* transform,
                             const RdtMatrix* gradient_transform) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_FILL_RADIAL_GRADIENT;
    dl_record_set_path_bounds(item, path, transform, 1.0f);
    item->fill_radial_gradient.path = rdt_path_clone(path);
    item->fill_radial_gradient.cx = cx;
    item->fill_radial_gradient.cy = cy;
    item->fill_radial_gradient.r = r;
    item->fill_radial_gradient.stops = dl_copy_stops(dl, stops, stop_count);
    item->fill_radial_gradient.stop_count = stop_count;
    item->fill_radial_gradient.rule = rule;
    item->fill_radial_gradient.has_transform = (transform != nullptr);
    if (transform) item->fill_radial_gradient.transform = *transform;
    item->fill_radial_gradient.has_gradient_transform = (gradient_transform != nullptr);
    if (gradient_transform) item->fill_radial_gradient.gradient_transform = *gradient_transform;
}

void dl_draw_image(DisplayList* dl, const uint32_t* pixels,
                   int src_w, int src_h, int src_stride,
                   float dst_x, float dst_y, float dst_w, float dst_h,
                   uint8_t opacity, const RdtMatrix* transform,
                   void* resource_owner, uint64_t resource_generation) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_DRAW_IMAGE;
    dl_record_set_rect_bounds(item, dst_x, dst_y, dst_w, dst_h, transform, 1.0f);
    item->draw_image.pixels = pixels;
    item->draw_image.resource_owner = resource_owner;
    item->draw_image.resource_generation = resource_generation;
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
                   const RdtMatrix* transform, uint64_t resource_generation) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_DRAW_GLYPH;
    dl_record_set_rect_bounds(item, (float)x, (float)y,
                              (float)bitmap->width, (float)bitmap->height,
                              transform, 1.0f);
    dl_record_intersect_bounds_with_clip(item, clip);
    item->draw_glyph.bitmap = *bitmap;  // copy descriptor, buffer pointer borrowed
    item->draw_glyph.resource_generation = resource_generation;
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
    float picture_w = 0.0f;
    float picture_h = 0.0f;
    if (picture) {
        rdt_picture_get_size(picture, &picture_w, &picture_h);
    }
    if (picture_w > 0.0f && picture_h > 0.0f) {
        dl_record_set_rect_bounds(item, 0.0f, 0.0f, picture_w, picture_h,
                                  transform, 1.0f);
    } else {
        dl_record_set_unbounded(item);
    }
    item->draw_picture.picture = picture;  // ownership transferred to display list
    item->draw_picture.opacity = opacity;
    item->draw_picture.has_transform = (transform != nullptr);
    if (transform) item->draw_picture.transform = *transform;
}

void dl_push_clip(DisplayList* dl, RdtPath* clip_path, const RdtMatrix* transform) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_PUSH_CLIP;
    dl_record_set_path_bounds(item, clip_path, transform, 1.0f);
    item->push_clip.path = rdt_path_clone(clip_path);
    item->push_clip.has_transform = (transform != nullptr);
    if (transform) item->push_clip.transform = *transform;
}

void dl_pop_clip(DisplayList* dl) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_POP_CLIP;
}
