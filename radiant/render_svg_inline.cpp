/**
 * render_svg_inline.cpp - Inline SVG Rendering via RdtVector
 *
 * Converts SVG element trees directly to rdt_ draw calls.
 * No ThorVG scene tree is constructed — shapes are drawn immediately
 * to the target RdtVector with accumulated transforms.
 */

#include "render_svg_inline.hpp"
#include "render.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/log.h"
#include "../lib/font/font.h"
#include "../lib/font/font_internal.h"
#include "../lib/strbuf.h"
#include "../lib/str.h"
#include <string.h>
#include "../lib/mem.h"
#include "../lib/base64.h"
#include <ctype.h>
#include <math.h>

#ifndef LAMBDA_HEADLESS
#include <thorvg_capi.h>  // needed for SVG text rendering (tvg_text_* API)
#endif

// ============================================================================
// Forward Declarations
// ============================================================================

// ---------------------------------------------------------------------------
// SVG display-list dispatch helpers (ctx->dl aware)
// ---------------------------------------------------------------------------
static inline void svg_fill_path(SvgRenderContext* ctx, RdtPath* path, Color color,
                                 RdtFillRule rule, const RdtMatrix* xform) {
    if (ctx->dl) dl_fill_path(ctx->dl, path, color, rule, xform);
    else rdt_fill_path(ctx->vec, path, color, rule, xform);
}
static inline void svg_stroke_path(SvgRenderContext* ctx, RdtPath* path, Color color, float width,
                                   RdtStrokeCap cap, RdtStrokeJoin join,
                                   const float* dash, int dash_count, const RdtMatrix* xform) {
    if (ctx->dl) dl_stroke_path(ctx->dl, path, color, width, cap, join, dash, dash_count, 0, xform);
    else rdt_stroke_path(ctx->vec, path, color, width, cap, join, dash, dash_count, 0, xform);
}
static inline void svg_fill_linear_gradient(SvgRenderContext* ctx, RdtPath* path,
                                            float x1, float y1, float x2, float y2,
                                            const RdtGradientStop* stops, int count,
                                            RdtFillRule rule, const RdtMatrix* xform) {
    if (ctx->dl) dl_fill_linear_gradient(ctx->dl, path, x1, y1, x2, y2, stops, count, rule, xform);
    else rdt_fill_linear_gradient(ctx->vec, path, x1, y1, x2, y2, stops, count, rule, xform);
}
static inline void svg_fill_radial_gradient(SvgRenderContext* ctx, RdtPath* path,
                                            float cx, float cy, float r,
                                            const RdtGradientStop* stops, int count,
                                            RdtFillRule rule, const RdtMatrix* xform) {
    if (ctx->dl) dl_fill_radial_gradient(ctx->dl, path, cx, cy, r, stops, count, rule, xform);
    else rdt_fill_radial_gradient(ctx->vec, path, cx, cy, r, stops, count, rule, xform);
}
static inline void svg_draw_picture(SvgRenderContext* ctx, RdtPicture* pic,
                                    uint8_t opacity, const RdtMatrix* xform) {
    if (ctx->dl) dl_draw_picture(ctx->dl, pic, opacity, xform);
    else rdt_picture_draw(ctx->vec, pic, opacity, xform);
}
static inline void svg_push_clip(SvgRenderContext* ctx, RdtPath* path, const RdtMatrix* xform) {
    if (ctx->dl) dl_push_clip(ctx->dl, path, xform);
    else rdt_push_clip(ctx->vec, path, xform);
}
static inline void svg_pop_clip(SvgRenderContext* ctx) {
    if (ctx->dl) dl_pop_clip(ctx->dl);
    else rdt_pop_clip(ctx->vec);
}

static void render_svg_element(SvgRenderContext* ctx, Element* elem);
static void render_svg_rect(SvgRenderContext* ctx, Element* elem);
static void render_svg_circle(SvgRenderContext* ctx, Element* elem);
static void render_svg_ellipse(SvgRenderContext* ctx, Element* elem);
static void render_svg_line(SvgRenderContext* ctx, Element* elem);
static void render_svg_polyline(SvgRenderContext* ctx, Element* elem, bool close_path);
static void render_svg_path(SvgRenderContext* ctx, Element* elem);
static void render_svg_text(SvgRenderContext* ctx, Element* elem);
static void render_svg_image(SvgRenderContext* ctx, Element* elem);
static void render_svg_group(SvgRenderContext* ctx, Element* elem);
static void render_svg_children(SvgRenderContext* ctx, Element* elem);
static void process_svg_defs(SvgRenderContext* ctx, Element* defs);

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

                // handle rotate(angle, cx, cy) with pivot point
                while (*p && (isspace(*p) || *p == ',')) p++;
                if (*p && *p != ')') {
                    float cx = strtof(p, (char**)&p);
                    while (*p && (isspace(*p) || *p == ',')) p++;
                    float cy = strtof(p, (char**)&p);
                    // rotate(angle, cx, cy) = translate(cx,cy) * rotate(angle) * translate(-cx,-cy)
                    local[4] = cx * (1.0f - c_val) + cy * s_val;
                    local[5] = -cx * s_val + cy * (1.0f - c_val);
                }
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
// SVG Defs: Gradient definitions and element refs
// ============================================================================

#define SVG_MAX_GRAD_DEFS  64
#define SVG_MAX_GRAD_STOPS 64
#define SVG_MAX_ELEM_DEFS  64

struct SvgGradStop {
    float   offset;
    Color   color;
};

struct SvgGradDef {
    char  id[128];
    bool  is_radial;
    bool  user_space;          // true = gradientUnits=userSpaceOnUse
    float x1, y1, x2, y2;     // linear gradient endpoints
    float cx, cy, r;           // radial gradient center + radius
    SvgGradStop stops[SVG_MAX_GRAD_STOPS];
    int   stop_count;
};

struct SvgElemDef {
    char     id[128];
    Element* elem;
};

struct SvgDefTable {
    SvgGradDef grads[SVG_MAX_GRAD_DEFS];
    int        grad_count;
    SvgElemDef elems[SVG_MAX_ELEM_DEFS];
    int        elem_count;
};

static float parse_svg_pct_or_num(const char* s, float fallback) {
    if (!s || !*s) return fallback;
    char* end;
    float v = strtof(s, &end);
    if (end == s) return fallback;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end == '%') v /= 100.0f;
    return v;
}

static SvgGradDef* lookup_grad_def(SvgDefTable* table, const char* id) {
    if (!table || !id) return nullptr;
    for (int i = 0; i < table->grad_count; i++) {
        if (strcmp(table->grads[i].id, id) == 0) return &table->grads[i];
    }
    return nullptr;
}

static Element* lookup_elem_def(SvgDefTable* table, const char* id) {
    if (!table || !id) return nullptr;
    for (int i = 0; i < table->elem_count; i++) {
        if (strcmp(table->elems[i].id, id) == 0) return table->elems[i].elem;
    }
    return nullptr;
}

// ============================================================================
// Compose element transform with accumulated context transform
// ============================================================================

static RdtMatrix compose_element_transform(SvgRenderContext* ctx, Element* elem) {
    const char* transform_str = get_svg_attr(elem, "transform");
    if (!transform_str) return ctx->transform;

    float m[6];
    if (!parse_svg_transform(transform_str, m)) return ctx->transform;

    RdtMatrix local = {
        m[0], m[2], m[4],  // a, c, e
        m[1], m[3], m[5],  // b, d, f
        0, 0, 1
    };
    return rdt_matrix_multiply(&ctx->transform, &local);
}

// ============================================================================
// Apply gradient fill via rdt_ API
// ============================================================================

static void draw_gradient_fill(SvgRenderContext* ctx, RdtPath* path, SvgGradDef* def,
                               float bx, float by, float bw, float bh,
                               const RdtMatrix* transform) {
    if (!path || !def || def->stop_count < 2) return;

    RdtGradientStop stops[SVG_MAX_GRAD_STOPS];
    for (int i = 0; i < def->stop_count; i++) {
        stops[i].offset = def->stops[i].offset;
        stops[i].r = def->stops[i].color.r;
        stops[i].g = def->stops[i].color.g;
        stops[i].b = def->stops[i].color.b;
        stops[i].a = def->stops[i].color.a;
    }

    if (def->is_radial) {
        float cx, cy, r;
        if (def->user_space) {
            cx = def->cx; cy = def->cy; r = def->r;
        } else {
            cx = bx + def->cx * bw;
            cy = by + def->cy * bh;
            r  = def->r * (bw < bh ? bw : bh);
        }
        svg_fill_radial_gradient(ctx, path, cx, cy, r,
                                 stops, def->stop_count, RDT_FILL_WINDING, transform);
    } else {
        float x1, y1, x2, y2;
        if (def->user_space) {
            x1 = def->x1; y1 = def->y1; x2 = def->x2; y2 = def->y2;
        } else {
            x1 = bx + def->x1 * bw; y1 = by + def->y1 * bh;
            x2 = bx + def->x2 * bw; y2 = by + def->y2 * bh;
        }
        svg_fill_linear_gradient(ctx, path, x1, y1, x2, y2,
                                 stops, def->stop_count, RDT_FILL_WINDING, transform);
    }
}

// ============================================================================
// Draw fill and stroke for an SVG shape via rdt_ API
// ============================================================================

