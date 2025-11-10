#pragma once
#include "dom.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"

#ifdef __cplusplus
extern "C" {
#endif
#include <GLFW/glfw3.h>
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H
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

// Define lexbor tag and CSS value constants first, before including headers that need them
enum {
    HTM_TAG__UNDEF,
    HTM_TAG__END_OF_FILE,
    HTM_TAG__TEXT,
    HTM_TAG__DOCUMENT,
    HTM_TAG__EM_COMMENT,
    HTM_TAG__EM_DOCTYPE,
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

typedef enum CssEnumGroup {
    CSS_VALUE_GROUP__UNDEF = 0,
    CSS_VALUE_GROUP_GLOBAL,              // initial, inherit, unset, revert
    CSS_VALUE_GROUP_ALIGNMENT,           // flex-start, flex-end, center, space-between, space-around, stretch, baseline
    CSS_VALUE_GROUP_SIZE,                // auto, min-content, max-content, fit-content
    CSS_VALUE_GROUP_VERTICAL_ALIGN,      // top, bottom, middle, baseline, text-top, text-bottom, sub, super, alphabetic, etc.
    CSS_VALUE_GROUP_POSITION_SIDE,       // first, last, top, bottom, left, right
    CSS_VALUE_GROUP_BORDER_WIDTH,        // thin, medium, thick
    CSS_VALUE_GROUP_BORDER_STYLE,        // none, hidden, dotted, dashed, solid, double, groove, ridge, inset, outset
    CSS_VALUE_GROUP_BOX_MODEL,           // content-box, border-box, padding-box
    CSS_VALUE_GROUP_LOGICAL_SIDE,        // inline-start, inline-end, block-start, block-end
    CSS_VALUE_GROUP_COLOR,               // currentcolor, transparent, aliceblue, antiquewhite, aqua, etc.
    CSS_VALUE_GROUP_SYSTEM_COLOR,        // Canvas, CanvasText, LinkText, etc.
    CSS_VALUE_GROUP_COLOR_FUNCTION,      // rgb, rgba, hsl, hsla, hwb, lab, lch, oklab, oklch, color
    CSS_VALUE_GROUP_CURSOR,              // hand, pointer, text, wait, progress, grab, grabbing, move
    CSS_VALUE_GROUP_DIRECTION,           // ltr, rtl
    CSS_VALUE_GROUP_DISPLAY_OUTSIDE,     // block, inline, run-in
    CSS_VALUE_GROUP_DISPLAY_INSIDE,      // flow, flow-root, table, flex, grid, ruby
    CSS_VALUE_GROUP_DISPLAY_LISTITEM,    // list-item
    CSS_VALUE_GROUP_DISPLAY_INTERNAL,    // table-row-group, table-header-group, table-footer-group, table-row, table-cell, etc.
    CSS_VALUE_GROUP_DISPLAY_BOX,         // contents, none
    CSS_VALUE_GROUP_DISPLAY_LEGACY,      // inline-block, inline-table, inline-flex, inline-grid
    CSS_VALUE_GROUP_FLEX_DIRECTION,      // row, row-reverse, column, column-reverse
    CSS_VALUE_GROUP_FLEX_WRAP,           // nowrap, wrap, wrap-reverse
    CSS_VALUE_GROUP_FLEX_JUSTIFY,        // space-evenly (extends alignment group)
    CSS_VALUE_GROUP_GRID_AUTO_FLOW,      // row, column, dense
    CSS_VALUE_GROUP_FONT_FAMILY,         // serif, sans-serif, cursive, fantasy, monospace, system-ui, emoji, math, fangsong, ui-*
    CSS_VALUE_GROUP_FONT_SIZE,           // xx-small, x-small, small, medium, large, x-large, xx-large, xxx-large, larger, smaller
    CSS_VALUE_GROUP_FONT_STRETCH,        // normal, ultra-condensed, extra-condensed, condensed, semi-condensed, semi-expanded, expanded, extra-expanded, ultra-expanded
    CSS_VALUE_GROUP_FONT_STYLE,          // normal, italic, oblique
    CSS_VALUE_GROUP_FONT_WEIGHT,         // normal, bold, bolder, lighter
    CSS_VALUE_GROUP_LINE_BREAK,          // auto, loose, normal, strict, anywhere
    CSS_VALUE_GROUP_OVERFLOW,            // visible, hidden, clip, scroll, auto
    CSS_VALUE_GROUP_OVERFLOW_WRAP,       // normal, break-word, anywhere
    CSS_VALUE_GROUP_POSITION,            // static, relative, absolute, sticky, fixed
    CSS_VALUE_GROUP_TEXT_ALIGN,          // left, right, center, justify, start, end, match-parent, justify-all
    CSS_VALUE_GROUP_TEXT_DECO_LINE,      // none, underline, overline, line-through, blink
    CSS_VALUE_GROUP_TEXT_DECO_STYLE,     // solid, double, dotted, dashed, wavy
    CSS_VALUE_GROUP_TEXT_JUSTIFY,        // auto, inter-word, inter-character, none
    CSS_VALUE_GROUP_TEXT_ORIENTATION,    // mixed, upright, sideways
    CSS_VALUE_GROUP_TEXT_OVERFLOW,       // clip, ellipsis
    CSS_VALUE_GROUP_TEXT_TRANSFORM,      // none, capitalize, uppercase, lowercase, full-width, full-size-kana
    CSS_VALUE_GROUP_UNICODE_BIDI,        // normal, embed, isolate, bidi-override, isolate-override, plaintext
    CSS_VALUE_GROUP_WHITE_SPACE,         // normal, pre, nowrap, pre-wrap, break-spaces, pre-line
    CSS_VALUE_GROUP_WORD_BREAK,          // normal, break-all, keep-all, break-word
    CSS_VALUE_GROUP_FLOAT,               // left, right, none
    CSS_VALUE_GROUP_CLEAR,               // left, right, both, none
    CSS_VALUE_GROUP_WRITING_MODE,        // horizontal-tb, vertical-rl, vertical-lr, sideways-rl, sideways-lr
    CSS_VALUE_GROUP_LIST_STYLE_TYPE,     // disc, circle, square, decimal, lower-roman, upper-roman, lower-alpha, upper-alpha, none
    CSS_VALUE_GROUP_BGROUND_SIZE,        // auto, contain, cover
    CSS_VALUE_GROUP_BGROUND_ATTACHMENT,  // scroll, fixed, local
    CSS_VALUE_GROUP_BGROUND_CLIP,        // border-box, padding-box, content-box
    CSS_VALUE_GROUP_BGROUND_BLEND,       // multiply, overlay, normal
    CSS_VALUE_GROUP_BGROUND_REPEAT,      // repeat, no-repeat, round, space
    CSS_VALUE_GROUP_TABLE_LAYOUT,        // auto, fixed
    CSS_VALUE_GROUP_BORDER_COLLAPSE,     // collapse, separate
    CSS_VALUE_GROUP_EMPTY_CELLS,         // show, hide
    CSS_VALUE_GROUP_SPECIAL_TYPE,        // _length, _percentage, _number, _integer, _angle
    CSS_VALUE_GROUP_MISC,                // other values that don't fit clear categories
    CSS_VALUE_GROUP_RADINT,              // Radiant specific values
} CssEnumGroup;

typedef enum CssEnum : int16_t {
    CSS_VALUE__UNDEF= 0,
    CSS_VALUE__LENGTH,
    CSS_VALUE__PERCENTAGE,
    CSS_VALUE__NUMBER,
    CSS_VALUE__INTEGER,
    CSS_VALUE__ANGLE,
    CSS_VALUE_INITIAL,
    CSS_VALUE_INHERIT,
    CSS_VALUE_UNSET,
    CSS_VALUE_REVERT,
    CSS_VALUE_FLEX_START,
    CSS_VALUE_FLEX_END,
    CSS_VALUE_CENTER,
    CSS_VALUE_SPACE_BETWEEN,
    CSS_VALUE_SPACE_AROUND,
    CSS_VALUE_STRETCH,
    CSS_VALUE_BASELINE,
    CSS_VALUE_AUTO,
    CSS_VALUE_TEXT_BOTTOM,
    CSS_VALUE_ALPHABETIC,
    CSS_VALUE_IDEOGRAPHIC,
    CSS_VALUE_MIDDLE,
    CSS_VALUE_CENTRAL,
    CSS_VALUE_MATHEMATICAL,
    CSS_VALUE_TEXT_TOP,
    CSS_VALUE_SUB,
    CSS_VALUE_SUPER,
    CSS_VALUE_TOP,
    CSS_VALUE_BOTTOM,
    CSS_VALUE_FIRST,
    CSS_VALUE_LAST,
    CSS_VALUE_THIN,
    CSS_VALUE_MEDIUM,
    CSS_VALUE_THICK,
    CSS_VALUE_NONE,
    CSS_VALUE_HIDDEN,
    CSS_VALUE_DOTTED,
    CSS_VALUE_DASHED,
    CSS_VALUE_SOLID,
    CSS_VALUE_DOUBLE,
    CSS_VALUE_GROOVE,
    CSS_VALUE_RIDGE,
    CSS_VALUE_INSET,
    CSS_VALUE_OUTSET,
    CSS_VALUE_CONTENT_BOX,
    CSS_VALUE_BORDER_BOX,
    CSS_VALUE_INLINE_START,
    CSS_VALUE_INLINE_END,
    CSS_VALUE_BLOCK_START,
    CSS_VALUE_BLOCK_END,
    CSS_VALUE_LEFT,
    CSS_VALUE_RIGHT,
    CSS_VALUE_CURRENTCOLOR,
    CSS_VALUE_TRANSPARENT,
    CSS_VALUE_HEX,
    CSS_VALUE_ALICEBLUE,
    CSS_VALUE_ANTIQUEWHITE,
    CSS_VALUE_AQUA,
    CSS_VALUE_AQUAMARINE,
    CSS_VALUE_AZURE,
    CSS_VALUE_BEIGE,
    CSS_VALUE_BISQUE,
    CSS_VALUE_BLACK,
    CSS_VALUE_BLANCHEDALMOND,
    CSS_VALUE_BLUE,
    CSS_VALUE_BLUEVIOLET,
    CSS_VALUE_BROWN,
    CSS_VALUE_BURLYWOOD,
    CSS_VALUE_CADETBLUE,
    CSS_VALUE_CHARTREUSE,
    CSS_VALUE_CHOCOLATE,
    CSS_VALUE_CORAL,
    CSS_VALUE_CORNFLOWERBLUE,
    CSS_VALUE_CORNSILK,
    CSS_VALUE_CRIMSON,
    CSS_VALUE_CYAN,
    CSS_VALUE_DARKBLUE,
    CSS_VALUE_DARKCYAN,
    CSS_VALUE_DARKGOLDENROD,
    CSS_VALUE_DARKGRAY,
    CSS_VALUE_DARKGREEN,
    CSS_VALUE_DARKGREY,
    CSS_VALUE_DARKKHAKI,
    CSS_VALUE_DARKMAGENTA,
    CSS_VALUE_DARKOLIVEGREEN,
    CSS_VALUE_DARKORANGE,
    CSS_VALUE_DARKORCHID,
    CSS_VALUE_DARKRED,
    CSS_VALUE_DARKSALMON,
    CSS_VALUE_DARKSEAGREEN,
    CSS_VALUE_DARKSLATEBLUE,
    CSS_VALUE_DARKSLATEGRAY,
    CSS_VALUE_DARKSLATEGREY,
    CSS_VALUE_DARKTURQUOISE,
    CSS_VALUE_DARKVIOLET,
    CSS_VALUE_DEEPPINK,
    CSS_VALUE_DEEPSKYBLUE,
    CSS_VALUE_DIMGRAY,
    CSS_VALUE_DIMGREY,
    CSS_VALUE_DODGERBLUE,
    CSS_VALUE_FIREBRICK,
    CSS_VALUE_FLORALWHITE,
    CSS_VALUE_FORESTGREEN,
    CSS_VALUE_FUCHSIA,
    CSS_VALUE_GAINSBORO,
    CSS_VALUE_GHOSTWHITE,
    CSS_VALUE_GOLD,
    CSS_VALUE_GOLDENROD,
    CSS_VALUE_GRAY,
    CSS_VALUE_GREEN,
    CSS_VALUE_GREENYELLOW,
    CSS_VALUE_GREY,
    CSS_VALUE_HONEYDEW,
    CSS_VALUE_HOTPINK,
    CSS_VALUE_INDIANRED,
    CSS_VALUE_INDIGO,
    CSS_VALUE_IVORY,
    CSS_VALUE_KHAKI,
    CSS_VALUE_LAVENDER,
    CSS_VALUE_LAVENDERBLUSH,
    CSS_VALUE_LAWNGREEN,
    CSS_VALUE_LEMONCHIFFON,
    CSS_VALUE_LIGHTBLUE,
    CSS_VALUE_LIGHTCORAL,
    CSS_VALUE_LIGHTCYAN,
    CSS_VALUE_LIGHTGOLDENRODYELLOW,
    CSS_VALUE_LIGHTGRAY,
    CSS_VALUE_LIGHTGREEN,
    CSS_VALUE_LIGHTGREY,
    CSS_VALUE_LIGHTPINK,
    CSS_VALUE_LIGHTSALMON,
    CSS_VALUE_LIGHTSEAGREEN,
    CSS_VALUE_LIGHTSKYBLUE,
    CSS_VALUE_LIGHTSLATEGRAY,
    CSS_VALUE_LIGHTSLATEGREY,
    CSS_VALUE_LIGHTSTEELBLUE,
    CSS_VALUE_LIGHTYELLOW,
    CSS_VALUE_LIME,
    CSS_VALUE_LIMEGREEN,
    CSS_VALUE_LINEN,
    CSS_VALUE_MAGENTA,
    CSS_VALUE_MAROON,
    CSS_VALUE_MEDIUMAQUAMARINE,
    CSS_VALUE_MEDIUMBLUE,
    CSS_VALUE_MEDIUMORCHID,
    CSS_VALUE_MEDIUMPURPLE,
    CSS_VALUE_MEDIUMSEAGREEN,
    CSS_VALUE_MEDIUMSLATEBLUE,
    CSS_VALUE_MEDIUMSPRINGGREEN,
    CSS_VALUE_MEDIUMTURQUOISE,
    CSS_VALUE_MEDIUMVIOLETRED,
    CSS_VALUE_MIDNIGHTBLUE,
    CSS_VALUE_MINTCREAM,
    CSS_VALUE_MISTYROSE,
    CSS_VALUE_MOCCASIN,
    CSS_VALUE_NAVAJOWHITE,
    CSS_VALUE_NAVY,
    CSS_VALUE_OLDLACE,
    CSS_VALUE_OLIVE,
    CSS_VALUE_OLIVEDRAB,
    CSS_VALUE_ORANGE,
    CSS_VALUE_ORANGERED,
    CSS_VALUE_ORCHID,
    CSS_VALUE_PALEGOLDENROD,
    CSS_VALUE_PALEGREEN,
    CSS_VALUE_PALETURQUOISE,
    CSS_VALUE_PALEVIOLETRED,
    CSS_VALUE_PAPAYAWHIP,
    CSS_VALUE_PEACHPUFF,
    CSS_VALUE_PERU,
    CSS_VALUE_PINK,
    CSS_VALUE_PLUM,
    CSS_VALUE_POWDERBLUE,
    CSS_VALUE_PURPLE,
    CSS_VALUE_REBECCAPURPLE,
    CSS_VALUE_RED,
    CSS_VALUE_ROSYBROWN,
    CSS_VALUE_ROYALBLUE,
    CSS_VALUE_SADDLEBROWN,
    CSS_VALUE_SALMON,
    CSS_VALUE_SANDYBROWN,
    CSS_VALUE_SEAGREEN,
    CSS_VALUE_SEASHELL,
    CSS_VALUE_SIENNA,
    CSS_VALUE_SILVER,
    CSS_VALUE_SKYBLUE,
    CSS_VALUE_SLATEBLUE,
    CSS_VALUE_SLATEGRAY,
    CSS_VALUE_SLATEGREY,
    CSS_VALUE_SNOW,
    CSS_VALUE_SPRINGGREEN,
    CSS_VALUE_STEELBLUE,
    CSS_VALUE_TAN,
    CSS_VALUE_TEAL,
    CSS_VALUE_THISTLE,
    CSS_VALUE_TOMATO,
    CSS_VALUE_TURQUOISE,
    CSS_VALUE_VIOLET,
    CSS_VALUE_WHEAT,
    CSS_VALUE_WHITE,
    CSS_VALUE_WHITESMOKE,
    CSS_VALUE_YELLOW,
    CSS_VALUE_YELLOWGREEN,
    CSS_VALUE_CANVAS,
    CSS_VALUE_CANVASTEXT,
    CSS_VALUE_LINKTEXT,
    CSS_VALUE_VISITEDTEXT,
    CSS_VALUE_ACTIVETEXT,
    CSS_VALUE_BUTTONFACE,
    CSS_VALUE_BUTTONTEXT,
    CSS_VALUE_BUTTONBORDER,
    CSS_VALUE_FIELD,
    CSS_VALUE_FIELDTEXT,
    CSS_VALUE_HIGHLIGHT,
    CSS_VALUE_HIGHLIGHTTEXT,
    CSS_VALUE_SELECTEDITEM,
    CSS_VALUE_SELECTEDITEMTEXT,
    CSS_VALUE_MARK,
    CSS_VALUE_MARKTEXT,
    CSS_VALUE_GRAYTEXT,
    CSS_VALUE_ACCENTCOLOR,
    CSS_VALUE_ACCENTCOLORTEXT,
    CSS_VALUE_RGB,
    CSS_VALUE_RGBA,
    CSS_VALUE_HSL,
    CSS_VALUE_HSLA,
    CSS_VALUE_HWB,
    CSS_VALUE_LAB,
    CSS_VALUE_LCH,
    CSS_VALUE_OKLAB,
    CSS_VALUE_OKLCH,
    CSS_VALUE_COLOR,
    CSS_VALUE_HAND,
    CSS_VALUE_POINTER,
    CSS_VALUE_TEXT,
    CSS_VALUE_WAIT,
    CSS_VALUE_PROGRESS,
    CSS_VALUE_GRAB,
    CSS_VALUE_GRABBING,
    CSS_VALUE_MOVE,
    CSS_VALUE_LTR,
    CSS_VALUE_RTL,
    CSS_VALUE_BLOCK,
    CSS_VALUE_INLINE,
    CSS_VALUE_RUN_IN,
    CSS_VALUE_FLOW,
    CSS_VALUE_FLOW_ROOT,
    CSS_VALUE_TABLE,
    CSS_VALUE_FLEX,
    CSS_VALUE_GRID,
    CSS_VALUE_RUBY,
    CSS_VALUE_LIST_ITEM,
    CSS_VALUE_TABLE_ROW_GROUP,
    CSS_VALUE_TABLE_HEADER_GROUP,
    CSS_VALUE_TABLE_FOOTER_GROUP,
    CSS_VALUE_TABLE_ROW,
    CSS_VALUE_TABLE_CELL,
    CSS_VALUE_TABLE_COLUMN_GROUP,
    CSS_VALUE_TABLE_COLUMN,
    CSS_VALUE_TABLE_CAPTION,
    CSS_VALUE_RUBY_BASE,
    CSS_VALUE_RUBY_TEXT,
    CSS_VALUE_RUBY_BASE_CONTAINER,
    CSS_VALUE_RUBY_TEXT_CONTAINER,
    CSS_VALUE_CONTENTS,
    CSS_VALUE_INLINE_BLOCK,
    CSS_VALUE_INLINE_TABLE,
    CSS_VALUE_INLINE_FLEX,
    CSS_VALUE_INLINE_GRID,
    CSS_VALUE_HANGING,
    CSS_VALUE_CONTENT,
    CSS_VALUE_ROW,
    CSS_VALUE_ROW_REVERSE,
    CSS_VALUE_COLUMN,
    CSS_VALUE_COLUMN_REVERSE,
    CSS_VALUE_NOWRAP,
    CSS_VALUE_WRAP,
    CSS_VALUE_WRAP_REVERSE,
    CSS_VALUE_SNAP_BLOCK,
    CSS_VALUE_START,
    CSS_VALUE_END,
    CSS_VALUE_NEAR,
    CSS_VALUE_SNAP_INLINE,
    CSS_VALUE_REGION,
    CSS_VALUE_PAGE,
    CSS_VALUE_SERIF,
    CSS_VALUE_SANS_SERIF,
    CSS_VALUE_CURSIVE,
    CSS_VALUE_FANTASY,
    CSS_VALUE_MONOSPACE,
    CSS_VALUE_SYSTEM_UI,
    CSS_VALUE_EMOJI,
    CSS_VALUE_MATH,
    CSS_VALUE_FANGSONG,
    CSS_VALUE_UI_SERIF,
    CSS_VALUE_UI_SANS_SERIF,
    CSS_VALUE_UI_MONOSPACE,
    CSS_VALUE_UI_ROUNDED,
    CSS_VALUE_XX_SMALL,
    CSS_VALUE_X_SMALL,
    CSS_VALUE_SMALL,
    CSS_VALUE_LARGE,
    CSS_VALUE_X_LARGE,
    CSS_VALUE_XX_LARGE,
    CSS_VALUE_XXX_LARGE,
    CSS_VALUE_LARGER,
    CSS_VALUE_SMALLER,
    CSS_VALUE_NORMAL,
    CSS_VALUE_ULTRA_CONDENSED,
    CSS_VALUE_EXTRA_CONDENSED,
    CSS_VALUE_CONDENSED,
    CSS_VALUE_SEMI_CONDENSED,
    CSS_VALUE_SEMI_EXPANDED,
    CSS_VALUE_EXPANDED,
    CSS_VALUE_EXTRA_EXPANDED,
    CSS_VALUE_ULTRA_EXPANDED,
    CSS_VALUE_ITALIC,
    CSS_VALUE_OBLIQUE,
    CSS_VALUE_BOLD,
    CSS_VALUE_BOLDER,
    CSS_VALUE_LIGHTER,
    CSS_VALUE_FORCE_END,
    CSS_VALUE_ALLOW_END,
    CSS_VALUE_MIN_CONTENT,
    CSS_VALUE_MAX_CONTENT,
    CSS_VALUE_MANUAL,
    CSS_VALUE_LOOSE,
    CSS_VALUE_STRICT,
    CSS_VALUE_ANYWHERE,
    CSS_VALUE_VISIBLE,
    CSS_VALUE_CLIP,
    CSS_VALUE_SCROLL,
    CSS_VALUE_BREAK_WORD,
    CSS_VALUE_STATIC,
    CSS_VALUE_RELATIVE,
    CSS_VALUE_ABSOLUTE,
    CSS_VALUE_STICKY,
    CSS_VALUE_FIXED,
    CSS_VALUE_JUSTIFY,
    CSS_VALUE_MATCH_PARENT,
    CSS_VALUE_JUSTIFY_ALL,
    CSS_VALUE_ALL,
    CSS_VALUE_DIGITS,
    CSS_VALUE_UNDERLINE,
    CSS_VALUE_OVERLINE,
    CSS_VALUE_LINE_THROUGH,
    CSS_VALUE_BLINK,
    CSS_VALUE_WAVY,
    CSS_VALUE_EACH_LINE,
    CSS_VALUE_INTER_WORD,
    CSS_VALUE_INTER_CHARACTER,
    CSS_VALUE_MIXED,
    CSS_VALUE_UPRIGHT,
    CSS_VALUE_SIDEWAYS,
    CSS_VALUE_ELLIPSIS,
    CSS_VALUE_CAPITALIZE,
    CSS_VALUE_UPPERCASE,
    CSS_VALUE_LOWERCASE,
    CSS_VALUE_FULL_WIDTH,
    CSS_VALUE_FULL_SIZE_KANA,
    CSS_VALUE_EMBED,
    CSS_VALUE_ISOLATE,
    CSS_VALUE_BIDI_OVERRIDE,
    CSS_VALUE_ISOLATE_OVERRIDE,
    CSS_VALUE_PLAINTEXT,
    CSS_VALUE_COLLAPSE,
    CSS_VALUE_PRE,
    CSS_VALUE_PRE_WRAP,
    CSS_VALUE_BREAK_SPACES,
    CSS_VALUE_PRE_LINE,
    CSS_VALUE_KEEP_ALL,
    CSS_VALUE_BREAK_ALL,
    CSS_VALUE_BOTH,
    CSS_VALUE_MINIMUM,
    CSS_VALUE_MAXIMUM,
    CSS_VALUE_CLEAR,
    CSS_VALUE_HORIZONTAL_TB,
    CSS_VALUE_VERTICAL_RL,
    CSS_VALUE_VERTICAL_LR,
    CSS_VALUE_SIDEWAYS_RL,
    CSS_VALUE_SIDEWAYS_LR,
    // List style types
    CSS_VALUE_DISC,
    CSS_VALUE_CIRCLE,
    CSS_VALUE_SQUARE,
    CSS_VALUE_DECIMAL,
    CSS_VALUE_LOWER_ROMAN,
    CSS_VALUE_UPPER_ROMAN,
    CSS_VALUE_LOWER_ALPHA,
    CSS_VALUE_UPPER_ALPHA,
    // Flex layout
    CSS_VALUE_SPACE_EVENLY,
    // Background properties
    CSS_VALUE_CONTAIN,  // background-size contain
    CSS_VALUE_COVER,  // background-size cover
    CSS_VALUE_LOCAL,  // background-attachment local
    CSS_VALUE_PADDING_BOX,  // background-origin/clip padding-box
    CSS_VALUE_MULTIPLY,  // background-blend-mode multiply
    CSS_VALUE_OVERLAY,  // background-blend-mode overlay
    CSS_VALUE_ROUND,  // background-repeat round
    CSS_VALUE_SPACE,  // background-repeat space
    // Table properties
    CSS_VALUE_COLLAPSE_TABLE,  // border-collapse collapse
    CSS_VALUE_SEPARATE,  // border-collapse separate
    CSS_VALUE_HIDE,  // empty-cells hide
    CSS_VALUE_SHOW,  // empty-cells show
    // Grid layout
    CSS_VALUE_FIT_CONTENT,
    CSS_VALUE_FR,
    CSS_VALUE_DENSE,
    // Radiant extensions
    CSS_VALUE__REPLACED,
    CSS_VALUE__LAST_ENTRY
} CssEnum;

// radiant specific CSS display values
#define RDT_DISPLAY_TEXT                CSS_VALUE_TEXT
#define RDT_DISPLAY_REPLACED            CSS_VALUE__REPLACED

// Enum definitions needed by flex system
typedef enum { VIS_VISIBLE, VIS_HIDDEN, VIS_COLLAPSE } Visibility;
typedef enum { POS_STATIC, POS_ABSOLUTE } PositionType;
typedef enum { WM_HORIZONTAL_TB, WM_VERTICAL_RL, WM_VERTICAL_LR } WritingMode;
typedef enum { TD_LTR, TD_RTL } TextDirection;

// Lexbor type replacements - define stubs for removed lexbor dependencies
typedef int lxb_status_t;
typedef unsigned char lxb_char_t;
#define LXB_STATUS_OK 0

// Length/percentage value structure (replacing lexbor's lxb_css_value_length_percentage_t)
typedef struct {
    CssEnum type;  // CSS_VALUE__LENGTH or CSS_VALUE__PERCENTAGE
    union {
        struct {
            float num;
            CssUnit unit;
            bool is_float;
        } length;
        struct {
            float num;
        } percentage;
    } u;
} lxb_css_value_length_percentage_t;

// Color value structure stub
typedef struct {
    CssEnum type;
    uint32_t rgba;
} lxb_css_value_color_t;

// Line height property structure (replacing lexbor's lxb_css_property_line_height_t)
typedef struct {
    CssEnum type;  // CSS_VALUE__NUMBER, CSS_VALUE__LENGTH, CSS_VALUE__PERCENTAGE, CSS_VALUE_NORMAL
    union {
        struct {
            float num;
        } number;
        struct {
            float num;
            CssUnit unit;
            bool is_float;
        } length;
        struct {
            float num;
        } percentage;
    } u;
} lxb_css_property_line_height_t;

// Now include headers that depend on these constants
#include "event.hpp"
#include "flex.hpp"

typedef struct CssEnumInfo{
    const char *name;
    size_t     length;
    CssEnum  unique;
    CssEnumGroup group;
} CssEnumInfo;

const CssEnumInfo* css_value_by_id(CssEnum id);
CssEnum css_value_by_name(const char* name);

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

typedef enum {
    RDT_VIEW_NONE = 0,
    RDT_VIEW_TEXT,
    RDT_VIEW_BR,
    // ViewSpan
    RDT_VIEW_INLINE,
    // ViewBlock
    RDT_VIEW_INLINE_BLOCK,
    RDT_VIEW_BLOCK,
    RDT_VIEW_LIST_ITEM,
    RDT_VIEW_TABLE,
    RDT_VIEW_TABLE_ROW_GROUP,
    RDT_VIEW_TABLE_ROW,
    RDT_VIEW_TABLE_CELL,
} ViewType;

typedef struct View View;
typedef struct ViewBlock ViewBlock;

typedef struct {
    CssEnum outer;
    CssEnum inner;
} DisplayValue;

typedef struct {
    char* family;  // font family name
    float font_size;  // font size in pixels, scaled by pixel_ratio
    CssEnum font_style;
    CssEnum font_weight;
    CssEnum text_deco; // CSS text decoration
    // derived font properties
    float space_width;  // width of a space character of the current font
    float ascender;    // font ascender in pixels
    float descender;   // font descender in pixels
    float font_height; // font height in pixels
    bool has_kerning;  // whether the font has kerning
} FontProp;

typedef struct {
    CssEnum cursor;
    Color color;
    CssEnum vertical_align;
    float opacity;  // CSS opacity value (0.0 to 1.0)
} InlineProp;

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
    Color top_color, right_color, bottom_color, left_color;
    int32_t top_color_specificity, right_color_specificity, bottom_color_specificity, left_color_specificity;
    Corner radius;
} BorderProp;

typedef struct {
    Color color; // background color
    char* image; // background image path
    char* repeat; // repeat behavior
    char* position; // positioning of background image
} BackgroundProp;

typedef struct {
    Margin margin;
    Spacing padding;
    BorderProp* border;
    BackgroundProp* background;
} BoundaryProp;

typedef struct {
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

typedef struct {
    CssEnum text_align;
    lxb_css_property_line_height_t *line_height;
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
    float given_width, given_height;  // CSS specified width/height values
    CssEnum given_width_type;
    // REMOVED DUPLICATE FIELDS: clear and float_prop are in PositionProp above
} BlockProp;


typedef struct ViewGroup ViewGroup;

// view always has x, y, wd, hg; otherwise, it is a property group
struct View {
    ViewType type;
    DomNode* node;  // DOM node (DomElement*, DomText*, or DomComment* via inheritance)
    View* next;
    ViewGroup* parent;  // corrected the type to ViewGroup
    float x, y, width, height;  // (x, y) relative to the BORDER box of parent block, and (width, height) forms the BORDER box of current block

    inline bool is_group() { return type >= RDT_VIEW_INLINE; }

    inline bool is_inline() { return type == RDT_VIEW_TEXT || type == RDT_VIEW_INLINE || type == RDT_VIEW_INLINE_BLOCK; }

    inline bool is_block() {
        return type == RDT_VIEW_BLOCK || type == RDT_VIEW_INLINE_BLOCK || type == RDT_VIEW_LIST_ITEM ||
            type == RDT_VIEW_TABLE || type == RDT_VIEW_TABLE_ROW_GROUP || type == RDT_VIEW_TABLE_ROW || type == RDT_VIEW_TABLE_CELL;
    }

    View* previous_view();
    const char* name();

    // DOM node access helpers - forward to node's methods for backward compatibility
    // These provide syntactic compatibility with old view->node->method() patterns
    inline const char* node_name() const { return node ? node->name() : "#null"; }
    inline const char* node_tag_name() const {
        DomElement* elem = node ? node->as_element() : nullptr;
        return elem ? elem->tag_name : nullptr;
    }
    inline const char* node_get_attribute(const char* attr_name) const {
        return node ? node->get_attribute(attr_name) : nullptr;
    }
    inline unsigned char* node_text_data() const {
        DomText* text = node ? node->as_text() : nullptr;
        return text ? (unsigned char*)text->text : nullptr;
    }
    inline DomNode* node_first_child() const {
        return node ? node->first_child : nullptr;
    }
    inline DomNode* node_next_sibling() const {
        return node ? node->next_sibling : nullptr;
    }
    inline bool node_is_element() const {
        return node ? node->is_element() : false;
    }
    inline bool node_is_text() const {
        return node ? node->is_text() : false;
    }
    inline DomElement* node_as_element() const {
        return node ? node->as_element() : nullptr;
    }
    inline DomNodeType node_get_type() const {
        return node ? node->type() : (DomNodeType)0;
    }
    inline uintptr_t node_tag() const {
        DomElement* elem = node ? node->as_element() : nullptr;
        return elem ? elem->tag_id : 0;
    }
};

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

typedef struct ViewText : View {
    TextRect *rect;  // first text rect
    FontProp *font;  // font for this text
    Color color;     // text color (for PDF text fill color)
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
    int align_self;  // AlignType or CSS_VALUE_*
    int order;
    bool flex_basis_is_percent;

    // Additional flex item properties from old implementation
    float aspect_ratio;
    float baseline_offset;

    // Percentage flags for constraints
    bool width_is_percent;
    bool height_is_percent;
    bool min_width_is_percent;
    bool max_width_is_percent;
    bool min_height_is_percent;
    bool max_height_is_percent;

    // Min/max constraints
    float min_width, max_width;
    float min_height, max_height;

    // Position and visibility (from old FlexItem)
    int position;  // PositionType
    int visibility;  // Visibility

    // Grid item properties (following flex pattern)
    int grid_row_start;          // Grid row start line
    int grid_row_end;            // Grid row end line
    int grid_column_start;       // Grid column start line
    int grid_column_end;         // Grid column end line
    char* grid_area;             // Named grid area
    int justify_self;            // Item-specific justify alignment (CSS_VALUE_*)
    int align_self_grid;         // Item-specific align alignment for grid (CSS_VALUE_*)

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

typedef struct {
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
    WritingMode writing_mode;
    TextDirection text_direction;
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
    int computed_row_count;
    int computed_column_count;
    // Grid areas
    GridArea* grid_areas;
    int area_count;
    int allocated_areas;

    // Advanced features
    bool is_dense_packing;       // grid-auto-flow: dense
} GridProp;

typedef struct {
    ImageSurface* img;  // image surface
    Document* doc;  // iframe document
    FlexProp* flex;
    GridProp* grid;
} EmbedProp;

struct ViewBlock : ViewSpan {
    float content_width, content_height;  // width and height of the child content including padding
    BlockProp* blk;  // block specific style properties
    ScrollProp* scroller;  // handles overflow
    // block content related properties for flexbox, image, iframe
    EmbedProp* embed;
    // positioning properties for CSS positioning
    PositionProp* position;

    // Child navigation for flex layout tests
    ViewBlock* first_child;
    ViewBlock* last_child;
    ViewBlock* next_sibling;
    ViewBlock* prev_sibling;
};

// Table-specific lightweight subclasses (no additional fields yet)
// These keep table concerns out of the base ViewBlock while preserving layout/render compatibility.
typedef struct ViewTable : ViewBlock {
    // Table layout algorithm mode
    enum {
        TABLE_LAYOUT_AUTO = 0,    // Content-based width calculation (default)
        TABLE_LAYOUT_FIXED = 1    // Fixed width calculation based on first row/col elements
    } table_layout;

    // Border model and spacing
    // border_collapse=false => separate borders, apply border-spacing gaps
    // border_collapse=true  => collapsed borders, no gaps between cells
    bool border_collapse;
    float border_spacing_h; // horizontal spacing between columns (px)
    float border_spacing_v; // vertical spacing between rows (px)

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

    // Fixed layout height distribution
    int fixed_row_height;   // Height per row for table-layout:fixed with explicit height (0=auto)

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

typedef enum HtmlVersion {
    HTML5 = 1,              // HTML5
    HTML4_01_STRICT,        // HTML4.01 Strict
    HTML4_01_TRANSITIONAL,  // HTML4.01 Transitional
    HTML4_01_FRAMESET,      // HTML4.01 Frameset
    HTML_QUIRKS,            // Legacy HTML or missing DOCTYPE
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
} BlockBlot;

typedef struct {
    CssEnum list_style_type;
    int item_index;
} ListBlot;

typedef struct {
    GLFWwindow *window;    // current window
    float window_width;    // window pixel width
    float window_height;   // window pixel height
    ImageSurface* surface;  // rendering surface of a window

    // font handling
    FcConfig *font_config;
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
    Document* document;     // current document
    MouseState mouse_state; // current mouse state
} UiContext;

extern FT_Face load_styled_font(UiContext* uicon, const char* font_name, FontProp* font_style);
extern FT_GlyphSlot load_glyph(UiContext* uicon, FT_Face face, FontProp* font_style, uint32_t codepoint, bool for_rendering);
extern void setup_font(UiContext* uicon, FontBox *fbox, FontProp *fprop);
extern ImageSurface* load_image(UiContext* uicon, const char *file_path);
