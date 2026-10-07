#include "mvp_lmd_runtime.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/heap_api.h"
#include "../../runtime/runtime-state.h"
#include "../../../lib/hashmap.h"
#include "../../../lib/utf.h"
#include "../../../lib/mem.h"
#include <math.h>

struct MapBucket { uint64_t hash; int64_t slot; };
static uint64_t bucket_hash(const void* item, uint64_t, uint64_t) {
    return ((const MapBucket*)item)->hash;
}
static int bucket_compare(const void* a, const void* b, void*) {
    uint64_t x = ((const MapBucket*)a)->hash, y = ((const MapBucket*)b)->hash;
    return x < y ? -1 : x != y;
}
static bool text_is(String* key, const char* text) {
    return key->len == strlen(text) && !memcmp(key->chars, text, key->len);
}
static bool is_number(Item key) {
    TypeId tid = get_type_id(key);
    return tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT;
}
static uint64_t key_hash(Item key) {
    if (is_number(key)) {
        double number = it2d(key);
        uint64_t bits = 0;
        if (isnan(number)) bits = UINT64_C(0x7ff8000000000000);
        else if (number != 0) memcpy(&bits, &number, sizeof(bits));
        return hashmap_xxhash3(&bits, sizeof(bits), 0, 0);
    }
    if (get_type_id(key) == LMD_TYPE_STRING) {
        String* s = key.get_string();
        return hashmap_sip(s->chars, s->len, 1, 0);
    }
    return hashmap_sip(&key.item, sizeof(key.item), 2, 0);
}
static bool key_equal(Item a, Item b) {
    if (is_number(a) && is_number(b)) {
        double x = it2d(a), y = it2d(b);
        return x == y || (isnan(x) && isnan(y));
    }
    if (get_type_id(a) == LMD_TYPE_STRING && get_type_id(b) == LMD_TYPE_STRING) {
        String* x = a.get_string(); String* y = b.get_string();
        return x->len == y->len && !memcmp(x->chars, y->chars, x->len);
    }
    return a.item == b.item;
}
static Item canonical_string(Item value) {
    if (get_type_id(value) != LMD_TYPE_STRING) return value;
    String* s = value.get_string();
    if (utf8_key_is_canonical(s->chars, s->len)) return value;
    if (!context->name_pool) context->name_pool = name_pool_create_runtime(context->pool);
    String* name = heap_create_name(s->chars, s->len);
    return name ? Item{.item = s2it(name)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}
extern "C" Item mvp_lmd_property_key(Item string) {
    String* s = string.get_string();
    if (text_is(s, "__proto__")) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    // most keys are already canonical; keep array/string indexing allocation-free.
    if (!utf8_key_is_canonical(s->chars, s->len)) return canonical_string(string);
    return string;
}
extern "C" Item mvp_lmd_object_new(TypeMap* shape, int64_t collection) {
    if (!collection) {
        Map* object = map_alloc_for_type(shape, NULL, 0);
        return object ? Item{.map = object} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    }
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    OrderedMap* map = (OrderedMap*)heap_calloc(sizeof(OrderedMap), LMD_TYPE_MAP);
    if (!map) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    map->type_id = LMD_TYPE_MAP; map->type = shape; map->map_kind = MAP_KIND_ORDERED;
    Rooted<OrderedMap*> owner(roots, map);
    map->entries = array();
    if (!map->entries) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    map->index = hashmap_new(sizeof(MapBucket), 0, 0, 0, bucket_hash, bucket_compare, NULL, NULL);
    return map->index ? Item{.map = map} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}
static Map* object_face(Item owner) {
    if (get_type_id(owner) != LMD_TYPE_MAP) return NULL;
    Map* map = owner.map;
    TypeMap* type = (TypeMap*)map->type;
    return type && type->nominal && !type->js_meta &&
        (map->map_kind == MAP_KIND_PLAIN || map->map_kind == MAP_KIND_ORDERED) ? map : NULL;
}
static ShapeEntry* own_field(Map* map, String* key) {
    return typemap_hash_lookup((TypeMap*)map->type, key->chars, (int)key->len);
}
int mvp_lmd_map_method(String* key) {
    static const char* names[] = {"get", "set", "has", "delete", "clear", "entries", "keys", "values"};
    for (int i = 0; i < 8; i++) if (text_is(key, names[i])) return i + 1;
    return 0;
}
extern "C" Item mvp_lmd_property_get(Item owner, Item name, int64_t callee) {
    Map* map = object_face(owner);
    if (!map) return mvp_lmd_fail(get_type_id(owner) == LMD_TYPE_NULL ||
        get_type_id(owner) == LMD_TYPE_UNDEFINED ? LMD_MVP_TYPE : LMD_MVP_CAPABILITY, 0);
    String* key = name.get_string();
    ShapeEntry* field = own_field(map, key);
    if (field) return map_shape_field_to_item(map->data, field);
    if (map->map_kind == MAP_KIND_ORDERED) {
        if (text_is(key, "size")) return {.item = i2it(((OrderedMap*)map)->size)};
        int method = mvp_lmd_map_method(key);
        if (method) return callee ? Item{.item = mvp_lmd_method_token(method)} : mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    }
    // fixed intrinsic prototype properties are not modeled as absent own fields.
    static const char* inherited[] = {"constructor", "toString", "toLocaleString", "valueOf",
        "hasOwnProperty", "isPrototypeOf", "propertyIsEnumerable", "__defineGetter__",
        "__defineSetter__", "__lookupGetter__", "__lookupSetter__"};
    for (const char* text : inherited) if (text_is(key, text)) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    return Item{.item = ITEM_JS_UNDEFINED};
}
extern "C" Item mvp_lmd_property_set(Item owner, Item name, Item value) {
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    uint64_t home = 0;
    Rooted<Item> object(roots, owner), key(roots, name), held(roots, lambda_item_adopt_scalar_home(value, &home));
    Map* map = object_face(object.get());
    if (!map) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    if (map->map_kind == MAP_KIND_ORDERED && !own_field(map, key.get().get_string()) &&
            (text_is(key.get().get_string(), "size") || mvp_lmd_map_method(key.get().get_string())))
        return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    return map_shape_set(map, key.get().get_string(), held.get()) ? held.get() : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}
extern "C" Item mvp_lmd_property_delete(Item owner, Item name) {
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner), key(roots, name);
    Map* map = object_face(object.get());
    if (!map) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    return map_shape_delete(map, key.get().get_string()) ? Item{.item = ITEM_TRUE} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}
