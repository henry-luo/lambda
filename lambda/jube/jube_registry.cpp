#include "jube_registry.h"
#include "jube_interface.h"
#include "../input/css/dom_element.hpp"
#include "../../lib/log.h"
#include <string.h>
#include <stdlib.h>
#if !defined(_WIN32)
#include <dlfcn.h>
#endif

#define JUBE_STATIC_MODULE_CAPACITY 64

typedef struct JubeStaticModuleEntry {
    const JubeModuleDef* module;
    bool initialized;
    void* dynamic_handle;
} JubeStaticModuleEntry;

static JubeStaticModuleEntry jube_static_modules[JUBE_STATIC_MODULE_CAPACITY];
static int jube_static_modules_count = 0;
static bool jube_dynamic_modules_from_env_loaded = false;
extern "C" void heap_register_gc_root(uint64_t* slot);
extern "C" void heap_unregister_gc_root(uint64_t* slot);
extern "C" Item vmap_new(void);
extern "C" Item js_new_object(void);
extern "C" Item js_array_new(int capacity);
extern "C" Item js_array_push(Item array, Item value);
extern "C" Item js_property_get(Item object, Item key);
extern "C" Item js_property_set(Item object, Item key, Item value);
extern "C" Item js_new_function(void* func_ptr, int param_count);
extern "C" void js_function_set_prototype(Item fn_item, Item proto);
extern "C" void js_set_function_name(Item fn_item, Item name_item);
extern "C" void js_mark_non_enumerable(Item object, Item name);
extern "C" Item js_get_global_this(void);
extern "C" Item js_get_global_property(Item key);
extern "C" Item js_new_error_with_name(Item error_name, Item message);
extern "C" void js_throw_value(Item error);
extern "C" Item js_reflect_own_keys(Item obj);
extern "C" Item js_reflect_delete_property(Item obj, Item key);
extern "C" Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
extern "C" int js_check_exception(void);
extern "C" bool js_is_truthy(Item value);
extern "C" Item js_get_intrinsic_prototype_for_class(int class_id);
extern "C" void* js_dom_get_document(void);
extern "C" Item js_get_document_object_value(void);
extern "C" void* js_dom_get_or_create_doc_node(void* doc);
extern "C" Item js_dom_document_proxy_for_doc_bridge(void* doc);
extern "C" void* js_dom_unwrap_element_impl(Item item);
extern "C" void js_dom_initialize_node_wrapper(void* dom_elem);
extern "C" bool js_is_css_namespace(Item item);
extern "C" bool js_is_inline_style_item(Item item);
extern "C" bool js_is_computed_style_item(Item item);
extern "C" bool js_is_stylesheet(Item item);
extern "C" bool js_is_css_rule(Item item);
extern "C" bool js_is_rule_style_decl(Item item);
extern "C" Item js_dom_get_property_impl(Item elem_item, Item prop_name);
extern "C" Item js_dom_set_property_impl(Item elem_item, Item prop_name, Item value);
extern "C" Item js_dom_element_method_impl(Item elem_item, Item method_name, Item* args, int argc);
extern "C" Item js_computed_style_get_property(Item style_item, Item prop_name);
extern "C" bool js_dom_style_resource_has_property(Item style_item, Item prop_name);
extern "C" Item js_dom_style_method(Item elem_item, Item method_name, Item* args, int argc);
extern "C" Item js_dom_get_prototype_value(Item obj);
extern "C" bool js_cssom_resource_has_property(Item item, Item prop_name);
extern "C" Item js_cssom_stylesheet_get_property(Item sheet_item, Item prop_name);
extern "C" Item js_cssom_rule_get_property(Item rule_item, Item prop_name);
extern "C" Item js_cssom_rule_set_property(Item rule_item, Item prop_name, Item value);
extern "C" Item js_cssom_rule_decl_get_property(Item decl_item, Item prop_name);
extern "C" Item js_cssom_rule_decl_set_property(Item decl_item, Item prop_name, Item value);
extern "C" void* js_get_foreign_doc(Item item);
extern "C" void* js_dom_swap_active_document(void* new_doc);
extern "C" void js_dom_restore_active_document(void* prev_doc);
extern "C" Item js_document_proxy_get_property(Item prop_name);
extern "C" Item js_document_proxy_set_property(Item prop_name, Item value);
extern "C" Item js_document_proxy_method(Item method_name, Item* args, int argc);
extern "C" bool js_dom_item_is_range(Item item);
extern "C" bool js_dom_item_is_selection(Item item);
extern "C" Item js_dom_range_get_property(Item obj, Item key);
extern "C" Item js_dom_range_set_property(Item obj, Item key, Item value);
extern "C" Item js_dom_selection_get_property(Item obj, Item key);
extern "C" Item js_dom_selection_set_property(Item obj, Item key, Item value);
extern "C" Item js_dom_range_get_prototype_value(void);
extern "C" Item js_dom_selection_get_prototype_value(void);
extern "C" bool js_dom_range_native_property(Item obj, Item key);
extern "C" bool js_dom_selection_native_property(Item obj, Item key);
extern "C" bool js_dom_expando_has_property(Item obj, Item key);
extern "C" bool js_dom_range_expando_has_property(Item obj, Item key);
extern "C" bool js_dom_selection_expando_has_property(Item obj, Item key);
extern "C" Item js_dom_expando_get_own_property_descriptor(Item obj, Item key);
extern "C" Item js_dom_range_expando_get_own_property_descriptor(Item obj, Item key);
extern "C" Item js_dom_selection_expando_get_own_property_descriptor(Item obj, Item key);
extern "C" Item js_dom_expando_delete_property(Item obj, Item key);
extern "C" Item js_dom_range_expando_delete_property(Item obj, Item key);
extern "C" Item js_dom_selection_expando_delete_property(Item obj, Item key);
extern "C" Item js_dom_expando_own_property_names(Item obj);
extern "C" Item js_dom_range_expando_own_property_names(Item obj);
extern "C" Item js_dom_selection_expando_own_property_names(Item obj);
extern "C" Item js_css_namespace_method(Item obj, Item method_name, Item* args, int argc);
extern "C" Item js_cssom_stylesheet_method(Item sheet_item, Item method_name, Item* args, int argc);
extern "C" Item js_cssom_rule_decl_method(Item decl_item, Item method_name, Item* args, int argc);
extern "C" Item js_dom_owner_document_for_node(void* node);
extern "C" const char* js_dom_to_attribute_cstr(Item value);
extern "C" void js_dom_after_set_attribute(void* elem, const char* attr_name, const char* attr_value);
extern "C" void js_dom_after_remove_attribute(void* elem, const char* attr_name);
extern "C" void js_dom_after_toggle_attribute_remove(void* elem, const char* attr_name);
extern "C" void js_dom_after_disabled_attribute_set(void* elem);
extern "C" void js_dom_after_default_checked_set(void* elem, bool checked);
extern "C" void js_dom_after_default_selected_set(void* elem, bool selected);
extern "C" void js_dom_after_select_multiple_removed(void* elem);
extern "C" void js_dom_set_checked_dirty(void* elem, bool checked);
extern "C" void js_dom_select_set_value_bridge(void* elem, const char* value);
extern "C" void js_dom_select_set_selected_index_bridge(void* elem, Item value);
extern "C" void js_dom_select_set_length_bridge(void* elem, Item value);
extern "C" void js_dom_set_option_selected_dirty(void* elem, bool selected);
extern "C" void js_dom_set_option_text_bridge(void* elem, const char* value);
extern "C" void js_dom_after_srcdoc_set(void* elem);
extern "C" void js_dom_throw_contenteditable_syntax_error(void);
extern "C" Item js_dom_set_text_data_property(void* text, Item value);
extern "C" Item js_dom_text_control_set_value_bridge(void* elem, Item value);
extern "C" Item js_dom_text_control_set_selection_start_bridge(void* elem, Item value);
extern "C" Item js_dom_text_control_set_selection_end_bridge(void* elem, Item value);
extern "C" Item js_dom_text_control_set_selection_direction_bridge(void* elem, Item value);
extern "C" Item js_dom_text_control_set_default_value_bridge(void* elem, Item value);
extern "C" Item js_dom_text_control_set_selection_range_bridge(void* elem, Item start, Item end, Item dir);
extern "C" Item js_dom_text_control_set_range_text_bridge(void* elem, Item replacement, Item start,
                                                          Item end, Item mode);
