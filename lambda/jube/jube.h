#pragma once

#include "../lambda.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JUBE_ABI_VERSION 1

typedef struct JubeHostAPI JubeHostAPI;
typedef struct JubeTypeDef JubeTypeDef;
typedef struct JubeFuncDef JubeFuncDef;
typedef struct JubeNamespaceDef JubeNamespaceDef;
typedef struct JubeModuleDef JubeModuleDef;
typedef struct JubeHostObjectOps JubeHostObjectOps;
typedef struct JubeHostGcAPI JubeHostGcAPI;
typedef struct JubeHostValueAPI JubeHostValueAPI;
typedef struct JubeHostScriptAPI JubeHostScriptAPI;
typedef struct JubeHostDomAPI JubeHostDomAPI;

typedef enum JubeFuncFlags {
    JUBE_FN_NONE = 0,
    JUBE_FN_METHOD_ELIGIBLE = 1u << 0,
    JUBE_FN_VARARGS = 1u << 1,
} JubeFuncFlags;

typedef enum JubeTypeFlags {
    JUBE_TYPE_NONE = 0,
    JUBE_TYPE_NON_OWNING_HOST = 1u << 0,
    JUBE_TYPE_OWNING_NATIVE = 1u << 1,
} JubeTypeFlags;

struct JubeHostObjectOps {
    int (*get_property)(Item receiver, Item key, Item* out);
    int (*set_property)(Item receiver, Item key, Item value, Item* out);
    int (*call_method)(Item receiver, Item method_name, Item* args, int argc, Item* out);
    int (*has_property)(Item receiver, Item key, Item* out);
    int (*delete_property)(Item receiver, Item key, Item* out);
    int (*get_own_property_descriptor)(Item receiver, Item key, Item* out);
    int (*own_property_keys)(Item receiver, Item* out);
    Item (*prototype)(Item receiver);
    void (*invalidate)(Item receiver);
    void (*destroy)(void* native);
};

struct JubeTypeDef {
    const char* name;
    uint32_t flags;
    const void* vmap_ops;
    const JubeHostObjectOps* host_ops;
    // Deprecated for host objects; use host_ops->destroy so the lifecycle
    // surface stays with the rest of the native object protocol.
    void (*destroy)(void* native);
};

struct JubeFuncDef {
    const char* name;
    const char* signature;
    fn_ptr func;
    uint32_t flags;
    const char* native_signature;
    fn_ptr native_func;
};

struct JubeNamespaceDef {
    const char* const* specifiers;
    int32_t specifier_count;
    Item (*build)(void);
    const JubeFuncDef* funcs;
    int32_t func_count;
};

// DOM3: binding-table halves of the module interface declaration.
// Shape (names, types, purity, arity, defaults) lives in Lambda type syntax in
// JubeModuleDef.interface_decl; behavior lives here as handler pointers.
// Handlers return 1 when handled, 0 to fall through, and use the
// pending-exception model (no unwinding across the module boundary).
typedef enum JubeMemberFlags {
    JUBE_MEMBER_NONE = 0,
    JUBE_MEMBER_NON_ENUMERABLE = 1u << 0,  // excluded from own-key enumeration
                                           //   (aliases like baseNode/extentNode)
} JubeMemberFlags;

typedef struct JubeMemberBind {
    const char* name;         // snake_case; must match a declared interface member
    const char* js_name;      // optional camelCase override for irregular names
                              //   (innerHTML, namespaceURI, ...); NULL = derived
    const char* applies_to;   // optional lowercase tag-list guard ("input select")
    int (*guard)(Item receiver);                     // optional extra predicate
    int (*get)(Item receiver, Item* out);
    int (*set)(Item receiver, Item value, Item* out);            // absent = readonly
    int (*call)(Item receiver, Item* args, int argc, Item* out); // methods
    const char* reflect_attr; // attribute-reflected member: generic reflect routine
                              //   handles get/set; no handler functions needed
    uint32_t flags;           // JubeMemberFlags
} JubeMemberBind;

