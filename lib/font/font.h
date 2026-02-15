/**
 * Lambda Unified Font Module — Public API
 *
 * Single header that callers include. No FreeType types are exposed.
 * Provides: FontContext lifecycle, font resolution, glyph metrics/rendering,
 * font face management (@font-face registry), fallback chain resolution,
 * and multi-format loading (TTF/OTF/TTC/WOFF1/WOFF2/data URI).
 *
 * Memory: all allocations go through Lambda Pool/Arena allocators.
 * Thread safety: single-threaded (matches FreeType constraints).
 *
 * Copyright (c) 2025 Lambda Script Project
 */

#ifndef LAMBDA_FONT_H
#define LAMBDA_FONT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Opaque types — callers never see internal layout
// ============================================================================

typedef struct FontContext FontContext;
typedef struct FontHandle  FontHandle;
typedef uint32_t           GlyphId;

// Forward declarations for Lambda allocators
struct Pool;
struct Arena;

// ============================================================================
// Font Context — replaces FT_Library + FontDatabase + UiContext font fields
// ============================================================================

typedef struct FontContextConfig {
    struct Pool*  pool;                 // memory pool (NULL = create internally)
    struct Arena* arena;                // arena for strings (NULL = create internally)
    float         pixel_ratio;          // display pixel ratio (1.0, 2.0, etc.)
    const char*   cache_dir;            // disk cache directory (NULL = default)
    int           max_cached_faces;     // max open font faces (0 = default 64)
    int           max_cached_glyphs;    // max cached glyph bitmaps (0 = default 4096)
    bool          enable_lcd_rendering; // subpixel rendering
    bool          enable_disk_cache;    // persist font database to disk
} FontContextConfig;

FontContext* font_context_create(FontContextConfig* config);
void         font_context_destroy(FontContext* ctx);

// trigger font directory scanning (auto-called on first lookup if needed)
bool font_context_scan(FontContext* ctx);

// ============================================================================
// Font Style — input for font resolution (CSS-like)
// ============================================================================

typedef enum FontWeight {
    FONT_WEIGHT_THIN        = 100,
    FONT_WEIGHT_EXTRA_LIGHT = 200,
    FONT_WEIGHT_LIGHT       = 300,
    FONT_WEIGHT_NORMAL      = 400,
    FONT_WEIGHT_MEDIUM      = 500,
    FONT_WEIGHT_SEMI_BOLD   = 600,
    FONT_WEIGHT_BOLD        = 700,
    FONT_WEIGHT_EXTRA_BOLD  = 800,
    FONT_WEIGHT_BLACK       = 900,
} FontWeight;

typedef enum FontSlant {
    FONT_SLANT_NORMAL,
    FONT_SLANT_ITALIC,
    FONT_SLANT_OBLIQUE,
} FontSlant;

typedef struct FontStyleDesc {
    const char* family;             // CSS font-family (single name or comma-separated)
    float       size_px;            // desired size in CSS pixels
    FontWeight  weight;
    FontSlant   slant;
} FontStyleDesc;

// ============================================================================
// Font Handle — an opened, sized font face (opaque, ref-counted)
// ============================================================================

// resolve a FontStyleDesc to a loaded, sized font handle.
// returns NULL on failure. caller must release with font_handle_release().
FontHandle* font_resolve(FontContext* ctx, const FontStyleDesc* style);

// increment / decrement reference count
FontHandle* font_handle_retain(FontHandle* handle);
void        font_handle_release(FontHandle* handle);

// ============================================================================
// Font Metrics — per-face, per-size metrics
// ============================================================================

