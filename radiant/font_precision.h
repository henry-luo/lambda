#ifndef FONT_PRECISION_H
#define FONT_PRECISION_H

#include "view.hpp"
#include <freetype/freetype.h>
#include <fontconfig/fontconfig.h>

// Enhanced font loading with float precision support
typedef struct {
    float font_size;        // Precise font size in points
    float pixel_ratio;      // Device pixel ratio for high-DPI displays
    bool sub_pixel_render;  // Enable sub-pixel rendering
    bool use_autohint;      // Use FreeType auto-hinter
} FontPrecisionConfig;

// Function declarations
char* load_font_path(FcConfig *font_config, const char* font_name);

// Load font with sub-pixel precision
FT_Face load_font_precise(UiContext* uicon, const char* font_name, FontPrecisionConfig* config);

// Set font size with sub-pixel precision
int set_font_size_precise(FT_Face face, float font_size, float pixel_ratio);

// Get glyph advance with sub-pixel precision
float get_glyph_advance_precise(FT_Face face, uint32_t codepoint, bool use_kerning);

// Calculate text width with float precision
float calculate_text_width_precise(FT_Face face, const char* text, int length);

// Configure FreeType for optimal sub-pixel rendering
void configure_freetype_subpixel(FT_Library library);

#endif // FONT_PRECISION_H
