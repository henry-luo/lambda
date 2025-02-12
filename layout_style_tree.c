#include "layout.h"
#include "./lib/string_buffer/string_buffer.h"

typedef enum LineFillStatus {
    RDT_NOT_SURE = 0,
    RDT_LINE_NOT_FILLED = 1,
    RDT_LINE_FILLED = 2,
} LineFillStatus;

FontProp default_font_prop = {LXB_CSS_VALUE_NORMAL, LXB_CSS_VALUE_NORMAL, LXB_CSS_VALUE_NONE};

bool is_space(char c) {
    return c == ' ' || c == '\t' || c== '\r' || c == '\n';
}

void line_break(LayoutContext* lycon);
void layout_node(LayoutContext* lycon, lxb_dom_node_t *node);
void print_view_tree(ViewGroup* view_block, StrBuf* buf, int indent);
LineFillStatus span_has_line_filled(LayoutContext* lycon, lxb_dom_node_t* span);

View* alloc_view(LayoutContext* lycon, ViewType type, lxb_dom_node_t *node) {
    View* view;
    switch (type) {
        case RDT_VIEW_BLOCK: view = calloc(1, sizeof(ViewBlock)); break;
        case RDT_VIEW_TEXT: view = calloc(1, sizeof(ViewText)); break;
        case RDT_VIEW_INLINE:  default:
            view = calloc(1, sizeof(ViewSpan)); break;
    }
    view->type = type;  view->node = node;  view->parent = lycon->parent;
    // link the view
    if (lycon->prev_view) { lycon->prev_view->next = view; }
    else { lycon->parent->child = view; }
    if (!lycon->line.start_view) lycon->line.start_view = view;
    return view;
}

PropValue element_display(lxb_html_element_t* elmt) {
    PropValue outer_display, inner_display;
    // determine element 'display'
    int name = elmt->element.node.local_name;  // todo: should check ns as well 
    switch (name) { 
        case LXB_TAG_H1: case LXB_TAG_H2: case LXB_TAG_H3: case LXB_TAG_H4: case LXB_TAG_H5: case LXB_TAG_H6:
        case LXB_TAG_P: case LXB_TAG_DIV: case LXB_TAG_CENTER: case LXB_TAG_UL: case LXB_TAG_OL:
            outer_display = LXB_CSS_VALUE_BLOCK;  inner_display = LXB_CSS_VALUE_FLOW;
            break;
        default:  // case LXB_TAG_B: case LXB_TAG_I: case LXB_TAG_U: case LXB_TAG_S: case LXB_TAG_FONT:
            outer_display = LXB_CSS_VALUE_INLINE;  inner_display = LXB_CSS_VALUE_FLOW;
    }
    // get CSS display if specified
    if (elmt->style != NULL) {
        const lxb_css_rule_declaration_t* display_decl = 
            lxb_html_element_style_by_id(elmt, LXB_CSS_PROPERTY_DISPLAY);
        if (display_decl) {
            // printf("display: %s, %s\n", lxb_css_value_by_id(display_decl->u.display->a)->name, 
            //     lxb_css_value_by_id(display_decl->u.display->b)->name);
            outer_display = display_decl->u.display->a;
            inner_display = display_decl->u.display->b;
        }
    }
    return outer_display;
}

void line_align(LayoutContext* lycon) {
    // align the views in the line
    printf("line align\n");
    if (lycon->block.text_align != LXB_CSS_VALUE_LEFT) {
        View* view = lycon->line.start_view;
        if (view) {
            int line_width = lycon->line.advance_x;
            int offset = 0;
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
                    // need to align the children
                }
                view = view->next;
            } while (view);            
        }
    }
    printf("end of line align\n");
}