typedef struct JubeTypeBinding {
    const char* type_name;    // matches `type X { ... }` in interface_decl
    const JubeTypeDef* host_brand;  // the JubeTypeDef used as vmap->host_type
    const JubeMemberBind* members;
    int32_t member_count;
    // open-name catch-alls (WebIDL named/indexed getters); any may be NULL
    int (*named_get)(Item receiver, Item key, Item* out);
    int (*named_set)(Item receiver, Item key, Item value, Item* out);
    int (*indexed_get)(Item receiver, int64_t index, Item* out);
    int (*indexed_set)(Item receiver, int64_t index, Item value, Item* out);
    // optional existing prototype object for this type (e.g. the engine's
    // Range.prototype); when set, jube_type_prototype adopts it instead of
    // creating a fresh object so constructor/instanceof identity is preserved
    Item (*prototype_seed)(void);
    // open-name membership for `in`/has: answers whether a non-declared name
    // exists (e.g. a CSS property name on a style object) without running the
    // named getter; NULL = named names are not part of `has`
    int (*named_has)(Item receiver, Item key, Item* out);
    // object-operation hooks for large WebIDL surfaces whose descriptor,
    // own-key, delete, and prototype semantics are receiver-specific. These
    // are record-owned hooks, not legacy host_ops fallbacks.
    int (*object_call)(Item receiver, Item method_name, Item* args, int argc, Item* out);
    int (*object_has)(Item receiver, Item key, Item* out);
    int (*object_delete)(Item receiver, Item key, Item* out);
    int (*object_descriptor)(Item receiver, Item key, Item* out);
    int (*object_own_keys)(Item receiver, Item* out);
    int (*object_prototype)(Item receiver, Item* out);
    // TRANSITIONAL (Phase 4 migration): when set, record misses delegate to
    // this legacy host-ops table instead of the generic expando/prototype
    // paths, so a large type (dom_node) converts cluster-by-cluster while
    // unconverted names, side-table expandos, per-kind prototypes, and
    // own-key semantics keep their existing behavior. Removed when the type
    // finishes converting.
    const JubeHostObjectOps* legacy_ops;
} JubeTypeBinding;

struct JubeHostGcAPI {
    void (*register_root)(uint64_t* slot);
    void (*unregister_root)(uint64_t* slot);
};

struct JubeHostValueAPI {
    Item (*vmap_new)(void);
    Item (*new_object)(void);
    Item (*array_new)(int capacity);
    Item (*array_push)(Item array, Item value);
    Item (*property_get)(Item object, Item key);
    Item (*property_set)(Item object, Item key, Item value);
};

struct JubeHostScriptAPI {
    Item (*new_function)(void* func_ptr, int param_count);
    void (*function_set_prototype)(Item fn_item, Item proto);
    void (*set_function_name)(Item fn_item, Item name_item);
    void (*mark_non_enumerable)(Item object, Item name);
    Item (*global_this)(void);
    Item (*global_property)(Item key);
    Item (*new_error_with_name)(Item error_name, Item message);
    void (*throw_value)(Item error);
    Item (*reflect_own_keys)(Item obj);
    Item (*reflect_delete_property)(Item obj, Item key);
    Item (*call_function)(Item func_item, Item this_val, Item* args, int arg_count);
    int (*check_exception)(void);
    bool (*is_truthy)(Item value);
    Item (*intrinsic_prototype_for_class)(int class_id);
};

