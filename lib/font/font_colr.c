/**
 * Lambda Unified Font Module — COLR v0 + CPAL Color Glyph Tables
 *
 * COLR v0 structure (OpenType spec):
 *   Header: version(2), numBaseGlyphRecords(2), offsetBaseGlyphRecord(4),
 *           offsetLayerRecord(4), numLayerRecords(2)
 *   BaseGlyphRecord[]: glyphID(2), firstLayerIndex(2), numLayers(2)
 *   LayerRecord[]:     glyphID(2), paletteIndex(2)
 *
 * CPAL structure:
 *   Header: version(2), numPaletteEntries(2), numPalettes(2),
 *           numColorRecords(2), offsetFirstColorRecord(4)
 *   ColorRecordIndices[numPalettes]: uint16 index into ColorRecords
 *   ColorRecords[]: blue(1), green(1), red(1), alpha(1)  — BGRA order
 *
 * Copyright (c) 2026 Lambda Script Project
 */

#include "font_colr.h"
#include "font_tables.h"
#include "../log.h"

#include <string.h>

// ============================================================================
// Byte reading helpers (big-endian)
// ============================================================================

static inline uint16_t rd16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

// ============================================================================
// COLR v0 header parsing
// ============================================================================

typedef struct {
    uint16_t        version;
    uint16_t        num_base_glyphs;
    const uint8_t*  base_glyph_records;  // pointer to first BaseGlyphRecord
    const uint8_t*  layer_records;       // pointer to first LayerRecord
    uint16_t        num_layer_records;
} ColrHeader;

static bool parse_colr_header(const uint8_t* colr, uint32_t colr_len, ColrHeader* out) {
    if (colr_len < 14) return false;

    out->version          = rd16(colr + 0);
    out->num_base_glyphs  = rd16(colr + 2);
    uint32_t base_offset  = rd32(colr + 4);
    uint32_t layer_offset = rd32(colr + 8);
    out->num_layer_records = rd16(colr + 12);

    // only v0 supported
    if (out->version != 0) return false;

    // validate offsets
    // BaseGlyphRecord: 6 bytes each (glyphID(2) + firstLayerIndex(2) + numLayers(2))
    if (base_offset + (uint64_t)out->num_base_glyphs * 6 > colr_len) return false;
    // LayerRecord: 4 bytes each (glyphID(2) + paletteIndex(2))
    if (layer_offset + (uint64_t)out->num_layer_records * 4 > colr_len) return false;

    out->base_glyph_records = colr + base_offset;
    out->layer_records = colr + layer_offset;

    return true;
}

// ============================================================================
// CPAL color lookup
// ============================================================================

static bool cpal_get_color(const uint8_t* cpal, uint32_t cpal_len,
                            uint16_t palette_index, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) {
    if (!cpal || cpal_len < 12) return false;

    // CPAL header
    uint16_t num_palette_entries = rd16(cpal + 2);
    uint16_t num_palettes        = rd16(cpal + 4);
    uint16_t num_color_records   = rd16(cpal + 6);
    uint32_t color_records_offset = rd32(cpal + 8);

    // use palette 0
    if (num_palettes == 0) return false;

    // colorRecordIndices array starts at offset 12
    if (12 + num_palettes * 2 > cpal_len) return false;
    uint16_t first_color_index = rd16(cpal + 12); // palette 0's first color record

    uint16_t color_index = first_color_index + palette_index;
    if (color_index >= num_color_records) return false;

    // each ColorRecord is 4 bytes: blue(1), green(1), red(1), alpha(1)
    uint32_t rec_offset = color_records_offset + (uint32_t)color_index * 4;
    if (rec_offset + 4 > cpal_len) return false;

    const uint8_t* rec = cpal + rec_offset;
    *b = rec[0];
    *g = rec[1];
    *r = rec[2];
    *a = rec[3];

    return true;
}

// ============================================================================
// Binary search for BaseGlyphRecord (sorted by glyphID)
// ============================================================================

static const uint8_t* find_base_glyph(const ColrHeader* hdr, uint16_t glyph_id) {
    int lo = 0, hi = (int)hdr->num_base_glyphs - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const uint8_t* rec = hdr->base_glyph_records + mid * 6;
        uint16_t gid = rd16(rec);
        if (gid == glyph_id) return rec;
        if (gid < glyph_id) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

// ============================================================================
// Public API
// ============================================================================

bool colr_has_table(FontTables* tables) {
    if (!tables) return false;
    return font_tables_has(tables, FONT_TAG('C','O','L','R'))
        && font_tables_has(tables, FONT_TAG('C','P','A','L'));
}

bool colr_has_glyph(FontTables* tables, uint16_t glyph_id) {
    if (!tables) return false;

    uint32_t colr_len = 0;
    const uint8_t* colr = font_tables_find(tables, FONT_TAG('C','O','L','R'), &colr_len);
    if (!colr) return false;

    ColrHeader hdr;
    if (!parse_colr_header(colr, colr_len, &hdr)) return false;

    return find_base_glyph(&hdr, glyph_id) != NULL;
}

int colr_get_layers(FontTables* tables, uint16_t glyph_id,
                    ColrLayer* layers, int max_layers) {
    if (!tables || !layers || max_layers <= 0) return 0;

    uint32_t colr_len = 0, cpal_len = 0;
    const uint8_t* colr = font_tables_find(tables, FONT_TAG('C','O','L','R'), &colr_len);
    const uint8_t* cpal = font_tables_find(tables, FONT_TAG('C','P','A','L'), &cpal_len);
    if (!colr || !cpal) return 0;

    ColrHeader hdr;
    if (!parse_colr_header(colr, colr_len, &hdr)) return 0;

    const uint8_t* base = find_base_glyph(&hdr, glyph_id);
    if (!base) return 0;

    uint16_t first_layer_index = rd16(base + 2);
    uint16_t num_layers        = rd16(base + 4);

    if (first_layer_index + num_layers > hdr.num_layer_records) return 0;
    if (num_layers > (uint16_t)max_layers) num_layers = (uint16_t)max_layers;

    for (uint16_t i = 0; i < num_layers; i++) {
        const uint8_t* lr = hdr.layer_records + ((uint32_t)(first_layer_index + i)) * 4;
        uint16_t layer_gid = rd16(lr);
        uint16_t palette_index = rd16(lr + 2);

        layers[i].glyph_id = layer_gid;

        // special value 0xFFFF means use the foreground color (default: black)
        if (palette_index == 0xFFFF) {
            layers[i].r = 0;
            layers[i].g = 0;
            layers[i].b = 0;
            layers[i].a = 255;
        } else {
            if (!cpal_get_color(cpal, cpal_len, palette_index,
                                &layers[i].r, &layers[i].g, &layers[i].b, &layers[i].a)) {
                // fallback: black
                layers[i].r = 0;
                layers[i].g = 0;
                layers[i].b = 0;
                layers[i].a = 255;
            }
        }
    }

    return (int)num_layers;
}
