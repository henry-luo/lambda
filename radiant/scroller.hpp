#pragma once

#include "render.hpp"

struct EventContext;

void scroll_config_init(int pixel_ratio);

void scrollpane_render(RenderContext* rdcon, ScrollPane* sp, Rect* block_bound,
    float content_width, float content_height, Bound* clip, float scale,
    DocState* state, View* view,
    bool show_hz_scroll = true, bool show_vt_scroll = true);

void setup_scroller(RenderContext* rdcon, ViewBlock* block);
void render_scroller(RenderContext* rdcon, ViewBlock* block, BlockBlot* pa_block);
void scroll_apply_pending_element_scroll(ViewBlock* block);

void scrollpane_scroll(EventContext* evcon, ViewBlock* block, ScrollPane* sp);
bool scrollpane_target(EventContext* evcon, ViewBlock* block);
void scrollpane_mouse_up(EventContext* evcon, ViewBlock* block);
void scrollpane_mouse_down(EventContext* evcon, ViewBlock* block);
void scrollpane_drag(EventContext* evcon, ViewBlock* block);
