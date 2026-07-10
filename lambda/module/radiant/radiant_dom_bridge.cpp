#include "../../lambda-data.hpp"
#include "../../mark_builder.hpp"
#include "../../input/css/dom_node.hpp"
#include "../../input/css/dom_element.hpp"
#include "../../input/css/css_tokenizer.hpp"
#include "../../input/css/selector_matcher.hpp"
#include "../../../radiant/form_control.hpp"
#include "../../../radiant/text_control.hpp"
#include "../../../lib/log.h"
#include "../../../lib/mem.h"
#include "../../../lib/str.h"
#include "../../../lib/strbuf.h"
#include "../../../lib/url.h"
#include <assert.h>
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void heap_register_gc_root(uint64_t* slot);
extern "C" void heap_unregister_gc_root(uint64_t* slot);

extern "C" void* js_dom_get_document(void);
extern "C" Item js_get_document_object_value(void);
extern "C" Item js_get_global_this(void);
extern "C" Item js_get_global_property(Item key);
extern "C" Item js_new_function(void* func_ptr, int param_count);
extern "C" void* js_dom_get_or_create_doc_node(void* doc);
extern "C" Item js_dom_document_proxy_for_doc_bridge(void* doc);
extern "C" void* js_dom_unwrap_element_impl(Item item);
extern "C" void js_dom_initialize_node_wrapper(void* dom_elem);
extern "C" Item vmap_new(void);
extern "C" Item js_new_error_with_name(Item error_name, Item message);
extern "C" void js_throw_value(Item error);
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
extern "C" const void* radiant_dom_node_host_type(void);
extern "C" void radiant_dom_host_invalidate(Item object);
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
extern "C" Item js_array_push(Item array, Item value);
extern "C" Item js_css_namespace_method(Item obj, Item method_name, Item* args, int argc);
extern "C" Item js_cssom_stylesheet_method(Item sheet_item, Item method_name, Item* args, int argc);
extern "C" Item js_cssom_rule_decl_method(Item decl_item, Item method_name, Item* args, int argc);
extern "C" Item js_dom_owner_document_for_node(void* node);
extern "C" const char* js_dom_to_attribute_cstr(Item value);
extern "C" bool js_is_truthy(Item value);
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
extern "C" Item js_dom_text_control_set_range_text_bridge(void* elem, Item replacement, Item start, Item end, Item mode);
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
extern "C" Item js_dom_style_set_property_bridge(void* elem, Item prop, Item value, Item priority, bool has_priority);
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
extern "C" Item radiant_dom_wrap_node(void* dom_elem);
extern "C" Item radiant_dom_get_property(Item elem_item, Item prop_name);
extern "C" Item js_new_object(void);
extern "C" Item js_array_new(int64_t capacity);
extern "C" Item js_property_get(Item object, Item key);
extern "C" Item js_property_set(Item object, Item key, Item value);
extern "C" Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
extern "C" int js_check_exception(void);

static const int RADIANT_DOM_WRAPPER_CACHE_CHUNK_SIZE = 4096;
static const char s_radiant_dom_vmap_type_marker = 0;

struct RadiantDomWrapperCacheEntry {
    DomNode* node;
    DomDocument* owner_doc;
    uint64_t item;
};

struct RadiantDomWrapperCacheChunk {
    RadiantDomWrapperCacheEntry entries[RADIANT_DOM_WRAPPER_CACHE_CHUNK_SIZE];
    int count;
    RadiantDomWrapperCacheChunk* next;
};

static __thread RadiantDomWrapperCacheChunk* s_radiant_dom_wrapper_cache_head = nullptr;
static __thread RadiantDomWrapperCacheChunk* s_radiant_dom_wrapper_cache_tail = nullptr;
static __thread bool s_radiant_dom_cache_owner_set = false;
static __thread pthread_t s_radiant_dom_cache_owner;

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
        host_type == radiant_dom_node_host_type();
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

static String* radiant_dom_document_string(DomDocument* doc, const char* str, size_t len) {
    if (!doc || !doc->arena || !str) return nullptr;
    String* s = (String*)arena_alloc(doc->arena, sizeof(String) + len + 1);
    if (!s) return nullptr;
    s->len = (uint32_t)len;
    s->is_ascii = str_is_ascii(str, len) ? 1 : 0;
    if (len > 0) memcpy(s->chars, str, len);
    s->chars[len] = '\0';
    return s;
}

static Item radiant_dom_node_item(DomNode* node) {
    return node ? radiant_dom_wrap_node((void*)node) : ItemNull;
}

static Item radiant_dom_empty_node_list() {
    Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    arr->type_id = LMD_TYPE_ARRAY;
    arr->items = nullptr;
    arr->length = 0;
    arr->capacity = 0;
    return (Item){.array = arr};
}

static Item radiant_dom_empty_array_item() {
    Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    arr->type_id = LMD_TYPE_ARRAY;
    arr->items = nullptr;
    arr->length = 0;
    arr->capacity = 0;
    return (Item){.array = arr};
}

static Item radiant_dom_array_item() {
    Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    arr->type_id = LMD_TYPE_ARRAY;
    arr->items = nullptr;
    arr->length = 0;
    arr->capacity = 0;
    return (Item){.array = arr};
}

static bool radiant_dom_is_internal_attr(const char* name) {
    return name && strncmp(name, "__lambda_", 9) == 0;
}

static Item radiant_dom_class_name_item(DomElement* elem) {
    if (!elem || elem->class_count == 0) return radiant_dom_string_item("");
    StrBuf* sb = strbuf_new_cap(64);
    if (!sb) return radiant_dom_string_item("");
    for (int i = 0; i < elem->class_count; i++) {
        if (i > 0) strbuf_append_char(sb, ' ');
        strbuf_append_str(sb, elem->class_names[i]);
    }
    Item result = radiant_dom_string_item(sb->str ? sb->str : "");
    strbuf_free(sb);
    return result;
}

static bool radiant_dom_is_tag(DomElement* elem, const char* tag) {
    return elem && elem->tag_name && tag && strcasecmp(elem->tag_name, tag) == 0;
}

static bool radiant_dom_bool_reflected(DomElement* elem, const char* prop, const char** attr_name) {
    if (!elem || !prop || !attr_name) return false;
    bool input = radiant_dom_is_tag(elem, "input");
    bool button = radiant_dom_is_tag(elem, "button");
    bool select = radiant_dom_is_tag(elem, "select");
    bool textarea = radiant_dom_is_tag(elem, "textarea");
    bool form = radiant_dom_is_tag(elem, "form");
    bool details = radiant_dom_is_tag(elem, "details");
    bool fieldset = radiant_dom_is_tag(elem, "fieldset");
    bool option = radiant_dom_is_tag(elem, "option");
    bool optgroup = radiant_dom_is_tag(elem, "optgroup");
    if (strcmp(prop, "disabled") == 0 &&
        (input || button || select || textarea || fieldset || option || optgroup)) {
        *attr_name = "disabled";
        return true;
    }
    if (strcmp(prop, "required") == 0 && (input || select || textarea)) {
        *attr_name = "required";
        return true;
    }
    if (strcmp(prop, "multiple") == 0 && (input || select)) {
        *attr_name = "multiple";
        return true;
    }
    if ((strcmp(prop, "readOnly") == 0 || strcmp(prop, "readonly") == 0) &&
        (input || textarea)) {
        *attr_name = "readonly";
        return true;
    }
    if (strcmp(prop, "noValidate") == 0 && form) {
        *attr_name = "novalidate";
        return true;
    }
    if (strcmp(prop, "formNoValidate") == 0 && (input || button)) {
        *attr_name = "formnovalidate";
        return true;
    }
    if (strcmp(prop, "open") == 0 && details) {
        *attr_name = "open";
        return true;
    }
    if (strcmp(prop, "defaultChecked") == 0 && input) {
        *attr_name = "checked";
        return true;
    }
    if (strcmp(prop, "defaultSelected") == 0 && option) {
        *attr_name = "selected";
        return true;
    }
    if (strcmp(prop, "autofocus") == 0) {
        *attr_name = "autofocus";
        return true;
    }
    return false;
}

static bool radiant_dom_simple_bool_setter(DomElement* elem, const char* prop, const char** attr_name) {
    if (!elem || !prop || !attr_name) return false;
    bool input = radiant_dom_is_tag(elem, "input");
    bool button = radiant_dom_is_tag(elem, "button");
    bool select = radiant_dom_is_tag(elem, "select");
    bool textarea = radiant_dom_is_tag(elem, "textarea");
    bool form = radiant_dom_is_tag(elem, "form");
    bool details = radiant_dom_is_tag(elem, "details");
    if (strcmp(prop, "required") == 0 && (input || select || textarea)) {
        *attr_name = "required";
        return true;
    }
    if (strcmp(prop, "multiple") == 0 && input) {
        *attr_name = "multiple";
        return true;
    }
    if ((strcmp(prop, "readOnly") == 0 || strcmp(prop, "readonly") == 0) &&
        (input || textarea)) {
        *attr_name = "readonly";
        return true;
    }
    if (strcmp(prop, "noValidate") == 0 && form) {
        *attr_name = "novalidate";
        return true;
    }
    if (strcmp(prop, "formNoValidate") == 0 && (input || button)) {
        *attr_name = "formnovalidate";
        return true;
    }
    if (strcmp(prop, "open") == 0 && details) {
        *attr_name = "open";
        return true;
    }
    if (strcmp(prop, "autofocus") == 0) {
        *attr_name = "autofocus";
        return true;
    }
    return false;
}

static bool radiant_dom_disabled_setter(DomElement* elem, const char* prop, const char** attr_name) {
    if (!elem || !prop || !attr_name || strcmp(prop, "disabled") != 0) return false;
    bool input = radiant_dom_is_tag(elem, "input");
    bool button = radiant_dom_is_tag(elem, "button");
    bool select = radiant_dom_is_tag(elem, "select");
    bool textarea = radiant_dom_is_tag(elem, "textarea");
    bool fieldset = radiant_dom_is_tag(elem, "fieldset");
    bool option = radiant_dom_is_tag(elem, "option");
    bool optgroup = radiant_dom_is_tag(elem, "optgroup");
    if (!(input || button || select || textarea || fieldset || option || optgroup)) return false;
    *attr_name = "disabled";
    return true;
}

static bool radiant_dom_live_bool_setter(DomElement* elem, const char* prop, const char** attr_name) {
    if (!elem || !prop || !attr_name) return false;
    if (strcmp(prop, "multiple") == 0 && radiant_dom_is_tag(elem, "select")) {
        *attr_name = "multiple";
        return true;
    }
    if (strcmp(prop, "defaultChecked") == 0 && radiant_dom_is_tag(elem, "input")) {
        *attr_name = "checked";
        return true;
    }
    if (strcmp(prop, "defaultSelected") == 0 && radiant_dom_is_tag(elem, "option")) {
        *attr_name = "selected";
        return true;
    }
    return false;
}

static const char* radiant_dom_item_to_html_bool_string(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_STRING || type == LMD_TYPE_SYMBOL) {
        const char* text = js_dom_to_attribute_cstr(value);
        return text ? text : "";
    }
    return js_is_truthy(value) ? "true" : "false";
}

static bool radiant_dom_int_reflected(DomElement* elem, const char* prop,
                                      const char** attr_name, int* default_value) {
    if (!elem || !prop || !attr_name || !default_value) return false;
    bool input = radiant_dom_is_tag(elem, "input");
    bool textarea = radiant_dom_is_tag(elem, "textarea");
    bool select = radiant_dom_is_tag(elem, "select");
    if (strcmp(prop, "maxLength") == 0 && (input || textarea)) {
        *attr_name = "maxlength"; *default_value = -1; return true;
    }
    if (strcmp(prop, "minLength") == 0 && (input || textarea)) {
        *attr_name = "minlength"; *default_value = 0; return true;
    }
    if (strcmp(prop, "size") == 0 && input) {
        *attr_name = "size"; *default_value = 20; return true;
    }
    if (strcmp(prop, "width") == 0 && input) {
        *attr_name = "width"; *default_value = 0; return true;
    }
    if (strcmp(prop, "height") == 0 && input) {
        *attr_name = "height"; *default_value = 0; return true;
    }
    if (strcmp(prop, "size") == 0 && select) {
        *attr_name = "size"; *default_value = 0; return true;
    }
    if (strcmp(prop, "rows") == 0 && textarea) {
        *attr_name = "rows"; *default_value = 2; return true;
    }
    if (strcmp(prop, "cols") == 0 && textarea) {
        *attr_name = "cols"; *default_value = 20; return true;
    }
    return false;
}

