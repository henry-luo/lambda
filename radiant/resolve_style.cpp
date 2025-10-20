#include "layout.hpp"
#include "grid.hpp"
#include "layout_table.hpp"

#include "../lib/log.h"
#include <string.h>
AlignType resolve_align_type(PropValue value);

float resolve_length_value(LayoutContext* lycon, uintptr_t property,
    const lxb_css_value_length_percentage_t *value);

lxb_status_t style_print_callback(const lxb_char_t *data, size_t len, void *ctx) {
    log_debug("style rule: %.*s", (int) len, (const char *) data);
    return LXB_STATUS_OK;
}

lxb_status_t lxb_html_element_style_print(lexbor_avl_t *avl, lexbor_avl_node_t **root,
    lexbor_avl_node_t *node, void *ctx) {
    lxb_css_rule_declaration_t *declr = (lxb_css_rule_declaration_t *) node->value;
    log_debug("style entry: %ld", declr->type);
    lxb_css_rule_declaration_serialize(declr, style_print_callback, NULL);
    return LXB_STATUS_OK;
}

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
    uint32_t r = (c >> 16) & 0xFF;
    uint32_t g = (c >> 8) & 0xFF;
    uint32_t b = c & 0xFF;
    return (Color){ 0xFF000000 | (b << 16) | (g << 8) | r };
}

Color resolve_color_value(const lxb_css_value_color_t *color) {
    uint32_t r, g, b, a;  Color c;
    switch (color->type) {
    case LXB_CSS_COLOR_TRANSPARENT:
        return (Color){0};
    case LXB_CSS_COLOR_HEX: {
        const lxb_css_value_color_hex_t *hex = &color->u.hex;
        const lxb_css_value_color_hex_rgba_t *rgba = &hex->rgba;
        switch (hex->type) {
            case LXB_CSS_PROPERTY_COLOR_HEX_TYPE_4:
            case LXB_CSS_PROPERTY_COLOR_HEX_TYPE_3:
                log_debug("color 3/4 hex: %d, %d, %d, %d", rgba->r, rgba->g, rgba->b, rgba->a);
                r = (rgba->r << 4) | rgba->r;
                g = (rgba->g << 4) | rgba->g;
                b = (rgba->b << 4) | rgba->b;
                a = (rgba->a << 4) | rgba->a;
                c.c = (a << 24) | (b << 16) | (g << 8) | r;
                return c;
            case LXB_CSS_PROPERTY_COLOR_HEX_TYPE_8:
            case LXB_CSS_PROPERTY_COLOR_HEX_TYPE_6:
                log_debug("color 6 hex: %d, %d, %d", rgba->r, rgba->g, rgba->b);
                // r = rgba->r;  g = rgba->g;  b = rgba->b;  a = rgba->a;
                // c.c = (r << 24) | (g << 16) | (b << 8) | a;  log_debug("c: %d", c.c);
                c.c = *((uint32_t*)rgba);
                return c;
        }
        break;
        // case LXB_CSS_COLOR_RGB:
        // case LXB_CSS_COLOR_RGBA:
        // case LXB_CSS_COLOR_HSL:
        // case LXB_CSS_COLOR_HSLA:
        // case LXB_CSS_COLOR_HWB:
        // case LXB_CSS_COLOR_LAB:
        // case LXB_CSS_COLOR_OKLAB:
        // case LXB_CSS_COLOR_LCH:
        // case LXB_CSS_COLOR_OKLCH:
        // case LXB_CSS_VALUE__UNDEF:
        //     break;
    }
    default:
        return color_name_to_rgb(color->type);
    }
    return (Color){0};
}

void resolve_font_size(LayoutContext* lycon, const lxb_css_rule_declaration_t* decl) {
    log_debug("resolve font size property");
    if (!decl) {
        log_debug("no decl");
        if (lycon->elmt->style) {
            if (lycon->elmt->as_element()) {
                decl = lxb_dom_element_style_by_id((lxb_dom_element_t*)lycon->elmt->as_element(), LXB_CSS_PROPERTY_FONT_SIZE);
            }
        }
    }
    if (decl) {
        log_debug("got decl");
        lxb_css_property_font_size_t* font_size = decl->u.font_size;
        log_debug("resolving font length");
        lycon->font.current_font_size = resolve_length_value(lycon,
            LXB_CSS_PROPERTY_FONT_SIZE, &font_size->length);
        return;
    }
    // use font size from context
    lycon->font.current_font_size = lycon->font.style->font_size;
    log_debug("resolved font size");
}

float resolve_length_value(LayoutContext* lycon, uintptr_t property,
    const lxb_css_value_length_percentage_t *value) {
    float result = 0;

    switch (value->type) {
    case LXB_CSS_VALUE__NUMBER:  // keep it as it is
        log_debug("number value");
        result = value->u.length.num;
        break;
    case LXB_CSS_VALUE__LENGTH:
        log_debug("length value unit: %d", value->u.length.unit);
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
        case LXB_CSS_UNIT_REM:
            if (lycon->root_font_size < 0) {
                log_debug("resolving font size for rem value");
                resolve_font_size(lycon, NULL);
                lycon->root_font_size = lycon->font.current_font_size < 0 ?
                    lycon->ui_context->default_font.font_size : lycon->font.current_font_size;
            }
            result = value->u.length.num * lycon->root_font_size;
            break;
        case LXB_CSS_UNIT_EM:
            if (property == LXB_CSS_PROPERTY_FONT_SIZE) {
                result = value->u.length.num * lycon->font.style->font_size;
            } else {
                if (lycon->font.current_font_size < 0) {
                    log_debug("resolving font size for em value");
                    resolve_font_size(lycon, NULL);
                }
                result = value->u.length.num * lycon->font.current_font_size;
            }
            break;
        default:
            result = 0;
            log_debug("Unknown unit: %d", value->u.length.unit);
        }
        break;
    case LXB_CSS_VALUE__PERCENTAGE:
        if (property == LXB_CSS_PROPERTY_FONT_SIZE) {
            result = value->u.percentage.num * lycon->font.style->font_size / 100;
        } else {
            // todo: handle % based on property
            log_debug("Percentage calculation: %.2f%% of parent width %d = %.2f",
                   value->u.percentage.num, lycon->block.pa_block->width,
                   value->u.percentage.num * lycon->block.pa_block->width / 100);
            result = value->u.percentage.num * lycon->block.pa_block->width / 100;
        }
        break;
    case LXB_CSS_VALUE_AUTO:
        log_info("length value: auto");
        result = 0;
        break;
    default:
        log_warn("unknown length type: %d (LXB_CSS_VALUE_AUTO=%d)", value->type, LXB_CSS_VALUE_AUTO);
        result = 0;
    }
    log_debug("length value: type %d, val %f", value->type, result);
    return result;
}

// resolve property 'margin', 'padding', etc.
void resolve_spacing_prop(LayoutContext* lycon, uintptr_t property,
    const lxb_css_property_margin_t *src_space, int32_t specificity, Spacing* trg_spacing) {
    Margin sp;  // temporal space
    int value_cnt = 0;  bool is_margin = property == LXB_CSS_PROPERTY_MARGIN;
    log_debug("resolve_spacing_prop");
    if (src_space->top.type != LXB_CSS_VALUE__UNDEF) {
        log_debug("resolving spacing 1st");
        sp.top = resolve_length_value(lycon, property, (lxb_css_value_length_percentage_t *)&src_space->top);
        value_cnt++;
    }
    if (src_space->right.type != LXB_CSS_VALUE__UNDEF) {
        log_debug("resolving spacing 2nd");
        sp.right = resolve_length_value(lycon, property, (lxb_css_value_length_percentage_t *)&src_space->right);
        value_cnt++;
    }
    if (src_space->bottom.type != LXB_CSS_VALUE__UNDEF) {
        log_debug("resolving spacing 3rd");
        sp.bottom = resolve_length_value(lycon, property, (lxb_css_value_length_percentage_t *)&src_space->bottom);
        value_cnt++;
    }
    if (src_space->left.type != LXB_CSS_VALUE__UNDEF) {
        log_debug("resolving spacing 4th");
        sp.left = resolve_length_value(lycon, property, (lxb_css_value_length_percentage_t *)&src_space->left);
        value_cnt++;
    }
    log_debug("spacing value count: %d, value: %f, specificity: %d, top spec: %d", value_cnt, sp.top, specificity, trg_spacing->top_specificity);
    switch (value_cnt) {
    case 1:
        sp.right = sp.left = sp.bottom = sp.top;
        if (is_margin) { sp.right_type = sp.left_type = sp.bottom_type = sp.top_type = src_space->top.type; }
        break;
    case 2:
        sp.bottom = sp.top;  sp.left = sp.right;
        if (is_margin) {
            sp.top_type = sp.bottom_type = src_space->top.type;
            sp.left_type = sp.right_type = src_space->right.type;
        }
        break;
    case 3:
        sp.left = sp.right;
        if (is_margin) {
            sp.top_type = src_space->top.type;  sp.bottom_type = src_space->bottom.type;
            sp.left_type = sp.right_type = src_space->right.type;
        }
        break;
    case 4:
        // no change to values
        if (is_margin) {
            sp.top_type = src_space->top.type;  sp.bottom_type = src_space->bottom.type;
            sp.left_type = src_space->left.type;  sp.right_type = src_space->right.type;
        }
        break;
    }
    // store value in final spacing struct if specificity is higher
    Margin *trg_margin = is_margin ? (Margin *) trg_spacing : NULL;
    if (specificity > trg_spacing->top_specificity) {
        trg_spacing->top = sp.top;
        trg_spacing->top_specificity = specificity;
        if (trg_margin) trg_margin->top_type = sp.top_type;
        log_debug("updated top spacing to %f", trg_spacing->top);
    }
    if (specificity > trg_spacing->bottom_specificity) {
        trg_spacing->bottom = sp.bottom;
        trg_spacing->bottom_specificity = specificity;
        if (trg_margin) trg_margin->bottom_type = sp.bottom_type;
    }
    if (specificity > trg_spacing->right_specificity) {
        // only margin-left and right support auto value
        trg_spacing->right = sp.right;
        trg_spacing->right_specificity = specificity;
        if (trg_margin) trg_margin->right_type = sp.right_type;
    }
    if (specificity > trg_spacing->left_specificity) {
        trg_spacing->left = sp.left;
        trg_spacing->left_specificity = specificity;
        if (trg_margin) trg_margin->left_type = sp.left_type;
    }
    log_debug("spacing value: top %f, right %f, bottom %f, left %f",
        trg_spacing->top, trg_spacing->right, trg_spacing->bottom, trg_spacing->left);
}

