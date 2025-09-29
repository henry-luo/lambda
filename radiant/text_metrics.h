#pragma once

#include "font_face.h"
#include "view.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Advanced character metrics for Unicode support
typedef struct AdvancedCharacterMetrics {
    uint32_t codepoint;         // Unicode codepoint
    int advance_x, advance_y;   // Character advance
    int bearing_x, bearing_y;   // Glyph bearing
    int width, height;          // Glyph dimensions
    
    // Advanced positioning
    int left_side_bearing;      // Left side bearing
    int right_side_bearing;     // Right side bearing
    int top_side_bearing;       // Top side bearing
    int bottom_side_bearing;    // Bottom side bearing
    
    // Baseline information
    int baseline_offset;        // Offset from baseline
    int ascender_offset;        // Offset from ascender line
    int descender_offset;       // Offset from descender line
    
    // High-DPI support
    float pixel_ratio;          // Display pixel ratio used
    bool scaled_for_display;    // Metrics scaled for high-DPI
    
    // Caching information
    bool is_cached;             // Cached flag
    uint64_t cache_timestamp;   // When cached (for invalidation)
} AdvancedCharacterMetrics;

// Enhanced text metrics for line layout
typedef struct TextLineMetrics {
    // Line dimensions
    int line_width;             // Total line width
    int line_height;            // Total line height
    int baseline_y;             // Baseline Y position
    
    // Font metrics for the line
    int max_ascender;           // Maximum ascender in line
    int max_descender;          // Maximum descender in line
    int max_line_gap;           // Maximum line gap in line
    
    // Advanced metrics
    int x_height_max;           // Maximum x-height in line
    int cap_height_max;         // Maximum cap-height in line
    int dominant_baseline;      // Dominant baseline for alignment
    
    // Character count and positioning
    int character_count;        // Number of characters in line
    int* character_positions;   // X positions of each character
    AdvancedCharacterMetrics* char_metrics; // Metrics for each character
    
    // Line breaking information
    int break_opportunities;    // Number of break opportunities
    int* break_positions;       // Positions where line can break
    float line_quality_score;   // Quality score for line breaking
} TextLineMetrics;

// Unicode text rendering context
typedef struct UnicodeRenderContext {
    // Font information
    EnhancedFontBox* primary_font;      // Primary font for rendering
    FontFallbackChain* fallback_chain;  // Font fallback chain
    
    // Rendering parameters
    float pixel_ratio;                  // Display pixel ratio
    bool subpixel_positioning;          // Enable subpixel positioning
    bool font_hinting;                  // Enable font hinting
    
    // Text properties
    PropValue text_direction;           // LTR, RTL, auto
    PropValue writing_mode;             // horizontal-tb, vertical-rl, etc.
    char* language;                     // Language code for text shaping
    
    // Performance caching
    struct hashmap* glyph_cache;        // Glyph rendering cache
    struct hashmap* metrics_cache;      // Character metrics cache
    bool cache_enabled;                 // Enable caching
    
    // Debug and logging
    bool debug_rendering;               // Enable debug output
    int cache_hits;                     // Cache hit counter
    int cache_misses;                   // Cache miss counter
} UnicodeRenderContext;

// Advanced glyph rendering information
typedef struct AdvancedGlyphRenderInfo {
    uint32_t codepoint;                 // Unicode codepoint
    FT_GlyphSlot glyph;                 // FreeType glyph slot
    AdvancedCharacterMetrics metrics;   // Advanced character metrics
    
    // Rendering state
    FT_Face font_face;                  // Font face used for rendering
    bool uses_fallback;                 // Uses fallback font
    char* fallback_font_name;           // Name of fallback font used
    
    // Positioning information
    float subpixel_x;                   // Subpixel X positioning
    float subpixel_y;                   // Subpixel Y positioning
    int pixel_x;                        // Final pixel X position
    int pixel_y;                        // Final pixel Y position
    
    // Quality information
    bool hinting_applied;               // Font hinting was applied
    bool antialiasing_enabled;          // Antialiasing enabled
    int rendering_quality;              // Rendering quality level (1-3)
} AdvancedGlyphRenderInfo;

// Function declarations

// Enhanced font metrics computation
void compute_advanced_font_metrics(EnhancedFontBox* fbox);
void compute_opentype_metrics(EnhancedFontBox* fbox);
void compute_baseline_metrics(EnhancedFontBox* fbox);

// Character metrics functions
AdvancedCharacterMetrics* get_advanced_character_metrics(EnhancedFontBox* fbox, uint32_t codepoint);
void cache_advanced_character_metrics(EnhancedFontBox* fbox, uint32_t codepoint, AdvancedCharacterMetrics* metrics);
bool is_character_metrics_cached(EnhancedFontBox* fbox, uint32_t codepoint);

// Unicode character rendering
AdvancedGlyphRenderInfo* render_unicode_character(UnicodeRenderContext* ctx, uint32_t codepoint);
FT_Face find_font_for_codepoint(UnicodeRenderContext* ctx, uint32_t codepoint);
bool load_unicode_glyph(FT_Face face, uint32_t codepoint, FT_GlyphSlot* glyph);

// Text line metrics computation
TextLineMetrics* compute_text_line_metrics(UnicodeRenderContext* ctx, const char* text, int length);
void compute_line_baseline_metrics(TextLineMetrics* line_metrics);
void compute_character_positions(TextLineMetrics* line_metrics, UnicodeRenderContext* ctx);

// Advanced baseline calculations
int calculate_dominant_baseline(TextLineMetrics* line_metrics);
int calculate_mixed_font_baseline(EnhancedFontBox** fonts, int font_count);
int calculate_vertical_alignment_offset(AdvancedCharacterMetrics* char_metrics, PropValue vertical_align);

// Subpixel positioning and rendering
void apply_subpixel_positioning(AdvancedGlyphRenderInfo* glyph_info, float subpixel_x, float subpixel_y);
void optimize_glyph_positioning(AdvancedGlyphRenderInfo* glyph_info);
void apply_font_hinting(AdvancedGlyphRenderInfo* glyph_info, bool enable_hinting);

// Unicode text width calculation
int calculate_unicode_text_width(UnicodeRenderContext* ctx, const char* text, int length);
int calculate_character_advance(UnicodeRenderContext* ctx, uint32_t codepoint);
float calculate_kerning_adjustment(UnicodeRenderContext* ctx, uint32_t left_char, uint32_t right_char);

// Context management
UnicodeRenderContext* create_unicode_render_context(UiContext* uicon, EnhancedFontBox* primary_font);
void destroy_unicode_render_context(UnicodeRenderContext* ctx);
void reset_unicode_render_context(UnicodeRenderContext* ctx);

// Performance and debugging
void log_character_rendering(uint32_t codepoint, AdvancedGlyphRenderInfo* glyph_info);
void log_font_fallback_usage(const char* requested_font, const char* used_font, uint32_t codepoint);
void log_rendering_performance(UnicodeRenderContext* ctx);

// Integration with existing layout system
void enhance_existing_font_box(FontBox* existing_fbox, EnhancedFontBox* enhanced_fbox);
void integrate_advanced_metrics_with_layout(LayoutContext* lycon, TextLineMetrics* line_metrics);
void update_layout_context_with_unicode_support(LayoutContext* lycon, UnicodeRenderContext* unicode_ctx);

#ifdef __cplusplus
}
#endif