static int64_t radiant_dom_reflected_int_value(DomElement* elem, const char* attr_name,
                                               int default_value) {
    const char* raw = dom_element_get_attribute(elem, attr_name);
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

static bool radiant_dom_string_reflected(DomElement* elem, const char* prop, const char** attr_name) {
    if (!elem || !prop || !attr_name) return false;
    if (strcmp(prop, "src") == 0 &&
        (radiant_dom_is_tag(elem, "img") || radiant_dom_is_tag(elem, "script") ||
         radiant_dom_is_tag(elem, "iframe") || radiant_dom_is_tag(elem, "embed") ||
         radiant_dom_is_tag(elem, "source") || radiant_dom_is_tag(elem, "track") ||
         radiant_dom_is_tag(elem, "audio") || radiant_dom_is_tag(elem, "video") ||
         radiant_dom_is_tag(elem, "input"))) {
        *attr_name = "src";
        return true;
    }
    if (strcmp(prop, "href") == 0 &&
        (radiant_dom_is_tag(elem, "a") || radiant_dom_is_tag(elem, "area") ||
         radiant_dom_is_tag(elem, "link") || radiant_dom_is_tag(elem, "base"))) {
        *attr_name = "href";
        return true;
    }
    if (strcmp(prop, "alt") == 0 && radiant_dom_is_tag(elem, "img")) {
        *attr_name = "alt";
        return true;
    }
    if (strcmp(prop, "name") == 0 &&
        (radiant_dom_is_tag(elem, "input") || radiant_dom_is_tag(elem, "button") ||
         radiant_dom_is_tag(elem, "select") || radiant_dom_is_tag(elem, "textarea") ||
         radiant_dom_is_tag(elem, "form") || radiant_dom_is_tag(elem, "fieldset") ||
         radiant_dom_is_tag(elem, "output") || radiant_dom_is_tag(elem, "object"))) {
        *attr_name = "name";
        return true;
    }
    if (strcmp(prop, "placeholder") == 0 &&
        (radiant_dom_is_tag(elem, "input") || radiant_dom_is_tag(elem, "textarea"))) {
        *attr_name = "placeholder";
        return true;
    }
    if (strcmp(prop, "autocomplete") == 0 &&
        (radiant_dom_is_tag(elem, "form") || radiant_dom_is_tag(elem, "input") ||
         radiant_dom_is_tag(elem, "select") || radiant_dom_is_tag(elem, "textarea"))) {
        *attr_name = "autocomplete";
        return true;
    }
    if (radiant_dom_is_tag(elem, "input") &&
        (strcmp(prop, "pattern") == 0 || strcmp(prop, "min") == 0 ||
         strcmp(prop, "max") == 0 || strcmp(prop, "step") == 0 ||
         strcmp(prop, "accept") == 0)) {
        *attr_name = prop;
        return true;
    }
    if (strcmp(prop, "htmlFor") == 0 &&
        (radiant_dom_is_tag(elem, "label") || radiant_dom_is_tag(elem, "output"))) {
        *attr_name = "for";
        return true;
    }
    if (strcmp(prop, "target") == 0 && radiant_dom_is_tag(elem, "form")) {
        *attr_name = "target";
        return true;
    }
    if (strcmp(prop, "acceptCharset") == 0 && radiant_dom_is_tag(elem, "form")) {
        *attr_name = "accept-charset";
        return true;
    }
    if (strcmp(prop, "formTarget") == 0 &&
        (radiant_dom_is_tag(elem, "input") || radiant_dom_is_tag(elem, "button"))) {
        *attr_name = "formtarget";
        return true;
    }
    if (strcmp(prop, "wrap") == 0 && radiant_dom_is_tag(elem, "textarea")) {
        *attr_name = "wrap";
        return true;
    }
    return false;
}

static const char* radiant_dom_canonical_token_attr(DomElement* elem, const char* attr_name,
                                                    const char* const* keywords) {
    const char* value = dom_element_get_attribute(elem, attr_name);
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
            if (dom_element_has_attribute(current, "contenteditable")) {
                const char* normalized = radiant_dom_normalize_contenteditable(
                    dom_element_get_attribute(current, "contenteditable"));
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

static bool radiant_dom_hint_reflected(const char* prop, const char** attr_name) {
    if (strcmp(prop, "inputMode") == 0) {
        *attr_name = "inputmode";
        return true;
    }
    if (strcmp(prop, "enterKeyHint") == 0) {
        *attr_name = "enterkeyhint";
        return true;
    }
    return false;
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
    Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    arr->type_id = LMD_TYPE_ARRAY;
    arr->items = nullptr;
    arr->length = 0;
    arr->capacity = 0;
    Item arr_item = (Item){.array = arr};

    int attr_count = 0;
    const char** attr_names = dom_element_get_attribute_names(elem, &attr_count);
    for (int i = 0; attr_names && i < attr_count; i++) {
        const char* name = attr_names[i];
        if (radiant_dom_is_internal_attr(name)) continue;
        const char* value = dom_element_get_attribute(elem, name);
        Item pair = js_new_object();
        js_property_set(pair,
            (Item){.item = s2it(heap_create_name("name"))},
            radiant_dom_string_item(name));
        js_property_set(pair,
            (Item){.item = s2it(heap_create_name("value"))},
            radiant_dom_string_item(value));
        array_push(arr, pair);
    }
    return arr_item;
}

static bool radiant_dom_is_listed_form_control(DomElement* elem) {
    if (!elem || !elem->tag_name) return false;
    if (radiant_dom_is_tag(elem, "input")) {
        const char* type = dom_element_get_attribute(elem, "type");
        return !type || strcasecmp(type, "image") != 0;
    }
    return radiant_dom_is_tag(elem, "button") ||
           radiant_dom_is_tag(elem, "select") ||
           radiant_dom_is_tag(elem, "textarea") ||
           radiant_dom_is_tag(elem, "fieldset") ||
           radiant_dom_is_tag(elem, "object") ||
           radiant_dom_is_tag(elem, "output");
}

static void radiant_dom_collect_form_controls(DomNode* node, Array* arr) {
    while (node) {
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            if (radiant_dom_is_listed_form_control(elem)) {
                array_push(arr, radiant_dom_node_item((DomNode*)elem));
            }
            radiant_dom_collect_form_controls(elem->first_child, arr);
        }
        node = node->next_sibling;
    }
}

static Item radiant_dom_form_controls_item(DomElement* form) {
    Item controls = radiant_dom_array_item();
    if (form) {
        radiant_dom_collect_form_controls(form->first_child, controls.array);
    }
    return controls;
}

static const char* radiant_dom_form_control_name_or_id(DomElement* elem) {
    const char* name = dom_element_get_attribute(elem, "name");
    if (name && name[0] != '\0') return name;
    return elem && elem->id && elem->id[0] != '\0' ? elem->id : nullptr;
}

static bool radiant_dom_is_form_named_getter_reserved(const char* prop) {
    static const char* reserved[] = {
        "elements", "length", "action", "method", "enctype", "encoding",
        "acceptCharset", "target", "noValidate", "autocomplete", "name",
        "submit", "reset", "checkValidity", "reportValidity", "requestSubmit",
        nullptr
    };
    for (int i = 0; reserved[i]; i++) {
        if (strcmp(prop, reserved[i]) == 0) return true;
    }
    return false;
}

static void radiant_dom_collect_form_named(DomNode* node, const char* key, Array* arr) {
    while (node) {
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            if (radiant_dom_is_listed_form_control(elem)) {
                const char* control_key = radiant_dom_form_control_name_or_id(elem);
                if (control_key && strcmp(control_key, key) == 0) {
                    array_push(arr, radiant_dom_node_item((DomNode*)elem));
                }
            }
            radiant_dom_collect_form_named(elem->first_child, key, arr);
        }
        node = node->next_sibling;
    }
}

static bool radiant_dom_form_named_getter(DomElement* form, const char* prop, Item* out) {
    if (!radiant_dom_is_tag(form, "form") || !prop || prop[0] == '\0' ||
        !out || radiant_dom_is_form_named_getter_reserved(prop)) {
        return false;
    }
    Item matches = radiant_dom_array_item();
    radiant_dom_collect_form_named(form->first_child, prop, matches.array);
    if (matches.array->length == 1) {
        *out = matches.array->items[0];
        return true;
    }
    if (matches.array->length > 1) {
        *out = matches;
        return true;
    }
    return false;
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
                                                         DomElement* elem) {
    if (!matcher || !group || !elem) return nullptr;
    if (selector_matcher_matches_group(matcher, group, elem, nullptr)) return elem;

    DomNode* child = elem->first_child;
    while (child) {
        if (child->is_element()) {
            DomElement* found = radiant_dom_selector_group_find_first(matcher, group, child->as_element());
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
                                                   ArrayList* results) {
    if (!matcher || !group || !elem || !results) return;
    if (selector_matcher_matches_group(matcher, group, elem, nullptr) &&
            !radiant_dom_selector_group_result_contains(results, elem)) {
        arraylist_append(results, elem);
    }

    DomNode* child = elem->first_child;
    while (child) {
        if (child->is_element()) {
            radiant_dom_selector_group_collect_all(matcher, group, child->as_element(), results);
        }
        child = child->next_sibling;
    }
}

static Item radiant_dom_lookup_wrapper(DomNode* node) {
    radiant_dom_cache_check_owner("lookup_wrapper");
    for (RadiantDomWrapperCacheChunk* chunk = s_radiant_dom_wrapper_cache_head; chunk; chunk = chunk->next) {
        for (int i = 0; i < chunk->count; i++) {
            if (chunk->entries[i].node == node) {
                return (Item){.item = chunk->entries[i].item};
            }
        }
    }
    return ItemNull;
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

static void radiant_dom_cache_wrapper(DomNode* node, Item wrapper) {
    radiant_dom_cache_check_owner("cache_wrapper");
    if (!node || wrapper.item == ITEM_NULL) return;
    RadiantDomWrapperCacheChunk* chunk = s_radiant_dom_wrapper_cache_tail;
    if (!chunk || chunk->count >= RADIANT_DOM_WRAPPER_CACHE_CHUNK_SIZE) {
        chunk = radiant_dom_alloc_wrapper_cache_chunk();
        if (!chunk) return;
    }
    int index = chunk->count++;
    chunk->entries[index].node = node;
    chunk->entries[index].owner_doc = radiant_dom_node_document(node, true);
    chunk->entries[index].item = wrapper.item;
    heap_register_gc_root(&chunk->entries[index].item);
}

static void radiant_dom_clear_cache_entry(RadiantDomWrapperCacheEntry* entry) {
    radiant_dom_cache_check_owner("clear_cache_entry");
    if (!entry || entry->item == 0) return;
    DomNode* node = entry->node;
    if (node && node->is_element()) {
        form_control_release_prop(node->as_element());
    }
    Item wrapper = (Item){.item = entry->item};
    // Document teardown frees arena-owned DOM nodes; retained wrappers must
    // keep their JS identity but lose the native payload before roots drop.
    radiant_dom_host_invalidate(wrapper);
    heap_unregister_gc_root(&entry->item);
    entry->node = nullptr;
    entry->owner_doc = nullptr;
    entry->item = 0;
}

extern "C" void radiant_dom_invalidate_document(DomDocument* doc) {
    radiant_dom_cache_check_owner("invalidate_document");
    if (!doc) return;
    for (RadiantDomWrapperCacheChunk* chunk = s_radiant_dom_wrapper_cache_head; chunk; chunk = chunk->next) {
        for (int i = 0; i < chunk->count; i++) {
            RadiantDomWrapperCacheEntry* entry = &chunk->entries[i];
            if (entry->owner_doc == doc || radiant_dom_node_document(entry->node, false) == doc) {
                radiant_dom_clear_cache_entry(entry);
            }
        }
    }
}

extern "C" void radiant_dom_reset_wrapper_cache(void) {
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
    s_radiant_dom_wrapper_cache_head = nullptr;
    s_radiant_dom_wrapper_cache_tail = nullptr;
}

extern "C" Item radiant_dom_wrap_node(void* dom_elem) {
    radiant_dom_cache_check_owner("wrap_node");
    if (!dom_elem) return ItemNull;

    DomNode* node = (DomNode*)dom_elem;
    if (node->is_element()) {
        DomElement* e = node->as_element();
        if (e->doc && e->doc->js_doc_node == (void*)e) {
            Item proxy = js_dom_document_proxy_for_doc_bridge(e->doc);
            if (proxy.item != ITEM_NULL) return proxy;
        }
    }

    Item cached = radiant_dom_lookup_wrapper(node);
    if (cached.item != ITEM_NULL) return cached;

    Item wrapper = vmap_new();
    if (get_type_id(wrapper) == LMD_TYPE_VMAP && wrapper.vmap) {
        wrapper.vmap->host_type = radiant_dom_node_host_type();
        wrapper.vmap->host_data = dom_elem;
        js_dom_initialize_node_wrapper(dom_elem);
        radiant_dom_cache_wrapper(node, wrapper);
        return wrapper;
    }
    // Phase 7 removes the DOM-node map shell; a failed VMap allocation must
    // not recreate the stale compatibility carrier or runtime dispatch diverges.
    return ItemNull;
}

extern "C" void* radiant_dom_unwrap_node(Item item) {
    if (get_type_id(item) == LMD_TYPE_VMAP && item.vmap &&
        radiant_dom_is_node_host_type(item.vmap->host_type)) {
        return item.vmap->host_data;
    }
    return js_dom_unwrap_element_impl(item);
}

extern "C" bool radiant_dom_is_node(Item item) {
    if (get_type_id(item) == LMD_TYPE_VMAP && item.vmap &&
        radiant_dom_is_node_host_type(item.vmap->host_type)) {
        return item.vmap->host_data != nullptr;
    }
    return false;
}

static bool radiant_dom_get_text_property(DomText* text_node, const char* prop, Item* out) {
    if (!text_node || !prop || !out) return false;
    if (strcmp(prop, "data") == 0 || strcmp(prop, "nodeValue") == 0 ||
        strcmp(prop, "textContent") == 0) {
        *out = radiant_dom_string_item(text_node->text);
        return true;
    }
    if (strcmp(prop, "length") == 0) {
        *out = radiant_dom_int_item(radiant_dom_utf16_length(text_node->text));
        return true;
    }
    if (strcmp(prop, "nodeType") == 0) {
        *out = radiant_dom_int_item(3);
        return true;
    }
    if (strcmp(prop, "nodeName") == 0) {
        *out = radiant_dom_string_item("#text");
        return true;
    }
    if (strcmp(prop, "parentNode") == 0 || strcmp(prop, "parentElement") == 0) {
        DomNode* parent = text_node->parent;
        *out = (parent && parent->is_element()) ? radiant_dom_node_item(parent) : ItemNull;
        return true;
    }
    if (strcmp(prop, "isConnected") == 0) {
        *out = (Item){.item = b2it(radiant_dom_node_is_connected((DomNode*)text_node) ? 1 : 0)};
        return true;
    }
    if (strcmp(prop, "nextSibling") == 0) {
        *out = radiant_dom_node_item(radiant_dom_next_script_visible_sibling((DomNode*)text_node));
        return true;
    }
    if (strcmp(prop, "previousSibling") == 0) {
        *out = radiant_dom_node_item(radiant_dom_prev_script_visible_sibling((DomNode*)text_node));
        return true;
    }
    if (strcmp(prop, "childNodes") == 0) {
        *out = radiant_dom_empty_node_list();
        return true;
    }
    if (strcmp(prop, "firstChild") == 0 || strcmp(prop, "lastChild") == 0) {
        *out = ItemNull;
        return true;
    }
    if (strcmp(prop, "ownerDocument") == 0) {
        *out = js_dom_owner_document_for_node((void*)text_node);
        return true;
    }
    return false;
}

static bool radiant_dom_get_comment_property(DomComment* comment_node, const char* prop, Item* out) {
    if (!comment_node || !prop || !out) return false;
    if (strcmp(prop, "data") == 0 || strcmp(prop, "nodeValue") == 0 ||
        strcmp(prop, "textContent") == 0) {
        *out = radiant_dom_string_item(comment_node->content);
        return true;
    }
    if (strcmp(prop, "nodeType") == 0) {
        *out = radiant_dom_int_item((int64_t)comment_node->node_type);
        return true;
    }
    if (strcmp(prop, "nodeName") == 0) {
        *out = radiant_dom_string_item("#comment");
        return true;
    }
    if (strcmp(prop, "length") == 0) {
        *out = radiant_dom_int_item((int64_t)comment_node->length);
        return true;
    }
    if (strcmp(prop, "parentNode") == 0 || strcmp(prop, "parentElement") == 0) {
        DomNode* parent = comment_node->parent;
        *out = (parent && parent->is_element()) ? radiant_dom_node_item(parent) : ItemNull;
        return true;
    }
    if (strcmp(prop, "isConnected") == 0) {
        *out = (Item){.item = b2it(radiant_dom_node_is_connected((DomNode*)comment_node) ? 1 : 0)};
        return true;
    }
    if (strcmp(prop, "nextSibling") == 0) {
        *out = radiant_dom_node_item(radiant_dom_next_script_visible_sibling((DomNode*)comment_node));
        return true;
    }
    if (strcmp(prop, "previousSibling") == 0) {
        *out = radiant_dom_node_item(radiant_dom_prev_script_visible_sibling((DomNode*)comment_node));
        return true;
    }
    if (strcmp(prop, "childNodes") == 0) {
        *out = radiant_dom_empty_node_list();
        return true;
    }
    if (strcmp(prop, "firstChild") == 0 || strcmp(prop, "lastChild") == 0) {
        *out = ItemNull;
        return true;
    }
    if (strcmp(prop, "ownerDocument") == 0) {
        *out = js_dom_owner_document_for_node((void*)comment_node);
        return true;
    }
    return false;
}

static bool radiant_dom_get_select_option_property(Item elem_item, DomElement* elem,
                                                   Item prop_name, Item* out) {
    if (!elem || !out) return false;
    const char* prop = fn_to_cstr(prop_name);
    if (radiant_dom_is_tag(elem, "select")) {
        bool numeric = false;
        if (get_type_id(prop_name) == LMD_TYPE_INT) {
            numeric = it2i(prop_name) >= 0;
        } else if (prop && prop[0] >= '0' && prop[0] <= '9') {
            char* end = nullptr;
            long idx = strtol(prop, &end, 10);
            numeric = end && *end == '\0' && idx >= 0;
        }
        if (numeric ||
            (prop && (
                strcmp(prop, "options") == 0 ||
                strcmp(prop, "length") == 0 ||
                strcmp(prop, "selectedOptions") == 0 ||
                strcmp(prop, "selectedIndex") == 0 ||
                strcmp(prop, "value") == 0 ||
                strcmp(prop, "type") == 0))) {
            // select collection/value properties must beat generic element
            // length/reflection fallbacks, otherwise options become stale.
            *out = js_dom_get_property_impl(elem_item, prop_name);
            return true;
        }
    }
    if (radiant_dom_is_tag(elem, "option") && prop &&
        (strcmp(prop, "value") == 0 ||
         strcmp(prop, "text") == 0 ||
         strcmp(prop, "label") == 0 ||
         strcmp(prop, "selected") == 0 ||
         strcmp(prop, "index") == 0 ||
         strcmp(prop, "form") == 0)) {
        *out = js_dom_get_property_impl(elem_item, prop_name);
        return true;
    }
    return false;
}

static bool radiant_dom_get_element_property(DomElement* elem, const char* prop, Item* out) {
    if (!elem || !prop || !out) return false;
    if (strcmp(prop, "tagName") == 0 || strcmp(prop, "nodeName") == 0) {
        *out = (Item){.item = s2it(radiant_dom_uppercase_name(elem->tag_name))};
        return true;
    }
    if (strcmp(prop, "localName") == 0) {
        *out = radiant_dom_string_item(elem->tag_name);
        return true;
    }
    if (strcmp(prop, "content") == 0 &&
        elem->tag_name && strcasecmp(elem->tag_name, "template") == 0) {
        // current template support exposes parsed children through the template
        // wrapper itself; keep that compatibility shim at the module boundary.
        *out = radiant_dom_node_item((DomNode*)elem);
        return true;
    }
    if (strcmp(prop, "namespaceURI") == 0) {
        const char* ns = dom_element_get_attribute(elem, "__lambda_ns_uri");
        *out = radiant_dom_string_item((ns && ns[0] != '\0') ? ns : "http://www.w3.org/1999/xhtml");
        return true;
    }
    if (strcmp(prop, "prefix") == 0) {
        *out = ItemNull;
        return true;
    }
    if (strcmp(prop, "id") == 0) {
        *out = radiant_dom_string_item(elem->id);
        return true;
    }
    if (strcmp(prop, "className") == 0) {
        *out = radiant_dom_class_name_item(elem);
        return true;
    }
    if (strcmp(prop, "nodeType") == 0) {
        *out = radiant_dom_int_item((int64_t)elem->node_type);
        return true;
    }
    const char* bool_attr = nullptr;
    if (radiant_dom_bool_reflected(elem, prop, &bool_attr)) {
        *out = (Item){.item = b2it(dom_element_has_attribute(elem, bool_attr) ? 1 : 0)};
        return true;
    }
    const char* int_attr = nullptr;
    int int_default = 0;
    if (radiant_dom_int_reflected(elem, prop, &int_attr, &int_default)) {
        *out = radiant_dom_int_item(radiant_dom_reflected_int_value(elem, int_attr, int_default));
        return true;
    }
    const char* string_attr = nullptr;
    if (radiant_dom_string_reflected(elem, prop, &string_attr)) {
        const char* value = dom_element_get_attribute(elem, string_attr);
        *out = radiant_dom_string_item(value ? value :
            (radiant_dom_is_tag(elem, "textarea") && strcmp(prop, "wrap") == 0 ? "soft" : ""));
        return true;
    }
    if (strcmp(prop, "inputMode") == 0) {
        static const char* const keywords[] = {
            "none", "text", "decimal", "numeric", "tel", "search", "email", "url", nullptr
        };
        *out = radiant_dom_string_item(radiant_dom_canonical_token_attr(elem, "inputmode", keywords));
        return true;
    }
    if (strcmp(prop, "enterKeyHint") == 0) {
        static const char* const keywords[] = {
            "enter", "done", "go", "next", "previous", "search", "send", nullptr
        };
        *out = radiant_dom_string_item(radiant_dom_canonical_token_attr(elem, "enterkeyhint", keywords));
        return true;
    }
    if (strcmp(prop, "contentEditable") == 0) {
        if (!dom_element_has_attribute(elem, "contenteditable")) {
            *out = radiant_dom_string_item("inherit");
            return true;
        }
        const char* normalized = radiant_dom_normalize_contenteditable(
            dom_element_get_attribute(elem, "contenteditable"));
        *out = radiant_dom_string_item(normalized ? normalized : "inherit");
        return true;
    }
    if (strcmp(prop, "isContentEditable") == 0) {
        *out = (Item){.item = b2it(radiant_dom_is_content_editable(elem) ? 1 : 0)};
        return true;
    }
    if (strcmp(prop, "parentNode") == 0 || strcmp(prop, "parentElement") == 0) {
        DomNode* parent = elem->parent;
        *out = (parent && parent->is_element()) ? radiant_dom_node_item(parent) : ItemNull;
        return true;
    }
    if (strcmp(prop, "isConnected") == 0) {
        *out = (Item){.item = b2it(radiant_dom_node_is_connected((DomNode*)elem) ? 1 : 0)};
        return true;
    }
    if (radiant_dom_is_tag(elem, "form") && strcmp(prop, "elements") == 0) {
        *out = js_dom_live_form_elements_bridge((void*)elem);
        return true;
    }
    if (radiant_dom_is_tag(elem, "form") && strcmp(prop, "length") == 0) {
        Item controls = radiant_dom_form_controls_item(elem);
        *out = radiant_dom_int_item(controls.array ? controls.array->length : 0);
        return true;
    }
    if (strcmp(prop, "childElementCount") == 0) {
        *out = radiant_dom_int_item(radiant_dom_script_visible_element_child_count(elem));
        return true;
    }
    if (strcmp(prop, "length") == 0) {
        *out = radiant_dom_int_item(radiant_dom_script_visible_element_child_count(elem));
        return true;
    }
    if (strcmp(prop, "children") == 0) {
        *out = js_dom_live_child_collection_bridge((void*)elem, true);
        return true;
    }
    if (strcmp(prop, "attributes") == 0) {
        *out = radiant_dom_attributes_item(elem);
        return true;
    }
    if (strcmp(prop, "ownerDocument") == 0) {
        *out = js_dom_owner_document_for_node((void*)elem);
        return true;
    }
    if (strcmp(prop, "firstChild") == 0) {
        *out = radiant_dom_node_item(radiant_dom_first_script_visible_child(elem));
        return true;
    }
    if (strcmp(prop, "lastChild") == 0) {
        *out = radiant_dom_node_item(radiant_dom_last_script_visible_child(elem));
        return true;
    }
    if (strcmp(prop, "nextSibling") == 0) {
        *out = radiant_dom_node_item(radiant_dom_next_script_visible_sibling((DomNode*)elem));
        return true;
    }
    if (strcmp(prop, "previousSibling") == 0) {
        *out = radiant_dom_node_item(radiant_dom_prev_script_visible_sibling((DomNode*)elem));
        return true;
    }
    if (strcmp(prop, "firstElementChild") == 0) {
        *out = radiant_dom_node_item(radiant_dom_first_script_visible_element_child(elem));
        return true;
    }
    if (strcmp(prop, "lastElementChild") == 0) {
        *out = radiant_dom_node_item(radiant_dom_last_script_visible_element_child(elem));
        return true;
    }
    if (strcmp(prop, "nextElementSibling") == 0) {
        *out = radiant_dom_node_item(radiant_dom_next_script_visible_element_sibling((DomNode*)elem));
        return true;
    }
    if (strcmp(prop, "previousElementSibling") == 0) {
        *out = radiant_dom_node_item(radiant_dom_prev_script_visible_element_sibling((DomNode*)elem));
        return true;
    }
    if (strcmp(prop, "childNodes") == 0) {
        *out = js_dom_live_child_collection_bridge((void*)elem, false);
        return true;
    }
    if (radiant_dom_form_named_getter(elem, prop, out)) {
        return true;
    }
    return false;
}

static bool radiant_dom_get_basic_property(Item elem_item, Item prop_name, Item* out) {
    if (!out) return false;
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(elem_item);
    if (!node) return false;
    if (node->is_element() &&
        radiant_dom_get_select_option_property(elem_item, node->as_element(), prop_name, out)) {
        return true;
    }

    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return false;

    if (node->is_text()) {
        return radiant_dom_get_text_property(node->as_text(), prop, out);
    }
    if (node->is_comment()) {
        return radiant_dom_get_comment_property(node->as_comment(), prop, out);
    }
    if (node->is_element()) {
        return radiant_dom_get_element_property(node->as_element(), prop, out);
    }
    return false;
}

static bool radiant_dom_set_basic_property(Item elem_item, Item prop_name, Item value, Item* out) {
    if (!out) return false;
    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return false;

    DomNode* node = (DomNode*)radiant_dom_unwrap_node(elem_item);
    if (!node) return false;

    if (node->is_text() &&
        (strcmp(prop, "data") == 0 ||
         strcmp(prop, "nodeValue") == 0 ||
         strcmp(prop, "textContent") == 0)) {
        *out = js_dom_set_text_data_property((void*)node->as_text(), value);
        return true;
    }

    if (!node->is_element()) return false;

    DomElement* elem = node->as_element();
    if (strcmp(prop, "id") == 0) {
        const char* id_str = fn_to_cstr(value);
        if (id_str && dom_element_set_attribute(elem, "id", id_str)) {
            js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
        }
        *out = value;
        return true;
    }
    if (strcmp(prop, "className") == 0) {
        const char* class_str = fn_to_cstr(value);
        if (class_str && dom_element_set_attribute(elem, "class", class_str)) {
            js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
        }
        *out = value;
        return true;
    }
    if (strcmp(prop, "contentEditable") == 0) {
        const char* text = nullptr;
        if (get_type_id(value) == LMD_TYPE_BOOL) {
            text = it2b(value) ? "true" : "false";
        } else {
            text = fn_to_cstr(value);
        }
        if (!text) text = "";
        if (*text == '\0') {
            dom_element_remove_attribute(elem, "contenteditable");
            js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
            *out = value;
            return true;
        }
        const char* normalized = radiant_dom_normalize_contenteditable(text);
        if (!normalized) {
            js_dom_throw_contenteditable_syntax_error();
            *out = value;
            return true;
        }
        if (strcmp(normalized, "inherit") == 0) {
            dom_element_remove_attribute(elem, "contenteditable");
        } else {
            dom_element_set_attribute(elem, "contenteditable", normalized);
        }
        js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
        *out = value;
        return true;
    }
    if (strcmp(prop, "srcdoc") == 0 && radiant_dom_is_tag(elem, "iframe")) {
        const char* text = fn_to_cstr(value);
        dom_element_set_attribute(elem, "srcdoc", text ? text : "");
        js_dom_after_srcdoc_set((void*)elem);
        js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
        *out = value;
        return true;
    }
    if (strcmp(prop, "autocapitalize") == 0) {
        const char* text = js_dom_to_attribute_cstr(value);
        dom_element_set_attribute(elem, "autocapitalize", text ? text : "");
        js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
        *out = value;
        return true;
    }
    if (strcmp(prop, "autocorrect") == 0) {
        dom_element_set_attribute(elem, "autocorrect", js_is_truthy(value) ? "on" : "off");
        js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
        *out = value;
        return true;
    }
    if (strcmp(prop, "spellcheck") == 0) {
        dom_element_set_attribute(elem, "spellcheck", radiant_dom_item_to_html_bool_string(value));
        js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
        *out = value;
        return true;
    }
    if (strcmp(prop, "writingSuggestions") == 0) {
        dom_element_set_attribute(elem, "writingsuggestions", radiant_dom_item_to_html_bool_string(value));
        js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
        *out = value;
        return true;
    }
    const char* disabled_attr = nullptr;
    if (radiant_dom_disabled_setter(elem, prop, &disabled_attr)) {
        if (js_is_truthy(value)) {
            dom_element_set_attribute(elem, disabled_attr, "");
            js_dom_after_disabled_attribute_set((void*)elem);
        } else {
            dom_element_remove_attribute(elem, disabled_attr);
        }
        js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
        *out = value;
        return true;
    }
    const char* live_bool_attr = nullptr;
    if (radiant_dom_live_bool_setter(elem, prop, &live_bool_attr)) {
        bool truthy = js_is_truthy(value);
        if (truthy) {
            dom_element_set_attribute(elem, live_bool_attr, "");
        } else {
            dom_element_remove_attribute(elem, live_bool_attr);
        }
        // These reflected booleans carry live checked/selected invariants that
        // remain centralized in js_dom.cpp while dispatch moves into the module.
        if (strcmp(prop, "defaultChecked") == 0) {
            js_dom_after_default_checked_set((void*)elem, truthy);
        } else if (strcmp(prop, "defaultSelected") == 0) {
            js_dom_after_default_selected_set((void*)elem, truthy);
        } else if (!truthy) {
            js_dom_after_select_multiple_removed((void*)elem);
        }
        if (strcmp(prop, "multiple") == 0) {
            js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
        }
        *out = value;
        return true;
    }
    if (strcmp(prop, "checked") == 0 && radiant_dom_is_tag(elem, "input")) {
        js_dom_set_checked_dirty((void*)elem, js_is_truthy(value));
        *out = value;
        return true;
    }
    if (radiant_dom_is_tag(elem, "select")) {
        if (strcmp(prop, "value") == 0) {
            const char* text = fn_to_cstr(value);
            js_dom_select_set_value_bridge((void*)elem, text ? text : "");
            *out = value;
            return true;
        }
        if (strcmp(prop, "selectedIndex") == 0) {
            js_dom_select_set_selected_index_bridge((void*)elem, value);
            *out = value;
            return true;
        }
        if (strcmp(prop, "length") == 0) {
            js_dom_select_set_length_bridge((void*)elem, value);
            *out = value;
            return true;
        }
    }
    if (strcmp(prop, "selected") == 0 && radiant_dom_is_tag(elem, "option")) {
        js_dom_set_option_selected_dirty((void*)elem, js_is_truthy(value));
        *out = value;
        return true;
    }
    if (strcmp(prop, "value") == 0 && radiant_dom_is_tag(elem, "option")) {
        const char* text = fn_to_cstr(value);
        dom_element_set_attribute(elem, "value", text ? text : "");
        *out = value;
        return true;
    }
    if (strcmp(prop, "text") == 0 && radiant_dom_is_tag(elem, "option")) {
        const char* text = fn_to_cstr(value);
        js_dom_set_option_text_bridge((void*)elem, text ? text : "");
        *out = value;
        return true;
    }
    if (tc_is_text_control(elem)) {
        if (strcmp(prop, "value") == 0) {
            *out = js_dom_text_control_set_value_bridge((void*)elem, value);
            return true;
        }
        if (strcmp(prop, "selectionStart") == 0) {
            *out = js_dom_text_control_set_selection_start_bridge((void*)elem, value);
            return true;
        }
        if (strcmp(prop, "selectionEnd") == 0) {
            *out = js_dom_text_control_set_selection_end_bridge((void*)elem, value);
            return true;
        }
        if (strcmp(prop, "selectionDirection") == 0) {
            *out = js_dom_text_control_set_selection_direction_bridge((void*)elem, value);
            return true;
        }
        if (strcmp(prop, "defaultValue") == 0) {
            *out = js_dom_text_control_set_default_value_bridge((void*)elem, value);
            return true;
        }
    }
    if (strcmp(prop, "value") == 0 && radiant_dom_is_tag(elem, "input") &&
        !tc_is_text_control(elem)) {
        const char* text = fn_to_cstr(value);
        dom_element_set_attribute(elem, "value", text ? text : "");
        if (elem->form) {
            elem->form->value = dom_element_get_attribute(elem, "value");
        }
        js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
        *out = value;
        return true;
    }
    const char* bool_attr = nullptr;
    if (radiant_dom_simple_bool_setter(elem, prop, &bool_attr)) {
        // only side-effect-free boolean reflections move here; live-state
        // booleans stay on the JS fallback until their invariants move too.
        if (js_is_truthy(value)) {
            dom_element_set_attribute(elem, bool_attr, "");
        } else {
            dom_element_remove_attribute(elem, bool_attr);
        }
        js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
        *out = value;
        return true;
    }
    const char* int_attr = nullptr;
    int int_default = 0;
    if (radiant_dom_int_reflected(elem, prop, &int_attr, &int_default)) {
        long reflected = radiant_dom_item_to_reflected_int(value, int_default);
        char buf[32];
        snprintf(buf, sizeof(buf), "%ld", reflected);
        dom_element_set_attribute(elem, int_attr, buf);
        js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
        *out = value;
        return true;
    }
    const char* string_attr = nullptr;
    if (radiant_dom_string_reflected(elem, prop, &string_attr)) {
        const char* text = js_dom_to_attribute_cstr(value);
        dom_element_set_attribute(elem, string_attr, text ? text : "");
        js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
        *out = value;
        return true;
    }
    const char* hint_attr = nullptr;
    if (radiant_dom_hint_reflected(prop, &hint_attr)) {
        const char* text = js_dom_to_attribute_cstr(value);
        dom_element_set_attribute(elem, hint_attr, text ? text : "");
        js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
        *out = value;
        return true;
    }
    return false;
}

static Item radiant_dom_attribute_names_item(DomElement* elem) {
    Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    arr->type_id = LMD_TYPE_ARRAY;
    arr->items = nullptr;
    arr->length = 0;
    arr->capacity = 0;

    int attr_count = 0;
    const char** attr_names = dom_element_get_attribute_names(elem, &attr_count);
    for (int i = 0; attr_names && i < attr_count; i++) {
        if (radiant_dom_is_internal_attr(attr_names[i])) continue;
        array_push(arr, radiant_dom_string_item(attr_names[i]));
    }
    return (Item){.array = arr};
}

static bool radiant_dom_element_method_basic(Item elem_item, Item method_name, Item* args, int argc, Item* out) {
    if (!out) return false;
    const char* method = fn_to_cstr(method_name);
    if (!method) return false;

    DomNode* node = (DomNode*)radiant_dom_unwrap_node(elem_item);
    if (!node) return false;

    if (node->is_element()) {
        DomElement* method_elem = node->as_element();
        if (radiant_dom_is_tag(method_elem, "select") &&
            (strcmp(method, "namedItem") == 0 ||
             strcmp(method, "add") == 0 ||
             strcmp(method, "remove") == 0)) {
            // HTMLSelectElement overrides ChildNode.remove(); dispatch this
            // before generic node removal so select.remove(index) and
            // select.remove() keep the legacy option-list semantics.
            *out = js_dom_element_method_impl(elem_item, method_name, args, argc);
            return true;
        }
    }

    if (strcmp(method, "contains") == 0) {
        DomNode* other = (argc >= 1) ? (DomNode*)radiant_dom_unwrap_node(args[0]) : nullptr;
        *out = (Item){.item = b2it(radiant_dom_node_contains(node, other) ? 1 : 0)};
        return true;
    }

    if (strcmp(method, "compareDocumentPosition") == 0) {
        DomNode* other = (argc >= 1) ? (DomNode*)radiant_dom_unwrap_node(args[0]) : nullptr;
        *out = radiant_dom_int_item(radiant_dom_compare_document_position(node, other));
        return true;
    }

    if (strcmp(method, "remove") == 0) {
        *out = js_dom_remove_bridge((void*)node);
        return true;
    }

    if (strcmp(method, "replaceWith") == 0) {
        *out = js_dom_replace_with_bridge((void*)node, args, argc);
        return true;
    }

    if (node->is_text()) {
        DomText* text = node->as_text();
        if (strcmp(method, "replaceData") == 0) {
            *out = js_dom_text_replace_data_bridge((void*)text,
                argc >= 1 ? args[0] : radiant_dom_undefined_item(),
                argc >= 2 ? args[1] : radiant_dom_undefined_item(),
                argc >= 3 ? args[2] : radiant_dom_undefined_item());
            return true;
        }
        if (strcmp(method, "insertData") == 0) {
            *out = js_dom_text_insert_data_bridge((void*)text,
                argc >= 1 ? args[0] : radiant_dom_undefined_item(),
                argc >= 2 ? args[1] : radiant_dom_undefined_item());
            return true;
        }
        if (strcmp(method, "appendData") == 0) {
            *out = js_dom_text_append_data_bridge((void*)text,
                argc >= 1 ? args[0] : radiant_dom_undefined_item());
            return true;
        }
        if (strcmp(method, "deleteData") == 0) {
            *out = js_dom_text_delete_data_bridge((void*)text,
                argc >= 1 ? args[0] : radiant_dom_undefined_item(),
                argc >= 2 ? args[1] : radiant_dom_undefined_item());
            return true;
        }
        if (strcmp(method, "substringData") == 0) {
            *out = js_dom_text_substring_data_bridge((void*)text,
                argc >= 1 ? args[0] : radiant_dom_undefined_item(),
                argc >= 2 ? args[1] : radiant_dom_undefined_item());
            return true;
        }
    }

    if (!node->is_element()) return false;
    DomElement* elem = node->as_element();

    if (strcmp(method, "getAttribute") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return true;
        }
        const char* attr_name = fn_to_cstr(args[0]);
        if (!attr_name || radiant_dom_is_internal_attr(attr_name)) {
            *out = ItemNull;
            return true;
        }
        const char* val = dom_element_get_attribute(elem, attr_name);
        if (val) {
            *out = radiant_dom_string_item(val);
            return true;
        }
        *out = dom_element_has_attribute(elem, attr_name) ? radiant_dom_string_item("") : ItemNull;
        return true;
    }

    if (strcmp(method, "hasAttribute") == 0) {
        if (argc < 1) {
            *out = (Item){.item = b2it(0)};
            return true;
        }
        const char* attr_name = fn_to_cstr(args[0]);
        bool has = attr_name && !radiant_dom_is_internal_attr(attr_name) &&
            dom_element_has_attribute(elem, attr_name);
        *out = (Item){.item = b2it(has ? 1 : 0)};
        return true;
    }

    if (strcmp(method, "getAttributeNames") == 0) {
        *out = radiant_dom_attribute_names_item(elem);
        return true;
    }

    if (strcmp(method, "getElementsByTagName") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return true;
        }
        *out = js_dom_live_element_get_elements_by_tag_name_bridge((void*)elem, args[0]);
        return true;
    }

    if (strcmp(method, "getElementsByClassName") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return true;
        }
        *out = js_dom_live_element_get_elements_by_class_name_bridge((void*)elem, args[0]);
        return true;
    }

    if (strcmp(method, "querySelector") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return true;
        }
        const char* sel_text = fn_to_cstr(args[0]);
        if (!sel_text || !elem->doc) {
            *out = ItemNull;
            return true;
        }
        CssSelectorGroup* selector_group = radiant_dom_parse_css_selector_group(sel_text, elem->doc->pool);
        if (!selector_group) {
            *out = ItemNull;
            return true;
        }
        // selector parsing and matching stay on the document pool so returned
        // wrappers are the only GC-managed values created by this read-only path.
        SelectorMatcher* matcher = selector_matcher_create(elem->doc->pool);
        DomElement* found = radiant_dom_selector_group_find_first(matcher, selector_group, elem);
        *out = radiant_dom_node_item((DomNode*)found);
        return true;
    }

    if (strcmp(method, "querySelectorAll") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return true;
        }
        const char* sel_text = fn_to_cstr(args[0]);
        if (!sel_text || !elem->doc) {
            *out = ItemNull;
            return true;
        }
        Item arr_item = radiant_dom_empty_array_item();
        CssSelectorGroup* selector_group = radiant_dom_parse_css_selector_group(sel_text, elem->doc->pool);
        if (!selector_group) {
            *out = arr_item;
            return true;
        }
        SelectorMatcher* matcher = selector_matcher_create(elem->doc->pool);
        ArrayList* results = arraylist_new(16);
        if (!results) {
            *out = arr_item;
            return true;
        }
        radiant_dom_selector_group_collect_all(matcher, selector_group, elem, results);
        for (int i = 0; i < results->length; i++) {
            array_push(arr_item.array, radiant_dom_node_item((DomNode*)results->data[i]));
        }
        arraylist_free(results);
        *out = arr_item;
        return true;
    }

    if (strcmp(method, "matches") == 0) {
        if (argc < 1) {
            *out = (Item){.item = b2it(0)};
            return true;
        }
        const char* sel_text = fn_to_cstr(args[0]);
        if (!sel_text || !elem->doc) {
            *out = (Item){.item = b2it(0)};
            return true;
        }
        CssSelectorGroup* selector_group = radiant_dom_parse_css_selector_group(sel_text, elem->doc->pool);
        if (!selector_group) {
            *out = (Item){.item = b2it(0)};
            return true;
        }
        SelectorMatcher* matcher = selector_matcher_create(elem->doc->pool);
        MatchResult result;
        bool matched = selector_matcher_matches_group(matcher, selector_group, elem, &result);
        *out = (Item){.item = b2it(matched ? 1 : 0)};
        return true;
    }

    if (strcmp(method, "closest") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return true;
        }
        const char* sel_text = fn_to_cstr(args[0]);
        if (!sel_text || !elem->doc) {
            *out = ItemNull;
            return true;
        }
        CssSelectorGroup* selector_group = radiant_dom_parse_css_selector_group(sel_text, elem->doc->pool);
        if (!selector_group) {
            *out = ItemNull;
            return true;
        }
        SelectorMatcher* matcher = selector_matcher_create(elem->doc->pool);
        MatchResult result;
        DomElement* current = elem;
        while (current) {
            if (selector_matcher_matches_group(matcher, selector_group, current, &result)) {
                *out = radiant_dom_node_item((DomNode*)current);
                return true;
            }
            DomNode* parent = current->parent;
            current = (parent && parent->is_element()) ? parent->as_element() : nullptr;
        }
        *out = ItemNull;
        return true;
    }

    if (strcmp(method, "getElementById") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return true;
        }
        const char* id = fn_to_cstr(args[0]);
        if (!id) {
            *out = ItemNull;
            return true;
        }
        *out = radiant_dom_node_item((DomNode*)radiant_dom_find_by_id(elem, id));
        return true;
    }

    if (strcmp(method, "appendChild") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return true;
        }
        *out = js_dom_append_child_bridge((void*)elem, args[0]);
        return true;
    }

    if (strcmp(method, "removeChild") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return true;
        }
        *out = js_dom_remove_child_bridge((void*)elem, args[0]);
        return true;
    }

    if (strcmp(method, "insertBefore") == 0) {
        if (argc < 2) {
            *out = ItemNull;
            return true;
        }
        *out = js_dom_insert_before_bridge((void*)elem, args[0], args[1]);
        return true;
    }

    if (strcmp(method, "normalize") == 0) {
        *out = js_dom_normalize_bridge((void*)elem);
        return true;
    }

    if (strcmp(method, "cloneNode") == 0) {
        *out = js_dom_clone_node_bridge((void*)elem,
            argc >= 1 ? args[0] : radiant_dom_undefined_item(), argc >= 1);
        return true;
    }

    if (strcmp(method, "replaceChild") == 0) {
        if (argc < 2) {
            *out = ItemNull;
            return true;
        }
        *out = js_dom_replace_child_bridge((void*)elem, args[0], args[1]);
        return true;
    }

    if (strcmp(method, "insertAdjacentElement") == 0) {
        if (argc < 2) {
            *out = ItemNull;
            return true;
        }
        *out = js_dom_insert_adjacent_element_bridge((void*)elem, args[0], args[1]);
        return true;
    }

    if (strcmp(method, "insertAdjacentHTML") == 0) {
        if (argc < 2) {
            *out = ItemNull;
            return true;
        }
        *out = js_dom_insert_adjacent_html_bridge((void*)elem, args[0], args[1]);
        return true;
    }

    if (strcmp(method, "append") == 0) {
        *out = js_dom_append_variadic_bridge((void*)elem, args, argc);
        return true;
    }

    if (strcmp(method, "prepend") == 0) {
        *out = js_dom_prepend_variadic_bridge((void*)elem, args, argc);
        return true;
    }

    if (strcmp(method, "addEventListener") == 0) {
        *out = argc >= 2
            ? js_dom_add_event_listener_bridge(elem_item, args[0], args[1],
                argc >= 3 ? args[2] : ItemNull)
            : radiant_dom_undefined_item();
        return true;
    }

    if (strcmp(method, "removeEventListener") == 0) {
        *out = argc >= 2
            ? js_dom_remove_event_listener_bridge(elem_item, args[0], args[1],
                argc >= 3 ? args[2] : ItemNull)
            : radiant_dom_undefined_item();
        return true;
    }

    if (strcmp(method, "dispatchEvent") == 0) {
        *out = argc >= 1
            ? js_dom_dispatch_event_bridge(elem_item, args[0])
            : (Item){.item = b2it(0)};
        return true;
    }

    if (strcmp(method, "getBoundingClientRect") == 0) {
        *out = js_dom_get_bounding_client_rect_bridge((void*)elem);
        return true;
    }

    if (strcmp(method, "getClientRects") == 0) {
        *out = js_dom_get_client_rects_bridge((void*)elem);
        return true;
    }

    if (strcmp(method, "scrollIntoView") == 0) {
        *out = js_dom_scroll_into_view_bridge((void*)elem);
        return true;
    }

    if (strcmp(method, "scroll") == 0 ||
        strcmp(method, "scrollTo") == 0 ||
        strcmp(method, "scrollBy") == 0) {
        *out = js_dom_scroll_method_bridge(elem_item, method_name, args, argc);
        return true;
    }

    if (strcmp(method, "__lambdaTextControlCaretBounds") == 0) {
        *out = js_dom_text_control_caret_bounds_bridge((void*)elem);
        return true;
    }

    if (strcmp(method, "__lambdaTextControlBoundaryFromPoint") == 0) {
        *out = js_dom_text_control_boundary_from_point_bridge((void*)elem,
            argc >= 1 ? args[0] : radiant_dom_undefined_item(),
            argc >= 2 ? args[1] : radiant_dom_undefined_item());
        return true;
    }

    if (strcmp(method, "__lambdaBoundaryFromPoint") == 0) {
        *out = js_dom_boundary_from_point_bridge((void*)elem,
            argc >= 1 ? args[0] : radiant_dom_undefined_item(),
            argc >= 2 ? args[1] : radiant_dom_undefined_item(),
            argc >= 3 ? args[2] : radiant_dom_undefined_item());
        return true;
    }

    if (strcmp(method, "focus") == 0 || strcmp(method, "blur") == 0) {
        *out = js_dom_focus_method_bridge((void*)elem, strcmp(method, "focus") == 0);
        return true;
    }

    if (strcmp(method, "click") == 0) {
        *out = js_dom_click_method_bridge(elem_item);
        return true;
    }

    if (radiant_dom_is_tag(elem, "form")) {
        if (strcmp(method, "submit") == 0) {
            *out = js_dom_form_submit_bridge(elem_item);
            return true;
        }
        if (strcmp(method, "requestSubmit") == 0) {
            *out = js_dom_form_request_submit_bridge(elem_item,
                argc >= 1 ? args[0] : radiant_dom_undefined_item());
            return true;
        }
        if (strcmp(method, "reset") == 0) {
            *out = js_dom_form_reset_bridge(elem_item);
            return true;
        }
        if (strcmp(method, "checkValidity") == 0) {
            *out = js_dom_check_validity_bridge(elem_item);
            return true;
        }
        if (strcmp(method, "reportValidity") == 0) {
            *out = js_dom_report_validity_bridge(elem_item);
            return true;
        }
    }

    if (tc_is_text_control(elem)) {
        if (strcmp(method, "setSelectionRange") == 0) {
            // preserve the legacy DOM fallback no-op when required offsets are absent.
            if (argc < 2) {
                *out = radiant_dom_undefined_item();
                return true;
            }
            *out = js_dom_text_control_set_selection_range_bridge((void*)elem,
                argc >= 1 ? args[0] : radiant_dom_undefined_item(),
                argc >= 2 ? args[1] : radiant_dom_undefined_item(),
                argc >= 3 ? args[2] : radiant_dom_undefined_item());
            return true;
        }
        if (strcmp(method, "setRangeText") == 0) {
            *out = js_dom_text_control_set_range_text_bridge((void*)elem,
                argc >= 1 ? args[0] : radiant_dom_undefined_item(),
                argc >= 2 ? args[1] : radiant_dom_undefined_item(),
                argc >= 3 ? args[2] : radiant_dom_undefined_item(),
                argc >= 4 ? args[3] : radiant_dom_undefined_item());
            return true;
        }
        if (strcmp(method, "select") == 0) {
            *out = js_dom_text_control_select_bridge((void*)elem);
            return true;
        }
    }

    if (strcmp(method, "setAttribute") == 0) {
        if (argc < 2) {
            *out = ItemNull;
            return true;
        }
        const char* attr_name = fn_to_cstr(args[0]);
        const char* attr_val = js_dom_to_attribute_cstr(args[1]);
        if (!attr_name || !attr_val || radiant_dom_is_internal_attr(attr_name)) {
            *out = ItemNull;
            return true;
        }
        dom_element_set_attribute(elem, attr_name, attr_val);
        js_dom_after_set_attribute((void*)elem, attr_name, attr_val);
        js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
        *out = ItemNull;
        return true;
    }

    if (strcmp(method, "removeAttribute") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return true;
        }
        const char* attr_name = fn_to_cstr(args[0]);
        if (!attr_name) {
            *out = ItemNull;
            return true;
        }
        dom_element_remove_attribute(elem, attr_name);
        js_dom_after_remove_attribute((void*)elem, attr_name);
        js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
        *out = ItemNull;
        return true;
    }

    if (strcmp(method, "toggleAttribute") == 0) {
        if (argc < 1) {
            *out = (Item){.item = b2it(0)};
            return true;
        }
        const char* attr_name = fn_to_cstr(args[0]);
        if (!attr_name) {
            *out = (Item){.item = b2it(0)};
            return true;
        }
        bool has = dom_element_has_attribute(elem, attr_name);
        bool should_have = (argc >= 2) ? js_is_truthy(args[1]) : !has;
        if (should_have && !has) {
            dom_element_set_attribute(elem, attr_name, "");
        } else if (!should_have && has) {
            dom_element_remove_attribute(elem, attr_name);
            js_dom_after_toggle_attribute_remove((void*)elem, attr_name);
        }
        if (should_have != has) {
            js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE, (void*)elem, (void*)elem->parent);
        }
        *out = (Item){.item = b2it(should_have ? 1 : 0)};
        return true;
    }

    if (strcmp(method, "hasChildNodes") == 0) {
        *out = (Item){.item = b2it(radiant_dom_first_script_visible_child(elem) ? 1 : 0)};
        return true;
    }

    return false;
}

