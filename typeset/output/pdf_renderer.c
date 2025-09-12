#include "pdf_renderer.h"
#include "../../lib/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// libharu error handler
void pdf_error_handler(HPDF_STATUS error_no, HPDF_STATUS detail_no, void* user_data) {
    PDFRenderer* renderer = (PDFRenderer*)user_data;
    
    // Format error message
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), 
             "PDF Error: error_no=0x%04X, detail_no=0x%04X", 
             (unsigned int)error_no, (unsigned int)detail_no);
    
    // Store error message
    if (renderer && renderer->last_error) {
        free(renderer->last_error);
    }
    if (renderer) {
        renderer->last_error = strdup(error_msg);
    }
    
    log_error("PDF Renderer: %s", error_msg);
}

// Forward declarations for base renderer interface
static bool pdf_renderer_initialize(ViewRenderer* renderer, ViewRenderOptions* options);
static bool pdf_renderer_render_tree(ViewRenderer* renderer, ViewTree* tree, StrBuf* output);
static bool pdf_renderer_render_node(ViewRenderer* renderer, ViewNode* node);
static void pdf_renderer_finalize(ViewRenderer* renderer);
static void pdf_renderer_cleanup(ViewRenderer* renderer);

// Create PDF renderer
PDFRenderer* pdf_renderer_create(PDFRenderOptions* options) {
    PDFRenderer* renderer = calloc(1, sizeof(PDFRenderer));
    if (!renderer) {
        log_error("Failed to allocate PDF renderer");
        return NULL;
    }
    
    // Initialize base renderer interface
    renderer->base.name = strdup("PDF Renderer");
    renderer->base.format_name = strdup("PDF");
    renderer->base.mime_type = strdup("application/pdf");
    renderer->base.file_extension = strdup(".pdf");
    
    // Set up function pointers
    renderer->base.initialize = pdf_renderer_initialize;
    renderer->base.render_tree = pdf_renderer_render_tree;
    renderer->base.render_node = pdf_renderer_render_node;
    renderer->base.finalize = pdf_renderer_finalize;
    renderer->base.cleanup = pdf_renderer_cleanup;
    
    // Initialize renderer data
    renderer->base.renderer_data = renderer;
    
    // Create PDF document
    renderer->pdf_doc = HPDF_New(pdf_error_handler, renderer);
    if (!renderer->pdf_doc) {
        log_error("Failed to create PDF document");
        pdf_renderer_destroy(renderer);
        return NULL;
    }
    
    // Set PDF options
    if (options) {
        renderer->options = calloc(1, sizeof(PDFRenderOptions));
        memcpy(renderer->options, options, sizeof(PDFRenderOptions));
    } else {
        // Create default options
        renderer->options = calloc(1, sizeof(PDFRenderOptions));
        renderer->options->base.dpi = 72.0;
        renderer->options->base.embed_fonts = true;
        renderer->options->base.quality = VIEW_RENDER_QUALITY_NORMAL;
        renderer->options->pdf_version = PDF_VERSION_1_4;
    }
    
    // Initialize state
    renderer->current_x = 0.0;
    renderer->current_y = 0.0;
    renderer->line_height = 12.0;
    renderer->page_started = false;
    renderer->last_error = NULL;
    
    log_info("PDF renderer created successfully");
    return renderer;
}

// Destroy PDF renderer
void pdf_renderer_destroy(PDFRenderer* renderer) {
    if (!renderer) return;
    
    // Clean up PDF document
    if (renderer->pdf_doc) {
        HPDF_Free(renderer->pdf_doc);
    }
    
    // Clean up strings
    if (renderer->base.name) free(renderer->base.name);
    if (renderer->base.format_name) free(renderer->base.format_name);
    if (renderer->base.mime_type) free(renderer->base.mime_type);
    if (renderer->base.file_extension) free(renderer->base.file_extension);
    if (renderer->last_error) free(renderer->last_error);
    
    // Clean up options
    if (renderer->options) free(renderer->options);
    
    free(renderer);
}

