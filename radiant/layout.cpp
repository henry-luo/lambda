#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"
#include "layout_positioned.hpp"
#include "layout_bfc.hpp"
#include "font_face.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H

#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_style.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/lambda-data.hpp"

void view_pool_init(ViewTree* tree);
void view_pool_destroy(ViewTree* tree);
// Function declaration moved to layout.hpp
char* read_text_file(const char *filename);
void finalize_block_flow(LayoutContext* lycon, ViewBlock* block, CssEnum display);
// Forward declarations
void layout_inline(LayoutContext* lycon, DomNode *elmt, DisplayValue display);
void adjust_text_bounds(ViewText* text);
// resolve default style for HTML inline elements
void apply_element_default_style(LayoutContext* lycon, DomNode* elmt);

bool is_space(char c) {
    return c == ' ' || c == '\t' || c== '\r' || c == '\n';
}

bool is_only_whitespace(const char* str) {
    if (!str) return true;
    while (*str) {
        if (!is_space(*str)) return false;
        str++;
    }
    return true;
}

// Constant for fsSelection bit 7 (USE_TYPO_METRICS)
constexpr uint16_t OS2_FS_SELECTION_USE_TYPO_METRICS = 0x0080;

// Read OS/2 table metrics using FreeType
// Reference: Chrome Blink simple_font_data.cc TypoAscenderAndDescender()
// Chrome checks fsSelection bit 7 (USE_TYPO_METRICS) to decide which metrics to use
TypoMetrics get_os2_typo_metrics(FT_Face face) {
    TypoMetrics result = {0, 0, 0, false, false};

    TT_OS2* os2 = (TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    if (!os2) {
        log_debug("No OS/2 table available for font: %s", face->family_name);
        return result;  // No OS/2 table available
    }

    // Convert from font units to CSS pixels
    // scale = ppem / units_per_EM
    float scale = (float)face->size->metrics.y_ppem / face->units_per_EM;

    // Check fsSelection bit 7 (USE_TYPO_METRICS)
    result.use_typo_metrics = (os2->fsSelection & OS2_FS_SELECTION_USE_TYPO_METRICS) != 0;

    result.ascender = os2->sTypoAscender * scale;
    result.descender = -os2->sTypoDescender * scale;  // Make positive (OS/2 descender is negative)
    // CSS spec: line gap must be floored at zero
    result.line_gap = (os2->sTypoLineGap > 0) ? (os2->sTypoLineGap * scale) : 0.0f;
    result.valid = true;

    log_debug("OS/2 typo metrics for %s: asc=%.2f, desc=%.2f, gap=%.2f, USE_TYPO=%s (raw: %d, %d, %d, fsSelection=0x%04x)",
              face->family_name, result.ascender, result.descender, result.line_gap,
              result.use_typo_metrics ? "yes" : "no",
              os2->sTypoAscender, os2->sTypoDescender, os2->sTypoLineGap, os2->fsSelection);

    return result;
}

// Calculate normal line height following CSS Inline Layout Module Level 3 spec
// and Chrome Blink implementation (simple_font_data.cc PlatformInit)
// Chrome uses OS/2 sTypo* metrics when USE_TYPO_METRICS flag is set.
// Otherwise it uses HHEA metrics.
// Formula: round(ascender) + round(descender) + round(line_gap)
float calc_normal_line_height(FT_Face face) {
    // Get OS/2 sTypo metrics
    TypoMetrics typo = get_os2_typo_metrics(face);

    // Only use sTypo metrics when USE_TYPO_METRICS flag is set (fsSelection bit 7)
    // Chrome checks this flag to decide which metrics to use for line-height
    if (typo.valid && typo.use_typo_metrics) {
        // Chrome formula: round each component separately, then sum
        long asc_r = lroundf(typo.ascender);
        long desc_r = lroundf(typo.descender);
        long gap_r = lroundf(typo.line_gap);
        float line_height = (float)(asc_r + desc_r + gap_r);
        log_debug("Normal line height (sTypo, USE_TYPO_METRICS=1): %.2f (asc=%ld + desc=%ld + gap=%ld) for %s",
                  line_height, asc_r, desc_r, gap_r, face->family_name);
        return line_height;
    }

    // Use FreeType's computed metrics (HHEA-based) when:
    // 1. No OS/2 table available, OR
    // 2. USE_TYPO_METRICS flag is NOT set
    // Note: Use FreeType's height directly as it represents the recommended line spacing
    float ft_height = face->size->metrics.height / 64.0f;
    float line_height = roundf(ft_height);
    log_debug("Normal line height (HHEA height): %.2f for %s",
              line_height, face->family_name);
    return line_height;
}

CssValue inherit_line_height(LayoutContext* lycon, ViewBlock* block) {
    // Inherit line height from parent
    INHERIT:
    ViewElement* parent = block->parent_view();
    if (parent) { // parent can be block or span
        // inherit the specified css value, not the resolved value
        if (parent->blk && parent->blk->line_height) {
            if (parent->blk->line_height->type == CSS_VALUE_TYPE_KEYWORD &&
                parent->blk->line_height->data.keyword == CSS_VALUE_INHERIT) {
                block = (ViewBlock*)parent;
                goto INHERIT;
            }
            return *parent->blk->line_height;
        }
        block = (ViewBlock*)parent;
        goto INHERIT;
    }
    else { // initial value - 'normal'
        CssValue normal_value;
        normal_value.type = CSS_VALUE_TYPE_KEYWORD;
        normal_value.data.keyword = CSS_VALUE_NORMAL;
        return normal_value;
    }
}

void setup_line_height(LayoutContext* lycon, ViewBlock* block) {
    CssValue value;
    if (block->blk && block->blk->line_height) {
        if (block->blk->line_height->type == CSS_VALUE_TYPE_KEYWORD &&
            block->blk->line_height->data.keyword == CSS_VALUE_INHERIT) {
            value = inherit_line_height(lycon, block);
        } else {
            value = *block->blk->line_height;
        }
    } else { // normal initial value
        value.type = CSS_VALUE_TYPE_KEYWORD;
        value.data.keyword = CSS_VALUE_NORMAL;
    }
    if (value.type == CSS_VALUE_TYPE_KEYWORD && value.data.keyword == CSS_VALUE_NORMAL) {
        // 'normal' line height
        lycon->block.line_height = calc_normal_line_height(lycon->font.ft_face);
        log_debug("normal lineHeight: %f", lycon->block.line_height);
    } else {
        // resolve length/number/percentage
        float resolved_height =
        value.type == CSS_VALUE_TYPE_NUMBER ?
            value.data.number.value * lycon->font.current_font_size :
            resolve_length_value(lycon, CSS_PROPERTY_LINE_HEIGHT, &value);
        lycon->block.line_height = resolved_height;
        log_debug("resolved line height: %f", lycon->block.line_height);
    }
}

// DomNode style resolution function
// Ensures styles are resolved only once per layout pass using styles_resolved flag
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon) {
    if (node && node->is_element()) {
        DomElement* dom_elem = node->as_element();
        if (dom_elem && dom_elem->specified_style) {
            // Check if styles already resolved in this layout pass
            // IMPORTANT: Skip this check during measurement mode (is_measuring=true)
            // because measurement passes should not permanently mark styles as resolved
            if (dom_elem->styles_resolved && !lycon->is_measuring) {
                log_debug("[CSS] Skipping style resolution for <%s> - already resolved",
                    dom_elem->tag_name ? dom_elem->tag_name : "unknown");
                return;
            }

            // resolve element default styles
            apply_element_default_style(lycon, dom_elem);

            // Lambda CSS: use the full implementation from resolve_css_style.cpp
            resolve_lambda_css_styles(dom_elem, lycon);

            // Mark as resolved for this layout pass
            // Don't mark as resolved during measurement mode - let the actual layout pass do that
            if (!lycon->is_measuring) {
                dom_elem->styles_resolved = true;
                log_debug("[CSS] Resolved styles for <%s> - marked as resolved",
                    dom_elem->tag_name ? dom_elem->tag_name : "unknown");
            } else {
                log_debug("[CSS] Resolved styles for <%s> in measurement mode - not marking resolved",
                    dom_elem->tag_name ? dom_elem->tag_name : "unknown");
            }
        }
    }
}

