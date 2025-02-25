#pragma once
#include "dom.h"
#include <fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <thorvg_capi.h>
#include "./lib/mem-pool/include/mem_pool.h"
#include "event.h"

#define RDT_DISPLAY_TEXT        (LXB_CSS_VALUE__LAST_ENTRY + 10)

extern bool can_break(char c);
extern bool is_space(char c);

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

typedef struct {
    float font_size;
    PropValue font_style;
    PropValue font_weight;
    PropValue text_deco; // CSS text decoration
} FontProp;

typedef struct {
    PropValue cursor;
} InlineProp;

typedef struct {
    PropValue text_align;
    float line_height;
} BlockProp;

typedef struct View View;
typedef struct ViewGroup ViewGroup;

struct View {
    ViewType type;
    lxb_dom_node_t *node;  // future optimization: use 32-bit pointer for style node
    View* next;
    ViewGroup* parent;  // corrected the type to ViewGroup
};

typedef struct {
    View; // extends View
    float x, y, width, height;  // bounds for the text, x, y relative to the parent block
    int start_index, length;  // start and length of the text in the style node
} ViewText;

struct ViewGroup {
    View; // extends View
    View* child;  // first child view
};

typedef struct {
    ViewGroup;  // extends ViewGroup
    // todo: convert FontProp to pointer
    FontProp* font;  // font style
    InlineProp* in_line;  // inline style properties
    // prop: vertical_align - fully resolved during layout, not stored in view tree
} ViewSpan;

typedef struct {
    ViewSpan;  // extends ViewSpan
    float x, y, width, height;  // x, y relative to the parent block    
    BlockProp* props;  // block style properties
} ViewBlock;

struct ViewTree {
    VariableMemPool *pool;
    View* root;
};

typedef struct CursorState {
    View* view;
    float x, y;
} CursorState;

typedef struct CaretState {
    View* view;
    float x_offset;
} CaretState;

typedef struct StateTree {
    CaretState* caret;
    CursorState* cursor;
} StateTree;

typedef struct {
    float x, y;  // abs x, y relative to entire canvas/screen
} BlockBlot;

typedef struct {
    FontProp style;  // current font style
    FT_Face face;  // current font face
    float space_width;  // width of a space character of the current font 
} FontBox;

typedef struct {
    SDL_Window *window;    // current window
    SDL_Renderer *renderer;  // current window renderer
    float window_width;    // logical window width
    float window_height;   // logical window height
    SDL_Surface* surface;  // rendering surface of a window
    Tvg_Canvas* canvas;    // ThorVG canvas
    SDL_Texture* texture;  // texture for rendering

    // font handling
    FcConfig *font_config;
    FT_Library ft_library;
    struct hashmap* fontface_map;  // font faces loaded

    float pixel_ratio;      // actual vs. logical pixel ratio, could be 1.0, 1.5, 2.0, etc.
    Document* document;     // current document
    MouseState mouse_state; // current mouse state
} UiContext;

extern FT_Face load_font_face(UiContext* uicon, const char* font_name, int font_size);
extern FT_Face load_styled_font(UiContext* uicon, FT_Face parent, FontProp* font_style);
extern void setup_font(UiContext* uicon, FontBox *fbox, FontProp *fprop);
extern FontProp default_font_prop;