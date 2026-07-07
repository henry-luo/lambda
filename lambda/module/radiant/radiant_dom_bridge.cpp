#include "../../lambda-data.hpp"
#include "../../input/css/dom_node.hpp"
#include "../../input/css/dom_element.hpp"
#include "../../input/css/css_tokenizer.hpp"
#include "../../input/css/selector_matcher.hpp"
#include "../../../radiant/form_control.hpp"
#include "../../../radiant/text_control.hpp"
#include "../../../lib/mem.h"
#include "../../../lib/strbuf.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void heap_register_gc_root(uint64_t* slot);
extern "C" void heap_unregister_gc_root(uint64_t* slot);

extern "C" void* js_dom_get_document(void);
extern "C" Item js_get_document_object_value(void);
extern "C" Item js_get_global_this(void);
extern "C" Item js_dom_create_wrapper_impl(void* dom_elem);
extern "C" void* js_dom_unwrap_element_impl(Item item);
extern "C" bool js_is_dom_node_impl(Item item);
extern "C" Item js_new_error_with_name(Item error_name, Item message);
extern "C" void js_throw_value(Item error);
extern "C" bool js_is_css_namespace(Item item);
extern "C" bool js_is_stylesheet(Item item);
extern "C" bool js_is_rule_style_decl(Item item);
extern "C" Item js_dom_get_property_impl(Item elem_item, Item prop_name);
extern "C" Item js_dom_set_property_impl(Item elem_item, Item prop_name, Item value);
extern "C" Item js_dom_element_method_impl(Item elem_item, Item method_name, Item* args, int argc);
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
extern "C" Item js_dom_focus_method_bridge(void* elem, bool focus);
extern "C" Item js_dom_click_method_bridge(Item elem_item);
extern "C" Item js_dom_add_event_listener_bridge(Item target_item, Item type, Item callback, Item opts);
extern "C" Item js_dom_remove_event_listener_bridge(Item target_item, Item type, Item callback, Item opts);
extern "C" Item js_dom_dispatch_event_bridge(Item target_item, Item event_item);
extern "C" Item js_dom_get_bounding_client_rect_bridge(void* elem);
extern "C" Item js_dom_get_client_rects_bridge(void* elem);
extern "C" Item js_dom_scroll_into_view_bridge(void* elem);
extern "C" Item js_dom_scroll_method_bridge(Item elem_item, Item method_name, Item* args, int argc);
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
extern "C" Item js_dom_normalize_bridge(void* elem);
extern "C" Item js_dom_clone_node_bridge(void* elem, Item deep, bool has_deep);
extern "C" Item js_dom_replace_child_bridge(void* parent, Item new_child, Item old_child);
extern "C" Item js_dom_replace_with_bridge(void* node, Item* args, int argc);
extern "C" Item js_dom_insert_adjacent_element_bridge(void* elem, Item position, Item new_node);
extern "C" Item js_dom_insert_adjacent_html_bridge(void* elem, Item position, Item html);
extern "C" Item js_dom_append_variadic_bridge(void* elem, Item* args, int argc);
extern "C" Item js_dom_prepend_variadic_bridge(void* elem, Item* args, int argc);
extern "C" void js_dom_notify_mutation(DomJsMutationKind kind, void* target, void* parent);
extern "C" Item radiant_dom_wrap_node(void* dom_elem);
extern "C" Item js_new_object(void);
extern "C" Item js_property_set(Item object, Item key, Item value);

static const int RADIANT_DOM_WRAPPER_CACHE_CHUNK_SIZE = 4096;

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

static Item radiant_dom_children_item(DomElement* elem) {
    Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    arr->type_id = LMD_TYPE_ARRAY;
    arr->items = nullptr;
    arr->length = 0;
    arr->capacity = 0;
    DomNode* child = radiant_dom_first_script_visible_child(elem);
    while (child) {
        if (child->is_element()) {
            array_push(arr, radiant_dom_node_item(child));
        }
        child = radiant_dom_next_script_visible_sibling(child);
    }
    return (Item){.array = arr};
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

static void radiant_dom_find_by_class(DomElement* root, const char* cls, Array* arr) {
    if (!root || !cls || !arr) return;
    for (int i = 0; i < root->class_count; i++) {
        if (root->class_names[i] && strcmp(root->class_names[i], cls) == 0) {
            array_push(arr, radiant_dom_node_item((DomNode*)root));
            break;
        }
    }
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            radiant_dom_find_by_class(child->as_element(), cls, arr);
        }
        child = child->next_sibling;
    }
}

