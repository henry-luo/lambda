#include "layout.hpp"
#include "grid.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include <time.h>

extern "C" {
#include "../lib/log.h"
#include "../lib/mempool.h"
#include "../lib/hashmap.h"
}
void print_view_group(ViewGroup* view_group, StrBuf* buf, int indent, DocumentType doc_type);

static const css_data lxb_css_value_data[] = {
    {"_undef", 6, LXB_CSS_VALUE__UNDEF},
    {"initial", 7, LXB_CSS_VALUE_INITIAL},
    {"inherit", 7, LXB_CSS_VALUE_INHERIT},
    {"unset", 5, LXB_CSS_VALUE_UNSET},
    {"revert", 6, LXB_CSS_VALUE_REVERT},
    {"flex-start", 10, LXB_CSS_VALUE_FLEX_START},
    {"flex-end", 8, LXB_CSS_VALUE_FLEX_END},
    {"center", 6, LXB_CSS_VALUE_CENTER},
    {"space-between", 13, LXB_CSS_VALUE_SPACE_BETWEEN},
    {"space-around", 12, LXB_CSS_VALUE_SPACE_AROUND},
    {"stretch", 7, LXB_CSS_VALUE_STRETCH},
    {"baseline", 8, LXB_CSS_VALUE_BASELINE},
    {"auto", 4, LXB_CSS_VALUE_AUTO},
    {"text-bottom", 11, LXB_CSS_VALUE_TEXT_BOTTOM},
    {"alphabetic", 10, LXB_CSS_VALUE_ALPHABETIC},
    {"ideographic", 11, LXB_CSS_VALUE_IDEOGRAPHIC},
    {"middle", 6, LXB_CSS_VALUE_MIDDLE},
    {"central", 7, LXB_CSS_VALUE_CENTRAL},
    {"mathematical", 12, LXB_CSS_VALUE_MATHEMATICAL},
    {"text-top", 8, LXB_CSS_VALUE_TEXT_TOP},
    {"_length", 7, LXB_CSS_VALUE__LENGTH},
    {"_percentage", 11, LXB_CSS_VALUE__PERCENTAGE},
    {"sub", 3, LXB_CSS_VALUE_SUB},
    {"super", 5, LXB_CSS_VALUE_SUPER},
    {"top", 3, LXB_CSS_VALUE_TOP},
    {"bottom", 6, LXB_CSS_VALUE_BOTTOM},
    {"first", 5, LXB_CSS_VALUE_FIRST},
    {"last", 4, LXB_CSS_VALUE_LAST},
    {"thin", 4, LXB_CSS_VALUE_THIN},
    {"medium", 6, LXB_CSS_VALUE_MEDIUM},
    {"thick", 5, LXB_CSS_VALUE_THICK},
    {"none", 4, LXB_CSS_VALUE_NONE},
    {"hidden", 6, LXB_CSS_VALUE_HIDDEN},
    {"dotted", 6, LXB_CSS_VALUE_DOTTED},
    {"dashed", 6, LXB_CSS_VALUE_DASHED},
    {"solid", 5, LXB_CSS_VALUE_SOLID},
    {"double", 6, LXB_CSS_VALUE_DOUBLE},
    {"groove", 6, LXB_CSS_VALUE_GROOVE},
    {"ridge", 5, LXB_CSS_VALUE_RIDGE},
    {"inset", 5, LXB_CSS_VALUE_INSET},
    {"outset", 6, LXB_CSS_VALUE_OUTSET},
    {"content-box", 11, LXB_CSS_VALUE_CONTENT_BOX},
    {"border-box", 10, LXB_CSS_VALUE_BORDER_BOX},
    {"inline-start", 12, LXB_CSS_VALUE_INLINE_START},
    {"inline-end", 10, LXB_CSS_VALUE_INLINE_END},
    {"block-start", 11, LXB_CSS_VALUE_BLOCK_START},
    {"block-end", 9, LXB_CSS_VALUE_BLOCK_END},
    {"left", 4, LXB_CSS_VALUE_LEFT},
    {"right", 5, LXB_CSS_VALUE_RIGHT},
    {"currentcolor", 12, LXB_CSS_VALUE_CURRENTCOLOR},
    {"transparent", 11, LXB_CSS_VALUE_TRANSPARENT},
    {"hex", 3, LXB_CSS_VALUE_HEX},
    {"aliceblue", 9, LXB_CSS_VALUE_ALICEBLUE},
    {"antiquewhite", 12, LXB_CSS_VALUE_ANTIQUEWHITE},
    {"aqua", 4, LXB_CSS_VALUE_AQUA},
    {"aquamarine", 10, LXB_CSS_VALUE_AQUAMARINE},
    {"azure", 5, LXB_CSS_VALUE_AZURE},
    {"beige", 5, LXB_CSS_VALUE_BEIGE},
    {"bisque", 6, LXB_CSS_VALUE_BISQUE},
    {"black", 5, LXB_CSS_VALUE_BLACK},
    {"blanchedalmond", 14, LXB_CSS_VALUE_BLANCHEDALMOND},
    {"blue", 4, LXB_CSS_VALUE_BLUE},
    {"blueviolet", 10, LXB_CSS_VALUE_BLUEVIOLET},
    {"brown", 5, LXB_CSS_VALUE_BROWN},
    {"burlywood", 9, LXB_CSS_VALUE_BURLYWOOD},
    {"cadetblue", 9, LXB_CSS_VALUE_CADETBLUE},
    {"chartreuse", 10, LXB_CSS_VALUE_CHARTREUSE},
    {"chocolate", 9, LXB_CSS_VALUE_CHOCOLATE},
    {"coral", 5, LXB_CSS_VALUE_CORAL},
    {"cornflowerblue", 14, LXB_CSS_VALUE_CORNFLOWERBLUE},
    {"cornsilk", 8, LXB_CSS_VALUE_CORNSILK},
    {"crimson", 7, LXB_CSS_VALUE_CRIMSON},
    {"cyan", 4, LXB_CSS_VALUE_CYAN},
    {"darkblue", 8, LXB_CSS_VALUE_DARKBLUE},
    {"darkcyan", 8, LXB_CSS_VALUE_DARKCYAN},
    {"darkgoldenrod", 13, LXB_CSS_VALUE_DARKGOLDENROD},
    {"darkgray", 8, LXB_CSS_VALUE_DARKGRAY},
    {"darkgreen", 9, LXB_CSS_VALUE_DARKGREEN},
    {"darkgrey", 8, LXB_CSS_VALUE_DARKGREY},
    {"darkkhaki", 9, LXB_CSS_VALUE_DARKKHAKI},
    {"darkmagenta", 11, LXB_CSS_VALUE_DARKMAGENTA},
    {"darkolivegreen", 14, LXB_CSS_VALUE_DARKOLIVEGREEN},
    {"darkorange", 10, LXB_CSS_VALUE_DARKORANGE},
    {"darkorchid", 10, LXB_CSS_VALUE_DARKORCHID},
    {"darkred", 7, LXB_CSS_VALUE_DARKRED},
    {"darksalmon", 10, LXB_CSS_VALUE_DARKSALMON},
    {"darkseagreen", 12, LXB_CSS_VALUE_DARKSEAGREEN},
    {"darkslateblue", 13, LXB_CSS_VALUE_DARKSLATEBLUE},
    {"darkslategray", 13, LXB_CSS_VALUE_DARKSLATEGRAY},
    {"darkslategrey", 13, LXB_CSS_VALUE_DARKSLATEGREY},
    {"darkturquoise", 13, LXB_CSS_VALUE_DARKTURQUOISE},
    {"darkviolet", 10, LXB_CSS_VALUE_DARKVIOLET},
    {"deeppink", 8, LXB_CSS_VALUE_DEEPPINK},
    {"deepskyblue", 11, LXB_CSS_VALUE_DEEPSKYBLUE},
    {"dimgray", 7, LXB_CSS_VALUE_DIMGRAY},
    {"dimgrey", 7, LXB_CSS_VALUE_DIMGREY},
    {"dodgerblue", 10, LXB_CSS_VALUE_DODGERBLUE},
    {"firebrick", 9, LXB_CSS_VALUE_FIREBRICK},
    {"floralwhite", 11, LXB_CSS_VALUE_FLORALWHITE},
    {"forestgreen", 11, LXB_CSS_VALUE_FORESTGREEN},
    {"fuchsia", 7, LXB_CSS_VALUE_FUCHSIA},
    {"gainsboro", 9, LXB_CSS_VALUE_GAINSBORO},
    {"ghostwhite", 10, LXB_CSS_VALUE_GHOSTWHITE},
    {"gold", 4, LXB_CSS_VALUE_GOLD},
    {"goldenrod", 9, LXB_CSS_VALUE_GOLDENROD},
    {"gray", 4, LXB_CSS_VALUE_GRAY},
    {"green", 5, LXB_CSS_VALUE_GREEN},
    {"greenyellow", 11, LXB_CSS_VALUE_GREENYELLOW},
    {"grey", 4, LXB_CSS_VALUE_GREY},
    {"honeydew", 8, LXB_CSS_VALUE_HONEYDEW},
    {"hotpink", 7, LXB_CSS_VALUE_HOTPINK},
    {"indianred", 9, LXB_CSS_VALUE_INDIANRED},
    {"indigo", 6, LXB_CSS_VALUE_INDIGO},
    {"ivory", 5, LXB_CSS_VALUE_IVORY},
    {"khaki", 5, LXB_CSS_VALUE_KHAKI},
    {"lavender", 8, LXB_CSS_VALUE_LAVENDER},
    {"lavenderblush", 13, LXB_CSS_VALUE_LAVENDERBLUSH},
    {"lawngreen", 9, LXB_CSS_VALUE_LAWNGREEN},
    {"lemonchiffon", 12, LXB_CSS_VALUE_LEMONCHIFFON},
    {"lightblue", 9, LXB_CSS_VALUE_LIGHTBLUE},
    {"lightcoral", 10, LXB_CSS_VALUE_LIGHTCORAL},
    {"lightcyan", 9, LXB_CSS_VALUE_LIGHTCYAN},
    {"lightgoldenrodyellow", 20, LXB_CSS_VALUE_LIGHTGOLDENRODYELLOW},
    {"lightgray", 9, LXB_CSS_VALUE_LIGHTGRAY},
    {"lightgreen", 10, LXB_CSS_VALUE_LIGHTGREEN},
    {"lightgrey", 9, LXB_CSS_VALUE_LIGHTGREY},
    {"lightpink", 9, LXB_CSS_VALUE_LIGHTPINK},
    {"lightsalmon", 11, LXB_CSS_VALUE_LIGHTSALMON},
    {"lightseagreen", 13, LXB_CSS_VALUE_LIGHTSEAGREEN},
    {"lightskyblue", 12, LXB_CSS_VALUE_LIGHTSKYBLUE},
    {"lightslategray", 14, LXB_CSS_VALUE_LIGHTSLATEGRAY},
    {"lightslategrey", 14, LXB_CSS_VALUE_LIGHTSLATEGREY},
    {"lightsteelblue", 14, LXB_CSS_VALUE_LIGHTSTEELBLUE},
    {"lightyellow", 11, LXB_CSS_VALUE_LIGHTYELLOW},
    {"lime", 4, LXB_CSS_VALUE_LIME},
    {"limegreen", 9, LXB_CSS_VALUE_LIMEGREEN},
    {"linen", 5, LXB_CSS_VALUE_LINEN},
    {"magenta", 7, LXB_CSS_VALUE_MAGENTA},
    {"maroon", 6, LXB_CSS_VALUE_MAROON},
    {"mediumaquamarine", 16, LXB_CSS_VALUE_MEDIUMAQUAMARINE},
    {"mediumblue", 10, LXB_CSS_VALUE_MEDIUMBLUE},
    {"mediumorchid", 12, LXB_CSS_VALUE_MEDIUMORCHID},
    {"mediumpurple", 12, LXB_CSS_VALUE_MEDIUMPURPLE},
    {"mediumseagreen", 14, LXB_CSS_VALUE_MEDIUMSEAGREEN},
    {"mediumslateblue", 15, LXB_CSS_VALUE_MEDIUMSLATEBLUE},
    {"mediumspringgreen", 17, LXB_CSS_VALUE_MEDIUMSPRINGGREEN},
    {"mediumturquoise", 15, LXB_CSS_VALUE_MEDIUMTURQUOISE},
    {"mediumvioletred", 15, LXB_CSS_VALUE_MEDIUMVIOLETRED},
    {"midnightblue", 12, LXB_CSS_VALUE_MIDNIGHTBLUE},
    {"mintcream", 9, LXB_CSS_VALUE_MINTCREAM},
    {"mistyrose", 9, LXB_CSS_VALUE_MISTYROSE},
    {"moccasin", 8, LXB_CSS_VALUE_MOCCASIN},
    {"navajowhite", 11, LXB_CSS_VALUE_NAVAJOWHITE},
    {"navy", 4, LXB_CSS_VALUE_NAVY},
    {"oldlace", 7, LXB_CSS_VALUE_OLDLACE},
    {"olive", 5, LXB_CSS_VALUE_OLIVE},
    {"olivedrab", 9, LXB_CSS_VALUE_OLIVEDRAB},
    {"orange", 6, LXB_CSS_VALUE_ORANGE},
    {"orangered", 9, LXB_CSS_VALUE_ORANGERED},
    {"orchid", 6, LXB_CSS_VALUE_ORCHID},
    {"palegoldenrod", 13, LXB_CSS_VALUE_PALEGOLDENROD},
    {"palegreen", 9, LXB_CSS_VALUE_PALEGREEN},
    {"paleturquoise", 13, LXB_CSS_VALUE_PALETURQUOISE},
    {"palevioletred", 13, LXB_CSS_VALUE_PALEVIOLETRED},
    {"papayawhip", 10, LXB_CSS_VALUE_PAPAYAWHIP},
    {"peachpuff", 9, LXB_CSS_VALUE_PEACHPUFF},
    {"peru", 4, LXB_CSS_VALUE_PERU},
    {"pink", 4, LXB_CSS_VALUE_PINK},
    {"plum", 4, LXB_CSS_VALUE_PLUM},
    {"powderblue", 10, LXB_CSS_VALUE_POWDERBLUE},
    {"purple", 6, LXB_CSS_VALUE_PURPLE},
    {"rebeccapurple", 13, LXB_CSS_VALUE_REBECCAPURPLE},
    {"red", 3, LXB_CSS_VALUE_RED},
    {"rosybrown", 9, LXB_CSS_VALUE_ROSYBROWN},
    {"royalblue", 9, LXB_CSS_VALUE_ROYALBLUE},
    {"saddlebrown", 11, LXB_CSS_VALUE_SADDLEBROWN},
    {"salmon", 6, LXB_CSS_VALUE_SALMON},
    {"sandybrown", 10, LXB_CSS_VALUE_SANDYBROWN},
    {"seagreen", 8, LXB_CSS_VALUE_SEAGREEN},
    {"seashell", 8, LXB_CSS_VALUE_SEASHELL},
    {"sienna", 6, LXB_CSS_VALUE_SIENNA},
    {"silver", 6, LXB_CSS_VALUE_SILVER},
    {"skyblue", 7, LXB_CSS_VALUE_SKYBLUE},
    {"slateblue", 9, LXB_CSS_VALUE_SLATEBLUE},
    {"slategray", 9, LXB_CSS_VALUE_SLATEGRAY},
    {"slategrey", 9, LXB_CSS_VALUE_SLATEGREY},
    {"snow", 4, LXB_CSS_VALUE_SNOW},
    {"springgreen", 11, LXB_CSS_VALUE_SPRINGGREEN},
    {"steelblue", 9, LXB_CSS_VALUE_STEELBLUE},
    {"tan", 3, LXB_CSS_VALUE_TAN},
    {"teal", 4, LXB_CSS_VALUE_TEAL},
    {"thistle", 7, LXB_CSS_VALUE_THISTLE},
    {"tomato", 6, LXB_CSS_VALUE_TOMATO},
    {"turquoise", 9, LXB_CSS_VALUE_TURQUOISE},
    {"violet", 6, LXB_CSS_VALUE_VIOLET},
    {"wheat", 5, LXB_CSS_VALUE_WHEAT},
    {"white", 5, LXB_CSS_VALUE_WHITE},
    {"whitesmoke", 10, LXB_CSS_VALUE_WHITESMOKE},
    {"yellow", 6, LXB_CSS_VALUE_YELLOW},
    {"yellowgreen", 11, LXB_CSS_VALUE_YELLOWGREEN},
    {"Canvas", 6, LXB_CSS_VALUE_CANVAS},
    {"CanvasText", 10, LXB_CSS_VALUE_CANVASTEXT},
    {"LinkText", 8, LXB_CSS_VALUE_LINKTEXT},
    {"VisitedText", 11, LXB_CSS_VALUE_VISITEDTEXT},
    {"ActiveText", 10, LXB_CSS_VALUE_ACTIVETEXT},
    {"ButtonFace", 10, LXB_CSS_VALUE_BUTTONFACE},
    {"ButtonText", 10, LXB_CSS_VALUE_BUTTONTEXT},
    {"ButtonBorder", 12, LXB_CSS_VALUE_BUTTONBORDER},
    {"Field", 5, LXB_CSS_VALUE_FIELD},
    {"FieldText", 9, LXB_CSS_VALUE_FIELDTEXT},
    {"Highlight", 9, LXB_CSS_VALUE_HIGHLIGHT},
    {"HighlightText", 13, LXB_CSS_VALUE_HIGHLIGHTTEXT},
    {"SelectedItem", 12, LXB_CSS_VALUE_SELECTEDITEM},
    {"SelectedItemText", 16, LXB_CSS_VALUE_SELECTEDITEMTEXT},
    {"Mark", 4, LXB_CSS_VALUE_MARK},
    {"MarkText", 8, LXB_CSS_VALUE_MARKTEXT},
    {"GrayText", 8, LXB_CSS_VALUE_GRAYTEXT},
    {"AccentColor", 11, LXB_CSS_VALUE_ACCENTCOLOR},
    {"AccentColorText", 15, LXB_CSS_VALUE_ACCENTCOLORTEXT},
    {"rgb", 3, LXB_CSS_VALUE_RGB},
    {"rgba", 4, LXB_CSS_VALUE_RGBA},
    {"hsl", 3, LXB_CSS_VALUE_HSL},
    {"hsla", 4, LXB_CSS_VALUE_HSLA},
    {"hwb", 3, LXB_CSS_VALUE_HWB},
    {"lab", 3, LXB_CSS_VALUE_LAB},
    {"lch", 3, LXB_CSS_VALUE_LCH},
    {"oklab", 5, LXB_CSS_VALUE_OKLAB},
    {"oklch", 5, LXB_CSS_VALUE_OKLCH},
    {"color", 5, LXB_CSS_VALUE_COLOR},
    {"hand", 4, LXB_CSS_VALUE_HAND},
    {"pointer", 7, LXB_CSS_VALUE_POINTER},
    {"text", 4, LXB_CSS_VALUE_TEXT},
    {"wait", 4, LXB_CSS_VALUE_WAIT},
    {"progress", 8, LXB_CSS_VALUE_PROGRESS},
    {"grab", 4, LXB_CSS_VALUE_GRAB},
    {"grabbing", 8, LXB_CSS_VALUE_GRABBING},
    {"move", 4, LXB_CSS_VALUE_MOVE},
    {"ltr", 3, LXB_CSS_VALUE_LTR},
    {"rtl", 3, LXB_CSS_VALUE_RTL},
    {"block", 5, LXB_CSS_VALUE_BLOCK},
    {"inline", 6, LXB_CSS_VALUE_INLINE},
    {"run-in", 6, LXB_CSS_VALUE_RUN_IN},
    {"flow", 4, LXB_CSS_VALUE_FLOW},
    {"flow-root", 9, LXB_CSS_VALUE_FLOW_ROOT},
    {"table", 5, LXB_CSS_VALUE_TABLE},
    {"flex", 4, LXB_CSS_VALUE_FLEX},
    {"grid", 4, LXB_CSS_VALUE_GRID},
    {"ruby", 4, LXB_CSS_VALUE_RUBY},
    {"list-item", 9, LXB_CSS_VALUE_LIST_ITEM},
    {"table-row-group", 15, LXB_CSS_VALUE_TABLE_ROW_GROUP},
    {"table-header-group", 18, LXB_CSS_VALUE_TABLE_HEADER_GROUP},
    {"table-footer-group", 18, LXB_CSS_VALUE_TABLE_FOOTER_GROUP},
    {"table-row", 9, LXB_CSS_VALUE_TABLE_ROW},
    {"table-cell", 10, LXB_CSS_VALUE_TABLE_CELL},
    {"table-column-group", 18, LXB_CSS_VALUE_TABLE_COLUMN_GROUP},
    {"table-column", 12, LXB_CSS_VALUE_TABLE_COLUMN},
    {"table-caption", 13, LXB_CSS_VALUE_TABLE_CAPTION},
    {"ruby-base", 9, LXB_CSS_VALUE_RUBY_BASE},
    {"ruby-text", 9, LXB_CSS_VALUE_RUBY_TEXT},
    {"ruby-base-container", 19, LXB_CSS_VALUE_RUBY_BASE_CONTAINER},
    {"ruby-text-container", 19, LXB_CSS_VALUE_RUBY_TEXT_CONTAINER},
    {"contents", 8, LXB_CSS_VALUE_CONTENTS},
    {"inline-block", 12, LXB_CSS_VALUE_INLINE_BLOCK},
    {"inline-table", 12, LXB_CSS_VALUE_INLINE_TABLE},
    {"inline-flex", 11, LXB_CSS_VALUE_INLINE_FLEX},
    {"inline-grid", 11, LXB_CSS_VALUE_INLINE_GRID},
    {"hanging", 7, LXB_CSS_VALUE_HANGING},
    {"content", 7, LXB_CSS_VALUE_CONTENT},
    {"row", 3, LXB_CSS_VALUE_ROW},
    {"row-reverse", 11, LXB_CSS_VALUE_ROW_REVERSE},
    {"column", 6, LXB_CSS_VALUE_COLUMN},
    {"column-reverse", 14, LXB_CSS_VALUE_COLUMN_REVERSE},
    {"_number", 7, LXB_CSS_VALUE__NUMBER},
    {"nowrap", 6, LXB_CSS_VALUE_NOWRAP},
    {"wrap", 4, LXB_CSS_VALUE_WRAP},
    {"wrap-reverse", 12, LXB_CSS_VALUE_WRAP_REVERSE},
    {"snap-block", 10, LXB_CSS_VALUE_SNAP_BLOCK},
    {"start", 5, LXB_CSS_VALUE_START},
    {"end", 3, LXB_CSS_VALUE_END},
    {"near", 4, LXB_CSS_VALUE_NEAR},
    {"snap-inline", 11, LXB_CSS_VALUE_SNAP_INLINE},
    {"_integer", 8, LXB_CSS_VALUE__INTEGER},
    {"region", 6, LXB_CSS_VALUE_REGION},
    {"page", 4, LXB_CSS_VALUE_PAGE},
    {"serif", 5, LXB_CSS_VALUE_SERIF},
    {"sans-serif", 10, LXB_CSS_VALUE_SANS_SERIF},
    {"cursive", 7, LXB_CSS_VALUE_CURSIVE},
    {"fantasy", 7, LXB_CSS_VALUE_FANTASY},
    {"monospace", 9, LXB_CSS_VALUE_MONOSPACE},
    {"system-ui", 9, LXB_CSS_VALUE_SYSTEM_UI},
    {"emoji", 5, LXB_CSS_VALUE_EMOJI},
    {"math", 4, LXB_CSS_VALUE_MATH},
    {"fangsong", 8, LXB_CSS_VALUE_FANGSONG},
    {"ui-serif", 8, LXB_CSS_VALUE_UI_SERIF},
    {"ui-sans-serif", 13, LXB_CSS_VALUE_UI_SANS_SERIF},
    {"ui-monospace", 12, LXB_CSS_VALUE_UI_MONOSPACE},
    {"ui-rounded", 10, LXB_CSS_VALUE_UI_ROUNDED},
    {"xx-small", 8, LXB_CSS_VALUE_XX_SMALL},
    {"x-small", 7, LXB_CSS_VALUE_X_SMALL},
    {"small", 5, LXB_CSS_VALUE_SMALL},
    {"large", 5, LXB_CSS_VALUE_LARGE},
    {"x-large", 7, LXB_CSS_VALUE_X_LARGE},
    {"xx-large", 8, LXB_CSS_VALUE_XX_LARGE},
    {"xxx-large", 9, LXB_CSS_VALUE_XXX_LARGE},
    {"larger", 6, LXB_CSS_VALUE_LARGER},
    {"smaller", 7, LXB_CSS_VALUE_SMALLER},
    {"normal", 6, LXB_CSS_VALUE_NORMAL},
    {"ultra-condensed", 15, LXB_CSS_VALUE_ULTRA_CONDENSED},
    {"extra-condensed", 15, LXB_CSS_VALUE_EXTRA_CONDENSED},
    {"condensed", 9, LXB_CSS_VALUE_CONDENSED},
    {"semi-condensed", 14, LXB_CSS_VALUE_SEMI_CONDENSED},
    {"semi-expanded", 13, LXB_CSS_VALUE_SEMI_EXPANDED},
    {"expanded", 8, LXB_CSS_VALUE_EXPANDED},
    {"extra-expanded", 14, LXB_CSS_VALUE_EXTRA_EXPANDED},
    {"ultra-expanded", 14, LXB_CSS_VALUE_ULTRA_EXPANDED},
    {"italic", 6, LXB_CSS_VALUE_ITALIC},
    {"oblique", 7, LXB_CSS_VALUE_OBLIQUE},
    {"bold", 4, LXB_CSS_VALUE_BOLD},
    {"bolder", 6, LXB_CSS_VALUE_BOLDER},
    {"lighter", 7, LXB_CSS_VALUE_LIGHTER},
    {"force-end", 9, LXB_CSS_VALUE_FORCE_END},
    {"allow-end", 9, LXB_CSS_VALUE_ALLOW_END},
    {"min-content", 11, LXB_CSS_VALUE_MIN_CONTENT},
    {"max-content", 11, LXB_CSS_VALUE_MAX_CONTENT},
    {"_angle", 6, LXB_CSS_VALUE__ANGLE},
    {"manual", 6, LXB_CSS_VALUE_MANUAL},
    {"loose", 5, LXB_CSS_VALUE_LOOSE},
    {"strict", 6, LXB_CSS_VALUE_STRICT},
    {"anywhere", 8, LXB_CSS_VALUE_ANYWHERE},
    {"visible", 7, LXB_CSS_VALUE_VISIBLE},
    {"clip", 4, LXB_CSS_VALUE_CLIP},
    {"scroll", 6, LXB_CSS_VALUE_SCROLL},
    {"break-word", 10, LXB_CSS_VALUE_BREAK_WORD},
    {"static", 6, LXB_CSS_VALUE_STATIC},
    {"relative", 8, LXB_CSS_VALUE_RELATIVE},
    {"absolute", 8, LXB_CSS_VALUE_ABSOLUTE},
    {"sticky", 6, LXB_CSS_VALUE_STICKY},
    {"fixed", 5, LXB_CSS_VALUE_FIXED},
    {"justify", 7, LXB_CSS_VALUE_JUSTIFY},
    {"match-parent", 12, LXB_CSS_VALUE_MATCH_PARENT},
    {"justify-all", 11, LXB_CSS_VALUE_JUSTIFY_ALL},
    {"all", 3, LXB_CSS_VALUE_ALL},
    {"digits", 6, LXB_CSS_VALUE_DIGITS},
    {"underline", 9, LXB_CSS_VALUE_UNDERLINE},
    {"overline", 8, LXB_CSS_VALUE_OVERLINE},
    {"line-through", 12, LXB_CSS_VALUE_LINE_THROUGH},
    {"blink", 5, LXB_CSS_VALUE_BLINK},
    {"wavy", 4, LXB_CSS_VALUE_WAVY},
    {"each-line", 9, LXB_CSS_VALUE_EACH_LINE},
    {"inter-word", 10, LXB_CSS_VALUE_INTER_WORD},
    {"inter-character", 15, LXB_CSS_VALUE_INTER_CHARACTER},
    {"mixed", 5, LXB_CSS_VALUE_MIXED},
    {"upright", 7, LXB_CSS_VALUE_UPRIGHT},
    {"sideways", 8, LXB_CSS_VALUE_SIDEWAYS},
    {"ellipsis", 8, LXB_CSS_VALUE_ELLIPSIS},
    {"capitalize", 10, LXB_CSS_VALUE_CAPITALIZE},
    {"uppercase", 9, LXB_CSS_VALUE_UPPERCASE},
    {"lowercase", 9, LXB_CSS_VALUE_LOWERCASE},
    {"full-width", 10, LXB_CSS_VALUE_FULL_WIDTH},
    {"full-size-kana", 14, LXB_CSS_VALUE_FULL_SIZE_KANA},
    {"embed", 5, LXB_CSS_VALUE_EMBED},
    {"isolate", 7, LXB_CSS_VALUE_ISOLATE},
    {"bidi-override", 13, LXB_CSS_VALUE_BIDI_OVERRIDE},
    {"isolate-override", 16, LXB_CSS_VALUE_ISOLATE_OVERRIDE},
    {"plaintext", 9, LXB_CSS_VALUE_PLAINTEXT},
    {"collapse", 8, LXB_CSS_VALUE_COLLAPSE},
    {"pre", 3, LXB_CSS_VALUE_PRE},
    {"pre-wrap", 8, LXB_CSS_VALUE_PRE_WRAP},
    {"break-spaces", 12, LXB_CSS_VALUE_BREAK_SPACES},
    {"pre-line", 8, LXB_CSS_VALUE_PRE_LINE},
    {"keep-all", 8, LXB_CSS_VALUE_KEEP_ALL},
    {"break-all", 9, LXB_CSS_VALUE_BREAK_ALL},
    {"both", 4, LXB_CSS_VALUE_BOTH},
    {"minimum", 7, LXB_CSS_VALUE_MINIMUM},
    {"maximum", 7, LXB_CSS_VALUE_MAXIMUM},
    {"clear", 5, LXB_CSS_VALUE_CLEAR},
    {"horizontal-tb", 13, LXB_CSS_VALUE_HORIZONTAL_TB},
    {"vertical-rl", 11, LXB_CSS_VALUE_VERTICAL_RL},
    {"vertical-lr", 11, LXB_CSS_VALUE_VERTICAL_LR},
    {"sideways-rl", 11, LXB_CSS_VALUE_SIDEWAYS_RL},
    {"sideways-lr", 11, LXB_CSS_VALUE_SIDEWAYS_LR},
    // Custom values added from Lambda CSS resolve
    {"contain", 7, LXB_CSS_VALUE_CONTAIN},
    {"cover", 5, LXB_CSS_VALUE_COVER},
    {"local", 5, LXB_CSS_VALUE_LOCAL},
    {"padding-box", 11, LXB_CSS_VALUE_PADDING_BOX},
    {"multiply", 8, LXB_CSS_VALUE_MULTIPLY},
    {"overlay", 7, LXB_CSS_VALUE_OVERLAY},
    {"round", 5, LXB_CSS_VALUE_ROUND},
    {"space", 5, LXB_CSS_VALUE_SPACE},
    {"collapse-table", 14, LXB_CSS_VALUE_COLLAPSE_TABLE},  // Use different name to avoid conflict with LXB_CSS_VALUE_COLLAPSE
    {"separate", 8, LXB_CSS_VALUE_SEPARATE},
    {"hide", 4, LXB_CSS_VALUE_HIDE},
    {"show", 4, LXB_CSS_VALUE_SHOW},
    {"fit-content", 11, LXB_CSS_VALUE_FIT_CONTENT},
    {"fr", 2, LXB_CSS_VALUE_FR},
    {"dense", 5, LXB_CSS_VALUE_DENSE}
};