struct JubeHostDomAPI {
    void* (*get_document)(void);
    Item (*get_document_object_value)(void);
    void* (*get_or_create_doc_node)(void* doc);
    Item (*document_proxy_for_doc_bridge)(void* doc);
    void* (*unwrap_element_impl)(Item item);
    void (*initialize_node_wrapper)(void* dom_elem);
    bool (*is_css_namespace)(Item item);
    bool (*is_inline_style_item)(Item item);
    bool (*is_computed_style_item)(Item item);
    bool (*is_stylesheet)(Item item);
    bool (*is_css_rule)(Item item);
    bool (*is_rule_style_decl)(Item item);
    Item (*dom_get_property_impl)(Item elem_item, Item prop_name);
    Item (*dom_set_property_impl)(Item elem_item, Item prop_name, Item value);
    Item (*dom_element_method_impl)(Item elem_item, Item method_name, Item* args, int argc);
    Item (*computed_style_get_property)(Item style_item, Item prop_name);
    bool (*style_resource_has_property)(Item style_item, Item prop_name);
    Item (*style_method)(Item elem_item, Item method_name, Item* args, int argc);
    Item (*dom_get_prototype_value)(Item obj);
    bool (*cssom_resource_has_property)(Item item, Item prop_name);
    Item (*cssom_stylesheet_get_property)(Item sheet_item, Item prop_name);
    Item (*cssom_rule_get_property)(Item rule_item, Item prop_name);
    Item (*cssom_rule_set_property)(Item rule_item, Item prop_name, Item value);
    Item (*cssom_rule_decl_get_property)(Item decl_item, Item prop_name);
    Item (*cssom_rule_decl_set_property)(Item decl_item, Item prop_name, Item value);
    void* (*get_foreign_doc)(Item item);
    void* (*swap_active_document)(void* new_doc);
    void (*restore_active_document)(void* prev_doc);
    Item (*document_proxy_get_property)(Item prop_name);
    Item (*document_proxy_set_property)(Item prop_name, Item value);
    Item (*document_proxy_method)(Item method_name, Item* args, int argc);
    bool (*item_is_range)(Item item);
    bool (*item_is_selection)(Item item);
    Item (*range_get_property)(Item obj, Item key);
    Item (*range_set_property)(Item obj, Item key, Item value);
    Item (*selection_get_property)(Item obj, Item key);
    Item (*selection_set_property)(Item obj, Item key, Item value);
    Item (*range_get_prototype_value)(void);
    Item (*selection_get_prototype_value)(void);
    bool (*range_native_property)(Item obj, Item key);
    bool (*selection_native_property)(Item obj, Item key);
    bool (*expando_has_property)(Item obj, Item key);
    bool (*range_expando_has_property)(Item obj, Item key);
    bool (*selection_expando_has_property)(Item obj, Item key);
    Item (*expando_get_own_property_descriptor)(Item obj, Item key);
    Item (*range_expando_get_own_property_descriptor)(Item obj, Item key);
    Item (*selection_expando_get_own_property_descriptor)(Item obj, Item key);
    Item (*expando_delete_property)(Item obj, Item key);
    Item (*range_expando_delete_property)(Item obj, Item key);
    Item (*selection_expando_delete_property)(Item obj, Item key);
    Item (*expando_own_property_names)(Item obj);
    Item (*range_expando_own_property_names)(Item obj);
    Item (*selection_expando_own_property_names)(Item obj);
    Item (*css_namespace_method)(Item obj, Item method_name, Item* args, int argc);
    Item (*cssom_stylesheet_method)(Item sheet_item, Item method_name, Item* args, int argc);
    Item (*cssom_rule_decl_method)(Item decl_item, Item method_name, Item* args, int argc);
    Item (*owner_document_for_node)(void* node);
    const char* (*to_attribute_cstr)(Item value);
    void (*after_set_attribute)(void* elem, const char* attr_name, const char* attr_value);
    void (*after_remove_attribute)(void* elem, const char* attr_name);
    void (*after_toggle_attribute_remove)(void* elem, const char* attr_name);
    void (*after_disabled_attribute_set)(void* elem);
    void (*after_default_checked_set)(void* elem, bool checked);
    void (*after_default_selected_set)(void* elem, bool selected);
    void (*after_select_multiple_removed)(void* elem);
    void (*set_checked_dirty)(void* elem, bool checked);
    void (*select_set_value_bridge)(void* elem, const char* value);
    void (*select_set_selected_index_bridge)(void* elem, Item value);
    void (*select_set_length_bridge)(void* elem, Item value);
    void (*set_option_selected_dirty)(void* elem, bool selected);
    void (*set_option_text_bridge)(void* elem, const char* value);
    void (*after_srcdoc_set)(void* elem);
    void (*throw_contenteditable_syntax_error)(void);
    Item (*set_text_data_property)(void* text, Item value);
    Item (*text_control_set_value_bridge)(void* elem, Item value);
    Item (*text_control_set_selection_start_bridge)(void* elem, Item value);
    Item (*text_control_set_selection_end_bridge)(void* elem, Item value);
    Item (*text_control_set_selection_direction_bridge)(void* elem, Item value);
    Item (*text_control_set_default_value_bridge)(void* elem, Item value);
    Item (*text_control_set_selection_range_bridge)(void* elem, Item start, Item end, Item dir);
    Item (*text_control_set_range_text_bridge)(void* elem, Item replacement, Item start,
                                               Item end, Item mode);
    Item (*text_control_select_bridge)(void* elem);
    Item (*form_reset_bridge)(Item form_item);
    Item (*check_validity_bridge)(Item elem_item);
    Item (*report_validity_bridge)(Item elem_item);
    Item (*form_submit_bridge)(Item form_item);
    Item (*form_request_submit_bridge)(Item form_item, Item submitter);
    Item (*focus_method_bridge)(void* elem, bool focus);
    Item (*click_method_bridge)(Item elem_item);
    Item (*add_event_listener_bridge)(Item target_item, Item type, Item callback, Item opts);
    Item (*remove_event_listener_bridge)(Item target_item, Item type, Item callback, Item opts);
    Item (*dispatch_event_bridge)(Item target_item, Item event_item);
    Item (*get_bounding_client_rect_bridge)(void* elem);
    Item (*get_client_rects_bridge)(void* elem);
    Item (*scroll_into_view_bridge)(void* elem);
    Item (*scroll_method_bridge)(Item elem_item, Item method_name, Item* args, int argc);
    Item (*text_control_caret_bounds_bridge)(void* elem);
    Item (*text_control_boundary_from_point_bridge)(void* elem, Item x, Item y);
    Item (*boundary_from_point_bridge)(void* elem, Item x, Item y, Item behavior);
    Item (*style_set_property_bridge)(void* elem, Item prop, Item value,
                                      Item priority, bool has_priority);
    Item (*style_remove_property_bridge)(void* elem, Item prop);
    Item (*text_replace_data_bridge)(void* text, Item offset, Item count, Item data);
    Item (*text_insert_data_bridge)(void* text, Item offset, Item data);
    Item (*text_append_data_bridge)(void* text, Item data);
    Item (*text_delete_data_bridge)(void* text, Item offset, Item count);
    Item (*text_substring_data_bridge)(void* text, Item offset, Item count);
    Item (*append_child_bridge)(void* parent, Item child);
    Item (*remove_child_bridge)(void* parent, Item child);
    Item (*insert_before_bridge)(void* parent, Item new_child, Item ref_child);
    Item (*remove_bridge)(void* node);
    Item (*adopt_node_bridge)(Item node);
    Item (*location_method_bridge)(void* doc, Item method_name, Item* args, int argc);
    Item (*document_open_bridge)(void* doc);
    Item (*document_write_bridge)(void* doc, Item text);
    Item (*document_element_from_point_bridge)(void* doc, Item x, Item y);
    Item (*create_range)(void);
    Item (*get_selection)(void);
    Item (*get_selection_function_for_document)(void* doc);
    bool (*doc_has_browsing_context)(void* doc);
    Item (*document_fonts_bridge)(void);
    Item (*document_stylesheets_bridge)(void);
    Item (*document_default_view_bridge)(void* doc);
    Item (*document_implementation_bridge)(void);
    Item (*document_design_mode_bridge)(void);
    Item (*document_active_element_bridge)(void* doc);
    Item (*normalize_bridge)(void* elem);
    Item (*live_child_collection_bridge)(void* elem, bool elements_only);
    Item (*live_document_forms_bridge)(void* doc);
    Item (*live_form_elements_bridge)(void* elem);
    Item (*live_document_get_elements_by_tag_name_bridge)(void* doc, Item query);
    Item (*live_document_get_elements_by_class_name_bridge)(void* doc, Item query);
    Item (*live_document_get_elements_by_name_bridge)(void* doc, Item query);
    Item (*live_element_get_elements_by_tag_name_bridge)(void* elem, Item query);
    Item (*live_element_get_elements_by_class_name_bridge)(void* elem, Item query);
    Item (*clone_node_bridge)(void* elem, Item deep, bool has_deep);
    Item (*replace_child_bridge)(void* parent, Item new_child, Item old_child);
    Item (*replace_with_bridge)(void* node, Item* args, int argc);
    Item (*insert_adjacent_element_bridge)(void* elem, Item position, Item new_node);
    Item (*insert_adjacent_html_bridge)(void* elem, Item position, Item html);
    Item (*append_variadic_bridge)(void* elem, Item* args, int argc);
    Item (*prepend_variadic_bridge)(void* elem, Item* args, int argc);
    void (*notify_mutation)(int kind, void* target, void* parent);
    void (*notify_mutation_detail)(int kind, void* target, void* parent,
                                   const char* attribute_name, const char* old_value);

