#ifndef CSS_STYLE_H
#define CSS_STYLE_H

#include "../../../lib/mempool.h"
#include "../../../lib/log.h"
#include "../../../lib/avl_tree.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../../../lib/strview.h"
#include "css_value.hpp"
#include "../../core/name_identity.h"

// Forward declarations
typedef struct CssSelectorComponent CssSelectorComponent;
typedef struct CssCalcNode CssCalcNode;
typedef struct CssValue CssValue;
struct DomElement;
struct CssStylesheet;

/**
 * CSS Style System
 *
 * This file contains the final CSS style types, properties, and values
 * that are used after parsing is complete. These are the types needed
 * for the final styling and rendering system.
 */

// ============================================================================
// CSS Units and Basic Value Types
// ============================================================================

typedef enum CssUnit {
    CSS_UNIT_NONE,            // no unit (for numbers)

    // Length units (absolute)
    CSS_UNIT_PX,              // pixels
    CSS_UNIT_CM,              // centimeters
    CSS_UNIT_MM,              // millimeters
    CSS_UNIT_IN,              // inches
    CSS_UNIT_PT,              // points (1/72 inch)
    CSS_UNIT_PC,              // picas (12 points)
    CSS_UNIT_Q,               // quarter-millimeters

    // Length units (relative)
    CSS_UNIT_EM,              // relative to font size of element
    CSS_UNIT_EX,              // relative to x-height of font
    CSS_UNIT_CAP,             // relative to cap height of font
    CSS_UNIT_CH,              // relative to width of "0" character
    CSS_UNIT_IC,              // relative to width of ideographic character "水"
    CSS_UNIT_REM,             // relative to font size of root element
    CSS_UNIT_LH,              // relative to line height of element
    CSS_UNIT_RLH,             // relative to line height of root element
    CSS_UNIT_VW,              // viewport width percentage
    CSS_UNIT_VH,              // viewport height percentage
    CSS_UNIT_VI,              // viewport inline size percentage
    CSS_UNIT_VB,              // viewport block size percentage
    CSS_UNIT_VMIN,            // minimum of vw and vh
    CSS_UNIT_VMAX,            // maximum of vw and vh

    // Small, large, and dynamic viewport units
    CSS_UNIT_SVW,             // small viewport width
    CSS_UNIT_SVH,             // small viewport height
    CSS_UNIT_SVI,             // small viewport inline size
    CSS_UNIT_SVB,             // small viewport block size
    CSS_UNIT_SVMIN,           // smaller small viewport axis
    CSS_UNIT_SVMAX,           // larger small viewport axis
    CSS_UNIT_LVW,             // large viewport width
    CSS_UNIT_LVH,             // large viewport height
    CSS_UNIT_LVI,             // large viewport inline size
    CSS_UNIT_LVB,             // large viewport block size
    CSS_UNIT_LVMIN,           // smaller large viewport axis
    CSS_UNIT_LVMAX,           // larger large viewport axis
    CSS_UNIT_DVW,             // dynamic viewport width
    CSS_UNIT_DVH,             // dynamic viewport height
    CSS_UNIT_DVI,             // dynamic viewport inline size
    CSS_UNIT_DVB,             // dynamic viewport block size
    CSS_UNIT_DVMIN,           // smaller dynamic viewport axis
    CSS_UNIT_DVMAX,           // larger dynamic viewport axis

    // Container query units
    CSS_UNIT_CQW,             // container query width
    CSS_UNIT_CQH,             // container query height
    CSS_UNIT_CQI,             // container query inline size
    CSS_UNIT_CQB,             // container query block size
    CSS_UNIT_CQMIN,           // container query minimum
    CSS_UNIT_CQMAX,           // container query maximum

    // Angle units
    CSS_UNIT_DEG,             // degrees
    CSS_UNIT_GRAD,            // gradians
    CSS_UNIT_RAD,             // radians
    CSS_UNIT_TURN,            // turns

    // Time units
    CSS_UNIT_S,               // seconds
    CSS_UNIT_MS,              // milliseconds

    // Frequency units
    CSS_UNIT_HZ,              // hertz
    CSS_UNIT_KHZ,             // kilohertz

    // Resolution units
    CSS_UNIT_DPI,             // dots per inch
    CSS_UNIT_DPCM,            // dots per centimeter
    CSS_UNIT_DPPX,            // dots per pixel

    // Flex units
    CSS_UNIT_FR,              // flexible length for grid layouts

    // Percentage and numbers
    CSS_UNIT_PERCENT,         // percentage
    CSS_UNIT_NUMBER,          // unitless number

    // Special values
    CSS_UNIT_AUTO,            // auto keyword
    CSS_UNIT_INHERIT,         // inherit keyword
    CSS_UNIT_INITIAL,         // initial keyword
    CSS_UNIT_UNSET,           // unset keyword
    CSS_UNIT_REVERT,          // revert keyword
    CSS_UNIT_REVERT_LAYER,    // revert-layer keyword

    CSS_UNIT_UNKNOWN          // unknown or invalid unit
} CssUnit;

// CSS Values §5.4 absolute lengths use the reference-pixel conversion shared
// by cascade consumers that cannot depend on a layout context.
bool css_absolute_length_to_px(CssUnit unit, double value, double* pixels);
bool css_dimension_to_canonical(CssUnit unit, double value, CssUnit* canonical_unit,
                                double* canonical_value);
bool css_viewport_length_to_px(CssUnit unit, double value, double width,
                               double height, bool vertical_inline_axis,
                               double* pixels);
const char* css_math_token_name(const CssValue* value);
bool css_value_contains_var_reference(const CssValue* value);
bool css_value_contains_pending_substitution(const CssValue* value);
const char* css_value_identifier_name(const CssValue* value);
bool css_value_is_global_keyword(const CssValue* value);

typedef enum CssMathType {
    CSS_MATH_INVALID, CSS_MATH_NUMBER, CSS_MATH_LENGTH, CSS_MATH_PERCENT,
    CSS_MATH_LENGTH_PERCENT, CSS_MATH_ANGLE, CSS_MATH_DEFERRED,
    CSS_MATH_TIME, CSS_MATH_RESOLUTION,
} CssMathType;
typedef bool (*CssMathLeafResolver)(void* context, const CssValue* value, double* result);
struct CssMathEvaluationContext {
    CssMathLeafResolver resolve_leaf;
    void* context;
    double line_width_step;
    bool preserve_percentages;
};
struct CssMathResult {
    CssMathType type;
    double value;
    double percentage;
    bool resolved;
};
CssMathResult css_math_evaluate(const CssValue* value,
    const CssMathEvaluationContext* context, int depth = 0);
CssMathType css_math_value_type(const CssValue* value, int depth = 0);

// CSS Fonts §2.5 maps absolute font-size keywords before layout applies
// inherited relative sizes such as larger and smaller.
float css_font_size_keyword_px(CssEnum keyword);

// CSS Color types
typedef enum CssColorType {
    CSS_COLOR_KEYWORD,        // named colors (red, blue, etc.)
    CSS_COLOR_HEX,            // #rgb, #rrggbb, #rgba, #rrggbbaa
    CSS_COLOR_RGB,            // rgb(), rgba()
    CSS_COLOR_HSL,            // hsl(), hsla()
    CSS_COLOR_HWB,            // hwb()
    CSS_COLOR_LAB,            // lab()
    CSS_COLOR_LCH,            // lch()
    CSS_COLOR_OKLAB,          // oklab()
    CSS_COLOR_OKLCH,          // oklch()
    CSS_COLOR_COLOR,          // color()
    CSS_COLOR_TRANSPARENT,    // transparent keyword
    CSS_COLOR_CURRENTCOLOR,   // currentColor keyword
    CSS_COLOR_CURRENT,        // alias for currentColor
    CSS_COLOR_SYSTEM          // system colors
} CssColorType;

// Computed components retain precision and missing channels until painting/interpolation.
// RGB components are normalized; HSL/HWB use degrees and unit fractions.
struct CssComputedColor {
    CssColorType type;
    double components[4];
    uint8_t missing;
    bool alpha_is_byte;
    CssEnum keyword;
    const char* color_space;
};
bool css_color_compute(const CssValue* value, CssComputedColor* result);
const CssValue* css_background_color_component(const CssValue* value);
bool css_color_to_rgba(const CssComputedColor* color, uint8_t* r, uint8_t* g,
                       uint8_t* b, uint8_t* a);
double css_color_legacy_alpha(uint8_t alpha);
CssValue* css_value_create_computed_color(Pool* pool, const CssComputedColor* color);

typedef struct CssLength {
    double value;
    CssUnit unit;
} CssLength;

typedef struct CssColor {
    uint8_t r, g, b, a;          // RGBA values
    CssColorType type;
    union {
        struct { double h, s, l; } hsl; // HSL values for HSL colors
        const char* keyword;            // Keyword name
    } data;
} CssColor;

typedef struct CssKeyword {
    const char* value;           // Keyword string
    int enum_value;              // Numeric value for known keywords
} CssKeyword;

typedef struct CssFunction {
    const char* name;            // Function name (calc, var, rgb, etc.)
    CssValue** args;             // Array of CssValue arguments
    int arg_count;               // Number of arguments
} CssFunction;