const css_data* css_value_by_id(PropValue id) {
    // Support both standard and custom value IDs
    if (id < LXB_CSS_VALUE__LAST_ENTRY) {
        return &lxb_css_value_data[id];
    }
    // For custom values beyond LXB_CSS_VALUE__LAST_ENTRY, calculate index
    if (id >= LXB_CSS_VALUE_CONTAIN && id <= LXB_CSS_VALUE_DENSE) {
        size_t custom_index = LXB_CSS_VALUE__LAST_ENTRY + (id - LXB_CSS_VALUE_CONTAIN);
        if (custom_index < sizeof(lxb_css_value_data) / sizeof(lxb_css_value_data[0])) {
            return &lxb_css_value_data[custom_index];
        }
    }
    return NULL;
}

// hash function for CSS keyword strings (case-insensitive)
static uint64_t css_keyword_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const char* str = *(const char**)item;
    // convert to lowercase for case-insensitive hashing
    char lower[256];
    size_t i = 0;
    while (str[i] && i < 255) {
        lower[i] = tolower((unsigned char)str[i]);
        i++;
    }
    lower[i] = '\0';
    return hashmap_sip(lower, i, seed0, seed1);
}

// comparison function for CSS keyword strings (case-insensitive)
static int css_keyword_compare(const void *a, const void *b, void *udata) {
    const char* str_a = *(const char**)a;
    const char* str_b = *(const char**)b;
    return strcasecmp(str_a, str_b);
}

