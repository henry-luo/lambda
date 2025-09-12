#ifndef PDF_RENDERER_H
#define PDF_RENDERER_H

#include "renderer.h"
#include "../view/view_tree.h"
#include <hpdf.h>
#include <stdbool.h>

// PDF renderer structure
typedef struct PDFRenderer {
    ViewRenderer base;          // Base renderer interface
    HPDF_Doc pdf_doc;          // libharu PDF document
    HPDF_Page current_page;    // Current page being rendered
    PDFRenderOptions* options; // PDF-specific options
    
    // Font management
    HPDF_Font default_font;    // Default font
    HPDF_Font current_font;    // Current font
    
    // State tracking
    double current_x;          // Current text position X
    double current_y;          // Current text position Y
    double line_height;        // Current line height
    bool page_started;         // Whether we have started a page
    
    // Error handling
    char* last_error;          // Last error message
} PDFRenderer;

// PDF renderer creation and destruction
PDFRenderer* pdf_renderer_create(PDFRenderOptions* options);
void pdf_renderer_destroy(PDFRenderer* renderer);

// Main rendering functions
bool pdf_render_view_tree(PDFRenderer* renderer, ViewTree* tree);
bool pdf_save_to_file(PDFRenderer* renderer, const char* filename);

// Internal rendering functions
bool pdf_render_page(PDFRenderer* renderer, ViewPage* page);
bool pdf_render_node(PDFRenderer* renderer, ViewNode* node);
bool pdf_render_text_run(PDFRenderer* renderer, ViewTextRun* text_run);
bool pdf_render_math_element(PDFRenderer* renderer, ViewMathElement* math);
bool pdf_render_geometry(PDFRenderer* renderer, ViewGeometry* geometry);

// Page management
bool pdf_start_page(PDFRenderer* renderer, double width, double height);
bool pdf_end_page(PDFRenderer* renderer);

// Font management
bool pdf_set_font(PDFRenderer* renderer, const char* font_name, double size);
HPDF_Font pdf_get_font(PDFRenderer* renderer, const char* font_name);

// Coordinate system utilities
double pdf_convert_y(PDFRenderer* renderer, double y);
void pdf_set_position(PDFRenderer* renderer, double x, double y);

// Error handling
void pdf_error_handler(HPDF_STATUS error_no, HPDF_STATUS detail_no, void* user_data);
const char* pdf_get_last_error(PDFRenderer* renderer);

#endif // PDF_RENDERER_H
