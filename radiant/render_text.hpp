#pragma once

#include "render.hpp"

void render_text_view(RenderContext* rdcon, ViewText* text_view);
void render_text_inline_background(RenderContext* rdcon, ViewText* text_view,
                                   TextRect* text_rect, DomElement* parent_elem,
                                   float x, float y);
bool render_text_paint_blurred_shadows(RenderContext* rdcon, unsigned char* str,
                                       TextRect* text_rect, TextShadow* text_shadow,
                                       CssEnum text_transform, bool preserve_spaces,
                                       float space_width, float scaled_space_width,
                                       float x, float y);
LoadedGlyph* render_text_load_glyph_for_paint(RenderContext* rdcon, uint32_t codepoint,
                                              unsigned char* cursor, unsigned char* end);
void render_text_paint_glyph_shadows(RenderContext* rdcon, LoadedGlyph* glyph,
                                     TextShadow* text_shadow, float x, float y,
                                     float ascend);
float render_text_trailing_marks(RenderContext* rdcon, TextRect* text_rect,
                                 float x, float y);
void render_text_decorations(RenderContext* rdcon, unsigned char* str, TextRect* text_rect);