// Look up CSS value by name (case-insensitive)
// Returns the LXB_CSS_VALUE enum, or LXB_CSS_VALUE__UNDEF if not found
PropValue css_value_by_name(const char* name) {
    if (!name) return LXB_CSS_VALUE__UNDEF;

    static HashMap* keyword_cache = NULL;
    static const size_t table_size = sizeof(lxb_css_value_data) / sizeof(lxb_css_value_data[0]);

    // initialize hashmap on first use
    if (!keyword_cache) {
        keyword_cache = hashmap_new(
            sizeof(const char*),  // key is pointer to string
            table_size,           // initial capacity
            0, 0,                 // seeds (0 means random)
            css_keyword_hash,
            css_keyword_compare,
            NULL,                 // no element free function
            (void*)lxb_css_value_data  // udata pointing to value table
        );

        // populate hashmap with all keywords
        for (size_t i = 0; i < table_size; i++) {
            const char* keyword = lxb_css_value_data[i].name;
            hashmap_set(keyword_cache, &keyword);
        }
    }

    // lookup in hashmap
    const char** result = (const char**)hashmap_get(keyword_cache, &name);
    if (result) {
        // find index in table to get the unique value
        const char* found_name = *result;
        for (size_t i = 0; i < table_size; i++) {
            if (lxb_css_value_data[i].name == found_name) {
                return lxb_css_value_data[i].unique;
            }
        }
    }

    return LXB_CSS_VALUE__UNDEF;
}

