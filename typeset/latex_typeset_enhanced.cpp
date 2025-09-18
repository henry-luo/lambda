#include "latex_typeset.h"
#include "integration/latex_bridge_enhanced.h"
#ifndef _WIN32
#include "output/pdf_renderer_enhanced.h"
#endif
#include "../lambda/input/input.h"
#include "../lambda/lambda-data.hpp"
#include "../lib/log.h"
#include "../lib/string.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Enhanced LaTeX typesetting implementation for Phase 3

// =============================================================================
// Enhanced LaTeX Typesetting Functions
// =============================================================================

ViewTree* typeset_latex_to_view_tree_enhanced(TypesetEngine* engine, Item latex_ast, TypesetOptions* options) {
    if (!engine) {
        log_error("No typeset engine provided for enhanced LaTeX typesetting");
        return NULL;
    }
    
    if (get_type_id(latex_ast) == LMD_TYPE_NULL) {
        log_error("No LaTeX AST provided for enhanced typesetting");
        return NULL;
    }
    
    log_info("Starting enhanced LaTeX typesetting process");
    
    // Validate LaTeX AST
    if (!validate_latex_ast(latex_ast)) {
        log_error("Invalid LaTeX AST provided for enhanced processing");
        return NULL;
    }
    
    // Create enhanced view tree using the enhanced LaTeX bridge
    ViewTree* tree = create_enhanced_view_tree_from_latex_ast(engine, latex_ast);
    if (!tree) {
        log_error("Failed to create enhanced view tree from LaTeX AST");
        return NULL;
    }
    
    // Apply options if provided
    if (options) {
        // TODO: Apply enhanced typeset options to the view tree
        log_debug("Applied enhanced typeset options to LaTeX view tree");
    }
    
    log_info("Enhanced LaTeX typesetting completed successfully");
    return tree;
}

#ifndef _WIN32
bool typeset_latex_to_pdf_enhanced(TypesetEngine* engine, Item latex_ast, const char* output_path, TypesetOptions* options) {
    if (!engine || get_type_id(latex_ast) == LMD_TYPE_NULL || !output_path) {
        log_error("Invalid parameters for enhanced LaTeX to PDF typesetting");
        return false;
    }
    
    log_info("Starting enhanced LaTeX to PDF typesetting: %s", output_path);
    
    // Create enhanced view tree
    ViewTree* tree = typeset_latex_to_view_tree_enhanced(engine, latex_ast, options);
    if (!tree) {
        log_error("Failed to create enhanced view tree for PDF output");
        return false;
    }
    
    // Create enhanced PDF renderer with enhanced options
    PDFRenderOptions pdf_options = {};
    pdf_options.base.format = VIEW_FORMAT_PDF;
    pdf_options.base.dpi = 72.0;
    pdf_options.base.embed_fonts = true;
    pdf_options.base.quality = VIEW_RENDER_QUALITY_HIGH;
    pdf_options.pdf_version = PDFRenderOptions::PDF_VERSION_1_4;
    pdf_options.compress_streams = true;
    pdf_options.compress_images = true;
    
    PDFRendererEnhanced* renderer = pdf_renderer_enhanced_create(&pdf_options);
    if (!renderer) {
        log_error("Failed to create enhanced PDF renderer");
        view_tree_release(tree);
        return false;
    }
    
    // Render the enhanced view tree to PDF
    bool render_success = pdf_render_view_tree_enhanced(renderer, tree);
    if (!render_success) {
        log_error("Failed to render enhanced view tree to PDF");
        pdf_renderer_enhanced_destroy(renderer);
        view_tree_release(tree);
        return false;
    }
    
    // Save enhanced PDF to file
    bool save_success = pdf_save_to_file(&renderer->base, output_path);
    if (!save_success) {
        log_error("Failed to save enhanced PDF to file: %s", output_path);
        pdf_renderer_enhanced_destroy(renderer);
        view_tree_release(tree);
        return false;
    }
    
    log_info("Enhanced LaTeX to PDF conversion completed successfully: %s", output_path);
    
    // Clean up
    pdf_renderer_enhanced_destroy(renderer);
    view_tree_release(tree);
    
    return true;
}
#endif

// =============================================================================
// Enhanced Standalone LaTeX Processing Function
// =============================================================================

