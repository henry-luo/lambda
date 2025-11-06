#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"
#include "layout_positioned.hpp"
#include "lambda_css_resolve.h"
#include "font_face.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H

extern "C" {
#include "../lambda/input/css/dom_element.h"
#include "../lambda/lambda-data.hpp"
}

#include "../lib/log.h"
void view_pool_init(ViewTree* tree);
void view_pool_destroy(ViewTree* tree);
// Function declaration moved to layout.hpp
char* read_text_file(const char *filename);
void finalize_block_flow(LayoutContext* lycon, ViewBlock* block, PropValue display);
void layout_inline(LayoutContext* lycon, DomNode *elmt, DisplayValue display);
void adjust_text_bounds(ViewText* text);

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

float calc_normal_line_height(FT_Face face) {
    FT_Short ascender  = face->size->metrics.ascender;
    FT_Short descender = -face->size->metrics.descender;
    FT_Short height    = face->size->metrics.height;
    float line_height;
    if (height != ascender + descender) {
        // true line gap exists
        line_height = height / 64.0f;
        log_debug("Using font height for line height: %f", line_height);
    } else {
        // Synthesized leading
        float line_gap = (ascender + descender) * 0.1f;   // adds 0.1em line gap when it is 0 from font
        line_height = (ascender + descender + line_gap) / 64.0f;
        log_debug("Using synthesized line gap: %f, asc %d, desc %d, line height: %f", line_gap, ascender, descender, line_height);
    }
    return line_height;
}

float calc_line_height(FontBox *fbox, lxb_css_property_line_height_t *line_height) {
    float height;
    if (line_height) {
        switch (line_height->type) {
        case LXB_CSS_VALUE__NUMBER:
            height = line_height->u.number.num * fbox->style->font_size;
            log_debug("property number: %lf", line_height->u.number.num);
            return height;
        case LXB_CSS_VALUE__LENGTH:
            height = line_height->u.length.num;  // px
            log_debug("property unit: %d", line_height->u.length.unit);
            return height;
        case LXB_CSS_VALUE__PERCENTAGE:
            height = line_height->u.percentage.num / 100.0 * fbox->style->font_size;
            log_debug("property percentage: %lf", line_height->u.percentage.num);
            return height;
        }
    }
    // default as 'normal'

    // FT_Pos asc  = fbox->ft_face->size->metrics.ascender;  // 26.6 fixed-point pixels
    // FT_Pos desc = fbox->ft_face->size->metrics.descender; // 26.6 fixed-point pixels
    // FT_Pos gap, lineHeight;

    // // Chrome seems to just use font height, not asc+desc, for line-height
    // // Get OS/2 table using proper FreeType API
    // // TT_OS2* os2_table = (TT_OS2*)FT_Get_Sfnt_Table(fbox->face.ft_face, FT_SFNT_OS2);
    // // if (os2_table && os2_table->sTypoLineGap != 0) {
    // //     // Scale the OS/2 line gap to current font size
    // //     gap = FT_MulFix(os2_table->sTypoLineGap, fbox->face.ft_face->size->metrics.y_scale);
    // //     log_debug("Using scaled OS/2 sTypoLineGap: %f", gap / 64.0f);
    // //     lineHeight = asc - desc + gap;
    // // } else {
    //     lineHeight = fbox->ft_face->size->metrics.height;
    // // }

    float lineHeight = calc_normal_line_height(fbox->ft_face);
    log_debug("got lineHeight: %f", lineHeight);
    return lineHeight;
}

float inherit_line_height(LayoutContext* lycon, ViewBlock* block) {
    // Inherit line height from parent
    INHERIT:
    ViewGroup* pa = block->parent;
    while (pa && !pa->is_block()) { pa = pa->parent; }
    if (pa) {
        ViewBlock* pa_block = (ViewBlock*)pa;
        if (pa_block->blk && pa_block->blk->line_height && pa_block->blk->line_height->type != LXB_CSS_VALUE_INHERIT) {
            return calc_line_height(&lycon->font, pa_block->blk->line_height);
        }
        block = pa_block;
        goto INHERIT;
    }
    // else initial value - 'normal'
    return calc_line_height(&lycon->font, NULL);
}