void layout_block(LayoutContext* lycon, lxb_html_element_t *elmt) {
    printf("layout block %s\n", lxb_dom_element_local_name(elmt, NULL));
    if (lycon->line.is_line_start) { line_break(lycon); }
        
    ViewBlock* block = alloc_view(lycon, RDT_VIEW_BLOCK, elmt);
    block->text_align = (elmt->element.node.local_name == LXB_TAG_CENTER) ? LXB_CSS_VALUE_CENTER : LXB_CSS_VALUE_LEFT;

    Blockbox pa_block = lycon->block;  Linebox pa_line = lycon->line;
    lycon->block.width = pa_block.width;  lycon->block.height = pa_block.height;  
    lycon->block.advance_y = 0;  lycon->block.max_width = 0;
    lycon->block.text_align = block->text_align;
    lycon->line.advance_x = 0;  lycon->line.max_height = 0;  
    lycon->line.right = lycon->block.width;  
    lycon->line.is_line_start = true;  lycon->line.last_space = NULL;  
    lycon->line.start_view = NULL;
    block->y = pa_block.advance_y;
    
    // layout block content
    lxb_dom_node_t *child = lxb_dom_node_first_child(lxb_dom_interface_node(elmt));
    if (child) {
        lycon->parent = block;  lycon->prev_view = NULL;
        do {
            layout_node(lycon, child);
            child = lxb_dom_node_next(child);
        } while (child);
        // handle last line
        if (lycon->line.max_height) {
            lycon->block.advance_y += lycon->line.max_height;
        }
        lycon->parent = block->parent;
        printf("block height: %d\n", lycon->block.advance_y);
    }
    line_align(lycon);

    block->width = max(lycon->block.width, lycon->block.max_width);  
    block->height = lycon->block.advance_y;
    pa_block.advance_y += block->height;
    pa_block.max_width = max(pa_block.max_width, block->width);
    lycon->block = pa_block;
    // reset linebox
    pa_line.advance_x = 0;  pa_line.max_height = 0;  
    pa_line.is_line_start = true;  pa_line.last_space = NULL;
    lycon->line = pa_line;  lycon->prev_view = block;
    printf("block view: %d, self %p, child %p\n", block->type, block, block->child);
}

void layout_inline(LayoutContext* lycon, lxb_html_element_t *elmt) {
    printf("layout inline %s\n", lxb_dom_element_local_name(elmt, NULL));
    ViewSpan* span = alloc_view(lycon, RDT_VIEW_INLINE, elmt);
    span->font = default_font_prop;
    int name = elmt->element.node.local_name;
    if (name == LXB_TAG_B) {
        span->font.font_weight = LXB_CSS_VALUE_BOLD;
    }
    else if (name == LXB_TAG_I) {
        span->font.font_style = LXB_CSS_VALUE_ITALIC;
    }
    else if (name == LXB_TAG_U) {
        span->font.text_deco = LXB_CSS_VALUE_UNDERLINE;
    }
    else if (name == LXB_TAG_S) {
        span->font.text_deco = LXB_CSS_VALUE_LINE_THROUGH;
    }
    else if (name == LXB_TAG_FONT) {
        // parse font style
        // lxb_dom_attr_t* color = lxb_dom_element_attr_by_id(element, LXB_DOM_ATTR_COLOR);
        // if (color) { printf("font color: %s\n", color->value->data); }
    }

    FontBox pa_font = lycon->font;  lycon->font.style = span->font;
    lycon->font.face = load_styled_font(lycon->ui_context, lycon->font.face, &span->font);
    // layout inline content
    lxb_dom_node_t *child = lxb_dom_node_first_child(lxb_dom_interface_node(elmt));
    if (child) {
        lycon->parent = span;  lycon->prev_view = NULL;
        do {
            layout_node(lycon, child);
            child = lxb_dom_node_next(child);
        } while (child);
        lycon->parent = span->parent;
    }
    // FT_Done_Face(lycon->font.face);
    lycon->font = pa_font;  lycon->prev_view = span;
    printf("inline view: %d, self %p, child %p\n", span->type, span, span->child);
}

