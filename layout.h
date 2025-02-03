#include <stdint.h>
#include <lexbor/html/html.h>
// #include <lexbor/tag/const.h>           // html tag names
// #include <lexbor/css/value/const.h>     // css property values
// #include <lexbor/dom/interface.h>

typedef unsigned short rdt_short_value;
#define RDT_DISPLAY_TEXT    (LXB_CSS_VALUE__LAST_ENTRY + 10)

typedef struct StyleNode {
    rdt_short_value display;  // computed display value
    lxb_dom_node_t* node;
    struct StyleNode* next;
} StyleNode;

typedef struct StyleText { 
    StyleNode n; // extends StyleNode
    char* str;  // text content
} StyleText;

typedef struct StyleElement {
    StyleNode n; // extends StyleNode
    // style tree pointers
    struct StyleNode* parent;
    struct StyleElement* child; // first child
} StyleElement;

typedef struct {
    struct StyleElement* parent;
    struct StyleNode* prev_node;
} StyleContext;

typedef enum {
    VIEW_BLOCK,
    VIEW_INLINE,
    VIEW_INLINE_BLOCK,
    VIEW_FLEX,
    VIEW_GRID,
    VIEW_TABLE,
    VIEW_TABLE_CELL,
    VIEW_TABLE_ROW,
    VIEW_TABLE_ROW_GROUP,
    VIEW_TABLE_COLUMN,
    VIEW_TABLE_COLUMN_GROUP,
    VIEW_TABLE_CAPTION,
    VIEW_TABLE_HEADER_GROUP,
    VIEW_TABLE_FOOTER_GROUP,
    VIEW_TABLE_BODY_GROUP,
    VIEW_LIST_ITEM,
    VIEW_NONE,
} ViewType;
typedef struct View {
    ViewType type;
    struct View* next;
    StyleNode* style;
} View;
typedef struct {
    View v; // extends View
    int x, y, width, height;  // bounds for the text, x, y relative to the parent block
} ViewText;

typedef struct {
    View v; // extends View
    struct ViewBlock* parent;  // parent block
    int x, y, width, height;  // x, y relative to the parent block
} ViewBlock;

typedef struct {
    int width, height;  // given width and height
    int advance_y;
    int max_width;
} Blockbox;

typedef struct {
    int advance_x;
    int max_height;
} Linebox;

typedef struct {
    Blockbox block;  // current blockbox
    Linebox line;  // current linebox
    ViewBlock* parent;
} LayoutContext;