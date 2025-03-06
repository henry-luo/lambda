#include "layout.h"
#define SV_IMPLEMENTATION
#include "./lib/sv.h"

// CSS4 has a total of 148 colors
Color color_name_to_rgb(PropValue color_name) {
    uint32_t c;
    switch (color_name) {
        case LXB_CSS_VALUE_ALICEBLUE: c = 0xF0F8FF;  break;
        case LXB_CSS_VALUE_ANTIQUEWHITE: c = 0xFAEBD7;  break;
        case LXB_CSS_VALUE_AQUA: c = 0x00FFFF;  break;
        case LXB_CSS_VALUE_AQUAMARINE: c = 0x7FFFD4;  break;
        case LXB_CSS_VALUE_AZURE: c = 0xF0FFFF;  break;
        case LXB_CSS_VALUE_BEIGE: c = 0xF5F5DC;  break;
        case LXB_CSS_VALUE_BISQUE: c = 0xFFE4C4;  break;
        case LXB_CSS_VALUE_BLACK: c = 0x000000;  break;
        case LXB_CSS_VALUE_BLANCHEDALMOND: c = 0xFFEBCD;  break;
        case LXB_CSS_VALUE_BLUE: c = 0x0000FF;  break;
        case LXB_CSS_VALUE_BLUEVIOLET: c = 0x8A2BE2;  break;
        case LXB_CSS_VALUE_BROWN: c = 0xA52A2A;  break;
        case LXB_CSS_VALUE_BURLYWOOD: c = 0xDEB887;  break;
        case LXB_CSS_VALUE_CADETBLUE: c = 0x5F9EA0;  break;
        case LXB_CSS_VALUE_CHARTREUSE: c = 0x7FFF00;  break;
        case LXB_CSS_VALUE_CHOCOLATE: c = 0xD2691E;  break;
        case LXB_CSS_VALUE_CORAL: c = 0xFF7F50;  break;
        case LXB_CSS_VALUE_CORNFLOWERBLUE: c = 0x6495ED;  break;
        case LXB_CSS_VALUE_CORNSILK: c = 0xFFF8DC;  break;
        case LXB_CSS_VALUE_CRIMSON: c = 0xDC143C;  break;
        case LXB_CSS_VALUE_CYAN: c = 0x00FFFF;  break;
        case LXB_CSS_VALUE_DARKBLUE: c = 0x00008B;  break;
        case LXB_CSS_VALUE_DARKCYAN: c = 0x008B8B;  break;
        case LXB_CSS_VALUE_DARKGOLDENROD: c = 0xB8860B;  break;
        case LXB_CSS_VALUE_DARKGRAY: c = 0xA9A9A9;  break;
        case LXB_CSS_VALUE_DARKGREEN: c = 0x006400;  break;
        case LXB_CSS_VALUE_DARKGREY: c = 0xA9A9A9;  break;
        case LXB_CSS_VALUE_DARKKHAKI: c = 0xBDB76B;  break;
        case LXB_CSS_VALUE_DARKMAGENTA: c = 0x8B008B;  break;
        case LXB_CSS_VALUE_DARKOLIVEGREEN: c = 0x556B2F;  break;
        case LXB_CSS_VALUE_DARKORANGE: c = 0xFF8C00;  break;
        case LXB_CSS_VALUE_DARKORCHID: c = 0x9932CC;  break;
        case LXB_CSS_VALUE_DARKRED: c = 0x8B0000;  break;   
        case LXB_CSS_VALUE_DARKSALMON: c = 0xE9967A;  break;
        case LXB_CSS_VALUE_DARKSEAGREEN: c = 0x8FBC8F;  break;
        case LXB_CSS_VALUE_DARKSLATEBLUE: c = 0x483D8B;  break;
        case LXB_CSS_VALUE_DARKSLATEGRAY: c = 0x2F4F4F;  break;
        case LXB_CSS_VALUE_DARKSLATEGREY: c = 0x2F4F4F;  break;
        case LXB_CSS_VALUE_DARKTURQUOISE: c = 0x00CED1;  break;
        case LXB_CSS_VALUE_DARKVIOLET: c = 0x9400D3;  break;
        case LXB_CSS_VALUE_DEEPPINK: c = 0xFF1493;  break;
        case LXB_CSS_VALUE_DEEPSKYBLUE: c = 0x00BFFF;  break;
        case LXB_CSS_VALUE_DIMGRAY: c = 0x696969;  break;
        case LXB_CSS_VALUE_DIMGREY: c = 0x696969;  break;
        case LXB_CSS_VALUE_DODGERBLUE: c = 0x1E90FF;  break;
        case LXB_CSS_VALUE_FIREBRICK: c = 0xB22222;  break;
        case LXB_CSS_VALUE_FLORALWHITE: c = 0xFFFAF0;  break;
        case LXB_CSS_VALUE_FORESTGREEN: c = 0x228B22;  break;
        case LXB_CSS_VALUE_FUCHSIA: c = 0xFF00FF;  break;
        case LXB_CSS_VALUE_GAINSBORO: c = 0xDCDCDC;  break;
        case LXB_CSS_VALUE_GHOSTWHITE: c = 0xF8F8FF;  break;
        case LXB_CSS_VALUE_GOLD: c = 0xFFD700;  break;  
        case LXB_CSS_VALUE_GOLDENROD: c = 0xDAA520;  break;
        case LXB_CSS_VALUE_GRAY: c = 0x808080;  break;
        case LXB_CSS_VALUE_GREEN: c = 0x008000;  break;
        case LXB_CSS_VALUE_GREENYELLOW: c = 0xADFF2F;  break;
        case LXB_CSS_VALUE_GREY: c = 0x808080;  break;
        case LXB_CSS_VALUE_HONEYDEW: c = 0xF0FFF0;  break;
        case LXB_CSS_VALUE_HOTPINK: c = 0xFF69B4;  break;
        case LXB_CSS_VALUE_INDIANRED: c = 0xCD5C5C;  break;
        case LXB_CSS_VALUE_INDIGO: c = 0x4B0082;  break;
        case LXB_CSS_VALUE_IVORY: c = 0xFFFFF0;  break;
        case LXB_CSS_VALUE_KHAKI: c = 0xF0E68C;  break;
        case LXB_CSS_VALUE_LAVENDER: c = 0xE6E6FA;  break;
        case LXB_CSS_VALUE_LAVENDERBLUSH: c = 0xFFF0F5;  break;
        case LXB_CSS_VALUE_LAWNGREEN: c = 0x7CFC00;  break;
        case LXB_CSS_VALUE_LEMONCHIFFON: c = 0xFFFACD;  break;
        case LXB_CSS_VALUE_LIGHTBLUE: c = 0xADD8E6;  break;
        case LXB_CSS_VALUE_LIGHTCORAL: c = 0xF08080;  break;
        case LXB_CSS_VALUE_LIGHTCYAN: c = 0xE0FFFF;  break;
        case LXB_CSS_VALUE_LIGHTGOLDENRODYELLOW: c = 0xFAFAD2;  break;
        case LXB_CSS_VALUE_LIGHTGRAY: c = 0xD3D3D3;  break;
        case LXB_CSS_VALUE_LIGHTGREEN: c = 0x90EE90;  break;
        case LXB_CSS_VALUE_LIGHTGREY: c = 0xD3D3D3;  break;
        case LXB_CSS_VALUE_LIGHTPINK: c = 0xFFB6C1;  break;
        case LXB_CSS_VALUE_LIGHTSALMON: c = 0xFFA07A;  break;
        case LXB_CSS_VALUE_LIGHTSEAGREEN: c = 0x20B2AA;  break;
        case LXB_CSS_VALUE_LIGHTSKYBLUE: c = 0x87CEFA;  break;
        case LXB_CSS_VALUE_LIGHTSLATEGRAY: c = 0x778899;  break;
        case LXB_CSS_VALUE_LIGHTSLATEGREY: c = 0x778899;  break;
        case LXB_CSS_VALUE_LIGHTSTEELBLUE: c = 0xB0C4DE;  break;
        case LXB_CSS_VALUE_LIGHTYELLOW: c = 0xFFFFE0;  break;
        case LXB_CSS_VALUE_LIME: c = 0x00FF00;  break;
        case LXB_CSS_VALUE_LIMEGREEN: c = 0x32CD32;  break;
        case LXB_CSS_VALUE_LINEN: c = 0xFAF0E6;  break;
        case LXB_CSS_VALUE_MAGENTA: c = 0xFF00FF;  break;
        case LXB_CSS_VALUE_MAROON: c = 0x800000;  break;
        case LXB_CSS_VALUE_MEDIUMAQUAMARINE: c = 0x66CDAA;  break;
        case LXB_CSS_VALUE_MEDIUMBLUE: c = 0x0000CD;  break;
        case LXB_CSS_VALUE_MEDIUMORCHID: c = 0xBA55D3;  break;
        case LXB_CSS_VALUE_MEDIUMPURPLE: c = 0x9370DB;  break;
        case LXB_CSS_VALUE_MEDIUMSEAGREEN: c = 0x3CB371;  break;
        case LXB_CSS_VALUE_MEDIUMSLATEBLUE: c = 0x7B68EE;  break;
        case LXB_CSS_VALUE_MEDIUMSPRINGGREEN: c = 0x00FA9A;  break;
        case LXB_CSS_VALUE_MEDIUMTURQUOISE: c = 0x48D1CC;  break;
        case LXB_CSS_VALUE_MEDIUMVIOLETRED: c = 0xC71585;  break;
        case LXB_CSS_VALUE_MIDNIGHTBLUE: c = 0x191970;  break;
        case LXB_CSS_VALUE_MINTCREAM: c = 0xF5FFFA;  break;
        case LXB_CSS_VALUE_MISTYROSE: c = 0xFFE4E1;  break;
        case LXB_CSS_VALUE_MOCCASIN: c = 0xFFE4B5;  break;
        case LXB_CSS_VALUE_NAVAJOWHITE: c = 0xFFDEAD;  break;
        case LXB_CSS_VALUE_NAVY: c = 0x000080;  break;
        case LXB_CSS_VALUE_OLDLACE: c = 0xFDF5E6;  break;
        case LXB_CSS_VALUE_OLIVE: c = 0x808000;  break;
        case LXB_CSS_VALUE_OLIVEDRAB: c = 0x6B8E23;  break;
        case LXB_CSS_VALUE_ORANGE: c = 0xFFA500;  break;
        case LXB_CSS_VALUE_ORANGERED: c = 0xFF4500;  break;
        case LXB_CSS_VALUE_ORCHID: c = 0xDA70D6;  break;
        case LXB_CSS_VALUE_PALEGOLDENROD: c = 0xEEE8AA;  break;
        case LXB_CSS_VALUE_PALEGREEN: c = 0x98FB98;  break;
        case LXB_CSS_VALUE_PALETURQUOISE: c = 0xAFEEEE;  break;
        case LXB_CSS_VALUE_PALEVIOLETRED: c = 0xDB7093;  break;
        case LXB_CSS_VALUE_PAPAYAWHIP: c = 0xFFEFD5;  break;
        case LXB_CSS_VALUE_PEACHPUFF: c = 0xFFDAB9;  break;
        case LXB_CSS_VALUE_PERU: c = 0xCD853F;  break;
        case LXB_CSS_VALUE_PINK: c = 0xFFC0CB;  break;
        case LXB_CSS_VALUE_PLUM: c = 0xDDA0DD;  break;
        case LXB_CSS_VALUE_POWDERBLUE: c = 0xB0E0E6;  break;
        case LXB_CSS_VALUE_PURPLE: c = 0x800080;  break;
        case LXB_CSS_VALUE_REBECCAPURPLE: c = 0x663399;  break;
        case LXB_CSS_VALUE_RED: c = 0xFF0000;  break;
        case LXB_CSS_VALUE_ROSYBROWN: c = 0xBC8F8F;  break;
        case LXB_CSS_VALUE_ROYALBLUE: c = 0x4169E1;  break;
        case LXB_CSS_VALUE_SADDLEBROWN: c = 0x8B4513;  break;
        case LXB_CSS_VALUE_SALMON: c = 0xFA8072;  break;
        case LXB_CSS_VALUE_SANDYBROWN: c = 0xF4A460;  break;
        case LXB_CSS_VALUE_SEAGREEN: c = 0x2E8B57;  break;
        case LXB_CSS_VALUE_SEASHELL: c = 0xFFF5EE;  break;
        case LXB_CSS_VALUE_SIENNA: c = 0xA0522D;  break;
        case LXB_CSS_VALUE_SILVER: c = 0xC0C0C0;  break;
        case LXB_CSS_VALUE_SKYBLUE: c = 0x87CEEB;  break;
        case LXB_CSS_VALUE_SLATEBLUE: c = 0x6A5ACD;  break;
        case LXB_CSS_VALUE_SLATEGRAY: c = 0x708090;  break;
        case LXB_CSS_VALUE_SLATEGREY: c = 0x708090;  break;
        case LXB_CSS_VALUE_SNOW: c = 0xFFFAFA;  break;
        case LXB_CSS_VALUE_SPRINGGREEN: c = 0x00FF7F;  break;
        case LXB_CSS_VALUE_STEELBLUE: c = 0x4682B4;  break;
        case LXB_CSS_VALUE_TAN: c = 0xD2B48C;  break;
        case LXB_CSS_VALUE_TEAL: c = 0x008080;  break;
        case LXB_CSS_VALUE_THISTLE: c = 0xD8BFD8;  break;
        case LXB_CSS_VALUE_TOMATO: c = 0xFF6347;  break;
        case LXB_CSS_VALUE_TURQUOISE: c = 0x40E0D0;  break;
        case LXB_CSS_VALUE_VIOLET: c = 0xEE82EE;  break;
        case LXB_CSS_VALUE_WHEAT: c = 0xF5DEB3;  break;
        case LXB_CSS_VALUE_WHITE: c = 0xFFFFFF;  break;
        case LXB_CSS_VALUE_WHITESMOKE: c = 0xF5F5F5;  break;
        case LXB_CSS_VALUE_YELLOW: c = 0xFFFF00;  break;
        case LXB_CSS_VALUE_YELLOWGREEN: c = 0x9ACD32;  break;
        default: c = 0x000000;  break;
    }
    return (Color){(c << 8) | 0xFF};
}