extern "C" Item js_dom_text_control_select_bridge(void* elem);
extern "C" Item js_dom_form_reset_bridge(Item form_item);
extern "C" Item js_dom_check_validity_bridge(Item elem_item);
extern "C" Item js_dom_report_validity_bridge(Item elem_item);
extern "C" Item js_dom_form_submit_bridge(Item form_item);
extern "C" Item js_dom_form_request_submit_bridge(Item form_item, Item submitter);
extern "C" Item js_dom_focus_method_bridge(void* elem, bool focus);
extern "C" Item js_dom_click_method_bridge(Item elem_item);
extern "C" Item js_dom_add_event_listener_bridge(Item target_item, Item type, Item callback, Item opts);
extern "C" Item js_dom_remove_event_listener_bridge(Item target_item, Item type, Item callback, Item opts);
extern "C" Item js_dom_dispatch_event_bridge(Item target_item, Item event_item);
extern "C" Item js_dom_get_bounding_client_rect_bridge(void* elem);
extern "C" Item js_dom_get_client_rects_bridge(void* elem);
extern "C" Item js_dom_scroll_into_view_bridge(void* elem);
extern "C" Item js_dom_scroll_method_bridge(Item elem_item, Item method_name, Item* args, int argc);
extern "C" Item js_dom_text_control_caret_bounds_bridge(void* elem);
extern "C" Item js_dom_text_control_boundary_from_point_bridge(void* elem, Item x, Item y);
extern "C" Item js_dom_boundary_from_point_bridge(void* elem, Item x, Item y, Item behavior);
extern "C" Item js_dom_style_set_property_bridge(void* elem, Item prop, Item value,
                                                 Item priority, bool has_priority);
