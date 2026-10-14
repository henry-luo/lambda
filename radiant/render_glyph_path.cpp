#include "render.hpp"
#include "layout.hpp"
#include "../lib/utf.h"
#include "../lib/str.h"

typedef struct RenderGlyphPathWriter {
    RdtPath* path;
    float x, y, scale_x;
} RenderGlyphPathWriter;

float render_font_pixel_to_css(FontHandle* font, const GlyphBitmap* bitmap) {
    float size = font_handle_get_size_px(font), physical = font_handle_get_physical_size_px(font);
    float scale = size > 0.0f && physical > 0.0f ? size / physical : 1.0f;
    return scale * (bitmap && bitmap->bitmap_scale > 0.0f ? bitmap->bitmap_scale : 1.0f);
}

bool render_text_synthesized_space_advance(const FontBox* font, uint32_t codepoint,
    float* advance) {
    if (!font || !font->style || !advance ||
        !font_get_synthesized_space_advance(font_box_handle(font), codepoint, advance)) return false;
    *advance = (font->style->font_size > 0.0f ? *advance : 0.0f) +
        text_character_spacing(font->style, codepoint);
    return true;
}

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
    float pixel_to_css = render_font_pixel_to_css(font, bitmap);
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
    if (!run || !paint_glyph_run_font(run) || !run->glyph_ids || !run->xs || !run->ys || run->count <= 0) return nullptr;
    FontHandle* font = font_box_handle(paint_glyph_run_font(run));
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

bool render_visit_text_run_glyphs(const PaintGlyphRun* run, FontContext* font_context,
    RenderTextRunGlyphFn visitor, void* context) {
    const FontBox* box = paint_glyph_run_font(run);
    FontHandle* primary = font_box_handle(box);
    if (!run || !run->text || !primary || !box->style || !visitor) return false;
    FontStyleDesc descriptor = font_style_desc_from_prop(box->style);
    bool ok = true;
    float pen = run->x;
    const char* cursor = run->text;
    const char* end = cursor + (run->text_len >= 0 ? (size_t)run->text_len : strlen(cursor));
    TextJustifyContext justify_context = {};
    while (ok && cursor < end) {
        uint32_t codepoint = 0;
        int bytes = str_utf8_decode(cursor, (size_t)(end - cursor), &codepoint);
        if (bytes <= 0) { ok = false; break; }
        cursor += bytes;
        bool character_gap = text_justify_character_gap(&justify_context, codepoint, run->justify_mode);
        // format and control characters (a preserved line feed in pre text) have no outline
        utf8proc_category_t category = utf8proc_category(codepoint);
        if (codepoint == 0xFE0F || codepoint == 0x00AD ||
            category == UTF8PROC_CATEGORY_CF || category == UTF8PROC_CATEGORY_CC) continue;
        if (character_gap) pen += run->cjk_spacing;
        if (codepoint == ' ') {
            pen += text_space_advance(box->style, run->word_spacing);
            continue;
        }
        float synthetic_advance = 0.0f;
        if (render_text_synthesized_space_advance(box, codepoint, &synthetic_advance)) {
            pen += synthetic_advance;
            continue;
        }
        uint32_t next = 0;
        bool emoji = utf_is_emoji_presentation_default(codepoint) ||
            (cursor < end && str_utf8_decode(cursor, (size_t)(end - cursor), &next) > 0 && next == 0xFE0F);
        FontHandle* fallback = emoji ? font_resolve_for_emoji(font_context, &descriptor, codepoint)
            : !font_has_codepoint(primary, codepoint)
                ? font_resolve_for_codepoint(font_context, &descriptor, codepoint) : nullptr;
        FontHandle* face = fallback ? fallback : primary;
        // exported text uses the selected Unicode face instead of a viewer's replacement font.
        float advance = font_get_glyph(face, codepoint).advance_x;
        ok = font_has_codepoint(face, codepoint) &&
            visitor(context, codepoint, face, pen, run->baseline_y, advance);
        pen += advance + text_character_spacing(box->style, codepoint);
        if (fallback) font_handle_release(fallback);
    }
    return ok;
}

struct RenderTextOutlineContext {
    RdtPath* path;
    Arena* scratch;
};

