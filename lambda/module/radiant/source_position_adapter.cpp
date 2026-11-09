#include "source_position_adapter.hpp"
#include "radiant/source_position_host.hpp"
#include "../../runtime/render_map.h"
#include "lib/hashmap_typed.hpp"
#include "lib/memtrack.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Path side-table: keyed on (source_item.item, template_ref) → SourcePathC.
// Heap-owned indices; freed when the entry is overwritten or the table is
// reset.
// ---------------------------------------------------------------------------

typedef struct PathTableEntry {
    uint64_t    source_item_bits;
    const char* template_ref;
    SourcePathC path;            // owned
} PathTableEntry;

// The C hashmap only requires zero/nonzero equality; never order unrelated
// template pointers while probing this identity key.
static void path_entry_free(PathTableEntry* entry) {
    if (entry) source_path_free(&entry->path);
}

typedef TypedHashMap<PathTableEntry,
    HashMapIdentity2MemberKeyOps<PathTableEntry, &PathTableEntry::source_item_bits,
        &PathTableEntry::template_ref>, path_entry_free> PathTable;

typedef struct SourcePathBridgeState {
    PathTable path_table;
} SourcePathBridgeState;

static void source_path_bridge_state_destroy(void* opaque) {
    SourcePathBridgeState* state = (SourcePathBridgeState*)opaque;
    if (!state) return;
    state->path_table.destroy();
    mem_free(state);
}

static SourcePathBridgeState* source_path_bridge_state(bool create) {
    SourcePathBridgeState* state = (SourcePathBridgeState*)
        render_map_get_path_recorder_state();
    if (!state && create) {
        state = (SourcePathBridgeState*)mem_calloc(1, sizeof(SourcePathBridgeState),
            MEM_CAT_RENDER);
        if (state) {
            render_map_set_path_recorder_state(state);
            render_map_set_path_recorder_state_cleanup(source_path_bridge_state_destroy);
        }
    }
    return state;
}

static PathTable* ensure_path_table(void) {
    SourcePathBridgeState* state = source_path_bridge_state(true);
    if (!state) return NULL;
    if (!state->path_table.initialized() &&
            !state->path_table.init(64, 0xBEEF1234u, 0x5678CAFEu)) return NULL;
    return &state->path_table;
}

static void lambda_source_position_reset() {
    SourcePathBridgeState* state = source_path_bridge_state(false);
    if (!state) return;
    render_map_set_path_recorder_state(NULL);
    source_path_bridge_state_destroy(state);
}

static void lambda_source_position_record(Item source_item, const char* template_ref,
                                         const int* path_indices, int depth) {
    PathTable* m = ensure_path_table();
    if (!m) return;
    PathTableEntry e;
    memset(&e, 0, sizeof(e));
    e.source_item_bits = source_item.item;
    e.template_ref = template_ref;
    if (depth > 0 && path_indices) {
        e.path.indices = (int*)mem_alloc(sizeof(int) * (size_t)depth, MEM_CAT_RENDER);
        if (e.path.indices) {
            memcpy(e.path.indices, path_indices, sizeof(int) * (size_t)depth);
            e.path.depth = depth;
        }
    }
    // hashmap_set returns the replaced entry from map->spare; it does not run
    // the element-free callback on replacement.
    const PathTableEntry* old_entry = m->set(e);
    if (old_entry) {
        SourcePathC old_path = old_entry->path;
        source_path_free(&old_path);
    }
}

// Internal helper: lookup path by (source_item, template_ref). Returns a
// pointer into the table on hit; caller must NOT free.
static const SourcePathC* path_table_get(Item source_item,
                                         const char* template_ref) {
    SourcePathBridgeState* state = source_path_bridge_state(false);
    if (!state || !state->path_table.initialized()) return NULL;
    PathTableEntry q;
    memset(&q, 0, sizeof(q));
    q.source_item_bits = source_item.item;
    q.template_ref = template_ref;
    const PathTableEntry* hit = state->path_table.get(q);
    return hit ? &hit->path : NULL;
}

static bool lambda_source_position_reverse_lookup(Item result_node,
                                         RenderMapLookup* out_lookup,
                                         SourcePathC* out_path) {
    if (out_path) source_path_init(out_path);
    RenderMapLookup local;
    RenderMapLookup* lookup = out_lookup ? out_lookup : &local;
    if (!render_map_reverse_lookup(result_node, lookup)) return false;
    const SourcePathC* p = path_table_get(lookup->source_item, lookup->template_ref);
    if (out_path && p) *out_path = source_path_clone(p);
    // Reverse lookup hit even without a recorded path is still success:
    // the caller may want the (item, template_ref) for handler dispatch.
    return true;
}

static bool lambda_source_position_lookup(Item result_node, Item* source_item, SourcePathC* out_path) {
    RenderMapLookup value;
    RenderMapLookup* lookup = &value;
    if (!render_map_reverse_lookup(result_node, lookup)) return false;
    const SourcePathC* path = path_table_get(lookup->source_item, lookup->template_ref);
    // application/toolbar owners have render identities but no document coordinates.
    // treating their missing or retired path as [] lets text input replace the document root.
    Item source_root = render_map_get_source_doc_root();
    // plain template events retain their recorded paths without an active editor root.
    if (!path || (path->depth == 0 && source_root.item != 0 &&
            lookup->source_item.item != source_root.item)) return false;
    *out_path = source_path_clone(path);
    *source_item = lookup->source_item;
    return true;
}

static const RadiantSourcePositionHostOps lambda_source_position_host_ops = {
    lambda_source_position_record, lambda_source_position_reset,
    lambda_source_position_reverse_lookup, lambda_source_position_lookup,
};

bool lambda_source_position_host_register() {
    return radiant_source_position_host_register(&lambda_source_position_host_ops);
}
