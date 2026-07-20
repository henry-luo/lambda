#pragma once

#include "../../jube/jube.h"

#ifdef __cplusplus
#define RADIANT_C_API extern "C"
#else
#define RADIANT_C_API extern
#endif

struct DomDocument;

RADIANT_C_API Item radiant_dom_wrap_node(void* dom_elem);
RADIANT_C_API Item radiant_dom_lookup_cached_node(void* dom_elem);
RADIANT_C_API void* radiant_dom_unwrap_node(Item item);
RADIANT_C_API bool radiant_dom_is_node(Item item);
RADIANT_C_API Item radiant_dom_get_property(Item elem_item, Item prop_name);
RADIANT_C_API Item radiant_dom_set_property(Item elem_item, Item prop_name, Item value);
RADIANT_C_API Item radiant_dom_element_method(Item elem_item, Item method_name, Item* args, int argc);
RADIANT_C_API void radiant_dom_invalidate_document(DomDocument* doc);
RADIANT_C_API void radiant_dom_reset_wrapper_cache(void);

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

RADIANT_C_API int radiant_dom_node_named_get(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_node_named_set(Item object, Item key, Item value, Item* out);
RADIANT_C_API int radiant_dom_node_prototype(Item object, Item* out);



RADIANT_C_API int radiant_dom_document_host_get_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_document_host_set_property(Item object, Item key, Item value, Item* out);
RADIANT_C_API int radiant_dom_document_host_call_method(Item object, Item method_name,
                                                        Item* args, int argc, Item* out);
RADIANT_C_API int radiant_dom_document_host_has_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_document_host_delete_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_document_host_own_property_descriptor(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_document_host_own_property_names(Item object, Item* out);
RADIANT_C_API Item radiant_dom_document_host_prototype(Item object);
RADIANT_C_API int radiant_dom_document_prototype(Item object, Item* out);

RADIANT_C_API int radiant_dom_document_get_property(Item prop_name, Item* out);
RADIANT_C_API int radiant_dom_document_method(Item method_name, Item* args, int argc, Item* out);
RADIANT_C_API Item radiant_dom_window_add_event_listener(Item type, Item callback, Item opts);
RADIANT_C_API Item radiant_dom_window_remove_event_listener(Item type, Item callback, Item opts);
RADIANT_C_API Item radiant_dom_window_dispatch_event(Item event_item);
RADIANT_C_API int radiant_dom_window_get_property(Item object, Item key, Item* out);
RADIANT_C_API bool radiant_dom_has_committed_geometry_snapshot(DomDocument* doc);
RADIANT_C_API int radiant_dom_cssom_method(Item obj, Item method_name, Item* args,
                                           int argc, Item* out);

RADIANT_C_API const void* radiant_dom_node_host_type(void);
RADIANT_C_API const void* radiant_dom_range_host_type(void);
RADIANT_C_API const void* radiant_dom_selection_host_type(void);
RADIANT_C_API const void* radiant_dom_inline_style_host_type(void);
RADIANT_C_API const void* radiant_dom_computed_style_host_type(void);
RADIANT_C_API const void* radiant_dom_stylesheet_host_type(void);
RADIANT_C_API const void* radiant_dom_css_rule_host_type(void);
RADIANT_C_API const void* radiant_dom_rule_style_decl_host_type(void);
RADIANT_C_API const void* radiant_dom_document_host_type(void);
RADIANT_C_API const void* radiant_dom_foreign_document_host_type(void);

RADIANT_C_API Item fn_radiant_load(Item path_item);
RADIANT_C_API Item fn_radiant_root(Item doc_item);
RADIANT_C_API Item fn_radiant_attr(Item node_item, Item name_item);
RADIANT_C_API Item fn_radiant_set_attr(Item node_item, Item name_item, Item value_item);
RADIANT_C_API Item fn_radiant_free(Item node_item);
RADIANT_C_API Item fn_radiant_layout(Item node_item);
RADIANT_C_API Item fn_radiant_render_svg(Item html_item, Item width_item, Item height_item);
RADIANT_C_API Item fn_radiant_box(Item node_item);
RADIANT_C_API Item fn_radiant_poc_attr(Item path_item);
RADIANT_C_API Item fn_radiant_register_layout(Item name_item, Item fn_item);
RADIANT_C_API Item fn_radiant_velmt_index(Item velmt_item);
RADIANT_C_API Item fn_radiant_velmt_tag(Item velmt_item);
RADIANT_C_API Item fn_radiant_velmt_id(Item velmt_item);
RADIANT_C_API Item fn_radiant_velmt_attr(Item velmt_item, Item name_item);
RADIANT_C_API Item fn_radiant_velmt_attr_or(Item velmt_item, Item name_item, Item default_item);
RADIANT_C_API Item fn_radiant_velmt_width(Item velmt_item);
RADIANT_C_API Item fn_radiant_velmt_height(Item velmt_item);
RADIANT_C_API Item fn_radiant_velmt_box(Item velmt_item);
RADIANT_C_API Item fn_radiant_velmt_children(Item velmt_item);
RADIANT_C_API Item fn_radiant_velmt_text(Item velmt_item);
RADIANT_C_API Item fn_radiant_velmt_style(Item velmt_item, Item name_item);
RADIANT_C_API Item fn_radiant_velmt_style_or(Item velmt_item, Item name_item, Item default_item);
RADIANT_C_API Item fn_radiant_velmt_margin(Item velmt_item);
RADIANT_C_API Item fn_radiant_velmt_border(Item velmt_item);
RADIANT_C_API Item fn_radiant_velmt_padding(Item velmt_item);

RADIANT_C_API const JubeModuleDef* radiant_jube_module(void);
RADIANT_C_API void radiant_jube_register_static(void);