extern "C" Item radiant_dom_get_property(Item elem_item, Item prop_name) {
    if (js_dom_item_is_range(elem_item)) {
        return js_dom_range_get_property(elem_item, prop_name);
    }
    if (js_dom_item_is_selection(elem_item)) {
        return js_dom_selection_get_property(elem_item, prop_name);
    }
    Item result = ItemNull;
    if (radiant_dom_get_basic_property(elem_item, prop_name, &result)) {
        return result;
    }
    return js_dom_get_property_impl(elem_item, prop_name);
}

extern "C" Item radiant_dom_set_property(Item elem_item, Item prop_name, Item value) {
    if (js_dom_item_is_range(elem_item)) {
        return js_dom_range_set_property(elem_item, prop_name, value);
    }
    if (js_dom_item_is_selection(elem_item)) {
        return js_dom_selection_set_property(elem_item, prop_name, value);
    }
    Item result = ItemNull;
    if (radiant_dom_set_basic_property(elem_item, prop_name, value, &result)) {
        return result;
    }
    return js_dom_set_property_impl(elem_item, prop_name, value);
}

extern "C" Item radiant_dom_element_method(Item elem_item, Item method_name, Item* args, int argc) {
    Item result = ItemNull;
    if (radiant_dom_element_method_basic(elem_item, method_name, args, argc, &result)) {
        return result;
    }
    return js_dom_element_method_impl(elem_item, method_name, args, argc);
}

