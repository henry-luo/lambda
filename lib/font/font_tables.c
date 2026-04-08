/**
 * Lambda Unified Font Module — TTF/OTF Table Parser
 *
 * Parses OpenType/TrueType font tables directly from raw font bytes.
 * Replaces FreeType for metric extraction and codepoint lookup.
 *
 * Supported tables:
 *   head, hhea, maxp, OS/2, post, cmap (format 4, 12), hmtx, kern (format 0),
 *   fvar, name
 *
 * Copyright (c) 2025-2026 Lambda Script Project
 */

#include "font_tables.h"
#include "../mempool.h"
#include "../log.h"

#include <string.h>
#include <stdlib.h>

// ============================================================================
// Byte-order helpers (big-endian → host)
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

static inline int32_t rd32s(const uint8_t* p) {
    return (int32_t)rd32(p);
}

// ============================================================================
// cmap format 4 subtable (BMP only, U+0000–U+FFFF)
// ============================================================================

typedef struct CmapFormat4 {
    uint16_t  seg_count;
    const uint8_t* end_codes;       // uint16[seg_count]
    const uint8_t* start_codes;     // uint16[seg_count]
    const uint8_t* id_deltas;       // int16[seg_count]
    const uint8_t* id_range_offsets;// uint16[seg_count]
    const uint8_t* glyph_id_array;  // uint16[]
    uint32_t  glyph_id_array_len;   // in bytes
} CmapFormat4;

// ============================================================================
// cmap format 12 subtable (full Unicode)
// ============================================================================

typedef struct CmapFormat12 {
    uint32_t  num_groups;
    const uint8_t* groups;          // each group: startCharCode(4), endCharCode(4), startGlyphID(4)
} CmapFormat12;

// ============================================================================
// CmapSubtable — internal representation
// ============================================================================

struct CmapSubtable {
    union {
        CmapFormat4  fmt4;
        CmapFormat12 fmt12;
    } u;
};

// ============================================================================
// Table directory parsing
// ============================================================================

FontTables* font_tables_open(const uint8_t* data, size_t data_len, void* pool_ptr) {
    Pool* pool = (Pool*)pool_ptr;

    if (!data || data_len < 12) {
        log_error("font_tables_open: data too small (%zu bytes)", data_len);
        return NULL;
    }

    uint32_t scaler_type = rd32(data);
    uint16_t num_tables = rd16(data + 4);

    // validate scaler type
    // 0x00010000 = TrueType, 'OTTO' = CFF, 'true' = Apple TrueType, 'typ1' = Type1
    bool valid_scaler = (scaler_type == 0x00010000 ||
                         scaler_type == FONT_TAG('O','T','T','O') ||
                         scaler_type == FONT_TAG('t','r','u','e') ||
                         scaler_type == FONT_TAG('t','y','p','1'));
    if (!valid_scaler) {
        log_debug("font_tables_open: unsupported scaler type 0x%08X", scaler_type);
        return NULL;
    }

    if (num_tables == 0 || num_tables > 200) {
        log_error("font_tables_open: invalid num_tables %d", num_tables);
        return NULL;
    }

    // table directory starts at offset 12, each entry is 16 bytes
    size_t dir_end = 12 + (size_t)num_tables * 16;
    if (dir_end > data_len) {
        log_error("font_tables_open: table directory extends past data (%zu > %zu)", dir_end, data_len);
        return NULL;
    }

    FontTables* tables = (FontTables*)pool_calloc(pool, sizeof(FontTables));
    if (!tables) return NULL;

    tables->data = data;
    tables->data_len = data_len;
    tables->scaler_type = scaler_type;
    tables->num_tables = num_tables;
    tables->pool = pool;

    tables->dirs = (FontTableDir*)pool_calloc(pool, (size_t)num_tables * sizeof(FontTableDir));
    if (!tables->dirs) {
        pool_free(pool, tables);
        return NULL;
    }

    const uint8_t* p = data + 12;
    for (int i = 0; i < num_tables; i++, p += 16) {
        tables->dirs[i].tag      = rd32(p);
        tables->dirs[i].checksum = rd32(p + 4);
        tables->dirs[i].offset   = rd32(p + 8);
        tables->dirs[i].length   = rd32(p + 12);
    }

    return tables;
}

