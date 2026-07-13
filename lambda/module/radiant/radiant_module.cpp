#include "../../jube/jube_registry.h"
#include "../../input/css/dom_element.hpp"
#include "radiant_host_api.hpp"
#include "radiant_dom_bridge.hpp"
#include "../../../radiant/layout_custom.hpp"
#include "../../../lib/log.h"
#include "../../../lib/mem_context.h"
#include "../../../lib/mem_factory.h"
#include "../../../lib/mempool.h"
#include "../../../lib/url.h"
#include <string.h>

extern DomDocument* load_lambda_html_doc(Url* html_url, const char* css_filename,
    int viewport_width, int viewport_height, Pool* pool, const char* html_source,
    bool track_source_lines, bool execute_scripts);
extern void free_document(DomDocument* doc);
RADIANT_C_API Item radiant_dom_wrap_node(void* dom_elem);
RADIANT_C_API void* radiant_dom_unwrap_node(Item item);
RADIANT_C_API int radiant_dom_host_get_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_host_set_property(Item object, Item key, Item value, Item* out);
RADIANT_C_API int radiant_dom_host_call_method(Item object, Item method_name,
                                            Item* args, int argc, Item* out);
RADIANT_C_API int radiant_dom_host_has_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_host_delete_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_host_own_property_descriptor(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_host_own_property_names(Item object, Item* out);
RADIANT_C_API Item radiant_dom_host_prototype(Item object);
RADIANT_C_API void radiant_dom_host_invalidate(Item object);
RADIANT_C_API int radiant_dom_document_host_get_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_document_host_set_property(Item object, Item key, Item value, Item* out);
RADIANT_C_API int radiant_dom_document_host_call_method(Item object, Item method_name,
                                                     Item* args, int argc, Item* out);
RADIANT_C_API int radiant_dom_document_host_has_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_document_host_delete_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_document_host_own_property_descriptor(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_document_host_own_property_names(Item object, Item* out);
RADIANT_C_API Item radiant_dom_document_host_prototype(Item object);

const JubeHostAPI* radiant_host_api = nullptr;

#define RADIANT_CUSTOM_LAYOUT_MAX_REGISTRY 64

typedef struct RadiantCustomLayoutEntry {
    const char* name;
    Item fn;
    bool rooted;
} RadiantCustomLayoutEntry;

static RadiantCustomLayoutEntry g_radiant_custom_layouts[RADIANT_CUSTOM_LAYOUT_MAX_REGISTRY];
static int g_radiant_custom_layout_count = 0;

static Item radiant_string_item(const char* value) {
    return value ? (Item){.item = s2it(heap_create_name(value))} : ItemNull;
}

static Item radiant_int_item(int64_t value) {
    return (Item){.item = i2it(value)};
}

static Item radiant_bool_item(bool value) {
    return (Item){.item = b2it(value ? 1 : 0)};
}

static Item radiant_float_item(double value) {
    double* ptr = (double*)heap_calloc(sizeof(double), LMD_TYPE_FLOAT);
    if (!ptr) return ItemNull;
    *ptr = value;
    return (Item){.item = d2it(ptr)};
}

static Item radiant_key_item(const char* key) {
    return radiant_string_item(key);
}

static void radiant_obj_set(Item obj, const char* key, Item value) {
    if (!radiant_host_api || !radiant_host_api->value || !key) return;
    radiant_host_api->value->property_set(obj, radiant_key_item(key), value);
}

static void radiant_obj_set_optional_float(Item obj, const char* key, float value) {
    radiant_obj_set(obj, key, value >= 0.0f ? radiant_float_item(value) : ItemNull);
}

static Item radiant_obj_get(Item obj, const char* key) {
    if (!radiant_host_api || !radiant_host_api->value || !key) return ItemNull;
    return radiant_host_api->value->property_get(obj, radiant_key_item(key));
}

static bool radiant_item_to_float(Item item, float* out) {
    if (!out) return false;
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 ||
        type == LMD_TYPE_FLOAT || type == LMD_TYPE_FLOAT64 ||
        type == LMD_TYPE_NUM_SIZED || type == LMD_TYPE_UINT64) {
        *out = (float)it2d(item);
        return true;
    }
    return false;
}

static bool radiant_item_to_index(Item item, int* out) {
    if (!out) return false;
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 ||
        type == LMD_TYPE_FLOAT || type == LMD_TYPE_FLOAT64 ||
        type == LMD_TYPE_NUM_SIZED || type == LMD_TYPE_UINT64) {
        *out = (int)it2i(item);
        return true;
    }
    return false;
}

