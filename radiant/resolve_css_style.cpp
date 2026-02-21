#include "layout.hpp"
#include "grid.hpp"
#include "form_control.hpp"
#include "font_face.h"  // for FontFaceDescriptor
#include "../lib/font/font.h"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/memtrack.h"
#include "../lib/str.h"
#include <string.h>
#include <strings.h>  // for strcasecmp
#include <cmath>

// Forward declaration for CSS variable lookup
static const CssValue* lookup_css_variable(LayoutContext* lycon, const char* var_name);

/**
 * Look up a CSS custom property (variable) value
 * Searches current element and ancestors (CSS variables inherit)
 * @param lycon Layout context containing current element
 * @param var_name Variable name (e.g., "--primary-color")
 * @return CssValue* if found, nullptr otherwise
 */
static const CssValue* lookup_css_variable(LayoutContext* lycon, const char* var_name) {
    if (!lycon || !lycon->view || !var_name) return nullptr;

    DomElement* element = (DomElement*)lycon->view;

    // Search up the DOM tree (CSS variables inherit)
    while (element) {
        // Check if this element has CSS variables
        if (element->css_variables) {
            CssCustomProp* var = element->css_variables;
            while (var) {
                if (var->name && strcmp(var->name, var_name) == 0) {
                    return var->value;
                }
                var = var->next;
            }
        }

        // Move to parent element
        if (element->parent && element->parent->is_element()) {
            element = (DomElement*)element->parent;
        } else {
            break;
        }
    }

    return nullptr;
}

// Helper: resolve var() function to get the actual CSS value
// Returns the resolved value, or the original value if not a var() function
// Recursively resolves nested var() calls
const CssValue* resolve_var_function(LayoutContext* lycon, const CssValue* value) {
    if (!value || value->type != CSS_VALUE_TYPE_FUNCTION) {
        return value;  // Not a function, return as-is
    }

    const CssFunction* func = value->data.function;
    if (!func || !func->name || strcmp(func->name, "var") != 0) {
        return value;  // Not a var() function, return as-is
    }

    // Extract variable name
    const char* var_name = nullptr;
    if (func->args && func->arg_count >= 1 && func->args[0]) {
        CssValue* first_arg = func->args[0];
        if (first_arg->type == CSS_VALUE_TYPE_CUSTOM && first_arg->data.custom_property.name) {
            var_name = first_arg->data.custom_property.name;
        } else if (first_arg->type == CSS_VALUE_TYPE_STRING && first_arg->data.string) {
            var_name = first_arg->data.string;
        }
    }

    if (!var_name) {
        // No variable name found, try fallback
        if (func->arg_count >= 2 && func->args[1]) {
            return resolve_var_function(lycon, func->args[1]);
        }
        return nullptr;  // No value found
    }

    // Look up the variable
    const CssValue* var_value = lookup_css_variable(lycon, var_name);
    if (var_value) {
        // Recursively resolve in case the variable value is also a var()
        return resolve_var_function(lycon, var_value);
    }

    // Variable not found, try fallback
    if (func->arg_count >= 2 && func->args[1]) {
        return resolve_var_function(lycon, func->args[1]);
    }

    return nullptr;  // No value found
}

// Helper: extract a numeric value from a CssValue (number, percentage, length)
// For colors, percentages are relative to 255 (for RGB) or 1.0 (for alpha)
static double resolve_color_component(const CssValue* v, bool is_alpha = false) {
    if (!v) return 0.0;
    switch (v->type) {
    case CSS_VALUE_TYPE_NUMBER:
        return v->data.number.value;
    case CSS_VALUE_TYPE_PERCENTAGE:
        // For alpha, 100% = 1.0; for RGB, 100% = 255
        return is_alpha ? v->data.percentage.value / 100.0 : (v->data.percentage.value * 255.0 / 100.0);
    case CSS_VALUE_TYPE_LENGTH:
        return v->data.length.value;
    default:
        return 0.0;
    }
}

Color resolve_color_value(LayoutContext* lycon, const CssValue* value) {
    Color result;
    result.r = 0;
    result.g = 0;
    result.b = 0;
    result.a = 255; // default black, opaque

    if (!value) return result;

    // Resolve var() if present
    value = resolve_var_function(lycon, value);
    if (!value) return result;

    switch (value->type) {
    case CSS_VALUE_TYPE_COLOR: {
        // Access color data from CssValue anonymous struct
        switch (value->data.color.type) {
        case CSS_COLOR_HEX:  case CSS_COLOR_RGB:
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
        break;
    }
    case CSS_VALUE_TYPE_FUNCTION: {
        // Handle rgb(), rgba(), hsl(), hsla() color functions
        const CssFunction* func = value->data.function;
        if (!func || !func->name) break;

        log_debug("[CSS] resolve_color_value: function=%s, arg_count=%d", func->name, func->arg_count);

        // rgb() and rgba() functions
        // Modern syntax: rgb(r g b) or rgb(r g b / alpha) - parsed as 1 arg (list)
        // Legacy syntax: rgb(r, g, b) or rgba(r, g, b, a) - parsed as 3-4 args
        if (str_ieq_const(func->name, strlen(func->name), "rgb") || str_ieq_const(func->name, strlen(func->name), "rgba")) {
            // Check for modern syntax: single list argument with space-separated values
            if (func->arg_count == 1 && func->args[0] && func->args[0]->type == CSS_VALUE_TYPE_LIST) {
                const CssValue* list = func->args[0];
                // Modern syntax: list contains R, G, B [, '/', alpha]
                // Find numeric values (skip '/' delimiter if present)
                double r = 0, g = 0, b = 0, a = 255;
                int num_idx = 0;
                bool found_slash = false;

                for (size_t i = 0; i < list->data.list.count && num_idx < 4; i++) {
                    const CssValue* v = list->data.list.values[i];
                    if (!v) continue;

                    // Check for '/' delimiter (CUSTOM type with "/" or DELIM)
                    if (v->type == CSS_VALUE_TYPE_CUSTOM && v->data.custom_property.name &&
                        strcmp(v->data.custom_property.name, "/") == 0) {
                        found_slash = true;
                        continue;
                    }
                    // Skip var() functions (for opacity like var(--tw-text-opacity))
                    if (v->type == CSS_VALUE_TYPE_FUNCTION || v->type == CSS_VALUE_TYPE_VAR) {
                        // If we've seen slash, this is alpha - default to 1 (fully opaque)
                        if (found_slash && num_idx == 3) {
                            a = 255;  // default opaque
                        }
                        continue;
                    }

                    double val = resolve_color_component(v, found_slash);
                    if (num_idx == 0) r = val;
                    else if (num_idx == 1) g = val;
                    else if (num_idx == 2) b = val;
                    else if (num_idx == 3) {
                        // Alpha value
                        if (v->type == CSS_VALUE_TYPE_NUMBER) {
                            a = val * 255.0;  // 0-1 scale
                        } else {
                            a = val;  // percentage already converted
                        }
                    }
                    num_idx++;
                }

                // Clamp to valid range
                result.r = (uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r));
                result.g = (uint8_t)(g < 0 ? 0 : (g > 255 ? 255 : g));
                result.b = (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b));
                result.a = (uint8_t)(a < 0 ? 0 : (a > 255 ? 255 : a));

                log_debug("[CSS] resolve_color_value: rgb modern syntax -> (%d, %d, %d, %d)", result.r, result.g, result.b, result.a);
            }
            else if (func->arg_count >= 3) {
                // Legacy syntax: separate arguments
                double r = resolve_color_component(func->args[0]);
                double g = resolve_color_component(func->args[1]);
                double b = resolve_color_component(func->args[2]);

                // Clamp to valid range
                result.r = (uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r));
                result.g = (uint8_t)(g < 0 ? 0 : (g > 255 ? 255 : g));
                result.b = (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b));

                // Check for alpha value
                if (func->arg_count >= 4) {
                    double a = resolve_color_component(func->args[3], true);
                    // Alpha can be a number (0-1) or percentage
                    if (func->args[3] && func->args[3]->type == CSS_VALUE_TYPE_NUMBER) {
                        // Number format: 0 to 1
                        a = a * 255.0;
                    }
                    result.a = (uint8_t)(a < 0 ? 0 : (a > 255 ? 255 : a));
                }

                log_debug("[CSS] resolve_color_value: rgb legacy syntax -> (%d, %d, %d, %d)", result.r, result.g, result.b, result.a);
            }
        }
        // hsl() and hsla() functions - TODO: implement HSL to RGB conversion
        else if (str_ieq_const(func->name, strlen(func->name), "hsl") || str_ieq_const(func->name, strlen(func->name), "hsla")) {
            // TODO: implement HSL to RGB conversion
            log_debug("[CSS] resolve_color_value: hsl() not yet implemented");
        }
        break;
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
    // Handle transparent specially - it has alpha = 0
    if (color_name == CSS_VALUE_TRANSPARENT) {
        return (Color){ .r = 0, .g = 0, .b = 0, .a = 0 };
    }

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

// map CSS font-weight keywords/numbers to PropValue enum
CssEnum map_font_weight(const CssValue* value) {
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
    else if (value->type == CSS_VALUE_TYPE_NUMBER) {
        // numeric weights: map to closest keyword or return as-is
        int weight = (int)value->data.number.value;
        // uses enum values for numeric weights, but for simplicity
        // we'll map common numeric values to their keyword equivalents
        if (weight <= 350) return CSS_VALUE_LIGHTER;
        if (weight <= 550) return CSS_VALUE_NORMAL;  // 400
        if (weight <= 750) return CSS_VALUE_BOLD;    // 700
        return CSS_VALUE_BOLDER;  // 900
    }
    return CSS_VALUE_NORMAL; // default
}

// Specificity Calculation
int32_t get_lambda_specificity(const CssDeclaration* decl) {
    if (!decl) {
        log_debug("[CSS] get_lambda_specificity: decl is NULL");
        return 0;
    }
    // Lambda CssSpecificity is a struct with (inline_style, ids, classes, elements) components
    // Convert to int32_t by packing:
    // inline_style is highest priority (bit 24), then ids (bits 16-23), classes (8-15), elements (0-7)
    int32_t specificity = (decl->specificity.inline_style << 24) |
                          (decl->specificity.ids << 16) |
                          (decl->specificity.classes << 8) |
                          decl->specificity.elements;
    log_debug("[CSS] decl specificity: inline=%d, ids=%d, classes=%d, elmts=%d => %d",
        decl->specificity.inline_style, decl->specificity.ids, decl->specificity.classes, decl->specificity.elements, specificity);
    return specificity;
}

// Helper: Check if element has float:left or float:right
// Returns CSS_VALUE_LEFT, CSS_VALUE_RIGHT, or CSS_VALUE_NONE
static CssEnum get_float_value_from_style(DomElement* elem) {
    if (!elem || !elem->specified_style || !elem->specified_style->tree) {
        return CSS_VALUE_NONE;
    }
    AvlNode* float_node = avl_tree_search(elem->specified_style->tree, CSS_PROPERTY_FLOAT);
    if (float_node) {
        StyleNode* style_node = (StyleNode*)float_node->declaration;
        if (style_node && style_node->winning_decl && style_node->winning_decl->value) {
            CssValue* val = style_node->winning_decl->value;
            if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                return val->data.keyword;
            }
        }
    }
    return CSS_VALUE_NONE;
}

// CSS 2.1 §9.7: Apply blockification for floated or absolutely positioned elements
// Converts internal table display values to 'block'
static DisplayValue blockify_display(DisplayValue display) {
    // Table internal display values that get blockified
    if (display.inner == CSS_VALUE_TABLE_ROW ||
        display.inner == CSS_VALUE_TABLE_ROW_GROUP ||
        display.inner == CSS_VALUE_TABLE_HEADER_GROUP ||
        display.inner == CSS_VALUE_TABLE_FOOTER_GROUP ||
        display.inner == CSS_VALUE_TABLE_COLUMN ||
        display.inner == CSS_VALUE_TABLE_COLUMN_GROUP ||
        display.inner == CSS_VALUE_TABLE_CELL ||
        display.inner == CSS_VALUE_TABLE_CAPTION) {
        log_debug("[CSS] §9.7 blockification: converting table-internal display to block");
        return DisplayValue{CSS_VALUE_BLOCK, CSS_VALUE_FLOW};
    }
    // inline becomes block
    if (display.outer == CSS_VALUE_INLINE && display.inner == CSS_VALUE_FLOW) {
        log_debug("[CSS] §9.7 blockification: inline -> block");
        return DisplayValue{CSS_VALUE_BLOCK, CSS_VALUE_FLOW};
    }
    // inline-block, inline-table, inline-flex, inline-grid stay as block-level equivalents
    if (display.outer == CSS_VALUE_INLINE_BLOCK) {
        display.outer = CSS_VALUE_BLOCK;
    }
    if (display.outer == CSS_VALUE_INLINE && display.inner == CSS_VALUE_TABLE) {
        display.outer = CSS_VALUE_BLOCK;  // inline-table -> table
    }
    return display;
}

