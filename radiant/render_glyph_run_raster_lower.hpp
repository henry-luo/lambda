#pragma once
// internal implementation header — do not include outside radiant/

#include "render.hpp"
#include "view.hpp"
#include "../lib/str.h"
#include <math.h>
#include <string.h>

static inline void render_glyph_run_raster_lower(const PaintGlyphRun* run,
                                                 DisplayList* dl) {
    if (!run || !dl || !run->text) return;

    FontBox* font = (FontBox*)run->font;
    FontHandle* font_handle = font ? font->font_handle : nullptr;
    FontProp* style = font ? font->style : nullptr;
    FontStyleDesc style_desc = {};
    if (style) {
        style_desc = font_style_desc_from_prop(style);
    }

    int text_len = run->text_len;
    if (text_len < 0) {
        text_len = (int)strlen(run->text); // INT_CAST_OK: PaintGlyphRun text length is bounded by source text.
    }
    if (text_len <= 0) return;

    float space_width = style ? style->space_width : 4.0f;
    float adjusted_space_width = space_width + run->word_spacing;
    if (adjusted_space_width < 0.0f) adjusted_space_width = space_width;

    float x = run->x;
    const char* cursor = run->text;
    const char* end = run->text + text_len;
    while (cursor < end) {
        uint32_t codepoint = 0;
        int bytes = str_utf8_decode(cursor, (size_t)(end - cursor), &codepoint);
        if (bytes <= 0) {
            cursor++;
            x += space_width;
            continue;
        }
        cursor += bytes;

        if (codepoint == 0xFE0F) {
            continue;
        }
        if (codepoint == (uint32_t)' ') {
            x += adjusted_space_width;
            continue;
        }

        bool emoji_presentation = false;
        if (cursor < end) {
            uint32_t peek_cp = 0;
            int peek_bytes = str_utf8_decode(cursor, (size_t)(end - cursor), &peek_cp);
            if (peek_bytes > 0 && peek_cp == 0xFE0F) {
                emoji_presentation = true;
            }
        }

        LoadedGlyph* glyph = nullptr;
        if (font_handle) {
            glyph = emoji_presentation
                ? font_load_glyph_emoji(font_handle, &style_desc, codepoint, true)
                : font_load_glyph(font_handle, &style_desc, codepoint, true);
        }
        if (!glyph) {
            x += space_width;
            continue;
        }

        int glyph_x = (int)lroundf(x + (float)glyph->bitmap.bearing_x); // INT_CAST_OK: glyph bitmap destination is an integer pixel coordinate.
        int glyph_y = (int)lroundf(run->baseline_y - (float)glyph->bitmap.bearing_y); // INT_CAST_OK: glyph bitmap destination is an integer pixel coordinate.
        dl_draw_glyph(dl, &glyph->bitmap, glyph_x, glyph_y,
                      run->color, emoji_presentation, nullptr,
                      run->has_transform ? &run->transform : nullptr,
                      0);
        x += glyph->advance_x;
    }
}
