#pragma once

#include "view.hpp"
#include "../lib/log.h"
#include <ft2build.h>
#include FT_FREETYPE_H

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct LayoutContext;

// Font face descriptor for @font-face support
typedef struct FontFaceDescriptor {
    char* family_name;           // font-family value
    char* src_local_path;        // local font file path (no web URLs)
    char* src_local_name;        // src: local() font name value
    CssEnum font_style;        // normal, italic, oblique
    CssEnum font_weight;       // 100-900, normal, bold
    CssEnum font_display;      // auto, block, swap, fallback, optional
    bool is_loaded;              // loading state
    FT_Face loaded_face;         // cached FT_Face when loaded

    // Performance optimizations
    struct hashmap* char_width_cache;  // Unicode codepoint -> width cache
    bool metrics_computed;             // Font metrics computed flag
} FontFaceDescriptor;

// Enhanced font metrics for better typography
typedef struct EnhancedFontMetrics {
    // Core metrics
    float ascender, descender, height;
    float line_gap;               // Additional line spacing

    // Advanced metrics for better typography
    float x_height;               // Height of lowercase 'x'
    float cap_height;             // Height of uppercase letters
    float baseline_offset;        // Baseline position adjustment

    // OpenType metrics for better compatibility
    float typo_ascender, typo_descender, typo_line_gap;
    float win_ascent, win_descent;
    float hhea_ascender, hhea_descender, hhea_line_gap;

    // Performance optimizations
    bool metrics_computed;      // Metrics computation flag
    struct hashmap* char_metrics_cache;  // Character metrics cache
} EnhancedFontMetrics;

// Enhanced FontBox extending existing structure
typedef struct EnhancedFontBox {
    FontProp style;
    FT_Face face;
    float space_width;
    int current_font_size;

    // Enhanced metrics
    EnhancedFontMetrics metrics;
    bool metrics_computed;

    // Character caching for performance (Unicode support)
    struct hashmap* char_width_cache;    // codepoint -> width
    struct hashmap* char_bearing_cache;  // codepoint -> bearing
    bool cache_enabled;                  // Enable character caching

    // High-DPI display support (preserve existing pixel_ratio handling)
    float pixel_ratio;                   // Current display pixel ratio
    bool high_dpi_aware;                 // High-DPI rendering enabled
} EnhancedFontBox;

// Font matching criteria
typedef struct FontMatchCriteria {
    const char* family_name;
    CssEnum weight;           // 100-900
    CssEnum style;           // normal, italic, oblique
    int size;                  // font size in pixels
    uint32_t required_codepoint; // Specific codepoint support needed (optional)
} FontMatchCriteria;

// Font matching result
typedef struct FontMatchResult {
    FT_Face face;
    FontFaceDescriptor* descriptor; // NULL for system fonts
    float match_score;              // 0.0-1.0 quality score
    bool is_exact_match;
    bool requires_synthesis;        // needs bold/italic synthesis
    bool supports_codepoint;        // Supports required codepoint
} FontMatchResult;

// Font fallback chain
typedef struct FontFallbackChain {
    char** family_names;        // Ordered list of font families
    int family_count;
    FontFaceDescriptor** web_fonts;  // Associated @font-face fonts
    char** system_fonts;        // System font fallbacks
    char* generic_family;       // serif, sans-serif, monospace, etc.

    // Caching for performance
    struct hashmap* codepoint_font_cache;  // codepoint -> FT_Face cache
    bool cache_enabled;                    // Enable codepoint caching
} FontFallbackChain;

// Character metrics for Unicode support
typedef struct CharacterMetrics {
    uint32_t codepoint;         // Unicode codepoint
    int advance_x, advance_y;   // Character advance
    int bearing_x, bearing_y;   // Glyph bearing
    int width, height;          // Glyph dimensions
    bool is_cached;             // Cached flag

    // High-DPI display support (preserve existing pixel_ratio handling)
    float pixel_ratio;          // Display pixel ratio used for these metrics
    bool scaled_for_display;    // Metrics scaled for high-DPI display
} CharacterMetrics;

// Enhanced glyph rendering info
typedef struct GlyphRenderInfo {
    uint32_t codepoint;
    FT_GlyphSlot glyph;
    CharacterMetrics metrics;

    // Rendering state
    bool needs_fallback;        // Requires font fallback
    FT_Face fallback_face;      // Fallback font face if needed
} GlyphRenderInfo;