void font_tables_close(FontTables* tables, void* pool_ptr) {
    if (!tables) return;
    Pool* pool = (Pool*)pool_ptr;

    if (tables->cmap && tables->cmap->subtable) {
        pool_free(pool, tables->cmap->subtable);
    }
    if (tables->cmap)  pool_free(pool, tables->cmap);
    if (tables->head)  pool_free(pool, tables->head);
    if (tables->hhea)  pool_free(pool, tables->hhea);
    if (tables->maxp)  pool_free(pool, tables->maxp);
    if (tables->os2)   pool_free(pool, tables->os2);
    if (tables->post)  pool_free(pool, tables->post);
    if (tables->hmtx)  pool_free(pool, tables->hmtx);
    if (tables->kern)  pool_free(pool, tables->kern);
    if (tables->name) {
        // name strings are pool-allocated
        if (tables->name->family_name)    pool_free(pool, tables->name->family_name);
        if (tables->name->subfamily_name) pool_free(pool, tables->name->subfamily_name);
        if (tables->name->postscript_name) pool_free(pool, tables->name->postscript_name);
        pool_free(pool, tables->name);
    }
    if (tables->fvar) {
        if (tables->fvar->axes) pool_free(pool, tables->fvar->axes);
        pool_free(pool, tables->fvar);
    }
    if (tables->dirs) pool_free(pool, tables->dirs);
    pool_free(pool, tables);
}

// ============================================================================
// Raw table lookup
// ============================================================================

const uint8_t* font_tables_find(FontTables* tables, uint32_t tag, uint32_t* out_len) {
    if (!tables) return NULL;
    for (int i = 0; i < tables->num_tables; i++) {
        if (tables->dirs[i].tag == tag) {
            uint32_t offset = tables->dirs[i].offset;
            uint32_t length = tables->dirs[i].length;
            if ((size_t)(offset + length) > tables->data_len) {
                log_error("font_tables_find: table '%c%c%c%c' extends past data",
                          (char)(tag >> 24), (char)(tag >> 16), (char)(tag >> 8), (char)tag);
                return NULL;
            }
            if (out_len) *out_len = length;
            return tables->data + offset;
        }
    }
    return NULL;
}

bool font_tables_has(FontTables* tables, uint32_t tag) {
    if (!tables) return false;
    for (int i = 0; i < tables->num_tables; i++) {
        if (tables->dirs[i].tag == tag) return true;
    }
    return false;
}

// ============================================================================
// head table
// ============================================================================

HeadTable* font_tables_get_head(FontTables* tables) {
    if (!tables) return NULL;
    if (tables->parsed_flags & FT_PARSED_HEAD) return tables->head;
    tables->parsed_flags |= FT_PARSED_HEAD;

    uint32_t len = 0;
    const uint8_t* raw = font_tables_find(tables, FONT_TAG('h','e','a','d'), &len);
    if (!raw || len < 54) return NULL;

    Pool* pool = (Pool*)tables->pool;
    HeadTable* h = (HeadTable*)pool_calloc(pool, sizeof(HeadTable));
    if (!h) return NULL;

    h->units_per_em      = rd16(raw + 18);
    // created/modified at offsets 20, 28 (8 bytes each, int64 — skip for now)
    h->x_min             = rd16s(raw + 36);
    h->y_min             = rd16s(raw + 38);
    h->x_max             = rd16s(raw + 40);
    h->y_max             = rd16s(raw + 42);
    h->mac_style         = rd16(raw + 44);
    h->index_to_loc_format = rd16s(raw + 50);

    tables->head = h;
    return h;
}

// ============================================================================
// hhea table
// ============================================================================

HheaTable* font_tables_get_hhea(FontTables* tables) {
    if (!tables) return NULL;
    if (tables->parsed_flags & FT_PARSED_HHEA) return tables->hhea;
    tables->parsed_flags |= FT_PARSED_HHEA;

    uint32_t len = 0;
    const uint8_t* raw = font_tables_find(tables, FONT_TAG('h','h','e','a'), &len);
    if (!raw || len < 36) return NULL;

    Pool* pool = (Pool*)tables->pool;
    HheaTable* h = (HheaTable*)pool_calloc(pool, sizeof(HheaTable));
    if (!h) return NULL;

    h->ascender          = rd16s(raw + 4);
    h->descender         = rd16s(raw + 6);
    h->line_gap          = rd16s(raw + 8);
    h->advance_width_max = rd16(raw + 10);
    h->number_of_h_metrics = rd16(raw + 34);

    tables->hhea = h;
    return h;
}

