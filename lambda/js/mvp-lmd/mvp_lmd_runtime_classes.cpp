#include "mvp_lmd_runtime.h"
#include "../js_ast.hpp"
#include "../js_props.h"
#include "../js_well_known_names.h"
#include "../../input/input.hpp"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/heap_api.h"
#include "../../runtime/runtime-state.h"
#include "../../runtime/lambda-error.h"

// class semantics live separately so new lookup sites do not reshape ordinary-object hot helpers.

MvpLmdClass* mvp_lmd_class_record(Item owner) {
    if (get_type_id(owner) == LMD_TYPE_FUNC) {
        Function* fn = owner.function;
        if (fn->entry_abi != FN_ENTRY_ABI_MVP_LMD || !fn->requires_runtime_context) return NULL;
        MvpLmdCallable* callable = (MvpLmdCallable*)fn;
        return callable->constructor ? callable->home : callable->properties;
    }
    if (get_type_id(owner) != LMD_TYPE_MAP) return NULL;
    TypeNominal* nominal = ((TypeMap*)owner.map->type)->nominal;
    return nominal && nominal->extension == &mvp_lmd_class_extension
        ? (MvpLmdClass*)nominal->extension_data : NULL;
}
extern "C" Item mvp_lmd_get_prototype(Item object) {
    if (mvp_lmd_is_proxy(object)) return mvp_lmd_proxy_property(object, ItemNull, ItemNull, object, LMD_PROXY_PROTOTYPE);
    TypeId kind = get_type_id(object);
    if (kind == LMD_TYPE_FUNC && !mvp_lmd_class_record(object)) {
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        Rooted<Item> held(roots, object);
        Item ready = mvp_lmd_function_prepare(held.get());
        if (item_is_error(ready)) return ready;
        return mvp_lmd_get_prototype(held.get());
    }
    Map* map = mvp_lmd_property_map(object);
    if (!map) return mvp_lmd_fail(kind == LMD_TYPE_NULL || kind == LMD_TYPE_UNDEFINED ? LMD_MVP_TYPE : LMD_MVP_CAPABILITY, 0);
    MvpLmdLibraryState* library = mvp_lmd_program_library();
    if (library && map->type) {
        ShapeEntry* link = typemap_hash_lookup_key((TypeMap*)map->type, library->prototype_key);
        if (link) return map_shape_field_to_item(map->data, link);
    }
    if (kind == LMD_TYPE_ARRAY) return library ? library->values[
        object.item == library->values[LMD_LIBRARY_ARRAY_PROTOTYPE].item
        ? LMD_LIBRARY_OBJECT_PROTOTYPE : LMD_LIBRARY_ARRAY_PROTOTYPE] : ItemNull;
    MvpLmdClass* cls = mvp_lmd_class_record(object);
    if (!cls) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    Item* values = mvp_lmd_class_values(cls);
    bool statics = kind == LMD_TYPE_FUNC || object.item == values[2].item;
    if (!statics && object.item != values[1].item) return values[1];
    if (cls->nominal.base && (!statics || cls->ast)) {
        if (cls->nominal.base->extension != &mvp_lmd_class_extension) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        MvpLmdClass* base = (MvpLmdClass*)cls->nominal.base->extension_data;
        return mvp_lmd_class_values(base)[statics ? 0 : 1];
    }
    if (!library || (!statics && cls == &library->object)) return ItemNull;
    if (statics && object.item != library->values[LMD_LIBRARY_FUNCTION_PROTOTYPE].item &&
            get_type_id(library->values[LMD_LIBRARY_FUNCTION_PROTOTYPE]) == LMD_TYPE_FUNC)
        return library->values[LMD_LIBRARY_FUNCTION_PROTOTYPE];
    return library->values[LMD_LIBRARY_OBJECT_PROTOTYPE];
}
extern "C" Item mvp_lmd_object_extensible(Item object, int64_t prevent) {
    if (mvp_lmd_is_proxy(object)) return mvp_lmd_proxy_property(object, ItemNull, ItemNull, object,
        prevent ? LMD_PROXY_PREVENT : LMD_PROXY_EXTENSIBLE);
    if (get_type_id(object) == LMD_TYPE_FUNC && !mvp_lmd_class_record(object)) {
        Item ready = mvp_lmd_function_prepare(object);
        if (item_is_error(ready)) return ready;
    }
    Map* map = mvp_lmd_property_map(object, prevent != 0);
    if (!map) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    MvpLmdLibraryState* library = mvp_lmd_program_library();
    if (!library) return prevent ? mvp_lmd_fail(LMD_MVP_CAPABILITY, 0) : Item{.item = ITEM_TRUE};
    ShapeEntry* field = map->type ? typemap_hash_lookup_key((TypeMap*)map->type, library->extensibility_key) : NULL;
    if (field) return Item{.item = prevent ? ITEM_TRUE : ITEM_FALSE};
    if (!prevent) return Item{.item = ITEM_TRUE};
    if (!map->type) map->type = type_tree_map_root(runtime_shape_tree());
    return map_shape_set_resolved(map, library->extensibility_key, NULL, Item{.item = ITEM_FALSE})
        ? Item{.item = ITEM_TRUE} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}
