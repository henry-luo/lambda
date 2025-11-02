// radiant/pdf/fonts.cpp
// PDF font mapping utilities

#include "operators.h"
#include "../view.hpp"
#include "../../lambda/input/input.h"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include <string.h>

/**
 * Map PDF font names to system fonts
 *
 * PDF Standard 14 Fonts:
 * - Times-Roman, Times-Bold, Times-Italic, Times-BoldItalic
 * - Helvetica, Helvetica-Bold, Helvetica-Oblique, Helvetica-BoldOblique
 * - Courier, Courier-Bold, Courier-Oblique, Courier-BoldOblique
 * - Symbol
 * - ZapfDingbats
 *
 * @param pdf_font PDF font name
 * @return System font name
 */
const char* map_pdf_font_to_system(const char* pdf_font) {
    // Standard 14 fonts mapping
    static const struct {
        const char* pdf_name;
        const char* system_name;
    } font_map[] = {
        // Helvetica family
        {"Helvetica", "Arial"},
        {"Helvetica-Bold", "Arial Bold"},
        {"Helvetica-Oblique", "Arial Italic"},
        {"Helvetica-BoldOblique", "Arial Bold Italic"},

        // Times family
        {"Times-Roman", "Times New Roman"},
        {"Times-Bold", "Times New Roman Bold"},
        {"Times-Italic", "Times New Roman Italic"},
        {"Times-BoldItalic", "Times New Roman Bold Italic"},

        // Courier family
        {"Courier", "Courier New"},
        {"Courier-Bold", "Courier New Bold"},
        {"Courier-Oblique", "Courier New Italic"},
        {"Courier-BoldOblique", "Courier New Bold Italic"},

        // Symbol fonts
        {"Symbol", "Symbol"},
        {"ZapfDingbats", "Zapf Dingbats"},

        {nullptr, nullptr}
    };

    for (int i = 0; font_map[i].pdf_name; i++) {
        if (strcmp(pdf_font, font_map[i].pdf_name) == 0) {
            return font_map[i].system_name;
        }
    }

    // If not found in standard fonts, try partial matching
    if (strstr(pdf_font, "Helvetica") || strstr(pdf_font, "Arial")) {
        return "Arial";
    }
    if (strstr(pdf_font, "Times") || strstr(pdf_font, "Serif")) {
        return "Times New Roman";
    }
    if (strstr(pdf_font, "Courier") || strstr(pdf_font, "Mono")) {
        return "Courier New";
    }

    // Default fallback
    log_debug("Unknown PDF font '%s', using Arial as fallback", pdf_font);
    return "Arial";
}

/**
 * Extract font weight from PDF font name
 *
 * @param pdf_font PDF font name
 * @return Font weight (LXB_CSS_VALUE_NORMAL or LXB_CSS_VALUE_BOLD)
 */
PropValue get_font_weight_from_name(const char* pdf_font) {
    if (strstr(pdf_font, "Bold") || strstr(pdf_font, "Heavy") || strstr(pdf_font, "Black")) {
        return LXB_CSS_VALUE_BOLD;
    }
    return LXB_CSS_VALUE_NORMAL;
}

/**
 * Extract font style from PDF font name
 *
 * @param pdf_font PDF font name
 * @return Font style (LXB_CSS_VALUE_NORMAL, LXB_CSS_VALUE_ITALIC, or LXB_CSS_VALUE_OBLIQUE)
 */
PropValue get_font_style_from_name(const char* pdf_font) {
    if (strstr(pdf_font, "Italic")) {
        return LXB_CSS_VALUE_ITALIC;
    }
    if (strstr(pdf_font, "Oblique")) {
        return LXB_CSS_VALUE_OBLIQUE;
    }
    return LXB_CSS_VALUE_NORMAL;
}

/**
 * Create font property from PDF font descriptor
 *
 * @param pool Memory pool for allocation
 * @param font_name PDF font name or font reference (e.g., "Helvetica-Bold" or "F2")
 * @param font_size Font size in points
 * @return FontProp structure
 */
FontProp* create_font_from_pdf(Pool* pool, const char* font_name, double font_size) {
    FontProp* font = (FontProp*)pool_calloc(pool, sizeof(FontProp));
    if (!font) {
        log_error("Failed to allocate font property");
        return nullptr;
    }

    // Handle font references (F1, F2, etc.) - common pattern in test PDFs
    // F1 typically = Helvetica, F2 = Helvetica-Bold
    // TODO: Properly resolve font references from PDF Resources dictionary
    const char* resolved_font_name = font_name;
    if (font_name && font_name[0] == 'F' && font_name[1] >= '1' && font_name[1] <= '9' && font_name[2] == '\0') {
        // Simple heuristic: F2, F4, F6, etc. are often bold variants
        if ((font_name[1] - '0') % 2 == 0) {
            resolved_font_name = "Helvetica-Bold";
            log_debug("Font reference '%s' mapped to '%s'", font_name, resolved_font_name);
        } else {
            resolved_font_name = "Helvetica";
            log_debug("Font reference '%s' mapped to '%s'", font_name, resolved_font_name);
        }
    }

    // Map PDF font to system font
    font->family = (char*)map_pdf_font_to_system(resolved_font_name);
    font->font_size = (float)font_size;

    // Extract weight and style from resolved font name
    font->font_weight = get_font_weight_from_name(resolved_font_name);
    font->font_style = get_font_style_from_name(resolved_font_name);

    log_debug("Created font: %s, size: %.2f, weight: %d, style: %d",
             font->family, font->font_size, font->font_weight, font->font_style);

    return font;
}

/**
 * Extract font descriptor information from PDF font dictionary
 *
 * This is for more advanced font handling in later phases.
 * For Phase 1, we use simple font name mapping.
 *
 * TODO: Implement in Phase 2
 *
 * @param pool Memory pool for allocation
 * @param font_dict PDF font dictionary (Map)
 * @param input Input context for string creation
 * @return FontProp structure
 */
/*
FontProp* create_font_from_dict(Pool* pool, Map* font_dict, Input* input) {
    if (!font_dict) {
        log_warn("No font dictionary provided");
        return nullptr;
    }

    // Get base font name
    String* base_font_key = input_create_string(input, "BaseFont");
    Item base_font_item = map_get(font_dict, s2it(base_font_key));

    const char* font_name = "Arial"; // Default
    if (base_font_item.item != ITEM_NULL) {
        String* base_font = (String*)base_font_item.item;
        font_name = base_font->chars;
    }

    // Get font size (might not be in dictionary, will be set by Tf operator)
    double font_size = 12.0; // Default

    return create_font_from_pdf(pool, font_name, font_size);
}
*//**
 * Calculate text width (simplified for Phase 1)
 *
 * This is a rough estimation. Proper text width calculation
 * requires font metrics and glyph widths, which will be
 * implemented in Phase 2.
 *
 * @param text Text string
 * @param font_size Font size in points
 * @return Estimated text width in points
 */
float estimate_text_width(const char* text, float font_size) {
    if (!text) return 0.0f;

    int char_count = strlen(text);

    // Rough estimation: average character width is about 0.5 * font_size
    // This is a simplification; actual width varies by font and character
    return (float)char_count * font_size * 0.5f;
}

/**
 * Get font baseline offset (distance from top to baseline)
 *
 * @param font_size Font size in points
 * @return Baseline offset in points
 */
float get_font_baseline_offset(float font_size) {
    // Typical baseline is about 75-80% from top
    return font_size * 0.75f;
}
