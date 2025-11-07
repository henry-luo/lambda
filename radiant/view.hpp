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
    LXB_TAG__UNDEF              = 0x0000,
    LXB_TAG__END_OF_FILE        = 0x0001,
    LXB_TAG__TEXT               = 0x0002,
    LXB_TAG__DOCUMENT           = 0x0003,
    LXB_TAG__EM_COMMENT         = 0x0004,
    LXB_TAG__EM_DOCTYPE         = 0x0005,
    LXB_TAG_A                   = 0x0006,
    LXB_TAG_ABBR                = 0x0007,
    LXB_TAG_ACRONYM             = 0x0008,
    LXB_TAG_ADDRESS             = 0x0009,
    LXB_TAG_ALTGLYPH            = 0x000a,
    LXB_TAG_ALTGLYPHDEF         = 0x000b,
    LXB_TAG_ALTGLYPHITEM        = 0x000c,
    LXB_TAG_ANIMATECOLOR        = 0x000d,
    LXB_TAG_ANIMATEMOTION       = 0x000e,
    LXB_TAG_ANIMATETRANSFORM    = 0x000f,
    LXB_TAG_ANNOTATION_XML      = 0x0010,
    LXB_TAG_APPLET              = 0x0011,
    LXB_TAG_AREA                = 0x0012,
    LXB_TAG_ARTICLE             = 0x0013,
    LXB_TAG_ASIDE               = 0x0014,
    LXB_TAG_AUDIO               = 0x0015,
    LXB_TAG_B                   = 0x0016,
    LXB_TAG_BASE                = 0x0017,
    LXB_TAG_BASEFONT            = 0x0018,
    LXB_TAG_BDI                 = 0x0019,
    LXB_TAG_BDO                 = 0x001a,
    LXB_TAG_BGSOUND             = 0x001b,
    LXB_TAG_BIG                 = 0x001c,
    LXB_TAG_BLINK               = 0x001d,
    LXB_TAG_BLOCKQUOTE          = 0x001e,
    LXB_TAG_BODY                = 0x001f,
    LXB_TAG_BR                  = 0x0020,
    LXB_TAG_BUTTON              = 0x0021,
    LXB_TAG_CANVAS              = 0x0022,
    LXB_TAG_CAPTION             = 0x0023,
    LXB_TAG_CENTER              = 0x0024,
    LXB_TAG_CITE                = 0x0025,
    LXB_TAG_CLIPPATH            = 0x0026,
    LXB_TAG_CODE                = 0x0027,
    LXB_TAG_COL                 = 0x0028,
    LXB_TAG_COLGROUP            = 0x0029,
    LXB_TAG_DATA                = 0x002a,
    LXB_TAG_DATALIST            = 0x002b,
    LXB_TAG_DD                  = 0x002c,
    LXB_TAG_DEL                 = 0x002d,
    LXB_TAG_DESC                = 0x002e,
    LXB_TAG_DETAILS             = 0x002f,
    LXB_TAG_DFN                 = 0x0030,
    LXB_TAG_DIALOG              = 0x0031,
    LXB_TAG_DIR                 = 0x0032,
    LXB_TAG_DIV                 = 0x0033,
    LXB_TAG_DL                  = 0x0034,
    LXB_TAG_DT                  = 0x0035,
    LXB_TAG_EM                  = 0x0036,
    LXB_TAG_EMBED               = 0x0037,
    LXB_TAG_FEBLEND             = 0x0038,
    LXB_TAG_FECOLORMATRIX       = 0x0039,
    LXB_TAG_FECOMPONENTTRANSFER = 0x003a,
    LXB_TAG_FECOMPOSITE         = 0x003b,
    LXB_TAG_FECONVOLVEMATRIX    = 0x003c,
    LXB_TAG_FEDIFFUSELIGHTING   = 0x003d,
    LXB_TAG_FEDISPLACEMENTMAP   = 0x003e,
    LXB_TAG_FEDISTANTLIGHT      = 0x003f,
    LXB_TAG_FEDROPSHADOW        = 0x0040,
    LXB_TAG_FEFLOOD             = 0x0041,
    LXB_TAG_FEFUNCA             = 0x0042,
    LXB_TAG_FEFUNCB             = 0x0043,
    LXB_TAG_FEFUNCG             = 0x0044,
    LXB_TAG_FEFUNCR             = 0x0045,
    LXB_TAG_FEGAUSSIANBLUR      = 0x0046,
    LXB_TAG_FEIMAGE             = 0x0047,
    LXB_TAG_FEMERGE             = 0x0048,
    LXB_TAG_FEMERGENODE         = 0x0049,
    LXB_TAG_FEMORPHOLOGY        = 0x004a,
    LXB_TAG_FEOFFSET            = 0x004b,
    LXB_TAG_FEPOINTLIGHT        = 0x004c,
    LXB_TAG_FESPECULARLIGHTING  = 0x004d,
    LXB_TAG_FESPOTLIGHT         = 0x004e,
    LXB_TAG_FETILE              = 0x004f,
    LXB_TAG_FETURBULENCE        = 0x0050,
    LXB_TAG_FIELDSET            = 0x0051,
    LXB_TAG_FIGCAPTION          = 0x0052,
    LXB_TAG_FIGURE              = 0x0053,
    LXB_TAG_FONT                = 0x0054,
    LXB_TAG_FOOTER              = 0x0055,
    LXB_TAG_FOREIGNOBJECT       = 0x0056,
    LXB_TAG_FORM                = 0x0057,
    LXB_TAG_FRAME               = 0x0058,
    LXB_TAG_FRAMESET            = 0x0059,
    LXB_TAG_GLYPHREF            = 0x005a,
    LXB_TAG_H1                  = 0x005b,
    LXB_TAG_H2                  = 0x005c,
    LXB_TAG_H3                  = 0x005d,
    LXB_TAG_H4                  = 0x005e,
    LXB_TAG_H5                  = 0x005f,
    LXB_TAG_H6                  = 0x0060,
    LXB_TAG_HEAD                = 0x0061,
    LXB_TAG_HEADER              = 0x0062,
    LXB_TAG_HGROUP              = 0x0063,
    LXB_TAG_HR                  = 0x0064,
    LXB_TAG_HTML                = 0x0065,
    LXB_TAG_I                   = 0x0066,
    LXB_TAG_IFRAME              = 0x0067,
    LXB_TAG_IMAGE               = 0x0068,
    LXB_TAG_IMG                 = 0x0069,
    LXB_TAG_INPUT               = 0x006a,
    LXB_TAG_INS                 = 0x006b,
    LXB_TAG_ISINDEX             = 0x006c,
    LXB_TAG_KBD                 = 0x006d,
    LXB_TAG_KEYGEN              = 0x006e,
    LXB_TAG_LABEL               = 0x006f,
    LXB_TAG_LEGEND              = 0x0070,
    LXB_TAG_LI                  = 0x0071,
    LXB_TAG_LINEARGRADIENT      = 0x0072,
    LXB_TAG_LINK                = 0x0073,
    LXB_TAG_LISTING             = 0x0074,
    LXB_TAG_MAIN                = 0x0075,
    LXB_TAG_MALIGNMARK          = 0x0076,
    LXB_TAG_MAP                 = 0x0077,
    LXB_TAG_MARK                = 0x0078,
    LXB_TAG_MARQUEE             = 0x0079,
    LXB_TAG_MATH                = 0x007a,
    LXB_TAG_MENU                = 0x007b,
    LXB_TAG_META                = 0x007c,
    LXB_TAG_METER               = 0x007d,
    LXB_TAG_MFENCED             = 0x007e,
    LXB_TAG_MGLYPH              = 0x007f,
    LXB_TAG_MI                  = 0x0080,
    LXB_TAG_MN                  = 0x0081,
    LXB_TAG_MO                  = 0x0082,
    LXB_TAG_MS                  = 0x0083,
    LXB_TAG_MTEXT               = 0x0084,
    LXB_TAG_MULTICOL            = 0x0085,
    LXB_TAG_NAV                 = 0x0086,
    LXB_TAG_NEXTID              = 0x0087,
    LXB_TAG_NOBR                = 0x0088,
    LXB_TAG_NOEMBED             = 0x0089,
    LXB_TAG_NOFRAMES            = 0x008a,
    LXB_TAG_NOSCRIPT            = 0x008b,
    LXB_TAG_OBJECT              = 0x008c,
    LXB_TAG_OL                  = 0x008d,
    LXB_TAG_OPTGROUP            = 0x008e,
    LXB_TAG_OPTION              = 0x008f,
    LXB_TAG_OUTPUT              = 0x0090,
    LXB_TAG_P                   = 0x0091,
    LXB_TAG_PARAM               = 0x0092,
    LXB_TAG_PATH                = 0x0093,
    LXB_TAG_PICTURE             = 0x0094,
    LXB_TAG_PLAINTEXT           = 0x0095,
    LXB_TAG_PRE                 = 0x0096,
    LXB_TAG_PROGRESS            = 0x0097,
    LXB_TAG_Q                   = 0x0098,
    LXB_TAG_RADIALGRADIENT      = 0x0099,
    LXB_TAG_RB                  = 0x009a,
    LXB_TAG_RP                  = 0x009b,
    LXB_TAG_RT                  = 0x009c,
    LXB_TAG_RTC                 = 0x009d,
    LXB_TAG_RUBY                = 0x009e,
    LXB_TAG_S                   = 0x009f,
    LXB_TAG_SAMP                = 0x00a0,
    LXB_TAG_SCRIPT              = 0x00a1,
    LXB_TAG_SECTION             = 0x00a2,
    LXB_TAG_SELECT              = 0x00a3,
    LXB_TAG_SLOT                = 0x00a4,
    LXB_TAG_SMALL               = 0x00a5,
    LXB_TAG_SOURCE              = 0x00a6,
    LXB_TAG_SPACER              = 0x00a7,
    LXB_TAG_SPAN                = 0x00a8,
    LXB_TAG_STRIKE              = 0x00a9,
    LXB_TAG_STRONG              = 0x00aa,
    LXB_TAG_STYLE               = 0x00ab,
    LXB_TAG_SUB                 = 0x00ac,
    LXB_TAG_SUMMARY             = 0x00ad,
    LXB_TAG_SUP                 = 0x00ae,
    LXB_TAG_SVG                 = 0x00af,
    LXB_TAG_TABLE               = 0x00b0,
    LXB_TAG_TBODY               = 0x00b1,
    LXB_TAG_TD                  = 0x00b2,
    LXB_TAG_TEMPLATE            = 0x00b3,
    LXB_TAG_TEXTAREA            = 0x00b4,
    LXB_TAG_TEXTPATH            = 0x00b5,
    LXB_TAG_TFOOT               = 0x00b6,
    LXB_TAG_TH                  = 0x00b7,
    LXB_TAG_THEAD               = 0x00b8,
    LXB_TAG_TIME                = 0x00b9,
    LXB_TAG_TITLE               = 0x00ba,
    LXB_TAG_TR                  = 0x00bb,
    LXB_TAG_TRACK               = 0x00bc,
    LXB_TAG_TT                  = 0x00bd,
    LXB_TAG_U                   = 0x00be,
    LXB_TAG_UL                  = 0x00bf,
    LXB_TAG_VAR                 = 0x00c0,
    LXB_TAG_VIDEO               = 0x00c1,
    LXB_TAG_WBR                 = 0x00c2,
    LXB_TAG_XMP                 = 0x00c3,
    LXB_TAG__LAST_ENTRY         = 0x00c4
};

