/**
 * render_svg_inline.cpp - Inline SVG Rendering via ThorVG
 *
 * Converts SVG element trees to ThorVG scene graphs for rendering
 * inline SVG content within HTML documents.
 */

#include "render_svg_inline.hpp"
#include "render.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/log.h"
#include "../lib/font/font.h"
#include "../lib/memtrack.h"
#include "../lib/strbuf.h"
#include "../lib/str.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

// ============================================================================
// Forward Declarations
// ============================================================================

static Tvg_Paint render_svg_element(SvgRenderContext* ctx, Element* elem);
static Tvg_Paint render_svg_rect(SvgRenderContext* ctx, Element* elem);
static Tvg_Paint render_svg_circle(SvgRenderContext* ctx, Element* elem);
static Tvg_Paint render_svg_ellipse(SvgRenderContext* ctx, Element* elem);
static Tvg_Paint render_svg_line(SvgRenderContext* ctx, Element* elem);
static Tvg_Paint render_svg_polyline(SvgRenderContext* ctx, Element* elem, bool close_path);
static Tvg_Paint render_svg_path(SvgRenderContext* ctx, Element* elem);
static Tvg_Paint render_svg_text(SvgRenderContext* ctx, Element* elem);
static Tvg_Paint render_svg_image(SvgRenderContext* ctx, Element* elem);
static Tvg_Paint render_svg_group(SvgRenderContext* ctx, Element* elem);
static Tvg_Paint render_svg_children_as_scene(SvgRenderContext* ctx, Element* elem);
static void process_svg_defs(SvgRenderContext* ctx, Element* defs);
static void apply_svg_fill_stroke(SvgRenderContext* ctx, Tvg_Paint shape, Element* elem);
static void apply_svg_transform(SvgRenderContext* ctx, Tvg_Paint shape, Element* elem);

// ============================================================================
// Helper: Get Attribute from Lambda Element
// ============================================================================

static const char* extract_element_attribute(Element* element, const char* attr_name, Arena* arena) {
    (void)arena;  // not used in this implementation
    if (!element || !element->data || !attr_name) return nullptr;

    TypeElmt* elem_type = (TypeElmt*)element->type;
    if (!elem_type) return nullptr;

    // cast the element type to TypeMap to access attributes
    TypeMap* map_type = (TypeMap*)elem_type;
    if (!map_type->shape) return nullptr;

    // search through element fields for the attribute
    ShapeEntry* field = map_type->shape;
    size_t attr_len = strlen(attr_name);
    for (int i = 0; i < map_type->length && field; i++) {
        if (field->name && field->name->str &&
            field->name->length == attr_len &&
            strncmp(field->name->str, attr_name, field->name->length) == 0) {

            if (field->type && field->type->type_id == LMD_TYPE_STRING) {
                void* data = ((char*)element->data) + field->byte_offset;
                String* str_val = *(String**)data;
                return str_val ? str_val->chars : nullptr;
            }
        }
        field = field->next;
    }

    return nullptr;
}

// ============================================================================
// Helper: Get element tag name
// ============================================================================

static const char* get_element_tag_name(Element* elem) {
    if (!elem || !elem->type) return nullptr;
    TypeElmt* type = (TypeElmt*)elem->type;
    return type->name.str;
}

// ============================================================================
// Helper: Get attribute from Element
// ============================================================================

static const char* get_svg_attr(Element* elem, const char* name) {
    return extract_element_attribute(elem, name, nullptr);
}

// ============================================================================
// Helper: Get child element at index
// ============================================================================

static Element* get_child_element_at(Element* parent, int index) {
    if (!parent || index < 0 || index >= parent->length) return nullptr;
    Item child = parent->items[index];
    if (get_type_id(child) != LMD_TYPE_ELEMENT) return nullptr;
    return child.element;
}

// ============================================================================
// SVG ViewBox Parsing
// ============================================================================

SvgViewBox parse_svg_viewbox(const char* viewbox_attr) {
    SvgViewBox vb = {0, 0, 0, 0, false};
    if (!viewbox_attr || !*viewbox_attr) return vb;

    // parse "min-x min-y width height"
    // separators can be comma or whitespace
    float values[4];
    int count = 0;
    const char* p = viewbox_attr;

    while (*p && count < 4) {
        // skip whitespace and commas
        while (*p && (isspace(*p) || *p == ',')) p++;
        if (!*p) break;

        char* end;
        values[count] = strtof(p, &end);
        if (end == p) break;  // parsing failed
        count++;
        p = end;
    }

    if (count == 4) {
        vb.min_x = values[0];
        vb.min_y = values[1];
        vb.width = values[2];
        vb.height = values[3];
        vb.has_viewbox = true;
    }

    return vb;
}

// ============================================================================
// SVG Length Parsing
// ============================================================================

float parse_svg_length(const char* value, float default_value) {
    if (!value || !*value) return default_value;

    char* end;
    float num = strtof(value, &end);
    if (end == value) return default_value;

    // skip whitespace after number
    while (*end && isspace(*end)) end++;

    // check for unit suffix
    if (*end == '\0') {
        return num;  // unitless = user units (pixels)
    } else if (strcmp(end, "px") == 0) {
        return num;
    } else if (strcmp(end, "pt") == 0) {
        return num * 1.333333f;  // 1pt = 1.333px
    } else if (strcmp(end, "pc") == 0) {
        return num * 16.0f;  // 1pc = 16px
    } else if (strcmp(end, "mm") == 0) {
        return num * 3.779528f;  // 1mm ≈ 3.78px
    } else if (strcmp(end, "cm") == 0) {
        return num * 37.79528f;  // 1cm ≈ 37.8px
    } else if (strcmp(end, "in") == 0) {
        return num * 96.0f;  // 1in = 96px
    } else if (strcmp(end, "em") == 0) {
        return num * 16.0f;  // assume 16px base font size
    } else if (strcmp(end, "ex") == 0) {
        return num * 8.0f;   // assume ex ≈ 0.5em
    } else if (*end == '%') {
        // return percentage as-is, caller must handle
        return num;
    }

    return num;  // unknown unit, use numeric value
}

// ============================================================================
// SVG Color Parsing
// ============================================================================

