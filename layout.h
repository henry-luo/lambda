#include "view.h"

typedef struct {
    struct StyleElement* parent;
    struct StyleNode* prev_node;
} StyleContext;

typedef struct {
    int width, height;  // given width and height of the block
    int advance_y;
    int max_width;
    PropValue text_align;
} Blockbox;

typedef struct {
    int left, right;  // left and right bounds of the line
    int advance_x;
    int max_height;
    char* last_space;
    bool is_line_start;
} Linebox;

typedef struct {
    FontProp style;  // current font style
    FT_Face face;   // current font face
} FontBox;

typedef struct {
    ViewBlock* parent;
    View* prev_view;
    Blockbox block;  // current blockbox
    Linebox line;  // current linebox
    FontBox font;  // current font style
    UiContext* ui_context;
} LayoutContext;
