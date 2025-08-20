#ifndef FONT_MANAGER_H
#define FONT_MANAGER_H

#include "../typeset.h"
#include "../../lambda/lambda.h"
#include <stdbool.h>
#include <stdint.h>

// Forward declarations
typedef struct FontManager FontManager;
typedef struct ViewFont ViewFont;
typedef struct FontCache FontCache;
typedef struct FontMetrics FontMetrics;

// Font weight constants
typedef enum ViewFontWeight {
    FONT_WEIGHT_THIN = 100,
    FONT_WEIGHT_EXTRA_LIGHT = 200,
    FONT_WEIGHT_LIGHT = 300,
    FONT_WEIGHT_NORMAL = 400,
    FONT_WEIGHT_MEDIUM = 500,
    FONT_WEIGHT_SEMI_BOLD = 600,
    FONT_WEIGHT_BOLD = 700,
    FONT_WEIGHT_EXTRA_BOLD = 800,
    FONT_WEIGHT_BLACK = 900
} ViewFontWeight;

// Font style constants
typedef enum ViewFontStyle {
    FONT_STYLE_NORMAL = 0,
    FONT_STYLE_ITALIC = 1,
    FONT_STYLE_OBLIQUE = 2
} ViewFontStyle;

// Font stretch constants
typedef enum ViewFontStretch {
    FONT_STRETCH_ULTRA_CONDENSED = 1,
    FONT_STRETCH_EXTRA_CONDENSED = 2,
    FONT_STRETCH_CONDENSED = 3,
    FONT_STRETCH_SEMI_CONDENSED = 4,
    FONT_STRETCH_NORMAL = 5,
    FONT_STRETCH_SEMI_EXPANDED = 6,
    FONT_STRETCH_EXPANDED = 7,
    FONT_STRETCH_EXTRA_EXPANDED = 8,
    FONT_STRETCH_ULTRA_EXPANDED = 9
} ViewFontStretch;

// Font face structure - represents a loaded font
struct ViewFont {
    char* family_name;          // Font family name (e.g., "Times New Roman")
    char* style_name;           // Style name (e.g., "Bold Italic")
    char* file_path;            // Path to font file
    
    double size;                // Font size in points
    ViewFontWeight weight;      // Font weight
    ViewFontStyle style;        // Font style
    ViewFontStretch stretch;    // Font stretch
    
    // Font face data (opaque pointer to FreeType FT_Face or similar)
    void* font_face;            // Actual font face implementation
    void* font_data;            // Font file data (if loaded from memory)
    size_t font_data_size;      // Size of font data
    
    // Metrics cache
    FontMetrics* cached_metrics; // Cached font metrics
    bool metrics_valid;         // Whether cached metrics are valid
    
    // Reference counting
    int ref_count;              // Reference count for memory management
    
    // Cache management
    uint32_t cache_key;         // Cache key for quick lookup
    struct ViewFont* next_in_cache; // Next font in cache chain
};

// Font cache entry
typedef struct FontCacheEntry {
    uint32_t key;               // Cache key (hash of family + size + weight + style)
    ViewFont* font;             // Cached font
    struct FontCacheEntry* next; // Next entry in hash chain
    uint64_t last_access_time;  // Last access time for LRU eviction
    int access_count;           // Access count for popularity tracking
} FontCacheEntry;

// Font cache structure
struct FontCache {
    FontCacheEntry** buckets;   // Hash table buckets
    int bucket_count;           // Number of buckets
    int entry_count;            // Current number of entries
    int max_entries;            // Maximum number of entries
    
    // Statistics
    uint64_t hits;              // Cache hits
    uint64_t misses;            // Cache misses
    uint64_t evictions;         // Number of evictions
};

// Font manager structure
struct FontManager {
    Context* lambda_context;    // Lambda memory context
    FontCache* font_cache;      // Font cache
    
    // Default font settings
    char* default_font_family;  // Default font family
    double default_font_size;   // Default font size
    ViewFontWeight default_weight; // Default font weight
    ViewFontStyle default_style; // Default font style
    
    // Font search paths
    char** font_directories;    // Array of font directory paths
    int font_directory_count;   // Number of font directories
    
    // Font fallbacks
    char** fallback_families;   // Array of fallback font families
    int fallback_count;         // Number of fallback families
    
    // System integration
    void* system_font_manager;  // Platform-specific font manager
    bool use_system_fonts;      // Whether to use system fonts
    
    // Statistics
    struct {
        int fonts_loaded;       // Total fonts loaded
        int cache_size;         // Current cache size
        double avg_load_time;   // Average font load time
        size_t memory_usage;    // Total memory usage
    } stats;
};

// Font manager creation and destruction
FontManager* font_manager_create(Context* ctx);
void font_manager_destroy(FontManager* mgr);

// Font loading and management
ViewFont* font_manager_get_font(FontManager* mgr, const char* family, double size, 
                               ViewFontWeight weight, ViewFontStyle style);