float calculate_vertical_align_offset(LayoutContext* lycon, CssEnum align, float item_height, float line_height, float baseline_pos, float item_baseline) {
    log_debug("calculate vertical align: align=%d, item_height=%f, line_height=%f, baseline_pos=%f, item_baseline=%f",
        align, item_height, line_height, baseline_pos, item_baseline);
    switch (align) {
    case CSS_VALUE_BASELINE:
        return baseline_pos - item_baseline;
    case CSS_VALUE_TOP:
        return 0;
    case CSS_VALUE_MIDDLE:
        return (line_height - item_height) / 2;
    case CSS_VALUE_BOTTOM:
        log_debug("bottom-aligned-text: line %d", line_height);
        return line_height - item_height;
    case CSS_VALUE_TEXT_TOP:
        // align with the top of the parent's font
        return baseline_pos - lycon->block.init_ascender;
    case CSS_VALUE_TEXT_BOTTOM:
        // align with the bottom of the parent's font
        return baseline_pos + lycon->block.init_descender - item_height;
    case CSS_VALUE_SUB:
        // Subscript position (approximately 0.3em lower)
        return baseline_pos - item_baseline + 0.3 * line_height;
    case CSS_VALUE_SUPER:
        // Superscript position (approximately 0.3em higher)
        return baseline_pos - item_baseline - 0.3 * line_height;
    default:
        return baseline_pos - item_baseline; // Default to baseline
    }
}

