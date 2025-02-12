#include "view.h"

typedef struct {
    struct StyleElement* parent;
    struct StyleNode* prev_node;
    lxb_css_parser_t *css_parser;
} StyleContext;

typedef struct {
    float width, height;  // given width and height of the block
    float advance_y;
    float max_width;
    float line_height;
    PropValue text_align;
} Blockbox;

typedef struct {
    float left, right;  // left and right bounds of the line
    float advance_x;
    float max_height;
    unsigned char* last_space; // last space character in the line
    View* start_view;
    bool is_line_start;
    bool has_space; // whether last layout character is a space
} Linebox;

typedef struct {
    FontProp style;  // current font style
    FT_Face face;  // current font face
    float space_width;  // width of a space character of the current font 
} FontBox;

typedef struct {
    ViewBlock* parent;
    View* prev_view;
    Blockbox block;  // current blockbox
    Linebox line;  // current linebox
    FontBox font;  // current font style
    UiContext* ui_context;
} LayoutContext;