static bool radiant_dom_key_equals(Item key, const char* name, uint32_t name_len) {
    if (get_type_id(key) != LMD_TYPE_STRING) return false;
    String* str_key = it2s(key);
    return str_key && str_key->len == name_len &&
        strncmp(str_key->chars, name, name_len) == 0;
}

static Item radiant_dom_data_descriptor(Item value, bool writable,
                                        bool enumerable, bool configurable) {
    Item desc = js_new_object();
    js_property_set(desc, (Item){.item = s2it(heap_create_name("value"))}, value);
    js_property_set(desc, (Item){.item = s2it(heap_create_name("writable"))},
        (Item){.item = b2it(writable ? 1 : 0)});
    js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable"))},
        (Item){.item = b2it(enumerable ? 1 : 0)});
    js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable"))},
        (Item){.item = b2it(configurable ? 1 : 0)});
    return desc;
}

static bool radiant_dom_projected_own_value(Item object, Item key, Item* out) {
    if (!out || get_type_id(key) != LMD_TYPE_STRING) return false;
    if (js_dom_item_is_range(object)) {
        if (!js_dom_range_native_property(object, key)) return false;
        *out = js_dom_range_get_property(object, key);
        return true;
    }
    if (js_dom_item_is_selection(object)) {
        if (!js_dom_selection_native_property(object, key)) return false;
        *out = js_dom_selection_get_property(object, key);
        return true;
    }
    return radiant_dom_get_basic_property(object, key, out);
}

