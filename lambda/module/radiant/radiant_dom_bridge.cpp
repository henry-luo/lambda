#include "../../lambda-data.hpp"
#include "../../input/css/dom_node.hpp"
#include "../../input/css/dom_element.hpp"
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

extern "C" Item radiant_dom_get_property(Item elem_item, Item prop_name) {
    Item result = ItemNull;
    if (radiant_dom_get_basic_property(elem_item, prop_name, &result)) {
        return result;
    }
    return js_dom_get_property_impl(elem_item, prop_name);
}

extern "C" Item radiant_dom_set_property(Item elem_item, Item prop_name, Item value) {
    return js_dom_set_property_impl(elem_item, prop_name, value);
}
