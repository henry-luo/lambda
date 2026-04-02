// render_map.cpp — Implementation of observer-based source→result mapping
// Phase 3 of Reactive UI: tracks which template invocations produced which
// result nodes, enabling targeted re-transformation when state/model changes.
#include "lambda-data.hpp"
#include "render_map.h"
#include "template_registry.h"
#include "../lib/log.h"
#include "../lib/hashmap.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// Global render map
// ============================================================================

static HashMap* s_render_map = NULL;
static bool s_owns_map = false;
static Item s_doc_root = {0};  // top-level element tree for parent fixup

// forward declarations
static bool replace_in_element_tree(Item node, Item old_child, Item new_child);

// ============================================================================
// Reverse map: result_node.item → RenderMapKey (source_item, template_ref)
// ============================================================================

typedef struct ReverseMapEntry {
    uint64_t result_item_bits;   // Item.item value of the result node (key)
    RenderMapKey key;            // source_item + template_ref
} ReverseMapEntry;

static HashMap* s_reverse_map = NULL;

static uint64_t reverse_map_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const ReverseMapEntry* entry = (const ReverseMapEntry*)item;
    return hashmap_murmur(&entry->result_item_bits, sizeof(uint64_t), seed0, seed1);
}

static int reverse_map_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const ReverseMapEntry* ea = (const ReverseMapEntry*)a;
    const ReverseMapEntry* eb = (const ReverseMapEntry*)b;
    if (ea->result_item_bits < eb->result_item_bits) return -1;
    if (ea->result_item_bits > eb->result_item_bits) return 1;
    return 0;
}

static HashMap* ensure_reverse_map(void) {
    if (!s_reverse_map) {
        s_reverse_map = hashmap_new(
            sizeof(ReverseMapEntry), 64,
            0xABCD1234, 0x5678FACE,
            reverse_map_hash, reverse_map_compare,
            NULL, NULL
        );
    }
    return s_reverse_map;
}

// ============================================================================
// Hash and compare for RenderMapKey
// ============================================================================

static uint64_t render_map_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const RenderMapEntry* entry = (const RenderMapEntry*)item;
    uint64_t h1 = hashmap_murmur(&entry->key.source_item, sizeof(Item), seed0, seed1);
    uint64_t h2 = hashmap_murmur(&entry->key.template_ref, sizeof(void*), seed0, seed1);
    return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL);
}

static int render_map_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const RenderMapEntry* ea = (const RenderMapEntry*)a;
    const RenderMapEntry* eb = (const RenderMapEntry*)b;
    if (ea->key.source_item.item != eb->key.source_item.item) {
        return ea->key.source_item.item < eb->key.source_item.item ? -1 : 1;
    }
    if (ea->key.template_ref != eb->key.template_ref) {
        return ea->key.template_ref < eb->key.template_ref ? -1 : 1;
    }
    return 0;
}

// ============================================================================
// Ensure map exists (lazy creation)
// ============================================================================

static HashMap* ensure_map(void) {
    if (!s_render_map) {
        s_render_map = hashmap_new(
            sizeof(RenderMapEntry), 64,
            0xFACE1234, 0x5678DEAD,
            render_map_hash, render_map_compare,
            NULL, NULL
        );
        s_owns_map = true;
    }
    return s_render_map;
}

// ============================================================================
// Public API
// ============================================================================

void render_map_init(void) {
    ensure_map();
    log_debug("render_map_init: render map initialized");
}

void render_map_destroy(void) {
    if (s_render_map && s_owns_map) {
        hashmap_free(s_render_map);
    }
    s_render_map = NULL;
    s_owns_map = false;
    if (s_reverse_map) {
        hashmap_free(s_reverse_map);
        s_reverse_map = NULL;
    }
}

