#include "render.hpp"
#include <string.h>

static void dl_copy_effect_params(float* dst, int type, const float* params) {
    if (type && params) {
        memcpy(dst, params, 8 * sizeof(float));
    } else {
        memset(dst, 0, 8 * sizeof(float));
    }
}

static DisplayItem* dl_alloc_int_rect_effect_item(DisplayList* dl, DisplayOp op,
                                                  int x, int y, int w, int h) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = op;
    item->bounds[0] = (float)x; item->bounds[1] = (float)y;
    item->bounds[2] = (float)w; item->bounds[3] = (float)h;
    return item;
}

// ---------------------------------------------------------------------------
// Recording: post-processing operations
// ---------------------------------------------------------------------------

void dl_composite_opacity(DisplayList* dl, int x0, int y0, int w, int h,
                          float opacity, bool premultiplied_source) {
    DisplayItem* item = dl_alloc_int_rect_effect_item(dl, DL_COMPOSITE_OPACITY,
                                                      x0, y0, w, h);
    item->composite_opacity.x0 = x0;
    item->composite_opacity.y0 = y0;
    item->composite_opacity.w = w;
    item->composite_opacity.h = h;
    item->composite_opacity.opacity = opacity;
    item->composite_opacity.premultiplied_source = premultiplied_source;
}

void dl_save_backdrop(DisplayList* dl, int x0, int y0, int w, int h) {
    DisplayItem* item = dl_alloc_int_rect_effect_item(dl, DL_SAVE_BACKDROP,
                                                      x0, y0, w, h);
    item->save_backdrop.x0 = x0;
    item->save_backdrop.y0 = y0;
    item->save_backdrop.w = w;
    item->save_backdrop.h = h;
}

void dl_apply_blend_mode(DisplayList* dl, int x0, int y0, int w, int h,
                         int blend_mode) {
    DisplayItem* item = dl_alloc_int_rect_effect_item(dl, DL_APPLY_BLEND_MODE,
                                                      x0, y0, w, h);
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
    dl_set_clipped_rect_bounds(item, x, y, w, h, clip);
    item->apply_filter.x = x;
    item->apply_filter.y = y;
    item->apply_filter.w = w;
    item->apply_filter.h = h;
    item->apply_filter.filter = filter;
    item->apply_filter.clip = clip ? *clip : dl_unbounded_clip();
}

void dl_box_blur_region(DisplayList* dl, int rx, int ry, int rw, int rh, float blur_radius,
                        int clip_type, const float* clip_params,
                        int exclude_type, const float* exclude_params,
                        bool premultiply_source,
                        bool tint_source, Color tint_color) {
    DisplayItem* item = dl_alloc_item(dl);
    item->op = DL_BOX_BLUR_REGION;
    item->bounds[0] = (float)rx; item->bounds[1] = (float)ry;
    item->bounds[2] = (float)rw; item->bounds[3] = (float)rh;
    item->box_blur_region.rx = rx;
    item->box_blur_region.ry = ry;
    item->box_blur_region.rw = rw;
    item->box_blur_region.rh = rh;
    item->box_blur_region.blur_radius = blur_radius;
    item->box_blur_region.premultiply_source = premultiply_source;
    item->box_blur_region.tint_source = tint_source;
    item->box_blur_region.tint_color = tint_color;
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
