#include "../../lambda-data.hpp"

extern "C" Item js_dom_get_property_impl(Item elem_item, Item prop_name);
extern "C" Item js_dom_set_property_impl(Item elem_item, Item prop_name, Item value);

extern "C" Item radiant_dom_get_property(Item elem_item, Item prop_name) {
    return js_dom_get_property_impl(elem_item, prop_name);
}

extern "C" Item radiant_dom_set_property(Item elem_item, Item prop_name, Item value) {
    return js_dom_set_property_impl(elem_item, prop_name, value);
}