// named color lookup table - extended SVG/CSS named colors
static const struct { const char* name; uint32_t rgb; } svg_named_colors[] = {
    // Basic colors
    {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000},
    {"green", 0x008000}, {"blue", 0x0000FF}, {"yellow", 0xFFFF00},
    {"cyan", 0x00FFFF}, {"magenta", 0xFF00FF}, {"gray", 0x808080},
    {"grey", 0x808080}, {"silver", 0xC0C0C0}, {"maroon", 0x800000},
    {"olive", 0x808000}, {"lime", 0x00FF00}, {"aqua", 0x00FFFF},
    {"teal", 0x008080}, {"navy", 0x000080}, {"fuchsia", 0xFF00FF},
    {"purple", 0x800080}, {"orange", 0xFFA500}, {"pink", 0xFFC0CB},
    {"brown", 0xA52A2A}, {"coral", 0xFF7F50}, {"gold", 0xFFD700},
    {"indigo", 0x4B0082}, {"ivory", 0xFFFFF0}, {"khaki", 0xF0E68C},
    {"lavender", 0xE6E6FA}, {"transparent", 0x00000000},
    // Reds
    {"crimson", 0xDC143C}, {"darkred", 0x8B0000}, {"firebrick", 0xB22222},
    {"indianred", 0xCD5C5C}, {"lightcoral", 0xF08080}, {"salmon", 0xFA8072},
    {"darksalmon", 0xE9967A}, {"lightsalmon", 0xFFA07A}, {"orangered", 0xFF4500},
    {"tomato", 0xFF6347},
    // Oranges & Yellows
    {"darkorange", 0xFF8C00}, {"peachpuff", 0xFFDAB9}, {"moccasin", 0xFFE4B5},
    {"palegoldenrod", 0xEEE8AA}, {"lightyellow", 0xFFFFE0}, {"lemonchiffon", 0xFFFACD},
    // Greens
    {"limegreen", 0x32CD32}, {"lightgreen", 0x90EE90}, {"palegreen", 0x98FB98},
    {"darkgreen", 0x006400}, {"forestgreen", 0x228B22}, {"seagreen", 0x2E8B57},
    {"mediumseagreen", 0x3CB371}, {"springgreen", 0x00FF7F}, {"mediumspringgreen", 0x00FA9A},
    {"darkseagreen", 0x8FBC8F}, {"mediumaquamarine", 0x66CDAA}, {"yellowgreen", 0x9ACD32},
    {"olivedrab", 0x6B8E23}, {"darkolivegreen", 0x556B2F}, {"greenyellow", 0xADFF2F},
    {"chartreuse", 0x7FFF00}, {"lawngreen", 0x7CFC00},
    // Blues
    {"lightblue", 0xADD8E6}, {"powderblue", 0xB0E0E6}, {"lightskyblue", 0x87CEFA},
    {"skyblue", 0x87CEEB}, {"deepskyblue", 0x00BFFF}, {"dodgerblue", 0x1E90FF},
    {"cornflowerblue", 0x6495ED}, {"steelblue", 0x4682B4}, {"royalblue", 0x4169E1},
    {"mediumblue", 0x0000CD}, {"darkblue", 0x00008B}, {"midnightblue", 0x191970},
    {"cadetblue", 0x5F9EA0}, {"lightsteelblue", 0xB0C4DE}, {"slateblue", 0x6A5ACD},
    {"mediumslateblue", 0x7B68EE}, {"darkslateblue", 0x483D8B},
    // Purples
    {"mediumpurple", 0x9370DB}, {"blueviolet", 0x8A2BE2}, {"darkviolet", 0x9400D3},
    {"darkorchid", 0x9932CC}, {"mediumorchid", 0xBA55D3}, {"orchid", 0xDA70D6},
    {"plum", 0xDDA0DD}, {"violet", 0xEE82EE}, {"thistle", 0xD8BFD8},
    {"darkmagenta", 0x8B008B}, {"mediumvioletred", 0xC71585}, {"deeppink", 0xFF1493},
    {"hotpink", 0xFF69B4}, {"lightpink", 0xFFB6C1}, {"palevioletred", 0xDB7093},
    // Cyans & Teals
    {"lightcyan", 0xE0FFFF}, {"paleturquoise", 0xAFEEEE}, {"aquamarine", 0x7FFFD4},
    {"turquoise", 0x40E0D0}, {"mediumturquoise", 0x48D1CC}, {"darkturquoise", 0x00CED1},
    {"darkcyan", 0x008B8B},
    // Browns & Tans
    {"tan", 0xD2B48C}, {"burlywood", 0xDEB887}, {"wheat", 0xF5DEB3},
    {"sandybrown", 0xF4A460}, {"goldenrod", 0xDAA520}, {"darkgoldenrod", 0xB8860B},
    {"peru", 0xCD853F}, {"chocolate", 0xD2691E}, {"sienna", 0xA0522D},
    {"saddlebrown", 0x8B4513}, {"rosybrown", 0xBC8F8F},
    // Grays
    {"lightgray", 0xD3D3D3}, {"lightgrey", 0xD3D3D3}, {"darkgray", 0xA9A9A9},
    {"darkgrey", 0xA9A9A9}, {"dimgray", 0x696969}, {"dimgrey", 0x696969},
    {"lightslategray", 0x778899}, {"slategray", 0x708090}, {"darkslategray", 0x2F4F4F},
    {"gainsboro", 0xDCDCDC},
    // Whites
    {"snow", 0xFFFAFA}, {"honeydew", 0xF0FFF0}, {"mintcream", 0xF5FFFA},
    {"azure", 0xF0FFFF}, {"aliceblue", 0xF0F8FF}, {"ghostwhite", 0xF8F8FF},
    {"whitesmoke", 0xF5F5F5}, {"seashell", 0xFFF5EE}, {"beige", 0xF5F5DC},
    {"oldlace", 0xFDF5E6}, {"floralwhite", 0xFFFAF0}, {"linen", 0xFAF0E6},
    {"lavenderblush", 0xFFF0F5}, {"mistyrose", 0xFFE4E1}, {"papayawhip", 0xFFEFD5},
    {"blanchedalmond", 0xFFEBCD}, {"bisque", 0xFFE4C4}, {"antiquewhite", 0xFAEBD7},
    {"cornsilk", 0xFFF8DC}, {"navajowhite", 0xFFDEAD},
    {nullptr, 0}
};

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

Color parse_svg_color(const char* value) {
    Color c;
    c.r = 0; c.g = 0; c.b = 0; c.a = 255;  // default black
    if (!value || !*value) return c;

    // skip whitespace
    while (*value && isspace(*value)) value++;

    // check for "none"
    if (strcmp(value, "none") == 0) {
        c.a = 0;
        return c;
    }

    // check for "transparent"
    if (strcmp(value, "transparent") == 0) {
        c.a = 0;
        return c;
    }

    // hex color: #rgb, #rrggbb, #rgba, #rrggbbaa
    if (*value == '#') {
        value++;
        int len = strlen(value);

        if (len == 3) {
            // #rgb
            int r = hex_digit(value[0]);
            int g = hex_digit(value[1]);
            int b = hex_digit(value[2]);
            if (r >= 0 && g >= 0 && b >= 0) {
                c.r = r * 17; c.g = g * 17; c.b = b * 17;
            }
        } else if (len == 4) {
            // #rgba
            int r = hex_digit(value[0]);
            int g = hex_digit(value[1]);
            int b = hex_digit(value[2]);
            int a = hex_digit(value[3]);
            if (r >= 0 && g >= 0 && b >= 0 && a >= 0) {
                c.r = r * 17; c.g = g * 17; c.b = b * 17; c.a = a * 17;
            }
        } else if (len == 6) {
            // #rrggbb
            int r = hex_digit(value[0]) * 16 + hex_digit(value[1]);
            int g = hex_digit(value[2]) * 16 + hex_digit(value[3]);
            int b = hex_digit(value[4]) * 16 + hex_digit(value[5]);
            c.r = r; c.g = g; c.b = b;
        } else if (len == 8) {
            // #rrggbbaa
            int r = hex_digit(value[0]) * 16 + hex_digit(value[1]);
            int g = hex_digit(value[2]) * 16 + hex_digit(value[3]);
            int b = hex_digit(value[4]) * 16 + hex_digit(value[5]);
            int a = hex_digit(value[6]) * 16 + hex_digit(value[7]);
            c.r = r; c.g = g; c.b = b; c.a = a;
        }
        return c;
    }

    // rgb() or rgba()
    if (strncmp(value, "rgb", 3) == 0) {
        const char* p = strchr(value, '(');
        if (p) {
            p++;
            int r, g, b;
            float a = 1.0f;
            if (sscanf(p, "%d,%d,%d,%f", &r, &g, &b, &a) >= 3 ||
                sscanf(p, "%d %d %d / %f", &r, &g, &b, &a) >= 3 ||
                sscanf(p, "%d %d %d", &r, &g, &b) == 3) {
                c.r = (uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r));
                c.g = (uint8_t)(g < 0 ? 0 : (g > 255 ? 255 : g));
                c.b = (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b));
                c.a = (uint8_t)(a * 255);
            }
        }
        return c;
    }

    // named color lookup
    for (int i = 0; svg_named_colors[i].name != nullptr; i++) {
        if (str_ieq(value, strlen(value), svg_named_colors[i].name, strlen(svg_named_colors[i].name))) {
            uint32_t rgb = svg_named_colors[i].rgb;
            c.r = (rgb >> 16) & 0xFF;
            c.g = (rgb >> 8) & 0xFF;
            c.b = rgb & 0xFF;
            if (strcmp(svg_named_colors[i].name, "transparent") == 0) {
                c.a = 0;
            }
            return c;
        }
    }

    return c;  // default black
}

// ============================================================================
// SVG Transform Parsing
// ============================================================================

