/**
 * Lambda Unified Font Module — GPOS Table Parser
 *
 * Parses OpenType GPOS PairAdjustment (type 2) lookups for kerning.
 * Supports PairPos Format 1 (individual pair sets), Format 2 (class-based),
 * and Extension lookups (type 9) wrapping PairPos.
 *
 * Only extracts XAdvance from value records (the kern adjustment).
 * Other GPOS lookup types (SingleAdj, MarkBase, etc.) are skipped.
 *
 * Reference: OpenType spec §7.2 — GPOS Table
 *
 * Copyright (c) 2026 Lambda Script Project
 */

#include "font_gpos.h"
#include "font_tables.h"
#include "../mempool.h"
#include "../log.h"

#include <string.h>

// ============================================================================
// Big-endian readers
// ============================================================================

static inline uint16_t rd16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline int16_t rd16s(const uint8_t* p) {
    return (int16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

// ============================================================================
// ValueFormat — bitmask describing which fields are present in a ValueRecord
// ============================================================================
// bit 0: XPlacement (int16)
// bit 1: YPlacement (int16)
// bit 2: XAdvance   (int16) — this is the kern value we want
// bit 3: YAdvance   (int16)
// bits 4-7: device table offsets (uint16 each), ignored

static int value_record_size(uint16_t value_format) {
    int size = 0;
    for (int i = 0; i < 8; i++) {
        if (value_format & (1 << i)) size += 2;
    }
    return size;
}

// extract XAdvance from a value record at `data`. Returns 0 if not present.
static int16_t value_record_get_x_advance(const uint8_t* data, uint16_t value_format) {
    if (!(value_format & 0x0004)) return 0; // no XAdvance
    int offset = 0;
    if (value_format & 0x0001) offset += 2; // skip XPlacement
    if (value_format & 0x0002) offset += 2; // skip YPlacement
    return rd16s(data + offset);
}

// ============================================================================
// Coverage table — maps glyph ID → coverage index
// ============================================================================

// returns coverage index (0-based), or -1 if glyph not covered
static int coverage_lookup(const uint8_t* cov_data, uint32_t cov_len, uint16_t glyph_id) {
    if (cov_len < 4) return -1;
    uint16_t format = rd16(cov_data);

    if (format == 1) {
        // Format 1: array of glyph IDs (sorted)
        uint16_t count = rd16(cov_data + 2);
        if (cov_len < 4 + (uint32_t)count * 2) return -1;
        // binary search
        int lo = 0, hi = (int)count - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            uint16_t g = rd16(cov_data + 4 + mid * 2);
            if (g == glyph_id) return mid;
            if (g < glyph_id) lo = mid + 1;
            else hi = mid - 1;
        }
        return -1;
    } else if (format == 2) {
        // Format 2: array of ranges [startGlyphID, endGlyphID, startCoverageIndex]
        uint16_t count = rd16(cov_data + 2);
        if (cov_len < 4 + (uint32_t)count * 6) return -1;
        int lo = 0, hi = (int)count - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            const uint8_t* r = cov_data + 4 + mid * 6;
            uint16_t start = rd16(r);
            uint16_t end   = rd16(r + 2);
            if (glyph_id < start) hi = mid - 1;
            else if (glyph_id > end) lo = mid + 1;
            else {
                uint16_t start_idx = rd16(r + 4);
                return start_idx + (glyph_id - start);
            }
        }
        return -1;
    }
    return -1;
}

// ============================================================================
// ClassDef table — maps glyph ID → class value
// ============================================================================

static uint16_t classdef_lookup(const uint8_t* cd_data, uint32_t cd_len, uint16_t glyph_id) {
    if (cd_len < 4) return 0;
    uint16_t format = rd16(cd_data);

    if (format == 1) {
        // Format 1: array starting at startGlyphID
        uint16_t start = rd16(cd_data + 2);
        uint16_t count = rd16(cd_data + 4);
        if (cd_len < 6 + (uint32_t)count * 2) return 0;
        if (glyph_id < start || glyph_id >= start + count) return 0;
        return rd16(cd_data + 6 + (glyph_id - start) * 2);
    } else if (format == 2) {
        // Format 2: array of ranges [startGlyphID, endGlyphID, class]
        uint16_t count = rd16(cd_data + 2);
        if (cd_len < 4 + (uint32_t)count * 6) return 0;
        int lo = 0, hi = (int)count - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            const uint8_t* r = cd_data + 4 + mid * 6;
            uint16_t start = rd16(r);
            uint16_t end   = rd16(r + 2);
            if (glyph_id < start) hi = mid - 1;
            else if (glyph_id > end) lo = mid + 1;
            else return rd16(r + 4);
        }
        return 0;
    }
    return 0;
}

