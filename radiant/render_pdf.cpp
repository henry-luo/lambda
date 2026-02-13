#include "render.hpp"
#include "view.hpp"
#include "layout.hpp"
#include "font_face.h"
#include "../lambda/input/css/dom_element.hpp"
extern "C" {
#include "../lib/url.h"
#include "../lib/pdf_writer.h"
#include "../lib/memtrack.h"
}
#include <stdio.h>
#include <string.h>
#include <math.h>

// Forward declarations for functions from other modules
int ui_context_init(UiContext* uicon, bool headless);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);
void layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
void setup_font(UiContext* uicon, FontBox *fbox, FontProp *fprop);

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
        font_size = ctx->font.style->font_size ? ctx->font.style->font_size : 16.0f;
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
    if (!text || !text->text_data()) return;

    // Get text-transform from parent elements
    CssEnum text_transform = CSS_VALUE_NONE;
    DomNode* parent = text->parent;
    while (parent) {
        if (parent->is_element()) {
            DomElement* elem = (DomElement*)parent;
            CssEnum transform = get_text_transform_from_block(elem->blk);
            if (transform != CSS_VALUE_NONE) {
                text_transform = transform;
                break;
            }
        }
        parent = parent->parent;
    }

    // Calculate absolute position using block context (like SVG renderer)
    // Extract the text content
    unsigned char* str = text->text_data();
    TextRect *text_rect = text->rect;
    NEXT_RECT:
    float base_x = (float)ctx->block.x + text_rect->x, y = (float)ctx->block.y + text_rect->y;

    // Apply text-transform if needed
    char* text_content = (char*)mem_alloc(text_rect->length * 4 + 1, MEM_CAT_RENDER);  // Extra space for UTF-8 expansion
    if (text_transform != CSS_VALUE_NONE) {
        unsigned char* src = str + text_rect->start_index;
        unsigned char* src_end = src + text_rect->length;
        char* dst = text_content;
        bool is_word_start = true;

        while (src < src_end) {
            uint32_t codepoint = *src;
            int bytes = 1;

            if (codepoint >= 128) {
                bytes = str_utf8_decode((const char*)src, (size_t)(src_end - src), &codepoint);
                if (bytes <= 0) bytes = 1;
            }

            if (is_space(codepoint)) {
                is_word_start = true;
                *dst++ = *src;
                src += bytes;
                continue;
            }

            uint32_t transformed = apply_text_transform(codepoint, text_transform, is_word_start);
            is_word_start = false;

            // Encode back to UTF-8
            if (transformed < 0x80) {
                *dst++ = (char)transformed;
            } else if (transformed < 0x800) {
                *dst++ = (char)(0xC0 | (transformed >> 6));
                *dst++ = (char)(0x80 | (transformed & 0x3F));
            } else if (transformed < 0x10000) {
                *dst++ = (char)(0xE0 | (transformed >> 12));
                *dst++ = (char)(0x80 | ((transformed >> 6) & 0x3F));
                *dst++ = (char)(0x80 | (transformed & 0x3F));
            } else {
                *dst++ = (char)(0xF0 | (transformed >> 18));
                *dst++ = (char)(0x80 | ((transformed >> 12) & 0x3F));
                *dst++ = (char)(0x80 | ((transformed >> 6) & 0x3F));
                *dst++ = (char)(0x80 | (transformed & 0x3F));
            }
            src += bytes;
        }
        *dst = '\0';
    } else {
        strncpy(text_content, (char*)str + text_rect->start_index, text_rect->length);
        text_content[text_rect->length] = '\0';
    }

    if (strlen(text_content) == 0) {
        mem_free(text_content);
        return;
    }

    // Set font if available
    float font_size = 16.0f;
    if (ctx->current_font) {
        font_size = ctx->font.style->font_size ? ctx->font.style->font_size : 16.0f;
        HPDF_Page_SetFontAndSize(ctx->current_page, ctx->current_font, font_size);
    }

    // Check if text is justified by comparing TextRect width with natural width
    // Use FreeType to measure text width (similar to canvas renderer)
    float space_width = ctx->font.style ? ctx->font.style->space_width : 4.0f;
    float adjusted_space_width = space_width;

    // Calculate natural width using glyph metrics (excluding trailing spaces)
    float natural_width = 0.0f;
    int space_count = 0;
    size_t content_len = strlen(text_content);
    // Find end of non-whitespace content
    while (content_len > 0 && text_content[content_len - 1] == ' ') {
        content_len--;
    }

    if (ctx->font.ft_face) {
        for (size_t i = 0; i < content_len; i++) {  // Only count up to content_len
            if (text_content[i] == ' ') {
                natural_width += space_width;
                space_count++;
            } else {
                FT_UInt glyph_index = FT_Get_Char_Index(ctx->font.ft_face, text_content[i]);
                if (FT_Load_Glyph(ctx->font.ft_face, glyph_index, FT_LOAD_DEFAULT) == 0) {
                    natural_width += ctx->font.ft_face->glyph->advance.x / 64.0f;
                }
            }
        }
    }

    // If text_rect width is larger than natural width and there are spaces, apply justify
    if (space_count > 0 && natural_width > 0 && text_rect->width > natural_width + 0.5f) {
        float extra_space = text_rect->width - natural_width;
        adjusted_space_width = space_width + (extra_space / space_count);
    }

    // Render text word by word with adjusted spacing
    pdf_set_color(ctx, ctx->color);
    float baseline_offset = font_size * 0.8f;
    float baseline_y = y + baseline_offset;
    float pdf_y = ctx->page_height - baseline_y;

    float x = base_x;
    size_t word_start = 0;
    for (size_t i = 0; i <= strlen(text_content); i++) {
        if (i == strlen(text_content) || text_content[i] == ' ') {
            if (i > word_start) {
                // Render word
                char word[256];
                size_t word_len = i - word_start;
                if (word_len < sizeof(word)) {
                    strncpy(word, text_content + word_start, word_len);
                    word[word_len] = '\0';

                    HPDF_Page_BeginText(ctx->current_page);
                    HPDF_Page_TextOut(ctx->current_page, x, pdf_y, word);
                    HPDF_Page_EndText(ctx->current_page);

                    // Calculate word width using FreeType
                    if (ctx->font.ft_face) {
                        for (size_t j = 0; j < word_len; j++) {
                            FT_UInt glyph_index = FT_Get_Char_Index(ctx->font.ft_face, word[j]);
                            if (FT_Load_Glyph(ctx->font.ft_face, glyph_index, FT_LOAD_DEFAULT) == 0) {
                                x += ctx->font.ft_face->glyph->advance.x / 64.0f;
                            }
                        }
                    }
                }
            }

            // Add space (adjusted if justified)
            if (i < strlen(text_content)) {
                x += adjusted_space_width;
            }
            word_start = i + 1;
        }
    }

    mem_free(text_content);
    text_rect = text_rect->next;
    if (text_rect) { goto NEXT_RECT; }
}