// Color resolve_color_value(const lxb_css_value_color_t *color) {
//     switch (color->type) {
//     case LXB_CSS_COLOR_HEX:
//         const lxb_css_value_color_hex_t *hex = &color->u.hex;
//         const lxb_css_value_color_hex_rgba_t *rgba = &hex->rgba;
//         switch (hex->type) {
//             case LXB_CSS_PROPERTY_COLOR_HEX_TYPE_3:
//             case LXB_CSS_PROPERTY_COLOR_HEX_TYPE_4:
//                 lexbor_serialize_write(cb, &hmo[rgba->r], 1, ctx, status);
//                 lexbor_serialize_write(cb, &hmo[rgba->g], 1, ctx, status);
//                 lexbor_serialize_write(cb, &hmo[rgba->b], 1, ctx, status);
    
//                 if (hex->type == LXB_CSS_PROPERTY_COLOR_HEX_TYPE_4) {
//                     lexbor_serialize_write(cb, &hmo[rgba->a], 1, ctx, status);
//                 }
    
//                 break;
    
//             case LXB_CSS_PROPERTY_COLOR_HEX_TYPE_6:
//             case LXB_CSS_PROPERTY_COLOR_HEX_TYPE_8:
//                 lexbor_serialize_write(cb, hmt[rgba->r], 2, ctx, status);
//                 lexbor_serialize_write(cb, hmt[rgba->g], 2, ctx, status);
//                 lexbor_serialize_write(cb, hmt[rgba->b], 2, ctx, status);
    
