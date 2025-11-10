#include "layout.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"

// ============================================================================
// Value Conversion Functions
// ============================================================================

float convert_lambda_length_to_px(const CssValue* value, LayoutContext* lycon, CssPropertyId prop_id) {
    if (!value) return 0.0f;

    switch (value->type) {
        case CSS_VALUE_TYPE_LENGTH: {
            float num = value->data.length.value;

            switch (value->data.length.unit) {
                case CSS_UNIT_PX:
                    return num;

                case CSS_UNIT_EM:
                    // relative to current font size
                    if (lycon) {
                        return num * lycon->font.current_font_size;
                    }
                    return num * 16.0f; // default font size

                case CSS_UNIT_REM:
                    // relative to root font size
                    return num * 16.0f; // TODO: get from root element

                case CSS_UNIT_PT:
                    return num * (96.0f / 72.0f); // 1pt = 1/72 inch, 96dpi

                case CSS_UNIT_PC:
                    return num * 16.0f; // 1pc = 12pt = 16px

                case CSS_UNIT_IN:
                    return num * 96.0f; // 96dpi

                case CSS_UNIT_CM:
                    return num * 37.795f; // 1cm = 37.795px at 96dpi

                case CSS_UNIT_MM:
                    return num * 3.7795f; // 1mm = 3.7795px at 96dpi

                case CSS_UNIT_VW:
                    // viewport width percentage
                    if (lycon && lycon->width > 0) {
                        return (num / 100.0f) * lycon->width;
                    }
                    return 0.0f;

                case CSS_UNIT_VH:
                    // viewport height percentage
                    if (lycon && lycon->height > 0) {
                        return (num / 100.0f) * lycon->height;
                    }
                    return 0.0f;

                default:
                    return num; // assume pixels for unknown units
            }
        }

        case CSS_VALUE_TYPE_PERCENTAGE: {
            // percentage resolution depends on property context
            // for now, return raw percentage (needs parent context)
            return (float)value->data.percentage.value;
        }

        case CSS_VALUE_TYPE_NUMBER: {
            // unitless number, treat as pixels for most properties
            return (float)value->data.number.value;
        }

        default:
            return 0.0f;
    }
}

Color resolve_color_value(const CssValue* value) {
    Color result;
    result.r = 0;
    result.g = 0;
    result.b = 0;
    result.a = 255; // default black, opaque

    if (!value) return result;
    switch (value->type) {
        case CSS_VALUE_TYPE_COLOR: {
            // Access color data from CssValue anonymous struct
            switch (value->data.color.type) {
                case CSS_COLOR_RGB:
                    result = value->data.color.data.color;
                    break;
                case CSS_COLOR_HSL:
                    // TODO: convert HSL to RGB
                    // for now, leave as black
                    break;
                default:
                    break;
            }
            break;
        }
        case CSS_VALUE_TYPE_KEYWORD: {
            // map color keyword to RGB
            result = color_name_to_rgb(value->data.keyword);
        }
        default:
            break;
    }
    return result;
}

// ============================================================================
// Keyword Mapping Functions
// ============================================================================

// CSS4 has a total of 148 colors
Color color_name_to_rgb(CssEnum color_name) {
    uint32_t c;
    switch (color_name) {
        case CSS_VALUE_ALICEBLUE: c = 0xF0F8FF;  break;
        case CSS_VALUE_ANTIQUEWHITE: c = 0xFAEBD7;  break;
        case CSS_VALUE_AQUA: c = 0x00FFFF;  break;
        case CSS_VALUE_AQUAMARINE: c = 0x7FFFD4;  break;
        case CSS_VALUE_AZURE: c = 0xF0FFFF;  break;
        case CSS_VALUE_BEIGE: c = 0xF5F5DC;  break;
        case CSS_VALUE_BISQUE: c = 0xFFE4C4;  break;
        case CSS_VALUE_BLACK: c = 0x000000;  break;
        case CSS_VALUE_BLANCHEDALMOND: c = 0xFFEBCD;  break;
        case CSS_VALUE_BLUE: c = 0x0000FF;  break;
        case CSS_VALUE_BLUEVIOLET: c = 0x8A2BE2;  break;
        case CSS_VALUE_BROWN: c = 0xA52A2A;  break;
        case CSS_VALUE_BURLYWOOD: c = 0xDEB887;  break;
        case CSS_VALUE_CADETBLUE: c = 0x5F9EA0;  break;
        case CSS_VALUE_CHARTREUSE: c = 0x7FFF00;  break;
        case CSS_VALUE_CHOCOLATE: c = 0xD2691E;  break;
        case CSS_VALUE_CORAL: c = 0xFF7F50;  break;
        case CSS_VALUE_CORNFLOWERBLUE: c = 0x6495ED;  break;
        case CSS_VALUE_CORNSILK: c = 0xFFF8DC;  break;
        case CSS_VALUE_CRIMSON: c = 0xDC143C;  break;
        case CSS_VALUE_CYAN: c = 0x00FFFF;  break;
        case CSS_VALUE_DARKBLUE: c = 0x00008B;  break;
        case CSS_VALUE_DARKCYAN: c = 0x008B8B;  break;
        case CSS_VALUE_DARKGOLDENROD: c = 0xB8860B;  break;
        case CSS_VALUE_DARKGRAY: c = 0xA9A9A9;  break;
        case CSS_VALUE_DARKGREEN: c = 0x006400;  break;
        case CSS_VALUE_DARKGREY: c = 0xA9A9A9;  break;
        case CSS_VALUE_DARKKHAKI: c = 0xBDB76B;  break;
        case CSS_VALUE_DARKMAGENTA: c = 0x8B008B;  break;
        case CSS_VALUE_DARKOLIVEGREEN: c = 0x556B2F;  break;
        case CSS_VALUE_DARKORANGE: c = 0xFF8C00;  break;
        case CSS_VALUE_DARKORCHID: c = 0x9932CC;  break;
        case CSS_VALUE_DARKRED: c = 0x8B0000;  break;
        case CSS_VALUE_DARKSALMON: c = 0xE9967A;  break;
        case CSS_VALUE_DARKSEAGREEN: c = 0x8FBC8F;  break;
        case CSS_VALUE_DARKSLATEBLUE: c = 0x483D8B;  break;
        case CSS_VALUE_DARKSLATEGRAY: c = 0x2F4F4F;  break;
        case CSS_VALUE_DARKSLATEGREY: c = 0x2F4F4F;  break;
        case CSS_VALUE_DARKTURQUOISE: c = 0x00CED1;  break;
        case CSS_VALUE_DARKVIOLET: c = 0x9400D3;  break;
        case CSS_VALUE_DEEPPINK: c = 0xFF1493;  break;
        case CSS_VALUE_DEEPSKYBLUE: c = 0x00BFFF;  break;
        case CSS_VALUE_DIMGRAY: c = 0x696969;  break;
        case CSS_VALUE_DIMGREY: c = 0x696969;  break;
        case CSS_VALUE_DODGERBLUE: c = 0x1E90FF;  break;
        case CSS_VALUE_FIREBRICK: c = 0xB22222;  break;
        case CSS_VALUE_FLORALWHITE: c = 0xFFFAF0;  break;
        case CSS_VALUE_FORESTGREEN: c = 0x228B22;  break;
        case CSS_VALUE_FUCHSIA: c = 0xFF00FF;  break;
        case CSS_VALUE_GAINSBORO: c = 0xDCDCDC;  break;
        case CSS_VALUE_GHOSTWHITE: c = 0xF8F8FF;  break;
        case CSS_VALUE_GOLD: c = 0xFFD700;  break;
        case CSS_VALUE_GOLDENROD: c = 0xDAA520;  break;
        case CSS_VALUE_GRAY: c = 0x808080;  break;
        case CSS_VALUE_GREEN: c = 0x008000;  break;
        case CSS_VALUE_GREENYELLOW: c = 0xADFF2F;  break;
        case CSS_VALUE_GREY: c = 0x808080;  break;
        case CSS_VALUE_HONEYDEW: c = 0xF0FFF0;  break;
        case CSS_VALUE_HOTPINK: c = 0xFF69B4;  break;
        case CSS_VALUE_INDIANRED: c = 0xCD5C5C;  break;
        case CSS_VALUE_INDIGO: c = 0x4B0082;  break;
        case CSS_VALUE_IVORY: c = 0xFFFFF0;  break;
        case CSS_VALUE_KHAKI: c = 0xF0E68C;  break;
        case CSS_VALUE_LAVENDER: c = 0xE6E6FA;  break;
        case CSS_VALUE_LAVENDERBLUSH: c = 0xFFF0F5;  break;
        case CSS_VALUE_LAWNGREEN: c = 0x7CFC00;  break;
        case CSS_VALUE_LEMONCHIFFON: c = 0xFFFACD;  break;
        case CSS_VALUE_LIGHTBLUE: c = 0xADD8E6;  break;
        case CSS_VALUE_LIGHTCORAL: c = 0xF08080;  break;
        case CSS_VALUE_LIGHTCYAN: c = 0xE0FFFF;  break;
        case CSS_VALUE_LIGHTGOLDENRODYELLOW: c = 0xFAFAD2;  break;
        case CSS_VALUE_LIGHTGRAY: c = 0xD3D3D3;  break;
        case CSS_VALUE_LIGHTGREEN: c = 0x90EE90;  break;
        case CSS_VALUE_LIGHTGREY: c = 0xD3D3D3;  break;
        case CSS_VALUE_LIGHTPINK: c = 0xFFB6C1;  break;
        case CSS_VALUE_LIGHTSALMON: c = 0xFFA07A;  break;
        case CSS_VALUE_LIGHTSEAGREEN: c = 0x20B2AA;  break;
        case CSS_VALUE_LIGHTSKYBLUE: c = 0x87CEFA;  break;
        case CSS_VALUE_LIGHTSLATEGRAY: c = 0x778899;  break;
        case CSS_VALUE_LIGHTSLATEGREY: c = 0x778899;  break;
        case CSS_VALUE_LIGHTSTEELBLUE: c = 0xB0C4DE;  break;
        case CSS_VALUE_LIGHTYELLOW: c = 0xFFFFE0;  break;
        case CSS_VALUE_LIME: c = 0x00FF00;  break;
        case CSS_VALUE_LIMEGREEN: c = 0x32CD32;  break;
        case CSS_VALUE_LINEN: c = 0xFAF0E6;  break;
        case CSS_VALUE_MAGENTA: c = 0xFF00FF;  break;
        case CSS_VALUE_MAROON: c = 0x800000;  break;
        case CSS_VALUE_MEDIUMAQUAMARINE: c = 0x66CDAA;  break;
        case CSS_VALUE_MEDIUMBLUE: c = 0x0000CD;  break;
        case CSS_VALUE_MEDIUMORCHID: c = 0xBA55D3;  break;
        case CSS_VALUE_MEDIUMPURPLE: c = 0x9370DB;  break;
        case CSS_VALUE_MEDIUMSEAGREEN: c = 0x3CB371;  break;
        case CSS_VALUE_MEDIUMSLATEBLUE: c = 0x7B68EE;  break;
        case CSS_VALUE_MEDIUMSPRINGGREEN: c = 0x00FA9A;  break;
        case CSS_VALUE_MEDIUMTURQUOISE: c = 0x48D1CC;  break;
        case CSS_VALUE_MEDIUMVIOLETRED: c = 0xC71585;  break;
        case CSS_VALUE_MIDNIGHTBLUE: c = 0x191970;  break;
        case CSS_VALUE_MINTCREAM: c = 0xF5FFFA;  break;
        case CSS_VALUE_MISTYROSE: c = 0xFFE4E1;  break;
        case CSS_VALUE_MOCCASIN: c = 0xFFE4B5;  break;
        case CSS_VALUE_NAVAJOWHITE: c = 0xFFDEAD;  break;
        case CSS_VALUE_NAVY: c = 0x000080;  break;
        case CSS_VALUE_OLDLACE: c = 0xFDF5E6;  break;
        case CSS_VALUE_OLIVE: c = 0x808000;  break;
        case CSS_VALUE_OLIVEDRAB: c = 0x6B8E23;  break;
        case CSS_VALUE_ORANGE: c = 0xFFA500;  break;
        case CSS_VALUE_ORANGERED: c = 0xFF4500;  break;
        case CSS_VALUE_ORCHID: c = 0xDA70D6;  break;
        case CSS_VALUE_PALEGOLDENROD: c = 0xEEE8AA;  break;
        case CSS_VALUE_PALEGREEN: c = 0x98FB98;  break;
        case CSS_VALUE_PALETURQUOISE: c = 0xAFEEEE;  break;
        case CSS_VALUE_PALEVIOLETRED: c = 0xDB7093;  break;
        case CSS_VALUE_PAPAYAWHIP: c = 0xFFEFD5;  break;
        case CSS_VALUE_PEACHPUFF: c = 0xFFDAB9;  break;
        case CSS_VALUE_PERU: c = 0xCD853F;  break;
        case CSS_VALUE_PINK: c = 0xFFC0CB;  break;
        case CSS_VALUE_PLUM: c = 0xDDA0DD;  break;
        case CSS_VALUE_POWDERBLUE: c = 0xB0E0E6;  break;
        case CSS_VALUE_PURPLE: c = 0x800080;  break;
        case CSS_VALUE_REBECCAPURPLE: c = 0x663399;  break;
        case CSS_VALUE_RED: c = 0xFF0000;  break;
        case CSS_VALUE_ROSYBROWN: c = 0xBC8F8F;  break;
        case CSS_VALUE_ROYALBLUE: c = 0x4169E1;  break;
        case CSS_VALUE_SADDLEBROWN: c = 0x8B4513;  break;
        case CSS_VALUE_SALMON: c = 0xFA8072;  break;
        case CSS_VALUE_SANDYBROWN: c = 0xF4A460;  break;
        case CSS_VALUE_SEAGREEN: c = 0x2E8B57;  break;
        case CSS_VALUE_SEASHELL: c = 0xFFF5EE;  break;
        case CSS_VALUE_SIENNA: c = 0xA0522D;  break;
        case CSS_VALUE_SILVER: c = 0xC0C0C0;  break;
        case CSS_VALUE_SKYBLUE: c = 0x87CEEB;  break;
        case CSS_VALUE_SLATEBLUE: c = 0x6A5ACD;  break;
        case CSS_VALUE_SLATEGRAY: c = 0x708090;  break;
        case CSS_VALUE_SLATEGREY: c = 0x708090;  break;
        case CSS_VALUE_SNOW: c = 0xFFFAFA;  break;
        case CSS_VALUE_SPRINGGREEN: c = 0x00FF7F;  break;
        case CSS_VALUE_STEELBLUE: c = 0x4682B4;  break;
        case CSS_VALUE_TAN: c = 0xD2B48C;  break;
        case CSS_VALUE_TEAL: c = 0x008080;  break;
        case CSS_VALUE_THISTLE: c = 0xD8BFD8;  break;
        case CSS_VALUE_TOMATO: c = 0xFF6347;  break;
        case CSS_VALUE_TURQUOISE: c = 0x40E0D0;  break;
        case CSS_VALUE_VIOLET: c = 0xEE82EE;  break;
        case CSS_VALUE_WHEAT: c = 0xF5DEB3;  break;
        case CSS_VALUE_WHITE: c = 0xFFFFFF;  break;
        case CSS_VALUE_WHITESMOKE: c = 0xF5F5F5;  break;
        case CSS_VALUE_YELLOW: c = 0xFFFF00;  break;
        case CSS_VALUE_YELLOWGREEN: c = 0x9ACD32;  break;
        default: c = 0x000000;  break;
    }
    uint32_t r = (c >> 16) & 0xFF;
    uint32_t g = (c >> 8) & 0xFF;
    uint32_t b = c & 0xFF;
    return (Color){ 0xFF000000 | (b << 16) | (g << 8) | r };
}

float map_lambda_font_size_keyword(CssEnum keyword_enum) {
    // Map font-size keywords to pixel values
    switch (keyword_enum) {
        case CSS_VALUE_XX_SMALL: return 9.0f;
        case CSS_VALUE_X_SMALL: return 10.0f;
        case CSS_VALUE_SMALL: return 13.0f;
        case CSS_VALUE_MEDIUM: return 16.0f;
        case CSS_VALUE_LARGE: return 18.0f;
        case CSS_VALUE_X_LARGE: return 24.0f;
        case CSS_VALUE_XX_LARGE: return 32.0f;
        case CSS_VALUE_SMALLER: return -1.0f;  // relative to parent
        case CSS_VALUE_LARGER: return -1.0f;   // relative to parent
        default: return 16.0f; // default medium size
    }
}

