#include "layout.h"
#include "./lib/string_buffer/string_buffer.h"

bool is_space(char c) {
    return c == ' ' || c == '\t' || c== '\r' || c == '\n';
}

void layout_node(LayoutContext* lycon, StyleNode* style_elmt);

void layout_block(LayoutContext* lycon, StyleElement* style_elmt) {
    printf("layout block %s\n", lxb_dom_element_local_name(style_elmt->node, NULL));
    Blockbox pa_block = lycon->block;  Linebox pa_line = lycon->line;
    lycon->block.width = pa_block.width;  lycon->block.height = pa_block.height;  
    lycon->block.advance_y = 0;  lycon->block.max_width = 0;
    lycon->line.advance_x = 0;  lycon->line.max_height = 0;  
    lycon->line.right = lycon->block.width;  
    lycon->line.is_line_start = true;  lycon->line.last_space = NULL;
    ViewBlock* block = calloc(1, sizeof(ViewBlock));
    block->type = RDT_VIEW_BLOCK;  block->style = style_elmt;  block->parent = lycon->parent;
    block->y = pa_block.advance_y;
    // link the block
    if (lycon->prev_view) { lycon->prev_view->next = block; }
    else { lycon->parent->child = block;  printf("link as first child\n"); }
    
    StyleNode* node = style_elmt->child;
    if (node) {
        lycon->parent = block;  lycon->prev_view = NULL;
        do {
            layout_node(lycon, node);
            node = node->next;
        } while (node);
        // handle last line
        if (lycon->line.max_height) {
            lycon->block.advance_y += lycon->line.max_height;
        }
        lycon->parent = block->parent;
        printf("block height: %d\n", lycon->block.advance_y);
    }
    block->width = max(lycon->block.width, lycon->block.max_width);  
    block->height = lycon->block.advance_y;
    pa_block.advance_y += block->height;
    pa_block.max_width = max(pa_block.max_width, block->width);
    lycon->block = pa_block;
    // reset linebox
    pa_line.advance_x = 0;  pa_line.max_height = 0;
    pa_line.is_line_start = true;  pa_line.last_space = NULL;
    lycon->line = pa_line;
    lycon->prev_view = block;
    printf("block view: %d, self %d, child %d\n", block->type, block, block->child);
}

void line_break(LayoutContext* lycon, ViewText* text) {
    lycon->line.max_height = max(lycon->line.max_height, text->height);
    lycon->block.advance_y += lycon->line.max_height;
    // reset linebox
    lycon->line.advance_x = 0;  lycon->line.max_height = 0;  
    lycon->line.is_line_start = true;  lycon->line.last_space = NULL;
    printf("text view: x %d, y %d, width %d, height %d\n", text->x, text->y, text->width, text->height);
}

void layout_text(LayoutContext* lycon, StyleText* style_text) {
    char* str = style_text->str;  printf("layout text %s\n", str);
    if (lycon->line.is_line_start && is_space(*str)) { // skip space at start of line
        do { str++; } while (is_space(*str));
        if (*str) { lycon->line.is_line_start = false; }
        else return;
    }
    LAYOUT_TEXT:
    // assume style_text has at least one character
    ViewText* text = calloc(1, sizeof(ViewText));
    text->type = RDT_VIEW_TEXT;  
    // link the text
    if (lycon->prev_view) { lycon->prev_view->next = (View*)text; }
    else { lycon->parent->child = (View*)text;  printf("link as first child\n"); }
    lycon->prev_view = (View*)text;    
    text->style = style_text;  text->start_index = str - style_text->str;
    text->x = lycon->line.advance_x;  text->y = lycon->block.advance_y;
    // layout the text
    do {
        if (FT_Load_Char(lycon->face, *str, FT_LOAD_NO_SCALE)) {
            fprintf(stderr, "Could not load character '%c'\n", *str);
            return;
        }
        FT_GlyphSlot slot = lycon->face->glyph;  int wd = slot->advance.x >> 6;
        text->height = max(text->height, slot->metrics.height >> 6);
        // printf("char: %c, width: %d, height: %d, right: %d\n", *str, wd, text->height, lycon->line.right);
        text->width += wd;
        if (text->x + text->width >= lycon->line.right) { // line filled up
            printf("line filled up\n");
            if (is_space(*str)) {
                printf("break on space\n");
                // skip all spaces
                do { str++; } while (is_space(*str));
                text->length = str - style_text->str - text->start_index;
                line_break(lycon, text);
                if (*str) { goto LAYOUT_TEXT; }
                else return;
            }
            else if (lycon->line.last_space) { // break at the last space
                printf("break at last space\n");
                text->length = lycon->line.last_space - style_text->str - text->start_index + 1;
                str = lycon->line.last_space + 1;
                line_break(lycon, text);
                goto LAYOUT_TEXT;
            }
            // else cannot break, and got overflow
        }
        if (is_space(*str)) {
            do { str++; } while (is_space(*str));
            lycon->line.last_space = str - 1;
        }
        else { str++; }
    } while (*str);
    text->length = str - style_text->str - text->start_index;
    lycon->line.advance_x += text->width;  
    lycon->line.max_height = max(lycon->line.max_height, text->height);
    printf("text view: x %d, y %d, width %d, height %d\n", text->x, text->y, text->width, text->height);
}

