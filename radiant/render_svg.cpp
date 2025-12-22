#include "render.hpp"
#include "view.hpp"
#include "layout.hpp"
#include "font_face.h"
extern "C" {
#include "../lib/url.h"
}
#include "../lib/strbuf.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <cctype>
#include <cwctype>

// Forward declarations for functions from other modules
int ui_context_init(UiContext* uicon, bool headless);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);
void layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);

typedef struct {
    StrBuf* svg_content;
    int indent_level;
    float viewport_width;
    float viewport_height;
    // Context from parent render context
    FontBox font;
    BlockBlot block;
    Color color;
    UiContext* ui_context;
} SvgRenderContext;

// Forward declarations
void render_block_view_svg(SvgRenderContext* ctx, ViewBlock* view_block);
void render_inline_view_svg(SvgRenderContext* ctx, ViewSpan* view_span);
void render_children_svg(SvgRenderContext* ctx, View* view);
void render_text_view_svg(SvgRenderContext* ctx, ViewText* text);
void calculate_content_bounds(View* view, int* max_x, int* max_y);

// Helper functions for SVG output
void svg_indent(SvgRenderContext* ctx) {
    for (int i = 0; i < ctx->indent_level; i++) {
        strbuf_append_str(ctx->svg_content, "  ");
    }
}

void svg_color_to_string(Color color, char* result) {
    if (color.a == 0) {
        strcpy(result, "transparent");
    } else if (color.a == 255) {
        sprintf(result, "rgb(%d,%d,%d)", color.r, color.g, color.b);
    } else {
        sprintf(result, "rgba(%d,%d,%d,%.3f)", color.r, color.g, color.b, color.a / 255.0f);
    }
}

