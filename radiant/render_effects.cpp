#include "render_effects.hpp"

#include "render_composite.hpp"
#include "render_filter.hpp"
#include "render_geometry.hpp"
#include "render_profiler.hpp"

#include "../lib/log.h"

#include <math.h>
#include <stddef.h>

static RenderEffectBackdrop render_effect_empty_backdrop(RenderContext* rdcon) {
    RenderEffectBackdrop backdrop = {};
    backdrop.context = rdcon;
    return backdrop;
}

RenderEffectBackdrop render_effect_backdrop_begin(RenderContext* rdcon,
                                                  float x0, float y0,
                                                  float x1, float y1) {
    RenderEffectBackdrop backdrop = render_effect_empty_backdrop(rdcon);
    if (!rdcon || !rdcon->ui_context) {
        return backdrop;
    }
    ImageSurface* surface = rdcon->ui_context->surface;
    if (!surface || !surface->pixels) {
        return backdrop;
    }

    int bx0 = (int)x0;
    int by0 = (int)y0;
    int bx1 = (int)x1;
    int by1 = (int)y1;
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 > surface->width) bx1 = surface->width;
    if (by1 > surface->height) by1 = surface->height;

    int width = bx1 - bx0;
    int height = by1 - by0;
    if (width <= 0 || height <= 0) {
        return backdrop;
    }

    backdrop.x = bx0;
    backdrop.y = by0;
    backdrop.width = width;
    backdrop.height = height;

    if (rdcon->dl) {
        dl_save_backdrop(rdcon->dl, bx0, by0, width, height);
        backdrop.active = true;
        return backdrop;
    }

    backdrop.pixels = (uint32_t*)scratch_alloc(&rdcon->scratch, (size_t)width * height * sizeof(uint32_t));
    if (!backdrop.pixels) {
        return backdrop;
    }
    if (!render_composite_copy_backdrop(surface, backdrop.pixels, bx0, by0, width, height, true)) {
        scratch_free(&rdcon->scratch, backdrop.pixels);
        backdrop.pixels = nullptr;
        return backdrop;
    }

    backdrop.active = true;
    return backdrop;
}

bool render_effect_backdrop_active(const RenderEffectBackdrop* backdrop) {
    return backdrop && backdrop->active && backdrop->width > 0 && backdrop->height > 0;
}

void render_effect_backdrop_finish_source_over(RenderEffectBackdrop* backdrop) {
    if (!render_effect_backdrop_active(backdrop)) {
        return;
    }
    RenderContext* rdcon = backdrop->context;
    if (rdcon->dl) {
        dl_composite_opacity(rdcon->dl, backdrop->x, backdrop->y,
                             backdrop->width, backdrop->height, 1.0f);
    } else if (backdrop->pixels) {
        render_composite_source_over_premul(rdcon->ui_context->surface, backdrop->pixels,
                                            backdrop->x, backdrop->y,
                                            backdrop->width, backdrop->height);
        scratch_free(&rdcon->scratch, backdrop->pixels);
        backdrop->pixels = nullptr;
    }
    backdrop->active = false;
}

void render_effect_backdrop_finish_opacity(RenderEffectBackdrop* backdrop,
                                           float opacity) {
    if (!render_effect_backdrop_active(backdrop)) {
        return;
    }
    RenderContext* rdcon = backdrop->context;
    if (rdcon->dl) {
        dl_composite_opacity(rdcon->dl, backdrop->x, backdrop->y,
                             backdrop->width, backdrop->height, opacity);
    } else if (backdrop->pixels) {
        render_composite_opacity(rdcon->ui_context->surface, backdrop->pixels,
                                 backdrop->x, backdrop->y,
                                 backdrop->width, backdrop->height, opacity);
        scratch_free(&rdcon->scratch, backdrop->pixels);
        backdrop->pixels = nullptr;
    }
    backdrop->active = false;
}

void render_effect_backdrop_finish_blend(RenderEffectBackdrop* backdrop,
                                         CssEnum blend_mode) {
    if (!render_effect_backdrop_active(backdrop)) {
        return;
    }
    RenderContext* rdcon = backdrop->context;
    if (rdcon->dl) {
        dl_apply_blend_mode(rdcon->dl, backdrop->x, backdrop->y,
                            backdrop->width, backdrop->height, (int)blend_mode);
    } else if (backdrop->pixels) {
        render_composite_apply_blend(rdcon->ui_context->surface, backdrop->pixels,
                                     backdrop->x, backdrop->y,
                                     backdrop->width, backdrop->height, blend_mode);
        scratch_free(&rdcon->scratch, backdrop->pixels);
        backdrop->pixels = nullptr;
    }
    backdrop->active = false;
}

