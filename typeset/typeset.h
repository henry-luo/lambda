#ifndef TYPESET_H
#define TYPESET_H

#include "../lambda/lambda.h"
#include "../lib/strbuf.h"
#include <stdbool.h>
#include <stdint.h>

// Forward declarations
typedef struct Document Document;
typedef struct TypesetEngine TypesetEngine;
typedef struct ViewTree ViewTree;
typedef struct ViewNode ViewNode;
typedef struct ViewRenderer ViewRenderer;
typedef struct FontManager FontManager;
typedef struct StyleSheet StyleSheet;
typedef struct PageSettings PageSettings;

// Include all module headers
#include "view/view_tree.h"
#include "document/document.h"
#include "document/page.h"
#include "style/font.h"
#include "style/style.h"
#include "layout/layout.h"
#include "math/math_layout.h"
#include "output/renderer.h"
#include "serialization/lambda_serializer.h"
#include "integration/lambda_bridge.h"
#include "integration/stylesheet.h"

// Typesetting options
typedef struct TypesetOptions {
    // Page settings
    ViewSize page_size;         // Page size (A4 default: 595.276 x 841.89 points)
    ViewRect margins;           // Page margins
    bool landscape;             // Landscape orientation
    
    // Typography
    char* default_font_family;  // Default font family
    double default_font_size;   // Default font size (12pt default)
    double line_height;         // Line height multiplier (1.2 default)
    double paragraph_spacing;   // Paragraph spacing
    
    // Layout
    double column_width;        // Column width (0 for auto)
    int column_count;           // Number of columns (1 default)
    double column_gap;          // Gap between columns
    
    // Math settings
    double math_scale;          // Math scale factor (1.0 default)
    bool inline_simple_math;    // Render simple math inline
    
    // Quality settings
    double text_quality;        // Text rendering quality (0.0-1.0)
    bool optimize_layout;       // Optimize layout for performance
    bool enable_hyphenation;    // Enable hyphenation
    
    // Debug options
    bool show_debug_info;       // Show debug information
    bool validate_output;       // Validate output
} TypesetOptions;

// Main typesetting engine
struct TypesetEngine {
    FontManager* font_manager;      // Font management
    StyleSheet* default_stylesheet; // Default stylesheet
    PageSettings* default_page_settings; // Default page settings
    Context* lambda_context;        // Lambda context
    
    // Layout engine
    struct LayoutEngine* layout_engine;
    
    // Math engine
    struct MathEngine* math_engine;
    
    // Statistics
    struct {
        int documents_processed;
        int pages_generated;
        double total_layout_time;
        size_t memory_usage;
    } stats;
};

// Core API functions
TypesetEngine* typeset_engine_create(Context* ctx);
void typeset_engine_destroy(TypesetEngine* engine);

// Main typesetting functions - produces device-independent view tree
ViewTree* typeset_create_view_tree(TypesetEngine* engine, Item content, TypesetOptions* options);

// Convenience functions for different input types
ViewTree* typeset_markdown_to_view_tree(TypesetEngine* engine, const char* markdown, TypesetOptions* options);
ViewTree* typeset_latex_to_view_tree(TypesetEngine* engine, const char* latex, TypesetOptions* options);
ViewTree* typeset_html_to_view_tree(TypesetEngine* engine, const char* html, TypesetOptions* options);
ViewTree* typeset_math_to_view_tree(TypesetEngine* engine, const char* math, TypesetOptions* options);

// Document processing (legacy interface - creates view tree internally)
Document* typeset_from_lambda_item(TypesetEngine* engine, Item root_item);

// View tree serialization
Item view_tree_to_lambda_element(Context* ctx, ViewTree* tree, SerializationOptions* options);
StrBuf* view_tree_to_markdown(ViewTree* tree, MarkdownSerializationOptions* options);