static Item radiant_layout_edges_item(const VelmtEdges* edges) {
    if (!radiant_host_api || !radiant_host_api->value || !edges) return ItemNull;
    Item obj = radiant_host_api->value->new_object();
    radiant_obj_set(obj, "left", radiant_float_item(edges->left));
    radiant_obj_set(obj, "right", radiant_float_item(edges->right));
    radiant_obj_set(obj, "top", radiant_float_item(edges->top));
    radiant_obj_set(obj, "bottom", radiant_float_item(edges->bottom));
    return obj;
}

static Item radiant_layout_box_item(const VelmtBox* box) {
    if (!radiant_host_api || !radiant_host_api->value || !box) return ItemNull;
    Item obj = radiant_host_api->value->new_object();
    radiant_obj_set(obj, "x", radiant_float_item(box->x));
    radiant_obj_set(obj, "y", radiant_float_item(box->y));
    radiant_obj_set(obj, "width", radiant_float_item(box->width));
    radiant_obj_set(obj, "height", radiant_float_item(box->height));
    return obj;
}

static Item radiant_layout_attrs_item(DomElement* elem) {
    if (!radiant_host_api || !radiant_host_api->value || !elem) return ItemNull;
    Item attrs = radiant_host_api->value->new_object();
    int attr_count = 0;
    const char** names = dom_element_get_attribute_names(elem, &attr_count);
    for (int i = 0; names && i < attr_count; i++) {
        const char* name = names[i];
        if (!name) continue;
        const char* value = dom_element_get_attribute(elem, name);
        radiant_obj_set(attrs, name, radiant_string_item(value ? value : ""));
    }
    return attrs;
}

static Item radiant_layout_velmt_item(const Velmt* velmt) {
    if (!radiant_host_api || !radiant_host_api->value || !velmt) return ItemNull;
    Item obj = radiant_host_api->value->new_object();
    const char* tag = velmt->view ? velmt->view->node_name() : "";
    radiant_obj_set(obj, "index", radiant_int_item(velmt->index));
    radiant_obj_set(obj, "tag", radiant_string_item(tag));
    radiant_obj_set(obj, "width", radiant_float_item(velmt->border_box.width));
    radiant_obj_set(obj, "height", radiant_float_item(velmt->border_box.height));
    radiant_obj_set(obj, "wd", radiant_float_item(velmt->border_box.width));
    radiant_obj_set(obj, "hg", radiant_float_item(velmt->border_box.height));
    radiant_obj_set(obj, "box", radiant_layout_box_item(&velmt->border_box));
    radiant_obj_set(obj, "margin", radiant_layout_edges_item(&velmt->margin));
    radiant_obj_set(obj, "border", radiant_layout_edges_item(&velmt->border));
    radiant_obj_set(obj, "padding", radiant_layout_edges_item(&velmt->padding));
    if (velmt->element) {
        radiant_obj_set(obj, "id", radiant_string_item(velmt->element->id));
        radiant_obj_set(obj, "attrs", radiant_layout_attrs_item(velmt->element));
    } else {
        radiant_obj_set(obj, "id", ItemNull);
        radiant_obj_set(obj, "attrs", ItemNull);
    }
    return obj;
}

static Item radiant_layout_parent_item(const CustomLayoutContext* context) {
    if (!radiant_host_api || !radiant_host_api->value || !context || !context->parent) return ItemNull;
    Velmt parent;
    memset(&parent, 0, sizeof(parent));
    parent.view = (View*)context->parent;
    parent.element = context->parent;
    parent.index = -1;
    parent.border_box.x = 0.0f;
    parent.border_box.y = 0.0f;
    parent.border_box.width = context->parent->width;
    parent.border_box.height = context->parent->height;
    if (context->parent) {
        BoxMetrics metrics = layout_box_metrics(context->parent);
        parent.margin.left = metrics.margin.left;
        parent.margin.right = metrics.margin.right;
        parent.margin.top = metrics.margin.top;
        parent.margin.bottom = metrics.margin.bottom;
        parent.border.left = metrics.border.left;
        parent.border.right = metrics.border.right;
        parent.border.top = metrics.border.top;
        parent.border.bottom = metrics.border.bottom;
        parent.padding.left = metrics.padding.left;
        parent.padding.right = metrics.padding.right;
        parent.padding.top = metrics.padding.top;
        parent.padding.bottom = metrics.padding.bottom;
    }
    return radiant_layout_velmt_item(&parent);
}

