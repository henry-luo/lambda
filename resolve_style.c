#include "layout.h"
#define SV_IMPLEMENTATION
#include "./lib/sv.h"

Uint32 color_name_to_rgb(PropValue color_name) {
    switch (color_name) {
        case LXB_CSS_VALUE_ALICEBLUE: return 0xF0F8FF;
        case LXB_CSS_VALUE_ANTIQUEWHITE: return 0xFAEBD7;
        case LXB_CSS_VALUE_AQUA: return 0x00FFFF;
        case LXB_CSS_VALUE_AQUAMARINE: return 0x7FFFD4;
        case LXB_CSS_VALUE_AZURE: return 0xF0FFFF;
        case LXB_CSS_VALUE_BEIGE: return 0xF5F5DC;
        case LXB_CSS_VALUE_BISQUE: return 0xFFE4C4;
        case LXB_CSS_VALUE_BLACK: return 0x000000;
        case LXB_CSS_VALUE_BLANCHEDALMOND: return 0xFFEBCD;
        case LXB_CSS_VALUE_BLUE: return 0x0000FF;
        case LXB_CSS_VALUE_BLUEVIOLET: return 0x8A2BE2;
        case LXB_CSS_VALUE_BROWN: return 0xA52A2A;
        case LXB_CSS_VALUE_BURLYWOOD: return 0xDEB887;
        case LXB_CSS_VALUE_CADETBLUE: return 0x5F9EA0;
        case LXB_CSS_VALUE_CHARTREUSE: return 0x7FFF00;
        case LXB_CSS_VALUE_CHOCOLATE: return 0xD2691E;
        case LXB_CSS_VALUE_CORAL: return 0xFF7F50;
        case LXB_CSS_VALUE_CORNFLOWERBLUE: return 0x6495ED;
        case LXB_CSS_VALUE_CORNSILK: return 0xFFF8DC;
        case LXB_CSS_VALUE_CRIMSON: return 0xDC143C;
        case LXB_CSS_VALUE_CYAN: return 0x00FFFF;
        case LXB_CSS_VALUE_DARKBLUE: return 0x00008B;
        case LXB_CSS_VALUE_DARKCYAN: return 0x008B8B;
        case LXB_CSS_VALUE_DARKGOLDENROD: return 0xB8860B;
        case LXB_CSS_VALUE_DARKGRAY: return 0xA9A9A9;
        case LXB_CSS_VALUE_DARKGREEN: return 0x006400;
        case LXB_CSS_VALUE_DARKGREY: return 0xA9A9A9;
        case LXB_CSS_VALUE_DARKKHAKI: return 0xBDB76B;
        case LXB_CSS_VALUE_DARKMAGENTA: return 0x8B008B;
        case LXB_CSS_VALUE_DARKOLIVEGREEN: return 0x556B2F;
        case LXB_CSS_VALUE_DARKORANGE: return 0xFF8C00;
        case LXB_CSS_VALUE_DARKORCHID: return 0x9932CC;
        case LXB_CSS_VALUE_DARKRED: return 0x8B0000;
        case LXB_CSS_VALUE_DARKSALMON: return 0xE9967A;
        case LXB_CSS_VALUE_DARKSEAGREEN: return 0x8FBC8F;
        case LXB_CSS_VALUE_DARKSLATEBLUE: return 0x483D8B;
        case LXB_CSS_VALUE_DARKSLATEGRAY: return 0x2F4F4F;
        case LXB_CSS_VALUE_DARKSLATEGREY: return 0x2F4F4F;
        case LXB_CSS_VALUE_DARKTURQUOISE: return 0x00CED1;
        case LXB_CSS_VALUE_DARKVIOLET: return 0x9400D3;
        case LXB_CSS_VALUE_DEEPPINK: return 0xFF1493;
        case LXB_CSS_VALUE_DEEPSKYBLUE: return 0x00BFFF;
        case LXB_CSS_VALUE_DIMGRAY: return 0x696969;
        case LXB_CSS_VALUE_DIMGREY: return 0x696969;
        case LXB_CSS_VALUE_DODGERBLUE: return 0x1E90FF;
        case LXB_CSS_VALUE_FIREBRICK: return 0xB22222;
        case LXB_CSS_VALUE_FLORALWHITE: return 0xFFFAF0;
        case LXB_CSS_VALUE_FORESTGREEN: return 0x228B22;
        case LXB_CSS_VALUE_FUCHSIA: return 0xFF00FF;
        case LXB_CSS_VALUE_GAINSBORO: return 0xDCDCDC;
        case LXB_CSS_VALUE_GHOSTWHITE: return 0xF8F8FF;
        case LXB_CSS_VALUE_GOLD: return 0xFFD700;
        case LXB_CSS_VALUE_GOLDENROD: return 0xDAA520;
        case LXB_CSS_VALUE_GRAY: return 0x808080;
        case LXB_CSS_VALUE_GREEN: return 0x008000;
        case LXB_CSS_VALUE_GREENYELLOW: return 0xADFF2F;
        case LXB_CSS_VALUE_GREY: return 0x808080;
        case LXB_CSS_VALUE_HONEYDEW: return 0xF0FFF0;
        case LXB_CSS_VALUE_HOTPINK: return 0xFF69B4;
        case LXB_CSS_VALUE_INDIANRED: return 0xCD5C5C;
        case LXB_CSS_VALUE_INDIGO: return 0x4B0082;
        case LXB_CSS_VALUE_IVORY: return 0xFFFFF0;
        case LXB_CSS_VALUE_KHAKI: return 0xF0E68C;
        case LXB_CSS_VALUE_LAVENDER: return 0xE6E6FA;
        case LXB_CSS_VALUE_LAVENDERBLUSH: return 0xFFF0F5;
        case LXB_CSS_VALUE_LAWNGREEN: return 0x7CFC00;
        case LXB_CSS_VALUE_LEMONCHIFFON: return 0xFFFACD;
        case LXB_CSS_VALUE_LIGHTBLUE: return 0xADD8E6;
        case LXB_CSS_VALUE_LIGHTCORAL: return 0xF08080;
        case LXB_CSS_VALUE_LIGHTCYAN: return 0xE0FFFF;
        case LXB_CSS_VALUE_LIGHTGOLDENRODYELLOW: return 0xFAFAD2;
        case LXB_CSS_VALUE_LIGHTGRAY: return 0xD3D3D3;
        case LXB_CSS_VALUE_LIGHTGREEN: return 0x90EE90;
        case LXB_CSS_VALUE_LIGHTGREY: return 0xD3D3D3;
        case LXB_CSS_VALUE_LIGHTPINK: return 0xFFB6C1;
        case LXB_CSS_VALUE_LIGHTSALMON: return 0xFFA07A;
        case LXB_CSS_VALUE_LIGHTSEAGREEN: return 0x20B2AA;
        case LXB_CSS_VALUE_LIGHTSKYBLUE: return 0x87CEFA;
        case LXB_CSS_VALUE_LIGHTSLATEGRAY: return 0x778899;
        case LXB_CSS_VALUE_LIGHTSLATEGREY: return 0x778899;
        case LXB_CSS_VALUE_LIGHTSTEELBLUE: return 0xB0C4DE;
        case LXB_CSS_VALUE_LIGHTYELLOW: return 0xFFFFE0;
        case LXB_CSS_VALUE_LIME: return 0x00FF00;
        case LXB_CSS_VALUE_LIMEGREEN: return 0x32CD32;
        case LXB_CSS_VALUE_LINEN: return 0xFAF0E6;
        case LXB_CSS_VALUE_MAGENTA: return 0xFF00FF;
        case LXB_CSS_VALUE_MAROON: return 0x800000;
        case LXB_CSS_VALUE_MEDIUMAQUAMARINE: return 0x66CDAA;
        case LXB_CSS_VALUE_MEDIUMBLUE: return 0x0000CD;
        case LXB_CSS_VALUE_MEDIUMORCHID: return 0xBA55D3;
        case LXB_CSS_VALUE_MEDIUMPURPLE: return 0x9370DB;
        case LXB_CSS_VALUE_MEDIUMSEAGREEN: return 0x3CB371;
        case LXB_CSS_VALUE_MEDIUMSLATEBLUE: return 0x7B68EE;
        case LXB_CSS_VALUE_MEDIUMSPRINGGREEN: return 0x00FA9A;
        case LXB_CSS_VALUE_MEDIUMTURQUOISE: return 0x48D1CC;
        case LXB_CSS_VALUE_MEDIUMVIOLETRED: return 0xC71585;
        case LXB_CSS_VALUE_MIDNIGHTBLUE: return 0x191970;
        case LXB_CSS_VALUE_MINTCREAM: return 0xF5FFFA;
        case LXB_CSS_VALUE_MISTYROSE: return 0xFFE4E1;
        case LXB_CSS_VALUE_MOCCASIN: return 0xFFE4B5;
        case LXB_CSS_VALUE_NAVAJOWHITE: return 0xFFDEAD;
        case LXB_CSS_VALUE_NAVY: return 0x000080;
        case LXB_CSS_VALUE_OLDLACE: return 0xFDF5E6;
        case LXB_CSS_VALUE_OLIVE: return 0x808000;
        case LXB_CSS_VALUE_OLIVEDRAB: return 0x6B8E23;
        case LXB_CSS_VALUE_ORANGE: return 0xFFA500;
        case LXB_CSS_VALUE_ORANGERED: return 0xFF4500;
        case LXB_CSS_VALUE_ORCHID: return 0xDA70D6;
        case LXB_CSS_VALUE_PALEGOLDENROD: return 0xEEE8AA;
        case LXB_CSS_VALUE_PALEGREEN: return 0x98FB98;
        case LXB_CSS_VALUE_PALETURQUOISE: return 0xAFEEEE;
        case LXB_CSS_VALUE_PALEVIOLETRED: return 0xDB7093;
        case LXB_CSS_VALUE_PAPAYAWHIP: return 0xFFEFD5;
        case LXB_CSS_VALUE_PEACHPUFF: return 0xFFDAB9;
        case LXB_CSS_VALUE_PERU: return 0xCD853F;
        case LXB_CSS_VALUE_PINK: return 0xFFC1CC;
        case LXB_CSS_VALUE_PLUM: return 0xDDA0DD;
        case LXB_CSS_VALUE_POWDERBLUE: return 0xB0E0E6;
        case LXB_CSS_VALUE_PURPLE: return 0x800080;
        case LXB_CSS_VALUE_REBECCAPURPLE: return 0x663399;
        case LXB_CSS_VALUE_RED: return 0xFF0000;
        case LXB_CSS_VALUE_ROSYBROWN: return 0xBC8F8F;
        case LXB_CSS_VALUE_ROYALBLUE: return 0x4169E1;
        case LXB_CSS_VALUE_SADDLEBROWN: return 0x8B4513;
        case LXB_CSS_VALUE_SALMON: return 0xFA8072;
        case LXB_CSS_VALUE_SANDYBROWN: return 0xF4A460;
        case LXB_CSS_VALUE_SEAGREEN: return 0x2E8B57;
        case LXB_CSS_VALUE_SEASHELL: return 0xFFF5EE;
        case LXB_CSS_VALUE_SIENNA: return 0xA0522D;
        case LXB_CSS_VALUE_SILVER: return 0xC0C0C0;
        case LXB_CSS_VALUE_SKYBLUE: return 0x87CEEB;
        case LXB_CSS_VALUE_SLATEBLUE: return 0x6A5ACD;
        case LXB_CSS_VALUE_SLATEGRAY: return 0x708090;
        case LXB_CSS_VALUE_SLATEGREY: return 0x708090;
        case LXB_CSS_VALUE_SNOW: return 0xFFFAFA;
        case LXB_CSS_VALUE_SPRINGGREEN: return 0x00FF7F;
        case LXB_CSS_VALUE_STEELBLUE: return 0x4682B4;
        case LXB_CSS_VALUE_TAN: return 0xD2B48C;
        case LXB_CSS_VALUE_TEAL: return 0x008080;
        case LXB_CSS_VALUE_THISTLE: return 0xD8BFD8;
        case LXB_CSS_VALUE_TOMATO: return 0xFF6347;
        case LXB_CSS_VALUE_TURQUOISE: return 0x40E0D0;
        case LXB_CSS_VALUE_VIOLET: return 0xEE82EE;
        case LXB_CSS_VALUE_WHEAT: return 0xF5DEB3;
        case LXB_CSS_VALUE_WHITE: return 0xFFFFFF;
        case LXB_CSS_VALUE_WHITESMOKE: return 0xF5F5F5;
        case LXB_CSS_VALUE_YELLOW: return 0xFFFF00;
        case LXB_CSS_VALUE_YELLOWGREEN: return 0x9ACD32;
        default: return 0x000000; // Default to black
    }
}

