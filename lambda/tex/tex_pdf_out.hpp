// tex_pdf_out.hpp - PDF Output Generation for TeX
//
// Converts TeX node trees to PDF using the Lambda pdf_writer library.
// This provides direct PDF generation as an alternative to DVI.
//
// Usage:
//   PDFWriter pdf(&arena);
//   pdf_open(pdf, "output.pdf", PDFParams{});
//   pdf_write_page(pdf, page_vlist, 1, &fonts);
//   pdf_close(pdf);

#ifndef TEX_PDF_OUT_HPP
#define TEX_PDF_OUT_HPP

#include "tex_node.hpp"
#include "tex_tfm.hpp"
#include "tex_vlist.hpp"
#include "tex_pagebreak.hpp"
#include "../../lib/pdf_writer.h"
#include "../../lib/arena.h"
#include <cstdint>

namespace tex {

// ============================================================================
// PDF Output Parameters
// ============================================================================

struct PDFParams {
    // Page dimensions in points (default: US Letter)
    float page_width  = HPDF_PAGE_SIZE_LETTER_WIDTH;
    float page_height = HPDF_PAGE_SIZE_LETTER_HEIGHT;

    // Margins in points (default: 1 inch)
    float margin_left   = 72.0f;
    float margin_right  = 72.0f;
    float margin_top    = 72.0f;
    float margin_bottom = 72.0f;

    // Metadata
    const char* title   = nullptr;
    const char* author  = nullptr;
    const char* subject = nullptr;
    const char* creator = "Lambda Script TeX";

    // Font mapping (TeX font name -> PDF font name)
    // Default uses Base14 fonts as fallback
    const char* default_font = "Times-Roman";
    bool use_compression = true;
};

// ============================================================================
// PDF Font Entry
// ============================================================================

struct PDFFontEntry {
    const char* tex_name;     // TeX/TFM font name (e.g., "cmr10")
    const char* pdf_name;     // PDF font name (e.g., "Times-Roman")
    float size_pt;            // Size in points
    HPDF_Font handle;         // PDF font handle
};

// ============================================================================
// PDF Writer Context
// ============================================================================

struct PDFWriter {
    Arena* arena;

    // PDF document and current page
    HPDF_Doc doc;
    HPDF_Page page;

    // Current position (in PDF coordinates - origin at bottom-left)
    float x, y;

    // Current font
    PDFFontEntry* current_font;
    float current_size;

    // Font table
    PDFFontEntry* fonts;
    int font_count;
    int font_capacity;

    // Parameters
    PDFParams params;

    // Page tracking
    int page_count;