// Helper function to get view type name for JSON
const char* View::name() {
    switch (this->type) {
        case RDT_VIEW_BLOCK: return "block";
        case RDT_VIEW_INLINE_BLOCK: return "inline-block";
        case RDT_VIEW_LIST_ITEM: return "list-item";
        case RDT_VIEW_TABLE: return "table";
        case RDT_VIEW_TABLE_ROW_GROUP: return "table-row-group";
        case RDT_VIEW_TABLE_ROW: return "table-row";
        case RDT_VIEW_TABLE_CELL: return "table-cell";
        case RDT_VIEW_INLINE: return "inline";
        case RDT_VIEW_TEXT: return "text";
        case RDT_VIEW_BR: return "br";
        default: return "unknown";
    }
}

View* View::previous_view() {
    if (!parent || parent->child == this) return NULL;
    View* sibling = parent->child;
    while (sibling && sibling->next != this) { sibling = sibling->next; }
    return sibling;
}

View* alloc_view(LayoutContext* lycon, ViewType type, DomNodeBase* node) {
    View* view;
    ViewTree* tree = lycon->doc->view_tree;
    switch (type) {
        case RDT_VIEW_BLOCK:  case RDT_VIEW_INLINE_BLOCK:  case RDT_VIEW_LIST_ITEM:
            view = (ViewBlock*)pool_calloc(tree->pool, sizeof(ViewBlock));
            break;
        case RDT_VIEW_TABLE:
            view = (ViewTable*)pool_calloc(tree->pool, sizeof(ViewTable));
            // Initialize defaults
            ((ViewTable*)view)->table_layout = ViewTable::TABLE_LAYOUT_AUTO;
            // CRITICAL FIX: Set CSS default border-spacing to 2px
            // CSS 2.1 spec: initial value for border-spacing is 2px
            ((ViewTable*)view)->border_spacing_h = 2.0f;
            ((ViewTable*)view)->border_spacing_v = 2.0f;
            ((ViewTable*)view)->border_collapse = false; // default is separate borders
            break;
        case RDT_VIEW_TABLE_ROW_GROUP:
            view = (ViewTableRowGroup*)pool_calloc(tree->pool, sizeof(ViewTableRowGroup));
            break;
        case RDT_VIEW_TABLE_ROW:
            view = (ViewTableRow*)pool_calloc(tree->pool, sizeof(ViewTableRow));
            break;
        case RDT_VIEW_TABLE_CELL:
            view = (ViewTableCell*)pool_calloc(tree->pool, sizeof(ViewTableCell));
            // Initialize rowspan/colspan from DOM attributes (for Lambda CSS support)
            if (view && node) {
                ViewTableCell* cell = (ViewTableCell*)view;

                // Read colspan attribute
                const char* colspan_str = dom_node_get_attribute(node, "colspan");
                if (colspan_str && *colspan_str) {
                    int colspan = atoi(colspan_str);
                    cell->col_span = (colspan > 0) ? colspan : 1;
                } else {
                    cell->col_span = 1;
                }
                // Read rowspan attribute
                const char* rowspan_str = dom_node_get_attribute(node, "rowspan");
                if (rowspan_str && *rowspan_str) {
                    int rowspan = atoi(rowspan_str);
                    cell->row_span = (rowspan > 0) ? rowspan : 1;
                } else {
                    cell->row_span = 1;
                }
            }
            break;
        case RDT_VIEW_INLINE:
            view = (ViewSpan*)pool_calloc(tree->pool, sizeof(ViewSpan));
            break;
        case RDT_VIEW_TEXT:
            view = (ViewText*)pool_calloc(tree->pool, sizeof(ViewText));
            break;
        case RDT_VIEW_BR:
            view = (View*)pool_calloc(tree->pool, sizeof(View));
            break;
        default:
            log_debug("Unknown view type: %d", type);
            return NULL;
    }
    if (!view) {
        log_debug("Failed to allocate view: %d", type);
        return NULL;
    }
    view->type = type;  view->node = node;  view->parent = lycon->parent;

    // COMPREHENSIVE VIEW ALLOCATION TRACING
    fprintf(stderr, "[DOM DEBUG] alloc_view - created view %p, type=%d, node=%p", view, type, (void*)node);
    if (node) {
        fprintf(stderr, ", node_type=%d\n", node->type());
    } else {
        fprintf(stderr, ", node=NULL\n");
    }

    const char* node_name = node ? node->name() : "NULL";
    const char* parent_name = lycon->parent ? "has_parent" : "no_parent";
    log_debug("*** ALLOC_VIEW TRACE: Created view %p (type=%d) for node %s (%p), parent=%p (%s)",
        view, type, node_name, node, lycon->parent, parent_name);

    // CRITICAL FIX: Initialize flex properties for ViewBlocks
    if (type == RDT_VIEW_BLOCK || type == RDT_VIEW_INLINE_BLOCK || type == RDT_VIEW_LIST_ITEM) {
        ViewBlock* block = (ViewBlock*)view;
        // Initialize flex item properties with proper defaults
        block->flex_grow = 0.0f;
        block->flex_shrink = 1.0f;
        block->flex_basis = -1; // auto
        block->align_self = ALIGN_AUTO; // FIXED: Use ALIGN_AUTO as per CSS spec
        block->order = 0;
        block->flex_basis_is_percent = false;
    }

    // link the view
    if (lycon->prev_view) { lycon->prev_view->next = view; }
    else { if (lycon->parent) lycon->parent->child = view; }
    if (!lycon->line.start_view) lycon->line.start_view = view;

    // CRITICAL FIX: Also maintain ViewBlock hierarchy for flex layout
    if (view->is_block() && lycon->parent && lycon->parent->is_block()) {
        ViewBlock* block_child = (ViewBlock*)view;
        ViewBlock* block_parent = (ViewBlock*)lycon->parent;

        // Link in ViewBlock hierarchy
        if (block_parent->last_child) {
            block_parent->last_child->next_sibling = block_child;
            block_child->prev_sibling = block_parent->last_child;
        } else {
            block_parent->first_child = block_child;
        }
        block_parent->last_child = block_child;
    }
    lycon->view = view;
    return view;
}

void free_view(ViewTree* tree, View* view) {
    log_debug("free view %p, type %s", view, view->name());
    if (view->type >= RDT_VIEW_INLINE) {
        View* child = ((ViewGroup*)view)->child;
        while (child) {
            View* next = child->next;
            free_view(tree, child);
            child = next;
        }
        // free view property groups
        ViewSpan* span = (ViewSpan*)view;
        if (span->font) {
            log_debug("free font prop");
            // font-family could be static and not from the pool
            if (span->font->family) {
                pool_free(tree->pool, span->font->family);
            }
            pool_free(tree->pool, span->font);
        }
        if (span->in_line) {
            log_debug("free inline prop");
            pool_free(tree->pool, span->in_line);
        }
        if (span->bound) {
            log_debug("free bound prop");
            if (span->bound->background) pool_free(tree->pool, span->bound->background);
            if (span->bound->border) pool_free(tree->pool, span->bound->border);
            pool_free(tree->pool, span->bound);
        }
        if (view->is_block()) {
            ViewBlock* block = (ViewBlock*)view;
            if (block->blk) {
                log_debug("free block prop");
                pool_free(tree->pool, block->blk);
            }
            if (block->scroller) {
                log_debug("free scroller");
                if (block->scroller->pane) pool_free(tree->pool, block->scroller->pane);
                pool_free(tree->pool, block->scroller);
            }
        }
    }
    else { // text or br view
        log_debug("free text/br view");
    }
    pool_free(tree->pool, view);
}

void* alloc_prop(LayoutContext* lycon, size_t size) {
    void* prop = pool_calloc(lycon->doc->view_tree->pool, size);
    if (prop) {
        return prop;
    }
    else {
        log_debug("Failed to allocate property");
        return NULL;
    }
}

ScrollProp* alloc_scroll_prop(LayoutContext* lycon) {
    ScrollProp* prop = (ScrollProp*)alloc_prop(lycon, sizeof(ScrollProp));
    prop->overflow_x = prop->overflow_y = LXB_CSS_VALUE_VISIBLE;   // initial value
    prop->pane = (ScrollPane*)pool_calloc(lycon->doc->view_tree->pool, sizeof(ScrollPane));
    return prop;
}

BlockProp* alloc_block_prop(LayoutContext* lycon) {
    BlockProp* prop = (BlockProp*)alloc_prop(lycon, sizeof(BlockProp));
    prop->line_height = null;
    prop->text_align = lycon->block.text_align;  // inherit from parent
    prop->given_min_height = prop->given_min_width = prop->given_max_height = prop->given_max_width = -1;  // -1 for undefined
    prop->box_sizing = LXB_CSS_VALUE_CONTENT_BOX;  // default to content-box
    prop->given_width = prop->given_height = -1;  // -1 for not specified
    return prop;
}

FontProp* alloc_font_prop(LayoutContext* lycon) {
    FontProp* prop = (FontProp*)alloc_prop(lycon, sizeof(FontProp));
    // inherit parent font styles
    *prop = *lycon->font.style;  // including font family, size, weight, style, etc.
    assert(prop->font_size > 0);
    return prop;
}

FlexItemProp* alloc_flex_item_prop(LayoutContext* lycon) {
    FlexItemProp* prop = (FlexItemProp*)alloc_prop(lycon, sizeof(FlexItemProp));
    // set defaults
    prop->flex_shrink = 1;  prop->flex_basis = -1;  // -1 for auto
    prop->align_self = ALIGN_AUTO; // FIXED: Use ALIGN_AUTO as per CSS spec
    // prop->flex_grow = 0;  prop->order = 0;
    return prop;
}

PositionProp* alloc_position_prop(LayoutContext* lycon) {
    PositionProp* prop = (PositionProp*)alloc_prop(lycon, sizeof(PositionProp));
    // set defaults using actual Lexbor constants
    prop->position = LXB_CSS_VALUE_STATIC;  // default position
    prop->top = prop->right = prop->bottom = prop->left = 0;  // default offsets
    prop->z_index = 0;  // default z-index
    prop->has_top = prop->has_right = prop->has_bottom = prop->has_left = false;  // no offsets set
    prop->clear = LXB_CSS_VALUE_NONE;  // default clear
    prop->float_prop = LXB_CSS_VALUE_NONE;  // default float
    return prop;
}

