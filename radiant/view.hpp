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
    HTM_TAG__UNDEF              = 0x0000,
    HTM_TAG__END_OF_FILE        = 0x0001,
    HTM_TAG__TEXT               = 0x0002,
    HTM_TAG__DOCUMENT           = 0x0003,
    HTM_TAG__EM_COMMENT         = 0x0004,
    HTM_TAG__EM_DOCTYPE         = 0x0005,
    HTM_TAG_A                   = 0x0006,
    HTM_TAG_ABBR                = 0x0007,
    HTM_TAG_ACRONYM             = 0x0008,
    HTM_TAG_ADDRESS             = 0x0009,
    HTM_TAG_ALTGLYPH            = 0x000a,
    HTM_TAG_ALTGLYPHDEF         = 0x000b,
    HTM_TAG_ALTGLYPHITEM        = 0x000c,
    HTM_TAG_ANIMATECOLOR        = 0x000d,
    HTM_TAG_ANIMATEMOTION       = 0x000e,
    HTM_TAG_ANIMATETRANSFORM    = 0x000f,
    HTM_TAG_ANNOTATION_XML      = 0x0010,
    HTM_TAG_APPLET              = 0x0011,
    HTM_TAG_AREA                = 0x0012,
    HTM_TAG_ARTICLE             = 0x0013,
    HTM_TAG_ASIDE               = 0x0014,
    HTM_TAG_AUDIO               = 0x0015,
    HTM_TAG_B                   = 0x0016,
    HTM_TAG_BASE                = 0x0017,
    HTM_TAG_BASEFONT            = 0x0018,
    HTM_TAG_BDI                 = 0x0019,
    HTM_TAG_BDO                 = 0x001a,
    HTM_TAG_BGSOUND             = 0x001b,
    HTM_TAG_BIG                 = 0x001c,
    HTM_TAG_BLINK               = 0x001d,
    HTM_TAG_BLOCKQUOTE          = 0x001e,
    HTM_TAG_BODY                = 0x001f,
    HTM_TAG_BR                  = 0x0020,
    HTM_TAG_BUTTON              = 0x0021,
    HTM_TAG_CANVAS              = 0x0022,
    HTM_TAG_CAPTION             = 0x0023,
    HTM_TAG_CENTER              = 0x0024,
    HTM_TAG_CITE                = 0x0025,
    HTM_TAG_CLIPPATH            = 0x0026,
    HTM_TAG_CODE                = 0x0027,
    HTM_TAG_COL                 = 0x0028,
    HTM_TAG_COLGROUP            = 0x0029,
    HTM_TAG_DATA                = 0x002a,
    HTM_TAG_DATALIST            = 0x002b,
    HTM_TAG_DD                  = 0x002c,
    HTM_TAG_DEL                 = 0x002d,
    HTM_TAG_DESC                = 0x002e,
    HTM_TAG_DETAILS             = 0x002f,
    HTM_TAG_DFN                 = 0x0030,
    HTM_TAG_DIALOG              = 0x0031,
    HTM_TAG_DIR                 = 0x0032,
    HTM_TAG_DIV                 = 0x0033,
    HTM_TAG_DL                  = 0x0034,
    HTM_TAG_DT                  = 0x0035,
    HTM_TAG_EM                  = 0x0036,
    HTM_TAG_EMBED               = 0x0037,
    HTM_TAG_FEBLEND             = 0x0038,
    HTM_TAG_FECOLORMATRIX       = 0x0039,
    HTM_TAG_FECOMPONENTTRANSFER = 0x003a,
    HTM_TAG_FECOMPOSITE         = 0x003b,
    HTM_TAG_FECONVOLVEMATRIX    = 0x003c,
    HTM_TAG_FEDIFFUSELIGHTING   = 0x003d,
    HTM_TAG_FEDISPLACEMENTMAP   = 0x003e,
    HTM_TAG_FEDISTANTLIGHT      = 0x003f,
    HTM_TAG_FEDROPSHADOW        = 0x0040,
    HTM_TAG_FEFLOOD             = 0x0041,
    HTM_TAG_FEFUNCA             = 0x0042,
    HTM_TAG_FEFUNCB             = 0x0043,
    HTM_TAG_FEFUNCG             = 0x0044,
    HTM_TAG_FEFUNCR             = 0x0045,
    HTM_TAG_FEGAUSSIANBLUR      = 0x0046,
    HTM_TAG_FEIMAGE             = 0x0047,
    HTM_TAG_FEMERGE             = 0x0048,
    HTM_TAG_FEMERGENODE         = 0x0049,
    HTM_TAG_FEMORPHOLOGY        = 0x004a,
    HTM_TAG_FEOFFSET            = 0x004b,
    HTM_TAG_FEPOINTLIGHT        = 0x004c,
    HTM_TAG_FESPECULARLIGHTING  = 0x004d,
    HTM_TAG_FESPOTLIGHT         = 0x004e,
    HTM_TAG_FETILE              = 0x004f,
    HTM_TAG_FETURBULENCE        = 0x0050,
    HTM_TAG_FIELDSET            = 0x0051,
    HTM_TAG_FIGCAPTION          = 0x0052,
    HTM_TAG_FIGURE              = 0x0053,
    HTM_TAG_FONT                = 0x0054,
    HTM_TAG_FOOTER              = 0x0055,
    HTM_TAG_FOREIGNOBJECT       = 0x0056,
    HTM_TAG_FORM                = 0x0057,
    HTM_TAG_FRAME               = 0x0058,
    HTM_TAG_FRAMESET            = 0x0059,
    HTM_TAG_GLYPHREF            = 0x005a,
    HTM_TAG_H1                  = 0x005b,
    HTM_TAG_H2                  = 0x005c,
    HTM_TAG_H3                  = 0x005d,
    HTM_TAG_H4                  = 0x005e,
    HTM_TAG_H5                  = 0x005f,
    HTM_TAG_H6                  = 0x0060,
    HTM_TAG_HEAD                = 0x0061,
    HTM_TAG_HEADER              = 0x0062,
    HTM_TAG_HGROUP              = 0x0063,
    HTM_TAG_HR                  = 0x0064,
    HTM_TAG_HTML                = 0x0065,
    HTM_TAG_I                   = 0x0066,
    HTM_TAG_IFRAME              = 0x0067,
    HTM_TAG_IMAGE               = 0x0068,
    HTM_TAG_IMG                 = 0x0069,
    HTM_TAG_INPUT               = 0x006a,
    HTM_TAG_INS                 = 0x006b,
    HTM_TAG_ISINDEX             = 0x006c,
    HTM_TAG_KBD                 = 0x006d,
    HTM_TAG_KEYGEN              = 0x006e,
    HTM_TAG_LABEL               = 0x006f,
    HTM_TAG_LEGEND              = 0x0070,
    HTM_TAG_LI                  = 0x0071,
    HTM_TAG_LINEARGRADIENT      = 0x0072,
    HTM_TAG_LINK                = 0x0073,
    HTM_TAG_LISTING             = 0x0074,
    HTM_TAG_MAIN                = 0x0075,
    HTM_TAG_MALIGNMARK          = 0x0076,
    HTM_TAG_MAP                 = 0x0077,
    HTM_TAG_MARK                = 0x0078,
    HTM_TAG_MARQUEE             = 0x0079,
    HTM_TAG_MATH                = 0x007a,
    HTM_TAG_MENU                = 0x007b,
    HTM_TAG_META                = 0x007c,
    HTM_TAG_METER               = 0x007d,
    HTM_TAG_MFENCED             = 0x007e,
    HTM_TAG_MGLYPH              = 0x007f,
    HTM_TAG_MI                  = 0x0080,
    HTM_TAG_MN                  = 0x0081,
    HTM_TAG_MO                  = 0x0082,
    HTM_TAG_MS                  = 0x0083,
    HTM_TAG_MTEXT               = 0x0084,
    HTM_TAG_MULTICOL            = 0x0085,
    HTM_TAG_NAV                 = 0x0086,
    HTM_TAG_NEXTID              = 0x0087,
    HTM_TAG_NOBR                = 0x0088,
    HTM_TAG_NOEMBED             = 0x0089,
    HTM_TAG_NOFRAMES            = 0x008a,
    HTM_TAG_NOSCRIPT            = 0x008b,
    HTM_TAG_OBJECT              = 0x008c,
    HTM_TAG_OL                  = 0x008d,
    HTM_TAG_OPTGROUP            = 0x008e,
    HTM_TAG_OPTION              = 0x008f,
    HTM_TAG_OUTPUT              = 0x0090,
    HTM_TAG_P                   = 0x0091,
    HTM_TAG_PARAM               = 0x0092,
    HTM_TAG_PATH                = 0x0093,
    HTM_TAG_PICTURE             = 0x0094,
    HTM_TAG_PLAINTEXT           = 0x0095,
    HTM_TAG_PRE                 = 0x0096,
    HTM_TAG_PROGRESS            = 0x0097,
    HTM_TAG_Q                   = 0x0098,
    HTM_TAG_RADIALGRADIENT      = 0x0099,
    HTM_TAG_RB                  = 0x009a,
    HTM_TAG_RP                  = 0x009b,
    HTM_TAG_RT                  = 0x009c,
    HTM_TAG_RTC                 = 0x009d,
    HTM_TAG_RUBY                = 0x009e,
    HTM_TAG_S                   = 0x009f,
    HTM_TAG_SAMP                = 0x00a0,
    HTM_TAG_SCRIPT              = 0x00a1,
    HTM_TAG_SECTION             = 0x00a2,
    HTM_TAG_SELECT              = 0x00a3,
    HTM_TAG_SLOT                = 0x00a4,
    HTM_TAG_SMALL               = 0x00a5,
    HTM_TAG_SOURCE              = 0x00a6,
    HTM_TAG_SPACER              = 0x00a7,
    HTM_TAG_SPAN                = 0x00a8,
    HTM_TAG_STRIKE              = 0x00a9,
    HTM_TAG_STRONG              = 0x00aa,
    HTM_TAG_STYLE               = 0x00ab,
    HTM_TAG_SUB                 = 0x00ac,
    HTM_TAG_SUMMARY             = 0x00ad,
    HTM_TAG_SUP                 = 0x00ae,
    HTM_TAG_SVG                 = 0x00af,
    HTM_TAG_TABLE               = 0x00b0,
    HTM_TAG_TBODY               = 0x00b1,
    HTM_TAG_TD                  = 0x00b2,
    HTM_TAG_TEMPLATE            = 0x00b3,
    HTM_TAG_TEXTAREA            = 0x00b4,
    HTM_TAG_TEXTPATH            = 0x00b5,
    HTM_TAG_TFOOT               = 0x00b6,
    HTM_TAG_TH                  = 0x00b7,
    HTM_TAG_THEAD               = 0x00b8,
    HTM_TAG_TIME                = 0x00b9,
    HTM_TAG_TITLE               = 0x00ba,
    HTM_TAG_TR                  = 0x00bb,
    HTM_TAG_TRACK               = 0x00bc,
    HTM_TAG_TT                  = 0x00bd,
    HTM_TAG_U                   = 0x00be,
    HTM_TAG_UL                  = 0x00bf,
    HTM_TAG_VAR                 = 0x00c0,
    HTM_TAG_VIDEO               = 0x00c1,
    HTM_TAG_WBR                 = 0x00c2,
    HTM_TAG_XMP                 = 0x00c3,
    HTM_TAG__LAST_ENTRY         = 0x00c4
};

