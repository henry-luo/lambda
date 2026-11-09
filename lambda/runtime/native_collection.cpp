#include "native_collection.hpp"
#include "lambda/lambda.h"
#include "lib/hashmap_helpers.h"
#include <string.h>

struct NativeCollectionEntry {
    // The order node is the sole owner of a collection entry. Hash buckets
    // merely index that stable node, which avoids a second mutable key/value
    // copy and an insertion-order scan after every hash operation.
    NativeCollectionNode* node;
};

Item native_collection_node_raw_item(const NativeCollectionNode* node,
        NativeCollectionSlot slot) {
    return node ? node->slots[slot] : ItemNull;
}

void native_collection_node_store_item(NativeCollectionNode* node,
        NativeCollectionSlot slot, Item value) {
    if (!node) return;
    owned_item_slot_store(node->slots, NATIVE_COLLECTION_NODE_ITEM_COUNT, slot,
        value);
}

static void native_collection_node_clear_items(NativeCollectionNode* node) {
    native_collection_node_store_item(node, NATIVE_COLLECTION_NODE_KEY, ItemNull);
    native_collection_node_store_item(node, NATIVE_COLLECTION_NODE_VALUE, ItemNull);
}

static uint64_t native_collection_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const NativeCollectionEntry* e = (const NativeCollectionEntry*)item;
    Item k = native_collection_node_raw_item(e ? e->node : NULL,
        NATIVE_COLLECTION_NODE_KEY);
    TypeId tid = get_type_id(k);
    if (tid == LMD_TYPE_STRING) {
        String* s = it2s(k);
        return hashmap_hash_bytes(s->chars, s->len, seed0, seed1);
    }
    // Numeric types: hash as double for SameValueZero consistency
    if (tid == LMD_TYPE_INT || tid == LMD_TYPE_INT64 || tid == LMD_TYPE_FLOAT) {
        double d;
        if (tid == LMD_TYPE_FLOAT) d = k.get_double();
        else if (tid == LMD_TYPE_INT64) d = (double)it2l(k);
        else d = (double)it2i(k);
        // SameValueZero: -0 and +0 are the same → normalize to +0
        if (d == 0.0) d = 0.0;
        // SameValueZero: all NaN values are equal → use canonical NaN bits
        if (d != d) { uint64_t nan_marker = 0x7FF8000000000001ULL; return hashmap_hash_bytes(&nan_marker, sizeof(nan_marker), seed0, seed1); }
        return hashmap_hash_bytes(&d, sizeof(d), seed0, seed1);
    }
    // fallback: hash the raw item bits
    uint64_t bits = k.item;
    return hashmap_hash_bytes(&bits, sizeof(bits), seed0, seed1);
}

static int native_collection_compare(const void *a, const void *b, void *udata) {
    const NativeCollectionEntry* ea = (const NativeCollectionEntry*)a;
    const NativeCollectionEntry* eb = (const NativeCollectionEntry*)b;
    Item ka = native_collection_node_raw_item(ea ? ea->node : NULL,
        NATIVE_COLLECTION_NODE_KEY);
    Item kb = native_collection_node_raw_item(eb ? eb->node : NULL,
        NATIVE_COLLECTION_NODE_KEY);
    TypeId ta = get_type_id(ka), tb = get_type_id(kb);
    // SameValueZero: handle numeric type coercion (-0 == +0, NaN == NaN)
    // Normalize numeric types for comparison
    bool a_num = (ta == LMD_TYPE_INT || ta == LMD_TYPE_INT64 || ta == LMD_TYPE_FLOAT);
    bool b_num = (tb == LMD_TYPE_INT || tb == LMD_TYPE_INT64 || tb == LMD_TYPE_FLOAT);
    if (a_num && b_num) {
        double da, db;
        if (ta == LMD_TYPE_FLOAT) da = ka.get_double();
        else if (ta == LMD_TYPE_INT64) da = (double)it2l(ka);
        else da = (double)it2i(ka);
        if (tb == LMD_TYPE_FLOAT) db = kb.get_double();
        else if (tb == LMD_TYPE_INT64) db = (double)it2l(kb);
        else db = (double)it2i(kb);
        // SameValueZero: NaN == NaN
        if (da != da && db != db) return 0;
        // SameValueZero: -0 == +0
        return (da == db) ? 0 : 1;
    }
    if (ta != tb) return 1;
    if (ta == LMD_TYPE_STRING) {
        String* sa = it2s(ka);
        String* sb = it2s(kb);
        if (sa->len != sb->len) return 1;
        return memcmp(sa->chars, sb->chars, sa->len);
    }
    // reference equality for objects
    return (ka.item == kb.item) ? 0 : 1;
}

NativeCollectionData* native_collection_data(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP || !obj.map ||
            obj.map->map_kind != MAP_KIND_COLLECTION) return NULL;
    return ((NativeCollectionMap*)obj.map)->collection_data;
}