// alloc flex container blk
void alloc_flex_prop(LayoutContext* lycon, ViewBlock* block) {
    if (!block->embed) {
        block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
    }
    if (!block->embed->flex) {
        FlexProp* prop = (FlexProp*)alloc_prop(lycon, sizeof(FlexProp));
        prop->direction = DIR_ROW;
        prop->wrap = WRAP_NOWRAP;
        prop->justify = JUSTIFY_START;
        prop->align_items = ALIGN_STRETCH;
        prop->align_content = ALIGN_STRETCH;  // CSS spec default for multi-line flex
        prop->row_gap = 0;  prop->column_gap = 0;
        block->embed->flex = prop;
    }
}

// alloc grid container prop
void alloc_grid_prop(LayoutContext* lycon, ViewBlock* block) {
    if (!block->embed) {
        block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
    }
    if (!block->embed->grid) {
        GridProp* grid = (GridProp*)alloc_prop(lycon, sizeof(GridProp));
        // Set default values using enum names that align with Lexbor constants
        grid->justify_content = LXB_CSS_VALUE_START;
        grid->align_content = LXB_CSS_VALUE_START;
        grid->justify_items = LXB_CSS_VALUE_STRETCH;
        grid->align_items = LXB_CSS_VALUE_STRETCH;
        grid->grid_auto_flow = LXB_CSS_VALUE_ROW;
        // Initialize gaps
        grid->row_gap = 0;
        grid->column_gap = 0;
        block->embed->grid = grid;
    }
}

void view_pool_init(ViewTree* tree) {
    log_debug("init view pool");
    tree->pool = pool_create();
    if (!tree->pool) {
        log_error("Failed to initialize view pool");
    }
    else {
        log_debug("view pool initialized");
    }
}

void view_pool_destroy(ViewTree* tree) {
    if (tree->pool) pool_destroy(tree->pool);
    tree->pool = NULL;
}

void print_inline_props(ViewSpan* span, StrBuf* buf, int indent, DocumentType doc_type) {
    if (span->in_line) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{");
        if (span->in_line->cursor) {
            char* cursor;
            switch (span->in_line->cursor) {
            case LXB_CSS_VALUE_POINTER:
                cursor = "pointer";  break;
            case LXB_CSS_VALUE_TEXT:
                cursor = "text";  break;
            default:
                cursor = (char*)css_value_by_id(span->in_line->cursor)->name;
            }
            strbuf_append_format(buf, "cursor:%s ", cursor);
        }
        if (span->in_line->color.c) {
            strbuf_append_format(buf, "color:#%x ", span->in_line->color.c);
        }
        if (span->in_line->vertical_align) {
            strbuf_append_format(buf, "vertical-align:%s ", css_value_by_id(span->in_line->vertical_align)->name);
        }
        strbuf_append_str(buf, "}\n");
    }
    if (span->font) {
        strbuf_append_char_n(buf, ' ', indent);

        // Handle font_weight differently for Lambda CSS vs Lexbor
        const char* weight_str;
        char weight_buf[16];
        if (doc_type == DOC_TYPE_LAMBDA_CSS) {
            // For Lambda CSS, font_weight is a numeric value (400, 700, etc.)
            snprintf(weight_buf, sizeof(weight_buf), "%d", span->font->font_weight);
            weight_str = weight_buf;
        } else {
            // For Lexbor, font_weight is an enum that can be looked up
            weight_str = (const char*)css_value_by_id(span->font->font_weight)->name;
        }

        strbuf_append_format(buf, "{font:{family:'%s', size:%d, style:%s, weight:%s, decoration:%s}}\n",
            span->font->family, span->font->font_size, css_value_by_id(span->font->font_style)->name,
            weight_str, css_value_by_id(span->font->text_deco)->name);
    }
    if (span->bound) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{");
        if (span->bound->background) {
            strbuf_append_format(buf, "bgcolor:#%x ", span->bound->background->color.c);
        }
        strbuf_append_format(buf, "margin:{left:%.1f, right:%.1f, top:%.1f, bottom:%.1f} ",
            span->bound->margin.left, span->bound->margin.right, span->bound->margin.top, span->bound->margin.bottom);
        strbuf_append_format(buf, "padding:{left:%.1f, right:%.1f, top:%.1f, bottom:%.1f}",
            span->bound->padding.left, span->bound->padding.right, span->bound->padding.top, span->bound->padding.bottom);
        strbuf_append_str(buf, "}\n");

        // border prop group
        if (span->bound->border) {
            strbuf_append_char_n(buf, ' ', indent);  strbuf_append_str(buf, "{");
            strbuf_append_format(buf, "border:{t-color:#%x, r-color:#%x, b-color:#%x, l-color:#%x,\n",
                span->bound->border->top_color.c, span->bound->border->right_color.c,
                span->bound->border->bottom_color.c, span->bound->border->left_color.c);
            strbuf_append_char_n(buf, ' ', indent);
            strbuf_append_format(buf, "  t-wd:%.1f, r-wd:%.1f, b-wd:%.1f, l-wd:%f, "
                "t-sty:%d, r-sty:%d, b-sty:%d, l-sty:%d\n",
                span->bound->border->width.top, span->bound->border->width.right,
                span->bound->border->width.bottom, span->bound->border->width.left,
                span->bound->border->top_style, span->bound->border->right_style,
                span->bound->border->bottom_style, span->bound->border->left_style);
            strbuf_append_char_n(buf, ' ', indent);
            strbuf_append_format(buf, "  tl-rd:%f, tr-rd:%f, br-rd:%f, bl-rd:%f}\n",
                span->bound->border->radius.top_left, span->bound->border->radius.top_right,
                span->bound->border->radius.bottom_right, span->bound->border->radius.bottom_left);
        }
    }
}

void print_block_props(ViewBlock* block, StrBuf* buf, int indent) {
    if (block->blk) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{");
        strbuf_append_format(buf, "line-hg:%.1f, ", block->blk->line_height);
        strbuf_append_format(buf, "txt-align:%s, ", css_value_by_id(block->blk->text_align)->name);
        strbuf_append_format(buf, "txt-indent:%.1f, ", block->blk->text_indent);
        strbuf_append_format(buf, "ls-sty-type:%d,\n", block->blk->list_style_type);
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_format(buf, " min-wd:%.1f, ", block->blk->given_min_width);
        strbuf_append_format(buf, "max-wd:%.1f, ", block->blk->given_max_width);
        strbuf_append_format(buf, "min-hg:%.1f, ", block->blk->given_min_height);
        strbuf_append_format(buf, "max-hg:%.1f, ", block->blk->given_max_height);
        if (block->blk->given_width >= 0) {
            strbuf_append_format(buf, "given-wd:%.1f, ", block->blk->given_width);
        }
        if (block->blk->given_height >= 0) {
            strbuf_append_format(buf, "given-hg:%.1f, ", block->blk->given_height);
        }
        if (block->blk->box_sizing == LXB_CSS_VALUE_BORDER_BOX) {
            strbuf_append_str(buf, "box-sizing:border-box");
        } else {
            strbuf_append_str(buf, "box-sizing:content-box");
        }
        strbuf_append_str(buf, "}\n");
    }

    // Add flex container debugging info
    if (block->embed && block->embed->flex) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{flex-container: ");

        // flex-direction
        const char* direction_str = "row";
        switch (block->embed->flex->direction) {
            case DIR_ROW: direction_str = "row"; break;
            case DIR_ROW_REVERSE: direction_str = "row-reverse"; break;
            case DIR_COLUMN: direction_str = "column"; break;
            case DIR_COLUMN_REVERSE: direction_str = "column-reverse"; break;
        }
        strbuf_append_format(buf, "direction:%s ", direction_str);

        // flex-wrap
        const char* wrap_str = "nowrap";
        switch (block->embed->flex->wrap) {
            case WRAP_NOWRAP: wrap_str = "nowrap"; break;
            case WRAP_WRAP: wrap_str = "wrap"; break;
            case WRAP_WRAP_REVERSE: wrap_str = "wrap-reverse"; break;
        }
        strbuf_append_format(buf, "wrap:%s ", wrap_str);

        // justify-content (handle custom value LXB_CSS_VALUE_SPACE_EVENLY = 0x0199)
        const char* justify_str = "flex-start";
        if (block->embed->flex->justify == LXB_CSS_VALUE_SPACE_EVENLY) {
            justify_str = "space-evenly";
        } else {
            const css_data* justify_value = css_value_by_id(block->embed->flex->justify);
            if (justify_value && justify_value->name) {
                justify_str = (const char*)justify_value->name;
            }
        }
        strbuf_append_format(buf, "justify:%s ", justify_str);

        // align-items (handle custom value for space-evenly)
        const char* align_items_str = "stretch";
        if (block->embed->flex->align_items == LXB_CSS_VALUE_SPACE_EVENLY) {
            align_items_str = "space-evenly";
        } else {
            const css_data* align_items_value = css_value_by_id(block->embed->flex->align_items);
            if (align_items_value && align_items_value->name) {
                align_items_str = (const char*)align_items_value->name;
            }
        }
        strbuf_append_format(buf, "align-items:%s ", align_items_str);

        // align-content (handle custom value for space-evenly)
        const char* align_content_str = "stretch";
        if (block->embed->flex->align_content == LXB_CSS_VALUE_SPACE_EVENLY) {
            align_content_str = "space-evenly";
        } else {
            const css_data* align_content_value = css_value_by_id(block->embed->flex->align_content);
            if (align_content_value && align_content_value->name) {
                align_content_str = (const char*)align_content_value->name;
            }
        }
        strbuf_append_format(buf, "align-content:%s ", align_content_str);

        strbuf_append_format(buf, "row-gap:%d column-gap:%d",
            block->embed->flex->row_gap, block->embed->flex->column_gap);
        strbuf_append_str(buf, "}\n");
    }

    if (block->scroller) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{");
        if (block->scroller->overflow_x) {
            const css_data* overflow_x_value = css_value_by_id(block->scroller->overflow_x);
            if (overflow_x_value && overflow_x_value->name) {
                strbuf_append_format(buf, "overflow-x:%s ", overflow_x_value->name);
            }
        }
        if (block->scroller->overflow_y) {
            const css_data* overflow_y_value = css_value_by_id(block->scroller->overflow_y);
            if (overflow_y_value && overflow_y_value->name) {
                strbuf_append_format(buf, "overflow-y:%s ", overflow_y_value->name);
            }
        }
        if (block->scroller->has_hz_overflow) {
            strbuf_append_str(buf, "hz-overflow:true ");
        }
        if (block->scroller->has_vt_overflow) { // corrected variable name
            strbuf_append_str(buf, "vt-overflow:true ");
        }
        if (block->scroller->has_hz_scroll) {
            strbuf_append_str(buf, "hz-scroll:true ");
        }
        if (block->scroller->has_vt_scroll) {
            strbuf_append_str(buf, "vt-scroll:true");
        }
        // strbuf_append_format(buf, "scrollbar:{v:%p, h:%p}", block->scroller->pane->v_scrollbar, block->scroller->pane->h_scrollbar);
        strbuf_append_str(buf, "}\n");
    }

    // Add position properties
    if (block->position) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{position:");
        if (block->position->position) {
            const css_data* pos_value = css_value_by_id(block->position->position);
            if (pos_value && pos_value->name) {
                strbuf_append_format(buf, "%s", pos_value->name);
            }
        }
        if (block->position->has_top) {
            strbuf_append_format(buf, ", top:%.1f", block->position->top);
        }
        if (block->position->has_right) {
            strbuf_append_format(buf, ", right:%.1f", block->position->right);
        }
        if (block->position->has_bottom) {
            strbuf_append_format(buf, ", bottom:%.1f", block->position->bottom);
        }
        if (block->position->has_left) {
            strbuf_append_format(buf, ", left:%.1f", block->position->left);
        }
        if (block->position->z_index != 0) {
            strbuf_append_format(buf, ", z-index:%d", block->position->z_index);
        }
        if (block->position->first_abs_child) {
            strbuf_append_str(buf, ", has-abs-child");
        }
        if (block->position->next_abs_sibling) {
            strbuf_append_str(buf, ", has-abs-sibling");
        }
        strbuf_append_str(buf, "}\n");
    }
}

