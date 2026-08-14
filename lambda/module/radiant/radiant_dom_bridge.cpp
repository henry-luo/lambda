#include "../../lambda-data.hpp"
#include "../../io/mark_builder.hpp"
#include "../../jube/jube_registry.h"
#include "radiant_host_api.hpp"
#include "radiant_dom_bridge.hpp"
#include "radiant_input_value.hpp"
#include "../../jube/jube_interface.h"
#include "../../input/css/dom_node.hpp"
#include "../../input/css/dom_element.hpp"
#include "../../input/css/dom_lifecycle.hpp"
#include "../../input/css/css_tokenizer.hpp"
#include "../../input/css/selector_matcher.hpp"
#include "../../core/well_known_markup_names.h"
#include "../../js/js_class.h"
#include "../../js/js_dom.h"
#include "../../../radiant/view.hpp"
#include "../../../radiant/render.hpp"
#include "../../../radiant/event.hpp"
#include "../../../lib/log.h"
#include "../../../lib/hashmap.h"
#include "../../../lib/mem.h"
#include "../../../lib/str.h"
#include "../../../lib/strbuf.h"
#include "../../../lib/url.h"
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RADIANT_C_API const void* radiant_dom_node_host_type(void);
RADIANT_C_API const void* radiant_dom_html_element_host_type(void);
RADIANT_C_API const void* radiant_dom_document_host_type(void);
RADIANT_C_API const void* radiant_dom_character_data_host_type(void);
RADIANT_C_API const void* radiant_dom_svg_element_host_type(void);
RADIANT_C_API const void* radiant_dom_input_element_host_type(void);
RADIANT_C_API const void* radiant_dom_select_element_host_type(void);
RADIANT_C_API const void* radiant_dom_textarea_element_host_type(void);
RADIANT_C_API const void* radiant_dom_option_element_host_type(void);
RADIANT_C_API void radiant_dom_host_invalidate(Item object);
RADIANT_C_API Item radiant_dom_wrap_node(void* dom_elem);
RADIANT_C_API Item radiant_dom_get_property(Item elem_item, Item prop_name);
RADIANT_C_API Item js_dom_dataset_property(Item elem_item);

// The Phase-3 DOM hook table preserves the legacy hook names at call sites
// while routing every module-to-engine call through the checked host API.
#define js_dom_get_document radiant_host_api->dom->get_document
#define js_get_document_object_value radiant_host_api->dom->get_document_object_value
#define js_dom_get_or_create_doc_node radiant_host_api->dom->get_or_create_doc_node
#define js_dom_document_proxy_for_doc_bridge radiant_host_api->dom->document_proxy_for_doc_bridge
#define js_dom_unwrap_element_impl radiant_host_api->dom->unwrap_element_impl
#define js_dom_initialize_node_wrapper radiant_host_api->dom->initialize_node_wrapper
#define js_is_inline_style_item radiant_host_api->dom->is_inline_style_item
#define js_is_computed_style_item radiant_host_api->dom->is_computed_style_item
#define js_is_stylesheet radiant_host_api->dom->is_stylesheet
#define js_is_css_rule radiant_host_api->dom->is_css_rule
#define js_is_rule_style_decl radiant_host_api->dom->is_rule_style_decl
#define js_dom_get_property_impl radiant_host_api->dom->dom_get_property_impl
#define js_dom_set_property_impl radiant_host_api->dom->dom_set_property_impl
#define js_dom_element_operation_impl radiant_host_api->dom->dom_element_operation_impl
#define js_computed_style_get_property radiant_host_api->dom->computed_style_get_property
#define js_dom_style_resource_has_property radiant_host_api->dom->style_resource_has_property
#define js_dom_get_prototype_value radiant_host_api->dom->dom_get_prototype_value
#define js_get_intrinsic_prototype_for_class radiant_host_api->script->intrinsic_prototype_for_class
#define js_cssom_rule_decl_get_property radiant_host_api->dom->cssom_rule_decl_get_property
#define js_cssom_rule_decl_set_property radiant_host_api->dom->cssom_rule_decl_set_property
#define js_get_foreign_doc radiant_host_api->dom->get_foreign_doc
#define js_dom_swap_active_document radiant_host_api->dom->swap_active_document
#define js_dom_restore_active_document radiant_host_api->dom->restore_active_document
#define js_document_proxy_get_property radiant_host_api->dom->document_proxy_get_property
#define js_document_proxy_set_property radiant_host_api->dom->document_proxy_set_property
#define js_dom_range_get_prototype_value radiant_host_api->dom->range_get_prototype_value
#define js_dom_selection_get_prototype_value radiant_host_api->dom->selection_get_prototype_value
#define js_dom_expando_has_property radiant_host_api->dom->expando_has_property
#define js_dom_expando_get_own_property_descriptor radiant_host_api->dom->expando_get_own_property_descriptor
#define js_dom_expando_delete_property radiant_host_api->dom->expando_delete_property
#define js_dom_expando_own_property_names radiant_host_api->dom->expando_own_property_names
#define js_dom_owner_document_for_node radiant_host_api->dom->owner_document_for_node
#define js_dom_to_attribute_cstr radiant_host_api->dom->to_attribute_cstr
#define js_is_truthy radiant_host_api->script->is_truthy
#define js_to_string radiant_host_api->script->to_string
#define js_dom_after_set_attribute radiant_host_api->dom->after_set_attribute
#define js_dom_after_remove_attribute radiant_host_api->dom->after_remove_attribute
#define js_dom_after_toggle_attribute_remove radiant_host_api->dom->after_toggle_attribute_remove
#define js_dom_after_disabled_attribute_set radiant_host_api->dom->after_disabled_attribute_set
#define js_dom_after_default_checked_set radiant_host_api->dom->after_default_checked_set
#define js_dom_after_default_selected_set radiant_host_api->dom->after_default_selected_set
#define js_dom_after_select_multiple_removed radiant_host_api->dom->after_select_multiple_removed
#define js_dom_set_checked_dirty radiant_host_api->dom->set_checked_dirty
#define js_dom_select_set_value_bridge radiant_host_api->dom->select_set_value_bridge
#define js_dom_select_set_selected_index_bridge radiant_host_api->dom->select_set_selected_index_bridge
#define js_dom_select_set_length_bridge radiant_host_api->dom->select_set_length_bridge
#define js_dom_set_option_selected_dirty radiant_host_api->dom->set_option_selected_dirty
#define js_dom_set_option_text_bridge radiant_host_api->dom->set_option_text_bridge
#define js_dom_after_srcdoc_set radiant_host_api->dom->after_srcdoc_set
#define js_dom_throw_contenteditable_syntax_error radiant_host_api->dom->throw_contenteditable_syntax_error
#define js_dom_set_text_data_property radiant_host_api->dom->set_text_data_property
#define js_dom_text_control_set_value_bridge radiant_host_api->dom->text_control_set_value_bridge
#define js_dom_text_control_set_selection_start_bridge radiant_host_api->dom->text_control_set_selection_start_bridge
#define js_dom_text_control_set_selection_end_bridge radiant_host_api->dom->text_control_set_selection_end_bridge
#define js_dom_text_control_set_selection_direction_bridge radiant_host_api->dom->text_control_set_selection_direction_bridge
#define js_dom_text_control_set_default_value_bridge radiant_host_api->dom->text_control_set_default_value_bridge
#define js_dom_text_control_set_selection_range_bridge radiant_host_api->dom->text_control_set_selection_range_bridge
#define js_dom_text_control_set_range_text_bridge radiant_host_api->dom->text_control_set_range_text_bridge
#define js_dom_text_control_select_bridge radiant_host_api->dom->text_control_select_bridge
#define js_dom_form_reset_bridge radiant_host_api->dom->form_reset_bridge
#define js_dom_check_validity_bridge radiant_host_api->dom->check_validity_bridge
#define js_dom_report_validity_bridge radiant_host_api->dom->report_validity_bridge
#define js_dom_form_submit_bridge radiant_host_api->dom->form_submit_bridge
#define js_dom_form_request_submit_bridge radiant_host_api->dom->form_request_submit_bridge
#define js_dom_focus_method_bridge radiant_host_api->dom->focus_method_bridge
#define js_dom_click_method_bridge radiant_host_api->dom->click_method_bridge
#define js_dom_add_event_listener_bridge radiant_host_api->dom->add_event_listener_bridge
#define js_dom_remove_event_listener_bridge radiant_host_api->dom->remove_event_listener_bridge
#define js_dom_dispatch_event_bridge radiant_host_api->dom->dispatch_event_bridge
#define js_dom_get_bounding_client_rect_bridge radiant_host_api->dom->get_bounding_client_rect_bridge
#define js_dom_get_client_rects_bridge radiant_host_api->dom->get_client_rects_bridge
#define js_dom_scroll_into_view_bridge radiant_host_api->dom->scroll_into_view_bridge
#define js_dom_scroll_operation_bridge radiant_host_api->dom->scroll_operation_bridge
#define js_dom_text_control_caret_bounds_bridge radiant_host_api->dom->text_control_caret_bounds_bridge
#define js_dom_text_control_boundary_from_point_bridge radiant_host_api->dom->text_control_boundary_from_point_bridge
#define js_dom_boundary_from_point_bridge radiant_host_api->dom->boundary_from_point_bridge
#define js_dom_style_set_property_bridge radiant_host_api->dom->style_set_property_bridge
#define js_dom_style_remove_property_bridge radiant_host_api->dom->style_remove_property_bridge
#define js_dom_text_replace_data_bridge radiant_host_api->dom->text_replace_data_bridge
#define js_dom_text_insert_data_bridge radiant_host_api->dom->text_insert_data_bridge
#define js_dom_text_append_data_bridge radiant_host_api->dom->text_append_data_bridge
#define js_dom_text_delete_data_bridge radiant_host_api->dom->text_delete_data_bridge
#define js_dom_text_substring_data_bridge radiant_host_api->dom->text_substring_data_bridge
#define js_dom_append_child_bridge radiant_host_api->dom->append_child_bridge
#define js_dom_remove_child_bridge radiant_host_api->dom->remove_child_bridge
#define js_dom_insert_before_bridge radiant_host_api->dom->insert_before_bridge
#define js_dom_remove_bridge radiant_host_api->dom->remove_bridge
#define js_dom_adopt_node_bridge radiant_host_api->dom->adopt_node_bridge
#define js_dom_location_navigate_bridge radiant_host_api->dom->location_navigate_bridge
#define js_dom_document_open_bridge radiant_host_api->dom->document_open_bridge
#define js_dom_document_write_bridge radiant_host_api->dom->document_write_bridge
#define js_dom_document_element_from_point_bridge radiant_host_api->dom->document_element_from_point_bridge
#define js_dom_create_range radiant_host_api->dom->create_range
#define js_dom_get_selection radiant_host_api->dom->get_selection
#define js_dom_get_selection_function_for_document radiant_host_api->dom->get_selection_function_for_document
#define js_doc_has_browsing_context radiant_host_api->dom->doc_has_browsing_context
#define js_dom_document_fonts_bridge radiant_host_api->dom->document_fonts_bridge
#define js_dom_document_stylesheets_bridge radiant_host_api->dom->document_stylesheets_bridge
#define js_dom_document_default_view_bridge radiant_host_api->dom->document_default_view_bridge
#define js_dom_document_implementation_bridge radiant_host_api->dom->document_implementation_bridge
#define js_dom_document_design_mode_bridge radiant_host_api->dom->document_design_mode_bridge
#define js_dom_document_active_element_bridge radiant_host_api->dom->document_active_element_bridge
#define js_dom_normalize_bridge radiant_host_api->dom->normalize_bridge
#define js_dom_live_child_collection_bridge radiant_host_api->dom->live_child_collection_bridge
#define js_dom_live_document_forms_bridge radiant_host_api->dom->live_document_forms_bridge
#define js_dom_live_form_elements_bridge radiant_host_api->dom->live_form_elements_bridge
#define js_dom_live_document_get_elements_by_tag_name_bridge radiant_host_api->dom->live_document_get_elements_by_tag_name_bridge
#define js_dom_live_document_get_elements_by_class_name_bridge radiant_host_api->dom->live_document_get_elements_by_class_name_bridge
#define js_dom_live_document_get_elements_by_name_bridge radiant_host_api->dom->live_document_get_elements_by_name_bridge
#define js_dom_live_element_get_elements_by_tag_name_bridge radiant_host_api->dom->live_element_get_elements_by_tag_name_bridge
#define js_dom_live_element_get_elements_by_class_name_bridge radiant_host_api->dom->live_element_get_elements_by_class_name_bridge
#define js_dom_clone_node_bridge radiant_host_api->dom->clone_node_bridge
#define js_dom_replace_child_bridge radiant_host_api->dom->replace_child_bridge
#define js_dom_replace_with_bridge radiant_host_api->dom->replace_with_bridge
#define js_dom_insert_adjacent_element_bridge radiant_host_api->dom->insert_adjacent_element_bridge
#define js_dom_insert_adjacent_html_bridge radiant_host_api->dom->insert_adjacent_html_bridge
#define js_dom_append_variadic_bridge radiant_host_api->dom->append_variadic_bridge
#define js_dom_prepend_variadic_bridge radiant_host_api->dom->prepend_variadic_bridge
#define js_dom_notify_mutation radiant_host_api->dom->notify_mutation
#define js_dom_notify_mutation_detail radiant_host_api->dom->notify_mutation_detail
#define js_dom_get_ui_context radiant_host_api->dom->get_ui_context
#define js_dom_has_committed_geometry_snapshot radiant_host_api->dom->has_committed_geometry_snapshot
#define js_dom_create_tree_walker_bridge radiant_host_api->dom->document_create_tree_walker_bridge
#define js_dom_document_create_event_bridge radiant_host_api->dom->document_create_event_bridge
#define js_dom_document_exec_command_bridge radiant_host_api->dom->document_exec_command_bridge

static const int RADIANT_DOM_WRAPPER_CACHE_CHUNK_SIZE = 4096;
static const char s_radiant_dom_vmap_type_marker = 0;

struct RadiantDomWrapperCacheEntry {
    DomNodeRef node_ref;
    DomDocument* owner_doc;
    uint64_t item;
    RadiantDomWrapperCacheEntry* next_free;
    RadiantDomWrapperCacheEntry* next_sweep;
};

struct RadiantDomWrapperCacheIndexEntry {
    DomNode* node;
    RadiantDomWrapperCacheEntry* entry;
};

struct RadiantDomWrapperCacheChunk {
    RadiantDomWrapperCacheEntry entries[RADIANT_DOM_WRAPPER_CACHE_CHUNK_SIZE];
    int count;
    RadiantDomWrapperCacheChunk* next;
};

static __thread RadiantDomWrapperCacheChunk* s_radiant_dom_wrapper_cache_head = nullptr;
static __thread RadiantDomWrapperCacheChunk* s_radiant_dom_wrapper_cache_tail = nullptr;
static __thread HashMap* s_radiant_dom_wrapper_index = nullptr;
static __thread RadiantDomWrapperCacheEntry* s_radiant_dom_wrapper_free = nullptr;
static __thread RadiantDomWrapperCacheEntry* s_radiant_dom_wrapper_sweep = nullptr;
static __thread bool s_radiant_dom_cache_owner_set = false;
static __thread pthread_t s_radiant_dom_cache_owner;
static __thread bool s_radiant_dom_geometry_layout_active;

static void* radiant_dom_cache_malloc(size_t size) {
    return mem_alloc(size, MEM_CAT_JS_RUNTIME);
}
static void* radiant_dom_cache_realloc(void* ptr, size_t size) {
    return mem_realloc(ptr, size, MEM_CAT_JS_RUNTIME);
}

static void radiant_dom_cache_free(void* ptr) {
    mem_free(ptr);
}

static uint64_t radiant_dom_cache_index_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const RadiantDomWrapperCacheIndexEntry* entry = (const RadiantDomWrapperCacheIndexEntry*)item;
    return hashmap_sip(&entry->node, sizeof(entry->node), seed0, seed1);
}

static int radiant_dom_cache_index_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const RadiantDomWrapperCacheIndexEntry* ea = (const RadiantDomWrapperCacheIndexEntry*)a;
    const RadiantDomWrapperCacheIndexEntry* eb = (const RadiantDomWrapperCacheIndexEntry*)b;
    return ea->node == eb->node ? 0 : 1;
}

static void radiant_dom_cache_check_owner(const char* op) {
    pthread_t current = pthread_self();
    if (!s_radiant_dom_cache_owner_set) {
        s_radiant_dom_cache_owner = current;
        s_radiant_dom_cache_owner_set = true;
        return;
    }
    if (!pthread_equal(s_radiant_dom_cache_owner, current)) {
        // wrapper cache slots are rooted per runtime thread; cross-thread use
        // would unregister or mutate roots owned by a different JS runtime.
        log_error("RDOM_CACHE_THREAD_MISMATCH: %s on non-owner thread", op ? op : "unknown");
#ifndef NDEBUG
        assert(false && "Radiant DOM wrapper cache used from non-owner thread");
#endif
    }
}

