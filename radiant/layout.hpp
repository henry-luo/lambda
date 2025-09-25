#include "view.hpp"
#include "dom.hpp"

typedef struct {
    struct StyleElement* parent;
    struct StyleNode* prev_node;
    lxb_css_parser_t *css_parser;
} StyleContext;

typedef struct Blockbox {
    int width, height;  // given width and height for the inner content of the block
    int advance_y;  // advance_y includes padding.top and border.top of current block
    int max_width, max_height;  // max content width and height (without padding)
    int line_height;
    int init_ascender;  // initial ascender of the line at start of the line
    int init_descender;  // initial descender of the line at start of the line
    PropValue text_align;
    int given_width, given_height;  // specified width and height by css or html attributes
    struct Blockbox* pa_block;  // parent block
} Blockbox;

typedef struct {
    int left, right;  // left and right bounds of the line
    int advance_x;
    int max_ascender;
    int max_descender;
    unsigned char* last_space; // last space character in the line
    int last_space_pos;  // position of the last space in the line
    View* start_view;
    PropValue vertical_align;
    bool is_line_start;
    bool has_space; // whether last layout character is a space
    FontBox line_start_font;
} Linebox;

typedef enum LineFillStatus {
    RDT_NOT_SURE = 0,
    RDT_LINE_NOT_FILLED = 1,
    RDT_LINE_FILLED = 2,
} LineFillStatus;

typedef struct {
    ViewGroup* parent;
    View* prev_view;
    View* view;  // current view
    Blockbox block;  // current blockbox
    Linebox line;  // current linebox
    FontBox font;  // current font style
    int root_font_size;
    DomNode *elmt;  // current dom element, used before the view is created
    Document* doc;
    UiContext* ui_context;
} LayoutContext;

void* alloc_prop(LayoutContext* lycon, size_t size);
FontProp* alloc_font_prop(LayoutContext* lycon);
BlockProp* alloc_block_prop(LayoutContext* lycon);
FlexItemProp* alloc_flex_item_prop(LayoutContext* lycon);
void alloc_flex_container_prop(LayoutContext* lycon, ViewBlock* block);
View* alloc_view(LayoutContext* lycon, ViewType type, DomNode *node);
void free_view(ViewTree* tree, View* view);

void line_break(LayoutContext* lycon);
void line_align(LayoutContext* lycon);
void layout_flow_node(LayoutContext* lycon, DomNode *node);
void layout_block(LayoutContext* lycon, DomNode *elmt, DisplayValue display);
void layout_text(LayoutContext* lycon, DomNode *text_node);
void layout_inline(LayoutContext* lycon, DomNode *elmt, DisplayValue display);
lxb_status_t resolve_element_style(lexbor_avl_t *avl, lexbor_avl_node_t **root,
    lexbor_avl_node_t *node, void *ctx);
DisplayValue resolve_display(lxb_html_element_t* elmt);
JustifyContent resolve_justify_content(PropValue value);
Color color_name_to_rgb(PropValue color_name);

void layout_flex_container(FlexContainer* container);
void layout_flex_nodes(LayoutContext* lycon, DomNode *first_child);
void layout_html_root(LayoutContext* lycon, DomNode *elmt);
void free_flex_container(FlexContainer* container);

void line_init(LayoutContext* lycon);
int calculate_vertical_align_offset(PropValue align, int item_height, int line_height, int baseline_pos, int item_baseline);
void view_vertical_align(LayoutContext* lycon, View* view);

// DomNode style resolution
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon);
