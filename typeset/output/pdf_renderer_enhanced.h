#ifndef PDF_RENDERER_ENHANCED_H
#define PDF_RENDERER_ENHANCED_H

#include "pdf_renderer.h"
#include "../view/view_tree.h"
#ifndef _WIN32
#include <hpdf.h>
#endif
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _WIN32
// Enhanced PDF renderer with advanced layout and typography support

// Advanced font management
typedef struct {
    HPDF_Font regular;
    HPDF_Font bold;
    HPDF_Font italic;
    HPDF_Font bold_italic;
    char* family_name;
} PDFFontFamily;

typedef struct {
    PDFFontFamily serif;        // Times, Computer Modern Serif
    PDFFontFamily sans_serif;   // Helvetica, Computer Modern Sans
    PDFFontFamily monospace;    // Courier, Computer Modern Typewriter
    PDFFontFamily math;         // Math fonts
} PDFFontRegistry;

// Enhanced text rendering context
typedef struct {
    double x, y;                // Current position
    double line_height;         // Current line height
    double paragraph_indent;    // First line indent
    double left_margin;         // Left margin
    double right_margin;        // Right margin
    double available_width;     // Available text width
    bool at_paragraph_start;    // At start of paragraph
    int alignment;              // Text alignment
} PDFTextContext;

// Enhanced list rendering context
typedef struct {
    int type;                   // List type (itemize, enumerate, description)
    int level;                  // Nesting level
    int item_number;            // Current item number (for enumerate)
    double indent;              // List indentation
    double bullet_width;        // Width reserved for bullet/number
    char* bullet_style;         // Bullet character or numbering style
} PDFListContext;

// Enhanced table rendering context
typedef struct {
    int rows;                   // Number of rows
    int cols;                   // Number of columns
    double* col_widths;         // Column widths
    double* row_heights;        // Row heights
    double cell_padding;        // Cell padding
    bool has_borders;           // Draw cell borders
} PDFTableContext;

// Enhanced PDF renderer structure
typedef struct PDFRendererEnhanced {
    PDFRenderer base;           // Base renderer
    
    // Enhanced font management
    PDFFontRegistry fonts;      // Font registry
    
    // Rendering contexts
    PDFTextContext text_ctx;    // Text rendering context
    PDFListContext list_ctx;    // List rendering context
    PDFTableContext table_ctx;  // Table rendering context
    
    // Advanced layout state
    double page_width;          // Current page width
    double page_height;         // Current page height
    double content_x;           // Content area X
    double content_y;           // Content area Y
    double content_width;       // Content area width
    double content_height;      // Content area height
    
    // Typography settings
    double base_font_size;      // Base font size
    double line_spacing;        // Line spacing multiplier
    double paragraph_spacing;   // Inter-paragraph spacing
    
    // Math rendering
    bool math_mode;             // Currently in math mode
    double math_axis_height;    // Mathematical axis position
    
} PDFRendererEnhanced;

// Enhanced PDF renderer creation and management
PDFRendererEnhanced* pdf_renderer_enhanced_create(PDFRenderOptions* options);
void pdf_renderer_enhanced_destroy(PDFRendererEnhanced* renderer);

// Enhanced font management
bool pdf_load_enhanced_fonts(PDFRendererEnhanced* renderer);
HPDF_Font pdf_get_font_variant(PDFRendererEnhanced* renderer, const char* family, 
                               bool bold, bool italic);
bool pdf_set_font_enhanced(PDFRendererEnhanced* renderer, const char* family, 
                          double size, bool bold, bool italic);

// Enhanced text rendering
bool pdf_render_text_run_enhanced(PDFRendererEnhanced* renderer, ViewTextRun* text_run);
bool pdf_render_paragraph_enhanced(PDFRendererEnhanced* renderer, ViewNode* paragraph);
bool pdf_render_section_heading_enhanced(PDFRendererEnhanced* renderer, ViewNode* heading, int level);

// Enhanced list rendering
bool pdf_render_list_enhanced(PDFRendererEnhanced* renderer, ViewNode* list);
bool pdf_render_list_item_enhanced(PDFRendererEnhanced* renderer, ViewNode* item, PDFListContext* ctx);
bool pdf_draw_list_bullet(PDFRendererEnhanced* renderer, PDFListContext* ctx, double x, double y);

// Enhanced table rendering
bool pdf_render_table_enhanced(PDFRendererEnhanced* renderer, ViewNode* table);
bool pdf_render_table_row_enhanced(PDFRendererEnhanced* renderer, ViewNode* row, PDFTableContext* ctx);
bool pdf_render_table_cell_enhanced(PDFRendererEnhanced* renderer, ViewNode* cell, 
                                   PDFTableContext* ctx, int row, int col);

// Enhanced math rendering
bool pdf_render_math_enhanced(PDFRendererEnhanced* renderer, ViewMathElement* math);
bool pdf_render_math_fraction_enhanced(PDFRendererEnhanced* renderer, ViewMathElement* fraction);
bool pdf_render_math_superscript_enhanced(PDFRendererEnhanced* renderer, ViewMathElement* superscript);
bool pdf_render_math_subscript_enhanced(PDFRendererEnhanced* renderer, ViewMathElement* subscript);

// Enhanced layout and positioning
void pdf_calculate_text_layout_enhanced(PDFRendererEnhanced* renderer, ViewNode* text_node);
void pdf_advance_position_enhanced(PDFRendererEnhanced* renderer, double dx, double dy);
void pdf_new_line_enhanced(PDFRendererEnhanced* renderer);
void pdf_new_paragraph_enhanced(PDFRendererEnhanced* renderer);
bool pdf_check_page_break_enhanced(PDFRendererEnhanced* renderer, double needed_height);

// Enhanced view tree rendering
bool pdf_render_view_tree_enhanced(PDFRendererEnhanced* renderer, ViewTree* tree);
bool pdf_render_view_node_enhanced(PDFRendererEnhanced* renderer, ViewNode* node);
bool pdf_render_page_enhanced(PDFRendererEnhanced* renderer, ViewPage* page);

// Enhanced document structure rendering
bool pdf_render_document_enhanced(PDFRendererEnhanced* renderer, ViewNode* document);
bool pdf_render_title_page_enhanced(PDFRendererEnhanced* renderer, ViewTree* tree);
bool pdf_render_table_of_contents_enhanced(PDFRendererEnhanced* renderer, ViewTree* tree);

// Utility functions
double pdf_measure_text_width_enhanced(PDFRendererEnhanced* renderer, const char* text, 
                                      const char* font_family, double font_size, 
                                      bool bold, bool italic);
double pdf_get_font_line_height_enhanced(PDFRendererEnhanced* renderer, double font_size);
void pdf_set_text_alignment_enhanced(PDFRendererEnhanced* renderer, int alignment);

#endif // !_WIN32

#ifdef __cplusplus
}
#endif

#endif // PDF_RENDERER_ENHANCED_H