static bool radiant_dom_is_node_host_type(const void* host_type) {
    return host_type == (const void*)&s_radiant_dom_vmap_type_marker ||
        host_type == radiant_dom_node_host_type() ||
        host_type == radiant_dom_html_element_host_type() ||
        host_type == radiant_dom_character_data_host_type() ||
        host_type == radiant_dom_svg_element_host_type() ||
        host_type == radiant_dom_input_element_host_type() ||
        host_type == radiant_dom_select_element_host_type() ||
        host_type == radiant_dom_textarea_element_host_type() ||
        host_type == radiant_dom_option_element_host_type();
}

static const void* radiant_dom_host_type_for_node(DomNode* node) {
    if (!node) return radiant_dom_node_host_type();
    if (node->is_text() || node->is_comment()) {
        return radiant_dom_character_data_host_type();
    }
    if (!node->is_element()) return radiant_dom_node_host_type();

    DomElement* elem = node->as_element();
    if (!elem) return radiant_dom_html_element_host_type();
    for (DomNode* current = node; current; current = current->parent) {
        if (!current->is_element()) continue;
        DomElement* ancestor = current->as_element();
        if (!ancestor) continue;
        const char* namespace_uri = ancestor->get_attribute("__lambda_ns_uri");
        if ((namespace_uri && strcmp(namespace_uri,
                "http://www.w3.org/2000/svg") == 0) ||
                ancestor->tag_name_id() == MARKUP_NAME_SVG) {
            return radiant_dom_svg_element_host_type();
        }
    }
    switch (elem->tag_name_id()) {
    case MARKUP_NAME_INPUT:
        return radiant_dom_input_element_host_type();
    case MARKUP_NAME_SELECT:
        return radiant_dom_select_element_host_type();
    case MARKUP_NAME_TEXTAREA:
        return radiant_dom_textarea_element_host_type();
    case MARKUP_NAME_OPTION:
        return radiant_dom_option_element_host_type();
    default:
        return radiant_dom_html_element_host_type();
    }
}

static HashMap* radiant_dom_wrapper_index() {
    if (!s_radiant_dom_wrapper_index) {
        s_radiant_dom_wrapper_index = hashmap_new_with_allocator(
            radiant_dom_cache_malloc,
            radiant_dom_cache_realloc,
            radiant_dom_cache_free,
            sizeof(RadiantDomWrapperCacheIndexEntry),
            4096,
            0x726164646f6d3032ULL,
            0x7772617063616368ULL,
            radiant_dom_cache_index_hash,
            radiant_dom_cache_index_compare,
            nullptr,
            nullptr);
    }
    return s_radiant_dom_wrapper_index;
}

static Item radiant_dom_string_item(const char* value) {
    return (Item){.item = s2it(heap_create_name(value ? value : ""))};
}

static Item radiant_dom_int_item(int64_t value) {
    return (Item){.item = i2it(value)};
}

static Item radiant_dom_undefined_item() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

static Item radiant_dom_node_item(DomNode* node) {
    return node ? radiant_dom_wrap_node((void*)node) : ItemNull;
}

static Item radiant_dom_document_item(DomDocument* doc) {
    if (!doc) return ItemNull;
    Item wrapper = radiant_host_api->value->vmap_new();
    if (get_type_id(wrapper) == LMD_TYPE_VMAP && wrapper.vmap) {
        wrapper.vmap->host_type = radiant_dom_document_host_type();
        wrapper.vmap->host_data = (void*)doc;
        return wrapper;
    }
    return ItemNull;
}

static Item radiant_dom_array_item() {
    Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    arr->type_id = LMD_TYPE_ARRAY;
    arr->items = nullptr;
    arr->length = 0;
    arr->capacity = 0;
    return (Item){.array = arr};
}

static const char* radiant_dom_to_dom_string_cstr(Item value) {
    Item string_value = js_to_string(value);
    return fn_to_cstr(string_value);
}

static bool radiant_dom_is_internal_attr(const char* name) {
    return name && strncmp(name, "__lambda_", 9) == 0;
}

static bool radiant_dom_is_attr_name_projection(const char* name) {
    // Lambda's dashed data/ARIA fields are DOM attributes, not JS expandos.
    return name && (strncmp(name, "data-", 5) == 0 ||
                    strncmp(name, "aria-", 5) == 0);
}

static bool radiant_dom_is_tag(DomElement* elem, const char* tag) {
    return elem && elem->tag_name && tag && strcasecmp(elem->tag_name, tag) == 0;
}

static bool radiant_dom_node_is_dom_element(DomNode* node) {
    if (!node || !node->is_element()) return false;
    DomElement* elem = node->as_element();
    // Document/fragment shells share DomElement storage internally but are
    // not Elements in the DOM type hierarchy.
    return elem && elem->tag_name && elem->tag_name[0] != '#';
}

static int64_t radiant_dom_reflected_int_value(DomElement* elem, const char* attr_name,
                                               int default_value) {
    const char* raw = elem->get_attribute(attr_name);
    if (!raw) return default_value;
    char* end = nullptr;
    long value = strtol(raw, &end, 10);
    if (end == raw || value < 0) return default_value;
    return (int64_t)value;
}

static long radiant_dom_item_to_reflected_int(Item value, int default_value) {
    TypeId type = get_type_id(value);
    long out = default_value;
    if (type == LMD_TYPE_INT) {
        out = (long)it2i(value);
    } else if (type == LMD_TYPE_INT64) {
        out = (long)it2l(value);
    } else if (type == LMD_TYPE_FLOAT) {
        out = (long)it2d(value);
    } else {
        const char* text = fn_to_cstr(value);
        if (text && *text) {
            char* end = nullptr;
            long parsed = strtol(text, &end, 10);
            out = (end != text) ? parsed : 0;
        } else {
            out = 0;
        }
    }
    return out < 0 ? default_value : out;
}

static const char* radiant_dom_canonical_token_attr(DomElement* elem, const char* attr_name,
                                                    const char* const* keywords) {
    const char* value = elem->get_attribute(attr_name);
    if (!value) return "";

    char lowered[32];
    size_t len = 0;
    while (value[len] && len < sizeof(lowered) - 1) {
        lowered[len] = (char)tolower((unsigned char)value[len]);
        len++;
    }
    if (value[len] != '\0') return "";
    lowered[len] = '\0';

    for (int i = 0; keywords[i]; i++) {
        if (strcmp(lowered, keywords[i]) == 0) return keywords[i];
    }
    return "";
}

static const char* radiant_dom_normalize_contenteditable(const char* value) {
    if (!value || *value == '\0' || strcasecmp(value, "true") == 0) return "true";
    if (strcasecmp(value, "false") == 0) return "false";
    if (strcasecmp(value, "plaintext-only") == 0) return "plaintext-only";
    if (strcasecmp(value, "inherit") == 0) return "inherit";
    return nullptr;
}

static bool radiant_dom_is_content_editable(DomElement* elem) {
    bool saw_false = false;
    DomNode* node = (DomNode*)elem;
    while (node) {
        if (node->is_element()) {
            DomElement* current = node->as_element();
            if (current->has_attribute("contenteditable")) {
                const char* normalized = radiant_dom_normalize_contenteditable(
                    current->get_attribute("contenteditable"));
                if (!normalized) normalized = "inherit";
                if (strcmp(normalized, "true") == 0 || strcmp(normalized, "plaintext-only") == 0) {
                    return !saw_false;
                }
                if (strcmp(normalized, "false") == 0) {
                    saw_false = true;
                }
            }
        }
        node = node->parent;
    }
    return false;
}

static bool radiant_dom_tree_has_contenteditable(DomNode* node) {
    for (DomNode* current = node; current; current = current->next_sibling) {
        if (!current->is_element()) continue;
        DomElement* elem = current->as_element();
        if (elem->has_attribute("contenteditable")) return true;
        if (radiant_dom_tree_has_contenteditable(elem->first_child)) return true;
    }
    return false;
}

extern "C" int radiant_dom_document_legacy_command_enabled(Item object) {
    (void)object;
    DomDocument* doc = (DomDocument*)js_dom_get_document();
    // the legacy command surface is an editor-only capability; exposing it on
    // every document makes feature detection select an inert formatting path.
    return doc && doc->root && radiant_dom_tree_has_contenteditable(doc->root) ? 1 : 0;
}

static String* radiant_dom_uppercase_name(const char* name) {
    if (!name) return heap_create_name("");
    size_t len = strlen(name);
    char stack_buf[64];
    char* upper = (len < sizeof(stack_buf)) ? stack_buf : (char*)mem_alloc(len + 1, MEM_CAT_JS_RUNTIME);
    if (!upper) return heap_create_name("");
    for (size_t i = 0; i < len; i++) {
        upper[i] = (char)toupper((unsigned char)name[i]);
    }
    upper[len] = '\0';
    String* result = heap_create_name(upper);
    if (upper != stack_buf) mem_free(upper);
    return result;
}

static int64_t radiant_dom_utf16_length(const char* text) {
    if (!text) return 0;
    int64_t units = 0;
    const unsigned char* p = (const unsigned char*)text;
    while (*p) {
        if ((*p & 0x80) == 0) {
            p++;
            units++;
        } else if ((*p & 0xE0) == 0xC0 && p[1]) {
            p += 2;
            units++;
        } else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
            p += 3;
            units++;
        } else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
            p += 4;
            units += 2;
        } else {
            p++;
            units++;
        }
    }
    return units;
}

static bool radiant_dom_is_generated_pseudo_node(DomNode* node) {
    if (!node || !node->is_element()) return false;
    DomElement* elem = node->as_element();
    return elem->tag_name && elem->tag_name[0] == ':' && elem->tag_name[1] == ':';
}

static bool radiant_dom_is_anonymous_table_wrapper(DomNode* node) {
    if (!node || !node->is_element()) return false;
    DomElement* elem = node->as_element();
    return elem->tag_name && strncmp(elem->tag_name, "::anon-", 7) == 0;
}

static DomNode* radiant_dom_first_script_visible_child(DomElement* elem);
static DomNode* radiant_dom_last_script_visible_child(DomElement* elem);

static DomNode* radiant_dom_next_script_visible_sibling(DomNode* node) {
    DomNode* sibling = node ? node->next_sibling : nullptr;
    while (sibling) {
        if (!radiant_dom_is_generated_pseudo_node(sibling)) return sibling;
        if (radiant_dom_is_anonymous_table_wrapper(sibling)) {
            DomNode* child = radiant_dom_first_script_visible_child(sibling->as_element());
            if (child) return child;
        }
        sibling = sibling->next_sibling;
    }
    DomNode* parent = node ? node->parent : nullptr;
    while (radiant_dom_is_anonymous_table_wrapper(parent)) {
        sibling = parent->next_sibling;
        while (sibling) {
            if (!radiant_dom_is_generated_pseudo_node(sibling)) return sibling;
            if (radiant_dom_is_anonymous_table_wrapper(sibling)) {
                DomNode* child = radiant_dom_first_script_visible_child(sibling->as_element());
                if (child) return child;
            }
            sibling = sibling->next_sibling;
        }
        parent = parent->parent;
    }
    return nullptr;
}

static DomNode* radiant_dom_prev_script_visible_sibling(DomNode* node) {
    DomNode* sibling = node ? node->prev_sibling : nullptr;
    while (sibling) {
        if (!radiant_dom_is_generated_pseudo_node(sibling)) return sibling;
        if (radiant_dom_is_anonymous_table_wrapper(sibling)) {
            DomNode* child = radiant_dom_last_script_visible_child(sibling->as_element());
            if (child) return child;
        }
        sibling = sibling->prev_sibling;
    }
    DomNode* parent = node ? node->parent : nullptr;
    while (radiant_dom_is_anonymous_table_wrapper(parent)) {
        sibling = parent->prev_sibling;
        while (sibling) {
            if (!radiant_dom_is_generated_pseudo_node(sibling)) return sibling;
            if (radiant_dom_is_anonymous_table_wrapper(sibling)) {
                DomNode* child = radiant_dom_last_script_visible_child(sibling->as_element());
                if (child) return child;
            }
            sibling = sibling->prev_sibling;
        }
        parent = parent->parent;
    }
    return nullptr;
}

static DomNode* radiant_dom_first_script_visible_child(DomElement* elem) {
    DomNode* child = elem ? elem->first_child : nullptr;
    while (child) {
        if (!radiant_dom_is_generated_pseudo_node(child)) return child;
        if (radiant_dom_is_anonymous_table_wrapper(child)) {
            // layout-only anonymous wrappers must stay transparent to DOM scripts.
            DomNode* nested = radiant_dom_first_script_visible_child(child->as_element());
            if (nested) return nested;
        }
        child = child->next_sibling;
    }
    return nullptr;
}

static DomNode* radiant_dom_last_script_visible_child(DomElement* elem) {
    DomNode* child = elem ? elem->last_child : nullptr;
    while (child) {
        if (!radiant_dom_is_generated_pseudo_node(child)) return child;
        if (radiant_dom_is_anonymous_table_wrapper(child)) {
            DomNode* nested = radiant_dom_last_script_visible_child(child->as_element());
            if (nested) return nested;
        }
        child = child->prev_sibling;
    }
    return nullptr;
}

static DomNode* radiant_dom_first_script_visible_element_child(DomElement* elem) {
    DomNode* child = radiant_dom_first_script_visible_child(elem);
    while (child) {
        if (child->is_element()) return child;
        child = radiant_dom_next_script_visible_sibling(child);
    }
    return nullptr;
}

static DomNode* radiant_dom_last_script_visible_element_child(DomElement* elem) {
    DomNode* child = radiant_dom_last_script_visible_child(elem);
    while (child) {
        if (child->is_element()) return child;
        child = radiant_dom_prev_script_visible_sibling(child);
    }
    return nullptr;
}

static DomNode* radiant_dom_next_script_visible_element_sibling(DomNode* node) {
    DomNode* sibling = radiant_dom_next_script_visible_sibling(node);
    while (sibling) {
        if (sibling->is_element()) return sibling;
        sibling = radiant_dom_next_script_visible_sibling(sibling);
    }
    return nullptr;
}

static DomNode* radiant_dom_prev_script_visible_element_sibling(DomNode* node) {
    DomNode* sibling = radiant_dom_prev_script_visible_sibling(node);
    while (sibling) {
        if (sibling->is_element()) return sibling;
        sibling = radiant_dom_prev_script_visible_sibling(sibling);
    }
    return nullptr;
}

static int64_t radiant_dom_script_visible_element_child_count(DomElement* elem) {
    int64_t count = 0;
    DomNode* child = radiant_dom_first_script_visible_child(elem);
    while (child) {
        if (child->is_element()) count++;
        child = radiant_dom_next_script_visible_sibling(child);
    }
    return count;
}

static Item radiant_dom_attributes_item(DomElement* elem) {
    Item arr_item = radiant_dom_array_item();
    Array* arr = arr_item.array;

    int attr_count = 0;
    const char** attr_names = elem->attribute_names(&attr_count);
    for (int i = 0; attr_names && i < attr_count; i++) {
        const char* name = attr_names[i];
        if (radiant_dom_is_internal_attr(name)) continue;
        const char* value = elem->get_attribute(name);
        Item pair = radiant_host_api->value->new_object();
        Item name_item = radiant_dom_string_item(name);
        Item value_item = radiant_dom_string_item(value);
        radiant_host_api->value->property_set(pair,
            (Item){.item = s2it(heap_create_name("name"))}, name_item);
        radiant_host_api->value->property_set(pair,
            (Item){.item = s2it(heap_create_name("value"))}, value_item);
        // Attr is a Node: sanitizers consume nodeName/nodeValue even when the
        // bridge represents NamedNodeMap entries as lightweight objects.
        radiant_host_api->value->property_set(pair,
            (Item){.item = s2it(heap_create_name("nodeName"))}, name_item);
        radiant_host_api->value->property_set(pair,
            (Item){.item = s2it(heap_create_name("nodeValue"))}, value_item);
        array_push(arr, pair);
    }
    return arr_item;
}

static DomDocument* radiant_dom_node_document(DomNode* node, bool active_fallback) {
    DomNode* current = node;
    while (current) {
        if (current->is_element()) {
            DomElement* elem = current->as_element();
            if (elem && elem->doc) return elem->doc;
        }
        current = current->parent;
    }
    if (active_fallback) {
        return (DomDocument*)js_dom_get_document();
    }
    return nullptr;
}

static bool radiant_dom_node_is_connected(DomNode* node) {
    DomDocument* doc = radiant_dom_node_document(node, false);
    if (!node || !doc || !doc->root) return false;
    for (DomNode* current = node; current; current = current->parent) {
        if (current == (DomNode*)doc->root) return true;
    }
    return false;
}