bool parse_svg_transform(const char* transform_str, float matrix[6]) {
    if (!transform_str || !matrix) return false;

    // initialize to identity matrix: [a, b, c, d, e, f] = [1, 0, 0, 1, 0, 0]
    matrix[0] = 1; matrix[1] = 0;  // a, b
    matrix[2] = 0; matrix[3] = 1;  // c, d
    matrix[4] = 0; matrix[5] = 0;  // e, f (translation)

    const char* p = transform_str;

    while (*p) {
        // skip whitespace
        while (*p && isspace(*p)) p++;
        if (!*p) break;

        float local[6] = {1, 0, 0, 1, 0, 0};

        if (strncmp(p, "translate", 9) == 0) {
            p += 9;
            while (*p && *p != '(') p++;
            if (*p == '(') {
                p++;
                float tx = 0, ty = 0;
                tx = strtof(p, (char**)&p);
                while (*p && (isspace(*p) || *p == ',')) p++;
                if (*p && *p != ')') {
                    ty = strtof(p, (char**)&p);
                }
                local[4] = tx;
                local[5] = ty;
            }
        } else if (strncmp(p, "scale", 5) == 0) {
            p += 5;
            while (*p && *p != '(') p++;
            if (*p == '(') {
                p++;
                float sx = 1, sy = 1;
                sx = strtof(p, (char**)&p);
                while (*p && (isspace(*p) || *p == ',')) p++;
                if (*p && *p != ')') {
                    sy = strtof(p, (char**)&p);
                } else {
                    sy = sx;  // uniform scale
                }
                local[0] = sx;
                local[3] = sy;
            }
        } else if (strncmp(p, "rotate", 6) == 0) {
            p += 6;
            while (*p && *p != '(') p++;
            if (*p == '(') {
                p++;
                float angle = strtof(p, (char**)&p);
                float rad = angle * 3.14159265f / 180.0f;
                float c_val = cosf(rad);
                float s_val = sinf(rad);
                local[0] = c_val; local[1] = s_val;
                local[2] = -s_val; local[3] = c_val;
                // TODO: handle rotate(angle, cx, cy) with pivot point
            }
        } else if (strncmp(p, "skewX", 5) == 0) {
            p += 5;
            while (*p && *p != '(') p++;
            if (*p == '(') {
                p++;
                float angle = strtof(p, (char**)&p);
                float rad = angle * 3.14159265f / 180.0f;
                local[2] = tanf(rad);
            }
        } else if (strncmp(p, "skewY", 5) == 0) {
            p += 5;
            while (*p && *p != '(') p++;
            if (*p == '(') {
                p++;
                float angle = strtof(p, (char**)&p);
                float rad = angle * 3.14159265f / 180.0f;
                local[1] = tanf(rad);
            }
        } else if (strncmp(p, "matrix", 6) == 0) {
            p += 6;
            while (*p && *p != '(') p++;
            if (*p == '(') {
                p++;
                for (int i = 0; i < 6 && *p; i++) {
                    while (*p && (isspace(*p) || *p == ',')) p++;
                    local[i] = strtof(p, (char**)&p);
                }
            }
        } else {
            // unknown transform, skip to next
            while (*p && *p != ')') p++;
        }

        // skip closing paren
        while (*p && *p != ')') p++;
        if (*p == ')') p++;

        // multiply: result = matrix * local
        float result[6];
        result[0] = matrix[0] * local[0] + matrix[2] * local[1];
        result[1] = matrix[1] * local[0] + matrix[3] * local[1];
        result[2] = matrix[0] * local[2] + matrix[2] * local[3];
        result[3] = matrix[1] * local[2] + matrix[3] * local[3];
        result[4] = matrix[0] * local[4] + matrix[2] * local[5] + matrix[4];
        result[5] = matrix[1] * local[4] + matrix[3] * local[5] + matrix[5];

        memcpy(matrix, result, sizeof(result));
    }

    return true;
}

// ============================================================================
// SVG Intrinsic Size Calculation
// ============================================================================

SvgIntrinsicSize calculate_svg_intrinsic_size(Element* svg_element) {
    SvgIntrinsicSize size = {300, 150, 2.0f, false, false};  // HTML default

    if (!svg_element) return size;

    const char* width_attr = get_svg_attr(svg_element, "width");
    const char* height_attr = get_svg_attr(svg_element, "height");
    const char* viewbox_attr = get_svg_attr(svg_element, "viewBox");

    SvgViewBox vb = parse_svg_viewbox(viewbox_attr);

    // determine width
    if (width_attr && *width_attr) {
        size.width = parse_svg_length(width_attr, 300);
        size.has_intrinsic_width = true;
    } else if (vb.has_viewbox && vb.width > 0) {
        size.width = vb.width;
        size.has_intrinsic_width = true;
    }

    // determine height
    if (height_attr && *height_attr) {
        size.height = parse_svg_length(height_attr, 150);
        size.has_intrinsic_height = true;
    } else if (vb.has_viewbox && vb.height > 0) {
        size.height = vb.height;
        size.has_intrinsic_height = true;
    }

    // calculate aspect ratio
    if (size.height > 0) {
        size.aspect_ratio = size.width / size.height;
    }

    return size;
}

// ============================================================================
// Check if element is inline SVG
// ============================================================================

bool is_inline_svg_element(DomElement* elem) {
    if (!elem) return false;
    return elem->tag_id == HTM_TAG_SVG;
}

// ============================================================================
// Apply Fill and Stroke to ThorVG Shape
// ============================================================================

static void apply_svg_fill_stroke(SvgRenderContext* ctx, Tvg_Paint shape, Element* elem) {
    if (!shape || !elem) return;

    // get fill attribute - inherit from context if not specified
    const char* fill = get_svg_attr(elem, "fill");

    // determine effective fill (element attribute → inherited from context → default black)
    Color fc;
    bool has_fill = true;

    if (fill) {
        // element has explicit fill attribute
        if (strcmp(fill, "none") == 0) {
            has_fill = false;
        } else if (strncmp(fill, "url(#", 5) == 0) {
            // gradient reference - TODO: implement gradient lookup
            log_debug("[SVG] gradient fill not yet implemented: %s", fill);
            fc = ctx->fill_color;  // fallback to inherited
        } else {
            fc = parse_svg_color(fill);
        }
    } else if (!ctx->fill_none) {
        // no fill attribute, inherit from context
        fc = ctx->fill_color;
    } else {
        // context has fill_none set
        has_fill = false;
    }

    if (has_fill) {
        // apply fill-opacity if present
        const char* fill_opacity = get_svg_attr(elem, "fill-opacity");
        if (fill_opacity) {
            float opacity = strtof(fill_opacity, nullptr);
            fc.a = (uint8_t)(fc.a * opacity);
        }

        // apply general opacity
        const char* opacity = get_svg_attr(elem, "opacity");
        if (opacity) {
            float op = strtof(opacity, nullptr);
            fc.a = (uint8_t)(fc.a * op);
        }

        tvg_shape_set_fill_color(shape, fc.r, fc.g, fc.b, fc.a);
    }

    // get stroke - inherit from context if not specified
    const char* stroke = get_svg_attr(elem, "stroke");
    bool has_stroke = false;
    Color sc;

    if (stroke) {
        // element has explicit stroke attribute
        if (strcmp(stroke, "none") != 0) {
            has_stroke = true;
            sc = parse_svg_color(stroke);
        }
    } else if (!ctx->stroke_none) {
        // no stroke attribute, inherit from context
        has_stroke = true;
        sc = ctx->stroke_color;
    }

    if (has_stroke) {
        // stroke width - inherit from context if not specified
        const char* stroke_width_str = get_svg_attr(elem, "stroke-width");
        float stroke_width = stroke_width_str ? parse_svg_length(stroke_width_str, 1.0f) : ctx->stroke_width;
        tvg_shape_set_stroke_width(shape, stroke_width);

        // apply stroke-opacity
        const char* stroke_opacity = get_svg_attr(elem, "stroke-opacity");
        if (stroke_opacity) {
            float opacity = strtof(stroke_opacity, nullptr);
            sc.a = (uint8_t)(sc.a * opacity);
        }

        tvg_shape_set_stroke_color(shape, sc.r, sc.g, sc.b, sc.a);

        // stroke-linecap
        const char* linecap = get_svg_attr(elem, "stroke-linecap");
        if (linecap) {
            if (strcmp(linecap, "round") == 0) {
                tvg_shape_set_stroke_cap(shape, TVG_STROKE_CAP_ROUND);
            } else if (strcmp(linecap, "square") == 0) {
                tvg_shape_set_stroke_cap(shape, TVG_STROKE_CAP_SQUARE);
            } else {
                tvg_shape_set_stroke_cap(shape, TVG_STROKE_CAP_BUTT);
            }
        }

        // stroke-linejoin
        const char* linejoin = get_svg_attr(elem, "stroke-linejoin");
        if (linejoin) {
            if (strcmp(linejoin, "round") == 0) {
                tvg_shape_set_stroke_join(shape, TVG_STROKE_JOIN_ROUND);
            } else if (strcmp(linejoin, "bevel") == 0) {
                tvg_shape_set_stroke_join(shape, TVG_STROKE_JOIN_BEVEL);
            } else {
                tvg_shape_set_stroke_join(shape, TVG_STROKE_JOIN_MITER);
            }
        }

        // stroke-dasharray
        const char* dasharray = get_svg_attr(elem, "stroke-dasharray");
        if (dasharray && strcmp(dasharray, "none") != 0) {
            float dashes[16];
            int count = 0;
            const char* p = dasharray;
            while (*p && count < 16) {
                while (*p && (isspace(*p) || *p == ',')) p++;
                if (!*p) break;
                dashes[count++] = strtof(p, (char**)&p);
            }
            if (count > 0) {
                const char* dashoffset = get_svg_attr(elem, "stroke-dashoffset");
                float offset = dashoffset ? parse_svg_length(dashoffset, 0) : 0;
                tvg_shape_set_stroke_dash(shape, dashes, count, offset);
            }
        }
    }
}