void span_vertical_align(LayoutContext* lycon, ViewSpan* span) {
    FontBox pa_font = lycon->font;  CssEnum pa_line_align = lycon->line.vertical_align;
    log_debug("span_vertical_align");
    View* child = span->first_child;
    if (child) {
        if (span->font) {
            setup_font(lycon->ui_context, &lycon->font, span->font);
        }
        if (span->in_line && span->in_line->vertical_align) {
            lycon->line.vertical_align = span->in_line->vertical_align;
        }
        do {
            view_vertical_align(lycon, child);
            child = child->next();
        } while (child);
    }
    lycon->font = pa_font;  lycon->line.vertical_align = pa_line_align;
}

// apply vertical alignment to a view
void view_vertical_align(LayoutContext* lycon, View* view) {
    log_debug("view_vertical_align: view=%d", view->view_type);
    float line_height = max(lycon->block.line_height, lycon->line.max_ascender + lycon->line.max_descender);
    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text_view = (ViewText*)view;
        TextRect* rect = text_view->rect;
        while (rect) {
            float item_height = rect->height;
            // for text, baseline is at font.ascender
            log_debug("text view font: %p", text_view->font);
            float item_baseline = text_view->font ? text_view->font->ascender : item_height;
            float vertical_offset = calculate_vertical_align_offset(lycon, lycon->line.vertical_align, item_height,
                line_height, lycon->line.max_ascender, item_baseline);
            log_debug("vertical-adjusted-text: y=%d, adv=%d, offset=%f, line=%f, hg=%f, txt='%.*t'",
                rect->y, lycon->block.advance_y, vertical_offset, lycon->block.line_height, item_height,
                rect->length, text_view->text_data() + rect->start_index);
            rect->y = lycon->block.advance_y + max(vertical_offset, 0);
            rect = rect->next;
        }
        adjust_text_bounds(text_view);
    }
    else if (view->view_type == RDT_VIEW_INLINE_BLOCK) {
        ViewBlock* block = (ViewBlock*)view;
        float item_height = block->height + (block->bound ?
            block->bound->margin.top + block->bound->margin.bottom : 0);
        float item_baseline = block->height + (block->bound ? block->bound->margin.top: 0);
        CssEnum align = block->in_line && block->in_line->vertical_align ?
            block->in_line->vertical_align : lycon->line.vertical_align;
        float vertical_offset = calculate_vertical_align_offset(lycon, align, item_height,
            line_height, lycon->line.max_ascender, item_baseline);
        block->y = lycon->block.advance_y + max(vertical_offset, 0) + (block->bound ? block->bound->margin.top : 0);
        log_debug("vertical-adjusted-inline-block: y=%f, adv_y=%f, offset=%f, line=%f, blk=%f, max_asc=%f, max_desc=%f",
            block->y, lycon->block.advance_y, vertical_offset, lycon->block.line_height, item_height, lycon->line.max_ascender, lycon->line.max_descender);
    }
    else if (view->view_type == RDT_VIEW_INLINE) {
        // for inline elements, apply to all children
        ViewSpan* span = (ViewSpan*)view;
        span_vertical_align(lycon, span);
    }
    else {
        log_debug("view_vertical_align: unknown view type %d", view->view_type);
    }
}