    // -- DOM3 Phase 1 additive tail: receiver-explicit Range/Selection behavior.
    // These carry the behavior the deleted strcmp chains used to reach through
    // cached method objects; the radiant module's declared-interface bindings
    // are their only callers.
    Item (*range_get_start_container)(Item self);
    Item (*range_get_start_offset)(Item self);
    Item (*range_get_end_container)(Item self);
    Item (*range_get_end_offset)(Item self);
    Item (*range_get_collapsed)(Item self);
    Item (*range_get_common_ancestor)(Item self);
    Item (*range_set_start)(Item self, Item node, Item offset);
    Item (*range_set_end)(Item self, Item node, Item offset);
    Item (*range_set_start_before)(Item self, Item node);
    Item (*range_set_start_after)(Item self, Item node);
    Item (*range_set_end_before)(Item self, Item node);
    Item (*range_set_end_after)(Item self, Item node);
    Item (*range_collapse)(Item self, Item to_start);
    Item (*range_select_node)(Item self, Item node);
    Item (*range_select_node_contents)(Item self, Item node);
    Item (*range_clone_range)(Item self);
    Item (*range_compare_boundary_points)(Item self, Item how, Item other);
    Item (*range_compare_point)(Item self, Item node, Item offset);
    Item (*range_is_point_in_range)(Item self, Item node, Item offset);
    Item (*range_intersects_node)(Item self, Item node);
    Item (*range_detach)(Item self);
    Item (*range_to_string)(Item self);
    Item (*range_get_client_rects)(Item self);
    Item (*range_get_bounding_client_rect)(Item self);
    Item (*range_delete_contents)(Item self);
    Item (*range_extract_contents)(Item self);
    Item (*range_clone_contents)(Item self);
    Item (*range_insert_node)(Item self, Item node);
    Item (*range_surround_contents)(Item self, Item node);
    Item (*selection_get_anchor_node)(Item self);
    Item (*selection_get_anchor_offset)(Item self);
    Item (*selection_get_focus_node)(Item self);
    Item (*selection_get_focus_offset)(Item self);
    Item (*selection_get_is_collapsed)(Item self);
    Item (*selection_get_range_count)(Item self);
    Item (*selection_get_type)(Item self);
    Item (*selection_get_direction)(Item self);
    Item (*selection_get_range_at)(Item self, Item index);
    Item (*selection_add_range)(Item self, Item range);
    Item (*selection_remove_range)(Item self, Item range);
    Item (*selection_remove_all_ranges)(Item self);
    Item (*selection_empty)(Item self);
    Item (*selection_collapse)(Item self, Item node, Item offset);
    Item (*selection_set_position)(Item self, Item node, Item offset);
    Item (*selection_collapse_to_start)(Item self);
    Item (*selection_collapse_to_end)(Item self);
    Item (*selection_extend)(Item self, Item node, Item offset);
    Item (*selection_set_base_and_extent)(Item self, Item anchor_node, Item anchor_offset,
                                          Item focus_node, Item focus_offset);
    Item (*selection_select_all_children)(Item self, Item node);
    Item (*selection_contains_node)(Item self, Item node, Item allow_partial);
    Item (*selection_delete_from_document)(Item self);
    Item (*selection_to_string)(Item self);
    Item (*selection_modify)(Item self, Item alter, Item direction, Item granularity);
    Item (*selection_force_direction)(Item self, Item direction);

