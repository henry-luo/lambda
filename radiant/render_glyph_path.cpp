#include "render.hpp"
#include "layout.hpp"
#include "../lib/utf.h"
#include "../lib/str.h"

typedef struct RenderGlyphPathWriter {
    RdtPath* path;
    float x, y, scale_x;
    const RdtMatrix* placement;
} RenderGlyphPathWriter;

float render_font_pixel_to_css(FontHandle* font, const GlyphBitmap* bitmap) {
    float size = font_handle_get_size_px(font), physical = font_handle_get_physical_size_px(font);
    float scale = size > 0.0f && physical > 0.0f ? size / physical : 1.0f;
    return scale * (bitmap && bitmap->bitmap_scale > 0.0f ? bitmap->bitmap_scale : 1.0f);
}

Rect render_text_missing_glyph_rect(const FontBox* font, float x, float top, float space_width) {
    FontHandle* handle = font_box_handle(font);
    float size = font_handle_get_size_px(handle);
    const FontMetrics* metrics = font_get_metrics(handle);
    float height = size > 0.0f ? size : metrics ? metrics->hhea_line_height / 1.2f : 16.0f;
    float pixel = render_font_pixel_to_css(handle);
    return {x + pixel, top, fmaxf(0.0f, space_width - 2.0f * pixel), height};
}

Color render_text_missing_glyph_color() { return Color{0xFF0000FF}; }

bool render_text_synthesized_space_advance(const FontBox* font, uint32_t codepoint,
    float* advance) {
    if (!font || !font->style || !advance ||
        !font_get_synthesized_space_advance(font_box_handle(font), codepoint, advance)) return false;
    *advance = (font->style->font_size > 0.0f ? *advance : 0.0f) +
        text_character_spacing(font->style, codepoint);
    return true;
}

struct RenderClusterVisit {
    RenderClusterGlyphFn visitor;
    void* context;
};

static void render_cluster_glyph(void* pointer, FontHandle* face, uint32_t glyph_index,
        float x, float y, float advance) {
    (void)advance;
    RenderClusterVisit* visit = (RenderClusterVisit*)pointer;
    if (!visit->visitor) return;
    const GlyphBitmap* bitmap = font_render_glyph_index(face, glyph_index, GLYPH_RENDER_NORMAL);
    visit->visitor(visit->context, bitmap, x, y);
}

bool render_text_visit_cluster(const FontBox* font, const unsigned char* start,
        const unsigned char* end, RenderClusterGlyphFn visitor, void* context,
        size_t* bytes, float* advance, bool include_scalar) {
    RenderClusterVisit visit = {visitor, context};
    float width = 0.0f;
    // zero advance does not mean no ink; retain selected-face substitutions/mark positions (D4.5.1v4).
    bool shaped = text_visit_glyph_unit(font, start, end, TEXT_SHAPE_HORIZONTAL_CLUSTER, include_scalar,
        render_cluster_glyph, &visit, bytes, &width);
    if (shaped) {
        uint32_t first;
        layout_utf8_next_codepoint(&start, end, &first);
        *advance = width + text_character_spacing(font->style, first);
    }
    return shaped;
}