static void radiant_dom_find_by_tag(DomElement* root, const char* tag, Array* arr) {
    if (!root || !tag || !arr) return;
    if (root->tag_name && ((tag[0] == '*' && tag[1] == '\0') ||
            strcasecmp(root->tag_name, tag) == 0)) {
        array_push(arr, radiant_dom_node_item((DomNode*)root));
    }
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            radiant_dom_find_by_tag(child->as_element(), tag, arr);
        }
        child = child->next_sibling;
    }
}

static void radiant_dom_find_by_name(DomElement* root, const char* name, Array* arr) {
    if (!root || !name || !arr) return;
    const char* attr = dom_element_get_attribute(root, "name");
    if (attr && strcmp(attr, name) == 0) {
        array_push(arr, radiant_dom_node_item((DomNode*)root));
    }
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            radiant_dom_find_by_name(child->as_element(), name, arr);
        }
        child = child->next_sibling;
    }
}

static void radiant_dom_find_descendants_by_tag(DomElement* root, const char* tag, Array* arr) {
    if (!root || !tag || !arr) return;
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            radiant_dom_find_by_tag(child->as_element(), tag, arr);
        }
        child = child->next_sibling;
    }
}

static void radiant_dom_find_descendants_by_class(DomElement* root, const char* cls, Array* arr) {
    if (!root || !cls || !arr) return;
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            radiant_dom_find_by_class(child->as_element(), cls, arr);
        }
        child = child->next_sibling;
    }
}

static CssSelector* radiant_dom_parse_css_selector(const char* sel_text, Pool* pool) {
    if (!sel_text || !pool) return nullptr;
    size_t sel_len = strlen(sel_text);
    if (sel_len == 0) return nullptr;
    size_t token_count = 0;
    CssToken* tokens = css_tokenize(sel_text, sel_len, pool, &token_count);
    if (!tokens || token_count == 0) return nullptr;
    int pos = 0;
    return css_parse_selector_with_combinators(tokens, &pos, (int)token_count, pool);
}

static Item radiant_dom_lookup_wrapper(DomNode* node) {
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
    if (!entry || entry->item == 0) return;
    DomNode* node = entry->node;
    if (node && node->is_element()) {
        form_control_release_prop(node->as_element());
    }
    heap_unregister_gc_root(&entry->item);
    entry->node = nullptr;
    entry->owner_doc = nullptr;
    entry->item = 0;
}

extern "C" void radiant_dom_invalidate_document(DomDocument* doc) {
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
    if (!dom_elem) return ItemNull;

    DomNode* node = (DomNode*)dom_elem;
    Item cached = radiant_dom_lookup_wrapper(node);
    if (cached.item != ITEM_NULL) return cached;

    Item wrapper = js_dom_create_wrapper_impl(dom_elem);
    if (js_is_dom_node_impl(wrapper)) {
        radiant_dom_cache_wrapper(node, wrapper);
    }
    return wrapper;
}

extern "C" void* radiant_dom_unwrap_node(Item item) {
    return js_dom_unwrap_element_impl(item);
}

extern "C" bool radiant_dom_is_node(Item item) {
    return js_is_dom_node_impl(item);
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
    if (strcmp(prop, "childElementCount") == 0) {
        *out = radiant_dom_int_item(radiant_dom_script_visible_element_child_count(elem));
        return true;
    }
    if (strcmp(prop, "length") == 0) {
        *out = radiant_dom_int_item(radiant_dom_script_visible_element_child_count(elem));
        return true;
    }
    if (strcmp(prop, "children") == 0) {
        *out = radiant_dom_children_item(elem);
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
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        DomNode* child = radiant_dom_first_script_visible_child(elem);
        while (child) {
            array_push(arr, radiant_dom_node_item(child));
            child = radiant_dom_next_script_visible_sibling(child);
        }
        *out = (Item){.array = arr};
        return true;
    }
    return false;
}

