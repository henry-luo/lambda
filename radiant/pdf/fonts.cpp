// radiant/pdf/fonts.cpp
// PDF font mapping and embedded font extraction utilities

#include "operators.h"
#include "../view.hpp"
#include "../../lambda/input/input.hpp"
#include "../../lambda/input/pdf_decompress.h"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include <string.h>
#include <stdlib.h>

// FreeType for embedded font loading
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H

/**
 * PDF Font Types
 * Based on pdf.js src/core/fonts.js classification
 */
typedef enum {
    PDF_FONT_TYPE1,           // PostScript Type 1
    PDF_FONT_TYPE1C,          // CFF-based Type 1
    PDF_FONT_TRUETYPE,        // TrueType
    PDF_FONT_OPENTYPE,        // OpenType (CFF or TrueType)
    PDF_FONT_TYPE3,           // Glyph streams (inline graphics)
    PDF_FONT_CID_TYPE0,       // CID-keyed Type 1
    PDF_FONT_CID_TYPE0C,      // CID-keyed CFF
    PDF_FONT_CID_TYPE2,       // CID-keyed TrueType
    PDF_FONT_UNKNOWN
} PDFFontType;

/**
 * PDF Font Entry - cached font information
 */
typedef struct PDFFontEntry {
    char* name;                  // Font reference name (e.g., "F1")
    char* base_font;             // BaseFont name (e.g., "Helvetica")
    PDFFontType type;            // Font type
    
    // Glyph metrics
    float* widths;               // Glyph widths array
    int widths_count;
    float default_width;
    int first_char;              // First character code
    int last_char;               // Last character code
    
    // Font metrics
    float ascent;
    float descent;
    float cap_height;
    float x_height;
    
    // Embedded font data (if present)
    unsigned char* font_data;    // Raw font file data
    size_t font_data_len;
    FT_Face ft_face;             // FreeType face (if loaded)
    
    // ToUnicode mapping
    uint32_t* to_unicode;        // Character code to Unicode mapping
    int to_unicode_count;
    
    // Flags
    bool is_embedded;
    bool is_symbolic;
    bool is_serif;
    bool is_script;
    bool is_italic;
    bool is_bold;
    
    struct PDFFontEntry* next;   // Linked list
} PDFFontEntry;

/**
 * PDF Font Cache - stores all fonts for a document
 */
typedef struct {
    PDFFontEntry* fonts;
    int count;
    Pool* pool;
    FT_Library ft_library;
} PDFFontCache;

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

// TODO: Implement font resolution from PDF resources
// The resolve_font_from_resources function was disabled due to crashes
// when looking up indirect object references. For now, we use hardcoded
// font mappings which work for most PDFs.

/**
 * Extract font weight from PDF font name
 *
 * @param pdf_font PDF font name
 * @return Font weight (CSS_VALUE_NORMAL or CSS_VALUE_BOLD)
 */
CssEnum get_font_weight_from_name(const char* pdf_font) {
    if (strstr(pdf_font, "Bold") || strstr(pdf_font, "Heavy") || strstr(pdf_font, "Black")) {
        return CSS_VALUE_BOLD;
    }
    return CSS_VALUE_NORMAL;
}

/**
 * Extract font style from PDF font name
 *
 * @param pdf_font PDF font name
 * @return Font style (CSS_VALUE_NORMAL, CSS_VALUE_ITALIC, or CSS_VALUE_OBLIQUE)
 */