//                 if (hex->type == LXB_CSS_PROPERTY_COLOR_HEX_TYPE_8) {
//                     lexbor_serialize_write(cb, hmt[rgba->a], 2, ctx, status);
//                 }
    
//                 break;
    
//             default:
//                 break;
//         }

//         case LXB_CSS_COLOR_RGB:
//         case LXB_CSS_COLOR_RGBA:
//             return lxb_css_value_color_rgb_sr(&color->u.rgb, cb, ctx,
//                                               color->type);

//         case LXB_CSS_COLOR_HSL:
//         case LXB_CSS_COLOR_HSLA:
//         case LXB_CSS_COLOR_HWB:
//             return lxb_css_value_color_hsl_sr(&color->u.hsl, cb, ctx,
//                                               color->type);

//         case LXB_CSS_COLOR_LAB:
//         case LXB_CSS_COLOR_OKLAB:
//             return lxb_css_value_color_lab_sr(&color->u.lab, cb, ctx,
//                                               color->type);

//         case LXB_CSS_COLOR_LCH:
//         case LXB_CSS_COLOR_OKLCH:
//             return lxb_css_value_color_lch_sr(&color->u.lch, cb, ctx,
//                                               color->type);

//         case LXB_CSS_VALUE__UNDEF:
//             break;

