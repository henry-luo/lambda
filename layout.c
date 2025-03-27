#include "layout.h"

void print_view_tree(ViewGroup* view_block);
void view_pool_init(ViewTree* tree);
void view_pool_destroy(ViewTree* tree);
void layout_text(LayoutContext* lycon, lxb_dom_text_t *text_node);
char* readTextFile(const char *filename);
void finalize_block_flow(LayoutContext* lycon, ViewBlock* block, PropValue display, Blockbox* pa_block);

bool is_space(char c) {
    return c == ' ' || c == '\t' || c== '\r' || c == '\n';
}

// Add this helper function for calculating vertical alignment offsets
int calculate_vertical_align_offset(PropValue align, int item_height, int line_height, int baseline_pos, int item_baseline) {
    switch (align) {
    case LXB_CSS_VALUE_BASELINE:
        return baseline_pos - item_baseline;
    case LXB_CSS_VALUE_TOP:
        return 0;
    case LXB_CSS_VALUE_MIDDLE:
        return (line_height - item_height) / 2;
    case LXB_CSS_VALUE_BOTTOM:
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

// Apply vertical alignment to a view
void apply_vertical_alignment(LayoutContext* lycon, View* view, int baseline_pos) {
    if (!view) return;
    
    int vertical_offset = 0;
    int item_baseline = 0;
    int item_height = 0;
    PropValue align = lycon->line.vertical_align;
    
    // Extract view-specific properties
    if (view->type == RDT_VIEW_TEXT) {
        ViewText* text_view = (ViewText*)view;
        item_height = text_view->height;
        // For text, baseline is at font.ascender
        item_baseline = (int)(lycon->font.face->size->metrics.ascender / 64);
        
    } else if (view->type == RDT_VIEW_INLINE) {
        ViewSpan* span = (ViewSpan*)view;
        // For inline elements, we need to determine the max baseline of its children
        // This is simplified - in reality, you'd compute this during inline layout
        item_baseline = lycon->font.face->size->metrics.height / 64 * 3/4; // Approximation
        
        // Check if span has its own vertical-align property
        if (span->in_line && span->in_line->vertical_align) {
            align = span->in_line->vertical_align;
        }
        
    } else if (view->type == RDT_VIEW_INLINE_BLOCK || view->type == RDT_VIEW_IMAGE) {
        ViewBlock* block = (ViewBlock*)view;
        item_height = block->height;
        // For replaced elements like images, baseline is at the bottom by default
        item_baseline = item_height;
    }
    
    // Calculate the offset based on vertical-align value
    vertical_offset = calculate_vertical_align_offset(align, item_height, 
                                                     lycon->block.line_height, 
                                                     baseline_pos, item_baseline);
    
    // Apply the offset
    if (view->type == RDT_VIEW_TEXT) {
        ViewText* text_view = (ViewText*)view;
        text_view->y += vertical_offset;
    } else if (view->type == RDT_VIEW_INLINE_BLOCK || view->type == RDT_VIEW_IMAGE) {
        ViewBlock* block = (ViewBlock*)view;
        block->y += vertical_offset;
    } else if (view->type == RDT_VIEW_INLINE) {
        // For inline elements, apply to all children
        ViewSpan* span = (ViewSpan*)view;
        View* child = span->child;
        while (child) {
            apply_vertical_alignment(lycon, child, baseline_pos);
            child = child->next;
        }
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

void line_break(LayoutContext* lycon) {
    printf("line break\n");
    if (lycon->line.is_line_start) return;  // no need to break at line start
    
    lycon->line.baseline_position = lycon->line.max_ascender;
    
    // Apply vertical alignment to all elements in the line
    View* view = lycon->line.start_view;
    while (view) {
        apply_vertical_alignment(lycon, view, lycon->line.baseline_position);
        view = view->next;
    }
    
    // Handle horizontal text alignment (existing code)
    line_align(lycon);
    
    // Move to next line
    lycon->block.advance_y += lycon->line.max_ascender + lycon->line.max_descender;
    lycon->line.left = lycon->line.advance_x = 0;
    lycon->line.max_ascender = lycon->line.max_descender = 0;
    lycon->line.is_line_start = true;  lycon->line.has_space = false;
    lycon->line.last_space = NULL;  lycon->line.last_space_pos = 0;
    lycon->line.start_view = NULL;
    lycon->line.baseline_position = 0;
    printf("end of line break\n");
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
        span->in_line->color = color_name_to_rgb(LXB_CSS_VALUE_BLUE);   
        span->font = alloc_font_prop(lycon);
        span->font->text_deco = LXB_CSS_VALUE_UNDERLINE;
        break;
    }
    // resolve CSS styles
    if (elmt->element.style) {
        // lxb_dom_document_t *doc = lxb_dom_element_document((lxb_dom_element_t*)elmt); // doc->css->styles
        lexbor_avl_foreach_recursion(NULL, elmt->element.style, resolve_element_style, lycon);
    }

    // Store current vertical alignment in linebox
    if (span->in_line && span->in_line->vertical_align) {
        lycon->line.vertical_align = span->in_line->vertical_align;
    } else {
        lycon->line.vertical_align = LXB_CSS_VALUE_BASELINE;  // Default
    }

    if (span->font) {
        setup_font(lycon->ui_context, &lycon->font, pa_font.face->family_name, span->font);
    }
    // layout inline content
    lxb_dom_node_t *child = lxb_dom_node_first_child(lxb_dom_interface_node(elmt));
    if (child) {
        lycon->parent = (ViewGroup*)span;  lycon->prev_view = NULL;
        do {
            layout_flow_node(lycon, child);
            child = lxb_dom_node_next(child);
        } while (child);
        lycon->parent = span->parent;
    }
    lycon->font = pa_font;  lycon->line.vertical_align = pa_line_align;
    lycon->prev_view = (View*)span;
    printf("inline view: %d, self %p, child %p\n", span->type, span, span->child);
}

void layout_flow_node(LayoutContext* lycon, lxb_dom_node_t *node) {
    printf("layout node %s\n", lxb_dom_element_local_name(lxb_dom_interface_element(node), NULL));
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        printf("Element: %s\n", lxb_dom_element_local_name(lxb_dom_interface_element(node), NULL));
        lxb_html_element_t *elmt = lxb_html_interface_element(node);
        DisplayValue display = resolve_display(elmt);
        switch (display.outer) {
        case LXB_CSS_VALUE_BLOCK:  case LXB_CSS_VALUE_INLINE_BLOCK:
        case LXB_CSS_VALUE_LIST_ITEM:
            layout_block(lycon, elmt, display);
            break;
        case LXB_CSS_VALUE_INLINE:
            layout_inline(lycon, elmt);
            break;
        case LXB_CSS_VALUE_NONE:
            printf("skipping elemnt of display: none\n");
            break;
        default:
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
                        lxb_url_t* abs_url = parse_url(lycon->ui_context->document->url, (const char*)href->value->data);
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

                        char* sty_source = readTextFile(file_path); // Use the constructed path
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

void layout_html_root(LayoutContext* lycon, lxb_html_element_t *elmt) {
    printf("layout html root\n");
    apply_header_style(lycon);

    // init context
    lycon->elmt = elmt;
    lycon->font.style = lycon->ui_context->default_font;
    lycon->root_font_size = lycon->font.current_font_size = -1;  // unresolved yet
    lycon->block.max_width = lycon->block.width = lycon->ui_context->window_width;  
    lycon->block.height = lycon->ui_context->window_height;
    lycon->block.advance_y = 0;
    lycon->block.line_height = round(1.2 * lycon->ui_context->default_font.font_size * lycon->ui_context->pixel_ratio);  
    lycon->block.text_align = LXB_CSS_VALUE_LEFT;
    lycon->line.is_line_start = true;
    Blockbox pa_block = lycon->block;  lycon->block.pa_block = &pa_block;

    ViewBlock* html = (ViewBlock*)alloc_view(lycon, RDT_VIEW_BLOCK, (lxb_dom_node_t*)elmt);
    html->width = lycon->block.width;  html->height = lycon->block.height;
    lycon->doc->view_tree->root = (View*)html;  lycon->parent = (ViewGroup*)html;
    lycon->elmt = elmt;
    // default html styles
    html->scroller = (ScrollProp*)alloc_prop(lycon, sizeof(ScrollProp));
    html->scroller->overflow_x = LXB_CSS_VALUE_AUTO;
    html->scroller->overflow_y = LXB_CSS_VALUE_AUTO;
    lycon->block.given_width = lycon->ui_context->window_width;
    lycon->block.given_height = lycon->ui_context->window_height;    
    // load CSS stylesheets
    if (elmt->element.style) {
        lexbor_avl_foreach_recursion(NULL, elmt->element.style, resolve_element_style, lycon);
    }

    if (html->font) {
        setup_font(lycon->ui_context, &lycon->font, lycon->font.face->family_name, html->font);
    }
    if (lycon->root_font_size < 0) {
        lycon->root_font_size = lycon->font.current_font_size < 0 ? 
            lycon->ui_context->default_font.font_size : lycon->font.current_font_size;
    }
    // layout body content
    lxb_dom_element_t *body = (lxb_dom_element_t*)lxb_html_document_body_element(lycon->doc->dom_tree);
    if (body) {
        layout_block(lycon, (lxb_html_element_t*)body, 
            (DisplayValue){.outer = LXB_CSS_VALUE_BLOCK, .inner = LXB_CSS_VALUE_FLOW});  
    } else {
        printf("No body element found\n");
    }

    finalize_block_flow(lycon, html, LXB_CSS_VALUE_BLOCK, &pa_block);
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