// DomNode style resolution function
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon) {
    log_debug("resolving style for elment '%s' of type %d", node->name(), node ? node->type : -1);
    log_enter();

    if (node && node->type == MARK_ELEMENT && node->dom_element) {
        // Lambda CSS path - use parallel resolution function
        DomElement* dom_elem = node->dom_element;
        resolve_lambda_css_styles(dom_elem, lycon);
        log_debug("resolved lambda css style for: %s", node->name());
    }
    else {
        log_debug("element has no style: %s", node->name());
    }

    log_leave();
}

float calculate_vertical_align_offset(LayoutContext* lycon, PropValue align, float item_height, float line_height, float baseline_pos, float item_baseline) {
    log_debug("calculate vertical align: align=%d, item_height=%f, line_height=%f, baseline_pos=%f, item_baseline=%f",
        align, item_height, line_height, baseline_pos, item_baseline);
    switch (align) {
    case LXB_CSS_VALUE_BASELINE:
        return baseline_pos - item_baseline;
    case LXB_CSS_VALUE_TOP:
        return 0;
    case LXB_CSS_VALUE_MIDDLE:
        return (line_height - item_height) / 2;
    case LXB_CSS_VALUE_BOTTOM:
        log_debug("bottom-aligned-text: line %d", line_height);
        return line_height - item_height;
    case LXB_CSS_VALUE_TEXT_TOP:
        // align with the top of the parent's font
        return baseline_pos - lycon->block.init_ascender;
    case LXB_CSS_VALUE_TEXT_BOTTOM:
        // align with the bottom of the parent's font
        return baseline_pos + lycon->block.init_descender - item_height;
    case LXB_CSS_VALUE_SUB:
        // Subscript position (approximately 0.3em lower)
        return baseline_pos - item_baseline + 0.3 * line_height;
    case LXB_CSS_VALUE_SUPER:
        // Superscript position (approximately 0.3em higher)
        return baseline_pos - item_baseline - 0.3 * line_height;
    default:
        return baseline_pos - item_baseline; // Default to baseline
    }
}

void span_vertical_align(LayoutContext* lycon, ViewSpan* span) {
    FontBox pa_font = lycon->font;  PropValue pa_line_align = lycon->line.vertical_align;
    log_debug("span_vertical_align");
    View* child = span->child;
    if (child) {
        if (span->font) {
            setup_font(lycon->ui_context, &lycon->font, span->font);
        }
        if (span->in_line && span->in_line->vertical_align) {
            lycon->line.vertical_align = span->in_line->vertical_align;
        }
        do {
            view_vertical_align(lycon, child);
            child = child->next;
        } while (child);
    }
    lycon->font = pa_font;  lycon->line.vertical_align = pa_line_align;
}

// apply vertical alignment to a view
void view_vertical_align(LayoutContext* lycon, View* view) {
    log_debug("view_vertical_align: view=%d", view->type);
    float line_height = max(lycon->block.line_height, lycon->line.max_ascender + lycon->line.max_descender);
    if (view->type == RDT_VIEW_TEXT) {
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
                rect->length, text_view->node->text_data() + rect->start_index);
            rect->y = lycon->block.advance_y + max(vertical_offset, 0);
            rect = rect->next;
        }
        adjust_text_bounds(text_view);
    }
    else if (view->type == RDT_VIEW_INLINE_BLOCK) {
        ViewBlock* block = (ViewBlock*)view;
        float item_height = block->height + (block->bound ?
            block->bound->margin.top + block->bound->margin.bottom : 0);
        float item_baseline = block->height + (block->bound ? block->bound->margin.top: 0);
        PropValue align = block->in_line && block->in_line->vertical_align ?
            block->in_line->vertical_align : lycon->line.vertical_align;
        float vertical_offset = calculate_vertical_align_offset(lycon, align, item_height,
            line_height, lycon->line.max_ascender, item_baseline);
        block->y = lycon->block.advance_y + max(vertical_offset, 0) + (block->bound ? block->bound->margin.top : 0);
        log_debug("vertical-adjusted-inline-block: y=%f, adv_y=%f, offset=%f, line=%f, blk=%f, max_asc=%f, max_desc=%f",
            block->y, lycon->block.advance_y, vertical_offset, lycon->block.line_height, item_height, lycon->line.max_ascender, lycon->line.max_descender);
    }
    else if (view->type == RDT_VIEW_INLINE) {
        // for inline elements, apply to all children
        ViewSpan* span = (ViewSpan*)view;
        span_vertical_align(lycon, span);
    }
    else {
        log_debug("view_vertical_align: unknown view type %d", view->type);
    }
}