DisplayValue resolve_display(lxb_html_element_t* elmt) {
    PropValue outer_display, inner_display;
    // determine element 'display'
    int name = elmt->element.node.local_name;  // todo: should check ns as well
    switch (name) {
        case LXB_TAG_BODY: case LXB_TAG_H1: case LXB_TAG_H2: case LXB_TAG_H3:
        case LXB_TAG_H4: case LXB_TAG_H5: case LXB_TAG_H6:
        case LXB_TAG_P: case LXB_TAG_DIV: case LXB_TAG_CENTER:
        case LXB_TAG_UL: case LXB_TAG_OL:
        case LXB_TAG_HEADER: case LXB_TAG_MAIN: case LXB_TAG_SECTION: case LXB_TAG_FOOTER:
        case LXB_TAG_ARTICLE: case LXB_TAG_ASIDE: case LXB_TAG_NAV:
        case LXB_TAG_ADDRESS: case LXB_TAG_BLOCKQUOTE:
        case LXB_TAG_DETAILS: case LXB_TAG_DIALOG: case LXB_TAG_FIGURE:
        case LXB_TAG_MENU:
            outer_display = LXB_CSS_VALUE_BLOCK;  inner_display = LXB_CSS_VALUE_FLOW;
            break;
        case LXB_TAG_LI:  case LXB_TAG_SUMMARY:
            outer_display = LXB_CSS_VALUE_LIST_ITEM;  inner_display = LXB_CSS_VALUE_FLOW;
            break;
        case LXB_TAG_IMG:  case LXB_TAG_VIDEO:
        case LXB_TAG_INPUT: case LXB_TAG_SELECT: case LXB_TAG_TEXTAREA:  case LXB_TAG_BUTTON:
            outer_display = LXB_CSS_VALUE_INLINE_BLOCK;  inner_display = RDT_DISPLAY_REPLACED;
            break;
        case LXB_TAG_HR:
            outer_display = LXB_CSS_VALUE_BLOCK;  inner_display = RDT_DISPLAY_REPLACED;
            break;
        case LXB_TAG_IFRAME:
            outer_display = LXB_CSS_VALUE_INLINE_BLOCK; inner_display = RDT_DISPLAY_REPLACED;
            break;
        case LXB_TAG_SCRIPT:  case LXB_TAG_STYLE:  case LXB_TAG_SVG:
            outer_display = LXB_CSS_VALUE_NONE;  inner_display = LXB_CSS_VALUE_NONE;
            break;
        // HTML table elements default display mapping (Phase 1)
        case LXB_TAG_TABLE:
            outer_display = LXB_CSS_VALUE_BLOCK;  inner_display = LXB_CSS_VALUE_TABLE;  break;
        case LXB_TAG_CAPTION:
            outer_display = LXB_CSS_VALUE_BLOCK;  inner_display = LXB_CSS_VALUE_FLOW;  break;
        case LXB_TAG_THEAD: case LXB_TAG_TBODY: case LXB_TAG_TFOOT:
            outer_display = LXB_CSS_VALUE_BLOCK;  inner_display = LXB_CSS_VALUE_TABLE_ROW_GROUP;  break;
        case LXB_TAG_TR:
            outer_display = LXB_CSS_VALUE_BLOCK;  inner_display = LXB_CSS_VALUE_TABLE_ROW;  break;
        case LXB_TAG_TH: case LXB_TAG_TD:
            outer_display = LXB_CSS_VALUE_TABLE_CELL;  inner_display = LXB_CSS_VALUE_TABLE_CELL;  break;
        case LXB_TAG_COLGROUP:
            outer_display = LXB_CSS_VALUE_BLOCK;  inner_display = LXB_CSS_VALUE_TABLE_COLUMN_GROUP;  break;
        case LXB_TAG_COL:
            outer_display = LXB_CSS_VALUE_BLOCK;  inner_display = LXB_CSS_VALUE_TABLE_COLUMN;  break;
        default:  // inline elements, like span, b, i, u, a, img, input, or custom elements
            outer_display = LXB_CSS_VALUE_INLINE;  inner_display = LXB_CSS_VALUE_FLOW;
    }
    // get CSS display if specified
    if (elmt->element.style != NULL) {
        const lxb_css_rule_declaration_t* display_decl =
            lxb_dom_element_style_by_id((lxb_dom_element_t*)elmt, LXB_CSS_PROPERTY_DISPLAY);
        if (display_decl) {
            log_debug("DEBUG: CSS display found - a=%d, b=%d (GRID=%d)",
                   display_decl->u.display->a, display_decl->u.display->b, LXB_CSS_VALUE_GRID);
            log_debug("display_value: %s, %s", lxb_css_value_by_id(display_decl->u.display->a)->name,
                lxb_css_value_by_id(display_decl->u.display->b)->name);
            if (display_decl->u.display->b == LXB_CSS_VALUE__UNDEF) {
                // map single display value
                log_debug("DEBUG: Mapping single display value: %d", display_decl->u.display->a);
                switch (display_decl->u.display->a) {
                    case LXB_CSS_VALUE_BLOCK:
                        outer_display = LXB_CSS_VALUE_BLOCK;
                        inner_display = LXB_CSS_VALUE_FLOW;
                        break;
                    case LXB_CSS_VALUE_INLINE:
                        outer_display = LXB_CSS_VALUE_INLINE;
                        inner_display = LXB_CSS_VALUE_FLOW;
                        break;
                    case LXB_CSS_VALUE_INLINE_BLOCK:
                        outer_display = LXB_CSS_VALUE_INLINE_BLOCK;
                        inner_display = LXB_CSS_VALUE_FLOW;
                        break;
                    case LXB_CSS_VALUE_FLEX:
                        outer_display = LXB_CSS_VALUE_BLOCK;
                        inner_display = LXB_CSS_VALUE_FLEX;
                        break;
                    case LXB_CSS_VALUE_INLINE_FLEX:
                        outer_display = LXB_CSS_VALUE_INLINE_BLOCK;
                        inner_display = LXB_CSS_VALUE_FLEX;
                        break;
                    case LXB_CSS_VALUE_GRID:
                        log_debug("DEBUG: GRID case matched! Setting inner=GRID\n");
                        outer_display = LXB_CSS_VALUE_BLOCK;
                        inner_display = LXB_CSS_VALUE_GRID;
                        break;
                    case LXB_CSS_VALUE_INLINE_GRID:
                        outer_display = LXB_CSS_VALUE_INLINE;
                        inner_display = LXB_CSS_VALUE_GRID;
                        break;
                    case LXB_CSS_VALUE_TABLE:
                        outer_display = LXB_CSS_VALUE_BLOCK;
                        inner_display = LXB_CSS_VALUE_TABLE;
                        break;
                    case LXB_CSS_VALUE_INLINE_TABLE:
                        outer_display = LXB_CSS_VALUE_INLINE;
                        inner_display = LXB_CSS_VALUE_TABLE;
                        break;
                    case LXB_CSS_VALUE_LIST_ITEM:
                        outer_display = LXB_CSS_VALUE_LIST_ITEM;
                        inner_display = LXB_CSS_VALUE_FLOW;
                        break;
                    case LXB_CSS_VALUE_TABLE_ROW:
                        outer_display = LXB_CSS_VALUE_TABLE_ROW;
                        inner_display = LXB_CSS_VALUE_TABLE_ROW;
                        break;
                    case LXB_CSS_VALUE_TABLE_CELL:
                        outer_display = LXB_CSS_VALUE_TABLE_CELL;
                        inner_display = LXB_CSS_VALUE_TABLE_CELL;
                        break;
                    case LXB_CSS_VALUE_NONE:
                        log_debug("DEBUG: NONE case matched! Setting display=none");
                        outer_display = LXB_CSS_VALUE_NONE;
                        inner_display = LXB_CSS_VALUE_NONE;
                        break;
                    default:  // unknown display
                        log_debug("DEBUG: Unknown display value %d, defaulting to inline flow", display_decl->u.display->a);
                        outer_display = LXB_CSS_VALUE_INLINE;
                        inner_display = LXB_CSS_VALUE_FLOW;
                }
            } else {
                outer_display = display_decl->u.display->a;
                inner_display = display_decl->u.display->b;
            }
        }
    }
    return (DisplayValue){.outer = outer_display, .inner = inner_display};
}