extern "C" Item js_dom_style_remove_property_bridge(void* elem, Item prop);
extern "C" Item js_dom_text_replace_data_bridge(void* text, Item offset, Item count, Item data);
extern "C" Item js_dom_text_insert_data_bridge(void* text, Item offset, Item data);
extern "C" Item js_dom_text_append_data_bridge(void* text, Item data);
extern "C" Item js_dom_text_delete_data_bridge(void* text, Item offset, Item count);
extern "C" Item js_dom_text_substring_data_bridge(void* text, Item offset, Item count);
extern "C" Item js_dom_append_child_bridge(void* parent, Item child);
extern "C" Item js_dom_remove_child_bridge(void* parent, Item child);
extern "C" Item js_dom_insert_before_bridge(void* parent, Item new_child, Item ref_child);
extern "C" Item js_dom_remove_bridge(void* node);
extern "C" Item js_dom_adopt_node_bridge(Item node);
extern "C" Item js_dom_location_method_bridge(void* doc, Item method_name, Item* args, int argc);
extern "C" Item js_dom_document_open_bridge(void* doc);
extern "C" Item js_dom_document_write_bridge(void* doc, Item text);
extern "C" Item js_dom_document_element_from_point_bridge(void* doc, Item x, Item y);
extern "C" Item js_dom_create_range(void);
extern "C" Item js_dom_get_selection(void);
extern "C" Item js_dom_get_selection_function_for_document(void* doc);
extern "C" bool js_doc_has_browsing_context(void* doc);
extern "C" Item js_dom_document_fonts_bridge(void);
extern "C" Item js_dom_document_stylesheets_bridge(void);
extern "C" Item js_dom_document_default_view_bridge(void* doc);
extern "C" Item js_dom_document_implementation_bridge(void);
extern "C" Item js_dom_document_design_mode_bridge(void);
extern "C" Item js_dom_document_active_element_bridge(void* doc);
extern "C" Item js_dom_normalize_bridge(void* elem);
extern "C" Item js_dom_live_child_collection_bridge(void* elem, bool elements_only);
extern "C" Item js_dom_live_document_forms_bridge(void* doc);
extern "C" Item js_dom_live_form_elements_bridge(void* elem);
extern "C" Item js_dom_live_document_get_elements_by_tag_name_bridge(void* doc, Item query);
extern "C" Item js_dom_live_document_get_elements_by_class_name_bridge(void* doc, Item query);
extern "C" Item js_dom_live_document_get_elements_by_name_bridge(void* doc, Item query);
extern "C" Item js_dom_live_element_get_elements_by_tag_name_bridge(void* elem, Item query);
extern "C" Item js_dom_live_element_get_elements_by_class_name_bridge(void* elem, Item query);
extern "C" Item js_dom_clone_node_bridge(void* elem, Item deep, bool has_deep);
extern "C" Item js_dom_replace_child_bridge(void* parent, Item new_child, Item old_child);
extern "C" Item js_dom_replace_with_bridge(void* node, Item* args, int argc);
extern "C" Item js_dom_insert_adjacent_element_bridge(void* elem, Item position, Item new_node);
extern "C" Item js_dom_insert_adjacent_html_bridge(void* elem, Item position, Item html);
extern "C" Item js_dom_append_variadic_bridge(void* elem, Item* args, int argc);
extern "C" Item js_dom_prepend_variadic_bridge(void* elem, Item* args, int argc);
extern "C" void js_dom_notify_mutation(DomJsMutationKind kind, void* target, void* parent);