DisplayValue resolve_display_value(void* child) {
    // Resolve display value for a DOM node
    DisplayValue display = {CSS_VALUE_BLOCK, CSS_VALUE_FLOW};

    DomNode* node = (DomNode*)child;
    if (node && node->is_element()) {
        // resolve display from CSS if available
        DomElement* dom_elem = node->as_element();
        uintptr_t tag_id = dom_elem ? dom_elem->tag_id : HTM_TAG__UNDEF;

        log_debug("[CSS] resolve_display_value for node=%p, tag_name=%s", node, node->node_name());

        // CSS 2.1 §9.7: Check for float - floated elements get blockified
        CssEnum float_value = get_float_value_from_style(dom_elem);
        bool is_floated = (float_value == CSS_VALUE_LEFT || float_value == CSS_VALUE_RIGHT);

        // Check if element already has display set directly (anonymous elements, pre-resolved)
        // This handles CSS 2.1 anonymous table objects created by layout
        if (dom_elem && dom_elem->display.inner != CSS_VALUE_NONE &&
            dom_elem->display.inner != 0 && dom_elem->styles_resolved) {
            log_debug("[CSS] Using pre-set display from element: outer=%d, inner=%d",
                dom_elem->display.outer, dom_elem->display.inner);
            return dom_elem->display;
        }

        // Determine if this is a replaced element (img, video, iframe, svg, etc.)
        // Replaced elements always have inner display of RDT_DISPLAY_REPLACED
        bool is_replaced = (tag_id == HTM_TAG_IMG || tag_id == HTM_TAG_VIDEO ||
                            tag_id == HTM_TAG_INPUT || tag_id == HTM_TAG_SELECT ||
                            tag_id == HTM_TAG_TEXTAREA || tag_id == HTM_TAG_BUTTON ||
                            tag_id == HTM_TAG_IFRAME || tag_id == HTM_TAG_HR ||
                            tag_id == HTM_TAG_SVG);

        // first, try to get display from CSS
        if (dom_elem && dom_elem->specified_style) {
            StyleTree* style_tree = dom_elem->specified_style;
            log_debug("[CSS]   has specified_style, tree=%p", style_tree->tree);
            if (style_tree->tree) {
                // look for display property in the AVL tree
                AvlNode* node = avl_tree_search(style_tree->tree, CSS_PROPERTY_DISPLAY);
                log_debug("[CSS]   AVL search result: node=%p", node);
                if (node) {
                    log_debug("[CSS] found display property for tag_id=%lu", tag_id);
                    StyleNode* style_node = (StyleNode*)node->declaration;
                    if (style_node && style_node->winning_decl) {
                        CssDeclaration* decl = style_node->winning_decl;
                        if (decl->value && decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                            CssEnum keyword = decl->value->data.keyword;
                            log_debug("[CSS] display keyword value = %d (FLEX=%d, BLOCK=%d, GRID=%d)", keyword, CSS_VALUE_FLEX, CSS_VALUE_BLOCK, CSS_VALUE_GRID);
                            // Map keyword to display values
                            if (keyword == CSS_VALUE_FLEX) {
                                log_debug("[CSS] ✅ MATCHED FLEX! Setting display to BLOCK+FLEX");
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_FLEX;
                                log_debug("[CSS] ✅ Returning outer=%d, inner=%d", display.outer, display.inner);
                                return display;
                            } else if (keyword == CSS_VALUE_INLINE_FLEX) {
                                display.outer = CSS_VALUE_INLINE_BLOCK;
                                display.inner = CSS_VALUE_FLEX;
                                return display;
                            } else if (keyword == CSS_VALUE_GRID) {
                                log_debug("[CSS] ✅ MATCHED GRID! Setting display to BLOCK+GRID");
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_GRID;
                                log_debug("[CSS] ✅ Returning outer=%d, inner=%d for GRID", display.outer, display.inner);
                                return display;
                            } else if (keyword == CSS_VALUE_INLINE_GRID) {
                                display.outer = CSS_VALUE_INLINE;
                                display.inner = CSS_VALUE_GRID;
                                return display;
                            } else if (keyword == CSS_VALUE_BLOCK) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = is_replaced ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW;
                                return display;
                            } else if (keyword == CSS_VALUE_INLINE) {
                                display.outer = CSS_VALUE_INLINE;
                                display.inner = is_replaced ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW;
                                // CSS 2.1 §9.7: Floated/absolutely positioned elements become block
                                return is_floated ? blockify_display(display) : display;
                            } else if (keyword == CSS_VALUE_INLINE_BLOCK) {
                                display.outer = CSS_VALUE_INLINE_BLOCK;
                                display.inner = is_replaced ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW;
                                // CSS 2.1 §9.7: Floated elements become block
                                return is_floated ? blockify_display(display) : display;
                            } else if (keyword == CSS_VALUE_LIST_ITEM) {
                                display.outer = CSS_VALUE_LIST_ITEM;
                                display.inner = CSS_VALUE_FLOW;
                                log_debug("[CSS] ✅ MATCHED LIST_ITEM! Setting display to LIST_ITEM+FLOW");
                                return display;
                            } else if (keyword == CSS_VALUE_NONE) {
                                display.outer = CSS_VALUE_NONE;
                                display.inner = CSS_VALUE_NONE;
                                return display;
                            } else if (keyword == CSS_VALUE_RUN_IN) {
                                // run-in is unsupported (matches Chrome, which dropped it in v32).
                                // Don't return — fall through to tag-based default display below.
                                log_debug("[CSS] run-in unsupported, using tag default display");
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
                                return is_floated ? blockify_display(display) : display;
                            } else if (keyword == CSS_VALUE_TABLE_CELL) {
                                display.outer = CSS_VALUE_TABLE_CELL;
                                display.inner = CSS_VALUE_TABLE_CELL;
                                return is_floated ? blockify_display(display) : display;
                            } else if (keyword == CSS_VALUE_TABLE_ROW_GROUP) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_ROW_GROUP;
                                return is_floated ? blockify_display(display) : display;
                            } else if (keyword == CSS_VALUE_TABLE_HEADER_GROUP) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_HEADER_GROUP;
                                return is_floated ? blockify_display(display) : display;
                            } else if (keyword == CSS_VALUE_TABLE_FOOTER_GROUP) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_FOOTER_GROUP;
                                return is_floated ? blockify_display(display) : display;
                            } else if (keyword == CSS_VALUE_TABLE_COLUMN) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_COLUMN;
                                return is_floated ? blockify_display(display) : display;
                            } else if (keyword == CSS_VALUE_TABLE_COLUMN_GROUP) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_COLUMN_GROUP;
                                return is_floated ? blockify_display(display) : display;
                            } else if (keyword == CSS_VALUE_TABLE_CAPTION) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_CAPTION;
                                return is_floated ? blockify_display(display) : display;
                            }
                        } else if (decl->value->type == CSS_VALUE_TYPE_LIST) {
                            // Handle CSS Display Level 3 two-value syntax: "display: <outer> <inner>"
                            // e.g., "display: block flow", "display: inline flow", etc.
                            CssValue** values = decl->value->data.list.values;
                            int count = decl->value->data.list.count;
                            log_debug("[CSS] display LIST value with %d items", count);

                            if (count >= 2 && values[0] && values[1] &&
                                values[0]->type == CSS_VALUE_TYPE_KEYWORD &&
                                values[1]->type == CSS_VALUE_TYPE_KEYWORD) {
                                CssEnum outer_kw = values[0]->data.keyword;
                                CssEnum inner_kw = values[1]->data.keyword;
                                log_debug("[CSS] two-value display: outer=%d, inner=%d", outer_kw, inner_kw);

                                // Map outer display keyword
                                if (outer_kw == CSS_VALUE_BLOCK) {
                                    display.outer = CSS_VALUE_BLOCK;
                                } else if (outer_kw == CSS_VALUE_INLINE) {
                                    display.outer = CSS_VALUE_INLINE;
                                } else if (outer_kw == CSS_VALUE_RUN_IN) {
                                    // run-in unsupported — don't set, will fall through to tag default
                                } else {
                                    display.outer = CSS_VALUE_BLOCK; // default to block
                                }

                                // Map inner display keyword
                                if (inner_kw == CSS_VALUE_FLOW) {
                                    display.inner = is_replaced ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW;
                                } else if (inner_kw == CSS_VALUE_FLOW_ROOT) {
                                    display.inner = CSS_VALUE_FLOW_ROOT;
                                } else if (inner_kw == CSS_VALUE_FLEX) {
                                    display.inner = CSS_VALUE_FLEX;
                                } else if (inner_kw == CSS_VALUE_GRID) {
                                    display.inner = CSS_VALUE_GRID;
                                } else if (inner_kw == CSS_VALUE_TABLE) {
                                    display.inner = CSS_VALUE_TABLE;
                                } else if (inner_kw == CSS_VALUE_RUBY) {
                                    display.inner = CSS_VALUE_RUBY;
                                } else {
                                    display.inner = CSS_VALUE_FLOW; // default to flow
                                }

                                log_debug("[CSS] ✅ Resolved two-value display: outer=%d, inner=%d",
                                    display.outer, display.inner);
                                return display;
                            } else if (count == 1 && values[0] &&
                                       values[0]->type == CSS_VALUE_TYPE_KEYWORD) {
                                // Single keyword in list (edge case)
                                CssEnum keyword = values[0]->data.keyword;
                                log_debug("[CSS] single keyword in list: %d", keyword);
                                // Handle same as single keyword (fall through to regular logic won't work here)
                                // Re-use the single keyword logic
                                if (keyword == CSS_VALUE_BLOCK) {
                                    display.outer = CSS_VALUE_BLOCK;
                                    display.inner = is_replaced ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW;
                                    return display;
                                } else if (keyword == CSS_VALUE_INLINE) {
                                    display.outer = CSS_VALUE_INLINE;
                                    display.inner = is_replaced ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW;
                                    return display;
                                } else if (keyword == CSS_VALUE_FLEX) {
                                    display.outer = CSS_VALUE_BLOCK;
                                    display.inner = CSS_VALUE_FLEX;
                                    return display;
                                } else if (keyword == CSS_VALUE_GRID) {
                                    display.outer = CSS_VALUE_BLOCK;
                                    display.inner = CSS_VALUE_GRID;
                                    return display;
                                } else if (keyword == CSS_VALUE_NONE) {
                                    display.outer = CSS_VALUE_NONE;
                                    display.inner = CSS_VALUE_NONE;
                                    return display;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Fall back to default display values based on tag ID
        if (tag_id == HTM_TAG_BODY || tag_id == HTM_TAG_H1 ||
            tag_id == HTM_TAG_H2 || tag_id == HTM_TAG_H3 ||
            tag_id == HTM_TAG_H4 || tag_id == HTM_TAG_H5 ||
            tag_id == HTM_TAG_H6 || tag_id == HTM_TAG_P ||
            tag_id == HTM_TAG_DIV || tag_id == HTM_TAG_CENTER ||
            tag_id == HTM_TAG_UL || tag_id == HTM_TAG_OL ||
            tag_id == HTM_TAG_DL || tag_id == HTM_TAG_DT || tag_id == HTM_TAG_DD ||
            tag_id == HTM_TAG_HEADER || tag_id == HTM_TAG_MAIN ||
            tag_id == HTM_TAG_SECTION || tag_id == HTM_TAG_FOOTER ||
            tag_id == HTM_TAG_ARTICLE || tag_id == HTM_TAG_ASIDE ||
            tag_id == HTM_TAG_NAV || tag_id == HTM_TAG_ADDRESS ||
            tag_id == HTM_TAG_BLOCKQUOTE || tag_id == HTM_TAG_DETAILS ||
            tag_id == HTM_TAG_DIALOG || tag_id == HTM_TAG_FIGURE ||
            tag_id == HTM_TAG_FIGCAPTION || tag_id == HTM_TAG_HGROUP ||
            tag_id == HTM_TAG_PRE || tag_id == HTM_TAG_FIELDSET ||
            tag_id == HTM_TAG_LEGEND || tag_id == HTM_TAG_FORM ||
            tag_id == HTM_TAG_MENU) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_FLOW;
        } else if (tag_id == HTM_TAG_LI || tag_id == HTM_TAG_SUMMARY) {
            display.outer = CSS_VALUE_LIST_ITEM;
            display.inner = CSS_VALUE_FLOW;
        } else if (tag_id == HTM_TAG_IMG || tag_id == HTM_TAG_VIDEO ||
            tag_id == HTM_TAG_INPUT || tag_id == HTM_TAG_SELECT ||
            tag_id == HTM_TAG_TEXTAREA || tag_id == HTM_TAG_BUTTON ||
            tag_id == HTM_TAG_IFRAME) {
            display.outer = CSS_VALUE_INLINE_BLOCK;
            display.inner = RDT_DISPLAY_REPLACED;
        } else if (tag_id == HTM_TAG_HR) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = RDT_DISPLAY_REPLACED;
        } else if (tag_id == HTM_TAG_SVG) {
            // SVG elements are inline replaced elements by default
            display.outer = CSS_VALUE_INLINE;
            display.inner = RDT_DISPLAY_REPLACED;
        } else if (tag_id == HTM_TAG_SCRIPT || tag_id == HTM_TAG_STYLE ||
            tag_id == HTM_TAG_HEAD || tag_id == HTM_TAG_TITLE || tag_id == HTM_TAG_META ||
            tag_id == HTM_TAG_LINK || tag_id == HTM_TAG_BASE || tag_id == HTM_TAG_NOSCRIPT ||
            tag_id == HTM_TAG_TEMPLATE || tag_id == HTM_TAG_MAP || tag_id == HTM_TAG_AREA ||
            tag_id == HTM_TAG_OPTION || tag_id == HTM_TAG_OPTGROUP) {
            // Option/optgroup elements inside <select> are rendered by the select control itself,
            // not as normal DOM layout elements. Browsers report 0x0 dimensions for them.
            display.outer = CSS_VALUE_NONE;
            display.inner = CSS_VALUE_NONE;
        } else if (tag_id == HTM_TAG_TABLE) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_TABLE;
        } else if (tag_id == HTM_TAG_CAPTION) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_FLOW;
        } else if (tag_id == HTM_TAG_THEAD || tag_id == HTM_TAG_TBODY || tag_id == HTM_TAG_TFOOT) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_TABLE_ROW_GROUP;
        } else if (tag_id == HTM_TAG_TR) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_TABLE_ROW;
        } else if (tag_id == HTM_TAG_TH || tag_id == HTM_TAG_TD) {
            display.outer = CSS_VALUE_TABLE_CELL;
            display.inner = CSS_VALUE_TABLE_CELL;
        } else if (tag_id == HTM_TAG_COLGROUP) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_TABLE_COLUMN_GROUP;
        } else if (tag_id == HTM_TAG_COL) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_TABLE_COLUMN;
        } else {
            // Fall back to tag name string comparison for elements without tag_id
            // This handles markdown/Lambda-generated HTML that doesn't go through HTML5 parser
            const char* tag_name = node->node_name();
            if (tag_name) {
                if (strcmp(tag_name, "table") == 0) {
                    display.outer = CSS_VALUE_BLOCK;
                    display.inner = CSS_VALUE_TABLE;
                } else if (strcmp(tag_name, "thead") == 0 || strcmp(tag_name, "tbody") == 0 || strcmp(tag_name, "tfoot") == 0) {
                    display.outer = CSS_VALUE_BLOCK;
                    display.inner = CSS_VALUE_TABLE_ROW_GROUP;
                } else if (strcmp(tag_name, "tr") == 0) {
                    display.outer = CSS_VALUE_BLOCK;
                    display.inner = CSS_VALUE_TABLE_ROW;
                } else if (strcmp(tag_name, "th") == 0 || strcmp(tag_name, "td") == 0) {
                    display.outer = CSS_VALUE_TABLE_CELL;
                    display.inner = CSS_VALUE_TABLE_CELL;
                } else if (strcmp(tag_name, "caption") == 0) {
                    display.outer = CSS_VALUE_BLOCK;
                    display.inner = CSS_VALUE_FLOW;
                } else if (strcmp(tag_name, "colgroup") == 0) {
                    display.outer = CSS_VALUE_BLOCK;
                    display.inner = CSS_VALUE_TABLE_COLUMN_GROUP;
                } else if (strcmp(tag_name, "col") == 0) {
                    display.outer = CSS_VALUE_BLOCK;
                    display.inner = CSS_VALUE_TABLE_COLUMN;
                } else {
                    // Default for truly unknown elements (inline)
                    display.outer = CSS_VALUE_INLINE;
                    display.inner = CSS_VALUE_FLOW;
                }
            } else {
                // No tag name available, default to inline
                display.outer = CSS_VALUE_INLINE;
                display.inner = CSS_VALUE_FLOW;
            }
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

        // Resolve var() if present
        value = resolve_var_function(lycon, value);
        if (!value) {
            // var() couldn't be resolved, use fallback
            if (lycon->font.style && lycon->font.style->font_size > 0) {
                lycon->font.current_font_size = lycon->font.style->font_size;
            } else {
                lycon->font.current_font_size = 16.0f;
            }
            return;
        }

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
 * @param property CSS property ID for context-specific resolution.
 *                 Use negative value to suppress line-height NUMBER multiplication (for calc() operands)
 *                 while still using absolute value for percentage base selection.
 * @param value Lambda CssValue pointer (CSS_VALUE_LENGTH, CSS_VALUE_PERCENTAGE, or CSS_VALUE_NUMBER)
 * @return Resolved value in pixels
 */
float resolve_length_value(LayoutContext* lycon, uintptr_t property, const CssValue* value) {
    if (!value) { log_debug("resolve_length_value: null value");  return 0.0f; }

    // Check if we're in "raw mode" (negative property) - used for calc() operands
    // In raw mode, NUMBER values are not multiplied by font-size for line-height
    bool raw_number_mode = (intptr_t)property < 0;
    uintptr_t effective_property = raw_number_mode ? (uintptr_t)(-(intptr_t)property) : property;

    float result = 0.0f;
    switch (value->type) {
    case CSS_VALUE_TYPE_NUMBER:
        // unitless number
        log_debug("number value: %.2f", value->data.number.value);
        if (!raw_number_mode && effective_property == CSS_PROPERTY_LINE_HEIGHT) {
            if (lycon->font.current_font_size < 0) {
                log_debug("resolving font size for em value");
                resolve_font_size(lycon, NULL);
            }
            result = value->data.number.value * lycon->font.current_font_size;
        } else {
            // treat as pixels for most properties (or in raw mode for calc operands)
            result = (float)value->data.number.value;
        }
        break;

    case CSS_VALUE_TYPE_LENGTH: {
        double num = value->data.length.value;
        CssUnit unit = value->data.length.unit;
        log_debug("length value: %.2f, unit: %d", num, unit);
        switch (unit) {
        // absolute units (all in CSS logical pixels, 96 dpi reference)
        case CSS_UNIT_Q:  // 1Q = 1cm / 40
            result = num * (96 / 2.54 / 40);
            break;
        case CSS_UNIT_CM:  // 96px / 2.54
            result = num * (96 / 2.54);
            break;
        case CSS_UNIT_IN:  // 96px
            result = num * 96;
            break;
        case CSS_UNIT_MM:  // 1mm = 1cm / 10
            result = num * (96 / 25.4);
            break;
        case CSS_UNIT_PC:  // 1pc = 12pt = 1in / 6
            result = num * 16;
            break;
        case CSS_UNIT_PT:  // 1pt = 1in / 72
            result = num * 4 / 3;
            break;
        case CSS_UNIT_PX:
            result = num;  // CSS logical pixels
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
            if (effective_property == CSS_PROPERTY_FONT_SIZE) {
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
            // viewport width percentage (result in CSS logical pixels)
            if (lycon && lycon->width > 0) {
                result = (num / 100.0) * lycon->width;
            }
            break;
        case CSS_UNIT_VH:
            // viewport height percentage (result in CSS logical pixels)
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
        case CSS_UNIT_EX: {
            // relative to x-height of the font
            if (lycon->font.current_font_size < 0) {
                resolve_font_size(lycon, NULL);
            }
            float x_height_ratio = font_get_x_height_ratio(lycon->font.font_handle);
            result = num * lycon->font.current_font_size * x_height_ratio;
            break;
        }
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
        if (effective_property == CSS_PROPERTY_FONT_SIZE || effective_property == CSS_PROPERTY_LINE_HEIGHT || effective_property == CSS_PROPERTY_VERTICAL_ALIGN) {
            // font-size percentage is relative to parent font size
            result = percentage * lycon->font.style->font_size / 100.0;
        } else if (effective_property == CSS_PROPERTY_HEIGHT || effective_property == CSS_PROPERTY_MIN_HEIGHT ||
                   effective_property == CSS_PROPERTY_MAX_HEIGHT || effective_property == CSS_PROPERTY_TOP ||
                   effective_property == CSS_PROPERTY_BOTTOM) {
            // height-related properties: percentage relative to parent HEIGHT
            if (lycon->block.parent && lycon->block.parent->content_height > 0) {
                log_debug("percentage height calculation: %.2f%% of parent height %.1f = %.2f",
                       percentage, lycon->block.parent->content_height,
                       percentage * lycon->block.parent->content_height / 100.0);
                result = percentage * lycon->block.parent->content_height / 100.0;
            } else if (lycon->block.parent && lycon->block.parent->given_height > 0) {
                // Parent has given height but content_height not yet calculated
                // This handles flex items with percentage heights where parent has definite height
                log_debug("percentage height calculation: %.2f%% of parent given_height %.1f = %.2f",
                       percentage, lycon->block.parent->given_height,
                       percentage * lycon->block.parent->given_height / 100.0);
                result = percentage * lycon->block.parent->given_height / 100.0;
            } else if (!lycon->block.parent && lycon && lycon->height > 0) {
                // No parent context (root html element) - use viewport height
                // This handles html element with height: 100%
                // Layout now uses CSS pixels, so use lycon->height directly (no pixel_ratio scaling)
                log_debug("percentage height value %.2f%% of viewport height %.1f = %.2f (no parent)",
                       percentage, lycon->height, percentage * lycon->height / 100.0);
                result = percentage * lycon->height / 100.0;
            } else {
                // Parent exists but has no definite height - percentage resolves to auto (0)
                // Per CSS spec, percentage heights compute to auto when parent height is not definite
                log_debug("percentage height value %.2f%% resolves to 0 (parent has no definite height)", percentage);
                result = 0.0f;
            }
        } else {
            // width-related and other properties: percentage relative to parent width
            if (lycon->block.parent) {
                log_debug("percentage calculation: %.2f%% of parent width %.1f = %.2f",
                       percentage, lycon->block.parent->content_width,
                       percentage * lycon->block.parent->content_width / 100.0);
                result = percentage * lycon->block.parent->content_width / 100.0;
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
    case CSS_VALUE_TYPE_FUNCTION: {
        // handle calc() and other CSS functions that return length values
        CssFunction* func = value->data.function;
        if (!func || !func->name) {
            log_warn("function value with no name");
            result = NAN;  // Use NAN to indicate unresolvable value
            break;
        }
        log_debug("resolving function: %s() with %d args", func->name, func->arg_count);

        if (strcmp(func->name, "calc") == 0) {
            // calc() expression - evaluate the expression
            // For now, handle simple cases like "calc(100% - 2rem)"

            // Use negative property to enable raw number mode (no line-height multiplication)
            // while preserving the property ID for correct percentage base selection
            uintptr_t raw_prop = (uintptr_t)(-(intptr_t)property);

            if (func->arg_count >= 1 && func->args && func->args[0]) {
                // Check for simple binary operations in a list value
                CssValue* arg = func->args[0];
                if (arg->type == CSS_VALUE_TYPE_LIST && arg->data.list.count == 3) {
                    // Expect: <value1> <operator> <value2>
                    CssValue* val1 = arg->data.list.values[0];
                    CssValue* op = arg->data.list.values[1];
                    CssValue* val2 = arg->data.list.values[2];

                    if (op && op->type == CSS_VALUE_TYPE_KEYWORD) {
                        // inside calc(), resolve operands without line-height special behavior
                        // unitless numbers inside calc() stay raw, not multiplied by font-size
                        float left = resolve_length_value(lycon, raw_prop, val1);
                        float right = resolve_length_value(lycon, raw_prop, val2);
                        const CssEnumInfo* op_info = css_enum_info(op->data.keyword);
                        const char* op_name = op_info ? op_info->name : "";

                        log_debug("calc: %.2f %s %.2f", left, op_name, right);

                        if (strcmp(op_name, "+") == 0) {
                            result = left + right;
                        } else if (strcmp(op_name, "-") == 0) {
                            result = left - right;
                        } else if (strcmp(op_name, "*") == 0) {
                            result = left * right;
                        } else if (strcmp(op_name, "/") == 0) {
                            result = right != 0 ? left / right : 0;
                        } else {
                            log_warn("calc: unknown operator '%s'", op_name);
                            result = NAN;
                        }
                    } else if (op && op->type == CSS_VALUE_TYPE_CUSTOM && op->data.custom_property.name) {
                        // Operator stored as custom property (e.g. "-" parsed as CSS_TOKEN_DELIM)
                        // inside calc(), resolve operands without line-height special behavior
                        float left = resolve_length_value(lycon, raw_prop, val1);
                        float right = resolve_length_value(lycon, raw_prop, val2);
                        const char* op_name = op->data.custom_property.name;

                        log_debug("calc (custom op): %.2f %s %.2f", left, op_name, right);

                        if (strcmp(op_name, "+") == 0) {
                            result = left + right;
                        } else if (strcmp(op_name, "-") == 0) {
                            result = left - right;
                        } else if (strcmp(op_name, "*") == 0) {
                            result = left * right;
                        } else if (strcmp(op_name, "/") == 0) {
                            result = right != 0 ? left / right : 0;
                        } else {
                            log_warn("calc: unknown operator '%s'", op_name);
                            result = NAN;
                        }
                    } else {
                        log_warn("calc: operator is not a keyword or custom (type=%d)", op ? op->type : -1);
                        result = NAN;
                    }
                } else if (arg->type == CSS_VALUE_TYPE_LIST && arg->data.list.count >= 1) {
                    // Try to evaluate as a simple expression with alternating values and operators
                    // Parse through the list: value op value op value ...
                    result = 0;
                    char pending_op = '+';  // Start with implicit + 0

                    for (int i = 0; i < arg->data.list.count; i++) {
                        CssValue* item = arg->data.list.values[i];
                        if (!item) continue;

                        bool is_operator = false;
                        const char* op_name = NULL;

                        if (item->type == CSS_VALUE_TYPE_KEYWORD) {
                            const CssEnumInfo* op_info = css_enum_info(item->data.keyword);
                            op_name = op_info ? op_info->name : "";
                            is_operator = true;
                        } else if (item->type == CSS_VALUE_TYPE_CUSTOM && item->data.custom_property.name) {
                            op_name = item->data.custom_property.name;
                            // Check if this looks like an operator
                            if (strlen(op_name) == 1 && (op_name[0] == '+' || op_name[0] == '-' ||
                                                         op_name[0] == '*' || op_name[0] == '/')) {
                                is_operator = true;
                            }
                        }

                        if (is_operator && op_name) {
                            if (strcmp(op_name, "+") == 0) pending_op = '+';
                            else if (strcmp(op_name, "-") == 0) pending_op = '-';
                            else if (strcmp(op_name, "*") == 0) pending_op = '*';
                            else if (strcmp(op_name, "/") == 0) pending_op = '/';
                        } else {
                            // This is a value - resolve with raw_prop for correct percentage base
                            float val = resolve_length_value(lycon, raw_prop, item);
                            if (!std::isnan(val)) {
                                switch (pending_op) {
                                    case '+': result += val; break;
                                    case '-': result -= val; break;
                                    case '*': result *= val; break;
                                    case '/': result = val != 0 ? result / val : result; break;
                                }
                            }
                            pending_op = '+';  // Reset to + for next value
                        }
                    }
                    log_debug("calc list expression result: %.2f", result);
                } else {
                    // Single value in calc - resolve with raw_prop for correct percentage base
                    result = resolve_length_value(lycon, raw_prop, arg);
                }
            } else {
                log_warn("calc() with no arguments");
                result = NAN;
            }

            // Note: We do NOT apply line-height unitless multiplier here because:
            // 1. calc() results lose type information - we can't distinguish calc(1.2) from calc(10px + 8px)
            // 2. The heuristic (< 10 means unitless) is too fragile for complex CSS with variables
            // 3. If the result is truly unitless for line-height, the caller should handle it
            // This matches browser behavior where calc(1.5) returns a dimensionless value,
            // and calc(10px + 8px) returns a length value.
        } else if (strcmp(func->name, "min") == 0 || strcmp(func->name, "max") == 0 ||
                   strcmp(func->name, "clamp") == 0) {
            // min(), max(), clamp() functions
            log_debug("CSS function %s() not yet implemented, treating as unset", func->name);
            result = NAN;
        } else if (strcmp(func->name, "var") == 0) {
            // var(--custom-property-name) or var(--custom-property-name, fallback)
            const char* var_name = nullptr;

            // Safety checks for arguments
            if (func->args && func->arg_count >= 1 && func->args[0]) {
                CssValue* first_arg = func->args[0];

                // Extract variable name based on argument type
                if (first_arg->type == CSS_VALUE_TYPE_CUSTOM && first_arg->data.custom_property.name) {
                    var_name = first_arg->data.custom_property.name;
                } else if (first_arg->type == CSS_VALUE_TYPE_STRING && first_arg->data.string) {
                    var_name = first_arg->data.string;
                }
            }

            if (var_name) {
                // Look up the variable value
                const CssValue* var_value = lookup_css_variable(lycon, var_name);
                if (var_value) {
                    // Recursively resolve the variable value
                    result = resolve_length_value(lycon, property, var_value);
                } else {
                    // Variable not found, use fallback if available
                    if (func->arg_count >= 2 && func->args[1]) {
                        result = resolve_length_value(lycon, property, func->args[1]);
                    } else {
                        result = 0.0f;
                    }
                }
            } else {
                result = 0.0f;
            }
        } else {
            log_warn("unknown CSS function: %s(), using 0 instead of NaN", func->name);
            result = 0.0f;  // Use 0 instead of NAN to prevent crash
        }
        break;
    }
    case CSS_VALUE_TYPE_LIST:
        // List of values - typically used in shorthand properties or multi-value contexts
        // For length values, just take the first value if available
        if (value->data.list.count > 0 && value->data.list.values[0]) {
            result = resolve_length_value(lycon, property, value->data.list.values[0]);
        } else {
            log_debug("empty list for length value, returning 0");
            result = 0.0f;
        }
        break;
    case CSS_VALUE_TYPE_CUSTOM:
        // Custom property value (e.g., --main-color: red;)
        // This should not be resolved directly - it should be stored and retrieved via var()
        log_debug("custom property value type encountered, returning 0");
        result = 0.0f;
        break;
    case CSS_VALUE_TYPE_VAR:
        // var() reference that wasn't handled in function case
        // This might be a standalone var reference without being wrapped in a function
        log_debug("var reference encountered outside function context, returning 0");
        result = 0.0f;
        break;
    default:
        log_warn("unknown length value type: %d", value->type);
        result = NAN;  // Use NAN instead of 0 to indicate unresolvable value
        break;
    }
    log_debug("resolved length value: type %d -> %.2f px", value->type, result);
    return result;
}

// Helper function to resolve margin value with inherit support
// Returns the resolved margin value in pixels
// If value is 'inherit', looks up parent element's computed margin value
static float resolve_margin_with_inherit(LayoutContext* lycon, CssPropertyId prop_id, const CssValue* value) {
    // Check for inherit keyword
    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_INHERIT) {
        // Look up parent element's computed margin value
        DomElement* current = (DomElement*)lycon->view;
        if (current && current->parent && current->parent->is_element()) {
            DomElement* parent = (DomElement*)current->parent;
            // Check if parent has bound property
            if (parent->bound) {
                switch (prop_id) {
                    case CSS_PROPERTY_MARGIN_TOP:
                        log_debug("[CSS] margin-top: inheriting %.2f from parent", parent->bound->margin.top);
                        return parent->bound->margin.top;
                    case CSS_PROPERTY_MARGIN_RIGHT:
                        log_debug("[CSS] margin-right: inheriting %.2f from parent", parent->bound->margin.right);
                        return parent->bound->margin.right;
                    case CSS_PROPERTY_MARGIN_BOTTOM:
                        log_debug("[CSS] margin-bottom: inheriting %.2f from parent", parent->bound->margin.bottom);
                        return parent->bound->margin.bottom;
                    case CSS_PROPERTY_MARGIN_LEFT:
                        log_debug("[CSS] margin-left: inheriting %.2f from parent", parent->bound->margin.left);
                        return parent->bound->margin.left;
                    default:
                        break;
                }
            }
        }
        // No parent or parent has no margin, use 0
        log_debug("[CSS] inherit: no parent margin found, using 0");
        return 0.0f;
    }
    // Not inherit, resolve normally
    return resolve_length_value(lycon, prop_id, value);
}

// Helper function to copy border side values from parent to child for inherit
// side: 0=top, 1=right, 2=bottom, 3=left
static bool copy_border_side_inherit(LayoutContext* lycon, ViewSpan* span, int side, int32_t specificity) {
    DomElement* current = (DomElement*)lycon->view;
    if (!current || !current->parent || !current->parent->is_element()) return false;
    DomElement* parent = (DomElement*)current->parent;
    if (!parent->bound || !parent->bound->border) return false;

    BorderProp* pb = parent->bound->border;
    switch (side) {
        case 0: // top
            span->bound->border->width.top = pb->width.top;
            span->bound->border->width.top_specificity = specificity;
            span->bound->border->top_style = pb->top_style;
            span->bound->border->top_style_specificity = specificity;
            span->bound->border->top_color = pb->top_color;
            span->bound->border->top_color_specificity = specificity;
            log_debug("[CSS] border-top: inherit - width=%.2f", pb->width.top);
            break;
        case 1: // right
            span->bound->border->width.right = pb->width.right;
            span->bound->border->width.right_specificity = specificity;
            span->bound->border->right_style = pb->right_style;
            span->bound->border->right_style_specificity = specificity;
            span->bound->border->right_color = pb->right_color;
            span->bound->border->right_color_specificity = specificity;
            log_debug("[CSS] border-right: inherit - width=%.2f", pb->width.right);
            break;
        case 2: // bottom
            span->bound->border->width.bottom = pb->width.bottom;
            span->bound->border->width.bottom_specificity = specificity;
            span->bound->border->bottom_style = pb->bottom_style;
            span->bound->border->bottom_style_specificity = specificity;
            span->bound->border->bottom_color = pb->bottom_color;
            span->bound->border->bottom_color_specificity = specificity;
            log_debug("[CSS] border-bottom: inherit - width=%.2f", pb->width.bottom);
            break;
        case 3: // left
            span->bound->border->width.left = pb->width.left;
            span->bound->border->width.left_specificity = specificity;
            span->bound->border->left_style = pb->left_style;
            span->bound->border->left_style_specificity = specificity;
            span->bound->border->left_color = pb->left_color;
            span->bound->border->left_color_specificity = specificity;
            log_debug("[CSS] border-left: inherit - width=%.2f", pb->width.left);
            break;
    }
    return true;
}

// Helper function to resolve padding value with inherit support
// Returns the resolved padding value in pixels
// If value is 'inherit', looks up parent element's computed padding value
static float resolve_padding_with_inherit(LayoutContext* lycon, CssPropertyId prop_id, const CssValue* value) {
    // Check for inherit keyword
    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_INHERIT) {
        // Look up parent element's computed padding value
        DomElement* current = (DomElement*)lycon->view;
        if (current && current->parent && current->parent->is_element()) {
            DomElement* parent = (DomElement*)current->parent;
            // Check if parent has bound property
            if (parent->bound) {
                switch (prop_id) {
                    case CSS_PROPERTY_PADDING_TOP:
                        log_debug("[CSS] padding-top: inheriting %.2f from parent", parent->bound->padding.top);
                        return parent->bound->padding.top;
                    case CSS_PROPERTY_PADDING_RIGHT:
                        log_debug("[CSS] padding-right: inheriting %.2f from parent", parent->bound->padding.right);
                        return parent->bound->padding.right;
                    case CSS_PROPERTY_PADDING_BOTTOM:
                        log_debug("[CSS] padding-bottom: inheriting %.2f from parent", parent->bound->padding.bottom);
                        return parent->bound->padding.bottom;
                    case CSS_PROPERTY_PADDING_LEFT:
                        log_debug("[CSS] padding-left: inheriting %.2f from parent", parent->bound->padding.left);
                        return parent->bound->padding.left;
                    default:
                        break;
                }
            }
        }
        // No parent or parent has no padding, use 0
        log_debug("[CSS] padding inherit: no parent padding found, using 0");
        return 0.0f;
    }
    // Not inherit, resolve normally
    return resolve_length_value(lycon, prop_id, value);
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
// Grid Track Parsing Helpers
// ============================================================================

// Parse a single CssValue into a GridTrackSize
// Returns NULL if the value cannot be converted to a track size
// Forward declaration for recursive parsing
static GridTrackSize* parse_css_value_to_track_size(const CssValue* val);

// Parse minmax() function to GridTrackSize
static GridTrackSize* parse_minmax_function(const CssValue* val) {
    if (!val || val->type != CSS_VALUE_TYPE_FUNCTION) return NULL;
    if (!val->data.function->name || strcmp(val->data.function->name, "minmax") != 0) return NULL;
    if (val->data.function->arg_count < 2) return NULL;

    GridTrackSize* min_size = parse_css_value_to_track_size(val->data.function->args[0]);
    GridTrackSize* max_size = parse_css_value_to_track_size(val->data.function->args[1]);

    if (!min_size && !max_size) return NULL;

    GridTrackSize* track_size = create_grid_track_size(GRID_TRACK_SIZE_MINMAX, 0);
    if (track_size) {
        track_size->min_size = min_size;
        track_size->max_size = max_size;
        log_debug("[CSS]   parsed minmax(%s, %s)",
                  min_size ? "valid" : "null", max_size ? "valid" : "null");
    }
    return track_size;
}

// Parse repeat() function including auto-fill/auto-fit
static GridTrackSize* parse_repeat_function(const CssValue* val) {
    if (!val || val->type != CSS_VALUE_TYPE_FUNCTION) return NULL;
    if (!val->data.function->name || strcmp(val->data.function->name, "repeat") != 0) return NULL;
    if (val->data.function->arg_count < 2) return NULL;

    CssValue* count_val = val->data.function->args[0];
    bool is_auto_fill = false;
    bool is_auto_fit = false;
    int repeat_count = 0;

    // Check if first arg is auto-fill, auto-fit, or a number
    if (count_val->type == CSS_VALUE_TYPE_KEYWORD) {
        if (count_val->data.keyword == CSS_VALUE_AUTO_FILL) {
            is_auto_fill = true;
            log_debug("[CSS] repeat(auto-fill, ...) detected");
        } else if (count_val->data.keyword == CSS_VALUE_AUTO_FIT) {
            is_auto_fit = true;
            log_debug("[CSS] repeat(auto-fit, ...) detected");
        }
    } else if (count_val->type == CSS_VALUE_TYPE_NUMBER) {
        repeat_count = (int)count_val->data.number.value;
        log_debug("[CSS] repeat(%d, ...) detected", repeat_count);
    }

    if (!is_auto_fill && !is_auto_fit && repeat_count <= 0) {
        log_debug("[CSS] Invalid repeat() count");
        return NULL;
    }

    // Parse the track sizes in the repeat pattern
    int track_count = val->data.function->arg_count - 1;
    GridTrackSize** repeat_tracks = (GridTrackSize**)mem_calloc(track_count, sizeof(GridTrackSize*), MEM_CAT_LAYOUT);
    if (!repeat_tracks) return NULL;

    int actual_track_count = 0;
    for (int i = 1; i < val->data.function->arg_count && actual_track_count < track_count; i++) {
        GridTrackSize* ts = parse_css_value_to_track_size(val->data.function->args[i]);
        if (ts) {
            repeat_tracks[actual_track_count++] = ts;
        }
    }

    if (actual_track_count == 0) {
        mem_free(repeat_tracks);
        return NULL;
    }

    // Create the repeat track size
    GridTrackSize* track_size = (GridTrackSize*)mem_calloc(1, sizeof(GridTrackSize), MEM_CAT_LAYOUT);
    if (!track_size) {
        mem_free(repeat_tracks);
        return NULL;
    }

    track_size->type = GRID_TRACK_SIZE_REPEAT;
    track_size->repeat_count = repeat_count;
    track_size->repeat_tracks = repeat_tracks;
    track_size->repeat_track_count = actual_track_count;
    track_size->is_auto_fill = is_auto_fill;
    track_size->is_auto_fit = is_auto_fit;

    log_debug("[CSS]   parsed repeat(%s%d, %d tracks)",
              is_auto_fill ? "auto-fill, " : (is_auto_fit ? "auto-fit, " : ""),
              repeat_count, actual_track_count);

    return track_size;
}

static GridTrackSize* parse_css_value_to_track_size(const CssValue* val) {
    if (!val) return NULL;

    GridTrackSize* track_size = NULL;

    if (val->type == CSS_VALUE_TYPE_LENGTH) {
        if (val->data.length.unit == CSS_UNIT_FR) {
            // Fractional unit - store as int * 100 for precision
            int fr_value = (int)(val->data.length.value * 100);
            track_size = create_grid_track_size(GRID_TRACK_SIZE_FR, fr_value);
            log_debug("[CSS]   parsed track: %.2ffr", val->data.length.value);
        } else {
            // Regular length (px, em, etc.)
            int px_value = (int)val->data.length.value;
            track_size = create_grid_track_size(GRID_TRACK_SIZE_LENGTH, px_value);
            log_debug("[CSS]   parsed track: %dpx", px_value);
        }
    } else if (val->type == CSS_VALUE_TYPE_PERCENTAGE) {
        int percent = (int)val->data.percentage.value;
        track_size = create_grid_track_size(GRID_TRACK_SIZE_PERCENTAGE, percent);
        track_size->is_percentage = true;
        log_debug("[CSS]   parsed track: %d%%", percent);
    } else if (val->type == CSS_VALUE_TYPE_KEYWORD) {
        if (val->data.keyword == CSS_VALUE_AUTO) {
            track_size = create_grid_track_size(GRID_TRACK_SIZE_AUTO, 0);
            log_debug("[CSS]   parsed track: auto");
        } else if (val->data.keyword == CSS_VALUE_MIN_CONTENT) {
            track_size = create_grid_track_size(GRID_TRACK_SIZE_MIN_CONTENT, 0);
            log_debug("[CSS]   parsed track: min-content");
        } else if (val->data.keyword == CSS_VALUE_MAX_CONTENT) {
            track_size = create_grid_track_size(GRID_TRACK_SIZE_MAX_CONTENT, 0);
            log_debug("[CSS]   parsed track: max-content");
        }
    } else if (val->type == CSS_VALUE_TYPE_FUNCTION) {
        // Handle function types: minmax(), repeat(), fit-content()
        const char* func_name = val->data.function->name;
        if (func_name) {
            if (strcmp(func_name, "minmax") == 0) {
                track_size = parse_minmax_function(val);
            } else if (strcmp(func_name, "repeat") == 0) {
                track_size = parse_repeat_function(val);
            } else if (strcmp(func_name, "fit-content") == 0) {
                // fit-content(<length-percentage>)
                track_size = create_grid_track_size(GRID_TRACK_SIZE_FIT_CONTENT, 0);
                if (track_size && val->data.function->arg_count > 0) {
                    CssValue* arg = val->data.function->args[0];
                    if (arg->type == CSS_VALUE_TYPE_LENGTH) {
                        track_size->fit_content_limit = (int)arg->data.length.value;
                        track_size->is_percentage = false;
                        log_debug("[CSS]   parsed fit-content(%dpx)", track_size->fit_content_limit);
                    } else if (arg->type == CSS_VALUE_TYPE_PERCENTAGE) {
                        track_size->fit_content_limit = (int)arg->data.percentage.value;
                        track_size->is_percentage = true;
                        log_debug("[CSS]   parsed fit-content(%d%%)", track_size->fit_content_limit);
                    }
                }
            }
        }
    }

    return track_size;
}

// Parse grid track list from CSS value list, handling repeat() functions
// The list may contain: lengths, percentages, keywords, or repeat(count, track-size)
// Parse grid track list from CSS value list
// Now supports: lengths, percentages, keywords, minmax(), repeat() with auto-fill/auto-fit
static void parse_grid_track_list(const CssValue* value, GridTrackList** track_list_ptr) {
    if (!value || value->type != CSS_VALUE_TYPE_LIST || !track_list_ptr) return;

    int count = value->data.list.count;
    CssValue** values = value->data.list.values;

    log_debug("[CSS] Parsing grid track list with %d values", count);

    // First pass: count tracks (auto-fill/auto-fit get 1 track as placeholder)
    int total_tracks = 0;
    for (int i = 0; i < count; i++) {
        CssValue* val = values[i];
        if (!val) continue;

        if (val->type == CSS_VALUE_TYPE_FUNCTION) {
            const char* func_name = val->data.function->name;
            if (func_name && strcmp(func_name, "repeat") == 0) {
                CssValue* count_val = val->data.function->arg_count > 0 ? val->data.function->args[0] : NULL;
                if (count_val && count_val->type == CSS_VALUE_TYPE_NUMBER) {
                    // Fixed repeat count - expand
                    int repeat_count = (int)count_val->data.number.value;
                    int track_vals = val->data.function->arg_count - 1;
                    total_tracks += repeat_count * (track_vals > 0 ? track_vals : 1);
                } else {
                    // auto-fill or auto-fit - just count as 1 (will be expanded at layout time)
                    total_tracks += 1;
                }
            } else {
                // minmax(), fit-content() - count as 1 track
                total_tracks += 1;
            }
        } else if (val->type == CSS_VALUE_TYPE_LENGTH ||
                   val->type == CSS_VALUE_TYPE_PERCENTAGE ||
                   val->type == CSS_VALUE_TYPE_KEYWORD) {
            total_tracks++;
        }
        // Legacy CUSTOM handling for old-style parsing
        else if (val->type == CSS_VALUE_TYPE_CUSTOM && val->data.custom_property.name) {
            const char* name = val->data.custom_property.name;
            if (strncmp(name, "repeat(", 7) == 0 || strcmp(name, "repeat") == 0) {
                if (i + 1 < count && values[i + 1] && values[i + 1]->type == CSS_VALUE_TYPE_NUMBER) {
                    int repeat_count = (int)values[i + 1]->data.number.value;
                    int track_values = 0;
                    for (int j = i + 2; j < count; j++) {
                        CssValue* tv = values[j];
                        if (!tv || tv->type == CSS_VALUE_TYPE_CUSTOM) break;
                        if (tv->type == CSS_VALUE_TYPE_LENGTH ||
                            tv->type == CSS_VALUE_TYPE_PERCENTAGE ||
                            tv->type == CSS_VALUE_TYPE_KEYWORD) {
                            track_values++;
                        }
                    }
                    total_tracks += repeat_count * (track_values > 0 ? track_values : 1);
                }
            }
        }
    }

    if (total_tracks == 0) {
        log_debug("[CSS] No tracks found in list");
        return;
    }

    // Create or resize track list
    if (!*track_list_ptr) {
        *track_list_ptr = create_grid_track_list(total_tracks);
    } else {
        (*track_list_ptr)->track_count = 0;
    }
    GridTrackList* track_list = *track_list_ptr;

    log_debug("[CSS] Parsing %d values into %d allocated tracks", count, total_tracks);

    // Second pass: parse values
    int i = 0;
    while (i < count && track_list->track_count < track_list->allocated_tracks) {
        CssValue* val = values[i];
        if (!val) { i++; continue; }

        // Handle FUNCTION type (modern parsing)
        if (val->type == CSS_VALUE_TYPE_FUNCTION) {
            const char* func_name = val->data.function->name;
            if (func_name && strcmp(func_name, "repeat") == 0) {
                // Check if auto-fill/auto-fit or fixed count
                CssValue* count_val = val->data.function->arg_count > 0 ? val->data.function->args[0] : NULL;
                bool is_auto = count_val && count_val->type == CSS_VALUE_TYPE_KEYWORD &&
                               (count_val->data.keyword == CSS_VALUE_AUTO_FILL ||
                                count_val->data.keyword == CSS_VALUE_AUTO_FIT);

                if (is_auto) {
                    // Keep repeat() as a single track - will be expanded at layout time
                    GridTrackSize* ts = parse_repeat_function(val);
                    if (ts) {
                        track_list->tracks[track_list->track_count++] = ts;
                        track_list->is_repeat = true;
                    }
                } else if (count_val && count_val->type == CSS_VALUE_TYPE_NUMBER) {
                    // Fixed repeat count - expand inline
                    int repeat_count = (int)count_val->data.number.value;
                    for (int r = 0; r < repeat_count && track_list->track_count < track_list->allocated_tracks; r++) {
                        for (int a = 1; a < val->data.function->arg_count && track_list->track_count < track_list->allocated_tracks; a++) {
                            GridTrackSize* ts = parse_css_value_to_track_size(val->data.function->args[a]);
                            if (ts) {
                                track_list->tracks[track_list->track_count++] = ts;
                            }
                        }
                    }
                }
            } else {
                // minmax() or other function
                GridTrackSize* ts = parse_css_value_to_track_size(val);
                if (ts) {
                    track_list->tracks[track_list->track_count++] = ts;
                }
            }
            i++;
            continue;
        }

        // Legacy CUSTOM handling for old-style parsing
        if (val->type == CSS_VALUE_TYPE_CUSTOM && val->data.custom_property.name) {
            const char* name = val->data.custom_property.name;
            if (strncmp(name, "repeat(", 7) == 0 || strcmp(name, "repeat") == 0) {
                i++; // Move past "repeat("
                if (i >= count || !values[i] || values[i]->type != CSS_VALUE_TYPE_NUMBER) {
                    continue;
                }
                int repeat_count = (int)values[i]->data.number.value;
                i++; // Move past count

                const CssValue* repeat_tracks[16];
                int repeat_track_count = 0;
                while (i < count && repeat_track_count < 16) {
                    CssValue* tv = values[i];
                    if (!tv) break;
                    if (tv->type == CSS_VALUE_TYPE_CUSTOM) { i++; break; }
                    if (tv->type == CSS_VALUE_TYPE_LENGTH ||
                        tv->type == CSS_VALUE_TYPE_PERCENTAGE ||
                        tv->type == CSS_VALUE_TYPE_KEYWORD) {
                        repeat_tracks[repeat_track_count++] = tv;
                    }
                    i++;
                }

                for (int r = 0; r < repeat_count && track_list->track_count < track_list->allocated_tracks; r++) {
                    for (int t = 0; t < repeat_track_count && track_list->track_count < track_list->allocated_tracks; t++) {
                        GridTrackSize* ts = parse_css_value_to_track_size(repeat_tracks[t]);
                        if (ts) {
                            track_list->tracks[track_list->track_count++] = ts;
                        }
                    }
                }
                continue;
            }
            i++;
            continue;
        }

        // Regular track value
        GridTrackSize* ts = parse_css_value_to_track_size(val);
        if (ts) {
            track_list->tracks[track_list->track_count++] = ts;
        }
        i++;
    }

    log_debug("[CSS] Parsed %d tracks total", track_list->track_count);
}

// ============================================================================
// Main Style Resolution
// ============================================================================

// Callback for AVL tree traversal - first pass (font properties only)
static bool resolve_font_property_callback(AvlNode* node, void* context) {
    LayoutContext* lycon = (LayoutContext*)context;
    StyleNode* style_node = (StyleNode*)node->declaration;
    CssPropertyId prop_id = (CssPropertyId)node->property_id;

    // Only process font-related properties in first pass
    // These must be resolved before width/height/etc. which may use em/ex units
    if (prop_id != CSS_PROPERTY_FONT &&
        prop_id != CSS_PROPERTY_FONT_SIZE &&
        prop_id != CSS_PROPERTY_FONT_FAMILY &&
        prop_id != CSS_PROPERTY_FONT_WEIGHT &&
        prop_id != CSS_PROPERTY_FONT_STYLE &&
        prop_id != CSS_PROPERTY_FONT_VARIANT &&
        prop_id != CSS_PROPERTY_LINE_HEIGHT) {
        return true; // skip, will process in second pass
    }

    CssDeclaration* decl = style_node ? style_node->winning_decl : NULL;
    if (!decl) return true;

    log_debug("[Lambda CSS] First pass - resolving font property %d", prop_id);
    resolve_css_property(prop_id, decl, lycon);
    return true;
}

// Callback for AVL tree traversal - second pass (non-font properties)
static bool resolve_non_font_property_callback(AvlNode* node, void* context) {
    LayoutContext* lycon = (LayoutContext*)context;
    StyleNode* style_node = (StyleNode*)node->declaration;
    CssPropertyId prop_id = (CssPropertyId)node->property_id;

    // Skip font properties (already processed in first pass)
    if (prop_id == CSS_PROPERTY_FONT ||
        prop_id == CSS_PROPERTY_FONT_SIZE ||
        prop_id == CSS_PROPERTY_FONT_FAMILY ||
        prop_id == CSS_PROPERTY_FONT_WEIGHT ||
        prop_id == CSS_PROPERTY_FONT_STYLE ||
        prop_id == CSS_PROPERTY_FONT_VARIANT ||
        prop_id == CSS_PROPERTY_LINE_HEIGHT) {
        return true; // already processed
    }

    CssDeclaration* decl = style_node ? style_node->winning_decl : NULL;
    if (!decl) return true;

    log_debug("[Lambda CSS] Second pass - resolving property %d", prop_id);
    resolve_css_property(prop_id, decl, lycon);
    return true;
}

void resolve_css_styles(DomElement* dom_elem, LayoutContext* lycon) {
    assert(dom_elem);
    log_debug("[Lambda CSS] Resolving styles for element <%s>", dom_elem->tag_name);

    // iterate through specified_style AVL tree
    StyleTree* style_tree = dom_elem->specified_style;
    if (!style_tree || !style_tree->tree) {
        log_debug("[Lambda CSS] No style tree found for element");
        return;
    }
    log_debug("[Lambda CSS] Style tree has %d nodes", style_tree->tree->node_count);

    // Two-pass resolution:
    // 1. First pass: Resolve font properties (font, font-size, font-family, etc.)
    //    This ensures font metrics are available for em/ex unit calculations
    // 2. Second pass: Resolve all other properties
    int font_processed = avl_tree_foreach_inorder(style_tree->tree, resolve_font_property_callback, lycon);
    log_debug("[Lambda CSS] First pass - processed %d font properties", font_processed);

    // Browser quirk: monospace generic family uses 13px default "medium" size (not 16px).
    // When font-family transitions to monospace and no explicit font-size was set,
    // apply the 13/16 scaling ratio to match browser behavior.
    {
        ViewSpan* span = (ViewSpan*)lycon->view;
        if (span && span->font && span->font->family) {
            bool is_mono = str_ieq_const(span->font->family, strlen(span->font->family), "monospace");
            if (is_mono) {
                bool has_explicit_size = avl_tree_search(style_tree->tree, CSS_PROPERTY_FONT_SIZE) != nullptr ||
                                         avl_tree_search(style_tree->tree, CSS_PROPERTY_FONT) != nullptr;
                if (!has_explicit_size) {
                    bool parent_is_mono = lycon->font.style && lycon->font.style->family &&
                        str_ieq_const(lycon->font.style->family, strlen(lycon->font.style->family), "monospace");
                    if (!parent_is_mono && span->font->font_size > 0) {
                        // Avoid double-applying: HTML defaults may have already applied this
                        // Check if font_size is still the parent's value (not already adjusted)
                        float parent_size = lycon->font.style ? lycon->font.style->font_size : 16.0f;
                        if (span->font->font_size == parent_size) {
                            span->font->font_size = span->font->font_size * 13.0f / 16.0f;
                            log_debug("[CSS] Monospace font-size quirk: %.1f -> %.1f", parent_size, span->font->font_size);
                        }
                    }
                }
            }
        }
    }

    // Set up FreeType font face if a font-family was specified for this element
    // This ensures ex/ch units use the correct font metrics
    if (font_processed > 0) {
        ViewSpan* span = (ViewSpan*)lycon->view;
        // Check if font or font-family was explicitly set on this element
        bool has_font = avl_tree_search(style_tree->tree, CSS_PROPERTY_FONT) != nullptr ||
                       avl_tree_search(style_tree->tree, CSS_PROPERTY_FONT_FAMILY) != nullptr;
        if (has_font && span && span->font && span->font->family && lycon->ui_context) {
            setup_font(lycon->ui_context, &lycon->font, span->font);
        }
    }

    int other_processed = avl_tree_foreach_inorder(style_tree->tree, resolve_non_font_property_callback, lycon);
    log_debug("[Lambda CSS] Second pass - processed %d other properties", other_processed);

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
        CSS_PROPERTY_TEXT_INDENT,
        CSS_PROPERTY_LETTER_SPACING,
        CSS_PROPERTY_WORD_SPACING,
        CSS_PROPERTY_WHITE_SPACE,
        CSS_PROPERTY_VISIBILITY,
        CSS_PROPERTY_EMPTY_CELLS,
    };
    static const size_t num_inheritable = sizeof(inheritable_props) / sizeof(inheritable_props[0]);

    // Get parent's style tree for inheritance
    DomElement* parent = dom_elem->parent ? static_cast<DomElement*>(dom_elem->parent) : nullptr;
    StyleTree* parent_tree = (parent && parent->specified_style)
                             ? parent->specified_style : NULL;

    // Run inheritance check if parent has either specified_style or computed font
    // This handles anonymous table elements that have font but no specified_style
    if (parent_tree || (parent && parent->font)) {
        log_debug("[Lambda CSS] Checking inheritance from parent <%s> (has_style=%d, has_font=%d)",
                parent->tag_name, parent_tree != NULL, parent->font != nullptr);

        for (size_t i = 0; i < num_inheritable; i++) {
            CssPropertyId prop_id = inheritable_props[i];

            // Check if this property is already set on the element
            CssDeclaration* existing = style_tree_get_declaration(style_tree, prop_id);
            if (existing) {
                // Property is explicitly set, don't inherit
                continue;
            }

            // Special case: font shorthand sets font-family directly on span->font
            // without creating a CssDeclaration, so also check if font->family is set
            if (prop_id == CSS_PROPERTY_FONT_FAMILY) {
                ViewSpan* span = (ViewSpan*)lycon->view;
                if (span->font && span->font->family) {
                    log_debug("[FONT INHERIT] Skipping inheritance - font-family already set via shorthand: %s",
                             span->font->family);
                    continue;
                }
            }

            // Property not set, check parent chain for inherited declaration
            // Walk up the parent chain until we find a declaration
            DomElement* ancestor = dom_elem->parent ? static_cast<DomElement*>(dom_elem->parent) : nullptr;
            CssDeclaration* inherited_decl = NULL;

            // Special handling for font-family: also check ancestor's computed font->family
            // This handles cases where font shorthand was used (sets font->family without
            // creating a CSS_PROPERTY_FONT_FAMILY declaration)
            // Apply for any parent with computed font->family (handles font shorthand case)
            if (prop_id == CSS_PROPERTY_FONT_FAMILY && ancestor && ancestor->font && ancestor->font->family) {
                log_debug("[FONT INHERIT] Found computed font-family in parent <%s>: %s",
                    ancestor->tag_name ? ancestor->tag_name : "?", ancestor->font->family);
                ViewSpan* span = (ViewSpan*)lycon->view;
                if (!span->font) {
                    span->font = alloc_font_prop(lycon);
                }
                // Copy font-family from parent's computed font
                span->font->family = ancestor->font->family;
                continue;  // Move to next property
            }

            // Special handling for font-size: also check ancestor's computed font->font_size
            // This handles cases where ancestor is an anonymous box with no specified_style
            // (e.g., anonymous table-row wrapping table-cells)
            // ONLY apply for anonymous parents (have font but no specified_style)
            if (prop_id == CSS_PROPERTY_FONT_SIZE && ancestor && !ancestor->specified_style
                && ancestor->font && ancestor->font->font_size > 0) {
                log_debug("[FONT INHERIT] Found computed font-size in anonymous parent <%s>: %.1f",
                    ancestor->tag_name ? ancestor->tag_name : "?", ancestor->font->font_size);
                ViewSpan* span = (ViewSpan*)lycon->view;
                if (!span->font) {
                    span->font = alloc_font_prop(lycon);
                }
                // Copy font-size from parent's computed font
                span->font->font_size = ancestor->font->font_size;
                continue;  // Move to next property
            }

            while (ancestor && !inherited_decl) {
                if (ancestor->specified_style) {
                    inherited_decl = style_tree_get_declaration(ancestor->specified_style, prop_id);
                    if (inherited_decl && inherited_decl->value) {
                        if (prop_id == CSS_PROPERTY_FONT_FAMILY) {
                            log_debug("[FONT INHERIT] Found font-family in ancestor <%s>, value_type=%d",
                                ancestor->tag_name ? ancestor->tag_name : "?", inherited_decl->value->type);
                        }
                        break; // Found it!
                    }
                }
                // BUG FIX: Was using dom_elem->parent instead of ancestor->parent!
                ancestor = ancestor->parent ? static_cast<DomElement*>(ancestor->parent) : nullptr;
            }

            if (inherited_decl && inherited_decl->value) {
                log_debug("[Lambda CSS] Inheriting property %d from ancestor <%s>",
                         prop_id, ancestor ? ancestor->tag_name : "unknown");

                // CRITICAL FIX: For font-size, do NOT re-resolve the specified value
                // because em/percentage values would compound incorrectly.
                // Instead, copy the computed font-size from lycon->font.style
                // which already has the parent's computed font-size.
                if (prop_id == CSS_PROPERTY_FONT_SIZE) {
                    log_debug("[Lambda CSS] Inheriting computed font-size from parent: %.1f",
                        lycon->font.style ? lycon->font.style->font_size : 16.0f);
                    ViewSpan* span = (ViewSpan*)lycon->view;
                    if (!span->font) {
                        span->font = alloc_font_prop(lycon);
                    }
                    // font is already correctly set via alloc_font_prop copying lycon->font.style
                    continue;
                }

                // Apply the inherited property using the ancestor's declaration
                resolve_css_property(prop_id, inherited_decl, lycon);
            }
        }
    }

    // Finalize border widths: per CSS spec, border-width computes to 0
    // when border-style is 'none' or 'hidden' (or unset, which defaults to 'none')
    ViewSpan* span = (ViewSpan*)lycon->view;
    if (span->bound && span->bound->border) {
        BorderProp* border = span->bound->border;
        // Check each side: if style is none, hidden, or unset (_UNDEF), width is 0
        if (border->top_style == CSS_VALUE_NONE || border->top_style == CSS_VALUE_HIDDEN ||
            border->top_style == CSS_VALUE__UNDEF) {
            if (border->width.top != 0) {
                log_debug("[CSS] Border-top-style is none/hidden/undef, zeroing width from %.1f to 0",
                          border->width.top);
                border->width.top = 0;
            }
        }
        if (border->right_style == CSS_VALUE_NONE || border->right_style == CSS_VALUE_HIDDEN ||
            border->right_style == CSS_VALUE__UNDEF) {
            if (border->width.right != 0) {
                log_debug("[CSS] Border-right-style is none/hidden/undef, zeroing width from %.1f to 0",
                          border->width.right);
                border->width.right = 0;
            }
        }
        if (border->bottom_style == CSS_VALUE_NONE || border->bottom_style == CSS_VALUE_HIDDEN ||
            border->bottom_style == CSS_VALUE__UNDEF) {
            if (border->width.bottom != 0) {
                log_debug("[CSS] Border-bottom-style is none/hidden/undef, zeroing width from %.1f to 0",
                          border->width.bottom);
                border->width.bottom = 0;
            }
        }
        if (border->left_style == CSS_VALUE_NONE || border->left_style == CSS_VALUE_HIDDEN ||
            border->left_style == CSS_VALUE__UNDEF) {
            if (border->width.left != 0) {
                log_debug("[CSS] Border-left-style is none/hidden/undef, zeroing width from %.1f to 0",
                          border->width.left);
                border->width.left = 0;
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
    if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_PERCENTAGE || value->type == CSS_VALUE_TYPE_NUMBER) {
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

void resolve_css_property(CssPropertyId prop_id, const CssDeclaration* decl, LayoutContext* lycon) {
    log_debug("[Lambda CSS Property] resolve_css_property called: prop_id=%d", prop_id);
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

    // Handle CSS custom properties (--variable-name: value)
    if (decl->property_name && decl->property_name[0] == '-' && decl->property_name[1] == '-') {
        // This is a CSS custom property, store it
        DomElement* element = (DomElement*)lycon->view;

        // Create new custom property entry
        CssCustomProp* new_var = (CssCustomProp*)pool_calloc(lycon->doc->view_tree->pool, sizeof(CssCustomProp));
        if (new_var) {
            // Allocate name from arena
            size_t name_len = strlen(decl->property_name);
            char* name_copy = (char*)arena_alloc(lycon->doc->arena, name_len + 1);
            if (name_copy) {
                memcpy(name_copy, decl->property_name, name_len + 1);
                new_var->name = name_copy;
                new_var->value = value;
                new_var->next = element->css_variables;
                element->css_variables = new_var;
                log_debug("[CSS] Stored custom property: %s", decl->property_name);
            }
        }
        return;  // Custom properties don't have standard processing
    }

    // Dispatch based on property ID
    // Parallel implementation to resolve_element_style() in resolve_style.cpp
    ViewSpan* span = (ViewSpan*)lycon->view;
    ViewBlock* block = (ViewBlock*)span;  // lycon->view->view_type != RDT_VIEW_INLINE ? (ViewBlock*)lycon->view : NULL;

    switch (prop_id) {
        // ===== GROUP 1: Core Typography & Color =====
        case CSS_PROPERTY_COLOR: {
            log_debug("[CSS] Processing color property");
            if (!span->in_line) {
                span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
            }
            span->in_line->color = resolve_color_value(lycon, value);
            break;
        }

        // ===== Font Shorthand (must be before individual font properties) =====
        case CSS_PROPERTY_FONT: {
            log_debug("[CSS] Processing font shorthand property");
            if (!span->font) { span->font = alloc_font_prop(lycon); }

            // Font shorthand format: [font-style] [font-variant] [font-weight] [font-stretch] font-size[/line-height] font-family
            // The last value (or values) is always font-family
            // font-size is required and comes before font-family

            // Handle list of values (common case for shorthand)
            if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count >= 2) {
                size_t count = value->data.list.count;
                log_debug("[CSS] Font shorthand: %zu values", count);

                // Last value(s) are font-family - find the font-size value first
                // Scan backwards: last is family, find size
                const CssValue* family_value = nullptr;
                const CssValue* size_value = nullptr;
                const CssValue* line_height_value = nullptr;
                const CssValue* weight_value = nullptr;
                const CssValue* style_value = nullptr;
                size_t family_start_index = count; // Index where font-family starts

                for (size_t i = 0; i < count; i++) {
                    const CssValue* v = value->data.list.values[i];
                    if (!v) continue;

                    log_debug("[CSS] Font shorthand value[%zu]: type=%d", i, v->type);

                    if (v->type == CSS_VALUE_TYPE_LENGTH || v->type == CSS_VALUE_TYPE_PERCENTAGE) {
                        if (!size_value) {
                            // First length is font-size
                            size_value = v;
                            log_debug("[CSS] Font shorthand: found font-size at [%zu]", i);

                            // Check for /line-height syntax: next values might be "/" and line-height
                            size_t next_idx = i + 1;

                            // Skip "/" delimiter if present
                            if (next_idx < count) {
                                const CssValue* next = value->data.list.values[next_idx];
                                // Check if next is "/" (could be CUSTOM type with name "/")
                                if (next && next->type == CSS_VALUE_TYPE_CUSTOM &&
                                    next->data.custom_property.name &&
                                    strcmp(next->data.custom_property.name, "/") == 0) {
                                    log_debug("[CSS] Font shorthand: found '/' delimiter at [%zu]", next_idx);
                                    next_idx++;

                                    // Next should be line-height
                                    if (next_idx < count) {
                                        const CssValue* lh = value->data.list.values[next_idx];
                                        if (lh && (lh->type == CSS_VALUE_TYPE_LENGTH ||
                                                   lh->type == CSS_VALUE_TYPE_PERCENTAGE ||
                                                   lh->type == CSS_VALUE_TYPE_NUMBER)) {
                                            line_height_value = lh;
                                            log_debug("[CSS] Font shorthand: found line-height at [%zu]", next_idx);
                                            next_idx++;
                                        }
                                    }
                                }
                            }

                            // Everything from next_idx onwards is font-family
                            family_start_index = next_idx;
                            if (family_start_index < count) {
                                family_value = value->data.list.values[family_start_index];
                            }
                            break;  // Found size, done scanning for size
                        }
                    } else if (v->type == CSS_VALUE_TYPE_KEYWORD) {
                        const CssEnumInfo* info = css_enum_info(v->data.keyword);
                        if (info) {
                            log_debug("[CSS] Font shorthand keyword: %s (group=%d)", info->name, info->group);
                            if (info->group == CSS_VALUE_GROUP_FONT_WEIGHT) {
                                weight_value = v;
                            } else if (info->group == CSS_VALUE_GROUP_FONT_STYLE) {
                                style_value = v;
                            } else if (v->data.keyword >= CSS_VALUE_SERIF && v->data.keyword <= CSS_VALUE_FANGSONG) {
                                // Generic font family keyword
                                family_value = v;
                            }
                        }
                    } else if (v->type == CSS_VALUE_TYPE_STRING) {
                        // String is font-family name
                        family_value = v;
                        log_debug("[CSS] Font shorthand: found string font-family '%s'", v->data.string);
                    } else if (v->type == CSS_VALUE_TYPE_CUSTOM && v->data.custom_property.name) {
                        // Custom identifier - could be font-family or "/" delimiter
                        // Skip "/" as it's the line-height separator
                        if (strcmp(v->data.custom_property.name, "/") != 0) {
                            family_value = v;
                            log_debug("[CSS] Font shorthand: found custom font-family '%s'", v->data.custom_property.name);
                        }
                    }
                }

                // Apply font-size
                if (size_value) {
                    float font_size = resolve_length_value(lycon, CSS_PROPERTY_FONT_SIZE, size_value);
                    if (font_size > 0) {
                        span->font->font_size = font_size;
                        log_debug("[CSS] Font shorthand: set font-size = %.2f", font_size);
                    }
                }

                // Apply line-height
                if (line_height_value) {
                    if (!span->blk) { span->blk = alloc_block_prop(lycon); }
                    span->blk->line_height = line_height_value;
                    log_debug("[CSS] Font shorthand: set line-height");
                }

                // Apply font-family
                if (family_value) {
                    log_debug("[CSS] Font shorthand: applying font-family, value type=%d", family_value->type);
                    if (family_value->type == CSS_VALUE_TYPE_STRING) {
                        span->font->family = (char*)family_value->data.string;
                        log_debug("[CSS] Font shorthand: set font-family from STRING = '%s'", span->font->family);
                    } else if (family_value->type == CSS_VALUE_TYPE_KEYWORD) {
                        const CssEnumInfo* info = css_enum_info(family_value->data.keyword);
                        span->font->family = info ? (char*)info->name : nullptr;
                        log_debug("[CSS] Font shorthand: set font-family from KEYWORD = '%s'", span->font->family);
                    } else if (family_value->type == CSS_VALUE_TYPE_CUSTOM && family_value->data.custom_property.name) {
                        span->font->family = (char*)family_value->data.custom_property.name;
                        log_debug("[CSS] Font shorthand: set font-family from CUSTOM = '%s'", span->font->family);
                    }

                } else {
                    log_debug("[CSS] Font shorthand: NO font-family found!");
                }

                // Apply font-weight if specified
                if (weight_value) {
                    span->font->font_weight = map_font_weight(weight_value);
                    log_debug("[CSS] Font shorthand: set font-weight");
                }

                // Apply font-style if specified
                if (style_value) {
                    span->font->font_style = style_value->data.keyword;
                    log_debug("[CSS] Font shorthand: set font-style");
                }
            }
            break;
        }

        case CSS_PROPERTY_FONT_SIZE: {
            log_debug("[CSS] Processing font-size property");
            if (!span->font) { span->font = alloc_font_prop(lycon); }

            float font_size = 0.0f;  bool valid = false;
            // For font-size, em/percentage are relative to PARENT font size, not element's current
            // lycon->font.style->font_size holds the inherited/parent font size
            float parent_font_size = lycon->font.style && lycon->font.style->font_size > 0
                ? lycon->font.style->font_size : 16.0f;
            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                // Special handling for em units: em is relative to parent font size for font-size property
                if (value->data.length.unit == CSS_UNIT_EM) {
                    font_size = value->data.length.value * parent_font_size;
                    log_debug("[CSS] Font size em: %.2fem -> %.2f px (parent size: %.2f px)",
                              value->data.length.value, font_size, parent_font_size);
                } else {
                    font_size = resolve_length_value(lycon, prop_id, value);
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
                font_size = parent_font_size * (value->data.percentage.value / 100.0f);
                log_debug("[CSS] Font size percentage: %.2f%% -> %.2f px (parent size: %.2f px)",
                          value->data.percentage.value, font_size, parent_font_size);
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
            // map CSS font weight to enum
            span->font->font_weight = map_font_weight(value);
            break;
        }

        case CSS_PROPERTY_FONT_FAMILY: {
            log_debug("[CSS] Processing font-family property");
            if (!span->font) {
                span->font = alloc_font_prop(lycon);
            }

            // Helper lambda to check if a font family exists in the database, @font-face, or is a generic family
            auto is_font_available = [&](const char* family) -> bool {
                if (!family) return false;
                // Generic font families are always "available" (resolved later)
                // Generic font families are always "available" (resolved later)
                // CSS 2.1: font family names are case-insensitive
                size_t flen = strlen(family);
                if (str_ieq(family, flen, "serif", 5) ||
                    str_ieq(family, flen, "sans-serif", 10) ||
                    str_ieq(family, flen, "monospace", 9) ||
                    str_ieq(family, flen, "cursive", 7) ||
                    str_ieq(family, flen, "fantasy", 7) ||
                    str_ieq(family, flen, "system-ui", 9) ||
                    str_ieq(family, flen, "ui-serif", 8) ||
                    str_ieq(family, flen, "ui-sans-serif", 13) ||
                    str_ieq(family, flen, "ui-monospace", 12) ||
                    str_ieq(family, flen, "ui-rounded", 10) ||
                    str_ieq(family, flen, "-apple-system", 13) ||
                    str_ieq(family, flen, "BlinkMacSystemFont", 18)) {
                    return true;
                }
                // Check @font-face descriptors first (custom fonts take precedence)
                if (lycon->ui_context && lycon->ui_context->font_faces && lycon->ui_context->font_face_count > 0) {
                    for (int i = 0; i < lycon->ui_context->font_face_count; i++) {
                        FontFaceDescriptor* desc = lycon->ui_context->font_faces[i];
                        if (desc && desc->family_name && str_ieq(desc->family_name, strlen(desc->family_name), family, strlen(family))) {
                            return true;  // Found in @font-face declarations
                        }
                    }
                }
                // Check system font database
                if (lycon->ui_context && lycon->ui_context->font_ctx) {
                    return font_family_exists(lycon->ui_context->font_ctx, family);
                }
                return false;  // No database available, can't verify
            };

            if (value->type == CSS_VALUE_TYPE_STRING) {
                // Font family name as string (quotes already stripped during parsing)
                span->font->family = (char*)value->data.string;
                log_debug("[CSS] Set font-family from STRING: '%s'", span->font->family);
            }
            else if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
                // Custom identifier font family (e.g., "ahem" without quotes)
                span->font->family = (char*)value->data.custom_property.name;
                log_debug("[CSS] Set font-family from CUSTOM: '%s'", span->font->family);
            }
            else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // Keyword font family - check if generic or specific
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                span->font->family = info ? (char*)info->name : NULL;
                log_debug("[CSS] Set font-family from KEYWORD: '%s'", span->font->family);
            }
            else if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
                // List of font families (e.g., "Charter, Linux Libertine, Times New Roman, serif")
                // Try each font in order until we find one that's available
                for (size_t i = 0; i < value->data.list.count; i++) {
                    CssValue* item = value->data.list.values[i];
                    if (!item) continue;
                    const char* family = NULL;
                    log_debug("[CSS] Font family list item[%zu] type: %d", i, item->type);
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
                        // Check if this font is available
                        if (is_font_available(family)) {
                            span->font->family = (char*)family;
                            log_debug("[CSS] Font family from list[%zu]: %s (available)", i, family);
                            break;
                        } else {
                            log_debug("[CSS] Font family '%s' not available, trying next", family);
                        }
                    }
                }
                // If no font was found, use the last one (usually a generic family)
                if (!span->font->family && value->data.list.count > 0) {
                    CssValue* last = value->data.list.values[value->data.list.count - 1];
                    if (last) {
                        if (last->type == CSS_VALUE_TYPE_STRING) {
                            span->font->family = (char*)last->data.string;
                        } else if (last->type == CSS_VALUE_TYPE_KEYWORD) {
                            const CssEnumInfo* info = css_enum_info(last->data.keyword);
                            span->font->family = info ? (char*)info->name : NULL;
                        } else if (last->type == CSS_VALUE_TYPE_CUSTOM) {
                            span->font->family = (char*)last->data.custom_property.name;
                        }
                        log_debug("[CSS] Using last font in list as fallback: %s", span->font->family);
                    }
                }
            }
            break;
        }

        case CSS_PROPERTY_LINE_HEIGHT: {
            log_debug("[CSS] Processing line-height property");
            if (!span->blk) { span->blk = alloc_block_prop(lycon); }
            span->blk->line_height = value;  // will be resolved later
            break;
        }

        // ===== GROUP 5: Text Properties =====
        case CSS_PROPERTY_TEXT_ALIGN: {
            log_debug("[CSS] Processing text-align property");
            if (!block) break;
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum align_value = value->data.keyword;

                // Handle explicit 'inherit' keyword
                if (align_value == CSS_VALUE_INHERIT) {
                    // Find parent's computed text-align value
                    // Check computed blk->text_align first (handles HTML align attribute and
                    // CSS values set through any path), then fall back to specified_style
                    DomElement* dom_elem = static_cast<DomElement*>(lycon->view);
                    DomElement* parent = dom_elem->parent ? static_cast<DomElement*>(dom_elem->parent) : nullptr;
                    bool resolved = false;

                    while (parent) {
                        // Prefer computed value (covers HTML align attr, CSS, and inherited values)
                        if (parent->blk && parent->blk->text_align != CSS_VALUE__UNDEF &&
                            parent->blk->text_align != CSS_VALUE_INHERIT) {
                            block->blk->text_align = parent->blk->text_align;
                            log_debug("[CSS] Text-align: inherit resolved to parent computed value %d", parent->blk->text_align);
                            resolved = true;
                            break;
                        }
                        // Fall back to specified style
                        if (parent->specified_style) {
                            CssDeclaration* parent_decl = style_tree_get_declaration(
                                parent->specified_style, CSS_PROPERTY_TEXT_ALIGN);
                            if (parent_decl && parent_decl->value &&
                                parent_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                                CssEnum parent_align = parent_decl->value->data.keyword;
                                if (parent_align != CSS_VALUE_INHERIT && parent_align != CSS_VALUE__UNDEF) {
                                    block->blk->text_align = parent_align;
                                    log_debug("[CSS] Text-align: inherit resolved to parent specified value %d", parent_align);
                                    resolved = true;
                                    break;
                                }
                            }
                        }
                        parent = parent->parent ? static_cast<DomElement*>(parent->parent) : nullptr;
                    }

                    // If no parent value found, use default (left)
                    if (!resolved) {
                        block->blk->text_align = CSS_VALUE_LEFT;
                        log_debug("[CSS] Text-align: inherit with no parent, using LEFT");
                    }
                }
                else if (align_value != CSS_VALUE__UNDEF) {
                    block->blk->text_align = align_value;
                    const CssEnumInfo* info = css_enum_info(align_value);
                    log_debug("[CSS] Text-align: %s -> 0x%04X", info ? info->name : "unknown", align_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_TEXT_INDENT: {
            log_debug("[CSS] Processing text-indent property");
            if (!block) break;
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }

            // text-indent can be a length or percentage
            // CSS 2.1: text-indent applies to the first line of a block container
            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                float indent = resolve_length_value(lycon, CSS_PROPERTY_TEXT_INDENT, value);
                block->blk->text_indent = indent;
                block->blk->text_indent_percent = NAN;  // not percentage
                log_debug("[CSS] Text-indent: %.1fpx", indent);
            }
            else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                // Percentage is relative to containing block's width
                // Store percentage for deferred resolution during layout
                float percent = value->data.percentage.value;
                block->blk->text_indent = 0;  // will be computed during layout
                block->blk->text_indent_percent = percent;  // store for layout resolution
                log_debug("[CSS] Text-indent: %.1f%% (deferred resolution)", percent);
            }
            else if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_INHERIT) {
                // Handle inherit - get parent's text-indent
                DomElement* dom_elem = static_cast<DomElement*>(lycon->view);
                DomElement* parent = dom_elem->parent ? static_cast<DomElement*>(dom_elem->parent) : nullptr;
                if (parent && parent->blk) {
                    block->blk->text_indent = parent->blk->text_indent;
                    block->blk->text_indent_percent = parent->blk->text_indent_percent;  // also inherit percentage
                    log_debug("[CSS] Text-indent: inherit -> %.1fpx", block->blk->text_indent);
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
            // CSS 'width: auto' should be represented as -1, not 0
            // This distinguishes from explicit 'width: 0'
            // Same for 'max-content', 'min-content', 'fit-content' - these are intrinsic sizing keywords
            float width;
            if (value && value->type == CSS_VALUE_TYPE_KEYWORD &&
                (value->data.keyword == CSS_VALUE_AUTO ||
                 value->data.keyword == CSS_VALUE_MAX_CONTENT ||
                 value->data.keyword == CSS_VALUE_MIN_CONTENT ||
                 value->data.keyword == CSS_VALUE_FIT_CONTENT)) {
                width = -1;  // auto/intrinsic width - will be calculated during layout
            } else {
                width = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, value);
                width = isnan(width) ? -1 : max(width, 0.0f);  // width cannot be negative
            }
            lycon->block.given_width = width;
            log_debug("width property: %f, type: %d", lycon->block.given_width, value->type);
            // Store the raw width value for box-sizing calculations
            if (block) {
                if (!block->blk) { block->blk = alloc_block_prop(lycon); }
                block->blk->given_width = width;
                block->blk->given_width_type = value->type == CSS_VALUE_TYPE_KEYWORD ? value->data.keyword : CSS_VALUE__UNDEF;
                // Store raw percentage for flex item re-resolution
                if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                    block->blk->given_width_percent = value->data.percentage.value;
                    log_debug("[CSS] Width percentage stored: %.2f%%", value->data.percentage.value);
                } else {
                    block->blk->given_width_percent = NAN;
                }
            }
            log_debug("[CSS] Width: %.2f px", width);
            break;
        }

        case CSS_PROPERTY_HEIGHT: {
            log_debug("[CSS] Processing height property");
            // CSS 'height: auto' should be represented as -1, not 0
            // This distinguishes from explicit 'height: 0'
            // Same for 'max-content', 'min-content', 'fit-content' - these are intrinsic sizing keywords
            float height;
            if (value && value->type == CSS_VALUE_TYPE_KEYWORD &&
                (value->data.keyword == CSS_VALUE_AUTO ||
                 value->data.keyword == CSS_VALUE_MAX_CONTENT ||
                 value->data.keyword == CSS_VALUE_MIN_CONTENT ||
                 value->data.keyword == CSS_VALUE_FIT_CONTENT)) {
                height = -1;  // auto/intrinsic height - will be calculated during layout
            } else {
                height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, value);
                height = isnan(height) ? -1 : max(height, 0.0f);  // height cannot be negative
            }
            lycon->block.given_height = height;
            log_debug("height property: %.1f", lycon->block.given_height);
            // store the raw height value for box-sizing calculations
            if (block) {
                if (!block->blk) { block->blk = alloc_block_prop(lycon); }
                block->blk->given_height = height;
                block->blk->given_height_type = value->type == CSS_VALUE_TYPE_KEYWORD ? value->data.keyword : CSS_VALUE__UNDEF;
                // Store raw percentage for flex item re-resolution
                if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                    block->blk->given_height_percent = value->data.percentage.value;
                    log_debug("[CSS] Height percentage stored: %.2f%%", value->data.percentage.value);
                } else {
                    block->blk->given_height_percent = NAN;
                }
            }
            break;
        }

        case CSS_PROPERTY_MIN_WIDTH: {
            log_debug("[CSS] Processing min-width property");
            if (!block) break;
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
            float resolved = resolve_length_value(lycon, CSS_PROPERTY_MIN_WIDTH, value);
            // If resolve_length_value returns NAN (unresolvable), treat as 0 (no minimum)
            if (std::isnan(resolved)) {
                block->blk->given_min_width = 0;
                log_debug("[CSS] Min-width: unresolvable value (e.g. calc), treating as 0");
            } else {
                block->blk->given_min_width = resolved;
                log_debug("[CSS] Min-width: %.2f px", block->blk->given_min_width);
            }
            break;
        }

        case CSS_PROPERTY_MAX_WIDTH: {
            log_debug("[CSS] Processing max-width property");
            if (!block) break;
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
            // CSS 2.2 Section 10.4: max-width percentage resolves against containing block width
            // If parent has 0 or auto width, percentage max-width should be treated as 'none'
            // because the containing block's width depends on this element's width
            if (value->type == CSS_VALUE_TYPE_PERCENTAGE && lycon->block.parent &&
                lycon->block.parent->content_width <= 0) {
                block->blk->given_max_width = -1;  // -1 means 'none' (unconstrained)
                log_debug("[CSS] Max-width: percentage on 0-width parent, treating as 'none'");
            } else {
                float resolved = resolve_length_value(lycon, CSS_PROPERTY_MAX_WIDTH, value);
                // If resolve_length_value returns NAN (unresolvable), treat as 'none' (-1)
                if (std::isnan(resolved)) {
                    block->blk->given_max_width = -1;
                    log_debug("[CSS] Max-width: unresolvable value (e.g. calc), treating as 'none'");
                } else {
                    block->blk->given_max_width = resolved;
                    log_debug("[CSS] Max-width: %.2f px", block->blk->given_max_width);
                }
            }
            break;
        }

        case CSS_PROPERTY_MIN_HEIGHT: {
            log_debug("[CSS] Processing min-height property");
            if (!block) break;
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
            float resolved = resolve_length_value(lycon, CSS_PROPERTY_MIN_HEIGHT, value);
            // If resolve_length_value returns NAN (unresolvable), treat as 0 (no minimum)
            if (std::isnan(resolved)) {
                block->blk->given_min_height = 0;
                log_debug("[CSS] Min-height: unresolvable value (e.g. calc), treating as 0");
            } else {
                block->blk->given_min_height = resolved;
                log_debug("[CSS] Min-height: %.2f px", block->blk->given_min_height);
            }
            break;
        }

        case CSS_PROPERTY_MAX_HEIGHT: {
            log_debug("[CSS] Processing max-height property");
            if (!block) break;
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
            float resolved = resolve_length_value(lycon, CSS_PROPERTY_MAX_HEIGHT, value);
            // If resolve_length_value returns NAN (unresolvable), treat as 'none' (-1)
            if (std::isnan(resolved)) {
                block->blk->given_max_height = -1;
                log_debug("[CSS] Max-height: unresolvable value (e.g. calc), treating as 'none'");
            } else {
                block->blk->given_max_height = resolved;
                log_debug("[CSS] Max-height: %.2f px", block->blk->given_max_height);
            }
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
                span->bound->margin.top = resolve_margin_with_inherit(lycon, CSS_PROPERTY_MARGIN_TOP, value);
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
                span->bound->margin.right = resolve_margin_with_inherit(lycon, CSS_PROPERTY_MARGIN_RIGHT, value);
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
                span->bound->margin.bottom = resolve_margin_with_inherit(lycon, CSS_PROPERTY_MARGIN_BOTTOM, value);
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
                span->bound->margin.left = resolve_margin_with_inherit(lycon, CSS_PROPERTY_MARGIN_LEFT, value);
                span->bound->margin.left_specificity = specificity;
                span->bound->margin.left_type = value->type == CSS_VALUE_TYPE_KEYWORD ? value->data.keyword : CSS_VALUE__UNDEF;
            }
            break;
        }

        // margin-block: sets both margin-top and margin-bottom (logical property for block axis)
        case CSS_PROPERTY_MARGIN_BLOCK: {
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            float margin_value = resolve_margin_with_inherit(lycon, CSS_PROPERTY_MARGIN_BLOCK, value);
            if (specificity >= span->bound->margin.top_specificity) {
                span->bound->margin.top = margin_value;
                span->bound->margin.top_specificity = specificity;
                span->bound->margin.top_type = value->type == CSS_VALUE_TYPE_KEYWORD ? value->data.keyword : CSS_VALUE__UNDEF;
            }
            if (specificity >= span->bound->margin.bottom_specificity) {
                span->bound->margin.bottom = margin_value;
                span->bound->margin.bottom_specificity = specificity;
                span->bound->margin.bottom_type = value->type == CSS_VALUE_TYPE_KEYWORD ? value->data.keyword : CSS_VALUE__UNDEF;
            }
            break;
        }

        // margin-inline: sets both margin-left and margin-right (logical property for inline axis)
        // NOTE: For LTR writing mode, inline-start=left, inline-end=right
        case CSS_PROPERTY_MARGIN_INLINE: {
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            float margin_value = resolve_margin_with_inherit(lycon, CSS_PROPERTY_MARGIN_INLINE, value);
            if (specificity >= span->bound->margin.left_specificity) {
                span->bound->margin.left = margin_value;
                span->bound->margin.left_specificity = specificity;
                span->bound->margin.left_type = value->type == CSS_VALUE_TYPE_KEYWORD ? value->data.keyword : CSS_VALUE__UNDEF;
            }
            if (specificity >= span->bound->margin.right_specificity) {
                span->bound->margin.right = margin_value;
                span->bound->margin.right_specificity = specificity;
                span->bound->margin.right_type = value->type == CSS_VALUE_TYPE_KEYWORD ? value->data.keyword : CSS_VALUE__UNDEF;
            }
            break;
        }
        case CSS_PROPERTY_MARGIN_INLINE_START: {
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            // For LTR, inline-start = left
            if (specificity >= span->bound->margin.left_specificity) {
                span->bound->margin.left = resolve_margin_with_inherit(lycon, CSS_PROPERTY_MARGIN_INLINE_START, value);
                span->bound->margin.left_specificity = specificity;
                span->bound->margin.left_type = value->type == CSS_VALUE_TYPE_KEYWORD ? value->data.keyword : CSS_VALUE__UNDEF;
            }
            break;
        }
        case CSS_PROPERTY_MARGIN_INLINE_END: {
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            // For LTR, inline-end = right
            if (specificity >= span->bound->margin.right_specificity) {
                span->bound->margin.right = resolve_margin_with_inherit(lycon, CSS_PROPERTY_MARGIN_INLINE_END, value);
                span->bound->margin.right_specificity = specificity;
                span->bound->margin.right_type = value->type == CSS_VALUE_TYPE_KEYWORD ? value->data.keyword : CSS_VALUE__UNDEF;
            }
            break;
        }

        case CSS_PROPERTY_PADDING_TOP: {
            log_debug("[CSS] Processing padding-top property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (specificity >= span->bound->padding.top_specificity) {
                span->bound->padding.top = resolve_padding_with_inherit(lycon, CSS_PROPERTY_PADDING_TOP, value);
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
                span->bound->padding.right = resolve_padding_with_inherit(lycon, CSS_PROPERTY_PADDING_RIGHT, value);
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
                span->bound->padding.bottom = resolve_padding_with_inherit(lycon, CSS_PROPERTY_PADDING_BOTTOM, value);
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
                span->bound->padding.left = resolve_padding_with_inherit(lycon, CSS_PROPERTY_PADDING_LEFT, value);
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
            span->bound->background->color = resolve_color_value(lycon, value);
            break;
        }

        case CSS_PROPERTY_BACKGROUND_IMAGE: {
            ViewSpan* span = (ViewSpan*)lycon->view;
            const char* elem_name = span && span->tag_name ? span->tag_name : "unknown";
            log_debug("[CSS] Processing background-image property on <%s> (value type=%d)", elem_name, value->type);
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->background) {
                span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
            }

            if (value->type == CSS_VALUE_TYPE_FUNCTION) {
                // url() is parsed as a function
                CssFunction* func = value->data.function;
                if (func && func->name && strcmp(func->name, "url") == 0) {
                    // Get the first argument of url() function
                    if (func->args && func->arg_count > 0) {
                        CssValue* arg = func->args[0];
                        const char* url = (arg->type == CSS_VALUE_TYPE_STRING) ? arg->data.string :
                                         (arg->type == CSS_VALUE_TYPE_URL) ? arg->data.url : nullptr;
                        if (url) {
                            // Allocate and copy the URL string
                            size_t url_len = strlen(url);
                            char* image_path = (char*)alloc_prop(lycon, url_len + 1);
                            if (image_path) {
                                str_copy(image_path, url_len + 1, url, url_len);
                                span->bound->background->image = image_path;
                                log_debug("[CSS] background-image stored: '%s'", image_path);
                            }
                        }
                    }
                }
            } else if (value->type == CSS_VALUE_TYPE_URL || value->type == CSS_VALUE_TYPE_STRING) {
                // Direct URL/string value (non-function form)
                const char* url = (value->type == CSS_VALUE_TYPE_URL) ? value->data.url : value->data.string;
                if (url) {
                    size_t url_len = strlen(url);
                    char* image_path = (char*)alloc_prop(lycon, url_len + 1);
                    if (image_path) {
                        str_copy(image_path, url_len + 1, url, url_len);
                        span->bound->background->image = image_path;
                        log_debug("[CSS] background-image stored: '%s'", image_path);
                    }
                }
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                span->bound->background->image = nullptr;
                log_debug("[CSS] background-image: none");
            }
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
                float pos_x = resolve_length_value(lycon, prop_id, value);
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
                float pos_y = resolve_length_value(lycon, prop_id, value);
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

        case CSS_PROPERTY_BOX_SHADOW: {
            log_debug("[CSS] Processing box-shadow property (value type=%d)", value->type);
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }

            // Handle 'none' keyword
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                span->bound->box_shadow = nullptr;
                log_debug("[CSS] box-shadow: none");
                break;
            }

            // Box-shadow can be a list of shadows (comma-separated)
            // Each shadow: [inset] <offset-x> <offset-y> [blur-radius] [spread-radius] [color]
            BoxShadow* shadow_list_head = nullptr;
            BoxShadow* shadow_list_tail = nullptr;

            // Helper lambda to parse a single shadow from a value list
            auto parse_single_shadow = [&](const CssValue* shadow_value) -> BoxShadow* {
                BoxShadow* shadow = (BoxShadow*)alloc_prop(lycon, sizeof(BoxShadow));
                memset(shadow, 0, sizeof(BoxShadow));
                // Default color: black with full opacity
                shadow->color.r = 0;
                shadow->color.g = 0;
                shadow->color.b = 0;
                shadow->color.a = 255;

                if (shadow_value->type == CSS_VALUE_TYPE_LIST) {
                    const CssValue* list = shadow_value;
                    int length_count = 0;
                    for (size_t i = 0; i < list->data.list.count; i++) {
                        const CssValue* v = list->data.list.values[i];
                        if (!v) continue;

                        if (v->type == CSS_VALUE_TYPE_KEYWORD) {
                            if (v->data.keyword == CSS_VALUE_INSET) {
                                shadow->inset = true;
                            } else {
                                // Could be a color keyword
                                shadow->color = color_name_to_rgb(v->data.keyword);
                            }
                        } else if (v->type == CSS_VALUE_TYPE_LENGTH || v->type == CSS_VALUE_TYPE_NUMBER) {
                            float val = (v->type == CSS_VALUE_TYPE_LENGTH)
                                ? resolve_length_value(lycon, prop_id, v)
                                : v->data.number.value;
                            switch (length_count) {
                                case 0: shadow->offset_x = val; break;
                                case 1: shadow->offset_y = val; break;
                                case 2: shadow->blur_radius = val; break;
                                case 3: shadow->spread_radius = val; break;
                            }
                            length_count++;
                        } else if (v->type == CSS_VALUE_TYPE_COLOR || v->type == CSS_VALUE_TYPE_FUNCTION) {
                            shadow->color = resolve_color_value(lycon, v);
                        }
                    }
                } else if (shadow_value->type == CSS_VALUE_TYPE_LENGTH || shadow_value->type == CSS_VALUE_TYPE_NUMBER) {
                    // Single length value - just offset-x (unlikely but valid syntax)
                    shadow->offset_x = (shadow_value->type == CSS_VALUE_TYPE_LENGTH)
                        ? resolve_length_value(lycon, prop_id, shadow_value)
                        : shadow_value->data.number.value;
                }
                return shadow;
            };

            // Check if this is a list of shadows (comma-separated)
            if (value->type == CSS_VALUE_TYPE_LIST) {
                // Could be a single shadow's components OR multiple shadows
                // Look for nested lists (multiple shadows) vs flat list (single shadow)
                const CssValue* list = value;
                bool is_multi_shadow = false;

                // Check if any child is itself a list (indicates multiple shadows)
                for (size_t i = 0; i < list->data.list.count && !is_multi_shadow; i++) {
                    if (list->data.list.values[i] &&
                        list->data.list.values[i]->type == CSS_VALUE_TYPE_LIST) {
                        is_multi_shadow = true;
                    }
                }

                if (is_multi_shadow) {
                    // Multiple shadows - each child is a shadow
                    for (size_t i = 0; i < list->data.list.count; i++) {
                        const CssValue* shadow_val = list->data.list.values[i];
                        if (!shadow_val) continue;
                        BoxShadow* shadow = parse_single_shadow(shadow_val);
                        if (shadow) {
                            if (!shadow_list_head) {
                                shadow_list_head = shadow;
                                shadow_list_tail = shadow;
                            } else {
                                shadow_list_tail->next = shadow;
                                shadow_list_tail = shadow;
                            }
                        }
                    }
                } else {
                    // Single shadow - parse the flat list directly
                    BoxShadow* shadow = parse_single_shadow(value);
                    if (shadow) {
                        shadow_list_head = shadow;
                    }
                }
            }

            span->bound->box_shadow = shadow_list_head;
            log_debug("[CSS] box-shadow parsed: %s", shadow_list_head ? "shadow(s) set" : "none");
            break;
        }

        // ============================================================================
        // CSS Transforms
        // ============================================================================
        case CSS_PROPERTY_TRANSFORM: {
            log_debug("[CSS] Processing transform property (value type=%d)", value->type);

            // Handle 'none' keyword
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                span->transform = nullptr;
                log_debug("[CSS] transform: none");
                break;
            }

            // Allocate transform property
            if (!span->transform) {
                span->transform = (TransformProp*)alloc_prop(lycon, sizeof(TransformProp));
                memset(span->transform, 0, sizeof(TransformProp));
                // Default origin: center (50% 50%)
                span->transform->origin_x = 50.0f;
                span->transform->origin_y = 50.0f;
                span->transform->origin_x_percent = true;
                span->transform->origin_y_percent = true;
            }

            TransformFunction* func_list_head = nullptr;
            TransformFunction* func_list_tail = nullptr;

            // Helper lambda to parse a single transform function
            auto parse_transform_function = [&](const CssValue* func_value) -> TransformFunction* {
                if (func_value->type != CSS_VALUE_TYPE_FUNCTION) return nullptr;

                const CssFunction* func = func_value->data.function;
                if (!func || !func->name) return nullptr;

                TransformFunction* tf = (TransformFunction*)alloc_prop(lycon, sizeof(TransformFunction));
                memset(tf, 0, sizeof(TransformFunction));
                // Initialize percentage fields to NaN (not percentage)
                tf->translate_x_percent = NAN;
                tf->translate_y_percent = NAN;

                // Parse function name and arguments
                if (str_ieq_const(func->name, strlen(func->name), "translate")) {
                    tf->type = TRANSFORM_TRANSLATE;
                    if (func->arg_count >= 1 && func->args[0]) {
                        // Check if X is a percentage (needs deferred resolution)
                        if (func->args[0]->type == CSS_VALUE_TYPE_PERCENTAGE) {
                            tf->translate_x_percent = func->args[0]->data.percentage.value;
                            tf->params.translate.x = 0;  // Will be resolved later
                            log_debug("[CSS] transform: translate X is percentage: %g%%", tf->translate_x_percent);
                        } else {
                            tf->params.translate.x = resolve_length_value(lycon, prop_id, func->args[0]);
                        }
                    }
                    if (func->arg_count >= 2 && func->args[1]) {
                        // Check if Y is a percentage
                        if (func->args[1]->type == CSS_VALUE_TYPE_PERCENTAGE) {
                            tf->translate_y_percent = func->args[1]->data.percentage.value;
                            tf->params.translate.y = 0;  // Will be resolved later
                            log_debug("[CSS] transform: translate Y is percentage: %g%%", tf->translate_y_percent);
                        } else {
                            tf->params.translate.y = resolve_length_value(lycon, prop_id, func->args[1]);
                        }
                    }
                    log_debug("[CSS] transform: translate(%g, %g)", tf->params.translate.x, tf->params.translate.y);
                }
                else if (str_ieq_const(func->name, strlen(func->name), "translateX")) {
                    tf->type = TRANSFORM_TRANSLATEX;
                    if (func->arg_count >= 1 && func->args[0]) {
                        if (func->args[0]->type == CSS_VALUE_TYPE_PERCENTAGE) {
                            tf->translate_x_percent = func->args[0]->data.percentage.value;
                            tf->params.translate.x = 0;
                        } else {
                            tf->params.translate.x = resolve_length_value(lycon, prop_id, func->args[0]);
                        }
                    }
                    log_debug("[CSS] transform: translateX(%g)", tf->params.translate.x);
                }
                else if (str_ieq_const(func->name, strlen(func->name), "translateY")) {
                    tf->type = TRANSFORM_TRANSLATEY;
                    if (func->arg_count >= 1 && func->args[0]) {
                        if (func->args[0]->type == CSS_VALUE_TYPE_PERCENTAGE) {
                            tf->translate_y_percent = func->args[0]->data.percentage.value;
                            tf->params.translate.y = 0;
                        } else {
                            tf->params.translate.y = resolve_length_value(lycon, prop_id, func->args[0]);
                        }
                    }
                    log_debug("[CSS] transform: translateY(%g)", tf->params.translate.y);
                }
                else if (str_ieq_const(func->name, strlen(func->name), "scale")) {
                    tf->type = TRANSFORM_SCALE;
                    tf->params.scale.x = 1.0f;
                    tf->params.scale.y = 1.0f;
                    if (func->arg_count >= 1 && func->args[0]) {
                        tf->params.scale.x = func->args[0]->data.number.value;
                        tf->params.scale.y = tf->params.scale.x; // default to uniform scale
                    }
                    if (func->arg_count >= 2 && func->args[1]) {
                        tf->params.scale.y = func->args[1]->data.number.value;
                    }
                    log_debug("[CSS] transform: scale(%g, %g)", tf->params.scale.x, tf->params.scale.y);
                }
                else if (str_ieq_const(func->name, strlen(func->name), "scaleX")) {
                    tf->type = TRANSFORM_SCALEX;
                    tf->params.scale.x = 1.0f;
                    tf->params.scale.y = 1.0f;
                    if (func->arg_count >= 1 && func->args[0]) {
                        tf->params.scale.x = func->args[0]->data.number.value;
                    }
                    log_debug("[CSS] transform: scaleX(%g)", tf->params.scale.x);
                }
                else if (str_ieq_const(func->name, strlen(func->name), "scaleY")) {
                    tf->type = TRANSFORM_SCALEY;
                    tf->params.scale.x = 1.0f;
                    tf->params.scale.y = 1.0f;
                    if (func->arg_count >= 1 && func->args[0]) {
                        tf->params.scale.y = func->args[0]->data.number.value;
                    }
                    log_debug("[CSS] transform: scaleY(%g)", tf->params.scale.y);
                }
                else if (str_ieq_const(func->name, strlen(func->name), "rotate")) {
                    tf->type = TRANSFORM_ROTATE;
                    if (func->arg_count >= 1 && func->args[0]) {
                        const CssValue* angle_val = func->args[0];
                        if (angle_val->type == CSS_VALUE_TYPE_LENGTH) {
                            // Angle values come as LENGTH with angle units
                            float angle = angle_val->data.length.value;
                            CssUnit unit = angle_val->data.length.unit;
                            // Convert to radians
                            if (unit == CSS_UNIT_DEG) {
                                tf->params.angle = angle * M_PI / 180.0f;
                            } else if (unit == CSS_UNIT_RAD) {
                                tf->params.angle = angle;
                            } else if (unit == CSS_UNIT_GRAD) {
                                tf->params.angle = angle * M_PI / 200.0f;
                            } else if (unit == CSS_UNIT_TURN) {
                                tf->params.angle = angle * 2.0f * M_PI;
                            } else {
                                tf->params.angle = angle * M_PI / 180.0f; // default to deg
                            }
                        } else if (angle_val->type == CSS_VALUE_TYPE_NUMBER) {
                            // Unitless number = radians in some browsers, deg in CSS spec
                            tf->params.angle = angle_val->data.number.value * M_PI / 180.0f;
                        }
                    }
                    log_debug("[CSS] transform: rotate(%g rad)", tf->params.angle);
                }
                else if (str_ieq_const(func->name, strlen(func->name), "skew")) {
                    tf->type = TRANSFORM_SKEW;
                    // Parse skew angles
                    if (func->arg_count >= 1 && func->args[0]) {
                        const CssValue* angle_val = func->args[0];
                        float angle = (angle_val->type == CSS_VALUE_TYPE_LENGTH)
                            ? angle_val->data.length.value : angle_val->data.number.value;
                        if (angle_val->type == CSS_VALUE_TYPE_LENGTH && angle_val->data.length.unit == CSS_UNIT_RAD) {
                            tf->params.skew.x = angle;
                        } else {
                            tf->params.skew.x = angle * M_PI / 180.0f;
                        }
                    }
                    if (func->arg_count >= 2 && func->args[1]) {
                        const CssValue* angle_val = func->args[1];
                        float angle = (angle_val->type == CSS_VALUE_TYPE_LENGTH)
                            ? angle_val->data.length.value : angle_val->data.number.value;
                        if (angle_val->type == CSS_VALUE_TYPE_LENGTH && angle_val->data.length.unit == CSS_UNIT_RAD) {
                            tf->params.skew.y = angle;
                        } else {
                            tf->params.skew.y = angle * M_PI / 180.0f;
                        }
                    }
                    log_debug("[CSS] transform: skew(%g, %g rad)", tf->params.skew.x, tf->params.skew.y);
                }
                else if (str_ieq_const(func->name, strlen(func->name), "skewX")) {
                    tf->type = TRANSFORM_SKEWX;
                    if (func->arg_count >= 1 && func->args[0]) {
                        const CssValue* angle_val = func->args[0];
                        float angle = (angle_val->type == CSS_VALUE_TYPE_LENGTH)
                            ? angle_val->data.length.value : angle_val->data.number.value;
                        if (angle_val->type == CSS_VALUE_TYPE_LENGTH && angle_val->data.length.unit == CSS_UNIT_RAD) {
                            tf->params.angle = angle;
                        } else {
                            tf->params.angle = angle * M_PI / 180.0f;
                        }
                    }
                    log_debug("[CSS] transform: skewX(%g rad)", tf->params.angle);
                }
                else if (str_ieq_const(func->name, strlen(func->name), "skewY")) {
                    tf->type = TRANSFORM_SKEWY;
                    if (func->arg_count >= 1 && func->args[0]) {
                        const CssValue* angle_val = func->args[0];
                        float angle = (angle_val->type == CSS_VALUE_TYPE_LENGTH)
                            ? angle_val->data.length.value : angle_val->data.number.value;
                        if (angle_val->type == CSS_VALUE_TYPE_LENGTH && angle_val->data.length.unit == CSS_UNIT_RAD) {
                            tf->params.angle = angle;
                        } else {
                            tf->params.angle = angle * M_PI / 180.0f;
                        }
                    }
                    log_debug("[CSS] transform: skewY(%g rad)", tf->params.angle);
                }
                else if (str_ieq_const(func->name, strlen(func->name), "matrix")) {
                    tf->type = TRANSFORM_MATRIX;
                    // matrix(a, b, c, d, e, f) = [a c e; b d f; 0 0 1]
                    // Default to identity
                    tf->params.matrix.a = 1; tf->params.matrix.b = 0;
                    tf->params.matrix.c = 0; tf->params.matrix.d = 1;
                    tf->params.matrix.e = 0; tf->params.matrix.f = 0;
                    if (func->arg_count >= 6) {
                        tf->params.matrix.a = func->args[0]->data.number.value;
                        tf->params.matrix.b = func->args[1]->data.number.value;
                        tf->params.matrix.c = func->args[2]->data.number.value;
                        tf->params.matrix.d = func->args[3]->data.number.value;
                        tf->params.matrix.e = func->args[4]->data.number.value;
                        tf->params.matrix.f = func->args[5]->data.number.value;
                    }
                    log_debug("[CSS] transform: matrix(%g,%g,%g,%g,%g,%g)",
                        tf->params.matrix.a, tf->params.matrix.b, tf->params.matrix.c,
                        tf->params.matrix.d, tf->params.matrix.e, tf->params.matrix.f);
                }
                // 3D transforms
                else if (str_ieq_const(func->name, strlen(func->name), "translate3d")) {
                    tf->type = TRANSFORM_TRANSLATE3D;
                    if (func->arg_count >= 1 && func->args[0]) {
                        tf->params.translate3d.x = resolve_length_value(lycon, prop_id, func->args[0]);
                    }
                    if (func->arg_count >= 2 && func->args[1]) {
                        tf->params.translate3d.y = resolve_length_value(lycon, prop_id, func->args[1]);
                    }
                    if (func->arg_count >= 3 && func->args[2]) {
                        tf->params.translate3d.z = resolve_length_value(lycon, prop_id, func->args[2]);
                    }
                    log_debug("[CSS] transform: translate3d(%g, %g, %g)",
                        tf->params.translate3d.x, tf->params.translate3d.y, tf->params.translate3d.z);
                }
                else if (str_ieq_const(func->name, strlen(func->name), "translateZ")) {
                    tf->type = TRANSFORM_TRANSLATEZ;
                    if (func->arg_count >= 1 && func->args[0]) {
                        tf->params.translate3d.z = resolve_length_value(lycon, prop_id, func->args[0]);
                    }
                    log_debug("[CSS] transform: translateZ(%g)", tf->params.translate3d.z);
                }
                else if (str_ieq_const(func->name, strlen(func->name), "rotateX")) {
                    tf->type = TRANSFORM_ROTATEX;
                    if (func->arg_count >= 1 && func->args[0]) {
                        const CssValue* angle_val = func->args[0];
                        float angle = (angle_val->type == CSS_VALUE_TYPE_LENGTH)
                            ? angle_val->data.length.value : angle_val->data.number.value;
                        if (angle_val->type == CSS_VALUE_TYPE_LENGTH && angle_val->data.length.unit == CSS_UNIT_RAD) {
                            tf->params.angle = angle;
                        } else {
                            tf->params.angle = angle * M_PI / 180.0f;
                        }
                    }
                    log_debug("[CSS] transform: rotateX(%g rad)", tf->params.angle);
                }
                else if (str_ieq_const(func->name, strlen(func->name), "rotateY")) {
                    tf->type = TRANSFORM_ROTATEY;
                    if (func->arg_count >= 1 && func->args[0]) {
                        const CssValue* angle_val = func->args[0];
                        float angle = (angle_val->type == CSS_VALUE_TYPE_LENGTH)
                            ? angle_val->data.length.value : angle_val->data.number.value;
                        if (angle_val->type == CSS_VALUE_TYPE_LENGTH && angle_val->data.length.unit == CSS_UNIT_RAD) {
                            tf->params.angle = angle;
                        } else {
                            tf->params.angle = angle * M_PI / 180.0f;
                        }
                    }
                    log_debug("[CSS] transform: rotateY(%g rad)", tf->params.angle);
                }
                else if (str_ieq_const(func->name, strlen(func->name), "rotateZ")) {
                    tf->type = TRANSFORM_ROTATEZ;
                    if (func->arg_count >= 1 && func->args[0]) {
                        const CssValue* angle_val = func->args[0];
                        float angle = (angle_val->type == CSS_VALUE_TYPE_LENGTH)
                            ? angle_val->data.length.value : angle_val->data.number.value;
                        if (angle_val->type == CSS_VALUE_TYPE_LENGTH && angle_val->data.length.unit == CSS_UNIT_RAD) {
                            tf->params.angle = angle;
                        } else {
                            tf->params.angle = angle * M_PI / 180.0f;
                        }
                    }
                    log_debug("[CSS] transform: rotateZ(%g rad)", tf->params.angle);
                }
                else if (str_ieq_const(func->name, strlen(func->name), "perspective")) {
                    tf->type = TRANSFORM_PERSPECTIVE;
                    if (func->arg_count >= 1 && func->args[0]) {
                        tf->params.perspective = resolve_length_value(lycon, prop_id, func->args[0]);
                    }
                    log_debug("[CSS] transform: perspective(%g)", tf->params.perspective);
                }
                else {
                    log_debug("[CSS] Unknown transform function: %s", func->name);
                    return nullptr;
                }

                return tf;
            };

            // Parse transform functions from value
            if (value->type == CSS_VALUE_TYPE_FUNCTION) {
                // Single transform function
                TransformFunction* tf = parse_transform_function(value);
                if (tf) {
                    func_list_head = tf;
                }
            } else if (value->type == CSS_VALUE_TYPE_LIST) {
                // Multiple transform functions
                const CssValue* list = value;
                for (size_t i = 0; i < list->data.list.count; i++) {
                    const CssValue* item = list->data.list.values[i];
                    if (!item) continue;

                    TransformFunction* tf = parse_transform_function(item);
                    if (tf) {
                        if (!func_list_head) {
                            func_list_head = tf;
                            func_list_tail = tf;
                        } else {
                            func_list_tail->next = tf;
                            func_list_tail = tf;
                        }
                    }
                }
            }

            span->transform->functions = func_list_head;
            log_debug("[CSS] transform parsed: %s", func_list_head ? "function(s) set" : "none");
            break;
        }

        case CSS_PROPERTY_TRANSFORM_ORIGIN: {
            log_debug("[CSS] Processing transform-origin property (value type=%d)", value->type);

            // Allocate transform property if needed
            if (!span->transform) {
                span->transform = (TransformProp*)alloc_prop(lycon, sizeof(TransformProp));
                memset(span->transform, 0, sizeof(TransformProp));
                span->transform->origin_x = 50.0f;
                span->transform->origin_y = 50.0f;
                span->transform->origin_x_percent = true;
                span->transform->origin_y_percent = true;
            }

            // Parse transform-origin: can be keywords (left, center, right, top, bottom)
            // or length/percentage values
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum kw = value->data.keyword;
                if (kw == CSS_VALUE_LEFT) {
                    span->transform->origin_x = 0;
                    span->transform->origin_x_percent = true;
                } else if (kw == CSS_VALUE_CENTER) {
                    span->transform->origin_x = 50.0f;
                    span->transform->origin_x_percent = true;
                } else if (kw == CSS_VALUE_RIGHT) {
                    span->transform->origin_x = 100.0f;
                    span->transform->origin_x_percent = true;
                } else if (kw == CSS_VALUE_TOP) {
                    span->transform->origin_y = 0;
                    span->transform->origin_y_percent = true;
                } else if (kw == CSS_VALUE_BOTTOM) {
                    span->transform->origin_y = 100.0f;
                    span->transform->origin_y_percent = true;
                }
            } else if (value->type == CSS_VALUE_TYPE_LIST) {
                const CssValue* list = value;
                // First value is X, second is Y (optional third is Z)
                for (size_t i = 0; i < list->data.list.count && i < 3; i++) {
                    const CssValue* v = list->data.list.values[i];
                    if (!v) continue;

                    if (v->type == CSS_VALUE_TYPE_PERCENTAGE) {
                        float pct = (float)v->data.percentage.value;
                        if (i == 0) {
                            span->transform->origin_x = pct;
                            span->transform->origin_x_percent = true;
                        } else if (i == 1) {
                            span->transform->origin_y = pct;
                            span->transform->origin_y_percent = true;
                        } else {
                            // Z cannot be percentage
                        }
                    } else if (v->type == CSS_VALUE_TYPE_LENGTH) {
                        float len = resolve_length_value(lycon, prop_id, v);
                        if (i == 0) {
                            span->transform->origin_x = len;
                            span->transform->origin_x_percent = false;
                        } else if (i == 1) {
                            span->transform->origin_y = len;
                            span->transform->origin_y_percent = false;
                        } else {
                            span->transform->origin_z = len;
                        }
                    } else if (v->type == CSS_VALUE_TYPE_KEYWORD) {
                        CssEnum kw = v->data.keyword;
                        // Keywords can be in any order for X/Y
                        if (kw == CSS_VALUE_LEFT || kw == CSS_VALUE_RIGHT) {
                            span->transform->origin_x = (kw == CSS_VALUE_LEFT) ? 0 : 100.0f;
                            span->transform->origin_x_percent = true;
                        } else if (kw == CSS_VALUE_TOP || kw == CSS_VALUE_BOTTOM) {
                            span->transform->origin_y = (kw == CSS_VALUE_TOP) ? 0 : 100.0f;
                            span->transform->origin_y_percent = true;
                        } else if (kw == CSS_VALUE_CENTER) {
                            if (i == 0) {
                                span->transform->origin_x = 50.0f;
                                span->transform->origin_x_percent = true;
                            } else {
                                span->transform->origin_y = 50.0f;
                                span->transform->origin_y_percent = true;
                            }
                        }
                    }
                }
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                span->transform->origin_x = (float)value->data.percentage.value;
                span->transform->origin_x_percent = true;
            } else if (value->type == CSS_VALUE_TYPE_LENGTH) {
                span->transform->origin_x = resolve_length_value(lycon, prop_id, value);
                span->transform->origin_x_percent = false;
            }

            log_debug("[CSS] transform-origin: (%g%s, %g%s)",
                span->transform->origin_x, span->transform->origin_x_percent ? "%" : "px",
                span->transform->origin_y, span->transform->origin_y_percent ? "%" : "px");
            break;
        }

        case CSS_PROPERTY_FILTER: {
            log_debug("[CSS] Processing filter property");

            // Helper lambda to parse a single filter function
            auto parse_filter_func = [&](CssFunction* func) -> FilterFunction* {
                if (!func || !func->name || func->arg_count == 0) return nullptr;

                FilterFunction* filter = (FilterFunction*)alloc_prop(lycon, sizeof(FilterFunction));
                filter->next = nullptr;

                const char* name = func->name;
                CssValue* arg = func->args[0];  // First argument

                if (strcmp(name, "blur") == 0) {
                    filter->type = FILTER_BLUR;
                    if (arg && arg->type == CSS_VALUE_TYPE_LENGTH) {
                        filter->params.blur_radius = resolve_length_value(lycon, prop_id, arg);
                    } else {
                        filter->params.blur_radius = 0;
                    }
                    log_debug("[CSS] filter: blur(%.2fpx)", filter->params.blur_radius);
                }
                else if (strcmp(name, "brightness") == 0) {
                    filter->type = FILTER_BRIGHTNESS;
                    if (arg) {
                        if (arg->type == CSS_VALUE_TYPE_PERCENTAGE) {
                            filter->params.amount = (float)arg->data.percentage.value / 100.0f;
                        } else if (arg->type == CSS_VALUE_TYPE_NUMBER) {
                            filter->params.amount = (float)arg->data.number.value;
                        } else {
                            filter->params.amount = 1.0f;
                        }
                    } else {
                        filter->params.amount = 1.0f;
                    }
                    log_debug("[CSS] filter: brightness(%.2f)", filter->params.amount);
                }
                else if (strcmp(name, "contrast") == 0) {
                    filter->type = FILTER_CONTRAST;
                    if (arg) {
                        if (arg->type == CSS_VALUE_TYPE_PERCENTAGE) {
                            filter->params.amount = (float)arg->data.percentage.value / 100.0f;
                        } else if (arg->type == CSS_VALUE_TYPE_NUMBER) {
                            filter->params.amount = (float)arg->data.number.value;
                        } else {
                            filter->params.amount = 1.0f;
                        }
                    } else {
                        filter->params.amount = 1.0f;
                    }
                    log_debug("[CSS] filter: contrast(%.2f)", filter->params.amount);
                }
                else if (strcmp(name, "grayscale") == 0) {
                    filter->type = FILTER_GRAYSCALE;
                    if (arg) {
                        if (arg->type == CSS_VALUE_TYPE_PERCENTAGE) {
                            filter->params.amount = (float)arg->data.percentage.value / 100.0f;
                        } else if (arg->type == CSS_VALUE_TYPE_NUMBER) {
                            filter->params.amount = (float)arg->data.number.value;
                        } else {
                            filter->params.amount = 1.0f;
                        }
                    } else {
                        filter->params.amount = 1.0f;
                    }
                    // Clamp to [0, 1]
                    if (filter->params.amount > 1.0f) filter->params.amount = 1.0f;
                    if (filter->params.amount < 0.0f) filter->params.amount = 0.0f;
                    log_debug("[CSS] filter: grayscale(%.2f)", filter->params.amount);
                }
                else if (strcmp(name, "invert") == 0) {
                    filter->type = FILTER_INVERT;
                    if (arg) {
                        if (arg->type == CSS_VALUE_TYPE_PERCENTAGE) {
                            filter->params.amount = (float)arg->data.percentage.value / 100.0f;
                        } else if (arg->type == CSS_VALUE_TYPE_NUMBER) {
                            filter->params.amount = (float)arg->data.number.value;
                        } else {
                            filter->params.amount = 1.0f;
                        }
                    } else {
                        filter->params.amount = 1.0f;
                    }
                    // Clamp to [0, 1]
                    if (filter->params.amount > 1.0f) filter->params.amount = 1.0f;
                    if (filter->params.amount < 0.0f) filter->params.amount = 0.0f;
                    log_debug("[CSS] filter: invert(%.2f)", filter->params.amount);
                }
                else if (strcmp(name, "opacity") == 0) {
                    filter->type = FILTER_OPACITY;
                    if (arg) {
                        if (arg->type == CSS_VALUE_TYPE_PERCENTAGE) {
                            filter->params.amount = (float)arg->data.percentage.value / 100.0f;
                        } else if (arg->type == CSS_VALUE_TYPE_NUMBER) {
                            filter->params.amount = (float)arg->data.number.value;
                        } else {
                            filter->params.amount = 1.0f;
                        }
                    } else {
                        filter->params.amount = 1.0f;
                    }
                    // Clamp to [0, 1]
                    if (filter->params.amount > 1.0f) filter->params.amount = 1.0f;
                    if (filter->params.amount < 0.0f) filter->params.amount = 0.0f;
                    log_debug("[CSS] filter: opacity(%.2f)", filter->params.amount);
                }
                else if (strcmp(name, "saturate") == 0) {
                    filter->type = FILTER_SATURATE;
                    if (arg) {
                        if (arg->type == CSS_VALUE_TYPE_PERCENTAGE) {
                            filter->params.amount = (float)arg->data.percentage.value / 100.0f;
                        } else if (arg->type == CSS_VALUE_TYPE_NUMBER) {
                            filter->params.amount = (float)arg->data.number.value;
                        } else {
                            filter->params.amount = 1.0f;
                        }
                    } else {
                        filter->params.amount = 1.0f;
                    }
                    log_debug("[CSS] filter: saturate(%.2f)", filter->params.amount);
                }
                else if (strcmp(name, "sepia") == 0) {
                    filter->type = FILTER_SEPIA;
                    if (arg) {
                        if (arg->type == CSS_VALUE_TYPE_PERCENTAGE) {
                            filter->params.amount = (float)arg->data.percentage.value / 100.0f;
                        } else if (arg->type == CSS_VALUE_TYPE_NUMBER) {
                            filter->params.amount = (float)arg->data.number.value;
                        } else {
                            filter->params.amount = 1.0f;
                        }
                    } else {
                        filter->params.amount = 1.0f;
                    }
                    // Clamp to [0, 1]
                    if (filter->params.amount > 1.0f) filter->params.amount = 1.0f;
                    if (filter->params.amount < 0.0f) filter->params.amount = 0.0f;
                    log_debug("[CSS] filter: sepia(%.2f)", filter->params.amount);
                }
                else if (strcmp(name, "hue-rotate") == 0) {
                    filter->type = FILTER_HUE_ROTATE;
                    if (arg && (arg->type == CSS_VALUE_TYPE_ANGLE || arg->type == CSS_VALUE_TYPE_LENGTH)) {
                        // Angles are stored in length.value (degrees)
                        float degrees = (float)arg->data.length.value;
                        filter->params.angle = degrees * ((float)M_PI / 180.0f);
                    } else if (arg && arg->type == CSS_VALUE_TYPE_NUMBER) {
                        // Unitless number treated as degrees
                        filter->params.angle = (float)arg->data.number.value * ((float)M_PI / 180.0f);
                    } else {
                        filter->params.angle = 0;
                    }
                    log_debug("[CSS] filter: hue-rotate(%.2frad)", filter->params.angle);
                }
                else if (strcmp(name, "drop-shadow") == 0) {
                    filter->type = FILTER_DROP_SHADOW;
                    filter->params.drop_shadow.offset_x = 0;
                    filter->params.drop_shadow.offset_y = 0;
                    filter->params.drop_shadow.blur_radius = 0;
                    filter->params.drop_shadow.color.r = 0;
                    filter->params.drop_shadow.color.g = 0;
                    filter->params.drop_shadow.color.b = 0;
                    filter->params.drop_shadow.color.a = 255;

                    // Parse drop-shadow arguments: <offset-x> <offset-y> [<blur-radius>] [<color>]
                    int len_idx = 0;
                    for (int i = 0; i < func->arg_count; i++) {
                        CssValue* a = func->args[i];
                        if (!a) continue;
                        if (a->type == CSS_VALUE_TYPE_LENGTH) {
                            float val = resolve_length_value(lycon, prop_id, a);
                            if (len_idx == 0) filter->params.drop_shadow.offset_x = val;
                            else if (len_idx == 1) filter->params.drop_shadow.offset_y = val;
                            else if (len_idx == 2) filter->params.drop_shadow.blur_radius = val;
                            len_idx++;
                        } else if (a->type == CSS_VALUE_TYPE_COLOR) {
                            filter->params.drop_shadow.color.r = a->data.color.data.rgba.r;
                            filter->params.drop_shadow.color.g = a->data.color.data.rgba.g;
                            filter->params.drop_shadow.color.b = a->data.color.data.rgba.b;
                            filter->params.drop_shadow.color.a = a->data.color.data.rgba.a;
                        }
                    }
                    log_debug("[CSS] filter: drop-shadow(%.2f %.2f %.2f rgba(%d,%d,%d,%.2f))",
                        filter->params.drop_shadow.offset_x, filter->params.drop_shadow.offset_y,
                        filter->params.drop_shadow.blur_radius,
                        filter->params.drop_shadow.color.r, filter->params.drop_shadow.color.g,
                        filter->params.drop_shadow.color.b, filter->params.drop_shadow.color.a / 255.0f);
                }
                else {
                    log_debug("[CSS] filter: unknown function '%s'", name);
                    return nullptr;
                }

                return filter;
            };

            // Handle "none" keyword
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                span->filter = nullptr;
                log_debug("[CSS] filter: none");
                break;
            }

            // Handle single filter function
            if (value->type == CSS_VALUE_TYPE_FUNCTION) {
                span->filter = (FilterProp*)alloc_prop(lycon, sizeof(FilterProp));
                span->filter->functions = parse_filter_func(value->data.function);
                break;
            }

            // Handle list of filter functions
            if (value->type == CSS_VALUE_TYPE_LIST) {
                span->filter = (FilterProp*)alloc_prop(lycon, sizeof(FilterProp));
                span->filter->functions = nullptr;
                FilterFunction* tail = nullptr;

                for (int i = 0; i < value->data.list.count; i++) {
                    CssValue* item = value->data.list.values[i];
                    if (item && item->type == CSS_VALUE_TYPE_FUNCTION) {
                        FilterFunction* f = parse_filter_func(item->data.function);
                        if (f) {
                            if (!span->filter->functions) {
                                span->filter->functions = f;
                            } else {
                                tail->next = f;
                            }
                            tail = f;
                        }
                    }
                }
            }
            break;
        }

        // ========================================================================
        // Multi-column Layout Properties
        // ========================================================================

        case CSS_PROPERTY_COLUMN_COUNT: {
            log_debug("[CSS] Processing column-count property");
            if (!block) {
                log_debug("[CSS] column-count: Cannot apply to non-block element");
                break;
            }

            if (!block->multicol) {
                block->multicol = (MultiColumnProp*)alloc_prop(lycon, sizeof(MultiColumnProp));
                block->multicol->column_count = 0;  // auto
                block->multicol->column_width = 0;  // auto
                block->multicol->column_gap = 16.0f;  // default 1em (assuming 16px font)
                block->multicol->column_gap_is_normal = true;
                block->multicol->rule_width = 0;
                block->multicol->rule_style = CSS_VALUE_NONE;
                block->multicol->rule_color.r = 0;
                block->multicol->rule_color.g = 0;
                block->multicol->rule_color.b = 0;
                block->multicol->rule_color.a = 255;
                block->multicol->span = COLUMN_SPAN_NONE;
                block->multicol->fill = COLUMN_FILL_BALANCE;
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_AUTO) {
                block->multicol->column_count = 0;  // auto
                log_debug("[CSS] column-count: auto");
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                int count = (int)value->data.number.value;
                if (count > 0) {
                    block->multicol->column_count = count;
                    log_debug("[CSS] column-count: %d", count);
                }
            }
            break;
        }

        case CSS_PROPERTY_COLUMN_WIDTH: {
            log_debug("[CSS] Processing column-width property");
            if (!block) {
                log_debug("[CSS] column-width: Cannot apply to non-block element");
                break;
            }

            if (!block->multicol) {
                block->multicol = (MultiColumnProp*)alloc_prop(lycon, sizeof(MultiColumnProp));
                block->multicol->column_count = 0;
                block->multicol->column_width = 0;
                block->multicol->column_gap = 16.0f;
                block->multicol->column_gap_is_normal = true;
                block->multicol->rule_width = 0;
                block->multicol->rule_style = CSS_VALUE_NONE;
                block->multicol->rule_color.r = 0;
                block->multicol->rule_color.g = 0;
                block->multicol->rule_color.b = 0;
                block->multicol->rule_color.a = 255;
                block->multicol->span = COLUMN_SPAN_NONE;
                block->multicol->fill = COLUMN_FILL_BALANCE;
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_AUTO) {
                block->multicol->column_width = 0;  // auto
                log_debug("[CSS] column-width: auto");
            } else if (value->type == CSS_VALUE_TYPE_LENGTH) {
                float width = resolve_length_value(lycon, prop_id, value);
                if (width > 0) {
                    block->multicol->column_width = width;
                    log_debug("[CSS] column-width: %.2fpx", width);
                }
            }
            break;
        }

        case CSS_PROPERTY_COLUMN_RULE_WIDTH: {
            log_debug("[CSS] Processing column-rule-width property");
            if (!block) break;

            if (!block->multicol) {
                block->multicol = (MultiColumnProp*)alloc_prop(lycon, sizeof(MultiColumnProp));
                memset(block->multicol, 0, sizeof(MultiColumnProp));
                block->multicol->column_gap = 16.0f;
                block->multicol->column_gap_is_normal = true;
                block->multicol->fill = COLUMN_FILL_BALANCE;
            }

            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                block->multicol->rule_width = resolve_length_value(lycon, prop_id, value);
                log_debug("[CSS] column-rule-width: %.2fpx", block->multicol->rule_width);
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // thin, medium, thick
                CssEnum kw = value->data.keyword;
                if (kw == CSS_VALUE_THIN) block->multicol->rule_width = 1.0f;
                else if (kw == CSS_VALUE_MEDIUM) block->multicol->rule_width = 3.0f;
                else if (kw == CSS_VALUE_THICK) block->multicol->rule_width = 5.0f;
                log_debug("[CSS] column-rule-width keyword: %.2fpx", block->multicol->rule_width);
            }
            break;
        }

        case CSS_PROPERTY_COLUMN_RULE_STYLE: {
            log_debug("[CSS] Processing column-rule-style property");
            if (!block) break;

            if (!block->multicol) {
                block->multicol = (MultiColumnProp*)alloc_prop(lycon, sizeof(MultiColumnProp));
                memset(block->multicol, 0, sizeof(MultiColumnProp));
                block->multicol->column_gap = 16.0f;
                block->multicol->column_gap_is_normal = true;
                block->multicol->fill = COLUMN_FILL_BALANCE;
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                block->multicol->rule_style = value->data.keyword;
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                log_debug("[CSS] column-rule-style: %s", info ? info->name : "unknown");
            }
            break;
        }

        case CSS_PROPERTY_COLUMN_RULE_COLOR: {
            log_debug("[CSS] Processing column-rule-color property");
            if (!block) break;

            if (!block->multicol) {
                block->multicol = (MultiColumnProp*)alloc_prop(lycon, sizeof(MultiColumnProp));
                memset(block->multicol, 0, sizeof(MultiColumnProp));
                block->multicol->column_gap = 16.0f;
                block->multicol->column_gap_is_normal = true;
                block->multicol->fill = COLUMN_FILL_BALANCE;
            }

            if (value->type == CSS_VALUE_TYPE_COLOR) {
                block->multicol->rule_color.r = value->data.color.data.rgba.r;
                block->multicol->rule_color.g = value->data.color.data.rgba.g;
                block->multicol->rule_color.b = value->data.color.data.rgba.b;
                block->multicol->rule_color.a = value->data.color.data.rgba.a;
                log_debug("[CSS] column-rule-color: rgba(%d,%d,%d,%.2f)",
                    block->multicol->rule_color.r, block->multicol->rule_color.g,
                    block->multicol->rule_color.b, block->multicol->rule_color.a / 255.0f);
            }
            break;
        }

        case CSS_PROPERTY_COLUMN_SPAN: {
            log_debug("[CSS] Processing column-span property");
            if (!block) break;

            if (!block->multicol) {
                block->multicol = (MultiColumnProp*)alloc_prop(lycon, sizeof(MultiColumnProp));
                memset(block->multicol, 0, sizeof(MultiColumnProp));
                block->multicol->column_gap = 16.0f;
                block->multicol->column_gap_is_normal = true;
                block->multicol->fill = COLUMN_FILL_BALANCE;
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum kw = value->data.keyword;
                if (kw == CSS_VALUE_ALL) {
                    block->multicol->span = COLUMN_SPAN_ALL;
                    log_debug("[CSS] column-span: all");
                } else {
                    block->multicol->span = COLUMN_SPAN_NONE;
                    log_debug("[CSS] column-span: none");
                }
            }
            break;
        }

        case CSS_PROPERTY_COLUMN_FILL: {
            log_debug("[CSS] Processing column-fill property");
            if (!block) break;

            if (!block->multicol) {
                block->multicol = (MultiColumnProp*)alloc_prop(lycon, sizeof(MultiColumnProp));
                memset(block->multicol, 0, sizeof(MultiColumnProp));
                block->multicol->column_gap = 16.0f;
                block->multicol->column_gap_is_normal = true;
                block->multicol->fill = COLUMN_FILL_BALANCE;
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum kw = value->data.keyword;
                if (kw == CSS_VALUE_AUTO) {
                    block->multicol->fill = COLUMN_FILL_AUTO;
                    log_debug("[CSS] column-fill: auto");
                } else {
                    block->multicol->fill = COLUMN_FILL_BALANCE;
                    log_debug("[CSS] column-fill: balance");
                }
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
                float width = resolve_length_value(lycon, prop_id, value);
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
                float width = resolve_length_value(lycon, prop_id, value);
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
                float width = resolve_length_value(lycon, prop_id, value);
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
                float width = resolve_length_value(lycon, prop_id, value);
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
                CssEnum val = value->data.keyword;
                span->bound->border->top_style = val;
                const CssEnumInfo* info = css_enum_info(val);
                log_debug("[CSS] Border-top-style: %s -> %d", info ? info->name : "unknown", val);
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
                CssEnum val = value->data.keyword;
                span->bound->border->right_style = val;
                const CssEnumInfo* info = css_enum_info(val);
                log_debug("[CSS] Border-right-style: %s -> %d", info ? info->name : "unknown", val);
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
                CssEnum val = value->data.keyword;
                span->bound->border->bottom_style = val;
                const CssEnumInfo* info = css_enum_info(val);
                log_debug("[CSS] Border-bottom-style: %s -> %d", info ? info->name : "unknown", val);
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
                CssEnum val = value->data.keyword;
                span->bound->border->left_style = val;
                const CssEnumInfo* info = css_enum_info(val);
                log_debug("[CSS] Border-left-style: %s -> %d", info ? info->name : "unknown", val);
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
                span->bound->border->top_color = resolve_color_value(lycon, value);
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
                span->bound->border->right_color = resolve_color_value(lycon, value);
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
                span->bound->border->bottom_color = resolve_color_value(lycon, value);
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
                span->bound->border->left_color = resolve_color_value(lycon, value);
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

            // Handle inherit keyword for border shorthand
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_INHERIT) {
                // Find parent with border and copy values
                DomElement* current = (DomElement*)lycon->view;
                if (current && current->parent && current->parent->is_element()) {
                    DomElement* parent = (DomElement*)current->parent;
                    if (parent->bound && parent->bound->border) {
                        BorderProp* pb = parent->bound->border;
                        span->bound->border->width.top = pb->width.top;
                        span->bound->border->width.right = pb->width.right;
                        span->bound->border->width.bottom = pb->width.bottom;
                        span->bound->border->width.left = pb->width.left;
                        span->bound->border->width.top_specificity = specificity;
                        span->bound->border->width.right_specificity = specificity;
                        span->bound->border->width.bottom_specificity = specificity;
                        span->bound->border->width.left_specificity = specificity;
                        span->bound->border->top_style = pb->top_style;
                        span->bound->border->right_style = pb->right_style;
                        span->bound->border->bottom_style = pb->bottom_style;
                        span->bound->border->left_style = pb->left_style;
                        span->bound->border->top_color = pb->top_color;
                        span->bound->border->right_color = pb->right_color;
                        span->bound->border->bottom_color = pb->bottom_color;
                        span->bound->border->left_color = pb->left_color;
                        span->bound->border->top_color_specificity = specificity;
                        span->bound->border->right_color_specificity = specificity;
                        span->bound->border->bottom_color_specificity = specificity;
                        span->bound->border->left_color_specificity = specificity;
                        log_debug("[CSS] border: inherit - copied border from parent (width: %.2f)", pb->width.top);
                    } else {
                        log_debug("[CSS] border: inherit - no parent border found, using defaults");
                    }
                }
                break;
            }

            // Border shorthand: <width> <style> <color> (any order)
            // Parse values from the list or single value
            float border_width = -1.0f;  CssEnum border_style = CSS_VALUE__UNDEF;  Color border_color = {0};
            if (value->type == CSS_VALUE_TYPE_LIST) {
                // Multiple values
                log_debug("[CSS] Border shorthand has multiple values: %d", value->data.list.count);
                size_t count = value->data.list.count;
                CssValue** values = value->data.list.values;
                for (size_t i = 0; i < count; i++) {
                    CssValue* val = values[i];
                    if (val->type == CSS_VALUE_TYPE_LENGTH || val->type == CSS_VALUE_TYPE_NUMBER) {
                        // Width - convert to pixels (NUMBER handles unitless 0)
                        border_width = resolve_length_value(lycon, prop_id, val);
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
                        border_color = resolve_color_value(lycon, val);
                    }
                    else if (val->type == CSS_VALUE_TYPE_FUNCTION) {
                        // Color function like rgb(), rgba(), hsl(), hsla()
                        log_debug("[CSS] Border color from function");
                        border_color = resolve_color_value(lycon, val);
                    }
                    else {
                        log_debug("[CSS] Unrecognized border shorthand value type: %d", val->type);
                    }
                }
            } else {
                // Single value
                log_debug("[CSS] Border shorthand has single value of type: %d", value->type);
                if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_NUMBER) {
                    // Width - convert to pixels (NUMBER handles unitless 0)
                    border_width = resolve_length_value(lycon, prop_id, value);
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
                    border_color = resolve_color_value(lycon, value);
                } else if (value->type == CSS_VALUE_TYPE_FUNCTION) {
                    // Color function like rgb(), rgba(), hsl(), hsla()
                    border_color = resolve_color_value(lycon, value);
                }
            }

            // Apply to all 4 sides
            // CSS spec: when style is set (and visible) but width is not, default to 'medium' (3px)
            // none/hidden styles mean no border, so no default width
            if (border_style >= 0 && border_width < 0 &&
                border_style != CSS_VALUE_NONE && border_style != CSS_VALUE_HIDDEN) {
                border_width = 3.0f;  // medium
            }
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
            // Handle inherit keyword
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_INHERIT) {
                copy_border_side_inherit(lycon, span, 0, specificity);
                break;
            }
            MultiValue border = {0};
            set_multi_value( &border, value);
            if (border.style) {
                span->bound->border->top_style = border.style->data.keyword;
                span->bound->border->top_style_specificity = specificity;
                // CSS spec: when style is set (and visible) but width is not, default to 'medium' (3px)
                // none/hidden styles mean no border, so no default width
                if (!border.length && border.style->data.keyword != CSS_VALUE_NONE &&
                    border.style->data.keyword != CSS_VALUE_HIDDEN &&
                    specificity >= span->bound->border->width.top_specificity) {
                    span->bound->border->width.top = 3.0f;  // medium
                    span->bound->border->width.top_specificity = specificity;
                }
            }
            if (border.length) {
                span->bound->border->width.top = resolve_length_value(lycon, CSS_PROPERTY_BORDER_TOP_WIDTH, border.length);
                span->bound->border->width.top_specificity = specificity;
            }
            if (border.color) {
                span->bound->border->top_color = resolve_color_value(lycon, border.color);
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
            // Handle inherit keyword
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_INHERIT) {
                copy_border_side_inherit(lycon, span, 1, specificity);
                break;
            }
            MultiValue border = {0};
            set_multi_value( &border, value);
            if (border.style) {
                span->bound->border->right_style = border.style->data.keyword;
                span->bound->border->right_style_specificity = specificity;
                // CSS spec: when style is set (and visible) but width is not, default to 'medium' (3px)
                // none/hidden styles mean no border, so no default width
                if (!border.length && border.style->data.keyword != CSS_VALUE_NONE &&
                    border.style->data.keyword != CSS_VALUE_HIDDEN &&
                    specificity >= span->bound->border->width.right_specificity) {
                    span->bound->border->width.right = 3.0f;  // medium
                    span->bound->border->width.right_specificity = specificity;
                }
            }
            if (border.length) {
                span->bound->border->width.right = resolve_length_value(lycon, CSS_PROPERTY_BORDER_RIGHT_WIDTH, border.length);
                span->bound->border->width.right_specificity = specificity;
            }
            if (border.color) {
                span->bound->border->right_color = resolve_color_value(lycon, border.color);
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
            // Handle inherit keyword
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_INHERIT) {
                copy_border_side_inherit(lycon, span, 2, specificity);
                break;
            }
            MultiValue border = {0};
            set_multi_value( &border, value);
            if (border.style) {
                span->bound->border->bottom_style = border.style->data.keyword;
                span->bound->border->bottom_style_specificity = specificity;
                // CSS spec: when style is set (and visible) but width is not, default to 'medium' (3px)
                // none/hidden styles mean no border, so no default width
                if (!border.length && border.style->data.keyword != CSS_VALUE_NONE &&
                    border.style->data.keyword != CSS_VALUE_HIDDEN &&
                    specificity >= span->bound->border->width.bottom_specificity) {
                    span->bound->border->width.bottom = 3.0f;  // medium
                    span->bound->border->width.bottom_specificity = specificity;
                }
            }
            if (border.length) {
                span->bound->border->width.bottom = resolve_length_value(lycon, CSS_PROPERTY_BORDER_BOTTOM_WIDTH, border.length);
                span->bound->border->width.bottom_specificity = specificity;
            }
            if (border.color) {
                span->bound->border->bottom_color = resolve_color_value(lycon, border.color);
                span->bound->border->bottom_color_specificity = specificity;
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
            // Handle inherit keyword
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_INHERIT) {
                copy_border_side_inherit(lycon, span, 3, specificity);
                break;
            }
            MultiValue border = {0};
            set_multi_value( &border, value);
            if (border.style) {
                span->bound->border->left_style = border.style->data.keyword;
                span->bound->border->left_style_specificity = specificity;
                // CSS spec: when style is set (and visible) but width is not, default to 'medium' (3px)
                // none/hidden styles mean no border, so no default width
                if (!border.length && border.style->data.keyword != CSS_VALUE_NONE &&
                    border.style->data.keyword != CSS_VALUE_HIDDEN &&
                    specificity >= span->bound->border->width.left_specificity) {
                    span->bound->border->width.left = 3.0f;  // medium
                    span->bound->border->width.left_specificity = specificity;
                }
            }
            if (border.length) {
                span->bound->border->width.left = resolve_length_value(lycon, CSS_PROPERTY_BORDER_LEFT_WIDTH, border.length);
                span->bound->border->width.left_specificity = specificity;
            }
            if (border.color) {
                span->bound->border->left_color = resolve_color_value(lycon, border.color);
                span->bound->border->left_color_specificity = specificity;
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
                Color color = resolve_color_value(lycon, value);

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
                    Color vertical = resolve_color_value(lycon, values[0]);
                    Color horizontal = resolve_color_value(lycon, values[1]);

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
                    Color top = resolve_color_value(lycon, values[0]);
                    Color horizontal = resolve_color_value(lycon, values[1]);
                    Color bottom = resolve_color_value(lycon, values[2]);

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
                    Color top = resolve_color_value(lycon, values[0]);
                    Color right = resolve_color_value(lycon, values[1]);
                    Color bottom = resolve_color_value(lycon, values[2]);
                    Color left = resolve_color_value(lycon, values[3]);

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
                block->position = alloc_position_prop(lycon);
            }
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum val = value->data.keyword;
                block->position->position = val;
                const CssEnumInfo* info = css_enum_info(val);
                log_debug("[CSS] Position: %s -> %d", info ? info->name : "unknown", val);
            }
            break;
        }

        case CSS_PROPERTY_TOP: {
            log_debug("[CSS] Processing top property");
            if (!block) break;
            if (!block->position) {
                block->position = alloc_position_prop(lycon);
            }
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {  // ignore 'auto' or any other keyword
                block->position->has_top = false;
            } else {
                block->position->top = resolve_length_value(lycon, CSS_PROPERTY_TOP, value);
                block->position->has_top = true;
                // store raw percentage for re-resolution during absolute positioning
                if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                    block->position->top_percent = value->data.percentage.value;
                }
            }
            break;
        }
        case CSS_PROPERTY_LEFT: {
            log_debug("[CSS] Processing left property");
            if (!block) break;
            if (!block->position) {
                block->position = alloc_position_prop(lycon);
            }
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {  // ignore 'auto' or any other keyword
                block->position->has_left = false;
            } else {
                block->position->left = resolve_length_value(lycon, CSS_PROPERTY_LEFT, value);
                block->position->has_left = true;
                // store raw percentage for re-resolution during absolute positioning
                if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                    block->position->left_percent = value->data.percentage.value;
                }
            }
            break;
        }
        case CSS_PROPERTY_RIGHT: {
            log_debug("[CSS] Processing right property");
            if (!block) break;
            if (!block->position) {
                block->position = alloc_position_prop(lycon);
            }
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {  // ignore 'auto' or any other keyword
                block->position->has_right = false;
            } else {
                block->position->right = resolve_length_value(lycon, CSS_PROPERTY_RIGHT, value);
                block->position->has_right = true;
                // store raw percentage for re-resolution during absolute positioning
                if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                    block->position->right_percent = value->data.percentage.value;
                }
            }
            break;
        }
        case CSS_PROPERTY_BOTTOM: {
            log_debug("[CSS] Processing bottom property");
            if (!block) break;
            if (!block->position) {
                block->position = alloc_position_prop(lycon);
            }
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {  // ignore 'auto' or any other keyword
                block->position->has_bottom = false;
            } else {
                block->position->bottom = resolve_length_value(lycon, CSS_PROPERTY_BOTTOM, value);
                block->position->has_bottom = true;
                // store raw percentage for re-resolution during absolute positioning
                if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                    block->position->bottom_percent = value->data.percentage.value;
                }
            }
            break;
        }

        case CSS_PROPERTY_Z_INDEX: {
            log_debug("[CSS] Processing z-index property");
            if (!block) break;
            if (!block->position) {
                block->position = alloc_position_prop(lycon);
            }
            if (value->type == CSS_VALUE_TYPE_NUMBER) {
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
            if (!block->position) {
                block->position = alloc_position_prop(lycon);
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
                block->position = alloc_position_prop(lycon);
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

                    // Note: has_clip and clip bounds are set during layout in layout_block.cpp and scroller.cpp
                    // when the actual block dimensions are known
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

                    // Note: has_clip is set during layout when dimensions are known
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

                    // Note: has_clip is set during layout when dimensions are known
                }
            }
            break;
        }

        // ===== GROUP 9: White-space Property =====

        case CSS_PROPERTY_WHITE_SPACE: {
            log_debug("[CSS] Processing white-space property");
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
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
            if (!span->in_line) {
                span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
            }
            // Visibility applies to all elements, stored in ViewSpan
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum visibility_value = value->data.keyword;
                if (visibility_value > 0) {
                    span->in_line->visibility = visibility_value;
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
            // TODO: Parse rect() values and set block->scroller->clip bounds
            // For now, clip bounds will be set during layout based on block dimensions
            log_debug("[CSS] Clip property detected (rect parsing not yet implemented)");
            break;
        }

        // ===== GROUP 11: Box Sizing =====
        case CSS_PROPERTY_BOX_SIZING: {
            log_debug("[CSS] Processing box-sizing property");
            if (!block) break;
            if (!block->blk) { block->blk = alloc_block_prop(lycon); }
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum boxsizing_value = value->data.keyword;
                if (boxsizing_value > 0) {
                    block->blk->box_sizing = boxsizing_value;
                    log_debug("[CSS] Box-sizing: %s -> 0x%04X", css_enum_info(value->data.keyword)->name, boxsizing_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_ASPECT_RATIO: {
            log_debug("[CSS] Processing aspect-ratio property");
            // aspect-ratio can apply to block-level and flex/grid items
            // For grid items, aspect-ratio is read from specified_style during layout
            // (fi and gi are in a union, so we can't store aspect_ratio in fi for grid items)
            if (!span) break;

            // Don't allocate fi for grid items - it would overwrite gi in the union!
            // Grid layout reads aspect-ratio from specified_style instead
            if (span->item_prop_type == DomElement::ITEM_PROP_GRID) {
                log_debug("[CSS] aspect-ratio: skipping fi allocation for grid item (will read from specified_style)");
                break;
            }

            if (!span->fi) { alloc_flex_item_prop(lycon, span); }
            if (!span->fi) break;

            // aspect-ratio values: auto | <ratio> | auto && <ratio>
            // <ratio> is expressed as "width / height" (e.g., "16 / 9") or just a number (e.g., "2")
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // "auto" means no aspect ratio enforced
                span->fi->aspect_ratio = 0;
                log_debug("[CSS] aspect-ratio: auto");
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                // Single number means ratio = number (e.g., aspect-ratio: 2 means 2/1)
                span->fi->aspect_ratio = (float)value->data.number.value;
                log_debug("[CSS] aspect-ratio: %.3f (from number)", span->fi->aspect_ratio);
            } else if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count >= 2) {
                // List format: [number, "/", number] for "16 / 9" syntax
                // Find two numbers in the list
                double numerator = 0, denominator = 0;
                bool got_numerator = false, got_denominator = false;
                for (int i = 0; i < value->data.list.count && !got_denominator; i++) {
                    CssValue* item = value->data.list.values[i];
                    if (item && item->type == CSS_VALUE_TYPE_NUMBER) {
                        if (!got_numerator) {
                            numerator = item->data.number.value;
                            got_numerator = true;
                        } else {
                            denominator = item->data.number.value;
                            got_denominator = true;
                        }
                    }
                }
                if (got_numerator && got_denominator && denominator > 0) {
                    span->fi->aspect_ratio = (float)(numerator / denominator);
                    log_debug("[CSS] aspect-ratio: %.3f (from %g / %g)", span->fi->aspect_ratio,
                              numerator, denominator);
                } else if (got_numerator) {
                    // Just one number in list means ratio = number
                    span->fi->aspect_ratio = (float)numerator;
                    log_debug("[CSS] aspect-ratio: %.3f (from single number in list)", span->fi->aspect_ratio);
                }
            }
            break;
        }

        // ===== GROUP 12: Advanced Typography Properties =====

        case CSS_PROPERTY_FONT_STYLE: {
            log_debug("[CSS] Processing font-style property");
            if (!span->font) { span->font = alloc_font_prop(lycon); }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum val = value->data.keyword;
                if (val > 0) {
                    span->font->font_style = val;
                    log_debug("[CSS] font-style: %s -> 0x%04X", css_enum_info(value->data.keyword)->name, val);
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
                CssEnum val = value->data.keyword;
                if (val > 0) {
                    block->blk->text_transform = val;
                    log_debug("[CSS] text-transform: %s -> 0x%04X",
                             css_enum_info(value->data.keyword)->name, val);
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
                CssEnum val = value->data.keyword;
                if (val > 0) {
                    // Note: Adding text_overflow field to BlockProp would be needed
                    log_debug("[CSS] text-overflow: %s -> 0x%04X (field not yet added to BlockProp)",
                             css_enum_info(value->data.keyword)->name, val);
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
                CssEnum val = value->data.keyword;
                if (val > 0) {
                    block->blk->word_break = val;
                    log_debug("[CSS] word-break: %s -> 0x%04X",
                             css_enum_info(value->data.keyword)->name, val);
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
                CssEnum val = value->data.keyword;
                if (val > 0) {
                    // Note: Adding word_wrap field to BlockProp would be needed
                    log_debug("[CSS] word-wrap: %s -> 0x%04X (field not yet added to BlockProp)",
                        css_enum_info(value->data.keyword)->name, val);
                }
            }
            break;
        }

        case CSS_PROPERTY_FONT_VARIANT: {
            log_debug("[CSS] Processing font-variant property");
            if (!span->font) {
                span->font = alloc_font_prop(lycon);
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum val = value->data.keyword;
                if (val == CSS_VALUE_INHERIT) {
                    // inherit from parent
                    DomElement* ancestor = static_cast<DomElement*>(lycon->view);
                    if (ancestor && ancestor->font) {
                        span->font->font_variant = ancestor->font->font_variant;
                    }
                } else if (val > 0) {
                    span->font->font_variant = val;
                    log_debug("[CSS] font-variant: %s -> 0x%04X",
                        css_enum_info(val)->name, val);
                }
            } else if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
                // Handle unregistered keywords (e.g., "small-caps" parsed as custom)
                CssEnum val = css_enum_by_name(value->data.custom_property.name);
                if (val != CSS_VALUE__UNDEF) {
                    span->font->font_variant = val;
                    log_debug("[CSS] font-variant from custom: %s -> 0x%04X",
                        value->data.custom_property.name, val);
                }
            }
            break;
        }

        case CSS_PROPERTY_LETTER_SPACING: {
            log_debug("[CSS] Processing letter-spacing property");
            if (!span->font) { span->font = alloc_font_prop(lycon); }

            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                float spacing = resolve_length_value(lycon, prop_id, value);
                span->font->letter_spacing = spacing;
                log_debug("[CSS] letter-spacing: %.2fpx", spacing);
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NORMAL) {
                span->font->letter_spacing = 0.0f;
                log_debug("[CSS] letter-spacing: normal -> 0px");
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
                float spacing = resolve_length_value(lycon, prop_id, value);
                span->font->word_spacing = spacing;
                log_debug("[CSS] word-spacing: %.2fpx", spacing);
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NORMAL) {
                span->font->word_spacing = 0.0f;
                log_debug("[CSS] word-spacing: normal -> 0px");
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
                CssEnum val = value->data.keyword;
                if (val > 0) {
                    block->embed->flex->direction = val;
                    log_debug("[CSS] flex-direction: %s -> 0x%04X", css_enum_info(value->data.keyword)->name, val);
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
            alloc_flex_prop(lycon, block);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum val = value->data.keyword;
                if (val > 0) {
                    block->embed->flex->wrap = val;
                    log_debug("[CSS] flex-wrap: %s -> 0x%04X", css_enum_info(value->data.keyword)->name, val);
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

            // Allocate FlexProp if needed (for flexbox)
            alloc_flex_prop(lycon, block);

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum val = value->data.keyword;
                if (val > 0) {
                    block->embed->flex->justify = val;
                    log_debug("[CSS] justify-content (flex): %s -> 0x%04X", css_enum_info(value->data.keyword)->name, val);
                }
            }

            // Also allocate GridProp and store value (for grid containers)
            alloc_grid_prop(lycon, block);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum val = value->data.keyword;
                if (val > 0) {
                    block->embed->grid->justify_content = val;
                    log_debug("[CSS] justify-content (grid): %s -> 0x%04X", css_enum_info(value->data.keyword)->name, val);
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

            // Allocate FlexProp if needed (for flexbox)
            alloc_flex_prop(lycon, block);

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum val = value->data.keyword;
                if (val > 0) {
                    block->embed->flex->align_items = val;
                    log_debug("[CSS] align-items (flex): %s -> 0x%04X", css_enum_info(value->data.keyword)->name, val);
                }
            }

            // Also allocate GridProp and store value (for grid containers)
            alloc_grid_prop(lycon, block);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum val = value->data.keyword;
                if (val > 0) {
                    block->embed->grid->align_items = val;
                    log_debug("[CSS] align-items (grid): %s -> 0x%04X", css_enum_info(value->data.keyword)->name, val);
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

            // Allocate FlexProp if needed (for flexbox)
            alloc_flex_prop(lycon, block);

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum val = value->data.keyword;
                if (val > 0) {
                    block->embed->flex->align_content = val;
                    log_debug("[CSS] align-content (flex): %s -> 0x%04X", css_enum_info(value->data.keyword)->name, val);
                }
            }

            // Also allocate GridProp and store value (for grid containers)
            alloc_grid_prop(lycon, block);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum val = value->data.keyword;
                if (val > 0) {
                    block->embed->grid->align_content = val;
                    log_debug("[CSS] align-content (grid): %s -> 0x%04X", css_enum_info(value->data.keyword)->name, val);
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

            float gap_value = 0;
            bool is_percent = false;
            if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_NUMBER) {
                gap_value = resolve_length_value(lycon, prop_id, value);
                log_debug("[CSS] row-gap: %.2fpx", gap_value);
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                gap_value = value->data.percentage.value;
                is_percent = true;
                log_debug("[CSS] row-gap: %.2f%% (percentage)", gap_value);
            }

            // Always apply to flex (for flexbox containers)
            alloc_flex_prop(lycon, block);
            block->embed->flex->row_gap = gap_value;
            block->embed->flex->row_gap_is_percent = is_percent;

            // Always apply to grid (for grid containers)
            // Display may not be resolved yet, so we store it in both places
            alloc_grid_prop(lycon, block);
            block->embed->grid->row_gap = gap_value;
            log_debug("[CSS] row-gap applied: %.2f (stored in both flex and grid)", gap_value);
            break;
        }

        case CSS_PROPERTY_COLUMN_GAP: {
            log_debug("[CSS] Processing column-gap property");
            if (!block) {
                log_debug("[CSS] column-gap: Cannot apply to non-block element");
                break;
            }

            float gap_value = 0;
            bool is_percent = false;
            bool is_normal = false;

            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NORMAL) {
                gap_value = 16.0f;  // Default 1em = 16px
                is_normal = true;
                log_debug("[CSS] column-gap: normal (16px)");
            } else if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_NUMBER) {
                gap_value = resolve_length_value(lycon, prop_id, value);
                log_debug("[CSS] column-gap: %.2fpx", gap_value);
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                gap_value = value->data.percentage.value;
                is_percent = true;
                log_debug("[CSS] column-gap: %.2f%% (percentage)", gap_value);
            }

            // Always apply to flex (for flexbox containers)
            alloc_flex_prop(lycon, block);
            block->embed->flex->column_gap = gap_value;
            block->embed->flex->column_gap_is_percent = is_percent;

            // Always apply to grid (for grid containers)
            alloc_grid_prop(lycon, block);
            block->embed->grid->column_gap = gap_value;

            // Also apply to multi-column layout
            // Create multicol struct if it doesn't exist (column-gap may be processed before column-count)
            if (!block->multicol) {
                block->multicol = (MultiColumnProp*)alloc_prop(lycon, sizeof(MultiColumnProp));
                block->multicol->column_count = 0;  // auto
                block->multicol->column_width = 0;  // auto
                block->multicol->column_gap = 16.0f;  // default 1em
                block->multicol->column_gap_is_normal = true;
                block->multicol->rule_width = 0;
                block->multicol->rule_style = CSS_VALUE_NONE;
                block->multicol->rule_color.r = 0;
                block->multicol->rule_color.g = 0;
                block->multicol->rule_color.b = 0;
                block->multicol->rule_color.a = 255;
                block->multicol->span = COLUMN_SPAN_NONE;
                block->multicol->fill = COLUMN_FILL_BALANCE;
            }
            block->multicol->column_gap = gap_value;
            block->multicol->column_gap_is_normal = is_normal;

            log_debug("[CSS] column-gap applied: %.2f (stored in flex, grid, and multicol)", gap_value);
            break;
        }

        // Grid Template Properties
        case CSS_PROPERTY_GRID_TEMPLATE_COLUMNS: {
            log_debug("[CSS] Processing grid-template-columns property, view_type=%d, block=%p",
                      lycon->view->view_type, (void*)block);
            if (!block) {
                log_debug("[CSS] grid-template-columns: Cannot apply to non-block element");
                break;
            }

            alloc_grid_prop(lycon, block);
            GridProp* grid = block->embed->grid;
            log_debug("[CSS] grid-template-columns: value_type=%d, grid=%p", value->type, (void*)grid);

            // Handle "none" keyword
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                log_debug("[CSS] grid-template-columns: none");
                if (grid->grid_template_columns) {
                    destroy_grid_track_list(grid->grid_template_columns);
                    grid->grid_template_columns = NULL;
                }
                break;
            }

            // Parse list of track sizes using helper function
            if (value->type == CSS_VALUE_TYPE_LIST) {
                log_debug("[CSS] grid-template-columns: using parse_grid_track_list helper (LIST)");
                parse_grid_track_list(value, &grid->grid_template_columns);
                log_debug("[CSS] grid-template-columns: %d tracks parsed",
                          grid->grid_template_columns ? grid->grid_template_columns->track_count : 0);
            }
            // Handle single function value (e.g., repeat(...))
            else if (value->type == CSS_VALUE_TYPE_FUNCTION) {
                log_debug("[CSS] grid-template-columns: handling single FUNCTION value");
                GridTrackSize* ts = parse_css_value_to_track_size(value);
                if (ts) {
                    // For fixed-count repeat(), expand immediately
                    if (ts->type == GRID_TRACK_SIZE_REPEAT && !ts->is_auto_fill && !ts->is_auto_fit && ts->repeat_count > 0) {
                        int total = ts->repeat_count * ts->repeat_track_count;
                        log_debug("[CSS] grid-template-columns: expanding fixed repeat(%d, ...) -> %d tracks",
                                  ts->repeat_count, total);
                        if (!grid->grid_template_columns || grid->grid_template_columns->allocated_tracks < total) {
                            if (grid->grid_template_columns) destroy_grid_track_list(grid->grid_template_columns);
                            grid->grid_template_columns = create_grid_track_list(total);
                        } else {
                            grid->grid_template_columns->track_count = 0;
                        }
                        for (int r = 0; r < ts->repeat_count; r++) {
                            for (int t = 0; t < ts->repeat_track_count; t++) {
                                grid->grid_template_columns->tracks[grid->grid_template_columns->track_count++] = ts->repeat_tracks[t];
                            }
                        }
                    } else {
                        // auto-fill/auto-fit or other function - store as single track for layout-time expansion
                        if (!grid->grid_template_columns) {
                            grid->grid_template_columns = create_grid_track_list(1);
                        } else {
                            grid->grid_template_columns->track_count = 0;
                        }
                        grid->grid_template_columns->tracks[0] = ts;
                        grid->grid_template_columns->track_count = 1;
                        if (ts->type == GRID_TRACK_SIZE_REPEAT) {
                            grid->grid_template_columns->is_repeat = true;
                        }
                    }
                    log_debug("[CSS] grid-template-columns: parsed FUNCTION -> %d tracks",
                              grid->grid_template_columns->track_count);
                }
            }
            // Handle single length/percentage value (e.g., 100px)
            else if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                log_debug("[CSS] grid-template-columns: handling single LENGTH/PERCENTAGE value");
                GridTrackSize* ts = parse_css_value_to_track_size(value);
                if (ts) {
                    if (!grid->grid_template_columns) {
                        grid->grid_template_columns = create_grid_track_list(1);
                    } else {
                        grid->grid_template_columns->track_count = 0;
                    }
                    grid->grid_template_columns->tracks[0] = ts;
                    grid->grid_template_columns->track_count = 1;
                    log_debug("[CSS] grid-template-columns: parsed single track -> %d tracks",
                              grid->grid_template_columns->track_count);
                }
            }
            break;
        }

        case CSS_PROPERTY_GRID_TEMPLATE_ROWS: {
            log_debug("[CSS] Processing grid-template-rows property");
            if (!block) {
                log_debug("[CSS] grid-template-rows: Cannot apply to non-block element");
                break;
            }

            alloc_grid_prop(lycon, block);
            GridProp* grid = block->embed->grid;

            // Handle "none" keyword
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                log_debug("[CSS] grid-template-rows: none");
                if (grid->grid_template_rows) {
                    destroy_grid_track_list(grid->grid_template_rows);
                    grid->grid_template_rows = NULL;
                }
                break;
            }

            // Parse list of track sizes using helper function
            if (value->type == CSS_VALUE_TYPE_LIST) {
                log_debug("[CSS] grid-template-rows: using parse_grid_track_list helper");
                parse_grid_track_list(value, &grid->grid_template_rows);
                log_debug("[CSS] grid-template-rows: %d tracks parsed",
                          grid->grid_template_rows ? grid->grid_template_rows->track_count : 0);
            }
            // Handle single length/percentage value (e.g., 40px)
            else if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                log_debug("[CSS] grid-template-rows: handling single LENGTH/PERCENTAGE value");
                GridTrackSize* ts = parse_css_value_to_track_size(value);
                if (ts) {
                    if (!grid->grid_template_rows) {
                        grid->grid_template_rows = create_grid_track_list(1);
                    } else {
                        grid->grid_template_rows->track_count = 0;
                    }
                    grid->grid_template_rows->tracks[0] = ts;
                    grid->grid_template_rows->track_count = 1;
                    log_debug("[CSS] grid-template-rows: parsed single track -> %d tracks",
                              grid->grid_template_rows->track_count);
                }
            }
            // Handle single function value (e.g., repeat(...))
            else if (value->type == CSS_VALUE_TYPE_FUNCTION) {
                log_debug("[CSS] grid-template-rows: handling single FUNCTION value");
                GridTrackSize* ts = parse_css_value_to_track_size(value);
                if (ts) {
                    // For fixed-count repeat(), expand immediately
                    if (ts->type == GRID_TRACK_SIZE_REPEAT && !ts->is_auto_fill && !ts->is_auto_fit && ts->repeat_count > 0) {
                        int total = ts->repeat_count * ts->repeat_track_count;
                        log_debug("[CSS] grid-template-rows: expanding fixed repeat(%d, ...) -> %d tracks",
                                  ts->repeat_count, total);
                        if (!grid->grid_template_rows || grid->grid_template_rows->allocated_tracks < total) {
                            if (grid->grid_template_rows) destroy_grid_track_list(grid->grid_template_rows);
                            grid->grid_template_rows = create_grid_track_list(total);
                        } else {
                            grid->grid_template_rows->track_count = 0;
                        }
                        for (int r = 0; r < ts->repeat_count; r++) {
                            for (int t = 0; t < ts->repeat_track_count; t++) {
                                grid->grid_template_rows->tracks[grid->grid_template_rows->track_count++] = ts->repeat_tracks[t];
                            }
                        }
                    } else {
                        // auto-fill/auto-fit or other function
                        if (!grid->grid_template_rows) {
                            grid->grid_template_rows = create_grid_track_list(1);
                        } else {
                            grid->grid_template_rows->track_count = 0;
                        }
                        grid->grid_template_rows->tracks[0] = ts;
                        grid->grid_template_rows->track_count = 1;
                        if (ts->type == GRID_TRACK_SIZE_REPEAT) {
                            grid->grid_template_rows->is_repeat = true;
                        }
                    }
                    log_debug("[CSS] grid-template-rows: parsed FUNCTION -> %d tracks",
                              grid->grid_template_rows->track_count);
                }
            }
            break;
        }

        case CSS_PROPERTY_GRID_TEMPLATE_AREAS: {
            log_debug("[CSS] Processing grid-template-areas property");
            if (!block) {
                log_debug("[CSS] grid-template-areas: Cannot apply to non-block element");
                break;
            }

            alloc_grid_prop(lycon, block);
            GridProp* grid = block->embed->grid;

            // Handle "none" keyword
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                log_debug("[CSS] grid-template-areas: none");
                // Clear existing areas
                for (int i = 0; i < grid->area_count; i++) {
                    if (grid->grid_areas && grid->grid_areas[i].name) {
                        mem_free(grid->grid_areas[i].name);
                    }
                }
                grid->area_count = 0;
                break;
            }

            // Handle string value containing area definitions
            // CSS format: "header header header" "sidebar main aside" "footer footer footer"
            if (value->type == CSS_VALUE_TYPE_STRING) {
                log_debug("[CSS] grid-template-areas: string value '%s'", value->data.string);
                parse_grid_template_areas(grid, value->data.string);
                log_debug("[CSS] grid-template-areas: parsed %d areas", grid->area_count);
            }
            // Handle list of strings (each row is a separate string)
            else if (value->type == CSS_VALUE_TYPE_LIST) {
                log_debug("[CSS] grid-template-areas: list of %d strings", value->data.list.count);
                // Concatenate all strings with quotes to form complete areas string
                // Each string needs to be wrapped in quotes for the parser
                size_t total_len = 0;
                for (int i = 0; i < value->data.list.count; i++) {
                    if (value->data.list.values[i]->type == CSS_VALUE_TYPE_STRING) {
                        // +3 for: quote, space/null, quote
                        total_len += strlen(value->data.list.values[i]->data.string) + 4;
                    }
                }
                if (total_len > 0) {
                    char* combined = (char*)mem_alloc(total_len + 1, MEM_CAT_LAYOUT);
                    combined[0] = '\0';
                    size_t combined_len = 0;
                    for (int i = 0; i < value->data.list.count; i++) {
                        if (value->data.list.values[i]->type == CSS_VALUE_TYPE_STRING) {
                            if (combined_len > 0) combined_len = str_cat(combined, combined_len, total_len + 1, " ", 1);
                            // Wrap each row in quotes
                            combined_len = str_cat(combined, combined_len, total_len + 1, "\"", 1);
                            combined_len = str_cat(combined, combined_len, total_len + 1, value->data.list.values[i]->data.string, strlen(value->data.list.values[i]->data.string));
                            combined_len = str_cat(combined, combined_len, total_len + 1, "\"", 1);
                        }
                    }
                    log_debug("[CSS] grid-template-areas: combined string '%s'", combined);
                    parse_grid_template_areas(grid, combined);
                    mem_free(combined);
                    log_debug("[CSS] grid-template-areas: parsed %d areas", grid->area_count);
                }
            }
            break;
        }

        case CSS_PROPERTY_GRID_AREA: {
            log_debug("[CSS] Processing grid-area property");
            alloc_grid_item_prop(lycon, span);

            // grid-area can be:
            // 1. A named area: grid-area: header
            // 2. A shorthand for row-start / column-start / row-end / column-end
            if (value->type == CSS_VALUE_TYPE_STRING) {
                // Named area (quoted string)
                if (span->gi->grid_area) mem_free(span->gi->grid_area);
                span->gi->grid_area = mem_strdup(value->data.string, MEM_CAT_LAYOUT);
                log_debug("[CSS] grid-area: named area (string) '%s'", span->gi->grid_area);
            }
            else if (value->type == CSS_VALUE_TYPE_CUSTOM) {
                // Named area (unquoted identifier like "header")
                // Stored as custom property reference when not a known keyword
                if (value->data.custom_property.name) {
                    if (span->gi->grid_area) mem_free(span->gi->grid_area);
                    span->gi->grid_area = mem_strdup(value->data.custom_property.name, MEM_CAT_LAYOUT);
                    log_debug("[CSS] grid-area: named area (custom) '%s'", span->gi->grid_area);
                }
            }
            else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // Can be "auto" or an identifier (area name)
                const char* name = css_enum_info(value->data.keyword)->name;
                if (value->data.keyword != CSS_VALUE_AUTO) {
                    if (span->gi->grid_area) mem_free(span->gi->grid_area);
                    span->gi->grid_area = mem_strdup(name, MEM_CAT_LAYOUT);
                    log_debug("[CSS] grid-area: named area (keyword) '%s'", span->gi->grid_area);
                }
            }
            else if (value->type == CSS_VALUE_TYPE_LIST) {
                // Shorthand: row-start / column-start / row-end / column-end
                // Values separated by /
                int count = value->data.list.count;
                log_debug("[CSS] grid-area: shorthand with %d values", count);

                auto parse_line = [](const CssValue* v, int* line, bool* has_explicit, bool* is_span) {
                    *has_explicit = false;
                    *is_span = false;
                    if (v->type == CSS_VALUE_TYPE_NUMBER) {
                        *line = (int)v->data.number.value;
                        *has_explicit = true;
                    } else if (v->type == CSS_VALUE_TYPE_KEYWORD && v->data.keyword == CSS_VALUE_AUTO) {
                        *line = 0;
                        *has_explicit = false;
                    } else if (v->type == CSS_VALUE_TYPE_FUNCTION && v->data.function) {
                        // span N - function named "span"
                        CssFunction* func = v->data.function;
                        if (strcmp(func->name, "span") == 0 && func->arg_count > 0 &&
                            func->args[0]->type == CSS_VALUE_TYPE_NUMBER) {
                            *line = -(int)func->args[0]->data.number.value;
                            *has_explicit = true;
                            *is_span = true;
                        }
                    }
                };

                if (count >= 1) {
                    parse_line(value->data.list.values[0], &span->gi->grid_row_start,
                              &span->gi->has_explicit_grid_row_start, &span->gi->grid_row_start_is_span);
                }
                if (count >= 2) {
                    parse_line(value->data.list.values[1], &span->gi->grid_column_start,
                              &span->gi->has_explicit_grid_column_start, &span->gi->grid_column_start_is_span);
                }
                if (count >= 3) {
                    parse_line(value->data.list.values[2], &span->gi->grid_row_end,
                              &span->gi->has_explicit_grid_row_end, &span->gi->grid_row_end_is_span);
                }
                if (count >= 4) {
                    parse_line(value->data.list.values[3], &span->gi->grid_column_end,
                              &span->gi->has_explicit_grid_column_end, &span->gi->grid_column_end_is_span);
                }
            }
            break;
        }

        // Grid Item Placement Properties
        case CSS_PROPERTY_GRID_COLUMN_START: {
            log_debug("[CSS] Processing grid-column-start property");
            alloc_grid_item_prop(lycon, span);
            if (value->type == CSS_VALUE_TYPE_NUMBER) {
                int line = (int)value->data.number.value;
                span->gi->grid_column_start = line;
                span->gi->has_explicit_grid_column_start = true;
                span->gi->is_grid_auto_placed = false;
                log_debug("[CSS] grid-column-start: %d", line);
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                if (value->data.keyword == CSS_VALUE_AUTO) {
                    span->gi->grid_column_start = 0;  // auto
                    log_debug("[CSS] grid-column-start: auto");
                }
            }
            break;
        }

        case CSS_PROPERTY_GRID_COLUMN_END: {
            log_debug("[CSS] Processing grid-column-end property");
            alloc_grid_item_prop(lycon, span);
            if (value->type == CSS_VALUE_TYPE_NUMBER) {
                int line = (int)value->data.number.value;
                span->gi->grid_column_end = line;
                span->gi->has_explicit_grid_column_end = true;
                span->gi->is_grid_auto_placed = false;
                log_debug("[CSS] grid-column-end: %d", line);
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_AUTO) {
                span->gi->grid_column_end = 0;  // auto
                log_debug("[CSS] grid-column-end: auto");
            }
            break;
        }

        case CSS_PROPERTY_GRID_ROW_START: {
            log_debug("[CSS] Processing grid-row-start property");
            alloc_grid_item_prop(lycon, span);
            if (value->type == CSS_VALUE_TYPE_NUMBER) {
                int line = (int)value->data.number.value;
                span->gi->grid_row_start = line;
                span->gi->has_explicit_grid_row_start = true;
                span->gi->is_grid_auto_placed = false;
                log_debug("[CSS] grid-row-start: %d", line);
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_AUTO) {
                span->gi->grid_row_start = 0;  // auto
                log_debug("[CSS] grid-row-start: auto");
            }
            break;
        }

        case CSS_PROPERTY_GRID_ROW_END: {
            log_debug("[CSS] Processing grid-row-end property");
            alloc_grid_item_prop(lycon, span);
            if (value->type == CSS_VALUE_TYPE_NUMBER) {
                int line = (int)value->data.number.value;
                span->gi->grid_row_end = line;
                span->gi->has_explicit_grid_row_end = true;
                span->gi->is_grid_auto_placed = false;
                log_debug("[CSS] grid-row-end: %d", line);
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_AUTO) {
                span->gi->grid_row_end = 0;  // auto
                log_debug("[CSS] grid-row-end: auto");
            }
            break;
        }

        case CSS_PROPERTY_GRID_COLUMN: {
            log_debug("[CSS] Processing grid-column shorthand property");
            alloc_grid_item_prop(lycon, span);
            // grid-column: <start> / <end> or span <n> or <line>
            // Per CSS Grid spec: "span N" without "/" means "auto / span N"
            if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
                size_t count = value->data.list.count;
                CssValue** values = value->data.list.values;
                bool has_separator = false;

                // First pass: check if there's a "/" separator
                // Note: "/" may come as STRING or CUSTOM type depending on parser
                for (size_t i = 0; i < count; i++) {
                    CssValue* v = values[i];
                    if (v->type == CSS_VALUE_TYPE_STRING && v->data.string && strcmp(v->data.string, "/") == 0) {
                        has_separator = true;
                        break;
                    }
                    if (v->type == CSS_VALUE_TYPE_CUSTOM && v->data.custom_property.name &&
                        strcmp(v->data.custom_property.name, "/") == 0) {
                        has_separator = true;
                        break;
                    }
                }

                if (!has_separator) {
                    // No separator: "span N" or just "N"
                    // Check if first value is span keyword
                    bool is_span = false;
                    int span_value = 1;
                    int line_value = 0;

                    for (size_t i = 0; i < count; i++) {
                        CssValue* v = values[i];
                        if (v->type == CSS_VALUE_TYPE_KEYWORD) {
                            const CssEnumInfo* info = css_enum_info(v->data.keyword);
                            if (info && info->name && strcmp(info->name, "span") == 0) {
                                is_span = true;
                            }
                        } else if (v->type == CSS_VALUE_TYPE_CUSTOM) {
                            // "span" may come as custom identifier
                            const char* name = v->data.custom_property.name;
                            if (name && strcmp(name, "span") == 0) {
                                is_span = true;
                            }
                        } else if (v->type == CSS_VALUE_TYPE_NUMBER) {
                            if (is_span) {
                                span_value = (int)v->data.number.value;
                            } else {
                                line_value = (int)v->data.number.value;
                            }
                        }
                    }                    if (is_span) {
                        // "span N" -> auto / span N
                        span->gi->grid_column_start = 0;  // auto
                        span->gi->grid_column_end = -span_value;  // negative for span
                        span->gi->has_explicit_grid_column_end = true;
                        span->gi->grid_column_end_is_span = true;
                    } else if (line_value != 0) {
                        // Just a line number
                        span->gi->grid_column_start = line_value;
                        span->gi->has_explicit_grid_column_start = true;
                    }
                } else {
                    // Has separator: <start> / <end>
                    int value_idx = 0;  // 0 = start, 1 = end
                    bool saw_span = false;

                    for (size_t i = 0; i < count; i++) {
                        CssValue* v = values[i];
                        if (v->type == CSS_VALUE_TYPE_NUMBER) {
                            int num = (int)v->data.number.value;
                            if (saw_span) {
                                if (value_idx == 0) {
                                    span->gi->grid_column_start = -num;
                                    span->gi->has_explicit_grid_column_start = true;
                                    span->gi->grid_column_start_is_span = true;
                                } else {
                                    span->gi->grid_column_end = -num;
                                    span->gi->grid_column_end_is_span = true;
                                    span->gi->has_explicit_grid_column_end = true;
                                }
                                saw_span = false;
                            } else {
                                if (value_idx == 0) {
                                    span->gi->grid_column_start = num;
                                    span->gi->has_explicit_grid_column_start = true;
                                } else {
                                    span->gi->grid_column_end = num;
                                    span->gi->has_explicit_grid_column_end = true;
                                }
                            }
                        } else if (v->type == CSS_VALUE_TYPE_KEYWORD) {
                            const CssEnumInfo* info = css_enum_info(v->data.keyword);
                            if (info && info->name && strcmp(info->name, "span") == 0) {
                                saw_span = true;
                            }
                        } else if (v->type == CSS_VALUE_TYPE_CUSTOM) {
                            const char* name = v->data.custom_property.name;
                            if (name && strcmp(name, "span") == 0) {
                                saw_span = true;
                            } else if (name && strcmp(name, "/") == 0) {
                                // "/" separator may come as CUSTOM type
                                value_idx = 1;
                                saw_span = false;
                            }
                        } else if (v->type == CSS_VALUE_TYPE_STRING) {
                            if (v->data.string && strcmp(v->data.string, "/") == 0) {
                                value_idx = 1;
                                saw_span = false;
                            }
                        }
                    }
                }
                span->gi->is_grid_auto_placed = false;
                log_debug("[CSS] grid-column: %d / %d (has_start=%d, has_end=%d, end_is_span=%d)",
                          span->gi->grid_column_start, span->gi->grid_column_end,
                          span->gi->has_explicit_grid_column_start, span->gi->has_explicit_grid_column_end,
                          span->gi->grid_column_end_is_span);
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                // Single line number
                int line = (int)value->data.number.value;
                span->gi->grid_column_start = line;
                span->gi->has_explicit_grid_column_start = true;
                span->gi->is_grid_auto_placed = false;
                log_debug("[CSS] grid-column: %d", line);
            }
            break;
        }

        case CSS_PROPERTY_GRID_ROW: {
            log_debug("[CSS] Processing grid-row shorthand property");
            alloc_grid_item_prop(lycon, span);
            // grid-row: <start> / <end> or span <n> or <line>
            // Per CSS Grid spec: "span N" without "/" means "auto / span N"
            if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
                size_t count = value->data.list.count;
                CssValue** values = value->data.list.values;
                bool has_separator = false;

                // First pass: check if there's a "/" separator
                // Note: "/" may come as STRING or CUSTOM type depending on parser
                for (size_t i = 0; i < count; i++) {
                    CssValue* v = values[i];
                    if (v->type == CSS_VALUE_TYPE_STRING && v->data.string && strcmp(v->data.string, "/") == 0) {
                        has_separator = true;
                        break;
                    }
                    if (v->type == CSS_VALUE_TYPE_CUSTOM && v->data.custom_property.name &&
                        strcmp(v->data.custom_property.name, "/") == 0) {
                        has_separator = true;
                        break;
                    }
                }

                if (!has_separator) {
                    // No separator: "span N" or just "N"
                    bool is_span = false;
                    int span_value = 1;
                    int line_value = 0;

                    for (size_t i = 0; i < count; i++) {
                        CssValue* v = values[i];
                        if (v->type == CSS_VALUE_TYPE_KEYWORD) {
                            const CssEnumInfo* info = css_enum_info(v->data.keyword);
                            if (info && info->name && strcmp(info->name, "span") == 0) {
                                is_span = true;
                            }
                        } else if (v->type == CSS_VALUE_TYPE_CUSTOM) {
                            const char* name = v->data.custom_property.name;
                            if (name && strcmp(name, "span") == 0) {
                                is_span = true;
                            }
                        } else if (v->type == CSS_VALUE_TYPE_NUMBER) {
                            if (is_span) {
                                span_value = (int)v->data.number.value;
                            } else {
                                line_value = (int)v->data.number.value;
                            }
                        }
                    }

                    if (is_span) {
                        // "span N" -> auto / span N
                        span->gi->grid_row_start = 0;  // auto
                        span->gi->grid_row_end = -span_value;  // negative for span
                        span->gi->has_explicit_grid_row_end = true;
                        span->gi->grid_row_end_is_span = true;
                    } else if (line_value != 0) {
                        span->gi->grid_row_start = line_value;
                        span->gi->has_explicit_grid_row_start = true;
                    }
                } else {
                    // Has separator: <start> / <end>
                    int value_idx = 0;
                    bool saw_span = false;

                    for (size_t i = 0; i < count; i++) {
                        CssValue* v = values[i];
                        if (v->type == CSS_VALUE_TYPE_NUMBER) {
                            int num = (int)v->data.number.value;
                            if (saw_span) {
                                if (value_idx == 0) {
                                    span->gi->grid_row_start = -num;
                                    span->gi->has_explicit_grid_row_start = true;
                                    span->gi->grid_row_start_is_span = true;
                                } else {
                                    span->gi->grid_row_end = -num;
                                    span->gi->has_explicit_grid_row_end = true;
                                    span->gi->grid_row_end_is_span = true;
                                }
                                saw_span = false;
                            } else {
                                if (value_idx == 0) {
                                    span->gi->grid_row_start = num;
                                    span->gi->has_explicit_grid_row_start = true;
                                } else {
                                    span->gi->grid_row_end = num;
                                    span->gi->has_explicit_grid_row_end = true;
                                }
                            }
                        } else if (v->type == CSS_VALUE_TYPE_KEYWORD) {
                            const CssEnumInfo* info = css_enum_info(v->data.keyword);
                            if (info && info->name && strcmp(info->name, "span") == 0) {
                                saw_span = true;
                            }
                        } else if (v->type == CSS_VALUE_TYPE_CUSTOM) {
                            const char* name = v->data.custom_property.name;
                            if (name && strcmp(name, "span") == 0) {
                                saw_span = true;
                            } else if (name && strcmp(name, "/") == 0) {
                                // "/" separator may come as CUSTOM type
                                value_idx = 1;
                                saw_span = false;
                            }
                        } else if (v->type == CSS_VALUE_TYPE_STRING) {
                            if (v->data.string && strcmp(v->data.string, "/") == 0) {
                                value_idx = 1;
                                saw_span = false;
                            }
                        }
                    }
                }
                span->gi->is_grid_auto_placed = false;
                log_debug("[CSS] grid-row: %d / %d",
                          span->gi->grid_row_start, span->gi->grid_row_end);
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                int line = (int)value->data.number.value;
                span->gi->grid_row_start = line;
                span->gi->has_explicit_grid_row_start = true;
                span->gi->is_grid_auto_placed = false;
                log_debug("[CSS] grid-row: %d", line);
            }
            break;
        }

        case CSS_PROPERTY_GRID_AUTO_FLOW: {
            log_debug("[CSS] Processing grid-auto-flow property");
            if (!block) {
                log_debug("[CSS] grid-auto-flow: Cannot apply to non-block element");
                break;
            }
            alloc_grid_prop(lycon, block);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum flow = value->data.keyword;
                block->embed->grid->grid_auto_flow = flow;
                log_debug("[CSS] grid-auto-flow: %s", css_enum_info(flow)->name);
            }
            break;
        }

        case CSS_PROPERTY_GRID_AUTO_ROWS: {
            log_debug("[CSS] Processing grid-auto-rows property");
            if (!block) {
                log_debug("[CSS] grid-auto-rows: Cannot apply to non-block element");
                break;
            }
            alloc_grid_prop(lycon, block);
            GridProp* grid = block->embed->grid;

            // Handle "auto" keyword
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_AUTO) {
                log_debug("[CSS] grid-auto-rows: auto");
                // Auto means content-based sizing - clear any explicit auto tracks
                if (grid->grid_auto_rows) {
                    destroy_grid_track_list(grid->grid_auto_rows);
                    grid->grid_auto_rows = NULL;
                }
                break;
            }

            // Handle single length/fr value (e.g., "100px" or "1fr")
            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                if (!grid->grid_auto_rows) {
                    grid->grid_auto_rows = create_grid_track_list(1);
                }
                // Use parse_css_value_to_track_size to properly handle fr units
                GridTrackSize* track_size = parse_css_value_to_track_size(value);
                if (track_size) {
                    grid->grid_auto_rows->tracks[0] = track_size;
                    grid->grid_auto_rows->track_count = 1;
                    log_debug("[CSS] grid-auto-rows: single track size set (type=%d, value=%d)",
                              track_size->type, track_size->value);
                }
                break;
            }

            // Handle list of track sizes
            if (value->type == CSS_VALUE_TYPE_LIST) {
                log_debug("[CSS] grid-auto-rows: using parse_grid_track_list helper");
                parse_grid_track_list(value, &grid->grid_auto_rows);
                log_debug("[CSS] grid-auto-rows: %d tracks parsed",
                          grid->grid_auto_rows ? grid->grid_auto_rows->track_count : 0);
            }
            break;
        }

        case CSS_PROPERTY_GRID_AUTO_COLUMNS: {
            log_debug("[CSS] Processing grid-auto-columns property");
            if (!block) {
                log_debug("[CSS] grid-auto-columns: Cannot apply to non-block element");
                break;
            }
            alloc_grid_prop(lycon, block);
            GridProp* grid = block->embed->grid;

            // Handle "auto" keyword
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_AUTO) {
                log_debug("[CSS] grid-auto-columns: auto");
                // Auto means content-based sizing - clear any explicit auto tracks
                if (grid->grid_auto_columns) {
                    destroy_grid_track_list(grid->grid_auto_columns);
                    grid->grid_auto_columns = NULL;
                }
                break;
            }

            // Handle single length/fr value (e.g., "100px" or "1fr")
            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                if (!grid->grid_auto_columns) {
                    grid->grid_auto_columns = create_grid_track_list(1);
                }
                // Use parse_css_value_to_track_size to properly handle fr units
                GridTrackSize* track_size = parse_css_value_to_track_size(value);
                if (track_size) {
                    grid->grid_auto_columns->tracks[0] = track_size;
                    grid->grid_auto_columns->track_count = 1;
                    log_debug("[CSS] grid-auto-columns: single track size set (type=%d, value=%d)",
                              track_size->type, track_size->value);
                }
                break;
            }

            // Handle list of track sizes
            if (value->type == CSS_VALUE_TYPE_LIST) {
                log_debug("[CSS] grid-auto-columns: using parse_grid_track_list helper");
                parse_grid_track_list(value, &grid->grid_auto_columns);
                log_debug("[CSS] grid-auto-columns: %d tracks parsed",
                          grid->grid_auto_columns ? grid->grid_auto_columns->track_count : 0);
            }
            break;
        }

        case CSS_PROPERTY_JUSTIFY_ITEMS: {
            log_debug("[CSS] Processing justify-items property");
            if (!block) {
                log_debug("[CSS] justify-items: Cannot apply to non-block element");
                break;
            }
            alloc_grid_prop(lycon, block);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum align = value->data.keyword;
                block->embed->grid->justify_items = align;
                log_debug("[CSS] justify-items: %s", css_enum_info(align)->name);
            }
            break;
        }

        case CSS_PROPERTY_JUSTIFY_SELF: {
            log_debug("[CSS] Processing justify-self property");
            alloc_grid_item_prop(lycon, span);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum align = value->data.keyword;
                span->gi->justify_self = align;
                log_debug("[CSS] justify-self: %s", css_enum_info(align)->name);
            }
            break;
        }

        case CSS_PROPERTY_PLACE_ITEMS: {
            // place-items is a shorthand for align-items and justify-items
            // Syntax: place-items: <align-items> <justify-items>?
            // If only one value, it applies to both
            log_debug("[CSS] Processing place-items shorthand property");
            if (!block) {
                log_debug("[CSS] place-items: Cannot apply to non-block element");
                break;
            }

            alloc_grid_prop(lycon, block);
            alloc_flex_prop(lycon, block);

            CssEnum align_val = CSS_VALUE_STRETCH;
            CssEnum justify_val = CSS_VALUE_STRETCH;

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // Single value applies to both
                align_val = value->data.keyword;
                justify_val = value->data.keyword;
            } else if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count >= 1) {
                // First value is align-items
                if (value->data.list.values[0]->type == CSS_VALUE_TYPE_KEYWORD) {
                    align_val = value->data.list.values[0]->data.keyword;
                }
                // Second value (if present) is justify-items
                if (value->data.list.count >= 2 && value->data.list.values[1]->type == CSS_VALUE_TYPE_KEYWORD) {
                    justify_val = value->data.list.values[1]->data.keyword;
                } else {
                    justify_val = align_val;  // If no second value, same as first
                }
            }

            // Apply to grid
            block->embed->grid->align_items = align_val;
            block->embed->grid->justify_items = justify_val;
            // Also apply to flex
            block->embed->flex->align_items = align_val;

            log_debug("[CSS] place-items: align=%s, justify=%s",
                      css_enum_info(align_val)->name, css_enum_info(justify_val)->name);
            break;
        }

        case CSS_PROPERTY_PLACE_SELF: {
            // place-self is a shorthand for align-self and justify-self
            // Syntax: place-self: <align-self> <justify-self>?
            // If only one value, it applies to both
            log_debug("[CSS] Processing place-self shorthand property");

            CssEnum align_val = CSS_VALUE_AUTO;
            CssEnum justify_val = CSS_VALUE_AUTO;

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // Single value applies to both
                align_val = value->data.keyword;
                justify_val = value->data.keyword;
            } else if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count >= 1) {
                // First value is align-self
                if (value->data.list.values[0]->type == CSS_VALUE_TYPE_KEYWORD) {
                    align_val = value->data.list.values[0]->data.keyword;
                }
                // Second value (if present) is justify-self
                if (value->data.list.count >= 2 && value->data.list.values[1]->type == CSS_VALUE_TYPE_KEYWORD) {
                    justify_val = value->data.list.values[1]->data.keyword;
                } else {
                    justify_val = align_val;  // If no second value, same as first
                }
            }

            // Set align-self based on item type
            if (span->item_prop_type == DomElement::ITEM_PROP_GRID) {
                span->gi->align_self_grid = align_val;
                span->gi->justify_self = justify_val;
            } else if (span->item_prop_type == DomElement::ITEM_PROP_FLEX) {
                span->fi->align_self = align_val;
                // Note: justify-self doesn't apply to flex items in the main axis
            } else {
                // Neither allocated yet - allocate grid prop (for grid items)
                // or flex prop (for flex items). Default to grid since place-self
                // is primarily used with grid.
                alloc_grid_item_prop(lycon, span);
                span->gi->align_self_grid = align_val;
                span->gi->justify_self = justify_val;
            }

            log_debug("[CSS] place-self: align=%s, justify=%s (type=%d)",
                      css_enum_info(align_val)->name, css_enum_info(justify_val)->name, span->item_prop_type);
            break;
        }

        case CSS_PROPERTY_FLEX_GROW: {
            log_debug("[CSS] Processing flex-grow property");
            if (value->type == CSS_VALUE_TYPE_NUMBER) {
                float grow_value = (float)value->data.number.value;
                // Form controls store flex props in FormControlProp
                if (span->item_prop_type == DomElement::ITEM_PROP_FORM && span->form) {
                    span->form->flex_grow = grow_value;
                } else {
                    alloc_flex_item_prop(lycon, span);
                    span->fi->flex_grow = grow_value;
                }
                log_debug("[CSS] flex-grow: %.2f", grow_value);
            }
            break;
        }

        case CSS_PROPERTY_FLEX_SHRINK: {
            log_debug("[CSS] Processing flex-shrink property");
            if (value->type == CSS_VALUE_TYPE_NUMBER) {
                float shrink_value = (float)value->data.number.value;
                // Form controls store flex props in FormControlProp
                if (span->item_prop_type == DomElement::ITEM_PROP_FORM && span->form) {
                    span->form->flex_shrink = shrink_value;
                } else {
                    alloc_flex_item_prop(lycon, span);
                    span->fi->flex_shrink = shrink_value;
                }
                log_debug("[CSS] flex-shrink: %.2f", shrink_value);
            }
            break;
        }

        case CSS_PROPERTY_FLEX_BASIS: {
            log_debug("[CSS] Processing flex-basis property");
            // Form controls store flex props in FormControlProp
            bool is_form = (span->item_prop_type == DomElement::ITEM_PROP_FORM && span->form);
            if (!is_form) {
                alloc_flex_item_prop(lycon, span);
            }
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_AUTO) {
                if (is_form) {
                    span->form->flex_basis = -1;
                    span->form->flex_basis_is_percent = false;
                } else {
                    span->fi->flex_basis = -1; // -1 indicates auto
                    span->fi->flex_basis_is_percent = false;
                }
                log_debug("[CSS] flex-basis: auto");
            } else if (value->type == CSS_VALUE_TYPE_LENGTH) {
                float basis_value = resolve_length_value(lycon, prop_id, value);
                if (is_form) {
                    span->form->flex_basis = (int)basis_value;
                    span->form->flex_basis_is_percent = false;
                } else {
                    span->fi->flex_basis = (int)basis_value;
                    span->fi->flex_basis_is_percent = false;
                }
                log_debug("[CSS] flex-basis: %.2fpx", basis_value);
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                // DEBUG: log raw percentage value to diagnose parsing issue
                log_debug("[CSS DEBUG] flex-basis percentage raw: %f", value->data.percentage.value);
                if (is_form) {
                    span->form->flex_basis = (float)value->data.percentage.value;
                    span->form->flex_basis_is_percent = true;
                } else {
                    span->fi->flex_basis = (float)value->data.percentage.value;
                    span->fi->flex_basis_is_percent = true;
                }
                log_debug("[CSS] flex-basis: %.1f%% (stored as %.1f)", value->data.percentage.value,
                    is_form ? span->form->flex_basis : span->fi->flex_basis);
            }
            break;
        }

        case CSS_PROPERTY_ORDER: {
            log_debug("[CSS] Processing order property");
            if (value->type == CSS_VALUE_TYPE_NUMBER) {
                int order_value = (int)value->data.number.value;
                // order applies to both flex and grid items
                // fi and gi share a union - check which type is allocated
                if (span->item_prop_type == DomElement::ITEM_PROP_GRID) {
                    span->gi->order = order_value;
                } else if (span->item_prop_type == DomElement::ITEM_PROP_FLEX) {
                    span->fi->order = order_value;
                } else {
                    // Neither allocated yet - allocate flex prop (default)
                    alloc_flex_item_prop(lycon, span);
                    span->fi->order = order_value;
                }
                log_debug("[CSS] order: %d (type=%d)", order_value, span->item_prop_type);
            }
            break;
        }

        case CSS_PROPERTY_ALIGN_SELF: {
            log_debug("[CSS] Processing align-self property");
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum val = value->data.keyword;
                if (val > 0) {
                    // align-self applies to both flex and grid items
                    // fi and gi share a union - check which type is allocated
                    if (span->item_prop_type == DomElement::ITEM_PROP_GRID) {
                        // Grid item - set align_self_grid
                        span->gi->align_self_grid = val;
                    } else if (span->item_prop_type == DomElement::ITEM_PROP_FLEX) {
                        // Flex item - set align_self
                        span->fi->align_self = val;
                    } else {
                        // Neither allocated yet - allocate flex prop (more common case)
                        // Grid items will have gi allocated first by init_grid_item_view
                        alloc_flex_item_prop(lycon, span);
                        span->fi->align_self = val;
                    }
                    log_debug("[CSS] align-self: %s -> 0x%04X (type=%d)",
                              css_enum_info(value->data.keyword)->name, val, span->item_prop_type);
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
            // Check if this is a form control - they store flex props in FormControlProp
            bool is_form = (span->item_prop_type == DomElement::ITEM_PROP_FORM && span->form);
            if (!is_form) {
                alloc_flex_item_prop(lycon, span);
            }
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

                if (is_form) {
                    span->form->flex_grow = flex_grow;
                    span->form->flex_shrink = flex_shrink;
                    span->form->flex_basis = flex_basis;
                    span->form->flex_basis_is_percent = flex_basis_is_percent;
                } else {
                    span->fi->flex_grow = flex_grow;
                    span->fi->flex_shrink = flex_shrink;
                    span->fi->flex_basis = flex_basis;
                    span->fi->flex_basis_is_percent = flex_basis_is_percent;
                }
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
                        // Numbers are grow and shrink (unitless), except:
                        // - Third number is flex-basis (unitless 0 is valid for lengths)
                        // - If the number is 0 and we already have grow+shrink, it's basis=0
                        if (value_index == 0) {
                            flex_grow = (float)val->data.number.value;
                            log_debug("[CSS]   flex-grow: %.2f", flex_grow);
                            value_index++;
                        } else if (value_index == 1) {
                            flex_shrink = (float)val->data.number.value;
                            log_debug("[CSS]   flex-shrink: %.2f", flex_shrink);
                            value_index++;
                        } else if (value_index == 2 && val->data.number.value == 0) {
                            // Third value is unitless 0 -> flex-basis: 0
                            // CSS allows unitless 0 for any length value
                            flex_basis = 0;
                            flex_basis_is_percent = false;
                            found_basis = true;
                            log_debug("[CSS]   flex-basis: 0 (unitless zero)");
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

                if (is_form) {
                    span->form->flex_grow = flex_grow;
                    span->form->flex_shrink = flex_shrink;
                    span->form->flex_basis = flex_basis;
                    span->form->flex_basis_is_percent = flex_basis_is_percent;
                } else {
                    span->fi->flex_grow = flex_grow;
                    span->fi->flex_shrink = flex_shrink;
                    span->fi->flex_basis = flex_basis;
                    span->fi->flex_basis_is_percent = flex_basis_is_percent;
                }

                log_debug("[CSS] flex shorthand resolved: grow=%.2f shrink=%.2f basis=%.2f%s",
                         flex_grow, flex_shrink, flex_basis,
                         flex_basis_is_percent ? "%" : (flex_basis == -1 ? " (auto)" : "px"));
            }
            else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                // Single number without list wrapper: just flex-grow
                flex_grow = (float)value->data.number.value;
                flex_shrink = 1.0f;
                flex_basis = 0;  // 0px when single unitless number

                if (is_form) {
                    span->form->flex_grow = flex_grow;
                    span->form->flex_shrink = flex_shrink;
                    span->form->flex_basis = flex_basis;
                    span->form->flex_basis_is_percent = false;
                } else {
                    span->fi->flex_grow = flex_grow;
                    span->fi->flex_shrink = flex_shrink;
                    span->fi->flex_basis = flex_basis;
                    span->fi->flex_basis_is_percent = false;
                }
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
                double spacing = resolve_length_value(lycon, prop_id, value);
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
                if (!block->blk) {
                    block->blk = alloc_block_prop(lycon);
                }
                CssEnum type = value->data.keyword;
                block->blk->list_style_type = type;
                if (type > 0) {
                    const CssEnumInfo* info = css_enum_info(type);
                    log_debug("[CSS] list-style-type: %s -> 0x%04X (stored)", info ? info->name : "unknown", type);
                } else {
                    const CssEnumInfo* info = css_enum_info(type);
                    log_debug("[CSS] list-style-type: %s (stored)", info ? info->name : "unknown");
                }
            }
            break;
        }

        case CSS_PROPERTY_LIST_STYLE_POSITION: {
            log_debug("[CSS] Processing list-style-position property");
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                if (!block->blk) {
                    block->blk = alloc_block_prop(lycon);
                }
                CssEnum position = value->data.keyword;
                block->blk->list_style_position = position;
                if (position > 0) {
                    const CssEnumInfo* info = css_enum_info(position);
                    log_debug("[CSS] list-style-position: %s -> 0x%04X (stored)", info ? info->name : "unknown", position);
                } else {
                    const CssEnumInfo* info = css_enum_info(position);
                    log_debug("[CSS] list-style-position: %s (stored)", info ? info->name : "unknown");
                }
            }
            break;
        }

        case CSS_PROPERTY_LIST_STYLE_IMAGE: {
            log_debug("[CSS] Processing list-style-image property");
            if (!block->blk) {
                block->blk = alloc_block_prop(lycon);
            }
            if (value->type == CSS_VALUE_TYPE_URL) {
                const char* url = value->data.url;
                if (url) {
                    size_t len = strlen(url);
                    block->blk->list_style_image = (char*)alloc_prop(lycon, len + 1);
                    str_copy(block->blk->list_style_image, len + 1, url, len);
                    log_debug("[CSS] list-style-image: %s (stored)", url);
                }
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                if (value->data.keyword == CSS_VALUE_NONE) {
                    block->blk->list_style_image = (char*)alloc_prop(lycon, 5);
                    str_copy(block->blk->list_style_image, 5, "none", 4);
                    log_debug("[CSS] list-style-image: none (stored)");
                } else {
                    const CssEnumInfo* info = css_enum_info(value->data.keyword);
                    log_debug("[CSS] list-style-image: %s", info ? info->name : "unknown");
                }
            }
            break;
        }

        case CSS_PROPERTY_LIST_STYLE: {
            // CSS 2.1 Section 12.5.1: list-style shorthand
            // Syntax: list-style: [ <list-style-type> || <list-style-position> || <list-style-image> ] | inherit
            log_debug("[CSS] Processing list-style shorthand property, value_type=%d", (int)value->type);

            if (!block->blk) {
                block->blk = alloc_block_prop(lycon);
            }

            // Handle single keyword value (most common case)
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum keyword = value->data.keyword;
                const CssEnumInfo* info = css_enum_info(keyword);
                log_debug("[CSS] list-style keyword: %s (0x%04X)", info ? info->name : "unknown", keyword);

                // Check if it's a list-style-position value (inside or outside)
                // Use string comparison since these are hash-based enums
                bool is_position = false;
                if (info && info->name) {
                    if (strcmp(info->name, "inside") == 0 || strcmp(info->name, "outside") == 0) {
                        block->blk->list_style_position = keyword;
                        log_debug("[CSS] list-style: expanded to list-style-position=%s", info->name);
                        is_position = true;
                    }
                }

                // Check if it's a list-style-type value (disc, circle, square, decimal, etc.)
                // These are in the 0x017D-0x0190 range approximately
                if (!is_position && keyword >= CSS_VALUE_DISC && keyword <= 0x0190) {
                    block->blk->list_style_type = keyword;
                    log_debug("[CSS] list-style: expanded to list-style-type=%s", info ? info->name : "unknown");
                }
                // Check if it's none (could be list-style-type: none or list-style-image: none)
                else if (!is_position && keyword == CSS_VALUE_NONE) {
                    block->blk->list_style_type = CSS_VALUE_NONE;
                    block->blk->list_style_image = (char*)alloc_prop(lycon, 5);
                    str_copy(block->blk->list_style_image, 5, "none", 4);
                    log_debug("[CSS] list-style: expanded to list-style-type=none, list-style-image=none");
                }
                // Otherwise might be other keyword
                else if (!is_position) {
                    log_debug("[CSS] list-style: keyword 0x%04X not recognized", keyword);
                }
            }
            // Handle custom property reference (which might be misidentified keywords like "inside")
            else if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
                // Check if it's actually a keyword like "inside" or "outside"
                const char* name = value->data.custom_property.name;
                log_debug("[CSS] list-style: checking custom value '%s'", name);

                if (strcmp(name, "inside") == 0) {
                    // "inside" keyword - set position to inside
                    block->blk->list_style_position = (CssEnum)1;  // 1 = inside
                    // CSS 2.1: Initial value for list-style-type is 'disc'
                    // If only position is specified, use default disc marker
                    if (block->blk->list_style_type == 0) {
                        block->blk->list_style_type = CSS_VALUE_DISC;
                        log_debug("[CSS] list-style: using default list-style-type=disc");
                    }
                    log_debug("[CSS] list-style: expanded to list-style-position=inside");
                } else if (strcmp(name, "outside") == 0) {
                    // "outside" is default, but set explicitly
                    block->blk->list_style_position = (CssEnum)2;  // 2 = outside
                    // Use default disc marker if type not set
                    if (block->blk->list_style_type == 0) {
                        block->blk->list_style_type = CSS_VALUE_DISC;
                        log_debug("[CSS] list-style: using default list-style-type=disc");
                    }
                    log_debug("[CSS] list-style: expanded to list-style-position=outside");
                } else {
                    log_debug("[CSS] list-style: unrecognized custom value '%s'", name);
                }
            }
            // Handle URL for list-style-image
            else if (value->type == CSS_VALUE_TYPE_URL) {
                const char* url = value->data.url;
                if (url) {
                    size_t len = strlen(url);
                    block->blk->list_style_image = (char*)alloc_prop(lycon, len + 1);
                    str_copy(block->blk->list_style_image, len + 1, url, len);
                    log_debug("[CSS] list-style: expanded to list-style-image=%s", url);
                }
            }
            // Handle multiple values (e.g., "square inside", "disc outside")
            else if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
                log_debug("[CSS] list-style: processing %d values", value->data.list.count);

                // Iterate through all values in the list
                for (int i = 0; i < value->data.list.count; i++) {
                    CssValue* item = value->data.list.values[i];
                    if (!item) continue;

                    if (item->type == CSS_VALUE_TYPE_KEYWORD) {
                        CssEnum keyword = item->data.keyword;
                        const CssEnumInfo* info = css_enum_info(keyword);

                        // Check if it's a position keyword
                        bool is_position = false;
                        if (info && info->name) {
                            if (strcmp(info->name, "inside") == 0 || strcmp(info->name, "outside") == 0) {
                                block->blk->list_style_position = keyword;
                                log_debug("[CSS] list-style: expanded to list-style-position=%s", info->name);
                                is_position = true;
                            }
                        }

                        // Check if it's a list-style-type keyword
                        if (!is_position && keyword >= CSS_VALUE_DISC && keyword <= 0x0190) {
                            block->blk->list_style_type = keyword;
                            log_debug("[CSS] list-style: expanded to list-style-type=%s", info ? info->name : "unknown");
                        }
                        else if (!is_position && keyword == CSS_VALUE_NONE) {
                            block->blk->list_style_type = CSS_VALUE_NONE;
                            log_debug("[CSS] list-style: set list-style-type=none");
                        }
                    }
                    else if (item->type == CSS_VALUE_TYPE_CUSTOM && item->data.custom_property.name) {
                        // Handle "inside"/"outside" that might be parsed as custom
                        const char* name = item->data.custom_property.name;
                        if (strcmp(name, "inside") == 0) {
                            block->blk->list_style_position = (CssEnum)1;
                            log_debug("[CSS] list-style: expanded to list-style-position=inside");
                        } else if (strcmp(name, "outside") == 0) {
                            block->blk->list_style_position = (CssEnum)2;
                            log_debug("[CSS] list-style: expanded to list-style-position=outside");
                        }
                    }
                    else if (item->type == CSS_VALUE_TYPE_URL) {
                        const char* url = item->data.url;
                        if (url) {
                            size_t len = strlen(url);
                            block->blk->list_style_image = (char*)alloc_prop(lycon, len + 1);
                            str_copy(block->blk->list_style_image, len + 1, url, len);
                            log_debug("[CSS] list-style: expanded to list-style-image=%s", url);
                        }
                    }
                }
            }
            break;
        }

        case CSS_PROPERTY_COUNTER_RESET: {
            // CSS 2.1 Section 12.4: counter-reset property
            // Syntax: counter-reset: [ <identifier> <integer>? ]+ | none
            log_debug("[CSS] counter-reset value type=%d", (int)value->type);

            if (!block->blk) {
                block->blk = alloc_block_prop(lycon);
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                block->blk->counter_reset = (char*)alloc_prop(lycon, 5);
                str_copy(block->blk->counter_reset, 5, "none", 4);
                log_debug("[CSS] counter-reset: none");
            } else if (value->type == CSS_VALUE_TYPE_STRING || value->type == CSS_VALUE_TYPE_CUSTOM) {
                // Direct string value (parsed by CSS parser) or custom property name (identifier)
                const char* str = (value->type == CSS_VALUE_TYPE_STRING) ? value->data.string : value->data.custom_property.name;
                if (str) {
                    size_t len = strlen(str);
                    block->blk->counter_reset = (char*)alloc_prop(lycon, len + 1);
                    str_copy(block->blk->counter_reset, len + 1, str, len);
                    log_debug("[CSS] counter-reset: %s", str);
                }
            } else if (value->type == CSS_VALUE_TYPE_LIST) {
                // Parse list of name-value pairs
                log_debug("[CSS] counter-reset list with %d items", value->data.list.count);
                StringBuf* sb = stringbuf_new(lycon->pool);

                if (!sb) {
                    log_error("[CSS] counter-reset: stringbuf_new failed!");
                    break;
                }

                int count = value->data.list.count;
                CssValue** values = value->data.list.values;
                for (int i = 0; i < count; i++) {
                    CssValue* item = values[i];
                    if (item->type == CSS_VALUE_TYPE_KEYWORD) {
                        const CssEnumInfo* info = css_enum_info(item->data.keyword);
                        if (info) {
                            if (sb->length > 0) stringbuf_append_char(sb, ' ');
                            stringbuf_append_str(sb, info->name);
                        }
                    } else if (item->type == CSS_VALUE_TYPE_NUMBER && item->data.number.is_integer) {
                        if (sb->length > 0) stringbuf_append_char(sb, ' ');
                        stringbuf_append_int(sb, (int)item->data.number.value);
                    }
                }

                if (sb->length > 0) {
                    block->blk->counter_reset = (char*)alloc_prop(lycon, sb->length + 1);
                    str_copy(block->blk->counter_reset, sb->length + 1, sb->str->chars, sb->length);
                    log_debug("[CSS] counter-reset: %s", sb->str->chars);
                }
                stringbuf_free(sb);
            }
            break;
        }

        case CSS_PROPERTY_COUNTER_INCREMENT: {
            // CSS 2.1 Section 12.4: counter-increment property
            // Syntax: counter-increment: [ <identifier> <integer>? ]+ | none
            log_debug("[CSS] counter-increment: entry, value type=%d", (int)value->type);

            if (!block->blk) {
                block->blk = alloc_block_prop(lycon);
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                block->blk->counter_increment = (char*)alloc_prop(lycon, 5);
                str_copy(block->blk->counter_increment, 5, "none", 4);
                log_debug("[CSS] counter-increment: none");
            } else if (value->type == CSS_VALUE_TYPE_STRING || value->type == CSS_VALUE_TYPE_CUSTOM) {
                // Direct string value (parsed by CSS parser) or custom property name (identifier)
                const char* str = (value->type == CSS_VALUE_TYPE_STRING) ? value->data.string : value->data.custom_property.name;
                if (str) {
                    size_t len = strlen(str);
                    block->blk->counter_increment = (char*)alloc_prop(lycon, len + 1);
                    str_copy(block->blk->counter_increment, len + 1, str, len);
                    log_debug("[CSS] counter-increment: %s", str);
                }
            } else if (value->type == CSS_VALUE_TYPE_LIST) {
                // Parse list of name-value pairs
                log_debug("[CSS] counter-increment: LIST case entered, value ptr=%p", (void*)value);
                log_debug("[CSS] counter-increment: about to access list.count");
                int count = value->data.list.count;
                log_debug("[CSS] counter-increment: list.count=%d", count);
                log_debug("[CSS] counter-increment: about to call stringbuf_new");
                StringBuf* sb = stringbuf_new(lycon->pool);
                log_debug("[CSS] counter-increment: stringbuf created at %p", (void*)sb);

                if (!sb) {
                    log_error("[CSS] counter-increment: stringbuf_new failed!");
                    break;
                }

                CssValue** values = value->data.list.values;
                log_debug("[CSS] counter-increment: about to iterate list");
                for (int i = 0; i < count; i++) {
                    log_debug("[CSS] counter-increment: processing item %d", i);
                    CssValue* item = values[i];
                    if (item->type == CSS_VALUE_TYPE_KEYWORD) {
                        const CssEnumInfo* info = css_enum_info(item->data.keyword);
                        if (info) {
                            if (sb->length > 0) stringbuf_append_char(sb, ' ');
                            stringbuf_append_str(sb, info->name);
                        }
                    } else if (item->type == CSS_VALUE_TYPE_NUMBER && item->data.number.is_integer) {
                        if (sb->length > 0) stringbuf_append_char(sb, ' ');
                        stringbuf_append_int(sb, (int)item->data.number.value);
                    }
                }

                if (sb->length > 0) {
                    block->blk->counter_increment = (char*)alloc_prop(lycon, sb->length + 1);
                    str_copy(block->blk->counter_increment, sb->length + 1, sb->str->chars, sb->length);
                    log_debug("[CSS] counter-increment: %s", sb->str->chars);
                }
                stringbuf_free(sb);
            }
            break;
        }

        case CSS_PROPERTY_CONTENT: {
            // CSS 2.1 Section 12.2: content property for ::before and ::after
            log_debug("[CSS] Processing content property for pseudo-elements");

            if (!block->pseudo) {
                block->pseudo = (PseudoContentProp*)alloc_prop(lycon, sizeof(PseudoContentProp));
                memset(block->pseudo, 0, sizeof(PseudoContentProp));
            }

            // Determine if this is ::before or ::after based on decl context
            // For now, we'll check the selector context (TODO: improve this)
            bool is_before = false;  // Will be determined by selector parsing
            bool is_after = false;

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                if (value->data.keyword == CSS_VALUE_NONE ||
                    value->data.keyword == CSS_VALUE_NORMAL) {
                    // No content generated
                    log_debug("[CSS] content: none/normal");
                    if (is_before) {
                        block->pseudo->before_content_type = CONTENT_TYPE_NONE;
                    } else if (is_after) {
                        block->pseudo->after_content_type = CONTENT_TYPE_NONE;
                    }
                }
            } else if (value->type == CSS_VALUE_TYPE_STRING) {
                // String literal content
                const char* str = value->data.string;
                log_debug("[CSS] content: \"%s\"", str ? str : "");

                if (str) {
                    // Allocate and store content string
                    size_t len = strlen(str);
                    char* content_copy = (char*)alloc_prop(lycon, len + 1);
                    str_copy(content_copy, len + 1, str, len);

                    if (is_before) {
                        block->pseudo->before_content = content_copy;
                        block->pseudo->before_content_type = CONTENT_TYPE_STRING;
                    } else if (is_after) {
                        block->pseudo->after_content = content_copy;
                        block->pseudo->after_content_type = CONTENT_TYPE_STRING;
                    }
                }
            } else if (value->type == CSS_VALUE_TYPE_FUNCTION) {
                // Handle counter(), counters(), attr(), url()
                CssFunction* func = value->data.function;
                if (func && func->name) {
                    log_debug("[CSS] content function: %s", func->name);

                    if (strcmp(func->name, "counter") == 0) {
                        // counter(name) or counter(name, style)
                        block->pseudo->before_content_type = is_before ? CONTENT_TYPE_COUNTER : block->pseudo->before_content_type;
                        block->pseudo->after_content_type = is_after ? CONTENT_TYPE_COUNTER : block->pseudo->after_content_type;
                        // TODO: Parse counter name and style
                    } else if (strcmp(func->name, "counters") == 0) {
                        // counters(name, separator) or counters(name, separator, style)
                        block->pseudo->before_content_type = is_before ? CONTENT_TYPE_COUNTERS : block->pseudo->before_content_type;
                        block->pseudo->after_content_type = is_after ? CONTENT_TYPE_COUNTERS : block->pseudo->after_content_type;
                        // TODO: Parse counter name, separator, and style
                    } else if (strcmp(func->name, "attr") == 0) {
                        // attr(attribute-name)
                        block->pseudo->before_content_type = is_before ? CONTENT_TYPE_ATTR : block->pseudo->before_content_type;
                        block->pseudo->after_content_type = is_after ? CONTENT_TYPE_ATTR : block->pseudo->after_content_type;
                        // TODO: Parse attribute name
                    } else if (strcmp(func->name, "url") == 0) {
                        // url(image)
                        block->pseudo->before_content_type = is_before ? CONTENT_TYPE_URI : block->pseudo->before_content_type;
                        block->pseudo->after_content_type = is_after ? CONTENT_TYPE_URI : block->pseudo->after_content_type;
                        // TODO: Parse URL
                    }
                }
            } else if (value->type == CSS_VALUE_TYPE_LIST) {
                // Multiple content values (concatenated)
                log_debug("[CSS] content: list with %d values", value->data.list.count);
                // TODO: Handle concatenated content values
            }
            break;
        }

       case CSS_PROPERTY_BACKGROUND: {
            // background shorthand can set background-color, background-image, etc.

            // Handle multiple background layers (comma-separated list)
            // CSS stacks backgrounds bottom-to-top, so last item is base layer
            if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 1) {
                log_debug("[Lambda CSS Background] Multiple background layers: %d", value->data.list.count);
                CssValue** layers = value->data.list.values;
                int count = value->data.list.count;

                // Ensure background prop exists
                if (!span->bound) {
                    span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
                }
                if (!span->bound->background) {
                    span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
                }
                BackgroundProp* bg = span->bound->background;

                // First, look for a solid color in the last layer (base background)
                CssValue* last_layer = layers[count - 1];
                if (last_layer) {
                    if (last_layer->type == CSS_VALUE_TYPE_COLOR ||
                        last_layer->type == CSS_VALUE_TYPE_KEYWORD ||
                        (last_layer->type == CSS_VALUE_TYPE_FUNCTION && last_layer->data.function &&
                         last_layer->data.function->name &&
                         (str_ieq_const(last_layer->data.function->name, strlen(last_layer->data.function->name), "rgb") ||
                          str_ieq_const(last_layer->data.function->name, strlen(last_layer->data.function->name), "rgba")))) {
                        // Set base background color
                        bg->color = resolve_color_value(lycon, last_layer);
                        log_debug("[Lambda CSS Background] Base layer color: #%02x%02x%02x%02x",
                            bg->color.r, bg->color.g, bg->color.b, bg->color.a);
                    }
                }

                // Count radial-gradient layers
                int radial_count = 0;
                for (int i = 0; i < count - 1; i++) {  // exclude last layer (base color)
                    CssValue* layer = layers[i];
                    if (layer && layer->type == CSS_VALUE_TYPE_FUNCTION &&
                        layer->data.function && layer->data.function->name &&
                        (str_ieq_const(layer->data.function->name, strlen(layer->data.function->name), "radial-gradient") ||
                         str_ieq_const(layer->data.function->name, strlen(layer->data.function->name), "repeating-radial-gradient"))) {
                        radial_count++;
                    }
                }

                // Allocate array for radial gradient layers if we have multiple
                if (radial_count > 0) {
                    bg->radial_layers = (RadialGradient**)alloc_prop(lycon, sizeof(RadialGradient*) * radial_count);
                    bg->radial_layer_count = 0;  // will be incremented as we parse each one

                    // Process all gradient layers (from bottom to top visually, i.e., last-to-first in CSS)
                    for (int i = count - 2; i >= 0; i--) {
                        CssValue* layer = layers[i];
                        if (layer && layer->type == CSS_VALUE_TYPE_FUNCTION &&
                            layer->data.function && layer->data.function->name) {
                            const char* func_name = layer->data.function->name;

                            if (str_ieq_const(func_name, strlen(func_name), "radial-gradient") ||
                                str_ieq_const(func_name, strlen(func_name), "repeating-radial-gradient")) {
                                // Parse this radial gradient into a new layer
                                CssDeclaration gradient_decl = *decl;
                                gradient_decl.value = layer;
                                log_debug("[Lambda CSS Background] Processing radial gradient layer %d: %s", i, func_name);
                                resolve_css_property(CSS_PROPERTY_BACKGROUND, &gradient_decl, lycon);

                                // After parsing, the radial_gradient is set on bg
                                // Move it to the layers array
                                if (bg->radial_gradient && bg->radial_layer_count < radial_count) {
                                    bg->radial_layers[bg->radial_layer_count++] = bg->radial_gradient;
                                    bg->radial_gradient = nullptr;  // will be set again by next parse
                                }
                            } else if (str_ieq_const(func_name, strlen(func_name), "linear-gradient") ||
                                       str_ieq_const(func_name, strlen(func_name), "conic-gradient")) {
                                // For now, only handle the first non-radial gradient
                                if (!bg->linear_gradient && !bg->conic_gradient) {
                                    CssDeclaration gradient_decl = *decl;
                                    gradient_decl.value = layer;
                                    log_debug("[Lambda CSS Background] Processing gradient layer %d: %s", i, func_name);
                                    resolve_css_property(CSS_PROPERTY_BACKGROUND, &gradient_decl, lycon);
                                }
                            }
                        }
                    }

                    log_debug("[Lambda CSS Background] Parsed %d radial gradient layers", bg->radial_layer_count);
                } else {
                    // No radial gradients, process first gradient layer as before
                    CssValue* first_layer = layers[0];
                    if (first_layer && first_layer->type == CSS_VALUE_TYPE_FUNCTION &&
                        first_layer->data.function && first_layer->data.function->name) {
                        CssDeclaration gradient_decl = *decl;
                        gradient_decl.value = first_layer;
                        log_debug("[Lambda CSS Background] Processing first layer gradient: %s",
                            first_layer->data.function->name);
                        resolve_css_property(CSS_PROPERTY_BACKGROUND, &gradient_decl, lycon);
                    }
                }
                return;
            }

            // simple case: single color value (e.g., "background: green;")
            if (value->type == CSS_VALUE_TYPE_COLOR || value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssDeclaration color_decl = *decl;
                color_decl.property_id = CSS_PROPERTY_BACKGROUND_COLOR;
                log_debug("[Lambda CSS Shorthand] Expanding background to background-color");
                resolve_css_property(CSS_PROPERTY_BACKGROUND_COLOR, &color_decl, lycon);
                return;
            }
            // Handle color functions like rgb(), rgba() as background color
            if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function && value->data.function->name) {
                const char* func_name = value->data.function->name;
                if (str_ieq_const(func_name, strlen(func_name), "rgb") || str_ieq_const(func_name, strlen(func_name), "rgba") ||
                    str_ieq_const(func_name, strlen(func_name), "hsl") || str_ieq_const(func_name, strlen(func_name), "hsla")) {
                    // Color function - treat as background-color
                    if (!span->bound) {
                        span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
                    }
                    if (!span->bound->background) {
                        span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
                    }
                    span->bound->background->color = resolve_color_value(lycon, value);
                    log_debug("[Lambda CSS Shorthand] Expanding %s to background-color #%02x%02x%02x%02x",
                        func_name, span->bound->background->color.r, span->bound->background->color.g,
                        span->bound->background->color.b, span->bound->background->color.a);
                    return;
                }
            }
            // Handle gradient functions (linear-gradient, radial-gradient, etc.)
            if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function && value->data.function->name) {
                const char* func_name = value->data.function->name;
                log_debug("[Lambda CSS Shorthand] Processing background function: %s", func_name);
                if (strcmp(func_name, "linear-gradient") == 0 ||
                    strcmp(func_name, "repeating-linear-gradient") == 0) {
                    // Parse linear-gradient(angle, color1, color2, ...)
                    if (!span->bound) {
                        span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
                    }
                    if (!span->bound->background) {
                        span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
                    }
                    span->bound->background->gradient_type = GRADIENT_LINEAR;

                    // Allocate LinearGradient
                    LinearGradient* lg = (LinearGradient*)alloc_prop(lycon, sizeof(LinearGradient));
                    span->bound->background->linear_gradient = lg;

                    // Parse arguments
                    CssFunction* func = value->data.function;
                    int arg_idx = 0;
                    float angle = 180.0f;  // default: to bottom

                    // Check if first arg is angle or direction
                    if (func->arg_count > 0 && func->args[0]) {
                        CssValue* first_arg = func->args[0];
                        log_debug("[CSS Gradient] first_arg type=%d (ANGLE=%d, KEYWORD=%d, NUMBER=%d)",
                            first_arg->type, CSS_VALUE_TYPE_ANGLE, CSS_VALUE_TYPE_KEYWORD, CSS_VALUE_TYPE_NUMBER);
                        if (first_arg->type == CSS_VALUE_TYPE_ANGLE) {
                            angle = first_arg->data.length.value;
                            arg_idx = 1;
                            log_debug("[CSS Gradient] angle: %.1f deg", angle);
                        } else if (first_arg->type == CSS_VALUE_TYPE_NUMBER) {
                            // Sometimes angles come as numbers with implicit deg unit
                            angle = first_arg->data.number.value;
                            arg_idx = 1;
                            log_debug("[CSS Gradient] angle from number: %.1f deg", angle);
                        } else if (first_arg->type == CSS_VALUE_TYPE_LENGTH) {
                            // Angle might be stored as length with deg unit
                            angle = first_arg->data.length.value;
                            arg_idx = 1;
                            log_debug("[CSS Gradient] angle from length: %.1f deg", angle);
                        } else if (first_arg->type == CSS_VALUE_TYPE_KEYWORD) {
                            // Handle "to top", "to right", etc.
                            // For now, use default angle
                            arg_idx = 1;
                        }
                    }
                    lg->angle = angle;

                    // Count color stops
                    int color_count = func->arg_count - arg_idx;
                    lg->stop_count = color_count > 0 ? color_count : 2;
                    lg->stops = (GradientStop*)alloc_prop(lycon, sizeof(GradientStop) * lg->stop_count);

                    // Parse color stops
                    int stop_idx = 0;
                    for (int i = arg_idx; i < func->arg_count && stop_idx < lg->stop_count; i++) {
                        CssValue* arg = func->args[i];
                        if (!arg) continue;

                        log_debug("[CSS Gradient] arg %d type=%d", i, arg->type);

                        if (arg->type == CSS_VALUE_TYPE_COLOR) {
                            // Simple color without position
                            lg->stops[stop_idx].color = resolve_color_value(lycon, arg);
                            lg->stops[stop_idx].position = -1;  // auto position
                            log_debug("[CSS Gradient] stop %d: color #%02x%02x%02x", stop_idx,
                                lg->stops[stop_idx].color.r, lg->stops[stop_idx].color.g, lg->stops[stop_idx].color.b);
                            stop_idx++;
                        } else if (arg->type == CSS_VALUE_TYPE_FUNCTION) {
                            // Color function like rgb(), rgba(), hsl(), etc.
                            lg->stops[stop_idx].color = resolve_color_value(lycon, arg);
                            lg->stops[stop_idx].position = -1;
                            log_debug("[CSS Gradient] stop %d (func): color #%02x%02x%02x", stop_idx,
                                lg->stops[stop_idx].color.r, lg->stops[stop_idx].color.g, lg->stops[stop_idx].color.b);
                            stop_idx++;
                        } else if (arg->type == CSS_VALUE_TYPE_KEYWORD) {
                            // Color keyword like red, blue, transparent
                            lg->stops[stop_idx].color = resolve_color_value(lycon, arg);
                            lg->stops[stop_idx].position = -1;
                            log_debug("[CSS Gradient] stop %d (kw): color #%02x%02x%02x", stop_idx,
                                lg->stops[stop_idx].color.r, lg->stops[stop_idx].color.g, lg->stops[stop_idx].color.b);
                            stop_idx++;
                        } else if (arg->type == CSS_VALUE_TYPE_LIST && arg->data.list.count >= 1) {
                            // Color stop with position: [color, position]
                            CssValue** items = arg->data.list.values;
                            CssValue* color_val = items[0];
                            if (color_val && (color_val->type == CSS_VALUE_TYPE_COLOR ||
                                              color_val->type == CSS_VALUE_TYPE_FUNCTION ||
                                              color_val->type == CSS_VALUE_TYPE_KEYWORD)) {
                                lg->stops[stop_idx].color = resolve_color_value(lycon, color_val);
                                lg->stops[stop_idx].position = -1;  // default auto

                                // Parse position if present
                                if (arg->data.list.count >= 2 && items[1]) {
                                    CssValue* pos_val = items[1];
                                    if (pos_val->type == CSS_VALUE_TYPE_PERCENTAGE) {
                                        lg->stops[stop_idx].position = pos_val->data.percentage.value / 100.0f;
                                    } else if (pos_val->type == CSS_VALUE_TYPE_NUMBER) {
                                        lg->stops[stop_idx].position = pos_val->data.number.value / 100.0f;
                                    }
                                }

                                log_debug("[CSS Gradient] stop %d: color #%02x%02x%02x pos=%.2f", stop_idx,
                                    lg->stops[stop_idx].color.r, lg->stops[stop_idx].color.g,
                                    lg->stops[stop_idx].color.b, lg->stops[stop_idx].position);
                                stop_idx++;
                            }
                        }
                    }
                    lg->stop_count = stop_idx;

                    // Auto-distribute positions if not specified
                    if (lg->stop_count > 0) {
                        for (int i = 0; i < lg->stop_count; i++) {
                            if (lg->stops[i].position < 0) {
                                lg->stops[i].position = (float)i / (float)(lg->stop_count - 1);
                            }
                        }
                    }

                    log_debug("[Lambda CSS Shorthand] Parsed linear-gradient with %d stops, angle=%.1f",
                        lg->stop_count, lg->angle);
                    return;
                }
                // Handle radial-gradient
                else if (strcmp(func_name, "radial-gradient") == 0 ||
                         strcmp(func_name, "repeating-radial-gradient") == 0) {
                    // Parse radial-gradient(shape size at position, color-stops...)
                    if (!span->bound) {
                        span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
                    }
                    if (!span->bound->background) {
                        span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
                    }
                    span->bound->background->gradient_type = GRADIENT_RADIAL;

                    // Allocate RadialGradient
                    RadialGradient* rg = (RadialGradient*)alloc_prop(lycon, sizeof(RadialGradient));
                    span->bound->background->radial_gradient = rg;

                    // Defaults
                    rg->shape = RADIAL_SHAPE_ELLIPSE;
                    rg->size = RADIAL_SIZE_FARTHEST_CORNER;
                    rg->cx = 0.5f;
                    rg->cy = 0.5f;
                    rg->cx_set = false;
                    rg->cy_set = false;

                    CssFunction* func = value->data.function;
                    int arg_idx = 0;

                    // Parse shape/size/position from first argument
                    // Format can be: "circle", "circle at top", "circle at top left", etc.
                    if (func->arg_count > 0 && func->args[0]) {
                        CssValue* first_arg = func->args[0];

                        // Check for keyword indicating shape/position
                        if (first_arg->type == CSS_VALUE_TYPE_KEYWORD) {
                            CssEnum kw = first_arg->data.keyword;
                            const CssEnumInfo* info = css_enum_info(kw);
                            const char* kw_name = info ? info->name : nullptr;
                            if (kw_name) {
                                if (strcmp(kw_name, "circle") == 0) {
                                    rg->shape = RADIAL_SHAPE_CIRCLE;
                                    arg_idx = 1;
                                } else if (strcmp(kw_name, "ellipse") == 0) {
                                    rg->shape = RADIAL_SHAPE_ELLIPSE;
                                    arg_idx = 1;
                                }
                            }
                            log_debug("[CSS Radial] First arg keyword: shape=%d", rg->shape);
                        }
                        // Check for list containing shape and position keywords
                        else if (first_arg->type == CSS_VALUE_TYPE_LIST) {
                            CssValue** items = first_arg->data.list.values;
                            int count = first_arg->data.list.count;
                            int at_idx = -1;

                            for (int i = 0; i < count; i++) {
                                if (!items[i]) continue;
                                log_debug("[CSS Radial] list item %d: type=%d", i, items[i]->type);

                                // Get keyword name from keyword or custom type
                                const char* kw_name = nullptr;
                                if (items[i]->type == CSS_VALUE_TYPE_KEYWORD) {
                                    const CssEnumInfo* kw_info = css_enum_info(items[i]->data.keyword);
                                    kw_name = kw_info ? kw_info->name : nullptr;
                                } else if (items[i]->type == CSS_VALUE_TYPE_CUSTOM) {
                                    kw_name = items[i]->data.custom_property.name;
                                }

                                if (kw_name) {
                                    log_debug("[CSS Radial] keyword: %s, at_idx=%d", kw_name, at_idx);

                                    if (strcmp(kw_name, "circle") == 0) {
                                        rg->shape = RADIAL_SHAPE_CIRCLE;
                                    } else if (strcmp(kw_name, "ellipse") == 0) {
                                        rg->shape = RADIAL_SHAPE_ELLIPSE;
                                    } else if (strcmp(kw_name, "at") == 0) {
                                        at_idx = i;
                                    } else if (at_idx >= 0) {
                                        // Position keyword after "at"
                                        if (strcmp(kw_name, "top") == 0) {
                                            rg->cy = 0.0f; rg->cy_set = true;
                                        } else if (strcmp(kw_name, "bottom") == 0) {
                                            rg->cy = 1.0f; rg->cy_set = true;
                                        } else if (strcmp(kw_name, "left") == 0) {
                                            rg->cx = 0.0f; rg->cx_set = true;
                                        } else if (strcmp(kw_name, "right") == 0) {
                                            rg->cx = 1.0f; rg->cx_set = true;
                                        } else if (strcmp(kw_name, "center") == 0) {
                                            // center is default, do nothing special
                                        }
                                    }
                                }
                            }
                            arg_idx = 1;
                            log_debug("[CSS Radial] Parsed list: shape=%d, center=(%.2f, %.2f)",
                                rg->shape, rg->cx, rg->cy);
                        }
                    }

                    // Count color stops
                    int color_count = func->arg_count - arg_idx;
                    rg->stop_count = color_count > 0 ? color_count : 2;
                    rg->stops = (GradientStop*)alloc_prop(lycon, sizeof(GradientStop) * rg->stop_count);

                    // Parse color stops (same logic as linear gradient)
                    int stop_idx = 0;
                    for (int i = arg_idx; i < func->arg_count && stop_idx < rg->stop_count; i++) {
                        CssValue* arg = func->args[i];
                        if (!arg) continue;

                        if (arg->type == CSS_VALUE_TYPE_COLOR) {
                            rg->stops[stop_idx].color = resolve_color_value(lycon, arg);
                            rg->stops[stop_idx].position = -1;
                            stop_idx++;
                        } else if (arg->type == CSS_VALUE_TYPE_FUNCTION) {
                            rg->stops[stop_idx].color = resolve_color_value(lycon, arg);
                            rg->stops[stop_idx].position = -1;
                            stop_idx++;
                        } else if (arg->type == CSS_VALUE_TYPE_KEYWORD) {
                            // Color keyword like "transparent"
                            Color c = resolve_color_value(lycon, arg);
                            rg->stops[stop_idx].color = c;
                            rg->stops[stop_idx].position = -1;
                            stop_idx++;
                        } else if (arg->type == CSS_VALUE_TYPE_LIST && arg->data.list.count >= 1) {
                            CssValue** items = arg->data.list.values;
                            Color c = resolve_color_value(lycon, items[0]);
                            rg->stops[stop_idx].color = c;
                            rg->stops[stop_idx].position = -1;

                            if (arg->data.list.count >= 2 && items[1]) {
                                CssValue* pos_val = items[1];
                                if (pos_val->type == CSS_VALUE_TYPE_PERCENTAGE) {
                                    rg->stops[stop_idx].position = pos_val->data.percentage.value / 100.0f;
                                } else if (pos_val->type == CSS_VALUE_TYPE_NUMBER) {
                                    rg->stops[stop_idx].position = pos_val->data.number.value / 100.0f;
                                }
                            }
                            stop_idx++;
                        }
                    }
                    rg->stop_count = stop_idx;

                    // Auto-distribute positions
                    if (rg->stop_count > 0) {
                        for (int i = 0; i < rg->stop_count; i++) {
                            if (rg->stops[i].position < 0) {
                                rg->stops[i].position = (float)i / (float)(rg->stop_count - 1);
                            }
                        }
                    }

                    log_debug("[Lambda CSS Shorthand] Parsed radial-gradient with %d stops, shape=%d, center=(%.2f,%.2f)",
                        rg->stop_count, rg->shape, rg->cx, rg->cy);
                    return;
                }
                // Handle conic-gradient
                else if (strcmp(func_name, "conic-gradient") == 0 ||
                         strcmp(func_name, "repeating-conic-gradient") == 0) {
                    // Parse conic-gradient(from angle at position, color-stops...)
                    if (!span->bound) {
                        span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
                    }
                    if (!span->bound->background) {
                        span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
                    }
                    span->bound->background->gradient_type = GRADIENT_CONIC;

                    // Allocate ConicGradient
                    ConicGradient* cg = (ConicGradient*)alloc_prop(lycon, sizeof(ConicGradient));
                    span->bound->background->conic_gradient = cg;

                    // Defaults
                    cg->from_angle = 0.0f;
                    cg->cx = 0.5f;
                    cg->cy = 0.5f;
                    cg->cx_set = false;
                    cg->cy_set = false;

                    CssFunction* func = value->data.function;
                    int arg_idx = 0;

                    // Parse "from Xdeg" from first argument
                    log_debug("[CSS Conic] func has %d args", func->arg_count);
                    if (func->arg_count > 0 && func->args[0]) {
                        CssValue* first_arg = func->args[0];
                        log_debug("[CSS Conic] first_arg type=%d", first_arg->type);

                        if (first_arg->type == CSS_VALUE_TYPE_LIST) {
                            CssValue** items = first_arg->data.list.values;
                            int count = first_arg->data.list.count;
                            log_debug("[CSS Conic] first_arg is list with %d items", count);

                            for (int i = 0; i < count; i++) {
                                if (!items[i]) continue;
                                log_debug("[CSS Conic] list item %d: type=%d", i, items[i]->type);

                                // Check for "from" keyword (may be keyword or custom type with name)
                                bool is_from_keyword = false;
                                if (items[i]->type == CSS_VALUE_TYPE_KEYWORD) {
                                    const CssEnumInfo* kw_info = css_enum_info(items[i]->data.keyword);
                                    const char* kw_name = kw_info ? kw_info->name : nullptr;
                                    log_debug("[CSS Conic] keyword: %s", kw_name ? kw_name : "(null)");
                                    is_from_keyword = (kw_name && strcmp(kw_name, "from") == 0);
                                } else if (items[i]->type == CSS_VALUE_TYPE_CUSTOM) {
                                    const char* custom_name = items[i]->data.custom_property.name;
                                    log_debug("[CSS Conic] custom property: %s", custom_name ? custom_name : "(null)");
                                    is_from_keyword = (custom_name && strcmp(custom_name, "from") == 0);
                                }

                                if (is_from_keyword) {
                                    // Next item should be angle
                                    if (i + 1 < count && items[i + 1]) {
                                        CssValue* angle_val = items[i + 1];
                                        log_debug("[CSS Conic] next item type=%d", angle_val->type);
                                        if (angle_val->type == CSS_VALUE_TYPE_ANGLE) {
                                            cg->from_angle = angle_val->data.length.value;
                                            log_debug("[CSS Conic] from angle (ANGLE)=%.1f", cg->from_angle);
                                        } else if (angle_val->type == CSS_VALUE_TYPE_NUMBER) {
                                            cg->from_angle = angle_val->data.number.value;
                                            log_debug("[CSS Conic] from angle (NUMBER)=%.1f", cg->from_angle);
                                        } else if (angle_val->type == CSS_VALUE_TYPE_LENGTH) {
                                            cg->from_angle = angle_val->data.length.value;
                                            log_debug("[CSS Conic] from angle (LENGTH)=%.1f", cg->from_angle);
                                        }
                                        i++; // Skip the angle value
                                    }
                                } else if (items[i]->type == CSS_VALUE_TYPE_ANGLE) {
                                    cg->from_angle = items[i]->data.length.value;
                                    log_debug("[CSS Conic] direct angle=%.1f", cg->from_angle);
                                } else if (items[i]->type == CSS_VALUE_TYPE_LENGTH) {
                                    // Angle stored as length with deg unit
                                    cg->from_angle = items[i]->data.length.value;
                                    log_debug("[CSS Conic] angle from length=%.1f", cg->from_angle);
                                }
                            }
                            arg_idx = 1;
                        } else if (first_arg->type == CSS_VALUE_TYPE_ANGLE) {
                            cg->from_angle = first_arg->data.length.value;
                            arg_idx = 1;
                        }
                        log_debug("[CSS Conic] from_angle=%.1f", cg->from_angle);
                    }

                    // Count and parse color stops
                    int color_count = func->arg_count - arg_idx;
                    cg->stop_count = color_count > 0 ? color_count : 2;
                    cg->stops = (GradientStop*)alloc_prop(lycon, sizeof(GradientStop) * cg->stop_count);

                    int stop_idx = 0;
                    for (int i = arg_idx; i < func->arg_count && stop_idx < cg->stop_count; i++) {
                        CssValue* arg = func->args[i];
                        if (!arg) continue;

                        log_debug("[CSS Conic] arg %d type=%d", i, arg->type);

                        if (arg->type == CSS_VALUE_TYPE_COLOR) {
                            cg->stops[stop_idx].color = resolve_color_value(lycon, arg);
                            cg->stops[stop_idx].position = -1;
                            log_debug("[CSS Conic] stop %d: color #%02x%02x%02x", stop_idx,
                                cg->stops[stop_idx].color.r, cg->stops[stop_idx].color.g, cg->stops[stop_idx].color.b);
                            stop_idx++;
                        } else if (arg->type == CSS_VALUE_TYPE_FUNCTION) {
                            cg->stops[stop_idx].color = resolve_color_value(lycon, arg);
                            cg->stops[stop_idx].position = -1;
                            log_debug("[CSS Conic] stop %d (func): color #%02x%02x%02x", stop_idx,
                                cg->stops[stop_idx].color.r, cg->stops[stop_idx].color.g, cg->stops[stop_idx].color.b);
                            stop_idx++;
                        } else if (arg->type == CSS_VALUE_TYPE_KEYWORD) {
                            Color c = resolve_color_value(lycon, arg);
                            cg->stops[stop_idx].color = c;
                            cg->stops[stop_idx].position = -1;
                            log_debug("[CSS Conic] stop %d (kw): color #%02x%02x%02x", stop_idx,
                                cg->stops[stop_idx].color.r, cg->stops[stop_idx].color.g, cg->stops[stop_idx].color.b);
                            stop_idx++;
                        } else if (arg->type == CSS_VALUE_TYPE_LIST && arg->data.list.count >= 1) {
                            CssValue** items = arg->data.list.values;
                            Color c = resolve_color_value(lycon, items[0]);
                            cg->stops[stop_idx].color = c;
                            cg->stops[stop_idx].position = -1;

                            if (arg->data.list.count >= 2 && items[1]) {
                                CssValue* pos_val = items[1];
                                if (pos_val->type == CSS_VALUE_TYPE_PERCENTAGE) {
                                    cg->stops[stop_idx].position = pos_val->data.percentage.value / 100.0f;
                                } else if (pos_val->type == CSS_VALUE_TYPE_NUMBER) {
                                    cg->stops[stop_idx].position = pos_val->data.number.value / 100.0f;
                                }
                            }
                            log_debug("[CSS Conic] stop %d (list): color #%02x%02x%02x", stop_idx,
                                cg->stops[stop_idx].color.r, cg->stops[stop_idx].color.g, cg->stops[stop_idx].color.b);
                            stop_idx++;
                        } else {
                            log_debug("[CSS Conic] arg %d: unhandled type %d", i, arg->type);
                        }
                    }
                    cg->stop_count = stop_idx;

                    // Auto-distribute positions (for conic, positions are angles 0-1 mapping to 0-360deg)
                    if (cg->stop_count > 0) {
                        for (int i = 0; i < cg->stop_count; i++) {
                            if (cg->stops[i].position < 0) {
                                cg->stops[i].position = (float)i / (float)(cg->stop_count - 1);
                            }
                        }
                    }

                    log_debug("[Lambda CSS Shorthand] Parsed conic-gradient with %d stops, from=%.1fdeg, center=(%.2f,%.2f)",
                        cg->stop_count, cg->from_angle, cg->cx, cg->cy);
                    return;
                }
            }
            log_debug("[Lambda CSS Shorthand] Complex background shorthand not yet implemented (type=%d)", value->type);
            return;
        }

        case CSS_PROPERTY_GAP: {
            // gap shorthand: 1-2 values (row-gap column-gap)
            // If only one value is specified, it's used for both row and column gap
            log_debug("[Lambda CSS Shorthand] Expanding gap shorthand");

            if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_NUMBER ||
                value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                // single value - use for both row-gap and column-gap
                log_debug("[Lambda CSS Shorthand] Expanding single-value gap to row-gap and column-gap");
                CssDeclaration gap_decl = *decl;
                gap_decl.property_id = CSS_PROPERTY_ROW_GAP;
                resolve_css_property(CSS_PROPERTY_ROW_GAP, &gap_decl, lycon);
                gap_decl.property_id = CSS_PROPERTY_COLUMN_GAP;
                resolve_css_property(CSS_PROPERTY_COLUMN_GAP, &gap_decl, lycon);
                return;
            } else if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count == 2) {
                // two values: row-gap column-gap
                log_debug("[Lambda CSS Shorthand] Expanding two-value gap");
                CssValue** values = value->data.list.values;

                CssDeclaration row_gap_decl = *decl;
                row_gap_decl.value = values[0];
                row_gap_decl.property_id = CSS_PROPERTY_ROW_GAP;
                resolve_css_property(CSS_PROPERTY_ROW_GAP, &row_gap_decl, lycon);

                CssDeclaration col_gap_decl = *decl;
                col_gap_decl.value = values[1];
                col_gap_decl.property_id = CSS_PROPERTY_COLUMN_GAP;
                resolve_css_property(CSS_PROPERTY_COLUMN_GAP, &col_gap_decl, lycon);
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