static bool render_font_path_command(void* context, FontPathCommand command,
    const float* values, int count) {
    RenderGlyphPathWriter* writer = (RenderGlyphPathWriter*)context;
    float args[6];
    for (int i = 0; i < count; i += 2) {
        args[i] = writer->x + values[i] * writer->scale_x;
        args[i + 1] = writer->y + values[i + 1];
        if (writer->placement) {
            rdt_matrix_transform_point(writer->placement, args[i], args[i + 1], &args[i], &args[i + 1]);
        }
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
    RenderTextRunGlyphFn visitor, void* context, bool include_spaces) {
    const FontBox* box = paint_glyph_run_font(run);
    FontHandle* primary = font_box_handle(box);
    if (!run || !run->text || !box || !box->style || !visitor) return false;
    // match raster text: a zero-size run has no ink and needs no font outline.
    if (box->style->font_size <= 0.0f) return true;
    if (!primary) return false;
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
            if (include_spaces && !visitor(context, codepoint, primary, pen, run->baseline_y,
                    text_space_advance(box->style, run->word_spacing))) return false;
            pen += text_space_advance(box->style, run->word_spacing);
            continue;
        }
        float synthetic_advance = 0.0f;
        if (render_text_synthesized_space_advance(box, codepoint, &synthetic_advance)) {
            if (include_spaces && font_has_codepoint(primary, codepoint) &&
                    !visitor(context, codepoint, primary, pen, run->baseline_y, synthetic_advance)) return false;
            pen += synthetic_advance;
            continue;
        }
        uint32_t next = 0;
        bool emoji = utf_is_emoji_presentation_default(codepoint) ||
            (cursor < end && str_utf8_decode(cursor, (size_t)(end - cursor), &next) > 0 && next == 0xFE0F);
        FontHandle* fallback = emoji ? font_resolve_for_emoji(font_context, &descriptor, codepoint)
            : !font_has_codepoint(primary, codepoint)
                ? font_resolve_fallback_for_codepoint(primary, &descriptor, codepoint) : nullptr;
        FontHandle* face = fallback ? fallback : primary;
        // exported text uses the selected Unicode face instead of a viewer's replacement font.
        bool missing = !font_has_codepoint(face, codepoint);
        float advance = include_spaces && missing ? box->style->space_width : font_get_glyph(face, codepoint).advance_x;
        // PDF text can retain an unmapped character with the face's .notdef glyph.
        ok = (include_spaces || font_has_codepoint(face, codepoint)) &&
            visitor(context, codepoint, face, pen, run->baseline_y, advance);
        if (!ok) log_debug("TEXT_OUTLINE: cannot extract U+%04X from %s (coverage=%u)",
            codepoint, font_handle_get_family_name(face), font_has_codepoint(face, codepoint));
        pen += advance + (include_spaces && missing ? 0.0f : text_character_spacing(box->style, codepoint));
        if (fallback) font_handle_release(fallback);
    }
    return ok;
}

struct RenderTextOutlineContext {
    RdtPath* path;
    Arena* scratch;
};

struct RenderOrientedUnit {
    RenderOrientedGlyphFn visitor;
    void* context;
    RdtMatrix frame;
    bool ok;
};

static void render_oriented_unit_glyph(void* pointer, FontHandle* face, uint32_t glyph,
        float x, float y, float advance) {
    RenderOrientedUnit* unit = (RenderOrientedUnit*)pointer;
    RdtMatrix placement = unit->frame;
    rdt_matrix_transform_point(&unit->frame, x, y, &placement.e13, &placement.e23);
    if (unit->ok) unit->ok = unit->visitor(unit->context, face, glyph, &placement);
}

struct RenderOrientedTextQuery {
    const PaintGlyphRun* run;
    RenderOrientedGlyphFn visitor;
    void* context;
    float pen;
    TextJustifyContext justification;
    bool has_range, selected;
    int range_start, range_end;
    Rect bounds;
};

