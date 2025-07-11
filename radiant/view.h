#pragma once
#include "dom.h"
#include <GLFW/glfw3.h>
#include <fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <thorvg_capi.h>
#include "../lib/mem-pool/include/mem_pool.h"
#include "event.h"
#include "flex.h"

#define RDT_DISPLAY_TEXT                (LXB_CSS_VALUE__LAST_ENTRY + 10)
#define RDT_DISPLAY_REPLACED            (LXB_CSS_VALUE__LAST_ENTRY + 11)

#define LXB_CSS_VALUE_DISC              (LXB_CSS_VALUE__LAST_ENTRY + 20)
#define LXB_CSS_VALUE_CIRCLE            (LXB_CSS_VALUE__LAST_ENTRY + 21)
#define LXB_CSS_VALUE_SQUARE            (LXB_CSS_VALUE__LAST_ENTRY + 22)
#define LXB_CSS_VALUE_DECIMAL           (LXB_CSS_VALUE__LAST_ENTRY + 23)
#define LXB_CSS_VALUE_LOWER_ROMAN       (LXB_CSS_VALUE__LAST_ENTRY + 24)
#define LXB_CSS_VALUE_UPPER_ROMAN       (LXB_CSS_VALUE__LAST_ENTRY + 25)
#define LXB_CSS_VALUE_LOWER_ALPHA       (LXB_CSS_VALUE__LAST_ENTRY + 26)
#define LXB_CSS_VALUE_UPPER_ALPHA       (LXB_CSS_VALUE__LAST_ENTRY + 27)


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

typedef struct Bound {
    int left, top, right, bottom;
} Bound;

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
    lxb_url_t* url;        // the resolved absolute URL of the image
} ImageSurface;
extern ImageSurface* image_surface_create(int pixel_width, int pixel_height);
extern ImageSurface* image_surface_create_from(int pixel_width, int pixel_height, void* pixels);
extern void image_surface_destroy(ImageSurface* img_surface);
extern void fill_surface_rect(ImageSurface* surface, Rect* rect, uint32_t color, Bound* clip);
extern void blit_surface_scaled(ImageSurface* src, Rect* src_rect, ImageSurface* dst, Rect* dst_rect, Bound* clip);

extern bool can_break(char c);
extern bool is_space(char c);

typedef enum {
    RDT_VIEW_NONE = 0,
    RDT_VIEW_TEXT,
    RDT_VIEW_INLINE,
    RDT_VIEW_INLINE_BLOCK,
    RDT_VIEW_BLOCK,
    RDT_VIEW_LIST_ITEM,
    RDT_VIEW_SCROLL_PANE,
} ViewType;

typedef struct {
    PropValue outer;
    PropValue inner;
} DisplayValue;

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
    PropValue vertical_align;
} InlineProp;

typedef struct {
    union {
        struct { int top, right, bottom, left; };
        struct { int top_left, top_right, bottom_right, bottom_left; };
    };
    uint32_t top_specificity, right_specificity, bottom_specificity, left_specificity;
} Spacing;

typedef struct {
    Spacing width;
    PropValue top_style, right_style, bottom_style, left_style;
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
    int line_height;  // non-negative
    int text_indent;  // can be negative
    int min_width, max_width;  // non-negative
    int min_height, max_height;  // non-negative
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
    DisplayValue display;
};

typedef struct {
    ViewGroup;  // extends ViewGroup
    FontProp* font;  // font style
    BoundaryProp* bound;  // block boundary properties
    InlineProp* in_line;  // inline specific style properties
    FlexItemProp* flex_item; // flex item properties
} ViewSpan;

typedef struct {
    int v_scroll_position, h_scroll_position;
    int v_max_scroll, h_max_scroll;
    int v_handle_y, v_handle_height;
    int h_handle_x, h_handle_width;
    
    bool is_h_hovered, is_v_hovered;
    bool v_is_dragging, h_is_dragging;
    int drag_start_x, drag_start_y;
    int v_drag_start_scroll, h_drag_start_scroll;
} ScrollPane;

typedef struct {
    PropValue overflow_x, overflow_y;
    ScrollPane* pane;
    bool has_hz_overflow, has_vt_overflow;
    bool has_hz_scroll, has_vt_scroll;

    Bound clip; // clipping rect, relative to the block border box
    bool has_clip;
} ScrollProp;

typedef struct {
    ImageSurface* img;  // image surface
    Document* doc;  // iframe document
    FlexContainerProp* flex_container; // flex container properties
} EmbedProp;

typedef struct {
    ViewSpan;  // extends ViewSpan
    // x, y, width, height forms the BORDER box of the block
    int x, y, width, height;  // x, y are relative to the parent block
    int content_width, content_height;  // width and height of the child content including padding
    BlockProp* blk;  // block specific style properties
    ScrollProp* scroller;  // handles overflow
    // block content related properties for flexbox, image, iframe
    EmbedProp* embed;
} ViewBlock;

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
    bool is_dragging;
    View* drag_target;
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
    Bound clip;  // clipping rect
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
    char** fallback_fonts;  // fallback fonts

    // image cache
    struct hashmap* image_cache;  // cache for images loaded

    float pixel_ratio;      // actual vs. logical pixel ratio, could be 1.0, 1.5, 2.0, etc.
    Document* document;     // current document
    MouseState mouse_state; // current mouse state
} UiContext;

extern FT_Face load_styled_font(UiContext* uicon, const char* font_name, FontProp* font_style);
extern FT_GlyphSlot load_glyph(UiContext* uicon, FT_Face face, FontProp* font_style, uint32_t codepoint);
extern void setup_font(UiContext* uicon, FontBox *fbox, const char* font_name, FontProp *fprop);

extern ImageSurface* load_image(UiContext* uicon, const char *file_path);