typedef struct FontMetrics {
    float ascender;             // above baseline (positive)
    float descender;            // below baseline (negative)
    float line_height;          // ascender - descender + line_gap
    float line_gap;             // additional leading

    // OpenType table metrics (for browser-compat line height)
    float typo_ascender, typo_descender, typo_line_gap;
    float win_ascent, win_descent;
    float hhea_ascender, hhea_descender, hhea_line_gap, hhea_line_height;

    // useful typographic measures
    float x_height;             // height of lowercase 'x'
    float cap_height;           // height of uppercase letters
    float space_width;          // advance width of U+0020 SPACE
    float em_size;              // units per em (typically 1000 or 2048)
    float underline_position;   // underline position below baseline (positive = down)
    float underline_thickness;  // underline stroke thickness

    bool has_kerning;           // font contains kerning data
    bool use_typo_metrics;      // OS/2 fsSelection bit 7 (USE_TYPO_METRICS) is set
} FontMetrics;

// get metrics for a resolved font handle (cached, zero-cost after first call)
const FontMetrics* font_get_metrics(FontHandle* handle);

// ============================================================================
// Glyph Info — per-glyph measurement (cached)
// ============================================================================

typedef struct GlyphInfo {
    GlyphId id;                 // glyph index in the font
    float   advance_x;         // horizontal advance
    float   advance_y;         // vertical advance (usually 0 for horizontal text)
    float   bearing_x;         // left side bearing
    float   bearing_y;         // top side bearing
    int     width, height;     // bitmap dimensions
    bool    is_color;          // color emoji / COLR glyph
} GlyphInfo;

// get glyph info for a codepoint (advance, bearings — cached)
GlyphInfo font_get_glyph(FontHandle* handle, uint32_t codepoint);

// get the glyph index for a codepoint (0 if not present)
uint32_t font_get_glyph_index(FontHandle* handle, uint32_t codepoint);

// get kerning between two codepoints (returns 0 if no kerning)
float font_get_kerning(FontHandle* handle, uint32_t left, uint32_t right);

// get kerning between two glyph indices (returns 0 if no kerning).
// use this when you already have glyph indices from font_get_glyph_index().
float font_get_kerning_by_index(FontHandle* handle, uint32_t left_index, uint32_t right_index);

// ============================================================================
// Glyph Rasterization — bitmap rendering for display
// ============================================================================

typedef enum GlyphRenderMode {
    GLYPH_RENDER_NORMAL,        // 8-bit anti-aliased
    GLYPH_RENDER_LCD,           // LCD subpixel (3x width)
    GLYPH_RENDER_MONO,          // 1-bit (for hit testing)
    GLYPH_RENDER_SDF,           // signed distance field
} GlyphRenderMode;

typedef struct GlyphBitmap {
    uint8_t* buffer;            // pixel data (owned by cache, valid until eviction)
    int      width, height;     // bitmap dimensions
    int      pitch;             // bytes per row
    int      bearing_x;         // left offset from pen position
    int      bearing_y;         // top offset from baseline
    GlyphRenderMode mode;
} GlyphBitmap;

// render a glyph to bitmap (result is cached; pointer valid until cache eviction)
const GlyphBitmap* font_render_glyph(FontHandle* handle, uint32_t codepoint,
                                      GlyphRenderMode mode);

// ============================================================================
// Text Measurement — convenience for layout
// ============================================================================

typedef struct TextExtents {
    float width;                // total advance width
    float height;               // ascender - descender
    int   glyph_count;          // number of glyphs shaped
} TextExtents;

// measure a UTF-8 text string (applies kerning, handles multi-byte)
TextExtents font_measure_text(FontHandle* handle, const char* text, int byte_len);

// measure width of a single codepoint (convenience wrapper)
float font_measure_char(FontHandle* handle, uint32_t codepoint);

// ============================================================================
// Font Fallback — for codepoints not covered by the primary font
// ============================================================================

// find a font that supports a specific codepoint, given a style hint.
// searches registered font face descriptors first, then system fonts.
// returns NULL if no font covers this codepoint.
FontHandle* font_resolve_for_codepoint(FontContext* ctx, const FontStyleDesc* style,
                                        uint32_t codepoint);

