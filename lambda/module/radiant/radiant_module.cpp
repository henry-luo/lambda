#include "../../jube/jube_registry.h"
#include "../../input/css/dom_element.hpp"
#include "radiant_host_api.hpp"
#include "radiant_dom_bridge.hpp"
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

static Item radiant_string_item(const char* value) {
    return value ? (Item){.item = s2it(heap_create_name(value))} : ItemNull;
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