enum {
    LXB_CSS_VALUE__UNDEF               = 0x0000,
    LXB_CSS_VALUE_INITIAL              = 0x0001,
    LXB_CSS_VALUE_INHERIT              = 0x0002,
    LXB_CSS_VALUE_UNSET                = 0x0003,
    LXB_CSS_VALUE_REVERT               = 0x0004,
    LXB_CSS_VALUE_FLEX_START           = 0x0005,
    LXB_CSS_VALUE_FLEX_END             = 0x0006,
    LXB_CSS_VALUE_CENTER               = 0x0007,
    LXB_CSS_VALUE_SPACE_BETWEEN        = 0x0008,
    LXB_CSS_VALUE_SPACE_AROUND         = 0x0009,
    LXB_CSS_VALUE_STRETCH              = 0x000a,
    LXB_CSS_VALUE_BASELINE             = 0x000b,
    LXB_CSS_VALUE_AUTO                 = 0x000c,
    LXB_CSS_VALUE_TEXT_BOTTOM          = 0x000d,
    LXB_CSS_VALUE_ALPHABETIC           = 0x000e,
    LXB_CSS_VALUE_IDEOGRAPHIC          = 0x000f,
    LXB_CSS_VALUE_MIDDLE               = 0x0010,
    LXB_CSS_VALUE_CENTRAL              = 0x0011,
    LXB_CSS_VALUE_MATHEMATICAL         = 0x0012,
    LXB_CSS_VALUE_TEXT_TOP             = 0x0013,
    LXB_CSS_VALUE__LENGTH              = 0x0014,
    LXB_CSS_VALUE__PERCENTAGE          = 0x0015,
    LXB_CSS_VALUE_SUB                  = 0x0016,
    LXB_CSS_VALUE_SUPER                = 0x0017,
    LXB_CSS_VALUE_TOP                  = 0x0018,
    LXB_CSS_VALUE_BOTTOM               = 0x0019,
    LXB_CSS_VALUE_FIRST                = 0x001a,
    LXB_CSS_VALUE_LAST                 = 0x001b,
    LXB_CSS_VALUE_THIN                 = 0x001c,
    LXB_CSS_VALUE_MEDIUM               = 0x001d,
    LXB_CSS_VALUE_THICK                = 0x001e,
    LXB_CSS_VALUE_NONE                 = 0x001f,
    LXB_CSS_VALUE_HIDDEN               = 0x0020,
    LXB_CSS_VALUE_DOTTED               = 0x0021,
    LXB_CSS_VALUE_DASHED               = 0x0022,
    LXB_CSS_VALUE_SOLID                = 0x0023,
    LXB_CSS_VALUE_DOUBLE               = 0x0024,
    LXB_CSS_VALUE_GROOVE               = 0x0025,
    LXB_CSS_VALUE_RIDGE                = 0x0026,
    LXB_CSS_VALUE_INSET                = 0x0027,
    LXB_CSS_VALUE_OUTSET               = 0x0028,
    LXB_CSS_VALUE_CONTENT_BOX          = 0x0029,
    LXB_CSS_VALUE_BORDER_BOX           = 0x002a,
    LXB_CSS_VALUE_INLINE_START         = 0x002b,
    LXB_CSS_VALUE_INLINE_END           = 0x002c,
    LXB_CSS_VALUE_BLOCK_START          = 0x002d,
    LXB_CSS_VALUE_BLOCK_END            = 0x002e,
    LXB_CSS_VALUE_LEFT                 = 0x002f,
    LXB_CSS_VALUE_RIGHT                = 0x0030,
    LXB_CSS_VALUE_CURRENTCOLOR         = 0x0031,
    LXB_CSS_VALUE_TRANSPARENT          = 0x0032,
    LXB_CSS_VALUE_HEX                  = 0x0033,
    LXB_CSS_VALUE_ALICEBLUE            = 0x0034,
    LXB_CSS_VALUE_ANTIQUEWHITE         = 0x0035,
    LXB_CSS_VALUE_AQUA                 = 0x0036,
    LXB_CSS_VALUE_AQUAMARINE           = 0x0037,
    LXB_CSS_VALUE_AZURE                = 0x0038,
    LXB_CSS_VALUE_BEIGE                = 0x0039,
    LXB_CSS_VALUE_BISQUE               = 0x003a,
    LXB_CSS_VALUE_BLACK                = 0x003b,
    LXB_CSS_VALUE_BLANCHEDALMOND       = 0x003c,
    LXB_CSS_VALUE_BLUE                 = 0x003d,
    LXB_CSS_VALUE_BLUEVIOLET           = 0x003e,
    LXB_CSS_VALUE_BROWN                = 0x003f,
    LXB_CSS_VALUE_BURLYWOOD            = 0x0040,
    LXB_CSS_VALUE_CADETBLUE            = 0x0041,
    LXB_CSS_VALUE_CHARTREUSE           = 0x0042,
    LXB_CSS_VALUE_CHOCOLATE            = 0x0043,
    LXB_CSS_VALUE_CORAL                = 0x0044,
    LXB_CSS_VALUE_CORNFLOWERBLUE       = 0x0045,
    LXB_CSS_VALUE_CORNSILK             = 0x0046,
    LXB_CSS_VALUE_CRIMSON              = 0x0047,
    LXB_CSS_VALUE_CYAN                 = 0x0048,
    LXB_CSS_VALUE_DARKBLUE             = 0x0049,
    LXB_CSS_VALUE_DARKCYAN             = 0x004a,
    LXB_CSS_VALUE_DARKGOLDENROD        = 0x004b,
    LXB_CSS_VALUE_DARKGRAY             = 0x004c,
    LXB_CSS_VALUE_DARKGREEN            = 0x004d,
    LXB_CSS_VALUE_DARKGREY             = 0x004e,
    LXB_CSS_VALUE_DARKKHAKI            = 0x004f,
    LXB_CSS_VALUE_DARKMAGENTA          = 0x0050,
    LXB_CSS_VALUE_DARKOLIVEGREEN       = 0x0051,
    LXB_CSS_VALUE_DARKORANGE           = 0x0052,
    LXB_CSS_VALUE_DARKORCHID           = 0x0053,
    LXB_CSS_VALUE_DARKRED              = 0x0054,
    LXB_CSS_VALUE_DARKSALMON           = 0x0055,
    LXB_CSS_VALUE_DARKSEAGREEN         = 0x0056,
    LXB_CSS_VALUE_DARKSLATEBLUE        = 0x0057,
    LXB_CSS_VALUE_DARKSLATEGRAY        = 0x0058,
    LXB_CSS_VALUE_DARKSLATEGREY        = 0x0059,
    LXB_CSS_VALUE_DARKTURQUOISE        = 0x005a,
    LXB_CSS_VALUE_DARKVIOLET           = 0x005b,
    LXB_CSS_VALUE_DEEPPINK             = 0x005c,
    LXB_CSS_VALUE_DEEPSKYBLUE          = 0x005d,
    LXB_CSS_VALUE_DIMGRAY              = 0x005e,
    LXB_CSS_VALUE_DIMGREY              = 0x005f,
    LXB_CSS_VALUE_DODGERBLUE           = 0x0060,
    LXB_CSS_VALUE_FIREBRICK            = 0x0061,
    LXB_CSS_VALUE_FLORALWHITE          = 0x0062,
    LXB_CSS_VALUE_FORESTGREEN          = 0x0063,
    LXB_CSS_VALUE_FUCHSIA              = 0x0064,
    LXB_CSS_VALUE_GAINSBORO            = 0x0065,
    LXB_CSS_VALUE_GHOSTWHITE           = 0x0066,
    LXB_CSS_VALUE_GOLD                 = 0x0067,
    LXB_CSS_VALUE_GOLDENROD            = 0x0068,
    LXB_CSS_VALUE_GRAY                 = 0x0069,
    LXB_CSS_VALUE_GREEN                = 0x006a,
    LXB_CSS_VALUE_GREENYELLOW          = 0x006b,
    LXB_CSS_VALUE_GREY                 = 0x006c,
    LXB_CSS_VALUE_HONEYDEW             = 0x006d,
    LXB_CSS_VALUE_HOTPINK              = 0x006e,
    LXB_CSS_VALUE_INDIANRED            = 0x006f,
    LXB_CSS_VALUE_INDIGO               = 0x0070,
    LXB_CSS_VALUE_IVORY                = 0x0071,
    LXB_CSS_VALUE_KHAKI                = 0x0072,
    LXB_CSS_VALUE_LAVENDER             = 0x0073,
    LXB_CSS_VALUE_LAVENDERBLUSH        = 0x0074,
    LXB_CSS_VALUE_LAWNGREEN            = 0x0075,
    LXB_CSS_VALUE_LEMONCHIFFON         = 0x0076,
    LXB_CSS_VALUE_LIGHTBLUE            = 0x0077,
    LXB_CSS_VALUE_LIGHTCORAL           = 0x0078,
    LXB_CSS_VALUE_LIGHTCYAN            = 0x0079,
    LXB_CSS_VALUE_LIGHTGOLDENRODYELLOW = 0x007a,
    LXB_CSS_VALUE_LIGHTGRAY            = 0x007b,
    LXB_CSS_VALUE_LIGHTGREEN           = 0x007c,
    LXB_CSS_VALUE_LIGHTGREY            = 0x007d,
    LXB_CSS_VALUE_LIGHTPINK            = 0x007e,
    LXB_CSS_VALUE_LIGHTSALMON          = 0x007f,
    LXB_CSS_VALUE_LIGHTSEAGREEN        = 0x0080,
    LXB_CSS_VALUE_LIGHTSKYBLUE         = 0x0081,
    LXB_CSS_VALUE_LIGHTSLATEGRAY       = 0x0082,
    LXB_CSS_VALUE_LIGHTSLATEGREY       = 0x0083,
    LXB_CSS_VALUE_LIGHTSTEELBLUE       = 0x0084,
    LXB_CSS_VALUE_LIGHTYELLOW          = 0x0085,
    LXB_CSS_VALUE_LIME                 = 0x0086,
    LXB_CSS_VALUE_LIMEGREEN            = 0x0087,
    LXB_CSS_VALUE_LINEN                = 0x0088,
    LXB_CSS_VALUE_MAGENTA              = 0x0089,
    LXB_CSS_VALUE_MAROON               = 0x008a,
    LXB_CSS_VALUE_MEDIUMAQUAMARINE     = 0x008b,
    LXB_CSS_VALUE_MEDIUMBLUE           = 0x008c,
    LXB_CSS_VALUE_MEDIUMORCHID         = 0x008d,
    LXB_CSS_VALUE_MEDIUMPURPLE         = 0x008e,
    LXB_CSS_VALUE_MEDIUMSEAGREEN       = 0x008f,
    LXB_CSS_VALUE_MEDIUMSLATEBLUE      = 0x0090,
    LXB_CSS_VALUE_MEDIUMSPRINGGREEN    = 0x0091,
    LXB_CSS_VALUE_MEDIUMTURQUOISE      = 0x0092,
    LXB_CSS_VALUE_MEDIUMVIOLETRED      = 0x0093,
    LXB_CSS_VALUE_MIDNIGHTBLUE         = 0x0094,
    LXB_CSS_VALUE_MINTCREAM            = 0x0095,
    LXB_CSS_VALUE_MISTYROSE            = 0x0096,
    LXB_CSS_VALUE_MOCCASIN             = 0x0097,
    LXB_CSS_VALUE_NAVAJOWHITE          = 0x0098,
    LXB_CSS_VALUE_NAVY                 = 0x0099,
    LXB_CSS_VALUE_OLDLACE              = 0x009a,
    LXB_CSS_VALUE_OLIVE                = 0x009b,
    LXB_CSS_VALUE_OLIVEDRAB            = 0x009c,
    LXB_CSS_VALUE_ORANGE               = 0x009d,
    LXB_CSS_VALUE_ORANGERED            = 0x009e,
    LXB_CSS_VALUE_ORCHID               = 0x009f,
    LXB_CSS_VALUE_PALEGOLDENROD        = 0x00a0,
    LXB_CSS_VALUE_PALEGREEN            = 0x00a1,
    LXB_CSS_VALUE_PALETURQUOISE        = 0x00a2,
    LXB_CSS_VALUE_PALEVIOLETRED        = 0x00a3,
    LXB_CSS_VALUE_PAPAYAWHIP           = 0x00a4,
    LXB_CSS_VALUE_PEACHPUFF            = 0x00a5,
    LXB_CSS_VALUE_PERU                 = 0x00a6,
    LXB_CSS_VALUE_PINK                 = 0x00a7,
    LXB_CSS_VALUE_PLUM                 = 0x00a8,
    LXB_CSS_VALUE_POWDERBLUE           = 0x00a9,
    LXB_CSS_VALUE_PURPLE               = 0x00aa,
    LXB_CSS_VALUE_REBECCAPURPLE        = 0x00ab,
    LXB_CSS_VALUE_RED                  = 0x00ac,
    LXB_CSS_VALUE_ROSYBROWN            = 0x00ad,
    LXB_CSS_VALUE_ROYALBLUE            = 0x00ae,
    LXB_CSS_VALUE_SADDLEBROWN          = 0x00af,
    LXB_CSS_VALUE_SALMON               = 0x00b0,
    LXB_CSS_VALUE_SANDYBROWN           = 0x00b1,
    LXB_CSS_VALUE_SEAGREEN             = 0x00b2,
    LXB_CSS_VALUE_SEASHELL             = 0x00b3,
    LXB_CSS_VALUE_SIENNA               = 0x00b4,
    LXB_CSS_VALUE_SILVER               = 0x00b5,
    LXB_CSS_VALUE_SKYBLUE              = 0x00b6,
    LXB_CSS_VALUE_SLATEBLUE            = 0x00b7,
    LXB_CSS_VALUE_SLATEGRAY            = 0x00b8,
    LXB_CSS_VALUE_SLATEGREY            = 0x00b9,
    LXB_CSS_VALUE_SNOW                 = 0x00ba,
    LXB_CSS_VALUE_SPRINGGREEN          = 0x00bb,
    LXB_CSS_VALUE_STEELBLUE            = 0x00bc,
    LXB_CSS_VALUE_TAN                  = 0x00bd,
    LXB_CSS_VALUE_TEAL                 = 0x00be,
    LXB_CSS_VALUE_THISTLE              = 0x00bf,
    LXB_CSS_VALUE_TOMATO               = 0x00c0,
    LXB_CSS_VALUE_TURQUOISE            = 0x00c1,
    LXB_CSS_VALUE_VIOLET               = 0x00c2,
    LXB_CSS_VALUE_WHEAT                = 0x00c3,
    LXB_CSS_VALUE_WHITE                = 0x00c4,
    LXB_CSS_VALUE_WHITESMOKE           = 0x00c5,
    LXB_CSS_VALUE_YELLOW               = 0x00c6,
    LXB_CSS_VALUE_YELLOWGREEN          = 0x00c7,
    LXB_CSS_VALUE_CANVAS               = 0x00c8,
    LXB_CSS_VALUE_CANVASTEXT           = 0x00c9,
    LXB_CSS_VALUE_LINKTEXT             = 0x00ca,
    LXB_CSS_VALUE_VISITEDTEXT          = 0x00cb,
    LXB_CSS_VALUE_ACTIVETEXT           = 0x00cc,
    LXB_CSS_VALUE_BUTTONFACE           = 0x00cd,
    LXB_CSS_VALUE_BUTTONTEXT           = 0x00ce,
    LXB_CSS_VALUE_BUTTONBORDER         = 0x00cf,
    LXB_CSS_VALUE_FIELD                = 0x00d0,
    LXB_CSS_VALUE_FIELDTEXT            = 0x00d1,
    LXB_CSS_VALUE_HIGHLIGHT            = 0x00d2,
    LXB_CSS_VALUE_HIGHLIGHTTEXT        = 0x00d3,
    LXB_CSS_VALUE_SELECTEDITEM         = 0x00d4,
    LXB_CSS_VALUE_SELECTEDITEMTEXT     = 0x00d5,
    LXB_CSS_VALUE_MARK                 = 0x00d6,
    LXB_CSS_VALUE_MARKTEXT             = 0x00d7,
    LXB_CSS_VALUE_GRAYTEXT             = 0x00d8,
    LXB_CSS_VALUE_ACCENTCOLOR          = 0x00d9,
    LXB_CSS_VALUE_ACCENTCOLORTEXT      = 0x00da,
    LXB_CSS_VALUE_RGB                  = 0x00db,
    LXB_CSS_VALUE_RGBA                 = 0x00dc,
    LXB_CSS_VALUE_HSL                  = 0x00dd,
    LXB_CSS_VALUE_HSLA                 = 0x00de,
    LXB_CSS_VALUE_HWB                  = 0x00df,
    LXB_CSS_VALUE_LAB                  = 0x00e0,
    LXB_CSS_VALUE_LCH                  = 0x00e1,
    LXB_CSS_VALUE_OKLAB                = 0x00e2,
    LXB_CSS_VALUE_OKLCH                = 0x00e3,
    LXB_CSS_VALUE_COLOR                = 0x00e4,
    LXB_CSS_VALUE_HAND                 = 0x00e5,
    LXB_CSS_VALUE_POINTER              = 0x00e6,
    LXB_CSS_VALUE_TEXT                 = 0x00e7,
    LXB_CSS_VALUE_WAIT                 = 0x00e8,
    LXB_CSS_VALUE_PROGRESS             = 0x00e9,
    LXB_CSS_VALUE_GRAB                 = 0x00ea,
    LXB_CSS_VALUE_GRABBING             = 0x00eb,
    LXB_CSS_VALUE_MOVE                 = 0x00ec,
    LXB_CSS_VALUE_LTR                  = 0x00ed,
    LXB_CSS_VALUE_RTL                  = 0x00ee,
    LXB_CSS_VALUE_BLOCK                = 0x00ef,
    LXB_CSS_VALUE_INLINE               = 0x00f0,
    LXB_CSS_VALUE_RUN_IN               = 0x00f1,
    LXB_CSS_VALUE_FLOW                 = 0x00f2,
    LXB_CSS_VALUE_FLOW_ROOT            = 0x00f3,
    LXB_CSS_VALUE_TABLE                = 0x00f4,
    LXB_CSS_VALUE_FLEX                 = 0x00f5,
    LXB_CSS_VALUE_GRID                 = 0x00f6,
    LXB_CSS_VALUE_RUBY                 = 0x00f7,
    LXB_CSS_VALUE_LIST_ITEM            = 0x00f8,
    LXB_CSS_VALUE_TABLE_ROW_GROUP      = 0x00f9,
    LXB_CSS_VALUE_TABLE_HEADER_GROUP   = 0x00fa,
    LXB_CSS_VALUE_TABLE_FOOTER_GROUP   = 0x00fb,
    LXB_CSS_VALUE_TABLE_ROW            = 0x00fc,
    LXB_CSS_VALUE_TABLE_CELL           = 0x00fd,
    LXB_CSS_VALUE_TABLE_COLUMN_GROUP   = 0x00fe,
    LXB_CSS_VALUE_TABLE_COLUMN         = 0x00ff,
    LXB_CSS_VALUE_TABLE_CAPTION        = 0x0100,
    LXB_CSS_VALUE_RUBY_BASE            = 0x0101,
    LXB_CSS_VALUE_RUBY_TEXT            = 0x0102,
    LXB_CSS_VALUE_RUBY_BASE_CONTAINER  = 0x0103,
    LXB_CSS_VALUE_RUBY_TEXT_CONTAINER  = 0x0104,
    LXB_CSS_VALUE_CONTENTS             = 0x0105,
    LXB_CSS_VALUE_INLINE_BLOCK         = 0x0106,
    LXB_CSS_VALUE_INLINE_TABLE         = 0x0107,
    LXB_CSS_VALUE_INLINE_FLEX          = 0x0108,
    LXB_CSS_VALUE_INLINE_GRID          = 0x0109,
    LXB_CSS_VALUE_HANGING              = 0x010a,
    LXB_CSS_VALUE_CONTENT              = 0x010b,
    LXB_CSS_VALUE_ROW                  = 0x010c,
    LXB_CSS_VALUE_ROW_REVERSE          = 0x010d,
    LXB_CSS_VALUE_COLUMN               = 0x010e,
    LXB_CSS_VALUE_COLUMN_REVERSE       = 0x010f,
    LXB_CSS_VALUE__NUMBER              = 0x0110,
    LXB_CSS_VALUE_NOWRAP               = 0x0111,
    LXB_CSS_VALUE_WRAP                 = 0x0112,
    LXB_CSS_VALUE_WRAP_REVERSE         = 0x0113,
    LXB_CSS_VALUE_SNAP_BLOCK           = 0x0114,
    LXB_CSS_VALUE_START                = 0x0115,
    LXB_CSS_VALUE_END                  = 0x0116,
    LXB_CSS_VALUE_NEAR                 = 0x0117,
    LXB_CSS_VALUE_SNAP_INLINE          = 0x0118,
    LXB_CSS_VALUE__INTEGER             = 0x0119,
    LXB_CSS_VALUE_REGION               = 0x011a,
    LXB_CSS_VALUE_PAGE                 = 0x011b,
    LXB_CSS_VALUE_SERIF                = 0x011c,
    LXB_CSS_VALUE_SANS_SERIF           = 0x011d,
    LXB_CSS_VALUE_CURSIVE              = 0x011e,
    LXB_CSS_VALUE_FANTASY              = 0x011f,
    LXB_CSS_VALUE_MONOSPACE            = 0x0120,
    LXB_CSS_VALUE_SYSTEM_UI            = 0x0121,
    LXB_CSS_VALUE_EMOJI                = 0x0122,
    LXB_CSS_VALUE_MATH                 = 0x0123,
    LXB_CSS_VALUE_FANGSONG             = 0x0124,
    LXB_CSS_VALUE_UI_SERIF             = 0x0125,
    LXB_CSS_VALUE_UI_SANS_SERIF        = 0x0126,
    LXB_CSS_VALUE_UI_MONOSPACE         = 0x0127,
    LXB_CSS_VALUE_UI_ROUNDED           = 0x0128,
    LXB_CSS_VALUE_XX_SMALL             = 0x0129,
    LXB_CSS_VALUE_X_SMALL              = 0x012a,
    LXB_CSS_VALUE_SMALL                = 0x012b,
    LXB_CSS_VALUE_LARGE                = 0x012c,
    LXB_CSS_VALUE_X_LARGE              = 0x012d,
    LXB_CSS_VALUE_XX_LARGE             = 0x012e,
    LXB_CSS_VALUE_XXX_LARGE            = 0x012f,
    LXB_CSS_VALUE_LARGER               = 0x0130,
    LXB_CSS_VALUE_SMALLER              = 0x0131,
    LXB_CSS_VALUE_NORMAL               = 0x0132,
    LXB_CSS_VALUE_ULTRA_CONDENSED      = 0x0133,
    LXB_CSS_VALUE_EXTRA_CONDENSED      = 0x0134,
    LXB_CSS_VALUE_CONDENSED            = 0x0135,
    LXB_CSS_VALUE_SEMI_CONDENSED       = 0x0136,
    LXB_CSS_VALUE_SEMI_EXPANDED        = 0x0137,
    LXB_CSS_VALUE_EXPANDED             = 0x0138,
    LXB_CSS_VALUE_EXTRA_EXPANDED       = 0x0139,
    LXB_CSS_VALUE_ULTRA_EXPANDED       = 0x013a,
    LXB_CSS_VALUE_ITALIC               = 0x013b,
    LXB_CSS_VALUE_OBLIQUE              = 0x013c,
    LXB_CSS_VALUE_BOLD                 = 0x013d,
    LXB_CSS_VALUE_BOLDER               = 0x013e,
    LXB_CSS_VALUE_LIGHTER              = 0x013f,
    LXB_CSS_VALUE_FORCE_END            = 0x0140,
    LXB_CSS_VALUE_ALLOW_END            = 0x0141,
    LXB_CSS_VALUE_MIN_CONTENT          = 0x0142,
    LXB_CSS_VALUE_MAX_CONTENT          = 0x0143,
    LXB_CSS_VALUE__ANGLE               = 0x0144,
    LXB_CSS_VALUE_MANUAL               = 0x0145,
    LXB_CSS_VALUE_LOOSE                = 0x0146,
    LXB_CSS_VALUE_STRICT               = 0x0147,
    LXB_CSS_VALUE_ANYWHERE             = 0x0148,
    LXB_CSS_VALUE_VISIBLE              = 0x0149,
    LXB_CSS_VALUE_CLIP                 = 0x014a,
    LXB_CSS_VALUE_SCROLL               = 0x014b,
    LXB_CSS_VALUE_BREAK_WORD           = 0x014c,
    LXB_CSS_VALUE_STATIC               = 0x014d,
    LXB_CSS_VALUE_RELATIVE             = 0x014e,
    LXB_CSS_VALUE_ABSOLUTE             = 0x014f,
    LXB_CSS_VALUE_STICKY               = 0x0150,
    LXB_CSS_VALUE_FIXED                = 0x0151,
    LXB_CSS_VALUE_JUSTIFY              = 0x0152,
    LXB_CSS_VALUE_MATCH_PARENT         = 0x0153,
    LXB_CSS_VALUE_JUSTIFY_ALL          = 0x0154,
    LXB_CSS_VALUE_ALL                  = 0x0155,
    LXB_CSS_VALUE_DIGITS               = 0x0156,
    LXB_CSS_VALUE_UNDERLINE            = 0x0157,
    LXB_CSS_VALUE_OVERLINE             = 0x0158,
    LXB_CSS_VALUE_LINE_THROUGH         = 0x0159,
    LXB_CSS_VALUE_BLINK                = 0x015a,
    LXB_CSS_VALUE_WAVY                 = 0x015b,
    LXB_CSS_VALUE_EACH_LINE            = 0x015c,
    LXB_CSS_VALUE_INTER_WORD           = 0x015d,
    LXB_CSS_VALUE_INTER_CHARACTER      = 0x015e,
    LXB_CSS_VALUE_MIXED                = 0x015f,
    LXB_CSS_VALUE_UPRIGHT              = 0x0160,
    LXB_CSS_VALUE_SIDEWAYS             = 0x0161,
    LXB_CSS_VALUE_ELLIPSIS             = 0x0162,
    LXB_CSS_VALUE_CAPITALIZE           = 0x0163,
    LXB_CSS_VALUE_UPPERCASE            = 0x0164,
    LXB_CSS_VALUE_LOWERCASE            = 0x0165,
    LXB_CSS_VALUE_FULL_WIDTH           = 0x0166,
    LXB_CSS_VALUE_FULL_SIZE_KANA       = 0x0167,
    LXB_CSS_VALUE_EMBED                = 0x0168,
    LXB_CSS_VALUE_ISOLATE              = 0x0169,
    LXB_CSS_VALUE_BIDI_OVERRIDE        = 0x016a,
    LXB_CSS_VALUE_ISOLATE_OVERRIDE     = 0x016b,
    LXB_CSS_VALUE_PLAINTEXT            = 0x016c,
    LXB_CSS_VALUE_COLLAPSE             = 0x016d,
    LXB_CSS_VALUE_PRE                  = 0x016e,
    LXB_CSS_VALUE_PRE_WRAP             = 0x016f,
    LXB_CSS_VALUE_BREAK_SPACES         = 0x0170,
    LXB_CSS_VALUE_PRE_LINE             = 0x0171,
    LXB_CSS_VALUE_KEEP_ALL             = 0x0172,
    LXB_CSS_VALUE_BREAK_ALL            = 0x0173,
    LXB_CSS_VALUE_BOTH                 = 0x0174,
    LXB_CSS_VALUE_MINIMUM              = 0x0175,
    LXB_CSS_VALUE_MAXIMUM              = 0x0176,
    LXB_CSS_VALUE_CLEAR                = 0x0177,
    LXB_CSS_VALUE_HORIZONTAL_TB        = 0x0178,
    LXB_CSS_VALUE_VERTICAL_RL          = 0x0179,
    LXB_CSS_VALUE_VERTICAL_LR          = 0x017a,
    LXB_CSS_VALUE_SIDEWAYS_RL          = 0x017b,
    LXB_CSS_VALUE_SIDEWAYS_LR          = 0x017c,
    // List style types
    LXB_CSS_VALUE_DISC                 = 0x017d,
    LXB_CSS_VALUE_CIRCLE               = 0x017e,
    LXB_CSS_VALUE_SQUARE               = 0x017f,
    LXB_CSS_VALUE_DECIMAL              = 0x0180,
    LXB_CSS_VALUE_LOWER_ROMAN          = 0x0181,
    LXB_CSS_VALUE_UPPER_ROMAN          = 0x0182,
    LXB_CSS_VALUE_LOWER_ALPHA          = 0x0183,
    LXB_CSS_VALUE_UPPER_ALPHA          = 0x0184,
    // Flex layout
    LXB_CSS_VALUE_SPACE_EVENLY         = 0x0185,
    // Background properties
    LXB_CSS_VALUE_CONTAIN              = 0x0186,  // background-size contain
    LXB_CSS_VALUE_COVER                = 0x0187,  // background-size cover
    LXB_CSS_VALUE_LOCAL                = 0x0188,  // background-attachment local
    LXB_CSS_VALUE_PADDING_BOX          = 0x0189,  // background-origin/clip padding-box
    LXB_CSS_VALUE_MULTIPLY             = 0x018a,  // background-blend-mode multiply
    LXB_CSS_VALUE_OVERLAY              = 0x018b,  // background-blend-mode overlay
    LXB_CSS_VALUE_ROUND                = 0x018c,  // background-repeat round
    LXB_CSS_VALUE_SPACE                = 0x018d,  // background-repeat space
    // Table properties
    LXB_CSS_VALUE_COLLAPSE_TABLE       = 0x018e,  // border-collapse collapse
    LXB_CSS_VALUE_SEPARATE             = 0x018f,  // border-collapse separate
    LXB_CSS_VALUE_HIDE                 = 0x0190,  // empty-cells hide
    LXB_CSS_VALUE_SHOW                 = 0x0191,  // empty-cells show
    // Grid layout
    LXB_CSS_VALUE_FIT_CONTENT          = 0x0192,
    LXB_CSS_VALUE_FR                   = 0x0193,
    LXB_CSS_VALUE_DENSE                = 0x0194,
    LXB_CSS_VALUE__LAST_ENTRY          = 0x0195
};