typedef enum TransformFunctionType {
    TRANSFORM_NONE = 0,
    // 2D Transforms
    TRANSFORM_TRANSLATE,        // translate(x, y) or translate(x)
    TRANSFORM_TRANSLATEX,       // translateX(x)
    TRANSFORM_TRANSLATEY,       // translateY(y)
    TRANSFORM_SCALE,            // scale(x, y) or scale(s)
    TRANSFORM_SCALEX,           // scaleX(x)
    TRANSFORM_SCALEY,           // scaleY(y)
    TRANSFORM_ROTATE,           // rotate(angle)
    TRANSFORM_SKEW,             // skew(x-angle, y-angle)
    TRANSFORM_SKEWX,            // skewX(angle)
    TRANSFORM_SKEWY,            // skewY(angle)
    TRANSFORM_MATRIX,           // matrix(a, b, c, d, e, f)
    // 3D Transforms
    TRANSFORM_TRANSLATE3D,      // translate3d(x, y, z)
    TRANSFORM_TRANSLATEZ,       // translateZ(z)
    TRANSFORM_SCALE3D,          // scale3d(x, y, z)
    TRANSFORM_SCALEZ,           // scaleZ(z)
    TRANSFORM_ROTATEX,          // rotateX(angle)
    TRANSFORM_ROTATEY,          // rotateY(angle)
    TRANSFORM_ROTATEZ,          // rotateZ(angle) - same as rotate()
    TRANSFORM_ROTATE3D,         // rotate3d(x, y, z, angle)
    TRANSFORM_PERSPECTIVE,      // perspective(d)
    TRANSFORM_MATRIX3D,         // matrix3d(16 values)
} TransformFunctionType;

// function metadata is shared by declaration validation and typed decoding.
typedef struct CssTransformFunctionInfo {
    const char* name;
    TransformFunctionType type;
    int min_args, max_args;
} CssTransformFunctionInfo;
const CssTransformFunctionInfo* css_transform_function_info(const char* name);

// CSS Value Types for final computed values
typedef enum CssValueType : uint8_t {
    CSS_VALUE_TYPE_KEYWORD,        // keywords (auto, inherit, etc.)
    CSS_VALUE_TYPE_LENGTH,         // length values with units
    CSS_VALUE_TYPE_PERCENTAGE,     // percentage values
    CSS_VALUE_TYPE_NUMBER,         // numeric values (includes integers via is_integer flag)
    CSS_VALUE_TYPE_COLOR,          // color values
    CSS_VALUE_TYPE_STRING,         // string values
    CSS_VALUE_TYPE_URL,            // URL values
    CSS_VALUE_TYPE_ANGLE,          // angle values
    CSS_VALUE_TYPE_TIME,           // time values
    CSS_VALUE_TYPE_FREQUENCY,      // frequency values
    CSS_VALUE_TYPE_LIST,           // list of values
    CSS_VALUE_TYPE_FUNCTION,       // function values
    CSS_VALUE_TYPE_VAR,            // var() function
    CSS_VALUE_TYPE_ENV,            // env() function
    CSS_VALUE_TYPE_ATTR,           // attr() function
    CSS_VALUE_TYPE_COLOR_MIX,      // color-mix() function
    CSS_VALUE_TYPE_CALC,           // calc() expressions
    CSS_VALUE_TYPE_CUSTOM,         // custom property references
    CSS_VALUE_TYPE_UNKNOWN         // unknown or invalid value
} CssValueType;

// ============================================================================
// CSS Property IDs (comprehensive CSS specification)
// ============================================================================

