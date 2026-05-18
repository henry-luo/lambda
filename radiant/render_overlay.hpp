#pragma once

#include "view.hpp"

struct RenderContext;

void render_focus_outline(struct RenderContext* rdcon, DocState* state);
void render_caret(struct RenderContext* rdcon, DocState* state);
void render_selection(struct RenderContext* rdcon, DocState* state);
void render_ui_overlays(struct RenderContext* rdcon, DocState* state);