lxb_status_t resolve_element_style(lexbor_avl_t *avl, lexbor_avl_node_t **root,
    lexbor_avl_node_t *node, void *ctx) {
    LayoutContext* lycon = (LayoutContext*) ctx;
    // specificity for * ( ... ) is 0, for html element default style is -1
    int32_t specificity = ((lxb_style_node_t *) node)->sp;
    lxb_css_rule_declaration_t *declr = (lxb_css_rule_declaration_t *) node->value;
    const lxb_css_entry_data_t *data = lxb_css_property_by_id(declr->type);
    if (!data) {
        log_debug("no data for property: %ld %s", declr->type);
        return LXB_STATUS_ERROR_NOT_EXISTS;
    }

    log_debug("style property: %s (type=%ld), specificity: %d", data->name, declr->type, specificity);
    if (!lycon->view) { log_debug("missing view");  return LXB_STATUS_ERROR_NOT_EXISTS; }
    ViewSpan* span = (ViewSpan*)lycon->view;
    ViewBlock* block = lycon->view->type != RDT_VIEW_INLINE ? (ViewBlock*)lycon->view : NULL;

    switch (declr->type) {
    case LXB_CSS_PROPERTY_LINE_HEIGHT: {
        if (!block) { break; }
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        lxb_css_property_line_height_t* line_height = declr->u.line_height;
        switch (line_height->type) {
        case LXB_CSS_VALUE__NUMBER:
            if (line_height->u.number.num >= 0) block->blk->line_height = line_height;
            break;
        case LXB_CSS_VALUE__LENGTH:
            if (line_height->u.length.num >= 0) block->blk->line_height = line_height;
            break;
        case LXB_CSS_VALUE__PERCENTAGE:
            if (line_height->u.percentage.num >= 0) block->blk->line_height = line_height;
            break;
        case LXB_CSS_VALUE_NORMAL:
            block->blk->line_height = line_height;
            break;
        case LXB_CSS_VALUE_INHERIT:
            block->blk->line_height = line_height;
            break;
        }
        // ignored
        break;
    }
    case LXB_CSS_PROPERTY_VERTICAL_ALIGN: {
        lxb_css_property_vertical_align_t* vertical_align = declr->u.vertical_align;
        PropValue valign = vertical_align->alignment.type ?
            vertical_align->alignment.type : vertical_align->shift.type;
        if (!span->in_line) {
            span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
        }
        span->in_line->vertical_align = valign;
        break;
    }
    case LXB_CSS_PROPERTY_CURSOR: {
        const lxb_css_property_cursor_t *cursor = declr->u.cursor;
        log_debug("cursor property: %d", cursor->type);
        if (!span->in_line) {
            span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
        }
        span->in_line->cursor = cursor->type;
        break;
    }
    case LXB_CSS_PROPERTY_COLOR: {
        const lxb_css_property_color_t *color = declr->u.color;
        log_debug("color property: %d, red: %d", color->type, LXB_CSS_VALUE_RED);
        if (!span->in_line) {
            span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
        }
        // black color is 0x000000FF, not 0x00
        span->in_line->color = resolve_color_value(color);
        break;
    }
    case LXB_CSS_PROPERTY_BACKGROUND:  case LXB_CSS_PROPERTY_BACKGROUND_COLOR: {
        const lxb_css_property_background_color_t *background_color = declr->u.background_color;
        log_debug("background color property: %d", background_color->type);
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        if (!span->bound->background) {
            span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
        }
        span->bound->background->color = resolve_color_value(background_color);
        break;
    }
    case LXB_CSS_PROPERTY_MARGIN: {
        const lxb_css_property_margin_t *margin = declr->u.margin;
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        resolve_spacing_prop(lycon, LXB_CSS_PROPERTY_MARGIN, margin, specificity, &span->bound->margin);

        // Handle shorthand expansion (1-4 values)
        int value_cnt = 0;
        if (margin->top.type != LXB_CSS_VALUE__UNDEF) value_cnt++;
        if (margin->right.type != LXB_CSS_VALUE__UNDEF) value_cnt++;
        if (margin->bottom.type != LXB_CSS_VALUE__UNDEF) value_cnt++;
        if (margin->left.type != LXB_CSS_VALUE__UNDEF) value_cnt++;
        log_debug("margin value: top %f, right %f, bottom %f, left %f",
            span->bound->margin.top, span->bound->margin.right, span->bound->margin.bottom, span->bound->margin.left);
        break;
    }
    case LXB_CSS_PROPERTY_PADDING: {
        const lxb_css_property_padding_t *padding = declr->u.padding;
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        resolve_spacing_prop(lycon, LXB_CSS_PROPERTY_PADDING,
            (lxb_css_property_margin_t*)padding, specificity, &span->bound->padding);
        break;
    }
    case LXB_CSS_PROPERTY_MARGIN_LEFT:      case LXB_CSS_PROPERTY_MARGIN_RIGHT:
    case LXB_CSS_PROPERTY_MARGIN_TOP:       case LXB_CSS_PROPERTY_MARGIN_BOTTOM:
    case LXB_CSS_PROPERTY_PADDING_LEFT:     case LXB_CSS_PROPERTY_PADDING_RIGHT:
    case LXB_CSS_PROPERTY_PADDING_TOP:      case LXB_CSS_PROPERTY_PADDING_BOTTOM: {
        const lxb_css_value_length_percentage_t *space = declr->u.margin_left;
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }

        // Check for auto margins (important for flexbox)
        float space_length = resolve_length_value(lycon, declr->type, (lxb_css_value_length_percentage_t *)space);

        switch (declr->type) {
        case LXB_CSS_PROPERTY_MARGIN_LEFT:
            if (specificity > span->bound->margin.left_specificity) {
                span->bound->margin.left = space_length;
                span->bound->margin.left_specificity = specificity;
            }
            break;
        case LXB_CSS_PROPERTY_MARGIN_RIGHT:
            if (specificity > span->bound->margin.right_specificity) {
                span->bound->margin.right = space_length;
                span->bound->margin.right_specificity = specificity;
            }
            break;
        case LXB_CSS_PROPERTY_MARGIN_TOP:
            if (specificity > span->bound->margin.top_specificity) {
                span->bound->margin.top = space_length;
                span->bound->margin.top_specificity = specificity;
            }
            break;
        case LXB_CSS_PROPERTY_MARGIN_BOTTOM:
            if (specificity > span->bound->margin.bottom_specificity) {
                span->bound->margin.bottom = space_length;
                span->bound->margin.bottom_specificity = specificity;
            }
            break;
        case LXB_CSS_PROPERTY_PADDING_LEFT:
            if (specificity > span->bound->padding.left_specificity) {
                span->bound->padding.left = space_length;
                span->bound->padding.left_specificity = specificity;
            }
            break;
        case LXB_CSS_PROPERTY_PADDING_RIGHT:
            if (specificity > span->bound->padding.right_specificity) {
                span->bound->padding.right = space_length;
                span->bound->padding.right_specificity = specificity;
            }
            break;
        case LXB_CSS_PROPERTY_PADDING_TOP:
            if (specificity > span->bound->padding.top_specificity) {
                span->bound->padding.top = space_length;
                span->bound->padding.top_specificity = specificity; // Updated from space->specificity
            }
            break;
        case LXB_CSS_PROPERTY_PADDING_BOTTOM:
            if (specificity > span->bound->padding.bottom_specificity) {
                span->bound->padding.bottom = space_length;
                span->bound->padding.bottom_specificity = specificity; // Updated from space->specificity
            }
            break;
        }
        break;
    }
    case LXB_CSS_PROPERTY_BORDER_TOP:  case LXB_CSS_PROPERTY_BORDER_RIGHT:
    case LXB_CSS_PROPERTY_BORDER_BOTTOM:  case LXB_CSS_PROPERTY_BORDER_LEFT: {
        const lxb_css_property_border_top_t *border_top = declr->u.border_top;
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        if (!span->bound->border) {
            span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
        }
        Color c = resolve_color_value(&border_top->color);
        if (declr->type == LXB_CSS_PROPERTY_BORDER_TOP) {
            if (specificity > span->bound->border->top_color_specificity) {
                span->bound->border->top_color = c;
                span->bound->border->top_color_specificity = specificity;
            }
        }
        else if (declr->type == LXB_CSS_PROPERTY_BORDER_BOTTOM) {
            if (specificity > span->bound->border->bottom_color_specificity) {
                span->bound->border->bottom_color = c;
                span->bound->border->bottom_color_specificity = specificity;
            }
        }
        else if (declr->type == LXB_CSS_PROPERTY_BORDER_LEFT) {
            if (specificity > span->bound->border->left_color_specificity) {
                span->bound->border->left_color = c;
                span->bound->border->left_color_specificity = specificity;
            }
        }
        else if (declr->type == LXB_CSS_PROPERTY_BORDER_RIGHT) {
            if (specificity > span->bound->border->right_color_specificity) {
                span->bound->border->right_color = c;
                span->bound->border->right_color_specificity = specificity;
            }
        }
        float length = resolve_length_value(lycon, LXB_CSS_PROPERTY_BORDER,
            (lxb_css_value_length_percentage_t*)&border_top->width);
        if (declr->type == LXB_CSS_PROPERTY_BORDER_TOP) {
            if (specificity > span->bound->border->width.top_specificity) {
                span->bound->border->width.top = length;
                span->bound->border->width.top_specificity = specificity;
            }
        }
        else if (declr->type == LXB_CSS_PROPERTY_BORDER_BOTTOM) {
            if (specificity > span->bound->border->width.bottom_specificity) {
                span->bound->border->width.bottom = length;
                span->bound->border->width.bottom_specificity = specificity;
            }
        }
        else if (declr->type == LXB_CSS_PROPERTY_BORDER_LEFT) {
            if (specificity > span->bound->border->width.left_specificity) {
                span->bound->border->width.left = length;
                span->bound->border->width.left_specificity = specificity;
            }
        }
        else if (declr->type == LXB_CSS_PROPERTY_BORDER_RIGHT) {
            if (specificity > span->bound->border->width.right_specificity) {
                span->bound->border->width.right = length;
                span->bound->border->width.right_specificity = specificity;
            }
        }
        span->bound->border->top_style = span->bound->border->right_style =
        span->bound->border->bottom_style = span->bound->border->left_style =
            border_top->style;
        break;
    }
    case LXB_CSS_PROPERTY_BORDER: {
        const lxb_css_property_border_t *border = declr->u.border;
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        if (!span->bound->border) {
            span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
        }
        Color c = resolve_color_value(&border->color);
        if (specificity > span->bound->border->top_color_specificity) {
            span->bound->border->top_color = c;
            span->bound->border->top_color_specificity = specificity;
        }
        if (specificity > span->bound->border->bottom_color_specificity) {
            span->bound->border->bottom_color = c;
            span->bound->border->bottom_color_specificity = specificity;
        }
        if (specificity > span->bound->border->left_color_specificity) {
            span->bound->border->left_color = c;
            span->bound->border->left_color_specificity = specificity;
        }
        if (specificity > span->bound->border->right_color_specificity) {
            span->bound->border->right_color = c;
            span->bound->border->right_color_specificity = specificity;
        }
        float length = resolve_length_value(lycon, LXB_CSS_PROPERTY_BORDER,
            (lxb_css_value_length_percentage_t*)&border->width);
        if (specificity > span->bound->border->width.top_specificity) {
            span->bound->border->width.top = length;
            span->bound->border->width.top_specificity = specificity;
        }
        if (specificity > span->bound->border->width.bottom_specificity) {
            span->bound->border->width.bottom = length;
            span->bound->border->width.bottom_specificity = specificity;
        }
        if (specificity > span->bound->border->width.left_specificity) {
            span->bound->border->width.left = length;
            span->bound->border->width.left_specificity = specificity;
        }
        if (specificity > span->bound->border->width.right_specificity) {
            span->bound->border->width.right = length;
            span->bound->border->width.right_specificity = specificity;
        }
        span->bound->border->top_style = span->bound->border->right_style =
        span->bound->border->bottom_style = span->bound->border->left_style =
            border->style;
        break;
    }
    case LXB_CSS_PROPERTY_BORDER_TOP_COLOR:  case LXB_CSS_PROPERTY_BORDER_BOTTOM_COLOR:
    case LXB_CSS_PROPERTY_BORDER_LEFT_COLOR: case LXB_CSS_PROPERTY_BORDER_RIGHT_COLOR: {
        const lxb_css_property_border_top_color_t *border_color = declr->u.border_top_color;
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        if (!span->bound->border) {
            span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
        }
        Color c = resolve_color_value(border_color);
        switch (declr->type) {
        case LXB_CSS_PROPERTY_BORDER_TOP_COLOR:
            if (specificity > span->bound->border->top_color_specificity) {
                span->bound->border->top_color = c;
                span->bound->border->top_color_specificity = specificity;
            }
            break;
        case LXB_CSS_PROPERTY_BORDER_BOTTOM_COLOR:
            if (specificity > span->bound->border->bottom_color_specificity) {
                span->bound->border->bottom_color = c;
                span->bound->border->bottom_color_specificity = specificity;
            }
            break;
        case LXB_CSS_PROPERTY_BORDER_LEFT_COLOR:
            if (specificity > span->bound->border->left_color_specificity) {
                span->bound->border->left_color = c;
                span->bound->border->left_color_specificity = specificity;
            }
            break;
        case LXB_CSS_PROPERTY_BORDER_RIGHT_COLOR:
            if (specificity > span->bound->border->right_color_specificity) {
                span->bound->border->right_color = c;
                span->bound->border->right_color_specificity = specificity;
            }
            break;
        }
        break;
    }
    case LXB_CSS_PROPERTY_BORDER_STYLE: {
        const lxb_css_property_border_style_t *border_style = declr->u.border_style;
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        if (!span->bound->border) {
            span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
        }

        PropValue top_style = LXB_CSS_VALUE__UNDEF;
        PropValue right_style = LXB_CSS_VALUE__UNDEF;
        PropValue bottom_style = LXB_CSS_VALUE__UNDEF;
        PropValue left_style = LXB_CSS_VALUE__UNDEF;

        int style_values = 0;
        if (border_style->top != LXB_CSS_VALUE__UNDEF) {
            top_style = border_style->top;
            style_values++;
        }
        if (border_style->right != LXB_CSS_VALUE__UNDEF) {
            right_style = border_style->right;
            style_values++;
        }
        if (border_style->bottom != LXB_CSS_VALUE__UNDEF) {
            bottom_style = border_style->bottom;
            style_values++;
        }
        if (border_style->left != LXB_CSS_VALUE__UNDEF) {
            left_style = border_style->left;
            style_values++;
        }

        switch (style_values) {
        case 1:
            span->bound->border->top_style = top_style;
            span->bound->border->right_style = top_style;
            span->bound->border->bottom_style = top_style;
            span->bound->border->left_style = top_style;
            break;
        case 2:
            span->bound->border->top_style = top_style;
            span->bound->border->bottom_style = top_style;
            span->bound->border->right_style = right_style;
            span->bound->border->left_style = right_style;
            break;
        case 3:
            span->bound->border->top_style = top_style;
            span->bound->border->right_style = right_style;
            span->bound->border->left_style = right_style;
            span->bound->border->bottom_style = bottom_style;
            break;
        case 4:
            span->bound->border->top_style = top_style;
            span->bound->border->right_style = right_style;
            span->bound->border->bottom_style = bottom_style;
            span->bound->border->left_style = left_style;
            break;
        }
        break;
    }
    case LXB_CSS_PROPERTY_BORDER_TOP_STYLE: {
        const lxb_css_property_border_top_style_t *border_top_style = declr->u.border_top_style;
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        if (!span->bound->border) {
            span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
        }
        span->bound->border->top_style = border_top_style->type;
        break;
    }
    case LXB_CSS_PROPERTY_BORDER_RIGHT_STYLE: {
        const lxb_css_property_border_right_style_t *border_right_style = declr->u.border_right_style;
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        if (!span->bound->border) {
            span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
        }
        span->bound->border->right_style = border_right_style->type;
        break;
    }
    case LXB_CSS_PROPERTY_BORDER_BOTTOM_STYLE: {
        const lxb_css_property_border_bottom_style_t *border_bottom_style = declr->u.border_bottom_style;
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        if (!span->bound->border) {
            span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
        }
        span->bound->border->bottom_style = border_bottom_style->type;
        break;
    }
    case LXB_CSS_PROPERTY_BORDER_LEFT_STYLE: {
        const lxb_css_property_border_left_style_t *border_left_style = declr->u.border_left_style;
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        if (!span->bound->border) {
            span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
        }
        span->bound->border->left_style = border_left_style->type;
        break;
    }
    case LXB_CSS_PROPERTY_BORDER_RADIUS: {
        const lxb_css_property_border_radius_t *border_radius = declr->u.border_radius;
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        if (!span->bound->border) {
            span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
        }
        resolve_spacing_prop(lycon, LXB_CSS_PROPERTY_BORDER_RADIUS,
            (lxb_css_property_margin_t*)border_radius, specificity, &span->bound->border->radius);
        break;
    }
    /*
    case LXB_CSS_PROPERTY_BORDER_TOP_LEFT_RADIUS:
        const lxb_css_property_border_top_left_radius_t *top_left_radius = declr->u.border_top_left_radius;
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        if (!span->bound->border) {
            span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
        }

        int tl_h = resolve_length_value(lycon, LXB_CSS_PROPERTY_BORDER_TOP_LEFT_RADIUS,
            &top_left_radius->horizontal);
        int tl_v = top_left_radius->vertical.type != LXB_CSS_VALUE__UNDEF ?
            resolve_length_value(lycon, LXB_CSS_PROPERTY_BORDER_TOP_LEFT_RADIUS, &top_left_radius->vertical) : tl_h;

        if (specificity > span->bound->border->top_radius.left_specificity) {
            span->bound->border->top_radius.left = (tl_h + tl_v) / 2;
            span->bound->border->top_radius.left_specificity = specificity;
        }
        break;

    case LXB_CSS_PROPERTY_BORDER_TOP_RIGHT_RADIUS:
        const lxb_css_property_border_top_right_radius_t *top_right_radius = declr->u.border_top_right_radius;
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        if (!span->bound->border) {
            span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
        }

        int tr_h = resolve_length_value(lycon, LXB_CSS_PROPERTY_BORDER_TOP_RIGHT_RADIUS,
            &top_right_radius->horizontal);
        int tr_v = top_right_radius->vertical.type != LXB_CSS_VALUE__UNDEF ?
            resolve_length_value(lycon, LXB_CSS_PROPERTY_BORDER_TOP_RIGHT_RADIUS, &top_right_radius->vertical) : tr_h;

        if (specificity > span->bound->border->top_radius.right_specificity) {
            span->bound->border->top_radius.right = (tr_h + tr_v) / 2;
            span->bound->border->top_radius.right_specificity = specificity;
        }
        break;

    case LXB_CSS_PROPERTY_BORDER_BOTTOM_RIGHT_RADIUS:
        const lxb_css_property_border_bottom_right_radius_t *bottom_right_radius = declr->u.border_bottom_right_radius;
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        if (!span->bound->border) {
            span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
        }

        int br_h = resolve_length_value(lycon, LXB_CSS_PROPERTY_BORDER_BOTTOM_RIGHT_RADIUS,
            &bottom_right_radius->horizontal);
        int br_v = bottom_right_radius->vertical.type != LXB_CSS_VALUE__UNDEF ?
            resolve_length_value(lycon, LXB_CSS_PROPERTY_BORDER_BOTTOM_RIGHT_RADIUS, &bottom_right_radius->vertical) : br_h;

        if (specificity > span->bound->border->bottom_radius.right_specificity) {
            span->bound->border->bottom_radius.right = (br_h + br_v) / 2;
            span->bound->border->bottom_radius.right_specificity = specificity;
        }
        break;

    case LXB_CSS_PROPERTY_BORDER_BOTTOM_LEFT_RADIUS:
        const lxb_css_property_border_bottom_left_radius_t *bottom_left_radius = declr->u.border_bottom_left_radius;
        if (!span->bound) {
            span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
        }
        if (!span->bound->border) {
            span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
        }

        int bl_h = resolve_length_value(lycon, LXB_CSS_PROPERTY_BORDER_BOTTOM_LEFT_RADIUS,
            &bottom_left_radius->horizontal);
        int bl_v = bottom_left_radius->vertical.type != LXB_CSS_VALUE__UNDEF ?
            resolve_length_value(lycon, LXB_CSS_PROPERTY_BORDER_BOTTOM_LEFT_RADIUS, &bottom_left_radius->vertical) : bl_h;

        if (specificity > span->bound->border->bottom_radius.left_specificity) {
            span->bound->border->bottom_radius.left = (bl_h + bl_v) / 2;
            span->bound->border->bottom_radius.left_specificity = specificity;
        }
        break;
*/
    case LXB_CSS_PROPERTY_FONT_FAMILY: {
        const lxb_css_property_font_family_t *font_family = declr->u.font_family;
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        if (font_family->first && font_family->first->u.str.length > 0) {
            span->font->family = (char*)alloc_prop(lycon, font_family->first->u.str.length + 1);
            strncpy(span->font->family, (char*)font_family->first->u.str.data, font_family->first->u.str.length);
            span->font->family[font_family->first->u.str.length] = '\0';
            log_debug("font family property: %s", span->font->family);
        }
        break;
    }
    case LXB_CSS_PROPERTY_FONT_SIZE: {
        log_debug("before resolving font size");
        resolve_font_size(lycon, declr);
        log_debug("after resolving font size");
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_size = lycon->font.current_font_size;
        assert(span->font->font_size >= 0);
        break;
    }
    case LXB_CSS_PROPERTY_FONT_STYLE: {
        const lxb_css_property_font_style_t *font_style = declr->u.font_style;
        log_debug("font style property: %d", font_style->type);
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_style = font_style->type;
        break;
    }
    case LXB_CSS_PROPERTY_FONT_WEIGHT: {
        const lxb_css_property_font_weight_t *font_weight = declr->u.font_weight;
        log_debug("font weight property: %d", font_weight->type);
        if (!span->font) { span->font = alloc_font_prop(lycon); }
        span->font->font_weight = font_weight->type;
        break;
    }
    case LXB_CSS_PROPERTY_TEXT_DECORATION: {
        const lxb_css_property_text_decoration_t *text_decoration = declr->u.text_decoration;
        log_debug("text decoration property: %d", text_decoration->line.type);
        if (!span->font) {
            span->font = (FontProp*)alloc_prop(lycon, sizeof(FontProp));
        }
        span->font->text_deco = text_decoration->line.type;
        break;
    }
    case LXB_CSS_PROPERTY_TEXT_ALIGN: {
        if (!block) { break; }
        const lxb_css_property_text_align_t *text_align = declr->u.text_align;
        log_debug("text align property: %d", text_align->type);
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->text_align = text_align->type;
        break;
    }
    case LXB_CSS_PROPERTY_WIDTH: {
        const lxb_css_property_width_t *width = declr->u.width;
        float value = resolve_length_value(lycon, LXB_CSS_PROPERTY_WIDTH, width);
        lycon->block.given_width = max(value, 0);  // width cannot be negative
        log_debug("width property: %f, type: %d", lycon->block.given_width, width->type);
        // Store the raw width value for box-sizing calculations
        if (block) {
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
            block->blk->given_width = lycon->block.given_width;
            block->blk->given_width_type = width->type;
        }
        break;
    }
    case LXB_CSS_PROPERTY_HEIGHT: {
        const lxb_css_property_height_t *height = declr->u.height;
        float value = resolve_length_value(lycon, LXB_CSS_PROPERTY_HEIGHT, height);
        lycon->block.given_height = isnan(value) ? value : max(value, 0);  // height cannot be negative
        log_debug("height property: %d", lycon->block.given_height);
        // Store the raw height value for box-sizing calculations
        if (block) {
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
            block->blk->given_height = lycon->block.given_height;
        }
        break;
    }
    case LXB_CSS_PROPERTY_BOX_SIZING: {
        if (!block) { break; }
        const lxb_css_property_box_sizing_t *box_sizing = declr->u.box_sizing;
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->box_sizing = box_sizing->type;
        log_debug("box-sizing property: %d (border-box=%d)", box_sizing->type, LXB_CSS_VALUE_BORDER_BOX);
        break;
    }
    case LXB_CSS_PROPERTY_MIN_WIDTH: {
        if (!block) { break; }
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->given_min_width = resolve_length_value(lycon, LXB_CSS_PROPERTY_MIN_WIDTH, declr->u.width);
        break;
    }
    case LXB_CSS_PROPERTY_MAX_WIDTH: {
        if (!block) { break; }
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->given_max_width = resolve_length_value(lycon, LXB_CSS_PROPERTY_MAX_WIDTH, declr->u.width);
        log_debug("max width property: %d, val %f", declr->u.width->type, block->blk->given_max_width);
        break;
    }
    case LXB_CSS_PROPERTY_MIN_HEIGHT: {
        if (!block) { break; }
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->given_min_height = resolve_length_value(lycon, LXB_CSS_PROPERTY_MIN_HEIGHT, declr->u.height);
        break;
    }
    case LXB_CSS_PROPERTY_MAX_HEIGHT: {
        if (!block) { break; }
        if (!block->blk) { block->blk = alloc_block_prop(lycon); }
        block->blk->given_max_height = resolve_length_value(lycon, LXB_CSS_PROPERTY_MAX_HEIGHT, declr->u.height);
        break;
    }
    case LXB_CSS_PROPERTY_OVERFLOW_X: {
        if (!block) { break; }
        const lxb_css_property_overflow_x_t *overflow_x = declr->u.overflow_x;
        log_debug("overflow x property: %d", overflow_x->type);
        if (!block->scroller) {
            block->scroller = (ScrollProp*)alloc_prop(lycon, sizeof(ScrollProp));
        }
        block->scroller->overflow_x = overflow_x->type;
        break;
    }
    case LXB_CSS_PROPERTY_OVERFLOW_Y: {
        if (!block) { break; }
        const lxb_css_property_overflow_y_t *overflow = declr->u.overflow_y;
        log_debug("overflow property: %d", overflow->type);
        if (!block->scroller) {
            block->scroller = (ScrollProp*)alloc_prop(lycon, sizeof(ScrollProp));
        }
        block->scroller->overflow_y = overflow->type;
        break;
    }
    case LXB_CSS_PROPERTY_POSITION: {
        log_debug("DEBUG: Entering LXB_CSS_PROPERTY_POSITION case!");
        if (!block) {
            log_debug("DEBUG: No block available for position property");
            break;
        }
        const lxb_css_property_position_t *position = declr->u.position;
        log_debug("DEBUG: CSS position property parsed: value=%d (STATIC=%d, RELATIVE=%d, ABSOLUTE=%d, FIXED=%d)",
                  position->type, LXB_CSS_VALUE_STATIC, LXB_CSS_VALUE_RELATIVE, LXB_CSS_VALUE_ABSOLUTE, LXB_CSS_VALUE_FIXED);
        if (!block->position) {
            block->position = alloc_position_prop(lycon);
            log_debug("DEBUG: Allocated new PositionProp for block");
        }
        block->position->position = position->type;
        log_debug("DEBUG: Stored position value %d in block->position->position", position->type);
        break;
    }
    case LXB_CSS_PROPERTY_TOP: {
        if (!block) { break; }
        const lxb_css_value_length_percentage_t *top = declr->u.top;
        if (!block->position) {
            block->position = alloc_position_prop(lycon);
        }
        block->position->top = resolve_length_value(lycon, LXB_CSS_PROPERTY_TOP, top);
        block->position->has_top = true;
        log_debug("top offset: %dpx", block->position->top);
        break;
    }
    case LXB_CSS_PROPERTY_RIGHT: {
        if (!block) { break; }
        const lxb_css_value_length_percentage_t *right = declr->u.right;
        if (!block->position) {
            block->position = alloc_position_prop(lycon);
        }
        block->position->right = resolve_length_value(lycon, LXB_CSS_PROPERTY_RIGHT, right);
        block->position->has_right = true;
        log_debug("right offset: %dpx", block->position->right);
        break;
    }
    case LXB_CSS_PROPERTY_BOTTOM: {
        if (!block) { break; }
        const lxb_css_value_length_percentage_t *bottom = declr->u.bottom;
        if (!block->position) {
            block->position = alloc_position_prop(lycon);
        }
        block->position->bottom = resolve_length_value(lycon, LXB_CSS_PROPERTY_BOTTOM, bottom);
        block->position->has_bottom = true;
        log_debug("bottom offset: %dpx", block->position->bottom);
        break;
    }
    case LXB_CSS_PROPERTY_LEFT: {
        if (!block) { break; }
        const lxb_css_value_length_percentage_t *left = declr->u.left;
        if (!block->position) {
            block->position = alloc_position_prop(lycon);
        }
        block->position->left = resolve_length_value(lycon, LXB_CSS_PROPERTY_LEFT, left);
        block->position->has_left = true;
        log_debug("left offset: %dpx", block->position->left);
        break;
    }
    case LXB_CSS_PROPERTY_CLEAR: {
        if (!block) { break; }
        const lxb_css_property_clear_t *clear = declr->u.clear;
        log_debug("CSS clear property parsed: value=%d (LEFT=47, RIGHT=48, BOTH=372, NONE=%d)",
               clear->type, LXB_CSS_VALUE_NONE);
        if (!block->position) {
            block->position = alloc_position_prop(lycon);
        }
        block->position->clear = clear->type;
        log_debug("Stored clear value %d in block->position->clear", clear->type);
        break;
    }
    case LXB_CSS_PROPERTY_Z_INDEX: {
        if (!block) { break; }
        const lxb_css_property_z_index_t *z_index = declr->u.z_index;
        if (!block->position) {
            block->position = alloc_position_prop(lycon);
        }
        if (z_index->type == LXB_CSS_VALUE__NUMBER) {
            block->position->z_index = (int)z_index->integer.num;
        }
        log_debug("z-index: %d", block->position->z_index);
        break;
    }
    case LXB_CSS_PROPERTY_FLOAT: {
        if (!block) { break; }
        const lxb_css_property_float_t *float_prop = declr->u.floatp;
        if (!block->position) {
            block->position = alloc_position_prop(lycon);
        }
        block->position->float_prop = float_prop->type;
        log_debug("float property: %d", float_prop->type);
        break;
    }
    case LXB_CSS_PROPERTY_FLEX_DIRECTION: {
        if (!block) { break; }
        const lxb_css_property_flex_direction_t *flex_direction = declr->u.flex_direction;
        alloc_flex_prop(lycon, block);
        // CRITICAL FIX: Now that enums align with Lexbor constants, we can use them directly
        block->embed->flex->direction = (FlexDirection)flex_direction->type;
        break;
    }
    case LXB_CSS_PROPERTY_FLEX_WRAP: {
        if (!block) { break; }
        const lxb_css_property_flex_wrap_t *flex_wrap = declr->u.flex_wrap;
        alloc_flex_prop(lycon, block);
        // CRITICAL FIX: Now that enums align with Lexbor constants, use directly
        log_debug("DEBUG: Setting flex-wrap to %d", flex_wrap->type);
        block->embed->flex->wrap = (FlexWrap)flex_wrap->type;
        break;
    }
    case LXB_CSS_PROPERTY_FLEX_FLOW: {
        if (!block) { break; }
        const lxb_css_property_flex_flow_t *flex_flow = declr->u.flex_flow;
        alloc_flex_prop(lycon, block);
        // CRITICAL FIX: Now that enums align with Lexbor constants, use directly
        if (flex_flow->type_direction != LXB_CSS_VALUE__UNDEF) {
            block->embed->flex->direction = (FlexDirection)flex_flow->type_direction;
        }
        if (flex_flow->wrap != LXB_CSS_VALUE__UNDEF) {
            block->embed->flex->wrap = (FlexWrap)flex_flow->wrap;
        }
        break;
    }
    case LXB_CSS_PROPERTY_JUSTIFY_CONTENT: {
        if (!block) { break; }
        const lxb_css_property_justify_content_t *justify_content = declr->u.justify_content;
        log_debug("DEBUG: JUSTIFY_CONTENT parsed - type=%d", justify_content->type);
        alloc_flex_prop(lycon, block);

        // Handle space-evenly specially since lexbor might not support it
        if (justify_content->type == LXB_CSS_VALUE_SPACE_EVENLY) {
            log_debug("Setting justify-content to SPACE_EVENLY");
            block->embed->flex->justify = LXB_CSS_VALUE_SPACE_EVENLY;
        } else {
            // CRITICAL FIX: Now that enums align with Lexbor constants, use directly
            block->embed->flex->justify = (JustifyContent)justify_content->type;
            log_debug("Set justify-content to %d", justify_content->type);
        }
        break;
    }
    case LXB_CSS_PROPERTY_ALIGN_ITEMS: {
        if (!block) { break; }
        const lxb_css_property_align_items_t *align_items = declr->u.align_items;
        alloc_flex_prop(lycon, block);
        // CRITICAL FIX: Now that enums align with Lexbor constants, use directly
        block->embed->flex->align_items = (AlignType)align_items->type;
        log_debug("CSS align-items parsed: type=%d", align_items->type);
        break;
    }
    case LXB_CSS_PROPERTY_ALIGN_CONTENT: {
        if (!block) { break; }
        const lxb_css_property_align_content_t *align_content = declr->u.align_content;
        alloc_flex_prop(lycon, block);
        // CRITICAL FIX: Now that enums align with Lexbor constants, use directly
        block->embed->flex->align_content = (AlignType)align_content->type;
        break;
    }
    case LXB_CSS_PROPERTY_ALIGN_SELF: {
        const lxb_css_property_align_self_t *align_self = declr->u.align_self;
        // CRITICAL FIX: Now that enums align with Lexbor constants, use directly
        span->align_self = (AlignType)align_self->type;
        break;
    }
    case LXB_CSS_PROPERTY_ORDER: {
        const lxb_css_property_order_t *order = declr->u.order;
        span->order = order->integer.num;
        break;
    }
    case LXB_CSS_PROPERTY_FLEX: {
        const lxb_css_property_flex_t *flex = declr->u.flex;
        // handle flex-grow
        if (flex->grow.type != LXB_CSS_VALUE__UNDEF) {
            span->flex_grow = flex->grow.number.num;
        }
        else {
            span->flex_grow = 1;  // Default for 'flex: auto'
        }
        // handle flex-shrink
        if (flex->shrink.type != LXB_CSS_VALUE__UNDEF) {
            span->flex_shrink = flex->shrink.number.num;
        }
        else {
            span->flex_shrink = 1;  // Default for 'flex: auto'
        }
        // handle flex-basis
        if (flex->basis.type == LXB_CSS_VALUE__LENGTH) {
            span->flex_basis = flex->basis.u.length.num;
            span->flex_basis_is_percent = false;
        } else if (flex->basis.type == LXB_CSS_VALUE__PERCENTAGE) {
            span->flex_basis = flex->basis.u.percentage.num;
            span->flex_basis_is_percent = true;
        } else if (flex->basis.type == LXB_CSS_VALUE_AUTO ) { // || flex->none
            span->flex_basis = -1; // auto
            span->flex_basis_is_percent = false;
        } else {
            span->flex_basis = 0;  // content
            span->flex_basis_is_percent = false;
        }
        break;
    }
    case LXB_CSS_PROPERTY_FLEX_GROW: {
        const lxb_css_property_flex_grow_t *flex_grow = declr->u.flex_grow;
        span->flex_grow = flex_grow->number.num;

        // CRITICAL FIX: If flex-basis is not explicitly set, default to auto (-1)
        // This matches CSS Flexbox specification where flex-basis defaults to auto
        if (span->flex_basis == 0 && !span->flex_basis_is_percent) {
            span->flex_basis = -1; // auto
        }
        break;
    }
    case LXB_CSS_PROPERTY_FLEX_SHRINK: {
        const lxb_css_property_flex_shrink_t *flex_shrink = declr->u.flex_shrink;
        span->flex_shrink = flex_shrink->number.num;
        break;
    }
    case LXB_CSS_PROPERTY_FLEX_BASIS: {
        const lxb_css_property_flex_basis_t *flex_basis = declr->u.flex_basis;
        if (flex_basis->type == LXB_CSS_VALUE__LENGTH) {
            span->flex_basis = flex_basis->u.length.num;
            span->flex_basis_is_percent = false;
        } else if (flex_basis->type == LXB_CSS_VALUE__PERCENTAGE) {
            span->flex_basis = flex_basis->u.percentage.num;
            span->flex_basis_is_percent = true;
        } else if (flex_basis->type == LXB_CSS_VALUE_AUTO) {
            span->flex_basis = -1; // auto
            span->flex_basis_is_percent = false;
        } else {
            span->flex_basis = 0;  // content
            span->flex_basis_is_percent = false;
        }
        break;
    }

    // Enhanced flexbox properties for new implementation
    // Note: aspect-ratio property not available in current lexbor version
    // Will be handled through custom properties when needed

    // Enhanced flexbox properties are handled through existing cases above
    // Additional properties like aspect-ratio will be handled as custom properties

    // CSS Grid Layout Properties
    // Note: Most grid properties are handled as custom properties since lexbor doesn't support them yet
    case LXB_CSS_PROPERTY__CUSTOM: { // properties not supported by Lexbor, return as #custom
        const lxb_css_property__custom_t *custom = declr->u.custom;
        // Handle CSS Grid properties as custom properties until lexbor supports them
        log_debug("Processing custom property: %.*s = %.*s\n",
               (int)custom->name.length, (const char*)custom->name.data,
               (int)custom->value.length, (const char*)custom->value.data);

        // Check if this is x-justify-content with space-evenly value (Lexbor compatibility fallback)
        if (custom->name.length == 17 && strncmp((const char*)custom->name.data, "x-justify-content", 17) == 0) {
            if (custom->value.length == 12 && strncmp((const char*)custom->value.data, "space-evenly", 12) == 0) {
                printf("DEBUG: X_JUSTIFY_CONTENT_WORKAROUND - Applied space-evenly via x-justify-content custom property\n");
                if (block) {
                    alloc_flex_prop(lycon, block);
                    block->embed->flex->justify = LXB_CSS_VALUE_SPACE_EVENLY;
                }
            }
        }

        // Handle aspect-ratio as custom property until lexbor supports it
        if (custom->name.length == 12 && strncmp((const char*)custom->name.data, "aspect-ratio", 12) == 0) {
            // Parse aspect-ratio value (simplified parsing)
            // Format: "width / height" or just "ratio"
            const char* value_str = (const char*)custom->value.data;
            float ratio = 0.0f;

            // Simple parsing - look for number before slash or standalone number
            char* endptr;
            float width = strtof(value_str, &endptr);
            if (endptr != value_str) {
                if (*endptr == '/' && *(endptr + 1)) {
                    float height = strtof(endptr + 1, NULL);
                    if (height > 0) {
                        ratio = width / height;
                    }
                } else {
                    ratio = width; // Single number format
                }
            }

            if (ratio > 0) {
                span->aspect_ratio = ratio;
                log_debug("Set aspect-ratio: %f\n", ratio);
            }
        }

        // Handle justify-content space-evenly as custom property since lexbor doesn't support it
        else if (custom->name.length == 15 && strncmp((const char*)custom->name.data, "justify-content", 15) == 0) {
            const char* value_str = (const char*)custom->value.data;
            log_debug("Custom justify-content property found: '%.*s'", (int)custom->value.length, value_str);

            // Check if the value is "space-evenly"
            if (custom->value.length == 12 && strncmp(value_str, "space-evenly", 12) == 0) {
                // Set justify-content to space-evenly using our custom constant
                if (block) {
                    alloc_flex_prop(lycon, block);
                    block->embed->flex->justify = LXB_CSS_VALUE_SPACE_EVENLY;
                    log_debug("Set justify-content to SPACE_EVENLY (%d)", LXB_CSS_VALUE_SPACE_EVENLY);
                }
            }
        }

        // Handle gap property as custom property until lexbor supports it
        else if (custom->name.length == 3 && strncmp((const char*)custom->name.data, "gap", 3) == 0) {
            const char* value_str = (const char*)custom->value.data;

            // Parse gap value (e.g., "10px", "1em", "20")
            char* endptr;
            float gap_value = strtof(value_str, &endptr);

            if (endptr != value_str && gap_value >= 0) {
                // Convert to pixels based on unit
                int gap_px = 0;
                if (custom->value.length >= 2 && strncmp((const char*)custom->value.data + custom->value.length - 2, "px", 2) == 0) {
                    gap_px = (int)(gap_value * lycon->ui_context->pixel_ratio);
                } else if (custom->value.length >= 2 && strncmp((const char*)custom->value.data + custom->value.length - 2, "em", 2) == 0) {
                    gap_px = (int)(gap_value * lycon->font.current_font_size * lycon->ui_context->pixel_ratio);
                } else {
                    gap_px = (int)(gap_value * lycon->ui_context->pixel_ratio); // Assume pixels if no unit
                }

                // Set gap on appropriate container type
                if (block) {
                    if (!block->embed) {
                        block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
                    }
                    // Check if this is a grid container
                    if (!block->embed && block->embed->grid) {
                        log_debug("Setting gap on grid container: %dpx", gap_px);
                        block->embed->grid->row_gap = gap_px;
                        block->embed->grid->column_gap = gap_px;
                        log_debug("Set grid gap: %dpx (from %s)", gap_px, value_str);
                    } else {
                        // Fallback to flex container for backward compatibility
                        alloc_flex_prop(lycon, block);
                        block->embed->flex->row_gap = gap_px;
                        block->embed->flex->column_gap = gap_px;
                        log_debug("Set flex gap: %dpx (from %s)", gap_px, value_str);
                    }
                }
            }
        }

        // grid-template-rows
        else if (custom->name.length == 18 && strncmp((const char*)custom->name.data, "grid-template-rows", 18) == 0) {
            log_debug("grid-template-rows matched! block=%p", block);
            if (block) {
                log_debug("Inside grid-template-rows block processing");
                alloc_grid_prop(lycon, block);

                // Enhanced parsing for advanced features
                char template_str[256];
                int len = min((int)custom->value.length, 255);
                strncpy(template_str, (const char*)custom->value.data, len);
                template_str[len] = '\0';

                // Parse the template string (handles minmax, repeat, etc.)
                log_debug("About to parse grid-template-rows: '%s'", template_str);
                if (block->embed->grid->grid_template_rows) {
                    log_debug("Calling parse_grid_template_tracks for rows");
                    parse_grid_template_tracks(block->embed->grid->grid_template_rows, template_str);
                } else {
                    log_debug("grid_template_rows is NULL!");
                }
                log_debug("Set grid-template-rows: %s", template_str);
            }
        }

        // grid-template-columns
        else if (custom->name.length == 21 && strncmp((const char*)custom->name.data, "grid-template-columns", 21) == 0) {
            log_debug("grid-template-columns matched! block=%p", block);
            if (block) {
                log_debug("Inside grid-template-columns block processing");
                alloc_grid_prop(lycon, block);

                // Enhanced parsing for advanced features
                char template_str[256];
                int len = min((int)custom->value.length, 255);
                strncpy(template_str, (const char*)custom->value.data, len);
                template_str[len] = '\0';

                // Parse the template string (handles minmax, repeat, etc.)
                log_debug("About to parse grid-template-columns: '%s'", template_str);
                if (block->embed->grid->grid_template_columns) {
                    log_debug("Calling parse_grid_template_tracks for columns");
                    parse_grid_template_tracks(block->embed->grid->grid_template_columns, template_str);
                } else {
                    log_debug("grid_template_columns is NULL!");
                }
                log_debug("Set grid-template-columns: %s", template_str);
            }
        }

        // grid-template-areas
        else if (custom->name.length == 19 && strncmp((const char*)custom->name.data, "grid-template-areas", 19) == 0) {
            log_debug("grid-template-areas matched! block=%p", block);
            if (block) {
                log_debug("Inside grid-template-areas block processing");
                alloc_grid_prop(lycon, block);
                log_debug("Grid container allocated, grid_container=%p", block->embed->grid);

                // Parse grid template areas
                char areas_str[256];
                int len = min((int)custom->value.length, 255);
                strncpy(areas_str, (const char*)custom->value.data, len);
                areas_str[len] = '\0';
                log_debug("About to parse grid-template-areas: '%s'", areas_str);
                parse_grid_template_areas(block->embed->grid, areas_str);
                log_debug("Finished parsing grid-template-areas");
                log_debug("Set grid-template-areas: %s", areas_str);
            }
        }

        // grid-row-start, grid-row-end, grid-column-start, grid-column-end
        else if (custom->name.length == 14 && strncmp((const char*)custom->name.data, "grid-row-start", 14) == 0) {
            char* endptr;
            int line_value = strtol((const char*)custom->value.data, &endptr, 10);
            if (endptr != (const char*)custom->value.data) {
                span->grid_row_start = line_value;
                span->has_explicit_grid_row_start = true;
                log_debug("Set grid-row-start: %d\n", line_value);
            }
        }

        else if (custom->name.length == 12 && strncmp((const char*)custom->name.data, "grid-row-end", 12) == 0) {
            char* endptr;
            int line_value = strtol((const char*)custom->value.data, &endptr, 10);
            if (endptr != (const char*)custom->value.data) {
                span->grid_row_end = line_value;
                span->has_explicit_grid_row_end = true;
                log_debug("Set grid-row-end: %d\n", line_value);
            }
        }

        else if (custom->name.length == 17 && strncmp((const char*)custom->name.data, "grid-column-start", 17) == 0) {
            char* endptr;
            int line_value = strtol((const char*)custom->value.data, &endptr, 10);
            if (endptr != (const char*)custom->value.data) {
                span->grid_column_start = line_value;
                span->has_explicit_grid_column_start = true;
                log_debug("Set grid-column-start: %d\n", line_value);
            }
        }

        else if (custom->name.length == 15 && strncmp((const char*)custom->name.data, "grid-column-end", 15) == 0) {
            char* endptr;
            int line_value = strtol((const char*)custom->value.data, &endptr, 10);
            if (endptr != (const char*)custom->value.data) {
                span->grid_column_end = line_value;
                span->has_explicit_grid_column_end = true;
                log_debug("Set grid-column-end: %d\n", line_value);
            }
        }

        // grid-area
        else if (custom->name.length == 9 && strncmp((const char*)custom->name.data, "grid-area", 9) == 0) {
            // Copy the grid area name
            int len = min((int)custom->value.length, 63); // Reasonable limit
            if (span->grid_area) {
                free(span->grid_area); // Free existing area name
            }
            span->grid_area = (char*)malloc(len + 1);
            strncpy(span->grid_area, (const char*)custom->value.data, len);
            span->grid_area[len] = '\0';
            log_debug("Set grid-area: %s\n", span->grid_area);
        }

        // row-gap and column-gap for grid
        else if (custom->name.length == 7 && strncmp((const char*)custom->name.data, "row-gap", 7) == 0) {
            const char* value_str = (const char*)custom->value.data;
            char* endptr;
            float gap_value = strtof(value_str, &endptr);

            if (endptr != value_str && gap_value >= 0) {
                int gap_px = (int)(gap_value * lycon->ui_context->pixel_ratio);
                if (block) {
                    alloc_grid_prop(lycon, block);
                    block->embed->grid->row_gap = gap_px;
                    log_debug("Set row-gap: %dpx\n", gap_px);
                }
            }
        }

        else if (custom->name.length == 10 && strncmp((const char*)custom->name.data, "column-gap", 10) == 0) {
            const char* value_str = (const char*)custom->value.data;
            char* endptr;
            float gap_value = strtof(value_str, &endptr);

            if (endptr != value_str && gap_value >= 0) {
                int gap_px = (int)(gap_value * lycon->ui_context->pixel_ratio);
                if (block) {
                    alloc_grid_prop(lycon, block);
                    block->embed->grid->column_gap = gap_px;
                    log_debug("Set column-gap: %dpx\n", gap_px);
                }
            }
        }

        // grid-auto-flow
        else if (custom->name.length == 14 && strncmp((const char*)custom->name.data, "grid-auto-flow", 14) == 0) {
            if (block) {
                alloc_grid_prop(lycon, block);
                const char* value_str = (const char*)custom->value.data;
                // Parse grid-auto-flow values (can be "row", "column", "row dense", "column dense")
                if (strstr(value_str, "row")) {
                    block->embed->grid->grid_auto_flow = LXB_CSS_VALUE_ROW;
                } else if (strstr(value_str, "column")) {
                    block->embed->grid->grid_auto_flow = LXB_CSS_VALUE_COLUMN;
                }

                // Check for dense packing
                if (strstr(value_str, "dense")) {
                    block->embed->grid->is_dense_packing = true;
                    log_debug("Enabled dense packing for grid auto-flow\n");
                }

                log_debug("Set grid-auto-flow: %.*s\n", (int)custom->value.length, custom->value.data);
            }
        }

        else if (custom->name.length == 12 && strncmp((const char*)custom->name.data, "table-layout", 12) == 0) {
            ViewTable* table = lycon->view->type == RDT_VIEW_TABLE ? (ViewTable*)lycon->view : NULL;
            if (table) {
                const char* value_str = (const char*)custom->value.data;
                // use strncmp since value_str is not null-terminated
                if (custom->value.length == 5 && strncmp(value_str, "fixed", 5) == 0) {
                    table->table_layout = ViewTable::TABLE_LAYOUT_FIXED;
                    log_debug("Detected table-layout: fixed (inline)");
                }
                else if (custom->value.length == 4 && strncmp(value_str, "auto", 4) == 0) {
                    table->table_layout = ViewTable::TABLE_LAYOUT_AUTO;
                    log_debug("Detected table-layout: auto (inline)");
                }
            }
        }
        else if (custom->name.length == 15 && strncmp((const char*)custom->name.data, "border-collapse", 15) == 0) {
            ViewTable* table = lycon->view->type == RDT_VIEW_TABLE ? (ViewTable*)lycon->view : NULL;
            if (table) {
                const char* value_str = (const char*)custom->value.data;
                // Use strncmp since value_str is not null-terminated
                if (custom->value.length == 8 && strncmp(value_str, "collapse", 8) == 0) {
                    table->border_collapse = true;
                    log_debug("Detected border-collapse: collapse (inline)");
                }
                else if (custom->value.length == 8 && strncmp(value_str, "separate", 8) == 0) {
                    table->border_collapse = false;
                    log_debug("Detected border-collapse: separate (inline)");
                }
            }
        }
        else if (custom->name.length == 14 && strncmp((const char*)custom->name.data, "border-spacing", 14) == 0) {
            printf("DEBUG: BORDER_SPACING condition matched!\n");
            const char* value_str = (const char*)custom->value.data;
            printf("DEBUG: Found border-spacing property: '%s', view type: %d\n", value_str, lycon->view->type);
            ViewTable* table = NULL;
            if (lycon->view->type == RDT_VIEW_TABLE) {
                table = (ViewTable*)lycon->view;
            } else if (lycon->view->type == RDT_VIEW_TABLE_ROW_GROUP && lycon->view->parent && lycon->view->parent->type == RDT_VIEW_TABLE) {
                table = (ViewTable*)lycon->view->parent;
            }
            printf("DEBUG: Table pointer: %p\n", table);
            if (table) {
                printf("DEBUG: Parsing border-spacing values!\n");
                const char* q = value_str;
                // Skip any leading whitespace
                while (*q == ' ') q++;

                // Parse first value (horizontal spacing)
                int h = atoi(q);
                while (*q && *q != ' ' && *q != ';' && *q != 'p') q++;
                if (*q == 'p' && *(q+1) == 'x') q += 2;

                // Parse second value (vertical spacing) if present
                int v = -1;
                while (*q == ' ') q++;
                if (*q && *q != ';' && *q != '\0') {
                    v = atoi(q);
                }

                // Apply CSS defaults: if only one value, use it for both dimensions
                if (h < 0) h = 0;
                if (v < 0) v = h;

                table->border_spacing_h = h;
                table->border_spacing_v = v;
                printf("DEBUG: Set border-spacing: %dpx %dpx\n", h, v);
                log_debug("Detected border-spacing: %dpx %dpx (stylesheet)", h, v);
            }
        }

        break;
    }
    default:
        printf("DEBUG: UNKNOWN_PROPERTY - type=%ld, name=%s\n", declr->type, (const char*)data->name);
        log_debug("unknown property: %d", declr->type);
        break;
    }

    return LXB_STATUS_OK;
}

