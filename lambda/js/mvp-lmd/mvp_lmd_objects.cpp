#include "mvp_lmd_runtime.h"
#include "mvp_lmd_dynamic.h"
#include "../js_property_attrs.h"
#include "../js_props.h"
#include "../js_runtime.h"
#include "../js_host_hooks.h"
#include "../js_source_module.hpp"
#include "../js_typed_array.h"
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
extern "C" int64_t mvp_lmd_same_value_zero(Item a, Item b) {
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
bool mvp_lmd_same_value(Item left, Item right) {
    if (!mvp_lmd_same_value_zero(left, right)) return false;
    return !is_number(left) || !is_number(right) || it2d(left) != 0 || signbit(it2d(left)) == signbit(it2d(right));
}
bool mvp_lmd_descriptor_compatible(bool extensible, const JsPropertyDescriptor* update, const JsPropertyDescriptor* current) {
    if (!current) return extensible;
    if (js_pd_is_configurable(current)) return true;
    if (((update->flags & JS_PD_HAS_CONFIGURABLE) && js_pd_is_configurable(update)) ||
            ((update->flags & JS_PD_HAS_ENUMERABLE) && ((update->flags ^ current->flags) & JS_PD_ENUMERABLE))) return false;
    bool accessor = js_pd_is_accessor(update), data = js_pd_is_data(update);
    if (!accessor && !data) return true;
    if (accessor != js_pd_is_accessor(current)) return false;
    if (accessor) {
        return (!(update->flags & JS_PD_HAS_GET) || mvp_lmd_same_value(update->getter, current->getter)) &&
            (!(update->flags & JS_PD_HAS_SET) || mvp_lmd_same_value(update->setter, current->setter));
    }
    if (current->flags & JS_PD_WRITABLE) return true;
    return !((update->flags & JS_PD_HAS_WRITABLE) && (update->flags & JS_PD_WRITABLE)) &&
        (!(update->flags & JS_PD_HAS_VALUE) || mvp_lmd_same_value(update->value, current->value));
}
static Item canonical_string(Item value) {
    if (get_type_id(value) != LMD_TYPE_STRING) return value;
    String* s = value.get_string();
    // ASCII metadata already proves canonical UTF-8, including embedded NULs.
    if (s->is_ascii || utf8_key_is_canonical(s->chars, s->len)) return value;
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
    return canonical_string(string);
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
Map* mvp_lmd_property_map(Item owner, bool writing, bool numeric_attributes) {
    TypeId kind = get_type_id(owner);
    if (kind == LMD_TYPE_MAP) {
        Map* map = owner.map;
        TypeMap* type = (TypeMap*)map->type;
        // ordinary reads need only the packed carrier; writes also require an MVP nominal owner.
        if (!type || type->js_meta || (writing && (!type->nominal ||
                (type->nominal->extension != &mvp_lmd_class_extension &&
                 type->nominal->extension != &mvp_lmd_plain_extension)))) return NULL;
        return map->map_kind == MAP_KIND_PLAIN || map->map_kind == MAP_KIND_ORDERED ||
            map->map_kind == MAP_KIND_GENERATOR || map->map_kind == MAP_KIND_ITERATOR ||
            map->map_kind == MAP_KIND_ARRAYBUFFER || map->map_kind == MAP_KIND_DATAVIEW ||
            map->map_kind == MAP_KIND_TYPED_ARRAY || map->map_kind == MAP_KIND_COLLECTION ||
            map->map_kind == MAP_KIND_WEAK ? map : NULL;
    }
    if (kind == LMD_TYPE_VMAP && js_promise_is(owner)) {
        Item storage = js_promise_property_storage(owner, writing);
        return get_type_id(storage) == LMD_TYPE_MAP ? storage.map : NULL;
    }
    if (writing && (kind == LMD_TYPE_ARRAY || kind == LMD_TYPE_ARRAY_NUM)) {
        TypeMap* shape = (TypeMap*)owner.map->type;
        // D3.2.4v4: guest descriptor operations cannot rewrite a foreign declared prefix.
        if (shape && (shape->is_trusted_contract || shape->nominal) && (!shape->nominal ||
                (shape->nominal->extension != &mvp_lmd_class_extension &&
                 shape->nominal->extension != &mvp_lmd_plain_extension))) return NULL;
    }
    if (kind == LMD_TYPE_FUNC) {
        MvpLmdClass* cls = mvp_lmd_class_record(owner);
        return cls ? mvp_lmd_class_values(cls)[2].map : NULL;
    }
    if (kind == LMD_TYPE_ARRAY) return js_array_has_props(owner.array) ? js_array_props(owner.array) : (Map*)owner.array;
    // only raw named reads/writes use this face; reflection still requires full typed storage.
    return numeric_attributes && kind == LMD_TYPE_ARRAY_NUM ? owner.map : NULL;
}
bool mvp_lmd_typed_array_index(Item owner, Item key, double* index) {
    if (get_type_id(owner) != LMD_TYPE_MAP || owner.map->map_kind != MAP_KIND_TYPED_ARRAY) return false;
    bool negative_zero;
    bool numeric = js_ta_key_canonical_numeric(key, index, &negative_zero);
    if (numeric && negative_zero) *index = -0.0;
    return numeric;
}
Item mvp_lmd_typed_array_property(Item owner, Item key, Item value, int64_t operation, Item receiver) {
    double number; bool negative_zero; int index;
    if (!js_ta_key_canonical_numeric(key, &number, &negative_zero)) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    bool valid = js_ta_numeric_index_valid(owner, number, negative_zero, &index);
    bool strict = operation & LMD_PROP_STRICT;
    operation &= ~LMD_PROP_STRICT;
    if (operation == LMD_PROP_GET || operation == LMD_PROP_CALLEE)
        return valid ? js_typed_array_get(owner, Item{.item = i2it(index)}) : Item{.item = ITEM_JS_UNDEFINED};
    if (operation == LMD_PROP_OWN || operation == LMD_PROP_HAS) return Item{.item = b2it(valid)};
    if (operation == LMD_PROP_DELETE) return valid && strict ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : Item{.item = b2it(!valid)};
    if (operation == LMD_PROP_SET) {
        Item status = mvp_lmd_set_completion(owner, key, value, receiver);
        return item_is_error(status) ? status : status.item == ITEM_FALSE && strict ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : value;
    }
    return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
}
static ShapeEntry* own_field(Map* map, String* key) {
    return typemap_hash_lookup_key((TypeMap*)map->type, key);
}
const MvpLmdBuiltinMethod mvp_lmd_builtin_methods[] = {
        {"get", LMD_TYPE_MAP, 1}, {"set", LMD_TYPE_MAP, 2, 2}, {"has", LMD_TYPE_MAP, 3},
        {"delete", LMD_TYPE_MAP, 4}, {"clear", LMD_TYPE_MAP, 5, 0}, {"entries", LMD_TYPE_MAP, 6, 0},
        {"keys", LMD_TYPE_MAP, 7, 0}, {"values", LMD_TYPE_MAP, 8, 0},
        {"fill", LMD_TYPE_ARRAY, LMD_METHOD_FILL}, {"push", LMD_TYPE_ARRAY, LMD_METHOD_PUSH},
        {"pop", LMD_TYPE_ARRAY, LMD_METHOD_POP, 0}, {"join", LMD_TYPE_ARRAY, LMD_METHOD_JOIN},
        {"slice", LMD_TYPE_ARRAY, LMD_METHOD_SLICE, 2}, {"forEach", LMD_TYPE_ARRAY, LMD_METHOD_FOREACH},
        {"map", LMD_TYPE_ARRAY, LMD_METHOD_ARRAY_MAP},
        {"filter", LMD_TYPE_ARRAY, LMD_METHOD_FILTER}, {"indexOf", LMD_TYPE_ARRAY, LMD_METHOD_ARRAY_INDEX_OF},
        {"reverse", LMD_TYPE_ARRAY, LMD_METHOD_REVERSE, 0}, {"sort", LMD_TYPE_ARRAY, LMD_METHOD_SORT},
        {"flat", LMD_TYPE_ARRAY, LMD_METHOD_FLAT, 0}, {"reduce", LMD_TYPE_ARRAY, LMD_METHOD_REDUCE},
        {"some", LMD_TYPE_ARRAY, LMD_METHOD_SOME}, {"includes", LMD_TYPE_ARRAY, LMD_METHOD_INCLUDES},
        {"toString", LMD_TYPE_ARRAY, LMD_METHOD_ARRAY_STRING, 0},
        {"concat", LMD_TYPE_ARRAY, LMD_METHOD_CONCAT}, {"splice", LMD_TYPE_ARRAY, LMD_METHOD_SPLICE, 2},
        {"unshift", LMD_TYPE_ARRAY, LMD_METHOD_UNSHIFT},
        {"shift", LMD_TYPE_ARRAY, LMD_METHOD_SHIFT, 0},
        {"toReversed", LMD_TYPE_ARRAY, LMD_METHOD_TO_REVERSED, 0},
        {"at", LMD_TYPE_ARRAY, LMD_METHOD_AT},
        {"every", LMD_TYPE_ARRAY, LMD_METHOD_EVERY}, {"find", LMD_TYPE_ARRAY, LMD_METHOD_FIND},
        {"findIndex", LMD_TYPE_ARRAY, LMD_METHOD_FIND_INDEX}, {"reduceRight", LMD_TYPE_ARRAY, LMD_METHOD_REDUCE_RIGHT},
        {"lastIndexOf", LMD_TYPE_ARRAY, LMD_METHOD_LAST_INDEX_OF},
        {"flatMap", LMD_TYPE_ARRAY, LMD_METHOD_FLAT_MAP},
        {"findLast", LMD_TYPE_ARRAY, LMD_METHOD_FIND_LAST},
        {"findLastIndex", LMD_TYPE_ARRAY, LMD_METHOD_FIND_LAST_INDEX},
        {"charAt", LMD_TYPE_STRING, LMD_METHOD_CHAR_AT}, {"charCodeAt", LMD_TYPE_STRING, LMD_METHOD_CODE_AT},
        {"repeat", LMD_TYPE_STRING, LMD_METHOD_REPEAT},
        {"substring", LMD_TYPE_STRING, LMD_METHOD_SUBSTRING, 2}, {"slice", LMD_TYPE_STRING, LMD_METHOD_STRING_SLICE, 2},
        {"split", LMD_TYPE_STRING, LMD_METHOD_SPLIT, 2}, {"indexOf", LMD_TYPE_STRING, LMD_METHOD_INDEX_OF},
        {"startsWith", LMD_TYPE_STRING, LMD_METHOD_STARTS_WITH}, {"toUpperCase", LMD_TYPE_STRING, LMD_METHOD_UPPER, 0},
        {"endsWith", LMD_TYPE_STRING, LMD_METHOD_ENDS_WITH}, {"padStart", LMD_TYPE_STRING, LMD_METHOD_PAD_START},
        {"search", LMD_TYPE_STRING, LMD_METHOD_SEARCH},
        {"matchAll", LMD_TYPE_STRING, LMD_METHOD_MATCH_ALL},
        {"match", LMD_TYPE_STRING, LMD_METHOD_MATCH}, {"replace", LMD_TYPE_STRING, LMD_METHOD_REPLACE, 2},
        {"toLowerCase", LMD_TYPE_STRING, LMD_METHOD_LOWER, 0}, {"toLocaleLowerCase", LMD_TYPE_STRING, LMD_METHOD_LOWER, 0},
        {"concat", LMD_TYPE_STRING, LMD_METHOD_STRING_CONCAT},
        {"trim", LMD_TYPE_STRING, LMD_METHOD_TRIM, 0}, {"trimStart", LMD_TYPE_STRING, LMD_METHOD_TRIM_START, 0},
        {"trimEnd", LMD_TYPE_STRING, LMD_METHOD_TRIM_END, 0},
        {"trimLeft", LMD_TYPE_STRING, LMD_METHOD_TRIM_START, 0}, {"trimRight", LMD_TYPE_STRING, LMD_METHOD_TRIM_END, 0},
        {"codePointAt", LMD_TYPE_STRING, LMD_METHOD_CODE_POINT_AT}, {"at", LMD_TYPE_STRING, LMD_METHOD_STRING_AT},
        {"includes", LMD_TYPE_STRING, LMD_METHOD_STRING_INCLUDES}, {"padEnd", LMD_TYPE_STRING, LMD_METHOD_PAD_END},
        {"lastIndexOf", LMD_TYPE_STRING, LMD_METHOD_STRING_LAST_INDEX_OF},
        {"toFixed", LMD_TYPE_FLOAT, LMD_METHOD_TO_FIXED},
        {"toString", LMD_TYPE_FLOAT, LMD_METHOD_NUMBER_STRING},
        {"toString", LMD_TYPE_DECIMAL, LMD_METHOD_BIGINT_STRING},
        {"copyWithin", LMD_TYPE_ARRAY, LMD_METHOD_COPY_WITHIN, 2},
        {"toSorted", LMD_TYPE_ARRAY, LMD_METHOD_TO_SORTED, 1},
        {"toSpliced", LMD_TYPE_ARRAY, LMD_METHOD_TO_SPLICED, 2},
        {"with", LMD_TYPE_ARRAY, LMD_METHOD_WITH, 2},
        {"normalize", LMD_TYPE_STRING, LMD_METHOD_STRING_NORMALIZE, 0},
        {"localeCompare", LMD_TYPE_STRING, LMD_METHOD_STRING_LOCALE_COMPARE, 1},
        {"isWellFormed", LMD_TYPE_STRING, LMD_METHOD_STRING_IS_WELL_FORMED, 0},
        {"toWellFormed", LMD_TYPE_STRING, LMD_METHOD_STRING_TO_WELL_FORMED, 0},
        {"anchor", LMD_TYPE_STRING, LMD_METHOD_STRING_ANCHOR, 1},
        {"big", LMD_TYPE_STRING, LMD_METHOD_STRING_BIG, 0},
        {"blink", LMD_TYPE_STRING, LMD_METHOD_STRING_BLINK, 0},
        {"bold", LMD_TYPE_STRING, LMD_METHOD_STRING_BOLD, 0},
        {"fixed", LMD_TYPE_STRING, LMD_METHOD_STRING_FIXED, 0},
        {"fontcolor", LMD_TYPE_STRING, LMD_METHOD_STRING_FONTCOLOR, 1},
        {"fontsize", LMD_TYPE_STRING, LMD_METHOD_STRING_FONTSIZE, 1},
        {"italics", LMD_TYPE_STRING, LMD_METHOD_STRING_ITALICS, 0},
        {"link", LMD_TYPE_STRING, LMD_METHOD_STRING_LINK, 1},
        {"small", LMD_TYPE_STRING, LMD_METHOD_STRING_SMALL, 0},
        {"strike", LMD_TYPE_STRING, LMD_METHOD_STRING_STRIKE, 0},
        {"sub", LMD_TYPE_STRING, LMD_METHOD_STRING_SUB, 0},
        {"sup", LMD_TYPE_STRING, LMD_METHOD_STRING_SUP, 0},
        {"toLocaleUpperCase", LMD_TYPE_STRING, LMD_METHOD_UPPER, 0},
        {"substr", LMD_TYPE_STRING, LMD_METHOD_STRING_SUBSTR, 2},
        {"replaceAll", LMD_TYPE_STRING, LMD_METHOD_REPLACE_ALL, 2}};
const int mvp_lmd_builtin_method_count = sizeof(mvp_lmd_builtin_methods) / sizeof(mvp_lmd_builtin_methods[0]);
TypeId mvp_lmd_method_owner(int id) {
    for (const auto& method : mvp_lmd_builtin_methods) if (method.id == id) return method.owner;
    return LMD_TYPE_NULL;
}
int mvp_lmd_builtin_method(String* key, TypeId owner) {
    for (const auto& method : mvp_lmd_builtin_methods)
        if (method.owner == owner && text_is(key, method.name)) return method.id;
    return 0;
}
static Item mvp_lmd_array_define_length(Item owner, const JsPropertyDescriptor* descriptor);
Item mvp_lmd_array_property(Item owner, Item name, Item value, int64_t operation, Item receiver) {
    bool strict = (operation & LMD_PROP_STRICT) != 0;
    operation &= ~LMD_PROP_STRICT;
    if (operation >= LMD_PROP_KEYS && operation <= LMD_PROP_ENTRIES)
        return mvp_lmd_object_project(owner, operation - LMD_PROP_KEYS);
    Map* map = mvp_lmd_property_map(owner, operation == LMD_PROP_SET || operation == LMD_PROP_DELETE ||
        operation == LMD_PROP_INITIALIZE || operation == LMD_PROP_CREATE_DATA);
    if (!map) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    ShapeEntry* field = map->type ? own_field(map, name.get_string()) : NULL;
    int64_t index = mvp_lmd_string_key(name.get_string());
    bool length = index == -1;
    Item element = index >= 0 ? mvp_lmd_array_own_item(owner.array, index) : Item{.item = ITEM_JS_DELETED_SENTINEL};
    bool found = length || field || element.item != ITEM_JS_DELETED_SENTINEL;
    if (operation == LMD_PROP_OWN) return Item{.item = b2it(found)};
    if (found && (operation == LMD_PROP_HAS || operation == LMD_PROP_GET || operation == LMD_PROP_CALLEE)) {
        // own storage reads cannot collect; accessor invocation owns its precise receiver/callable roots.
        if (operation == LMD_PROP_HAS) return Item{.item = ITEM_TRUE};
        if (length) return Item{.item = i2it(owner.array->length)};
        if (field) return field->flags & JSPD_IS_ACCESSOR
            ? mvp_lmd_accessor_call(receiver, field, ItemNull, LMD_PROP_GET) : map_shape_field_to_item(map->data, field);
        return element;
    }
    if (operation == LMD_PROP_HAS || operation == LMD_PROP_GET || operation == LMD_PROP_CALLEE) {
        if (mvp_lmd_program_library()) {
            // an Array's own prototype lookup is passive; tail dispatch retains no live caller state.
            Item parent = mvp_lmd_get_prototype(owner);
            if (item_is_error(parent)) return parent;
            TypeId kind = get_type_id(parent);
            if ((kind == LMD_TYPE_MAP || kind == LMD_TYPE_ARRAY) && !mvp_lmd_is_proxy(parent))
                return mvp_lmd_property_with_receiver(parent, name, ItemNull, operation, receiver);
            if (parent.item != ITEM_NULL) {
                // lazy function preparation can collect before semantic dispatch has rooted its inputs.
                RootFrame roots(3);
                if (!roots.valid()) return ItemError;
                Rooted<Item> prototype(roots, parent), key(roots, name), target(roots, receiver);
                return mvp_lmd_property_with_receiver(prototype.get(), key.get(), ItemNull, operation, target.get());
            }
        } else if (operation == LMD_PROP_CALLEE) {
            int method = mvp_lmd_builtin_method(name.get_string(), LMD_TYPE_ARRAY);
            if (method) return Item{.item = mvp_lmd_method_token(method)};
        }
        return Item{.item = operation == LMD_PROP_HAS ? ITEM_FALSE : ITEM_JS_UNDEFINED};
    }
    RootFrame roots(5);
    if (!roots.valid()) return ItemError;
    uint64_t home = 0;
    Rooted<Item> object(roots, owner), key(roots, name), target(roots, receiver);
    Rooted<Item> argument(roots, lambda_item_adopt_scalar_home(value, &home)), prototype(roots, ItemNull);
    if (operation == LMD_PROP_DELETE) {
        Item deleted = mvp_lmd_property_delete(object.get(), key.get());
        return strict && deleted.item == ITEM_FALSE ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : deleted;
    }
    bool define = operation == LMD_PROP_INITIALIZE || operation == LMD_PROP_CREATE_DATA;
    auto assignment_value = [&]() { return lambda_item_uses_scalar_home(argument.get()) ? push_d(it2d(argument.get())) : argument.get(); };
    auto rejected = [&]() { return strict ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : assignment_value(); };
    if (!define && field && (field->flags & JSPD_IS_ACCESSOR))
        return mvp_lmd_accessor_call(target.get(), field, argument.get(), LMD_PROP_SET | (strict ? LMD_PROP_STRICT : 0));
    if (field && (field->flags & JSPD_NON_WRITABLE)) return rejected();
    if (!found && !define && mvp_lmd_program_library()) {
        prototype.set(mvp_lmd_get_prototype(object.get()));
        if (item_is_error(prototype.get())) return prototype.get();
        if (prototype.get().item != ITEM_NULL)
            return mvp_lmd_property_with_receiver(prototype.get(), key.get(), argument.get(),
                LMD_PROP_SET | (strict ? LMD_PROP_STRICT : 0), target.get());
    }
    if (target.get().item != object.get().item)
        return mvp_lmd_property_with_receiver(target.get(), key.get(), argument.get(),
            LMD_PROP_INITIALIZE | (strict ? LMD_PROP_STRICT : 0), target.get());
    if (!found) {
        Item allowed = mvp_lmd_object_extensible(object.get(), false);
        if (item_is_error(allowed)) return allowed;
        if (allowed.item == ITEM_FALSE) return rejected();
    }
    if (length) {
        JsPropertyDescriptor descriptor = {};
        descriptor.flags = JS_PD_HAS_VALUE; descriptor.value = argument.get();
        Item status = mvp_lmd_array_define_length(object.get(), &descriptor);
        return item_is_error(status) ? status : status.item == ITEM_FALSE ? rejected() : assignment_value();
    }
    if (index >= 0 && index >= object.get().array->length) {
        ShapeEntry* policy = map->type ? typemap_hash_lookup((TypeMap*)map->type, "length", 6) : NULL;
        if (policy && (policy->flags & JSPD_NON_WRITABLE)) return rejected();
    }
    if (index >= 0 && !field) {
        Item status = mvp_lmd_array_store(object.get(), (uint32_t)index, argument.get());
        return item_is_error(status) ? status : assignment_value();
    }
    if (!map->type) map->type = type_tree_map_root(runtime_shape_tree());
    if (!map_shape_set_resolved(map, key.get().get_string(), field, argument.get(), false)) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    if (index >= object.get().array->length) object.get().array->length = index + 1;
    return lambda_item_uses_scalar_home(argument.get()) ? push_d(it2d(argument.get())) : argument.get();
}
extern "C" Item mvp_lmd_array_index_property(Item owner, int64_t index, Item value, int64_t operation) {
    bool own_storage = operation & LMD_PROP_OWN_STORAGE;
    operation &= ~LMD_PROP_OWN_STORAGE;
    int64_t op = operation & ~LMD_PROP_STRICT;
    // 2^32-1 and larger integer keys are ordinary named properties, never array indices.
    if (get_type_id(owner) == LMD_TYPE_ARRAY && index < UINT32_MAX) {
        if (op == LMD_PROP_GET || op == LMD_PROP_PRESENT || op == LMD_PROP_HAS || op == LMD_PROP_OWN) {
            if (index == -1) return op == LMD_PROP_HAS || op == LMD_PROP_OWN ? Item{.item = ITEM_TRUE} : Item{.item = i2it(owner.array->length)};
            Item element = mvp_lmd_array_own_item(owner.array, index);
            // indexed descriptors remove the physical value; present slots are ordinary own data.
            if (element.item != ITEM_JS_DELETED_SENTINEL) {
                return op == LMD_PROP_HAS || op == LMD_PROP_OWN ? Item{.item = ITEM_TRUE} : element;
            }
            // present own data is independent of prototypes; consult program policy only on a miss.
            MvpLmdLibraryState* state = own_storage ? NULL : mvp_lmd_program_library();
            if (!state || !state->mutable_prototypes ||
                    (index >= 0 && mvp_lmd_array_indices_use_own_storage(owner))) {
                // reuse the live chain proof before interning a hole's index key.
                if (op == LMD_PROP_HAS || op == LMD_PROP_OWN) return Item{.item = ITEM_FALSE};
                return op == LMD_PROP_GET ? Item{.item = ITEM_JS_UNDEFINED} : element;
            }
        } else {
            if (own_storage && op == LMD_PROP_CREATE_DATA && index >= 0) {
                // intrinsic result arrays have no exposed aliases or indexed descriptors.
                Item stored = mvp_lmd_array_store(owner, (uint32_t)index, value);
                return item_is_error(stored) ? stored : Item{.item = ITEM_TRUE};
            }
            MvpLmdLibraryState* state = own_storage ? NULL : mvp_lmd_program_library();
            bool fixed_prototypes = !state || !state->mutable_prototypes;
            if (op == LMD_PROP_SET && index >= 0 && (fixed_prototypes ||
                    mvp_lmd_array_own_item(owner.array, index).item != ITEM_JS_DELETED_SENTINEL ||
                    mvp_lmd_array_indices_use_own_storage(owner)))
                return mvp_lmd_array_store(owner, (uint32_t)index, value);
            if (fixed_prototypes && op == LMD_PROP_SET && index == -1) {
                double length = it2d(value);
                if (!(length >= 0 && length <= UINT32_MAX && length == trunc(length))) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
                Item result = mvp_lmd_array_resize(owner, (int64_t)length);
                return item_is_error(result) ? result : value;
            }
            if (fixed_prototypes && op == LMD_PROP_DELETE && index >= 0) {
                js_array_sparse_delete_index(owner, index);
                if (index < owner.array->length && index < container_dense_capacity(owner.array))
                    owner.array->items[index].item = ITEM_JS_DELETED_SENTINEL;
                return Item{.item = ITEM_TRUE};
            }
        }
    }
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    uint64_t home = 0;
    Rooted<Item> object(roots, owner), argument(roots, lambda_item_adopt_scalar_home(value, &home));
    Rooted<Item> key(roots, index == -1 ? Item{.item = s2it(heap_create_name("length", 6))} : mvp_lmd_number_to_string(index));
    if (item_is_error(key.get())) return key.get();
    if (op == LMD_PROP_PRESENT) {
        Item has = mvp_lmd_property_with_receiver(object.get(), key.get(), ItemNull, LMD_PROP_HAS, object.get());
        if (item_is_error(has)) return has;
        if (has.item == ITEM_FALSE) return Item{.item = ITEM_JS_DELETED_SENTINEL};
        operation = LMD_PROP_GET;
    }
    return mvp_lmd_property_with_receiver(object.get(), key.get(), argument.get(), operation, object.get());
}
extern "C" Item mvp_lmd_property_get(Item owner, Item name, int64_t callee) {
    TypeId tid = get_type_id(owner);
    if (tid == LMD_TYPE_ARRAY) return mvp_lmd_array_property(owner, name, ItemNull,
        callee ? LMD_PROP_CALLEE : LMD_PROP_GET, owner);
    if (tid == LMD_TYPE_STRING || tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT || tid == LMD_TYPE_BOOL ||
            tid == LMD_TYPE_DECIMAL || tid == LMD_TYPE_SYMBOL) {
        MvpLmdLibraryState* state = mvp_lmd_program_library();
        if (state && (state->primitives || tid == LMD_TYPE_SYMBOL)) {
            if (tid == LMD_TYPE_STRING) {
                int64_t index = mvp_lmd_string_key(name.get_string());
                if (index == -1) return Item{.item = i2it(utf8_to_utf16_length(owner.get_string()->chars, owner.get_string()->len))};
                if (index >= 0) return mvp_lmd_string_at(owner, index, LMD_STRING_INDEX);
            }
            int kind = tid == LMD_TYPE_STRING ? LMD_PRIMITIVE_STRING : tid == LMD_TYPE_BOOL ? LMD_PRIMITIVE_BOOLEAN :
                tid == LMD_TYPE_DECIMAL ? LMD_PRIMITIVE_BIGINT : LMD_PRIMITIVE_NUMBER;
            Item prototype = mvp_lmd_class_values(tid == LMD_TYPE_SYMBOL ? &state->symbol : &state->primitives[kind])[1];
            return mvp_lmd_property_with_receiver(prototype, name, ItemNull, LMD_PROP_GET, owner);
        }
    }
    if (tid == LMD_TYPE_ARRAY || tid == LMD_TYPE_STRING || tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT || tid == LMD_TYPE_DECIMAL) {
        int method = mvp_lmd_builtin_method(name.get_string(), tid == LMD_TYPE_INT ? LMD_TYPE_FLOAT : tid);
        if (method) return callee ? Item{.item = mvp_lmd_method_token(method)} : mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        return tid == LMD_TYPE_ARRAY ? Item{.item = ITEM_JS_UNDEFINED} : mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    }
    Map* map = mvp_lmd_property_map(owner, false, true);
    if (!map) return mvp_lmd_fail(get_type_id(owner) == LMD_TYPE_NULL ||
        get_type_id(owner) == LMD_TYPE_UNDEFINED ? LMD_MVP_TYPE : LMD_MVP_CAPABILITY, 0);
    String* key = name.get_string();
    ShapeEntry* field = own_field(map, key);
    if (field) return field->flags & JSPD_IS_ACCESSOR ? mvp_lmd_accessor_call(owner, field, ItemNull, LMD_PROP_GET) : map_shape_field_to_item(map->data, field);
    if (tid == LMD_TYPE_MAP && map->map_kind == MAP_KIND_ORDERED) {
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
    Map* map = mvp_lmd_property_map(owner, true, true);
    if (!map) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    TypeId tid = get_type_id(owner);
    if ((tid == LMD_TYPE_ARRAY || tid == LMD_TYPE_ARRAY_NUM) && !map->type) {
        // D2.6.6v3: the existing array attribute face owns and traces these packed fields.
        Input* tree = runtime_shape_tree();
        map->type = tree ? type_tree_map_root(tree) : NULL;
        if (!map->type) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    }
    ShapeEntry* field = own_field(map, name.get_string());
    if (tid == LMD_TYPE_MAP && map->map_kind == MAP_KIND_ORDERED && !field &&
            (text_is(name.get_string(), "size") || (!mvp_lmd_class_record(owner) && mvp_lmd_builtin_method(name.get_string()))))
        return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    if (field && (field->flags & (JSPD_IS_ACCESSOR | JSPD_NON_WRITABLE)))
        return field->flags & JSPD_IS_ACCESSOR ? mvp_lmd_accessor_call(owner, field, value, LMD_PROP_SET) : value;
    // descriptor admission already resolved this immutable field; reuse it for the physical write.
    return map_shape_set_resolved(map, name.get_string(), field, value, false) ? value : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}
extern "C" Item mvp_lmd_property_delete(Item owner, Item name) {
    Map* face = mvp_lmd_property_map(owner, true);
    if (!face) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    ShapeEntry* field = face->type ? own_field(face, name.get_string()) : NULL;
    if (field && (field->flags & JSPD_NON_CONFIGURABLE)) return Item{.item = ITEM_FALSE};
    if (get_type_id(owner) == LMD_TYPE_ARRAY) {
        int64_t index = mvp_lmd_string_key(name.get_string());
        if (index == -1) return Item{.item = ITEM_FALSE};
        if (index < -1) return !owner.array->type || map_shape_delete_resolved(face, field)
            ? Item{.item = ITEM_TRUE} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        if (field && !map_shape_delete_resolved(face, field)) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        js_array_sparse_delete_index(owner, index);
        if (index < owner.array->length && index < container_dense_capacity(owner.array))
            owner.array->items[index].item = ITEM_JS_DELETED_SENTINEL;
        return Item{.item = ITEM_TRUE};
    }
    // the MIR caller (including class dispatch) owns the roots throughout shared shape deletion.
    if (!face) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    // configurable admission and physical deletion use the same current-shape field.
    if (!map_shape_delete_resolved(face, field)) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    mvp_lmd_arguments_cell(owner, name.get_string(), true);
    return Item{.item = ITEM_TRUE};
}
extern "C" Item mvp_lmd_accessor_call(Item receiver, ShapeEntry* field, Item value, int64_t operation) {
    bool write = (operation & ~LMD_PROP_STRICT) == LMD_PROP_SET;
    JsAccessorCell* cell = js_shape_entry_accessor_pair(field);
    if (!cell) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    Item function = write ? cell->setter : cell->getter;
    if (function.item == ITEM_NULL || function.item == ITEM_JS_UNDEFINED)
        return write ? (operation & LMD_PROP_STRICT ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : value)
            : Item{.item = ITEM_JS_UNDEFINED};
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    uint64_t home = 0;
    Rooted<Item> object(roots, receiver), callable(roots, function);
    Rooted<Item> argument(roots, lambda_item_adopt_scalar_home(value, &home));
    Item args[] = {argument.get()};
    Item result = mvp_lmd_class_invoke(callable.get(), object.get(), args, write ? 1 : 0,
        Item{.item = ITEM_JS_UNDEFINED});
    return item_is_error(result) || !write ? result : lambda_item_uses_scalar_home(argument.get()) ? push_d(it2d(argument.get())) : argument.get();
}
extern "C" Item mvp_lmd_define_accessor(Item owner, Item name, Item function, int64_t attributes) {
    RootFrame roots(5);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner), key(roots, name), callable(roots, function);
    Rooted<Item> getter(roots, ItemNull), setter(roots, ItemNull);
    Map* map = mvp_lmd_property_map(object.get(), true);
    if (!map) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    if (!map->type) map->type = type_tree_map_root(runtime_shape_tree());
    ShapeEntry* field = own_field(map, key.get().get_string());
    if (field && (field->flags & JSPD_NON_CONFIGURABLE)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    JsAccessorCell* old = js_shape_entry_accessor_pair(field);
    if (old) { getter.set(old->getter); setter.set(old->setter); }
    if (attributes & 1) setter.set(callable.get()); else getter.set(callable.get());
    JsAccessorCell* cell = js_alloc_accessor_pair(getter.get(), setter.get());
    // installation allocates only pool metadata; no GC boundary before the cell is owned.
    return cell && js_map_store_accessor(mvp_lmd_property_map(object.get(), true), key.get().get_string(), cell,
        (uint8_t)(attributes >> 8), context->pool) ? object.get() : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}
extern "C" Item mvp_lmd_private_element(Item owner, Item key, Item input, int64_t operation) {
    if (operation == LMD_PRIVATE_NAME) {
        String* spelling = key.get_string();
        NameRef name = name_pool_create_unique_private(context->name_pool, {spelling->chars, spelling->len});
        return name ? Item{.item = s2it(name)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    }
    if (!mvp_lmd_is_object(owner)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    uint64_t home = 0;
    Rooted<Item> object(roots, owner), name(roots, key), storage(roots, owner);
    Rooted<Item> value(roots, lambda_item_adopt_scalar_home(input, &home));
    bool add = operation == LMD_PRIVATE_ADD || operation == LMD_PRIVATE_METHOD;
    if (mvp_lmd_is_proxy(object.get())) {
        storage.set(js_proxy_private_slots(object.get(), add, []() {
            return mvp_lmd_object_new(mvp_lmd_program_library()->object_shape, 0);
        }, true));
    } else if (get_type_id(object.get()) == LMD_TYPE_FUNC && !mvp_lmd_class_record(object.get())) {
        Item ready = mvp_lmd_function_prepare(object.get());
        if (item_is_error(ready)) return ready;
    }
    Map* map = mvp_lmd_property_map(storage.get(), add || operation == LMD_PRIVATE_SET);
    ShapeEntry* field = map && map->type ? own_field(map, name.get().get_string()) : NULL;
    if (operation == LMD_PRIVATE_HAS) return Item{.item = b2it(field != NULL)};
    if (add ? field != NULL : field == NULL) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (!map) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    if (!map->type) map->type = type_tree_map_root(runtime_shape_tree());
    if (field && (field->flags & JSPD_IS_ACCESSOR)) {
        JsAccessorCell* pair = js_shape_entry_accessor_pair(field);
        Item callable = operation == LMD_PRIVATE_SET ? pair->setter : pair->getter;
        // private accessors throw for either missing half, even in sloppy callers.
        if (callable.item == ITEM_NULL || callable.item == ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        return mvp_lmd_accessor_call(object.get(), field, value.get(),
            operation == LMD_PRIVATE_SET ? LMD_PROP_SET | LMD_PROP_STRICT : LMD_PROP_GET);
    }
    if (operation == LMD_PRIVATE_GET) return map_shape_field_to_item(map->data, field);
    if (field && (field->flags & JSPD_NON_WRITABLE)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (!map_shape_set_resolved(map, name.get().get_string(), field, value.get(), false)) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    if (operation == LMD_PRIVATE_METHOD) {
        field = own_field(map, name.get().get_string());
        // the descriptor is shared immutable shape metadata; use its existing transition kernel.
        JsPropertyDescriptor descriptor = {};
        descriptor.flags = JS_PD_HAS_WRITABLE;
        Item result = mvp_lmd_define_own_property(storage.get(), name.get(), &descriptor);
        if (item_is_error(result)) return result;
    }
    return lambda_item_uses_scalar_home(value.get()) ? push_d(it2d(value.get())) : value.get();
}
static Item mvp_lmd_descriptor_read(Item owner, Item key, bool presence) {
    return mvp_lmd_class_property(owner, key, ItemNull, presence ? LMD_PROP_HAS : LMD_PROP_GET, NULL);
}
static bool mvp_lmd_descriptor_callable(Item value) { return mvp_lmd_is_callable(value); }
static Item mvp_lmd_descriptor_error(const char*) { return mvp_lmd_fail(LMD_MVP_TYPE, 0); }
static const JsDescriptorReader descriptor_reader = {
    mvp_lmd_descriptor_read, mvp_lmd_descriptor_callable, mvp_lmd_descriptor_error
};
static Item mvp_lmd_array_define_length(Item owner, const JsPropertyDescriptor* descriptor) {
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner), value(roots, descriptor->value), keys(roots, ItemNull);
    double requested = owner.array->length;
    if (descriptor->flags & JS_PD_HAS_VALUE) {
        Item number = mvp_lmd_primitive_to_number(value.get());
        if (item_is_error(number)) return number;
        double first = it2d(number);
        double integer = !isfinite(first) ? 0 : fmod(trunc(first), 4294967296.0);
        if (integer < 0) integer += 4294967296.0;
        number = mvp_lmd_primitive_to_number(value.get());
        if (item_is_error(number)) return number;
        requested = it2d(number);
        if (integer != requested) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
    }
    Map* map = mvp_lmd_property_map(object.get());
    String* name = heap_create_name("length", 6);
    ShapeEntry* old = map->type ? own_field(map, name) : NULL;
    bool writable = !old || !(old->flags & JSPD_NON_WRITABLE);
    bool has_writable = (descriptor->flags & JS_PD_HAS_WRITABLE) != 0;
    bool next_writable = has_writable ? (descriptor->flags & JS_PD_WRITABLE) != 0 : writable;
    if ((descriptor->flags & (JS_PD_HAS_GET | JS_PD_HAS_SET | JS_PD_ENUMERABLE)) || js_pd_is_configurable(descriptor) ||
            (!writable && (next_writable || requested != object.get().array->length))) return Item{.item = ITEM_FALSE};
    int64_t length = (int64_t)requested, old_length = object.get().array->length, blocked = -1;
    if (length < old_length) {
        keys.set(mvp_lmd_object_project(object.get(), 5));
        if (item_is_error(keys.get())) return keys.get();
        // Ordinary ArraySetLength deletes descending and restores past the first protected index.
        for (int64_t i = keys.get().array->length; i-- > 0;) {
            Item key = keys.get().array->items[i];
            int64_t index = mvp_lmd_string_key(key.get_string());
            if (index < length) continue;
            Item deleted = mvp_lmd_property_delete(object.get(), key);
            if (item_is_error(deleted)) return deleted;
            if (deleted.item == ITEM_FALSE) { blocked = index; length = index + 1; break; }
        }
    }
    Item resized = mvp_lmd_array_resize(object.get(), length);
    if (item_is_error(resized)) return resized;
    map = mvp_lmd_property_map(object.get());
    if (old || !next_writable) {
        if (!map->type) map->type = type_tree_map_root(runtime_shape_tree());
        if (!map_shape_set(map, name, Item{.item = i2it(length)})) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        ShapeEntry* field = own_field(map, name);
        uint8_t flags = JSPD_NON_ENUMERABLE | JSPD_NON_CONFIGURABLE | (next_writable ? 0 : JSPD_NON_WRITABLE);
        TypeMap* shape = type_tree_reflag_field(runtime_shape_tree(), (TypeMap*)map->type, field, flags);
        if (shape) map->type = shape;
        else {
            if (!map_clone_typemap_for_mutation(map, context->pool)) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            own_field(map, name)->flags = flags;
        }
    }
    return Item{.item = b2it(blocked < 0)};
}
Item mvp_lmd_descriptor_from_object(Item object, JsPropertyDescriptor* descriptor, uint64_t* home) {
    return js_descriptor_from_object_with_reader(object, descriptor, &descriptor_reader, home);
}
extern "C" Item mvp_lmd_define_property(Item owner, Item name, Item descriptor) {
    if (!mvp_lmd_is_object(owner)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner), key(roots, name), input(roots, descriptor);
    JsPropertyDescriptor converted = {}; uint64_t home = 0;
    Item status = mvp_lmd_descriptor_from_object(input.get(), &converted, &home);
    if (!item_is_error(status)) status = mvp_lmd_define_own_property(object.get(), key.get(), &converted);
    return item_is_error(status) ? status : status.item == ITEM_FALSE ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : object.get();
}
Item mvp_lmd_define_own_property(Item owner, Item name, const JsPropertyDescriptor* descriptor) {
    if (mvp_lmd_foreign_candidate(owner)) {
        Item foreign;
        JsPropertyDescriptor converted = *descriptor;
        if (mvp_lmd_foreign_property(owner, name, ItemNull, owner,
                LMD_FOREIGN_DEFINE, &foreign, &converted)) return foreign;
    }
    if (get_type_id(owner) == LMD_TYPE_VMAP && js_module_namespace_record(owner))
        return js_module_namespace_define(owner, name, descriptor);
    double numeric; int index;
    if (mvp_lmd_typed_array_index(owner, name, &numeric)) {
        if (!js_ta_numeric_index_valid(owner, numeric, numeric == 0 && signbit(numeric), &index) ||
                js_pd_is_accessor(descriptor) ||
                ((descriptor->flags & JS_PD_HAS_CONFIGURABLE) && !js_pd_is_configurable(descriptor)) ||
                ((descriptor->flags & JS_PD_HAS_ENUMERABLE) && !(descriptor->flags & JS_PD_ENUMERABLE)) ||
                ((descriptor->flags & JS_PD_HAS_WRITABLE) && !(descriptor->flags & JS_PD_WRITABLE)))
            return Item{.item = ITEM_FALSE};
        Item stored = descriptor->flags & JS_PD_HAS_VALUE ? mvp_lmd_typed_array_store(owner, numeric, descriptor->value) : ItemNull;
        return item_is_error(stored) ? stored : Item{.item = ITEM_TRUE};
    }
    uint64_t home = 0;
    JsPropertyDescriptor converted = *descriptor;
    converted.value = lambda_item_adopt_scalar_home(descriptor->value, &home);
    Item alias = mvp_lmd_arguments_cell(owner, name.get_string());
    if (get_type_id(alias) == LMD_TYPE_ARRAY && !js_pd_is_accessor(&converted) &&
            (converted.flags & JS_PD_HAS_WRITABLE) && !(converted.flags & JS_PD_WRITABLE) &&
            !(converted.flags & JS_PD_HAS_VALUE)) {
        // disconnecting a mapped index preserves its current parameter value.
        converted.flags |= JS_PD_HAS_VALUE;
        converted.value = lambda_item_adopt_scalar_home(alias.array->items[0], &home);
    }
    int64_t array_index = get_type_id(owner) == LMD_TYPE_ARRAY ? mvp_lmd_string_key(name.get_string()) : -2;
    Map* map = NULL;
    if (array_index >= 0) {
        // ordinary indexed admission cannot collect; the storage helper roots its actual operands.
        map = mvp_lmd_property_map(owner, true);
        if (!map) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        ShapeEntry* policy = map->type ? typemap_hash_lookup((TypeMap*)map->type, "length", 6) : NULL;
        if (array_index >= owner.array->length && policy && (policy->flags & JSPD_NON_WRITABLE))
            return Item{.item = ITEM_FALSE};
        bool physical = mvp_lmd_array_own_item(owner.array, array_index).item != ITEM_JS_DELETED_SENTINEL;
        ShapeEntry* field = map->type ? own_field(map, name.get_string()) : NULL;
        bool defaults = !js_pd_is_accessor(&converted) &&
            (!(converted.flags & JS_PD_HAS_WRITABLE) || (converted.flags & JS_PD_WRITABLE)) &&
            (!(converted.flags & JS_PD_HAS_ENUMERABLE) || (converted.flags & JS_PD_ENUMERABLE)) &&
            (!(converted.flags & JS_PD_HAS_CONFIGURABLE) || js_pd_is_configurable(&converted));
        uint8_t attributes = JS_PD_HAS_WRITABLE | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
        if (!field && defaults && (physical || (converted.flags & attributes) == attributes)) {
            if (!physical) {
                Item allowed = mvp_lmd_object_extensible(owner, false);
                if (item_is_error(allowed) || allowed.item == ITEM_FALSE) return allowed;
            }
            if (physical && !(converted.flags & JS_PD_HAS_VALUE)) return Item{.item = ITEM_TRUE};
            Item status = mvp_lmd_array_store(owner, array_index,
                converted.flags & JS_PD_HAS_VALUE ? converted.value : Item{.item = ITEM_JS_UNDEFINED});
            return item_is_error(status) ? status : Item{.item = ITEM_TRUE};
        }
    }
    RootFrame roots(11);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner), key(roots, name), value_root(roots, converted.value);
    Rooted<Item> getter(roots, converted.getter), setter(roots, converted.setter);
    if (mvp_lmd_is_proxy(owner))
        return mvp_lmd_proxy_property(object.get(), key.get(), ItemNull, object.get(), LMD_PROXY_DEFINE, &converted);
    if (get_type_id(owner) == LMD_TYPE_FUNC) {
        Item ready = mvp_lmd_function_prepare(owner);
        if (item_is_error(ready)) return ready;
    }
    if (!map) map = mvp_lmd_property_map(object.get(), true);
    if (!map) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    uint64_t* values[6]; bool present[6] = {};
    for (int i = 0; i < 6; i++) { values[i] = roots.take_slot(); *values[i] = ITEM_JS_UNDEFINED; }
    if (array_index == -1) return mvp_lmd_array_define_length(object.get(), &converted);
    if (array_index >= 0) {
        Item stored = mvp_lmd_array_own_item(object.get().array, array_index);
        if (stored.item != ITEM_JS_DELETED_SENTINEL && (!map->type || !own_field(map, key.get().get_string()))) {
            if (!map->type) map->type = type_tree_map_root(runtime_shape_tree());
            if (!map_shape_set(map, key.get().get_string(), stored)) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        }
    }
    const uint8_t fields[] = {JS_PD_HAS_ENUMERABLE, JS_PD_HAS_CONFIGURABLE, JS_PD_HAS_VALUE,
        JS_PD_HAS_WRITABLE, JS_PD_HAS_GET, JS_PD_HAS_SET};
    for (int i = 0; i < 6; i++) present[i] = (converted.flags & fields[i]) != 0;
    *values[0] = b2it((converted.flags & JS_PD_ENUMERABLE) != 0);
    *values[1] = b2it(js_pd_is_configurable(&converted));
    *values[2] = present[2] ? converted.value.item : ITEM_JS_UNDEFINED;
    *values[3] = b2it((converted.flags & JS_PD_WRITABLE) != 0);
    *values[4] = converted.getter.item; *values[5] = converted.setter.item;
    bool accessor = present[4] || present[5];
    if (accessor && (present[2] || present[3])) return Item{.item = ITEM_FALSE};
    map = mvp_lmd_property_map(object.get());
    // fresh dense arrays have no attribute shape for the shared accessor installer to clone.
    if (!map->type) map->type = type_tree_map_root(runtime_shape_tree());
    ShapeEntry* old = map->type ? own_field(map, key.get().get_string()) : NULL;
    JsPropertyDescriptor current = {};
    Item current_status = {.item = ITEM_FALSE};
    if (old) {
        TypeMap* previous_type = (TypeMap*)map->type;
        current_status = mvp_lmd_get_own_property(object.get(), key.get(), &current);
        if (item_is_error(current_status)) return current_status;
        Map* live = mvp_lmd_property_map(object.get());
        // lazy descriptor publication can relocate storage and change field offsets.
        if (live != map || live->type != previous_type) {
            map = live;
            old = map && map->type ? own_field(map, key.get().get_string()) : NULL;
        }
        converted.value = Item{.item = *values[2]};
        converted.getter = getter.get(); converted.setter = setter.get();
        if (!mvp_lmd_descriptor_compatible(true, &converted,
                current_status.item == ITEM_TRUE ? &current : NULL)) return Item{.item = ITEM_FALSE};
    }
    if (!old) {
        Item allowed = mvp_lmd_object_extensible(owner, false);
        if (item_is_error(allowed)) return allowed;
        if (allowed.item == ITEM_FALSE) return Item{.item = ITEM_FALSE};
    }
    uint8_t flags = old ? old->flags : JSPD_NON_ENUMERABLE | JSPD_NON_CONFIGURABLE | JSPD_NON_WRITABLE;
    bool old_accessor = (flags & JSPD_IS_ACCESSOR) != 0;
    bool conversion = old && (accessor ? !old_accessor : (present[2] || present[3]) && old_accessor);
    if (conversion) flags = (flags & (JSPD_NON_ENUMERABLE | JSPD_NON_CONFIGURABLE)) |
        (accessor ? JSPD_IS_ACCESSOR : JSPD_NON_WRITABLE);
    const int indices[] = {0, 1, 3};
    const uint8_t bits[] = {JSPD_NON_ENUMERABLE, JSPD_NON_CONFIGURABLE, JSPD_NON_WRITABLE};
    for (int i = 0; i < 3; i++) if (present[indices[i]]) {
        if (mvp_lmd_truthy(Item{.item = *values[indices[i]]})) flags &= ~bits[i]; else flags |= bits[i];
    }
    Item value = {.item = *values[2]};
    auto finish = [&]() -> Item {
        Item cell = mvp_lmd_arguments_cell(object.get(), key.get().get_string());
        if (get_type_id(cell) == LMD_TYPE_ARRAY) {
            if (!accessor && present[2]) {
                Item stored = mvp_lmd_array_store(cell, 0, value);
                if (item_is_error(stored)) return stored;
            }
            if (accessor || (present[3] && !(converted.flags & JS_PD_WRITABLE)))
                mvp_lmd_arguments_cell(object.get(), key.get().get_string(), true);
        }
        if (array_index >= 0) {
            js_array_sparse_delete_index(object.get(), array_index);
            if (array_index < container_dense_capacity(object.get().array))
                object.get().array->items[array_index].item = ITEM_JS_DELETED_SENTINEL;
            if (array_index >= object.get().array->length) {
                Item resized = mvp_lmd_array_resize(object.get(), array_index + 1);
                if (item_is_error(resized)) return resized;
            }
        }
        return Item{.item = ITEM_TRUE};
    };
    if (accessor || (old_accessor && !conversion)) {
        JsAccessorCell* previous = old_accessor ? js_shape_entry_accessor_pair(old) : NULL;
        Item getter = present[4] ? Item{.item = *values[4]} : previous ? previous->getter : ItemNull;
        Item setter = present[5] ? Item{.item = *values[5]} : previous ? previous->setter : ItemNull;
        if (getter.item == ITEM_JS_UNDEFINED) getter = ItemNull;
        if (setter.item == ITEM_JS_UNDEFINED) setter = ItemNull;
        JsAccessorCell* cell = js_alloc_accessor_pair(getter, setter);
        if (!cell || !js_map_store_accessor(mvp_lmd_property_map(object.get()), key.get().get_string(), cell, flags, context->pool))
            return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        return finish();
    }
    if (present[2] || !old || conversion) {
        // A fresh Array acquires its attribute face through the shared property writer.
        bool stored = map->type ? map_shape_set_resolved(map, key.get().get_string(), old, value, true) :
            !item_is_error(mvp_lmd_property_set(object.get(), key.get(), value));
        if (!stored) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        map = mvp_lmd_property_map(object.get()); old = own_field(map, key.get().get_string());
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
    return finish();
}
Item mvp_lmd_descriptor_object(const JsPropertyDescriptor* descriptor, bool null_prototype) {
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    uint64_t home = 0;
    Rooted<Item> value(roots, lambda_item_adopt_scalar_home(descriptor->value, &home));
    Rooted<Item> getter(roots, descriptor->getter), setter(roots, descriptor->setter), result(roots, ItemNull);
    MvpLmdLibraryState* library = mvp_lmd_program_library();
    if (!library) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    result.set(mvp_lmd_object_new(library->object_shape, 0));
    if (item_is_error(result.get())) return result.get();
    if (null_prototype) {
        Item linked = mvp_lmd_set_prototype(result.get(), ItemNull);
        if (item_is_error(linked)) return linked;
    }
    const char* names[] = {"value", "writable", "get", "set", "enumerable", "configurable"};
    const uint8_t bits[] = {JS_PD_HAS_VALUE, JS_PD_HAS_WRITABLE, JS_PD_HAS_GET, JS_PD_HAS_SET, JS_PD_HAS_ENUMERABLE, JS_PD_HAS_CONFIGURABLE};
    for (int i = 0; i < 6; i++) {
        if (!(descriptor->flags & bits[i])) continue;
        Item field = i == 0 ? value.get() : i == 2 ? getter.get() : i == 3 ? setter.get() :
            Item{.item = b2it(i == 1 ? (descriptor->flags & JS_PD_WRITABLE) != 0 :
                i == 4 ? (descriptor->flags & JS_PD_ENUMERABLE) != 0 : js_pd_is_configurable(descriptor))};
        Item stored = mvp_lmd_named_set(result.get(), names[i], field);
        if (item_is_error(stored)) return stored;
    }
    return result.get();
}
extern "C" Item mvp_lmd_define_properties(Item owner, Item descriptors) {
    RootFrame roots(6);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner), input(roots, descriptors), keys(roots, ItemNull);
    Rooted<Item> pending(roots, ItemNull), descriptor(roots, ItemNull), key(roots, ItemNull);
    // lazy callable metadata is independent of whether the target is an object.
    if (!mvp_lmd_is_object(object.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    input.set(mvp_lmd_box_primitive(input.get()));
    if (item_is_error(input.get())) return input.get();
    if (get_type_id(input.get()) == LMD_TYPE_FUNC) {
        Item status = mvp_lmd_function_prepare(input.get());
        if (item_is_error(status)) return status;
    }
    keys.set(mvp_lmd_object_project(input.get(), 5));
    if (item_is_error(keys.get())) return keys.get();
    pending.set(mvp_lmd_array_new(0));
    if (item_is_error(pending.get())) return pending.get();
    Item enumerable = {.item = s2it(heap_create_name("enumerable", 10))};
    for (int64_t i = 0; i < keys.get().array->length; i++) {
        key.set(keys.get().array->items[i]);
        descriptor.set(mvp_lmd_get_own_descriptor(input.get(), key.get()));
        if (item_is_error(descriptor.get())) return descriptor.get();
        if (descriptor.get().item == ITEM_JS_UNDEFINED) continue;
        Item flag = mvp_lmd_class_property(descriptor.get(), enumerable, ItemNull, LMD_PROP_GET, NULL);
        if (item_is_error(flag)) return flag;
        if (flag.item != ITEM_TRUE) continue;
        descriptor.set(mvp_lmd_class_property(input.get(), key.get(), ItemNull, LMD_PROP_GET, NULL));
        if (item_is_error(descriptor.get())) return descriptor.get();
        JsPropertyDescriptor converted = {}; uint64_t home = 0;
        Item status = js_descriptor_from_object_with_reader(descriptor.get(), &converted, &descriptor_reader, &home);
        if (item_is_error(status)) return status;
        descriptor.set(mvp_lmd_descriptor_object(&converted, true));
        if (item_is_error(descriptor.get())) return descriptor.get();
        status = mvp_lmd_array_store(pending.get(), pending.get().array->length, key.get());
        if (item_is_error(status)) return status;
        status = mvp_lmd_array_store(pending.get(), pending.get().array->length, descriptor.get());
        if (item_is_error(status)) return status;
    }
    // ES DefineProperties converts every descriptor before the first target mutation.
    for (int64_t i = 0; i < pending.get().array->length; i += 2) {
        Item status = mvp_lmd_define_property(object.get(), pending.get().array->items[i], pending.get().array->items[i + 1]);
        if (item_is_error(status)) return status;
    }
    return object.get();
}
Item mvp_lmd_get_own_property(Item owner, Item name, JsPropertyDescriptor* descriptor) {
    Item foreign;
    if (mvp_lmd_foreign_candidate(owner, true) && mvp_lmd_foreign_property(owner, name,
            ItemNull, owner, LMD_FOREIGN_DESCRIPTOR, &foreign, descriptor)) return foreign;
    if (get_type_id(owner) == LMD_TYPE_VMAP && js_module_namespace_record(owner))
        return js_module_namespace_get_own(owner, name, descriptor);
    double numeric; int index;
    if (mvp_lmd_typed_array_index(owner, name, &numeric)) {
        if (!js_ta_numeric_index_valid(owner, numeric, numeric == 0 && signbit(numeric), &index)) return Item{.item = ITEM_FALSE};
        *descriptor = {};
        descriptor->flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_WRITABLE |
            JS_PD_HAS_ENUMERABLE | JS_PD_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
        js_pd_set_configurable(descriptor, true);
        descriptor->value = js_typed_array_get(owner, Item{.item = i2it(index)});
        return item_is_error(descriptor->value) ? descriptor->value : Item{.item = ITEM_TRUE};
    }
    if (mvp_lmd_is_proxy(owner)) return mvp_lmd_proxy_property(owner, name, ItemNull, owner, LMD_PROXY_DESCRIPTOR, descriptor);
    if (get_type_id(owner) == LMD_TYPE_FUNC && !mvp_lmd_class_record(owner)) {
        RootFrame roots(2);
        if (!roots.valid()) return ItemError;
        Rooted<Item> object(roots, owner), key(roots, name);
        Item ready = mvp_lmd_function_prepare(object.get());
        if (item_is_error(ready)) return ready;
        owner = object.get(); name = key.get();
    }
    // descriptor inspection invokes no guest code; only lazy function preparation can collect.
    Item value = ItemNull, setter = ItemNull;
    Map* map = mvp_lmd_property_map(owner);
    if (!map && get_type_id(owner) == LMD_TYPE_VMAP && js_promise_is(owner)) return Item{.item = ITEM_FALSE};
    if (!map) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    ShapeEntry* field = map->type ? own_field(map, name.get_string()) : NULL;
    uint8_t flags = field ? field->flags : 0;
    if (field) {
        JsAccessorCell* cell = js_shape_entry_accessor_pair(field);
        if (cell) {
            value = cell->getter.item == ITEM_NULL ? Item{.item = ITEM_JS_UNDEFINED} : cell->getter;
            setter = cell->setter.item == ITEM_NULL ? Item{.item = ITEM_JS_UNDEFINED} : cell->setter;
        } else value = get_type_id(owner) == LMD_TYPE_ARRAY && mvp_lmd_string_key(name.get_string()) == -1 ?
            Item{.item = i2it(owner.array->length)} : map_shape_field_to_item(map->data, field);
    } else if (get_type_id(owner) == LMD_TYPE_ARRAY) {
        int64_t index = mvp_lmd_string_key(name.get_string());
        if (index == -1) {
            value = Item{.item = i2it(owner.array->length)};
            flags = JSPD_NON_ENUMERABLE | JSPD_NON_CONFIGURABLE;
        } else if (index >= 0 && index < owner.array->length &&
                mvp_lmd_array_own_item(owner.array, index).item != ITEM_JS_DELETED_SENTINEL)
            value = mvp_lmd_array_own_item(owner.array, index);
        else return Item{.item = ITEM_FALSE};
    } else return Item{.item = ITEM_FALSE};
    if (value.item == ITEM_JS_LAZY_GLOBAL_SENTINEL) {
        RootFrame roots(2);
        if (!roots.valid()) return ItemError;
        Rooted<Item> object(roots, owner), key(roots, name);
        Item resolved;
        if (!js_resolve_lazy_global(object.get(), key.get(), &resolved)) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        return item_is_error(resolved) ? resolved : mvp_lmd_get_own_property(object.get(), key.get(), descriptor);
    }
    JsPropertyDescriptor converted = {};
    converted.flags = JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE |
        (flags & JSPD_IS_ACCESSOR ? JS_PD_HAS_GET | JS_PD_HAS_SET : JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE);
    if (!(flags & JSPD_NON_ENUMERABLE)) converted.flags |= JS_PD_ENUMERABLE;
    if (!(flags & JSPD_NON_WRITABLE)) converted.flags |= JS_PD_WRITABLE;
    js_pd_set_configurable(&converted, !(flags & JSPD_NON_CONFIGURABLE));
    if (flags & JSPD_IS_ACCESSOR) { converted.getter = value; converted.setter = setter; }
    else {
        Item cell = mvp_lmd_arguments_cell(owner, name.get_string());
        converted.value = get_type_id(cell) == LMD_TYPE_ARRAY ? cell.array->items[0] : value;
    }
    *descriptor = converted;
    return Item{.item = ITEM_TRUE};
}
Item mvp_lmd_set_completion(Item owner, Item name, Item value, Item receiver, MvpLmdPropertyCache* cache) {
    if (!mvp_lmd_is_object(owner)) {
        RootFrame roots(4);
        if (!roots.valid()) return ItemError;
        uint64_t home = 0;
        Rooted<Item> object(roots, owner), key(roots, name),
            argument(roots, lambda_item_adopt_scalar_home(value, &home)), target(roots, receiver);
        // ToObject supplies descriptors; setters still receive the primitive from the reference.
        object.set(mvp_lmd_box_primitive(object.get()));
        return item_is_error(object.get()) ? object.get() :
            mvp_lmd_set_completion(object.get(), key.get(), argument.get(), target.get(), cache);
    }
    Item foreign;
    if (mvp_lmd_foreign_candidate(owner) && mvp_lmd_foreign_property(owner, name,
            value, receiver, LMD_PROP_SET, &foreign)) return foreign;
    if (get_type_id(owner) == LMD_TYPE_VMAP && js_module_namespace_record(owner))
        return Item{.item = ITEM_FALSE};
    if (lambda_stack_is_exhausted(context->stack_limit)) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
    if (mvp_lmd_is_proxy(owner)) return mvp_lmd_proxy_property(owner, name, value, receiver, LMD_PROXY_SET);
    if (owner.item == receiver.item && get_type_id(owner) == LMD_TYPE_MAP && owner.map->map_kind == MAP_KIND_PLAIN) {
        Map* map = mvp_lmd_property_map(owner, true);
        String* key = name.get_string();
        TypeMap* shape = map ? (TypeMap*)map->type : NULL;
        MvpLmdPropertyCacheEntry* cached = cache && shape && typemap_is_shared_shape(shape)
            ? cache->find_shape(shape) : NULL;
        ShapeEntry* field = cached && cached->field ? cached->field : shape ? own_field(map, key) : NULL;
        if (field && field->byte_offset >= 0 && !(field->flags & JSPD_IS_ACCESSOR)) {
            if (field->flags & JSPD_NON_WRITABLE) return Item{.item = ITEM_FALSE};
            // named own slots cannot alias arguments; indexed candidates still use the canonical key parser.
            if (key->len && key->chars[0] >= '0' && key->chars[0] <= '9' &&
                    get_type_id(mvp_lmd_arguments_cell(owner, key)) == LMD_TYPE_ARRAY) {
                JsPropertyDescriptor update = {};
                update.flags = JS_PD_HAS_VALUE; update.value = value;
                return mvp_lmd_define_own_property(owner, name, &update);
            }
            if (cache && typemap_is_shared_shape((TypeMap*)map->type)) {
                MvpLmdClass* cls = mvp_lmd_class_record(owner);
                if (cls && !cls->owner && map != mvp_lmd_class_values(cls)[1].map &&
                        map != mvp_lmd_class_values(cls)[2].map)
                    mvp_lmd_cache_property(cache, (TypeMap*)map->type, field, ItemNull, true);
            }
            // an existing own data slot needs no prototype/extensibility walk or descriptor construction.
            return map_shape_set_resolved(map, name.get_string(), field, value, true)
                ? Item{.item = ITEM_TRUE} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        }
    }
    uint64_t home = 0;
    value = lambda_item_adopt_scalar_home(value, &home);
    JsPropertyDescriptor descriptor = {};
    for (;;) {
        // an exotic prototype retains its own Set algorithm for a different receiver.
        if (get_type_id(owner) == LMD_TYPE_VMAP && js_module_namespace_record(owner)) return Item{.item = ITEM_FALSE};
        // ordinary descriptor/link inspection cannot collect; exotic adapters root before reentry.
        if (mvp_lmd_is_proxy(owner))
            return mvp_lmd_proxy_property(owner, name, value, receiver, LMD_PROXY_SET);
        if (get_type_id(owner) == LMD_TYPE_FUNC && !mvp_lmd_class_record(owner)) {
            RootFrame roots(4);
            if (!roots.valid()) return ItemError;
            Rooted<Item> object(roots, owner), key(roots, name), argument(roots, value), target(roots, receiver);
            Item ready = mvp_lmd_function_prepare(object.get());
            return item_is_error(ready) ? ready :
                mvp_lmd_set_completion(object.get(), key.get(), argument.get(), target.get());
        }
        double numeric; int index;
        if (mvp_lmd_typed_array_index(owner, name, &numeric)) {
            if (owner.item == receiver.item) {
                Item stored = mvp_lmd_typed_array_store(owner, numeric, value);
                return item_is_error(stored) ? stored : Item{.item = ITEM_TRUE};
            }
            // an invalid numeric key terminates the exotic prototype's Set without receiver creation.
            if (!js_ta_numeric_index_valid(owner, numeric, numeric == 0 && signbit(numeric), &index)) return Item{.item = ITEM_TRUE};
        }
        Item found = mvp_lmd_get_own_property(owner, name, &descriptor);
        if (item_is_error(found)) return found;
        if (found.item == ITEM_TRUE) break;
        owner = mvp_lmd_get_prototype(owner);
        if (item_is_error(owner)) return owner;
        if (owner.item == ITEM_NULL) {
            descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_WRITABLE;
            break;
        }
    }
    if (js_pd_is_accessor(&descriptor)) {
        if (descriptor.setter.item == ITEM_JS_UNDEFINED) return Item{.item = ITEM_FALSE};
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        Rooted<Item> argument(roots, value);
        Item args[] = {argument.get()};
        Item status = mvp_lmd_class_invoke(descriptor.setter, receiver, args, 1, Item{.item = ITEM_JS_UNDEFINED});
        return item_is_error(status) ? status : Item{.item = ITEM_TRUE};
    }
    if (!(descriptor.flags & JS_PD_WRITABLE)) return Item{.item = ITEM_FALSE};
    TypeId type = get_type_id(receiver);
    if (!mvp_lmd_is_object(receiver)) return Item{.item = ITEM_FALSE};
    auto write_receiver = [&]() -> Item {
        JsPropertyDescriptor current = {};
        Item found = mvp_lmd_get_own_property(receiver, name, &current);
        if (item_is_error(found)) return found;
        if (found.item == ITEM_TRUE && (js_pd_is_accessor(&current) || !(current.flags & JS_PD_WRITABLE)))
            return Item{.item = ITEM_FALSE};
        // receiver DefineOwnProperty decides creation policy and array length effects.
        current = {};
        current.flags = JS_PD_HAS_VALUE;
        current.value = value;
        if (found.item == ITEM_FALSE) {
            current.flags |= JS_PD_HAS_WRITABLE | JS_PD_WRITABLE | JS_PD_HAS_ENUMERABLE |
                JS_PD_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
            js_pd_set_configurable(&current, true);
        }
        return mvp_lmd_define_own_property(receiver, name, &current);
    };
    if (mvp_lmd_is_proxy(receiver) || (type == LMD_TYPE_FUNC && !mvp_lmd_class_record(receiver))) {
        // receiver descriptor inspection may trap or prepare a function before the final definition.
        RootFrame roots(3);
        if (!roots.valid()) return ItemError;
        Rooted<Item> target(roots, receiver), key(roots, name), argument(roots, value);
        return write_receiver();
    }
    return write_receiver();
}
extern "C" Item mvp_lmd_get_own_descriptor(Item owner, Item name) {
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner), key(roots, name);
    JsPropertyDescriptor descriptor = {};
    Item status = mvp_lmd_get_own_property(object.get(), key.get(), &descriptor);
    if (item_is_error(status)) return status;
    return status.item == ITEM_FALSE ? Item{.item = ITEM_JS_UNDEFINED} : mvp_lmd_descriptor_object(&descriptor);
}

extern "C" Item mvp_lmd_object_integrity(Item owner, int64_t operation) {
    // 0/1 seal/freeze; 2/3 query sealed/frozen. Extensibility has its own operation.
    bool query = operation >= 2, freeze = operation & 1;
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner);
    Item status = mvp_lmd_object_extensible(object.get(), !query);
    if (item_is_error(status)) return status;
    if (!query && status.item == ITEM_FALSE) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (query && status.item == ITEM_TRUE) return Item{.item = ITEM_FALSE};
    MvpLmdLibraryState* state = mvp_lmd_program_library();
    bool arguments = state && state->arguments_key && get_type_id(owner) == LMD_TYPE_MAP &&
        typemap_hash_lookup_key((TypeMap*)owner.map->type, state->arguments_key);
    if (get_type_id(owner) == LMD_TYPE_ARRAY || mvp_lmd_is_proxy(owner) || arguments || js_is_typed_array(owner) || mvp_lmd_foreign_candidate(owner) ||
            (get_type_id(owner) == LMD_TYPE_VMAP && js_module_namespace_record(owner))) {
        Rooted<Item> keys(roots, mvp_lmd_object_project(object.get(), 5));
        Rooted<Item> descriptor(roots, ItemNull), attributes(roots, ItemNull);
        if (item_is_error(keys.get())) return keys.get();
        for (int64_t i = 0; i < keys.get().array->length; i++) {
            Item key = keys.get().array->items[i];
            descriptor.set(mvp_lmd_get_own_descriptor(object.get(), key));
            if (item_is_error(descriptor.get())) return descriptor.get();
            if (descriptor.get().item == ITEM_JS_UNDEFINED) continue;
            Map* face = mvp_lmd_property_map(descriptor.get());
            ShapeEntry* writable = typemap_hash_lookup((TypeMap*)face->type, "writable", 8);
            ShapeEntry* configurable = typemap_hash_lookup((TypeMap*)face->type, "configurable", 12);
            if (query) {
                if (map_shape_field_to_item(face->data, configurable).item == ITEM_TRUE ||
                        (freeze && writable && map_shape_field_to_item(face->data, writable).item == ITEM_TRUE))
                    return Item{.item = ITEM_FALSE};
                continue;
            }
            JsPropertyDescriptor flags = {};
            flags.flags = freeze && writable ? JS_PD_HAS_WRITABLE : 0;
            flags.flags |= JS_PD_HAS_CONFIGURABLE;
            attributes.set(mvp_lmd_descriptor_object(&flags, true));
            if (item_is_error(attributes.get())) return attributes.get();
            Item status = mvp_lmd_define_property(object.get(), key, attributes.get());
            if (item_is_error(status)) return status;
        }
        return query ? Item{.item = ITEM_TRUE} : object.get();
    }
    Map* map = mvp_lmd_property_map(object.get());
    if (!map && get_type_id(object.get()) == LMD_TYPE_VMAP && js_promise_is(object.get()))
        return query ? Item{.item = ITEM_TRUE} : object.get();
    TypeMap* original = (TypeMap*)map->type;
    FOR_EACH_MAP_FIELD(original, field) {
        if (field->key_kind == NAME_KEY_PRIVATE) continue;
        uint8_t required = JSPD_NON_CONFIGURABLE |
            (freeze && !(field->flags & JSPD_IS_ACCESSOR) ? JSPD_NON_WRITABLE : 0);
        if ((field->flags & required) == required) continue;
        if (query) return Item{.item = ITEM_FALSE};
        String* key = field->key_kind == NAME_KEY_STRING ? heap_create_name(field->name->str, field->name->length) :
            name_pool_resolve_id(context->name_pool, field->name_id);
        if (!key) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        ShapeEntry* current = own_field(map, key);
        TypeMap* shape = type_tree_reflag_field(runtime_shape_tree(), (TypeMap*)map->type, current, current->flags | required);
        if (shape) map->type = shape;
        else {
            if (!map_clone_typemap_for_mutation(map, context->pool)) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            own_field(map, key)->flags |= required;
        }
    }
    return query ? Item{.item = ITEM_TRUE} : object.get();
}
extern "C" Item mvp_lmd_property_has(Item owner, Item name, int64_t inherited) {
    if (get_type_id(owner) == LMD_TYPE_ARRAY) return mvp_lmd_array_property(owner, name, ItemNull,
        inherited ? LMD_PROP_HAS : LMD_PROP_OWN, owner);
    Map* map = mvp_lmd_property_map(owner);
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
        if (mvp_lmd_same_value_zero(map->entries->items[slot], key)) return slot;
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
    if (!mvp_lmd_is_ordered_map(owner)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    OrderedMap* map = (OrderedMap*)owner.map;
    if (method == 5) {
        // keep stable positions for active cursors, while releasing every strong entry edge.
        for (int64_t i = 0; i < map->entries->length; i++) map->entries->items[i] = ItemNull;
        if (!map->cursors) { map->entries->length = 0; map->entries->extra = 0; }
        hashmap_clear(map->index, false); map->size = 0;
        return Item{.item = ITEM_JS_UNDEFINED};
    }
    if (method < 1 || method > 4) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    // canonical keys, immediate updates and read/delete hits need no GC-capable setup.
    if (get_type_id(key) == LMD_TYPE_STRING && !key.get_string()->is_ascii &&
            !utf8_key_is_canonical(key.get_string()->chars, key.get_string()->len))
        return map_call_owned(owner, method_item, key, value, -1, 0, -1);
    uint64_t hash = 0;
    int64_t previous = -1, head = -1, slot = map->last_entry - 1;
    // ordinals retain no GC edges; validate the live key after deletion, clear or compaction.
    // deletion still walks the bucket because it needs the predecessor link.
    if (method == 4 || slot < 0 || slot >= map->entries->length ||
            map->entries->items[slot + 3].item != ITEM_TRUE || !mvp_lmd_same_value_zero(map->entries->items[slot], key)) {
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
        ? ItemError : mvp_lmd_array_adopt_realm(Item{.array = pair.get()});
}
struct ProjectionField { ShapeEntry* field; uint32_t index; int64_t order; NameRef key; };
static int projection_order(const void* a, const void* b) {
    const ProjectionField* x = (const ProjectionField*)a; const ProjectionField* y = (const ProjectionField*)b;
    if (x->index == y->index) return x->order < y->order ? -1 : x->order != y->order;
    return x->index < y->index ? -1 : 1;
}
static bool for_in_own_keys_suffice(Item owner) {
    MvpLmdLibraryState* library = mvp_lmd_program_library();
    for (bool ancestor = false;; ancestor = true) {
        TypeId kind = get_type_id(owner);
        // this proof cannot prepare a function or invoke a proxy while inspecting links.
        if ((kind != LMD_TYPE_MAP && kind != LMD_TYPE_ARRAY) || mvp_lmd_is_proxy(owner) ||
                mvp_lmd_foreign_candidate(owner)) return false;
        Map* map = mvp_lmd_property_map(owner);
        if (!map) return false;
        if (ancestor) {
            if (kind == LMD_TYPE_ARRAY && owner.array->length) return false;
            if (map->type) FOR_EACH_MAP_FIELD((TypeMap*)map->type, field) {
                if (field->key_kind == NAME_KEY_STRING && !(field->flags & JSPD_NON_ENUMERABLE)) return false;
            }
        }
        if (!library && !mvp_lmd_class_record(owner)) return true;
        // ordinary map/array prototype lookup neither allocates nor executes guest code.
        owner = mvp_lmd_get_prototype_with_library(owner, library);
        if (owner.item == ITEM_NULL) return true;
        if (item_is_error(owner)) return false;
    }
}
extern "C" Item mvp_lmd_for_in_keys(Item owner) {
    if (for_in_own_keys_suffice(owner)) return mvp_lmd_object_project(owner, 0);
    if (owner.item != ITEM_NULL && owner.item != ITEM_JS_UNDEFINED && !mvp_lmd_is_object(owner)) {
        MvpLmdLibraryState* library = mvp_lmd_program_library();
        if (library && library->mutable_prototypes) {
            // open primitive prototypes contribute inherited keys through the existing object walker.
            RootFrame roots(1);
            if (!roots.valid()) return ItemError;
            Rooted<Item> object(roots, mvp_lmd_box_primitive(owner));
            return item_is_error(object.get()) ? object.get() : mvp_lmd_for_in_keys(object.get());
        }
    }
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
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_ARRAY && type != LMD_TYPE_FUNC &&
            !(type == LMD_TYPE_VMAP && js_promise_is(owner)) &&
            !(is_virtual_container_type_id(type) && js_host_object_type(owner))) return result.get();
    MvpLmdClass* cls = mvp_lmd_class_record(owner);
    bool statics = type == LMD_TYPE_FUNC;
    if (statics && !mvp_lmd_foreign_candidate(owner)) {
        if (!cls) {
            Item ready = mvp_lmd_function_prepare(object.get());
            if (item_is_error(ready)) return ready;
            cls = mvp_lmd_class_record(object.get());
        }
        if (!cls) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        object.set(mvp_lmd_class_values(cls)[2]);
    }
    for (;;) {
        keys.set(mvp_lmd_object_project(object.get(), 3));
        if (item_is_error(keys.get())) return keys.get();
        for (int64_t i = 0; i < keys.get().array->length; i++) {
            key.set(keys.get().array->items[i]); bool visited = false;
            for (int64_t j = 0; j < seen.get().array->length; j++)
                if (mvp_lmd_same_value_zero(key.get(), seen.get().array->items[j])) { visited = true; break; }
            if (visited) continue;
            JsPropertyDescriptor descriptor = {};
            Item found = mvp_lmd_get_own_property(object.get(), key.get(), &descriptor);
            if (item_is_error(found)) return found;
            // an absent proxy key cannot shadow an inherited property with the same name.
            if (found.item == ITEM_FALSE) continue;
            Item stored = mvp_lmd_array_store(seen.get(), seen.get().array->length, key.get());
            if (item_is_error(stored)) return stored;
            if (!(descriptor.flags & JS_PD_ENUMERABLE)) continue;
            stored = mvp_lmd_array_store(result.get(), result.get().array->length, key.get());
            if (item_is_error(stored)) return stored;
        }
        if (!cls && !mvp_lmd_program_library()) break;
        object.set(mvp_lmd_get_prototype(object.get()));
        if (item_is_error(object.get())) return object.get();
        if (object.get().item == ITEM_NULL) break;
        cls = mvp_lmd_class_record(object.get());
    }
    return result.get();
}
extern "C" Item mvp_lmd_object_project(Item owner, int64_t projection) {
    if (mvp_lmd_foreign_candidate(owner, true)) {
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        Rooted<Item> object(roots, owner);
        Item keys;
        if (mvp_lmd_foreign_property(object.get(), ItemNull, ItemNull, object.get(), LMD_FOREIGN_PROJECT, &keys))
            return mvp_lmd_project_keys(object.get(), keys, projection);
    }
    if (get_type_id(owner) == LMD_TYPE_VMAP && js_module_namespace_record(owner))
        return js_module_namespace_project(owner, projection);
    if (mvp_lmd_is_proxy(owner)) return mvp_lmd_proxy_project(owner, projection);
    Map* map = mvp_lmd_property_map(owner);
    bool indexed = get_type_id(owner) == LMD_TYPE_ARRAY;
    bool typed = js_is_typed_array(owner);
    if (!map && get_type_id(owner) == LMD_TYPE_VMAP && js_promise_is(owner)) return mvp_lmd_array_new(0);
    if (!map && !indexed) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    if (!context->name_pool) context->name_pool = name_pool_create_runtime(context->pool);
    TypeMap* type = map ? (TypeMap*)map->type : NULL;
    MvpLmdLibraryState* state = mvp_lmd_program_library();
    bool arguments = state && state->arguments_key && type && typemap_hash_lookup_key(type, state->arguments_key);
    RootFrame roots(5);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner);
    Rooted<Array*> result(roots, array()), pair(roots, NULL);
    Rooted<Item> key(roots, ItemNull), value(roots, ItemNull);
    if (!result.get()) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    int64_t dense_count = indexed ? object.get().array->length : typed ? js_typed_array_length(owner) : 0;
    if (indexed && dense_count > container_dense_capacity(object.get().array)) dense_count = container_dense_capacity(object.get().array);
    int64_t sparse_count = indexed ? js_array_sparse_collect_indices(object.get(), 0, object.get().array->length, NULL, 0) : 0;
    int64_t capacity = (type ? type->length : 0) + dense_count + sparse_count + (indexed ? 1 : 0);
    ProjectionField* fields = capacity ? (ProjectionField*)pool_calloc(context->pool, capacity * sizeof(ProjectionField)) : NULL;
    if (capacity && !fields) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    int64_t count = 0;
    // keys-only projection never invokes getters; its own-descriptor snapshot stays valid across GC.
    bool reads_values = projection == 1 || projection == 2;
    bool callbacks = reads_values && (indexed || typed || arguments);
    if (indexed || typed) {
        // descriptor entries replace storage entries in the ordered own-key list.
        for (int64_t i = 0; i < dense_count; i++) {
            if (indexed && object.get().array->items[i].item == ITEM_JS_DELETED_SENTINEL) continue;
            fields[count++] = {NULL, (uint32_t)i, -1};
        }
        int64_t* indices = sparse_count ? (int64_t*)pool_alloc(context->pool, sparse_count * sizeof(int64_t)) : NULL;
        if (sparse_count && !indices) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        if (indexed) js_array_sparse_collect_indices(object.get(), 0, object.get().array->length, indices, sparse_count);
        for (int64_t i = 0; i < sparse_count; i++) fields[count++] = {NULL, (uint32_t)indices[i], -1};
        if (indices) pool_free(context->pool, indices);
        if (indexed && (projection == 3 || projection == 5)) fields[count++] = {NULL, UINT32_MAX, -1};
    }
    if (type) {
        FOR_EACH_MAP_FIELD(type, field) {
            if (field->key_kind == NAME_KEY_PRIVATE || (field->key_kind != NAME_KEY_STRING && projection != 4 && projection != 5) ||
                    ((projection == 0 || projection == 4) && (field->flags & JSPD_NON_ENUMERABLE))) continue;
            callbacks |= reads_values && (field->flags & JSPD_IS_ACCESSOR) != 0;
            NameRef name = field->name_id ? name_pool_resolve_id(context->name_pool, field->name_id) : NULL;
            if (field->name_id && !name) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            uint32_t index = NAME_ARRAY_INDEX_NONE;
            if (field->key_kind == NAME_KEY_STRING) {
                if (name) index = property_key_array_index(name);
                else js_property_name_to_array_index(field->name->str, field->name->length, &index);
            }
            if (indexed && field->key_kind == NAME_KEY_STRING && field->name->length == 6 &&
                    memcmp(field->name->str, "length", 6) == 0) continue;
            // values without callbacks only need field order; retain the shape-owned spelling
            // and materialize a canonical key below only when the projection consumes it.
            fields[count] = {field, index,
                count + (field->key_kind != NAME_KEY_STRING ? capacity : 0), name}; count++;
        }
    }
    // named fields already follow insertion order; sort only an actual key-order inversion.
    for (int64_t i = 1; i < count; i++) if (projection_order(&fields[i - 1], &fields[i]) > 0) {
        qsort(fields, (size_t)count, sizeof(ProjectionField), projection_order);
        break;
    }
    // the key snapshot bounds output length; reserve scalar-tail headroom before callbacks.
    if (!array_reserve_append_slots(result.get(), count * (projection == 1 ? 2 : 1))) {
        if (fields) pool_free(context->pool, fields);
        return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    }
    uint64_t value_home = 0;
    for (int64_t i = 0; i < count; i++) {
        bool element = fields[i].index != UINT32_MAX && !fields[i].field;
        bool length_key = !fields[i].field && !element;
        if (fields[i].index != UINT32_MAX && i + 1 < count && fields[i + 1].index == fields[i].index) continue;
        ShapeEntry* field = fields[i].field;
        if (projection != 1 || callbacks) {
            if (field && !fields[i].key) {
                fields[i].key = heap_create_name(field->name->str, field->name->length);
                if (!fields[i].key) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            }
            key.set(element ? mvp_lmd_number_to_string((double)fields[i].index) : length_key ?
                Item{.item = s2it(heap_create_name("length", 6))} :
                Item{.item = s2it(fields[i].key)});
            if (item_is_error(key.get())) return key.get();
        }
        if (projection < 3 || projection == 4) {
            if (callbacks) field = own_field(mvp_lmd_property_map(object.get()), key.get().get_string());
            if (field && (field->flags & JSPD_NON_ENUMERABLE)) continue;
            if (!field && !element && !length_key) continue;
            if (element && !field && (typed ? fields[i].index >= (uint32_t)js_typed_array_length(object.get()) :
                    mvp_lmd_array_own_item(object.get().array, fields[i].index).item == ITEM_JS_DELETED_SENTINEL)) continue;
        }
        value.set(projection == 0 || projection >= 3 ? key.get() : lambda_item_adopt_scalar_home(arguments || typed ?
            mvp_lmd_class_property(object.get(), key.get(), ItemNull, LMD_PROP_GET, NULL) : indexed ?
            mvp_lmd_array_property(object.get(), key.get(), ItemNull, LMD_PROP_GET, object.get()) :
            field->flags & JSPD_IS_ACCESSOR ? mvp_lmd_accessor_call(object.get(), field, ItemNull, LMD_PROP_GET) :
                // without accessors, enumeration cannot change the rooted owner's property face.
                map_shape_field_to_item((callbacks ? mvp_lmd_property_map(object.get()) : map)->data, field), &value_home));
        if (item_is_error(value.get())) return value.get();
        if (projection == 2) {
            pair.set(array());
            if (!pair.get()) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            Item written = store(pair.get(), 0, key.get());
            if (get_type_id(written) == LMD_TYPE_ERROR) return written;
            written = store(pair.get(), 1, value.get());
            if (get_type_id(written) == LMD_TYPE_ERROR) return written;
            value.set(mvp_lmd_array_adopt_realm(Item{.array = pair.get()}));
            if (item_is_error(value.get())) return value.get();
        }
        Item written = store(result.get(), result.get()->length, value.get());
        if (get_type_id(written) == LMD_TYPE_ERROR) return written;
    }
    if (fields) pool_free(context->pool, fields);
    return mvp_lmd_array_adopt_realm(Item{.array = result.get()});
}
extern "C" Item mvp_lmd_object_copy(Item target, Item source, int64_t assign, Item excluded) {
    TypeId type = get_type_id(source);
    if (!mvp_lmd_is_object(source) && type != LMD_TYPE_STRING) return target;
    RootFrame roots(6);
    if (!roots.valid()) return ItemError;
    Rooted<Item> output(roots, target), input(roots, source), keys(roots, ItemNull), key(roots, ItemNull), value(roots, ItemNull);
    Rooted<Item> omitted(roots, excluded);
    if (type == LMD_TYPE_FUNC) {
        Item ready = mvp_lmd_function_prepare(input.get());
        if (item_is_error(ready)) return ready;
    }
    if (type != LMD_TYPE_STRING) {
        keys.set(mvp_lmd_object_project(input.get(), 5));
        if (item_is_error(keys.get())) return keys.get();
    }
    int64_t length = type == LMD_TYPE_STRING ? utf8_to_utf16_length(input.get().get_string()->chars, input.get().get_string()->len) : keys.get().array->length;
    uint64_t home = 0;
    for (int64_t i = 0; i < length; i++) {
        key.set(type == LMD_TYPE_STRING ? mvp_lmd_number_to_string(i) : keys.get().array->items[i]);
        if (item_is_error(key.get())) return key.get();
        // CopyDataProperties excludes keys before descriptor/getter effects, retaining Symbol identity.
        bool skip = false;
        if (omitted.get().item != ITEM_NULL) for (int64_t j = 0; j < omitted.get().array->length; j++)
            if (mvp_lmd_property_key_equal(key.get(), omitted.get().array->items[j])) { skip = true; break; }
        if (skip) continue;
        if (type != LMD_TYPE_STRING) {
            JsPropertyDescriptor descriptor = {};
            Item status = mvp_lmd_get_own_property(input.get(), key.get(), &descriptor);
            if (item_is_error(status)) return status;
            if (status.item == ITEM_FALSE || !(descriptor.flags & JS_PD_ENUMERABLE)) continue;
        }
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