static bool radiant_dom_has_expando(Item object, Item key) {
    if (js_dom_item_is_range(object)) return js_dom_range_expando_has_property(object, key);
    if (js_dom_item_is_selection(object)) return js_dom_selection_expando_has_property(object, key);
    return js_dom_expando_has_property(object, key);
}

static Item radiant_dom_expando_descriptor(Item object, Item key) {
    if (js_dom_item_is_range(object)) return js_dom_range_expando_get_own_property_descriptor(object, key);
    if (js_dom_item_is_selection(object)) return js_dom_selection_expando_get_own_property_descriptor(object, key);
    return js_dom_expando_get_own_property_descriptor(object, key);
}

static Item radiant_dom_delete_expando(Item object, Item key) {
    if (js_dom_item_is_range(object)) return js_dom_range_expando_delete_property(object, key);
    if (js_dom_item_is_selection(object)) return js_dom_selection_expando_delete_property(object, key);
    return js_dom_expando_delete_property(object, key);
}

static Item radiant_dom_expando_names(Item object) {
    if (js_dom_item_is_range(object)) return js_dom_range_expando_own_property_names(object);
    if (js_dom_item_is_selection(object)) return js_dom_selection_expando_own_property_names(object);
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
    Item key_item = (Item){.item = s2it(heap_create_name(key))};
    Item value = ItemNull;
    if (radiant_dom_projected_own_value(object, key_item, &value)) {
        js_array_push(result, key_item);
    }
}

