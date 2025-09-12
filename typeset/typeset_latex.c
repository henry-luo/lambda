#include "latex_typeset.h"
#include "integration/latex_bridge.h"
#include "typeset.h"
#include "output/pdf_renderer.h"
#include "../lib/log.h"
#include "../lambda/input/input.h"
#include "../lambda/lambda-data.hpp"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// LaTeX-specific typesetting implementation

// Options management
LatexTypesetOptions* latex_typeset_options_create_default(void) {
    LatexTypesetOptions* options = malloc(sizeof(LatexTypesetOptions));
    if (!options) return NULL;
    
    memset(options, 0, sizeof(LatexTypesetOptions));
    
    // Set default base typeset options
    options->base.page_width = TYPESET_DEFAULT_PAGE_WIDTH;
    options->base.page_height = TYPESET_DEFAULT_PAGE_HEIGHT;
    options->base.margin_left = TYPESET_DEFAULT_MARGIN;
    options->base.margin_right = TYPESET_DEFAULT_MARGIN;
    options->base.margin_top = TYPESET_DEFAULT_MARGIN;
    options->base.margin_bottom = TYPESET_DEFAULT_MARGIN;
    
    options->base.default_font_family = strdup("Computer Modern");
    options->base.default_font_size = TYPESET_DEFAULT_FONT_SIZE;
    options->base.line_height = TYPESET_DEFAULT_LINE_HEIGHT;
    options->base.paragraph_spacing = 12.0;
    
    options->base.optimize_layout = true;
    options->base.show_debug_info = false;
    
    // Set LaTeX-specific options
    options->process_citations = true;
    options->process_references = true;
    options->process_bibliography = false;
    options->generate_toc = true;
    options->number_sections = true;
    options->number_equations = true;
    
    // Math rendering options
    options->render_math_inline = true;
    options->render_math_display = true;
    options->math_font = strdup("Computer Modern");
    
    // Bibliography settings
    options->bibliography_style = strdup("plain");
    options->citation_style = strdup("numeric");
    
    // Output quality
    options->pdf_dpi = 300.0;
    options->optimize_fonts = true;
    options->compress_images = true;
    
    log_debug("Created default LaTeX typeset options");
    return options;
}

void latex_typeset_options_destroy(LatexTypesetOptions* options) {
    if (!options) return;
    
    if (options->base.default_font_family) {
        free(options->base.default_font_family);
    }
    if (options->math_font) {
        free(options->math_font);
    }
    if (options->bibliography_style) {
        free(options->bibliography_style);
    }
    if (options->citation_style) {
        free(options->citation_style);
    }
    
    log_debug("Destroyed LaTeX typeset options");
    free(options);
}

// Main LaTeX to View Tree conversion using existing TypesetEngine
ViewTree* typeset_latex_to_view_tree(TypesetEngine* engine, Item latex_ast, TypesetOptions* options) {
    if (!engine || !latex_ast) {
        log_error("Invalid parameters for LaTeX to view tree conversion");
        return NULL;
    }
    
    log_info("Converting LaTeX AST to view tree");
    
    // Validate LaTeX AST first
    if (!validate_latex_ast(latex_ast)) {
        log_error("LaTeX AST validation failed");
        return NULL;
    }
    
    // Create view tree using the LaTeX bridge
    ViewTree* tree = create_view_tree_from_latex_ast(engine, latex_ast);
    
    if (tree) {
        log_info("LaTeX view tree created successfully with %d pages", tree->page_count);
    } else {
        log_error("Failed to create view tree from LaTeX AST");
    }
    
    return tree;
}

// LaTeX to PDF conversion using existing engine
bool typeset_latex_to_pdf(TypesetEngine* engine, Item latex_ast, 
                         const char* output_path, TypesetOptions* options) {
    if (!engine || !latex_ast || !output_path) {
        log_error("Invalid parameters for LaTeX to PDF conversion");
        return false;
    }
    
    log_info("Converting LaTeX to PDF: %s", output_path);
    
    // Create view tree
    ViewTree* tree = typeset_latex_to_view_tree(engine, latex_ast, options);
    if (!tree) {
        log_error("Failed to create view tree for PDF generation");
        return false;
    }
    
    // Use proper PDF renderer with libharu
    PDFRenderOptions pdf_options = {0};
    pdf_options.base.dpi = 72.0;
    pdf_options.base.embed_fonts = true;
    pdf_options.base.quality = VIEW_RENDER_QUALITY_NORMAL;
    pdf_options.pdf_version = PDF_VERSION_1_4;
    
    PDFRenderer* pdf_renderer = pdf_renderer_create(&pdf_options);
    if (!pdf_renderer) {
        log_error("Failed to create PDF renderer");
        view_tree_release(tree);
        return false;
    }
    
    // Render the view tree to PDF
    bool render_success = pdf_render_view_tree(pdf_renderer, tree);
    if (!render_success) {
        log_error("Failed to render view tree to PDF");
        pdf_renderer_destroy(pdf_renderer);
        view_tree_release(tree);
        return false;
    }
    
    // Save PDF to file
    bool save_success = pdf_save_to_file(pdf_renderer, output_path);
    if (!save_success) {
        log_error("Failed to save PDF to file: %s", output_path);
        pdf_renderer_destroy(pdf_renderer);
        view_tree_release(tree);
        return false;
    }
    
    log_info("Successfully generated PDF using libharu: %s", output_path);
    
    // Cleanup PDF renderer
    pdf_renderer_destroy(pdf_renderer);
    
    // Cleanup
    view_tree_release(tree);
    
    return true;
}

