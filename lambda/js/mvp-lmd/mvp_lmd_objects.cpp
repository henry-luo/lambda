#include "mvp_lmd_runtime.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/heap_api.h"
#include "../../runtime/runtime-state.h"
#include "../../input/input.hpp"
#include "../../core/lambda-decimal.hpp"
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
    return !property_key_requires_identity(key) && key->len == strlen(text) && !memcmp(key->chars, text, key->len);
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
    if (get_type_id(key) == LMD_TYPE_DECIMAL) {
        char* digits = bigint_to_cstring_radix(key, 10);
        uint64_t hash = digits ? hashmap_sip(digits, strlen(digits), 3, 0) : 0;
        mem_free(digits); return hash;
    }
    return hashmap_sip(&key.item, sizeof(key.item), 2, 0);
}
static bool key_equal(Item a, Item b) {
    if (a.item == b.item) return true;
    // distinct packed integers need no double conversion for SameValueZero.
    if (get_type_id(a) == LMD_TYPE_INT && get_type_id(b) == LMD_TYPE_INT) return false;
    if (is_number(a) && is_number(b)) {
        double x = it2d(a), y = it2d(b);
        return x == y || (isnan(x) && isnan(y));
    }
    if (get_type_id(a) == LMD_TYPE_STRING && get_type_id(b) == LMD_TYPE_STRING) {
        String* x = a.get_string(); String* y = b.get_string();
        return x->len == y->len && !memcmp(x->chars, y->chars, x->len);
    }
    if (get_type_id(a) == LMD_TYPE_DECIMAL && get_type_id(b) == LMD_TYPE_DECIMAL)
        return bigint_cmp(a, b) == 0;
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
    if (get_type_id(string) != LMD_TYPE_STRING) {
        string = mvp_lmd_to_primitive(string, 2);
        if (item_is_error(string)) return string;
        if (get_type_id(string) == LMD_TYPE_SYMBOL) {
            Symbol* symbol = string.get_symbol();
            NameRef key = symbol_has_js_identity(symbol) ? name_pool_resolve_id(context->name_pool, symbol->name_id) : NULL;
            return key ? Item{.item = s2it(key)} : mvp_lmd_fail(LMD_MVP_TYPE, 0);
        }
        string = mvp_lmd_primitive_to_string(string);
        if (item_is_error(string)) return string;
    }
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
    if (get_type_id(owner) == LMD_TYPE_ARRAY) return (Map*)owner.array;
    if (get_type_id(owner) != LMD_TYPE_MAP) return NULL;
    Map* map = owner.map;
    TypeMap* type = (TypeMap*)map->type;
    return type && type->nominal && !type->js_meta &&
        (map->map_kind == MAP_KIND_PLAIN || map->map_kind == MAP_KIND_ORDERED ||
         map->map_kind == MAP_KIND_GENERATOR || map->map_kind == MAP_KIND_ITERATOR) ? map : NULL;
}
static ShapeEntry* own_field(Map* map, String* key) {
    return typemap_hash_lookup_key((TypeMap*)map->type, key);
}
static const struct { const char* name; TypeId owner; int id; } methods[] = {
        {"get", LMD_TYPE_MAP, 1}, {"set", LMD_TYPE_MAP, 2}, {"has", LMD_TYPE_MAP, 3},
        {"delete", LMD_TYPE_MAP, 4}, {"clear", LMD_TYPE_MAP, 5}, {"entries", LMD_TYPE_MAP, 6},
        {"keys", LMD_TYPE_MAP, 7}, {"values", LMD_TYPE_MAP, 8},
        {"fill", LMD_TYPE_ARRAY, LMD_METHOD_FILL}, {"push", LMD_TYPE_ARRAY, LMD_METHOD_PUSH},
        {"pop", LMD_TYPE_ARRAY, LMD_METHOD_POP}, {"join", LMD_TYPE_ARRAY, LMD_METHOD_JOIN},
        {"slice", LMD_TYPE_ARRAY, LMD_METHOD_SLICE}, {"forEach", LMD_TYPE_ARRAY, LMD_METHOD_FOREACH},
        {"map", LMD_TYPE_ARRAY, LMD_METHOD_ARRAY_MAP},
        {"filter", LMD_TYPE_ARRAY, LMD_METHOD_FILTER}, {"indexOf", LMD_TYPE_ARRAY, LMD_METHOD_ARRAY_INDEX_OF},
        {"reverse", LMD_TYPE_ARRAY, LMD_METHOD_REVERSE}, {"sort", LMD_TYPE_ARRAY, LMD_METHOD_SORT},
        {"flat", LMD_TYPE_ARRAY, LMD_METHOD_FLAT}, {"reduce", LMD_TYPE_ARRAY, LMD_METHOD_REDUCE},
        {"some", LMD_TYPE_ARRAY, LMD_METHOD_SOME}, {"includes", LMD_TYPE_ARRAY, LMD_METHOD_INCLUDES},
        {"toString", LMD_TYPE_ARRAY, LMD_METHOD_ARRAY_STRING},
        {"concat", LMD_TYPE_ARRAY, LMD_METHOD_CONCAT}, {"splice", LMD_TYPE_ARRAY, LMD_METHOD_SPLICE},
        {"unshift", LMD_TYPE_ARRAY, LMD_METHOD_UNSHIFT},
        {"shift", LMD_TYPE_ARRAY, LMD_METHOD_SHIFT},
        {"toReversed", LMD_TYPE_ARRAY, LMD_METHOD_TO_REVERSED},
        {"at", LMD_TYPE_ARRAY, LMD_METHOD_AT},
        {"charAt", LMD_TYPE_STRING, LMD_METHOD_CHAR_AT}, {"charCodeAt", LMD_TYPE_STRING, LMD_METHOD_CODE_AT},
        {"repeat", LMD_TYPE_STRING, LMD_METHOD_REPEAT},
        {"substring", LMD_TYPE_STRING, LMD_METHOD_SUBSTRING}, {"slice", LMD_TYPE_STRING, LMD_METHOD_STRING_SLICE},
        {"split", LMD_TYPE_STRING, LMD_METHOD_SPLIT}, {"indexOf", LMD_TYPE_STRING, LMD_METHOD_INDEX_OF},
        {"startsWith", LMD_TYPE_STRING, LMD_METHOD_STARTS_WITH}, {"toUpperCase", LMD_TYPE_STRING, LMD_METHOD_UPPER},
        {"endsWith", LMD_TYPE_STRING, LMD_METHOD_ENDS_WITH}, {"padStart", LMD_TYPE_STRING, LMD_METHOD_PAD_START},
        {"match", LMD_TYPE_STRING, LMD_METHOD_MATCH}, {"replace", LMD_TYPE_STRING, LMD_METHOD_REPLACE},
        {"toLowerCase", LMD_TYPE_STRING, LMD_METHOD_LOWER}, {"toLocaleLowerCase", LMD_TYPE_STRING, LMD_METHOD_LOWER},
        {"toFixed", LMD_TYPE_FLOAT, LMD_METHOD_TO_FIXED},
        {"toString", LMD_TYPE_FLOAT, LMD_METHOD_NUMBER_STRING},
        {"toString", LMD_TYPE_DECIMAL, LMD_METHOD_BIGINT_STRING}};