void render_text_view_svg(SvgRenderContext* ctx, ViewText* text) {
    if (!text || !text->text_data()) return;
    // Extract the text content
    unsigned char* str = text->text_data();

    // Get text-transform from the text node's parent elements
    CssEnum text_transform = CSS_VALUE_NONE;
    DomNode* parent = text->parent;
    while (parent) {
        if (parent->is_element()) {
            DomElement* elem = (DomElement*)parent;
            text_transform = get_text_transform_from_block(elem->blk);
            if (text_transform != CSS_VALUE_NONE) break;
        }
        parent = parent->parent;
    }

    TextRect *text_rect = text->rect;
    NEXT_RECT:
    float x = ctx->block.x + text_rect->x, y = ctx->block.y + text_rect->y;

    // Transform text if needed
    char* text_content = (char*)malloc(text_rect->length * 4 + 1);  // Allocate extra for UTF-8
    if (text_transform != CSS_VALUE_NONE) {
        // Apply text-transform character by character
        unsigned char* src = str + text_rect->start_index;
        unsigned char* src_end = src + text_rect->length;
        char* dst = text_content;
        bool is_word_start = true;

        while (src < src_end) {
            uint32_t codepoint = *src;
            int bytes = 1;

            if (codepoint >= 128) {
                bytes = utf8_to_codepoint(src, &codepoint);
                if (bytes <= 0) bytes = 1;
            }

            // Track word boundaries
            if (is_space(codepoint)) {
                is_word_start = true;
                *dst++ = *src;
                src += bytes;
                continue;
            }

            // Apply transformation
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
        // No transformation - direct copy
        strncpy(text_content, (char*)str + text_rect->start_index, text_rect->length);
        text_content[text_rect->length] = '\0';
    }

    // Calculate natural text width for justify rendering (excluding trailing spaces)
    float natural_width = 0.0f;
    int space_count = 0;
    if (ctx->font.ft_face) {
        // Find end of non-whitespace content
        size_t content_len = strlen(text_content);
        while (content_len > 0 && text_content[content_len - 1] == ' ') {
            content_len--;
        }

        unsigned char* scan = (unsigned char*)text_content;
        unsigned char* content_end = scan + content_len;
        while (scan < content_end) {  // Only scan up to content_end
            if (is_space(*scan)) {
                natural_width += ctx->font.style->space_width;
                space_count++;
                scan++;
            }
            else {
                uint32_t codepoint;
                int bytes = utf8_to_codepoint(scan, &codepoint);
                if (bytes <= 0) { scan++; }
                else { scan += bytes; }

                FT_GlyphSlot glyph = load_glyph(ctx->ui_context, ctx->font.ft_face, ctx->font.style, codepoint, false);
                if (glyph) {
                    natural_width += glyph->advance.x / 64.0;
                } else {
                    natural_width += ctx->font.style->space_width;
                }
            }
        }
    }

    // Calculate word-spacing for justified text
    float word_spacing = 0.0f;
    if (space_count > 0 && natural_width > 0 && text_rect->width > natural_width) {
        // This text is justified - calculate extra space per word
        float extra_space = text_rect->width - natural_width;
        word_spacing = extra_space / space_count;
    }

    // Escape XML entities in text
    size_t transformed_len = strlen(text_content);
    StrBuf* escaped_text = strbuf_new_cap(transformed_len * 2);
    for (size_t i = 0; i < transformed_len; i++) {
        char c = text_content[i];
        switch (c) {
            case '<': strbuf_append_str(escaped_text, "&lt;"); break;
            case '>': strbuf_append_str(escaped_text, "&gt;"); break;
            case '&': strbuf_append_str(escaped_text, "&amp;"); break;
            case '"': strbuf_append_str(escaped_text, "&quot;"); break;
            case '\'': strbuf_append_str(escaped_text, "&#39;"); break;
            default: strbuf_append_char(escaped_text, c); break;
        }
    }

    svg_indent(ctx);

    char color_str[32];
    svg_color_to_string(ctx->color, color_str);

    // y_ppem is already in pixels (not 26.6 fixed point), so use directly
    // ascender is in 26.6 format, so divide by 64 for pixels
    float font_size = ctx->font.ft_face ? (float)ctx->font.ft_face->size->metrics.y_ppem : 16;
    float baseline_y = y + (ctx->font.ft_face ? (ctx->font.ft_face->size->metrics.ascender / 64.0f) : font_size * 0.8f);

    strbuf_append_format(ctx->svg_content,
        "<text x=\"%.2f\" y=\"%.2f\" font-family=\"%s\" font-size=\"%.0f\" fill=\"%s\"",
        x, baseline_y,
        ctx->font.ft_face ? ctx->font.ft_face->family_name : "Arial",
        font_size,
        color_str);

    // Add font style attributes
    if (ctx->font.style->font_weight != CSS_VALUE_NORMAL && ctx->font.style->font_weight != 400) {
        if (ctx->font.style->font_weight >= 700) {
            strbuf_append_str(ctx->svg_content, " font-weight=\"bold\"");
        } else {
            strbuf_append_format(ctx->svg_content, " font-weight=\"%d\"", ctx->font.style->font_weight);
        }
    }

    if (ctx->font.style->font_style == CSS_VALUE_ITALIC) {
        strbuf_append_str(ctx->svg_content, " font-style=\"italic\"");
    }

    // Add text decoration
    if (ctx->font.style->text_deco != CSS_VALUE_NONE) {
        if (ctx->font.style->text_deco == CSS_VALUE_UNDERLINE) {
            strbuf_append_str(ctx->svg_content, " text-decoration=\"underline\"");
        } else if (ctx->font.style->text_deco == CSS_VALUE_OVERLINE) {
            strbuf_append_str(ctx->svg_content, " text-decoration=\"overline\"");
        } else if (ctx->font.style->text_deco == CSS_VALUE_LINE_THROUGH) {
            strbuf_append_str(ctx->svg_content, " text-decoration=\"line-through\"");
        }
    }

    // Add word-spacing for justified text
    if (word_spacing > 0.01f) {
        strbuf_append_format(ctx->svg_content, " word-spacing=\"%.2f\"", word_spacing);
    }

    strbuf_append_format(ctx->svg_content, ">%s</text>\n", escaped_text->str);

    free(text_content);  strbuf_free(escaped_text);
    text_rect = text_rect->next;
    if (text_rect) { goto NEXT_RECT; }
}

void render_bound_svg(SvgRenderContext* ctx, ViewBlock* view) {
    if (!view->bound) return;

    float x = ctx->block.x + view->x;
    float y = ctx->block.y + view->y;
    float width = view->width;
    float height = view->height;

    // Render background
    if (view->bound->background && view->bound->background->color.a > 0) {
        char bg_color[32];
        svg_color_to_string(view->bound->background->color, bg_color);

        svg_indent(ctx);

        // Check for border radius
        if (view->bound->border && (view->bound->border->radius.top_left > 0 ||
            view->bound->border->radius.top_right > 0 ||
            view->bound->border->radius.bottom_left > 0 ||
            view->bound->border->radius.bottom_right > 0)) {

            float rx = view->bound->border->radius.top_left;
            float ry = view->bound->border->radius.top_left;
            strbuf_append_format(ctx->svg_content,
                "<rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" rx=\"%.2f\" ry=\"%.2f\" fill=\"%s\" />\n",
                x, y, width, height, rx, ry, bg_color);
        } else {
            strbuf_append_format(ctx->svg_content,
                "<rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" fill=\"%s\" />\n",
                x, y, width, height, bg_color);
        }
    }

    // Render borders
    if (view->bound->border) {
        BorderProp* border = view->bound->border;

        // Left border
        if (border->width.left > 0 && border->left_color.a > 0) {
            char border_color[32];
            svg_color_to_string(border->left_color, border_color);
            svg_indent(ctx);
            strbuf_append_format(ctx->svg_content,
                "<rect x=\"%.2f\" y=\"%.2f\" width=\"%d\" height=\"%.2f\" fill=\"%s\" />\n",
                x, y, border->width.left, height, border_color);
        }

        // Right border
        if (border->width.right > 0 && border->right_color.a > 0) {
            char border_color[32];
            svg_color_to_string(border->right_color, border_color);
            svg_indent(ctx);
            strbuf_append_format(ctx->svg_content,
                "<rect x=\"%.2f\" y=\"%.2f\" width=\"%d\" height=\"%.2f\" fill=\"%s\" />\n",
                x + width - border->width.right, y, border->width.right, height, border_color);
        }

        // Top border
        if (border->width.top > 0 && border->top_color.a > 0) {
            char border_color[32];
            svg_color_to_string(border->top_color, border_color);
            svg_indent(ctx);
            strbuf_append_format(ctx->svg_content,
                "<rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%d\" fill=\"%s\" />\n",
                x, y, width, border->width.top, border_color);
        }

        // Bottom border
        if (border->width.bottom > 0 && border->bottom_color.a > 0) {
            char border_color[32];
            svg_color_to_string(border->bottom_color, border_color);
            svg_indent(ctx);
            strbuf_append_format(ctx->svg_content,
                "<rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%d\" fill=\"%s\" />\n",
                x, y + height - border->width.bottom, width, border->width.bottom, border_color);
        }
    }
}

void render_block_view_svg(SvgRenderContext* ctx, ViewBlock* block) {
    // Save parent context
    BlockBlot pa_block = ctx->block;
    FontBox pa_font = ctx->font;
    Color pa_color = ctx->color;

    // Update font if specified
    if (block->font) {
        // In a real implementation, we'd call setup_font here
        // For now, just copy the font properties
        ctx->font = pa_font; // Keep parent font for now
    }

    // Render background and borders
    if (block->bound) {
        render_bound_svg(ctx, block);
    }

    // Update position context
    ctx->block.x = pa_block.x + block->x;
    ctx->block.y = pa_block.y + block->y;

    // Update color context
    if (block->in_line && block->in_line->color.c) {
        ctx->color = block->in_line->color;
    }

    // Render embedded image if present
    if (block->embed && block->embed->img) {
        ImageSurface* img = block->embed->img;
        float img_x = ctx->block.x + block->x;
        float img_y = ctx->block.y + block->y;
        float img_width = block->width;
        float img_height = block->height;

        log_debug("[SVG IMAGE RENDER] url=%s, format=%d, img_size=%dx%d, view_size=%.0fx%.0f, pos=(%.0f,%.0f)",
                  img->url && img->url->href ? img->url->href->chars : "unknown",
                  img->format, img->width, img->height,
                  img_width, img_height, img_x, img_y);

        if (img->url && img->url->href) {
            // Convert local file path to href for SVG
            const char* href = img->url->href->chars;
            svg_indent(ctx);
            strbuf_append_format(ctx->svg_content,
                "<image x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" href=\"%s\" "
                "preserveAspectRatio=\"none\" />\n",
                img_x, img_y, img_width, img_height, href);
        }
    }

    // Render children
    if (block->first_child) {
        svg_indent(ctx);
        strbuf_append_format(ctx->svg_content, "<g class=\"block\" data-element=\"%s\">\n", block->node_name());
        ctx->indent_level++;
        render_children_svg(ctx, block->first_child);
        ctx->indent_level--;
        svg_indent(ctx);
        strbuf_append_str(ctx->svg_content, "</g>\n");
    }

    // Restore context
    ctx->block = pa_block;
    ctx->font = pa_font;
    ctx->color = pa_color;
}

void render_inline_view_svg(SvgRenderContext* ctx, ViewSpan* view_span) {
    // Save parent context
    FontBox pa_font = ctx->font;
    Color pa_color = ctx->color;

    // Update font and color if specified
    if (view_span->font) {
        ctx->font = pa_font; // Keep parent font for now
    }

    if (view_span->in_line && view_span->in_line->color.c) {
        log_debug("[SVG COLOR] element=%s has color set: #%02x%02x%02x (was #%02x%02x%02x from parent)",
                  view_span->node_name(),
                  view_span->in_line->color.r, view_span->in_line->color.g, view_span->in_line->color.b,
                  pa_color.r, pa_color.g, pa_color.b);
        ctx->color = view_span->in_line->color;
    } else {
        log_debug("[SVG COLOR] element=%s inheriting color #%02x%02x%02x from parent (in_line=%p, color.c=%u)",
                  view_span->node_name(), pa_color.r, pa_color.g, pa_color.b,
                  view_span->in_line, view_span->in_line ? view_span->in_line->color.c : 0);
    }

    // Render children
    if (view_span->first_child) {
        svg_indent(ctx);
        strbuf_append_format(ctx->svg_content, "<g class=\"inline\" data-element=\"%s\">\n", view_span->node_name());
        ctx->indent_level++;
        render_children_svg(ctx, view_span->first_child);
        ctx->indent_level--;
        svg_indent(ctx);
        strbuf_append_str(ctx->svg_content, "</g>\n");
    }

    // Restore context
    ctx->font = pa_font;
    ctx->color = pa_color;
}

void render_children_svg(SvgRenderContext* ctx, View* view) {
    while (view) {
        switch (view->view_type) {
            case RDT_VIEW_BLOCK:
            case RDT_VIEW_INLINE_BLOCK:
            case RDT_VIEW_TABLE:
            case RDT_VIEW_TABLE_ROW_GROUP:
            case RDT_VIEW_TABLE_ROW:
            case RDT_VIEW_TABLE_CELL:
            case RDT_VIEW_LIST_ITEM:
                render_block_view_svg(ctx, (ViewBlock*)view);
                break;

            case RDT_VIEW_INLINE:
                render_inline_view_svg(ctx, (ViewSpan*)view);
                break;

            case RDT_VIEW_TEXT:
                render_text_view_svg(ctx, (ViewText*)view);
                break;

            default:
                log_debug("Unknown view type in SVG rendering: %d", view->view_type);
                break;
        }
        view = view->next();
    }
}

// Calculate the actual content bounds recursively
void calculate_content_bounds(View* view, int* max_x, int* max_y) {
    if (!view) return;

    if (view->view_type == RDT_VIEW_BLOCK) {
        ViewBlock* block = (ViewBlock*)view;
        int right = block->x + block->width;
        int bottom = block->y + block->height;
        if (right > *max_x) *max_x = right;
        if (bottom > *max_y) *max_y = bottom;
    }
    else if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = (ViewText*)view;
        int right = text->x + text->width;
        int bottom = text->y + text->height;
        if (right > *max_x) *max_x = right;
        if (bottom > *max_y) *max_y = bottom;
    }

    // Recursively check children
    if (view->view_type >= RDT_VIEW_INLINE) {
        ViewElement* group = (ViewElement*)view;
        View* child = group->first_child;
        while (child) {
            calculate_content_bounds(child, max_x, max_y);
            child = child->next_sibling;
        }
    }
}

