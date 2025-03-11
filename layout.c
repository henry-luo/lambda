#include "layout.h"

void print_view_tree(ViewGroup* view_block);
void view_pool_init(ViewTree* tree);
void view_pool_destroy(ViewTree* tree);
void layout_list_item(LayoutContext* lycon, lxb_html_element_t *elmt);
void layout_text(LayoutContext* lycon, lxb_dom_text_t *text_node);

bool is_space(char c) {
    return c == ' ' || c == '\t' || c== '\r' || c == '\n';
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
            float line_width = lycon->line.advance_x;
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
                else if (view->type == RDT_VIEW_BLOCK) {
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

void layout_inline(LayoutContext* lycon, lxb_html_element_t *elmt) {
    printf("layout inline %s\n", lxb_dom_element_local_name(lxb_dom_interface_element(elmt), NULL));
    if (elmt->element.node.local_name == LXB_TAG_BR) { line_break(lycon); return; }

    // save parent context
    FontBox pa_font = lycon->font;  lycon->font.current_font_size = -1;  // unresolved yet
    PropValue pa_line_align = lycon->line.vertical_align;
    lycon->elmt = elmt;

    ViewSpan* span = (ViewSpan*)alloc_view(lycon, RDT_VIEW_INLINE, (lxb_dom_node_t*)elmt);
    switch (elmt->element.node.local_name) {
    case LXB_TAG_B:
        span->font = alloc_font_prop(lycon);
        span->font->font_weight = LXB_CSS_VALUE_BOLD;
        break;
    case LXB_TAG_I:
        span->font = alloc_font_prop(lycon);
        span->font->font_style = LXB_CSS_VALUE_ITALIC;
        break;
    case LXB_TAG_U:
        span->font = alloc_font_prop(lycon);    
        span->font->text_deco = LXB_CSS_VALUE_UNDERLINE;
        break;
    case LXB_TAG_S:
        span->font = alloc_font_prop(lycon);    
        span->font->text_deco = LXB_CSS_VALUE_LINE_THROUGH;
        break;
    case LXB_TAG_FONT:
        // parse font style
        lxb_dom_attr_t* color = lxb_dom_element_attr_by_id((lxb_dom_element_t *)elmt, LXB_DOM_ATTR_COLOR);
        if (color) { printf("font color: %s\n", color->value->data); }
        break;
    case LXB_TAG_A:
        // anchor style
        span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
        span->in_line->cursor = LXB_CSS_VALUE_POINTER;
        span->font = alloc_font_prop(lycon);
        span->font->text_deco = LXB_CSS_VALUE_UNDERLINE;
        break;
    }
    // resolve CSS styles
    if (elmt->element.style) {
        // lxb_dom_document_t *doc = lxb_dom_element_document((lxb_dom_element_t*)elmt); // doc->css->styles
        lexbor_avl_foreach_recursion(NULL, elmt->element.style, resolve_element_style, lycon);
    }

    if (span->font) {
        setup_font(lycon->ui_context, &lycon->font, pa_font.face->family_name, span->font);
    }
    // layout inline content
    lxb_dom_node_t *child = lxb_dom_node_first_child(lxb_dom_interface_node(elmt));
    if (child) {
        lycon->parent = (ViewGroup*)span;  lycon->prev_view = NULL;
        do {
            layout_node(lycon, child);
            child = lxb_dom_node_next(child);
        } while (child);
        lycon->parent = span->parent;
    }
    lycon->font = pa_font;  lycon->line.vertical_align = pa_line_align;
    lycon->prev_view = (View*)span;
    printf("inline view: %d, self %p, child %p\n", span->type, span, span->child);
}

void layout_node(LayoutContext* lycon, lxb_dom_node_t *node) {
    printf("layout node %s\n", lxb_dom_element_local_name(lxb_dom_interface_element(node), NULL));
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        printf("Element: %s\n", lxb_dom_element_local_name(lxb_dom_interface_element(node), NULL));
        lxb_html_element_t *elmt = lxb_html_interface_element(node);
        PropValue outer_display = resolve_element_display(elmt);
        if (outer_display == LXB_CSS_VALUE_BLOCK) {
            layout_block(lycon, elmt, outer_display);
        }
        else if (outer_display == LXB_CSS_VALUE_INLINE) {
            layout_inline(lycon, elmt);
        }
        else if (outer_display == LXB_CSS_VALUE_INLINE_BLOCK) {
            layout_block(lycon, elmt, outer_display);
        }
        else if (outer_display == LXB_CSS_VALUE_LIST_ITEM) {
            layout_list_item(lycon, elmt);
        }
        else {
            printf("unknown display type\n");
            // skip the element
        }
    }
    else if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        lxb_dom_text_t *text = lxb_dom_interface_text(node);
        const unsigned char* str = text->char_data.data.data;
        printf(" Text: %s\n", str);
        layout_text(lycon, text);
    }
    else {
        printf("layout unknown node type: %d\n", node->type);
        // skip the node
    }    
}

void layout_html_root(LayoutContext* lycon, lxb_html_element_t *elmt) {
    printf("layout html root\n");

    // init context
    lycon->font.current_font_size = -1;  // unresolved yet
    lycon->elmt = elmt;

    ViewBlock* html = (ViewBlock*)alloc_view(lycon, RDT_VIEW_BLOCK, (lxb_dom_node_t*)elmt);
    lycon->doc->view_tree->root = (View*)html;
    // resolve CSS styles
    if (elmt->element.style) {
        // lxb_dom_document_t *doc = lxb_dom_element_document((lxb_dom_element_t*)elmt); // doc->css->styles
        lexbor_avl_foreach_recursion(NULL, elmt->element.style, resolve_element_style, lycon);
    }

    if (html->font) {
        setup_font(lycon->ui_context, &lycon->font, lycon->font.face->family_name, html->font);
    }
    lycon->parent = (ViewGroup*)html;
    lycon->block.max_width = lycon->block.width = lycon->ui_context->window_width;  
    lycon->block.height = lycon->ui_context->window_height;
    lycon->block.advance_y = 0;
    lycon->block.line_height = round(1.2 * lycon->ui_context->default_font.font_size * lycon->ui_context->pixel_ratio);  
    lycon->block.text_align = LXB_CSS_VALUE_LEFT;
    lycon->line.is_line_start = true;

    lxb_dom_element_t *body = (lxb_dom_element_t*)lxb_html_document_body_element(lycon->doc->dom_tree);
    if (body) {
        printf("layout body\n");
        layout_block(lycon, (lxb_html_element_t*)body, LXB_CSS_VALUE_BLOCK);  
    } else {
        printf("No body element found\n");
    }
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
    if (is_reflow) {
        // free existing view tree
        if (doc->view_tree->root) free_view(doc->view_tree, doc->view_tree->root);
        view_pool_destroy(doc->view_tree);
    } else {
        doc->view_tree = calloc(1, sizeof(ViewTree));
    }
    view_pool_init(doc->view_tree);
    printf("start to layout DOM tree\n");
    layout_init(&lycon, doc, uicon);

    lxb_html_element_t *root = (lxb_html_element_t *)doc->dom_tree->dom_document.element;
    printf("layout html root %s\n", lxb_dom_element_local_name(lxb_dom_interface_element(root), NULL));
    layout_html_root(&lycon, root);

    printf("end layout\n");
    layout_cleanup(&lycon);

    if (doc->view_tree && doc->view_tree->root) 
        print_view_tree((ViewGroup*)doc->view_tree->root);
    
}

