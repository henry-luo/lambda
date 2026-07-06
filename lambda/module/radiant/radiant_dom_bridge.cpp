#include "../../lambda-data.hpp"
#include "../../input/css/dom_node.hpp"
#include "../../input/css/dom_element.hpp"
#include "../../input/css/css_tokenizer.hpp"
#include "../../input/css/selector_matcher.hpp"
#include "../../../radiant/form_control.hpp"
#include "../../../lib/mem.h"
#include "../../../lib/strbuf.h"
#include <ctype.h>
#include <string.h>

extern "C" void heap_register_gc_root(uint64_t* slot);
extern "C" void heap_unregister_gc_root(uint64_t* slot);

extern "C" void* js_dom_get_document(void);
extern "C" Item js_dom_create_wrapper_impl(void* dom_elem);
extern "C" void* js_dom_unwrap_element_impl(Item item);
extern "C" bool js_is_dom_node_impl(Item item);
extern "C" Item js_dom_get_property_impl(Item elem_item, Item prop_name);
extern "C" Item js_dom_set_property_impl(Item elem_item, Item prop_name, Item value);
extern "C" Item js_dom_element_method_impl(Item elem_item, Item method_name, Item* args, int argc);
extern "C" Item js_dom_owner_document_for_node(void* node);
extern "C" const char* js_dom_to_attribute_cstr(Item value);
extern "C" bool js_is_truthy(Item value);
extern "C" void js_dom_after_set_attribute(void* elem, const char* attr_name, const char* attr_value);
extern "C" void js_dom_after_remove_attribute(void* elem, const char* attr_name);
extern "C" void js_dom_after_toggle_attribute_remove(void* elem, const char* attr_name);
extern "C" Item js_dom_set_text_data_property(void* text, Item value);
extern "C" Item js_dom_text_replace_data_bridge(void* text, Item offset, Item count, Item data);
extern "C" Item js_dom_text_insert_data_bridge(void* text, Item offset, Item data);
extern "C" Item js_dom_text_append_data_bridge(void* text, Item data);
extern "C" Item js_dom_text_delete_data_bridge(void* text, Item offset, Item count);
extern "C" Item js_dom_text_substring_data_bridge(void* text, Item offset, Item count);
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