void layout_node(LayoutContext* lycon, StyleNode* style_node) {
    printf("layout node %s\n", lxb_dom_element_local_name(style_node->node, NULL));
    if (style_node->display == LXB_CSS_VALUE_BLOCK) {
        layout_block(lycon, (StyleElement*)style_node);
    }
    else if (style_node->display == LXB_CSS_VALUE_INLINE) {
        // layout inline
    }
    else if (style_node->display == RDT_DISPLAY_TEXT) {
        // layout text
        layout_text(lycon, (StyleText*)style_node);
    }
    else {
        printf("layout unknown node\n");
    }
}

void print_view_tree(ViewBlock* view_block, StrBuf* buf, int indent) {
    View* view = view_block->child;
    if (view) {
        do {
            strbuf_append_charn(buf, ' ', indent);
            if (view->type == RDT_VIEW_BLOCK) {
                ViewBlock* block = (ViewText*)view;
                strbuf_sprintf(buf, "view block:%s, x:%d, y:%d, wd:%d, hg:%d\n",
                    lxb_dom_element_local_name(block->style->node, NULL),
                    block->x, block->y, block->width, block->height);                
                print_view_tree((ViewBlock*)view, buf, indent+2);
            }
            else {
                ViewText* text = (ViewText*)view;
                char* str = ((StyleText*)text->style)->str;
                strbuf_append_str(buf, "text:'");  strbuf_append_strn(buf, str, text->length);
                strbuf_sprintf(buf, "', start:%d, len:%d, x:%d, y:%d, wd:%d, hg:%d\n", 
                    text->start_index, text->length, text->x, text->y, text->width, text->height);
            }
            view = view->next;
        } while (view);
    }
    else {
        strbuf_append_charn(buf, ' ', indent);
        strbuf_append_str(buf, "view has no child\n");
    }
}

int layout_init(LayoutContext* lycon, UiContext* uicon) {
    memset(lycon, 0, sizeof(LayoutContext));
    lycon->ui_context = uicon;
    // most browsers use a generic sans-serif font as the default
    // Google Chrome default fonts: Times New Roman (Serif), Arial (Sans-serif), and Courier New (Monospace)
    // default font size in HTML is 16 px for most browsers
    lycon->face = load_font_face(uicon, "Arial", 16);
}

int layout_cleanup(LayoutContext* lycon) {
    FT_Done_Face(lycon->face);
}

View* layout_style_tree(UiContext* uicon, StyleElement* style_root) {
    LayoutContext lycon;
    layout_init(&lycon, uicon);
    ViewBlock* root_view = calloc(1, sizeof(ViewBlock));
    root_view->type = RDT_VIEW_BLOCK;  root_view->style = style_root;
    lycon.parent = root_view;
    lycon.block.width = 200;  lycon.block.height = 600;
    lycon.block.advance_y = 0;  lycon.block.max_width = 800;
    layout_block(&lycon, style_root);
    layout_cleanup(&lycon);

    StrBuf* buf = strbuf_new(1024);
    print_view_tree(root_view, buf, 0);
    printf("=================\nView tree:\n");
    printf("%s", buf->b);
    printf("=================\n");

    return (View*)root_view;
}