// ============================================================================
// Apply Transform to ThorVG Paint
// ============================================================================

static void apply_svg_transform(SvgRenderContext* ctx, Tvg_Paint paint, Element* elem) {
    const char* transform_str = get_svg_attr(elem, "transform");
    if (!transform_str) return;

    float m[6];
    if (parse_svg_transform(transform_str, m)) {
        Tvg_Matrix matrix = {
            m[0], m[2], m[4],  // a, c, e
            m[1], m[3], m[5],  // b, d, f
            0, 0, 1
        };
        tvg_paint_set_transform(paint, &matrix);
    }
}

// ============================================================================
// SVG Shape Renderers
// ============================================================================

static Tvg_Paint render_svg_rect(SvgRenderContext* ctx, Element* elem) {
    float x = parse_svg_length(get_svg_attr(elem, "x"), 0);
    float y = parse_svg_length(get_svg_attr(elem, "y"), 0);
    float width = parse_svg_length(get_svg_attr(elem, "width"), 0);
    float height = parse_svg_length(get_svg_attr(elem, "height"), 0);
    float rx = parse_svg_length(get_svg_attr(elem, "rx"), 0);
    float ry = parse_svg_length(get_svg_attr(elem, "ry"), rx);  // default to rx

    if (width <= 0 || height <= 0) return nullptr;

    Tvg_Paint shape = tvg_shape_new();
    tvg_shape_append_rect(shape, x, y, width, height, rx, ry, true);

    apply_svg_fill_stroke(ctx, shape, elem);
    apply_svg_transform(ctx, shape, elem);

    log_debug("[SVG] rect: x=%.1f y=%.1f w=%.1f h=%.1f rx=%.1f", x, y, width, height, rx);
    return shape;
}

static Tvg_Paint render_svg_circle(SvgRenderContext* ctx, Element* elem) {
    float cx = parse_svg_length(get_svg_attr(elem, "cx"), 0);
    float cy = parse_svg_length(get_svg_attr(elem, "cy"), 0);
    float r = parse_svg_length(get_svg_attr(elem, "r"), 0);

    if (r <= 0) return nullptr;

    Tvg_Paint shape = tvg_shape_new();
    tvg_shape_append_circle(shape, cx, cy, r, r, true);

    apply_svg_fill_stroke(ctx, shape, elem);
    apply_svg_transform(ctx, shape, elem);

    log_debug("[SVG] circle: cx=%.1f cy=%.1f r=%.1f", cx, cy, r);
    return shape;
}

static Tvg_Paint render_svg_ellipse(SvgRenderContext* ctx, Element* elem) {
    float cx = parse_svg_length(get_svg_attr(elem, "cx"), 0);
    float cy = parse_svg_length(get_svg_attr(elem, "cy"), 0);
    float rx = parse_svg_length(get_svg_attr(elem, "rx"), 0);
    float ry = parse_svg_length(get_svg_attr(elem, "ry"), 0);

    if (rx <= 0 || ry <= 0) return nullptr;

    Tvg_Paint shape = tvg_shape_new();
    tvg_shape_append_circle(shape, cx, cy, rx, ry, true);

    apply_svg_fill_stroke(ctx, shape, elem);
    apply_svg_transform(ctx, shape, elem);

    log_debug("[SVG] ellipse: cx=%.1f cy=%.1f rx=%.1f ry=%.1f", cx, cy, rx, ry);
    return shape;
}

static Tvg_Paint render_svg_line(SvgRenderContext* ctx, Element* elem) {
    float x1 = parse_svg_length(get_svg_attr(elem, "x1"), 0);
    float y1 = parse_svg_length(get_svg_attr(elem, "y1"), 0);
    float x2 = parse_svg_length(get_svg_attr(elem, "x2"), 0);
    float y2 = parse_svg_length(get_svg_attr(elem, "y2"), 0);

    Tvg_Paint shape = tvg_shape_new();
    tvg_shape_move_to(shape, x1, y1);
    tvg_shape_line_to(shape, x2, y2);

    // lines have stroke only, no fill by default
    const char* stroke = get_svg_attr(elem, "stroke");
    if (!stroke) {
        // SVG default: lines inherit stroke or have none
        // set default black stroke
        tvg_shape_set_stroke_color(shape, 0, 0, 0, 255);
        tvg_shape_set_stroke_width(shape, 1.0f);
    }
    apply_svg_fill_stroke(ctx, shape, elem);
    apply_svg_transform(ctx, shape, elem);

    log_debug("[SVG] line: (%.1f,%.1f) -> (%.1f,%.1f)", x1, y1, x2, y2);
    return shape;
}

// helper: parse points attribute for polyline/polygon
static bool parse_points(const char* points_str, Tvg_Paint shape, bool close_path) {
    if (!points_str || !shape) return false;

    const char* p = points_str;
    float x, y;
    bool first = true;

    while (*p) {
        // skip whitespace and commas
        while (*p && (isspace(*p) || *p == ',')) p++;
        if (!*p) break;

        // parse x
        char* end;
        x = strtof(p, &end);
        if (end == p) break;
        p = end;

        // skip separator
        while (*p && (isspace(*p) || *p == ',')) p++;
        if (!*p) break;

        // parse y
        y = strtof(p, &end);
        if (end == p) break;
        p = end;

        if (first) {
            tvg_shape_move_to(shape, x, y);
            first = false;
        } else {
            tvg_shape_line_to(shape, x, y);
        }
    }

    if (close_path && !first) {
        tvg_shape_close(shape);
    }

    return !first;  // return true if at least one point was parsed
}

static Tvg_Paint render_svg_polyline(SvgRenderContext* ctx, Element* elem, bool close_path) {
    const char* points = get_svg_attr(elem, "points");
    if (!points) return nullptr;

    Tvg_Paint shape = tvg_shape_new();
    if (!parse_points(points, shape, close_path)) {
        tvg_paint_unref(shape, true);
        return nullptr;
    }

    apply_svg_fill_stroke(ctx, shape, elem);
    apply_svg_transform(ctx, shape, elem);

    log_debug("[SVG] %s: points=%s", close_path ? "polygon" : "polyline", points);
    return shape;
}

// ============================================================================
// SVG Path Rendering
// ============================================================================

// helper functions for path parsing
static void skip_wsp_comma(const char** p) {
    while (**p && (isspace(**p) || **p == ',')) (*p)++;
}

static bool peek_number(const char* p) {
    skip_wsp_comma(&p);
    return *p == '-' || *p == '+' || *p == '.' || isdigit(*p);
}

static float parse_number(const char** p) {
    skip_wsp_comma(p);
    char* end;
    float val = strtof(*p, &end);
    *p = end;
    return val;
}

