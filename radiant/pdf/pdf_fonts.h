// radiant/pdf/pdf_fonts.h
// PDF font handling and embedded font extraction

#ifndef PDF_FONTS_H
#define PDF_FONTS_H

#include "../../lib/mempool.h"
#include "../../lambda/lambda-data.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct PDFFontEntry PDFFontEntry;
typedef struct PDFFontCache PDFFontCache;
typedef struct FT_FaceRec_* FT_Face;

/**
 * PDF Font Types
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
 * Initialize FreeType library for PDF font loading
 */
bool pdf_font_init_freetype();

/**
 * Cleanup FreeType library
 */
void pdf_font_cleanup_freetype();

/**
 * Create a font cache for a document
 */
PDFFontCache* pdf_font_cache_create(Pool* pool);

/**
 * Add font to cache from PDF Resources
 * @param cache Font cache
 * @param ref_name Font reference name (e.g., "F1")
 * @param font_dict Font dictionary from PDF
 * @param input Input context
 * @return Cached font entry
 */
PDFFontEntry* pdf_font_cache_add(PDFFontCache* cache, const char* ref_name, 
                                  Map* font_dict, Input* input);

/**
 * Get font entry from cache
 */
PDFFontEntry* pdf_font_cache_get(PDFFontCache* cache, const char* ref_name);

/**
 * Detect font type from PDF font dictionary
 */
PDFFontType pdf_font_detect_type(Map* font_dict, Input* input);

/**
 * Load embedded font into FreeType
 */
FT_Face pdf_font_load_embedded(PDFFontCache* cache, unsigned char* font_data, 
                                size_t font_data_len, PDFFontType font_type);

/**
 * Create FontProp from cached font entry
 */
struct FontProp* create_font_from_cache_entry(Pool* pool, PDFFontEntry* entry, double font_size);

/**
 * Get glyph width from cached font entry
 */
float pdf_font_get_glyph_width(PDFFontEntry* entry, int char_code, float font_size);

/**
 * Calculate text width using cached font
 */
float pdf_font_calculate_text_width(PDFFontEntry* entry, const char* text, float font_size);

// Existing functions from fonts.cpp
const char* map_pdf_font_to_system(const char* pdf_font);
CssEnum get_font_weight_from_name(const char* pdf_font);
CssEnum get_font_style_from_name(const char* pdf_font);
FontProp* create_font_from_pdf(Pool* pool, const char* font_name, double font_size);
float estimate_text_width(const char* text, float font_size);
float get_font_baseline_offset(float font_size);

#ifdef __cplusplus
}
#endif

#endif // PDF_FONTS_H
