#pragma once
#include "../lib/log.h"
#include "../lib/hashmap.h"
#include "../lib/arraylist.h"
#include "../lib/utf.h"
#include "../lib/url.h"
#include "../lib/mempool.h"
#include "../lambda/lambda-data.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_value.hpp"

#include <GLFW/glfw3.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H
#include <thorvg_capi.h>

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

// Forward declarations
struct FontFaceDescriptor;
typedef struct FontFaceDescriptor FontFaceDescriptor;
struct FontDatabase;
typedef struct FontDatabase FontDatabase;

// Define lexbor tag and CSS value constants first, before including headers that need them
enum {
    HTM_TAG__UNDEF,
    HTM_TAG__TEXT,          // text node
    HTM_TAG__EM_COMMENT,    // for HTML comments
    HTM_TAG__EM_DOCTYPE,    // for DOCTYPE declaration
    HTM_TAG_A,
    HTM_TAG_ABBR,
    HTM_TAG_ACRONYM,
    HTM_TAG_ADDRESS,
    HTM_TAG_ALTGLYPH,
    HTM_TAG_ALTGLYPHDEF,
    HTM_TAG_ALTGLYPHITEM,
    HTM_TAG_ANIMATECOLOR,
    HTM_TAG_ANIMATEMOTION,
    HTM_TAG_ANIMATETRANSFORM,
    HTM_TAG_ANNOTATION_XML,
    HTM_TAG_APPLET,
    HTM_TAG_AREA,
    HTM_TAG_ARTICLE,
    HTM_TAG_ASIDE,
    HTM_TAG_AUDIO,
    HTM_TAG_B,
    HTM_TAG_BASE,
    HTM_TAG_BASEFONT,
    HTM_TAG_BDI,
    HTM_TAG_BDO,
    HTM_TAG_BGSOUND,
    HTM_TAG_BIG,
    HTM_TAG_BLINK,
    HTM_TAG_BLOCKQUOTE,
    HTM_TAG_BODY,
    HTM_TAG_BR,
    HTM_TAG_BUTTON,
    HTM_TAG_CANVAS,
    HTM_TAG_CAPTION,
    HTM_TAG_CENTER,
    HTM_TAG_CITE,
    HTM_TAG_CLIPPATH,
    HTM_TAG_CODE,
    HTM_TAG_COL,
    HTM_TAG_COLGROUP,
    HTM_TAG_DATA,
    HTM_TAG_DATALIST,
    HTM_TAG_DD,
    HTM_TAG_DEL,
    HTM_TAG_DESC,
    HTM_TAG_DETAILS,
    HTM_TAG_DFN,
    HTM_TAG_DIALOG,
    HTM_TAG_DIR,
    HTM_TAG_DIV,
    HTM_TAG_DL,
    HTM_TAG_DT,
    HTM_TAG_EM,
    HTM_TAG_EMBED,
    HTM_TAG_FEBLEND,
    HTM_TAG_FECOLORMATRIX,
    HTM_TAG_FECOMPONENTTRANSFER,
    HTM_TAG_FECOMPOSITE,
    HTM_TAG_FECONVOLVEMATRIX,
    HTM_TAG_FEDIFFUSELIGHTING,
    HTM_TAG_FEDISPLACEMENTMAP,
    HTM_TAG_FEDISTANTLIGHT,
    HTM_TAG_FEDROPSHADOW,
    HTM_TAG_FEFLOOD,
    HTM_TAG_FEFUNCA,
    HTM_TAG_FEFUNCB,
    HTM_TAG_FEFUNCG,
    HTM_TAG_FEFUNCR,
    HTM_TAG_FEGAUSSIANBLUR,
    HTM_TAG_FEIMAGE,
    HTM_TAG_FEMERGE,
    HTM_TAG_FEMERGENODE,
    HTM_TAG_FEMORPHOLOGY,
    HTM_TAG_FEOFFSET,
    HTM_TAG_FEPOINTLIGHT,
    HTM_TAG_FESPECULARLIGHTING,
    HTM_TAG_FESPOTLIGHT,
    HTM_TAG_FETILE,
    HTM_TAG_FETURBULENCE,
    HTM_TAG_FIELDSET,
    HTM_TAG_FIGCAPTION,
    HTM_TAG_FIGURE,
    HTM_TAG_FONT,
    HTM_TAG_FOOTER,
    HTM_TAG_FOREIGNOBJECT,
    HTM_TAG_FORM,
    HTM_TAG_FRAME,
    HTM_TAG_FRAMESET,
    HTM_TAG_GLYPHREF,
    HTM_TAG_H1,
    HTM_TAG_H2,
    HTM_TAG_H3,
    HTM_TAG_H4,
    HTM_TAG_H5,
    HTM_TAG_H6,
    HTM_TAG_HEAD,
    HTM_TAG_HEADER,
    HTM_TAG_HGROUP,
    HTM_TAG_HR,
    HTM_TAG_HTML,
    HTM_TAG_I,
    HTM_TAG_IFRAME,
    HTM_TAG_IMAGE,
    HTM_TAG_IMG,
    HTM_TAG_INPUT,
    HTM_TAG_INS,
    HTM_TAG_ISINDEX,
    HTM_TAG_KBD,
    HTM_TAG_KEYGEN,
    HTM_TAG_LABEL,
    HTM_TAG_LEGEND,
    HTM_TAG_LI,
    HTM_TAG_LINEARGRADIENT,
    HTM_TAG_LINK,
    HTM_TAG_LISTING,
    HTM_TAG_MAIN,
    HTM_TAG_MALIGNMARK,
    HTM_TAG_MAP,
    HTM_TAG_MARK,
    HTM_TAG_MARQUEE,
    HTM_TAG_MATH,
    HTM_TAG_MENU,
    HTM_TAG_META,
    HTM_TAG_METER,
    HTM_TAG_MFENCED,
    HTM_TAG_MGLYPH,
    HTM_TAG_MI,
    HTM_TAG_MN,
    HTM_TAG_MO,
    HTM_TAG_MS,
    HTM_TAG_MTEXT,
    HTM_TAG_MULTICOL,
    HTM_TAG_NAV,
    HTM_TAG_NEXTID,
    HTM_TAG_NOBR,
    HTM_TAG_NOEMBED,
    HTM_TAG_NOFRAMES,
    HTM_TAG_NOSCRIPT,
    HTM_TAG_OBJECT,
    HTM_TAG_OL,
    HTM_TAG_OPTGROUP,
    HTM_TAG_OPTION,
    HTM_TAG_OUTPUT,
    HTM_TAG_P,
    HTM_TAG_PARAM,
    HTM_TAG_PATH,
    HTM_TAG_PICTURE,
    HTM_TAG_PLAINTEXT,
    HTM_TAG_PRE,
    HTM_TAG_PROGRESS,
    HTM_TAG_Q,
    HTM_TAG_RADIALGRADIENT,
    HTM_TAG_RB,
    HTM_TAG_RP,
    HTM_TAG_RT,
    HTM_TAG_RTC,
    HTM_TAG_RUBY,
    HTM_TAG_S,
    HTM_TAG_SAMP,
    HTM_TAG_SCRIPT,
    HTM_TAG_SECTION,
    HTM_TAG_SELECT,
    HTM_TAG_SLOT,
    HTM_TAG_SMALL,
    HTM_TAG_SOURCE,
    HTM_TAG_SPACER,
    HTM_TAG_SPAN,
    HTM_TAG_STRIKE,
    HTM_TAG_STRONG,
    HTM_TAG_STYLE,
    HTM_TAG_SUB,
    HTM_TAG_SUMMARY,
    HTM_TAG_SUP,
    HTM_TAG_SVG,
    HTM_TAG_TABLE,
    HTM_TAG_TBODY,
    HTM_TAG_TD,
    HTM_TAG_TEMPLATE,
    HTM_TAG_TEXTAREA,
    HTM_TAG_TEXTPATH,
    HTM_TAG_TFOOT,
    HTM_TAG_TH,
    HTM_TAG_THEAD,
    HTM_TAG_TIME,
    HTM_TAG_TITLE,
    HTM_TAG_TR,
    HTM_TAG_TRACK,
    HTM_TAG_TT,
    HTM_TAG_U,
    HTM_TAG_UL,
    HTM_TAG_VAR,
    HTM_TAG_VIDEO,
    HTM_TAG_WBR,
    HTM_TAG_XMP,
    HTM_TAG__LAST_ENTRY         = 0x00c4
};