static void jube_host_dom_notify_mutation(int kind, void* target, void* parent) {
    js_dom_notify_mutation((DomJsMutationKind)kind, target, parent);
}

static const JubeHostGcAPI jube_host_gc_api = {
    heap_register_gc_root,
    heap_unregister_gc_root,
};

static const JubeHostValueAPI jube_host_value_api = {
    vmap_new,
    js_new_object,
    js_array_new,
    js_array_push,
    js_property_get,
    js_property_set,
};

static const JubeHostScriptAPI jube_host_script_api = {
    js_new_function,
    js_function_set_prototype,
    js_set_function_name,
    js_mark_non_enumerable,
    js_get_global_this,
    js_get_global_property,
    js_new_error_with_name,
    js_throw_value,
    js_reflect_own_keys,
    js_reflect_delete_property,
    js_call_function,
    js_check_exception,
    js_is_truthy,
    js_get_intrinsic_prototype_for_class,
};

static const JubeHostDomAPI jube_host_dom_api = {
    js_dom_get_document,
    js_get_document_object_value,
    js_dom_get_or_create_doc_node,
    js_dom_document_proxy_for_doc_bridge,
    js_dom_unwrap_element_impl,
    js_dom_initialize_node_wrapper,
    js_is_css_namespace,
    js_is_inline_style_item,
    js_is_computed_style_item,
    js_is_stylesheet,
    js_is_css_rule,
    js_is_rule_style_decl,
    js_dom_get_property_impl,
    js_dom_set_property_impl,
    js_dom_element_method_impl,
    js_computed_style_get_property,
    js_dom_style_resource_has_property,
    js_dom_style_method,
    js_dom_get_prototype_value,
    js_cssom_resource_has_property,
    js_cssom_stylesheet_get_property,
    js_cssom_rule_get_property,
    js_cssom_rule_set_property,
    js_cssom_rule_decl_get_property,
    js_cssom_rule_decl_set_property,
    js_get_foreign_doc,
    js_dom_swap_active_document,
    js_dom_restore_active_document,
    js_document_proxy_get_property,
    js_document_proxy_set_property,
    js_document_proxy_method,
    js_dom_item_is_range,
    js_dom_item_is_selection,
    js_dom_range_get_property,
    js_dom_range_set_property,
    js_dom_selection_get_property,
    js_dom_selection_set_property,
    js_dom_range_get_prototype_value,
    js_dom_selection_get_prototype_value,
    js_dom_range_native_property,
    js_dom_selection_native_property,
    js_dom_expando_has_property,
    js_dom_range_expando_has_property,
    js_dom_selection_expando_has_property,
    js_dom_expando_get_own_property_descriptor,
    js_dom_range_expando_get_own_property_descriptor,
    js_dom_selection_expando_get_own_property_descriptor,
    js_dom_expando_delete_property,
    js_dom_range_expando_delete_property,
    js_dom_selection_expando_delete_property,
    js_dom_expando_own_property_names,
    js_dom_range_expando_own_property_names,
    js_dom_selection_expando_own_property_names,
    js_css_namespace_method,
    js_cssom_stylesheet_method,
    js_cssom_rule_decl_method,
    js_dom_owner_document_for_node,
    js_dom_to_attribute_cstr,
    js_dom_after_set_attribute,
    js_dom_after_remove_attribute,
    js_dom_after_toggle_attribute_remove,
    js_dom_after_disabled_attribute_set,
    js_dom_after_default_checked_set,
    js_dom_after_default_selected_set,
    js_dom_after_select_multiple_removed,
    js_dom_set_checked_dirty,
    js_dom_select_set_value_bridge,
    js_dom_select_set_selected_index_bridge,
    js_dom_select_set_length_bridge,
    js_dom_set_option_selected_dirty,
    js_dom_set_option_text_bridge,
    js_dom_after_srcdoc_set,
    js_dom_throw_contenteditable_syntax_error,
    js_dom_set_text_data_property,
    js_dom_text_control_set_value_bridge,
    js_dom_text_control_set_selection_start_bridge,
    js_dom_text_control_set_selection_end_bridge,
    js_dom_text_control_set_selection_direction_bridge,
    js_dom_text_control_set_default_value_bridge,
    js_dom_text_control_set_selection_range_bridge,
    js_dom_text_control_set_range_text_bridge,
    js_dom_text_control_select_bridge,
    js_dom_form_reset_bridge,
    js_dom_check_validity_bridge,
    js_dom_report_validity_bridge,
    js_dom_form_submit_bridge,
    js_dom_form_request_submit_bridge,
    js_dom_focus_method_bridge,
    js_dom_click_method_bridge,
    js_dom_add_event_listener_bridge,
    js_dom_remove_event_listener_bridge,
    js_dom_dispatch_event_bridge,
    js_dom_get_bounding_client_rect_bridge,
    js_dom_get_client_rects_bridge,
    js_dom_scroll_into_view_bridge,
    js_dom_scroll_method_bridge,
    js_dom_text_control_caret_bounds_bridge,
    js_dom_text_control_boundary_from_point_bridge,
    js_dom_boundary_from_point_bridge,
    js_dom_style_set_property_bridge,
    js_dom_style_remove_property_bridge,
    js_dom_text_replace_data_bridge,
    js_dom_text_insert_data_bridge,
    js_dom_text_append_data_bridge,
    js_dom_text_delete_data_bridge,
    js_dom_text_substring_data_bridge,
    js_dom_append_child_bridge,
    js_dom_remove_child_bridge,
    js_dom_insert_before_bridge,
    js_dom_remove_bridge,
    js_dom_adopt_node_bridge,
    js_dom_location_method_bridge,
    js_dom_document_open_bridge,
    js_dom_document_write_bridge,
    js_dom_document_element_from_point_bridge,
    js_dom_create_range,
    js_dom_get_selection,
    js_dom_get_selection_function_for_document,
    js_doc_has_browsing_context,
    js_dom_document_fonts_bridge,
    js_dom_document_stylesheets_bridge,
    js_dom_document_default_view_bridge,
    js_dom_document_implementation_bridge,
    js_dom_document_design_mode_bridge,
    js_dom_document_active_element_bridge,
    js_dom_normalize_bridge,
    js_dom_live_child_collection_bridge,
    js_dom_live_document_forms_bridge,
    js_dom_live_form_elements_bridge,
    js_dom_live_document_get_elements_by_tag_name_bridge,
    js_dom_live_document_get_elements_by_class_name_bridge,
    js_dom_live_document_get_elements_by_name_bridge,
    js_dom_live_element_get_elements_by_tag_name_bridge,
    js_dom_live_element_get_elements_by_class_name_bridge,
    js_dom_clone_node_bridge,
    js_dom_replace_child_bridge,
    js_dom_replace_with_bridge,
    js_dom_insert_adjacent_element_bridge,
    js_dom_insert_adjacent_html_bridge,
    js_dom_append_variadic_bridge,
    js_dom_prepend_variadic_bridge,
    jube_host_dom_notify_mutation,
};