enum {
    CSS_VALUE__UNDEF               = 0x0000,
    CSS_VALUE_INITIAL              = 0x0001,
    CSS_VALUE_INHERIT              = 0x0002,
    CSS_VALUE_UNSET                = 0x0003,
    CSS_VALUE_REVERT               = 0x0004,
    CSS_VALUE_FLEX_START           = 0x0005,
    CSS_VALUE_FLEX_END             = 0x0006,
    CSS_VALUE_CENTER               = 0x0007,
    CSS_VALUE_SPACE_BETWEEN        = 0x0008,
    CSS_VALUE_SPACE_AROUND         = 0x0009,
    CSS_VALUE_STRETCH              = 0x000a,
    CSS_VALUE_BASELINE             = 0x000b,
    CSS_VALUE_AUTO                 = 0x000c,
    CSS_VALUE_TEXT_BOTTOM          = 0x000d,
    CSS_VALUE_ALPHABETIC           = 0x000e,
    CSS_VALUE_IDEOGRAPHIC          = 0x000f,
    CSS_VALUE_MIDDLE               = 0x0010,
    CSS_VALUE_CENTRAL              = 0x0011,
    CSS_VALUE_MATHEMATICAL         = 0x0012,
    CSS_VALUE_TEXT_TOP             = 0x0013,
    CSS_VALUE__LENGTH              = 0x0014,
    CSS_VALUE__PERCENTAGE          = 0x0015,
    CSS_VALUE_SUB                  = 0x0016,
    CSS_VALUE_SUPER                = 0x0017,
    CSS_VALUE_TOP                  = 0x0018,
    CSS_VALUE_BOTTOM               = 0x0019,
    CSS_VALUE_FIRST                = 0x001a,
    CSS_VALUE_LAST                 = 0x001b,
    CSS_VALUE_THIN                 = 0x001c,
    CSS_VALUE_MEDIUM               = 0x001d,
    CSS_VALUE_THICK                = 0x001e,
    CSS_VALUE_NONE                 = 0x001f,
    CSS_VALUE_HIDDEN               = 0x0020,
    CSS_VALUE_DOTTED               = 0x0021,
    CSS_VALUE_DASHED               = 0x0022,
    CSS_VALUE_SOLID                = 0x0023,
    CSS_VALUE_DOUBLE               = 0x0024,
    CSS_VALUE_GROOVE               = 0x0025,
    CSS_VALUE_RIDGE                = 0x0026,
    CSS_VALUE_INSET                = 0x0027,
    CSS_VALUE_OUTSET               = 0x0028,
    CSS_VALUE_CONTENT_BOX          = 0x0029,
    CSS_VALUE_BORDER_BOX           = 0x002a,
    CSS_VALUE_INLINE_START         = 0x002b,
    CSS_VALUE_INLINE_END           = 0x002c,
    CSS_VALUE_BLOCK_START          = 0x002d,
    CSS_VALUE_BLOCK_END            = 0x002e,
    CSS_VALUE_LEFT                 = 0x002f,
    CSS_VALUE_RIGHT                = 0x0030,
    CSS_VALUE_CURRENTCOLOR         = 0x0031,
    CSS_VALUE_TRANSPARENT          = 0x0032,
    CSS_VALUE_HEX                  = 0x0033,
    CSS_VALUE_ALICEBLUE            = 0x0034,
    CSS_VALUE_ANTIQUEWHITE         = 0x0035,
    CSS_VALUE_AQUA                 = 0x0036,
    CSS_VALUE_AQUAMARINE           = 0x0037,
    CSS_VALUE_AZURE                = 0x0038,
    CSS_VALUE_BEIGE                = 0x0039,
    CSS_VALUE_BISQUE               = 0x003a,
    CSS_VALUE_BLACK                = 0x003b,
    CSS_VALUE_BLANCHEDALMOND       = 0x003c,
    CSS_VALUE_BLUE                 = 0x003d,
    CSS_VALUE_BLUEVIOLET           = 0x003e,
    CSS_VALUE_BROWN                = 0x003f,
    CSS_VALUE_BURLYWOOD            = 0x0040,
    CSS_VALUE_CADETBLUE            = 0x0041,
    CSS_VALUE_CHARTREUSE           = 0x0042,
    CSS_VALUE_CHOCOLATE            = 0x0043,
    CSS_VALUE_CORAL                = 0x0044,
    CSS_VALUE_CORNFLOWERBLUE       = 0x0045,
    CSS_VALUE_CORNSILK             = 0x0046,
    CSS_VALUE_CRIMSON              = 0x0047,
    CSS_VALUE_CYAN                 = 0x0048,
    CSS_VALUE_DARKBLUE             = 0x0049,
    CSS_VALUE_DARKCYAN             = 0x004a,
    CSS_VALUE_DARKGOLDENROD        = 0x004b,
    CSS_VALUE_DARKGRAY             = 0x004c,
    CSS_VALUE_DARKGREEN            = 0x004d,
    CSS_VALUE_DARKGREY             = 0x004e,
    CSS_VALUE_DARKKHAKI            = 0x004f,
    CSS_VALUE_DARKMAGENTA          = 0x0050,
    CSS_VALUE_DARKOLIVEGREEN       = 0x0051,
    CSS_VALUE_DARKORANGE           = 0x0052,
    CSS_VALUE_DARKORCHID           = 0x0053,
    CSS_VALUE_DARKRED              = 0x0054,
    CSS_VALUE_DARKSALMON           = 0x0055,
    CSS_VALUE_DARKSEAGREEN         = 0x0056,
    CSS_VALUE_DARKSLATEBLUE        = 0x0057,
    CSS_VALUE_DARKSLATEGRAY        = 0x0058,
    CSS_VALUE_DARKSLATEGREY        = 0x0059,
    CSS_VALUE_DARKTURQUOISE        = 0x005a,
    CSS_VALUE_DARKVIOLET           = 0x005b,
    CSS_VALUE_DEEPPINK             = 0x005c,
    CSS_VALUE_DEEPSKYBLUE          = 0x005d,
    CSS_VALUE_DIMGRAY              = 0x005e,
    CSS_VALUE_DIMGREY              = 0x005f,
    CSS_VALUE_DODGERBLUE           = 0x0060,
    CSS_VALUE_FIREBRICK            = 0x0061,
    CSS_VALUE_FLORALWHITE          = 0x0062,
    CSS_VALUE_FORESTGREEN          = 0x0063,
    CSS_VALUE_FUCHSIA              = 0x0064,
    CSS_VALUE_GAINSBORO            = 0x0065,
    CSS_VALUE_GHOSTWHITE           = 0x0066,
    CSS_VALUE_GOLD                 = 0x0067,
    CSS_VALUE_GOLDENROD            = 0x0068,
    CSS_VALUE_GRAY                 = 0x0069,
    CSS_VALUE_GREEN                = 0x006a,
    CSS_VALUE_GREENYELLOW          = 0x006b,
    CSS_VALUE_GREY                 = 0x006c,
    CSS_VALUE_HONEYDEW             = 0x006d,
    CSS_VALUE_HOTPINK              = 0x006e,
    CSS_VALUE_INDIANRED            = 0x006f,
    CSS_VALUE_INDIGO               = 0x0070,
    CSS_VALUE_IVORY                = 0x0071,
    CSS_VALUE_KHAKI                = 0x0072,
    CSS_VALUE_LAVENDER             = 0x0073,
    CSS_VALUE_LAVENDERBLUSH        = 0x0074,
    CSS_VALUE_LAWNGREEN            = 0x0075,
    CSS_VALUE_LEMONCHIFFON         = 0x0076,
    CSS_VALUE_LIGHTBLUE            = 0x0077,
    CSS_VALUE_LIGHTCORAL           = 0x0078,
    CSS_VALUE_LIGHTCYAN            = 0x0079,
    CSS_VALUE_LIGHTGOLDENRODYELLOW = 0x007a,
    CSS_VALUE_LIGHTGRAY            = 0x007b,
    CSS_VALUE_LIGHTGREEN           = 0x007c,
    CSS_VALUE_LIGHTGREY            = 0x007d,
    CSS_VALUE_LIGHTPINK            = 0x007e,
    CSS_VALUE_LIGHTSALMON          = 0x007f,
    CSS_VALUE_LIGHTSEAGREEN        = 0x0080,
    CSS_VALUE_LIGHTSKYBLUE         = 0x0081,
    CSS_VALUE_LIGHTSLATEGRAY       = 0x0082,
    CSS_VALUE_LIGHTSLATEGREY       = 0x0083,
    CSS_VALUE_LIGHTSTEELBLUE       = 0x0084,
    CSS_VALUE_LIGHTYELLOW          = 0x0085,
    CSS_VALUE_LIME                 = 0x0086,
    CSS_VALUE_LIMEGREEN            = 0x0087,
    CSS_VALUE_LINEN                = 0x0088,
    CSS_VALUE_MAGENTA              = 0x0089,
    CSS_VALUE_MAROON               = 0x008a,
    CSS_VALUE_MEDIUMAQUAMARINE     = 0x008b,
    CSS_VALUE_MEDIUMBLUE           = 0x008c,
    CSS_VALUE_MEDIUMORCHID         = 0x008d,
    CSS_VALUE_MEDIUMPURPLE         = 0x008e,
    CSS_VALUE_MEDIUMSEAGREEN       = 0x008f,
    CSS_VALUE_MEDIUMSLATEBLUE      = 0x0090,
    CSS_VALUE_MEDIUMSPRINGGREEN    = 0x0091,
    CSS_VALUE_MEDIUMTURQUOISE      = 0x0092,
    CSS_VALUE_MEDIUMVIOLETRED      = 0x0093,
    CSS_VALUE_MIDNIGHTBLUE         = 0x0094,
    CSS_VALUE_MINTCREAM            = 0x0095,
    CSS_VALUE_MISTYROSE            = 0x0096,
    CSS_VALUE_MOCCASIN             = 0x0097,
    CSS_VALUE_NAVAJOWHITE          = 0x0098,
    CSS_VALUE_NAVY                 = 0x0099,
    CSS_VALUE_OLDLACE              = 0x009a,
    CSS_VALUE_OLIVE                = 0x009b,
    CSS_VALUE_OLIVEDRAB            = 0x009c,
    CSS_VALUE_ORANGE               = 0x009d,
    CSS_VALUE_ORANGERED            = 0x009e,
    CSS_VALUE_ORCHID               = 0x009f,
    CSS_VALUE_PALEGOLDENROD        = 0x00a0,
    CSS_VALUE_PALEGREEN            = 0x00a1,
    CSS_VALUE_PALETURQUOISE        = 0x00a2,
    CSS_VALUE_PALEVIOLETRED        = 0x00a3,
    CSS_VALUE_PAPAYAWHIP           = 0x00a4,
    CSS_VALUE_PEACHPUFF            = 0x00a5,
    CSS_VALUE_PERU                 = 0x00a6,
    CSS_VALUE_PINK                 = 0x00a7,
    CSS_VALUE_PLUM                 = 0x00a8,
    CSS_VALUE_POWDERBLUE           = 0x00a9,
    CSS_VALUE_PURPLE               = 0x00aa,
    CSS_VALUE_REBECCAPURPLE        = 0x00ab,
    CSS_VALUE_RED                  = 0x00ac,
    CSS_VALUE_ROSYBROWN            = 0x00ad,
    CSS_VALUE_ROYALBLUE            = 0x00ae,
    CSS_VALUE_SADDLEBROWN          = 0x00af,
    CSS_VALUE_SALMON               = 0x00b0,
    CSS_VALUE_SANDYBROWN           = 0x00b1,
    CSS_VALUE_SEAGREEN             = 0x00b2,
    CSS_VALUE_SEASHELL             = 0x00b3,
    CSS_VALUE_SIENNA               = 0x00b4,
    CSS_VALUE_SILVER               = 0x00b5,
    CSS_VALUE_SKYBLUE              = 0x00b6,
    CSS_VALUE_SLATEBLUE            = 0x00b7,
    CSS_VALUE_SLATEGRAY            = 0x00b8,
    CSS_VALUE_SLATEGREY            = 0x00b9,
    CSS_VALUE_SNOW                 = 0x00ba,
    CSS_VALUE_SPRINGGREEN          = 0x00bb,
    CSS_VALUE_STEELBLUE            = 0x00bc,
    CSS_VALUE_TAN                  = 0x00bd,
    CSS_VALUE_TEAL                 = 0x00be,
    CSS_VALUE_THISTLE              = 0x00bf,
    CSS_VALUE_TOMATO               = 0x00c0,
    CSS_VALUE_TURQUOISE            = 0x00c1,
    CSS_VALUE_VIOLET               = 0x00c2,
    CSS_VALUE_WHEAT                = 0x00c3,
    CSS_VALUE_WHITE                = 0x00c4,
    CSS_VALUE_WHITESMOKE           = 0x00c5,
    CSS_VALUE_YELLOW               = 0x00c6,
    CSS_VALUE_YELLOWGREEN          = 0x00c7,
    CSS_VALUE_CANVAS               = 0x00c8,
    CSS_VALUE_CANVASTEXT           = 0x00c9,
    CSS_VALUE_LINKTEXT             = 0x00ca,
    CSS_VALUE_VISITEDTEXT          = 0x00cb,
    CSS_VALUE_ACTIVETEXT           = 0x00cc,
    CSS_VALUE_BUTTONFACE           = 0x00cd,
    CSS_VALUE_BUTTONTEXT           = 0x00ce,
    CSS_VALUE_BUTTONBORDER         = 0x00cf,
    CSS_VALUE_FIELD                = 0x00d0,
    CSS_VALUE_FIELDTEXT            = 0x00d1,
    CSS_VALUE_HIGHLIGHT            = 0x00d2,
    CSS_VALUE_HIGHLIGHTTEXT        = 0x00d3,
    CSS_VALUE_SELECTEDITEM         = 0x00d4,
    CSS_VALUE_SELECTEDITEMTEXT     = 0x00d5,
    CSS_VALUE_MARK                 = 0x00d6,
    CSS_VALUE_MARKTEXT             = 0x00d7,
    CSS_VALUE_GRAYTEXT             = 0x00d8,
    CSS_VALUE_ACCENTCOLOR          = 0x00d9,
    CSS_VALUE_ACCENTCOLORTEXT      = 0x00da,
    CSS_VALUE_RGB                  = 0x00db,
    CSS_VALUE_RGBA                 = 0x00dc,
    CSS_VALUE_HSL                  = 0x00dd,
    CSS_VALUE_HSLA                 = 0x00de,
    CSS_VALUE_HWB                  = 0x00df,
    CSS_VALUE_LAB                  = 0x00e0,
    CSS_VALUE_LCH                  = 0x00e1,
    CSS_VALUE_OKLAB                = 0x00e2,
    CSS_VALUE_OKLCH                = 0x00e3,
    CSS_VALUE__COLOR               = 0x00e4,  // extra _ to avoid conflict
    CSS_VALUE_HAND                 = 0x00e5,
    CSS_VALUE_POINTER              = 0x00e6,
    CSS_VALUE_TEXT                 = 0x00e7,
    CSS_VALUE_WAIT                 = 0x00e8,
    CSS_VALUE_PROGRESS             = 0x00e9,
    CSS_VALUE_GRAB                 = 0x00ea,
    CSS_VALUE_GRABBING             = 0x00eb,
    CSS_VALUE_MOVE                 = 0x00ec,
    CSS_VALUE_LTR                  = 0x00ed,
    CSS_VALUE_RTL                  = 0x00ee,
    CSS_VALUE_BLOCK                = 0x00ef,
    CSS_VALUE_INLINE               = 0x00f0,
    CSS_VALUE_RUN_IN               = 0x00f1,
    CSS_VALUE_FLOW                 = 0x00f2,
    CSS_VALUE_FLOW_ROOT            = 0x00f3,
    CSS_VALUE_TABLE                = 0x00f4,
    CSS_VALUE_FLEX                = 0x00f5, // extra _ to avoid conflict
    CSS_VALUE_GRID                 = 0x00f6,
    CSS_VALUE_RUBY                 = 0x00f7,
    CSS_VALUE_LIST_ITEM            = 0x00f8,
    CSS_VALUE_TABLE_ROW_GROUP      = 0x00f9,
    CSS_VALUE_TABLE_HEADER_GROUP   = 0x00fa,
    CSS_VALUE_TABLE_FOOTER_GROUP   = 0x00fb,
    CSS_VALUE_TABLE_ROW            = 0x00fc,
    CSS_VALUE_TABLE_CELL           = 0x00fd,
    CSS_VALUE_TABLE_COLUMN_GROUP   = 0x00fe,
    CSS_VALUE_TABLE_COLUMN         = 0x00ff,
    CSS_VALUE_TABLE_CAPTION        = 0x0100,
    CSS_VALUE_RUBY_BASE            = 0x0101,
    CSS_VALUE_RUBY_TEXT            = 0x0102,
    CSS_VALUE_RUBY_BASE_CONTAINER  = 0x0103,
    CSS_VALUE_RUBY_TEXT_CONTAINER  = 0x0104,
    CSS_VALUE_CONTENTS             = 0x0105,
    CSS_VALUE_INLINE_BLOCK         = 0x0106,
    CSS_VALUE_INLINE_TABLE         = 0x0107,
    CSS_VALUE_INLINE_FLEX          = 0x0108,
    CSS_VALUE_INLINE_GRID          = 0x0109,
    CSS_VALUE_HANGING              = 0x010a,
    CSS_VALUE_CONTENT              = 0x010b,
    CSS_VALUE_ROW                  = 0x010c,
    CSS_VALUE_ROW_REVERSE          = 0x010d,
    CSS_VALUE_COLUMN               = 0x010e,
    CSS_VALUE_COLUMN_REVERSE       = 0x010f,
    CSS_VALUE__NUMBER              = 0x0110,
    CSS_VALUE_NOWRAP               = 0x0111,
    CSS_VALUE_WRAP                 = 0x0112,
    CSS_VALUE_WRAP_REVERSE         = 0x0113,
    CSS_VALUE_SNAP_BLOCK           = 0x0114,
    CSS_VALUE_START                = 0x0115,
    CSS_VALUE_END                  = 0x0116,
    CSS_VALUE_NEAR                 = 0x0117,
    CSS_VALUE_SNAP_INLINE          = 0x0118,
    CSS_VALUE__INTEGER             = 0x0119,
    CSS_VALUE_REGION               = 0x011a,
    CSS_VALUE_PAGE                 = 0x011b,
    CSS_VALUE_SERIF                = 0x011c,
    CSS_VALUE_SANS_SERIF           = 0x011d,
    CSS_VALUE_CURSIVE              = 0x011e,
    CSS_VALUE_FANTASY              = 0x011f,
    CSS_VALUE_MONOSPACE            = 0x0120,
    CSS_VALUE_SYSTEM_UI            = 0x0121,
    CSS_VALUE_EMOJI                = 0x0122,
    CSS_VALUE_MATH                 = 0x0123,
    CSS_VALUE_FANGSONG             = 0x0124,
    CSS_VALUE_UI_SERIF             = 0x0125,
    CSS_VALUE_UI_SANS_SERIF        = 0x0126,
    CSS_VALUE_UI_MONOSPACE         = 0x0127,
    CSS_VALUE_UI_ROUNDED           = 0x0128,
    CSS_VALUE_XX_SMALL             = 0x0129,
    CSS_VALUE_X_SMALL              = 0x012a,
    CSS_VALUE_SMALL                = 0x012b,
    CSS_VALUE_LARGE                = 0x012c,
    CSS_VALUE_X_LARGE              = 0x012d,
    CSS_VALUE_XX_LARGE             = 0x012e,
    CSS_VALUE_XXX_LARGE            = 0x012f,
    CSS_VALUE_LARGER               = 0x0130,
    CSS_VALUE_SMALLER              = 0x0131,
    CSS_VALUE_NORMAL               = 0x0132,
    CSS_VALUE_ULTRA_CONDENSED      = 0x0133,
    CSS_VALUE_EXTRA_CONDENSED      = 0x0134,
    CSS_VALUE_CONDENSED            = 0x0135,
    CSS_VALUE_SEMI_CONDENSED       = 0x0136,
    CSS_VALUE_SEMI_EXPANDED        = 0x0137,
    CSS_VALUE_EXPANDED             = 0x0138,
    CSS_VALUE_EXTRA_EXPANDED       = 0x0139,
    CSS_VALUE_ULTRA_EXPANDED       = 0x013a,
    CSS_VALUE_ITALIC               = 0x013b,
    CSS_VALUE_OBLIQUE              = 0x013c,
    CSS_VALUE_BOLD                 = 0x013d,
    CSS_VALUE_BOLDER               = 0x013e,
    CSS_VALUE_LIGHTER              = 0x013f,
    CSS_VALUE_FORCE_END            = 0x0140,
    CSS_VALUE_ALLOW_END            = 0x0141,
    CSS_VALUE_MIN_CONTENT          = 0x0142,
    CSS_VALUE_MAX_CONTENT          = 0x0143,
    CSS_VALUE__ANGLE               = 0x0144,
    CSS_VALUE_MANUAL               = 0x0145,
    CSS_VALUE_LOOSE                = 0x0146,
    CSS_VALUE_STRICT               = 0x0147,
    CSS_VALUE_ANYWHERE             = 0x0148,
    CSS_VALUE_VISIBLE              = 0x0149,
    CSS_VALUE_CLIP                 = 0x014a,
    CSS_VALUE_SCROLL               = 0x014b,
    CSS_VALUE_BREAK_WORD           = 0x014c,
    CSS_VALUE_STATIC               = 0x014d,
    CSS_VALUE_RELATIVE             = 0x014e,
    CSS_VALUE_ABSOLUTE             = 0x014f,
    CSS_VALUE_STICKY               = 0x0150,
    CSS_VALUE_FIXED                = 0x0151,
    CSS_VALUE_JUSTIFY              = 0x0152,
    CSS_VALUE_MATCH_PARENT         = 0x0153,
    CSS_VALUE_JUSTIFY_ALL          = 0x0154,
    CSS_VALUE_ALL                  = 0x0155,
    CSS_VALUE_DIGITS               = 0x0156,
    CSS_VALUE_UNDERLINE            = 0x0157,
    CSS_VALUE_OVERLINE             = 0x0158,
    CSS_VALUE_LINE_THROUGH         = 0x0159,
    CSS_VALUE_BLINK                = 0x015a,
    CSS_VALUE_WAVY                 = 0x015b,
    CSS_VALUE_EACH_LINE            = 0x015c,
    CSS_VALUE_INTER_WORD           = 0x015d,
    CSS_VALUE_INTER_CHARACTER      = 0x015e,
    CSS_VALUE_MIXED                = 0x015f,
    CSS_VALUE_UPRIGHT              = 0x0160,
    CSS_VALUE_SIDEWAYS             = 0x0161,
    CSS_VALUE_ELLIPSIS             = 0x0162,
    CSS_VALUE_CAPITALIZE           = 0x0163,
    CSS_VALUE_UPPERCASE            = 0x0164,
    CSS_VALUE_LOWERCASE            = 0x0165,
    CSS_VALUE_FULL_WIDTH           = 0x0166,
    CSS_VALUE_FULL_SIZE_KANA       = 0x0167,
    CSS_VALUE_EMBED                = 0x0168,
    CSS_VALUE_ISOLATE              = 0x0169,
    CSS_VALUE_BIDI_OVERRIDE        = 0x016a,
    CSS_VALUE_ISOLATE_OVERRIDE     = 0x016b,
    CSS_VALUE_PLAINTEXT            = 0x016c,
    CSS_VALUE_COLLAPSE             = 0x016d,
    CSS_VALUE_PRE                  = 0x016e,
    CSS_VALUE_PRE_WRAP             = 0x016f,
    CSS_VALUE_BREAK_SPACES         = 0x0170,
    CSS_VALUE_PRE_LINE             = 0x0171,
    CSS_VALUE_KEEP_ALL             = 0x0172,
    CSS_VALUE_BREAK_ALL            = 0x0173,
    CSS_VALUE_BOTH                 = 0x0174,
    CSS_VALUE_MINIMUM              = 0x0175,
    CSS_VALUE_MAXIMUM              = 0x0176,
    CSS_VALUE_CLEAR                = 0x0177,
    CSS_VALUE_HORIZONTAL_TB        = 0x0178,
    CSS_VALUE_VERTICAL_RL          = 0x0179,
    CSS_VALUE_VERTICAL_LR          = 0x017a,
    CSS_VALUE_SIDEWAYS_RL          = 0x017b,
    CSS_VALUE_SIDEWAYS_LR          = 0x017c,
    // List style types
    CSS_VALUE_DISC                 = 0x017d,
    CSS_VALUE_CIRCLE               = 0x017e,
    CSS_VALUE_SQUARE               = 0x017f,
    CSS_VALUE_DECIMAL              = 0x0180,
    CSS_VALUE_LOWER_ROMAN          = 0x0181,
    CSS_VALUE_UPPER_ROMAN          = 0x0182,
    CSS_VALUE_LOWER_ALPHA          = 0x0183,
    CSS_VALUE_UPPER_ALPHA          = 0x0184,
    // Flex layout
    CSS_VALUE_SPACE_EVENLY         = 0x0185,
    // Background properties
    CSS_VALUE_CONTAIN              = 0x0186,  // background-size contain
    CSS_VALUE_COVER                = 0x0187,  // background-size cover
    CSS_VALUE_LOCAL                = 0x0188,  // background-attachment local
    CSS_VALUE_PADDING_BOX          = 0x0189,  // background-origin/clip padding-box
    CSS_VALUE_MULTIPLY             = 0x018a,  // background-blend-mode multiply
    CSS_VALUE_OVERLAY              = 0x018b,  // background-blend-mode overlay
    CSS_VALUE_ROUND                = 0x018c,  // background-repeat round
    CSS_VALUE_SPACE                = 0x018d,  // background-repeat space
    // Table properties
    CSS_VALUE_COLLAPSE_TABLE       = 0x018e,  // border-collapse collapse
    CSS_VALUE_SEPARATE             = 0x018f,  // border-collapse separate
    CSS_VALUE_HIDE                 = 0x0190,  // empty-cells hide
    CSS_VALUE_SHOW                 = 0x0191,  // empty-cells show
    // Grid layout
    CSS_VALUE_FIT_CONTENT          = 0x0192,
    CSS_VALUE_FR                   = 0x0193,
    CSS_VALUE_DENSE                = 0x0194,
    CSS_VALUE__LAST_ENTRY          = 0x0195
};

