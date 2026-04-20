/**
 * Lambda Unified Font Module — GPOS Table Parser (Header)
 *
 * Parses OpenType GPOS PairAdjustment (type 2) lookups for kerning.
 * Supports Format 1 (individual pair sets) and Format 2 (class-based).
 * Extension lookups (type 9) wrapping PairAdj are also handled.
 *
 * Copyright (c) 2026 Lambda Script Project
 */

#ifndef LAMBDA_FONT_GPOS_H
#define LAMBDA_FONT_GPOS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// forward declaration
typedef struct FontTables FontTables;

// opaque handle — parsed GPOS pair adjustment data
typedef struct GposTable GposTable;

// parse the GPOS table from FontTables. Returns NULL if no GPOS table
// or no PairAdjustment lookups found. Allocates from pool.
GposTable* font_gpos_parse(FontTables* tables, void* pool);

// look up kerning value between two glyph IDs in the GPOS table.
// returns the x-advance adjustment in font design units, or 0 if not found.
int16_t gpos_get_kern(GposTable* gpos, uint16_t left_glyph, uint16_t right_glyph);

// check if the GPOS table has any pair adjustment data
bool gpos_has_kerning(GposTable* gpos);

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_FONT_GPOS_H
