#include "input.hpp"
#include "input-parsers.h"
#include "../core/lambda-decimal.hpp"
#include "../io/mark_builder.hpp"
#include "../../lib/url.h"
#include "../../lib/rdb.h"
#include "../../lib/stringbuf.h"
#include "../../lib/mime-detect.h"
#include "../../lib/arena.h"
#include "../../lib/mem_factory.h"
#include "../core/mem_factory_core.h"
#include "../../lib/log.h"  // add logging support
#include "../../lib/memtrack.h"
#include "../../lib/file.h"
#include "../../lib/str.h"
#include "../../lib/hashmap.h"
#include <limits.h>
#include <new>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

// Include Target API
extern "C" {
    #include "../lambda.h"  // for Target, TargetScheme, etc.
}
extern "C" Pool* path_get_pool(void);

#define MAX(a, b) ((a) > (b) ? (a) : (b))

__thread Context* input_context = NULL;
__thread InputAllocationContext* input_allocation_context = NULL;

// Input parsing may run concurrently, but ArrayList and Pool are deliberately
// single-owner types.  The manager lock protects only singleton bookkeeping;
// each worker keeps its own parser pool so document construction remains parallel.
static pthread_mutex_t g_input_manager_mutex = PTHREAD_MUTEX_INITIALIZER;
static __thread InputManager* g_input_thread_manager = NULL;
static __thread Pool* g_input_thread_pool = NULL;

ShapeEntry* alloc_shape_entry(Pool* pool, String* key, TypeId type_id, ShapeEntry* prev_entry) {
    return alloc_shape_entry_in(type_alloc_of_pool(pool), key, type_id, prev_entry);
}

static void shape_entry_append_after(ShapeEntry* shape_entry, ShapeEntry* prev_entry) {
    if (prev_entry) {
        prev_entry->chain_next = shape_entry;
        int prev_size = prev_entry->type ? shape_entry_storage_size(prev_entry) : (int)sizeof(Item);
        shape_entry->byte_offset = prev_entry->byte_offset + prev_size;
    }
    else { shape_entry->byte_offset = 0; }
}

// One named entry in one block -- the ShapeEntry, its StrView and a copy of the
// spelling -- so the name lives as long as the entry, even when the key came
// from a shorter-lived pool (e.g. a JS transpiler's name_pool that is freed by
// js_transpiler_destroy) or is a transient Lambda Symbol (D3.4.4v4). A zero
// `name_hash` routes by the spelling.
static ShapeEntry* alloc_named_shape_entry_in(TypeAlloc alloc, const char* chars,
        size_t len, NameId name_id, uint8_t key_kind, uint32_t name_hash,
        TypeId type_id, ShapeEntry* prev_entry) {
    size_t str_copy_size = len + 1;
    ShapeEntry* shape_entry = (ShapeEntry*)type_alloc_zeroed(alloc,
        sizeof(ShapeEntry) + sizeof(StrView) + str_copy_size);
    if (!shape_entry) return NULL;
    StrView* nv = (StrView*)((char*)shape_entry + sizeof(ShapeEntry));
    char* str_copy = (char*)nv + sizeof(StrView);
    ::str_copy(str_copy, str_copy_size, chars, len);
    nv->str = str_copy;  nv->length = len;
    shape_entry->name = nv;
    shape_entry->name_hash = name_hash ? name_hash : typemap_name_hash(nv->str, (int)nv->length);
    shape_entry->name_id = name_id;
    shape_entry->key_kind = key_kind;
    shape_entry_set_type(shape_entry, type_info[type_id].type);
    shape_entry_append_after(shape_entry, prev_entry);
    return shape_entry;
}

ShapeEntry* alloc_shape_entry_in(TypeAlloc alloc, String* key, TypeId type_id,
        ShapeEntry* prev_entry) {
    if (key) {
        // Input copies spelling for its own lifetime; only an already-owned
        // generated identity may cross this construction seam.
        return alloc_named_shape_entry_in(alloc, key->chars, key->len,
            string_is_pooled(key) ? name_ref_id(key) : NAME_ID_NONE,
            property_key_kind(key),
            property_key_requires_identity(key) ? property_key_hash(key) : 0,
            type_id, prev_entry);
    }
    // no key, for nested map
    log_debug("alloc_shape_entry: null key for nested map, type_id=%d", type_id);
    ShapeEntry* shape_entry = (ShapeEntry*)type_alloc_zeroed(alloc, sizeof(ShapeEntry));
    if (!shape_entry) return NULL;
    shape_entry->name = NULL;
    shape_entry_set_type(shape_entry, type_info[type_id].type);
    shape_entry_append_after(shape_entry, prev_entry);
    return shape_entry;
}

static bool map_key_is_array_index_name(String* key) {
    if (!key || key->len <= 0 || key->len > 10) return false;
    if (key->len > 1 && key->chars[0] == '0') return false;
    uint64_t index = 0;
    for (size_t i = 0; i < key->len; i++) {
        char c = key->chars[i];
        if (c < '0' || c > '9') return false;
        index = index * 10 + (uint64_t)(c - '0');
        if (index > 0xFFFFFFFEULL) return false;
    }
    return true;
}

static bool store_common_field_value(void* field_ptr, TypeId type_id, Item value) {
    switch (type_id) {
    case LMD_TYPE_NULL:
    case LMD_TYPE_UNDEFINED:
        *(bool*)field_ptr = false;
        return true;
    case LMD_TYPE_BOOL:
        *(bool*)field_ptr = value.bool_val;
        return true;
    case LMD_TYPE_INT:
        *(int64_t*)field_ptr = lambda_int_item_to_lane(value.item);
        return true;
    case LMD_TYPE_INT64:
        *(int64_t*)field_ptr = value.get_int64();
        return true;
    case LMD_TYPE_UINT64:
        *(uint64_t*)field_ptr = value.get_uint64();
        return true;
    case LMD_TYPE_NUM_SIZED:
        *(Item*)field_ptr = value;
        return true;
    case LMD_TYPE_FLOAT:
        *(double*)field_ptr = value.get_double();
        return true;
    case LMD_TYPE_DTIME:
        *(DateTime**)field_ptr = value.get_datetime_ptr();
        return true;
    case LMD_TYPE_DECIMAL:
        *(Decimal**)field_ptr = value.get_decimal();
        return true;
    case LMD_TYPE_STRING:
        *(String**)field_ptr = value.get_safe_string();
        return true;
    case LMD_TYPE_SYMBOL:
        *(Symbol**)field_ptr = value.get_safe_symbol();
        return true;
    case LMD_TYPE_BINARY:
        *(Binary**)field_ptr = value.get_safe_binary();
        return true;
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_MAP:
    case LMD_TYPE_ELEMENT:
        *(Container**)field_ptr = value.container;
        return true;
    case LMD_TYPE_PATH:
        *(Path**)field_ptr = value.path;
        return true;
    default:
        return false;
    }
}

static bool map_store_field_value(void* field_ptr, TypeId type_id, Item value) {
    if (!field_ptr) return false;
    if (store_common_field_value(field_ptr, type_id, value)) return true;
    switch (type_id) {
    case LMD_TYPE_RANGE:
        *(Container**)field_ptr = value.container;
        break;
    case LMD_TYPE_VMAP:
    case LMD_TYPE_VARRAY:
    case LMD_TYPE_VELMT:
        // D7.4.5v2 virtual carriers are host-object pointers, not materialized
        // Map/Array/Element payloads; preserve their exact carrier identity.
        *(VirtualContainer**)field_ptr = virtual_container_from_item(value);
        break;
    case LMD_TYPE_FUNC:
        *(Function**)field_ptr = value.function;
        break;
    case LMD_TYPE_TYPE:
        *(Type**)field_ptr = value.type;
        break;
    case LMD_TYPE_ANY: {
        Item item = value;
        TypeId item_type_id = get_type_id(item);
        log_debug("set field of ANY type to type: %d", item_type_id);
        TypedItem titem = {.type_id = item_type_id, .item = item.item};
        switch (item_type_id) {
        case LMD_TYPE_NULL:
        case LMD_TYPE_UNDEFINED:
            break; // no extra work needed
        case LMD_TYPE_BOOL:
            titem.bool_val = item.bool_val;  break;
        case LMD_TYPE_INT:
            // C16: carry the numeric value; an int Item payload is not its value.
            titem.double_val = lambda_int_item_value(item);  break;
        case LMD_TYPE_INT64:
            titem.long_val = item.get_int64();  break;
        case LMD_TYPE_UINT64:
            titem.uint64_val = item.get_uint64();  break;
        case LMD_TYPE_FLOAT:
            titem.double_val = item.get_double();  break;
        case LMD_TYPE_DTIME:
            titem.datetime_ptr = item.get_datetime_ptr();  break;
        case LMD_TYPE_STRING:
            titem.string = item.get_safe_string();
            break;
        case LMD_TYPE_SYMBOL:
            titem.symbol = item.get_safe_symbol();
            break;
        case LMD_TYPE_BINARY:
            titem.binary = item.get_safe_binary();
            break;
        case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_NUM:
        case LMD_TYPE_MAP:  case LMD_TYPE_VMAP: case LMD_TYPE_VARRAY:
        case LMD_TYPE_ELEMENT: case LMD_TYPE_VELMT: {
            Container *container = item.container;
            titem.container = container;
            break;
        }
        case LMD_TYPE_TYPE:
            titem.type = item.type;
            break;
        case LMD_TYPE_FUNC:
            titem.function = item.function;
            break;
        case LMD_TYPE_PATH:
            titem.path = item.path;
            break;
        default:
            log_error("unknown type %d in set_fields", item_type_id);
            // set as ERROR
            titem = {.type_id = LMD_TYPE_ERROR};
        }
        // set in map
        *(TypedItem*)field_ptr = titem;
        break;
    }
    default:
        log_debug("unknown type %d\n", value._type_id);
        return false;
    }
    return true;
}

static bool map_ensure_data_capacity_for_end(Map** map_slot, Pool* pool,
        int64_t byte_end, int64_t copy_bytes, MapDataGrowFn grow,
        void* grow_context, String** keys, int key_count,
        Item* values, int value_count) {
    if (!map_slot || !*map_slot || !pool || byte_end < 0 || byte_end > INT_MAX) {
        return false;
    }
    Map* mp = *map_slot;
    if (mp->data && byte_end <= mp->data_cap) return true;
    int byte_cap = mp->data_cap == 0
        ? MAX(64, (int)byte_end)
        : MAX(mp->data_cap, (int)byte_end) * 2;

    if (grow) {
        return grow(map_slot, byte_cap, copy_bytes, keys, key_count,
            values, value_count, grow_context);
    }

    void* new_data = pool_calloc(pool, byte_cap);
    if (!new_data) return false;
    if (mp->data) {
        if (copy_bytes < 0) copy_bytes = 0;
        if (copy_bytes > mp->data_cap) copy_bytes = mp->data_cap;
        if (copy_bytes > 0) memcpy(new_data, mp->data, (size_t)copy_bytes);
        pool_free(pool, mp->data);
    }
    mp->data = new_data;
    mp->data_cap = byte_cap;
    return true;
}

// Copies `source`'s fields into fresh entries. It walks the type, not the
// chain: a tree node's chain may run on into a descendant's fields (D3.4.3v3).
// The copies keep chain_index 0 -- a copied prefix is on every type of the
// chain it starts.
static ShapeEntry* clone_shape_entries(TypeAlloc alloc, const TypeMap* source,
        ShapeEntry** out_last) {
    if (out_last) *out_last = NULL;
    if (!source || !source->shape) return NULL;
    ShapeEntry* first = NULL;
    ShapeEntry* prev = NULL;
    ShapeEntry* last = NULL;
    FOR_EACH_MAP_FIELD(source, src) {
        ShapeEntry* dst = (ShapeEntry*)type_alloc_zeroed(alloc, sizeof(ShapeEntry));
        if (!dst) return NULL;
        dst->name = src->name;
        dst->type = src->type;
        dst->storage = *shape_entry_storage(src);
        dst->byte_offset = src->byte_offset;
        dst->ns = src->ns;
        dst->default_value = src->default_value;
        dst->name_hash = src->name_hash;
        dst->name_id = src->name_id;
        dst->key_kind = src->key_kind;
        dst->flags = src->flags;
        if (!first) first = dst;
        if (prev) prev->chain_next = dst;
        prev = dst;
        last = dst;
    }
    if (out_last) *out_last = last;
    return first;
}

static ShapeEntry* clone_shape_chain_for_transition(TypeAlloc alloc, TypeMap* parent,
        ShapeEntry** out_last) {
    return clone_shape_entries(alloc, parent, out_last);
}

// js constructor/pre-shape caches share TypeMap instances across many Maps.
// generic map writers append ShapeEntry nodes, so detach before mutating a
// shared shape; otherwise one object can leak fields or slot metadata into
// sibling objects and cause order-dependent Node baseline regressions.
static TypeMap* map_clone_typemap_for_mutation(Map* mp, Input* input) {
    if (!mp || !input || !input->pool) return NULL;
    TypeMap* tm = (TypeMap*)mp->type;
    if (!tm) return NULL;
    if (tm->is_private_clone) return tm;

    Pool* pool = input->pool;
    TypeMap* clone = (TypeMap*)alloc_type(pool, LMD_TYPE_MAP, sizeof(TypeMap));
    if (!clone) return NULL;
    clone->length = tm->length;
    clone->byte_size = tm->byte_size;
    clone->type_index = tm->type_index;
    clone->has_spread = tm->has_spread;  // cloned chain keeps any nameless spread slot
    clone->has_named_shape = tm->has_named_shape;
    clone->is_trusted_contract = false;
    clone->struct_name = tm->struct_name;
    clone->is_private_clone = true;
    clone->is_shared_constructor_shape = false;
    clone->is_transition_shared_shape = false;
    clone->transitions = NULL;
    clone->js_meta = tm->js_meta;
    clone->has_array_index_shape = tm->has_array_index_shape;

    ShapeEntry* last_clone = NULL;
    clone->shape = clone_shape_chain_for_transition(type_alloc_of_pool(pool), tm, &last_clone);
    if (tm->shape && !clone->shape) return NULL;
    clone->last = last_clone;

    typemap_hash_build(clone, pool);

    if (tm->slot_entries && tm->slot_count > 0) {
        // rebuild slot_entries against cloned ShapeEntry nodes so fast slot
        // access never points back into the shared blueprint shape.
        ShapeEntry** entries = (ShapeEntry**)pool_calloc(pool,
            (size_t)tm->slot_count * sizeof(ShapeEntry*));
        if (entries) {
            ShapeEntry* e = clone->shape;
            for (int i = 0; i < tm->slot_count && e; i++, e = typemap_next_field(clone, e)) {
                entries[i] = e;
            }
            clone->slot_entries = entries;
            clone->slot_count = tm->slot_count;
        }
    }

    mp->type = clone;
    log_debug("map_clone_typemap_for_mutation: cloned TypeMap %p -> %p for Map %p",
        (void*)tm, (void*)clone, (void*)mp);
    return clone;
}

