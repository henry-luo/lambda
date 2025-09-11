#ifndef LATEX_TYPESET_H
#define LATEX_TYPESET_H

#include "typeset.h"
#include "integration/latex_bridge.h"

// LaTeX-specific typeset entry points - separate from main typeset pipeline
// These functions provide LaTeX-specific typesetting without affecting existing flows

// Main LaTeX typeset function - primary entry point
ViewTree* typeset_latex_to_view_tree(TypesetEngine* engine, Item latex_ast, TypesetOptions* options);

// LaTeX to PDF pipeline (new functionality)
bool typeset_latex_to_pdf(TypesetEngine* engine, Item latex_ast, const char* output_path, TypesetOptions* options);

// LaTeX to other formats (extensible)
bool typeset_latex_to_svg(TypesetEngine* engine, Item latex_ast, const char* output_path, TypesetOptions* options);
bool typeset_latex_to_html(TypesetEngine* engine, Item latex_ast, const char* output_path, TypesetOptions* options);

// LaTeX input validation and preprocessing
bool validate_latex_ast(Item latex_ast);
Item preprocess_latex_ast(Item latex_ast); // Optional preprocessing step

// LaTeX-specific options (extends TypesetOptions)
typedef struct {
    TypesetOptions base;        // Base typeset options
    
    // LaTeX-specific settings
    bool process_citations;     // Process \cite commands
    bool process_references;    // Process \ref commands  
    bool process_bibliography;  // Process bibliography
    bool generate_toc;          // Generate table of contents
    bool number_sections;       // Number sections automatically
    bool number_equations;      // Number equations automatically
    
    // Math rendering
    bool render_math_inline;    // Render inline math
    bool render_math_display;   // Render display math
    char* math_font;           // Font for math rendering
    
    // Bibliography settings
    char* bibliography_style;  // e.g., "plain", "alpha", "abbrv"
    char* citation_style;      // Citation format
    
    // Output quality
    double pdf_dpi;            // DPI for PDF output
    bool optimize_fonts;       // Optimize font embedding
    bool compress_images;      // Compress embedded images
} LatexTypesetOptions;

// LaTeX options management
LatexTypesetOptions* latex_typeset_options_create_default(void);
void latex_typeset_options_destroy(LatexTypesetOptions* options);
LatexTypesetOptions* latex_typeset_options_from_document_class(const char* document_class);

// LaTeX document analysis
typedef struct {
    bool has_title_page;
    bool has_abstract;
    bool has_toc;
    bool has_bibliography;
    bool has_index;
    int section_count;
    int figure_count;
    int table_count;
    int equation_count;
    int citation_count;
    int page_estimate;
} LatexDocumentAnalysis;

LatexDocumentAnalysis* analyze_latex_document(Item latex_ast);
void latex_document_analysis_destroy(LatexDocumentAnalysis* analysis);

// LaTeX error reporting (specific to LaTeX processing)
typedef enum {
    LATEX_ERROR_NONE,
    LATEX_ERROR_INVALID_AST,
    LATEX_ERROR_UNKNOWN_COMMAND,
    LATEX_ERROR_MISSING_ARGUMENT,
    LATEX_ERROR_INVALID_ENVIRONMENT,
    LATEX_ERROR_MATH_ERROR,
    LATEX_ERROR_REFERENCE_ERROR,
    LATEX_ERROR_CITATION_ERROR,
    LATEX_ERROR_PACKAGE_ERROR,
    LATEX_ERROR_LAYOUT_ERROR,
    LATEX_ERROR_FONT_ERROR,
    LATEX_ERROR_IMAGE_ERROR,
    LATEX_ERROR_OUTPUT_ERROR
} LatexErrorType;

typedef struct {
    LatexErrorType type;
    char* message;
    Item problematic_element;
    int line_number;
    int column_number;
    char* suggestion;           // Suggested fix
} LatexError;

// LaTeX error handling
LatexError* latex_error_create(LatexErrorType type, const char* message, Item element);
void latex_error_destroy(LatexError* error);
void latex_error_print(LatexError* error);

// LaTeX debug and profiling
typedef struct {
    double parsing_time;
    double conversion_time;
    double layout_time;
    double rendering_time;
    double total_time;
    size_t memory_used;
    int nodes_created;
    int pages_generated;
} LatexProcessingStats;

LatexProcessingStats* latex_processing_stats_create(void);
void latex_processing_stats_destroy(LatexProcessingStats* stats);
void latex_processing_stats_print(LatexProcessingStats* stats);

// LaTeX testing utilities (for validation against reference PDFs)
bool latex_compare_with_reference(const char* generated_pdf, const char* reference_pdf, double tolerance);
bool latex_run_test_suite(const char* test_directory);

#endif // LATEX_TYPESET_H