    // Constructor
    PDFWriter(Arena* a) : arena(a), doc(nullptr), page(nullptr),
        x(0), y(0), current_font(nullptr), current_size(0),
        fonts(nullptr), font_count(0), font_capacity(0), page_count(0) {}
};

// ============================================================================
// Font Mapping
// ============================================================================

// Map TeX/Computer Modern font names to PDF Base14 fonts
inline const char* map_tex_font_to_pdf(const char* tex_font) {
    if (!tex_font) return "Times-Roman";

    // Computer Modern mappings (approximate)
    if (strncmp(tex_font, "cmr", 3) == 0)   return "Times-Roman";        // Roman
    if (strncmp(tex_font, "cmbx", 4) == 0)  return "Times-Bold";         // Bold extended
    if (strncmp(tex_font, "cmti", 4) == 0)  return "Times-Italic";       // Text italic
    if (strncmp(tex_font, "cmsl", 4) == 0)  return "Times-Italic";       // Slanted
    if (strncmp(tex_font, "cmss", 4) == 0)  return "Helvetica";          // Sans serif
    if (strncmp(tex_font, "cmtt", 4) == 0)  return "Courier";            // Typewriter
    if (strncmp(tex_font, "cmmi", 4) == 0)  return "Times-Italic";       // Math italic
    if (strncmp(tex_font, "cmsy", 4) == 0)  return "Symbol";             // Math symbols
    if (strncmp(tex_font, "cmex", 4) == 0)  return "Symbol";             // Math extension

    // Latin Modern mappings
    if (strncmp(tex_font, "lmr", 3) == 0)   return "Times-Roman";
    if (strncmp(tex_font, "lmbx", 4) == 0)  return "Times-Bold";
    if (strncmp(tex_font, "lmti", 4) == 0)  return "Times-Italic";
    if (strncmp(tex_font, "lmss", 4) == 0)  return "Helvetica";
    if (strncmp(tex_font, "lmtt", 4) == 0)  return "Courier";

    // Default
    return "Times-Roman";
}

// ============================================================================
// File Management
// ============================================================================

// Open a PDF file for writing
bool pdf_open(PDFWriter& writer, const char* filename, const PDFParams& params = PDFParams{});

// Close the PDF file and free resources
bool pdf_close(PDFWriter& writer);

// ============================================================================
// Page Commands
// ============================================================================

// Begin a new page
void pdf_begin_page(PDFWriter& writer);

// End the current page
void pdf_end_page(PDFWriter& writer);

// ============================================================================
// Font Commands
// ============================================================================

// Define or retrieve a font
HPDF_Font pdf_get_font(PDFWriter& writer, const char* tex_name, float size_pt);

// Select a font for subsequent text
void pdf_select_font(PDFWriter& writer, const char* tex_name, float size_pt);

// ============================================================================
// Drawing Commands
// ============================================================================

// Set position (in TeX coordinates - origin at top-left)
void pdf_set_position(PDFWriter& writer, float x, float y);

// Draw a character at current position
void pdf_draw_char(PDFWriter& writer, int32_t codepoint);

// Draw text at current position
void pdf_draw_text(PDFWriter& writer, const char* text);

// Draw a filled rectangle (rule)
void pdf_draw_rule(PDFWriter& writer, float x, float y, float width, float height);

// Move right by specified amount
void pdf_move_right(PDFWriter& writer, float amount);

// Move down by specified amount (positive moves down in TeX coordinates)
void pdf_move_down(PDFWriter& writer, float amount);

// ============================================================================
// Graphics State
// ============================================================================

// Save graphics state
void pdf_gsave(PDFWriter& writer);

// Restore graphics state
void pdf_grestore(PDFWriter& writer);

// Set fill color
void pdf_set_fill_color(PDFWriter& writer, float r, float g, float b);

// Set stroke color
void pdf_set_stroke_color(PDFWriter& writer, float r, float g, float b);

// ============================================================================
// Node Tree Traversal
// ============================================================================

// Output a single node
void pdf_output_node(PDFWriter& writer, TexNode* node, TFMFontManager* fonts);

// Output a horizontal list
void pdf_output_hlist(PDFWriter& writer, TexNode* hlist, TFMFontManager* fonts);

// Output a vertical list
void pdf_output_vlist(PDFWriter& writer, TexNode* vlist, TFMFontManager* fonts);

// ============================================================================
// High-Level API
// ============================================================================

// Write a single page
bool pdf_write_page(
    PDFWriter& writer,
    TexNode* page_vlist,
    int page_number,
    TFMFontManager* fonts
);

// Write multiple pages from PageContent array
bool pdf_write_document(
    PDFWriter& writer,
    PageContent* pages,
    int page_count,
    TFMFontManager* fonts
);

// ============================================================================
// Convenience Functions
// ============================================================================

// Write complete PDF file from page array
bool write_pdf_file(
    const char* filename,
    PageContent* pages,
    int page_count,
    TFMFontManager* fonts,
    Arena* arena,
    const PDFParams& params = PDFParams{}
);

// Write single page to PDF
bool write_pdf_page(
    const char* filename,
    TexNode* vlist,
    TFMFontManager* fonts,
    Arena* arena,
    const PDFParams& params = PDFParams{}
);

// ============================================================================
// Coordinate Conversion
// ============================================================================

// Convert TeX Y coordinate (origin top-left) to PDF Y (origin bottom-left)
inline float tex_y_to_pdf(float tex_y, float page_height) {
    return page_height - tex_y;
}

// Convert points to PDF units (they're the same)
inline float pt_to_pdf(float pt) { return pt; }

// ============================================================================
// Debugging
// ============================================================================

void dump_pdf_writer_state(const PDFWriter& writer);

} // namespace tex

#endif // TEX_PDF_OUT_HPP
