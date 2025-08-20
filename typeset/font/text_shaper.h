#ifndef TEXT_SHAPER_H
#define TEXT_SHAPER_H

#include "font_manager.h"
#include "font_metrics.h"
#include "../view/view_tree.h"
#include "../../lambda/lambda.h"
#include <stdbool.h>
#include <stdint.h>

// Forward declarations
typedef struct TextShaper TextShaper;
typedef struct TextShapeResult TextShapeResult;
typedef struct ShapingContext ShapingContext;

// Text direction enumeration
typedef enum TextDirection {
    TEXT_DIRECTION_LTR = 0,     // Left-to-right (default)
    TEXT_DIRECTION_RTL = 1,     // Right-to-left (Arabic, Hebrew)
    TEXT_DIRECTION_TTB = 2,     // Top-to-bottom (vertical scripts)
    TEXT_DIRECTION_BTT = 3      // Bottom-to-top
} TextDirection;

// Script identification
typedef enum ScriptType {
    SCRIPT_LATIN = 0,           // Latin script (English, European languages)
    SCRIPT_ARABIC = 1,          // Arabic script
    SCRIPT_HEBREW = 2,          // Hebrew script
    SCRIPT_CHINESE = 3,         // Chinese (Han) script
    SCRIPT_JAPANESE = 4,        // Japanese (Hiragana, Katakana, Kanji)
    SCRIPT_KOREAN = 5,          // Korean (Hangul)
    SCRIPT_THAI = 6,            // Thai script
    SCRIPT_DEVANAGARI = 7,      // Devanagari (Hindi, Sanskrit)
    SCRIPT_CYRILLIC = 8,        // Cyrillic script
    SCRIPT_GREEK = 9,           // Greek script
    SCRIPT_UNKNOWN = 10         // Unknown or mixed script
} ScriptType;

// Text shaping features
typedef struct ShapingFeatures {
    bool enable_kerning;        // Enable kerning pairs
    bool enable_ligatures;      // Enable ligature substitution
    bool enable_contextual;     // Enable contextual alternates
    bool enable_positional;     // Enable positional forms (Arabic)
    bool enable_marks;          // Enable mark positioning
    bool enable_cursive;        // Enable cursive attachment
    
    // OpenType features (4-character tags)
    char** feature_tags;        // Array of feature tag strings
    bool* feature_values;       // Array of feature enable/disable values
    int feature_count;          // Number of features
} ShapingFeatures;

// Shaping context - maintains state during text processing
struct ShapingContext {
    ViewFont* font;             // Font being used
    double font_size;           // Font size
    
    // Text properties
    TextDirection direction;    // Text direction
    ScriptType script;          // Script type
    char* language;             // Language code (e.g., "en-US")
    
    // Shaping features
    ShapingFeatures features;   // Enabled shaping features
    
    // Context state
    uint32_t* input_text;       // Unicode codepoints
    int text_length;            // Number of codepoints
    int cluster_level;          // Cluster level for complex scripts
    
    // Memory management
    Context* lambda_context;    // Lambda memory context
    int ref_count;              // Reference count
};

// Text shaping result - contains positioned glyphs
struct TextShapeResult {
    // Input information
    char* original_text;        // Original UTF-8 text
    int text_length;            // Length in bytes
    ViewFont* font;             // Font used for shaping
    double font_size;           // Font size used
    
    // Shaped output
    ViewGlyphInfo* glyphs;      // Array of shaped glyphs
    ViewPoint* positions;       // Array of glyph positions
    int glyph_count;            // Number of glyphs
    
    // Cluster mapping (for complex scripts)
    int* cluster_map;           // Maps glyphs to text positions
    int* reverse_cluster_map;   // Maps text positions to glyphs
    
    // Measurements
    double total_width;         // Total width of shaped text
    double total_height;        // Total height
    double ascent;              // Maximum ascent
    double descent;             // Maximum descent
    
    // Shaping metadata
    TextDirection direction;    // Text direction used
    ScriptType script;          // Script detected/used
    char* language;             // Language used
    bool is_complex_script;     // Whether complex shaping was applied
    bool has_marks;             // Whether text contains combining marks
    bool has_ligatures;         // Whether ligatures were applied
    bool has_kerning;           // Whether kerning was applied
    
    // Line break information
    bool* can_break_after;      // Whether line can break after each glyph
    double* break_penalties;    // Break penalty for each position
    int break_opportunity_count; // Number of break opportunities
    