    // -- DOM3 Phase 3 additive tail: style-host behavior.
    // style_get/set_property take the OWNER ELEMENT item (inline-style wrappers
    // carry the owner as host_data; adapters wrap it before calling).
    Item (*style_get_property)(Item owner_elem, Item prop);
    Item (*style_set_property)(Item owner_elem, Item prop, Item value);
    Item (*style_css_has)(Item style, Item prop);

    // -- DOM3 Phase 2 additive tail: CSSOM behavior.
    Item (*stylesheet_get_css_rules)(Item sheet);
    Item (*stylesheet_get_length)(Item sheet);
    Item (*stylesheet_get_disabled)(Item sheet);
    Item (*stylesheet_get_type)(Item sheet);
    Item (*stylesheet_get_href)(Item sheet);
    Item (*stylesheet_get_title)(Item sheet);
    Item (*stylesheet_index)(Item sheet, int64_t index);
    Item (*stylesheet_insert_rule)(Item sheet, Item text, Item index);
    Item (*stylesheet_delete_rule)(Item sheet, Item index);
    Item (*rule_get_selector_text)(Item rule);
    Item (*rule_set_selector_text)(Item rule, Item value);
    Item (*rule_get_style)(Item rule);
    Item (*rule_get_css_rules)(Item rule);
    Item (*rule_get_css_text)(Item rule);
    Item (*rule_get_type)(Item rule);
    Item (*rule_get_parent_rule)(Item rule);
    Item (*rule_decl_remove_property)(Item decl, Item prop);
    Item (*rule_decl_css_has)(Item decl, Item prop);