static void render_effect_filter_backdrop_info(const FilterProp* filter,
                                               bool* has_backdrop,
                                               float* expand) {
    *has_backdrop = false;
    *expand = 0;
    if (!filter || !filter->functions) {
        return;
    }

    bool has_drop_shadow = false;
    bool has_filter_opacity = false;
    float ds_offset_x = 0;
    float ds_offset_y = 0;
    float ds_blur = 0;
    float filter_blur_max = 0;

    FilterFunction* ff = filter->functions;
    while (ff) {
        if (ff->type == FILTER_DROP_SHADOW) {
            has_drop_shadow = true;
            ds_offset_x = ff->params.drop_shadow.offset_x;
            ds_offset_y = ff->params.drop_shadow.offset_y;
            ds_blur = ff->params.drop_shadow.blur_radius;
        } else if (ff->type == FILTER_BLUR) {
            if (ff->params.blur_radius > filter_blur_max) {
                filter_blur_max = ff->params.blur_radius;
            }
        } else if (ff->type == FILTER_OPACITY) {
            has_filter_opacity = true;
        }
        ff = ff->next;
    }

    // Backdrop save is needed only for filters that produce alpha which must
    // composite over the underlying surface.
    *has_backdrop = has_drop_shadow || has_filter_opacity;
    if (!*has_backdrop) {
        return;
    }

    float ds_expand = has_drop_shadow ?
        (ceilf(fabsf(ds_offset_x)) +
         ceilf(fabsf(ds_offset_y)) +
         ceilf(ds_blur) + 2) : 0;
    float blur_expand = ceilf(filter_blur_max * 2.0f);
    *expand = ds_expand > blur_expand ? ds_expand : blur_expand;
}

RenderEffectGroup render_effect_group_begin(RenderContext* rdcon,
                                            ViewBlock* block,
                                            const BlockBlot* parent_block) {
    RenderEffectGroup group = {};
    group.context = rdcon;
    if (!rdcon || !block || !parent_block) {
        return group;
    }

    group.mix_blend_mode = (block->in_line && block->in_line->mix_blend_mode &&
                            block->in_line->mix_blend_mode != CSS_VALUE_NORMAL)
                           ? block->in_line->mix_blend_mode : (CssEnum)0;
    group.has_opacity_group = block->in_line &&
        block->in_line->opacity < 1.0f && block->in_line->opacity >= 0.0f;
    group.opacity = group.has_opacity_group ? block->in_line->opacity : 1.0f;
    group.has_filter = block->filter && block->filter->functions;

    float scale = rdcon->scale;
    float x0 = parent_block->x + block->x * scale;
    float y0 = parent_block->y + block->y * scale;
    float x1 = x0 + block->width * scale;
    float y1 = y0 + block->height * scale;

    if (group.mix_blend_mode) {
        group.mix_blend_backdrop = render_effect_backdrop_begin(rdcon, x0, y0, x1, y1);
        if (group.mix_blend_backdrop.pixels) {
            log_debug("[MIX-BLEND] Saved backdrop %dx%d for <%s>",
                      group.mix_blend_backdrop.width,
                      group.mix_blend_backdrop.height,
                      block->node_name());
        }
    }

    if (group.has_opacity_group) {
        group.opacity_backdrop = render_effect_backdrop_begin(rdcon, x0, y0, x1, y1);
        if (group.opacity_backdrop.pixels) {
            log_debug("[OPACITY] Saved backdrop %dx%d for <%s>",
                      group.opacity_backdrop.width,
                      group.opacity_backdrop.height,
                      block->node_name());
        }
    }

    if (group.has_filter) {
        float filter_expand = render_geometry_filter_effect_expand(block->filter);
        Rect border_rect = render_geometry_block_border_rect(parent_block, block, scale);
        group.filter_rect = render_geometry_expand_rect(border_rect, filter_expand);

        float backdrop_expand = 0;
        render_effect_filter_backdrop_info(block->filter,
                                           &group.has_filter_backdrop,
                                           &backdrop_expand);
        if (group.has_filter_backdrop) {
            group.filter_backdrop = render_effect_backdrop_begin(rdcon,
                x0 - backdrop_expand, y0 - backdrop_expand,
                x1 + backdrop_expand, y1 + backdrop_expand);
            if (group.filter_backdrop.pixels) {
                log_debug("[DROP-SHADOW] Saved backdrop %dx%d for <%s>",
                          group.filter_backdrop.width,
                          group.filter_backdrop.height,
                          block->node_name());
            }
        }
    }

    return group;
}