Item native_collection_create(TypeMap* shape, Pool* pool, int kind, bool weak) {
    if (!shape || !pool) return ItemError;
    NativeCollectionData* data = (NativeCollectionData*)pool_calloc(pool, sizeof(NativeCollectionData));
    if (!data) return ItemError;
    data->hmap = hashmap_new(sizeof(NativeCollectionEntry), 16, 0, 0,
        native_collection_hash, native_collection_compare, NULL, NULL);
    if (!data->hmap) return ItemError;
    data->pool = pool;
    data->type = kind;
    data->is_weak = weak;
    NativeCollectionMap* collection = (NativeCollectionMap*)heap_calloc(sizeof(NativeCollectionMap), LMD_TYPE_MAP);
    if (!collection) { hashmap_free(data->hmap); data->hmap = NULL; return ItemError; }
    collection->base.type_id = LMD_TYPE_MAP;
    collection->base.map_kind = MAP_KIND_COLLECTION;
    collection->base.type = shape;
    // D7.2.2: capability storage is not an observable property or cloneable field.
    collection->collection_data = data;
    Map* initialized = map_fill_reserve_data(&collection->base);
    return initialized ? Item{.map = initialized} : ItemError;
}

NativeCollectionNode* native_collection_find_node(NativeCollectionData* cd,
        Item key) {
    if (!cd || !cd->hmap) return NULL;
    NativeCollectionNode probe_node = {};
    probe_node.slots[NATIVE_COLLECTION_NODE_KEY] = key;
    NativeCollectionEntry probe = {.node = &probe_node};
    const NativeCollectionEntry* found = (const NativeCollectionEntry*)hashmap_get(
        cd->hmap, &probe);
    return found ? found->node : NULL;
}

bool native_collection_insert_node(NativeCollectionData* cd, Item key,
        Item value) {
    if (!cd || !cd->hmap || !cd->pool) return false;
    NativeCollectionNode* node = (NativeCollectionNode*)pool_calloc(
        cd->pool, sizeof(NativeCollectionNode));
    if (!node) return false;
    native_collection_node_store_item(node, NATIVE_COLLECTION_NODE_KEY, key);
    native_collection_node_store_item(node, NATIVE_COLLECTION_NODE_VALUE, value);
    NativeCollectionEntry entry = {.node = node};
    hashmap_set(cd->hmap, &entry);
    if (hashmap_oom(cd->hmap)) {
        // An unpublished node is not traced; do not link a ghost entry when
        // native hash allocation fails.
        native_collection_node_clear_items(node);
        return false;
    }
    if (cd->order_tail) cd->order_tail->next = node;
    else cd->order_head = node;
    cd->order_tail = node;
    return true;
}

void native_collection_remove_node(NativeCollectionNode* node) {
    if (!node || node->deleted) return;
    // Keep next links for live iterators and forEach. Deleted nodes are
    // tombstones until collection teardown, but release their payload now.
    node->deleted = true;
    native_collection_node_clear_items(node);
}

bool native_collection_set(NativeCollectionData* data, Item key, Item value) {
    NativeCollectionNode* node = native_collection_find_node(data, key);
    if (!node) return native_collection_insert_node(data, key, value);
    // updates preserve insertion order and every outstanding iterator route.
    native_collection_node_store_item(node, NATIVE_COLLECTION_NODE_VALUE, value);
    return true;
}

bool native_collection_delete(NativeCollectionData* data, Item key) {
    NativeCollectionNode* node = native_collection_find_node(data, key);
    if (!node) return false;
    NativeCollectionEntry entry = {.node = node};
    hashmap_delete(data->hmap, &entry);
    native_collection_remove_node(node);
    return true;
}

void native_collection_heap_destroy(Map* map, gc_native_seen_t* seen_native) {
    if (!map || map->map_kind != MAP_KIND_COLLECTION) return;
    NativeCollectionData* cd = ((NativeCollectionMap*)map)->collection_data;
    if (!cd) return;
    if (cd->hmap && !gc_native_seen_seen_or_add(seen_native, cd->hmap)) {
        hashmap_free(cd->hmap);
    }
    cd->hmap = NULL;
    ((NativeCollectionMap*)map)->collection_data = NULL;
}

static void native_collection_weak_entry_clear(uint64_t key_item, void* context) {
    NativeCollectionData* cd = (NativeCollectionData*)context;
    if (!cd || !cd->hmap || key_item == 0) return;
    Item key = (Item){.item = key_item};
    native_collection_delete(cd, key);
}

void native_collection_gc_trace(Map* map, gc_heap_t* gc) {
    if (!map || !gc || map->map_kind != MAP_KIND_COLLECTION) return;
    NativeCollectionData* cd = ((NativeCollectionMap*)map)->collection_data;
    if (!cd) return;
    if (cd->is_weak) {
        for (NativeCollectionNode* node = cd->order_head; node; node = node->next) {
            if (node->deleted) continue;
            // A weak value is retained only after its key is independently
            // reachable; dead keys are removed before sweep can reuse identity.
            if (!gc_register_ephemeron(gc,
                                       &node->slots[NATIVE_COLLECTION_NODE_KEY].item,
                                       &node->slots[NATIVE_COLLECTION_NODE_VALUE].item,
                                       native_collection_weak_entry_clear, cd)) {
                gc_mark_item(gc, node->slots[NATIVE_COLLECTION_NODE_KEY].item);
                gc_mark_item(gc, node->slots[NATIVE_COLLECTION_NODE_VALUE].item);
            }
        }
        return;
    }

    // Strong collection entries live in native insertion-order storage, which
    // the managed Map payload scan cannot see.
    for (NativeCollectionNode* node = cd->order_head; node; node = node->next) {
        if (node->deleted) continue;
        gc_mark_item(gc, node->slots[NATIVE_COLLECTION_NODE_KEY].item);
        gc_mark_item(gc, node->slots[NATIVE_COLLECTION_NODE_VALUE].item);
    }
}