static Item radiant_dom_node_root_item(DomNode* node) {
    if (!node) return ItemNull;
    DomDocument* doc = radiant_dom_node_document(node, false);
    if (doc && radiant_dom_node_is_connected(node)) {
        // A connected engine tree omits an explicit Document parent, so expose
        // the document proxy as the DOM root instead of stopping at <html>.
        return js_dom_document_proxy_for_doc_bridge((void*)doc);
    }
    DomNode* root = node;
    while (root->parent) root = root->parent;
    return radiant_dom_node_item(root);
}

static bool radiant_dom_node_contains(DomNode* root, DomNode* other) {
    if (!root || !other) return false;
    for (DomNode* current = other; current; current = current->parent) {
        if (current == root) return true;
    }
    return false;
}

static int64_t radiant_dom_compare_document_position(DomNode* node, DomNode* other) {
    if (!other) return 1;
    if (node == other) return 0;
    for (DomNode* p = other->parent; p; p = p->parent) {
        if (p == node) return 16 + 4;
    }
    for (DomNode* p = node->parent; p; p = p->parent) {
        if (p == other) return 8 + 2;
    }
    DomNode* a_path[256];
    int a_depth = 0;
    for (DomNode* p = node; p && a_depth < 256; p = p->parent) a_path[a_depth++] = p;
    DomNode* b_path[256];
    int b_depth = 0;
    for (DomNode* p = other; p && b_depth < 256; p = p->parent) b_path[b_depth++] = p;
    if (a_depth == 0 || b_depth == 0 || a_path[a_depth - 1] != b_path[b_depth - 1]) {
        return 1;
    }
    int ai = a_depth - 1;
    int bi = b_depth - 1;
    while (ai > 0 && bi > 0 && a_path[ai - 1] == b_path[bi - 1]) {
        ai--;
        bi--;
    }
    DomNode* a_child = (ai > 0) ? a_path[ai - 1] : node;
    DomNode* b_child = (bi > 0) ? b_path[bi - 1] : other;
    for (DomNode* s = a_child->next_sibling; s; s = s->next_sibling) {
        if (s == b_child) return 4;
    }
    return 2;
}

static DomElement* radiant_dom_find_by_id(DomElement* root, const char* id) {
    if (!root || !id) return nullptr;
    if (root->id && strcmp(root->id, id) == 0) return root;
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            DomElement* found = radiant_dom_find_by_id(child->as_element(), id);
            if (found) return found;
        }
        child = child->next_sibling;
    }
    return nullptr;
}

static CssSelectorGroup* radiant_dom_parse_css_selector_group(const char* sel_text, Pool* pool) {
    if (!sel_text || !pool) return nullptr;
    size_t sel_len = strlen(sel_text);
    if (sel_len == 0) return nullptr;
    size_t token_count = 0;
    CssToken* tokens = css_tokenize(sel_text, sel_len, pool, &token_count);
    if (!tokens || token_count == 0) return nullptr;
    int pos = 0;
    // DOM selector APIs receive selector lists; parsing only the first selector
    // makes editor hit-tests such as closest("td, th") miss valid cells.
    return css_parse_selector_group_from_tokens(tokens, &pos, (int)token_count, pool);
}

static DomElement* radiant_dom_selector_group_find_first(SelectorMatcher* matcher,
                                                         CssSelectorGroup* group,
                                                         DomElement* elem,
                                                         bool include_elem) {
    if (!matcher || !group || !elem) return nullptr;
    if (include_elem && selector_matcher_matches_group(matcher, group, elem, nullptr)) return elem;

    DomNode* child = elem->first_child;
    while (child) {
        if (child->is_element()) {
            DomElement* found = radiant_dom_selector_group_find_first(
                matcher, group, child->as_element(), true);
            if (found) return found;
        }
        child = child->next_sibling;
    }
    return nullptr;
}

static bool radiant_dom_selector_group_result_contains(ArrayList* results, DomElement* elem) {
    if (!results || !elem) return false;
    for (int i = 0; i < results->length; i++) {
        if ((DomElement*)results->data[i] == elem) return true;
    }
    return false;
}

static void radiant_dom_selector_group_collect_all(SelectorMatcher* matcher,
                                                   CssSelectorGroup* group,
                                                   DomElement* elem,
                                                   ArrayList* results,
                                                   bool include_elem) {
    if (!matcher || !group || !elem || !results) return;
    if (include_elem && selector_matcher_matches_group(matcher, group, elem, nullptr) &&
            !radiant_dom_selector_group_result_contains(results, elem)) {
        arraylist_append(results, elem);
    }

    DomNode* child = elem->first_child;
    while (child) {
        if (child->is_element()) {
            radiant_dom_selector_group_collect_all(
                matcher, group, child->as_element(), results, true);
        }
        child = child->next_sibling;
    }
}

static Item radiant_dom_lookup_wrapper(DomNode* node) {
    radiant_dom_cache_check_owner("lookup_wrapper");
    HashMap* index = s_radiant_dom_wrapper_index;
    if (!index || !node) return ItemNull;
    RadiantDomWrapperCacheIndexEntry probe = {.node = node, .entry = nullptr};
    const RadiantDomWrapperCacheIndexEntry* found =
        (const RadiantDomWrapperCacheIndexEntry*)hashmap_get(index, &probe);
    if (found && found->entry && found->entry->item != 0 &&
        dom_node_ref_validate(found->entry->owner_doc, found->entry->node_ref)) {
        return (Item){.item = found->entry->item};
    }
    return ItemNull;
}

RADIANT_C_API Item radiant_dom_lookup_cached_node(void* dom_node) {
    return radiant_dom_lookup_wrapper((DomNode*)dom_node);
}

static RadiantDomWrapperCacheChunk* radiant_dom_alloc_wrapper_cache_chunk() {
    radiant_dom_cache_check_owner("alloc_wrapper_cache_chunk");
    RadiantDomWrapperCacheChunk* chunk = (RadiantDomWrapperCacheChunk*)mem_alloc(
        sizeof(RadiantDomWrapperCacheChunk), MEM_CAT_JS_RUNTIME);
    if (!chunk) return nullptr;
    memset(chunk, 0, sizeof(*chunk));
    if (!s_radiant_dom_wrapper_cache_head) {
        s_radiant_dom_wrapper_cache_head = chunk;
        s_radiant_dom_wrapper_cache_tail = chunk;
    } else {
        s_radiant_dom_wrapper_cache_tail->next = chunk;
        s_radiant_dom_wrapper_cache_tail = chunk;
    }
    return chunk;
}

static void radiant_dom_weak_wrapper_cleared(uint64_t*, void* context) {
    RadiantDomWrapperCacheEntry* entry = (RadiantDomWrapperCacheEntry*)context;
    radiant_dom_cache_check_owner("weak_wrapper_cleared");
    if (!entry || !entry->owner_doc || !entry->node_ref.address) return;
    DomNode* address = entry->node_ref.address;
    if (s_radiant_dom_wrapper_index) {
        RadiantDomWrapperCacheIndexEntry probe = {.node = address, .entry = nullptr};
        hashmap_delete(s_radiant_dom_wrapper_index, &probe);
    }
    // The wrapper pin guarantees the generation is still registered. Weak
    // cleanup uses only the validated token and never inspects detached bytes.
    dom_node_unpin(entry->owner_doc, entry->node_ref, DOM_NODE_PIN_WRAPPER);
    entry->node_ref = {nullptr, 0};
    entry->item = 0;
    // Do not recycle the cache slot until the collector completes all weak
    // callbacks; the owner document is needed for one post-batch DOM sweep.
    entry->next_sweep = s_radiant_dom_wrapper_sweep;
    s_radiant_dom_wrapper_sweep = entry;
}

extern "C" void gc_weak_slots_processed(void) {
    if (!s_radiant_dom_wrapper_sweep) return;
    radiant_dom_cache_check_owner("weak_slots_processed");
    for (RadiantDomWrapperCacheEntry* entry = s_radiant_dom_wrapper_sweep;
         entry; entry = entry->next_sweep) {
        bool already_swept = false;
        for (RadiantDomWrapperCacheEntry* prior = s_radiant_dom_wrapper_sweep;
             prior != entry; prior = prior->next_sweep) {
            if (prior->owner_doc == entry->owner_doc) {
                already_swept = true;
                break;
            }
        }
        if (!already_swept && entry->owner_doc) {
            dom_retire_sweep(entry->owner_doc);
        }
    }
    while (s_radiant_dom_wrapper_sweep) {
        RadiantDomWrapperCacheEntry* entry = s_radiant_dom_wrapper_sweep;
        s_radiant_dom_wrapper_sweep = entry->next_sweep;
        entry->owner_doc = nullptr;
        entry->next_sweep = nullptr;
        entry->next_free = s_radiant_dom_wrapper_free;
        s_radiant_dom_wrapper_free = entry;
    }
}

static void radiant_dom_cache_wrapper(DomNode* node, Item wrapper) {
    radiant_dom_cache_check_owner("cache_wrapper");
    if (!node || wrapper.item == ITEM_NULL) return;
    RadiantDomWrapperCacheEntry* entry = s_radiant_dom_wrapper_free;
    if (entry) {
        s_radiant_dom_wrapper_free = entry->next_free;
    } else {
        RadiantDomWrapperCacheChunk* chunk = s_radiant_dom_wrapper_cache_tail;
        if (!chunk || chunk->count >= RADIANT_DOM_WRAPPER_CACHE_CHUNK_SIZE) {
            chunk = radiant_dom_alloc_wrapper_cache_chunk();
            if (!chunk) return;
        }
        entry = &chunk->entries[chunk->count++];
    }
    entry->owner_doc = radiant_dom_node_document(node, true);
    entry->node_ref = dom_node_ref(node);
    if (!entry->owner_doc ||
        !dom_node_ref_validate(entry->owner_doc, entry->node_ref)) {
        entry->node_ref = {nullptr, 0};
        entry->owner_doc = nullptr;
        entry->item = 0;
        entry->next_free = s_radiant_dom_wrapper_free;
        s_radiant_dom_wrapper_free = entry;
        return;
    }
    entry->item = wrapper.item;
    entry->next_free = nullptr;
    entry->next_sweep = nullptr;

    HashMap* index = radiant_dom_wrapper_index();
    if (index) {
        // The hash table stores pointers to stable chunk slots; GC root slots
        // never move when the index grows or rehashes.
        RadiantDomWrapperCacheIndexEntry index_entry = {.node = node, .entry = entry};
        hashmap_set(index, &index_entry);
    }
    // Wrapper identity is weak: live JS reaches the wrapper naturally; this
    // slot only observes death so the matching native pin can be released.
    dom_node_pin(entry->owner_doc, entry->node_ref, DOM_NODE_PIN_WRAPPER);
    radiant_host_api->gc->register_weak(
        &entry->item, radiant_dom_weak_wrapper_cleared, entry);
}

static void radiant_dom_clear_cache_entry(RadiantDomWrapperCacheEntry* entry) {
    radiant_dom_cache_check_owner("clear_cache_entry");
    if (!entry || entry->item == 0) return;
    DomNode* node = entry->node_ref.address;
    if (s_radiant_dom_wrapper_index && node) {
        RadiantDomWrapperCacheIndexEntry probe = {.node = node, .entry = nullptr};
        hashmap_delete(s_radiant_dom_wrapper_index, &probe);
    }
    DomNode* live_node = dom_node_ref_validate(entry->owner_doc, entry->node_ref);
    if (live_node && live_node->is_element()) {
        form_control_release_prop(live_node->as_element());
    }
    Item wrapper = (Item){.item = entry->item};
    // document teardown frees arena-owned DOM nodes; retained wrappers must
    // keep their JS identity but lose the native payload through the husk protocol.
    if (get_type_id(wrapper) == LMD_TYPE_VMAP && wrapper.vmap && wrapper.vmap->host_type) {
        radiant_dom_host_invalidate(wrapper);
    }
    radiant_host_api->gc->unregister_weak(&entry->item);
    if (live_node) {
        dom_node_unpin(entry->owner_doc, entry->node_ref, DOM_NODE_PIN_WRAPPER);
    }
    entry->node_ref = {nullptr, 0};
    entry->owner_doc = nullptr;
    entry->item = 0;
    entry->next_sweep = nullptr;
    entry->next_free = s_radiant_dom_wrapper_free;
    s_radiant_dom_wrapper_free = entry;
}

RADIANT_C_API void radiant_dom_invalidate_document(DomDocument* doc) {
    radiant_dom_cache_check_owner("invalidate_document");
    if (!doc) return;
    for (RadiantDomWrapperCacheChunk* chunk = s_radiant_dom_wrapper_cache_head; chunk; chunk = chunk->next) {
        for (int i = 0; i < chunk->count; i++) {
            RadiantDomWrapperCacheEntry* entry = &chunk->entries[i];
            if (entry->owner_doc == doc) {
                radiant_dom_clear_cache_entry(entry);
            }
        }
    }
}

RADIANT_C_API void radiant_dom_reset_wrapper_cache(void) {
    radiant_dom_cache_check_owner("reset_wrapper_cache");
    RadiantDomWrapperCacheChunk* chunk = s_radiant_dom_wrapper_cache_head;
    while (chunk) {
        for (int i = 0; i < chunk->count; i++) {
            radiant_dom_clear_cache_entry(&chunk->entries[i]);
        }
        RadiantDomWrapperCacheChunk* next = chunk->next;
        mem_free(chunk);
        chunk = next;
    }
    if (s_radiant_dom_wrapper_index) {
        hashmap_free(s_radiant_dom_wrapper_index);
    }
    s_radiant_dom_wrapper_cache_head = nullptr;
    s_radiant_dom_wrapper_cache_tail = nullptr;
    s_radiant_dom_wrapper_index = nullptr;
    s_radiant_dom_wrapper_free = nullptr;
}

RADIANT_C_API Item radiant_dom_wrap_node(void* dom_elem) {
    radiant_dom_cache_check_owner("wrap_node");
    if (!dom_elem) return ItemNull;

    DomNode* node = (DomNode*)dom_elem;
    if (node->is_element()) {
        DomElement* e = node->as_element();
        if (e->doc && e->doc->js.doc_node == (void*)e) {
            Item proxy = js_dom_document_proxy_for_doc_bridge(e->doc);
            if (proxy.item != ITEM_NULL) return proxy;
        }
    }

    Item cached = radiant_dom_lookup_wrapper(node);
    if (cached.item != ITEM_NULL) return cached;

    Item wrapper = radiant_host_api->value->vmap_new();
    if (get_type_id(wrapper) == LMD_TYPE_VMAP && wrapper.vmap) {
        wrapper.vmap->host_type = radiant_dom_host_type_for_node(node);
        wrapper.vmap->host_data = dom_elem;
        // Cache identity before initialization because inline handlers and
        // native DOM state attach expandos to this same wrapper recursively.
        radiant_dom_cache_wrapper(node, wrapper);
        radiant_host_api->gc->register_root(&wrapper.item);
        js_dom_initialize_node_wrapper(dom_elem);
        radiant_host_api->gc->unregister_root(&wrapper.item);
        return wrapper;
    }
    // Phase 7 removes the DOM-node map shell; a failed VMap allocation must
    // not recreate the stale compatibility carrier or runtime dispatch diverges.
    return ItemNull;
}

RADIANT_C_API void* radiant_dom_unwrap_node(Item item) {
    if (get_type_id(item) == LMD_TYPE_VMAP && item.vmap &&
        radiant_dom_is_node_host_type(item.vmap->host_type)) {
        return item.vmap->host_data;
    }
    // Generic event/property paths probe arbitrary JS values before Radiant is
    // requested; an inactive module has no DOM bridge table to delegate to.
    if (!radiant_host_api || !radiant_host_api->dom ||
            !radiant_host_api->dom->unwrap_element_impl) {
        return NULL;
    }
    return js_dom_unwrap_element_impl(item);
}

RADIANT_C_API bool radiant_dom_is_node(Item item) {
    if (get_type_id(item) == LMD_TYPE_VMAP && item.vmap &&
        radiant_dom_is_node_host_type(item.vmap->host_type)) {
        return item.vmap->host_data != nullptr;
    }
    return false;
}