// map Lambda CSS font-weight keywords/numbers to Lexbor PropValue enum
CssEnum map_lambda_font_weight_to_lexbor(const CssValue* value) {
    if (!value) return CSS_VALUE_NORMAL;

    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum keyword = value->data.keyword;

        // The keyword is already an enum, just return appropriate values
        switch (keyword) {
            case CSS_VALUE_NORMAL: return CSS_VALUE_NORMAL;
            case CSS_VALUE_BOLD: return CSS_VALUE_BOLD;
            case CSS_VALUE_BOLDER: return CSS_VALUE_BOLDER;
            case CSS_VALUE_LIGHTER: return CSS_VALUE_LIGHTER;
            default: return CSS_VALUE_NORMAL;
        }
    }
    else if (value->type == CSS_VALUE_TYPE_NUMBER || value->type == CSS_VALUE_TYPE_INTEGER) {
        // numeric weights: map to closest keyword or return as-is
        int weight = (int)value->data.number.value;

        // Lexbor uses enum values for numeric weights too, but for simplicity
        // we'll map common numeric values to their keyword equivalents
        if (weight <= 350) return CSS_VALUE_LIGHTER;
        if (weight <= 550) return CSS_VALUE_NORMAL;  // 400
        if (weight <= 750) return CSS_VALUE_BOLD;    // 700
        return CSS_VALUE_BOLDER;  // 900
    }

    return CSS_VALUE_NORMAL; // default
}

// ============================================================================
// Specificity Calculation
// ============================================================================

int32_t get_lambda_specificity(const CssDeclaration* decl) {
    if (!decl) {
        log_debug("[CSS] get_lambda_specificity: decl is NULL");
        return 0;
    }
    // Lambda CssSpecificity is a struct with (a, b, c) components
    // Convert to int32_t by packing:
    int32_t specificity = (decl->specificity.ids << 16) | (decl->specificity.classes << 8) | decl->specificity.elements;
    log_debug("[CSS] decl specificity: ids=%d, classes=%d, elmts=%d => %d",
        decl->specificity.ids, decl->specificity.classes, decl->specificity.elements, specificity);
    return specificity;
}

DisplayValue resolve_display_value(void* child) {
    // Resolve display value for a DOM node
    DisplayValue display = {CSS_VALUE_BLOCK, CSS_VALUE_FLOW};

    DomNode* node = (DomNode*)child;
    if (node && node->is_element()) {
        // resolve display from CSS if available
        const char* tag_name = node->name();

        // first, try to get display from CSS
        DomElement* dom_elem = node->as_element();
        if (dom_elem && dom_elem->specified_style) {
            StyleTree* style_tree = dom_elem->specified_style;
            if (style_tree->tree) {
                // look for display property in the AVL tree
                AvlNode* node = avl_tree_search(style_tree->tree, CSS_PROPERTY_DISPLAY);
                if (node) {
                    log_debug("[CSS] found display property for <%s>", tag_name);
                    StyleNode* style_node = (StyleNode*)node->declaration;
                    if (style_node && style_node->winning_decl) {
                        CssDeclaration* decl = style_node->winning_decl;
                        if (decl->value && decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                            CssEnum keyword = decl->value->data.keyword;

                            // Map keyword to display values
                            if (keyword == CSS_VALUE_FLEX) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_FLEX;
                                return display;
                            } else if (keyword == CSS_VALUE_INLINE_FLEX) {
                                display.outer = CSS_VALUE_INLINE_BLOCK;
                                display.inner = CSS_VALUE_FLEX;
                                return display;
                            } else if (keyword == CSS_VALUE_GRID) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_GRID;
                                return display;
                            } else if (keyword == CSS_VALUE_INLINE_GRID) {
                                display.outer = CSS_VALUE_INLINE;
                                display.inner = CSS_VALUE_GRID;
                                return display;
                            } else if (keyword == CSS_VALUE_BLOCK) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_FLOW;
                                return display;
                            } else if (keyword == CSS_VALUE_INLINE) {
                                display.outer = CSS_VALUE_INLINE;
                                display.inner = CSS_VALUE_FLOW;
                                return display;
                            } else if (keyword == CSS_VALUE_INLINE_BLOCK) {
                                display.outer = CSS_VALUE_INLINE_BLOCK;
                                display.inner = CSS_VALUE_FLOW;
                                return display;
                            } else if (keyword == CSS_VALUE_NONE) {
                                display.outer = CSS_VALUE_NONE;
                                display.inner = CSS_VALUE_NONE;
                                return display;
                            } else if (keyword == CSS_VALUE_TABLE) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE;
                                return display;
                            } else if (keyword == CSS_VALUE_INLINE_TABLE) {
                                display.outer = CSS_VALUE_INLINE;
                                display.inner = CSS_VALUE_TABLE;
                                return display;
                            } else if (keyword == CSS_VALUE_TABLE_ROW) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_ROW;
                                return display;
                            } else if (keyword == CSS_VALUE_TABLE_CELL) {
                                display.outer = CSS_VALUE_TABLE_CELL;
                                display.inner = CSS_VALUE_TABLE_CELL;
                                return display;
                            } else if (keyword == CSS_VALUE_TABLE_ROW_GROUP) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_ROW_GROUP;
                                return display;
                            } else if (keyword == CSS_VALUE_TABLE_HEADER_GROUP) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_HEADER_GROUP;
                                return display;
                            } else if (keyword == CSS_VALUE_TABLE_FOOTER_GROUP) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_FOOTER_GROUP;
                                return display;
                            } else if (keyword == CSS_VALUE_TABLE_COLUMN) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_COLUMN;
                                return display;
                            } else if (keyword == CSS_VALUE_TABLE_COLUMN_GROUP) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_COLUMN_GROUP;
                                return display;
                            } else if (keyword == CSS_VALUE_TABLE_CAPTION) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_CAPTION;
                                return display;
                            }
                        }
                    }
                }
            }
        }

        // Fall back to default display values based on tag name
        if (strcmp(tag_name, "body") == 0 || strcmp(tag_name, "h1") == 0 ||
            strcmp(tag_name, "h2") == 0 || strcmp(tag_name, "h3") == 0 ||
            strcmp(tag_name, "h4") == 0 || strcmp(tag_name, "h5") == 0 ||
            strcmp(tag_name, "h6") == 0 || strcmp(tag_name, "p") == 0 ||
            strcmp(tag_name, "div") == 0 || strcmp(tag_name, "center") == 0 ||
            strcmp(tag_name, "ul") == 0 || strcmp(tag_name, "ol") == 0 ||
            strcmp(tag_name, "header") == 0 || strcmp(tag_name, "main") == 0 ||
            strcmp(tag_name, "section") == 0 || strcmp(tag_name, "footer") == 0 ||
            strcmp(tag_name, "article") == 0 || strcmp(tag_name, "aside") == 0 ||
            strcmp(tag_name, "nav") == 0 || strcmp(tag_name, "address") == 0 ||
            strcmp(tag_name, "blockquote") == 0 || strcmp(tag_name, "details") == 0 ||
            strcmp(tag_name, "dialog") == 0 || strcmp(tag_name, "figure") == 0 ||
            strcmp(tag_name, "menu") == 0) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_FLOW;
        } else if (strcmp(tag_name, "li") == 0 || strcmp(tag_name, "summary") == 0) {
            display.outer = CSS_VALUE_LIST_ITEM;
            display.inner = CSS_VALUE_FLOW;
        } else if (strcmp(tag_name, "img") == 0 || strcmp(tag_name, "video") == 0 ||
                    strcmp(tag_name, "input") == 0 || strcmp(tag_name, "select") == 0 ||
                    strcmp(tag_name, "textarea") == 0 || strcmp(tag_name, "button") == 0 ||
                    strcmp(tag_name, "iframe") == 0) {
            display.outer = CSS_VALUE_INLINE_BLOCK;
            display.inner = RDT_DISPLAY_REPLACED;
        } else if (strcmp(tag_name, "hr") == 0) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = RDT_DISPLAY_REPLACED;
        } else if (strcmp(tag_name, "script") == 0 || strcmp(tag_name, "style") == 0 ||
                    strcmp(tag_name, "svg") == 0) {
            display.outer = CSS_VALUE_NONE;
            display.inner = CSS_VALUE_NONE;
        } else if (strcmp(tag_name, "table") == 0) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_TABLE;
        } else if (strcmp(tag_name, "caption") == 0) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_FLOW;
        } else if (strcmp(tag_name, "thead") == 0 || strcmp(tag_name, "tbody") == 0 ||
                    strcmp(tag_name, "tfoot") == 0) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_TABLE_ROW_GROUP;
        } else if (strcmp(tag_name, "tr") == 0) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_TABLE_ROW;
        } else if (strcmp(tag_name, "th") == 0 || strcmp(tag_name, "td") == 0) {
            display.outer = CSS_VALUE_TABLE_CELL;
            display.inner = CSS_VALUE_TABLE_CELL;
        } else if (strcmp(tag_name, "colgroup") == 0) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_TABLE_COLUMN_GROUP;
        } else if (strcmp(tag_name, "col") == 0) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_TABLE_COLUMN;
        } else {
            // Default for unknown elements (inline)
            display.outer = CSS_VALUE_INLINE;
            display.inner = CSS_VALUE_FLOW;
        }

        // TODO: Check for CSS display property in child->style (DomElement)
        // For now, using tag-based defaults is sufficient
    }

    return display;
}

/**
 * Helper function to resolve font size for Lambda CSS
 * Used internally by resolve_length_value for em/rem calculations
 */
static void resolve_font_size(LayoutContext* lycon, const CssDeclaration* decl) {
    log_debug("resolve font size property (Lambda CSS)");

    if (!decl && lycon->view) {
        // Try to get font-size from the view's font property
        ViewSpan* span = (ViewSpan*)lycon->view;
        if (span->font && span->font->font_size > 0) {
            lycon->font.current_font_size = span->font->font_size;
            log_debug("resolved font size from view: %.2f px", lycon->font.current_font_size);
            return;
        }
    }

    if (decl && decl->value) {
        // resolve font size from declaration
        const CssValue* value = decl->value;

        if (value->type == CSS_VALUE_TYPE_LENGTH) {
            // Direct length value
            lycon->font.current_font_size = resolve_length_value(lycon,
                CSS_PROPERTY_FONT_SIZE, value);
            log_debug("resolved font size from declaration: %.2f px", lycon->font.current_font_size);
            return;
        } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
            // Keyword font size
            float size = map_lambda_font_size_keyword(value->data.keyword);
            if (size > 0) {
                lycon->font.current_font_size = size;
                log_debug("resolved font size from keyword '%s': %.2f px",
                         css_enum_info(value->data.keyword)->name, size);
                return;
            }
        }
    }

    // fallback: use font size from style context
    if (lycon->font.style && lycon->font.style->font_size > 0) {
        lycon->font.current_font_size = lycon->font.style->font_size;
        log_debug("resolved font size from style context: %.2f px", lycon->font.current_font_size);
    } else {
        // ultimate fallback: use default
        lycon->font.current_font_size = 16.0f;
        log_debug("resolved font size to default: 16.0 px");
    }
}

/**
 * Resolve length/percentage value to pixels using Lambda CSS value structures
 *
 * @param lycon Layout context for font size, viewport, and parent dimensions
 * @param property CSS property ID for context-specific resolution
 * @param value Lambda CssValue pointer (CSS_VALUE_LENGTH, CSS_VALUE_PERCENTAGE, or CSS_VALUE_NUMBER)
 * @return Resolved value in pixels
 */
float resolve_length_value(LayoutContext* lycon, uintptr_t property, const CssValue* value) {
    if (!value) { log_debug("resolve_length_value: null value");  return 0.0f; }

    float result = 0.0f;
    switch (value->type) {
    case CSS_VALUE_TYPE_NUMBER:
        // unitless number - treat as pixels for most properties
        log_debug("number value: %.2f", value->data.number.value);
        result = (float)value->data.number.value;
        break;

    case CSS_VALUE_TYPE_INTEGER:
        // integer value - treat as pixels
        log_debug("integer value: %d", value->data.integer.value);
        result = (float)value->data.integer.value;
        break;

    case CSS_VALUE_TYPE_LENGTH: {
        double num = value->data.length.value;
        CssUnit unit = value->data.length.unit;
        log_debug("length value: %.2f, unit: %d", num, unit);

        switch (unit) {
        // absolute units
        case CSS_UNIT_Q:  // 1Q = 1cm / 40
            result = num * (96 / 2.54 / 40) * lycon->ui_context->pixel_ratio;
            break;
        case CSS_UNIT_CM:  // 96px / 2.54
            result = num * (96 / 2.54) * lycon->ui_context->pixel_ratio;
            break;
        case CSS_UNIT_IN:  // 96px
            result = num * 96 * lycon->ui_context->pixel_ratio;
            break;
        case CSS_UNIT_MM:  // 1mm = 1cm / 10
            result = num * (96 / 25.4) * lycon->ui_context->pixel_ratio;
            break;
        case CSS_UNIT_PC:  // 1pc = 12pt = 1in / 6
            result = num * 16 * lycon->ui_context->pixel_ratio;
            break;
        case CSS_UNIT_PT:  // 1pt = 1in / 72
            result = num * 4 / 3 * lycon->ui_context->pixel_ratio;
            break;
        case CSS_UNIT_PX:
            result = num * lycon->ui_context->pixel_ratio;
            break;

        // relative units
        case CSS_UNIT_REM:
            if (lycon->root_font_size < 0) {
                log_debug("resolving font size for rem value");
                resolve_font_size(lycon, NULL);
                lycon->root_font_size = lycon->font.current_font_size < 0 ?
                    lycon->ui_context->default_font.font_size : lycon->font.current_font_size;
            }
            result = num * lycon->root_font_size;
            break;
        case CSS_UNIT_EM:
            if (property == CSS_PROPERTY_FONT_SIZE) {
                result = num * lycon->font.style->font_size;
            } else {
                if (lycon->font.current_font_size < 0) {
                    log_debug("resolving font size for em value");
                    resolve_font_size(lycon, NULL);
                }
                result = num * lycon->font.current_font_size;
            }
            break;
        case CSS_UNIT_VW:
            // viewport width percentage
            if (lycon && lycon->width > 0) {
                result = (num / 100.0) * lycon->width;
            }
            break;
        case CSS_UNIT_VH:
            // viewport height percentage
            if (lycon && lycon->height > 0) {
                result = (num / 100.0) * lycon->height;
            }
            break;
        case CSS_UNIT_VMIN: {
            float vmin = (lycon->width < lycon->height) ? lycon->width : lycon->height;
            result = (num / 100.0) * vmin;
            break;
        }
        case CSS_UNIT_VMAX: {
            float vmax = (lycon->width > lycon->height) ? lycon->width : lycon->height;
            result = (num / 100.0) * vmax;
            break;
        }
        case CSS_UNIT_EX:
            // relative to x-height (approximate as 0.5em)
            if (lycon->font.current_font_size < 0) {
                resolve_font_size(lycon, NULL);
            }
            result = num * lycon->font.current_font_size * 0.5;
            break;
        case CSS_UNIT_CH:
            // relative to width of "0" character (approximate as 0.5em)
            if (lycon->font.current_font_size < 0) {
                resolve_font_size(lycon, NULL);
            }
            result = num * lycon->font.current_font_size * 0.5;
            break;

        default:
            result = num;  // fallback: assume pixels for unknown units
            log_debug("unknown unit: %d, treating as pixels", unit);
            break;
        }
        break;
    }
    case CSS_VALUE_TYPE_PERCENTAGE: {
        double percentage = value->data.percentage.value;
        if (property == CSS_PROPERTY_FONT_SIZE) {
            // font-size percentage is relative to parent font size
            result = percentage * lycon->font.style->font_size / 100.0;
        } else {
            // most properties: percentage relative to parent width
            if (lycon->block.pa_block) {
                log_debug("percentage calculation: %.2f%% of parent width %d = %.2f",
                       percentage, lycon->block.pa_block->content_width,
                       percentage * lycon->block.pa_block->content_width / 100.0);
                result = percentage * lycon->block.pa_block->content_width / 100.0;
            } else {
                log_debug("percentage value %.2f%% without parent context", percentage);
                result = 0.0f;
            }
        }
        break;
    }
    case CSS_VALUE_TYPE_KEYWORD: {
        // handle special keywords like 'auto'
        CssEnum keyword = value->data.keyword;
        if (keyword == CSS_VALUE_AUTO) {
            log_info("length value: auto");
            result = 0.0f;  // auto represented as 0, caller should check keyword separately
        } else {
            const CssEnumInfo* info = css_enum_info(keyword);
            log_debug("length keyword: %s (treating as 0)", info ? info->name : "unknown");
            result = 0.0f;
        }
        break;
    }
    default:
        log_warn("unknown length value type: %d", value->type);
        result = 0.0f;
        break;
    }
    log_debug("resolved length value: type %d -> %.2f px", value->type, result);
    return result;
}