static Item radiant_layout_children_item(const CustomLayoutContext* context) {
    if (!radiant_host_api || !radiant_host_api->value || !context) return ItemNull;
    Item arr = radiant_host_api->value->array_new(context->child_count);
    for (int i = 0; i < context->child_count; i++) {
        radiant_host_api->value->array_push(arr, radiant_layout_velmt_item(&context->children[i]));
    }
    return arr;
}

static Item radiant_layout_context_item(const CustomLayoutContext* context) {
    if (!radiant_host_api || !radiant_host_api->value || !context) return ItemNull;
    Item obj = radiant_host_api->value->new_object();
    radiant_obj_set(obj, "layout_name", radiant_string_item(context->layout_name));
    radiant_obj_set(obj, "available_width", radiant_float_item(context->available_width));
    radiant_obj_set(obj, "available_height", radiant_float_item(context->available_height));
    radiant_obj_set_optional_float(obj, "css_width", context->css_width);
    radiant_obj_set_optional_float(obj, "css_height", context->css_height);
    radiant_obj_set(obj, "direction", radiant_string_item(
        context->direction == CSS_VALUE_RTL ? "rtl" : "ltr"));
    radiant_obj_set(obj, "writing_mode", radiant_string_item(
        context->writing_mode ? context->writing_mode : "horizontal-tb"));
    radiant_obj_set(obj, "child_count", radiant_int_item(context->child_count));
    return obj;
}

static RadiantCustomLayoutEntry* radiant_custom_layout_entry(const char* name) {
    if (!name || name[0] == '\0') return nullptr;
    for (int i = 0; i < g_radiant_custom_layout_count; i++) {
        if (strcmp(g_radiant_custom_layouts[i].name, name) == 0) {
            return &g_radiant_custom_layouts[i];
        }
    }
    return nullptr;
}

static bool radiant_custom_layout_parse_result(const CustomLayoutContext* context,
                                               Item result_item,
                                               CustomLayoutResult* result) {
    if (!context || !result) return false;
    Item width_item = radiant_obj_get(result_item, "width");
    float width = 0.0f;
    if (!radiant_item_to_float(width_item, &width)) {
        width_item = radiant_obj_get(result_item, "wd");
    }
    if (radiant_item_to_float(width_item, &width)) {
        result->width = width;
        result->has_width = true;
    }
    Item height_item = radiant_obj_get(result_item, "height");
    float height = 0.0f;
    if (!radiant_item_to_float(height_item, &height)) {
        height_item = radiant_obj_get(result_item, "hg");
    }
    if (radiant_item_to_float(height_item, &height)) {
        result->height = height;
        result->has_height = true;
    }

    Item placements = radiant_obj_get(result_item, "placements");
    if (get_type_id(placements) != LMD_TYPE_ARRAY || !placements.array) {
        log_error("CUSTOM_LAYOUT_LAMBDA_RESULT: result.placements must be an array");
        return false;
    }

    bool ok = true;
    for (int i = 0; i < placements.array->length; i++) {
        Item placement = placements.array->items[i];
        Item index_item = radiant_obj_get(placement, "index");
        int child_index = -1;
        if (!radiant_item_to_index(index_item, &child_index)) {
            index_item = radiant_obj_get(placement, "child_index");
            radiant_item_to_index(index_item, &child_index);
        }
        if (child_index < 0) {
            Item child = radiant_obj_get(placement, "child");
            Item child_index_item = radiant_obj_get(child, "index");
            radiant_item_to_index(child_index_item, &child_index);
        }

        float x = 0.0f;
        float y = 0.0f;
        bool has_x = radiant_item_to_float(radiant_obj_get(placement, "x"), &x);
        bool has_y = radiant_item_to_float(radiant_obj_get(placement, "y"), &y);
        if (!has_x || !has_y || child_index < 0 || child_index >= context->child_count ||
            !custom_layout_result_place(result, child_index, x, y)) {
            log_error("CUSTOM_LAYOUT_LAMBDA_PLACEMENT: invalid placement at index %d", i);
            ok = false;
        }
    }
    return ok;
}

