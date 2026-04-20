/**
 * Lambda Unified Font Module — COLR v0 + CPAL Color Glyph Tables
 *
 * Parses COLR v0 (layered color glyphs) and CPAL (color palette)
 * tables. Each color glyph is decomposed into an ordered list of
 * (glyph_id, palette_color) layers rendered bottom-to-top.
 *
 * COLR v1 (gradients, transforms, compositing) is out of scope.
 *
 * Copyright (c) 2026 Lambda Script Project
 */

#ifndef FONT_COLR_H
#define FONT_COLR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FontTables FontTables;

// a single COLR v0 layer: glyph outline + fill color
typedef struct ColrLayer {
    uint16_t glyph_id;       // glyph outline to render
    uint8_t  r, g, b, a;     // fill color from CPAL palette
} ColrLayer;

// result of a COLR v0 glyph lookup
typedef struct ColrGlyph {
    ColrLayer* layers;        // array of layers (bottom-to-top order)
    int        num_layers;
} ColrGlyph;

// check if a font has COLR v0 + CPAL tables
bool colr_has_table(FontTables* tables);

// check if a specific glyph has COLR v0 layers.
// glyph_id: from cmap lookup.
bool colr_has_glyph(FontTables* tables, uint16_t glyph_id);

// get the layers for a COLR v0 glyph.
// layers are written to a caller-provided buffer.
// returns number of layers (0 if not a COLR glyph or error).
// max_layers: size of the layers[] buffer.
int colr_get_layers(FontTables* tables, uint16_t glyph_id,
                    ColrLayer* layers, int max_layers);

#ifdef __cplusplus
}
#endif

#endif // FONT_COLR_H
