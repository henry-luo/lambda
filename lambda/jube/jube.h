#pragma once

#include "../lambda.h"
#include "../../lib/side_stack.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JUBE_ABI_VERSION 2
#define JUBE_ABI_VERSION_LEGACY 1
#define JUBE_HOST_API_VERSION 1
#define JUBE_HOST_LANG_API_VERSION 1

// Hosted compiler services are intentionally build-coupled while their opaque
// handle contracts evolve. A module validates this at registration, never in
// an evaluation or generated-code path.
// Bump this exact-build compiler contract whenever an opaque hosted-compiler
// service table changes; struct-size checks alone cannot identify stale module
// binaries built against a prior same-day table shape.
#define JUBE_HOST_BUILD_ID "lambda-hosted-lang-20260721-h7e9"

typedef struct JubeHostAPI JubeHostAPI;
typedef struct JubeTypeDef JubeTypeDef;
typedef struct JubeFuncDef JubeFuncDef;
typedef struct JubeNamespaceDef JubeNamespaceDef;
typedef struct JubeModuleDef JubeModuleDef;
typedef struct JubeHostObjectOps JubeHostObjectOps;
typedef struct JubeHostGcAPI JubeHostGcAPI;
typedef struct JubeRootFrame JubeRootFrame;
typedef struct JubeHostRootAPI JubeHostRootAPI;
typedef struct JubeHostDataAPI JubeHostDataAPI;
typedef struct JubeHostValueAPI JubeHostValueAPI;
typedef struct JubeHostScriptAPI JubeHostScriptAPI;
typedef struct JubeHostDomAPI JubeHostDomAPI;
typedef struct JubeHostLangAPI JubeHostLangAPI;
typedef struct JubeSourceAPI JubeSourceAPI;
typedef struct JubeDiagnosticAPI JubeDiagnosticAPI;
typedef struct JubeOutputAPI JubeOutputAPI;
typedef struct JubeSessionMemoryAPI JubeSessionMemoryAPI;
typedef struct JubeGuestExecutionAPI JubeGuestExecutionAPI;
typedef struct JubeModuleGraphAPI JubeModuleGraphAPI;
typedef struct JubeRuntimeCatalogAPI JubeRuntimeCatalogAPI;
typedef struct JubeRuntimeImport JubeRuntimeImport;
typedef struct JubeRuntimeImportMetadata JubeRuntimeImportMetadata;
typedef struct JubeModuleNamespaceOps JubeModuleNamespaceOps;
typedef struct JubeLanguageDef JubeLanguageDef;
typedef struct JubeLanguageSession JubeLanguageSession;
typedef struct JubeLanguageSessionConfig JubeLanguageSessionConfig;
typedef struct JubeLanguageRunRequest JubeLanguageRunRequest;
typedef struct JubeLanguageModuleRequest JubeLanguageModuleRequest;
typedef struct JubeHostedSource JubeHostedSource;
typedef struct JubeHostedDiagnostic JubeHostedDiagnostic;

typedef enum JubeHostCapability {
    JUBE_HOST_CAP_NONE = 0,
    JUBE_HOST_CAP_GC_ROOTS = 1ull << 0,
    JUBE_HOST_CAP_NEUTRAL_DATA = 1ull << 1,
    JUBE_HOST_CAP_RUNTIME_CATALOG = 1ull << 2,
    JUBE_HOST_CAP_MODULE_GRAPH = 1ull << 3,
    JUBE_HOST_CAP_GUEST_EXECUTION = 1ull << 4,
    JUBE_HOST_CAP_COMPILER = 1ull << 5,
} JubeHostCapability;

// Hosted-language service capabilities are separate from the base host
// capability mask.  A language asks only for the slices it consumes, so a
// later compiler service can be added without making it part of Lambda or JS
// execution startup.
typedef enum JubeHostedLanguageCapability {
    JUBE_HOSTED_LANG_CAP_NONE = 0,
    JUBE_HOSTED_LANG_CAP_SOURCE = 1ull << 32,
    JUBE_HOSTED_LANG_CAP_DIAGNOSTICS = 1ull << 33,
    JUBE_HOSTED_LANG_CAP_RESULT_FORMAT = 1ull << 34,
    JUBE_HOSTED_LANG_CAP_SESSION_MEMORY = 1ull << 35,
    JUBE_HOSTED_LANG_CAP_EXECUTION = 1ull << 36,
    JUBE_HOSTED_LANG_CAP_MODULE_GRAPH = 1ull << 37,
} JubeHostedLanguageCapability;