static bool radiant_lambda_custom_layout_callback(const CustomLayoutContext* context,
                                                  CustomLayoutResult* result) {
    if (!radiant_host_api || !radiant_host_api->script || !context || !result) return false;
    RadiantCustomLayoutEntry* entry = radiant_custom_layout_entry(context->layout_name);
    if (!entry || get_type_id(entry->fn) != LMD_TYPE_FUNC) {
        log_error("CUSTOM_LAYOUT_LAMBDA_MISSING_FN: layout='%s'",
                  context && context->layout_name ? context->layout_name : "(null)");
        return false;
    }

    Item args[3];
    args[0] = radiant_layout_parent_item(context);
    args[1] = radiant_layout_children_item(context);
    args[2] = radiant_layout_context_item(context);
    Item result_item = radiant_host_api->script->call_function(entry->fn, ItemNull, args, 3);
    if (radiant_host_api->script->check_exception()) {
        log_error("CUSTOM_LAYOUT_LAMBDA_EXCEPTION: layout='%s'", context->layout_name);
        return false;
    }
    return radiant_custom_layout_parse_result(context, result_item, result);
}

static DomDocument* radiant_dom_document_from_node(DomNode* node) {
    DomNode* current = node;
    while (current) {
        if (current->is_element()) {
            DomElement* elem = current->as_element();
            if (elem && elem->doc) return elem->doc;
        }
        current = current->parent;
    }
    return nullptr;
}

static DomNode* radiant_dom_node_from_item(Item node_item, const char* func_name) {
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(node_item);
    if (!node) {
        log_error("JUBE_RADIANT_%s: expected DOM node wrapper", func_name);
    }
    return node;
}

static DomElement* radiant_dom_element_from_item(Item node_item, const char* func_name) {
    DomNode* node = radiant_dom_node_from_item(node_item, func_name);
    if (!node) return nullptr;
    if (!node->is_element()) {
        log_error("JUBE_RADIANT_%s: expected DOM element wrapper", func_name);
        return nullptr;
    }
    return node->as_element();
}

static DomDocument* radiant_load_html_document(const char* path, const char* func_name) {
    if (!path || !path[0]) {
        log_error("JUBE_RADIANT_%s: missing HTML path", func_name);
        return nullptr;
    }

    Url* cwd = get_current_dir();
    Url* html_url = parse_url(cwd, path);
    if (cwd) url_destroy(cwd);

    Pool* doc_pool = mem_pool_create(NULL, MEM_ROLE_LAYOUT, "radiant.document");
    if (!html_url || !doc_pool) {
        log_error("JUBE_RADIANT_%s: failed to create document inputs for '%s'", func_name, path);
        if (html_url) url_destroy(html_url);
        if (doc_pool) pool_destroy(doc_pool);
        return nullptr;
    }

    DomDocument* doc = load_lambda_html_doc(html_url, NULL, 800, 600, doc_pool, nullptr, false, false);
    if (!doc) {
        log_error("JUBE_RADIANT_%s: failed to load HTML document '%s'", func_name, path);
        url_destroy(html_url);
        pool_destroy(doc_pool);
        return nullptr;
    }

    if (!doc->root) {
        log_error("JUBE_RADIANT_%s: document '%s' has no root element", func_name, path);
        free_document(doc);
        return nullptr;
    }
    return doc;
}

RADIANT_C_API Item fn_radiant_load(Item path_item) {
    DomDocument* doc = radiant_load_html_document(fn_to_cstr(path_item), "LOAD");
    if (!doc || !doc->root) return ItemNull;
    // The POC exposes the document through its root wrapper until document
    // wrappers become a first-class native type in the VMap phase.
    return radiant_dom_wrap_node(doc->root);
}

RADIANT_C_API Item fn_radiant_root(Item doc_item) {
    DomNode* node = radiant_dom_node_from_item(doc_item, "ROOT");
    if (!node) return ItemNull;
    DomDocument* doc = radiant_dom_document_from_node(node);
    if (!doc || !doc->root) {
        log_error("JUBE_RADIANT_ROOT: DOM node has no owning root document");
        return ItemNull;
    }
    return radiant_dom_wrap_node(doc->root);
}

RADIANT_C_API Item fn_radiant_attr(Item node_item, Item name_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "ATTR");
    const char* name = fn_to_cstr(name_item);
    if (!elem || !name || !name[0]) return ItemNull;
    return radiant_string_item(dom_element_get_attribute(elem, name));
}

