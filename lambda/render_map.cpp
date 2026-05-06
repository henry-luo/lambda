// render_map.cpp — Implementation of observer-based source→result mapping
// Phase 3 of Reactive UI: tracks which template invocations produced which
// result nodes, enabling targeted re-transformation when state/model changes.
#include "lambda-data.hpp"
#include "render_map.h"
#include "template_registry.h"
#include "transpiler.hpp"
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

// R7 step 3c — source-document path tracking
static Item s_source_doc_root = {0};
static render_map_path_recorder_fn s_path_recorder = NULL;

// forward declarations
static Item find_parent_of(Item node, Item target, int* out_index, int depth = 0);
extern Item _map_get(TypeMap* map_type, void* map_data, char* key, bool* is_found);

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
    // R7 step 3c — clear stale source-doc-root from any prior runtime.
    // The item's pointer would otherwise dangle after the previous
    // runtime's heap was torn down, crashing the next apply()'s path walk.
    s_source_doc_root = (Item){0};
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
    // R7 step 3c — drop dangling source-doc-root before next runtime.
    s_source_doc_root = (Item){0};
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

    log_debug("render_map_record: tmpl=%s result=0x%llx reverse_map_count=%zu",
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

        // Save entry data BEFORE re-execution. fn() may call apply() which
        // calls render_map_record() → hashmap_set(), potentially resizing the
        // hashmap and invalidating the 'entry' pointer.
        RenderMapEntry saved = *entry;

        // find the template by template_ref
        TemplateEntry* tmpl = NULL;
        for (TemplateEntry* e = g_template_registry->first; e; e = e->next) {
            if (e->template_ref == saved.key.template_ref) {
                tmpl = e;
                break;
            }
        }

        if (!tmpl || !tmpl->body_func) {
            log_error("render_map_retransform: no template found for ref=%s",
                      saved.key.template_ref ? saved.key.template_ref : "(null)");
            entry->dirty = false;
            continue;
        }

        Item old_result = saved.result_node;

        // Find parent in element tree BEFORE fn() re-executes, because fn()
        // may trigger GC which reuses memory of old elements in the tree.
        Item tree_parent = saved.parent_result;
        int tree_child_index = saved.child_index;
        if (get_type_id(tree_parent) == LMD_TYPE_NULL && s_doc_root.item) {
            if (s_doc_root.item == old_result.item) {
                // old result IS the doc root — will be replaced directly
            } else {
                // Walk tree to find parent while tree is still valid
                tree_parent = find_parent_of(s_doc_root, old_result, &tree_child_index);
            }
        }

        // re-execute template body with the source item
        // NOTE: fn() may call apply() which modifies this hashmap — after this
        // call, 'entry' may be dangling. Use 'saved' for old values.
        typedef Item (*template_body_fn)(Item);
        template_body_fn fn = (template_body_fn)tmpl->body_func;
        Item new_result = fn(saved.key.source_item);

        // update reverse map
        if (s_reverse_map) {
            if (new_result.item) {
                ReverseMapEntry rentry;
                memset(&rentry, 0, sizeof(rentry));
                rentry.result_item_bits = new_result.item;
                rentry.key = saved.key;
                hashmap_set(s_reverse_map, &rentry);
            }
        }

        // Replace old result with new result in the element tree
        if (get_type_id(tree_parent) != LMD_TYPE_NULL && tree_child_index >= 0) {
            TypeId parent_type = get_type_id(tree_parent);
            if (parent_type == LMD_TYPE_ELEMENT) {
                Element* parent_elmt = it2elmt(tree_parent);
                if (parent_elmt && tree_child_index < (int)parent_elmt->length) {
                    parent_elmt->items[tree_child_index] = new_result;
                }
            } else if (parent_type == LMD_TYPE_ARRAY) {
                Array* parent_arr = it2arr(tree_parent);
                if (parent_arr && tree_child_index < (int)parent_arr->length) {
                    parent_arr->items[tree_child_index] = new_result;
                }
            }
        } else if (s_doc_root.item == old_result.item && old_result.item != new_result.item) {
            s_doc_root = new_result;
            log_debug("render_map_retransform: updated s_doc_root to new result 0x%llx",
                      (unsigned long long)new_result.item);
        }

        // write back the updated entry to the map (re-lookup since entry may be stale)
        RenderMapEntry updated = saved;
        updated.result_node = new_result;
        updated.dirty = false;
        hashmap_set(map, &updated);

        count++;
        log_debug("render_map_retransform: re-transformed tmpl=%s (entry %d)",
                  saved.key.template_ref ? saved.key.template_ref : "(anon)", count);
    }

    if (count > 0) {
        log_debug("render_map_retransform: re-transformed %d dirty entries", count);
    }
    return count;
}