typedef enum JubeHostedDiagnosticSeverity {
    JUBE_HOSTED_DIAGNOSTIC_ERROR = 1,
    JUBE_HOSTED_DIAGNOSTIC_WARNING = 2,
    JUBE_HOSTED_DIAGNOSTIC_NOTE = 3,
} JubeHostedDiagnosticSeverity;

// The host owns the buffers in this record.  The module must call
// source_release exactly once after source_read succeeds; source bytes remain
// valid until then and are always NUL terminated for existing parsers.
struct JubeHostedSource {
    uint32_t struct_size;
    const char* bytes;
    size_t byte_length;
    const char* canonical_path;
    void* host_owner;
};

// Locations are byte offsets in the source supplied to source_read.  A zero
// offset/length is valid for a file-level diagnostic.
struct JubeHostedDiagnostic {
    uint32_t struct_size;
    uint32_t severity;
    const JubeHostedSource* source;
    size_t byte_offset;
    size_t byte_length;
    const char* message;
};

// A hosted language describes only its own runtime helpers.  The host owns
// validation, collision handling, metadata defaults, and resolver storage, so
// generated calls retain their existing direct targets without a per-call
// language lookup.
struct JubeRuntimeImport {
    const char* name;
    fn_ptr function;
};

// This mirrors the reviewed, fixed-width import-effect contract without
// publishing the core JitImportMetadata layout to a hosted language module.
// Values are copied by the host and remain compile-time metadata only.
struct JubeRuntimeImportMetadata {
    uint32_t gc_effect;
    uint32_t reentry_effect;
    uint32_t result_class;
    uint32_t argument_classes;
    uint32_t flags;
    uint32_t exception_effect;
    uint32_t argument_effects;
};

struct JubeRuntimeCatalogAPI {
    uint32_t api_version;
    uint32_t struct_size;
    int (*register_imports)(const JubeRuntimeImport* imports, int import_count,
                            const char* owner_name);
    // Resolves the host-owned effect contract for a generated call. The
    // lookup occurs during lowering; generated code keeps its direct target.
    int (*lookup_import_metadata)(const char* name,
                                  JubeRuntimeImportMetadata* out_metadata);
};

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

typedef void (*JubeGcWeakClearFn)(uint64_t* slot, void* context);

// Guest code may reserve this object on its native stack, but its storage is
// private to the host.  That prevents hosted languages from depending on
// LambdaRootFrame's runtime pointer and side-stack watermark layout.
struct JubeRootFrame {
    uintptr_t storage[8];
};

struct JubeHostGcAPI {
    void (*register_root)(uint64_t* slot);
    void (*unregister_root)(uint64_t* slot);
    bool (*root_frame_begin)(LambdaRootFrame* frame, size_t slot_count);
    uint64_t* (*root_frame_take_slot)(LambdaRootFrame* frame);
    void (*root_frame_end)(LambdaRootFrame* frame);
    // Preserve the released v2 root-frame offsets; new GC capabilities are an
    // additive tail so existing rooting-capable modules keep their ABI.
    void (*register_weak)(uint64_t* slot, JubeGcWeakClearFn on_clear, void* context);
    void (*unregister_weak)(uint64_t* slot);
};

// This independently versioned table replaces the legacy JubeHostGcAPI root
// signatures for hosted languages.  It is an additive hosted-language tail so
// existing generic modules retain their unchanged GC table ABI.
struct JubeHostRootAPI {
    uint32_t api_version;
    uint32_t struct_size;
    bool (*root_frame_begin)(JubeRootFrame* frame, size_t slot_count);
    uint64_t* (*root_frame_take_slot)(JubeRootFrame* frame);
    void (*root_frame_end)(JubeRootFrame* frame);
    // Persistent slots belong to a guest runtime, not to a stack frame.  The
    // session token prevents a module from registering roots in another run.
    int (*persistent_root_register)(void* session, uint64_t* slot);
};

