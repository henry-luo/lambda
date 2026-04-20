/**
 * Lambda Unified Font Module — glyf Table Parser (Header)
 *
 * Extracts TrueType glyph outlines (contour points and flags) from the
 * glyf and loca tables. Used by the ThorVG rasterizer on Linux/WASM
 * to render glyphs without FreeType.
 *
 * Supports simple glyphs (quadratic Bézier contours) and compound glyphs
 * (component references with affine transforms, recursively flattened).
 *
 * Copyright (c) 2026 Lambda Script Project
 */

#ifndef LAMBDA_FONT_GLYF_H
#define LAMBDA_FONT_GLYF_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// forward declarations
typedef struct FontTables FontTables;
typedef struct Arena Arena;

// ============================================================================
// Glyph outline data structures
// ============================================================================

typedef struct {
    float x, y;
    bool  on_curve;     // true = on-curve point, false = off-curve control point
} GlyfPoint;

typedef struct {
    GlyfPoint* points;
    int        num_points;
} GlyfContour;

typedef struct {
    GlyfContour* contours;
    int          num_contours;
    int16_t      x_min, y_min, x_max, y_max;   // bounding box in font design units
} GlyphOutline;

// ============================================================================
// Public API
// ============================================================================

// extract outline for a glyph ID from the glyf/loca tables.
// handles both simple and compound (composite) glyphs.
// allocates all memory from arena. returns 0 on success, -1 on error.
// on error or empty glyph (e.g. space), out->num_contours is set to 0.
int glyf_get_outline(FontTables* tables, uint16_t glyph_id,
                     GlyphOutline* out, Arena* arena);

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_FONT_GLYF_H
