#pragma once

#include "view.hpp"

struct DocState;
struct RenderContext;

void render_simple_string(RenderContext* rdcon, const char* text, float x, float y,
                          FontProp* font, Color color);
void render_form_control(RenderContext* rdcon, ViewBlock* block);
void render_select_dropdown(RenderContext* rdcon, ViewBlock* select, DocState* state);