#define RDT_DISPLAY_TEXT                (CSS_VALUE__LAST_ENTRY + 10)
#define RDT_DISPLAY_REPLACED            (CSS_VALUE__LAST_ENTRY + 11)

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
    PropValue type;  // CSS_VALUE__LENGTH or CSS_VALUE__PERCENTAGE
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
    PropValue type;
    uint32_t rgba;
} lxb_css_value_color_t;

// Line height property structure (replacing lexbor's lxb_css_property_line_height_t)
typedef struct {
    PropValue type;  // CSS_VALUE__NUMBER, CSS_VALUE__LENGTH, CSS_VALUE__PERCENTAGE, CSS_VALUE_NORMAL
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

typedef struct {
    const char *name;
    size_t     length;
    PropValue  unique;
} css_data;

const css_data* css_value_by_id(PropValue id);
PropValue css_value_by_name(const char* name);

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
    PropValue outer;
    PropValue inner;
} DisplayValue;

typedef struct {
    char* family;  // font family name
    float font_size;  // font size in pixels, scaled by pixel_ratio
    PropValue font_style;
    PropValue font_weight;
    PropValue text_deco; // CSS text decoration
    // derived font properties
    float space_width;  // width of a space character of the current font
    float ascender;    // font ascender in pixels
    float descender;   // font descender in pixels
    float font_height; // font height in pixels
    bool has_kerning;  // whether the font has kerning
} FontProp;