void render_map_record(Item source_item, const char* template_ref,
                       Item result_node, Item parent_result, int child_index) {
    HashMap* map = ensure_map();
    RenderMapEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.key.source_item = source_item;
    entry.key.template_ref = template_ref;
    entry.result_node = result_node;
    entry.parent_result = parent_result;
    entry.child_index = child_index;
    entry.dirty = false;
    hashmap_set(map, &entry);

    // also record in reverse map: result_node → (source_item, template_ref)
    if (result_node.item) {
        HashMap* rmap = ensure_reverse_map();
        ReverseMapEntry rentry;
        memset(&rentry, 0, sizeof(rentry));
        rentry.result_item_bits = result_node.item;
        rentry.key.source_item = source_item;
        rentry.key.template_ref = template_ref;
        hashmap_set(rmap, &rentry);
    }

    log_info("render_map_record: tmpl=%s result=0x%llx reverse_map_count=%zu",
              template_ref ? template_ref : "(anon)",
              (unsigned long long)result_node.item,
              s_reverse_map ? hashmap_count(s_reverse_map) : 0);
}

void render_map_mark_dirty(Item source_item, const char* template_ref) {
    HashMap* map = ensure_map();
    RenderMapEntry query;
    memset(&query, 0, sizeof(query));
    query.key.source_item = source_item;
    query.key.template_ref = template_ref;
    RenderMapEntry* found = (RenderMapEntry*)hashmap_get(map, &query);
    if (found) {
        // hashmap_get returns const, but we need to mutate dirty flag
        // re-insert with dirty=true
        RenderMapEntry updated = *found;
        updated.dirty = true;
        hashmap_set(map, &updated);
        log_debug("render_map_mark_dirty: tmpl=%s marked dirty",
                  template_ref ? template_ref : "(anon)");
    } else {
        log_debug("render_map_mark_dirty: tmpl=%s not found in render map",
                  template_ref ? template_ref : "(anon)");
    }
}

bool render_map_has_dirty(void) {
    HashMap* map = ensure_map();
    size_t iter = 0;
    void* item;
    while (hashmap_iter(map, &iter, &item)) {
        const RenderMapEntry* entry = (const RenderMapEntry*)item;
        if (entry->dirty) return true;
    }
    return false;
}

Item render_map_get_result(Item source_item, const char* template_ref) {
    HashMap* map = ensure_map();
    RenderMapEntry query;
    memset(&query, 0, sizeof(query));
    query.key.source_item = source_item;
    query.key.template_ref = template_ref;
    const RenderMapEntry* found = (const RenderMapEntry*)hashmap_get(map, &query);
    return found ? found->result_node : ItemNull;
}

