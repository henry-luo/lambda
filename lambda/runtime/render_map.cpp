// render_map.cpp — Implementation of observer-based source→result mapping
// Phase 3 of Reactive UI: tracks which template invocations produced which
// result nodes, enabling targeted re-transformation when state/model changes.
#include "../lambda-data.hpp"
#include "render_map.h"
#include "runtime-state.h"
#include "heap_api.h"
#include "template_registry.h"
#include "transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/hashmap.h"
#include "../../lib/hashmap_helpers.h"
#include <limits.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Context-owned render reconciliation state
// ============================================================================

typedef struct RenderMapState {
    HashMap* render_map;
    bool owns_map;
    Item doc_root;          // top-level element tree for parent fixup
    Item source_doc_root;   // source-document path tracking
    render_map_path_recorder_fn path_recorder;
    void* path_recorder_state; // recorder-private, context-owned side state
    render_map_path_recorder_state_cleanup_fn path_recorder_state_cleanup;
    HashMap* reverse_map;
    bool roots_registered;
} RenderMapState;

static RenderMapState* render_map_state(void) {
    if (!context) {
        log_error("render-map: no bound canonical EvalContext");
        abort();
    }
    RenderMapState* state = (RenderMapState*)context->render_map_state;
    if (state) return state;
    state = (RenderMapState*)mem_calloc(1, sizeof(RenderMapState), MEM_CAT_EVAL);
    if (!state) {
        log_error("render-map: failed to create context state");
        abort();
    }
    context->render_map_state = state;
    return state;
}

static void render_map_register_roots(RenderMapState* state) {
    if (!state || state->roots_registered) return;
    if (!context || !context->heap ||
            !heap_try_register_gc_root_range(&state->doc_root.item, 1) ||
            !heap_try_register_gc_root_range(&state->source_doc_root.item, 1)) {
        log_error("render-map: failed to publish context root slots");
        abort();
    }
    state->roots_registered = true;
}

#define s_render_map (render_map_state()->render_map)
#define s_owns_map (render_map_state()->owns_map)
#define s_doc_root (render_map_state()->doc_root)
#define s_source_doc_root (render_map_state()->source_doc_root)
#define s_path_recorder (render_map_state()->path_recorder)
#define s_path_recorder_state (render_map_state()->path_recorder_state)
#define s_reverse_map (render_map_state()->reverse_map)

// forward declarations
static Item find_parent_of(Item node, Item target, int* out_index, int depth = 0);
static Item render_map_find_tree_parent(RenderMapEntry saved, int* out_child_index);
static bool render_map_replace_tree_result(RenderMapEntry saved, Item tree_parent,
                                           int tree_child_index, Item new_result);

static Item render_map_read_field(ShapeEntry* field, void* map_data) {
    if (!field || !field->type || !map_data) return ItemNull;
    return map_shape_field_to_item(map_data, field);
}

static Item render_map_get_field_from_type(TypeMap* map_type, void* map_data, const char* key, bool* is_found) {
    Item result = ItemNull;
    *is_found = false;
    if (!map_type || !map_data || !key) return result;
    FOR_EACH_MAP_FIELD(map_type, field) {
        if (!field->name) {
            Map* nested_map = map_shape_field_to_map(map_data, field);
            if (nested_map && nested_map->type_id == LMD_TYPE_MAP) {
                bool nested_found = false;
                Item nested_result = render_map_get_field_from_type(
                    (TypeMap*)nested_map->type, nested_map->data, key, &nested_found);
                if (nested_found) {
                    *is_found = true;
                    result = nested_result;
                }
            }
        } else if (field->name->str && field->name->length == strlen(key) &&
                   memcmp(field->name->str, key, field->name->length) == 0) {
            *is_found = true;
            result = render_map_read_field(field, map_data);
        }
    }
    return result;
}

// ============================================================================
// Reverse map: result_node.item → RenderMapKey (source_item, template_ref)
// ============================================================================

