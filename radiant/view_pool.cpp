#include "layout.hpp"
#include "grid.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include <time.h>

extern "C" {
#include "../lib/log.h"
#include "../lib/mempool.h"
#include "../lib/hashmap.h"
}
void print_view_group(ViewGroup* view_group, StrBuf* buf, int indent);

static const CssEnumInfo css_value_definitions[] = {
    {"_undef", 6, CSS_VALUE__UNDEF, CSS_VALUE_GROUP__UNDEF},
    {"_length", 7, CSS_VALUE__LENGTH, CSS_VALUE_GROUP_SPECIAL_TYPE},
    {"_percentage", 11, CSS_VALUE__PERCENTAGE, CSS_VALUE_GROUP_SPECIAL_TYPE},
    {"_number", 7, CSS_VALUE__NUMBER, CSS_VALUE_GROUP_SPECIAL_TYPE},
    {"_integer", 8, CSS_VALUE__INTEGER, CSS_VALUE_GROUP_SPECIAL_TYPE},
    {"_angle", 6, CSS_VALUE__ANGLE, CSS_VALUE_GROUP_SPECIAL_TYPE},
    {"initial", 7, CSS_VALUE_INITIAL, CSS_VALUE_GROUP_GLOBAL},
    {"inherit", 7, CSS_VALUE_INHERIT, CSS_VALUE_GROUP_GLOBAL},
    {"unset", 5, CSS_VALUE_UNSET, CSS_VALUE_GROUP_GLOBAL},
    {"revert", 6, CSS_VALUE_REVERT, CSS_VALUE_GROUP_GLOBAL},
    {"flex-start", 10, CSS_VALUE_FLEX_START, CSS_VALUE_GROUP_ALIGNMENT},
    {"flex-end", 8, CSS_VALUE_FLEX_END, CSS_VALUE_GROUP_ALIGNMENT},
    {"center", 6, CSS_VALUE_CENTER, CSS_VALUE_GROUP_ALIGNMENT},
    {"space-between", 13, CSS_VALUE_SPACE_BETWEEN, CSS_VALUE_GROUP_ALIGNMENT},
    {"space-around", 12, CSS_VALUE_SPACE_AROUND, CSS_VALUE_GROUP_ALIGNMENT},
    {"stretch", 7, CSS_VALUE_STRETCH, CSS_VALUE_GROUP_ALIGNMENT},
    {"baseline", 8, CSS_VALUE_BASELINE, CSS_VALUE_GROUP_ALIGNMENT},
    {"auto", 4, CSS_VALUE_AUTO, CSS_VALUE_GROUP_SIZE},
    {"text-bottom", 11, CSS_VALUE_TEXT_BOTTOM, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"alphabetic", 10, CSS_VALUE_ALPHABETIC, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"ideographic", 11, CSS_VALUE_IDEOGRAPHIC, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"middle", 6, CSS_VALUE_MIDDLE, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"central", 7, CSS_VALUE_CENTRAL, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"mathematical", 12, CSS_VALUE_MATHEMATICAL, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"text-top", 8, CSS_VALUE_TEXT_TOP, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"sub", 3, CSS_VALUE_SUB, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"super", 5, CSS_VALUE_SUPER, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"top", 3, CSS_VALUE_TOP, CSS_VALUE_GROUP_POSITION_SIDE},
    {"bottom", 6, CSS_VALUE_BOTTOM, CSS_VALUE_GROUP_POSITION_SIDE},
    {"first", 5, CSS_VALUE_FIRST, CSS_VALUE_GROUP_POSITION_SIDE},
    {"last", 4, CSS_VALUE_LAST, CSS_VALUE_GROUP_POSITION_SIDE},
    {"thin", 4, CSS_VALUE_THIN, CSS_VALUE_GROUP_BORDER_WIDTH},
    {"medium", 6, CSS_VALUE_MEDIUM, CSS_VALUE_GROUP_BORDER_WIDTH},
    {"thick", 5, CSS_VALUE_THICK, CSS_VALUE_GROUP_BORDER_WIDTH},
    {"none", 4, CSS_VALUE_NONE, CSS_VALUE_GROUP_BORDER_STYLE},
    {"hidden", 6, CSS_VALUE_HIDDEN, CSS_VALUE_GROUP_BORDER_STYLE},
    {"dotted", 6, CSS_VALUE_DOTTED, CSS_VALUE_GROUP_BORDER_STYLE},
    {"dashed", 6, CSS_VALUE_DASHED, CSS_VALUE_GROUP_BORDER_STYLE},
    {"solid", 5, CSS_VALUE_SOLID, CSS_VALUE_GROUP_BORDER_STYLE},
    {"double", 6, CSS_VALUE_DOUBLE, CSS_VALUE_GROUP_BORDER_STYLE},
    {"groove", 6, CSS_VALUE_GROOVE, CSS_VALUE_GROUP_BORDER_STYLE},
    {"ridge", 5, CSS_VALUE_RIDGE, CSS_VALUE_GROUP_BORDER_STYLE},
    {"inset", 5, CSS_VALUE_INSET, CSS_VALUE_GROUP_BORDER_STYLE},
    {"outset", 6, CSS_VALUE_OUTSET, CSS_VALUE_GROUP_BORDER_STYLE},
    {"content-box", 11, CSS_VALUE_CONTENT_BOX, CSS_VALUE_GROUP_BOX_MODEL},
    {"border-box", 10, CSS_VALUE_BORDER_BOX, CSS_VALUE_GROUP_BOX_MODEL},
    {"inline-start", 12, CSS_VALUE_INLINE_START, CSS_VALUE_GROUP_LOGICAL_SIDE},
    {"inline-end", 10, CSS_VALUE_INLINE_END, CSS_VALUE_GROUP_LOGICAL_SIDE},
    {"block-start", 11, CSS_VALUE_BLOCK_START, CSS_VALUE_GROUP_LOGICAL_SIDE},
    {"block-end", 9, CSS_VALUE_BLOCK_END, CSS_VALUE_GROUP_LOGICAL_SIDE},
    {"left", 4, CSS_VALUE_LEFT, CSS_VALUE_GROUP_POSITION_SIDE},
    {"right", 5, CSS_VALUE_RIGHT, CSS_VALUE_GROUP_POSITION_SIDE},
    {"currentcolor", 12, CSS_VALUE_CURRENTCOLOR, CSS_VALUE_GROUP_COLOR},
    {"transparent", 11, CSS_VALUE_TRANSPARENT, CSS_VALUE_GROUP_COLOR},
    {"hex", 3, CSS_VALUE_HEX, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"aliceblue", 9, CSS_VALUE_ALICEBLUE, CSS_VALUE_GROUP_COLOR},
    {"antiquewhite", 12, CSS_VALUE_ANTIQUEWHITE, CSS_VALUE_GROUP_COLOR},
    {"aqua", 4, CSS_VALUE_AQUA, CSS_VALUE_GROUP_COLOR},
    {"aquamarine", 10, CSS_VALUE_AQUAMARINE, CSS_VALUE_GROUP_COLOR},
    {"azure", 5, CSS_VALUE_AZURE, CSS_VALUE_GROUP_COLOR},
    {"beige", 5, CSS_VALUE_BEIGE, CSS_VALUE_GROUP_COLOR},
    {"bisque", 6, CSS_VALUE_BISQUE, CSS_VALUE_GROUP_COLOR},
    {"black", 5, CSS_VALUE_BLACK, CSS_VALUE_GROUP_COLOR},
    {"blanchedalmond", 14, CSS_VALUE_BLANCHEDALMOND, CSS_VALUE_GROUP_COLOR},
    {"blue", 4, CSS_VALUE_BLUE, CSS_VALUE_GROUP_COLOR},
    {"blueviolet", 10, CSS_VALUE_BLUEVIOLET, CSS_VALUE_GROUP_COLOR},
    {"brown", 5, CSS_VALUE_BROWN, CSS_VALUE_GROUP_COLOR},
    {"burlywood", 9, CSS_VALUE_BURLYWOOD, CSS_VALUE_GROUP_COLOR},
    {"cadetblue", 9, CSS_VALUE_CADETBLUE, CSS_VALUE_GROUP_COLOR},
    {"chartreuse", 10, CSS_VALUE_CHARTREUSE, CSS_VALUE_GROUP_COLOR},
    {"chocolate", 9, CSS_VALUE_CHOCOLATE, CSS_VALUE_GROUP_COLOR},
    {"coral", 5, CSS_VALUE_CORAL, CSS_VALUE_GROUP_COLOR},
    {"cornflowerblue", 14, CSS_VALUE_CORNFLOWERBLUE, CSS_VALUE_GROUP_COLOR},
    {"cornsilk", 8, CSS_VALUE_CORNSILK, CSS_VALUE_GROUP_COLOR},
    {"crimson", 7, CSS_VALUE_CRIMSON, CSS_VALUE_GROUP_COLOR},
    {"cyan", 4, CSS_VALUE_CYAN, CSS_VALUE_GROUP_COLOR},
    {"darkblue", 8, CSS_VALUE_DARKBLUE, CSS_VALUE_GROUP_COLOR},
    {"darkcyan", 8, CSS_VALUE_DARKCYAN, CSS_VALUE_GROUP_COLOR},
    {"darkgoldenrod", 13, CSS_VALUE_DARKGOLDENROD, CSS_VALUE_GROUP_COLOR},
    {"darkgray", 8, CSS_VALUE_DARKGRAY, CSS_VALUE_GROUP_COLOR},
    {"darkgreen", 9, CSS_VALUE_DARKGREEN, CSS_VALUE_GROUP_COLOR},
    {"darkgrey", 8, CSS_VALUE_DARKGREY, CSS_VALUE_GROUP_COLOR},
    {"darkkhaki", 9, CSS_VALUE_DARKKHAKI, CSS_VALUE_GROUP_COLOR},
    {"darkmagenta", 11, CSS_VALUE_DARKMAGENTA, CSS_VALUE_GROUP_COLOR},
    {"darkolivegreen", 14, CSS_VALUE_DARKOLIVEGREEN, CSS_VALUE_GROUP_COLOR},
    {"darkorange", 10, CSS_VALUE_DARKORANGE, CSS_VALUE_GROUP_COLOR},
    {"darkorchid", 10, CSS_VALUE_DARKORCHID, CSS_VALUE_GROUP_COLOR},
    {"darkred", 7, CSS_VALUE_DARKRED, CSS_VALUE_GROUP_COLOR},
    {"darksalmon", 10, CSS_VALUE_DARKSALMON, CSS_VALUE_GROUP_COLOR},
    {"darkseagreen", 12, CSS_VALUE_DARKSEAGREEN, CSS_VALUE_GROUP_COLOR},
    {"darkslateblue", 13, CSS_VALUE_DARKSLATEBLUE, CSS_VALUE_GROUP_COLOR},
    {"darkslategray", 13, CSS_VALUE_DARKSLATEGRAY, CSS_VALUE_GROUP_COLOR},
    {"darkslategrey", 13, CSS_VALUE_DARKSLATEGREY, CSS_VALUE_GROUP_COLOR},
    {"darkturquoise", 13, CSS_VALUE_DARKTURQUOISE, CSS_VALUE_GROUP_COLOR},
    {"darkviolet", 10, CSS_VALUE_DARKVIOLET, CSS_VALUE_GROUP_COLOR},
    {"deeppink", 8, CSS_VALUE_DEEPPINK, CSS_VALUE_GROUP_COLOR},
    {"deepskyblue", 11, CSS_VALUE_DEEPSKYBLUE, CSS_VALUE_GROUP_COLOR},
    {"dimgray", 7, CSS_VALUE_DIMGRAY, CSS_VALUE_GROUP_COLOR},
    {"dimgrey", 7, CSS_VALUE_DIMGREY, CSS_VALUE_GROUP_COLOR},
    {"dodgerblue", 10, CSS_VALUE_DODGERBLUE, CSS_VALUE_GROUP_COLOR},
    {"firebrick", 9, CSS_VALUE_FIREBRICK, CSS_VALUE_GROUP_COLOR},
    {"floralwhite", 11, CSS_VALUE_FLORALWHITE, CSS_VALUE_GROUP_COLOR},
    {"forestgreen", 11, CSS_VALUE_FORESTGREEN, CSS_VALUE_GROUP_COLOR},
    {"fuchsia", 7, CSS_VALUE_FUCHSIA, CSS_VALUE_GROUP_COLOR},
    {"gainsboro", 9, CSS_VALUE_GAINSBORO, CSS_VALUE_GROUP_COLOR},
    {"ghostwhite", 10, CSS_VALUE_GHOSTWHITE, CSS_VALUE_GROUP_COLOR},
    {"gold", 4, CSS_VALUE_GOLD, CSS_VALUE_GROUP_COLOR},
    {"goldenrod", 9, CSS_VALUE_GOLDENROD, CSS_VALUE_GROUP_COLOR},
    {"gray", 4, CSS_VALUE_GRAY, CSS_VALUE_GROUP_COLOR},
    {"green", 5, CSS_VALUE_GREEN, CSS_VALUE_GROUP_COLOR},
    {"greenyellow", 11, CSS_VALUE_GREENYELLOW, CSS_VALUE_GROUP_COLOR},
    {"grey", 4, CSS_VALUE_GREY, CSS_VALUE_GROUP_COLOR},
    {"honeydew", 8, CSS_VALUE_HONEYDEW, CSS_VALUE_GROUP_COLOR},
    {"hotpink", 7, CSS_VALUE_HOTPINK, CSS_VALUE_GROUP_COLOR},
    {"indianred", 9, CSS_VALUE_INDIANRED, CSS_VALUE_GROUP_COLOR},
    {"indigo", 6, CSS_VALUE_INDIGO, CSS_VALUE_GROUP_COLOR},
    {"ivory", 5, CSS_VALUE_IVORY, CSS_VALUE_GROUP_COLOR},
    {"khaki", 5, CSS_VALUE_KHAKI, CSS_VALUE_GROUP_COLOR},
    {"lavender", 8, CSS_VALUE_LAVENDER, CSS_VALUE_GROUP_COLOR},
    {"lavenderblush", 13, CSS_VALUE_LAVENDERBLUSH, CSS_VALUE_GROUP_COLOR},
    {"lawngreen", 9, CSS_VALUE_LAWNGREEN, CSS_VALUE_GROUP_COLOR},
    {"lemonchiffon", 12, CSS_VALUE_LEMONCHIFFON, CSS_VALUE_GROUP_COLOR},
    {"lightblue", 9, CSS_VALUE_LIGHTBLUE, CSS_VALUE_GROUP_COLOR},
    {"lightcoral", 10, CSS_VALUE_LIGHTCORAL, CSS_VALUE_GROUP_COLOR},
    {"lightcyan", 9, CSS_VALUE_LIGHTCYAN, CSS_VALUE_GROUP_COLOR},
    {"lightgoldenrodyellow", 20, CSS_VALUE_LIGHTGOLDENRODYELLOW, CSS_VALUE_GROUP_COLOR},
    {"lightgray", 9, CSS_VALUE_LIGHTGRAY, CSS_VALUE_GROUP_COLOR},
    {"lightgreen", 10, CSS_VALUE_LIGHTGREEN, CSS_VALUE_GROUP_COLOR},
    {"lightgrey", 9, CSS_VALUE_LIGHTGREY, CSS_VALUE_GROUP_COLOR},
    {"lightpink", 9, CSS_VALUE_LIGHTPINK, CSS_VALUE_GROUP_COLOR},
    {"lightsalmon", 11, CSS_VALUE_LIGHTSALMON, CSS_VALUE_GROUP_COLOR},
    {"lightseagreen", 13, CSS_VALUE_LIGHTSEAGREEN, CSS_VALUE_GROUP_COLOR},
    {"lightskyblue", 12, CSS_VALUE_LIGHTSKYBLUE, CSS_VALUE_GROUP_COLOR},
    {"lightslategray", 14, CSS_VALUE_LIGHTSLATEGRAY, CSS_VALUE_GROUP_COLOR},
    {"lightslategrey", 14, CSS_VALUE_LIGHTSLATEGREY, CSS_VALUE_GROUP_COLOR},
    {"lightsteelblue", 14, CSS_VALUE_LIGHTSTEELBLUE, CSS_VALUE_GROUP_COLOR},
    {"lightyellow", 11, CSS_VALUE_LIGHTYELLOW, CSS_VALUE_GROUP_COLOR},
    {"lime", 4, CSS_VALUE_LIME, CSS_VALUE_GROUP_COLOR},
    {"limegreen", 9, CSS_VALUE_LIMEGREEN, CSS_VALUE_GROUP_COLOR},
    {"linen", 5, CSS_VALUE_LINEN, CSS_VALUE_GROUP_COLOR},
    {"magenta", 7, CSS_VALUE_MAGENTA, CSS_VALUE_GROUP_COLOR},
    {"maroon", 6, CSS_VALUE_MAROON, CSS_VALUE_GROUP_COLOR},
    {"mediumaquamarine", 16, CSS_VALUE_MEDIUMAQUAMARINE, CSS_VALUE_GROUP_COLOR},
    {"mediumblue", 10, CSS_VALUE_MEDIUMBLUE, CSS_VALUE_GROUP_COLOR},
    {"mediumorchid", 12, CSS_VALUE_MEDIUMORCHID, CSS_VALUE_GROUP_COLOR},
    {"mediumpurple", 12, CSS_VALUE_MEDIUMPURPLE, CSS_VALUE_GROUP_COLOR},
    {"mediumseagreen", 14, CSS_VALUE_MEDIUMSEAGREEN, CSS_VALUE_GROUP_COLOR},
    {"mediumslateblue", 15, CSS_VALUE_MEDIUMSLATEBLUE, CSS_VALUE_GROUP_COLOR},
    {"mediumspringgreen", 17, CSS_VALUE_MEDIUMSPRINGGREEN, CSS_VALUE_GROUP_COLOR},
    {"mediumturquoise", 15, CSS_VALUE_MEDIUMTURQUOISE, CSS_VALUE_GROUP_COLOR},
    {"mediumvioletred", 15, CSS_VALUE_MEDIUMVIOLETRED, CSS_VALUE_GROUP_COLOR},
    {"midnightblue", 12, CSS_VALUE_MIDNIGHTBLUE, CSS_VALUE_GROUP_COLOR},
    {"mintcream", 9, CSS_VALUE_MINTCREAM, CSS_VALUE_GROUP_COLOR},
    {"mistyrose", 9, CSS_VALUE_MISTYROSE, CSS_VALUE_GROUP_COLOR},
    {"moccasin", 8, CSS_VALUE_MOCCASIN, CSS_VALUE_GROUP_COLOR},
    {"navajowhite", 11, CSS_VALUE_NAVAJOWHITE, CSS_VALUE_GROUP_COLOR},
    {"navy", 4, CSS_VALUE_NAVY, CSS_VALUE_GROUP_COLOR},
    {"oldlace", 7, CSS_VALUE_OLDLACE, CSS_VALUE_GROUP_COLOR},
    {"olive", 5, CSS_VALUE_OLIVE, CSS_VALUE_GROUP_COLOR},
    {"olivedrab", 9, CSS_VALUE_OLIVEDRAB, CSS_VALUE_GROUP_COLOR},
    {"orange", 6, CSS_VALUE_ORANGE, CSS_VALUE_GROUP_COLOR},
    {"orangered", 9, CSS_VALUE_ORANGERED, CSS_VALUE_GROUP_COLOR},
    {"orchid", 6, CSS_VALUE_ORCHID, CSS_VALUE_GROUP_COLOR},
    {"palegoldenrod", 13, CSS_VALUE_PALEGOLDENROD, CSS_VALUE_GROUP_COLOR},
    {"palegreen", 9, CSS_VALUE_PALEGREEN, CSS_VALUE_GROUP_COLOR},
    {"paleturquoise", 13, CSS_VALUE_PALETURQUOISE, CSS_VALUE_GROUP_COLOR},
    {"palevioletred", 13, CSS_VALUE_PALEVIOLETRED, CSS_VALUE_GROUP_COLOR},
    {"papayawhip", 10, CSS_VALUE_PAPAYAWHIP, CSS_VALUE_GROUP_COLOR},
    {"peachpuff", 9, CSS_VALUE_PEACHPUFF, CSS_VALUE_GROUP_COLOR},
    {"peru", 4, CSS_VALUE_PERU, CSS_VALUE_GROUP_COLOR},
    {"pink", 4, CSS_VALUE_PINK, CSS_VALUE_GROUP_COLOR},
    {"plum", 4, CSS_VALUE_PLUM, CSS_VALUE_GROUP_COLOR},
    {"powderblue", 10, CSS_VALUE_POWDERBLUE, CSS_VALUE_GROUP_COLOR},
    {"purple", 6, CSS_VALUE_PURPLE, CSS_VALUE_GROUP_COLOR},
    {"rebeccapurple", 13, CSS_VALUE_REBECCAPURPLE, CSS_VALUE_GROUP_COLOR},
    {"red", 3, CSS_VALUE_RED, CSS_VALUE_GROUP_COLOR},
    {"rosybrown", 9, CSS_VALUE_ROSYBROWN, CSS_VALUE_GROUP_COLOR},
    {"royalblue", 9, CSS_VALUE_ROYALBLUE, CSS_VALUE_GROUP_COLOR},
    {"saddlebrown", 11, CSS_VALUE_SADDLEBROWN, CSS_VALUE_GROUP_COLOR},
    {"salmon", 6, CSS_VALUE_SALMON, CSS_VALUE_GROUP_COLOR},
    {"sandybrown", 10, CSS_VALUE_SANDYBROWN, CSS_VALUE_GROUP_COLOR},
    {"seagreen", 8, CSS_VALUE_SEAGREEN, CSS_VALUE_GROUP_COLOR},
    {"seashell", 8, CSS_VALUE_SEASHELL, CSS_VALUE_GROUP_COLOR},
    {"sienna", 6, CSS_VALUE_SIENNA, CSS_VALUE_GROUP_COLOR},
    {"silver", 6, CSS_VALUE_SILVER, CSS_VALUE_GROUP_COLOR},
    {"skyblue", 7, CSS_VALUE_SKYBLUE, CSS_VALUE_GROUP_COLOR},
    {"slateblue", 9, CSS_VALUE_SLATEBLUE, CSS_VALUE_GROUP_COLOR},
    {"slategray", 9, CSS_VALUE_SLATEGRAY, CSS_VALUE_GROUP_COLOR},
    {"slategrey", 9, CSS_VALUE_SLATEGREY, CSS_VALUE_GROUP_COLOR},
    {"snow", 4, CSS_VALUE_SNOW, CSS_VALUE_GROUP_COLOR},
    {"springgreen", 11, CSS_VALUE_SPRINGGREEN, CSS_VALUE_GROUP_COLOR},
    {"steelblue", 9, CSS_VALUE_STEELBLUE, CSS_VALUE_GROUP_COLOR},
    {"tan", 3, CSS_VALUE_TAN, CSS_VALUE_GROUP_COLOR},
    {"teal", 4, CSS_VALUE_TEAL, CSS_VALUE_GROUP_COLOR},
    {"thistle", 7, CSS_VALUE_THISTLE, CSS_VALUE_GROUP_COLOR},
    {"tomato", 6, CSS_VALUE_TOMATO, CSS_VALUE_GROUP_COLOR},
    {"turquoise", 9, CSS_VALUE_TURQUOISE, CSS_VALUE_GROUP_COLOR},
    {"violet", 6, CSS_VALUE_VIOLET, CSS_VALUE_GROUP_COLOR},
    {"wheat", 5, CSS_VALUE_WHEAT, CSS_VALUE_GROUP_COLOR},
    {"white", 5, CSS_VALUE_WHITE, CSS_VALUE_GROUP_COLOR},
    {"whitesmoke", 10, CSS_VALUE_WHITESMOKE, CSS_VALUE_GROUP_COLOR},
    {"yellow", 6, CSS_VALUE_YELLOW, CSS_VALUE_GROUP_COLOR},
    {"yellowgreen", 11, CSS_VALUE_YELLOWGREEN, CSS_VALUE_GROUP_COLOR},
    {"Canvas", 6, CSS_VALUE_CANVAS, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"CanvasText", 10, CSS_VALUE_CANVASTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"LinkText", 8, CSS_VALUE_LINKTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"VisitedText", 11, CSS_VALUE_VISITEDTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"ActiveText", 10, CSS_VALUE_ACTIVETEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"ButtonFace", 10, CSS_VALUE_BUTTONFACE, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"ButtonText", 10, CSS_VALUE_BUTTONTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"ButtonBorder", 12, CSS_VALUE_BUTTONBORDER, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"Field", 5, CSS_VALUE_FIELD, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"FieldText", 9, CSS_VALUE_FIELDTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"Highlight", 9, CSS_VALUE_HIGHLIGHT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"HighlightText", 13, CSS_VALUE_HIGHLIGHTTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"SelectedItem", 12, CSS_VALUE_SELECTEDITEM, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"SelectedItemText", 16, CSS_VALUE_SELECTEDITEMTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"Mark", 4, CSS_VALUE_MARK, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"MarkText", 8, CSS_VALUE_MARKTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"GrayText", 8, CSS_VALUE_GRAYTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"AccentColor", 11, CSS_VALUE_ACCENTCOLOR, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"AccentColorText", 15, CSS_VALUE_ACCENTCOLORTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"rgb", 3, CSS_VALUE_RGB, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"rgba", 4, CSS_VALUE_RGBA, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"hsl", 3, CSS_VALUE_HSL, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"hsla", 4, CSS_VALUE_HSLA, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"hwb", 3, CSS_VALUE_HWB, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"lab", 3, CSS_VALUE_LAB, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"lch", 3, CSS_VALUE_LCH, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"oklab", 5, CSS_VALUE_OKLAB, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"oklch", 5, CSS_VALUE_OKLCH, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"color", 5, CSS_VALUE_COLOR, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"hand", 4, CSS_VALUE_HAND, CSS_VALUE_GROUP_CURSOR},
    {"pointer", 7, CSS_VALUE_POINTER, CSS_VALUE_GROUP_CURSOR},
    {"text", 4, CSS_VALUE_TEXT, CSS_VALUE_GROUP_CURSOR},
    {"wait", 4, CSS_VALUE_WAIT, CSS_VALUE_GROUP_CURSOR},
    {"progress", 8, CSS_VALUE_PROGRESS, CSS_VALUE_GROUP_CURSOR},
    {"grab", 4, CSS_VALUE_GRAB, CSS_VALUE_GROUP_CURSOR},
    {"grabbing", 8, CSS_VALUE_GRABBING, CSS_VALUE_GROUP_CURSOR},
    {"move", 4, CSS_VALUE_MOVE, CSS_VALUE_GROUP_CURSOR},
    {"ltr", 3, CSS_VALUE_LTR, CSS_VALUE_GROUP_DIRECTION},
    {"rtl", 3, CSS_VALUE_RTL, CSS_VALUE_GROUP_DIRECTION},
    {"block", 5, CSS_VALUE_BLOCK, CSS_VALUE_GROUP_DISPLAY_OUTSIDE},
    {"inline", 6, CSS_VALUE_INLINE, CSS_VALUE_GROUP_DISPLAY_OUTSIDE},
    {"run-in", 6, CSS_VALUE_RUN_IN, CSS_VALUE_GROUP_DISPLAY_OUTSIDE},
    {"flow", 4, CSS_VALUE_FLOW, CSS_VALUE_GROUP_DISPLAY_INSIDE},
    {"flow-root", 9, CSS_VALUE_FLOW_ROOT, CSS_VALUE_GROUP_DISPLAY_INSIDE},
    {"table", 5, CSS_VALUE_TABLE, CSS_VALUE_GROUP_DISPLAY_INSIDE},
    {"flex", 4, CSS_VALUE_FLEX, CSS_VALUE_GROUP_DISPLAY_INSIDE},
    {"grid", 4, CSS_VALUE_GRID, CSS_VALUE_GROUP_DISPLAY_INSIDE},
    {"ruby", 4, CSS_VALUE_RUBY, CSS_VALUE_GROUP_DISPLAY_INSIDE},
    {"list-item", 9, CSS_VALUE_LIST_ITEM, CSS_VALUE_GROUP_DISPLAY_LISTITEM},
    {"table-row-group", 15, CSS_VALUE_TABLE_ROW_GROUP, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"table-header-group", 18, CSS_VALUE_TABLE_HEADER_GROUP, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"table-footer-group", 18, CSS_VALUE_TABLE_FOOTER_GROUP, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"table-row", 9, CSS_VALUE_TABLE_ROW, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"table-cell", 10, CSS_VALUE_TABLE_CELL, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"table-column-group", 18, CSS_VALUE_TABLE_COLUMN_GROUP, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"table-column", 12, CSS_VALUE_TABLE_COLUMN, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"table-caption", 13, CSS_VALUE_TABLE_CAPTION, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"ruby-base", 9, CSS_VALUE_RUBY_BASE, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"ruby-text", 9, CSS_VALUE_RUBY_TEXT, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"ruby-base-container", 19, CSS_VALUE_RUBY_BASE_CONTAINER, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"ruby-text-container", 19, CSS_VALUE_RUBY_TEXT_CONTAINER, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"contents", 8, CSS_VALUE_CONTENTS, CSS_VALUE_GROUP_DISPLAY_BOX},
    {"inline-block", 12, CSS_VALUE_INLINE_BLOCK, CSS_VALUE_GROUP_DISPLAY_LEGACY},
    {"inline-table", 12, CSS_VALUE_INLINE_TABLE, CSS_VALUE_GROUP_DISPLAY_LEGACY},
    {"inline-flex", 11, CSS_VALUE_INLINE_FLEX, CSS_VALUE_GROUP_DISPLAY_LEGACY},
    {"inline-grid", 11, CSS_VALUE_INLINE_GRID, CSS_VALUE_GROUP_DISPLAY_LEGACY},
    {"hanging", 7, CSS_VALUE_HANGING, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"content", 7, CSS_VALUE_CONTENT, CSS_VALUE_GROUP_SIZE},
    {"row", 3, CSS_VALUE_ROW, CSS_VALUE_GROUP_FLEX_DIRECTION},
    {"row-reverse", 11, CSS_VALUE_ROW_REVERSE, CSS_VALUE_GROUP_FLEX_DIRECTION},
    {"column", 6, CSS_VALUE_COLUMN, CSS_VALUE_GROUP_FLEX_DIRECTION},
    {"column-reverse", 14, CSS_VALUE_COLUMN_REVERSE, CSS_VALUE_GROUP_FLEX_DIRECTION},
    {"nowrap", 6, CSS_VALUE_NOWRAP, CSS_VALUE_GROUP_FLEX_WRAP},
    {"wrap", 4, CSS_VALUE_WRAP, CSS_VALUE_GROUP_FLEX_WRAP},
    {"wrap-reverse", 12, CSS_VALUE_WRAP_REVERSE, CSS_VALUE_GROUP_FLEX_WRAP},
    {"snap-block", 10, CSS_VALUE_SNAP_BLOCK, CSS_VALUE_GROUP_MISC},
    {"start", 5, CSS_VALUE_START, CSS_VALUE_GROUP_ALIGNMENT},
    {"end", 3, CSS_VALUE_END, CSS_VALUE_GROUP_ALIGNMENT},
    {"near", 4, CSS_VALUE_NEAR, CSS_VALUE_GROUP_MISC},
    {"snap-inline", 11, CSS_VALUE_SNAP_INLINE, CSS_VALUE_GROUP_MISC},
    {"region", 6, CSS_VALUE_REGION, CSS_VALUE_GROUP_MISC},
    {"page", 4, CSS_VALUE_PAGE, CSS_VALUE_GROUP_MISC},
    {"serif", 5, CSS_VALUE_SERIF, CSS_VALUE_GROUP_FONT_FAMILY},
    {"sans-serif", 10, CSS_VALUE_SANS_SERIF, CSS_VALUE_GROUP_FONT_FAMILY},
    {"cursive", 7, CSS_VALUE_CURSIVE, CSS_VALUE_GROUP_FONT_FAMILY},
    {"fantasy", 7, CSS_VALUE_FANTASY, CSS_VALUE_GROUP_FONT_FAMILY},
    {"monospace", 9, CSS_VALUE_MONOSPACE, CSS_VALUE_GROUP_FONT_FAMILY},
    {"system-ui", 9, CSS_VALUE_SYSTEM_UI, CSS_VALUE_GROUP_FONT_FAMILY},
    {"emoji", 5, CSS_VALUE_EMOJI, CSS_VALUE_GROUP_FONT_FAMILY},
    {"math", 4, CSS_VALUE_MATH, CSS_VALUE_GROUP_FONT_FAMILY},
    {"fangsong", 8, CSS_VALUE_FANGSONG, CSS_VALUE_GROUP_FONT_FAMILY},
    {"ui-serif", 8, CSS_VALUE_UI_SERIF, CSS_VALUE_GROUP_FONT_FAMILY},
    {"ui-sans-serif", 13, CSS_VALUE_UI_SANS_SERIF, CSS_VALUE_GROUP_FONT_FAMILY},
    {"ui-monospace", 12, CSS_VALUE_UI_MONOSPACE, CSS_VALUE_GROUP_FONT_FAMILY},
    {"ui-rounded", 10, CSS_VALUE_UI_ROUNDED, CSS_VALUE_GROUP_FONT_FAMILY},
    {"xx-small", 8, CSS_VALUE_XX_SMALL, CSS_VALUE_GROUP_FONT_SIZE},
    {"x-small", 7, CSS_VALUE_X_SMALL, CSS_VALUE_GROUP_FONT_SIZE},
    {"small", 5, CSS_VALUE_SMALL, CSS_VALUE_GROUP_FONT_SIZE},
    {"large", 5, CSS_VALUE_LARGE, CSS_VALUE_GROUP_FONT_SIZE},
    {"x-large", 7, CSS_VALUE_X_LARGE, CSS_VALUE_GROUP_FONT_SIZE},
    {"xx-large", 8, CSS_VALUE_XX_LARGE, CSS_VALUE_GROUP_FONT_SIZE},
    {"xxx-large", 9, CSS_VALUE_XXX_LARGE, CSS_VALUE_GROUP_FONT_SIZE},
    {"larger", 6, CSS_VALUE_LARGER, CSS_VALUE_GROUP_FONT_SIZE},
    {"smaller", 7, CSS_VALUE_SMALLER, CSS_VALUE_GROUP_FONT_SIZE},
    {"normal", 6, CSS_VALUE_NORMAL, CSS_VALUE_GROUP_FONT_STYLE},
    {"ultra-condensed", 15, CSS_VALUE_ULTRA_CONDENSED, CSS_VALUE_GROUP_FONT_STRETCH},
    {"extra-condensed", 15, CSS_VALUE_EXTRA_CONDENSED, CSS_VALUE_GROUP_FONT_STRETCH},
    {"condensed", 9, CSS_VALUE_CONDENSED, CSS_VALUE_GROUP_FONT_STRETCH},
    {"semi-condensed", 14, CSS_VALUE_SEMI_CONDENSED, CSS_VALUE_GROUP_FONT_STRETCH},
    {"semi-expanded", 13, CSS_VALUE_SEMI_EXPANDED, CSS_VALUE_GROUP_FONT_STRETCH},
    {"expanded", 8, CSS_VALUE_EXPANDED, CSS_VALUE_GROUP_FONT_STRETCH},
    {"extra-expanded", 14, CSS_VALUE_EXTRA_EXPANDED, CSS_VALUE_GROUP_FONT_STRETCH},
    {"ultra-expanded", 14, CSS_VALUE_ULTRA_EXPANDED, CSS_VALUE_GROUP_FONT_STRETCH},
    {"italic", 6, CSS_VALUE_ITALIC, CSS_VALUE_GROUP_FONT_STYLE},
    {"oblique", 7, CSS_VALUE_OBLIQUE, CSS_VALUE_GROUP_FONT_STYLE},
    {"bold", 4, CSS_VALUE_BOLD, CSS_VALUE_GROUP_FONT_WEIGHT},
    {"bolder", 6, CSS_VALUE_BOLDER, CSS_VALUE_GROUP_FONT_WEIGHT},
    {"lighter", 7, CSS_VALUE_LIGHTER, CSS_VALUE_GROUP_FONT_WEIGHT},
    {"force-end", 9, CSS_VALUE_FORCE_END, CSS_VALUE_GROUP_MISC},
    {"allow-end", 9, CSS_VALUE_ALLOW_END, CSS_VALUE_GROUP_MISC},
    {"min-content", 11, CSS_VALUE_MIN_CONTENT, CSS_VALUE_GROUP_SIZE},
    {"max-content", 11, CSS_VALUE_MAX_CONTENT, CSS_VALUE_GROUP_SIZE},
    {"manual", 6, CSS_VALUE_MANUAL, CSS_VALUE_GROUP_LINE_BREAK},
    {"loose", 5, CSS_VALUE_LOOSE, CSS_VALUE_GROUP_LINE_BREAK},
    {"strict", 6, CSS_VALUE_STRICT, CSS_VALUE_GROUP_LINE_BREAK},
    {"anywhere", 8, CSS_VALUE_ANYWHERE, CSS_VALUE_GROUP_LINE_BREAK},
    {"visible", 7, CSS_VALUE_VISIBLE, CSS_VALUE_GROUP_OVERFLOW},
    {"clip", 4, CSS_VALUE_CLIP, CSS_VALUE_GROUP_OVERFLOW},
    {"scroll", 6, CSS_VALUE_SCROLL, CSS_VALUE_GROUP_OVERFLOW},
    {"break-word", 10, CSS_VALUE_BREAK_WORD, CSS_VALUE_GROUP_OVERFLOW_WRAP},
    {"static", 6, CSS_VALUE_STATIC, CSS_VALUE_GROUP_POSITION},
    {"relative", 8, CSS_VALUE_RELATIVE, CSS_VALUE_GROUP_POSITION},
    {"absolute", 8, CSS_VALUE_ABSOLUTE, CSS_VALUE_GROUP_POSITION},
    {"sticky", 6, CSS_VALUE_STICKY, CSS_VALUE_GROUP_POSITION},
    {"fixed", 5, CSS_VALUE_FIXED, CSS_VALUE_GROUP_POSITION},
    {"justify", 7, CSS_VALUE_JUSTIFY, CSS_VALUE_GROUP_TEXT_ALIGN},
    {"match-parent", 12, CSS_VALUE_MATCH_PARENT, CSS_VALUE_GROUP_TEXT_ALIGN},
    {"justify-all", 11, CSS_VALUE_JUSTIFY_ALL, CSS_VALUE_GROUP_TEXT_ALIGN},
    {"all", 3, CSS_VALUE_ALL, CSS_VALUE_GROUP_MISC},
    {"digits", 6, CSS_VALUE_DIGITS, CSS_VALUE_GROUP_MISC},
    {"underline", 9, CSS_VALUE_UNDERLINE, CSS_VALUE_GROUP_TEXT_DECO_LINE},
    {"overline", 8, CSS_VALUE_OVERLINE, CSS_VALUE_GROUP_TEXT_DECO_LINE},
    {"line-through", 12, CSS_VALUE_LINE_THROUGH, CSS_VALUE_GROUP_TEXT_DECO_LINE},
    {"blink", 5, CSS_VALUE_BLINK, CSS_VALUE_GROUP_TEXT_DECO_LINE},
    {"wavy", 4, CSS_VALUE_WAVY, CSS_VALUE_GROUP_TEXT_DECO_STYLE},
    {"each-line", 9, CSS_VALUE_EACH_LINE, CSS_VALUE_GROUP_MISC},
    {"inter-word", 10, CSS_VALUE_INTER_WORD, CSS_VALUE_GROUP_TEXT_JUSTIFY},
    {"inter-character", 15, CSS_VALUE_INTER_CHARACTER, CSS_VALUE_GROUP_TEXT_JUSTIFY},
    {"mixed", 5, CSS_VALUE_MIXED, CSS_VALUE_GROUP_TEXT_ORIENTATION},
    {"upright", 7, CSS_VALUE_UPRIGHT, CSS_VALUE_GROUP_TEXT_ORIENTATION},
    {"sideways", 8, CSS_VALUE_SIDEWAYS, CSS_VALUE_GROUP_TEXT_ORIENTATION},
    {"ellipsis", 8, CSS_VALUE_ELLIPSIS, CSS_VALUE_GROUP_TEXT_OVERFLOW},
    {"capitalize", 10, CSS_VALUE_CAPITALIZE, CSS_VALUE_GROUP_TEXT_TRANSFORM},
    {"uppercase", 9, CSS_VALUE_UPPERCASE, CSS_VALUE_GROUP_TEXT_TRANSFORM},
    {"lowercase", 9, CSS_VALUE_LOWERCASE, CSS_VALUE_GROUP_TEXT_TRANSFORM},
    {"full-width", 10, CSS_VALUE_FULL_WIDTH, CSS_VALUE_GROUP_TEXT_TRANSFORM},
    {"full-size-kana", 14, CSS_VALUE_FULL_SIZE_KANA, CSS_VALUE_GROUP_TEXT_TRANSFORM},
    {"embed", 5, CSS_VALUE_EMBED, CSS_VALUE_GROUP_UNICODE_BIDI},
    {"isolate", 7, CSS_VALUE_ISOLATE, CSS_VALUE_GROUP_UNICODE_BIDI},
    {"bidi-override", 13, CSS_VALUE_BIDI_OVERRIDE, CSS_VALUE_GROUP_UNICODE_BIDI},
    {"isolate-override", 16, CSS_VALUE_ISOLATE_OVERRIDE, CSS_VALUE_GROUP_UNICODE_BIDI},
    {"plaintext", 9, CSS_VALUE_PLAINTEXT, CSS_VALUE_GROUP_UNICODE_BIDI},
    {"collapse", 8, CSS_VALUE_COLLAPSE, CSS_VALUE_GROUP_MISC},
    {"pre", 3, CSS_VALUE_PRE, CSS_VALUE_GROUP_WHITE_SPACE},
    {"pre-wrap", 8, CSS_VALUE_PRE_WRAP, CSS_VALUE_GROUP_WHITE_SPACE},
    {"break-spaces", 12, CSS_VALUE_BREAK_SPACES, CSS_VALUE_GROUP_WHITE_SPACE},
    {"pre-line", 8, CSS_VALUE_PRE_LINE, CSS_VALUE_GROUP_WHITE_SPACE},
    {"keep-all", 8, CSS_VALUE_KEEP_ALL, CSS_VALUE_GROUP_WORD_BREAK},
    {"break-all", 9, CSS_VALUE_BREAK_ALL, CSS_VALUE_GROUP_WORD_BREAK},
    {"both", 4, CSS_VALUE_BOTH, CSS_VALUE_GROUP_CLEAR},
    {"minimum", 7, CSS_VALUE_MINIMUM, CSS_VALUE_GROUP_MISC},
    {"maximum", 7, CSS_VALUE_MAXIMUM, CSS_VALUE_GROUP_MISC},
    {"clear", 5, CSS_VALUE_CLEAR, CSS_VALUE_GROUP_CLEAR},
    {"horizontal-tb", 13, CSS_VALUE_HORIZONTAL_TB, CSS_VALUE_GROUP_WRITING_MODE},
    {"vertical-rl", 11, CSS_VALUE_VERTICAL_RL, CSS_VALUE_GROUP_WRITING_MODE},
    {"vertical-lr", 11, CSS_VALUE_VERTICAL_LR, CSS_VALUE_GROUP_WRITING_MODE},
    {"sideways-rl", 11, CSS_VALUE_SIDEWAYS_RL, CSS_VALUE_GROUP_WRITING_MODE},
    {"sideways-lr", 11, CSS_VALUE_SIDEWAYS_LR, CSS_VALUE_GROUP_WRITING_MODE},
    // Custom values added from Lambda CSS resolve
    {"contain", 7, CSS_VALUE_CONTAIN, CSS_VALUE_GROUP_BGROUND_SIZE},
    {"cover", 5, CSS_VALUE_COVER, CSS_VALUE_GROUP_BGROUND_SIZE},
    {"local", 5, CSS_VALUE_LOCAL, CSS_VALUE_GROUP_BGROUND_ATTACHMENT},
    {"padding-box", 11, CSS_VALUE_PADDING_BOX, CSS_VALUE_GROUP_BOX_MODEL},
    {"multiply", 8, CSS_VALUE_MULTIPLY, CSS_VALUE_GROUP_BGROUND_BLEND},
    {"overlay", 7, CSS_VALUE_OVERLAY, CSS_VALUE_GROUP_BGROUND_BLEND},
    {"round", 5, CSS_VALUE_ROUND, CSS_VALUE_GROUP_BGROUND_REPEAT},
    {"space", 5, CSS_VALUE_SPACE, CSS_VALUE_GROUP_BGROUND_REPEAT},
    {"collapse-table", 14, CSS_VALUE_COLLAPSE_TABLE, CSS_VALUE_GROUP_BORDER_COLLAPSE},  // Use different name to avoid conflict with CSS_VALUE_COLLAPSE
    {"separate", 8, CSS_VALUE_SEPARATE, CSS_VALUE_GROUP_BORDER_COLLAPSE},
    {"hide", 4, CSS_VALUE_HIDE, CSS_VALUE_GROUP_EMPTY_CELLS},
    {"show", 4, CSS_VALUE_SHOW, CSS_VALUE_GROUP_EMPTY_CELLS},
    {"fit-content", 11, CSS_VALUE_FIT_CONTENT, CSS_VALUE_GROUP_SIZE},
    {"fr", 2, CSS_VALUE_FR, CSS_VALUE_GROUP_MISC},
    {"dense", 5, CSS_VALUE_DENSE, CSS_VALUE_GROUP_GRID_AUTO_FLOW},
    // List style types
    {"disc", 4, CSS_VALUE_DISC, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"circle", 6, CSS_VALUE_CIRCLE, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"square", 6, CSS_VALUE_SQUARE, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"decimal", 7, CSS_VALUE_DECIMAL, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"lower-roman", 11, CSS_VALUE_LOWER_ROMAN, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"upper-roman", 11, CSS_VALUE_UPPER_ROMAN, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"lower-alpha", 11, CSS_VALUE_LOWER_ALPHA, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"upper-alpha", 11, CSS_VALUE_UPPER_ALPHA, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    // Flex layout
    {"space-evenly", 12, CSS_VALUE_SPACE_EVENLY, CSS_VALUE_GROUP_FLEX_JUSTIFY},
    {"_replaced", 9, CSS_VALUE__REPLACED, CSS_VALUE_GROUP_RADINT},
};

const CssEnumInfo* css_value_by_id(CssEnum id) {
    // Support both standard and custom value IDs
    if (id < CSS_VALUE__LAST_ENTRY) {
        return &css_value_definitions[id];
    }
    // For custom values beyond CSS_VALUE__LAST_ENTRY, calculate index
    if (id >= CSS_VALUE_CONTAIN && id <= CSS_VALUE_DENSE) {
        size_t custom_index = CSS_VALUE__LAST_ENTRY + (id - CSS_VALUE_CONTAIN);
        if (custom_index < sizeof(css_value_definitions) / sizeof(css_value_definitions[0])) {
            return &css_value_definitions[custom_index];
        }
    }
    return css_value_definitions; // return _undef for unknown IDs
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
// Returns the LXB_CSS_VALUE enum, or CSS_VALUE__UNDEF if not found
CssEnum css_value_by_name(const char* name) {
    if (!name) return CSS_VALUE__UNDEF;

    static HashMap* keyword_cache = NULL;
    static const size_t table_size = sizeof(css_value_definitions) / sizeof(css_value_definitions[0]);

    // initialize hashmap on first use
    if (!keyword_cache) {
        keyword_cache = hashmap_new(
            sizeof(const char*),  // key is pointer to string
            table_size,           // initial capacity
            0, 0,                 // seeds (0 means random)
            css_keyword_hash,
            css_keyword_compare,
            NULL,                 // no element free function
            (void*)css_value_definitions  // udata pointing to value table
        );

        // populate hashmap with all keywords
        for (size_t i = 0; i < table_size; i++) {
            const char* keyword = css_value_definitions[i].name;
            hashmap_set(keyword_cache, &keyword);
        }
    }

    // lookup in hashmap
    const char** result = (const char**)hashmap_get(keyword_cache, &name);
    if (result) {
        // find index in table to get the unique value
        const char* found_name = *result;
        for (size_t i = 0; i < table_size; i++) {
            if (css_value_definitions[i].name == found_name) {
                return css_value_definitions[i].unique;
            }
        }
    }

    return CSS_VALUE__UNDEF;
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

View* alloc_view(LayoutContext* lycon, ViewType type, DomNode* node) {
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
                const char* colspan_str = node->get_attribute("colspan");
                if (colspan_str && *colspan_str) {
                    int colspan = atoi(colspan_str);
                    cell->col_span = (colspan > 0) ? colspan : 1;
                } else {
                    cell->col_span = 1;
                }
                // Read rowspan attribute
                const char* rowspan_str = node->get_attribute("rowspan");
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
    prop->overflow_x = prop->overflow_y = CSS_VALUE_VISIBLE;   // initial value
    prop->pane = (ScrollPane*)pool_calloc(lycon->doc->view_tree->pool, sizeof(ScrollPane));
    return prop;
}

BlockProp* alloc_block_prop(LayoutContext* lycon) {
    BlockProp* prop = (BlockProp*)alloc_prop(lycon, sizeof(BlockProp));
    prop->line_height = null;
    prop->text_align = lycon->block.text_align;  // inherit from parent
    prop->given_min_height = prop->given_min_width = prop->given_max_height = prop->given_max_width = -1;  // -1 for undefined
    prop->box_sizing = CSS_VALUE_CONTENT_BOX;  // default to content-box
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
    prop->position = CSS_VALUE_STATIC;  // default position
    prop->top = prop->right = prop->bottom = prop->left = 0;  // default offsets
    prop->z_index = 0;  // default z-index
    prop->has_top = prop->has_right = prop->has_bottom = prop->has_left = false;  // no offsets set
    prop->clear = CSS_VALUE_NONE;  // default clear
    prop->float_prop = CSS_VALUE_NONE;  // default float
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
        grid->justify_content = CSS_VALUE_START;
        grid->align_content = CSS_VALUE_START;
        grid->justify_items = CSS_VALUE_STRETCH;
        grid->align_items = CSS_VALUE_STRETCH;
        grid->grid_auto_flow = CSS_VALUE_ROW;
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

void print_inline_props(ViewSpan* span, StrBuf* buf, int indent) {
    if (span->in_line) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{");
        if (span->in_line->cursor) {
            char* cursor;
            switch (span->in_line->cursor) {
            case CSS_VALUE_POINTER:
                cursor = "pointer";  break;
            case CSS_VALUE_TEXT:
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

        // Font weight is a numeric value (400, 700, etc.)
        char weight_buf[16];
        snprintf(weight_buf, sizeof(weight_buf), "%d", span->font->font_weight);

        strbuf_append_format(buf, "{font:{family:'%s', size:%d, style:%s, weight:%s, decoration:%s}}\n",
            span->font->family, span->font->font_size, css_value_by_id(span->font->font_style)->name,
            weight_buf, css_value_by_id(span->font->text_deco)->name);
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
        if (block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
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

        // justify-content (handle custom value CSS_VALUE_SPACE_EVENLY = 0x0199)
        const char* justify_str = "flex-start";
        if (block->embed->flex->justify == CSS_VALUE_SPACE_EVENLY) {
            justify_str = "space-evenly";
        } else {
            const CssEnumInfo* justify_value = css_value_by_id((CssEnum)block->embed->flex->justify);
            if (justify_value && justify_value->name) {
                justify_str = (const char*)justify_value->name;
            }
        }
        strbuf_append_format(buf, "justify:%s ", justify_str);

        // align-items (handle custom value for space-evenly)
        const char* align_items_str = "stretch";
        if (block->embed->flex->align_items == CSS_VALUE_SPACE_EVENLY) {
            align_items_str = "space-evenly";
        } else {
            const CssEnumInfo* align_items_value = css_value_by_id((CssEnum)block->embed->flex->align_items);
            if (align_items_value && align_items_value->name) {
                align_items_str = (const char*)align_items_value->name;
            }
        }
        strbuf_append_format(buf, "align-items:%s ", align_items_str);

        // align-content (handle custom value for space-evenly)
        const char* align_content_str = "stretch";
        if (block->embed->flex->align_content == CSS_VALUE_SPACE_EVENLY) {
            align_content_str = "space-evenly";
        } else {
            const CssEnumInfo* align_content_value = css_value_by_id((CssEnum)block->embed->flex->align_content);
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
            const CssEnumInfo* overflow_x_value = css_value_by_id(block->scroller->overflow_x);
            if (overflow_x_value && overflow_x_value->name) {
                strbuf_append_format(buf, "overflow-x:%s ", overflow_x_value->name);
            }
        }
        if (block->scroller->overflow_y) {
            const CssEnumInfo* overflow_y_value = css_value_by_id(block->scroller->overflow_y);
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
            const CssEnumInfo* pos_value = css_value_by_id(block->position->position);
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

void print_block(ViewBlock* block, StrBuf* buf, int indent) {
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_format(buf, "[view-%s:%s, x:%.1f, y:%.1f, wd:%.1f, hg:%.1f\n",
        block->name(), block->node->name(),
        (float)block->x, (float)block->y, (float)block->width, (float)block->height);
    print_block_props(block, buf, indent + 2);
    print_inline_props((ViewSpan*)block, buf, indent+2);
    print_view_group((ViewGroup*)block, buf, indent+2);
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "]\n");
}

void print_view_group(ViewGroup* view_group, StrBuf* buf, int indent) {
    View* view = view_group->child;
    if (view) {
        do {
            if (view->is_block()) {
                print_block((ViewBlock*)view, buf, indent);
            }
            else if (view->type == RDT_VIEW_INLINE) {
                strbuf_append_char_n(buf, ' ', indent);
                ViewSpan* span = (ViewSpan*)view;
                strbuf_append_format(buf, "[view-inline:%s, x:%.1f, y:%.1f, wd:%.1f, hg:%.1f\n",
                    span->node->name(), (float)span->x, (float)span->y, (float)span->width, (float)span->height);
                print_inline_props(span, buf, indent + 2);
                print_view_group((ViewGroup*)view, buf, indent + 2);
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

void print_view_tree(ViewGroup* view_root, Url* url, float pixel_ratio) {
    StrBuf* buf = strbuf_new_cap(1024);
    print_block((ViewBlock*)view_root, buf, 0);
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
        DomNode* parent = block->node->parent;
        if (parent) {
            // Count siblings with same tag name
            int sibling_count = 0;
            int current_index = 0;
            DomNode* sibling = parent->first_child;

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
            block->blk->box_sizing == CSS_VALUE_BORDER_BOX ? "border-box" : "content-box");

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

        // justify-content (handle custom value CSS_VALUE_SPACE_EVENLY = 0x0199)
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* justify_str = "flex-start";
        if (block->embed->flex->justify == CSS_VALUE_SPACE_EVENLY) {
            justify_str = "space-evenly";
        } else {
            const CssEnumInfo* justify_value = css_value_by_id((CssEnum)block->embed->flex->justify);
            if (justify_value && justify_value->name) {
                justify_str = (const char*)justify_value->name;
            }
        }
        strbuf_append_format(buf, "\"justify\": \"%s\",\n", justify_str);

        // align-items (handle custom value for space-evenly)
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* align_items_str = "stretch";
        if (block->embed->flex->align_items == CSS_VALUE_SPACE_EVENLY) {
            align_items_str = "space-evenly";
        } else {
            const CssEnumInfo* align_items_value = css_value_by_id((CssEnum)block->embed->flex->align_items);
            if (align_items_value && align_items_value->name) {
                align_items_str = (const char*)align_items_value->name;
            }
        }
        strbuf_append_format(buf, "\"align_items\": \"%s\",\n", align_items_str);

        // align-content (handle custom value for space-evenly)
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* align_content_str = "stretch";
        if (block->embed->flex->align_content == CSS_VALUE_SPACE_EVENLY) {
            align_content_str = "space-evenly";
        } else {
            const CssEnumInfo* align_content_value = css_value_by_id((CssEnum)block->embed->flex->align_content);
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
        DomNode* parent = span->node->parent;
        if (parent) {
            // Count siblings with same tag name
            int sibling_count = 0;
            int current_index = 0;
            DomNode* sibling = parent->first_child;

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
                case CSS_VALUE_POINTER: cursor = "pointer"; break;
                case CSS_VALUE_TEXT: cursor = "text"; break;
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