void view_line_align(LayoutContext* lycon, float offset, View* view) {
    while (view) {
        log_debug("view line align: %d", view->type);
        view->x += offset;
        if (view->type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)view;
            text->x += offset;
            TextRect* rect = text->rect;
            while (rect) {
                rect->x += offset;
                rect = rect->next;
            }
        }
        else if (view->type == RDT_VIEW_INLINE) {
            ViewSpan* sp = (ViewSpan*)view;
            if (sp->child) view_line_align(lycon, offset, sp->child);
        }
        // else if (view->is_block()) {
        //     view->x += offset;
        // }
        // else {} // br
        view = view->next;
    }
}

void line_align(LayoutContext* lycon) {
    // align the views in the line
    log_debug("line align");
    if (lycon->block.text_align != LXB_CSS_VALUE_LEFT) {
        View* view = lycon->line.start_view;
        if (view) {
            float line_width = lycon->line.advance_x -lycon->line.left;
            float offset = 0;
            if (lycon->block.text_align == LXB_CSS_VALUE_CENTER) {
                offset = (lycon->block.content_width - line_width) / 2;
            }
            else if (lycon->block.text_align == LXB_CSS_VALUE_RIGHT) {
                offset = lycon->block.content_width - line_width;
            }
            if (offset <= 0) return;  // no need to adjust the views
            view_line_align(lycon, offset, view);
        }
    }
    log_debug("end of line align");
}

void layout_flow_node(LayoutContext* lycon, DomNode *node) {
    log_debug("layout node %s, advance_y: %f", node->name(), lycon->block.advance_y);

    // Skip HTML comments (Lambda CSS parser creates these as elements with name "!--")
    const char* node_name = node->name();
    if (node_name && (strcmp(node_name, "!--") == 0 || strcmp(node_name, "#comment") == 0)) {
        log_debug("skipping HTML comment node");
        return;
    }

    if (node->is_element()) {
        log_debug("Element: %s", node->name());
        log_debug("Processing element: %s", node->name());
        // Use resolve_display_value which handles both Lexbor and Lambda CSS nodes
        DisplayValue display = resolve_display_value(node);

        // Debug: print display values for all elements to diagnose grid issue
        log_debug("DEBUG: Element %s - outer=%d, inner=%d", node->name(), display.outer, display.inner);
        if (strcmp(node->name(), "table") == 0) {
            log_debug("TABLE ELEMENT in layout_flow_node - outer=%d, inner=%d (TABLE=%d)",
                   display.outer, display.inner, LXB_CSS_VALUE_TABLE);
        }
        if (strcmp(node->name(), "tbody") == 0) {
            printf("DEBUG: TBODY in layout_flow_node - outer=%d, inner=%d\n", display.outer, display.inner);
            printf("DEBUG: TBODY current position before layout_block: x=%.1f, y=%.1f\n",
                   ((View*)lycon->view)->x, ((View*)lycon->view)->y);
        }
        switch (display.outer) {
        case LXB_CSS_VALUE_BLOCK:  case LXB_CSS_VALUE_INLINE_BLOCK:  case LXB_CSS_VALUE_LIST_ITEM:
            layout_block(lycon, node, display);
            break;
        case LXB_CSS_VALUE_INLINE:
            layout_inline(lycon, node, display);
            break;
        case LXB_CSS_VALUE_NONE:
            log_debug("skipping element of display: none");
            break;
        default:
            log_debug("unknown display type");
            // skip the element
        }
    }
    else if (node->is_text()) {
        const unsigned char* str = node->text_data();
        log_debug("layout_text: '%t'", str);
        // skip whitespace at end of block
        if (!node->next_sibling() && lycon->parent->is_block() && is_only_whitespace((const char*)str)) {
            log_debug("skipping whitespace text at end of block");
        }
        else {
            layout_text(lycon, node);
        }
    }
    else {
        log_debug("layout unknown node type: %d", node->type);
        // skip the node
    }
    log_debug("end flow node, block advance_y: %d", lycon->block.advance_y);
}

