/**
 * render_svg_inline.cpp - Inline SVG Rendering via PaintIR/DisplayList
 *
 * Converts SVG element trees to paint/display-list commands with accumulated
 * transforms. No ThorVG scene tree is constructed for the inline path.
 */

#include "render.hpp"
#include "layout.hpp"
#include "radiant.hpp"
#include "svg_animation.hpp"
#include "../lambda/core/mark_reader.hpp"
#include "../lambda/format/format.h"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/css_formatter.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/dom/dom.h"
#include "../lib/tagged.hpp"
#include "../lib/mem_factory.h"
#include "../lib/hashmap.h"
#include "../lib/hashmap_typed.hpp"
#include "../lib/hash.h"
#include "../lib/log.h"
#include "../lib/arena.h"
#include "../lib/font/font.h"
#include "../lib/font/font_internal.h"
#include "../lib/mempool.h"
#include "../lib/mem_grow.hpp"
#include "../lib/math_utils.h"
#include "../lib/str.h"
#include "../lib/utf.h"
#include "../lib/file.h"
#include "../lib/escape.h"
#include "../lib/color.h"
#include <string.h>
#include <limits.h>
#include <float.h>
#include "../lib/mem.h"
#include "../lib/base64.h"
#include <ctype.h>
#include <math.h>
#include <inttypes.h>

#ifndef LAMBDA_HEADLESS
#include <thorvg_capi.h>  // needed for SVG text rendering (tvg_text_* API)
#endif

static const int SVG_RESOURCE_STACK_MAX = 32;
static thread_local const char* g_svg_resource_stack[SVG_RESOURCE_STACK_MAX];
static thread_local int g_svg_resource_stack_depth = 0;
static thread_local RasterRenderContext* g_svg_active_rdcon = nullptr;

struct SvgImageResolverEntry {
    Element* svg_root;
    Item pdf_root;
};

typedef struct SvgImageResolverRegistry {
    HashMap* entries;
    MemContext* context;
    MemNode* registry_node;
} SvgImageResolverRegistry;

static SvgImageResolverRegistry g_svg_image_resolvers = {};

static bool svg_item_number_equals(ItemReader item, int value);
static const char* svg_pdf_registered_image_resolver(Element* context, int object_num);

typedef TypedHashMap<SvgImageResolverEntry,
    HashMapPointerMemberKeyOps<SvgImageResolverEntry, &SvgImageResolverEntry::svg_root>>
    SvgImageResolverMap;

static bool svg_image_resolver_registry_stat(void* allocator, MemStatSample* sample) {
    SvgImageResolverRegistry* registry = (SvgImageResolverRegistry*)allocator;
    if (!registry || !registry->entries || !sample) return false;
    sample->alloc_count = SvgImageResolverMap::count(registry->entries);
    sample->bytes_in_use = sample->alloc_count * sizeof(SvgImageResolverEntry);
    sample->bytes_reserved = sample->bytes_in_use;
    return true;
}

static void svg_image_resolver_registry_destroy(void* allocator) {
    SvgImageResolverRegistry* registry = (SvgImageResolverRegistry*)allocator;
    if (!registry) return;
    HashMap* entries = registry->entries;
    registry->entries = nullptr;
    SvgImageResolverMap::destroy(entries);
}

static bool svg_image_resolver_registry_ensure() {
    if (g_svg_image_resolvers.entries) return true;
    g_svg_image_resolvers.entries = SvgImageResolverMap::create(16);
    if (!g_svg_image_resolvers.entries) return false;
    g_svg_image_resolvers.context = mem_context_create(
        mem_context_root(), MEM_ROLE_RENDER, "render.svg.image_resolvers");
    if (g_svg_image_resolvers.context) {
        g_svg_image_resolvers.registry_node = mem_register(
            g_svg_image_resolvers.context, MEM_KIND_CACHE, MEM_ROLE_RENDER,
            "render.svg.image_resolver_cache", &g_svg_image_resolvers, NULL,
            svg_image_resolver_registry_stat, svg_image_resolver_registry_destroy);
    }
    return true;
}

static SvgImageResolverEntry* svg_find_image_resolver_entry(Element* svg_root) {
    if (!g_svg_image_resolvers.entries || !svg_root) return nullptr;
    SvgImageResolverEntry query = {};
    query.svg_root = svg_root;
    return SvgImageResolverMap::get(g_svg_image_resolvers.entries, query);
}

extern "C" void svg_register_pdf_image_resolver(Element* svg_root, Item pdf_root) {
    if (!svg_root || get_type_id(pdf_root) != LMD_TYPE_MAP) return;

    if (!svg_image_resolver_registry_ensure()) return;
    SvgImageResolverEntry entry = {};
    entry.svg_root = svg_root;
    entry.pdf_root = pdf_root;
    SvgImageResolverMap::set(g_svg_image_resolvers.entries, entry);
    if (SvgImageResolverMap::oom(g_svg_image_resolvers.entries)) {
        log_error("[SVG] failed to register PDF image resolver");
    }
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
    if (!g_svg_image_resolvers.entries) return;
    struct ResolverRemoval {
        Element* svg_root;
        Element* tree_root;
    } removal = { nullptr, root };
    auto find_removal = [](const void* item, void* udata) -> bool {
        const SvgImageResolverEntry* entry = (const SvgImageResolverEntry*)item;
        ResolverRemoval* candidate = (ResolverRemoval*)udata;
        if (svg_element_tree_contains(candidate->tree_root, entry->svg_root)) {
            candidate->svg_root = entry->svg_root;
            return false;
        }
        return true;
    };
    do {
        removal.svg_root = nullptr;
        hashmap_scan(g_svg_image_resolvers.entries, find_removal, &removal);
        if (removal.svg_root) {
            SvgImageResolverEntry query = {};
            query.svg_root = removal.svg_root;
            SvgImageResolverMap::erase(g_svg_image_resolvers.entries, query);
        }
    } while (removal.svg_root);

    if (SvgImageResolverMap::count(g_svg_image_resolvers.entries) == 0) {
        // Resolver registry lifetime follows its last owning DOM tree, so empty
        // registries do not survive into the process leak report.
        MemContext* context = g_svg_image_resolvers.context;
        MemNode* node = g_svg_image_resolvers.registry_node;
        if (context && node) {
            mem_context_destroy(context);
        } else {
            svg_image_resolver_registry_destroy(&g_svg_image_resolvers);
            if (context) mem_context_destroy(context);
        }
        g_svg_image_resolvers.context = nullptr;
        g_svg_image_resolvers.registry_node = nullptr;
    }
}

extern "C" bool svg_get_registered_image_resolver(Element* svg_root,
                                                   SvgImageResolverFn* out_resolver,
                                                   Element** out_context) {
    if (out_resolver) *out_resolver = nullptr;
    if (out_context) *out_context = nullptr;
    SvgImageResolverEntry* entry = svg_find_image_resolver_entry(svg_root);
    if (!entry) return false;
    if (out_resolver) *out_resolver = svg_pdf_registered_image_resolver;
    if (out_context) *out_context = entry->svg_root;
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

static float parse_svg_length(const char* value, float default_value);
static Color parse_svg_color(const char* value);
bool svg_parse_transform(const char* transform_str, float matrix[6]);

// ---------------------------------------------------------------------------
// SVG PaintIR/display-list dispatch helpers
// ---------------------------------------------------------------------------
static inline PaintRecordTarget svg_record_target(SvgInlineRenderContext* ctx) {
    PaintRecordTarget target = {
        ctx ? ctx->paint_list : nullptr,
        ctx ? ctx->dl : nullptr,
        lam::up("SVG")
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
                                   const float* dash, int dash_count, float dash_phase,
                                   const RdtMatrix* xform, float miter_limit = 4.0f) {
    PaintRecordTarget target = svg_record_target(ctx);
    paint_record_stroke_path(&target, "svg_stroke_path", path, color, width,
                             cap, join, dash, dash_count, dash_phase, xform, miter_limit);
}
static inline void svg_fill_linear_gradient(SvgInlineRenderContext* ctx, RdtPath* path,
                                            float x1, float y1, float x2, float y2,
                                            const RdtGradientStop* stops, int count,
                                            RdtFillRule rule, const RdtMatrix* xform,
                                            const RdtMatrix* gradient_xform,
                                            const RdtGradientOptions* options) {
    PaintRecordTarget target = svg_record_target(ctx);
    paint_record_fill_linear_gradient(&target, "svg_fill_linear_gradient",
                                      path, x1, y1, x2, y2,
                                      stops, count, rule, xform, gradient_xform, options);
}
static inline void svg_fill_radial_gradient(SvgInlineRenderContext* ctx, RdtPath* path,
                                            float cx, float cy, float r,
                                            const RdtGradientStop* stops, int count,
                                            RdtFillRule rule, const RdtMatrix* xform,
                                            const RdtMatrix* gradient_xform,
                                            const RdtGradientOptions* options) {
    PaintRecordTarget target = svg_record_target(ctx);
    paint_record_fill_radial_gradient(&target, "svg_fill_radial_gradient",
                                      path, cx, cy, r, stops, count, rule, xform,
                                      gradient_xform, options);
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


typedef void (*SvgElementDrawFn)(SvgInlineRenderContext*, Element*, void*);
static bool svg_render_effect_boundary(SvgInlineRenderContext* ctx, Element* elem,
    SvgElementDrawFn draw, void* data = nullptr, bool frame_ready = false);
static void render_svg_element(SvgInlineRenderContext* ctx, Element* elem);
static void render_svg_basic_shape(SvgInlineRenderContext* ctx, Element* elem);
static void render_svg_path(SvgInlineRenderContext* ctx, Element* elem);
static void render_svg_text(SvgInlineRenderContext* ctx, Element* elem);
static void render_svg_image(SvgInlineRenderContext* ctx, Element* elem);
static void render_svg_children(SvgInlineRenderContext* ctx, Element* elem);
static void render_svg_group(SvgInlineRenderContext* ctx, Element* elem,
    void (*draw_children)(SvgInlineRenderContext*, Element*) = render_svg_children);
static bool svg_element_is_eligible(SvgInlineRenderContext* ctx, Element* elem);
static void render_svg_viewport(SvgInlineRenderContext* ctx, Element* elem, float width, float height, float x, float y);
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

struct SvgStyleContext;
static DomElement* svg_style_node(SvgStyleContext* style, Element* element);
static const char* get_element_tag_name(SvgInlineRenderContext* ctx, Element* element);

// ============================================================================
// Helper: Get attribute from Element
// ============================================================================

static const char* get_svg_attr(Element* elem, const char* name) {
    const char* sampled = name ? svg_animation_source_value(elem, name) : nullptr;
    if (sampled) return sampled;
    const char* value = extract_element_attribute(elem, name, nullptr);
    if (value || !name) return value;

    char lowercase_name[128];
    size_t name_len = strlen(name);
    if (name_len >= sizeof(lowercase_name)) return nullptr;
    bool has_uppercase = false;
    for (size_t i = 0; i < name_len; i++) {
        unsigned char ch = (unsigned char)name[i];
        lowercase_name[i] = (char)tolower(ch);
        has_uppercase = has_uppercase || lowercase_name[i] != name[i];
    }
    lowercase_name[name_len] = '\0';

    // Synthetic SVG elements pass through HTML DOM normalization, which lowercases names.
    return has_uppercase ? extract_element_attribute(elem, lowercase_name, nullptr) : nullptr;
}

static bool svg_export_semantic_attribute(const char* name) {
    if (!name || (strncmp(name, "data-", 5) != 0 && strcmp(name, "role") != 0 &&
        strcmp(name, "aria-label") != 0 && strcmp(name, "x") != 0 && strcmp(name, "y") != 0 &&
        strcmp(name, "width") != 0 && strcmp(name, "height") != 0)) return false;
    // semantic names remain XML attributes; authored paint, script and resource links are resolved elsewhere.
    for (const unsigned char* ch = (const unsigned char*)name; *ch; ch++)
        if (!isalnum(*ch) && *ch != '-' && *ch != '_' && *ch != '.') return false;
    return true;
}

static int svg_export_begin_element(SvgInlineRenderContext* ctx, Element* elem) {
    if (!ctx->semantic_target || ctx->semantic_target != ctx->dl) return -1;
    ElementReader reader(elem);
    lam::ArrayList<RenderSemanticAttribute> attrs;
    lam::ArrayList<StringBuf*> values;
    auto it = reader.attrs();
    const char* name;
    ItemReader value;
    while (it.next(&name, &value)) {
        if (!svg_export_semantic_attribute(name) || value.isNull()) continue;
        StringBuf* text = stringbuf_new(ctx->pool);
        if (!text) continue;
        if (value.isString()) {
            String* string = value.asString();
            stringbuf_append_str_n(text, string->chars, string->len);
        } else if (value.isSymbol()) {
            Symbol* symbol = value.asSymbol();
            stringbuf_append_str_n(text, symbol->chars, symbol->len);
        } else if (value.isNumber()) {
            format_number(text, value.item());
        } else if (value.isBool()) {
            stringbuf_append_str(text, value.asBool() ? "true" : "false");
        }
        if (!values.append(text)) { stringbuf_free(text); break; }
        attrs.append({lam::up(name), lam::up(text->str->chars)});
    }
    const char* tag = get_element_tag_name(ctx, elem);
    StringBuf* title = nullptr;
    if (tag && strcmp(tag, "text") == 0) {
        title = stringbuf_new(ctx->pool);
        if (title) reader.textContent(title);
    }
    int begin = -1;
    if (attrs.size() || (title && title->length)) {
        RenderSemanticGroup group = {lam::up(attrs.data()), (int)attrs.size(), // INT_CAST_OK: attribute count is bounded by the element shape.
            lam::up(title && title->length ? title->str->chars : nullptr)};
        RenderSemanticGroup snapshot = {};
        if (dl_copy_semantic_group(ctx->dl, &snapshot, &group)) {
            begin = dl_begin_element(ctx->dl, 0, 0, 0, 0, 0);
            if (begin >= 0) ctx->dl->data()[begin].element_marker.semantics = snapshot;
        }
    }
    for (StringBuf* text : values) stringbuf_free(text);
    if (title) stringbuf_free(title);
    return begin;
}

// numeric Items from Lambda-built SVGs are invisible to the string accessor.
static bool read_svg_number_attr(Element* elem, const char* name, float* result,
    const SvgLengthContext* lengths = nullptr, SvgLengthAxis axis = SVG_LENGTH_DIAGONAL) {
    if (!elem || !name || !result) return false;
    const char* sampled = svg_animation_source_value(elem, name);
    if (sampled) {
        float number = lengths ? svg_resolve_length(sampled, lengths, axis, NAN) : strtof(sampled, nullptr);
        if (!isfinite(number)) return false;
        *result = number; return true;
    }
    ConstItem value = elem->get_attr(name);
    Item item = {.item = value.item};
    double number = 0.0;
    if (item_try_to_double(item, &number)) {
        float value = (float)number;
        if (!isfinite(value)) return false;
        *result = value;
        return true;
    }
    const char* text = get_svg_attr(elem, name);
    if (!text || !*text) return false;
    // invalid attributes retain the caller's default instead of inventing zero.
    float length = lengths ? svg_resolve_length(text, lengths, axis, NAN)
        : parse_svg_length(text, NAN);
    if (!isfinite(length)) return false;
    *result = length;
    return true;
}

static float get_svg_number_attr(Element* elem, const char* name, float fallback,
    const SvgLengthContext* lengths = nullptr, SvgLengthAxis axis = SVG_LENGTH_DIAGONAL) {
    float result = fallback;
    read_svg_number_attr(elem, name, &result, lengths, axis);
    return result;
}



static void svg_preserve_aspect_alignment(const char* value,
                                          float* align_x, float* align_y) {
    *align_x = 0.5f;
    *align_y = 0.5f;
    if (!value) return;
    if (strstr(value, "xMin")) *align_x = 0.0f;
    else if (strstr(value, "xMax")) *align_x = 1.0f;
    if (strstr(value, "YMin")) *align_y = 0.0f;
    else if (strstr(value, "YMax")) *align_y = 1.0f;
}

struct SvgStyleProperty {
    const char* name;
    const char* value;
    uint64_t animation_generation;
    bool from_css;
    bool inherits;
    DomElement* declaring_element;
    SvgStyleProperty* next;
};

struct SvgStyleEntry {
    Element* element;
    DomElement* node;
    CssDeclaration** inline_declarations;
    size_t inline_count;
    bool inline_parsed;
    SvgStyleProperty* properties;
    SvgStyleProperty* computed_properties;
    RdtPath* text_hit_path;
    FontContext* text_hit_context;
    SvgLengthContext text_hit_lengths;
    uint64_t text_hit_animation, text_hit_fonts;
};

typedef TypedHashMap<SvgStyleEntry,
    HashMapPointerMemberKeyOps<SvgStyleEntry, &SvgStyleEntry::element>> SvgStyleMap;

struct SvgResourceDocument;

struct SvgStyleContext {
    Pool* pool;
    DomDocument* document;
    DomDocument* isolated_document;
    CssEngine* engine;
    SelectorMatcher* matcher;
    HashMap* entries;
    SvgStyleContext* resource_owner;
    SvgResourceDocument* resource_document;
    SvgResourceDocument* resources;
    lam::Own<UiContext> isolated_ui;   // headless shell for isolated HTML layout
    ImageSurface* isolated_surface;
};

struct SvgResourceDocument {
    lam::Own<char> path;
    RdtPicture* picture;
    SvgStyleContext style;
    FontContext* fonts;
    SvgResourceDocument* next;
};

static SvgStyleEntry* svg_style_entry_lookup(SvgStyleContext* style, Element* element) {
    SvgStyleEntry key = {}; key.element = element;
    return style && style->entries ? SvgStyleMap::get(style->entries, key) : nullptr;
}

static DomElement* svg_style_node(SvgStyleContext* style, Element* element) {
    SvgStyleEntry* entry = svg_style_entry_lookup(style, element);
    return entry ? entry->node : nullptr;
}

static const char* get_element_tag_name(SvgInlineRenderContext* ctx, Element* element) {
    DomElement* node = ctx ? svg_style_node((SvgStyleContext*)ctx->style_context, element) : nullptr;
    // expanded names distinguish qualified SVG from identically named foreign content.
    return node && dom_element_is_svg(node) ? node->local_name() : nullptr;
}

static const char* get_svg_href(SvgInlineRenderContext* ctx, Element* element) {
    const char* href = get_svg_attr(element, "href");
    if (href) return href;
    // a referenced document owns its XLink binding and animation registry, independently of its prefix.
    DomElement* node = ctx ? svg_style_node((SvgStyleContext*)ctx->style_context, element) : nullptr;
    return node ? svg_animation_attribute(node, "xlink:href") : nullptr;
}

static void svg_resource_document_destroy(SvgResourceDocument* document);

DomElement* build_dom_tree_from_element(Element* elem, DomDocument* doc, DomElement* parent);
static const char* get_direct_text_content(Element* elem);

static void svg_style_index_tree(SvgStyleContext* style, DomElement* node) {
    if (!node) return;
    if (style->isolated_document && node->tag_name) {
        const char* prefix = strchr(node->tag_name, ':');
        const char* uri = prefix ? dom_element_namespace_uri(node) : nullptr;
        // private XML HTML layout uses local tag identity while retaining the authored qualified name.
        if (uri && strcmp(uri, "http://www.w3.org/1999/xhtml") == 0)
            node->tag_id = DomNode::tag_name_to_id(node->local_name());
    }
    SvgStyleEntry entry = {};
    entry.element = dom_element_to_element(node);
    entry.node = node;
    SvgStyleMap::set(style->entries, entry);
    // parsed images retain source Elements; inline UI inputs may use the embedded one.
    Element* source = dom_element_render_source(node);
    if (source && source != entry.element) {
        entry.element = source;
        SvgStyleMap::set(style->entries, entry);
    }
    for (DomNode* child = node->first_child; child; child = child->next_sibling) {
        if (child->is_element()) svg_style_index_tree(style, child->as_element());
    }
}

static void svg_style_collect_sheets(SvgStyleContext* style, Element* elem) {
    if (!elem) return;
    DomElement* node = svg_style_node(style, elem);
    const char* tag = node ? node->local_name() : nullptr;
    const char* uri = node ? dom_element_namespace_uri(node) : "";
    if (tag && strcmp(tag, "style") == 0 &&
        (!strcmp(uri, "http://www.w3.org/2000/svg") || !strcmp(uri, "http://www.w3.org/1999/xhtml"))) {
        lam::Temp<char> text((char*)get_direct_text_content(elem));
        const char* type = get_svg_attr(elem, "type");
        if (text && (!type || str_icmp_cstr(type, "text/css") == 0)) {
            CssStylesheet* sheet = css_parse_stylesheet(style->engine, text.get(),
                style->document->url ? url_get_href(style->document->url) : nullptr);
            DomDocument* doc = style->document;
            if (sheet && lam::pool_copy_grow_array(doc->document_pool, &doc->stylesheets,
                &doc->stylesheet_capacity, doc->stylesheet_count,
                doc->stylesheet_count + 1, 4, true)) {
                const char* media = get_svg_attr(elem, "media");
                sheet->disabled = media && !css_evaluate_media_query(style->engine, media);
                doc->stylesheets[doc->stylesheet_count++] = sheet;
            }
        }
        return;
    }
    for (int64_t i = 0; i < elem->length; i++) {
        if (get_type_id(elem->items[i]) == LMD_TYPE_ELEMENT)
            svg_style_collect_sheets(style, elem->items[i].element);
    }
}

static void svg_style_destroy(SvgStyleContext* style) {
    if (!style) return;
    // D4.2: external template documents outlive all borrowed resolution facts in this walk.
    for (SvgResourceDocument* resource = style->resources; resource;) {
        SvgResourceDocument* next = resource->next;
        svg_resource_document_destroy(resource);
        resource = next;
    }
    if (style->matcher) selector_matcher_destroy(style->matcher);
    size_t cursor = 0; SvgStyleEntry* entry = nullptr;
    while (SvgStyleMap::next(style->entries, &cursor, &entry))
        if (entry->text_hit_path) rdt_path_free(entry->text_hit_path);
    SvgStyleMap::destroy(style->entries);
    if (style->isolated_document) {
        // D4.2.6: layout payloads release borrowed fonts/images before their private document owner.
        view_tree_shell_destroy(style->isolated_document, style->isolated_document->view_tree);
        if (style->isolated_ui) image_cache_cleanup(style->isolated_ui);
        if (style->isolated_surface) image_surface_destroy(style->isolated_surface);
        lam::free_owned(style->isolated_ui);
        if (style->isolated_document->url) url_destroy(style->isolated_document->url);
        dom_document_destroy(style->isolated_document);
    }
    if (style->pool) mem_pool_destroy(style->pool);
    *style = {};
}

static void svg_resource_document_destroy(SvgResourceDocument* document) {
    if (!document) return;
    // the destroy call hands the resource document over
    lam::Temp<SvgResourceDocument> owned(document);
    svg_style_destroy(&document->style);
    if (document->fonts) font_context_destroy(document->fonts);
    rdt_picture_free(document->picture); lam::free_owned(document->path);
}

static bool svg_style_begin(SvgStyleContext* style) {
    *style = {};
    style->resource_owner = style;
    style->pool = mem_pool_create(nullptr, MEM_ROLE_RENDER, "render.svg.styles");
    if (!style->pool) return false;
    style->entries = SvgStyleMap::create(64);
    return true;
}

// shared tail: a matcher and the element index of the document being styled
static bool svg_style_finish(SvgStyleContext* style, DocState* host_state) {
    style->matcher = selector_matcher_create(style->pool);
    if (!style->entries || !style->matcher) { svg_style_destroy(style); return false; }
    if (host_state) state_configure_selector_matcher(host_state, style->matcher);
    svg_style_index_tree(style, style->document->root);
    return true;
}

// Inline SVG content is styled against its live host document.
static bool svg_style_init_host(SvgStyleContext* style, DomDocument* host) {
    if (!host || !host->root || !svg_style_begin(style)) return false;
    style->document = host;
    style->engine = (CssEngine*)host->services.cached_css_engine;
    return svg_style_finish(style, (DocState*)host->state);
}

static bool svg_style_init(SvgStyleContext* style, Element* root,
                            float viewport_width, float viewport_height, DomDocument* host = nullptr,
                            const char* source_path = nullptr, bool image_document = false) {
    if (!host && g_svg_active_rdcon && g_svg_active_rdcon->ui_context)
        host = g_svg_active_rdcon->ui_context->document;
    if (host && dom_find_element_for_source(host->root, root)) return svg_style_init_host(style, host);
    if (!svg_style_begin(style)) return false;
    // isolated image/use documents borrow the parsed SVG only for this walk;
    // their CSS/DOM metadata has its own owner and cannot leak host selectors.
    Input* input = Input::create(style->pool);
    style->isolated_document = input ? dom_document_create(input) : nullptr;
    style->document = style->isolated_document;
    if (!style->document) { svg_style_destroy(style); return false; }
    style->document->resource_policy = host ? host->resource_policy : INPUT_RESOURCE_ALLOW_NETWORK;
    dom_document_borrow_input_resources(style->document);
    style->document->services.svg_image_document = image_document;
    if (source_path && *source_path) style->document->url = lam::own(url_parse_path_or_url(source_path, nullptr));
    DomElement* node = build_dom_tree_from_element(root, style->document, nullptr);
    if (node && strcmp(node->local_name(), "svg")) {
        // generated fragments inherit SVG membership from an implicit viewport; authored declarations still win.
        DomElement* viewport = DomElement::create(style->document, "svg", nullptr);
        if (viewport) viewport->link_child(node);
        style->document->root = lam::up(viewport);
    } else style->document->root = lam::up(node);
    style->engine = css_engine_create(style->document->document_pool);
    if (!style->document->root || !style->engine) { svg_style_destroy(style); return false; }
    style->document->services.cached_css_engine = style->engine;
    css_engine_set_viewport(style->engine, viewport_width, viewport_height);
    if (!svg_style_finish(style, nullptr)) return false;
    // stylesheet discovery needs the same expanded names as paint and resource dispatch.
    svg_style_collect_sheets(style, root);
    return true;
}

// painting and DOM geometry queries share the host cascade. Rebuilding a
// matcher and reparsing inline CSS per glyph property made SVG hit testing
// dominate scrolling. Invalidation also covers queries between paint passes.
struct SvgPaintHostStyle : DomDocumentResourceData {
    uint64_t pass;
    uint64_t mutation_epoch;
    uint64_t state_version;
    uint64_t style_epoch;
    uint64_t query_epoch;
    uint64_t animation_generation;
    uint64_t font_generation;
    uint32_t layout_generation;
    bool valid;
    bool animation_prepared;
    SvgStyleContext style;
};

static uint64_t g_svg_paint_pass;

void render_svg_begin_paint_pass(void) { g_svg_paint_pass++; }

static void svg_paint_host_style_destroy(DomDocumentResourceData* data) {
    auto* shared = static_cast<SvgPaintHostStyle*>(data);
    svg_style_destroy(&shared->style);
    mem_free(shared);
}

static SvgPaintHostStyle* svg_host_style(DomDocument* host) {
    if (!host || !host->root) return nullptr;
    SvgPaintHostStyle* shared = nullptr;
    for (DomDocumentResource* resource = host->resources; resource; resource = resource->next)
        if (resource->destroy == svg_paint_host_style_destroy) {
            shared = static_cast<SvgPaintHostStyle*>(resource->data);
            break;
        }
    if (!shared) {
        shared = (SvgPaintHostStyle*)mem_calloc(1, sizeof(SvgPaintHostStyle), MEM_CAT_RENDER);
        if (!shared) return nullptr;
        if (!dom_document_add_resource(host, shared, svg_paint_host_style_destroy)) {
            mem_free(shared);
            return nullptr;
        }
    }
    uint64_t state_version = doc_state_selector_version((DocState*)host->state);
    uint64_t animation_generation = svg_animation_generation(host);
    UiContext* ui = (UiContext*)host->js.host_ui_context;
    uint64_t font_generation = ui ? font_context_resource_generation(ui->font_ctx) : 0;
    bool new_pass = shared->pass != g_svg_paint_pass;
    uint32_t layout_generation = host->view_tree ? host->view_tree->layout_generation : 0;
    // scroll/repaint flags do not change CSS. Animation/resource snapshots still
    // expire at paint boundaries, before any traversal borrows their values.
    if (!shared->valid || (new_pass && (shared->animation_generation != animation_generation ||
        shared->font_generation != font_generation || shared->style.resources)) ||
        shared->mutation_epoch != host->mutation_epoch ||
        (shared->state_version != state_version && shared->style.matcher && shared->style.matcher->depends_on_state) ||
        shared->style_epoch != host->style_content_epoch || shared->query_epoch != host->style_query_epoch ||
        shared->layout_generation != layout_generation) {
        svg_style_destroy(&shared->style);
        shared->valid = svg_style_init_host(&shared->style, host);
        shared->mutation_epoch = host->mutation_epoch;
        shared->state_version = state_version;
        shared->style_epoch = host->style_content_epoch;
        shared->query_epoch = host->style_query_epoch;
        shared->layout_generation = layout_generation;
        shared->animation_generation = animation_generation;
        shared->font_generation = font_generation;
        shared->animation_prepared = false;
        if (!shared->valid) return nullptr;
    }
    if (new_pass) shared->animation_prepared = false;
    // state-independent values remain valid; any newly queried pseudo-class
    // observes this state and makes the next interaction expire the snapshot.
    shared->state_version = state_version;
    shared->pass = g_svg_paint_pass;
    return shared;
}

static SvgStyleContext* svg_paint_host_style(Element* svg_element) {
    DomDocument* host = g_svg_paint_pass && g_svg_active_rdcon && g_svg_active_rdcon->ui_context
        ? g_svg_active_rdcon->ui_context->document : nullptr;
    SvgPaintHostStyle* shared = svg_element ? svg_host_style(host) : nullptr;
    if (!shared) return nullptr;
    SvgStyleEntry query = {};
    query.element = svg_element;
    if (!SvgStyleMap::get(shared->style.entries, query)) return nullptr;
    if (!shared->animation_prepared) {
        svg_animation_prepare(host->root);
        shared->animation_prepared = true;
    }
    return &shared->style;
}

bool render_svg_picture_has_animation(RdtPicture* picture) {
    Element* root = rdt_picture_get_svg_root(picture);
    if (!root) return false;
    float width = 0, height = 0; rdt_picture_get_size(picture, &width, &height);
    SvgStyleContext style = {};
    if (!svg_style_init(&style, root, width, height, nullptr, rdt_picture_get_source_path(picture), true)) return false;
    bool animated = style.document && svg_animation_has_elements(style.document->root);
    svg_style_destroy(&style);
    return animated;
}

static FontContext* svg_style_font_context(SvgStyleContext* style, const char* source_path,
    float raster_scale, bool image_document, FontContext* parent_fonts) {
    if (!style || !style->document) return nullptr;
    FontContextConfig config = {}; config.pixel_ratio = raster_scale;
    FontContext* fonts = font_context_create(&config);
    if (!fonts) return nullptr;
    // Isolated SVG images need the page's explicit font directories for authored families.
    if (parent_fonts && parent_fonts->explicit_font_directories) {
        ArrayList* directories = parent_fonts->explicit_font_directories;
        for (int i = 0; i < directories->length; i++) {
            const char* directory = (const char*)directories->data[i];
            if (directory) font_context_add_scan_directory(fonts, directory);
        }
    }
    // D4: registration copies descriptors; the temporary UI bridge owns only its CSS metadata.
    UiContext bridge = {}; bridge.font_ctx = lam::up(fonts);
    for (int i = 0; i < style->document->stylesheet_count; i++) {
        CssStylesheet* sheet = style->document->stylesheets[i];
        if (sheet && !sheet->disabled) process_font_face_rules_from_stylesheet(&bridge, sheet, source_path, image_document);
    }
    fontface_cleanup(&bridge);
    return fonts;
}

static SvgStyleEntry* svg_style_entry(SvgStyleContext* style, Element* element) {
    SvgStyleEntry* entry = svg_style_entry_lookup(style, element);
    if (entry && !entry->inline_parsed) {
        // each SVG indexes the host for references; unrelated inline CSS is never queried.
        const char* text = entry->node->get_attribute("style");
        if (text) entry->inline_declarations = css_parse_declaration_list_text(
            text, strlen(text), style->pool, &entry->inline_count);
        entry->inline_parsed = true;
    }
    return entry;
}

CssValue* svg_parse_property_value(Pool* pool, const char* text, const char* name) {
    if (!text) return nullptr;
    StrBuf* source = strbuf_new();
    if (!name || !source) { if (source) strbuf_free(source); return nullptr; }
    strbuf_append_str(source, name);
    strbuf_append_char(source, ':');
    strbuf_append_str(source, text);
    size_t count = 0;
    CssDeclaration** declarations = css_parse_declaration_list_text(source->str,
        (size_t)source->length, pool, &count);
    CssValue* result = count && declarations ? declarations[0]->value : nullptr;
    strbuf_free(source);
    return result;
}

typedef struct {
    SvgStyleContext* style;
    const SvgDomStyleScope* scope = nullptr;
} SvgVariableContext;

static const CssValue* svg_lookup_variable(void* context, DomElement* element,
                                           const char* name, DomElement** owner) {
    if (owner) *owner = nullptr;
    SvgVariableContext* variables = (SvgVariableContext*)context;
    SvgStyleContext* style = variables->style;
    DomDocument* doc = style->document;
    if (strncmp(name, "--", 2) != 0) {
        // the dedicated var parser omits dashes; declaration queries require the full name.
        size_t length = strlen(name);
        char* qualified = (char*)pool_alloc(style->pool, length + 3);
        if (!qualified) return nullptr;
        qualified[0] = qualified[1] = '-';
        memcpy(qualified + 2, name, length + 1);
        name = qualified;
    }
    for (DomNode* node = element; node && node->is_element();
        node = svg_dom_style_parent(node, variables->scope)) {
        SvgStyleEntry* entry = svg_style_entry(style, dom_element_to_element(node->as_element()));
        DomElement* current = node->as_element();
        size_t inline_count = entry ? entry->inline_count : 0;
        const char* inline_text = entry ? nullptr : current->get_attribute("style");
        CssDeclaration** inline_declarations = entry ? entry->inline_declarations : inline_text
            ? css_parse_declaration_list_text(inline_text, strlen(inline_text), style->pool, &inline_count)
            : nullptr;
        CssDeclaration declaration = {};
        if (css_select_element_declaration(style->engine, style->matcher, current,
            doc->stylesheets, (size_t)doc->stylesheet_count, inline_declarations,
            inline_count, name, &declaration)) {
            CssEnum keyword = declaration.value && declaration.value->type == CSS_VALUE_TYPE_KEYWORD
                ? declaration.value->data.keyword : CSS_VALUE_NONE;
            if (keyword == CSS_VALUE_INITIAL) return nullptr;
            if (keyword == CSS_VALUE_INHERIT || keyword == CSS_VALUE_UNSET) continue;
            if (owner) *owner = current;
            return declaration.value;
        }
    }
    return nullptr;
}

static const char* svg_resolve_property_declaration(SvgStyleContext* style, DomElement* element,
    const char* name, CssDeclaration* declaration, const SvgDomStyleScope* scope = nullptr,
    bool compute_lengths = true) {
    SvgVariableContext variables = {style, scope};
    const CssValue* authored = declaration->value;
    declaration->value = (CssValue*)css_resolve_var_value(
        style->pool, authored, svg_lookup_variable, &variables, element);
    // layout, DOM geometry and painting must all project the same resolved CSS tokens.
    if (declaration->value != authored) {
        declaration->value_text = nullptr; declaration->value_text_len = 0;
    }
    if (declaration->property_name && strcmp(declaration->property_name, "font") == 0 &&
        css_font_shorthand_contains_property(name)) {
        declaration->value = (CssValue*)css_font_shorthand_longhand(declaration->value, name, style->pool);
        declaration->value_text = nullptr; declaration->value_text_len = 0;
    }
    CssPropertyCode property = css_property_code_from_name(name);
    // invalid winning substitutions default as a unit; a losing declaration cannot become visible.
    if (css_property_is_svg_presentation(property) &&
        !css_property_validate_value(property, declaration->value)) return nullptr;
    // only validated paint lists contain a URL head; other properties can substitute an empty list.
    const CssValue* paint = css_property_is_svg_resource(property) && declaration->value &&
        declaration->value->type == CSS_VALUE_TYPE_LIST
        ? declaration->value->data.list.values[0] : declaration->value;
    if (css_property_is_svg_resource(property) && paint && paint->type == CSS_VALUE_TYPE_URL) {
        Url* base = declaration->source_file ? url_parse_path_or_url(declaration->source_file,
            style->document ? style->document->url.get() : nullptr) : nullptr;
        const char* address = radiant_resolve_css_url(style->pool, paint->data.url,
            base ? base : style->document ? style->document->url.get() : nullptr);
        if (base) url_destroy(base);
        if (!address) return nullptr;
        if (address != paint->data.url) {
            // URL computation must not rewrite the retained authored declaration or its shared value tree.
            declaration->value = css_value_clone_owned(declaration->value, style->pool);
            if (!declaration->value) return nullptr;
            CssValue* computed_paint = declaration->value->type == CSS_VALUE_TYPE_LIST
                ? declaration->value->data.list.values[0] : declaration->value;
            computed_paint->data.url = address;
        }
        // loader-normalized canonical URLs also take precedence over authored relative text.
        declaration->value_text = nullptr; declaration->value_text_len = 0;
    }
    if (css_property_is_svg_presentation(property) && !css_property_is_svg_paint(property) &&
        (!css_property_is_svg_length(property) || compute_lengths)) {
        // compute at the declaring element so inherited lengths and opacity retain their computed basis.
        String* computed = css_prop_serialize_svg_value(style->pool, element, property, declaration->value);
        return computed ? computed->chars : nullptr;
    }
    if (css_property_is_svg_length(property) && !compute_lengths) {
        // cached source values bind to the existing instance font context, not the definition's DOM ancestry.
        declaration->value_text = nullptr; declaration->value_text_len = 0;
    }
    return declaration->value ? css_serialize_declaration_value(declaration, style->pool) : nullptr;
}

static const char* svg_resolve_attribute_variables(SvgStyleContext* style, DomElement* element,
    const char* name, const char* value, const SvgDomStyleScope* scope = nullptr,
    bool compute_lengths = true) {
    if (!value || (!strstr(value, "var(") &&
        !css_property_is_svg_presentation(css_property_code_from_name(name)))) return value;
    CssDeclaration declaration = {};
    declaration.value = svg_parse_property_value(style->pool, value, name);
    return svg_resolve_property_declaration(style, element, name, &declaration, scope, compute_lengths);
}

struct SvgPropertyDefault { const char* name; const char* initial; bool inherits; };
static const SvgPropertyDefault svg_property_defaults[] = {
        {"mask-type", "luminance", false},
        {"clip-path", "none", false}, {"mask", "none", false}, {"filter", "none", false},
    };

const char* svg_property_initial(const char* name, bool* inherits) {
    const CssProperty* property = css_property_get_by_name(name);
    if (property) { *inherits = property->inheritance == PROP_INHERIT_YES; return property->initial_value; }
    for (const SvgPropertyDefault& entry : svg_property_defaults) if (strcmp(name, entry.name) == 0) {
        *inherits = entry.inherits; return entry.initial;
    }
    *inherits = strncmp(name, "--", 2) == 0;
    return nullptr;
}

static const char* svg_style_property_value(SvgInlineRenderContext* ctx, Element* elem,
                                            const char* name) {
    SvgStyleContext* style = ctx ? (SvgStyleContext*)ctx->style_context : nullptr;
    SvgStyleEntry* entry = svg_style_entry(style, elem);
    if (!entry) return get_svg_attr(elem, name);
    uint64_t generation = svg_animation_source_generation(style->document, elem, entry->node);
    for (SvgStyleProperty* prop = entry->properties; prop; prop = prop->next) {
        if (strcmp(prop->name, name) == 0 && prop->animation_generation == generation) return prop->value;
    }
    SvgStyleProperty* prop = (SvgStyleProperty*)pool_calloc(style->pool, sizeof(SvgStyleProperty));
    if (!prop) return get_svg_attr(elem, name);
    prop->name = pool_strdup(style->pool, name);
    prop->animation_generation = generation;
    CssDeclaration declaration = {};
    DomDocument* doc = style->document;
    bool selected = css_select_element_declaration(style->engine, style->matcher, entry->node,
        doc->stylesheets, (size_t)doc->stylesheet_count, entry->inline_declarations,
        entry->inline_count, name, &declaration);
    if (selected) {
        prop->value = svg_resolve_property_declaration(style, entry->node, name, &declaration, nullptr,
            !dom_element_is_svg(entry->node));
        prop->from_css = true;
    } else {
        // css shorthands have no SVG presentation-attribute form.
        prop->value = dom_element_is_svg(entry->node) &&
            !css_property_is_identity_shorthand(css_property_code_from_name(name)) ? get_svg_attr(elem, name) : nullptr;
        prop->value = svg_resolve_attribute_variables(style, entry->node, name, prop->value, nullptr, false);
    }
    if (!declaration.important) {
        const char* animated = svg_animation_value(entry->node, name, true);
        if (animated) {
            prop->value = animated;
            prop->from_css = strcmp(name, "transform") != 0;
        }
    }
    bool inherits = false;
    const char* initial = svg_property_initial(name, &inherits);
    if (prop->value && (str_icmp_cstr(prop->value, "initial") == 0 ||
        (str_icmp_cstr(prop->value, "unset") == 0 && !inherits))) prop->value = initial;
    else if (prop->value && (str_icmp_cstr(prop->value, "inherit") == 0 || str_icmp_cstr(prop->value, "unset") == 0)) {
        // inherited properties follow the instance traversal; explicit inherit also applies to non-inherited properties.
        DomNode* parent = entry->node->parent;
        prop->value = inherits ? nullptr : parent && parent->is_element()
            ? svg_style_property_value(ctx, dom_element_to_element(parent->as_element()), name) : initial;
        if (!inherits && !prop->value) prop->value = initial;
    }
    CssPropertyCode property = css_property_code_from_name(name);
    DomNode* parent = entry->node->parent;
    if (!prop->value && inherits && css_property_is_svg_presentation(property) &&
        !css_property_is_svg_paint(property) && parent && parent->is_element() &&
        !dom_element_is_svg(parent->as_element())) {
        // the SVG traversal inherits internally; seed its root from surrounding HTML CSS, ignoring HTML attributes.
        prop->value = svg_style_property_value(ctx, dom_element_to_element(parent->as_element()), name);
    }
    // D4.5.1v4: another use instance can replace the document's borrowed sample pool in this walk.
    if (generation && prop->value) prop->value = pool_strdup(style->pool, prop->value);
    prop->next = entry->properties;
    entry->properties = prop;
    return prop->value;
}

static const char* svg_style_ancestor_property_value(SvgInlineRenderContext* ctx,
    Element* elem, const char* name) {
    SvgStyleContext* style = ctx ? (SvgStyleContext*)ctx->style_context : nullptr;
    SvgStyleEntry* entry = svg_style_entry(style, elem);
    for (DomNode* node = entry ? entry->node : nullptr; node && node->is_element(); node = node->parent) {
        const char* value = svg_style_property_value(ctx, dom_element_to_element(node->as_element()), name);
        if (value && str_icmp_cstr(value, "currentColor") != 0) return value;
    }
    return nullptr;
}

static Element* svg_find_element_id(SvgInlineRenderContext* ctx, const char* id) {
    SvgStyleContext* style = ctx ? (SvgStyleContext*)ctx->style_context : nullptr;
    DomElement* node = style && style->document
        ? dom_find_element_by_id(style->document->root, id) : nullptr;
    // live DOM links retain non-layout definitions even when a synthetic
    // document wrapper has no corresponding source Element children.
    return node ? dom_element_to_element(node)
        : ctx ? rdt_picture_find_element_id(ctx->id_scope, id, get_svg_attr) : nullptr;
}

DomNode* svg_dom_style_parent(DomNode* node, const SvgDomStyleScope* scope) {
    // a use shadow root inherits from its instance host without changing selector ancestry.
    for (const SvgDomStyleScope* current = scope; current; current = current->previous)
        if (node == current->root) return current->parent;
    return node ? node->parent : nullptr;
}

static const char* svg_dom_resolve_property(SvgStyleContext* query, DomElement* element,
    const char* name, bool inherits, bool* from_css, DomElement** declaring_element,
    const SvgDomStyleScope* scope) {
    DomDocument* doc = element->doc;
    for (DomNode* node = element; query->matcher && node && node->is_element(); node = svg_dom_style_parent(node, scope)) {
        DomElement* current = node->as_element();
        SvgStyleEntry* entry = svg_style_entry(query, dom_element_to_element(current));
        const char* inline_text = entry ? nullptr : current->get_attribute("style");
        size_t inline_count = entry ? entry->inline_count : 0;
        CssDeclaration** inline_declarations = entry ? entry->inline_declarations : inline_text
            ? css_parse_declaration_list_text(inline_text, strlen(inline_text), query->pool, &inline_count)
            : nullptr;
        CssDeclaration declaration = {};
        bool selected = css_select_element_declaration((CssEngine*)doc->services.cached_css_engine,
            query->matcher, current, doc->stylesheets, (size_t)doc->stylesheet_count,
            inline_declarations, inline_count, name, &declaration);
        const char* value = selected ? svg_resolve_property_declaration(query, current, name, &declaration, scope)
            : dom_element_is_svg(current) &&
                !css_property_is_identity_shorthand(css_property_code_from_name(name))
                ? svg_animation_attribute(current, name) : nullptr;
        bool svg_transform_sample = false;
        if (!declaration.important) {
            const char* animated = svg_animation_value(current, name, true);
            if (animated) {
                value = animated; selected = true;
                svg_transform_sample = strcmp(name, "transform") == 0;
            }
        }
        if (!selected) value = svg_resolve_attribute_variables(query, current, name, value, scope);
        bool inherit = value && (str_icmp_cstr(value, "inherit") == 0 ||
            (inherits && str_icmp_cstr(value, "unset") == 0) ||
            (strcmp(name, "color") == 0 && str_icmp_cstr(value, "currentColor") == 0));
        if (value && (str_icmp_cstr(value, "initial") == 0 || str_icmp_cstr(value, "unset") == 0)) {
            bool property_inherits = false;
            if (!inherit) value = svg_property_initial(name, &property_inherits);
        }
        if (value && !inherit) {
            // animateTransform emits SVG transform syntax even when targeting the CSS property.
            if (from_css) *from_css = selected && !svg_transform_sample;
            if (declaring_element) *declaring_element = current;
            return value;
        }
        if (!inherits && !inherit) break;
    }
    return nullptr;
}

const char* svg_get_dom_presentation_property(DomElement* element, const char* name,
    bool inherits, char* buffer, size_t buffer_size, bool* from_css,
    char** owned_value, DomElement** declaring_element, const SvgDomStyleScope* scope) {
    if (from_css) *from_css = false;
    if (owned_value) *owned_value = nullptr;
    if (declaring_element) *declaring_element = nullptr;
    if (!element || !element->doc || !name || (!owned_value && (!buffer || !buffer_size))) return nullptr;
    // use instances have a different inheritance chain; do not retain their computed values.
    SvgPaintHostStyle* shared = !scope ? svg_host_style(element->doc) : nullptr;
    SvgStyleContext local = {};
    SvgStyleContext* query = shared ? &shared->style : &local;
    if (!shared) {
        if (!svg_style_begin(query)) return nullptr;
        query->document = element->doc;
        query->engine = (CssEngine*)element->doc->services.cached_css_engine;
        query->matcher = selector_matcher_create(query->pool);
        if (query->matcher) state_configure_selector_matcher((DocState*)element->doc->state, query->matcher);
    }
    // SMIL queries base values while assembling a sample; they must neither
    // read nor populate the cache of completed animated presentation values.
    SvgStyleEntry* entry = shared && !svg_animation_is_sampling(element->doc)
        ? svg_style_entry(query, dom_element_to_element(element)) : nullptr;
    uint64_t generation = svg_animation_source_generation(element->doc, dom_element_to_element(element), element);
    SvgStyleProperty* property = entry ? entry->computed_properties : nullptr;
    while (property && (property->inherits != inherits || property->animation_generation != generation ||
        strcmp(property->name, name) != 0)) property = property->next;
    bool css = false;
    DomElement* owner = nullptr;
    const char* value = property ? property->value
        : svg_dom_resolve_property(query, element, name, inherits, &css, &owner, scope);
    if (!property && entry) {
        property = (SvgStyleProperty*)pool_calloc(query->pool, sizeof(SvgStyleProperty));
        if (property) {
            property->name = pool_strdup(query->pool, name);
            // animation samples are borrowed from a pool that can change between queries.
            property->value = value ? pool_strdup(query->pool, value) : nullptr;
            property->animation_generation = generation;
            property->inherits = inherits;
            property->from_css = css;
            property->declaring_element = owner;
            property->next = entry->computed_properties;
            entry->computed_properties = property;
        }
    }
    if (from_css) *from_css = property ? property->from_css : css;
    if (declaring_element) *declaring_element = property ? property->declaring_element : owner;
    bool found = value != nullptr;
    if (value) {
        if (owned_value) { *owned_value = mem_strdup(value, MEM_CAT_RENDER); found = *owned_value != nullptr; }
        else str_copy(buffer, buffer_size, value, strlen(value));
    }
    if (!shared) svg_style_destroy(&local);
    return found ? owned_value ? *owned_value : buffer : nullptr;
}

static const char* get_svg_attr_or_style(SvgInlineRenderContext* ctx, Element* elem,
    const char* name, char* buffer, size_t buffer_size) {
    if (!buffer || !buffer_size) return nullptr;
    const char* value = svg_style_property_value(ctx, elem, name);
    if (!value) return nullptr;
    str_copy(buffer, buffer_size, value, strlen(value));
    return buffer;
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

SvgViewBox svg_parse_viewbox(const char* viewbox_attr) {
    SvgViewBox vb = {0, 0, 0, 0, false};
    if (!viewbox_attr || !*viewbox_attr) return vb;

    float values[4];
    const char* end = nullptr;
    // reject malformed and negative extents; zero remains a valid disabled viewport.
    if (str_parse_float_list(viewbox_attr, ", \t\n\r\f\v", values, 4, &end) == 4 &&
        end && !*str_skip_ascii_space(end) && isfinite(values[0]) && isfinite(values[1]) &&
        isfinite(values[2]) && isfinite(values[3]) && values[2] >= 0 && values[3] >= 0) {
        vb.min_x = values[0];
        vb.min_y = values[1];
        vb.width = values[2];
        vb.height = values[3];
        vb.has_viewbox = true;
    }

    return vb;
}

RdtMatrix svg_viewbox_transform(const SvgViewBox* viewbox, float width, float height,
    const char* preserve_aspect_ratio) {
    RdtMatrix matrix = rdt_matrix_identity();
    if (!viewbox || !viewbox->has_viewbox || viewbox->width <= 0.0f || viewbox->height <= 0.0f) return matrix;
    float sx = width / viewbox->width;
    float sy = height / viewbox->height;
    float x = 0.0f, y = 0.0f;
    if (!preserve_aspect_ratio || !strstr(preserve_aspect_ratio, "none")) {
        float scale = preserve_aspect_ratio && strstr(preserve_aspect_ratio, "slice")
            ? fmaxf(sx, sy) : fminf(sx, sy);
        float align_x, align_y;
        svg_preserve_aspect_alignment(preserve_aspect_ratio, &align_x, &align_y);
        x = (width - viewbox->width * scale) * align_x;
        y = (height - viewbox->height * scale) * align_y;
        sx = sy = scale;
    }
    matrix.e11 = sx; matrix.e22 = sy;
    matrix.e13 = x - viewbox->min_x * sx; matrix.e23 = y - viewbox->min_y * sy;
    return matrix;
}

// ============================================================================
// SVG Length Parsing
// ============================================================================

float svg_resolve_length_unit(float number, CssUnit unit,
    const SvgLengthContext* context, SvgLengthAxis axis, float fallback) {
    switch (unit) {
        case CSS_UNIT_NONE: case CSS_UNIT_PX: return number;
        case CSS_UNIT_EM: return number * context->font_size;
        case CSS_UNIT_EX: return number * (context->fonts
            ? svg_font_x_height(context->fonts, &context->font) : context->x_height);
        case CSS_UNIT_PT: return number * (96.0f / 72.0f);
        case CSS_UNIT_PC: return number * 16.0f;
        case CSS_UNIT_IN: return number * 96.0f;
        case CSS_UNIT_CM: return number * (96.0f / 2.54f);
        case CSS_UNIT_MM: return number * (96.0f / 25.4f);
        case CSS_UNIT_Q: return number * (96.0f / 101.6f);
        case CSS_UNIT_PERCENT: {
        // SVG 2 coordinates use the viewport axis, or its normalized diagonal.
        float basis = axis == SVG_LENGTH_X ? context->viewport_width
            : axis == SVG_LENGTH_Y ? context->viewport_height
            : hypotf(context->viewport_width, context->viewport_height) / sqrtf(2.0f);
        return number * basis / 100.0f;
        }
        default: return fallback;
    }
}

static bool svg_normalize_math_lengths(CssValue* value, const SvgLengthContext* context,
    SvgLengthAxis axis, unsigned depth, size_t* remaining, bool preserve_percentages,
    CssMathLeafResolver resolve_leaf, void* leaf_context) {
    if (!value || depth > 32 || !*remaining) return false;
    (*remaining)--;
    if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        if (preserve_percentages && value->type == CSS_VALUE_TYPE_PERCENTAGE)
            return isfinite(value->data.percentage.value);
        double resolved = NAN;
        float number = resolve_leaf && resolve_leaf(leaf_context, value, &resolved) ? (float)resolved
            : value->type == CSS_VALUE_TYPE_LENGTH
            ? svg_resolve_length_unit((float)value->data.length.value, value->data.length.unit, context, axis, NAN)
            : svg_resolve_length_unit((float)value->data.percentage.value, CSS_UNIT_PERCENT, context, axis, NAN);
        if (!isfinite(number)) return false;
        if (preserve_percentages) {
            value->type = CSS_VALUE_TYPE_LENGTH;
            value->data.length = {number, CSS_UNIT_PX};
        } else {
            value->type = CSS_VALUE_TYPE_NUMBER;
            value->data.number = {number, false};
        }
        return true;
    }
    if (value->type == CSS_VALUE_TYPE_NUMBER) return isfinite(value->data.number.value);
    if (value->type == CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < value->data.list.count; i++)
            if (!svg_normalize_math_lengths(value->data.list.values[i], context, axis, depth + 1,
                remaining, preserve_percentages, resolve_leaf, leaf_context)) return false;
        return value->data.list.count > 0;
    }
    if (value->type == CSS_VALUE_TYPE_FUNCTION) {
        CssFunction* function = value->data.function;
        if (!function || !function->name || (strcmp(function->name, "calc") != 0 &&
            strcmp(function->name, "min") != 0 && strcmp(function->name, "max") != 0 &&
            strcmp(function->name, "clamp") != 0)) return false;
        for (int i = 0; i < function->arg_count; i++)
            if (!svg_normalize_math_lengths(function->args[i], context, axis, depth + 1,
                remaining, preserve_percentages, resolve_leaf, leaf_context)) return false;
        return function->arg_count > 0;
    }
    // the CSS parser retains arithmetic operators and parentheses as tokens in its lists.
    const char* token = nullptr;
    if (value->type == CSS_VALUE_TYPE_CUSTOM) token = value->data.custom_property.name;
    else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        token = info ? info->name : nullptr;
    }
    return token && token[0] && !token[1] && strchr("+-*/()", token[0]);
}

bool svg_normalize_length_value(CssValue* value, const SvgLengthContext* context,
    SvgLengthAxis axis, bool preserve_percentages, CssMathLeafResolver resolve_leaf,
    void* leaf_context) {
    size_t remaining = 4096;
    // both used geometry and CSSOM normalize caller-owned leaves through the same bounded walk.
    return context && svg_normalize_math_lengths(value, context, axis, 0, &remaining,
        preserve_percentages, resolve_leaf, leaf_context);
}

float svg_resolve_angle(const char* value, float fallback) {
    if (!value) return fallback;
    value = str_skip_ascii_space(value);
    char* end = nullptr;
    float number = strtof(value, &end);
    if (end == value || !isfinite(number)) return fallback;
    const char* unit = end;
    while (str_is_alpha(*end)) end++;
    CssUnit parsed = css_unit_from_string(unit, end - unit);
    if (*str_skip_ascii_space(end) || (end != unit && !css_unit_is_angle(parsed))) return fallback;
    CssValue angle = {};
    // SVG unitless angles are degrees; the shared CSS resolver represents them as numbers.
    if (end == unit) {
        angle.type = CSS_VALUE_TYPE_NUMBER;
        angle.data.number.value = number;
    } else {
        angle.type = CSS_VALUE_TYPE_ANGLE;
        angle.data.length = {number, parsed};
    }
    return resolve_css_angle_value(&angle) * 180.0f / math_pi_f();
}

float svg_resolve_length(const char* value, const SvgLengthContext* context,
    SvgLengthAxis axis, float fallback) {
    if (!value || !context) return fallback;
    value = str_skip_ascii_space(value);
    if (strncmp(value, "calc(", 5) == 0 || strncmp(value, "min(", 4) == 0 ||
        strncmp(value, "max(", 4) == 0 || strncmp(value, "clamp(", 6) == 0) {
        if (strlen(value) > 65536) return fallback;
        Pool* pool = mem_pool_create(nullptr, MEM_ROLE_RENDER, "render.svg.length_math");
        if (!pool) return fallback;
        // SVG lengths accept unitless number math; CSS box width rejects that domain.
        CssValue* parsed = svg_parse_property_value(pool, value, "stroke-width");
        CssMathEvaluationContext math = {nullptr, nullptr, 1.0, false};
        CssMathResult evaluated = svg_normalize_length_value(parsed, context, axis)
            ? css_math_evaluate(parsed, &math) : CssMathResult{};
        float result = evaluated.resolved && evaluated.type == CSS_MATH_NUMBER
            ? (float)evaluated.value : NAN;
        mem_pool_destroy(pool);
        return isfinite(result) ? result : fallback;
    }
    char* end = nullptr;
    float number = strtof(value, &end);
    if (end == value || !isfinite(number)) return fallback;
    const char* unit = str_skip_ascii_space(end);
    size_t length = strlen(unit);
    while (length && isspace((unsigned char)unit[length - 1])) length--;
    CssUnit parsed = css_unit_from_string(unit, length);
    if (length && parsed == CSS_UNIT_NONE) return fallback;
    return svg_resolve_length_unit(number, parsed, context, axis, fallback);
}

int svg_resolve_dash_array(const char* value, const SvgLengthContext* lengths,
    float* dashes, int capacity) {
    bool measure = !dashes && capacity == 0;
    if (!value || !lengths || (!measure && capacity < 1)) return 0;
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_RENDER, "render.svg.dash_math");
    if (!pool) return 0;
    CssValue* parsed = svg_parse_property_value(pool, value, "stroke-dasharray");
    struct DashContext {
        const SvgLengthContext* lengths;
        float* output;
        int capacity;
        int count;
        double total;
    } context = {lengths, dashes, capacity, 0, 0.0};
    auto visit = [](const CssValue* item, void* data) -> bool {
        DashContext* context = (DashContext*)data;
        if (context->count >= INT_MAX / 2 || (context->output && context->count >= context->capacity)) return false;
        // parsed values are scratch-owned; the shared SVG walk computes percentages against the viewport diagonal.
        if (!svg_normalize_length_value((CssValue*)item, context->lengths, SVG_LENGTH_DIAGONAL)) return false;
        CssMathEvaluationContext math_context = {};
        CssMathResult math = css_math_evaluate(item, &math_context);
        if (!math.resolved || math.type != CSS_MATH_NUMBER || !isfinite(math.value)) return false;
        float dash = (float)fmax(0.0, math.value);
        if (!isfinite(dash)) return false;
        if (context->output) context->output[context->count] = dash;
        context->count++;
        context->total += dash;
        return true;
    };
    bool valid = parsed && css_property_validate_value(CSS_PROPERTY_STROKE_DASHARRAY, parsed) &&
        css_value_visit_list_items(parsed, visit, &context);
    mem_pool_destroy(pool);
    if (!valid || !isfinite(context.total) || context.total <= 0.0) return 0;
    if (context.count & 1) {
        if (measure) return context.count * 2;
        if (context.count > capacity / 2) return 0;
        int original = context.count;
        for (int index = 0; index < original; index++) dashes[context.count++] = dashes[index];
    }
    return context.count;
}

static float parse_svg_length(const char* value, float default_value) {
    if (!value || !*value) return default_value;

    char* parsed_end;
    float num = strtof(value, &parsed_end);
    if (parsed_end == value) return default_value;

    // skip whitespace after number
    const char* end = str_skip_ascii_space(parsed_end);

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

static Color parse_svg_color(const char* value) {
    Color result = {};
    result.a = 255;
    if (!value || !*value) return result;
    value = str_skip_ascii_space(value);
    if (strcmp(value, "none") == 0) {
        result.a = 0;
        return result;
    }
    // SVG and HTML paint share named colors, numeric syntax and channel clamping.
    CssColor color = {};
    if (css_parse_color(value, &color) && color.type != CSS_COLOR_CURRENT) {
        result.r = color.r; result.g = color.g; result.b = color.b; result.a = color.a;
    }
    return result;
}

static Color svg_resolve_color_keyword(SvgInlineRenderContext* ctx, const char* value, const char* property = nullptr) {
    // traversal keeps omitted declarations distinct from inherited paint; resource colors need their CSS initial value.
    bool inherits = false;
    if (!value && property) value = svg_property_initial(property, &inherits);
    if (ctx && value && str_icmp_cstr(value, "currentColor") == 0) {
        return ctx->current_color;
    }
    return parse_svg_color(value);
}

float svg_font_size_value(const char* value, float parent_size, float parent_x_height);
int svg_font_weight_value(const char* value, int parent_weight);
static float parse_svg_pct_or_num(const char* value, float default_val);

float svg_font_x_height(FontContext* fonts, const FontStyleDesc* descriptor) {
    float result = descriptor->size_px * 0.5f;
    FontHandle* handle = fonts ? font_resolve(fonts, descriptor) : nullptr;
    if (handle) { result = descriptor->size_px * font_get_x_height_ratio(handle); font_handle_release(handle); }
    return result;
}

static FontStyleDesc svg_context_font_descriptor(SvgInlineRenderContext* ctx) {
    FontStyleDesc descriptor = {};
    descriptor.family = ctx->inherited_font_family ? ctx->inherited_font_family : "Arial";
    descriptor.size_px = ctx->inherited_font_size > 0.0f ? ctx->inherited_font_size : 16.0f;
    descriptor.weight = (FontWeight)(ctx->inherited_font_weight > 0 ? ctx->inherited_font_weight : 400);
    const char* slant = ctx->inherited_font_style;
    descriptor.slant = slant && strcmp(slant, "italic") == 0 ? FONT_SLANT_ITALIC
        : slant && strcmp(slant, "oblique") == 0 ? FONT_SLANT_OBLIQUE : FONT_SLANT_NORMAL;
    return descriptor;
}

static float svg_context_font_size(const char* value, FontContext* fonts,
    const FontStyleDesc* parent) {
    char* end = nullptr;
    strtof(value, &end);
    // absolute and em/% sizes do not need a font database lookup.
    float x_height = strcmp(str_skip_ascii_space(end), "ex") == 0
        ? svg_font_x_height(fonts, parent) : 0.0f;
    return svg_font_size_value(value, parent->size_px, x_height);
}

static SvgLengthContext svg_length_context(SvgInlineRenderContext* ctx, Element* elem = nullptr) {
    FontStyleDesc descriptor = svg_context_font_descriptor(ctx);
    const char* own_size = elem ? svg_style_property_value(ctx, elem, "font-size") : nullptr;
    if (own_size) descriptor.size_px = svg_context_font_size(own_size, ctx->font_ctx, &descriptor);
    const char* family = elem ? svg_style_property_value(ctx, elem, "font-family") : nullptr;
    if (family) descriptor.family = family;
    const char* weight = elem ? svg_style_property_value(ctx, elem, "font-weight") : nullptr;
    if (weight) descriptor.weight = (FontWeight)svg_font_weight_value(weight, descriptor.weight);
    const char* slant = elem ? svg_style_property_value(ctx, elem, "font-style") : nullptr;
    if (slant) descriptor.slant = strcmp(slant, "italic") == 0 ? FONT_SLANT_ITALIC
        : strcmp(slant, "oblique") == 0 ? FONT_SLANT_OBLIQUE : FONT_SLANT_NORMAL;
    return {ctx->current_viewport_w, ctx->current_viewport_h, descriptor.size_px,
        descriptor.size_px * 0.5f, ctx->font_ctx, descriptor};
}

static const char* svg_scratch_text(SvgInlineRenderContext* ctx, const char* start, size_t length) {
    char* copy = (char*)scratch_alloc(ctx->resource_scratch, length + 1);
    if (!copy) return nullptr;
    memcpy(copy, start, length); copy[length] = '\0';
    return copy;
}

static SvgPaint svg_parse_paint(SvgInlineRenderContext* ctx, const char* value,
                                Element* source, bool stroke = false) {
    SvgPaint paint = {}; paint.kind = SVG_PAINT_NONE;
    paint.source = lam::up(source);
    if (ctx) {
        paint.source_transform = ctx->transform;
        paint.source_style = ctx->style_context; paint.source_path = ctx->source_path;
    }
    if (!value) return paint;
    value = str_skip_ascii_space(value);
    if (str_icmp_cstr(value, "none") == 0) return paint;
    if (str_icmp_cstr(value, "currentColor") == 0) paint.kind = SVG_PAINT_CURRENT_COLOR;
    else if (str_icmp_cstr(value, "context-fill") == 0) paint.kind = SVG_PAINT_CONTEXT_FILL;
    else if (str_icmp_cstr(value, "context-stroke") == 0) paint.kind = SVG_PAINT_CONTEXT_STROKE;
    else if (str_istarts_with_cstr(value, "url(")) {
        const char* start = str_skip_ascii_space(value + 4);
        char quote = (*start == '\'' || *start == '"') ? *start++ : 0;
        const char* end = quote ? strchr(start, quote) : strchr(start, ')');
        if (!end) return paint;
        const char* close = quote ? str_skip_ascii_space(end + 1) : end;
        if (*close != ')') return paint;
        while (end > start && str_char_is_ascii_space(end[-1])) end--;
        paint.kind = SVG_PAINT_RESOURCE;
        paint.reference = lam::up(ctx ? svg_scratch_text(ctx, start, (size_t)(end - start)) : start);
        const char* fallback = str_skip_ascii_space(close + 1);
        if (*fallback) paint.fallback = lam::up(ctx ? svg_scratch_text(ctx, fallback, strlen(fallback)) : fallback);
    } else if (strcmp(value, "initial") == 0) {
        paint.kind = stroke ? SVG_PAINT_NONE : SVG_PAINT_COLOR;
        paint.color.a = 255;
    } else {
        CssColor color = {};
        if (css_parse_color(value, &color)) {
            paint.kind = SVG_PAINT_COLOR;
            paint.color.r = color.r; paint.color.g = color.g; paint.color.b = color.b; paint.color.a = color.a;
        }
    }
    return paint;
}

bool svg_paint_value_is_valid(const char* value) {
    if (!value) return false;
    SvgPaint paint = svg_parse_paint(nullptr, value, nullptr);
    if (paint.kind == SVG_PAINT_RESOURCE && paint.fallback) {
        SvgPaint fallback = svg_parse_paint(nullptr, paint.fallback, nullptr);
        return fallback.kind == SVG_PAINT_COLOR || fallback.kind == SVG_PAINT_CURRENT_COLOR ||
            strcmp(paint.fallback, "none") == 0;
    }
    return paint.kind != SVG_PAINT_NONE || strcmp(value, "none") == 0 || strcmp(value, "initial") == 0;
}

static Color svg_paint_color(SvgInlineRenderContext* ctx, const SvgPaint* paint) {
    return paint->kind == SVG_PAINT_CURRENT_COLOR ? ctx->current_color : paint->color;
}

static const char* const svg_inherited_property_names[SVG_STYLE_PROPERTY_COUNT] = {
    "fill-rule", "stroke-width", "stroke-linecap", "stroke-linejoin", "stroke-miterlimit",
    "stroke-dasharray", "stroke-dashoffset", "paint-order", "marker-start", "marker-mid", "marker-end",
};

static SvgInheritedProperty svg_computed_property(SvgInlineRenderContext* ctx, Element* elem,
    SvgInheritedPropertyId id, bool apply_font = true) {
    SvgInheritedProperty result = ctx->inherited_properties[id];
    const char* value = elem ? svg_style_property_value(ctx, elem, svg_inherited_property_names[id]) : nullptr;
    if (value) result = {lam::up(value), svg_length_context(ctx, apply_font ? elem : nullptr), ctx->style_context, ctx->source_path};
    // percentages inherit as percentages; font-relative lengths compute at their declaration.
    result.lengths.viewport_width = ctx->current_viewport_w;
    result.lengths.viewport_height = ctx->current_viewport_h;
    return result;
}

static void svg_apply_inherited_paint_attrs(SvgInlineRenderContext* ctx, Element* elem) {
    if (!ctx || !elem) return;

    char color_buf[256];
    const char* color_attr = get_svg_attr_or_style(ctx, elem, "color", color_buf, sizeof(color_buf));
    if (color_attr && str_icmp_cstr(color_attr, "currentColor") != 0) {
        ctx->current_color = parse_svg_color(color_attr);
    }

    const char* fill = svg_style_property_value(ctx, elem, "fill");
    if (fill) ctx->fill_paint = svg_parse_paint(ctx, fill, elem);
    const char* stroke = svg_style_property_value(ctx, elem, "stroke");
    if (stroke) ctx->stroke_paint = svg_parse_paint(ctx, stroke, elem, true);
    ctx->fill_none = ctx->fill_paint.kind == SVG_PAINT_NONE;
    ctx->stroke_none = ctx->stroke_paint.kind == SVG_PAINT_NONE;
    ctx->fill_color = svg_paint_color(ctx, &ctx->fill_paint);
    ctx->stroke_color = svg_paint_color(ctx, &ctx->stroke_paint);

    char stroke_width_buf[64];
    const char* stroke_width = get_svg_attr_or_style(ctx, elem, "stroke-width", stroke_width_buf, sizeof(stroke_width_buf));
    if (stroke_width) {
        SvgLengthContext lengths = svg_length_context(ctx, elem);
        // signed SVG coordinate math stays signed; stroke widths clamp only after computation.
        ctx->stroke_width = fmaxf(0.0f, svg_resolve_length(stroke_width, &lengths, SVG_LENGTH_DIAGONAL, 1.0f));
    }
    const char* fill_opacity = svg_style_property_value(ctx, elem, "fill-opacity");
    if (fill_opacity) ctx->fill_opacity = clamp_unit(parse_svg_pct_or_num(fill_opacity, 1.0f));
    const char* stroke_opacity = svg_style_property_value(ctx, elem, "stroke-opacity");
    if (stroke_opacity) ctx->stroke_opacity = clamp_unit(parse_svg_pct_or_num(stroke_opacity, 1.0f));
    FontStyleDesc parent_font = svg_context_font_descriptor(ctx);
    const char* size = svg_style_property_value(ctx, elem, "font-size");
    if (size) ctx->inherited_font_size = svg_context_font_size(size, ctx->font_ctx, &parent_font);
    const char* family = svg_style_property_value(ctx, elem, "font-family");
    if (family) ctx->inherited_font_family = lam::up(family);
    const char* weight = svg_style_property_value(ctx, elem, "font-weight");
    if (weight) ctx->inherited_font_weight = svg_font_weight_value(weight,
        ctx->inherited_font_weight > 0 ? ctx->inherited_font_weight : 400);
    const char* slant = svg_style_property_value(ctx, elem, "font-style");
    if (slant) ctx->inherited_font_style = lam::up(slant);
    const char* anchor = svg_style_property_value(ctx, elem, "text-anchor");
    if (anchor) ctx->inherited_text_anchor = lam::up(anchor);
    SvgLengthContext computed_lengths = svg_length_context(ctx);
    for (size_t i = 0; i < SVG_STYLE_PROPERTY_COUNT; i++) {
        const char* value = svg_style_property_value(ctx, elem, svg_inherited_property_names[i]);
        if (value) ctx->inherited_properties[i] = {lam::up(value), computed_lengths, ctx->style_context, ctx->source_path};
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

bool svg_parse_transform(const char* transform_str, float matrix[6]) {
    if (!transform_str || !matrix) return false;

    // initialize to identity matrix: [a, b, c, d, e, f] = [1, 0, 0, 1, 0, 0]
    matrix[0] = 1; matrix[1] = 0;  // a, b
    matrix[2] = 0; matrix[3] = 1;  // c, d
    matrix[4] = 0; matrix[5] = 0;  // e, f (translation)

    const char* p = transform_str;

    while (*p) {
        // skip whitespace
        p = str_skip_ascii_space(p);
        if (!*p) break;

        float local[6] = {1, 0, 0, 1, 0, 0};

        if (strncmp(p, "translate", 9) == 0) {
            p += 9;
            while (*p && *p != '(') p++;
            if (*p == '(') {
                p++;
                float values[2] = {};
                str_parse_float_list(p, ", \t\n\r\f\v", values, 2, &p);
                local[4] = values[0];
                local[5] = values[1];
            }
        } else if (strncmp(p, "scale", 5) == 0) {
            p += 5;
            while (*p && *p != '(') p++;
            if (*p == '(') {
                p++;
                float values[2] = {};
                size_t count = str_parse_float_list(p, ", \t\n\r\f\v", values, 2, &p);
                local[0] = values[0];
                local[3] = count > 1 ? values[1] : (*p && *p != ')' ? 0.0f : values[0]);
            }
        } else if (strncmp(p, "rotate", 6) == 0) {
            p += 6;
            while (*p && *p != '(') p++;
            if (*p == '(') {
                p++;
                float values[3] = {};
                size_t count = str_parse_float_list(p, ", \t\n\r\f\v", values, 3, &p);
                float angle = values[0];
                float rad = math_degrees_to_radians(angle);
                float c_val = cosf(rad);
                float s_val = sinf(rad);
                local[0] = c_val; local[1] = s_val;
                local[2] = -s_val; local[3] = c_val;

                // handle rotate(angle, cx, cy) with pivot point
                if (count > 1 || (*p && *p != ')')) {
                    float cx = values[1];
                    float cy = values[2];
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
                float values[1] = {};
                str_parse_float_list(p, ", \t\n\r\f\v", values, 1, &p);
                float angle = values[0];
                float rad = math_degrees_to_radians(angle);
                local[2] = tanf(rad);
            }
        } else if (strncmp(p, "skewY", 5) == 0) {
            p += 5;
            while (*p && *p != '(') p++;
            if (*p == '(') {
                p++;
                float values[1] = {};
                str_parse_float_list(p, ", \t\n\r\f\v", values, 1, &p);
                float angle = values[0];
                float rad = math_degrees_to_radians(angle);
                local[1] = tanf(rad);
            }
        } else if (strncmp(p, "matrix", 6) == 0) {
            p += 6;
            while (*p && *p != '(') p++;
            if (*p == '(') {
                p++;
                float values[6] = {};
                size_t count = str_parse_float_list(p, ", \t\n\r\f\v", values, 6, &p);
                for (size_t i = 0; i < count; i++) local[i] = values[i];
                if (*p) for (size_t i = count; i < 6; i++) local[i] = 0.0f;
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
    SvgIntrinsicSize size = {300, 150, 2.0f, false, false, false};  // HTML default

    if (!svg_element) return size;

    const char* width_attr = get_svg_attr(svg_element, "width");
    const char* height_attr = get_svg_attr(svg_element, "height");
    const char* viewbox_attr = get_svg_attr(svg_element, "viewBox");
    if (!viewbox_attr) viewbox_attr = get_svg_attr(svg_element, "viewbox");
    float numeric_width=NAN,numeric_height=NAN;

    SvgViewBox vb = svg_parse_viewbox(viewbox_attr);

    // determine width. A viewBox provides a coordinate system and aspect ratio,
    // but not an explicit intrinsic width/height attribute. Percentage
    // attributes are presentation hints resolved against the containing block,
    // so they must not become natural dimensions here.
    if (!width_attr && read_svg_number_attr(svg_element,"width",&numeric_width) && numeric_width >= 0) {
        // Lambda element presentation hints can carry numbers rather than HTML strings.
        size.width = numeric_width;
        size.has_intrinsic_width = true;
    } else if (width_attr && *width_attr && !strchr(width_attr, '%')) {
        size.width = parse_svg_length(width_attr, 300);
        size.has_intrinsic_width = true;
    } else if (vb.has_viewbox && vb.width > 0) {
        size.width = vb.width;
    }

    // determine height
    if (!height_attr && read_svg_number_attr(svg_element,"height",&numeric_height) && numeric_height >= 0) {
        size.height = numeric_height;
        size.has_intrinsic_height = true;
    } else if (height_attr && *height_attr && !strchr(height_attr, '%')) {
        size.height = parse_svg_length(height_attr, 150);
        size.has_intrinsic_height = true;
    } else if (vb.has_viewbox && vb.height > 0) {
        size.height = vb.height;
    }

    // A zero SVG viewport dimension is a degenerate natural ratio; when a
    // valid viewBox exists, sizing must use its ratio instead of height=0.
    if (vb.has_viewbox && vb.width > 0 && vb.height > 0 &&
        size.has_intrinsic_height && size.height <= 0.0f) {
        size.has_intrinsic_height = false;
    }

    // calculate aspect ratio
    if (vb.has_viewbox && vb.width > 0 && vb.height > 0) {
        size.aspect_ratio = vb.width / vb.height;
        size.has_intrinsic_aspect_ratio = true;
    } else if (size.has_intrinsic_width && size.has_intrinsic_height && size.height > 0) {
        size.aspect_ratio = size.width / size.height;
        size.has_intrinsic_aspect_ratio = true;
    }

    return size;
}

// ============================================================================
// SVG Defs: Gradient definitions and element refs
// ============================================================================

#define SVG_MAX_ELEM_DEFS  4096
#define SVG_DEF_INITIAL_CAPACITY 16

struct SvgGradDef {
    bool is_radial, user_space;
    float x1, y1, x2, y2, cx, cy, r;
    RdtGradientOptions options;
    RdtMatrix gradient_transform;
    RdtGradientStop* stops;
    int stop_count;
};

struct SvgElemDef {
    char     id[128];
    Element* elem;
};

struct SvgDefTable {
    // grow definition arrays on demand; a full 4096-entry table exceeds the Linux render scratch arena.
    SvgElemDef* elems;
    int        elem_count;
    int        elem_capacity;
};

static float parse_svg_pct_or_num(const char* s, float fallback) {
    if (!s || !*s) return fallback;
    char* parsed_end;
    float v = strtof(s, &parsed_end);
    if (parsed_end == s) return fallback;
    const char* end = str_skip_ascii_space(parsed_end);
    if (*end == '%') v /= 100.0f;
    return v;
}



static bool grow_svg_elem_defs(SvgInlineRenderContext* ctx, SvgDefTable* table) {
    if (!ctx || !table || table->elem_capacity >= SVG_MAX_ELEM_DEFS) return false;
    return lam::scratch_grow_array(ctx->resource_scratch, &table->elems,
        &table->elem_capacity, table->elem_count, table->elem_count + 1,
        SVG_DEF_INITIAL_CAPACITY, SVG_MAX_ELEM_DEFS);
}

static SvgDefTable* ensure_svg_def_table(SvgInlineRenderContext* ctx) {
    if (!ctx->defs) {
        if (!ctx->resource_scratch) return nullptr;
        SvgDefTable* table = (SvgDefTable*)scratch_calloc(
            ctx->resource_scratch, sizeof(SvgDefTable));
        if (!table) return nullptr;
        ctx->defs = lam::up((HashMap*)table);
    }
    return (SvgDefTable*)ctx->defs;
}

static void register_svg_def_element(SvgInlineRenderContext* ctx, Element* elem) {
    if (!ctx || !elem) return;
    const char* id = get_svg_attr(elem, "id");
    SvgDefTable* table = id ? ensure_svg_def_table(ctx) : nullptr;
    if (!table) return;
    for (int i = 0; i < table->elem_count; i++) {
        if (strcmp(table->elems[i].id, id) == 0) { table->elems[i].elem = elem; return; }
    }
    if (table->elem_count >= SVG_MAX_ELEM_DEFS ||
        (table->elem_count >= table->elem_capacity && !grow_svg_elem_defs(ctx, table))) return;
    SvgElemDef* definition = &table->elems[table->elem_count++];
    str_copy(definition->id, sizeof(definition->id), id, strlen(id));
    definition->elem = elem;
}

static char* svg_href_file_part(const char* href, const char** fragment_out);
static char* svg_resolve_resource_path(SvgInlineRenderContext* ctx, const char* href_no_fragment);
static SvgInlineRenderContext svg_resource_style_context(SvgInlineRenderContext* ctx, Element* resource);

typedef struct {
    Element* element;
    SvgResourceDocument* document;
} SvgResourceReference;

static SvgResourceDocument* svg_resource_document_create(SvgInlineRenderContext* ctx,
    RdtPicture* picture, const char* path) {
    Element* root = picture ? rdt_picture_get_svg_root(picture) : nullptr;
    SvgResourceDocument* document = root ? (SvgResourceDocument*)mem_calloc(1, sizeof(*document), MEM_CAT_RENDER) : nullptr; // OBJ_HEAP_OK: a walk's style context or the document's use-resource cache owns it; svg_resource_document_destroy releases it
    if (!document) { rdt_picture_free(picture); return nullptr; }
    document->picture = picture; document->path = lam::own(mem_strdup(path, MEM_CAT_RENDER));
    if (!document->path || !svg_style_init(&document->style, root, ctx->current_viewport_w,
        ctx->current_viewport_h, nullptr, path, ctx->image_document)) {
        svg_resource_document_destroy(document); return nullptr;
    }
    document->style.resource_document = document;
    svg_animation_mark_reference(document->style.document->root);
    document->fonts = svg_style_font_context(&document->style, document->path,
        ctx->raster_scale, ctx->image_document, ctx->font_ctx);
    return document;
}

// the cache lives in `pool`, which the document's resource hook destroys
struct SvgUseResourceCache : DomDocumentResourceData {
    lam::Own<Pool> pool;
    lam::Up<DomDocument> document;
    SvgResourceDocument* documents;
};

static void svg_use_resource_cache_destroy(DomDocumentResourceData* data) {
    SvgUseResourceCache* cache = (SvgUseResourceCache*)data;
    for (SvgResourceDocument* document = cache->documents; document;) {
        SvgResourceDocument* next = document->next;
        // instance controls release their borrowed DOM refs before the source owner is destroyed (D4.2.6).
        svg_animation_forget_source_document(cache->document, document->style.document);
        svg_resource_document_destroy(document); document = next;
    }
    cache->document->services.svg_use_resource_cache = nullptr;
    // the cache itself lives in this pool
    mem_pool_destroy(cache->pool);
}

static SvgResourceReference svg_retain_use_reference(SvgInlineRenderContext* ctx, DomElement* host,
    const SvgResourceReference* source, const char* fragment) {
    SvgResourceReference result = {};
    if (!host || !source->document || host->doc->services.svg_image_document) return result;
    SvgUseResourceCache* cache = (SvgUseResourceCache*)host->doc->services.svg_use_resource_cache;
    if (!cache) {
        MemContext* context = (MemContext*)host->doc->services.mem_ctx;
        Pool* pool = mem_pool_create(context ? context : mem_context_process(MEM_ROLE_RENDER),
                                     MEM_ROLE_RENDER, "render.svg.use_resource_cache");
        if (!pool) return result;
        cache = (SvgUseResourceCache*)pool_calloc(pool, sizeof(*cache));
        // the document takes ownership once its resource hook is registered
        if (!cache || !dom_document_add_resource(host->doc, cache, svg_use_resource_cache_destroy)) {
            mem_pool_destroy(pool); return result;
        }
        cache->pool = lam::own(pool);
        cache->document = lam::up(host->doc);
        host->doc->services.svg_use_resource_cache = cache;
    }
    Element* root = rdt_picture_get_svg_root(source->document->picture);
    SvgResourceDocument* retained = nullptr;
    unsigned count = 0;
    for (SvgResourceDocument* document = cache->documents; document; document = document->next) {
        if (rdt_picture_get_svg_root(document->picture) == root && strcmp(document->path, source->document->path) == 0)
            retained = document;
        count++;
    }
    if (!retained) {
        if (count >= SVG_MAX_ELEM_DEFS) {
            log_error("SVG_USE_RESOURCE_LIMIT: retained documents exceed %d", SVG_MAX_ELEM_DEFS);
            return result;
        }
        retained = svg_resource_document_create(ctx, rdt_picture_dup(source->document->picture), source->document->path);
        if (!retained) return result;
        retained->next = cache->documents; cache->documents = retained;
    }
    result.document = retained;
    result.element = rdt_picture_find_svg_element_by_id(retained->picture, fragment);
    return result;
}

static SvgInlineRenderContext svg_reference_render_context(SvgInlineRenderContext* ctx,
                                                           const SvgResourceReference* reference) {
    SvgInlineRenderContext result = *ctx;
    if (reference->document) {
        SvgResourceDocument* document = reference->document;
        result.svg_root = result.id_scope = lam::up(rdt_picture_get_svg_root(document->picture));
        result.style_context = lam::up(&document->style); result.source_path = lam::up(document->path);
        result.font_ctx = lam::up(document->fonts ? document->fonts : ctx->font_ctx);
        result.defs = nullptr;
    }
    return result;
}

static SvgResourceReference svg_resolve_reference(SvgInlineRenderContext* ctx, const char* href) {
    SvgResourceReference result = {};
    SvgStyleContext* style = (SvgStyleContext*)ctx->style_context;
    if (!href || !*href || !style) return result;
    if (*href == '#') return {svg_find_element_id(ctx, href + 1), style->resource_document};
    const char* fragment = nullptr;
    lam::Temp<char> file(svg_href_file_part(href, &fragment));
    if (!file || !*file.get() || !fragment || !*fragment ||
        (ctx->image_document && strncmp(file.get(), "data:", 5) != 0)) return result;
    lam::Temp<char> path(svg_resolve_resource_path(ctx, file.get())); file.reset();
    if (!path) return result;
    if (style->document && !input_resource_policy_admits(style->document->resource_policy, path.get())) return result;
    SvgStyleContext* owner = style->resource_owner ? style->resource_owner : style;
    SvgResourceDocument* document = nullptr;
    int count = 0;
    for (SvgResourceDocument* entry = owner->resources; entry; entry = entry->next) {
        count++;
        if (strcmp(path.get(), entry->path) == 0) { document = entry; break; }
    }
    if (!document && !svg_resource_stack_contains(path.get()) && count < SVG_MAX_ELEM_DEFS) {
        UiContext* ui = g_svg_active_rdcon ? g_svg_active_rdcon->ui_context : nullptr;
        ImageSurface* image = ui ? load_document_image(style->document, ui, path.get()) : nullptr;
        RdtPicture* picture = image && image->format == IMAGE_FORMAT_SVG
            ? rdt_picture_dup(image->pic) : ui ? nullptr : rdt_picture_load(path.get());
        document = svg_resource_document_create(ctx, picture, path.get());
        if (document) {
            document->style.resource_owner = owner; document->style.resource_document = document;
            document->next = owner->resources; owner->resources = document;
        }
    }
    if (document) result = {rdt_picture_find_svg_element_by_id(document->picture, fragment), document};
    return result;
}

static bool svg_is_gradient(SvgInlineRenderContext* ctx, Element* elem) {
    const char* tag = get_element_tag_name(ctx, elem);
    return tag && (strcmp(tag, "linearGradient") == 0 || strcmp(tag, "radialGradient") == 0);
}

// cycles end the template link; valid attributes and children encountered earlier remain usable.
static void svg_template_chain(SvgInlineRenderContext* ctx, Element* resource,
                                lam::ArrayList<SvgResourceReference>* chain, bool gradient) {
    SvgStyleContext* style = (SvgStyleContext*)ctx->style_context;
    SvgResourceReference current = {resource, style ? style->resource_document : nullptr};
    while (current.element && chain->size() < SVG_MAX_ELEM_DEFS) {
        SvgInlineRenderContext source = svg_reference_render_context(ctx, &current);
        const char* tag = get_element_tag_name(&source, current.element);
        if (!tag || (gradient ? !svg_is_gradient(&source, current.element) : strcmp(tag, "pattern") != 0)) break;
        bool cycle = false;
        for (size_t i = 0; i < chain->size(); i++) if ((*chain)[i].element == current.element) { cycle = true; break; }
        if (cycle || !chain->append(current)) break;
        const char* href = get_svg_href(&source, current.element);
        current = svg_resolve_reference(&source, href);
    }
}

static Element* svg_template_attribute_source(SvgInlineRenderContext* ctx,
    const lam::ArrayList<SvgResourceReference>* chain, const char* name, bool same_kind) {
    SvgInlineRenderContext first = chain->size() ? svg_reference_render_context(ctx, &(*chain)[0]) : *ctx;
    const char* first_tag = chain->size() ? get_element_tag_name(&first, (*chain)[0].element) : nullptr;
    for (size_t i = 0; i < chain->size(); i++) {
        Element* elem = (*chain)[i].element;
        SvgInlineRenderContext source = svg_reference_render_context(ctx, &(*chain)[i]);
        const char* tag = get_element_tag_name(&source, elem);
        if (same_kind && (!tag || !first_tag || strcmp(tag, first_tag) != 0)) continue;
        if (get_svg_attr(elem, name) || ElementReader(elem).has_attr(name)) return elem;
    }
    return nullptr;
}

static const char* svg_template_attribute(SvgInlineRenderContext* ctx, const lam::ArrayList<SvgResourceReference>* chain,
    const char* name, const char* fallback, bool same_kind = false) {
    Element* source = svg_template_attribute_source(ctx, chain, name, same_kind);
    const char* value = source ? get_svg_attr(source, name) : nullptr;
    return value ? value : fallback;
}

static float svg_gradient_length(SvgInlineRenderContext* ctx, const lam::ArrayList<SvgResourceReference>* chain,
    const char* name, const char* fallback, const SvgLengthContext* lengths,
    SvgLengthAxis axis) {
    // Lambda SVG builders carry numeric Items; resource inheritance must preserve them.
    float initial = svg_resolve_length(fallback, lengths, axis, 0.0f);
    Element* source = svg_template_attribute_source(ctx, chain, name, true);
    return source ? get_svg_number_attr(source, name, initial, lengths, axis) : initial;
}

static bool svg_resolve_gradient(SvgInlineRenderContext* ctx, Element* element,
                                 float bx, float by, float bw, float bh, SvgGradDef* def) {
    if (!svg_is_gradient(ctx, element)) return false;
    *def = {};
    lam::ArrayList<SvgResourceReference> chain(MEM_CAT_RENDER, 0);
    svg_template_chain(ctx, element, &chain, true);
    def->is_radial = strcmp(get_element_tag_name(ctx, element), "radialGradient") == 0;
    def->user_space = strcmp(svg_template_attribute(ctx, &chain, "gradientUnits", "objectBoundingBox"), "userSpaceOnUse") == 0;
    SvgInlineRenderContext resource_style = svg_resource_style_context(ctx, element);
    SvgLengthContext lengths = svg_length_context(&resource_style);
    if (!def->user_space) { lengths.viewport_width = lengths.viewport_height = 1.0f; }
    def->x1 = svg_gradient_length(ctx, &chain, "x1", "0%", &lengths, SVG_LENGTH_X);
    def->y1 = svg_gradient_length(ctx, &chain, "y1", "0%", &lengths, SVG_LENGTH_Y);
    def->x2 = svg_gradient_length(ctx, &chain, "x2", "100%", &lengths, SVG_LENGTH_X);
    def->y2 = svg_gradient_length(ctx, &chain, "y2", "0%", &lengths, SVG_LENGTH_Y);
    def->cx = svg_gradient_length(ctx, &chain, "cx", "50%", &lengths, SVG_LENGTH_X);
    def->cy = svg_gradient_length(ctx, &chain, "cy", "50%", &lengths, SVG_LENGTH_Y);
    def->r = svg_gradient_length(ctx, &chain, "r", "50%", &lengths, SVG_LENGTH_DIAGONAL);
    Element* fx = svg_template_attribute_source(ctx, &chain, "fx", true);
    Element* fy = svg_template_attribute_source(ctx, &chain, "fy", true);
    def->options.has_focal = def->is_radial;
    def->options.fx = fx ? get_svg_number_attr(fx, "fx", def->cx, &lengths, SVG_LENGTH_X) : def->cx;
    def->options.fy = fy ? get_svg_number_attr(fy, "fy", def->cy, &lengths, SVG_LENGTH_Y) : def->cy;
    def->options.fr = svg_gradient_length(ctx, &chain, "fr", "0%", &lengths, SVG_LENGTH_DIAGONAL);
    const char* spread = svg_template_attribute(ctx, &chain, "spreadMethod", "pad");
    def->options.spread = strcmp(spread, "repeat") == 0 ? RDT_GRADIENT_REPEAT
        : strcmp(spread, "reflect") == 0 ? RDT_GRADIENT_REFLECT : RDT_GRADIENT_PAD;
    RdtMatrix authored = rdt_matrix_identity();
    for (size_t i = 0; i < chain.size(); i++) {
        Element* candidate = chain[i].element;
        SvgInlineRenderContext candidate_context = svg_reference_render_context(ctx, &chain[i]);
        const char* value = svg_style_property_value(&candidate_context, candidate, "transform");
        bool css_transform = value != nullptr;
        if (!value) value = get_svg_attr(candidate, "gradientTransform");
        if (value) {
            Bound box = {0.0f, 0.0f, lengths.viewport_width, lengths.viewport_height};
            authored = svg_resolve_local_transform(value, css_transform,
                svg_style_property_value(&candidate_context, candidate, "transform-origin"), &box, &lengths);
            break;
        }
    }
    RdtMatrix basis = def->user_space ? rdt_matrix_identity()
        : RdtMatrix{bw, 0.0f, bx, 0.0f, bh, by, 0.0f, 0.0f, 1.0f};
    def->gradient_transform = rdt_matrix_multiply(&basis, &authored);
    Element* content = nullptr;
    SvgInlineRenderContext content_context = *ctx;
    int count = 0;
    for (size_t i = 0; i < chain.size() && !content; i++) {
        SvgInlineRenderContext source = svg_reference_render_context(ctx, &chain[i]);
        for (int64_t j = 0; j < chain[i].element->length; j++) {
            Element* child = get_child_element_at(chain[i].element, j);
            const char* tag = child ? get_element_tag_name(&source, child) : nullptr;
            if (tag && strcmp(tag, "stop") == 0) { content = chain[i].element; content_context = svg_reference_render_context(ctx, &chain[i]); count++; }
        }
    }
    if (!content || count <= 0) return true;
    def->stops = (RdtGradientStop*)scratch_calloc(ctx->resource_scratch, (size_t)count * sizeof(RdtGradientStop));
    if (!def->stops) return true;
    const char* host_color = svg_style_ancestor_property_value(ctx, element, "color");
    Color current_color = parse_svg_color(host_color);
    float previous_offset = 0.0f;
    SvgLengthContext offsets = lengths;
    offsets.viewport_width = offsets.viewport_height = 1.0f;
    for (int64_t j = 0; j < content->length; j++) {
        Element* child = get_child_element_at(content, j);
        const char* tag = child ? get_element_tag_name(&content_context, child) : nullptr;
        if (!tag || strcmp(tag, "stop") != 0) continue;
        RdtGradientStop* stop = &def->stops[def->stop_count++];
        stop->offset = fmaxf(previous_offset, clamp_unit(get_svg_number_attr(child, "offset", 0.0f, &offsets, SVG_LENGTH_X)));
        previous_offset = stop->offset;
        const char* color_text = svg_style_property_value(&content_context, child, "stop-color");
        const char* own_color = svg_style_property_value(&content_context, child, "color");
        Color color = color_text && str_icmp_cstr(color_text, "currentColor") == 0
            ? own_color ? parse_svg_color(own_color) : current_color : parse_svg_color(color_text);
        stop->r = color.r; stop->g = color.g; stop->b = color.b;
        stop->a = clamp_byte_round((float)color.a * clamp_unit(parse_svg_pct_or_num(
            svg_style_property_value(&content_context, child, "stop-opacity"), 1.0f)));
    }
    return true;
}

// ============================================================================
// Compose element transform with accumulated context transform
// ============================================================================

static CssValue* svg_parse_css_value(Pool* pool, const char* text, CssPropertyCode property) {
    return svg_parse_property_value(pool, text, css_property_spelling_from_code(property));
}

static float svg_transform_length(void* context, const CssValue* value) {
    if (!value) return 0.0f;
    SvgLengthContext* lengths = (SvgLengthContext*)context;
    if (value->type == CSS_VALUE_TYPE_NUMBER) return (float)value->data.number.value;
    if (value->type != CSS_VALUE_TYPE_LENGTH) return 0.0f;
    return svg_resolve_length_unit((float)value->data.length.value,
        value->data.length.unit, lengths, SVG_LENGTH_DIAGONAL, 0.0f);
}

RdtMatrix svg_resolve_local_transform(const char* value, bool from_css,
    const char* origin, const Bound* reference_box, const SvgLengthContext* lengths) {
    RdtMatrix result = rdt_matrix_identity();
    if (!value || !reference_box || !lengths) return result;
    // SVG transform attributes parse directly; only CSS syntax/origins need a parser pool.
    Pool* pool = from_css || origin ? mem_pool_create(nullptr, MEM_ROLE_RENDER, "render.svg.transform") : nullptr;
    if ((from_css || origin) && !pool) return result;
    TransformProp transform = {};
    if (origin) {
        // a supplied origin's omitted axis is center; SVG's unsupplied origin is zero.
        transform.origin_x = transform.origin_y = 50.0f;
        transform.origin_x_percent = transform.origin_y_percent = true;
        resolve_transform_origin_value(svg_parse_css_value(pool, origin, CSS_PROPERTY_TRANSFORM_ORIGIN),
            &transform, svg_transform_length, (void*)lengths);
    }
    RdtLogicalPoint point = radiant::transform_origin(&transform, reference_box->left,
        reference_box->top, reference_box->right - reference_box->left,
        reference_box->bottom - reference_box->top);
    if (from_css) {
        CssValue* parsed = svg_parse_css_value(pool, value, CSS_PROPERTY_TRANSFORM);
        TransformFunction* head = nullptr;
        TransformFunction* tail = nullptr;
        int count = css_value_count(parsed, 0);
        for (int index = 0; index < count; index++) {
            TransformFunction function = {};
            if (!resolve_transform_function_value(css_value_at(parsed, index), &function,
                svg_transform_length, (void*)lengths)) continue;
            TransformFunction* stored = (TransformFunction*)pool_calloc(pool, sizeof(TransformFunction));
            if (!stored) break;
            *stored = function;
            if (tail) tail->next = lam::own(stored);
            else head = stored;
            tail = stored;
        }
        result = radiant::compute_transform_matrix(head, reference_box->right - reference_box->left,
            reference_box->bottom - reference_box->top, point.x, point.y);
    } else {
        float m[6];
        if (svg_parse_transform(value, m)) result = {m[0], m[2], m[4], m[1], m[3], m[5], 0, 0, 1};
        if (origin) {
            RdtMatrix to = rdt_matrix_translate(point.x, point.y);
            RdtMatrix from = rdt_matrix_translate(-point.x, -point.y);
            result = rdt_matrix_multiply(&to, &result);
            result = rdt_matrix_multiply(&result, &from);
        }
    }
    if (pool) mem_pool_destroy(pool);
    return result;
}

static bool svg_style_property_from_css(SvgInlineRenderContext* ctx, Element* elem, const char* name) {
    SvgStyleContext* style = (SvgStyleContext*)ctx->style_context;
    SvgStyleEntry* entry = svg_style_entry(style, elem);
    uint64_t generation = entry ? svg_animation_source_generation(style->document, elem, entry->node) : 0;
    for (SvgStyleProperty* prop = entry ? entry->properties : nullptr; prop; prop = prop->next) {
        if (strcmp(prop->name, name) == 0 && prop->animation_generation == generation) return prop->from_css;
    }
    return false;
}

static RdtMatrix compose_element_transform(SvgInlineRenderContext* ctx, Element* elem) {
    const char* value = svg_style_property_value(ctx, elem, "transform");
    if (!value) return ctx->transform;
    SvgLengthContext lengths = svg_length_context(ctx, elem);
    Bound box = {ctx->viewbox_x, ctx->viewbox_y,
        ctx->viewbox_x + lengths.viewport_width, ctx->viewbox_y + lengths.viewport_height};
    const char* reference = svg_style_property_value(ctx, elem, "transform-box");
    if (reference && (strcmp(reference, "fill-box") == 0 || strcmp(reference, "stroke-box") == 0)) {
        SvgStyleContext* style = (SvgStyleContext*)ctx->style_context;
        SvgStyleEntry* entry = svg_style_entry(style, elem);
        const char* tag = get_element_tag_name(ctx, elem);
        RdtPath* path = tag && strcmp(tag, "path") == 0
            ? svg_parse_path_d(get_svg_attr(elem, "d")) : rdt_path_new();
        if (path) {
            if (tag && strcmp(tag, "path") != 0) svg_append_basic_shape_path(svg_style_node((SvgStyleContext*)ctx->style_context, elem), path, &lengths);
            rdt_path_get_bounds(path, &box.left, &box.top, &box.right, &box.bottom);
            rdt_path_free(path);
        }
        if (entry) dom_svg_element_geometry_bounds(entry->node, &box.left, &box.top, &box.right, &box.bottom);
    }
    RdtMatrix local = svg_resolve_local_transform(value, svg_style_property_from_css(ctx, elem, "transform"),
        svg_style_property_value(ctx, elem, "transform-origin"), &box, &lengths);
    return rdt_matrix_multiply(&ctx->transform, &local);
}

// ============================================================================
// Apply gradient fill via the SVG painter gateway
// ============================================================================

typedef uint32_t (*SvgPaintSampler)(void* context, float x, float y);
static void svg_rasterize_path_paint(SvgInlineRenderContext* ctx, RdtPath* path,
    const RdtMatrix* transform, RdtFillRule rule, float opacity, const RdtGradientOptions* stroke,
    SvgPaintSampler sampler, void* sampler_context, const char* operation);
static void svg_draw_radial_fallback(SvgInlineRenderContext* ctx, RdtPath* path, const SvgGradDef* gradient,
    const RdtMatrix* transform, RdtFillRule rule, float opacity, const RdtGradientOptions* stroke);

static void draw_gradient_fill(SvgInlineRenderContext* ctx, RdtPath* path, SvgGradDef* def,
                               float bx, float by, float bw, float bh,
                               const RdtMatrix* transform, RdtFillRule fill_rule,
                               float opacity, const RdtGradientOptions* stroke = nullptr) {
    if (!path || !def || def->stop_count == 0 ||
        (!def->user_space && (bw <= 0.0f || bh <= 0.0f)) ||
        (def->is_radial && (def->r < 0.0f || def->options.fr < 0.0f))) return;
    if (def->is_radial) {
        float distance = hypotf(def->cx - def->options.fx, def->cy - def->options.fy);
        // ThorVG's software backend clamps the focal circle; SVG 2 preserves the two-circle cone.
        if (def->r > 0.0f && (distance + def->options.fr >= def->r * 0.99f || def->r < 0.01f)) {
            svg_draw_radial_fallback(ctx, path, def, transform, fill_rule, opacity, stroke);
            return;
        }
    }
    RdtGradientOptions options = stroke ? *stroke : RdtGradientOptions{};
    options.spread = def->options.spread; options.has_focal = def->options.has_focal;
    options.fx = def->options.fx; options.fy = def->options.fy; options.fr = def->options.fr;
    RdtGradientStop* stops = (RdtGradientStop*)scratch_alloc(ctx->resource_scratch,
        (size_t)def->stop_count * sizeof(RdtGradientStop));
    if (!stops) return;
    memcpy(stops, def->stops, (size_t)def->stop_count * sizeof(RdtGradientStop));
    for (int i = 0; i < def->stop_count; i++) stops[i].a = clamp_byte_round((float)stops[i].a * clamp_unit(opacity));
    if (def->stop_count == 1 || (def->is_radial ? def->r == 0.0f
        : def->x1 == def->x2 && def->y1 == def->y2)) {
        RdtGradientStop stop = stops[def->stop_count - 1];
        Color color = {}; color.r = stop.r; color.g = stop.g; color.b = stop.b; color.a = stop.a;
        if (options.stroke_width > 0.0f) svg_stroke_path(ctx, path, color, options.stroke_width,
            options.cap, options.join, options.dash_array, options.dash_count, options.dash_phase, transform, options.miter_limit);
        else svg_fill_path(ctx, path, color, fill_rule, transform);
        return;
    }
    if (def->is_radial) svg_fill_radial_gradient(ctx, path,
        def->cx, def->cy, def->r, stops, def->stop_count, fill_rule, transform, &def->gradient_transform, &options);
    else svg_fill_linear_gradient(ctx, path,
        def->x1, def->y1, def->x2, def->y2, stops, def->stop_count, fill_rule,
        transform, &def->gradient_transform, &options);
}

typedef struct SvgPaintResourceScope {
    Element* element;
    const SvgPaintResourceScope* parent;
} SvgPaintResourceScope;

static SvgInlineRenderContext svg_resource_style_context(SvgInlineRenderContext* ctx, Element* resource) {
    SvgInlineRenderContext result = *ctx;
    memset(result.inherited_properties, 0, sizeof(result.inherited_properties));
    result.fill_color = parse_svg_color("black"); result.current_color = result.fill_color;
    result.stroke_color = {}; result.fill_none = false; result.stroke_none = true;
    result.fill_paint = {}; result.fill_paint.color = result.fill_color;
    result.stroke_paint = {}; result.stroke_paint.kind = SVG_PAINT_NONE;
    result.stroke_width = result.opacity = result.fill_opacity = result.stroke_opacity = 1.0f;
    result.inherited_font_family = nullptr; result.inherited_font_size = 16.0f;
    result.inherited_font_weight = 400; result.inherited_font_style = nullptr;
    result.inherited_text_anchor = nullptr; result.visibility_hidden = false;
    SvgStyleEntry* entry = svg_style_entry((SvgStyleContext*)ctx->style_context, resource);
    lam::ArrayList<Element*> ancestors(MEM_CAT_RENDER, 0);
    for (DomNode* node = entry ? entry->node : nullptr; node && node->is_element(); node = node->parent)
        if (!ancestors.append(dom_element_to_element(node->as_element()))) break;
    for (size_t i = ancestors.size(); i > 0; i--) svg_apply_inherited_paint_attrs(&result, ancestors[i - 1]);
    if (ancestors.size() == 0) svg_apply_inherited_paint_attrs(&result, resource);
    return result;
}

static ImageSurface* svg_create_paint_surface(float width, float height);

static void svg_draw_children(SvgInlineRenderContext* ctx, Element* element, void*) {
    render_svg_children(ctx, element);
}

static bool svg_replay_capture(SvgInlineRenderContext* ctx, DisplayList* dl, ImageSurface* surface,
    const Bound* bounds, ScratchArena* scratch, int first = 0) {
    if (ctx->filter_work) {
        if (!render_svg_filter_spend_work(ctx->filter_work, (size_t)surface->width * (size_t)surface->height)) return false;
        for (int index = first; index < dl_item_count(dl); index++) {
            const DisplayItem* item = &(*dl)[index];
            if (item->op == DL_BEGIN_ELEMENT || item->op == DL_END_ELEMENT) continue;
            double width = fmax(0.0, fmin(bounds->right, item->bounds[0] + item->bounds[2]) - fmax(bounds->left, item->bounds[0]));
            double height = fmax(0.0, fmin(bounds->bottom, item->bounds[1] + item->bounds[3]) - fmax(bounds->top, item->bounds[1]));
            double work = ceil(width) * ceil(height) + 1.0;
            // capture replay and resampling share the graph budget, including prior backdrop commands.
            if (!isfinite(work) || work >= (double)SIZE_MAX || !render_svg_filter_spend_work(ctx->filter_work, (size_t)work)) return false;
        }
    }
    RdtVector vector = {};
    rdt_vector_init(&vector, (uint32_t*)surface->pixels, surface->width, surface->height, surface->width);
    bool valid = vector.impl != nullptr;
    if (valid) {
        int saved_depth = rdt_clip_save_depth();
        dl_replay_tile(dl, &vector, surface, scratch, bounds->left, bounds->top,
            (float)surface->width, (float)surface->height, ctx->raster_scale, first);
        rdt_clip_restore_depth(saved_depth);
    }
    rdt_vector_destroy(&vector);
    return valid;
}

static bool svg_export_copy_captured_semantics(DisplayList* target, const DisplayList* capture) {
    int first = target->item_count();
    lam::ArrayList<int> begins;
    bool valid = true;
    for (int i = 0; valid && i < capture->item_count(); i++) {
        const DisplayItem& item = capture->data()[i];
        if (item.op == DL_BEGIN_ELEMENT) {
            RenderSemanticGroup group = {};
            valid = dl_copy_semantic_group(target, &group, &item.element_marker.semantics);
            if (!valid) break;
            int begin = dl_begin_element(target, 0, 0, 0, 0, 0);
            valid = begin >= 0 && begins.append(begin);
            if (valid) target->data()[begin].element_marker.semantics = group;
        } else if (item.op == DL_END_ELEMENT) {
            if (begins.empty()) { valid = false; break; }
            int begin = begins.back();
            dl_end_element(target, begin);
            valid = target->data()[begin].element_marker.matching_index >= 0;
            begins.remove_range(begins.size() - 1, 1);
        }
    }
    valid = valid && begins.empty();
    if (!valid) {
        // only value-only markers were appended; rollback leaves the enclosing paint scope intact.
        target->remove_range((size_t)first, (size_t)(target->item_count() - first));
        log_error("SVG_EXPORT_METADATA: failed to copy captured semantic groups");
    }
    return valid;
}

static bool svg_rasterize_traversal(SvgInlineRenderContext* source, Element* content,
    SvgElementDrawFn draw, void* data, ImageSurface** surface, Bound* bounds, float padding = 0.0f,
    bool allow_empty = false, bool retain_semantics = false) {
    OffscreenRenderArenas arenas;
    if (!arenas.init("render.svg.capture", "render.svg.capture.list_arena",
        "render.svg.capture.scratch_arena")) return false;
    ScratchArena scratch = {};
    mem_scratch_init(nullptr, &scratch, arenas.scratch_arena, MEM_ROLE_RENDER, "render.svg.capture.scratch");
    DisplayList dl = {}; dl_init(&dl, arenas.list_arena);
    PaintList paint;
    SvgInlineRenderContext ctx = *source;
    ctx.dl = lam::up(&dl); ctx.paint_list = lam::up(&paint);
    ctx.semantic_target = lam::up(retain_semantics && source->semantic_target ? &dl : nullptr);
    ctx.backdrop_start = 0; // private effect/resource captures establish an isolated transparent backdrop.
    // fallback glyphs use the active gateway; private captures must own their commands too.
    DisplayList* saved_dl = g_svg_active_rdcon ? g_svg_active_rdcon->dl : nullptr;
    PaintList* saved_paint = g_svg_active_rdcon ? g_svg_active_rdcon->paint_list : nullptr;
    if (g_svg_active_rdcon) { g_svg_active_rdcon->dl = lam::up(&dl); g_svg_active_rdcon->paint_list = lam::up(&paint); }
    draw(&ctx, content, data);
    if (g_svg_active_rdcon) { g_svg_active_rdcon->dl = lam::up(saved_dl); g_svg_active_rdcon->paint_list = lam::up(saved_paint); }
    if (paint.item_count() > 0) paint_ir_lower_raster(&paint, &dl);
    bool valid = dl_validate_or_log(&dl, "svg_capture");
    if (valid && !*surface) {
        *bounds = dl_content_bounds(&dl);
        bounds->left = floorf(bounds->left - padding); bounds->top = floorf(bounds->top - padding);
        bounds->right = ceilf(bounds->right + padding); bounds->bottom = ceilf(bounds->bottom + padding);
        if (allow_empty && (bounds->right <= bounds->left || bounds->bottom <= bounds->top)) {
            *bounds = {};
        } else {
            *surface = svg_create_paint_surface(bounds->right - bounds->left, bounds->bottom - bounds->top);
            valid = *surface != nullptr;
        }
    }
    if (valid && *surface) valid = svg_replay_capture(&ctx, &dl, *surface, bounds, &scratch);
    // effect SourceGraphic carries its visited semantics; mask/pattern/filter resources remain private.
    if (valid && ctx.semantic_target) valid = svg_export_copy_captured_semantics(source->semantic_target, &dl);
    paint.clear(); dl_destroy(&dl); scratch_release(&scratch); arenas.destroy();
    return valid;
}

static bool svg_rasterize_resource_children(SvgInlineRenderContext* source, Element* content,
    ImageSurface* surface, const RdtMatrix* matrix) {
    SvgInlineRenderContext ctx = *source;
    ctx.transform = *matrix; ctx.viewport_transform = rdt_matrix_identity();
    ctx.viewport_width = (float)surface->width; ctx.viewport_height = (float)surface->height;
    Bound bounds = {0, 0, (float)surface->width, (float)surface->height};
    return svg_rasterize_traversal(&ctx, content, svg_draw_children, nullptr, &surface, &bounds);
}

static ImageSurface* svg_create_paint_surface(float width, float height) {
    DomDocument* document = g_svg_active_rdcon && g_svg_active_rdcon->ui_context ?
        g_svg_active_rdcon->ui_context->document : nullptr;
    return render_surface_create_budgeted(document ? (MemContext*)document->services.mem_ctx : nullptr, width, height);
}

static void svg_rasterize_path_paint(SvgInlineRenderContext* ctx, RdtPath* path,
    const RdtMatrix* transform, RdtFillRule rule, float opacity, const RdtGradientOptions* stroke,
    SvgPaintSampler sampler, void* sampler_context, const char* operation) {
    float left, top, right, bottom;
    if (!rdt_path_get_bounds(path, &left, &top, &right, &bottom)) return;
    float pad = stroke ? stroke->stroke_width * fmaxf(1.0f, stroke->miter_limit) : 0.0f;
    left -= pad; top -= pad; right += pad; bottom += pad;
    if (transform) rdt_matrix_transform_rect_bounds(transform, left, top, right, bottom, &left, &top, &right, &bottom);
    left = floorf(left); top = floorf(top); right = ceilf(right); bottom = ceilf(bottom);
    ImageSurface* output = svg_create_paint_surface(right - left, bottom - top);
    if (!output) return;
    RdtMatrix translation = rdt_matrix_translate(-left, -top);
    RdtMatrix coverage_transform = transform ? rdt_matrix_multiply(&translation, transform) : translation;
    RdtVector coverage = {};
    rdt_vector_init(&coverage, (uint32_t*)output->pixels, output->width, output->height, output->width);
    int saved_depth = rdt_clip_save_depth();
    Color white = parse_svg_color("white");
    if (stroke) rdt_stroke_path(&coverage, path, white, stroke->stroke_width, stroke->cap, stroke->join,
        stroke->dash_array, stroke->dash_count, stroke->dash_phase, &coverage_transform, stroke->miter_limit);
    else rdt_fill_path(&coverage, path, white, rule, &coverage_transform);
    rdt_clip_restore_depth(saved_depth); rdt_vector_destroy(&coverage);
    // sampling a prepared resource bounds work by destination coverage, independent of its period.
    for (int row = 0; row < output->height; row++) {
        uint32_t* pixels = (uint32_t*)((uint8_t*)output->pixels + (size_t)row * output->pitch);
        for (int column = 0; column < output->width; column++) {
            uint8_t alpha = pixels[column] >> 24;
            if (!alpha) continue;
            uint32_t sample = sampler(sampler_context, left + (float)column + 0.5f,
                top + (float)row + 0.5f);
            uint8_t source_alpha = sample >> 24;
            float coverage_alpha = (float)alpha / 255.0f * clamp_unit(opacity);
            pixels[column] = render_pixel_pack_abgr(
                clamp_byte_round((float)(sample & 255u) * coverage_alpha),
                clamp_byte_round((float)((sample >> 8) & 255u) * coverage_alpha),
                clamp_byte_round((float)((sample >> 16) & 255u) * coverage_alpha),
                clamp_byte_round((float)source_alpha * coverage_alpha));
        }
    }
    PaintRecordTarget destination = svg_record_target(ctx);
    paint_record_draw_image_resource(&destination, operation, output, left, top,
        (float)output->width, (float)output->height, 255, nullptr, image_surface_destroy);
}

typedef struct {
    ImageSurface* tile;
    RdtMatrix inverse;
} SvgPatternSample;

static uint32_t svg_sample_pattern(void* context, float x, float y) {
    SvgPatternSample* sample = (SvgPatternSample*)context;
    float u, v; rdt_matrix_transform_point(&sample->inverse, x, y, &u, &v);
    u -= floorf(u); v -= floorf(v);
    ImageSurface* tile = sample->tile;
    return render_pixel_sample_bilinear((uint8_t*)tile->pixels, tile->width, tile->height, tile->pitch,
        u * (float)tile->width - 0.5f, v * (float)tile->height - 0.5f, true, true);
}

typedef struct {
    const SvgGradDef* gradient;
    RdtMatrix inverse;
    uint32_t average;
} SvgRadialSample;

static float svg_gradient_spread(float offset, RdtGradientSpread spread) {
    if (spread == RDT_GRADIENT_REPEAT) return offset - floorf(offset);
    if (spread == RDT_GRADIENT_REFLECT) {
        offset -= floorf(offset / 2.0f) * 2.0f;
        return offset > 1.0f ? 2.0f - offset : offset;
    }
    return clamp_unit(offset);
}

static uint32_t svg_sample_gradient_stops(const SvgGradDef* gradient, float offset) {
    const RdtGradientStop* left = &gradient->stops[0];
    const RdtGradientStop* right = left;
    for (int index = 1; index < gradient->stop_count; index++) {
        if (offset < gradient->stops[index].offset) { right = &gradient->stops[index]; break; }
        left = right = &gradient->stops[index];
    }
    float t = right->offset > left->offset ? clamp_unit((offset - left->offset) / (right->offset - left->offset)) : 0.0f;
    float a = (float)left->a * (1.0f - t) + (float)right->a * t;
    return render_pixel_pack_abgr(
        clamp_byte_round(((float)left->r * (float)left->a * (1.0f - t) + (float)right->r * (float)right->a * t) / 255.0f),
        clamp_byte_round(((float)left->g * (float)left->a * (1.0f - t) + (float)right->g * (float)right->a * t) / 255.0f),
        clamp_byte_round(((float)left->b * (float)left->a * (1.0f - t) + (float)right->b * (float)right->a * t) / 255.0f),
        clamp_byte_round(a));
}

static uint32_t svg_sample_radial(void* context, float x, float y) {
    SvgRadialSample* sample = (SvgRadialSample*)context;
    const SvgGradDef* gradient = sample->gradient;
    float px, py; rdt_matrix_transform_point(&sample->inverse, x, y, &px, &py);
    double dx = (double)gradient->cx - gradient->options.fx;
    double dy = (double)gradient->cy - gradient->options.fy;
    double dr = (double)gradient->r - gradient->options.fr;
    double rx = (double)px - gradient->options.fx, ry = (double)py - gradient->options.fy;
    double a = dr * dr - dx * dx - dy * dy;
    double b = dr * gradient->options.fr + rx * dx + ry * dy;
    double c = (double)gradient->options.fr * gradient->options.fr - rx * rx - ry * ry;
    double offset;
    if (a == 0.0) {
        if (b == 0.0) return sample->average;
        offset = -c / (2.0 * b);
        if (gradient->options.fr + offset * dr < 0.0) return sample->average;
    } else {
        double determinant = b * b - a * c;
        if (determinant < 0.0) return 0u;
        double root = sqrt(determinant);
        double first = (-b + root) / a, second = (-b - root) / a;
        bool first_valid = gradient->options.fr + first * dr >= 0.0;
        bool second_valid = gradient->options.fr + second * dr >= 0.0;
        if (!first_valid && !second_valid) return 0u;
        offset = first_valid && second_valid ? fmax(first, second) : first_valid ? first : second;
    }
    if (!isfinite(offset)) return 0u;
    return svg_sample_gradient_stops(gradient, svg_gradient_spread((float)offset, gradient->options.spread));
}

static void svg_draw_radial_fallback(SvgInlineRenderContext* ctx, RdtPath* path, const SvgGradDef* gradient,
    const RdtMatrix* transform, RdtFillRule rule, float opacity, const RdtGradientOptions* stroke) {
    if (gradient->cx == gradient->options.fx && gradient->cy == gradient->options.fy && gradient->r == gradient->options.fr) return;
    RdtMatrix frame = transform ? rdt_matrix_multiply(transform, &gradient->gradient_transform) : gradient->gradient_transform;
    SvgRadialSample sample = {}; sample.gradient = gradient;
    if (!rdt_matrix_invert_affine(&frame, &sample.inverse)) return;
    float distance = hypotf(gradient->cx - gradient->options.fx, gradient->cy - gradient->options.fy);
    if (gradient->options.fr == 0.0f && distance == gradient->r) {
        if (gradient->options.spread == RDT_GRADIENT_PAD) sample.average = svg_sample_gradient_stops(gradient, 1.0f);
        else {
            float channels[4] = {};
            float previous = 0.0f;
            // integrate each linear stop interval; duplicate offsets contribute zero area.
            for (int index = 0; index <= gradient->stop_count; index++) {
                float next = index < gradient->stop_count ? gradient->stops[index].offset : 1.0f;
                uint32_t left = svg_sample_gradient_stops(gradient, previous);
                uint32_t right = svg_sample_gradient_stops(gradient, next == previous ? next : nextafterf(next, previous));
                for (int channel = 0; channel < 4; channel++)
                    channels[channel] += (next - previous) * 0.5f * ((float)((left >> (channel * 8)) & 255u) + (float)((right >> (channel * 8)) & 255u));
                previous = next;
            }
            sample.average = render_pixel_pack_abgr(clamp_byte_round(channels[0]), clamp_byte_round(channels[1]),
                clamp_byte_round(channels[2]), clamp_byte_round(channels[3]));
        }
    }
    svg_rasterize_path_paint(ctx, path, transform, rule, opacity, stroke, svg_sample_radial, &sample, "svg_radial_cone");
}

static bool draw_pattern_fill(SvgInlineRenderContext* ctx, RdtPath* path, Element* pattern_elem,
                              float bx, float by, float bw, float bh,
                              const RdtMatrix* transform, RdtFillRule rule, float opacity,
                              const RdtGradientOptions* stroke = nullptr,
                              const RdtMatrix* resource_frame = nullptr) {
    const char* tag = pattern_elem ? get_element_tag_name(ctx, pattern_elem) : nullptr;
    if (!ctx || !path || !tag || strcmp(tag, "pattern") != 0) return false;
    const SvgPaintResourceScope* parent = (SvgPaintResourceScope*)ctx->paint_resource_scope;
    int depth = 0;
    for (const SvgPaintResourceScope* item = parent; item; item = item->parent) {
        if (item->element == pattern_elem || ++depth >= SVG_USE_DEPTH_MAX) return false;
    }
    SvgPaintResourceScope scope = {pattern_elem, parent};
    lam::ArrayList<SvgResourceReference> chain(MEM_CAT_RENDER, 0);
    svg_template_chain(ctx, pattern_elem, &chain, false);
    Element* content = nullptr;
    SvgInlineRenderContext content_context = *ctx;
    for (size_t i = 0; i < chain.size() && !content; i++) {
        SvgInlineRenderContext source = svg_reference_render_context(ctx, &chain[i]);
        for (int64_t j = 0; j < chain[i].element->length; j++) {
            Element* child = get_child_element_at(chain[i].element, j);
            const char* name = child ? get_element_tag_name(&source, child) : nullptr;
            if (name && strcmp(name, "title") != 0 && strcmp(name, "desc") != 0 && strcmp(name, "metadata") != 0)
                { content = chain[i].element; content_context = svg_reference_render_context(ctx, &chain[i]); break; }
        }
    }
    if (!content) return false;
    bool user_space = strcmp(svg_template_attribute(ctx, &chain, "patternUnits", "objectBoundingBox"), "userSpaceOnUse") == 0;
    bool content_bbox = strcmp(svg_template_attribute(ctx, &chain, "patternContentUnits", "userSpaceOnUse"), "objectBoundingBox") == 0;
    if ((!user_space || content_bbox) && (bw <= 0.0f || bh <= 0.0f)) return true;
    SvgInlineRenderContext resource = svg_resource_style_context(ctx, pattern_elem);
    // template children retain their own selector/document context and inherit the host's paint state.
    resource.style_context = content_context.style_context; resource.source_path = content_context.source_path;
    resource.id_scope = content_context.id_scope; resource.font_ctx = content_context.font_ctx;
    resource.paint_resource_scope = lam::up(&scope);
    SvgLengthContext lengths = svg_length_context(&resource);
    if (!user_space) lengths.viewport_width = lengths.viewport_height = 1.0f;
    float x = svg_resolve_length(svg_template_attribute(ctx, &chain, "x", "0"), &lengths, SVG_LENGTH_X, 0.0f);
    float y = svg_resolve_length(svg_template_attribute(ctx, &chain, "y", "0"), &lengths, SVG_LENGTH_Y, 0.0f);
    float width = svg_resolve_length(svg_template_attribute(ctx, &chain, "width", "0"), &lengths, SVG_LENGTH_X, 0.0f);
    float height = svg_resolve_length(svg_template_attribute(ctx, &chain, "height", "0"), &lengths, SVG_LENGTH_Y, 0.0f);
    if (width <= 0.0f || height <= 0.0f) return true;
    RdtMatrix authored = rdt_matrix_identity();
    for (size_t i = 0; i < chain.size(); i++) {
        SvgInlineRenderContext candidate_context = svg_reference_render_context(ctx, &chain[i]);
        const char* value = svg_style_property_value(&candidate_context, chain[i].element, "transform");
        bool css = value != nullptr;
        if (!value) value = get_svg_attr(chain[i].element, "patternTransform");
        if (!value) continue;
        Bound box = {0.0f, 0.0f, lengths.viewport_width, lengths.viewport_height};
        authored = svg_resolve_local_transform(value, css,
            svg_style_property_value(&candidate_context, chain[i].element, "transform-origin"), &box, &lengths);
        break;
    }
    RdtMatrix basis = user_space ? rdt_matrix_identity() : RdtMatrix{bw, 0.0f, bx, 0.0f, bh, by, 0.0f, 0.0f, 1.0f};
    RdtMatrix tiles = rdt_matrix_multiply(&basis, &authored);
    RdtMatrix origin = rdt_matrix_translate(x, y); tiles = rdt_matrix_multiply(&tiles, &origin);
    RdtMatrix size = {width, 0.0f, 0.0f, 0.0f, height, 0.0f, 0.0f, 0.0f, 1.0f};
    tiles = rdt_matrix_multiply(&tiles, &size);
    RdtMatrix target = resource_frame ? *resource_frame : transform ? *transform : rdt_matrix_identity();
    tiles = rdt_matrix_multiply(&target, &tiles);
    RdtMatrix inverse;
    if (!rdt_matrix_invert_affine(&tiles, &inverse)) return true;
    ImageSurface* tile = svg_create_paint_surface(fmaxf(1.0f, hypotf(tiles.e11, tiles.e21)),
        fmaxf(1.0f, hypotf(tiles.e12, tiles.e22)));
    if (!tile) return true;
    SvgViewBox viewbox = svg_parse_viewbox(svg_template_attribute(ctx, &chain, "viewBox", nullptr));
    RdtMatrix content_matrix = rdt_matrix_identity();
    if (viewbox.has_viewbox) {
        if (viewbox.width <= 0.0f || viewbox.height <= 0.0f) { image_surface_destroy(tile); return true; }
        content_matrix = svg_viewbox_transform(&viewbox, width, height,
            svg_template_attribute(ctx, &chain, "preserveAspectRatio", nullptr));
        resource.current_viewport_w = viewbox.width; resource.current_viewport_h = viewbox.height;
    } else {
        content_matrix.e11 = content_bbox ? (user_space ? bw : 1.0f) : user_space ? 1.0f : 1.0f / bw;
        content_matrix.e22 = content_bbox ? (user_space ? bh : 1.0f) : user_space ? 1.0f : 1.0f / bh;
        if (content_bbox) resource.current_viewport_w = resource.current_viewport_h = 1.0f;
    }
    RdtMatrix raster = {(float)tile->width / width, 0.0f, 0.0f,
        0.0f, (float)tile->height / height, 0.0f, 0.0f, 0.0f, 1.0f};
    raster = rdt_matrix_multiply(&raster, &content_matrix);
    if (!svg_rasterize_resource_children(&resource, content, tile, &raster)) { image_surface_destroy(tile); return true; }
    SvgPatternSample sample = {tile, inverse};
    svg_rasterize_path_paint(ctx, path, transform, rule, opacity, stroke,
        svg_sample_pattern, &sample, "svg_pattern_image");
    image_surface_destroy(tile);
    return true;
}

// ============================================================================
// Draw fill and stroke for an SVG shape via the SVG painter gateway
// ============================================================================

static void svg_draw_resolved_paint(SvgInlineRenderContext* ctx, RdtPath* path,
    const SvgPaint* paint, const RdtMatrix* transform, float bx, float by, float bw, float bh,
    RdtFillRule rule, float alpha, const RdtGradientOptions* stroke = nullptr, int depth = 0,
    const RdtMatrix* resource_frame = nullptr) {
    if (!paint || paint->kind == SVG_PAINT_NONE || depth >= SVG_USE_DEPTH_MAX) return;
    if (paint->kind == SVG_PAINT_CONTEXT_FILL || paint->kind == SVG_PAINT_CONTEXT_STROKE) {
        if (!ctx->context_paint) return;
        const SvgContextPaint* context = ctx->context_paint;
        const SvgPaint* contextual = paint->kind == SVG_PAINT_CONTEXT_FILL ? &context->fill : &context->stroke;
        // SVG 2 context paint keeps the producer's bounds, document and coordinate frame.
        SvgInlineRenderContext source = context->source_context ? *context->source_context : *ctx;
        source.dl = ctx->dl; source.paint_list = ctx->paint_list;
        source.resource_scratch = ctx->resource_scratch; source.current_color = context->current_color;
        const Bound* box = &context->geometry_box;
        svg_draw_resolved_paint(&source, path, contextual, transform, box->left, box->top,
            box->right - box->left, box->bottom - box->top, rule, alpha, stroke, depth + 1, &context->transform);
        return;
    }
    if (paint->kind == SVG_PAINT_RESOURCE) {
        SvgInlineRenderContext declaration = *ctx;
        if (paint->source_style) { declaration.style_context = lam::up((SvgStyleContext*)paint->source_style); declaration.source_path = lam::up(paint->source_path); }
        SvgResourceReference reference = svg_resolve_reference(&declaration, paint->reference);
        Element* resource = reference.element;
        SvgInlineRenderContext source = svg_reference_render_context(&declaration, &reference);
        SvgGradDef gradient = {};
        if (svg_resolve_gradient(&source, resource, bx, by, bw, bh, &gradient)) {
            if (resource_frame && transform) {
                RdtMatrix inverse;
                if (!rdt_matrix_invert_affine(transform, &inverse)) return;
                RdtMatrix relative = rdt_matrix_multiply(&inverse, resource_frame);
                gradient.gradient_transform = rdt_matrix_multiply(&relative, &gradient.gradient_transform);
            }
            draw_gradient_fill(&source, path, &gradient, bx, by, bw, bh, transform, rule, alpha, stroke);
            return;
        }
        if (resource && draw_pattern_fill(&source, path, resource, bx, by, bw, bh, transform, rule, alpha, stroke, resource_frame)) return;
        if (paint->fallback) {
            SvgPaint fallback = svg_parse_paint(ctx, paint->fallback, paint->source, stroke != nullptr);
            svg_draw_resolved_paint(ctx, path, &fallback, transform, bx, by, bw, bh, rule, alpha, stroke, depth + 1, resource_frame);
        }
        return;
    }
    Color color = svg_paint_color(ctx, paint);
    color.a = clamp_byte_round((float)color.a * clamp_unit(alpha));
    if (stroke) {
        if (stroke->stroke_width > 0.0f) svg_stroke_path(ctx, path, color, stroke->stroke_width,
            stroke->cap, stroke->join, stroke->dash_array, stroke->dash_count, stroke->dash_phase, transform, stroke->miter_limit);
    } else svg_fill_path(ctx, path, color, rule, transform);
}

struct SvgSegmentTangents { float start_x, start_y, end_x, end_y; };
struct SvgPathVertex {
    float x, y, incoming_x, incoming_y, outgoing_x, outgoing_y;
    size_t subpath;
    bool closed;
};
struct SvgPathTopology {
    lam::ArrayList<SvgPathVertex> vertices{MEM_CAT_RENDER, 0};
    size_t subpath_start = 0;
    bool valid = true;
};

static bool svg_vector_nonzero(float x, float y) { return x != 0.0f || y != 0.0f; }

static SvgSegmentTangents svg_cubic_tangents(float sx, float sy, float x1, float y1,
    float x2, float y2, float x, float y) {
    SvgSegmentTangents result = {x1 - sx, y1 - sy, x - x2, y - y2};
    if (!svg_vector_nonzero(result.start_x, result.start_y)) { result.start_x = x2 - sx; result.start_y = y2 - sy; }
    if (!svg_vector_nonzero(result.start_x, result.start_y)) { result.start_x = x - sx; result.start_y = y - sy; }
    if (!svg_vector_nonzero(result.end_x, result.end_y)) { result.end_x = x - x1; result.end_y = y - y1; }
    if (!svg_vector_nonzero(result.end_x, result.end_y)) { result.end_x = x - sx; result.end_y = y - sy; }
    return result;
}

static void svg_topology_move(SvgPathTopology* topology, float x, float y) {
    if (!topology || !topology->valid) return;
    topology->subpath_start = topology->vertices.size();
    topology->valid = topology->vertices.append({x, y, 0, 0, 0, 0, topology->subpath_start, false});
}

static void svg_topology_segment(SvgPathTopology* topology, float x, float y,
    const SvgSegmentTangents& tangents, bool close = false) {
    if (!topology || !topology->valid || topology->vertices.empty()) return;
    SvgPathVertex& previous = topology->vertices.back();
    previous.outgoing_x = tangents.start_x; previous.outgoing_y = tangents.start_y;
    topology->valid = topology->vertices.append({x, y, tangents.end_x, tangents.end_y,
        0, 0, topology->subpath_start, close});
    if (close && topology->valid) {
        SvgPathVertex& first = topology->vertices[topology->subpath_start];
        first.incoming_x = tangents.end_x; first.incoming_y = tangents.end_y;
        topology->vertices.back().outgoing_x = first.outgoing_x;
        topology->vertices.back().outgoing_y = first.outgoing_y;
        for (size_t i = topology->subpath_start; i < topology->vertices.size(); i++) topology->vertices[i].closed = true;
    }
}

static void render_svg_markers(SvgInlineRenderContext* ctx, Element* elem, RdtPath* path,
    const SvgPathTopology* topology, const RdtMatrix* transform,
    const SvgContextPaint* producer, const RdtGradientOptions* stroke);

static RdtGradientOptions svg_stroke_options(SvgInlineRenderContext* ctx, Element* elem, bool apply_font) {
    RdtGradientOptions result = {};
    SvgInheritedProperty width = svg_computed_property(ctx, elem, SVG_STYLE_STROKE_WIDTH, apply_font);
    result.stroke_width = width.value ? svg_resolve_length(width.value, &width.lengths, SVG_LENGTH_DIAGONAL, ctx->stroke_width)
        : get_svg_number_attr(elem, "stroke-width", ctx->stroke_width, &width.lengths, SVG_LENGTH_DIAGONAL);
    result.stroke_width = fmaxf(0.0f, result.stroke_width);
    const char* cap = svg_computed_property(ctx, elem, SVG_STYLE_STROKE_CAP, apply_font).value;
    result.cap = cap && strcmp(cap, "round") == 0 ? RDT_CAP_ROUND
        : cap && strcmp(cap, "square") == 0 ? RDT_CAP_SQUARE : RDT_CAP_BUTT;
    const char* join = svg_computed_property(ctx, elem, SVG_STYLE_STROKE_JOIN, apply_font).value;
    result.join = join && strcmp(join, "round") == 0 ? RDT_JOIN_ROUND
        : join && strcmp(join, "bevel") == 0 ? RDT_JOIN_BEVEL : RDT_JOIN_MITER;
    result.miter_limit = parse_svg_pct_or_num(svg_computed_property(ctx, elem, SVG_STYLE_MITER_LIMIT, apply_font).value, 4.0f);
    if (!isfinite(result.miter_limit) || result.miter_limit < 0.0f) result.miter_limit = 4.0f;
    // SVG 2 permits limits below one; every miter then exceeds the limit and falls back to a bevel.
    if (result.join == RDT_JOIN_MITER && result.miter_limit < 1.0f) result.join = RDT_JOIN_BEVEL;
    SvgInheritedProperty dashes = svg_computed_property(ctx, elem, SVG_STYLE_DASH_ARRAY, apply_font);
    result.dash_count = svg_resolve_dash_array(dashes.value, &dashes.lengths, nullptr, 0);
    if (result.dash_count > 0) {
        float* values = (float*)scratch_alloc(ctx->resource_scratch, (size_t)result.dash_count * sizeof(float));
        if (values) result.dash_count = svg_resolve_dash_array(dashes.value, &dashes.lengths, values, result.dash_count);
        result.dash_array = lam::up(values);
    }
    SvgInheritedProperty offset = svg_computed_property(ctx, elem, SVG_STYLE_DASH_OFFSET, apply_font);
    result.dash_phase = svg_resolve_length(offset.value, &offset.lengths, SVG_LENGTH_DIAGONAL, 0.0f);
    return result;
}

static void svg_paint_order(const char* value, uint8_t order[3]) {
    static const char* const names[] = {"fill", "stroke", "markers"};
    size_t count = 0;
    bool used[3] = {};
    const char* cursor = value ? str_skip_ascii_space(value) : "";
    if (strcmp(cursor, "normal") != 0) while (*cursor) {
        const char* start = cursor;
        while (*cursor && !str_char_is_ascii_space(*cursor)) cursor++;
        size_t length = (size_t)(cursor - start), operation = 3;
        for (size_t i = 0; i < 3; i++) if (strlen(names[i]) == length && strncmp(start, names[i], length) == 0) operation = i;
        if (operation == 3 || used[operation]) { count = 0; memset(used, 0, sizeof(used)); break; }
        order[count++] = (uint8_t)operation; used[operation] = true;
        cursor = str_skip_ascii_space(cursor);
    }
    for (size_t i = 0; i < 3; i++) if (!used[i]) order[count++] = (uint8_t)i;
}

static void draw_svg_fill_stroke(SvgInlineRenderContext* ctx, RdtPath* path, Element* elem,
    const RdtMatrix* transform, float bx, float by, float bw, float bh, bool apply_effects = true,
    const char* fill_override = nullptr, const char* stroke_override = nullptr,
    const RdtMatrix* resource_frame = nullptr, const SvgPathTopology* topology = nullptr) {
    if (!path || !elem) return;
    if (ctx->clip_geometry) {
        const char* clip_rule = svg_style_ancestor_property_value(ctx, elem, "clip-rule");
        RdtFillRule rule = clip_rule && strcmp(clip_rule, "evenodd") == 0 ? RDT_FILL_EVEN_ODD : RDT_FILL_WINDING;
        if (ctx->clip_hit_query) ctx->clip_hit_query->hit |=
            ctx->clip_hit_query->contains(path, transform, rule, ctx->clip_hit_query->point);
        else svg_fill_path(ctx, path, parse_svg_color("white"), rule, transform);
        return;
    }
    bool own_effects = apply_effects && ctx->effect_source != elem;
    Color saved_color = ctx->current_color;
    const char* color = svg_style_property_value(ctx, elem, "color");
    if (color && str_icmp_cstr(color, "currentColor") != 0) ctx->current_color = parse_svg_color(color);
    const char* value = fill_override ? fill_override : svg_style_property_value(ctx, elem, "fill");
    SvgPaint fill = value ? svg_parse_paint(ctx, value, elem) : ctx->fill_paint;
    value = stroke_override ? stroke_override : svg_style_property_value(ctx, elem, "stroke");
    SvgPaint stroke = value ? svg_parse_paint(ctx, value, elem, true) : ctx->stroke_paint;
    const char* opacity = own_effects ? svg_style_property_value(ctx, elem, "opacity") : nullptr;
    float element_alpha = ctx->opacity * clamp_unit(parse_svg_pct_or_num(opacity, 1.0f));
    const char* fill_opacity = svg_style_property_value(ctx, elem, "fill-opacity");
    float fill_alpha = element_alpha * (fill_opacity ? clamp_unit(parse_svg_pct_or_num(fill_opacity, 1.0f)) : ctx->fill_opacity);
    const char* stroke_opacity = svg_style_property_value(ctx, elem, "stroke-opacity");
    float stroke_alpha = element_alpha * (stroke_opacity ? clamp_unit(parse_svg_pct_or_num(stroke_opacity, 1.0f)) : ctx->stroke_opacity);
    const char* fill_rule = svg_computed_property(ctx, elem, SVG_STYLE_FILL_RULE, apply_effects).value;
    RdtFillRule rule = fill_rule && strcmp(fill_rule, "evenodd") == 0 ? RDT_FILL_EVEN_ODD : RDT_FILL_WINDING;
    RdtGradientOptions options = svg_stroke_options(ctx, elem, apply_effects);
    SvgInlineRenderContext source_context = *ctx;
    SvgContextPaint producer = {fill, stroke, ctx->current_color, {bx, by, bx + bw, by + bh},
        transform ? *transform : rdt_matrix_identity(), lam::up(&source_context)};
    uint8_t order[3];
    svg_paint_order(svg_computed_property(ctx, elem, SVG_STYLE_PAINT_ORDER, apply_effects).value, order);
    for (size_t operation = 0; operation < 3; operation++) {
        if (order[operation] == 0) {
            svg_draw_resolved_paint(ctx, path, &fill, transform, bx, by, bw, bh, rule, fill_alpha, nullptr, 0, resource_frame);
        } else if (order[operation] == 1 && stroke.kind != SVG_PAINT_NONE && options.stroke_width > 0.0f) {
            RdtPath* stroke_path = path;
            RdtPath* transformed_path = nullptr;
            RdtMatrix identity = rdt_matrix_identity();
            const RdtMatrix* stroke_transform = transform;
            const RdtMatrix* stroke_frame = resource_frame;
            RdtGradientOptions stroke_options = options;
            const char* effect = svg_style_property_value(ctx, elem, "vector-effect");
            if (effect && strcmp(effect, "non-scaling-stroke") == 0 && transform) {
                transformed_path = rdt_path_new();
                if (!transformed_path || !render_path_append_transformed(transformed_path, path, transform)) {
                    if (transformed_path) rdt_path_free(transformed_path);
                    continue;
                }
                // stroke in target space; its paint server retains the shape's original user frame.
                stroke_path = transformed_path; stroke_transform = &identity;
                stroke_frame = resource_frame ? resource_frame : transform;
                float scale = ctx->raster_scale > 0.0f ? ctx->raster_scale : 1.0f;
                stroke_options.stroke_width *= scale; stroke_options.dash_phase *= scale;
                if (options.dash_count > 0 && options.dash_array) {
                    float* dashes = (float*)scratch_alloc(ctx->resource_scratch, (size_t)options.dash_count * sizeof(float));
                    if (!dashes) { rdt_path_free(transformed_path); continue; }
                    for (int i = 0; i < options.dash_count; i++) dashes[i] = options.dash_array[i] * scale;
                    stroke_options.dash_array = lam::up(dashes);
                }
            }
            svg_draw_resolved_paint(ctx, stroke_path, &stroke, stroke_transform, bx, by, bw, bh,
                rule, stroke_alpha, &stroke_options, 0, stroke_frame);
            if (transformed_path) rdt_path_free(transformed_path);
        } else if (order[operation] == 2) {
            render_svg_markers(ctx, elem, path, topology, transform, &producer, &options);
        }
    }
    ctx->current_color = saved_color;
}

// ============================================================================
// SVG Shape Renderers
// ============================================================================

// helper: parse points attribute for polyline/polygon into RdtPath
static bool parse_points_to_path(const char* points_str, RdtPath* path, bool close_path) {
    if (!points_str || !path) return false;

    const char* p = points_str;
    float x, y;
    bool first = true;

    while (*p) {
        float point[2];
        if (str_parse_float_list(p, ", \t\n\r\f\v", point, 2, &p) != 2) break;
        x = point[0];
        y = point[1];

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

static void svg_path_add_ellipse(RdtPath* path, float cx, float cy,
                                        float rx, float ry) {
    if (!path || rx <= 0.0f || ry <= 0.0f) return;
    const float kappa = 0.5522847498307936f;
    rdt_path_move_to(path, cx + rx, cy);
    rdt_path_cubic_to(path, cx + rx, cy + kappa * ry,
        cx + kappa * rx, cy + ry, cx, cy + ry);
    rdt_path_cubic_to(path, cx - kappa * rx, cy + ry,
        cx - rx, cy + kappa * ry, cx - rx, cy);
    rdt_path_cubic_to(path, cx - rx, cy - kappa * ry,
        cx - kappa * rx, cy - ry, cx, cy - ry);
    rdt_path_cubic_to(path, cx + kappa * rx, cy - ry,
        cx + rx, cy - kappa * ry, cx + rx, cy);
    rdt_path_close(path);
}


// Appends the simple SVG primitives shared by normal painting, masks, and clips.
bool svg_append_basic_shape_path(DomElement* node, RdtPath* path,
    const SvgLengthContext* lengths, SvgBasicShapeGeometry* geometry) {
    if (!node || !path || !dom_element_is_svg(node)) return false;
    Element* elem = dom_element_render_source(node);
    if (!elem) elem = dom_element_to_element(node);
    const char* tag = node->local_name();
    SvgBasicShapeGeometry result = {};

    if (strcmp(tag, "rect") == 0) {
        float rx = 0.0f, ry = 0.0f;
        bool has_rx = read_svg_number_attr(elem, "rx", &rx, lengths, SVG_LENGTH_X) && rx >= 0.0f;
        bool has_ry = read_svg_number_attr(elem, "ry", &ry, lengths, SVG_LENGTH_Y) && ry >= 0.0f;
        // either auto radius takes the other's used length before clamping.
        if (!has_rx) rx = has_ry ? ry : 0.0f;
        if (!has_ry) ry = rx;
        result.x = get_svg_number_attr(elem, "x", 0.0f, lengths, SVG_LENGTH_X);
        result.y = get_svg_number_attr(elem, "y", 0.0f, lengths, SVG_LENGTH_Y);
        result.width = get_svg_number_attr(elem, "width", 0.0f, lengths, SVG_LENGTH_X);
        result.height = get_svg_number_attr(elem, "height", 0.0f, lengths, SVG_LENGTH_Y);
        if (result.width <= 0.0f || result.height <= 0.0f) return false;
        rx = fminf(rx, result.width * 0.5f);
        ry = fminf(ry, result.height * 0.5f);
        if (rx <= 0.0f || ry <= 0.0f) rx = ry = 0.0f;
        Corner radius = {};
        radius.top_left = radius.top_right = radius.bottom_left = radius.bottom_right = rx;
        radius.top_left_y = radius.top_right_y = radius.bottom_left_y = radius.bottom_right_y = ry;
        // contours serve paint, markers and hit testing through the same visitor.
        render_path_append_rounded_rect(path, {result.x, result.y, result.width, result.height}, &radius, true);
    } else if (strcmp(tag, "circle") == 0 || strcmp(tag, "ellipse") == 0) {
        float rx = get_svg_number_attr(elem, strcmp(tag, "circle") == 0 ? "r" : "rx", 0.0f, lengths,
            strcmp(tag, "circle") == 0 ? SVG_LENGTH_DIAGONAL : SVG_LENGTH_X);
        float ry = strcmp(tag, "circle") == 0 ? rx
            : get_svg_number_attr(elem, "ry", 0.0f, lengths, SVG_LENGTH_Y);
        if (rx <= 0.0f || ry <= 0.0f) return false;
        float cx = get_svg_number_attr(elem, "cx", 0.0f, lengths, SVG_LENGTH_X);
        float cy = get_svg_number_attr(elem, "cy", 0.0f, lengths, SVG_LENGTH_Y);
        result.x = cx - rx;
        result.y = cy - ry;
        result.width = 2.0f * rx;
        result.height = 2.0f * ry;
        svg_path_add_ellipse(path, cx, cy, rx, ry);
    } else if (strcmp(tag, "line") == 0) {
        float x1 = get_svg_number_attr(elem, "x1", 0.0f, lengths, SVG_LENGTH_X);
        float y1 = get_svg_number_attr(elem, "y1", 0.0f, lengths, SVG_LENGTH_Y);
        float x2 = get_svg_number_attr(elem, "x2", 0.0f, lengths, SVG_LENGTH_X);
        float y2 = get_svg_number_attr(elem, "y2", 0.0f, lengths, SVG_LENGTH_Y);
        rdt_path_move_to(path, x1, y1);
        rdt_path_line_to(path, x2, y2);
    } else if (strcmp(tag, "polygon") == 0 || strcmp(tag, "polyline") == 0) {
        const char* points = get_svg_attr(elem, "points");
        if (!points || !parse_points_to_path(points, path, strcmp(tag, "polygon") == 0)) {
            return false;
        }
    } else {
        return false;
    }

    if (geometry) {
        // every primitive supplies its actual bounds to objectBoundingBox paint servers.
        float right, bottom;
        if (rdt_path_get_bounds(path, &result.x, &result.y, &right, &bottom)) {
            result.width = right - result.x;
            result.height = bottom - result.y;
        }
        *geometry = result;
    }
    return true;
}

static void render_svg_basic_shape(SvgInlineRenderContext* ctx, Element* elem) {
    RdtPath* path = rdt_path_new();
    SvgBasicShapeGeometry geometry = {};
    SvgLengthContext lengths = svg_length_context(ctx, elem);
    if (!path || !svg_append_basic_shape_path(svg_style_node((SvgStyleContext*)ctx->style_context, elem), path, &lengths, &geometry)) {
        if (path) rdt_path_free(path);
        return;
    }

    RdtMatrix matrix = compose_element_transform(ctx, elem);
    // lines use the same computed stroke as other shapes; the initial paint is none.
    draw_svg_fill_stroke(ctx, path, elem, &matrix,
                         geometry.x, geometry.y, geometry.width, geometry.height);
    rdt_path_free(path);
}

// ============================================================================
// SVG Path Rendering
// ============================================================================

static bool parse_number(const char** p, float* result) {
    const char* start = *p;
    const char* end = start;
    if (*end == '+' || *end == '-') end++;
    bool digits = false;
    while (str_char_is_digit(*end)) { digits = true; end++; }
    if (*end == '.') {
        end++;
        while (str_char_is_digit(*end)) { digits = true; end++; }
    }
    if (!digits) return false;
    if (*end == 'e' || *end == 'E') {
        end++;
        if (*end == '+' || *end == '-') end++;
        if (!str_char_is_digit(*end)) return false;
        while (str_char_is_digit(*end)) end++;
    }
    // strtof also accepts hex/inf; SVG path numbers permit finite decimal syntax only.
    char* parsed_end = nullptr;
    float value = strtof(start, &parsed_end);
    if (parsed_end != end || !isfinite(value)) return false;
    *p = end;
    *result = value;
    return true;
}

int svg_path_parameter_count(char command) {
    switch (command) {
        case 'M': case 'L': case 'T': return 2;
        case 'H': case 'V': return 1;
        case 'S': case 'Q': return 4;
        case 'C': return 6;
        case 'A': return 7;
        default: return -1;
    }
}

bool svg_read_path_parameters(const char** p, char command,
                                      bool repeated, float args[7]) {
    int count = svg_path_parameter_count(command);
    if (count < 0) return false;
    // commit geometry only after a whole segment parses; a bad suffix keeps its prefix.
    for (int i = 0; i < count; i++) {
        *p = str_skip_ascii_space(*p);
        if ((i > 0 || repeated) && **p == ',') {
            *p = str_skip_ascii_space(*p + 1);
        }
        if (command == 'A' && (i == 3 || i == 4)) {
            if (**p != '0' && **p != '1') return false;
            args[i] = (float)(**p - '0');
            (*p)++;
        } else if (!parse_number(p, &args[i])) {
            return false;
        }
    }
    return true;
}

static void svg_path_resolve_relative_parameters(char command, float args[7],
                                                  float x, float y) {
    int count = svg_path_parameter_count(command);
    if (count == 1) {
        args[0] += command == 'H' ? x : y;
        return;
    }
    // arc radii, rotation and flags are not coordinates.
    for (int i = command == 'A' ? 5 : 0; i < count; i += 2) {
        args[i] += x;
        args[i + 1] += y;
    }
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

    // commas separate parameters, never commands, even on the rectangle fast path.
    p = str_skip_ascii_space(p);
    if (*p != 'M') return false;
    p++;
    float point[7];
    if (!svg_read_path_parameters(&p, 'M', false, point)) return false;
    xs[0] = point[0];
    ys[0] = point[1];

    for (size_t i = 1; i < 4; i++) {
        p = str_skip_ascii_space(p);
        if (*p != 'L') return false;
        p++;
        if (!svg_read_path_parameters(&p, 'L', false, point)) return false;
        xs[i] = point[0];
        ys[i] = point[1];
    }

    p = str_skip_ascii_space(p);
    if (*p != 'Z') return false;
    p++;
    p = str_skip_ascii_space(p);
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
                           int large_arc, int sweep, float x2, float y2, SvgSegmentTangents* tangents = nullptr) {
    // either zero radius is a line; the ellipse conversion would divide by zero.
    if ((x1 == x2 && y1 == y2) || rx == 0.0f || ry == 0.0f) {
        rdt_path_line_to(path, x2, y2);
        if (tangents) *tangents = {x2 - x1, y2 - y1, x2 - x1, y2 - y1};
        return;
    }

    rx = fabsf(rx);
    ry = fabsf(ry);

    float phi = math_degrees_to_radians(x_rotation);
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

    if (!sweep && dtheta > 0) dtheta -= math_tau_f();
    if (sweep && dtheta < 0)  dtheta += math_tau_f();

    // split arc into segments of at most PI/2 and approximate each with a cubic bezier
    int n_segs = (int)ceilf(fabsf(dtheta) / (math_pi_f() * 0.5f));
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

        if (tangents) {
            if (i == 0) { tangents->start_x = tx(cp1x, cp1y) - x1; tangents->start_y = ty(cp1x, cp1y) - y1; }
            if (i == n_segs - 1) { tangents->end_x = x2 - tx(cp2x, cp2y); tangents->end_y = y2 - ty(cp2x, cp2y); }
        }
        rdt_path_cubic_to(path,
                           tx(cp1x, cp1y), ty(cp1x, cp1y),
                           tx(cp2x, cp2y), ty(cp2x, cp2y),
                           tx(ep2x, ep2y), ty(ep2x, ep2y));

        prev_cos = next_cos;
        prev_sin = next_sin;
    }
}

typedef struct SvgPathEmitContext {
    RdtPath* path;
    float* cur_x;
    float* cur_y;
    float* pending_x;
    float* pending_y;
    float* last_ctrl_x;
    float* last_ctrl_y;
    bool* pending_move;
    bool* subpath_has_draw;
    bool* any_draw;
    SvgPathTopology* topology;
} SvgPathEmitContext;

static void svg_path_emit_segment(SvgPathEmitContext* context, float x, float y,
    const SvgSegmentTangents& tangents) {
    *context->cur_x = x; *context->cur_y = y;
    *context->subpath_has_draw = true; *context->any_draw = true;
    svg_topology_segment(context->topology, x, y, tangents);
}

static void svg_path_emit_line(SvgPathEmitContext* context, float x, float y) {
    SvgSegmentTangents tangent = {x - *context->cur_x, y - *context->cur_y,
        x - *context->cur_x, y - *context->cur_y};
    svg_emit_pending_move(context->path, context->pending_move, *context->pending_x, *context->pending_y);
    // zero-length segments still own caps and marker vertices under SVG 2 §9.5.3.
    rdt_path_line_to(context->path, x, y);
    svg_path_emit_segment(context, x, y, tangent);
}

static void svg_path_emit_cubic(SvgPathEmitContext* context,
    float x1, float y1, float x2, float y2, float x, float y) {
    SvgSegmentTangents tangents = svg_cubic_tangents(*context->cur_x, *context->cur_y, x1, y1, x2, y2, x, y);
    svg_emit_pending_move(context->path, context->pending_move, *context->pending_x, *context->pending_y);
    rdt_path_cubic_to(context->path, x1, y1, x2, y2, x, y);
    svg_path_emit_segment(context, x, y, tangents);
    *context->last_ctrl_x = x2; *context->last_ctrl_y = y2;
}

static void svg_path_emit_quadratic(SvgPathEmitContext* context, float qx, float qy, float x, float y) {
    SvgSegmentTangents tangents = {qx - *context->cur_x, qy - *context->cur_y, x - qx, y - qy};
    if (!svg_vector_nonzero(tangents.start_x, tangents.start_y)) { tangents.start_x = x - *context->cur_x; tangents.start_y = y - *context->cur_y; }
    if (!svg_vector_nonzero(tangents.end_x, tangents.end_y)) { tangents.end_x = x - *context->cur_x; tangents.end_y = y - *context->cur_y; }
    svg_emit_pending_move(context->path, context->pending_move, *context->pending_x, *context->pending_y);
    render_path_append_quadratic(context->path, *context->cur_x, *context->cur_y, qx, qy, x, y);
    svg_path_emit_segment(context, x, y, tangents);
    // smooth quadratic reflection retains the authored control, rather than the converted cubic.
    *context->last_ctrl_x = qx; *context->last_ctrl_y = qy;
}

// Parse SVG path 'd' attribute into an RdtPath. Returns new path (caller must free).
static RdtPath* parse_svg_path_d(const char* d, SvgPathTopology* topology = nullptr,
                                 bool allow_move_only = false) {
    if (!d || !*d) return nullptr;

    RdtPath* path = rdt_path_new();

    if (!path) return nullptr;

    float cur_x = 0, cur_y = 0;
    float start_x = 0, start_y = 0;
    float pending_x = 0, pending_y = 0;
    float last_ctrl_x = 0, last_ctrl_y = 0;
    char last_cmd = 0;
    char previous_cmd = 0;
    bool pending_move = false;
    bool subpath_has_draw = false;
    bool any_draw = false;
    SvgPathEmitContext emit_context = {
        path, &cur_x, &cur_y, &pending_x, &pending_y,
        &last_ctrl_x, &last_ctrl_y, &pending_move,
        &subpath_has_draw, &any_draw, topology
    };

    const char* p = d;

    while (*p) {
        p = str_skip_ascii_space(p);
        if (!*p) break;

        char cmd = *p;
        bool is_cmd = isalpha((unsigned char)cmd);

        if (is_cmd) {
            p++;
        } else {
            if (!last_cmd || last_cmd == 'Z' || last_cmd == 'z') {
                log_error("[SVG] path parse: invalid token near '%.16s'", p);
                break;
            }
            cmd = last_cmd;
        }

        bool relative = islower((unsigned char)cmd);
        cmd = (char)toupper((unsigned char)cmd);
        float args[7] = {};
        if ((!last_cmd && cmd != 'M') ||
            (cmd != 'Z' && !svg_read_path_parameters(&p, cmd, !is_cmd, args))) {
            log_error("[SVG] path parse: invalid '%c' segment near '%.16s'", cmd, p);
            break;
        }
        last_cmd = relative ? (char)tolower((unsigned char)cmd) : cmd;
        if (relative) svg_path_resolve_relative_parameters(cmd, args, cur_x, cur_y);

        switch (cmd) {
            case 'M': {  // moveto
                float x = args[0], y = args[1];
                cur_x = start_x = x;
                cur_y = start_y = y;
                svg_topology_move(topology, x, y);
                pending_x = x;
                pending_y = y;
                pending_move = true;
                subpath_has_draw = false;
                last_ctrl_x = cur_x;
                last_ctrl_y = cur_y;
                last_cmd = relative ? 'l' : 'L';
                break;
            }
            case 'L': {  // lineto
                float x = args[0], y = args[1];
                svg_path_emit_line(&emit_context, x, y);
                last_ctrl_x = cur_x;
                last_ctrl_y = cur_y;
                break;
            }
            case 'H': {  // horizontal lineto
                svg_path_emit_line(&emit_context, args[0], cur_y);
                last_ctrl_x = cur_x;
                last_ctrl_y = cur_y;
                break;
            }
            case 'V': {  // vertical lineto
                svg_path_emit_line(&emit_context, cur_x, args[0]);
                last_ctrl_x = cur_x;
                last_ctrl_y = cur_y;
                break;
            }
            case 'C': {  // cubic bezier
                svg_path_emit_cubic(&emit_context, args[0], args[1], args[2], args[3], args[4], args[5]);
                break;
            }
            case 'S': {  // smooth cubic bezier
                bool reflect = previous_cmd == 'C' || previous_cmd == 'S';
                float x1 = reflect ? 2 * cur_x - last_ctrl_x : cur_x;
                float y1 = reflect ? 2 * cur_y - last_ctrl_y : cur_y;
                svg_path_emit_cubic(&emit_context, x1, y1, args[0], args[1], args[2], args[3]);
                break;
            }
            case 'Q': {  // quadratic bezier -> convert to cubic
                svg_path_emit_quadratic(&emit_context, args[0], args[1], args[2], args[3]);
                break;
            }
            case 'T': {  // smooth quadratic bezier
                bool reflect = previous_cmd == 'Q' || previous_cmd == 'T';
                float qx = reflect ? 2 * cur_x - last_ctrl_x : cur_x;
                float qy = reflect ? 2 * cur_y - last_ctrl_y : cur_y;
                float x = args[0], y = args[1];
                svg_path_emit_quadratic(&emit_context, qx, qy, x, y);
                break;
            }
            case 'A': {  // retain one authored vertex, irrespective of arc subdivision.
                float x = args[5], y = args[6];
                SvgSegmentTangents tangents = {};
                svg_emit_pending_move(path, &pending_move, pending_x, pending_y);
                arc_to_beziers(path, cur_x, cur_y, args[0], args[1], args[2],
                    args[3] != 0.0f, args[4] != 0.0f, x, y, &tangents);
                svg_path_emit_segment(&emit_context, x, y, tangents);
                last_ctrl_x = cur_x; last_ctrl_y = cur_y;
                break;
            }
            case 'Z': {
                SvgSegmentTangents tangents = {start_x - cur_x, start_y - cur_y, start_x - cur_x, start_y - cur_y};
                if (subpath_has_draw) {
                    svg_emit_pending_move(path, &pending_move, pending_x, pending_y);
                    rdt_path_close(path);
                    svg_topology_segment(topology, start_x, start_y, tangents, true);
                }
                cur_x = start_x; cur_y = start_y;
                last_ctrl_x = cur_x; last_ctrl_y = cur_y;
                break;
            }
        }
        previous_cmd = cmd;
    }

    if (!any_draw && !allow_move_only) {
        rdt_path_free(path);
        return nullptr;
    }

    if (allow_move_only) svg_emit_pending_move(path, &pending_move, pending_x, pending_y);
    return path;
}

RdtPath* svg_parse_path_d(const char* d) {
    return parse_svg_path_d(d);
}



static RdtMatrix svg_matrix_rotate(float radians) {
    float c = cosf(radians);
    float s = sinf(radians);
    RdtMatrix m = { c, -s, 0,  s, c, 0,  0, 0, 1 };
    return m;
}

struct SvgTopologyVisitor {
    SvgPathTopology* topology;
    float x = 0.0f, y = 0.0f, start_x = 0.0f, start_y = 0.0f;
};

static bool svg_topology_visit(void* context, RdtPathCommand command, const float* args, int count) {
    (void)count;
    SvgTopologyVisitor* visitor = (SvgTopologyVisitor*)context;
    if (command == RDT_PATH_MOVE) {
        visitor->x = visitor->start_x = args[0]; visitor->y = visitor->start_y = args[1];
        svg_topology_move(visitor->topology, args[0], args[1]);
        return visitor->topology->valid;
    }
    float x, y;
    SvgSegmentTangents tangents;
    if (command == RDT_PATH_CLOSE) { x = visitor->start_x; y = visitor->start_y; }
    else if (command == RDT_PATH_LINE) { x = args[0]; y = args[1]; }
    else if (command == RDT_PATH_CUBIC) { x = args[4]; y = args[5]; }
    else return false;
    if (command == RDT_PATH_CUBIC) tangents = svg_cubic_tangents(visitor->x, visitor->y, args[0], args[1], args[2], args[3], x, y);
    else tangents = {x - visitor->x, y - visitor->y, x - visitor->x, y - visitor->y};
    svg_topology_segment(visitor->topology, x, y, tangents, command == RDT_PATH_CLOSE);
    visitor->x = x; visitor->y = y;
    return visitor->topology->valid;
}

static float svg_marker_angle(const SvgPathTopology* topology, size_t index, const RdtMatrix* frame) {
    const SvgPathVertex& vertex = topology->vertices[index];
    size_t first = vertex.subpath, last = first;
    while (last + 1 < topology->vertices.size() && topology->vertices[last + 1].subpath == first) last++;
    float ix = vertex.incoming_x, iy = vertex.incoming_y;
    float ox = vertex.outgoing_x, oy = vertex.outgoing_y;
    for (size_t i = index; !svg_vector_nonzero(ix, iy) && i > first; i--) {
        ix = topology->vertices[i].incoming_x; iy = topology->vertices[i].incoming_y;
    }
    for (size_t i = index; !svg_vector_nonzero(ox, oy) && i < last; i++) {
        ox = topology->vertices[i].outgoing_x; oy = topology->vertices[i].outgoing_y;
    }
    if (vertex.closed) {
        for (size_t i = last; !svg_vector_nonzero(ix, iy) && i > first; i--) {
            ix = topology->vertices[i].incoming_x; iy = topology->vertices[i].incoming_y;
        }
        for (size_t i = first; !svg_vector_nonzero(ox, oy) && i < last; i++) {
            ox = topology->vertices[i].outgoing_x; oy = topology->vertices[i].outgoing_y;
        }
    }
    if (!svg_vector_nonzero(ix, iy)) { ix = ox; iy = oy; }
    if (!svg_vector_nonzero(ox, oy)) { ox = ix; oy = iy; }
    if (frame) {
        float tx = frame->e11 * ix + frame->e12 * iy, ty = frame->e21 * ix + frame->e22 * iy;
        ix = tx; iy = ty;
        tx = frame->e11 * ox + frame->e12 * oy; ty = frame->e21 * ox + frame->e22 * oy;
        ox = tx; oy = ty;
    }
    float incoming = atan2f(iy, ix), outgoing = atan2f(oy, ox);
    float delta = outgoing - incoming;
    if (delta > math_pi_f()) delta -= math_tau_f();
    else if (delta < -math_pi_f()) delta += math_tau_f();
    return incoming + delta * 0.5f;
}

static float svg_marker_reference(Element* marker, const char* name,
    const SvgLengthContext* lengths, SvgLengthAxis axis) {
    const char* value = get_svg_attr(marker, name);
    if (value && (strcmp(value, "center") == 0)) value = "50%";
    else if (value && (strcmp(value, "right") == 0 || strcmp(value, "bottom") == 0)) value = "100%";
    else if (value && (strcmp(value, "left") == 0 || strcmp(value, "top") == 0)) value = "0%";
    return value ? svg_resolve_length(value, lengths, axis, 0.0f) : get_svg_number_attr(marker, name, 0.0f, lengths, axis);
}

static void render_svg_markers(SvgInlineRenderContext* ctx, Element* elem, RdtPath* path,
    const SvgPathTopology* authored, const RdtMatrix* transform,
    const SvgContextPaint* producer, const RdtGradientOptions* stroke) {
    const char* tag = get_element_tag_name(ctx, elem);
    if (!tag || (strcmp(tag, "text") == 0 || strcmp(tag, "tspan") == 0)) return;
    SvgInheritedProperty properties[3];
    bool present = false;
    for (size_t i = 0; i < 3; i++) {
        properties[i] = svg_computed_property(ctx, elem, (SvgInheritedPropertyId)(SVG_STYLE_MARKER_START + i));
        if (properties[i].value && strcmp(properties[i].value, "none") != 0) present = true;
    }
    if (!present) return;
    SvgPathTopology inspected;
    if (!authored) {
        SvgTopologyVisitor visitor = {&inspected};
        if (!rdt_path_visit(path, svg_topology_visit, &visitor)) return;
        authored = &inspected;
    }
    if (!authored->valid || authored->vertices.empty()) return;
    for (size_t index = 0; index < authored->vertices.size(); index++) {
        size_t operation = index == 0 ? 0 : index + 1 == authored->vertices.size() ? 2 : 1;
        // a single moveto owns both endpoint marker operations.
        size_t repetitions = authored->vertices.size() == 1 ? 2 : 1;
        for (size_t repetition = 0; repetition < repetitions; repetition++) {
            if (repetition) operation = 2;
            SvgInheritedProperty property = properties[operation];
            if (!property.value || strcmp(property.value, "none") == 0) continue;
            SvgInlineRenderContext declaration = *ctx;
            if (property.source_style) { declaration.style_context = lam::up((SvgStyleContext*)property.source_style); declaration.source_path = lam::up(property.source_path); }
            SvgPaint reference_paint = svg_parse_paint(&declaration, property.value, elem);
            if (reference_paint.kind != SVG_PAINT_RESOURCE) continue;
            SvgResourceReference reference = svg_resolve_reference(&declaration, reference_paint.reference);
            Element* marker = reference.element;
            SvgInlineRenderContext document = svg_reference_render_context(&declaration, &reference);
            const char* marker_tag = marker ? get_element_tag_name(&document, marker) : nullptr;
            if (!marker_tag || strcmp(marker_tag, "marker") != 0) continue;
            const SvgPaintResourceScope* parent = (const SvgPaintResourceScope*)ctx->paint_resource_scope;
            bool recursive = false; size_t depth = 0;
            for (const SvgPaintResourceScope* scope = parent; scope; scope = scope->parent) {
                if (scope->element == marker || ++depth >= SVG_USE_DEPTH_MAX) { recursive = true; break; }
            }
            if (recursive) continue;
            SvgInlineRenderContext instance = svg_resource_style_context(&document, marker);
            SvgLengthContext lengths = svg_length_context(&instance);
            float width = get_svg_number_attr(marker, "markerWidth", 3.0f, &lengths, SVG_LENGTH_X);
            float height = get_svg_number_attr(marker, "markerHeight", 3.0f, &lengths, SVG_LENGTH_Y);
            if (width <= 0.0f || height <= 0.0f) continue;
            SvgViewBox viewbox = svg_parse_viewbox(get_svg_attr(marker, "viewBox"));
            if (viewbox.has_viewbox && (viewbox.width <= 0.0f || viewbox.height <= 0.0f)) continue;
            RdtMatrix mapping = viewbox.has_viewbox ? svg_viewbox_transform(&viewbox, width, height,
                get_svg_attr(marker, "preserveAspectRatio")) : rdt_matrix_identity();
            instance.current_viewport_w = viewbox.has_viewbox ? viewbox.width : width;
            instance.current_viewport_h = viewbox.has_viewbox ? viewbox.height : height;
            instance.viewbox_x = viewbox.has_viewbox ? viewbox.min_x : 0.0f;
            instance.viewbox_y = viewbox.has_viewbox ? viewbox.min_y : 0.0f;
            lengths = svg_length_context(&instance);
            float ref_x = svg_marker_reference(marker, "refX", &lengths, SVG_LENGTH_X);
            float ref_y = svg_marker_reference(marker, "refY", &lengths, SVG_LENGTH_Y);
            rdt_matrix_transform_point(&mapping, ref_x, ref_y, &ref_x, &ref_y);
            const SvgPathVertex& vertex = authored->vertices[index];
            float x = vertex.x, y = vertex.y;
            const char* units = get_svg_attr(marker, "markerUnits");
            bool stroke_units = !units || strcmp(units, "userSpaceOnUse") != 0;
            float scale = stroke_units ? stroke->stroke_width : 1.0f;
            const char* effect = svg_style_property_value(ctx, elem, "vector-effect");
            bool non_scaling = stroke_units && effect && strcmp(effect, "non-scaling-stroke") == 0 && transform;
            RdtMatrix identity = rdt_matrix_identity();
            const RdtMatrix* frame = transform ? transform : &identity;
            if (non_scaling) {
                rdt_matrix_transform_point(transform, x, y, &x, &y);
                scale *= ctx->raster_scale > 0.0f ? ctx->raster_scale : 1.0f;
                frame = &identity;
            }
            if (scale <= 0.0f) continue;
            float angle = 0.0f;
            const char* orient = get_svg_attr(marker, "orient");
            if (orient && (strcmp(orient, "auto") == 0 || strcmp(orient, "auto-start-reverse") == 0)) {
                angle = svg_marker_angle(authored, index, non_scaling ? transform : nullptr);
                if (operation == 0 && strcmp(orient, "auto-start-reverse") == 0) angle += math_pi_f();
            } else if (orient) {
                angle = math_degrees_to_radians(svg_resolve_angle(orient, 0.0f));
            }
            RdtMatrix placement = rdt_matrix_translate(x, y), rotation = svg_matrix_rotate(angle);
            placement = rdt_matrix_multiply(&placement, &rotation);
            RdtMatrix scaling = rdt_matrix_scale(scale, scale), offset = rdt_matrix_translate(-ref_x, -ref_y);
            placement = rdt_matrix_multiply(&placement, &scaling);
            placement = rdt_matrix_multiply(&placement, &offset);
            placement = rdt_matrix_multiply(frame, &placement);
            instance.transform = rdt_matrix_multiply(&placement, &mapping);
            instance.context_paint = lam::up(producer);
            // marker styles come from the definition; only explicit context paint reaches the producer.
            SvgPaintResourceScope scope = {marker, parent}; instance.paint_resource_scope = lam::up(&scope);
            instance.opacity = ctx->opacity;
            const char* overflow = svg_style_property_value(&instance, marker, "overflow");
            bool clipped = !overflow || strcmp(overflow, "visible") != 0;
            if (clipped) {
                RdtPath* clip = rdt_path_new();
                if (!clip) continue;
                rdt_path_add_rect(clip, 0, 0, width, height, 0, 0);
                svg_push_clip(&instance, clip, &placement); rdt_path_free(clip);
            }
            if (!svg_render_effect_boundary(&instance, marker, svg_draw_children)) render_svg_children(&instance, marker);
            if (clipped) svg_pop_clip(&instance);
        }
    }
}

static void render_svg_path(SvgInlineRenderContext* ctx, Element* elem) {
    const char* d = get_svg_attr(elem, "d");
    SvgPathTopology topology;
    RdtPath* path = parse_svg_path_d(d, &topology, true);
    if (!path) return;

    RdtMatrix m = compose_element_transform(ctx, elem);
    RdtPath* draw_path = path;
    SvgSimpleRectPath rect = {};
    RdtPath* stable_path = nullptr;
    if (svg_path_is_fill_only(ctx, elem) && svg_parse_simple_rect_path(d, &rect)) {
        stable_path = svg_make_stable_hairline_rect_path(&rect, &m);
        if (stable_path) draw_path = stable_path;
    }

    float bx = 0.0f, by = 0.0f, right = 0.0f, bottom = 0.0f;
    rdt_path_get_bounds(path, &bx, &by, &right, &bottom);
    draw_svg_fill_stroke(ctx, draw_path, elem, &m, bx, by, right - bx, bottom - by, true, nullptr, nullptr, nullptr, &topology);
    if (stable_path) rdt_path_free(stable_path);
    rdt_path_free(path);

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
    str_copy(criteria.family_name, sizeof(criteria.family_name), family, strlen(family));
    criteria.weight = weight;
    criteria.style = slant;

    FontDatabaseResult result = font_database_find_best_match_internal(font_ctx->database, &criteria);
    if (result.font && result.font->file_path) {
        // skip TTC files — ThorVG TTF loader doesn't handle TrueType Collections
        if (strstr(result.font->file_path, ".ttc")) {
            return nullptr;
        }
        if (out_font_name) *out_font_name = result.font->family_name;
        // return a mem_alloc'd copy so caller can mem_free() uniformly
        return mem_strdup(result.font->file_path, MEM_CAT_RENDER);
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
            return p;
        }

        // data URI — decode and write to ./temp/lambda_font_<hash>.<ext>
        const char* comma = strchr(src, ',');
        if (!comma) continue;

        // pick extension from format (if known), else from MIME type, else ttf
        const char* ext = "ttf";
        const char* fmt = entry->sources[i].format;
        if (fmt) {
            if (str_icmp_cstr(fmt, "opentype") == 0 || str_icmp_cstr(fmt, "otf") == 0) ext = "otf";
            else if (str_icmp_cstr(fmt, "woff2") == 0) ext = "woff2";
            else if (str_icmp_cstr(fmt, "woff") == 0) ext = "woff";
        } else {
            // sniff from mime: data:font/ttf;... or data:font/otf;...
            if (strncmp(src, "data:font/otf", 13) == 0 ||
                strncmp(src, "data:application/font-otf", 25) == 0) ext = "otf";
            else if (strstr(src, "woff2")) ext = "woff2";
            else if (strstr(src, "woff")) ext = "woff";
        }

        // hash the data URI suffix (after comma) for a stable filename
        // simple FNV-1a 64-bit
        uint64_t h = hash_fnv1a_64_cstr(comma + 1);

        char temp_path[512];
        snprintf(temp_path, sizeof(temp_path), "./temp/lambda_font_%016llx.%s",
                 (unsigned long long)h, ext);

        // if file already exists (cached), use it directly
        FILE* fcheck = fopen(temp_path, "rb");
        if (fcheck) {
            fclose(fcheck);
            // reject if no Unicode cmap (PDF Identity / Mac Roman subsets)
            if (!allow_nonunicode_cmap && !font_file_has_unicode_cmap(temp_path)) {
                return nullptr;
            }
            char* p = mem_strdup(temp_path, MEM_CAT_RENDER);
            if (out_font_name) *out_font_name = entry->family;
            return p;
        }

        // base64 decode and write
        size_t b64_len = strlen(comma + 1);
        size_t decoded_len = 0;
        lam::Temp<uint8_t> decoded(base64_decode(comma + 1, b64_len, &decoded_len));
        if (!decoded || decoded_len == 0) continue;

        FILE* fout = fopen(temp_path, "wb");
        if (!fout) continue;
        size_t written = fwrite(decoded.get(), 1, decoded_len, fout);
        fclose(fout);
        decoded.reset();
        if (written != decoded_len) {
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

static void svg_font_name_from_path(char* out, size_t out_cap, const char* path) {
    if (!out || out_cap == 0) return;
    out[0] = '\0';
    if (!path) return;

    const char* base = file_path_basename(path);
    const char* ext = file_path_ext(path);
    size_t name_len = ext ? (size_t)(ext - base) : strlen(base);
    if (name_len >= out_cap) name_len = out_cap - 1;
    str_copy(out, out_cap, base, name_len);
}

// family of SVG text that specifies none
static const char* const SVG_DEFAULT_FONT_FAMILY = "Arial";

static char* resolve_svg_font_path(const char* font_family, char* out_font_name, size_t name_capacity,
                                    FontContext* font_ctx = nullptr, int weight = 400,
                                    FontSlant slant = FONT_SLANT_NORMAL,
                                    bool allow_nonunicode_fontface = false) {
    if (!font_family || !*font_family) {
        // default to a common sans-serif font
        font_family = SVG_DEFAULT_FONT_FAMILY;
    }

    // SVG font-family is a comma-separated list of family names (with optional
    // single/double quotes around multi-word names) and at most one generic
    // family keyword (serif, sans-serif, monospace, cursive, fantasy).  Try
    // each candidate in order, applying weight-aware matching for each, before
    // falling back to a global default list.
    // collect candidate family names with the shared CSS list parser
    char candidate_storage[16][256];
    const char* candidates[16];
    int candidate_count = 0;
    const char* family_cursor = font_family;
    while (candidate_count < 16 && font_family_list_next(
            &family_cursor, candidate_storage[candidate_count],
            sizeof(candidate_storage[candidate_count]))) {
        const char* candidate = candidate_storage[candidate_count];
        if (str_icmp_cstr(candidate, "serif") == 0)            candidate = "Times New Roman";
        else if (str_icmp_cstr(candidate, "sans-serif") == 0)  candidate = "Arial";
        else if (str_icmp_cstr(candidate, "monospace") == 0)   candidate = "Courier New";
        else if (str_icmp_cstr(candidate, "cursive") == 0)     candidate = "Comic Sans MS";
        else if (str_icmp_cstr(candidate, "fantasy") == 0)     candidate = "Impact";
        candidates[candidate_count++] = candidate;
    }

    // caller-owned names prevent concurrent headless measurements from sharing scratch bytes.
    auto named_path = [&](char* path, const char* name) -> char* {
        if (path && out_font_name && name_capacity) {
            if (name) str_copy(out_font_name, name_capacity, name, strlen(name));
            else svg_font_name_from_path(out_font_name, name_capacity, path);
        }
        return path;
    };
    auto try_family = [&](const char* family) -> char* {
        if (font_ctx) {
            const char* name = nullptr;
            char* path = resolve_font_via_fontface(font_ctx, family, &name, weight, slant,
                allow_nonunicode_fontface);
            if (path) return named_path(path, name);
        }
        if (font_ctx && (weight >= 600 || slant != FONT_SLANT_NORMAL)) {
            FontMatchResult match = font_find_best_match(font_ctx, family, weight, slant);
            if (match.found && match.file_path && !strstr(match.file_path, ".ttc"))
                return named_path(mem_strdup(match.file_path, MEM_CAT_RENDER), nullptr);
        }
        lam::Temp<char> path(font_platform_find_fallback(family, NULL));
        if (path && strstr(path.get(), ".ttc")) path.reset();
        if (path) return named_path(path.release(), nullptr);
        if (font_ctx) {
            const char* name = nullptr;
            path.reset(resolve_font_via_database(font_ctx, family, &name, weight, slant));
            if (path) return named_path(path.release(), name);
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
            return p;
        }
    }

    if (out_font_name && name_capacity) out_font_name[0] = '\0';
    return nullptr;
}


/**
 * Trim leading and trailing whitespace from a string
 * Returns a newly allocated trimmed string, or nullptr if result is empty
 */
static char* trim_whitespace(const char* str, size_t len) {
    if (!str || len == 0) return nullptr;
    str_trim(&str, &len);
    return len ? mem_dup_n(str, len, MEM_CAT_RENDER) : nullptr;
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
                if (str_all(str->chars, str->len, str_is_space)) continue;
                return trim_whitespace(str->chars, str->len);
            }
        }
    }
    return nullptr;
}

/**
 * Create a single ThorVG text object with specified properties
 * Note: font_size_px is in SVG/CSS user units, the coordinate space of the
 * surrounding SVG transform.
 * anchor_x: horizontal anchor (0=start, 0.5=middle, 1=end) from SVG text-anchor
 */
static Tvg_Paint create_text_segment(const char* text, float x, float y,
                                     const char* font_path, const char* font_name,
                                     float font_size_px, Color fill_color,
                                     float anchor_x = 0.0f) {
    if (!text || !*text || !font_path) return nullptr;

    // tvg_text_set_size takes points and the TTF loader scales them by 96/72;
    // passing pixels drew text 4/3 too large, with its baseline that much lower.
    float font_size_tvg = font_size_px * 72.0f / 96.0f;

    Tvg_Paint tvg_text = tvg_text_new();
    if (!tvg_text) return nullptr;

    // load font (ThorVG caches it)
    Tvg_Result load_result = tvg_font_load(font_path);
    if (load_result != TVG_RESULT_SUCCESS) {
        tvg_paint_unref(tvg_text, true);
        return nullptr;
    }


    // set font by name - ThorVG matches the font name from the loaded font file
    // common font names: "Arial", "Helvetica", "SF NS", "Geneva", etc.
    Tvg_Result result = tvg_text_set_font(tvg_text, font_name);
    if (result != TVG_RESULT_SUCCESS) {
        // if font name fails, try nullptr as fallback
        result = tvg_text_set_font(tvg_text, nullptr);
        if (result != TVG_RESULT_SUCCESS) {
            tvg_paint_unref(tvg_text, true);
            return nullptr;
        }
    } else {
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


    return tvg_text;
}

/**
 * Measure text width using the font system for accurate positioning.
 * Falls back to rough estimate if font handle unavailable.
 */
bool svg_measure_text_metrics(const char* text, float font_size_px,
                              FontContext* font_ctx, const char* font_family,
                              int weight, FontSlant slant,
                              SvgTextMetrics* out_metrics) {
    if (!out_metrics) return false;
    *out_metrics = {};
    if (!text || !*text) return true;

    out_metrics->ascent = font_size_px * 0.8f;
    out_metrics->descent = font_size_px - out_metrics->ascent;

    // try measuring with the font system
    if (font_ctx && font_family) {
        FontStyleDesc style = {};
        style.family = font_family;
        style.size_px = font_size_px;
        style.weight = (FontWeight)weight;
        style.slant = slant;

        FontHandle* handle = font_resolve(font_ctx, &style);
        if (handle) {
            TextExtents ext = font_measure_text(handle, text, (int)strlen(text));
            const FontMetrics* metrics = font_get_metrics(handle);
            if (metrics) {
                out_metrics->ascent = metrics->ascender;
                out_metrics->descent = -metrics->descender;
                out_metrics->used_font_metrics = true;
            }
            font_handle_release(handle);
            if (ext.width > 0) {
                out_metrics->width = ext.width;
                return true;
            }
        }
    }

    // fallback: rough estimate
    size_t len = strlen(text);
    out_metrics->width = len * font_size_px * 0.55f;
    return true;
}

static void draw_glyph_affine(RasterRenderContext* rdcon, GlyphBitmap* bitmap,
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

// A Radiant font for SVG text drawn under `matrix`. The face is resolved at
// the device size, so glyph bitmaps and advances are already device pixels.
typedef struct SvgGlyphFont {
    FontStyleDesc style;
    FontHandle* handle;
    float sx;
    float sy;
    bool rotated;
    float shear_x;
} SvgGlyphFont;

static bool svg_glyph_font_open(SvgInlineRenderContext* ctx, const char* font_family,
                                float font_size, int font_weight, FontSlant font_slant,
                                const RdtMatrix* matrix, SvgGlyphFont* font) {
    *font = {};
    RasterRenderContext* rdcon = g_svg_active_rdcon;
    if (!rdcon || !ctx || !ctx->font_ctx || !matrix) return false;

    font->sx = sqrtf(matrix->e11 * matrix->e11 + matrix->e21 * matrix->e21);
    font->sy = sqrtf(matrix->e12 * matrix->e12 + matrix->e22 * matrix->e22);
    if (font->sx <= 0.0f || font->sy <= 0.0f) return false;
    font->rotated = fabsf(matrix->e21) > 0.001f;
    font->shear_x = matrix->e12 / font->sy;

    float raster_scale = rdcon->raster_scale > 0.0f ? rdcon->raster_scale : 1.0f;
    font->style.family = font_family ? font_family : SVG_DEFAULT_FONT_FAMILY;
    font->style.size_px = font_size * font->sy / raster_scale;
    font->style.weight = (FontWeight)font_weight;
    font->style.slant = font_slant;
    font->handle = font_resolve(ctx->font_ctx, &font->style);
    return font->handle != nullptr;
}

// Sum of glyph advances in device pixels: exactly how far the glyph drawer
// moves the pen, since it places glyphs unkerned.
static float svg_glyph_font_width(SvgGlyphFont* font, const char* text) {
    float width = 0.0f;
    const unsigned char* cursor = (const unsigned char*)text;
    const unsigned char* end = cursor + strlen(text);
    while (cursor < end) {
        uint32_t codepoint = 0;
        if (!layout_utf8_next_codepoint(&cursor, end, &codepoint)) continue;
        LoadedGlyph* glyph = font_load_glyph(font->handle, &font->style, codepoint, false);
        if (glyph) width += glyph->advance_x;
    }
    return width;
}

static bool render_svg_text_with_radiant_glyphs(SvgInlineRenderContext* ctx, const char* text,
                                                const char* font_family, float font_size,
                                                int font_weight, FontSlant font_slant,
                                                Color fill_color, const RdtMatrix* matrix,
                                                float base_x, float base_y, float text_length,
                                                bool scale_glyphs_x) {
    RasterRenderContext* rdcon = g_svg_active_rdcon;
    if (!text || !*text) return false;
    SvgGlyphFont font;
    if (!svg_glyph_font_open(ctx, font_family, font_size, font_weight, font_slant,
                             matrix, &font)) {
        return false;
    }
    FontHandle* handle = font.handle;
    FontStyleDesc style = font.style;
    float sx = font.sx;
    float sy = font.sy;
    bool rotated_text = font.rotated;
    float shear_x = font.shear_x;

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

    float natural_width = svg_glyph_font_width(&font, text);
    const unsigned char* cursor = nullptr;
    const unsigned char* end = (const unsigned char*)text + strlen(text);

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
        uint32_t codepoint = 0;
        if (!layout_utf8_next_codepoint(&cursor, end, &codepoint)) continue;
        LoadedGlyph* glyph = font_load_glyph(handle, &style, codepoint, false);
        if (!glyph) continue;
        float glyph_advance = glyph->advance_x;
        // A glyph without ink (a space) moves the pen like any other; the
        // rotated path steps in user units, glyph_advance / sy.
        float pen_step = glyph_advance * advance_scale;
        float local_step = rotated_text ? (glyph_advance / sy) * local_advance_scale : pen_step;
        LoadedGlyph* drawn_glyph = font_load_glyph(draw_handle, &draw_style, codepoint, true);
        bool has_bitmap = drawn_glyph && drawn_glyph->bitmap.buffer &&
            drawn_glyph->bitmap.width > 0 &&
            drawn_glyph->bitmap.height > 0 &&
            drawn_glyph->bitmap.pitch > 0;
        if (!has_bitmap) {
            pen_x += pen_step;
            local_pen_x += local_step;
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
        pen_x += pen_step;
        local_pen_x += local_step;
    }

    rdcon->has_transform = saved_has_transform;
    rdcon->color = saved_color;
    if (draw_handle != handle) font_handle_release(draw_handle);
    font_handle_release(handle);
    return true;
}

// ============================================================================
// SVG Text Layout (SVG 2 §11)
// ============================================================================
//
// A <text> element sets its character data, including that of nested <tspan>
// and <a> elements, as one line: white space collapses across element
// boundaries (CSS Text §4.1.1), each run of characters keeps the font and fill
// of its own element, runs advance the current text position in document
// order, an absolute x or y starts a new text chunk, and text-anchor aligns
// each chunk as a whole (SVG 2 §11.10.1).

// em and % font sizes are relative to the inherited size (CSS Fonts §2.3).
float svg_font_size_value(const char* value, float parent_size, float parent_x_height) {
    if (!value) return parent_size;
    if (strcmp(value, "initial") == 0 || strcmp(value, "medium") == 0) return 16.0f;
    char* end = nullptr;
    float number = strtof(value, &end);
    if (end == value) return parent_size;
    const char* unit = str_skip_ascii_space(end);
    if (strcmp(unit, "em") == 0) return number * parent_size;
    if (strcmp(unit, "ex") == 0) return number * (parent_x_height > 0.0f ? parent_x_height : parent_size * 0.5f);
    if (*unit == '%') return number * parent_size / 100.0f;
    float size = parse_svg_length(value, parent_size);
    return size >= 0.0f ? size : parent_size;
}

// bolder and lighter step from the inherited weight (CSS Fonts §2.2).
int svg_font_weight_value(const char* value, int parent_weight) {
    if (strcmp(value, "normal") == 0) return 400;
    if (strcmp(value, "bold") == 0) return 700;
    if (strcmp(value, "bolder") == 0) {
        return parent_weight < 350 ? 400 : parent_weight < 550 ? 700 : 900;
    }
    if (strcmp(value, "lighter") == 0) {
        return parent_weight < 550 ? 100 : parent_weight < 750 ? 400 : 700;
    }
    int weight = atoi(value);
    return weight >= 1 && weight <= 1000 ? weight : parent_weight;
}

static float svg_text_anchor_value(const char* value) {
    if (value && strcmp(value, "middle") == 0) return 0.5f;
    if (value && strcmp(value, "end") == 0) return 1.0f;
    return 0.0f;
}

typedef struct SvgGroupScope {
    SvgPaint fill_paint, stroke_paint;
    SvgInheritedProperty inherited_properties[SVG_STYLE_PROPERTY_COUNT];
    Color fill_color;
    Color stroke_color;
    Color current_color;
    float stroke_width;
    float opacity;
    float fill_opacity, stroke_opacity;
    bool fill_none;
    bool stroke_none;
    RdtMatrix transform;
    const char* font_family;
    float font_size;
    int font_weight;
    const char* text_anchor;
    const char* font_style;
} SvgGroupScope;

static void svg_group_enter(SvgInlineRenderContext* ctx, Element* elem, SvgGroupScope* scope);
static void svg_group_leave(SvgInlineRenderContext* ctx, const SvgGroupScope* scope);

typedef struct SvgTextStyle {
    Element* element;
    int parent, first_run, end_run;
    int path_owner;             // textPath style whose coordinate line owns these characters
    char font_family[256];      // empty when none is specified
    float font_size;
    int font_weight;
    FontSlant font_slant;
    Color fill;
    float anchor;               // text-anchor: 0 start, 0.5 middle, 1 end
    float letter_spacing, word_spacing;
    float text_length;
    bool scale_glyphs;
    const char* baseline;
    float baseline_shift;
    unsigned decoration;
    Element* decoration_element;
    bool hidden;
    bool preserve_space;        // xml:space="preserve"
} SvgTextStyle;

// character adjustments are resolved from the nearest contributing ancestor.
typedef struct SvgTextAdjust {
    bool pending;
    bool has_x;
    bool has_y;
    bool has_rotate;
    float x, y, dx, dy, rotate;
} SvgTextAdjust;

typedef struct SvgTextPositionList {
    float* values;
    int count;
} SvgTextPositionList;

// each ancestor consumes all descendant UTF-16 addresses, even when overridden.
typedef struct SvgTextPositionScope {
    SvgTextPositionScope* parent;
    SvgTextPositionList lists[5];
    size_t index;
} SvgTextPositionScope;


typedef struct SvgTextFont {
    bool resolved;              // resolution was attempted
    lam::Own<char> path;        // ThorVG font file; null when no font is found
    char name[256];             // ThorVG font name (the resolvers reuse static buffers)
    const char* family;         // family for font_resolve
    float ascent_ratio;
} SvgTextFont;

typedef struct SvgTextRun {
    size_t start;               // offset of the run's NUL-terminated text
    size_t len;
    int style;                  // index into SvgTextLayout.styles
    int length_owner;           // nearest calibrated subtree; an ancestor treats it as one unit
    SvgTextAdjust adjust;       // applied before the run's first character
    SvgTextFont* font;          // null: no font, not drawn
    bool glyphs;                // drawn by the Radiant glyph path
    float advance;              // user units
    float natural_advance, baseline_offset;
    int glyph_count;
    uint32_t codepoint;
    bool color_bitmap;
    bool path_hidden, on_path, paint_warped;
    RdtMatrix path_transform;
    RdtPath* paint_path;
    float glyph_scale_x;
    float x, y;                 // pen position
} SvgTextRun;

struct SvgTextPathData {
    RdtPathMetrics metrics;
    float start_offset;
    bool valid, reversed, stretch;
};

typedef struct SvgTextLayout {
    SvgInlineRenderContext* ctx;
    TextMeasureFontFn on_font;
    void* font_context;
    size_t request_index;
    float text_length;
    Bound paint_box;
    RdtMatrix paint_transform;
    bool allow_embedded_font, scale_glyphs;
    StrBuf* chars;              // run texts, each NUL-terminated
    SvgTextStyle* styles;       // one per text content element
    int style_count;
    int style_capacity;
    SvgTextFont* fonts;         // parallel to styles once collection is done
    SvgTextPathData* paths;     // metrics are owned until glyph paint/query completion
    SvgTextRun* runs;
    int run_count;
    int run_capacity;
    SvgTextPositionScope* positions; // active ancestor positioning lists
    int open_run;               // run still taking characters, -1 when none
    bool space_before;          // the last character was a collapsible space
    int trailing_space_run;     // run ending in a collapsible space, -1 when none
} SvgTextLayout;

static FontStyleDesc svg_text_font_descriptor(const SvgTextStyle* style, const char* family = nullptr) {
    FontStyleDesc descriptor = {};
    descriptor.family = family ? family : style->font_family[0] ? style->font_family : SVG_DEFAULT_FONT_FAMILY;
    descriptor.size_px = style->font_size;
    descriptor.weight = (FontWeight)style->font_weight; descriptor.slant = style->font_slant;
    return descriptor;
}

static SvgLengthContext svg_text_length_context(SvgInlineRenderContext* ctx, const SvgTextStyle* style) {
    FontStyleDesc descriptor = svg_text_font_descriptor(style);
    return {ctx->current_viewport_w, ctx->current_viewport_h,
        style->font_size, style->font_size * 0.5f, ctx->font_ctx, descriptor};
}

static void svg_text_style_apply(SvgInlineRenderContext* ctx, Element* elem, SvgTextStyle* style) {
    char buf[256];
    FontStyleDesc parent_font = svg_text_font_descriptor(style);
    const char* value = get_svg_attr_or_style(ctx, elem, "font-size", buf, sizeof(buf));
    if (value) style->font_size = svg_context_font_size(value, ctx->font_ctx, &parent_font);
    else style->font_size = get_svg_number_attr(elem, "font-size", style->font_size);
    value = get_svg_attr_or_style(ctx, elem, "font-family", buf, sizeof(buf));
    if (value) str_copy(style->font_family, sizeof(style->font_family), value, strlen(value));
    value = get_svg_attr_or_style(ctx, elem, "font-weight", buf, sizeof(buf));
    if (value) style->font_weight = svg_font_weight_value(value, style->font_weight);
    value = get_svg_attr_or_style(ctx, elem, "font-style", buf, sizeof(buf));
    if (value) {
        style->font_slant = strcmp(value, "italic") == 0 ? FONT_SLANT_ITALIC
                          : strcmp(value, "oblique") == 0 ? FONT_SLANT_OBLIQUE
                          : FONT_SLANT_NORMAL;
    }
    value = get_svg_attr_or_style(ctx, elem, "fill", buf, sizeof(buf));
    // text is filled with plain colour only; a paint server keeps the inherited one
    if (value && strncmp(value, "url(", 4) != 0) style->fill = svg_resolve_color_keyword(ctx, value);
    value = get_svg_attr_or_style(ctx, elem, "text-anchor", buf, sizeof(buf));
    if (value) style->anchor = svg_text_anchor_value(value);
    value = svg_style_property_value(ctx, elem, "visibility");
    if (value) style->hidden = strcmp(value, "hidden") == 0 || strcmp(value, "collapse") == 0;
    value = get_svg_attr(elem, "xml:space");
    if (value) style->preserve_space = strcmp(value, "preserve") == 0;
    SvgLengthContext lengths = svg_text_length_context(ctx, style);
    // textLength belongs to this content element; it is not inherited.
    value = get_svg_attr(elem, "textLength");
    style->text_length = value ? svg_resolve_length(value, &lengths, SVG_LENGTH_X, -1.0f) : -1.0f;
    value = get_svg_attr(elem, "lengthAdjust");
    style->scale_glyphs = value && (strcmp(value, "spacingAndGlyphs") == 0 || strcmp(value, "spacingandglyphs") == 0);
    const char* spacing_names[] = {"letter-spacing", "word-spacing"};
    float* spacing_values[] = {&style->letter_spacing, &style->word_spacing};
    for (int i = 0; i < 2; i++) {
        value = svg_style_property_value(ctx, elem, spacing_names[i]);
        if (!value && style->parent < 0) value = svg_style_ancestor_property_value(ctx, elem, spacing_names[i]);
        if (value) *spacing_values[i] = svg_resolve_length(value, &lengths, SVG_LENGTH_X, 0.0f);
    }
    value = svg_style_property_value(ctx, elem, "dominant-baseline");
    if (!value && style->parent < 0) value = svg_style_ancestor_property_value(ctx, elem, "dominant-baseline");
    if (value && strcmp(value, "auto") != 0) style->baseline = value;
    value = svg_style_property_value(ctx, elem, "alignment-baseline");
    if (value && strcmp(value, "auto") != 0 && strcmp(value, "baseline") != 0) style->baseline = value;
    value = svg_style_property_value(ctx, elem, "baseline-shift");
    if (value) {
        float shift = strcmp(value, "super") == 0 ? style->font_size * 0.6f
            : strcmp(value, "sub") == 0 ? -style->font_size * 0.3f
            : svg_resolve_length(value, &lengths, SVG_LENGTH_X, 0.0f);
        if (strchr(value, '%')) shift = strtof(value, nullptr) * style->font_size / 100.0f;
        style->baseline_shift -= shift;
    }
    value = svg_style_property_value(ctx, elem, "text-decoration-line");
    if (!value) value = svg_style_property_value(ctx, elem, "text-decoration");
    if (!value && style->parent < 0) {
        value = svg_style_ancestor_property_value(ctx, elem, "text-decoration-line");
        if (!value) value = svg_style_ancestor_property_value(ctx, elem, "text-decoration");
    }
    if (value) {
        style->decoration = (strstr(value, "underline") ? 1u : 0u) |
            (strstr(value, "overline") ? 2u : 0u) | (strstr(value, "line-through") ? 4u : 0u);
        style->decoration_element = elem;
    }

}

static SvgTextPositionList svg_text_position_list(SvgTextLayout* layout,
    Element* element, const char* name, const SvgLengthContext* lengths, SvgLengthAxis axis, bool angle) {
    SvgTextPositionList list = {};
    const char* value = get_svg_attr(element, name);
    if (!value) {
        // Lambda-built SVGs retain numeric Items; a scalar is a one-entry position list.
        float number = 0.0f;
        if (!read_svg_number_attr(element, name, &number, lengths, axis)) return list;
        list.values = (float*)scratch_alloc(layout->ctx->resource_scratch, sizeof(float));
        if (!list.values) return {};
        list.values[0] = number;
        list.count = 1;
        return list;
    }
    int capacity = 0;
    const char* cursor = value;
    while (cursor && *cursor) {
        cursor = str_skip_ascii_space(cursor);
        if (*cursor == ',') cursor = str_skip_ascii_space(cursor + 1);
        if (!*cursor) break;
        const char* end = cursor;
        while (*end && *end != ',' && !isspace((unsigned char)*end)) end++;
        size_t length = (size_t)(end - cursor);
        char* token = (char*)scratch_alloc(layout->ctx->resource_scratch, length + 1);
        if (!token) return {};
        memcpy(token, cursor, length); token[length] = '\0';
        float number = NAN;
        if (angle) {
            char* parsed_end = nullptr;
            number = strtof(token, &parsed_end);
            if (parsed_end == token || *parsed_end) number = NAN;
        } else number = svg_resolve_length(token, lengths, axis, NAN);
        if (!isfinite(number)) return {};
        if (!lam::scratch_grow_array(layout->ctx->resource_scratch, &list.values,
            &capacity, list.count, list.count + 1, 8)) return {};
        list.values[list.count++] = number;
        cursor = end;
    }
    return list;
}

static void svg_text_position_scope(SvgTextLayout* layout, Element* element,
    int style_index, SvgTextPositionScope* scope) {
    scope->parent = layout->positions;
    const SvgTextStyle* style = &layout->styles[style_index];
    SvgLengthContext lengths = svg_text_length_context(layout->ctx, style);
    const char* names[] = {"x", "y", "dx", "dy", "rotate"};
    for (int i = 0; i < 5; i++) scope->lists[i] = svg_text_position_list(layout,
        element, names[i], &lengths,
        i == 1 || i == 3 ? SVG_LENGTH_Y : SVG_LENGTH_X, i == 4);
    layout->positions = scope;
}

static SvgTextAdjust svg_text_character_adjust(SvgTextLayout* layout, uint32_t codepoint) {
    SvgTextAdjust adjust = {};
    bool found[5] = {};
    float* values[] = {&adjust.x, &adjust.y, &adjust.dx, &adjust.dy, &adjust.rotate};
    for (SvgTextPositionScope* scope = layout->positions; scope; scope = scope->parent) {
        for (int i = 0; i < 5; i++) {
            const SvgTextPositionList* list = &scope->lists[i];
            if (found[i] || !list->count || (scope->index >= (size_t)list->count && i != 4)) continue;
            size_t index = scope->index < (size_t)list->count ? scope->index : (size_t)list->count - 1;
            *values[i] = list->values[index]; found[i] = true; adjust.pending = true;
        }
        scope->index += codepoint > 0xffffu ? 2u : 1u;
    }
    adjust.has_x = found[0]; adjust.has_y = found[1]; adjust.has_rotate = found[4];
    return adjust;
}

static bool svg_text_push_style(SvgTextLayout* layout, const SvgTextStyle* style, int* index) {
    if (layout->style_count >= layout->style_capacity &&
        !lam::scratch_grow_array(layout->ctx->resource_scratch, &layout->styles,
                                 &layout->style_capacity, layout->style_count,
                                 layout->style_count + 1, 8)) {
        return false;
    }
    layout->styles[layout->style_count] = *style;
    *index = layout->style_count++;
    return true;
}

static void svg_text_emit(SvgTextLayout* layout, int style, const char* text,
    size_t length, uint32_t codepoint) {
    SvgTextAdjust adjust = svg_text_character_adjust(layout, codepoint);
    if (layout->run_count >= layout->run_capacity &&
        !lam::scratch_grow_array(layout->ctx->resource_scratch, &layout->runs,
                                 &layout->run_capacity, layout->run_count,
                                 layout->run_count + 1, 8)) return;
    if (layout->run_count > 0) strbuf_append_char(layout->chars, '\0');
    SvgTextRun* run = &layout->runs[layout->run_count];
    *run = {};
    run->start = layout->chars->length; run->style = style; run->adjust = adjust;
    run->codepoint = codepoint; run->glyph_scale_x = 1.0f; run->length_owner = -1;
    layout->open_run = layout->run_count++;
    strbuf_append_str_n(layout->chars, text, length);
    layout->runs[layout->open_run].len += length;
}

// collapse before assigning addresses; multibyte UTF-8 characters stay atomic.
static void svg_text_append(SvgTextLayout* layout, int style, const char* text, size_t len) {
    bool preserve = layout->styles[style].preserve_space;
    const unsigned char* cursor = (const unsigned char*)text;
    const unsigned char* end = cursor + len;
    while (cursor < end) {
        const unsigned char* start = cursor;
        uint32_t codepoint = 0;
        if (!layout_utf8_next_codepoint(&cursor, end, &codepoint)) continue;
        bool space = codepoint < 128u && str_is_space((char)codepoint);
        if (preserve || !space) {
            svg_text_emit(layout, style, space ? " " : (const char*)start,
                space ? 1u : (size_t)(cursor - start), space ? 32u : codepoint);
            layout->space_before = false; layout->trailing_space_run = -1;
        } else if (!layout->space_before) {
            svg_text_emit(layout, style, " ", 1u, 32u);
            layout->space_before = true; layout->trailing_space_run = layout->open_run;
        }
    }
}

static void svg_text_collect(SvgTextLayout* layout, Element* elem, int style) {
    SvgInlineRenderContext* ctx = layout->ctx;
    layout->styles[style].first_run = layout->run_count;
    SvgTextPositionScope positions = {};
    svg_text_position_scope(layout, elem, style, &positions);
    for (int64_t i = 0; i < elem->length; i++) {
        Item child = elem->items[i];
        TypeId type = get_type_id(child);
        if (type == LMD_TYPE_STRING) {
            String* str = child.get_string();
            if (str && str->len > 0) svg_text_append(layout, style, str->chars, str->len);
            continue;
        }
        if (type != LMD_TYPE_ELEMENT || !child.element) continue;
        Element* child_elem = child.element;
        const char* tag = get_element_tag_name(ctx, child_elem);
        if (!tag || (strcmp(tag, "tspan") != 0 && strcmp(tag, "a") != 0 && strcmp(tag, "textPath") != 0)) continue;
        if (!svg_element_is_eligible(ctx, child_elem)) continue;
        char display_buf[64];
        const char* display = get_svg_attr_or_style(ctx, child_elem, "display",
                                                    display_buf, sizeof(display_buf));
        if (display && strcmp(display, "none") == 0) continue;
        SvgTextStyle child_style = layout->styles[style];
        child_style.element = child_elem; child_style.parent = style;
        svg_text_style_apply(ctx, child_elem, &child_style);
        int child_index = -1;
        if (!svg_text_push_style(layout, &child_style, &child_index)) continue;
        if (strcmp(tag, "textPath") == 0) layout->styles[child_index].path_owner = child_index;
        svg_text_collect(layout, child_elem, child_index);
    }
    layout->positions = positions.parent;
    layout->styles[style].end_run = layout->run_count;
}

// A collapsible space that ends the line is removed (CSS Text §4.1.2).
static void svg_text_trim_end(SvgTextLayout* layout) {
    if (layout->trailing_space_run < 0) return;
    // the space is the last character collected, so it ends the last run
    SvgTextRun* run = &layout->runs[layout->trailing_space_run];
    run->len--;
    layout->chars->length = run->start + run->len;
    layout->chars->str[layout->chars->length] = '\0';
    if (run->len == 0) layout->run_count--;
}

static bool svg_text_font_path(SvgInlineRenderContext* ctx, SvgTextFont* font,
    const SvgTextStyle* style, bool allow_embedded_font) {
    const char* family = style->font_family[0] ? style->font_family : SVG_DEFAULT_FONT_FAMILY;
    if (!font->path) font->path = lam::own(resolve_svg_font_path(family,
        font->name, sizeof(font->name), ctx->font_ctx, style->font_weight,
        style->font_slant, allow_embedded_font));
    return font->path != nullptr;
}

// resolve each distinct family/weight/slant once per text element.
static SvgTextFont* svg_text_font(SvgTextLayout* layout, int style_index, bool allow_embedded_font) {
    const SvgTextStyle* style = &layout->styles[style_index];
    for (int i = 0; i < layout->style_count; i++) {
        SvgTextFont* font = &layout->fonts[i];
        const SvgTextStyle* other = &layout->styles[i];
        if (font->resolved && other->font_size == style->font_size &&
            other->font_weight == style->font_weight &&
            other->font_slant == style->font_slant &&
            strcmp(other->font_family, style->font_family) == 0) {
            return font->family ? font : nullptr;
        }
    }
    SvgInlineRenderContext* ctx = layout->ctx;
    SvgTextFont* font = &layout->fonts[style_index];
    font->resolved = true;
    // Name the default family itself: the ThorVG face key found for it (e.g.
    // "Arial Bold") is a file name that font_resolve does not know as a family.
    const char* family = style->font_family[0] ? style->font_family : SVG_DEFAULT_FONT_FAMILY;
    // native measurement and glyph painting use FontContext's cached family resolution;
    // search for a ThorVG font file only when that backend actually needs one.
    if ((!ctx->font_ctx || allow_embedded_font) &&
        !svg_text_font_path(ctx, font, style, allow_embedded_font)) return nullptr;

    // Radiant resolves the full authored family list, including collection and fallback faces.
    font->family = ctx->font_ctx && !allow_embedded_font ? family
        : font->name[0] ? font->name : family;
    font->ascent_ratio = 0.8f;
    if (ctx->font_ctx && font->family && style->font_size > 0.0f) {
        FontStyleDesc desc = {};
        desc.family = font->family;
        desc.size_px = style->font_size;
        desc.weight = (FontWeight)style->font_weight;
        desc.slant = style->font_slant;
        FontHandle* handle = font_resolve(ctx->font_ctx, &desc);
        if (handle) {
            const FontMetrics* metrics = font_get_metrics(handle);
            if (metrics && metrics->ascender > 0) font->ascent_ratio = metrics->ascender / style->font_size;
            font_handle_release(handle);
        }
    }
    return font;
}

static void svg_text_release_fonts(SvgTextLayout* layout) {
    for (int i = 0; i < layout->style_count; i++) {
        SvgTextFont* font = &layout->fonts[i];
        lam::free_owned(font->path);
        if (layout->paths) render_path_metrics_destroy(&layout->paths[i].metrics);
    }
}

// use the same presentation selector for logical metrics and glyph paint.
static FontHandle* svg_text_fallback_face(SvgTextLayout* layout, FontHandle* primary,
    const FontStyleDesc* descriptor, uint32_t codepoint) {
    FontHandle* emoji = utf_is_emoji_presentation_default(codepoint)
        ? font_resolve_for_emoji(layout->ctx->font_ctx, descriptor, codepoint) : nullptr;
    if (emoji) return emoji;
    return font_has_codepoint(primary, codepoint) ? nullptr
        : font_resolve_for_codepoint(layout->ctx->font_ctx, descriptor, codepoint);
}

static float svg_text_natural_width(SvgTextLayout* layout, SvgTextRun* run,
    int* glyph_count, int* word_spaces) {
    const SvgTextStyle* style = &layout->styles[run->style];
    FontStyleDesc descriptor = svg_text_font_descriptor(style, run->font->family);
    FontHandle* handle = font_resolve(layout->ctx->font_ctx, &descriptor);
    float width = 0.0f;
    if (!handle) return width;
    const unsigned char* cursor = (const unsigned char*)layout->chars->str + run->start;
    const unsigned char* end = cursor + run->len;
    while (cursor < end) {
        uint32_t codepoint = 0;
        if (!layout_utf8_next_codepoint(&cursor, end, &codepoint)) continue;
        FontHandle* fallback = svg_text_fallback_face(layout, handle, &descriptor, codepoint);
        if (layout->on_font) layout->on_font(layout->font_context, layout->request_index, fallback ? fallback : handle);
        GlyphInfo glyph = font_get_glyph(fallback ? fallback : handle, codepoint);
        width += glyph.id ? glyph.advance_x : font_get_missing_glyph_advance(handle);
        if (fallback) font_handle_release(fallback);
        (*glyph_count)++;
        if (codepoint == 32u) (*word_spaces)++;
    }
    font_handle_release(handle);
    return width;
}

static float svg_text_baseline_offset(const SvgTextStyle* style, const FontMetrics* metrics) {
    const char* baseline = style->baseline;
    float offset = style->baseline_shift;
    if (!baseline || !metrics) return offset;
    if (strcmp(baseline, "middle") == 0) offset += metrics->x_height * 0.5f;
    else if (strcmp(baseline, "central") == 0) offset += (metrics->ascender + metrics->descender) * 0.5f;
    else if (strcmp(baseline, "hanging") == 0) offset += metrics->ascender * 0.8f;
    else if (strcmp(baseline, "text-before-edge") == 0 || strcmp(baseline, "text-top") == 0) offset += metrics->ascender;
    else if (strcmp(baseline, "text-after-edge") == 0 || strcmp(baseline, "text-bottom") == 0 ||
        strcmp(baseline, "ideographic") == 0) offset += metrics->descender;
    else if (strcmp(baseline, "mathematical") == 0) offset += metrics->x_height * 0.5f;
    return offset;
}

static void svg_text_measure(SvgTextLayout* layout, const RdtMatrix* matrix, bool allow_embedded_font) {
    SvgInlineRenderContext* ctx = layout->ctx;
    for (int i = 0; i < layout->run_count; i++) {
        SvgTextRun* run = &layout->runs[i];
        const SvgTextStyle* style = &layout->styles[run->style];
        run->font = svg_text_font(layout, run->style, allow_embedded_font);
        if (!run->font) continue;
        int word_spaces = 0;
        run->natural_advance = svg_text_natural_width(layout, run, &run->glyph_count, &word_spaces);
        run->advance = run->natural_advance + style->letter_spacing * (float)run->glyph_count +
            style->word_spacing * (float)word_spaces;
        run->glyphs = ctx->font_ctx != nullptr;
        FontStyleDesc descriptor = svg_text_font_descriptor(style, run->font->family);
        FontHandle* handle = font_resolve(ctx->font_ctx, &descriptor);
        if (handle) {
            run->baseline_offset = svg_text_baseline_offset(style, font_get_metrics(handle));
            font_handle_release(handle);
        }
    }
}

// SVG 2 §11.10.1 text-anchor: shift a chunk so its extent [a, b] meets the
// anchor at the chunk's first position, per the first character's element.
static void svg_text_anchor_chunk(SvgTextLayout* layout, int first, int end) {
    SvgTextRun* runs = layout->runs;
    float a = fminf(runs[first].x, runs[first].x + runs[first].advance);
    float b = fmaxf(runs[first].x, runs[first].x + runs[first].advance);
    for (int i = first + 1; i < end; i++) {
        a = fminf(a, fminf(runs[i].x, runs[i].x + runs[i].advance));
        b = fmaxf(b, fmaxf(runs[i].x, runs[i].x + runs[i].advance));
    }
    float anchor = layout->styles[runs[first].style].anchor;
    float shift = runs[first].x - (a + (b - a) * anchor);
    if (shift == 0.0f) return;
    for (int i = first; i < end; i++) runs[i].x += shift;
}

// SVG 2 §11.9: calibrate descendants first, preserving each resolved subtree as a unit.
static void svg_text_resolve_lengths(SvgTextLayout* layout) {
    for (int style_index = layout->style_count - 1; style_index >= 0; style_index--) {
        const SvgTextStyle* style = &layout->styles[style_index];
        int first = style->first_run, end = style->end_run;
        if (style->text_length < 0.0f || first >= end) continue;
        float a = INFINITY, b = -INFINITY, protected_length = 0.0f;
        int units = 0;
        for (int i = first; i < end;) {
            SvgTextRun* run = &layout->runs[i];
            int owner = run->length_owner;
            int unit_end = owner >= 0 ? layout->styles[owner].end_run : i + 1;
            float left = INFINITY, right = -INFINITY;
            for (int j = i; j < unit_end; j++) {
                SvgTextRun* child = &layout->runs[j];
                left = fminf(left, fminf(child->x, child->x + child->advance));
                right = fmaxf(right, fmaxf(child->x, child->x + child->advance));
            }
            a = fminf(a, left); b = fmaxf(b, right);
            if (owner >= 0) protected_length += right - left;
            units++; i = unit_end;
        }
        float natural = b - a, delta = style->text_length - natural;
        bool scale_glyphs = style->scale_glyphs && !layout->allow_embedded_font;
        if (!isfinite(natural) || natural <= 0.0f || (!scale_glyphs && units < 2)) continue;
        float free_length = natural - protected_length;
        float scale = scale_glyphs && free_length > 0.0f
            ? fmaxf((style->text_length - protected_length) / free_length, 0.0f) : 1.0f;
        float step = scale_glyphs ? 0.0f : delta / (float)(units - 1);
        float shift = 0.0f, preceding_protected = 0.0f;
        for (int i = first; i < end;) {
            SvgTextRun* run = &layout->runs[i];
            int owner = run->length_owner;
            int unit_end = owner >= 0 ? layout->styles[owner].end_run : i + 1;
            float left = run->x, right = run->x + run->advance;
            for (int j = i + 1; j < unit_end; j++) {
                left = fminf(left, layout->runs[j].x);
                right = fmaxf(right, layout->runs[j].x + layout->runs[j].advance);
            }
            if (scale_glyphs) shift = (left - a - preceding_protected) * (scale - 1.0f);
            for (int j = i; j < unit_end; j++) {
                SvgTextRun* child = &layout->runs[j];
                child->x += shift;
                if (scale_glyphs && owner < 0) { child->advance *= scale; child->glyph_scale_x *= scale; }
                child->length_owner = style_index;
            }
            if (owner >= 0) preceding_protected += right - left;
            if (!scale_glyphs) shift += step;
            i = unit_end;
        }
        // content after the calibrated subtree continues at its adjusted end.
        float actual_delta = scale_glyphs ? free_length * (scale - 1.0f) : delta;
        for (int i = end; i < layout->run_count; i++) {
            if (layout->styles[layout->runs[i].style].path_owner != style->path_owner) break;
            layout->runs[i].x += actual_delta;
        }
    }
}

static void svg_text_resolve_paths(SvgTextLayout* layout) {
    SvgInlineRenderContext* ctx = layout->ctx;
    layout->paths = (SvgTextPathData*)scratch_calloc(ctx->resource_scratch,
        sizeof(SvgTextPathData) * (size_t)layout->style_count);
    if (!layout->paths) return;
    for (int index = 0; index < layout->style_count; index++) {
        const SvgTextStyle* style = &layout->styles[index];
        if (style->path_owner != index) continue;
        SvgTextPathData* data = &layout->paths[index];
        Element* elem = style->element;
        const char* inline_path = get_svg_attr(elem, "path");
        const char* href = get_svg_href(ctx, elem);
        SvgResourceReference reference = !inline_path && href ? svg_resolve_reference(ctx, href) : SvgResourceReference{};
        RdtPath* path = inline_path ? svg_parse_path_d(inline_path) : nullptr;
        RdtMatrix mapping = rdt_matrix_identity();
        float calibration = 1.0f;
        if (!inline_path && reference.element) {
            SvgInlineRenderContext document = svg_reference_render_context(ctx, &reference);
            SvgInlineRenderContext geometry = svg_resource_style_context(&document, reference.element);
            geometry.transform = rdt_matrix_identity();
            geometry.current_viewport_w = ctx->current_viewport_w; geometry.current_viewport_h = ctx->current_viewport_h;
            SvgLengthContext lengths = svg_length_context(&geometry, reference.element);
            const char* tag = get_element_tag_name(&geometry, reference.element);
            path = tag && strcmp(tag, "path") == 0 ? svg_parse_path_d(get_svg_attr(reference.element, "d")) : rdt_path_new();
            if (path && (!tag || (strcmp(tag, "path") != 0 && !svg_append_basic_shape_path(svg_style_node((SvgStyleContext*)geometry.style_context, reference.element), path, &lengths)))) {
                rdt_path_free(path); path = nullptr;
            }
            // SVG2 section 11.8.2 uses the referenced element's own transform, excluding its ancestors.
            mapping = compose_element_transform(&geometry, reference.element);
            float authored_length = get_svg_number_attr(reference.element, "pathLength", -1.0f);
            if (authored_length > 0.0f) calibration = authored_length;
            else calibration = -1.0f;
        }
        data->valid = path && render_path_metrics_build(&data->metrics, path, &mapping);
        if (path) rdt_path_free(path);
        if (!data->valid) continue;
        SvgLengthContext lengths = svg_text_length_context(ctx, style);
        lengths.viewport_width = lengths.viewport_height = data->metrics.length;
        const char* offset = get_svg_attr(elem, "startOffset");
        data->start_offset = svg_resolve_length(offset, &lengths, SVG_LENGTH_X, 0.0f);
        if (calibration > 0.0f && !inline_path && (!offset || !strchr(offset, '%')))
            data->start_offset *= data->metrics.length / calibration;
        const char* side = get_svg_attr(elem, "side");
        const char* method = get_svg_attr(elem, "method");
        data->reversed = side && strcmp(side, "right") == 0;
        data->stretch = method && strcmp(method, "stretch") == 0;
    }
}

static int svg_text_path_owner(const SvgTextLayout* layout, int run_index) {
    return run_index >= 0 && run_index < layout->run_count ? layout->styles[layout->runs[run_index].style].path_owner : -1;
}

static bool svg_text_path_sample(const SvgTextPathData* data, float distance,
    float* x, float* y, float* tx, float* ty, bool extend = false) {
    if (data->metrics.closed) {
        distance = fmodf(distance, data->metrics.length);
        if (distance < 0.0f) distance += data->metrics.length;
    }
    if (data->reversed) distance = data->metrics.length - distance;
    if (!render_path_metrics_sample(&data->metrics, distance, x, y, tx, ty, extend)) return false;
    if (data->reversed) { *tx = -*tx; *ty = -*ty; }
    return true;
}

static void svg_text_place_on_paths(SvgTextLayout* layout) {
    for (int index = 0; index < layout->run_count; index++) {
        SvgTextRun* run = &layout->runs[index];
        int owner = svg_text_path_owner(layout, index);
        if (owner < 0) continue;
        SvgTextPathData* data = layout->paths ? &layout->paths[owner] : nullptr;
        if (!data || !data->valid) { run->path_hidden = true; continue; }
        float glyph_width = run->natural_advance * run->glyph_scale_x;
        float center = run->x + glyph_width * .5f;
        float distance = center + data->start_offset;
        if (data->metrics.closed) {
            float anchor = layout->styles[owner].anchor;
            float relative = distance - data->start_offset;
            if (relative < -data->metrics.length * anchor || relative > data->metrics.length * (1.0f - anchor)) {
                run->path_hidden = true; continue;
            }
        }
        float x, y, tx, ty;
        if (!svg_text_path_sample(data, distance, &x, &y, &tx, &ty)) { run->path_hidden = true; continue; }
        float x0, y0, x1, y1, tangent_x, tangent_y;
        if (svg_text_path_sample(data, distance - glyph_width * .5f, &x0, &y0, &tangent_x, &tangent_y, true) &&
            svg_text_path_sample(data, distance + glyph_width * .5f, &x1, &y1, &tangent_x, &tangent_y, true)) {
            float chord = hypotf(x1 - x0, y1 - y0);
            if (chord > 0.0f) { tx = (x1 - x0) / chord; ty = (y1 - y0) / chord; }
        }
        run->on_path = true;
        run->path_transform = {tx, -ty, x - tx * center, ty, tx, y - ty * center, 0.0f, 0.0f, 1.0f};
    }
}

static void svg_text_place(SvgTextLayout* layout) {
    svg_text_resolve_paths(layout);
    float pen_x = 0.0f, pen_y = 0.0f;
    float before_path_x = 0.0f, before_path_y = 0.0f;
    for (int i = 0; i < layout->run_count; i++) {
        SvgTextRun* run = &layout->runs[i];
        int owner = svg_text_path_owner(layout, i), previous = svg_text_path_owner(layout, i - 1);
        if (owner != previous) {
            if (previous >= 0 && layout->paths && layout->paths[previous].valid) {
                const SvgTextPathData* path = &layout->paths[previous];
                float tx, ty;
                float endpoint = path->metrics.length + (path->metrics.closed ? path->start_offset : 0.0f);
                svg_text_path_sample(path, endpoint, &pen_x, &pen_y, &tx, &ty, true);
            } else if (previous >= 0) { pen_x = before_path_x; pen_y = before_path_y; }
            if (owner >= 0) { before_path_x = pen_x; before_path_y = pen_y; pen_x = pen_y = 0.0f; }
        }
        pen_x += run->adjust.dx; pen_y += run->adjust.dy;
        run->x = pen_x; run->y = pen_y + run->baseline_offset;
        pen_x += run->advance;
    }
    svg_text_resolve_lengths(layout);
    // absolute lists are applied after textLength, before anchoring (SVG 2 §11.9).
    float shift_x = 0.0f, shift_y = 0.0f;
    float before_path_shift_x = 0.0f, before_path_shift_y = 0.0f;
    int chunk_start = 0;
    for (int i = 0; i < layout->run_count; i++) {
        SvgTextRun* run = &layout->runs[i];
        int owner = svg_text_path_owner(layout, i), previous = svg_text_path_owner(layout, i - 1);
        if (i > chunk_start && (owner != previous || run->adjust.has_x || (owner < 0 && run->adjust.has_y))) {
            svg_text_anchor_chunk(layout, chunk_start, i); chunk_start = i;
        }
        if (owner != previous) {
            if (previous >= 0 && (!layout->paths || !layout->paths[previous].valid)) {
                shift_x = before_path_shift_x; shift_y = before_path_shift_y;
            } else if (previous >= 0) shift_x = shift_y = 0.0f;
            if (owner >= 0) {
                before_path_shift_x = shift_x; before_path_shift_y = shift_y;
                shift_x = shift_y = 0.0f;
            }
        }
        if (run->adjust.has_x) shift_x = run->adjust.x + run->adjust.dx - run->x;
        if (owner < 0 && run->adjust.has_y) shift_y = run->adjust.y + run->adjust.dy + run->baseline_offset - run->y;
        run->x += shift_x; run->y += shift_y;
    }
    if (layout->run_count > chunk_start) svg_text_anchor_chunk(layout, chunk_start, layout->run_count);
    svg_text_place_on_paths(layout);
}

// ThorVG fallback for runs the glyph path cannot draw (no active raster
// context, e.g. SVG pictures). ThorVG places text by its top edge.
static void svg_text_draw_tvg_run(SvgInlineRenderContext* ctx, const RdtMatrix* m,
                                  SvgTextFont* font, const SvgTextStyle* style,
                                  const char* text, float x, float y, float fit_width) {
    if (!svg_text_font_path(ctx, font, style, false)) return;
    Tvg_Paint paint = create_text_segment(text, x, y, font->path,
                                          font->name[0] ? font->name : nullptr,
                                          style->font_size, style->fill);
    if (!paint) return;
    float scale_x = 1.0f;
    if (fit_width > 0.0f) {
        // ThorVG text cannot be respaced, so a fitted run scales its glyphs
        float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
        if (tvg_paint_get_aabb(paint, &bx, &by, &bw, &bh) == TVG_RESULT_SUCCESS && bw > 0.0f) {
            scale_x = fit_width / bw;
        }
    }
    RdtMatrix local = rdt_matrix_translate(x, y - style->font_size * font->ascent_ratio);
    if (fabsf(scale_x - 1.0f) > 0.001f) {
        RdtMatrix scale = { scale_x, 0, 0,  0, 1, 0,  0, 0, 1 };
        local = rdt_matrix_multiply(&local, &scale);
    }
    RdtMatrix final_m = rdt_matrix_multiply(m, &local);
    RdtPicture* pic = rdt_picture_take_tvg_paint(paint, 0, 0);
    if (pic) svg_draw_picture(ctx, pic, 255, &final_m);
}

static RdtMatrix svg_text_run_transform(const SvgTextRun* run, const RdtMatrix* parent) {
    RdtMatrix frame = run->on_path ? rdt_matrix_multiply(parent, &run->path_transform) : *parent;
    if (!run->adjust.has_rotate || run->adjust.rotate == 0.0f) return frame;
    float radians = run->adjust.rotate * ((float)M_PI / 180.0f);
    float cosine = cosf(radians), sine = sinf(radians);
    RdtMatrix rotation = {cosine, -sine, run->x * (1.0f - cosine) + run->y * sine,
        sine, cosine, run->y * (1.0f - cosine) - run->x * sine, 0.0f, 0.0f, 1.0f};
    return rdt_matrix_multiply(&frame, &rotation);
}

struct SvgTextWarpContext {
    const SvgTextPathData* data;
    RdtPath* output;
    float x, y, start_x, start_y;
};

static bool svg_text_warp_point(SvgTextWarpContext* ctx, float x, float y, float* px, float* py) {
    float tx, ty;
    if (!svg_text_path_sample(ctx->data, x + ctx->data->start_offset, px, py, &tx, &ty, true)) return false;
    *px -= ty * y; *py += tx * y;
    return isfinite(*px) && isfinite(*py);
}

static bool svg_text_warp_edge(SvgTextWarpContext* ctx, float x0, float y0, float x1, float y1,
    float px0, float py0, float px1, float py1, unsigned depth) {
    float samples[6], error = 0.0f;
    for (size_t point = 0; point < 3; point++) {
        float fraction = (float)(point + 1) * .25f;
        if (!svg_text_warp_point(ctx, x0 + (x1 - x0) * fraction,
            y0 + (y1 - y0) * fraction, &samples[point * 2], &samples[point * 2 + 1])) return false;
        error = fmaxf(error, hypotf(samples[point * 2] - (px0 + (px1 - px0) * fraction),
            samples[point * 2 + 1] - (py0 + (py1 - py0) * fraction)));
    }
    // sampled positions are curve points, not Bezier control points; bound the warped edge error.
    if (error <= .03f || depth >= 16) { rdt_path_line_to(ctx->output, px1, py1); return true; }
    float xm = (x0 + x1) * .5f, ym = (y0 + y1) * .5f;
    return svg_text_warp_edge(ctx, x0, y0, xm, ym, px0, py0, samples[2], samples[3], depth + 1) &&
        svg_text_warp_edge(ctx, xm, ym, x1, y1, samples[2], samples[3], px1, py1, depth + 1);
}

static bool svg_text_warp_line(void* data, float x0, float y0, float x1, float y1) {
    SvgTextWarpContext* ctx = (SvgTextWarpContext*)data;
    float px0, py0, px1, py1;
    if (!svg_text_warp_point(ctx, x0, y0, &px0, &py0) || !svg_text_warp_point(ctx, x1, y1, &px1, &py1)) return false;
    return svg_text_warp_edge(ctx, x0, y0, x1, y1, px0, py0, px1, py1, 0);
}

static bool svg_text_warp_visit(void* data, RdtPathCommand command, const float* args, int count) {
    SvgTextWarpContext* ctx = (SvgTextWarpContext*)data;
    if (command == RDT_PATH_CLOSE) {
        if (!svg_text_warp_line(ctx, ctx->x, ctx->y, ctx->start_x, ctx->start_y)) return false;
        ctx->x = ctx->start_x; ctx->y = ctx->start_y;
        rdt_path_close(ctx->output); return true;
    }
    if (command == RDT_PATH_LINE || command == RDT_PATH_CUBIC) {
        bool valid = command == RDT_PATH_LINE ? svg_text_warp_line(ctx, ctx->x, ctx->y, args[0], args[1]) :
            render_path_flatten_cubic(ctx->x, ctx->y, args[0], args[1], args[2], args[3], args[4], args[5],
                .03f, 16, svg_text_warp_line, ctx);
        ctx->x = args[count - 2]; ctx->y = args[count - 1]; return valid;
    }
    if (command != RDT_PATH_MOVE) return false;
    float values[6];
    for (int point = 0; point < count; point += 2)
        if (!svg_text_warp_point(ctx, args[point], args[point + 1], &values[point], &values[point + 1])) return false;
    rdt_path_move_to(ctx->output, values[0], values[1]); ctx->start_x = args[0]; ctx->start_y = args[1];
    ctx->x = args[count - 2]; ctx->y = args[count - 1];
    return true;
}

static const SvgTextPathData* svg_text_stretch_data(const SvgTextLayout* layout, const SvgTextRun* run) {
    int owner = layout->styles[run->style].path_owner;
    return layout->paths && owner >= 0 && layout->paths[owner].valid && layout->paths[owner].stretch
        ? &layout->paths[owner] : nullptr;
}

static RdtPath* svg_text_warp_path(const SvgTextPathData* data, const RdtPath* source) {
    RdtPath* result = rdt_path_new();
    if (!result) return nullptr;
    SvgTextWarpContext ctx = {data, result, 0, 0, 0, 0};
    if (!rdt_path_visit(source, svg_text_warp_visit, &ctx)) { rdt_path_free(result); return nullptr; }
    return result;
}

static RdtPath* svg_text_warp_run_path(const SvgTextLayout* layout, const SvgTextRun* run, const RdtPath* source) {
    const SvgTextPathData* data = svg_text_stretch_data(layout, run);
    if (!data) return nullptr;
    RdtMatrix identity = rdt_matrix_identity();
    SvgTextRun normal = *run; normal.on_path = false;
    RdtMatrix rotation = svg_text_run_transform(&normal, &identity);
    RdtPath* rotated = rdt_path_new();
    bool valid = rotated && render_path_append_transformed(rotated, source, &rotation);
    RdtPath* warped = valid ? svg_text_warp_path(data, rotated) : nullptr;
    if (rotated) rdt_path_free(rotated);
    return warped;
}

static RdtPath* svg_text_run_path(SvgTextLayout* layout, SvgTextRun* run) {
    const SvgTextStyle* style = &layout->styles[run->style];
    FontStyleDesc descriptor = svg_text_font_descriptor(style, run->font->family);
    FontHandle* handle = font_resolve(layout->ctx->font_ctx, &descriptor);
    if (!handle) return nullptr;
    RdtPath* path = rdt_path_new();
    const unsigned char* begin = (const unsigned char*)layout->chars->str + run->start;
    const unsigned char* end = begin + run->len;
    float stretch = run->glyph_scale_x;
    float pen = run->x;
    bool outlines = path != nullptr;
    Arena* arena = mem_arena_create(nullptr, MEM_ROLE_RENDER, "render.svg.glyph_path");
    for (const unsigned char* cursor = begin; outlines && cursor < end;) {
        uint32_t codepoint = 0;
        if (!layout_utf8_next_codepoint(&cursor, end, &codepoint)) continue;
        FontHandle* fallback = svg_text_fallback_face(layout, handle, &descriptor, codepoint);
        FontHandle* face = fallback ? fallback : handle;
        outlines = render_path_append_font_glyph(path, face, codepoint, pen, run->y, stretch, arena, &run->color_bitmap);
        pen += font_get_glyph(face, codepoint).advance_x * stretch;
        if (fallback) font_handle_release(fallback);
    }
    mem_arena_destroy(arena);
    font_handle_release(handle);
    if (!outlines) { rdt_path_free(path); return nullptr; }
    const SvgTextPathData* stretch_data = svg_text_stretch_data(layout, run);
    if (stretch_data) {
        RdtPath* warped = svg_text_warp_run_path(layout, run, path);
        rdt_path_free(path); path = warped;
        run->paint_warped = warped != nullptr;
    }
    return path;
}

static void svg_text_append_cell(SvgTextLayout* layout, SvgTextRun* run, RdtPath* path) {
    if (run->path_hidden) return;
    const SvgTextStyle* style = &layout->styles[run->style];
    FontStyleDesc descriptor = svg_text_font_descriptor(style, run->font->family);
    FontHandle* handle = font_resolve(layout->ctx->font_ctx, &descriptor);
    if (!handle) return;
    FontHandle* fallback = svg_text_fallback_face(layout, handle, &descriptor, run->codepoint);
    const FontMetrics* metrics = font_get_metrics(fallback ? fallback : handle);
    if (!metrics) { if (fallback) font_handle_release(fallback); font_handle_release(handle); return; }
    RdtMatrix identity = rdt_matrix_identity();
    const SvgTextPathData* stretch_data = svg_text_stretch_data(layout, run);
    RdtMatrix matrix = stretch_data ? identity : svg_text_run_transform(run, &identity);
    RdtPath* cell = stretch_data ? rdt_path_new() : path;
    if (!cell) { if (fallback) font_handle_release(fallback); font_handle_release(handle); return; }
    // SVG text targeting uses character cells; gaps between positioned cells stay empty.
    float cell_width = run->advance;
    float xs[] = {run->x, run->x + cell_width, run->x + cell_width, run->x};
    float ys[] = {run->y - metrics->ascender, run->y - metrics->ascender,
        run->y - metrics->descender, run->y - metrics->descender};
    for (int corner = 0; corner < 4; corner++) {
        float x, y; rdt_matrix_transform_point(&matrix, xs[corner], ys[corner], &x, &y);
        if (corner == 0) rdt_path_move_to(cell, x, y); else rdt_path_line_to(cell, x, y);
    }
    rdt_path_close(cell);
    if (stretch_data) {
        RdtPath* warped = svg_text_warp_run_path(layout, run, cell);
        if (warped) { render_path_append_transformed(path, warped, &identity); rdt_path_free(warped); }
        rdt_path_free(cell);
    }
    if (fallback) font_handle_release(fallback);
    font_handle_release(handle);
}

static void svg_text_prepare_paint_geometry(SvgTextLayout* layout, const RdtMatrix* transform) {
    layout->paint_transform = *transform;
    RdtPath* combined = rdt_path_new();
    for (int i = 0; i < layout->run_count; i++) {
        SvgTextRun* run = &layout->runs[i];
        if (!run->font || run->path_hidden || run->glyph_scale_x <= 0.0f) continue;
        svg_text_append_cell(layout, run, combined);
        if (!str_all(layout->chars->str + run->start, run->len, str_is_space))
            run->paint_path = svg_text_run_path(layout, run);
    }
    rdt_path_get_bounds(combined, &layout->paint_box.left, &layout->paint_box.top,
        &layout->paint_box.right, &layout->paint_box.bottom);
    rdt_path_free(combined);
}

struct SvgTextColorWarpSample {
    const SvgTextPathData* path;
    ImageSurface* bitmap;
    RdtMatrix inverse, inverse_rotation;
    float x, y, scale_x, scale_y, from, to;
};

static uint32_t svg_text_sample_color_warp(void* data, float x, float y) {
    SvgTextColorWarpSample* sample = (SvgTextColorWarpSample*)data;
    float px, py, distance, normal;
    rdt_matrix_transform_point(&sample->inverse, x, y, &px, &py);
    if (!render_path_metrics_project(&sample->path->metrics, px, py, sample->from, sample->to,
        &distance, &normal, sample->path->metrics.closed)) return 0;
    if (sample->path->reversed) { distance = sample->path->metrics.length - distance; normal = -normal; }
    rdt_matrix_transform_point(&sample->inverse_rotation, distance - sample->path->start_offset, normal, &px, &py);
    px = (px - sample->x) / sample->scale_x; py = (py - sample->y) / sample->scale_y;
    if (px < 0.0f || py < 0.0f || px >= (float)sample->bitmap->width || py >= (float)sample->bitmap->height) return 0;
    return render_pixel_sample_bilinear((uint8_t*)sample->bitmap->pixels, sample->bitmap->width,
        sample->bitmap->height, sample->bitmap->pitch, px - .5f, py - .5f, false, true);
}

static bool svg_text_draw_bitmap_glyph(SvgTextLayout* layout, SvgTextRun* run,
    const RdtMatrix* matrix, bool native_text = false) {
    SvgInlineRenderContext* ctx = layout->ctx;
    if (ctx->fill_none || !ctx->paint_list) return false;
    const SvgTextStyle* style = &layout->styles[run->style];
    FontStyleDesc descriptor = svg_text_font_descriptor(style, run->font->family);
    FontHandle* primary = font_resolve(ctx->font_ctx, &descriptor);
    if (native_text && primary) {
        // SVG images may share a host font context with a different device ratio.
        float ratio = font_handle_get_physical_size_px(primary) / font_handle_get_size_px(primary);
        descriptor.size_px *= matrix->e22 / ratio;
        font_handle_release(primary);
        primary = font_resolve(ctx->font_ctx, &descriptor);
    }
    FontHandle* fallback = svg_text_fallback_face(layout, primary, &descriptor, run->codepoint);
    FontHandle* font = fallback ? fallback : primary;
    if (fallback && primary) font_handle_release(primary);
    if (!font) return false;
    const GlyphBitmap* bitmap = font_render_glyph(font, run->codepoint, GLYPH_RENDER_NORMAL);
    if (!bitmap || (!native_text && bitmap->pixel_mode != GLYPH_PIXEL_BGRA) || !bitmap->buffer ||
        (native_text && bitmap->bitmap_scale > 0.0f && fabsf(bitmap->bitmap_scale - 1.0f) > 0.00001f)) {
        font_handle_release(font); return false;
    }
    ImageSurface* image = image_surface_create(bitmap->width, bitmap->height);
    if (!image) { font_handle_release(font); return false; }
    if (native_text) {
        // isolated SVG font contexts die after recording; keep owned pixels for retained replay.
        memset(image->pixels, 0, (size_t)image->pitch * image->height);
        DlDrawGlyph glyph = {};
        glyph.bitmap = *bitmap; glyph.color = ctx->fill_color;
        glyph.is_color_emoji = bitmap->pixel_mode == GLYPH_PIXEL_BGRA;
        glyph.clip = {0.0f, 0.0f, (float)image->width, (float)image->height};
        dl_replay_draw_glyph(image, &glyph);
    } else for (int row = 0; row < bitmap->height; row++) {
        const uint8_t* source = bitmap->buffer + (ptrdiff_t)row * bitmap->pitch;
        uint32_t* pixels = (uint32_t*)((uint8_t*)image->pixels + (size_t)row * image->pitch);
        for (int column = 0; column < bitmap->width; column++) {
            uint8_t alpha = source[4 * column + 3];
            pixels[column] = render_pixel_pack_abgr(
                source[4 * column + 2], source[4 * column + 1], source[4 * column], alpha);
        }
    }
    float scale = font_handle_get_size_px(font) / font_handle_get_physical_size_px(font);
    if (bitmap->bitmap_scale > 0.0f) scale *= bitmap->bitmap_scale;
    RdtMatrix placement = {scale * run->glyph_scale_x, 0.0f,
        run->x + (float)bitmap->bearing_x * scale * run->glyph_scale_x,
        0.0f, scale, run->y - (float)bitmap->bearing_y * scale, 0.0f, 0.0f, 1.0f};
    if (native_text) {
        placement = rdt_matrix_translate(
            lroundf(matrix->e11 * run->x + matrix->e13 + (float)bitmap->bearing_x),
            lroundf(matrix->e22 * run->y + matrix->e23 - (float)bitmap->bearing_y));
    }
    const SvgTextPathData* stretch_data = svg_text_stretch_data(layout, run);
    if (run->paint_warped && stretch_data && run->paint_path) {
        SvgTextColorWarpSample sample = {};
        sample.path = stretch_data; sample.bitmap = image;
        sample.x = placement.e13; sample.y = placement.e23;
        sample.scale_x = placement.e11; sample.scale_y = placement.e22;
        RdtMatrix identity = rdt_matrix_identity();
        SvgTextRun normal_run = *run; normal_run.on_path = false;
        RdtMatrix rotation = svg_text_run_transform(&normal_run, &identity);
        float left, top, right, bottom;
        rdt_matrix_transform_rect_bounds(&rotation, sample.x, sample.y,
            sample.x + (float)image->width * sample.scale_x, sample.y + (float)image->height * sample.scale_y,
            &left, &top, &right, &bottom);
        sample.from = left + stretch_data->start_offset; sample.to = right + stretch_data->start_offset;
        if (stretch_data->metrics.closed) {
            float center = run->x + run->natural_advance * run->glyph_scale_x * .5f + stretch_data->start_offset;
            sample.from = fmaxf(sample.from, center - stretch_data->metrics.length * .5f);
            sample.to = fminf(sample.to, center + stretch_data->metrics.length * .5f);
        }
        if (stretch_data->reversed) {
            float first = stretch_data->metrics.length - sample.to;
            sample.to = stretch_data->metrics.length - sample.from; sample.from = first;
        }
        if (rdt_matrix_invert_affine(matrix, &sample.inverse) && rdt_matrix_invert_affine(&rotation, &sample.inverse_rotation))
            svg_rasterize_path_paint(ctx, run->paint_path, matrix, RDT_FILL_WINDING,
                ctx->fill_opacity * ctx->opacity, nullptr, svg_text_sample_color_warp, &sample, "svg_color_glyph_warp");
        image_surface_destroy(image);
    } else {
        if (!native_text) placement = rdt_matrix_multiply(matrix, &placement);
        // glyph-cache storage is borrowed; the retained image owns its converted pixels.
        PaintRecordTarget destination = svg_record_target(ctx);
        paint_record_draw_image_resource(&destination, "svg_glyph_image", image, 0.0f, 0.0f,
            (float)image->width, (float)image->height,
            clamp_byte_round(255.0f * ctx->fill_opacity * ctx->opacity), &placement, image_surface_destroy);
    }
    font_handle_release(font);
    return true;
}

static bool svg_text_can_paint_native(const SvgTextLayout* layout, const SvgTextRun* run,
    const RdtMatrix* matrix) {
    const SvgInlineRenderContext* ctx = layout->ctx;
    // retain vector geometry for exports, paint servers, strokes, and transformed glyphs.
    return g_svg_active_rdcon && !ctx->semantic_target && ctx->font_ctx &&
        (ctx->fill_paint.kind == SVG_PAINT_COLOR || ctx->fill_paint.kind == SVG_PAINT_CURRENT_COLOR) && ctx->stroke_none &&
        !run->on_path && !run->paint_warped &&
        fabsf(run->glyph_scale_x - 1.0f) < 0.00001f &&
        matrix->e11 > 0.0f && matrix->e22 > 0.0f &&
        fabsf(matrix->e12) < 0.00001f && fabsf(matrix->e21) < 0.00001f &&
        fabsf(matrix->e11 - matrix->e22) <= matrix->e22 * 0.00001f;
}

static void svg_text_draw_decoration(SvgTextLayout* layout, int style_index, bool after_glyphs) {
    SvgInlineRenderContext* ctx = layout->ctx;
    const SvgTextStyle* style = &layout->styles[style_index];
    if (!style->decoration || style->decoration_element != style->element) return;
    FontStyleDesc descriptor = svg_text_font_descriptor(style);
    FontHandle* handle = font_resolve(ctx->font_ctx, &descriptor);
    if (!handle) return;
    const FontMetrics* metrics = font_get_metrics(handle);
    if (!metrics) { font_handle_release(handle); return; }
    float thickness = fmaxf(ceilf(metrics->underline_thickness), 1.0f);
    SvgLengthContext lengths = {ctx->current_viewport_w, ctx->current_viewport_h,
        style->font_size, metrics->x_height};
    const char* authored = svg_style_property_value(ctx, style->element, "text-decoration-thickness");
    if (authored && strcmp(authored, "auto") != 0 && strcmp(authored, "from-font") != 0)
        thickness = fmaxf(svg_resolve_length(authored, &lengths, SVG_LENGTH_X, thickness), 0.0f);
    const char* shorthand = svg_style_property_value(ctx, style->element, "text-decoration");
    const char* decoration_name = svg_style_property_value(ctx, style->element, "text-decoration-style");
    CssEnum decoration_style = CSS_VALUE_SOLID;
    const char* style_names[] = {"solid", "double", "dotted", "dashed", "wavy"};
    CssEnum styles[] = {CSS_VALUE_SOLID, CSS_VALUE_DOUBLE, CSS_VALUE_DOTTED, CSS_VALUE_DASHED, CSS_VALUE_WAVY};
    for (int i = 0; i < 5; i++) if ((decoration_name && strcmp(decoration_name, style_names[i]) == 0) ||
        (!decoration_name && shorthand && strstr(shorthand, style_names[i]))) decoration_style = styles[i];
    char shorthand_color[256] = {};
    if (shorthand) {
        const char* token = shorthand;
        while (*token) {
            token = str_skip_ascii_space(token); const char* end = token;
            while (*end && !str_is_space(*end)) end++;
            size_t length = (size_t)(end - token);
            bool keyword = str_eq_const(token, length, "underline") || str_eq_const(token, length, "overline") ||
                str_eq_const(token, length, "line-through") || str_eq_const(token, length, "none");
            for (int i = 0; i < 5; i++) keyword = keyword || str_eq(token, length, style_names[i], strlen(style_names[i]));
            if (length && !keyword) { str_copy(shorthand_color, sizeof(shorthand_color), token, strlen(token)); break; }
            token = end;
        }
    }
    const char* fill = svg_style_property_value(ctx, style->element, "text-decoration-fill");
    if (!fill) fill = svg_style_property_value(ctx, style->element, "text-decoration-color");
    if (!fill && shorthand_color[0]) fill = shorthand_color;
    const char* stroke = svg_style_property_value(ctx, style->element, "text-decoration-stroke");
    for (int i = style->first_run; i < style->end_run; i++) {
        SvgTextRun* run = &layout->runs[i];
        const SvgTextStyle* child = &layout->styles[run->style];
        if (child->hidden || run->path_hidden || run->advance <= 0.0f) continue;
        float tops[] = {floorf(run->y - metrics->underline_position - thickness * 0.5f),
            floorf(run->y - metrics->ascender - thickness),
            floorf(run->y - metrics->strikeout_position - thickness * 0.5f)};
        RdtMatrix matrix = svg_text_run_transform(run, &ctx->transform);
        for (int line = 0; line < 3; line++) {
            if (!(style->decoration & (1u << line)) || (line == 2) != after_glyphs) continue;
            Rect rect = {run->x, tops[line], run->advance, thickness};
            RdtPath* path = render_path_create_decoration(&rect, decoration_style, true);
            if (!path) continue;
            if (svg_text_stretch_data(layout, run)) {
                RdtPath* warped = svg_text_warp_run_path(layout, run, path);
                rdt_path_free(path); path = warped; matrix = ctx->transform;
                if (!path) continue;
            }
            draw_svg_fill_stroke(ctx, path, style->element, &matrix,
                layout->paint_box.left, layout->paint_box.top,
                layout->paint_box.right - layout->paint_box.left, layout->paint_box.bottom - layout->paint_box.top,
                false, fill, stroke, &layout->paint_transform);
            rdt_path_free(path);
        }
    }
    font_handle_release(handle);
}

struct SvgTextDrawScope {
    SvgTextLayout* layout;
    int style_index;
    bool fitted, scale_glyphs;
};

static void svg_text_draw_style(SvgTextLayout* layout, int style_index, bool fitted, bool scale_glyphs);
static void svg_text_draw_style_content(SvgTextLayout* layout, int style_index, bool fitted, bool scale_glyphs);

static void svg_text_draw_scope(SvgInlineRenderContext* ctx, Element*, void* data) {
    SvgTextDrawScope* scope = (SvgTextDrawScope*)data;
    SvgInlineRenderContext* saved = scope->layout->ctx;
    scope->layout->ctx = ctx;
    svg_text_draw_style_content(scope->layout, scope->style_index, scope->fitted, scope->scale_glyphs);
    scope->layout->ctx = saved;
}

// tspan effects capture the already positioned glyphs without collecting text twice.
static void svg_text_draw_style(SvgTextLayout* layout, int style_index, bool fitted, bool scale_glyphs) {
    SvgTextDrawScope scope = {layout, style_index, fitted, scale_glyphs};
    if (!svg_render_effect_boundary(layout->ctx, layout->styles[style_index].element, svg_text_draw_scope, &scope))
        svg_text_draw_style_content(layout, style_index, fitted, scale_glyphs);
}

static void svg_text_draw_style_content(SvgTextLayout* layout, int style_index,
    bool fitted, bool scale_glyphs) {
    SvgInlineRenderContext* ctx = layout->ctx;
    const SvgTextStyle* style = &layout->styles[style_index];
    SvgGroupScope scope = {};
    svg_group_enter(ctx, style->element, &scope);
    svg_text_draw_decoration(layout, style_index, false);
    for (int i = style->first_run; i < style->end_run;) {
        SvgTextRun* run = &layout->runs[i];
        if (run->style != style_index) {
            int child = run->style;
            while (layout->styles[child].parent != style_index) child = layout->styles[child].parent;
            svg_text_draw_style(layout, child, fitted, scale_glyphs);
            i = layout->styles[child].end_run;
            continue;
        }
        i++;
        const char* text = layout->chars->str + run->start;
        if (style->hidden || run->path_hidden || !run->font || run->glyph_scale_x <= 0.0f || str_all(text, run->len, str_is_space)) continue;
        RdtMatrix matrix = run->paint_warped ? ctx->transform : svg_text_run_transform(run, &ctx->transform);
        RdtPath* path = run->paint_path;
        if (svg_text_can_paint_native(layout, run, &matrix) &&
            svg_text_draw_bitmap_glyph(layout, run, &matrix, true)) {
            if (path) rdt_path_free(path);
            run->paint_path = nullptr;
            continue;
        }
        if (path) {
            float left, top, right, bottom;
            if (rdt_path_get_bounds(path, &left, &top, &right, &bottom)) {
                if (run->color_bitmap) svg_text_draw_bitmap_glyph(layout, run, &matrix);
                draw_svg_fill_stroke(ctx, path, style->element, &matrix,
                    layout->paint_box.left, layout->paint_box.top,
                    layout->paint_box.right - layout->paint_box.left,
                    layout->paint_box.bottom - layout->paint_box.top, false,
                    run->color_bitmap ? "none" : nullptr, nullptr, &layout->paint_transform);
            }
            rdt_path_free(path); run->paint_path = nullptr;
        } else {
            Color fill = ctx->fill_color;
            fill.a = clamp_byte_round((float)fill.a * ctx->fill_opacity * ctx->opacity);
            if (!ctx->fill_none && run->glyphs && render_svg_text_with_radiant_glyphs(ctx,
                text, run->font->family, style->font_size, style->font_weight, style->font_slant,
                fill, &matrix, run->x, run->y, fitted ? run->advance : 0.0f, scale_glyphs)) continue;
            if (!ctx->fill_none) {
                SvgTextStyle fallback_style = *style; fallback_style.fill = fill;
                svg_text_draw_tvg_run(ctx, &matrix, run->font, &fallback_style, text,
                    run->x, run->y, fitted ? run->advance : 0.0f);
            }
        }
    }
    svg_text_draw_decoration(layout, style_index, true);
    svg_group_leave(ctx, &scope);
}

static bool svg_text_layout_collect(SvgInlineRenderContext* ctx, Element* elem, SvgTextLayout* output) {
    // PDF-generated SVG uses textLength to preserve exact run advances when
    // embedded PDF subset fonts fall back to metric-different system fonts.
    const char* raw_font_attr = get_svg_attr(elem, "data-pdf-raw-font");
    bool allow_embedded_font = raw_font_attr && strcmp(raw_font_attr, "true") == 0;

    SvgTextStyle root = {};
    root.element = elem; root.parent = -1; root.path_owner = -1;
    if (ctx->inherited_font_family) {
        str_copy(root.font_family, sizeof(root.font_family), ctx->inherited_font_family,
                 strlen(ctx->inherited_font_family));
    }
    root.font_size = ctx->inherited_font_size > 0 ? ctx->inherited_font_size : 16.0f;
    root.font_weight = ctx->inherited_font_weight > 0 ? ctx->inherited_font_weight : 400;
    root.font_slant = ctx->inherited_font_style && strcmp(ctx->inherited_font_style, "italic") == 0
        ? FONT_SLANT_ITALIC : ctx->inherited_font_style && strcmp(ctx->inherited_font_style, "oblique") == 0
        ? FONT_SLANT_OBLIQUE : FONT_SLANT_NORMAL;
    root.hidden = ctx->visibility_hidden;
    root.fill = ctx->fill_none ? Color{} : ctx->fill_color;
    root.anchor = svg_text_anchor_value(ctx->inherited_text_anchor);
    svg_text_style_apply(ctx, elem, &root);

    SvgTextLayout layout = {};
    layout.ctx = ctx;
    layout.open_run = -1;
    layout.trailing_space_run = -1;
    layout.space_before = true;  // white space at the start of the line collapses away
    layout.chars = strbuf_new_cap(64);
    int root_index = -1;
    if (layout.chars && svg_text_push_style(&layout, &root, &root_index)) {
        svg_text_collect(&layout, elem, root_index);
        svg_text_trim_end(&layout);
        strbuf_append_char(layout.chars, '\0');
        layout.fonts = layout.run_count > 0
            ? (SvgTextFont*)scratch_calloc(ctx->resource_scratch,
                                           sizeof(SvgTextFont) * (size_t)layout.style_count)
            : nullptr;
    }
    layout.text_length = root.text_length;
    layout.allow_embedded_font = allow_embedded_font;
    layout.scale_glyphs = root.scale_glyphs && !allow_embedded_font;
    for (int i = 0; i < layout.style_count; i++)
        layout.styles[i].end_run = LMB_MIN(layout.styles[i].end_run, layout.run_count);
    *output = layout;
    return layout.fonts != nullptr;
}

static void render_svg_text(SvgInlineRenderContext* ctx, Element* elem) {
    if (!ctx || !elem || !ctx->resource_scratch) return;
    ScratchMark mark = scratch_mark(ctx->resource_scratch);
    SvgTextLayout layout = {};
    if (svg_text_layout_collect(ctx, elem, &layout)) {
        RdtMatrix matrix = compose_element_transform(ctx, elem);
        svg_text_measure(&layout, &matrix, layout.allow_embedded_font);
        svg_text_place(&layout);
        svg_text_prepare_paint_geometry(&layout, &matrix);
        svg_text_draw_style(&layout, 0, layout.text_length > 0.0f, layout.scale_glyphs);
        for (int i = 0; i < layout.run_count; i++) if (layout.runs[i].paint_path) rdt_path_free(layout.runs[i].paint_path);
        svg_text_release_fonts(&layout);
    }
    if (layout.chars) strbuf_free(layout.chars);
    scratch_restore(ctx->resource_scratch, mark);
}

struct SvgTextGeometryScope {
    Arena* arena;
    ScratchArena scratch;
    SvgStyleContext style;
    FontContext* fonts;
    bool owns_fonts;
    char* source_path;
    SvgInlineRenderContext context;
};

static void svg_text_geometry_scope_close(SvgTextGeometryScope* scope) {
    svg_style_destroy(&scope->style);
    scratch_release(&scope->scratch);
    if (scope->arena) mem_arena_destroy(scope->arena);
    if (scope->owns_fonts && scope->fonts) font_context_destroy(scope->fonts);
    if (scope->source_path) mem_free(scope->source_path);
}

static bool svg_text_geometry_scope_open(SvgTextGeometryScope* scope, DomElement* svg,
    const SvgLengthContext* lengths, FontContext* fonts) {
    *scope = {};
    scope->fonts = fonts;
    scope->owns_fonts = !fonts;
    scope->arena = mem_arena_create(nullptr, MEM_ROLE_RENDER, "render.svg.text_geometry");
    mem_scratch_init(nullptr, &scope->scratch, scope->arena, MEM_ROLE_RENDER, "render.svg.text_geometry.scratch");
    Element* source = dom_element_to_element(svg);
    // a live DOM query shares the document cascade; indexing the entire host
    // for each glyph made pointer targeting quadratic in document size.
    SvgPaintHostStyle* shared = svg_host_style(svg->doc);
    SvgStyleContext* style = shared ? &shared->style : &scope->style;
    if (!shared && !svg_style_init_host(style, svg->doc)) return false;
    scope->source_path = radiant_document_resource_base(svg->doc, MEM_CAT_FONT);
    if (scope->owns_fonts) scope->fonts = svg_style_font_context(style,
        scope->source_path, 1.0f, false, nullptr);
    if (!scope->fonts) return false;
    SvgInlineRenderContext* ctx = &scope->context;
    ctx->svg_root = lam::up(source); ctx->id_scope = lam::up(render_svg_reference_scope(svg));
    ctx->source_path = lam::up(scope->source_path); ctx->resource_scratch = lam::up(&scope->scratch);
    ctx->font_ctx = lam::up(scope->fonts); ctx->style_context = lam::up(style);
    ctx->transform = rdt_matrix_identity();
    ctx->current_viewport_w = lengths->viewport_width; ctx->current_viewport_h = lengths->viewport_height;
    ctx->inherited_font_size = 16.0f; ctx->inherited_font_weight = 400;
    ctx->fill_color = parse_svg_color("black"); ctx->current_color = ctx->fill_color;
    ctx->stroke_none = true;
    ctx->fill_opacity = ctx->stroke_opacity = ctx->opacity = 1.0f;
    return true;
}

// measurement adapters share the painter's placement, fallback and ink paths.
static RdtPath* svg_text_layout_geometry(SvgTextLayout* layout, Element* target_source,
    bool collected, SvgTextMeasurement* measurement) {
    RdtPath* path = rdt_path_new();
    RdtPath* ink = measurement ? rdt_path_new() : nullptr;
    if (measurement) *measurement = {};
    if (measurement) measurement->valid = path && ink && (collected || layout->run_count == 0);
    if (collected) {
        RdtMatrix identity = rdt_matrix_identity();
        svg_text_measure(layout, &identity, layout->allow_embedded_font);
        svg_text_place(layout);
        for (int i = 0; i < layout->run_count; i++) {
            SvgTextRun* run = &layout->runs[i];
            if (target_source) {
                int ancestor = run->style;
                while (ancestor >= 0 && layout->styles[ancestor].element != target_source) ancestor = layout->styles[ancestor].parent;
                if (ancestor < 0) continue;
            }
            if (!run->font) { if (measurement) measurement->valid = false; continue; }
            svg_text_append_cell(layout, run, path);
            if (measurement) {
                measurement->advance += run->advance;
                if (!str_all(layout->chars->str + run->start, run->len, str_is_space)) {
                    RdtPath* glyphs = svg_text_run_path(layout, run);
                    if (glyphs) {
                        RdtMatrix matrix = svg_text_run_transform(run, &identity);
                        render_path_append_transformed(ink, glyphs, &matrix);
                        rdt_path_free(glyphs);
                    }
                }
            }
        }
        svg_text_release_fonts(layout);
    }
    if (measurement) {
        measurement->has_logical = rdt_path_get_bounds(path, &measurement->logical.left,
            &measurement->logical.top, &measurement->logical.right, &measurement->logical.bottom);
        measurement->has_ink = rdt_path_get_bounds(ink, &measurement->ink.left,
            &measurement->ink.top, &measurement->ink.right, &measurement->ink.bottom);
    }
    if (ink) rdt_path_free(ink);
    return path;
}

static RdtPath* svg_text_geometry_collect(SvgTextGeometryScope* scope, DomElement* target,
    DomElement* text, SvgTextMeasurement* measurement) {
    ScratchMark mark = scratch_mark(&scope->scratch);
    SvgInlineRenderContext ctx = scope->context;
    DomElement* ancestors[64]; int count = 0;
    for (DomNode* node = text->parent; node && count < 64; node = node->parent) {
        if (node->is_element()) ancestors[count++] = node->as_element();
    }
    for (int i = count - 1; i >= 0; i--) svg_apply_inherited_paint_attrs(&ctx, dom_element_to_element(ancestors[i]));
    SvgTextLayout layout = {};
    Element* text_source = dom_element_to_element(text);
    Element* target_source = dom_element_to_element(target);
    bool collected = svg_text_layout_collect(&ctx, text_source, &layout);
    RdtPath* path = svg_text_layout_geometry(&layout, target_source, collected, measurement);
    if (layout.chars) strbuf_free(layout.chars);
    scratch_restore(&scope->scratch, mark);
    return path;
}

RdtPath* svg_text_geometry_path(DomElement* target, const SvgLengthContext* lengths,
    FontContext* font_context) {
    if (!target || !target->doc || !lengths) return nullptr;
    UiContext* ui = (UiContext*)target->doc->js.host_ui_context;
    SvgPaintHostStyle* shared = font_context && ui && font_context == ui->font_ctx &&
        !svg_animation_is_sampling(target->doc) ? svg_host_style(target->doc) : nullptr;
    SvgStyleEntry* entry = shared ? svg_style_entry(&shared->style, dom_element_to_element(target)) : nullptr;
    uint64_t animation = entry ? svg_animation_source_generation(target->doc,
        dom_element_to_element(target), target) : 0;
    // logical text cells survive scrolling. The document snapshot owns them;
    // callers receive a clone and can free it independently of later mutations.
    if (entry && entry->text_hit_path && entry->text_hit_context == font_context && entry->text_hit_animation == animation &&
        entry->text_hit_fonts == font_context_resource_generation(font_context) &&
        entry->text_hit_lengths.viewport_width == lengths->viewport_width &&
        entry->text_hit_lengths.viewport_height == lengths->viewport_height &&
        entry->text_hit_lengths.font_size == lengths->font_size &&
        entry->text_hit_lengths.x_height == lengths->x_height) {
        RdtPath* cached = rdt_path_clone(entry->text_hit_path);
        if (cached) return cached;
    }
    DomElement* text = target;
    for (DomNode* node = target; node && node->is_element(); node = node->parent) {
        DomElement* element = node->as_element();
        if (element->tag_name && strcmp(element->tag_name, "text") == 0) { text = element; break; }
    }
    DomElement* svg = text;
    for (DomNode* node = text->parent; node && node->is_element(); node = node->parent) {
        if (node->as_element()->tag_name && strcmp(node->as_element()->tag_name, "svg") == 0) svg = node->as_element();
    }
    SvgTextGeometryScope scope = {};
    bool ready = svg_text_geometry_scope_open(&scope, svg, lengths, font_context);
    RdtPath* path = ready ? svg_text_geometry_collect(&scope, target, text, nullptr) : nullptr;
    svg_text_geometry_scope_close(&scope);
    if (entry && path) {
        if (entry->text_hit_path) rdt_path_free(entry->text_hit_path);
        entry->text_hit_path = rdt_path_clone(path);
        entry->text_hit_context = font_context;
        entry->text_hit_lengths = *lengths;
        entry->text_hit_animation = animation;
        entry->text_hit_fonts = font_context_resource_generation(font_context);
    }
    return path;
}

bool svg_text_measure_batch(DomElement* svg, const SvgLengthContext* lengths,
    SvgTextMeasurement* measurements, size_t count) {
    if (!svg || !svg->doc || !lengths || (!measurements && count)) return false;
    if (!count) return true;
    SvgTextGeometryScope scope = {};
    bool ready = svg_text_geometry_scope_open(&scope, svg, lengths, nullptr);
    size_t index = 0;
    // only direct text children are requests; style/defs remain available to font resolution.
    for (DomNode* node = svg->first_child; ready && node; node = node->next_sibling) {
        if (!node->is_element()) continue;
        DomElement* text = node->as_element();
        if (!text->tag_name || strcmp(text->tag_name, "text") != 0) continue;
        if (index >= count) { ready = false; break; }
        RdtPath* path = svg_text_geometry_collect(&scope, text, text, &measurements[index++]);
        ready = path && measurements[index - 1].valid;
        if (path) rdt_path_free(path);
    }
    svg_text_geometry_scope_close(&scope);
    return ready && index == count;
}

bool text_measure_batch(FontContext* fonts, const TextMeasureRequest* requests,
    SvgTextMeasurement* measurements, size_t count, TextMeasureFontFn on_font, void* font_context) {
    if (!fonts || (count && (!requests || !measurements))) return false;
    Arena* arena = mem_arena_create(nullptr, MEM_ROLE_RENDER, "render.text_measure");
    if (!arena) return false;
    ScratchArena scratch = {};
    mem_scratch_init(nullptr, &scratch, arena, MEM_ROLE_RENDER, "render.text_measure.scratch");
    SvgInlineRenderContext context = {};
    context.font_ctx = lam::up(fonts);
    context.resource_scratch = lam::up(&scratch);
    bool valid = true;
    for (size_t index = 0; valid && index < count; index++) {
        const TextMeasureRequest* request = &requests[index];
        const char* family = request->font.family;
        if (!request->text || !family || strlen(family) >= sizeof(SvgTextStyle::font_family) ||
            !isfinite(request->font.size_px) || request->font.size_px <= 0.0f) {
            valid = false;
            break;
        }
        ScratchMark mark = scratch_mark(&scratch);
        SvgTextStyle style = {};
        style.parent = style.path_owner = -1;
        style.text_length = -1.0f;
        style.preserve_space = true;
        str_copy(style.font_family, sizeof(style.font_family), family, strlen(family));
        style.font_size = request->font.size_px;
        style.font_weight = request->font.weight;
        style.font_slant = request->font.slant;
        style.letter_spacing = request->letter_spacing;
        style.word_spacing = request->word_spacing;
        SvgTextLayout layout = {};
        layout.ctx = &context;
        layout.on_font = on_font;
        layout.font_context = font_context;
        layout.request_index = index;
        layout.open_run = layout.trailing_space_run = -1;
        layout.chars = strbuf_new_cap(64);
        int style_index = -1;
        bool collected = layout.chars && svg_text_push_style(&layout, &style, &style_index);
        if (collected) {
            svg_text_append(&layout, style_index, request->text, request->length);
            strbuf_append_char(layout.chars, '\0');
            layout.styles[style_index].end_run = layout.run_count;
            layout.fonts = (SvgTextFont*)scratch_calloc(&scratch, sizeof(SvgTextFont));
            collected = layout.fonts != nullptr;
        }
        RdtPath* path = svg_text_layout_geometry(&layout, nullptr, collected, &measurements[index]);
        valid = collected && path && measurements[index].valid;
        if (path) rdt_path_free(path);
        if (layout.chars) strbuf_free(layout.chars);
        scratch_restore(&scratch, mark);
    }
    scratch_release(&scratch);
    mem_arena_destroy(arena);
    return valid;
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

static char* svg_href_file_part(const char* href, const char** fragment_out) {
    if (fragment_out) *fragment_out = nullptr;
    if (!href || !*href) return nullptr;
    const char* fragment = strchr(href, '#');
    const char* end = fragment ? fragment : href + strlen(href);
    if (fragment_out && fragment && fragment[1]) *fragment_out = fragment + 1;
    if (end == href) return mem_strdup("", MEM_CAT_RENDER);
    return mem_dup_n(href, (size_t)(end - href), MEM_CAT_RENDER);
}

static char* svg_resolve_resource_path(SvgInlineRenderContext* ctx, const char* href_no_fragment) {
    if (!href_no_fragment || !*href_no_fragment) return nullptr;
    if (strncmp(href_no_fragment, "data:", 5) == 0 || strstr(href_no_fragment, "://") || href_no_fragment[0] == '/') {
        return mem_strdup(href_no_fragment, MEM_CAT_RENDER);
    }
    UiContext* ui = g_svg_active_rdcon ? g_svg_active_rdcon->ui_context : nullptr;
    Url* owned_base = ctx && ctx->source_path && *ctx->source_path
        ? url_parse_path_or_url(ctx->source_path, nullptr) : nullptr;
    Url* base = owned_base ? owned_base : ui && ui->document ? ui->document->url : nullptr;
    Url* resolved = base ? url_parse_with_base(href_no_fragment, base) : nullptr;
    char* path = resolved && resolved->scheme == URL_SCHEME_FILE ? url_to_local_path(resolved)
        : resolved ? mem_strdup(url_get_href(resolved), MEM_CAT_RENDER)
                   : mem_strdup(href_no_fragment, MEM_CAT_RENDER);
    if (resolved) url_destroy(resolved);
    if (owned_base) url_destroy(owned_base);
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

static const char* svg_pdf_registered_image_resolver(Element* context, int object_num) {
    SvgImageResolverEntry* entry = svg_find_image_resolver_entry(context);
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
        return data_uri;
    }
    return href;
}

static void render_svg_image_resource(SvgInlineRenderContext* ctx, Element* elem, const char* href,
    Bound rectangle, const RdtMatrix* frame, bool auto_width, bool auto_height) {
    if (ctx->clip_geometry) return;
    if (!href || !*href) return;
    href = svg_resolve_pdf_image_href(ctx, href);
    // referenced SVG images follow secure static mode, including data URI documents.
    if (ctx->image_document && strncmp(href, "data:", 5) != 0) return;
    float x = rectangle.left, y = rectangle.top, width = rectangle.right - x, height = rectangle.bottom - y;
    RdtMatrix transform = *frame;
    lam::Temp<char> file(svg_href_file_part(href, nullptr));
    lam::Temp<char> resolved(svg_resolve_resource_path(ctx, file ? file.get() : href));
    file.reset();
    if (!resolved) return;
    SvgStyleContext* style = (SvgStyleContext*)ctx->style_context;
    if (style && style->document &&
        !input_resource_policy_admits(style->document->resource_policy, resolved.get())) return;
    ImageSurface* image = nullptr;
    RdtPicture* standalone_picture = nullptr;
    bool owns_image = false;
    UiContext* ui = g_svg_active_rdcon ? g_svg_active_rdcon->ui_context : nullptr;
    if (ui) {
        image = load_document_image(style ? style->document : nullptr, ui, resolved.get());
    } else if (strncmp(resolved.get(), "data:", 5) == 0) {
        size_t length = 0;
        lam::Temp<unsigned char> data(parse_data_uri(resolved.get(), nullptr, 0, &length));
        if (data) {
            if (image_content_is_svg(data.get(), length)) {
                standalone_picture = rdt_picture_load_data((const char*)data.get(),
                    (int)length, "svg"); // INT_CAST_OK: picture input byte-count API.
            } else {
                image = image_surface_decode_data(data.get(), length);
                owns_image = image != nullptr;
            }
        }
    } else if (svg_image_href_is_svg(resolved.get())) {
        if (!svg_resource_stack_contains(resolved.get())) standalone_picture = rdt_picture_load(resolved.get());
    } else {
        image = image_surface_decode_file(resolved.get());
        owns_image = image != nullptr;
    }
    resolved.reset();
    RdtPicture* picture = image && image->format == IMAGE_FORMAT_SVG ? image->pic : standalone_picture;
    float intrinsic_width = image ? (float)image->width : 0.0f;
    float intrinsic_height = image ? (float)image->height : 0.0f;
    if (picture) rdt_picture_get_size(picture, &intrinsic_width, &intrinsic_height);
    if (auto_width) width = intrinsic_width;
    if (auto_height) height = intrinsic_height;
    if (intrinsic_width > 0.0f && intrinsic_height > 0.0f && width > 0.0f && height > 0.0f) {
        const char* overflow = svg_style_property_value(ctx, elem, "overflow");
        bool image_clip = !overflow || strcmp(overflow, "visible") != 0;
        if (image_clip) {
            RdtPath* clip = rdt_path_new();
            rdt_path_add_rect(clip, x, y, width, height, 0.0f, 0.0f);
            svg_push_clip(ctx, clip, &transform);
            rdt_path_free(clip);
        }
        RdtMatrix translation = rdt_matrix_translate(x, y);
        RdtMatrix placement = rdt_matrix_multiply(&transform, &translation);
        const char* aspect = get_svg_attr(elem, "preserveAspectRatio");
        Element* picture_root = picture ? rdt_picture_get_svg_root(picture) : nullptr;
        if (picture_root && !get_svg_attr(picture_root, "viewBox") && !get_svg_attr(picture_root, "viewbox")) {
            // SVG images without a viewBox fill their image rectangle in browser image mode.
            aspect = "none";
        }
        SvgViewBox source = {0.0f, 0.0f, intrinsic_width, intrinsic_height, true};
        RdtMatrix fit = svg_viewbox_transform(&source, width, height, aspect);
        placement = rdt_matrix_multiply(&placement, &fit);
        if (picture) {
            RdtPicture* sampled = ctx->image_document ? rdt_picture_dup(picture) : nullptr;
            SvgStyleContext* style = (SvgStyleContext*)ctx->style_context;
            if (sampled && style && style->document)
                rdt_picture_set_animation_time(sampled, svg_animation_current_time(style->document->root));
            render_svg_record_picture(ctx->paint_list, ctx->dl, ctx->resource_scratch,
                ctx->font_ctx, g_svg_active_rdcon, sampled ? sampled : picture, clamp_byte_round(ctx->opacity * 255.0f), &placement);
            rdt_picture_free(sampled);
        } else if (image) {
            float decode_width = intrinsic_width * hypotf(placement.e11, placement.e21);
            float decode_height = intrinsic_height * hypotf(placement.e12, placement.e22);
            image_surface_ensure_decoded(image,
                (int)ceilf(decode_width), (int)ceilf(decode_height)); // INT_CAST_OK: decoder pixel-count API.
            PaintRecordTarget target = svg_record_target(ctx);
            paint_record_draw_image_resource(&target, "svg_raster_image", image, 0.0f, 0.0f,
                intrinsic_width, intrinsic_height, clamp_byte_round(ctx->opacity * 255.0f), &placement,
                owns_image ? image_surface_destroy : nullptr);
            owns_image = false;
        }
        if (image_clip) svg_pop_clip(ctx);
    }
    if (owns_image) image_surface_destroy(image);
    if (standalone_picture) rdt_picture_free(standalone_picture);
}

static void render_svg_image(SvgInlineRenderContext* ctx, Element* elem) {
    const char* href = get_svg_href(ctx, elem);
    SvgLengthContext lengths = svg_length_context(ctx, elem);
    float x = svg_resolve_length(get_svg_attr(elem, "x"), &lengths, SVG_LENGTH_X, 0.0f);
    float y = svg_resolve_length(get_svg_attr(elem, "y"), &lengths, SVG_LENGTH_Y, 0.0f);
    float width = svg_resolve_length(get_svg_attr(elem, "width"), &lengths, SVG_LENGTH_X, 0.0f);
    float height = svg_resolve_length(get_svg_attr(elem, "height"), &lengths, SVG_LENGTH_Y, 0.0f);
    RdtMatrix frame = compose_element_transform(ctx, elem);
    render_svg_image_resource(ctx, elem, href, {x, y, x + width, y + height}, &frame,
        !get_svg_attr(elem, "width"), !get_svg_attr(elem, "height"));
}

// ============================================================================
// SVG ClipPath Support
// ============================================================================

// ============================================================================
// SVG Group and Children
// ============================================================================

// Inherited state a container element (<g>, or a <use> instance) scopes for
// its content, plus its group-opacity layer.
static void svg_group_enter(SvgInlineRenderContext* ctx, Element* elem, SvgGroupScope* scope) {
    // save current inherited state
    memcpy(scope->inherited_properties, ctx->inherited_properties, sizeof(scope->inherited_properties));
    scope->fill_paint = ctx->fill_paint; scope->stroke_paint = ctx->stroke_paint;
    scope->fill_color = ctx->fill_color;
    scope->stroke_color = ctx->stroke_color;
    scope->current_color = ctx->current_color;
    scope->stroke_width = ctx->stroke_width;
    scope->opacity = ctx->opacity;
    scope->fill_opacity = ctx->fill_opacity; scope->stroke_opacity = ctx->stroke_opacity;
    scope->fill_none = ctx->fill_none;
    scope->stroke_none = ctx->stroke_none;
    scope->transform = ctx->transform;
    scope->font_family = ctx->inherited_font_family;
    scope->font_size = ctx->inherited_font_size;
    scope->font_weight = ctx->inherited_font_weight;
    scope->text_anchor = ctx->inherited_text_anchor;
    scope->font_style = ctx->inherited_font_style;

    // element effects are composited once by the common rendering boundary.
    // lengths in the transform use this group's computed font exactly once.
    ctx->transform = compose_element_transform(ctx, elem);
    svg_apply_inherited_paint_attrs(ctx, elem);
}

static void svg_group_leave(SvgInlineRenderContext* ctx, const SvgGroupScope* scope) {
    // restore inherited state
    memcpy(ctx->inherited_properties, scope->inherited_properties, sizeof(ctx->inherited_properties));
    ctx->fill_paint = scope->fill_paint; ctx->stroke_paint = scope->stroke_paint;
    ctx->fill_color = scope->fill_color;
    ctx->stroke_color = scope->stroke_color;
    ctx->current_color = scope->current_color;
    ctx->stroke_width = scope->stroke_width;
    ctx->opacity = scope->opacity;
    ctx->fill_opacity = scope->fill_opacity; ctx->stroke_opacity = scope->stroke_opacity;
    ctx->fill_none = scope->fill_none;
    ctx->stroke_none = scope->stroke_none;
    ctx->transform = scope->transform;
    ctx->inherited_font_family = lam::up(scope->font_family);
    ctx->inherited_font_size = scope->font_size;
    ctx->inherited_font_weight = scope->font_weight;
    ctx->inherited_text_anchor = lam::up(scope->text_anchor);
    ctx->inherited_font_style = lam::up(scope->font_style);
}

static void render_svg_group(SvgInlineRenderContext* ctx, Element* elem,
    void (*draw_children)(SvgInlineRenderContext*, Element*)) {
    if (!elem) return;
    SvgGroupScope scope;
    svg_group_enter(ctx, elem, &scope);
    draw_children(ctx, elem);
    svg_group_leave(ctx, &scope);
}

static void render_svg_children(SvgInlineRenderContext* ctx, Element* elem) {
    if (!elem || elem->length == 0) return;

    for (int64_t i = 0; i < elem->length; i++) {
        Element* child = get_child_element_at(elem, i);
        if (!child) continue;
        render_svg_element(ctx, child);
    }
}

static bool svg_element_is_eligible(SvgInlineRenderContext* ctx, Element* elem) {
    SvgStyleEntry* entry = svg_style_entry((SvgStyleContext*)ctx->style_context, elem);
    if (entry) return dom_svg_element_is_eligible(entry->node);
    if (elem->has_attr("requiredExtensions")) return false;
    const char* languages = get_svg_attr(elem, "systemLanguage");
    if (!languages && elem->has_attr("systemLanguage")) languages = "";
    return dom_svg_conditions_match(nullptr, languages, dom_document_preferred_languages(nullptr));
}

static void svg_draw_switch_child(SvgInlineRenderContext* ctx, Element* elem) {
    for (int64_t index = 0; index < elem->length; index++) {
        if (get_type_id(elem->items[index]) != LMD_TYPE_ELEMENT) continue;
        Element* child = elem->items[index].element;
        if (svg_element_is_eligible(ctx, child)) { render_svg_element(ctx, child); break; }
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
        const char* child_tag = get_element_tag_name(ctx, child);
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
    process_svg_def_resources(ctx, svg_element);
}

static void render_svg_use_target(SvgInlineRenderContext* ctx, Element* use_elem, Element* ref, const char* href,
                                   const SvgInlineRenderContext* reference_context = nullptr, const Bound* image_viewport = nullptr) {
    if (!ctx || !use_elem || !ref) return;
    // SVG 2 §5.6: a reference to the <use> itself, to one of its ancestors, or
    // to an element already being instantiated is circular; it renders nothing.
    if (svg_element_tree_contains(ref, use_elem)) {
        log_debug("[SVG] <use> href='%s' references its own ancestor", href ? href : "");
        return;
    }
    for (int i = 0; i < ctx->use_depth; i++) {
        if (ctx->use_chain[i] == ref) {
            log_debug("[SVG] <use> href='%s' is a circular reference", href ? href : "");
            return;
        }
    }
    if (ctx->use_depth >= SVG_USE_DEPTH_MAX) {
        log_debug("[SVG] <use> href='%s' exceeds nesting depth %d", href ? href : "", SVG_USE_DEPTH_MAX);
        return;
    }
    ctx->use_chain[ctx->use_depth++] = lam::up(ref);

    // The instance renders like a <g> carrying the <use>'s transform and
    // presentation attributes, then translate(x, y); the referenced content
    // inherits from the <use>, not from its own ancestors (SVG 2 §5.6.3).
    SvgGroupScope scope;
    svg_group_enter(ctx, use_elem, &scope);
    SvgInlineRenderContext source_context = *ctx;
    SvgContextPaint context = {};
    context.fill = ctx->fill_paint; context.stroke = ctx->stroke_paint;
    context.current_color = ctx->current_color; context.transform = ctx->transform;
    context.source_context = lam::up(&source_context);
    SvgStyleEntry* use_entry = svg_style_entry((SvgStyleContext*)ctx->style_context, use_elem);
    bool has_bounds = use_entry && dom_svg_element_geometry_bounds(use_entry->node, &context.geometry_box.left,
        &context.geometry_box.top, &context.geometry_box.right, &context.geometry_box.bottom);
    const SvgContextPaint* saved_context_paint = ctx->context_paint;
    ctx->context_paint = lam::up(&context);
    SvgLengthContext lengths = svg_length_context(ctx);
    float ux = image_viewport ? image_viewport->left : get_svg_number_attr(use_elem, "x", 0.0f, &lengths, SVG_LENGTH_X);
    float uy = image_viewport ? image_viewport->top : get_svg_number_attr(use_elem, "y", 0.0f, &lengths, SVG_LENGTH_Y);
    if (ux != 0.0f || uy != 0.0f) {
        RdtMatrix translate = rdt_matrix_translate(ux, uy);
        ctx->transform = rdt_matrix_multiply(&ctx->transform, &translate);
    }

    SvgInlineRenderContext instance = *ctx;
    if (reference_context) {
        instance.style_context = reference_context->style_context; instance.source_path = reference_context->source_path;
        instance.id_scope = reference_context->id_scope; instance.svg_root = reference_context->svg_root;
        instance.font_ctx = reference_context->font_ctx; instance.defs = nullptr;
    }
    SvgStyleContext* instance_style = (SvgStyleContext*)instance.style_context;
    SvgStyleEntry* instance_entry = svg_style_entry(instance_style, ref);
    if (reference_context && use_entry && instance_style && instance_style->document)
        svg_animation_prepare_instance(use_entry->node, instance_style->document->root);
    // SVG 2 §5.6.5: external use instances sample the host timeline; resource documents stay static.
    SvgAnimationSourceScope instance_sources(instance_style ? instance_style->document : nullptr,
        instance_entry ? instance_entry->node : nullptr, use_entry ? use_entry->node : nullptr);
    if (!has_bounds) {
        SvgStyleEntry* ref_entry = svg_style_entry((SvgStyleContext*)instance.style_context, ref);
        if (ref_entry && dom_svg_element_geometry_bounds(ref_entry->node, &context.geometry_box.left,
            &context.geometry_box.top, &context.geometry_box.right, &context.geometry_box.bottom)) {
            SvgInlineRenderContext local = instance; local.transform = rdt_matrix_identity();
            RdtMatrix reference_transform = compose_element_transform(&local, ref);
            rdt_matrix_transform_rect_bounds(&reference_transform, context.geometry_box.left, context.geometry_box.top,
                context.geometry_box.right, context.geometry_box.bottom, &context.geometry_box.left, &context.geometry_box.top,
                &context.geometry_box.right, &context.geometry_box.bottom);
            context.geometry_box.left += ux; context.geometry_box.right += ux;
            context.geometry_box.top += uy; context.geometry_box.bottom += uy;
        }
    }
    const char* ref_tag = get_element_tag_name(&instance, ref);
    if (ref_tag && strcmp(ref_tag, "symbol") == 0) {
        float sym_w = image_viewport ? image_viewport->right - image_viewport->left : get_svg_number_attr(use_elem, "width", lengths.viewport_width, &lengths, SVG_LENGTH_X);
        float sym_h = image_viewport ? image_viewport->bottom - image_viewport->top : get_svg_number_attr(use_elem, "height", lengths.viewport_height, &lengths, SVG_LENGTH_Y);
        render_svg_viewport(&instance, ref, sym_w, sym_h, 0.0f, 0.0f);
    } else {
        render_svg_element(&instance, ref);
    }

    ctx->context_paint = lam::up(saved_context_paint);
    svg_group_leave(ctx, &scope);
    ctx->use_depth--;
}

static bool render_svg_external_use(SvgInlineRenderContext* ctx, Element* use_elem, const char* href) {
    if (!ctx || !use_elem || !href || ctx->image_document) return false;
    SvgResourceReference reference = svg_resolve_reference(ctx, href);
    if (!reference.element || !reference.document) return false;
    SvgStyleEntry* entry = svg_style_entry((SvgStyleContext*)ctx->style_context, use_elem);
    const char* fragment = strrchr(href, '#');
    if (entry && fragment) {
        reference = svg_retain_use_reference(ctx, entry->node, &reference, fragment + 1);
        if (!reference.element || !reference.document) return false;
    }
    SvgInlineRenderContext source = svg_reference_render_context(ctx, &reference);
    render_svg_use_target(ctx, use_elem, reference.element, href, &source);
    return true;
}

// ============================================================================
// Main SVG Element Dispatcher
// ============================================================================

static void render_svg_viewport(SvgInlineRenderContext* ctx, Element* elem,
    float width, float height, float x, float y) {
    if (width <= 0.0f || height <= 0.0f) return;
    PaintRecordTarget prior = svg_record_target(ctx);
    paint_record_lower_pending(&prior);
    SvgInlineRenderContext content = *ctx;
    content.backdrop_start = dl_item_count(ctx->dl);
    SvgGroupScope scope = {};
    svg_group_enter(&content, elem, &scope);
    RdtMatrix offset = rdt_matrix_translate(x, y);
    content.transform = rdt_matrix_multiply(&content.transform, &offset);
    const char* overflow = svg_style_property_value(&content, elem, "overflow");
    bool clipped = !overflow || strcmp(overflow, "visible") != 0;
    if (clipped) {
        RdtPath* path = rdt_path_new();
        if (!path) return;
        rdt_path_add_rect(path, 0.0f, 0.0f, width, height, 0.0f, 0.0f);
        svg_push_clip(&content, path, &content.transform); rdt_path_free(path);
    }
    SvgViewBox box = svg_parse_viewbox(get_svg_attr(elem, "viewBox"));
    content.viewbox_x = box.has_viewbox ? box.min_x : 0.0f;
    content.viewbox_y = box.has_viewbox ? box.min_y : 0.0f;
    content.current_viewport_w = box.has_viewbox ? box.width : width;
    content.current_viewport_h = box.has_viewbox ? box.height : height;
    if (box.has_viewbox) {
        RdtMatrix mapping = svg_viewbox_transform(&box, width, height, get_svg_attr(elem, "preserveAspectRatio"));
        content.transform = rdt_matrix_multiply(&content.transform, &mapping);
    }
    // effects use the child user space established by the viewport, including nonzero viewBox origins.
    if (!svg_render_effect_boundary(&content, elem, svg_draw_children, nullptr, true)) render_svg_children(&content, elem);
    if (clipped) svg_pop_clip(&content);
}

struct SvgForeignObjectPaint {
    ViewBlock* block;
    Bound clip;
    float scale;
    RasterRenderContext* context;
};

struct SvgHtmlScratchScope {
    ScratchArena* scratch;
    ~SvgHtmlScratchScope() { if (scratch) scratch_release(scratch); }
};

static UiContext* svg_style_layout_html(SvgInlineRenderContext* ctx, SvgStyleContext* style) {
    if (!style || !style->isolated_document) return nullptr;
    if (style->isolated_ui) return style->isolated_ui;
    lam::Temp<UiContext> owned_ui((UiContext*)mem_calloc(1, sizeof(UiContext), MEM_CAT_RENDER)); // OBJ_HEAP_OK: the SVG style context owns the isolated layout's headless UI shell; svg_style_destroy releases it
    lam::Own<ViewTree> tree = view_tree_shell_create(style->isolated_document);
    if (!owned_ui || !tree) { lam::free_owned(tree); return nullptr; }
    UiContext* ui = owned_ui.get();
    style->isolated_ui = lam::own(owned_ui.release());
    DomDocument* doc = style->isolated_document;
    ui->document = lam::up(doc); ui->font_ctx = ctx->font_ctx; ui->headless = true;
    ui->viewport_width = ctx->current_viewport_w; ui->viewport_height = ctx->current_viewport_h;
    ui->device_scale = ui->device_scale_x = ui->device_scale_y = ctx->raster_scale;
    ui_context_init_default_fonts(ui);
    doc->view_tree = tree; tree->init((MemContext*)doc->services.mem_ctx);
    radiant_apply_css_stylesheets_to_tree(doc, doc->root, doc->stylesheets,
        doc->stylesheet_count, doc->document_pool, style->engine, style->matcher);
    // isolated layout never runs document scripts or publishes host observer callbacks.
    LayoutContext layout;
    LayoutPassScope pass(&layout, doc, ui);
    layout_html_root(&layout, doc->root);
    return ui;
}

static void svg_draw_foreign_object_html(SvgInlineRenderContext* ctx, Element*, void* data) {
    SvgForeignObjectPaint* content = (SvgForeignObjectPaint*)data;
    RasterRenderContext html = *content->context;
    html.transform = rdt_matrix_identity(); html.has_transform = false;
    html.has_transform_3d = false;
    html.block = {}; html.block.clip = content->clip;
    html.clip_shape_depth = 0; html.has_dirty_union = false; html.dirty_tracker = nullptr;
    // foreignObject records in local coordinates; outer projected clip bounds
    // belong to the host surface and cannot constrain this capture.
    html.vector_clip_depth = 0;
    html.retained_dl_cache = nullptr; html.element_marker_suppression_depth++;
    html.dl = ctx->dl; html.paint_list = ctx->paint_list; html.raster_scale = content->scale;
    ScratchMark mark = scratch_mark(&html.scratch);
    RasterRenderContext* previous = g_svg_active_rdcon; g_svg_active_rdcon = &html;
    if (content->block->font && html.ui_context) setup_font(html.ui_context, &html.font, content->block->font);
    render_block_paint_children(&html, content->block);
    g_svg_active_rdcon = previous;
    scratch_restore(&html.scratch, mark);
}

static void render_svg_foreign_object(SvgInlineRenderContext* ctx, Element* elem) {
    if (ctx->clip_geometry) return;
    SvgStyleContext* style = (SvgStyleContext*)ctx->style_context;
    UiContext* isolated_ui = svg_style_layout_html(ctx, style);
    RasterRenderContext isolated = g_svg_active_rdcon ? *g_svg_active_rdcon : RasterRenderContext{};
    RasterRenderContext* html_context = g_svg_active_rdcon;
    SvgHtmlScratchScope scratch_scope = {};
    if (isolated_ui) {
        isolated.ui_context = lam::up(isolated_ui);
        isolated.content_bounds_cache = nullptr; isolated.retained_dl_cache = nullptr;
        isolated.raster_scale = ctx->raster_scale;
        if (g_svg_active_rdcon && g_svg_active_rdcon->ui_context)
            isolated_ui->surface = g_svg_active_rdcon->ui_context->surface;
        else {
            if (!style->isolated_surface) style->isolated_surface = svg_create_paint_surface(
                ctx->viewport_width * ctx->raster_scale, ctx->viewport_height * ctx->raster_scale);
            isolated_ui->surface = lam::up(style->isolated_surface);
            if (!isolated_ui->surface) return;
            isolated.block.clip = {0, 0, (float)isolated_ui->surface->width, (float)isolated_ui->surface->height};
        }
        isolated.scratch = {};
        mem_scratch_init((MemContext*)style->document->services.mem_ctx, &isolated.scratch,
            style->document->view_tree->scratch_arena, MEM_ROLE_RENDER, "render.svg.html.scratch");
        scratch_scope.scratch = &isolated.scratch; html_context = &isolated;
    }
    if (!html_context) return;
    SvgStyleEntry* entry = svg_style_entry(style, elem);
    ViewBlock* block = entry ? lam::view_as_block(entry->node) : nullptr;
    float x, y, width, height;
    if (!block || !dom_svg_foreign_object_rectangle(entry->node, &x, &y, &width, &height) ||
        width <= 0.0f || height <= 0.0f) return;
    RdtMatrix frame = compose_element_transform(ctx, elem), offset = rdt_matrix_translate(x, y);
    frame = rdt_matrix_multiply(&frame, &offset);
    float scale = html_context->raster_scale > 0.0f ? html_context->raster_scale : 1.0f;
    RdtMatrix to_user = rdt_matrix_scale(1.0f / scale, 1.0f / scale);
    RdtMatrix placement = rdt_matrix_multiply(&frame, &to_user), inverse;
    if (!rdt_matrix_invert_affine(&placement, &inverse)) return;
    Bound clip = html_context->block.clip, local_clip;
    rdt_matrix_transform_rect_bounds(&inverse, clip.left, clip.top, clip.right, clip.bottom,
        &local_clip.left, &local_clip.top, &local_clip.right, &local_clip.bottom);
    bool clipped = dom_svg_foreign_object_clips(entry->node);
    if (clipped) local_clip = view_geometry_intersect_bound_rect(local_clip, {0, 0, width * scale, height * scale});
    if (local_clip.right <= local_clip.left || local_clip.bottom <= local_clip.top) return;
    // CSS clips and opacity groups are recorded in their containing space before the SVG CTM applies.
    SvgForeignObjectPaint content = {block, local_clip, scale, html_context};
    SvgInlineRenderContext source = *ctx; source.raster_scale = scale;
    ImageSurface* surface = nullptr; Bound capture = {};
    bool captured = svg_rasterize_traversal(&source, elem, svg_draw_foreign_object_html, &content, &surface, &capture, 0, true);
    if (!captured || !surface) {
        if (surface) image_surface_destroy(surface);
        return;
    }
    RdtPath* path = clipped ? rdt_path_new() : nullptr;
    if (clipped && !path) { image_surface_destroy(surface); return; }
    if (path) {
        rdt_path_add_rect(path, 0.0f, 0.0f, width, height, 0.0f, 0.0f);
        svg_push_clip(ctx, path, &frame); rdt_path_free(path);
    }
    PaintRecordTarget target = svg_record_target(ctx);
    paint_record_draw_image_resource(&target, "svg_foreign_object_html", surface, capture.left, capture.top,
        (float)surface->width, (float)surface->height, 255, &placement, image_surface_destroy);
    if (clipped) svg_pop_clip(ctx);
}

static void render_svg_element_content(SvgInlineRenderContext* ctx, Element* elem) {
    if (!elem) return;

    const char* tag = get_element_tag_name(ctx, elem);
    if (!tag) return;

    char display_buf[64];
    const char* display = get_svg_attr_or_style(ctx, elem, "display", display_buf, sizeof(display_buf));
    if (display && strcmp(display, "none") == 0) {
        return;
    }



    if (strcmp(tag, "rect") == 0 || strcmp(tag, "circle") == 0 ||
        strcmp(tag, "ellipse") == 0 || strcmp(tag, "line") == 0 ||
        strcmp(tag, "polyline") == 0 || strcmp(tag, "polygon") == 0) {
        render_svg_basic_shape(ctx, elem);
    } else if (strcmp(tag, "path") == 0) {
        render_svg_path(ctx, elem);
    } else if (strcmp(tag, "g") == 0 || strcmp(tag, "a") == 0) {
        render_svg_group(ctx, elem);
    } else if (strcmp(tag, "switch") == 0) {
        render_svg_group(ctx, elem, svg_draw_switch_child);
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
        const char* href = get_svg_href(ctx, elem);
        bool resolved = false;
        if (href && href[0] == '#') {
            // SVG 2 §5.6: the fragment names any element of the document, as
            // getElementById would; the <defs> resource table holds only the
            // resources that paint servers and clip/mask references need.
            Element* ref = href[1]
                ? svg_find_element_id(ctx, href + 1) : nullptr;
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
    } else if (strcmp(tag, "foreignObject") == 0) {
        render_svg_foreign_object(ctx, elem);
    } else if (strcmp(tag, "svg") == 0) {
        SvgLengthContext lengths = svg_length_context(ctx, elem);
        render_svg_viewport(ctx, elem,
            get_svg_number_attr(elem, "width", lengths.viewport_width, &lengths, SVG_LENGTH_X),
            get_svg_number_attr(elem, "height", lengths.viewport_height, &lengths, SVG_LENGTH_Y),
            get_svg_number_attr(elem, "x", 0.0f, &lengths, SVG_LENGTH_X),
            get_svg_number_attr(elem, "y", 0.0f, &lengths, SVG_LENGTH_Y));
    } else {
        // unknown element - try rendering children
        render_svg_children(ctx, elem);
    }

}

static void svg_draw_element_content(SvgInlineRenderContext* ctx, Element* elem, void*) {
    render_svg_element_content(ctx, elem);
}

static bool svg_effect_geometry_box(SvgInlineRenderContext* ctx, Element* elem, Bound* box) {
    SvgLengthContext lengths = svg_length_context(ctx, elem);
    const char* tag = get_element_tag_name(ctx, elem);
    RdtPath* path = tag && strcmp(tag, "path") == 0 ? svg_parse_path_d(get_svg_attr(elem, "d")) : rdt_path_new();
    bool valid = path && tag && (strcmp(tag, "path") == 0 || svg_append_basic_shape_path(svg_style_node((SvgStyleContext*)ctx->style_context, elem), path, &lengths));
    if (valid) valid = rdt_path_get_bounds(path, &box->left, &box->top, &box->right, &box->bottom);
    if (path) rdt_path_free(path);
    if (valid) return true;
    SvgStyleEntry* entry = svg_style_entry((SvgStyleContext*)ctx->style_context, elem);
    return entry && dom_svg_element_geometry_bounds(entry->node, &box->left, &box->top, &box->right, &box->bottom);
}

static SvgResourceReference svg_effect_reference(SvgInlineRenderContext* ctx, Element* elem, const char* name) {
    const char* value = svg_style_property_value(ctx, elem, name);
    SvgPaint paint = value ? svg_parse_paint(ctx, value, elem) : SvgPaint{};
    SvgResourceReference reference = paint.kind == SVG_PAINT_RESOURCE
        ? svg_resolve_reference(ctx, paint.reference) : SvgResourceReference{};
    // a circular clip URL is invalid and applies no clipping; invalid masks remain transparent black.
    if (strcmp(name, "clip-path") == 0)
        for (const SvgPaintResourceScope* scope = (const SvgPaintResourceScope*)ctx->paint_resource_scope;
            scope; scope = scope->parent) if (scope->element == reference.element) return SvgResourceReference{};
    return reference;
}

static bool svg_effect_resource_is(SvgInlineRenderContext* ctx, const SvgResourceReference* resource, const char* tag) {
    SvgInlineRenderContext document = svg_reference_render_context(ctx, resource);
    const char* actual = resource->element ? get_element_tag_name(&document, resource->element) : nullptr;
    return actual && strcmp(actual, tag) == 0;
}

static bool svg_is_clip_shape_tag(const char* tag) {
    return tag && (strcmp(tag, "path") == 0 || strcmp(tag, "text") == 0 ||
        strcmp(tag, "rect") == 0 || strcmp(tag, "circle") == 0 || strcmp(tag, "ellipse") == 0 ||
        strcmp(tag, "polygon") == 0 || strcmp(tag, "polyline") == 0 || strcmp(tag, "line") == 0);
}

static bool svg_effect_clip_is_valid(SvgInlineRenderContext* ctx, const SvgResourceReference* resource) {
    if (!svg_effect_resource_is(ctx, resource, "clipPath")) return false;
    SvgInlineRenderContext document = svg_reference_render_context(ctx, resource);
    for (int64_t index = 0; index < resource->element->length; index++) {
        Element* child = get_child_element_at(resource->element, index);
        const char* tag = child ? get_element_tag_name(&document, child) : nullptr;
        if (!tag || strcmp(tag, "use") != 0) continue;
        const char* href = get_svg_href(&document, child);
        SvgResourceReference target = href ? svg_resolve_reference(&document, href) : SvgResourceReference{};
        // CSS Masking 1 section 6.1 permits only direct shape/text references in a clipPath.
        SvgInlineRenderContext target_document = svg_reference_render_context(&document, &target);
        if (!svg_is_clip_shape_tag(target.element ? get_element_tag_name(&target_document, target.element) : nullptr)) return false;
    }
    return true;
}

static void svg_draw_clip_children(SvgInlineRenderContext* ctx, Element* elem, void* data) {
    if (svg_render_effect_boundary(ctx, elem, svg_draw_clip_children, data)) return;
    RdtMatrix saved = ctx->transform;
    ctx->transform = compose_element_transform(ctx, elem);
    for (int64_t index = 0; index < elem->length; index++) {
        Element* child = get_child_element_at(elem, index);
        const char* tag = child ? get_element_tag_name(ctx, child) : nullptr;
        if (svg_is_clip_shape_tag(tag) || (tag && strcmp(tag, "use") == 0))
            render_svg_element(ctx, child);
    }
    ctx->transform = saved;
}

struct SvgMaskRegion {
    RdtMatrix transform;
    float x, y, width, height;
};

static void svg_draw_mask_children(SvgInlineRenderContext* ctx, Element* elem, void* data) {
    if (svg_render_effect_boundary(ctx, elem, svg_draw_mask_children, data)) return;
    SvgMaskRegion* region = (SvgMaskRegion*)data;
    if (!region || region->width <= 0.0f || region->height <= 0.0f) return;
    RdtPath* path = rdt_path_new();
    if (!path) return;
    rdt_path_add_rect(path, region->x, region->y, region->width, region->height, 0, 0);
    svg_push_clip(ctx, path, &region->transform); rdt_path_free(path);
    RdtMatrix saved = ctx->transform;
    ctx->transform = compose_element_transform(ctx, elem);
    render_svg_children(ctx, elem);
    ctx->transform = saved;
    svg_pop_clip(ctx);
}

static bool svg_prepare_effect_resource(SvgInlineRenderContext* ctx, const SvgResourceReference* reference,
    const Bound* geometry, const RdtMatrix* target_frame, bool mask,
    SvgInlineRenderContext* prepared, SvgPaintResourceScope* resource_scope) {
    Element* resource = reference->element;
    const SvgPaintResourceScope* parent = (const SvgPaintResourceScope*)ctx->paint_resource_scope;
    size_t depth = 0;
    for (const SvgPaintResourceScope* scope = parent; scope; scope = scope->parent)
        if (scope->element == resource || ++depth >= SVG_USE_DEPTH_MAX) return false;
    SvgInlineRenderContext document = svg_reference_render_context(ctx, reference);
    SvgInlineRenderContext style = svg_resource_style_context(&document, resource);
    *resource_scope = {resource, parent}; style.paint_resource_scope = lam::up(resource_scope);
    style.clip_geometry = !mask; style.effect_source = nullptr; style.opacity = 1.0f;
    float width = geometry->right - geometry->left, height = geometry->bottom - geometry->top;
    RdtMatrix basis = {width, 0, geometry->left, 0, height, geometry->top, 0, 0, 1};
    const char* units = get_svg_attr(resource, mask ? "maskContentUnits" : "clipPathUnits");
    bool content_bbox = units && strcmp(units, "objectBoundingBox") == 0;
    if (content_bbox && (width <= 0.0f || height <= 0.0f)) return false;
    style.transform = content_bbox ? rdt_matrix_multiply(target_frame, &basis) : *target_frame;
    if (content_bbox) style.current_viewport_w = style.current_viewport_h = 1.0f;
    *prepared = style;
    return true;
}


static bool svg_effect_clip_contains(SvgInlineRenderContext* ctx, const SvgResourceReference* reference,
    const Bound* geometry, const RdtMatrix* frame) {
    SvgInlineRenderContext style = {}; SvgPaintResourceScope scope = {};
    if (!svg_prepare_effect_resource(ctx, reference, geometry, frame, false, &style, &scope)) return false;
    SvgClipHitQuery query = *ctx->clip_hit_query; query.hit = false;
    style.clip_hit_query = lam::up(&query);
    svg_draw_clip_children(&style, reference->element, nullptr);
    return query.hit;
}

static bool svg_effect_coverage(SvgInlineRenderContext* ctx, const SvgResourceReference* reference,
    const Bound* geometry, const RdtMatrix* target_frame, const Bound* capture,
    bool mask, ImageSurface** coverage, bool* luminance, bool* linear) {
    SvgInlineRenderContext style = {}; SvgPaintResourceScope scope = {};
    if (!svg_prepare_effect_resource(ctx, reference, geometry, target_frame, mask, &style, &scope)) return false;
    style.clip_hit_query = nullptr;
    Element* resource = reference->element;
    float width = geometry->right - geometry->left, height = geometry->bottom - geometry->top;
    RdtMatrix basis = {width, 0, geometry->left, 0, height, geometry->top, 0, 0, 1};
    *coverage = svg_create_paint_surface(capture->right - capture->left, capture->bottom - capture->top);
    if (!*coverage) return false;
    Bound bounds = *capture;
    if (!mask) return svg_rasterize_traversal(&style, resource, svg_draw_clip_children, nullptr, coverage, &bounds);
    const char* region_units = get_svg_attr(resource, "maskUnits");
    bool region_bbox = !region_units || strcmp(region_units, "userSpaceOnUse") != 0;
    if (region_bbox && (width <= 0.0f || height <= 0.0f)) return false;
    SvgLengthContext lengths = svg_length_context(&style);
    if (region_bbox) lengths.viewport_width = lengths.viewport_height = 1.0f;
    else { lengths.viewport_width = ctx->current_viewport_w; lengths.viewport_height = ctx->current_viewport_h; }
    SvgMaskRegion region = {};
    region.transform = region_bbox ? rdt_matrix_multiply(target_frame, &basis) : *target_frame;
    region.x = get_svg_number_attr(resource, "x", -.1f * lengths.viewport_width, &lengths, SVG_LENGTH_X);
    region.y = get_svg_number_attr(resource, "y", -.1f * lengths.viewport_height, &lengths, SVG_LENGTH_Y);
    region.width = get_svg_number_attr(resource, "width", 1.2f * lengths.viewport_width, &lengths, SVG_LENGTH_X);
    region.height = get_svg_number_attr(resource, "height", 1.2f * lengths.viewport_height, &lengths, SVG_LENGTH_Y);
    const char* type = svg_style_property_value(&style, resource, "mask-type");
    *luminance = !type || strcmp(type, "alpha") != 0;
    const char* interpolation = svg_style_ancestor_property_value(&style, resource, "color-interpolation");
    *linear = interpolation && str_ieq_cstr(interpolation, "linearRGB");
    return svg_rasterize_traversal(&style, resource, svg_draw_mask_children, &region, coverage, &bounds);
}

static uint8_t svg_mask_pixel_coverage(uint32_t pixel, bool luminance, bool linear) {
    uint8_t alpha = (uint8_t)(pixel >> 24);
    if (!luminance || !alpha) return alpha;
    float r = (float)(pixel & 255u), g = (float)((pixel >> 8) & 255u), b = (float)((pixel >> 16) & 255u);
    if (linear) {
        r = render_color_srgb_to_linear(r / (float)alpha) * (float)alpha;
        g = render_color_srgb_to_linear(g / (float)alpha) * (float)alpha;
        b = render_color_srgb_to_linear(b / (float)alpha) * (float)alpha;
    }
    // premultiplied luminance already includes alpha; black over white naturally cuts a hole.
    return clamp_byte_round(.2126f * r + .7152f * g + .0722f * b);
}

static bool svg_filter_pair(const char* value, float fallback, float output[2], bool nonnegative = true) {
    output[0] = output[1] = fallback;
    if (!value) return true;
    const char* end = nullptr;
    size_t count = str_parse_float_list(value, ", \t\n\r\f\v", output, 2, &end);
    if (!count || (end && *str_skip_ascii_space(end)) || !isfinite(output[0]) || !isfinite(output[1])) return false;
    if (count == 1) output[1] = output[0];
    if (output[0] < 0.0f || output[1] < 0.0f) {
        if (nonnegative) return false;
        // negative radii disable blur/morphology; lighting uses the default sampling kernel (§§9.10/9.14/9.17).
        output[0] = output[1] = 0.0f;
    }
    return true;
}

static bool svg_filter_kind(const char* tag, RdtSvgFilterKind* kind) {
    static const char* const names[] = {"feGaussianBlur", "feOffset", "feFlood", "feMerge", "feColorMatrix", "feDropShadow",
        "feBlend", "feComposite", "feMorphology", "feImage", "feTile", "feTurbulence", "feDisplacementMap", "feDiffuseLighting", "feSpecularLighting"};
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++)
        if (strcmp(tag, names[index]) == 0) { *kind = (RdtSvgFilterKind)index; return true; }
    return false;
}

static void* svg_filter_compile_alloc(RdtSvgFilterProgram* program, MemContext* memory, size_t bytes) {
    if (!bytes) return nullptr;
    // arena growth includes its next chunk; admission happens outside allocator/reclaimer callbacks.
    void* result = bytes <= SIZE_MAX - ARENA_MAX_CHUNK_SIZE &&
        render_memory_allow_allocation(memory, bytes + ARENA_MAX_CHUNK_SIZE) ? arena_calloc(program->arena, bytes) : nullptr;
    if (!result) { program->valid = false; program->allocation_failed = true; }
    return result;
}

static const char* svg_filter_compile_token(RdtSvgFilterProgram* program, MemContext* memory, const char* token) {
    if (!token) return nullptr;
    size_t bytes = strlen(token) + 1;
    char* result = (char*)svg_filter_compile_alloc(program, memory, bytes);
    if (result) memcpy(result, token, bytes);
    return result;
}

static RdtSvgFilterProgram* svg_filter_compile(SvgInlineRenderContext* ctx, const SvgResourceReference* reference) {
    SvgInlineRenderContext document = svg_reference_render_context(ctx, reference);
    SvgInlineRenderContext style = svg_resource_style_context(&document, reference->element);
    SvgStyleContext* owner = (SvgStyleContext*)style.style_context;
    RdtSvgFilterProgram* program = render_svg_filter_program_acquire(owner->document, reference->element);
    if (!program || program->compiled) return program;
    program->compiled = program->valid = true;
    MemContext* memory = (MemContext*)owner->document->services.mem_ctx;
    const char* units = get_svg_attr(reference->element, "filterUnits");
    program->filter_bbox = !units || strcmp(units, "userSpaceOnUse") != 0;
    units = get_svg_attr(reference->element, "primitiveUnits");
    program->primitive_bbox = units && strcmp(units, "objectBoundingBox") == 0;
    static const char* const region_names[] = {"x", "y", "width", "height"};
    for (size_t axis = 0; axis < 4; axis++) program->region[axis] = lam::own(svg_filter_compile_token(program, memory, get_svg_attr(reference->element, region_names[axis])));
    if (!program->valid) return program;
    for (int64_t index = 0; index < reference->element->length; index++) {
        Element* child = get_child_element_at(reference->element, index);
        const char* tag = child ? get_element_tag_name(&style, child) : nullptr;
        if (tag && strncmp(tag, "fe", 2) == 0) program->count++;
    }
    if (!program->count || program->count > RDT_SVG_FILTER_MAX_NODES) { program->valid = false; return program; }
    program->nodes = lam::own_arr((RdtSvgFilterNode*)svg_filter_compile_alloc(program, memory, program->count * sizeof(RdtSvgFilterNode)));
    if (!program->nodes) return program;
    size_t next = 0;
    for (int64_t index = 0; index < reference->element->length; index++) {
        Element* child = get_child_element_at(reference->element, index);
        const char* tag = child ? get_element_tag_name(&style, child) : nullptr;
        if (!tag || strncmp(tag, "fe", 2) != 0) continue;
        RdtSvgFilterNode* node = &program->nodes[next];
        node->element = lam::up(child);
        if (!svg_filter_kind(tag, &node->kind)) { program->valid = false; break; }
        node->valid = true;
        node->input = render_svg_filter_input(program, next, get_svg_attr(child, "in"));
        node->input2 = render_svg_filter_input(program, next, get_svg_attr(child, "in2"));
        node->result = lam::own(svg_filter_compile_token(program, memory, get_svg_attr(child, "result")));
        for (size_t axis = 0; axis < 4; axis++) node->region[axis] = lam::own(svg_filter_compile_token(program, memory, get_svg_attr(child, region_names[axis])));
        if (!program->valid) break;
        const char* interpolation = svg_style_ancestor_property_value(&style, child, "color-interpolation-filters");
        node->linear = !interpolation || !str_ieq_cstr(interpolation, "sRGB");
        if (node->kind == RDT_SVG_FILTER_BLUR || node->kind == RDT_SVG_FILTER_SHADOW) {
            node->valid = svg_filter_pair(get_svg_attr(child, "stdDeviation"), node->kind == RDT_SVG_FILTER_SHADOW ? 2.0f : 0.0f, node->values, false);
            if (node->kind == RDT_SVG_FILTER_BLUR) {
                const char* edge = get_svg_attr(child, "edgeMode");
                node->variant = edge && strcmp(edge, "duplicate") == 0 ? 1u : edge && strcmp(edge, "wrap") == 0 ? 2u : 0u;
                node->valid = node->valid && (!edge || node->variant || strcmp(edge, "none") == 0);
            }
            if (node->kind == RDT_SVG_FILTER_SHADOW) {
                node->values[2] = get_svg_number_attr(child, "dx", 2.0f);
                node->values[3] = get_svg_number_attr(child, "dy", 2.0f);
            }
        }
        if (node->kind == RDT_SVG_FILTER_FLOOD || node->kind == RDT_SVG_FILTER_SHADOW) {
            SvgInlineRenderContext child_style = svg_resource_style_context(&style, child);
            node->color = svg_resolve_color_keyword(&child_style, svg_style_property_value(&style, child, "flood-color"), "flood-color");
            const char* opacity = svg_style_property_value(&style, child, "flood-opacity");
            node->color.a = clamp_byte_round((float)node->color.a * clamp_unit(parse_svg_pct_or_num(opacity, 1.0f)));
        }
        if (node->kind == RDT_SVG_FILTER_OFFSET) {
            node->values[0] = get_svg_number_attr(child, "dx", 0.0f); node->values[1] = get_svg_number_attr(child, "dy", 0.0f);
        } else if (node->kind == RDT_SVG_FILTER_MERGE) {
            for (int64_t child_index = 0; child_index < child->length; child_index++) {
                Element* merge = get_child_element_at(child, child_index);
                const char* name = merge ? get_element_tag_name(&style, merge) : nullptr;
                if (name && strcmp(name, "feMergeNode") == 0) node->merge_count++;
            }
            if (node->merge_count > SIZE_MAX / sizeof(int)) { program->valid = false; break; }
            node->merge_inputs = lam::own_arr((int*)svg_filter_compile_alloc(program, memory, node->merge_count * sizeof(int)));
            if (node->merge_count && !node->merge_inputs) break;
            size_t merge_index = 0;
            for (int64_t child_index = 0; child_index < child->length; child_index++) {
                Element* merge = get_child_element_at(child, child_index);
                const char* name = merge ? get_element_tag_name(&style, merge) : nullptr;
                if (name && strcmp(name, "feMergeNode") == 0) node->merge_inputs[merge_index++] = render_svg_filter_input(program, next, get_svg_attr(merge, "in"));
            }
        } else if (node->kind == RDT_SVG_FILTER_MATRIX) {
            const char* type = get_svg_attr(child, "type"), *values = get_svg_attr(child, "values");
            node->values[0] = node->values[6] = node->values[12] = node->values[18] = 1.0f;
            if (!type || strcmp(type, "matrix") == 0) {
                if (values) {
                    const char* end = nullptr;
                    node->valid = str_parse_float_list(values, ", \t\n\r", node->values, 20, &end) == 20 &&
                        (!end || !*str_skip_ascii_space(end));
                    for (unsigned coefficient = 0; coefficient < 20; coefficient++) node->valid = node->valid && isfinite(node->values[coefficient]);
                }
            } else if (strcmp(type, "saturate") == 0 || strcmp(type, "hueRotate") == 0) {
                float matrix[3][3], amount = strcmp(type, "saturate") == 0 ? 1.0f : 0.0f;
                if (values) {
                    const char* end = nullptr;
                    node->valid = str_parse_float_list(values, ", \t\n\r", &amount, 1, &end) == 1 && isfinite(amount) &&
                        (!end || !*str_skip_ascii_space(end));
                }
                if (strcmp(type, "saturate") == 0) render_filter_saturate_matrix(amount, matrix);
                else render_filter_hue_matrix(math_degrees_to_radians(amount), matrix);
                for (size_t row = 0; row < 3; row++) for (size_t column = 0; column < 3; column++) node->values[row * 5 + column] = matrix[row][column];
            } else if (strcmp(type, "luminanceToAlpha") == 0) {
                memset(node->values, 0, sizeof(node->values)); node->values[15] = .2126f; node->values[16] = .7152f; node->values[17] = .0722f;
            } else node->valid = false;
            // malformed matrix arity is a pass-through, rather than a transparent primitive (Filter Effects §9.6).
            if (!node->valid) {
                memset(node->values, 0, sizeof(node->values));
                node->values[0] = node->values[6] = node->values[12] = node->values[18] = 1.0f; node->valid = true;
            }
        } else if (node->kind == RDT_SVG_FILTER_COMPOSITE) {
            static const char* const operators[] = {"over", "in", "out", "atop", "xor", "arithmetic", "lighter"};
            const char* operation = get_svg_attr(child, "operator"); bool found = !operation;
            for (unsigned kind = 0; kind < 7; kind++) if (operation && strcmp(operation, operators[kind]) == 0) { node->variant = kind; found = true; }
            node->valid = found;
            for (unsigned coefficient = 0; coefficient < 4; coefficient++) {
                char name[] = {'k', (char)('1' + coefficient), 0}; node->values[coefficient] = get_svg_number_attr(child, name, 0.0f);
            }
        } else if (node->kind == RDT_SVG_FILTER_BLEND) {
            static const char* const names[] = {"normal", "multiply", "screen", "darken", "lighten", "overlay", "color-dodge", "color-burn", "hard-light", "soft-light", "difference", "exclusion", "hue", "saturation", "color", "luminosity"};
            static const CssEnum modes[] = {CSS_VALUE_NORMAL, CSS_VALUE_MULTIPLY, CSS_VALUE_SCREEN, CSS_VALUE_DARKEN, CSS_VALUE_LIGHTEN, CSS_VALUE_OVERLAY,
                CSS_VALUE_COLOR_DODGE, CSS_VALUE_COLOR_BURN, CSS_VALUE_HARD_LIGHT, CSS_VALUE_SOFT_LIGHT, CSS_VALUE_DIFFERENCE, CSS_VALUE_EXCLUSION,
                CSS_VALUE_HUE_BLEND, CSS_VALUE_SATURATION_BLEND, CSS_VALUE_COLOR_BLEND, CSS_VALUE_LUMINOSITY_BLEND};
            const char* mode = get_svg_attr(child, "mode"); node->blend_mode = CSS_VALUE_NORMAL; bool found = !mode;
            for (size_t mode_index = 0; mode_index < sizeof(names) / sizeof(names[0]); mode_index++) if (mode && strcmp(mode, names[mode_index]) == 0) { node->blend_mode = modes[mode_index]; found = true; }
            node->valid = found;
            node->variant = get_svg_attr(child, "no-composite") != nullptr;
        } else if (node->kind == RDT_SVG_FILTER_MORPHOLOGY) {
            node->valid = svg_filter_pair(get_svg_attr(child, "radius"), 0.0f, node->values, false);
            const char* operation = get_svg_attr(child, "operator");
            node->variant = operation && strcmp(operation, "dilate") == 0;
            node->valid = node->valid && (!operation || node->variant || strcmp(operation, "erode") == 0);
        } else if (node->kind == RDT_SVG_FILTER_TURBULENCE) {
            node->valid = svg_filter_pair(get_svg_attr(child, "baseFrequency"), 0.0f, node->values);
            float octaves = get_svg_number_attr(child, "numOctaves", 1.0f), seed = get_svg_number_attr(child, "seed", 0.0f);
            node->valid = node->valid && isfinite(octaves) && octaves >= 0.0f && octaves == floorf(octaves) && isfinite(seed);
            // Filter Effects §9.21 permits an octave cap at the precision of the eight-bit output channels.
            node->values[2] = fminf(octaves, 9.0f);
            const char* type = get_svg_attr(child, "type"), *stitch = get_svg_attr(child, "stitchTiles");
            node->variant = type && strcmp(type, "fractalNoise") == 0 ? 1u : 0u;
            if (stitch && strcmp(stitch, "stitch") == 0) node->variant |= 2u;
            node->valid = node->valid && (!type || (node->variant & 1u) || strcmp(type, "turbulence") == 0) &&
                (!stitch || (node->variant & 2u) || strcmp(stitch, "noStitch") == 0);
            if (node->valid) {
                node->noise = lam::own(render_svg_filter_noise_create(program->arena, memory, seed));
                if (!node->noise) { program->valid = false; program->allocation_failed = true; break; }
            }
        } else if (node->kind == RDT_SVG_FILTER_DISPLACEMENT) {
            node->values[0] = get_svg_number_attr(child, "scale", 0.0f);
            node->valid = isfinite(node->values[0]);
            static const char* const names[] = {"xChannelSelector", "yChannelSelector"};
            const char* channels = "RGBA";
            for (unsigned axis = 0; axis < 2; axis++) {
                const char* selector = get_svg_attr(child, names[axis]);
                const char* channel = selector && strlen(selector) == 1 ? strchr(channels, *selector) : nullptr;
                if (selector && !channel) node->valid = false;
                node->variant |= (channel ? (unsigned)(channel - channels) : 3u) << (axis * 2);
            }
        } else if (node->kind == RDT_SVG_FILTER_DIFFUSE || node->kind == RDT_SVG_FILTER_SPECULAR) {
            RdtSvgFilterLight* light = &node->light;
            light->surface_scale = get_svg_number_attr(child, "surfaceScale", 1.0f);
            light->constant = get_svg_number_attr(child, node->kind == RDT_SVG_FILTER_DIFFUSE ? "diffuseConstant" : "specularConstant", 1.0f);
            light->exponent = get_svg_number_attr(child, "specularExponent", 1.0f);
            node->valid = svg_filter_pair(get_svg_attr(child, "kernelUnitLength"), 0.0f, light->kernel, false);
            if (light->kernel[0] <= 0.0f || light->kernel[1] <= 0.0f) light->kernel[0] = light->kernel[1] = 0.0f;
            SvgInlineRenderContext child_style = svg_resource_style_context(&style, child);
            node->color = svg_resolve_color_keyword(&child_style, svg_style_property_value(&style, child, "lighting-color"), "lighting-color");
            node->color.a = 255; // lighting-color supplies RGB; lighting equations determine output alpha.
            for (int64_t light_index = 0; light_index < child->length; light_index++) {
                Element* source = get_child_element_at(child, light_index);
                const char* source_tag = source ? get_element_tag_name(&style, source) : nullptr;
                if (!source_tag) continue;
                if (strcmp(source_tag, "feDistantLight") == 0) {
                    light->kind = 1;
                    float azimuth = get_svg_number_attr(source, "azimuth", 0.0f) * (float)(M_PI / 180.0);
                    float elevation = get_svg_number_attr(source, "elevation", 0.0f) * (float)(M_PI / 180.0);
                    light->position[0] = cosf(azimuth) * cosf(elevation);
                    light->position[1] = sinf(azimuth) * cosf(elevation); light->position[2] = sinf(elevation);
                } else if (strcmp(source_tag, "fePointLight") == 0 || strcmp(source_tag, "feSpotLight") == 0) {
                    light->kind = strcmp(source_tag, "fePointLight") == 0 ? 2u : 3u;
                    static const char* const positions[] = {"x", "y", "z"}, * const targets[] = {"pointsAtX", "pointsAtY", "pointsAtZ"};
                    for (unsigned axis = 0; axis < 3; axis++) {
                        light->position[axis] = get_svg_number_attr(source, positions[axis], 0.0f);
                        light->target[axis] = get_svg_number_attr(source, targets[axis], 0.0f);
                    }
                    light->spot_exponent = get_svg_number_attr(source, "specularExponent", 1.0f);
                    light->limiting_cone = get_svg_attr(source, "limitingConeAngle") != nullptr;
                    light->cone_cosine = cosf(get_svg_number_attr(source, "limitingConeAngle", 0.0f) * (float)(M_PI / 180.0));
                } else continue;
                break;
            }
            node->valid = node->valid && light->kind && isfinite(light->surface_scale) && isfinite(light->constant) && light->constant >= 0.0f &&
                isfinite(light->exponent) && light->exponent >= 0.0f && isfinite(light->spot_exponent) && light->spot_exponent >= 0.0f && isfinite(light->cone_cosine);
            for (unsigned axis = 0; axis < 3; axis++) node->valid = node->valid && isfinite(light->position[axis]) && isfinite(light->target[axis]);
        }
        next++;
    }
    if (!program->valid) log_error("SVG_FILTER_COMPILE: filter contains an unavailable primitive or exceeds graph limits");
    return program;
}

RdtSvgFilterProgram* render_css_svg_filter_compile(DomDocument* document, const char* url,
    ScratchArena* scratch) {
    SvgPaintHostStyle* host = svg_host_style(document);
    if (!host || !url || !scratch) return nullptr;
    if (!host->animation_prepared) {
        svg_animation_prepare(document->root);
        host->animation_prepared = true;
    }
    SvgInlineRenderContext context = {};
    context.svg_root = context.id_scope = lam::up(dom_element_render_source(document->root));
    context.style_context = lam::up(&host->style);
    context.resource_scratch = lam::up(scratch);
    context.current_viewport_w = document->viewport.width;
    context.current_viewport_h = document->viewport.height;
    context.raster_scale = 1.0f;
    lam::Temp<char> base(radiant_document_resource_base(document, MEM_CAT_RENDER));
    context.source_path = lam::up(base.get());
    SvgResourceReference reference = svg_resolve_reference(&context, url);
    if (!reference.element || !svg_effect_resource_is(&context, &reference, "filter")) return nullptr;
    return svg_filter_compile(&context, &reference);
}

struct SvgFilterImageContext : RdtSvgFilterHost {
    SvgInlineRenderContext context;
    const RdtSvgFilterProgram* program;
    const RdtSvgFilterRun* run;
    Bound region;
};

static void svg_filter_resolve_lengths(RdtSvgFilterHost* host, Element* element, SvgLengthContext* lengths) {
    SvgFilterImageContext* owner = static_cast<SvgFilterImageContext*>(host);
    SvgInlineRenderContext resource = svg_resource_style_context(&owner->context, element);
    SvgLengthContext declared = svg_length_context(&resource);
    // font-relative lengths use the declaration's current font; percentages use the referencing viewport (§9.4).
    lengths->font_size = declared.font_size;
    lengths->x_height = declared.x_height;
    lengths->fonts = declared.fonts;
    lengths->font = declared.font;
}

static void svg_filter_draw_image(SvgInlineRenderContext* ctx, Element* element, void* data) {
    SvgFilterImageContext* image = (SvgFilterImageContext*)data;
    const char* href = get_svg_href(ctx, element);
    if (!href || !*href) return;
    if (strchr(href, '#') && strncmp(href, "data:", 5) != 0) {
        SvgResourceReference reference = svg_resolve_reference(ctx, href);
        if (!reference.element) return;
        SvgInlineRenderContext document = svg_reference_render_context(ctx, &reference);
        Bound viewport = image->region;
        if (image->program->primitive_bbox) {
            Bound geometry = image->run->geometry;
            float width = geometry.right - geometry.left, height = geometry.bottom - geometry.top;
            if (width <= 0 || height <= 0) return;
            viewport = {(viewport.left - geometry.left) / width, (viewport.top - geometry.top) / height,
                (viewport.right - geometry.left) / width, (viewport.bottom - geometry.top) / height};
            RdtMatrix basis = {width, 0, geometry.left, 0, height, geometry.top, 0, 0, 1};
            ctx->transform = rdt_matrix_multiply(&ctx->transform, &basis);
        }
        render_svg_use_target(ctx, element, reference.element, href, &document, &viewport);
    } else render_svg_image_resource(ctx, element, href, image->region, &ctx->transform, false, false);
}

static bool svg_filter_render_image(RdtSvgFilterHost* host, const RdtSvgFilterNode* node, const RdtSvgFilterRun* run,
    Bound region, Bound grid, ImageSurface* output) {
    SvgFilterImageContext* owner = static_cast<SvgFilterImageContext*>(host);
    SvgFilterImageContext image = *owner; image.run = run; image.region = region;
    SvgInlineRenderContext context = svg_resource_style_context(&image.context, node->element);
    context.filter_work = lam::up(run);
    context.transform = rdt_matrix_scale(run->density, run->density);
    context.viewport_transform = rdt_matrix_identity(); context.opacity = 1.0f;
    // the callback shares the ordinary image/use traversal and paints directly into the owned filter grid.
    return svg_rasterize_traversal(&context, node->element, svg_filter_draw_image, &image, &output, &grid);
}

struct SvgFilterPaintContext : RdtSvgFilterHost {
    SvgInlineRenderContext context;
    Element* element;
    const RdtSvgFilterRun* run;
    Bound region;
    bool stroke;
};

static void svg_filter_draw_paint(SvgInlineRenderContext* ctx, Element* element, void* data) {
    SvgFilterPaintContext* input = (SvgFilterPaintContext*)data;
    svg_apply_inherited_paint_attrs(ctx, element);
    RdtPath* path = rdt_path_new();
    if (!path) return;
    const Bound* region = &input->region;
    rdt_path_add_rect(path, region->left, region->top, region->right - region->left, region->bottom - region->top, 0, 0);
    const Bound* geometry = &input->run->geometry;
    // standard paint inputs fill the entire filter region, using the target's bbox and viewport, without fill/stroke opacity (§9.2).
    svg_draw_resolved_paint(ctx, path, input->stroke ? &ctx->stroke_paint : &ctx->fill_paint,
        &ctx->transform, geometry->left, geometry->top, geometry->right - geometry->left,
        geometry->bottom - geometry->top, RDT_FILL_WINDING, 1.0f);
    rdt_path_free(path);
}

static bool svg_filter_render_input(RdtSvgFilterHost* host, int input, const RdtSvgFilterRun* run, Bound grid, ImageSurface* output) {
    SvgFilterPaintContext paint = *static_cast<SvgFilterPaintContext*>(host);
    paint.context.filter_work = lam::up(run);
    if (input == RDT_SVG_FILTER_BACKGROUND) {
        PaintRecordTarget prior = svg_record_target(&paint.context);
        paint_record_lower_pending(&prior);
        Bound capture = {grid.left / run->density, grid.top / run->density, grid.right / run->density, grid.bottom / run->density};
        rdt_matrix_transform_rect_bounds(&run->frame, capture.left, capture.top, capture.right, capture.bottom,
            &capture.left, &capture.top, &capture.right, &capture.bottom);
        capture.left = floorf(capture.left); capture.top = floorf(capture.top);
        capture.right = ceilf(capture.right); capture.bottom = ceilf(capture.bottom);
        ImageSurface* backdrop = svg_create_paint_surface(capture.right - capture.left, capture.bottom - capture.top);
        if (!backdrop) return false;
        // replay only preceding paint in this isolation group; resampling restores the filtered element's user axes.
        bool valid = svg_replay_capture(&paint.context, paint.context.dl, backdrop, &capture, run->scratch, paint.context.backdrop_start);
        if (valid) render_svg_filter_resample_source(run, backdrop, capture, grid, output);
        image_surface_destroy(backdrop);
        return valid;
    }
    if (input != RDT_SVG_FILTER_FILL && input != RDT_SVG_FILTER_STROKE) return true;
    paint.run = run; paint.stroke = input == RDT_SVG_FILTER_STROKE;
    paint.region = {grid.left / run->density, grid.top / run->density, grid.right / run->density, grid.bottom / run->density};
    SvgInlineRenderContext context = paint.context;
    context.transform = rdt_matrix_scale(run->density, run->density);
    context.viewport_transform = rdt_matrix_identity(); context.opacity = 1.0f;
    return svg_rasterize_traversal(&context, paint.element, svg_filter_draw_paint, &paint, &output, &grid);
}

struct SvgFilterSourceContext : RdtSvgFilterHost {
    SvgInlineRenderContext context;
    Element* element;
    SvgElementDrawFn draw;
    void* data;
};

static bool svg_filter_render_source(RdtSvgFilterHost* host, const RdtSvgFilterRun* run, Bound grid, ImageSurface* output) {
    SvgFilterSourceContext* owner = static_cast<SvgFilterSourceContext*>(host);
    SvgInlineRenderContext context = owner->context; context.filter_work = lam::up(run);
    ImageSurface* source = nullptr; Bound capture = {};
    // only a reachable SourceGraphic/SourceAlpha input captures the original subtree.
    bool valid = svg_rasterize_traversal(&context, owner->element, owner->draw, owner->data, &source, &capture, 0.0f, true, true);
    if (valid) render_svg_filter_resample_source(run, source, capture, grid, output);
    if (source) image_surface_destroy(source);
    return valid;
}

static bool svg_render_effect_boundary(SvgInlineRenderContext* ctx, Element* elem,
    SvgElementDrawFn draw, void* data, bool frame_ready) {
    if (ctx->effect_source == elem) return false;
    const char* opacity = ctx->clip_geometry ? nullptr : svg_style_property_value(ctx, elem, "opacity");
    float alpha = opacity ? clamp_unit(parse_svg_pct_or_num(opacity, 1.0f)) : 1.0f;
    SvgResourceReference clip = svg_effect_reference(ctx, elem, "clip-path");
    SvgResourceReference mask = ctx->clip_geometry ? SvgResourceReference{} : svg_effect_reference(ctx, elem, "mask");
    bool has_clip = svg_effect_clip_is_valid(ctx, &clip);
    if (ctx->clip_hit_query) {
        if (!has_clip) return false;
        Bound box = {}; svg_effect_geometry_box(ctx, elem, &box);
        RdtMatrix frame = frame_ready ? ctx->transform : compose_element_transform(ctx, elem);
        return !svg_effect_clip_contains(ctx, &clip, &box, &frame);
    }
    const char* mask_value = ctx->clip_geometry ? nullptr : svg_style_property_value(ctx, elem, "mask");
    bool has_mask = mask_value && strcmp(mask_value, "none") != 0;
    const char* filter_value = ctx->clip_geometry ? nullptr : svg_style_property_value(ctx, elem, "filter");
    const char* isolation = ctx->clip_geometry ? nullptr : svg_style_property_value(ctx, elem, "isolation");
    bool isolated = isolation && strcmp(isolation, "isolate") == 0;
    if (!has_clip && !has_mask && !isolated && (!filter_value || strcmp(filter_value, "none") == 0) && alpha >= 1.0f) return false;
    Bound geometry = {}; svg_effect_geometry_box(ctx, elem, &geometry);
    RdtMatrix frame = frame_ready ? ctx->transform : compose_element_transform(ctx, elem);
    SvgResourceReference filter_reference = ctx->clip_geometry ? SvgResourceReference{} : svg_effect_reference(ctx, elem, "filter");
    bool filtered = svg_effect_resource_is(ctx, &filter_reference, "filter");
    if (!has_clip && !has_mask && !filtered && !isolated && alpha >= 1.0f) return false;
    if (alpha <= 0.0f) return true;
    RdtSvgFilterProgram* program = filtered ? svg_filter_compile(ctx, &filter_reference) : nullptr;
    if (filtered && (!program || !program->valid || !program->count)) { render_svg_filter_program_release(program); return true; }
    SvgInlineRenderContext source = *ctx;
    source.effect_source = lam::up(elem); source.opacity = 1.0f;
    ImageSurface* image = nullptr;
    Bound capture = {};
    if (!filtered && !svg_rasterize_traversal(&source, elem, draw, data, &image, &capture, 0, false, true)) {
        if (image) image_surface_destroy(image);
        render_svg_filter_program_release(program); return true;
    }
    RdtMatrix placement = rdt_matrix_identity();
    SvgInlineRenderContext coverage_context = *ctx;
    RdtSvgFilterRun run = {};
    size_t work_used = 0;
    if (filtered) {
        run.scratch = ctx->resource_scratch;
        SvgStyleContext* style = (SvgStyleContext*)ctx->style_context;
        run.memory = lam::up(style && style->document ? (MemContext*)style->document->services.mem_ctx : nullptr);
        run.geometry = geometry; run.frame = frame;
        run.lengths = svg_length_context(ctx, elem);
        run.density = fmaxf(hypotf(frame.e11, frame.e21), hypotf(frame.e12, frame.e22));
        SvgFilterSourceContext source_context = {{}, source, elem, draw, data};
        run.draw_source = svg_filter_render_source; run.source_context = lam::up(&source_context);
        SvgFilterImageContext image_context = {{}, svg_reference_render_context(ctx, &filter_reference), program, &run, {}};
        run.draw_image = svg_filter_render_image; run.image_context = lam::up(&image_context);
        run.resolve_lengths = svg_filter_resolve_lengths;
        SvgFilterPaintContext paint_context = {{}, *ctx, elem, &run, {}, false};
        run.draw_input = svg_filter_render_input; run.input_context = lam::up(&paint_context);
        memtrack_get_limits(nullptr, nullptr, &run.work_limit);
        run.work_used = lam::up(ctx->filter_work ? ctx->filter_work->work_used : &work_used);
        ImageSurface* output = nullptr;
        bool valid = render_svg_filter_execute(program, &run, &output, &capture, &placement);
        render_svg_filter_program_release(program);
        if (image) image_surface_destroy(image);
        image = output;
        RdtMatrix inverse;
        if (!valid || !image || !rdt_matrix_invert_affine(&placement, &inverse)) {
            if (image) image_surface_destroy(image); return true;
        }
        // masks and clips share the filter grid; final placement restores the element's physical frame.
        coverage_context.transform = rdt_matrix_multiply(&inverse, &ctx->transform);
        coverage_context.viewport_transform = rdt_matrix_multiply(&inverse, &ctx->viewport_transform);
        coverage_context.filter_work = lam::up(&run);
        frame = rdt_matrix_scale(run.density, run.density);
    }
    uint32_t* pixels = (uint32_t*)image->pixels;
    size_t count = (size_t)image->width * (size_t)image->height;
    for (size_t operation = 0; operation < 2; operation++) {
        if (!(operation == 0 ? has_clip : has_mask)) continue;
        const SvgResourceReference* resource = operation == 0 ? &clip : &mask;
        ImageSurface* coverage = nullptr;
        bool luminance = false, linear = false;
        bool valid = operation == 0 || svg_effect_resource_is(ctx, resource, "mask");
        valid = valid && render_svg_filter_spend_work(coverage_context.filter_work, count) &&
            svg_effect_coverage(&coverage_context, resource, &geometry, &frame, &capture,
            operation == 1, &coverage, &luminance, &linear);
        const uint32_t* values = valid && coverage ? (const uint32_t*)coverage->pixels : nullptr;
        for (size_t index = 0; index < count; index++) pixels[index] = render_pixel_scale_premultiplied(pixels[index],
            values ? svg_mask_pixel_coverage(values[index], luminance, linear) : 0);
        if (coverage) image_surface_destroy(coverage);
    }
    uint8_t opacity_byte = clamp_byte_round(alpha * ctx->opacity * 255.0f);
    if (!render_svg_filter_spend_work(coverage_context.filter_work, count)) { image_surface_destroy(image); return true; }
    for (size_t index = 0; index < count; index++) pixels[index] = render_pixel_scale_premultiplied(pixels[index], opacity_byte);
    PaintRecordTarget destination = svg_record_target(ctx);
    paint_record_draw_image_resource(&destination, "svg_effect_result", image, capture.left, capture.top,
        (float)image->width, (float)image->height, 255, filtered ? &placement : nullptr, image_surface_destroy);
    return true;
}

bool svg_dom_clip_contains_point(DomElement* target, const SvgLengthContext* lengths,
    FontContext* fonts, const RdtMatrix* frame, SvgPathContainsFn contains, const RdtLogicalPoint* point,
    const SvgDomStyleScope* instance_scope) {
    if (!target || !target->doc || !lengths || !frame || !contains) return false;
    DomElement* root = target;
    for (DomNode* node = target; node && node->is_element(); node = svg_dom_style_parent(node, instance_scope))
        if (node->as_element()->tag_name && str_icmp_cstr(node->as_element()->tag_name, "svg") == 0) root = node->as_element();
    SvgStyleContext style = {};
    if (!svg_style_init(&style, dom_element_to_element(root), lengths->viewport_width, lengths->viewport_height, target->doc)) return false;
    Arena* arena = mem_arena_create(nullptr, MEM_ROLE_RENDER, "render.svg.clip_query");
    if (!arena) { svg_style_destroy(&style); return false; }
    ScratchArena scratch = {};
    mem_scratch_init(nullptr, &scratch, arena, MEM_ROLE_RENDER, "render.svg.clip_query.scratch");
    SvgInlineRenderContext ctx = {};
    ctx.svg_root = lam::up(dom_element_to_element(root)); ctx.id_scope = lam::up(ctx.svg_root);
    ctx.style_context = lam::up(&style); ctx.resource_scratch = lam::up(&scratch); ctx.font_ctx = lam::up(fonts);
    ctx.current_viewport_w = lengths->viewport_width; ctx.current_viewport_h = lengths->viewport_height;
    ctx.inherited_font_size = 16.0f; ctx.inherited_font_weight = 400;
    ctx.transform = *frame; ctx.raster_scale = 1.0f;
    lam::ArrayList<Element*> ancestors(MEM_CAT_RENDER, 0);
    for (DomNode* node = svg_dom_style_parent(target, instance_scope); node && node->is_element();
        node = svg_dom_style_parent(node, instance_scope))
        if (!ancestors.append(dom_element_to_element(node->as_element()))) break;
    for (size_t index = ancestors.size(); index > 0; index--) svg_apply_inherited_paint_attrs(&ctx, ancestors[index - 1]);
    SvgClipHitQuery query = {contains, lam::up(point), false}; ctx.clip_hit_query = lam::up(&query);
    lam::Temp<char> base(radiant_document_resource_base(target->doc, MEM_CAT_RENDER)); ctx.source_path = lam::up(base.get());
    Element* element = dom_element_to_element(target);
    SvgResourceReference clip = svg_effect_reference(&ctx, element, "clip-path");
    bool hit = true;
    if (svg_effect_clip_is_valid(&ctx, &clip)) {
        Bound geometry = {}; svg_effect_geometry_box(&ctx, element, &geometry);
        hit = svg_effect_clip_contains(&ctx, &clip, &geometry, frame);
    }
    base.reset(); svg_style_destroy(&style); scratch_release(&scratch); mem_arena_destroy(arena);
    return hit;
}

// visibility suppresses ink, while containers still visit descendants that
// explicitly restore visibility. Display:none remains a subtree exclusion.
static void render_svg_element(SvgInlineRenderContext* ctx, Element* elem) {
    if (!ctx || !elem || !svg_element_is_eligible(ctx, elem)) return;
    bool saved_hidden = ctx->visibility_hidden;
    const char* visibility = svg_style_property_value(ctx, elem, "visibility");
    if (visibility) ctx->visibility_hidden = strcmp(visibility, "hidden") == 0 ||
        strcmp(visibility, "collapse") == 0;
    const char* tag = get_element_tag_name(ctx, elem);
    bool container = tag && (strcmp(tag, "g") == 0 || strcmp(tag, "svg") == 0 ||
        strcmp(tag, "a") == 0 || strcmp(tag, "use") == 0 || strcmp(tag, "text") == 0 ||
        strcmp(tag, "switch") == 0 || strcmp(tag, "foreignObject") == 0);
    if (!ctx->visibility_hidden || container) {
        int semantic_begin = svg_export_begin_element(ctx, elem);
        if ((tag && strcmp(tag, "svg") == 0) ||
            !svg_render_effect_boundary(ctx, elem, svg_draw_element_content)) render_svg_element_content(ctx, elem);
        if (semantic_begin >= 0) dl_end_element(ctx->dl, semantic_begin);
    }
    ctx->visibility_hidden = saved_hidden;
}

// ============================================================================
// Build SVG Scene
// ============================================================================

static void render_svg_to_display_list_primitives(Element* svg_element, float viewport_width, float viewport_height,
                       Pool* pool, float raster_scale, FontContext* font_ctx, const RdtMatrix* base_transform,
                       DisplayList* dl, const Color* initial_current_color, const Color* initial_fill_color,
                       const char* source_path, float initial_opacity, bool initial_fill_none,
                       const Color* initial_stroke_color, bool initial_stroke_none,
                       float initial_stroke_width, PaintList* paint_list,
                       ScratchArena* resource_scratch, Element* id_scope,
                       bool image_document = false, double image_time = 0,
                       bool capture_semantics = false) {
    if (!svg_element) return;
    if (!dl || !paint_list) {
        log_error("[SVG] render_svg_to_display_list requires display-list and PaintIR targets");
        return;
    }
    if (source_path && svg_resource_stack_contains(source_path)) {
        return;
    }
    if (!resource_scratch) {
        log_error("[SVG] render_svg_to_display_list requires resource scratch");
        return;
    }
    ScratchMark resource_mark = scratch_mark(resource_scratch);
    bool pushed_source = svg_resource_stack_push(source_path);


    // initialize render context
    SvgInlineRenderContext ctx = {};
    ctx.svg_root = lam::up(svg_element);
    ctx.id_scope = lam::up(id_scope ? id_scope : svg_element);
    ctx.pool = lam::up(pool);
    ctx.font_ctx = lam::up(font_ctx);
    ctx.dl = lam::up(dl);
    ctx.semantic_target = lam::up(capture_semantics ? dl : nullptr);
    ctx.paint_list = lam::up(paint_list);
    PaintRecordTarget prior = svg_record_target(&ctx);
    paint_record_lower_pending(&prior);
    ctx.backdrop_start = dl_item_count(dl);
    ctx.resource_scratch = lam::up(resource_scratch);
    ctx.source_path = lam::up(source_path);
    ctx.image_document = image_document;
    Element* resolver_root = nullptr;
    svg_get_registered_image_resolver(svg_element, &ctx.image_resolver, &resolver_root);
    ctx.image_resolver_context = lam::up(resolver_root);
    ctx.raster_scale = raster_scale > 0.0f ? raster_scale : 1.0f;
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
    ctx.fill_paint.kind = ctx.fill_none ? SVG_PAINT_NONE : SVG_PAINT_COLOR;
    ctx.fill_paint.color = ctx.fill_color;
    ctx.stroke_paint.kind = ctx.stroke_none ? SVG_PAINT_NONE : SVG_PAINT_COLOR;
    ctx.stroke_paint.color = ctx.stroke_color;
    if (initial_stroke_width >= 0.0f) {
        ctx.stroke_width = initial_stroke_width;
    }
    initial_opacity = clamp_unit(initial_opacity);
    ctx.opacity = initial_opacity;
    ctx.fill_opacity = ctx.stroke_opacity = 1.0f;

    // start with base transform (document position/scale)
    ctx.transform = base_transform ? *base_transform : rdt_matrix_identity();
    ctx.viewport_transform = ctx.transform;
    ctx.viewport_width = viewport_width;
    ctx.viewport_height = viewport_height;

    // sample document values before the root viewport and its resource spaces are established.
    SvgStyleContext local_style = {};
    SvgStyleContext* shared_style = image_document ? nullptr : svg_paint_host_style(svg_element);
    if (!shared_style) svg_style_init(&local_style, svg_element, viewport_width, viewport_height,
        nullptr, source_path, image_document);
    SvgStyleContext& style = shared_style ? *shared_style : local_style;
    ctx.style_context = lam::up(&style);
    SvgAnimationSourceScope animation_sources(style.document);
    // the shared host context prepares SMIL once per pass
    if (!shared_style && style.document && style.document->root) svg_animation_prepare(style.document->root);
    if (style.isolated_document && style.document->root)
        svg_animation_set_document_time(style.document, image_time);

    // parse viewBox
    const char* viewbox_attr = get_svg_attr(svg_element, "viewBox");
    if (!viewbox_attr) viewbox_attr = get_svg_attr(svg_element, "viewbox");
    SvgViewBox vb = svg_parse_viewbox(viewbox_attr);

    // implicit viewBox: when an SVG has no viewBox but has explicit width/height
    // attributes, browsers treat the intrinsic dims as an implicit viewBox so
    // that content scales to fit the rendered viewport (this matches the way
    // browsers paint <img src=svg> at its CSS box size, and avoids leaving the
    // canvas mostly empty when SVG intrinsic size differs from the viewport).
    DomDocument* host_document = g_svg_active_rdcon && g_svg_active_rdcon->ui_context
        ? g_svg_active_rdcon->ui_context->document : nullptr;
    // the shared host context exists only for an SVG found in the host index
    bool inline_root = shared_style || (host_document && dom_find_element_for_source(host_document->root, svg_element));
    if (!inline_root && !vb.has_viewbox && viewport_width > 0 && viewport_height > 0) {
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
        RdtMatrix mapping = svg_viewbox_transform(&vb, viewport_width, viewport_height,
            get_svg_attr(svg_element, "preserveAspectRatio"));
        ctx.scale_x = mapping.e11; ctx.scale_y = mapping.e22;
        ctx.translate_x = mapping.e13; ctx.translate_y = mapping.e23;
        ctx.transform = rdt_matrix_multiply(&ctx.transform, &mapping);
    } else {
        ctx.scale_x = ctx.scale_y = 1.0f;
        ctx.translate_x = ctx.translate_y = 0;
    }

    FontContext* isolated_fonts = nullptr;
    if (style.isolated_document || !ctx.font_ctx) {
        isolated_fonts = svg_style_font_context(&style, source_path,
            ctx.raster_scale, image_document, ctx.font_ctx);
        if (isolated_fonts) ctx.font_ctx = lam::up(isolated_fonts);
    }
    SvgStyleEntry* root_style = svg_style_entry(&style, svg_element);
    DomElement* host_parent = root_style && root_style->node->parent &&
        root_style->node->parent->is_element() ? root_style->node->parent->as_element() : nullptr;
    if (host_parent && host_parent->font) {
        FontProp* font = host_parent->font;
        FontStyleDesc descriptor = font_style_desc_from_prop(font);
        ctx.inherited_font_family = font->family;
        ctx.inherited_font_size = font->font_size;
        ctx.inherited_font_weight = descriptor.weight;
        ctx.inherited_font_style = lam::up(descriptor.slant == FONT_SLANT_ITALIC ? "italic"
            : descriptor.slant == FONT_SLANT_OBLIQUE ? "oblique" : "normal");
    }
    process_svg_root_resources(&ctx, svg_element);
    svg_apply_inherited_paint_attrs(&ctx, svg_element);
    const char* root_visibility = svg_style_property_value(&ctx, svg_element, "visibility");
    ctx.visibility_hidden = root_visibility ? strcmp(root_visibility, "hidden") == 0 ||
        strcmp(root_visibility, "collapse") == 0 : host_parent && host_parent->in_line &&
        host_parent->in_line->visibility != VIS_VISIBLE;

    // the root's viewport is already established; its effects still scope all content once.
    if (svg_element_is_eligible(&ctx, svg_element)) {
        int semantic_begin = svg_export_begin_element(&ctx, svg_element);
        if (!svg_render_effect_boundary(&ctx, svg_element, svg_draw_children, nullptr, true)) render_svg_children(&ctx, svg_element);
        if (semantic_begin >= 0) dl_end_element(dl, semantic_begin);
    }

    if (!shared_style) svg_style_destroy(&local_style);
    if (isolated_fonts) font_context_destroy(isolated_fonts);
    scratch_restore(resource_scratch, resource_mark);
    if (pushed_source) svg_resource_stack_pop(source_path);
}

void render_svg_build_subscene(PaintSvgSubscene* subscene,
                      Element* svg_element,
                      float viewport_width, float viewport_height,
                      Pool* pool, float raster_scale,
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
                      float initial_stroke_width, UiContext* ui_context) {
    if (!subscene) return;
    memset(subscene, 0, sizeof(PaintSvgSubscene));
    subscene->svg_root = lam::up(svg_element);
    subscene->pool = lam::up(pool);
    subscene->font_context = lam::up(font_ctx);
    subscene->ui_context = lam::up(ui_context ? ui_context : g_svg_active_rdcon ? g_svg_active_rdcon->ui_context : nullptr);
    subscene->clip_viewport = true;
    DomDocument* doc = subscene->ui_context ? subscene->ui_context->document : nullptr;
    DomElement* live = doc ? dom_find_element_for_source(doc->root, svg_element) : nullptr;
    if (live) {
        char overflow[64];
        const char* value = svg_get_dom_presentation_property(live, "overflow", false, overflow, sizeof(overflow));
        subscene->clip_viewport = !value || strcmp(value, "visible") != 0;
    }
    subscene->viewport_width = viewport_width;
    subscene->viewport_height = viewport_height;
    subscene->raster_scale = raster_scale > 0.0f ? raster_scale : 1.0f;
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
    subscene->source_path = lam::up(source_path);  // RETAINED_FIELD_OK: subscene-local field, not a retained DOM field
    initial_opacity = clamp_unit(initial_opacity);
    subscene->opacity = initial_opacity;
    subscene->resource_generation = (uint64_t)(uintptr_t)svg_element;
}

struct SvgExportOutput { StrBuf* out; int indent; };

static bool svg_export_paint_consume(PaintList* paint, void* context) {
    SvgExportOutput* output = (SvgExportOutput*)context;
    if (output->out->length > INT_MAX) return false;
    StrBuf* fragment = strbuf_new();
    if (!fragment) return false;
    PaintSvgLoweringOptions options = {}; options.indent_level = output->indent;
    // generated definitions are scoped by their position in the final stream, including repeated pictures.
    options.resource_id_base = (int)output->out->length; // INT_CAST_OK: bounded output offset is a generated resource ID.
    PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(paint, fragment, &options, &stats);
    bool ok = stats.unsupported_count == 0 && stats.fallback_count == 0;
    if (ok) strbuf_append_str_n(output->out, fragment->str, fragment->length);
    strbuf_free(fragment);
    return ok;
}

static bool render_svg_subscene_to_svg(const PaintSvgSubscene* subscene, StrBuf* out, int indent) {
    if (!subscene || !out) return false;
    SvgExportOutput output = {out, indent};
    return render_svg_subscene_with_paint(subscene, svg_export_paint_consume, &output, true);
}

static void render_svg_record_subscene(const PaintSvgSubscene* subscene,
                                      DisplayList* dl, bool capture_semantics) {
    if (!subscene || !subscene->svg_root || !dl) return;

    Pool* temp_pool = mem_pool_create(mem_context_process(MEM_ROLE_RENDER), MEM_ROLE_RENDER, "render.svg_inline");
    if (!temp_pool) return;
    Arena* temp_arena = mem_arena_create(mem_context_process(MEM_ROLE_RENDER), MEM_ROLE_RENDER, "render.svg_inline.arena");
    if (!temp_arena) {
        mem_pool_destroy(temp_pool);
        return;
    }

    PaintList nested_paint = {};
    ScratchArena resource_scratch = {};
    paint_list_init(&nested_paint, temp_arena);
    mem_scratch_init(NULL, &resource_scratch, temp_arena,
                     MEM_ROLE_RENDER, "render.svg_inline.resources");

    Color* current_color = subscene->has_color ? (Color*)&subscene->color : nullptr;
    Color* fill_color = subscene->has_fill ? (Color*)&subscene->fill : nullptr;
    Color* stroke_color = subscene->has_stroke ? (Color*)&subscene->stroke : nullptr;
    Pool* render_pool = subscene->pool ? (Pool*)subscene->pool : temp_pool;

    // export walks need the same host CSS/font/foreignObject context as interactive recording.
    RasterRenderContext bridge = {};
    bridge.ui_context = lam::up(subscene->ui_context); bridge.dl = lam::up(dl); bridge.paint_list = lam::up(&nested_paint);
    bridge.raster_scale = subscene->raster_scale; bridge.scratch = resource_scratch;
    bridge.block.clip = subscene->clip_viewport
        ? Bound{0, 0, subscene->viewport_width * subscene->raster_scale, subscene->viewport_height * subscene->raster_scale}
        : Bound{-FLT_MAX / 16, -FLT_MAX / 16, FLT_MAX / 16, FLT_MAX / 16};
    bridge.color.a = 255;
    if (subscene->has_color) bridge.color = subscene->color;
    RasterRenderContext* previous = g_svg_active_rdcon;
    if (subscene->ui_context) g_svg_active_rdcon = &bridge;
    RdtPath* viewport_clip = nullptr;
    if (subscene->clip_viewport) {
        viewport_clip = rdt_path_new();
        if (viewport_clip) {
            rdt_path_add_rect(viewport_clip, 0, 0, subscene->viewport_width, subscene->viewport_height, 0, 0);
            dl_push_clip(dl, viewport_clip, &subscene->transform);
        }
    }

    render_svg_to_display_list_primitives((Element*)subscene->svg_root,
                      subscene->viewport_width,
                      subscene->viewport_height,
                      render_pool,
                      subscene->raster_scale,
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
                      &nested_paint,
                      &resource_scratch,
                      (Element*)subscene->id_scope, subscene->image_document, subscene->animation_time,
                      capture_semantics);

    if (viewport_clip) { dl_pop_clip(dl); rdt_path_free(viewport_clip); }
    g_svg_active_rdcon = previous;

    scratch_release(&resource_scratch);
    paint_list_destroy(&nested_paint);
    mem_arena_destroy(temp_arena);
    mem_pool_destroy(temp_pool);
}

static void render_svg_subscene_to_display_list(const PaintSvgSubscene* subscene, DisplayList* dl) {
    render_svg_record_subscene(subscene, dl, false);
}

bool render_svg_subscene_with_paint(const PaintSvgSubscene* subscene,
    SvgExportPaintConsumer consumer, void* context, bool allow_raster_fallback) {
    if (!subscene || !subscene->svg_root || !consumer) return false;
    Arena* arena = mem_arena_create(nullptr, MEM_ROLE_RENDER, "render.svg.export.vector");
    if (!arena) return false;
    DisplayList dl = {}; dl_init(&dl, arena);
    PaintSvgSubscene logical = *subscene; logical.raster_scale = 1;
    render_svg_record_subscene(&logical, &dl, true);
    PaintList paint = {};
    bool valid = dl_validate_or_log(&dl, "svg_export_vector");
    // resolved image pixels feed the paint list until the consumer has run
    ImageSurfaceReadScope read_scope;
    // the consumer runs before the recording owner expires (D4.2.2v2); unrepresentable pixel operations select shared replay.
    for (int i = 0; valid && i < dl.item_count(); i++) {
        const DisplayItem& item = dl.data()[i];
        switch (item.op) {
        case DL_FILL_RECT: {
            const DlFillRect& p = item.fill_rect;
            paint_fill_rect(&paint, p.x, p.y, p.w, p.h, p.color); break;
        }
        case DL_FILL_ROUNDED_RECT: {
            const DlFillRoundedRect& p = item.fill_rounded_rect;
            paint_fill_rounded_rect(&paint, p.x, p.y, p.w, p.h, p.rx, p.ry, p.color); break;
        }
        case DL_FILL_PATH: {
            const DlFillPath& p = item.fill_path;
            paint_fill_path(&paint, p.path, p.color, p.rule, p.has_transform ? &p.transform : nullptr); break;
        }
        case DL_STROKE_PATH: {
            const DlStrokePath& p = item.stroke_path;
            paint_stroke_path(&paint, p.path, p.color, p.width, p.cap, p.join,
                p.dash_array, p.dash_count, p.dash_phase, p.has_transform ? &p.transform : nullptr, p.miter_limit); break;
        }
        case DL_FILL_LINEAR_GRADIENT: {
            const DlFillLinearGradient& p = item.fill_linear_gradient;
            paint_fill_linear_gradient(&paint, p.path, p.x1, p.y1, p.x2, p.y2, p.stops,
                p.stop_count, p.rule, p.has_transform ? &p.transform : nullptr,
                p.has_gradient_transform ? &p.gradient_transform : nullptr, &p.options); break;
        }
        case DL_FILL_RADIAL_GRADIENT: {
            const DlFillRadialGradient& p = item.fill_radial_gradient;
            paint_fill_radial_gradient(&paint, p.path, p.cx, p.cy, p.r, p.stops,
                p.stop_count, p.rule, p.has_transform ? &p.transform : nullptr,
                p.has_gradient_transform ? &p.gradient_transform : nullptr, &p.options); break;
        }
        case DL_DRAW_IMAGE: {
            const DlDrawImage& p = item.draw_image;
            DlResolvedImage resolved;
            // a released owner draws nothing
            if (!dl_draw_image_resolve(&p, &resolved)) break;
            // the display-list stack exclusively owns this arena until synchronous export ends.
            ImageSurface* image = (ImageSurface*)scratch_calloc(&dl.arena, sizeof(ImageSurface));
            if (!image) { valid = false; break; }
            image->width = resolved.width; image->height = resolved.height; image->pitch = resolved.stride * 4;
            image->pixels = (void*)resolved.pixels;
            image->alpha_mode = resolved.straight_alpha ? IMAGE_ALPHA_STRAIGHT : IMAGE_ALPHA_PREMULTIPLIED;
            paint_draw_image(&paint, resolved.pixels, resolved.width, resolved.height, resolved.stride,
                p.dst_x, p.dst_y, p.dst_w, p.dst_h, p.opacity,
                p.has_transform ? &p.transform : nullptr, image, p.scale_mode); break;
        }
        case DL_PUSH_CLIP: {
            const DlPushClip& p = item.push_clip;
            paint_push_clip(&paint, p.path, p.has_transform ? &p.transform : nullptr, p.rule); break;
        }
        case DL_POP_CLIP: paint_pop_clip(&paint); break;
        case DL_BEGIN_ELEMENT:
            paint_begin_semantic_group(&paint, &item.element_marker.semantics); break;
        case DL_END_ELEMENT:
            paint_end_semantic_group(&paint); break;
        default: valid = false; break;
        }
    }
    bool ok = valid && paint_ir_validate_or_log(&paint, "svg_export_vector_paint") && consumer(&paint, context);
    if (!ok && allow_raster_fallback) {
        Bound bounds = {};
        ImageSurface* surface = render_svg_subscene_rasterize(subscene, &bounds);
        if (surface) {
            paint_list_clear(&paint);
            bool placed = false;
            auto place_snapshot = [&]() {
                paint_draw_image(&paint, (const uint32_t*)surface->pixels, surface->width, surface->height,
                    surface->pitch / 4, bounds.left, bounds.top, bounds.right - bounds.left,
                    bounds.bottom - bounds.top, 255, &subscene->transform, surface);
                placed = true;
            };
            // flatten unrepresentable paint while retaining the recording's semantic tree and text.
            for (int i = 0; i < dl.item_count(); i++) {
                const DisplayItem& item = dl.data()[i];
                if (item.op == DL_BEGIN_ELEMENT) {
                    paint_begin_semantic_group(&paint, &item.element_marker.semantics);
                    if (!placed) place_snapshot();
                } else if (item.op == DL_END_ELEMENT) paint_end_semantic_group(&paint);
            }
            if (!placed) place_snapshot();
            ok = paint_ir_validate_or_log(&paint, "svg_export_snapshot_paint") && consumer(&paint, context);
            paint_list_clear(&paint);
            image_surface_destroy(surface);
        }
    }
    paint_list_destroy(&paint); dl_destroy(&dl); mem_arena_destroy(arena);
    return ok;
}

ImageSurface* render_svg_subscene_rasterize(const PaintSvgSubscene* subscene, Bound* logical_bounds) {
    if (!subscene || !subscene->svg_root || !logical_bounds ||
        !isfinite(subscene->viewport_width) || !isfinite(subscene->viewport_height) ||
        subscene->viewport_width <= 0 || subscene->viewport_height <= 0) return nullptr;
    float scale = isfinite(subscene->raster_scale) && subscene->raster_scale > 0 ? subscene->raster_scale : 1;
    OffscreenRenderArenas arenas;
    if (!arenas.init("render.svg.export", "render.svg.export.list_arena",
        "render.svg.export.scratch_arena")) return nullptr;
    DisplayList dl = {}; dl_init(&dl, arenas.list_arena);
    PaintSvgSubscene local = *subscene;
    local.transform = rdt_matrix_scale(scale, scale);
    render_svg_subscene_to_display_list(&local, &dl);
    Bound bounds = {0, 0, subscene->viewport_width * scale, subscene->viewport_height * scale};
    if (!subscene->clip_viewport) {
        Bound ink = dl_content_bounds(&dl);
        bounds.left = fminf(bounds.left, ink.left); bounds.top = fminf(bounds.top, ink.top);
        bounds.right = fmaxf(bounds.right, ink.right); bounds.bottom = fmaxf(bounds.bottom, ink.bottom);
    }
    bounds.left = floorf(bounds.left); bounds.top = floorf(bounds.top);
    bounds.right = ceilf(bounds.right); bounds.bottom = ceilf(bounds.bottom);
    DomDocument* doc = subscene->ui_context ? subscene->ui_context->document : nullptr;
    MemContext* memory = doc ? (MemContext*)doc->services.mem_ctx : nullptr;
    ImageSurface* surface = render_display_list_snapshot(&dl, memory, bounds, scale);
    if (surface) {
        *logical_bounds = {bounds.left / scale, bounds.top / scale, bounds.right / scale, bounds.bottom / scale};
    }
    dl_destroy(&dl); arenas.destroy();
    return surface;
}

void render_svg_inline_register_paint_ir_lowerers(void) {
    paint_ir_register_svg_subscene_lowerers(render_svg_subscene_to_display_list,
                                            render_svg_subscene_to_svg);
}

static void render_svg_to_display_list(Element* svg_element, float viewport_width, float viewport_height,
                       Pool* pool, float raster_scale, FontContext* font_ctx, const RdtMatrix* base_transform,
                       DisplayList* dl, const Color* initial_current_color, const Color* initial_fill_color,
                       const char* source_path, float initial_opacity, bool initial_fill_none,
                       const Color* initial_stroke_color, bool initial_stroke_none,
                       float initial_stroke_width, PaintList* paint_list,
                       ScratchArena* resource_scratch, Element* id_scope,
                       bool image_document = false, double image_time = 0) {
    render_svg_inline_register_paint_ir_lowerers();
    render_svg_to_display_list_primitives(svg_element,
                                          viewport_width,
                                          viewport_height,
                                          pool,
                                          raster_scale,
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
                                          paint_list,
                                          resource_scratch,
                                          id_scope, image_document, image_time);
}

void render_svg_record_picture(PaintList* paint_list, DisplayList* dl,
                               ScratchArena* scratch, FontContext* font_ctx,
                               RasterRenderContext* glyph_rdcon, RdtPicture* picture,
                               uint8_t opacity, const RdtMatrix* transform) {
    Element* svg_root = rdt_picture_get_svg_root(picture);
    if (!svg_root || !paint_list || !dl || !scratch) return;
    float width = 0.0f;
    float height = 0.0f;
    rdt_picture_get_size(picture, &width, &height);
    RdtMatrix base = rdt_picture_compose_transform(picture, transform);
    // glyph items go to the RasterRenderContext recording `dl`, or, without one,
    // text falls back to ThorVG paints
    RasterRenderContext* saved_svg_rdcon = g_svg_active_rdcon;
    g_svg_active_rdcon = glyph_rdcon;
    // an SVG image is its own document: no inherited paint, its own id scope
    render_svg_to_display_list(svg_root, width, height, rdt_picture_get_pool(picture), 1.0f,
                               font_ctx, &base, dl, nullptr, nullptr,
                               rdt_picture_get_source_path(picture),
                               (float)opacity / 255.0f, false, nullptr, true, -1.0f,
                               paint_list, scratch, nullptr, true, rdt_picture_animation_time(picture));
    g_svg_active_rdcon = saved_svg_rdcon;
}

void render_svg_to_vec_via_display_list(RdtVector* vec, Element* svg_element,
                       float viewport_width, float viewport_height,
                       Pool* pool, float raster_scale, FontContext* font_ctx,
                       const RdtMatrix* base_transform,
                       const Color* initial_current_color,
                       const Color* initial_fill_color,
                       const char* source_path, float initial_opacity,
                       bool initial_fill_none,
                       const Color* initial_stroke_color,
                       bool initial_stroke_none,
                       float initial_stroke_width,
                       Element* id_scope, double image_time) {
    if (!vec || !svg_element) return;

    RdtVectorTarget target = {};
    if (!rdt_vector_get_target(vec, &target)) {
        log_error("[SVG] display-list SVG picture render missing vector target");
        return;
    }

    OffscreenRenderArenas arenas;
    if (!arenas.init("render.svg_inline", "render.svg_inline.list_arena",
                     "render.svg_inline.scratch_arena")) return;

    DisplayList dl = {};
    PaintList paint_list = {};
    ScratchArena scratch = {};
    dl_init(&dl, arenas.list_arena);
    paint_list_init(&paint_list, nullptr);
    mem_scratch_init(NULL, &scratch, arenas.scratch_arena, MEM_ROLE_RENDER, "render.svg_inline.scratch");

    // this list is private, so no RasterRenderContext may receive its glyphs
    RasterRenderContext* saved_svg_rdcon = g_svg_active_rdcon;
    g_svg_active_rdcon = nullptr;
    render_svg_to_display_list(svg_element, viewport_width, viewport_height,
                               pool, raster_scale, font_ctx, base_transform, &dl,
                               initial_current_color, initial_fill_color, source_path,
                               initial_opacity, initial_fill_none,
                               initial_stroke_color, initial_stroke_none,
                               initial_stroke_width, &paint_list, &scratch, id_scope, true, image_time);
    g_svg_active_rdcon = saved_svg_rdcon;

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
                           raster_scale > 0.0f ? raster_scale : 1.0f);
        } else {
            Bound clip = {0.0f, 0.0f, (float)target.width, (float)target.height};
            dl_replay(&dl, vec, &surface, &clip, &scratch,
                      raster_scale > 0.0f ? raster_scale : 1.0f, nullptr);
        }
    }

    scratch_release(&scratch);
    paint_list_destroy(&paint_list);
    dl_destroy(&dl);
    arenas.destroy();
}

// ============================================================================
// Inline SVG raster layer cache
// ============================================================================
// An inline <svg> whose subtree has not changed since its last paint is
// rasterised once into a document-owned offscreen surface and drawn as one
// image on the following frames, instead of re-walking its DOM, re-parsing
// every path and re-rasterising every shape. The DOM bumps
// DomElement::svg_layer_generation on any mutation under the root. A layer is
// captured only on the second consecutive paint of identical content, so an
// SVG that changes every frame never pays for a capture it would not reuse.

struct SvgLayerEntry {
    DomNodeRef element;             // the node id guards against a recycled element address
    uint32_t generation;            // svg_layer_generation last painted
    uint64_t document_epoch;        // host styles and cross-root resource mutations
    uint64_t interaction_generation; // live HTML control values, focus and foreignObject scrolling
    uint64_t animation_generation;  // SMIL samples change paint without changing DOM base attributes
    uint64_t font_generation;       // glyph cache lifetime changes
    uint64_t font_resource_generation; // descriptors and resolved font sources
    uint64_t image_resource_generation; // decoded/promoted pixels, GIF frames and ready network resources
    int pixel_width, pixel_height;  // layer size in physical pixels
    float scale;
    SvgInitialPaint paint;          // inherited paint is part of the rendered content
    ImageSurface* surface;          // null until the content was painted twice unchanged
    lam::Own<SvgLayerEntry> next;
};

// The registry and its entries live in one pool under the document's memory
// context; entries are only ever added, so the pool is released whole.
struct SvgLayerRegistry : DomDocumentResourceData {
    lam::Own<SvgLayerEntry> entries;
    size_t cached_bytes;
    lam::Own<Pool> pool;  // holds this registry and its entries
    lam::Up<DomDocument> document;
};

static const size_t SVG_LAYER_MAX_BYTES = (size_t)48 << 20;         // one layer
static const size_t SVG_LAYER_TOTAL_MAX_BYTES = (size_t)256 << 20;  // per document

enum SvgLayerMode { SVG_LAYER_MODE_ON = 0, SVG_LAYER_MODE_OFF, SVG_LAYER_MODE_EAGER };

// RADIANT_SVG_LAYER=off disables the cache; =eager captures on the first paint so a
// one-frame `lambda render` exercises the layer path and can be diffed against `off`.
static SvgLayerMode svg_layer_mode(void) {
    static int mode = -1;
    if (mode < 0) {
        const char* env = getenv("RADIANT_SVG_LAYER");
        mode = SVG_LAYER_MODE_ON;
        if (env && strcmp(env, "off") == 0) mode = SVG_LAYER_MODE_OFF;
        else if (env && strcmp(env, "eager") == 0) mode = SVG_LAYER_MODE_EAGER;
    }
    return (SvgLayerMode)mode;
}

static void svg_layer_entry_release_surface(SvgLayerRegistry* registry, SvgLayerEntry* entry) {
    if (!entry->surface) return;
    registry->cached_bytes -= (size_t)entry->surface->pitch * (size_t)entry->surface->height;
    image_surface_destroy(entry->surface);
    entry->surface = nullptr;
}

static void svg_layer_registry_destroy(DomDocumentResourceData* data) {
    // the document resource hands its registry over for teardown
    SvgLayerRegistry* registry = (SvgLayerRegistry*)data;
    if (!registry) return;
    for (SvgLayerEntry* entry = registry->entries; entry; entry = entry->next) {
        svg_layer_entry_release_surface(registry, entry);
    }
    // later resource destructors must not reach the freed registry (teardown audit F5)
    registry->document->services.svg_layer_registry = nullptr;
    // the registry itself lives in this pool
    mem_pool_destroy(registry->pool);
}

static SvgLayerRegistry* svg_layer_registry_for_document(DomDocument* document) {
    if (!document) return nullptr;
    SvgLayerRegistry* registry = (SvgLayerRegistry*)document->services.svg_layer_registry;
    if (registry) return registry;
    // the document takes ownership once its resource hook is registered
    MemContext* context = (MemContext*)document->services.mem_ctx;
    Pool* pool = mem_pool_create(context ? context : mem_context_process(MEM_ROLE_RENDER),
                                 MEM_ROLE_RENDER, "svg_layer.registry");
    if (!pool) return nullptr;
    registry = (SvgLayerRegistry*)pool_calloc(pool, sizeof(SvgLayerRegistry));
    if (!registry || !dom_document_add_resource(document, registry, svg_layer_registry_destroy)) {
        mem_pool_destroy(pool);
        return nullptr;
    }
    registry->pool = lam::own(pool);
    registry->document = lam::up(document);
    document->services.svg_layer_registry = registry;
    return registry;
}

static SvgLayerEntry* svg_layer_entry_for_element(SvgLayerRegistry* registry, DomElement* element) {
    for (SvgLayerEntry* entry = registry->entries; entry; entry = entry->next) {
        if (entry->element.address == (DomNode*)element) return entry;
    }
    SvgLayerEntry* entry = (SvgLayerEntry*)pool_calloc(registry->pool, sizeof(SvgLayerEntry));
    if (!entry) return nullptr;
    entry->element = dom_node_ref(element);
    entry->next = registry->entries;
    registry->entries = lam::own(entry);
    return entry;
}

static bool svg_layer_paint_equal(const SvgInitialPaint* a, const SvgInitialPaint* b) {
    return a->current_color.c == b->current_color.c &&
           a->has_fill_color == b->has_fill_color && a->fill_none == b->fill_none &&
           (!a->has_fill_color || a->fill_color.c == b->fill_color.c) &&
           a->has_stroke_color == b->has_stroke_color && a->stroke_none == b->stroke_none &&
           (!a->has_stroke_color || a->stroke_color.c == b->stroke_color.c) &&
           a->stroke_width == b->stroke_width;
}

// Rasterise the SVG at the layer origin into a straight-alpha surface. Every
// painter that shares the page surface (ThorVG fills, glyph blits, group
// opacity composites) assumes an opaque target, so the content is rendered
// twice, over black and over white, and the exact colour and coverage of each
// pixel are recovered from the difference. Each pass records its own display
// list: serial replay hands owned payloads such as pictures to the backend, so
// one recorded list cannot be replayed twice. Glyph runs record through the
// active RasterRenderContext, so its list, clip, transform and dirty state are pointed
// at the private layer while recording.
static bool svg_layer_render_pass(RasterRenderContext* rdcon, Element* svg_elem, DomElement* dom_elem,
                                  float viewport_width, float viewport_height, float scale,
                                  const SvgInitialPaint* paint, uint32_t* pixels,
                                  int width, int height, uint32_t backdrop) {
    for (size_t i = 0, n = (size_t)width * (size_t)height; i < n; i++) pixels[i] = backdrop;
    OffscreenRenderArenas arenas;
    if (!arenas.init("render.svg_layer", "render.svg_layer.list_arena",
                     "render.svg_layer.scratch_arena")) {
        return false;
    }
    DisplayList dl = {};
    PaintList paint_list = {};
    ScratchArena scratch = {};
    dl_init(&dl, arenas.list_arena);
    paint_list_init(&paint_list, nullptr);
    mem_scratch_init(NULL, &scratch, arenas.scratch_arena, MEM_ROLE_RENDER, "render.svg_layer.scratch");

    DisplayList* saved_dl = rdcon->dl;
    PaintList* saved_paint_list = rdcon->paint_list;
    Bound saved_clip = rdcon->block.clip;
    int saved_vector_clip_depth = rdcon->vector_clip_depth;
    Bound saved_vector_clip_bounds[RDT_MAX_CLIP_SHAPES];
    memcpy(saved_vector_clip_bounds, rdcon->vector_clip_bounds, sizeof(saved_vector_clip_bounds));
    RdtMatrix saved_transform = rdcon->transform;
    bool saved_has_transform = rdcon->has_transform;
    DirtyTracker* saved_dirty_tracker = rdcon->dirty_tracker;
    bool saved_has_dirty_union = rdcon->has_dirty_union;
    rdcon->dl = lam::up(&dl);
    rdcon->paint_list = lam::up(&paint_list);
    rdcon->block.clip = {0.0f, 0.0f, (float)width, (float)height};
    // the isolated layer starts a new clip coordinate space.
    rdcon->vector_clip_depth = 0;
    rdcon->has_transform = false;
    rdcon->dirty_tracker = nullptr;
    rdcon->has_dirty_union = false;

    RdtMatrix base_transform = { scale, 0.0f, 0.0f, 0.0f, scale, 0.0f, 0.0f, 0.0f, 1.0f };
    FontContext* font_ctx = rdcon->ui_context ? rdcon->ui_context->font_ctx : nullptr;
    render_svg_to_display_list(svg_elem, viewport_width, viewport_height,
                               rdcon->ui_context->document->document_pool, scale,
                               font_ctx, &base_transform, &dl, &paint->current_color,
                               paint->has_fill_color ? &paint->fill_color : nullptr,
                               nullptr, 1.0f, paint->fill_none,
                               paint->has_stroke_color ? &paint->stroke_color : nullptr,
                               paint->stroke_none, paint->stroke_width,
                               &paint_list, &scratch, render_svg_reference_scope(dom_elem));

    rdcon->dl = lam::up(saved_dl);
    rdcon->paint_list = lam::up(saved_paint_list);
    rdcon->block.clip = saved_clip;
    rdcon->vector_clip_depth = saved_vector_clip_depth;
    memcpy(rdcon->vector_clip_bounds, saved_vector_clip_bounds, sizeof(saved_vector_clip_bounds));
    rdcon->transform = saved_transform;
    rdcon->has_transform = saved_has_transform;
    rdcon->dirty_tracker = lam::up(saved_dirty_tracker);
    rdcon->has_dirty_union = saved_has_dirty_union;

    bool ok = dl_validate_or_log(&dl, "render_svg_layer_capture");
    if (ok) {
        ImageSurface pass = {};
        pass.format = IMAGE_FORMAT_UNKNOWN;
        pass.width = width;
        pass.height = height;
        pass.encoded_width = width;
        pass.encoded_height = height;
        pass.orientation = 1;
        pass.has_intrinsic_size = true;
        pass.pitch = width * 4;
        pass.pixels = pixels;
        RdtVector vec = {};
        rdt_vector_init(&vec, pixels, width, height, width);
        ok = vec.impl != nullptr;
        if (ok) {
            Bound clip = {0.0f, 0.0f, (float)width, (float)height};
            dl_replay(&dl, &vec, &pass, &clip, &scratch, scale, nullptr);
            rdt_vector_destroy(&vec);
        }
    }
    scratch_release(&scratch);
    paint_list_destroy(&paint_list);
    dl_destroy(&dl);
    arenas.destroy();
    return ok;
}

// straight RGBA from the black and white passes: coverage is what the white
// backdrop lost, colour is the black pass scaled back up by that coverage
static void svg_layer_resolve_alpha(const uint32_t* over_black, const uint32_t* over_white,
                                    uint32_t* out, size_t count) {
    for (size_t i = 0; i < count; i++) {
        const uint8_t* b = (const uint8_t*)&over_black[i];
        const uint8_t* w = (const uint8_t*)&over_white[i];
        uint32_t lost = (uint32_t)(w[0] - b[0]) + (uint32_t)(w[1] - b[1]) + (uint32_t)(w[2] - b[2]);
        uint32_t alpha = 255u - (lost + 1u) / 3u;
        uint8_t* o = (uint8_t*)&out[i];
        if (alpha == 0) {
            out[i] = 0;
            continue;
        }
        for (int c = 0; c < 3; c++) {
            uint32_t value = ((uint32_t)b[c] * 255u + alpha / 2u) / alpha;
            o[c] = (uint8_t)(value > 255u ? 255u : value);
        }
        o[3] = (uint8_t)alpha;
    }
}

static bool svg_layer_capture(RasterRenderContext* rdcon, SvgLayerRegistry* registry,
                              SvgLayerEntry* entry, Element* svg_elem, DomElement* dom_elem,
                              float viewport_width, float viewport_height, float scale,
                              const SvgInitialPaint* paint) {
    int width = entry->pixel_width;
    int height = entry->pixel_height;
    size_t count = (size_t)width * (size_t)height;
    size_t bytes = count * 4u;
    if (bytes > SVG_LAYER_MAX_BYTES || registry->cached_bytes + bytes > SVG_LAYER_TOTAL_MAX_BYTES) {
        return false;
    }
    ImageSurface* surface = image_surface_create(width, height);
    if (!surface) return false;
    lam::Temp<uint32_t> over_black = lam::temp_array<uint32_t>(count, MEM_CAT_IMAGE);
    lam::Temp<uint32_t> over_white = lam::temp_array<uint32_t>(count, MEM_CAT_IMAGE);
    bool ok = over_black && over_white &&
        svg_layer_render_pass(rdcon, svg_elem, dom_elem, viewport_width, viewport_height, scale,
                              paint, over_black.get(), width, height, 0xFF000000u) &&
        svg_layer_render_pass(rdcon, svg_elem, dom_elem, viewport_width, viewport_height, scale,
                              paint, over_white.get(), width, height, 0xFFFFFFFFu);
    if (ok) svg_layer_resolve_alpha(over_black.get(), over_white.get(),
                                    (uint32_t*)surface->pixels, count);
    over_black.reset();
    over_white.reset();
    if (!ok) {
        image_surface_destroy(surface);
        return false;
    }
    surface->alpha_mode = IMAGE_ALPHA_STRAIGHT;
    entry->surface = surface;
    registry->cached_bytes += bytes;
    log_debug("svg-layer: captured %dx%d layer for <svg> node=%u generation=%u", width, height,
              dom_elem->DomNode::id, entry->generation);
    return true;
}

// A layer is blitted by the CPU painter, which has no transform. An
// axis-aligned uniform scale plus translation (a CSS drift on a decorative
// layer, a fitted presentation stage) folds into the capture resolution and
// the destination; rotation, skew, flips and non-uniform scale disqualify it.
static bool svg_layer_device_transform(const RasterRenderContext* rdcon, float* sx, float* dx, float* dy) {
    *sx = 1.0f;
    *dx = 0.0f;
    *dy = 0.0f;
    if (!rdcon->has_transform) return true;
    const RdtMatrix* m = &rdcon->transform;
    if (m->e11 <= 0.0f || fabsf(m->e11 - m->e22) > 0.0005f ||
        fabsf(m->e12) > 0.0005f || fabsf(m->e21) > 0.0005f) {
        return false;
    }
    *sx = m->e11;
    *dx = m->e13;
    *dy = m->e23;
    return true;
}

// Paint the SVG from its cached layer when its content is unchanged. Returns
// false when the caller must record the SVG directly: the first paint of new
// content (which also arms the capture), a layer that is too large, or a
// transform the blit cannot express.
static bool svg_layer_paint(RasterRenderContext* rdcon, DomElement* dom_elem, Element* svg_elem,
                            const Rect* content_rect, float viewport_width,
                            float viewport_height, float scale, const SvgInitialPaint* paint) {
    SvgLayerMode mode = svg_layer_mode();
    if (mode == SVG_LAYER_MODE_OFF || !rdcon->ui_context || !rdcon->ui_context->document) {
        return false;
    }
    float device_scale, offset_x, offset_y;
    if (!svg_layer_device_transform(rdcon, &device_scale, &offset_x, &offset_y)) return false;
    // the layer is rasterised at the scale it lands on the surface
    float layer_scale = scale * device_scale;
    int width = (int)lroundf(content_rect->width * device_scale);   // INT_CAST_OK: layer pixel size
    int height = (int)lroundf(content_rect->height * device_scale); // INT_CAST_OK: layer pixel size
    if (width <= 0 || height <= 0 || (size_t)width * (size_t)height * 4u > SVG_LAYER_MAX_BYTES) {
        return false;
    }
    SvgLayerRegistry* registry = svg_layer_registry_for_document(rdcon->ui_context->document);
    SvgLayerEntry* entry = registry ? svg_layer_entry_for_element(registry, dom_elem) : nullptr;
    if (!entry) return false;

    uint32_t generation = dom_elem->svg_layer_generation;
    // presentation writes of non-inherited values elsewhere cannot change this raster
    uint64_t document_epoch = rdcon->ui_context->document->style_content_epoch;
    DocState* state = rdcon->ui_context->document->state;
    // repaint/reflow bookkeeping is not an interaction change
    uint64_t interaction_generation = doc_state_content_version(state);
    uint64_t animation_generation = svg_animation_generation(rdcon->ui_context->document);
    uint64_t font_generation = font_context_glyph_cache_generation(rdcon->ui_context->font_ctx);
    uint64_t font_resource_generation = font_context_resource_generation(rdcon->ui_context->font_ctx);
    uint64_t image_generation = image_cache_resource_generation(rdcon->ui_context);
    bool unchanged = entry->image_resource_generation == image_generation && entry->element.expected_id == dom_elem->DomNode::id && entry->generation == generation &&
                     entry->document_epoch == document_epoch && entry->interaction_generation == interaction_generation &&
                     entry->animation_generation == animation_generation &&
                     entry->font_generation == font_generation &&
                     entry->font_resource_generation == font_resource_generation &&
                     entry->pixel_width == width && entry->pixel_height == height &&
                     entry->scale == layer_scale && svg_layer_paint_equal(&entry->paint, paint);
    if (!unchanged) {
        svg_layer_entry_release_surface(registry, entry);
        entry->element = dom_node_ref(dom_elem);
        entry->generation = generation;
        entry->document_epoch = document_epoch;
        entry->interaction_generation = interaction_generation;
        entry->animation_generation = animation_generation;
        entry->font_generation = font_generation;
        entry->font_resource_generation = font_resource_generation;
        entry->image_resource_generation = image_generation;
        entry->pixel_width = width;
        entry->pixel_height = height;
        entry->scale = layer_scale;
        entry->paint = *paint;
        if (mode != SVG_LAYER_MODE_EAGER) return false;
    }
    if (!entry->surface &&
        !svg_layer_capture(rdcon, registry, entry, svg_elem, dom_elem,
                           viewport_width, viewport_height, layer_scale, paint)) {
        return false;
    }
    // capture can resolve a font source; key the resulting paint by that resource.
    entry->interaction_generation = doc_state_content_version(state);
    entry->font_generation = font_context_glyph_cache_generation(rdcon->ui_context->font_ctx);
    entry->font_resource_generation = font_context_resource_generation(rdcon->ui_context->font_ctx);
    entry->image_resource_generation = image_cache_resource_generation(rdcon->ui_context);
    ImageSurface* surface = entry->surface;
    // whole device pixels, so the blit copies samples instead of resampling
    Rect dst = { roundf(content_rect->x * device_scale + offset_x),
                 roundf(content_rect->y * device_scale + offset_y),
                 (float)surface->width, (float)surface->height };
    Bound clip = view_geometry_intersect_bound_rect(rdcon->block.clip, dst);
    // the cached layer already includes the device transform; vector-backed
    // blits must not apply the ancestor translation/scale a second time.
    bool saved_has_transform = rdcon->has_transform;
    rdcon->has_transform = false;
    render_painter_blit_surface_scaled(rdcon, surface, nullptr, rdcon->ui_context->surface,
                                       &dst, &clip, SCALE_MODE_NEAREST, rdcon->clip_shapes,
                                       rdcon->clip_shape_depth, 255);
    rdcon->has_transform = saved_has_transform;
    return true;
}

// ============================================================================
// Render Inline SVG
// ============================================================================

Element* render_svg_reference_scope(DomElement* svg_element) {
    if (!svg_element) return nullptr;
    // getElementById semantics: the reference resolves in the node's tree,
    // whose root is the document element for a connected <svg>.
    DomNode* root = svg_element;
    while (root->parent) root = root->parent;
    return root->is_element() ? dom_element_backing(root->as_element()) : nullptr;
}

void render_inline_svg(RasterRenderContext* rdcon, ViewBlock* view) {
    if (!rdcon || !view) return;

    DomElement* dom_elem = lam::dom_require_element(lam::view_dom_node(view));
    if (dom_elem->is_synthetic()) {
        return;
    }

    float scale = rdcon->raster_scale;

    Rect content_rect = render_geometry_block_content_rect(&rdcon->block, view, scale);
    float viewport_width = scale > 0.0f ? content_rect.width / scale : content_rect.width;
    float viewport_height = scale > 0.0f ? content_rect.height / scale : content_rect.height;
    if (viewport_width <= 0.0f || viewport_height <= 0.0f) {
        return;
    }

    char overflow_buffer[64];
    const char* overflow = svg_get_dom_presentation_property(dom_elem, "overflow", false,
        overflow_buffer, sizeof(overflow_buffer));
    bool viewport_clip = !overflow || strcmp(overflow, "visible") != 0;
    // visible SVG ink may extend beyond a viewport that lies outside the paint clip.
    if (viewport_clip && !rdcon->has_transform &&
        !transform_has_functions(view->transform) &&
        !view_geometry_bounds_intersect(view_geometry_rect_to_bound(content_rect), rdcon->block.clip)) return;

    Element* svg_elem = dom_element_to_element(dom_elem);

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
    bool has_content_clip = viewport_clip && content_rect.width > 0.0f && content_rect.height > 0.0f;
    if (has_content_clip) {
        RdtPath* clip_path = rdt_path_new();
        rdt_path_add_rect(clip_path, content_rect.x, content_rect.y,
                          content_rect.width, content_rect.height, 0, 0);
        // the SVG viewport moves with its CSS ancestors, like its painted content
        rc_push_clip(rdcon, clip_path, render_state_current_transform(rdcon));
        rdt_path_free(clip_path);
    }

    // render SVG through the shared painter gateway
    FontContext* font_ctx = rdcon->ui_context ? rdcon->ui_context->font_ctx : nullptr;
    SvgInitialPaint initial_paint;
    render_svg_initial_paint(view, rdcon->color, &initial_paint);
    RasterRenderContext* saved_svg_rdcon = g_svg_active_rdcon;
    g_svg_active_rdcon = rdcon;
    // unchanged content comes from its cached raster layer; otherwise record directly
    if (!viewport_clip || !svg_layer_paint(rdcon, dom_elem, svg_elem, &content_rect, viewport_width,
                         viewport_height, scale, &initial_paint)) {
        render_svg_to_display_list(svg_elem, viewport_width, viewport_height,
                                   rdcon->ui_context->document->document_pool, scale,
                                   font_ctx, &base_transform, rdcon->dl,
                                   &initial_paint.current_color,
                                   initial_paint.has_fill_color ? &initial_paint.fill_color : nullptr,
                                   nullptr, 1.0f, initial_paint.fill_none,
                                   initial_paint.has_stroke_color ? &initial_paint.stroke_color : nullptr,
                                   initial_paint.stroke_none, initial_paint.stroke_width,
                                   rdcon->paint_list, &rdcon->scratch,
                                   render_svg_reference_scope(dom_elem));
    }
    g_svg_active_rdcon = saved_svg_rdcon;

    if (has_content_clip) {
        rc_pop_clip(rdcon);
    }
    if (has_clip) {
        rc_pop_clip(rdcon);
    }

}

void render_custom_svg_subscene(RasterRenderContext* rdcon, Element* svg_element,
                                float viewport_width, float viewport_height) {
    if (!rdcon || !svg_element || viewport_width <= 0.0f || viewport_height <= 0.0f ||
        !rdcon->dl || !rdcon->paint_list) {
        return;
    }
    float scale = rdcon->raster_scale > 0.0f ? rdcon->raster_scale : 1.0f;
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
        ? rdcon->ui_context->document->document_pool : nullptr;
    Color current_color = rdcon->color;
    // generated SVG layers must retain the ancestor overflow clip during paint.
    RenderClipScope clip_scope = render_clip_push_rect_scope(rdcon, &rdcon->block.clip);
    RasterRenderContext* saved_svg_rdcon = g_svg_active_rdcon;
    g_svg_active_rdcon = rdcon;
    render_svg_to_display_list(svg_element, viewport_width, viewport_height,
                               pool, scale, font_ctx, &base_transform, rdcon->dl,
                               &current_color, nullptr, nullptr, 1.0f, false,
                               nullptr, true, -1.0f, rdcon->paint_list,
                               &rdcon->scratch, nullptr);
    g_svg_active_rdcon = saved_svg_rdcon;
    render_clip_pop_scope(rdcon, &clip_scope);
}
