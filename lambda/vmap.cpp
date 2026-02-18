// vmap.cpp — VMap (Virtual Map) implementation with HashMap backend
// Provides a dynamic hash-map type for Lambda with arbitrary key types.
// Uses lib/hashmap.h (Robin Hood open-addressed hash table) as the backing store.

#include "lambda.hpp"
#include "lambda-data.hpp"
#include "../lib/hashmap.h"
#include "../lib/arraylist.h"
#include "../lib/log.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

extern Context* context;

// ============================================================================
// HashMap Entry and Data Structures
// ============================================================================

struct HashMapEntry {
    Item key;
    Item value;
};

// backing data for the HashMap-backed VMap
struct HashMapData {
    HashMap* table;              // lib/hashmap.h instance
    ArrayList* key_order;        // insertion-order list of Item keys
    int64_t count;
};

// ============================================================================
// Hash and Compare Functions
// ============================================================================

// hash an Item key for use in the hash table
static uint64_t vmap_hash_item(const void* entry, uint64_t seed0, uint64_t seed1) {
    const HashMapEntry* e = (const HashMapEntry*)entry;
    Item key = e->key;
    TypeId type_id = get_type_id(key);

    switch (type_id) {
    case LMD_TYPE_STRING: {
        String* s = key.get_string();
        if (s) return hashmap_sip(s->chars, s->len, seed0, seed1);
        return hashmap_sip(&key.item, sizeof(uint64_t), seed0, seed1);
    }
    case LMD_TYPE_SYMBOL: {
        Symbol* s = key.get_symbol();
        if (s) return hashmap_sip(s->chars, s->len, seed0, seed1);
        return hashmap_sip(&key.item, sizeof(uint64_t), seed0, seed1);
    }
    default:
        // pointer-identity or packed-value: hash the raw 64-bit Item
        return hashmap_sip(&key.item, sizeof(uint64_t), seed0, seed1);
    }
}

// compare two Item keys for equality
static int vmap_compare_item(const void* a, const void* b, void* udata) {
    const HashMapEntry* ea = (const HashMapEntry*)a;
    const HashMapEntry* eb = (const HashMapEntry*)b;
    Item ka = ea->key, kb = eb->key;
    TypeId ta = get_type_id(ka), tb = get_type_id(kb);
    if (ta != tb) return 1;  // different types → not equal

    switch (ta) {
    case LMD_TYPE_STRING: {
        String* sa = ka.get_string();
        String* sb = kb.get_string();
        if (sa == sb) return 0;
        if (!sa || !sb) return 1;
        if (sa->len != sb->len) return 1;
        return memcmp(sa->chars, sb->chars, sa->len);
    }
    case LMD_TYPE_SYMBOL: {
        Symbol* sa = ka.get_symbol();
        Symbol* sb = kb.get_symbol();
        if (sa == sb) return 0;
        if (!sa || !sb) return 1;
        if (sa->len != sb->len) return 1;
        return memcmp(sa->chars, sb->chars, sa->len);
    }
    default:
        return (ka.item == kb.item) ? 0 : 1;
    }
}

// ============================================================================
// HashMapData Lifecycle
// ============================================================================

static HashMapData* hashmap_data_new() {
    HashMapData* hd = (HashMapData*)calloc(1, sizeof(HashMapData));
    hd->table = hashmap_new(sizeof(HashMapEntry), 8, 0, 0,
                            vmap_hash_item, vmap_compare_item, NULL, NULL);
    hd->key_order = arraylist_new(8);
    hd->count = 0;
    return hd;
}

static void hashmap_data_free(HashMapData* hd) {
    if (!hd) return;
    if (hd->table) hashmap_free(hd->table);
    if (hd->key_order) arraylist_free(hd->key_order);
    free(hd);
}

