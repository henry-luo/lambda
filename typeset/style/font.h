#ifndef FONT_H
#define FONT_H

#include "../typeset.h"

// Font weight constants
#define FONT_WEIGHT_THIN 100
#define FONT_WEIGHT_EXTRA_LIGHT 200
#define FONT_WEIGHT_LIGHT 300
#define FONT_WEIGHT_NORMAL 400
#define FONT_WEIGHT_MEDIUM 500
#define FONT_WEIGHT_SEMI_BOLD 600
#define FONT_WEIGHT_BOLD 700
#define FONT_WEIGHT_EXTRA_BOLD 800
#define FONT_WEIGHT_BLACK 900

// Font stretch constants
#define FONT_STRETCH_ULTRA_CONDENSED 50
#define FONT_STRETCH_EXTRA_CONDENSED 62
#define FONT_STRETCH_CONDENSED 75
#define FONT_STRETCH_SEMI_CONDENSED 87
#define FONT_STRETCH_NORMAL 100
#define FONT_STRETCH_SEMI_EXPANDED 112
#define FONT_STRETCH_EXPANDED 125
#define FONT_STRETCH_EXTRA_EXPANDED 150
#define FONT_STRETCH_ULTRA_EXPANDED 200

// Font structure
struct Font {
    char* family_name;          // "Times New Roman", "Arial", etc.
    char* style_name;           // "Regular", "Bold", "Italic", etc.
    char* full_name;            // Full font name
    float size;                 // Font size in points
    float line_height;          // Line height multiplier
    float letter_spacing;       // Additional letter spacing
    uint32_t weight;            // Font weight (100-900)
    uint32_t stretch;           // Font stretch (50-200)
    bool italic;                // Is italic/oblique
    bool bold;                  // Is bold
    
    // Font metrics
    float ascent;               // Ascent in font units
    float descent;              // Descent in font units
    float line_gap;             // Line gap in font units
    float cap_height;           // Cap height in font units
    float x_height;             // x-height in font units
    float units_per_em;         // Units per em square
    
    // Platform-specific font data
    void* platform_font;       // Platform-specific font handle
    
    // Glyph cache (for performance)
    void* glyph_cache;          // Cache for measured glyphs
    
    // Font loading state
    bool is_loaded;
    bool load_failed;
    char* load_error;
};

// Font manager structure
struct FontManager {
    Font** fonts;               // Array of loaded fonts
    size_t font_count;
    size_t font_capacity;
    
    // Default fonts
    Font* default_font;         // Default text font
    Font* math_font;           // Mathematical font
    Font* monospace_font;      // Code/monospace font
    Font* serif_font;          // Serif font
    Font* sans_serif_font;     // Sans-serif font
    
    // Font search paths
    char** font_paths;
    size_t font_path_count;
    
    // Font fallback chain
    Font** fallback_fonts;
    size_t fallback_count;
    
    // Platform-specific font system
    void* platform_font_system;
};

// Font creation and management
FontManager* font_manager_create(void);
void font_manager_destroy(FontManager* manager);

// Font loading
Font* font_load_from_file(const char* path, float size);
Font* font_load_from_system(const char* family, const char* style, float size);
Font* font_manager_load_font(FontManager* manager, const char* family, const char* style, float size);
Font* font_manager_get_font(FontManager* manager, const char* family, float size, uint32_t weight, bool italic);

// Font caching and lookup
Font* font_manager_find_cached_font(FontManager* manager, const char* family, float size, uint32_t weight, bool italic);
void font_manager_cache_font(FontManager* manager, Font* font);

// Default font management
Font* font_manager_get_default(FontManager* manager);
Font* font_manager_get_math_font(FontManager* manager);
Font* font_manager_get_monospace_font(FontManager* manager);
void font_manager_set_default_font(FontManager* manager, const char* family, float size);
void font_manager_set_math_font(FontManager* manager, const char* family, float size);

// Font metrics and measurement
float font_measure_text_width(Font* font, const char* text);
float font_measure_text_width_utf8(Font* font, const char* text, int length);
float font_get_line_height(Font* font);
float font_get_ascent(Font* font);
float font_get_descent(Font* font);
float font_get_cap_height(Font* font);
float font_get_x_height(Font* font);

// Advanced text measurement
typedef struct TextMetrics {
    float width;
    float height;
    float ascent;
    float descent;
    float baseline;
    float advance_width;
    int glyph_count;
} TextMetrics;

void font_get_text_metrics(Font* font, const char* text, TextMetrics* metrics);
void font_get_text_bounds(Font* font, const char* text, float* width, float* height, float* baseline);

// Character and glyph measurement
float font_measure_char_width(Font* font, uint32_t codepoint);
bool font_has_glyph(Font* font, uint32_t codepoint);
float font_get_char_advance(Font* font, uint32_t codepoint);

// Font styling
Font* font_create_styled_variant(Font* base_font, uint32_t weight, bool italic, float size);
Font* font_create_bold_variant(Font* font);
Font* font_create_italic_variant(Font* font);
Font* font_create_sized_variant(Font* font, float new_size);

// Font destruction
void font_destroy(Font* font);

// Font utilities
bool font_is_monospace(Font* font);
bool font_is_serif(Font* font);
bool font_supports_language(Font* font, const char* language_code);
float font_scale_size(Font* font, float target_size);

// Font family detection
typedef enum {
    FONT_FAMILY_SERIF,
    FONT_FAMILY_SANS_SERIF,
    FONT_FAMILY_MONOSPACE,
    FONT_FAMILY_CURSIVE,
    FONT_FAMILY_FANTASY,
    FONT_FAMILY_MATH
} FontFamilyClass;

FontFamilyClass font_get_family_class(Font* font);

// Font loading error handling
typedef enum {
    FONT_LOAD_SUCCESS,
    FONT_LOAD_FILE_NOT_FOUND,
    FONT_LOAD_INVALID_FORMAT,
    FONT_LOAD_UNSUPPORTED_FORMAT,
    FONT_LOAD_MEMORY_ERROR,
    FONT_LOAD_SYSTEM_ERROR
} FontLoadResult;

FontLoadResult font_get_load_result(Font* font);
const char* font_get_load_error(Font* font);

// Platform-specific implementations (implemented in separate files)
#ifdef __APPLE__
#include "font_macos.h"
#elif defined(__linux__)
#include "font_linux.h"
#elif defined(_WIN32)
#include "font_windows.h"
#else
#include "font_generic.h"
#endif

// Font system initialization
bool font_system_init(void);
void font_system_cleanup(void);

// System font enumeration
typedef struct FontInfo {
    char* family_name;
    char* style_name;
    char* full_name;
    uint32_t weight;
    bool italic;
    bool monospace;
    char* file_path;
} FontInfo;

FontInfo** font_enumerate_system_fonts(int* count);
void font_info_list_destroy(FontInfo** fonts, int count);

#endif // FONT_H