typedef struct ReverseMapEntry {
    uint64_t result_item_bits;   // Item.item value of the result node (key)
    RenderMapKey key;            // source_item + template_ref
} ReverseMapEntry;

HASHMAP_DEFINE_INTKEY(reverse_map, ReverseMapEntry, result_item_bits)

static HashMap* ensure_reverse_map(void) {
    if (!s_reverse_map) {
        s_reverse_map = hashmap_new(
            sizeof(ReverseMapEntry), 64,
            0xABCD1234, 0x5678FACE,
            reverse_map_hash, reverse_map_cmp,
            NULL, NULL
        );
    }
    return s_reverse_map;
}

static void render_map_record_reverse_result_tree(HashMap* reverse_map,
                                                  Item result_node,
                                                  RenderMapKey key,
                                                  int depth) {
    if (!reverse_map || !result_node.item || depth > 128) return;

    ReverseMapEntry query = {};
    query.result_item_bits = result_node.item;
    // A direct template result can reuse a fat-element address after a
    // retransform. Refresh that root mapping; nested apply() results below
    // it still retain their more specific reverse ownership.
    if (depth == 0 || !hashmap_get(reverse_map, &query)) {
        ReverseMapEntry entry = {};
        entry.result_item_bits = result_node.item;
        entry.key = key;
        hashmap_set(reverse_map, &entry);
    }

    TypeId result_type = get_type_id(result_node);
    if (result_type == LMD_TYPE_ELEMENT) {
        Element* element = result_node.element;
        if (!element || !element->items) return;
        for (int64_t i = 0; i < element->length; i++) {
            render_map_record_reverse_result_tree(reverse_map, element->items[i],
                                                  key, depth + 1);
        }
    } else if (result_type == LMD_TYPE_ARRAY) {
        Array* array = result_node.array;
        if (!array || !array->items) return;
        for (int64_t i = 0; i < array->length; i++) {
            render_map_record_reverse_result_tree(reverse_map, array->items[i],
                                                  key, depth + 1);
        }
    }
}

HASHMAP_DEFINE_FIELD2_KEY(render_map, RenderMapEntry, key.source_item.item, key.template_ref)

// ============================================================================
// Ensure map exists (lazy creation)
// ============================================================================