// deep copy: creates a new HashMapData with the same entries
static HashMapData* hashmap_data_copy(HashMapData* src) {
    HashMapData* dst = hashmap_data_new();
    // iterate through entries in insertion order
    for (int i = 0; i < src->key_order->length; i++) {
        Item key = *(Item*)&src->key_order->data[i];
        HashMapEntry probe = { .key = key };
        const HashMapEntry* found = (const HashMapEntry*)hashmap_get(src->table, &probe);
        if (found) {
            HashMapEntry entry = { .key = found->key, .value = found->value };
            hashmap_set(dst->table, &entry);
            arraylist_append(dst->key_order, (void*)key.item);
            dst->count++;
        }
    }
    return dst;
}

// insert or update an entry (in-place mutation)
static void hashmap_data_set(HashMapData* hd, Item key, Item value) {
    HashMapEntry probe = { .key = key };
    const HashMapEntry* existing = (const HashMapEntry*)hashmap_get(hd->table, &probe);
    HashMapEntry entry = { .key = key, .value = value };
    hashmap_set(hd->table, &entry);
    if (!existing) {
        // new key — add to insertion order
        arraylist_append(hd->key_order, (void*)key.item);
        hd->count++;
    }
}

// get value by key (returns ItemNull if not found)
static Item hashmap_data_get(HashMapData* hd, Item key) {
    HashMapEntry probe = { .key = key };
    const HashMapEntry* found = (const HashMapEntry*)hashmap_get(hd->table, &probe);
    if (found) return found->value;
    return ItemNull;
}

// ============================================================================
// VMap Vtable Implementation: HashMap Backend
// ============================================================================

static Item hashmap_vmap_get(void* data, Item key) {
    HashMapData* hd = (HashMapData*)data;
    return hashmap_data_get(hd, key);
}

static void hashmap_vmap_set(void* data, Item key, Item value) {
    HashMapData* hd = (HashMapData*)data;
    hashmap_data_set(hd, key, value);
}

static int64_t hashmap_vmap_count(void* data) {
    HashMapData* hd = (HashMapData*)data;
    return hd->count;
}

// return keys as ArrayList<String*> for compatibility with item_keys() / for-loop
// string/symbol keys → use the key string directly
// other keys → synthetic string "__v<index>"
static ArrayList* hashmap_vmap_keys(void* data) {
    HashMapData* hd = (HashMapData*)data;
    ArrayList* keys = arraylist_new(hd->count > 0 ? (int)hd->count : 4);
    for (int i = 0; i < hd->key_order->length; i++) {
        Item key = *(Item*)&hd->key_order->data[i];
        TypeId kt = get_type_id(key);
        if (kt == LMD_TYPE_STRING) {
            String* s = key.get_string();
            if (s) {
                String* copy = heap_strcpy(s->chars, s->len);
                arraylist_append(keys, (void*)copy);
            }
        } else if (kt == LMD_TYPE_SYMBOL) {
            Symbol* s = key.get_symbol();
            if (s) {
                String* copy = heap_strcpy(s->chars, s->len);
                arraylist_append(keys, (void*)copy);
            }
        } else {
            // synthetic key: "__v<index>"
            char buf[32];
            snprintf(buf, sizeof(buf), "__v%d", i);
            String* str = heap_strcpy(buf, (int)strlen(buf));
            arraylist_append(keys, (void*)str);
        }
    }
    return keys;
}

static Item hashmap_vmap_key_at(void* data, int64_t index) {
    HashMapData* hd = (HashMapData*)data;
    if (index < 0 || index >= hd->key_order->length) return ItemNull;
    return *(Item*)&hd->key_order->data[index];
}

static Item hashmap_vmap_value_at(void* data, int64_t index) {
    HashMapData* hd = (HashMapData*)data;
    if (index < 0 || index >= hd->key_order->length) return ItemNull;
    Item key = *(Item*)&hd->key_order->data[index];
    return hashmap_data_get(hd, key);
}

static void hashmap_vmap_destroy(void* data) {
    HashMapData* hd = (HashMapData*)data;
    hashmap_data_free(hd);
}

