#include "render.hpp"
#include "view.hpp"
#include "layout.hpp"
#include "dom.hpp"
#include "../lib/log.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef _WIN32
#include <hpdf.h>
#endif

// Forward declarations for functions from other modules
int ui_context_init(UiContext* uicon, bool headless);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);
void layout_html_doc(UiContext* uicon, Document* doc, bool is_reflow);
lxb_url_t* get_current_dir_lexbor();

#ifndef _WIN32

typedef struct PdfRenderContext {
    HPDF_Doc pdf_doc;
    HPDF_Page current_page;
    HPDF_Font current_font;
    UiContext* ui_context;

    float page_width;
    float page_height;
    float current_x;
    float current_y;

    FontBox font;
    Color color;
    BlockBlot block;  // Current block context for coordinate transformation
} PdfRenderContext;

// Forward declarations
void render_block_view_pdf(PdfRenderContext* ctx, ViewBlock* view_block);
void render_inline_view_pdf(PdfRenderContext* ctx, ViewSpan* view_span);
void render_children_pdf(PdfRenderContext* ctx, View* view);
void render_text_view_pdf(PdfRenderContext* ctx, ViewText* text);

// External function from render_svg.cpp
extern void calculate_content_bounds(View* view, int* max_x, int* max_y);

// Error handler for libharu
void pdf_error_handler(HPDF_STATUS error_no, HPDF_STATUS detail_no, void* user_data) {
    log_error("PDF Error: error_no=0x%04X, detail_no=0x%04X",
              (unsigned int)error_no, (unsigned int)detail_no);
}

// Helper function to get PDF font name from system font
const char* get_pdf_font_name(const char* font_family) {
    if (!font_family) return "Helvetica";

    if (strstr(font_family, "Arial") || strstr(font_family, "arial")) {
        return "Helvetica";
    } else if (strstr(font_family, "Times") || strstr(font_family, "times")) {
        return "Times-Roman";
    } else if (strstr(font_family, "Courier") || strstr(font_family, "courier")) {
        return "Courier";
    }

    return "Helvetica"; // Default fallback
}

// Set PDF color from Color struct
void pdf_set_color(PdfRenderContext* ctx, Color color) {
    float r = color.r / 255.0f;
    float g = color.g / 255.0f;
    float b = color.b / 255.0f;

    HPDF_Page_SetRGBFill(ctx->current_page, r, g, b);
    HPDF_Page_SetRGBStroke(ctx->current_page, r, g, b);
}

// Render a rectangle (for backgrounds and borders)
void pdf_render_rect(PdfRenderContext* ctx, float x, float y, float width, float height, Color color, bool fill) {
    if (color.a == 0) return; // Transparent, don't render

    pdf_set_color(ctx, color);

    // Convert coordinates (PDF origin is bottom-left, we use top-left)
    float pdf_y = ctx->page_height - y - height;

    HPDF_Page_Rectangle(ctx->current_page, x, pdf_y, width, height);

    if (fill) {
        HPDF_Page_Fill(ctx->current_page);
    } else {
        HPDF_Page_Stroke(ctx->current_page);
    }
}

// Render text at specific position
void pdf_render_text(PdfRenderContext* ctx, const char* text, float x, float y, Color color) {
    if (!text || strlen(text) == 0) return;

    pdf_set_color(ctx, color);

    // Get font height for proper baseline positioning
    float font_size = 16.0f;
    if (ctx->current_font) {
        font_size = ctx->font.face.style.font_size ? ctx->font.face.style.font_size : 16.0f;
    }

    // Convert coordinates (PDF origin is bottom-left, we use top-left)
    // Calculate baseline position similar to SVG: y + ascender height
    // In PDF, we need to convert from top-left to bottom-left coordinates
    float baseline_offset = font_size * 0.8f;  // Approximate ascender height
    float baseline_y = y + baseline_offset;    // Calculate baseline in top-left system
    float pdf_y = ctx->page_height - baseline_y; // Convert to PDF bottom-left system

    HPDF_Page_BeginText(ctx->current_page);
    HPDF_Page_TextOut(ctx->current_page, x, pdf_y, text);
    HPDF_Page_EndText(ctx->current_page);
}

// Render text view
void render_text_view_pdf(PdfRenderContext* ctx, ViewText* text) {
    if (!text || !text->node || !text->node->text_data()) return;

    // Calculate absolute position using block context (like SVG renderer)
    float x = (float)ctx->block.x + text->x;
    float y = (float)ctx->block.y + text->y;

    // Extract the text content
    unsigned char* str = text->node->text_data();
    char* text_content = (char*)malloc(text->length + 1);
    strncpy(text_content, (char*)str + text->start_index, text->length);
    text_content[text->length] = '\0';  // Fix: Use single backslash for null terminator

    if (strlen(text_content) == 0) {
        free(text_content);
        return;
    }

    // Set font if available
    if (ctx->current_font) {
        float font_size = ctx->font.face.style.font_size ? ctx->font.face.style.font_size : 16.0f;
        HPDF_Page_SetFontAndSize(ctx->current_page, ctx->current_font, font_size);
    }

    // Render the text using absolute coordinates
    pdf_render_text(ctx, text_content, x, y, ctx->color);

    free(text_content);
}