void print_block(ViewBlock* block, StrBuf* buf, int indent, DocumentType doc_type) {
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_format(buf, "[view-%s:%s, x:%.1f, y:%.1f, wd:%.1f, hg:%.1f\n",
        block->name(), block->node->name(),
        (float)block->x, (float)block->y, (float)block->width, (float)block->height);
    print_block_props(block, buf, indent + 2);
    print_inline_props((ViewSpan*)block, buf, indent+2, doc_type);
    print_view_group((ViewGroup*)block, buf, indent+2, doc_type);
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "]\n");
}

void print_view_group(ViewGroup* view_group, StrBuf* buf, int indent, DocumentType doc_type) {
    View* view = view_group->child;
    if (view) {
        do {
            if (view->is_block()) {
                print_block((ViewBlock*)view, buf, indent, doc_type);
            }
            else if (view->type == RDT_VIEW_INLINE) {
                strbuf_append_char_n(buf, ' ', indent);
                ViewSpan* span = (ViewSpan*)view;
                strbuf_append_format(buf, "[view-inline:%s, x:%.1f, y:%.1f, wd:%.1f, hg:%.1f\n",
                    span->node->name(), (float)span->x, (float)span->y, (float)span->width, (float)span->height);
                print_inline_props(span, buf, indent + 2, doc_type);
                print_view_group((ViewGroup*)view, buf, indent + 2, doc_type);
                strbuf_append_char_n(buf, ' ', indent);
                strbuf_append_str(buf, "]\n");
            }
            else if (view->type == RDT_VIEW_BR) {
                strbuf_append_char_n(buf, ' ', indent);
                strbuf_append_format(buf, "[br: x:%.1f, y:%.1f, wd:%.1f, hg:%.1f]\n",
                    (float)view->x, (float)view->y, (float)view->width, (float)view->height);
            }
            else if (view->type == RDT_VIEW_TEXT) {
                ViewText* text = (ViewText*)view;
                unsigned char* text_data = view->node->text_data();
                strbuf_append_char_n(buf, ' ', indent);
                strbuf_append_format(buf, "[text: {x:%.1f, y:%.1f, wd:%.1f, hg:%.1f}\n",
                    text->x, text->y, text->width, text->height);
                TextRect* rect = text->rect;
                while (rect) {
                    strbuf_append_char_n(buf, ' ', indent+1);
                    unsigned char* str = text_data ? text_data + rect->start_index : nullptr;
                    if (!str || !(*str) || rect->length <= 0) {
                        strbuf_append_format(buf, "invalid text node: len:%d\n", rect->length);
                    } else {
                        strbuf_append_str(buf, "[rect:'");
                        strbuf_append_str_n(buf, (char*)str, rect->length);
                        // replace newline and '\'' with '^'
                        char* s = buf->str + buf->length - rect->length;
                        while (*s) {
                            if (*s == '\n' || *s == '\r') { *s = '^'; }
                            s++;
                        }
                        strbuf_append_format(buf, "', start:%d, len:%d, x:%.1f, y:%.1f, wd:%.1f, hg:%.1f]]\n",
                            rect->start_index, rect->length, rect->x, rect->y, rect->width, rect->height);
                    }
                    rect = rect->next;
                }
            }
            else {
                strbuf_append_char_n(buf, ' ', indent);
                strbuf_append_format(buf, "unknown-view: %d\n", view->type);
            }
            // a check for robustness
            if (view == view->next) { log_debug("invalid next view");  return; }
            view = view->next;
        } while (view);
    }
    // else no child view
}

void write_string_to_file(const char *filename, const char *text) {
    FILE *file = fopen(filename, "w"); // Open file in write mode
    if (file == NULL) {
        perror("Error opening file");
        return;
    }
    fprintf(file, "%s", text); // Write string to file
    fclose(file); // Close file
}

void print_view_tree(ViewGroup* view_root, Url* url, float pixel_ratio, DocumentType doc_type) {
    StrBuf* buf = strbuf_new_cap(1024);
    print_block((ViewBlock*)view_root, buf, 0, doc_type);
    log_debug("=================\nView tree:");
    log_debug("%s", buf->str);
    log_debug("=================\n");
    char vfile[1024];  const char *last_slash;
    if (url && url->pathname && url->pathname->chars) {
        last_slash = strrchr((const char*)url->pathname->chars, '/');
        snprintf(vfile, sizeof(vfile), "./test_output/view_tree_%s.txt", last_slash + 1);
        write_string_to_file(vfile, buf->str);
    }
    write_string_to_file("./view_tree.txt", buf->str);
    strbuf_free(buf);
    // also generate JSON output
    print_view_tree_json(view_root, url, pixel_ratio);
}

// Helper function to escape JSON strings
void append_json_string(StrBuf* buf, const char* str) {
    if (!str) {
        strbuf_append_str(buf, "null");
        return;
    }

    strbuf_append_char(buf, '"');
    for (const char* p = str; *p; p++) {
        switch (*p) {
            case '"': strbuf_append_str(buf, "\\\""); break;
            case '\\': strbuf_append_str(buf, "\\\\"); break;
            case '\n': strbuf_append_str(buf, "\\n"); break;
            case '\r': strbuf_append_str(buf, "\\r"); break;
            case '\t': strbuf_append_str(buf, "\\t"); break;
            default: strbuf_append_char(buf, *p); break;
        }
    }
    strbuf_append_char(buf, '"');
}

void print_bounds_json(View* view, StrBuf* buf, int indent, float pixel_ratio, TextRect* rect = nullptr) {
    // calculate absolute position for view
    float abs_x = rect ? rect->x : view->x, abs_y = rect ? rect->y : view->y;
    float initial_y = abs_y;
    const char* view_tag = view->node ? view->node->name() : "unknown";
    log_debug("[Coord] %s: initial y=%.2f", view_tag, initial_y);
    // Calculate absolute position by traversing up the parent chain
    ViewGroup* parent = view->parent;
    while (parent) {
        if (parent->is_block()) {
            const char* parent_tag = parent->node ? parent->node->name() : "unknown";
            log_debug("[Coord]   + parent %s: y=%.2f (abs_y: %.2f -> %.2f)", parent_tag, parent->y, abs_y, abs_y + parent->y);
            abs_x += parent->x;  abs_y += parent->y;
        }
        parent = parent->parent;
    }
    log_debug("[Coord] %s: final abs_y=%.2f", view_tag, abs_y);

    // Convert absolute view dimensions to CSS pixels
    float css_x = abs_x / pixel_ratio;
    float css_y = abs_y / pixel_ratio;
    float css_width = (rect ? rect->width : view->width) / pixel_ratio;
    float css_height = (rect ? rect->height : view->height) / pixel_ratio;

    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"x\": %.1f,\n", css_x);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"y\": %.1f,\n", css_y);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"width\": %.1f,\n", css_width);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"height\": %.1f\n", css_height);
}

