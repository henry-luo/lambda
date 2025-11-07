#pragma once

#include "view.hpp"
#include "text_metrics.h"
#include "../lib/log.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct LayoutContext;
typedef struct LayoutContext LayoutContext;

// OpenType feature tags (4-byte identifiers)
typedef uint32_t OpenTypeFeatureTag;

// Common OpenType feature tags
#define OT_FEATURE_KERN 0x6B65726E  // 'kern' - Kerning
#define OT_FEATURE_LIGA 0x6C696761  // 'liga' - Standard Ligatures
#define OT_FEATURE_DLIG 0x646C6967  // 'dlig' - Discretionary Ligatures
#define OT_FEATURE_CLIG 0x636C6967  // 'clig' - Contextual Ligatures
#define OT_FEATURE_HLIG 0x686C6967  // 'hlig' - Historical Ligatures
#define OT_FEATURE_CALT 0x63616C74  // 'calt' - Contextual Alternates
#define OT_FEATURE_SWSH 0x73777368  // 'swsh' - Swash
#define OT_FEATURE_SMCP 0x736D6370  // 'smcp' - Small Capitals
#define OT_FEATURE_C2SC 0x63327363  // 'c2sc' - Capitals to Small Capitals
#define OT_FEATURE_ONUM 0x6F6E756D  // 'onum' - Oldstyle Figures
#define OT_FEATURE_LNUM 0x6C6E756D  // 'lnum' - Lining Figures
#define OT_FEATURE_TNUM 0x746E756D  // 'tnum' - Tabular Figures
#define OT_FEATURE_PNUM 0x706E756D  // 'pnum' - Proportional Figures
#define OT_FEATURE_FRAC 0x66726163  // 'frac' - Fractions
#define OT_FEATURE_SUPS 0x73757073  // 'sups' - Superscript
#define OT_FEATURE_SUBS 0x73756273  // 'subs' - Subscript

// OpenType feature state
typedef enum OpenTypeFeatureState {
    OT_FEATURE_OFF = 0,     // Feature disabled
    OT_FEATURE_ON = 1,      // Feature enabled
    OT_FEATURE_AUTO = 2     // Feature auto-enabled based on context
} OpenTypeFeatureState;

// OpenType feature configuration
typedef struct OpenTypeFeature {
    OpenTypeFeatureTag tag;         // Feature tag (4 bytes)
    OpenTypeFeatureState state;     // Feature state
    uint32_t parameter;             // Optional parameter value
    bool is_supported;              // Whether font supports this feature
    char name[32];                  // Human-readable name
    char description[128];          // Feature description
} OpenTypeFeature;

// Ligature information
typedef struct LigatureInfo {
    uint32_t* input_codepoints;     // Input codepoints that form ligature
    int input_count;                // Number of input codepoints
    uint32_t output_codepoint;      // Resulting ligature codepoint
    FT_UInt glyph_index;           // Glyph index in font
    int advance_width;              // Ligature advance width
    bool is_standard;               // Standard ligature (fi, fl, etc.)
    bool is_discretionary;          // Discretionary ligature
    bool is_contextual;             // Contextual ligature
    char ligature_name[64];         // Ligature name (e.g., "fi_ligature")
} LigatureInfo;

// Kerning pair information
typedef struct KerningPair {
    uint32_t left_codepoint;        // Left character codepoint
    uint32_t right_codepoint;       // Right character codepoint
    FT_UInt left_glyph_index;      // Left glyph index
    FT_UInt right_glyph_index;     // Right glyph index
    int kerning_value;              // Kerning adjustment (in font units)
    int scaled_kerning;             // Kerning scaled for current size
    bool is_horizontal;             // Horizontal kerning
    bool is_vertical;               // Vertical kerning
} KerningPair;

// Glyph substitution information
typedef struct GlyphSubstitution {
    uint32_t input_codepoint;       // Original codepoint
    uint32_t output_codepoint;      // Substituted codepoint
    FT_UInt input_glyph_index;     // Original glyph index
    FT_UInt output_glyph_index;    // Substituted glyph index
    OpenTypeFeatureTag feature;     // Feature that triggered substitution
    char substitution_name[64];     // Substitution name
} GlyphSubstitution;