static bool radiant_dom_get_character_data_property(DomNode* node,
        const char* content, int64_t length, int64_t node_type,
        const char* node_name, const char* prop, Item* out) {
    if (!node || !prop || !out) return false;
    if (strcmp(prop, "data") == 0 || strcmp(prop, "nodeValue") == 0 ||
        strcmp(prop, "textContent") == 0) {
        *out = radiant_dom_string_item(content);
        return true;
    }
    if (strcmp(prop, "length") == 0) {
        *out = radiant_dom_int_item(length);
        return true;
    }
    if (strcmp(prop, "nodeType") == 0) {
        *out = radiant_dom_int_item(node_type);
        return true;
    }
    if (strcmp(prop, "nodeName") == 0) {
        *out = radiant_dom_string_item(node_name);
        return true;
    }
    if (strcmp(prop, "parentNode") == 0 || strcmp(prop, "parentElement") == 0) {
        DomNode* parent = node->parent;
        bool element_only = strcmp(prop, "parentElement") == 0;
        *out = (!element_only && parent && parent->is_element()) ||
               (element_only && radiant_dom_node_is_dom_element(parent))
            ? radiant_dom_node_item(parent) : ItemNull;
        return true;
    }
    if (strcmp(prop, "isConnected") == 0) {
        *out = (Item){.item = b2it(radiant_dom_node_is_connected(node) ? 1 : 0)};
        return true;
    }
    if (strcmp(prop, "nextSibling") == 0) {
        *out = radiant_dom_node_item(radiant_dom_next_script_visible_sibling(node));
        return true;
    }
    if (strcmp(prop, "previousSibling") == 0) {
        *out = radiant_dom_node_item(radiant_dom_prev_script_visible_sibling(node));
        return true;
    }
    if (strcmp(prop, "childNodes") == 0) {
        *out = radiant_dom_array_item();
        return true;
    }
    if (strcmp(prop, "firstChild") == 0 || strcmp(prop, "lastChild") == 0) {
        *out = ItemNull;
        return true;
    }
    if (strcmp(prop, "ownerDocument") == 0) {
        DomNode* parent = node->parent;
        DomDocument* doc = (parent && parent->is_element()) ? parent->as_element()->doc : nullptr;
        *out = doc ? radiant_dom_document_item(doc) : js_dom_owner_document_for_node((void*)node);
        return true;
    }
    return false;
}


// ---- DOM3 Phase 4a: record-driven member getters (identity/navigation) ----
// JubeMemberBind handler shapes; the strcmp arms these replace are deleted
// from radiant_dom_get_element_property.

RADIANT_C_API int radiant_dom_member_is_element(Item receiver) {
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(receiver);
    return node && node->is_element();
}

static DomElement* radiant_dom_member_elem(Item receiver) {
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(receiver);
    return (node && node->is_element()) ? node->as_element() : nullptr;
}

#define RADIANT_MEMBER_GET(name, expr)                                       \
    RADIANT_C_API int name(Item receiver, Item* out) {                       \
        DomElement* elem = radiant_dom_member_elem(receiver);                \
        if (!elem || !out) return 0;                                         \
        *out = (expr);                                                       \
        return 1;                                                            \
    }

RADIANT_MEMBER_GET(radiant_dom_member_tag_name,
    (Item){.item = s2it(radiant_dom_uppercase_name(elem->tag_name))})
RADIANT_MEMBER_GET(radiant_dom_member_local_name,
    radiant_dom_string_item(elem->tag_name))
RADIANT_C_API int radiant_dom_member_namespace_uri(Item receiver, Item* out) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    // Record access predates parser-created SVG nodes. Keep it on the
    // canonical resolver so namespace inheritance matches generic DOM reads.
    *out = js_dom_get_property_impl(receiver,
        (Item){.item = s2it(heap_create_name("namespaceURI"))});
    return 1;
}
RADIANT_MEMBER_GET(radiant_dom_member_prefix, ItemNull)
RADIANT_MEMBER_GET(radiant_dom_member_id, radiant_dom_string_item(elem->id))
RADIANT_C_API int radiant_dom_member_class_name(Item receiver, Item* out) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    // SVG className is SVGAnimatedString, unlike HTML's string reflection.
    // Using one resolver prevents the record fast path from changing the
    // WebIDL type according to how an element was created.
    *out = js_dom_get_property_impl(receiver,
        (Item){.item = s2it(heap_create_name("className"))});
    return 1;
}
RADIANT_MEMBER_GET(radiant_dom_member_node_type,
    radiant_dom_int_item((int64_t)elem->node_type))
RADIANT_C_API int radiant_dom_member_parent_node(Item receiver, Item* out) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    DomNode* parent = elem->parent;
    *out = (parent && parent->is_element()) ? radiant_dom_node_item(parent) : ItemNull;
    return 1;
}
RADIANT_MEMBER_GET(radiant_dom_member_is_connected,
    (Item){.item = b2it(radiant_dom_node_is_connected((DomNode*)elem) ? 1 : 0)})
RADIANT_MEMBER_GET(radiant_dom_member_child_element_count,
    radiant_dom_int_item(radiant_dom_script_visible_element_child_count(elem)))
RADIANT_MEMBER_GET(radiant_dom_member_children,
    js_dom_live_child_collection_bridge((void*)elem, true))
RADIANT_MEMBER_GET(radiant_dom_member_attributes, radiant_dom_attributes_item(elem))
RADIANT_MEMBER_GET(radiant_dom_member_owner_document,
    radiant_dom_document_item(elem->doc))
RADIANT_MEMBER_GET(radiant_dom_member_first_child,
    radiant_dom_node_item(radiant_dom_first_script_visible_child(elem)))
RADIANT_MEMBER_GET(radiant_dom_member_last_child,
    radiant_dom_node_item(radiant_dom_last_script_visible_child(elem)))
RADIANT_MEMBER_GET(radiant_dom_member_next_sibling,
    radiant_dom_node_item(radiant_dom_next_script_visible_sibling((DomNode*)elem)))
RADIANT_MEMBER_GET(radiant_dom_member_previous_sibling,
    radiant_dom_node_item(radiant_dom_prev_script_visible_sibling((DomNode*)elem)))
RADIANT_MEMBER_GET(radiant_dom_member_first_element_child,
    radiant_dom_node_item(radiant_dom_first_script_visible_element_child(elem)))
RADIANT_MEMBER_GET(radiant_dom_member_last_element_child,
    radiant_dom_node_item(radiant_dom_last_script_visible_element_child(elem)))
RADIANT_MEMBER_GET(radiant_dom_member_next_element_sibling,
    radiant_dom_node_item(radiant_dom_next_script_visible_element_sibling((DomNode*)elem)))
RADIANT_MEMBER_GET(radiant_dom_member_previous_element_sibling,
    radiant_dom_node_item(radiant_dom_prev_script_visible_element_sibling((DomNode*)elem)))
RADIANT_MEMBER_GET(radiant_dom_member_child_nodes,
    js_dom_live_child_collection_bridge((void*)elem, false))


// ---- DOM3 Phase 4b: reflected-attribute members ----
// Generated cluster: the bool/int/string/hint reflection predicate chains and
// their get/set arms collapse into guarded record rows. Live-state booleans
// keep their centralized invariant hooks.
static bool radiant_dom_member_tag_set(Item receiver, const char* tags) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !elem->tag_name || !tags) return false;
    const char* tag = tags;
    while (*tag) {
        while (*tag == ' ') tag++;
        const char* end = tag;
        while (*end && *end != ' ') end++;
        if (end != tag && strlen(elem->tag_name) == (size_t)(end - tag) &&
            strncasecmp(elem->tag_name, tag, (size_t)(end - tag)) == 0) return true;
        tag = end;
    }
    return false;
}

#define RADIANT_DOM_TAG_SET_GUARD(name, tags) \
    RADIANT_C_API int name(Item receiver) { return radiant_dom_member_tag_set(receiver, tags); }

RADIANT_DOM_TAG_SET_GUARD(radiant_dom_guard_dis,
    "input button select textarea fieldset option optgroup")
RADIANT_DOM_TAG_SET_GUARD(radiant_dom_guard_ist, "input select textarea")
RADIANT_DOM_TAG_SET_GUARD(radiant_dom_guard_it, "input textarea")
RADIANT_DOM_TAG_SET_GUARD(radiant_dom_guard_ib, "input button")
RADIANT_DOM_TAG_SET_GUARD(radiant_dom_guard_fist, "form input select textarea")
RADIANT_DOM_TAG_SET_GUARD(radiant_dom_guard_form, "form")
RADIANT_DOM_TAG_SET_GUARD(radiant_dom_guard_details, "details")
RADIANT_DOM_TAG_SET_GUARD(radiant_dom_guard_img, "img")
RADIANT_DOM_TAG_SET_GUARD(radiant_dom_guard_srct,
    "img script iframe embed source track audio video input")
RADIANT_DOM_TAG_SET_GUARD(radiant_dom_guard_hreft, "a area link base")
RADIANT_DOM_TAG_SET_GUARD(radiant_dom_guard_anchor, "a area")
RADIANT_DOM_TAG_SET_GUARD(radiant_dom_guard_namet,
    "input button select textarea form fieldset output object")
RADIANT_DOM_TAG_SET_GUARD(radiant_dom_guard_lblout, "label output")

#undef RADIANT_DOM_TAG_SET_GUARD
static int radiant_dom_reflected_bool_get(Item receiver, Item* out,
                                          const char* attribute) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    *out = (Item){.item = b2it(elem->has_attribute(attribute) ? 1 : 0)};
    return 1;
}

static int radiant_dom_reflected_bool_set(Item receiver, Item value, Item* out,
                                          const char* attribute, bool notify) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    if (js_is_truthy(value)) elem->set_attribute(attribute, "");
    else elem->remove_attribute(attribute);
    if (notify) {
        js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
    }
    *out = value;
    return 1;
}

#define RADIANT_REFLECT_BOOL(prefix, attribute) \
    RADIANT_C_API int prefix##_get(Item receiver, Item* out) { \
        return radiant_dom_reflected_bool_get(receiver, out, attribute); \
    } \
    RADIANT_C_API int prefix##_set(Item receiver, Item value, Item* out) { \
        return radiant_dom_reflected_bool_set(receiver, value, out, attribute, true); \
    }

RADIANT_REFLECT_BOOL(radiant_dom_m4b_required, "required")
RADIANT_REFLECT_BOOL(radiant_dom_m4b_multiple, "multiple")
RADIANT_REFLECT_BOOL(radiant_dom_m4b_read_only, "readonly")
RADIANT_REFLECT_BOOL(radiant_dom_m4b_readonly, "readonly")
RADIANT_REFLECT_BOOL(radiant_dom_m4b_no_validate, "novalidate")
RADIANT_REFLECT_BOOL(radiant_dom_m4b_form_no_validate, "formnovalidate")
RADIANT_REFLECT_BOOL(radiant_dom_m4b_open, "open")
RADIANT_REFLECT_BOOL(radiant_dom_m4b_autofocus, "autofocus")

#undef RADIANT_REFLECT_BOOL

RADIANT_C_API int radiant_dom_m4b_disabled_get(Item receiver, Item* out) {
    return radiant_dom_reflected_bool_get(receiver, out, "disabled");
}

RADIANT_C_API int radiant_dom_m4b_disabled_set(Item receiver, Item value, Item* out) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    if (js_is_truthy(value)) {
        elem->set_attribute("disabled", "");
        js_dom_after_disabled_attribute_set((void*)elem);
    } else {
        elem->remove_attribute("disabled");
    }
    js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
    *out = value;
    return 1;
}

RADIANT_C_API int radiant_dom_m4b_multiple2_get(Item receiver, Item* out) {
    return radiant_dom_reflected_bool_get(receiver, out, "multiple");
}

RADIANT_C_API int radiant_dom_m4b_multiple2_set(Item receiver, Item value, Item* out) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    bool truthy = js_is_truthy(value);
    if (truthy) elem->set_attribute("multiple", "");
    else elem->remove_attribute("multiple");
    if (!truthy) js_dom_after_select_multiple_removed((void*)elem);
    js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
    *out = value;
    return 1;
}

RADIANT_C_API int radiant_dom_m4b_default_checked_get(Item receiver, Item* out) {
    return radiant_dom_reflected_bool_get(receiver, out, "checked");
}

RADIANT_C_API int radiant_dom_m4b_default_checked_set(Item receiver, Item value, Item* out) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    bool truthy = js_is_truthy(value);
    if (truthy) elem->set_attribute("checked", "");
    else elem->remove_attribute("checked");
    js_dom_after_default_checked_set((void*)elem, truthy);
    *out = value;
    return 1;
}

RADIANT_C_API int radiant_dom_m4b_default_selected_get(Item receiver, Item* out) {
    return radiant_dom_reflected_bool_get(receiver, out, "selected");
}

RADIANT_C_API int radiant_dom_m4b_default_selected_set(Item receiver, Item value, Item* out) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    bool truthy = js_is_truthy(value);
    if (truthy) elem->set_attribute("selected", "");
    else elem->remove_attribute("selected");
    js_dom_after_default_selected_set((void*)elem, truthy);
    *out = value;
    return 1;
}

static int radiant_dom_reflected_int_get(Item receiver, Item* out,
                                         const char* attribute, long fallback) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    *out = radiant_dom_int_item(radiant_dom_reflected_int_value(elem, attribute, fallback));
    return 1;
}

static int radiant_dom_reflected_int_set(Item receiver, Item value, Item* out,
                                         const char* attribute, long fallback) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld",
        radiant_dom_item_to_reflected_int(value, fallback));
    elem->set_attribute(attribute, buf);
    js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
    *out = value;
    return 1;
}

#define RADIANT_REFLECT_INT(prefix, attribute, fallback) \
    RADIANT_C_API int prefix##_get(Item receiver, Item* out) { \
        return radiant_dom_reflected_int_get(receiver, out, attribute, fallback); \
    } \
    RADIANT_C_API int prefix##_set(Item receiver, Item value, Item* out) { \
        return radiant_dom_reflected_int_set(receiver, value, out, attribute, fallback); \
    }

RADIANT_REFLECT_INT(radiant_dom_m4b_max_length, "maxlength", -1)
RADIANT_REFLECT_INT(radiant_dom_m4b_min_length, "minlength", 0)
RADIANT_REFLECT_INT(radiant_dom_m4b_size, "size", 20)
RADIANT_REFLECT_INT(radiant_dom_m4b_size2, "size", 0)
RADIANT_REFLECT_INT(radiant_dom_m4b_width, "width", 0)
RADIANT_REFLECT_INT(radiant_dom_m4b_height, "height", 0)
RADIANT_REFLECT_INT(radiant_dom_m4b_rows, "rows", 2)
RADIANT_REFLECT_INT(radiant_dom_m4b_cols, "cols", 20)

#undef RADIANT_REFLECT_INT

static int radiant_dom_reflected_string_get(Item receiver, Item* out,
                                            const char* attribute, const char* fallback) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    const char* value = elem->get_attribute(attribute);
    *out = radiant_dom_string_item(value ? value : fallback);
    return 1;
}

static int radiant_dom_reflected_string_set(Item receiver, Item value, Item* out,
                                            const char* attribute) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    const char* text = js_dom_to_attribute_cstr(value);
    elem->set_attribute(attribute, text ? text : "");
    js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
    *out = value;
    return 1;
}

#define RADIANT_REFLECT_STRING(prefix, attribute, fallback) \
    RADIANT_C_API int prefix##_get(Item receiver, Item* out) { \
        return radiant_dom_reflected_string_get(receiver, out, attribute, fallback); \
    } \
    RADIANT_C_API int prefix##_set(Item receiver, Item value, Item* out) { \
        return radiant_dom_reflected_string_set(receiver, value, out, attribute); \
    }

RADIANT_REFLECT_STRING(radiant_dom_m4b_src, "src", "")
RADIANT_REFLECT_STRING(radiant_dom_m4b_alt, "alt", "")
RADIANT_REFLECT_STRING(radiant_dom_m4b_name, "name", "")
RADIANT_REFLECT_STRING(radiant_dom_m4b_placeholder, "placeholder", "")
RADIANT_REFLECT_STRING(radiant_dom_m4b_autocomplete, "autocomplete", "")
RADIANT_REFLECT_STRING(radiant_dom_m4b_pattern, "pattern", "")
RADIANT_REFLECT_STRING(radiant_dom_m4b_min, "min", "")
RADIANT_REFLECT_STRING(radiant_dom_m4b_max, "max", "")
RADIANT_REFLECT_STRING(radiant_dom_m4b_step, "step", "")
RADIANT_REFLECT_STRING(radiant_dom_m4b_accept, "accept", "")
RADIANT_REFLECT_STRING(radiant_dom_m4b_html_for, "for", "")
RADIANT_REFLECT_STRING(radiant_dom_m4b_target, "target", "")
RADIANT_REFLECT_STRING(radiant_dom_m4b_accept_charset, "accept-charset", "")
RADIANT_REFLECT_STRING(radiant_dom_m4b_form_target, "formtarget", "")
RADIANT_REFLECT_STRING(radiant_dom_m4b_wrap, "wrap", "soft")

#undef RADIANT_REFLECT_STRING