// radiant specific CSS display values
#define RDT_DISPLAY_TEXT                CSS_VALUE_TEXT
#define RDT_DISPLAY_REPLACED            CSS_VALUE__REPLACED

// Enum definitions needed by flex system
typedef enum { VIS_VISIBLE, VIS_HIDDEN, VIS_COLLAPSE } Visibility;
typedef enum { POS_STATIC, POS_ABSOLUTE } PositionType;
typedef enum { WM_HORIZONTAL_TB, WM_VERTICAL_RL, WM_VERTICAL_LR } WritingMode;
typedef enum { TD_LTR, TD_RTL } TextDirection;
typedef enum AlignType {
    ALIGN_AUTO = CSS_VALUE_AUTO,
    ALIGN_START = CSS_VALUE_FLEX_START,
    ALIGN_END = CSS_VALUE_FLEX_END,
    ALIGN_CENTER = CSS_VALUE_CENTER,
    ALIGN_BASELINE = CSS_VALUE_BASELINE,
    ALIGN_STRETCH = CSS_VALUE_STRETCH,
    ALIGN_SPACE_BETWEEN = CSS_VALUE_SPACE_BETWEEN,
    ALIGN_SPACE_AROUND = CSS_VALUE_SPACE_AROUND,
    ALIGN_SPACE_EVENLY = CSS_VALUE_SPACE_EVENLY
} AlignType;