    // Reference counting
    int ref_count;              // Reference count
};

// Text shaper main interface
struct TextShaper {
    Context* lambda_context;    // Lambda memory context
    FontManager* font_manager;  // Font manager for fallback fonts
    
    // Shaping engine (could be HarfBuzz, ICU, or custom)
    void* shaping_engine;       // Opaque pointer to shaping implementation
    
    // Default settings
    ShapingFeatures default_features; // Default shaping features
    TextDirection default_direction;  // Default text direction
    char* default_language;     // Default language
    
    // Caching
    struct ShapeCache* cache;   // Shape result cache
    bool enable_caching;        // Whether to cache results
    int max_cache_size;         // Maximum cache size
    
    // Statistics
    struct {
        uint64_t shapes_performed; // Total shapes performed
        uint64_t cache_hits;       // Cache hits
        uint64_t cache_misses;     // Cache misses
        double avg_shape_time;     // Average shaping time (ms)
        size_t memory_usage;       // Current memory usage
    } stats;
};

// Text shaper creation and destruction
TextShaper* text_shaper_create(Context* ctx, FontManager* font_manager);
void text_shaper_destroy(TextShaper* shaper);

// Shaping context management
ShapingContext* shaping_context_create(TextShaper* shaper, ViewFont* font);
ShapingContext* shaping_context_create_with_options(TextShaper* shaper, ViewFont* font,
                                                   TextDirection direction, ScriptType script,
                                                   const char* language);
void shaping_context_retain(ShapingContext* context);
void shaping_context_release(ShapingContext* context);

// Context configuration
void shaping_context_set_direction(ShapingContext* context, TextDirection direction);
void shaping_context_set_script(ShapingContext* context, ScriptType script);
void shaping_context_set_language(ShapingContext* context, const char* language);
void shaping_context_set_features(ShapingContext* context, ShapingFeatures* features);

// Main shaping functions
TextShapeResult* text_shape(ViewFont* font, const char* text, int length);
TextShapeResult* text_shape_with_context(ShapingContext* context, const char* text, int length);
TextShapeResult* text_shape_with_features(ViewFont* font, const char* text, int length,
                                         ShapingFeatures* features);

// Advanced shaping
TextShapeResult* text_shape_segment(ShapingContext* context, const char* text, int start, int length);
TextShapeResult* text_shape_with_fallback(TextShaper* shaper, const char* text, int length,
                                         ViewFont* primary_font, ViewFont** fallback_fonts,
                                         int fallback_count);

// Shape result management
void text_shape_result_retain(TextShapeResult* result);
void text_shape_result_release(TextShapeResult* result);

// Shape result access
int text_shape_result_get_glyph_count(TextShapeResult* result);
ViewGlyphInfo* text_shape_result_get_glyph(TextShapeResult* result, int index);
ViewPoint text_shape_result_get_glyph_position(TextShapeResult* result, int index);
double text_shape_result_get_total_width(TextShapeResult* result);
double text_shape_result_get_total_height(TextShapeResult* result);

// Text analysis and detection
TextDirection detect_text_direction(const char* text, int length);
ScriptType detect_script(const char* text, int length);
char* detect_language(const char* text, int length);
bool is_complex_script(ScriptType script);
bool requires_bidi_processing(const char* text, int length);

// Unicode processing utilities
uint32_t* utf8_to_unicode(const char* utf8_text, int byte_length, int* codepoint_count);
char* unicode_to_utf8(uint32_t* unicode_text, int codepoint_count, int* byte_length);
bool is_combining_mark(uint32_t codepoint);
bool is_variation_selector(uint32_t codepoint);
bool is_emoji(uint32_t codepoint);

// Script-specific processing
TextShapeResult* shape_arabic_text(ShapingContext* context, const char* text, int length);
TextShapeResult* shape_thai_text(ShapingContext* context, const char* text, int length);
TextShapeResult* shape_devanagari_text(ShapingContext* context, const char* text, int length);
TextShapeResult* shape_cjk_text(ShapingContext* context, const char* text, int length);

// Bidirectional text processing
typedef struct BidiResult {
    TextDirection* directions;  // Direction for each character
    int* levels;               // Embedding levels
    int* reorder_map;          // Visual to logical mapping
    int char_count;            // Number of characters
} BidiResult;

BidiResult* process_bidi_text(const char* text, int length, TextDirection base_direction);
void bidi_result_destroy(BidiResult* result);
TextShapeResult* shape_bidi_text(ShapingContext* context, const char* text, int length,
                                TextDirection base_direction);