static bool render_oriented_segment(void* pointer, const TextGlyphSegment* segment) {
    RenderOrientedTextQuery* query = (RenderOrientedTextQuery*)pointer;
    const PaintGlyphRun* run = query->run;
    const FontBox* font = paint_glyph_run_font(run);
    int source = (int)(segment->text - (const unsigned char*)run->text.get()); // INT_CAST_OK: bounded source byte offset.
    int length = (int)segment->bytes; // INT_CAST_OK: bounded text segment byte length.
    bool selected = query->has_range && (query->range_start == query->range_end
        ? source <= query->range_start && (query->range_start < source + length ||
            (query->range_start == run->text_len && source + length == query->range_start))
        : query->range_start < source + length && query->range_end > source);
    TextGlyphRange range = {max(query->range_start - source, 0), min(query->range_end - source, length)};
    if (!segment->ignored && text_justify_character_gap(&query->justification, segment->first, run->justify_mode))
        query->pen += run->cjk_spacing;
    float ascent = font_get_rendering_ascender(font_box_handle(font));
    RdtMatrix frame = rdt_matrix_identity();
    if (segment->orientation == TEXT_GLYPH_HORIZONTAL) {
        // horizontal units use the same shaped source ranges in an identity frame.
        frame.e13 = run->x + query->pen;
        frame.e23 = run->baseline_y;
    } else if (segment->orientation == TEXT_GLYPH_UPRIGHT) {
        frame.e13 = run->x + run->cross_extent / 2.0f;
        frame.e23 = run->baseline_y + query->pen;
    } else if (segment->orientation == TEXT_GLYPH_SIDEWAYS_CCW) {
        frame = {0.0f, 1.0f, run->x + ascent,
            -1.0f, 0.0f, run->baseline_y + run->inline_extent - query->pen, 0.0f, 0.0f, 1.0f};
    } else {
        frame = {0.0f, -1.0f, run->x + run->cross_extent - ascent,
            1.0f, 0.0f, run->baseline_y + query->pen, 0.0f, 0.0f, 1.0f};
    }
    RenderOrientedUnit unit = {query->visitor, query->context, frame, true};
    float advance = 0.0f;
    float spacing = 0.0f;
    if (!segment->ignored) {
        size_t bytes;
        if (!text_visit_glyph_unit(font, segment->text, segment->text + segment->bytes, segment->shaping, true,
                query->visitor ? render_oriented_unit_glyph : nullptr, &unit, &bytes, &advance,
                run->direction, selected ? &range : nullptr) || !unit.ok || bytes != segment->bytes) return false;
        const unsigned char* following = segment->text;
        uint32_t first;
        layout_utf8_next_codepoint(&following, segment->text + segment->bytes, &first);
        text_justify_cluster_gap(&query->justification, following,
            segment->text + segment->bytes, run->justify_mode);
        spacing = text_character_spacing(font->style, segment->first) +
            (segment->first == ' ' ? run->word_spacing : 0.0f);
    }
    if (selected) {
        float first = segment->ignored ? 0.0f : fminf(range.inline_start, range.inline_end);
        float last = segment->ignored ? 0.0f : fmaxf(range.inline_start, range.inline_end);
        if (range.source_start == 0 && range.source_end == length && range.source_start < range.source_end) last += spacing;
        float top = segment->orientation == TEXT_GLYPH_SIDEWAYS_CCW ? frame.e23 - last : frame.e23 + first;
        bool vertical = text_orientation_is_vertical(run->orientation);
        Rect bounds = vertical ? Rect{run->x, top, run->cross_extent, last - first}
            : Rect{frame.e13 + first, run->baseline_y - ascent,
                last - first, font_get_cell_height(font_box_handle(font))};
        if (!query->selected) query->bounds = bounds;
        else {
            // units share their cross-axis cell; extend only the actual inline range.
            float* start = vertical ? &query->bounds.y : &query->bounds.x;
            float* extent = vertical ? &query->bounds.height : &query->bounds.width;
            float next_start = vertical ? bounds.y : bounds.x;
            float next_extent = vertical ? bounds.height : bounds.width;
            float end = fmaxf(*start + *extent, next_start + next_extent);
            *start = fminf(*start, next_start);
            *extent = end - *start;
        }
        query->selected = true;
    }
    query->pen += advance + spacing;
    return true;
}

