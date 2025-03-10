#include "view.h"

typedef struct {
    struct StyleElement* parent;
    struct StyleNode* prev_node;
    lxb_css_parser_t *css_parser;
} StyleContext;

typedef struct {
    int width, height;  // given width and height for the inner content of the block
    int advance_y;
    int max_width, max_height;  // max content width and height (without padding)
    int line_height;
    PropValue text_align;
    int given_width, given_height;  // specified width and height by css or html attributes
} Blockbox;

typedef struct {
    int left, right;  // left and right bounds of the line
    int advance_x;
    int max_ascender;
    int max_descender;
    unsigned char* last_space; // last space character in the line
    View* start_view;
    PropValue vertical_align;
    bool is_line_start;
    bool has_space; // whether last layout character is a space
} Linebox;

typedef struct {
    ViewGroup* parent;
    View* prev_view;
    View* view;  // current view
    Blockbox block;  // current blockbox
    Linebox line;  // current linebox
    FontBox font;  // current font style
    Document* doc;
    UiContext* ui_context;
} LayoutContext;

void* alloc_prop(LayoutContext* lycon, size_t size);
FontProp* alloc_font_prop(LayoutContext* lycon);
View* alloc_view(LayoutContext* lycon, ViewType type, lxb_dom_node_t *node);
void free_view(ViewTree* tree, View* view);

void line_break(LayoutContext* lycon);
void line_align(LayoutContext* lycon);
void layout_node(LayoutContext* lycon, lxb_dom_node_t *node);
void layout_block(LayoutContext* lycon, lxb_html_element_t *elmt, PropValue display);
lxb_status_t resolve_element_style(lexbor_avl_t *avl, lexbor_avl_node_t **root,
    lexbor_avl_node_t *node, void *ctx);