RADIANT_C_API int radiant_dom_m4b_href_get(Item receiver, Item* out) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    const char* value = elem->get_attribute("href");
    if (radiant_dom_is_tag(elem, "a") || radiant_dom_is_tag(elem, "area")) {
        Url* resolved = elem->doc && elem->doc->url
            ? url_parse_with_base(value ? value : "", elem->doc->url)
            : url_parse(value ? value : "");
        *out = radiant_dom_string_item(resolved ? url_get_href(resolved) : "");
        if (resolved) url_destroy(resolved);
    } else {
        *out = radiant_dom_string_item(value ? value : "");
    }
    return 1;
}

RADIANT_C_API int radiant_dom_m4b_href_set(Item receiver, Item value, Item* out) {
    return radiant_dom_reflected_string_set(receiver, value, out, "href");
}

typedef const char* (*RadiantUrlGetter)(const Url* url);

static int radiant_dom_anchor_component(Item receiver, Item* out,
                                        RadiantUrlGetter getter) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out || !getter) return 0;
    const char* href = elem->get_attribute("href");
    Url* resolved = elem->doc && elem->doc->url
        ? url_parse_with_base(href ? href : "", elem->doc->url)
        : url_parse(href ? href : "");
    *out = radiant_dom_string_item(resolved ? getter(resolved) : "");
    if (resolved) url_destroy(resolved);
    return 1;
}

#define RADIANT_ANCHOR_COMPONENT_GETTER(fn_name, url_getter) \
    RADIANT_C_API int fn_name(Item receiver, Item* out) { \
        return radiant_dom_anchor_component(receiver, out, url_getter); \
    }

RADIANT_ANCHOR_COMPONENT_GETTER(radiant_dom_anchor_protocol_get, url_get_protocol)
RADIANT_ANCHOR_COMPONENT_GETTER(radiant_dom_anchor_host_get, url_get_host)
RADIANT_ANCHOR_COMPONENT_GETTER(radiant_dom_anchor_hostname_get, url_get_hostname)
RADIANT_ANCHOR_COMPONENT_GETTER(radiant_dom_anchor_pathname_get, url_get_pathname)
RADIANT_ANCHOR_COMPONENT_GETTER(radiant_dom_anchor_search_get, url_get_search)
RADIANT_ANCHOR_COMPONENT_GETTER(radiant_dom_anchor_origin_get, url_get_origin)

RADIANT_C_API int radiant_dom_anchor_hash_get(Item receiver, Item* out) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    const char* href = elem->get_attribute("href");
    // Fragment-only references are valid URL references even when the host
    // parser has no base-path component; their hash is still the raw suffix.
    const char* hash = href ? strchr(href, '#') : nullptr;
    *out = radiant_dom_string_item(hash ? hash : "");
    return 1;
}

#undef RADIANT_ANCHOR_COMPONENT_GETTER

RADIANT_C_API int radiant_dom_m4b_input_mode_get(Item receiver, Item* out) {
    static const char* const keywords[] = {
        "none", "text", "decimal", "numeric", "tel", "search", "email", "url", nullptr
    };
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    *out = radiant_dom_string_item(
        radiant_dom_canonical_token_attr(elem, "inputmode", keywords));
    return 1;
}

RADIANT_C_API int radiant_dom_m4b_input_mode_set(Item receiver, Item value, Item* out) {
    return radiant_dom_reflected_string_set(receiver, value, out, "inputmode");
}

RADIANT_C_API int radiant_dom_m4b_enter_key_hint_get(Item receiver, Item* out) {
    static const char* const keywords[] = {
        "enter", "done", "go", "next", "previous", "search", "send", nullptr
    };
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    *out = radiant_dom_string_item(
        radiant_dom_canonical_token_attr(elem, "enterkeyhint", keywords));
    return 1;
}

RADIANT_C_API int radiant_dom_m4b_enter_key_hint_set(Item receiver, Item value, Item* out) {
    return radiant_dom_reflected_string_set(receiver, value, out, "enterkeyhint");
}

RADIANT_C_API int radiant_dom_m4b_content_editable_get(Item receiver, Item* out) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    if (!elem->has_attribute("contenteditable")) {
        *out = radiant_dom_string_item("inherit");
        return 1;
    }
    const char* normalized = radiant_dom_normalize_contenteditable(
        elem->get_attribute("contenteditable"));
    *out = radiant_dom_string_item(normalized ? normalized : "inherit");
    return 1;
}

RADIANT_C_API int radiant_dom_m4b_content_editable_set(Item receiver, Item value, Item* out) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    const char* text = nullptr;
    if (get_type_id(value) == LMD_TYPE_BOOL) {
        text = it2b(value) ? "true" : "false";
    } else {
        text = fn_to_cstr(value);
    }
    if (!text) text = "";
    if (*text == '\0') {
        elem->remove_attribute("contenteditable");
    } else {
        const char* normalized = radiant_dom_normalize_contenteditable(text);
        if (!normalized) {
            *out = js_dom_throw_contenteditable_syntax_error();
            return 1;
        }
        if (strcmp(normalized, "inherit") == 0) {
            elem->remove_attribute("contenteditable");
        } else {
            elem->set_attribute("contenteditable", normalized);
        }
    }
    js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
    *out = value;
    return 1;
}

RADIANT_C_API int radiant_dom_m4b_is_content_editable_get(Item receiver, Item* out) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    *out = (Item){.item = b2it(radiant_dom_is_content_editable(elem) ? 1 : 0)};
    return 1;
}

// ---- DOM3 Phase 4c: live form-control members ----
RADIANT_C_API int radiant_dom_guard_tc(Item receiver) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    return elem && tc_is_text_control(elem);
}

RADIANT_C_API int radiant_dom_guard_input_typed_value(Item receiver) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !radiant_dom_is_tag(elem, "input")) return 0;
    const char* type = elem->get_attribute("type");
    RadiantInputValueKind kind = radiant_input_value_kind(type);
    return kind != RADIANT_INPUT_VALUE_TEXT &&
           kind != RADIANT_INPUT_VALUE_UNSUPPORTED;
}

static Item radiant_dom_input_empty_file_list(void) {
    Item constructor = radiant_host_api->script->global_property(
        (Item){.item = s2it(heap_create_name("DataTransfer"))});
    if (get_type_id(constructor) != LMD_TYPE_FUNC) {
        return radiant_host_api->value->array_new(0);
    }
    Item transfer = radiant_host_api->script->call_function(
        constructor, (Item){.item = ITEM_JS_UNDEFINED}, nullptr, 0);
    return radiant_host_api->value->property_get(
        transfer, (Item){.item = s2it(heap_create_name("files"))});
}

static Item radiant_dom_input_files_get(DomElement* elem) {
    if (radiant_input_value_kind(elem->get_attribute("type")) !=
        RADIANT_INPUT_VALUE_FILE) return ItemNull;
    Item files = radiant_input_files(elem);
    if (get_type_id(files) != LMD_TYPE_ARRAY) {
        files = radiant_dom_input_empty_file_list();
        radiant_input_set_files(elem, files);
    }
    return files;
}

static Item radiant_dom_input_throw(const char* name, const char* message) {
    Item error = radiant_host_api->script->new_error_with_name(
        (Item){.item = s2it(heap_create_name(name))},
        (Item){.item = s2it(heap_create_name(message))});
    // the host ABI carries exceptions in the returned Item; discarding this
    // lane would let a rejected DOM setter continue as a successful write.
    return radiant_host_api->script->throw_value(error);
}

RADIANT_C_API int radiant_dom_input_type_get(Item r, Item* out) {
    DomElement* elem = radiant_dom_member_elem(r);
    if (!elem || !out) return 0;
    char normalized[32];
    *out = (Item){.item = s2it(heap_create_name(radiant_input_type_normalize(
        elem->get_attribute("type"), normalized, sizeof(normalized))))};
    return 1;
}

RADIANT_C_API int radiant_dom_input_type_set(Item r, Item v, Item* out) {
    DomElement* elem = radiant_dom_member_elem(r);
    if (!elem || !out) return 0;
    char normalized[32];
    const char* type = radiant_input_type_normalize(fn_to_cstr(v), normalized,
                                                     sizeof(normalized));
    elem->set_attribute("type", type);
    // The live value belongs to a value state, so a type transition must run
    // that state's sanitizer instead of leaving an impossible old value behind.
    radiant_input_type_changed(elem);
    js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem,
                           (void*)elem->parent);
    *out = v;
    return 1;
}

RADIANT_C_API int radiant_dom_input_typed_value_get(Item r, Item* out) {
    DomElement* elem = radiant_dom_member_elem(r);
    if (!elem || !out) return 0;
    RadiantInputValueKind kind = radiant_input_value_kind(
        elem->get_attribute("type"));
    if (kind == RADIANT_INPUT_VALUE_FILE) {
        Item files = radiant_dom_input_files_get(elem);
        if (radiant_host_api->value->array_length(files) <= 0) {
            *out = (Item){.item = s2it(heap_create_name(""))};
            return 1;
        }
        Item file = radiant_host_api->value->array_get(files, 0);
        Item name = radiant_host_api->value->property_get(
            file, (Item){.item = s2it(heap_create_name("name"))});
        const char* filename = fn_to_cstr(name);
        char fake_path[512];
        snprintf(fake_path, sizeof(fake_path), "C:\\fakepath\\%s",
                 filename ? filename : "");
        *out = (Item){.item = s2it(heap_create_name(fake_path))};
        return 1;
    }
    *out = (Item){.item = s2it(heap_create_name(radiant_input_live_value(elem)))};
    return 1;
}

RADIANT_C_API int radiant_dom_input_typed_value_set(Item r, Item v, Item* out) {
    DomElement* elem = radiant_dom_member_elem(r);
    if (!elem || !out) return 0;
    const char* text = fn_to_cstr(v);
    if (!text) text = "";
    RadiantInputValueKind kind = radiant_input_value_kind(
        elem->get_attribute("type"));
    if (kind == RADIANT_INPUT_VALUE_FILE) {
        if (text[0]) {
            // File inputs cannot manufacture host paths from script-provided text.
            *out = radiant_dom_input_throw("InvalidStateError",
                                          "File input value can only be set to the empty string");
            return 1;
        } else {
            radiant_input_set_files(elem, radiant_dom_input_empty_file_list());
        }
        *out = v;
        return 1;
    }
    radiant_input_set_live_value(elem, text);
    if (elem->form) elem->form->value = radiant_input_live_value(elem);
    *out = v;
    return 1;
}

RADIANT_C_API int radiant_dom_input_value_as_number_get(Item r, Item* out) {
    DomElement* elem = radiant_dom_member_elem(r);
    if (!elem || !out) return 0;
    double number = NAN;
    radiant_input_value_as_number(elem->get_attribute("type"),
                                  radiant_input_live_value(elem), &number);
    *out = radiant_host_api->script->make_number(number);
    return 1;
}

RADIANT_C_API int radiant_dom_input_value_as_number_set(Item r, Item v, Item* out) {
    DomElement* elem = radiant_dom_member_elem(r);
    if (!elem || !out) return 0;
    double number = radiant_host_api->script->get_number(v);
    char formatted[128];
    if (!isfinite(number)) {
        *out = radiant_dom_input_throw("TypeError", "valueAsNumber must be finite");
        return 1;
    } else if (!radiant_input_value_from_number(
                   elem->get_attribute("type"), number,
                   formatted, sizeof(formatted))) {
        *out = radiant_dom_input_throw("InvalidStateError",
                                      "This input type has no numeric value state");
        return 1;
    } else {
        radiant_input_set_live_value(elem, formatted);
    }
    *out = v;
    return 1;
}

RADIANT_C_API int radiant_dom_input_value_as_date_get(Item r, Item* out) {
    DomElement* elem = radiant_dom_member_elem(r);
    if (!elem || !out) return 0;
    const char* type = elem->get_attribute("type");
    double number;
    if (!radiant_input_value_as_date_supported(type) ||
        !radiant_input_value_as_number(type, radiant_input_live_value(elem), &number)) {
        *out = ItemNull;
        return 1;
    }
    *out = radiant_host_api->script->date_new_from(
        radiant_host_api->script->make_number(number));
    return 1;
}

RADIANT_C_API int radiant_dom_input_value_as_date_set(Item r, Item v, Item* out) {
    DomElement* elem = radiant_dom_member_elem(r);
    if (!elem || !out) return 0;
    const char* type = elem->get_attribute("type");
    if (!radiant_input_value_as_date_supported(type)) {
        *out = radiant_dom_input_throw("InvalidStateError",
                                      "This input type has no Date value state");
        return 1;
    } else if (v.item == ITEM_NULL) {
        radiant_input_set_live_value(elem, "");
    } else if (radiant_host_api->script->class_id(v) != JS_CLASS_DATE) {
        *out = radiant_dom_input_throw("TypeError", "valueAsDate requires a Date or null");
        return 1;
    } else {
        Item time = radiant_host_api->script->date_method(v, 0);
        double number = radiant_host_api->script->get_number(time);
        char formatted[128];
        if (!isfinite(number)) {
            radiant_input_set_live_value(elem, "");
        } else if (radiant_input_value_from_number(type, number, formatted,
                                                    sizeof(formatted))) {
            radiant_input_set_live_value(elem, formatted);
        }
    }
    *out = v;
    return 1;
}

RADIANT_C_API int radiant_dom_input_files_get_member(Item r, Item* out) {
    DomElement* elem = radiant_dom_member_elem(r);
    if (!elem || !out) return 0;
    *out = radiant_dom_input_files_get(elem);
    return 1;
}

RADIANT_C_API int radiant_dom_input_files_set_member(Item r, Item v, Item* out) {
    DomElement* elem = radiant_dom_member_elem(r);
    if (!elem || !out) return 0;
    if (radiant_input_value_kind(elem->get_attribute("type")) !=
        RADIANT_INPUT_VALUE_FILE) {
        *out = v;
        return 1;
    }
    if (v.item == ITEM_NULL) {
        radiant_input_set_files(elem, radiant_dom_input_empty_file_list());
    } else if (get_type_id(v) == LMD_TYPE_ARRAY &&
               radiant_host_api->script->class_id(v) == JS_CLASS_FILE_LIST) {
        // Only the browser-created FileList brand is assignable; accepting an
        // arbitrary Array would let script bypass the file-input security model.
        radiant_input_set_files(elem, v);
    } else {
        *out = radiant_dom_input_throw("TypeError", "files must be a FileList or null");
        return 1;
    }
    *out = v;
    return 1;
}

RADIANT_C_API int radiant_dom_input_step_up(Item r, Item* args, int argc, Item* out) {
    DomElement* elem = radiant_dom_member_elem(r);
    if (!elem || !out) return 0;
    int count = argc > 0 ? (int)radiant_host_api->script->get_number(args[0]) : 1;
    char stepped[128];
    if (!radiant_input_value_step(elem->get_attribute("type"),
            radiant_input_live_value(elem), elem->get_attribute("min"),
            elem->get_attribute("max"), elem->get_attribute("step"),
            count, stepped, sizeof(stepped))) {
        *out = radiant_dom_input_throw("InvalidStateError", "Input value cannot be stepped");
        return 1;
    } else {
        radiant_input_set_live_value(elem, stepped);
    }
    *out = (Item){.item = ITEM_JS_UNDEFINED};
    return 1;
}

RADIANT_C_API int radiant_dom_input_step_down(Item r, Item* args, int argc, Item* out) {
    if (argc > 0) {
        Item negated = radiant_host_api->script->make_number(
            -radiant_host_api->script->get_number(args[0]));
        return radiant_dom_input_step_up(r, &negated, 1, out);
    }
    Item minus_one = radiant_host_api->script->make_number(-1.0);
    return radiant_dom_input_step_up(r, &minus_one, 1, out);
}
static int radiant_dom_m4c_get_property(Item receiver, Item* out,
                                          const char* property) {
    if (!out) return 0;
    *out = js_dom_get_property_impl(receiver,
        (Item){.item = s2it(heap_create_name(property))});
    return 1;
}

#define RADIANT_M4C_GET(name, property) \
    RADIANT_C_API int name(Item receiver, Item* out) { \
        return radiant_dom_m4c_get_property(receiver, out, property); \
    }

RADIANT_M4C_GET(radiant_dom_m4c_get_checked, "checked")
RADIANT_M4C_GET(radiant_dom_m4c_get_value, "value")
RADIANT_M4C_GET(radiant_dom_m4c_get_selectedIndex, "selectedIndex")
RADIANT_M4C_GET(radiant_dom_m4c_get_length, "length")
RADIANT_M4C_GET(radiant_dom_m4c_get_selected, "selected")
RADIANT_M4C_GET(radiant_dom_m4c_get_text, "text")
RADIANT_M4C_GET(radiant_dom_m4c_get_selectionStart, "selectionStart")
RADIANT_M4C_GET(radiant_dom_m4c_get_selectionEnd, "selectionEnd")
RADIANT_M4C_GET(radiant_dom_m4c_get_selectionDirection, "selectionDirection")
RADIANT_M4C_GET(radiant_dom_m4c_get_defaultValue, "defaultValue")
RADIANT_M4C_GET(radiant_dom_m4c_get_options, "options")
RADIANT_M4C_GET(radiant_dom_m4c_get_selectedOptions, "selectedOptions")
RADIANT_M4C_GET(radiant_dom_m4c_get_type, "type")
RADIANT_M4C_GET(radiant_dom_m4c_get_index, "index")
RADIANT_M4C_GET(radiant_dom_m4c_get_label, "label")
RADIANT_M4C_GET(radiant_dom_m4c_get_form, "form")