CssEnum get_font_style_from_name(const char* pdf_font) {
    if (strstr(pdf_font, "Italic")) {
        return CSS_VALUE_ITALIC;
    }
    if (strstr(pdf_font, "Oblique")) {
        return CSS_VALUE_OBLIQUE;
    }
    return CSS_VALUE_NORMAL;
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

    // Resolve font reference from hardcoded mapping
    const char* resolved_font_name = font_name;
    
    // Check if this is a font reference (F1, F2, F1.0, F2.0 etc.)
    if (font_name && font_name[0] == 'F' && font_name[1] >= '1' && font_name[1] <= '9') {
        // Use hardcoded mapping for common PDF font references
        if (font_name[2] == '\0' || (font_name[2] == '.' && font_name[3] == '0')) {
            switch (font_name[1]) {
                case '1': resolved_font_name = "Helvetica"; break;
                case '2': resolved_font_name = "Times-Roman"; break;
                case '3': resolved_font_name = "Helvetica"; break;  // Changed from Courier - most PDFs use proportional fonts
                case '4': resolved_font_name = "Helvetica-Bold"; break;
                case '5': resolved_font_name = "Times-Bold"; break;
                case '6': resolved_font_name = "Courier-Bold"; break;
                default: resolved_font_name = "Helvetica"; break;
            }
            log_debug("Font reference '%s' using fallback mapping to '%s'", font_name, resolved_font_name);
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

// ============================================================================
// Phase 2: Embedded Font Support
// ============================================================================

static FT_Library g_ft_library = nullptr;

/**
 * Initialize FreeType library for PDF font loading
 */
bool pdf_font_init_freetype() {
    if (g_ft_library) return true;
    
    FT_Error error = FT_Init_FreeType(&g_ft_library);
    if (error) {
        log_error("Failed to initialize FreeType: error %d", error);
        return false;
    }
    log_debug("Initialized FreeType for PDF font loading");
    return true;
}

/**
 * Cleanup FreeType library
 */
void pdf_font_cleanup_freetype() {
    if (g_ft_library) {
        FT_Done_FreeType(g_ft_library);
        g_ft_library = nullptr;
    }
}

/**
 * Create a font cache for a document
 */
PDFFontCache* pdf_font_cache_create(Pool* pool) {
    PDFFontCache* cache = (PDFFontCache*)pool_calloc(pool, sizeof(PDFFontCache));
    if (!cache) return nullptr;
    
    cache->pool = pool;
    cache->fonts = nullptr;
    cache->count = 0;
    
    // Initialize FreeType if needed
    if (!g_ft_library) {
        pdf_font_init_freetype();
    }
    cache->ft_library = g_ft_library;
    
    return cache;
}

/**
 * Detect font type from PDF font dictionary
 */
PDFFontType pdf_font_detect_type(Map* font_dict, Input* input) {
    if (!font_dict) return PDF_FONT_UNKNOWN;
    
    // Create helper to get string from dict
    auto get_name = [input, font_dict](const char* key) -> const char* {
        MarkBuilder builder(input);
        String* key_str = builder.createString(key);
        Item item = {.item = map_get(font_dict, {.item = s2it(key_str)}).item};
        if (item.item != ITEM_NULL) {
            String* val = item.get_string();
            return val ? val->chars : nullptr;
        }
        return nullptr;
    };
    
    // Get Subtype
    const char* subtype = get_name("Subtype");
    if (!subtype) return PDF_FONT_UNKNOWN;
    
    if (strcmp(subtype, "Type1") == 0) {
        // Check for CFF font data (FontFile3 with Type1C subtype)
        MarkBuilder builder(input);
        String* desc_key = builder.createString("FontDescriptor");
        Item desc_item = {.item = map_get(font_dict, {.item = s2it(desc_key)}).item};
        if (desc_item.item != ITEM_NULL && get_type_id(desc_item) == LMD_TYPE_MAP) {
            Map* desc_dict = desc_item.map;
            String* ff3_key = builder.createString("FontFile3");
            Item ff3_item = {.item = map_get(desc_dict, {.item = s2it(ff3_key)}).item};
            if (ff3_item.item != ITEM_NULL) {
                return PDF_FONT_TYPE1C;
            }
        }
        return PDF_FONT_TYPE1;
    }
    if (strcmp(subtype, "TrueType") == 0) return PDF_FONT_TRUETYPE;
    if (strcmp(subtype, "Type3") == 0) return PDF_FONT_TYPE3;
    if (strcmp(subtype, "CIDFontType0") == 0) return PDF_FONT_CID_TYPE0;
    if (strcmp(subtype, "CIDFontType0C") == 0) return PDF_FONT_CID_TYPE0C;
    if (strcmp(subtype, "CIDFontType2") == 0) return PDF_FONT_CID_TYPE2;
    if (strcmp(subtype, "Type0") == 0) {
        // Composite font - need to check descendant
        return PDF_FONT_CID_TYPE2; // Common case
    }
    if (strcmp(subtype, "OpenType") == 0) return PDF_FONT_OPENTYPE;
    
    return PDF_FONT_UNKNOWN;
}

/**
 * Extract embedded font data from PDF font dictionary
 * Returns the raw font data that can be loaded by FreeType
 */
static unsigned char* extract_embedded_font_data(Map* font_dict, Input* input, 
                                                  size_t* out_len, PDFFontType* out_type) {
    if (!font_dict || !out_len) return nullptr;
    *out_len = 0;
    
    MarkBuilder builder(input);
    
    // Get FontDescriptor
    String* desc_key = builder.createString("FontDescriptor");
    Item desc_item = {.item = map_get(font_dict, {.item = s2it(desc_key)}).item};
    if (desc_item.item == ITEM_NULL || get_type_id(desc_item) != LMD_TYPE_MAP) {
        log_debug("No FontDescriptor in font dict");
        return nullptr;
    }
    Map* desc_dict = desc_item.map;
    
    // Try FontFile (Type 1), FontFile2 (TrueType), FontFile3 (CFF/OpenType)
    const char* font_file_keys[] = {"FontFile3", "FontFile2", "FontFile", nullptr};
    PDFFontType font_types[] = {PDF_FONT_TYPE1C, PDF_FONT_TRUETYPE, PDF_FONT_TYPE1};
    
    for (int i = 0; font_file_keys[i]; i++) {
        String* ff_key = builder.createString(font_file_keys[i]);
        Item ff_item = {.item = map_get(desc_dict, {.item = s2it(ff_key)}).item};
        
        if (ff_item.item != ITEM_NULL && get_type_id(ff_item) == LMD_TYPE_MAP) {
            Map* stream_dict = ff_item.map;
            
            // Get stream data
            String* data_key = builder.createString("data");
            Item data_item = {.item = map_get(stream_dict, {.item = s2it(data_key)}).item};
            if (data_item.item == ITEM_NULL) continue;
            
            String* data_str = data_item.get_string();
            if (!data_str || data_str->len == 0) continue;
            
            // Check for filter (might need decompression)
            String* filter_key = builder.createString("Filter");
            Item filter_item = {.item = map_get(stream_dict, {.item = s2it(filter_key)}).item};
            
            if (filter_item.item != ITEM_NULL) {
                // Need to decompress
                String* filter_name = filter_item.get_string();
                const char* filters[1] = { filter_name->chars };
                
                size_t decompressed_len = 0;
                char* decompressed = pdf_decompress_stream(data_str->chars, data_str->len,
                                                           filters, 1, &decompressed_len);
                if (decompressed) {
                    *out_len = decompressed_len;
                    if (out_type) *out_type = font_types[i];
                    log_info("Extracted embedded font (%s): %zu bytes", font_file_keys[i], decompressed_len);
                    return (unsigned char*)decompressed;
                }
            } else {
                // Raw data
                unsigned char* font_data = (unsigned char*)malloc(data_str->len);
                if (font_data) {
                    memcpy(font_data, data_str->chars, data_str->len);
                    *out_len = data_str->len;
                    if (out_type) *out_type = font_types[i];
                    log_info("Extracted embedded font (%s): %zu bytes", font_file_keys[i], data_str->len);
                    return font_data;
                }
            }
        }
    }
    
    return nullptr;
}

/**
 * Load embedded font into FreeType
 */
FT_Face pdf_font_load_embedded(PDFFontCache* cache, unsigned char* font_data, 
                                size_t font_data_len, PDFFontType font_type) {
    if (!cache || !font_data || font_data_len == 0) return nullptr;
    if (!cache->ft_library) {
        if (!pdf_font_init_freetype()) return nullptr;
        cache->ft_library = g_ft_library;
    }
    
    FT_Face face = nullptr;
    FT_Error error;
    
    // Load based on font type
    switch (font_type) {
        case PDF_FONT_TRUETYPE:
        case PDF_FONT_OPENTYPE:
        case PDF_FONT_CID_TYPE2:
            // Direct TrueType/OpenType loading
            error = FT_New_Memory_Face(cache->ft_library, font_data, font_data_len, 0, &face);
            break;
            
        case PDF_FONT_TYPE1C:
        case PDF_FONT_CID_TYPE0C:
            // CFF font - FreeType can handle directly
            error = FT_New_Memory_Face(cache->ft_library, font_data, font_data_len, 0, &face);
            break;
            
        case PDF_FONT_TYPE1:
            // Type 1 font - FreeType can handle PFB/PFA
            error = FT_New_Memory_Face(cache->ft_library, font_data, font_data_len, 0, &face);
            break;
            
        default:
            log_warn("Unsupported font type for embedded loading: %d", font_type);
            return nullptr;
    }
    
    if (error) {
        log_error("FreeType failed to load embedded font: error %d", error);
        return nullptr;
    }
    
    log_info("Loaded embedded font: %s (%s)", 
             face->family_name ? face->family_name : "unknown",
             face->style_name ? face->style_name : "");
    
    return face;
}

/**
 * Add font to cache from PDF Resources
 */
PDFFontEntry* pdf_font_cache_add(PDFFontCache* cache, const char* ref_name, 
                                  Map* font_dict, Input* input) {
    if (!cache || !ref_name || !font_dict) return nullptr;
    
    // Check if already cached
    for (PDFFontEntry* entry = cache->fonts; entry; entry = entry->next) {
        if (entry->name && strcmp(entry->name, ref_name) == 0) {
            return entry;
        }
    }
    
    // Create new entry
    PDFFontEntry* entry = (PDFFontEntry*)pool_calloc(cache->pool, sizeof(PDFFontEntry));
    if (!entry) return nullptr;
    
    // Copy reference name
    size_t name_len = strlen(ref_name);
    entry->name = (char*)pool_calloc(cache->pool, name_len + 1);
    if (entry->name) {
        strcpy(entry->name, ref_name);
    }
    
    // Get BaseFont
    MarkBuilder builder(input);
    String* base_key = builder.createString("BaseFont");
    Item base_item = {.item = map_get(font_dict, {.item = s2it(base_key)}).item};
    if (base_item.item != ITEM_NULL) {
        String* base_str = base_item.get_string();
        if (base_str) {
            entry->base_font = (char*)pool_calloc(cache->pool, base_str->len + 1);
            if (entry->base_font) {
                strcpy(entry->base_font, base_str->chars);
            }
        }
    }
    
    // Detect font type
    entry->type = pdf_font_detect_type(font_dict, input);
    
    // Try to extract embedded font
    PDFFontType embed_type;
    size_t font_data_len = 0;
    unsigned char* font_data = extract_embedded_font_data(font_dict, input, &font_data_len, &embed_type);
    
    if (font_data && font_data_len > 0) {
        entry->is_embedded = true;
        entry->font_data = font_data;
        entry->font_data_len = font_data_len;
        
        // Load into FreeType
        entry->ft_face = pdf_font_load_embedded(cache, font_data, font_data_len, embed_type);
        if (entry->ft_face) {
            log_info("Cached embedded font '%s' -> '%s'", ref_name, 
                    entry->ft_face->family_name ? entry->ft_face->family_name : "unknown");
        }
    } else {
        entry->is_embedded = false;
        log_debug("Font '%s' (%s) is not embedded, using system fallback", 
                 ref_name, entry->base_font ? entry->base_font : "unknown");
    }
    
    // Extract widths if present
    String* widths_key = builder.createString("Widths");
    Item widths_item = {.item = map_get(font_dict, {.item = s2it(widths_key)}).item};
    if (widths_item.item != ITEM_NULL && get_type_id(widths_item) == LMD_TYPE_ARRAY) {
        Array* widths_array = widths_item.array;
        entry->widths_count = widths_array->length;
        entry->widths = (float*)pool_calloc(cache->pool, sizeof(float) * entry->widths_count);
        if (entry->widths) {
            for (int i = 0; i < entry->widths_count; i++) {
                Item w = array_get(widths_array, i);
                TypeId w_type = get_type_id(w);
                if (w_type == LMD_TYPE_FLOAT) {
                    entry->widths[i] = (float)w.get_double();
                } else if (w_type == LMD_TYPE_INT) {
                    entry->widths[i] = (float)w.int_val;
                }
            }
        }
    }
    
    // Get FirstChar/LastChar
    String* fc_key = builder.createString("FirstChar");
    Item fc_item = {.item = map_get(font_dict, {.item = s2it(fc_key)}).item};
    if (fc_item.item != ITEM_NULL) {
        entry->first_char = (int)(get_type_id(fc_item) == LMD_TYPE_FLOAT ? 
                                  fc_item.get_double() : fc_item.int_val);
    }
    
    String* lc_key = builder.createString("LastChar");
    Item lc_item = {.item = map_get(font_dict, {.item = s2it(lc_key)}).item};
    if (lc_item.item != ITEM_NULL) {
        entry->last_char = (int)(get_type_id(lc_item) == LMD_TYPE_FLOAT ? 
                                 lc_item.get_double() : lc_item.int_val);
    }
    
    // Add to cache list
    entry->next = cache->fonts;
    cache->fonts = entry;
    cache->count++;
    
    log_debug("Added font to cache: %s (type=%d, embedded=%d, widths=%d)", 
             ref_name, entry->type, entry->is_embedded, entry->widths_count);
    
    return entry;
}

/**
 * Get font entry from cache
 */
PDFFontEntry* pdf_font_cache_get(PDFFontCache* cache, const char* ref_name) {
    if (!cache || !ref_name) return nullptr;
    
    for (PDFFontEntry* entry = cache->fonts; entry; entry = entry->next) {
        if (entry->name && strcmp(entry->name, ref_name) == 0) {
            return entry;
        }
    }
    return nullptr;
}

/**
 * Create FontProp from cached font entry
 * Uses embedded FreeType face if available, otherwise falls back to system fonts
 */
FontProp* create_font_from_cache_entry(Pool* pool, PDFFontEntry* entry, double font_size) {
    if (!pool || !entry) return nullptr;
    
    FontProp* font = (FontProp*)pool_calloc(pool, sizeof(FontProp));
    if (!font) return nullptr;
    
    font->font_size = (float)font_size;
    
    if (entry->ft_face) {
        // Use embedded font - get family name from FreeType
        if (entry->ft_face->family_name) {
            font->family = entry->ft_face->family_name;
        } else {
            font->family = (char*)"Arial";
        }
        
        // Get style from FreeType
        if (entry->ft_face->style_flags & FT_STYLE_FLAG_BOLD) {
            font->font_weight = CSS_VALUE_BOLD;
        } else {
            font->font_weight = CSS_VALUE_NORMAL;
        }
        
        if (entry->ft_face->style_flags & FT_STYLE_FLAG_ITALIC) {
            font->font_style = CSS_VALUE_ITALIC;
        } else {
            font->font_style = CSS_VALUE_NORMAL;
        }
        
        // Store FreeType face reference for direct rendering
        font->ft_face = entry->ft_face;
        
        log_debug("Using embedded font: %s, size: %.2f", font->family, font->font_size);
    } else {
        // Fall back to system font mapping
        const char* base_font = entry->base_font ? entry->base_font : "Helvetica";
        font->family = (char*)map_pdf_font_to_system(base_font);
        font->font_weight = get_font_weight_from_name(base_font);
        font->font_style = get_font_style_from_name(base_font);
        
        log_debug("Using system font: %s for %s, size: %.2f", 
                 font->family, entry->name, font->font_size);
    }
    
    return font;
}

/**
 * Get glyph width from cached font entry
 */
float pdf_font_get_glyph_width(PDFFontEntry* entry, int char_code, float font_size) {
    if (!entry) return font_size * 0.5f;  // Default estimate
    
    // Check widths array first
    if (entry->widths && char_code >= entry->first_char && char_code <= entry->last_char) {
        int idx = char_code - entry->first_char;
        if (idx >= 0 && idx < entry->widths_count) {
            // PDF widths are in 1/1000 of text space unit
            return entry->widths[idx] / 1000.0f * font_size;
        }
    }
    
    // Try FreeType if embedded
    if (entry->ft_face) {
        FT_Error error = FT_Set_Char_Size(entry->ft_face, 0, (FT_F26Dot6)(font_size * 64), 72, 72);
        if (!error) {
            FT_UInt glyph_index = FT_Get_Char_Index(entry->ft_face, char_code);
            error = FT_Load_Glyph(entry->ft_face, glyph_index, FT_LOAD_NO_SCALE);
            if (!error) {
                // Advance width in font units
                float advance = entry->ft_face->glyph->linearHoriAdvance / 65536.0f;
                float units_per_em = (float)entry->ft_face->units_per_EM;
                return advance / units_per_em * font_size;
            }
        }
    }
    
    // Fallback: estimate based on font size
    return font_size * 0.5f;
}

/**
 * Calculate text width using cached font
 */
float pdf_font_calculate_text_width(PDFFontEntry* entry, const char* text, float font_size) {
    if (!text || !entry) return estimate_text_width(text, font_size);
    
    float total_width = 0.0f;
    const unsigned char* p = (const unsigned char*)text;
    
    while (*p) {
        total_width += pdf_font_get_glyph_width(entry, *p, font_size);
        p++;
    }
    
    return total_width;
}
