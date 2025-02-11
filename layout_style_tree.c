#include "layout.h"
#include "./lib/string_buffer/string_buffer.h"

bool is_space(char c) {
    return c == ' ' || c == '\t' || c== '\r' || c == '\n';
}

void line_break(LayoutContext* lycon);
void layout_node(LayoutContext* lycon, StyleNode* style_elmt);

View* alloc_view(LayoutContext* lycon, ViewType type, StyleNode* node) {
    View* view;
    switch (type) {
        case RDT_VIEW_BLOCK: view = calloc(1, sizeof(ViewBlock)); break;
        case RDT_VIEW_TEXT: view = calloc(1, sizeof(ViewText)); break;
        case RDT_VIEW_INLINE:  default:
            view = calloc(1, sizeof(ViewSpan)); break;
    }
    view->type = type;  view->style = node;  view->parent = lycon->parent;
    // link the view
    if (lycon->prev_view) { lycon->prev_view->next = view; }
    else { lycon->parent->child = view; }
    if (!lycon->line.start_view) lycon->line.start_view = view;
    return view;
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

void layout_block(LayoutContext* lycon, StyleBlock* style_elmt) {
    printf("layout block %s\n", lxb_dom_element_local_name(style_elmt->node, NULL));
    if (lycon->line.is_line_start) { line_break(lycon); }
        
    ViewBlock* block = alloc_view(lycon, RDT_VIEW_BLOCK, style_elmt);
    Blockbox pa_block = lycon->block;  Linebox pa_line = lycon->line;
    lycon->block.width = pa_block.width;  lycon->block.height = pa_block.height;  
    lycon->block.advance_y = 0;  lycon->block.max_width = 0;
    lycon->block.text_align = style_elmt->text_align;
    lycon->line.advance_x = 0;  lycon->line.max_height = 0;  
    lycon->line.right = lycon->block.width;  
    lycon->line.is_line_start = true;  lycon->line.last_space = NULL;  
    lycon->line.start_view = NULL;
    block->y = pa_block.advance_y;
    
    // layout block content
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
    // line_align(lycon);

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

void layout_inline(LayoutContext* lycon, StyleElement* style_elmt) {
    printf("layout inline %s\n", lxb_dom_element_local_name(style_elmt->node, NULL));
    ViewSpan* span = alloc_view(lycon, RDT_VIEW_INLINE, style_elmt);
    span->font = &style_elmt->font;
    FontBox pa_font = lycon->font;  lycon->font.style = style_elmt->font;
    lycon->font.face = load_styled_font(lycon->ui_context, lycon->font.face, span->font);

    // layout inline content
    StyleNode* node = style_elmt->child;
    if (node) {
        lycon->parent = span;  lycon->prev_view = NULL;
        do {
            layout_node(lycon, node);
            node = node->next;
        } while (node);
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

enum {
    RDT_NOT_SURE = 0,
    RDT_LINE_NOT_FILLED = 1,
    RDT_LINE_FILLED = 2,
};

bool text_has_line_filled(LayoutContext* lycon, StyleText* text) {
    int text_width = 0;  char *str = text->str;
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

bool span_has_line_filled(LayoutContext* lycon, StyleElement* span) {
    StyleNode* node = span->child;
    while (node) {
        if (node->display == LXB_CSS_VALUE_BLOCK) { return RDT_LINE_NOT_FILLED; }
        else if (node->display == LXB_CSS_VALUE_INLINE) {
            int result = span_has_line_filled(lycon, (StyleElement*)node);
            if (result) { return result; }
        }
        else if (node->display == RDT_DISPLAY_TEXT) {
            int result = text_has_line_filled(lycon, (StyleText*)node);
            if (result) { return result; }
        }
        node = node->next;
    }
    return RDT_NOT_SURE;
}

bool view_has_line_filled(LayoutContext* lycon, View* view) {
    // note: this function navigates to parenets through laid out view tree, 
    // and siblings through non-processed style nodes
    StyleElement* node;
    if (view->style->next) { node = view->style->next; }
    else goto CHECK_PARENT;

    CHECK_VIEW:
    if (node->display == LXB_CSS_VALUE_BLOCK) { return RDT_LINE_NOT_FILLED; }
    else if (node->display == LXB_CSS_VALUE_INLINE) {
        int result = span_has_line_filled(lycon, node);
        if (result) { return result; }
    }
    else if (node->display == RDT_DISPLAY_TEXT) {
        int result = text_has_line_filled(lycon, node);
        if (result) { return result; }
    }
    if (node->next) { node = node->next;  goto CHECK_VIEW; }
    // else check at parent level

    CHECK_PARENT:
    view = view->parent;
    if (view->style->display == LXB_CSS_VALUE_BLOCK) { return RDT_LINE_NOT_FILLED; }
    else if (view->style->display == LXB_CSS_VALUE_INLINE) {
        return view_has_line_filled(lycon, view);
    }
    printf("unknown view type\n");
    return RDT_NOT_SURE;
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
    ViewText* text = alloc_view(lycon, RDT_VIEW_TEXT, style_text);
    lycon->prev_view = (View*)text;    
    text->start_index = str - style_text->str;
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
                text->length = str - style_text->str - text->start_index;
                assert(text->length > 0);
                line_break(lycon);
                if (*str) { goto LAYOUT_TEXT; }
                else return;
            }
            else if (lycon->line.last_space) { // break at the last space
                printf("break at last space\n");
                if (style_text->str <= lycon->line.last_space && lycon->line.last_space < str) {
                    lycon->line.max_height = max(lycon->line.max_height, text->height);
                    str = lycon->line.last_space + 1;
                    text->length = str - style_text->str - text->start_index;
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
        if (view_has_line_filled(lycon, (View*)text) == RDT_LINE_FILLED) {
            if (style_text->str <= lycon->line.last_space && lycon->line.last_space < str) {
                lycon->line.max_height = max(lycon->line.max_height, text->height);
                str = lycon->line.last_space + 1;
                text->length = str - style_text->str - text->start_index;
                assert(text->length > 0);
                line_break(lycon);  
                goto LAYOUT_TEXT;
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
    text->length = str - style_text->str - text->start_index;  assert(text->length > 0);
    lycon->line.advance_x += text->width;
    lycon->line.max_height = max(lycon->line.max_height, text->height);
    printf("text view: x %d, y %d, width %d, height %d\n", text->x, text->y, text->width, text->height);
}

void layout_node(LayoutContext* lycon, StyleNode* style_node) {
    printf("layout node %s\n", lxb_dom_element_local_name(style_node->node, NULL));
    if (style_node->display == LXB_CSS_VALUE_BLOCK) {
        layout_block(lycon, (StyleBlock*)style_node);
    }
    else if (style_node->display == LXB_CSS_VALUE_INLINE) {
        layout_inline(lycon, (StyleElement*)style_node);
    }
    else if (style_node->display == RDT_DISPLAY_TEXT) {
        // layout text
        layout_text(lycon, (StyleText*)style_node);
    }
    else {
        printf("layout unknown node\n");
    }
}

void print_view_tree(ViewGroup* view_block, StrBuf* buf, int indent) {
    View* view = view_block->child;
    if (view) {
        do {
            printf("view %s\n", lxb_dom_element_local_name(view->style->node, NULL));
            strbuf_append_charn(buf, ' ', indent);
            if (view->type == RDT_VIEW_BLOCK) {
                ViewBlock* block = (ViewBlock*)view;
                strbuf_sprintf(buf, "view block:%s, x:%d, y:%d, wd:%d, hg:%d\n",
                    lxb_dom_element_local_name(block->style->node, NULL),
                    block->x, block->y, block->width, block->height);                
                print_view_tree((ViewGroup*)view, buf, indent+2);
            }
            else if (view->type == RDT_VIEW_INLINE) {
                ViewSpan* span = (ViewSpan*)view;
                strbuf_sprintf(buf, "view inline:%s, font deco: %s, weight: %s, style: %s\n",
                    lxb_dom_element_local_name(span->style->node, NULL), 
                    lxb_css_value_by_id(span->font->text_deco)->name, 
                    lxb_css_value_by_id(span->font->font_weight)->name,
                    lxb_css_value_by_id(span->font->font_style)->name);
                print_view_tree((ViewGroup*)view, buf, indent+2);
            }
            else if (view->type == RDT_VIEW_TEXT) {
                ViewText* text = (ViewText*)view;
                char* str = ((StyleText*)text->style)->str;
                printf("text:%s, %d\n", str, text->length);
                strbuf_append_str(buf, "text:'");  strbuf_append_strn(buf, str, text->length);
                strbuf_sprintf(buf, "', start:%d, len:%d, x:%d, y:%d, wd:%d, hg:%d\n", 
                    text->start_index, text->length, text->x, text->y, text->width, text->height);
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

View* layout_style_tree(UiContext* uicon, StyleBlock* style_root) {
    LayoutContext lycon;
    layout_init(&lycon, uicon);

    ViewBlock* root_view = calloc(1, sizeof(ViewBlock));
    root_view->type = RDT_VIEW_BLOCK;  root_view->style = style_root;
    lycon.parent = root_view;
    lycon.block.width = 200;  lycon.block.height = 600;
    lycon.block.advance_y = 0;  lycon.block.max_width = 800;
    layout_block(&lycon, style_root);
    printf("end layout\n");
    layout_cleanup(&lycon);

    StrBuf* buf = strbuf_new(4096);
    print_view_tree(root_view, buf, 0);
    printf("=================\nView tree:\n");
    printf("%s", buf->b);
    printf("=================\n");

    return (View*)root_view;
}