// Initialize renderer
static bool pdf_renderer_initialize(ViewRenderer* renderer, ViewRenderOptions* options) {
    PDFRenderer* pdf_renderer = (PDFRenderer*)renderer->renderer_data;
    
    if (!pdf_renderer || !pdf_renderer->pdf_doc) {
        log_error("PDF renderer not properly initialized");
        return false;
    }
    
    // Set PDF compression mode
    HPDF_SetCompressionMode(pdf_renderer->pdf_doc, HPDF_COMP_ALL);
    
    // Set PDF info
    HPDF_SetInfoAttr(pdf_renderer->pdf_doc, HPDF_INFO_CREATOR, "Lambda Typeset");
    HPDF_SetInfoAttr(pdf_renderer->pdf_doc, HPDF_INFO_PRODUCER, "Lambda PDF Renderer");
    
    log_info("PDF renderer initialized");
    return true;
}

// Get or load font
HPDF_Font pdf_get_font(PDFRenderer* renderer, const char* font_name) {
    if (!renderer || !renderer->pdf_doc || !font_name) {
        return NULL;
    }
    
    // Map common font names to PDF built-in fonts
    const char* pdf_font_name = font_name;
    if (strcmp(font_name, "Arial") == 0 || strcmp(font_name, "sans-serif") == 0) {
        pdf_font_name = "Helvetica";
    } else if (strcmp(font_name, "Times") == 0 || strcmp(font_name, "serif") == 0) {
        pdf_font_name = "Times-Roman";
    } else if (strcmp(font_name, "Courier") == 0 || strcmp(font_name, "monospace") == 0) {
        pdf_font_name = "Courier";
    }
    
    // Get font from PDF document
    HPDF_Font font = HPDF_GetFont(renderer->pdf_doc, pdf_font_name, NULL);
    if (!font) {
        log_warn("Failed to load font '%s', using default", font_name);
        font = HPDF_GetFont(renderer->pdf_doc, "Helvetica", NULL);
    }
    
    return font;
}

// Set font
bool pdf_set_font(PDFRenderer* renderer, const char* font_name, double size) {
    if (!renderer || !renderer->current_page) {
        return false;
    }
    
    HPDF_Font font = pdf_get_font(renderer, font_name);
    if (!font) {
        return false;
    }
    
    HPDF_Page_SetFontAndSize(renderer->current_page, font, (float)size);
    renderer->current_font = font;
    renderer->line_height = size * 1.2; // Default line height
    
    return true;
}

// Convert Y coordinate (PDF uses bottom-left origin)
double pdf_convert_y(PDFRenderer* renderer, double y) {
    if (!renderer || !renderer->current_page) {
        return y;
    }
    
    double page_height = HPDF_Page_GetHeight(renderer->current_page);
    return page_height - y;
}

// Set text position
void pdf_set_position(PDFRenderer* renderer, double x, double y) {
    if (!renderer || !renderer->current_page) {
        return;
    }
    
    renderer->current_x = x;
    renderer->current_y = y;
}

// Start a new page
bool pdf_start_page(PDFRenderer* renderer, double width, double height) {
    if (!renderer || !renderer->pdf_doc) {
        return false;
    }
    
    // Create new page
    renderer->current_page = HPDF_AddPage(renderer->pdf_doc);
    if (!renderer->current_page) {
        log_error("Failed to create PDF page");
        return false;
    }
    
    // Set page size
    HPDF_Page_SetSize(renderer->current_page, HPDF_PAGE_SIZE_LETTER, HPDF_PAGE_PORTRAIT);
    if (width > 0 && height > 0) {
        HPDF_Page_SetWidth(renderer->current_page, (float)width);
        HPDF_Page_SetHeight(renderer->current_page, (float)height);
    }
    
    // Initialize font
    if (!renderer->default_font) {
        renderer->default_font = pdf_get_font(renderer, "Helvetica");
        renderer->current_font = renderer->default_font;
    }
    
    // Set default font
    pdf_set_font(renderer, "Helvetica", 12.0);
    
    // Initialize position
    renderer->current_x = 72.0; // 1 inch margin
    renderer->current_y = 72.0; // 1 inch margin
    renderer->page_started = true;
    
    log_debug("Started PDF page: %.1f x %.1f", width, height);
    return true;
}