void view_line_align(LayoutContext* lycon, float offset, View* view) {
    while (view) {
        log_debug("view line align: %d", view->view_type);
        view->x += offset;
        if (view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)view;
            text->x += offset;
            TextRect* rect = text->rect;
            while (rect) {
                rect->x += offset;
                rect = rect->next;
            }
        }
        else if (view->view_type == RDT_VIEW_INLINE) {
            ViewSpan* sp = (ViewSpan*)view;
            if (sp->first_child) view_line_align(lycon, offset, sp->first_child);
        }
        // else if (view->is_block()) {
        //     view->x += offset;
        // }
        // else {} // br
        view = view->next();
    }
}

void line_align(LayoutContext* lycon) {
    // align the views in the line
    log_debug("line align");
    if (lycon->block.text_align != CSS_VALUE_LEFT) {
        View* view = lycon->line.start_view;
        if (view) {
            float line_width = lycon->line.advance_x -lycon->line.left;
            float offset = 0;
            if (lycon->block.text_align == CSS_VALUE_CENTER) {
                offset = (lycon->block.content_width - line_width) / 2;
            }
            else if (lycon->block.text_align == CSS_VALUE_RIGHT) {
                offset = lycon->block.content_width - line_width;
            }
            if (offset <= 0) return;  // no need to adjust the views
            view_line_align(lycon, offset, view);
        }
    }
    log_debug("end of line align");
}

void layout_flow_node(LayoutContext* lycon, DomNode *node) {
    log_debug("layout node %s, advance_y: %f", node->node_name(), lycon->block.advance_y);

    // Skip HTML comments (Lambda CSS parser creates these as elements with name "!--")
    const char* node_name = node->node_name();
    if (node_name && (strcmp(node_name, "!--") == 0 || strcmp(node_name, "#comment") == 0)) {
        log_debug("skipping HTML comment node");
        return;
    }

    if (node->is_element()) {
        DomElement* elem = node->as_element();

        // Skip floats that were pre-laid in the float pre-pass
        if (elem->float_prelaid) {
            log_debug("skipping pre-laid float: %s", node->node_name());
            return;
        }

        // Use resolve_display_value which handles both Lexbor and Lambda CSS nodes
        DisplayValue display = resolve_display_value(node);
        log_debug("processing element: %s, with display: outer=%d, inner=%d", node->node_name(), display.outer, display.inner);

        // CSS 2.2 Section 9.7: When float is not 'none', display is computed as 'block'
        // Check float property from specified styles (before view is created)
        CssEnum float_value = CSS_VALUE_NONE;

        // First check if position is already resolved
        if (elem->position) {
            float_value = elem->position->float_prop;
        } else if (elem->specified_style && elem->specified_style->tree) {
            // Check float property from CSS style tree
            AvlNode* float_node = avl_tree_search(elem->specified_style->tree, CSS_PROPERTY_FLOAT);
            if (float_node) {
                StyleNode* style_node = (StyleNode*)float_node->declaration;
                if (style_node && style_node->winning_decl && style_node->winning_decl->value) {
                    CssValue* val = style_node->winning_decl->value;
                    if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                        float_value = val->data.keyword;
                    }
                }
            }
        }

        if (float_value == CSS_VALUE_LEFT || float_value == CSS_VALUE_RIGHT) {
            // Float transforms most display values to block
            if (display.outer != CSS_VALUE_NONE) {
                log_debug("Float on %s: transforming display from outer=%d to BLOCK (float=%d)",
                          node->node_name(), display.outer, float_value);
                display.outer = CSS_VALUE_BLOCK;
                // Keep inner display but treat as flow for layout purposes if it's a table type
                if (display.inner == CSS_VALUE_TABLE_ROW_GROUP ||
                    display.inner == CSS_VALUE_TABLE_HEADER_GROUP ||
                    display.inner == CSS_VALUE_TABLE_FOOTER_GROUP ||
                    display.inner == CSS_VALUE_TABLE_ROW ||
                    display.inner == CSS_VALUE_TABLE_COLUMN ||
                    display.inner == CSS_VALUE_TABLE_COLUMN_GROUP ||
                    display.inner == CSS_VALUE_TABLE_CAPTION) {
                    display.inner = CSS_VALUE_FLOW;
                }
            }
        }

        if (strcmp(node->node_name(), "table") == 0) {
            log_debug("TABLE ELEMENT in layout_flow_node - outer=%d, inner=%d (TABLE=%d)",
                   display.outer, display.inner, CSS_VALUE_TABLE);
        }
        if (strcmp(node->node_name(), "tbody") == 0) {
            printf("DEBUG: TBODY in layout_flow_node - outer=%d, inner=%d\n", display.outer, display.inner);
            printf("DEBUG: TBODY current position before layout_block: x=%.1f, y=%.1f\n",
                   ((View*)lycon->view)->x, ((View*)lycon->view)->y);
        }
        switch (display.outer) {
        case CSS_VALUE_BLOCK:  case CSS_VALUE_INLINE_BLOCK:  case CSS_VALUE_LIST_ITEM:
        case CSS_VALUE_TABLE_CELL:  // CSS display: table-cell on non-table elements
            layout_block(lycon, node, display);
            break;
        case CSS_VALUE_INLINE:
            layout_inline(lycon, node, display);
            break;
        case CSS_VALUE_NONE:
            log_debug("skipping element of display: none");
            break;
        default:
            log_debug("unknown display type: outer=%d", display.outer);
            // skip the element
        }
    }
    else if (node->is_text()) {
        const unsigned char* str = node->text_data();
        log_debug("layout_text: '%t'", str);
        // skip whitespace at end of block
        if (!node->next_sibling && node->parent->is_block() && is_only_whitespace((const char*)str)) {
            node->view_type = RDT_VIEW_NONE;
            log_debug("skipping whitespace text at end of block");
        }
        else {
            layout_text(lycon, node);
        }
    }
    else {
        log_debug("layout unknown node type: %d", node->node_type);
        // skip the node
    }
    log_debug("end flow node, block advance_y: %d", lycon->block.advance_y);
}