typedef enum CssPropertyCode {
    // Layout Properties
    CSS_PROPERTY_DISPLAY = 1,
    CSS_PROPERTY_POSITION,
    CSS_PROPERTY_TOP,
    CSS_PROPERTY_RIGHT,
    CSS_PROPERTY_BOTTOM,
    CSS_PROPERTY_LEFT,
    CSS_PROPERTY_Z_INDEX,
    CSS_PROPERTY_FLOAT,
    CSS_PROPERTY_CLEAR,
    CSS_PROPERTY_OVERFLOW,
    CSS_PROPERTY_OVERFLOW_X,
    CSS_PROPERTY_OVERFLOW_Y,
    CSS_PROPERTY_CLIP,
    CSS_PROPERTY_CLIP_PATH,
    CSS_PROPERTY_VISIBILITY,
    CSS_PROPERTY_ZOOM,

    // Box Model Properties
    CSS_PROPERTY_WIDTH,
    CSS_PROPERTY_HEIGHT,
    CSS_PROPERTY_MIN_WIDTH,
    CSS_PROPERTY_MIN_HEIGHT,
    CSS_PROPERTY_MAX_WIDTH,
    CSS_PROPERTY_MAX_HEIGHT,
    CSS_PROPERTY_BOX_SIZING,
    CSS_PROPERTY_BOX_DECORATION_BREAK,
    CSS_PROPERTY_ASPECT_RATIO,

    // Margin Properties
    CSS_PROPERTY_MARGIN,
    CSS_PROPERTY_MARGIN_TOP,
    CSS_PROPERTY_MARGIN_RIGHT,
    CSS_PROPERTY_MARGIN_BOTTOM,
    CSS_PROPERTY_MARGIN_LEFT,
    CSS_PROPERTY_MARGIN_BLOCK,
    CSS_PROPERTY_MARGIN_BLOCK_START,
    CSS_PROPERTY_MARGIN_BLOCK_END,
    CSS_PROPERTY_MARGIN_INLINE,
    CSS_PROPERTY_MARGIN_INLINE_START,
    CSS_PROPERTY_MARGIN_INLINE_END,
    CSS_PROPERTY_MARGIN_TRIM,

    // Padding Properties
    CSS_PROPERTY_PADDING,
    CSS_PROPERTY_PADDING_TOP,
    CSS_PROPERTY_PADDING_RIGHT,
    CSS_PROPERTY_PADDING_BOTTOM,
    CSS_PROPERTY_PADDING_LEFT,
    CSS_PROPERTY_PADDING_BLOCK,
    CSS_PROPERTY_PADDING_BLOCK_START,
    CSS_PROPERTY_PADDING_BLOCK_END,
    CSS_PROPERTY_PADDING_INLINE,
    CSS_PROPERTY_PADDING_INLINE_START,
    CSS_PROPERTY_PADDING_INLINE_END,

    // Border Properties
    CSS_PROPERTY_BORDER,
    CSS_PROPERTY_BORDER_WIDTH,
    CSS_PROPERTY_BORDER_STYLE,
    CSS_PROPERTY_BORDER_COLOR,
    CSS_PROPERTY_BORDER_TOP,
    CSS_PROPERTY_BORDER_RIGHT,
    CSS_PROPERTY_BORDER_BOTTOM,
    CSS_PROPERTY_BORDER_LEFT,

    // CSS Logical border properties (inline/block axis)
    CSS_PROPERTY_BORDER_INLINE,
    CSS_PROPERTY_BORDER_INLINE_WIDTH,
    CSS_PROPERTY_BORDER_INLINE_STYLE,
    CSS_PROPERTY_BORDER_INLINE_COLOR,
    CSS_PROPERTY_BORDER_INLINE_START,
    CSS_PROPERTY_BORDER_INLINE_START_WIDTH,
    CSS_PROPERTY_BORDER_INLINE_START_STYLE,
    CSS_PROPERTY_BORDER_INLINE_START_COLOR,
    CSS_PROPERTY_BORDER_INLINE_END,
    CSS_PROPERTY_BORDER_INLINE_END_WIDTH,
    CSS_PROPERTY_BORDER_INLINE_END_STYLE,
    CSS_PROPERTY_BORDER_INLINE_END_COLOR,
    CSS_PROPERTY_BORDER_BLOCK,
    CSS_PROPERTY_BORDER_BLOCK_STYLE,
    CSS_PROPERTY_BORDER_BLOCK_START,
    CSS_PROPERTY_BORDER_BLOCK_START_STYLE,
    CSS_PROPERTY_BORDER_BLOCK_END,
    CSS_PROPERTY_BORDER_BLOCK_END_STYLE,
    CSS_PROPERTY_BORDER_BLOCK_WIDTH,
    CSS_PROPERTY_BORDER_BLOCK_COLOR,
    CSS_PROPERTY_BORDER_BLOCK_START_WIDTH,
    CSS_PROPERTY_BORDER_BLOCK_START_COLOR,
    CSS_PROPERTY_BORDER_BLOCK_END_COLOR,
    CSS_PROPERTY_BORDER_BLOCK_END_WIDTH,

    // Individual border width properties
    CSS_PROPERTY_BORDER_TOP_WIDTH,
    CSS_PROPERTY_BORDER_RIGHT_WIDTH,
    CSS_PROPERTY_BORDER_BOTTOM_WIDTH,
    CSS_PROPERTY_BORDER_LEFT_WIDTH,

    // Individual border style properties
    CSS_PROPERTY_BORDER_TOP_STYLE,
    CSS_PROPERTY_BORDER_RIGHT_STYLE,
    CSS_PROPERTY_BORDER_BOTTOM_STYLE,
    CSS_PROPERTY_BORDER_LEFT_STYLE,

    // Individual border color properties
    CSS_PROPERTY_BORDER_TOP_COLOR,
    CSS_PROPERTY_BORDER_RIGHT_COLOR,
    CSS_PROPERTY_BORDER_BOTTOM_COLOR,
    CSS_PROPERTY_BORDER_LEFT_COLOR,

    CSS_PROPERTY_BORDER_RADIUS,
    CSS_PROPERTY_BORDER_TOP_LEFT_RADIUS,
    CSS_PROPERTY_BORDER_TOP_RIGHT_RADIUS,
    CSS_PROPERTY_BORDER_BOTTOM_RIGHT_RADIUS,
    CSS_PROPERTY_BORDER_BOTTOM_LEFT_RADIUS,
    CSS_PROPERTY_BORDER_START_START_RADIUS,
    CSS_PROPERTY_BORDER_START_END_RADIUS,
    CSS_PROPERTY_BORDER_END_START_RADIUS,
    CSS_PROPERTY_BORDER_END_END_RADIUS,

    // Background Properties
    CSS_PROPERTY_BACKGROUND,
    CSS_PROPERTY_BACKGROUND_COLOR,
    CSS_PROPERTY_BACKGROUND_IMAGE,
    CSS_PROPERTY_BACKGROUND_POSITION,
    CSS_PROPERTY_BACKGROUND_SIZE,
    CSS_PROPERTY_BACKGROUND_REPEAT,
    CSS_PROPERTY_BACKGROUND_ATTACHMENT,
    CSS_PROPERTY_BACKGROUND_ORIGIN,
    CSS_PROPERTY_BACKGROUND_CLIP,

    // Typography Properties
    CSS_PROPERTY_FONT,
    CSS_PROPERTY_FONT_FAMILY,
    CSS_PROPERTY_FONT_SIZE,
    CSS_PROPERTY_FONT_WEIGHT,
    CSS_PROPERTY_FONT_STYLE,
    CSS_PROPERTY_FONT_VARIANT,
    CSS_PROPERTY_FONT_STRETCH,
    CSS_PROPERTY_LINE_HEIGHT,
    CSS_PROPERTY_LETTER_SPACING,
    CSS_PROPERTY_WORD_SPACING,
    CSS_PROPERTY_TEXT_ALIGN,
    CSS_PROPERTY_VERTICAL_ALIGN,
    CSS_PROPERTY_TEXT_DECORATION,
    CSS_PROPERTY_TEXT_TRANSFORM,
    CSS_PROPERTY_TEXT_INDENT,
    CSS_PROPERTY_INITIAL_LETTER,
    CSS_PROPERTY_TEXT_WRAP,
    CSS_PROPERTY_TEXT_WRAP_MODE,
    CSS_PROPERTY_TEXT_WRAP_STYLE,
    CSS_PROPERTY_TEXT_SPACING_TRIM,
    CSS_PROPERTY_TEXT_AUTOSPACE,
    CSS_PROPERTY_TEXT_SHADOW,
    CSS_PROPERTY_WHITE_SPACE,
    CSS_PROPERTY_COLOR,
    CSS_PROPERTY_FILL,
    CSS_PROPERTY_OPACITY,
    CSS_PROPERTY_CURSOR,

    // Flexbox Properties
    CSS_PROPERTY_FLEX,
    CSS_PROPERTY_FLEX_DIRECTION,
    CSS_PROPERTY_FLEX_WRAP,
    CSS_PROPERTY_FLEX_FLOW,
    CSS_PROPERTY_JUSTIFY_CONTENT,
    CSS_PROPERTY_ALIGN_ITEMS,
    CSS_PROPERTY_ALIGN_CONTENT,
    CSS_PROPERTY_PLACE_CONTENT,
    CSS_PROPERTY_ALIGN_SELF,
    CSS_PROPERTY_FLEX_GROW,
    CSS_PROPERTY_FLEX_SHRINK,
    CSS_PROPERTY_FLEX_BASIS,
    CSS_PROPERTY_ORDER,

    // Grid Properties
    CSS_PROPERTY_GRID,
    CSS_PROPERTY_GRID_TEMPLATE,
    CSS_PROPERTY_GRID_TEMPLATE_ROWS,
    CSS_PROPERTY_GRID_TEMPLATE_COLUMNS,
    CSS_PROPERTY_GRID_TEMPLATE_AREAS,
    CSS_PROPERTY_GRID_AUTO_ROWS,
    CSS_PROPERTY_GRID_AUTO_COLUMNS,
    CSS_PROPERTY_GRID_AUTO_FLOW,
    CSS_PROPERTY_GRID_ROW,
    CSS_PROPERTY_GRID_COLUMN,
    CSS_PROPERTY_GRID_AREA,
    CSS_PROPERTY_GRID_ROW_START,
    CSS_PROPERTY_GRID_ROW_END,
    CSS_PROPERTY_GRID_COLUMN_START,
    CSS_PROPERTY_GRID_COLUMN_END,

    // Grid gap properties
    CSS_PROPERTY_GRID_ROW_GAP,
    CSS_PROPERTY_GRID_COLUMN_GAP,
    CSS_PROPERTY_GRID_GAP,
    CSS_PROPERTY_GAP,
    CSS_PROPERTY_ROW_GAP,
    CSS_PROPERTY_COLUMN_GAP,

    // Grid/Flex alignment properties
    CSS_PROPERTY_JUSTIFY_ITEMS,
    CSS_PROPERTY_JUSTIFY_SELF,
    CSS_PROPERTY_PLACE_ITEMS,
    CSS_PROPERTY_PLACE_SELF,

    // Transform Properties
    CSS_PROPERTY_TRANSFORM,
    CSS_PROPERTY_TRANSFORM_ORIGIN,
    CSS_PROPERTY_TRANSFORM_STYLE,
    CSS_PROPERTY_PERSPECTIVE,
    CSS_PROPERTY_PERSPECTIVE_ORIGIN,
    CSS_PROPERTY_BACKFACE_VISIBILITY,

    // Animation Properties
    CSS_PROPERTY_ANIMATION,
    CSS_PROPERTY_ANIMATION_NAME,
    CSS_PROPERTY_ANIMATION_DURATION,
    CSS_PROPERTY_ANIMATION_TIMING_FUNCTION,
    CSS_PROPERTY_ANIMATION_DELAY,
    CSS_PROPERTY_ANIMATION_ITERATION_COUNT,
    CSS_PROPERTY_ANIMATION_DIRECTION,
    CSS_PROPERTY_ANIMATION_FILL_MODE,
    CSS_PROPERTY_ANIMATION_PLAY_STATE,

    // Transition Properties
    CSS_PROPERTY_TRANSITION,
    CSS_PROPERTY_TRANSITION_PROPERTY,
    CSS_PROPERTY_TRANSITION_DURATION,
    CSS_PROPERTY_TRANSITION_TIMING_FUNCTION,
    CSS_PROPERTY_TRANSITION_DELAY,

    // Filter Properties
    CSS_PROPERTY_FILTER,
    CSS_PROPERTY_BACKDROP_FILTER,

    // Logical Properties
    CSS_PROPERTY_BLOCK_SIZE,
    CSS_PROPERTY_INLINE_SIZE,
    CSS_PROPERTY_MIN_BLOCK_SIZE,
    CSS_PROPERTY_MIN_INLINE_SIZE,
    CSS_PROPERTY_MAX_BLOCK_SIZE,
    CSS_PROPERTY_MAX_INLINE_SIZE,
    CSS_PROPERTY_INSET,
    CSS_PROPERTY_INSET_BLOCK,
    CSS_PROPERTY_INSET_BLOCK_START,
    CSS_PROPERTY_INSET_BLOCK_END,
    CSS_PROPERTY_INSET_INLINE,
    CSS_PROPERTY_INSET_INLINE_START,
    CSS_PROPERTY_INSET_INLINE_END,

    // Container Queries
    CSS_PROPERTY_CONTAINER,
    CSS_PROPERTY_CONTAINER_TYPE,
    CSS_PROPERTY_CONTAINER_NAME,
    CSS_PROPERTY_CONTAIN,
    CSS_PROPERTY_CONTAIN_INTRINSIC_WIDTH,
    CSS_PROPERTY_CONTAIN_INTRINSIC_HEIGHT,
    CSS_PROPERTY_CONTAIN_INTRINSIC_SIZE,
    CSS_PROPERTY_CONTAIN_INTRINSIC_INLINE_SIZE,
    CSS_PROPERTY_CONTAIN_INTRINSIC_BLOCK_SIZE,

    // CSS Nesting
    CSS_PROPERTY_NESTING,

    // Multi-column Layout Properties
    CSS_PROPERTY_COLUMN_WIDTH,
    CSS_PROPERTY_COLUMN_COUNT,
    CSS_PROPERTY_COLUMNS,
    CSS_PROPERTY_COLUMN_RULE,
    CSS_PROPERTY_COLUMN_RULE_WIDTH,
    CSS_PROPERTY_COLUMN_RULE_STYLE,
    CSS_PROPERTY_COLUMN_RULE_COLOR,
    CSS_PROPERTY_COLUMN_SPAN,
    CSS_PROPERTY_COLUMN_FILL,
    CSS_PROPERTY_COLUMN_HEIGHT,
    CSS_PROPERTY_COLUMN_WRAP,

    // Text Effects Properties
    CSS_PROPERTY_TEXT_DECORATION_LINE,
    CSS_PROPERTY_TEXT_DECORATION_STYLE,
    CSS_PROPERTY_TEXT_DECORATION_COLOR,
    CSS_PROPERTY_TEXT_DECORATION_THICKNESS,
    CSS_PROPERTY_TEXT_EMPHASIS,
    CSS_PROPERTY_TEXT_EMPHASIS_STYLE,
    CSS_PROPERTY_TEXT_EMPHASIS_COLOR,
    CSS_PROPERTY_TEXT_EMPHASIS_POSITION,
    CSS_PROPERTY_TEXT_OVERFLOW,
    CSS_PROPERTY_WORD_BREAK,
    CSS_PROPERTY_LINE_BREAK,
    CSS_PROPERTY_HYPHENS,
    CSS_PROPERTY_HYPHENATE_CHARACTER,
    CSS_PROPERTY_OVERFLOW_WRAP,
    CSS_PROPERTY_WORD_WRAP,
    CSS_PROPERTY_TAB_SIZE,
    CSS_PROPERTY_HANGING_PUNCTUATION,
    CSS_PROPERTY_TEXT_JUSTIFY,
    CSS_PROPERTY_TEXT_ALIGN_ALL,
    CSS_PROPERTY_TEXT_ALIGN_LAST,

    // List Properties
    CSS_PROPERTY_LIST_STYLE,
    CSS_PROPERTY_LIST_STYLE_TYPE,
    CSS_PROPERTY_LIST_STYLE_POSITION,
    CSS_PROPERTY_LIST_STYLE_IMAGE,

    // Table Properties
    CSS_PROPERTY_BORDER_COLLAPSE,
    CSS_PROPERTY_BORDER_SPACING,
    CSS_PROPERTY_CAPTION_SIDE,
    CSS_PROPERTY_EMPTY_CELLS,
    CSS_PROPERTY_TABLE_LAYOUT,

    // User Interface Properties
    CSS_PROPERTY_RESIZE,
    CSS_PROPERTY_ACCENT_COLOR,
    CSS_PROPERTY_CARET_COLOR,
    CSS_PROPERTY_CARET_SHAPE,
    CSS_PROPERTY_NAV_INDEX,
    CSS_PROPERTY_NAV_UP,
    CSS_PROPERTY_NAV_RIGHT,
    CSS_PROPERTY_NAV_DOWN,
    CSS_PROPERTY_NAV_LEFT,
    CSS_PROPERTY_APPEARANCE,
    CSS_PROPERTY_FIELD_SIZING,
    CSS_PROPERTY_USER_SELECT,

    // Paged Media Properties
    CSS_PROPERTY_PAGE_BREAK_BEFORE,
    CSS_PROPERTY_PAGE_BREAK_AFTER,
    CSS_PROPERTY_PAGE_BREAK_INSIDE,
    CSS_PROPERTY_ORPHANS,
    CSS_PROPERTY_WIDOWS,
    CSS_PROPERTY_BREAK_BEFORE,
    CSS_PROPERTY_BREAK_AFTER,
    CSS_PROPERTY_BREAK_INSIDE,

    // Generated Content Properties
    CSS_PROPERTY_CONTENT,
    CSS_PROPERTY_QUOTES,
    CSS_PROPERTY_COUNTER_RESET,
    CSS_PROPERTY_COUNTER_INCREMENT,
    CSS_PROPERTY_COUNTER_SET,
    CSS_PROPERTY_MARKER_OFFSET,

    // Miscellaneous Properties
    CSS_PROPERTY_ISOLATION,
    CSS_PROPERTY_MIX_BLEND_MODE,
    CSS_PROPERTY_OBJECT_FIT,
    CSS_PROPERTY_OBJECT_POSITION,
    CSS_PROPERTY_OBJECT_VIEW_BOX,
    CSS_PROPERTY_IMAGE_RENDERING,
    CSS_PROPERTY_IMAGE_ORIENTATION,
    CSS_PROPERTY_MASK_IMAGE,
    CSS_PROPERTY_MASK_TYPE,

    // Writing Modes Properties
    CSS_PROPERTY_DIRECTION,
    CSS_PROPERTY_UNICODE_BIDI,
    CSS_PROPERTY_WRITING_MODE,
    CSS_PROPERTY_TEXT_ORIENTATION,
    CSS_PROPERTY_TEXT_COMBINE_UPRIGHT,

    // Overflow Properties
    CSS_PROPERTY_OVERFLOW_BLOCK,
    CSS_PROPERTY_OVERFLOW_INLINE,
    CSS_PROPERTY_OVERFLOW_CLIP_MARGIN,
    CSS_PROPERTY_SCROLLBAR_GUTTER,

    // Pointer Events
    CSS_PROPERTY_POINTER_EVENTS,

    // Scrolling Properties
    CSS_PROPERTY_SCROLL_BEHAVIOR,
    CSS_PROPERTY_OVERSCROLL_BEHAVIOR,
    CSS_PROPERTY_OVERSCROLL_BEHAVIOR_X,
    CSS_PROPERTY_OVERSCROLL_BEHAVIOR_Y,
    CSS_PROPERTY_SCROLL_SNAP_TYPE,
    CSS_PROPERTY_SCROLL_SNAP_ALIGN,
    CSS_PROPERTY_SCROLL_MARGIN,
    CSS_PROPERTY_SCROLL_PADDING,
    CSS_PROPERTY_SCROLL_MARGIN_TOP,
    CSS_PROPERTY_SCROLL_MARGIN_RIGHT,
    CSS_PROPERTY_SCROLL_MARGIN_BOTTOM,
    CSS_PROPERTY_SCROLL_MARGIN_LEFT,
    CSS_PROPERTY_SCROLL_MARGIN_BLOCK,
    CSS_PROPERTY_SCROLL_MARGIN_BLOCK_START,
    CSS_PROPERTY_SCROLL_MARGIN_BLOCK_END,
    CSS_PROPERTY_SCROLL_MARGIN_INLINE,
    CSS_PROPERTY_SCROLL_MARGIN_INLINE_START,
    CSS_PROPERTY_SCROLL_MARGIN_INLINE_END,
    CSS_PROPERTY_SCROLL_PADDING_TOP,
    CSS_PROPERTY_SCROLL_PADDING_RIGHT,
    CSS_PROPERTY_SCROLL_PADDING_BOTTOM,
    CSS_PROPERTY_SCROLL_PADDING_LEFT,
    CSS_PROPERTY_SCROLL_PADDING_BLOCK,
    CSS_PROPERTY_SCROLL_PADDING_BLOCK_START,
    CSS_PROPERTY_SCROLL_PADDING_BLOCK_END,
    CSS_PROPERTY_SCROLL_PADDING_INLINE,
    CSS_PROPERTY_SCROLL_PADDING_INLINE_START,
    CSS_PROPERTY_SCROLL_PADDING_INLINE_END,

    // Ruby Annotation Properties
    CSS_PROPERTY_RUBY_ALIGN,
    CSS_PROPERTY_RUBY_POSITION,

    // Additional Font Properties
    CSS_PROPERTY_FONT_SIZE_ADJUST,
    CSS_PROPERTY_FONT_KERNING,
    CSS_PROPERTY_FONT_VARIANT_LIGATURES,
    CSS_PROPERTY_FONT_VARIANT_CAPS,
    CSS_PROPERTY_FONT_VARIANT_NUMERIC,
    CSS_PROPERTY_FONT_VARIANT_ALTERNATES,
    CSS_PROPERTY_FONT_VARIANT_EAST_ASIAN,
    CSS_PROPERTY_FONT_FEATURE_SETTINGS,
    CSS_PROPERTY_FONT_LANGUAGE_OVERRIDE,
    CSS_PROPERTY_FONT_OPTICAL_SIZING,
    CSS_PROPERTY_FONT_VARIATION_SETTINGS,
    CSS_PROPERTY_FONT_DISPLAY,

    // Background Properties (additional)
    CSS_PROPERTY_BACKGROUND_POSITION_X,
    CSS_PROPERTY_BACKGROUND_POSITION_Y,
    CSS_PROPERTY_BACKGROUND_BLEND_MODE,

    // Border Properties (additional)
    CSS_PROPERTY_BORDER_IMAGE,
    CSS_PROPERTY_BORDER_IMAGE_SOURCE,
    CSS_PROPERTY_BORDER_IMAGE_SLICE,
    CSS_PROPERTY_BORDER_IMAGE_WIDTH,
    CSS_PROPERTY_BORDER_IMAGE_OUTSET,
    CSS_PROPERTY_BORDER_IMAGE_REPEAT,
    CSS_PROPERTY_OUTLINE,
    CSS_PROPERTY_OUTLINE_STYLE,
    CSS_PROPERTY_OUTLINE_WIDTH,
    CSS_PROPERTY_OUTLINE_COLOR,
    CSS_PROPERTY_OUTLINE_OFFSET,

    // Box Shadow
    CSS_PROPERTY_BOX_SHADOW,

    // Float Properties (additional)
    CSS_PROPERTY_FLOAT_REFERENCE,
    CSS_PROPERTY_FLOAT_DEFER,
    CSS_PROPERTY_FLOAT_OFFSET,
    CSS_PROPERTY_WRAP_FLOW,
    CSS_PROPERTY_WRAP_THROUGH,

    // Baseline Properties
    CSS_PROPERTY_DOMINANT_BASELINE,
    CSS_PROPERTY_ALIGNMENT_BASELINE,
    CSS_PROPERTY_BASELINE_SHIFT,
    CSS_PROPERTY_BASELINE_SOURCE,

    // Text Box Trim Properties (CSS Inline Level 3)
    CSS_PROPERTY_TEXT_BOX,
    CSS_PROPERTY_TEXT_BOX_TRIM,
    CSS_PROPERTY_TEXT_BOX_EDGE,
    CSS_PROPERTY_LINE_CLAMP,

    // Vendor-prefixed Properties
    CSS_PROPERTY_WEBKIT_LINE_CLAMP,

    // SVG Paint Properties
    CSS_PROPERTY_STROKE,
    CSS_PROPERTY_STROKE_WIDTH,

    // Content Visibility
    CSS_PROPERTY_CONTENT_VISIBILITY,

    // Global property reset
    CSS_PROPERTY_ALL,

    CSS_PROPERTY_TEXT_UNDERLINE_OFFSET,
    CSS_PROPERTY_TEXT_DECORATION_SKIP_INK,
    CSS_PROPERTY_TEXT_UNDERLINE_POSITION,

    CSS_PROPERTY_TRANSLATE,
    CSS_PROPERTY_ROTATE,
    CSS_PROPERTY_SCALE,

    // registered SVG presentation properties share their existing paint consumers.
    CSS_PROPERTY_FILL_OPACITY,
    CSS_PROPERTY_STROKE_OPACITY,
    CSS_PROPERTY_STROKE_DASHARRAY,
    CSS_PROPERTY_STROKE_DASHOFFSET,
    CSS_PROPERTY_STROKE_LINECAP,
    CSS_PROPERTY_STROKE_LINEJOIN,
    CSS_PROPERTY_STROKE_MITERLIMIT,
    CSS_PROPERTY_FILL_RULE,
    CSS_PROPERTY_CLIP_RULE,
    CSS_PROPERTY_PAINT_ORDER,
    CSS_PROPERTY_STOP_COLOR,
    CSS_PROPERTY_STOP_OPACITY,
    CSS_PROPERTY_FLOOD_COLOR,
    CSS_PROPERTY_FLOOD_OPACITY,
    CSS_PROPERTY_LIGHTING_COLOR,

    // Custom Properties (CSS Variables)
    CSS_PROPERTY_CUSTOM,

    CSS_PROPERTY_COUNT,
    CSS_PROPERTY_UNKNOWN = -1
} CssPropertyCode;