// End current page
bool pdf_end_page(PDFRenderer* renderer) {
    if (!renderer || !renderer->current_page) {
        return false;
    }
    
    renderer->current_page = NULL;
    renderer->page_started = false;
    
    log_debug("Ended PDF page");
    return true;
}

// Render text run
bool pdf_render_text_run(PDFRenderer* renderer, ViewTextRun* text_run) {
    if (!renderer || !renderer->current_page || !text_run || !text_run->text) {
        return false;
    }
    
    // Set font if specified
    if (text_run->font) {
        pdf_set_font(renderer, "Helvetica", text_run->font_size);
    }
    
    // For now, use a fixed position since ViewTextRun doesn't have x/y fields
    double x = renderer->current_x;
    double y = pdf_convert_y(renderer, renderer->current_y);
    
    // Begin text
    HPDF_Page_BeginText(renderer->current_page);
    HPDF_Page_TextOut(renderer->current_page, (float)x, (float)y, text_run->text);
    HPDF_Page_EndText(renderer->current_page);
    
    // Update current position
    renderer->current_x += text_run->total_width;
    
    log_debug("Rendered text: '%s' at (%.1f, %.1f)", text_run->text, x, y);
    return true;
}

// Render geometry (lines, rectangles, etc.)
bool pdf_render_geometry(PDFRenderer* renderer, ViewGeometry* geometry) {
    if (!renderer || !renderer->current_page || !geometry) {
        return false;
    }
    
    switch (geometry->type) {
        case VIEW_GEOM_RECTANGLE: {
            // Draw a simple test rectangle
            double x = 100.0;
            double y = pdf_convert_y(renderer, 200.0);
            double width = 200.0;
            double height = 100.0;
            
            HPDF_Page_Rectangle(renderer->current_page, 
                               (float)x, (float)y, (float)width, (float)height);
            HPDF_Page_Stroke(renderer->current_page);
            
            log_debug("Rendered rectangle placeholder: (%.1f, %.1f, %.1f, %.1f)", x, y, width, height);
            break;
        }
        
        case VIEW_GEOM_LINE: {
            // Draw a simple test line
            double x1 = 50.0;
            double y1 = pdf_convert_y(renderer, 150.0);
            double x2 = 250.0;
            double y2 = pdf_convert_y(renderer, 250.0);
            
            HPDF_Page_MoveTo(renderer->current_page, (float)x1, (float)y1);
            HPDF_Page_LineTo(renderer->current_page, (float)x2, (float)y2);
            HPDF_Page_Stroke(renderer->current_page);
            
            log_debug("Rendered line placeholder: (%.1f, %.1f) to (%.1f, %.1f)", x1, y1, x2, y2);
            break;
        }
        
        default:
            log_warn("Unsupported geometry type: %d", geometry->type);
            return false;
    }
    
    return true;
}

// Render math element
bool pdf_render_math_element(PDFRenderer* renderer, ViewMathElement* math) {
    if (!renderer || !renderer->current_page || !math) {
        return false;
    }
    
    // For now, render math as simple text placeholder
    // TODO: Implement proper mathematical layout
    pdf_set_font(renderer, "Times-Italic", 12.0);
    
    double x = renderer->current_x;
    double y = pdf_convert_y(renderer, renderer->current_y);
    
    // Render different math types with placeholder text
    const char* math_text;
    switch (math->type) {
        case VIEW_MATH_ATOM:
            math_text = "x";  // Simple placeholder
            break;
        case VIEW_MATH_FRACTION:
            math_text = "a/b";
            break;
        case VIEW_MATH_OPERATOR:
            math_text = "+";
            break;
        default:
            math_text = "∅"; // Empty set symbol as placeholder
            break;
    }
    
    HPDF_Page_BeginText(renderer->current_page);
    HPDF_Page_TextOut(renderer->current_page, (float)x, (float)y, math_text);
    HPDF_Page_EndText(renderer->current_page);
    
    // Update position
    renderer->current_x += 20.0; // Simple spacing
    
    log_debug("Rendered math element: '%s' at (%.1f, %.1f)", math_text, x, y);
    return true;
}

