#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_table.hpp"

#include "../lib/log.h"
void view_pool_init(ViewTree* tree);
void view_pool_destroy(ViewTree* tree);
// Function declaration moved to layout.hpp
char* read_text_file(const char *filename);
void finalize_block_flow(LayoutContext* lycon, ViewBlock* block, PropValue display);

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



// DomNode style resolution function
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon) {
    log_debug("resolving style for node %p of type %d\n", node, node ? node->type : -1);
    if (node && node->type == LEXBOR_ELEMENT && node->lxb_elmt && node->lxb_elmt->element.style) {
        lexbor_avl_foreach_recursion(NULL, node->lxb_elmt->element.style, resolve_element_style, lycon);
        log_debug("resolved element style for node %p: %p\n", node, node->lxb_elmt->element.style);
    }
}

int calculate_vertical_align_offset(PropValue align, int item_height, int line_height, int baseline_pos, int item_baseline) {
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
        // Align with the top of the parent's font
        return 0;
    case LXB_CSS_VALUE_TEXT_BOTTOM:
        // Align with the bottom of the parent's font
        return line_height - item_height;
    case LXB_CSS_VALUE_SUB:
        // Subscript position (approximately 0.3em lower)
        return baseline_pos - item_baseline + (int)(0.3 * line_height);
    case LXB_CSS_VALUE_SUPER:
        // Superscript position (approximately 0.3em higher)
        return baseline_pos - item_baseline - (int)(0.3 * line_height);
    default:
        return baseline_pos - item_baseline; // Default to baseline
    }
}

void span_vertical_align(LayoutContext* lycon, ViewSpan* span) {
    FontBox pa_font = lycon->font;  PropValue pa_line_align = lycon->line.vertical_align;
    printf("render inline view\n");
    View* child = span->child;
    if (child) {
        if (span->font) {
            setup_font(lycon->ui_context, &lycon->font, pa_font.face->family_name, span->font);
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
    int line_height = max(lycon->block.line_height, lycon->line.max_ascender + lycon->line.max_descender);
    if (view->type == RDT_VIEW_TEXT) {
        ViewText* text_view = (ViewText*)view;
        int item_height = text_view->height;
        // for text, baseline is at font.ascender
        int item_baseline = (int)(lycon->font.face->size->metrics.ascender / 64);
        int vertical_offset = calculate_vertical_align_offset(lycon->line.vertical_align, item_height, 
            line_height, lycon->line.max_ascender, item_baseline);
        text_view->y = lycon->block.advance_y + max(vertical_offset, 0);
    } else if (view->type == RDT_VIEW_INLINE_BLOCK) {
        ViewBlock* block = (ViewBlock*)view;
        int item_height = block->height + (block->bound ? 
            block->bound->margin.top + block->bound->margin.bottom : 0);
        int item_baseline = block->height + (block->bound ? block->bound->margin.top: 0);
        PropValue align = block->in_line && block->in_line->vertical_align ? 
            block->in_line->vertical_align : lycon->line.vertical_align;
        int vertical_offset = calculate_vertical_align_offset(align, item_height, 
            line_height, lycon->line.max_ascender, item_baseline);
        block->y = lycon->block.advance_y + max(vertical_offset, 0);
        log_debug("vertical-adjusted-inline-block: y=%d, adv=%d, offset=%d, line=%d, blk=%d", 
            block->y, lycon->block.advance_y, vertical_offset, lycon->block.line_height, item_height);
    } else if (view->type == RDT_VIEW_INLINE) {
        // for inline elements, apply to all children
        ViewSpan* span = (ViewSpan*)view;
        span_vertical_align(lycon, span);
    }
}

void span_line_align(LayoutContext* lycon, float offset, ViewSpan* span) {
    // align the views in the line
    printf("span line align\n");
    View* view = span->child;
    while (view) {
        if (view->type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)view;
            text->x += offset;
        }
        else if (view->type == RDT_VIEW_BLOCK) {
            ViewBlock* block = (ViewBlock*)view;
            block->x += offset;
        }
        else if (view->type == RDT_VIEW_INLINE) {
            ViewSpan* sp = (ViewSpan*)view;
            span_line_align(lycon, offset, sp);
        }
        view = view->next;
    }
    printf("end of span line align\n");
}

void line_align(LayoutContext* lycon) {
    // align the views in the line
    printf("line align\n");
    if (lycon->block.text_align != LXB_CSS_VALUE_LEFT) {
        View* view = lycon->line.start_view;
        if (view) {
            float line_width = lycon->line.advance_x -lycon->line.left;
            float offset = 0;
            if (lycon->block.text_align == LXB_CSS_VALUE_CENTER) {
                offset = (lycon->block.width - line_width) / 2;
            }
            else if (lycon->block.text_align == LXB_CSS_VALUE_RIGHT) {
                offset = lycon->block.width - line_width;
            }
            if (offset <= 0) return;  // no need to adjust the views
            do {
                if (view->type == RDT_VIEW_TEXT) {
                    ViewText* text = (ViewText*)view;
                    text->x += offset;
                }
                else if (view->type == RDT_VIEW_BLOCK || view->type == RDT_VIEW_INLINE_BLOCK) {
                    ViewBlock* block = (ViewBlock*)view;
                    block->x += offset;
                }
                else if (view->type == RDT_VIEW_INLINE) {
                    ViewSpan* span = (ViewSpan*)view;
                    span_line_align(lycon, offset, span);
                }
                view = view->next;
            } while (view);            
        }
    }
    printf("end of line align\n");
}

void resolve_inline_default(LayoutContext* lycon, ViewSpan* span) {
    uintptr_t elmt_name = span->node->tag();
    switch (elmt_name) {
    case LXB_TAG_B:
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_weight = LXB_CSS_VALUE_BOLD;
        break;
    case LXB_TAG_I:
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_style = LXB_CSS_VALUE_ITALIC;
        break;
    case LXB_TAG_U:
        if (!span->font) { span->font = alloc_font_prop(lycon); }    
        span->font->text_deco = LXB_CSS_VALUE_UNDERLINE;
        break;
    case LXB_TAG_S:
        if (!span->font) { span->font = alloc_font_prop(lycon); }    
        span->font->text_deco = LXB_CSS_VALUE_LINE_THROUGH;
        break;
    case LXB_TAG_FONT: {
        // parse font style
        // Get color attribute using DomNode interface
        const lxb_char_t* color_attr = span->node->get_attribute("color");
        if (color_attr) {
            printf("font color: %s\n", color_attr);
        }
        break;
    }
    case LXB_TAG_A: {
        // anchor style
        if (!span->in_line) { span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp)); }
        span->in_line->cursor = LXB_CSS_VALUE_POINTER;
        span->in_line->color = color_name_to_rgb(LXB_CSS_VALUE_BLUE);   
        span->font = alloc_font_prop(lycon);
        span->font->text_deco = LXB_CSS_VALUE_UNDERLINE;
        break;
    }
    }
}