// OpenType font capabilities
typedef struct OpenTypeFontInfo {
    FT_Face face;                   // FreeType face
    bool has_gpos_table;            // Has GPOS table (positioning)
    bool has_gsub_table;            // Has GSUB table (substitution)
    bool has_kern_table;            // Has legacy kern table

    // Supported features
    OpenTypeFeature* features;      // Array of supported features
    int feature_count;              // Number of supported features
    int feature_capacity;           // Capacity of features array

    // Ligature support
    LigatureInfo* ligatures;        // Array of available ligatures
    int ligature_count;             // Number of ligatures
    int ligature_capacity;          // Capacity of ligatures array

    // Kerning support
    struct hashmap* kerning_cache;  // Kerning pair cache
    bool kerning_enabled;           // Whether kerning is enabled
    int kerning_scale_factor;       // Scale factor for current font size

    // Performance counters
    int ligature_substitutions;     // Number of ligature substitutions
    int kerning_adjustments;        // Number of kerning adjustments
    int feature_lookups;            // Number of feature lookups
} OpenTypeFontInfo;

// Advanced glyph rendering with OpenType features
typedef struct AdvancedGlyphInfo {
    uint32_t original_codepoint;    // Original codepoint
    uint32_t rendered_codepoint;    // Final rendered codepoint (after substitution)
    FT_UInt glyph_index;           // Glyph index in font

    // Positioning information
    int advance_x, advance_y;       // Glyph advance
    int offset_x, offset_y;         // Positioning offset
    int bearing_x, bearing_y;       // Glyph bearing

    // OpenType feature information
    bool is_ligature;               // Whether this is a ligature
    bool has_kerning;               // Whether kerning was applied
    bool was_substituted;           // Whether glyph was substituted
    LigatureInfo* ligature_info;    // Ligature information (if applicable)
    KerningPair* kerning_info;      // Kerning information (if applicable)

    // Feature tags that affected this glyph
    OpenTypeFeatureTag* applied_features;
    int applied_feature_count;

    // Rendering metrics
    float pixel_ratio;              // High-DPI scaling
    bool subpixel_positioned;       // Subpixel positioning applied
} AdvancedGlyphInfo;

// OpenType text shaping context
typedef struct OpenTypeShapingContext {
    OpenTypeFontInfo* font_info;    // Font OpenType information
    EnhancedFontBox* font_box;      // Enhanced font box

    // Input text
    const uint32_t* input_codepoints;   // Input codepoints
    int input_count;                    // Number of input codepoints

    // Shaped output
    AdvancedGlyphInfo* shaped_glyphs;   // Shaped glyph information
    int shaped_count;                   // Number of shaped glyphs
    int shaped_capacity;                // Capacity of shaped array

    // Feature configuration
    OpenTypeFeature* enabled_features; // Enabled OpenType features
    int enabled_feature_count;          // Number of enabled features

    // Shaping parameters
    bool enable_ligatures;              // Enable ligature substitution
    bool enable_kerning;                // Enable kerning adjustment
    bool enable_contextual_alternates;  // Enable contextual alternates
    float font_size;                    // Current font size
    float pixel_ratio;                  // High-DPI scaling

    // Performance counters
    int total_substitutions;            // Total glyph substitutions
    int total_positioning_adjustments; // Total positioning adjustments
    int cache_hits;                     // Feature cache hits
    int cache_misses;                   // Feature cache misses
} OpenTypeShapingContext;

// === Core OpenType Functions ===

// Initialize OpenType logging
void init_opentype_logging(void);

// OpenType font analysis
OpenTypeFontInfo* analyze_opentype_font(FT_Face face);
void destroy_opentype_font_info(OpenTypeFontInfo* info);
bool font_supports_feature(OpenTypeFontInfo* info, OpenTypeFeatureTag feature);
void scan_font_features(OpenTypeFontInfo* info);

// Feature management
OpenTypeFeature* create_opentype_feature(OpenTypeFeatureTag tag, OpenTypeFeatureState state);
void enable_opentype_feature(OpenTypeFontInfo* info, OpenTypeFeatureTag feature);
void disable_opentype_feature(OpenTypeFontInfo* info, OpenTypeFeatureTag feature);
bool is_feature_enabled(OpenTypeFontInfo* info, OpenTypeFeatureTag feature);

// Ligature processing
int find_ligatures_in_text(OpenTypeFontInfo* info, const uint32_t* codepoints, int count, LigatureInfo** ligatures);
bool can_form_ligature(OpenTypeFontInfo* info, const uint32_t* codepoints, int count);
LigatureInfo* get_ligature_info(OpenTypeFontInfo* info, const uint32_t* input, int input_count);
void apply_ligature_substitution(AdvancedGlyphInfo* glyphs, int* glyph_count, int position, LigatureInfo* ligature);