// Multi-format rendering from view tree
StrBuf* render_view_tree_to_html(ViewTree* tree, HTMLRenderOptions* options);
StrBuf* render_view_tree_to_svg(ViewTree* tree, SVGRenderOptions* options);
StrBuf* render_view_tree_to_tex(ViewTree* tree, TeXRenderOptions* options);
bool render_view_tree_to_pdf_file(ViewTree* tree, const char* filename, PDFRenderOptions* options);
bool render_view_tree_to_png_file(ViewTree* tree, const char* filename, PNGRenderOptions* options);

// Generic rendering interface
bool render_view_tree(ViewTree* tree, const char* format, StrBuf* output, ViewRenderOptions* options);

// View tree manipulation
ViewNode* view_tree_find_node_by_id(ViewTree* tree, const char* id);
ViewNode* view_tree_find_node_by_role(ViewTree* tree, const char* role);
void view_tree_apply_transform(ViewTree* tree, ViewTransform* transform);
ViewTree* view_tree_extract_pages(ViewTree* tree, int start_page, int end_page);
ViewTree* view_tree_merge(ViewTree* tree1, ViewTree* tree2);

// View tree analysis
ViewStats* view_tree_calculate_stats(ViewTree* tree);
ViewRect view_tree_get_bounding_box(ViewTree* tree);
double view_tree_get_total_text_length(ViewTree* tree);

// Options management
TypesetOptions* typeset_options_create_default(void);
void typeset_options_destroy(TypesetOptions* options);
TypesetOptions* typeset_options_copy(TypesetOptions* options);

// Lambda function integration
Item fn_typeset(Context* ctx, Item* args, int arg_count);
Item fn_render(Context* ctx, Item* args, int arg_count);
Item fn_view_tree_query(Context* ctx, Item* args, int arg_count);
Item fn_view_tree_transform(Context* ctx, Item* args, int arg_count);

// Error handling
typedef struct TypesetError {
    char* message;              // Error message
    Item problematic_item;      // Item that caused error
    int line_number;            // Line number (if available)
    int column_number;          // Column number (if available)
    struct TypesetError* next;  // Next error in chain
} TypesetError;

TypesetError* typeset_get_last_error(TypesetEngine* engine);
void typeset_clear_errors(TypesetEngine* engine);

#endif // TYPESET_H
Document* typeset_from_markdown(TypesetEngine* engine, const char* markdown);
Document* typeset_from_latex(TypesetEngine* engine, const char* latex);
Document* typeset_math_expression(TypesetEngine* engine, const char* math);

// Output generation
DocumentOutput* render_document_to_svg(TypesetEngine* engine, Document* doc);
void save_document_as_svg_pages(DocumentOutput* output, const char* base_filename);

// Configuration
void typeset_set_page_settings(TypesetEngine* engine, PageSettings* settings);
void typeset_set_default_font(TypesetEngine* engine, const char* font_family, float size);
void typeset_apply_stylesheet(TypesetEngine* engine, Document* doc, const char* css_like_rules);

// Lambda function integration
Item fn_typeset(Context* ctx, Item* args, int arg_count);

// Common constants
#define TYPESET_DEFAULT_PAGE_WIDTH 595.276f    // A4 width in points
#define TYPESET_DEFAULT_PAGE_HEIGHT 841.89f    // A4 height in points
#define TYPESET_DEFAULT_MARGIN 72.0f           // 1 inch in points
#define TYPESET_DEFAULT_FONT_SIZE 12.0f
#define TYPESET_DEFAULT_LINE_HEIGHT 1.2f

// Units conversion (everything internal is in points)
#define POINTS_PER_INCH 72.0f
#define POINTS_PER_MM 2.834645669f
#define POINTS_PER_CM 28.34645669f

// Paper sizes in points
#define PAPER_A4_WIDTH 595.276f
#define PAPER_A4_HEIGHT 841.89f
#define PAPER_LETTER_WIDTH 612.0f
#define PAPER_LETTER_HEIGHT 792.0f
#define PAPER_LEGAL_WIDTH 612.0f
#define PAPER_LEGAL_HEIGHT 1008.0f

#endif // TYPESET_H