// Recursive JSON generation for view blocks
void print_block_json(ViewBlock* block, StrBuf* buf, int indent, float pixel_ratio) {
    if (!block) {
        strbuf_append_str(buf, "null");
        return;
    }

    // Add indentation
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "{\n");

    // Basic view properties
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"type\": ");
    append_json_string(buf, block->name());
    strbuf_append_str(buf, ",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"tag\": ");

    // CRITICAL FIX: Provide better element names for debugging
    const char* tag_name = "unknown";
    if (block->node) {
        fprintf(stderr, "[DOM DEBUG] view_to_json accessing block %p, block->node=%p\n", (void*)block, (void*)block->node);
        const char* node_name = block->node->name();
        if (node_name) {
            // CRITICAL ISSUE: #null elements should not exist in proper DOM structure
            if (strcmp(node_name, "#null") == 0) {
                printf("ERROR: Found #null element! This indicates DOM structure issue.\n");
                printf("ERROR: Element details - parent: %p, parent_node: %p\n",
                       (void*)block->parent,
                       block->parent ? (void*)block->parent->node : nullptr);

                if (block->parent && block->parent->node) {
                    printf("ERROR: Parent node name: %s\n", block->parent->node->name());
                }

                // Try to infer the element type from context (TEMPORARY WORKAROUND)
                if (block->parent == nullptr) {
                    tag_name = "html";  // Root element should be html, not html-root
                    printf("WORKAROUND: Mapping root #null -> html\n");
                } else if (block->parent && block->parent->node &&
                          strcmp(block->parent->node->name(), "html") == 0) {
                    tag_name = "body";
                    printf("WORKAROUND: Mapping child of html #null -> body\n");
                } else {
                    tag_name = "div";  // Most #null elements are divs
                    printf("WORKAROUND: Mapping other #null -> div\n");
                }
            } else {
                tag_name = node_name;
                printf("DEBUG: Using proper node name: %s\n", node_name);
            }
        }
    } else {
        // No DOM node - try to infer from view type
        switch (block->type) {
            case RDT_VIEW_BLOCK:
                tag_name = "div";
                break;
            case RDT_VIEW_INLINE:
            case RDT_VIEW_INLINE_BLOCK:
                tag_name = "span";
                break;
            case RDT_VIEW_LIST_ITEM:
                tag_name = "li";
                break;
            default:
                tag_name = "unknown";
                break;
        }
    }

    append_json_string(buf, tag_name);
    strbuf_append_str(buf, ",\n");

    // ENHANCEMENT: Add CSS class information if available
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"selector\": ");

    // Generate enhanced CSS selector with nth-of-type support (matches browser behavior)
    if (block->node) {
        const char* class_attr = block->node->get_attribute("class");

        // Start with tag name and class
        char base_selector[256];
        if (class_attr) {
            size_t class_len = strlen(class_attr);
            snprintf(base_selector, sizeof(base_selector), "%s.%.*s", tag_name, (int)class_len, class_attr);
        } else {
            snprintf(base_selector, sizeof(base_selector), "%s", tag_name);
        }

        // Add nth-of-type if there are multiple siblings with same tag
        char final_selector[512];
        DomNodeBase* parent = block->node->parent;
        if (parent) {
            // Count siblings with same tag name
            int sibling_count = 0;
            int current_index = 0;
            DomNodeBase* sibling = parent->first_child;

            while (sibling) {
                if (sibling->type() == DOM_NODE_ELEMENT) {
                    const char* sibling_tag = sibling->name();
                    if (sibling_tag && strcmp(sibling_tag, tag_name) == 0) {
                        sibling_count++;
                        if (sibling == block->node) {
                            current_index = sibling_count; // 1-based index
                        }
                    }
                }
                sibling = sibling->next_sibling;
            }

            // Add nth-of-type if multiple siblings exist
            if (sibling_count > 1 && current_index > 0) {
                snprintf(final_selector, sizeof(final_selector), "%s:nth-of-type(%d)", base_selector, current_index);
            } else {
                snprintf(final_selector, sizeof(final_selector), "%s", base_selector);
            }
        } else {
            snprintf(final_selector, sizeof(final_selector), "%s", base_selector);
        }

        append_json_string(buf, final_selector);
    } else {
        append_json_string(buf, tag_name);
    }
    strbuf_append_str(buf, ",\n");

    // Add classes array (for test compatibility)
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"classes\": [");
    if (block->node) {
        const char* class_attr = block->node->get_attribute("class");
        if (class_attr) {
            size_t class_len = strlen(class_attr);
            // Output class names as array
            // For now, assume single class (TODO: split on whitespace for multiple classes)
            strbuf_append_char(buf, '\"');
            strbuf_append_str_n(buf, class_attr, class_len);
            strbuf_append_char(buf, '\"');
        }
    }
    strbuf_append_str(buf, "],\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"layout\": {\n");
    print_bounds_json(block, buf, indent, pixel_ratio);
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "},\n");

    // CRITICAL FIX: Use "computed" instead of "css_properties" to match test framework expectations
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"computed\": {\n");

    // Display property
    strbuf_append_char_n(buf, ' ', indent + 4);

    const char* display = "block";
    if (block->type == RDT_VIEW_INLINE_BLOCK) display = "inline-block";
    else if (block->type == RDT_VIEW_LIST_ITEM) display = "list-item";
    else if (block->type == RDT_VIEW_TABLE) display = "table";
    // CRITICAL FIX: Check for flex container
    else if (block->embed && block->embed->flex) display = "flex";
    strbuf_append_format(buf, "\"display\": \"%s\",\n", display);

    // Add block properties if available
    if (block->blk) {
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"line_height\": %.1f,\n", block->blk->line_height);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"text_align\": \"%s\",\n", css_value_by_id(block->blk->text_align)->name);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"text_indent\": %.1f,\n", block->blk->text_indent);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"min_width\": %.1f,\n", block->blk->given_min_width);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"max_width\": %.1f,\n", block->blk->given_max_width);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"min_height\": %.1f,\n", block->blk->given_min_height);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"max_height\": %.1f,\n", block->blk->given_max_height);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"box_sizing\": \"%s\",\n",
            block->blk->box_sizing == LXB_CSS_VALUE_BORDER_BOX ? "border-box" : "content-box");

        if (block->blk->given_width >= 0) {
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"given_width\": %.1f,\n", block->blk->given_width);
        }
        if (block->blk->given_height >= 0) {
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"given_height\": %.1f,\n", block->blk->given_height);
        }
    }

    // Add flex container properties if available
    if (block->embed && block->embed->flex) {
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "\"flex_container\": {\n");

        // flex-direction
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* direction_str = "row";
        switch (block->embed->flex->direction) {
            case DIR_ROW: direction_str = "row"; break;
            case DIR_ROW_REVERSE: direction_str = "row-reverse"; break;
            case DIR_COLUMN: direction_str = "column"; break;
            case DIR_COLUMN_REVERSE: direction_str = "column-reverse"; break;
        }
        strbuf_append_format(buf, "\"direction\": \"%s\",\n", direction_str);

        // flex-wrap
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* wrap_str = "nowrap";
        switch (block->embed->flex->wrap) {
            case WRAP_NOWRAP: wrap_str = "nowrap"; break;
            case WRAP_WRAP: wrap_str = "wrap"; break;
            case WRAP_WRAP_REVERSE: wrap_str = "wrap-reverse"; break;
        }
        strbuf_append_format(buf, "\"wrap\": \"%s\",\n", wrap_str);

        // justify-content (handle custom value LXB_CSS_VALUE_SPACE_EVENLY = 0x0199)
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* justify_str = "flex-start";
        if (block->embed->flex->justify == LXB_CSS_VALUE_SPACE_EVENLY) {
            justify_str = "space-evenly";
        } else {
            const css_data* justify_value = css_value_by_id(block->embed->flex->justify);
            if (justify_value && justify_value->name) {
                justify_str = (const char*)justify_value->name;
            }
        }
        strbuf_append_format(buf, "\"justify\": \"%s\",\n", justify_str);

        // align-items (handle custom value for space-evenly)
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* align_items_str = "stretch";
        if (block->embed->flex->align_items == LXB_CSS_VALUE_SPACE_EVENLY) {
            align_items_str = "space-evenly";
        } else {
            const css_data* align_items_value = css_value_by_id(block->embed->flex->align_items);
            if (align_items_value && align_items_value->name) {
                align_items_str = (const char*)align_items_value->name;
            }
        }
        strbuf_append_format(buf, "\"align_items\": \"%s\",\n", align_items_str);

        // align-content (handle custom value for space-evenly)
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* align_content_str = "stretch";
        if (block->embed->flex->align_content == LXB_CSS_VALUE_SPACE_EVENLY) {
            align_content_str = "space-evenly";
        } else {
            const css_data* align_content_value = css_value_by_id(block->embed->flex->align_content);
            if (align_content_value && align_content_value->name) {
                align_content_str = (const char*)align_content_value->name;
            }
        }
        strbuf_append_format(buf, "\"align_content\": \"%s\",\n", align_content_str);

        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"row_gap\": %.1f,\n", block->embed->flex->row_gap);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"column_gap\": %.1f\n", block->embed->flex->column_gap);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "},\n");
    }

    // Flexbox properties
    // Get actual flex-wrap value from the block
    const char* flex_wrap_str = "nowrap";  // default
    if (block->embed && block->embed->flex) {
        switch (block->embed->flex->wrap) {
            case WRAP_WRAP:
                flex_wrap_str = "wrap";
                break;
            case WRAP_WRAP_REVERSE:
                flex_wrap_str = "wrap-reverse";
                break;
            case WRAP_NOWRAP:
            default:
                flex_wrap_str = "nowrap";
                break;
        }
    }
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"flexWrap\": \"%s\",\n", flex_wrap_str);

    // Add boundary properties (margin, padding, border)
    if (block->bound) {
        // Margin properties
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "\"margin\": {\n");
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"top\": %.2f,\n", block->bound->margin.top);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"right\": %.2f,\n", block->bound->margin.right);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"bottom\": %.2f,\n", block->bound->margin.bottom);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"left\": %.2f\n", block->bound->margin.left);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "},\n");

        // Padding properties
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "\"padding\": {\n");
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"top\": %.2f,\n", block->bound->padding.top);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"right\": %.2f,\n", block->bound->padding.right);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"bottom\": %.2f,\n", block->bound->padding.bottom);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"left\": %.2f\n", block->bound->padding.left);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "},\n");

        // Border properties
        if (block->bound->border) {
            // Border width
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "\"borderWidth\": {\n");
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"top\": %.2f,\n", block->bound->border->width.top);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"right\": %.2f,\n", block->bound->border->width.right);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"bottom\": %.2f,\n", block->bound->border->width.bottom);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"left\": %.2f\n", block->bound->border->width.left);
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "},\n");

            // Border color
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "\"borderColor\": {\n");
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"top\": \"rgba(%d, %d, %d, %.2f)\",\n",
                block->bound->border->top_color.r,
                block->bound->border->top_color.g,
                block->bound->border->top_color.b,
                block->bound->border->top_color.a / 255.0f);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"right\": \"rgba(%d, %d, %d, %.2f)\",\n",
                block->bound->border->right_color.r,
                block->bound->border->right_color.g,
                block->bound->border->right_color.b,
                block->bound->border->right_color.a / 255.0f);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"bottom\": \"rgba(%d, %d, %d, %.2f)\",\n",
                block->bound->border->bottom_color.r,
                block->bound->border->bottom_color.g,
                block->bound->border->bottom_color.b,
                block->bound->border->bottom_color.a / 255.0f);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"left\": \"rgba(%d, %d, %d, %.2f)\"\n",
                block->bound->border->left_color.r,
                block->bound->border->left_color.g,
                block->bound->border->left_color.b,
                block->bound->border->left_color.a / 255.0f);
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "},\n");

            // Border radius
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "\"borderRadius\": {\n");
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"topLeft\": %.2f,\n", block->bound->border->radius.top_left);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"topRight\": %.2f,\n", block->bound->border->radius.top_right);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"bottomRight\": %.2f,\n", block->bound->border->radius.bottom_right);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"bottomLeft\": %.2f\n", block->bound->border->radius.bottom_left);
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "},\n");
        }

        // Background color
        if (block->bound->background) {
            fprintf(stderr, "[view_to_json] Background color: r=%d, g=%d, b=%d, a=%d (0x%08X)\n",
                    block->bound->background->color.r,
                    block->bound->background->color.g,
                    block->bound->background->color.b,
                    block->bound->background->color.a,
                    block->bound->background->color.c);
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"backgroundColor\": \"rgba(%d, %d, %d, %.2f)\",\n",
                block->bound->background->color.r,
                block->bound->background->color.g,
                block->bound->background->color.b,
                block->bound->background->color.a / 255.0f);
        }
    }

    // Position properties
    if (block->position) {
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "\"position\": {\n");
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"type\": \"%s\",\n", css_value_by_id(block->position->position)->name);
        if (block->position->has_top) {
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"top\": %.2f,\n", block->position->top);
        }
        if (block->position->has_right) {
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"right\": %.2f,\n", block->position->right);
        }
        if (block->position->has_bottom) {
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"bottom\": %.2f,\n", block->position->bottom);
        }
        if (block->position->has_left) {
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"left\": %.2f,\n", block->position->left);
        }
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"zIndex\": %d,\n", block->position->z_index);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"float\": \"%s\",\n", css_value_by_id(block->position->float_prop)->name);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"clear\": \"%s\"\n", css_value_by_id(block->position->clear)->name);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "},\n");
    }

    // Font properties (output for all elements, use defaults if not set)
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "\"font\": {\n");
    strbuf_append_char_n(buf, ' ', indent + 6);
    if (block->font && block->font->family) {
        strbuf_append_format(buf, "\"family\": \"%s\",\n", block->font->family);
    } else {
        // CSS default font-family (browser default is typically Times/serif)
        strbuf_append_str(buf, "\"family\": \"Times\",\n");
    }
    strbuf_append_char_n(buf, ' ', indent + 6);
    if (block->font && block->font->font_size > 0) {
        strbuf_append_format(buf, "\"size\": %.2f,\n", block->font->font_size);
    } else {
        // CSS default font-size is 16px (medium)
        strbuf_append_str(buf, "\"size\": 16,\n");
    }
    strbuf_append_char_n(buf, ' ', indent + 6);
    if (block->font && block->font->font_style) {
        const char* style_str = "normal";
        auto style_val = css_value_by_id(block->font->font_style);
        if (style_val) style_str = (const char*)style_val->name;
        strbuf_append_format(buf, "\"style\": \"%s\",\n", style_str);
    } else {
        // CSS default font-style is normal
        strbuf_append_str(buf, "\"style\": \"normal\",\n");
    }
    strbuf_append_char_n(buf, ' ', indent + 6);
    if (block->font && block->font->font_weight) {
        const char* weight_str = "normal";
        auto weight_val = css_value_by_id(block->font->font_weight);
        if (weight_val) weight_str = (const char*)weight_val->name;
        strbuf_append_format(buf, "\"weight\": \"%s\"\n", weight_str);
    } else {
        // CSS default font-weight is 400 (normal)
        strbuf_append_str(buf, "\"weight\": \"400\"\n");
    }
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "},\n");

    // Inline properties (color, opacity, etc.)
    if (block->in_line) {
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"color\": \"rgba(%d, %d, %d, %.2f)\",\n",
            block->in_line->color.r,
            block->in_line->color.g,
            block->in_line->color.b,
            block->in_line->color.a / 255.0f);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"opacity\": %.2f,\n", block->in_line->opacity);
    }

    // Remove trailing comma from last property
    // Note: we need to track if this is the last property, for now just ensure consistency
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "\"_cssPropertiesComplete\": true\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "},\n");

    // Children
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"children\": [\n");

    View* child = ((ViewGroup*)block)->child;
    bool first_child = true;
    while (child) {
        if (!first_child) {
            strbuf_append_str(buf, ",\n");
        }
        first_child = false;

        if (child->is_block()) {
            print_block_json((ViewBlock*)child, buf, indent + 4, pixel_ratio);
        }
        else if (child->type == RDT_VIEW_TEXT) {
            print_text_json((ViewText*)child, buf, indent + 4, pixel_ratio);
        }
        else if (child->type == RDT_VIEW_BR) {
            print_br_json(child, buf, indent + 4, pixel_ratio);
        }
        else if (child->type == RDT_VIEW_INLINE) {
            print_inline_json((ViewSpan*)child, buf, indent + 4, pixel_ratio);
        }
        else {
            // Handle other view types
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "{\n");
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_str(buf, "\"type\": ");
            append_json_string(buf, child->name());
            strbuf_append_str(buf, "\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "}");
        }

        child = child->next;
    }

    strbuf_append_str(buf, "\n");
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "]\n");

    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "}");
}