// Render view node
bool pdf_render_node(PDFRenderer* renderer, ViewNode* node) {
    if (!renderer || !node) {
        return false;
    }
    
    // Render based on node type
    switch (node->type) {
        case VIEW_NODE_TEXT_RUN:
            if (node->content.text_run) {
                return pdf_render_text_run(renderer, node->content.text_run);
            }
            break;
            
        case VIEW_NODE_MATH_ELEMENT:
            if (node->content.math_elem) {
                return pdf_render_math_element(renderer, node->content.math_elem);
            }
            break;
            
        case VIEW_NODE_RECTANGLE:
        case VIEW_NODE_LINE:
            // Create a simple geometry placeholder
            ViewGeometry geom = {0};
            geom.type = (node->type == VIEW_NODE_RECTANGLE) ? VIEW_GEOM_RECTANGLE : VIEW_GEOM_LINE;
            return pdf_render_geometry(renderer, &geom);
            
        case VIEW_NODE_BLOCK:
        case VIEW_NODE_INLINE:
        case VIEW_NODE_GROUP:
            // TODO: Render children when view tree iteration is available
            log_debug("Rendered container node type: %d", node->type);
            return true;
            
        default:
            log_debug("Skipping unsupported node type: %d", node->type);
            return true;
    }
    
    return false;
}

// Render page
bool pdf_render_page(PDFRenderer* renderer, ViewPage* page) {
    if (!renderer || !page) {
        return false;
    }
    
    // Start new PDF page
    if (!pdf_start_page(renderer, page->page_size.width, page->page_size.height)) {
        return false;
    }
    
    log_info("Rendering page %d (%.1f x %.1f)", page->page_number, page->page_size.width, page->page_size.height);
    
    // Render the page node if it exists
    if (page->page_node) {
        if (!pdf_render_node(renderer, page->page_node)) {
            log_warn("Failed to render page node for page %d", page->page_number);
        }
    }
    
    // End page
    pdf_end_page(renderer);
    
    return true;
}

// Main tree rendering function
bool pdf_render_view_tree(PDFRenderer* renderer, ViewTree* tree) {
    if (!renderer || !tree) {
        log_error("Invalid renderer or tree");
        return false;
    }
    
    log_info("Starting PDF rendering of %d pages", tree->page_count);
    
    // Render each page
    for (int i = 0; i < tree->page_count; i++) {
        if (!pdf_render_page(renderer, tree->pages[i])) {
            log_error("Failed to render page %d", i + 1);
            return false;
        }
    }
    
    log_info("PDF rendering completed successfully");
    return true;
}

// Save to file
bool pdf_save_to_file(PDFRenderer* renderer, const char* filename) {
    if (!renderer || !renderer->pdf_doc || !filename) {
        log_error("Invalid parameters for PDF save");
        return false;
    }
    
    // Save PDF to file
    HPDF_STATUS status = HPDF_SaveToFile(renderer->pdf_doc, filename);
    if (status != HPDF_OK) {
        log_error("Failed to save PDF to file: %s", filename);
        return false;
    }
    
    log_info("PDF saved to: %s", filename);
    return true;
}

// Base renderer interface implementations
static bool pdf_renderer_render_tree(ViewRenderer* renderer, ViewTree* tree, StrBuf* output) {
    PDFRenderer* pdf_renderer = (PDFRenderer*)renderer->renderer_data;
    
    // For PDF, we don't use StrBuf output - we save directly to file
    // This function is mainly for compatibility with the base interface
    return pdf_render_view_tree(pdf_renderer, tree);
}

static bool pdf_renderer_render_node(ViewRenderer* renderer, ViewNode* node) {
    PDFRenderer* pdf_renderer = (PDFRenderer*)renderer->renderer_data;
    return pdf_render_node(pdf_renderer, node);
}

static void pdf_renderer_finalize(ViewRenderer* renderer) {
    // Nothing specific to finalize for PDF
    log_debug("PDF renderer finalized");
}

static void pdf_renderer_cleanup(ViewRenderer* renderer) {
    if (renderer && renderer->renderer_data) {
        pdf_renderer_destroy((PDFRenderer*)renderer->renderer_data);
        renderer->renderer_data = NULL;
    }
}

// Get last error
const char* pdf_get_last_error(PDFRenderer* renderer) {
    if (!renderer) return "Invalid renderer";
    return renderer->last_error ? renderer->last_error : "No error";
}