/*
void load_style(LayoutContext* lycon, unsigned char* style_source) {
    lxb_css_parser_t *parser;  lxb_status_t status;  lxb_css_stylesheet_t *sst;
    parser = lxb_css_parser_create();
    status = lxb_css_parser_init(parser, NULL);
    if (status == LXB_STATUS_OK) {
        sst = lxb_css_stylesheet_parse(parser, (const lxb_char_t *)style_source, strlen((char*)style_source));
        if (sst != NULL) {
            status = lxb_html_document_stylesheet_attach(lycon->doc->dom_tree, sst);
            if (status != LXB_STATUS_OK) {
                log_debug("Failed to attach CSS StyleSheet to document");
            } else {
                log_debug("CSS StyleSheet attached to document");
            }
        }
        else {
            log_debug("Failed to parse CSS StyleSheet");
        }
    }
    lxb_css_parser_destroy(parser, true);
}
*/

/*
void apply_header_style(LayoutContext* lycon) {
    // apply header styles - only for Lexbor documents
    // Lambda CSS documents have CSS already applied during load_lambda_html_doc()
    if (lycon->doc->doc_type != DOC_TYPE_LEXBOR) {
        log_debug("Skipping apply_header_style for non-Lexbor document");
        return;
    }

    lxb_html_head_element_t *head = lxb_html_document_head_element(lycon->doc->dom_tree);
    if (head) {
        lxb_dom_node_t *child = lxb_dom_node_first_child(lxb_dom_interface_node(head));
        while (child) {
            if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                lxb_html_element_t *child_elmt = lxb_html_interface_element(child);
                if (child_elmt->element.node.local_name == LXB_TAG_STYLE) {
                    // style element already handled by the parser
                    // lxb_dom_node_t *text_node = lxb_dom_node_first_child(lxb_dom_interface_node(child_elmt));
                    // if (text_node && text_node->type == LXB_DOM_NODE_TYPE_TEXT) {
                    //     lxb_dom_text_t *text = lxb_dom_interface_text(text_node);
                    //     unsigned char* str = text->char_data.data.data;
                    //     printf("@@@ loading style: %s\n", str);
                    //     load_style(lycon, str);
                    // }
                }
                else if (child_elmt->element.node.local_name == LXB_TAG_LINK) {
                    // Lexbor does not support 'rel' attribute
                    // lxb_dom_attr_t* rel = lxb_dom_element_attr_by_id((lxb_dom_element_t *)child_elmt, LXB_DOM_ATTR_REL);
                    // load and parse linked stylesheet
                    lxb_dom_attr_t* href = lxb_dom_element_attr_by_id((lxb_dom_element_t *)child_elmt, LXB_DOM_ATTR_HREF);

                    if (href) {
                        Url* abs_url = parse_url(lycon->ui_context->document->url, (const char*)href->value->data);
                        if (!abs_url) {
                            log_debug("Failed to parse URL");
                            goto NEXT;
                        }
                        char* file_path = url_to_local_path(abs_url);
                        if (!file_path) {
                            log_debug("Failed to parse URL");
                            goto NEXT;
                        }
                        log_debug("Loading stylesheet: %s", file_path);

                        int len = strlen(file_path);
                        if (!(len > 4 && strcmp(file_path + len - 4, ".css") == 0)) {
                            log_debug("not stylesheet");  goto NEXT;
                        }

                        char* sty_source = read_text_file(file_path); // Use the constructed path
                        if (!sty_source) {
                            log_debug("Failed to read CSS file");
                        }
                        else {
                            load_style(lycon, (unsigned char*)sty_source);
                            free(sty_source);
                        }
                        free(file_path);
                    }
                }
            }
            NEXT:
            child = lxb_dom_node_next(child);
        }
    }
}
*/

