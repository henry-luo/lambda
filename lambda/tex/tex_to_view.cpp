// tex_to_view.cpp - Convert TeX nodes directly to Radiant ViewTree
//
// This bypasses PDF generation by converting TeX typeset output directly
// to Radiant ViewBlocks that can be rendered using FreeType and ThorVG.

#include "tex_to_view.hpp"
#include "tex_node.hpp"
#include "tex_glue.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cmath>

namespace tex {

// ============================================================================
// Forward Declarations
// ============================================================================

static ViewBlock* create_view_block(Pool* pool);
static ViewSpan* create_view_span(Pool* pool);
static void append_child_view(ViewBlock* parent, View* child);
static void process_hlist_nodes(TexNode* hlist, ViewBlock* parent, TexViewContext& ctx);
static void process_vlist_nodes(TexNode* vlist, ViewBlock* parent, TexViewContext& ctx);

// ============================================================================
// Context Creation
// ============================================================================

TexViewContext create_tex_view_context(
    Pool* pool,
    Arena* arena,
    TFMFontManager* fonts,
    DocumentContext& doc_ctx
) {
    TexViewContext ctx = {};
    ctx.pool = pool;
    ctx.arena = arena;
    ctx.fonts = fonts;

    // Page dimensions from document context
    ctx.page_width = doc_ctx.page_width;
    ctx.page_height = doc_ctx.page_height;
    ctx.margin_left = doc_ctx.margin_left;
    ctx.margin_top = doc_ctx.margin_top;

    // Initial position
    ctx.cur_x = doc_ctx.margin_left;
    ctx.cur_y = doc_ctx.margin_top;

    // Default font (Computer Modern Roman 10pt)
    ctx.cur_font_name = "cmr10";
    ctx.cur_font_size = 10.0f;
    ctx.cur_color.r = 0;
    ctx.cur_color.g = 0;
    ctx.cur_color.b = 0;
    ctx.cur_color.a = 255;  // Black

    // Scale: 1 TeX point ≈ 1 CSS pixel at 72dpi
    // Actually TeX uses 72.27 points/inch, CSS uses 96 dpi
    // For simplicity, use 1:1 scaling (can adjust later)
    ctx.scale = 1.0f;

    ctx.char_count = 0;
    ctx.box_count = 0;
    ctx.glue_count = 0;

    return ctx;
}

// ============================================================================
// Font Mapping
// ============================================================================

const char* tex_font_to_system_font(const char* tex_font) {
    // Map TeX Computer Modern fonts to system equivalents
    // These mappings work with FontConfig on most systems

    if (!tex_font) return "serif";

    // Computer Modern Roman variants
    if (strncmp(tex_font, "cmr", 3) == 0) return "CMU Serif";
    if (strncmp(tex_font, "cmbx", 4) == 0) return "CMU Serif";  // Bold
    if (strncmp(tex_font, "cmti", 4) == 0) return "CMU Serif";  // Italic
    if (strncmp(tex_font, "cmsl", 4) == 0) return "CMU Serif";  // Slanted

    // Computer Modern Sans
    if (strncmp(tex_font, "cmss", 4) == 0) return "CMU Sans Serif";

    // Computer Modern Typewriter
    if (strncmp(tex_font, "cmtt", 4) == 0) return "CMU Typewriter Text";

    // Math fonts - fall back to serif
    if (strncmp(tex_font, "cmmi", 4) == 0) return "CMU Serif";
    if (strncmp(tex_font, "cmsy", 4) == 0) return "CMU Serif";
    if (strncmp(tex_font, "cmex", 4) == 0) return "CMU Serif";

    // Default fallback
    return "serif";
}

float tex_to_css_size(float tex_size, float scale) {
    // TeX internal units are scaled points (1/65536 of a point)
    // but our TeX nodes store dimensions in points already
    return tex_size * scale;
}

// ============================================================================
// View Creation Helpers
// ============================================================================

static ViewBlock* create_view_block(Pool* pool) {
    ViewBlock* block = (ViewBlock*)pool_calloc(pool, sizeof(ViewBlock));
    block->view_type = RDT_VIEW_BLOCK;
    block->node_type = DOM_NODE_ELEMENT;
    return block;
}

static ViewSpan* create_view_span(Pool* pool) {
    ViewSpan* span = (ViewSpan*)pool_calloc(pool, sizeof(ViewSpan));
    span->view_type = RDT_VIEW_INLINE;
    span->node_type = DOM_NODE_ELEMENT;
    return span;
}

static DomText* create_view_text(Pool* pool, const char* text_content, size_t len) {
    DomText* text_node = (DomText*)pool_calloc(pool, sizeof(DomText));
    text_node->view_type = RDT_VIEW_TEXT;
    text_node->node_type = DOM_NODE_TEXT;

    // Copy text content
    char* content = (char*)pool_alloc(pool, len + 1);
    memcpy(content, text_content, len);
    content[len] = '\0';
    text_node->text = content;
    text_node->length = len;

    return text_node;
}

static void append_child_view(ViewBlock* parent, View* child) {
    if (!parent || !child) return;

    child->parent = (DomNode*)parent;
    child->next_sibling = nullptr;
    child->prev_sibling = nullptr;

    if (!parent->first_child) {
        parent->first_child = (DomNode*)child;
        parent->last_child = (DomNode*)child;
    } else {
        // Append to last child
        child->prev_sibling = parent->last_child;
        parent->last_child->next_sibling = (DomNode*)child;
        parent->last_child = (DomNode*)child;
    }
}

// ============================================================================
// Font Property Creation
// ============================================================================

static FontProp* create_font_prop(Pool* pool, const char* family, float size,
                                   bool bold, bool italic) {
    FontProp* font = (FontProp*)pool_calloc(pool, sizeof(FontProp));

    // Copy family name
    size_t len = strlen(family);
    font->family = (char*)pool_alloc(pool, len + 1);
    memcpy(font->family, family, len + 1);

    font->font_size = size;
    font->font_weight = bold ? CSS_VALUE_BOLD : CSS_VALUE_NORMAL;
    font->font_style = italic ? CSS_VALUE_ITALIC : CSS_VALUE_NORMAL;
    font->letter_spacing = 0;

    return font;
}

// ============================================================================
// TeX Node to View Conversion
// ============================================================================

ViewSpan* tex_char_to_view(TexNode* char_node, TexViewContext& ctx) {
    if (!char_node || char_node->node_class != NodeClass::Char) {
        return nullptr;
    }

    // Create a span for this character
    ViewSpan* span = create_view_span(ctx.pool);

    // Position the span
    span->x = ctx.cur_x;
    span->y = ctx.cur_y;
    span->width = char_node->width;
    span->height = char_node->height + char_node->depth;

    // Get codepoint from content union
    int32_t codepoint = char_node->content.ch.codepoint;

    // Create text content (single character as UTF-8)
    char text[8] = {0};
    int text_len = 0;
    if (codepoint < 0x80) {
        text[0] = (char)codepoint;
        text_len = 1;
    } else if (codepoint < 0x800) {
        text[0] = 0xC0 | (codepoint >> 6);
        text[1] = 0x80 | (codepoint & 0x3F);
        text_len = 2;
    } else if (codepoint < 0x10000) {
        text[0] = 0xE0 | (codepoint >> 12);
        text[1] = 0x80 | ((codepoint >> 6) & 0x3F);
        text[2] = 0x80 | (codepoint & 0x3F);
        text_len = 3;
    } else {
        text[0] = 0xF0 | (codepoint >> 18);
        text[1] = 0x80 | ((codepoint >> 12) & 0x3F);
        text[2] = 0x80 | ((codepoint >> 6) & 0x3F);
        text[3] = 0x80 | (codepoint & 0x3F);
        text_len = 4;
    }

    // Create text node as child
    DomText* text_node = create_view_text(ctx.pool, text, text_len);
    text_node->x = 0;
    text_node->y = 0;
    text_node->width = span->width;
    text_node->height = span->height;

    // Set font for the span
    const char* font_name = char_node->content.ch.font.name;
    const char* sys_font = tex_font_to_system_font(font_name);
    bool bold = (font_name && strstr(font_name, "bx") != nullptr);
    bool italic = (font_name && (strstr(font_name, "ti") != nullptr ||
                                  strstr(font_name, "sl") != nullptr));
    float font_size = char_node->content.ch.font.size_pt > 0 ?
                      char_node->content.ch.font.size_pt : ctx.cur_font_size;
    span->font = create_font_prop(ctx.pool, sys_font, font_size, bold, italic);

    // Create TextRect for rendering (required by render_text_view)
    TextRect* text_rect = (TextRect*)pool_calloc(ctx.pool, sizeof(TextRect));
    text_rect->x = 0;
    text_rect->y = 0;
    text_rect->width = span->width;
    text_rect->height = span->height;
    text_rect->start_index = 0;
    text_rect->length = text_len;
    text_rect->next = nullptr;
    text_node->rect = text_rect;
    text_node->font = span->font;

    append_child_view((ViewBlock*)span, (View*)text_node);

    // Advance position
    ctx.cur_x += char_node->width;
    ctx.char_count++;

    return span;
}

ViewBlock* tex_rule_to_view(TexNode* rule_node, TexViewContext& ctx) {
    if (!rule_node || rule_node->node_class != NodeClass::Rule) {
        return nullptr;
    }

    ViewBlock* block = create_view_block(ctx.pool);

    block->x = ctx.cur_x;
    block->y = ctx.cur_y;
    block->width = rule_node->width;
    block->height = rule_node->height + rule_node->depth;

    // Create background for the rule (solid black box)
    block->bound = (BoundaryProp*)pool_calloc(ctx.pool, sizeof(BoundaryProp));
    block->bound->background = (BackgroundProp*)pool_calloc(ctx.pool, sizeof(BackgroundProp));
    block->bound->background->color = ctx.cur_color;

    ctx.box_count++;
    return block;
}

// ============================================================================
// HList Processing (Horizontal Box)
// ============================================================================

static void process_hlist_nodes(TexNode* hlist, ViewBlock* parent, TexViewContext& ctx) {
    if (!hlist) return;

    // Save starting position (unused but kept for reference)
    (void)ctx.cur_y;

    // Process each child node in the hlist
    TexNode* node = hlist->first_child;
    while (node) {
        switch (node->node_class) {
            case NodeClass::Char: {
                ViewSpan* span = tex_char_to_view(node, ctx);
                if (span) {
                    append_child_view(parent, (View*)span);
                }
                break;
            }

            case NodeClass::Ligature: {
                // Ligature is similar to Char but uses content.lig
                ViewSpan* span = create_view_span(ctx.pool);
                span->x = ctx.cur_x;
                span->y = ctx.cur_y;
                span->width = node->width;
                span->height = node->height + node->depth;

                int32_t codepoint = node->content.lig.codepoint;
                char text[8] = {0};
                int text_len = 0;
                if (codepoint < 0x80) {
                    text[0] = (char)codepoint;
                    text_len = 1;
                } else if (codepoint < 0x800) {
                    text[0] = 0xC0 | (codepoint >> 6);
                    text[1] = 0x80 | (codepoint & 0x3F);
                    text_len = 2;
                } else {
                    text[0] = 0xE0 | (codepoint >> 12);
                    text[1] = 0x80 | ((codepoint >> 6) & 0x3F);
                    text[2] = 0x80 | (codepoint & 0x3F);
                    text_len = 3;
                }

                DomText* text_node = create_view_text(ctx.pool, text, text_len);
                text_node->width = span->width;
                text_node->height = span->height;

                const char* font_name = node->content.lig.font.name;
                const char* sys_font = tex_font_to_system_font(font_name);
                bool bold = (font_name && strstr(font_name, "bx") != nullptr);
                bool italic = (font_name && strstr(font_name, "ti") != nullptr);
                span->font = create_font_prop(ctx.pool, sys_font, ctx.cur_font_size, bold, italic);

                // Create TextRect for rendering (required by render_text_view)
                TextRect* text_rect = (TextRect*)pool_calloc(ctx.pool, sizeof(TextRect));
                text_rect->x = 0;
                text_rect->y = 0;
                text_rect->width = span->width;
                text_rect->height = span->height;
                text_rect->start_index = 0;
                text_rect->length = text_len;
                text_rect->next = nullptr;
                text_node->rect = text_rect;
                text_node->font = span->font;

                append_child_view((ViewBlock*)span, (View*)text_node);
                append_child_view(parent, (View*)span);
                ctx.cur_x += node->width;
                ctx.char_count++;
                break;
            }

            case NodeClass::Glue: {
                // Glue adds space
                ctx.cur_x += node->width;
                ctx.glue_count++;
                break;
            }

            case NodeClass::Kern: {
                // Kern adjusts spacing
                ctx.cur_x += node->width;
                break;
            }

            case NodeClass::Rule: {
                ViewBlock* rule = tex_rule_to_view(node, ctx);
                if (rule) {
                    append_child_view(parent, (View*)rule);
                    ctx.cur_x += node->width;
                }
                break;
            }

            case NodeClass::HList:
            case NodeClass::HBox: {
                // Nested hlist - create a sub-block
                ViewBlock* sub = create_view_block(ctx.pool);
                sub->x = ctx.cur_x;
                sub->y = ctx.cur_y;
                sub->width = node->width;
                sub->height = node->height + node->depth;

                float save_x = ctx.cur_x;
                float save_y = ctx.cur_y;
                ctx.cur_x = 0;
                ctx.cur_y = 0;

                process_hlist_nodes(node, sub, ctx);

                ctx.cur_x = save_x + node->width;
                ctx.cur_y = save_y;

                append_child_view(parent, (View*)sub);
                ctx.box_count++;
                break;
            }

            case NodeClass::VList:
            case NodeClass::VBox:
            case NodeClass::VTop: {
                // Nested vlist in hlist
                ViewBlock* sub = create_view_block(ctx.pool);
                sub->x = ctx.cur_x;
                sub->y = ctx.cur_y;
                sub->width = node->width;
                sub->height = node->height + node->depth;

                float save_x = ctx.cur_x;
                float save_y = ctx.cur_y;
                ctx.cur_x = 0;
                ctx.cur_y = 0;

                process_vlist_nodes(node, sub, ctx);

                ctx.cur_x = save_x + node->width;
                ctx.cur_y = save_y;

                append_child_view(parent, (View*)sub);
                ctx.box_count++;
                break;
            }

            default:
                // Skip other node types (Penalty, Disc, etc.)
                break;
        }

        node = node->next_sibling;
    }
}

// ============================================================================
// VList Processing (Vertical Box)
// ============================================================================

static void process_vlist_nodes(TexNode* vlist, ViewBlock* parent, TexViewContext& ctx) {
    if (!vlist) return;

    // Process each child node in the vlist
    TexNode* node = vlist->first_child;
    while (node) {
        switch (node->node_class) {
            case NodeClass::HList:
            case NodeClass::HBox: {
                // Create a line block for this hlist
                ViewBlock* line = create_view_block(ctx.pool);
                line->x = ctx.margin_left;
                line->y = ctx.cur_y;
                line->width = node->width;
                line->height = node->height + node->depth;

                float save_x = ctx.cur_x;
                ctx.cur_x = 0;

                process_hlist_nodes(node, line, ctx);

                ctx.cur_x = save_x;
                ctx.cur_y += node->height + node->depth;

                append_child_view(parent, (View*)line);
                ctx.box_count++;
                break;
            }

            case NodeClass::VList:
            case NodeClass::VBox:
            case NodeClass::VTop: {
                // Nested vlist
                ViewBlock* sub = create_view_block(ctx.pool);
                sub->x = ctx.margin_left;
                sub->y = ctx.cur_y;
                sub->width = node->width;
                sub->height = node->height + node->depth;

                float save_y = ctx.cur_y;
                ctx.cur_y = 0;

                process_vlist_nodes(node, sub, ctx);

                ctx.cur_y = save_y + node->height + node->depth;

                append_child_view(parent, (View*)sub);
                ctx.box_count++;
                break;
            }

            case NodeClass::Glue: {
                // Vertical glue adds space
                ctx.cur_y += node->width;  // width is vertical extent for vlist glue
                ctx.glue_count++;
                break;
            }

            case NodeClass::Kern: {
                // Vertical kern
                ctx.cur_y += node->content.kern.amount;
                break;
            }

            case NodeClass::Rule: {
                // Horizontal rule
                ViewBlock* rule = create_view_block(ctx.pool);
                rule->x = ctx.margin_left;
                rule->y = ctx.cur_y;
                rule->width = node->width;
                rule->height = node->height > 0 ? node->height : 0.4f;  // Default rule thickness

                rule->bound = (BoundaryProp*)pool_calloc(ctx.pool, sizeof(BoundaryProp));
                rule->bound->background = (BackgroundProp*)pool_calloc(ctx.pool, sizeof(BackgroundProp));
                rule->bound->background->color = ctx.cur_color;

                ctx.cur_y += rule->height;
                append_child_view(parent, (View*)rule);
                ctx.box_count++;
                break;
            }

            case NodeClass::Penalty: {
                // Penalties don't affect visual output
                break;
            }

            default:
                break;
        }

        node = node->next_sibling;
    }
}

// ============================================================================
// Page to View Conversion
// ============================================================================

ViewBlock* tex_page_to_view(TexNode* page_vlist, TexViewContext& ctx) {
    if (!page_vlist) return nullptr;

    // Create page container
    ViewBlock* page = create_view_block(ctx.pool);
    page->x = 0;
    page->y = 0;
    page->width = ctx.page_width;
    page->height = ctx.page_height;

    // Set white background
    page->bound = (BoundaryProp*)pool_calloc(ctx.pool, sizeof(BoundaryProp));
    page->bound->background = (BackgroundProp*)pool_calloc(ctx.pool, sizeof(BackgroundProp));
    page->bound->background->color.r = 255;
    page->bound->background->color.g = 255;
    page->bound->background->color.b = 255;
    page->bound->background->color.a = 255;

    // Reset position to top-left of content area
    ctx.cur_x = ctx.margin_left;
    ctx.cur_y = ctx.margin_top;

    // Process page content
    process_vlist_nodes(page_vlist, page, ctx);

    ctx.box_count++;

    log_debug("tex_to_view: page converted - chars=%d boxes=%d glue=%d",
              ctx.char_count, ctx.box_count, ctx.glue_count);

    return page;
}

ViewBlock* tex_vlist_to_view(TexNode* vlist, TexViewContext& ctx) {
    return tex_page_to_view(vlist, ctx);
}

ViewBlock* tex_hlist_to_view(TexNode* hlist, TexViewContext& ctx) {
    if (!hlist) return nullptr;

    ViewBlock* container = create_view_block(ctx.pool);
    container->x = ctx.cur_x;
    container->y = ctx.cur_y;
    container->width = hlist->width;
    container->height = hlist->height + hlist->depth;

    float save_x = ctx.cur_x;
    ctx.cur_x = 0;

    process_hlist_nodes(hlist, container, ctx);

    ctx.cur_x = save_x;

    return container;
}

// ============================================================================
// Main Entry Point
// ============================================================================

ViewTree* tex_pages_to_view_tree(
    PageList& pages,
    DocumentContext& ctx,
    Pool* view_pool
) {
    log_info("tex_to_view: converting %d pages to ViewTree", pages.page_count);

    if (pages.page_count == 0 || !pages.pages) {
        log_error("tex_to_view: no pages to convert");
        return nullptr;
    }

    // Create view tree
    ViewTree* tree = (ViewTree*)pool_calloc(view_pool, sizeof(ViewTree));
    tree->pool = view_pool;
    tree->html_version = HTML5;

    // Create root view (scrollable document container)
    ViewBlock* root = create_view_block(view_pool);
    root->x = 0;
    root->y = 0;
    root->width = ctx.page_width;
    root->height = ctx.page_height * pages.page_count;

    // Light gray background for document area
    root->bound = (BoundaryProp*)pool_calloc(view_pool, sizeof(BoundaryProp));
    root->bound->background = (BackgroundProp*)pool_calloc(view_pool, sizeof(BackgroundProp));
    root->bound->background->color.r = 240;
    root->bound->background->color.g = 240;
    root->bound->background->color.b = 240;
    root->bound->background->color.a = 255;

    tree->root = (View*)root;

    // Create view context
    TexViewContext vctx = create_tex_view_context(view_pool, ctx.arena, ctx.fonts, ctx);

    // Convert each page
    float page_offset = 0;
    for (int i = 0; i < pages.page_count; i++) {
        if (!pages.pages[i]) continue;

        // Reset context for new page
        vctx.cur_x = ctx.margin_left;
        vctx.cur_y = ctx.margin_top;
        vctx.char_count = 0;
        vctx.box_count = 0;
        vctx.glue_count = 0;

        ViewBlock* page_view = tex_page_to_view(pages.pages[i], vctx);
        if (page_view) {
            // Offset page vertically
            page_view->y = page_offset;
            append_child_view(root, (View*)page_view);

            log_info("tex_to_view: page %d converted at y=%.1f", i + 1, page_offset);
        }

        page_offset += ctx.page_height + 10;  // 10px gap between pages
    }

    // Update root height
    root->height = page_offset;

    log_info("tex_to_view: ViewTree created with %d pages", pages.page_count);

    return tree;
}

} // namespace tex
