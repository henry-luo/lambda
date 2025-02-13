#include "dom.h"
#include <fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <thorvg_capi.h>

typedef unsigned short PropValue;
#define RDT_DISPLAY_TEXT    (LXB_CSS_VALUE__LAST_ENTRY + 10)

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
    PropValue font_style;
    PropValue font_weight;
    PropValue text_deco; // CSS text decoration    
} FontProp;

typedef struct {
    PropValue text_align;
    float line_height;
} BlockProp;

typedef struct View {
    ViewType type;
    lxb_dom_node_t *node;  // future optimization: use 32-bit pointer for style node
    struct View* next;
    struct ViewGroup* parent;
} View;

typedef struct {
    View; // extends View
    float x, y, width, height;  // bounds for the text, x, y relative to the parent block
    int start_index, length;  // start and length of the text in the style node
} ViewText;

typedef struct {
    View; // extends View
    struct View* child;  // first child view
} ViewGroup;

typedef struct {
    ViewGroup;  // extends ViewGroup
    FontProp font;  // font style
} ViewSpan;

typedef struct {
    ViewGroup;  // extends ViewGroup
    float x, y, width, height;  // x, y relative to the parent block    
    BlockProp* props;  // block style properties
} ViewBlock;

typedef struct {
    SDL_Surface* surface;  // rendering surface of a window
    Tvg_Canvas* canvas;    // ThorVG canvas
    SDL_Texture* texture;  // texture for rendering
    FcConfig *font_config;
    FT_Library ft_library; 
    float pixel_ratio;      // actual vs. logical pixel ratio, could be 1.0, 1.5, 2.0, etc.
} UiContext;

extern FT_Face load_font_face(UiContext* uicon, const char* font_name, int font_size);
extern FT_Face load_styled_font(UiContext* uicon, FT_Face parent, FontProp* font_style);