// static inline float pack_as_nan(int value) {
//     uint32_t bits = 0x7FC00000u | ((uint32_t)value & 0x003FFFFF);       // quiet NaN + payload
//     float f;
//     memcpy(&f, &bits, sizeof(f));                           // avoid aliasing UB
//     return f;
// }

// CSS auto packed as special NaN float value
// inline const float LENGTH_AUTO = pack_as_nan(CSS_VALUE_AUTO);

// inline bool is_length_auto(float a) {
//     uint32_t ia;
//     memcpy(&ia, &a, sizeof(a));
//     return (ia & 0x003FFFFF) == CSS_VALUE_AUTO;
// }

typedef struct Rect {
    float x, y;
    float width, height;
} Rect;

typedef struct Bound {
    float left, top, right, bottom;
} Bound;

typedef enum {
    IMAGE_FORMAT_UNKNOWN = 0,
    IMAGE_FORMAT_SVG,
    IMAGE_FORMAT_PNG,
    IMAGE_FORMAT_JPEG,
    IMAGE_FORMAT_GIF,
} ImageFormat;

typedef enum {
    SCALE_MODE_NEAREST = 0,  // Nearest neighbor (fast, pixelated)
    SCALE_MODE_LINEAR,       // Bilinear interpolation (smooth)
} ScaleMode;

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
    Url* url;        // the resolved absolute URL of the image
} ImageSurface;

extern ImageSurface* image_surface_create(int pixel_width, int pixel_height);
extern ImageSurface* image_surface_create_from(int pixel_width, int pixel_height, void* pixels);
extern void image_surface_destroy(ImageSurface* img_surface);
extern void fill_surface_rect(ImageSurface* surface, Rect* rect, uint32_t color, Bound* clip);
extern void blit_surface_scaled(ImageSurface* src, Rect* src_rect, ImageSurface* dst, Rect* dst_rect, Bound* clip, ScaleMode scale_mode);

extern bool can_break(char c);
extern bool is_space(char c);

typedef struct ViewBlock ViewBlock;

struct FontProp {
    char* family;  // font family name
    float font_size;  // font size in pixels, scaled by pixel_ratio
    CssEnum font_style;
    CssEnum font_weight;
    CssEnum text_deco; // CSS text decoration
    float letter_spacing;  // letter spacing in pixels (default 0)
    // derived font properties
    float space_width;  // width of a space character of the current font
    float ascender;    // font ascender in pixels
    float descender;   // font descender in pixels
    float font_height; // font height in pixels
    bool has_kerning;  // whether the font has kerning
};

struct GridItemProp {
    // Grid item properties (following flex pattern)
    int grid_row_start;          // Grid row start line
    int grid_row_end;            // Grid row end line
    int grid_column_start;       // Grid column start line
    int grid_column_end;         // Grid column end line
    char* grid_area;             // Named grid area
    int justify_self;            // Item-specific justify alignment (CSS_VALUE_*)
    int align_self_grid;         // Item-specific align alignment for grid (CSS_VALUE_*)
    int order;                   // CSS order property (affects placement order)

    // Grid item computed properties
    int computed_grid_row_start;
    int computed_grid_row_end;
    int computed_grid_column_start;
    int computed_grid_column_end;

    // Track area dimensions (computed during positioning phase, used for alignment)
    int track_area_width;        // Width of the track area this item spans
    int track_area_height;       // Height of the track area this item spans