static JubeHostAPI jube_host_api = {
    JUBE_ABI_VERSION,
    &jube_host_gc_api,
    &jube_host_value_api,
    &jube_host_script_api,
    &jube_host_dom_api,
};

extern "C" const JubeHostAPI* jube_internal_host_api(void) {
    return &jube_host_api;
}

// size-gated access to the DOM3 additive tail: a field exists only when the
// module's declared struct_size covers it, so v1 descriptors read as "no tail"
static bool jube_module_has_field(const JubeModuleDef* module, size_t field_end) {
    return module && module->struct_size >= field_end;
}

extern "C" const char* jube_module_interface_decl(const JubeModuleDef* module) {
    size_t end = offsetof(JubeModuleDef, interface_decl) + sizeof(module->interface_decl);
    return jube_module_has_field(module, end) ? module->interface_decl : NULL;
}

extern "C" const JubeTypeBinding* jube_module_type_bindings(const JubeModuleDef* module,
                                                            int32_t* count) {
    if (count) *count = 0;
    size_t end = offsetof(JubeModuleDef, type_binding_count) +
                 sizeof(module->type_binding_count);
    if (!jube_module_has_field(module, end)) return NULL;
    if (count) *count = module->type_binding_count;
    return module->type_bindings;
}

extern "C" void radiant_jube_register_static(void);
extern "C" void hostobj_demo_jube_register_static(void);