void layout_html_root(LayoutContext* lycon, DomNode *elmt) {
    log_debug("layout html root");
    log_debug("DEBUG: elmt=%p, type=%d", (void*)elmt, elmt ? elmt->type : -1);
    //log_debug("DEBUG: About to call apply_header_style");
    //apply_header_style(lycon);
    log_debug("DEBUG: apply_header_style complete");

    // init context
    log_debug("DEBUG: Initializing layout context");
    lycon->elmt = elmt;
    lycon->root_font_size = lycon->font.current_font_size = -1;  // unresolved yet
    lycon->block.max_width = lycon->block.content_width = lycon->ui_context->window_width;
    // CRITICAL FIX: Let HTML element auto-size to content instead of forcing viewport height
    // This matches browser behavior where HTML element fits content, not viewport
    lycon->block.content_height = 0;  // Will be calculated based on content
    lycon->block.advance_y = 0;  lycon->block.line_height = -1;
    lycon->block.text_align = LXB_CSS_VALUE_LEFT;
    line_init(lycon, 0, lycon->block.content_width);
    Blockbox pa_block = lycon->block;  lycon->block.pa_block = &pa_block;

    ViewBlock* html = (ViewBlock*)alloc_view(lycon, RDT_VIEW_BLOCK, elmt);
    html->width = lycon->block.content_width;  html->height = lycon->block.content_height;
    lycon->doc->view_tree->root = (View*)html;  lycon->parent = (ViewGroup*)html;
    lycon->elmt = elmt;
    // default html styles
    html->scroller = alloc_scroll_prop(lycon);
    html->scroller->overflow_x = LXB_CSS_VALUE_AUTO;
    html->scroller->overflow_y = LXB_CSS_VALUE_AUTO;
    lycon->block.given_width = lycon->ui_context->window_width;
    lycon->block.given_height = -1;  // -1 means auto-size to content, instead of setting to viewport height
    html->position = alloc_position_prop(lycon);

    // resolve CSS style
    log_debug("DEBUG: About to resolve style for elmt, type=%d, name=%s", elmt->type, elmt->name());
    dom_node_resolve_style(elmt, lycon);
    log_debug("DEBUG: After resolve style");

    if (html->font) {
        setup_font(lycon->ui_context, &lycon->font, html->font);
    }
    if (lycon->root_font_size < 0) {
        lycon->root_font_size = lycon->font.current_font_size < 0 ?
            lycon->ui_context->default_font.font_size : lycon->font.current_font_size;
    }
    lycon->block.init_ascender = lycon->font.ft_face->size->metrics.ascender / 64.0;
    lycon->block.init_descender = (-lycon->font.ft_face->size->metrics.descender) / 64.0;

    // layout body content - handle both Lexbor and Lambda CSS documents
    DomNode* body_node = nullptr;

    if (lycon->doc->doc_type == DOC_TYPE_LAMBDA_CSS) {
        // Lambda CSS document - navigate DomNode tree to find body
        log_debug("Searching for body element in Lambda CSS document");
        DomNode* child = elmt->first_child();
        while (child) {
            if (child->is_element()) {
                const char* tag_name = child->name();
                log_debug("  Checking child element: %s", tag_name);
                if (strcmp(tag_name, "body") == 0) {
                    body_node = child;
                    log_debug("Found Lambda CSS body element");
                    break;
                }
            }
            child = child->next_sibling();
        }
    }

    if (body_node) {
        log_debug("Laying out body element: %p", (void*)body_node);
        layout_block(lycon, body_node,
            (DisplayValue){.outer = LXB_CSS_VALUE_BLOCK, .inner = LXB_CSS_VALUE_FLOW});
    } else {
        log_debug("No body element found in DOM tree");
    }

    finalize_block_flow(lycon, html, LXB_CSS_VALUE_BLOCK);
}