// Main SVG rendering function
char* render_view_tree_to_svg(UiContext* uicon, View* root_view, int width, int height) {
    if (!root_view || !uicon) {
        return NULL;
    }

    SvgRenderContext ctx;
    memset(&ctx, 0, sizeof(SvgRenderContext));

    ctx.svg_content = strbuf_new_cap(8192);
    ctx.indent_level = 0;
    ctx.viewport_width = width;
    ctx.viewport_height = height;
    ctx.ui_context = uicon;

    // Initialize default font and color
    ctx.color.r = 0; ctx.color.g = 0; ctx.color.b = 0; ctx.color.a = 255; // Black text
    ctx.block.x = 0; ctx.block.y = 0;

    // Initialize font from default
    ctx.font.style = &uicon->default_font;
    ctx.font.ft_face = NULL; // Will be set if needed

    // SVG header
    strbuf_append_format(ctx.svg_content,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        "width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\">\n",
        width, height, width, height);

    ctx.indent_level++;

    // Add background
    svg_indent(&ctx);
    strbuf_append_format(ctx.svg_content,
        "<rect x=\"0\" y=\"0\" width=\"%d\" height=\"%d\" fill=\"white\" />\n",
        width, height);

    // Render the root view
    if (root_view->view_type == RDT_VIEW_BLOCK) {
        render_block_view_svg(&ctx, (ViewBlock*)root_view);
    } else {
        render_children_svg(&ctx, root_view);
    }

    ctx.indent_level--;

    // SVG footer
    strbuf_append_str(ctx.svg_content, "</svg>\n");

    // Extract the final SVG string
    char* result = strdup(ctx.svg_content->str);
    strbuf_free(ctx.svg_content);

    return result;
}

