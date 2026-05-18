#pragma once

#include "render.hpp"

typedef struct RenderEffectBackdrop {
    RenderContext* context;
    uint32_t* pixels;
    int x;
    int y;
    int width;
    int height;
    bool active;
} RenderEffectBackdrop;

typedef struct RenderEffectGroup {
    RenderContext* context;
    RenderEffectBackdrop mix_blend_backdrop;
    RenderEffectBackdrop opacity_backdrop;
    RenderEffectBackdrop filter_backdrop;
    Rect filter_rect;
    CssEnum mix_blend_mode;
    float opacity;
    bool has_opacity_group;
    bool has_filter_backdrop;
    bool has_filter;
} RenderEffectGroup;

RenderEffectBackdrop render_effect_backdrop_begin(RenderContext* rdcon,
                                                  float x0, float y0,
                                                  float x1, float y1);
bool render_effect_backdrop_active(const RenderEffectBackdrop* backdrop);
void render_effect_backdrop_finish_source_over(RenderEffectBackdrop* backdrop);
void render_effect_backdrop_finish_opacity(RenderEffectBackdrop* backdrop,
                                           float opacity);
void render_effect_backdrop_finish_blend(RenderEffectBackdrop* backdrop,
                                         CssEnum blend_mode);

RenderEffectGroup render_effect_group_begin(RenderContext* rdcon,
                                            ViewBlock* block,
                                            const BlockBlot* parent_block);
bool render_effect_group_has_filter_rect(const RenderEffectGroup* group);
bool render_effect_group_apply_filter(RenderEffectGroup* group,
                                      ViewBlock* block,
                                      Bound* clip);
bool render_effect_group_finish_filter_backdrop(RenderEffectGroup* group,
                                                ViewBlock* block);
bool render_effect_group_finish_opacity(RenderEffectGroup* group,
                                        ViewBlock* block);
bool render_effect_group_finish_blend(RenderEffectGroup* group,
                                      ViewBlock* block);