// Function to determine HTML version from DOCTYPE and compat mode
/*
HtmlVersion detect_html_version(lxb_html_document_t* html_doc) {
    lxb_dom_document_t* dom_doc = lxb_html_document_original_ref(html_doc);
    if (!dom_doc) {
        return HTML_QUIRKS;
    }

    // Check compatibility mode first - this is the most reliable indicator
    switch (dom_doc->compat_mode) {
        case LXB_DOM_DOCUMENT_CMODE_NO_QUIRKS:
            // Modern HTML5 or strict XHTML/HTML4.01
            break;  // continue below
        case LXB_DOM_DOCUMENT_CMODE_QUIRKS:
            // Legacy HTML or missing DOCTYPE - likely HTML4 or older
            log_debug("Document in quirks mode");
            return HTML_QUIRKS;
        case LXB_DOM_DOCUMENT_CMODE_LIMITED_QUIRKS:
            // Transitional HTML4.01 or some HTML5 edge cases
            log_debug("Document in limited quirks mode");
            return HTML4_01_TRANSITIONAL;
    }

    // For no-quirks mode, examine the DOCTYPE more carefully
    lxb_dom_document_type_t* doctype = dom_doc->doctype;
    if (!doctype) {
        log_debug("Document has no DOCTYPE");
        return HTML5;  // "HTML5 (No DOCTYPE)";
    }

    // Get DOCTYPE name
    size_t name_len;
    const lxb_char_t* name = lxb_dom_document_type_name(doctype, &name_len);

    // Get public ID
    size_t public_len;
    const lxb_char_t* public_id = lxb_dom_document_type_public_id(doctype, &public_len);

    // Get system ID
    size_t system_len;
    const lxb_char_t* system_id = lxb_dom_document_type_system_id(doctype, &system_len);
    log_debug("DOCTYPE name: '%.*s', public ID: '%.*s', system ID: '%.*s'",
        (int)name_len, name ? (const char*)name : "",
        (int)public_len, public_id ? (const char*)public_id : "",
        (int)system_len, system_id ? (const char*)system_id : "");

    // HTML5 DOCTYPE: "<!DOCTYPE html>" (no public/system ID)
    if (name_len == 4 && strncasecmp((char*)name, "html", 4) == 0 &&
        public_len == 0 && system_len == 0) {
        return HTML5;  // "HTML5";
    }

    // Check for HTML 4.01 DOCTYPE patterns
    if (public_len > 0 && public_id) {
        const char* pub_str = (const char*)public_id;

        // HTML 4.01 Strict
        if (strstr(pub_str, "-//W3C//DTD HTML 4.01//EN")) {
            return HTML4_01_STRICT;
        }

        // HTML 4.01 Transitional
        if (strstr(pub_str, "-//W3C//DTD HTML 4.01 Transitional//EN")) {
            return HTML4_01_TRANSITIONAL;
        }

        // HTML 4.01 Frameset
        if (strstr(pub_str, "-//W3C//DTD HTML 4.01 Frameset//EN")) {
            return HTML4_01_FRAMESET;
        }

        // XHTML 1.0 variants
        if (strstr(pub_str, "-//W3C//DTD XHTML 1.0")) {
            if (strstr(pub_str, "Strict")) return HTML4_01_STRICT; // "XHTML 1.0 Strict";
            if (strstr(pub_str, "Transitional")) return HTML4_01_TRANSITIONAL; // "XHTML 1.0 Transitional";
            if (strstr(pub_str, "Frameset")) return HTML4_01_FRAMESET; // "XHTML 1.0 Frameset";
            return HTML4_01_TRANSITIONAL;  // "XHTML 1.0";
        }

        // XHTML 1.1
        if (strstr(pub_str, "-//W3C//DTD XHTML 1.1//EN")) {
            return HTML4_01_TRANSITIONAL;  // "XHTML 1.1";
        }
    }
    // If we have a DOCTYPE but don't recognize it
    // if (name_len > 0) {
    //     return HTML5;  // "HTML (Unknown DOCTYPE)";
    // }
    return HTML5;  // "HTML5 (Standards Mode)";
}
*/