// JSON generation for text nodes
void print_text_json(ViewText* text, StrBuf* buf, int indent, float pixel_ratio) {
    TextRect* rect = text->rect;

    NEXT_RECT:
    bool is_last_char_space = false;
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "{\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"type\": \"text\",\n");

    // CRITICAL FIX: Add tag field for consistency with block elements
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"tag\": \"text\",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"selector\": \"text\",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"content\": ");

    if (text->node) {
        unsigned char* text_data = text->node->text_data();
        if (text_data && rect->length > 0) {
            char content[2048];
            int len = min(sizeof(content) - 1, rect->length);
            strncpy(content, (char*)(text_data + rect->start_index), len);
            content[len] = '\0';
            unsigned char last_char = content[len - 1];  // todo: this is not unicode-safe
            is_last_char_space = is_space(last_char);
            append_json_string(buf, content);
        } else {
            append_json_string(buf, "[empty]");
        }
    } else {
        append_json_string(buf, "[no-node]");
    }
    strbuf_append_str(buf, ",\n");

    // Add text fragment information (matching text output)
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"text_info\": {\n");
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"start_index\": %d,\n", rect->start_index);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"length\": %d\n", rect->length);
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "},\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"layout\": {\n");
    print_bounds_json(text, buf, indent, pixel_ratio, rect);

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "}\n");

    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "}");

    rect = rect->next;
    if (rect) { strbuf_append_str(buf, ",\n");  goto NEXT_RECT; }
}

void print_br_json(View* br, StrBuf* buf, int indent, float pixel_ratio) {
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "{\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"type\": \"br\",\n");

    // CRITICAL FIX: Add tag field for consistency with block elements
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"tag\": \"br\",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"selector\": \"br\",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"layout\": {\n");
    print_bounds_json(br, buf, indent, pixel_ratio);
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "}\n");

    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "}");
}

// JSON generation for inline elements (spans)
void print_inline_json(ViewSpan* span, StrBuf* buf, int indent, float pixel_ratio) {
    if (!span) {
        strbuf_append_str(buf, "null");
        return;
    }

    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "{\n");

    // Basic view properties
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"type\": ");
    append_json_string(buf, span->name());
    strbuf_append_str(buf, ",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"tag\": ");

    // Get tag name
    const char* tag_name = "span";
    if (span->node) {
        const char* node_name = span->node->name();
        if (node_name) {
            tag_name = node_name;
        }
    }
    append_json_string(buf, tag_name);
    strbuf_append_str(buf, ",\n");

    // Generate selector (same logic as blocks)
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"selector\": ");

    if (span->node) {
        const char* class_attr = span->node->get_attribute("class");

        // Start with tag name and class
        char base_selector[256];
        if (class_attr) {
            size_t class_len = strlen(class_attr);
            snprintf(base_selector, sizeof(base_selector), "%s.%.*s", tag_name, (int)class_len, class_attr);
        } else {
            snprintf(base_selector, sizeof(base_selector), "%s", tag_name);
        }

        // Add nth-of-type if there are multiple siblings with same tag
        char final_selector[512];
        DomNodeBase* parent = span->node->parent;
        if (parent) {
            // Count siblings with same tag name
            int sibling_count = 0;
            int current_index = 0;
            DomNodeBase* sibling = parent->first_child;

            while (sibling) {
                if (sibling->type() == DOM_NODE_ELEMENT) {
                    const char* sibling_tag = sibling->name();
                    if (sibling_tag && strcmp(sibling_tag, tag_name) == 0) {
                        sibling_count++;
                        if (sibling == span->node) {
                            current_index = sibling_count; // 1-based index
                        }
                    }
                }
                sibling = sibling->next_sibling;
            }

            // Add nth-of-type if multiple siblings exist
            if (sibling_count > 1 && current_index > 0) {
                snprintf(final_selector, sizeof(final_selector), "%s:nth-of-type(%d)", base_selector, current_index);
            } else {
                snprintf(final_selector, sizeof(final_selector), "%s", base_selector);
            }
        } else {
            snprintf(final_selector, sizeof(final_selector), "%s", base_selector);
        }

        append_json_string(buf, final_selector);
    } else {
        append_json_string(buf, tag_name);
    }
    strbuf_append_str(buf, ",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"layout\": {\n");
    print_bounds_json(span, buf, indent, pixel_ratio);
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "},\n");

    // CSS properties (enhanced to match text output)
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"computed\": {\n");
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "\"display\": \"inline\"");

    // Add inline properties if available
    if (span->in_line) {
        if (span->in_line->cursor) {
            const char* cursor = "default";
            switch (span->in_line->cursor) {
                case LXB_CSS_VALUE_POINTER: cursor = "pointer"; break;
                case LXB_CSS_VALUE_TEXT: cursor = "text"; break;
                default: cursor = (const char*)css_value_by_id(span->in_line->cursor)->name; break;
            }
            strbuf_append_str(buf, ",\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"cursor\": \"%s\"", cursor);
        }
        if (span->in_line->color.c) {
            strbuf_append_str(buf, ",\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"color\": \"#%06x\"", span->in_line->color.c);
        }
        if (span->in_line->vertical_align) {
            strbuf_append_str(buf, ",\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"vertical_align\": \"%s\"", css_value_by_id(span->in_line->vertical_align)->name);
        }
    }

    // Add font properties if available
    if (span->font) {
        strbuf_append_str(buf, ",\n");
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "\"font\": {\n");
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"family\": \"%s\",\n", span->font->family);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"size\": %f,\n", span->font->font_size);
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* style_str = "normal";
        auto style_val = css_value_by_id(span->font->font_style);
        if (style_val) style_str = (const char*)style_val->name;
        strbuf_append_format(buf, "\"style\": \"%s\",\n", style_str);
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* weight_str = "normal";
        auto weight_val = css_value_by_id(span->font->font_weight);
        if (weight_val) weight_str = (const char*)weight_val->name;
        strbuf_append_format(buf, "\"weight\": \"%s\",\n", weight_str);
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* deco_str = "none";
        auto deco_val = css_value_by_id(span->font->text_deco);
        if (deco_val) deco_str = (const char*)deco_val->name;
        strbuf_append_format(buf, "\"decoration\": \"%s\"\n", deco_str);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "}");
    }

    strbuf_append_str(buf, "\n");
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "},\n");

    // Children (this is the critical part - process span children!)
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"children\": [\n");

    View* child = ((ViewGroup*)span)->child;
    bool first_child = true;
    while (child) {
        if (!first_child) {
            strbuf_append_str(buf, ",\n");
        }
        first_child = false;

        if (child->type == RDT_VIEW_TEXT) {
            print_text_json((ViewText*)child, buf, indent + 4, pixel_ratio);
        }
        else if (child->type == RDT_VIEW_BR) {
            print_br_json(child, buf, indent + 4, pixel_ratio);
        }
        else if (child->type == RDT_VIEW_INLINE) {
            // Nested inline elements
            print_inline_json((ViewSpan*)child, buf, indent + 4, pixel_ratio);
        } else {
            // Handle other child types
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "{\n");
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_str(buf, "\"type\": ");
            append_json_string(buf, child->name());
            strbuf_append_str(buf, "\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "}");
        }

        child = child->next;
    }

    strbuf_append_str(buf, "\n");
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "]\n");

    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "}");
}

// Main JSON generation function
void print_view_tree_json(ViewGroup* view_root, Url* url, float pixel_ratio) {
    log_debug("Generating JSON layout data...");
    StrBuf* json_buf = strbuf_new_cap(2048);

    strbuf_append_str(json_buf, "{\n");
    strbuf_append_str(json_buf, "  \"test_info\": {\n");

    // Add timestamp
    strbuf_append_str(json_buf, "    \"timestamp\": \"");
    time_t now = time(0);
    char* time_str = ctime(&now);
    if (time_str) {
        time_str[strlen(time_str) - 1] = '\0'; // Remove newline
        strbuf_append_str(json_buf, time_str);
    }
    strbuf_append_str(json_buf, "\",\n");

    strbuf_append_str(json_buf, "    \"radiant_version\": \"1.0\",\n");
    strbuf_append_format(json_buf, "    \"pixel_ratio\": %.2f,\n", pixel_ratio);
    strbuf_append_str(json_buf, "    \"viewport\": { \"width\": 1200, \"height\": 800 }\n");
    strbuf_append_str(json_buf, "  },\n");

    strbuf_append_str(json_buf, "  \"layout_tree\": ");
    if (view_root) {
        print_block_json((ViewBlock*)view_root, json_buf, 2, pixel_ratio);
    } else {
        strbuf_append_str(json_buf, "null");
    }
    strbuf_append_str(json_buf, "\n}\n");

    // Write to file in both ./ and /tmp directory for easier access
    char buf[1024];  const char *last_slash;
    if (url && url->pathname && url->pathname->chars) {
        last_slash = strrchr((const char*)url->pathname->chars, '/');
        snprintf(buf, sizeof(buf), "./test_output/view_tree_%s.json", last_slash + 1);
        log_debug("Writing JSON layout data to: %s", buf);
        write_string_to_file(buf, json_buf->str);
    }
    write_string_to_file("/tmp/view_tree.json", json_buf->str);
    strbuf_free(json_buf);
}
