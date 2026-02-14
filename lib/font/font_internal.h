/**
 * Lambda Unified Font Module — Internal Header
 *
 * This header contains FreeType types and internal struct definitions.
 * It is ONLY included by files inside lib/font/. No external caller
 * should ever include this header directly.
 *
 * FreeType types (FT_Face, FT_Library, etc.) are confined here so that
 * the public API (font.h) exposes only opaque handles.
 *
 * Copyright (c) 2025 Lambda Script Project
 */

#ifndef LAMBDA_FONT_INTERNAL_H
#define LAMBDA_FONT_INTERNAL_H

#include "font.h"
#include "../mempool.h"
#include "../arena.h"
#include "../hashmap.h"
#include "../arraylist.h"
#include "../log.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_BITMAP_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H
#include FT_TRUETYPE_TABLES_H
#include FT_LCD_FILTER_H
#include FT_SIZES_H
#include FT_MODULE_H

#include <time.h>
#include <string.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Font Format Detection
// ============================================================================

typedef enum FontFormat {
    FONT_FORMAT_TTF,
    FONT_FORMAT_OTF,
    FONT_FORMAT_TTC,
    FONT_FORMAT_WOFF,
    FONT_FORMAT_WOFF2,
    FONT_FORMAT_UNKNOWN,
} FontFormat;

// detect format from magic bytes in data buffer
FontFormat font_detect_format(const uint8_t* data, size_t len);

// detect format from file extension (fallback)
FontFormat font_detect_format_ext(const char* path);

// ============================================================================
// FontHandle — internal layout (public API sees opaque pointer)
// ============================================================================

struct FontHandle {
    FT_Face     ft_face;                // FreeType face object
    int         ref_count;              // reference counting

    // cached metrics (computed lazily on first font_get_metrics call)
    FontMetrics metrics;
    bool        metrics_ready;

    // memory buffer for in-memory loaded fonts (WOFF decompressed, data URI, etc.)
    // FreeType requires the buffer to outlive the face, so arena-allocated.
    uint8_t*    memory_buffer;
    size_t      memory_buffer_size;

    // per-face glyph advance cache: codepoint → advance_x
    struct hashmap* advance_cache;

    // back-pointer to owning context (for pool access)
    FontContext* ctx;

    // LRU tracking for face cache eviction
    uint32_t    lru_tick;

    // font info
    float       size_px;                // requested size in CSS pixels
    float       physical_size_px;       // size * pixel_ratio (actual FreeType size)
    FontWeight  weight;
    FontSlant   slant;
    char*       family_name;            // arena-allocated
};

// ============================================================================
// Font Database Entry (migrated from font_config.h)
// ============================================================================

typedef struct FontUnicodeRange {
    uint32_t start_codepoint;
    uint32_t end_codepoint;
    struct FontUnicodeRange* next;
} FontUnicodeRange;

typedef struct FontEntry {
    char*       family_name;            // "Arial", "Times New Roman"
    char*       subfamily_name;         // "Regular", "Bold Italic"
    char*       postscript_name;        // "Arial-BoldMT"
    char*       file_path;              // full path to font file

    int         weight;                 // 100-900 (CSS weight scale)
    FontSlant   style;                  // normal/italic/oblique
    bool        is_monospace;           // fixed-width font
    FontFormat  format;                 // TTF, OTF, etc.

    // unicode coverage
    FontUnicodeRange* unicode_ranges;
    uint32_t    unicode_coverage_hash;

    // file metadata
    time_t      file_mtime;             // for cache invalidation
    size_t      file_size;

    // collection info (for .ttc files)
    int         collection_index;
    bool        is_collection;

    // lazy loading flag
    bool        is_placeholder;         // true if needs full parsing
} FontEntry;

typedef struct FontFamily {
    char*       family_name;
    ArrayList*  aliases;                // alternative family names
    ArrayList*  fonts;                  // array of FontEntry*
    bool        is_system_family;
} FontFamily;

// ============================================================================
// Font Database
// ============================================================================

typedef struct FontDatabase {
    // core storage
    HashMap*    families;               // family_name → FontFamily*
    HashMap*    postscript_names;       // ps_name → FontEntry*
    HashMap*    file_paths;             // file_path → FontEntry*
    ArrayList*  all_fonts;              // all FontEntry* for iteration
    ArrayList*  font_files;             // lazy loading: discovered font file paths

    // platform directories
    ArrayList*  scan_directories;

    // cache metadata
    time_t      last_scan;
    char*       cache_file_path;
    bool        cache_dirty;
    bool        scanned;                // true after first scan

    // memory management (owned by FontContext)
    Pool*       pool;
    Arena*      arena;
} FontDatabase;

typedef struct FontDatabaseCriteria {
    char        family_name[256];
    int         weight;                 // 100-900, -1 for any
    FontSlant   style;
    bool        prefer_monospace;
    uint32_t    required_codepoint;     // must support this codepoint (0 = any)
    char        language[8];            // language hint (ISO 639-1)
} FontDatabaseCriteria;

typedef struct FontDatabaseResult {
    FontEntry*  font;
    float       match_score;            // 0.0-1.0
    bool        exact_family_match;
    bool        requires_synthesis;
    char*       synthetic_style;
} FontDatabaseResult;

// ============================================================================
// FontFaceEntry — internal storage for registered @font-face descriptors
// ============================================================================

typedef struct FontFaceEntry {
    // copied from FontFaceDesc at registration time
    char*       family;                 // arena_strdup'd
    FontWeight  weight;
    FontSlant   slant;

    // sources array (pool-allocated)
    struct FontFaceEntrySrc {
        char* path;                     // arena_strdup'd
        char* format;                   // arena_strdup'd, or NULL
    } *sources;
    int source_count;

    // loaded handle (NULL until first load)
    FontHandle* loaded_handle;
} FontFaceEntry;