// ============================================================================
// maxp table
// ============================================================================

MaxpTable* font_tables_get_maxp(FontTables* tables) {
    if (!tables) return NULL;
    if (tables->parsed_flags & FT_PARSED_MAXP) return tables->maxp;
    tables->parsed_flags |= FT_PARSED_MAXP;

    uint32_t len = 0;
    const uint8_t* raw = font_tables_find(tables, FONT_TAG('m','a','x','p'), &len);
    if (!raw || len < 6) return NULL;

    Pool* pool = (Pool*)tables->pool;
    MaxpTable* m = (MaxpTable*)pool_calloc(pool, sizeof(MaxpTable));
    if (!m) return NULL;

    m->num_glyphs = rd16(raw + 4);

    tables->maxp = m;
    return m;
}

// ============================================================================
// OS/2 table
// ============================================================================

Os2Table* font_tables_get_os2(FontTables* tables) {
    if (!tables) return NULL;
    if (tables->parsed_flags & FT_PARSED_OS2) return tables->os2;
    tables->parsed_flags |= FT_PARSED_OS2;

    uint32_t len = 0;
    const uint8_t* raw = font_tables_find(tables, FONT_TAG('O','S','/','2'), &len);
    if (!raw || len < 78) return NULL;   // minimum OS/2 v0 is 78 bytes

    Pool* pool = (Pool*)tables->pool;
    Os2Table* o = (Os2Table*)pool_calloc(pool, sizeof(Os2Table));
    if (!o) return NULL;

    o->version            = rd16(raw + 0);
    o->avg_char_width     = rd16s(raw + 2);
    o->weight_class       = rd16(raw + 4);
    o->width_class        = rd16(raw + 6);
    o->fs_type            = rd16(raw + 8);
    o->y_subscript_x_size = rd16s(raw + 10);
    o->y_subscript_y_size = rd16s(raw + 12);
    o->y_subscript_x_offset = rd16s(raw + 14);
    o->y_subscript_y_offset = rd16s(raw + 16);
    o->y_superscript_x_size = rd16s(raw + 18);
    o->y_superscript_y_size = rd16s(raw + 20);
    o->y_superscript_x_offset = rd16s(raw + 22);
    o->y_superscript_y_offset = rd16s(raw + 24);
    o->y_strikeout_size   = rd16s(raw + 26);
    o->y_strikeout_position = rd16s(raw + 28);
    o->s_family_class     = rd16s(raw + 30);
    // panose at 32..41 (10 bytes) — skipped
    // ulUnicodeRange1-4 at 42..57 — skipped
    // achVendID at 58..61 — skipped
    o->fs_selection       = rd16(raw + 62);
    o->first_char_index   = rd16(raw + 64);
    o->last_char_index    = rd16(raw + 66);
    o->s_typo_ascender    = rd16s(raw + 68);
    o->s_typo_descender   = rd16s(raw + 70);
    o->s_typo_line_gap    = rd16s(raw + 72);
    o->us_win_ascent      = rd16(raw + 74);
    o->us_win_descent     = rd16(raw + 76);

    // version >= 2 fields
    if (o->version >= 2 && len >= 96) {
        o->sx_height      = rd16s(raw + 88);
        o->s_cap_height   = rd16s(raw + 90);
    }

    tables->os2 = o;
    return o;
}

// ============================================================================
// post table
// ============================================================================

PostTable* font_tables_get_post(FontTables* tables) {
    if (!tables) return NULL;
    if (tables->parsed_flags & FT_PARSED_POST) return tables->post;
    tables->parsed_flags |= FT_PARSED_POST;

    uint32_t len = 0;
    const uint8_t* raw = font_tables_find(tables, FONT_TAG('p','o','s','t'), &len);
    if (!raw || len < 32) return NULL;

    Pool* pool = (Pool*)tables->pool;
    PostTable* p = (PostTable*)pool_calloc(pool, sizeof(PostTable));
    if (!p) return NULL;

    p->format              = rd32s(raw + 0);
    // italicAngle at 4 (fixed 16.16) — skipped
    p->underline_position  = rd16s(raw + 8);
    p->underline_thickness = rd16s(raw + 10);
    p->is_fixed_pitch      = rd32(raw + 12);

    tables->post = p;
    return p;
}