// Function to save SVG to file
bool save_svg_to_file(const char* svg_content, const char* filename) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        log_debug("Failed to open file for writing: %s", filename);
        return false;
    }

    size_t len = strlen(svg_content);
    size_t written = fwrite(svg_content, 1, len, file);
    fclose(file);

    if (written != len) {
        log_debug("Failed to write complete SVG content to file: %s", filename);
        return false;
    }

    return true;
}

// Main function to layout HTML and render to SVG
int render_html_to_svg(const char* html_file, const char* svg_file, int viewport_width, int viewport_height) {
    log_debug("render_html_to_svg called with html_file='%s', svg_file='%s', viewport=%dx%d",
              html_file, svg_file, viewport_width, viewport_height);

    // Initialize UI context in headless mode
    UiContext ui_context;
    if (ui_context_init(&ui_context, true) != 0) {
        log_debug("Failed to initialize UI context for SVG rendering");
        return 1;
    }

    // Create a surface for layout calculations with specified viewport dimensions
    ui_context_create_surface(&ui_context, viewport_width, viewport_height);

    // Update UI context viewport dimensions for layout calculations
    ui_context.window_width = viewport_width;
    ui_context.window_height = viewport_height;

    // Get current directory for relative path resolution
    Url* cwd = get_current_dir();
    if (!cwd) {
        log_debug("Could not get current directory");
        ui_context_cleanup(&ui_context);
        return 1;
    }

    // Load HTML document
    log_debug("Loading HTML document: %s", html_file);
    DomDocument* doc = load_html_doc(cwd, (char*)html_file, viewport_width, viewport_height);
    if (!doc) {
        log_debug("Could not load HTML file: %s", html_file);
        url_destroy(cwd);
        ui_context_cleanup(&ui_context);
        return 1;
    }

    ui_context.document = doc;

    // Process @font-face rules before layout
    process_document_font_faces(&ui_context, doc);

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

        // Use minimum dimensions to ensure reasonable viewport
        if (content_max_x < viewport_width) content_max_x = viewport_width;
        if (content_max_y < viewport_height) content_max_y = viewport_height;

        log_debug("Calculated content bounds: %dx%d", content_max_x, content_max_y);
    }

    // Render to SVG
    if (doc->view_tree && doc->view_tree->root) {
        log_debug("Rendering view tree to SVG...");
        char* svg_content = render_view_tree_to_svg(&ui_context, doc->view_tree->root,
                                                   content_max_x, content_max_y);
        if (svg_content) {
            if (save_svg_to_file(svg_content, svg_file)) {
                printf("Successfully rendered HTML to SVG: %s\n", svg_file);
                free(svg_content);
                url_destroy(cwd);
                ui_context_cleanup(&ui_context);
                return 0;
            } else {
                log_debug("Failed to save SVG to file: %s", svg_file);
                free(svg_content);
            }
        } else {
            log_debug("Failed to render view tree to SVG");
        }
    } else {
        log_debug("No view tree available for rendering");
    }

    // Cleanup
    url_destroy(cwd);
    ui_context_cleanup(&ui_context);
    return 1;
}