// individual transforms compose in this order, independently of declaration order.
static inline int css_individual_transform_index(CssPropertyCode property) {
    return property >= CSS_PROPERTY_TRANSLATE && property <= CSS_PROPERTY_SCALE
        ? property - CSS_PROPERTY_TRANSLATE : -1;
}

static inline int css_animation_longhand_index(CssPropertyCode property) {
    return property >= CSS_PROPERTY_ANIMATION_NAME && property <= CSS_PROPERTY_ANIMATION_PLAY_STATE
        ? property - CSS_PROPERTY_ANIMATION_NAME : -1;
}

static inline int css_transition_longhand_index(CssPropertyCode property) {
    return property >= CSS_PROPERTY_TRANSITION_PROPERTY && property <= CSS_PROPERTY_TRANSITION_DELAY
        ? property - CSS_PROPERTY_TRANSITION_PROPERTY : -1;
}

// ============================================================================
// CSS Value Structures
// ============================================================================

// Forward declaration
typedef struct CssValue CssValue;

// CSS Variable (custom property) reference
typedef struct CSSVarRef {
    const char* name;           // Variable name (without --)
    CssValue* fallback;         // Fallback value
    bool has_fallback;
} CSSVarRef;

// Environment variable reference
typedef struct CSSEnvRef {
    const char* name;           // Environment variable name
    CssValue* fallback;         // Fallback value
    bool has_fallback;
} CSSEnvRef;