// ============================================================================
// FontContext — internal layout (public API sees opaque pointer)
// ============================================================================

struct FontContext {
    // memory management
    Pool*           pool;               // owned (or borrowed if caller supplied)
    Arena*          arena;              // owned (or borrowed if caller supplied)
    Arena*          glyph_arena;        // separate arena for glyph bitmap data
    bool            owns_pool;          // true if we created the pool
    bool            owns_arena;         // true if we created the arena

    // FreeType
    FT_Library      ft_library;
    struct FT_MemoryRec_   ft_memory;          // custom allocator routing to pool

    // font database
    FontDatabase*   database;

    // face cache: cache_key → FontHandle*
    struct hashmap*  face_cache;
    uint32_t         lru_counter;       // monotonically increasing for LRU

    // glyph bitmap cache
    struct hashmap*  bitmap_cache;

    // codepoint → fallback handle cache (for font_find_codepoint_fallback)
    struct hashmap*  codepoint_fallback_cache;

    // registered @font-face descriptors
    FontFaceEntry** face_descriptors;
    int             face_descriptor_count;
    int             face_descriptor_capacity;

    // fallback fonts list
    const char**    fallback_fonts;

    // configuration
    FontContextConfig config;
};

// ============================================================================
// Internal helper: face cache key
// ============================================================================

typedef struct FontCacheKey {
    char*       key_str;                // "family:weight:slant:size" — arena-allocated
    FontHandle* handle;                 // associated handle (for hashmap storage)
} FontCacheKey;

// ============================================================================
// Internal helper: glyph advance cache entry
// ============================================================================

typedef struct GlyphAdvanceEntry {
    uint32_t codepoint;
    uint32_t glyph_id;
    float    advance_x;
} GlyphAdvanceEntry;

// ============================================================================
// Internal helper: glyph bitmap cache entry
// ============================================================================

typedef struct BitmapCacheEntry {
    uint32_t        codepoint;
    GlyphRenderMode mode;
    FontHandle*     handle;
    GlyphBitmap     bitmap;
} BitmapCacheEntry;

// ============================================================================
// Internal functions — called across font module source files
// ============================================================================

// font_database.c
FontDatabase*       font_database_create_internal(Pool* pool, Arena* arena);
void                font_database_destroy_internal(FontDatabase* db);
bool                font_database_scan_internal(FontDatabase* db);
FontDatabaseResult  font_database_find_best_match_internal(FontDatabase* db, FontDatabaseCriteria* criteria);
ArrayList*          font_database_find_all_matches_internal(FontDatabase* db, const char* family_name);
FontEntry*          font_database_get_by_postscript_name_internal(FontDatabase* db, const char* ps_name);

// font_platform.c
void                font_platform_add_default_dirs(FontDatabase* db);
char*               font_platform_find_fallback(const char* font_name);

// font_loader.c
FontHandle*         font_load_face_internal(FontContext* ctx, const char* path,
                                            int face_index, float size_px,
                                            float physical_size, FontWeight weight, FontSlant slant);
FontHandle*         font_load_memory_internal(FontContext* ctx, const uint8_t* data,
                                              size_t len, int face_index, float size_px,
                                              float physical_size, FontWeight weight, FontSlant slant);
void                font_select_best_fixed_size(FT_Face face, int target_ppem);

// font_decompress.c
bool                font_decompress_woff1(Arena* arena, const uint8_t* data, size_t len,
                                          uint8_t** out, size_t* out_len);
bool                font_decompress_woff2(Arena* arena, const uint8_t* data, size_t len,
                                          uint8_t** out, size_t* out_len);
bool                font_decompress_if_needed(Arena* arena, const uint8_t* data, size_t len,
                                              FontFormat format,
                                              const uint8_t** out, size_t* out_len);

// font_metrics.c
void                font_compute_metrics(FontHandle* handle);

// font_glyph.c
GlyphInfo           font_load_glyph_internal(FontHandle* handle, uint32_t codepoint);

// font_cache.c
FontHandle*         font_cache_lookup(FontContext* ctx, const char* key);
void                font_cache_insert(FontContext* ctx, const char* key, FontHandle* handle);
void                font_cache_evict_lru(FontContext* ctx);
char*               font_cache_make_key(Arena* arena, const char* family,
                                        FontWeight weight, FontSlant slant, float size_px);

// font_fallback.c
const char**        font_get_generic_family(const char* family);
FontHandle*         font_resolve_fallback(FontContext* ctx, const FontStyleDesc* style);
FontHandle*         font_find_codepoint_fallback(FontContext* ctx, const FontStyleDesc* style,
                                                  uint32_t codepoint);

// font_face.c
const FontFaceEntry* font_face_find_internal(FontContext* ctx, const char* family,
                                              FontWeight weight, FontSlant slant);

// ============================================================================
// Internal utility macros
// ============================================================================

// 26.6 fixed-point to float conversion (FreeType uses 26.6 format)
#define FT_F26DOT6_TO_FLOAT(x) ((float)(x) / 64.0f)

// float to 26.6 fixed-point
#define FT_FLOAT_TO_F26DOT6(x) ((FT_F26Dot6)((x) * 64.0f))

// 16.16 fixed-point to float (FreeType uses for some metrics)
#define FT_F16DOT16_TO_FLOAT(x) ((float)(x) / 65536.0f)

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_FONT_INTERNAL_H