extern "C" Item mvp_lmd_set_prototype(Item object, Item prototype) {
    TypeId kind = get_type_id(prototype);
    if (kind != LMD_TYPE_MAP && kind != LMD_TYPE_ARRAY && kind != LMD_TYPE_FUNC && kind != LMD_TYPE_NULL)
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (mvp_lmd_is_proxy(object)) return mvp_lmd_proxy_property(object, prototype, ItemNull, object, LMD_PROXY_SET_PROTOTYPE);
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, object), parent(roots, prototype), cursor(roots, ItemNull);
    cursor.set(mvp_lmd_get_prototype(held.get()));
    if (item_is_error(cursor.get())) return cursor.get();
    if (cursor.get().item == parent.get().item) return Item{.item = ITEM_TRUE};
    MvpLmdLibraryState* state = mvp_lmd_program_library();
    if (state && held.get().item == state->values[LMD_LIBRARY_OBJECT_PROTOTYPE].item) return Item{.item = ITEM_FALSE};
    Item extensible = mvp_lmd_object_extensible(held.get(), false);
    if (item_is_error(extensible) || extensible.item == ITEM_FALSE) return extensible;
    cursor.set(parent.get());
    while (cursor.get().item != ITEM_NULL) {
        if (cursor.get().item == held.get().item) return Item{.item = ITEM_FALSE};
        // OrdinarySetPrototypeOf stops cycle probing at an exotic internal method.
        if (mvp_lmd_is_proxy(cursor.get())) break;
        cursor.set(mvp_lmd_get_prototype(cursor.get()));
        if (item_is_error(cursor.get())) return cursor.get();
    }
    Map* map = mvp_lmd_property_map(held.get(), true);
    MvpLmdLibraryState* library = mvp_lmd_program_library();
    if (!library || !map) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    if (!map->type) map->type = type_tree_map_root(runtime_shape_tree());
    // D2.6.10: this traced private slot never rewrites TypeNominal::base.
    return map_shape_set(map, library->prototype_key, parent.get())
        ? Item{.item = ITEM_TRUE} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}
static Item mvp_lmd_inherited_member_flags(Item owner, const char* key, size_t length, bool* found, uint8_t* flags, NameRef identity = NULL, ShapeEntry** descriptor = NULL, Item actual_receiver = Item{.item = 0}, bool semantic = false) {
    *found = false;
    MvpLmdClass* cls = mvp_lmd_class_record(owner);
    if (!cls) return Item{.item = ITEM_JS_UNDEFINED};
    Item receiver = actual_receiver.item ? actual_receiver : owner;
    auto resolve = [&](Map* properties) -> Item {
        ShapeEntry* field = properties->type ? (identity
            ? typemap_hash_lookup_key((TypeMap*)properties->type, identity)
            : typemap_hash_lookup((TypeMap*)properties->type, key, (int)length)) : NULL;
        if (!field) return Item{.item = ITEM_JS_UNDEFINED};
        *found = true;
        if (descriptor) *descriptor = field;
        if (flags) { *flags = field->flags; return ItemNull; }
        return field->flags & JSPD_IS_ACCESSOR ? mvp_lmd_accessor_call(receiver, field, ItemNull, LMD_PROP_GET)
            : map_shape_field_to_item(properties->data, field);
    };
    MvpLmdLibraryState* library = mvp_lmd_program_library(cls->program);
    if (!library || !library->mutable_prototypes) {
        // closed units without reflective writers retain the declared-chain proof.
        bool statics = get_type_id(owner) == LMD_TYPE_FUNC || owner.map == mvp_lmd_class_values(cls)[2].map;
        TypeNominal* nominal = &cls->nominal;
        if (statics) nominal = cls->ast ? nominal->base : NULL;
        else if (owner.map == mvp_lmd_class_values(cls)[1].map) nominal = nominal->base;
        for (; nominal; nominal = nominal->base) {
            if (nominal->extension != &mvp_lmd_class_extension) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
            MvpLmdClass* current = (MvpLmdClass*)nominal->extension_data;
            Item prototype = mvp_lmd_class_values(current)[statics ? 2 : 1];
            if (prototype.item == ITEM_NULL) continue;
            Item result = resolve(prototype.map);
            if (*found || item_is_error(result)) return result;
        }
        return Item{.item = ITEM_JS_UNDEFINED};
    }
    Item cursor = mvp_lmd_get_prototype(owner);
    while (cursor.item != ITEM_NULL) {
        if (item_is_error(cursor)) return cursor;
        if (semantic && (get_type_id(cursor) != LMD_TYPE_MAP || mvp_lmd_is_proxy(cursor))) {
            // array indices and exotic internal methods need the original receiver and semantic dispatch.
            RootFrame roots(3);
            if (!roots.valid()) return ItemError;
            Rooted<Item> held(roots, receiver), parent(roots, cursor), name(roots, Item{.item = s2it(identity)});
            Item result = mvp_lmd_property_with_receiver(parent.get(), name.get(), ItemNull,
                flags ? LMD_PROP_HAS : LMD_PROP_GET, held.get());
            *found = !flags || result.item == ITEM_TRUE;
            return result;
        }
        if (mvp_lmd_is_proxy(cursor)) {
            // pure Lambda member reads must not invoke guest traps (S12.1.1v2).
            return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        }
        Map* properties = mvp_lmd_property_map(cursor);
        if (!properties) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        // ordinary Map traversal cannot collect; an accessor roots its receiver/callable and returns immediately.
        Item result = resolve(properties);
        if (*found || item_is_error(result)) return result;
        cursor = mvp_lmd_get_prototype(cursor);
    }
    return Item{.item = ITEM_JS_UNDEFINED};
}

