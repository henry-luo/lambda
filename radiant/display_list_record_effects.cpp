#include "display_list.h"

#include "display_list_storage.hpp"
#include <string.h>

static Bound dl_default_effect_clip() {
    Bound clip = {0, 0, 99999, 99999};
    return clip;
}

static float dl_effect_min_f(float a, float b) {
    return a < b ? a : b;
}

static float dl_effect_max_f(float a, float b) {
    return a > b ? a : b;
}

static void dl_effect_set_bounds_xyxy(DisplayItem* item,
                                      float left, float top,
                                      float right, float bottom) {
    if (!item) return;
    if (right <= left || bottom <= top) {
        item->bounds[0] = left;
        item->bounds[1] = top;
        item->bounds[2] = 0.0f;
        item->bounds[3] = 0.0f;
        return;
    }
    item->bounds[0] = left;
    item->bounds[1] = top;
    item->bounds[2] = right - left;
    item->bounds[3] = bottom - top;
}

static void dl_effect_set_clipped_rect_bounds(DisplayItem* item,
                                              float x, float y, float w, float h,
                                              const Bound* clip) {
    float left = x;
    float top = y;
    float right = x + w;
    float bottom = y + h;
    if (clip) {
        left = dl_effect_max_f(left, clip->left);
        top = dl_effect_max_f(top, clip->top);
        right = dl_effect_min_f(right, clip->right);
        bottom = dl_effect_min_f(bottom, clip->bottom);
    }
    dl_effect_set_bounds_xyxy(item, left, top, right, bottom);
}

static void dl_copy_effect_params(float* dst, int type, const float* params) {
    if (type && params) {
        memcpy(dst, params, 8 * sizeof(float));
    } else {
        memset(dst, 0, 8 * sizeof(float));
    }
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
    dl_effect_set_clipped_rect_bounds(item, x, y, w, h, clip);
    item->apply_filter.x = x;
    item->apply_filter.y = y;
    item->apply_filter.w = w;
    item->apply_filter.h = h;
    item->apply_filter.filter = filter;
    item->apply_filter.clip = clip ? *clip : dl_default_effect_clip();
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
    dl_copy_effect_params(item->box_blur_region.clip_params, clip_type, clip_params);
    item->box_blur_region.exclude_type = exclude_type;
    dl_copy_effect_params(item->box_blur_region.exclude_params, exclude_type, exclude_params);
}

void dl_box_blur_inset(DisplayList* dl, int rx, int ry, int rw, int rh, int pad,
                       float blur_radius, uint32_t bg_color) {
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
    dl_copy_effect_params(item->shadow_clip_restore.exclude_params,
                          exclude_type, exclude_params);
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
    // bounds = blur region, used for tile culling
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
    dl_copy_effect_params(o->exclude_params, exclude_type, exclude_params);
    o->clip_type = clip_type;
    dl_copy_effect_params(o->clip_params, clip_type, clip_params);
}