static void draw_svg_fill_stroke(SvgRenderContext* ctx, RdtPath* path, Element* elem,
                                  const RdtMatrix* transform,
                                  float bx, float by, float bw, float bh) {
    if (!path || !elem) return;

    // --- FILL ---
    const char* fill = get_svg_attr(elem, "fill");
    Color fc;
    bool has_fill = true;
    bool gradient_applied = false;

    if (fill) {
        if (strcmp(fill, "none") == 0) {
            has_fill = false;
        } else if (strncmp(fill, "url(#", 5) == 0) {
            // gradient reference
            if (ctx->defs) {
                const char* id_start = fill + 5;
                const char* id_end   = strchr(id_start, ')');
                char id_buf[128]     = {};
                size_t id_len = id_end ? (size_t)(id_end - id_start) : strlen(id_start);
                if (id_len < sizeof(id_buf)) {
                    memcpy(id_buf, id_start, id_len);
                    SvgGradDef* def = lookup_grad_def((SvgDefTable*)ctx->defs, id_buf);
                    if (def && def->stop_count >= 2) {
                        draw_gradient_fill(ctx, path, def, bx, by, bw, bh, transform);
                        gradient_applied = true;
                        has_fill = false;
                    }
                }
            }
            if (!gradient_applied) {
                // unresolved url() reference - per SVG spec, this should NOT
                // fall back to a default solid color (black); skip the fill.
                log_debug("[SVG] gradient fill not resolved: %s (skip fill)", fill);
                has_fill = false;
            }
        } else if (strcmp(fill, "currentColor") == 0) {
            fc = ctx->current_color;
        } else {
            fc = parse_svg_color(fill);
        }
    } else if (!ctx->fill_none) {
        fc = ctx->fill_color;
    } else {
        has_fill = false;
    }

    if (has_fill) {
        const char* fill_opacity = get_svg_attr(elem, "fill-opacity");
        if (fill_opacity) {
            float opacity = strtof(fill_opacity, nullptr);
            fc.a = (uint8_t)(fc.a * opacity);
        }
        const char* opacity = get_svg_attr(elem, "opacity");
        if (opacity) {
            float op = strtof(opacity, nullptr);
            fc.a = (uint8_t)(fc.a * op);
        }
        // apply inherited group opacity
        if (ctx->opacity < 1.0f) {
            fc.a = (uint8_t)(fc.a * ctx->opacity);
        }
        svg_fill_path(ctx, path, fc, RDT_FILL_WINDING, transform);
    }

    // --- STROKE ---
    const char* stroke = get_svg_attr(elem, "stroke");
    bool has_stroke = false;
    Color sc;

    if (stroke) {
        if (strcmp(stroke, "none") != 0) {
            has_stroke = true;
            if (strcmp(stroke, "currentColor") == 0) {
                sc = ctx->current_color;
            } else {
                sc = parse_svg_color(stroke);
            }
        }
    } else if (!ctx->stroke_none) {
        has_stroke = true;
        sc = ctx->stroke_color;
    }

    if (has_stroke) {
        const char* stroke_width_str = get_svg_attr(elem, "stroke-width");
        float stroke_width = stroke_width_str ? parse_svg_length(stroke_width_str, 1.0f) : ctx->stroke_width;

        const char* stroke_opacity = get_svg_attr(elem, "stroke-opacity");
        if (stroke_opacity) {
            float opacity = strtof(stroke_opacity, nullptr);
            sc.a = (uint8_t)(sc.a * opacity);
        }
        // apply inherited group opacity
        if (ctx->opacity < 1.0f) {
            sc.a = (uint8_t)(sc.a * ctx->opacity);
        }

        // linecap
        RdtStrokeCap cap = RDT_CAP_BUTT;
        const char* linecap = get_svg_attr(elem, "stroke-linecap");
        if (linecap) {
            if (strcmp(linecap, "round") == 0)       cap = RDT_CAP_ROUND;
            else if (strcmp(linecap, "square") == 0)  cap = RDT_CAP_SQUARE;
        }

        // linejoin
        RdtStrokeJoin join = RDT_JOIN_MITER;
        const char* linejoin = get_svg_attr(elem, "stroke-linejoin");
        if (linejoin) {
            if (strcmp(linejoin, "round") == 0)       join = RDT_JOIN_ROUND;
            else if (strcmp(linejoin, "bevel") == 0)   join = RDT_JOIN_BEVEL;
        }

        // dash array
        float dashes[16];
        int dash_count = 0;
        const char* dasharray = get_svg_attr(elem, "stroke-dasharray");
        if (dasharray && strcmp(dasharray, "none") != 0) {
            const char* p = dasharray;
            while (*p && dash_count < 16) {
                while (*p && (isspace(*p) || *p == ',')) p++;
                if (!*p) break;
                dashes[dash_count++] = strtof(p, (char**)&p);
            }
        }

        svg_stroke_path(ctx, path, sc, stroke_width, cap, join,
                        dash_count > 0 ? dashes : nullptr, dash_count, transform);
    }
}

// ============================================================================
// SVG Shape Renderers (draw directly via rdt_ API)
// ============================================================================

static void render_svg_rect(SvgRenderContext* ctx, Element* elem) {
    float x = parse_svg_length(get_svg_attr(elem, "x"), 0);
    float y = parse_svg_length(get_svg_attr(elem, "y"), 0);
    float width = parse_svg_length(get_svg_attr(elem, "width"), 0);
    float height = parse_svg_length(get_svg_attr(elem, "height"), 0);
    float rx = parse_svg_length(get_svg_attr(elem, "rx"), 0);
    float ry = parse_svg_length(get_svg_attr(elem, "ry"), rx);  // default to rx

    if (width <= 0 || height <= 0) return;

    RdtMatrix m = compose_element_transform(ctx, elem);
    RdtPath* path = rdt_path_new();
    rdt_path_add_rect(path, x, y, width, height, rx, ry);
    draw_svg_fill_stroke(ctx, path, elem, &m, x, y, width, height);
    rdt_path_free(path);

    log_debug("[SVG] rect: x=%.1f y=%.1f w=%.1f h=%.1f rx=%.1f", x, y, width, height, rx);
}

static void render_svg_circle(SvgRenderContext* ctx, Element* elem) {
    float cx = parse_svg_length(get_svg_attr(elem, "cx"), 0);
    float cy = parse_svg_length(get_svg_attr(elem, "cy"), 0);
    float r = parse_svg_length(get_svg_attr(elem, "r"), 0);

    if (r <= 0) return;

    RdtMatrix m = compose_element_transform(ctx, elem);
    RdtPath* path = rdt_path_new();
    rdt_path_add_circle(path, cx, cy, r, r);
    draw_svg_fill_stroke(ctx, path, elem, &m, cx - r, cy - r, 2 * r, 2 * r);
    rdt_path_free(path);

    log_debug("[SVG] circle: cx=%.1f cy=%.1f r=%.1f", cx, cy, r);
}

static void render_svg_ellipse(SvgRenderContext* ctx, Element* elem) {
    float cx = parse_svg_length(get_svg_attr(elem, "cx"), 0);
    float cy = parse_svg_length(get_svg_attr(elem, "cy"), 0);
    float rx = parse_svg_length(get_svg_attr(elem, "rx"), 0);
    float ry = parse_svg_length(get_svg_attr(elem, "ry"), 0);

    if (rx <= 0 || ry <= 0) return;

    RdtMatrix m = compose_element_transform(ctx, elem);
    RdtPath* path = rdt_path_new();
    rdt_path_add_circle(path, cx, cy, rx, ry);
    draw_svg_fill_stroke(ctx, path, elem, &m, cx - rx, cy - ry, 2 * rx, 2 * ry);
    rdt_path_free(path);

    log_debug("[SVG] ellipse: cx=%.1f cy=%.1f rx=%.1f ry=%.1f", cx, cy, rx, ry);
}

static void render_svg_line(SvgRenderContext* ctx, Element* elem) {
    float x1 = parse_svg_length(get_svg_attr(elem, "x1"), 0);
    float y1 = parse_svg_length(get_svg_attr(elem, "y1"), 0);
    float x2 = parse_svg_length(get_svg_attr(elem, "x2"), 0);
    float y2 = parse_svg_length(get_svg_attr(elem, "y2"), 0);

    RdtMatrix m = compose_element_transform(ctx, elem);
    RdtPath* path = rdt_path_new();
    rdt_path_move_to(path, x1, y1);
    rdt_path_line_to(path, x2, y2);

    // lines have stroke only by default — ensure stroke is set
    const char* stroke = get_svg_attr(elem, "stroke");
    if (!stroke && ctx->stroke_none) {
        // no inherited stroke and no explicit stroke: draw with default black
        Color black = {}; black.a = 255;
        svg_stroke_path(ctx, path, black, 1.0f, RDT_CAP_BUTT, RDT_JOIN_MITER,
                        nullptr, 0, &m);
    }
    draw_svg_fill_stroke(ctx, path, elem, &m, 0, 0, 0, 0);
    rdt_path_free(path);

    log_debug("[SVG] line: (%.1f,%.1f) -> (%.1f,%.1f)", x1, y1, x2, y2);
}

// helper: parse points attribute for polyline/polygon into RdtPath
static bool parse_points_to_path(const char* points_str, RdtPath* path, bool close_path) {
    if (!points_str || !path) return false;

    const char* p = points_str;
    float x, y;
    bool first = true;

    while (*p) {
        while (*p && (isspace(*p) || *p == ',')) p++;
        if (!*p) break;

        char* end;
        x = strtof(p, &end);
        if (end == p) break;
        p = end;

        while (*p && (isspace(*p) || *p == ',')) p++;
        if (!*p) break;

        y = strtof(p, &end);
        if (end == p) break;
        p = end;

        if (first) {
            rdt_path_move_to(path, x, y);
            first = false;
        } else {
            rdt_path_line_to(path, x, y);
        }
    }

    if (close_path && !first) {
        rdt_path_close(path);
    }

    return !first;
}