static HashMap* ensure_map(void) {
    if (!s_render_map) {
        s_render_map = hashmap_new(
            sizeof(RenderMapEntry), 64,
            0xFACE1234, 0x5678DEAD,
            render_map_hash, render_map_cmp,
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
    if (!context || !context->render_map_state) return;
    RenderMapState* state = (RenderMapState*)context->render_map_state;
    if (state->render_map && state->owns_map) {
        hashmap_free(state->render_map);
    }
    if (state->reverse_map) {
        hashmap_free(state->reverse_map);
    }
    if (state->roots_registered) {
        heap_unregister_gc_root_range(&state->doc_root.item);
        heap_unregister_gc_root_range(&state->source_doc_root.item);
    }
    if (state->path_recorder_state && state->path_recorder_state_cleanup) {
        state->path_recorder_state_cleanup(state->path_recorder_state);
        state->path_recorder_state = NULL;
    }
    context->render_map_state = NULL;
    mem_free(state);
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
    entry.child_count = result_node.item ? 1 : 0;
    entry.dirty = false;
    hashmap_set(map, &entry);

    // Also record every DOM-reachable result node. Template bodies may return
    // a fragment list that the parent flattens, so registering only the outer
    // list leaves all rendered descendants without route ownership.
    if (result_node.item) {
        HashMap* rmap = ensure_reverse_map();
        render_map_record_reverse_result_tree(rmap, result_node, entry.key, 0);
    }

    log_debug("render_map_record: tmpl=%s result=0x%llx reverse_map_count=%zu",
              template_ref ? template_ref : "(anon)",
              (unsigned long long)result_node.item,
              s_reverse_map ? hashmap_count(s_reverse_map) : 0);
}

void render_map_bind_fragment_parent(Item fragment_result, Item parent_result,
                                     int child_index, int child_count) {
    // Collection construction is also used outside a template evaluation.
    // Never create context-owned reconciliation state for those plain lists.
    if (!context || !context->render_map_state || !fragment_result.item ||
            child_index < 0 || child_count < 0) return;
    HashMap* map = s_render_map;
    if (!map) return;

    size_t iter = 0;
    void* item;
    while (hashmap_iter(map, &iter, &item)) {
        RenderMapEntry* entry = (RenderMapEntry*)item;
        if (entry->result_node.item != fragment_result.item) continue;  // RAW_ITEM_EQ_OK: render results are identity-tracked.
        entry->parent_result = parent_result;
        entry->child_index = child_index;
        entry->child_count = child_count;
        return;
    }
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
        int tree_child_index = saved.child_index;
        Item tree_parent = render_map_find_tree_parent(saved, &tree_child_index);

        // re-execute template body with the source item
        // NOTE: fn() may call apply() which modifies this hashmap — after this
        // call, 'entry' may be dangling. Use 'saved' for old values.
        typedef Item (*template_body_fn)(Context*, Item);
        template_body_fn fn = (template_body_fn)tmpl->body_func;
        if (!context) {
            log_error("render_map_retransform: no bound EvalContext");
            entry->dirty = false;
            continue;
        }
        Item new_result = fn((Context*)context, saved.key.source_item);

        // update reverse map
        if (s_reverse_map && new_result.item) {
            render_map_record_reverse_result_tree(s_reverse_map, new_result, saved.key, 0);
        }

        render_map_replace_tree_result(saved, tree_parent, tree_child_index, new_result);

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
        int tree_child_index = saved.child_index;
        Item tree_parent = render_map_find_tree_parent(saved, &tree_child_index);

        // re-execute template body with the source item
        // NOTE: fn() may call apply() which modifies this hashmap — after this
        // call, 'entry' may be dangling. Use 'saved' for old values.
        typedef Item (*template_body_fn)(Context*, Item);
        template_body_fn fn = (template_body_fn)tmpl->body_func;
        if (!context) {
            log_error("render_map_retransform_with_results: no bound EvalContext");
            entry->dirty = false;
            continue;
        }
        Item new_result = fn((Context*)context, saved.key.source_item);

        // record result before updating entry
        if (out_results && count < max_results) {
            out_results[count].parent_result = tree_parent;
            out_results[count].new_result = new_result;
            out_results[count].old_result = old_result;
            out_results[count].child_index = tree_child_index;
            out_results[count].child_count = saved.child_count;
            out_results[count].template_ref = saved.key.template_ref;
        }

        if (s_reverse_map && new_result.item) {
            render_map_record_reverse_result_tree(s_reverse_map, new_result, saved.key, 0);
        }

        render_map_replace_tree_result(saved, tree_parent, tree_child_index, new_result);

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
    // A plain DOM document has no template-render map.  Reverse lookup is an
    // optional query for source mapping, so report no mapping rather than
    // manufacturing or borrowing context-owned reconciliation state.
    if (!context) return false;
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
    render_map_register_roots(render_map_state());
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
            if (elmt->items[i].item == target.item) {  // RAW_ITEM_EQ_OK: tree parent search needs exact child identity.
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
            if (arr->items[i].item == target.item) {  // RAW_ITEM_EQ_OK: tree parent search needs exact child identity.
                *out_index = (int)i;
                return node;
            }
            Item found = find_parent_of(arr->items[i], target, out_index, depth + 1);
            if (get_type_id(found) != LMD_TYPE_NULL) return found;
        }
    }
    return ItemNull;
}

static int render_map_flattened_child_count(Item node, int depth) {
    if (!node.item || get_type_id(node) == LMD_TYPE_NULL) return 0;
    if (depth > 128) return -1;
    if (get_type_id(node) != LMD_TYPE_ARRAY || !node.array || !node.array->is_content) {
        return 1;
    }
    int total = 0;
    Array* fragment = node.array;
    for (int64_t i = 0; i < fragment->length; i++) {
        int child_count = render_map_flattened_child_count(fragment->items[i], depth + 1);
        if (child_count < 0 || total > INT_MAX - child_count) return -1;
        total += child_count;
    }
    return total;
}

static int render_map_flattened_scalar_tail_count(Item node, int depth) {
    if (!node.item || get_type_id(node) == LMD_TYPE_NULL) return 0;
    if (depth > 128) return -1;
    if (get_type_id(node) == LMD_TYPE_ARRAY && node.array && node.array->is_content) {
        int total = 0;
        Array* fragment = node.array;
        for (int64_t i = 0; i < fragment->length; i++) {
            int child_count = render_map_flattened_scalar_tail_count(fragment->items[i], depth + 1);
            if (child_count < 0 || total > INT_MAX - child_count) return -1;
            total += child_count;
        }
        return total;
    }
    TypeId type_id = get_type_id(node);
    return type_id == LMD_TYPE_INT64 || type_id == LMD_TYPE_UINT64 ||
           type_id == LMD_TYPE_FLOAT ? 1 : 0;
}

static int64_t render_map_store_flattened_children(List* parent, int64_t index,
                                                    Item node, int depth) {
    if (!node.item || get_type_id(node) == LMD_TYPE_NULL || depth > 128) return index;
    if (get_type_id(node) == LMD_TYPE_ARRAY && node.array && node.array->is_content) {
        Array* fragment = node.array;
        for (int64_t i = 0; i < fragment->length; i++) {
            index = render_map_store_flattened_children(parent, index,
                                                        fragment->items[i], depth + 1);
        }
        return index;
    }
    array_set((Array*)parent, index, node);
    return index + 1;
}

static void render_map_shift_bound_sibling_indices(Item parent_result,
                                                    int first_shifted_child,
                                                    int child_delta,
                                                    RenderMapKey replaced_key) {
    if (!child_delta || !s_render_map) return;
    size_t iter = 0;
    void* item;
    while (hashmap_iter(s_render_map, &iter, &item)) {
        RenderMapEntry* entry = (RenderMapEntry*)item;
        bool is_replaced_entry = entry->key.source_item.item == replaced_key.source_item.item &&
            entry->key.template_ref == replaced_key.template_ref;  // RAW_ITEM_EQ_OK: render-map keys use source identity.
        if (!is_replaced_entry && entry->parent_result.item == parent_result.item &&
                entry->child_index >= first_shifted_child) {  // RAW_ITEM_EQ_OK: bound fragments share their exact parent result.
            entry->child_index += child_delta;
        }
    }
}

static bool render_map_replace_child_range(RenderMapEntry saved, Item parent_result,
                                           int child_index, Item new_result) {
    TypeId parent_type = get_type_id(parent_result);
    if ((parent_type != LMD_TYPE_ELEMENT && parent_type != LMD_TYPE_ARRAY) ||
            child_index < 0) return false;

    int old_child_count = saved.child_count;
    if (old_child_count < 0) return false;
    int new_child_count = render_map_flattened_child_count(new_result, 0);
    int new_scalar_tail_count = render_map_flattened_scalar_tail_count(new_result, 0);
    if (new_child_count < 0 || new_scalar_tail_count < 0) return false;

    RootFrame roots(2);
    Rooted<Item> rooted_parent(roots, parent_result);
    Rooted<Item> rooted_result(roots, new_result);
    List* parent = (List*)rooted_parent.get().array;
    if (!parent || child_index > parent->length ||
            old_child_count > parent->length - child_index) return false;

    int64_t new_length = parent->length - old_child_count + new_child_count;
    if (new_length < 0 || new_length > INT64_MAX - parent->extra - new_scalar_tail_count) {
        return false;
    }
    int64_t required_capacity = new_length + parent->extra + new_scalar_tail_count;
    while (parent->capacity < required_capacity) {
        int64_t previous_capacity = parent->capacity;
        expand_list(parent, nullptr);
        parent = (List*)rooted_parent.get().array;
        // Allocation failure leaves capacity unchanged; retrying would spin
        // forever while a template transaction still owns the live document.
        if (!parent || parent->capacity <= previous_capacity) return false;
    }

    int64_t tail_count = parent->length - child_index - old_child_count;
    if (tail_count > 0 && new_child_count != old_child_count) {
        memmove(parent->items + child_index + new_child_count,
                parent->items + child_index + old_child_count,
                (size_t)tail_count * sizeof(Item));
    }
    parent->length = new_length;

    // Rendered fragment items are copied through array_set so tagged wide
    // scalars retain destination-owned tail storage when templates emit text.
    int64_t stored_end = render_map_store_flattened_children(
        parent, child_index, rooted_result.get(), 0);
    if (stored_end != child_index + new_child_count) return false;

    render_map_shift_bound_sibling_indices(parent_result,
                                           child_index + old_child_count,
                                           new_child_count - old_child_count,
                                           saved.key);
    return true;
}

static Item render_map_find_tree_parent(RenderMapEntry saved, int* out_child_index) {
    Item tree_parent = saved.parent_result;
    if (get_type_id(tree_parent) != LMD_TYPE_NULL || !s_doc_root.item) return tree_parent;
    if (s_doc_root.item == saved.result_node.item) {  // RAW_ITEM_EQ_OK: render tree root replacement is identity-based.
        return ItemNull;
    }
    return find_parent_of(s_doc_root, saved.result_node, out_child_index);
}

static bool render_map_replace_tree_result(RenderMapEntry saved, Item tree_parent,
                                           int tree_child_index, Item new_result) {
    if (get_type_id(tree_parent) != LMD_TYPE_NULL && tree_child_index >= 0) {
        return render_map_replace_child_range(saved, tree_parent, tree_child_index, new_result);
    }
    if (s_doc_root.item == saved.result_node.item &&
            saved.result_node.item != new_result.item) {  // RAW_ITEM_EQ_OK: render tree root replacement is identity-based.
        s_doc_root = new_result;
        log_debug("render_map_retransform: updated s_doc_root to new result 0x%llx",
                  (unsigned long long)new_result.item);
        return true;
    }
    return false;
}

// ============================================================================
// R7 step 3c — source-document path tracking
// ============================================================================

void render_map_set_source_doc_root(Item root) {
    render_map_register_roots(render_map_state());
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
    return render_map_get_field_from_type((TypeMap*)map->type, map->data, key, &found);
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

void* render_map_get_path_recorder_state(void) {
    // Teardown may run after a document has intentionally detached TLS. A
    // lookup must not recreate or abort; the owning runtime destroys the
    // payload through render_map_destroy while its context is still bound.
    if (!context || !context->render_map_state) return NULL;
    return ((RenderMapState*)context->render_map_state)->path_recorder_state;
}

void render_map_set_path_recorder_state(void* state) {
    s_path_recorder_state = state;
}

void render_map_set_path_recorder_state_cleanup(
        render_map_path_recorder_state_cleanup_fn cleanup) {
    render_map_state()->path_recorder_state_cleanup = cleanup;
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
    if (node.item == target.item) return depth;  // RAW_ITEM_EQ_OK: path search matches exact render node identity.
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
    if (s_source_doc_root.item == 0) return;  // RAW_ITEM_EQ_OK: zero Item means no source root recorded.
    int indices[64];
    int depth = find_path_to(s_source_doc_root, target,
                             indices, (int)(sizeof(indices) / sizeof(int)), 0);
    if (depth < 0) {
        log_debug("render_map_record_source_path: target not found under source root");
        return;
    }
    s_path_recorder(target, template_ref, indices, depth);
}
#include "runtime-state.h"