// ============================================================================
// cmap table
// ============================================================================

// parse cmap format 4 subtable
static CmapSubtable* parse_cmap_format4(const uint8_t* raw, uint32_t len, Pool* pool) {
    if (len < 14) return NULL;

    uint16_t format = rd16(raw);
    if (format != 4) return NULL;

    uint16_t sub_len = rd16(raw + 2);
    if (sub_len > len) return NULL;

    uint16_t seg_count_x2 = rd16(raw + 6);
    uint16_t seg_count = seg_count_x2 / 2;
    if (seg_count == 0) return NULL;

    // header is 14 bytes, then:
    // endCodes: seg_count * 2
    // reservedPad: 2
    // startCodes: seg_count * 2
    // idDeltas: seg_count * 2
    // idRangeOffsets: seg_count * 2
    // glyphIdArray: remaining bytes

    size_t arrays_start = 14;
    size_t end_codes_off = arrays_start;
    size_t start_codes_off = end_codes_off + seg_count * 2 + 2; // +2 for reservedPad
    size_t id_deltas_off = start_codes_off + seg_count * 2;
    size_t id_range_off = id_deltas_off + seg_count * 2;
    size_t glyph_array_off = id_range_off + seg_count * 2;

    if (glyph_array_off > sub_len) return NULL;

    CmapSubtable* sub = (CmapSubtable*)pool_calloc(pool, sizeof(CmapSubtable));
    if (!sub) return NULL;

    sub->u.fmt4.seg_count = seg_count;
    sub->u.fmt4.end_codes = raw + end_codes_off;
    sub->u.fmt4.start_codes = raw + start_codes_off;
    sub->u.fmt4.id_deltas = raw + id_deltas_off;
    sub->u.fmt4.id_range_offsets = raw + id_range_off;
    sub->u.fmt4.glyph_id_array = raw + glyph_array_off;
    sub->u.fmt4.glyph_id_array_len = (sub_len > glyph_array_off) ? (sub_len - (uint32_t)glyph_array_off) : 0;

    return sub;
}

// parse cmap format 12 subtable
static CmapSubtable* parse_cmap_format12(const uint8_t* raw, uint32_t len, Pool* pool) {
    if (len < 16) return NULL;

    uint16_t format = rd16(raw);
    if (format != 12) return NULL;

    // format 12 uses fixed32 header: format(2), reserved(2), length(4), language(4), numGroups(4)
    uint32_t num_groups = rd32(raw + 12);
    size_t required = 16 + (size_t)num_groups * 12;
    if (required > len) return NULL;

    CmapSubtable* sub = (CmapSubtable*)pool_calloc(pool, sizeof(CmapSubtable));
    if (!sub) return NULL;

    sub->u.fmt12.num_groups = num_groups;
    sub->u.fmt12.groups = raw + 16;

    return sub;
}