//         default:
//             return lxb_css_value_serialize(color->type, cb, ctx);
//     }
// }

float resolve_length_value(LayoutContext* lycon, const lxb_css_value_length_percentage_t *value) {
    float result = 0;
    switch (value->type) {
    case LXB_CSS_VALUE__NUMBER:  // keep it as it is
        result = value->u.length.num;
        break;
    case LXB_CSS_VALUE__LENGTH:      
        result = value->u.length.num;
        switch (value->u.length.unit) {
        // absolute units
        case LXB_CSS_UNIT_Q:  // 1Q = 1cm / 40
            result = value->u.length.num * (96 / 2.54 / 40) * lycon->ui_context->pixel_ratio;
            break;            
        case LXB_CSS_UNIT_CM:  // 96px / 2.54
            result = value->u.length.num * (96 / 2.54) * lycon->ui_context->pixel_ratio;
            break;
        case LXB_CSS_UNIT_IN:  // 96px
            result = value->u.length.num * 96 * lycon->ui_context->pixel_ratio;
            break;
        case LXB_CSS_UNIT_MM:  // 1mm = 1cm / 10
            result = value->u.length.num * (96 / 25.4) * lycon->ui_context->pixel_ratio;
            break;
        case LXB_CSS_UNIT_PC:  // 1pc = 12pt = 1in / 6.
            result = value->u.length.num * 16 * lycon->ui_context->pixel_ratio;
            break;
        case LXB_CSS_UNIT_PT:  // 1pt = 1in / 72
            result = value->u.length.num * 4 / 3 * lycon->ui_context->pixel_ratio;
            break;
        case LXB_CSS_UNIT_PX:
            result = value->u.length.num * lycon->ui_context->pixel_ratio;
            break;
        // relative units
        // case LXB_CSS_UNIT_CAP:
        //     result = value->u.length.num * lycon->font.style.font_size;
        //     break;
        case LXB_CSS_UNIT_EM:
            result = value->u.length.num * lycon->font.style.font_size;
            break;
        default:
            result = 0;
            printf("Unknown unit: %d\n", value->u.length.unit);    
        }
        break;
    case LXB_CSS_VALUE__PERCENTAGE:
        // todo: fix this
        result = value->u.percentage.num * lycon->block.width;
        break;
    default:
        result = 0;
    }
    return result;
}

