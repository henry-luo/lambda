/**
 * Lambda Unified Font Module — CBDT/CBLC Color Bitmap Tables
 *
 * Parses CBLC (index) + CBDT (data) tables to extract embedded PNG
 * bitmaps for color emoji glyphs.
 *
 * Table structure (OpenType spec):
 *   CBLC header → BitmapSize records (one per strike/ppem)
 *     → IndexSubtable arrays → IndexSubtable (format 1–5)
 *       → glyph_id → CBDT offset
 *   CBDT header → GlyphBitmapData (format 17/18/19 = embedded PNG)
 *
 * Only PNG-based formats (17, 18, 19) are supported — these cover
 * virtually all color emoji fonts in practice (Noto Color Emoji, etc.).
 *
 * Copyright (c) 2026 Lambda Script Project
 */

#include "font_cbdt.h"
#include "font_tables.h"
#include "../log.h"

#include <string.h>
#include <stdlib.h>

// ============================================================================
// Byte reading helpers (big-endian)
// ============================================================================

static inline uint16_t rd16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline int16_t rd16s(const uint8_t* p) {
    return (int16_t)rd16(p);
}

static inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

// ============================================================================
// CBLC BitmapSize record (48 bytes per record)
// ============================================================================

typedef struct {
    uint32_t index_subtable_array_offset;
    uint32_t index_tables_size;
    uint32_t number_of_index_subtables;
    // SbitLineMetrics hori (12 bytes), vert (12 bytes) — skipped
    uint16_t start_glyph_index;
    uint16_t end_glyph_index;
    uint8_t  ppem_x;
    uint8_t  ppem_y;
    // bit_depth (1 byte), flags (1 byte) — skipped
} CblcBitmapSize;

#define BITMAP_SIZE_RECORD_LEN 48

static bool parse_bitmap_size(const uint8_t* data, CblcBitmapSize* out) {
    out->index_subtable_array_offset = rd32(data + 0);
    out->index_tables_size           = rd32(data + 4);
    out->number_of_index_subtables   = rd32(data + 8);
    // skip hori metrics (12 bytes at offset 12) and vert metrics (12 bytes at offset 24)
    out->start_glyph_index = rd16(data + 36);
    out->end_glyph_index   = rd16(data + 38);
    out->ppem_x            = data[40];
    out->ppem_y            = data[41];
    return true;
}

// ============================================================================
// IndexSubtableArray header (8 bytes per entry)
// ============================================================================

typedef struct {
    uint16_t first_glyph_index;
    uint16_t last_glyph_index;
    uint32_t additional_offset_to_index_subtable;
} IndexSubtableArrayEntry;

// ============================================================================
// Public API
// ============================================================================

bool cbdt_has_table(FontTables* tables) {
    if (!tables) return false;
    return font_tables_has(tables, FONT_TAG('C','B','L','C'))
        && font_tables_has(tables, FONT_TAG('C','B','D','T'));
}

// find the best strike for the target ppem
// returns pointer to the BitmapSize record in CBLC data, or NULL
static const uint8_t* find_best_strike(const uint8_t* cblc, uint32_t cblc_len,
                                        uint16_t target_ppem, CblcBitmapSize* out_size) {
    if (cblc_len < 8) return NULL;

    // CBLC header: majorVersion (2), minorVersion (2), numSizes (4)
    uint32_t num_sizes = rd32(cblc + 4);
    if (num_sizes == 0) return NULL;
    if (8 + (uint64_t)num_sizes * BITMAP_SIZE_RECORD_LEN > cblc_len) return NULL;

    // find strike with smallest ppem >= target, or the largest available
    const uint8_t* best = NULL;
    CblcBitmapSize best_size = {0};
    int best_diff = 0x7FFFFFFF;

    for (uint32_t i = 0; i < num_sizes; i++) {
        const uint8_t* rec = cblc + 8 + i * BITMAP_SIZE_RECORD_LEN;
        CblcBitmapSize bs;
        parse_bitmap_size(rec, &bs);

        int diff = (int)bs.ppem_y - (int)target_ppem;
        int abs_diff = diff < 0 ? -diff : diff;

        if (abs_diff < best_diff) {
            best_diff = abs_diff;
            best = rec;
            best_size = bs;
        }
    }

    if (best) {
        *out_size = best_size;
    }
    return best;
}