void layout_inline(LayoutContext* lycon, DomNode *elmt, DisplayValue display) {
    printf("layout inline %s\n", elmt->name());
    if (elmt->tag() == LXB_TAG_BR) { line_break(lycon); return; }

    // save parent context
    FontBox pa_font = lycon->font;  lycon->font.current_font_size = -1;  // unresolved yet
    PropValue pa_line_align = lycon->line.vertical_align;
    lycon->elmt = elmt;

    ViewSpan* span = (ViewSpan*)alloc_view(lycon, RDT_VIEW_INLINE, elmt);
    span->display = display;
    // resolve element default styles
    resolve_inline_default(lycon, span);
    // resolve CSS styles
    dom_node_resolve_style(elmt, lycon);
    
    if (span->font) {
        setup_font(lycon->ui_context, &lycon->font, pa_font.face->family_name, span->font);
    }
    if (span->in_line && span->in_line->vertical_align) {
        lycon->line.vertical_align = span->in_line->vertical_align;
    }
    // line.max_ascender and max_descender to be changed only when there's output from the span   

    // layout inline content
    DomNode *child = elmt->first_child();
    if (child) {
        lycon->parent = (ViewGroup*)span;  lycon->prev_view = NULL;
        do {
            layout_flow_node(lycon, child);
            child = child->next_sibling();
        } while (child);
        lycon->parent = span->parent;
    }
    lycon->font = pa_font;  lycon->line.vertical_align = pa_line_align;
    lycon->prev_view = (View*)span;
    printf("inline view: %d, self %p, child %p\n", span->type, span, span->child);
}