static TypeElmt* elmt_clone_type_for_mutation(Element* elmt, Pool* pool) {
    if (!elmt || !pool) return NULL;
    TypeElmt* tm = (TypeElmt*)elmt->type;
    if (!tm) return NULL;
    if (tm->is_private_clone) return tm;

    TypeElmt* clone = (TypeElmt*)alloc_type(pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
    if (!clone) return NULL;
    clone->length = tm->length;
    clone->byte_size = tm->byte_size;
    clone->type_index = tm->type_index;
    clone->has_spread = tm->has_spread;  // cloned chain keeps any nameless spread slot
    clone->has_named_shape = tm->has_named_shape;
    clone->is_trusted_contract = false;
    clone->struct_name = tm->struct_name;
    clone->is_private_clone = true;
    clone->is_shared_constructor_shape = false;
    clone->is_transition_shared_shape = false;
    clone->transitions = NULL;
    clone->js_meta = tm->js_meta;
    clone->has_array_index_shape = tm->has_array_index_shape;
    clone->name = tm->name;
    clone->name_id = tm->name_id;  // the HTML5 parser compares tags by this id
    clone->content_list = tm->content_list;
    clone->ns = tm->ns;

    ShapeEntry* last_clone = NULL;
    clone->shape = clone_shape_chain_for_transition(type_alloc_of_pool(pool), (TypeMap*)tm,
        &last_clone);
    if (tm->shape && !clone->shape) return NULL;
    clone->last = last_clone;
    typemap_hash_build((TypeMap*)clone, pool);

    elmt->type = clone;
    log_debug("elmt_clone_type_for_mutation: cloned TypeElmt %p -> %p for Element %p",
        (void*)tm, (void*)clone, (void*)elmt);
    return clone;
}

// A recorded transition is only valid while its parent is still the shape it
// was recorded from, so a hit re-checks the child's cloned prefix against it.
// That is O(depth) per add and now runs on the hot path, but measured min-of-5
// it is only 1.5% of an add at 8 fields and 3.1% at 256, and the width-dependent
// cost of building an object is the same with and without it — so the cheaper
// length/byte_size check that a shared parent would allow buys nothing real and
// is not worth weakening this one for (JS_Tune_History rule 5).
static bool map_transition_prefix_matches_parent(TypeMap* parent, TypeMap* target) {
    if (!parent || !target) return false;
    // D3.4.3v3: a child that extended the parent's chain in place holds the
    // parent's own entries as its prefix, so there is nothing to compare.
    if (parent->shape && target->shape == parent->shape) return true;
    ShapeEntry* parent_entry = parent->shape;
    ShapeEntry* target_entry = target->shape;
    while (parent_entry) {
        if (!target_entry) return false;
        if (target_entry->name != parent_entry->name) {
            if (!target_entry->name || !parent_entry->name) return false;
            uint32_t parent_name_hash = typemap_shape_entry_name_hash(parent_entry);
            uint32_t target_name_hash = typemap_shape_entry_name_hash(target_entry);
            if (parent_name_hash != 0 && target_name_hash != 0 &&
                    parent_name_hash != target_name_hash) return false;
            if (target_entry->name->length != parent_entry->name->length) return false;
            if (memcmp(target_entry->name->str, parent_entry->name->str,
                    parent_entry->name->length) != 0) {
                return false;
            }
        }
        if (target_entry->type != parent_entry->type ||
                target_entry->byte_offset != parent_entry->byte_offset ||
                target_entry->flags != parent_entry->flags) {
            return false;
        }
        parent_entry = typemap_next_field(parent, parent_entry);
        target_entry = typemap_next_field(target, target_entry);
    }
    return true;
}

// The graph budgets (D3.4.3v3). The per-node edge caps bound one shape's
// fan-out, not the graph. A long-lived process that runs thousands of unrelated
// scripts through one Input (the test262 batch runner is the extreme case)
// would otherwise keep minting map shapes for the rest of its life: with only
// the edge cap it grew to 5.4 GB. Parsed elements count against their own
// budget: one document's attribute variety runs to thousands of distinct
// sequences (a 13 MiB corpus of 77 real sites needs about 5.7K element nodes),
// which would exhaust the map budget and leave most elements on private types.
// Past a budget every add keeps a private type, as before the graph existed.
static const int MAX_SHAPE_GRAPH = 1024;
static const int MAX_ELEMENT_SHAPE_GRAPH = 16384;

// Impl_Map_Transition_Coverage P1.5 (Q5): measurement only. Process-wide tree
// counters, split into the runtime tree [0] and every other tree [1] (parsed
// documents, js_input). LAMBDA_SHAPE_TREE_STATS=<file> appends one line per
// class to <file> at exit, so batch runners aggregate across processes.
typedef struct ShapeTreeStats {
    uint64_t hits;                 // adds that followed an existing edge
    uint64_t mints;                // adds that minted a node
    uint64_t declined_fanout;      // adds declined by a node's edge cap
    uint64_t declined_budget;      // adds declined by the graph budget
    uint64_t external_hits;        // the same three for external parents (D3.4.3v5)
    uint64_t external_mints;
    uint64_t external_declined_fanout;
    uint64_t edges_walked;         // edge-list entries scanned, over all adds
    uint64_t max_walk;             // the longest scan of one add
    uint64_t decline_degree[4];    // out-degree at a fan-out decline: <=16 <=64 <=256 >256
    uint64_t private_types;        // map_put fallbacks to a private type
    uint64_t private_copies;       // runtime adds that copied a whole shape
    uint64_t private_entries;      // entries those copies duplicated
    uint64_t peak_nodes;           // the most map and element nodes one tree held
} ShapeTreeStats;

static ShapeTreeStats g_shape_tree_stats[2];
static const char* g_shape_tree_stats_path = NULL;
static int g_shape_tree_stats_state = -1;  // -1 unread, 0 off, 1 on

static void shape_tree_stats_flush(void) {
    static const char* const kClass[2] = {"runtime", "other"};
    for (int c = 0; c < 2; c++) {
        const ShapeTreeStats* s = &g_shape_tree_stats[c];
        char line[768];
        snprintf(line, sizeof(line),
            "shape_tree_stats tree=%s hits=%llu mints=%llu "
            "declined_fanout=%llu declined_budget=%llu ext_hits=%llu ext_mints=%llu "
            "ext_declined_fanout=%llu edges_walked=%llu max_walk=%llu "
            "decline_degree=%llu/%llu/%llu/%llu private_types=%llu private_copies=%llu "
            "private_entries=%llu peak_nodes=%llu\n",
            kClass[c], (unsigned long long)s->hits, (unsigned long long)s->mints,
            (unsigned long long)s->declined_fanout, (unsigned long long)s->declined_budget,
            (unsigned long long)s->external_hits, (unsigned long long)s->external_mints,
            (unsigned long long)s->external_declined_fanout,
            (unsigned long long)s->edges_walked, (unsigned long long)s->max_walk,
            (unsigned long long)s->decline_degree[0], (unsigned long long)s->decline_degree[1],
            (unsigned long long)s->decline_degree[2], (unsigned long long)s->decline_degree[3],
            (unsigned long long)s->private_types, (unsigned long long)s->private_copies,
            (unsigned long long)s->private_entries, (unsigned long long)s->peak_nodes);
        append_text_file(g_shape_tree_stats_path, line);
    }
}

static bool shape_tree_stats_on(void) {
    int state = __atomic_load_n(&g_shape_tree_stats_state, __ATOMIC_ACQUIRE);
    if (state >= 0) return state == 1;
    const char* path = getenv("LAMBDA_SHAPE_TREE_STATS");
    int next = path && path[0] ? 1 : 0;
    g_shape_tree_stats_path = path;
    int expected = -1;
    if (__atomic_compare_exchange_n(&g_shape_tree_stats_state, &expected, next, false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        if (next) atexit(shape_tree_stats_flush);
        return next == 1;
    }
    return expected == 1;
}

static ShapeTreeStats* shape_tree_stats_for(const Input* input) {
    if (!shape_tree_stats_on()) return NULL;
    return &g_shape_tree_stats[input && input->keeps_external_edges ? 0 : 1];
}

static void shape_tree_stat_add(uint64_t* counter, uint64_t amount) {
    __atomic_fetch_add(counter, amount, __ATOMIC_RELAXED);
}

static void shape_tree_stat_max(uint64_t* counter, uint64_t value) {
    uint64_t seen = __atomic_load_n(counter, __ATOMIC_RELAXED);
    while (value > seen && !__atomic_compare_exchange_n(counter, &seen, value, false,
            __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

void shape_tree_stats_note_private_copy(int64_t entries) {
    if (!shape_tree_stats_on()) return;
    shape_tree_stat_add(&g_shape_tree_stats[0].private_copies, 1);
    shape_tree_stat_add(&g_shape_tree_stats[0].private_entries, (uint64_t)(entries > 0 ? entries : 0));
}

// The edge cap for a step from `parent`, for every tree and for external-table
// edges alike: each list is walked linearly per add (the table only finds a
// parent's list). Impl_Map_Transition_Coverage P1.5 measured it (Q5): no
// benchmark or corpus script reached 16 below the root, and lifting the root's
// 256 to share jq_mix's 2,048 one-key objects cost 9-10% CPU in edge walks.
static int shape_tree_fanout_cap(const TypeMap* parent) {
    return parent->length == 0 ? 256 : 16;
}

// D4.1.4v4: a tree node and its edge are never freed on their own, so they
// come from the Input's arena and go with the Input (D4.2.6).
static TypeAlloc input_tree_alloc(Input* input) {
    TypeAlloc alloc = {input->pool, input->arena};
    return alloc;
}

static bool transition_graph_has_room(Input* input, bool element) {
    int map_budget = input->shape_graph_budget > 0
        ? input->shape_graph_budget : MAX_SHAPE_GRAPH;
    return element ? input->element_transition_shapes < MAX_ELEMENT_SHAPE_GRAPH
                   : input->shape_transition_shapes < map_budget;
}

static void transition_graph_count(Input* input, bool element) {
    if (element) input->element_transition_shapes++;
    else input->shape_transition_shapes++;
    // a document past the budget silently loses sharing, so say so once
    if (element && input->element_transition_shapes == MAX_ELEMENT_SHAPE_GRAPH) {
        log_info("element_tree_budget: an Input reached %d element types; later elements keep private types",
            MAX_ELEMENT_SHAPE_GRAPH);
    }
}

// A field's identity on a transition edge (D3.4.4v4): its key kind, then for
// a JS Symbol or private name its record (NameId), and for a STRING name its
// spelling, of which an equal NameId is only a fast proof. A new edge's entry
// is minted from the spelling, or copied from `like` when an editor rebuild
// replays a field.
typedef struct TransitionKey {
    NameId name_id;
    uint8_t key_kind;
    uint32_t name_hash;  // routing only: the spelling's, or a record's unique hash
    const char* name;
    uint32_t name_len;
    const ShapeEntry* like;
} TransitionKey;

static TransitionKey transition_key_of_string(String* key) {
    // D4.6.1v2: a pooled name carries its NameId, which proves two STRING
    // names equal at once; an unpooled or computed key has none and is
    // matched by its spelling (D3.4.4v4).
    TransitionKey k = {};
    k.name_id = string_is_pooled(key) ? name_ref_id(key) : NAME_ID_NONE;
    k.key_kind = property_key_kind(key);
    k.name = key->chars;
    k.name_len = (uint32_t)key->len;
    k.name_hash = property_key_requires_identity(key)
        ? property_key_hash(key) : typemap_name_hash(key->chars, (int)key->len);
    return k;
}

// A Lambda name given only by its characters -- a Symbol key, which is a
// STRING name in the global namespace (S8.2.2v4).
static TransitionKey transition_key_of_chars(const char* chars, uint32_t len) {
    TransitionKey k = {};
    k.name_id = NAME_ID_NONE;
    k.key_kind = NAME_KEY_STRING;
    k.name = chars;
    k.name_len = len;
    k.name_hash = typemap_name_hash(chars, (int)len);
    return k;
}

static TransitionKey transition_key_of_entry(const ShapeEntry* like) {
    TransitionKey k = {};
    k.name_id = like->name_id;
    k.key_kind = like->key_kind;
    k.name_hash = like->name_hash ? like->name_hash
        : typemap_name_hash(like->name->str, (int)like->name->length);
    k.name = like->name->str;
    k.name_len = (uint32_t)like->name->length;
    k.like = like;
    return k;
}

// The entry a replayed field adds: `like`'s identity at a new type. The name
// is shared, as clone_shape_entries shares it, since both live in the Input.
ShapeEntry* shape_entry_copy_as(TypeAlloc alloc, const ShapeEntry* like,
        TypeId type_id, ShapeEntry* prev_entry) {
    ShapeEntry* entry = (ShapeEntry*)type_alloc_zeroed(alloc, sizeof(ShapeEntry));
    if (!entry) return NULL;
    entry->name = like->name;
    entry->name_hash = like->name_hash;
    entry->name_id = like->name_id;
    entry->key_kind = like->key_kind;
    entry->ns = like->ns;
    shape_entry_set_type(entry, type_info[type_id].type);
    if (prev_entry) prev_entry->chain_next = entry;
    return entry;
}

// A copy of `src`'s characters in `alloc`; false when the allocation fails.
static bool strview_copy_into(TypeAlloc alloc, StrView* out, StrView src) {
    if (!src.str) { *out = src; return true; }
    char* chars = (char*)type_alloc_zeroed(alloc, src.length + 1);
    if (!chars) return false;
    memcpy(chars, src.str, src.length);
    out->str = chars;
    out->length = src.length;
    return true;
}

// clone_shape_entries for a parent the tree does not own (D3.4.3v5): the names
// move into `alloc` too, since the parent's owner -- a module, a parsed Input,
// a context pool -- may be freed while the tree lives on.
static ShapeEntry* clone_shape_entries_owned(TypeAlloc alloc, const TypeMap* source,
        ShapeEntry** out_last) {
    if (out_last) *out_last = NULL;
    ShapeEntry* last = NULL;
    ShapeEntry* first = clone_shape_entries(alloc, source, &last);
    for (ShapeEntry* e = first; e; e = shape_chain_next_until(e, last)) {
        if (!e->name) continue;
        StrView* name = (StrView*)type_alloc_zeroed(alloc, sizeof(StrView));
        if (!name || !strview_copy_into(alloc, name, *e->name)) return NULL;
        e->name = name;
    }
    if (out_last) *out_last = last;
    return first;
}

// D3.4.3v5: a type the runtime tree may grow from without owning it -- a
// literal's, a contract's, a nominal, a parsed or a private type. JS shapes
// (class metadata, fixed slots, descriptor flags, accessors), array-index
// shapes and spread link slots keep their own paths.
static bool external_parent_admissible(const TypeMap* parent) {
    if (!parent || (parent->type_id != LMD_TYPE_MAP && parent->type_id != LMD_TYPE_ELEMENT)) {
        return false;
    }
    if (parent->js_meta || parent->has_spread || parent->has_array_index_shape ||
            parent->slot_entries || parent->slot_count > 0) return false;
    int64_t count = 0;
    FOR_EACH_MAP_FIELD(parent, field) {
        if (!field->name || field->byte_offset < 0 || field->flags != 0 || field->accessor) {
            return false;
        }
        count++;
    }
    return count == parent->length;
}

// D3.4.3v5: whether a cached child of an external parent extends it by exactly
// one field. Children are found by the parent's structural fingerprint, and a
// child minted from another parent -- a document since freed, a colliding
// shape -- is reused only if every value it copied equals this live parent's:
// each entry's spelling, identity, contract, offset, flags, namespace and
// default, and the type's record identity. It then references nothing that
// died, and only what this parent references too.
// The type-level part of the match: the record identity, and for an element
// its tag, namespace and declared content.
static bool external_identity_matches(const TypeMap* parent, const TypeMap* child) {
    if (!parent || !child || child->type_id != parent->type_id ||
            child->nominal != parent->nominal || child->is_nominal != parent->is_nominal ||
            child->struct_name != parent->struct_name ||
            child->has_named_shape != parent->has_named_shape) {
        return false;
    }
    if (parent->type_id == LMD_TYPE_ELEMENT) {
        const TypeElmt* pe = (const TypeElmt*)parent;
        const TypeElmt* ce = (const TypeElmt*)child;
        if (ce->name_id != pe->name_id || ce->ns != pe->ns ||
                ce->content_list != pe->content_list ||
                ce->name.length != pe->name.length ||
                (pe->name.length && memcmp(ce->name.str, pe->name.str, pe->name.length) != 0)) {
            return false;
        }
    }
    return true;
}

// The entry-level part: `copy`'s first parent->length entries carry the
// parent's spelling, identity, contract, flags, namespace and default, and,
// when `same_offsets`, its offsets. The entry at `retyped` (-1: none) carries
// `retyped_to` instead of the parent's contract.
static bool external_entries_match(const TypeMap* parent, const TypeMap* copy,
        bool same_offsets, int64_t retyped, const Type* retyped_to) {
    const ShapeEntry* c = typemap_first_field(copy);
    int64_t count = 0;
    FOR_EACH_MAP_FIELD(parent, p) {
        const Type* expected = count == retyped ? retyped_to : p->type;
        if (!c || !p->name || !c->name || c->name->length != p->name->length ||
                memcmp(c->name->str, p->name->str, p->name->length) != 0 ||
                c->name_id != p->name_id || c->key_kind != p->key_kind ||
                c->type != expected || (same_offsets && c->byte_offset != p->byte_offset) ||
                c->flags != p->flags || c->ns != p->ns ||
                c->default_value != p->default_value) {
            return false;
        }
        c = typemap_next_field(copy, c);
        count++;
    }
    return count == parent->length;
}

static bool external_child_matches_parent(const TypeMap* parent, const TypeMap* child) {
    return external_identity_matches(parent, child) &&
        child->length == parent->length + 1 &&
        external_entries_match(parent, child, true, -1, NULL);
}

// Impl_Map_Transition_Coverage P2: whether a cached retype target is `parent`
// with the field at `position` laid out for `retyped_to` (offsets after it
// may move).
static bool retype_target_matches_parent(const TypeMap* parent, const TypeMap* target,
        int64_t position, const Type* retyped_to) {
    return external_identity_matches(parent, target) &&
        target->length == parent->length &&
        external_entries_match(parent, target, false, position, retyped_to);
}

// The identity a node minted from an external parent copies: the record, and
// an element's tag -- whose spelling lives with the parent's owner, so it is
// copied into the tree -- namespace and declared content.
static bool copy_external_identity(TypeAlloc tree, TypeMap* child, const TypeMap* parent) {
    child->nominal = parent->nominal;
    child->is_nominal = parent->is_nominal;
    child->struct_name = parent->struct_name;
    child->has_named_shape = parent->has_named_shape;
    if (parent->type_id != LMD_TYPE_ELEMENT) return true;
    const TypeElmt* element_parent = (const TypeElmt*)parent;
    TypeElmt* element_child = (TypeElmt*)child;
    element_child->name_id = element_parent->name_id;
    element_child->ns = element_parent->ns;
    element_child->content_list = element_parent->content_list;
    return strview_copy_into(tree, &element_child->name, element_parent->name);
}

// D3.4.3v5: the runtime tree's edges from external parents, one list per
// parent STRUCTURE. The key is a fingerprint of every value a child copies
// from its parent -- the type's record identity and each entry's spelling,
// identity, contract, offset, flags, namespace and default -- so equal shapes
// from different documents, or different private types, share one list;
// keying by address minted a separate path for every parsed document. A
// fingerprint collision only shares a list: each child there is still matched
// against the live parent in full (external_child_matches_parent). The list
// head lives in the tree's arena so a pointer to it survives the table growing.
typedef struct ExternalParentEdges {
    uint64_t fingerprint;
    TypeMapTransition** edges;
} ExternalParentEdges;

static uint64_t fingerprint_mix(uint64_t h, const void* data, size_t len) {
    const unsigned char* bytes = (const unsigned char*)data;
    for (size_t i = 0; i < len; i++) {
        h ^= bytes[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

static uint64_t fingerprint_mix_ptr(uint64_t h, const void* ptr) {
    uintptr_t value = (uintptr_t)ptr;
    return fingerprint_mix(h, &value, sizeof(value));
}

// Hashes exactly what external_child_matches_parent compares.
static uint64_t external_parent_fingerprint(const TypeMap* parent) {
    uint64_t h = 0xcbf29ce484222325ull;
    uint8_t header[4] = {parent->type_id, (uint8_t)parent->is_nominal,
        (uint8_t)parent->has_named_shape, 0};
    h = fingerprint_mix(h, header, sizeof(header));
    h = fingerprint_mix(h, &parent->length, sizeof(parent->length));
    h = fingerprint_mix_ptr(h, parent->nominal);
    h = fingerprint_mix_ptr(h, parent->struct_name);
    if (parent->type_id == LMD_TYPE_ELEMENT) {
        const TypeElmt* element = (const TypeElmt*)parent;
        if (element->name.str) h = fingerprint_mix(h, element->name.str, element->name.length);
        h = fingerprint_mix(h, &element->name_id, sizeof(element->name_id));
        h = fingerprint_mix_ptr(h, element->ns);
        h = fingerprint_mix_ptr(h, element->content_list);
    }
    FOR_EACH_MAP_FIELD(parent, field) {
        if (field->name) h = fingerprint_mix(h, field->name->str, field->name->length);
        uint8_t kinds[2] = {field->key_kind, field->flags};
        h = fingerprint_mix(h, kinds, sizeof(kinds));
        h = fingerprint_mix(h, &field->name_id, sizeof(field->name_id));
        h = fingerprint_mix(h, &field->byte_offset, sizeof(field->byte_offset));
        h = fingerprint_mix_ptr(h, field->type);
        h = fingerprint_mix_ptr(h, field->ns);
        h = fingerprint_mix_ptr(h, field->default_value);
    }
    return h;
}

static uint64_t external_parent_edges_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    (void)seed0;
    (void)seed1;
    return ((const ExternalParentEdges*)item)->fingerprint;
}

static int external_parent_edges_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    uint64_t left = ((const ExternalParentEdges*)a)->fingerprint;
    uint64_t right = ((const ExternalParentEdges*)b)->fingerprint;
    return left < right ? -1 : (left > right ? 1 : 0);
}

// The edge list for one fingerprint in the tree's external table, made on
// first use; NULL when this Input keeps no external edges.
static TypeMapTransition** external_edges_for(Input* input, uint64_t fingerprint) {
    if (!input || !input->keeps_external_edges) return NULL;
    if (!input->external_edges) {
        input->external_edges = hashmap_new(sizeof(ExternalParentEdges), 64, 0, 0,
            external_parent_edges_hash, external_parent_edges_compare, NULL, NULL);
        if (!input->external_edges) return NULL;
    }
    ExternalParentEdges probe = {fingerprint, NULL};
    const ExternalParentEdges* found =
        (const ExternalParentEdges*)hashmap_get(input->external_edges, &probe);
    if (found) return found->edges;
    TypeMapTransition** head = (TypeMapTransition**)type_alloc_zeroed(
        input_tree_alloc(input), sizeof(TypeMapTransition*));
    if (!head) return NULL;
    probe.edges = head;
    hashmap_set(input->external_edges, &probe);
    if (hashmap_oom(input->external_edges)) return NULL;
    return head;
}

// The add edges from `parent`'s structure.
static TypeMapTransition** external_parent_edges(Input* input, const TypeMap* parent) {
    return parent ? external_edges_for(input, external_parent_fingerprint(parent)) : NULL;
}

// D3.4.4v4: whether an edge carries `k`'s name. A JS Symbol or private name is
// its record, never its description bytes. A STRING name is its spelling:
// equal ids prove it at once, while differing or absent ids fall back to the
// bytes -- ids are per name pool, and an unpooled or computed key has none, so
// comparing ids alone split one spelling into one edge per pool.
static bool transition_names_key(const TypeMapTransition* tr, const TransitionKey* k) {
    if (tr->key_kind != k->key_kind) return false;
    if (k->key_kind != NAME_KEY_STRING) {
        return tr->name_id != NAME_ID_NONE && tr->name_id == k->name_id;
    }
    if (tr->name_id != NAME_ID_NONE && tr->name_id == k->name_id) return true;
    return tr->name && tr->name_len == k->name_len &&
        (tr->name_hash == 0 || tr->name_hash == k->name_hash) &&
        memcmp(tr->name, k->name, k->name_len) == 0;
}

// One step through a tree from `parent`, whose outgoing edges are `*edges`.
// For a node the tree owns that is `&parent->transitions`. For an external
// parent (D3.4.3v5) it is the parent's list in the tree's own table, and the
// parent is never written: its chain is copied rather than extended, names
// included, and a cached child must match it in full (see
// external_child_matches_parent).
static TypeMap* transition_target_via(TypeMap* parent, TypeMapTransition** edges,
        bool external, const TransitionKey* k, TypeId type_id, Input* input,
        ShapeEntry** out_entry) {
    if (out_entry) *out_entry = NULL;
    if (!parent || !edges || !k || !input || !input->pool || !input->type_list) return NULL;
    // The list is scanned per add, so it is bounded: a shape with more outgoing
    // edges than this is a dictionary-shaped site (parsed data, per-record
    // keys) where sharing cannot pay for a linear walk on every property.
    // Past the bound the caller keeps its private shape, as before.
    // The shared root is every plain map's first-property node, so its edge
    // count is the number of distinct first properties in the whole program —
    // a far larger budget than an interior shape needs. Capping it at 16 let it
    // saturate after a few hundred objects, after which every map fell back to
    // a private shape and the sharing bought nothing.
    const int MAX_SHAPE_TRANSITIONS = shape_tree_fanout_cap(parent);
    ShapeTreeStats* stats = shape_tree_stats_for(input);
    int transition_count = 0;
    for (TypeMapTransition* tr = *edges; tr; tr = tr->next) {
        transition_count++;
        if (tr->value_type != type_id || tr->flags != 0 || !tr->target ||
                !transition_names_key(tr, k)) continue;
        if (external ? external_child_matches_parent(parent, tr->target)
                     : map_transition_prefix_matches_parent(parent, tr->target)) {
            if (stats) {
                shape_tree_stat_add(external ? &stats->external_hits : &stats->hits, 1);
                shape_tree_stat_add(&stats->edges_walked, (uint64_t)transition_count);
                shape_tree_stat_max(&stats->max_walk, (uint64_t)transition_count);
            }
            if (out_entry) *out_entry = tr->target->last;
            return tr->target;
        }
    }
    if (stats) {
        shape_tree_stat_add(&stats->edges_walked, (uint64_t)transition_count);
        shape_tree_stat_max(&stats->max_walk, (uint64_t)transition_count);
    }

    if (transition_count >= MAX_SHAPE_TRANSITIONS) {
        if (stats) {
            shape_tree_stat_add(external ? &stats->external_declined_fanout
                                         : &stats->declined_fanout, 1);
            int bucket = transition_count <= 16 ? 0 : transition_count <= 64 ? 1
                : transition_count <= 256 ? 2 : 3;
            shape_tree_stat_add(&stats->decline_degree[bucket], 1);
        }
        return NULL;
    }
    bool is_element = parent->type_id == LMD_TYPE_ELEMENT;
    if (!transition_graph_has_room(input, is_element)) {
        if (stats) shape_tree_stat_add(&stats->declined_budget, 1);
        return NULL;
    }

    // an element's node is a TypeElmt: its tag, id and namespace ride along
    TypeAlloc tree = input_tree_alloc(input);
    TypeMap* child = (TypeMap*)alloc_type_in(tree, parent->type_id,
        is_element ? sizeof(TypeElmt) : sizeof(TypeMap));
    if (!child) return NULL;
    if (external) {
        if (!copy_external_identity(tree, child, parent)) return NULL;
    } else if (is_element) {
        TypeElmt* element_parent = (TypeElmt*)parent;
        TypeElmt* element_child = (TypeElmt*)child;
        element_child->name = element_parent->name;
        element_child->name_id = element_parent->name_id;
        element_child->ns = element_parent->ns;
        element_child->content_list = element_parent->content_list;
    }

    // D3.4.3v3: while the parent still owns its chain's tail -- no other child
    // has extended it -- the child extends it in place, so a linear path keeps
    // one chain. Once the tail is taken, this branch copies the prefix. An
    // external parent is never extended (D3.4.3v5).
    bool in_place = !external && parent->last && !parent->last->chain_next;  // SHAPE_CHAIN_OK: tail ownership
    ShapeEntry* first = parent->shape;
    ShapeEntry* prefix_last = parent->last;
    if (!in_place) {
        first = external ? clone_shape_entries_owned(tree, parent, &prefix_last)
                         : clone_shape_chain_for_transition(tree, parent, &prefix_last);
        if (parent->shape && !first) return NULL;
    }
    ShapeEntry* added = k->like
        ? shape_entry_copy_as(tree, k->like, type_id, prefix_last)
        : alloc_named_shape_entry_in(tree, k->name, k->name_len, k->name_id,
            k->key_kind, k->name_hash, type_id, prefix_last);
    if (!added) return NULL;
    added->byte_offset = parent->byte_size;
    added->chain_index = (uint32_t)parent->length;
    if (!first) first = added;

    child->shape = first;
    child->last = added;
    child->length = parent->length + 1;
    child->byte_size = added->byte_offset + shape_entry_storage_size(added);
    child->has_spread = parent->has_spread;  // prefix clone keeps any nameless spread slot
    child->has_named_shape = parent->has_named_shape;
    child->is_trusted_contract = false;
    child->struct_name = parent->struct_name;
    child->is_private_clone = false;
    child->is_shared_constructor_shape = false;
    child->is_transition_shared_shape = true;
    child->transitions = NULL;
    child->js_meta = parent->js_meta;
    child->has_array_index_shape = parent->has_array_index_shape;
    // S2.1.4/OB16: a nominal instance stays an instance of its type through
    // every shape it grows into, as the private path ensures (LR03-8)
    child->nominal = parent->nominal;
    child->is_nominal = parent->is_nominal;

    // D3.4.3v3: an in-place child shares the parent's hash table while the
    // table has room for it and holds no entry with the added field's
    // identity; otherwise it builds its own, which its own in-place children
    // then share.
    if (in_place && parent->field_index &&
            (int)parent->field_capacity >= typemap_hash_recommended_capacity(child->length) &&
            !typemap_hash_holds_equal(parent, added)) {
        child->field_index = parent->field_index;
        child->field_capacity = parent->field_capacity;
        child->field_count = parent->field_count;
        typemap_hash_insert(child, added);
    } else {
        typemap_hash_build_in(child, tree);
    }

    int fixed_slot_count = typemap_fixed_slot_prefix_count(parent);
    if (fixed_slot_count > 0 && in_place) {
        // the fixed prefix is the parent's own entries, so is its slot index
        child->slot_entries = parent->slot_entries;
        child->slot_count = fixed_slot_count;
    } else if (fixed_slot_count > 0) {
        ShapeEntry** entries = (ShapeEntry**)type_alloc_zeroed(tree,
            (size_t)fixed_slot_count * sizeof(ShapeEntry*));
        if (entries) {
            ShapeEntry* e = first;
            for (int i = 0; i < fixed_slot_count && e; i++, e = typemap_next_field(child, e)) {
                entries[i] = e;
            }
            child->slot_entries = entries;
            // A transition can append packed metadata such as __proto__ to a
            // constructor shape.  Keep only the inherited fixed-width prefix;
            // publishing the appended field as a slot would make a later map
            // rebuild repack the constructor's byte offsets.
            child->slot_count = fixed_slot_count;
        }
    }

    arraylist_append(input->type_list, child);
    child->type_index = input->type_list->length - 1;
    transition_graph_count(input, is_element);
    if (stats) {
        shape_tree_stat_add(external ? &stats->external_mints : &stats->mints, 1);
        shape_tree_stat_max(&stats->peak_nodes, (uint64_t)(input->shape_transition_shapes +
            input->element_transition_shapes));
    }

    TypeMapTransition* tr = (TypeMapTransition*)type_alloc_zeroed(tree,
        sizeof(TypeMapTransition));
    if (!tr) return child;
    tr->name_id = added->name_id;
    tr->key_kind = added->key_kind;
    // D3.4.4v4: every STRING edge keeps its spelling, so a key with another
    // pool's id, or none, still finds it; the entry's name lives in this arena
    tr->name = added->key_kind == NAME_KEY_STRING && added->name ? added->name->str : NULL;
    tr->name_len = added->name ? (uint32_t)added->name->length : 0;
    tr->name_hash = added->name_hash;
    tr->value_type = type_id;
    tr->flags = 0;
    tr->target = child;
    tr->next = *edges;
    *edges = tr;

    if (out_entry) *out_entry = added;
    return child;
}

static TypeMap* transition_target_for_key(TypeMap* parent, const TransitionKey* k,
        TypeId type_id, Input* input, ShapeEntry** out_entry) {
    if (!parent) return NULL;
    return transition_target_via(parent, &parent->transitions, false, k, type_id,
        input, out_entry);
}

static TypeMap* map_transition_target_for_add(TypeMap* parent, String* key,
        TypeId type_id, Input* input, ShapeEntry** out_entry) {
    if (out_entry) *out_entry = NULL;
    if (!key) return NULL;
    TransitionKey k = transition_key_of_string(key);
    return transition_target_for_key(parent, &k, type_id, input, out_entry);
}

// The transition graph needs a shared root, otherwise the first add on a fresh
// object mints a private TypeMap and the object never rejoins the graph. The
// root is per-Input so its transitions and their targets share one pool
// lifetime; the global EmptyMap could not hold them safely.
static TypeMap* map_shape_transition_root(Input* input,
        const struct JsClassMeta* js_meta) {
    if (!input || !input->pool) return NULL;
    if (input->shape_transition_root) {
        // js_meta is part of shape identity and every child inherits the
        // root's, so a differently classified blueprint may not share it.
        return input->shape_transition_root->js_meta == js_meta
            ? input->shape_transition_root : NULL;
    }
    TypeMap* root = (TypeMap*)alloc_type_in(input_tree_alloc(input), LMD_TYPE_MAP,
        sizeof(TypeMap));
    if (!root) return NULL;
    root->is_transition_shared_shape = true;
    root->js_meta = js_meta;
    input->shape_transition_root = root;
    return root;
}

// D3.4.3v5: one field added to a runtime-grown map or element through
// `input`'s tree. A NULL `parent` starts at the tree's root; a node of this
// tree follows its own edges; any other admissible type is an external parent
// when the tree keeps external edges -- its edges live in the tree's table,
// keyed by the parent's address, and the parent is never written.
static TypeMap* type_tree_step(Input* input, TypeMap* parent, const TransitionKey* k,
        TypeId type_id, ShapeEntry** out_entry) {
    if (!parent) parent = map_shape_transition_root(input, NULL);
    if (!parent) return NULL;
    if (parent == input->shape_transition_root || type_tree_owns(input, parent)) {
        return transition_target_for_key(parent, k, type_id, input, out_entry);
    }
    if (!external_parent_admissible(parent)) return NULL;
    TypeMapTransition** edges = external_parent_edges(input, parent);
    if (!edges) return NULL;
    return transition_target_via(parent, edges, true, k, type_id, input, out_entry);
}

// Every key kind may take an edge (D3.4.4v4): a JS Symbol or private name is
// matched by its record, as the edge already compares it.
TypeMap* type_tree_add_map_field(Input* input, TypeMap* parent, String* key,
        TypeId type_id, ShapeEntry** out_entry) {
    if (out_entry) *out_entry = NULL;
    if (!input || !key) return NULL;
    TransitionKey k = transition_key_of_string(key);
    return type_tree_step(input, parent, &k, type_id, out_entry);
}

// The same add for a key given by its characters alone: a Lambda Symbol, a
// STRING name in the global namespace (S8.2.2v4).
TypeMap* type_tree_add_map_field_chars(Input* input, TypeMap* parent,
        const char* chars, uint32_t len, TypeId type_id, ShapeEntry** out_entry) {
    if (out_entry) *out_entry = NULL;
    if (!input || !chars) return NULL;
    TransitionKey k = transition_key_of_chars(chars, len);
    return type_tree_step(input, parent, &k, type_id, out_entry);
}

// Marks a retype edge (Impl_Map_Transition_Coverage P2); an add lookup only
// ever follows edges with no flags, so the two never match each other.
static const uint8_t TYPE_TREE_RETYPE_EDGE = 0x80;

// Impl_Map_Transition_Coverage P2 (D3.4.5 through the tree): the type a map
// takes when the value of one field changes kind -- `parent`'s fields in
// order, the one at `field` laid out for `value_type`, every other field
// keeping its full contract (a `number` or union contract is not its TypeId,
// so the path cannot simply be replayed from the root). The target is minted
// once per parent structure and shared by every map retyped the same way;
// its own adds then follow its edges like any node's. NULL when the tree
// declines (an inadmissible parent, the fan-out cap, the budget).
TypeMap* type_tree_retype_field(Input* input, TypeMap* parent, const ShapeEntry* field,
        TypeId value_type) {
    if (!input || !input->keeps_external_edges || !parent || !field ||
            !input->pool || !input->type_list) return NULL;
    if (!external_parent_admissible(parent)) return NULL;
    int64_t position = 0;
    bool found = false;
    FOR_EACH_MAP_FIELD(parent, entry) {
        if (entry == field) { found = true; break; }
        position++;
    }
    if (!found) return NULL;
    const Type* retyped_to = type_info[value_type].type;
    uint8_t op[sizeof(int64_t) + 2] = {TYPE_TREE_RETYPE_EDGE, value_type};
    memcpy(op + 2, &position, sizeof(position));
    uint64_t fingerprint = fingerprint_mix(external_parent_fingerprint(parent), op, sizeof(op));
    TypeMapTransition** edges = external_edges_for(input, fingerprint);
    if (!edges) return NULL;

    ShapeTreeStats* stats = shape_tree_stats_for(input);
    const int MAX_SHAPE_TRANSITIONS = shape_tree_fanout_cap(parent);
    int transition_count = 0;
    for (TypeMapTransition* tr = *edges; tr; tr = tr->next) {
        transition_count++;
        if (tr->flags == TYPE_TREE_RETYPE_EDGE && tr->value_type == value_type && tr->target &&
                retype_target_matches_parent(parent, tr->target, position, retyped_to)) {
            if (stats) shape_tree_stat_add(&stats->external_hits, 1);
            return tr->target;
        }
    }
    if (transition_count >= MAX_SHAPE_TRANSITIONS) {
        if (stats) shape_tree_stat_add(&stats->external_declined_fanout, 1);
        return NULL;
    }
    bool is_element = parent->type_id == LMD_TYPE_ELEMENT;
    if (!transition_graph_has_room(input, is_element)) {
        if (stats) shape_tree_stat_add(&stats->declined_budget, 1);
        return NULL;
    }

    TypeAlloc tree = input_tree_alloc(input);
    TypeMap* target = (TypeMap*)alloc_type_in(tree, parent->type_id,
        is_element ? sizeof(TypeElmt) : sizeof(TypeMap));
    if (!target || !copy_external_identity(tree, target, parent)) return NULL;
    ShapeEntry* last = NULL;
    ShapeEntry* first = clone_shape_entries_owned(tree, parent, &last);
    if (parent->shape && !first) return NULL;
    // D3.4.5: the layout is repacked from the start, the retyped field at its
    // new width, as map_rebuild_for_type_change packs a private chain
    int64_t offset = 0;
    int64_t index = 0;
    for (ShapeEntry* e = first; e; e = shape_chain_next_until(e, last), index++) {
        if (index == position) shape_entry_set_type(e, (Type*)retyped_to);
        e->byte_offset = offset;
        offset += shape_entry_storage_size(e);
    }
    target->shape = first;
    target->last = last;
    target->length = parent->length;
    target->byte_size = offset;
    target->is_trusted_contract = false;
    target->is_private_clone = false;
    target->is_shared_constructor_shape = false;
    target->is_transition_shared_shape = true;
    target->transitions = NULL;
    typemap_hash_build_in(target, tree);
    arraylist_append(input->type_list, target);
    target->type_index = input->type_list->length - 1;
    transition_graph_count(input, is_element);
    if (stats) {
        shape_tree_stat_add(&stats->external_mints, 1);
        shape_tree_stat_max(&stats->peak_nodes, (uint64_t)(input->shape_transition_shapes +
            input->element_transition_shapes));
    }

    TypeMapTransition* tr = (TypeMapTransition*)type_alloc_zeroed(tree,
        sizeof(TypeMapTransition));
    if (!tr) return target;
    tr->value_type = value_type;
    tr->flags = TYPE_TREE_RETYPE_EDGE;
    tr->target = target;
    tr->next = *edges;
    *edges = tr;
    return target;
}

bool type_tree_owns(const Input* input, const TypeMap* type) {
    if (!input || !input->type_list || !type || type->type_index < 0 ||
            type->type_index >= input->type_list->length) {
        return false;
    }
    return input->type_list->data[type->type_index] == type;
}

static ShapeEntry* map_existing_shape_entry(TypeMap* map_type, String* key) {
    if (!map_type || !key) return NULL;
    NameId key_id = property_key_id(key);
    if (key_id != NAME_ID_NONE) {
        ShapeEntry* hit = typemap_hash_lookup_by_name_id(map_type, key_id,
            property_key_hash(key));
        // D3.4.4v4: an id proves a STRING name equal, never different, so a
        // miss falls back to the spelling, which an id-less entry carries
        if (hit || property_key_requires_identity(key)) return hit;
    }
    return typemap_hash_lookup(map_type, key->chars, (int)key->len);
}

// One add through the shared shape graph. Returns true when this call owns the
// outcome — the value was stored, or capacity growth failed and the caller must
// give up exactly as the private path does.
static bool map_put_via_shape_transition(Map** mp_ref, TypeMap* parent,
        String* key, Item value, TypeId type_id, Input* input,
        int64_t min_byte_end, MapDataGrowFn grow, void* grow_context) {
    ShapeEntry* entry = NULL;
    TypeMap* target = map_transition_target_for_add(parent, key, type_id, input,
        &entry);
    if (!target || !entry) return false;
    int64_t byte_end = entry->byte_offset + type_info[type_id].byte_size;
    if (byte_end < min_byte_end) byte_end = min_byte_end;
    String* keys[1] = {key};
    Item values[1] = {value};
    Map* mp = *mp_ref;
    if (!map_ensure_data_capacity_for_end(&mp, input->pool, byte_end,
            parent->byte_size, grow, grow_context, keys, 1, values, 1)) {
        return true;
    }
    *mp_ref = mp;
    mp->type = target;
    map_store_field_value((char*)mp->data + entry->byte_offset, type_id,
        values[0]);
    return true;
}

// Internal helper function - not exported in header but accessible to mark_builder.cpp
void map_put_with_data_growth(Map* mp, String* key, Item value, Input *input,
        MapDataGrowFn grow, void* grow_context) {
    // note: key could be null for nested map
    TypeMap *map_type = (TypeMap*)mp->type;
    TypeId type_id = get_type_id(value);
    bool array_index_shape = map_kind_is_array_props(mp->map_kind) &&
        map_key_is_array_index_name(key);
    if (map_type == &EmptyMap) {
        const struct JsClassMeta* js_meta = map_type->js_meta;
        // An ordinary map starts at the shared root, so all objects built by
        // the same sequence of adds end up on one TypeMap. The root carries the
        // blueprint's own js_meta — EmptyMap's is JS_CLASS_OBJECT once the JS
        // metadata is initialized — because children inherit it and it is part
        // of shape identity; gating on `!js_meta` instead made this path dead
        // for every JS object and silently dropped their class brand.
        // Every key kind may take an edge (D3.4.4v4, NI18): a JS Symbol or
        // private name is matched by its record, never by its description.
        if (!array_index_shape && key && mp->map_kind == MAP_KIND_PLAIN) {
            TypeMap* root = map_shape_transition_root(input, js_meta);
            if (root && map_put_via_shape_transition(&mp, root, key, value,
                    type_id, input, 64, grow, grow_context)) {
                return;
            }
        }
        // alloc map type and data chunk
        if (ShapeTreeStats* stats = shape_tree_stats_for(input)) {
            shape_tree_stat_add(&stats->private_types, 1);
        }
        map_type = (TypeMap*)alloc_type(input->pool, LMD_TYPE_MAP, sizeof(TypeMap));
        if (!map_type) { return; }
        map_type->js_meta = js_meta;
        mp->type = map_type;
        arraylist_append(input->type_list, map_type);
        map_type->type_index = input->type_list->length - 1;
        map_type->has_array_index_shape = array_index_shape;
        String* keys[1] = {key};
        Item values[1] = {value};
        if (!map_ensure_data_capacity_for_end(&mp, input->pool, 64, 0,
                grow, grow_context, keys, 1, values, 1)) return;
        key = keys[0];
        value = values[0];
    } else if (typemap_is_shared_shape(map_type)) {
        if (key && mp->map_kind == MAP_KIND_PLAIN &&
                map_put_via_shape_transition(&mp, map_type, key, value, type_id,
                    input, 0, grow, grow_context)) {
            return;
        }
        // transition lookup is the fast path; if no compatible transition
        // exists, clone before appending a new ShapeEntry to this map only.
        if (ShapeTreeStats* stats = shape_tree_stats_for(input)) {
            shape_tree_stat_add(&stats->private_types, 1);
        }
        TypeMap* clone = map_clone_typemap_for_mutation(mp, input);
        if (clone) map_type = clone;
    }

    ShapeEntry* existing = map_existing_shape_entry(map_type, key);
    if (existing && existing->byte_offset < 0) {
        // A virtual JS accessor becomes a data property only through a
        // DefineOwn storage write. Materialize its first physical lane here;
        // accessor cells themselves are never written to Map::data.
        int bsize = type_info[type_id].byte_size;
        int64_t byte_offset = map_type->byte_size;
        int64_t byte_end = byte_offset + bsize;
        if (bsize <= 0 || byte_end > INT_MAX) return;
        String* keys[1] = {key};
        Item values[1] = {value};
        if (!map_ensure_data_capacity_for_end(&mp, input->pool, byte_end,
                map_type->byte_size, grow, grow_context, keys, 1, values, 1)) {
            return;
        }
        shape_entry_set_type(existing, type_info[type_id].type);
        existing->byte_offset = byte_offset;
        existing->accessor = NULL;
        existing->flags &= (uint8_t)~(JSPD_IS_ACCESSOR | JSPD_DELETED);
        map_type->byte_size = byte_end;
        map_store_field_value((char*)mp->data + byte_offset, type_id, values[0]);
        return;
    }

    ShapeEntry* shape_entry = alloc_shape_entry(input->pool, key, type_id, map_type->last);
    // Type-changing rebuilds can repack earlier fields, so `last + sizeof(last)`
    // is not authoritative for appends; new fields must start at byte_size.
    shape_entry->byte_offset = map_type->byte_size;
    if (!map_type->shape) { map_type->shape = shape_entry; }
    map_type->last = shape_entry;
    map_type->length++;
    if (array_index_shape) map_type->has_array_index_shape = true;

    // A1: populate/grow property hash table for O(1) property lookup
    typemap_hash_insert_owned(map_type, shape_entry, input->pool);

    // ensure data capacity
    int bsize = type_info[type_id].byte_size;
    int64_t byte_offset64 = shape_entry->byte_offset + bsize;
    if (byte_offset64 > INT_MAX) return;
    int byte_offset = (int)byte_offset64;
    String* keys[1] = {key};
    Item values[1] = {value};
    if (!map_ensure_data_capacity_for_end(&mp, input->pool, byte_offset,
            byte_offset - bsize, grow, grow_context, keys, 1, values, 1)) return;
    key = keys[0];
    value = values[0];
    map_type->byte_size = byte_offset;

    // store the value
    void* field_ptr = (char*)mp->data + byte_offset - bsize;
    map_store_field_value(field_ptr, type_id, value);
}

void map_put(Map* mp, String* key, Item value, Input *input) {
    map_put_with_data_growth(mp, key, value, input, NULL, NULL);
}

bool map_put_undefined_unique_absent_bulk_with_data_growth(Map* mp,
        String** keys, int count, Input* input, uint8_t shape_flags,
        MapDataGrowFn grow, void* grow_context) {
    if (!mp || !keys || count <= 0 || !input || !input->pool) return false;
    for (int i = 0; i < count; i++) {
        if (!keys[i]) return false;
    }

    TypeMap *map_type = (TypeMap*)mp->type;
    if (map_type == &EmptyMap) {
        const struct JsClassMeta* js_meta = map_type->js_meta;
        map_type = (TypeMap*)alloc_type(input->pool, LMD_TYPE_MAP, sizeof(TypeMap));
        if (!map_type) return false;
        map_type->js_meta = js_meta;
        mp->type = map_type;
        arraylist_append(input->type_list, map_type);
        map_type->type_index = input->type_list->length - 1;
    }
    if (!map_type) return false;
    if (typemap_is_shared_shape(map_type)) {
        // bulk appends still mutate the shape chain, so they need the same
        // detach behavior as single-property map_put.
        TypeMap* clone = map_clone_typemap_for_mutation(mp, input);
        if (clone) map_type = clone;
    }

    int bsize = type_info[LMD_TYPE_UNDEFINED].byte_size;
    int64_t old_byte_size = map_type->byte_size;
    int64_t new_byte_size = old_byte_size + ((int64_t)bsize * count);
    if (!mp->data || new_byte_size > mp->data_cap) {
        if (!map_ensure_data_capacity_for_end(&mp, input->pool, new_byte_size,
                old_byte_size, grow, grow_context, keys, count,
                NULL, 0)) return false;
    }

    ShapeEntry* prev = map_type->last;
    int64_t byte_offset = old_byte_size;
    for (int i = 0; i < count; i++) {
        ShapeEntry* shape_entry = alloc_shape_entry(input->pool, keys[i],
            LMD_TYPE_UNDEFINED, prev);
        if (!shape_entry) return false;
        shape_entry->byte_offset = byte_offset;
        shape_entry->flags = shape_flags;
        if (!map_type->shape) map_type->shape = shape_entry;
        map_type->last = shape_entry;
        map_type->length++;
        typemap_hash_insert_owned(map_type, shape_entry, input->pool);
        if (mp->data) {
            *(bool*)((char*)mp->data + byte_offset) = false;
        }
        byte_offset += bsize;
        prev = shape_entry;
    }
    map_type->byte_size = byte_offset;
    if (shape_flags != 0 && mp->map_kind == MAP_KIND_PLAIN) {
        mp->map_kind = MAP_KIND_DESC;
    }
    return true;
}

bool map_put_undefined_unique_absent_bulk(Map* mp, String** keys, int count,
        Input* input, uint8_t shape_flags) {
    return map_put_undefined_unique_absent_bulk_with_data_growth(mp, keys,
        count, input, shape_flags, NULL, NULL);
}

extern TypeElmt EmptyElmt;
static void elmt_store_value(void* field_ptr, TypeId type_id, Item value) {
    if (store_common_field_value(field_ptr, type_id, value)) return;
    switch (type_id) {
    default:
        log_debug("elmt_store_value: unknown type %d", type_id);
    }
}

// An attribute already present with this name and value type: ElementReader
// walks shape order while dynamic lookup is hashed, so a same-typed duplicate
// key overwrites instead of appending, or the two APIs would disagree.
static ShapeEntry* elmt_same_typed_attr(TypeElmt* elmt_type, String* key, TypeId type_id) {
    // D3.4.4v4: a JS Symbol or private name is its record; only a STRING name
    // is matched by its spelling
    bool identity = property_key_requires_identity(key);
    NameId key_id = identity ? name_ref_id(key) : NAME_ID_NONE;
    FOR_EACH_MAP_FIELD(elmt_type, field) {
        if (!field->name || field->type->type_id != type_id) continue;
        bool same = identity
            ? field->key_kind == property_key_kind(key) && field->name_id == key_id
            : field->key_kind == NAME_KEY_STRING && strview_equal(field->name, key->chars);
        if (same) return field;
    }
    return NULL;
}

// Grow an element's attribute buffer to hold `byte_end` bytes, keeping the
// first `used_bytes`.
static bool elmt_ensure_data_capacity(Element* elmt, int64_t byte_end,
        int64_t used_bytes, Pool* pool) {
    if (byte_end <= elmt->data_cap) return true;
    if (byte_end > INT_MAX / 2) return false;
    // elmt->data_cap could be 0
    int byte_cap = (int)MAX((int64_t)elmt->data_cap, byte_end) * 2;
    void* new_data = pool_calloc(pool, byte_cap);
    if (!new_data) return false;
    if (elmt->data) {
        memcpy(new_data, elmt->data, (size_t)used_bytes);
        pool_free(pool, elmt->data);
    }
    elmt->data = new_data;  elmt->data_cap = byte_cap;
    return true;
}

void elmt_put(Element* elmt, String* key, Item value, Pool* pool) {
    assert(elmt->type != &EmptyElmt);
    TypeId type_id = get_type_id(value);
    TypeElmt* elmt_type = (TypeElmt*)elmt->type;
    if (ShapeEntry* field = elmt_same_typed_attr(elmt_type, key, type_id)) {
        elmt_store_value((char*)elmt->data + field->byte_offset, type_id, value);
        return;
    }
    if (typemap_is_shared_shape((TypeMap*)elmt_type) ||
            (elmt_type->shape && !elmt_type->is_private_clone)) {
        // A transition-tree type is shared by every element built the same
        // way (D3.4.3v3), and a pooled chain by every element with the same
        // attributes: an append here must not reach either, so detach first.
        TypeElmt* clone = elmt_clone_type_for_mutation(elmt, pool);
        if (!clone) return;
        elmt_type = clone;
    }
    ShapeEntry* shape_entry = alloc_shape_entry(pool, key, type_id, elmt_type->last);
    if (!elmt_type->shape) { elmt_type->shape = shape_entry; }
    elmt_type->last = shape_entry;
    elmt_type->length++;

    // ensure data capacity
    int bsize = type_info[type_id].byte_size;
    int byte_offset = shape_entry->byte_offset + bsize;
    if (!elmt_ensure_data_capacity(elmt, byte_offset, byte_offset - bsize, pool)) return;
    elmt_type->byte_size = byte_offset;

    // store the value
    void* field_ptr = (char*)elmt->data + byte_offset - bsize;
    elmt_store_value(field_ptr, type_id, value);
}

// D3.4.3v3: an element's type starts at its tag's root in the Input's
// transition tree. Pooled tag names are unique pointers, so a root is found by
// the (name, namespace) pointer pair in a small pool-owned table. Returns NULL
// when the name is not pooled or the element budget is spent; the caller then
// keeps a private type.
static uint64_t elmt_root_slot_hash(const char* name, const Target* ns) {
    uint64_t h = (uint64_t)(uintptr_t)name * 0x9E3779B97F4A7C15ull;
    return h ^ ((uint64_t)(uintptr_t)ns * 0xC2B2AE3D27D4EB4Full);
}

static void elmt_root_table_insert(TypeElmt** table, int cap, TypeElmt* root) {
    uint64_t mask = (uint64_t)cap - 1;
    for (uint64_t i = elmt_root_slot_hash(root->name.str, root->ns) & mask;;
            i = (i + 1) & mask) {
        if (!table[i]) { table[i] = root; return; }
    }
}

static TypeElmt* elmt_tree_root_find(Input* input, const char* name, const Target* ns) {
    if (!input || !input->element_root_cap || !name) return NULL;
    uint64_t mask = (uint64_t)input->element_root_cap - 1;
    for (uint64_t i = elmt_root_slot_hash(name, ns) & mask;; i = (i + 1) & mask) {
        TypeElmt* root = input->element_roots[i];
        if (!root) return NULL;
        if (root->name.str == name && root->ns == ns) return root;
    }
}

TypeElmt* elmt_tree_root(Input* input, String* tag_name, Target* ns) {
    if (!input || !input->pool || !input->type_list || !tag_name ||
            !string_is_pooled(tag_name)) return NULL;
    const char* name = tag_name->chars;
    if (TypeElmt* root = elmt_tree_root_find(input, name, ns)) return root;
    if (!transition_graph_has_room(input, true)) return NULL;
    // Keep the table at most half full. Growing replaces it and frees the old
    // one, so the table is a pool block, unlike the roots it indexes (D4.1.4v4).
    if ((input->element_root_count + 1) * 2 > input->element_root_cap) {
        int cap = input->element_root_cap ? input->element_root_cap * 2 : 64;
        TypeElmt** table = (TypeElmt**)pool_calloc(input->pool, sizeof(TypeElmt*) * (size_t)cap);
        if (!table) return NULL;
        TypeElmt** old_table = input->element_roots;
        for (int i = 0; i < input->element_root_cap; i++) {
            if (old_table[i]) elmt_root_table_insert(table, cap, old_table[i]);
        }
        input->element_roots = table;
        input->element_root_cap = cap;
        if (old_table) pool_free(input->pool, old_table);
    }
    TypeElmt* root = (TypeElmt*)alloc_type_in(input_tree_alloc(input), LMD_TYPE_ELEMENT,
        sizeof(TypeElmt));
    if (!root) return NULL;
    root->name.str = name;
    root->name.length = tag_name->len;
    root->name_id = name_ref_id(tag_name);
    root->ns = ns;
    root->is_transition_shared_shape = true;
    arraylist_append(input->type_list, root);
    root->type_index = input->type_list->length - 1;
    transition_graph_count(input, true);
    elmt_root_table_insert(input->element_roots, input->element_root_cap, root);
    input->element_root_count++;
    return root;
}

// D3.4.3v3: add an attribute through the Input's transition tree, exactly as
// map_put adds a field. A tree-typed element follows or mints the edge and only
// its own data buffer changes; without a usable edge, elmt_put detaches it onto
// a private type and appends there.
void elmt_put_tree(Element* elmt, String* key, Item value, Input* input) {
    TypeElmt* elmt_type = (TypeElmt*)elmt->type;
    if (input && key && elmt_type && elmt_type->is_transition_shared_shape) {
        TypeId type_id = get_type_id(value);
        if (ShapeEntry* field = elmt_same_typed_attr(elmt_type, key, type_id)) {
            elmt_store_value((char*)elmt->data + field->byte_offset, type_id, value);
            return;
        }
        ShapeEntry* entry = NULL;
        TypeMap* target = map_transition_target_for_add((TypeMap*)elmt_type, key,
            type_id, input, &entry);
        if (target && entry) {
            int64_t byte_end = entry->byte_offset + type_info[type_id].byte_size;
            if (!elmt_ensure_data_capacity(elmt, byte_end, elmt_type->byte_size,
                    input->pool)) return;
            elmt->type = target;
            elmt_store_value((char*)elmt->data + entry->byte_offset, type_id, value);
            return;
        }
    }
    elmt_put(elmt, key, value, input ? input->pool : NULL);
}

// D3.4.3v3: the tree root a container's rebuilt type starts from — the plain
// map root, or the element's tag root. An element root exists only for a
// pooled tag, so it is found, never made; NULL keeps the container private.
TypeMap* type_tree_root_like(Input* input, Map* container) {
    if (!input || !container || !container->type) return NULL;
    TypeMap* type = (TypeMap*)container->type;
    if (container->type_id == LMD_TYPE_ELEMENT) {
        return (TypeMap*)elmt_tree_root_find(input, ((TypeElmt*)type)->name.str,
            ((TypeElmt*)type)->ns);
    }
    if (container->type_id == LMD_TYPE_MAP && container->map_kind == MAP_KIND_PLAIN) {
        return map_shape_transition_root(input, type->js_meta);
    }
    return NULL;
}

// The tree node reached from `start` by adding `steps` in order, minting the
// edges that are missing. Named data fields of every key kind take edges, as
// in map_put (D3.4.4v4); NULL when the tree declines any step.
TypeMap* type_tree_follow(Input* input, TypeMap* start, const TypeTreeStep* steps, int count) {
    TypeMap* node = start;
    for (int i = 0; node && i < count; i++) {
        const TypeTreeStep* step = &steps[i];
        TransitionKey k;
        if (step->like) {
            if (!step->like->name || step->like->flags != 0) return NULL;
            k = transition_key_of_entry(step->like);
        } else {
            if (!step->key) return NULL;
            k = transition_key_of_string(step->key);
        }
        node = transition_target_for_key(node, &k, step->type_id, input, NULL);
    }
    return node;
}

typedef void (*InputParserFn)(Input* input, const char* source);

struct MimeParserMapping {
    const char* mime_type;
    const char* parser_type;
};

struct MarkupFlavorMapping {
    const char* flavor;
    MarkupFormat format;
};

struct InputParserMapping {
    const char* type;
    InputParserFn parse;
};

static void parse_markdown_input(Input* input, const char* source) {
    input->root = input_markup_modular(input, source);
}

static void parse_rst_input(Input* input, const char* source) {
    input->root = input_markup_with_format(input, source, MARKUP_RST);
}

static void parse_wiki_input(Input* input, const char* source) {
    input->root = input_markup_with_format(input, source, MARKUP_WIKI);
}

static void parse_asciidoc_input(Input* input, const char* source) {
    input->root = input_markup_with_format(input, source, MARKUP_ASCIIDOC);
}

static void parse_man_input(Input* input, const char* source) {
    input->root = input_markup_with_format(input, source, MARKUP_MAN);
}

static void parse_org_input(Input* input, const char* source) {
    input->root = input_markup_with_format(input, source, MARKUP_ORG);
}

static void parse_typst_input(Input* input, const char* source) {
    input->root = input_markup_with_format(input, source, MARKUP_TYPST);
}

static void parse_textile_input(Input* input, const char* source) {
    input->root = input_markup_modular(input, source);
}

static void parse_html_input(Input* input, const char* source) {
    Element* doc = html5_parse(input, source);
    input->root = (Item){.element = doc};
}

#ifndef LAMBDA_NO_LATEX
static void parse_latex_input(Input* input, const char* source) {
    parse_latex_direct(input, source);
}
#endif

#ifndef LAMBDA_NO_LATEX
static void parse_tikz_input(Input* input, const char* source) {
    parse_tikz_direct(input, source);
}
#endif

static void parse_mdx_input(Input* input, const char* source) {
    input->root = input_mdx(input, source);
}

static const MimeParserMapping MIME_PARSER_MAPPINGS[] = {
    {"application/json", "json"},
    {"text/csv", "csv"},
    {"text/tab-separated-values", "tsv"},
    {"application/xml", "xml"},
    {"text/html", "html"},
    {"text/markdown", "markdown"},
    {"text/mdx", "mdx"},
    {"text/x-rst", "rst"},
    {"application/rtf", "rtf"},
    {"application/pdf", "pdf"},
    {"application/x-tex", "latex"},
    {"application/x-latex", "latex"},
    {"text/x-pgf", "tikz"},
    {"application/toml", "toml"},
    {"application/x-yaml", "yaml"},
    {"text/x-java-properties", "properties"},
    {"application/x-java-properties", "properties"},
    {"message/rfc822", "eml"},
    {"application/eml", "eml"},
    {"message/eml", "eml"},
    {"text/vcard", "vcf"},
    {"text/calendar", "ics"},
    {"application/ics", "ics"},
    {"text/textile", "textile"},
    {"application/textile", "textile"},
    {"text/x-org", "org"},
    {"text/x-asciidoc", "asciidoc"},
    {"text/x-wiki", "wiki"},
    {"text/troff", "man"},
    {"text/typst", "typst"},
    {"application/typst", "typst"},
    {"text/x-mark", "mark"},
    {"application/x-mark", "mark"},
    {"text/css", "css"},
    {"application/css", "css"},
};

static const MarkupFlavorMapping MARKUP_FLAVOR_MAPPINGS[] = {
    {"rst", MARKUP_RST},
    {"wiki", MARKUP_WIKI},
    {"asciidoc", MARKUP_ASCIIDOC},
    {"adoc", MARKUP_ASCIIDOC},
    {"man", MARKUP_MAN},
    {"org", MARKUP_ORG},
    {"textile", MARKUP_TEXTILE},
};

static const InputParserMapping INPUT_PARSER_MAPPINGS[] = {
    {"json", parse_json},
    {"csv", parse_csv},
    {"tsv", parse_tsv},
    {"ini", parse_ini},
    {"properties", parse_properties},
    {"toml", parse_toml},
    {"yaml", parse_yaml},
    {"xml", parse_xml},
    {"markdown", parse_markdown_input},
    {"rst", parse_rst_input},
    {"html", parse_html_input},
    {"html5", parse_html_input},
#ifndef LAMBDA_NO_LATEX
    {"latex", parse_latex_input},
    {"latex-ts", parse_latex_input},
#endif
#ifndef LAMBDA_NO_LATEX
    {"tikz", parse_tikz_input},
#endif
    {"rtf", parse_rtf},
    {"wiki", parse_wiki_input},
    {"asciidoc", parse_asciidoc_input},
    {"adoc", parse_asciidoc_input},
    {"man", parse_man_input},
    {"eml", parse_eml},
    {"vcf", parse_vcf},
    {"ics", parse_ics},
    {"textile", parse_textile_input},
    {"mark", parse_mark},
    {"org", parse_org_input},
    {"typst", parse_typst_input},
#ifndef LAMBDA_NO_CSS_INPUT
    {"css", parse_css},
#endif
    {"jsx", parse_jsx},
    {"mdx", parse_mdx_input},
};

const char* input_detect_structurizr_flavor(const char* pathname,
                                            const char* source,
                                            size_t source_len) {
    if (!pathname || !source) return NULL;
    size_t path_len = strlen(pathname);
    if (str_iends_with_const(pathname, path_len, ".structurizr")) return "structurizr";
    if (!str_iends_with_const(pathname, path_len, ".dsl")) return NULL;

    const char* cursor = source;
    const char* end = source + source_len;
    if ((size_t)(end - cursor) >= 3 &&
            (unsigned char)cursor[0] == 0xEF &&
            (unsigned char)cursor[1] == 0xBB &&
            (unsigned char)cursor[2] == 0xBF) {
        cursor += 3;
    }
    while (cursor < end) {
        if (input_is_whitespace_char(*cursor)) {
            cursor++;
        } else if ((size_t)(end - cursor) >= 2 && cursor[0] == '/' && cursor[1] == '/') {
            cursor += 2;
            while (cursor < end && *cursor != '\n' && *cursor != '\r') cursor++;
        } else if (*cursor == '#') {
            while (cursor < end && *cursor != '\n' && *cursor != '\r') cursor++;
        } else if ((size_t)(end - cursor) >= 2 && cursor[0] == '/' && cursor[1] == '*') {
            cursor += 2;
            while ((size_t)(end - cursor) >= 2 &&
                    !(cursor[0] == '*' && cursor[1] == '/')) cursor++;
            if ((size_t)(end - cursor) < 2) return NULL;
            cursor += 2;
        } else {
            break;
        }
    }
    static const char keyword[] = "workspace";
    size_t keyword_len = sizeof(keyword) - 1;
    if ((size_t)(end - cursor) < keyword_len ||
            memcmp(cursor, keyword, keyword_len) != 0) return NULL;
    const char* after = cursor + keyword_len;
    return after == end || input_is_whitespace_char(*after) || *after == '{'
        ? "structurizr" : NULL;
}

const char* input_detect_graph_flavor(const char* pathname,
                                      const char* source,
                                      size_t source_len) {
    if (!pathname) return NULL;
    size_t path_len = strlen(pathname);
    if (str_iends_with_const(pathname, path_len, ".mmd")) return "mermaid";
    if (str_iends_with_const(pathname, path_len, ".d2")) return "d2";
    if (str_iends_with_const(pathname, path_len, ".dot") ||
            str_iends_with_const(pathname, path_len, ".gv")) return "dot";
    if (str_iends_with_const(pathname, path_len, ".structurizr")) return "structurizr";
    return input_detect_structurizr_flavor(pathname, source, source_len);
}

bool graph_path_is_graph(const char* graph_file) {
    if (!graph_file) return false;
    if (input_detect_graph_flavor(graph_file, NULL, 0)) return true;
    // extension-less sources are recognized by content sniffing
    char* source = read_text_file(graph_file);
    if (!source) return false;
    bool is_graph = input_detect_graph_flavor(graph_file, source, strlen(source)) != NULL;
    mem_free(source);
    return is_graph;
}

static bool markup_flavor_to_format(const char* flavor, MarkupFormat* format) {
    for (size_t i = 0; i < sizeof(MARKUP_FLAVOR_MAPPINGS) / sizeof(MARKUP_FLAVOR_MAPPINGS[0]); i++) {
        if (strcmp(flavor, MARKUP_FLAVOR_MAPPINGS[i].flavor) == 0) {
            *format = MARKUP_FLAVOR_MAPPINGS[i].format;
            return true;
        }
    }
    return false;
}

static bool dispatch_exact_input_parser(const char* effective_type, Input* input, const char* source) {
    for (size_t i = 0; i < sizeof(INPUT_PARSER_MAPPINGS) / sizeof(INPUT_PARSER_MAPPINGS[0]); i++) {
        if (strcmp(effective_type, INPUT_PARSER_MAPPINGS[i].type) == 0) {
            INPUT_PARSER_MAPPINGS[i].parse(input, source);
            return true;
        }
    }
    return false;
}

static const char* mime_to_parser_type(const char* mime_type) {
    if (!mime_type) return "text";

    for (size_t i = 0; i < sizeof(MIME_PARSER_MAPPINGS) / sizeof(MIME_PARSER_MAPPINGS[0]); i++) {
        if (strcmp(mime_type, MIME_PARSER_MAPPINGS[i].mime_type) == 0) {
            return MIME_PARSER_MAPPINGS[i].parser_type;
        }
    }

    // Check for XML-based formats
    if (strstr(mime_type, "+xml") || strstr(mime_type, "xml")) return "xml";

    // Check for text formats
    if (strstr(mime_type, "text/") == mime_type) {
        // Handle specific text subtypes
        if (strstr(mime_type, "x-c") || strstr(mime_type, "x-java") ||
            strstr(mime_type, "javascript") || strstr(mime_type, "x-python")) {
            return "text";
        }
        if (strstr(mime_type, "ini")) return "ini";
        return "text";
    }

    // For unsupported formats, default to text if it might be readable
    if (strstr(mime_type, "application/") == mime_type) {
        if (strstr(mime_type, "javascript") || strstr(mime_type, "typescript") ||
            strstr(mime_type, "x-sh") || strstr(mime_type, "x-bash")) {
            return "text";
        }
    }

    // Default fallback
    return "text";
}

static Input* input_from_source_n_with_name_parent(const char* source,
        size_t source_len, Url* abs_url, String* type, String* flavor,
        NamePool* name_parent, const InputParseOptions* options = NULL) {
    log_debug("input_from_source_n: ENTRY type='%s', flavor='%s', len=%zu",
              type ? type->chars : "null",
              flavor ? flavor->chars : "null",
              source_len);
    const char* effective_type = NULL;
    const char* pathname = abs_url && abs_url->pathname
        ? abs_url->pathname->chars : "";
    // Explicit `graph` inputs still need extension/content flavor detection;
    // limiting detection to `auto` silently sent flavorless .dsl files to DOT.
    const char* detected_graph_flavor = flavor ? NULL
        : input_detect_graph_flavor(pathname, source, source_len);
    // Determine the effective type to use
    if (!type || strcmp(type->chars, "auto") == 0) {
        // in-memory auto inputs may omit a URL, so content detection must stay null-safe.
        if (detected_graph_flavor) effective_type = "graph";
        // Auto-detect MIME type
        MimeDetector* detector = effective_type ? NULL : mime_detector_init();
        if (!effective_type && detector) {
            const char* detected_mime = detect_mime_type(detector, pathname, source, source_len);
            if (detected_mime) {
                effective_type = mime_to_parser_type(detected_mime);
                log_debug("Auto-detected MIME type: %s -> parser type: %s\n", detected_mime, effective_type);
            } else {
                effective_type = "text";
                log_debug("MIME detection failed, defaulting to text\n");
            }
            mime_detector_destroy(detector);
        } else if (!effective_type) {
            effective_type = "text";
            log_debug("Failed to initialize MIME detector, defaulting to text\n");
        }
    } else {
        effective_type = type->chars;
    }

    log_debug("input_from_source: effective_type='%s'", effective_type ? effective_type : "null");

    Input* input = NULL;
    if (!effective_type || strcmp(effective_type, "text") == 0) { // treat as plain text
        // Use InputManager to properly set up the Input with a pool
        input = InputManager::create_input_with_name_parent(abs_url, name_parent);
        if (!input) {
            log_error("input_from_source: Failed to create input for plain text");
            return NULL;
        }
        // Allocate string from the pool instead of malloc
        String *str = create_string(input->pool, source);
        input->root = {.item = s2it(str)};
    }
    else {
        InputAllocationContext allocation_context = {};
        InputAllocationContext* saved_allocation_context = input_allocation_context;
        input = InputManager::create_input_with_name_parent(abs_url, name_parent);
        if (!input) {
            log_error("input_from_source: Failed to create input for type '%s'", effective_type);
            return NULL;
        }
        input->source_positions = options && options->source_positions;
        input->parse_embedded_math = options && options->embedded_math;
        allocation_context.pool = input->pool;
        allocation_context.arena = input->arena;
        allocation_context.ui_mode = input->ui_mode;
        allocation_context.input = input;
        input_allocation_context = &allocation_context;

        if (strcmp(effective_type, "markup") == 0) {
            // Generic markup type - use flavor to select format
            const char* markup_flavor = (flavor) ? flavor->chars : "markdown";
            log_debug("input_from_source markup: flavor='%s'", markup_flavor);
            MarkupFormat markup_format;
            if (markup_flavor_to_format(markup_flavor, &markup_format)) {
                input->root = input_markup_with_format(input, source, markup_format);
            } else if (strcmp(markup_flavor, "commonmark") == 0) {
                // Strict CommonMark mode - no GFM extensions
                log_debug("input_from_source: using commonmark mode");
                input->root = input_markup_commonmark(input, source);
            } else {
                // Default to markdown with GFM extensions
                log_debug("input_from_source: using default markdown mode");
                input->root = input_markup_modular(input, source);
            }
        }
        else if (dispatch_exact_input_parser(effective_type, input, source)) {}
#ifndef LAMBDA_NO_PDF
        else if (strcmp(effective_type, "pdf") == 0) {
            // PDF is binary; use the explicit length we received instead of strlen,
            // which would truncate at the first null byte inside the binary stream.
            parse_pdf(input, source, source_len);
        }

#endif
#if !defined(LAMBDA_NO_LATEX) && !defined(LAMBDA_NO_MATH_INPUT)
        else if (strcmp(effective_type, "math") == 0) {
            const char* math_flavor = (flavor) ? flavor->chars : "latex";
            // Both ASCII and LaTeX math use the direct cursor parser.
            parse_math(input, source, math_flavor);
        }
        else if (strncmp(effective_type, "math-", 5) == 0) {
            // Handle compound math formats like "math-ascii", "math-latex", etc.
            const char* math_flavor = effective_type + 5; // Skip "math-" prefix
            parse_math(input, source, math_flavor);
        }

#endif
#ifndef LAMBDA_NO_GRAPH_INPUT
        else if (strcmp(effective_type, "graph") == 0) {
            const char* graph_flavor = flavor ? flavor->chars
                : (detected_graph_flavor ? detected_graph_flavor : "dot");
            parse_graph(input, source, graph_flavor);
        }
#endif
        else {
            input->parse_failed = true;
            log_error("input_from_source: unsupported input type '%s'", effective_type);
        }
        if (get_type_id(input->root) == LMD_TYPE_ERROR) input->parse_failed = true;
        input_allocation_context = saved_allocation_context;
    }
    // Note: don't mem_free(source) here - it's the caller's responsibility
    return input;
}

extern "C" Input* input_from_source_n(const char* source, size_t source_len,
        Url* abs_url, String* type, String* flavor) {
    return input_from_source_n_with_name_parent(source, source_len, abs_url,
        type, flavor, NULL);
}

extern "C" Input* input_from_source(const char* source, Url* abs_url, String* type, String* flavor) {
    // Back-compat wrapper: assumes source is a null-terminated string (text only).
    // Binary inputs (e.g. PDF) must call input_from_source_n with the actual byte count.
    return input_from_source_n(source, source ? strlen(source) : 0, abs_url, type, flavor);
}

extern "C" Input* input_from_source_with_name_parent(const char* source,
        Url* abs_url, String* type, String* flavor, NamePool* name_parent) {
    return input_from_source_n_with_name_parent(source,
        source ? strlen(source) : 0, abs_url, type, flavor, name_parent);
}

extern "C" Input* input_from_source_with_positions(const char* source,
        Url* abs_url, String* type, String* flavor) {
    InputParseOptions options = {};
    options.source_positions = true;
    return input_from_source_with_options(source, abs_url, type, flavor, &options);
}

extern "C" Input* input_from_source_with_options(const char* source,
        Url* abs_url, String* type, String* flavor, const InputParseOptions* options) {
    return input_from_source_n_with_name_parent(source,
        source ? strlen(source) : 0, abs_url, type, flavor, NULL, options);
}

// Read a local file and parse it via input_from_source_n. Detects binary
// formats (currently PDF) and reads them with read_binary_file so that null
// bytes in the payload are preserved and the parser receives an accurate
// byte length instead of strlen() which would truncate at the first null.
static Input* input_from_local_path(const char* pathname, Url* abs_url,
        String* type, String* flavor, NamePool* name_parent = NULL) {
    bool is_binary_pdf = false;
    if (type && strcmp(type->chars, "pdf") == 0) {
        is_binary_pdf = true;
    } else if (pathname) {
        size_t plen = strlen(pathname);
        if (plen >= 4 && str_icmp_cstr(pathname + plen - 4, ".pdf") == 0) {
            is_binary_pdf = true;
        }
    }

    size_t src_len = 0;
    char* source = is_binary_pdf ? read_binary_file(pathname, &src_len)
                                 : read_text_file(pathname);
    if (!source) {
        log_debug("input_from_local_path: failed to read file at path: %s", pathname ? pathname : "null");
        return NULL;
    }
    if (!is_binary_pdf) src_len = strlen(source);

    Input* input = input_from_source_n_with_name_parent(source, src_len,
        abs_url, type, flavor, name_parent);
#ifndef LAMBDA_NO_GRAPH_INPUT
    const bool explicit_structurizr = flavor &&
        (strcmp(flavor->chars, "structurizr") == 0 || strcmp(flavor->chars, "c4") == 0);
    const bool detected_structurizr = !flavor &&
        input_detect_structurizr_flavor(pathname, source, src_len) != NULL;
    if (input && !is_binary_pdf && (explicit_structurizr || detected_structurizr)) {
        resolve_graph_structurizr_local_includes(input, pathname);
    }
#endif
    mem_free(source);
    return input;
}

Input* input_from_url(String* url, String* type, String* flavor, Url* cwd) {
    log_debug("input_from_url: ENTRY url='%s', type='%s'", url ? url->chars : "null", type ? type->chars : "null");
    log_debug("input_data at: %s, type: %s, cwd: %p", url ? url->chars : "null", type ? type->chars : "null", cwd);

    Url* abs_url;
    if (cwd) {
        abs_url = url_parse_with_base(url->chars, cwd);
    } else {
        abs_url = url_parse(url->chars);
    }
    if (!abs_url) {
        log_error("Failed to parse URL\n");
        return NULL;
    }
    log_debug("Parsed URL: scheme=%d, host=%s, pathname=%s", abs_url->scheme, abs_url->host ? abs_url->host->chars : "null",
        abs_url->pathname ? abs_url->pathname->chars : "null");

    // Handle different URL schemes
    if (abs_url->scheme == URL_SCHEME_FILE) {
        // Decode the URL pathname before any filesystem operation.
        char* pathname = url_to_local_path(abs_url);
        if (!pathname) {
            url_destroy(abs_url);
            return NULL;
        }
        if (file_is_dir(pathname)) {
            // URL points to a directory - use directory listing
            log_debug("URL points to directory, using input_from_directory\n");
            // Pass original URL string to preserve relative path info
            Input* input = input_from_directory(pathname, url ? url->chars : NULL, false, 1); // non-recursive, single level only
            mem_free(pathname);
            url_destroy(abs_url);
            return input;
        }

        // check if file should be handled as a relational database
        const char* type_str = type ? type->chars : NULL;
        const char* rdb_driver = rdb_detect_format(pathname, type_str);
        if (rdb_driver) {
            log_debug("rdb: detected driver '%s' for path '%s'", rdb_driver, pathname);
            Input* input = input_rdb_from_path(pathname, rdb_driver);
            mem_free(pathname);
            url_destroy(abs_url);
            return input;
        }

        // URL points to a file - read as normal
        log_debug("reading file from path: %s", pathname);

        Input* input = input_from_local_path(pathname, abs_url, type, flavor);
        mem_free(pathname);
        // on success the Input owns abs_url (stored as input->url, freed by
        // ~InputManager); only free it here if no input was created.
        if (!input) url_destroy(abs_url);
        return input;
    }
    else if (abs_url->scheme == URL_SCHEME_HTTP || abs_url->scheme == URL_SCHEME_HTTPS) {
        // Handle HTTP/HTTPS URLs
        log_debug("HTTP/HTTPS URL detected, using HTTP client\n");

        const char* type_str = type ? type->chars : NULL;
        const char* flavor_str = flavor ? flavor->chars : NULL;

        Input* input = input_from_http(url->chars, type_str, flavor_str, "./temp/cache");
        url_destroy(abs_url);
        return input;
    }
    else if (abs_url->scheme == URL_SCHEME_SYS) {
        // Handle sys:// URLs for system information
        log_debug("sys:// URL detected, using system information provider\n");

        // Create a variable pool for the input
        Pool* pool = mem_pool_create(NULL, MEM_ROLE_INPUT, "input.sysinfo");
        if (pool == NULL) {
            log_debug("Failed to create variable pool for sys:// URL\n");
            url_destroy(abs_url);
            return NULL;
        }

        Input* input = input_from_sysinfo(abs_url, pool);
        // on success the Input owns abs_url (create_input stores it, freed by
        // ~InputManager); only tear down the pool and url if creation failed.
        if (!input) {
            pool_destroy(pool);
            url_destroy(abs_url);
        }
        return input;
    }
    else {
        log_debug("Unsupported URL scheme for: %s\n", url ? url->chars : "null");
        url_destroy(abs_url);
        return NULL;
    }
}

/**
 * Load input from a Target (unified I/O target).
 * This function dispatches to appropriate handlers based on target scheme.
 *
 * @param target - The Target to load from (URL or Path)
 * @param type - Optional type hint (json, xml, etc.)
 * @param flavor - Optional flavor hint (for markup variants)
 * @return Input* on success, NULL on failure
 */
static Input* input_from_target_impl(Target* target, String* type,
        String* flavor, NamePool* name_parent) {
    if (!target) {
        log_error("input_from_target: target is NULL");
        return NULL;
    }

    log_debug("input_from_target: scheme=%d, type=%d", target->scheme, target->type);

    // network database targets go to the RDB layer before generic URL/path
    // handling, which neither knows their schemes nor may log their credentials:
    // URIs (postgresql://, mysql://, ...) and, with an explicit driver type,
    // libpq key/value strings ("host=... password=...")
    if (target->type == TARGET_TYPE_URL && target->original &&
            (strstr(target->original, "://") || type)) {
        const char* rdb_driver = rdb_detect_format(target->original, type ? type->chars : NULL);
        if (rdb_driver && strcmp(rdb_driver, "sqlite") != 0) {
            return input_rdb_from_path_with_name_parent(target->original, rdb_driver, name_parent);
        }
    }

    // Check if target is a directory first (for local targets)
    if (target_is_dir(target)) {
        log_debug("input_from_target: directory detected, using directory listing");
        StrBuf* path_buf = (StrBuf*)target_to_local_path(target, NULL);
        if (path_buf) {
            Input* input = input_from_directory_with_name_parent(path_buf->str,
                target->original, false, 1, name_parent);
            strbuf_free(path_buf);
            return input;
        }
        return NULL;
    }

    // For URL targets, use the existing URL-based dispatch
    if (target->type == TARGET_TYPE_URL && target->url) {
        Url* url = target->url;
        char redacted_href[1024];
        rdb_redact_uri(url->href ? url->href->chars : "null", redacted_href, sizeof(redacted_href));
        log_debug("input_from_target: URL target, href=%s", redacted_href);

        // Handle different URL schemes
        if (target->scheme == TARGET_SCHEME_FILE) {
            char* pathname = url_to_local_path(url);
            if (!pathname) return NULL;

            // check if file should be handled as a relational database
            const char* type_str = type ? type->chars : NULL;
            const char* rdb_driver = rdb_detect_format(pathname, type_str);
            if (rdb_driver) {
                log_debug("input_from_target: rdb detected driver '%s' for '%s'", rdb_driver, pathname);
                Input* input = input_rdb_from_path_with_name_parent(pathname, rdb_driver,
                    name_parent);
                mem_free(pathname);
                return input;
            }

            log_debug("input_from_target: reading file from path: %s", pathname);
            // Create a copy of the URL for the input (input owns lifecycle of url_copy via Input)
            Url* url_copy = url_parse(url->href->chars);
            Input* input = input_from_local_path(pathname, url_copy, type, flavor,
                name_parent);
            mem_free(pathname);
            if (!input && url_copy) url_destroy(url_copy);
            return input;
        }
        else if (target->scheme == TARGET_SCHEME_HTTP || target->scheme == TARGET_SCHEME_HTTPS) {
            log_debug("input_from_target: HTTP/HTTPS URL detected");
            const char* type_str = type ? type->chars : NULL;
            const char* flavor_str = flavor ? flavor->chars : NULL;
            return input_from_http_with_name_parent(url->href->chars, type_str,
                flavor_str, "./temp/cache", name_parent);
        }
        else if (target->scheme == TARGET_SCHEME_SYS) {
            log_debug("input_from_target: sys:// URL detected");
            Pool* pool = mem_pool_create(NULL, MEM_ROLE_INPUT, "input.sysinfo");
            if (!pool) {
                log_error("input_from_target: failed to create pool for sys:// URL");
                return NULL;
            }
            Input* input = input_from_sysinfo_with_name_parent(url, pool, name_parent);
            if (!input) pool_destroy(pool);
            return input;
        }
        else {
            log_error("input_from_target: unsupported URL scheme %d", target->scheme);
            return NULL;
        }
    }
    // For Path targets, convert to OS path and read
    else if (target->type == TARGET_TYPE_PATH && target->path) {
        Path* path = target->path;
        log_debug("input_from_target: Path target");

        // Check scheme of the path
        PathScheme path_scheme = path_get_scheme(path);
        if (path_scheme == PATH_SCHEME_HTTP || path_scheme == PATH_SCHEME_HTTPS) {
            // Convert path to URL string and handle as HTTP
            StrBuf* url_buf = strbuf_new();
            path_to_string(path, url_buf);
            const char* type_str = type ? type->chars : NULL;
            const char* flavor_str = flavor ? flavor->chars : NULL;
            Input* input = input_from_http_with_name_parent(url_buf->str, type_str,
                flavor_str, "./temp/cache", name_parent);
            strbuf_free(url_buf);
            return input;
        }

        // Local file path - convert to OS path
        StrBuf* path_buf = strbuf_new();
        path_to_os_path(path_qualify_default(path_get_pool(), path), path_buf);
        const char* pathname = path_buf->str;

        // Directory case already handled by target_is_dir check above

        log_debug("input_from_target: reading file from path: %s", pathname);
        // Create file:// URL from file path (percent-encoded, cross-platform)
        char* file_url_str = url_from_local_path(pathname);
        Url* file_url = file_url_str ? url_parse(file_url_str) : NULL;
        if (file_url_str) mem_free(file_url_str);

        Input* input = input_from_local_path(pathname, file_url, type, flavor,
            name_parent);
        strbuf_free(path_buf);
        if (!input && file_url) url_destroy(file_url);
        return input;
    }

    log_error("input_from_target: invalid target (type=%d)", target->type);
    return NULL;
}

Input* input_from_target(Target* target, String* type, String* flavor) {
    return input_from_target_impl(target, type, flavor, NULL);
}

Input* input_from_target_with_name_parent(Target* target, String* type,
        String* flavor, NamePool* name_parent) {
    // Explicitly carry the retained schema parent down to Input construction;
    // parser work may yield or run nested loaders, so thread-local ownership
    // state would bind a document to the wrong runtime context.
    return input_from_target_impl(target, type, flavor, name_parent);
}

static void input_pool_cleanup(void* arg);

Input* Input::create(Pool* pool, Url* abs_url, Input* parent) {
    return Input::create_with_name_parent(pool, abs_url, parent, NULL);
}

Input* Input::create_with_name_parent(Pool* pool, Url* abs_url, Input* parent,
        NamePool* name_parent) {
    if (!pool) {
        log_error("Input::create: pool is NULL");
        return NULL;
    }
    Input* input = (Input*)pool_alloc(pool, sizeof(Input));
    if (!input) {
        log_error("Failed to allocate Input structure (pool=%p, size=%zu)", (void*)pool, sizeof(Input));
        return NULL;
    }
    input->pool = pool;
    // D4.2.3: every Input owns a context for its arena and name pool, with or
    // without a URL; a URL also registers the document so snapshots attribute
    // the memory to it. The context sits under the pool's own context, so a
    // cascade that destroys the pool destroys this context first (D4.2.6).
    bool has_url = abs_url && abs_url->href && abs_url->href->len > 0;
    MemContext* dctx = mem_context_create(
        mem_node_owner((MemNode*)pool_get_mem_node(pool)), MEM_ROLE_INPUT,
        has_url ? "input.doc" : "input.local");
    if (has_url) {
        uint32_t parent_doc = (parent && parent->mem_ctx)
            ? mem_context_doc_id((MemContext*)parent->mem_ctx) : 0;
        uint32_t doc_id = mem_doc_register(abs_url->href->chars, parent_doc);
        mem_context_set_doc_id(dctx, doc_id);
    }
    input->mem_ctx = dctx;
    input->arena = mem_arena_create(dctx, MEM_ROLE_INPUT, "input.arena");
    // A schema-backed child reuses only the explicit retained NamePool parent;
    // document-tree parentage is intentionally unrelated to name identity.
    input->name_pool = mem_name_pool_create(dctx, pool, name_parent,
        MEM_ROLE_INPUT, "input.name_pool");
    input->type_list = arraylist_new(16);
    // Input is pool_alloc'd, not pool_calloc'd: every field must be set here.
    // Leaving this one uninitialized made map_put dereference pool garbage.
    input->shape_transition_root = nullptr;
    input->shape_transition_shapes = 0;
    input->shape_graph_budget = 0;
    input->element_roots = nullptr;
    input->element_root_cap = 0;
    input->element_root_count = 0;
    input->element_transition_shapes = 0;
    input->keeps_external_edges = false;
    input->external_edges = nullptr;
    input->url = abs_url;
    input->path = nullptr;
    input->parent = parent;     // Set parent Input for hierarchical ownership
    input->root = (Item){.item = ITEM_NULL};
    input->doc_count = 0;
    input->ui_mode = false;
    input->source_positions = false;
    input->parse_failed = false;
    input->parse_error_message = nullptr;
    input->parse_embedded_math = false;
    input->embedded_math = nullptr;
    input->xml_stylesheet_href = nullptr;
    // D4.2.6: the Input lives in `pool`, so the pool releases it at the latest.
    // Without this, a URL-less Input's arena outlived every owner.
    if (!pool_add_cleanup(pool, input_pool_cleanup, input)) {
        log_error("Input::create: failed to register the Input with its pool");
        input_release_document_resources(input);
        return NULL;
    }
    return input;
}

void input_release_auxiliary_resources(Input* input) {
    if (!input) return;
    if (input->name_pool) {
        name_pool_release(input->name_pool);
        input->name_pool = nullptr;
    }
    if (input->type_list) {
        arraylist_free(input->type_list);
        input->type_list = nullptr;
    }
}

void input_release_document_resources(Input* input) {
    if (!input) return;
    input_release_auxiliary_resources(input);
    if (input->mem_ctx) {
        // The document context owns the Input arena and its remaining tracked
        // allocators; destroy it only after its DOM has detached from them.
        mem_context_destroy((MemContext*)input->mem_ctx);
        input->mem_ctx = nullptr;
        input->arena = nullptr;
    }
    // The transition tree lived in that arena (D3.4.3v3); drop its entry points.
    // The external-parent table (D3.4.3v5) holds only arena pointers.
    if (input->external_edges) {
        hashmap_free(input->external_edges);
        input->external_edges = nullptr;
    }
    if (input->element_roots) {
        pool_free(input->pool, input->element_roots);
        input->element_roots = nullptr;
        input->element_root_cap = 0;
        input->element_root_count = 0;
    }
    input->shape_transition_root = nullptr;
}

// D4.2.6: runs when the Input's pool releases its blocks. Releasing is
// idempotent, so an Input already released explicitly is left as is. Inside a
// registry cascade the lock is held and the Input's context — created under
// the pool's context — is already gone, so only the registry-free parts remain.
static void input_pool_cleanup(void* arg) {
    Input* input = (Input*)arg;
    if (mem_context_in_teardown()) {
        input->mem_ctx = nullptr;
        input->arena = nullptr;
    }
    input_release_document_resources(input);
}

// Global singleton instance
static InputManager* g_input_manager = nullptr;

InputManager::InputManager() {
    global_pool = mem_pool_create(NULL, MEM_ROLE_INPUT, "input.global_pool");
    if (!global_pool) {
        log_error("InputManager: Failed to create global_pool");
    }
    inputs = arraylist_new(16);
    thread_pools = arraylist_new(4);
#ifndef LAMBDA_NO_RESOURCE_CACHE
    script_cache = input_script_cache_create();
#else
    script_cache = nullptr;
#endif
    // Use shared global decimal context
    decimal_ctx = decimal_fixed_context();
}

InputManager::~InputManager() {
#ifndef LAMBDA_NO_RESOURCE_CACHE
    if (script_cache) {
        input_script_cache_log_summary(script_cache);
        input_script_cache_destroy(script_cache);
        script_cache = nullptr;
    }

#endif
    // clean up all tracked inputs
    reset_inputs();
    if (inputs) {
        arraylist_free(inputs);
        inputs = nullptr;
    }
    if (thread_pools) {
        arraylist_free(thread_pools);
        thread_pools = nullptr;
    }

    // destroy the global pool (this frees all pool-allocated memory)
    if (global_pool) {
        log_debug("InputManager::~InputManager destroying global_pool=%p", (void*)global_pool);
        mem_pool_destroy(global_pool);
        global_pool = nullptr;
    }

    // decimal_ctx is now shared global - don't free
    decimal_ctx = nullptr;
}

void InputManager::reset_inputs() {
    if (inputs) {
        for (int i = 0; i < inputs->length; i++) {
            Input* input = (Input*)inputs->data[i];
            input_release_document_resources(input);
            if (input && input->url) {
                url_destroy((Url*)input->url);
                input->url = nullptr;
            }
        }
        arraylist_clear(inputs);
    }

    if (thread_pools) {
        for (int i = 0; i < thread_pools->length; i++) {
            Pool* pool = (Pool*)thread_pools->data[i];
            if (pool) mem_pool_destroy(pool);
        }
        arraylist_clear(thread_pools);
    }
    if (g_input_thread_manager == this) {
        g_input_thread_manager = nullptr;
        g_input_thread_pool = nullptr;
    }
}

//------------------------------------------------------------------------------
// Heap factory (audited boundary for `new InputManager` / `delete mgr`)
//------------------------------------------------------------------------------

InputManager* input_manager_create() {
    InputManager* mgr = (InputManager*)mem_alloc(sizeof(InputManager), MEM_CAT_INPUT_OTHER);
    if (!mgr) return nullptr;
    new (mgr) InputManager(); // NEW_DELETE_OK: single audited construction boundary for InputManager singleton.
    return mgr;
}

void input_manager_destroy(InputManager* mgr) {
    if (!mgr) return;
    mgr->~InputManager(); // NEW_DELETE_OK: paired with input_manager_create.
    mem_free(mgr);
}

mpd_context_t* InputManager::decimal_context() {
    // Lazy initialization of singleton
    pthread_mutex_lock(&g_input_manager_mutex);
    if (!g_input_manager) {
        g_input_manager = input_manager_create();
    }
    mpd_context_t* context = g_input_manager ? g_input_manager->decimal_ctx : nullptr;
    pthread_mutex_unlock(&g_input_manager_mutex);
    return context;
}

InputScriptCache* InputManager::global_script_cache() {
    pthread_mutex_lock(&g_input_manager_mutex);
    if (!g_input_manager) g_input_manager = input_manager_create();
    InputScriptCache* cache = g_input_manager
        ? g_input_manager->get_script_cache() : nullptr;
    pthread_mutex_unlock(&g_input_manager_mutex);
    return cache;
}

// Static method to create input using global manager
Input* InputManager::create_input(Url* abs_url) {
    return create_input_with_name_parent(abs_url, NULL);
}

Input* InputManager::create_input_with_name_parent(Url* abs_url,
        NamePool* name_parent) {
    // Lazy initialization of singleton
    pthread_mutex_lock(&g_input_manager_mutex);
    if (!g_input_manager) {
        g_input_manager = input_manager_create();
    }
    InputManager* manager = g_input_manager;
    pthread_mutex_unlock(&g_input_manager_mutex);
    if (!manager) return nullptr;
    return manager->create_input_instance_with_name_parent(abs_url, name_parent);
}

// Instance method to create input
Input* InputManager::create_input_instance(Url* abs_url) {
    return create_input_instance_with_name_parent(abs_url, NULL);
}

Input* InputManager::create_input_instance_with_name_parent(Url* abs_url,
        NamePool* name_parent) {
    pthread_mutex_lock(&g_input_manager_mutex);

    Pool* pool = nullptr;
    if (g_input_thread_manager == this) {
        pool = g_input_thread_pool;
    }
    if (!pool) {
        pool = mem_pool_create(NULL, MEM_ROLE_INPUT, "input.thread_pool");
        if (!pool || !thread_pools || !arraylist_append(thread_pools, pool)) {
            if (pool) mem_pool_destroy(pool);
            pthread_mutex_unlock(&g_input_manager_mutex);
            log_error("create_input_instance: failed to create thread input pool");
            return nullptr;
        }
        g_input_thread_manager = this;
        g_input_thread_pool = pool;
    }

    Input* input = Input::create_with_name_parent(pool, abs_url, NULL,
        name_parent);
    if (!input) {
        pthread_mutex_unlock(&g_input_manager_mutex);
        log_error("create_input_instance: Input::create returned NULL");
        return nullptr;
    }

    // Track this input for cleanup
    if (!inputs || !arraylist_append(inputs, input)) {
        pthread_mutex_unlock(&g_input_manager_mutex);
        log_error("create_input_instance: failed to track input");
        return nullptr;
    }

    pthread_mutex_unlock(&g_input_manager_mutex);
    return input;
}

// Destroy the global instance
void InputManager::destroy_global() {
    pthread_mutex_lock(&g_input_manager_mutex);
    InputManager* manager = g_input_manager;
    if (manager) {
        input_manager_destroy(manager);
        g_input_manager = nullptr;
    }
    if (g_input_thread_manager == manager) {
        g_input_thread_manager = nullptr;
        g_input_thread_pool = nullptr;
    }
    pthread_mutex_unlock(&g_input_manager_mutex);
}

void InputManager::reset_global_inputs() {
    pthread_mutex_lock(&g_input_manager_mutex);
    if (g_input_manager) g_input_manager->reset_inputs();
    pthread_mutex_unlock(&g_input_manager_mutex);
}

// Detach a URL pointer from any tracked Input that owns it, so the caller can
// free the URL without ~InputManager double-freeing the same pointer.
void InputManager::detach_url(Url* url) {
    pthread_mutex_lock(&g_input_manager_mutex);
    if (!g_input_manager || !url || !g_input_manager->inputs) {
        pthread_mutex_unlock(&g_input_manager_mutex);
        return;
    }
    ArrayList* inputs = g_input_manager->inputs;
    for (int i = 0; i < inputs->length; i++) {
        Input* input = (Input*)inputs->data[i];
        if (input && input->url == url) {
            input->url = nullptr;
        }
    }
    pthread_mutex_unlock(&g_input_manager_mutex);
}

// Get the <html> element from the #document tree
extern "C" Element* input_get_html_element(Input* input) {
    if (!input) return nullptr;

    TypeId root_type = get_type_id(input->root);

    if (root_type == LMD_TYPE_ELEMENT) {
        Element* elem = input->root.element;
        TypeElmt* type = (TypeElmt*)elem->type;

        // If it's a #document node, get the html child
        if (strcmp(type->name.str, "#document") == 0) {
            List* doc_children = elem;
            if (doc_children->length > 0) {
                Item html_item = doc_children->items[0];
                if (html_item.type_id() == LMD_TYPE_ELEMENT) {
                    return html_item.element;
                }
            }
        }
    }

    return nullptr;
}

// Get fragment element (extracts from body for fragments, returns html for full docs)
extern "C" Element* input_get_html_fragment_element(Input* input, const char* original_html) {
    if (!input) return nullptr;

    TypeId root_type = get_type_id(input->root);

    if (root_type == LMD_TYPE_ELEMENT) {
        Element* elem = input->root.element;
        TypeElmt* type = (TypeElmt*)elem->type;

        // If it's a #document node from HTML5 parser
        if (strcmp(type->name.str, "#document") == 0) {
            List* doc_children = elem;
            if (doc_children->length > 0) {
                Item html_item = doc_children->items[0];
                if (html_item.type_id() == LMD_TYPE_ELEMENT) {
                    Element* html_elem = html_item.element;
                    TypeElmt* html_type = (TypeElmt*)html_elem->type;

                    if (strcmp(html_type->name.str, "html") == 0) {
                        // Check if original HTML explicitly starts with <html>
                        if (original_html) {
                            const char* p = original_html;
                            while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;

                            bool has_explicit_html = (strncmp(p, "<html", 5) == 0);

                            // If <html> was explicit in source, return it
                            if (has_explicit_html) {
                                return html_elem;
                            }
                        }

                        // Otherwise, extract the actual fragment from body
                        List* html_children = html_elem;

                        for (int64_t i = 0; i < html_children->length; i++) {
                            Item child = html_children->items[i];
                            if (child.type_id() == LMD_TYPE_ELEMENT) {
                                Element* child_elem = child.element;
                                TypeElmt* child_type = (TypeElmt*)child_elem->type;

                                if (strcmp(child_type->name.str, "body") == 0) {
                                    List* body_children = child_elem;

                                    // Count element children (skip text nodes)
                                    int element_count = 0;
                                    Element* first_element = nullptr;
                                    for (int64_t j = 0; j < body_children->length; j++) {
                                        Item body_child = body_children->items[j];
                                        if (body_child.type_id() == LMD_TYPE_ELEMENT) {
                                            if (!first_element) {
                                                first_element = body_child.element;
                                            }
                                            element_count++;
                                        }
                                    }

                                    // If body has exactly one element child, return it
                                    if (element_count == 1 && first_element) {
                                        return first_element;
                                    }

                                    // Multiple element children -> not a simple fragment, return html
                                    return html_elem;
                                }
                            }
                        }

                        return html_elem;
                    }
                }
            }
        }

        // Fallback
        return elem;
    }
    else if (root_type == LMD_TYPE_ARRAY) {
        List* root_list = input->root.array;
        for (int64_t i = 0; i < root_list->length; i++) {
            Item item = root_list->items[i];
            if (item.type_id() == LMD_TYPE_ELEMENT) {
                Element* elem = item.element;
                TypeElmt* type = (TypeElmt*)elem->type;

                // Skip DOCTYPE and comments
                if (strcmp(type->name.str, "!DOCTYPE") != 0 &&
                    strcmp(type->name.str, "!--") != 0) {
                    return elem;
                }
            }
        }
    }

    return nullptr;
}