typedef struct {
    PropValue cursor;
    Color color;
    PropValue vertical_align;
    float opacity;  // CSS opacity value (0.0 to 1.0)
} InlineProp;

typedef struct Spacing {
    struct { float top, right, bottom, left; };  // for margin, padding, border
    int32_t top_specificity, right_specificity, bottom_specificity, left_specificity;
} Spacing;

typedef struct Margin : Spacing {
    PropValue top_type, right_type, bottom_type, left_type;   // for CSS enum values, like 'auto'
} Margin;

typedef struct Corner {
    struct { float top_left, top_right, bottom_right, bottom_left; };  // for border radius
    int32_t tl_specificity, tr_specificity, br_specificity, bl_specificity;
} Corner;

typedef struct {
    Spacing width;
    PropValue top_style, right_style, bottom_style, left_style;
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
    PropValue position;     // static, relative, absolute, fixed, sticky
    float top, right, bottom, left;  // offset values in pixels
    int z_index;            // stacking order
    bool has_top, has_right, has_bottom, has_left;  // which offsets are set
    PropValue clear;        // clear property for floats
    PropValue float_prop;   // float property (left, right, none)
    ViewBlock* first_abs_child;   // first child absolute/fixed positioned view
    ViewBlock* last_abs_child;    // last child absolute/fixed positioned view
    ViewBlock* next_abs_sibling;    // next sibling absolute/fixed positioned view
} PositionProp;

typedef struct {
    PropValue text_align;
    lxb_css_property_line_height_t *line_height;
    float text_indent;  // can be negative
    float given_min_width, given_max_width;  // non-negative
    float given_min_height, given_max_height;  // non-negative
    PropValue list_style_type;
    PropValue list_style_position;  // inside, outside
    char* list_style_image;         // URL or none
    char* counter_reset;            // counter names and values
    char* counter_increment;        // counter names and values
    PropValue box_sizing;  // CSS_VALUE_CONTENT_BOX or CSS_VALUE_BORDER_BOX
    PropValue white_space;  // CSS_VALUE_NORMAL, CSS_VALUE_NOWRAP, CSS_VALUE_PRE, etc.
    float given_width, given_height;  // CSS specified width/height values
    PropValue given_width_type;
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
    PropValue overflow_x, overflow_y;
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
    PropValue list_style_type;
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
