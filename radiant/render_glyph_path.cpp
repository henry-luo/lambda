#include "render.hpp"
#include "layout.hpp"
#include "../lib/utf.h"
#include "../lib/str.h"

typedef struct RenderGlyphPathWriter {
    RdtPath* path;
    float x, y, scale_x;
} RenderGlyphPathWriter;

static bool render_font_path_command(void* context, FontPathCommand command,
    const float* values, int count) {
    RenderGlyphPathWriter* writer = (RenderGlyphPathWriter*)context;
    float args[6];
    for (int i = 0; i < count; i += 2) {
        args[i] = writer->x + values[i] * writer->scale_x;
        args[i + 1] = writer->y + values[i + 1];
    }
    switch (command) {
    case FONT_PATH_MOVE: rdt_path_move_to(writer->path, args[0], args[1]); break;
    case FONT_PATH_LINE: rdt_path_line_to(writer->path, args[0], args[1]); break;
    case FONT_PATH_CUBIC: rdt_path_cubic_to(writer->path, args[0], args[1],
        args[2], args[3], args[4], args[5]); break;
    case FONT_PATH_CLOSE: rdt_path_close(writer->path); break;
    }
    return true;
}

bool render_path_append_font_glyph(RdtPath* path, FontHandle* font, uint32_t codepoint,
    float x, float y, float scale_x, Arena* arena, bool* color_bitmap) {
    if (color_bitmap) *color_bitmap = false;
    if (!path || !font) return false;
    RenderGlyphPathWriter writer = {path, x, y, scale_x};
    GlyphInfo glyph = font_get_glyph(font, codepoint);
    if (!glyph.is_color && font_visit_glyph_path(font, codepoint, render_font_path_command, &writer, arena)) return true;
    const GlyphBitmap* bitmap = font_render_glyph(font, codepoint, GLYPH_RENDER_NORMAL);
    float size = font_handle_get_size_px(font), physical = font_handle_get_physical_size_px(font);
    if (!bitmap || size <= 0.0f || physical <= 0.0f) return false;
    float pixel_to_css = size / physical * (bitmap->bitmap_scale > 0.0f ? bitmap->bitmap_scale : 1.0f);
    if (color_bitmap) *color_bitmap = bitmap->pixel_mode == GLYPH_PIXEL_BGRA;
    return font_visit_bitmap_contours(bitmap, pixel_to_css, render_font_path_command, &writer, arena);
}

bool render_path_append_font_glyph_index(RdtPath* path, FontHandle* font, uint32_t glyph,
    float x, float y, float scale_x, Arena* arena) {
    if (!path || !font || !arena) return false;
    RenderGlyphPathWriter writer = {path, x, y, scale_x};
    return font_visit_glyph_index_path(font, glyph, render_font_path_command, &writer, arena);
}

RdtPath* render_path_create_glyph_run(const PaintGlyphRun* run) {
    if (!run || !run->font || !run->glyph_ids || !run->xs || !run->ys || run->count <= 0) return nullptr;
    FontHandle* font = font_box_handle(run->font);
    if (!font) return nullptr;
    RdtPath* path = rdt_path_new();
    Arena* scratch = mem_arena_create(mem_context_process(MEM_ROLE_RENDER), MEM_ROLE_RENDER, "render.glyph_run.outline");
    bool ok = path && scratch;
    for (int i = 0; ok && i < run->count; i++) {
        ok = render_path_append_font_glyph_index(path, font, run->glyph_ids.get()[i],
            run->x + run->xs.get()[i], run->baseline_y + run->ys.get()[i], 1.0f, scratch);
        arena_reset(scratch);
    }
    if (scratch) mem_arena_destroy(scratch);
    if (!ok) { if (path) rdt_path_free(path); return nullptr; }
    return path;
}

RdtPath* render_path_create_text_run(const PaintGlyphRun* run, FontContext* font_context) {
    FontBox* box = run ? (FontBox*)run->font : nullptr;
    FontHandle* primary = font_box_handle(box);
    if (!run || !run->text || !primary || !box->style) return nullptr;
    FontStyleDesc descriptor = font_style_desc_from_prop(box->style);
    RdtPath* path = rdt_path_new();
    Arena* scratch = mem_arena_create(mem_context_process(MEM_ROLE_RENDER), MEM_ROLE_RENDER, "render.text_run.outline");
    bool ok = path && scratch;
    float pen = run->x;
    const char* cursor = run->text;
    const char* end = cursor + (run->text_len >= 0 ? (size_t)run->text_len : strlen(cursor));
    uint32_t previous = 0;
    while (ok && cursor < end) {
        uint32_t codepoint = 0;
        int bytes = str_utf8_decode(cursor, (size_t)(end - cursor), &codepoint);
        if (bytes <= 0) { ok = false; break; }
        cursor += bytes;
        if (codepoint == 0xFE0F) continue;
        if (text_justify_cjk_gap(previous, codepoint)) pen += run->cjk_spacing;
        previous = codepoint;
        if (codepoint == ' ') {
            pen += max(0.0f, box->style->space_width + run->word_spacing) + box->style->letter_spacing;
            continue;
        }
        uint32_t next = 0;
        bool emoji = utf_is_emoji_presentation_default(codepoint) ||
            (cursor < end && str_utf8_decode(cursor, (size_t)(end - cursor), &next) > 0 && next == 0xFE0F);
        FontHandle* fallback = emoji ? font_resolve_for_emoji(font_context, &descriptor, codepoint)
            : !font_has_codepoint(primary, codepoint)
                ? font_resolve_for_codepoint(font_context, &descriptor, codepoint) : nullptr;
        FontHandle* face = fallback ? fallback : primary;
        // PDF text must use the selected Unicode face, never interpret UTF-8 as WinAnsi bytes.
        ok = font_has_codepoint(face, codepoint) && render_path_append_font_glyph(path, face,
            codepoint, pen, run->baseline_y, 1.0f, scratch, nullptr);
        pen += font_get_glyph(face, codepoint).advance_x + box->style->letter_spacing;
        if (fallback) font_handle_release(fallback);
        arena_reset(scratch);
    }
    if (scratch) mem_arena_destroy(scratch);
    if (!ok) { if (path) rdt_path_free(path); return nullptr; }
    return path;
}