// Attribute reference
typedef struct CSSAttrRef {
    const char* name;           // Attribute name
    const char* type_or_unit;   // Type or unit specifier
    CssValue* fallback;         // Fallback value
    bool has_fallback;
} CSSAttrRef;

// Color mix function reference
typedef struct CSSColorMix {
    CssValue* color1;           // First color
    CssValue* color2;           // Second color
    double percentage;          // Mix percentage
    const char* method;         // Color space method
} CSSColorMix;

#ifndef COLOR_TYPE_DEFINED
#define COLOR_TYPE_DEFINED
typedef union {
    uint32_t c;  // 32-bit ABGR color format,
    struct {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    };
} Color;
#endif

// Color components structure for HSL, HWB, LAB, LCH color spaces
typedef struct {
    double component1;  // h (hue) for HSL/HWB/LCH, l (lightness) for LAB
    double component2;  // s (saturation) for HSL, w (whiteness) for HWB, a for LAB, c (chroma) for LCH
    double component3;  // l (lightness) for HSL, b (blackness) for HWB, b for LAB, h (hue) for LCH
    double component4;  // alpha channel for all formats
} CssColorComponents;

// Generic CSS value structure
enum CssValueFlag : uint8_t {
    CSS_VALUE_AUTHORED_IDENTIFIER = 1u << 0,
    CSS_VALUE_NON_IDENTIFIER_TOKEN = 1u << 1
};

typedef struct CssValue {
    CssValueType type;
    uint8_t flags;
    bool has_keyword_spelling;
    uint8_t reserved;
    union {
        // Numeric values
        struct {
            double value;
            CssUnit unit;
        } length;

        struct {
            double value;
        } percentage;

        struct {
            double value;
            bool is_integer;
        } number;

        // Color value
        struct {
            CssColorType type;
            union {
                Color color;
                struct { uint8_t r, g, b, a; } rgba;
                CssColorComponents* components;  // For HSLA, HWBA, LABA, LCHA (pointer to 4 doubles)
                const char* keyword;
            } data;
        } color;

        // String value
        const char* string;

        // URL value
        const char* url;

        // Keyword value (enum-based)
        CssEnum keyword;
        struct {
            CssEnum id;
            const char* spelling; // stylesheet-owned identity, shares the compact keyword union
        } keyword_token;

        // Color hex value (legacy support)
        const char* color_hex;

        // Unicode range value
        const char* unicode_range;

        // Custom property reference
        struct {
            const char* name;
            struct CssValue* fallback;
        } custom_property;

        // List of values
        struct {
            struct CssValue** values;
            int count;
            bool comma_separated;
        } list;

        // Function value
        CssFunction* function;

        // CSS Variable reference
        CSSVarRef* var_ref;

        // Environment variable reference
        CSSEnvRef* env_ref;

        // Attribute reference
        CSSAttrRef* attr_ref;

        // Color mix function
        CSSColorMix* color_mix;

        // Calc expression
        CssCalcNode* calc_expression;
    } data;
} CssValue;

// comma groups are list entries; a space-separated value remains one entry.
static inline int css_value_comma_count(const CssValue* value) {
    return !value ? 0 : value->type == CSS_VALUE_TYPE_LIST && value->data.list.comma_separated
        ? value->data.list.count : 1;
}

static inline const CssValue* css_value_comma_at(const CssValue* value, int index) {
    int count = css_value_comma_count(value);
    if (count <= 0 || index < 0) return NULL;
    return value->type == CSS_VALUE_TYPE_LIST && value->data.list.comma_separated
        ? (value->data.list.values ? value->data.list.values[index % count] : NULL) : value;
}

typedef struct CssMotionShorthandParts {
    const CssValue* values[8];
} CssMotionShorthandParts;

// one definition is shared by validation, computed style and frame scheduling.
bool css_parse_motion_shorthand(CssPropertyCode shorthand, const CssValue* group,
    CssMotionShorthandParts* parts);
bool css_motion_longhand_accepts(CssPropertyCode property, const CssValue* value);
typedef struct CssBorderImageComponents {
    const CssValue* source;
    const CssValue* slice[5];
    int slice_count;
    const CssValue* width[4];
    int width_count;
    const CssValue* outset[4];
    int outset_count;
    const CssValue* repeat[2];
    int repeat_count;
} CssBorderImageComponents;

bool css_split_border_image_shorthand(const CssValue* value,
                                      CssBorderImageComponents* parts);

// CSS shorthand expansion is part of the value model, not layout: one to
// four components map clockwise to top, right, bottom, and left.
static inline int css_quad_value_index(int count, int side) {
    static const uint8_t indices[4][4] = {
        {0, 0, 0, 0}, {0, 1, 0, 1},
        {0, 1, 2, 1}, {0, 1, 2, 3}
    };
    return count >= 1 && count <= 4 && side >= 0 && side < 4
        ? indices[count - 1][side] : -1;
}

static inline const CssValue* css_box_shorthand_side_value(const CssValue* value,
                                                           int side) {
    if (!value || value->type != CSS_VALUE_TYPE_LIST) return value;
    int index = css_quad_value_index(value->data.list.count, side);
    CssValue** values = value->data.list.values;
    return index >= 0 && values ? values[index] : nullptr;
}