extern "C" int radiant_dom_host_get_property(Item object, Item key, Item* out) {
    if (!out) return 0;
    *out = radiant_dom_get_property(object, key);
    return 1;
}

extern "C" int radiant_dom_host_set_property(Item object, Item key, Item value, Item* out) {
    if (!out) return 0;
    *out = radiant_dom_set_property(object, key, value);
    return 1;
}

extern "C" int radiant_dom_host_call_method(Item object,
                                            Item method_name,
                                            Item* args,
                                            int argc,
                                            Item* out) {
    if (!out) return 0;
    if (js_dom_item_is_range(object)) {
        Item fn = js_dom_range_get_property(object, method_name);
        *out = js_call_function(fn, object, args, argc);
        return 1;
    }
    if (js_dom_item_is_selection(object)) {
        Item fn = js_dom_selection_get_property(object, method_name);
        *out = js_call_function(fn, object, args, argc);
        return 1;
    }
    *out = radiant_dom_element_method(object, method_name, args, argc);
    return 1;
}

extern "C" int radiant_dom_host_has_property(Item object, Item key, Item* out) {
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

extern "C" int radiant_dom_host_delete_property(Item object, Item key, Item* out) {
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

extern "C" int radiant_dom_host_own_property_descriptor(Item object, Item key, Item* out) {
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

extern "C" int radiant_dom_host_own_property_names(Item object, Item* out) {
    if (!out) return 0;
    Item result = js_array_new(0);
    if (js_dom_item_is_range(object)) {
        static const char* const range_keys[] = {
            "startContainer", "startOffset", "endContainer", "endOffset",
            "collapsed", "commonAncestorContainer", nullptr
        };
        for (int i = 0; range_keys[i]; i++) radiant_dom_push_projected_key(result, object, range_keys[i]);
    } else if (js_dom_item_is_selection(object)) {
        static const char* const selection_keys[] = {
            "anchorNode", "anchorOffset", "focusNode", "focusOffset",
            "isCollapsed", "rangeCount", "type", "direction", nullptr
        };
        for (int i = 0; selection_keys[i]; i++) radiant_dom_push_projected_key(result, object, selection_keys[i]);
    } else {
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
            if (!radiant_dom_array_has_key(result, key)) js_array_push(result, key);
        }
    }
    *out = result;
    return 1;
}

extern "C" Item radiant_dom_host_prototype(Item object) {
    if (js_dom_item_is_range(object)) return js_dom_range_get_prototype_value();
    if (js_dom_item_is_selection(object)) return js_dom_selection_get_prototype_value();
    return js_dom_get_prototype_value(object);
}

extern "C" void radiant_dom_host_invalidate(Item object) {
    if (get_type_id(object) == LMD_TYPE_VMAP && object.vmap &&
        radiant_dom_is_node_host_type(object.vmap->host_type)) {
        object.vmap->host_data = nullptr;
    }
}

extern "C" int radiant_dom_style_host_get_property(Item object, Item key, Item* out) {
    if (!out) return 0;
    *out = js_is_computed_style_item(object)
        ? js_computed_style_get_property(object, key)
        : radiant_dom_get_property(object, key);
    return 1;
}

extern "C" int radiant_dom_style_host_set_property(Item object, Item key, Item value, Item* out) {
    if (!out) return 0;
    *out = js_is_computed_style_item(object) ? value : radiant_dom_set_property(object, key, value);
    return 1;
}

extern "C" int radiant_dom_style_host_call_method(Item object,
                                                  Item method_name,
                                                  Item* args,
                                                  int argc,
                                                  Item* out) {
    if (!out) return 0;
    if (js_is_computed_style_item(object)) {
        const char* method = fn_to_cstr(method_name);
        if (method && strcmp(method, "getPropertyValue") == 0) {
            *out = argc >= 1 ? js_computed_style_get_property(object, args[0]) : radiant_dom_string_item("");
            return 1;
        }
        *out = ItemNull;
        return 1;
    }
    *out = js_dom_style_method(object, method_name, args, argc);
    return 1;
}

extern "C" int radiant_dom_style_host_has_property(Item object, Item key, Item* out) {
    if (!out) return 0;
    *out = (Item){.item = b2it(js_dom_style_resource_has_property(object, key) ? 1 : 0)};
    return 1;
}

extern "C" int radiant_dom_style_host_delete_property(Item object, Item key, Item* out) {
    if (!out) return 0;
    *out = (Item){.item = b2it(js_dom_style_resource_has_property(object, key) ? 0 : 1)};
    return 1;
}

extern "C" int radiant_dom_style_host_own_property_descriptor(Item object, Item key, Item* out) {
    if (!out) return 0;
    *out = radiant_dom_undefined_item();
    return 1;
}

extern "C" int radiant_dom_style_host_own_property_names(Item object, Item* out) {
    if (!out) return 0;
    *out = js_array_new(0);
    return 1;
}

extern "C" Item radiant_dom_style_host_prototype(Item object) {
    return ItemNull;
}

extern "C" int radiant_dom_cssom_host_get_property(Item object, Item key, Item* out) {
    if (!out) return 0;
    if (js_is_stylesheet(object)) {
        *out = js_cssom_stylesheet_get_property(object, key);
        return 1;
    }
    if (js_is_css_rule(object)) {
        *out = js_cssom_rule_get_property(object, key);
        return 1;
    }
    if (js_is_rule_style_decl(object)) {
        *out = js_cssom_rule_decl_get_property(object, key);
        return 1;
    }
    return 0;
}

extern "C" int radiant_dom_cssom_host_set_property(Item object, Item key, Item value, Item* out) {
    if (!out) return 0;
    if (js_is_css_rule(object)) {
        *out = js_cssom_rule_set_property(object, key, value);
        return 1;
    }
    if (js_is_rule_style_decl(object)) {
        *out = js_cssom_rule_decl_set_property(object, key, value);
        return 1;
    }
    if (js_is_stylesheet(object)) {
        *out = value;
        return 1;
    }
    return 0;
}

extern "C" int radiant_dom_cssom_host_call_method(Item object,
                                                  Item method_name,
                                                  Item* args,
                                                  int argc,
                                                  Item* out) {
    if (!out) return 0;
    if (js_is_stylesheet(object)) {
        *out = js_cssom_stylesheet_method(object, method_name, args, argc);
        return 1;
    }
    if (js_is_rule_style_decl(object)) {
        *out = js_cssom_rule_decl_method(object, method_name, args, argc);
        return 1;
    }
    if (js_is_css_rule(object)) {
        *out = ItemNull;
        return 1;
    }
    return 0;
}

extern "C" int radiant_dom_cssom_host_has_property(Item object, Item key, Item* out) {
    if (!out) return 0;
    *out = (Item){.item = b2it(js_cssom_resource_has_property(object, key) ? 1 : 0)};
    return 1;
}

extern "C" int radiant_dom_cssom_host_delete_property(Item object, Item key, Item* out) {
    if (!out) return 0;
    *out = (Item){.item = b2it(js_cssom_resource_has_property(object, key) ? 0 : 1)};
    return 1;
}

extern "C" int radiant_dom_cssom_host_own_property_descriptor(Item object, Item key, Item* out) {
    if (!out) return 0;
    *out = radiant_dom_undefined_item();
    return 1;
}

extern "C" int radiant_dom_cssom_host_own_property_names(Item object, Item* out) {
    if (!out) return 0;
    *out = js_array_new(0);
    return 1;
}

extern "C" Item radiant_dom_cssom_host_prototype(Item object) {
    return ItemNull;
}

static Item radiant_dom_call_foreign_window_global_method(Item object,
                                                          void* foreign_doc,
                                                          Item method_name,
                                                          Item* args,
                                                          int argc) {
    if (!foreign_doc || !js_doc_has_browsing_context(foreign_doc)) return ItemNull;
    Item fn = js_get_global_property(method_name);
    if (get_type_id(fn) != LMD_TYPE_FUNC) return ItemNull;

    Item global = js_get_global_this();
    Item window_key = (Item){.item = s2it(heap_create_name("window"))};
    Item old_window = js_property_get(global, window_key);

    void* prev_doc = js_dom_swap_active_document(foreign_doc);
    js_property_set(global, window_key, object);
    Item result = js_call_function(fn, object, args, argc);
    js_property_set(global, window_key, old_window);
    js_dom_restore_active_document(prev_doc);
    return result;
}

static Item radiant_dom_foreign_get_computed_style(Item elem_item, Item pseudo_item) {
    (void)elem_item; (void)pseudo_item;
    return ItemNull;
}

extern "C" int radiant_dom_foreign_document_get_property(Item object, Item key, Item* out) {
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
        if (radiant_dom_key_equals(key, "getSelection", 12)) {
            *out = js_dom_get_selection_function_for_document(foreign_doc);
            return 1;
        }
        if (radiant_dom_key_equals(key, "getComputedStyle", 16)) {
            // iframe contentWindow is modeled as a document wrapper; do not let
            // the main-window getComputedStyle binding leak into foreign docs.
            *out = js_new_function((void*)radiant_dom_foreign_get_computed_style, 2);
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

extern "C" int radiant_dom_foreign_document_set_property(Item object,
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

extern "C" int radiant_dom_foreign_document_method(Item object,
                                                   Item method_name,
                                                   Item* args,
                                                   int argc,
                                                   Item* out) {
    if (!out) return 0;
    void* foreign_doc = js_get_foreign_doc(object);
    if (!foreign_doc) return 0;

    // run document methods against the foreign doc first, then fall back to
    // window-global methods for iframe contentWindow compatibility.
    if (js_doc_has_browsing_context(foreign_doc) &&
        radiant_dom_key_equals(method_name, "getComputedStyle", 16)) {
        *out = ItemNull;
        return 1;
    }
    void* prev = js_dom_swap_active_document(foreign_doc);
    Item result = js_document_proxy_method(method_name, args, argc);
    js_dom_restore_active_document(prev);
    if (result.item == ItemNull.item && !js_check_exception()) {
        Item fallback = radiant_dom_call_foreign_window_global_method(
            object, foreign_doc, method_name, args, argc);
        if (fallback.item != ItemNull.item || js_check_exception()) {
            *out = fallback;
            return 1;
        }
    }
    *out = result;
    return 1;
}

extern "C" int radiant_dom_document_host_get_property(Item object, Item key, Item* out) {
    if (!out) return 0;
    if (js_get_foreign_doc(object)) {
        return radiant_dom_foreign_document_get_property(object, key, out);
    }
    *out = js_document_proxy_get_property(key);
    return 1;
}

extern "C" int radiant_dom_document_host_set_property(Item object,
                                                      Item key,
                                                      Item value,
                                                      Item* out) {
    if (!out) return 0;
    if (js_get_foreign_doc(object)) {
        return radiant_dom_foreign_document_set_property(object, key, value, out);
    }
    *out = js_document_proxy_set_property(key, value);
    return 1;
}

extern "C" int radiant_dom_document_host_call_method(Item object,
                                                     Item method_name,
                                                     Item* args,
                                                     int argc,
                                                     Item* out) {
    if (!out) return 0;
    if (js_get_foreign_doc(object)) {
        return radiant_dom_foreign_document_method(object, method_name, args, argc, out);
    }
    *out = js_document_proxy_method(method_name, args, argc);
    return 1;
}

extern "C" int radiant_dom_document_host_has_property(Item object, Item key, Item* out) {
    if (!out) return 0;
    Item value = ItemNull;
    if (!radiant_dom_document_host_get_property(object, key, &value)) return 0;
    *out = (Item){.item = b2it(value.item != ItemNull.item && value.item != ITEM_JS_UNDEFINED)};
    return 1;
}

extern "C" int radiant_dom_document_host_delete_property(Item object, Item key, Item* out) {
    (void)object; (void)key;
    if (!out) return 0;
    *out = (Item){.item = b2it(true)};
    return 1;
}

extern "C" int radiant_dom_document_host_own_property_descriptor(Item object, Item key, Item* out) {
    (void)object; (void)key;
    if (!out) return 0;
    *out = radiant_dom_undefined_item();
    return 1;
}

extern "C" int radiant_dom_document_host_own_property_names(Item object, Item* out) {
    (void)object;
    if (!out) return 0;
    *out = js_array_new(0);
    return 1;
}

extern "C" Item radiant_dom_document_host_prototype(Item object) {
    (void)object;
    return ItemNull;
}

static DomElement* radiant_dom_document_child_by_tag(DomDocument* doc, const char* tag) {
    if (!doc || !doc->root || !tag) return nullptr;
    DomNode* child = doc->root->first_child;
    while (child) {
        if (child->is_element()) {
            DomElement* elem = child->as_element();
            if (elem->tag_name && strcasecmp(elem->tag_name, tag) == 0) {
                return elem;
            }
        }
        child = child->next_sibling;
    }
    return nullptr;
}

static void radiant_dom_collect_text_content(DomNode* node, StrBuf* sb) {
    if (!node || !sb || radiant_dom_is_generated_pseudo_node(node)) return;
    if (node->is_text()) {
        DomText* text = node->as_text();
        if (text->text && text->length > 0) {
            strbuf_append_str_n(sb, text->text, text->length);
        }
        return;
    }
    if (node->is_element()) {
        DomNode* child = radiant_dom_first_script_visible_child(node->as_element());
        while (child) {
            radiant_dom_collect_text_content(child, sb);
            child = radiant_dom_next_script_visible_sibling(child);
        }
    }
}

static Item radiant_dom_document_title_item(DomDocument* doc) {
    DomElement* head = radiant_dom_document_child_by_tag(doc, "head");
    DomNode* child = head ? head->first_child : nullptr;
    while (child) {
        if (child->is_element()) {
            DomElement* elem = child->as_element();
            if (elem->tag_name && strcasecmp(elem->tag_name, "title") == 0) {
                StrBuf* sb = strbuf_new_cap(64);
                if (!sb) return radiant_dom_string_item("");
                radiant_dom_collect_text_content((DomNode*)elem, sb);
                Item result = radiant_dom_string_item(sb->str ? sb->str : "");
                strbuf_free(sb);
                return result;
            }
        }
        child = child->next_sibling;
    }
    return radiant_dom_string_item("");
}

static const char* radiant_dom_url_component(DomDocument* doc, const char* prop) {
    if (!doc || !doc->url || !prop) return "";
    if (strcmp(prop, "URL") == 0 || strcmp(prop, "href") == 0) {
        const char* value = url_get_href(doc->url);
        return value ? value : "";
    }
    if (strcmp(prop, "protocol") == 0) {
        const char* value = url_get_protocol(doc->url);
        return value ? value : "";
    }
    if (strcmp(prop, "hostname") == 0) {
        const char* value = url_get_hostname(doc->url);
        return value ? value : "";
    }
    if (strcmp(prop, "port") == 0) {
        const char* value = url_get_port(doc->url);
        return value ? value : "";
    }
    if (strcmp(prop, "pathname") == 0) {
        const char* value = url_get_pathname(doc->url);
        return value ? value : "";
    }
    if (strcmp(prop, "search") == 0) {
        const char* value = url_get_search(doc->url);
        return value ? value : "";
    }
    if (strcmp(prop, "hash") == 0) {
        const char* value = url_get_hash(doc->url);
        return value ? value : "";
    }
    if (strcmp(prop, "host") == 0) {
        const char* value = url_get_host(doc->url);
        return value ? value : "";
    }
    if (strcmp(prop, "origin") == 0) {
        const char* value = url_get_origin(doc->url);
        return value ? value : "";
    }
    return "";
}

static Item radiant_dom_document_child_nodes_item(DomDocument* doc) {
    void* stub_v = js_dom_get_or_create_doc_node((void*)doc);
    if (!stub_v) return ItemNull;
    DomElement* stub = (DomElement*)stub_v;
    Item arr_item = radiant_dom_array_item();
    DomNode* child = stub->first_child;
    while (child) {
        array_push(arr_item.array, radiant_dom_node_item(child));
        child = child->next_sibling;
    }
    return arr_item;
}

static Item radiant_dom_document_doctype_item(DomDocument* doc) {
    void* stub_v = js_dom_get_or_create_doc_node((void*)doc);
    if (!stub_v) return ItemNull;
    DomElement* stub = (DomElement*)stub_v;
    DomNode* first = stub->first_child;
    return (first && first->is_comment()) ? radiant_dom_node_item(first) : ItemNull;
}

extern "C" int radiant_dom_document_get_property(Item prop_name, Item* out) {
    const char* prop = fn_to_cstr(prop_name);
    if (!prop || !out) return 0;

    DomDocument* doc = (DomDocument*)js_dom_get_document();
    if (!doc) {
        *out = ItemNull;
        return 1;
    }

    if (strcmp(prop, "documentElement") == 0) {
        *out = doc->root ? radiant_dom_node_item((DomNode*)doc->root) : ItemNull;
        return 1;
    }
    if (strcmp(prop, "body") == 0) {
        DomElement* body = radiant_dom_document_child_by_tag(doc, "body");
        *out = body ? radiant_dom_node_item((DomNode*)body) : ItemNull;
        return 1;
    }
    if (strcmp(prop, "head") == 0) {
        DomElement* head = radiant_dom_document_child_by_tag(doc, "head");
        *out = head ? radiant_dom_node_item((DomNode*)head) : ItemNull;
        return 1;
    }
    if (strcmp(prop, "title") == 0) {
        *out = radiant_dom_document_title_item(doc);
        return 1;
    }
    if (strcmp(prop, "URL") == 0 || strcmp(prop, "href") == 0 ||
        strcmp(prop, "protocol") == 0 || strcmp(prop, "hostname") == 0 ||
        strcmp(prop, "port") == 0 || strcmp(prop, "pathname") == 0 ||
        strcmp(prop, "search") == 0 || strcmp(prop, "hash") == 0 ||
        strcmp(prop, "host") == 0 || strcmp(prop, "origin") == 0) {
        *out = radiant_dom_string_item(radiant_dom_url_component(doc, prop));
        return 1;
    }
    if (strcmp(prop, "location") == 0 || strcmp(prop, "document") == 0) {
        *out = js_dom_document_proxy_for_doc_bridge((void*)doc);
        return 1;
    }
    if (strcmp(prop, "readyState") == 0) {
        *out = radiant_dom_string_item(doc->js_ready_state ? doc->js_ready_state : "complete");
        return 1;
    }
    if (strcmp(prop, "fonts") == 0) {
        *out = js_dom_document_fonts_bridge();
        return 1;
    }
    if (strcmp(prop, "compatMode") == 0) {
        *out = radiant_dom_string_item("CSS1Compat");
        return 1;
    }
    if (strcmp(prop, "characterSet") == 0 || strcmp(prop, "charset") == 0) {
        *out = radiant_dom_string_item("UTF-8");
        return 1;
    }
    if (strcmp(prop, "contentType") == 0) {
        *out = radiant_dom_string_item("text/html");
        return 1;
    }
    if (strcmp(prop, "nodeType") == 0) {
        *out = radiant_dom_int_item(9);
        return 1;
    }
    if (strcmp(prop, "nodeName") == 0) {
        *out = radiant_dom_string_item("#document");
        return 1;
    }
    if (strcmp(prop, "ownerDocument") == 0) {
        *out = ItemNull;
        return 1;
    }
    if (strcmp(prop, "childNodes") == 0) {
        *out = radiant_dom_document_child_nodes_item(doc);
        return 1;
    }
    if (strcmp(prop, "doctype") == 0) {
        *out = radiant_dom_document_doctype_item(doc);
        return 1;
    }
    if (strcmp(prop, "styleSheets") == 0) {
        *out = js_dom_document_stylesheets_bridge();
        return 1;
    }
    if (strcmp(prop, "defaultView") == 0) {
        *out = js_dom_document_default_view_bridge((void*)doc);
        return 1;
    }
    if (strcmp(prop, "implementation") == 0) {
        *out = js_dom_document_implementation_bridge();
        return 1;
    }
    if (strcmp(prop, "designMode") == 0) {
        *out = js_dom_document_design_mode_bridge();
        return 1;
    }
    if (strcmp(prop, "activeElement") == 0) {
        *out = js_dom_document_active_element_bridge((void*)doc);
        return 1;
    }
    if (strcmp(prop, "forms") == 0) {
        *out = js_dom_live_document_forms_bridge((void*)doc);
        return 1;
    }
    if (strcmp(prop, "getSelection") == 0) {
        *out = js_dom_get_selection_function_for_document((void*)doc);
        return 1;
    }

    return 0;
}

extern "C" int radiant_dom_document_method(Item method_name, Item* args, int argc, Item* out) {
    const char* method = fn_to_cstr(method_name);
    if (!method || !out) return 0;

    DomDocument* doc = (DomDocument*)js_dom_get_document();
    DomElement* root = doc ? doc->root : nullptr;

    if (strcmp(method, "assign") == 0 ||
        strcmp(method, "replace") == 0 ||
        strcmp(method, "reload") == 0) {
        *out = js_dom_location_method_bridge((void*)doc, method_name, args, argc);
        return 1;
    }

    if (strcmp(method, "focus") == 0 || strcmp(method, "blur") == 0) {
        *out = radiant_dom_undefined_item();
        return 1;
    }

    if (strcmp(method, "open") == 0) {
        *out = js_dom_document_open_bridge((void*)doc);
        return 1;
    }

    if (strcmp(method, "close") == 0) {
        *out = radiant_dom_undefined_item();
        return 1;
    }

    if (strcmp(method, "write") == 0 || strcmp(method, "writeln") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return 1;
        }
        *out = js_dom_document_write_bridge((void*)doc, args[0]);
        return 1;
    }

    if (strcmp(method, "elementFromPoint") == 0) {
        Item x_arg = argc >= 1 ? args[0] : radiant_dom_int_item(0);
        Item y_arg = argc >= 2 ? args[1] : radiant_dom_int_item(0);
        *out = js_dom_document_element_from_point_bridge((void*)doc, x_arg, y_arg);
        return 1;
    }

    if (strcmp(method, "execCommand") == 0 ||
        strcmp(method, "queryCommandSupported") == 0 ||
        strcmp(method, "queryCommandEnabled") == 0 ||
        strcmp(method, "queryCommandIndeterm") == 0 ||
        strcmp(method, "queryCommandState") == 0) {
        *out = (Item){.item = b2it(0)};
        return 1;
    }

    if (strcmp(method, "queryCommandValue") == 0) {
        *out = radiant_dom_string_item("");
        return 1;
    }

    if (strcmp(method, "createRange") == 0) {
        *out = js_dom_create_range();
        return 1;
    }

    if (strcmp(method, "getSelection") == 0) {
        *out = js_doc_has_browsing_context((void*)doc) ? js_dom_get_selection() : ItemNull;
        return 1;
    }

    if (strcmp(method, "getElementById") == 0) {
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

    if (strcmp(method, "getElementsByClassName") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return 1;
        }
        *out = js_dom_live_document_get_elements_by_class_name_bridge((void*)doc, args[0]);
        return 1;
    }

    if (strcmp(method, "getElementsByTagName") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return 1;
        }
        *out = js_dom_live_document_get_elements_by_tag_name_bridge((void*)doc, args[0]);
        return 1;
    }

    if (strcmp(method, "getElementsByName") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return 1;
        }
        *out = js_dom_live_document_get_elements_by_name_bridge((void*)doc, args[0]);
        return 1;
    }

    if (strcmp(method, "querySelector") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return 1;
        }
        const char* sel_text = fn_to_cstr(args[0]);
        if (!sel_text || !doc || !doc->pool) {
            *out = ItemNull;
            return 1;
        }
        CssSelectorGroup* selector_group = radiant_dom_parse_css_selector_group(sel_text, doc->pool);
        if (!selector_group) {
            Item err_name = (Item){.item = s2it(heap_create_name("SyntaxError"))};
            Item err_msg = (Item){.item = s2it(heap_create_name("is not a valid selector"))};
            js_throw_value(js_new_error_with_name(err_name, err_msg));
            *out = ItemNull;
            return 1;
        }
        SelectorMatcher* matcher = selector_matcher_create(doc->pool);
        DomElement* found = radiant_dom_selector_group_find_first(matcher, selector_group, root);
        *out = radiant_dom_node_item((DomNode*)found);
        return 1;
    }

    if (strcmp(method, "querySelectorAll") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return 1;
        }
        const char* sel_text = fn_to_cstr(args[0]);
        if (!sel_text || !doc || !doc->pool) {
            *out = radiant_dom_empty_array_item();
            return 1;
        }
        CssSelectorGroup* selector_group = radiant_dom_parse_css_selector_group(sel_text, doc->pool);
        if (!selector_group) {
            *out = radiant_dom_empty_array_item();
            return 1;
        }
        SelectorMatcher* matcher = selector_matcher_create(doc->pool);
        ArrayList* results = arraylist_new(16);
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        if (results) {
            radiant_dom_selector_group_collect_all(matcher, selector_group, root, results);
            for (int i = 0; i < results->length; i++) {
                array_push(arr, radiant_dom_node_item((DomNode*)results->data[i]));
            }
            arraylist_free(results);
        }
        *out = (Item){.array = arr};
        return 1;
    }

    if (strcmp(method, "createElement") == 0) {
        if (argc < 1 || !doc || !doc->input) {
            *out = ItemNull;
            return 1;
        }
        const char* tag = fn_to_cstr(args[0]);
        if (!tag) {
            *out = ItemNull;
            return 1;
        }
        MarkBuilder builder(doc->input);
        Item elem_item = builder.element(tag).final();
        DomElement* elem = dom_element_create(doc, tag, elem_item.element);
        *out = radiant_dom_node_item((DomNode*)elem);
        return 1;
    }

    if (strcmp(method, "createElementNS") == 0) {
        if (argc < 2 || !doc || !doc->input) {
            *out = ItemNull;
            return 1;
        }
        const char* ns = fn_to_cstr(args[0]);
        const char* tag = fn_to_cstr(args[1]);
        if (!tag) {
            *out = ItemNull;
            return 1;
        }
        MarkBuilder builder(doc->input);
        Item elem_item = builder.element(tag).final();
        DomElement* elem = dom_element_create(doc, tag, elem_item.element);
        if (elem && ns && ns[0] != '\0') {
            // namespace reflection is stored as an internal attribute, hidden from scripts.
            dom_element_set_attribute(elem, "__lambda_ns_uri", ns);
        }
        *out = radiant_dom_node_item((DomNode*)elem);
        return 1;
    }

    if (strcmp(method, "createTextNode") == 0) {
        if (argc < 1 || !doc) {
            *out = ItemNull;
            return 1;
        }
        const char* text = fn_to_cstr(args[0]);
        if (!text) {
            *out = ItemNull;
            return 1;
        }
        String* str = radiant_dom_document_string(doc, text, strlen(text));
        DomText* text_node = dom_text_create_detached(str, doc);
        *out = radiant_dom_node_item((DomNode*)text_node);
        return 1;
    }

    if (strcmp(method, "createDocumentFragment") == 0) {
        if (!doc || !doc->input) {
            *out = ItemNull;
            return 1;
        }
        MarkBuilder builder(doc->input);
        Item frag_item = builder.element("#document-fragment").final();
        DomElement* frag = dom_element_create(doc, "#document-fragment", frag_item.element);
        *out = radiant_dom_node_item((DomNode*)frag);
        return 1;
    }

    if (strcmp(method, "createComment") == 0) {
        if (!doc || !doc->input) {
            *out = ItemNull;
            return 1;
        }
        const char* text = (argc >= 1) ? fn_to_cstr(args[0]) : "";
        if (!text) text = "";
        MarkBuilder builder(doc->input);
        Item comment_item = builder.element("!--").text(text).final();
        DomComment* comment = dom_comment_create_detached(comment_item.element, doc);
        *out = radiant_dom_node_item((DomNode*)comment);
        return 1;
    }

    if (strcmp(method, "createProcessingInstruction") == 0) {
        if (!doc || !doc->input) {
            *out = ItemNull;
            return 1;
        }
        const char* target = (argc >= 1) ? fn_to_cstr(args[0]) : "";
        const char* data = (argc >= 2) ? fn_to_cstr(args[1]) : "";
        if (!target) target = "";
        if (!data) data = "";
        MarkBuilder builder(doc->input);
        Item pi_item = builder.element("?").text(data).final();
        DomElement* pi = dom_element_create(doc, target, pi_item.element);
        *out = radiant_dom_node_item((DomNode*)pi);
        return 1;
    }

    if (strcmp(method, "importNode") == 0) {
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
        Item clone_method = (Item){.item = s2it(heap_create_name("cloneNode"))};
        *out = radiant_dom_element_method(source_item, clone_method, &deep_arg, 1);
        return 1;
    }

    if (strcmp(method, "normalize") == 0) {
        if (root) {
            Item root_item = radiant_dom_node_item((DomNode*)root);
            Item normalize_method = (Item){.item = s2it(heap_create_name("normalize"))};
            *out = radiant_dom_element_method(root_item, normalize_method, nullptr, 0);
        } else {
            *out = ItemNull;
        }
        return 1;
    }

    if (strcmp(method, "adoptNode") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return 1;
        }
        *out = js_dom_adopt_node_bridge(args[0]);
        return 1;
    }

    if (strcmp(method, "appendChild") == 0) {
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
    if (strcmp(method, "addEventListener") == 0) {
        // document EventTarget storage is keyed by the singleton document wrapper.
        *out = argc >= 2
            ? js_dom_add_event_listener_bridge(doc_item, args[0], args[1],
                argc >= 3 ? args[2] : ItemNull)
            : radiant_dom_undefined_item();
        return 1;
    }

    if (strcmp(method, "removeEventListener") == 0) {
        // document EventTarget storage is keyed by the singleton document wrapper.
        *out = argc >= 2
            ? js_dom_remove_event_listener_bridge(doc_item, args[0], args[1],
                argc >= 3 ? args[2] : ItemNull)
            : radiant_dom_undefined_item();
        return 1;
    }

    if (strcmp(method, "dispatchEvent") == 0) {
        // dispatch must use the same wrapper identity listeners were registered with.
        *out = argc >= 1
            ? js_dom_dispatch_event_bridge(doc_item, args[0])
            : (Item){.item = b2it(0)};
        return 1;
    }

    return 0;
}