void resolve_length_prop(LayoutContext* lycon, const lxb_css_property_margin_t *margin, Spacing* spacing) {
    printf("margin property: t %d, r %d, b %d, l %d, t %f, r %f, b %f, l %f\n", 
        margin->top.u.length.unit, margin->right.u.length.unit, margin->bottom.u.length.unit, margin->left.u.length.unit,
        margin->top.u.length.num, margin->right.u.length.num, margin->bottom.u.length.num, margin->left.u.length.num);
    int value_cnt = 0;
    if (margin->top.u.length.unit) {
        spacing->top = resolve_length_value(lycon, &margin->top);
        value_cnt++;
    }
    if (margin->right.u.length.unit) {
        spacing->right = resolve_length_value(lycon, &margin->right);
        value_cnt++;
    }
    if (margin->bottom.u.length.unit) {
        spacing->bottom = resolve_length_value(lycon, &margin->bottom);
        value_cnt++;
    }
    if (margin->left.u.length.unit) {
        spacing->left = resolve_length_value(lycon, &margin->left);
        value_cnt++;
    }
    switch (value_cnt) {
    case 1:  // all sides
        spacing->right = spacing->left = spacing->bottom = spacing->top;
        break;
    case 2:  // top-bottom, left-right
        spacing->left = spacing->right;
        spacing->bottom = spacing->top;
        break;      
    case 3:  // top, left-right, bottom
        spacing->left = spacing->right;
        break;
    // case 4:  // top, right, bottom, left
    //    break;
    }
}