// resolve property 'margin', 'padding', etc.
void resolve_spacing_prop(LayoutContext* lycon, uintptr_t property,
    const CssValue *src_space, int32_t specificity, Spacing* trg_spacing) {
    Margin sp;  // temporal space
    int value_cnt = 1;  bool is_margin = property == CSS_PROPERTY_MARGIN;
    log_debug("resolve_spacing_prop with specificity %d", specificity);
    if (src_space->type == CSS_VALUE_TYPE_LIST) {
        // Multi-value margin
        value_cnt = src_space->data.list.count;
        CssValue** values = src_space->data.list.values;
        switch (value_cnt) {
        case 4:
            log_debug("resolving 4th spacing");
            sp.left = resolve_length_value(lycon, property, values[3]);
            sp.left_type = values[3]->type == CSS_VALUE_TYPE_KEYWORD ? values[3]->data.keyword : CSS_VALUE__UNDEF;
            // intended fall through
        case 3:
            log_debug("resolving 3rd spacing");
            sp.bottom = resolve_length_value(lycon, property, values[2]);
            sp.bottom_type = values[2]->type == CSS_VALUE_TYPE_KEYWORD ? values[2]->data.keyword : CSS_VALUE__UNDEF;
            // intended fall through
        case 2:
            log_debug("resolving 2nd spacing");
            sp.right = resolve_length_value(lycon, property, values[1]);
            sp.right_type = values[1]->type == CSS_VALUE_TYPE_KEYWORD ? values[1]->data.keyword : CSS_VALUE__UNDEF;
            // intended fall through
        case 1:
            log_debug("resolving 1st spacing");
            sp.top = resolve_length_value(lycon, property, values[0]);
            sp.top_type = values[0]->type == CSS_VALUE_TYPE_KEYWORD ? values[0]->data.keyword : CSS_VALUE__UNDEF;
            break;
        default:
            log_warn("unexpected spacing value count: %d", value_cnt);
            break;
        }
    } else {
        // Single value margin
        sp.top = resolve_length_value(lycon, property, src_space);
        sp.top_type = src_space->type == CSS_VALUE_TYPE_KEYWORD ? src_space->data.keyword : CSS_VALUE__UNDEF;
    }
    switch (value_cnt) {
    case 1:
        sp.right = sp.left = sp.bottom = sp.top;
        if (is_margin) { sp.right_type = sp.left_type = sp.bottom_type = sp.top_type; }
        break;
    case 2:
        sp.bottom = sp.top;  sp.left = sp.right;
        if (is_margin) { sp.bottom_type = sp.top_type;  sp.left_type = sp.right_type; }
        break;
    case 3:
        sp.left = sp.right;
        if (is_margin) { sp.left_type = sp.right_type; }
        break;
    case 4:
        // no change to values
        break;
    }
    // store value in final spacing struct if specificity is higher
    Margin *trg_margin = is_margin ? (Margin *) trg_spacing : NULL;
    if (specificity >= trg_spacing->top_specificity) {
        trg_spacing->top = sp.top;
        trg_spacing->top_specificity = specificity;
        if (trg_margin) trg_margin->top_type = sp.top_type;
        log_debug("updated top spacing to %f", trg_spacing->top);
    } else {
        log_debug("skipped top spacing update due to lower specificity: %d <= %d", specificity, trg_spacing->top_specificity);
    }
    if (specificity >= trg_spacing->bottom_specificity) {
        trg_spacing->bottom = sp.bottom;
        trg_spacing->bottom_specificity = specificity;
        if (trg_margin) trg_margin->bottom_type = sp.bottom_type;
    }
    if (specificity >= trg_spacing->right_specificity) {
        // only margin-left and right support auto value
        trg_spacing->right = sp.right;
        trg_spacing->right_specificity = specificity;
        if (trg_margin) trg_margin->right_type = sp.right_type;
    }
    if (specificity >= trg_spacing->left_specificity) {
        trg_spacing->left = sp.left;
        trg_spacing->left_specificity = specificity;
        if (trg_margin) trg_margin->left_type = sp.left_type;
    }
    log_debug("spacing value: top %f, right %f, bottom %f, left %f",
        trg_spacing->top, trg_spacing->right, trg_spacing->bottom, trg_spacing->left);
}

// ============================================================================
// Main Style Resolution
// ============================================================================

// Callback for AVL tree traversal
static bool resolve_property_callback(AvlNode* node, void* context) {
    LayoutContext* lycon = (LayoutContext*)context;

    // Get StyleNode from AvlNode->declaration field
    StyleNode* style_node = (StyleNode*)node->declaration;

    // get property ID from node
    CssPropertyId prop_id = (CssPropertyId)node->property_id;

    log_debug("[Lambda CSS Property] Processing property ID: %d", prop_id);

    // get the CSS declaration for this property
    CssDeclaration* decl = style_node ? style_node->winning_decl : NULL;
    if (!decl) {
        log_debug("[Lambda CSS Property] No declaration found for property %d (style_node=%p)",
                  prop_id, (void*)style_node);
        return true; // continue iteration
    }

    log_debug("[Lambda CSS Property] Found declaration for property %d: decl=%p, value=%p",
              prop_id, (void*)decl, (void*)decl->value);

    // resolve this property
    resolve_lambda_css_property(prop_id, decl, lycon);
    return true; // continue iteration
}

void resolve_lambda_css_styles(DomElement* dom_elem, LayoutContext* lycon) {
    assert(dom_elem);
    log_debug("[Lambda CSS] Resolving styles for element <%s>", dom_elem->tag_name);

    // iterate through specified_style AVL tree
    StyleTree* style_tree = dom_elem->specified_style;
    if (!style_tree || !style_tree->tree) {
        log_debug("[Lambda CSS] No style tree found for element");
        return;
    }
    log_debug("[Lambda CSS] Style tree has %d nodes", style_tree->tree->node_count);

    // Traverse the AVL tree and resolve each property
    int processed = avl_tree_foreach_inorder(style_tree->tree, resolve_property_callback, lycon);
    log_debug("[Lambda CSS] Processed %d style properties", processed);

    // Handle CSS inheritance for inheritable properties not explicitly set
    // Important inherited properties: font-family, font-size, font-weight, color, etc.
    static const CssPropertyId inheritable_props[] = {
        CSS_PROPERTY_FONT_FAMILY,
        CSS_PROPERTY_FONT_SIZE,
        CSS_PROPERTY_FONT_WEIGHT,
        CSS_PROPERTY_FONT_STYLE,
        CSS_PROPERTY_COLOR,
        CSS_PROPERTY_LINE_HEIGHT,
        CSS_PROPERTY_TEXT_ALIGN,
        CSS_PROPERTY_TEXT_DECORATION,
        CSS_PROPERTY_TEXT_TRANSFORM,
        CSS_PROPERTY_LETTER_SPACING,
        CSS_PROPERTY_WORD_SPACING,
        CSS_PROPERTY_WHITE_SPACE,
        CSS_PROPERTY_VISIBILITY,
    };
    static const size_t num_inheritable = sizeof(inheritable_props) / sizeof(inheritable_props[0]);

    // Get parent's style tree for inheritance
    DomElement* parent = dom_elem->parent ? static_cast<DomElement*>(dom_elem->parent) : nullptr;
    StyleTree* parent_tree = (parent && parent->specified_style)
                             ? parent->specified_style : NULL;

    if (parent_tree) {
        log_debug("[Lambda CSS] Checking inheritance from parent <%s>",
                parent->tag_name);

        for (size_t i = 0; i < num_inheritable; i++) {
            CssPropertyId prop_id = inheritable_props[i];

            // Check if this property is already set on the element
            CssDeclaration* existing = style_tree_get_declaration(style_tree, prop_id);
            if (existing) {
                // Property is explicitly set, don't inherit
                continue;
            }

            // Property not set, check parent chain for inherited declaration
            // Walk up the parent chain until we find a declaration
            DomElement* ancestor = dom_elem->parent ? static_cast<DomElement*>(dom_elem->parent) : nullptr;
            CssDeclaration* inherited_decl = NULL;

            while (ancestor && !inherited_decl) {
                if (ancestor->specified_style) {
                    inherited_decl = style_tree_get_declaration(ancestor->specified_style, prop_id);
                    if (inherited_decl && inherited_decl->value) {
                        break; // Found it!
                    }
                }
                ancestor = ancestor->parent ? static_cast<DomElement*>(ancestor->parent) : nullptr;
            }

            if (inherited_decl && inherited_decl->value) {
                log_debug("[Lambda CSS] Inheriting property %d from ancestor <%s>",
                         prop_id, ancestor ? ancestor->tag_name : "unknown");

                // Apply the inherited property using the ancestor's declaration
                resolve_lambda_css_property(prop_id, inherited_decl, lycon);
            }
        }
    }
}

struct MultiValue {
    const CssValue* length;
    const CssValue* color;
    const CssValue* style;
};

void set_multi_value(MultiValue* mv, const CssValue* value) {
    if (!mv || !value) return;
    if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_PERCENTAGE || value->type == CSS_VALUE_TYPE_NUMBER || value->type == CSS_VALUE_TYPE_INTEGER) {
        mv->length = (CssValue*)value;
    } else if (value->type == CSS_VALUE_TYPE_COLOR) {
        mv->color = (CssValue*)value;
    } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        if (info) {
            switch (info->group) {
                case CSS_VALUE_GROUP_BORDER_STYLE:
                    mv->style = value;
                    break;
                case CSS_VALUE_GROUP_COLOR:
                    mv->color = value;
                    break;
                default:
                    // could be other keyword types
                    log_debug("Unhandled keyword group: %d", info->group);
                    break;
            }
        }
    }
    else if (value->type == CSS_VALUE_TYPE_LIST) {
        // handle list of values
        for (size_t i = 0; i < value->data.list.count; i++) {
            CssValue* item = value->data.list.values[i];
            set_multi_value(mv, item);
        }
    }
}