ViewFont* font_manager_get_default_font(FontManager* mgr);
ViewFont* font_manager_load_font_from_file(FontManager* mgr, const char* file_path, double size);
ViewFont* font_manager_load_font_from_memory(FontManager* mgr, const void* data, size_t size, double font_size);

// Font search and fallback
ViewFont* font_manager_find_best_match(FontManager* mgr, const char* family, double size,
                                      ViewFontWeight weight, ViewFontStyle style);
ViewFont* font_manager_get_fallback_font(FontManager* mgr, const char* preferred_family, 
                                        double size, uint32_t codepoint);

// Font reference management
void font_retain(ViewFont* font);
void font_release(ViewFont* font);

// Font properties
const char* font_get_family_name(ViewFont* font);
const char* font_get_style_name(ViewFont* font);
double font_get_size(ViewFont* font);
ViewFontWeight font_get_weight(ViewFont* font);
ViewFontStyle font_get_style(ViewFont* font);

// Font settings
void font_manager_set_default_font(FontManager* mgr, const char* family, double size);
void font_manager_set_default_weight(FontManager* mgr, ViewFontWeight weight);
void font_manager_set_default_style(FontManager* mgr, ViewFontStyle style);
void font_manager_add_font_directory(FontManager* mgr, const char* directory);
void font_manager_add_fallback_family(FontManager* mgr, const char* family);

// Font cache management
void font_cache_clear(FontManager* mgr);
void font_cache_set_max_size(FontManager* mgr, int max_entries);
void font_cache_evict_lru(FontManager* mgr, int count);

// Font enumeration
typedef struct FontFamilyInfo {
    char* family_name;          // Font family name
    char** style_names;         // Available style names
    int style_count;            // Number of styles
    bool is_monospace;          // Whether font is monospace
    bool has_bold;              // Whether bold variant exists
    bool has_italic;            // Whether italic variant exists
} FontFamilyInfo;

FontFamilyInfo** font_manager_enumerate_families(FontManager* mgr, int* family_count);
void font_family_info_free(FontFamilyInfo* info);
void font_family_info_array_free(FontFamilyInfo** families, int count);

// Font matching and scoring
typedef struct FontMatchCriteria {
    const char* family;         // Preferred family name
    double size;                // Font size
    ViewFontWeight weight;      // Preferred weight
    ViewFontStyle style;        // Preferred style
    ViewFontStretch stretch;    // Preferred stretch
    const char* language;       // Language for script support
    uint32_t* required_codepoints; // Required Unicode codepoints
    int codepoint_count;        // Number of required codepoints
} FontMatchCriteria;

ViewFont* font_manager_match_font(FontManager* mgr, FontMatchCriteria* criteria);
double font_calculate_match_score(ViewFont* font, FontMatchCriteria* criteria);

// Font metrics access (forward declaration - defined in font_metrics.h)
FontMetrics* font_get_metrics(ViewFont* font);

// Utility functions
uint32_t font_calculate_cache_key(const char* family, double size, 
                                 ViewFontWeight weight, ViewFontStyle style);
bool font_families_equal(const char* family1, const char* family2);
ViewFontWeight font_weight_from_string(const char* weight_str);
ViewFontStyle font_style_from_string(const char* style_str);
const char* font_weight_to_string(ViewFontWeight weight);
const char* font_style_to_string(ViewFontStyle style);

// Error handling
typedef enum FontManagerError {
    FONT_ERROR_NONE = 0,
    FONT_ERROR_NOT_FOUND,       // Font not found
    FONT_ERROR_INVALID_FILE,    // Invalid font file
    FONT_ERROR_UNSUPPORTED_FORMAT, // Unsupported font format
    FONT_ERROR_MEMORY,          // Memory allocation error
    FONT_ERROR_SYSTEM,          // System font manager error
    FONT_ERROR_CACHE_FULL       // Font cache is full
} FontManagerError;

FontManagerError font_manager_get_last_error(FontManager* mgr);
const char* font_manager_error_string(FontManagerError error);

// Statistics and debugging
typedef struct FontManagerStats {
    int total_fonts_loaded;     // Total fonts loaded
    int cached_fonts;           // Currently cached fonts
    uint64_t cache_hits;        // Cache hits
    uint64_t cache_misses;      // Cache misses
    uint64_t total_requests;    // Total font requests
    double cache_hit_ratio;     // Cache hit ratio
    size_t memory_usage;        // Total memory usage in bytes
    double avg_load_time_ms;    // Average font load time in milliseconds
} FontManagerStats;

FontManagerStats font_manager_get_stats(FontManager* mgr);
void font_manager_print_stats(FontManager* mgr);
void font_manager_reset_stats(FontManager* mgr);

// Lambda integration
Item fn_font_manager_create(Context* ctx, Item* args, int arg_count);
Item fn_font_manager_get_font(Context* ctx, Item* args, int arg_count);
Item fn_font_manager_enumerate_families(Context* ctx, Item* args, int arg_count);

#endif // FONT_MANAGER_H
