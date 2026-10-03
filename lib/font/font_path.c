#include "font_internal.h"
#include "font_glyf.h"

typedef struct FontPathForwarder {
    FontPathVisitFn visitor;
    void* context;
    bool aborted, visited;
} FontPathForwarder;
static bool font_path_forward(void* context, FontPathCommand command, const float* args, int count) {
    FontPathForwarder* forward = (FontPathForwarder*)context;
    forward->visited = true;
    bool result = forward->visitor(forward->context, command, args, count);
    if (!result) forward->aborted = true;
    return result;
}

// expose logical, baseline-relative outlines without retaining backend objects.
bool font_visit_glyph_path(FontHandle* handle, uint32_t codepoint,
    FontPathVisitFn visitor, void* context, Arena* arena) {
    if (!handle || !visitor || handle->resources_destroyed) return false;
    FontPathForwarder forward = {visitor, context, false, false};
#ifdef __APPLE__
    if (handle->ct_raster_ref) {
        bool result = font_rasterize_ct_visit_path(handle->ct_raster_ref, codepoint, font_path_forward, &forward);
        // aborting a visitor is not a missing backend outline.
        if ((result && forward.visited) || forward.aborted) return result;
    }
#endif
    if (!handle->tables || !arena) return false;
    // missing outline tables identify bitmap/CFF formats, not an empty space glyph.
    if (!font_tables_find(handle->tables, FONT_TAG('g','l','y','f'), NULL) ||
        !font_tables_find(handle->tables, FONT_TAG('l','o','c','a'), NULL)) return false;
    HeadTable* head = font_tables_get_head(handle->tables);
    if (!head || !head->units_per_em) return false;
    uint32_t glyph = font_get_glyph_index(handle, codepoint);
    if (!glyph || glyph > UINT16_MAX) return false;
    GlyphOutline outline = {0};
    if (glyf_get_outline(handle->tables, (uint16_t)glyph, &outline, arena) != 0) return false;
    // bitmap faces may carry empty glyf stubs; coverage determines their actual ink.
    if (outline.num_contours == 0) return false;
    float scale = handle->size_px / (float)head->units_per_em;
    bool result = glyf_visit_outline(&outline, scale, -scale, 0.0f, 0.0f, font_path_forward, &forward);
    // empty outline stubs must fall through to the glyph bitmap.
    return result && forward.visited;
}

static bool font_bitmap_covered(const GlyphBitmap* bitmap, int x, int y) {
    if (x < 0 || y < 0 || x >= bitmap->width || y >= bitmap->height) return false;
    const uint8_t* row = bitmap->buffer + (ptrdiff_t)y * bitmap->pitch;
    switch (bitmap->pixel_mode) {
    case GLYPH_PIXEL_GRAY: return row[x] != 0;
    case GLYPH_PIXEL_MONO: return (row[x / 8] & (0x80u >> (x % 8))) != 0;
    case GLYPH_PIXEL_BGRA: return row[4 * x + 3] != 0;
    case GLYPH_PIXEL_LCD: return row[3 * x] || row[3 * x + 1] || row[3 * x + 2];
    }
    return false;
}

// trace only exposed pixel edges, so shared edges never become stroke seams.
bool font_visit_bitmap_contours(const GlyphBitmap* bitmap, float pixel_to_css,
    FontPathVisitFn visitor, void* context, Arena* arena) {
    if (!bitmap || !visitor || !arena || pixel_to_css <= 0.0f) return false;
    if (bitmap->width <= 0 || bitmap->height <= 0) return true;
    if (!bitmap->buffer || !bitmap->pitch) return false;
    size_t columns = (size_t)bitmap->width + 1, rows = (size_t)bitmap->height + 1;
    if (rows > SIZE_MAX / columns) return false;
    uint8_t* edges = (uint8_t*)arena_calloc(arena, rows * columns);
    if (!edges) return false;
    for (int y = 0; y < bitmap->height; y++) {
        for (int x = 0; x < bitmap->width; x++) {
            if (!font_bitmap_covered(bitmap, x, y)) continue;
            if (!font_bitmap_covered(bitmap, x, y - 1)) edges[(size_t)y * columns + x] |= 1u;
            if (!font_bitmap_covered(bitmap, x + 1, y)) edges[(size_t)y * columns + x + 1] |= 2u;
            if (!font_bitmap_covered(bitmap, x, y + 1)) edges[(size_t)(y + 1) * columns + x + 1] |= 4u;
            if (!font_bitmap_covered(bitmap, x - 1, y)) edges[(size_t)(y + 1) * columns + x] |= 8u;
        }
    }
    const int step_x[] = {1, 0, -1, 0}, step_y[] = {0, 1, 0, -1};
    for (int y = 0; y <= bitmap->height; y++) {
        for (int x = 0; x <= bitmap->width; x++) {
            for (int first = 0; first < 4; first++) {
                if (!(edges[(size_t)y * columns + x] & (1u << first))) continue;
                float args[] = {((float)x + bitmap->bearing_x) * pixel_to_css,
                    ((float)y - bitmap->bearing_y) * pixel_to_css};
                if (!visitor(context, FONT_PATH_MOVE, args, 2)) return false;
                int px = x, py = y, direction = first;
                do {
                    edges[(size_t)py * columns + px] &= (uint8_t)~(1u << direction);
                    px += step_x[direction]; py += step_y[direction];
                    if (px == x && py == y) break;
                    args[0] = ((float)px + bitmap->bearing_x) * pixel_to_css;
                    args[1] = ((float)py - bitmap->bearing_y) * pixel_to_css;
                    if (!visitor(context, FONT_PATH_LINE, args, 2)) return false;
                    uint8_t choices = edges[(size_t)py * columns + px];
                    // a right turn keeps diagonally touching components separate.
                    int turns[] = {(direction + 1) % 4, direction, (direction + 3) % 4, (direction + 2) % 4};
                    direction = -1;
                    for (int i = 0; i < 4; i++) {
                        if (choices & (1u << turns[i])) { direction = turns[i]; break; }
                    }
                    if (direction < 0) return false;
                } while (true);
                if (!visitor(context, FONT_PATH_CLOSE, NULL, 0)) return false;
            }
        }
    }
    return true;
}