// Language-neutral projection of Lambda Item/container mechanics.  The
// session is an opaque host token valid only during an active guest execution
// on this thread.  JavaScript property/prototype/coercion semantics are
// intentionally absent. Returned Items are borrowed and must be rooted by a
// caller before an operation that can allocate or re-enter the host.
struct JubeHostDataAPI {
    uint32_t api_version;
    uint32_t struct_size;
    Item (*name_from_utf8)(void* session, const char* text);
    int (*map_set)(void* session, Item map, Item key, Item value);
    Item (*float_from_f64)(void* session, double value);
    Item (*format_json)(void* session, Item value);
    // Closure environments are neutral traced Item storage.  The host retains
    // allocation layout, bounds validation, and write barriers; Python owns
    // only closure semantics and may not inspect Context/GC internals.
    void* (*closure_env_alloc)(void* session, size_t item_count);
    int (*closure_env_store)(void* session, void* environment, int slot, Item value);
    int (*closure_env_load)(void* session, void* environment, int slot, Item* out_value);
    int (*item_slots_store)(void* session, Item* storage, int64_t item_count,
                            int64_t slot, Item value);
    // Moves a scalar payload out of the current activation's number stack so
    // an Item retained by guest TLS remains valid after that frame returns.
    Item (*item_heap_rehome)(void* session, Item value);
    // Standard host allocations retain their existing object layouts and GC
    // registration; a hosted language receives only the resulting Item.
    Item (*map_new)(void* session);
    Item (*function_new)(void* session, void* function_ptr, int param_count);
};

struct JubeHostValueAPI {
    Item (*vmap_new)(void);
    Item (*new_object)(void);
    Item (*array_new)(int capacity);
    Item (*array_push)(Item array, Item value);
    int64_t (*array_length)(Item array);
    Item (*array_get)(Item array, int64_t index);
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
    Item (*make_number)(double value);
    double (*get_number)(Item value);
    Item (*date_new_from)(Item value);
    Item (*date_method)(Item date, int method_id);
    int (*class_id)(Item value);
    Item (*to_string)(Item value);
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
    bool (*has_committed_geometry_snapshot)(void* doc);
};

// Each hosted service table evolves independently. A module checks both the
// containing hosted-language table and the specific service table before it
// dereferences a function pointer.
struct JubeSourceAPI {
    uint32_t api_version;
    uint32_t struct_size;
    int (*source_read)(const char* path, JubeHostedSource* out_source);
    void (*source_release)(JubeHostedSource* source);
    int (*source_line_column)(const JubeHostedSource* source, size_t byte_offset,
                              size_t* out_line, size_t* out_column);
};

struct JubeDiagnosticAPI {
    uint32_t api_version;
    uint32_t struct_size;
    void (*write_diagnostic)(const JubeLanguageRunRequest* request,
                             const JubeHostedDiagnostic* diagnostic);
};

struct JubeOutputAPI {
    uint32_t api_version;
    uint32_t struct_size;
    void (*write_result)(const JubeLanguageRunRequest* request, Item result);
};

struct JubeSessionMemoryAPI {
    uint32_t api_version;
    uint32_t struct_size;
    void* (*session_alloc)(size_t size);
    void (*session_free)(void* memory);
};