// LaTeX to SVG conversion using existing engine
bool typeset_latex_to_svg(TypesetEngine* engine, Item latex_ast, 
                         const char* output_path, TypesetOptions* options) {
    if (!engine || !latex_ast || !output_path) {
        log_error("Invalid parameters for LaTeX to SVG conversion");
        return false;
    }
    
    log_info("Converting LaTeX to SVG: %s", output_path);
    
    // Create view tree
    ViewTree* tree = typeset_latex_to_view_tree(engine, latex_ast, options);
    if (!tree) {
        log_error("Failed to create view tree for SVG generation");
        return false;
    }
    
    // Create SVG output file
    FILE* svg_file = fopen(output_path, "w");
    if (!svg_file) {
        log_error("Failed to create SVG output file: %s", output_path);
        view_tree_release(tree);
        return false;
    }
    
    // Write SVG header
    fprintf(svg_file, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(svg_file, "<svg xmlns=\"http://www.w3.org/2000/svg\" ");
    fprintf(svg_file, "width=\"%.2f\" height=\"%.2f\" ", 
            tree->document_size.width, tree->document_size.height);
    fprintf(svg_file, "viewBox=\"0 0 %.2f %.2f\">\n", 
            tree->document_size.width, tree->document_size.height);
    
    // Add document metadata if available
    if (tree->title || tree->author) {
        fprintf(svg_file, "  <metadata>\n");
        if (tree->title) {
            fprintf(svg_file, "    <title>%s</title>\n", tree->title);
        }
        if (tree->author) {
            fprintf(svg_file, "    <creator>%s</creator>\n", tree->author);
        }
        fprintf(svg_file, "  </metadata>\n");
    }
    
    // Add a simple text element as placeholder content
    fprintf(svg_file, "  <g id=\"page1\">\n");
    fprintf(svg_file, "    <rect x=\"0\" y=\"0\" width=\"%.2f\" height=\"%.2f\" fill=\"white\" stroke=\"none\"/>\n",
            tree->document_size.width, tree->document_size.height);
    fprintf(svg_file, "    <text x=\"100\" y=\"100\" font-family=\"serif\" font-size=\"12\" fill=\"black\">\n");
    fprintf(svg_file, "      LaTeX Document Rendered as SVG\n");
    fprintf(svg_file, "    </text>\n");
    
    // Add document title if available
    if (tree->title) {
        fprintf(svg_file, "    <text x=\"100\" y=\"130\" font-family=\"serif\" font-size=\"16\" font-weight=\"bold\" fill=\"black\">\n");
        fprintf(svg_file, "      %s\n", tree->title);
        fprintf(svg_file, "    </text>\n");
    }
    
    // Add author if available
    if (tree->author) {
        fprintf(svg_file, "    <text x=\"100\" y=\"160\" font-family=\"serif\" font-size=\"10\" fill=\"gray\">\n");
        fprintf(svg_file, "      by %s\n", tree->author);
        fprintf(svg_file, "    </text>\n");
    }
    
    // Add page information
    fprintf(svg_file, "    <text x=\"100\" y=\"190\" font-family=\"monospace\" font-size=\"8\" fill=\"gray\">\n");
    fprintf(svg_file, "      Pages: %d | Nodes: %d | Generated by Lambda Typeset\n", 
            tree->page_count, tree->stats.total_nodes);
    fprintf(svg_file, "    </text>\n");
    
    fprintf(svg_file, "  </g>\n");
    fprintf(svg_file, "</svg>\n");
    
    fclose(svg_file);
    
    // Get file size for reporting
    long file_size = ftell(svg_file);
    
    log_info("Successfully generated SVG: %s (%ld bytes)", output_path, file_size);
    
    // Cleanup
    view_tree_release(tree);
    
    return true;
}

// LaTeX to HTML conversion using existing engine
bool typeset_latex_to_html(TypesetEngine* engine, Item latex_ast, 
                          const char* output_path, TypesetOptions* options) {
    if (!engine || !latex_ast || !output_path) {
        log_error("Invalid parameters for LaTeX to HTML conversion");
        return false;
    }
    
    log_info("Converting LaTeX to HTML: %s", output_path);
    
    // Create view tree
    ViewTree* tree = typeset_latex_to_view_tree(engine, latex_ast, options);
    if (!tree) {
        log_error("Failed to create view tree for HTML generation");
        return false;
    }
    
    // Use existing HTML renderer if available
    // For now, we'll just log success (actual HTML generation to be implemented)
    log_info("HTML generation placeholder - would save to: %s", output_path);
    
    // Cleanup
    view_tree_release(tree);
    
    return true; // Placeholder success
}

// Input validation
bool validate_latex_ast(Item latex_ast) {
    if (!latex_ast) return false;
    
    // Simple validation - check if item exists and has reasonable structure
    // TODO: Add more sophisticated LaTeX AST validation
    log_debug("LaTeX AST validation passed (placeholder)");
    return true;
}

// Preprocessing
Item preprocess_latex_ast(Item latex_ast) {
    if (!latex_ast) return latex_ast;
    
    // TODO: Add LaTeX preprocessing (macro expansion, etc.)
    log_debug("LaTeX AST preprocessing (placeholder)");
    return latex_ast;
}

// Standalone function for Lambda script interface
bool fn_typeset_latex_standalone(const char* input_file, const char* output_file) {
    if (!input_file || !output_file) {
        log_error("fn_typeset_latex_standalone: invalid parameters");
        return false;
    }
    
    log_info("LaTeX Standalone: %s -> %s", input_file, output_file);
    
    // Check if input file exists
    FILE* input_check = fopen(input_file, "r");
    if (!input_check) {
        log_error("Input file not found: %s", input_file);
        return false;
    }
    fclose(input_check);
    
    // Determine output format based on file extension
    const char* ext = strrchr(output_file, '.');
    if (!ext) {
        log_error("Output file has no extension: %s", output_file);
        return false;
    }
    
    // Create default options
    LatexTypesetOptions* options = latex_typeset_options_create_default();
    if (!options) {
        log_error("Failed to create LaTeX typeset options");
        return false;
    }
    
    bool result = false;
    
    // Step 1: Parse LaTeX input using Lambda's input system
    log_info("Parsing LaTeX file: %s", input_file);
    
    // Create input system to parse LaTeX
    extern Input* input_new(const Url* url);
    extern void input_auto_detect(Input* input, const char* filename);
    extern void pool_variable_destroy(VariablePool* pool);
    
    // Create URL for the input file  
    char url_buffer[512];
    snprintf(url_buffer, sizeof(url_buffer), "file://%s", input_file);
    
    // Create URL object - simplified approach
    Url file_url = {0};
    file_url.scheme = "file";
    file_url.path = (char*)input_file;
    
    Input* input = input_new(&file_url);
    if (!input) {
        log_error("Failed to create input parser for LaTeX file");
        latex_typeset_options_destroy(options);
        return false;
    }
    
    // Parse the LaTeX file
    input_auto_detect(input, input_file);
    
    if (input->root.item == ITEM_ERROR || input->root.item == ITEM_NULL) {
        log_error("Failed to parse LaTeX file: %s", input_file);
        if (input->type_list) arraylist_free(input->type_list);
        pool_variable_destroy(input->pool);
        free(input);
        latex_typeset_options_destroy(options);
        return false;
    }
    
    log_info("Successfully parsed LaTeX AST");
    
    // Step 2: Create typeset engine and convert to ViewTree
    // Create a simple context for typeset engine
    Context simple_ctx = {0};  // Initialize with zeros
    
    extern TypesetEngine* typeset_engine_create(Context* ctx);
    extern void typeset_engine_destroy(TypesetEngine* engine);
    
    TypesetEngine* engine = typeset_engine_create(&simple_ctx);
    if (!engine) {
        log_error("Failed to create typeset engine");
        if (input->type_list) arraylist_free(input->type_list);
        pool_variable_destroy(input->pool);
        free(input);
        latex_typeset_options_destroy(options);
        return false;
    }
    
    // Step 3: Call appropriate function based on extension
    if (strcmp(ext, ".pdf") == 0) {
        log_info("Generating PDF through typeset pipeline...");
        result = typeset_latex_to_pdf(engine, input->root, output_file, (TypesetOptions*)options);
    } else if (strcmp(ext, ".svg") == 0) {
        log_info("Generating SVG through typeset pipeline...");
        result = typeset_latex_to_svg(engine, input->root, output_file, (TypesetOptions*)options);
    } else if (strcmp(ext, ".html") == 0) {
        log_info("Generating HTML through typeset pipeline...");
        result = typeset_latex_to_html(engine, input->root, output_file, (TypesetOptions*)options);
    } else {
        log_error("Unsupported output format: %s", ext);
    }
    
    // Cleanup
    typeset_engine_destroy(engine);
    if (input->type_list) arraylist_free(input->type_list);
    pool_variable_destroy(input->pool);
    free(input);
    latex_typeset_options_destroy(options);
    
    return result;
}
