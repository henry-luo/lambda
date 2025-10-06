#pragma once
#include "dom.hpp"

#ifdef __cplusplus
extern "C" {
#endif
#include <GLFW/glfw3.h>
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <thorvg_capi.h>
#ifdef __cplusplus
}
#endif

// #include "lib/arraylist.h"

// Forward declarations
struct FontFaceDescriptor;
typedef struct FontFaceDescriptor FontFaceDescriptor;

#ifdef __cplusplus
extern "C" {
#endif
#include "../lib/log.h"
#include "../lib/mempool.h"
#ifdef __cplusplus
}
#endif
#include "event.hpp"
#include "flex.hpp"

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

// Additional CSS constants for flex layout
#define LXB_CSS_VALUE_SPACE_EVENLY      (LXB_CSS_VALUE__LAST_ENTRY + 28)
#define LXB_CSS_VALUE_AUTO              (LXB_CSS_VALUE__LAST_ENTRY + 29)

// Additional CSS constants for grid layout
#define LXB_CSS_VALUE_GRID              (LXB_CSS_VALUE__LAST_ENTRY + 30)
#define LXB_CSS_VALUE_INLINE_GRID       (LXB_CSS_VALUE__LAST_ENTRY + 31)
#define LXB_CSS_VALUE_MIN_CONTENT       (LXB_CSS_VALUE__LAST_ENTRY + 32)
#define LXB_CSS_VALUE_MAX_CONTENT       (LXB_CSS_VALUE__LAST_ENTRY + 33)
#define LXB_CSS_VALUE_FIT_CONTENT       (LXB_CSS_VALUE__LAST_ENTRY + 34)
#define LXB_CSS_VALUE_FR                (LXB_CSS_VALUE__LAST_ENTRY + 35)
#define LXB_CSS_VALUE_DENSE             (LXB_CSS_VALUE__LAST_ENTRY + 36)

// Note: CSS positioning constants are already defined in Lexbor:
// LXB_CSS_VALUE_STATIC = 0x014d, LXB_CSS_VALUE_RELATIVE = 0x014e,
// LXB_CSS_VALUE_ABSOLUTE = 0x014f, LXB_CSS_VALUE_FIXED = 0x0151, LXB_CSS_VALUE_STICKY = 0x0150


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
    // Table-related view types (Phase 1 scaffolding)
    RDT_VIEW_TABLE,
    RDT_VIEW_TABLE_ROW_GROUP,
    RDT_VIEW_TABLE_ROW,
    RDT_VIEW_TABLE_CELL,
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
    PropValue position;        // static, relative, absolute, fixed, sticky
    int top, right, bottom, left;  // offset values in pixels
    int z_index;              // stacking order
    bool has_top, has_right, has_bottom, has_left;  // which offsets are set
    PropValue clear;          // clear property for floats
    PropValue float_prop;     // float property (left, right, none)
} PositionProp;

typedef struct {
    PropValue text_align;
    int line_height;  // non-negative
    int text_indent;  // can be negative
    int min_width, max_width;  // non-negative
    int min_height, max_height;  // non-negative
    PropValue list_style_type;
    PropValue box_sizing;  // LXB_CSS_VALUE_CONTENT_BOX or LXB_CSS_VALUE_BORDER_BOX
    int given_width, given_height;  // CSS specified width/height values
    PropValue clear;          // clear property for floats
    PropValue float_prop;     // float property (left, right, none)
} BlockProp;

typedef struct View View;
typedef struct ViewGroup ViewGroup;

// view always has x, y, wd, hg; otherwise, it is a property group
struct View {
    ViewType type;
    DomNode *node;  // DOM node abstraction instead of direct lexbor dependency
    View* next;
    ViewGroup* parent;  // corrected the type to ViewGroup
    int x, y, width, height;  // x, y relative to the parent block, width, height forms the BORDER box of the block
};

typedef struct ViewText : View {
    int start_index, length;  // start and length of the text in the style node
} ViewText;

struct ViewGroup : View {
    View* child;  // first child view
    DisplayValue display;
};