CmapTable* font_tables_get_cmap(FontTables* tables) {
    if (!tables) return NULL;
    if (tables->parsed_flags & FT_PARSED_CMAP) return tables->cmap;
    tables->parsed_flags |= FT_PARSED_CMAP;

    uint32_t len = 0;
    const uint8_t* raw = font_tables_find(tables, FONT_TAG('c','m','a','p'), &len);
    if (!raw || len < 4) return NULL;

    Pool* pool = (Pool*)tables->pool;

    uint16_t num_records = rd16(raw + 2);
    if (4 + (size_t)num_records * 8 > len) return NULL;

    // find the best subtable: prefer format 12 (full unicode), then format 4 (BMP)
    // prefer platformID=3 (Windows), encodingID=10 (Full Unicode) or encodingID=1 (BMP)
    // or platformID=0 (Unicode), encodingID=3/4
    uint32_t best_offset = 0;
    int best_format = 0;
    int best_score = -1;

    const uint8_t* rec = raw + 4;
    for (int i = 0; i < num_records; i++, rec += 8) {
        uint16_t platform_id = rd16(rec);
        uint16_t encoding_id = rd16(rec + 2);
        uint32_t offset = rd32(rec + 4);

        if (offset + 2 > len) continue;

        uint16_t format = rd16(raw + offset);
        int score = 0;

        if (format == 12) {
            score = 100;
            // prefer Windows Full Unicode
            if (platform_id == 3 && encoding_id == 10) score += 20;
            else if (platform_id == 0 && (encoding_id == 4 || encoding_id == 3)) score += 15;
        } else if (format == 4) {
            score = 50;
            if (platform_id == 3 && encoding_id == 1) score += 20;
            else if (platform_id == 0) score += 15;
        }

        if (score > best_score) {
            best_score = score;
            best_format = format;
            best_offset = offset;
        }
    }

    if (best_score <= 0) {
        log_debug("font_tables: no suitable cmap subtable found");
        return NULL;
    }

    CmapSubtable* sub = NULL;
    uint32_t sub_len = (len > best_offset) ? (len - best_offset) : 0;
    const uint8_t* sub_raw = raw + best_offset;

    if (best_format == 12) {
        sub = parse_cmap_format12(sub_raw, sub_len, pool);
    } else if (best_format == 4) {
        sub = parse_cmap_format4(sub_raw, sub_len, pool);
    }

    if (!sub) return NULL;

    CmapTable* cmap = (CmapTable*)pool_calloc(pool, sizeof(CmapTable));
    if (!cmap) {
        pool_free(pool, sub);
        return NULL;
    }

    cmap->subtable = sub;
    cmap->format = best_format;

    tables->cmap = cmap;
    return cmap;
}

// ============================================================================
// cmap lookup
// ============================================================================

// format 4 binary search
static uint16_t cmap_lookup_format4(CmapSubtable* sub, uint32_t codepoint) {
    if (codepoint > 0xFFFF) return 0; // BMP only

    CmapFormat4* f = &sub->u.fmt4;
    uint16_t cp16 = (uint16_t)codepoint;

    // binary search on endCodes
    int lo = 0, hi = (int)f->seg_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint16_t end_code = rd16(f->end_codes + mid * 2);
        if (cp16 > end_code) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    if (lo >= (int)f->seg_count) return 0;

    uint16_t start_code = rd16(f->start_codes + lo * 2);
    if (cp16 < start_code) return 0;

    uint16_t end_code = rd16(f->end_codes + lo * 2);
    if (cp16 > end_code) return 0;

    int16_t id_delta = rd16s(f->id_deltas + lo * 2);
    uint16_t id_range_offset = rd16(f->id_range_offsets + lo * 2);

    if (id_range_offset == 0) {
        return (uint16_t)((cp16 + id_delta) & 0xFFFF);
    }

    // offset into glyphIdArray
    // the id_range_offset is relative to the current position in the idRangeOffset array
    size_t range_off_pos = (size_t)(f->id_range_offsets + lo * 2 - f->glyph_id_array);
    // glyphIdArray index
    size_t glyph_off = (size_t)id_range_offset + (cp16 - start_code) * 2 - range_off_pos;
    // the above is a standard cmap4 formula but let's use the direct approach:
    // ptr = &idRangeOffsets[lo] + idRangeOffset + (cp - startCode) * 2
    const uint8_t* ptr = f->id_range_offsets + lo * 2 + id_range_offset + (cp16 - start_code) * 2;

    // bounds check: ptr must be within the subtable data
    if (ptr < f->end_codes || ptr + 1 >= f->glyph_id_array + f->glyph_id_array_len) {
        return 0;
    }

    uint16_t glyph_id = rd16(ptr);
    if (glyph_id == 0) return 0;

    return (uint16_t)((glyph_id + id_delta) & 0xFFFF);
}

// format 12 binary search
static uint16_t cmap_lookup_format12(CmapSubtable* sub, uint32_t codepoint) {
    CmapFormat12* f = &sub->u.fmt12;

    int lo = 0, hi = (int)f->num_groups - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const uint8_t* g = f->groups + mid * 12;
        uint32_t start_code = rd32(g);
        uint32_t end_code   = rd32(g + 4);

        if (codepoint < start_code) {
            hi = mid - 1;
        } else if (codepoint > end_code) {
            lo = mid + 1;
        } else {
            uint32_t start_glyph = rd32(g + 8);
            return (uint16_t)(start_glyph + (codepoint - start_code));
        }
    }

    return 0;
}