// Render block view with background and borders
void render_block_view_pdf(PdfRenderContext* ctx, ViewBlock* view_block) {
    if (!view_block) return;

    // Save parent context (like SVG renderer)
    BlockBlot pa_block = ctx->block;
    FontBox pa_font = ctx->font;
    Color pa_color = ctx->color;

    // Update font if specified
    if (view_block->font) {
        setup_font(ctx->ui_context, &ctx->font, view_block->font);
        // Update PDF font
        const char* pdf_font_name = get_pdf_font_name(view_block->font->family);
        HPDF_Font font = HPDF_GetFont(ctx->pdf_doc, pdf_font_name, NULL);
        if (font) {
            ctx->current_font = font;
            HPDF_Page_SetFontAndSize(ctx->current_page, font, view_block->font->font_size);
        }
    }

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

    // Save parent font context
    FontBox pa_font = ctx->font;

    // Set font if specified
    if (view_span->font) {
        setup_font(ctx->ui_context, &ctx->font, view_span->font);
        // Update PDF font
        const char* pdf_font_name = get_pdf_font_name(view_span->font->family);
        HPDF_Font font = HPDF_GetFont(ctx->pdf_doc, pdf_font_name, NULL);
        if (font) {
            ctx->current_font = font;
            HPDF_Page_SetFontAndSize(ctx->current_page, font, view_span->font->font_size);
        }
    }

    // Set color if specified
    if (view_span->in_line) {
        ctx->color = view_span->in_line->color;
    }

    // Render children
    render_children_pdf(ctx, (View*)view_span);

    // Restore font context
    ctx->font = pa_font;
}