PropValue element_display(lxb_html_element_t* elmt) {
    PropValue outer_display, inner_display;
    // determine element 'display'
    int name = elmt->element.node.local_name;  // todo: should check ns as well 
    switch (name) { 
        case LXB_TAG_H1: case LXB_TAG_H2: case LXB_TAG_H3: case LXB_TAG_H4: case LXB_TAG_H5: case LXB_TAG_H6:
        case LXB_TAG_P: case LXB_TAG_DIV: case LXB_TAG_CENTER: case LXB_TAG_UL: case LXB_TAG_OL:
            outer_display = LXB_CSS_VALUE_BLOCK;  inner_display = LXB_CSS_VALUE_FLOW;
            break;
        default:  // case LXB_TAG_B: case LXB_TAG_I: case LXB_TAG_U: case LXB_TAG_S: case LXB_TAG_FONT:
            outer_display = LXB_CSS_VALUE_INLINE;  inner_display = LXB_CSS_VALUE_FLOW;
    }
    // get CSS display if specified
    if (elmt->element.style != NULL) {
        const lxb_css_rule_declaration_t* display_decl = 
            lxb_dom_element_style_by_id((lxb_dom_element_t*)elmt, LXB_CSS_PROPERTY_DISPLAY);
        if (display_decl) {
            // printf("display: %s, %s\n", lxb_css_value_by_id(display_decl->u.display->a)->name, 
            //     lxb_css_value_by_id(display_decl->u.display->b)->name);
            outer_display = display_decl->u.display->a;
            inner_display = display_decl->u.display->b;
        }
    }
    return outer_display;
}