static bool render_query_oriented_text(RenderOrientedTextQuery* query) {
    const PaintGlyphRun* run = query->run;
    const FontBox* font = paint_glyph_run_font(run);
    if (!run || !run->text || !font || !font->style) return false;
    if (font->style->font_size <= 0.0f) return true;
    size_t length = run->text_len >= 0 ? (size_t)run->text_len : strlen(run->text);
    const unsigned char* text = (const unsigned char*)run->text.get();
    if (!length) return true;
    if (run->text_combine) {
        TextCombineMetrics metrics;
        if (!text_visit_combined_glyphs(font, text, text + length, run->direction,
                nullptr, nullptr, &metrics)) return false;
        float em = font->style->font_size;
        RdtMatrix frame = rdt_matrix_identity();
        frame.e11 = metrics.scale;
        frame.e13 = run->x + (run->cross_extent - metrics.advance * metrics.scale) / 2.0f;
        frame.e23 = run->baseline_y + (em - font_get_cell_height(font_box_handle(font))) / 2.0f +
            font_get_rendering_ascender(font_box_handle(font));
        RenderOrientedUnit unit = {query->visitor, query->context, frame, true};
        TextGlyphRange range = {query->range_start, query->range_end};
        if (!text_visit_combined_glyphs(font, text, text + length, run->direction,
                query->visitor ? render_oriented_unit_glyph : nullptr, &unit, &metrics,
                query->has_range ? &range : nullptr) || !unit.ok) return false;
        if (query->has_range) {
            float first = fminf(range.inline_start, range.inline_end);
            float last = fmaxf(range.inline_start, range.inline_end);
            query->bounds = {frame.e13 + first * metrics.scale, run->baseline_y,
                (last - first) * metrics.scale, em};
            query->selected = true;
        }
        return true;
    }
    return text_visit_glyph_segments(font, run->orientation, text, text + length, run->direction,
        render_oriented_segment, query);
}

bool render_visit_oriented_text_run_glyphs(const PaintGlyphRun* run,
        RenderOrientedGlyphFn visitor, void* context) {
    if (!visitor) return false;
    RenderOrientedTextQuery query = {}; query.run = run; query.visitor = visitor; query.context = context;
    return render_query_oriented_text(&query);
}

bool render_text_run_range_bounds(const PaintGlyphRun* run, int start, int end, Rect* bounds) {
    if (!run || !bounds || start < 0 || end < start) return false;
    size_t length = run->text_len >= 0 ? (size_t)run->text_len : run->text ? strlen(run->text) : 0;
    if ((size_t)end > length) return false;
    if (!length) { *bounds = {run->x, run->baseline_y, run->cross_extent, 0.0f}; return true; }
    RenderOrientedTextQuery query = {}; query.run = run; query.has_range = true;
    query.range_start = start; query.range_end = end;
    if (!render_query_oriented_text(&query) || !query.selected) return false;
    *bounds = query.bounds;
    return true;
}

static bool render_text_outline_glyph(void* pointer, uint32_t codepoint,
    FontHandle* face, float x, float baseline_y, float advance) {
    RenderTextOutlineContext* context = (RenderTextOutlineContext*)pointer;
    bool color_bitmap = false;
    bool ok = render_path_append_font_glyph(context->path, face, codepoint,
        x, baseline_y, 1.0f, context->scratch, &color_bitmap);
    arena_reset(context->scratch);
    return ok && !color_bitmap;
}

RdtPath* render_path_create_text_run(const PaintGlyphRun* run, FontContext* font_context) {
    RdtPath* path = rdt_path_new();
    Arena* scratch = mem_arena_create(mem_context_process(MEM_ROLE_RENDER), MEM_ROLE_RENDER, "render.text_run.outline");
    RenderTextOutlineContext context = {path, scratch};
    auto oriented = [](void* pointer, FontHandle* face, uint32_t glyph, const RdtMatrix* matrix) {
        RenderTextOutlineContext* context = (RenderTextOutlineContext*)pointer;
        RenderGlyphPathWriter writer = {context->path, 0.0f, 0.0f, 1.0f, matrix};
        bool ok = font_visit_glyph_index_path(face, glyph, render_font_path_command, &writer, context->scratch);
        arena_reset(context->scratch);
        return ok;
    };
    bool vertical = run && text_orientation_is_vertical(run->orientation);
    bool ok = path && scratch && (vertical
        ? render_visit_oriented_text_run_glyphs(run, oriented, &context)
        : render_visit_text_run_glyphs(run, font_context, render_text_outline_glyph, &context));
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