// Kerning processing
int get_kerning_adjustment(OpenTypeFontInfo* info, uint32_t left_char, uint32_t right_char);
KerningPair* get_kerning_pair(OpenTypeFontInfo* info, uint32_t left_char, uint32_t right_char);
void apply_kerning_to_glyphs(AdvancedGlyphInfo* glyphs, int glyph_count, OpenTypeFontInfo* info);
void cache_kerning_pair(OpenTypeFontInfo* info, KerningPair* pair);

// Text shaping (main OpenType processing)
OpenTypeShapingContext* create_shaping_context(OpenTypeFontInfo* font_info, EnhancedFontBox* font_box);
void destroy_shaping_context(OpenTypeShapingContext* ctx);
int shape_text_with_opentype(OpenTypeShapingContext* ctx, const uint32_t* codepoints, int count);
void apply_opentype_features(OpenTypeShapingContext* ctx);

// Glyph substitution
void apply_glyph_substitutions(OpenTypeShapingContext* ctx);
void apply_ligature_substitutions(OpenTypeShapingContext* ctx);
void apply_contextual_substitutions(OpenTypeShapingContext* ctx);
GlyphSubstitution* find_glyph_substitution(OpenTypeFontInfo* info, uint32_t codepoint, OpenTypeFeatureTag feature);

// Glyph positioning
void apply_glyph_positioning(OpenTypeShapingContext* ctx);
void apply_kerning_positioning(OpenTypeShapingContext* ctx);
void apply_mark_positioning(OpenTypeShapingContext* ctx);
void calculate_final_positions(OpenTypeShapingContext* ctx);

// === Integration Functions ===

// Integration with existing text rendering
void enhance_font_box_with_opentype(EnhancedFontBox* font_box, OpenTypeFontInfo* ot_info);
void render_text_with_opentype_features(LayoutContext* lycon, DomNode* text_node, OpenTypeShapingContext* ctx);
int calculate_text_width_with_opentype(OpenTypeShapingContext* ctx, const uint32_t* codepoints, int count);

// CSS font-feature-settings support
void parse_font_feature_settings(const char* feature_string, OpenTypeFeature** features, int* feature_count);
void apply_css_font_features(OpenTypeShapingContext* ctx, const char* feature_settings);
char* serialize_font_features(OpenTypeFeature* features, int feature_count);

// Performance optimization
void enable_opentype_caching(OpenTypeShapingContext* ctx);
void disable_opentype_caching(OpenTypeShapingContext* ctx);
void clear_opentype_caches(OpenTypeShapingContext* ctx);
void print_opentype_performance_stats(OpenTypeShapingContext* ctx);

// === Utility Functions ===

// Feature tag utilities
OpenTypeFeatureTag make_feature_tag(const char* tag_string);
void feature_tag_to_string(OpenTypeFeatureTag tag, char* output);
const char* get_feature_name(OpenTypeFeatureTag tag);
const char* get_feature_description(OpenTypeFeatureTag tag);

// Glyph utilities
bool is_ligature_glyph(FT_Face face, FT_UInt glyph_index);
bool is_mark_glyph(FT_Face face, FT_UInt glyph_index);
bool glyphs_can_kern(FT_Face face, FT_UInt left_glyph, FT_UInt right_glyph);

// Text analysis
bool text_benefits_from_ligatures(const uint32_t* codepoints, int count);
bool text_benefits_from_kerning(const uint32_t* codepoints, int count);
int count_potential_ligatures(const uint32_t* codepoints, int count);
int estimate_kerning_pairs(const uint32_t* codepoints, int count);

// Memory management
void cleanup_advanced_glyph_info(AdvancedGlyphInfo* glyph);
void cleanup_ligature_info(LigatureInfo* ligature);
void cleanup_kerning_pair(KerningPair* pair);
void cleanup_opentype_font_info_memory(OpenTypeFontInfo* info);

// Debugging and logging
void log_opentype_feature(OpenTypeFeature* feature);
void log_ligature_substitution(LigatureInfo* ligature);
void log_kerning_adjustment(KerningPair* pair);
void log_shaping_results(OpenTypeShapingContext* ctx);
void debug_print_shaped_glyphs(OpenTypeShapingContext* ctx);

#ifdef __cplusplus
}
#endif