void resolve_lambda_css_property(CssPropertyId prop_id, const CssDeclaration* decl, LayoutContext* lycon) {
    log_debug("[Lambda CSS Property] resolve_lambda_css_property called: prop_id=%d", prop_id);
    if (!decl || !lycon || !lycon->view) {
        log_debug("[Lambda CSS Property] Early return: decl=%p, lycon=%p, view=%p",
            (void*)decl, (void*)lycon, lycon ? (void*)lycon->view : NULL);
        return;
    }
    const CssValue* value = decl->value;
    if (!value) { log_debug("No value in declaration");  return; }
    log_debug("[Lambda CSS Property] Processing property %d, %s, value type=%d",
        prop_id, css_property_name_from_id(prop_id), value->type);
    int32_t specificity = get_lambda_specificity(decl);
    log_debug("[Lambda CSS Property] Specificity: %d", specificity);

    // Dispatch based on property ID
    // Parallel implementation to resolve_element_style() in resolve_style.cpp
    ViewSpan* span = (ViewSpan*)lycon->view;
    ViewBlock* block = lycon->view->type != RDT_VIEW_INLINE ? (ViewBlock*)lycon->view : NULL;

    switch (prop_id) {
        // ===== GROUP 1: Core Typography & Color =====
        case CSS_PROPERTY_COLOR: {
            log_debug("[CSS] Processing color property");
            if (!span->in_line) {
                span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
            }
            span->in_line->color = resolve_color_value(value);
            break;
        }

        case CSS_PROPERTY_FONT_SIZE: {
            log_debug("[CSS] Processing font-size property");
            if (!span->font) {
                span->font = alloc_font_prop(lycon);
            }

            float font_size = 0.0f;
            bool valid = false;

            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                // Special handling for em units: em is relative to parent font size for font-size property
                if (value->data.length.unit == CSS_UNIT_EM) {
                    float parent_size = span->font->font_size > 0 ? span->font->font_size : 16.0f;
                    font_size = value->data.length.value * parent_size;
                    log_debug("[CSS] Font size em: %.2fem -> %.2f px (parent size: %.2f px)",
                              value->data.length.value, font_size, parent_size);
                } else {
                    font_size = convert_lambda_length_to_px(value, lycon, prop_id);
                    log_debug("[CSS] Font size length: %.2f px (after conversion)", font_size);
                }
                // Per CSS spec, negative font-size values are invalid, but 0 is valid
                if (font_size >= 0) {
                    valid = true;
                } else {
                    log_debug("[CSS] Font size: %.2f px invalid (must be >= 0), ignoring", font_size);
                }
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                // Percentage of parent font size
                float parent_size = span->font->font_size > 0 ? span->font->font_size : 16.0f;
                font_size = parent_size * (value->data.percentage.value / 100.0f);
                log_debug("[CSS] Font size percentage: %.2f%% -> %.2f px (parent size: %.2f px)",
                          value->data.percentage.value, font_size, parent_size);
                if (font_size >= 0) {
                    valid = true;
                } else {
                    log_debug("[CSS] Font size: %.2f px invalid (must be >= 0), ignoring", font_size);
                }
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // Named font sizes: small, medium, large, etc.
                font_size = map_lambda_font_size_keyword(value->data.keyword);
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                log_debug("[CSS] Font size keyword: %s -> %.2f px", info ? info->name : "unknown", font_size);
                if (font_size > 0) {
                    valid = true;
                }
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                // Per CSS spec, unitless zero is valid and treated as 0px
                // Other unitless numbers are invalid for font-size
                font_size = value->data.number.value;
                if (font_size == 0.0f) {
                    valid = true;
                    log_debug("[CSS] Font size: unitless 0 (treated as 0px)");
                } else {
                    log_debug("[CSS] Font size number: %.2f (non-zero unitless values invalid for font-size)", font_size);
                }
            }

            if (valid) {
                span->font->font_size = font_size;
                log_debug("[CSS] Font size set to: %.2f px", font_size);
            } else {
                log_debug("[CSS] Font size not set (invalid value)");
            }
            break;
        }

        case CSS_PROPERTY_FONT_WEIGHT: {
            log_debug("[CSS] Processing font-weight property");
            if (!span->font) {
                span->font = alloc_font_prop(lycon);
                log_debug("[CSS]   Created new FontProp with defaults");
            }

            // Map Lambda CSS value to Lexbor PropValue enum
            CssEnum lexbor_weight = map_lambda_font_weight_to_lexbor(value);
            span->font->font_weight = lexbor_weight;

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                log_debug("[CSS] Font weight keyword: '%s' -> Lexbor enum: %d",
                         css_enum_info(value->data.keyword)->name, lexbor_weight);
            } else if (value->type == CSS_VALUE_TYPE_NUMBER || value->type == CSS_VALUE_TYPE_INTEGER) {
                log_debug("[CSS] Font weight number: %d -> Lexbor enum: %d",
                         (int)value->data.number.value, lexbor_weight);
            }
            break;
        }

        case CSS_PROPERTY_FONT_FAMILY: {
            log_debug("[CSS] Processing font-family property");
            if (!span->font) {
                span->font = alloc_font_prop(lycon);
            }
            if (value->type == CSS_VALUE_TYPE_STRING) {
                // Font family name as string (quotes already stripped during parsing)
                span->font->family = (char*)value->data.string;
            }
            else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // Keyword font family - check if generic or specific
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                span->font->family = info ? (char*)info->name : NULL;
                log_debug("[CSS] Set span->font->family = '%s'", span->font->family);
            }
            else if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
                // List of font families (e.g., "Arial, sans-serif")
                // Use the first available font family
                for (size_t i = 0; i < value->data.list.count; i++) {
                    CssValue* item = value->data.list.values[i];
                    if (!item) continue;
                    const char* family = NULL;
                    log_debug("[CSS] Font family list item type: %d", item->type);
                    if (item->type == CSS_VALUE_TYPE_STRING && item->data.string) {
                        family = item->data.string;
                        log_debug("[CSS] Font family STRING value: '%s'", family);
                    } else if (item->type == CSS_VALUE_TYPE_KEYWORD) {
                        // Check if it's a generic font family keyword
                        const CssEnumInfo* info = css_enum_info(item->data.keyword);
                        family = info ? info->name : NULL;
                        log_debug("[CSS] Font family KEYWORD value: '%s'", family ? family : "(null)");
                    } else if (item->type == CSS_VALUE_TYPE_CUSTOM && item->data.custom_property.name) {
                        // Custom font family name (e.g., "Arial", "Helvetica")
                        family = item->data.custom_property.name;
                        log_debug("[CSS] Font family CUSTOM value: '%s'", family);
                    }
                    if (family) {
                        // Quotes already stripped during parsing
                        span->font->family = (char*)family;
                        log_debug("[CSS] Font family from list[%zu]: %s", i, family);
                        break; // Use first font in the list
                    }
                }
            }
            break;
        }

        case CSS_PROPERTY_LINE_HEIGHT: {
            log_debug("[CSS] Processing line-height property");
            if (!block) { break; }
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }

            // Allocate lxb_css_property_line_height_t structure (compatible with Lexbor)
            lxb_css_property_line_height_t* line_height =
                (lxb_css_property_line_height_t*)alloc_prop(lycon, sizeof(lxb_css_property_line_height_t));

            if (!line_height) {
                log_debug("[CSS] Failed to allocate line_height structure");
                break;
            }

            // Line height can be number (multiplier), length, percentage, or 'normal'
            if (value->type == CSS_VALUE_TYPE_NUMBER) {
                // Unitless number - multiply by font size
                line_height->type = CSS_VALUE__NUMBER;
                line_height->u.number.num = value->data.number.value;
                log_debug("[CSS] Line height number: %.2f", value->data.number.value);
                block->blk->line_height = line_height;
            } else if (value->type == CSS_VALUE_TYPE_LENGTH) {
                line_height->type = CSS_VALUE__LENGTH;
                line_height->u.length.num = value->data.length.value;
                line_height->u.length.is_float = true;
                line_height->u.length.unit = value->data.length.unit;
                log_debug("[CSS] Line height length: %.2f px (unit: %d)", value->data.length.value, value->data.length.unit);
                block->blk->line_height = line_height;
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                line_height->type = CSS_VALUE__PERCENTAGE;
                line_height->u.percentage.num = value->data.percentage.value;
                log_debug("[CSS] Line height percentage: %.2f%%", value->data.percentage.value);
                block->blk->line_height = line_height;
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum keyword = value->data.keyword;
                if (keyword == CSS_VALUE_NORMAL) {
                    line_height->type = CSS_VALUE_NORMAL;
                    log_debug("[CSS] Line height keyword: normal");
                    block->blk->line_height = line_height;
                } else if (keyword == CSS_VALUE_INHERIT) {
                    line_height->type = CSS_VALUE_INHERIT;
                    log_debug("[CSS] Line height keyword: inherit");
                    block->blk->line_height = line_height;
                }
            }
            break;
        }

        // ===== GROUP 5: Text Properties =====
        case CSS_PROPERTY_TEXT_ALIGN: {
            log_debug("[CSS] Processing text-align property");
            if (!block) break;
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum align_value = value->data.keyword;
                if (align_value != CSS_VALUE__UNDEF) {
                    block->blk->text_align = align_value;
                    const CssEnumInfo* info = css_enum_info(align_value);
                    log_debug("[CSS] Text-align: %s -> 0x%04X", info ? info->name : "unknown", align_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_TEXT_DECORATION: {
            log_debug("[CSS] Processing text-decoration property");
            if (!span->font) {
                span->font = alloc_font_prop(lycon);
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum deco_value = value->data.keyword;
                if (deco_value != CSS_VALUE__UNDEF) {
                    span->font->text_deco = deco_value;
                    const CssEnumInfo* info = css_enum_info(deco_value);
                    log_debug("[CSS] Text-decoration: %s -> 0x%04X", info ? info->name : "unknown", deco_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_VERTICAL_ALIGN: {
            log_debug("[CSS] Processing vertical-align property");
            if (!span->in_line) {
                span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum valign_value = value->data.keyword;
                if (valign_value != CSS_VALUE__UNDEF) {
                    span->in_line->vertical_align = valign_value;
                    const CssEnumInfo* info = css_enum_info(valign_value);
                    log_debug("[CSS] Vertical-align: %s -> 0x%04X", info ? info->name : "unknown", valign_value);
                } else {
                    log_debug("[CSS] Vertical-align: unknown keyword (enum undefined)");
                }
            } else if (value->type == CSS_VALUE_TYPE_LENGTH) {
                // Length values for vertical-align (e.g., 10px, -5px)
                // Store as offset - will need to extend PropValue to support length offsets
                log_debug("[CSS] Vertical-align length: %.2f px (not yet fully supported)", value->data.length.value);
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                // Percentage values relative to line-height
                log_debug("[CSS] Vertical-align percentage: %.2f%% (not yet fully supported)", value->data.percentage.value);
            } else {
                log_debug("[CSS] Vertical-align: unsupported value type %d", value->type);
            }
            break;
        }

        case CSS_PROPERTY_CURSOR: {
            log_debug("[CSS] Processing cursor property");
            if (!span->in_line) {
                span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum cursor_value = value->data.keyword;
                if (cursor_value != CSS_VALUE__UNDEF) {
                    span->in_line->cursor = cursor_value;
                    const CssEnumInfo* info = css_enum_info(cursor_value);
                    log_debug("[CSS] Cursor: %s -> 0x%04X", info ? info->name : "unknown", cursor_value);
                }
            }
            break;
        }

        // ===== GROUP 2: Box Model Basics =====

        case CSS_PROPERTY_WIDTH: {
            log_debug("[CSS] Processing width property");
            // width cannot be negative
            float width = max(resolve_length_value(lycon, CSS_PROPERTY_WIDTH, value), 0);
            lycon->block.given_width = width;
            log_debug("width property: %f, type: %d", lycon->block.given_width, value->type);
            // Store the raw width value for box-sizing calculations
            if (block) {
                if (!block->blk) { block->blk = alloc_block_prop(lycon); }
                block->blk->given_width = width;
                block->blk->given_width_type = value->type == CSS_VALUE_TYPE_KEYWORD ? value->data.keyword : CSS_VALUE__UNDEF;
            }
            log_debug("[CSS] Width: %.2f px", width);
            break;
        }

        case CSS_PROPERTY_HEIGHT: {
            log_debug("[CSS] Processing height property");
            float height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, value);
            lycon->block.given_height = height = isnan(height) ? height : max(height, 0);  // height cannot be negative
            log_debug("height property: %d", lycon->block.given_height);
            // store the raw height value for box-sizing calculations
            if (block) {
                if (!block->blk) { block->blk = alloc_block_prop(lycon); }
                block->blk->given_height = height;
            }
            break;
        }

        case CSS_PROPERTY_MIN_WIDTH: {
            log_debug("[CSS] Processing min-width property");
            if (!block) break;
            if (!block->blk) {
                block->blk = alloc_block_prop(lycon);
            }
            block->blk->given_min_width = resolve_length_value(lycon, CSS_PROPERTY_MIN_WIDTH, value);
            log_debug("[CSS] Min-width: %.2f px", block->blk->given_min_width);
            break;
        }

        case CSS_PROPERTY_MAX_WIDTH: {
            log_debug("[CSS] Processing max-width property");
            if (!block) break;
            if (!block->blk) {
                block->blk = alloc_block_prop(lycon);
            }
            block->blk->given_max_width = resolve_length_value(lycon, CSS_PROPERTY_MAX_WIDTH, value);
            log_debug("[CSS] Max-width: %.2f px", block->blk->given_max_width);
            break;
        }

        case CSS_PROPERTY_MIN_HEIGHT: {
            log_debug("[CSS] Processing min-height property");
            if (!block) break;
            if (!block->blk) {
                block->blk = alloc_block_prop(lycon);
            }
            block->blk->given_min_height = resolve_length_value(lycon, CSS_PROPERTY_MIN_HEIGHT, value);
            log_debug("[CSS] Min-height: %.2f px", block->blk->given_min_height);
            break;
        }

        case CSS_PROPERTY_MAX_HEIGHT: {
            log_debug("[CSS] Processing max-height property");
            if (!block) break;
            if (!block->blk) {
                block->blk = alloc_block_prop(lycon);
            }
            block->blk->given_max_height = resolve_length_value(lycon, CSS_PROPERTY_MAX_HEIGHT, value);
            log_debug("[CSS] Max-height: %.2f px", block->blk->given_max_height);
            break;
        }

        case CSS_PROPERTY_MARGIN: {
            log_debug("[CSS Switch] Entered CSS_PROPERTY_MARGIN case! value type: %d, span: %p, bound: %p",
                value->type, (void*)span, (void*)(span->bound));
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            resolve_spacing_prop(lycon, CSS_PROPERTY_MARGIN, value, specificity, &span->bound->margin);
            break;
        }

        case CSS_PROPERTY_PADDING: {
            log_debug("[CSS] Processing padding shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            resolve_spacing_prop(lycon, CSS_PROPERTY_PADDING, value, specificity, &span->bound->padding);
            break;
        }

        case CSS_PROPERTY_MARGIN_TOP: {
            log_debug("[CSS] Processing margin-top property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (specificity >= span->bound->margin.top_specificity) {
                span->bound->margin.top = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_TOP, value);
                span->bound->margin.top_specificity = specificity;
                span->bound->margin.top_type = value->type == CSS_VALUE_TYPE_KEYWORD ? value->data.keyword : CSS_VALUE__UNDEF;
            }
            break;
        }
        case CSS_PROPERTY_MARGIN_RIGHT: {
            log_debug("[CSS] Processing margin-right property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (specificity >= span->bound->margin.right_specificity) {
                span->bound->margin.right = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_RIGHT, value);
                span->bound->margin.right_specificity = specificity;
                span->bound->margin.right_type = value->type == CSS_VALUE_TYPE_KEYWORD ? value->data.keyword : CSS_VALUE__UNDEF;
            }
            break;
        }
        case CSS_PROPERTY_MARGIN_BOTTOM: {
            log_debug("[CSS] Processing margin-bottom property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (specificity >= span->bound->margin.bottom_specificity) {
                span->bound->margin.bottom = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_BOTTOM, value);
                span->bound->margin.bottom_specificity = specificity;
                span->bound->margin.bottom_type = value->type == CSS_VALUE_TYPE_KEYWORD ? value->data.keyword : CSS_VALUE__UNDEF;
            }
            break;
        }
        case CSS_PROPERTY_MARGIN_LEFT: {
            log_debug("[CSS] Processing margin-left property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (specificity >= span->bound->margin.left_specificity) {
                span->bound->margin.left = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_LEFT, value);
                span->bound->margin.left_specificity = specificity;
                span->bound->margin.left_type = value->type == CSS_VALUE_TYPE_KEYWORD ? value->data.keyword : CSS_VALUE__UNDEF;
            }
            break;
        }

        case CSS_PROPERTY_PADDING_TOP: {
            log_debug("[CSS] Processing padding-top property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (specificity >= span->bound->padding.top_specificity) {
                span->bound->padding.top = resolve_length_value(lycon, CSS_PROPERTY_PADDING_TOP, value);
                span->bound->padding.top_specificity = specificity;
            }
            break;
        }
        case CSS_PROPERTY_PADDING_RIGHT: {
            log_debug("[CSS] Processing padding-right property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (specificity >= span->bound->padding.right_specificity) {
                span->bound->padding.right = resolve_length_value(lycon, CSS_PROPERTY_PADDING_RIGHT, value);
                span->bound->padding.right_specificity = specificity;
            }
            break;
        }
        case CSS_PROPERTY_PADDING_BOTTOM: {
            log_debug("[CSS] Processing padding-bottom property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (specificity >= span->bound->padding.bottom_specificity) {
                span->bound->padding.bottom = resolve_length_value(lycon, CSS_PROPERTY_PADDING_BOTTOM, value);
                span->bound->padding.bottom_specificity = specificity;
            }
            break;
        }
        case CSS_PROPERTY_PADDING_LEFT: {
            log_debug("[CSS] Processing padding-left property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (specificity >= span->bound->padding.left_specificity) {
                span->bound->padding.left = resolve_length_value(lycon, CSS_PROPERTY_PADDING_LEFT, value);
                span->bound->padding.left_specificity = specificity;
            }
            break;
        }

        case CSS_PROPERTY_BACKGROUND_COLOR: {
            log_debug("[CSS] Processing background-color property (value type=%d)", value->type);
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->background) {
                span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
            }
            span->bound->background->color = resolve_color_value(value);
            break;
        }

        // ===== GROUP 16: Background Advanced Properties =====
        case CSS_PROPERTY_BACKGROUND_ATTACHMENT: {
            log_debug("[CSS] Processing background-attachment property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->background) {
                span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // Values: scroll, fixed, local
                log_debug("[CSS] background-attachment: %s", css_enum_info(value->data.keyword)->name);
                // TODO: Store attachment value when BackgroundProp is extended
            }
            break;
        }

        case CSS_PROPERTY_BACKGROUND_ORIGIN: {
            log_debug("[CSS] Processing background-origin property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->background) {
                span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // Values: border-box, padding-box, content-box
                log_debug("[CSS] background-origin: %s", css_enum_info(value->data.keyword)->name);
                // TODO: Store origin value when BackgroundProp is extended
            }
            break;
        }

        case CSS_PROPERTY_BACKGROUND_CLIP: {
            log_debug("[CSS] Processing background-clip property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->background) {
                span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // Values: border-box, padding-box, content-box
                log_debug("[CSS] background-clip: %s", css_enum_info(value->data.keyword)->name);
                // TODO: Store clip value when BackgroundProp is extended
            }
            break;
        }

        case CSS_PROPERTY_BACKGROUND_POSITION_X: {
            log_debug("[CSS] Processing background-position-x property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->background) {
                span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
            }

            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                float pos_x = convert_lambda_length_to_px(value, lycon, prop_id);
                log_debug("[CSS] background-position-x: %.2fpx", pos_x);
                // TODO: Store position-x when BackgroundProp is extended
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                float pos_x_percent = value->data.percentage.value;
                log_debug("[CSS] background-position-x: %.2f%%", pos_x_percent);
                // TODO: Store position-x percentage when BackgroundProp is extended
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // Values: left, center, right
                log_debug("[CSS] background-position-x: %s", css_enum_info(value->data.keyword)->name);
                // TODO: Store position-x keyword when BackgroundProp is extended
            }
            break;
        }

        case CSS_PROPERTY_BACKGROUND_POSITION_Y: {
            log_debug("[CSS] Processing background-position-y property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->background) {
                span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
            }

            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                float pos_y = convert_lambda_length_to_px(value, lycon, prop_id);
                log_debug("[CSS] background-position-y: %.2fpx", pos_y);
                // TODO: Store position-y when BackgroundProp is extended
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                float pos_y_percent = value->data.percentage.value;
                log_debug("[CSS] background-position-y: %.2f%%", pos_y_percent);
                // TODO: Store position-y percentage when BackgroundProp is extended
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // Values: top, center, bottom
                log_debug("[CSS] background-position-y: %s", css_enum_info(value->data.keyword)->name);
                // TODO: Store position-y keyword when BackgroundProp is extended
            }
            break;
        }

        case CSS_PROPERTY_BACKGROUND_BLEND_MODE: {
            log_debug("[CSS] Processing background-blend-mode property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->background) {
                span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // Values: normal, multiply, screen, overlay, darken, lighten, etc.
                log_debug("[CSS] background-blend-mode: %s", css_enum_info(value->data.keyword)->name);
                // TODO: Store blend mode when BackgroundProp is extended
            }
            break;
        }

        case CSS_PROPERTY_BORDER_TOP_WIDTH: {
            log_debug("[CSS] Processing border-top-width property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->border->width.top_specificity) {
                break; // lower specificity, skip
            }

            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                float width = convert_lambda_length_to_px(value, lycon, prop_id);
                span->bound->border->width.top = width;
                span->bound->border->width.top_specificity = specificity;
                log_debug("[CSS] Border-top-width: %.2f px", width);
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                // unitless zero is valid for border-width per CSS spec
                float width = value->data.number.value;
                // per CSS spec, only unitless zero is valid (treated as 0px)
                if (width != 0.0f) {
                    log_debug("[CSS] Border-top-width: unitless %.2f (invalid, only 0 allowed)", width);
                    break;
                }
                span->bound->border->width.top = 0.0f;
                span->bound->border->width.top_specificity = specificity;
                log_debug("[CSS] Border-top-width: 0 (unitless zero)");
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // Keywords: thin (1px), medium (3px), thick (5px)
                CssEnum keyword = value->data.keyword;
                float width = 3.0f; // default to medium
                if (keyword == CSS_VALUE_THIN) width = 1.0f;
                else if (keyword == CSS_VALUE_THICK) width = 5.0f;
                span->bound->border->width.top = width;
                span->bound->border->width.top_specificity = specificity;
                const CssEnumInfo* info = css_enum_info(keyword);
                log_debug("[CSS] Border-top-width keyword: %s -> %.2f px", info ? info->name : "unknown", width);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_RIGHT_WIDTH: {
            log_debug("[CSS] Processing border-right-width property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->border->width.right_specificity) {
                break; // lower specificity, skip
            }

            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                float width = convert_lambda_length_to_px(value, lycon, prop_id);
                span->bound->border->width.right = width;
                span->bound->border->width.right_specificity = specificity;
                log_debug("[CSS] Border-right-width: %.2f px", width);
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                // unitless zero is valid for border-width per CSS spec
                float width = value->data.number.value;
                // per CSS spec, only unitless zero is valid (treated as 0px)
                if (width != 0.0f) {
                    log_debug("[CSS] Border-right-width: unitless %.2f (invalid, only 0 allowed)", width);
                    break;
                }
                span->bound->border->width.right = 0.0f;
                span->bound->border->width.right_specificity = specificity;
                log_debug("[CSS] Border-right-width: 0 (unitless zero)");
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum keyword = value->data.keyword;
                float width = 3.0f;
                if (keyword == CSS_VALUE_THIN) width = 1.0f;
                else if (keyword == CSS_VALUE_THICK) width = 5.0f;
                span->bound->border->width.right = width;
                span->bound->border->width.right_specificity = specificity;
                const CssEnumInfo* info = css_enum_info(keyword);
                log_debug("[CSS] Border-right-width keyword: %s -> %.2f px", info ? info->name : "unknown", width);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_BOTTOM_WIDTH: {
            log_debug("[CSS] Processing border-bottom-width property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->border->width.bottom_specificity) {
                break; // lower specificity, skip
            }

            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                float width = convert_lambda_length_to_px(value, lycon, prop_id);
                span->bound->border->width.bottom = width;
                span->bound->border->width.bottom_specificity = specificity;
                log_debug("[CSS] Border-bottom-width: %.2f px", width);
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                // unitless zero is valid for border-width per CSS spec
                float width = value->data.number.value;
                // per CSS spec, only unitless zero is valid (treated as 0px)
                if (width != 0.0f) {
                    log_debug("[CSS] Border-bottom-width: unitless %.2f (invalid, only 0 allowed)", width);
                    break;
                }
                span->bound->border->width.bottom = 0.0f;
                span->bound->border->width.bottom_specificity = specificity;
                log_debug("[CSS] Border-bottom-width: 0 (unitless zero)");
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum keyword = value->data.keyword;
                float width = 3.0f;
                if (keyword == CSS_VALUE_THIN) width = 1.0f;
                else if (keyword == CSS_VALUE_THICK) width = 5.0f;
                span->bound->border->width.bottom = width;
                span->bound->border->width.bottom_specificity = specificity;
                const CssEnumInfo* info = css_enum_info(keyword);
                log_debug("[CSS] Border-bottom-width keyword: %s -> %.2f px", info ? info->name : "unknown", width);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_LEFT_WIDTH: {
            log_debug("[CSS] Processing border-left-width property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->border->width.left_specificity) {
                break; // lower specificity, skip
            }

            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                float width = convert_lambda_length_to_px(value, lycon, prop_id);
                span->bound->border->width.left = width;
                span->bound->border->width.left_specificity = specificity;
                log_debug("[CSS] Border-left-width: %.2f px", width);
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                // unitless zero is valid for border-width per CSS spec
                float width = value->data.number.value;
                // per CSS spec, only unitless zero is valid (treated as 0px)
                if (width != 0.0f) {
                    log_debug("[CSS] Border-left-width: unitless %.2f (invalid, only 0 allowed)", width);
                    break;
                }
                span->bound->border->width.left = 0.0f;
                span->bound->border->width.left_specificity = specificity;
                log_debug("[CSS] Border-left-width: 0 (unitless zero)");
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum keyword = value->data.keyword;
                float width = 3.0f;
                if (keyword == CSS_VALUE_THIN) width = 1.0f;
                else if (keyword == CSS_VALUE_THICK) width = 5.0f;
                span->bound->border->width.left = width;
                span->bound->border->width.left_specificity = specificity;
                const CssEnumInfo* info = css_enum_info(keyword);
                log_debug("[CSS] Border-left-width keyword: %s -> %.2f px", info ? info->name : "unknown", width);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_TOP_STYLE: {
            log_debug("[CSS] Processing border-top-style property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum lexbor_val = value->data.keyword;
                span->bound->border->top_style = lexbor_val;
                const CssEnumInfo* info = css_enum_info(lexbor_val);
                log_debug("[CSS] Border-top-style: %s -> %d", info ? info->name : "unknown", lexbor_val);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_RIGHT_STYLE: {
            log_debug("[CSS] Processing border-right-style property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum lexbor_val = value->data.keyword;
                span->bound->border->right_style = lexbor_val;
                const CssEnumInfo* info = css_enum_info(lexbor_val);
                log_debug("[CSS] Border-right-style: %s -> %d", info ? info->name : "unknown", lexbor_val);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_BOTTOM_STYLE: {
            log_debug("[CSS] Processing border-bottom-style property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum lexbor_val = value->data.keyword;
                span->bound->border->bottom_style = lexbor_val;
                const CssEnumInfo* info = css_enum_info(lexbor_val);
                log_debug("[CSS] Border-bottom-style: %s -> %d", info ? info->name : "unknown", lexbor_val);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_LEFT_STYLE: {
            log_debug("[CSS] Processing border-left-style property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum lexbor_val = value->data.keyword;
                span->bound->border->left_style = lexbor_val;
                const CssEnumInfo* info = css_enum_info(lexbor_val);
                log_debug("[CSS] Border-left-style: %s -> %d", info ? info->name : "unknown", lexbor_val);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_TOP_COLOR: {
            log_debug("[CSS] Processing border-top-color property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }
            if (specificity >= span->bound->border->top_color_specificity) {
                span->bound->border->top_color = resolve_color_value(value);
                span->bound->border->top_color_specificity = specificity;
            }
            break;
        }
        case CSS_PROPERTY_BORDER_RIGHT_COLOR: {
            log_debug("[CSS] Processing border-right-color property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }
            if (specificity >= span->bound->border->right_color_specificity) {
                span->bound->border->right_color = resolve_color_value(value);
                span->bound->border->right_color_specificity = specificity;
            }
            break;
        }
        case CSS_PROPERTY_BORDER_BOTTOM_COLOR: {
            log_debug("[CSS] Processing border-bottom-color property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }
            if (specificity >= span->bound->border->bottom_color_specificity) {
                span->bound->border->bottom_color = resolve_color_value(value);
                span->bound->border->bottom_color_specificity = specificity;
            }
            break;
        }
        case CSS_PROPERTY_BORDER_LEFT_COLOR: {
            log_debug("[CSS] Processing border-left-color property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }
            if (specificity >= span->bound->border->left_color_specificity) {
                span->bound->border->left_color = resolve_color_value(value);
                span->bound->border->left_color_specificity = specificity;
            }
            break;
        }

        case CSS_PROPERTY_BORDER: {
            log_debug("[CSS] Processing border shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // Border shorthand: <width> <style> <color> (any order)
            // Parse values from the list or single value
            float border_width = -1.0f;  CssEnum border_style = CSS_VALUE__UNDEF;  Color border_color = {0};

            if (value->type == CSS_VALUE_TYPE_LIST) {
                // Multiple values
                size_t count = value->data.list.count;
                CssValue** values = value->data.list.values;

                for (size_t i = 0; i < count; i++) {
                    CssValue* val = values[i];
                    if (val->type == CSS_VALUE_TYPE_LENGTH) {
                        // Width - convert to pixels
                        border_width = convert_lambda_length_to_px(val, lycon, prop_id);
                    }
                    else if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                        // Could be width keyword, style, or color
                        CssEnum keyword = val->data.keyword;
                        if (keyword == CSS_VALUE_THIN) {
                            border_width = 1.0f;
                        } else if (keyword == CSS_VALUE_MEDIUM) {
                            border_width = 3.0f;
                        } else if (keyword == CSS_VALUE_THICK) {
                            border_width = 5.0f;
                        } else if (keyword == CSS_VALUE_SOLID || keyword == CSS_VALUE_DASHED ||
                                   keyword == CSS_VALUE_DOTTED || keyword == CSS_VALUE_DOUBLE ||
                                   keyword == CSS_VALUE_GROOVE || keyword == CSS_VALUE_RIDGE ||
                                   keyword == CSS_VALUE_INSET || keyword == CSS_VALUE_OUTSET ||
                                   keyword == CSS_VALUE_NONE || keyword == CSS_VALUE_HIDDEN) {
                            // Style keyword
                            border_style = keyword;
                        } else {
                            // Color keyword
                            border_color = color_name_to_rgb(keyword);
                        }
                    }
                    else if (val->type == CSS_VALUE_TYPE_COLOR) {
                        // Color
                        log_debug("[CSS] Border color value type: %d", val->data.color.type);
                        if (val->data.color.type == CSS_COLOR_RGB) {
                            border_color.r = val->data.color.data.rgba.r;
                            border_color.g = val->data.color.data.rgba.g;
                            border_color.b = val->data.color.data.rgba.b;
                            border_color.a = val->data.color.data.rgba.a;
                        }
                    }
                }
            } else {
                // Single value
                if (value->type == CSS_VALUE_TYPE_LENGTH) {
                    border_width = convert_lambda_length_to_px(value, lycon, prop_id);
                } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                    CssEnum keyword = value->data.keyword;
                    if (keyword == CSS_VALUE_THIN) {
                        border_width = 1.0f;
                    } else if (keyword == CSS_VALUE_MEDIUM) {
                        border_width = 3.0f;
                    } else if (keyword == CSS_VALUE_THICK) {
                        border_width = 5.0f;
                    } else if (keyword == CSS_VALUE_SOLID || keyword == CSS_VALUE_DASHED ||
                               keyword == CSS_VALUE_DOTTED || keyword == CSS_VALUE_DOUBLE ||
                               keyword == CSS_VALUE_GROOVE || keyword == CSS_VALUE_RIDGE ||
                               keyword == CSS_VALUE_INSET || keyword == CSS_VALUE_OUTSET ||
                               keyword == CSS_VALUE_NONE || keyword == CSS_VALUE_HIDDEN) {
                        border_style = keyword;
                    } else {
                        border_color = color_name_to_rgb(keyword);
                    }
                } else if (value->type == CSS_VALUE_TYPE_COLOR) {
                    if (value->data.color.type == CSS_COLOR_RGB) {
                        border_color.r = value->data.color.data.rgba.r;
                        border_color.g = value->data.color.data.rgba.g;
                        border_color.b = value->data.color.data.rgba.b;
                        border_color.a = value->data.color.data.rgba.a;
                    }
                }
            }

            // Apply to all 4 sides
            if (border_width >= 0) {
                span->bound->border->width.top = border_width;
                span->bound->border->width.right = border_width;
                span->bound->border->width.bottom = border_width;
                span->bound->border->width.left = border_width;
                span->bound->border->width.top_specificity = specificity;
                span->bound->border->width.right_specificity = specificity;
                span->bound->border->width.bottom_specificity = specificity;
                span->bound->border->width.left_specificity = specificity;
                log_debug("[CSS] Border width (all sides): %.2f px", border_width);
            }
            if (border_style >= 0) {
                span->bound->border->top_style = border_style;
                span->bound->border->right_style = border_style;
                span->bound->border->bottom_style = border_style;
                span->bound->border->left_style = border_style;
                log_debug("[CSS] Border style (all sides): %d", border_style);
            }
            if (border_color.c != 0) {
                span->bound->border->top_color = border_color;
                span->bound->border->right_color = border_color;
                span->bound->border->bottom_color = border_color;
                span->bound->border->left_color = border_color;
                span->bound->border->top_color_specificity = specificity;
                span->bound->border->right_color_specificity = specificity;
                span->bound->border->bottom_color_specificity = specificity;
                span->bound->border->left_color_specificity = specificity;
                log_debug("[CSS] Border color (all sides): 0x%08X", border_color.c);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_TOP: {
            log_debug("[CSS] Processing border-top shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }
            MultiValue border = {0};
            set_multi_value( &border, value);
            if (border.style) {
                span->bound->border->top_style = border.style->data.keyword;
                span->bound->border->top_style_specificity = specificity;
            }
            else if (border.length) {
                span->bound->border->width.top = resolve_length_value(lycon, CSS_PROPERTY_BORDER_TOP_WIDTH, border.length);
                span->bound->border->width.top_specificity = specificity;
            }
            else if (border.color) {
                span->bound->border->top_color = resolve_color_value(border.color);
                span->bound->border->top_color_specificity = specificity;
            }
            break;
        }
        case CSS_PROPERTY_BORDER_RIGHT: {
            log_debug("[CSS] Processing border-right shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }
            MultiValue border = {0};
            set_multi_value( &border, value);
            if (border.style) {
                span->bound->border->right_style = border.style->data.keyword;
                span->bound->border->right_style_specificity = specificity;
            }
            else if (border.length) {
                span->bound->border->width.right = resolve_length_value(lycon, CSS_PROPERTY_BORDER_RIGHT_WIDTH, border.length);
                span->bound->border->width.right_specificity = specificity;
            }
            else if (border.color) {
                span->bound->border->right_color = resolve_color_value(border.color);
                span->bound->border->right_color_specificity = specificity;
            }
            break;
        }
        case CSS_PROPERTY_BORDER_BOTTOM: {
            log_debug("[CSS] Processing border-bottom shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }
            MultiValue border = {0};
            set_multi_value( &border, value);
            if (border.style) {
                span->bound->border->bottom_style = border.style->data.keyword;
                span->bound->border->bottom_style_specificity = specificity;
            }
            else if (border.length) {
                span->bound->border->width.bottom = resolve_length_value(lycon, CSS_PROPERTY_BORDER_BOTTOM_WIDTH, border.length);
                span->bound->border->width.bottom_specificity = specificity;
            }
            else if (border.color) {
                span->bound->border->bottom_color = resolve_color_value(border.color);
            }
            break;
        }
        case CSS_PROPERTY_BORDER_LEFT: {
            log_debug("[CSS] Processing border-left shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }
            MultiValue border = {0};
            set_multi_value( &border, value);
            if (border.style) {
                span->bound->border->left_style = border.style->data.keyword;
                span->bound->border->left_style_specificity = specificity;
            }
            else if (border.length) {
                span->bound->border->width.left = resolve_length_value(lycon, CSS_PROPERTY_BORDER_LEFT_WIDTH, border.length);
                span->bound->border->width.left_specificity = specificity;
            }
            else if (border.color) {
                span->bound->border->left_color = resolve_color_value(border.color);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_STYLE: {
            log_debug("[CSS] Processing border-style shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // CSS border-style shorthand: 1-4 keyword values
            // 1 value: all sides
            // 2 values: top/bottom, left/right
            // 3 values: top, left/right, bottom
            // 4 values: top, right, bottom, left
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // Single value - all sides get same style
                CssEnum border_style = value->data.keyword;
                if (border_style != CSS_VALUE__UNDEF) {
                    span->bound->border->top_style = border_style;
                    span->bound->border->right_style = border_style;
                    span->bound->border->bottom_style = border_style;
                    span->bound->border->left_style = border_style;
                    log_debug("[CSS] Border-style (all): %s -> 0x%04X", css_enum_info(value->data.keyword)->name, border_style);
                }
            }
            else if (value->type == CSS_VALUE_TYPE_LIST) {
                // Multi-value border-style
                size_t count = value->data.list.count;
                CssValue** values = value->data.list.values;

                if (count == 2 && values[0]->type == CSS_VALUE_TYPE_KEYWORD && values[1]->type == CSS_VALUE_TYPE_KEYWORD) {
                    // top/bottom, left/right
                    CssEnum vertical = values[0]->data.keyword;
                    CssEnum horizontal = values[1]->data.keyword;
                    span->bound->border->top_style = vertical;
                    span->bound->border->bottom_style = vertical;
                    span->bound->border->left_style = horizontal;
                    span->bound->border->right_style = horizontal;
                    const CssEnumInfo* info_v = css_enum_info(vertical);
                    const CssEnumInfo* info_h = css_enum_info(horizontal);
                    log_debug("[CSS] Border-style (2 values): %s %s", info_v ? info_v->name : "unknown", info_h ? info_h->name : "unknown");
                }
                else if (count == 3 && values[0]->type == CSS_VALUE_TYPE_KEYWORD &&
                           values[1]->type == CSS_VALUE_TYPE_KEYWORD && values[2]->type == CSS_VALUE_TYPE_KEYWORD) {
                    // top, left/right, bottom
                    CssEnum top = values[0]->data.keyword;
                    CssEnum horizontal = values[1]->data.keyword;
                    CssEnum bottom = values[2]->data.keyword;
                    span->bound->border->top_style = top;
                    span->bound->border->left_style = horizontal;
                    span->bound->border->right_style = horizontal;
                    span->bound->border->bottom_style = bottom;
                    const CssEnumInfo* info_t = css_enum_info(top);
                    const CssEnumInfo* info_h = css_enum_info(horizontal);
                    const CssEnumInfo* info_b = css_enum_info(bottom);
                    log_debug("[CSS] Border-style (3 values): %s %s %s", info_t ? info_t->name : "unknown", info_h ? info_h->name : "unknown", info_b ? info_b->name : "unknown");
                }
                else if (count == 4 && values[0]->type == CSS_VALUE_TYPE_KEYWORD &&
                           values[1]->type == CSS_VALUE_TYPE_KEYWORD && values[2]->type == CSS_VALUE_TYPE_KEYWORD &&
                           values[3]->type == CSS_VALUE_TYPE_KEYWORD) {
                    // top, right, bottom, left
                    CssEnum top = values[0]->data.keyword;
                    CssEnum right = values[1]->data.keyword;
                    CssEnum bottom = values[2]->data.keyword;
                    CssEnum left = values[3]->data.keyword;
                    span->bound->border->top_style = top;
                    span->bound->border->right_style = right;
                    span->bound->border->bottom_style = bottom;
                    span->bound->border->left_style = left;
                    log_debug("[CSS] Border-style (4 values): %s %s %s %s", values[0]->data.keyword, values[1]->data.keyword, values[2]->data.keyword, values[3]->data.keyword);
                }
            }
            break;
        }

        case CSS_PROPERTY_BORDER_WIDTH: {
            log_debug("[CSS] Processing border-width shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }
            resolve_spacing_prop(lycon, CSS_PROPERTY_BORDER_WIDTH, value, specificity, &span->bound->border->width);
            break;

        }

        case CSS_PROPERTY_BORDER_COLOR: {
            log_debug("[CSS] Processing border-color shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // CSS border-color shorthand: 1-4 color values
            // 1 value: all sides
            // 2 values: top/bottom, left/right
            // 3 values: top, left/right, bottom
            // 4 values: top, right, bottom, left

            if (value->type == CSS_VALUE_TYPE_COLOR || value->type == CSS_VALUE_TYPE_KEYWORD) {
                // Single value - all sides get same color
                Color color = resolve_color_value(value);

                // Check specificity for each side before setting
                if (specificity >= span->bound->border->top_color_specificity) {
                    span->bound->border->top_color = color;
                    span->bound->border->top_color_specificity = specificity;
                }
                if (specificity >= span->bound->border->right_color_specificity) {
                    span->bound->border->right_color = color;
                    span->bound->border->right_color_specificity = specificity;
                }
                if (specificity >= span->bound->border->bottom_color_specificity) {
                    span->bound->border->bottom_color = color;
                    span->bound->border->bottom_color_specificity = specificity;
                }
                if (specificity >= span->bound->border->left_color_specificity) {
                    span->bound->border->left_color = color;
                    span->bound->border->left_color_specificity = specificity;
                }
                log_debug("[CSS] Border-color (all): 0x%08X", color.c);
            }
            else if (value->type == CSS_VALUE_TYPE_LIST) {
                // Multi-value border-color
                size_t count = value->data.list.count;
                CssValue** values = value->data.list.values;
                if (count == 2) {
                    // top/bottom, left/right
                    Color vertical = resolve_color_value(values[0]);
                    Color horizontal = resolve_color_value(values[1]);

                    // Check specificity for each side before setting
                    if (specificity >= span->bound->border->top_color_specificity) {
                        span->bound->border->top_color = vertical;
                        span->bound->border->top_color_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->bottom_color_specificity) {
                        span->bound->border->bottom_color = vertical;
                        span->bound->border->bottom_color_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->left_color_specificity) {
                        span->bound->border->left_color = horizontal;
                        span->bound->border->left_color_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->right_color_specificity) {
                        span->bound->border->right_color = horizontal;
                        span->bound->border->right_color_specificity = specificity;
                    }
                    log_debug("[CSS] Border-color (2 values): 0x%08X 0x%08X", vertical.c, horizontal.c);
                }
                else if (count == 3) {
                    // top, left/right, bottom
                    Color top = resolve_color_value(values[0]);
                    Color horizontal = resolve_color_value(values[1]);
                    Color bottom = resolve_color_value(values[2]);

                    // Check specificity for each side before setting
                    if (specificity >= span->bound->border->top_color_specificity) {
                        span->bound->border->top_color = top;
                        span->bound->border->top_color_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->left_color_specificity) {
                        span->bound->border->left_color = horizontal;
                        span->bound->border->left_color_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->right_color_specificity) {
                        span->bound->border->right_color = horizontal;
                        span->bound->border->right_color_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->bottom_color_specificity) {
                        span->bound->border->bottom_color = bottom;
                        span->bound->border->bottom_color_specificity = specificity;
                    }
                    log_debug("[CSS] Border-color (3 values): 0x%08X 0x%08X 0x%08X", top.c, horizontal.c, bottom.c);
                }
                else if (count == 4) {
                    // top, right, bottom, left
                    Color top = resolve_color_value(values[0]);
                    Color right = resolve_color_value(values[1]);
                    Color bottom = resolve_color_value(values[2]);
                    Color left = resolve_color_value(values[3]);

                    // Check specificity for each side before setting
                    if (specificity >= span->bound->border->top_color_specificity) {
                        span->bound->border->top_color = top;
                        span->bound->border->top_color_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->right_color_specificity) {
                        span->bound->border->right_color = right;
                        span->bound->border->right_color_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->bottom_color_specificity) {
                        span->bound->border->bottom_color = bottom;
                        span->bound->border->bottom_color_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->left_color_specificity) {
                        span->bound->border->left_color = left;
                        span->bound->border->left_color_specificity = specificity;
                    }
                    log_debug("[CSS] Border-color (4 values): 0x%08X 0x%08X 0x%08X 0x%08X", top.c, right.c, bottom.c, left.c);
                }
            }
            break;
        }

        case CSS_PROPERTY_BORDER_RADIUS: {
            log_debug("[CSS] Processing border-radius shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // CSS border-radius shorthand: 1-4 length values
            // 1 value: all corners
            // 2 values: top-left/bottom-right, top-right/bottom-left
            // 3 values: top-left, top-right/bottom-left, bottom-right
            // 4 values: top-left, top-right, bottom-right, bottom-left

            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                // Single value - all corners get same radius
                float radius = value->data.length.value;

                // Check specificity before setting each corner
                if (specificity >= span->bound->border->radius.tl_specificity) {
                    span->bound->border->radius.top_left = radius;
                    span->bound->border->radius.tl_specificity = specificity;
                }
                if (specificity >= span->bound->border->radius.tr_specificity) {
                    span->bound->border->radius.top_right = radius;
                    span->bound->border->radius.tr_specificity = specificity;
                }
                if (specificity >= span->bound->border->radius.br_specificity) {
                    span->bound->border->radius.bottom_right = radius;
                    span->bound->border->radius.br_specificity = specificity;
                }
                if (specificity >= span->bound->border->radius.bl_specificity) {
                    span->bound->border->radius.bottom_left = radius;
                    span->bound->border->radius.bl_specificity = specificity;
                }
                log_debug("[CSS] Border-radius (all): %.2f px", radius);
            } else if (value->type == CSS_VALUE_TYPE_LIST) {
                // Multi-value border-radius
                size_t count = value->data.list.count;
                CssValue** values = value->data.list.values;

                if (count == 2 && (values[0]->type == CSS_VALUE_TYPE_LENGTH || values[0]->type == CSS_VALUE_TYPE_NUMBER) &&
                           (values[1]->type == CSS_VALUE_TYPE_LENGTH || values[1]->type == CSS_VALUE_TYPE_NUMBER)) {
                    // top-left/bottom-right, top-right/bottom-left
                    float diagonal1 = (values[0]->type == CSS_VALUE_TYPE_LENGTH) ? values[0]->data.length.value : values[0]->data.number.value;
                    float diagonal2 = (values[1]->type == CSS_VALUE_TYPE_LENGTH) ? values[1]->data.length.value : values[1]->data.number.value;

                    if (specificity >= span->bound->border->radius.tl_specificity) {
                        span->bound->border->radius.top_left = diagonal1;
                        span->bound->border->radius.tl_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->radius.tr_specificity) {
                        span->bound->border->radius.top_right = diagonal2;
                        span->bound->border->radius.tr_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->radius.br_specificity) {
                        span->bound->border->radius.bottom_right = diagonal1;
                        span->bound->border->radius.br_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->radius.bl_specificity) {
                        span->bound->border->radius.bottom_left = diagonal2;
                        span->bound->border->radius.bl_specificity = specificity;
                    }
                    log_debug("[CSS] Border-radius (2 values): %.2f %.2f px", diagonal1, diagonal2);
                } else if (count == 3 && (values[0]->type == CSS_VALUE_TYPE_LENGTH || values[0]->type == CSS_VALUE_TYPE_NUMBER) &&
                           (values[1]->type == CSS_VALUE_TYPE_LENGTH || values[1]->type == CSS_VALUE_TYPE_NUMBER) &&
                           (values[2]->type == CSS_VALUE_TYPE_LENGTH || values[2]->type == CSS_VALUE_TYPE_NUMBER)) {
                    // top-left, top-right/bottom-left, bottom-right
                    float top_left = (values[0]->type == CSS_VALUE_TYPE_LENGTH) ? values[0]->data.length.value : values[0]->data.number.value;
                    float diagonal = (values[1]->type == CSS_VALUE_TYPE_LENGTH) ? values[1]->data.length.value : values[1]->data.number.value;
                    float bottom_right = (values[2]->type == CSS_VALUE_TYPE_LENGTH) ? values[2]->data.length.value : values[2]->data.number.value;

                    if (specificity >= span->bound->border->radius.tl_specificity) {
                        span->bound->border->radius.top_left = top_left;
                        span->bound->border->radius.tl_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->radius.tr_specificity) {
                        span->bound->border->radius.top_right = diagonal;
                        span->bound->border->radius.tr_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->radius.br_specificity) {
                        span->bound->border->radius.bottom_right = bottom_right;
                        span->bound->border->radius.br_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->radius.bl_specificity) {
                        span->bound->border->radius.bottom_left = diagonal;
                        span->bound->border->radius.bl_specificity = specificity;
                    }
                    log_debug("[CSS] Border-radius (3 values): %.2f %.2f %.2f px", top_left, diagonal, bottom_right);
                } else if (count == 4 && (values[0]->type == CSS_VALUE_TYPE_LENGTH || values[0]->type == CSS_VALUE_TYPE_NUMBER) &&
                           (values[1]->type == CSS_VALUE_TYPE_LENGTH || values[1]->type == CSS_VALUE_TYPE_NUMBER) &&
                           (values[2]->type == CSS_VALUE_TYPE_LENGTH || values[2]->type == CSS_VALUE_TYPE_NUMBER) &&
                           (values[3]->type == CSS_VALUE_TYPE_LENGTH || values[3]->type == CSS_VALUE_TYPE_NUMBER)) {
                    // top-left, top-right, bottom-right, bottom-left
                    float top_left = (values[0]->type == CSS_VALUE_TYPE_LENGTH) ? values[0]->data.length.value : values[0]->data.number.value;
                    float top_right = (values[1]->type == CSS_VALUE_TYPE_LENGTH) ? values[1]->data.length.value : values[1]->data.number.value;
                    float bottom_right = (values[2]->type == CSS_VALUE_TYPE_LENGTH) ? values[2]->data.length.value : values[2]->data.number.value;
                    float bottom_left = (values[3]->type == CSS_VALUE_TYPE_LENGTH) ? values[3]->data.length.value : values[3]->data.number.value;

                    if (specificity >= span->bound->border->radius.tl_specificity) {
                        span->bound->border->radius.top_left = top_left;
                        span->bound->border->radius.tl_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->radius.tr_specificity) {
                        span->bound->border->radius.top_right = top_right;
                        span->bound->border->radius.tr_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->radius.br_specificity) {
                        span->bound->border->radius.bottom_right = bottom_right;
                        span->bound->border->radius.br_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->radius.bl_specificity) {
                        span->bound->border->radius.bottom_left = bottom_left;
                        span->bound->border->radius.bl_specificity = specificity;
                    }
                    log_debug("[CSS] Border-radius (4 values): %.2f %.2f %.2f %.2f px", top_left, top_right, bottom_right, bottom_left);
                }
            }
            break;
        }

        // ===== GROUP 15: Additional Border Properties =====

        case CSS_PROPERTY_BORDER_TOP_LEFT_RADIUS: {
            log_debug("[CSS] Processing border-top-left-radius property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }
            if (specificity >= span->bound->border->radius.tl_specificity) {
                float radius = resolve_length_value(lycon, prop_id, value);
                span->bound->border->radius.top_left = radius;
                span->bound->border->radius.tl_specificity = specificity;
            }
            break;
        }
        case CSS_PROPERTY_BORDER_TOP_RIGHT_RADIUS: {
            log_debug("[CSS] Processing border-top-right-radius property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }
            if (specificity >= span->bound->border->radius.tr_specificity) {
                float radius = resolve_length_value(lycon, prop_id, value);
                span->bound->border->radius.top_right = radius;
                span->bound->border->radius.tr_specificity = specificity;
            }
            break;
        }
        case CSS_PROPERTY_BORDER_BOTTOM_RIGHT_RADIUS: {
            log_debug("[CSS] Processing border-bottom-right-radius property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }
            if (specificity >= span->bound->border->radius.br_specificity) {
                float radius = resolve_length_value(lycon, prop_id, value);
                span->bound->border->radius.bottom_right = radius;
                span->bound->border->radius.br_specificity = specificity;
            }
            break;
        }
        case CSS_PROPERTY_BORDER_BOTTOM_LEFT_RADIUS: {
            log_debug("[CSS] Processing border-bottom-left-radius property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }
            if (specificity >= span->bound->border->radius.bl_specificity) {
                float radius = resolve_length_value(lycon, prop_id, value);
                span->bound->border->radius.bottom_left = radius;
                span->bound->border->radius.bl_specificity = specificity;
            }
            break;
        }

        // ===== GROUP 4: Layout Properties =====
        case CSS_PROPERTY_DISPLAY: {
            log_debug("[CSS] css display property should have been resolved earlier");
            // nothing to do here
            break;
        }

        case CSS_PROPERTY_POSITION: {
            log_debug("[CSS] Processing position property");
            if (!block) break;
            if (!block->position) {
                block->position = (PositionProp*)alloc_prop(lycon, sizeof(PositionProp));
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum lexbor_val = value->data.keyword;
                block->position->position = lexbor_val;
                const CssEnumInfo* info = css_enum_info(lexbor_val);
                log_debug("[CSS] Position: %s -> %d", info ? info->name : "unknown", lexbor_val);
            }
            break;
        }

        case CSS_PROPERTY_TOP: {
            log_debug("[CSS] Processing top property");
            if (!block) break;
            if (!block->position) {
                block->position = (PositionProp*)alloc_prop(lycon, sizeof(PositionProp));
            }
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {  // ignore 'auto' or any other keyword
                block->position->has_top = false;
            } else {
                block->position->top = resolve_length_value(lycon, CSS_PROPERTY_TOP, value);
                block->position->has_top = true;
            }
            break;
        }
        case CSS_PROPERTY_LEFT: {
            log_debug("[CSS] Processing left property");
            if (!block) break;
            if (!block->position) {
                block->position = (PositionProp*)alloc_prop(lycon, sizeof(PositionProp));
            }
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {  // ignore 'auto' or any other keyword
                block->position->has_left = false;
            } else {
                block->position->left = resolve_length_value(lycon, CSS_PROPERTY_LEFT, value);
                block->position->has_left = true;
            }
            break;
        }
        case CSS_PROPERTY_RIGHT: {
            log_debug("[CSS] Processing right property");
            if (!block) break;
            if (!block->position) {
                block->position = (PositionProp*)alloc_prop(lycon, sizeof(PositionProp));
            }
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {  // ignore 'auto' or any other keyword
                block->position->has_right = false;
            } else {
                block->position->right = resolve_length_value(lycon, CSS_PROPERTY_RIGHT, value);
                block->position->has_right = true;
            }
            break;
        }
        case CSS_PROPERTY_BOTTOM: {
            log_debug("[CSS] Processing bottom property");
            if (!block) break;
            if (!block->position) {
                block->position = (PositionProp*)alloc_prop(lycon, sizeof(PositionProp));
            }
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {  // ignore 'auto' or any other keyword
                block->position->has_bottom = false;
            } else {
                block->position->bottom = resolve_length_value(lycon, CSS_PROPERTY_BOTTOM, value);
                block->position->has_bottom = true;
            }
            break;
        }

        case CSS_PROPERTY_Z_INDEX: {
            log_debug("[CSS] Processing z-index property");
            if (!block) break;
            if (!block->position) {
                block->position = (PositionProp*)alloc_prop(lycon, sizeof(PositionProp));
            }

            if (value->type == CSS_VALUE_TYPE_NUMBER || value->type == CSS_VALUE_TYPE_INTEGER) {
                int z = (int)value->data.number.value;
                block->position->z_index = z;
                log_debug("[CSS] Z-index: %d", z);
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // 'auto' keyword - typically means z-index = 0
                log_debug("[CSS] Z-index: auto");
                block->position->z_index = 0;
            }
            break;
        }

        // ===== GROUP 7: Float and Clear =====

        case CSS_PROPERTY_FLOAT: {
            log_debug("[CSS] Processing float property");
            if (!block) break;
            if (!block->position) {
                block->position = (PositionProp*)alloc_prop(lycon, sizeof(PositionProp));
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum float_value = value->data.keyword;
                if (float_value > 0) {
                    block->position->float_prop = float_value;
                    const CssEnumInfo* info = css_enum_info(float_value);
                    log_debug("[CSS] Float: %s -> 0x%04X", info ? info->name : "unknown", float_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_CLEAR: {
            log_debug("[CSS] Processing clear property");
            if (!block) break;
            if (!block->position) {
                block->position = (PositionProp*)alloc_prop(lycon, sizeof(PositionProp));
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum clear_value = value->data.keyword;
                if (clear_value > 0) {
                    block->position->clear = clear_value;
                    const CssEnumInfo* info = css_enum_info(clear_value);
                    log_debug("[CSS] Clear: %s -> 0x%04X", info ? info->name : "unknown", clear_value);
                }
            }
            break;
        }

        // ===== GROUP 8: Overflow Properties =====

        case CSS_PROPERTY_OVERFLOW: {
            log_debug("[CSS] Processing overflow property (sets both x and y)");
            if (!block) break;
            if (!block->scroller) {
                block->scroller = (ScrollProp*)alloc_prop(lycon, sizeof(ScrollProp));
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum overflow_value = value->data.keyword;
                if (overflow_value > 0) {
                    block->scroller->overflow_x = overflow_value;
                    block->scroller->overflow_y = overflow_value;
                    log_debug("[CSS] Overflow: %s -> 0x%04X (both x and y)", css_enum_info(value->data.keyword)->name, overflow_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_OVERFLOW_X: {
            log_debug("[CSS] Processing overflow-x property");
            if (!block) break;
            if (!block->scroller) {
                block->scroller = (ScrollProp*)alloc_prop(lycon, sizeof(ScrollProp));
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum overflow_value = value->data.keyword;
                if (overflow_value > 0) {
                    block->scroller->overflow_x = overflow_value;
                    log_debug("[CSS] Overflow-x: %s -> 0x%04X", css_enum_info(value->data.keyword)->name, overflow_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_OVERFLOW_Y: {
            log_debug("[CSS] Processing overflow-y property");
            if (!block) break;
            if (!block->scroller) {
                block->scroller = (ScrollProp*)alloc_prop(lycon, sizeof(ScrollProp));
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum overflow_value = value->data.keyword;
                if (overflow_value > 0) {
                    block->scroller->overflow_y = overflow_value;
                    log_debug("[CSS] Overflow-y: %s -> 0x%04X", css_enum_info(value->data.keyword)->name, overflow_value);
                }
            }
            break;
        }

        // ===== GROUP 9: White-space Property =====

        case CSS_PROPERTY_WHITE_SPACE: {
            log_debug("[CSS] Processing white-space property");
            if (!block || !block->blk) {
                if (block) {
                    block->blk = alloc_block_prop(lycon);
                } else {
                    break; // inline elements don't have white-space
                }
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum whitespace_value = value->data.keyword;
                if (whitespace_value > 0) {
                    block->blk->white_space = whitespace_value;
                    log_debug("[CSS] White-space: %s -> 0x%04X", css_enum_info(value->data.keyword)->name, whitespace_value);
                }
            }
            break;
        }

        // ===== GROUP 10: Visibility and Opacity =====

        case CSS_PROPERTY_VISIBILITY: {
            log_debug("[CSS] Processing visibility property");
            // Visibility applies to all elements, stored in ViewSpan
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum visibility_value = value->data.keyword;
                if (visibility_value > 0) {
                    span->visibility = visibility_value;
                    log_debug("[CSS] Visibility: %s -> 0x%04X", css_enum_info(value->data.keyword)->name, visibility_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_OPACITY: {
            log_debug("[CSS] Processing opacity property");
            if (!span->in_line) {
                span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
            }

            if (value->type == CSS_VALUE_TYPE_NUMBER) {
                float opacity = value->data.number.value;
                // Clamp opacity to 0.0 - 1.0 range
                if (opacity < 0.0f) opacity = 0.0f;
                if (opacity > 1.0f) opacity = 1.0f;
                span->in_line->opacity = opacity;
                log_debug("[CSS] Opacity: %.2f", opacity);
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                float opacity = value->data.percentage.value / 100.0f;
                // Clamp opacity to 0.0 - 1.0 range
                if (opacity < 0.0f) opacity = 0.0f;
                if (opacity > 1.0f) opacity = 1.0f;
                span->in_line->opacity = opacity;
                log_debug("[CSS] Opacity: %.2f%% -> %.2f", value->data.percentage.value, opacity);
            }
            break;
        }

        case CSS_PROPERTY_CLIP: {
            log_debug("[CSS] Processing clip property");
            if (!block) break;
            if (!block->scroller) {
                block->scroller = (ScrollProp*)alloc_prop(lycon, sizeof(ScrollProp));
            }

            // CSS clip property uses rect(top, right, bottom, left) syntax
            // For now, just log that it's present. Full rect parsing would be complex.
            // The clip field exists in ScrollProp as a Bound structure
            log_debug("[CSS] Clip property detected (rect parsing not yet implemented)");
            block->scroller->has_clip = true;
            // TODO: Parse rect() values and set block->scroller->clip bounds
            break;
        }

        // ===== GROUP 11: Box Sizing =====

        case CSS_PROPERTY_BOX_SIZING: {
            log_debug("[CSS] Processing box-sizing property");
            if (!block || !block->blk) {
                if (block) {
                    block->blk = alloc_block_prop(lycon);
                } else {
                    break; // inline elements don't have box-sizing
                }
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum boxsizing_value = value->data.keyword;
                if (boxsizing_value > 0) {
                    block->blk->box_sizing = boxsizing_value;
                    log_debug("[CSS] Box-sizing: %s -> 0x%04X", css_enum_info(value->data.keyword)->name, boxsizing_value);
                }
            }
            break;
        }

        // ===== GROUP 12: Advanced Typography Properties =====

        case CSS_PROPERTY_FONT_STYLE: {
            log_debug("[CSS] Processing font-style property");
            if (!span->font) {
                log_debug("[CSS] font-style: FontProp is NULL");
                break;
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum lexbor_value = value->data.keyword;
                if (lexbor_value > 0) {
                    span->font->font_style = lexbor_value;
                    log_debug("[CSS] font-style: %s -> 0x%04X", css_enum_info(value->data.keyword)->name, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_TEXT_TRANSFORM: {
            log_debug("[CSS] Processing text-transform property");
            if (!block || !block->blk) {
                if (block) {
                    block->blk = alloc_block_prop(lycon);
                } else {
                    log_debug("[CSS] text-transform: Cannot apply to inline element without block context");
                    break;
                }
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum lexbor_value = value->data.keyword;
                if (lexbor_value > 0) {
                    // Note: Adding text_transform field to BlockProp would be needed
                    // For now, log the value that would be set
                    log_debug("[CSS] text-transform: %s -> 0x%04X (field not yet added to BlockProp)",
                             css_enum_info(value->data.keyword)->name, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_TEXT_OVERFLOW: {
            log_debug("[CSS] Processing text-overflow property");
            if (!block || !block->blk) {
                if (block) {
                    block->blk = alloc_block_prop(lycon);
                } else {
                    log_debug("[CSS] text-overflow: Cannot apply to inline element without block context");
                    break;
                }
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum lexbor_value = value->data.keyword;
                if (lexbor_value > 0) {
                    // Note: Adding text_overflow field to BlockProp would be needed
                    log_debug("[CSS] text-overflow: %s -> 0x%04X (field not yet added to BlockProp)",
                             css_enum_info(value->data.keyword)->name, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_WORD_BREAK: {
            log_debug("[CSS] Processing word-break property");
            if (!block || !block->blk) {
                if (block) {
                    block->blk = alloc_block_prop(lycon);
                } else {
                    log_debug("[CSS] word-break: Cannot apply to inline element without block context");
                    break;
                }
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum lexbor_value = value->data.keyword;
                if (lexbor_value > 0) {
                    // Note: Adding word_break field to BlockProp would be needed
                    log_debug("[CSS] word-break: %s -> 0x%04X (field not yet added to BlockProp)",
                             css_enum_info(value->data.keyword)->name, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_WORD_WRAP: {
            log_debug("[CSS] Processing word-wrap property");
            if (!block || !block->blk) {
                if (block) {
                    block->blk = alloc_block_prop(lycon);
                } else {
                    log_debug("[CSS] word-wrap: Cannot apply to inline element without block context");
                    break;
                }
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum lexbor_value = value->data.keyword;
                if (lexbor_value > 0) {
                    // Note: Adding word_wrap field to BlockProp would be needed
                    log_debug("[CSS] word-wrap: %s -> 0x%04X (field not yet added to BlockProp)",
                             css_enum_info(value->data.keyword)->name, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_FONT_VARIANT: {
            log_debug("[CSS] Processing font-variant property");
            if (!span->font) {
                log_debug("[CSS] font-variant: FontProp is NULL");
                break;
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum lexbor_value = value->data.keyword;
                if (lexbor_value > 0) {
                    // Note: Adding font_variant field to FontProp would be needed
                    log_debug("[CSS] font-variant: %s -> 0x%04X (field not yet added to FontProp)",
                             css_enum_info(value->data.keyword)->name, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_LETTER_SPACING: {
            log_debug("[CSS] Processing letter-spacing property");
            if (!span->font) {
                log_debug("[CSS] letter-spacing: FontProp is NULL");
                break;
            }

            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                float spacing = convert_lambda_length_to_px(value, lycon, prop_id);
                // Note: Adding letter_spacing field to FontProp would be needed
                log_debug("[CSS] letter-spacing: %.2fpx (field not yet added to FontProp)", spacing);
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NORMAL) {
                log_debug("[CSS] letter-spacing: normal -> 0px (field not yet added to FontProp)");
            }
            break;
        }

        case CSS_PROPERTY_WORD_SPACING: {
            log_debug("[CSS] Processing word-spacing property");
            if (!span->font) {
                log_debug("[CSS] word-spacing: FontProp is NULL");
                break;
            }

            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                float spacing = convert_lambda_length_to_px(value, lycon, prop_id);
                // Note: Adding word_spacing field to FontProp would be needed
                log_debug("[CSS] word-spacing: %.2fpx (field not yet added to FontProp)", spacing);
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NORMAL) {
                log_debug("[CSS] word-spacing: normal -> 0px (field not yet added to FontProp)");
            }
            break;
        }

        case CSS_PROPERTY_TEXT_SHADOW: {
            log_debug("[CSS] Processing text-shadow property");
            if (!span->font) {
                log_debug("[CSS] text-shadow: FontProp is NULL");
                break;
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                log_debug("[CSS] text-shadow: none (field not yet added to FontProp)");
            } else {
                // TODO: Parse shadow offset, blur, and color
                // For now, just log that shadow is set
                log_debug("[CSS] text-shadow: complex value (needs full shadow parsing and field not yet added)");
            }
            break;
        }

        // ===== GROUP 13: Flexbox Properties =====

        case CSS_PROPERTY_FLEX_DIRECTION: {
            log_debug("[CSS] Processing flex-direction property");
            if (!block) {
                log_debug("[CSS] flex-direction: Cannot apply to non-block element");
                break;
            }

            // Allocate FlexProp if needed (same as resolve_style.cpp)
            alloc_flex_prop(lycon, block);

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum lexbor_value = value->data.keyword;
                if (lexbor_value > 0) {
                    block->embed->flex->direction = lexbor_value;
                    log_debug("[CSS] flex-direction: %s -> 0x%04X", css_enum_info(value->data.keyword)->name, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_FLEX_WRAP: {
            log_debug("[CSS] Processing flex-wrap property");
            if (!block) {
                log_debug("[CSS] flex-wrap: Cannot apply to non-block element");
                break;
            }

            // Allocate FlexProp if needed
            alloc_flex_prop(lycon, block);

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum lexbor_value = value->data.keyword;
                if (lexbor_value > 0) {
                    block->embed->flex->wrap = lexbor_value;
                    log_debug("[CSS] flex-wrap: %s -> 0x%04X", css_enum_info(value->data.keyword)->name, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_JUSTIFY_CONTENT: {
            log_debug("[CSS] Processing justify-content property");
            if (!block) {
                log_debug("[CSS] justify-content: Cannot apply to non-block element");
                break;
            }

            // Allocate FlexProp if needed
            alloc_flex_prop(lycon, block);

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum lexbor_value = value->data.keyword;
                if (lexbor_value > 0) {
                    block->embed->flex->justify = lexbor_value;
                    log_debug("[CSS] justify-content: %s -> 0x%04X", css_enum_info(value->data.keyword)->name, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_ALIGN_ITEMS: {
            log_debug("[CSS] Processing align-items property");
            if (!block) {
                log_debug("[CSS] align-items: Cannot apply to non-block element");
                break;
            }

            // Allocate FlexProp if needed
            alloc_flex_prop(lycon, block);

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum lexbor_value = value->data.keyword;
                if (lexbor_value > 0) {
                    block->embed->flex->align_items = lexbor_value;
                    log_debug("[CSS] align-items: %s -> 0x%04X", css_enum_info(value->data.keyword)->name, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_ALIGN_CONTENT: {
            log_debug("[CSS] Processing align-content property");
            if (!block) {
                log_debug("[CSS] align-content: Cannot apply to non-block element");
                break;
            }

            // Allocate FlexProp if needed
            alloc_flex_prop(lycon, block);

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum lexbor_value = value->data.keyword;
                if (lexbor_value > 0) {
                    block->embed->flex->align_content = lexbor_value;
                    log_debug("[CSS] align-content: %s -> 0x%04X", css_enum_info(value->data.keyword)->name, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_ROW_GAP: {
            log_debug("[CSS] Processing row-gap property");
            if (!block) {
                log_debug("[CSS] row-gap: Cannot apply to non-block element");
                break;
            }

            // Allocate FlexProp if needed
            alloc_flex_prop(lycon, block);

            if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_NUMBER) {
                float gap_value = convert_lambda_length_to_px(value, lycon, prop_id);
                block->embed->flex->row_gap = (int)gap_value;
                log_debug("[CSS] row-gap: %.2fpx", gap_value);
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                // row-gap percentage is relative to the container's width
                float gap_value = value->data.percentage.value;
                block->embed->flex->row_gap = (int)gap_value; // Store percentage value
                // TODO: might need a flag to indicate percentage vs absolute
                log_debug("[CSS] row-gap: %.2f%% (stored as: %d)", gap_value, block->embed->flex->row_gap);
            }
            break;
        }

        case CSS_PROPERTY_COLUMN_GAP: {
            log_debug("[CSS] Processing column-gap property");
            if (!block) {
                log_debug("[CSS] column-gap: Cannot apply to non-block element");
                break;
            }

            // Allocate FlexProp if needed
            alloc_flex_prop(lycon, block);

            if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_NUMBER) {
                float gap_value = convert_lambda_length_to_px(value, lycon, prop_id);
                block->embed->flex->column_gap = (int)gap_value;
                log_debug("[CSS] column-gap: %.2fpx", gap_value);
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                // column-gap percentage is relative to the container's width
                float gap_value = value->data.percentage.value;
                block->embed->flex->column_gap = (int)gap_value; // Store percentage value
                // TODO: might need a flag to indicate percentage vs absolute
                log_debug("[CSS] column-gap: %.2f%% (stored as: %d)", gap_value, block->embed->flex->column_gap);
            }
            break;
        }

        case CSS_PROPERTY_FLEX_GROW: {
            log_debug("[CSS] Processing flex-grow property");
            if (value->type == CSS_VALUE_TYPE_NUMBER) {
                float grow_value = (float)value->data.number.value;
                span->flex_grow = grow_value;
                log_debug("[CSS] flex-grow: %.2f", grow_value);
            }
            break;
        }

        case CSS_PROPERTY_FLEX_SHRINK: {
            log_debug("[CSS] Processing flex-shrink property");
            if (value->type == CSS_VALUE_TYPE_NUMBER) {
                float shrink_value = (float)value->data.number.value;
                span->flex_shrink = shrink_value;
                log_debug("[CSS] flex-shrink: %.2f", shrink_value);
            }
            break;
        }

        case CSS_PROPERTY_FLEX_BASIS: {
            log_debug("[CSS] Processing flex-basis property");
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_AUTO) {
                span->flex_basis = -1; // -1 indicates auto
                span->flex_basis_is_percent = false;
                log_debug("[CSS] flex-basis: auto");
            } else if (value->type == CSS_VALUE_TYPE_LENGTH) {
                float basis_value = convert_lambda_length_to_px(value, lycon, prop_id);
                span->flex_basis = (int)basis_value;
                span->flex_basis_is_percent = false;
                log_debug("[CSS] flex-basis: %.2fpx", basis_value);
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                span->flex_basis = (int)value->data.percentage.value;
                span->flex_basis_is_percent = true;
                log_debug("[CSS] flex-basis: %d%%", span->flex_basis);
            }
            break;
        }

        case CSS_PROPERTY_ORDER: {
            log_debug("[CSS] Processing order property");
            if (value->type == CSS_VALUE_TYPE_NUMBER || value->type == CSS_VALUE_TYPE_INTEGER) {
                int order_value = (int)value->data.number.value;
                span->order = order_value;
                log_debug("[CSS] order: %d", order_value);
            }
            break;
        }

        case CSS_PROPERTY_ALIGN_SELF: {
            log_debug("[CSS] Processing align-self property");
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum lexbor_value = value->data.keyword;
                if (lexbor_value > 0) {
                    span->align_self = lexbor_value;
                    log_debug("[CSS] align-self: %s -> 0x%04X", css_enum_info(value->data.keyword)->name, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_FLEX_FLOW: {
            log_debug("[CSS] Processing flex-flow shorthand property");
            if (!block) {
                log_debug("[CSS] flex-flow: Cannot apply to non-block element");
                break;
            }

            // Allocate FlexProp if needed
            alloc_flex_prop(lycon, block);

            // Note: flex-flow is a shorthand for flex-direction and flex-wrap
            // Would need to parse both values from the declaration
            // For now, just log
            log_debug("[CSS] flex-flow: shorthand parsing not yet fully implemented");
            break;
        }

        case CSS_PROPERTY_FLEX: {
            log_debug("[CSS] Processing flex shorthand property");
            // flex is a shorthand for flex-grow, flex-shrink, and flex-basis
            // Syntax: none | [ <'flex-grow'> <'flex-shrink'>? || <'flex-basis'> ]

            ViewSpan* span = (ViewSpan*)lycon->view;

            // Initialize with defaults
            float flex_grow = 1.0f;      // default when using shorthand
            float flex_shrink = 1.0f;    // default
            float flex_basis = -1.0f;    // auto
            bool flex_basis_is_percent = false;

            // Handle single keyword values
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                if (value->data.keyword == CSS_VALUE_NONE) {
                    flex_grow = 0;
                    flex_shrink = 0;
                    flex_basis = -1;  // auto
                    log_debug("[CSS] flex: none -> grow=0 shrink=0 basis=auto");
                } else if (value->data.keyword == CSS_VALUE_AUTO) {
                    flex_grow = 1;
                    flex_shrink = 1;
                    flex_basis = -1;  // auto
                    log_debug("[CSS] flex: auto -> grow=1 shrink=1 basis=auto");
                } else if (value->data.keyword == CSS_VALUE_INITIAL) {
                    flex_grow = 0;
                    flex_shrink = 1;
                    flex_basis = -1;  // auto
                    log_debug("[CSS] flex: initial -> grow=0 shrink=1 basis=auto");
                }

                span->flex_grow = flex_grow;
                span->flex_shrink = flex_shrink;
                span->flex_basis = flex_basis;
                span->flex_basis_is_percent = flex_basis_is_percent;
                break;
            }

            // Parse multi-value flex shorthand (e.g., "1 0 100px" or "2 1 50px")
            if (value->type == CSS_VALUE_TYPE_LIST) {
                size_t count = value->data.list.count;
                CssValue** values = value->data.list.values;

                int value_index = 0;
                bool found_basis = false;

                log_debug("[CSS] flex shorthand with %zu values", count);

                // Parse up to 3 values: grow, shrink, basis
                for (size_t i = 0; i < count && i < 3; i++) {
                    CssValue* val = values[i];

                    if (val->type == CSS_VALUE_TYPE_NUMBER) {
                        // Numbers are grow and shrink (unitless)
                        if (value_index == 0) {
                            flex_grow = (float)val->data.number.value;
                            log_debug("[CSS]   flex-grow: %.2f", flex_grow);
                            value_index++;
                        } else if (value_index == 1) {
                            flex_shrink = (float)val->data.number.value;
                            log_debug("[CSS]   flex-shrink: %.2f", flex_shrink);
                            value_index++;
                        }
                    } else if (val->type == CSS_VALUE_TYPE_LENGTH) {
                        // Length is basis
                        flex_basis = val->data.length.value;
                        flex_basis_is_percent = false;
                        found_basis = true;
                        log_debug("[CSS]   flex-basis: %.2fpx", flex_basis);
                    } else if (val->type == CSS_VALUE_TYPE_PERCENTAGE) {
                        // Percentage is basis
                        flex_basis = val->data.percentage.value;
                        flex_basis_is_percent = true;
                        found_basis = true;
                        log_debug("[CSS]   flex-basis: %.2f%%", flex_basis);
                    } else if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                        if (val->data.keyword == CSS_VALUE_AUTO) {
                            flex_basis = -1;  // auto
                            flex_basis_is_percent = false;
                            found_basis = true;
                            log_debug("[CSS]   flex-basis: auto");
                        }
                    }
                }

                // If only one number was provided, it's grow with implicit 1 0
                if (count == 1 && value_index == 1 && !found_basis) {
                    flex_shrink = 1.0f;
                    flex_basis = 0;  // 0px basis when single number
                    log_debug("[CSS] flex: <grow> -> grow=%.2f shrink=1 basis=0", flex_grow);
                }

                span->flex_grow = flex_grow;
                span->flex_shrink = flex_shrink;
                span->flex_basis = flex_basis;
                span->flex_basis_is_percent = flex_basis_is_percent;

                log_debug("[CSS] flex shorthand resolved: grow=%.2f shrink=%.2f basis=%.2f%s",
                         flex_grow, flex_shrink, flex_basis,
                         flex_basis_is_percent ? "%" : (flex_basis == -1 ? " (auto)" : "px"));
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                // Single number without list wrapper: just flex-grow
                flex_grow = (float)value->data.number.value;
                flex_shrink = 1.0f;
                flex_basis = 0;  // 0px when single unitless number

                span->flex_grow = flex_grow;
                span->flex_shrink = flex_shrink;
                span->flex_basis = flex_basis;
                span->flex_basis_is_percent = false;

                log_debug("[CSS] flex: %.2f -> grow=%.2f shrink=1 basis=0", flex_grow, flex_grow);
            }

            break;
        }

        // Animation Properties (Group 14)
        case CSS_PROPERTY_ANIMATION: {
            log_debug("[CSS] Processing animation shorthand property");
            // Note: Animation shorthand would be parsed into individual properties
            // For now, just log the value
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                log_debug("[CSS] animation: %s", css_enum_info(value->data.keyword)->name);
            }
            break;
        }

        case CSS_PROPERTY_ANIMATION_NAME: {
            log_debug("[CSS] Processing animation-name property");
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                if (value->data.keyword == CSS_VALUE_NONE) {
                    log_debug("[CSS] animation-name: none");
                } else {
                    const CssEnumInfo* info = css_enum_info(value->data.keyword);
                    log_debug("[CSS] animation-name: %s", info ? info->name : "unknown");
                }
            } else if (value->type == CSS_VALUE_TYPE_STRING) {
                log_debug("[CSS] animation-name: \"%s\"", value->data.string);
            }
            break;
        }

        case CSS_PROPERTY_ANIMATION_DURATION: {
            log_debug("[CSS] Processing animation-duration property");
            if (value->type == CSS_VALUE_TYPE_TIME) {
                float duration = (float)value->data.length.value;
                log_debug("[CSS] animation-duration: %.3fs", duration);
            }
            break;
        }

        case CSS_PROPERTY_ANIMATION_TIMING_FUNCTION: {
            log_debug("[CSS] Processing animation-timing-function property");
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum timing = value->data.keyword;
                if (timing > 0) {
                    const CssEnumInfo* info = css_enum_info(timing);
                    log_debug("[CSS] animation-timing-function: %s -> 0x%04X", info ? info->name : "unknown", timing);
                } else {
                    const CssEnumInfo* info = css_enum_info(timing);
                    log_debug("[CSS] animation-timing-function: %s", info ? info->name : "unknown");
                }
            }
            break;
        }

        case CSS_PROPERTY_ANIMATION_DELAY: {
            log_debug("[CSS] Processing animation-delay property");
            if (value->type == CSS_VALUE_TYPE_TIME) {
                float delay = (float)value->data.length.value;
                log_debug("[CSS] animation-delay: %.3fs", delay);
            }
            break;
        }

        case CSS_PROPERTY_ANIMATION_ITERATION_COUNT: {
            log_debug("[CSS] Processing animation-iteration-count property");
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                log_debug("[CSS] animation-iteration-count: %s", info ? info->name : "unknown");
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                float count = (float)value->data.number.value;
                log_debug("[CSS] animation-iteration-count: %.2f", count);
            }
            break;
        }

        case CSS_PROPERTY_ANIMATION_DIRECTION: {
            log_debug("[CSS] Processing animation-direction property");
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum direction = value->data.keyword;
                if (direction > 0) {
                    const CssEnumInfo* info = css_enum_info(direction);
                    log_debug("[CSS] animation-direction: %s -> 0x%04X", info ? info->name : "unknown", direction);
                } else {
                    const CssEnumInfo* info = css_enum_info(direction);
                    log_debug("[CSS] animation-direction: %s", info ? info->name : "unknown");
                }
            }
            break;
        }

        case CSS_PROPERTY_ANIMATION_FILL_MODE: {
            log_debug("[CSS] Processing animation-fill-mode property");
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum fill_mode = value->data.keyword;
                if (fill_mode > 0) {
                    const CssEnumInfo* info = css_enum_info(fill_mode);
                    log_debug("[CSS] animation-fill-mode: %s -> 0x%04X", info ? info->name : "unknown", fill_mode);
                } else {
                    const CssEnumInfo* info = css_enum_info(fill_mode);
                    log_debug("[CSS] animation-fill-mode: %s", info ? info->name : "unknown");
                }
            }
            break;
        }

        case CSS_PROPERTY_ANIMATION_PLAY_STATE: {
            log_debug("[CSS] Processing animation-play-state property");
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                log_debug("[CSS] animation-play-state: %s", info ? info->name : "unknown");
            }
            break;
        }

        // Table Properties (Group 17)
        case CSS_PROPERTY_TABLE_LAYOUT: {
            log_debug("[CSS] Processing table-layout property");
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum layout = value->data.keyword;
                if (layout == CSS_VALUE_AUTO) {
                    log_debug("[CSS] table-layout: auto");
                } else if (layout == CSS_VALUE_FIXED) {
                    log_debug("[CSS] table-layout: fixed");
                } else {
                    const CssEnumInfo* info = css_enum_info(layout);
                    log_debug("[CSS] table-layout: %s", info ? info->name : "unknown");
                }
            }
            break;
        }

        case CSS_PROPERTY_BORDER_COLLAPSE: {
            log_debug("[CSS] Processing border-collapse property");
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum collapse = value->data.keyword;
                if (collapse > 0) {
                    const CssEnumInfo* info = css_enum_info(collapse);
                    log_debug("[CSS] border-collapse: %s -> 0x%04X", info ? info->name : "unknown", collapse);
                } else {
                    const CssEnumInfo* info = css_enum_info(collapse);
                    log_debug("[CSS] border-collapse: %s", info ? info->name : "unknown");
                }
            }
            break;
        }

        case CSS_PROPERTY_BORDER_SPACING: {
            log_debug("[CSS] Processing border-spacing property");
            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                double spacing = convert_lambda_length_to_px(value, lycon, prop_id);
                log_debug("[CSS] border-spacing: %.2fpx", spacing);
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                log_debug("[CSS] border-spacing: %s", css_enum_info(value->data.keyword)->name);
            }
            break;
        }

        case CSS_PROPERTY_CAPTION_SIDE: {
            log_debug("[CSS] Processing caption-side property");
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum side = value->data.keyword;
                if (side == CSS_VALUE_TOP) {
                    log_debug("[CSS] caption-side: top");
                } else if (side == CSS_VALUE_BOTTOM) {
                    log_debug("[CSS] caption-side: bottom");
                } else {
                    const CssEnumInfo* info = css_enum_info(side);
                    log_debug("[CSS] caption-side: %s", info ? info->name : "unknown");
                }
            }
            break;
        }

        case CSS_PROPERTY_EMPTY_CELLS: {
            log_debug("[CSS] Processing empty-cells property");
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum cells = value->data.keyword;
                if (cells > 0) {
                    const CssEnumInfo* info = css_enum_info(cells);
                    log_debug("[CSS] empty-cells: %s -> 0x%04X", info ? info->name : "unknown", cells);
                } else {
                    const CssEnumInfo* info = css_enum_info(cells);
                    log_debug("[CSS] empty-cells: %s", info ? info->name : "unknown");
                }
            }
            break;
        }

        // List Properties (Group 18)
        case CSS_PROPERTY_LIST_STYLE_TYPE: {
            log_debug("[CSS] Processing list-style-type property");
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum type = value->data.keyword;
                if (type > 0) {
                    const CssEnumInfo* info = css_enum_info(type);
                    log_debug("[CSS] list-style-type: %s -> 0x%04X", info ? info->name : "unknown", type);
                } else {
                    const CssEnumInfo* info = css_enum_info(type);
                    log_debug("[CSS] list-style-type: %s", info ? info->name : "unknown");
                }
            }
            break;
        }

        case CSS_PROPERTY_LIST_STYLE_POSITION: {
            log_debug("[CSS] Processing list-style-position property");
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum position = value->data.keyword;
                if (position > 0) {
                    const CssEnumInfo* info = css_enum_info(position);
                    log_debug("[CSS] list-style-position: %s -> 0x%04X", info ? info->name : "unknown", position);
                } else {
                    const CssEnumInfo* info = css_enum_info(position);
                    log_debug("[CSS] list-style-position: %s", info ? info->name : "unknown");
                }
            }
            break;
        }

        case CSS_PROPERTY_LIST_STYLE_IMAGE: {
            log_debug("[CSS] Processing list-style-image property");
            if (value->type == CSS_VALUE_TYPE_URL) {
                log_debug("[CSS] list-style-image: %s", value->data.url);
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                if (value->data.keyword == CSS_VALUE_NONE) {
                    log_debug("[CSS] list-style-image: none");
                } else {
                    const CssEnumInfo* info = css_enum_info(value->data.keyword);
                    log_debug("[CSS] list-style-image: %s", info ? info->name : "unknown");
                }
            }
            break;
        }

        case CSS_PROPERTY_LIST_STYLE: {
            log_debug("[CSS] Processing list-style shorthand property");
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                log_debug("[CSS] list-style: %s", info ? info->name : "unknown");
                // Note: Shorthand parsing would need more complex implementation
            }
            break;
        }

        case CSS_PROPERTY_COUNTER_RESET: {
            log_debug("[CSS] Processing counter-reset property");
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum reset = value->data.keyword;
                if (reset == CSS_VALUE_NONE) {
                    log_debug("[CSS] counter-reset: none");
                } else {
                    const CssEnumInfo* info = css_enum_info(reset);
                    log_debug("[CSS] counter-reset: %s", info ? info->name : "unknown");
                }
            }
            break;
        }

        case CSS_PROPERTY_COUNTER_INCREMENT: {
            log_debug("[CSS] Processing counter-increment property");
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum increment = value->data.keyword;
                if (increment == CSS_VALUE_NONE) {
                    log_debug("[CSS] counter-increment: none");
                } else {
                    const CssEnumInfo* info = css_enum_info(increment);
                    log_debug("[CSS] counter-increment: %s", info ? info->name : "unknown");
                }
            }
            break;
        }

       case CSS_PROPERTY_BACKGROUND: {
            // background shorthand can set background-color, background-image, etc.
            // simple case: single color value (e.g., "background: green;")
            if (value->type == CSS_VALUE_TYPE_COLOR || value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssDeclaration color_decl = *decl;
                color_decl.property_id = CSS_PROPERTY_BACKGROUND_COLOR;
                log_debug("[Lambda CSS Shorthand] Expanding background to background-color");
                resolve_lambda_css_property(CSS_PROPERTY_BACKGROUND_COLOR, &color_decl, lycon);
                return;
            }
            log_debug("[Lambda CSS Shorthand] Complex background shorthand not yet implemented");
            return;
        }

        case CSS_PROPERTY_GAP: {
            // gap shorthand: 1-2 values (row-gap column-gap)
            // If only one value is specified, it's used for both row and column gap
            log_debug("[Lambda CSS Shorthand] Expanding gap shorthand");

            if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_NUMBER) {
                // single value - use for both row-gap and column-gap
                log_debug("[Lambda CSS Shorthand] Expanding single-value gap to row-gap and column-gap");
                CssDeclaration gap_decl = *decl;
                gap_decl.property_id = CSS_PROPERTY_ROW_GAP;
                resolve_lambda_css_property(CSS_PROPERTY_ROW_GAP, &gap_decl, lycon);
                gap_decl.property_id = CSS_PROPERTY_COLUMN_GAP;
                resolve_lambda_css_property(CSS_PROPERTY_COLUMN_GAP, &gap_decl, lycon);
                return;
            } else if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count == 2) {
                // two values: row-gap column-gap
                log_debug("[Lambda CSS Shorthand] Expanding two-value gap");
                CssValue** values = value->data.list.values;

                CssDeclaration row_gap_decl = *decl;
                row_gap_decl.value = values[0];
                row_gap_decl.property_id = CSS_PROPERTY_ROW_GAP;
                resolve_lambda_css_property(CSS_PROPERTY_ROW_GAP, &row_gap_decl, lycon);

                CssDeclaration col_gap_decl = *decl;
                col_gap_decl.value = values[1];
                col_gap_decl.property_id = CSS_PROPERTY_COLUMN_GAP;
                resolve_lambda_css_property(CSS_PROPERTY_COLUMN_GAP, &col_gap_decl, lycon);
                return;
            }
            log_debug("[Lambda CSS Shorthand] Gap shorthand expansion complete");
            return;
        }

        default:
            // Unknown or unimplemented property
            log_debug("[CSS] Unimplemented property: %d", prop_id);
            break;
    }
}
