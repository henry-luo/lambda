#include "view.h"

typedef struct {
    struct StyleElement* parent;
    struct StyleNode* prev_node;
} StyleContext;

typedef struct {
    int width, height;  // given width and height of the block
    int advance_y;
    int max_width;
} Blockbox;

typedef struct {
    int left, right;  // left and right bounds of the line
    int advance_x;
    int max_height;
    char* last_space;
    bool is_line_start;
} Linebox;

typedef struct {
    UiContext* ui_context;
    Blockbox block;  // current blockbox
    Linebox line;  // current linebox
    ViewBlock* parent;
    View* prev_view;
    FT_Face face;   // current font face
} LayoutContext;