// ============================================================================
// CSS Style Declaration and Cascade
// ============================================================================

// CSS Specificity for cascade calculation
typedef struct CssSpecificity {
    uint8_t inline_style;     // 1 if inline style, 0 otherwise
    uint8_t ids;              // Number of ID selectors
    uint8_t classes;          // Number of class, attribute, and pseudo-class selectors
    uint8_t elements;         // Number of type and pseudo-element selectors
    bool important;           // !important flag
} CssSpecificity;

// CSS Declaration Origin for cascade ordering
typedef enum CssOrigin {
    CSS_ORIGIN_USER_AGENT,    // Browser default styles
    CSS_ORIGIN_USER,          // User stylesheet
    CSS_ORIGIN_AUTHOR,        // Document stylesheet
    CSS_ORIGIN_ANIMATION,     // CSS animations
    CSS_ORIGIN_TRANSITION     // CSS transitions
} CssOrigin;

typedef void (*CssDeclarationPayloadRetainFn)(void* owner);
typedef void (*CssDeclarationPayloadReleaseFn)(void* owner);

// CSS Declaration with metadata
typedef struct CssDeclaration {
    CssPropertyCode property_code;
    NameId name_id;               // sparse generated identity; custom/unknown names use NONE
    CssValue* value;
    CssSpecificity specificity;
    CssOrigin origin;
    uint32_t source_order;    // Declaration order within stylesheet
    uint32_t layer_order;     // zero is unlayered; named layers follow declaration order
    uint32_t scope_proximity; // zero is unscoped; scoped values store ancestor hops plus one
    bool important;           // !important flag
    const char* source_file;  // Source file (for debugging)
    int source_line;          // Source line (for debugging)
    const char* property_name; // Original property name (for unknown/vendor properties)
    size_t property_name_length; // CSSOM names may contain embedded NUL code points

    // Raw value text from source (for faithful CSSOM serialization)
    const char* value_text;
    size_t value_text_len;

    // Memory management and validation
    bool owns_payload;        // declaration owns a deep CssValue/string snapshot
    bool tree_owned_record;   // containing StyleTree may reclaim this declaration record
    bool presentation_value; // transient host layer; survives authored-style recascade
    bool valid;               // Validation flag
    int ref_count;            // Reference counting for memory management
    void* payload_owner;      // immutable epoch payload shared by cascade records
    CssDeclarationPayloadRetainFn payload_retain;
    CssDeclarationPayloadReleaseFn payload_release;
} CssDeclaration;

static inline StrView css_declaration_name(const CssDeclaration* declaration) {
    if (!declaration) return strview_init(nullptr, 0);
    return declaration->property_name_length
        ? strview_init(declaration->property_name, declaration->property_name_length)
        : strview_from_cstr(declaration->property_name);
}

// CSS Style Node for cascade resolution
typedef struct CssStyleNode {
    AvlNode base;             // AVL tree node (key is property_code)
    CssPropertyCode property_code;
    CssDeclaration* winning_declaration;
    CssDeclaration** losing_declarations;
    size_t losing_count;
    size_t losing_capacity;
    bool has_custom_property; // For CSS variables
} CssStyleNode;

// Complete computed style for an element
typedef struct CssComputedStyle {
    AvlTree* properties;      // Tree of CssStyleNode
    Pool* pool;               // Memory pool for allocations

    // Inheritance chain
    struct CssComputedStyle* parent;
    bool is_root;
} CssComputedStyle;

// ============================================================================
// CSS Rule and Stylesheet Types
// ============================================================================

// Forward declarations
typedef struct CssRule CssRule;
typedef struct CssStylesheet CssStylesheet;
typedef struct CssPageRule CssPageRule;

typedef enum CssPropertySyntaxType {
    CSS_SYNTAX_IDENT, CSS_SYNTAX_LENGTH, CSS_SYNTAX_NUMBER,
    CSS_SYNTAX_PERCENTAGE, CSS_SYNTAX_LENGTH_PERCENTAGE, CSS_SYNTAX_STRING,
    CSS_SYNTAX_COLOR, CSS_SYNTAX_IMAGE, CSS_SYNTAX_URL, CSS_SYNTAX_INTEGER,
    CSS_SYNTAX_ANGLE, CSS_SYNTAX_TIME, CSS_SYNTAX_RESOLUTION,
    CSS_SYNTAX_TRANSFORM_FUNCTION, CSS_SYNTAX_TRANSFORM_LIST, CSS_SYNTAX_CUSTOM_IDENT,
} CssPropertySyntaxType;

struct CssPropertySyntaxComponent {
    CssPropertySyntaxType type;
    const char* identifier;
    char multiplier;
};

struct CssPropertyRegistration {
    const char* name;
    const char* syntax;
    CssPropertySyntaxComponent* components;
    size_t component_count;
    bool universal;
    bool inherits;
    const CssValue* initial_value;
    const char* initial_text;
    size_t initial_text_length;
};

bool css_value_is_custom_ident(const CssValue* value);
bool css_parse_property_syntax(const char* syntax, Pool* pool, CssPropertyRegistration* result);
const CssPropertySyntaxComponent* css_match_property_syntax(
    const CssPropertyRegistration* registration, const CssValue* value);
bool css_property_initial_is_independent(const CssValue* value);
bool css_property_registration_is_valid(const CssPropertyRegistration* registration);

// CSS Rule types
typedef enum CssRuleType {
    CSS_RULE_STYLE,          // Standard style rule with selector and declarations
    CSS_RULE_MEDIA,          // @media rule
    CSS_RULE_IMPORT,         // @import rule
    CSS_RULE_CHARSET,        // @charset rule
    CSS_RULE_NAMESPACE,      // @namespace rule
    CSS_RULE_SUPPORTS,       // @supports rule
    CSS_RULE_KEYFRAMES,      // @keyframes rule
    CSS_RULE_KEYFRAME,       // Individual keyframe within @keyframes
    CSS_RULE_PAGE,           // @page rule
    CSS_RULE_FONT_FACE,      // @font-face rule
    CSS_RULE_VIEWPORT,       // @viewport rule
    CSS_RULE_COUNTER_STYLE,  // @counter-style rule
    CSS_RULE_LAYER,          // @layer rule
    CSS_RULE_CONTAINER,      // @container rule
    CSS_RULE_SCOPE,          // @scope rule
    CSS_RULE_NESTING,        // Nested rule
    CSS_RULE_NESTED_DECLARATIONS, // CSSNestedDeclarations (declarations after nested rules)
    CSS_RULE_PROPERTY,           // @property custom-property registration
} CssRuleType;

typedef struct CssLayerName {
    const char** parts;
    size_t part_count;
} CssLayerName;

// CSS Rule structure
typedef struct CssRule {
    CssRuleType type;
    Pool* pool;
    struct DomDocument* owner_document; // pool lifetime survives removal from CSSOM parents
    CssPageRule* page; // typed page selectors/descriptors; generic text remains available to CSSOM

    // Rule content varies by type
    union {
        struct {
            struct CssSelector* selector;          // Single selector (for backward compatibility)
            struct CssSelectorGroup* selector_group; // Selector group (comma-separated selectors)
            const char* authored_selector_text; // nested CSSOM selector before & expansion
            CssDeclaration** declarations;
            size_t declaration_count;
            CssRule** nested_rules;
            size_t nested_rule_count;
        } style_rule;

        struct {
            const char* condition;
            CssRule** rules;
            size_t rule_count;
            CssLayerName* layer_names;
            size_t layer_name_count;
            bool layer_statement;
            bool invalid_layer;
            CssSelectorGroup* scope_start;
            CssSelectorGroup* scope_end;
            const char* scope_start_text;
            const char* scope_end_text;
            CssSelectorGroup* scope_selector;
            bool invalid_scope;
        } conditional_rule; // For @media, @supports, @container, etc.

        struct {
            const char* url;
            const char* media;
            const char* supports;
            CssLayerName layer_name;
            bool has_layer;
            bool anonymous_layer;
            bool invalid;
            struct CssStylesheet* stylesheet;
        } import_rule;

        struct {
            const char* charset;
        } charset_rule;

        struct {
            const char* prefix;
            const char* namespace_url;
        } namespace_rule;

        struct {
            const char* name;       // e.g. "font-face", "keyframes"
            const char* content;    // Raw content inside the at-rule
        } generic_rule; // For @font-face, @keyframes, etc.

        CssPropertyRegistration property_rule;
    } data;

    // Source information
    CssOrigin origin;
    uint32_t source_order;

    // Parent rule (for nested rules)
    CssRule* parent;
    CssStylesheet* stylesheet; // pool-owned association; cleared when detached through CSSOM

    // Legacy compatibility fields (for older code that expects these)
    size_t property_count;       // Number of properties in this rule
    CssValue** property_values;  // Array of property values
    const char** property_names; // Array of property names

    // Specificity caching for performance
    bool specificity_computed;   // Whether specificity has been calculated
    uint32_t cached_specificity; // Cached specificity value
} CssRule;

struct CssRuleChildList {
    CssRule*** rules;
    size_t* count;
};
CssRuleChildList css_rule_child_list(CssRule* rule);
struct CssRuleInterface {
    const char* name;
    const char* base;
    uint16_t legacy_type;
    const char* host_name;
};
const CssRuleInterface* css_rule_interface(const CssRule* rule);
void css_rule_attach(CssRule* rule, CssRule* parent, CssStylesheet* stylesheet);

