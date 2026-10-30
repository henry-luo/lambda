#include "font_internal.h"
#include "font_gsub.h"
#include "../arraylist.hpp"
#include "../utf.h"
#include <utf8proc.h>

bool font_get_feature_glyph(FontHandle* handle, uint32_t feature, uint32_t glyph_index,
        uint32_t* result, bool* covered) {
    if (!handle || !result || glyph_index > UINT16_MAX || handle->resources_destroyed) return false;
    uint16_t index;
    if (!gsub_feature_glyph(font_tables_get_gsub(handle->tables), feature,
            (uint16_t)glyph_index, &index, covered)) return false;
    *result = index;
    return true;
}

bool font_vertical_glyph(FontHandle* handle, uint32_t glyph_index,
        float horizontal_advance, FontVerticalGlyph* result) {
    if (!result) return false;
    uint32_t index;
    if (!font_get_feature_glyph(handle, FONT_TAG('v','e','r','t'), glyph_index, &index, nullptr)) return false;
    FontLongMetrics* horizontal = font_tables_get_hmtx(handle->tables);
    HeadTable* head = font_tables_get_head(handle->tables);
    float scale = head && head->units_per_em ? handle->size_px / head->units_per_em : 0.0f;
    if (horizontal && scale > 0.0f)
        horizontal_advance = font_long_metrics_advance(horizontal, index) * scale;
    *result = {};
    result->glyph_index = index;
    result->origin_x = -horizontal_advance / 2.0f;
    // reuse the selected face's content cell when it lacks authored vertical metrics (Writing Modes §5.1.1).
    result->advance = font_get_cell_height(handle);
    if (result->advance <= 0.0f) result->advance = handle->size_px;
    result->origin_y = font_get_rendering_ascender(handle);
    FontVerticalMetrics metrics;
    if (scale > 0.0f && font_tables_vertical_metrics(handle->tables, index, &metrics)) {
        result->advance = metrics.advance * scale;
        if (metrics.has_origin) result->origin_y = metrics.origin_y * scale;
#ifdef __APPLE__
        else {
            float x, y;
            if (font_platform_vertical_translation(handle->ct_raster_ref, index, &x, &y)) {
                result->origin_x = x;
                result->origin_y = y;
            }
        }
#endif
    }
    return true;
}

struct FontUnitGlyph {
    uint32_t index;
    float x, y, advance;
};

struct FontUnitGlyphCollector {
    lam::ArrayList<FontUnitGlyph> glyphs;
    bool failed = false;
};

static void font_unit_collect_glyph(void* pointer, uint32_t index,
        float x, float y, float advance) {
    FontUnitGlyphCollector* collect = (FontUnitGlyphCollector*)pointer;
    collect->failed |= !collect->glyphs.append({index, x, y, advance});
}

bool font_visit_upright_unit(FontHandle* handle, const char* text, int byte_len,
        FontGlyphVisitFn visitor, void* context, float* advance) {
    if (!handle || !text || byte_len <= 0 || !advance) return false;
    FontUnitGlyphCollector collect;
    FontFaceTextRequest request = {text, byte_len, FONT_TEXT_LTR};
    FontFaceTextMetrics shaped;
    if (!font_visit_face_text(handle, &request, font_unit_collect_glyph, &collect, &shaped)) {
        // backends without a shaper retain scalar coverage and zero-advance marks.
        const char* cursor = text;
        const char* end = text + byte_len;
        float x = 0.0f;
        while (cursor < end) {
            uint32_t cp;
            int bytes = utf8_decode(cursor, (size_t)(end - cursor), &cp);
            if (bytes <= 0) return false;
            cursor += bytes;
            if (utf8proc_category(cp) == UTF8PROC_CATEGORY_CF ||
                cp == 0xfe0e || cp == 0xfe0f) continue;
            GlyphInfo glyph = font_get_glyph(handle, cp);
            if (!glyph.id) return false;
            font_unit_collect_glyph(&collect, glyph.id, x, 0.0f, glyph.advance_x);
            x += glyph.advance_x;
        }
    }
    if (collect.failed || !collect.glyphs.size()) return false;
    size_t base = 0;
    for (size_t i = 0; i < collect.glyphs.size(); i++) {
        if (collect.glyphs[i].advance > 0.0f) { base = i; break; }
    }
    FontUnitGlyph* anchor = &collect.glyphs[base];
    FontVerticalGlyph vertical;
    if (!font_vertical_glyph(handle, anchor->index, anchor->advance, &vertical)) return false;
    float origin_x = vertical.origin_x - anchor->x;
    float origin_y = vertical.origin_y - anchor->y;
    float unit_advance = vertical.advance;
    // validate all substitutions before emitting any callback; marks borrow the base unit's frame.
    for (size_t i = 0; i < collect.glyphs.size(); i++) {
        FontUnitGlyph* glyph = &collect.glyphs[i];
        if (!font_vertical_glyph(handle, glyph->index, glyph->advance, &vertical)) return false;
        glyph->index = vertical.glyph_index;
    }
    for (size_t i = 0; visitor && i < collect.glyphs.size(); i++) {
        const FontUnitGlyph* glyph = &collect.glyphs[i];
        visitor(context, glyph->index, origin_x + glyph->x, origin_y + glyph->y,
            i == base ? unit_advance : 0.0f);
    }
    *advance = unit_advance;
    return true;
}
