// radiant/pdf/pdf_fonts.h
// PDF font handling and embedded font extraction

#ifndef PDF_FONTS_H
#define PDF_FONTS_H

#include "../../lib/mempool.h"
#include "../../lambda/lambda-data.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// FreeType forward declarations - using void* to avoid FreeType header dependency
typedef struct FT_FaceRec_* FT_Face;
typedef struct FT_LibraryRec_* FT_Library;

// Forward declaration for FontProp
struct FontProp;

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
 * PDF Font Encoding Types
 */
typedef enum {
    PDF_ENCODING_STANDARD = 0,     // StandardEncoding (default for Type 1)
    PDF_ENCODING_MAC_ROMAN,        // MacRomanEncoding
    PDF_ENCODING_WIN_ANSI,         // WinAnsiEncoding  
    PDF_ENCODING_PDF_DOC,          // PDFDocEncoding
    PDF_ENCODING_MACEXPERT,        // MacExpertEncoding
    PDF_ENCODING_IDENTITY_H,       // Identity-H (CID)
    PDF_ENCODING_SYMBOL,           // Symbol font encoding
    PDF_ENCODING_ZAPFDINGBATS,     // ZapfDingbats font encoding
    PDF_ENCODING_CUSTOM            // Custom Differences encoding
} PDFEncodingType;

/**
 * PDF Font Entry - cached font information
 */
typedef struct PDFFontEntry {
    char* name;                  // Font reference name (e.g., "F1")
    char* base_font;             // BaseFont name (e.g., "Helvetica")
    PDFFontType type;            // Font type
    PDFEncodingType encoding;    // Encoding type
    
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
typedef struct PDFFontCache {
    PDFFontEntry* fonts;
    int count;
    Pool* pool;
    FT_Library ft_library;
} PDFFontCache;

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
 * @param pdf_data Root PDF data for resolving indirect references (optional)
 * @return Cached font entry
 */
PDFFontEntry* pdf_font_cache_add(PDFFontCache* cache, const char* ref_name, 
                                  Map* font_dict, Input* input, Map* pdf_data);

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

/**
 * Decode PDF text using ToUnicode CMap
 * Converts character codes to Unicode string
 * 
 * @param entry Font entry with ToUnicode mapping
 * @param input_text Raw text from PDF (character codes)
 * @param input_len Length of input text
 * @param output_buf Buffer for decoded UTF-8 output
 * @param output_size Size of output buffer
 * @return Length of decoded string, or -1 on error
 */
int pdf_font_decode_text(PDFFontEntry* entry, const char* input_text, int input_len,
                         char* output_buf, int output_size);

/**
 * Check if font has ToUnicode mapping
 */
bool pdf_font_has_tounicode(PDFFontEntry* entry);

/**
 * Check if font needs text decoding (has ToUnicode or special encoding)
 */
bool pdf_font_needs_decoding(PDFFontEntry* entry);

// Font style functions - implementation in fonts.cpp
// Note: These are not declared here to avoid CssEnum dependency issues
// They are accessed through direct inclusion of view.hpp or css_value.hpp

// Basic functions from fonts.cpp
const char* map_pdf_font_to_system(const char* pdf_font);
struct FontProp* create_font_from_pdf(Pool* pool, const char* font_name, double font_size);
float estimate_text_width(const char* text, float font_size);
float get_font_baseline_offset(float font_size);

#ifdef __cplusplus
}
#endif

#endif // PDF_FONTS_H