// Render block view with background and borders
void render_block_view_pdf(PdfRenderContext* ctx, ViewBlock* view_block) {
    if (!view_block) return;

    // Save parent context (like SVG renderer)
    BlockBlot pa_block = ctx->block;
    FontBox pa_font = ctx->font;
    Color pa_color = ctx->color;

    // Update position context - add this block's offset to parent context
    ctx->block.x = pa_block.x + (int)view_block->x;
    ctx->block.y = pa_block.y + (int)view_block->y;

    // Calculate absolute position for background/borders
    float x = (float)ctx->block.x;
    float y = (float)ctx->block.y;
    float width = (float)view_block->width;
    float height = (float)view_block->height;

    // Render background if present
    if (view_block->bound && view_block->bound->background && view_block->bound->background->color.a > 0) {
        BackgroundProp* bg = view_block->bound->background;
        pdf_render_rect(ctx, x, y, width, height, bg->color, true);
    }

    // Update color context
    if (view_block->in_line && view_block->in_line->color.c) {
        ctx->color = view_block->in_line->color;
    }

    // Render borders if present
    if (view_block->bound && view_block->bound->border) {
        BorderProp* border = view_block->bound->border;

        // Top border
        if (border->width.top > 0 && border->top_color.a > 0) {
            pdf_render_rect(ctx, x, y, width, (float)border->width.top, border->top_color, true);
        }

        // Right border
        if (border->width.right > 0 && border->right_color.a > 0) {
            pdf_render_rect(ctx, x + width - border->width.right, y, (float)border->width.right, height, border->right_color, true);
        }

        // Bottom border
        if (border->width.bottom > 0 && border->bottom_color.a > 0) {
            pdf_render_rect(ctx, x, y + height - border->width.bottom, width, (float)border->width.bottom, border->bottom_color, true);
        }

        // Left border
        if (border->width.left > 0 && border->left_color.a > 0) {
            pdf_render_rect(ctx, x, y, (float)border->width.left, height, border->left_color, true);
        }
    }

    // Render children
    render_children_pdf(ctx, (View*)view_block);

    // Restore context (like SVG renderer)
    ctx->block = pa_block;
    ctx->font = pa_font;
    ctx->color = pa_color;
}

// Render inline view (spans)
void render_inline_view_pdf(PdfRenderContext* ctx, ViewSpan* view_span) {
    if (!view_span) return;

    // Set font if specified
    if (view_span->font && view_span->font->family) {
        const char* pdf_font_name = get_pdf_font_name(view_span->font->family);
        HPDF_Font font = HPDF_GetFont(ctx->pdf_doc, pdf_font_name, NULL);
        if (font) {
            ctx->current_font = font;
            float font_size = view_span->font->font_size ? view_span->font->font_size : 16.0f;
            HPDF_Page_SetFontAndSize(ctx->current_page, font, font_size);
        }
    }

    // Set color if specified
    if (view_span->in_line) {
        ctx->color = view_span->in_line->color;
    }

    // Render children
    render_children_pdf(ctx, (View*)view_span);
}

// Render children recursively
void render_children_pdf(PdfRenderContext* ctx, View* view) {
    if (!view || view->type < RDT_VIEW_INLINE) return;

    ViewGroup* group = (ViewGroup*)view;
    View* child = group->child;

    while (child) {
        switch (child->type) {
            case RDT_VIEW_BLOCK:
            case RDT_VIEW_LIST_ITEM:
            case RDT_VIEW_TABLE:
            case RDT_VIEW_TABLE_ROW_GROUP:
            case RDT_VIEW_TABLE_ROW:
            case RDT_VIEW_TABLE_CELL:
                render_block_view_pdf(ctx, (ViewBlock*)child);
                break;

            case RDT_VIEW_INLINE:
            case RDT_VIEW_INLINE_BLOCK:
                render_inline_view_pdf(ctx, (ViewSpan*)child);
                break;

            case RDT_VIEW_TEXT:
                render_text_view_pdf(ctx, (ViewText*)child);
                break;

            default:
                // Handle other view types if needed
                break;
        }

        child = child->next;
    }
}