static int parse_flag(const char** p) {
    skip_wsp_comma(p);
    int flag = 0;
    if (**p == '0' || **p == '1') {
        flag = **p - '0';
        (*p)++;
    }
    return flag;
}

// arc to bezier conversion helper
static void arc_to_beziers(Tvg_Paint shape, float x1, float y1,
                           float rx, float ry, float rotation,
                           int large_arc, int sweep, float x2, float y2) {
    // simplified arc: just draw line for now
    // TODO: implement proper arc-to-bezier conversion
    tvg_shape_line_to(shape, x2, y2);
}

static Tvg_Paint render_svg_path(SvgRenderContext* ctx, Element* elem) {
    const char* d = get_svg_attr(elem, "d");
    if (!d || !*d) return nullptr;

    Tvg_Paint shape = tvg_shape_new();

    float cur_x = 0, cur_y = 0;
    float start_x = 0, start_y = 0;
    float last_ctrl_x = 0, last_ctrl_y = 0;
    char last_cmd = 0;

    const char* p = d;

    while (*p) {
        skip_wsp_comma(&p);
        if (!*p) break;

        char cmd = *p;
        bool is_cmd = isalpha(cmd);

        if (is_cmd) {
            p++;
            last_cmd = cmd;
        } else {
            // implicit command - repeat last command
            cmd = last_cmd;
            // after M, implicit command is L
            if (cmd == 'M') cmd = 'L';
            if (cmd == 'm') cmd = 'l';
        }

        bool relative = islower(cmd);
        cmd = toupper(cmd);

        switch (cmd) {
            case 'M': {  // moveto
                float x = parse_number(&p);
                float y = parse_number(&p);
                if (relative) { x += cur_x; y += cur_y; }
                tvg_shape_move_to(shape, x, y);
                cur_x = start_x = x;
                cur_y = start_y = y;
                last_ctrl_x = cur_x;
                last_ctrl_y = cur_y;
                // subsequent coords are implicit lineto
                while (peek_number(p)) {
                    x = parse_number(&p);
                    y = parse_number(&p);
                    if (relative) { x += cur_x; y += cur_y; }
                    tvg_shape_line_to(shape, x, y);
                    cur_x = x; cur_y = y;
                }
                break;
            }
            case 'L': {  // lineto
                while (peek_number(p)) {
                    float x = parse_number(&p);
                    float y = parse_number(&p);
                    if (relative) { x += cur_x; y += cur_y; }
                    tvg_shape_line_to(shape, x, y);
                    cur_x = x; cur_y = y;
                }
                last_ctrl_x = cur_x;
                last_ctrl_y = cur_y;
                break;
            }
            case 'H': {  // horizontal lineto
                while (peek_number(p)) {
                    float x = parse_number(&p);
                    if (relative) { x += cur_x; }
                    tvg_shape_line_to(shape, x, cur_y);
                    cur_x = x;
                }
                last_ctrl_x = cur_x;
                last_ctrl_y = cur_y;
                break;
            }
            case 'V': {  // vertical lineto
                while (peek_number(p)) {
                    float y = parse_number(&p);
                    if (relative) { y += cur_y; }
                    tvg_shape_line_to(shape, cur_x, y);
                    cur_y = y;
                }
                last_ctrl_x = cur_x;
                last_ctrl_y = cur_y;
                break;
            }
            case 'C': {  // cubic bezier
                while (peek_number(p)) {
                    float x1 = parse_number(&p);
                    float y1 = parse_number(&p);
                    float x2 = parse_number(&p);
                    float y2 = parse_number(&p);
                    float x = parse_number(&p);
                    float y = parse_number(&p);
                    if (relative) {
                        x1 += cur_x; y1 += cur_y;
                        x2 += cur_x; y2 += cur_y;
                        x += cur_x; y += cur_y;
                    }
                    tvg_shape_cubic_to(shape, x1, y1, x2, y2, x, y);
                    last_ctrl_x = x2; last_ctrl_y = y2;
                    cur_x = x; cur_y = y;
                }
                break;
            }
            case 'S': {  // smooth cubic bezier
                while (peek_number(p)) {
                    // reflect previous control point
                    float x1 = 2 * cur_x - last_ctrl_x;
                    float y1 = 2 * cur_y - last_ctrl_y;
                    float x2 = parse_number(&p);
                    float y2 = parse_number(&p);
                    float x = parse_number(&p);
                    float y = parse_number(&p);
                    if (relative) {
                        x2 += cur_x; y2 += cur_y;
                        x += cur_x; y += cur_y;
                    }
                    tvg_shape_cubic_to(shape, x1, y1, x2, y2, x, y);
                    last_ctrl_x = x2; last_ctrl_y = y2;
                    cur_x = x; cur_y = y;
                }
                break;
            }
            case 'Q': {  // quadratic bezier -> convert to cubic
                while (peek_number(p)) {
                    float qx = parse_number(&p);
                    float qy = parse_number(&p);
                    float x = parse_number(&p);
                    float y = parse_number(&p);
                    if (relative) {
                        qx += cur_x; qy += cur_y;
                        x += cur_x; y += cur_y;
                    }
                    // convert Q to C: control points at 2/3 along Q handles
                    float cx1 = cur_x + 2.0f/3.0f * (qx - cur_x);
                    float cy1 = cur_y + 2.0f/3.0f * (qy - cur_y);
                    float cx2 = x + 2.0f/3.0f * (qx - x);
                    float cy2 = y + 2.0f/3.0f * (qy - y);
                    tvg_shape_cubic_to(shape, cx1, cy1, cx2, cy2, x, y);
                    last_ctrl_x = qx; last_ctrl_y = qy;
                    cur_x = x; cur_y = y;
                }
                break;
            }
            case 'T': {  // smooth quadratic bezier
                while (peek_number(p)) {
                    float qx = 2 * cur_x - last_ctrl_x;
                    float qy = 2 * cur_y - last_ctrl_y;
                    float x = parse_number(&p);
                    float y = parse_number(&p);
                    if (relative) {
                        x += cur_x; y += cur_y;
                    }
                    float cx1 = cur_x + 2.0f/3.0f * (qx - cur_x);
                    float cy1 = cur_y + 2.0f/3.0f * (qy - cur_y);
                    float cx2 = x + 2.0f/3.0f * (qx - x);
                    float cy2 = y + 2.0f/3.0f * (qy - y);
                    tvg_shape_cubic_to(shape, cx1, cy1, cx2, cy2, x, y);
                    last_ctrl_x = qx; last_ctrl_y = qy;
                    cur_x = x; cur_y = y;
                }
                break;
            }
            case 'A': {  // arc
                while (peek_number(p)) {
                    float rx = parse_number(&p);
                    float ry = parse_number(&p);
                    float rotation = parse_number(&p);
                    int large_arc = parse_flag(&p);
                    int sweep = parse_flag(&p);
                    float x = parse_number(&p);
                    float y = parse_number(&p);
                    if (relative) { x += cur_x; y += cur_y; }

                    arc_to_beziers(shape, cur_x, cur_y, rx, ry, rotation, large_arc, sweep, x, y);
                    cur_x = x; cur_y = y;
                }
                last_ctrl_x = cur_x;
                last_ctrl_y = cur_y;
                break;
            }
            case 'Z': {  // closepath
                tvg_shape_close(shape);
                cur_x = start_x;
                cur_y = start_y;
                last_ctrl_x = cur_x;
                last_ctrl_y = cur_y;
                break;
            }
            default:
                // skip unknown command
                break;
        }
    }

    apply_svg_fill_stroke(ctx, shape, elem);
    apply_svg_transform(ctx, shape, elem);

    log_debug("[SVG] path: d=%s", d);
    return shape;
}

// ============================================================================
// SVG Text Rendering (requires ThorVG with TTF loader)
// ============================================================================

/**
 * Resolve font path from font-family name
 * Uses platform-specific font lookup
 * out_font_name: returns the actual font name used (may be different due to fallback)
 */