static void render_svg_polyline(SvgRenderContext* ctx, Element* elem, bool close_path) {
    const char* points = get_svg_attr(elem, "points");
    if (!points) return;

    RdtPath* path = rdt_path_new();
    if (!parse_points_to_path(points, path, close_path)) {
        rdt_path_free(path);
        return;
    }

    RdtMatrix m = compose_element_transform(ctx, elem);
    draw_svg_fill_stroke(ctx, path, elem, &m, 0, 0, 0, 0);
    rdt_path_free(path);

    log_debug("[SVG] %s: points=%s", close_path ? "polygon" : "polyline", points);
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

// arc-to-bezier conversion: SVG endpoint parameterization → center parameterization → cubic beziers
// follows the SVG spec F.6 "Conversion from endpoint to center parameterization"
static void arc_to_beziers(RdtPath* path, float x1, float y1,
                           float rx, float ry, float x_rotation,
                           int large_arc, int sweep, float x2, float y2) {
    // F.6.2 - degenerate cases
    if ((x1 == x2 && y1 == y2) || (rx == 0 && ry == 0)) {
        rdt_path_line_to(path, x2, y2);
        return;
    }

    rx = fabsf(rx);
    ry = fabsf(ry);

    float phi = x_rotation * (float)M_PI / 180.0f;
    float cos_phi = cosf(phi);
    float sin_phi = sinf(phi);

    // F.6.5.1 - compute (x1', y1')
    float dx2 = (x1 - x2) / 2.0f;
    float dy2 = (y1 - y2) / 2.0f;
    float x1p =  cos_phi * dx2 + sin_phi * dy2;
    float y1p = -sin_phi * dx2 + cos_phi * dy2;

    // F.6.6 - ensure radii are large enough
    float x1p2 = x1p * x1p;
    float y1p2 = y1p * y1p;
    float rx2 = rx * rx;
    float ry2 = ry * ry;

    float lambda = x1p2 / rx2 + y1p2 / ry2;
    if (lambda > 1.0f) {
        float lambda_sqrt = sqrtf(lambda);
        rx *= lambda_sqrt;
        ry *= lambda_sqrt;
        rx2 = rx * rx;
        ry2 = ry * ry;
    }

    // F.6.5.2 - compute (cx', cy')
    float num = rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2;
    float den = rx2 * y1p2 + ry2 * x1p2;
    float sq = (den > 0.0f) ? sqrtf(fmaxf(num / den, 0.0f)) : 0.0f;
    if (large_arc == sweep) sq = -sq;

    float cxp =  sq * rx * y1p / ry;
    float cyp = -sq * ry * x1p / rx;

    // F.6.5.3 - compute (cx, cy)
    float cx = cos_phi * cxp - sin_phi * cyp + (x1 + x2) / 2.0f;
    float cy = sin_phi * cxp + cos_phi * cyp + (y1 + y2) / 2.0f;

    // F.6.5.5 - compute start angle and sweep angle
    auto angle_between = [](float ux, float uy, float vx, float vy) -> float {
        float dot = ux * vx + uy * vy;
        float len = sqrtf((ux * ux + uy * uy) * (vx * vx + vy * vy));
        float cos_a = (len > 0) ? fmaxf(-1.0f, fminf(1.0f, dot / len)) : 1.0f;
        float a = acosf(cos_a);
        if (ux * vy - uy * vx < 0) a = -a;
        return a;
    };

    float theta1 = angle_between(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
    float dtheta = angle_between((x1p - cxp) / rx, (y1p - cyp) / ry,
                                 (-x1p - cxp) / rx, (-y1p - cyp) / ry);

    if (!sweep && dtheta > 0) dtheta -= 2.0f * (float)M_PI;
    if (sweep && dtheta < 0)  dtheta += 2.0f * (float)M_PI;

    // split arc into segments of at most PI/2 and approximate each with a cubic bezier
    int n_segs = (int)ceilf(fabsf(dtheta) / ((float)M_PI / 2.0f));
    if (n_segs < 1) n_segs = 1;
    float seg_angle = dtheta / (float)n_segs;
    // control point distance factor: (4/3) * tan(seg_angle / 4)
    float alpha = 4.0f / 3.0f * tanf(seg_angle / 4.0f);

    float prev_cos = cosf(theta1);
    float prev_sin = sinf(theta1);

    for (int i = 0; i < n_segs; i++) {
        float angle = theta1 + (float)(i + 1) * seg_angle;
        float next_cos = cosf(angle);
        float next_sin = sinf(angle);

        // endpoint of this segment on the unit circle
        float ep1x = prev_cos;
        float ep1y = prev_sin;
        float ep2x = next_cos;
        float ep2y = next_sin;

        // control points on unit circle
        float cp1x = ep1x - alpha * ep1y;
        float cp1y = ep1y + alpha * ep1x;
        float cp2x = ep2x + alpha * ep2y;
        float cp2y = ep2y - alpha * ep2x;

        // transform back: scale by rx,ry then rotate by phi then translate by cx,cy
        auto tx = [&](float px, float py) -> float {
            return cos_phi * rx * px - sin_phi * ry * py + cx;
        };
        auto ty = [&](float px, float py) -> float {
            return sin_phi * rx * px + cos_phi * ry * py + cy;
        };

        rdt_path_cubic_to(path,
                           tx(cp1x, cp1y), ty(cp1x, cp1y),
                           tx(cp2x, cp2y), ty(cp2x, cp2y),
                           tx(ep2x, ep2y), ty(ep2x, ep2y));

        prev_cos = next_cos;
        prev_sin = next_sin;
    }
}

// Parse SVG path 'd' attribute into an RdtPath. Returns new path (caller must free).
static RdtPath* parse_svg_path_d(const char* d) {
    if (!d || !*d) return nullptr;

    RdtPath* path = rdt_path_new();

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
            cmd = last_cmd;
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
                rdt_path_move_to(path, x, y);
                cur_x = start_x = x;
                cur_y = start_y = y;
                last_ctrl_x = cur_x;
                last_ctrl_y = cur_y;
                // subsequent coords are implicit lineto
                while (peek_number(p)) {
                    x = parse_number(&p);
                    y = parse_number(&p);
                    if (relative) { x += cur_x; y += cur_y; }
                    rdt_path_line_to(path, x, y);
                    cur_x = x; cur_y = y;
                }
                break;
            }
            case 'L': {  // lineto
                while (peek_number(p)) {
                    float x = parse_number(&p);
                    float y = parse_number(&p);
                    if (relative) { x += cur_x; y += cur_y; }
                    rdt_path_line_to(path, x, y);
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
                    rdt_path_line_to(path, x, cur_y);
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
                    rdt_path_line_to(path, cur_x, y);
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
                    rdt_path_cubic_to(path, x1, y1, x2, y2, x, y);
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
                    rdt_path_cubic_to(path, x1, y1, x2, y2, x, y);
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
                    rdt_path_cubic_to(path, cx1, cy1, cx2, cy2, x, y);
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
                    rdt_path_cubic_to(path, cx1, cy1, cx2, cy2, x, y);
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

                    arc_to_beziers(path, cur_x, cur_y, rx, ry, rotation, large_arc, sweep, x, y);
                    cur_x = x; cur_y = y;
                }
                last_ctrl_x = cur_x;
                last_ctrl_y = cur_y;
                break;
            }
            case 'Z': {  // closepath
                rdt_path_close(path);
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

    return path;
}

static void render_svg_path(SvgRenderContext* ctx, Element* elem) {
    const char* d = get_svg_attr(elem, "d");
    RdtPath* path = parse_svg_path_d(d);
    if (!path) return;

    RdtMatrix m = compose_element_transform(ctx, elem);
    draw_svg_fill_stroke(ctx, path, elem, &m, 0, 0, 0, 0);
    rdt_path_free(path);

    log_debug("[SVG] path: d=%s", d);
}

// ============================================================================
// SVG Text Rendering (requires ThorVG with TTF loader)
// ============================================================================

/**
 * Resolve font path from font-family name
 * Uses platform-specific font lookup
 * out_font_name: returns the actual font name used (may be different due to fallback)
 */
// helper: try font database lookup for a given family name
// returns mem_alloc'd path string (caller frees), or nullptr
static char* resolve_font_via_database(FontContext* font_ctx, const char* family,
                                       const char** out_font_name,
                                       int weight = 400,
                                       FontSlant slant = FONT_SLANT_NORMAL) {
    if (!font_ctx || !font_ctx->database || !family) return nullptr;

    FontDatabaseCriteria criteria = {};
    strncpy(criteria.family_name, family, sizeof(criteria.family_name) - 1);
    criteria.weight = weight;
    criteria.style = slant;

    FontDatabaseResult result = font_database_find_best_match_internal(font_ctx->database, &criteria);
    if (result.font && result.font->file_path) {
        // skip TTC files — ThorVG TTF loader doesn't handle TrueType Collections
        if (strstr(result.font->file_path, ".ttc")) {
            log_debug("[SVG] skipping TTC from database: %s", result.font->file_path);
            return nullptr;
        }
        if (out_font_name) *out_font_name = result.font->family_name;
        // return a mem_alloc'd copy so caller can mem_free() uniformly
        size_t len = strlen(result.font->file_path);
        char* path = (char*)mem_alloc(len + 1, MEM_CAT_RENDER);
        if (path) memcpy(path, result.font->file_path, len + 1);
        return path;
    }
    return nullptr;
}

static char* resolve_svg_font_path(const char* font_family, const char** out_font_name,
                                    FontContext* font_ctx = nullptr, int weight = 400,
                                    FontSlant slant = FONT_SLANT_NORMAL) {
    // default font name
    const char* used_font_name = font_family;

    if (!font_family || !*font_family) {
        // default to a common sans-serif font
        font_family = "Arial";
        used_font_name = "Arial";
    }

    // SVG font-family is a comma-separated list of family names (with optional
    // single/double quotes around multi-word names) and at most one generic
    // family keyword (serif, sans-serif, monospace, cursive, fantasy).  Try
    // each candidate in order, applying weight-aware matching for each, before
    // falling back to a global default list.
    char family_list[512];
    strncpy(family_list, font_family, sizeof(family_list) - 1);
    family_list[sizeof(family_list) - 1] = '\0';

    // collect candidate family names by splitting on commas
    const char* candidates[16];
    int candidate_count = 0;
    {
        char* cursor = family_list;
        while (cursor && *cursor && candidate_count < 16) {
            // skip leading whitespace
            while (*cursor == ' ' || *cursor == '\t') cursor++;
            // strip surrounding quotes
            char qc = 0;
            if (*cursor == '"' || *cursor == '\'') { qc = *cursor; cursor++; }
            char* start = cursor;
            char* end;
            if (qc) {
                end = strchr(cursor, qc);
                if (!end) end = cursor + strlen(cursor);
                *end = '\0';
                cursor = end + 1;
                // skip until next comma (or end)
                char* nc = strchr(cursor, ',');
                cursor = nc ? nc + 1 : nullptr;
            } else {
                end = strchr(cursor, ',');
                if (end) { *end = '\0'; cursor = end + 1; }
                else { cursor = nullptr; }
            }
            // strip trailing whitespace
            char* te = start + strlen(start);
            while (te > start && (te[-1] == ' ' || te[-1] == '\t')) te--;
            *te = '\0';
            if (*start) {
                // map generic CSS keywords to a concrete platform font
                const char* mapped = start;
                if (strcasecmp(start, "serif") == 0)            mapped = "Times New Roman";
                else if (strcasecmp(start, "sans-serif") == 0)  mapped = "Arial";
                else if (strcasecmp(start, "monospace") == 0)   mapped = "Courier New";
                else if (strcasecmp(start, "cursive") == 0)     mapped = "Comic Sans MS";
                else if (strcasecmp(start, "fantasy") == 0)     mapped = "Impact";
                candidates[candidate_count++] = mapped;
            }
        }
    }

    // helper: try a single family with full resolution chain
    auto try_family = [&](const char* fam) -> char* {
        // weight-aware / slant-aware best match. Use it whenever bold or
        // italic/oblique is requested — the platform lookup ignores style.
        if (font_ctx && (weight >= 600 || slant != FONT_SLANT_NORMAL)) {
            FontMatchResult match = font_find_best_match(font_ctx, fam, weight, slant);
            if (match.found && match.file_path && !strstr(match.file_path, ".ttc")) {
                char* path = mem_strdup(match.file_path, MEM_CAT_RENDER);
                if (out_font_name) {
                    const char* slash = strrchr(match.file_path, '/');
                    if (!slash) slash = strrchr(match.file_path, '\\');
                    const char* base = slash ? slash + 1 : match.file_path;
                    const char* dot = strrchr(base, '.');
                    static char bold_font_name[256];
                    int name_len = dot ? (int)(dot - base) : (int)strlen(base);
                    if (name_len > 255) name_len = 255;
                    memcpy(bold_font_name, base, name_len);
                    bold_font_name[name_len] = '\0';
                    *out_font_name = bold_font_name;
                }
                log_debug("[SVG] font resolved via best-match (weight=%d slant=%d): %s -> %s (name='%s')",
                          weight, (int)slant, fam, path, out_font_name ? *out_font_name : "?");
                return path;
            }
        }
        // platform lookup
        char* p = font_platform_find_fallback(fam);
        if (p && strstr(p, ".ttc")) { mem_free(p); p = nullptr; }
        if (p) {
            if (out_font_name) {
                // derive font_name from the file basename (sans extension); the
                // candidate `fam` may live in a stack buffer that goes out of
                // scope after this function returns.
                static char platform_font_name[256];
                const char* slash = strrchr(p, '/');
                if (!slash) slash = strrchr(p, '\\');
                const char* base = slash ? slash + 1 : p;
                const char* dot = strrchr(base, '.');
                int name_len = dot ? (int)(dot - base) : (int)strlen(base);
                if (name_len > 255) name_len = 255;
                memcpy(platform_font_name, base, name_len);
                platform_font_name[name_len] = '\0';
                *out_font_name = platform_font_name;
            }
            log_debug("[SVG] font resolved via platform: %s -> %s (name='%s')",
                      fam, p, out_font_name ? *out_font_name : "?");
            return p;
        }
        // database lookup
        if (font_ctx) {
            const char* dbname = nullptr;
            p = resolve_font_via_database(font_ctx, fam, &dbname, weight, slant);
            if (p) {
                if (out_font_name) {
                    static char db_font_name[256];
                    if (dbname) {
                        strncpy(db_font_name, dbname, sizeof(db_font_name) - 1);
                        db_font_name[sizeof(db_font_name) - 1] = '\0';
                    } else {
                        const char* slash = strrchr(p, '/');
                        if (!slash) slash = strrchr(p, '\\');
                        const char* base = slash ? slash + 1 : p;
                        const char* dot = strrchr(base, '.');
                        int name_len = dot ? (int)(dot - base) : (int)strlen(base);
                        if (name_len > 255) name_len = 255;
                        memcpy(db_font_name, base, name_len);
                        db_font_name[name_len] = '\0';
                    }
                    *out_font_name = db_font_name;
                }
                log_debug("[SVG] font resolved via database: %s -> %s", fam, p);
                return p;
            }
        }
        return nullptr;
    };

    // try each candidate from the SVG font-family list
    for (int i = 0; i < candidate_count; i++) {
        char* p = try_family(candidates[i]);
        if (p) return p;
    }

    // try common fallbacks - prefer simple TTF files that ThorVG can load
    static const char* fallbacks[] = {
        "Arial", "Segoe UI", "Calibri", "Verdana",
        "SFNS", "Geneva", "Arial Unicode MS",
        "DejaVu Sans", "Liberation Sans", "Noto Sans",
        nullptr
    };
    for (int i = 0; fallbacks[i]; i++) {
        char* p = try_family(fallbacks[i]);
        if (p) {
            log_debug("[SVG] font fallback: %s -> %s", font_family, fallbacks[i]);
            return p;
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
    char* result = (char*)mem_alloc(trimmed_len + 1, MEM_CAT_RENDER);
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
            String* str = child.get_string();
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
 * anchor_x: horizontal anchor (0=start, 0.5=middle, 1=end) from SVG text-anchor
 */
static Tvg_Paint create_text_segment(const char* text, float x, float y,
                                     const char* font_path, const char* font_name,
                                     float font_size_px, Color fill_color,
                                     float anchor_x = 0.0f) {
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

    // apply horizontal anchor (SVG text-anchor: start=0, middle=0.5, end=1)
    if (anchor_x > 0.0f) {
        tvg_text_align(tvg_text, anchor_x, 0.0f);
    }

    // NOTE: do NOT call tvg_paint_translate here — it would be overwritten
    // by tvg_paint_set_transform in rdt_picture_draw. The caller composes
    // text position into the drawing transform matrix instead.

    log_debug("[SVG] text segment: '%s' at (%.1f, %.1f) size=%.1fpx (%.1fpt) color=rgb(%d,%d,%d)",
              text, x, y, font_size_px, font_size_pt, fill_color.r, fill_color.g, fill_color.b);

    return tvg_text;
}

/**
 * Measure text width using the font system for accurate positioning.
 * Falls back to rough estimate if font handle unavailable.
 */
static float measure_svg_text_width(const char* text, float font_size_px,
                                    FontContext* font_ctx, const char* font_family, int weight) {
    if (!text || !*text) return 0;

    // try measuring with the font system
    if (font_ctx && font_family) {
        FontStyleDesc style = {};
        style.family = font_family;
        style.size_px = font_size_px;
        style.weight = (FontWeight)weight;
        style.slant = FONT_SLANT_NORMAL;

        FontHandle* handle = font_resolve(font_ctx, &style);
        if (handle) {
            TextExtents ext = font_measure_text(handle, text, (int)strlen(text));
            font_handle_release(handle);
            if (ext.width > 0) return ext.width;
        }
    }

    // fallback: rough estimate
    size_t len = strlen(text);
    return len * font_size_px * 0.55f;
}

/**
 * Render SVG <text> element with proper tspan support
 * Each tspan gets its own color and position
 */
static void render_svg_text(SvgRenderContext* ctx, Element* elem) {
    if (!elem) return;

    // parse parent text attributes (fall back to inherited from parent <g>)
    float base_x = parse_svg_length(get_svg_attr(elem, "x"), 0);
    float base_y = parse_svg_length(get_svg_attr(elem, "y"), 0);

    const char* font_family = get_svg_attr(elem, "font-family");
    if (!font_family) font_family = ctx->inherited_font_family;
    const char* font_size_str = get_svg_attr(elem, "font-size");
    float font_size;
    if (font_size_str) {
        font_size = parse_svg_length(font_size_str, 16);
    } else if (ctx->inherited_font_size > 0) {
        font_size = ctx->inherited_font_size;
    } else {
        font_size = 16;
    }

    // parse text-anchor: start (default), middle, end
    const char* text_anchor_str = get_svg_attr(elem, "text-anchor");
    if (!text_anchor_str) text_anchor_str = ctx->inherited_text_anchor;
    float anchor_x = 0.0f;
    if (text_anchor_str) {
        if (strcmp(text_anchor_str, "middle") == 0) anchor_x = 0.5f;
        else if (strcmp(text_anchor_str, "end") == 0) anchor_x = 1.0f;
    }

    // get default fill from element, then inherited group fill
    const char* parent_fill = get_svg_attr(elem, "fill");
    Color default_fill;
    if (parent_fill) {
        default_fill = parse_svg_color(parent_fill);
    } else if (!ctx->fill_none) {
        default_fill = ctx->fill_color;
    } else {
        default_fill = parse_svg_color("black");
    }

    // parse font-weight: normal (400), bold (700), or numeric
    const char* font_weight_str = get_svg_attr(elem, "font-weight");
    int font_weight = ctx->inherited_font_weight > 0 ? ctx->inherited_font_weight : 400;
    if (font_weight_str) {
        if (strcmp(font_weight_str, "bold") == 0) font_weight = 700;
        else if (strcmp(font_weight_str, "bolder") == 0) font_weight = 700;
        else if (strcmp(font_weight_str, "lighter") == 0) font_weight = 300;
        else if (strcmp(font_weight_str, "normal") == 0) font_weight = 400;
        else font_weight = atoi(font_weight_str);
        if (font_weight <= 0) font_weight = 400;
    }

    // parse font-style: italic / oblique / normal. SVG attribute or
    // inherited value from parent <g>. Required so the resolved font
    // file actually carries the italic glyphs (otherwise SVG output
    // marked italic renders upright because the platform lookup
    // ignores style).
    const char* font_style_str = get_svg_attr(elem, "font-style");
    FontSlant font_slant = FONT_SLANT_NORMAL;
    if (font_style_str) {
        if      (strcmp(font_style_str, "italic") == 0)  font_slant = FONT_SLANT_ITALIC;
        else if (strcmp(font_style_str, "oblique") == 0) font_slant = FONT_SLANT_OBLIQUE;
    }

    // resolve font path and name
    const char* font_name = nullptr;
    char* font_path = resolve_svg_font_path(font_family, &font_name, ctx->font_ctx, font_weight, font_slant);
    if (!font_path) {
        log_debug("[SVG] <text> no font available for: %s", font_family ? font_family : "default");
        return;
    }

    // compose element transform with accumulated context transform
    RdtMatrix m = compose_element_transform(ctx, elem);

    // count children to see if we have tspans
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
        mem_free(font_path);
        return;
    }

    // resolve font handle once for accurate metrics. Use the resolved single
    // font_name (e.g. "Verdana") rather than the raw comma-list font_family
    // (e.g. "DejaVu Sans,Verdana,Geneva,sans-serif") which font_resolve does
    // not parse — feeding it the list yields a generic fallback whose ascent
    // ratio (e.g. Helvetica ~0.77) does not match the actually-drawn font.
    float font_ascent_ratio = 0.8f;  // fallback
    const char* metrics_family = font_name ? font_name : font_family;
    if (ctx->font_ctx && metrics_family) {
        FontStyleDesc style = {};
        style.family = metrics_family;
        style.size_px = font_size;
        style.weight = (FontWeight)font_weight;
        style.slant = font_slant;
        FontHandle* handle = font_resolve(ctx->font_ctx, &style);
        if (handle) {
            const FontMetrics* fm = font_get_metrics(handle);
            if (fm && fm->ascender > 0 && font_size > 0) {
                font_ascent_ratio = fm->ascender / font_size;
            }
            font_handle_release(handle);
        }
    }

    // helper lambda to wrap and draw a ThorVG text paint
    // text position (tx, ty) is composed into the transform since
    // tvg_paint_set_transform in rdt_picture_draw overwrites any prior tvg_paint_translate
    auto draw_text_paint = [&](Tvg_Paint tvg_text, float tx, float ty, float fs_px) {
        if (!tvg_text) return;
        float ascent = fs_px * font_ascent_ratio;
        float adj_y = ty - ascent;
        RdtMatrix pos = rdt_matrix_translate(tx, adj_y);
        RdtMatrix final_m = rdt_matrix_multiply(&m, &pos);
        RdtPicture* pic = rdt_picture_take_tvg_paint(tvg_text, 0, 0);
        if (pic) {
            svg_draw_picture(ctx, pic, 255, &final_m);
        }
    };

    // if single text with no tspan, use simple rendering
    if (text_segments == 1 && !has_tspan) {
        const char* text_content = get_direct_text_content(elem);
        if (text_content) {
            Tvg_Paint text = create_text_segment(text_content, base_x, base_y,
                                                  font_path, font_name, font_size, default_fill,
                                                  anchor_x);
            mem_free((void*)text_content);
            draw_text_paint(text, base_x, base_y, font_size);
        }
        mem_free(font_path);
        return;
    }

    // multiple segments - draw each directly with accumulated transform
    float cur_x = base_x;
    float cur_y = base_y;

    for (int64_t i = 0; i < elem->length; i++) {
        Item child = elem->items[i];
        TypeId type = get_type_id(child);

        if (type == LMD_TYPE_STRING) {
            // direct text node
            String* str = child.get_string();
            if (str && str->chars && str->len > 0) {
                if (is_whitespace_only(str->chars, str->len)) {
                    // SVG spec: whitespace between tspans collapses to a single space
                    if (has_tspan) {
                        cur_x += measure_svg_text_width(" ", font_size, ctx->font_ctx, metrics_family, font_weight);
                    }
                    continue;
                }
                char* text_copy = trim_whitespace(str->chars, str->len);
                if (text_copy) {
                    Tvg_Paint text_obj = create_text_segment(text_copy, cur_x, cur_y,
                                                              font_path, font_name, font_size, default_fill);
                    if (text_obj) {
                        float w = measure_svg_text_width(text_copy, font_size, ctx->font_ctx, metrics_family, font_weight);
                        draw_text_paint(text_obj, cur_x, cur_y, font_size);
                        cur_x += w;
                    }
                    mem_free(text_copy);
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
                        float w = measure_svg_text_width(text_content, tspan_font_size, ctx->font_ctx, metrics_family, font_weight);
                        draw_text_paint(text_obj, cur_x, cur_y, tspan_font_size);
                        cur_x += w;
                    }
                    mem_free((void*)text_content);
                }
            }
        }
    }

    mem_free(font_path);

    log_debug("[SVG] <text> rendered with %d segments at base (%.1f, %.1f)",
              text_segments, base_x, base_y);
}

// ============================================================================
// SVG Image Rendering (using Radiant's unified image loading)
// ============================================================================

/**
 * Render SVG <image> element using Radiant's image loading infrastructure
 * Images are loaded via Radiant's load_image() and converted to ThorVG pictures
 */
static void render_svg_image(SvgRenderContext* ctx, Element* elem) {
    if (!elem) return;

    // get href attribute (SVG 2 uses href, SVG 1.1 uses xlink:href)
    const char* href = get_svg_attr(elem, "href");
    if (!href) href = get_svg_attr(elem, "xlink:href");
    if (!href || !*href) {
        log_debug("[SVG] <image> missing href attribute");
        return;
    }

    // parse position and size
    float x = parse_svg_length(get_svg_attr(elem, "x"), 0);
    float y = parse_svg_length(get_svg_attr(elem, "y"), 0);
    float width = parse_svg_length(get_svg_attr(elem, "width"), 0);
    float height = parse_svg_length(get_svg_attr(elem, "height"), 0);

    // TODO: integrate with Radiant's load_image() when UiContext is available
    // For now, use ThorVG's picture loading for SVG images
    Tvg_Paint pic = tvg_picture_new();
    if (!pic) return;

    Tvg_Result result;

    // Handle data: URIs by decoding base64 and feeding raw bytes to ThorVG.
    // The mime type embedded in the URI is sometimes wrong (e.g. "data:false;base64,...")
    // so we always sniff the actual content from magic bytes.
    if (strncmp(href, "data:", 5) == 0) {
        size_t decoded_len = 0;
        char declared_mime[64] = {0};
        uint8_t* decoded = parse_data_uri(href, declared_mime, sizeof(declared_mime), &decoded_len);
        if (!decoded || decoded_len == 0) {
            log_debug("[SVG] <image> data URI decode failed");
            if (decoded) mem_free(decoded);
            tvg_paint_unref(pic, true);
            return;
        }

        // Sniff actual format from magic bytes (more reliable than declared mime)
        const char* mime_hint = nullptr;
        if (decoded_len >= 8 && decoded[0] == 0x89 && decoded[1] == 'P' &&
            decoded[2] == 'N' && decoded[3] == 'G') {
            mime_hint = "png";
        } else if (decoded_len >= 3 && decoded[0] == 0xFF && decoded[1] == 0xD8 &&
                   decoded[2] == 0xFF) {
            mime_hint = "jpg";
        } else if ((decoded_len >= 5 && memcmp(decoded, "<?xml", 5) == 0) ||
                   (decoded_len >= 4 && memcmp(decoded, "<svg", 4) == 0)) {
            mime_hint = "svg";
        } else {
            // fallback to declared mime if it looks meaningful
            if (strstr(declared_mime, "png")) mime_hint = "png";
            else if (strstr(declared_mime, "jpeg") || strstr(declared_mime, "jpg")) mime_hint = "jpg";
            else if (strstr(declared_mime, "svg")) mime_hint = "svg";
        }

        if (!mime_hint) {
            log_debug("[SVG] <image> unknown data URI format (declared mime='%s')", declared_mime);
            mem_free(decoded);
            tvg_paint_unref(pic, true);
            return;
        }

        // copy=true so ThorVG holds its own copy and we can free decoded
        result = tvg_picture_load_data(pic, (const char*)decoded, (uint32_t)decoded_len,
                                       mime_hint, NULL, true);
        mem_free(decoded);
        if (result != TVG_RESULT_SUCCESS) {
            log_debug("[SVG] <image> failed to load data URI (mime=%s, declared=%s)",
                      mime_hint, declared_mime);
            tvg_paint_unref(pic, true);
            return;
        }
    } else {
        result = tvg_picture_load(pic, href);
        if (result != TVG_RESULT_SUCCESS) {
            log_debug("[SVG] <image> failed to load: %s", href);
            tvg_paint_unref(pic, true);
            return;
        }
    }

    // set size if specified
    if (width > 0 && height > 0) {
        tvg_picture_set_size(pic, width, height);
    }

    // position the image
    tvg_paint_translate(pic, x, y);

    // apply opacity if present
    uint8_t op = 255;
    const char* opacity = get_svg_attr(elem, "opacity");
    if (opacity) {
        float opf = strtof(opacity, nullptr);
        op = (uint8_t)(opf * 255);
    }

    // compose element transform with accumulated context transform
    RdtMatrix m = compose_element_transform(ctx, elem);

    // wrap as RdtPicture and draw
    RdtPicture* rdt_pic = rdt_picture_take_tvg_paint(pic, 0, 0);
    if (rdt_pic) {
        svg_draw_picture(ctx, rdt_pic, op, &m);
    }

    log_debug("[SVG] <image> loaded: %s at (%.1f, %.1f) size %.1fx%.1f",
              href, x, y, width, height);
}

// ============================================================================
// SVG ClipPath Support
// ============================================================================

// Build a composite RdtPath from the children of a <clipPath> element.
// Returns new path (caller must free), or nullptr if no geometry found.
static RdtPath* build_clip_path_from_def(SvgRenderContext* ctx, Element* clip_elem) {
    if (!clip_elem || clip_elem->length == 0) return nullptr;

    RdtPath* clip_path = rdt_path_new();
    bool has_geometry = false;

    for (int64_t i = 0; i < clip_elem->length; i++) {
        Element* child = get_child_element_at(clip_elem, i);
        if (!child) continue;
        const char* tag = get_element_tag_name(child);
        if (!tag) continue;

        if (strcmp(tag, "rect") == 0) {
            float x = parse_svg_length(get_svg_attr(child, "x"), 0);
            float y = parse_svg_length(get_svg_attr(child, "y"), 0);
            float w = parse_svg_length(get_svg_attr(child, "width"), 0);
            float h = parse_svg_length(get_svg_attr(child, "height"), 0);
            float rx = parse_svg_length(get_svg_attr(child, "rx"), 0);
            float ry = parse_svg_length(get_svg_attr(child, "ry"), rx);
            if (w > 0 && h > 0) {
                rdt_path_add_rect(clip_path, x, y, w, h, rx, ry);
                has_geometry = true;
            }
        } else if (strcmp(tag, "circle") == 0) {
            float cx = parse_svg_length(get_svg_attr(child, "cx"), 0);
            float cy = parse_svg_length(get_svg_attr(child, "cy"), 0);
            float r = parse_svg_length(get_svg_attr(child, "r"), 0);
            if (r > 0) {
                rdt_path_add_circle(clip_path, cx, cy, r, r);
                has_geometry = true;
            }
        } else if (strcmp(tag, "ellipse") == 0) {
            float cx = parse_svg_length(get_svg_attr(child, "cx"), 0);
            float cy = parse_svg_length(get_svg_attr(child, "cy"), 0);
            float rx = parse_svg_length(get_svg_attr(child, "rx"), 0);
            float ry = parse_svg_length(get_svg_attr(child, "ry"), 0);
            if (rx > 0 && ry > 0) {
                rdt_path_add_circle(clip_path, cx, cy, rx, ry);
                has_geometry = true;
            }
        } else if (strcmp(tag, "polygon") == 0 || strcmp(tag, "polyline") == 0) {
            const char* points = get_svg_attr(child, "points");
            if (points) {
                has_geometry = parse_points_to_path(points, clip_path, (strcmp(tag, "polygon") == 0)) || has_geometry;
            }
        } else if (strcmp(tag, "path") == 0) {
            const char* d = get_svg_attr(child, "d");
            if (d) {
                // parse path 'd' commands directly into clip_path
                RdtPath* temp = parse_svg_path_d(d);
                if (temp) {
                    // we can't copy entries (opaque struct), so free and use temp directly
                    // for single-child clipPath (common case)
                    if (clip_elem->length == 1) {
                        rdt_path_free(clip_path);
                        return temp;
                    }
                    // for multi-child, we need to build separate and push multiple clips
                    // for now, just use the first path child
                    rdt_path_free(clip_path);
                    return temp;
                }
            }
        }
    }

    if (!has_geometry) {
        rdt_path_free(clip_path);
        return nullptr;
    }
    return clip_path;
}

// Parse clip-path="url(#id)" and return the clip RdtPath if found in defs.
static RdtPath* resolve_svg_clip_path(SvgRenderContext* ctx, Element* elem) {
    const char* cp = get_svg_attr(elem, "clip-path");
    if (!cp) return nullptr;

    // parse url(#id) reference
    if (strncmp(cp, "url(#", 5) != 0) return nullptr;
    const char* id_start = cp + 5;
    const char* id_end = strchr(id_start, ')');
    if (!id_end || id_end == id_start) return nullptr;

    char id_buf[128];
    int id_len = (int)(id_end - id_start);
    if (id_len >= (int)sizeof(id_buf)) return nullptr;
    memcpy(id_buf, id_start, id_len);
    id_buf[id_len] = '\0';

    if (!ctx->defs) return nullptr;
    Element* clip_elem = lookup_elem_def((SvgDefTable*)ctx->defs, id_buf);
    if (!clip_elem) {
        log_debug("[SVG] clip-path ref '%s' not found in defs", id_buf);
        return nullptr;
    }

    RdtPath* path = build_clip_path_from_def(ctx, clip_elem);
    if (path) {
        log_debug("[SVG] resolved clip-path='%s'", cp);
    }
    return path;
}

// ============================================================================
// SVG Group and Children
// ============================================================================

static void render_svg_group(SvgRenderContext* ctx, Element* elem) {
    if (!elem) return;

    // save current inherited state
    Color saved_fill = ctx->fill_color;
    Color saved_stroke = ctx->stroke_color;
    Color saved_current_color = ctx->current_color;
    float saved_stroke_width = ctx->stroke_width;
    float saved_opacity = ctx->opacity;
    bool saved_fill_none = ctx->fill_none;
    bool saved_stroke_none = ctx->stroke_none;
    RdtMatrix saved_transform = ctx->transform;
    const char* saved_font_family = ctx->inherited_font_family;
    float saved_font_size = ctx->inherited_font_size;
    int saved_font_weight = ctx->inherited_font_weight;
    const char* saved_text_anchor = ctx->inherited_text_anchor;

    // apply group opacity via save/composite for correct compositing
    const char* opacity_attr = get_svg_attr(elem, "opacity");
    float group_op = 1.0f;
    bool use_opacity_layer = false;
    int op_x0 = 0, op_y0 = 0, op_w = 0, op_h = 0;
    if (opacity_attr) {
        group_op = strtof(opacity_attr, nullptr);
        if (group_op < 0.0f) group_op = 0.0f;
        if (group_op > 1.0f) group_op = 1.0f;
        if (group_op < 1.0f) {
            // use backdrop save/composite so overlapping children composite correctly
            // compute viewport bounds in screen coords from the accumulated transform
            float vx0 = ctx->transform.e13;
            float vy0 = ctx->transform.e23;
            float vx1 = ctx->transform.e11 * ctx->viewbox_width + ctx->transform.e12 * ctx->viewbox_height + ctx->transform.e13;
            float vy1 = ctx->transform.e21 * ctx->viewbox_width + ctx->transform.e22 * ctx->viewbox_height + ctx->transform.e23;
            op_x0 = (int)floorf(fminf(vx0, vx1));
            op_y0 = (int)floorf(fminf(vy0, vy1));
            op_w = (int)ceilf(fmaxf(vx0, vx1)) - op_x0;
            op_h = (int)ceilf(fmaxf(vy0, vy1)) - op_y0;
            if (op_w > 0 && op_h > 0) {
                use_opacity_layer = true;
                if (ctx->dl) {
                    dl_save_backdrop(ctx->dl, op_x0, op_y0, op_w, op_h);
                }
                log_debug("[SVG-GROUP] opacity=%.2f, save backdrop (%d,%d,%d,%d) dl=%d",
                          group_op, op_x0, op_y0, op_w, op_h, ctx->dl != nullptr);
            }
        }
    }
    // if not using opacity layer, fall back to inherited alpha multiply
    if (!use_opacity_layer && group_op < 1.0f) {
        ctx->opacity *= group_op;
    }

    // update CSS 'color' property (for currentColor keyword)
    const char* color_attr = get_svg_attr(elem, "color");
    if (color_attr) {
        ctx->current_color = parse_svg_color(color_attr);
    }

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

    // inherited text properties from group attributes
    const char* g_font_family = get_svg_attr(elem, "font-family");
    if (g_font_family) ctx->inherited_font_family = g_font_family;
    const char* g_font_size = get_svg_attr(elem, "font-size");
    if (g_font_size) ctx->inherited_font_size = parse_svg_length(g_font_size, ctx->inherited_font_size);
    const char* g_font_weight = get_svg_attr(elem, "font-weight");
    if (g_font_weight) {
        if (strcmp(g_font_weight, "bold") == 0) ctx->inherited_font_weight = 700;
        else if (strcmp(g_font_weight, "normal") == 0) ctx->inherited_font_weight = 400;
        else {
            int w = atoi(g_font_weight);
            if (w >= 100 && w <= 900) ctx->inherited_font_weight = w;
        }
    }
    const char* g_text_anchor = get_svg_attr(elem, "text-anchor");
    if (g_text_anchor) ctx->inherited_text_anchor = g_text_anchor;

    // apply group transform to accumulated transform
    ctx->transform = compose_element_transform(ctx, elem);

    // render children directly
    render_svg_children(ctx, elem);

    // composite opacity layer if active
    if (use_opacity_layer && op_w > 0 && op_h > 0) {
        if (ctx->dl) {
            dl_composite_opacity(ctx->dl, op_x0, op_y0, op_w, op_h, group_op);
        }
        log_debug("[SVG-GROUP] composite opacity=%.2f over backdrop", group_op);
    }

    // restore inherited state
    ctx->fill_color = saved_fill;
    ctx->stroke_color = saved_stroke;
    ctx->current_color = saved_current_color;
    ctx->stroke_width = saved_stroke_width;
    ctx->opacity = saved_opacity;
    ctx->fill_none = saved_fill_none;
    ctx->stroke_none = saved_stroke_none;
    ctx->transform = saved_transform;
    ctx->inherited_font_family = saved_font_family;
    ctx->inherited_font_size = saved_font_size;
    ctx->inherited_font_weight = saved_font_weight;
    ctx->inherited_text_anchor = saved_text_anchor;
}

static void render_svg_children(SvgRenderContext* ctx, Element* elem) {
    if (!elem || elem->length == 0) return;

    for (int64_t i = 0; i < elem->length; i++) {
        Element* child = get_child_element_at(elem, i);
        if (!child) continue;
        render_svg_element(ctx, child);
    }
}

// ============================================================================
// SVG Defs Processing
// ============================================================================

static void process_svg_defs(SvgRenderContext* ctx, Element* defs) {
    if (!defs) return;

    if (!ctx->defs) {
        SvgDefTable* table = (SvgDefTable*)mem_alloc(sizeof(SvgDefTable), MEM_CAT_RENDER);
        memset(table, 0, sizeof(SvgDefTable));
        ctx->defs = (HashMap*)table;
    }
    SvgDefTable* table = (SvgDefTable*)ctx->defs;

    for (int64_t i = 0; i < defs->length; i++) {
        Element* child = get_child_element_at(defs, i);
        if (!child) continue;

        const char* tag = get_element_tag_name(child);
        if (!tag) continue;
        const char* id  = get_svg_attr(child, "id");

        if (strcmp(tag, "linearGradient") == 0 || strcmp(tag, "radialGradient") == 0) {
            if (table->grad_count >= SVG_MAX_GRAD_DEFS) continue;
            SvgGradDef* def = &table->grads[table->grad_count++];
            memset(def, 0, sizeof(SvgGradDef));
            if (id) str_copy(def->id, sizeof(def->id), id, strlen(id));

            def->is_radial = (strcmp(tag, "radialGradient") == 0);

            const char* gu = get_svg_attr(child, "gradientUnits");
            def->user_space = (gu && strcmp(gu, "userSpaceOnUse") == 0);

            def->x1 = parse_svg_pct_or_num(get_svg_attr(child, "x1"), 0.0f);
            def->y1 = parse_svg_pct_or_num(get_svg_attr(child, "y1"), 0.0f);
            def->x2 = parse_svg_pct_or_num(get_svg_attr(child, "x2"), 1.0f);
            def->y2 = parse_svg_pct_or_num(get_svg_attr(child, "y2"), 0.0f);

            def->cx = parse_svg_pct_or_num(get_svg_attr(child, "cx"), 0.5f);
            def->cy = parse_svg_pct_or_num(get_svg_attr(child, "cy"), 0.5f);
            def->r  = parse_svg_pct_or_num(get_svg_attr(child, "r"),  0.5f);

            for (int64_t s = 0; s < child->length && def->stop_count < SVG_MAX_GRAD_STOPS; s++) {
                Element* stop_elem = get_child_element_at(child, s);
                if (!stop_elem) continue;
                const char* stag = get_element_tag_name(stop_elem);
                if (!stag || strcmp(stag, "stop") != 0) continue;

                SvgGradStop* gs = &def->stops[def->stop_count++];
                gs->offset = parse_svg_pct_or_num(get_svg_attr(stop_elem, "offset"), 0.0f);
                const char* sc = get_svg_attr(stop_elem, "stop-color");
                if (sc) {
                    gs->color = parse_svg_color(sc);
                } else {
                    gs->color.r = 0; gs->color.g = 0; gs->color.b = 0; gs->color.a = 255;
                }
                const char* so = get_svg_attr(stop_elem, "stop-opacity");
                if (so) gs->color.a = (uint8_t)((float)gs->color.a * strtof(so, nullptr));
            }
            log_debug("[SVG] defs: %s id='%s' stops=%d", tag, id ? id : "", def->stop_count);

        } else if (id) {
            if (table->elem_count < SVG_MAX_ELEM_DEFS) {
                SvgElemDef* ed = &table->elems[table->elem_count++];
                str_copy(ed->id, sizeof(ed->id), id, strlen(id));
                ed->elem = child;
            }
        }
    }
}

// ============================================================================
// Main SVG Element Dispatcher
// ============================================================================

static void render_svg_element(SvgRenderContext* ctx, Element* elem) {
    if (!elem) return;

    const char* tag = get_element_tag_name(elem);
    if (!tag) return;

    log_debug("[SVG] rendering element: %s", tag);

    // check for clip-path="url(#id)" attribute and push clip if found
    RdtPath* clip_path = resolve_svg_clip_path(ctx, elem);
    if (clip_path) {
        svg_push_clip(ctx, clip_path, &ctx->transform);
    }

    if (strcmp(tag, "rect") == 0) {
        render_svg_rect(ctx, elem);
    } else if (strcmp(tag, "circle") == 0) {
        render_svg_circle(ctx, elem);
    } else if (strcmp(tag, "ellipse") == 0) {
        render_svg_ellipse(ctx, elem);
    } else if (strcmp(tag, "line") == 0) {
        render_svg_line(ctx, elem);
    } else if (strcmp(tag, "polyline") == 0) {
        render_svg_polyline(ctx, elem, false);
    } else if (strcmp(tag, "polygon") == 0) {
        render_svg_polyline(ctx, elem, true);
    } else if (strcmp(tag, "path") == 0) {
        render_svg_path(ctx, elem);
    } else if (strcmp(tag, "g") == 0) {
        render_svg_group(ctx, elem);
    } else if (strcmp(tag, "defs") == 0) {
        process_svg_defs(ctx, elem);
    } else if (strcmp(tag, "linearGradient") == 0 ||
               strcmp(tag, "radialGradient") == 0 ||
               strcmp(tag, "clipPath") == 0 ||
               strcmp(tag, "mask") == 0 ||
               strcmp(tag, "symbol") == 0 ||
               strcmp(tag, "pattern") == 0) {
        // these are definitions, don't render directly
    } else if (strcmp(tag, "use") == 0) {
        const char* href = get_svg_attr(elem, "href");
        if (!href) href = get_svg_attr(elem, "xlink:href");
        if (href && href[0] == '#' && ctx->defs) {
            Element* ref = lookup_elem_def((SvgDefTable*)ctx->defs, href + 1);
            if (ref) {
                float ux = parse_svg_length(get_svg_attr(elem, "x"), 0.0f);
                float uy = parse_svg_length(get_svg_attr(elem, "y"), 0.0f);
                // compose <use> offset + element transform with accumulated transform
                RdtMatrix saved = ctx->transform;
                Color saved_color = ctx->current_color;
                RdtMatrix el_m = compose_element_transform(ctx, elem);
                if (ux != 0.0f || uy != 0.0f) {
                    RdtMatrix translate = rdt_matrix_translate(ux, uy);
                    ctx->transform = rdt_matrix_multiply(&el_m, &translate);
                } else {
                    ctx->transform = el_m;
                }

                // update currentColor from <use> element's color attribute
                const char* use_color = get_svg_attr(elem, "color");
                if (use_color) {
                    ctx->current_color = parse_svg_color(use_color);
                }

                const char* ref_tag = get_element_tag_name(ref);
                if (ref_tag && strcmp(ref_tag, "symbol") == 0) {
                    // <symbol> needs viewBox/preserveAspectRatio applied
                    const char* vb_attr = get_svg_attr(ref, "viewBox");
                    SvgViewBox vb = parse_svg_viewbox(vb_attr);

                    // <use> width/height override (default: symbol's viewBox or 100%)
                    float sym_w = parse_svg_length(get_svg_attr(elem, "width"), vb.has_viewbox ? vb.width : 0);
                    float sym_h = parse_svg_length(get_svg_attr(elem, "height"), vb.has_viewbox ? vb.height : 0);

                    if (vb.has_viewbox && sym_w > 0 && sym_h > 0) {
                        float sx = sym_w / vb.width;
                        float sy = sym_h / vb.height;
                        const char* par = get_svg_attr(ref, "preserveAspectRatio");
                        bool par_none = par && strcmp(par, "none") == 0;
                        bool par_slice = par && strstr(par, "slice") != nullptr;
                        if (!par_none) {
                            float scale = par_slice ? fmax(sx, sy) : fmin(sx, sy);
                            float align_x = 0.5f, align_y = 0.5f;
                            if (par) {
                                if      (strstr(par, "xMin")) align_x = 0.0f;
                                else if (strstr(par, "xMax")) align_x = 1.0f;
                                if      (strstr(par, "YMin")) align_y = 0.0f;
                                else if (strstr(par, "YMax")) align_y = 1.0f;
                            }
                            float tx = -vb.min_x * scale + (sym_w - vb.width * scale) * align_x;
                            float ty = -vb.min_y * scale + (sym_h - vb.height * scale) * align_y;
                            RdtMatrix vb_m = {scale, 0, tx, 0, scale, ty, 0, 0, 1};
                            ctx->transform = rdt_matrix_multiply(&ctx->transform, &vb_m);
                        } else {
                            float tx = -vb.min_x * sx;
                            float ty = -vb.min_y * sy;
                            RdtMatrix vb_m = {sx, 0, tx, 0, sy, ty, 0, 0, 1};
                            ctx->transform = rdt_matrix_multiply(&ctx->transform, &vb_m);
                        }
                    }
                    // render <symbol> children
                    render_svg_children(ctx, ref);
                    log_debug("[SVG] rendered <use> -> <symbol> href='%s' vb=%s", href, vb_attr ? vb_attr : "(none)");
                } else {
                    render_svg_element(ctx, ref);
                }
                ctx->transform = saved;
                ctx->current_color = saved_color;
            }
        }
        log_debug("[SVG] <use> href='%s' not resolved", href ? href : "(none)");
    } else if (strcmp(tag, "text") == 0) {
        render_svg_text(ctx, elem);
    } else if (strcmp(tag, "image") == 0) {
        render_svg_image(ctx, elem);
    } else if (strcmp(tag, "svg") == 0) {
        // Nested <svg>: applies its own viewBox/x/y/width/height transform,
        // then renders children.  Used by badge SVGs that embed a logo as a
        // sub-svg (e.g. codecov_badge.svg `<svg viewBox="140 -8 60 60">...`).
        const char* vb_attr = get_svg_attr(elem, "viewBox");
        SvgViewBox vb = parse_svg_viewbox(vb_attr);
        float nx = parse_svg_length(get_svg_attr(elem, "x"), 0.0f);
        float ny = parse_svg_length(get_svg_attr(elem, "y"), 0.0f);
        // Per SVG spec, missing width/height defaults to "100%" of the parent
        // viewport (in user coordinates), NOT the viewBox extents. Falling back
        // to viewBox extents would make a viewBox like "140 -8 60 60" map to a
        // 60×60 box that places content far off-canvas.
        float nw = parse_svg_length(get_svg_attr(elem, "width"),  ctx->current_viewport_w);
        float nh = parse_svg_length(get_svg_attr(elem, "height"), ctx->current_viewport_h);
        RdtMatrix saved = ctx->transform;
        float saved_vw = ctx->current_viewport_w;
        float saved_vh = ctx->current_viewport_h;
        RdtMatrix el_m = compose_element_transform(ctx, elem);
        if (nx != 0.0f || ny != 0.0f) {
            RdtMatrix t = rdt_matrix_translate(nx, ny);
            ctx->transform = rdt_matrix_multiply(&el_m, &t);
        } else {
            ctx->transform = el_m;
        }
        if (vb.has_viewbox && vb.width > 0 && vb.height > 0 && nw > 0 && nh > 0) {
            float sx = nw / vb.width;
            float sy = nh / vb.height;
            const char* par = get_svg_attr(elem, "preserveAspectRatio");
            bool par_none  = par && strcmp(par, "none") == 0;
            bool par_slice = par && strstr(par, "slice") != nullptr;
            float scale_x, scale_y, tx, ty;
            if (par_none) {
                scale_x = sx; scale_y = sy;
                tx = -vb.min_x * sx;
                ty = -vb.min_y * sy;
            } else {
                float scale = par_slice ? fmax(sx, sy) : fmin(sx, sy);
                scale_x = scale_y = scale;
                float align_x = 0.5f, align_y = 0.5f;
                if (par) {
                    if      (strstr(par, "xMin")) align_x = 0.0f;
                    else if (strstr(par, "xMax")) align_x = 1.0f;
                    if      (strstr(par, "YMin")) align_y = 0.0f;
                    else if (strstr(par, "YMax")) align_y = 1.0f;
                }
                tx = -vb.min_x * scale + (nw - vb.width  * scale) * align_x;
                ty = -vb.min_y * scale + (nh - vb.height * scale) * align_y;
            }
            RdtMatrix vb_m = {scale_x, 0, tx, 0, scale_y, ty, 0, 0, 1};
            ctx->transform = rdt_matrix_multiply(&ctx->transform, &vb_m);
            // children of this <svg> see a viewport sized by the viewBox extents
            ctx->current_viewport_w = vb.width;
            ctx->current_viewport_h = vb.height;
        } else {
            // no viewBox: children inherit nw/nh as their viewport
            if (nw > 0) ctx->current_viewport_w = nw;
            if (nh > 0) ctx->current_viewport_h = nh;
        }
        render_svg_children(ctx, elem);
        ctx->transform = saved;
        ctx->current_viewport_w = saved_vw;
        ctx->current_viewport_h = saved_vh;
    } else {
        // unknown element - try rendering children
        render_svg_children(ctx, elem);
    }

    if (clip_path) {
        svg_pop_clip(ctx);
        rdt_path_free(clip_path);
    }
}

// ============================================================================
// Build SVG Scene
// ============================================================================

void render_svg_to_vec(RdtVector* vec, Element* svg_element, float viewport_width, float viewport_height,
                       Pool* pool, float pixel_ratio, FontContext* font_ctx, const RdtMatrix* base_transform,
                       DisplayList* dl) {
    if (!svg_element || !vec) return;

    log_debug("[SVG] render_svg_to_vec: viewport %.0fx%.0f pixel_ratio=%.2f font_ctx=%p", viewport_width, viewport_height, pixel_ratio, (void*)font_ctx);

    // initialize render context
    SvgRenderContext ctx = {};
    ctx.svg_root = svg_element;
    ctx.pool = pool;
    ctx.font_ctx = font_ctx;
    ctx.vec = vec;
    ctx.dl = dl;
    ctx.pixel_ratio = (pixel_ratio > 0) ? pixel_ratio : 1.0f;
    ctx.fill_color.r = 0; ctx.fill_color.g = 0; ctx.fill_color.b = 0; ctx.fill_color.a = 255;  // default black
    ctx.stroke_color.r = 0; ctx.stroke_color.g = 0; ctx.stroke_color.b = 0; ctx.stroke_color.a = 0;  // default none
    ctx.current_color.r = 0; ctx.current_color.g = 0; ctx.current_color.b = 0; ctx.current_color.a = 255;  // default black
    ctx.stroke_width = 1.0f;
    ctx.opacity = 1.0f;
    ctx.fill_none = false;
    ctx.stroke_none = true;

    // start with base transform (document position/scale)
    ctx.transform = base_transform ? *base_transform : rdt_matrix_identity();

    // parse viewBox
    const char* viewbox_attr = get_svg_attr(svg_element, "viewBox");
    if (!viewbox_attr) viewbox_attr = get_svg_attr(svg_element, "viewbox");
    SvgViewBox vb = parse_svg_viewbox(viewbox_attr);

    // implicit viewBox: when an SVG has no viewBox but has explicit width/height
    // attributes, browsers treat the intrinsic dims as an implicit viewBox so
    // that content scales to fit the rendered viewport (this matches the way
    // browsers paint <img src=svg> at its CSS box size, and avoids leaving the
    // canvas mostly empty when SVG intrinsic size differs from the viewport).
    if (!vb.has_viewbox && viewport_width > 0 && viewport_height > 0) {
        const char* w_attr = get_svg_attr(svg_element, "width");
        const char* h_attr = get_svg_attr(svg_element, "height");
        if (w_attr && *w_attr && h_attr && *h_attr) {
            float intrinsic_w = parse_svg_length(w_attr, 0.0f);
            float intrinsic_h = parse_svg_length(h_attr, 0.0f);
            if (intrinsic_w > 0 && intrinsic_h > 0 &&
                (intrinsic_w != viewport_width || intrinsic_h != viewport_height)) {
                vb.min_x = 0;  vb.min_y = 0;
                vb.width = intrinsic_w;  vb.height = intrinsic_h;
                vb.has_viewbox = true;
            }
        }
    }

    // initial viewport in user-coordinate units. With a viewBox this is the
    // viewBox extents; without one, user coords == viewport pixels.
    ctx.current_viewport_w = (vb.has_viewbox && vb.width > 0) ? vb.width : viewport_width;
    ctx.current_viewport_h = (vb.has_viewbox && vb.height > 0) ? vb.height : viewport_height;

    if (vb.has_viewbox && vb.width > 0 && vb.height > 0) {
        ctx.viewbox_x = vb.min_x;
        ctx.viewbox_y = vb.min_y;
        ctx.viewbox_width = vb.width;
        ctx.viewbox_height = vb.height;
        ctx.scale_x = viewport_width / vb.width;
        ctx.scale_y = viewport_height / vb.height;
        // handle preserveAspectRatio
        const char* par = get_svg_attr(svg_element, "preserveAspectRatio");
        bool par_none  = par && strcmp(par, "none") == 0;
        bool par_slice = par && strstr(par, "slice") != nullptr;
        if (par_none) {
            ctx.translate_x = -vb.min_x * ctx.scale_x;
            ctx.translate_y = -vb.min_y * ctx.scale_y;
        } else {
            float scale = par_slice
                ? (ctx.scale_x > ctx.scale_y ? ctx.scale_x : ctx.scale_y)
                : (ctx.scale_x < ctx.scale_y ? ctx.scale_x : ctx.scale_y);
            ctx.scale_x = ctx.scale_y = scale;
            float align_x = 0.5f, align_y = 0.5f;
            if (par) {
                if      (strstr(par, "xMin")) align_x = 0.0f;
                else if (strstr(par, "xMax")) align_x = 1.0f;
                if      (strstr(par, "YMin")) align_y = 0.0f;
                else if (strstr(par, "YMax")) align_y = 1.0f;
            }
            ctx.translate_x = -vb.min_x * scale + (viewport_width  - vb.width  * scale) * align_x;
            ctx.translate_y = -vb.min_y * scale + (viewport_height - vb.height * scale) * align_y;
        }

        // compose viewBox transform with base transform
        RdtMatrix vb_matrix = {
            ctx.scale_x, 0, ctx.translate_x,
            0, ctx.scale_y, ctx.translate_y,
            0, 0, 1
        };
        ctx.transform = rdt_matrix_multiply(&ctx.transform, &vb_matrix);
    } else {
        ctx.scale_x = ctx.scale_y = 1.0f;
        ctx.translate_x = ctx.translate_y = 0;
    }

    // pre-pass: process all <defs> AND any top-level definition elements so
    // gradients, masks, clipPaths, symbols and id'd elements are ready before
    // rendering shapes.  Many real-world SVGs (badges, shields.io output)
    // place <linearGradient>/<mask> directly under the root, not in <defs>.
    for (int64_t i = 0; i < svg_element->length; i++) {
        Element* child = get_child_element_at(svg_element, i);
        if (!child) continue;
        const char* child_tag = get_element_tag_name(child);
        if (!child_tag) continue;
        if (strcmp(child_tag, "defs") == 0) {
            process_svg_defs(&ctx, child);
        } else if (strcmp(child_tag, "linearGradient") == 0 ||
                   strcmp(child_tag, "radialGradient") == 0 ||
                   strcmp(child_tag, "clipPath") == 0 ||
                   strcmp(child_tag, "mask") == 0 ||
                   strcmp(child_tag, "symbol") == 0 ||
                   strcmp(child_tag, "pattern") == 0) {
            // Treat the root as if it were a <defs> container for this child.
            // process_svg_defs iterates the *children* of its argument, so we
            // wrap by passing a synthetic single-child view: easiest is to
            // process inline here.
            if (!ctx.defs) {
                SvgDefTable* table = (SvgDefTable*)mem_alloc(sizeof(SvgDefTable), MEM_CAT_RENDER);
                memset(table, 0, sizeof(SvgDefTable));
                ctx.defs = (HashMap*)table;
            }
            // reuse the same logic by constructing a minimal pseudo-defs walk:
            // call process_svg_defs on the parent, but with only this child.
            // Simpler: directly handle the gradient/elem-def cases here.
            SvgDefTable* table = (SvgDefTable*)ctx.defs;
            const char* id = get_svg_attr(child, "id");
            if (strcmp(child_tag, "linearGradient") == 0 || strcmp(child_tag, "radialGradient") == 0) {
                if (table->grad_count < SVG_MAX_GRAD_DEFS) {
                    SvgGradDef* def = &table->grads[table->grad_count++];
                    memset(def, 0, sizeof(SvgGradDef));
                    if (id) str_copy(def->id, sizeof(def->id), id, strlen(id));
                    def->is_radial = (strcmp(child_tag, "radialGradient") == 0);
                    const char* gu = get_svg_attr(child, "gradientUnits");
                    def->user_space = (gu && strcmp(gu, "userSpaceOnUse") == 0);
                    def->x1 = parse_svg_pct_or_num(get_svg_attr(child, "x1"), 0.0f);
                    def->y1 = parse_svg_pct_or_num(get_svg_attr(child, "y1"), 0.0f);
                    def->x2 = parse_svg_pct_or_num(get_svg_attr(child, "x2"), 1.0f);
                    def->y2 = parse_svg_pct_or_num(get_svg_attr(child, "y2"), 0.0f);
                    def->cx = parse_svg_pct_or_num(get_svg_attr(child, "cx"), 0.5f);
                    def->cy = parse_svg_pct_or_num(get_svg_attr(child, "cy"), 0.5f);
                    def->r  = parse_svg_pct_or_num(get_svg_attr(child, "r"),  0.5f);
                    for (int64_t s = 0; s < child->length && def->stop_count < SVG_MAX_GRAD_STOPS; s++) {
                        Element* stop_elem = get_child_element_at(child, s);
                        if (!stop_elem) continue;
                        const char* stag = get_element_tag_name(stop_elem);
                        if (!stag || strcmp(stag, "stop") != 0) continue;
                        SvgGradStop* gs = &def->stops[def->stop_count++];
                        gs->offset = parse_svg_pct_or_num(get_svg_attr(stop_elem, "offset"), 0.0f);
                        const char* sc = get_svg_attr(stop_elem, "stop-color");
                        if (sc) gs->color = parse_svg_color(sc);
                        else { gs->color.r = 0; gs->color.g = 0; gs->color.b = 0; gs->color.a = 255; }
                        const char* so = get_svg_attr(stop_elem, "stop-opacity");
                        if (so) gs->color.a = (uint8_t)((float)gs->color.a * strtof(so, nullptr));
                    }
                    log_debug("[SVG] root-level def: %s id='%s' stops=%d", child_tag, id ? id : "", def->stop_count);
                }
            } else if (id && table->elem_count < SVG_MAX_ELEM_DEFS) {
                SvgElemDef* ed = &table->elems[table->elem_count++];
                str_copy(ed->id, sizeof(ed->id), id, strlen(id));
                ed->elem = child;
            }
        }
    }

    // render children directly to vec
    for (int64_t i = 0; i < svg_element->length; i++) {
        Element* child = get_child_element_at(svg_element, i);
        if (!child) continue;
        render_svg_element(&ctx, child);
    }

    log_debug("[SVG] render_svg_to_vec complete");
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

    // compute document position
    float x = rdcon->block.x + view->x * scale;
    float y = rdcon->block.y + view->y * scale;

    log_debug("[SVG] render_inline_svg: doc pos=(%.1f,%.1f) block pos=(%.1f,%.1f) clip=(%.1f,%.1f,%.1f,%.1f)",
              x, y, rdcon->block.x, rdcon->block.y,
              rdcon->block.clip.left, rdcon->block.clip.top,
              rdcon->block.clip.right, rdcon->block.clip.bottom);

    // build base transform: Translate(x,y) * Scale(scale)
    RdtMatrix base_transform = {
        scale, 0, x,
        0, scale, y,
        0, 0, 1
    };

    // apply document transform if any
    if (rdcon->has_transform) {
        base_transform = rdt_matrix_multiply(&rdcon->transform, &base_transform);
    }

    // apply clip region (e.g. iframe content box, overflow:hidden)
    Bound* clip = &rdcon->block.clip;
    float clip_w = clip->right - clip->left;
    float clip_h = clip->bottom - clip->top;
    bool has_clip = (clip_w > 0 && clip_h > 0);
    if (has_clip) {
        RdtPath* clip_path = rdt_path_new();
        rdt_path_add_rect(clip_path, clip->left, clip->top, clip_w, clip_h, 0, 0);
        rc_push_clip(rdcon, clip_path, nullptr);
        rdt_path_free(clip_path);
    }

    // render SVG directly to the framebuffer
    FontContext* font_ctx = rdcon->ui_context ? rdcon->ui_context->font_ctx : nullptr;
    render_svg_to_vec(&rdcon->vec, svg_elem, view->width, view->height,
                      rdcon->ui_context->document->pool, scale, font_ctx, &base_transform,
                      rdcon->dl);

    if (has_clip) {
        rc_pop_clip(rdcon);
    }

    log_debug("[SVG] render_inline_svg: rendered to buffer");
}