struct JubeGuestExecutionAPI {
    uint32_t api_version;
    uint32_t struct_size;
    // Creates/destroys a host-owned runtime activation for one guest run. The
    // returned token is opaque; later compiler services replace its temporary
    // bridge use without exposing Runtime layout in this ABI.
    void* (*execution_create)(void);
    void (*execution_destroy)(void* execution_context);
    // Owns the host import resolver used when a guest-finalized MIR module is
    // linked. The guest supplies an opaque MIR context only; it never receives
    // the resolver function or performs symbol lookup itself.
    int (*execution_link_module)(void* execution_context, void* mir_context);
    // These opaque compiler-lifecycle operations preserve the existing MIR/JIT
    // implementation while keeping context ownership, module loading, and
    // generated-function lookup in the host. They are compile/link-time only;
    // Lambda and JavaScript execution paths never consult this table.
    void* (*mir_context_create)(int optimization_level);
    void (*mir_context_destroy)(void* mir_context);
    void* (*mir_module_create)(void* mir_context, const char* module_name);
    int (*mir_module_finalize_and_load)(void* mir_context, void* mir_module);
    void* (*mir_function_lookup)(void* mir_context, const char* function_name);
    // Completes the current function before the module can be finalized. The
    // guest never imports MIR_finish_func or observes the MIR context layout.
    void (*mir_function_finish)(void* mir_context);
    // Enters a host-owned guest activation and creates an opaque compilation
    // input. The guest may pass that input only to its own semantic adapter;
    // it never receives an EvalContext, Runtime, or Input layout.
    int (*execution_activate)(void* execution_context, void** out_input);
    // Activates a host-created import wrapper. A standalone activation remains
    // retained until the language releases it from its heap-cleanup hook;
    // nested imports restore their caller when the wrapper is destroyed.
    int (*execution_activate_import)(void* execution_context, void** out_input,
                                    bool* out_retained_until_heap_cleanup);
    // Executes a generated guest entry under the host recovery boundary. The
    // entry receives an opaque runtime token whose concrete type is private to
    // the host implementation.
    int (*execution_run_main)(void* execution_context, void* entry_function,
                              Item* out_result);
    // Completes a guest activation, restoring the saved thread-local state and
    // transferring any standalone result heap to its host owner.
    void (*execution_finish_guest)(void* execution_context);
    // Returns an opaque address whose current value is the active frame runtime.
    // It is valid only for the active execution and is consumed by the reviewed
    // shared frame emitter; the guest never names or accesses host storage.
    void* (*execution_frame_runtime_slot)(void* execution_context);
    // Creates a function whose result and parameters use Lambda's boxed Item
    // ABI. Parameter names are borrowed for this call only; the returned item
    // and function handles remain opaque and belong to the MIR context.
    // Keep this tail-appended: size-gated older guests retain every preceding
    // execution-service offset unchanged.
    int (*mir_item_function_create)(void* mir_context, const char* function_name,
                                    uint32_t parameter_count,
                                    const char* const* parameter_names,
                                    void** out_function_item, void** out_function);
    // Declares a forward target for an in-context direct call. The opaque item
    // is valid only until the owning MIR context is destroyed.
    int (*mir_function_forward_create)(void* mir_context, const char* function_name,
                                       void** out_function_item);
    // Creates the boxed-Item signature used by an in-context direct call.
    // The returned prototype is opaque and owned by the MIR context.
    int (*mir_item_function_proto_create)(void* mir_context, const char* prototype_name,
                                          uint32_t parameter_count,
                                          void** out_prototype_item);
    // Resolves a register name within an opaque, context-bound function.
    int (*mir_function_register_lookup)(void* mir_context, void* function,
                                        const char* register_name,
                                        uint32_t* out_register);
};

// Module graph operations retain host path/state ownership. The execution
// context is opaque to a language module and is only associated with Runtime
// state by the host implementation.
struct JubeModuleGraphAPI {
    uint32_t api_version;
    uint32_t struct_size;
    // Returns 1 and a partial namespace for a circular import, 0 when none is
    // loading, and -1 for an invalid request.
    int (*loading_namespace)(void* execution_context, const char* source_path,
                             Item* out_namespace);
    // Loads a Lambda source module through the host importer. Returns 0 on
    // success and retains the namespace's language-owned representation.
    int (*load_lambda_module)(void* execution_context, const char* source_path,
                              const char* importer_path, Item* out_namespace);
    // Reports 0 for absent, 1 for loading, and 2 for initialized modules.
    // A non-absent state returns the language-owned namespace membrane.
    int (*module_state)(void* execution_context, const char* source_path,
                        Item* out_namespace);
    // Publishes a partial namespace before guest execution and replaces it
    // with the completed namespace after compilation. The host owns all graph
    // records and only retains the module's supplied export membrane.
    int (*module_begin_loading)(void* execution_context, const char* source_path,
                                const char* language,
                                const JubeModuleNamespaceOps* namespace_ops);
    int (*module_publish)(void* execution_context, const char* source_path,
                          const char* language, Item namespace_obj, void* mir_context,
                          const JubeModuleNamespaceOps* namespace_ops);
};

