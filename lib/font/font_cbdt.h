/**
 * Lambda Unified Font Module — CBDT/CBLC Color Bitmap Tables
 *
 * Extracts pre-rasterized color emoji bitmaps from the CBDT/CBLC
 * tables in TrueType/OpenType fonts (e.g., Noto Color Emoji).
 *
 * CBLC = Color Bitmap Location (index: glyph_id → offset in CBDT)
 * CBDT = Color Bitmap Data (actual PNG/raw bitmaps)
 *
 * Copyright (c) 2026 Lambda Script Project
 */

#ifndef FONT_CBDT_H
#define FONT_CBDT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FontTables FontTables;
typedef struct Arena Arena;

// result of a CBDT bitmap lookup
typedef struct CbdtBitmap {
    const uint8_t* png_data;    // pointer into raw CBDT table (not owned)
    uint32_t       png_len;     // length of PNG data
    int16_t        bearing_x;   // horizontal bearing (design units)
    int16_t        bearing_y;   // vertical bearing (design units)
    uint8_t        advance;     // horizontal advance (design units)
    uint16_t       ppem;        // strike ppem (pixels per em of this strike)
} CbdtBitmap;

// check if a font has CBDT/CBLC tables
bool cbdt_has_table(FontTables* tables);

// look up a glyph's color bitmap at the best available strike size.
// target_ppem: desired ppem (size_px × pixel_ratio).
// returns true if bitmap found, fills *out with PNG data pointer and metrics.
bool cbdt_get_bitmap(FontTables* tables, uint16_t glyph_id,
                     uint16_t target_ppem, CbdtBitmap* out);

#ifdef __cplusplus
}
#endif

#endif // FONT_CBDT_H