// check whether a handle supports a codepoint (without loading glyph)
bool font_has_codepoint(FontHandle* handle, uint32_t codepoint);

// ============================================================================
// Font Handle Accessors — for callers migrating from FT_Face
// ============================================================================

// get the font family name (e.g., "Arial", "Times New Roman")
const char* font_handle_get_family_name(FontHandle* handle);

// get the requested CSS pixel size
float font_handle_get_size_px(FontHandle* handle);

// get the physical pixel size (CSS px * pixel_ratio)
float font_handle_get_physical_size_px(FontHandle* handle);

// get the x-height to em-size ratio (for CSS 'ex' unit resolution)
float font_get_x_height_ratio(FontHandle* handle);

// ============================================================================
// Line Height Computation — Chrome-compatible algorithm
// ============================================================================

// Calculate normal CSS line-height following Chrome/Blink's algorithm:
//   1. CoreText metrics on macOS (with 15% hack for Times/Helvetica/Courier)
//   2. OS/2 USE_TYPO_METRICS path
//   3. HHEA fallback with font-unit scaling and individual rounding
// Returns the line-height in CSS pixels.
float font_calc_normal_line_height(FontHandle* handle);

// Get the font cell height for text rect height computation.
// Matches browser's Range.getClientRects() which uses font metrics, not CSS line-height.
// For Apple's classic fonts (Times/Helvetica/Courier), uses CoreText with 15% hack.
// For all other fonts, returns FreeType metrics.height (ascent + descent).
float font_get_cell_height(FontHandle* handle);

// ============================================================================
// Font Face Management — register, query, and load font face descriptors
// ============================================================================
//
// Separation of concerns:
//   - CSS @font-face PARSING lives in Radiant (radiant/font_face.cpp).
//     Radiant walks the stylesheet, extracts family/weight/style/src, and
//     calls the registration API below.
//   - Font face MANAGEMENT lives here: storing descriptors, matching them
//     by criteria, loading the actual font data, and caching loaded faces.
//

// descriptor for a single font face source (path or data URI + format hint)
typedef struct FontFaceSource {
    const char* path;           // local file path or data URI
    const char* format;         // "truetype", "opentype", "woff", "woff2", or NULL
} FontFaceSource;

// descriptor for a registered font face (one @font-face rule = one descriptor)
typedef struct FontFaceDesc {
    const char*   family;       // font-family value
    FontWeight    weight;       // font-weight (100–900)
    FontSlant     slant;        // font-style (normal/italic/oblique)
    FontFaceSource*  sources;      // ordered src list (tried in order)
    int           source_count;
} FontFaceDesc;

// register a font face descriptor (called by Radiant after parsing @font-face).
// the descriptor is copied into the font module's pool/arena.
// returns true on success.
bool font_face_register(FontContext* ctx, const FontFaceDesc* desc);

// find the best-matching registered font face for a given style.
// returns NULL if no registered face matches. does NOT load the font;
// use font_face_load() on the result to get a FontHandle.
const FontFaceDesc* font_face_find(FontContext* ctx, const FontStyleDesc* style);

// list all registered descriptors for a family name.
// returns count; fills out up to max_out entries.
int font_face_list(FontContext* ctx, const char* family,
                   const FontFaceDesc** out, int max_out);

// load a font from a registered descriptor (tries sources in order,
// handles all formats: TTF/OTF/TTC/WOFF/WOFF2/data URI).
// returns a cached FontHandle if already loaded.
FontHandle* font_face_load(FontContext* ctx, const FontFaceDesc* desc,
                            float size_px);

// remove all registered descriptors (e.g., on document unload)
void font_face_clear(FontContext* ctx);

// ============================================================================
// Direct Font Loading — for non-CSS use cases (PDF, CLI, tests)
// ============================================================================