extern "C" Item mvp_lmd_property_has(Item owner, Item name, int64_t inherited) {
    Map* map = object_face(owner);
    if (!map) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    String* key = name.get_string();
    if (own_field(map, key)) return Item{.item = ITEM_TRUE};
    if (inherited) {
        if (map->map_kind == MAP_KIND_ORDERED && (text_is(key, "size") || mvp_lmd_map_method(key))) return Item{.item = ITEM_TRUE};
        // diagnose excluded prototype queries through the same fixed-property policy.
        Item result = mvp_lmd_property_get(owner, name, 0);
        if (get_type_id(result) == LMD_TYPE_ERROR) return result;
    }
    return Item{.item = ITEM_FALSE};
}
static Item store(Array* array, int64_t index, Item value) {
    uint64_t home = 0;
    return mvp_lmd_array_store(Item{.array = array}, (uint32_t)index, lambda_item_adopt_scalar_home(value, &home));
}
static int64_t entry_find(OrderedMap* map, Item key, uint64_t hash, int64_t* previous) {
    MapBucket query = {hash, -1};
    const MapBucket* bucket = (const MapBucket*)hashmap_get(map->index, &query);
    *previous = -1;
    for (int64_t slot = bucket ? bucket->slot : -1; slot >= 0; ) {
        if (key_equal(map->entries->items[slot], key)) return slot;
        *previous = slot;
        slot = it2i(map->entries->items[slot + 2]);
    }
    return -1;
}
static bool compact_entries(OrderedMap* map) {
    if (map->cursors || map->entries->length <= 128 || map->entries->length <= map->size * 8) return true;
    RootFrame roots(2);
    if (!roots.valid()) return false;
    Rooted<OrderedMap*> owner(roots, map);
    Rooted<Array*> entries(roots, array());
    if (!entries.get()) return false;
    struct hashmap* index = hashmap_new(sizeof(MapBucket), 0, 0, 0, bucket_hash, bucket_compare, NULL, NULL);
    if (!index) return false;
    for (int64_t i = 0; i < map->entries->length; i += 4) {
        if (map->entries->items[i + 3].item != ITEM_TRUE) continue;
        uint64_t hash = key_hash(map->entries->items[i]);
        MapBucket query = {hash, -1};
        const MapBucket* old = (const MapBucket*)hashmap_get(index, &query);
        int64_t next = old ? old->slot : -1;
        MapBucket bucket = {hash, entries.get()->length};
        hashmap_set(index, &bucket);
        if (hashmap_oom(index)) { hashmap_free(index); return false; }
        Item values[4] = {map->entries->items[i], map->entries->items[i + 1], Item{.item = i2it(next)}, Item{.item = ITEM_TRUE}};
        // old keys/values remain rooted by the owner, but their scalar homes can move.
        uint64_t homes[2] = {};
        values[0] = lambda_item_adopt_scalar_home(values[0], &homes[0]);
        values[1] = lambda_item_adopt_scalar_home(values[1], &homes[1]);
        RootFrame row_roots(2);
        if (!row_roots.valid()) { hashmap_free(index); return false; }
        Rooted<Item> key(row_roots, values[0]), value(row_roots, values[1]);
        for (int j = 0; j < 4; j++) {
            Item item = j == 0 ? key.get() : j == 1 ? value.get() : values[j];
            if (get_type_id(store(entries.get(), bucket.slot + j, item)) == LMD_TYPE_ERROR) {
                hashmap_free(index); return false;
            }
        }
    }
    hashmap_free(map->index); map->index = index; map->entries = entries.get();
    return true;
}
extern "C" Item mvp_lmd_map_call(Item owner, Item method_item, Item key, Item value) {
    int64_t method = (method_item.item & 0xffff) >> 8;
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    uint64_t key_home = 0, value_home = 0;
    Rooted<Item> object(roots, owner), k(roots, lambda_item_adopt_scalar_home(key, &key_home)),
        v(roots, lambda_item_adopt_scalar_home(value, &value_home));
    Map* face = object_face(object.get());
    if (!face || face->map_kind != MAP_KIND_ORDERED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    OrderedMap* map = (OrderedMap*)face;
    if (method == 5) {
        // keep stable positions for active cursors, while releasing every strong entry edge.
        for (int64_t i = 0; i < map->entries->length; i++) map->entries->items[i] = ItemNull;
        if (!map->cursors) { map->entries->length = 0; map->entries->extra = 0; }
        hashmap_clear(map->index, false); map->size = 0;
        return Item{.item = ITEM_JS_UNDEFINED};
    }
    if (method < 1 || method > 4) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    k.set(canonical_string(k.get()));
    if (get_type_id(k.get()) == LMD_TYPE_ERROR) return k.get();
    if (method == 2 && !compact_entries(map)) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    uint64_t hash = key_hash(k.get());
    int64_t previous, slot = entry_find(map, k.get(), hash, &previous);
    if (method == 1) return slot < 0 ? Item{.item = ITEM_JS_UNDEFINED} : map->entries->items[slot + 1];
    if (method == 3) return slot < 0 ? Item{.item = ITEM_FALSE} : Item{.item = ITEM_TRUE};
    if (method == 4) {
        if (slot < 0) return Item{.item = ITEM_FALSE};
        Item next = map->entries->items[slot + 2];
        if (previous >= 0) map->entries->items[previous + 2] = next;
        else {
            MapBucket bucket = {hash, it2i(next)};
            if (bucket.slot < 0) hashmap_delete(map->index, &bucket);
            else {
                hashmap_set(map->index, &bucket);
                if (hashmap_oom(map->index)) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            }
        }
        for (int i = 0; i < 4; i++) map->entries->items[slot + i] = ItemNull;
        map->size--; return Item{.item = ITEM_TRUE};
    }
    if (slot >= 0) {
        Item stored = store(map->entries, slot + 1, v.get());
        return get_type_id(stored) == LMD_TYPE_ERROR ? stored : object.get();
    }
    slot = map->entries->length;
    if (slot > UINT32_MAX - 4 || !array_reserve_append_slots(map->entries, 8))
        return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    MapBucket query = {hash, -1};
    const MapBucket* old = (const MapBucket*)hashmap_get(map->index, &query);
    int64_t next = old ? old->slot : -1;
    // publish the index only after every owned slot is installed successfully.
    Item stored_key = is_number(k.get()) && it2d(k.get()) == 0 ? Item{.item = i2it(0)} : k.get();
    Item row[4] = {stored_key, v.get(), Item{.item = i2it(next)}, Item{.item = ITEM_TRUE}};
    for (int i = 0; i < 4; i++) {
        Item written = store(map->entries, slot + i, i == 0 ? stored_key : i == 1 ? v.get() : row[i]);
        if (get_type_id(written) == LMD_TYPE_ERROR) {
            for (int64_t j = slot; j < map->entries->length; j++) map->entries->items[j] = ItemNull;
            map->entries->length = slot; return written;
        }
    }
    MapBucket added = {hash, slot};
    hashmap_set(map->index, &added);
    if (hashmap_oom(map->index)) {
        for (int i = 0; i < 4; i++) map->entries->items[slot + i] = ItemNull;
        map->entries->length = slot; return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    }
    map->size++; return object.get();
}
extern "C" int64_t mvp_lmd_map_next(Item owner, int64_t cursor) {
    OrderedMap* map = (OrderedMap*)owner.map;
    for (int64_t i = cursor; i < map->entries->length; i += 4)
        if (map->entries->items[i + 3].item == ITEM_TRUE) return i;
    return -1;
}
extern "C" Item mvp_lmd_map_entry(Item owner, int64_t cursor, int64_t projection) {
    OrderedMap* map = (OrderedMap*)owner.map;
    if (projection == 7) return map->entries->items[cursor];
    if (projection == 8) return map->entries->items[cursor + 1];
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner);
    Rooted<Array*> pair(roots, array());
    if (!pair.get()) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    map = (OrderedMap*)object.get().map;
    Item first = store(pair.get(), 0, map->entries->items[cursor]);
    if (get_type_id(first) == LMD_TYPE_ERROR) return first;
    return get_type_id(store(pair.get(), 1, map->entries->items[cursor + 1])) == LMD_TYPE_ERROR
        ? ItemError : Item{.array = pair.get()};
}
struct ProjectionField { ShapeEntry* field; uint32_t index; int64_t order; };
static int projection_order(const void* a, const void* b) {
    const ProjectionField* x = (const ProjectionField*)a; const ProjectionField* y = (const ProjectionField*)b;
    if (x->index == y->index) return x->order < y->order ? -1 : x->order != y->order;
    return x->index < y->index ? -1 : 1;
}
extern "C" Item mvp_lmd_object_project(Item owner, int64_t projection) {
    Map* map = object_face(owner);
    if (!map) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    if (!context->name_pool) context->name_pool = name_pool_create_runtime(context->pool);
    TypeMap* type = (TypeMap*)map->type;
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner);
    Rooted<Array*> result(roots, array()), pair(roots, NULL);
    if (!result.get()) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    ProjectionField* fields = (ProjectionField*)pool_calloc(context->pool, type->length * sizeof(ProjectionField));
    if (!fields && type->length) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    int64_t count = 0;
    FOR_EACH_MAP_FIELD(type, field) {
        String* key = heap_create_name(field->name->str, field->name->length);
        if (!key) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        int64_t index = mvp_lmd_string_key(key);
        fields[count] = {field, index >= 0 ? (uint32_t)index : UINT32_MAX, count}; count++;
    }
    // creation ordinals break ties so ordinary string keys retain source order.
    qsort(fields, (size_t)count, sizeof(ProjectionField), projection_order);
    for (int64_t i = 0; i < count; i++) {
        ShapeEntry* field = fields[i].field;
        String* key = heap_create_name(field->name->str, field->name->length);
        Item value = projection == 0 ? Item{.item = s2it(key)} : map_shape_field_to_item(object.get().map->data, field);
        if (projection == 2) {
            pair.set(array());
            if (!pair.get()) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            Item written = store(pair.get(), 0, Item{.item = s2it(key)});
            if (get_type_id(written) == LMD_TYPE_ERROR) return written;
            written = store(pair.get(), 1, map_shape_field_to_item(object.get().map->data, field));
            if (get_type_id(written) == LMD_TYPE_ERROR) return written;
            value = Item{.array = pair.get()};
        }
        Item written = store(result.get(), i, value);
        if (get_type_id(written) == LMD_TYPE_ERROR) return written;
    }
    pool_free(context->pool, fields);
    return Item{.array = result.get()};
}
