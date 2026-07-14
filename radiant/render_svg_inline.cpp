/**
 * render_svg_inline.cpp - Inline SVG Rendering via PaintIR/DisplayList
 *
 * Converts SVG element trees to paint/display-list commands with accumulated
 * transforms. No ThorVG scene tree is constructed for the inline path.
 */

#include "render.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/tagged.hpp"
#include "../lib/mem_factory.h"
#include "../lib/log.h"
#include "../lib/arena.h"
#include "../lib/font/font.h"
#include "../lib/font/font_internal.h"
#include "../lib/mempool.h"
#include "../lib/str.h"
#include "../lib/file.h"
#include "../lib/escape.h"
#include "../lib/color.h"
#include <string.h>
#include "../lib/mem.h"
#include "../lib/base64.h"
#include <ctype.h>
#include <math.h>
#include <strings.h>
#include <inttypes.h>

#ifndef LAMBDA_HEADLESS
#include <thorvg_capi.h>  // needed for SVG text rendering (tvg_text_* API)
#endif

static const int SVG_RESOURCE_STACK_MAX = 32;
static thread_local const char* g_svg_resource_stack[SVG_RESOURCE_STACK_MAX];
static thread_local int g_svg_resource_stack_depth = 0;
static thread_local RenderContext* g_svg_active_rdcon = nullptr;

struct SvgImageResolverEntry {
    Element* svg_root;
    Item pdf_root;
    SvgImageResolverEntry* next;
};

static SvgImageResolverEntry* g_svg_image_resolvers = nullptr;

static bool svg_item_number_equals(ItemReader item, int value);
static const char* svg_pdf_registered_image_resolver(void* context, int object_num);

static SvgImageResolverEntry* svg_find_image_resolver_entry(Element* svg_root) {
    for (SvgImageResolverEntry* entry = g_svg_image_resolvers; entry; entry = entry->next) {
        if (entry->svg_root == svg_root) return entry;
    }
    return nullptr;
}

extern "C" void svg_register_pdf_image_resolver(Element* svg_root, Item pdf_root) {
    if (!svg_root || get_type_id(pdf_root) != LMD_TYPE_MAP) return;

    SvgImageResolverEntry* entry = svg_find_image_resolver_entry(svg_root);
    if (!entry) {
        entry = (SvgImageResolverEntry*)mem_calloc(1, sizeof(SvgImageResolverEntry), MEM_CAT_RENDER);
        if (!entry) return;
        entry->svg_root = svg_root;
        entry->next = g_svg_image_resolvers;
        g_svg_image_resolvers = entry;
    }
    entry->pdf_root = pdf_root;
}

static bool svg_element_tree_contains(Element* root, Element* needle) {
    if (!root || !needle) return false;
    if (root == needle) return true;
    for (int64_t i = 0; i < root->length; i++) {
        Item child = root->items[i];
        if (get_type_id(child) == LMD_TYPE_ELEMENT && svg_element_tree_contains(child.element, needle)) {
            return true;
        }
    }
    return false;
}

extern "C" void svg_unregister_image_resolvers_for_tree(Element* root) {
    if (!root) return;
    SvgImageResolverEntry** link = &g_svg_image_resolvers;
    while (*link) {
        SvgImageResolverEntry* entry = *link;
        if (svg_element_tree_contains(root, entry->svg_root)) {
            *link = entry->next;
            mem_free(entry);
        } else {
            link = &entry->next;
        }
    }
}

extern "C" bool svg_get_registered_image_resolver(Element* svg_root,
                                                   SvgImageResolverFn* out_resolver,
                                                   void** out_context) {
    if (out_resolver) *out_resolver = nullptr;
    if (out_context) *out_context = nullptr;
    SvgImageResolverEntry* entry = svg_find_image_resolver_entry(svg_root);
    if (!entry) return false;
    if (out_resolver) *out_resolver = svg_pdf_registered_image_resolver;
    if (out_context) *out_context = entry;
    return true;
}

extern "C" Item pdf_register_svg_image_resolver(Item svg_item, Item pdf_item) {
    if (get_type_id(svg_item) == LMD_TYPE_ELEMENT) {
        svg_register_pdf_image_resolver(svg_item.element, pdf_item);
    }
    return svg_item;
}

extern "C" Item fn_pdf_register_svg_image_resolver(Item svg_item, Item pdf_item) {
    return pdf_register_svg_image_resolver(svg_item, pdf_item);
}

static bool svg_resource_stack_contains(const char* path) {
    if (!path || !*path) return false;
    for (int i = 0; i < g_svg_resource_stack_depth; i++) {
        const char* entry = g_svg_resource_stack[i];
        if (entry && strcmp(entry, path) == 0) return true;
    }
    return false;
}

static bool svg_resource_stack_push(const char* path) {
    if (!path || !*path) return false;
    if (svg_resource_stack_contains(path)) return false;
    if (g_svg_resource_stack_depth >= SVG_RESOURCE_STACK_MAX) return false;
    g_svg_resource_stack[g_svg_resource_stack_depth++] = path;
    return true;
}

static void svg_resource_stack_pop(const char* path) {
    if (!path || !*path || g_svg_resource_stack_depth <= 0) return;
    const char* top = g_svg_resource_stack[g_svg_resource_stack_depth - 1];
    if (top == path || (top && strcmp(top, path) == 0)) {
        g_svg_resource_stack[--g_svg_resource_stack_depth] = nullptr;
    }
}

// ============================================================================
// Forward Declarations
// ============================================================================

typedef struct SvgViewBox {
    float min_x;
    float min_y;
    float width;
    float height;
    bool has_viewbox;
} SvgViewBox;

static SvgViewBox parse_svg_viewbox(const char* viewbox_attr);
static float parse_svg_length(const char* value, float default_value);
static Color parse_svg_color(const char* value);
static bool parse_svg_transform(const char* transform_str, float matrix[6]);

// ---------------------------------------------------------------------------
// SVG PaintIR/display-list dispatch helpers
// ---------------------------------------------------------------------------
static inline PaintRecordTarget svg_record_target(SvgInlineRenderContext* ctx) {
    PaintRecordTarget target = {
        ctx ? ctx->paint_list : nullptr,
        ctx ? ctx->dl : nullptr,
        "SVG"
    };
    return target;
}

static inline void svg_fill_path(SvgInlineRenderContext* ctx, RdtPath* path, Color color,
                                 RdtFillRule rule, const RdtMatrix* xform) {
    PaintRecordTarget target = svg_record_target(ctx);
    paint_record_fill_path(&target, "svg_fill_path", path, color, rule, xform);
}

static inline void svg_stroke_path(SvgInlineRenderContext* ctx, RdtPath* path, Color color, float width,
                                   RdtStrokeCap cap, RdtStrokeJoin join,
                                   const float* dash, int dash_count, const RdtMatrix* xform) {
    PaintRecordTarget target = svg_record_target(ctx);
    paint_record_stroke_path(&target, "svg_stroke_path", path, color, width,
                             cap, join, dash, dash_count, 0, xform);
}
static inline void svg_fill_linear_gradient(SvgInlineRenderContext* ctx, RdtPath* path,
                                            float x1, float y1, float x2, float y2,
                                            const RdtGradientStop* stops, int count,
                                            RdtFillRule rule, const RdtMatrix* xform,
                                            const RdtMatrix* gradient_xform) {
    PaintRecordTarget target = svg_record_target(ctx);
    paint_record_fill_linear_gradient(&target, "svg_fill_linear_gradient",
                                      path, x1, y1, x2, y2,
                                      stops, count, rule, xform, gradient_xform);
}
static inline void svg_fill_radial_gradient(SvgInlineRenderContext* ctx, RdtPath* path,
                                            float cx, float cy, float r,
                                            const RdtGradientStop* stops, int count,
                                            RdtFillRule rule, const RdtMatrix* xform,
                                            const RdtMatrix* gradient_xform) {
    PaintRecordTarget target = svg_record_target(ctx);
    paint_record_fill_radial_gradient(&target, "svg_fill_radial_gradient",
                                      path, cx, cy, r, stops, count, rule, xform,
                                      gradient_xform);
}
static inline void svg_draw_picture(SvgInlineRenderContext* ctx, RdtPicture* pic,
                                    uint8_t opacity, const RdtMatrix* xform) {
    PaintRecordTarget target = svg_record_target(ctx);
    paint_record_draw_picture(&target, "svg_draw_picture", pic, opacity, xform);
}
static inline void svg_push_clip(SvgInlineRenderContext* ctx, RdtPath* path, const RdtMatrix* xform) {
    PaintRecordTarget target = svg_record_target(ctx);
    paint_record_push_clip(&target, "svg_push_clip", path, xform);
}
static inline void svg_pop_clip(SvgInlineRenderContext* ctx) {
    PaintRecordTarget target = svg_record_target(ctx);
    paint_record_pop_clip(&target, "svg_pop_clip");
}

static inline void svg_save_backdrop(SvgInlineRenderContext* ctx, int x0, int y0, int w, int h) {
    PaintRecordTarget target = svg_record_target(ctx);
    paint_record_save_backdrop(&target, "svg_save_backdrop", x0, y0, w, h);
}

static inline void svg_composite_opacity(SvgInlineRenderContext* ctx, int x0, int y0, int w, int h,
                                         float opacity, bool premultiplied_source = false) {
    PaintRecordTarget target = svg_record_target(ctx);
    paint_record_composite_opacity(&target, "svg_composite_opacity",
                                   x0, y0, w, h, opacity, premultiplied_source);
}

static inline void svg_box_blur_region(SvgInlineRenderContext* ctx, int rx, int ry, int rw, int rh,
                                       float blur_radius, int clip_type, const float* clip_params,
                                       int exclude_type, const float* exclude_params,
                                       bool premultiply_source,
                                       bool tint_source, Color tint_color) {
    PaintRecordTarget target = svg_record_target(ctx);
    paint_record_box_blur_region(&target, "svg_box_blur_region",
                                 rx, ry, rw, rh, blur_radius,
                                 clip_type, clip_params, exclude_type, exclude_params,
                                 premultiply_source, tint_source, tint_color);
}

static void render_svg_element(SvgInlineRenderContext* ctx, Element* elem);
static void render_svg_rect(SvgInlineRenderContext* ctx, Element* elem);
static void render_svg_circle(SvgInlineRenderContext* ctx, Element* elem);
static void render_svg_ellipse(SvgInlineRenderContext* ctx, Element* elem);
static void render_svg_line(SvgInlineRenderContext* ctx, Element* elem);
static void render_svg_polyline(SvgInlineRenderContext* ctx, Element* elem, bool close_path);
static void render_svg_path(SvgInlineRenderContext* ctx, Element* elem);
static void render_svg_text(SvgInlineRenderContext* ctx, Element* elem);
static void render_svg_image(SvgInlineRenderContext* ctx, Element* elem);
static void render_svg_group(SvgInlineRenderContext* ctx, Element* elem);
static void render_svg_children(SvgInlineRenderContext* ctx, Element* elem);
static void process_svg_defs(SvgInlineRenderContext* ctx, Element* defs);

// ============================================================================
// Helper: Get Attribute from Lambda Element
// ============================================================================

static const char* extract_element_attribute(Element* element, const char* attr_name, Arena* arena) {
    (void)arena;  // not used in this implementation
    if (!element || !attr_name) return nullptr;
    ConstItem attr_value = element->get_attr(attr_name);
    String* string_value = attr_value.string();
    return string_value ? string_value->chars : nullptr;
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

struct SvgStyleRule {
    char selector[128];
    char name[64];
    char value[256];
    int specificity;
    int order;
};

static void svg_copy_trim(char* dst, size_t dst_size, const char* start, const char* end) {
    if (!dst || dst_size == 0) return;
    if (!start || !end || end <= start) { dst[0] = '\0'; return; }
    while (start < end && isspace((unsigned char)*start)) start++;
    while (end > start && isspace((unsigned char)end[-1])) end--;
    size_t len = (size_t)(end - start);
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, start, len);
    dst[len] = '\0';
}

static bool svg_class_list_contains(const char* class_attr, const char* cls, size_t cls_len) {
    if (!class_attr || !cls || cls_len == 0) return false;
    const char* p = class_attr;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        const char* start = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if ((size_t)(p - start) == cls_len && strncmp(start, cls, cls_len) == 0) return true;
    }
    return false;
}

static int svg_selector_specificity(const char* selector) {
    if (!selector || !*selector) return 0;
    int score = 0;
    for (const char* p = selector; *p; p++) {
        if (*p == '#') score += 100;
        else if (*p == '.') score += 10;
    }
    if (selector[0] != '#' && selector[0] != '.' && selector[0] != '*') score += 1;
    return score;
}

static bool svg_simple_selector_matches(Element* elem, const char* selector) {
    if (!elem || !selector || !*selector) return false;
    while (isspace((unsigned char)*selector)) selector++;
    if (strchr(selector, ' ') || strchr(selector, '>') || strchr(selector, '+') ||
        strchr(selector, '~') || strchr(selector, ',')) {
        return false;
    }
    if (strcmp(selector, "*") == 0) return true;

    const char* tag = get_element_tag_name(elem);
    const char* id = get_svg_attr(elem, "id");
    const char* cls_attr = get_svg_attr(elem, "class");
    const char* p = selector;
    if (*p && *p != '#' && *p != '.') {
        const char* start = p;
        while (*p && *p != '#' && *p != '.') p++;
        if (!tag || (size_t)(p - start) != strlen(tag) || strncmp(start, tag, (size_t)(p - start)) != 0) return false;
    }
    while (*p) {
        if (*p == '#') {
            p++;
            const char* start = p;
            while (*p && *p != '.' && *p != '#') p++;
            if (!id || (size_t)(p - start) != strlen(id) || strncmp(start, id, (size_t)(p - start)) != 0) return false;
        } else if (*p == '.') {
            p++;
            const char* start = p;
            while (*p && *p != '.' && *p != '#') p++;
            if (!svg_class_list_contains(cls_attr, start, (size_t)(p - start))) return false;
        } else {
            return false;
        }
    }
    return true;
}

static const char* get_svg_style_rule_value(SvgInlineRenderContext* ctx, Element* elem, const char* name, char* buffer, size_t buffer_size) {
    if (!ctx || !ctx->style_rules || !elem || !name || !buffer || buffer_size == 0) return nullptr;
    SvgStyleRule* rules = (SvgStyleRule*)ctx->style_rules;
    SvgStyleRule* best = nullptr;
    for (int i = 0; i < ctx->style_rule_count; i++) {
        if (strcmp(rules[i].name, name) != 0) continue;
        if (!svg_simple_selector_matches(elem, rules[i].selector)) continue;
        if (!best || rules[i].specificity > best->specificity ||
            (rules[i].specificity == best->specificity && rules[i].order > best->order)) {
            best = &rules[i];
        }
    }
    if (!best) return nullptr;
    str_copy(buffer, buffer_size, best->value, strlen(best->value));
    return buffer;
}

static bool svg_style_name_matches(const char* style, const char* name, size_t name_len) {
    if (strncmp(style, name, name_len) != 0) return false;
    const char* p = style + name_len;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return *p == ':';
}

static const char* get_svg_attr_or_style(SvgInlineRenderContext* ctx, Element* elem, const char* name, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return nullptr;

    const char* style = get_svg_attr(elem, "style");
    if (style) {
        size_t name_len = strlen(name);
        const char* p = style;
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ';') p++;
            if (!*p) break;

            if (svg_style_name_matches(p, name, name_len)) {
                p += name_len;
                while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
                if (*p == ':') p++;
                while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

                const char* value_start = p;
                while (*p && *p != ';') p++;
                svg_copy_trim(buffer, buffer_size, value_start, p);
                return buffer;
            }

            while (*p && *p != ';') p++;
            if (*p == ';') p++;
        }
    }

    const char* rule_value = get_svg_style_rule_value(ctx, elem, name, buffer, buffer_size);
    if (rule_value) return rule_value;

    const char* attr = get_svg_attr(elem, name);
    if (attr) return attr;
    return nullptr;
}

static void svg_add_style_rule(SvgInlineRenderContext* ctx, const char* selector_start, const char* selector_end,
                               const char* name_start, const char* name_end,
                               const char* value_start, const char* value_end) {
    if (!ctx || !selector_start || !name_start || !value_start) return;
    if (ctx->style_rule_count >= 256) return;
    if (!ctx->style_rules) {
        ctx->style_rule_capacity = 64;
        ctx->style_rules = mem_calloc(ctx->style_rule_capacity, sizeof(SvgStyleRule), MEM_CAT_RENDER);
    } else if (ctx->style_rule_count >= ctx->style_rule_capacity) {
        int new_cap = ctx->style_rule_capacity * 2;
        SvgStyleRule* old_rules = (SvgStyleRule*)ctx->style_rules;
        SvgStyleRule* new_rules = (SvgStyleRule*)mem_calloc(new_cap, sizeof(SvgStyleRule), MEM_CAT_RENDER);
        if (!new_rules) return;
        memcpy(new_rules, old_rules, sizeof(SvgStyleRule) * ctx->style_rule_count);
        mem_free(old_rules);
        ctx->style_rules = new_rules;
        ctx->style_rule_capacity = new_cap;
    }
    SvgStyleRule* rules = (SvgStyleRule*)ctx->style_rules;
    SvgStyleRule* rule = &rules[ctx->style_rule_count];
    svg_copy_trim(rule->selector, sizeof(rule->selector), selector_start, selector_end);
    svg_copy_trim(rule->name, sizeof(rule->name), name_start, name_end);
    svg_copy_trim(rule->value, sizeof(rule->value), value_start, value_end);
    if (!rule->selector[0] || !rule->name[0] || !rule->value[0]) return;
    rule->specificity = svg_selector_specificity(rule->selector);
    rule->order = ctx->style_rule_count;
    ctx->style_rule_count++;
}

static void parse_svg_style_text(SvgInlineRenderContext* ctx, const char* css) {
    if (!ctx || !css) return;
    const char* p = css;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        const char* selector_start = p;
        while (*p && *p != '{') p++;
        if (*p != '{') break;
        const char* selector_end = p++;
        const char* block_end = strchr(p, '}');
        if (!block_end) break;
        const char* d = p;
        while (d < block_end) {
            while (d < block_end && (isspace((unsigned char)*d) || *d == ';')) d++;
            const char* name_start = d;
            while (d < block_end && *d != ':' && *d != ';') d++;
            if (d >= block_end || *d != ':') break;
            const char* name_end = d++;
            const char* value_start = d;
            while (d < block_end && *d != ';') d++;
            svg_add_style_rule(ctx, selector_start, selector_end, name_start, name_end, value_start, d);
            if (d < block_end && *d == ';') d++;
        }
        p = block_end + 1;
    }
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

