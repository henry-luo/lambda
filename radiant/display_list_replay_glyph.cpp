#include "display_list_replay_glyph.hpp"
#include "glyph_sampling.hpp"

void dl_replay_draw_glyph(ImageSurface* surface, const DlDrawGlyph* glyph) {
    GlyphBitmap* bitmap = (GlyphBitmap*)&glyph->bitmap;
    int x = glyph->x, y = glyph->y;
    Color color = glyph->color;
    if (color.a == 0) return;
    const Bound* clip = &glyph->clip;

    if (glyph->has_transform && !glyph->is_color_emoji) {
        glyph_draw_transformed_coverage_bitmap(surface, bitmap, x, y, clip, color,
            &glyph->transform, 0.0f, (float)surface->tile_offset_y);
        return;
    }

    if (glyph->is_color_emoji) {
        glyph_draw_color_bgra_bitmap(surface, bitmap, x, y, clip);
        return;
    }

    glyph_draw_coverage_bitmap(surface, bitmap, x, y, clip, color);
}