// CSS Stylesheet structure
typedef struct CssStylesheet {
    Pool* pool;

    // Rules in the stylesheet
    CssRule** rules;
    size_t rule_count;
    size_t rule_capacity;
    uint64_t mutation_generation; // includes edits in imported child sheets

    // Stylesheet metadata
    const char* title;
    const char* href;
    const char* media;
    const char* origin_url;      // URL where stylesheet was loaded from
    CssOrigin origin;
    bool disabled;
    bool is_import_child;
    bool constructed;           // constructed sheets reject @import through CSSOM.
    // Document sheets retain their owning <link> or <style> for source order.
    struct DomElement* owner_element;
    struct DomDocument* owner_document; // native lifetime remains known after rule detachment

    // Source information
    const char* source_text;
    size_t source_length;

    // Performance metrics
    double parse_time;           // Time taken to parse the stylesheet

    // Import information
    struct CssStylesheet* parent_stylesheet;
    struct CssRule* owner_rule;  // retained import association, including after rule detachment
    struct CssStylesheet** imported_stylesheets;
    size_t imported_count;

    // Namespace declarations
    struct {
        const char* prefix;
        const char* url;
    }* namespaces;
    size_t namespace_count;

    // Feature flags
    bool uses_nesting;           // Whether the stylesheet uses CSS nesting
    bool uses_custom_properties; // Whether the stylesheet uses custom properties
} CssStylesheet;

// ============================================================================
// CSS Property System Functions
// ============================================================================

// Property information
typedef struct CssPropertyInfo {
    CssPropertyCode id;
    const char* name;
    bool inherited;
    CssValue* initial_value;
    bool supports_percentage;
    bool supports_calc;
    CssValueType* valid_types;
    size_t valid_type_count;
} CssPropertyInfo;

// Style system functions
CssComputedStyle* css_computed_style_create(Pool* pool);
void css_computed_style_destroy(CssComputedStyle* style);

// Property management
CssValue* css_style_get_property(CssComputedStyle* style, CssPropertyCode property_code);
bool css_style_has_property(CssComputedStyle* style, CssPropertyCode property_code);
void css_style_remove_property(CssComputedStyle* style, CssPropertyCode property_code);

// Declaration management
void css_style_cascade_resolve(CssComputedStyle* style);