// ============================================================================
// Internal structures for parsed GPOS pair data
// ============================================================================

// a single PairPos subtable reference (we keep pointers into the raw GPOS data)
typedef struct {
    uint16_t format;                    // 1 or 2
    const uint8_t* data;                // start of PairPos subtable
    uint32_t data_len;                  // available bytes from subtable start
} GposPairSub;

#define GPOS_MAX_PAIR_SUBS 64

struct GposTable {
    GposPairSub subs[GPOS_MAX_PAIR_SUBS];
    int         num_subs;
    const uint8_t* gpos_data;           // raw GPOS table base
    uint32_t    gpos_len;               // raw GPOS table length
};

// ============================================================================
// Parse GPOS table
// ============================================================================

// collect PairPos subtables from a single lookup
static void collect_pairpos_from_lookup(const uint8_t* gpos_data, uint32_t gpos_len,
                                         uint32_t lookup_abs, GposTable* out) {
    if (lookup_abs + 6 > gpos_len) return;
    uint16_t lookup_type = rd16(gpos_data + lookup_abs);
    uint16_t lookup_flag = rd16(gpos_data + lookup_abs + 2);
    (void)lookup_flag;
    uint16_t subtable_count = rd16(gpos_data + lookup_abs + 4);

    for (int si = 0; si < subtable_count && out->num_subs < GPOS_MAX_PAIR_SUBS; si++) {
        if (lookup_abs + 6 + (uint32_t)(si + 1) * 2 > gpos_len) break;
        uint16_t st_off = rd16(gpos_data + lookup_abs + 6 + si * 2);
        uint32_t abs_st = lookup_abs + st_off;

        if (lookup_type == 9) {
            // Extension lookup — resolve to actual subtable
            if (abs_st + 8 > gpos_len) continue;
            uint16_t ext_format = rd16(gpos_data + abs_st);
            uint16_t ext_type   = rd16(gpos_data + abs_st + 2);
            uint32_t ext_off    = rd32(gpos_data + abs_st + 4);
            if (ext_format != 1 || ext_type != 2) continue;
            abs_st = abs_st + ext_off;
            if (abs_st + 2 > gpos_len) continue;
            lookup_type = 2; // treat as PairPos from here
        }

        if (lookup_type != 2) continue;
        if (abs_st + 2 > gpos_len) continue;

        uint16_t format = rd16(gpos_data + abs_st);
        if (format != 1 && format != 2) continue;

        GposPairSub* sub = &out->subs[out->num_subs++];
        sub->format   = format;
        sub->data     = gpos_data + abs_st;
        sub->data_len = gpos_len - abs_st;
    }
}

GposTable* font_gpos_parse(FontTables* tables, void* pool) {
    if (!tables || !pool) return NULL;

    uint32_t gpos_len = 0;
    const uint8_t* gpos_data = font_tables_find(tables, FONT_TAG('G','P','O','S'), &gpos_len);
    if (!gpos_data || gpos_len < 10) return NULL;

    // GPOS header
    uint16_t ver_major = rd16(gpos_data);
    uint16_t ver_minor = rd16(gpos_data + 2);
    (void)ver_major; (void)ver_minor;

    uint16_t script_off = rd16(gpos_data + 4);
    uint16_t feat_off   = rd16(gpos_data + 6);
    uint16_t lookup_off = rd16(gpos_data + 8);
    (void)script_off; (void)feat_off;

    if ((uint32_t)lookup_off + 2 > gpos_len) return NULL;

    // read LookupList
    uint16_t lookup_count = rd16(gpos_data + lookup_off);
    if ((uint32_t)lookup_off + 2 + (uint32_t)lookup_count * 2 > gpos_len) return NULL;

    // allocate result
    Pool* p = (Pool*)pool;
    GposTable* gpos = (GposTable*)pool_calloc(p, sizeof(GposTable));
    if (!gpos) return NULL;
    gpos->gpos_data = gpos_data;
    gpos->gpos_len  = gpos_len;

    // scan all lookups for PairPos (type 2) or Extension (type 9) wrapping PairPos
    for (int li = 0; li < lookup_count && gpos->num_subs < GPOS_MAX_PAIR_SUBS; li++) {
        uint16_t lo = rd16(gpos_data + lookup_off + 2 + li * 2);
        uint32_t abs_lo = (uint32_t)lookup_off + lo;
        if (abs_lo + 6 > gpos_len) continue;

        uint16_t ltype = rd16(gpos_data + abs_lo);
        if (ltype == 2 || ltype == 9) {
            collect_pairpos_from_lookup(gpos_data, gpos_len, abs_lo, gpos);
        }
    }

    if (gpos->num_subs == 0) {
        // no PairPos data found
        return NULL;
    }

    log_debug("GPOS parsed: %d PairPos subtables", gpos->num_subs);
    return gpos;
}

