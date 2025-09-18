#ifndef LATEX_TYPESET_ENHANCED_H
#define LATEX_TYPESET_ENHANCED_H

#include "typeset.h"
#include "latex_typeset.h"
#include "integration/latex_bridge_enhanced.h"
#include "output/pdf_renderer_enhanced.h"

#ifdef __cplusplus
extern "C" {
#endif

// Enhanced LaTeX typesetting functions for Phase 3 - Advanced Layout and Typography

// =============================================================================
// Enhanced Core Typesetting Functions
// =============================================================================

// Enhanced LaTeX to ViewTree conversion with advanced layout processing
ViewTree* typeset_latex_to_view_tree_enhanced(TypesetEngine* engine, Item latex_ast, TypesetOptions* options);

// Enhanced LaTeX to PDF with sophisticated typography and layout
#ifndef _WIN32
bool typeset_latex_to_pdf_enhanced(TypesetEngine* engine, Item latex_ast, const char* output_path, TypesetOptions* options);
#endif

// Enhanced standalone LaTeX processing (main entry point for Phase 3)
bool fn_typeset_latex_enhanced_standalone(const char* input_file, const char* output_file);

// =============================================================================
// Enhanced Options Management
// =============================================================================

// Create enhanced LaTeX typeset options with advanced settings
LatexTypesetOptions* latex_typeset_options_create_enhanced(void);
void latex_typeset_options_destroy_enhanced(LatexTypesetOptions* options);

// =============================================================================
// Advanced Document Analysis
// =============================================================================

// Analyze LaTeX document structure with enhanced capabilities
bool analyze_latex_document_enhanced(Item latex_ast, LatexDocumentStructure** structure_out);

// Quality assessment for enhanced rendering
typedef struct LatexQualityMetrics LatexQualityMetrics;
bool assess_latex_rendering_quality_enhanced(ViewTree* tree, LatexQualityMetrics** metrics_out);
void latex_quality_metrics_destroy(LatexQualityMetrics* metrics);

// =============================================================================
// Phase 3 Advanced Features
// =============================================================================

// Advanced typography features
typedef enum {
    LATEX_TYPOGRAPHY_COMPUTER_MODERN,
    LATEX_TYPOGRAPHY_TIMES,
    LATEX_TYPOGRAPHY_HELVETICA,
    LATEX_TYPOGRAPHY_PALATINO
} LatexTypographyStyle;

// Advanced layout features
typedef enum {
    LATEX_LAYOUT_SINGLE_COLUMN,
    LATEX_LAYOUT_TWO_COLUMN,
    LATEX_LAYOUT_BOOK_STYLE,
    LATEX_LAYOUT_ARTICLE_STYLE
} LatexLayoutStyle;

// Enhanced document processing options
typedef struct {
    LatexTypographyStyle typography_style;
    LatexLayoutStyle layout_style;
    bool enable_advanced_math;      // Enhanced math rendering
    bool enable_complex_tables;     // Advanced table layout
    bool enable_figure_placement;   // Intelligent figure placement
    bool enable_cross_references;   // Cross-reference resolution
    bool enable_bibliography;       // Bibliography processing
    bool enable_index;              // Index generation
    double quality_factor;          // Rendering quality (1.0 = standard, 2.0 = high)
} LatexEnhancedOptions;

// Create and manage enhanced processing options
LatexEnhancedOptions* latex_enhanced_options_create_default(void);
void latex_enhanced_options_destroy(LatexEnhancedOptions* options);

// Process LaTeX with enhanced options
bool typeset_latex_enhanced_with_options(TypesetEngine* engine, Item latex_ast, 
                                       const char* output_path, LatexEnhancedOptions* options);

// =============================================================================
// Phase 3 Quality Verification
// =============================================================================

// PDF verification and comparison
typedef struct {
    bool pdf_valid;                 // PDF file is valid
    long file_size;                 // File size in bytes
    int page_count;                 // Number of pages
    double render_time;             // Time taken to render
    char* quality_assessment;       // Quality assessment message
} LatexPDFVerification;

// Verify generated PDF quality
bool verify_latex_pdf_quality(const char* pdf_path, LatexPDFVerification** verification_out);
void latex_pdf_verification_destroy(LatexPDFVerification* verification);

// Compare PDFs using diff-pdf if available
typedef struct {
    bool pdfs_identical;            // PDFs are identical
    bool diff_available;            // diff-pdf tool available
    char* diff_output_path;         // Path to diff output (if different)
    char* comparison_summary;       // Summary of comparison
} LatexPDFComparison;

bool compare_latex_pdfs(const char* pdf1_path, const char* pdf2_path, LatexPDFComparison** comparison_out);
void latex_pdf_comparison_destroy(LatexPDFComparison* comparison);

// =============================================================================
// Test and Validation Functions
// =============================================================================

// Run comprehensive Phase 3 test suite
bool run_latex_phase3_test_suite(void);

// Test specific enhanced features
bool test_enhanced_typography(void);
bool test_enhanced_math_rendering(void);
bool test_enhanced_list_rendering(void);
bool test_enhanced_table_rendering(void);
bool test_enhanced_cross_references(void);

// Performance benchmarking
typedef struct {
    double parse_time;              // LaTeX parsing time
    double layout_time;             // Layout computation time
    double render_time;             // PDF rendering time
    double total_time;              // Total processing time
    size_t peak_memory;             // Peak memory usage
    int pages_rendered;             // Number of pages rendered
    double pages_per_second;        // Rendering speed
} LatexPerformanceMetrics;

bool benchmark_latex_performance(const char* input_file, LatexPerformanceMetrics** metrics_out);
void latex_performance_metrics_destroy(LatexPerformanceMetrics* metrics);

#ifdef __cplusplus
}
#endif

#endif // LATEX_TYPESET_ENHANCED_H