RADIANT_C_API Item fn_radiant_set_attr(Item node_item, Item name_item, Item value_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "SET_ATTR");
    const char* name = fn_to_cstr(name_item);
    const char* value = fn_to_cstr(value_item);
    if (!elem || !name || !name[0] || !value) return ItemNull;
    // Attribute writes from Lambda must share JS DOM side effects such as
    // event-attribute compilation, selection refresh, and mutation notices.
    Item args[2] = {name_item, value_item};
    Item method = (Item){.item = s2it(heap_create_name("setAttribute"))};
    radiant_dom_element_method(node_item, method, args, 2);
    return node_item;
}

RADIANT_C_API Item fn_radiant_free(Item node_item) {
    DomNode* node = radiant_dom_node_from_item(node_item, "FREE");
    if (!node) return ItemNull;
    DomDocument* doc = radiant_dom_document_from_node(node);
    if (!doc) {
        log_error("JUBE_RADIANT_FREE: DOM node has no owning document");
        return ItemNull;
    }
    free_document(doc);
    return ItemNull;
}

RADIANT_C_API Item fn_radiant_poc_attr(Item path_item) {
    DomDocument* doc = radiant_load_html_document(fn_to_cstr(path_item), "POC");
    if (!doc || !doc->root) return ItemNull;

    dom_element_set_attribute(doc->root, "data-poc", "ok");
    Item result = radiant_string_item(dom_element_get_attribute(doc->root, "data-poc"));
    free_document(doc);
    return result;
}

RADIANT_C_API Item fn_radiant_register_layout(Item name_item, Item fn_item) {
    const char* name = fn_to_cstr(name_item);
    if (!name || name[0] == '\0' || get_type_id(fn_item) != LMD_TYPE_FUNC) {
        log_error("JUBE_RADIANT_REGISTER_LAYOUT: expected name and fn callback");
        return radiant_bool_item(false);
    }
    if (!radiant_host_api || !radiant_host_api->gc) {
        log_error("JUBE_RADIANT_REGISTER_LAYOUT: radiant host API not initialized");
        return radiant_bool_item(false);
    }

    RadiantCustomLayoutEntry* entry = radiant_custom_layout_entry(name);
    if (!entry) {
        if (g_radiant_custom_layout_count >= RADIANT_CUSTOM_LAYOUT_MAX_REGISTRY) {
            log_error("JUBE_RADIANT_REGISTER_LAYOUT: registry full for '%s'", name);
            return radiant_bool_item(false);
        }
        entry = &g_radiant_custom_layouts[g_radiant_custom_layout_count];
        memset(entry, 0, sizeof(*entry));
        entry->name = heap_create_name(name)->chars;
        entry->fn = ItemNull;
        radiant_host_api->gc->register_root(&entry->fn.item);
        entry->rooted = true;
        g_radiant_custom_layout_count++;
    }

    entry->fn = fn_item;
    if (!custom_layout_register(entry->name, radiant_lambda_custom_layout_callback)) {
        log_error("JUBE_RADIANT_REGISTER_LAYOUT: native registry failed for '%s'", entry->name);
        return radiant_bool_item(false);
    }
    log_info("JUBE_RADIANT_REGISTER_LAYOUT: registered custom layout '%s'", entry->name);
    return radiant_bool_item(true);
}

static int radiant_module_init(const JubeHostAPI* host) {
    if (!host || host->api_version != JUBE_ABI_VERSION ||
        !host->gc || !host->value || !host->script || !host->dom) {
        log_error("JUBE_RADIANT: missing host API during module init");
        return -1;
    }
    radiant_host_api = host;
    log_info("JUBE_RADIANT: static radiant module initialized");
    return 0;
}

extern const JubeHostObjectOps radiant_dom_node_host_ops;
const JubeHostObjectOps radiant_dom_node_host_ops = {
    radiant_dom_host_get_property,
    radiant_dom_host_set_property,
    radiant_dom_host_call_method,
    radiant_dom_host_has_property,
    radiant_dom_host_delete_property,
    radiant_dom_host_own_property_descriptor,
    radiant_dom_host_own_property_names,
    radiant_dom_host_prototype,
    radiant_dom_host_invalidate,
    NULL,
};



static const JubeTypeDef radiant_types[] = {
    {"dom_node", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL, NULL},
    {"range", JUBE_TYPE_NON_OWNING_HOST, NULL, &radiant_dom_node_host_ops, NULL},
    {"selection", JUBE_TYPE_NON_OWNING_HOST, NULL, &radiant_dom_node_host_ops, NULL},
    // DOM3: style hosts are record-driven; no hand-written host ops remain
    {"inline_style", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL, NULL},
    {"computed_style", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL, NULL},
    {"stylesheet", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL, NULL},
    {"css_rule", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL, NULL},
    {"rule_style_decl", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL, NULL},
    {"document", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL, NULL},
    {"foreign_document", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL, NULL},
};