AlignType resolve_align_type(PropValue value) {
    switch (value) {
        case LXB_CSS_VALUE_FLEX_START:
        case LXB_CSS_VALUE_START:
            return ALIGN_START;
        case LXB_CSS_VALUE_FLEX_END:
        case LXB_CSS_VALUE_END:
            return ALIGN_END;
        case LXB_CSS_VALUE_CENTER:
            return ALIGN_CENTER;
        case LXB_CSS_VALUE_BASELINE:
            return ALIGN_BASELINE;
        case LXB_CSS_VALUE_STRETCH:
            return ALIGN_STRETCH;
        case LXB_CSS_VALUE_SPACE_BETWEEN:
            return ALIGN_SPACE_BETWEEN;
        case LXB_CSS_VALUE_SPACE_AROUND:
            return ALIGN_SPACE_AROUND;
        // case LXB_CSS_VALUE_SPACE_EVENLY:
        //     return ALIGN_SPACE_EVENLY;
        default:
            return ALIGN_START;
    }
}

int resolve_justify_content(PropValue value) {
    // CRITICAL FIX: Return Lexbor constants directly instead of old enum values
    // This eliminates the enum conversion mismatch that was breaking justify-content
    switch (value) {
        case LXB_CSS_VALUE_FLEX_START:
        case LXB_CSS_VALUE_START:
            return LXB_CSS_VALUE_FLEX_START;
        case LXB_CSS_VALUE_FLEX_END:
        case LXB_CSS_VALUE_END:
            return LXB_CSS_VALUE_FLEX_END;
        case LXB_CSS_VALUE_CENTER:
            return LXB_CSS_VALUE_CENTER;
        case LXB_CSS_VALUE_SPACE_BETWEEN:
            return LXB_CSS_VALUE_SPACE_BETWEEN;
        case LXB_CSS_VALUE_SPACE_AROUND:
            return LXB_CSS_VALUE_SPACE_AROUND;
        case LXB_CSS_VALUE_SPACE_EVENLY:
            return LXB_CSS_VALUE_SPACE_EVENLY;
        default:
            return LXB_CSS_VALUE_FLEX_START;
    }
}