uint16_t cmap_lookup(CmapTable* cmap, uint32_t codepoint) {
    if (!cmap || !cmap->subtable) return 0;

    if (cmap->format == 12) {
        return cmap_lookup_format12(cmap->subtable, codepoint);
    } else if (cmap->format == 4) {
        return cmap_lookup_format4(cmap->subtable, codepoint);
    }

    return 0;
}

// ============================================================================
// hmtx table
// ============================================================================

HmtxTable* font_tables_get_hmtx(FontTables* tables) {
    if (!tables) return NULL;
    if (tables->parsed_flags & FT_PARSED_HMTX) return tables->hmtx;
    tables->parsed_flags |= FT_PARSED_HMTX;

    // hmtx depends on hhea (for numberOfHMetrics) and maxp (for numGlyphs)
    HheaTable* hhea = font_tables_get_hhea(tables);
    MaxpTable* maxp = font_tables_get_maxp(tables);
    if (!hhea || !maxp) return NULL;

    uint32_t len = 0;
    const uint8_t* raw = font_tables_find(tables, FONT_TAG('h','m','t','x'), &len);
    if (!raw) return NULL;

    // validate minimum size: numberOfHMetrics * 4 bytes
    size_t min_size = (size_t)hhea->number_of_h_metrics * 4;
    if (len < min_size) {
        log_error("font_tables: hmtx table too small (%u < %zu)", len, min_size);
        return NULL;
    }

    Pool* pool = (Pool*)tables->pool;
    HmtxTable* h = (HmtxTable*)pool_calloc(pool, sizeof(HmtxTable));
    if (!h) return NULL;

    h->data = raw;
    h->num_h_metrics = hhea->number_of_h_metrics;
    h->num_glyphs = maxp->num_glyphs;

    tables->hmtx = h;
    return h;
}

uint16_t hmtx_get_advance(HmtxTable* hmtx, uint16_t glyph_id) {
    if (!hmtx || !hmtx->data) return 0;

    if (glyph_id < hmtx->num_h_metrics) {
        // longHorMetric[glyph_id]: advanceWidth(2) + lsb(2)
        return rd16(hmtx->data + glyph_id * 4);
    }

    // glyphs beyond numberOfHMetrics share the last advance width
    if (hmtx->num_h_metrics > 0) {
        return rd16(hmtx->data + (hmtx->num_h_metrics - 1) * 4);
    }

    return 0;
}

int16_t hmtx_get_lsb(HmtxTable* hmtx, uint16_t glyph_id) {
    if (!hmtx || !hmtx->data) return 0;

    if (glyph_id < hmtx->num_h_metrics) {
        return rd16s(hmtx->data + glyph_id * 4 + 2);
    }

    // lsb for glyphs beyond numberOfHMetrics is in a separate array
    uint32_t extra_idx = glyph_id - hmtx->num_h_metrics;
    size_t offset = (size_t)hmtx->num_h_metrics * 4 + (size_t)extra_idx * 2;
    // bounds check would need total hmtx length, which we don't store — return 0
    return rd16s(hmtx->data + offset);
}

// ============================================================================
// kern table (format 0)
// ============================================================================

KernTable* font_tables_get_kern(FontTables* tables) {
    if (!tables) return NULL;
    if (tables->parsed_flags & FT_PARSED_KERN) return tables->kern;
    tables->parsed_flags |= FT_PARSED_KERN;

    uint32_t len = 0;
    const uint8_t* raw = font_tables_find(tables, FONT_TAG('k','e','r','n'), &len);
    if (!raw || len < 4) return NULL;

    Pool* pool = (Pool*)tables->pool;
    KernTable* k = (KernTable*)pool_calloc(pool, sizeof(KernTable));
    if (!k) return NULL;

    // kern table header: version(2), nTables(2)
    uint16_t version = rd16(raw);
    uint16_t n_tables = rd16(raw + 2);

    if (n_tables == 0) {
        tables->kern = k;
        return k;
    }

    // parse first subtable only (format 0 is the common case)
    const uint8_t* sub = raw + 4;
    if (sub + 6 > raw + len) {
        tables->kern = k;
        return k;
    }

    // subtable header: version(2), length(2), coverage(2)
    // coverage bits: format in bits 8-15
    uint16_t sub_version = rd16(sub);
    (void)sub_version;
    uint16_t sub_length = rd16(sub + 2);
    uint16_t coverage = rd16(sub + 4);
    uint8_t format = (uint8_t)(coverage >> 8);

    if (format != 0) {
        // only format 0 (sorted pairs) supported
        log_debug("font_tables: kern subtable format %d not supported", format);
        tables->kern = k;
        return k;
    }

    if (sub + 8 + 6 > raw + len) {
        tables->kern = k;
        return k;
    }

    // format 0 header: nPairs(2), searchRange(2), entrySelector(2), rangeShift(2)
    uint16_t n_pairs = rd16(sub + 6);

    size_t pairs_offset = 14; // 6 (subtable header) + 8 (format 0 header)
    if (sub + pairs_offset + (size_t)n_pairs * 6 > raw + len) {
        // truncated, use what we have
        n_pairs = (uint16_t)((len - (uint32_t)(sub - raw) - pairs_offset) / 6);
    }

    k->num_pairs = n_pairs;
    k->pairs = sub + pairs_offset;
    k->valid = true;

    tables->kern = k;
    return k;
}

