#pragma once
#include "dom.h"
#include <GLFW/glfw3.h>
#include <fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <thorvg_capi.h>
#include "./lib/mem-pool/include/mem_pool.h"
#include "event.h"

#define RDT_DISPLAY_TEXT            (LXB_CSS_VALUE__LAST_ENTRY + 10)
#define LXB_CSS_VALUE_REPLACED      (LXB_CSS_VALUE__LAST_ENTRY + 11)

#define LXB_CSS_VALUE_DISC          (LXB_CSS_VALUE__LAST_ENTRY + 12)
#define LXB_CSS_VALUE_CIRCLE        (LXB_CSS_VALUE__LAST_ENTRY + 13)
#define LXB_CSS_VALUE_SQUARE        (LXB_CSS_VALUE__LAST_ENTRY + 14)
#define LXB_CSS_VALUE_DECIMAL       (LXB_CSS_VALUE__LAST_ENTRY + 15)
#define LXB_CSS_VALUE_LOWER_ROMAN   (LXB_CSS_VALUE__LAST_ENTRY + 16)
#define LXB_CSS_VALUE_UPPER_ROMAN   (LXB_CSS_VALUE__LAST_ENTRY + 17)
#define LXB_CSS_VALUE_LOWER_ALPHA   (LXB_CSS_VALUE__LAST_ENTRY + 18)
#define LXB_CSS_VALUE_UPPER_ALPHA   (LXB_CSS_VALUE__LAST_ENTRY + 19)

typedef union {
    uint32_t c;  // 32-bit ARGB color format, 
    struct {
        uint8_t a;
        uint8_t b;
        uint8_t g;
        uint8_t r;
    };
} Color;  

typedef struct Rect {
    int x, y;
    int w, h;
} Rect;

typedef struct ImageSurface {
    // SDL_SurfaceFlags flags;     /**< The flags of the surface, read-only */
    // SDL_PixelFormat format;     /**< The format of the surface, read-only */
    int width;                      /**< The width of the surface, read-only. */
    int height;                      /**< The height of the surface, read-only. */
    int pitch;                  /**< The distance in bytes between rows of pixels, read-only */
    // image pixels, 32-bits per pixel, RGBA format
    // pack order is [R] [G] [B] [A], high bit -> low bit    
    void *pixels;               /**< A pointer to the pixels of the surface, the pixels are writeable if non-NULL */
    // int refcount;               /**< Application reference count, used when freeing surface */
    // void *reserved;             /**< Reserved for internal use */
} ImageSurface;
extern ImageSurface* image_surface_create(int pixel_width, int pixel_height);
extern ImageSurface* image_surface_create_from(int pixel_width, int pixel_height, void* pixels);
extern void image_surface_destroy(ImageSurface* img_surface);
extern void fill_surface_rect(ImageSurface* surface, Rect* rect, uint32_t color);
extern void blit_surface_scaled(ImageSurface* src, Rect* src_rect, ImageSurface* dst, Rect* dst_rect);

extern bool can_break(char c);
extern bool is_space(char c);

typedef enum {
    RDT_VIEW_NONE = 0,
    RDT_VIEW_TEXT,
    RDT_VIEW_INLINE,
    RDT_VIEW_INLINE_BLOCK,
    RDT_VIEW_BLOCK,
    RDT_VIEW_LIST,
    RDT_VIEW_LIST_ITEM,
    RDT_VIEW_IMAGE, 
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
} ViewType;

typedef struct {
    char* family;  // font family name
    int font_size;  // font size in pixels, scaled by pixel_ratio
    PropValue font_style;
    PropValue font_weight;
    PropValue text_deco; // CSS text decoration
} FontProp;

typedef struct {
    PropValue cursor;
    Color color;
} InlineProp;

typedef struct {
    int top, right, bottom, left;
} Spacing;

typedef struct {
    Spacing width;
    PropValue style;
    Color color;
    Spacing radius;    
} BorderProp;

typedef struct {
    Color color; // background color
    char* image; // background image path
    char* repeat; // repeat behavior
    char* position; // positioning of background image
} BackgroundProp;

typedef struct {
    Spacing margin;
    Spacing padding;
    BorderProp* border;
    BackgroundProp* background;
} BoundaryProp;

typedef struct {
    PropValue text_align;
    float line_height;
    float text_indent;
    // float letter_spacing;
    PropValue list_style_type;
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
    int x, y, width, height;  // bounds for the text, x, y relative to the parent block
    int start_index, length;  // start and length of the text in the style node
} ViewText;

struct ViewGroup {
    View; // extends View
    View* child;  // first child view
};

typedef struct {
    ViewGroup;  // extends ViewGroup
    FontProp* font;  // font style
    BoundaryProp* bound;  // block boundary properties
    InlineProp* in_line;  // inline specific style properties
    // prop: vertical_align - fully resolved during layout, not stored in view tree
} ViewSpan;

typedef struct {
    int x, y;
    int width, height;
    int contentWidth, contentHeight;
    int scrollX, scrollY;
    PropValue overflowX, overflowY;
    bool hasHScroll, hasVScroll;
    int scrollSpeed;
    bool draggingHScroll;    // Is horizontal scrollbar being dragged
    bool draggingVScroll;    // Is vertical scrollbar being dragged
    int dragStartX, dragStartY;  // Changed to int for consistent types
    int scrollStartX, scrollStartY;  // Changed to int for consistent types
} ScrollProp;

typedef struct {
    ViewSpan;  // extends ViewSpan
    // x, y, width, height forms the BORDER box of the block
    int x, y, width, height;  // x, y are relative to the parent block
    int content_width, content_height;  // width and height of the content box
    BlockProp* props;  // block specific style properties
    ScrollProp* scroller;  // handles overflow
} ViewBlock;

typedef struct {
    ViewBlock;  // extends ViewBlock
    // const char* src;  // image src; should be in URL format
    ImageSurface* img;  // image surface
} ViewImage;

struct ViewTree {
    VariableMemPool *pool;
    View* root;
};

typedef struct CursorState {
    View* view;
    int x, y;
} CursorState;

typedef struct CaretState {
    View* view;
    int x_offset;
} CaretState;

typedef struct StateTree {
    CaretState* caret;
    CursorState* cursor;
} StateTree;

// layout, rendering context structs
typedef struct {
    FontProp style;  // current font style
    FT_Face face;  // current font face
    float space_width;  // width of a space character of the current font 
} FontBox;

// rendering context structs
typedef struct {
    int x, y;  // abs x, y relative to entire canvas/screen
} BlockBlot;

typedef struct {
    PropValue list_style_type;
    int item_index;
} ListBlot;

typedef struct {
    GLFWwindow *window;    // current window
    int window_width;    // window pixel width
    int window_height;   // window pixel height
    ImageSurface* surface;  // rendering surface of a window
    Tvg_Canvas* canvas;    // ThorVG canvas

    // font handling
    FcConfig *font_config;
    FT_Library ft_library;
    struct hashmap* fontface_map;  // cache of font faces loaded

    // image cache
    struct hashmap* image_cache;  // cache for images loaded

    float pixel_ratio;      // actual vs. logical pixel ratio, could be 1.0, 1.5, 2.0, etc.
    Document* document;     // current document
    MouseState mouse_state; // current mouse state
} UiContext;

extern FT_Face load_styled_font(UiContext* uicon, const char* font_name, FontProp* font_style);
extern void setup_font(UiContext* uicon, FontBox *fbox, const char* font_name, FontProp *fprop);
extern FontProp default_font_prop;