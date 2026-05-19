#pragma once

#include "view.hpp"

struct RenderContext;

void render_marker_view(struct RenderContext* rdcon, ViewSpan* marker);
void render_list_bullet(struct RenderContext* rdcon, ViewBlock* list_item);
void render_litem_view(struct RenderContext* rdcon, ViewBlock* list_item);
void render_list_view(struct RenderContext* rdcon, ViewBlock* view);