    // Grid item flags
    bool has_explicit_grid_row_start;
    bool has_explicit_grid_row_end;
    bool has_explicit_grid_column_start;
    bool has_explicit_grid_column_end;
    bool is_grid_auto_placed;
    bool grid_row_start_is_span;     // True if grid_row_start negative value means "span N", not "negative line"
    bool grid_row_end_is_span;       // True if grid_row_end negative value means "span N", not "negative line"
    bool grid_column_start_is_span;  // True if grid_column_start negative value means "span N", not "negative line"
    bool grid_column_end_is_span;    // True if grid_column_end negative value means "span N", not "negative line"

    // Measured dimensions (from multipass measurement phase)
    float measured_width;          // Content-based width (from measure pass)
    float measured_height;         // Content-based height (from measure pass)
    float measured_min_width;      // Minimum content width (longest word)
    float measured_max_width;      // Maximum content width (no wrapping)
    float measured_min_height;     // Minimum content height
    float measured_max_height;     // Maximum content height
    bool has_measured_size;      // Whether measurement pass has been done
};

// Intrinsic size type (shared by flex and grid layout)
typedef struct {
    float min_content;  // Minimum content width (longest word/element)
    float max_content;  // Maximum content width (no wrapping)
} IntrinsicSizes;

// FlexItemProp definition (needed by flex.hpp)
typedef struct FlexItemProp {
    float flex_basis;  // -1 for auto
    float flex_grow;
    float flex_shrink;
    CssEnum align_self;  // AlignType
    int order;
    float aspect_ratio;
    float baseline_offset;

    // Intrinsic sizing cache (computed during measurement phase)
    IntrinsicSizes intrinsic_width;   // min_content and max_content widths
    IntrinsicSizes intrinsic_height;  // min_content and max_content heights

    // Resolved constraints (computed from BlockProp given_min/max values)
    float resolved_min_width;    // Resolved min-width (including auto = min-content)
    float resolved_max_width;    // Resolved max-width (FLT_MAX if none)
    float resolved_min_height;   // Resolved min-height (including auto = min-content)
    float resolved_max_height;   // Resolved max-height (FLT_MAX if none)

    // Hypothetical sizes (computed during flex layout Phase 4.5)
    // These are the item's cross-axis sizes before alignment stretching
    float hypothetical_cross_size;        // Inner cross size (content box)
    float hypothetical_outer_cross_size;  // Outer cross size (with margins)

    // Flags for percentage values and measurement state
    int flex_basis_is_percent : 1;
    int is_margin_top_auto : 1;
    int is_margin_right_auto : 1;
    int is_margin_bottom_auto : 1;
    int is_margin_left_auto : 1;
    int has_intrinsic_width : 1;   // True if intrinsic widths calculated
    int has_intrinsic_height : 1;  // True if intrinsic heights calculated
    int needs_measurement : 1;      // True if content needs measuring
    int has_explicit_width : 1;     // True if width explicitly set in CSS
    int has_explicit_height : 1;    // True if height explicitly set in CSS
} FlexItemProp;

struct InlineProp {
    CssEnum cursor;
    Color color;
    CssEnum vertical_align;
    float opacity;  // CSS opacity value (0.0 to 1.0)
    int visibility;  // Visibility
};

typedef struct Spacing {
    struct { float top, right, bottom, left; };  // for margin, padding, border
    int32_t top_specificity, right_specificity, bottom_specificity, left_specificity;
} Spacing;

typedef struct Margin : Spacing {
    CssEnum top_type, right_type, bottom_type, left_type;   // for CSS enum values, like 'auto'
} Margin;

typedef struct Corner {
    struct { float top_left, top_right, bottom_right, bottom_left; };  // for border radius
    int32_t tl_specificity, tr_specificity, br_specificity, bl_specificity;
} Corner;

typedef struct {
    Spacing width;
    CssEnum top_style, right_style, bottom_style, left_style;
    int32_t top_style_specificity, right_style_specificity, bottom_style_specificity, left_style_specificity;
    Color top_color, right_color, bottom_color, left_color;
    int32_t top_color_specificity, right_color_specificity, bottom_color_specificity, left_color_specificity;
    Corner radius;
} BorderProp;

// Gradient types for CSS background gradients
typedef enum {
    GRADIENT_NONE = 0,
    GRADIENT_LINEAR,
    GRADIENT_RADIAL,
    GRADIENT_CONIC
} GradientType;