#undef RADIANT_M4C_GET

typedef Item (*RadiantDomItemSetter)(void* element, Item value);

static int radiant_dom_m4c_set_bridge(Item receiver, Item value, Item* out,
                                      RadiantDomItemSetter setter) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out || !setter) return 0;
    *out = setter((void*)elem, value);
    return 1;
}

#define RADIANT_M4C_SET(name, setter) \
    RADIANT_C_API int name(Item receiver, Item value, Item* out) { \
        return radiant_dom_m4c_set_bridge(receiver, value, out, setter); \
    }

RADIANT_C_API int radiant_dom_m4c_checked_set(Item receiver, Item value, Item* out) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    js_dom_set_checked_dirty((void*)elem, js_is_truthy(value));
    *out = value;
    return 1;
}

RADIANT_C_API int radiant_dom_m4c_value_set(Item receiver, Item value, Item* out) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    const char* text = fn_to_cstr(value);
    js_dom_select_set_value_bridge((void*)elem, text ? text : "");
    *out = value;
    return 1;
}

RADIANT_M4C_SET(radiant_dom_m4c_value2_set, js_dom_text_control_set_value_bridge)

static int radiant_dom_m4c_attribute_set(Item receiver, Item value, Item* out,
                                         bool update_form, bool notify) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    const char* text = fn_to_cstr(value);
    elem->set_attribute("value", text ? text : "");
    if (update_form && elem->form) elem->form->value = elem->get_attribute("value");
    if (notify) {
        js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
    }
    *out = value;
    return 1;
}

RADIANT_C_API int radiant_dom_m4c_value3_set(Item receiver, Item value, Item* out) {
    return radiant_dom_m4c_attribute_set(receiver, value, out, true, true);
}

RADIANT_C_API int radiant_dom_m4c_value4_set(Item receiver, Item value, Item* out) {
    return radiant_dom_m4c_attribute_set(receiver, value, out, false, false);
}

typedef void (*RadiantDomVoidSetter)(void* element, Item value);

static int radiant_dom_m4c_set_void(Item receiver, Item value, Item* out,
                                    RadiantDomVoidSetter setter) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out || !setter) return 0;
    setter((void*)elem, value);
    *out = value;
    return 1;
}

#define RADIANT_M4C_VOID_SET(name, setter) \
    RADIANT_C_API int name(Item receiver, Item value, Item* out) { \
        return radiant_dom_m4c_set_void(receiver, value, out, setter); \
    }

RADIANT_M4C_VOID_SET(radiant_dom_m4c_selected_index_set, js_dom_select_set_selected_index_bridge)
RADIANT_M4C_VOID_SET(radiant_dom_m4c_length_set, js_dom_select_set_length_bridge)

RADIANT_C_API int radiant_dom_m4c_selected_set(Item receiver, Item value, Item* out) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    js_dom_set_option_selected_dirty((void*)elem, js_is_truthy(value));
    *out = value;
    return 1;
}

RADIANT_C_API int radiant_dom_m4c_text_set(Item receiver, Item value, Item* out) {
    DomElement* elem = radiant_dom_member_elem(receiver);
    if (!elem || !out) return 0;
    const char* text = fn_to_cstr(value);
    js_dom_set_option_text_bridge((void*)elem, text ? text : "");
    *out = value;
    return 1;
}

RADIANT_M4C_SET(radiant_dom_m4c_selection_start_set,
                js_dom_text_control_set_selection_start_bridge)
RADIANT_M4C_SET(radiant_dom_m4c_selection_end_set,
                js_dom_text_control_set_selection_end_bridge)
RADIANT_M4C_SET(radiant_dom_m4c_selection_direction_set,
                js_dom_text_control_set_selection_direction_bridge)
RADIANT_M4C_SET(radiant_dom_m4c_default_value_set,
                js_dom_text_control_set_default_value_bridge)

#undef RADIANT_M4C_SET
#undef RADIANT_M4C_VOID_SET
static int radiant_dom_member_character_data_property(Item receiver,
                                                      const char* prop,
                                                      Item* out) {
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(receiver);
    if (!node || !prop || !out) return 0;
    if (node->is_text()) {
        DomText* text = node->as_text();
        return radiant_dom_get_character_data_property(node, text->text,
            radiant_dom_utf16_length(text->text), 3, "#text", prop, out) ? 1 : 0;
    }
    if (node->is_comment()) {
        DomComment* comment = node->as_comment();
        return radiant_dom_get_character_data_property(node, comment->content,
            comment->length, comment->node_type, "#comment", prop, out) ? 1 : 0;
    }
    return 0;
}

RADIANT_C_API int radiant_dom_member_data(Item receiver, Item* out) {
    return radiant_dom_member_character_data_property(receiver, "data", out);
}

RADIANT_C_API int radiant_dom_member_node_value(Item receiver, Item* out) {
    return radiant_dom_member_character_data_property(receiver, "nodeValue", out);
}

RADIANT_C_API int radiant_dom_member_text_content(Item receiver, Item* out) {
    return radiant_dom_member_character_data_property(receiver, "textContent", out);
}

RADIANT_C_API int radiant_dom_member_node_name(Item receiver, Item* out) {
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(receiver);
    if (!node || !out) return 0;
    if (node->is_element()) {
        *out = (Item){.item = s2it(radiant_dom_uppercase_name(node->as_element()->tag_name))};
        return 1;
    }
    // Text/comment wrappers no longer fall through VMap camelization; their
    // shared Node fields must be resolved by the record table directly.
    return radiant_dom_member_character_data_property(receiver, "nodeName", out);
}

RADIANT_C_API int radiant_dom_member_node_type_any(Item receiver, Item* out) {
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(receiver);
    if (!node || !out) return 0;
    if (node->is_element()) {
        DomElement* elem = node->as_element();
        // DocumentFragment uses the shared container storage internally, but
        // its projected DOM discriminator must remain nodeType 11.
        *out = radiant_dom_int_item(
            radiant_dom_is_tag(elem, "#document-fragment") ? 11 :
            (int64_t)elem->node_type);
        return 1;
    }
    return radiant_dom_member_character_data_property(receiver, "nodeType", out);
}

RADIANT_C_API int radiant_dom_member_parent_node_any(Item receiver, Item* out) {
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(receiver);
    if (!node || !out) return 0;
    DomNode* parent = node->parent;
    *out = (parent && parent->is_element()) ? radiant_dom_node_item(parent) : ItemNull;
    return 1;
}

RADIANT_C_API int radiant_dom_member_parent_element_any(Item receiver, Item* out) {
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(receiver);
    if (!node || !out) return 0;
    // parentElement is narrower than parentNode: an internal Document or
    // DocumentFragment shell must terminate the Element ancestor walk.
    DomNode* parent = node->parent;
    *out = radiant_dom_node_is_dom_element(parent)
        ? radiant_dom_node_item(parent) : ItemNull;
    return 1;
}

RADIANT_C_API int radiant_dom_member_is_connected_any(Item receiver, Item* out) {
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(receiver);
    if (!node || !out) return 0;
    *out = (Item){.item = b2it(radiant_dom_node_is_connected(node) ? 1 : 0)};
    return 1;
}

RADIANT_C_API int radiant_dom_member_owner_document_any(Item receiver, Item* out) {
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(receiver);
    if (!node || !out) return 0;
    if (node->is_element() && node->as_element()->doc) {
        *out = radiant_dom_document_item(node->as_element()->doc);
        return 1;
    }
    DomNode* parent = node->parent;
    DomDocument* doc = (parent && parent->is_element()) ? parent->as_element()->doc : nullptr;
    *out = doc ? radiant_dom_document_item(doc) : js_dom_owner_document_for_node((void*)node);
    return 1;
}

RADIANT_C_API int radiant_dom_member_first_child_any(Item receiver, Item* out) {
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(receiver);
    if (!node || !out) return 0;
    *out = node->is_element()
        ? radiant_dom_node_item(radiant_dom_first_script_visible_child(node->as_element()))
        : ItemNull;
    return 1;
}

RADIANT_C_API int radiant_dom_member_last_child_any(Item receiver, Item* out) {
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(receiver);
    if (!node || !out) return 0;
    *out = node->is_element()
        ? radiant_dom_node_item(radiant_dom_last_script_visible_child(node->as_element()))
        : ItemNull;
    return 1;
}

RADIANT_C_API int radiant_dom_member_next_sibling_any(Item receiver, Item* out) {
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(receiver);
    if (!node || !out) return 0;
    *out = radiant_dom_node_item(radiant_dom_next_script_visible_sibling(node));
    return 1;
}

RADIANT_C_API int radiant_dom_member_previous_sibling_any(Item receiver, Item* out) {
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(receiver);
    if (!node || !out) return 0;
    *out = radiant_dom_node_item(radiant_dom_prev_script_visible_sibling(node));
    return 1;
}

RADIANT_C_API int radiant_dom_member_child_nodes_any(Item receiver, Item* out) {
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(receiver);
    if (!node || !out) return 0;
    *out = node->is_element()
        ? js_dom_live_child_collection_bridge((void*)node->as_element(), false)
        : radiant_dom_array_item();
    return 1;
}

#define RADIANT_DOM_OPERATION_BINDING(name, operation) \
    RADIANT_C_API int name(Item receiver, Item* args, int argc, Item* out) { \
        *out = radiant_dom_element_operation(receiver, operation, args, argc); \
        return 1; \
    }
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_named_item, JUBE_DOM_NAMED_ITEM)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_add, JUBE_DOM_ADD)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_remove, JUBE_DOM_REMOVE)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_contains, JUBE_DOM_CONTAINS)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_compare_document_position, JUBE_DOM_COMPARE_DOCUMENT_POSITION)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_get_root_node, JUBE_DOM_GET_ROOT_NODE)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_remove2, JUBE_DOM_REMOVE)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_replace_with, JUBE_DOM_REPLACE_WITH)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_after, JUBE_DOM_AFTER)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_before, JUBE_DOM_BEFORE)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_has_child_nodes, JUBE_DOM_HAS_CHILD_NODES)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_clone_node, JUBE_DOM_CLONE_NODE)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_replace_data, JUBE_DOM_REPLACE_DATA)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_insert_data, JUBE_DOM_INSERT_DATA)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_append_data, JUBE_DOM_APPEND_DATA)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_delete_data, JUBE_DOM_DELETE_DATA)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_substring_data, JUBE_DOM_SUBSTRING_DATA)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_get_attribute, JUBE_DOM_GET_ATTRIBUTE)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_set_attribute, JUBE_DOM_SET_ATTRIBUTE)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_remove_attribute, JUBE_DOM_REMOVE_ATTRIBUTE)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_toggle_attribute, JUBE_DOM_TOGGLE_ATTRIBUTE)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_has_attribute, JUBE_DOM_HAS_ATTRIBUTE)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_get_attribute_names, JUBE_DOM_GET_ATTRIBUTE_NAMES)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_matches, JUBE_DOM_MATCHES)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_query_selector, JUBE_DOM_QUERY_SELECTOR)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_query_selector_all, JUBE_DOM_QUERY_SELECTOR_ALL)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_closest, JUBE_DOM_CLOSEST)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_get_elements_by_tag_name, JUBE_DOM_GET_ELEMENTS_BY_TAG_NAME)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_get_elements_by_class_name, JUBE_DOM_GET_ELEMENTS_BY_CLASS_NAME)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_get_element_by_id, JUBE_DOM_GET_ELEMENT_BY_ID)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_add_event_listener, JUBE_DOM_ADD_EVENT_LISTENER)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_remove_event_listener, JUBE_DOM_REMOVE_EVENT_LISTENER)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_dispatch_event, JUBE_DOM_DISPATCH_EVENT)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_append_child, JUBE_DOM_APPEND_CHILD)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_remove_child, JUBE_DOM_REMOVE_CHILD)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_insert_before, JUBE_DOM_INSERT_BEFORE)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_replace_child, JUBE_DOM_REPLACE_CHILD)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_normalize, JUBE_DOM_NORMALIZE)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_append, JUBE_DOM_APPEND)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_prepend, JUBE_DOM_PREPEND)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_insert_adjacent_element, JUBE_DOM_INSERT_ADJACENT_ELEMENT)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_insert_adjacent_html, JUBE_DOM_INSERT_ADJACENT_HTML)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_get_bounding_client_rect, JUBE_DOM_GET_BOUNDING_CLIENT_RECT)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_get_client_rects, JUBE_DOM_GET_CLIENT_RECTS)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_scroll_into_view, JUBE_DOM_SCROLL_INTO_VIEW)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_scroll, JUBE_DOM_SCROLL)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_scroll_to, JUBE_DOM_SCROLL_TO)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_scroll_by, JUBE_DOM_SCROLL_BY)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_focus, JUBE_DOM_FOCUS)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_blur, JUBE_DOM_BLUR)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_click, JUBE_DOM_CLICK)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_reset, JUBE_DOM_RESET)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_submit, JUBE_DOM_SUBMIT)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_request_submit, JUBE_DOM_REQUEST_SUBMIT)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_check_validity, JUBE_DOM_CHECK_VALIDITY)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_report_validity, JUBE_DOM_REPORT_VALIDITY)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_set_custom_validity, JUBE_DOM_SET_CUSTOM_VALIDITY)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_set_selection_range, JUBE_DOM_SET_SELECTION_RANGE)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_set_range_text, JUBE_DOM_SET_RANGE_TEXT)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_select, JUBE_DOM_SELECT)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_item, JUBE_DOM_ITEM)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_toggle, JUBE_DOM_TOGGLE)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_replace, JUBE_DOM_REPLACE)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_attach_shadow, JUBE_DOM_ATTACH_SHADOW)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_to_string, JUBE_DOM_TO_STRING)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d___lambda_boundary_from_point, JUBE_DOM_BOUNDARY_FROM_POINT)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d___lambda_text_control_boundary_from_point, JUBE_DOM_TEXT_CONTROL_BOUNDARY_FROM_POINT)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d___lambda_text_control_caret_bounds, JUBE_DOM_TEXT_CONTROL_CARET_BOUNDS)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_get_attribute_ns, JUBE_DOM_GET_ATTRIBUTE_NS)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_set_attribute_ns, JUBE_DOM_SET_ATTRIBUTE_NS)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_remove_attribute_ns, JUBE_DOM_REMOVE_ATTRIBUTE_NS)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_is_equal_node, JUBE_DOM_IS_EQUAL_NODE)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_is_same_node, JUBE_DOM_IS_SAME_NODE)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_create_svg_point, JUBE_DOM_CREATE_SVG_POINT)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_create_svg_matrix, JUBE_DOM_CREATE_SVG_MATRIX)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_create_svg_transform, JUBE_DOM_CREATE_SVG_TRANSFORM)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_create_svg_transform_from_matrix, JUBE_DOM_CREATE_SVG_TRANSFORM_FROM_MATRIX)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_get_bbox, JUBE_DOM_GET_BBOX)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_get_ctm, JUBE_DOM_GET_CTM)
RADIANT_DOM_OPERATION_BINDING(radiant_dom_m4d_get_screen_ctm, JUBE_DOM_GET_SCREEN_CTM)

#undef RADIANT_DOM_OPERATION_BINDING

static void radiant_dom_commit_geometry_layout(DomDocument* doc) {
    if (!doc || !doc->root || s_radiant_dom_geometry_layout_active) return;
    UiContext* uicon = (UiContext*)doc->js.host_ui_context;
    if (!uicon) return;
    // geometry reads must reconcile pending DOM mutations before the first snapshot.
    s_radiant_dom_geometry_layout_active = true;
    DomDocument* saved_document = uicon->document;
    uicon->document = doc;
    process_document_font_faces(uicon, doc);
    if (!doc->js.host_driven_loop && doc->js.mutation_count > 0) {
        radiant_reconcile_js_dom_mutations(uicon, doc);
    } else if (!doc->view_tree || !doc->view_tree->root) {
        layout_html_doc(uicon, doc, false);
    }
    uicon->document = saved_document;
    s_radiant_dom_geometry_layout_active = false;
}

