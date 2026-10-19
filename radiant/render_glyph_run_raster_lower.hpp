#pragma once
// internal implementation header — do not include outside radiant/

#include "render.hpp"
#include "layout.hpp"
#include "view.hpp"
#include "../lib/str.h"
#include "../lib/utf.h"
#include <math.h>
#include <string.h>

static inline void render_glyph_run_bitmap_raster_lower(const PaintGlyphRun* run,
        DisplayList* dl, const GlyphBitmap* source, float x, float baseline_y) {
    if (!source) { log_error("[RASTER_TEXT_CLUSTER] selected glyph bitmap is unavailable"); return; }
    if (!source->buffer || source->width <= 0 || source->height <= 0) return;
    FontHandle* font_handle = font_box_handle(paint_glyph_run_font(run));
    // effect paint coordinates are CSS pixels; cached glyphs follow the font context's physical density (RSC6).
    GlyphBitmap bitmap = *source;
    float bitmap_scale = render_font_pixel_to_css(font_handle, &bitmap);
    int glyph_x = (int)lroundf(x + (float)bitmap.bearing_x * bitmap_scale); // INT_CAST_OK: glyph bitmap destination is an integer pixel coordinate.
    int glyph_y = (int)lroundf(baseline_y - (float)bitmap.bearing_y * bitmap_scale); // INT_CAST_OK: glyph bitmap destination is an integer pixel coordinate.
    bool color = bitmap.pixel_mode == GLYPH_PIXEL_BGRA;
    RdtMatrix placement;
    const RdtMatrix* transform = run->has_transform ? &run->transform : nullptr;
    if (color) bitmap.bitmap_scale = bitmap_scale;
    else if (bitmap_scale != 1.0f) {
        placement = {bitmap_scale, 0.0f, (float)glyph_x,
            0.0f, bitmap_scale, (float)glyph_y, 0.0f, 0.0f, 1.0f};
        if (run->has_transform) placement = rdt_matrix_multiply(&run->transform, &placement);
        transform = &placement;
        glyph_x = glyph_y = 0;
    }
    dl_draw_glyph(dl, &bitmap, glyph_x, glyph_y, run->color, color, nullptr, transform, 0);
}

struct RenderRasterTextCluster {
    const PaintGlyphRun* run;
    DisplayList* list;
    float x;
};

static inline void render_raster_cluster_glyph(void* pointer,
        const GlyphBitmap* bitmap, float x, float y) {
    RenderRasterTextCluster* cluster = (RenderRasterTextCluster*)pointer;
    render_glyph_run_bitmap_raster_lower(cluster->run, cluster->list, bitmap,
        cluster->x + x, cluster->run->baseline_y + y);
}

static inline void render_glyph_run_raster_lower(const PaintGlyphRun* run,
                                                 DisplayList* dl) {
    if (!run || !dl) return;
    if (run->count > 0 && run->glyph_ids) {
        RdtPath* path = render_path_create_glyph_run(run);
        if (!path) { log_error("[RASTER_GLYPH_RUN] selected glyph outline could not be painted"); return; }
        dl_fill_path(dl, path, run->color, RDT_FILL_WINDING, run->has_transform ? &run->transform : nullptr);
        rdt_path_free(path);
        return;
    }
    if (!run->text) return;

    const FontBox* font = paint_glyph_run_font(run);
    FontHandle* font_handle = font ? font_box_handle(font) : nullptr;
    float metric_scale = render_font_pixel_to_css(font_handle);
    FontProp* style = font ? font->style : nullptr;
    if (style && style->font_size <= 0.0f) return;
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
    float adjusted_space_width = text_space_advance(style, run->word_spacing);

    float x = run->x;
    const char* cursor = run->text;
    const char* end = run->text + text_len;
    TextJustifyContext justify_context = {};
    while (cursor < end) {
        const char* source_start = cursor;
        uint32_t codepoint = 0;
        if (!layout_utf8_next_codepoint(&cursor, end, &codepoint)) {
            x += space_width;
            continue;
        }

        bool character_gap = text_justify_character_gap(&justify_context, codepoint, run->justify_mode);
        if (codepoint == 0xFE0F || codepoint == 0x00AD ||
            utf8proc_category(codepoint) == UTF8PROC_CATEGORY_CF ||
            utf8proc_category(codepoint) == UTF8PROC_CATEGORY_CC) {
            continue;
        }
        if (run->cjk_spacing > 0.0f && character_gap)
            x += run->cjk_spacing;
        if (codepoint == (uint32_t)' ') {
            x += adjusted_space_width;
            continue;
        }

        float synthetic_advance = 0.0f;
        if (render_text_synthesized_space_advance(font, codepoint, &synthetic_advance)) {
            x += synthetic_advance;
            continue;
        }

        RenderRasterTextCluster cluster = {run, dl, x};
        size_t cluster_bytes = 0; float cluster_advance = 0.0f;
        if (render_text_visit_cluster(font, (const unsigned char*)source_start,
                (const unsigned char*)end, render_raster_cluster_glyph, &cluster,
                &cluster_bytes, &cluster_advance)) {
            text_justify_cluster_gap(&justify_context, (const unsigned char*)cursor,
                (const unsigned char*)source_start + cluster_bytes, run->justify_mode);
            cursor = source_start + cluster_bytes;
            x += cluster_advance;
            continue;
        }

        bool emoji_presentation = utf_is_emoji_presentation_default(codepoint);
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
            Rect rect = render_text_missing_glyph_rect(font, x,
                run->baseline_y - font_get_rendering_ascender(font_handle), space_width);
            RdtPath* path = rdt_path_new();
            if (!path) return;
            rdt_path_add_rect(path, rect.x, rect.y, rect.width, rect.height, 0.0f, 0.0f);
            dl_fill_path(dl, path, Color{0xFF0000FF}, RDT_FILL_WINDING,
                run->has_transform ? &run->transform : nullptr);
            rdt_path_free(path);
            x += space_width;
            continue;
        }

        // a glyph without ink (a preserved line feed, a blank glyph) only advances;
        // the display list rejects an empty bitmap
        if (!glyph->bitmap.buffer || glyph->bitmap.width <= 0 || glyph->bitmap.height <= 0) {
            x += glyph->advance_x * metric_scale + text_character_spacing(style, codepoint);
            continue;
        }
        render_glyph_run_bitmap_raster_lower(run, dl, &glyph->bitmap, x, run->baseline_y);
        x += glyph->advance_x * metric_scale + text_character_spacing(style, codepoint);
    }
}
