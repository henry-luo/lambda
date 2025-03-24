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

#define LENGTH_AUTO                 (INT_MAX - 1)

typedef union {
    uint32_t c;  // 32-bit ABGR color format, 
    struct {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    };
} Color;  

typedef struct Rect {
    int x, y;
    int width, height;
} Rect;

typedef enum {
    IMAGE_FORMAT_UNKNOWN = 0,
    IMAGE_FORMAT_SVG,
    IMAGE_FORMAT_PNG,
    IMAGE_FORMAT_JPEG,
} ImageFormat;

typedef struct ImageSurface {
    ImageFormat format;
    int width;             // the intrinsic width of the surface/image
    int height;            // the intrinsic height of the surface/image
    int pitch;             // no. of bytes for rows of pixels
    // image pixels, 32-bits per pixel, RGBA format
    // pack order is [R] [G] [B] [A], high bit -> low bit    
    void *pixels;          // A pointer to the pixels of the surface, the pixels are writeable if non-NULL
    Tvg_Paint* pic;        // ThorVG picture for SVG image
    int max_render_width;  // maximum width for rendering the image
} ImageSurface;
extern ImageSurface* image_surface_create(int pixel_width, int pixel_height);
extern ImageSurface* image_surface_create_from(int pixel_width, int pixel_height, void* pixels);
extern void image_surface_destroy(ImageSurface* img_surface);
extern void fill_surface_rect(ImageSurface* surface, Rect* rect, uint32_t color, Rect* clip);
extern void blit_surface_scaled(ImageSurface* src, Rect* src_rect, ImageSurface* dst, Rect* dst_rect, Rect* clip);

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
    uint32_t top_specificity, right_specificity, bottom_specificity, left_specificity;
} Spacing;

typedef struct {
    Spacing width;
    PropValue style;
    Color top_color, right_color, bottom_color, left_color;
    uint32_t top_color_specificity, right_color_specificity, bottom_color_specificity, left_color_specificity;
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
    Tvg_Paint* v_scrollbar;    // Vertical scrollbar
    Tvg_Paint* v_scroll_handle;
    Tvg_Paint* h_scrollbar;    // Horizontal scrollbar
    Tvg_Paint* h_scroll_handle;
    
    int content_width, content_height;
    int view_x, view_y;
    int view_width, view_height;
    int v_scroll_position, h_scroll_position;
    int v_max_scroll, h_max_scroll;
    
    bool v_is_dragging, h_is_dragging;
    int drag_start_x, drag_start_y;
    int v_drag_start_scroll, h_drag_start_scroll;
    
    int drag_speed;
    int scrollSpeed;
} ScrollPane;

typedef struct {
    PropValue overflow_x, overflow_y;
    ScrollPane* pane;
    bool has_hz_overflow, has_vt_overflow;
    bool has_hz_scroll, has_vt_scroll;

    Rect clip; // clipping rect, relative to the block border box
    bool has_clip;
} ScrollProp;

typedef struct {
    ViewSpan;  // extends ViewSpan
    // x, y, width, height forms the BORDER box of the block
    int x, y, width, height;  // x, y are relative to the parent block
    int content_width, content_height;  // width and height of the child content including padding
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

typedef struct StateStore {
    CaretState* caret;
    CursorState* cursor;
    bool is_dirty;
} StateStore;

// layout, rendering context structs
typedef struct {
    FontProp style;  // current font style
    FT_Face face;  // current font face
    float space_width;  // width of a space character of the current font 
    int current_font_size;  // font size of current element
} FontBox;

// rendering context structs
typedef struct {
    int x, y;  // abs x, y relative to entire canvas/screen
    Rect clip;  // clipping rect
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

    // font handling
    FcConfig *font_config;
    FT_Library ft_library;
    struct hashmap* fontface_map;  // cache of font faces loaded
    FontProp default_font;  // default font style

    // image cache
    struct hashmap* image_cache;  // cache for images loaded

    float pixel_ratio;      // actual vs. logical pixel ratio, could be 1.0, 1.5, 2.0, etc.
    Document* document;     // current document
    MouseState mouse_state; // current mouse state
} UiContext;

extern FT_Face load_styled_font(UiContext* uicon, const char* font_name, FontProp* font_style);
extern void setup_font(UiContext* uicon, FontBox *fbox, const char* font_name, FontProp *fprop);

extern ImageSurface* load_image(UiContext* uicon, const char *file_path);