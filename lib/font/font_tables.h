/**
 * Lambda Unified Font Module — TTF/OTF Table Parser (Header)
 *
 * Parses OpenType/TrueType font tables directly from raw font bytes,
 * replacing FreeType for metric extraction and codepoint lookup.
 *
 * Tables are parsed lazily on first access and cached on the FontTables struct.
 * Data stays in the original memory-mapped buffer where possible (zero-copy).
 *
 * Copyright (c) 2025-2026 Lambda Script Project
 */

#ifndef LAMBDA_FONT_TABLES_H
#define LAMBDA_FONT_TABLES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Parsed table structures
// ============================================================================

typedef struct HeadTable {
    uint16_t units_per_em;
    int16_t  x_min, y_min, x_max, y_max;
    uint16_t mac_style;                 // bit 0 = bold, bit 1 = italic
    int16_t  index_to_loc_format;       // 0 = short offsets, 1 = long offsets
    int32_t  created;                   // not fully used but parsed
    int32_t  modified;
} HeadTable;

typedef struct HheaTable {
    int16_t  ascender;
    int16_t  descender;
    int16_t  line_gap;
    uint16_t advance_width_max;
    uint16_t number_of_h_metrics;
} HheaTable;

typedef struct MaxpTable {
    uint16_t num_glyphs;
} MaxpTable;

typedef struct Os2Table {
    uint16_t version;
    int16_t  avg_char_width;
    uint16_t weight_class;              // usWeightClass: 100-900
    uint16_t width_class;
    uint16_t fs_type;
    int16_t  y_subscript_x_size;
    int16_t  y_subscript_y_size;
    int16_t  y_subscript_x_offset;
    int16_t  y_subscript_y_offset;
    int16_t  y_superscript_x_size;
    int16_t  y_superscript_y_size;
    int16_t  y_superscript_x_offset;
    int16_t  y_superscript_y_offset;
    int16_t  y_strikeout_size;
    int16_t  y_strikeout_position;
    int16_t  s_family_class;
    // panose[10] — skipped
    // ulUnicodeRange1-4 — skipped
    // achVendID[4] — skipped
    uint16_t fs_selection;              // fsSelection (bit 0=italic, bit 5=bold, bit 7=USE_TYPO_METRICS)
    uint16_t first_char_index;
    uint16_t last_char_index;
    int16_t  s_typo_ascender;           // sTypoAscender
    int16_t  s_typo_descender;          // sTypoDescender (negative)
    int16_t  s_typo_line_gap;           // sTypoLineGap
    uint16_t us_win_ascent;             // usWinAscent
    uint16_t us_win_descent;            // usWinDescent
    // version >= 2
    int16_t  sx_height;                 // sxHeight (0 if version < 2)
    int16_t  s_cap_height;              // sCapHeight (0 if version < 2)
} Os2Table;

typedef struct PostTable {
    int32_t  format;                    // fixed 16.16
    int16_t  underline_position;        // in font units
    int16_t  underline_thickness;       // in font units
    uint32_t is_fixed_pitch;
} PostTable;

// cmap subtable — opaque, parsed internally
typedef struct CmapSubtable CmapSubtable;

typedef struct CmapTable {
    CmapSubtable* subtable;            // best subtable (format 4 or 12)
    int format;                         // 4 or 12
} CmapTable;

// hmtx — horizontal metrics
typedef struct HmtxTable {
    const uint8_t* data;                // raw hmtx data (zero-copy from font buffer)
    uint16_t num_h_metrics;             // from hhea
    uint16_t num_glyphs;                // from maxp
} HmtxTable;

// kern format 0 — pair kerning
typedef struct KernTable {
    const uint8_t* pairs;               // raw pair data
    uint16_t num_pairs;
    bool valid;
} KernTable;

// fvar — variable font axes
typedef struct FvarAxis {
    uint32_t tag;                       // axis tag (e.g., 'wght', 'opsz')
    int32_t  min_value;                 // 16.16 fixed-point
    int32_t  default_value;
    int32_t  max_value;
    uint16_t name_id;
} FvarAxis;

typedef struct FvarTable {
    FvarAxis* axes;
    uint16_t  axis_count;
} FvarTable;

