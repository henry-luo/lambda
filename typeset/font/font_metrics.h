#ifndef FONT_METRICS_H
#define FONT_METRICS_H

#include "font_manager.h"
#include "../view/view_tree.h"
#include <stdbool.h>
#include <stdint.h>

// Forward declarations
typedef struct FontMetrics FontMetrics;
typedef struct GlyphMetrics GlyphMetrics;
typedef struct TextMeasurement TextMeasurement;

// Font metrics structure - contains all measurement data for a font
struct FontMetrics {
    // Basic font metrics (in font units, typically 1000 or 2048 per em)
    int units_per_em;           // Font units per em square
    int ascent;                 // Ascent in font units
    int descent;                // Descent in font units (typically negative)
    int line_height;            // Default line height in font units
    
    // Scaled metrics (in points, scaled for font size)
    double scaled_ascent;       // Ascent scaled to font size
    double scaled_descent;      // Descent scaled to font size (positive value)
    double scaled_line_height;  // Line height scaled to font size
    double scaled_x_height;     // x-height scaled to font size
    double scaled_cap_height;   // Capital letter height scaled to font size
    
    // Horizontal metrics
    double max_advance_width;   // Maximum advance width
    double average_char_width;  // Average character width
    double space_width;         // Width of space character
    double em_width;            // Width of 'M' character
    double en_width;            // Width of 'N' character
    
    // Mathematical metrics
    double math_axis_height;    // Mathematical axis height
    double superscript_offset;  // Superscript vertical offset
    double subscript_offset;    // Subscript vertical offset
    double superscript_scale;   // Superscript scale factor
    double subscript_scale;     // Subscript scale factor
    
    // Layout metrics
    double baseline_to_baseline; // Distance between baselines
    double leading;             // Additional space between lines
    double em_size;             // Em size (typically equals font size)
    
    // Font properties
    bool is_monospace;          // Whether font is monospace
    bool has_kerning;           // Whether font has kerning tables
    bool has_ligatures;         // Whether font has ligature tables
    bool supports_math;         // Whether font supports mathematical typesetting
    
    // Unicode coverage
    uint32_t* supported_ranges; // Array of Unicode ranges (start, end pairs)
    int range_count;            // Number of Unicode ranges
    
    // Reference to source font
    ViewFont* source_font;      // Font these metrics belong to
    double font_size;           // Font size these metrics are calculated for
    
    // Cache validity
    bool is_valid;              // Whether metrics are valid
    uint64_t cache_timestamp;   // When metrics were calculated
};

// Individual glyph metrics
struct GlyphMetrics {
    uint32_t glyph_id;          // Glyph ID in font
    uint32_t codepoint;         // Unicode codepoint
    
    // Horizontal metrics
    double advance_width;       // Horizontal advance
    double left_side_bearing;   // Left side bearing
    double right_side_bearing;  // Right side bearing
    
    // Vertical metrics
    double advance_height;      // Vertical advance (for vertical text)
    double top_side_bearing;    // Top side bearing
    double bottom_side_bearing; // Bottom side bearing
    
    // Bounding box
    ViewRect bounding_box;      // Glyph bounding box
    
    // Special properties
    bool is_whitespace;         // Whether glyph represents whitespace
    bool is_line_break;         // Whether glyph can break lines
    bool is_combining;          // Whether glyph is combining character
};

// Text measurement result
struct TextMeasurement {
    // Overall measurements
    double total_width;         // Total width of text
    double total_height;        // Total height of text
    double ascent;              // Maximum ascent
    double descent;             // Maximum descent
    double leading;             // Line leading
    
    // Individual glyph information
    GlyphMetrics* glyph_metrics; // Array of glyph metrics
    ViewPoint* glyph_positions; // Array of glyph positions
    int glyph_count;            // Number of glyphs
    
    // Line break information
    int* line_breaks;           // Array of line break positions
    int line_break_count;       // Number of line breaks
    double* line_widths;        // Width of each line
    
    // Text properties
    ViewFont* font;             // Font used for measurement
    double font_size;           // Font size used
    int text_length;            // Length of original text in bytes
    char* text;                 // Original text (UTF-8)
    
    // Measurement flags
    bool includes_kerning;      // Whether kerning was applied
    bool includes_ligatures;    // Whether ligatures were applied
    bool is_shaped;             // Whether text was shaped (complex scripts)
};

// Font metrics creation and destruction
FontMetrics* font_metrics_create(ViewFont* font);
FontMetrics* font_metrics_create_for_size(ViewFont* font, double size);
void font_metrics_destroy(FontMetrics* metrics);

// Font metrics calculation
FontMetrics* font_calculate_metrics(ViewFont* font);
void font_metrics_scale_for_size(FontMetrics* metrics, double size);
bool font_metrics_update_if_needed(FontMetrics* metrics);

// Font metrics access
FontMetrics* font_get_metrics(ViewFont* font);
FontMetrics* font_get_metrics_for_size(ViewFont* font, double size);

// Basic measurements
double font_get_ascent(ViewFont* font);
double font_get_descent(ViewFont* font);
double font_get_line_height(ViewFont* font);
double font_get_x_height(ViewFont* font);
double font_get_cap_height(ViewFont* font);
double font_get_em_size(ViewFont* font);