// Main PDF rendering function
HPDF_Doc render_view_tree_to_pdf(UiContext* uicon, View* root_view, float width, float height) {
    if (!root_view || !uicon) {
        return NULL;
    }

    PdfRenderContext ctx;
    memset(&ctx, 0, sizeof(PdfRenderContext));

    // Create PDF document
    ctx.pdf_doc = HPDF_New(pdf_error_handler, NULL);
    if (!ctx.pdf_doc) {
        log_error("Failed to create PDF document");
        return NULL;
    }

    // Set PDF compression
    HPDF_SetCompressionMode(ctx.pdf_doc, HPDF_COMP_ALL);

    // Set document info
    HPDF_SetInfoAttr(ctx.pdf_doc, HPDF_INFO_CREATOR, "Lambda Script Renderer");
    HPDF_SetInfoAttr(ctx.pdf_doc, HPDF_INFO_PRODUCER, "Lambda PDF Renderer");

    // Add a page
    ctx.current_page = HPDF_AddPage(ctx.pdf_doc);
    if (!ctx.current_page) {
        log_error("Failed to add PDF page");
        HPDF_Free(ctx.pdf_doc);
        return NULL;
    }

    // Set page size to match content dimensions
    ctx.page_width = width;
    ctx.page_height = height;
    HPDF_Page_SetWidth(ctx.current_page, width);
    HPDF_Page_SetHeight(ctx.current_page, height);

    // Initialize context
    ctx.ui_context = uicon;
    ctx.color.r = 0; ctx.color.g = 0; ctx.color.b = 0; ctx.color.a = 255; // Black text
    ctx.current_x = 0;
    ctx.current_y = 0;

    // Initialize block context (starting at origin)
    ctx.block.x = 0;
    ctx.block.y = 0;

    // Initialize font from default
    ctx.font.face.style = uicon->default_font;

    // Set default font
    ctx.current_font = HPDF_GetFont(ctx.pdf_doc, "Helvetica", NULL);
    if (ctx.current_font) {
        HPDF_Page_SetFontAndSize(ctx.current_page, ctx.current_font, 16.0f);
    }

    // Render the root view
    if (root_view->type == RDT_VIEW_BLOCK) {
        render_block_view_pdf(&ctx, (ViewBlock*)root_view);
    } else if (root_view->type >= RDT_VIEW_INLINE) {
        render_children_pdf(&ctx, root_view);
    }

    return ctx.pdf_doc;
}

// Save PDF to file
bool save_pdf_to_file(HPDF_Doc pdf_doc, const char* filename) {
    if (!pdf_doc || !filename) {
        return false;
    }

    HPDF_STATUS status = HPDF_SaveToFile(pdf_doc, filename);
    if (status != HPDF_OK) {
        log_error("Failed to save PDF to file: %s", filename);
        return false;
    }

    return true;
}

// Main function to layout HTML and render to PDF
int render_html_to_pdf(const char* html_file, const char* pdf_file) {
    log_debug("render_html_to_pdf called with html_file='%s', pdf_file='%s'", html_file, pdf_file);

    // Initialize UI context in headless mode
    UiContext ui_context;
    if (ui_context_init(&ui_context, true) != 0) {
        log_debug("Failed to initialize UI context for PDF rendering");
        return 1;
    }

    // Create a surface for layout calculations (no actual rendering needed)
    int default_width = 800;   // A4 width in points (approximately)
    int default_height = 1200; // A4 height in points (approximately)
    ui_context_create_surface(&ui_context, default_width, default_height);

    // Get current directory for relative path resolution
    lxb_url_t* cwd = get_current_dir_lexbor();
    if (!cwd) {
        log_debug("Could not get current directory");
        ui_context_cleanup(&ui_context);
        return 1;
    }

    // Load HTML document
    log_debug("Loading HTML document: %s", html_file);
    Document* doc = load_html_doc(cwd, (char*)html_file);
    if (!doc) {
        log_debug("Could not load HTML file: %s", html_file);
        lxb_url_destroy(cwd);
        ui_context_cleanup(&ui_context);
        return 1;
    }

    ui_context.document = doc;

    // Layout the document
    log_debug("Performing layout...");
    layout_html_doc(&ui_context, doc, false);

    // Calculate actual content dimensions
    int content_max_x = 0, content_max_y = 0;
    if (doc->view_tree && doc->view_tree->root) {
        calculate_content_bounds(doc->view_tree->root, &content_max_x, &content_max_y);
        // Add some padding to ensure nothing is cut off
        content_max_x += 50;
        content_max_y += 50;

        // Use minimum dimensions to ensure reasonable PDF size
        if (content_max_x < default_width) content_max_x = default_width;
        if (content_max_y < default_height) content_max_y = default_height;

        log_debug("Calculated content bounds: %dx%d", content_max_x, content_max_y);
    }

    // Render to PDF
    if (doc->view_tree && doc->view_tree->root) {
        log_debug("Rendering view tree to PDF...");
        HPDF_Doc pdf_doc = render_view_tree_to_pdf(&ui_context, doc->view_tree->root,
                                                   (float)content_max_x, (float)content_max_y);
        if (pdf_doc) {
            if (save_pdf_to_file(pdf_doc, pdf_file)) {
                printf("Successfully rendered HTML to PDF: %s\\n", pdf_file);
                HPDF_Free(pdf_doc);
                lxb_url_destroy(cwd);
                ui_context_cleanup(&ui_context);
                return 0;
            } else {
                log_debug("Failed to save PDF to file: %s", pdf_file);
                HPDF_Free(pdf_doc);
            }
        } else {
            log_debug("Failed to render view tree to PDF");
        }
    } else {
        log_debug("No view tree available for rendering");
    }

    // Cleanup
    lxb_url_destroy(cwd);
    ui_context_cleanup(&ui_context);
    return 1;
}

#else
// Windows stub implementation
int render_html_to_pdf(const char* html_file, const char* pdf_file) {
    printf("PDF rendering is not supported on Windows (libharu not available)\\n");
    return 1;
}
#endif
