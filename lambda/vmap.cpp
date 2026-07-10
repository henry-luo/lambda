// vmap.cpp — VMap (Virtual Map) implementation with HashMap backend
// Provides a dynamic hash-map type for Lambda with arbitrary key types.
// Uses lib/hashmap.h (Robin Hood open-addressed hash table) as the backing store.

#include "lambda.hpp"
#include "lambda-decimal.hpp"
#include "../lib/memtrack.h"
#include "lambda-data.hpp"
#include "../lib/hashmap.h"
#include "../lib/arraylist.h"
#include "../lib/gc/gc_heap.h"
#include "../lib/log.h"
#include "jube/jube_registry.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

extern Context* context;

static bool ascii_is_lower(char c) {
    return c >= 'a' && c <= 'z';
}

static bool ascii_is_upper(char c) {
    return c >= 'A' && c <= 'Z';
}

static char ascii_to_upper(char c) {
    return ascii_is_lower(c) ? (char)(c - 'a' + 'A') : c;
}

static char ascii_to_lower(char c) {
    return ascii_is_upper(c) ? (char)(c - 'A' + 'a') : c;
}

static const JubeHostObjectOps* vmap_host_ops(VMap* vm) {
    if (!vm || !vm->host_type) return nullptr;
    const JubeTypeDef* type = jube_find_type_by_host_type(vm->host_type);
    return type ? type->host_ops : nullptr;
}

static bool item_key_chars(Item key, const char** chars, uint32_t* len) {
    TypeId type_id = get_type_id(key);
    if (type_id != LMD_TYPE_STRING && type_id != LMD_TYPE_SYMBOL) return false;
    const char* key_chars = key.get_chars();
    if (!key_chars) return false;
    *chars = key_chars;
    *len = key.get_len();
    return true;
}

static Item string_key_item(const char* chars, uint32_t len) {
    return (Item){.item = s2it(heap_strcpy((char*)chars, len))};
}

static bool snake_to_camel_key(const char* in, uint32_t len, char* out, size_t cap) {
    if (!in || !out || cap == 0) return false;
    bool saw_underscore = false;
    size_t oi = 0;
    for (uint32_t i = 0; i < len; i++) {
        char c = in[i];
        if (c == '_' && i + 1 < len && ascii_is_lower(in[i + 1])) {
            saw_underscore = true;
            i++;
            c = ascii_to_upper(in[i]);
        }
        if (oi + 1 >= cap) return false;
        out[oi++] = c;
    }
    out[oi] = '\0';
    return saw_underscore;
}

static bool camel_to_snake_key(const char* in, uint32_t len, char* out, size_t cap) {
    if (!in || !out || cap == 0) return false;
    size_t oi = 0;
    bool changed = false;
    for (uint32_t i = 0; i < len; i++) {
        char c = in[i];
        if (ascii_is_upper(c)) {
            if (oi > 0) {
                if (oi + 1 >= cap) return false;
                out[oi++] = '_';
            }
            c = ascii_to_lower(c);
            changed = true;
        }
        if (oi + 1 >= cap) return false;
        out[oi++] = c;
    }
    out[oi] = '\0';
    return changed;
}

static bool key_has_attribute_syntax(Item key) {
    const char* chars = nullptr;
    uint32_t len = 0;
    if (!item_key_chars(key, &chars, &len)) return false;
    for (uint32_t i = 0; i < len; i++) {
        if (chars[i] == '-') return true;
    }
    return false;
}

static bool host_item_is_absent(Item item) {
    return item.item == ITEM_JS_UNDEFINED;
}

static bool vmap_host_get_by_item(VMap* vm, Item key, Item* out) {
    const JubeHostObjectOps* ops = vmap_host_ops(vm);
    if (!ops || !ops->get_property || !out) return false;

    Item receiver = (Item){.vmap = vm};
    Item result = ItemNull;
    const char* chars = nullptr;
    uint32_t len = 0;
    bool has_chars = item_key_chars(key, &chars, &len);
    if (has_chars) {
        char camel[256];
        if (snake_to_camel_key(chars, len, camel, sizeof(camel))) {
            Item camel_key = string_key_item(camel, (uint32_t)strlen(camel));
            if (ops->get_property(receiver, camel_key, &result) && !host_item_is_absent(result)) {
                *out = result;
                return true;
            }
            *out = ItemNull;
            return true;
        }
    }

    Item lookup_key = has_chars ? string_key_item(chars, len) : key;
    if (ops->get_property(receiver, lookup_key, &result) && !host_item_is_absent(result)) {
        *out = result;
        return true;
    }

    *out = ItemNull;
    return true;
}

static bool vmap_host_set_attribute(VMap* vm, Item key, Item value, Item* out) {
    const JubeHostObjectOps* ops = vmap_host_ops(vm);
    if (!ops || !ops->call_method || !key_has_attribute_syntax(key)) return false;
    Item receiver = (Item){.vmap = vm};
    Item args[2] = {key, value};
    Item method = string_key_item("setAttribute", 12);
    return ops->call_method(receiver, method, args, 2, out) != 0;
}