// Render children recursively
void render_children_pdf(PdfRenderContext* ctx, View* view) {
    if (!view || view->view_type < RDT_VIEW_INLINE) return;

    ViewElement* group = (ViewElement*)view;
    View* child = group->first_child;

    while (child) {
        switch (child->view_type) {
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

            case RDT_VIEW_MATH:
                // MathBox rendering removed - use RDT_VIEW_TEXNODE instead
                log_debug("render_children_pdf: RDT_VIEW_MATH deprecated, skipping");
                break;

            default:
                // Handle other view types if needed
                break;
        }

        child = child->next();
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
    ctx.font.style = &uicon->default_font;

    // Set default font
    ctx.current_font = HPDF_GetFont(ctx.pdf_doc, "Helvetica", NULL);
    if (ctx.current_font) {
        HPDF_Page_SetFontAndSize(ctx.current_page, ctx.current_font, 16.0f);
    }

    // Render the root view
    if (root_view->view_type == RDT_VIEW_BLOCK) {
        render_block_view_pdf(&ctx, (ViewBlock*)root_view);
    } else if (root_view->view_type >= RDT_VIEW_INLINE) {
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
int render_html_to_pdf(const char* html_file, const char* pdf_file, int viewport_width, int viewport_height, float scale) {
    log_debug("render_html_to_pdf called with html_file='%s', pdf_file='%s', viewport=%dx%d, scale=%.2f",
              html_file, pdf_file, viewport_width, viewport_height, scale);

    // Remember if we need to auto-size (viewport was 0)
    bool auto_width = (viewport_width == 0);
    bool auto_height = (viewport_height == 0);

    // Use reasonable defaults for layout if auto-sizing
    int layout_width = viewport_width > 0 ? viewport_width : 800;
    int layout_height = viewport_height > 0 ? viewport_height : 1200;

    // Initialize UI context in headless mode
    UiContext ui_context;
    if (ui_context_init(&ui_context, true) != 0) {
        log_debug("Failed to initialize UI context for PDF rendering");
        return 1;
    }

    // Create a surface for layout calculations with layout dimensions
    ui_context_create_surface(&ui_context, layout_width, layout_height);

    // Update UI context viewport dimensions for layout calculations
    ui_context.window_width = layout_width;
    ui_context.window_height = layout_height;

    // Get current directory for relative path resolution
    Url* cwd = get_current_dir();
    if (!cwd) {
        log_debug("Could not get current directory");
        ui_context_cleanup(&ui_context);
        return 1;
    }

    // Load HTML document
    log_debug("Loading HTML document: %s", html_file);
    DomDocument* doc = load_html_doc(cwd, (char*)html_file, layout_width, layout_height);
    if (!doc) {
        log_debug("Could not load HTML file: %s", html_file);
        url_destroy(cwd);
        ui_context_cleanup(&ui_context);
        return 1;
    }

    // Set scale for rendering (in headless mode, pixel_ratio is always 1.0)
    doc->given_scale = scale;
    doc->scale = scale;

    ui_context.document = doc;

    // Process @font-face rules before layout
    process_document_font_faces(&ui_context, doc);

    // Layout the document
    log_debug("Performing layout...");
    layout_html_doc(&ui_context, doc, false);

    // Calculate actual content dimensions
    int content_max_x = layout_width;
    int content_max_y = layout_height;
    if (doc->view_tree && doc->view_tree->root) {
        int bounds_x = 0, bounds_y = 0;
        calculate_content_bounds(doc->view_tree->root, &bounds_x, &bounds_y);
        // Add some padding to ensure nothing is cut off
        bounds_x += 50;
        bounds_y += 50;

        // If auto-sizing, use content bounds; otherwise use minimum of viewport and content
        if (auto_width) {
            content_max_x = bounds_x;
        } else {
            content_max_x = (bounds_x > layout_width) ? bounds_x : layout_width;
        }
        if (auto_height) {
            content_max_y = bounds_y;
        } else {
            content_max_y = (bounds_y > layout_height) ? bounds_y : layout_height;
        }

        if (auto_width || auto_height) {
            log_info("Auto-sized output dimensions: %dx%d (content bounds with 50px padding)", content_max_x, content_max_y);
        } else {
            log_debug("Calculated content bounds: %dx%d", content_max_x, content_max_y);
        }
    }

    // Render to PDF (apply scale to output dimensions)
    if (doc->view_tree && doc->view_tree->root) {
        log_debug("Rendering view tree to PDF...");
        // PDF output dimensions are scaled; coordinates inside are in CSS pixels with transform
        float pdf_width = content_max_x * scale;
        float pdf_height = content_max_y * scale;
        HPDF_Doc pdf_doc = render_view_tree_to_pdf(&ui_context, doc->view_tree->root,
                                                   pdf_width, pdf_height);
        if (pdf_doc) {
            if (save_pdf_to_file(pdf_doc, pdf_file)) {
                printf("Successfully rendered HTML to PDF: %s\\n", pdf_file);
                HPDF_Free(pdf_doc);
                url_destroy(cwd);
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
    url_destroy(cwd);
    ui_context_cleanup(&ui_context);
    return 1;
}

// ============================================================================
// Math Rendering Functions for PDF
// ============================================================================
// NOTE: MathBox rendering has been removed. Use RDT_VIEW_TEXNODE for math rendering.
// The old MathBox pipeline (RDT_VIEW_MATH) is deprecated.
// ============================================================================