void layout_html_root(LayoutContext* lycon, DomNode* elmt) {
    log_debug("layout html root");
    log_debug("DEBUG: elmt=%p, type=%d", (void*)elmt, elmt ? (int)elmt->node_type : -1);
    //log_debug("DEBUG: About to call apply_header_style");
    //apply_header_style(lycon);
    log_debug("DEBUG: apply_header_style complete");

    // init context
    log_debug("DEBUG: Initializing layout context");
    lycon->elmt = elmt;
    lycon->root_font_size = lycon->font.current_font_size = -1;  // unresolved yet
    lycon->block.max_width = lycon->block.content_width = lycon->ui_context->window_width;
    // Set root element height to viewport to enable scrollbars when content overflows
    lycon->block.content_height = lycon->ui_context->window_height;
    lycon->block.advance_y = 0;  lycon->block.line_height = -1;
    lycon->block.text_align = CSS_VALUE_LEFT;

    // Set available space to viewport dimensions
    lycon->available_space = AvailableSpace::make_width_definite(lycon->ui_context->window_width);

    line_init(lycon, 0, lycon->block.content_width);
    Blockbox pa_block = lycon->block;  lycon->block.pa_block = &pa_block;

    ViewBlock* html = (ViewBlock*)set_view(lycon, RDT_VIEW_BLOCK, elmt);
    html->width = lycon->block.content_width;  
    html->height = lycon->ui_context->window_height;  // Constrain to viewport height for scrollbars
    lycon->doc->view_tree->root = (View*)html;  lycon->elmt = elmt;
    // default html styles
    html->scroller = alloc_scroll_prop(lycon);
    html->scroller->overflow_x = CSS_VALUE_AUTO;
    html->scroller->overflow_y = CSS_VALUE_AUTO;
    lycon->block.given_width = lycon->ui_context->window_width;
    // Set height to viewport height to enable scrollbars when content overflows
    lycon->block.given_height = lycon->ui_context->window_height;
    html->position = alloc_position_prop(lycon);

    // Create the initial Block Formatting Context for the root element
    // CSS 2.2: The root element establishes the initial BFC
    // We create it directly here since the HTML element is the root
    html->content_width = lycon->ui_context->window_width;
    Pool* layout_pool = lycon->doc->view_tree->pool;
    log_debug("[BFC] About to create root BFC, pool=%p, html=%p", (void*)layout_pool, (void*)html);
    BlockFormattingContext* root_bfc = (BlockFormattingContext*)pool_calloc(layout_pool, sizeof(BlockFormattingContext));
    log_debug("[BFC] pool_calloc returned: %p", (void*)root_bfc);
    if (root_bfc) {
        root_bfc->init(html, layout_pool);
        lycon->bfc = root_bfc;
        lycon->owns_bfc = true;
        log_debug("[BFC] Created root BFC for HTML element (width=%.1f)", html->content_width);
    } else {
        log_error("[BFC] Failed to allocate root BFC!");
    }

    // resolve CSS style
    log_debug("DEBUG: About to resolve style for elmt of name=%s", elmt->node_name());
    dom_node_resolve_style(elmt, lycon);
    log_debug("DEBUG: After resolve style");

    if (html->font) {
        setup_font(lycon->ui_context, &lycon->font, html->font);
    }
    if (lycon->root_font_size < 0) {
        lycon->root_font_size = lycon->font.current_font_size < 0 ?
            lycon->ui_context->default_font.font_size : lycon->font.current_font_size;
    }
    // Use OS/2 sTypo metrics only when USE_TYPO_METRICS flag is set (Chrome behavior)
    TypoMetrics typo = get_os2_typo_metrics(lycon->font.ft_face);
    if (typo.valid && typo.use_typo_metrics) {
        lycon->block.init_ascender = typo.ascender;
        lycon->block.init_descender = typo.descender;
    } else {
        lycon->block.init_ascender = lycon->font.ft_face->size->metrics.ascender / 64.0;
        lycon->block.init_descender = (-lycon->font.ft_face->size->metrics.descender) / 64.0;
    }

    // navigate DomNode tree to find body
    DomNode* body_node = nullptr;
    log_debug("Searching for body element in Lambda CSS document");
    DomNode* child = nullptr;
    if (elmt->is_element()) {
        child = static_cast<DomElement*>(elmt)->first_child;
    }
    while (child) {
        if (child->is_element()) {
            const char* tag_name = child->node_name();
            log_debug("  Checking child element: %s", tag_name);
            if (strcmp(tag_name, "body") == 0) {
                body_node = child;
                log_debug("Found Lambda CSS body element");
                break;
            }
        }
        child = child->next_sibling;
    }

    if (body_node) {
        log_debug("Laying out body element: %p", (void*)body_node);
        layout_block(lycon, body_node,
            (DisplayValue){.outer = CSS_VALUE_BLOCK, .inner = CSS_VALUE_FLOW});
    } else {
        log_debug("No body element found in DOM tree");
    }

    finalize_block_flow(lycon, html, CSS_VALUE_BLOCK);
}