lxb_status_t style_print_callback(const lxb_char_t *data, size_t len, void *ctx) {
    printf("style rule: %.*s\n", (int) len, (const char *) data);
    return LXB_STATUS_OK;
}

lxb_status_t lxb_html_element_style_print(lexbor_avl_t *avl, lexbor_avl_node_t **root,
    lexbor_avl_node_t *node, void *ctx) {
    lxb_css_rule_declaration_t *declr = (lxb_css_rule_declaration_t *) node->value;
    printf("style entry: %ld\n", declr->type);
    lxb_css_rule_declaration_serialize(declr, style_print_callback, NULL);
    return LXB_STATUS_OK;
}

lxb_status_t lxb_html_element_style_resolve(lexbor_avl_t *avl, lexbor_avl_node_t **root,
    lexbor_avl_node_t *node, void *ctx) {
    LayoutContext* lycon = (LayoutContext*) ctx;
    lxb_css_rule_declaration_t *declr = (lxb_css_rule_declaration_t *) node->value;
    const lxb_css_entry_data_t *data = lxb_css_property_by_id(declr->type);
    if (!data) { return LXB_STATUS_ERROR_NOT_EXISTS; }
    printf("style entry: %ld %s\n", declr->type, data->name);
    ViewSpan* span = (ViewSpan*)lycon->view;

    switch (declr->type) {
    case LXB_CSS_PROPERTY_LINE_HEIGHT: 
        lxb_css_property_line_height_t* line_height = declr->u.line_height;
        switch (line_height->type) {
        case LXB_CSS_VALUE__NUMBER: 
            lycon->block.line_height = line_height->u.number.num * lycon->font.style.font_size;
            printf("property number: %lf\n", line_height->u.number.num);
            break;
        case LXB_CSS_VALUE__LENGTH:      
            lycon->block.line_height = line_height->u.length.num;
            printf("property unit: %d\n", line_height->u.length.unit);
            break;
        case LXB_CSS_VALUE__PERCENTAGE:
            lycon->block.line_height = line_height->u.percentage.num * lycon->font.style.font_size;
            printf("property percentage: %lf\n", line_height->u.percentage.num);
            break;
        }
        break;
    case LXB_CSS_PROPERTY_VERTICAL_ALIGN:
        lxb_css_property_vertical_align_t* vertical_align = declr->u.vertical_align;
        lycon->line.vertical_align = vertical_align->alignment.type ? 
            vertical_align->alignment.type : vertical_align->shift.type;
        printf("vertical align: %d, %d, %d, %d, %d\n", 
            vertical_align->type, vertical_align->alignment.type, vertical_align->shift.type, 
            LXB_CSS_VALUE_MIDDLE, LXB_CSS_VALUE_BOTTOM);
        break;
    case LXB_CSS_PROPERTY_CURSOR:
        const lxb_css_property_cursor_t *cursor = declr->u.cursor;
        printf("cursor property: %d\n", cursor->type);
        if (!span->in_line) {
            span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
        }
        span->in_line->cursor = cursor->type;
        break;
    case LXB_CSS_PROPERTY_COLOR:
        const lxb_css_property_color_t *color = declr->u.color;
        printf("color property: %d, red: %d\n", color->type, LXB_CSS_VALUE_RED);
        if (!span->in_line) {
            span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
        }
        span->in_line->color.c = color_name_to_rgb(color->type);
        break;
    case LXB_CSS_PROPERTY__CUSTOM: // properties not supported by Lexbor, return as #custom
        const lxb_css_property__custom_t *custom = declr->u.custom;
        // String_View custom_name = sv_from_parts((char*)custom->name.data, custom->name.length);
        // if (sv_eq(custom_name, sv_from_cstr("cursor"))) {
        //     ViewSpan* span = (ViewSpan*)lycon->view;
        //     if (!span->in_line) {
        //         span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
        //     }
        //     String_View custom_value = sv_from_parts((char*)custom->value.data, custom->value.length);
        //     if (sv_eq(custom_value, sv_from_cstr("pointer"))) {
        //         printf("got cursor: pointer\n");
        //         span->in_line->cursor = LXB_CSS_VALUE_POINTER;
        //     }
        // }
        printf("custom property: %.*s\n", (int)custom->name.length, custom->name.data);
        break;
    default:
        printf("unhandled property: %s\n", data->name);
    }
    return LXB_STATUS_OK;
}