PropValue resolve_element_display(lxb_html_element_t* elmt) {
    PropValue outer_display, inner_display;
    // determine element 'display'
    int name = elmt->element.node.local_name;  // todo: should check ns as well 
    switch (name) { 
        case LXB_TAG_H1: case LXB_TAG_H2: case LXB_TAG_H3: case LXB_TAG_H4: case LXB_TAG_H5: case LXB_TAG_H6:
        case LXB_TAG_P: case LXB_TAG_DIV: case LXB_TAG_CENTER: 
        case LXB_TAG_UL: case LXB_TAG_OL: 
            outer_display = LXB_CSS_VALUE_BLOCK;  inner_display = LXB_CSS_VALUE_FLOW;
            break;
        case LXB_TAG_LI:
            outer_display = LXB_CSS_VALUE_LIST_ITEM;  inner_display = LXB_CSS_VALUE_FLOW;
            break;
        case LXB_TAG_IMG:
            outer_display = LXB_CSS_VALUE_INLINE_BLOCK;  inner_display = LXB_CSS_VALUE_REPLACED;
            break;
        default:  // inline elements, like span, b, i, u, a, img, input
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

lxb_status_t resolve_element_style(lexbor_avl_t *avl, lexbor_avl_node_t **root,
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
        // black color is 0x000000FF, not 0x00
        span->in_line->color = color_name_to_rgb(color->type);
        break;
    case LXB_CSS_PROPERTY_BACKGROUND_COLOR:
        const lxb_css_property_background_color_t *background_color = declr->u.background_color;
        printf("background color property: %d\n", background_color->type);
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        if (!span->bound->background) {
            span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
        }
        span->bound->background->color = color_name_to_rgb(background_color->type);
        break;
    case LXB_CSS_PROPERTY_MARGIN:
        const lxb_css_property_margin_t *margin = declr->u.margin;
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        printf("@@margin prop: %lf, unit: %d\n", margin->top.u.length.num, margin->top.u.length.unit);
        resolve_length_prop(lycon, margin, &span->bound->margin);
        break;
    case LXB_CSS_PROPERTY_PADDING:
        const lxb_css_property_padding_t *padding = declr->u.padding;
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        resolve_length_prop(lycon, (lxb_css_property_margin_t*)padding, &span->bound->padding);
        break;
    case LXB_CSS_PROPERTY_BORDER:
        const lxb_css_property_border_t *border = declr->u.border;
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        if (!span->bound->border) {
            span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
        }
        span->bound->border->color = color_name_to_rgb(border->color.type);
        span->bound->border->width.top = resolve_length_value(lycon, 
            (lxb_css_value_length_percentage_t*)&border->width);
        span->bound->border->width.bottom = span->bound->border->width.left 
            = span->bound->border->width.right = span->bound->border->width.top;
        span->bound->border->style = border->style;
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