TypeId mvp_lmd_method_owner(int id) {
    for (const auto& method : methods) if (method.id == id) return method.owner;
    return LMD_TYPE_NULL;
}
int mvp_lmd_builtin_method(String* key, TypeId owner) {
    for (const auto& method : methods)
        if (method.owner == owner && text_is(key, method.name)) return method.id;
    return 0;
}
extern "C" Item mvp_lmd_property_get(Item owner, Item name, int64_t callee) {
    TypeId tid = get_type_id(owner);
    if (tid == LMD_TYPE_ARRAY) {
        int64_t index = mvp_lmd_string_key(name.get_string());
        if (index >= 0) {
            Item value = index < owner.array->length ? owner.array->items[index] : Item{.item = ITEM_JS_UNDEFINED};
            return value.item == ITEM_JS_DELETED_SENTINEL ? Item{.item = ITEM_JS_UNDEFINED} : value;
        }
        if (text_is(name.get_string(), "length")) return Item{.item = i2it(owner.array->length)};
    }
    if (tid == LMD_TYPE_ARRAY && owner.array->type) {
        ShapeEntry* field = own_field((Map*)owner.array, name.get_string());
        if (field) return map_shape_field_to_item(owner.array->data, field);
    }
    if (tid == LMD_TYPE_ARRAY || tid == LMD_TYPE_STRING || tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT || tid == LMD_TYPE_DECIMAL) {
        int method = mvp_lmd_builtin_method(name.get_string(), tid == LMD_TYPE_INT ? LMD_TYPE_FLOAT : tid);
        if (method) return callee ? Item{.item = mvp_lmd_method_token(method)} : mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        return tid == LMD_TYPE_ARRAY ? Item{.item = ITEM_JS_UNDEFINED} : mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    }
    Map* map = object_face(owner);
    if (!map) return mvp_lmd_fail(get_type_id(owner) == LMD_TYPE_NULL ||
        get_type_id(owner) == LMD_TYPE_UNDEFINED ? LMD_MVP_TYPE : LMD_MVP_CAPABILITY, 0);
    String* key = name.get_string();
    ShapeEntry* field = own_field(map, key);
    if (field) return map_shape_field_to_item(map->data, field);
    if (map->map_kind == MAP_KIND_ORDERED) {
        if (text_is(key, "size")) return {.item = i2it(((OrderedMap*)map)->size)};
        // library collections have actual prototype functions; Set must not acquire Map-only methods.
        if (mvp_lmd_class_record(owner)) return Item{.item = ITEM_JS_UNDEFINED};
        int method = mvp_lmd_builtin_method(key);
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
    // audited MIR callers retain all arguments and scalar homes across this non-reentrant helper.
    Map* map = object_face(owner);
    if (!map) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    if (get_type_id(owner) == LMD_TYPE_ARRAY && !map->type) {
        // D2.6.6v3: the existing array attribute face owns and traces these packed fields.
        Input* tree = runtime_shape_tree();
        map->type = tree ? type_tree_map_root(tree) : NULL;
        if (!map->type) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    }
    ShapeEntry* field = own_field(map, name.get_string());
    if (map->map_kind == MAP_KIND_ORDERED && !field &&
            (text_is(name.get_string(), "size") || (!mvp_lmd_class_record(owner) && mvp_lmd_builtin_method(name.get_string()))))
        return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    if (field && (field->flags & (JSPD_IS_ACCESSOR | JSPD_NON_WRITABLE)))
        return field->flags & JSPD_IS_ACCESSOR ? mvp_lmd_fail(LMD_MVP_CAPABILITY, 0) : value;
    // descriptor admission already resolved this immutable field; reuse it for the physical write.
    return map_shape_set_resolved(map, name.get_string(), field, value) ? value : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}
extern "C" Item mvp_lmd_property_delete(Item owner, Item name) {
    Map* face = object_face(owner);
    ShapeEntry* field = face && face->type ? own_field(face, name.get_string()) : NULL;
    if (field && (field->flags & JSPD_NON_CONFIGURABLE)) return Item{.item = ITEM_FALSE};
    if (get_type_id(owner) == LMD_TYPE_ARRAY) {
        int64_t index = mvp_lmd_string_key(name.get_string());
        if (index == -1) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        if (index < -1) return !owner.array->type || map_shape_delete_resolved(face, field)
            ? Item{.item = ITEM_TRUE} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        if (index < owner.array->length) owner.array->items[index].item = ITEM_JS_DELETED_SENTINEL;
        return Item{.item = ITEM_TRUE};
    }
    // the MIR caller (including class dispatch) owns the roots throughout shared shape deletion.
    if (!face) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    // configurable admission and physical deletion use the same current-shape field.
    return map_shape_delete_resolved(face, field) ? Item{.item = ITEM_TRUE} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}
extern "C" Item mvp_lmd_define_data_property(Item owner, Item name, Item descriptor) {
    RootFrame roots(9);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner), key(roots, name), desc(roots, descriptor);
    TypeId type = get_type_id(descriptor);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_ARRAY && type != LMD_TYPE_FUNC)
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    Map* map = object_face(owner);
    if (!map) return mvp_lmd_fail(get_type_id(owner) == LMD_TYPE_FUNC ? LMD_MVP_CAPABILITY : LMD_MVP_TYPE, 0);
    if (get_type_id(owner) == LMD_TYPE_ARRAY && mvp_lmd_string_key(name.get_string()) >= -1)
        return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    const char* names[] = {"enumerable", "configurable", "value", "writable", "get", "set"};
    uint64_t* values[6]; bool present[6] = {};
    for (int i = 0; i < 6; i++) {
        values[i] = roots.take_slot(); *values[i] = ITEM_JS_UNDEFINED;
        String* spelling = heap_create_name(names[i], strlen(names[i]));
        if (!spelling) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        Item field_name = {.item = s2it(spelling)};
        Item exists = mvp_lmd_class_property(desc.get(), field_name, ItemNull, LMD_PROP_HAS, NULL);
        if (item_is_error(exists)) return exists;
        present[i] = exists.item == ITEM_TRUE;
        if (present[i]) {
            Item value = mvp_lmd_class_property(desc.get(), field_name, ItemNull, LMD_PROP_GET, NULL);
            if (item_is_error(value)) return value;
            *values[i] = value.item;
        }
    }
    if (present[4] || present[5]) return mvp_lmd_fail(present[2] || present[3] ? LMD_MVP_TYPE : LMD_MVP_CAPABILITY, 0);
    map = object.get().map;
    ShapeEntry* old = map->type ? own_field(map, key.get().get_string()) : NULL;
    uint8_t flags = old ? old->flags : JSPD_NON_ENUMERABLE | JSPD_NON_CONFIGURABLE | JSPD_NON_WRITABLE;
    if (flags & JSPD_IS_ACCESSOR) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    const int indices[] = {0, 1, 3};
    const uint8_t bits[] = {JSPD_NON_ENUMERABLE, JSPD_NON_CONFIGURABLE, JSPD_NON_WRITABLE};
    for (int i = 0; i < 3; i++) if (present[indices[i]]) {
        if (mvp_lmd_truthy(Item{.item = *values[indices[i]]})) flags &= ~bits[i]; else flags |= bits[i];
    }
    Item value = {.item = *values[2]};
    if (old && (old->flags & JSPD_NON_CONFIGURABLE)) {
        if (!(flags & JSPD_NON_CONFIGURABLE) || ((flags ^ old->flags) & JSPD_NON_ENUMERABLE) ||
                ((old->flags & JSPD_NON_WRITABLE) && !(flags & JSPD_NON_WRITABLE)))
            return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        if ((old->flags & JSPD_NON_WRITABLE) && present[2]) {
            Item previous = map_shape_field_to_item(map->data, old);
            bool same = key_equal(previous, value);
            if (same && is_number(previous) && it2d(previous) == 0 && is_number(value))
                same = signbit(it2d(previous)) == signbit(it2d(value));
            if (!same) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        }
    }
    if (present[2] || !old) {
        // A fresh Array acquires its attribute face through the shared property writer.
        bool stored = map->type ? map_shape_set_resolved(map, key.get().get_string(), old, value) :
            !item_is_error(mvp_lmd_property_set(object.get(), key.get(), value));
        if (!stored) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        map = object.get().map; old = own_field(map, key.get().get_string());
    }
    if (old->flags != flags) {
        Input* tree = runtime_shape_tree();
        TypeMap* transitioned = type_tree_reflag_field(tree, (TypeMap*)map->type, old, flags);
        if (transitioned) map->type = transitioned;
        else {
            if (!map_clone_typemap_for_mutation(map, context->pool)) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            own_field(map, key.get().get_string())->flags = flags;
        }
    }
    return object.get();
}
extern "C" Item mvp_lmd_property_has(Item owner, Item name, int64_t inherited) {
    if (get_type_id(owner) == LMD_TYPE_ARRAY) {
        String* key = name.get_string();
        int64_t index = mvp_lmd_string_key(key);
        if (index == -1) return Item{.item = ITEM_TRUE};
        if (index >= 0) return Item{.item = b2it(index < owner.array->length &&
            owner.array->items[index].item != ITEM_JS_DELETED_SENTINEL)};
        if (owner.array->type && own_field((Map*)owner.array, key)) return Item{.item = ITEM_TRUE};
        if (!inherited) return Item{.item = ITEM_FALSE};
        if (mvp_lmd_builtin_method(key, LMD_TYPE_ARRAY)) return Item{.item = ITEM_TRUE};
        return Item{.item = ITEM_FALSE};
    }
    Map* map = object_face(owner);
    // typed arrays are JS objects, but their reflection is outside this phase.
    if (!map) return mvp_lmd_fail(get_type_id(owner) == LMD_TYPE_ARRAY_NUM
        ? LMD_MVP_CAPABILITY : LMD_MVP_TYPE, 0);
    String* key = name.get_string();
    if (own_field(map, key)) return Item{.item = ITEM_TRUE};
    if (inherited) {
        if (map->map_kind == MAP_KIND_ORDERED && (text_is(key, "size") || mvp_lmd_builtin_method(key))) return Item{.item = ITEM_TRUE};
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
static int64_t entry_find(OrderedMap* map, Item key, uint64_t hash, int64_t* previous, int64_t* head) {
    MapBucket query = {hash, -1};
    const MapBucket* bucket = (const MapBucket*)hashmap_get(map->index, &query);
    *previous = -1; *head = bucket ? bucket->slot : -1;
    for (int64_t slot = *head; slot >= 0; ) {
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
// keep allocation/root ownership off the read and immediate-update stack frame.
static Item map_call_owned(Item owner, Item method_item, Item key, Item value,
        int64_t slot, uint64_t hash, int64_t head) {
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    uint64_t key_home = 0, value_home = 0;
    Rooted<Item> object(roots, owner), k(roots, lambda_item_adopt_scalar_home(key, &key_home)),
        v(roots, lambda_item_adopt_scalar_home(value, &value_home));
    Item canonical = canonical_string(k.get());
    if (get_type_id(canonical) == LMD_TYPE_ERROR) return canonical;
    if (canonical.item != k.get().item)
        return mvp_lmd_map_call(object.get(), method_item, canonical, v.get());
    OrderedMap* map = (OrderedMap*)object.get().map;
    int64_t previous = -1;
    if (slot >= 0) {
        Item stored = store(map->entries, slot + 1, v.get());
        return get_type_id(stored) == LMD_TYPE_ERROR ? stored : object.get();
    }
    Array* old_entries = map->entries;
    if (!compact_entries(map)) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    if (old_entries != map->entries) slot = entry_find(map, k.get(), hash, &previous, &head);
    slot = map->entries->length;
    if (slot > UINT32_MAX - 4 || !array_reserve_append_slots(map->entries, 8))
        return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    // publish the index only after every owned slot is installed successfully.
    Item stored_key = is_number(k.get()) && it2d(k.get()) == 0 ? Item{.item = i2it(0)} : k.get();
    Item row[4] = {stored_key, v.get(), Item{.item = i2it(head)}, Item{.item = ITEM_TRUE}};
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
    // learn only existing-entry hits: unique insertions do not benefit from a last-key probe.
    map->size++; return object.get();
}
extern "C" Item mvp_lmd_map_call(Item owner, Item method_item, Item key, Item value) {
    int64_t method = (method_item.item & 0xffff) >> 8;
    Map* face = object_face(owner);
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
    // canonical keys, immediate updates and read/delete hits need no GC-capable setup.
    if (get_type_id(key) == LMD_TYPE_STRING &&
            !utf8_key_is_canonical(key.get_string()->chars, key.get_string()->len))
        return map_call_owned(owner, method_item, key, value, -1, 0, -1);
    uint64_t hash = 0;
    int64_t previous = -1, head = -1, slot = map->last_entry - 1;
    // ordinals retain no GC edges; validate the live key after deletion, clear or compaction.
    // deletion still walks the bucket because it needs the predecessor link.
    if (method == 4 || slot < 0 || slot >= map->entries->length ||
            map->entries->items[slot + 3].item != ITEM_TRUE || !key_equal(map->entries->items[slot], key)) {
        hash = key_hash(key);
        slot = entry_find(map, key, hash, &previous, &head);
        map->last_entry = slot + 1;
    }
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
    if (slot >= 0 && !lambda_item_uses_scalar_home(value)) {
        map->entries->items[slot + 1] = value;
        return owner;
    }
    return map_call_owned(owner, method_item, key, value, slot, hash, head);
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
extern "C" Item mvp_lmd_for_in_keys(Item owner) {
    RootFrame roots(5);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner), result(roots, mvp_lmd_array_new(0));
    Rooted<Item> seen(roots, ItemNull), keys(roots, ItemNull), key(roots, ItemNull);
    if (item_is_error(result.get())) return result.get();
    if (owner.item == ITEM_NULL || owner.item == ITEM_JS_UNDEFINED) return result.get();
    seen.set(mvp_lmd_array_new(0));
    if (item_is_error(seen.get())) return seen.get();
    if (get_type_id(owner) == LMD_TYPE_STRING) {
        int64_t length = utf8_to_utf16_length(owner.get_string()->chars, owner.get_string()->len);
        for (int64_t i = 0; i < length; i++) {
            key.set(mvp_lmd_number_to_string(i));
            if (item_is_error(key.get())) return key.get();
            Item stored = mvp_lmd_array_store(result.get(), i, key.get());
            if (item_is_error(stored)) return stored;
        }
        return result.get();
    }
    TypeId type = get_type_id(owner);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_ARRAY && type != LMD_TYPE_FUNC) return result.get();
    MvpLmdClass* cls = mvp_lmd_class_record(owner);
    bool statics = type == LMD_TYPE_FUNC;
    if (statics) {
        if (!cls) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        object.set(mvp_lmd_class_values(cls)[2]);
    }
    TypeNominal* nominal = cls ? &cls->nominal : NULL;
    if (cls && (statics || object.get().item == mvp_lmd_class_values(cls)[1].item)) nominal = nominal->base;
    for (;;) {
        keys.set(mvp_lmd_object_project(object.get(), 3));
        if (item_is_error(keys.get())) return keys.get();
        for (int64_t i = 0; i < keys.get().array->length; i++) {
            key.set(keys.get().array->items[i]); bool visited = false;
            for (int64_t j = 0; j < seen.get().array->length; j++)
                if (key_equal(key.get(), seen.get().array->items[j])) { visited = true; break; }
            if (visited) continue;
            Item stored = mvp_lmd_array_store(seen.get(), seen.get().array->length, key.get());
            if (item_is_error(stored)) return stored;
            ShapeEntry* field = object.get().map->type ? own_field(object.get().map, key.get().get_string()) : NULL;
            if (field && (field->flags & JSPD_NON_ENUMERABLE)) continue;
            stored = mvp_lmd_array_store(result.get(), result.get().array->length, key.get());
            if (item_is_error(stored)) return stored;
        }
        if (!nominal) break;
        if (nominal->extension != &mvp_lmd_class_extension) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        cls = (MvpLmdClass*)nominal->extension_data;
        object.set(mvp_lmd_class_values(cls)[statics ? 2 : 1]); nominal = nominal->base;
    }
    return result.get();
}
extern "C" Item mvp_lmd_object_project(Item owner, int64_t projection) {
    Map* map = object_face(owner);
    bool indexed = get_type_id(owner) == LMD_TYPE_ARRAY;
    if (!map && !indexed) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    if (!context->name_pool) context->name_pool = name_pool_create_runtime(context->pool);
    TypeMap* type = map ? (TypeMap*)map->type : NULL;
    RootFrame roots(5);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner);
    Rooted<Array*> result(roots, array()), pair(roots, NULL);
    Rooted<Item> key(roots, ItemNull), value(roots, ItemNull);
    if (!result.get()) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    ProjectionField* fields = type ? (ProjectionField*)pool_calloc(context->pool, type->length * sizeof(ProjectionField)) : NULL;
    if (type && !fields && type->length) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    int64_t indexed_count = indexed ? object.get().array->length : 0;
    int64_t count = 0;
    if (type) {
        FOR_EACH_MAP_FIELD(type, field) {
            if ((field->key_kind != NAME_KEY_STRING && projection != 4) || (projection != 3 && (field->flags & JSPD_NON_ENUMERABLE))) continue;
            String* name = field->key_kind != NAME_KEY_STRING ? name_pool_resolve_id(context->name_pool, field->name_id) :
                heap_create_name(field->name->str, field->name->length);
            if (!name) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            int64_t index = mvp_lmd_string_key(name);
            fields[count] = {field, index >= 0 ? (uint32_t)index : UINT32_MAX,
                count + (field->key_kind != NAME_KEY_STRING ? type->length : 0)}; count++;
        }
        // creation ordinals break ties so ordinary string keys retain source order.
        qsort(fields, (size_t)count, sizeof(ProjectionField), projection_order);
    }
    uint64_t value_home = 0;
    for (int64_t i = 0; i < indexed_count + count; i++) {
        bool element = i < indexed_count;
        if (element && object.get().array->items[i].item == ITEM_JS_DELETED_SENTINEL) continue;
        ShapeEntry* field = element ? NULL : fields[i - indexed_count].field;
        if (projection != 1) {
            key.set(element ? mvp_lmd_number_to_string((double)i) :
                Item{.item = s2it(field->key_kind != NAME_KEY_STRING ? name_pool_resolve_id(context->name_pool, field->name_id) :
                    heap_create_name(field->name->str, field->name->length))});
            if (item_is_error(key.get())) return key.get();
        }
        // snapshots must outlive pair allocation and any subsequent storage growth.
        value.set(projection == 0 || projection >= 3 ? key.get() : lambda_item_adopt_scalar_home(element
            ? object.get().array->items[i] : map_shape_field_to_item(object.get().map->data, field), &value_home));
        if (projection == 2) {
            pair.set(array());
            if (!pair.get()) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            Item written = store(pair.get(), 0, key.get());
            if (get_type_id(written) == LMD_TYPE_ERROR) return written;
            written = store(pair.get(), 1, value.get());
            if (get_type_id(written) == LMD_TYPE_ERROR) return written;
            value.set(Item{.array = pair.get()});
        }
        Item written = store(result.get(), result.get()->length, value.get());
        if (get_type_id(written) == LMD_TYPE_ERROR) return written;
    }
    if (fields) pool_free(context->pool, fields);
    return Item{.array = result.get()};
}
extern "C" Item mvp_lmd_object_copy(Item target, Item source, int64_t assign) {
    TypeId type = get_type_id(source);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_ARRAY && type != LMD_TYPE_FUNC && type != LMD_TYPE_STRING) return target;
    RootFrame roots(5);
    if (!roots.valid()) return ItemError;
    Rooted<Item> output(roots, target), input(roots, source), keys(roots, ItemNull), key(roots, ItemNull), value(roots, ItemNull);
    if (type == LMD_TYPE_FUNC) {
        Item ready = mvp_lmd_function_prepare(input.get());
        if (item_is_error(ready)) return ready;
        input.set(mvp_lmd_class_values(mvp_lmd_class_record(input.get()))[2]);
    }
    if (type != LMD_TYPE_STRING) {
        keys.set(mvp_lmd_object_project(input.get(), 4));
        if (item_is_error(keys.get())) return keys.get();
    }
    int64_t length = type == LMD_TYPE_STRING ? utf8_to_utf16_length(input.get().get_string()->chars, input.get().get_string()->len) : keys.get().array->length;
    uint64_t home = 0;
    for (int64_t i = 0; i < length; i++) {
        key.set(type == LMD_TYPE_STRING ? mvp_lmd_number_to_string(i) : keys.get().array->items[i]);
        if (item_is_error(key.get())) return key.get();
        value.set(type == LMD_TYPE_STRING ? mvp_lmd_string_at(input.get(), i, LMD_STRING_INDEX) :
            mvp_lmd_class_property(input.get(), key.get(), ItemNull, LMD_PROP_GET, NULL));
        if (item_is_error(value.get())) return value.get();
        value.set(lambda_item_adopt_scalar_home(value.get(), &home));
        Item stored = assign ? mvp_lmd_class_property(output.get(), key.get(), value.get(), LMD_PROP_SET | LMD_PROP_STRICT, NULL) :
            map_shape_set(output.get().map, key.get().get_string(), value.get()) ? output.get() : ItemError;
        if (item_is_error(stored)) return stored;
    }
    return output.get();
}