// Text flow logging categories
extern log_category_t* font_log;
extern log_category_t* text_log;
extern log_category_t* layout_log;

// Function declarations

// Logging initialization
void init_text_flow_logging(void);
void setup_text_flow_log_categories(void);

// CSS @font-face parsing integration
void parse_font_face_rule(struct LayoutContext* lycon, void* rule);
FontFaceDescriptor* create_font_face_descriptor(struct LayoutContext* lycon);
void register_font_face(UiContext* uicon, FontFaceDescriptor* descriptor);

// Font loading with @font-face support (local fonts only)
FT_Face load_font_with_descriptors(UiContext* uicon, const char* family_name,
                                   FontProp* style, bool* is_fallback);
FT_Face load_local_font_file(UiContext* uicon, const char* font_path, FontProp* style);
bool resolve_font_path_from_descriptor(FontFaceDescriptor* descriptor, char** resolved_path);

// Character width caching for performance
void cache_character_width(FontFaceDescriptor* descriptor, uint32_t codepoint, int width);
int get_cached_char_width(FontFaceDescriptor* descriptor, uint32_t codepoint);

// Structured logging for font operations (replace printf)
void log_font_loading_attempt(const char* family_name, const char* path);
void log_font_loading_result(const char* family_name, bool success, const char* error);
void log_font_cache_hit(const char* family_name, int font_size);
void log_font_fallback_triggered(const char* requested, const char* fallback);

// Enhanced font matching
FontMatchResult find_best_font_match(UiContext* uicon, FontMatchCriteria* criteria);
float calculate_font_match_score(FontFaceDescriptor* descriptor, FontMatchCriteria* criteria);
FT_Face synthesize_font_style(FT_Face base_face, CssEnum target_style, CssEnum target_weight);

// Font fallback chain management
FontFallbackChain* build_fallback_chain(UiContext* uicon, const char* css_font_family);
FT_Face resolve_font_for_codepoint(FontFallbackChain* chain, uint32_t codepoint, FontProp* style);
bool font_supports_codepoint(FT_Face face, uint32_t codepoint);
void cache_codepoint_font_mapping(FontFallbackChain* chain, uint32_t codepoint, FT_Face face);

// Enhanced metrics functions
void compute_enhanced_font_metrics(EnhancedFontBox* fbox);
int calculate_line_height_from_css(EnhancedFontBox* fbox, CssEnum line_height_css);
int get_baseline_offset(EnhancedFontBox* fbox);
int get_char_width_cached(EnhancedFontBox* fbox, uint32_t codepoint);
void cache_character_metrics(EnhancedFontBox* fbox, uint32_t codepoint);

// High-DPI display support functions (preserve existing pixel_ratio handling)
void apply_pixel_ratio_to_font_metrics(EnhancedFontBox* fbox, float pixel_ratio);
int scale_font_size_for_display(int base_size, float pixel_ratio);
void ensure_pixel_ratio_compatibility(EnhancedFontBox* fbox);

// Enhanced glyph functions extending existing load_glyph
GlyphRenderInfo* load_glyph_enhanced(UiContext* uicon, EnhancedFontBox* fbox, uint32_t codepoint);
CharacterMetrics* get_character_metrics(EnhancedFontBox* fbox, uint32_t codepoint);
int calculate_text_width_enhanced(EnhancedFontBox* fbox, const char* text, int length);
void cache_glyph_metrics(EnhancedFontBox* fbox, uint32_t codepoint, CharacterMetrics* metrics);

// High-DPI display support for character rendering (preserve existing pixel_ratio)
void scale_character_metrics_for_display(CharacterMetrics* metrics, float pixel_ratio);
GlyphRenderInfo* load_glyph_with_pixel_ratio(UiContext* uicon, EnhancedFontBox* fbox,
                                            uint32_t codepoint, float pixel_ratio);

// Enhanced font system integration
void setup_font_enhanced(UiContext* uicon, EnhancedFontBox* fbox,
                        const char* font_name, FontProp* fprop);
FT_Face load_font_with_fallback(UiContext* uicon, const char* family_name,
                                FontProp* style, FontFallbackChain* fallback);

#ifdef __cplusplus
}
#endif