// Shaping features management
ShapingFeatures* shaping_features_create(void);
void shaping_features_destroy(ShapingFeatures* features);
void shaping_features_enable_kerning(ShapingFeatures* features, bool enable);
void shaping_features_enable_ligatures(ShapingFeatures* features, bool enable);
void shaping_features_add_feature(ShapingFeatures* features, const char* tag, bool enabled);
void shaping_features_copy(ShapingFeatures* dest, ShapingFeatures* src);

// Line breaking integration
typedef struct LineBreakInfo {
    bool* can_break_before;     // Can break before each character
    bool* can_break_after;      // Can break after each character
    double* break_penalties;    // Break penalties
    int char_count;             // Number of characters
} LineBreakInfo;

LineBreakInfo* analyze_line_breaks(const char* text, int length, const char* language);
void line_break_info_destroy(LineBreakInfo* info);
void text_shape_result_apply_line_breaks(TextShapeResult* result, LineBreakInfo* line_breaks);

// Font fallback during shaping
typedef struct FallbackResult {
    TextShapeResult** shape_results; // Array of shape results per font
    ViewFont** fonts_used;      // Array of fonts used
    int* font_segments;         // Font used for each character segment
    int segment_count;          // Number of font segments
    int result_count;           // Number of shape results
} FallbackResult;

FallbackResult* shape_with_font_fallback(TextShaper* shaper, const char* text, int length,
                                        ViewFont** fonts, int font_count);
void fallback_result_destroy(FallbackResult* result);

// Caching
typedef struct ShapeCache ShapeCache;

ShapeCache* shape_cache_create(int max_entries);
void shape_cache_destroy(ShapeCache* cache);
TextShapeResult* shape_cache_get(ShapeCache* cache, ViewFont* font, const char* text, int length);
void shape_cache_put(ShapeCache* cache, ViewFont* font, const char* text, int length,
                     TextShapeResult* result);
void shape_cache_clear(ShapeCache* cache);

// Performance optimization
void text_shaper_set_cache_enabled(TextShaper* shaper, bool enabled);
void text_shaper_set_max_cache_size(TextShaper* shaper, int max_size);
void text_shaper_warm_cache(TextShaper* shaper, const char** common_texts, int text_count,
                           ViewFont** fonts, int font_count);

// Statistics and debugging
typedef struct TextShaperStats {
    uint64_t total_shapes;      // Total shaping operations
    uint64_t cache_hits;        // Cache hits
    uint64_t cache_misses;      // Cache misses
    double cache_hit_ratio;     // Cache hit ratio
    double avg_shape_time_ms;   // Average shaping time
    size_t memory_usage;        // Memory usage in bytes
    int active_contexts;        // Active shaping contexts
} TextShaperStats;

TextShaperStats text_shaper_get_stats(TextShaper* shaper);
void text_shaper_print_stats(TextShaper* shaper);
void text_shaper_reset_stats(TextShaper* shaper);

// Debugging and validation
void text_shape_result_print(TextShapeResult* result);
void text_shape_result_dump_glyphs(TextShapeResult* result);
bool text_shape_result_validate(TextShapeResult* result);
void shaping_context_print(ShapingContext* context);

// Lambda integration
Item fn_text_shape(Context* ctx, Item* args, int arg_count);
Item fn_detect_text_direction(Context* ctx, Item* args, int arg_count);
Item fn_detect_script(Context* ctx, Item* args, int arg_count);
Item text_shape_result_to_lambda_item(Context* ctx, TextShapeResult* result);

// Utility constants
#define MAX_FEATURE_COUNT 32
#define MAX_LANGUAGE_LENGTH 16
#define DEFAULT_CACHE_SIZE 100

// OpenType feature tags (common ones)
#define FEATURE_KERN "kern"     // Kerning
#define FEATURE_LIGA "liga"     // Standard ligatures
#define FEATURE_DLIG "dlig"     // Discretionary ligatures
#define FEATURE_CLIG "clig"     // Contextual ligatures
#define FEATURE_CALT "calt"     // Contextual alternates
#define FEATURE_INIT "init"     // Initial forms
#define FEATURE_MEDI "medi"     // Medial forms
#define FEATURE_FINA "fina"     // Final forms
#define FEATURE_ISOL "isol"     // Isolated forms

#endif // TEXT_SHAPER_H