struct JubeModuleNamespaceOps {
    Item (*create)(void);
    Item (*get)(Item namespace_obj, const char* name);
    int (*function_arity)(Item function_obj);
    void* (*function_ptr)(Item function_obj);
};

// This table establishes the hosted-language negotiation boundary before
// compiler operations are exposed. Future compiler services join it as opaque,
// size-gated sub-tables rather than leaking Runtime, AST, or MIR layouts.
struct JubeHostLangAPI {
    uint32_t api_version;
    uint32_t struct_size;
    uint64_t capabilities;
    const char* host_build_id;
    const JubeSourceAPI* source;
    const JubeDiagnosticAPI* diagnostic;
    const JubeOutputAPI* output;
    const JubeSessionMemoryAPI* session_memory;
    const JubeGuestExecutionAPI* execution;
    const JubeModuleGraphAPI* module_graph;
    const JubeHostRootAPI* roots;
};

struct JubeHostAPI {
    uint32_t api_version;
    uint32_t struct_size;
    uint64_t capabilities;
    const char* host_build_id;
    const JubeHostLangAPI* hosted_language;
    const JubeHostGcAPI* gc;
    const JubeHostValueAPI* value;
    const JubeHostScriptAPI* script;
    const JubeHostDomAPI* dom;
    const JubeRuntimeCatalogAPI* runtime_catalog;
    // Additive tail: old generic Jube modules must size-gate this service.
    const JubeHostDataAPI* data;
};

// A hosted language owns parsing and language semantics while the host owns
// discovery and command routing. This initial file-oriented surface keeps
// Runtime and MIR implementation layouts out of the module boundary.
typedef void (*JubeLanguageWriteFn)(void* user, const char* bytes, size_t length);

struct JubeLanguageSessionConfig {
    uint32_t struct_size;
};

struct JubeLanguageRunRequest {
    uint32_t struct_size;
    const char* source_path;
    int32_t argc;
    const char* const* argv;
    bool show_help;
    void* output_user;
    JubeLanguageWriteFn write_stdout;
    JubeLanguageWriteFn write_stderr;
};

// This transitional request deliberately exposes only an opaque host context.
// The final hosted compiler API replaces it with a size-gated execution handle;
// guest code must never depend on the pointed-to layout.
struct JubeLanguageModuleRequest {
    uint32_t struct_size;
    void* host_context;
    const char* source_path;
    const char* importer_path;
};

struct JubeLanguageDef {
    uint32_t abi_version;
    uint32_t struct_size;
    const char* name;
    const char* const* aliases;
    int32_t alias_count;
    const char* const* extensions;
    int32_t extension_count;
    int (*create_session)(const JubeLanguageSessionConfig* config,
                          JubeLanguageSession** out_session);
    void (*destroy_session)(JubeLanguageSession* session);
    int (*run)(JubeLanguageSession* session, const JubeLanguageRunRequest* request);
    // Optional module loader. It returns a language-owned export membrane in
    // Item form; the host keeps only neutral path/state bookkeeping.
    int (*load_module)(JubeLanguageSession* session,
                       const JubeLanguageModuleRequest* request,
                       Item* out_namespace);

    // Additive capability/build negotiation tail. A pre-tail v1 descriptor
    // requests no hosted compiler services and remains valid for base modules.
    uint64_t required_host_capabilities;
    uint64_t required_hosted_capabilities;
    const char* required_host_build_id;
};

