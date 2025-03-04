#include "layout.h"

typedef enum LineFillStatus {
    RDT_NOT_SURE = 0,
    RDT_LINE_NOT_FILLED = 1,
    RDT_LINE_FILLED = 2,
} LineFillStatus;

bool is_space(char c) {
    return c == ' ' || c == '\t' || c== '\r' || c == '\n';
}

LineFillStatus span_has_line_filled(LayoutContext* lycon, lxb_dom_node_t* span);
void line_break(LayoutContext* lycon);
void layout_node(LayoutContext* lycon, lxb_dom_node_t *node);
void* alloc_font_prop(LayoutContext* lycon);
PropValue element_display(lxb_html_element_t* elmt);
lxb_status_t lxb_html_element_style_resolve(lexbor_avl_t *avl, lexbor_avl_node_t **root,
    lexbor_avl_node_t *node, void *ctx);

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

void setup_font(UiContext* uicon, FontBox *fbox, const char* font_name, FontProp *fprop) {
    fbox->style = *fprop;
    fbox->face = load_styled_font(uicon, font_name, fprop);
    if (FT_Load_Char(fbox->face, ' ', FT_LOAD_RENDER)) {
        fprintf(stderr, "could not load space character\n");
        fbox->space_width = fbox->face->size->metrics.height >> 6;
    } else {
        fbox->space_width = fbox->face->glyph->advance.x >> 6;
    }
}

void layout_block(LayoutContext* lycon, lxb_html_element_t *elmt) {
    printf("layout block %s\n", lxb_dom_element_local_name(lxb_dom_interface_element(elmt), NULL));
    if (!lycon->line.is_line_start) { line_break(lycon); }
    // save parent context
    Blockbox pa_block = lycon->block;  Linebox pa_line = lycon->line;   FontBox pa_font = lycon->font;

    ViewBlock* block = (ViewBlock*)alloc_view(lycon, RDT_VIEW_BLOCK, (lxb_dom_node_t*)elmt);
    // handle element default styles
    float em_size = 0;
    switch (elmt->element.node.local_name) {
    case LXB_TAG_CENTER:
        block->props = (BlockProp*)alloc_prop(lycon, sizeof(BlockProp));
        block->props->text_align = LXB_CSS_VALUE_CENTER;
        break;
    case LXB_TAG_H1:
        em_size = 2;  // 2em
        goto HEADING_PROP;
    case LXB_TAG_H2:
        em_size = 1.5;  // 1.5em
        goto HEADING_PROP;
    case LXB_TAG_H3:
        em_size = 1.17;  // 1.17em
        goto HEADING_PROP;
    case LXB_TAG_H4:    
        em_size = 1;  // 1em
        goto HEADING_PROP;
    case LXB_TAG_H5:
        em_size = 0.83;  // 0.83em 
        goto HEADING_PROP;
    case LXB_TAG_H6:
        em_size = 0.67;  // 0.67em
        HEADING_PROP:
        block->font = alloc_font_prop(lycon);
        block->font->font_size = lycon->font.style.font_size * em_size;
        block->font->font_weight = LXB_CSS_VALUE_BOLD;
        break;
    }
    lycon->block.line_height = lycon->font.style.font_size * 1.2;  // default line height

    // resolve CSS styles
    if (elmt->element.style) {
        // lxb_dom_document_t *doc = lxb_dom_element_document((lxb_dom_element_t*)elmt);
        lexbor_avl_foreach_recursion(NULL, elmt->element.style, lxb_html_element_style_resolve, lycon);
        printf("### got element style: %p\n", elmt->element.style);
    }
 
    lycon->block.advance_y = 0;  lycon->block.max_width = 0;
    if (block->props) lycon->block.text_align = block->props->text_align;
    lycon->line.advance_x = lycon->line.max_ascender = lycon->line.max_descender = 0;  
    lycon->line.is_line_start = true;  lycon->line.has_space = false;
    lycon->line.last_space = NULL;  lycon->line.start_view = NULL;
    block->y = pa_block.advance_y;
    block->width = pa_block.width;  block->height = pa_block.height;
    
    if (block->font) {
        setup_font(lycon->ui_context, &lycon->font, pa_font.face->family_name, block->font);
    }
    if (block->bound) {
        block->width -= block->bound->margin.left + block->bound->margin.right;
        block->height -= block->bound->margin.top + block->bound->margin.bottom;
        lycon->block.width = block->width - (block->bound->padding.left + block->bound->padding.right);
        lycon->block.height = block->height - (block->bound->padding.top + block->bound->padding.bottom);
        block->x += block->bound->margin.left;
        block->y += block->bound->margin.top;
        lycon->line.advance_x += block->bound->padding.left;
        lycon->block.advance_y += block->bound->padding.top;
    } 
    else {
        lycon->block.width = pa_block.width;  lycon->block.height = pa_block.height; 
    }
    lycon->line.right = lycon->block.width;  

    // layout block content
    lxb_dom_node_t *child = lxb_dom_node_first_child(lxb_dom_interface_node(elmt));
    if (child) {
        lycon->parent = (ViewGroup*)block;  lycon->prev_view = NULL;
        do {
            layout_node(lycon, child);
            child = lxb_dom_node_next(child);
        } while (child);
        // handle last line
        if (lycon->line.max_ascender) {
            lycon->block.advance_y += max(lycon->line.max_ascender + lycon->line.max_descender, lycon->block.line_height);
        }
        lycon->parent = block->parent;
        printf("block height: %f\n", lycon->block.advance_y);
    }
    line_align(lycon);

    if (block->bound) {
        block->width = max(block->width, lycon->block.max_width 
            + block->bound->padding.left + block->bound->padding.right);  
        block->height = lycon->block.advance_y + block->bound->padding.bottom;  
        pa_block.advance_y += block->height + block->bound->margin.top + block->bound->margin.bottom; 
        pa_block.max_width = max(pa_block.max_width, block->width 
            + block->bound->margin.left + block->bound->margin.right);              
    } 
    else {
        block->width = max(block->width, lycon->block.max_width);  
        block->height = lycon->block.advance_y;    
        pa_block.advance_y += block->height;
        pa_block.max_width = max(pa_block.max_width, block->width);        
    }
    lycon->block = pa_block;
    // reset linebox
    pa_line.advance_x = pa_line.max_ascender = pa_line.max_descender = 0;  
    pa_line.is_line_start = true;  pa_line.last_space = NULL;
    lycon->line = pa_line;  lycon->font = pa_font;
    lycon->prev_view = (View*)block;
    printf("block view: %d, self %p, child %p\n", block->type, block, block->child);
}