static bool jube_module_name_equals(const char* a, const char* b) {
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

static int jube_find_static_module_index(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < jube_static_modules_count; i++) {
        const JubeModuleDef* module = jube_static_modules[i].module;
        if (module && jube_module_name_equals(module->name, name)) return i;
    }
    return -1;
}

static bool jube_env_flag_enabled(const char* name) {
    const char* value = getenv(name);
    if (!value || !*value) return false;
    return strcmp(value, "0") != 0 && strcmp(value, "false") != 0 && strcmp(value, "FALSE") != 0;
}

static void jube_close_dynamic_handle(void* handle) {
#if !defined(_WIN32)
    if (handle) dlclose(handle);
#else
    (void)handle;
#endif
}

// release strips log_info arguments, so keep diagnostic-only helpers out of NDEBUG builds.
#if !defined(NDEBUG)
static int jube_host_ops_count(const JubeHostObjectOps* ops) {
    if (!ops) return 0;
    int count = 0;
    if (ops->get_property) count++;
    if (ops->set_property) count++;
    if (ops->call_method) count++;
    if (ops->has_property) count++;
    if (ops->delete_property) count++;
    if (ops->get_own_property_descriptor) count++;
    if (ops->own_property_keys) count++;
    if (ops->prototype) count++;
    if (ops->invalidate) count++;
    if (ops->destroy) count++;
    return count;
}

static void jube_log_module_type_ops(const JubeModuleDef* module) {
    if (!module || !module->types || module->type_count <= 0) return;
    for (int i = 0; i < module->type_count; i++) {
        const JubeTypeDef* type = &module->types[i];
        if (!type || !(type->flags & (JUBE_TYPE_NON_OWNING_HOST | JUBE_TYPE_OWNING_NATIVE))) {
            continue;
        }
        log_info("JUBE_REG: type %s.%s host_ops=%d/10",
                 module->name ? module->name : "(module)",
                 type->name ? type->name : "(type)",
                 jube_host_ops_count(type->host_ops));
    }
}
#endif

static int jube_register_module_descriptor(const JubeModuleDef* module, void* dynamic_handle,
                                           const char* source_label) {
    if (!module || !module->name) {
        log_error("JUBE_REG: cannot register null %s module", source_label ? source_label : "Jube");
        return -1;
    }
    if (module->abi_version != JUBE_ABI_VERSION) {
        log_error("JUBE_REG: module '%s' ABI mismatch: got %u expected %u",
                  module->name, module->abi_version, JUBE_ABI_VERSION);
        return -1;
    }
    // v1 modules stop at JUBE_MODULE_DEF_V1_SIZE; the DOM3 tail is additive and
    // size-gated, so only the frozen v1 prefix is a hard requirement here.
    if (module->struct_size < JUBE_MODULE_DEF_V1_SIZE) {
        log_error("JUBE_REG: module '%s' descriptor is too small: got %u expected %zu",
                  module->name, module->struct_size, (size_t)JUBE_MODULE_DEF_V1_SIZE);
        return -1;
    }

    int existing = jube_find_static_module_index(module->name);
    if (existing >= 0) {
        log_debug("JUBE_REG: %s module '%s' already registered",
                  source_label ? source_label : "Jube", module->name);
        jube_close_dynamic_handle(dynamic_handle);
        return 0;
    }
    if (jube_static_modules_count >= JUBE_STATIC_MODULE_CAPACITY) {
        log_error("JUBE_REG: module capacity exceeded while registering '%s'", module->name);
        jube_close_dynamic_handle(dynamic_handle);
        return -1;
    }

    int slot = jube_static_modules_count++;
    jube_static_modules[slot].module = module;
    jube_static_modules[slot].initialized = false;
    // Dynamic descriptors and function tables live in the loaded image, so keep the handle open.
    jube_static_modules[slot].dynamic_handle = dynamic_handle;

    if (module->init) {
        int rc = module->init(&jube_host_api);
        if (rc != 0) {
            jube_static_modules_count--;
            jube_static_modules[slot].module = NULL;
            jube_static_modules[slot].initialized = false;
            jube_static_modules[slot].dynamic_handle = NULL;
            jube_close_dynamic_handle(dynamic_handle);
            log_error("JUBE_REG: %s module '%s' init failed with code %d",
                      source_label ? source_label : "Jube", module->name, rc);
            return -1;
        }
        jube_static_modules[slot].initialized = true;
    }

    // DOM3: compile the module's interface declaration against its binding
    // tables; a half-valid interface must fail registration, not limp along.
    if (jube_compile_module_interface(module) != 0) {
        if (module->shutdown) module->shutdown();
        jube_static_modules_count--;
        jube_static_modules[slot].module = NULL;
        jube_static_modules[slot].initialized = false;
        jube_static_modules[slot].dynamic_handle = NULL;
        jube_close_dynamic_handle(dynamic_handle);
        log_error("JUBE_REG: %s module '%s' interface compilation failed",
                  source_label ? source_label : "Jube", module->name);
        return -1;
    }

    log_info("JUBE_REG: registered %s module '%s' version '%s'",
             source_label ? source_label : "Jube", module->name,
             module->version ? module->version : "(none)");
#if !defined(NDEBUG)
    jube_log_module_type_ops(module);
#endif
    return 0;
}