static SvgViewBox parse_svg_viewbox(const char* viewbox_attr) {
    SvgViewBox vb = {0, 0, 0, 0, false};
    if (!viewbox_attr || !*viewbox_attr) return vb;

    // parse "min-x min-y width height"
    // separators can be comma or whitespace
    float values[4];
    int count = 0;
    const char* p = viewbox_attr;

    while (*p && count < 4) {
        // skip whitespace and commas
        while (*p && (str_char_is_ascii_space(*p) || *p == ',')) p++;
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

static float parse_svg_length(const char* value, float default_value) {
    if (!value || !*value) return default_value;

    char* end;
    float num = strtof(value, &end);
    if (end == value) return default_value;

    // skip whitespace after number
    while (*end && str_char_is_ascii_space(*end)) end++;

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

static Color parse_svg_color(const char* value) {
    Color c;
    c.r = 0; c.g = 0; c.b = 0; c.a = 255;  // default black
    if (!value || !*value) return c;

    // skip whitespace
    while (*value && str_char_is_ascii_space(*value)) value++;

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
        uint8_t r, g, b, a;
        // Shared parser rejects malformed long hex colors instead of folding
        // invalid nibbles into negative channel math.
        if (color_parse_hex(value, &r, &g, &b, &a)) {
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

static Color svg_resolve_color_keyword(SvgInlineRenderContext* ctx, const char* value) {
    if (ctx && value && strcasecmp(value, "currentColor") == 0) {
        return ctx->current_color;
    }
    return parse_svg_color(value);
}

static void svg_apply_inherited_paint_attrs(SvgInlineRenderContext* ctx, Element* elem) {
    if (!ctx || !elem) return;

    char color_buf[256];
    const char* color_attr = get_svg_attr_or_style(ctx, elem, "color", color_buf, sizeof(color_buf));
    if (color_attr) {
        ctx->current_color = parse_svg_color(color_attr);
    }

    char fill_buf[256];
    const char* fill = get_svg_attr_or_style(ctx, elem, "fill", fill_buf, sizeof(fill_buf));
    if (fill) {
        if (strcmp(fill, "none") == 0) {
            ctx->fill_none = true;
        } else if (strncmp(fill, "url(#", 5) != 0) {
            ctx->fill_color = svg_resolve_color_keyword(ctx, fill);
            ctx->fill_none = false;
        }
    }

    char stroke_buf[256];
    const char* stroke = get_svg_attr_or_style(ctx, elem, "stroke", stroke_buf, sizeof(stroke_buf));
    if (stroke) {
        if (strcmp(stroke, "none") == 0) {
            ctx->stroke_none = true;
        } else {
            ctx->stroke_color = svg_resolve_color_keyword(ctx, stroke);
            ctx->stroke_none = false;
        }
    }

    char stroke_width_buf[64];
    const char* stroke_width = get_svg_attr_or_style(ctx, elem, "stroke-width", stroke_width_buf, sizeof(stroke_width_buf));
    if (stroke_width) {
        ctx->stroke_width = parse_svg_length(stroke_width, 1.0f);
    }
}

void render_svg_initial_paint(const ViewSpan* view, Color current_color,
                              SvgInitialPaint* paint) {
    if (!paint) return;
    *paint = {};
    paint->current_color = current_color;
    paint->stroke_none = true;
    paint->stroke_width = -1.0f;
    if (!view || !view->in_line) return;

    InlineProp* in_line = view->in_line;
    if (in_line->has_color) paint->current_color = in_line->color;
    if (in_line->has_svg_fill) {
        paint->fill_none = in_line->svg_fill_none;
        paint->has_fill_color = !paint->fill_none;
        if (paint->has_fill_color) paint->fill_color = in_line->svg_fill_color;
    }
    if (in_line->has_svg_stroke) {
        paint->stroke_none = in_line->svg_stroke_none;
        paint->has_stroke_color = !paint->stroke_none;
        if (paint->has_stroke_color) paint->stroke_color = in_line->svg_stroke_color;
    }
    if (in_line->has_svg_stroke_width) paint->stroke_width = in_line->svg_stroke_width;
}

// ============================================================================
// SVG Transform Parsing
// ============================================================================

static bool parse_svg_transform(const char* transform_str, float matrix[6]) {
    if (!transform_str || !matrix) return false;

    // initialize to identity matrix: [a, b, c, d, e, f] = [1, 0, 0, 1, 0, 0]
    matrix[0] = 1; matrix[1] = 0;  // a, b
    matrix[2] = 0; matrix[3] = 1;  // c, d
    matrix[4] = 0; matrix[5] = 0;  // e, f (translation)

    const char* p = transform_str;

    while (*p) {
        // skip whitespace
        while (*p && str_char_is_ascii_space(*p)) p++;
        if (!*p) break;

        float local[6] = {1, 0, 0, 1, 0, 0};

        if (strncmp(p, "translate", 9) == 0) {
            p += 9;
            while (*p && *p != '(') p++;
            if (*p == '(') {
                p++;
                float tx = 0, ty = 0;
                tx = strtof(p, (char**)&p);
                while (*p && (str_char_is_ascii_space(*p) || *p == ',')) p++;
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
                while (*p && (str_char_is_ascii_space(*p) || *p == ',')) p++;
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
                while (*p && (str_char_is_ascii_space(*p) || *p == ',')) p++;
                if (*p && *p != ')') {
                    float cx = strtof(p, (char**)&p);
                    while (*p && (str_char_is_ascii_space(*p) || *p == ',')) p++;
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
                    while (*p && (str_char_is_ascii_space(*p) || *p == ',')) p++;
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
    if (!viewbox_attr) viewbox_attr = get_svg_attr(svg_element, "viewbox");

    SvgViewBox vb = parse_svg_viewbox(viewbox_attr);

    // determine width. A viewBox provides a coordinate system and aspect ratio,
    // but not an explicit intrinsic width/height attribute.
    if (width_attr && *width_attr) {
        size.width = parse_svg_length(width_attr, 300);
        size.has_intrinsic_width = true;
    } else if (vb.has_viewbox && vb.width > 0) {
        size.width = vb.width;
    }

    // determine height
    if (height_attr && *height_attr) {
        size.height = parse_svg_length(height_attr, 150);
        size.has_intrinsic_height = true;
    } else if (vb.has_viewbox && vb.height > 0) {
        size.height = vb.height;
    }

    // calculate aspect ratio
    if (vb.has_viewbox && vb.width > 0 && vb.height > 0) {
        size.aspect_ratio = vb.width / vb.height;
    } else if (size.height > 0) {
        size.aspect_ratio = size.width / size.height;
    }

    return size;
}

// ============================================================================
// SVG Defs: Gradient definitions and element refs
// ============================================================================

#define SVG_MAX_GRAD_DEFS  4096
#define SVG_MAX_GRAD_STOPS 64
#define SVG_MAX_ELEM_DEFS  4096

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

static SvgDefTable* ensure_svg_def_table(SvgInlineRenderContext* ctx) {
    if (!ctx->defs) {
        SvgDefTable* table = (SvgDefTable*)mem_alloc(sizeof(SvgDefTable), MEM_CAT_RENDER);
        memset(table, 0, sizeof(SvgDefTable));
        ctx->defs = (HashMap*)table;
    }
    return (SvgDefTable*)ctx->defs;
}

static void register_svg_def_element(SvgInlineRenderContext* ctx, Element* elem) {
    if (!ctx || !elem) return;
    const char* tag = get_element_tag_name(elem);
    if (!tag) return;
    const char* id = get_svg_attr(elem, "id");
    SvgDefTable* table = ensure_svg_def_table(ctx);

    if (strcmp(tag, "linearGradient") == 0 || strcmp(tag, "radialGradient") == 0) {
        if (!id) return;
        SvgGradDef* existing = lookup_grad_def(table, id);
        SvgGradDef* def = existing;
        if (!def) {
            if (table->grad_count >= SVG_MAX_GRAD_DEFS) return;
            def = &table->grads[table->grad_count++];
        }
        memset(def, 0, sizeof(SvgGradDef));
        str_copy(def->id, sizeof(def->id), id, strlen(id));
        def->is_radial = (strcmp(tag, "radialGradient") == 0);

        const char* gu = get_svg_attr(elem, "gradientUnits");
        def->user_space = (gu && strcmp(gu, "userSpaceOnUse") == 0);

        def->x1 = parse_svg_pct_or_num(get_svg_attr(elem, "x1"), 0.0f);
        def->y1 = parse_svg_pct_or_num(get_svg_attr(elem, "y1"), 0.0f);
        def->x2 = parse_svg_pct_or_num(get_svg_attr(elem, "x2"), 1.0f);
        def->y2 = parse_svg_pct_or_num(get_svg_attr(elem, "y2"), 0.0f);

        def->cx = parse_svg_pct_or_num(get_svg_attr(elem, "cx"), 0.5f);
        def->cy = parse_svg_pct_or_num(get_svg_attr(elem, "cy"), 0.5f);
        def->r  = parse_svg_pct_or_num(get_svg_attr(elem, "r"),  0.5f);

        for (int64_t s = 0; s < elem->length && def->stop_count < SVG_MAX_GRAD_STOPS; s++) {
            Element* stop_elem = get_child_element_at(elem, s);
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
        log_debug("[SVG] defs: %s id='%s' stops=%d", tag, id, def->stop_count);
    } else if (id) {
        SvgElemDef* existing = nullptr;
        for (int i = 0; i < table->elem_count; i++) {
            if (strcmp(table->elems[i].id, id) == 0) existing = &table->elems[i];
        }
        SvgElemDef* ed = existing;
        if (!ed) {
            if (table->elem_count >= SVG_MAX_ELEM_DEFS) return;
            ed = &table->elems[table->elem_count++];
        }
        str_copy(ed->id, sizeof(ed->id), id, strlen(id));
        ed->elem = elem;
    }
}

// ============================================================================
// Compose element transform with accumulated context transform
// ============================================================================

static RdtMatrix compose_element_transform(SvgInlineRenderContext* ctx, Element* elem) {
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

static bool parse_pdf_bounds_attr(const char* value, float* x, float* y, float* w, float* h) {
    if (!value || !x || !y || !w || !h) return false;
    char* end = nullptr;
    float vals[4];
    const char* p = value;
    for (int i = 0; i < 4; i++) {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        vals[i] = strtof(p, &end);
        if (end == p) return false;
        p = end;
    }
    if (vals[2] <= 0.0f || vals[3] <= 0.0f) return false;
    *x = vals[0];
    *y = vals[1];
    *w = vals[2];
    *h = vals[3];
    return true;
}

static void opacity_bounds_from_rect(const RdtMatrix* transform, float x, float y, float w, float h,
                                     int* out_x0, int* out_y0, int* out_w, int* out_h) {
    float xs[4] = {x, x + w, x + w, x};
    float ys[4] = {y, y, y + h, y + h};
    float min_x = transform->e11 * xs[0] + transform->e12 * ys[0] + transform->e13;
    float max_x = min_x;
    float min_y = transform->e21 * xs[0] + transform->e22 * ys[0] + transform->e23;
    float max_y = min_y;
    for (int i = 1; i < 4; i++) {
        float tx = transform->e11 * xs[i] + transform->e12 * ys[i] + transform->e13;
        float ty = transform->e21 * xs[i] + transform->e22 * ys[i] + transform->e23;
        if (tx < min_x) min_x = tx;
        if (tx > max_x) max_x = tx;
        if (ty < min_y) min_y = ty;
        if (ty > max_y) max_y = ty;
    }
    *out_x0 = (int)floorf(min_x) - 2;
    *out_y0 = (int)floorf(min_y) - 2;
    *out_w = (int)ceilf(max_x) - *out_x0 + 2;
    *out_h = (int)ceilf(max_y) - *out_y0 + 2;
}

typedef struct SvgGaussianBlurFilter {
    bool active;
    int x;
    int y;
    int width;
    int height;
    float blur_radius;
    bool tint_source;
    Color tint_color;
    uint32_t* backdrop;
} SvgGaussianBlurFilter;

static bool parse_svg_url_id(const char* value, char* out_id, size_t out_id_size) {
    if (!value || !out_id || out_id_size == 0) return false;
    if (strncmp(value, "url(#", 5) != 0) return false;
    const char* id_start = value + 5;
    const char* id_end = strchr(id_start, ')');
    if (!id_end || id_end == id_start) return false;
    size_t id_len = (size_t)(id_end - id_start);
    if (id_len >= out_id_size) return false;
    memcpy(out_id, id_start, id_len);
    out_id[id_len] = '\0';
    return true;
}

static bool resolve_svg_solid_filter_tint(SvgInlineRenderContext* ctx, Element* elem,
                                          Color* out_color) {
    if (!ctx || !elem || !out_color) return false;
    char fill_buf[256];
    const char* fill = get_svg_attr_or_style(ctx, elem, "fill",
                                             fill_buf, sizeof(fill_buf));
    if (fill) {
        if (strcmp(fill, "none") == 0 || strncmp(fill, "url(#", 5) == 0) {
            return false;
        }
        *out_color = svg_resolve_color_keyword(ctx, fill);
        return true;
    }
    if (!ctx->fill_none) {
        *out_color = ctx->fill_color;
        return true;
    }
    return false;
}

static bool parse_svg_std_deviation(const char* value, float* out_x, float* out_y) {
    if (!value || !out_x || !out_y) return false;
    char* end = nullptr;
    float x = strtof(value, &end);
    if (end == value || x <= 0.0f) return false;
    while (*end && (isspace((unsigned char)*end) || *end == ',')) end++;
    float y = x;
    if (*end) {
        char* end_y = nullptr;
        float parsed_y = strtof(end, &end_y);
        if (end_y != end && parsed_y > 0.0f) y = parsed_y;
    }
    *out_x = x;
    *out_y = y;
    return true;
}

static bool resolve_svg_gaussian_blur_filter(SvgInlineRenderContext* ctx, Element* elem,
                                             float bx, float by, float bw, float bh,
                                             const RdtMatrix* transform,
                                             SvgGaussianBlurFilter* out_filter) {
    if (!ctx || !elem || !transform || !out_filter || !ctx->defs) return false;
    memset(out_filter, 0, sizeof(SvgGaussianBlurFilter));

    char filter_buf[256];
    const char* filter_attr = get_svg_attr_or_style(ctx, elem, "filter",
                                                    filter_buf, sizeof(filter_buf));
    char filter_id[128];
    if (!parse_svg_url_id(filter_attr, filter_id, sizeof(filter_id))) {
        return false;
    }

    Element* filter_elem = lookup_elem_def((SvgDefTable*)ctx->defs, filter_id);
    if (!filter_elem) {
        log_debug("[SVG-FILTER] filter ref '%s' not found", filter_id);
        return false;
    }

    float std_x = 0.0f;
    float std_y = 0.0f;
    bool found_blur = false;
    for (int64_t i = 0; i < filter_elem->length; i++) {
        Element* child = get_child_element_at(filter_elem, i);
        if (!child) continue;
        const char* tag = get_element_tag_name(child);
        if (!tag || strcmp(tag, "feGaussianBlur") != 0) continue;
        if (parse_svg_std_deviation(get_svg_attr(child, "stdDeviation"), &std_x, &std_y)) {
            found_blur = true;
            break;
        }
    }
    if (!found_blur) {
        return false;
    }

    if (bw <= 0.0f || bh <= 0.0f) {
        bx = 0.0f;
        by = 0.0f;
        bw = ctx->current_viewport_w;
        bh = ctx->current_viewport_h;
    }

    const char* units = get_svg_attr(filter_elem, "filterUnits");
    bool user_space = units && strcmp(units, "userSpaceOnUse") == 0;
    float fx, fy, fw, fh;
    if (user_space) {
        fx = parse_svg_length(get_svg_attr(filter_elem, "x"), bx - bw * 0.1f);
        fy = parse_svg_length(get_svg_attr(filter_elem, "y"), by - bh * 0.1f);
        fw = parse_svg_length(get_svg_attr(filter_elem, "width"), bw * 1.2f);
        fh = parse_svg_length(get_svg_attr(filter_elem, "height"), bh * 1.2f);
    } else {
        float ux = parse_svg_pct_or_num(get_svg_attr(filter_elem, "x"), -0.1f);
        float uy = parse_svg_pct_or_num(get_svg_attr(filter_elem, "y"), -0.1f);
        float uw = parse_svg_pct_or_num(get_svg_attr(filter_elem, "width"), 1.2f);
        float uh = parse_svg_pct_or_num(get_svg_attr(filter_elem, "height"), 1.2f);
        fx = bx + ux * bw;
        fy = by + uy * bh;
        fw = uw * bw;
        fh = uh * bh;
    }
    if (fw <= 0.0f || fh <= 0.0f) return false;

    int px = 0, py = 0, pw = 0, ph = 0;
    opacity_bounds_from_rect(transform, fx, fy, fw, fh, &px, &py, &pw, &ph);
    if (pw <= 0 || ph <= 0) return false;

    float scale_x = sqrtf(transform->e11 * transform->e11 + transform->e21 * transform->e21);
    float scale_y = sqrtf(transform->e12 * transform->e12 + transform->e22 * transform->e22);
    if (scale_x <= 0.0f) scale_x = 1.0f;
    if (scale_y <= 0.0f) scale_y = scale_x;
    float sigma_px_x = std_x * scale_x;
    float sigma_px_y = std_y * scale_y;
    float sigma_px = sigma_px_x > sigma_px_y ? sigma_px_x : sigma_px_y;

    out_filter->active = true;
    out_filter->x = px;
    out_filter->y = py;
    out_filter->width = pw;
    out_filter->height = ph;
    // box_blur_region's argument follows the box-shadow convention where
    // sigma is half the blur radius; SVG stdDeviation is already sigma.
    out_filter->blur_radius = sigma_px * 2.0f;
    log_debug("[SVG-FILTER] resolved feGaussianBlur id='%s' std=(%.2f,%.2f) region=(%d,%d,%d,%d)",
              filter_id, std_x, std_y, px, py, pw, ph);
    return true;
}

static bool svg_begin_gaussian_blur_filter(SvgInlineRenderContext* ctx, SvgGaussianBlurFilter* filter) {
    if (!ctx || !filter || !filter->active) return false;
    svg_save_backdrop(ctx, filter->x, filter->y, filter->width, filter->height);
    return true;
}

static void svg_finish_gaussian_blur_filter(SvgInlineRenderContext* ctx, SvgGaussianBlurFilter* filter) {
    if (!ctx || !filter || !filter->active) return;
    svg_box_blur_region(ctx, filter->x, filter->y, filter->width, filter->height,
                        filter->blur_radius, 0, nullptr, 0, nullptr, true,
                        filter->tint_source, filter->tint_color);
    svg_composite_opacity(ctx, filter->x, filter->y,
                          filter->width, filter->height, 1.0f, true);
    filter->active = false;
}

// ============================================================================
// Apply gradient fill via the SVG painter gateway
// ============================================================================

static void draw_gradient_fill(SvgInlineRenderContext* ctx, RdtPath* path, SvgGradDef* def,
                               float bx, float by, float bw, float bh,
                               const RdtMatrix* transform, RdtFillRule fill_rule,
                               float opacity) {
    if (!path || !def || def->stop_count < 2) return;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;

    RdtGradientStop stops[SVG_MAX_GRAD_STOPS];
    for (int i = 0; i < def->stop_count; i++) {
        stops[i].offset = def->stops[i].offset;
        stops[i].r = def->stops[i].color.r;
        stops[i].g = def->stops[i].color.g;
        stops[i].b = def->stops[i].color.b;
        stops[i].a = (uint8_t)((float)def->stops[i].color.a * opacity);
    }

    if (def->is_radial) {
        float cx, cy, r;
        RdtMatrix gradient_transform = rdt_matrix_identity();
        const RdtMatrix* gradient_transform_ptr = nullptr;
        if (def->user_space) {
            cx = def->cx; cy = def->cy; r = def->r;
        } else {
            cx = def->cx; cy = def->cy; r = def->r;
            gradient_transform = {bw, 0.0f, bx, 0.0f, bh, by, 0.0f, 0.0f, 1.0f};
            gradient_transform_ptr = &gradient_transform;
        }
        svg_fill_radial_gradient(ctx, path, cx, cy, r,
                                 stops, def->stop_count, fill_rule, transform,
                                 gradient_transform_ptr);
    } else {
        float x1, y1, x2, y2;
        RdtMatrix gradient_transform = rdt_matrix_identity();
        const RdtMatrix* gradient_transform_ptr = nullptr;
        if (def->user_space) {
            x1 = def->x1; y1 = def->y1; x2 = def->x2; y2 = def->y2;
        } else {
            x1 = def->x1; y1 = def->y1; x2 = def->x2; y2 = def->y2;
            gradient_transform = {bw, 0.0f, bx, 0.0f, bh, by, 0.0f, 0.0f, 1.0f};
            gradient_transform_ptr = &gradient_transform;
        }
        svg_fill_linear_gradient(ctx, path, x1, y1, x2, y2,
                                 stops, def->stop_count, fill_rule, transform,
                                 gradient_transform_ptr);
    }
}

static bool draw_pattern_fill(SvgInlineRenderContext* ctx, RdtPath* path, Element* pattern_elem,
                              float bx, float by, float bw, float bh,
                              const RdtMatrix* transform) {
    if (!ctx || !path || !pattern_elem) return false;
    const char* tag = get_element_tag_name(pattern_elem);
    if (!tag || strcmp(tag, "pattern") != 0) return false;
    if (pattern_elem->length <= 0) return false;

    float x = parse_svg_length(get_svg_attr(pattern_elem, "x"), 0.0f);
    float y = parse_svg_length(get_svg_attr(pattern_elem, "y"), 0.0f);
    float w = parse_svg_length(get_svg_attr(pattern_elem, "width"), 0.0f);
    float h = parse_svg_length(get_svg_attr(pattern_elem, "height"), 0.0f);
    if (w <= 0.0f || h <= 0.0f) return false;
    if (bw <= 0.0f || bh <= 0.0f) {
        bx = ctx->viewbox_x;
        by = ctx->viewbox_y;
        bw = ctx->viewbox_width;
        bh = ctx->viewbox_height;
    }
    if (bw <= 0.0f || bh <= 0.0f) return false;

    RdtMatrix saved_transform = ctx->transform;
    RdtMatrix pattern_matrix = rdt_matrix_identity();
    const char* pt = get_svg_attr(pattern_elem, "patternTransform");
    if (pt && *pt) {
        float pm[6];
        if (parse_svg_transform(pt, pm)) {
            pattern_matrix.e11 = pm[0]; pattern_matrix.e12 = pm[2]; pattern_matrix.e13 = pm[4];
            pattern_matrix.e21 = pm[1]; pattern_matrix.e22 = pm[3]; pattern_matrix.e23 = pm[5];
            pattern_matrix.e31 = 0.0f;  pattern_matrix.e32 = 0.0f;  pattern_matrix.e33 = 1.0f;
        }
    }

    svg_push_clip(ctx, path, transform);

    float pattern_tx = pattern_matrix.e13;
    float pattern_ty = pattern_matrix.e23;
    float start_x = x + pattern_tx;
    while (start_x + w > bx) start_x -= w;
    while (start_x + w <= bx) start_x += w;
    start_x -= pattern_tx;
    float start_y = y + pattern_ty;
    while (start_y + h > by) start_y -= h;
    while (start_y + h <= by) start_y += h;
    start_y -= pattern_ty;

    float tile_y = start_y;
    while (tile_y < by + bh + h) {
        float tile_x = start_x;
        while (tile_x < bx + bw + w) {
            RdtMatrix tile_translate = rdt_matrix_translate(tile_x, tile_y);
            RdtMatrix pattern_local = rdt_matrix_multiply(&tile_translate, &pattern_matrix);
            ctx->transform = rdt_matrix_multiply(&saved_transform, &pattern_local);
            render_svg_children(ctx, pattern_elem);
            tile_x += w;
        }
        tile_y += h;
    }

    ctx->transform = saved_transform;
    svg_pop_clip(ctx);
    return true;
}

// ============================================================================
// Draw fill and stroke for an SVG shape via the SVG painter gateway
// ============================================================================

static void draw_svg_fill_stroke(SvgInlineRenderContext* ctx, RdtPath* path, Element* elem,
                                  const RdtMatrix* transform,
                                  float bx, float by, float bw, float bh) {
    if (!path || !elem) return;

    SvgGaussianBlurFilter filter = {};
    bool use_filter = resolve_svg_gaussian_blur_filter(ctx, elem, bx, by, bw, bh,
                                                       transform, &filter);
    if (use_filter) {
        filter.tint_source = resolve_svg_solid_filter_tint(ctx, elem, &filter.tint_color);
        use_filter = svg_begin_gaussian_blur_filter(ctx, &filter);
    }

    // --- FILL ---
    char fill_buf[256];
    char fill_rule_buf[64];
    char fill_opacity_buf[64];
    char opacity_buf[64];
    char stroke_buf[256];
    char stroke_width_buf[64];
    char stroke_opacity_buf[64];
    char linecap_buf[64];
    char linejoin_buf[64];
    const char* fill = get_svg_attr_or_style(ctx, elem, "fill", fill_buf, sizeof(fill_buf));
    const char* fill_opacity_attr = get_svg_attr_or_style(ctx, elem, "fill-opacity", fill_opacity_buf, sizeof(fill_opacity_buf));
    const char* opacity_attr = get_svg_attr_or_style(ctx, elem, "opacity", opacity_buf, sizeof(opacity_buf));
    float fill_opacity = fill_opacity_attr ? strtof(fill_opacity_attr, nullptr) : 1.0f;
    float element_opacity = opacity_attr ? strtof(opacity_attr, nullptr) : 1.0f;
    float fill_alpha = fill_opacity * element_opacity * ctx->opacity;
    Color fc;
    bool has_fill = true;
    bool gradient_applied = false;
    bool pattern_applied = false;
    const char* fill_rule_attr = get_svg_attr_or_style(ctx, elem, "fill-rule", fill_rule_buf, sizeof(fill_rule_buf));
    RdtFillRule fill_rule = (fill_rule_attr && strcmp(fill_rule_attr, "evenodd") == 0)
        ? RDT_FILL_EVEN_ODD : RDT_FILL_WINDING;

    if (fill) {
        if (strcmp(fill, "none") == 0) {
            has_fill = false;
        } else if (strncmp(fill, "url(#", 5) == 0) {
            // gradient or pattern paint server reference
            if (ctx->defs) {
                const char* id_start = fill + 5;
                const char* id_end   = strchr(id_start, ')');
                char id_buf[128]     = {};
                size_t id_len = id_end ? (size_t)(id_end - id_start) : strlen(id_start);
                if (id_len < sizeof(id_buf)) {
                    memcpy(id_buf, id_start, id_len);
                    SvgGradDef* def = lookup_grad_def((SvgDefTable*)ctx->defs, id_buf);
                    if (def && def->stop_count >= 2) {
                        draw_gradient_fill(ctx, path, def, bx, by, bw, bh, transform, fill_rule, fill_alpha);
                        gradient_applied = true;
                        has_fill = false;
                    } else {
                        Element* pattern_elem = lookup_elem_def((SvgDefTable*)ctx->defs, id_buf);
                        if (draw_pattern_fill(ctx, path, pattern_elem, bx, by, bw, bh, transform)) {
                            pattern_applied = true;
                            has_fill = false;
                        }
                    }
                }
            }
            if (!gradient_applied && !pattern_applied) {
                // unresolved url() reference - per SVG spec, this should NOT
                // fall back to a default solid color (black); skip the fill.
                log_debug("[SVG] paint server fill not resolved: %s (skip fill)", fill);
                has_fill = false;
            }
        } else {
            fc = svg_resolve_color_keyword(ctx, fill);
        }
    } else if (!ctx->fill_none) {
        fc = ctx->fill_color;
    } else {
        has_fill = false;
    }

    if (has_fill) {
        fc.a = (uint8_t)((float)fc.a * fill_alpha);
        svg_fill_path(ctx, path, fc, fill_rule, transform);
    }

    // --- STROKE ---
    const char* stroke = get_svg_attr_or_style(ctx, elem, "stroke", stroke_buf, sizeof(stroke_buf));
    bool has_stroke = false;
    Color sc;

    if (stroke) {
        if (strcmp(stroke, "none") != 0) {
            has_stroke = true;
            sc = svg_resolve_color_keyword(ctx, stroke);
        }
    } else if (!ctx->stroke_none) {
        has_stroke = true;
        sc = ctx->stroke_color;
    }

    if (has_stroke) {
        const char* stroke_width_str = get_svg_attr_or_style(ctx, elem, "stroke-width", stroke_width_buf, sizeof(stroke_width_buf));
        float stroke_width = stroke_width_str ? parse_svg_length(stroke_width_str, 1.0f) : ctx->stroke_width;

        const char* stroke_opacity = get_svg_attr_or_style(ctx, elem, "stroke-opacity", stroke_opacity_buf, sizeof(stroke_opacity_buf));
        if (stroke_opacity) {
            float opacity = strtof(stroke_opacity, nullptr);
            sc.a = (uint8_t)(sc.a * opacity);
        }
        if (opacity_attr) {
            sc.a = (uint8_t)((float)sc.a * element_opacity);
        }
        // apply inherited group opacity
        if (ctx->opacity < 1.0f) {
            sc.a = (uint8_t)(sc.a * ctx->opacity);
        }

        // linecap
        RdtStrokeCap cap = RDT_CAP_BUTT;
        const char* linecap = get_svg_attr_or_style(ctx, elem, "stroke-linecap", linecap_buf, sizeof(linecap_buf));
        if (linecap) {
            if (strcmp(linecap, "round") == 0)       cap = RDT_CAP_ROUND;
            else if (strcmp(linecap, "square") == 0)  cap = RDT_CAP_SQUARE;
        }

        // linejoin
        RdtStrokeJoin join = RDT_JOIN_MITER;
        const char* linejoin = get_svg_attr_or_style(ctx, elem, "stroke-linejoin", linejoin_buf, sizeof(linejoin_buf));
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
                while (*p && (str_char_is_ascii_space(*p) || *p == ',')) p++;
                if (!*p) break;
                dashes[dash_count++] = strtof(p, (char**)&p);
            }
        }

        svg_stroke_path(ctx, path, sc, stroke_width, cap, join,
                        dash_count > 0 ? dashes : nullptr, dash_count, transform);
    }

    if (use_filter) {
        svg_finish_gaussian_blur_filter(ctx, &filter);
    }
}

// ============================================================================
// SVG Shape Renderers
// ============================================================================

static void render_svg_rect(SvgInlineRenderContext* ctx, Element* elem) {
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

static void render_svg_circle(SvgInlineRenderContext* ctx, Element* elem) {
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

static void render_svg_ellipse(SvgInlineRenderContext* ctx, Element* elem) {
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

static void render_svg_line(SvgInlineRenderContext* ctx, Element* elem) {
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
        while (*p && (str_char_is_ascii_space(*p) || *p == ',')) p++;
        if (!*p) break;

        char* end;
        x = strtof(p, &end);
        if (end == p) break;
        p = end;

        while (*p && (str_char_is_ascii_space(*p) || *p == ',')) p++;
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

static void render_svg_polyline(SvgInlineRenderContext* ctx, Element* elem, bool close_path) {
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
    while (**p && (str_char_is_ascii_space(**p) || **p == ',')) (*p)++;
}

static bool peek_number(const char* p) {
    skip_wsp_comma(&p);
    return *p == '-' || *p == '+' || *p == '.' || str_char_is_digit(*p);
}

static float parse_number(const char** p) {
    skip_wsp_comma(p);
    char* end;
    float val = strtof(*p, &end);
    if (end == *p) {
        log_error("[SVG] path parse: expected number near '%.16s'", *p);
        if (**p) (*p)++;
        return 0.0f;
    }
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

static inline bool svg_same_point(float x1, float y1, float x2, float y2) {
    return fabsf(x1 - x2) < 0.0001f && fabsf(y1 - y2) < 0.0001f;
}

typedef struct SvgSimpleRectPath {
    float x;
    float y;
    float width;
    float height;
} SvgSimpleRectPath;

static bool svg_parse_simple_rect_path(const char* d, SvgSimpleRectPath* rect) {
    if (!d || !rect) return false;
    const char* p = d;
    float xs[4] = {};
    float ys[4] = {};

    skip_wsp_comma(&p);
    if (*p != 'M') return false;
    p++;
    xs[0] = parse_number(&p);
    ys[0] = parse_number(&p);

    for (size_t i = 1; i < 4; i++) {
        skip_wsp_comma(&p);
        if (*p != 'L') return false;
        p++;
        xs[i] = parse_number(&p);
        ys[i] = parse_number(&p);
    }

    skip_wsp_comma(&p);
    if (*p != 'Z') return false;
    p++;
    skip_wsp_comma(&p);
    if (*p) return false;

    float min_x = xs[0], max_x = xs[0];
    float min_y = ys[0], max_y = ys[0];
    for (size_t i = 1; i < 4; i++) {
        if (xs[i] < min_x) min_x = xs[i];
        if (xs[i] > max_x) max_x = xs[i];
        if (ys[i] < min_y) min_y = ys[i];
        if (ys[i] > max_y) max_y = ys[i];
    }
    if (max_x <= min_x || max_y <= min_y) return false;

    for (size_t i = 0; i < 4; i++) {
        bool x_ok = fabsf(xs[i] - min_x) < 0.0001f || fabsf(xs[i] - max_x) < 0.0001f;
        bool y_ok = fabsf(ys[i] - min_y) < 0.0001f || fabsf(ys[i] - max_y) < 0.0001f;
        if (!x_ok || !y_ok) return false;
    }

    rect->x = min_x;
    rect->y = min_y;
    rect->width = max_x - min_x;
    rect->height = max_y - min_y;
    return true;
}

static bool svg_path_is_fill_only(SvgInlineRenderContext* ctx, Element* elem) {
    char fill_buf[256];
    char stroke_buf[256];
    const char* fill = get_svg_attr_or_style(ctx, elem, "fill", fill_buf, sizeof(fill_buf));
    if (fill && strcmp(fill, "none") == 0) return false;
    if (!fill && ctx->fill_none) return false;

    const char* stroke = get_svg_attr_or_style(ctx, elem, "stroke", stroke_buf, sizeof(stroke_buf));
    if (stroke) return strcmp(stroke, "none") == 0;
    return ctx->stroke_none;
}

static RdtPath* svg_make_stable_hairline_rect_path(const SvgSimpleRectPath* rect,
                                                   const RdtMatrix* transform) {
    if (!rect || !transform) return nullptr;
    float x_scale = sqrtf(transform->e11 * transform->e11 + transform->e21 * transform->e21);
    float y_scale = sqrtf(transform->e12 * transform->e12 + transform->e22 * transform->e22);
    if (x_scale <= 0.0f || y_scale <= 0.0f) return nullptr;

    float x = rect->x;
    float y = rect->y;
    float width = rect->width;
    float height = rect->height;
    float device_width = width * x_scale;
    float device_height = height * y_scale;
    bool adjusted = false;

    if (device_height > 0.0f && device_height < 1.0f && device_width >= 4.0f) {
        float stable_height = 1.0f / y_scale;
        y -= (stable_height - height) * 0.5f;
        height = stable_height;
        adjusted = true;
    }
    if (device_width > 0.0f && device_width < 1.0f && device_height >= 4.0f) {
        float stable_width = 1.0f / x_scale;
        x -= (stable_width - width) * 0.5f;
        width = stable_width;
        adjusted = true;
    }
    if (!adjusted) return nullptr;

    RdtPath* stable_path = rdt_path_new();
    rdt_path_add_rect(stable_path, x, y, width, height, 0.0f, 0.0f);
    return stable_path;
}

static inline void svg_emit_pending_move(RdtPath* path, bool* pending_move,
                                         float pending_x, float pending_y) {
    if (*pending_move) {
        rdt_path_move_to(path, pending_x, pending_y);
        *pending_move = false;
    }
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

typedef struct SvgPathEndInfo {
    float x;
    float y;
    float tangent_x;
    float tangent_y;
    bool has_tangent;
} SvgPathEndInfo;

// Parse SVG path 'd' attribute into an RdtPath. Returns new path (caller must free).
static RdtPath* parse_svg_path_d(const char* d, SvgPathEndInfo* end_info = nullptr,
                                 bool allow_move_only = false) {
    if (!d || !*d) return nullptr;
    if (end_info) memset(end_info, 0, sizeof(SvgPathEndInfo));

    RdtPath* path = rdt_path_new();

    float cur_x = 0, cur_y = 0;
    float start_x = 0, start_y = 0;
    float pending_x = 0, pending_y = 0;
    float last_ctrl_x = 0, last_ctrl_y = 0;
    char last_cmd = 0;
    bool pending_move = false;
    bool subpath_has_draw = false;
    bool any_draw = false;

    const char* p = d;

    while (*p) {
        skip_wsp_comma(&p);
        if (!*p) break;

        char cmd = *p;
        bool is_cmd = isalpha((unsigned char)cmd);

        if (is_cmd) {
            p++;
            last_cmd = cmd;
        } else {
            if (!last_cmd || !peek_number(p)) {
                log_error("[SVG] path parse: invalid token near '%.16s'", p);
                rdt_path_free(path);
                return nullptr;
            }
            cmd = last_cmd;
            if (cmd == 'M') cmd = 'L';
            if (cmd == 'm') cmd = 'l';
        }

        bool relative = islower((unsigned char)cmd);
        cmd = (char)toupper((unsigned char)cmd);

        switch (cmd) {
            case 'M': {  // moveto
                float x = parse_number(&p);
                float y = parse_number(&p);
                if (relative) { x += cur_x; y += cur_y; }
                cur_x = start_x = x;
                cur_y = start_y = y;
                pending_x = x;
                pending_y = y;
                pending_move = true;
                subpath_has_draw = false;
                last_ctrl_x = cur_x;
                last_ctrl_y = cur_y;
                // subsequent coords are implicit lineto
                while (peek_number(p)) {
                    float prev_x = cur_x, prev_y = cur_y;
                    x = parse_number(&p);
                    y = parse_number(&p);
                    if (relative) { x += cur_x; y += cur_y; }
                    if (!svg_same_point(cur_x, cur_y, x, y)) {
                        svg_emit_pending_move(path, &pending_move, pending_x, pending_y);
                        rdt_path_line_to(path, x, y);
                        subpath_has_draw = true;
                        any_draw = true;
                    }
                    cur_x = x; cur_y = y;
                    if (end_info) {
                        end_info->tangent_x = cur_x - prev_x;
                        end_info->tangent_y = cur_y - prev_y;
                        end_info->has_tangent = !svg_same_point(cur_x, cur_y, prev_x, prev_y);
                    }
                }
                break;
            }
            case 'L': {  // lineto
                while (peek_number(p)) {
                    float prev_x = cur_x, prev_y = cur_y;
                    float x = parse_number(&p);
                    float y = parse_number(&p);
                    if (relative) { x += cur_x; y += cur_y; }
                    if (!svg_same_point(cur_x, cur_y, x, y)) {
                        svg_emit_pending_move(path, &pending_move, pending_x, pending_y);
                        rdt_path_line_to(path, x, y);
                        subpath_has_draw = true;
                        any_draw = true;
                    }
                    cur_x = x; cur_y = y;
                    if (end_info) {
                        end_info->tangent_x = cur_x - prev_x;
                        end_info->tangent_y = cur_y - prev_y;
                        end_info->has_tangent = !svg_same_point(cur_x, cur_y, prev_x, prev_y);
                    }
                }
                last_ctrl_x = cur_x;
                last_ctrl_y = cur_y;
                break;
            }
            case 'H': {  // horizontal lineto
                while (peek_number(p)) {
                    float previous_x = cur_x;
                    float x = parse_number(&p);
                    if (relative) { x += cur_x; }
                    if (!svg_same_point(cur_x, cur_y, x, cur_y)) {
                        svg_emit_pending_move(path, &pending_move, pending_x, pending_y);
                        rdt_path_line_to(path, x, cur_y);
                        subpath_has_draw = true;
                        any_draw = true;
                    }
                    cur_x = x;
                    if (end_info) {
                        end_info->tangent_x = cur_x - previous_x;
                        end_info->tangent_y = 0.0f;
                        end_info->has_tangent = fabsf(end_info->tangent_x) > 0.0001f;
                    }
                }
                last_ctrl_x = cur_x;
                last_ctrl_y = cur_y;
                break;
            }
            case 'V': {  // vertical lineto
                while (peek_number(p)) {
                    float previous_y = cur_y;
                    float y = parse_number(&p);
                    if (relative) { y += cur_y; }
                    if (!svg_same_point(cur_x, cur_y, cur_x, y)) {
                        svg_emit_pending_move(path, &pending_move, pending_x, pending_y);
                        rdt_path_line_to(path, cur_x, y);
                        subpath_has_draw = true;
                        any_draw = true;
                    }
                    cur_y = y;
                    if (end_info) {
                        end_info->tangent_x = 0.0f;
                        end_info->tangent_y = cur_y - previous_y;
                        end_info->has_tangent = fabsf(end_info->tangent_y) > 0.0001f;
                    }
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
                    bool degenerate = svg_same_point(cur_x, cur_y, x1, y1)
                        && svg_same_point(cur_x, cur_y, x2, y2)
                        && svg_same_point(cur_x, cur_y, x, y);
                    if (!degenerate) {
                        svg_emit_pending_move(path, &pending_move, pending_x, pending_y);
                        rdt_path_cubic_to(path, x1, y1, x2, y2, x, y);
                        subpath_has_draw = true;
                        any_draw = true;
                    }
                    last_ctrl_x = x2; last_ctrl_y = y2;
                    cur_x = x; cur_y = y;
                    if (end_info) {
                        end_info->tangent_x = cur_x - x2;
                        end_info->tangent_y = cur_y - y2;
                        end_info->has_tangent = !svg_same_point(cur_x, cur_y, x2, y2);
                    }
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
                    bool degenerate = svg_same_point(cur_x, cur_y, x1, y1)
                        && svg_same_point(cur_x, cur_y, x2, y2)
                        && svg_same_point(cur_x, cur_y, x, y);
                    if (!degenerate) {
                        svg_emit_pending_move(path, &pending_move, pending_x, pending_y);
                        rdt_path_cubic_to(path, x1, y1, x2, y2, x, y);
                        subpath_has_draw = true;
                        any_draw = true;
                    }
                    last_ctrl_x = x2; last_ctrl_y = y2;
                    cur_x = x; cur_y = y;
                    if (end_info) {
                        end_info->tangent_x = cur_x - x2;
                        end_info->tangent_y = cur_y - y2;
                        end_info->has_tangent = !svg_same_point(cur_x, cur_y, x2, y2);
                    }
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
                    bool degenerate = svg_same_point(cur_x, cur_y, cx1, cy1)
                        && svg_same_point(cur_x, cur_y, cx2, cy2)
                        && svg_same_point(cur_x, cur_y, x, y);
                    if (!degenerate) {
                        svg_emit_pending_move(path, &pending_move, pending_x, pending_y);
                        rdt_path_cubic_to(path, cx1, cy1, cx2, cy2, x, y);
                        subpath_has_draw = true;
                        any_draw = true;
                    }
                    last_ctrl_x = qx; last_ctrl_y = qy;
                    cur_x = x; cur_y = y;
                    if (end_info) {
                        end_info->tangent_x = cur_x - qx;
                        end_info->tangent_y = cur_y - qy;
                        end_info->has_tangent = !svg_same_point(cur_x, cur_y, qx, qy);
                    }
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
                    bool degenerate = svg_same_point(cur_x, cur_y, cx1, cy1)
                        && svg_same_point(cur_x, cur_y, cx2, cy2)
                        && svg_same_point(cur_x, cur_y, x, y);
                    if (!degenerate) {
                        svg_emit_pending_move(path, &pending_move, pending_x, pending_y);
                        rdt_path_cubic_to(path, cx1, cy1, cx2, cy2, x, y);
                        subpath_has_draw = true;
                        any_draw = true;
                    }
                    last_ctrl_x = qx; last_ctrl_y = qy;
                    cur_x = x; cur_y = y;
                    if (end_info) {
                        end_info->tangent_x = cur_x - qx;
                        end_info->tangent_y = cur_y - qy;
                        end_info->has_tangent = !svg_same_point(cur_x, cur_y, qx, qy);
                    }
                }
                break;
            }
            case 'A': {  // arc
                while (peek_number(p)) {
                    float previous_x = cur_x, previous_y = cur_y;
                    float rx = parse_number(&p);
                    float ry = parse_number(&p);
                    float rotation = parse_number(&p);
                    int large_arc = parse_flag(&p);
                    int sweep = parse_flag(&p);
                    float x = parse_number(&p);
                    float y = parse_number(&p);
                    if (relative) { x += cur_x; y += cur_y; }

                    if (!svg_same_point(cur_x, cur_y, x, y)) {
                        svg_emit_pending_move(path, &pending_move, pending_x, pending_y);
                        arc_to_beziers(path, cur_x, cur_y, rx, ry, rotation, large_arc, sweep, x, y);
                        subpath_has_draw = true;
                        any_draw = true;
                    }
                    cur_x = x; cur_y = y;
                    if (end_info) {
                        end_info->tangent_x = cur_x - previous_x;
                        end_info->tangent_y = cur_y - previous_y;
                        end_info->has_tangent = !svg_same_point(cur_x, cur_y, previous_x, previous_y);
                    }
                }
                last_ctrl_x = cur_x;
                last_ctrl_y = cur_y;
                break;
            }
            case 'Z': {  // closepath
                float previous_x = cur_x, previous_y = cur_y;
                if (subpath_has_draw) {
                    svg_emit_pending_move(path, &pending_move, pending_x, pending_y);
                    rdt_path_close(path);
                }
                cur_x = start_x;
                cur_y = start_y;
                last_ctrl_x = cur_x;
                last_ctrl_y = cur_y;
                if (end_info) {
                    end_info->tangent_x = cur_x - previous_x;
                    end_info->tangent_y = cur_y - previous_y;
                    end_info->has_tangent = !svg_same_point(cur_x, cur_y, previous_x, previous_y);
                }
                break;
            }
            default:
                log_error("[SVG] path parse: unsupported command '%c'", cmd);
                rdt_path_free(path);
                return nullptr;
        }
    }

    if (end_info) {
        end_info->x = cur_x;
        end_info->y = cur_y;
    }
    if (!any_draw && !allow_move_only) {
        rdt_path_free(path);
        return nullptr;
    }

    return path;
}

static bool svg_parse_path_end_info(const char* d, SvgPathEndInfo* info) {
    if (!d || !*d || !info) return false;
    RdtPath* parsed = parse_svg_path_d(d, info, true);
    if (!parsed) return false;
    rdt_path_free(parsed);
    return true;
}

static RdtMatrix svg_matrix_scale(float sx, float sy) {
    RdtMatrix m = { sx, 0, 0,  0, sy, 0,  0, 0, 1 };
    return m;
}

static RdtMatrix svg_matrix_rotate(float radians) {
    float c = cosf(radians);
    float s = sinf(radians);
    RdtMatrix m = { c, -s, 0,  s, c, 0,  0, 0, 1 };
    return m;
}

static void render_svg_path_marker_end(SvgInlineRenderContext* ctx, Element* elem,
                                       const RdtMatrix* path_transform,
                                       float stroke_width) {
    if (!ctx || !elem || !path_transform || !ctx->defs) return;

    char marker_buf[256];
    const char* marker_ref = get_svg_attr_or_style(ctx, elem, "marker-end",
                                                   marker_buf, sizeof(marker_buf));
    if (!marker_ref) return;

    char id_buf[128];
    if (!parse_svg_url_id(marker_ref, id_buf, sizeof(id_buf))) return;
    Element* marker_elem = lookup_elem_def((SvgDefTable*)ctx->defs, id_buf);
    if (!marker_elem) return;

    const char* d = get_svg_attr(elem, "d");
    SvgPathEndInfo end = {};
    if (!svg_parse_path_end_info(d, &end) || !end.has_tangent) return;

    float marker_scale = stroke_width;
    const char* units = get_svg_attr(marker_elem, "markerUnits");
    if (units && strcmp(units, "userSpaceOnUse") == 0) marker_scale = 1.0f;

    float ref_x = parse_svg_length(get_svg_attr(marker_elem, "refX"), 0.0f);
    float ref_y = parse_svg_length(get_svg_attr(marker_elem, "refY"), 0.0f);
    float angle = atan2f(end.tangent_y, end.tangent_x);
    const char* orient = get_svg_attr(marker_elem, "orient");
    if (orient && strcmp(orient, "auto") != 0 && strcmp(orient, "auto-start-reverse") != 0) {
        angle = parse_svg_length(orient, 0.0f) * (float)M_PI / 180.0f;
    }

    RdtMatrix translate = rdt_matrix_translate(end.x, end.y);
    RdtMatrix rotate = svg_matrix_rotate(angle);
    RdtMatrix scale = svg_matrix_scale(marker_scale, marker_scale);
    RdtMatrix ref = rdt_matrix_translate(-ref_x, -ref_y);
    RdtMatrix local = rdt_matrix_multiply(&translate, &rotate);
    local = rdt_matrix_multiply(&local, &scale);
    local = rdt_matrix_multiply(&local, &ref);
    RdtMatrix marker_transform = rdt_matrix_multiply(path_transform, &local);

    RdtMatrix saved_transform = ctx->transform;
    float saved_vw = ctx->current_viewport_w;
    float saved_vh = ctx->current_viewport_h;
    ctx->transform = marker_transform;
    ctx->current_viewport_w = parse_svg_length(get_svg_attr(marker_elem, "markerWidth"), 3.0f);
    ctx->current_viewport_h = parse_svg_length(get_svg_attr(marker_elem, "markerHeight"), 3.0f);

    for (int64_t i = 0; i < marker_elem->length; i++) {
        Element* child = get_child_element_at(marker_elem, i);
        if (child) render_svg_element(ctx, child);
    }

    ctx->current_viewport_w = saved_vw;
    ctx->current_viewport_h = saved_vh;
    ctx->transform = saved_transform;
}

static void render_svg_path(SvgInlineRenderContext* ctx, Element* elem) {
    const char* d = get_svg_attr(elem, "d");
    RdtPath* path = parse_svg_path_d(d);
    if (!path) return;

    RdtMatrix m = compose_element_transform(ctx, elem);
    RdtPath* draw_path = path;
    SvgSimpleRectPath rect = {};
    RdtPath* stable_path = nullptr;
    if (svg_path_is_fill_only(ctx, elem) && svg_parse_simple_rect_path(d, &rect)) {
        stable_path = svg_make_stable_hairline_rect_path(&rect, &m);
        if (stable_path) draw_path = stable_path;
    }

    draw_svg_fill_stroke(ctx, draw_path, elem, &m, 0, 0, 0, 0);
    char marker_stroke_width_buf[64];
    const char* marker_stroke_width = get_svg_attr_or_style(ctx, elem, "stroke-width",
                                                            marker_stroke_width_buf,
                                                            sizeof(marker_stroke_width_buf));
    float stroke_width = marker_stroke_width ? parse_svg_length(marker_stroke_width, 1.0f)
                                             : ctx->stroke_width;
    render_svg_path_marker_end(ctx, elem, &m, stroke_width);
    if (stable_path) rdt_path_free(stable_path);
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

// Materialize a registered @font-face entry to a usable file path for ThorVG.
// Returns mem_alloc'd path string (caller mem_free's). For data URIs, decodes
// to a temp file under ./temp/ keyed by the data URI hash so each unique font
// is materialized at most once. For regular file paths, returns a copy of the
// path. Returns nullptr if no @font-face entry matches `family` or no source
// can be materialized.
static bool font_file_has_unicode_cmap(const char* path);

static char* resolve_font_via_fontface(FontContext* font_ctx, const char* family,
                                        const char** out_font_name,
                                        int weight, FontSlant slant,
                                        bool allow_nonunicode_cmap = false) {
    if (!font_ctx || !family || !*family) return nullptr;

    FontWeight fw = (weight >= 100 && weight <= 900) ? (FontWeight)weight : FONT_WEIGHT_NORMAL;
    const FontFaceEntry* entry = font_face_find_internal(font_ctx, family, fw, slant);
    if (!entry || entry->source_count <= 0 || !entry->sources) return nullptr;

    for (int i = 0; i < entry->source_count; i++) {
        const char* src = entry->sources[i].path;
        if (!src) continue;

        if (strncmp(src, "data:", 5) != 0) {
            // local file path — return copy, skip TTC
            if (strstr(src, ".ttc")) continue;
            char* p = mem_strdup(src, MEM_CAT_RENDER);
            if (out_font_name) *out_font_name = entry->family;
            log_debug("[SVG] @font-face resolved: %s -> %s", family, src);
            return p;
        }

        // data URI — decode and write to ./temp/lambda_font_<hash>.<ext>
        const char* comma = strchr(src, ',');
        if (!comma) continue;

        // pick extension from format (if known), else from MIME type, else ttf
        const char* ext = "ttf";
        const char* fmt = entry->sources[i].format;
        if (fmt) {
            if (strcasecmp(fmt, "opentype") == 0 || strcasecmp(fmt, "otf") == 0) ext = "otf";
            else if (strcasecmp(fmt, "woff2") == 0) ext = "woff2";
            else if (strcasecmp(fmt, "woff") == 0) ext = "woff";
        } else {
            // sniff from mime: data:font/ttf;... or data:font/otf;...
            if (strncmp(src, "data:font/otf", 13) == 0 ||
                strncmp(src, "data:application/font-otf", 25) == 0) ext = "otf";
            else if (strstr(src, "woff2")) ext = "woff2";
            else if (strstr(src, "woff")) ext = "woff";
        }

        // hash the data URI suffix (after comma) for a stable filename
        // simple FNV-1a 64-bit
        uint64_t h = 1469598103934665603ULL;
        for (const char* p = comma + 1; *p; p++) {
            h ^= (uint8_t)*p;
            h *= 1099511628211ULL;
        }

        char temp_path[512];
        snprintf(temp_path, sizeof(temp_path), "./temp/lambda_font_%016llx.%s",
                 (unsigned long long)h, ext);

        // if file already exists (cached), use it directly
        FILE* fcheck = fopen(temp_path, "rb");
        if (fcheck) {
            fclose(fcheck);
            // reject if no Unicode cmap (PDF Identity / Mac Roman subsets)
            if (!allow_nonunicode_cmap && !font_file_has_unicode_cmap(temp_path)) {
                log_debug("[SVG] @font-face %s has no Unicode cmap, falling back to system font", family);
                return nullptr;
            }
            char* p = mem_strdup(temp_path, MEM_CAT_RENDER);
            if (out_font_name) *out_font_name = entry->family;
            log_debug("[SVG] @font-face cached file: %s -> %s", family, temp_path);
            return p;
        }

        // base64 decode and write
        size_t b64_len = strlen(comma + 1);
        size_t decoded_len = 0;
        uint8_t* decoded = base64_decode(comma + 1, b64_len, &decoded_len);
        if (!decoded || decoded_len == 0) {
            if (decoded) mem_free(decoded);
            log_debug("[SVG] @font-face base64 decode failed for %s", family);
            continue;
        }

        FILE* fout = fopen(temp_path, "wb");
        if (!fout) {
            mem_free(decoded);
            log_debug("[SVG] @font-face cannot open temp file %s", temp_path);
            continue;
        }
        size_t written = fwrite(decoded, 1, decoded_len, fout);
        fclose(fout);
        mem_free(decoded);
        if (written != decoded_len) {
            log_debug("[SVG] @font-face write incomplete %zu/%zu", written, decoded_len);
            continue;
        }

        log_info("[SVG] @font-face materialized: %s -> %s (%zu bytes)",
                 family, temp_path, decoded_len);
        // reject if no Unicode cmap (PDF Identity / Mac Roman subsets) — these
        // font subsets are GID-keyed via the PDF's ToUnicode CMap and can't
        // be used directly for Unicode-text SVG rendering.
        if (!allow_nonunicode_cmap && !font_file_has_unicode_cmap(temp_path)) {
            log_info("[SVG] @font-face %s has no Unicode cmap, falling back to system font", family);
            return nullptr;
        }
        char* p = mem_strdup(temp_path, MEM_CAT_RENDER);
        if (out_font_name) *out_font_name = entry->family;
        return p;
    }

    return nullptr;
}

// Quickly check whether a TTF/OTF file has a Unicode cmap subtable
// (platformID=0, or platformID=3 encodingID=1/10). PDFs embed font subsets
// with Mac Roman / Identity cmaps that are GID-keyed, not Unicode-keyed —
// ThorVG's Unicode-based glyph lookup returns nothing for those, so we must
// reject them and fall back to a system font.
static bool font_file_has_unicode_cmap(const char* path) {
    if (!path) return false;
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    uint8_t hdr[12];
    if (fread(hdr, 1, 12, f) != 12) { fclose(f); return false; }
    uint16_t numTables = (uint16_t)((hdr[4] << 8) | hdr[5]);
    if (numTables == 0 || numTables > 64) { fclose(f); return false; }

    uint32_t cmap_off = 0, cmap_len = 0;
    for (uint16_t i = 0; i < numTables; i++) {
        uint8_t rec[16];
        if (fread(rec, 1, 16, f) != 16) { fclose(f); return false; }
        if (rec[0] == 'c' && rec[1] == 'm' && rec[2] == 'a' && rec[3] == 'p') {
            cmap_off = ((uint32_t)rec[8] << 24) | ((uint32_t)rec[9] << 16) |
                       ((uint32_t)rec[10] << 8) | (uint32_t)rec[11];
            cmap_len = ((uint32_t)rec[12] << 24) | ((uint32_t)rec[13] << 16) |
                       ((uint32_t)rec[14] << 8) | (uint32_t)rec[15];
            break;
        }
    }
    if (!cmap_off || cmap_len < 4) { fclose(f); return false; }

    if (fseek(f, (long)cmap_off, SEEK_SET) != 0) { fclose(f); return false; }
    uint8_t cmap_hdr[4];
    if (fread(cmap_hdr, 1, 4, f) != 4) { fclose(f); return false; }
    uint16_t numSub = (uint16_t)((cmap_hdr[2] << 8) | cmap_hdr[3]);
    if (numSub == 0 || numSub > 64) { fclose(f); return false; }

    bool has_unicode = false;
    for (uint16_t i = 0; i < numSub; i++) {
        uint8_t rec[8];
        if (fread(rec, 1, 8, f) != 8) break;
        uint16_t pid = (uint16_t)((rec[0] << 8) | rec[1]);
        uint16_t eid = (uint16_t)((rec[2] << 8) | rec[3]);
        // platformID 0 = Unicode (any encoding)
        // platformID 3 (Microsoft) + encodingID 1 (BMP) or 10 (UCS-4) = Unicode
        if (pid == 0) { has_unicode = true; break; }
        if (pid == 3 && (eid == 1 || eid == 10)) { has_unicode = true; break; }
    }
    fclose(f);
    return has_unicode;
}

static char* resolve_svg_font_path(const char* font_family, const char** out_font_name,
                                    FontContext* font_ctx = nullptr, int weight = 400,
                                    FontSlant slant = FONT_SLANT_NORMAL,
                                    bool allow_nonunicode_fontface = false) {
    if (!font_family || !*font_family) {
        // default to a common sans-serif font
        font_family = "Arial";
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
        // first, check the @font-face registry — embedded fonts (e.g. from
        // PDFs with FontFile2/FontFile3 streams) live here and would otherwise
        // be invisible to platform/database lookups.
        if (font_ctx) {
            char* p = resolve_font_via_fontface(font_ctx, fam, out_font_name, weight, slant,
                                                allow_nonunicode_fontface);
            if (p) return p;
        }
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

static const char* resolve_svg_radiant_font_family(const char* font_family,
                                                   FontContext* font_ctx,
                                                   int weight,
                                                   FontSlant slant,
                                                   const char* fallback_family,
                                                   bool allow_embedded_font) {
    if (!font_family || !font_ctx) return fallback_family ? fallback_family : font_family;

    char family_list[512];
    strncpy(family_list, font_family, sizeof(family_list) - 1);
    family_list[sizeof(family_list) - 1] = '\0';

    FontWeight fw = (weight >= 100 && weight <= 900) ? (FontWeight)weight : FONT_WEIGHT_NORMAL;
    char* cursor = family_list;
    while (cursor && *cursor) {
        while (*cursor == ' ' || *cursor == '\t') cursor++;
        char quote = 0;
        if (*cursor == '"' || *cursor == '\'') { quote = *cursor; cursor++; }

        char* start = cursor;
        char* end = nullptr;
        if (quote) {
            end = strchr(cursor, quote);
            if (!end) end = cursor + strlen(cursor);
            *end = '\0';
            cursor = end + 1;
            char* comma = strchr(cursor, ',');
            cursor = comma ? comma + 1 : nullptr;
        } else {
            end = strchr(cursor, ',');
            if (end) { *end = '\0'; cursor = end + 1; }
            else { cursor = nullptr; }
        }

        char* tail = start + strlen(start);
        while (tail > start && (tail[-1] == ' ' || tail[-1] == '\t')) tail--;
        *tail = '\0';
        if (!*start) continue;
        if (strcasecmp(start, "serif") == 0 || strcasecmp(start, "sans-serif") == 0 ||
            strcasecmp(start, "monospace") == 0 || strcasecmp(start, "cursive") == 0 ||
            strcasecmp(start, "fantasy") == 0) {
            return mem_strdup(start, MEM_CAT_RENDER);
        }
        if (font_face_find_internal(font_ctx, start, fw, slant) ||
            font_family_exists(font_ctx, start)) {
            return mem_strdup(start, MEM_CAT_RENDER);
        }
        char* platform_path = font_platform_find_fallback(start);
        if (platform_path) {
            mem_free(platform_path);
            return mem_strdup(start, MEM_CAT_RENDER);
        }
    }

    return fallback_family ? fallback_family : font_family;
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
            if (str && str->len > 0) {
                // skip whitespace-only nodes
                if (is_whitespace_only(str->chars, str->len)) continue;
                return trim_whitespace(str->chars, str->len);
            }
        }
    }
    return nullptr;
}

static void collect_svg_style_rules(SvgInlineRenderContext* ctx, Element* elem) {
    if (!ctx || !elem) return;
    const char* tag = get_element_tag_name(elem);
    if (tag && strcmp(tag, "style") == 0) {
        const char* css = get_direct_text_content(elem);
        if (css && *css) {
            parse_svg_style_text(ctx, css);
            log_debug("[SVG] collected embedded style rules, total=%d", ctx->style_rule_count);
            mem_free((void*)css);
        }
        return;
    }
    for (int64_t i = 0; i < elem->length; i++) {
        Element* child = get_child_element_at(elem, i);
        if (!child) continue;
        collect_svg_style_rules(ctx, child);
    }
}

/**
 * Create a single ThorVG text object with specified properties
 * Note: font_size is in SVG/CSS user units. ThorVG's text size is used in
 * the same coordinate space as the surrounding SVG transform.
 * anchor_x: horizontal anchor (0=start, 0.5=middle, 1=end) from SVG text-anchor
 */
static Tvg_Paint create_text_segment(const char* text, float x, float y,
                                     const char* font_path, const char* font_name,
                                     float font_size_px, Color fill_color,
                                     float anchor_x = 0.0f) {
    if (!text || !*text || !font_path) return nullptr;

    float font_size_tvg = font_size_px;

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

    result = tvg_text_set_size(tvg_text, font_size_tvg);
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

    log_debug("[SVG] text segment: '%s' at (%.1f, %.1f) size=%.1f color=rgb(%d,%d,%d)",
              text, x, y, font_size_tvg, fill_color.r, fill_color.g, fill_color.b);

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

static void draw_glyph_affine(RenderContext* rdcon, GlyphBitmap* bitmap,
                              float x, float y, float scale_x, float shear_x,
                              float scale_y = 1.0f) {
    if (!rdcon || !bitmap || scale_x <= 0.0f || scale_y <= 0.0f) return;
    if ((fabsf(scale_x - 1.0f) <= 0.01f && fabsf(scale_y - 1.0f) <= 0.01f &&
         fabsf(shear_x) <= 0.001f) ||
        bitmap->pixel_mode == GLYPH_PIXEL_BGRA) {
        draw_glyph(rdcon, bitmap, lroundf(x), lroundf(y));
        return;
    }

    bool saved_has_transform = rdcon->has_transform;
    RdtMatrix saved_transform = rdcon->transform;
    RdtMatrix local = { scale_x, shear_x, x - scale_x * x - shear_x * y,
                        0, scale_y, y - scale_y * y,
                        0, 0, 1 };
    rdcon->has_transform = true;
    rdcon->transform = saved_has_transform ? rdt_matrix_multiply(&local, &saved_transform) : local;
    draw_glyph(rdcon, bitmap, lroundf(x), lroundf(y));
    rdcon->has_transform = saved_has_transform;
    rdcon->transform = saved_transform;
}

static bool render_svg_text_with_radiant_glyphs(SvgInlineRenderContext* ctx, const char* text,
                                                const char* font_family, float font_size,
                                                int font_weight, FontSlant font_slant,
                                                Color fill_color, const RdtMatrix* matrix,
                                                float base_x, float base_y, float text_length,
                                                bool scale_glyphs_x) {
    RenderContext* rdcon = g_svg_active_rdcon;
    if (!rdcon || !ctx || !ctx->font_ctx || !text || !*text || !matrix) return false;

    float sx = sqrtf(matrix->e11 * matrix->e11 + matrix->e21 * matrix->e21);
    float sy = sqrtf(matrix->e12 * matrix->e12 + matrix->e22 * matrix->e22);
    if (sx <= 0.0f || sy <= 0.0f) return false;
    bool rotated_text = fabsf(matrix->e21) > 0.001f;
    float shear_x = matrix->e12 / sy;

    float device_scale = rdcon->scale > 0.0f ? rdcon->scale : 1.0f;
    FontStyleDesc style = {};
    style.family = font_family ? font_family : "Arial";
    style.size_px = font_size * sy / device_scale;
    style.weight = (FontWeight)font_weight;
    style.slant = font_slant;
    FontHandle* handle = font_resolve(ctx->font_ctx, &style);
    if (!handle) return false;

    float oversample = fabsf(shear_x) > 0.001f ? 2.0f : 1.0f;
    FontStyleDesc draw_style = style;
    FontHandle* draw_handle = handle;
    if (oversample > 1.0f) {
        draw_style.size_px = style.size_px * oversample;
        draw_handle = font_resolve(ctx->font_ctx, &draw_style);
        if (!draw_handle) {
            draw_handle = handle;
            draw_style = style;
            oversample = 1.0f;
        }
    }

    float natural_width = 0.0f;
    const unsigned char* cursor = (const unsigned char*)text;
    const unsigned char* end = cursor + strlen(text);
    while (cursor < end) {
        uint32_t codepoint;
        int bytes = str_utf8_decode((const char*)cursor, (size_t)(end - cursor), &codepoint);
        if (bytes <= 0) { cursor++; continue; }
        cursor += bytes;
        LoadedGlyph* glyph = font_load_glyph(handle, &style, codepoint, false);
        if (glyph) natural_width += glyph->advance_x;
    }

    // Glyph bitmaps and advances are already loaded in physical pixels for
    // font_size * sy. Only apply the residual x/y transform ratio here; using
    // sx directly double-scales text on HiDPI displays where sx == sy == DPR.
    float base_x_scale = sx / sy;
    float advance_scale = base_x_scale;
    float glyph_scale_x = base_x_scale;
    float local_advance_scale = 1.0f;
    float local_glyph_scale_x = 1.0f;
    if (text_length > 0.0f && natural_width > 0.0f) {
        if (rotated_text) {
            local_advance_scale = text_length * sy / natural_width;
            local_glyph_scale_x = scale_glyphs_x ? local_advance_scale : 1.0f;
        } else {
            float target_width = text_length * sx;
            advance_scale = target_width / natural_width;
            glyph_scale_x = scale_glyphs_x ? advance_scale : base_x_scale;
        }
        log_debug("[SVG] Radiant textLength fit: '%s' target=%.3f measured=%.3f advance_scale=%.3f",
                  text, rotated_text ? text_length * sy : text_length * sx, natural_width,
                  rotated_text ? local_advance_scale : advance_scale);
    }

    float pen_x = matrix->e11 * base_x + matrix->e12 * base_y + matrix->e13;
    float baseline_y = matrix->e21 * base_x + matrix->e22 * base_y + matrix->e23;
    float local_pen_x = base_x;
    Color saved_color = rdcon->color;
    bool saved_has_transform = rdcon->has_transform;
    rdcon->color = fill_color;
    rdcon->has_transform = false;

    cursor = (const unsigned char*)text;
    while (cursor < end) {
        uint32_t codepoint;
        int bytes = str_utf8_decode((const char*)cursor, (size_t)(end - cursor), &codepoint);
        if (bytes <= 0) { cursor++; continue; }
        cursor += bytes;
        LoadedGlyph* glyph = font_load_glyph(handle, &style, codepoint, false);
        if (!glyph) continue;
        float glyph_advance = glyph->advance_x;
        LoadedGlyph* drawn_glyph = font_load_glyph(draw_handle, &draw_style, codepoint, true);
        if (!drawn_glyph) {
            pen_x += glyph_advance * advance_scale;
            local_pen_x += glyph_advance * advance_scale;
            continue;
        }
        bool has_bitmap = drawn_glyph->bitmap.buffer &&
            drawn_glyph->bitmap.width > 0 &&
            drawn_glyph->bitmap.height > 0 &&
            drawn_glyph->bitmap.pitch > 0;
        if (!has_bitmap) {
            pen_x += glyph_advance * advance_scale;
            local_pen_x += glyph_advance * advance_scale;
            continue;
        }
        if (rotated_text) {
            float local_scale_x = local_glyph_scale_x / (sy * oversample);
            float local_scale_y = 1.0f / (sy * oversample);
            float gx = local_pen_x + drawn_glyph->bitmap.bearing_x * local_scale_x;
            float gy = base_y - drawn_glyph->bitmap.bearing_y * local_scale_y;
            RdtMatrix glyph_scale = {
                local_scale_x, 0, gx - local_scale_x * gx,
                0, local_scale_y, gy - local_scale_y * gy,
                0, 0, 1
            };
            RdtMatrix final_transform = rdt_matrix_multiply(matrix, &glyph_scale);
            rdcon->has_transform = true;
            rdcon->transform = final_transform;
            draw_glyph(rdcon, &drawn_glyph->bitmap, lroundf(gx), lroundf(gy));
        } else {
            float gx = pen_x + drawn_glyph->bitmap.bearing_x * glyph_scale_x / oversample;
            float gy = baseline_y - drawn_glyph->bitmap.bearing_y / oversample;
            draw_glyph_affine(rdcon, &drawn_glyph->bitmap, gx, gy,
                      glyph_scale_x / oversample, shear_x / oversample,
                      1.0f / oversample);
        }
        pen_x += glyph_advance * advance_scale;
        local_pen_x += rotated_text
            ? (glyph_advance / sy) * local_advance_scale
            : glyph_advance * advance_scale;
    }

    rdcon->has_transform = saved_has_transform;
    rdcon->color = saved_color;
    if (draw_handle != handle) font_handle_release(draw_handle);
    font_handle_release(handle);
    return true;
}

/**
 * Render SVG <text> element with proper tspan support
 * Each tspan gets its own color and position
 */
static void render_svg_text(SvgInlineRenderContext* ctx, Element* elem) {
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

    // PDF-generated SVG uses textLength to preserve exact run advances when
    // embedded PDF subset fonts fall back to metric-different system fonts.
    const char* text_length_str = get_svg_attr(elem, "textLength");
    if (!text_length_str) text_length_str = get_svg_attr(elem, "textlength");
    float text_length = text_length_str ? parse_svg_length(text_length_str, 0.0f) : 0.0f;
    const char* length_adjust_str = get_svg_attr(elem, "lengthAdjust");
    if (!length_adjust_str) length_adjust_str = get_svg_attr(elem, "lengthadjust");
    bool spacing_and_glyphs = length_adjust_str &&
        (strcmp(length_adjust_str, "spacingAndGlyphs") == 0 || strcmp(length_adjust_str, "spacingandglyphs") == 0);

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

    const char* raw_font_attr = get_svg_attr(elem, "data-pdf-raw-font");
    bool allow_embedded_font = raw_font_attr && strcmp(raw_font_attr, "true") == 0;

    // resolve font path and name
    const char* font_name = nullptr;
    char* font_path = resolve_svg_font_path(font_family, &font_name, ctx->font_ctx,
                                           font_weight, font_slant, allow_embedded_font);
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
    const char* radiant_family = resolve_svg_radiant_font_family(font_family, ctx->font_ctx,
                                                                 font_weight, font_slant,
                                                                 metrics_family,
                                                                 allow_embedded_font);
    bool free_radiant_family = radiant_family && radiant_family != metrics_family && radiant_family != font_family;
    metrics_family = radiant_family ? radiant_family : metrics_family;
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
    auto draw_text_paint = [&](Tvg_Paint tvg_text, float tx, float ty, float fs_px, float scale_x = 1.0f) {
        if (!tvg_text) return;
        float ascent = fs_px * font_ascent_ratio;
        float adj_y = ty - ascent;
        RdtMatrix pos = rdt_matrix_translate(tx, adj_y);
        RdtMatrix local = pos;
        if (scale_x > 0.0f && fabsf(scale_x - 1.0f) > 0.001f) {
            RdtMatrix scale = { scale_x, 0, 0,  0, 1, 0,  0, 0, 1 };
            local = rdt_matrix_multiply(&pos, &scale);
        }
        RdtMatrix final_m = rdt_matrix_multiply(&m, &local);
        RdtPicture* pic = rdt_picture_take_tvg_paint(tvg_text, 0, 0);
        if (pic) {
            svg_draw_picture(ctx, pic, 255, &final_m);
        }
    };

    // if single text with no tspan, use simple rendering
    if (text_segments == 1 && !has_tspan) {
        const char* text_content = get_direct_text_content(elem);
        if (text_content) {
            float text_scale_x = 1.0f;
            if (text_length > 0.0f) {
                float measured_width = measure_svg_text_width(text_content, font_size, ctx->font_ctx, metrics_family, font_weight);
                if (measured_width > 0.0f) {
                    text_scale_x = text_length / measured_width;
                    log_debug("[SVG] textLength fit: '%s' target=%.3f measured=%.3f scale_x=%.3f",
                              text_content, text_length, measured_width, text_scale_x);
                }
            }
            bool rendered_with_radiant = false;
            if (anchor_x >= 0.0f) {
                float anchor_adjust = 0.0f;
                if (anchor_x > 0.0f) {
                    float anchor_width = text_length > 0.0f ? text_length :
                        measure_svg_text_width(text_content, font_size, ctx->font_ctx, metrics_family, font_weight);
                    anchor_adjust = anchor_width * anchor_x;
                }
                rendered_with_radiant = render_svg_text_with_radiant_glyphs(ctx, text_content,
                    metrics_family, font_size, font_weight, font_slant, default_fill, &m,
                    base_x - anchor_adjust, base_y, text_length, spacing_and_glyphs && !allow_embedded_font);
            }
            Tvg_Paint text = nullptr;
            if (!rendered_with_radiant) {
                text = create_text_segment(text_content, base_x, base_y,
                                           font_path, font_name, font_size, default_fill,
                                           anchor_x);
                if (text && text_length > 0.0f) {
                    float bounds_x = 0.0f;
                    float bounds_y = 0.0f;
                    float bounds_w = 0.0f;
                    float bounds_h = 0.0f;
                    if (tvg_paint_get_aabb(text, &bounds_x, &bounds_y, &bounds_w, &bounds_h) == TVG_RESULT_SUCCESS &&
                        bounds_w > 0.0f) {
                        text_scale_x = text_length / bounds_w;
                        log_debug("[SVG] ThorVG textLength fit: '%s' target=%.3f bounds=%.3f scale_x=%.3f",
                                  text_content, text_length, bounds_w, text_scale_x);
                    }
                }
            }
            mem_free((void*)text_content);
            if (!rendered_with_radiant) {
                draw_text_paint(text, base_x, base_y, font_size, text_scale_x);
            }
        }
        mem_free(font_path);
        if (free_radiant_family) mem_free((void*)radiant_family);
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
            if (str && str->len > 0) {
                if (is_whitespace_only(str->chars, str->len)) {
                    // SVG spec: whitespace between tspans collapses to a single space
                    if (has_tspan) {
                        cur_x += measure_svg_text_width(" ", font_size, ctx->font_ctx, metrics_family, font_weight);
                    }
                    continue;
                }
                char* text_copy = trim_whitespace(str->chars, str->len);
                if (text_copy) {
                    bool rendered_with_radiant = false;
                    if (!has_tspan && anchor_x == 0.0f) {
                        rendered_with_radiant = render_svg_text_with_radiant_glyphs(ctx, text_copy,
                            metrics_family, font_size, font_weight, font_slant, default_fill, &m,
                            cur_x, cur_y, text_length, spacing_and_glyphs && !allow_embedded_font);
                    }
                    if (rendered_with_radiant) {
                        float w = text_length > 0.0f ? text_length :
                            measure_svg_text_width(text_copy, font_size, ctx->font_ctx, metrics_family, font_weight);
                        cur_x += w;
                    } else {
                        Tvg_Paint text_obj = create_text_segment(text_copy, cur_x, cur_y,
                                                                  font_path, font_name, font_size, default_fill);
                        if (text_obj) {
                            float w = measure_svg_text_width(text_copy, font_size, ctx->font_ctx, metrics_family, font_weight);
                            draw_text_paint(text_obj, cur_x, cur_y, font_size);
                            cur_x += w;
                        }
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
                    bool rendered_with_radiant = false;
                    if (anchor_x == 0.0f) {
                        rendered_with_radiant = render_svg_text_with_radiant_glyphs(ctx, text_content,
                            metrics_family, tspan_font_size, font_weight, font_slant, fill, &m,
                            cur_x, cur_y, text_length, spacing_and_glyphs && !allow_embedded_font);
                    }
                    if (rendered_with_radiant) {
                        float w = text_length > 0.0f ? text_length :
                            measure_svg_text_width(text_content, tspan_font_size, ctx->font_ctx, metrics_family, font_weight);
                        cur_x += w;
                    } else {
                        Tvg_Paint text_obj = create_text_segment(text_content, cur_x, cur_y,
                                                                  font_path, font_name, tspan_font_size, fill);
                        if (text_obj) {
                            float w = measure_svg_text_width(text_content, tspan_font_size, ctx->font_ctx, metrics_family, font_weight);
                            draw_text_paint(text_obj, cur_x, cur_y, tspan_font_size);
                            cur_x += w;
                        }
                    }
                    mem_free((void*)text_content);
                }
            }
        }
    }

    mem_free(font_path);
    if (free_radiant_family) mem_free((void*)radiant_family);

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
static bool svg_image_href_is_svg(const char* href) {
    if (!href || !*href) return false;
    if (strncmp(href, "data:", 5) == 0) {
        const char* comma = strchr(href, ',');
        size_t meta_len = comma ? (size_t)(comma - href) : strlen(href);
        return str_ifind(href, meta_len, "image/svg", strlen("image/svg")) != STR_NPOS;
    }

    const char* end = href + strlen(href);
    const char* fragment = strchr(href, '#');
    if (fragment && fragment < end) end = fragment;
    const char* query = strchr(href, '?');
    if (query && query < end) end = query;
    if (end - href < 4) return false;
    return str_ieq_const(end - 4, 4, ".svg");
}

static bool svg_preserve_aspect_none(const char* value) {
    if (!value) return false;
    while (*value && isspace((unsigned char)*value)) value++;
    return strncmp(value, "none", 4) == 0 &&
           (value[4] == '\0' || isspace((unsigned char)value[4]));
}

static bool svg_raster_dimensions(const uint8_t* data, size_t len, float* out_w, float* out_h) {
    if (!data || len < 8 || !out_w || !out_h) return false;

    if (len >= 24 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        uint32_t w = ((uint32_t)data[16] << 24) | ((uint32_t)data[17] << 16) |
                     ((uint32_t)data[18] << 8) | (uint32_t)data[19];
        uint32_t h = ((uint32_t)data[20] << 24) | ((uint32_t)data[21] << 16) |
                     ((uint32_t)data[22] << 8) | (uint32_t)data[23];
        if (w == 0 || h == 0) return false;
        *out_w = (float)w;
        *out_h = (float)h;
        return true;
    }

    if (len < 4 || data[0] != 0xFF || data[1] != 0xD8) return false;
    size_t i = 2;
    while (i + 9 < len) {
        while (i < len && data[i] != 0xFF) i++;
        while (i < len && data[i] == 0xFF) i++;
        if (i >= len) break;
        uint8_t marker = data[i++];
        if (marker == 0xD8 || marker == 0xD9 || (marker >= 0xD0 && marker <= 0xD7)) continue;
        if (i + 2 > len) break;
        uint16_t seg_len = ((uint16_t)data[i] << 8) | (uint16_t)data[i + 1];
        if (seg_len < 2 || i + seg_len > len) break;
        if ((marker >= 0xC0 && marker <= 0xC3) || (marker >= 0xC5 && marker <= 0xC7) ||
            (marker >= 0xC9 && marker <= 0xCB) || (marker >= 0xCD && marker <= 0xCF)) {
            if (seg_len < 7) return false;
            uint16_t h = ((uint16_t)data[i + 3] << 8) | (uint16_t)data[i + 4];
            uint16_t w = ((uint16_t)data[i + 5] << 8) | (uint16_t)data[i + 6];
            if (w == 0 || h == 0) return false;
            *out_w = (float)w;
            *out_h = (float)h;
            return true;
        }
        i += seg_len;
    }
    return false;
}

static char* svg_href_file_part(const char* href, const char** fragment_out) {
    if (fragment_out) *fragment_out = nullptr;
    if (!href || !*href) return nullptr;
    const char* fragment = strchr(href, '#');
    const char* end = fragment ? fragment : href + strlen(href);
    if (fragment_out && fragment && fragment[1]) *fragment_out = fragment + 1;
    if (end == href) return mem_strdup("", MEM_CAT_RENDER);
    char* out = (char*)mem_alloc((size_t)(end - href) + 1, MEM_CAT_RENDER);
    if (!out) return nullptr;
    memcpy(out, href, (size_t)(end - href));
    out[end - href] = '\0';
    return out;
}

static char* svg_resolve_resource_path(SvgInlineRenderContext* ctx, const char* href_no_fragment) {
    if (!href_no_fragment || !*href_no_fragment) return nullptr;
    if (strncmp(href_no_fragment, "data:", 5) == 0 || strstr(href_no_fragment, "://") || href_no_fragment[0] == '/') {
        return mem_strdup(href_no_fragment, MEM_CAT_RENDER);
    }
    if (!ctx || !ctx->source_path || !*ctx->source_path) return mem_strdup(href_no_fragment, MEM_CAT_RENDER);
    char* dir = file_path_dirname(ctx->source_path);
    if (!dir) return mem_strdup(href_no_fragment, MEM_CAT_RENDER);
    char* path = file_path_join(dir, href_no_fragment);
    mem_free(dir);
    return path;
}

static int svg_pdf_image_id_from_href(const char* href) {
    if (!href || strncmp(href, "img:", 4) != 0) return 0;
    const char* p = href + 4;
    if (!*p) return 0;
    int value = 0;
    while (*p) {
        if (*p < '0' || *p > '9') return 0;
        value = value * 10 + (*p - '0');
        p++;
    }
    return value;
}

static bool svg_item_number_equals(ItemReader item, int value) {
    if (item.isInt()) return item.asInt() == value;
    if (item.isFloat()) return fabs(item.asFloat() - (double)value) < 0.0001;
    return false;
}

static const char* svg_pdf_registered_image_resolver(void* context, int object_num) {
    SvgImageResolverEntry* entry = (SvgImageResolverEntry*)context;
    if (!entry || object_num <= 0) return nullptr;

    MapReader pdf_root = MapReader::fromItem(entry->pdf_root);
    if (!pdf_root.isValid()) return nullptr;
    ItemReader objects_item = pdf_root.get("objects");
    if (!objects_item.isArray()) return nullptr;

    ArrayReader objects = objects_item.asArray();
    int64_t count = objects.length();
    for (int64_t i = 0; i < count; i++) {
        ItemReader obj_item = objects.get(i);
        MapReader obj = MapReader::fromItem(obj_item.item());
        if (!obj.isValid()) continue;
        ItemReader num_item = obj.get("object_num");
        if (!svg_item_number_equals(num_item, object_num)) continue;

        ItemReader content_item = obj.get("content");
        MapReader content = MapReader::fromItem(content_item.item());
        if (!content.isValid()) return nullptr;
        ItemReader data_uri = content.get("data_uri");
        return data_uri.isString() ? data_uri.cstring() : nullptr;
    }

    return nullptr;
}

static const char* svg_pdf_data_uri_for_image_id(SvgInlineRenderContext* ctx, int object_num) {
    if (!ctx || !ctx->image_resolver || object_num <= 0) return nullptr;
    return ctx->image_resolver(ctx->image_resolver_context, object_num);
}

static const char* svg_resolve_pdf_image_href(SvgInlineRenderContext* ctx, const char* href) {
    int object_num = svg_pdf_image_id_from_href(href);
    if (object_num <= 0) return href;
    const char* data_uri = svg_pdf_data_uri_for_image_id(ctx, object_num);
    if (data_uri && *data_uri) {
        log_debug("[SVG] resolved PDF image handle img:%d", object_num);
        return data_uri;
    }
    log_debug("[SVG] PDF image handle unresolved: img:%d", object_num);
    return href;
}

static void render_svg_image(SvgInlineRenderContext* ctx, Element* elem) {
    if (!elem) return;

    // get href attribute (SVG 2 uses href, SVG 1.1 uses xlink:href)
    const char* href = get_svg_attr(elem, "href");
    if (!href) href = get_svg_attr(elem, "xlink:href");
    if (!href || !*href) {
        log_debug("[SVG] <image> missing href attribute");
        return;
    }

    const char* display_href = href;
    href = svg_resolve_pdf_image_href(ctx, href);

    // parse position and size
    float x = parse_svg_length(get_svg_attr(elem, "x"), 0);
    float y = parse_svg_length(get_svg_attr(elem, "y"), 0);
    float width = parse_svg_length(get_svg_attr(elem, "width"), 0);
    float height = parse_svg_length(get_svg_attr(elem, "height"), 0);

    bool href_is_svg = svg_image_href_is_svg(href);

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
            return;
        }

        if (strcmp(mime_hint, "svg") == 0) {
            RdtPicture* rdt_pic = rdt_picture_load_data((const char*)decoded, (int)decoded_len, "svg");
            mem_free(decoded);
            if (!rdt_pic) {
                log_debug("[SVG] <image> failed to parse nested SVG data URI");
                return;
            }
            if (width > 0 && height > 0) {
                rdt_picture_set_size(rdt_pic, width, height);
            }

            uint8_t op = 255;
            const char* opacity = get_svg_attr(elem, "opacity");
            if (opacity) {
                float opf = strtof(opacity, nullptr);
                op = (uint8_t)(opf * 255);
            }

            RdtMatrix m = compose_element_transform(ctx, elem);
            RdtMatrix translate = rdt_matrix_translate(x, y);
            RdtMatrix final_m = rdt_matrix_multiply(&m, &translate);
            svg_draw_picture(ctx, rdt_pic, op, &final_m);
            log_debug("[SVG] <image> loaded nested SVG data URI at (%.1f, %.1f) size %.1fx%.1f", x, y, width, height);
            return;
        }

        Tvg_Paint pic = tvg_picture_new();
        if (!pic) { mem_free(decoded); return; }

        float intrinsic_w = 0;
        float intrinsic_h = 0;
        bool has_raster_dims = svg_raster_dimensions(decoded, decoded_len, &intrinsic_w, &intrinsic_h);

        // copy=true so ThorVG holds its own copy and we can free decoded
        Tvg_Result result = tvg_picture_load_data(pic, (const char*)decoded, (uint32_t)decoded_len,
                                                  mime_hint, NULL, true);
        mem_free(decoded);
        if (result != TVG_RESULT_SUCCESS) {
            log_debug("[SVG] <image> failed to load data URI (mime=%s, declared=%s)",
                      mime_hint, declared_mime);
            tvg_paint_unref(pic, true);
            return;
        }

        RdtMatrix m = compose_element_transform(ctx, elem);
        bool stretched = false;
        if (width > 0 && height > 0) {
            if (svg_preserve_aspect_none(get_svg_attr(elem, "preserveAspectRatio")) && has_raster_dims) {
                float fit_h = width * intrinsic_h / intrinsic_w;
                if (fit_h > 0.0f) {
                    tvg_picture_set_size(pic, width, fit_h);
                    RdtMatrix local = { 1, 0, x,  0, height / fit_h, y,  0, 0, 1 };
                    m = rdt_matrix_multiply(&m, &local);
                    stretched = true;
                }
            }
            if (!stretched) {
                tvg_picture_set_size(pic, width, height);
            }
        }
        if (!stretched) {
            tvg_paint_translate(pic, x, y);
        }

        uint8_t op = 255;
        const char* opacity = get_svg_attr(elem, "opacity");
        if (opacity) {
            float opf = strtof(opacity, nullptr);
            op = (uint8_t)(opf * 255);
        }

        RdtPicture* rdt_pic = rdt_picture_take_tvg_paint(pic, 0, 0);
        if (rdt_pic) {
            svg_draw_picture(ctx, rdt_pic, op, &m);
        }

        log_debug("[SVG] <image> loaded: %s at (%.1f, %.1f) size %.1fx%.1f", display_href, x, y, width, height);
        return;
    } else if (href_is_svg) {
        char* href_file = svg_href_file_part(href, nullptr);
        char* resolved_href = svg_resolve_resource_path(ctx, href_file ? href_file : href);
        if (href_file) mem_free(href_file);
        if (resolved_href && svg_resource_stack_contains(resolved_href)) {
            log_debug("[SVG] <image> skipped recursive SVG reference: %s", resolved_href);
            mem_free(resolved_href);
            return;
        }
        RdtPicture* rdt_pic = rdt_picture_load(resolved_href ? resolved_href : href);
        if (!rdt_pic) {
            log_debug("[SVG] <image> failed to parse nested SVG: %s", href);
            if (resolved_href) mem_free(resolved_href);
            return;
        }
        if (width > 0 && height > 0) {
            rdt_picture_set_size(rdt_pic, width, height);
        }

        uint8_t op = 255;
        const char* opacity = get_svg_attr(elem, "opacity");
        if (opacity) {
            float opf = strtof(opacity, nullptr);
            op = (uint8_t)(opf * 255);
        }

        RdtMatrix m = compose_element_transform(ctx, elem);
        RdtMatrix translate = rdt_matrix_translate(x, y);
        RdtMatrix final_m = rdt_matrix_multiply(&m, &translate);
        svg_draw_picture(ctx, rdt_pic, op, &final_m);
        log_debug("[SVG] <image> loaded nested SVG: %s at (%.1f, %.1f) size %.1fx%.1f", resolved_href ? resolved_href : href, x, y, width, height);
        if (resolved_href) mem_free(resolved_href);
        return;
    } else {
        Tvg_Paint pic = tvg_picture_new();
        if (!pic) return;
        char* href_file = svg_href_file_part(href, nullptr);
        char* resolved_href = svg_resolve_resource_path(ctx, href_file ? href_file : href);
        if (href_file) mem_free(href_file);
        Tvg_Result result = tvg_picture_load(pic, resolved_href ? resolved_href : href);
        if (result != TVG_RESULT_SUCCESS) {
            log_debug("[SVG] <image> failed to load: %s", display_href);
            if (resolved_href) mem_free(resolved_href);
            tvg_paint_unref(pic, true);
            return;
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
        if (resolved_href) mem_free(resolved_href);
    }
}

// ============================================================================
// SVG ClipPath Support
// ============================================================================

static RdtPath* build_path_from_svg_shape(Element* elem) {
    if (!elem) return nullptr;
    const char* tag = get_element_tag_name(elem);
    if (!tag) return nullptr;

    RdtPath* path = rdt_path_new();
    bool has_geometry = false;

    if (strcmp(tag, "rect") == 0) {
        float x = parse_svg_length(get_svg_attr(elem, "x"), 0);
        float y = parse_svg_length(get_svg_attr(elem, "y"), 0);
        float w = parse_svg_length(get_svg_attr(elem, "width"), 0);
        float h = parse_svg_length(get_svg_attr(elem, "height"), 0);
        float rx = parse_svg_length(get_svg_attr(elem, "rx"), 0);
        float ry = parse_svg_length(get_svg_attr(elem, "ry"), rx);
        if (w > 0 && h > 0) {
            rdt_path_add_rect(path, x, y, w, h, rx, ry);
            has_geometry = true;
        }
    } else if (strcmp(tag, "circle") == 0) {
        float cx = parse_svg_length(get_svg_attr(elem, "cx"), 0);
        float cy = parse_svg_length(get_svg_attr(elem, "cy"), 0);
        float r = parse_svg_length(get_svg_attr(elem, "r"), 0);
        if (r > 0) {
            rdt_path_add_circle(path, cx, cy, r, r);
            has_geometry = true;
        }
    } else if (strcmp(tag, "ellipse") == 0) {
        float cx = parse_svg_length(get_svg_attr(elem, "cx"), 0);
        float cy = parse_svg_length(get_svg_attr(elem, "cy"), 0);
        float rx = parse_svg_length(get_svg_attr(elem, "rx"), 0);
        float ry = parse_svg_length(get_svg_attr(elem, "ry"), 0);
        if (rx > 0 && ry > 0) {
            rdt_path_add_circle(path, cx, cy, rx, ry);
            has_geometry = true;
        }
    } else if (strcmp(tag, "polygon") == 0 || strcmp(tag, "polyline") == 0) {
        const char* points = get_svg_attr(elem, "points");
        if (points) {
            has_geometry = parse_points_to_path(points, path, (strcmp(tag, "polygon") == 0));
        }
    } else if (strcmp(tag, "path") == 0) {
        const char* d = get_svg_attr(elem, "d");
        if (d) {
            rdt_path_free(path);
            return parse_svg_path_d(d);
        }
    }

    if (!has_geometry) {
        rdt_path_free(path);
        return nullptr;
    }
    return path;
}

static bool svg_mask_rect_bounds(Element* elem, float* x, float* y, float* w, float* h,
                                 float* rx, float* ry) {
    if (!elem || !x || !y || !w || !h || !rx || !ry) return false;
    const char* tag = get_element_tag_name(elem);
    if (!tag || strcmp(tag, "rect") != 0) return false;
    *x = parse_svg_length(get_svg_attr(elem, "x"), 0.0f);
    *y = parse_svg_length(get_svg_attr(elem, "y"), 0.0f);
    *w = parse_svg_length(get_svg_attr(elem, "width"), 0.0f);
    *h = parse_svg_length(get_svg_attr(elem, "height"), 0.0f);
    *rx = parse_svg_length(get_svg_attr(elem, "rx"), 0.0f);
    *ry = parse_svg_length(get_svg_attr(elem, "ry"), *rx);
    if (*rx < 0.0f) *rx = 0.0f;
    if (*ry < 0.0f) *ry = 0.0f;
    if (*rx > *w * 0.5f) *rx = *w * 0.5f;
    if (*ry > *h * 0.5f) *ry = *h * 0.5f;
    return *w > 0.0f && *h > 0.0f;
}

static float svg_mask_source_alpha_for_child(SvgInlineRenderContext* ctx, Element* child);

static bool svg_mask_next_opaque_rect(SvgInlineRenderContext* ctx, Element* mask_elem,
                                      int64_t start_index, float* x, float* y,
                                      float* w, float* h, float* rx, float* ry) {
    if (!ctx || !mask_elem) return false;
    for (int64_t j = start_index; j < mask_elem->length; j++) {
        Element* later = get_child_element_at(mask_elem, j);
        if (svg_mask_source_alpha_for_child(ctx, later) >= 0.999f &&
            svg_mask_rect_bounds(later, x, y, w, h, rx, ry)) {
            return true;
        }
    }
    return false;
}

static void render_svg_masked_source_clip(SvgInlineRenderContext* ctx, Element* elem,
                                          RdtPath* mask_path,
                                          const RdtMatrix* mask_transform,
                                          float opacity) {
    if (!ctx || !elem || !mask_path) return;
    float saved_opacity = ctx->opacity;
    svg_push_clip(ctx, mask_path, mask_transform);
    ctx->opacity = opacity;
    render_svg_element(ctx, elem);
    svg_pop_clip(ctx);
    ctx->opacity = saved_opacity;
}

static bool render_svg_masked_source_extra_clip(SvgInlineRenderContext* ctx, Element* elem,
                                                RdtPath* mask_path,
                                                const RdtMatrix* mask_transform,
                                                RdtPath* extra_path,
                                                float opacity) {
    if (!ctx || !elem || !mask_path || !extra_path) return false;
    svg_push_clip(ctx, mask_path, mask_transform);
    svg_push_clip(ctx, extra_path, &ctx->transform);
    float saved_opacity = ctx->opacity;
    ctx->opacity = opacity;
    render_svg_element(ctx, elem);
    ctx->opacity = saved_opacity;
    svg_pop_clip(ctx);
    svg_pop_clip(ctx);
    return true;
}

static RdtPath* svg_mask_corner_outside_round_rect(float x, float y, float w, float h,
                                                  float rx, float ry, int corner) {
    if (rx <= 0.0f || ry <= 0.0f) return nullptr;
    const float k = 0.5522847498f;
    RdtPath* path = rdt_path_new();
    if (corner == 0) {
        rdt_path_move_to(path, x, y);
        rdt_path_line_to(path, x + rx, y);
        rdt_path_cubic_to(path, x + rx - rx * k, y, x, y + ry - ry * k, x, y + ry);
        rdt_path_line_to(path, x, y);
    } else if (corner == 1) {
        rdt_path_move_to(path, x + w - rx, y);
        rdt_path_line_to(path, x + w, y);
        rdt_path_line_to(path, x + w, y + ry);
        rdt_path_cubic_to(path, x + w, y + ry - ry * k, x + w - rx + rx * k, y, x + w - rx, y);
    } else if (corner == 2) {
        rdt_path_move_to(path, x, y + h - ry);
        rdt_path_cubic_to(path, x, y + h - ry + ry * k, x + rx - rx * k, y + h, x + rx, y + h);
        rdt_path_line_to(path, x, y + h);
        rdt_path_line_to(path, x, y + h - ry);
    } else {
        rdt_path_move_to(path, x + w, y + h - ry);
        rdt_path_line_to(path, x + w, y + h);
        rdt_path_line_to(path, x + w - rx, y + h);
        rdt_path_cubic_to(path, x + w - rx + rx * k, y + h, x + w, y + h - ry + ry * k,
                          x + w, y + h - ry);
    }
    rdt_path_close(path);
    return path;
}

static bool render_svg_masked_source_excluding_rect(SvgInlineRenderContext* ctx, Element* elem,
                                                    RdtPath* mask_path,
                                                    const RdtMatrix* mask_transform,
                                                    float opacity,
                                                    float mask_x, float mask_y,
                                                    float mask_w, float mask_h,
                                                    float ex_x, float ex_y,
                                                    float ex_w, float ex_h,
                                                    float ex_rx, float ex_ry) {
    if (!ctx || !elem || !mask_path || mask_w <= 0.0f || mask_h <= 0.0f ||
        ex_w <= 0.0f || ex_h <= 0.0f) {
        return false;
    }

    float mask_r = mask_x + mask_w;
    float mask_b = mask_y + mask_h;
    float ex_r = ex_x + ex_w;
    float ex_b = ex_y + ex_h;
    if (ex_x < mask_x) ex_x = mask_x;
    if (ex_y < mask_y) ex_y = mask_y;
    if (ex_r > mask_r) ex_r = mask_r;
    if (ex_b > mask_b) ex_b = mask_b;
    if (ex_x >= ex_r || ex_y >= ex_b) return false;

    float rects[4][4] = {
        {mask_x, mask_y, ex_x - mask_x, mask_h},
        {ex_r, mask_y, mask_r - ex_r, mask_h},
        {ex_x, mask_y, ex_r - ex_x, ex_y - mask_y},
        {ex_x, ex_b, ex_r - ex_x, mask_b - ex_b}
    };

    bool painted = false;
    for (int i = 0; i < 4; i++) {
        if (rects[i][2] <= 0.0f || rects[i][3] <= 0.0f) continue;
        RdtPath* slice = rdt_path_new();
        rdt_path_add_rect(slice, rects[i][0], rects[i][1], rects[i][2], rects[i][3],
                          0.0f, 0.0f);
        if (render_svg_masked_source_extra_clip(ctx, elem, mask_path, mask_transform,
                                                slice, opacity)) {
            painted = true;
        }
        rdt_path_free(slice);
    }

    if (ex_rx > 0.0f && ex_ry > 0.0f) {
        for (int i = 0; i < 4; i++) {
            RdtPath* corner_path = svg_mask_corner_outside_round_rect(ex_x, ex_y,
                                                                      ex_w, ex_h,
                                                                      ex_rx, ex_ry, i);
            if (!corner_path) continue;
            if (render_svg_masked_source_extra_clip(ctx, elem, mask_path, mask_transform,
                                                    corner_path, opacity)) {
                painted = true;
            }
            rdt_path_free(corner_path);
        }
    }
    return painted;
}

// Build a composite RdtPath from the children of a <clipPath> element.
// Returns new path (caller must free), or nullptr if no geometry found.
static RdtPath* build_clip_path_from_def(Element* clip_elem) {
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
static RdtPath* resolve_svg_clip_path(SvgInlineRenderContext* ctx, Element* elem) {
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

    RdtPath* path = build_clip_path_from_def(clip_elem);
    if (path) {
        log_debug("[SVG] resolved clip-path='%s'", cp);
    }
    return path;
}

// ============================================================================
// SVG Group and Children
// ============================================================================

static void render_svg_group(SvgInlineRenderContext* ctx, Element* elem) {
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
    char opacity_buf[64];
    const char* opacity_attr = get_svg_attr_or_style(ctx, elem, "opacity", opacity_buf, sizeof(opacity_buf));
    float group_op = 1.0f;
    bool use_opacity_layer = false;
    int op_x0 = 0, op_y0 = 0, op_w = 0, op_h = 0;
    if (opacity_attr) {
        group_op = strtof(opacity_attr, nullptr);
        if (group_op < 0.0f) group_op = 0.0f;
        if (group_op > 1.0f) group_op = 1.0f;
        if (group_op < 1.0f) {
            // use backdrop save/composite so overlapping children composite correctly
            // compute bounds in screen coords from either an explicit PDF Form
            // bounds hint or, for general SVG, the whole viewport fallback.
            float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
            const char* pdf_bounds = get_svg_attr(elem, "data-pdf-bounds");
            if (parse_pdf_bounds_attr(pdf_bounds, &bx, &by, &bw, &bh)) {
                opacity_bounds_from_rect(&ctx->transform, bx, by, bw, bh, &op_x0, &op_y0, &op_w, &op_h);
            }
            else {
                opacity_bounds_from_rect(&ctx->transform, 0.0f, 0.0f,
                                         ctx->viewbox_width, ctx->viewbox_height,
                                         &op_x0, &op_y0, &op_w, &op_h);
            }
            if (op_w > 0 && op_h > 0) {
                use_opacity_layer = true;
                svg_save_backdrop(ctx, op_x0, op_y0, op_w, op_h);
                log_debug("[SVG-GROUP] opacity=%.2f, save backdrop (%d,%d,%d,%d)",
                          group_op, op_x0, op_y0, op_w, op_h);
            }
        }
    }
    // if not using opacity layer, fall back to inherited alpha multiply
    if (!use_opacity_layer && group_op < 1.0f) {
        ctx->opacity *= group_op;
    }

    svg_apply_inherited_paint_attrs(ctx, elem);

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
        svg_composite_opacity(ctx, op_x0, op_y0, op_w, op_h, group_op);
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

static void render_svg_children(SvgInlineRenderContext* ctx, Element* elem) {
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

static void process_svg_defs(SvgInlineRenderContext* ctx, Element* defs) {
    if (!defs) return;

    for (int64_t i = 0; i < defs->length; i++) {
        Element* child = get_child_element_at(defs, i);
        if (!child) continue;
        register_svg_def_element(ctx, child);
    }
}

static void process_svg_def_resources(SvgInlineRenderContext* ctx, Element* elem) {
    if (!ctx || !elem) return;
    for (int64_t i = 0; i < elem->length; i++) {
        Element* child = get_child_element_at(elem, i);
        if (!child) continue;
        const char* child_tag = get_element_tag_name(child);
        if (!child_tag) continue;
        if (strcmp(child_tag, "defs") == 0) {
            process_svg_defs(ctx, child);
        } else if (strcmp(child_tag, "linearGradient") == 0 ||
                   strcmp(child_tag, "radialGradient") == 0 ||
                   strcmp(child_tag, "clipPath") == 0 ||
                   strcmp(child_tag, "mask") == 0 ||
                   strcmp(child_tag, "symbol") == 0 ||
                   strcmp(child_tag, "pattern") == 0 ||
                   strcmp(child_tag, "marker") == 0) {
            register_svg_def_element(ctx, child);
        }
        process_svg_def_resources(ctx, child);
    }
}

static void process_svg_root_resources(SvgInlineRenderContext* ctx, Element* svg_element) {
    if (!ctx || !svg_element) return;
    collect_svg_style_rules(ctx, svg_element);
    process_svg_def_resources(ctx, svg_element);
}

static void render_svg_use_target(SvgInlineRenderContext* ctx, Element* use_elem, Element* ref, const char* href) {
    if (!ctx || !use_elem || !ref) return;
    float ux = parse_svg_length(get_svg_attr(use_elem, "x"), 0.0f);
    float uy = parse_svg_length(get_svg_attr(use_elem, "y"), 0.0f);
    RdtMatrix saved = ctx->transform;
    Color saved_color = ctx->current_color;
    RdtMatrix el_m = compose_element_transform(ctx, use_elem);
    if (ux != 0.0f || uy != 0.0f) {
        RdtMatrix translate = rdt_matrix_translate(ux, uy);
        ctx->transform = rdt_matrix_multiply(&el_m, &translate);
    } else {
        ctx->transform = el_m;
    }

    const char* use_color = get_svg_attr(use_elem, "color");
    if (use_color) {
        ctx->current_color = parse_svg_color(use_color);
    }

    const char* ref_tag = get_element_tag_name(ref);
    if (ref_tag && strcmp(ref_tag, "symbol") == 0) {
        const char* vb_attr = get_svg_attr(ref, "viewBox");
        SvgViewBox vb = parse_svg_viewbox(vb_attr);
        float sym_w = parse_svg_length(get_svg_attr(use_elem, "width"), vb.has_viewbox ? vb.width : 0);
        float sym_h = parse_svg_length(get_svg_attr(use_elem, "height"), vb.has_viewbox ? vb.height : 0);

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
        render_svg_children(ctx, ref);
        log_debug("[SVG] rendered <use> -> <symbol> href='%s'", href ? href : "(none)");
    } else {
        render_svg_element(ctx, ref);
    }

    ctx->transform = saved;
    ctx->current_color = saved_color;
}

static bool render_svg_external_use(SvgInlineRenderContext* ctx, Element* use_elem, const char* href) {
    if (!ctx || !use_elem || !href) return false;
    const char* fragment = nullptr;
    char* href_file = svg_href_file_part(href, &fragment);
    if (!href_file || !*href_file || !fragment || !*fragment) {
        if (href_file) mem_free(href_file);
        return false;
    }
    char* resolved_href = svg_resolve_resource_path(ctx, href_file);
    mem_free(href_file);
    if (!resolved_href) return false;
    if (svg_resource_stack_contains(resolved_href)) {
        log_debug("[SVG] external <use> skipped recursive reference '%s'", resolved_href);
        mem_free(resolved_href);
        return false;
    }
    bool pushed_resource = svg_resource_stack_push(resolved_href);

    RdtPicture* pic = rdt_picture_load(resolved_href);
    if (!pic) {
        log_debug("[SVG] external <use> failed to load '%s'", resolved_href);
        if (pushed_resource) svg_resource_stack_pop(resolved_href);
        mem_free(resolved_href);
        return false;
    }
    Element* root = rdt_picture_get_svg_root(pic);
    Element* ref = rdt_picture_find_svg_element_by_id(pic, fragment);
    if (!root || !ref) {
        log_debug("[SVG] external <use> missing id '%s' in '%s'", fragment, resolved_href);
        rdt_picture_free(pic);
        if (pushed_resource) svg_resource_stack_pop(resolved_href);
        mem_free(resolved_href);
        return false;
    }

    HashMap* saved_defs = ctx->defs;
    void* saved_rules = ctx->style_rules;
    int saved_rule_count = ctx->style_rule_count;
    int saved_rule_capacity = ctx->style_rule_capacity;
    const char* saved_source_path = ctx->source_path;
    ctx->defs = nullptr;
    ctx->style_rules = nullptr;
    ctx->style_rule_count = 0;
    ctx->style_rule_capacity = 0;
    ctx->source_path = rdt_picture_get_source_path(pic);  // RETAINED_FIELD_OK: render-context field, not a retained DOM field
    process_svg_root_resources(ctx, root);
    render_svg_use_target(ctx, use_elem, ref, href);
    if (ctx->style_rules) mem_free(ctx->style_rules);
    if (ctx->defs) mem_free(ctx->defs);
    ctx->defs = saved_defs;
    ctx->style_rules = saved_rules;
    ctx->style_rule_count = saved_rule_count;
    ctx->style_rule_capacity = saved_rule_capacity;
    ctx->source_path = saved_source_path;  // RETAINED_FIELD_OK: render-context field, not a retained DOM field

    rdt_picture_free(pic);
    log_debug("[SVG] external <use> rendered href='%s'", href);
    if (pushed_resource) svg_resource_stack_pop(resolved_href);
    mem_free(resolved_href);
    return true;
}

// ============================================================================
// SVG Mask Support
// ============================================================================

static float svg_mask_luminance_for_child(SvgInlineRenderContext* ctx, Element* child) {
    char fill_buf[256];
    char opacity_buf[64];
    char fill_opacity_buf[64];
    const char* fill = get_svg_attr_or_style(ctx, child, "fill", fill_buf, sizeof(fill_buf));
    if (fill && strcmp(fill, "none") == 0) return 0.0f;

    Color c;
    if (fill) {
        c = svg_resolve_color_keyword(ctx, fill);
    } else {
        c.r = 0; c.g = 0; c.b = 0; c.a = 255;
    }
    float alpha = (float)c.a / 255.0f;

    const char* opacity = get_svg_attr_or_style(ctx, child, "opacity", opacity_buf, sizeof(opacity_buf));
    if (opacity) alpha *= strtof(opacity, nullptr);
    const char* fill_opacity = get_svg_attr_or_style(ctx, child, "fill-opacity",
                                                     fill_opacity_buf, sizeof(fill_opacity_buf));
    if (fill_opacity) alpha *= strtof(fill_opacity, nullptr);

    float lum = (0.2126f * (float)c.r + 0.7152f * (float)c.g + 0.0722f * (float)c.b) / 255.0f;
    float amount = lum * alpha;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    return amount;
}

static float svg_mask_source_alpha_for_child(SvgInlineRenderContext* ctx, Element* child) {
    char opacity_buf[64];
    char fill_opacity_buf[64];
    char fill_buf[256];
    const char* fill = get_svg_attr_or_style(ctx, child, "fill", fill_buf, sizeof(fill_buf));
    if (fill && strcmp(fill, "none") == 0) return 0.0f;

    Color c;
    if (fill) {
        c = svg_resolve_color_keyword(ctx, fill);
    } else {
        c.r = 0; c.g = 0; c.b = 0; c.a = 255;
    }
    float alpha = (float)c.a / 255.0f;
    const char* opacity = get_svg_attr_or_style(ctx, child, "opacity", opacity_buf, sizeof(opacity_buf));
    if (opacity) alpha *= strtof(opacity, nullptr);
    const char* fill_opacity = get_svg_attr_or_style(ctx, child, "fill-opacity",
                                                     fill_opacity_buf, sizeof(fill_opacity_buf));
    if (fill_opacity) alpha *= strtof(fill_opacity, nullptr);
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    return alpha;
}

static bool render_svg_element_with_simple_mask(SvgInlineRenderContext* ctx, Element* elem) {
    if (!ctx || !elem || ctx->suppress_masks) return false;

    char mask_buf[256];
    const char* mask_ref = get_svg_attr_or_style(ctx, elem, "mask", mask_buf, sizeof(mask_buf));
    if (!mask_ref) return false;

    char id_buf[128];
    if (!parse_svg_url_id(mask_ref, id_buf, sizeof(id_buf)) || !ctx->defs) return false;
    Element* mask_elem = lookup_elem_def((SvgDefTable*)ctx->defs, id_buf);
    if (!mask_elem) {
        log_debug("[SVG-MASK] mask ref '%s' not found", id_buf);
        return false;
    }

    bool painted = false;
    bool saved_suppress = ctx->suppress_masks;
    float saved_opacity = ctx->opacity;
    float mask_x = parse_svg_length(get_svg_attr(mask_elem, "x"), 0.0f);
    float mask_y = parse_svg_length(get_svg_attr(mask_elem, "y"), 0.0f);
    float mask_w = parse_svg_length(get_svg_attr(mask_elem, "width"), ctx->current_viewport_w);
    float mask_h = parse_svg_length(get_svg_attr(mask_elem, "height"), ctx->current_viewport_h);
    ctx->suppress_masks = true;

    for (int64_t i = 0; i < mask_elem->length; i++) {
        Element* child = get_child_element_at(mask_elem, i);
        if (!child) continue;
        RdtPath* mask_path = build_path_from_svg_shape(child);
        if (!mask_path) continue;

        float mask_alpha = svg_mask_luminance_for_child(ctx, child);
        if (mask_alpha > 0.0f) {
            RdtMatrix mask_transform = compose_element_transform(ctx, child);
            float ex_x, ex_y, ex_w, ex_h, ex_rx, ex_ry;
            if (!svg_mask_next_opaque_rect(ctx, mask_elem, i + 1,
                                           &ex_x, &ex_y, &ex_w, &ex_h,
                                           &ex_rx, &ex_ry) ||
                !render_svg_masked_source_excluding_rect(ctx, elem, mask_path, &mask_transform,
                                                         saved_opacity * mask_alpha,
                                                         mask_x, mask_y, mask_w, mask_h,
                                                         ex_x, ex_y, ex_w, ex_h,
                                                         ex_rx, ex_ry)) {
                render_svg_masked_source_clip(ctx, elem, mask_path, &mask_transform,
                                              saved_opacity * mask_alpha);
            }
            painted = true;
        }
        rdt_path_free(mask_path);
    }

    ctx->opacity = saved_opacity;
    ctx->suppress_masks = saved_suppress;
    if (painted) {
        log_debug("[SVG-MASK] applied simple luminance mask '%s'", id_buf);
    }
    return painted;
}

// ============================================================================
// Main SVG Element Dispatcher
// ============================================================================

static void render_svg_element(SvgInlineRenderContext* ctx, Element* elem) {
    if (!elem) return;

    const char* tag = get_element_tag_name(elem);
    if (!tag) return;

    char display_buf[64];
    const char* display = get_svg_attr_or_style(ctx, elem, "display", display_buf, sizeof(display_buf));
    if (display && strcmp(display, "none") == 0) {
        log_debug("[SVG] skipping display:none element: %s", tag);
        return;
    }

    log_debug("[SVG] rendering element: %s", tag);

    if (render_svg_element_with_simple_mask(ctx, elem)) {
        return;
    }

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
               strcmp(tag, "pattern") == 0 ||
               strcmp(tag, "marker") == 0) {
        // these are definitions, don't render directly; PDFs may emit them
        // inline inside transformed groups immediately before their users.
        register_svg_def_element(ctx, elem);
    } else if (strcmp(tag, "use") == 0) {
        const char* href = get_svg_attr(elem, "href");
        if (!href) href = get_svg_attr(elem, "xlink:href");
        bool resolved = false;
        if (href && href[0] == '#' && ctx->defs) {
            Element* ref = lookup_elem_def((SvgDefTable*)ctx->defs, href + 1);
            if (ref) {
                render_svg_use_target(ctx, elem, ref, href);
                resolved = true;
            }
        } else if (href && strchr(href, '#')) {
            resolved = render_svg_external_use(ctx, elem, href);
        }
        if (!resolved) log_debug("[SVG] <use> href='%s' not resolved", href ? href : "(none)");
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

static void render_svg_to_display_list_primitives(Element* svg_element, float viewport_width, float viewport_height,
                       Pool* pool, float pixel_ratio, FontContext* font_ctx, const RdtMatrix* base_transform,
                       DisplayList* dl, const Color* initial_current_color, const Color* initial_fill_color,
                       const char* source_path, float initial_opacity, bool initial_fill_none,
                       const Color* initial_stroke_color, bool initial_stroke_none,
                       float initial_stroke_width, PaintList* paint_list) {
    if (!svg_element) return;
    if (!dl || !paint_list) {
        log_error("[SVG] render_svg_to_display_list requires display-list and PaintIR targets");
        return;
    }
    if (source_path && svg_resource_stack_contains(source_path)) {
        log_debug("[SVG] skipped recursive render of SVG resource: %s", source_path);
        return;
    }
    bool pushed_source = svg_resource_stack_push(source_path);

    log_debug("[SVG] render_svg_to_display_list: viewport %.0fx%.0f pixel_ratio=%.2f font_ctx=%p", viewport_width, viewport_height, pixel_ratio, (void*)font_ctx);

    // initialize render context
    SvgInlineRenderContext ctx = {};
    ctx.svg_root = svg_element;
    ctx.pool = pool;
    ctx.font_ctx = font_ctx;
    ctx.dl = dl;
    ctx.paint_list = paint_list;
    ctx.source_path = source_path;
    svg_get_registered_image_resolver(svg_element, &ctx.image_resolver, &ctx.image_resolver_context);
    ctx.pixel_ratio = (pixel_ratio > 0) ? pixel_ratio : 1.0f;
    ctx.fill_color.r = 0; ctx.fill_color.g = 0; ctx.fill_color.b = 0; ctx.fill_color.a = 255;  // default black
    ctx.stroke_color.r = 0; ctx.stroke_color.g = 0; ctx.stroke_color.b = 0; ctx.stroke_color.a = 0;  // default none
    ctx.current_color.r = 0; ctx.current_color.g = 0; ctx.current_color.b = 0; ctx.current_color.a = 255;  // default black
    ctx.fill_none = false;
    ctx.stroke_none = true;
    if (initial_current_color) {
        ctx.current_color = *initial_current_color;
    }
    if (initial_fill_none) {
        ctx.fill_none = true;
    } else if (initial_fill_color) {
        ctx.fill_color = *initial_fill_color;
        ctx.fill_none = false;
    }
    ctx.stroke_width = 1.0f;
    if (!initial_stroke_none && initial_stroke_color) {
        ctx.stroke_color = *initial_stroke_color;
        ctx.stroke_none = false;
    }
    if (initial_stroke_width >= 0.0f) {
        ctx.stroke_width = initial_stroke_width;
    }
    if (initial_opacity < 0.0f) initial_opacity = 0.0f;
    if (initial_opacity > 1.0f) initial_opacity = 1.0f;
    ctx.opacity = initial_opacity;

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

    process_svg_root_resources(&ctx, svg_element);
    svg_apply_inherited_paint_attrs(&ctx, svg_element);

    // render children through the SVG painter gateway
    for (int64_t i = 0; i < svg_element->length; i++) {
        Element* child = get_child_element_at(svg_element, i);
        if (!child) continue;
        render_svg_element(&ctx, child);
    }

    log_debug("[SVG] render_svg_to_display_list complete");
    if (ctx.style_rules) mem_free(ctx.style_rules);
    if (ctx.defs) mem_free(ctx.defs);
    if (pushed_source) svg_resource_stack_pop(source_path);
}

void render_svg_build_subscene(PaintSvgSubscene* subscene,
                      Element* svg_element,
                      float viewport_width, float viewport_height,
                      Pool* pool, float pixel_ratio,
                      FontContext* font_ctx,
                      const RdtMatrix* base_transform,
                      const Bound* content_clip,
                      const Color* initial_current_color,
                      const Color* initial_fill_color,
                      const char* source_path,
                      float initial_opacity,
                      bool initial_fill_none,
                      const Color* initial_stroke_color,
                      bool initial_stroke_none,
                      float initial_stroke_width) {
    if (!subscene) return;
    memset(subscene, 0, sizeof(PaintSvgSubscene));
    subscene->svg_root = svg_element;
    subscene->pool = pool;
    subscene->font_context = font_ctx;
    subscene->viewport_width = viewport_width;
    subscene->viewport_height = viewport_height;
    subscene->pixel_ratio = pixel_ratio > 0.0f ? pixel_ratio : 1.0f;
    subscene->transform = base_transform ? *base_transform : rdt_matrix_identity();
    if (content_clip) {
        subscene->content_clip = *content_clip;
    } else {
        subscene->content_clip = {0.0f, 0.0f, viewport_width, viewport_height};
    }
    subscene->has_color = initial_current_color != nullptr;
    if (initial_current_color) subscene->color = *initial_current_color;
    subscene->has_fill = initial_fill_color != nullptr;
    if (initial_fill_color) subscene->fill = *initial_fill_color;
    subscene->fill_none = initial_fill_none;
    subscene->has_stroke = initial_stroke_color != nullptr;
    if (initial_stroke_color) subscene->stroke = *initial_stroke_color;
    subscene->stroke_none = initial_stroke_none;
    subscene->stroke_width = initial_stroke_width;
    subscene->source_path = source_path;  // RETAINED_FIELD_OK: subscene-local field, not a retained DOM field
    if (initial_opacity < 0.0f) initial_opacity = 0.0f;
    if (initial_opacity > 1.0f) initial_opacity = 1.0f;
    subscene->opacity = initial_opacity;
    subscene->resource_generation = (uint64_t)(uintptr_t)svg_element;
}

static void svg_subscene_indent(StrBuf* out, int indent_level) {
    for (int i = 0; i < indent_level; i++) {
        strbuf_append_str(out, "  ");
    }
}

static void svg_subscene_escape_text(StrBuf* out, const char* s, size_t len) {
    escape_append(out, s, len, ESCAPE_RULES_HTML_TEXT, ESCAPE_RULES_HTML_TEXT_COUNT, ESCAPE_CTRL_NONE);
}

static void svg_subscene_escape_attr(StrBuf* out, const char* s, size_t len) {
    escape_append(out, s, len, ESCAPE_RULES_XML_ATTR, ESCAPE_RULES_XML_ATTR_COUNT, ESCAPE_CTRL_XML_NUMERIC);
}

static bool svg_subscene_attr_name_equals(ShapeEntry* field, const char* name) {
    return field && field->name && field->name->str && name &&
           strlen(name) == field->name->length &&
           strncmp(field->name->str, name, field->name->length) == 0;
}

static int svg_subscene_pdf_image_id_from_href(const char* href) {
    if (!href) return 0;
    const char* prefix = "img:";
    size_t prefix_len = strlen(prefix);
    if (strncmp(href, prefix, prefix_len) != 0) return 0;
    const char* p = href + prefix_len;
    if (!*p) return 0;
    int value = 0;
    while (*p) {
        if (*p < '0' || *p > '9') return 0;
        value = value * 10 + (*p - '0');
        p++;
    }
    return value;
}

static const char* svg_subscene_resolve_image_href(Element* root, const char* href) {
    int object_num = svg_subscene_pdf_image_id_from_href(href);
    if (object_num <= 0) return href;
    SvgImageResolverFn resolver = nullptr;
    void* resolver_context = nullptr;
    if (!svg_get_registered_image_resolver(root, &resolver, &resolver_context) || !resolver) {
        return href;
    }
    const char* resolved = resolver(resolver_context, object_num);
    return resolved ? resolved : href;
}

static void svg_subscene_serialize_image_href(StrBuf* out, Element* root,
                                              const ElementReader& elem) {
    ItemReader href_item = elem.get_attr("href");
    if (!href_item.isString()) return;

    const char* href = href_item.cstring();
    if (!href) return;
    const char* resolved = svg_subscene_resolve_image_href(root, href);
    strbuf_append_str(out, " href=\"");
    svg_subscene_escape_attr(out, resolved, strlen(resolved));
    strbuf_append_char(out, '"');
}

static void svg_subscene_serialize_element(StrBuf* out, Element* root,
                                           const ElementReader& elem) {
    const char* tag = elem.tagName();
    if (!tag) return;

    bool is_image = strcmp(tag, "image") == 0;
    strbuf_append_char(out, '<');
    strbuf_append_str(out, tag);

    const Element* e = elem.element();
    if (e && e->type && e->data) {
        TypeMap* map_type = (TypeMap*)e->type;
        FOR_EACH_MAP_FIELD(map_type, field) {
            if (field->name && field->name->str && field->type) {
                if (svg_subscene_attr_name_equals(field, "data-pdf-root") ||
                    (is_image &&
                     (svg_subscene_attr_name_equals(field, "href") ||
                      svg_subscene_attr_name_equals(field, "xlink:href")))) {
                    continue;
                }

                Item attr_item = map_shape_field_to_item(e->data, field);
                // Computed Lambda SVG attributes often infer as `any`; serialize
                // the runtime value type or dynamic d/stroke/id values disappear.
                TypeId ftype = field->type->type_id;
                if (ftype == LMD_TYPE_ANY) ftype = get_type_id(attr_item);

                if (ftype == LMD_TYPE_STRING) {
                    String* str = attr_item.get_safe_string();
                    if (str) {
                        strbuf_append_char(out, ' ');
                        strbuf_append_str_n(out, field->name->str, field->name->length);
                        strbuf_append_str(out, "=\"");
                        svg_subscene_escape_attr(out, str->chars, str->len);
                        strbuf_append_char(out, '"');
                    }
                } else if (ftype == LMD_TYPE_INT || ftype == LMD_TYPE_INT64 ||
                           ftype == LMD_TYPE_UINT64) {
                    int64_t val = it2i(attr_item);
                    strbuf_append_char(out, ' ');
                    strbuf_append_str_n(out, field->name->str, field->name->length);
                    strbuf_append_format(out, "=\"%" PRId64 "\"", val);
                } else if (ftype == LMD_TYPE_FLOAT || ftype == LMD_TYPE_FLOAT64 ||
                           ftype == LMD_TYPE_NUM_SIZED) {
                    double val = it2d(attr_item);
                    strbuf_append_char(out, ' ');
                    strbuf_append_str_n(out, field->name->str, field->name->length);
                    strbuf_append_format(out, "=\"%g\"", val);
                }
            }
        }
    }

    if (is_image) {
        svg_subscene_serialize_image_href(out, root, elem);
    }

    int64_t count = elem.childCount();
    if (count == 0) {
        strbuf_append_str(out, "/>");
        return;
    }

    strbuf_append_char(out, '>');
    auto children = elem.children();
    ItemReader child;
    while (children.next(&child)) {
        if (child.isString()) {
            String* str = child.asString();
            if (str) {
                svg_subscene_escape_text(out, str->chars, str->len);
            }
        } else if (child.isElement()) {
            ElementReader child_elem(child.item());
            svg_subscene_serialize_element(out, root, child_elem);
        }
    }

    strbuf_append_str(out, "</");
    strbuf_append_str(out, tag);
    strbuf_append_char(out, '>');
}

static bool svg_subscene_matrix_is_identity(const RdtMatrix* matrix) {
    if (!matrix) return true;
    return fabsf(matrix->e11 - 1.0f) < 1e-5f &&
           fabsf(matrix->e12) < 1e-5f &&
           fabsf(matrix->e13) < 1e-5f &&
           fabsf(matrix->e21) < 1e-5f &&
           fabsf(matrix->e22 - 1.0f) < 1e-5f &&
           fabsf(matrix->e23) < 1e-5f;
}

static void svg_subscene_append_color_attr(StrBuf* out, const char* name, Color color) {
    strbuf_append_char(out, ' ');
    strbuf_append_str(out, name);
    strbuf_append_str(out, "=\"");
    if (color.a == 0) {
        strbuf_append_str(out, "transparent");
    } else if (color.a == 255) {
        strbuf_append_format(out, "rgb(%d,%d,%d)", color.r, color.g, color.b);
    } else {
        strbuf_append_format(out, "rgba(%d,%d,%d,%.3f)",
                             color.r, color.g, color.b, color.a / 255.0f);
    }
    strbuf_append_char(out, '"');
}

static bool render_svg_subscene_to_svg(const PaintSvgSubscene* subscene,
                                       StrBuf* out, int indent_level) {
    if (!subscene || !subscene->svg_root || !out) return false;

    svg_subscene_indent(out, indent_level);
    strbuf_append_str(out, "<g");
    if (!svg_subscene_matrix_is_identity(&subscene->transform)) {
        const RdtMatrix* m = &subscene->transform;
        strbuf_append_format(out, " transform=\"matrix(%.6g %.6g %.6g %.6g %.6g %.6g)\"",
                             m->e11, m->e21, m->e12, m->e22, m->e13, m->e23);
    }
    if (subscene->has_color) {
        svg_subscene_append_color_attr(out, "color", subscene->color);
    }
    if (subscene->fill_none) {
        strbuf_append_str(out, " fill=\"none\"");
    } else if (subscene->has_fill) {
        svg_subscene_append_color_attr(out, "fill", subscene->fill);
    }
    if (subscene->stroke_none) {
        strbuf_append_str(out, " stroke=\"none\"");
    } else if (subscene->has_stroke) {
        svg_subscene_append_color_attr(out, "stroke", subscene->stroke);
    }
    if (subscene->stroke_width >= 0.0f) {
        strbuf_append_format(out, " stroke-width=\"%.3f\"", subscene->stroke_width);
    }
    if (subscene->opacity < 0.9995f) {
        strbuf_append_format(out, " opacity=\"%.4f\"", subscene->opacity);
    }
    strbuf_append_str(out, ">\n");

    svg_subscene_indent(out, indent_level + 1);
    ElementReader reader((Element*)subscene->svg_root);
    svg_subscene_serialize_element(out, (Element*)subscene->svg_root, reader);
    strbuf_append_char(out, '\n');

    svg_subscene_indent(out, indent_level);
    strbuf_append_str(out, "</g>\n");
    log_debug("[SVG_SUBSCENE] svg lowering %.1fx%.1f generation=%" PRIu64,
              subscene->viewport_width, subscene->viewport_height,
              subscene->resource_generation);
    return true;
}

static void render_svg_subscene_to_display_list(const PaintSvgSubscene* subscene,
                                                DisplayList* dl) {
    if (!subscene || !subscene->svg_root || !dl) return;

    Pool* temp_pool = mem_pool_create(NULL, MEM_ROLE_RENDER, "render.svg_inline");
    if (!temp_pool) return;
    Arena* temp_arena = mem_arena_create(NULL, temp_pool, MEM_ROLE_RENDER, "render.svg_inline.arena");
    if (!temp_arena) {
        mem_pool_destroy(temp_pool);
        return;
    }

    PaintList nested_paint = {};
    paint_list_init(&nested_paint, temp_arena);

    Color* current_color = subscene->has_color ? (Color*)&subscene->color : nullptr;
    Color* fill_color = subscene->has_fill ? (Color*)&subscene->fill : nullptr;
    Color* stroke_color = subscene->has_stroke ? (Color*)&subscene->stroke : nullptr;
    Pool* render_pool = subscene->pool ? (Pool*)subscene->pool : temp_pool;

    log_debug("[SVG_SUBSCENE] raster lowering %.1fx%.1f generation=%" PRIu64,
              subscene->viewport_width, subscene->viewport_height,
              subscene->resource_generation);
    render_svg_to_display_list_primitives((Element*)subscene->svg_root,
                      subscene->viewport_width,
                      subscene->viewport_height,
                      render_pool,
                      subscene->pixel_ratio,
                      (FontContext*)subscene->font_context,
                      &subscene->transform,
                      dl,
                      current_color,
                      fill_color,
                      subscene->source_path,
                      subscene->opacity,
                      subscene->fill_none,
                      stroke_color,
                      subscene->stroke_none,
                      subscene->stroke_width,
                      &nested_paint);

    paint_list_destroy(&nested_paint);
    arena_destroy(temp_arena);
    mem_pool_destroy(temp_pool);
}

void render_svg_inline_register_paint_ir_lowerers(void) {
    paint_ir_register_svg_subscene_lowerers(render_svg_subscene_to_display_list,
                                            render_svg_subscene_to_svg);
}

static void render_svg_to_display_list(Element* svg_element, float viewport_width, float viewport_height,
                       Pool* pool, float pixel_ratio, FontContext* font_ctx, const RdtMatrix* base_transform,
                       DisplayList* dl, const Color* initial_current_color, const Color* initial_fill_color,
                       const char* source_path, float initial_opacity, bool initial_fill_none,
                       const Color* initial_stroke_color, bool initial_stroke_none,
                       float initial_stroke_width, PaintList* paint_list) {
    if (!svg_element) return;
    if (!dl || !paint_list) {
        log_error("[SVG] render_svg_to_display_list requires display-list and PaintIR targets");
        return;
    }

    render_svg_inline_register_paint_ir_lowerers();
    render_svg_to_display_list_primitives(svg_element,
                                          viewport_width,
                                          viewport_height,
                                          pool,
                                          pixel_ratio,
                                          font_ctx,
                                          base_transform,
                                          dl,
                                          initial_current_color,
                                          initial_fill_color,
                                          source_path,
                                          initial_opacity,
                                          initial_fill_none,
                                          initial_stroke_color,
                                          initial_stroke_none,
                                          initial_stroke_width,
                                          paint_list);
}

void render_svg_to_vec_via_display_list(RdtVector* vec, Element* svg_element,
                       float viewport_width, float viewport_height,
                       Pool* pool, float pixel_ratio, FontContext* font_ctx,
                       const RdtMatrix* base_transform,
                       const Color* initial_current_color,
                       const Color* initial_fill_color,
                       const char* source_path, float initial_opacity,
                       bool initial_fill_none,
                       const Color* initial_stroke_color,
                       bool initial_stroke_none,
                       float initial_stroke_width) {
    if (!vec || !svg_element) return;

    RdtVectorTarget target = {};
    if (!rdt_vector_get_target(vec, &target)) {
        log_error("[SVG] display-list SVG picture render missing vector target");
        return;
    }

    Pool* temp_pool = mem_pool_create(NULL, MEM_ROLE_RENDER, "render.svg_inline");
    if (!temp_pool) return;
    Arena* temp_arena = mem_arena_create(NULL, temp_pool, MEM_ROLE_RENDER, "render.svg_inline.arena");
    if (!temp_arena) {
        mem_pool_destroy(temp_pool);
        return;
    }

    DisplayList dl = {};
    PaintList paint_list = {};
    ScratchArena scratch = {};
    dl_init(&dl, temp_arena);
    paint_list_init(&paint_list, temp_arena);
    mem_scratch_init(NULL, &scratch, temp_arena, MEM_ROLE_RENDER, "render.svg_inline.scratch");

    render_svg_to_display_list(svg_element, viewport_width, viewport_height,
                               pool, pixel_ratio, font_ctx, base_transform, &dl,
                               initial_current_color, initial_fill_color, source_path,
                               initial_opacity, initial_fill_none,
                               initial_stroke_color, initial_stroke_none,
                               initial_stroke_width, &paint_list);

    if (dl_validate_or_log(&dl, "render_svg_picture_display_list")) {
        ImageSurface surface = {};
        surface.format = IMAGE_FORMAT_PNG;
        surface.width = target.width;
        surface.height = target.height;
        surface.pitch = target.stride * 4;
        surface.pixels = target.pixels;
        surface.tile_offset_y = 0;

        if (target.tile_offset_x != 0.0f || target.tile_offset_y != 0.0f) {
            dl_replay_tile(&dl, vec, &surface, &scratch,
                           target.tile_offset_x, target.tile_offset_y,
                           (float)target.width, (float)target.height,
                           pixel_ratio > 0.0f ? pixel_ratio : 1.0f);
        } else {
            Bound clip = {0.0f, 0.0f, (float)target.width, (float)target.height};
            dl_replay(&dl, vec, &surface, &clip, &scratch,
                      pixel_ratio > 0.0f ? pixel_ratio : 1.0f, nullptr);
        }
    }

    scratch_release(&scratch);
    paint_list_destroy(&paint_list);
    dl_destroy(&dl);
    arena_destroy(temp_arena);
    mem_pool_destroy(temp_pool);
}

// ============================================================================
// Render Inline SVG
// ============================================================================

void render_inline_svg(RenderContext* rdcon, ViewBlock* view) {
    if (!rdcon || !view) return;

    DomElement* dom_elem = lam::dom_require_element(lam::view_dom_node(view));
    if (!dom_elem->native_element) {
        log_debug("[SVG] render_inline_svg: no native element");
        return;
    }

    Element* svg_elem = dom_elem->native_element;
    float scale = rdcon->scale;

    log_debug("[SVG] render_inline_svg: view pos=(%.0f,%.0f) size=(%.0f,%.0f) pixel_ratio=%.2f",
              view->x, view->y, view->width, view->height, scale);

    Rect content_rect = render_geometry_block_content_rect(&rdcon->block, view, scale);
    float viewport_width = scale > 0.0f ? content_rect.width / scale : content_rect.width;
    float viewport_height = scale > 0.0f ? content_rect.height / scale : content_rect.height;
    if (viewport_width <= 0.0f || viewport_height <= 0.0f) {
        log_debug("[SVG] render_inline_svg: skipped empty content viewport %.1fx%.1f",
                  viewport_width, viewport_height);
        return;
    }

    log_debug("[SVG] render_inline_svg: doc pos=(%.1f,%.1f) block pos=(%.1f,%.1f) clip=(%.1f,%.1f,%.1f,%.1f)",
              content_rect.x, content_rect.y, rdcon->block.x, rdcon->block.y,
              rdcon->block.clip.left, rdcon->block.clip.top,
              rdcon->block.clip.right, rdcon->block.clip.bottom);

    // build base transform: Translate(x,y) * Scale(scale)
    RdtMatrix base_transform = {
        scale, 0, content_rect.x,
        0, scale, content_rect.y,
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
    bool has_content_clip = content_rect.width > 0.0f && content_rect.height > 0.0f;
    if (has_content_clip) {
        RdtPath* clip_path = rdt_path_new();
        rdt_path_add_rect(clip_path, content_rect.x, content_rect.y,
                          content_rect.width, content_rect.height, 0, 0);
        rc_push_clip(rdcon, clip_path, nullptr);
        rdt_path_free(clip_path);
    }

    // render SVG through the shared painter gateway
    FontContext* font_ctx = rdcon->ui_context ? rdcon->ui_context->font_ctx : nullptr;
    SvgInitialPaint initial_paint;
    render_svg_initial_paint(view, rdcon->color, &initial_paint);
    RenderContext* saved_svg_rdcon = g_svg_active_rdcon;
    g_svg_active_rdcon = rdcon;
    render_svg_to_display_list(svg_elem, viewport_width, viewport_height,
                               rdcon->ui_context->document->pool, scale,
                               font_ctx, &base_transform, rdcon->dl,
                               &initial_paint.current_color,
                               initial_paint.has_fill_color ? &initial_paint.fill_color : nullptr,
                               nullptr, 1.0f, initial_paint.fill_none,
                               initial_paint.has_stroke_color ? &initial_paint.stroke_color : nullptr,
                               initial_paint.stroke_none, initial_paint.stroke_width,
                               rdcon->paint_list);
    g_svg_active_rdcon = saved_svg_rdcon;

    if (has_content_clip) {
        rc_pop_clip(rdcon);
    }
    if (has_clip) {
        rc_pop_clip(rdcon);
    }

    log_debug("[SVG] render_inline_svg: rendered to buffer");
}

void render_custom_svg_subscene(RenderContext* rdcon, Element* svg_element,
                                float viewport_width, float viewport_height) {
    if (!rdcon || !svg_element || viewport_width <= 0.0f || viewport_height <= 0.0f ||
        !rdcon->dl || !rdcon->paint_list) {
        return;
    }
    float scale = rdcon->scale > 0.0f ? rdcon->scale : 1.0f;
    RdtMatrix base_transform = {
        scale, 0.0f, rdcon->block.x,
        0.0f, scale, rdcon->block.y,
        0.0f, 0.0f, 1.0f
    };
    if (rdcon->has_transform) {
        base_transform = rdt_matrix_multiply(&rdcon->transform, &base_transform);
    }
    FontContext* font_ctx = rdcon->ui_context ? rdcon->ui_context->font_ctx : nullptr;
    Pool* pool = (rdcon->ui_context && rdcon->ui_context->document)
        ? rdcon->ui_context->document->pool : nullptr;
    Color current_color = rdcon->color;
    RenderContext* saved_svg_rdcon = g_svg_active_rdcon;
    g_svg_active_rdcon = rdcon;
    render_svg_to_display_list(svg_element, viewport_width, viewport_height,
                               pool, scale, font_ctx, &base_transform, rdcon->dl,
                               &current_color, nullptr, nullptr, 1.0f, false,
                               nullptr, true, -1.0f, rdcon->paint_list);
    g_svg_active_rdcon = saved_svg_rdcon;
}