#define JUBE_LANGUAGE_ABI_VERSION 1
#define JUBE_LANGUAGE_DEF_V1_SIZE offsetof(JubeLanguageDef, required_host_capabilities)
#define JUBE_LANGUAGE_DEF_CAPABILITIES_SIZE sizeof(JubeLanguageDef)
#define JUBE_LANGUAGE_SESSION_CONFIG_V1_SIZE sizeof(JubeLanguageSessionConfig)
#define JUBE_LANGUAGE_RUN_REQUEST_V1_SIZE sizeof(JubeLanguageRunRequest)
#define JUBE_LANGUAGE_MODULE_REQUEST_V1_SIZE sizeof(JubeLanguageModuleRequest)
#define JUBE_HOSTED_SOURCE_V1_SIZE sizeof(JubeHostedSource)
#define JUBE_HOSTED_DIAGNOSTIC_V1_SIZE sizeof(JubeHostedDiagnostic)
#define JUBE_HOST_SERVICE_API_VERSION 1
#define JUBE_SOURCE_API_V1_SIZE sizeof(JubeSourceAPI)
#define JUBE_DIAGNOSTIC_API_V1_SIZE sizeof(JubeDiagnosticAPI)
#define JUBE_OUTPUT_API_V1_SIZE sizeof(JubeOutputAPI)
#define JUBE_SESSION_MEMORY_API_V1_SIZE sizeof(JubeSessionMemoryAPI)
#define JUBE_GUEST_EXECUTION_API_V1_SIZE offsetof(JubeGuestExecutionAPI, execution_frame_runtime_slot)
#define JUBE_GUEST_EXECUTION_API_H7C_RUNTIME_SLOT_SIZE offsetof(JubeGuestExecutionAPI, mir_item_function_create)
#define JUBE_GUEST_EXECUTION_API_H7C_FUNCTION_CREATE_SIZE offsetof(JubeGuestExecutionAPI, mir_function_forward_create)
#define JUBE_GUEST_EXECUTION_API_H7C_FORWARD_CREATE_SIZE offsetof(JubeGuestExecutionAPI, mir_item_function_proto_create)
#define JUBE_GUEST_EXECUTION_API_H7C_PROTO_CREATE_SIZE offsetof(JubeGuestExecutionAPI, mir_function_register_lookup)
#define JUBE_GUEST_EXECUTION_API_H7C_REGISTER_LOOKUP_SIZE sizeof(JubeGuestExecutionAPI)
#define JUBE_MODULE_GRAPH_API_V1_SIZE sizeof(JubeModuleGraphAPI)
#define JUBE_HOST_LANG_API_V1_SIZE offsetof(JubeHostLangAPI, source)
#define JUBE_HOST_LANG_API_H7A_SIZE offsetof(JubeHostLangAPI, execution)
#define JUBE_HOST_LANG_API_H6_EXECUTION_SIZE offsetof(JubeHostLangAPI, module_graph)
#define JUBE_HOST_LANG_API_H6_MODULE_GRAPH_SIZE sizeof(JubeHostLangAPI)
#define JUBE_RUNTIME_CATALOG_API_V1_SIZE sizeof(JubeRuntimeCatalogAPI)
#define JUBE_HOST_ROOT_API_V1_SIZE offsetof(JubeHostRootAPI, persistent_root_register)
#define JUBE_HOST_ROOT_API_H5_PERSISTENT_SIZE sizeof(JubeHostRootAPI)
#define JUBE_HOST_LANG_API_H7E2_ROOTS_SIZE sizeof(JubeHostLangAPI)
#define JUBE_HOST_API_RUNTIME_CATALOG_SIZE offsetof(JubeHostAPI, data)
#define JUBE_HOST_DATA_API_V1_SIZE offsetof(JubeHostDataAPI, closure_env_alloc)
#define JUBE_HOST_DATA_API_H5_CLOSURE_ENV_SIZE offsetof(JubeHostDataAPI, item_slots_store)
#define JUBE_HOST_DATA_API_H5_STORAGE_SIZE offsetof(JubeHostDataAPI, item_heap_rehome)
#define JUBE_HOST_DATA_API_H5_REHOME_SIZE offsetof(JubeHostDataAPI, map_new)
#define JUBE_HOST_DATA_API_H5_ALLOCATORS_SIZE sizeof(JubeHostDataAPI)
#define JUBE_HOST_API_DATA_SIZE sizeof(JubeHostAPI)

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

    // Hosted-language descriptor. This additive tail keeps ordinary native
    // modules independent of language registration and execution.
    const JubeLanguageDef* language;
};

// Size of the frozen v1 layout: everything before the DOM3 additive tail.
#define JUBE_MODULE_DEF_V1_SIZE offsetof(JubeModuleDef, interface_decl)

#ifdef __cplusplus
}
#endif