// name table — parsed name strings
typedef struct NameTable {
    char* family_name;                  // nameID 1
    char* subfamily_name;              // nameID 2
    char* postscript_name;             // nameID 6
} NameTable;

// ============================================================================
// Raw table directory entry
// ============================================================================

typedef struct FontTableDir {
    uint32_t tag;
    uint32_t checksum;
    uint32_t offset;
    uint32_t length;
} FontTableDir;

// ============================================================================
// FontTables — main container (per font face)
// ============================================================================

typedef struct FontTables {
    // raw font data (not owned — points into arena or mmap'd buffer)
    const uint8_t* data;
    size_t data_len;

    // table directory
    FontTableDir* dirs;
    uint16_t      num_tables;
    uint32_t      scaler_type;

    // lazily-parsed tables (NULL until first access)
    HeadTable*  head;
    HheaTable*  hhea;
    MaxpTable*  maxp;
    Os2Table*   os2;
    PostTable*  post;
    CmapTable*  cmap;
    HmtxTable*  hmtx;
    KernTable*  kern;
    FvarTable*  fvar;
    NameTable*  name;

    // parse flags (to avoid re-parsing on NULL result)
    uint32_t parsed_flags;

    // memory pool for allocations
    void* pool;                         // Pool* — void* to avoid including mempool.h
} FontTables;

// parsed_flags bits
#define FT_PARSED_HEAD  (1u << 0)
#define FT_PARSED_HHEA  (1u << 1)
#define FT_PARSED_MAXP  (1u << 2)
#define FT_PARSED_OS2   (1u << 3)
#define FT_PARSED_POST  (1u << 4)
#define FT_PARSED_CMAP  (1u << 5)
#define FT_PARSED_HMTX  (1u << 6)
#define FT_PARSED_KERN  (1u << 7)
#define FT_PARSED_FVAR  (1u << 8)
#define FT_PARSED_NAME  (1u << 9)

// ============================================================================
// Public API
// ============================================================================

// open a FontTables from raw font data. The data buffer must outlive the
// FontTables. pool is used for internal allocations. Returns NULL on error.
FontTables* font_tables_open(const uint8_t* data, size_t data_len, void* pool);

// close and free all internal allocations. Does NOT free data buffer.
void font_tables_close(FontTables* tables, void* pool);

// find a raw table by 4-byte tag. Returns NULL if not found.
// *out_len is set to the table length.
const uint8_t* font_tables_find(FontTables* tables, uint32_t tag, uint32_t* out_len);

// check if a table exists
bool font_tables_has(FontTables* tables, uint32_t tag);

// lazy table accessors — parse on first call, cache result
HeadTable*  font_tables_get_head(FontTables* tables);
HheaTable*  font_tables_get_hhea(FontTables* tables);
MaxpTable*  font_tables_get_maxp(FontTables* tables);
Os2Table*   font_tables_get_os2(FontTables* tables);
PostTable*  font_tables_get_post(FontTables* tables);
CmapTable*  font_tables_get_cmap(FontTables* tables);
HmtxTable*  font_tables_get_hmtx(FontTables* tables);
KernTable*  font_tables_get_kern(FontTables* tables);
FvarTable*  font_tables_get_fvar(FontTables* tables);
NameTable*  font_tables_get_name(FontTables* tables);

// ============================================================================
// Lookup functions
// ============================================================================

// look up a codepoint in the cmap table. Returns glyph ID, or 0 if not found.
uint16_t cmap_lookup(CmapTable* cmap, uint32_t codepoint);

// get horizontal advance width for a glyph ID, in font design units.
uint16_t hmtx_get_advance(HmtxTable* hmtx, uint16_t glyph_id);

// get left side bearing for a glyph ID, in font design units.
int16_t hmtx_get_lsb(HmtxTable* hmtx, uint16_t glyph_id);

// get kerning value for a pair of glyph IDs, in font design units.
// returns 0 if no kerning pair found.
int16_t kern_get_pair(KernTable* kern, uint16_t left, uint16_t right);

// ============================================================================
// Utility: make a 4-byte tag from characters
// ============================================================================

#define FONT_TAG(a, b, c, d) \
    ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_FONT_TABLES_H