bool render_effect_group_has_filter_rect(const RenderEffectGroup* group) {
    return group && group->has_filter;
}

bool render_effect_group_apply_filter(RenderEffectGroup* group,
                                      ViewBlock* block,
                                      Bound* clip) {
    if (!group || !group->has_filter || !group->context || !block || !block->filter) {
        return false;
    }
    RenderContext* rdcon = group->context;
    Rect filter_rect = group->filter_rect;

    log_debug("[FILTER] Applying filters to element %s at (%.0f,%.0f) size %.0fx%.0f",
              block->node_name(), filter_rect.x, filter_rect.y,
              filter_rect.width, filter_rect.height);

    if (rdcon->dl) {
        dl_apply_filter(rdcon->dl, filter_rect.x, filter_rect.y,
                        filter_rect.width, filter_rect.height,
                        block->filter, clip);
    } else {
        const RenderBackendCaps* caps = render_backend_get_caps(&rdcon->vec);
        render_filter_apply_with_backend(caps, &rdcon->scratch, rdcon->ui_context->surface,
                                         block->filter, &filter_rect, clip);
    }
    return true;
}

bool render_effect_group_finish_filter_backdrop(RenderEffectGroup* group,
                                                ViewBlock* block) {
    (void)block;
    if (!group || !group->has_filter_backdrop ||
        !render_effect_backdrop_active(&group->filter_backdrop)) {
        return false;
    }
    bool direct_backdrop = group->filter_backdrop.pixels != nullptr;
    int width = group->filter_backdrop.width;
    int height = group->filter_backdrop.height;
    render_effect_backdrop_finish_source_over(&group->filter_backdrop);
    if (direct_backdrop) {
        log_debug("[FILTER] Composited filtered element over backdrop %dx%d",
                  width, height);
    }
    return true;
}

bool render_effect_group_finish_opacity(RenderEffectGroup* group,
                                        ViewBlock* block) {
    if (!group || !group->has_opacity_group ||
        !render_effect_backdrop_active(&group->opacity_backdrop)) {
        return false;
    }
    bool direct_backdrop = group->opacity_backdrop.pixels != nullptr;
    int x = group->opacity_backdrop.x;
    int y = group->opacity_backdrop.y;
    int width = group->opacity_backdrop.width;
    int height = group->opacity_backdrop.height;
    render_effect_backdrop_finish_opacity(&group->opacity_backdrop, group->opacity);
    if (direct_backdrop) {
        log_debug("[OPACITY] Composited opacity=%.2f on <%s> region (%d,%d) %dx%d",
                  group->opacity, block ? block->node_name() : "unknown",
                  x, y, width, height);
    }
    return true;
}

bool render_effect_group_finish_blend(RenderEffectGroup* group,
                                      ViewBlock* block) {
    if (!group || !group->mix_blend_mode ||
        !render_effect_backdrop_active(&group->mix_blend_backdrop)) {
        return false;
    }
    bool direct_backdrop = group->mix_blend_backdrop.pixels != nullptr;
    int width = group->mix_blend_backdrop.width;
    int height = group->mix_blend_backdrop.height;
    render_effect_backdrop_finish_blend(&group->mix_blend_backdrop,
                                        group->mix_blend_mode);
    if (direct_backdrop) {
        log_debug("[MIX-BLEND] Applied mix-blend-mode on <%s> %dx%d",
                  block ? block->node_name() : "unknown", width, height);
    }
    return true;
}

bool render_effect_group_finish(RenderEffectGroup* group,
                                ViewBlock* block,
                                Bound* clip) {
    if (!group || !group->context) {
        return false;
    }

    RenderContext* rdcon = group->context;
    bool finished = false;

    if (render_effect_group_has_filter_rect(group)) {
        double start_ms = render_profiler_now_ms();
        finished = render_effect_group_apply_filter(group, block, clip) || finished;
        render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_FILTER,
                                   render_profiler_now_ms() - start_ms);
    }

    finished = render_effect_group_finish_filter_backdrop(group, block) || finished;

    if (render_effect_backdrop_active(&group->opacity_backdrop)) {
        double start_ms = render_profiler_now_ms();
        finished = render_effect_group_finish_opacity(group, block) || finished;
        render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_OPACITY,
                                   render_profiler_now_ms() - start_ms);
    }

    if (render_effect_backdrop_active(&group->mix_blend_backdrop)) {
        double start_ms = render_profiler_now_ms();
        finished = render_effect_group_finish_blend(group, block) || finished;
        render_profiler_add_sample(rdcon->profiler, RENDER_PROFILE_BLEND,
                                   render_profiler_now_ms() - start_ms);
    }

    return finished;
}