static Item mvp_lmd_inherited_member(Item owner, const char* key, size_t length, bool* found) {
    uint8_t flags = 0;
    Item result = mvp_lmd_inherited_member_flags(owner, key, length, found, &flags);
    if (item_is_error(result) || !*found) return result;
    // S12.1.1v2: Lambda nominal reads cannot hide an effectful JS accessor call.
    return flags & JSPD_IS_ACCESSOR ? mvp_lmd_fail(LMD_MVP_CAPABILITY, 0) :
        mvp_lmd_inherited_member_flags(owner, key, length, found, NULL);
}
static Item mvp_lmd_nominal_owner(const void* data) {
    const MvpLmdClass* cls = (const MvpLmdClass*)data;
    return cls->owner ? Item{.function = cls->owner} : ItemNull;
}
// closed ordinary families carry guest identity without class callbacks or retained owners.
const TypeNominalExtension mvp_lmd_plain_extension = {};
const TypeNominalExtension mvp_lmd_class_extension = {mvp_lmd_inherited_member, mvp_lmd_nominal_owner};

void mvp_lmd_cache_property(MvpLmdPropertyCache* cache, TypeMap* shape,
        ShapeEntry* field, Item inherited, bool writable, bool nullable_initializer) {
    MvpLmdPropertyCacheEntry* entry = NULL;
    for (int i = 0; i < MVP_LMD_PROPERTY_CACHE_SIZE; i++)
        if (cache->entries[i].shape == shape) { entry = &cache->entries[i]; break; }
    if (!entry) entry = &cache->entries[cache->next++ % MVP_LMD_PROPERTY_CACHE_SIZE];
    entry->shape = shape; entry->field = field;
    entry->inherited = field ? ItemNull : inherited;
    entry->writable = writable && (!field || !(field->flags & (JSPD_NON_WRITABLE | JSPD_IS_ACCESSOR)));
    entry->nullable_initializer = nullable_initializer;
    // only ordinary packed fields use the inline cache lane; shared readers own optional layouts.
    entry->offset = field ? field->byte_offset : 0;
    entry->storage = field && field->name && field->byte_offset >= 0 &&
        !(field->flags & JSPD_IS_ACCESSOR) && !shape_entry_uses_native_lane(field, NULL)
        ? shape_entry_storage_type_id(field) : LMD_TYPE_ANY;
    TypeId type = entry->storage;
    entry->pointer_lane = type == LMD_TYPE_STRING ? 1 : type == LMD_TYPE_FUNC ||
        type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY || type == LMD_TYPE_ARRAY_NUM ? 2 : 0;
    // function Items are direct pointers, like Maps; tagging them changes strict identity.
    entry->pointer_tag = type == LMD_TYPE_STRING ? (uint64_t)type << 56 : 0;
}

static bool mvp_lmd_class_widen_allocation(MvpLmdClass* cls, String* key, TypeId value_type) {
    if (!cls->allocation_shape || !cls->nullable_initializer_count ||
            (value_type != LMD_TYPE_MAP && value_type != LMD_TYPE_NULL)) return false;
    for (int i = 0; i < cls->nullable_initializer_count; i++) {
        String* name = cls->nullable_initializers[i];
        if (name->len != key->len || memcmp(name->chars, key->chars, key->len)) continue;
        if (value_type == LMD_TYPE_MAP) {
            ShapeEntry* field = typemap_hash_lookup(cls->allocation_shape, key->chars, key->len);
            // widen only this declared null initializer; never copy an instance's extra fields.
            if (field && field->type == &TYPE_NULL) {
                TypeMap* shape = type_tree_retype_field(runtime_shape_tree(), cls->allocation_shape,
                    field, LMD_TYPE_MAP, NULL);
                if (shape) { cls->allocation_shape = shape; cls->allocation_epoch++; }
            }
        }
        return true;
    }
    return false;
}