// singleton vtable for HashMap-backed VMaps
static VMapVtable hashmap_vtable = {
    hashmap_vmap_get,
    hashmap_vmap_set,
    hashmap_vmap_count,
    hashmap_vmap_keys,
    hashmap_vmap_key_at,
    hashmap_vmap_value_at,
    hashmap_vmap_destroy,
};

// ============================================================================
// VMap Construction: Public C API
// ============================================================================

static VMap* vmap_alloc() {
    VMap* vm = (VMap*)heap_calloc(sizeof(VMap), LMD_TYPE_VMAP);
    vm->type_id = LMD_TYPE_VMAP;
    vm->data = hashmap_data_new();
    vm->vtable = &hashmap_vtable;
    return vm;
}

// create an empty VMap
extern "C" Item vmap_new() {
    log_debug("vmap_new: creating empty VMap");
    VMap* vm = vmap_alloc();
    return {.vmap = vm};
}

// create a VMap from an array/list of alternating [k1, v1, k2, v2, ...]
extern "C" Item vmap_from_array(Item array_item) {
    log_debug("vmap_from_array: creating VMap from array");
    TypeId type_id = get_type_id(array_item);
    if (type_id != LMD_TYPE_ARRAY && type_id != LMD_TYPE_LIST) {
        log_error("vmap_from_array: expected array/list, got type %s", get_type_name(type_id));
        return ItemNull;
    }
    // Array is typedef for List — both have items[] and length
    List* list = array_item.list;
    if (!list) return ItemNull;
    int64_t len = list->length;
    if (len % 2 != 0) {
        log_error("vmap_from_array: odd number of elements (%lld), expected key-value pairs", len);
        return ItemNull;
    }
    VMap* vm = vmap_alloc();
    HashMapData* hd = (HashMapData*)vm->data;

    for (int64_t i = 0; i < len; i += 2) {
        Item key = list->items[i];
        Item value = list->items[i + 1];
        hashmap_data_set(hd, key, value);
    }

    log_debug("vmap_from_array: created VMap with %lld entries", hd->count);
    return {.vmap = vm};
}

// in-place mutation: insert or update an entry in the VMap (for procedural m.set(k, v))
extern "C" void vmap_set(Item vmap_item, Item key, Item value) {
    log_debug("vmap_set: in-place insert on VMap");
    TypeId type_id = get_type_id(vmap_item);

    if (type_id != LMD_TYPE_VMAP) {
        log_error("vmap_set: expected vmap, got type %s", get_type_name(type_id));
        return;
    }
    VMap* vm = vmap_item.vmap;
    if (!vm || !vm->vtable) {
        log_error("vmap_set: null vmap or vtable");
        return;
    }
    vm->vtable->set(vm->data, key, value);
}

// ============================================================================
// VMap Access Helpers (for runtime dispatch)
// ============================================================================

// get value from VMap by string key (used by item_attr dispatch)
// handles both regular string keys and synthetic "__v<N>" keys
Item vmap_get_by_str(VMap* vm, const char* key) {
    if (!vm || !vm->data || !key) return ItemNull;
    HashMapData* hd = (HashMapData*)vm->data;

    // check for synthetic key format "__v<N>"
    if (key[0] == '_' && key[1] == '_' && key[2] == 'v' && key[3] >= '0' && key[3] <= '9') {
        int index = atoi(key + 3);
        if (index >= 0 && index < hd->key_order->length) {
            Item orig_key = *(Item*)&hd->key_order->data[index];
            return hashmap_data_get(hd, orig_key);
        }
        // fall through to string key lookup
    }

    // look up as string key
    String* str = heap_create_name(key);
    Item key_item = {.item = s2it(str)};
    return hashmap_data_get(hd, key_item);
}

// get value from VMap by Item key (used by map_get / fn_member dispatch)
Item vmap_get_by_item(VMap* vm, Item key) {
    if (!vm || !vm->data) return ItemNull;
    return vm->vtable->get(vm->data, key);
}
