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
    Rect backdrop_filter_rect;
    CssEnum mix_blend_mode;
    float opacity;
    bool has_opacity_group;
    bool has_filter_backdrop;
    bool has_filter;
    bool has_backdrop_filter;
} RenderEffectGroup;

RenderEffectGroup render_effect_group_begin(RenderContext* rdcon,
                                            ViewBlock* block,
                                            const BlockBlot* parent_block);
bool render_effect_group_finish(RenderEffectGroup* group,
                                ViewBlock* block,
                                Bound* clip);