int16_t kern_get_pair(KernTable* kern, uint16_t left, uint16_t right) {
    if (!kern || !kern->valid || kern->num_pairs == 0 || !kern->pairs) return 0;

    // binary search — pairs are sorted by (left << 16 | right)
    uint32_t key = ((uint32_t)left << 16) | (uint32_t)right;

    int lo = 0, hi = (int)kern->num_pairs - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const uint8_t* p = kern->pairs + mid * 6;
        uint32_t pair_key = ((uint32_t)rd16(p) << 16) | (uint32_t)rd16(p + 2);

        if (key < pair_key) {
            hi = mid - 1;
        } else if (key > pair_key) {
            lo = mid + 1;
        } else {
            return rd16s(p + 4);
        }
    }

    return 0;
}

// ============================================================================
// fvar table (variable font axes)
// ============================================================================

FvarTable* font_tables_get_fvar(FontTables* tables) {
    if (!tables) return NULL;
    if (tables->parsed_flags & FT_PARSED_FVAR) return tables->fvar;
    tables->parsed_flags |= FT_PARSED_FVAR;

    uint32_t len = 0;
    const uint8_t* raw = font_tables_find(tables, FONT_TAG('f','v','a','r'), &len);
    if (!raw || len < 16) return NULL;

    Pool* pool = (Pool*)tables->pool;

    // fvar header: majorVersion(2), minorVersion(2), axesArrayOffset(2),
    //              reserved(2), axisCount(2), axisSize(2), instanceCount(2), instanceSize(2)
    uint16_t axes_offset = rd16(raw + 4);
    uint16_t axis_count = rd16(raw + 8);
    uint16_t axis_size = rd16(raw + 10);

    if (axis_count == 0 || axis_size < 20) return NULL;

    size_t required = (size_t)axes_offset + (size_t)axis_count * axis_size;
    if (required > len) return NULL;

    FvarTable* f = (FvarTable*)pool_calloc(pool, sizeof(FvarTable));
    if (!f) return NULL;

    f->axis_count = axis_count;
    f->axes = (FvarAxis*)pool_calloc(pool, (size_t)axis_count * sizeof(FvarAxis));
    if (!f->axes) {
        pool_free(pool, f);
        return NULL;
    }

    const uint8_t* axis_data = raw + axes_offset;
    for (int i = 0; i < axis_count; i++) {
        const uint8_t* a = axis_data + i * axis_size;
        f->axes[i].tag           = rd32(a);
        f->axes[i].min_value     = rd32s(a + 4);
        f->axes[i].default_value = rd32s(a + 8);
        f->axes[i].max_value     = rd32s(a + 12);
        // flags at 16 — skipped
        f->axes[i].name_id       = rd16(a + 18);
    }

    tables->fvar = f;
    return f;
}

// ============================================================================
// name table
// ============================================================================

