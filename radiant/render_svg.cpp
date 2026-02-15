#include "render.hpp"
#include "view.hpp"
#include "layout.hpp"
#include "font_face.h"
#include "../lib/font/font.h"
#include "../lambda/input/css/dom_element.hpp"
extern "C" {
#include "../lib/url.h"
#include "../lib/memtrack.h"
}
#include "../lib/strbuf.h"
#include "../lib/str.h"
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
void setup_font(UiContext* uicon, FontBox *fbox, FontProp *fprop);

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
void render_column_rules_svg(SvgRenderContext* ctx, ViewBlock* block);
void calculate_content_bounds(View* view, int* max_x, int* max_y);

// Helper functions for SVG output
void svg_indent(SvgRenderContext* ctx) {
    for (int i = 0; i < ctx->indent_level; i++) {
        strbuf_append_str(ctx->svg_content, "  ");
    }
}

void svg_color_to_string(Color color, char* result) {
    if (color.a == 0) {
        str_copy(result, 32, "transparent", 11);
    } else if (color.a == 255) {
        str_fmt(result, 32, "rgb(%d,%d,%d)", color.r, color.g, color.b);
    } else {
        str_fmt(result, 32, "rgba(%d,%d,%d,%.3f)", color.r, color.g, color.b, color.a / 255.0f);
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
    char* text_content = (char*)mem_alloc(text_rect->length * 4 + 1, MEM_CAT_RENDER);  // Allocate extra for UTF-8
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
                bytes = str_utf8_decode((const char*)src, (size_t)(src_end - src), &codepoint);
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
    if (ctx->font.font_handle) {
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
                int bytes = str_utf8_decode((const char*)scan, (size_t)(content_end - scan), &codepoint);
                if (bytes <= 0) { scan++; }
                else { scan += bytes; }

                FT_GlyphSlot glyph = (FT_GlyphSlot)load_glyph(ctx->ui_context, ctx->font.font_handle, ctx->font.style, codepoint, false);
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

    // Use CSS font-size from style, fallback to 16 if not available
    float font_size = ctx->font.style->font_size > 0 ? ctx->font.style->font_size : 16;
    // Use font ascender from FontProp (already in pixels), or fallback to 80% of font_size
    float baseline_y = y + (ctx->font.style->ascender > 0 ? ctx->font.style->ascender : font_size * 0.8f);

    strbuf_append_format(ctx->svg_content,
        "<text x=\"%.2f\" y=\"%.2f\" font-family=\"%s\" font-size=\"%.0f\" fill=\"%s\"",
        x, baseline_y,
        ctx->font.font_handle ? font_handle_get_family_name(ctx->font.font_handle) : "Arial",
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

    mem_free(text_content);  strbuf_free(escaped_text);
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

// Render multi-column rules (vertical lines between columns)
void render_column_rules_svg(SvgRenderContext* ctx, ViewBlock* block) {
    if (!block->multicol) return;

    MultiColumnProp* mc = block->multicol;

    // Only render if we have rules and multiple columns
    if (mc->computed_column_count <= 1 || mc->rule_width <= 0 ||
        mc->rule_style == CSS_VALUE_NONE) {
        return;
    }

    float column_width = mc->computed_column_width;
    float gap = mc->column_gap_is_normal ? 16.0f : mc->column_gap;

    // Calculate block position
    float block_x = ctx->block.x + block->x;
    float block_y = ctx->block.y + block->y;

    // Adjust for padding
    if (block->bound) {
        block_x += block->bound->padding.left;
        block_y += block->bound->padding.top;
    }

    // Rule height is the content area height
    float rule_height = block->height;
    if (block->bound) {
        rule_height -= block->bound->padding.top + block->bound->padding.bottom;
        if (block->bound->border) {
            rule_height -= block->bound->border->width.top + block->bound->border->width.bottom;
        }
    }

    // Ensure minimum rule height - compute from children if needed
    if (rule_height <= 0) {
        View* child = (View*)block->first_child;
        float max_bottom = 0;
        while (child) {
            if (child->is_element()) {
                ViewBlock* child_block = (ViewBlock*)child;
                float child_bottom = child_block->y + child_block->height;
                if (child_bottom > max_bottom) max_bottom = child_bottom;
            }
            child = child->next();
        }
        rule_height = max_bottom;
    }

    if (rule_height <= 0) return;

    // Convert color to string
    char rule_color_str[32];
    svg_color_to_string(mc->rule_color, rule_color_str);

    log_debug("[MULTICOL SVG] Rendering %d column rules, width=%.1f, style=%d, height=%.1f",
              mc->computed_column_count - 1, mc->rule_width, mc->rule_style, rule_height);

    // Draw rule between each pair of columns
    for (int i = 0; i < mc->computed_column_count - 1; i++) {
        float rule_x = block_x + (i + 1) * column_width + i * gap + gap / 2.0f;

        svg_indent(ctx);

        // Different stroke patterns for different styles
        if (mc->rule_style == CSS_VALUE_DOTTED) {
            strbuf_append_format(ctx->svg_content,
                "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" "
                "stroke=\"%s\" stroke-width=\"%.2f\" stroke-dasharray=\"%.2f,%.2f\" />\n",
                rule_x, block_y, rule_x, block_y + rule_height,
                rule_color_str, mc->rule_width, mc->rule_width, mc->rule_width * 2);
        } else if (mc->rule_style == CSS_VALUE_DASHED) {
            strbuf_append_format(ctx->svg_content,
                "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" "
                "stroke=\"%s\" stroke-width=\"%.2f\" stroke-dasharray=\"%.2f,%.2f\" />\n",
                rule_x, block_y, rule_x, block_y + rule_height,
                rule_color_str, mc->rule_width, mc->rule_width * 3, mc->rule_width * 2);
        } else if (mc->rule_style == CSS_VALUE_DOUBLE) {
            // Double: two lines
            float thin_width = mc->rule_width / 3.0f;
            float offset = mc->rule_width / 2.0f;
            strbuf_append_format(ctx->svg_content,
                "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" "
                "stroke=\"%s\" stroke-width=\"%.2f\" />\n",
                rule_x - offset, block_y, rule_x - offset, block_y + rule_height,
                rule_color_str, thin_width);
            svg_indent(ctx);
            strbuf_append_format(ctx->svg_content,
                "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" "
                "stroke=\"%s\" stroke-width=\"%.2f\" />\n",
                rule_x + offset, block_y, rule_x + offset, block_y + rule_height,
                rule_color_str, thin_width);
        } else {
            // Solid (default)
            strbuf_append_format(ctx->svg_content,
                "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" "
                "stroke=\"%s\" stroke-width=\"%.2f\" />\n",
                rule_x, block_y, rule_x, block_y + rule_height,
                rule_color_str, mc->rule_width);
        }

        log_debug("[MULTICOL SVG] Rule %d at x=%.1f, height=%.1f", i, rule_x, rule_height);
    }
}

void render_block_view_svg(SvgRenderContext* ctx, ViewBlock* block) {
    // Save parent context
    BlockBlot pa_block = ctx->block;
    FontBox pa_font = ctx->font;
    Color pa_color = ctx->color;

    // Update font if specified
    if (block->font) {
        setup_font(ctx->ui_context, &ctx->font, block->font);
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

    // Render multi-column rules between columns
    if (block->multicol && block->multicol->computed_column_count > 1) {
        render_column_rules_svg(ctx, block);
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
        setup_font(ctx->ui_context, &ctx->font, view_span->font);
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

            case RDT_VIEW_MATH:
                // MathBox rendering removed - use RDT_VIEW_TEXNODE instead
                log_debug("render_children_svg: RDT_VIEW_MATH deprecated, skipping");
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

// Render caret to SVG
static void render_caret_svg(SvgRenderContext* ctx, RadiantState* state) {
    if (!state || !state->caret || !state->caret->visible) return;
    if (!state->caret->view) return;

    CaretState* caret = state->caret;
    View* view = caret->view;

    // Calculate absolute position (CSS pixels)
    float x = caret->x;
    float y = caret->y;

    // Walk up the tree to get absolute coordinates
    View* parent = view;
    while (parent) {
        if (parent->view_type == RDT_VIEW_BLOCK) {
            x += ((ViewBlock*)parent)->x;
            y += ((ViewBlock*)parent)->y;
        }
        parent = parent->parent;
    }

    // Add iframe offset (if the caret is inside an iframe, parent chain stops at iframe doc root)
    x += caret->iframe_offset_x;
    y += caret->iframe_offset_y;

    float height = caret->height;

    // Render caret as a line
    svg_indent(ctx);
    strbuf_append_format(ctx->svg_content,
        "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" "
        "stroke=\"black\" stroke-width=\"1.5\" id=\"caret\" />\n",
        x, y, x, y + height);

    log_debug("[CARET SVG] Rendered caret at (%.1f, %.1f) height=%.1f", x, y, height);
}

// Main SVG rendering function
char* render_view_tree_to_svg(UiContext* uicon, View* root_view, int width, int height, RadiantState* state) {
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
    ctx.font.font_handle = NULL;

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

    // Render caret if present
    render_caret_svg(&ctx, state);

    ctx.indent_level--;

    // SVG footer
    strbuf_append_str(ctx.svg_content, "</svg>\n");

    // Extract the final SVG string
    char* result = mem_strdup(ctx.svg_content->str, MEM_CAT_RENDER);
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
// scale: User-specified scale factor (default 1.0, use 2.0 for high-DPI output)
int render_html_to_svg(const char* html_file, const char* svg_file, int viewport_width, int viewport_height, float scale) {
    log_debug("render_html_to_svg called with html_file='%s', svg_file='%s', viewport=%dx%d, scale=%.2f",
              html_file, svg_file, viewport_width, viewport_height, scale);

    // Validate scale
    if (scale <= 0) scale = 1.0f;

    // Remember if we need to auto-size (viewport was 0)
    bool auto_width = (viewport_width == 0);
    bool auto_height = (viewport_height == 0);

    // Use reasonable defaults for layout if auto-sizing
    int layout_width = viewport_width > 0 ? viewport_width : 1200;
    int layout_height = viewport_height > 0 ? viewport_height : 800;

    // Initialize UI context in headless mode
    UiContext ui_context;
    if (ui_context_init(&ui_context, true) != 0) {
        log_debug("Failed to initialize UI context for SVG rendering");
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

    ui_context.document = doc;

    // Set document scale for rendering
    doc->given_scale = scale;
    doc->scale = scale;  // In headless mode, pixel_ratio is always 1.0

    // Process @font-face rules before layout
    process_document_font_faces(&ui_context, doc);

    // Layout the document (produces CSS logical pixels)
    log_debug("Performing layout...");
    layout_html_doc(&ui_context, doc, false);

    // Calculate actual content dimensions (in CSS logical pixels)
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

    // Render to SVG (apply scale to output dimensions)
    if (doc->view_tree && doc->view_tree->root) {
        log_debug("Rendering view tree to SVG...");
        // SVG output dimensions are scaled; coordinates inside are in CSS pixels with viewBox transform
        int svg_width = (int)(content_max_x * scale);
        int svg_height = (int)(content_max_y * scale);
        char* svg_content = render_view_tree_to_svg(&ui_context, doc->view_tree->root,
                                                   svg_width, svg_height, doc->state);
        if (svg_content) {
            if (save_svg_to_file(svg_content, svg_file)) {
                printf("Successfully rendered HTML to SVG: %s\n", svg_file);
                mem_free(svg_content);
                url_destroy(cwd);
                ui_context_cleanup(&ui_context);
                return 0;
            } else {
                log_debug("Failed to save SVG to file: %s", svg_file);
                mem_free(svg_content);
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

// ============================================================================
// Math Rendering Functions for SVG
// ============================================================================

using namespace radiant;

// Helper to escape XML special characters for SVG text content
static void escape_xml_text(const char* text, StrBuf* buf) {
    while (*text) {
        switch (*text) {
            case '<': strbuf_append_str(buf, "&lt;"); break;
            case '>': strbuf_append_str(buf, "&gt;"); break;
            case '&': strbuf_append_str(buf, "&amp;"); break;
            default: strbuf_append_char(buf, *text); break;
        }
        text++;
    }
}

// Math Rendering Functions for SVG
// ============================================================================
// NOTE: MathBox rendering has been removed. Use RDT_VIEW_TEXNODE for math rendering.
// The old MathBox pipeline (RDT_VIEW_MATH) is deprecated.
// ============================================================================
