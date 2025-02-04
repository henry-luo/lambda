#include "layout.h"

void layout_node(LayoutContext* lycon, StyleNode* style_elmt);

bool can_break(char c) {
    return c == ' ' || c == '\t' || c== '\r' || c == '\n';
}

bool is_space(char c) {
    return c == ' ' || c == '\t';
}

void layout_block(LayoutContext* lycon, StyleElement* style_elmt) {
    printf("layout block %s\n", lxb_dom_element_local_name(style_elmt->node, NULL));
    Blockbox bbox = lycon->block;  Linebox lbox = lycon->line;
    lycon->block.width = bbox.width;  lycon->block.height = bbox.height;  
    lycon->block.advance_y = 0;  lycon->block.max_width = 0;
    lycon->line.advance_x = 0;  lycon->line.max_height = 0;  
    lycon->line.right = lycon->block.width;
    ViewBlock* block = calloc(1, sizeof(ViewBlock));
    block->type = RDT_VIEW_BLOCK;  block->style = style_elmt;  block->parent = lycon->parent;  
    // link the block
    if (lycon->prev_view) { lycon->prev_view->next = block; }
    else { lycon->parent->child = block; }
    
    StyleNode* node = style_elmt->child;
    if (node) {
        lycon->parent = block;  lycon->prev_view = NULL;
        do {
            layout_node(lycon, node);
            node = node->next;
        } while (node);
        lycon->parent = block->parent;
    }
    block->width = lycon->block.max_width;  block->height = lycon->block.advance_y;
    lycon->block = bbox;  lycon->line = lbox;
    lycon->prev_view = block;
    lycon->block.advance_y += block->height;
    lycon->block.max_width = max(lycon->block.max_width, block->width);
}

void layout_text(LayoutContext* lycon, StyleText* style_text) {
    char* str = style_text->str;  printf("layout text %s\n", str);
    LAYOUT_TEXT:
    // assume style_text has at least one character
    ViewText* text = calloc(1, sizeof(ViewText));
    text->type = RDT_VIEW_TEXT;  
    // link the text
    if (lycon->prev_view) { lycon->prev_view->next = (View*)text; }
    else { lycon->parent->child = (View*)text; }
    lycon->prev_view = (View*)text;    
    text->style = style_text;  text->start_index = 0;
    text->x = lycon->line.advance_x; text->y = lycon->block.advance_y;
    // layout the text
    do {
        if (FT_Load_Char(lycon->face, *str, FT_LOAD_NO_SCALE)) {
            fprintf(stderr, "Could not load character '%c'\n", *str);
            return;
        }
        FT_GlyphSlot slot = lycon->face->glyph;  int wd = slot->advance.x >> 6;
        text->height = max(text->height, slot->metrics.height >> 6);
        printf("char: %c, width: %d, height: %d, right: %d\n", *str, wd, text->height, lycon->line.right);
        text->width += wd;
        if (text->x + text->width >= lycon->line.right) { // line filled up
            printf("line filled up\n");
            if (can_break(*str)) {
                // skip all spaces
                while (is_space(*str)) { str++; }
                text->length = str - style_text->str;
                lycon->line.max_height = max(lycon->line.max_height, text->height);
                lycon->block.advance_y += lycon->line.max_height;
                // reset linebox
                lycon->line.advance_x = 0;  lycon->line.max_height = 0;
                printf("text view: x %d, y %d, width %d, height %d\n", text->x, text->y, text->width, text->height);
                if (*str) { str++;  goto LAYOUT_TEXT; }
                else return;
            }
        }
        str++;
    } while (*str);
    text->length = str - style_text->str;  lycon->line.advance_x += text->width;
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

void print_view_tree(ViewBlock* view_block, char* indent) {
    printf("%sview: %s\n", indent, lxb_dom_element_local_name(view_block->style->node, NULL));
    View* view = view_block->child;
    if (view) {
        printf("%s view block\n", indent);
        char* nest_indent = malloc(strlen(indent) + 3);  
        sprintf(nest_indent, "%s%s", indent, "  ");
        do {
            if (view->type == RDT_VIEW_BLOCK) {
                print_view_tree((ViewBlock*)view, nest_indent);
            }
            else {
                printf("%s%s\n", nest_indent, lxb_dom_element_local_name(view->style->node, NULL));
            }
            view = view->next;
        } while (view);
        free(nest_indent);
    }
}

int layout_init(LayoutContext* lycon) {
    // Initialize FreeType
    if (FT_Init_FreeType(&lycon->library)) {
        fprintf(stderr, "Could not initialize FreeType library\n");
        return EXIT_FAILURE;
    }
    // Load a font face
    if (FT_New_Face(lycon->library, "./lato.ttf", 0, &lycon->face)) {
        fprintf(stderr, "Could not load font\n");
        printf("Could not load font\n");
        return EXIT_FAILURE;
    }
    // Set the font size
    FT_Set_Pixel_Sizes(lycon->face, 0, 48);    
}

View* layout_style_tree(StyleElement* style_root) {
    LayoutContext lycon;
    layout_init(&lycon);
    ViewBlock* root_view = calloc(1, sizeof(ViewBlock));
    root_view->style = style_root;
    lycon.parent = root_view;
    lycon.block.width = 50;  lycon.block.height = 600;
    lycon.block.advance_y = 0;  lycon.block.max_width = 800;
    layout_block(&lycon, style_root);

    printf("View tree:\n");
    print_view_tree(root_view, "  ");
    return (View*)root_view;
}