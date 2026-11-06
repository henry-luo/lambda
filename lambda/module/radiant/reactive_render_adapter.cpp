#include "reactive_render_adapter.hpp"
#include "../../../radiant/reactive_render_host.hpp"
#include "../../runtime/render_map.h"
#include "../../dom/dom_observers.h"

static bool reactive_render_equal(Item left, Item right) {
    return fn_eq_strict(left, right) == BOOL_TRUE;
}

static bool reactive_render_set_metadata(DomElement* element, const char* name, const char* value) {
    return dom_set_non_style_metadata(element, name, value);
}

bool lambda_reactive_render_host_register() {
    static const RadiantReactiveRenderHostOps operations = {
        render_map_get_doc_root, render_map_get_result, reactive_render_equal,
        reactive_render_set_metadata,
    };
    return radiant_reactive_render_host_register(&operations);
}