int render_map_retransform(void) {
    HashMap* map = ensure_map();
    if (!g_template_registry) {
        log_error("render_map_retransform: no template registry");
        return 0;
    }

    // collect dirty entries (iterate + re-execute)
    // we iterate the map, and for each dirty entry, re-execute the template
    int count = 0;
    size_t iter = 0;
    void* item;
    while (hashmap_iter(map, &iter, &item)) {
        RenderMapEntry* entry = (RenderMapEntry*)item;
        if (!entry->dirty) continue;

        // find the template by template_ref
        TemplateEntry* tmpl = NULL;
        for (TemplateEntry* e = g_template_registry->first; e; e = e->next) {
            if (e->template_ref == entry->key.template_ref) {
                tmpl = e;
                break;
            }
        }

        if (!tmpl || !tmpl->body_func) {
            log_error("render_map_retransform: no template found for ref=%s",
                      entry->key.template_ref ? entry->key.template_ref : "(null)");
            entry->dirty = false;
            continue;
        }

        // re-execute template body with the source item
        // NOTE: Do NOT call heap_gc_collect() here — the GC doesn't know about
        // s_doc_root or other static roots, so it would collect live elements.
        typedef Item (*template_body_fn)(Item);
        template_body_fn fn = (template_body_fn)tmpl->body_func;
        Item new_result = fn(entry->key.source_item);

        // update the entry
        Item old_result = entry->result_node;
        entry->result_node = new_result;
        entry->dirty = false;

        // update reverse map: remove old result, add new result
        if (s_reverse_map) {
            if (old_result.item) {
                ReverseMapEntry rquery;
                memset(&rquery, 0, sizeof(rquery));
                rquery.result_item_bits = old_result.item;
                hashmap_delete(s_reverse_map, &rquery);
            }
            if (new_result.item) {
                ReverseMapEntry rentry;
                memset(&rentry, 0, sizeof(rentry));
                rentry.result_item_bits = new_result.item;
                rentry.key = entry->key;
                hashmap_set(s_reverse_map, &rentry);
            }
        }

        // if we have a parent, replace the child in the parent's children
        if (get_type_id(entry->parent_result) != LMD_TYPE_NULL && entry->child_index >= 0) {
            TypeId parent_type = get_type_id(entry->parent_result);
            if (parent_type == LMD_TYPE_ELEMENT) {
                Element* parent_elmt = it2elmt(entry->parent_result);
                if (parent_elmt && entry->child_index < (int)parent_elmt->length) {
                    parent_elmt->items[entry->child_index] = new_result;
                }
            } else if (parent_type == LMD_TYPE_ARRAY) {
                Array* parent_arr = it2arr(entry->parent_result);
                if (parent_arr && entry->child_index < (int)parent_arr->length) {
                    parent_arr->items[entry->child_index] = new_result;
                }
            }
        } else if (s_doc_root.item && old_result.item != new_result.item) {
            // parent unknown — walk the doc root tree to find and replace
            replace_in_element_tree(s_doc_root, old_result, new_result);
        }

        // write back the updated entry to the map
        hashmap_set(map, entry);

        count++;
        log_debug("render_map_retransform: re-transformed tmpl=%s (entry %d)",
                  entry->key.template_ref ? entry->key.template_ref : "(anon)", count);
    }

    if (count > 0) {
        log_info("render_map_retransform: re-transformed %d dirty entries", count);
    }
    return count;
}

void render_map_reset(void) {
    if (s_render_map) {
        hashmap_clear(s_render_map, false);
    }
    if (s_reverse_map) {
        hashmap_clear(s_reverse_map, false);
    }
    log_debug("render_map_reset: all render map entries cleared");
}

struct hashmap* render_map_get_map(void) {
    return ensure_map();
}

void render_map_set_map(struct hashmap* map) {
    if (s_render_map && s_owns_map) {
        hashmap_free(s_render_map);
    }
    s_render_map = map;
    s_owns_map = false;
}

bool render_map_reverse_lookup(Item result_node, RenderMapLookup* out) {
    if (!result_node.item || !out) return false;
    HashMap* rmap = ensure_reverse_map();
    ReverseMapEntry query;
    memset(&query, 0, sizeof(query));
    query.result_item_bits = result_node.item;
    const ReverseMapEntry* found = (const ReverseMapEntry*)hashmap_get(rmap, &query);
    if (found) {
        out->source_item = found->key.source_item;
        out->template_ref = found->key.template_ref;
        return true;
    }
    return false;
}

void render_map_set_doc_root(Item root) {
    s_doc_root = root;
}

// Recursively search the element tree for old_child and replace with new_child.
// Returns true if replacement was made.
static bool replace_in_element_tree(Item node, Item old_child, Item new_child) {
    TypeId tid = get_type_id(node);
    if (tid == LMD_TYPE_ELEMENT) {
        Element* elmt = it2elmt(node);
        if (!elmt) return false;
        for (unsigned i = 0; i < elmt->length; i++) {
            if (elmt->items[i].item == old_child.item) {
                elmt->items[i] = new_child;
                return true;
            }
            if (replace_in_element_tree(elmt->items[i], old_child, new_child)) {
                return true;
            }
        }
    } else if (tid == LMD_TYPE_ARRAY) {
        Array* arr = it2arr(node);
        if (!arr) return false;
        for (unsigned i = 0; i < arr->length; i++) {
            if (arr->items[i].item == old_child.item) {
                arr->items[i] = new_child;
                return true;
            }
            if (replace_in_element_tree(arr->items[i], old_child, new_child)) {
                return true;
            }
        }
    }
    return false;
}