void layout_inline(LayoutContext* lycon, lxb_html_element_t *elmt) {
    printf("layout inline %s\n", lxb_dom_element_local_name(lxb_dom_interface_element(elmt), NULL));
    if (elmt->element.node.local_name == LXB_TAG_BR) { line_break(lycon); return; }

    // save parent context
    FontBox pa_font = lycon->font;  PropValue pa_line_align = lycon->line.vertical_align;
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
        lexbor_avl_foreach_recursion(NULL, elmt->element.style, lxb_html_element_style_resolve, lycon);
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

void line_break(LayoutContext* lycon) {
    lycon->block.advance_y += max(lycon->line.max_ascender + lycon->line.max_descender, lycon->block.line_height);
    // reset linebox
    lycon->line.advance_x = lycon->line.max_ascender = lycon->line.max_descender = 0;  
    lycon->line.is_line_start = true;  lycon->line.has_space = false;
    lycon->line.last_space = NULL;  lycon->line.start_view = NULL;
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
            LineFillStatus result = text_has_line_filled(lycon, (lxb_dom_text_t *)node);
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
    // and siblings through non-processed html nodes
    float current_advance_x = lycon->line.advance_x;
    node = lxb_dom_node_next(node);
    if (node) {
        LineFillStatus result = node_has_line_filled(lycon, node);
        if (result) { return result; }        
    }
    // check at parent level
    view = (View*)view->parent;
    if (view->type == RDT_VIEW_BLOCK) { return RDT_LINE_NOT_FILLED; }
    else if (view->type == RDT_VIEW_INLINE) {
        return view_has_line_filled(lycon, view, view->node);
    }
    printf("unknown view type\n");
    return RDT_NOT_SURE;
}

void layout_text(LayoutContext* lycon, lxb_dom_text_t *text_node) {
    unsigned char* text_start = text_node->char_data.data.data;  
    unsigned char* str = text_start;  printf("layout text: %s\n", str);
    if ((lycon->line.is_line_start || lycon->line.has_space) && is_space(*str)) { // skip space at start of line
        do { str++; } while (is_space(*str));
        if (!*str) return;
    }
    LAYOUT_TEXT:
    // assume style_text has at least one character
    ViewText* text = (ViewText*)alloc_view(lycon, RDT_VIEW_TEXT, (lxb_dom_node_t*)text_node);
    lycon->prev_view = (View*)text;    
    text->start_index = str - text_start;
    int font_height = lycon->font.face->size->metrics.height >> 6;
    text->x = lycon->line.advance_x;  text->height = font_height;
    if (lycon->line.vertical_align == LXB_CSS_VALUE_MIDDLE) {
        printf("middle aligned text\n");
        text->y = lycon->block.advance_y + (lycon->block.line_height - font_height) / 2;
    }
    else if (lycon->line.vertical_align == LXB_CSS_VALUE_BOTTOM) {
        printf("bottom aligned text\n");
        text->y = lycon->block.advance_y + lycon->block.line_height - font_height;
    }
    else if (lycon->line.vertical_align == LXB_CSS_VALUE_TOP) {
        printf("top aligned text\n");
        text->y = lycon->block.advance_y;
    }
    else { // baseline
        printf("baseline aligned text\n");
        text->y = lycon->block.advance_y; //  + (lycon->font.face->size->metrics.ascender >> 6);
    }
    // layout the text glyphs
    do {
        int wd;
        if (is_space(*str)) {
            wd = lycon->font.space_width;
        }
        else {
            if (FT_Load_Char(lycon->font.face, *str, FT_LOAD_RENDER)) {
                fprintf(stderr, "Could not load character '%c'\n", *str);
                return;
            }
            FT_GlyphSlot slot = lycon->font.face->glyph;  
            wd = slot->advance.x >> 6;
        }
        // printf("char: %c, width: %d\n", *str, wd);
        text->width += wd;
        if (text->x + text->width >= lycon->line.right) { // line filled up
            printf("line filled up\n");
            if (is_space(*str)) {
                printf("break on space\n");
                // skip all spaces
                do { str++; } while (is_space(*str));
                text->length = str - text_start - text->start_index;
                assert(text->length > 0);
                line_break(lycon);
                if (*str) { goto LAYOUT_TEXT; }
                else return;
            }
            else if (lycon->line.last_space) { // break at the last space
                printf("break at last space\n");
                if (text_start <= lycon->line.last_space && lycon->line.last_space < str) {
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
            lycon->line.last_space = str - 1;  lycon->line.has_space = true;
        }
        else { 
            str++;  lycon->line.is_line_start = false;  lycon->line.has_space = false;
        }
    } while (*str);
    // end of text
    if (lycon->line.last_space) { // need to check if line will fill up
        int advance_x = lycon->line.advance_x;  lycon->line.advance_x += text->width;
        if (view_has_line_filled(lycon, (View*)text, text->node) == RDT_LINE_FILLED) {
            if (text_start <= lycon->line.last_space && lycon->line.last_space < str) {
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
    lycon->line.max_ascender = max(lycon->line.max_ascender, lycon->font.face->size->metrics.ascender >> 6);
    lycon->line.max_descender = max(lycon->line.max_descender, (-lycon->font.face->size->metrics.descender) >> 6);
    printf("text view: x %f, y %f, width %f, height %f\n", text->x, text->y, text->width, text->height);
}

void layout_node(LayoutContext* lycon, lxb_dom_node_t *node) {
    printf("layout node %s\n", lxb_dom_element_local_name(lxb_dom_interface_element(node), NULL));
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        printf("Element: %s\n", lxb_dom_element_local_name(lxb_dom_interface_element(node), NULL));
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
        printf("layout unknown node type: %d\n", node->type);
        // skip the node
    }    
}