RADIANT_C_API const void* radiant_dom_node_host_type(void) {
    return &radiant_types[0];
}

RADIANT_C_API const void* radiant_dom_range_host_type(void) {
    return &radiant_types[1];
}

RADIANT_C_API const void* radiant_dom_selection_host_type(void) {
    return &radiant_types[2];
}

RADIANT_C_API const void* radiant_dom_inline_style_host_type(void) {
    return &radiant_types[3];
}

RADIANT_C_API const void* radiant_dom_computed_style_host_type(void) {
    return &radiant_types[4];
}

RADIANT_C_API const void* radiant_dom_stylesheet_host_type(void) {
    return &radiant_types[5];
}

RADIANT_C_API const void* radiant_dom_css_rule_host_type(void) {
    return &radiant_types[6];
}

RADIANT_C_API const void* radiant_dom_rule_style_decl_host_type(void) {
    return &radiant_types[7];
}

RADIANT_C_API const void* radiant_dom_document_host_type(void) {
    return &radiant_types[8];
}

RADIANT_C_API const void* radiant_dom_foreign_document_host_type(void) {
    return &radiant_types[9];
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
static const JubeFuncDef radiant_functions[] = {
    {"load", "fn(path: string) -> dom_node", (fn_ptr)fn_radiant_load, JUBE_FN_NONE,
     "Item fn_radiant_load(Item path)", (fn_ptr)fn_radiant_load},
    {"root", "fn(doc: dom_node) -> dom_node", (fn_ptr)fn_radiant_root, JUBE_FN_NONE,
     "Item fn_radiant_root(Item doc)", (fn_ptr)fn_radiant_root},
    {"attr", "fn(node: dom_node, name: string) -> string", (fn_ptr)fn_radiant_attr, JUBE_FN_NONE,
     "Item fn_radiant_attr(Item node, Item name)", (fn_ptr)fn_radiant_attr},
    {"set_attr", "fn(node: dom_node, name: string, value: string) -> dom_node", (fn_ptr)fn_radiant_set_attr, JUBE_FN_NONE,
     "Item fn_radiant_set_attr(Item node, Item name, Item value)", (fn_ptr)fn_radiant_set_attr},
    {"free", "fn(node: dom_node) -> null", (fn_ptr)fn_radiant_free, JUBE_FN_NONE,
     "Item fn_radiant_free(Item node)", (fn_ptr)fn_radiant_free},
    {"poc_attr", "fn(path: string) -> string", (fn_ptr)fn_radiant_poc_attr, JUBE_FN_NONE,
     "Item fn_radiant_poc_attr(Item path)", (fn_ptr)fn_radiant_poc_attr},
    {"register_layout", "fn(name: string, callback: fn) -> bool", (fn_ptr)fn_radiant_register_layout, JUBE_FN_NONE,
     "Item fn_radiant_register_layout(Item name, Item callback)", (fn_ptr)fn_radiant_register_layout},
};
#pragma clang diagnostic pop

// DOM3 declared interface + binding tables (radiant_dom_iface.cpp)
extern const char radiant_dom_interface_decl[];
extern const JubeTypeBinding radiant_dom_type_bindings[];
extern const int32_t radiant_dom_type_binding_count;

static const JubeModuleDef radiant_module = {
    JUBE_ABI_VERSION,
    sizeof(JubeModuleDef),
    "radiant",
    "0.2.0",
    "Radiant DOM and layout access",
    radiant_types,
    (int32_t)(sizeof(radiant_types) / sizeof(radiant_types[0])),
    radiant_functions,
    (int32_t)(sizeof(radiant_functions) / sizeof(radiant_functions[0])),
    NULL,
    0,
    radiant_module_init,
    NULL,
    radiant_dom_interface_decl,
    radiant_dom_type_bindings,
    10,  // DOM3 Phase 4e: document/foreign_document are binding-hook driven
};

RADIANT_C_API const JubeModuleDef* radiant_jube_module(void) {
    return &radiant_module;
}

RADIANT_C_API void radiant_jube_register_static(void) {
    jube_register_static_module(&radiant_module);
}