// ============================================================================
// Kern lookup
// ============================================================================

// Format 1: individual pair sets — one pair set per first glyph
static int16_t pairpos_fmt1_lookup(const GposPairSub* sub, uint16_t left, uint16_t right) {
    const uint8_t* d = sub->data;
    uint32_t len = sub->data_len;
    if (len < 10) return 0;

    uint16_t cov_off   = rd16(d + 2);
    uint16_t vf1       = rd16(d + 4);
    uint16_t vf2       = rd16(d + 6);
    uint16_t pair_count = rd16(d + 8);

    int vr1_size = value_record_size(vf1);
    int vr2_size = value_record_size(vf2);

    // look up left glyph in coverage
    if ((uint32_t)cov_off >= len) return 0;
    int cov_idx = coverage_lookup(d + cov_off, len - cov_off, left);
    if (cov_idx < 0 || cov_idx >= pair_count) return 0;

    // read PairSet offset
    if (10 + (uint32_t)(cov_idx + 1) * 2 > len) return 0;
    uint16_t ps_off = rd16(d + 10 + cov_idx * 2);
    if ((uint32_t)ps_off + 2 > len) return 0;

    // PairSet: count + array of [secondGlyph(2) + valueRecord1(vr1_size) + valueRecord2(vr2_size)]
    uint16_t pvr_count = rd16(d + ps_off);
    int record_size = 2 + vr1_size + vr2_size;
    const uint8_t* records = d + ps_off + 2;

    if ((uint32_t)ps_off + 2 + (uint32_t)pvr_count * record_size > len) return 0;

    // binary search for right glyph
    int lo = 0, hi = (int)pvr_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const uint8_t* rec = records + mid * record_size;
        uint16_t second = rd16(rec);
        if (second == right) {
            return value_record_get_x_advance(rec + 2, vf1);
        }
        if (second < right) lo = mid + 1;
        else hi = mid - 1;
    }
    return 0;
}

// Format 2: class-based pair adjustment
static int16_t pairpos_fmt2_lookup(const GposPairSub* sub, uint16_t left, uint16_t right) {
    const uint8_t* d = sub->data;
    uint32_t len = sub->data_len;
    if (len < 16) return 0;

    uint16_t cov_off    = rd16(d + 2);
    uint16_t vf1        = rd16(d + 4);
    uint16_t vf2        = rd16(d + 6);
    uint16_t cd1_off    = rd16(d + 8);
    uint16_t cd2_off    = rd16(d + 10);
    uint16_t class1_cnt = rd16(d + 12);
    uint16_t class2_cnt = rd16(d + 14);

    int vr1_size = value_record_size(vf1);
    int vr2_size = value_record_size(vf2);
    int record_size = vr1_size + vr2_size;

    // check left glyph is in coverage
    if ((uint32_t)cov_off >= len) return 0;
    int cov_idx = coverage_lookup(d + cov_off, len - cov_off, left);
    if (cov_idx < 0) return 0;

    // get class values
    if ((uint32_t)cd1_off >= len || (uint32_t)cd2_off >= len) return 0;
    uint16_t c1 = classdef_lookup(d + cd1_off, len - cd1_off, left);
    uint16_t c2 = classdef_lookup(d + cd2_off, len - cd2_off, right);

    if (c1 >= class1_cnt || c2 >= class2_cnt) return 0;

    // Class1Record[c1].Class2Record[c2]
    uint32_t row_size = (uint32_t)class2_cnt * record_size;
    uint32_t rec_off = 16 + (uint32_t)c1 * row_size + (uint32_t)c2 * record_size;
    if (rec_off + (uint32_t)vr1_size > len) return 0;

    return value_record_get_x_advance(d + rec_off, vf1);
}

int16_t gpos_get_kern(GposTable* gpos, uint16_t left_glyph, uint16_t right_glyph) {
    if (!gpos) return 0;

    for (int i = 0; i < gpos->num_subs; i++) {
        const GposPairSub* sub = &gpos->subs[i];
        int16_t val = 0;
        if (sub->format == 1) {
            val = pairpos_fmt1_lookup(sub, left_glyph, right_glyph);
        } else if (sub->format == 2) {
            val = pairpos_fmt2_lookup(sub, left_glyph, right_glyph);
        }
        if (val != 0) return val;
    }
    return 0;
}

bool gpos_has_kerning(GposTable* gpos) {
    return gpos && gpos->num_subs > 0;
}
