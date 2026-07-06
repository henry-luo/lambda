#include "../../lambda-data.hpp"

extern "C" Item js_dom_wrap_element_impl(void* dom_elem);
extern "C" void* js_dom_unwrap_element_impl(Item item);
extern "C" bool js_is_dom_node_impl(Item item);
extern "C" Item js_dom_get_property_impl(Item elem_item, Item prop_name);
extern "C" Item js_dom_set_property_impl(Item elem_item, Item prop_name, Item value);

extern "C" Item radiant_dom_wrap_node(void* dom_elem) {
    return js_dom_wrap_element_impl(dom_elem);
}

extern "C" void* radiant_dom_unwrap_node(Item item) {
    return js_dom_unwrap_element_impl(item);
}

extern "C" bool radiant_dom_is_node(Item item) {
    return js_is_dom_node_impl(item);
}

extern "C" Item radiant_dom_get_property(Item elem_item, Item prop_name) {
    return js_dom_get_property_impl(elem_item, prop_name);
}

extern "C" Item radiant_dom_set_property(Item elem_item, Item prop_name, Item value) {
    return js_dom_set_property_impl(elem_item, prop_name, value);
}