int jube_register_static_module(const JubeModuleDef* module) {
    return jube_register_module_descriptor(module, NULL, "static");
}

typedef const JubeModuleDef* (*JubeDynamicModuleEntry)(void);

int jube_load_dynamic_module(const char* path, const char* entry_symbol) {
    if (!path || !*path) {
        log_error("JUBE_REG: dynamic module path is empty");
        return -1;
    }
#if defined(_WIN32)
    (void)entry_symbol;
    log_error("JUBE_REG: dynamic Jube module loading is not implemented on Windows");
    return -1;
#else
    const char* symbol = (entry_symbol && *entry_symbol) ? entry_symbol : "jube_module";
    void* handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        log_error("JUBE_REG: dlopen failed for '%s': %s", path, dlerror());
        return -1;
    }

    dlerror();
    JubeDynamicModuleEntry entry = (JubeDynamicModuleEntry)dlsym(handle, symbol);
    const char* error = dlerror();
    if (error || !entry) {
        log_error("JUBE_REG: dlsym failed for '%s' in '%s': %s",
                  symbol, path, error ? error : "entry not found");
        jube_close_dynamic_handle(handle);
        return -1;
    }

    const JubeModuleDef* module = entry();
    if (!module) {
        log_error("JUBE_REG: dynamic module '%s' returned a null descriptor", path);
        jube_close_dynamic_handle(handle);
        return -1;
    }
    return jube_register_module_descriptor(module, handle, "dynamic");
#endif
}

static void jube_load_dynamic_modules_from_env(void) {
    if (jube_dynamic_modules_from_env_loaded) return;
    jube_dynamic_modules_from_env_loaded = true;
    const char* path = getenv("JUBE_DYNAMIC_MODULE");
    if (!path || !*path) return;
    const char* entry = getenv("JUBE_DYNAMIC_ENTRY");
    if (jube_load_dynamic_module(path, entry) != 0) {
        log_error("JUBE_REG: failed to load env dynamic module '%s'", path);
    }
}

void jube_register_builtin_modules(void) {
    radiant_jube_register_static();
    // Phase-7 validation must let the dlopen copy win name registration over the static demo.
    if (!jube_env_flag_enabled("JUBE_HOSTOBJ_DEMO_DYNAMIC_ONLY")) {
        hostobj_demo_jube_register_static();
    }
    jube_load_dynamic_modules_from_env();
}

int jube_static_module_count(void) {
    return jube_static_modules_count;
}

const JubeModuleDef* jube_static_module_at(int index) {
    if (index < 0 || index >= jube_static_modules_count) return NULL;
    return jube_static_modules[index].module;
}

const JubeModuleDef* jube_find_static_module(const char* name) {
    int index = jube_find_static_module_index(name);
    if (index < 0) return NULL;
    return jube_static_modules[index].module;
}

const JubeTypeDef* jube_find_type_by_host_type(const void* host_type) {
    if (!host_type) return NULL;
    for (int i = 0; i < jube_static_modules_count; i++) {
        const JubeModuleDef* module = jube_static_modules[i].module;
        if (!module || !module->types || module->type_count <= 0) continue;
        for (int j = 0; j < module->type_count; j++) {
            const JubeTypeDef* type = &module->types[j];
            if ((const void*)type == host_type) return type;
        }
    }
    return NULL;
}