template<bool fixed>
static Item mvp_lmd_class_property_impl(Item owner, Item name, Item value, int64_t operation,
        MvpLmdPropertyCache* cache, Item receiver) {
    if ((operation & ~LMD_PROP_STRICT) == LMD_PROP_CREATE_DATA) {
        JsPropertyDescriptor descriptor = {};
        descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_WRITABLE |
            JS_PD_HAS_ENUMERABLE | JS_PD_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
        js_pd_set_configurable(&descriptor, true);
        descriptor.value = value;
        Item status = mvp_lmd_define_own_property(owner, name, &descriptor);
        return status.item == ITEM_FALSE ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : status;
    }
    if (!fixed && (operation & ~LMD_PROP_STRICT) == LMD_PROP_SET &&
            mvp_lmd_program_library() && mvp_lmd_program_library()->mutable_prototypes) {
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        uint64_t home = 0;
        Rooted<Item> argument(roots, lambda_item_adopt_scalar_home(value, &home));
        Item status = mvp_lmd_set_completion(owner, name, argument.get(), receiver, cache);
        if (item_is_error(status)) return status;
        if (status.item == ITEM_FALSE && (operation & LMD_PROP_STRICT)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        return lambda_item_uses_scalar_home(argument.get()) ? push_d(it2d(argument.get())) : argument.get();
    }
    if (get_type_id(owner) == LMD_TYPE_ARRAY)
        return mvp_lmd_array_property(owner, name, value, operation, receiver);
    bool strict = (operation & LMD_PROP_STRICT) != 0;
    operation &= ~LMD_PROP_STRICT;
    if (!fixed && mvp_lmd_is_proxy(owner)) {
        if (operation == LMD_PROP_GET || operation == LMD_PROP_CALLEE || operation == LMD_PROP_HAS || operation == LMD_PROP_DELETE) {
            Item result = mvp_lmd_proxy_property(owner, name, value, receiver,
                operation == LMD_PROP_HAS ? LMD_PROXY_HAS : operation == LMD_PROP_DELETE ? LMD_PROXY_DELETE : LMD_PROXY_GET);
            return strict && operation == LMD_PROP_DELETE && result.item == ITEM_FALSE ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : result;
        }
        if (operation == LMD_PROP_OWN) {
            JsPropertyDescriptor descriptor = {};
            return mvp_lmd_get_own_property(owner, name, &descriptor);
        }
        if (operation >= LMD_PROP_KEYS && operation <= LMD_PROP_ENTRIES)
            return mvp_lmd_object_project(owner, operation - LMD_PROP_KEYS);
        return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    }
    bool initializing = operation == LMD_PROP_INITIALIZE;
    if (initializing) operation = LMD_PROP_SET;
    if (get_type_id(owner) == LMD_TYPE_FUNC && !mvp_lmd_class_record(owner)) {
        if (operation == LMD_PROP_GET || operation == LMD_PROP_CALLEE || operation == LMD_PROP_HAS) {
            MvpLmdLibraryState* library = fixed ? NULL : mvp_lmd_program_library();
            Item method = library && library->mutable_prototypes ? Item{.item = ITEM_JS_UNDEFINED} :
                mvp_lmd_function_method(owner, name.get_string());
            if (method.item != ITEM_JS_UNDEFINED) return operation == LMD_PROP_HAS ? Item{.item = ITEM_TRUE} : method;
            if (!owner.function->def && name.get_string()->len == 6 && !memcmp(name.get_string()->chars, "length", 6))
                return operation == LMD_PROP_HAS ? Item{.item = ITEM_TRUE} : Item{.item = i2it(owner.function->arity)};
        }
        Item prepared = mvp_lmd_function_prepare(owner);
        if (item_is_error(prepared)) return prepared;
    }
    MvpLmdClass* cls = mvp_lmd_class_record(owner);
    if (cls) {
        bool constructor = get_type_id(owner) == LMD_TYPE_FUNC;
        Map* map = constructor ? mvp_lmd_class_values(cls)[2].map : owner.map;
        bool metadata = constructor || map == mvp_lmd_class_values(cls)[1].map || map == mvp_lmd_class_values(cls)[2].map;
        if (metadata && operation >= LMD_PROP_KEYS) {
            return mvp_lmd_object_project(Item{.map = map}, operation - LMD_PROP_KEYS);
        }
        if (operation == LMD_PROP_GET || operation == LMD_PROP_CALLEE ||
                operation == LMD_PROP_OWN || operation == LMD_PROP_HAS) {
            String* key = name.get_string();
            ShapeEntry* field = typemap_hash_lookup_key((TypeMap*)map->type, key);
            bool found = field != NULL;
            if (field && (field->flags & JSPD_IS_ACCESSOR) &&
                    (operation == LMD_PROP_GET || operation == LMD_PROP_CALLEE))
                return mvp_lmd_accessor_call(receiver, field, ItemNull, LMD_PROP_GET);
            bool presence = operation == LMD_PROP_OWN || operation == LMD_PROP_HAS;
            Item result = found && !presence ? map_shape_field_to_item(map->data, field) : Item{.item = ITEM_JS_UNDEFINED};
            if (!found && operation != LMD_PROP_OWN) {
                uint8_t flags;
                result = mvp_lmd_inherited_member_flags(owner, key->chars, key->len, &found, presence ? &flags : NULL, key, NULL, receiver, true);
            }
            MvpLmdLibraryState* library = fixed ? NULL : mvp_lmd_program_library(cls->program);
            if (!found && constructor && operation != LMD_PROP_OWN && (!library || !library->mutable_prototypes)) {
                result = mvp_lmd_function_method(owner, key);
                found = result.item != ITEM_JS_UNDEFINED;
            }
            if (item_is_error(result)) return result;
            // immutable shapes invalidate on shadowing/retyping; methods remain program-rooted.
            TypeMap* shape = (TypeMap*)map->type;
            if (cache && !presence && !cls->owner && !constructor && found &&
                    (typemap_is_shared_shape(shape) || shape == &cls->shape) &&
                    (field || ((!library || !library->mutable_prototypes) && get_type_id(result) == LMD_TYPE_FUNC))) {
                mvp_lmd_cache_property(cache, shape, field, result, !metadata);
            }
            if (operation == LMD_PROP_OWN) return Item{.item = b2it(found)};
            if (found) return operation == LMD_PROP_HAS ? Item{.item = ITEM_TRUE} : result;
            // The nominal walk is complete, including a null prototype; no implicit Object fallback remains.
            if (map->map_kind != MAP_KIND_ORDERED)
                return Item{.item = operation == LMD_PROP_HAS ? ITEM_FALSE : ITEM_JS_UNDEFINED};
        }
        if (operation == LMD_PROP_SET) {
            ShapeEntry* field = typemap_hash_lookup_key((TypeMap*)map->type, name.get_string());
            ShapeEntry* descriptor = field;
            uint8_t flags = field ? field->flags : 0;
            if (!field) {
                bool found;
                Item inherited = mvp_lmd_inherited_member_flags(owner, name.get_string()->chars,
                    name.get_string()->len, &found, &flags, name.get_string(), &descriptor);
                if (item_is_error(inherited)) return inherited;
            }
            if (flags & (JSPD_IS_ACCESSOR | JSPD_NON_WRITABLE))
                return flags & JSPD_IS_ACCESSOR ? mvp_lmd_accessor_call(receiver, descriptor, value,
                    LMD_PROP_SET | (strict ? LMD_PROP_STRICT : 0)) : strict ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : value;
            if (receiver.item != owner.item && get_type_id(receiver) == LMD_TYPE_ARRAY)
                return mvp_lmd_array_property(receiver, name, value,
                    LMD_PROP_INITIALIZE | (strict ? LMD_PROP_STRICT : 0), receiver);
            if (receiver.item != owner.item) {
                map = mvp_lmd_property_map(receiver);
                if (!map) return strict ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : value;
                field = map->type ? typemap_hash_lookup_key((TypeMap*)map->type, name.get_string()) : NULL;
                if (field && (field->flags & (JSPD_IS_ACCESSOR | JSPD_NON_WRITABLE)))
                    return strict ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : value;
                owner = receiver; cache = NULL; metadata = true;
            }
            MvpLmdLibraryState* library = fixed ? NULL : mvp_lmd_program_library(cls->program);
            if (!field && library && library->mutable_prototypes) {
                Item allowed = mvp_lmd_object_extensible(owner, false);
                if (item_is_error(allowed)) return allowed;
                if (allowed.item == ITEM_FALSE) return strict ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : value;
            }
            // classes without nullable initializers need no extra value classification.
            bool nullable_initializer = !metadata && cls->nullable_initializer_count &&
                mvp_lmd_class_widen_allocation(cls, name.get_string(), get_type_id(value));
            // ordinary prototype data stays on shared dispatch; it must never populate legacy site state.
            if (field && cache && !cls->owner && !metadata && typemap_is_shared_shape((TypeMap*)map->type)) {
                mvp_lmd_cache_property(cache, (TypeMap*)map->type, field, ItemNull, true, nullable_initializer);
            }
            // Lambda's Map pointer lane already represents null; preserve its immutable shape as fn_map_set does.
            if (field && (!initializing || nullable_initializer) && field->type == &TYPE_MAP && get_type_id(value) == LMD_TYPE_NULL)
                return map_field_store((char*)map->data + field->byte_offset, value, LMD_TYPE_NULL)
                    ? value : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            if (field && field->type == &TYPE_FLOAT && get_type_id(value) == LMD_TYPE_INT) {
                // Lambda widens compatible integer writes in place instead of alternating numeric shapes.
                map_field_store_int_as_float((char*)map->data + field->byte_offset, value);
                return value;
            }
            // the nominal lookup and descriptor checks also serve the physical shape write.
            if (map->map_kind != MAP_KIND_ORDERED)
                return map_shape_set_resolved(map, name.get_string(), field, value)
                    ? value : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        }
        owner = Item{.map = map};
    }
    switch (operation) {
    case LMD_PROP_GET: case LMD_PROP_CALLEE:
        return mvp_lmd_property_get(owner, name, operation == LMD_PROP_CALLEE);
    case LMD_PROP_SET: {
        if (strict && get_type_id(owner) == LMD_TYPE_ARRAY && owner.array->type) {
            ShapeEntry* field = typemap_hash_lookup_key((TypeMap*)owner.array->type, name.get_string());
            if (field && (field->flags & JSPD_NON_WRITABLE)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        }
        return mvp_lmd_property_set(owner, name, value);
    }
    case LMD_PROP_DELETE: {
        Item result = mvp_lmd_property_delete(owner, name);
        return strict && result.item == ITEM_FALSE ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : result;
    }
    case LMD_PROP_OWN: case LMD_PROP_HAS:
        return mvp_lmd_property_has(owner, name, operation == LMD_PROP_HAS);
    default: return mvp_lmd_object_project(owner, operation - LMD_PROP_KEYS);
    }
}

extern "C" Item mvp_lmd_class_property(Item owner, Item name, Item value, int64_t operation,
        MvpLmdPropertyCache* cache) {
    return mvp_lmd_class_property_impl<false>(owner, name, value, operation, cache, owner);
}
extern "C" Item mvp_lmd_class_property_fixed(Item owner, Item name, Item value, int64_t operation,
        MvpLmdPropertyCache* cache) {
    // the compiler's closed-unit proof excludes proxies and reflective mutation before selecting this import.
    return mvp_lmd_class_property_impl<true>(owner, name, value, operation, cache, owner);
}
extern "C" Item mvp_lmd_property_with_receiver(Item owner, Item name, Item value, int64_t operation, Item receiver) {
    return mvp_lmd_class_property_impl<false>(owner, name, value, operation, NULL, receiver);
}

extern "C" Item mvp_lmd_class_super(Item function, int64_t constructor) {
    if (get_type_id(function) != LMD_TYPE_FUNC ||
            function.function->entry_abi != FN_ENTRY_ABI_MVP_LMD || !function.function->requires_runtime_context)
        return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    MvpLmdCallable* fn = (MvpLmdCallable*)function.function;
    if (fn->home_slot) {
        if (!fn->closure_env || fn->home_slot > fn->closure_field_count) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        Item home = ((Item*)fn->closure_env)[fn->home_slot - 1];
        if (get_type_id(home) == LMD_TYPE_FUNC) return mvp_lmd_class_super(home, constructor);
        if (constructor || get_type_id(home) != LMD_TYPE_MAP) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        return mvp_lmd_get_prototype(home);
    }
    if (!fn->home) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    return mvp_lmd_get_prototype(mvp_lmd_class_values(fn->home)[constructor || fn->static_method ? 0 : 1]);
}
extern "C" Item mvp_lmd_constructor_result(Item value, Item receiver, int64_t derived) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY || type == LMD_TYPE_ARRAY_NUM ||
            type == LMD_TYPE_FUNC || type == LMD_TYPE_ERROR) return value;
    if (derived && value.item != ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    return receiver.item == ITEM_JS_TDZ ? mvp_lmd_fail(LMD_MVP_REFERENCE, 0) : receiver;
}
extern "C" Item mvp_lmd_class_initialize_instance(Item constructor, Item receiver) {
    MvpLmdClass* cls = ((MvpLmdCallable*)constructor.function)->home;
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, constructor), object(roots, receiver), value(roots, ItemNull);
    uint32_t index = 0;
    for (AstNode* node = ((AstBlockNode*)cls->ast->body)->statements; node; node = node->next) {
        if (node->node_type != AST_NODE_FIELD || ((AstClassFieldNode*)node)->is_static) continue;
        AstClassFieldNode* field = (AstClassFieldNode*)node;
        Item initializer = cls->initializers[index++];
        uint64_t home = 0;
        value.set(lambda_item_adopt_scalar_home(initializer.item == ITEM_JS_UNDEFINED ? initializer :
            mvp_lmd_class_invoke(initializer, object.get(), NULL, 0, Item{.item = ITEM_JS_UNDEFINED}), &home));
        if (item_is_error(value.get())) return value.get();
        Item key = {.item = s2it(((AstIdentNode*)field->key)->name)};
        MvpLmdLibraryState* library = mvp_lmd_program_library(cls->program);
        Item stored = library && library->mutable_prototypes ?
            mvp_lmd_property_with_receiver(object.get(), key, value.get(), LMD_PROP_CREATE_DATA, object.get()) :
            mvp_lmd_property_set(object.get(), key, value.get());
        if (item_is_error(stored)) return stored;
    }
    return object.get();
}
extern "C" Item mvp_lmd_class_invoke(Item callee, Item receiver, Item* arguments,
        int64_t argc, Item new_target) {
    if (mvp_lmd_is_proxy(callee)) return mvp_lmd_proxy_invoke(callee, receiver, arguments, argc, new_target);
    if (get_type_id(callee) != LMD_TYPE_FUNC) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    Function* function = callee.function;
    if (function->entry_abi != FN_ENTRY_ABI_MVP_LMD || !function->requires_runtime_context ||
            function->runtime_context != context) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    MvpLmdCallable* fn = (MvpLmdCallable*)function;
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, callee), self(roots, receiver), target(roots, new_target);
    MvpLmdClass* reusable = NULL;
    if (fn->constructor) {
        if (new_target.item == ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        MvpLmdClass* cls = fn->home;
        MvpLmdLibraryState* library = mvp_lmd_program_library(fn->program);
        if (cls->constructor_id < 0 && cls->nominal.base && library && library->mutable_prototypes) {
            // default derived constructors follow the live constructor link; nominal ancestry stays fixed.
            RootFrame observed(1);
            if (!observed.valid()) return ItemError;
            Rooted<Item> prototype(observed, mvp_lmd_get_prototype(held.get()));
            if (item_is_error(prototype.get())) return prototype.get();
            if (!mvp_lmd_is_constructor(prototype.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            self.set(mvp_lmd_class_invoke(prototype.get(), receiver, arguments, argc, target.get()));
            return item_is_error(self.get()) ? self.get() : mvp_lmd_class_initialize_instance(held.get(), self.get());
        }
        while (cls->constructor_id < 0 && cls->nominal.base) {
            if (cls->initializer_count) {
                MvpLmdClass* base = (MvpLmdClass*)cls->nominal.base->extension_data;
                self.set(mvp_lmd_class_invoke(mvp_lmd_class_values(base)[0], receiver, arguments, argc, target.get()));
                return item_is_error(self.get()) ? self.get() : mvp_lmd_class_initialize_instance(held.get(), self.get());
            }
            cls = (MvpLmdClass*)cls->nominal.base->extension_data;
            held.set(mvp_lmd_class_values(cls)[0]); fn = (MvpLmdCallable*)held.get().function;
        }
        MvpLmdClass* actual = mvp_lmd_class_record(new_target);
        if (!actual || get_type_id(new_target) != LMD_TYPE_FUNC) {
            if (!mvp_lmd_is_constructor(target.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            actual = cls;
        }
        if (cls->nominal.base) {
            self.set(Item{.item = ITEM_JS_TDZ});
        } else {
            if (actual == cls && cls->reusable_layout) reusable = cls;
            self.set(mvp_lmd_object_new(reusable && reusable->allocation_shape
                ? reusable->allocation_shape : &actual->shape, 0));
            if (item_is_error(self.get())) return self.get();
            if (mvp_lmd_is_proxy(target.get())) {
                RootFrame observed(1);
                if (!observed.valid()) return ItemError;
                Rooted<Item> prototype(observed, ItemNull);
                Item key = {.item = s2it(heap_create_name("prototype", 9))};
                prototype.set(mvp_lmd_property_with_receiver(target.get(), key, ItemNull, LMD_PROP_GET, target.get()));
                if (item_is_error(prototype.get())) return prototype.get();
                if (!mvp_lmd_is_object(prototype.get())) prototype.set(mvp_lmd_program_library(fn->program)->values[LMD_LIBRARY_OBJECT_PROTOTYPE]);
                Item linked = mvp_lmd_set_prototype(self.get(), prototype.get());
                if (item_is_error(linked)) return linked;
            }
            if (cls->initializer_count) {
                Item initialized = mvp_lmd_class_initialize_instance(held.get(), self.get());
                if (item_is_error(initialized)) return initialized;
            }
            if (cls->constructor_id < 0) return self.get();
        }
    } else if (new_target.item != ITEM_JS_UNDEFINED && fn->native_constructor) {
        if (new_target.item != callee.item && !fn->custom_new_target && fn->ptr != (fn_ptr)mvp_lmd_function_bound)
            return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    } else if (new_target.item != ITEM_JS_UNDEFINED) {
        if (fn->non_constructible) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        const AstFuncNode* ast = (const AstFuncNode*)fn->def;
        if (!ast || (ast->node_type != AST_NODE_FUNC_EXPR && ast->node_type != AST_NODE_FUNC))
            return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        Item prepared = mvp_lmd_function_prepare(held.get());
        if (item_is_error(prepared)) return prepared;
        fn = (MvpLmdCallable*)held.get().function;
        MvpLmdClass* properties = fn->properties;
        MvpLmdLibraryState* library = mvp_lmd_program_library(fn->program);
        if (target.get().item == held.get().item && (!library || !library->mutable_prototypes)) {
            // the closed-unit proof retains this constructor's original prototype and nominal family.
            self.set(mvp_lmd_object_new(&properties->shape, 0));
            if (item_is_error(self.get())) return self.get();
        } else {
            if (!mvp_lmd_is_constructor(target.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            RootFrame observed(1);
            if (!observed.valid()) return ItemError;
            Rooted<Item> prototype(observed, ItemNull);
            String* key = heap_create_name("prototype", 9);
            if (!key) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            prototype.set(mvp_lmd_property_with_receiver(target.get(), Item{.item = s2it(key)}, ItemNull, LMD_PROP_GET, target.get()));
            if (item_is_error(prototype.get())) return prototype.get();
            TypeId type = get_type_id(prototype.get());
            if (type != LMD_TYPE_MAP && type != LMD_TYPE_ARRAY && type != LMD_TYPE_FUNC)
                prototype.set(mvp_lmd_program_library(fn->program)->values[LMD_LIBRARY_OBJECT_PROTOTYPE]);
            // a known newTarget determines nominal allocation; its live prototype remains independent.
            MvpLmdClass* actual = target.get().item == held.get().item ? properties : mvp_lmd_class_record(target.get());
            self.set(mvp_lmd_object_new(actual ? &actual->shape : &properties->shape, 0));
            if (item_is_error(self.get())) return self.get();
            if (prototype.get().item != mvp_lmd_class_values(properties)[1].item) {
                Item linked = mvp_lmd_set_prototype(self.get(), prototype.get());
                if (item_is_error(linked)) return linked;
            }
        }
    }
    // the caller roots the borrowed argument span; each MIR activation owns its receiver roots.
    typedef Item (*Entry)(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);
    Item result = ((Entry)fn->ptr)(context, fn->program, arguments, (uint64_t)argc,
        held.get(), self.get(), target.get());
    result = lambda_item_resolve_pending_slot(result);
    if (item_is_error(result)) return result;
    if (!fn->constructor && !fn->native_constructor && target.get().item != ITEM_JS_UNDEFINED)
        result = mvp_lmd_constructor_result(result, self.get(), false);
    // admitted initializers cannot observe these fields until every store has completed.
    if (reusable && result.item == self.get().item && get_type_id(result) == LMD_TYPE_MAP &&
            typemap_is_shared_shape((TypeMap*)result.map->type))
        reusable->allocation_shape = (TypeMap*)result.map->type;
    return result;
}
extern "C" Item mvp_lmd_instanceof(Item value, Item constructor, int64_t ordinary) {
    MvpLmdLibraryState* state = ordinary == 2 ? NULL : mvp_lmd_program_library();
    if (ordinary == 1 || (state && state->mutable_prototypes)) {
        RootFrame roots(3);
        if (!roots.valid()) return ItemError;
        Rooted<Item> object(roots, value), rhs(roots, constructor), method(roots, ItemNull);
        if (!ordinary) {
            if (!mvp_lmd_is_object(rhs.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            Item key = {.item = s2it(well_known_name_ref(JS_SYMBOL_HAS_INSTANCE))};
            method.set(mvp_lmd_property_with_receiver(rhs.get(), key, ItemNull, LMD_PROP_GET, rhs.get()));
            if (item_is_error(method.get())) return method.get();
            if (method.get().item != ITEM_JS_UNDEFINED && method.get().item != ITEM_NULL) {
                if (!mvp_lmd_is_callable(method.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
                Item argument = object.get();
                Item result = mvp_lmd_class_invoke(method.get(), rhs.get(), &argument, 1, Item{.item = ITEM_JS_UNDEFINED});
                return item_is_error(result) ? result : Item{.item = b2it(mvp_lmd_truthy(result))};
            }
        }
        if (!mvp_lmd_is_callable(rhs.get())) return ordinary ? Item{.item = ITEM_FALSE} : mvp_lmd_fail(LMD_MVP_TYPE, 0);
        if (get_type_id(rhs.get()) == LMD_TYPE_FUNC && rhs.get().function->ptr == (fn_ptr)mvp_lmd_function_bound)
            return mvp_lmd_instanceof(object.get(), ((Item*)rhs.get().function->closure_env)[0]);
        if (!mvp_lmd_is_object(object.get())) return Item{.item = ITEM_FALSE};
        String* name = heap_create_name("prototype", 9);
        if (!name) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        method.set(mvp_lmd_property_with_receiver(rhs.get(), Item{.item = s2it(name)}, ItemNull, LMD_PROP_GET, rhs.get()));
        if (item_is_error(method.get())) return method.get();
        if (!mvp_lmd_is_object(method.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        for (;;) {
            object.set(mvp_lmd_get_prototype(object.get()));
            if (item_is_error(object.get())) return object.get();
            if (object.get().item == ITEM_NULL) return Item{.item = ITEM_FALSE};
            if (object.get().item == method.get().item) return Item{.item = ITEM_TRUE};
        }
    }
    if (get_type_id(constructor) == LMD_TYPE_FUNC && !mvp_lmd_class_record(constructor)) {
        RootFrame roots(2);
        if (!roots.valid()) return ItemError;
        Rooted<Item> held_value(roots, value), held_constructor(roots, constructor);
        Item prepared = mvp_lmd_function_prepare(held_constructor.get());
        if (item_is_error(prepared)) return prepared;
        return mvp_lmd_instanceof(held_value.get(), held_constructor.get());
    }
    MvpLmdClass* cls = mvp_lmd_class_record(constructor);
    if (!cls || get_type_id(constructor) != LMD_TYPE_FUNC)
        return mvp_lmd_fail(get_type_id(constructor) == LMD_TYPE_FUNC ? LMD_MVP_CAPABILITY : LMD_MVP_TYPE, 0);
    if (!typemap_hash_lookup((TypeMap*)mvp_lmd_class_values(cls)[2].map->type, "prototype", 9))
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    MvpLmdLibraryState* library = mvp_lmd_program_library(cls->program);
    if (library && !library->mutable_prototypes && get_type_id(value) == LMD_TYPE_ARRAY) {
        // Lambda arrays retain their native layout; their fixed JS prototype is program-owned.
        bool result = cls == &library->object || (cls == &library->array &&
            value.item != library->values[LMD_LIBRARY_ARRAY_PROTOTYPE].item);
        return Item{.item = b2it(result)};
    }
    // fixed class-created links make the shared ancestry predicate exact for admitted instances.
    MvpLmdClass* actual = mvp_lmd_class_record(value);
    bool result = actual && get_type_id(value) == LMD_TYPE_MAP &&
        value.map != mvp_lmd_class_values(actual)[1].map && value.map != mvp_lmd_class_values(actual)[2].map &&
        lambda_nominal_derives_from(&actual->nominal, &cls->nominal);
    if (actual && get_type_id(value) == LMD_TYPE_MAP && value.map == mvp_lmd_class_values(actual)[1].map)
        result = lambda_nominal_derives_from(actual->nominal.base, &cls->nominal);
    return Item{.item = b2it(result)};
}
extern "C" Item mvp_lmd_throw(Item value, int64_t error_constructor) {
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    uint64_t home = 0;
    Rooted<Item> held(roots, lambda_item_adopt_scalar_home(value, &home));
    const char* message = get_type_id(value) == LMD_TYPE_STRING ? value.get_string()->chars : "Uncaught JavaScript throw";
    LambdaError* error = err_create_heap(ERR_USER_ERROR, message, NULL);
    if (!error) return ItemError;
    // the shared error trace owns the thrown value; scalar payloads get destination-owned storage.
    if (!error_constructor) {
        uint64_t* persistent_home = (uint64_t*)pool_calloc(context->pool, sizeof(uint64_t));
        if (!persistent_home) return ItemError;
        error->thrown_value_item = lambda_item_adopt_scalar_home(held.get(), persistent_home).item;
    }
    return err2it(error);
}