// look up glyph offset in CBDT via index subtables
static bool find_glyph_in_strike(const uint8_t* cblc, uint32_t cblc_len,
                                  const uint8_t* cbdt, uint32_t cbdt_len,
                                  const CblcBitmapSize* strike, uint16_t glyph_id,
                                  CbdtBitmap* out) {
    // glyph must be in strike range
    if (glyph_id < strike->start_glyph_index || glyph_id > strike->end_glyph_index)
        return false;

    uint32_t array_offset = strike->index_subtable_array_offset;
    uint32_t num_subtables = strike->number_of_index_subtables;

    if (array_offset + (uint64_t)num_subtables * 8 > cblc_len) return false;

    // scan IndexSubtableArray entries
    for (uint32_t i = 0; i < num_subtables; i++) {
        const uint8_t* entry_ptr = cblc + array_offset + i * 8;
        uint16_t first = rd16(entry_ptr + 0);
        uint16_t last  = rd16(entry_ptr + 2);
        uint32_t additional_offset = rd32(entry_ptr + 4);

        if (glyph_id < first || glyph_id > last) continue;

        // found the right subtable — parse its header
        uint32_t subtable_offset = array_offset + additional_offset;
        if (subtable_offset + 8 > cblc_len) return false;

        const uint8_t* subtable = cblc + subtable_offset;
        uint16_t index_format = rd16(subtable + 0);
        uint16_t image_format = rd16(subtable + 2);
        uint32_t image_data_offset = rd32(subtable + 4);

        // we only support PNG-embedded formats (17, 18, 19)
        if (image_format != 17 && image_format != 18 && image_format != 19)
            return false;

        uint32_t cbdt_glyph_offset = 0;
        uint32_t cbdt_glyph_len = 0;

        if (index_format == 1) {
            // format 1: array of uint32 offsets, one per glyph in [first..last+1]
            uint32_t idx = (uint32_t)(glyph_id - first);
            uint32_t offsets_start = subtable_offset + 8;
            if (offsets_start + (idx + 2) * 4 > cblc_len) return false;
            uint32_t off1 = rd32(cblc + offsets_start + idx * 4);
            uint32_t off2 = rd32(cblc + offsets_start + (idx + 1) * 4);
            cbdt_glyph_offset = image_data_offset + off1;
            cbdt_glyph_len = off2 - off1;
        } else if (index_format == 2) {
            // format 2: all glyphs have the same image size
            uint32_t image_size = rd32(cblc + subtable_offset + 8);
            // bigGlyphMetrics at offset 12 (8 bytes) — skipped for now
            uint32_t idx = (uint32_t)(glyph_id - first);
            cbdt_glyph_offset = image_data_offset + idx * image_size;
            cbdt_glyph_len = image_size;
        } else if (index_format == 3) {
            // format 3: array of uint16 offsets
            uint32_t idx = (uint32_t)(glyph_id - first);
            uint32_t offsets_start = subtable_offset + 8;
            if (offsets_start + (idx + 2) * 2 > cblc_len) return false;
            uint32_t off1 = (uint32_t)rd16(cblc + offsets_start + idx * 2);
            uint32_t off2 = (uint32_t)rd16(cblc + offsets_start + (idx + 1) * 2);
            cbdt_glyph_offset = image_data_offset + off1;
            cbdt_glyph_len = off2 - off1;
        } else if (index_format == 4) {
            // format 4: array of (glyphID, offset) pairs — sparse
            uint32_t num_glyphs = rd32(cblc + subtable_offset + 8);
            uint32_t pairs_start = subtable_offset + 12;
            if (pairs_start + (uint64_t)(num_glyphs + 1) * 4 > cblc_len) return false;
            // each pair is (uint16 glyphID, uint16 offset) — 4 bytes
            // actually format 4 is: numGlyphs (uint32) followed by
            // (numGlyphs+1) GlyphIdOffsetPair records {uint16 glyphID, uint16 sbitOffset}
            bool found = false;
            for (uint32_t j = 0; j < num_glyphs; j++) {
                const uint8_t* pair = cblc + pairs_start + j * 4;
                uint16_t gid = rd16(pair);
                uint16_t sbit_off = rd16(pair + 2);
                if (gid == glyph_id) {
                    const uint8_t* next_pair = cblc + pairs_start + (j + 1) * 4;
                    uint16_t next_off = rd16(next_pair + 2);
                    cbdt_glyph_offset = image_data_offset + sbit_off;
                    cbdt_glyph_len = next_off - sbit_off;
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        } else if (index_format == 5) {
            // format 5: constant image size + sparse glyph array
            uint32_t image_size = rd32(cblc + subtable_offset + 8);
            // bigGlyphMetrics at offset 12 (8 bytes)
            uint32_t num_glyphs = rd32(cblc + subtable_offset + 20);
            uint32_t gids_start = subtable_offset + 24;
            if (gids_start + (uint64_t)num_glyphs * 2 > cblc_len) return false;
            bool found = false;
            for (uint32_t j = 0; j < num_glyphs; j++) {
                uint16_t gid = rd16(cblc + gids_start + j * 2);
                if (gid == glyph_id) {
                    cbdt_glyph_offset = image_data_offset + j * image_size;
                    cbdt_glyph_len = image_size;
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        } else {
            return false; // unknown index format
        }

        if (cbdt_glyph_len == 0) return false;
        if (cbdt_glyph_offset + cbdt_glyph_len > cbdt_len) return false;

        const uint8_t* glyph_data = cbdt + cbdt_glyph_offset;

        // parse CBDT image data based on image format
        if (image_format == 17) {
            // format 17: smallGlyphMetrics (5 bytes) + uint32 dataLen + PNG data
            if (cbdt_glyph_len < 9) return false;
            // smallGlyphMetrics: height(1), width(1), bearingX(1s), bearingY(1s), advance(1)
            out->bearing_x = (int16_t)(int8_t)glyph_data[2];
            out->bearing_y = (int16_t)(int8_t)glyph_data[3];
            out->advance   = glyph_data[4];
            uint32_t data_len = rd32(glyph_data + 5);
            if (9 + data_len > cbdt_glyph_len) return false;
            out->png_data = glyph_data + 9;
            out->png_len  = data_len;
        } else if (image_format == 18) {
            // format 18: bigGlyphMetrics (8 bytes) + uint32 dataLen + PNG data
            if (cbdt_glyph_len < 12) return false;
            // bigGlyphMetrics: height(1), width(1), horiBearingX(1s), horiBearingY(1s),
            //                  horiAdvance(1), vertBearingX(1s), vertBearingY(1s), vertAdvance(1)
            out->bearing_x = (int16_t)(int8_t)glyph_data[2];
            out->bearing_y = (int16_t)(int8_t)glyph_data[3];
            out->advance   = glyph_data[4];
            uint32_t data_len = rd32(glyph_data + 8);
            if (12 + data_len > cbdt_glyph_len) return false;
            out->png_data = glyph_data + 12;
            out->png_len  = data_len;
        } else if (image_format == 19) {
            // format 19: no metrics (uses strike-level metrics) + uint32 dataLen + PNG data
            if (cbdt_glyph_len < 4) return false;
            uint32_t data_len = rd32(glyph_data);
            if (4 + data_len > cbdt_glyph_len) return false;
            out->png_data = glyph_data + 4;
            out->png_len  = data_len;
            out->bearing_x = 0;
            out->bearing_y = 0;
            out->advance   = 0;
        }

        out->ppem = strike->ppem_y;
        return true;
    }

    return false;
}

bool cbdt_get_bitmap(FontTables* tables, uint16_t glyph_id,
                     uint16_t target_ppem, CbdtBitmap* out) {
    if (!tables || !out) return false;
    memset(out, 0, sizeof(CbdtBitmap));

    uint32_t cblc_len = 0, cbdt_len = 0;
    const uint8_t* cblc = font_tables_find(tables, FONT_TAG('C','B','L','C'), &cblc_len);
    const uint8_t* cbdt = font_tables_find(tables, FONT_TAG('C','B','D','T'), &cbdt_len);
    if (!cblc || !cbdt) return false;

    CblcBitmapSize strike = {0};
    if (!find_best_strike(cblc, cblc_len, target_ppem, &strike)) return false;

    return find_glyph_in_strike(cblc, cblc_len, cbdt, cbdt_len, &strike, glyph_id, out);
}