static bool render_text_outline_glyph(void* pointer, uint32_t codepoint,
    FontHandle* face, float x, float baseline_y, float advance) {
    RenderTextOutlineContext* context = (RenderTextOutlineContext*)pointer;
    bool ok = render_path_append_font_glyph(context->path, face, codepoint,
        x, baseline_y, 1.0f, context->scratch, nullptr);
    arena_reset(context->scratch);
    return ok;
}

RdtPath* render_path_create_text_run(const PaintGlyphRun* run, FontContext* font_context) {
    RdtPath* path = rdt_path_new();
    Arena* scratch = mem_arena_create(mem_context_process(MEM_ROLE_RENDER), MEM_ROLE_RENDER, "render.text_run.outline");
    RenderTextOutlineContext context = {path, scratch};
    bool ok = path && scratch &&
        render_visit_text_run_glyphs(run, font_context, render_text_outline_glyph, &context);
    if (scratch) mem_arena_destroy(scratch);
    if (!ok) { if (path) rdt_path_free(path); return nullptr; }
    return path;
}

bool render_text_emphasis_resolve(FontContext* fonts, const FontBox* owner,
    Color foreground, RenderTextEmphasis* emphasis) {
    if (!emphasis) return false;
    *emphasis = {};
    if (!owner || !owner->style || !owner->style->text_emphasis_enabled) return true;
    FontHandle* primary = font_box_handle(owner);
    if (!fonts || !primary) return false;
    emphasis->style = font_style_desc_from_prop(owner->style);
    emphasis->style.size_px *= 0.5f;
    if (!emphasis->style.family)
        emphasis->style.family = font_handle_get_family_name(primary);
    emphasis->codepoint = owner->style->text_emphasis_mark;
    emphasis->handle = font_resolve_for_codepoint(fonts, &emphasis->style, emphasis->codepoint);
    if (!emphasis->handle) return false;
    const FontMetrics* mark = font_get_metrics(emphasis->handle);
    float ascent = 0.0f, descent = 0.0f;
    font_get_content_area_split(primary, &ascent, &descent);
    float mark_ascent = mark ? mark->typo_ascender : 0.0f;
    float mark_descent = mark ? mark->typo_descender : 0.0f;
    if (!(mark_ascent >= 0.0f && mark_descent >= 0.0f && mark_ascent + mark_descent > 0.0f)) {
        mark_ascent = mark ? mark->ascender : 0.0f;
        mark_descent = mark ? -mark->descender : 0.0f;
    }
    float height = mark_ascent + mark_descent;
    if (!(height > 0.0f && ascent >= 0.0f && descent >= 0.0f)) {
        render_text_emphasis_release(emphasis); return false;
    }
    // ruby placement uses the mark's em box, not its hhea cell or a font-size multiplier (FT-17).
    float size = font_handle_get_size_px(emphasis->handle);
    mark_ascent *= size / height;
    mark_descent *= size / height;
    emphasis->baseline_offset = owner->style->text_emphasis_under
        ? ceilf(descent + mark_ascent) : -ceilf(ascent + mark_descent);
    emphasis->color = owner->style->text_emphasis_color_current
        ? foreground : owner->style->text_emphasis_color;
    return true;
}

float render_text_baseline_y(const FontBox* font, float text_top) {
    float ascent = font_get_rendering_ascender(font_box_handle(font));
    if (!(ascent > 0.0f) && font && font->style)
        ascent = font->style->ascender > 0.0f ? font->style->ascender : font->style->font_size * 0.8f;
    // TextRect top already includes leading; adding the styled ascender counts it twice (FT-15/FT-17).
    return text_top + ascent;
}

void render_text_emphasis_release(RenderTextEmphasis* emphasis) {
    if (!emphasis) return;
    if (emphasis->handle) font_handle_release(emphasis->handle);
    *emphasis = {};
}

void render_text_emphasis_place(const RenderTextEmphasis* emphasis,
    float x, float baseline_y, float advance, float mark_advance,
    float* mark_x, float* mark_baseline) {
    *mark_x = x + (advance - mark_advance) * 0.5f;
    *mark_baseline = baseline_y + emphasis->baseline_offset;
}

RdtPath* render_path_create_paint_glyph_run(const PaintGlyphRun* run, FontContext* fonts) {
    if (!run) return nullptr;
    return run->glyph_ids && run->count > 0 ? render_path_create_glyph_run(run)
        : render_path_create_text_run(run, fonts);
}