int render_map_retransform_with_results(RetransformResult* out_results, int max_results) {
    HashMap* map = ensure_map();
    if (!g_template_registry) {
        log_error("render_map_retransform_with_results: no template registry");
        return 0;
    }

    int count = 0;
    size_t iter = 0;
    void* item;
    while (hashmap_iter(map, &iter, &item)) {
        RenderMapEntry* entry = (RenderMapEntry*)item;
        if (!entry->dirty) continue;

        // Save entry data BEFORE re-execution. fn() may call apply() which
        // calls render_map_record() → hashmap_set(), potentially resizing the
        // hashmap and invalidating the 'entry' pointer.
        RenderMapEntry saved = *entry;

        // find the template by template_ref
        TemplateEntry* tmpl = NULL;
        for (TemplateEntry* e = g_template_registry->first; e; e = e->next) {
            if (e->template_ref == saved.key.template_ref) {
                tmpl = e;
                break;
            }
        }

        if (!tmpl || !tmpl->body_func) {
            log_error("render_map_retransform_with_results: no template found for ref=%s",
                      saved.key.template_ref ? saved.key.template_ref : "(null)");
            entry->dirty = false;
            continue;
        }

        Item old_result = saved.result_node;

        // Find parent in element tree BEFORE fn() re-executes, because fn()
        // may trigger GC which reuses memory of old elements in the tree.
        Item tree_parent = saved.parent_result;
        int tree_child_index = saved.child_index;
        if (get_type_id(tree_parent) == LMD_TYPE_NULL && s_doc_root.item) {
            if (s_doc_root.item == old_result.item) {
                // old result IS the doc root — will be replaced directly
            } else {
                // Walk tree to find parent while tree is still valid
                tree_parent = find_parent_of(s_doc_root, old_result, &tree_child_index);
            }
        }

        // re-execute template body with the source item
        // NOTE: fn() may call apply() which modifies this hashmap — after this
        // call, 'entry' may be dangling. Use 'saved' for old values.
        typedef Item (*template_body_fn)(Item);
        template_body_fn fn = (template_body_fn)tmpl->body_func;
        Item new_result = fn(saved.key.source_item);

        // record result before updating entry
        if (out_results && count < max_results) {
            out_results[count].parent_result = tree_parent;
            out_results[count].new_result = new_result;
            out_results[count].old_result = old_result;
            out_results[count].child_index = tree_child_index;
            out_results[count].template_ref = saved.key.template_ref;
        }

        // update reverse map
        if (s_reverse_map && new_result.item) {
            ReverseMapEntry rentry;
            memset(&rentry, 0, sizeof(rentry));
            rentry.result_item_bits = new_result.item;
            rentry.key = saved.key;
            hashmap_set(s_reverse_map, &rentry);
        }

        // Replace old result with new result in the element tree
        if (get_type_id(tree_parent) != LMD_TYPE_NULL && tree_child_index >= 0) {
            TypeId parent_type = get_type_id(tree_parent);
            if (parent_type == LMD_TYPE_ELEMENT) {
                Element* parent_elmt = it2elmt(tree_parent);
                if (parent_elmt && tree_child_index < (int)parent_elmt->length) {
                    parent_elmt->items[tree_child_index] = new_result;
                }
            } else if (parent_type == LMD_TYPE_ARRAY) {
                Array* parent_arr = it2arr(tree_parent);
                if (parent_arr && tree_child_index < (int)parent_arr->length) {
                    parent_arr->items[tree_child_index] = new_result;
                }
            }
        } else if (s_doc_root.item == old_result.item && old_result.item != new_result.item) {
            s_doc_root = new_result;
            log_debug("render_map_retransform_with_results: updated s_doc_root to new result 0x%llx",
                      (unsigned long long)new_result.item);
        }

        // write back the updated entry to the map (re-lookup since entry may be stale)
        RenderMapEntry updated = saved;
        updated.result_node = new_result;
        updated.dirty = false;
        hashmap_set(map, &updated);

        count++;
        log_debug("render_map_retransform_with_results: re-transformed tmpl=%s (entry %d)",
                  saved.key.template_ref ? saved.key.template_ref : "(anon)", count);
    }

    if (count > 0) {
        log_debug("render_map_retransform_with_results: re-transformed %d entries (%d reported)",
                  count, count < max_results ? count : max_results);
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
    // register s_doc_root as a GC root so the doc root element is not collected
    // by the garbage collector (static variables are invisible to stack scanning)
    static bool root_registered = false;
    if (!root_registered) {
        heap_register_gc_root(&s_doc_root.item);
        root_registered = true;
    }
    s_doc_root = root;
}

Item render_map_get_doc_root(void) {
    return s_doc_root;
}

// Find the parent element/array that contains target as a direct child.
// Returns the parent Item and sets *out_index to the child index.
// Must be called BEFORE fn() re-execution while the tree is still valid.
static Item find_parent_of(Item node, Item target, int* out_index, int depth) {
    if (depth > 64) return ItemNull;
    TypeId tid = get_type_id(node);
    if (tid == LMD_TYPE_ELEMENT) {
        Element* elmt = it2elmt(node);
        if (!elmt) return ItemNull;
        for (unsigned i = 0; i < elmt->length; i++) {
            if (elmt->items[i].item == target.item) {
                *out_index = (int)i;
                return node;
            }
            Item found = find_parent_of(elmt->items[i], target, out_index, depth + 1);
            if (get_type_id(found) != LMD_TYPE_NULL) return found;
        }
    } else if (tid == LMD_TYPE_ARRAY) {
        Array* arr = it2arr(node);
        if (!arr) return ItemNull;
        for (unsigned i = 0; i < arr->length; i++) {
            if (arr->items[i].item == target.item) {
                *out_index = (int)i;
                return node;
            }
            Item found = find_parent_of(arr->items[i], target, out_index, depth + 1);
            if (get_type_id(found) != LMD_TYPE_NULL) return found;
        }
    }
    return ItemNull;
}

// ============================================================================
// R7 step 3c — source-document path tracking
// ============================================================================

void render_map_set_source_doc_root(Item root) {
    static bool registered = false;
    if (!registered) {
        heap_register_gc_root(&s_source_doc_root.item);
        registered = true;
    }
    s_source_doc_root = root;
}

Item render_map_get_source_doc_root(void) {
    return s_source_doc_root;
}

static Item render_map_get_map_field(Item node, const char* key) {
    if (get_type_id(node) != LMD_TYPE_MAP || !key) return ItemNull;
    Map* map = it2map(node);
    if (!map || !map->type || !map->data) return ItemNull;
    bool found = false;
    return _map_get((TypeMap*)map->type, map->data, (char*)key, &found);
}

static bool item_chars_equal(Item item, const char* value) {
    if (!value) return false;
    TypeId tid = get_type_id(item);
    if (tid != LMD_TYPE_SYMBOL && tid != LMD_TYPE_STRING) return false;
    const char* chars = item.get_chars();
    return chars && strcmp(chars, value) == 0;
}

static bool is_editor_doc_root(Item target) {
    if (get_type_id(target) != LMD_TYPE_MAP) return false;
    Item kind = render_map_get_map_field(target, "kind");
    Item tag = render_map_get_map_field(target, "tag");
    Item content = render_map_get_map_field(target, "content");
    return item_chars_equal(kind, "node") &&
           item_chars_equal(tag, "doc") &&
           get_type_id(content) == LMD_TYPE_ARRAY;
}

bool render_map_maybe_set_source_doc_root(Item target) {
    if (!s_path_recorder) return false;
    if (!is_editor_doc_root(target)) return false;
    render_map_set_source_doc_root(target);
    log_debug("render_map: source root set to editor doc map");
    return true;
}

void render_map_set_path_recorder(render_map_path_recorder_fn fn) {
    s_path_recorder = fn;
}

extern "C" bool render_map_has_path_recorder(void) {
    return s_path_recorder != NULL;
}

// DFS walk of element/array containers from `node`, locating `target`.
// On hit fills `out_indices` with the child-index path (root-relative,
// in walk order) and returns its depth; returns -1 on miss.
// `max_depth` bounds both recursion and output length.
//
// NOTE: Map descent (e.g., into a `content` field of `{kind, tag, attrs,
// content}` mod_doc nodes) is intentionally omitted here to keep
// render_map.cpp free of the heavyweight `item_attr` runtime dep. Most
// editor doc trees that flow through `apply()` are element-based; if a
// future doc-tree shape uses pure-map nesting we'll add a leaner Map
// accessor here.
static int find_path_to(Item node, Item target,
                        int* out_indices, int max_depth, int depth) {
    if (depth > max_depth || depth > 64) return -1;
    if (node.item == target.item) return depth;
    TypeId tid = get_type_id(node);
    if (tid == LMD_TYPE_ELEMENT) {
        Element* elmt = it2elmt(node);
        if (!elmt) return -1;
        for (unsigned i = 0; i < elmt->length; i++) {
            if (depth < max_depth) out_indices[depth] = (int)i;
            int found = find_path_to(elmt->items[i], target,
                                     out_indices, max_depth, depth + 1);
            if (found >= 0) return found;
        }
    } else if (tid == LMD_TYPE_ARRAY) {
        Array* arr = it2arr(node);
        if (!arr) return -1;
        for (unsigned i = 0; i < arr->length; i++) {
            if (depth < max_depth) out_indices[depth] = (int)i;
            int found = find_path_to(arr->items[i], target,
                                     out_indices, max_depth, depth + 1);
            if (found >= 0) return found;
        }
    } else if (tid == LMD_TYPE_MAP) {
        Item content_item = render_map_get_map_field(node, "content");
        if (get_type_id(content_item) != LMD_TYPE_ARRAY) return -1;
        Array* content = it2arr(content_item);
        if (!content) return -1;
        for (unsigned i = 0; i < content->length; i++) {
            if (depth < max_depth) out_indices[depth] = (int)i;
            int found = find_path_to(content->items[i], target,
                                     out_indices, max_depth, depth + 1);
            if (found >= 0) return found;
        }
    }
    return -1;
}

void render_map_record_source_path(Item target, const char* template_ref) {
    if (!s_path_recorder) return;
    if (s_source_doc_root.item == 0) return;
    int indices[64];
    int depth = find_path_to(s_source_doc_root, target,
                             indices, (int)(sizeof(indices) / sizeof(int)), 0);
    if (depth < 0) {
        log_debug("render_map_record_source_path: target not found under source root");
        return;
    }
    s_path_recorder(target, template_ref, indices, depth);
}