static bool radiant_dom_get_basic_property(Item elem_item, Item prop_name, Item* out) {
    if (!out) return false;
    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return false;

    DomNode* node = (DomNode*)radiant_dom_unwrap_node(elem_item);
    if (!node) return false;
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
        const char* tag = fn_to_cstr(args[0]);
        if (!tag) {
            *out = ItemNull;
            return true;
        }
        Item arr_item = radiant_dom_empty_array_item();
        radiant_dom_find_descendants_by_tag(elem, tag, arr_item.array);
        *out = arr_item;
        return true;
    }

    if (strcmp(method, "getElementsByClassName") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return true;
        }
        const char* cls = fn_to_cstr(args[0]);
        if (!cls) {
            *out = ItemNull;
            return true;
        }
        Item arr_item = radiant_dom_empty_array_item();
        radiant_dom_find_descendants_by_class(elem, cls, arr_item.array);
        *out = arr_item;
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
        CssSelector* selector = radiant_dom_parse_css_selector(sel_text, elem->doc->pool);
        if (!selector) {
            *out = ItemNull;
            return true;
        }
        // selector parsing and matching stay on the document pool so returned
        // wrappers are the only GC-managed values created by this read-only path.
        SelectorMatcher* matcher = selector_matcher_create(elem->doc->pool);
        DomElement* found = selector_matcher_find_first(matcher, selector, elem);
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
        CssSelector* selector = radiant_dom_parse_css_selector(sel_text, elem->doc->pool);
        if (!selector) {
            *out = arr_item;
            return true;
        }
        SelectorMatcher* matcher = selector_matcher_create(elem->doc->pool);
        DomElement** results = nullptr;
        int count = 0;
        selector_matcher_find_all(matcher, selector, elem, &results, &count);
        for (int i = 0; i < count; i++) {
            array_push(arr_item.array, radiant_dom_node_item((DomNode*)results[i]));
        }
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
        CssSelector* selector = radiant_dom_parse_css_selector(sel_text, elem->doc->pool);
        if (!selector) {
            *out = (Item){.item = b2it(0)};
            return true;
        }
        SelectorMatcher* matcher = selector_matcher_create(elem->doc->pool);
        MatchResult result;
        bool matched = selector_matcher_matches(matcher, selector, elem, &result);
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
        CssSelector* selector = radiant_dom_parse_css_selector(sel_text, elem->doc->pool);
        if (!selector) {
            *out = ItemNull;
            return true;
        }
        SelectorMatcher* matcher = selector_matcher_create(elem->doc->pool);
        MatchResult result;
        DomElement* current = elem;
        while (current) {
            if (selector_matcher_matches(matcher, selector, current, &result)) {
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

    if (strcmp(method, "focus") == 0 || strcmp(method, "blur") == 0) {
        *out = js_dom_focus_method_bridge((void*)elem, strcmp(method, "focus") == 0);
        return true;
    }

    if (strcmp(method, "click") == 0) {
        *out = js_dom_click_method_bridge(elem_item);
        return true;
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
    Item result = ItemNull;
    if (radiant_dom_get_basic_property(elem_item, prop_name, &result)) {
        return result;
    }
    return js_dom_get_property_impl(elem_item, prop_name);
}

extern "C" Item radiant_dom_set_property(Item elem_item, Item prop_name, Item value) {
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

extern "C" int radiant_dom_document_method(Item method_name, Item* args, int argc, Item* out) {
    const char* method = fn_to_cstr(method_name);
    if (!method || !out) return 0;

    DomDocument* doc = (DomDocument*)js_dom_get_document();
    DomElement* root = doc ? doc->root : nullptr;

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
        const char* cls = fn_to_cstr(args[0]);
        if (!cls) {
            *out = ItemNull;
            return 1;
        }
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        radiant_dom_find_by_class(root, cls, arr);
        *out = (Item){.array = arr};
        return 1;
    }

    if (strcmp(method, "getElementsByTagName") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return 1;
        }
        const char* tag = fn_to_cstr(args[0]);
        if (!tag) {
            *out = ItemNull;
            return 1;
        }
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        radiant_dom_find_by_tag(root, tag, arr);
        *out = (Item){.array = arr};
        return 1;
    }

    if (strcmp(method, "getElementsByName") == 0) {
        if (argc < 1) {
            *out = ItemNull;
            return 1;
        }
        const char* name = fn_to_cstr(args[0]);
        if (!name) {
            *out = ItemNull;
            return 1;
        }
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        radiant_dom_find_by_name(root, name, arr);
        *out = (Item){.array = arr};
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
        CssSelector* selector = radiant_dom_parse_css_selector(sel_text, doc->pool);
        if (!selector) {
            Item err_name = (Item){.item = s2it(heap_create_name("SyntaxError"))};
            Item err_msg = (Item){.item = s2it(heap_create_name("is not a valid selector"))};
            js_throw_value(js_new_error_with_name(err_name, err_msg));
            *out = ItemNull;
            return 1;
        }
        SelectorMatcher* matcher = selector_matcher_create(doc->pool);
        DomElement* found = selector_matcher_find_first(matcher, selector, root);
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
        CssSelector* selector = radiant_dom_parse_css_selector(sel_text, doc->pool);
        if (!selector) {
            *out = radiant_dom_empty_array_item();
            return 1;
        }
        SelectorMatcher* matcher = selector_matcher_create(doc->pool);
        DomElement** results = nullptr;
        int count = 0;
        selector_matcher_find_all(matcher, selector, root, &results, &count);
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        for (int i = 0; i < count; i++) {
            array_push(arr, radiant_dom_node_item((DomNode*)results[i]));
        }
        *out = (Item){.array = arr};
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