void layout_flow_node(LayoutContext* lycon, DomNode *node) {
    if (!node) {
        printf("Error: null node passed to layout_flow_node\n");
        return;
    }
    printf("layout node %s\n", node->name());
    if (node->is_element()) {
        printf("Element: %s\n", node->name());
        lxb_html_element_t *elmt = node->as_element();
        DisplayValue display = resolve_display(elmt);
        
        // Debug: print display values for table elements
        if (node->tag() == LXB_TAG_TABLE) {
            printf("DEBUG: TABLE element - outer=%d, inner=%d (LXB_CSS_VALUE_TABLE=%d)\n", 
                   display.outer, display.inner, LXB_CSS_VALUE_TABLE);
        }
        
        // Check for flex container first
        if (display.inner == LXB_CSS_VALUE_FLEX) {
            // This element is a flex container - route to flex layout
            layout_block(lycon, node, display); // Block layout will handle flex containers
            return;
        }
        // Check for table formatting context root (Phase 1)
        if (display.outer == LXB_CSS_VALUE_TABLE || display.inner == LXB_CSS_VALUE_TABLE) {
            printf("DEBUG: Table detected! outer=%d, inner=%d\n", display.outer, display.inner);
            layout_table_box(lycon, node, display);
            return;
        }
        
        switch (display.outer) {
        case LXB_CSS_VALUE_BLOCK:  case LXB_CSS_VALUE_INLINE_BLOCK:
        case LXB_CSS_VALUE_LIST_ITEM:
            layout_block(lycon, node, display);
            break;
        case LXB_CSS_VALUE_INLINE:
            layout_inline(lycon, node, display);
            break;
        case LXB_CSS_VALUE_NONE:
            printf("skipping elemnt of display: none\n");
            break;
        default:
            printf("unknown display type\n");
            // skip the element
        }
    }
    else if (node->is_text()) {
        const unsigned char* str = node->text_data();
        printf(" Text: %s\n", str);
        if (str && *str && !is_only_whitespace((const char*)str)) {
            layout_text(lycon, node);
        }
    }
    else {
        printf("layout unknown node type: %d\n", node->type);
        // skip the node
    }    
}

void load_style(LayoutContext* lycon, unsigned char* style_source) {
    lxb_css_parser_t *parser;  lxb_status_t status;  lxb_css_stylesheet_t *sst;
    parser = lxb_css_parser_create();
    status = lxb_css_parser_init(parser, NULL);
    if (status == LXB_STATUS_OK) {
        sst = lxb_css_stylesheet_parse(parser, (const lxb_char_t *)style_source, strlen((char*)style_source));
        if (sst != NULL) {
            status = lxb_html_document_stylesheet_attach(lycon->doc->dom_tree, sst);
            if (status != LXB_STATUS_OK) {
                printf("Failed to attach CSS StyleSheet to document");
            } else {
                printf("CSS StyleSheet attached to document\n");
            }
        }
        else {
            printf("Failed to parse CSS StyleSheet\n");
        }
    }
    lxb_css_parser_destroy(parser, true);    
}