extern "C" Item radiant_dom_window_add_event_listener(Item type, Item callback, Item opts) {
    // window EventTarget storage must key on the canonical global object.
    return js_dom_add_event_listener_bridge(js_get_global_this(), type, callback, opts);
}

extern "C" Item radiant_dom_window_remove_event_listener(Item type, Item callback, Item opts) {
    // window EventTarget storage must key on the canonical global object.
    return js_dom_remove_event_listener_bridge(js_get_global_this(), type, callback, opts);
}

extern "C" Item radiant_dom_window_dispatch_event(Item event_item) {
    // dispatch must use the same global-object key that listener registration uses.
    return js_dom_dispatch_event_bridge(js_get_global_this(), event_item);
}

extern "C" int radiant_dom_style_method(Item elem_item, Item method_name, Item* args, int argc, Item* out) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element_impl(elem_item);
    if (!elem || !out) return 0;

    const char* method = fn_to_cstr(method_name);
    if (!method) return 0;

    if (strcmp(method, "setProperty") == 0) {
        // CSS rule style declarations are not DOM elements and stay on CSSOM fallback.
        *out = argc >= 2
            ? js_dom_style_set_property_bridge((void*)elem, args[0], args[1],
                argc >= 3 ? args[2] : radiant_dom_undefined_item(), argc >= 3)
            : ItemNull;
        return 1;
    }

    if (strcmp(method, "removeProperty") == 0) {
        // CSS rule style declarations are not DOM elements and stay on CSSOM fallback.
        *out = argc >= 1
            ? js_dom_style_remove_property_bridge((void*)elem, args[0])
            : radiant_dom_string_item("");
        return 1;
    }

    return 0;
}

extern "C" int radiant_dom_cssom_method(Item obj, Item method_name, Item* args, int argc, Item* out) {
    if (!out) return 0;

    if (js_is_css_namespace(obj)) {
        // CSSOM parsing and mutation internals remain in js_cssom.cpp.
        *out = js_css_namespace_method(obj, method_name, args, argc);
        return 1;
    }

    if (js_is_stylesheet(obj)) {
        // CSSOM parsing and mutation internals remain in js_cssom.cpp.
        *out = js_cssom_stylesheet_method(obj, method_name, args, argc);
        return 1;
    }

    if (js_is_rule_style_decl(obj)) {
        // CSSOM parsing and mutation internals remain in js_cssom.cpp.
        *out = js_cssom_rule_decl_method(obj, method_name, args, argc);
        return 1;
    }

    return 0;
}
