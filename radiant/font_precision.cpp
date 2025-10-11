#include "font_precision.h"
#include "../lib/log.h"
#include <freetype/ftlcdfil.h>

// Load font with sub-pixel precision
FT_Face load_font_precise(UiContext* uicon, const char* font_name, FontPrecisionConfig* config) {
    if (!uicon || !font_name || !config) {
        return NULL;
    }

    // Load font path using existing infrastructure
    char* font_path = load_font_path(uicon->font_config, font_name);
    if (!font_path) {
        log_error("Font path not found: %s", font_name);
        return NULL;
    }

    FT_Face face;
    if (FT_New_Face(uicon->ft_library, font_path, 0, &face)) {
        log_error("Failed to load font face: %s", font_path);
        free(font_path);
        return NULL;
    }

    // Set font size with sub-pixel precision
    if (set_font_size_precise(face, config->font_size, config->pixel_ratio) != 0) {
        log_error("Failed to set precise font size: %.2f", config->font_size);
        FT_Done_Face(face);
        free(font_path);
        return NULL;
    }

    log_debug("Loaded font with precision: %s, size: %.2f, pixel_ratio: %.2f",
              font_name, config->font_size, config->pixel_ratio);

    free(font_path);
    return face;
}

// Set font size with sub-pixel precision
int set_font_size_precise(FT_Face face, float font_size, float pixel_ratio) {
    if (!face || font_size <= 0.0f || pixel_ratio <= 0.0f) {
        return -1;
    }

    // Convert font size to 26.6 fixed point format with pixel ratio scaling
    FT_F26Dot6 char_size = (FT_F26Dot6)(font_size * pixel_ratio * 64.0f);

    // Use 96 DPI as standard for screen rendering
    FT_UInt horizontal_dpi = (FT_UInt)(96.0f * pixel_ratio);
    FT_UInt vertical_dpi = horizontal_dpi;

    FT_Error error = FT_Set_Char_Size(face, 0, char_size, horizontal_dpi, vertical_dpi);
    if (error) {
        log_error("FT_Set_Char_Size failed with error: %d", error);
        return -1;
    }

    log_debug("Set precise font size: %.2f pts (26.6 fixed: %ld) at %u DPI",
              font_size, char_size, horizontal_dpi);

    return 0;
}

// Get glyph advance with sub-pixel precision
float get_glyph_advance_precise(FT_Face face, uint32_t codepoint, bool use_kerning) {
    if (!face) {
        return 0.0f;
    }

    // Load flags for sub-pixel rendering
    FT_Int32 load_flags = FT_LOAD_DEFAULT;
    if (use_kerning) {
        load_flags |= FT_LOAD_ADVANCE_ONLY; // Faster for advance-only queries
    }

    FT_Error error = FT_Load_Char(face, codepoint, load_flags);
    if (error) {
        log_warn("Failed to load glyph for codepoint U+%04X", codepoint);
        return 0.0f;
    }

    // Convert 26.6 fixed point to float with high precision
    float advance = (float)(face->glyph->advance.x) / 64.0f;

    return advance;
}

// Calculate text width with float precision
float calculate_text_width_precise(FT_Face face, const char* text, int length) {
    if (!face || !text || length <= 0) {
        return 0.0f;
    }

    float total_width = 0.0f;
    FT_UInt previous_glyph_index = 0;
    bool has_kerning = FT_HAS_KERNING(face);

    for (int i = 0; i < length; i++) {
        uint32_t codepoint = (uint32_t)text[i]; // Simplified - real implementation would handle UTF-8

        // Get glyph advance
        float advance = get_glyph_advance_precise(face, codepoint, false);

        // Apply kerning if available
        if (has_kerning && previous_glyph_index && face->glyph) {
            FT_UInt current_glyph_index = FT_Get_Char_Index(face, codepoint);
            if (current_glyph_index) {
                FT_Vector kerning;
                FT_Error error = FT_Get_Kerning(face, previous_glyph_index, current_glyph_index,
                                              FT_KERNING_DEFAULT, &kerning);
                if (!error) {
                    advance += (float)(kerning.x) / 64.0f;
                }
            }
            previous_glyph_index = current_glyph_index;
        } else if (face->glyph) {
            previous_glyph_index = FT_Get_Char_Index(face, codepoint);
        }

        total_width += advance;
    }

    return total_width;
}

// Configure FreeType for optimal sub-pixel rendering
void configure_freetype_subpixel(FT_Library library) {
    if (!library) {
        log_error("Invalid FreeType library handle");
        return;
    }

    // Enable LCD filtering for sub-pixel rendering
    FT_Error error = FT_Library_SetLcdFilter(library, FT_LCD_FILTER_DEFAULT);
    if (error) {
        log_warn("Failed to set LCD filter: %d", error);
    } else {
        log_debug("LCD filter enabled for sub-pixel rendering");
    }

    log_info("FreeType configured for sub-pixel rendering (basic mode)");
}
