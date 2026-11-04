#pragma once

// module-owned collection metadata has process lifetime; runtime callers borrow
// its identity through a lower-owned registry (D9.1.3 / D9.5.6, D5.5.1).
#define DOM_COLLECTION_HOST_TYPES(OP) \
    OP(node_list) \
    OP(html_collection) \
    OP(html_options_collection) \
    OP(html_form_controls_collection) \
    OP(named_node_map) \
    OP(token_list) \
    OP(radio_node_list) \
    OP(rect_list) \
    OP(style_sheet_list) \
    OP(css_rule_list)

struct DomCollectionHostTypes {
#define DOM_COLLECTION_FIELD(name) const void* name;
    DOM_COLLECTION_HOST_TYPES(DOM_COLLECTION_FIELD)
#undef DOM_COLLECTION_FIELD
};

extern "C" bool dom_collection_register_types(const DomCollectionHostTypes* types);
extern "C" const DomCollectionHostTypes* dom_collection_host_types(void);
#define DOM_COLLECTION_ACCESSOR(name) extern "C" const void* dom_##name##_host_type(void);
DOM_COLLECTION_HOST_TYPES(DOM_COLLECTION_ACCESSOR)
#undef DOM_COLLECTION_ACCESSOR