static char* resolve_svg_font_path(const char* font_family, const char** out_font_name) {
    // default font name
    const char* used_font_name = font_family;

    if (!font_family || !*font_family) {
        // default to a common sans-serif font
        font_family = "Arial";
        used_font_name = "Arial";
    }

    // try platform font lookup
    char* path = font_platform_find_fallback(font_family);

    // check if path is a TTC file - ThorVG TTF loader doesn't support TTC (TrueType Collection)
    if (path && strstr(path, ".ttc")) {
        log_debug("[SVG] skipping TTC file (not supported by ThorVG TTF loader): %s", path);
        mem_free(path);
        path = nullptr;
    }

    if (path) {
        if (out_font_name) *out_font_name = used_font_name;
        return path;
    }

    // try common fallbacks - prefer simple TTF files that ThorVG can load
    // avoid fonts that are typically in TTC format
    static const char* fallbacks[] = {
        "Arial",             // /System/Library/Fonts/Supplemental/Arial.ttf
        "SFNS",              // /System/Library/Fonts/SFNS.ttf (macOS) - font name is "SF NS"
        "Geneva",            // /System/Library/Fonts/Geneva.ttf (macOS)
        "Arial Unicode MS",  // /System/Library/Fonts/Supplemental/Arial Unicode.ttf
        "DejaVu Sans",       // Linux
        "Liberation Sans",   // Linux
        "Noto Sans",         // Linux
        nullptr
    };

    for (int i = 0; fallbacks[i]; i++) {
        if (strcmp(fallbacks[i], font_family) != 0) {
            path = font_platform_find_fallback(fallbacks[i]);

            // skip TTC files
            if (path && strstr(path, ".ttc")) {
                log_debug("[SVG] skipping TTC file (not supported): %s", path);
                mem_free(path);
                path = nullptr;
                continue;
            }

            if (path) {
                log_debug("[SVG] font fallback: %s -> %s", font_family, fallbacks[i]);
                used_font_name = fallbacks[i];
                if (out_font_name) *out_font_name = used_font_name;
                return path;
            }
        }
    }

    log_debug("[SVG] no font found for: %s", font_family);
    if (out_font_name) *out_font_name = nullptr;
    return nullptr;
}

/**
 * Check if a string is only whitespace
 */
static bool is_whitespace_only(const char* str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (!isspace((unsigned char)str[i])) return false;
    }
    return true;
}

/**
 * Trim leading and trailing whitespace from a string
 * Returns a newly allocated trimmed string, or nullptr if result is empty
 */
static char* trim_whitespace(const char* str, size_t len) {
    if (!str || len == 0) return nullptr;

    // skip leading whitespace
    size_t start = 0;
    while (start < len && isspace((unsigned char)str[start])) start++;

    // skip trailing whitespace
    size_t end = len;
    while (end > start && isspace((unsigned char)str[end - 1])) end--;

    if (end <= start) return nullptr;  // all whitespace

    size_t trimmed_len = end - start;
    char* result = (char*)malloc(trimmed_len + 1);
    if (!result) return nullptr;

    memcpy(result, str + start, trimmed_len);
    result[trimmed_len] = '\0';
    return result;
}

/**
 * Get direct text content from an SVG element (non-recursive, single string node only)
 * Used for getting text content from a tspan without recursing into children
 * Returns trimmed content, skipping whitespace-only nodes
 */
static const char* get_direct_text_content(Element* elem) {
    if (!elem || elem->length == 0) return nullptr;

    for (int64_t i = 0; i < elem->length; i++) {
        Item child = elem->items[i];
        TypeId type = get_type_id(child);

        if (type == LMD_TYPE_STRING) {
            String* str = (String*)child.item;
            if (str && str->chars && str->len > 0) {
                // skip whitespace-only nodes
                if (is_whitespace_only(str->chars, str->len)) continue;
                return trim_whitespace(str->chars, str->len);
            }
        }
    }
    return nullptr;
}

/**
 * Create a single ThorVG text object with specified properties
 * Note: font_size is in CSS pixels, but ThorVG uses points internally.
 * We convert: points = pixels * 72/96 = pixels * 0.75
 */
static Tvg_Paint create_text_segment(const char* text, float x, float y,
                                     const char* font_path, const char* font_name,
                                     float font_size_px, Color fill_color) {
    if (!text || !*text || !font_path) return nullptr;

    // convert CSS pixels to points for ThorVG
    // 1 CSS pixel = 1/96 inch, 1 point = 1/72 inch
    // points = pixels * 72 / 96 = pixels * 0.75
    float font_size_pt = font_size_px * 0.75f;

    Tvg_Paint tvg_text = tvg_text_new();
    if (!tvg_text) return nullptr;

    // load font (ThorVG caches it)
    Tvg_Result load_result = tvg_font_load(font_path);
    if (load_result != TVG_RESULT_SUCCESS) {
        log_debug("[SVG] failed to load font file: %s (result=%d)", font_path, load_result);
        tvg_paint_unref(tvg_text, true);
        return nullptr;
    }

    log_debug("[SVG TEXT] loaded font file: %s, setting font name: '%s'", font_path, font_name ? font_name : "null");

    // set font by name - ThorVG matches the font name from the loaded font file
    // common font names: "Arial", "Helvetica", "SF NS", "Geneva", etc.
    Tvg_Result result = tvg_text_set_font(tvg_text, font_name);
    if (result != TVG_RESULT_SUCCESS) {
        // if font name fails, try nullptr as fallback
        log_debug("[SVG TEXT] font name '%s' not found (result=%d), trying nullptr fallback", font_name ? font_name : "null", result);
        result = tvg_text_set_font(tvg_text, nullptr);
        if (result != TVG_RESULT_SUCCESS) {
            log_debug("[SVG] failed to set font (result=%d)", result);
            tvg_paint_unref(tvg_text, true);
            return nullptr;
        }
    } else {
        log_debug("[SVG TEXT] successfully set font name: '%s'", font_name);
    }

    result = tvg_text_set_size(tvg_text, font_size_pt);
    if (result != TVG_RESULT_SUCCESS) {
        tvg_paint_unref(tvg_text, true);
        return nullptr;
    }

    result = tvg_text_set_text(tvg_text, text);
    if (result != TVG_RESULT_SUCCESS) {
        tvg_paint_unref(tvg_text, true);
        return nullptr;
    }

    // set fill color
    if (fill_color.a > 0) {
        tvg_text_set_color(tvg_text, fill_color.r, fill_color.g, fill_color.b);
        if (fill_color.a < 255) {
            tvg_paint_set_opacity(tvg_text, fill_color.a);
        }
    }

    // position the text
    tvg_paint_translate(tvg_text, x, y);

    log_debug("[SVG] text segment: '%s' at (%.1f, %.1f) size=%.1fpx (%.1fpt) color=rgb(%d,%d,%d)",
              text, x, y, font_size_px, font_size_pt, fill_color.r, fill_color.g, fill_color.b);

    return tvg_text;
}

/**
 * Estimate text width for horizontal positioning of tspans
 * This is approximate - proper metrics would require ThorVG API or font metrics
 */
static float estimate_text_width(const char* text, float font_size) {
    if (!text) return 0;
    // rough estimate: average character width is about 0.5-0.6 of font size
    size_t len = strlen(text);
    return len * font_size * 0.55f;
}

/**
 * Render SVG <text> element with proper tspan support
 * Each tspan gets its own color and position
 */
