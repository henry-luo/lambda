#include "../../lambda-data.hpp"
#include "../../input/css/dom_node.hpp"
#include "../../input/css/dom_element.hpp"
#include "../../../radiant/form_control.hpp"
#include "../../../lib/mem.h"
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

extern "C" Item radiant_dom_get_property(Item elem_item, Item prop_name) {
    return js_dom_get_property_impl(elem_item, prop_name);
}

extern "C" Item radiant_dom_set_property(Item elem_item, Item prop_name, Item value) {
    return js_dom_set_property_impl(elem_item, prop_name, value);
}