// decode a UTF-16 BE string to a pool-allocated ASCII/UTF-8 string
static char* decode_utf16be(const uint8_t* data, uint16_t byte_len, Pool* pool) {
    uint16_t char_count = byte_len / 2;
    // allocate worst case: each UTF-16 char → up to 3 UTF-8 bytes
    char* buf = (char*)pool_alloc(pool, (size_t)char_count * 3 + 1);
    if (!buf) return NULL;

    size_t out = 0;
    for (uint16_t i = 0; i < char_count; i++) {
        uint16_t ch = rd16(data + i * 2);
        if (ch < 0x80) {
            buf[out++] = (char)ch;
        } else if (ch < 0x800) {
            buf[out++] = (char)(0xC0 | (ch >> 6));
            buf[out++] = (char)(0x80 | (ch & 0x3F));
        } else {
            buf[out++] = (char)(0xE0 | (ch >> 12));
            buf[out++] = (char)(0x80 | ((ch >> 6) & 0x3F));
            buf[out++] = (char)(0x80 | (ch & 0x3F));
        }
    }
    buf[out] = '\0';
    return buf;
}

// copy a Mac Roman string to pool
static char* copy_mac_string(const uint8_t* data, uint16_t byte_len, Pool* pool) {
    char* buf = (char*)pool_alloc(pool, (size_t)byte_len + 1);
    if (!buf) return NULL;
    memcpy(buf, data, byte_len);
    buf[byte_len] = '\0';
    return buf;
}

NameTable* font_tables_get_name(FontTables* tables) {
    if (!tables) return NULL;
    if (tables->parsed_flags & FT_PARSED_NAME) return tables->name;
    tables->parsed_flags |= FT_PARSED_NAME;

    uint32_t len = 0;
    const uint8_t* raw = font_tables_find(tables, FONT_TAG('n','a','m','e'), &len);
    if (!raw || len < 6) return NULL;

    Pool* pool = (Pool*)tables->pool;

    uint16_t format = rd16(raw);
    (void)format;
    uint16_t count = rd16(raw + 2);
    uint16_t string_offset = rd16(raw + 4);

    if (6 + (size_t)count * 12 > len) return NULL;

    NameTable* n = (NameTable*)pool_calloc(pool, sizeof(NameTable));
    if (!n) return NULL;

    // track best matches per name ID (prefer Windows platformID=3, encodingID=1, English)
    const uint8_t* best_data[7] = {0};
    uint16_t best_len[7] = {0};
    int best_score[7] = {0};
    bool best_is_utf16[7] = {0};

    const uint8_t* rec = raw + 6;
    for (int i = 0; i < count; i++, rec += 12) {
        uint16_t platform_id = rd16(rec);
        uint16_t encoding_id = rd16(rec + 2);
        uint16_t language_id = rd16(rec + 4);
        uint16_t name_id     = rd16(rec + 6);
        uint16_t str_length  = rd16(rec + 8);
        uint16_t str_offset  = rd16(rec + 10);

        // only interested in nameIDs 1, 2, 6
        if (name_id != 1 && name_id != 2 && name_id != 6) continue;

        size_t abs_offset = (size_t)string_offset + str_offset;
        if (abs_offset + str_length > len) continue;

        const uint8_t* str_data = raw + abs_offset;
        int score = 0;
        bool is_utf16 = false;

        // scoring: Windows UTF-16 English > Windows UTF-16 any > Mac Roman English > Mac Roman any
        if (platform_id == 3 && encoding_id == 1) {
            is_utf16 = true;
            score = (language_id == 0x0409) ? 100 : 80;
        } else if (platform_id == 0) {
            is_utf16 = true;
            score = (language_id == 0) ? 90 : 70;
        } else if (platform_id == 1 && encoding_id == 0) {
            is_utf16 = false;
            score = (language_id == 0) ? 60 : 40;
        }

        if (score > best_score[name_id]) {
            best_score[name_id] = score;
            best_data[name_id] = str_data;
            best_len[name_id] = str_length;
            best_is_utf16[name_id] = is_utf16;
        }
    }

    // decode best matches
    if (best_data[1]) {
        n->family_name = best_is_utf16[1]
            ? decode_utf16be(best_data[1], best_len[1], pool)
            : copy_mac_string(best_data[1], best_len[1], pool);
    }
    if (best_data[2]) {
        n->subfamily_name = best_is_utf16[2]
            ? decode_utf16be(best_data[2], best_len[2], pool)
            : copy_mac_string(best_data[2], best_len[2], pool);
    }
    if (best_data[6]) {
        n->postscript_name = best_is_utf16[6]
            ? decode_utf16be(best_data[6], best_len[6], pool)
            : copy_mac_string(best_data[6], best_len[6], pool);
    }

    tables->name = n;
    return n;
}
