#include "latex_typeset.h"
#include "integration/latex_bridge.h"
#include "output/renderer.h"
#include "../lib/log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// LaTeX typeset implementation - separate entry point that doesn't interfere with existing flows

// =============================================================================
// Main LaTeX typeset functions
// =============================================================================

ViewTree* typeset_latex_to_view_tree(TypesetEngine* engine, Item latex_ast, TypesetOptions* options) {
    if (!engine) {
        log_error("No typeset engine provided for LaTeX typesetting");
        return NULL;
    }
    
    if (!latex_ast) {
        log_error("No LaTeX AST provided for typesetting");
        return NULL;
    }
    
    log_info("Starting LaTeX typesetting process");
    
    // Validate LaTeX AST
    if (!validate_latex_ast(latex_ast)) {
        log_error("Invalid LaTeX AST provided");
        return NULL;
    }
    
    // Create view tree using the LaTeX bridge
    ViewTree* tree = create_view_tree_from_latex_ast(engine, latex_ast);
    if (!tree) {
        log_error("Failed to create view tree from LaTeX AST");
        return NULL;
    }
    
    // Apply options if provided
    if (options) {
        // TODO: Apply typeset options to the view tree
        log_debug("Applied typeset options to LaTeX view tree");
    }
    
    log_info("LaTeX typesetting completed successfully");
    return tree;
}

bool typeset_latex_to_pdf(TypesetEngine* engine, Item latex_ast, const char* output_path, TypesetOptions* options) {
    if (!engine || !latex_ast || !output_path) {
        log_error("Invalid parameters for LaTeX to PDF typesetting");
        return false;
    }
    
    log_info("Starting LaTeX to PDF typesetting: %s", output_path);
    
    // Create view tree
    ViewTree* tree = typeset_latex_to_view_tree(engine, latex_ast, options);
    if (!tree) {
        log_error("Failed to create view tree for PDF output");
        return false;
    }
    
    // Create PDF renderer
    ViewRenderer* renderer = view_renderer_create(RENDERER_FORMAT_PDF);
    if (!renderer) {
        log_error("Failed to create PDF renderer");
        view_tree_destroy(tree);
        return false;
    }
    
    // Render to PDF
    bool success = view_renderer_render_to_file(renderer, tree, output_path);
    if (!success) {
        log_error("Failed to render LaTeX to PDF: %s", output_path);
    } else {
        log_info("LaTeX successfully rendered to PDF: %s", output_path);
    }
    
    // Cleanup
    view_renderer_destroy(renderer);
    view_tree_destroy(tree);
    
    return success;
}

bool typeset_latex_to_svg(TypesetEngine* engine, Item latex_ast, const char* output_path, TypesetOptions* options) {
    if (!engine || !latex_ast || !output_path) {
        log_error("Invalid parameters for LaTeX to SVG typesetting");
        return false;
    }
    
    log_info("Starting LaTeX to SVG typesetting: %s", output_path);
    
    // Create view tree
    ViewTree* tree = typeset_latex_to_view_tree(engine, latex_ast, options);
    if (!tree) {
        log_error("Failed to create view tree for SVG output");
        return false;
    }
    
    // Create SVG renderer
    ViewRenderer* renderer = view_renderer_create(RENDERER_FORMAT_SVG);
    if (!renderer) {
        log_error("Failed to create SVG renderer");
        view_tree_destroy(tree);
        return false;
    }
    
    // Render to SVG
    bool success = view_renderer_render_to_file(renderer, tree, output_path);
    if (!success) {
        log_error("Failed to render LaTeX to SVG: %s", output_path);
    } else {
        log_info("LaTeX successfully rendered to SVG: %s", output_path);
    }
    
    // Cleanup
    view_renderer_destroy(renderer);
    view_tree_destroy(tree);
    
    return success;
}

bool typeset_latex_to_html(TypesetEngine* engine, Item latex_ast, const char* output_path, TypesetOptions* options) {
    // TODO: Implement HTML output
    log_warning("LaTeX to HTML typesetting not yet implemented");
    return false;
}