void line_break(LayoutContext* lycon) {
    lycon->block.advance_y += lycon->line.max_height;
    // reset linebox
    lycon->line.advance_x = 0;  lycon->line.max_height = 0;  
    lycon->line.is_line_start = true;  lycon->line.last_space = NULL;  
    lycon->line.start_view = NULL;
    line_align(lycon);
}

LineFillStatus text_has_line_filled(LayoutContext* lycon, lxb_dom_text_t *text_node) {
    int text_width = 0;  unsigned char *str = text_node->char_data.data.data;
    do {
        if (is_space(*str)) return RDT_LINE_NOT_FILLED;
        if (FT_Load_Char(lycon->font.face, *str, FT_LOAD_RENDER)) {
            fprintf(stderr, "Could not load character '%c'\n", *str);
            return RDT_LINE_NOT_FILLED;
        }
        FT_GlyphSlot slot = lycon->font.face->glyph;
        text_width += slot->advance.x >> 6;
        if (lycon->line.advance_x + text_width >= lycon->line.right) { // line filled up
            return RDT_LINE_FILLED;
        }
        str++;
    } while (*str);  // end of text
    lycon->line.advance_x += text_width;
    return RDT_NOT_SURE;
}

LineFillStatus node_has_line_filled(LayoutContext* lycon, lxb_dom_node_t* node) {
    do {
        if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
            LineFillStatus result = text_has_line_filled(lycon, node);
            if (result) { return result; }        
        }
        else if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_html_element_t *elmt = lxb_html_interface_element(node);
            PropValue outer_display = element_display(elmt);  
            if (outer_display == LXB_CSS_VALUE_BLOCK) { return RDT_LINE_NOT_FILLED; }
            else if (outer_display == LXB_CSS_VALUE_INLINE) {
                LineFillStatus result = span_has_line_filled(lycon, node);
                if (result) { return result; }
            }          
        }
        else {
            printf("unknown node type\n");
            // skip the node
        }
        node = lxb_dom_node_next(node);
    } while (node);
    return RDT_NOT_SURE;
}

LineFillStatus span_has_line_filled(LayoutContext* lycon, lxb_dom_node_t* span) {
    lxb_dom_node_t *node = lxb_dom_node_first_child(lxb_dom_interface_node(span));
    if (node) {
        LineFillStatus result = node_has_line_filled(lycon, node);
        if (result) { return result; }
    }
    return RDT_NOT_SURE;
}

LineFillStatus view_has_line_filled(LayoutContext* lycon, View* view, lxb_dom_node_t* node) {
    // note: this function navigates to parenets through laid out view tree, 
    // and siblings through non-processed style nodes
    node = lxb_dom_node_next(node);
    if (node) {
        LineFillStatus result = node_has_line_filled(lycon, node);
        if (result) { return result; }        
    }
    // check at parent level
    view = view->parent;
    if (view->type == RDT_VIEW_BLOCK) { return RDT_LINE_NOT_FILLED; }
    else if (view->type == RDT_VIEW_INLINE) {
        return view_has_line_filled(lycon, view, view->node);
    }
    printf("unknown view type\n");
    return RDT_NOT_SURE;
}