// Character measurements
double font_measure_char_width(ViewFont* font, uint32_t codepoint);
double font_measure_space_width(ViewFont* font);
double font_measure_em_width(ViewFont* font);
double font_measure_en_width(ViewFont* font);

// Text measurement
TextMeasurement* font_measure_text(ViewFont* font, const char* text, int length);
TextMeasurement* font_measure_text_with_options(ViewFont* font, const char* text, int length,
                                               bool apply_kerning, bool apply_ligatures);
void text_measurement_destroy(TextMeasurement* measurement);

// Simple text width measurement (for quick calculations)
double font_measure_text_width(ViewFont* font, const char* text, int length);
double font_measure_text_width_fast(ViewFont* font, const char* text, int length);

// Glyph metrics
GlyphMetrics* font_get_glyph_metrics(ViewFont* font, uint32_t glyph_id);
GlyphMetrics* font_get_codepoint_metrics(ViewFont* font, uint32_t codepoint);
void glyph_metrics_destroy(GlyphMetrics* glyph);

// Glyph lookup
uint32_t font_get_glyph_id(ViewFont* font, uint32_t codepoint);
bool font_has_glyph(ViewFont* font, uint32_t codepoint);
uint32_t font_get_fallback_glyph_id(ViewFont* font);

// Kerning
double font_get_kerning(ViewFont* font, uint32_t left_glyph, uint32_t right_glyph);
bool font_has_kerning_pair(ViewFont* font, uint32_t left_glyph, uint32_t right_glyph);

// Baseline calculations
double font_get_alphabetic_baseline(ViewFont* font);
double font_get_ideographic_baseline(ViewFont* font);
double font_get_hanging_baseline(ViewFont* font);
double font_get_mathematical_baseline(ViewFont* font);

// Mathematical typography metrics
double font_get_math_axis_height(ViewFont* font);
double font_get_superscript_offset(ViewFont* font);
double font_get_subscript_offset(ViewFont* font);
double font_get_superscript_scale(ViewFont* font);
double font_get_subscript_scale(ViewFont* font);

// Line metrics calculations
typedef struct LineMetrics {
    double ascent;              // Maximum ascent in line
    double descent;             // Maximum descent in line
    double line_height;         // Total line height
    double baseline_offset;     // Baseline position from top
    double leading;             // Additional leading space
    ViewFont** fonts_in_line;   // Fonts used in line
    int font_count;             // Number of different fonts
} LineMetrics;

LineMetrics* calculate_line_metrics(ViewFont** fonts, double* font_sizes, int font_count);
LineMetrics* calculate_line_metrics_from_text_runs(ViewNode** text_runs, int run_count);
void line_metrics_destroy(LineMetrics* metrics);

// Font feature detection
bool font_supports_feature(ViewFont* font, const char* feature_tag);
bool font_supports_script(ViewFont* font, const char* script_tag);
bool font_supports_language(ViewFont* font, const char* language_tag);

// Unicode support
bool font_supports_codepoint(ViewFont* font, uint32_t codepoint);
bool font_supports_unicode_range(ViewFont* font, uint32_t start, uint32_t end);
uint32_t* font_get_supported_codepoints(ViewFont* font, int* count);

// Font classification
bool font_is_monospace(ViewFont* font);
bool font_is_serif(ViewFont* font);
bool font_is_sans_serif(ViewFont* font);
bool font_is_script(ViewFont* font);
bool font_is_decorative(ViewFont* font);
bool font_supports_mathematics(ViewFont* font);

// Performance optimization
typedef struct FontMetricsCache {
    FontMetrics** cached_metrics; // Array of cached metrics
    int cache_size;             // Current cache size
    int max_cache_size;         // Maximum cache size
    uint64_t hits;              // Cache hits
    uint64_t misses;            // Cache misses
} FontMetricsCache;

FontMetricsCache* font_metrics_cache_create(int max_size);
void font_metrics_cache_destroy(FontMetricsCache* cache);
FontMetrics* font_metrics_cache_get(FontMetricsCache* cache, ViewFont* font, double size);
void font_metrics_cache_put(FontMetricsCache* cache, FontMetrics* metrics);

// Measurement utilities
double points_to_pixels(double points, double dpi);
double pixels_to_points(double pixels, double dpi);
double font_units_to_points(int font_units, int units_per_em, double font_size);
int points_to_font_units(double points, int units_per_em, double font_size);

// Text shaping integration (forward declaration for text_shaper.h)
struct TextShapeResult;
TextMeasurement* text_measurement_from_shape_result(struct TextShapeResult* shape_result);

// Debugging and validation
void font_metrics_print(FontMetrics* metrics);
void text_measurement_print(TextMeasurement* measurement);
bool font_metrics_validate(FontMetrics* metrics);
void font_metrics_dump_to_file(FontMetrics* metrics, const char* filename);

// Lambda integration
Item fn_font_get_metrics(Context* ctx, Item* args, int arg_count);
Item fn_font_measure_text(Context* ctx, Item* args, int arg_count);
Item fn_font_get_glyph_metrics(Context* ctx, Item* args, int arg_count);

#endif // FONT_METRICS_H