// Color stop for gradients
typedef struct {
    Color color;
    float position;  // 0.0 to 1.0, or -1 for auto
} GradientStop;

// Linear gradient data
typedef struct {
    float angle;           // in degrees, 0 = to top, 90 = to right
    GradientStop* stops;   // array of color stops
    int stop_count;
} LinearGradient;

typedef struct {
    Color color; // background color
    char* image; // background image path
    char* repeat; // repeat behavior
    char* position; // positioning of background image
    // Gradient support
    GradientType gradient_type;
    LinearGradient* linear_gradient;
} BackgroundProp;

typedef struct BoundaryProp {
    Margin margin;
    Spacing padding;
    BorderProp* border;
    BackgroundProp* background;
} BoundaryProp;

typedef struct PositionProp {
    CssEnum position;     // static, relative, absolute, fixed, sticky
    float top, right, bottom, left;  // offset values in pixels
    int z_index;            // stacking order
    bool has_top, has_right, has_bottom, has_left;  // which offsets are set
    CssEnum clear;        // clear property for floats
    CssEnum float_prop;   // float property (left, right, none)
    ViewBlock* first_abs_child;   // first child absolute/fixed positioned view
    ViewBlock* last_abs_child;    // last child absolute/fixed positioned view
    ViewBlock* next_abs_sibling;    // next sibling absolute/fixed positioned view
} PositionProp;

/**
 * PseudoContentProp - Stores dynamically created ::before and ::after pseudo-elements
/*
 * Instead of storing content strings and layout bounds, we create actual DomElement
 * and DomText nodes for pseudo-elements. This allows reusing the existing layout
 * infrastructure for text and inline content.
 *
 * The pseudo DomElements are created during style cascade when 'content' property
 * is resolved, and laid out as part of normal block layout flow.
 */

// Content value types for pseudo-elements (CSS 2.1 Section 12.2)
enum ContentType {
    CONTENT_TYPE_NONE = 0,      // no content
    CONTENT_TYPE_STRING = 1,     // string literal
    CONTENT_TYPE_URI = 2,        // url()
    CONTENT_TYPE_COUNTER = 3,    // counter()
    CONTENT_TYPE_COUNTERS = 4,   // counters()
    CONTENT_TYPE_ATTR = 5,       // attr()
    CONTENT_TYPE_OPEN_QUOTE = 6,
    CONTENT_TYPE_CLOSE_QUOTE = 7
};

typedef struct PseudoContentProp {
    DomElement* before;    // ::before pseudo-element (NULL if none)
    DomElement* after;     // ::after pseudo-element (NULL if none)

    // Content value storage for generation
    char* before_content;         // Parsed content string/template (or counter name for counters)
    char* after_content;
    char* before_separator;       // Separator for counters() function
    char* after_separator;
    uint32_t before_counter_style;  // CSS enum value for counter style
    uint32_t after_counter_style;
    uint8_t before_content_type;  // ContentType enum
    uint8_t after_content_type;
    bool before_generated;         // True if before element created
    bool after_generated;          // True if after element created
} PseudoContentProp;

typedef struct BlockProp {
    CssEnum text_align;
    CssEnum text_transform;  // CSS_VALUE_NONE, CSS_VALUE_UPPERCASE, CSS_VALUE_LOWERCASE, CSS_VALUE_CAPITALIZE
    const CssValue* line_height;
    float text_indent;  // can be negative
    float given_min_width, given_max_width;  // non-negative
    float given_min_height, given_max_height;  // non-negative
    CssEnum list_style_type;
    CssEnum list_style_position;  // inside, outside
    char* list_style_image;         // URL or none
    char* counter_reset;            // counter names and values
    char* counter_increment;        // counter names and values
    CssEnum box_sizing;  // CSS_VALUE_CONTENT_BOX or CSS_VALUE_BORDER_BOX
    CssEnum white_space;  // CSS_VALUE_NORMAL, CSS_VALUE_NOWRAP, CSS_VALUE_PRE, etc.
    CssEnum word_break;   // CSS_VALUE_NORMAL, CSS_VALUE_BREAK_ALL, CSS_VALUE_KEEP_ALL
    float given_width, given_height;  // CSS specified width/height values
    CssEnum given_width_type;
    CssEnum given_height_type;
    float given_width_percent;  // Raw percentage if width: X% (NaN if not percentage)
    float given_height_percent; // Raw percentage if height: X% (NaN if not percentage)
} BlockProp;