static Tvg_Paint render_svg_text(SvgRenderContext* ctx, Element* elem) {
    if (!elem) return nullptr;

    // parse parent text attributes
    float base_x = parse_svg_length(get_svg_attr(elem, "x"), 0);
    float base_y = parse_svg_length(get_svg_attr(elem, "y"), 0);

    const char* font_family = get_svg_attr(elem, "font-family");
    float font_size = parse_svg_length(get_svg_attr(elem, "font-size"), 16);

    // get default fill from parent
    const char* parent_fill = get_svg_attr(elem, "fill");
    if (!parent_fill) parent_fill = "black";
    Color default_fill = parse_svg_color(parent_fill);

    // resolve font path and name
    const char* font_name = nullptr;
    char* font_path = resolve_svg_font_path(font_family, &font_name);
    if (!font_path) {
        log_debug("[SVG] <text> no font available for: %s", font_family ? font_family : "default");
        return nullptr;
    }

    // count children to see if we need a scene
    int text_segments = 0;
    bool has_tspan = false;

    for (int64_t i = 0; i < elem->length; i++) {
        Item child = elem->items[i];
        TypeId type = get_type_id(child);
        if (type == LMD_TYPE_STRING) {
            text_segments++;
        } else if (type == LMD_TYPE_ELEMENT) {
            Element* child_elem = child.element;
            if (child_elem && child_elem->type) {
                TypeElmt* child_type = (TypeElmt*)child_elem->type;
                if (child_type->name.str && strcmp(child_type->name.str, "tspan") == 0) {
                    text_segments++;
                    has_tspan = true;
                }
            }
        }
    }

    if (text_segments == 0) {
        free(font_path);
        return nullptr;
    }

    // if single text with no tspan, use simple rendering
    if (text_segments == 1 && !has_tspan) {
        const char* text_content = get_direct_text_content(elem);
        if (text_content) {
            Tvg_Paint text = create_text_segment(text_content, base_x, base_y,
                                                  font_path, font_name, font_size, default_fill);
            free((void*)text_content);
            free(font_path);
            if (text) {
                apply_svg_transform(ctx, text, elem);
            }
            return text;
        }
    }

    // multiple segments - create a scene
    Tvg_Paint scene = tvg_scene_new();
    if (!scene) {
        free(font_path);
        return nullptr;
    }

    float cur_x = base_x;
    float cur_y = base_y;

    for (int64_t i = 0; i < elem->length; i++) {
        Item child = elem->items[i];
        TypeId type = get_type_id(child);

        if (type == LMD_TYPE_STRING) {
            // direct text node - skip whitespace-only
            String* str = (String*)child.item;
            if (str && str->chars && str->len > 0) {
                if (is_whitespace_only(str->chars, str->len)) continue;
                char* text_copy = trim_whitespace(str->chars, str->len);
                if (text_copy) {
                    Tvg_Paint text_obj = create_text_segment(text_copy, cur_x, cur_y,
                                                              font_path, font_name, font_size, default_fill);
                    if (text_obj) {
                        tvg_scene_push(scene, text_obj);
                        cur_x += estimate_text_width(text_copy, font_size);
                    }
                    free(text_copy);
                }
            }
        } else if (type == LMD_TYPE_ELEMENT) {
            Element* child_elem = child.element;
            if (!child_elem || !child_elem->type) continue;

            TypeElmt* child_type = (TypeElmt*)child_elem->type;
            const char* tag = child_type->name.str;

            if (tag && strcmp(tag, "tspan") == 0) {
                // get tspan-specific attributes
                const char* tspan_x = get_svg_attr(child_elem, "x");
                const char* tspan_y = get_svg_attr(child_elem, "y");
                const char* tspan_dx = get_svg_attr(child_elem, "dx");
                const char* tspan_dy = get_svg_attr(child_elem, "dy");

                // update position
                if (tspan_x) cur_x = parse_svg_length(tspan_x, cur_x);
                if (tspan_y) cur_y = parse_svg_length(tspan_y, cur_y);
                if (tspan_dx) cur_x += parse_svg_length(tspan_dx, 0);
                if (tspan_dy) cur_y += parse_svg_length(tspan_dy, 0);

                // get tspan fill color (inherit from parent if not specified)
                const char* tspan_fill = get_svg_attr(child_elem, "fill");
                Color fill = tspan_fill ? parse_svg_color(tspan_fill) : default_fill;

                // check for fill="none"
                if (tspan_fill && strcmp(tspan_fill, "none") == 0) {
                    fill.a = 0;
                }

                // get tspan font-size (inherit from parent if not specified)
                const char* tspan_font_size_str = get_svg_attr(child_elem, "font-size");
                float tspan_font_size = tspan_font_size_str ?
                    parse_svg_length(tspan_font_size_str, font_size) : font_size;

                // get text content
                const char* text_content = get_direct_text_content(child_elem);
                if (text_content && *text_content) {
                    Tvg_Paint text_obj = create_text_segment(text_content, cur_x, cur_y,
                                                              font_path, font_name, tspan_font_size, fill);
                    if (text_obj) {
                        tvg_scene_push(scene, text_obj);
                        cur_x += estimate_text_width(text_content, tspan_font_size);
                    }
                    free((void*)text_content);
                }
            }
        }
    }

    // apply parent transform to the scene
    apply_svg_transform(ctx, scene, elem);

    free(font_path);

    log_debug("[SVG] <text> rendered with %d segments at base (%.1f, %.1f)",
              text_segments, base_x, base_y);

    return scene;
}

// ============================================================================
// SVG Image Rendering (using Radiant's unified image loading)
// ============================================================================

/**
 * Render SVG <image> element using Radiant's image loading infrastructure
 * Images are loaded via Radiant's load_image() and converted to ThorVG pictures
 */
static Tvg_Paint render_svg_image(SvgRenderContext* ctx, Element* elem) {
    if (!elem) return nullptr;

    // get href attribute (SVG 2 uses href, SVG 1.1 uses xlink:href)
    const char* href = get_svg_attr(elem, "href");
    if (!href) href = get_svg_attr(elem, "xlink:href");
    if (!href || !*href) {
        log_debug("[SVG] <image> missing href attribute");
        return nullptr;
    }

    // parse position and size
    float x = parse_svg_length(get_svg_attr(elem, "x"), 0);
    float y = parse_svg_length(get_svg_attr(elem, "y"), 0);
    float width = parse_svg_length(get_svg_attr(elem, "width"), 0);
    float height = parse_svg_length(get_svg_attr(elem, "height"), 0);

    // TODO: integrate with Radiant's load_image() when UiContext is available
    // For now, use ThorVG's picture loading for SVG images
    Tvg_Paint pic = tvg_picture_new();
    if (!pic) return nullptr;

    Tvg_Result result = tvg_picture_load(pic, href);
    if (result != TVG_RESULT_SUCCESS) {
        log_debug("[SVG] <image> failed to load: %s", href);
        tvg_paint_unref(pic, true);
        return nullptr;
    }

    // set size if specified
    if (width > 0 && height > 0) {
        tvg_picture_set_size(pic, width, height);
    }

    // position the image
    tvg_paint_translate(pic, x, y);

    // apply transform if present
    apply_svg_transform(ctx, pic, elem);

    // apply opacity if present
    const char* opacity = get_svg_attr(elem, "opacity");
    if (opacity) {
        float op = strtof(opacity, nullptr);
        tvg_paint_set_opacity(pic, (uint8_t)(op * 255));
    }

    log_debug("[SVG] <image> loaded: %s at (%.1f, %.1f) size %.1fx%.1f",
              href, x, y, width, height);

    return pic;
}

// ============================================================================
// SVG Group and Children
// ============================================================================

static Tvg_Paint render_svg_group(SvgRenderContext* ctx, Element* elem) {
    if (!elem) return nullptr;

    // save current inherited state
    Color saved_fill = ctx->fill_color;
    Color saved_stroke = ctx->stroke_color;
    float saved_stroke_width = ctx->stroke_width;
    bool saved_fill_none = ctx->fill_none;
    bool saved_stroke_none = ctx->stroke_none;

    // update inherited state from group attributes
    const char* fill = get_svg_attr(elem, "fill");
    if (fill) {
        if (strcmp(fill, "none") == 0) {
            ctx->fill_none = true;
        } else if (strncmp(fill, "url(#", 5) != 0) {
            ctx->fill_color = parse_svg_color(fill);
            ctx->fill_none = false;
        }
    }

    const char* stroke = get_svg_attr(elem, "stroke");
    if (stroke) {
        if (strcmp(stroke, "none") == 0) {
            ctx->stroke_none = true;
        } else {
            ctx->stroke_color = parse_svg_color(stroke);
            ctx->stroke_none = false;
        }
    }

    const char* stroke_width = get_svg_attr(elem, "stroke-width");
    if (stroke_width) {
        ctx->stroke_width = parse_svg_length(stroke_width, 1.0f);
    }

    // render children with updated inherited state
    Tvg_Paint scene = render_svg_children_as_scene(ctx, elem);

    // restore inherited state
    ctx->fill_color = saved_fill;
    ctx->stroke_color = saved_stroke;
    ctx->stroke_width = saved_stroke_width;
    ctx->fill_none = saved_fill_none;
    ctx->stroke_none = saved_stroke_none;

    return scene;
}