int detect_html_version_lambda_css(DomDocument* doc) {
    if (!doc) { return HTML5; } // Default fallback
    // Return the HTML version that was detected during document loading
    log_debug("Using pre-detected HTML version: %d", doc->html_version);
    return doc->html_version;
}

// Reset styles_resolved flag for all elements before layout pass
// This ensures CSS style resolution happens exactly once per element per layout
static void reset_styles_resolved_recursive(DomNode* node) {
    if (!node) return;

    if (node->is_element()) {
        DomElement* elem = node->as_element();
        elem->styles_resolved = false;

        // Recursively process children
        DomNode* child = elem->first_child;
        while (child) {
            reset_styles_resolved_recursive(child);
            child = child->next_sibling;
        }
    }
}

// Public function to reset all styles_resolved flags in the document
void reset_styles_resolved(DomDocument* doc) {
    if (!doc || !doc->root) return;
    log_debug("[CSS] Resetting styles_resolved flags for all elements");
    reset_styles_resolved_recursive(doc->root);
}

void layout_init(LayoutContext* lycon, DomDocument* doc, UiContext* uicon) {
    memset(lycon, 0, sizeof(LayoutContext));
    lycon->doc = doc;  lycon->ui_context = uicon;

    // Initialize viewport dimensions for vw/vh units
    lycon->width = uicon->window_width;
    lycon->height = uicon->window_height;

    // Initialize available space to indefinite (will be set properly during layout)
    lycon->available_space = AvailableSpace::make_indefinite();

    // Clear measurement cache at the start of each layout pass
    // This ensures fresh intrinsic size calculations for each layout
    clear_measurement_cache();

    // Reset styles_resolved flags for all elements before layout
    // This ensures CSS style resolution happens exactly once per element per layout pass
    reset_styles_resolved(doc);

    // Initialize text flow logging
    init_text_flow_logging();

    // Process @font-face rules before layout begins
    // This is a simplified implementation - in a full system, this would be done during CSS parsing
    if (doc) {
        // Detect HTML version based on document type
        doc->view_tree->html_version = (HtmlVersion)detect_html_version_lambda_css(doc);
        clog_info(font_log, "Lambda CSS document - detected HTML version: %d", doc->view_tree->html_version);
    } else {
        doc->view_tree->html_version = HTML5;
    }
    log_debug("Detected HTML version: %d", doc->view_tree->html_version);

    // setup default font
    FontProp* default_font = doc->view_tree->html_version == HTML5 ? &uicon->default_font : &uicon->legacy_default_font;
    setup_font(uicon, &lycon->font, default_font);

    // Initialize float context to NULL - will be created when needed
    lycon->current_float_context = NULL;
    log_debug("DEBUG: Layout context initialized with NULL float context");
}