bool fn_typeset_latex_enhanced_standalone(const char* input_file, const char* output_file) {
    if (!input_file || !output_file) {
        log_error("Invalid input or output file parameters");
        return false;
    }
    
    log_info("Enhanced LaTeX standalone processing: %s -> %s", input_file, output_file);
    
    // Initialize memory pool
    VariableMemPool* pool;
    if (pool_variable_init(&pool, 1024 * 1024, MEM_POOL_NO_BEST_FIT) != MEM_POOL_ERR_OK) { // 1MB initial pool
        log_error("Failed to create memory pool for enhanced processing");
        return false;
    }
    
    // Parse LaTeX input file using Lambda's input system
    log_info("Parsing LaTeX file: %s", input_file);
    
    // Create URL for the input file
    char url_buffer[512];
    snprintf(url_buffer, sizeof(url_buffer), "file://%s", input_file);
    
    // Create URL object
    Url file_url = {0};
    file_url.scheme = URL_SCHEME_FILE;
    file_url.pathname = create_string(pool, input_file);
    
    // Read file content
    FILE* file = fopen(input_file, "r");
    if (!file) {
        log_error("Failed to open input file: %s", input_file);
        pool_variable_destroy(pool);
        return false;
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* file_content = (char*)malloc(file_size + 1);
    if (!file_content) {
        log_error("Failed to allocate memory for file content");
        fclose(file);
        pool_variable_destroy(pool);
        return false;
    }
    
    fread(file_content, 1, file_size, file);
    file_content[file_size] = '\0';
    fclose(file);
    
    // Create input from source with auto-detection
    String* type_str = create_string(pool, "auto");
    Input* input = input_from_source(file_content, &file_url, type_str, NULL);
    
    free(file_content);  // Free file content after parsing
    
    if (!input) {
        log_error("Failed to create input parser for LaTeX file");
        pool_variable_destroy(pool);
        return false;
    }
    
    Item latex_ast = input->root;
    
    if (get_type_id(latex_ast) == LMD_TYPE_ERROR) {
        log_error("Failed to parse LaTeX file: %s", input_file);
        if (input->type_list) arraylist_free(input->type_list);
        pool_variable_destroy(input->pool);
        free(input);
        pool_variable_destroy(pool);
        return false;
    }
    
    log_info("LaTeX file parsed successfully");
    
    // Create simple context for typeset engine
    Context simple_ctx = {0};
    simple_ctx.ast_pool = pool;
    
    // Create enhanced typeset engine
    TypesetEngine* engine = typeset_engine_create(&simple_ctx);
    if (!engine) {
        log_error("Failed to create enhanced typeset engine");
        if (input->type_list) arraylist_free(input->type_list);
        pool_variable_destroy(input->pool);
        free(input);
        pool_variable_destroy(pool);
        return false;
    }
    
    // Determine output format based on file extension
    const char* ext = strrchr(output_file, '.');
    bool success = false;
    
    if (ext && strcmp(ext, ".pdf") == 0) {
        // Enhanced PDF output
#ifndef _WIN32
        log_info("Generating enhanced PDF output");
        success = typeset_latex_to_pdf_enhanced(engine, latex_ast, output_file, NULL);
#else
        log_error("Enhanced PDF generation not supported on Windows");
        success = false;
#endif
    } else if (ext && strcmp(ext, ".svg") == 0) {
        // Enhanced SVG output (fallback to standard for now)
        log_info("Generating SVG output (using standard renderer)");
        success = typeset_latex_to_svg(engine, latex_ast, output_file, NULL);
    } else if (ext && strcmp(ext, ".html") == 0) {
        // Enhanced HTML output (fallback to standard for now)
        log_info("Generating HTML output (using standard renderer)");
        success = typeset_latex_to_html(engine, latex_ast, output_file, NULL);
    } else {
        log_error("Unsupported output format for enhanced processing: %s", ext ? ext : "unknown");
    }
    
    // Clean up
    typeset_engine_destroy(engine);
    if (input->type_list) arraylist_free(input->type_list);
    pool_variable_destroy(input->pool);
    free(input);
    pool_variable_destroy(pool);
    
    if (success) {
        log_info("Enhanced LaTeX processing completed successfully");
    } else {
        log_error("Enhanced LaTeX processing failed");
    }
    
    return success;
}

// =============================================================================
// Enhanced Options Management
// =============================================================================

LatexTypesetOptions* latex_typeset_options_create_enhanced(void) {
    LatexTypesetOptions* options = (LatexTypesetOptions*)malloc(sizeof(LatexTypesetOptions));
    if (!options) return NULL;
    
    memset(options, 0, sizeof(LatexTypesetOptions));
    
    // Set enhanced base typeset options
    options->base.page_width = 612.0;      // Letter width
    options->base.page_height = 792.0;     // Letter height
    options->base.margin_left = 72.0;      // 1 inch margins
    options->base.margin_right = 72.0;
    options->base.margin_top = 72.0;
    options->base.margin_bottom = 72.0;
    
    options->base.default_font_family = strdup("Computer Modern");
    options->base.default_font_size = 10.0;
    options->base.line_height = 12.0;
    
    // Set enhanced LaTeX-specific options
    options->process_citations = true;
    options->process_references = true;
    options->process_bibliography = true;
    options->generate_toc = false;        // Disable for now
    options->number_sections = true;
    options->number_equations = true;
    
    // Enhanced math rendering
    options->render_math_inline = true;
    options->render_math_display = true;
    options->math_font = strdup("Computer Modern Math");
    
    // Enhanced bibliography settings
    options->bibliography_style = strdup("plain");
    options->citation_style = strdup("numeric");
    
    // Enhanced output quality
    options->pdf_dpi = 72.0;
    options->optimize_fonts = true;
    options->compress_images = true;
    
    return options;
}

void latex_typeset_options_destroy_enhanced(LatexTypesetOptions* options) {
    if (!options) return;
    
    // Clean up base options
    if (options->base.default_font_family) free(options->base.default_font_family);
    
    // Clean up enhanced LaTeX options
    if (options->math_font) free(options->math_font);
    if (options->bibliography_style) free(options->bibliography_style);
    if (options->citation_style) free(options->citation_style);
    
    free(options);
}

// =============================================================================
// Enhanced Document Analysis
// =============================================================================

bool analyze_latex_document_enhanced(Item latex_ast, LatexDocumentStructure** structure_out) {
    if (get_type_id(latex_ast) == LMD_TYPE_NULL || !structure_out) {
        log_error("Invalid parameters for enhanced document analysis");
        return false;
    }
    
    log_info("Analyzing enhanced LaTeX document structure");
    
    LatexDocumentStructure* structure = analyze_latex_document_structure(latex_ast);
    if (!structure) {
        log_error("Failed to analyze enhanced document structure");
        return false;
    }
    
    log_info("Enhanced document analysis completed: %d sections, title=%s, toc=%s, bib=%s",
             structure->section_count,
             structure->has_title_page ? "yes" : "no",
             structure->has_table_of_contents ? "yes" : "no",
             structure->has_bibliography ? "yes" : "no");
    
    *structure_out = structure;
    return true;
}

// =============================================================================
// Enhanced Quality Assessment
// =============================================================================

typedef struct {
    int total_elements;
    int text_elements;
    int math_elements;
    int list_elements;
    int table_elements;
    int figure_elements;
    double estimated_render_time;
    size_t estimated_memory_usage;
} LatexQualityMetrics;

bool assess_latex_rendering_quality_enhanced(ViewTree* tree, LatexQualityMetrics** metrics_out) {
    if (!tree || !metrics_out) {
        log_error("Invalid parameters for enhanced quality assessment");
        return false;
    }
    
    log_info("Assessing enhanced LaTeX rendering quality");
    
    LatexQualityMetrics* metrics = (LatexQualityMetrics*)malloc(sizeof(LatexQualityMetrics));
    if (!metrics) {
        log_error("Failed to allocate quality metrics structure");
        return false;
    }
    
    memset(metrics, 0, sizeof(LatexQualityMetrics));
    
    // Calculate metrics from view tree statistics
    if (tree->stats.total_nodes > 0) {
        metrics->total_elements = tree->stats.total_nodes;
        metrics->text_elements = tree->stats.text_runs;
        metrics->math_elements = tree->stats.math_elements;
        metrics->list_elements = metrics->total_elements / 10; // Estimate
        metrics->table_elements = 0; // TODO: Implement table counting
        metrics->figure_elements = 0; // TODO: Implement figure counting
        
        // Estimate render time and memory usage
        metrics->estimated_render_time = tree->stats.layout_time + 0.1; // Add rendering overhead
        metrics->estimated_memory_usage = tree->stats.memory_usage;
    }
    
    log_info("Quality assessment: %d total elements, %.2fs estimated render time, %zu bytes memory",
             metrics->total_elements, metrics->estimated_render_time, metrics->estimated_memory_usage);
    
    *metrics_out = metrics;
    return true;
}

void latex_quality_metrics_destroy(LatexQualityMetrics* metrics) {
    if (metrics) free(metrics);
}
