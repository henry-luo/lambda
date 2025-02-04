#pragma once
#include <stdbool.h>
#include <stdint.h>

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

#include <ft2build.h>
#include FT_FREETYPE_H
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
    StyleNode; // extends StyleNode
    char* str;  // text content
} StyleText;

typedef struct StyleElement {
    StyleNode; // extends StyleNode
    // style tree pointers
    struct StyleNode* parent;
    struct StyleElement* child; // first child
} StyleElement;

typedef enum {
    RDT_VIEW_BLOCK = 1,
    RDT_VIEW_TEXT,

    RDT_VIEW_INLINE,
    RDT_VIEW_INLINE_BLOCK,
    RDT_VIEW_FLEX,
    RDT_VIEW_GRID,
    RDT_VIEW_TABLE,
    RDT_VIEW_TABLE_CELL,
    RDT_VIEW_TABLE_ROW,
    RDT_VIEW_TABLE_ROW_GROUP,
    RDT_VIEW_TABLE_COLUMN,
    RDT_VIEW_TABLE_COLUMN_GROUP,
    RDT_VIEW_TABLE_CAPTION,
    RDT_VIEW_TABLE_HEADER_GROUP,
    RDT_VIEW_TABLE_FOOTER_GROUP,
    RDT_VIEW_TABLE_BODY_GROUP,
    RDT_VIEW_LIST_ITEM,
    RDT_VIEW_NONE,
} ViewType;

typedef struct View {
    ViewType type;
    struct View* next;
    StyleNode* style;
} View;

typedef struct {
    View; // extends View
    int start_index, length;  // start and length of the text in the style node
    int x, y, width, height;  // bounds for the text, x, y relative to the parent block
} ViewText;

typedef struct {
    View; // extends View
    int x, y, width, height;  // x, y relative to the parent block
    struct ViewBlock* parent;  // parent block
    struct View* child;  // first child view
} ViewBlock;