void layout_cleanup(LayoutContext* lycon) {
    // Clean up float context if it exists
    cleanup_float_context(lycon);
}

void layout_html_doc(UiContext* uicon, DomDocument *doc, bool is_reflow) {
    LayoutContext lycon;
    if (!doc) return;
    log_debug("layout html doc - start");
    if (is_reflow) {
        // free existing view tree
        log_debug("free existing views");
        // if (doc->view_tree->root) free_view(doc->view_tree, doc->view_tree->root);
        // view_pool_destroy(doc->view_tree);
    } else {
        doc->view_tree = (ViewTree*)calloc(1, sizeof(ViewTree));
        log_debug("allocated view tree");
    }
    view_pool_init(doc->view_tree);
    log_debug("initialized view pool");
    log_debug("calling layout_init...");
    layout_init(&lycon, doc, uicon);
    log_debug("layout_init complete");

    // Get root node based on document type
    DomNode* root_node = nullptr;
    root_node = doc->root;
    log_debug("DEBUG: Using root directly: %p", root_node);
    if (root_node) {
        // Validate pointer before calling virtual methods
        log_debug("DEBUG: root_node->node_type = %d", root_node->node_type);
        if (root_node->node_type >= DOM_NODE_ELEMENT && root_node->node_type <= DOM_NODE_DOCTYPE) {
            log_debug("layout lambda css html root %s", root_node->node_name());
        } else {
            log_error("Invalid node_type: %d (pointer may be corrupted)", root_node->node_type);
            return;
        }
    }

    if (!root_node) {
        log_error("Failed to get root_node");
        return;
    }

    log_debug("calling layout_html_root...");
    layout_html_root(&lycon, root_node);
    log_debug("layout_html_root complete");

    log_debug("end layout");
    log_debug("calling layout_cleanup...");
    layout_cleanup(&lycon);
    log_debug("layout_cleanup complete");

    // Print view tree (existing functionality)
    log_debug("checking view tree: %p, root: %p", (void*)doc->view_tree,
              doc->view_tree ? (void*)doc->view_tree->root : NULL);
    if (doc->view_tree && doc->view_tree->root) {
        log_debug("DOM tree: html version %d", doc->view_tree->html_version);
        log_debug("calling print_view_tree...");
        print_view_tree((ViewElement*)doc->view_tree->root, doc->url, uicon->pixel_ratio);
        log_debug("print_view_tree complete");
    } else {
        log_debug("Warning: No view tree generated");
    }

    log_debug("layout_html_doc complete");
}