void apply_header_style(LayoutContext* lycon) {
    // apply header styles
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
                        lxb_url_t* abs_url = parse_lexbor_url(lycon->ui_context->document->url, (const char*)href->value->data);
                        if (!abs_url) {
                            printf("Failed to parse URL\n");
                            goto NEXT;
                        }
                        char* file_path = url_to_local_path(abs_url);
                        if (!file_path) {
                            printf("Failed to parse URL\n");
                            goto NEXT;
                        }
                        printf("Loading stylesheet: %s\n", file_path);

                        int len = strlen(file_path);
                        if (!(len > 4 && strcmp(file_path + len - 4, ".css") == 0)) {
                            printf("not stylesheet\n");  goto NEXT;
                        }

                        char* sty_source = read_text_file(file_path); // Use the constructed path
                        if (!sty_source) {
                            fprintf(stderr, "Failed to read CSS file\n");
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

void layout_html_root(LayoutContext* lycon, DomNode *elmt) {
    printf("layout html root\n");
    apply_header_style(lycon);

    // init context
    lycon->elmt = elmt;
    lycon->font.style = lycon->ui_context->default_font;
    lycon->root_font_size = lycon->font.current_font_size = -1;  // unresolved yet
    lycon->block.max_width = lycon->block.width = lycon->ui_context->window_width;  
    // CRITICAL FIX: Let HTML element auto-size to content instead of forcing viewport height
    // This matches browser behavior where HTML element fits content, not viewport
    lycon->block.height = 0;  // Will be calculated based on content
    lycon->block.advance_y = 0;
    lycon->block.line_height = round(1.2 * lycon->ui_context->default_font.font_size * lycon->ui_context->pixel_ratio);  
    lycon->block.text_align = LXB_CSS_VALUE_LEFT;
    lycon->line.left = 0;  lycon->line.right = lycon->block.width;
    lycon->line.vertical_align = LXB_CSS_VALUE_BASELINE;
    line_init(lycon);
    Blockbox pa_block = lycon->block;  lycon->block.pa_block = &pa_block;

    ViewBlock* html = (ViewBlock*)alloc_view(lycon, RDT_VIEW_BLOCK, elmt);
    html->width = lycon->block.width;  html->height = lycon->block.height;
    lycon->doc->view_tree->root = (View*)html;  lycon->parent = (ViewGroup*)html;
    lycon->elmt = elmt;
    // default html styles
    html->scroller = (ScrollProp*)alloc_prop(lycon, sizeof(ScrollProp));
    html->scroller->overflow_x = LXB_CSS_VALUE_AUTO;
    html->scroller->overflow_y = LXB_CSS_VALUE_AUTO;
    lycon->block.given_width = lycon->ui_context->window_width;
    // CRITICAL FIX: Don't force HTML height to viewport - let it auto-size to content
    lycon->block.given_height = -1;  // -1 means auto-size to content    
    // load CSS stylesheets
    dom_node_resolve_style(elmt, lycon);

    if (html->font) {
        setup_font(lycon->ui_context, &lycon->font, lycon->font.face->family_name, html->font);
    }
    if (lycon->root_font_size < 0) {
        lycon->root_font_size = lycon->font.current_font_size < 0 ? 
            lycon->ui_context->default_font.font_size : lycon->font.current_font_size;
    }
    lycon->block.init_ascender = lycon->font.face->size->metrics.ascender >> 6;
    lycon->block.init_descender = (-lycon->font.face->size->metrics.descender) >> 6;

    // layout body content
    lxb_dom_element_t *lexbor_body = (lxb_dom_element_t*)lxb_html_document_body_element(lycon->doc->dom_tree);
    if (lexbor_body) {
        // CRITICAL FIX: Allocate DomNode dynamically to avoid dangling pointer
        DomNode* body_node = (DomNode*)malloc(sizeof(DomNode));
        memset(body_node, 0, sizeof(DomNode));
        body_node->type = LEXBOR_ELEMENT;
        body_node->lxb_node = (lxb_dom_node_t*)lexbor_body;
        
        printf("DEBUG: Created proper body DOM node: %p\n", (void*)body_node);
        layout_block(lycon, body_node, 
            (DisplayValue){.outer = LXB_CSS_VALUE_BLOCK, .inner = LXB_CSS_VALUE_FLOW});  
    } else {
        printf("ERROR: No body element found in DOM tree\n");
    }

    finalize_block_flow(lycon, html, LXB_CSS_VALUE_BLOCK);
}

void layout_init(LayoutContext* lycon, Document* doc, UiContext* uicon) {
    memset(lycon, 0, sizeof(LayoutContext));
    lycon->doc = doc;  lycon->ui_context = uicon;
    // most browsers use a generic sans-serif font as the default
    // Google Chrome default fonts: Times New Roman (Serif), Arial (Sans-serif), and Courier New (Monospace)
    // default font size in HTML is 16 px for most browsers
    setup_font(uicon, &lycon->font, uicon->default_font.family, &lycon->ui_context->default_font);
}

void layout_cleanup(LayoutContext* lycon) {
}

void layout_html_doc(UiContext* uicon, Document *doc, bool is_reflow) {
    LayoutContext lycon;
    if (!doc) return;
    printf("layout html doc\n");
    if (is_reflow) {
        // free existing view tree
        printf("free existing views\n");
        if (doc->view_tree->root) free_view(doc->view_tree, doc->view_tree->root);
        view_pool_destroy(doc->view_tree);
    } else {
        doc->view_tree = (ViewTree*)calloc(1, sizeof(ViewTree));
    }
    view_pool_init(doc->view_tree);
    printf("start to layout DOM tree\n");
    layout_init(&lycon, doc, uicon);

    lxb_html_element_t *lexbor_root = (lxb_html_element_t *)doc->dom_tree->dom_document.element;
    // Create DomNode wrapper for root
    DomNode root_node;
    memset(&root_node, 0, sizeof(DomNode));
    root_node.type = LEXBOR_ELEMENT;
    root_node.lxb_node = (lxb_dom_node_t*)lexbor_root;
    printf("layout html root %s\n", root_node.name());
    layout_html_root(&lycon, &root_node);

    printf("end layout\n");
    layout_cleanup(&lycon);

    if (doc->view_tree && doc->view_tree->root) 
        print_view_tree((ViewGroup*)doc->view_tree->root, uicon->pixel_ratio);
}