// load a font from a local file path (any format: TTF/OTF/TTC/WOFF/WOFF2).
FontHandle* font_load_from_file(FontContext* ctx, const char* path,
                                const FontStyleDesc* style);

// load a font from a data URI (base64-encoded, possibly WOFF2-compressed).
FontHandle* font_load_from_data_uri(FontContext* ctx, const char* data_uri,
                                     const FontStyleDesc* style);

// load a font from raw bytes in memory (any format, auto-detected).
// the buffer is copied into the arena; caller can free their copy.
FontHandle* font_load_from_memory(FontContext* ctx, const uint8_t* data,
                                   size_t len, const FontStyleDesc* style);

// ============================================================================
// Font Database — system font discovery and matching
// ============================================================================

// add a custom directory to scan for fonts
void font_context_add_scan_directory(FontContext* ctx, const char* directory);

// get the number of discovered fonts / families
int font_get_font_count(FontContext* ctx);
int font_get_family_count(FontContext* ctx);

// check whether a font family name exists in the system font database.
// returns true if at least one font with that family name is installed.
bool font_family_exists(FontContext* ctx, const char* family);

// find the file path of the best Regular-weight font for a given family name.
// returns a mem_strdup'd path (caller must mem_free), or NULL if not found.
// prefers weight=400, non-italic, non-.ttc files — suitable for ThorVG loading.
char* font_find_path(FontContext* ctx, const char* family);

// convert FontSlant enum to string ("normal", "italic", "oblique")
const char* font_slant_to_string(FontSlant slant);

// result of a font database best-match query
typedef struct FontMatchResult {
    const char* file_path;          // font file path (valid until database is freed)
    const char* family_name;        // matched font family name
    int         weight;             // matched font weight (100-900)
    FontSlant   style;              // matched font style
    int         face_index;         // face index for TTC collections (0 for single-face)
    float       match_score;        // 0.0-1.0 match quality
    bool        found;              // true if a match was found
} FontMatchResult;

// find the best-matching system font for given criteria.
// score_threshold: minimum match_score to accept (use 0.5 for typical CSS matching).
FontMatchResult font_find_best_match(FontContext* ctx,
                                      const char* family,
                                      int weight,
                                      FontSlant style);

// ============================================================================
// Cache Control
// ============================================================================

// persist font database to disk (call on shutdown or periodically)
bool font_cache_save(FontContext* ctx);

// evict least-recently-used entries from glyph cache
void font_cache_trim(FontContext* ctx);

// get cache statistics for diagnostics
typedef struct FontCacheStats {
    int    face_count;             // currently loaded faces
    int    glyph_cache_count;      // cached glyph entries
    int    glyph_cache_hit_rate;   // percentage (0-100)
    size_t memory_usage_bytes;     // approximate memory footprint
    int    database_font_count;    // fonts in database
    int    database_family_count;  // font families
} FontCacheStats;

FontCacheStats font_get_cache_stats(FontContext* ctx);

// ============================================================================
// Internal access — for Radiant integration during migration
// ============================================================================

// get the underlying FreeType library handle (void* to avoid FT_Library in API).
// only use this for code that still needs direct FreeType access during migration.
void* font_context_get_ft_library(FontContext* ctx);

// get the underlying FT_Face from a FontHandle (void* to avoid FT_Face in API).
// only use this for code that still needs direct FreeType access during migration.
void* font_handle_get_ft_face(FontHandle* handle);

// wrap an externally-loaded FT_Face into a FontHandle (borrowed — does NOT own the face).
// the returned handle provides FontMetrics, accessors, etc., but will NOT call FT_Done_Face
// on release. caller is responsible for the FT_Face lifetime.
FontHandle* font_handle_wrap(FontContext* ctx, void* ft_face, float size_px);

// get the font database from context (for backward compat during migration).
// returns the internal FontDatabase pointer.
struct FontDatabase* font_context_get_database(FontContext* ctx);

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_FONT_H