typedef struct FontBox {
    FontProp *style;  // current font style
    FT_Face ft_face;  // FreeType font face
    int current_font_size;  // font size of current element
} FontBox;

typedef struct TextRect {
    float x, y, width, height;
    int start_index, length;  // start and length of the text in the style node
    TextRect* next;
} TextRect;

typedef struct ViewText : DomText {
    // TextRect *rect;  // first text rect
    // FontProp *font;  // font for this text
    // Color color;     // text color (for PDF text fill color)
} ViewText;

struct ViewElement : DomElement {
    // exclude those skipped text nodes
    View* first_placed_child() {
        View* child = (View*)first_child;
        while (child) {
            if (child->view_type) return child;
            child = (View*)child->next_sibling;
        }
        return nullptr;
    }

    // exclude those skipped text nodes
    View* last_placed_child() {
        View* last_placed = nullptr;
        View* child = (View*)first_child;
        while (child) {
            if (child->view_type) { last_placed = child; }
            child = (View*)child->next_sibling;
        }
        return last_placed;
    }
};

typedef ViewElement ViewSpan;

typedef struct {
    float v_scroll_position, h_scroll_position;
    float v_max_scroll, h_max_scroll;
    float v_handle_y, v_handle_height;
    float h_handle_x, h_handle_width;

    bool is_h_hovered, is_v_hovered;
    bool v_is_dragging, h_is_dragging;
    float drag_start_x, drag_start_y;
    float v_drag_start_scroll, h_drag_start_scroll;
    void reset();
} ScrollPane;

typedef struct ScrollProp {
    CssEnum overflow_x, overflow_y;
    ScrollPane* pane;
    bool has_hz_overflow, has_vt_overflow;
    bool has_hz_scroll, has_vt_scroll;

    Bound clip; // clipping rect, relative to the block border box
    bool has_clip;
} ScrollProp;

typedef struct FlexProp {
    // CSS properties (using int to allow both enum and Lexbor constants)
    int direction;      // FlexDirection or CSS_VALUE_*
    int wrap;           // FlexWrap or CSS_VALUE_*
    int justify;        // JustifyContent or CSS_VALUE_*
    int align_items;    // AlignType or CSS_VALUE_*
    int align_content;  // AlignType or CSS_VALUE_*
    float row_gap;
    float column_gap;
    bool row_gap_is_percent;      // true if row_gap is a percentage
    bool column_gap_is_percent;   // true if column_gap is a percentage
    WritingMode writing_mode;
    TextDirection text_direction;
    // First baseline of this flex container (computed after layout)
    // Used when this container participates in parent's baseline alignment
    int first_baseline;
    bool has_baseline_child;       // true if first line has baseline-aligned items
} FlexProp;

typedef struct GridTrackList GridTrackList;
typedef struct GridArea GridArea;
typedef struct GridProp {
    // Grid alignment properties (using Lexbor CSS constants)
    int justify_content;         // CSS_VALUE_START, etc.
    int align_content;           // CSS_VALUE_START, etc.
    int justify_items;           // CSS_VALUE_STRETCH, etc.
    int align_items;             // CSS_VALUE_STRETCH, etc.
    int grid_auto_flow;          // CSS_VALUE_ROW, CSS_VALUE_COLUMN
    // Grid gap properties
    float row_gap;
    float column_gap;

    // Grid template properties
    GridTrackList* grid_template_rows;
    GridTrackList* grid_template_columns;
    GridTrackList* grid_template_areas;

    // Grid auto track sizing
    GridTrackList* grid_auto_rows;
    GridTrackList* grid_auto_columns;

    int computed_row_count;
    int computed_column_count;
    // Grid areas
    GridArea* grid_areas;
    int area_count;
    int allocated_areas;

    // Advanced features
    bool is_dense_packing;       // grid-auto-flow: dense
} GridProp;

typedef struct EmbedProp {
    ImageSurface* img;  // image surface
    DomDocument* doc;   // iframe document
    FlexProp* flex;
    GridProp* grid;
} EmbedProp;

struct ViewBlock : ViewSpan {
    // float content_width, content_height;  // width and height of the child content including padding
    // BlockProp* blk;  // block specific style properties
    // ScrollProp* scroller;  // handles overflow
    // // block content related properties for flexbox, image, iframe
    // EmbedProp* embed;
    // // positioning properties for CSS positioning
    // PositionProp* position;
    // ViewBlock* last_child;
};