static bool vmap_host_set_by_item(VMap* vm, Item key, Item value, Item* out) {
    const JubeHostObjectOps* ops = vmap_host_ops(vm);
    if (!ops || !ops->set_property || !out) return false;

    if (vmap_host_set_attribute(vm, key, value, out)) return true;

    Item receiver = (Item){.vmap = vm};
    const char* chars = nullptr;
    uint32_t len = 0;
    if (item_key_chars(key, &chars, &len)) {
        char camel[256];
        if (snake_to_camel_key(chars, len, camel, sizeof(camel))) {
            // Lambda projection keys are snake_case; DOM host setters are camelCase.
            Item camel_key = string_key_item(camel, (uint32_t)strlen(camel));
            return ops->set_property(receiver, camel_key, value, out) != 0;
        }
        Item lookup_key = string_key_item(chars, len);
        return ops->set_property(receiver, lookup_key, value, out) != 0;
    }
    return ops->set_property(receiver, key, value, out) != 0;
}

static void append_host_key(SymbolKeyList* keys, Item key_item) {
    const char* chars = nullptr;
    uint32_t len = 0;
    if (!keys || !item_key_chars(key_item, &chars, &len)) return;
    char snake[256];
    const char* out_chars = chars;
    uint32_t out_len = len;
    if (camel_to_snake_key(chars, len, snake, sizeof(snake))) {
        out_chars = snake;
        out_len = (uint32_t)strlen(snake);
    }
    Symbol* sym = heap_create_symbol(out_chars, out_len);
    if (sym) symbol_key_list_append(keys, sym);
}

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
    ArrayList* num_values;       // heap-allocated numeric value storage (survives frame_end)
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
    case LMD_TYPE_INT:
    case LMD_TYPE_INT64:
    case LMD_TYPE_UINT64:
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_DECIMAL:
    case LMD_TYPE_NUM_SIZED: {
        char num_buf[128];
        if (lambda_numeric_to_canonical_string(key, num_buf, sizeof(num_buf))) {
            return hashmap_sip(num_buf, strlen(num_buf), seed0, seed1);
        }
        return hashmap_sip(&key.item, sizeof(uint64_t), seed0, seed1);
    }
    case LMD_TYPE_STRING: {
        String* s = key.get_safe_string();
        if (s) return hashmap_sip(s->chars, s->len, seed0, seed1);
        return hashmap_sip(&key.item, sizeof(uint64_t), seed0, seed1);
    }
    case LMD_TYPE_SYMBOL: {
        Symbol* s = key.get_safe_symbol();
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
    if (IS_NUMERIC_ID(ta) && IS_NUMERIC_ID(tb)) {
        Bool eq = fn_eq(ka, kb);
        return eq == BOOL_TRUE ? 0 : 1;
    }
    if (ta != tb) return 1;  // different non-numeric types → not equal

    switch (ta) {
    case LMD_TYPE_STRING: {
        String* sa = ka.get_safe_string();
        String* sb = kb.get_safe_string();
        if (sa == sb) return 0;
        if (!sa || !sb) return 1;
        if (sa->len != sb->len) return 1;
        return memcmp(sa->chars, sb->chars, sa->len);
    }
    case LMD_TYPE_SYMBOL: {
        Symbol* sa = ka.get_safe_symbol();
        Symbol* sb = kb.get_safe_symbol();
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
    HashMapData* hd = (HashMapData*)mem_calloc(1, sizeof(HashMapData), MEM_CAT_EVAL);
    hd->table = hashmap_new(sizeof(HashMapEntry), 8, 0, 0,
                            vmap_hash_item, vmap_compare_item, NULL, NULL);
    hd->key_order = arraylist_new(8);
    hd->num_values = NULL;  // lazily allocated when needed
    hd->count = 0;
    return hd;
}

static void hashmap_data_free(HashMapData* hd) {
    if (!hd) return;
    if (hd->table) hashmap_free(hd->table);
    if (hd->key_order) arraylist_free(hd->key_order);
    // free heap-allocated numeric value storage
    if (hd->num_values) {
        for (int i = 0; i < hd->num_values->length; i++) {
            mem_free(hd->num_values->data[i]);
        }
        arraylist_free(hd->num_values);
    }
    mem_free(hd);
}

// Stabilize numeric values: float/int64/datetime use tagged pointers into nursery memory.
// Copy them to heap-owned storage so they survive independent of nursery lifecycle.
static Item stabilize_value(HashMapData* hd, Item value) {
    TypeId vtype = get_type_id(value);
    if (vtype == LMD_TYPE_FLOAT) {
        double* dval = (double*)mem_alloc(sizeof(double), MEM_CAT_EVAL);
        *dval = value.get_double();
        value = {.item = d2it(dval)};
        if (!hd->num_values) hd->num_values = arraylist_new(4);
        arraylist_append(hd->num_values, (void*)dval);
    } else if (vtype == LMD_TYPE_INT64) {
        int64_t* ival = (int64_t*)mem_alloc(sizeof(int64_t), MEM_CAT_EVAL);
        *ival = value.get_int64();
        value = {.item = l2it(ival)};
        if (!hd->num_values) hd->num_values = arraylist_new(4);
        arraylist_append(hd->num_values, (void*)ival);
    } else if (vtype == LMD_TYPE_DTIME) {
        DateTime* dtval = (DateTime*)mem_alloc(sizeof(DateTime), MEM_CAT_EVAL);
        *dtval = value.get_datetime();
        value = {.item = k2it(dtval)};
        if (!hd->num_values) hd->num_values = arraylist_new(4);
        arraylist_append(hd->num_values, (void*)dtval);
    }
    return value;
}

// insert or update an entry (in-place mutation)
static void hashmap_data_set(HashMapData* hd, Item key, Item value) {
    value = stabilize_value(hd, value);
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

// return keys as SymbolKeyList for compatibility with item_keys() / for-loop
// string/symbol keys → create a symbol from the key string
// other keys → synthetic symbol "__v<index>"
static SymbolKeyList* hashmap_vmap_keys(void* data) {
    HashMapData* hd = (HashMapData*)data;
    SymbolKeyList* keys = symbol_key_list_new(hd->count > 0 ? hd->count : 4);
    for (int i = 0; i < hd->key_order->length; i++) {
        Item key = *(Item*)&hd->key_order->data[i];
        TypeId kt = get_type_id(key);
        if (kt == LMD_TYPE_STRING) {
            String* s = key.get_safe_string();
            if (s) {
                Symbol* sym = heap_create_symbol(s->chars, s->len);
                symbol_key_list_append(keys, sym);
            }
        } else if (kt == LMD_TYPE_SYMBOL) {
            Symbol* s = key.get_safe_symbol();
            if (s) {
                Symbol* sym = heap_create_symbol(s->chars, s->len);
                symbol_key_list_append(keys, sym);
            }
        } else {
            // synthetic key: "__v<index>"
            char buf[32];
            snprintf(buf, sizeof(buf), "__v%d", i);
            Symbol* sym = heap_create_symbol(buf, (int)strlen(buf));
            symbol_key_list_append(keys, sym);
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
    // lists are represented as LMD_TYPE_ARRAY at runtime (LMD_TYPE_LIST was removed);
    // LMD_TYPE_ARRAY_NUM is intentionally rejected — its packed int64/double layout is
    // incompatible with the Item* items[] access below.
    if (type_id != LMD_TYPE_ARRAY) {
        log_error("vmap_from_array: expected array, got type %s", get_type_name(type_id));
        return ItemNull;
    }
    // Array is typedef for List — both have items[] and length
    List* list = array_item.array;
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
    Item host_result = ItemNull;
    if (vmap_host_set_by_item(vm, key, value, &host_result)) {
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
    Item host_result = ItemNull;
    Item host_key = {.item = s2it(heap_create_name(key))};
    if (vmap_host_get_by_item(vm, host_key, &host_result)) return host_result;

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
    Item host_result = ItemNull;
    if (vmap_host_get_by_item(vm, key, &host_result)) return host_result;
    return vm->vtable->get(vm->data, key);
}

SymbolKeyList* vmap_keys_for_item(Item vmap_item) {
    if (get_type_id(vmap_item) != LMD_TYPE_VMAP || !vmap_item.vmap) return nullptr;
    VMap* vm = vmap_item.vmap;
    const JubeHostObjectOps* ops = vmap_host_ops(vm);
    if (!ops || !ops->own_property_keys) return nullptr;

    Item result = ItemNull;
    if (!ops->own_property_keys(vmap_item, &result)) return nullptr;
    if (get_type_id(result) != LMD_TYPE_ARRAY || !result.array) return nullptr;

    List* list = result.array;
    SymbolKeyList* keys = symbol_key_list_new(list->length > 0 ? list->length : 4);
    for (int64_t i = 0; i < list->length; i++) {
        append_host_key(keys, list->items[i]);
    }
    return keys;
}

// ============================================================================
// GC Bridge: tracing and finalization for VMap backing data
// ============================================================================

// Trace all Item keys and values stored in a VMap's HashMapData.
// Called by gc_trace_object() during the mark phase.
extern "C" void vmap_gc_trace(void* data, gc_heap_t* gc) {
    HashMapData* hd = (HashMapData*)data;
    if (!hd || !hd->table) return;

    // iterate all entries in the HashMap and mark both keys and values
    size_t iter = 0;
    void* entry;
    while (hashmap_iter(hd->table, &iter, &entry)) {
        HashMapEntry* e = (HashMapEntry*)entry;
        gc_mark_item(gc, e->key.item);
        gc_mark_item(gc, e->value.item);
    }
}

// Destroy VMap's HashMapData backing store.
// Called by gc_finalize_dead_object() during the sweep phase.
extern "C" void vmap_gc_destroy(void* obj, void* data) {
    (void)obj;
    hashmap_data_free((HashMapData*)data);
}
