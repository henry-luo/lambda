#include "render.hpp"

void draw_glyph(RenderContext* rdcon, GlyphBitmap* bitmap, int x, int y) {
    if (!rdcon || !bitmap || rdcon->color.a == 0) return;
    if (!bitmap->buffer || bitmap->width <= 0 || bitmap->height <= 0 || bitmap->pitch <= 0) return;
    bool is_color = (bitmap->pixel_mode == GLYPH_PIXEL_BGRA);
    uint64_t glyph_generation = rdcon->ui_context ?
        font_context_glyph_cache_generation(rdcon->ui_context->font_ctx) : 0;
    rc_draw_glyph(rdcon, bitmap, x, y, rdcon->color, is_color, &rdcon->block.clip,
                  rdcon->has_transform ? &rdcon->transform : nullptr, glyph_generation);
}
