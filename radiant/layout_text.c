#include "layout.h"

LineFillStatus span_has_line_filled(LayoutContext* lycon, lxb_dom_node_t* span);

void line_init(LayoutContext* lycon) {
    lycon->line.max_ascender = lycon->line.max_descender = 0;
    lycon->line.advance_x = lycon->line.left;
    lycon->line.is_line_start = true;  lycon->line.has_space = false;
    lycon->line.last_space = NULL;  lycon->line.last_space_pos = 0;
    lycon->line.start_view = NULL;
    lycon->line.line_start_font = lycon->font;
}

void line_break(LayoutContext* lycon) {
    lycon->block.max_width = max(lycon->block.max_width, lycon->line.advance_x);
    
    if (lycon->line.max_ascender > lycon->block.init_ascender || 
        lycon->line.max_descender > lycon->block.init_descender) {
        // apply vertical alignment
        View* view = lycon->line.start_view;
        if (view) {
            FontBox pa_font = lycon->font;
            lycon->font = lycon->line.line_start_font;
            do {
                view_vertical_align(lycon, view);
                view = view->next;
            } while (view);
            // todo: handle more views after the start span
            lycon->font = pa_font;
        }
    }
    // else no change to vertical alignment

    // horizontal text alignment
    line_align(lycon);

    // advance to next line
    lycon->block.advance_y += max(lycon->line.max_ascender + lycon->line.max_descender, lycon->block.line_height);
    // reset the new line
    line_init(lycon);
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
        if (lycon->line.advance_x + text_width > lycon->line.right) { // line filled up
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
            PropValue outer_display = resolve_display(elmt).outer;  
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
    printf("check if view has line filled\n");
    node = lxb_dom_node_next(node);
    if (node) {
        LineFillStatus result = node_has_line_filled(lycon, node);
        if (result) { return result; }        
    }
    // check at parent level
    view = (View*)view->parent;
    if (view) {
        if (view->type == RDT_VIEW_BLOCK) { return RDT_LINE_NOT_FILLED; }
        else if (view->type == RDT_VIEW_INLINE) {
            return view_has_line_filled(lycon, view, view->node);
        }
        printf("unknown view type\n");
    }
    return RDT_NOT_SURE;
}

void output_text(LayoutContext* lycon, ViewText* text, int text_length, int text_width) {
    text->length = text_length;  assert(text->length > 0);
    text->width = text_width;
    lycon->line.advance_x += text_width;
    lycon->line.max_ascender = max(lycon->line.max_ascender, lycon->font.face->size->metrics.ascender >> 6);
    lycon->line.max_descender = max(lycon->line.max_descender, (-lycon->font.face->size->metrics.descender) >> 6);
    printf("text view: x %d, y %d, width %d, height %d\n", text->x, text->y, text->width, text->height);
}

void layout_text(LayoutContext* lycon, lxb_dom_text_t *text_node) {
    unsigned char* next_ch;
    unsigned char* text_start = text_node->char_data.data.data;  
    unsigned char* str = text_start;  
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
        dzlog_debug("middle-aligned-text: font %d, line %d", font_height, lycon->block.line_height);
        text->y = lycon->block.advance_y + (lycon->block.line_height - font_height) / 2;
    }
    else if (lycon->line.vertical_align == LXB_CSS_VALUE_BOTTOM) {
        dzlog_debug("bottom-aligned-text: font %d, line %d", font_height, lycon->block.line_height);
        text->y = lycon->block.advance_y + lycon->block.line_height - font_height;
    }
    else if (lycon->line.vertical_align == LXB_CSS_VALUE_TOP) {
        dzlog_debug("top-aligned-text");
        text->y = lycon->block.advance_y;
    }
    else { // baseline
        // text->y = lycon->block.advance_y + (lycon->block.line_height - (lycon->line.max_ascender + lycon->line.max_descender))/2 
        //     + lycon->line.max_ascender - (lycon->font.face->size->metrics.ascender >> 6); 
        text->y = lycon->block.advance_y;
    }
    // layout the text glyphs
    do {
        int wd;
        if (is_space(*str)) {
            wd = lycon->font.space_width;
        }
        else {
            uint32_t codepoint = *str;
            if (codepoint >= 128) { // unicode char
                int bytes = utf8_to_codepoint(str, &codepoint);
                if (bytes <= 0) { // invalid utf8 char
                    next_ch = str + 1;  codepoint = 0;
                }
                else { next_ch = str + bytes; }
            }
            else { next_ch = str + 1; } 
            FT_GlyphSlot glyph = load_glyph(lycon->ui_context, lycon->font.face, &lycon->font.style, codepoint);
            wd = glyph ? (glyph->advance.x >> 6) : lycon->font.space_width;
        }
        // printf("char: %c, width: %d\n", *str, wd);
        text->width += wd;
        if (text->x + text->width > lycon->line.right) { // line filled up
            printf("line filled up\n");
            if (is_space(*str)) {
                printf("break on space\n");
                // skip all spaces
                do { str++; } while (is_space(*str));
                output_text(lycon, text, str - text_start - text->start_index, text->width);
                line_break(lycon);
                printf("after space line break\n");
                if (*str) { goto LAYOUT_TEXT; }
                else return;
            }
            else if (lycon->line.last_space) { // break at the last space
                printf("break at last space\n");
                if (text_start <= lycon->line.last_space && lycon->line.last_space < str) {
                    str = lycon->line.last_space + 1;
                    output_text(lycon, text, str - text_start - text->start_index, lycon->line.last_space_pos);
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
            // else cannot break, continue the flow in current line
        }
        if (is_space(*str)) {
            do { str++; } while (is_space(*str));
            lycon->line.last_space = str - 1;  lycon->line.last_space_pos = text->width;
            lycon->line.has_space = true;
        }
        else { 
            str = next_ch;  lycon->line.is_line_start = false;  lycon->line.has_space = false;
        }
    } while (*str);
    // end of text
    if (lycon->line.last_space) { // need to check if line will fill up
        int advance_x = lycon->line.advance_x;  lycon->line.advance_x += text->width;
        if (view_has_line_filled(lycon, (View*)text, text->node) == RDT_LINE_FILLED) {
            if (text_start <= lycon->line.last_space && lycon->line.last_space < str) {
                str = lycon->line.last_space + 1;
                output_text(lycon, text, str - text_start - text->start_index, lycon->line.last_space_pos);
                line_break(lycon);  
                if (*str) goto LAYOUT_TEXT;
                else return;  // end of text
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
    output_text(lycon, text, str - text_start - text->start_index, text->width);
}