typedef struct TableProp {
    // Table layout algorithm mode
    enum {
        TABLE_LAYOUT_AUTO = 0,    // Content-based width calculation (default)
        TABLE_LAYOUT_FIXED = 1    // Fixed width calculation based on first row/col elements
    } table_layout;

    // Caption positioning
    enum {
        CAPTION_SIDE_TOP = 0,     // Caption appears above the table (default)
        CAPTION_SIDE_BOTTOM = 1   // Caption appears below the table
    } caption_side;

    // Empty cells display
    enum {
        EMPTY_CELLS_SHOW = 0,     // Show borders and backgrounds of empty cells (default)
        EMPTY_CELLS_HIDE = 1      // Hide borders and backgrounds of empty cells
    } empty_cells;

    // Border model and spacing
    float border_spacing_h; // horizontal spacing between columns (px)
    float border_spacing_v; // vertical spacing between rows (px)
    // Fixed layout height distribution
    int fixed_row_height;   // Height per row for table-layout:fixed with explicit height (0=auto)
    // border_collapse=false => separate borders, apply border-spacing gaps
    // border_collapse=true  => collapsed borders, no gaps between cells
    bool border_collapse;
    uint8_t is_annoy_tbody:1;    // whether this element is doubled as an anonymous tbody
    uint8_t is_annoy_tr:1;       // whether this element is doubled as an anonymous tr
    uint8_t is_annoy_td:1;       // whether this element is doubled as an anonymous td
    uint8_t is_annoy_colgroup:1; // whether this element is doubled as an anonymous colgroup

} TableProp;

// Table-specific lightweight subclasses (no additional fields yet)
// These keep table concerns out of the base ViewBlock while preserving layout/render compatibility.

// Forward declarations for table navigation
struct ViewTableRow;
struct ViewTableCell;
struct ViewTableRowGroup;

typedef struct ViewTable : ViewBlock {
    // Navigation helpers that respect anonymous box flags (CSS 2.1 Section 17.2.1)

    // Get first logical row (may be in a row group or directly under table if is_annoy_tbody)
    ViewTableRow* first_row();

    // Get first row group (may be the table itself if is_annoy_tbody)
    ViewBlock* first_row_group();

    // Iterate all rows across all row groups
    // Usage: for (auto row = table->first_row(); row; row = table->next_row(row))
    ViewTableRow* next_row(ViewTableRow* current);

    // Get first cell when table acts as its own row (is_annoy_tr)
    // Returns nullptr if table doesn't act as a row
    ViewTableCell* first_direct_cell();

    // Get next cell when table acts as its own row
    ViewTableCell* next_direct_cell(ViewTableCell* current);

    // Check if table acts as its own tbody
    inline bool acts_as_tbody() { return tb && tb->is_annoy_tbody; }

    // Check if table acts as its own row (cells are direct children)
    inline bool acts_as_row() { return tb && tb->is_annoy_tr; }
} ViewTable;

// CSS 2.1 Section 17.2: Row group types for visual ordering
enum TableSectionType {
    TABLE_SECTION_THEAD = 0,   // Header group - renders first
    TABLE_SECTION_TBODY = 1,   // Body group - renders in middle (default)
    TABLE_SECTION_TFOOT = 2    // Footer group - renders last
};

typedef struct ViewTableRowGroup : ViewBlock {
    // NOTE: Do NOT add fields here - views share memory with DomElement!
    // Section type is determined at runtime via get_section_type() method.

    // Get section type from tag/display for visual ordering
    TableSectionType get_section_type() const;

    // Get first row in this group
    ViewTableRow* first_row();

    // Get next row in this group
    ViewTableRow* next_row(ViewTableRow* current);
} ViewTableRowGroup;

typedef struct ViewTableRow : ViewBlock {
    // Minimal metadata may be added later (e.g., computed baseline)

    // Get first cell in this row
    ViewTableCell* first_cell();

    // Get next cell in this row
    ViewTableCell* next_cell(ViewTableCell* current);

    // Get parent row group (or table if row is direct child)
    ViewBlock* parent_row_group();
} ViewTableRow;

// Forward declaration for border-collapse support
struct CollapsedBorder;

struct TableCellProp {
    // Vertical alignment
    enum {
        CELL_VALIGN_TOP = 0,
        CELL_VALIGN_MIDDLE = 1,
        CELL_VALIGN_BOTTOM = 2,
        CELL_VALIGN_BASELINE = 3
    } vertical_align;