static Tvg_Paint render_svg_children_as_scene(SvgRenderContext* ctx, Element* elem) {
    if (!elem || elem->length == 0) return nullptr;

    Tvg_Paint scene = tvg_scene_new();
    int child_count = 0;

    for (int64_t i = 0; i < elem->length; i++) {
        Element* child = get_child_element_at(elem, i);
        if (!child) continue;

        Tvg_Paint child_paint = render_svg_element(ctx, child);
        if (child_paint) {
            tvg_scene_push(scene, child_paint);
            child_count++;
        }
    }

    if (child_count == 0) {
        tvg_paint_unref(scene, true);
        return nullptr;
    }

    // apply group transform
    apply_svg_transform(ctx, scene, elem);

    return scene;
}

// ============================================================================
// SVG Defs Processing
// ============================================================================

static void process_svg_defs(SvgRenderContext* ctx, Element* defs) {
    // TODO: implement gradient and clipPath definitions
    // For now, we skip <defs> processing
    log_debug("[SVG] defs processing not yet implemented");
}

// ============================================================================
// Main SVG Element Dispatcher
// ============================================================================

static Tvg_Paint render_svg_element(SvgRenderContext* ctx, Element* elem) {
    if (!elem) return nullptr;

    const char* tag = get_element_tag_name(elem);
    if (!tag) return nullptr;

    log_debug("[SVG] rendering element: %s", tag);

    if (strcmp(tag, "rect") == 0) {
        return render_svg_rect(ctx, elem);
    } else if (strcmp(tag, "circle") == 0) {
        return render_svg_circle(ctx, elem);
    } else if (strcmp(tag, "ellipse") == 0) {
        return render_svg_ellipse(ctx, elem);
    } else if (strcmp(tag, "line") == 0) {
        return render_svg_line(ctx, elem);
    } else if (strcmp(tag, "polyline") == 0) {
        return render_svg_polyline(ctx, elem, false);
    } else if (strcmp(tag, "polygon") == 0) {
        return render_svg_polyline(ctx, elem, true);
    } else if (strcmp(tag, "path") == 0) {
        return render_svg_path(ctx, elem);
    } else if (strcmp(tag, "g") == 0) {
        return render_svg_group(ctx, elem);
    } else if (strcmp(tag, "defs") == 0) {
        process_svg_defs(ctx, elem);
        return nullptr;  // defs don't render
    } else if (strcmp(tag, "linearGradient") == 0 ||
               strcmp(tag, "radialGradient") == 0 ||
               strcmp(tag, "clipPath") == 0 ||
               strcmp(tag, "mask") == 0 ||
               strcmp(tag, "symbol") == 0 ||
               strcmp(tag, "pattern") == 0) {
        // these are definitions, don't render directly
        return nullptr;
    } else if (strcmp(tag, "use") == 0) {
        // TODO: implement use element (clone referenced element)
        log_debug("[SVG] <use> element not yet implemented");
        return nullptr;
    } else if (strcmp(tag, "text") == 0) {
        return render_svg_text(ctx, elem);
    } else if (strcmp(tag, "image") == 0) {
        return render_svg_image(ctx, elem);
    } else {
        // unknown element - try rendering children
        return render_svg_children_as_scene(ctx, elem);
    }
}

// ============================================================================
// Build SVG Scene
// ============================================================================

Tvg_Paint build_svg_scene(Element* svg_element, float viewport_width, float viewport_height, Pool* pool, float pixel_ratio) {
    if (!svg_element) return nullptr;

    log_debug("[SVG] build_svg_scene: viewport %.0fx%.0f pixel_ratio=%.2f", viewport_width, viewport_height, pixel_ratio);

    // initialize render context
    SvgRenderContext ctx = {};
    ctx.svg_root = svg_element;
    ctx.pool = pool;
    ctx.pixel_ratio = (pixel_ratio > 0) ? pixel_ratio : 1.0f;  // ensure valid pixel ratio
    ctx.fill_color.r = 0; ctx.fill_color.g = 0; ctx.fill_color.b = 0; ctx.fill_color.a = 255;  // default black
    ctx.stroke_color.r = 0; ctx.stroke_color.g = 0; ctx.stroke_color.b = 0; ctx.stroke_color.a = 0;  // default none
    ctx.stroke_width = 1.0f;
    ctx.opacity = 1.0f;
    ctx.fill_none = false;
    ctx.stroke_none = true;

    // parse viewBox
    const char* viewbox_attr = get_svg_attr(svg_element, "viewBox");
    SvgViewBox vb = parse_svg_viewbox(viewbox_attr);

    if (vb.has_viewbox && vb.width > 0 && vb.height > 0) {
        ctx.viewbox_x = vb.min_x;
        ctx.viewbox_y = vb.min_y;
        ctx.viewbox_width = vb.width;
        ctx.viewbox_height = vb.height;
        ctx.scale_x = viewport_width / vb.width;
        ctx.scale_y = viewport_height / vb.height;
        // use uniform scale to preserve aspect ratio (TODO: handle preserveAspectRatio)
        float scale = ctx.scale_x < ctx.scale_y ? ctx.scale_x : ctx.scale_y;
        ctx.scale_x = ctx.scale_y = scale;
        ctx.translate_x = -vb.min_x * scale;
        ctx.translate_y = -vb.min_y * scale;
    } else {
        ctx.scale_x = ctx.scale_y = 1.0f;
        ctx.translate_x = ctx.translate_y = 0;
    }

    // create root scene
    Tvg_Paint scene = tvg_scene_new();

    // apply viewBox transform
    if (vb.has_viewbox) {
        Tvg_Matrix matrix = {
            ctx.scale_x, 0, ctx.translate_x,
            0, ctx.scale_y, ctx.translate_y,
            0, 0, 1
        };
        tvg_paint_set_transform(scene, &matrix);
    }

    // render children
    for (int64_t i = 0; i < svg_element->length; i++) {
        Element* child = get_child_element_at(svg_element, i);
        if (!child) continue;

        Tvg_Paint child_paint = render_svg_element(&ctx, child);
        if (child_paint) {
            tvg_scene_push(scene, child_paint);
        }
    }

    log_debug("[SVG] build_svg_scene complete");
    return scene;
}

// ============================================================================
// Render Inline SVG
// ============================================================================

void render_inline_svg(RenderContext* rdcon, ViewBlock* view) {
    if (!rdcon || !view) return;

    // ViewBlock inherits from DomElement, so we can cast directly
    DomElement* dom_elem = static_cast<DomElement*>(view);
    if (!dom_elem->native_element) {
        log_debug("[SVG] render_inline_svg: no native element");
        return;
    }

    Element* svg_elem = dom_elem->native_element;
    float scale = rdcon->scale;

    log_debug("[SVG] render_inline_svg: view pos=(%.0f,%.0f) size=(%.0f,%.0f) pixel_ratio=%.2f",
              view->x, view->y, view->width, view->height, scale);

    // build ThorVG scene from SVG element tree
    // pass pixel_ratio so text sizes can be adjusted (divided by pixel_ratio)
    // since the entire scene will be scaled by pixel_ratio after building
    Tvg_Paint svg_scene = build_svg_scene(svg_elem, view->width, view->height,
                                            rdcon->ui_context->document->pool, scale);
    if (!svg_scene) {
        log_debug("[SVG] render_inline_svg: failed to build scene");
        return;
    }

    // position in document coordinates
    float x = rdcon->block.x + view->x * scale;
    float y = rdcon->block.y + view->y * scale;

    tvg_paint_translate(svg_scene, x, y);
    tvg_paint_scale(svg_scene, scale);

    // apply document transform if any
    if (rdcon->has_transform) {
        tvg_paint_set_transform(svg_scene, &rdcon->transform);
    }

    // render immediately to buffer (same pattern as SVG images)
    tvg_canvas_remove(rdcon->canvas, NULL);  // clear any existing shapes
    tvg_canvas_push(rdcon->canvas, svg_scene);
    tvg_canvas_reset_and_draw(rdcon, false);
    tvg_canvas_remove(rdcon->canvas, NULL);  // clear shapes after rendering

    log_debug("[SVG] render_inline_svg: rendered to buffer");
}
