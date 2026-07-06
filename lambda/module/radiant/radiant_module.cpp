#include "../../jube/jube_registry.h"
#include "../../input/css/dom_element.hpp"
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

extern "C" Item fn_radiant_poc_attr(Item path_item) {
    const char* path = fn_to_cstr(path_item);
    if (!path || !path[0]) {
        log_error("JUBE_RADIANT_POC: missing HTML path");
        return ItemNull;
    }

    Url* cwd = get_current_dir();
    Url* html_url = parse_url(cwd, path);
    if (cwd) url_destroy(cwd);

    Pool* doc_pool = mem_pool_create(NULL, MEM_ROLE_LAYOUT, "radiant.poc_attr");
    if (!html_url || !doc_pool) {
        log_error("JUBE_RADIANT_POC: failed to create document inputs for '%s'", path);
        if (html_url) url_destroy(html_url);
        if (doc_pool) pool_destroy(doc_pool);
        return ItemNull;
    }

    DomDocument* doc = load_lambda_html_doc(html_url, NULL, 800, 600, doc_pool, nullptr, false, false);
    if (!doc) {
        log_error("JUBE_RADIANT_POC: failed to load HTML document '%s'", path);
        url_destroy(html_url);
        pool_destroy(doc_pool);
        return ItemNull;
    }

    if (!doc->root) {
        log_error("JUBE_RADIANT_POC: document '%s' has no root element", path);
        free_document(doc);
        return ItemNull;
    }

    dom_element_set_attribute(doc->root, "data-poc", "ok");
    const char* value = dom_element_get_attribute(doc->root, "data-poc");
    Item result = value ? (Item){.item = s2it(heap_create_name(value))} : ItemNull;
    free_document(doc);
    return result;
}

static int radiant_module_init(const JubeHostAPI* host) {
    if (!host) {
        log_error("JUBE_RADIANT: missing host API during module init");
        return -1;
    }
    log_info("JUBE_RADIANT: static radiant module initialized");
    return 0;
}

static const JubeTypeDef radiant_types[] = {
    {"dom_node", 0},
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
static const JubeFuncDef radiant_functions[] = {
    {"poc_attr", "fn(path: string) -> string", (fn_ptr)fn_radiant_poc_attr, JUBE_FN_NONE,
     "Item fn_radiant_poc_attr(Item path)", (fn_ptr)fn_radiant_poc_attr},
};
#pragma clang diagnostic pop

static const JubeModuleDef radiant_module = {
    JUBE_ABI_VERSION,
    sizeof(JubeModuleDef),
    "radiant",
    "0.1.0",
    "Radiant DOM and layout access",
    radiant_types,
    (int32_t)(sizeof(radiant_types) / sizeof(radiant_types[0])),
    radiant_functions,
    (int32_t)(sizeof(radiant_functions) / sizeof(radiant_functions[0])),
    NULL,
    0,
    radiant_module_init,
    NULL,
};

extern "C" const JubeModuleDef* radiant_jube_module(void) {
    return &radiant_module;
}

extern "C" void radiant_jube_register_static(void) {
    jube_register_static_module(&radiant_module);
}