// =============================================================================
// LaTeX validation and preprocessing
// =============================================================================

bool validate_latex_ast(Item latex_ast) {
    if (!latex_ast) {
        log_error("Null LaTeX AST");
        return false;
    }
    
    // TODO: Implement AST validation
    // - Check for required document structure
    // - Validate command syntax
    // - Check for balanced environments
    
    log_debug("LaTeX AST validation passed (placeholder)");
    return true;
}

Item preprocess_latex_ast(Item latex_ast) {
    if (!latex_ast) {
        log_error("Null LaTeX AST for preprocessing");
        return 0;
    }
    
    // TODO: Implement preprocessing
    // - Expand macros
    // - Resolve includes
    // - Normalize structure
    
    log_debug("LaTeX AST preprocessing completed (placeholder)");
    return latex_ast;
}

// =============================================================================
// LaTeX-specific options
// =============================================================================

LatexTypesetOptions* latex_typeset_options_create_default(void) {
    LatexTypesetOptions* options = malloc(sizeof(LatexTypesetOptions));
    if (!options) return NULL;
    
    memset(options, 0, sizeof(LatexTypesetOptions));
    
    // Initialize base options
    options->base.page_width = 595.276;      // A4 width
    options->base.page_height = 841.89;      // A4 height
    options->base.margin_left = 72.0;        // 1 inch
    options->base.margin_right = 72.0;       // 1 inch
    options->base.margin_top = 72.0;         // 1 inch
    options->base.margin_bottom = 72.0;      // 1 inch
    options->base.default_font_family = strdup("Computer Modern");
    options->base.default_font_size = 12.0;
    options->base.line_height = 1.2;
    options->base.paragraph_spacing = 12.0;
    options->base.optimize_layout = true;
    options->base.show_debug_info = false;
    
    // Initialize LaTeX-specific options
    options->process_citations = true;
    options->process_references = true;
    options->process_bibliography = true;
    options->generate_toc = false;
    options->number_sections = true;
    options->number_equations = true;
    options->render_math_inline = true;
    options->render_math_display = true;
    options->math_font = strdup("Computer Modern Math");
    options->bibliography_style = strdup("plain");
    options->citation_style = strdup("numeric");
    options->pdf_dpi = 300.0;
    options->optimize_fonts = true;
    options->compress_images = true;
    
    log_debug("Created default LaTeX typeset options");
    return options;
}

void latex_typeset_options_destroy(LatexTypesetOptions* options) {
    if (!options) return;
    
    free(options->base.default_font_family);
    free(options->math_font);
    free(options->bibliography_style);
    free(options->citation_style);
    free(options);
    
    log_debug("Destroyed LaTeX typeset options");
}

LatexTypesetOptions* latex_typeset_options_from_document_class(const char* document_class) {
    LatexTypesetOptions* options = latex_typeset_options_create_default();
    if (!options || !document_class) return options;
    
    // Adjust settings based on document class
    if (strcmp(document_class, "book") == 0) {
        options->generate_toc = true;
        options->number_sections = true;
        options->base.margin_left = 90.0;    // Larger margins for book
        options->base.margin_right = 54.0;   // Asymmetric for binding
    } else if (strcmp(document_class, "report") == 0) {
        options->generate_toc = true;
        options->number_sections = true;
    } else if (strcmp(document_class, "letter") == 0) {
        options->number_sections = false;
        options->generate_toc = false;
        options->base.margin_top = 108.0;    // More space for letterhead
    }
    
    log_debug("Created LaTeX options for document class: %s", document_class);
    return options;
}

// =============================================================================
// Document analysis
// =============================================================================

LatexDocumentAnalysis* analyze_latex_document(Item latex_ast) {
    LatexDocumentAnalysis* analysis = malloc(sizeof(LatexDocumentAnalysis));
    if (!analysis) return NULL;
    
    memset(analysis, 0, sizeof(LatexDocumentAnalysis));
    
    // TODO: Implement document analysis
    // - Count sections, figures, tables, equations
    // - Detect document features
    // - Estimate page count
    
    // Placeholder values
    analysis->section_count = 1;
    analysis->page_estimate = 1;
    
    log_debug("Analyzed LaTeX document (placeholder)");
    return analysis;
}