typedef struct ViewSpan : ViewGroup {
    FontProp* font;  // font style
    BoundaryProp* bound;  // block boundary properties
    InlineProp* in_line;  // inline specific style properties
    // Integrated flex item properties (no separate allocation)
    float flex_grow;
    float flex_shrink;
    int flex_basis;  // -1 for auto
    int align_self;  // AlignType or LXB_CSS_VALUE_*
    int order;
    bool flex_basis_is_percent;

    // Additional flex item properties from old implementation
    float aspect_ratio;
    int baseline_offset;

    // Auto margin flags
    bool margin_top_auto;
    bool margin_right_auto;
    bool margin_bottom_auto;
    bool margin_left_auto;

    // Percentage flags for constraints
    bool width_is_percent;
    bool height_is_percent;
    bool min_width_is_percent;
    bool max_width_is_percent;
    bool min_height_is_percent;
    bool max_height_is_percent;

    // Min/max constraints
    int min_width, max_width;
    int min_height, max_height;

    // Position and visibility (from old FlexItem)
    int position;  // PositionType
    int visibility;  // Visibility

    // Grid item properties (following flex pattern)
    int grid_row_start;          // Grid row start line
    int grid_row_end;            // Grid row end line
    int grid_column_start;       // Grid column start line
    int grid_column_end;         // Grid column end line
    char* grid_area;             // Named grid area
    int justify_self;            // Item-specific justify alignment (LXB_CSS_VALUE_*)
    int align_self_grid;         // Item-specific align alignment for grid (LXB_CSS_VALUE_*)

    // Grid item computed properties
    int computed_grid_row_start;
    int computed_grid_row_end;
    int computed_grid_column_start;
    int computed_grid_column_end;

    // Grid item flags
    bool has_explicit_grid_row_start;
    bool has_explicit_grid_row_end;
    bool has_explicit_grid_column_start;
    bool has_explicit_grid_column_end;
    bool is_grid_auto_placed;
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

// Integrated flex container layout state
typedef struct {
    // CSS properties (using int to allow both enum and Lexbor constants)
    int direction;      // FlexDirection or LXB_CSS_VALUE_*
    int wrap;           // FlexWrap or LXB_CSS_VALUE_*
    int justify;        // JustifyContent or LXB_CSS_VALUE_*
    int align_items;    // AlignType or LXB_CSS_VALUE_*
    int align_content;  // AlignType or LXB_CSS_VALUE_*
    int row_gap;
    int column_gap;
    WritingMode writing_mode;
    TextDirection text_direction;

    // Layout state (computed during layout)
    struct ViewBlock** flex_items;  // Array of child flex items
    int item_count;
    int allocated_items;  // For dynamic array growth

    // Line information
    struct FlexLineInfo* lines;
    int line_count;
    int allocated_lines;

    // Cached calculations
    int main_axis_size;
    int cross_axis_size;
    bool needs_reflow;
} FlexContainerLayout;

typedef struct {
    ImageSurface* img;  // image surface
    Document* doc;  // iframe document
    FlexContainerLayout* flex_container; // integrated flex container layout
    struct GridContainerLayout* grid_container; // integrated grid container layout
} EmbedProp;

typedef struct ViewBlock : ViewSpan {
    int content_width, content_height;  // width and height of the child content including padding
    BlockProp* blk;  // block specific style properties
    ScrollProp* scroller;  // handles overflow
    // block content related properties for flexbox, image, iframe
    EmbedProp* embed;
    // positioning properties for CSS positioning
    PositionProp* position;

    // Child navigation for flex layout tests
    struct ViewBlock* first_child;
    struct ViewBlock* last_child;
    struct ViewBlock* next_sibling;
    struct ViewBlock* prev_sibling;
} ViewBlock;

// Table-specific lightweight subclasses (no additional fields yet)
// These keep table concerns out of the base ViewBlock while preserving layout/render compatibility.
typedef struct ViewTable : ViewBlock {
    // Table layout algorithm mode
    enum {
        TABLE_LAYOUT_AUTO = 0,    // Content-based width calculation (default)
        TABLE_LAYOUT_FIXED = 1    // Fixed width calculation based on first row/col elements
    } table_layout;

    // Border model and spacing (Phase 4)
    // border_collapse=false => separate borders, apply border-spacing gaps
    // border_collapse=true  => collapsed borders, no gaps between cells
    bool border_collapse;
    int border_spacing_h; // horizontal spacing between columns (px)
    int border_spacing_v; // vertical spacing between rows (px)

    // Table-specific state will be held externally (e.g., TableModel) and referenced by ViewTable later.
} ViewTable;

typedef struct ViewTableRowGroup : ViewBlock {
    // Minimal metadata may be added later (e.g., group kind: thead/tbody/tfoot)
} ViewTableRowGroup;

typedef struct ViewTableRow : ViewBlock {
    // Minimal metadata may be added later (e.g., computed baseline)
} ViewTableRow;

typedef struct ViewTableCell : ViewBlock {
    // Cell spanning metadata
    int col_span;  // Number of columns this cell spans (default: 1)
    int row_span;  // Number of rows this cell spans (default: 1)
    int col_index; // Starting column index (computed during layout)
    int row_index; // Starting row index (computed during layout)

    // Vertical alignment
    enum {
        CELL_VALIGN_TOP = 0,
        CELL_VALIGN_MIDDLE = 1,
        CELL_VALIGN_BOTTOM = 2,
        CELL_VALIGN_BASELINE = 3
    } vertical_align;
} ViewTableCell;

struct ViewTree {
    Pool *pool;
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

    // @font-face support
    FontFaceDescriptor** font_faces;    // Array of @font-face declarations
    int font_face_count;
    int font_face_capacity;

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