void layout_text(LayoutContext* lycon, lxb_dom_text_t *text_node) {
    unsigned char* text_start = text_node->char_data.data.data;  
    unsigned char* str = text_start;  printf("layout text %s\n", str);
    if (lycon->line.is_line_start && is_space(*str)) { // skip space at start of line
        do { str++; } while (is_space(*str));
        if (*str) { lycon->line.is_line_start = false; }
        else return;
    }
    LAYOUT_TEXT:
    // assume style_text has at least one character
    ViewText* text = alloc_view(lycon, RDT_VIEW_TEXT, text_node);
    lycon->prev_view = (View*)text;    
    text->start_index = str - text_start;
    text->x = lycon->line.advance_x;  text->y = lycon->block.advance_y;
    // layout the text
    do {
        if (FT_Load_Char(lycon->font.face, *str, FT_LOAD_RENDER)) {
            fprintf(stderr, "Could not load character '%c'\n", *str);
            return;
        }
        FT_GlyphSlot slot = lycon->font.face->glyph;  int wd = slot->advance.x >> 6;
        text->height = max(text->height, slot->metrics.height >> 6);
        printf("char: %c, width: %d\n", *str, wd);
        text->width += wd;
        if (text->x + text->width >= lycon->line.right) { // line filled up
            printf("line filled up\n");
            if (is_space(*str)) {
                printf("break on space\n");
                // skip all spaces
                do { str++; } while (is_space(*str));
                lycon->line.max_height = max(lycon->line.max_height, text->height);
                text->length = str - text_start - text->start_index;
                assert(text->length > 0);
                line_break(lycon);
                if (*str) { goto LAYOUT_TEXT; }
                else return;
            }
            else if (lycon->line.last_space) { // break at the last space
                printf("break at last space\n");
                if (text_start <= lycon->line.last_space && lycon->line.last_space < str) {
                    lycon->line.max_height = max(lycon->line.max_height, text->height);
                    str = lycon->line.last_space + 1;
                    text->length = str - text_start - text->start_index;
                    assert(text->length > 0);
                    line_break(lycon);  goto LAYOUT_TEXT;
                }
                else { // last_space outside the text, break at start of text
                    int advance_x = lycon->line.advance_x;
                    line_break(lycon);
                    text->y = lycon->block.advance_y;
                    text->x = lycon->line.advance_x;  lycon->line.advance_x = advance_x;
                    // continue the text flow
                }
            }
            // else cannot break, continue the flow with overflow
        }
        if (is_space(*str)) {
            do { str++; } while (is_space(*str));
            lycon->line.last_space = str - 1;
        }
        else { str++; }
    } while (*str);
    // end of text
    if (lycon->line.last_space) { // need to check if line will fill up
        int advance_x = lycon->line.advance_x;  lycon->line.advance_x += text->width;
        if (view_has_line_filled(lycon, (View*)text, text->node) == RDT_LINE_FILLED) {
            if (text_start <= lycon->line.last_space && lycon->line.last_space < str) {
                lycon->line.max_height = max(lycon->line.max_height, text->height);
                str = lycon->line.last_space + 1;
                text->length = str - text_start - text->start_index;
                assert(text->length > 0);
                line_break(lycon);  
                if (*str) goto LAYOUT_TEXT;
                else return;
            }
            else { // last_space outside the text, break at start of text
                line_break(lycon);
                text->x = lycon->line.advance_x;  text->y = lycon->block.advance_y;
                // output the entire text
            }
        }
        else {
            lycon->line.advance_x = advance_x;
            // output the entire text
        }
    }
    // else output the entire text
    text->length = str - text_start - text->start_index;  assert(text->length > 0);
    lycon->line.advance_x += text->width;
    lycon->line.max_height = max(lycon->line.max_height, text->height);
    printf("text view: x %d, y %d, width %d, height %d\n", text->x, text->y, text->width, text->height);
}

void layout_node(LayoutContext* lycon, lxb_dom_node_t *node) {
    printf("layout node %s\n", lxb_dom_element_local_name(node, NULL));
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        printf("Element: %s\n", lxb_dom_element_local_name(node, NULL));
        lxb_html_element_t *elmt = lxb_html_interface_element(node);
        PropValue outer_display = element_display(elmt);
        if (outer_display == LXB_CSS_VALUE_BLOCK) {
            layout_block(lycon, elmt);
        }
        else if (outer_display == LXB_CSS_VALUE_INLINE) {
            layout_inline(lycon, elmt);
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
        printf("layout unknown node\n");
    }    
}