#define RDT_DISPLAY_TEXT                (LXB_CSS_VALUE__LAST_ENTRY + 10)
#define RDT_DISPLAY_REPLACED            (LXB_CSS_VALUE__LAST_ENTRY + 11)

// CSS unit type (replacing lexbor's lxb_css_unit_t)
typedef enum {
    LXB_CSS_UNIT_NONE = 0,
    LXB_CSS_UNIT_PX,
    LXB_CSS_UNIT_EM,
    LXB_CSS_UNIT_REM,
    LXB_CSS_UNIT_PERCENT,
    // Add more as needed
} lxb_css_unit_t;

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
    PropValue type;  // LXB_CSS_VALUE__LENGTH or LXB_CSS_VALUE__PERCENTAGE
    union {
        struct {
            float num;
            lxb_css_unit_t unit;
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
    PropValue type;  // LXB_CSS_VALUE__NUMBER, LXB_CSS_VALUE__LENGTH, LXB_CSS_VALUE__PERCENTAGE, LXB_CSS_VALUE_NORMAL
    union {
        struct {
            float num;
        } number;
        struct {
            float num;
            lxb_css_unit_t unit;
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
// inline const float LENGTH_AUTO = pack_as_nan(LXB_CSS_VALUE_AUTO);

// inline bool is_length_auto(float a) {
//     uint32_t ia;
//     memcpy(&ia, &a, sizeof(a));
//     return (ia & 0x003FFFFF) == LXB_CSS_VALUE_AUTO;
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
    PropValue box_sizing;  // LXB_CSS_VALUE_CONTENT_BOX or LXB_CSS_VALUE_BORDER_BOX
    PropValue white_space;  // LXB_CSS_VALUE_NORMAL, LXB_CSS_VALUE_NOWRAP, LXB_CSS_VALUE_PRE, etc.
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
    int align_self;  // AlignType or LXB_CSS_VALUE_*
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
    int direction;      // FlexDirection or LXB_CSS_VALUE_*
    int wrap;           // FlexWrap or LXB_CSS_VALUE_*
    int justify;        // JustifyContent or LXB_CSS_VALUE_*
    int align_items;    // AlignType or LXB_CSS_VALUE_*
    int align_content;  // AlignType or LXB_CSS_VALUE_*
    float row_gap;
    float column_gap;
    WritingMode writing_mode;
    TextDirection text_direction;
} FlexProp;

typedef struct GridTrackList GridTrackList;
typedef struct GridArea GridArea;
typedef struct GridProp {
    // Grid alignment properties (using Lexbor CSS constants)
    int justify_content;         // LXB_CSS_VALUE_START, etc.
    int align_content;           // LXB_CSS_VALUE_START, etc.
    int justify_items;           // LXB_CSS_VALUE_STRETCH, etc.
    int align_items;             // LXB_CSS_VALUE_STRETCH, etc.
    int grid_auto_flow;          // LXB_CSS_VALUE_ROW, LXB_CSS_VALUE_COLUMN
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
