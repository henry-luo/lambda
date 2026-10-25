#include "render.hpp"
#include "glyph_sampling.hpp"

void dl_replay_draw_glyph(ImageSurface* surface, const DlDrawGlyph* glyph) {
    GlyphBitmap* bitmap = (GlyphBitmap*)&glyph->bitmap;
    int x = glyph->x, y = glyph->y;
    Color color = glyph->color;
    if (color.a == 0) return;
    const Bound* clip = &glyph->clip;
    // clip-path shapes, in the same device coordinates as the glyph
    ClipShape shapes[RDT_MAX_CLIP_SHAPES];
    ClipShape* shape_ptrs[RDT_MAX_CLIP_SHAPES];
    int clip_depth = dl_restore_clip_shapes(glyph->clip_shapes, shapes, shape_ptrs);

    if (glyph->has_transform && !glyph->is_color_emoji) {
        glyph_draw_transformed_coverage_bitmap(surface, bitmap, x, y, clip, color,
            &glyph->transform, 0.0f, (float)surface->tile_offset_y, shape_ptrs, clip_depth);
        return;
    }

    if (glyph->is_color_emoji) {
        glyph_draw_color_bgra_bitmap(surface, bitmap, x, y, clip, shape_ptrs, clip_depth);
        return;
    }

    glyph_draw_coverage_bitmap(surface, bitmap, x, y, clip, color, shape_ptrs, clip_depth);
}

void dl_replay_draw_glyph_at_offset(ImageSurface* surface, const DlDrawGlyph* glyph,
                                    float offset_x, float offset_y, ScratchArena* scratch) {
    GlyphBitmap* bitmap = (GlyphBitmap*)&glyph->bitmap;
    Color color = glyph->color;
    if (color.a == 0) return;
    ClipShape shapes[RDT_MAX_CLIP_SHAPES];
    ClipShape* shape_ptrs[RDT_MAX_CLIP_SHAPES];

    if (glyph->has_transform && !glyph->is_color_emoji) {
        // transformed glyphs sample in page coordinates, so their shapes stay unshifted
        int clip_depth = dl_restore_clip_shapes(glyph->clip_shapes, shapes, shape_ptrs);
        glyph_draw_transformed_coverage_bitmap(surface, bitmap, glyph->x, glyph->y,
            &glyph->clip, color, &glyph->transform, offset_x, offset_y, shape_ptrs, clip_depth);
        return;
    }

    int x = glyph->x - (int)offset_x; // INT_CAST_OK: glyph bitmap origins are integer pixels.
    int y = glyph->y - (int)offset_y; // INT_CAST_OK: glyph bitmap origins are integer pixels.

    Bound clip;
    clip.left = glyph->clip.left - offset_x;
    clip.top = glyph->clip.top - offset_y;
    clip.right = glyph->clip.right - offset_x;
    clip.bottom = glyph->clip.bottom - offset_y;
    if (clip.left < 0.0f) clip.left = 0.0f;
    if (clip.top < 0.0f) clip.top = 0.0f;
    if (clip.right > surface->width) clip.right = (float)surface->width;
    if (clip.bottom > surface->height) clip.bottom = (float)surface->height;

    // tile-local shapes; polygon vertices are shifted into scratch
    ScratchMark clip_mark = {};
    if (scratch) clip_mark = scratch_mark(scratch);
    int clip_depth = dl_restore_clip_shapes(glyph->clip_shapes, shapes, shape_ptrs,
                                            scratch, offset_x, offset_y);
    if (glyph->is_color_emoji) {
        glyph_draw_color_bgra_bitmap(surface, bitmap, x, y, &clip, shape_ptrs, clip_depth);
    } else {
        glyph_draw_coverage_bitmap(surface, bitmap, x, y, &clip, color, shape_ptrs, clip_depth);
    }
    if (scratch) scratch_restore(scratch, clip_mark);
}