void layout_init(LayoutContext* lycon, UiContext* uicon) {
    memset(lycon, 0, sizeof(LayoutContext));
    lycon->ui_context = uicon;
    // most browsers use a generic sans-serif font as the default
    // Google Chrome default fonts: Times New Roman (Serif), Arial (Sans-serif), and Courier New (Monospace)
    // default font size in HTML is 16 px for most browsers
    lycon->font.face = load_font_face(uicon, "Arial", 16);
    lycon->font.style.font_style = LXB_CSS_VALUE_NORMAL;
    lycon->font.style.font_weight = LXB_CSS_VALUE_NORMAL;
    lycon->font.style.text_deco = LXB_CSS_VALUE_NONE;
}

void layout_cleanup(LayoutContext* lycon) {
    FT_Done_Face(lycon->font.face);
}

View* layout_html_doc(UiContext* uicon, lxb_html_document_t *doc) {
    lxb_dom_element_t *body = lxb_html_document_body_element(doc);
    if (body) {
        // layout: computed style tree >> view tree
        printf("start to layout style tree\n");
        LayoutContext lycon;
        layout_init(&lycon, uicon);
        ViewBlock* root_view = calloc(1, sizeof(ViewBlock));
        root_view->type = RDT_VIEW_BLOCK;  root_view->node = body;
        lycon.parent = root_view;
        lycon.block.width = 400;  lycon.block.height = 600;
        lycon.block.advance_y = 0;  lycon.block.max_width = 800;
        layout_block(&lycon, body);
        printf("end layout\n");
        layout_cleanup(&lycon);
    
        StrBuf* buf = strbuf_new(4096);
        print_view_tree(root_view, buf, 0);
        printf("=================\nView tree:\n");
        printf("%s", buf->b);
        printf("=================\n");
        return (View*)root_view;
    }
    return NULL;
}

void print_view_tree(ViewGroup* view_block, StrBuf* buf, int indent) {
    View* view = view_block->child;
    if (view) {
        do {
            printf("view %s\n", lxb_dom_element_local_name(view->node, NULL));
            strbuf_append_charn(buf, ' ', indent);
            if (view->type == RDT_VIEW_BLOCK) {
                ViewBlock* block = (ViewBlock*)view;
                strbuf_sprintf(buf, "view block:%s, x:%d, y:%d, wd:%d, hg:%d\n",
                    lxb_dom_element_local_name(block->node, NULL),
                    block->x, block->y, block->width, block->height);                
                print_view_tree((ViewGroup*)view, buf, indent+2);
            }
            else if (view->type == RDT_VIEW_INLINE) {
                ViewSpan* span = (ViewSpan*)view;
                strbuf_sprintf(buf, "view inline:%s, font deco: %s, weight: %s, style: %s\n",
                    lxb_dom_element_local_name(span->node, NULL), 
                    lxb_css_value_by_id(span->font.text_deco)->name, 
                    lxb_css_value_by_id(span->font.font_weight)->name,
                    lxb_css_value_by_id(span->font.font_style)->name);
                print_view_tree((ViewGroup*)view, buf, indent+2);
            }
            else if (view->type == RDT_VIEW_TEXT) {
                ViewText* text = (ViewText*)view;
                lxb_dom_text_t *node = lxb_dom_interface_text(view->node);
                unsigned char* str = node->char_data.data.data + text->start_index;
                if (!(*str) || text->length <= 0) {
                    strbuf_sprintf(buf, "invalid text node: len:%d\n", text->length); 
                } else {
                    strbuf_append_str(buf, "text:'");  strbuf_append_strn(buf, (char*)str, text->length);
                    strbuf_sprintf(buf, "', start:%d, len:%d, x:%d, y:%d, wd:%d, hg:%d\n", 
                        text->start_index, text->length, text->x, text->y, text->width, text->height);                    
                }
            }
            else {
                strbuf_sprintf(buf, "unknown view: %d\n", view->type);
            }
            if (view == view->next) { printf("invalid next view\n");  return; }
            view = view->next;
        } while (view);
    }
    else {
        strbuf_append_charn(buf, ' ', indent);
        strbuf_append_str(buf, "view has no child\n");
    }
}