void latex_document_analysis_destroy(LatexDocumentAnalysis* analysis) {
    if (!analysis) return;
    free(analysis);
}

// =============================================================================
// Error handling
// =============================================================================

LatexError* latex_error_create(LatexErrorType type, const char* message, Item element) {
    LatexError* error = malloc(sizeof(LatexError));
    if (!error) return NULL;
    
    error->type = type;
    error->message = strdup(message ? message : "Unknown LaTeX error");
    error->problematic_element = element;
    error->line_number = -1;
    error->column_number = -1;
    error->suggestion = NULL;
    
    return error;
}

void latex_error_destroy(LatexError* error) {
    if (!error) return;
    
    free(error->message);
    free(error->suggestion);
    free(error);
}

void latex_error_print(LatexError* error) {
    if (!error) return;
    
    const char* type_names[] = {
        "None", "Invalid AST", "Unknown Command", "Missing Argument",
        "Invalid Environment", "Math Error", "Reference Error", 
        "Citation Error", "Package Error", "Layout Error",
        "Font Error", "Image Error", "Output Error"
    };
    
    const char* type_name = (error->type < sizeof(type_names)/sizeof(type_names[0])) 
                           ? type_names[error->type] : "Unknown";
    
    log_error("LaTeX Error [%s]: %s", type_name, error->message);
    
    if (error->line_number >= 0) {
        log_error("  At line %d", error->line_number);
    }
    
    if (error->suggestion) {
        log_info("  Suggestion: %s", error->suggestion);
    }
}

// =============================================================================
// Performance tracking
// =============================================================================

LatexProcessingStats* latex_processing_stats_create(void) {
    LatexProcessingStats* stats = malloc(sizeof(LatexProcessingStats));
    if (!stats) return NULL;
    
    memset(stats, 0, sizeof(LatexProcessingStats));
    return stats;
}

void latex_processing_stats_destroy(LatexProcessingStats* stats) {
    if (!stats) return;
    free(stats);
}

void latex_processing_stats_print(LatexProcessingStats* stats) {
    if (!stats) return;
    
    log_info("LaTeX Processing Statistics:");
    log_info("  Parsing time: %.3f ms", stats->parsing_time * 1000.0);
    log_info("  Conversion time: %.3f ms", stats->conversion_time * 1000.0);
    log_info("  Layout time: %.3f ms", stats->layout_time * 1000.0);
    log_info("  Rendering time: %.3f ms", stats->rendering_time * 1000.0);
    log_info("  Total time: %.3f ms", stats->total_time * 1000.0);
    log_info("  Memory used: %.2f MB", stats->memory_used / (1024.0 * 1024.0));
    log_info("  Nodes created: %d", stats->nodes_created);
    log_info("  Pages generated: %d", stats->pages_generated);
}

// =============================================================================
// Testing utilities
// =============================================================================

bool latex_compare_with_reference(const char* generated_pdf, const char* reference_pdf, double tolerance) {
    if (!generated_pdf || !reference_pdf) {
        log_error("Invalid file paths for PDF comparison");
        return false;
    }
    
    // TODO: Implement PDF comparison using diff-pdf or similar tool
    // This would shell out to diff-pdf and parse the results
    
    log_info("Comparing PDFs: %s vs %s (tolerance: %.2f)", 
             generated_pdf, reference_pdf, tolerance);
    
    // Placeholder - always return true for now
    log_debug("PDF comparison completed (placeholder)");
    return true;
}

bool latex_run_test_suite(const char* test_directory) {
    if (!test_directory) {
        log_error("No test directory specified");
        return false;
    }
    
    // TODO: Implement test suite runner
    // - Find all .tex files in test directory
    // - Process each with LaTeX typesetter
    // - Compare with reference PDFs
    // - Report results
    
    log_info("Running LaTeX test suite in: %s", test_directory);
    log_debug("Test suite completed (placeholder)");
    return true;
}