RADIANT_C_API Item radiant_dom_get_property(Item elem_item, Item prop_name) {
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(elem_item);
    const char* prop = fn_to_cstr(prop_name);
    if (node && node->is_element() && prop &&
        (strcmp(prop, "offsetWidth") == 0 || strcmp(prop, "offsetHeight") == 0 ||
         strcmp(prop, "offsetTop") == 0 || strcmp(prop, "offsetLeft") == 0 ||
         strcmp(prop, "offsetParent") == 0 || strncmp(prop, "client", 6) == 0 ||
         strncmp(prop, "scroll", 6) == 0)) {
        radiant_dom_commit_geometry_layout(node->as_element()->doc);
        radiant_dom_has_committed_geometry_snapshot(node->as_element()->doc);
    }
    return js_dom_get_property_impl(elem_item, prop_name);
}

RADIANT_C_API Item radiant_dom_set_property(Item elem_item, Item prop_name, Item value) {
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(elem_item);
    const char* prop = fn_to_cstr(prop_name);
    if (node && node->is_element() && prop &&
            radiant_dom_is_attr_name_projection(prop) &&
            !radiant_dom_is_internal_attr(prop)) {
        const char* text = js_dom_to_attribute_cstr(value);
        node->as_element()->set_attribute(prop, text ? text : "");
        js_dom_after_set_attribute((void*)node, prop, text ? text : "");
        js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE,
            (void*)node, (void*)node->parent);
        return value;
    }
    return js_dom_set_property_impl(elem_item, prop_name, value);
}

RADIANT_C_API Item radiant_dom_element_operation(Item elem_item,
                                                  JubeDomElementOperation operation,
                                                  Item* args, int argc) {
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(elem_item);
    if (!node) return ItemNull;
    if (operation == JUBE_DOM_CONTAINS) {
        DomNode* other = (argc >= 1) ? (DomNode*)radiant_dom_unwrap_node(args[0]) : nullptr;
        return (Item){.item = b2it(radiant_dom_node_contains(node, other) ? 1 : 0)};
    }
    if (operation == JUBE_DOM_COMPARE_DOCUMENT_POSITION) {
        DomNode* other = (argc >= 1) ? (DomNode*)radiant_dom_unwrap_node(args[0]) : nullptr;
        return radiant_dom_int_item(radiant_dom_compare_document_position(node, other));
    }
    if (operation == JUBE_DOM_GET_ROOT_NODE) {
        // Shadow DOM is deferred, so composed and non-composed roots coincide.
        return radiant_dom_node_root_item(node);
    }
    if (node->is_element() && radiant_dom_is_tag(node->as_element(), "select") &&
        (operation == JUBE_DOM_NAMED_ITEM || operation == JUBE_DOM_ADD ||
         operation == JUBE_DOM_REMOVE)) {
        // HTMLSelectElement overrides ChildNode.remove(); preserve the option
        // list overload before the generic node-removal bridge.
        return js_dom_element_operation_impl(elem_item, operation, args, argc);
    }
    if (operation == JUBE_DOM_REMOVE) {
        // Node.remove() must use the backed-tree path so renderer and DOM
        // sibling state are retired together and observers receive a detail.
        return js_dom_remove_bridge((void*)node);
    }
    if (operation == JUBE_DOM_REPLACE_WITH) {
        return js_dom_replace_with_bridge((void*)node, args, argc);
    }
    return js_dom_element_operation_impl(elem_item, operation, args, argc);
}

static bool radiant_dom_key_equals(Item key, const char* name, uint32_t name_len) {
    if (get_type_id(key) != LMD_TYPE_STRING) return false;
    String* str_key = it2s(key);
    return str_key && str_key->len == name_len &&
        strncmp(str_key->chars, name, name_len) == 0;
}

static Item radiant_dom_data_descriptor(Item value, bool writable,
                                        bool enumerable, bool configurable) {
    Item desc = radiant_host_api->value->new_object();
    radiant_host_api->value->property_set(desc, (Item){.item = s2it(heap_create_name("value"))}, value);
    radiant_host_api->value->property_set(desc, (Item){.item = s2it(heap_create_name("writable"))},
        (Item){.item = b2it(writable ? 1 : 0)});
    radiant_host_api->value->property_set(desc, (Item){.item = s2it(heap_create_name("enumerable"))},
        (Item){.item = b2it(enumerable ? 1 : 0)});
    radiant_host_api->value->property_set(desc, (Item){.item = s2it(heap_create_name("configurable"))},
        (Item){.item = b2it(configurable ? 1 : 0)});
    return desc;
}

static bool radiant_dom_projected_own_value(Item object, Item key, Item* out) {
    if (!out || get_type_id(key) != LMD_TYPE_STRING) return false;
    // DOM3 Phase 4: converted members resolve through the record system, so
    // own-keys projection and descriptors keep covering them after their
    // legacy chain arms are deleted
    if (jube_member_projected_get(object, key, out)) return true;
    // Generic DOM reads also expose expandos; treating that fallback as a
    // projected value would make an expando undeletable and non-configurable.
    return false;
}

static bool radiant_dom_has_expando(Item object, Item key) {
    return js_dom_expando_has_property(object, key);
}

static Item radiant_dom_expando_descriptor(Item object, Item key) {
    return js_dom_expando_get_own_property_descriptor(object, key);
}

static Item radiant_dom_delete_expando(Item object, Item key) {
    return js_dom_expando_delete_property(object, key);
}

static Item radiant_dom_expando_names(Item object) {
    return js_dom_expando_own_property_names(object);
}

static bool radiant_dom_array_has_key(Item arr, Item key) {
    if (get_type_id(arr) != LMD_TYPE_ARRAY || !arr.array ||
        get_type_id(key) != LMD_TYPE_STRING) {
        return false;
    }
    String* key_str = it2s(key);
    if (!key_str) return false;
    for (int i = 0; i < arr.array->length; i++) {
        Item existing = arr.array->items[i];
        if (get_type_id(existing) != LMD_TYPE_STRING) continue;
        String* existing_str = it2s(existing);
        if (existing_str && existing_str->len == key_str->len &&
            memcmp(existing_str->chars, key_str->chars, key_str->len) == 0) {
            return true;
        }
    }
    return false;
}

static void radiant_dom_push_projected_key(Item result, Item object, const char* key) {
    (void)object;
    Item key_item = (Item){.item = s2it(heap_create_name(key))};
    // Own-key enumeration must not materialize live DOM collections; value
    // reads remain lazy through get_property/descriptor dispatch.
    radiant_host_api->value->array_push(result, key_item);
}

RADIANT_C_API int radiant_dom_host_get_property(Item object, Item key, Item* out) {
    if (!out) return 0;
    *out = radiant_dom_get_property(object, key);
    return 1;
}

RADIANT_C_API int radiant_dom_host_set_property(Item object, Item key, Item value, Item* out) {
    if (!out) return 0;
    *out = radiant_dom_set_property(object, key, value);
    return 1;
}

RADIANT_C_API int radiant_dom_node_named_get(Item object, Item key, Item* out) {
    if (!out || !radiant_dom_unwrap_node(object)) return 0;
    // Phase 4e migration: DOM open-name lookup must be handled through Jube
    // hooks before the legacy host-ops fallback so record methods keep their
    // method-function identity after selector engines train property IC sites.
    return radiant_dom_host_get_property(object, key, out);
}

RADIANT_C_API int radiant_dom_node_named_set(Item object, Item key, Item value, Item* out) {
    if (!out || !radiant_dom_unwrap_node(object)) return 0;
    return radiant_dom_host_set_property(object, key, value, out);
}

RADIANT_C_API int radiant_dom_node_prototype(Item object, Item* out) {
    if (!out || !radiant_dom_unwrap_node(object)) return 0;
    *out = radiant_dom_host_prototype(object);
    return 1;
}

RADIANT_C_API int radiant_dom_host_has_property(Item object, Item key, Item* out) {
    if (!out) return 0;
    Item projected = ItemNull;
    if (radiant_dom_projected_own_value(object, key, &projected) ||
        radiant_dom_has_expando(object, key)) {
        *out = (Item){.item = b2it(true)};
        return 1;
    }
    Item fallback = radiant_dom_get_property(object, key);
    bool present = fallback.item != ItemNull.item && fallback.item != ITEM_JS_UNDEFINED;
    *out = (Item){.item = b2it(present ? 1 : 0)};
    return 1;
}

RADIANT_C_API int radiant_dom_host_delete_property(Item object, Item key, Item* out) {
    if (!out) return 0;
    Item projected = ItemNull;
    if (radiant_dom_projected_own_value(object, key, &projected)) {
        // Projected host properties are native state, not wrapper slots; the
        // compatibility descriptor marks them non-configurable, so delete fails.
        *out = (Item){.item = b2it(false)};
        return 1;
    }
    if (radiant_dom_has_expando(object, key)) {
        *out = radiant_dom_delete_expando(object, key);
        return 1;
    }
    *out = (Item){.item = b2it(true)};
    return 1;
}

RADIANT_C_API int radiant_dom_host_own_property_descriptor(Item object, Item key, Item* out) {
    if (!out) return 0;
    Item projected = ItemNull;
    if (radiant_dom_projected_own_value(object, key, &projected)) {
        *out = radiant_dom_data_descriptor(projected, true, true, false);
        return 1;
    }
    if (radiant_dom_has_expando(object, key)) {
        *out = radiant_dom_expando_descriptor(object, key);
        return 1;
    }
    *out = radiant_dom_undefined_item();
    return 1;
}

RADIANT_C_API int radiant_dom_host_own_property_names(Item object, Item* out) {
    if (!out) return 0;
    Item result = radiant_host_api->value->array_new(0);
    {
        DomNode* node = (DomNode*)radiant_dom_unwrap_node(object);
        if (node && node->is_element()) {
            static const char* const element_keys[] = {
                "tagName", "nodeName", "localName", "namespaceURI", "prefix",
                "id", "className", "nodeType", "parentNode", "parentElement",
                "isConnected", "childElementCount", "length", "children",
                "attributes", "ownerDocument", "firstChild", "lastChild",
                "nextSibling", "previousSibling", "firstElementChild",
                "lastElementChild", "nextElementSibling", "previousElementSibling",
                "childNodes", nullptr
            };
            for (int i = 0; element_keys[i]; i++) radiant_dom_push_projected_key(result, object, element_keys[i]);
        } else if (node && (node->is_text() || node->is_comment())) {
            static const char* const character_keys[] = {
                "data", "nodeValue", "textContent", "length", "nodeType",
                "nodeName", nullptr
            };
            for (int i = 0; character_keys[i]; i++) radiant_dom_push_projected_key(result, object, character_keys[i]);
        }
    }
    Item expando_names = radiant_dom_expando_names(object);
    if (get_type_id(expando_names) == LMD_TYPE_ARRAY && expando_names.array) {
        for (int i = 0; i < expando_names.array->length; i++) {
            Item key = expando_names.array->items[i];
            if (!radiant_dom_array_has_key(result, key)) radiant_host_api->value->array_push(result, key);
        }
    }
    *out = result;
    return 1;
}

RADIANT_C_API Item radiant_dom_host_prototype(Item object) {
    return js_dom_get_prototype_value(object);
}

RADIANT_C_API void radiant_dom_host_invalidate(Item object) {
    if (get_type_id(object) == LMD_TYPE_VMAP && object.vmap &&
        radiant_dom_is_node_host_type(object.vmap->host_type)) {
        object.vmap->host_data = nullptr;
    }
}



static Item radiant_dom_foreign_get_computed_style(Item elem_item, Item pseudo_item) {
    (void)elem_item; (void)pseudo_item;
    return ItemNull;
}

RADIANT_C_API int radiant_dom_foreign_document_get_property(Item object, Item key, Item* out) {
    if (!out) return 0;
    void* foreign_doc = js_get_foreign_doc(object);
    if (foreign_doc && js_doc_has_browsing_context(foreign_doc)) {
        if (radiant_dom_key_equals(key, "defaultView", 11) ||
            radiant_dom_key_equals(key, "document", 8) ||
            radiant_dom_key_equals(key, "window", 6) ||
            radiant_dom_key_equals(key, "self", 4)) {
            *out = object;
            return 1;
        }
        if (radiant_dom_key_equals(key, "getComputedStyle", 16)) {
            // iframe contentWindow is modeled as a document wrapper; do not let
            // the main-window getComputedStyle binding leak into foreign docs.
            *out = jube_new_function(radiant_host_api->script,
                radiant_dom_foreign_get_computed_style, 2);
            return 1;
        }
    }

    // foreign document proxies use the normal document table with a temporary
    // active-document swap; otherwise reads accidentally target the main doc.
    void* prev = js_dom_swap_active_document(foreign_doc);
    *out = js_document_proxy_get_property(key);
    js_dom_restore_active_document(prev);
    return 1;
}

RADIANT_C_API int radiant_dom_foreign_document_set_property(Item object,
                                                         Item key,
                                                         Item value,
                                                         Item* out) {
    if (!out) return 0;
    void* foreign_doc = js_get_foreign_doc(object);
    // writes share the document proxy setter, but must run under the foreign
    // active document so title/location/defaultView state lands on that proxy.
    void* prev = js_dom_swap_active_document(foreign_doc);
    *out = js_document_proxy_set_property(key, value);
    js_dom_restore_active_document(prev);
    return 1;
}

RADIANT_C_API int radiant_dom_document_host_get_property(Item object, Item key, Item* out) {
    if (!out) return 0;
    if (js_get_foreign_doc(object)) {
        return radiant_dom_foreign_document_get_property(object, key, out);
    }
    if (get_type_id(object) == LMD_TYPE_VMAP && object.vmap && object.vmap->host_data) {
        void* prev = js_dom_swap_active_document(object.vmap->host_data);
        *out = js_document_proxy_get_property(key);
        js_dom_restore_active_document(prev);
        return 1;
    }
    *out = js_document_proxy_get_property(key);
    return 1;
}

RADIANT_C_API int radiant_dom_document_host_set_property(Item object,
                                                      Item key,
                                                      Item value,
                                                      Item* out) {
    if (!out) return 0;
    if (js_get_foreign_doc(object)) {
        return radiant_dom_foreign_document_set_property(object, key, value, out);
    }
    if (get_type_id(object) == LMD_TYPE_VMAP && object.vmap && object.vmap->host_data) {
        void* prev = js_dom_swap_active_document(object.vmap->host_data);
        *out = js_document_proxy_set_property(key, value);
        js_dom_restore_active_document(prev);
        return 1;
    }
    *out = js_document_proxy_set_property(key, value);
    return 1;
}

RADIANT_C_API int radiant_dom_document_host_has_property(Item object, Item key, Item* out) {
    if (!out) return 0;
    Item value = ItemNull;
    if (!radiant_dom_document_host_get_property(object, key, &value)) return 0;
    *out = (Item){.item = b2it(value.item != ItemNull.item && value.item != ITEM_JS_UNDEFINED)};
    return 1;
}

RADIANT_C_API int radiant_dom_document_host_delete_property(Item object, Item key, Item* out) {
    (void)object; (void)key;
    if (!out) return 0;
    *out = (Item){.item = b2it(true)};
    return 1;
}

RADIANT_C_API int radiant_dom_document_host_own_property_descriptor(Item object, Item key, Item* out) {
    (void)object; (void)key;
    if (!out) return 0;
    *out = radiant_dom_undefined_item();
    return 1;
}

RADIANT_C_API int radiant_dom_document_host_own_property_names(Item object, Item* out) {
    (void)object;
    if (!out) return 0;
    *out = radiant_host_api->value->array_new(0);
    return 1;
}

RADIANT_C_API Item radiant_dom_document_host_prototype(Item object) {
    (void)object;
    Item global = radiant_host_api->script->global_this();
    Item ctor = radiant_host_api->value->property_get(
        global, radiant_dom_string_item("Document"));
    if (get_type_id(ctor) == LMD_TYPE_FUNC) {
        Item proto = radiant_host_api->value->property_get(
            ctor, radiant_dom_string_item("prototype"));
        if (get_type_id(proto) == LMD_TYPE_MAP) return proto;
    }
    // Startup can ask for the host prototype before DOM globals are installed.
    Item proto = js_get_intrinsic_prototype_for_class(JS_CLASS_OBJECT);
    return get_type_id(proto) == LMD_TYPE_MAP ? proto : ItemNull;
}

RADIANT_C_API int radiant_dom_document_prototype(Item object, Item* out) {
    if (!out) return 0;
    *out = radiant_dom_document_host_prototype(object);
    return 1;
}

static Item radiant_dom_create_element_item(DomDocument* doc, const char* tag,
                                             const char* namespace_uri) {
    if (!doc || !doc->input || !tag) return ItemNull;
    MarkBuilder builder(doc->input);
    Item elem_item = builder.element(tag).final();
    DomElement* elem = dom_element_create(doc, tag, elem_item.element);
    if (elem && namespace_uri && namespace_uri[0]) {
        elem->set_attribute("__lambda_ns_uri", namespace_uri);
    }
    return radiant_dom_node_item((DomNode*)elem);
}

