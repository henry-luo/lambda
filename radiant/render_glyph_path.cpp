#include "render.hpp"

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

RdtPath* render_path_create_glyph_run(const PaintGlyphRun* run) {
    if (!run || !run->font || !run->glyph_ids || !run->xs || !run->ys || run->count <= 0) return nullptr;
    FontHandle* font = font_box_handle(run->font);
    if (!font) return nullptr;
    RdtPath* path = rdt_path_new();
    Arena* scratch = mem_arena_create(mem_context_process(MEM_ROLE_RENDER), MEM_ROLE_RENDER, "render.glyph_run.outline");
    bool ok = path && scratch;
    for (int i = 0; ok && i < run->count; i++) {
        RenderGlyphPathWriter writer = {path, run->x + run->xs.get()[i], run->baseline_y + run->ys.get()[i], 1.0f};
        ok = font_visit_glyph_index_path(font, run->glyph_ids.get()[i], render_font_path_command, &writer, scratch);
        arena_reset(scratch);
    }
    if (scratch) mem_arena_destroy(scratch);
    if (!ok) { if (path) rdt_path_free(path); return nullptr; }
    return path;
}