// Value creation and manipulation
CssValue* css_value_create_length(Pool* pool, double value, CssUnit unit);
CssValue* css_value_create_percentage(Pool* pool, double value);
CssValue* css_value_create_number(Pool* pool, double value);
CssValue* css_value_create_color_rgba(Pool* pool, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
CssValue* css_value_create_keyword(Pool* pool, const char* keyword);
CssValue* css_value_create_string(Pool* pool, const char* string);
CssValue* css_value_create_url(Pool* pool, const char* url);
CssValue* css_value_create_list(Pool* pool, CssValue** values, size_t count);
CssValue* css_value_create_function(Pool* pool, const char* name, CssValue** args, int count);
void css_value_destroy(CssValue* value);

// Unit and value utilities
CssUnit css_unit_from_string(const char* unit_str, size_t length);
const char* css_unit_to_string(CssUnit unit);

// Hex color parsing utility
// hex_str must start with '#'; returns true on success, writes RGBA channels.
bool css_parse_hex_to_rgba(const char* hex_str, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a);
const char* css_color_type_to_string(CssColorType type);
bool css_unit_is_length(CssUnit unit);
bool css_unit_is_angle(CssUnit unit);
bool css_unit_is_time(CssUnit unit);
bool css_unit_is_frequency(CssUnit unit);
bool css_unit_is_resolution(CssUnit unit);
bool css_unit_is_relative(CssUnit unit);
bool css_unit_is_absolute(CssUnit unit);

// Value type checking
bool css_value_is_inherit(const CssValue* value);
bool css_value_is_initial(const CssValue* value);
bool css_value_is_unset(const CssValue* value);
bool css_value_is_auto(const CssValue* value);
bool css_value_is_none(const CssValue* value);

typedef const CssValue* (*CssVariableLookupFn)(void* context, DomElement* element,
                                               const char* name, DomElement** owner);
const CssValue* css_resolve_var_value(Pool* pool, const CssValue* value,
                                      CssVariableLookupFn lookup, void* context,
                                      DomElement* element = nullptr);

// Value conversion and computation
double css_value_to_pixels(const CssValue* value, double font_size, double viewport_width, double viewport_height);
CssValue* css_value_compute(const CssValue* value, const CssComputedStyle* parent_style, Pool* pool);

// Inheritance and cascade
void css_cascade_declarations(CssStyleNode* node);
bool css_property_is_inherited(CssPropertyCode property_code);
CssValue* css_get_initial_value(CssPropertyCode property_code, Pool* pool);
void css_inherit_properties(CssComputedStyle* style, const CssComputedStyle* parent);



/**
 * CSS Property System
 *
 * This system provides a comprehensive database of CSS properties with their
 * types, validation rules, inheritance behavior, and initial values.
 * It's designed to integrate with the AVL tree style system.
 */

// ============================================================================
// Property Value Types
// ============================================================================

typedef enum PropertyValueType {
    PROP_TYPE_KEYWORD,           // Named values (auto, none, inherit, etc.)
    PROP_TYPE_LENGTH,            // px, em, rem, %, etc.
    PROP_TYPE_NUMBER,            // Unitless numbers (includes integers via is_integer flag)
    PROP_TYPE_PERCENTAGE,        // Percentage values
    PROP_TYPE_COLOR,             // Color values
    PROP_TYPE_STRING,            // String literals
    PROP_TYPE_URL,               // URL references
    PROP_TYPE_ANGLE,             // deg, rad, grad, turn
    PROP_TYPE_TIME,              // s, ms
    PROP_TYPE_FREQUENCY,         // Hz, kHz
    PROP_TYPE_RESOLUTION,        // dpi, dpcm, dppx
    PROP_TYPE_FUNCTION,          // calc(), var(), rgb(), etc.
    PROP_TYPE_LIST,              // Space or comma-separated values
    PROP_TYPE_CUSTOM             // Custom property values
} PropertyValueType;

// ============================================================================
// Property Inheritance and Initial Values
// ============================================================================

typedef enum PropertyInheritance {
    PROP_INHERIT_NO,             // Property doesn't inherit
    PROP_INHERIT_YES,            // Property inherits by default
    PROP_INHERIT_KEYWORD         // Inherit keyword supported
} PropertyInheritance;

// ============================================================================
// Property Definition Structure
// ============================================================================

typedef struct CssProperty {
    CssPropertyCode code;          // dense dispatch code, never semantic name identity
    const char* name;            // Property name (e.g., "color", "margin-top")
    PropertyValueType type;      // Primary value type
    PropertyInheritance inheritance; // Inheritance behavior
    const char* initial_value;   // Initial value as string
    bool animatable;             // Whether property can be animated
    bool shorthand;              // Whether this is a shorthand property
    CssPropertyCode* longhand_props; // Array of longhand properties (for shorthands)
    int longhand_count;          // Number of longhand properties

    // Validation function pointer
    bool (*validate_value)(const char* value_str, void** parsed_value, Pool* pool);

    // Value computation function
    void* (*compute_value)(void* specified_value, void* parent_value, Pool* pool);
    NameId name_id;                // generated identity for predefined property spelling
} CssProperty;

// ============================================================================
// Property Database API
// ============================================================================

/**
 * Initialize the CSS property system
 * @param pool Memory pool for allocations
 * @return true on success, false on failure
 */
bool css_property_system_init(Pool* pool);

/**
 * Cleanup the CSS property system
 */
void css_property_system_cleanup(void);

/**
 * Get property by dense dispatch code
 * @param property_code Property code to look up
 * @return Property definition or NULL if not found
 */
const CssProperty* css_property_get_by_code(CssPropertyCode property_code);

/**
 * Get property by name
 * @param name Property name to look up
 * @return Property definition or NULL if not found
 */
const CssProperty* css_property_get_by_name(const char* name);

/**
 * Get dense property code by name
 * @param name Property name to look up
 * @return Property code or 0 if not found
 */
CssPropertyCode css_property_code_from_name(const char* name);
CssPropertyCode css_property_code_from_name_id(NameId name_id);
NameId css_property_name_id(CssPropertyCode property_code);

const char* css_property_spelling_from_code(CssPropertyCode property_code);


/**
 * Check if a property exists
 * @param property_code Property code to check
 * @return true if property exists, false otherwise
 */
bool css_property_exists(CssPropertyCode property_code);

/**
 * Check if a property is inherited by default
 * @param property_code Property ID to check
 * @return true if inherited, false otherwise
 */
bool css_property_is_inherited(CssPropertyCode property_code);

/**
 * Check if a property is animatable
 * @param property_code Property ID to check
 * @return true if animatable, false otherwise
 */
bool css_property_is_animatable(CssPropertyCode property_code);

/**
 * Check if a property is a shorthand
 * @param property_code Property ID to check
 * @return true if shorthand, false otherwise
 */
bool css_property_is_shorthand(CssPropertyCode property_code);
bool css_property_shorthand_contains(CssPropertyCode shorthand, CssPropertyCode property);

/**
 * Get longhand properties for a shorthand property
 * @param shorthand_id Shorthand property ID
 * @param longhand_ids Output array for longhand property IDs
 * @param max_count Maximum number of longhand properties to return
 * @return Number of longhand properties returned
 */
int css_property_get_longhand_properties(CssPropertyCode shorthand_id,
                                        CssPropertyCode* longhand_ids,
                                        int max_count);
CssPropertyCode css_property_cascade_shorthand(CssPropertyCode property);

/**
 * Get the initial value for a property
 * @param property_code Property ID
 * @param pool Memory pool for allocation
 * @return Initial value or NULL if not available
 */
void* css_property_get_initial_value(CssPropertyCode property_code, Pool* pool);

/**
 * Validate a property value
 * @param property_code Property ID
 * @param value_str Value string to validate
 * @param parsed_value Output parsed value (allocated from pool)
 * @param pool Memory pool for allocations
 * @return true if valid, false otherwise
 */
bool css_property_validate_and_parse(CssPropertyCode property_code,
                                     const char* value_str,
                                     void** parsed_value,
                                     Pool* pool);

/**
 * Compute a property value (resolve relative units, inheritance, etc.)
 * @param property_code Property ID
 * @param specified_value Specified value from CSS
 * @param parent_value Parent element's computed value (for inheritance)
 * @param pool Memory pool for allocations
 * @return Computed value
 */
void* css_property_compute_value(CssPropertyCode property_code,
                                void* specified_value,
                                void* parent_value,
                                Pool* pool);

// ============================================================================
// Custom Property Support
// ============================================================================

/**
 * Register a custom property (CSS custom properties: --property-name)
 * @param name Custom property name (including --)
 * @param pool Memory pool for allocations
 * @return Property ID for the custom property
 */
CssPropertyCode css_property_register_custom(const char* name, Pool* pool);

/**
 * Get custom property ID by name
 * @param name Custom property name (including --)
 * @return Property ID or 0 if not found
 */
CssPropertyCode css_property_get_custom_id(const char* name);

/**
 * Check if a property ID represents a custom property
 * @param property_code Property ID to check
 * @return true if custom property, false otherwise
 */
bool css_property_is_custom(CssPropertyCode property_code);

// ============================================================================
// Property Value Parsing Utilities
// ============================================================================

/**
 * Parse a length value from string
 * @param value_str Value string (e.g., "10px", "1.5em")
 * @param length Output length structure
 * @return true if successfully parsed, false otherwise
 */
bool css_parse_length(const char* value_str, CssLength* length);

/**
 * Parse a color value from string
 * @param value_str Value string (e.g., "#ff0000", "rgb(255,0,0)", "red")
 * @param color Output color structure
 * @return true if successfully parsed, false otherwise
 */
bool css_parse_color(const char* value_str, CssColor* color);

// ============================================================================
// Debugging and Utility Functions
// ============================================================================

/**
 * Get property name by ID
 * @param property_code Property ID
 * @return Property name or NULL if not found
 */
const char* css_property_spelling_from_code(CssPropertyCode property_code);

/**
 * Print property information for debugging
 * @param property_code Property ID to print
 */
void css_property_print_info(CssPropertyCode property_code);

/**
 * Get total number of registered properties
 * @return Number of properties
 */
int css_property_get_count(void);

/**
 * Iterate over all properties
 * @param callback Function to call for each property
 * @param context User context passed to callback
 * @return Number of properties processed
 */
int css_property_foreach(bool (*callback)(const CssProperty* prop, void* context),
                        void* context);

// ============================================================================
// CSS Properties Compatibility Layer
// ============================================================================

/**
 * CSS Properties Compatibility
 *
 * This section provides compatibility aliases for legacy code
 * that may be using older property type names or function signatures.
 */

// Compatibility aliases for legacy code
typedef CssValue CSSPropertyValue;
typedef CssValueType CSSPropertyType;

// Legacy enum aliases for property types
#define CSS_PROP_TYPE_KEYWORD CSS_VALUE_TYPE_KEYWORD
#define CSS_PROP_TYPE_LENGTH CSS_VALUE_TYPE_LENGTH
#define CSS_PROP_TYPE_PERCENTAGE CSS_VALUE_TYPE_PERCENTAGE
#define CSS_PROP_TYPE_COLOR CSS_VALUE_TYPE_COLOR
#define CSS_PROP_TYPE_NUMBER CSS_VALUE_TYPE_NUMBER
#define CSS_PROP_TYPE_STRING CSS_VALUE_TYPE_STRING
#define CSS_PROP_TYPE_URL CSS_VALUE_TYPE_URL
#define CSS_PROP_TYPE_CALC CSS_VALUE_TYPE_CALC
#define CSS_PROP_TYPE_CUSTOM CSS_VALUE_TYPE_CUSTOM
#define CSS_PROP_TYPE_UNKNOWN CSS_VALUE_TYPE_UNKNOWN

// Legacy property ID aliases
#define CSS_PROP_COLOR CSS_PROPERTY_COLOR
#define CSS_PROP_BACKGROUND_COLOR CSS_PROPERTY_BACKGROUND_COLOR
#define CSS_PROP_FONT_SIZE CSS_PROPERTY_FONT_SIZE
#define CSS_PROP_FONT_FAMILY CSS_PROPERTY_FONT_FAMILY
#define CSS_PROP_FONT_WEIGHT CSS_PROPERTY_FONT_WEIGHT
#define CSS_PROP_WIDTH CSS_PROPERTY_WIDTH
#define CSS_PROP_HEIGHT CSS_PROPERTY_HEIGHT
#define CSS_PROP_MARGIN CSS_PROPERTY_MARGIN
#define CSS_PROP_PADDING CSS_PROPERTY_PADDING
#define CSS_PROP_BORDER CSS_PROPERTY_BORDER
#define CSS_PROP_DISPLAY CSS_PROPERTY_DISPLAY
#define CSS_PROP_POSITION CSS_PROPERTY_POSITION
#define CSS_PROP_TOP CSS_PROPERTY_TOP
#define CSS_PROP_RIGHT CSS_PROPERTY_RIGHT
#define CSS_PROP_BOTTOM CSS_PROPERTY_BOTTOM
#define CSS_PROP_LEFT CSS_PROPERTY_LEFT
#define CSS_PROP_Z_INDEX CSS_PROPERTY_Z_INDEX
#define CSS_PROP_OPACITY CSS_PROPERTY_OPACITY
#define CSS_PROP_VISIBILITY CSS_PROPERTY_VISIBILITY
#define CSS_PROP_OVERFLOW CSS_PROPERTY_OVERFLOW
#define CSS_PROP_TEXT_ALIGN CSS_PROPERTY_TEXT_ALIGN
#define CSS_PROP_TEXT_DECORATION CSS_PROPERTY_TEXT_DECORATION
#define CSS_PROP_LINE_HEIGHT CSS_PROPERTY_LINE_HEIGHT
#define CSS_PROP_FLEX CSS_PROPERTY_FLEX
#define CSS_PROP_GRID CSS_PROPERTY_GRID
#define CSS_PROP_TRANSFORM CSS_PROPERTY_TRANSFORM
#define CSS_PROP_TRANSITION CSS_PROPERTY_TRANSITION
#define CSS_PROP_ANIMATION CSS_PROPERTY_ANIMATION
#define CSS_PROP_UNKNOWN CSS_PROPERTY_UNKNOWN

// CSS Property (legacy compatibility)
typedef CssDeclaration CSSProperty;

// Function declarations (compatibility layer using css_style.h types)
enum CssTextDecorationLineFlag : uint8_t {
    CSS_TEXT_DECO_UNDERLINE = 1u << 0,
    CSS_TEXT_DECO_OVERLINE = 1u << 1,
    CSS_TEXT_DECO_LINE_THROUGH = 1u << 2,
    CSS_TEXT_DECO_BLINK = 1u << 3
};
uint8_t css_text_decoration_line_flag(CssEnum keyword);
bool css_property_validate_value(CssPropertyCode id, const CssValue* value);
bool css_property_is_svg_paint(CssPropertyCode id);
bool css_property_is_svg_presentation(CssPropertyCode id);
bool css_property_is_svg_opacity(CssPropertyCode id);
bool css_property_is_svg_length(CssPropertyCode id);

typedef bool (*CssListItemVisitor)(const CssValue* value, void* context);
bool css_value_visit_list_items(const CssValue* value, CssListItemVisitor visitor, void* context);
bool css_property_validate_value_mode(CssPropertyCode id,
                                      const CssValue* value,
                                      bool quirks_mode);
bool css_text_emphasis_parse_style(const CssValue* value, bool vertical,
                                   uint32_t* mark, const CssValue** color);
bool css_display_legacy_keyword_supported(const char* name);
bool css_value_as_seconds(const CssValue* value, double* seconds);
bool css_value_is_timing_function(const CssValue* value);
bool css_animation_property_is_longhand(CssPropertyCode property);
CssPropertyCode css_timeline_shorthand_for(CssPropertyCode property);
bool css_property_validate_value_from_string(CssPropertyCode property_code,
    const char* value_str, void** parsed_value, Pool* pool);

#endif // CSS_STYLE_H