    // Cell spanning metadata
    int col_span;  // Number of columns this cell spans (default: 1)
    int row_span;  // Number of rows this cell spans (default: 1)
    int col_index; // Starting column index (computed during layout)
    int row_index; // Starting row index (computed during layout)
    uint8_t is_annoy_tr:1;       // whether this element is doubled as an anonymous tr
    uint8_t is_annoy_td:1;       // whether this element is doubled as an anonymous td
    uint8_t is_annoy_colgroup:1; // whether this element is doubled as an anonymous colgroup
    uint8_t is_empty:1;          // whether this cell has no content (for empty-cells: hide)
    uint8_t hide_empty:1;        // combined flag: is_empty && table has empty-cells: hide

    // Border-collapse resolved borders (CSS 2.1 §17.6.2)
    // Only populated when table has border-collapse: collapse
    // These store the winning borders after conflict resolution
    // Used during rendering phase to draw correct collapsed borders
    CollapsedBorder* top_resolved;
    CollapsedBorder* right_resolved;
    CollapsedBorder* bottom_resolved;
    CollapsedBorder* left_resolved;
};

typedef struct ViewTableCell : ViewBlock {
} ViewTableCell;

typedef enum HtmlVersion {
    HTML5 = 1,              // HTML5
    HTML4_01_STRICT,        // HTML4.01 Strict
    HTML4_01_TRANSITIONAL,  // HTML4.01 Transitional
    HTML4_01_FRAMESET,      // HTML4.01 Frameset
    HTML_QUIRKS,            // Legacy HTML or missing DOCTYPE
    HTML1_0,                // HTML 1.0 (1991) - uses <HEADER> as head, <NEXTID> void element
} HtmlVersion;

struct ViewTree {
    Pool *pool;
    View* root;
    HtmlVersion html_version;
};

typedef struct CursorState {
    View* view;
    float x, y;
} CursorState;

typedef struct CaretState {
    View* view;
    float x_offset;
} CaretState;

typedef struct StateStore {
    CaretState* caret;
    CursorState* cursor;
    bool is_dirty;
    bool is_dragging;
    View* drag_target;
} StateStore;

// rendering context structs
typedef struct {
    float x, y;  // abs x, y relative to entire canvas/screen
    Bound clip;  // clipping rect
    Corner clip_radius;  // rounded corner clipping (for overflow:hidden with border-radius)
    bool has_clip_radius;  // true if clip_radius should be applied
} BlockBlot;

typedef struct {
    CssEnum list_style_type;
    int item_index;
} ListBlot;

// Now include headers that depend on these constants
#include "event.hpp"

typedef struct {
    GLFWwindow *window;    // current window
    float window_width;    // window pixel width
    float window_height;   // window pixel height
    ImageSurface* surface;  // rendering surface of a window

    // font handling
    FontDatabase *font_db;
    FT_Library ft_library;
    struct hashmap* fontface_map;  // cache of font faces loaded
    FontProp default_font;  // default font style for HTML5
    FontProp legacy_default_font;  // default font style for legacy HTML before HTML5
    char** fallback_fonts;  // fallback fonts

    // @font-face support
    FontFaceDescriptor** font_faces;    // Array of @font-face declarations
    int font_face_count;
    int font_face_capacity;

    // image cache
    struct hashmap* image_cache;  // cache for images loaded

    float pixel_ratio;      // actual vs. logical pixel ratio, could be 1.0, 1.5, 2.0, etc.
    DomDocument* document;  // current document
    MouseState mouse_state; // current mouse state
} UiContext;

extern FT_Face load_styled_font(UiContext* uicon, const char* font_name, FontProp* font_style);
extern FT_GlyphSlot load_glyph(UiContext* uicon, FT_Face face, FontProp* font_style, uint32_t codepoint, bool for_rendering);
extern void setup_font(UiContext* uicon, FontBox *fbox, FontProp *fprop);
extern ImageSurface* load_image(UiContext* uicon, const char *file_path);

typedef struct DomDocument DomDocument;  // Forward declaration for Lambda CSS DOM Document
DomDocument* load_html_doc(Url *base, char* doc_filename, int viewport_width, int viewport_height);
DomDocument* load_markdown_doc(Url* markdown_url, int viewport_width, int viewport_height, Pool* pool);
void free_document(DomDocument* doc);