    // -- Radiant browser-global state. Kept behind the host boundary so the
    // module owns DOM-facing window semantics without reaching into js_dom.cpp.
    void* (*get_ui_context)(void);
    bool (*force_layout_for_geometry)(void* doc);
};

struct JubeHostAPI {
    uint32_t api_version;
    const JubeHostGcAPI* gc;
    const JubeHostValueAPI* value;
    const JubeHostScriptAPI* script;
    const JubeHostDomAPI* dom;
};

struct JubeModuleDef {
    uint32_t abi_version;
    uint32_t struct_size;
    const char* name;
    const char* version;
    const char* description;

    const JubeTypeDef* types;
    int32_t type_count;
    const JubeFuncDef* functions;
    int32_t function_count;
    const JubeNamespaceDef* namespaces;
    int32_t namespace_count;

    int (*init)(const JubeHostAPI* host);
    void (*shutdown)(void);

    // -- DOM3 additive tail --
    // JubeModuleDef is always passed by pointer (never embedded in arrays), so
    // appending fields is ABI-safe: the registry gates access on struct_size and
    // accepts v1 modules whose struct_size stops at JUBE_MODULE_DEF_V1_SIZE.
    const char* interface_decl;            // Lambda-type-syntax module interface
    const JubeTypeBinding* type_bindings;  // one per declared type
    int32_t type_binding_count;
    void (*runtime_reset)(void);            // drop JS heap-backed module caches

    // Optional cleanup for values rooted in one Lambda heap. Called while the
    // heap is active, immediately before that runtime destroys it.
    void (*heap_cleanup)(void* heap);
};

// Size of the frozen v1 layout: everything before the DOM3 additive tail.
#define JUBE_MODULE_DEF_V1_SIZE offsetof(JubeModuleDef, interface_decl)

#ifdef __cplusplus
}
#endif
