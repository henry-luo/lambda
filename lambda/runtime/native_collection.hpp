#pragma once

#include "../lambda-data.hpp"
#include "../../lib/hashmap.h"
#include "../../lib/mempool.h"
#include "gc/gc_heap.h"

// shared native collection storage; front ends own key policy and dispatch.
struct NativeCollectionNode {
    // slots[0..1] own the observable key/value. The matching tail slots hold
    // wide scalar payloads so entries never borrow a caller's number frame.
    Item slots[4];
    NativeCollectionNode* next;
    bool deleted;
};

enum NativeCollectionSlot {
    NATIVE_COLLECTION_NODE_KEY = 0,
    NATIVE_COLLECTION_NODE_VALUE = 1,
    NATIVE_COLLECTION_NODE_ITEM_COUNT = 2,
};

struct NativeCollectionData {
    HashMap* hmap;
    Pool* pool; // native nodes share the carrier owner, independent of front-end TLS
    int type; // JS_COLLECTION_MAP or JS_COLLECTION_SET
    bool is_weak; // true for WeakMap/WeakSet
    NativeCollectionNode* order_head;
    NativeCollectionNode* order_tail;
};

// Collection instances retain ordinary Map shape/data storage for expandos,
// while this trailing field implements the unobservable [[MapData]]/[[SetData]]
// internal slot. It must never be represented by a string-keyed shape entry.
struct NativeCollectionMap {
    Map base;
    NativeCollectionData* collection_data;
};

enum { NATIVE_COLLECTION_MAP = 0, NATIVE_COLLECTION_SET = 1 };
NativeCollectionData* native_collection_data(Item object);
Item native_collection_create(TypeMap* shape, Pool* pool, int kind, bool weak);
Item native_collection_node_raw_item(const NativeCollectionNode* node, NativeCollectionSlot slot);
void native_collection_node_store_item(NativeCollectionNode* node, NativeCollectionSlot slot, Item value);
NativeCollectionNode* native_collection_find_node(NativeCollectionData* data, Item key);
bool native_collection_insert_node(NativeCollectionData* data, Item key, Item value);
bool native_collection_set(NativeCollectionData* data, Item key, Item value);
bool native_collection_delete(NativeCollectionData* data, Item key);
void native_collection_remove_node(NativeCollectionNode* node);
void native_collection_gc_trace(Map* map, gc_heap_t* gc);
void native_collection_heap_destroy(Map* map, gc_native_seen_t* seen);