int detect_html_version_lambda_css(Document* doc) {
    if (!doc || doc->doc_type != DOC_TYPE_LAMBDA_CSS) {
        return HTML5;  // Default fallback
    }

    // Return the HTML version that was detected during document loading
    log_debug("Using pre-detected HTML version: %d", doc->html_version);
    return doc->html_version;
}

void layout_init(LayoutContext* lycon, Document* doc, UiContext* uicon) {
    memset(lycon, 0, sizeof(LayoutContext));
    lycon->doc = doc;  lycon->ui_context = uicon;

    // Initialize text flow logging
    init_text_flow_logging();

    // Process @font-face rules before layout begins
    // This is a simplified implementation - in a full system, this would be done during CSS parsing
    if (doc) {
        // Detect HTML version based on document type
        if (doc->doc_type == DOC_TYPE_LAMBDA_CSS) {
            // Detect HTML version from Lambda CSS document
            doc->view_tree->html_version = (HtmlVersion)detect_html_version_lambda_css(doc);
            clog_info(font_log, "Lambda CSS document - detected HTML version: %d", doc->view_tree->html_version);
        } else {
            doc->view_tree->html_version = HTML5;
        }
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

void layout_html_doc(UiContext* uicon, Document *doc, bool is_reflow) {
    LayoutContext lycon;
    if (!doc) return;
    log_debug("layout html doc - start");
    if (is_reflow) {
        // free existing view tree
        log_debug("free existing views");
        if (doc->view_tree->root) free_view(doc->view_tree, doc->view_tree->root);
        view_pool_destroy(doc->view_tree);
    } else {
        doc->view_tree = (ViewTree*)calloc(1, sizeof(ViewTree));
        log_debug("allocated view tree");
    }
    view_pool_init(doc->view_tree);
    log_debug("initialized view pool");
    log_debug("calling layout_init...");
    layout_init(&lycon, doc, uicon);
    log_debug("layout_init complete");

    // Create DomNode wrapper for root based on document type
    // CRITICAL: Heap-allocate and store in Document so it persists with the view tree
    DomNode* root_node = (DomNode*)calloc(1, sizeof(DomNode));
    if (!root_node) {
        log_error("Failed to allocate root_node");
        return;
    }
    doc->root_dom_node = root_node;  // Store in Document for later cleanup

    if (doc->doc_type == DOC_TYPE_LAMBDA_CSS) {
        // Lambda CSS document - DomNode wraps DomElement directly
        log_debug("DEBUG: Setting root_node.type to MARK_ELEMENT (%d)", MARK_ELEMENT);
        root_node->type = MARK_ELEMENT;
        root_node->dom_element = doc->lambda_dom_root;  // DomElement contains all data
        log_debug("DEBUG: root_node.type is now %d", root_node->type);
        log_debug("layout lambda css html root %s", root_node->name());
    } else {
        log_error("Unknown document type: %d", doc->doc_type);
        free(root_node);
        doc->root_dom_node = NULL;
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
        print_view_tree((ViewGroup*)doc->view_tree->root, doc->url, uicon->pixel_ratio, doc->doc_type);
        log_debug("print_view_tree complete");
    } else {
        log_debug("Warning: No view tree generated");
    }

    // Note: root_dom_node and its cached children are NOT freed here
    // They are stored in doc->root_dom_node and will be freed when the view tree is freed
    log_debug("DomNode cleanup skipped - will be freed with view tree");

    log_debug("layout_html_doc complete");
}