static Item radiant_dom_create_mark_element_item(DomDocument* doc, const char* backing_tag,
                                                  const char* node_tag, const char* text,
                                                  bool comment, bool add_text) {
    if (!doc || !doc->input) return ItemNull;
    MarkBuilder builder(doc->input);
    Item backing = add_text
        ? builder.element(backing_tag).text(text ? text : "").final()
        : builder.element(backing_tag).final();
    DomNode* node = comment
        ? (DomNode*)dom_comment_create_detached(backing.element, doc)
        : (DomNode*)dom_element_create(doc, node_tag, backing.element);
    return radiant_dom_node_item(node);
}

static int radiant_dom_document_operation_active(RadiantDocumentOperation operation,
                                                   Item* args, int argc, Item* out) {
    if (!out) return 0;

    DomDocument* doc = (DomDocument*)js_dom_get_document();
    DomElement* root = doc ? doc->root : nullptr;

    if (operation == RADIANT_DOCUMENT_ASSIGN ||
        operation == RADIANT_DOCUMENT_REPLACE) {
        *out = argc >= 1
            ? js_dom_location_navigate_bridge((void*)doc, args[0],
                operation == RADIANT_DOCUMENT_REPLACE)
            : radiant_dom_undefined_item();
        return 1;
    }
    if (operation == RADIANT_DOCUMENT_RELOAD) {
        *out = radiant_dom_undefined_item();
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_FOCUS || operation == RADIANT_DOCUMENT_BLUR) {
        *out = radiant_dom_undefined_item();
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_HAS_FOCUS) {
        // A focused descendant means this retained browsing-context document
        // owns keyboard focus. Editor view observers use this predicate while
        // reconciling native contenteditable mutations.
        DocState* state = doc ? doc->state : nullptr;
        *out = (Item){.item = b2it(state && focus_get(state) ? 1 : 0)};
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_OPEN) {
        *out = js_dom_document_open_bridge((void*)doc);
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_CLOSE) {
        *out = radiant_dom_undefined_item();
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_WRITE || operation == RADIANT_DOCUMENT_WRITELN) {
        if (argc < 1) {
            *out = ItemNull;
            return 1;
        }
        *out = js_dom_document_write_bridge((void*)doc, args[0]);
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_ELEMENT_FROM_POINT) {
        Item x_arg = argc >= 1 ? args[0] : radiant_dom_int_item(0);
        Item y_arg = argc >= 2 ? args[1] : radiant_dom_int_item(0);
        *out = js_dom_document_element_from_point_bridge((void*)doc, x_arg, y_arg);
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_EXEC_COMMAND) {
        Item command = argc >= 1 ? args[0] : radiant_dom_undefined_item();
        Item value = argc >= 3 ? args[2] : radiant_dom_string_item("");
        *out = js_dom_document_exec_command_bridge(command, value);
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_CREATE_RANGE) {
        *out = js_dom_create_range();
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_GET_SELECTION) {
        *out = js_doc_has_browsing_context((void*)doc) ? js_dom_get_selection() : ItemNull;
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_CONTAINS) {
        if (argc < 1 || !doc) {
            *out = (Item){.item = b2it(0)};
            return 1;
        }
        Item document_item = js_dom_document_proxy_for_doc_bridge((void*)doc);
        DomNode* other = (DomNode*)js_dom_unwrap_element_impl(args[0]);
        // Document inherits Node; attachment checks used by jQuery must include
        // the documentElement itself and every descendant in the live tree.
        bool contained = args[0].item == document_item.item ||
            (root && other && radiant_dom_node_contains((DomNode*)root, other));
        *out = (Item){.item = b2it(contained ? 1 : 0)};
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_COMPARE_DOCUMENT_POSITION) {
        // The document is an ancestor of every attached node; keep the legacy
        // Node bitmask while making the callable target independent of its name.
        *out = radiant_dom_int_item(20);
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_GET_ROOT_NODE) {
        *out = doc ? js_dom_document_proxy_for_doc_bridge((void*)doc) : ItemNull;
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_GET_ELEMENT_BY_ID) {
        if (argc < 1) {
            *out = ItemNull;
            return 1;
        }
        const char* id = fn_to_cstr(args[0]);
        if (!id) {
            *out = ItemNull;
            return 1;
        }
        // handled flag preserves valid null lookup results without JS fallback.
        *out = radiant_dom_node_item((DomNode*)radiant_dom_find_by_id(root, id));
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_GET_ELEMENTS_BY_CLASS_NAME) {
        if (argc < 1) {
            *out = ItemNull;
            return 1;
        }
        *out = js_dom_live_document_get_elements_by_class_name_bridge((void*)doc, args[0]);
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_GET_ELEMENTS_BY_TAG_NAME) {
        if (argc < 1) {
            *out = ItemNull;
            return 1;
        }
        *out = js_dom_live_document_get_elements_by_tag_name_bridge((void*)doc, args[0]);
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_GET_ELEMENTS_BY_NAME) {
        if (argc < 1) {
            *out = ItemNull;
            return 1;
        }
        *out = js_dom_live_document_get_elements_by_name_bridge((void*)doc, args[0]);
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_QUERY_SELECTOR) {
        if (argc < 1) {
            *out = ItemNull;
            return 1;
        }
        const char* sel_text = radiant_dom_to_dom_string_cstr(args[0]);
        if (!sel_text || !doc || !doc->document_pool) {
            *out = ItemNull;
            return 1;
        }
        CssSelectorGroup* selector_group = radiant_dom_parse_css_selector_group(sel_text, doc->document_pool);
        if (!selector_group) {
            Item err_name = (Item){.item = s2it(heap_create_name("SyntaxError"))};
            Item err_msg = (Item){.item = s2it(heap_create_name("is not a valid selector"))};
            radiant_host_api->script->throw_value(
                radiant_host_api->script->new_error_with_name(err_name, err_msg));
            *out = ItemNull;
            return 1;
        }
        SelectorMatcher* matcher = (SelectorMatcher*)js_dom_create_selector_matcher_bridge((void*)doc);
        DomElement* found = radiant_dom_selector_group_find_first(
            matcher, selector_group, root, true);
        *out = radiant_dom_node_item((DomNode*)found);
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_QUERY_SELECTOR_ALL) {
        if (argc < 1) {
            *out = ItemNull;
            return 1;
        }
        const char* sel_text = radiant_dom_to_dom_string_cstr(args[0]);
        if (!sel_text || !doc || !doc->document_pool) {
            *out = radiant_dom_array_item();
            return 1;
        }
        CssSelectorGroup* selector_group = radiant_dom_parse_css_selector_group(sel_text, doc->document_pool);
        if (!selector_group) {
            *out = radiant_dom_array_item();
            return 1;
        }
        SelectorMatcher* matcher = (SelectorMatcher*)js_dom_create_selector_matcher_bridge((void*)doc);
        ArrayList* results = arraylist_new(16);
        Item arr_item = radiant_dom_array_item();
        Array* arr = arr_item.array;
        if (results) {
            radiant_dom_selector_group_collect_all(
                matcher, selector_group, root, results, true);
            for (int i = 0; i < results->length; i++) {
                array_push(arr, radiant_dom_node_item((DomNode*)results->data[i]));
            }
            arraylist_free(results);
        }
        *out = arr_item;
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_CREATE_ELEMENT) {
        *out = argc >= 1 ? radiant_dom_create_element_item(
            doc, fn_to_cstr(args[0]), nullptr) : ItemNull;
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_CREATE_ELEMENT_NS) {
        *out = argc >= 2 ? radiant_dom_create_element_item(
            doc, fn_to_cstr(args[1]), fn_to_cstr(args[0])) : ItemNull;
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_CREATE_TEXT_NODE) {
        const char* text = argc >= 1 ? fn_to_cstr(args[0]) : nullptr;
        DomText* text_node = (doc && text)
            ? DomText::create_detached_copy(doc, text, strlen(text)) : nullptr;
        *out = radiant_dom_node_item((DomNode*)text_node);
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_CREATE_DOCUMENT_FRAGMENT) {
        *out = radiant_dom_create_mark_element_item(doc, "#document-fragment",
            "#document-fragment", nullptr, false, false);
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_CREATE_COMMENT) {
        const char* text = (argc >= 1) ? fn_to_cstr(args[0]) : "";
        *out = radiant_dom_create_mark_element_item(doc, "!--", "!--", text, true, true);
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_CREATE_PROCESSING_INSTRUCTION) {
        if (!doc || !doc->input) {
            *out = ItemNull;
            return 1;
        }
        const char* target = (argc >= 1) ? fn_to_cstr(args[0]) : "";
        const char* data = (argc >= 2) ? fn_to_cstr(args[1]) : "";
        *out = radiant_dom_create_mark_element_item(doc, "?", target ? target : "",
            data, false, true);
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_IMPORT_NODE) {
        if (argc < 1) {
            *out = ItemNull;
            return 1;
        }
        DomNode* source = (DomNode*)js_dom_unwrap_element_impl(args[0]);
        if (!source || !source->is_element()) {
            *out = ItemNull;
            return 1;
        }
        Item source_item = radiant_dom_node_item(source);
        Item deep_arg = (Item){.item = b2it((argc >= 2 && js_is_truthy(args[1])) ? 1 : 0)};
        *out = radiant_dom_element_operation(source_item, JUBE_DOM_CLONE_NODE,
            &deep_arg, 1);
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_NORMALIZE) {
        if (root) {
            Item root_item = radiant_dom_node_item((DomNode*)root);
            *out = radiant_dom_element_operation(root_item, JUBE_DOM_NORMALIZE,
                nullptr, 0);
        } else {
            *out = ItemNull;
        }
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_ADOPT_NODE) {
        if (argc < 1) {
            *out = ItemNull;
            return 1;
        }
        *out = js_dom_adopt_node_bridge(args[0]);
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_APPEND_CHILD) {
        if (argc < 1 || !doc) {
            *out = ItemNull;
            return 1;
        }
        DomNode* child = (DomNode*)js_dom_unwrap_element_impl(args[0]);
        if (!child) {
            *out = ItemNull;
            return 1;
        }
        if (!doc->root && child->is_element()) {
            if (child->parent) {
                // document root bootstrap must detach through adoptNode bookkeeping before re-rooting.
                js_dom_adopt_node_bridge(args[0]);
            }
            doc->root = child->as_element();
            *out = args[0];
            return 1;
        }
        if (doc->root) {
            *out = js_dom_append_child_bridge((void*)doc->root, args[0]);
            return 1;
        }
        *out = args[0];
        return 1;
    }

    Item doc_item = js_get_document_object_value();
    if (operation == RADIANT_DOCUMENT_ADD_EVENT_LISTENER) {
        // document EventTarget storage is keyed by the singleton document wrapper.
        *out = argc >= 2
            ? js_dom_add_event_listener_bridge(doc_item, args[0], args[1],
                argc >= 3 ? args[2] : ItemNull)
            : radiant_dom_undefined_item();
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_REMOVE_EVENT_LISTENER) {
        // document EventTarget storage is keyed by the singleton document wrapper.
        *out = argc >= 2
            ? js_dom_remove_event_listener_bridge(doc_item, args[0], args[1],
                argc >= 3 ? args[2] : ItemNull)
            : radiant_dom_undefined_item();
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_DISPATCH_EVENT) {
        // dispatch must use the same wrapper identity listeners were registered with.
        *out = argc >= 1
            ? js_dom_dispatch_event_bridge(doc_item, args[0])
            : (Item){.item = b2it(0)};
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_CREATE_TREE_WALKER) {
        *out = argc >= 2
            ? js_dom_create_tree_walker_bridge(args[0], args[1])
            : ItemNull;
        return 1;
    }

    if (operation == RADIANT_DOCUMENT_CREATE_EVENT) {
        Item interface_name = argc >= 1 ? args[0] : radiant_dom_undefined_item();
        *out = js_dom_document_create_event_bridge(interface_name);
        return 1;
    }

    return 0;
}

RADIANT_C_API int radiant_dom_document_operation(Item object,
                                                  RadiantDocumentOperation operation,
                                                  Item* args, int argc, Item* out) {
    void* target_doc = js_get_foreign_doc(object);
    if (!target_doc && get_type_id(object) == LMD_TYPE_VMAP && object.vmap) {
        target_doc = object.vmap->host_data;
    }
    void* previous_doc = target_doc ? js_dom_swap_active_document(target_doc) : nullptr;
    int handled = radiant_dom_document_operation_active(operation, args, argc, out);
    if (target_doc) js_dom_restore_active_document(previous_doc);
    return handled;
}

RADIANT_C_API Item radiant_dom_window_add_event_listener(Item type, Item callback, Item opts) {
    // window EventTarget storage must key on the canonical global object.
    return js_dom_add_event_listener_bridge(radiant_host_api->script->global_this(), type, callback, opts);
}

RADIANT_C_API Item radiant_dom_window_remove_event_listener(Item type, Item callback, Item opts) {
    // window EventTarget storage must key on the canonical global object.
    return js_dom_remove_event_listener_bridge(radiant_host_api->script->global_this(), type, callback, opts);
}

RADIANT_C_API Item radiant_dom_window_dispatch_event(Item event_item) {
    // dispatch must use the same global-object key that listener registration uses.
    return js_dom_dispatch_event_bridge(radiant_host_api->script->global_this(), event_item);
}

RADIANT_C_API bool radiant_dom_has_committed_geometry_snapshot(DomDocument* doc) {
    return doc && js_dom_has_committed_geometry_snapshot &&
        js_dom_has_committed_geometry_snapshot((void*)doc);
}

static Item radiant_dom_window_dimension(float value) {
    return (Item){.item = i2it((int64_t)llroundf(value))};
}

RADIANT_C_API int radiant_dom_window_get_property(Item object, Item key, Item* out) {
    // Ordinary JS property reads probe this optional hook before any Radiant
    // value exists, so lazy module registration must leave the bridge inert.
    if (!out || !radiant_host_api || !radiant_host_api->script ||
        !radiant_host_api->script->global_this ||
        object.item != radiant_host_api->script->global_this().item ||
        !js_dom_get_ui_context) {
        return 0;
    }
    UiContext* uicon = (UiContext*)js_dom_get_ui_context();
    if (!uicon) return 0;

    if (radiant_dom_key_equals(key, "innerWidth", 10)) {
        *out = radiant_dom_window_dimension(uicon->viewport_width);
        return 1;
    }
    if (radiant_dom_key_equals(key, "innerHeight", 11)) {
        *out = radiant_dom_window_dimension(uicon->viewport_height);
        return 1;
    }
    if (radiant_dom_key_equals(key, "outerWidth", 10)) {
        *out = radiant_dom_window_dimension(uicon->window_width);
        return 1;
    }
    if (radiant_dom_key_equals(key, "outerHeight", 11)) {
        *out = radiant_dom_window_dimension(uicon->window_height);
        return 1;
    }
    if (radiant_dom_key_equals(key, "devicePixelRatio", 16)) {
        *out = radiant_dom_window_dimension(uicon->pixel_ratio > 0.0f ? uicon->pixel_ratio : 1.0f);
        return 1;
    }

    DomDocument* doc = uicon->document;
    float scroll_x = doc && doc->state ? doc->state->scroll_x
        : (doc ? doc->pending_viewport_scroll_x : 0.0f);
    float scroll_y = doc && doc->state ? doc->state->scroll_y
        : (doc ? doc->pending_viewport_scroll_y : 0.0f);
    if (radiant_dom_key_equals(key, "scrollX", 7) ||
        radiant_dom_key_equals(key, "pageXOffset", 11)) {
        *out = radiant_dom_window_dimension(scroll_x);
        return 1;
    }
    if (radiant_dom_key_equals(key, "scrollY", 7) ||
        radiant_dom_key_equals(key, "pageYOffset", 11)) {
        *out = radiant_dom_window_dimension(scroll_y);
        return 1;
    }
    if (radiant_dom_key_equals(key, "screen", 6)) {
        // The active surface is the only screen available to embedded/headless
        // Radiant; deriving this object here keeps it synchronized with resize.
        Item screen = radiant_host_api->value->new_object();
        Item width = radiant_dom_window_dimension(uicon->window_width);
        Item height = radiant_dom_window_dimension(uicon->window_height);
        Item depth = (Item){.item = i2it(24)};
        radiant_host_api->value->property_set(screen, (Item){.item = s2it(heap_create_name("width"))}, width);
        radiant_host_api->value->property_set(screen, (Item){.item = s2it(heap_create_name("height"))}, height);
        radiant_host_api->value->property_set(screen, (Item){.item = s2it(heap_create_name("availWidth"))}, width);
        radiant_host_api->value->property_set(screen, (Item){.item = s2it(heap_create_name("availHeight"))}, height);
        radiant_host_api->value->property_set(screen, (Item){.item = s2it(heap_create_name("colorDepth"))}, depth);
        radiant_host_api->value->property_set(screen, (Item){.item = s2it(heap_create_name("pixelDepth"))}, depth);
        *out = screen;
        return 1;
    }
    return 0;
}
