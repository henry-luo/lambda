#include "mvp_lmd_runtime.h"
#include "mvp_lmd_async.h"
#include "mvp_lmd_dynamic.h"
#include "../js_ast.hpp"
#include "../js_props.h"
#include "../js_runtime.h"
#include "../js_source_module.hpp"
#include "../js_runtime_state.hpp"
#include "../js_property_attrs.h"
#include "../js_well_known_names.h"
#include "../js_typed_array.h"
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
Item mvp_lmd_get_prototype_with_library(Item object, MvpLmdLibraryState* library) {
    Item foreign;
    if (mvp_lmd_foreign_candidate(object) && mvp_lmd_foreign_property(object, ItemNull,
            ItemNull, object, LMD_FOREIGN_GET_PROTOTYPE, &foreign)) return foreign;
    if (get_type_id(object) == LMD_TYPE_VMAP && js_module_namespace_record(object)) return ItemNull;
    if (get_type_id(object) == LMD_TYPE_VMAP && js_promise_is(object)) return js_get_prototype(object);
    if (mvp_lmd_is_proxy(object)) return mvp_lmd_proxy_property(object, ItemNull, ItemNull, object, LMD_PROXY_PROTOTYPE);
    TypeId kind = get_type_id(object);
    if (kind == LMD_TYPE_FUNC && !mvp_lmd_class_record(object)) {
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        Rooted<Item> held(roots, object);
        Item ready = mvp_lmd_function_prepare(held.get());
        if (item_is_error(ready)) return ready;
        return mvp_lmd_get_prototype_with_library(held.get(), library);
    }
    Map* map = mvp_lmd_property_map(object);
    if (!map) return mvp_lmd_fail(kind == LMD_TYPE_NULL || kind == LMD_TYPE_UNDEFINED ? LMD_MVP_TYPE : LMD_MVP_CAPABILITY, 0);
    if (library && map->type) {
        TypeMap* shape = (TypeMap*)map->type;
        ShapeEntry* link = NULL;
        bool cached = false, shared = typemap_is_shared_shape(shape);
        if (MvpLmdPropertyCacheEntry* entry = shared ? library->prototype_links.find_shape(shape) : NULL) {
            // reuse the MIR cache's proven packed object-pointer lane, including null.
            if (entry->pointer_lane == 2 && map->data) {
                uint64_t pointer;
                memcpy(&pointer, (char*)map->data + entry->offset, sizeof(pointer));
                return Item{.item = pointer ? pointer : ITEM_NULL};
            }
            link = entry->field; cached = true;
        }
        if (!cached) {
            link = typemap_hash_lookup_key(shape, library->prototype_key);
            if (shared) mvp_lmd_cache_property(&library->prototype_links, shape, link, ItemNull, false);
        }
        // immutable shapes also make cached absence safe; values are read from the current owner.
        if (link) return map_shape_field_to_item(map->data, link);
    }
    if (kind == LMD_TYPE_ARRAY) {
        if (library && library->default_realm) library = library->default_realm;
        return library ? library->values[object.item == library->values[LMD_LIBRARY_ARRAY_PROTOTYPE].item
            ? LMD_LIBRARY_OBJECT_PROTOTYPE : LMD_LIBRARY_ARRAY_PROTOTYPE] : ItemNull;
    }
    MvpLmdClass* cls = mvp_lmd_class_record(object);
    if (!cls) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    library = mvp_lmd_program_library(cls->program);
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
extern "C" Item mvp_lmd_get_prototype(Item object) {
    return mvp_lmd_get_prototype_with_library(object, mvp_lmd_program_library());
}
extern "C" Item mvp_lmd_object_extensible(Item object, int64_t prevent) {
    if (prevent && js_is_typed_array(object)) {
        JsTypedArray* view = js_get_typed_array_ptr(object.map);
        // resizable backing storage cannot establish a permanent indexed property set.
        if (js_arraybuffer_resizable(view->base.buffer)) return Item{.item = ITEM_FALSE};
    }
    Item foreign;
    if (mvp_lmd_foreign_candidate(object) && mvp_lmd_foreign_property(object, ItemNull,
            ItemNull, object, prevent ? LMD_FOREIGN_PREVENT : LMD_FOREIGN_EXTENSIBLE, &foreign)) return foreign;
    if (get_type_id(object) == LMD_TYPE_VMAP && js_module_namespace_record(object))
        return Item{.item = b2it(prevent != 0)};
    if (get_type_id(object) == LMD_TYPE_VMAP && js_promise_is(object)) {
        // the native carrier owns this state; generic LJS object gates exclude VMAP.
        JsPromise* promise = (JsPromise*)object.vmap;
        if (prevent) promise->extensible = false;
        return Item{.item = b2it(prevent || promise->extensible)};
    }
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
    return map_shape_set_resolved(map, library->extensibility_key, NULL, Item{.item = ITEM_FALSE}, false)
        ? Item{.item = ITEM_TRUE} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}
extern "C" Item mvp_lmd_set_prototype(Item object, Item prototype) {
    TypeId kind = get_type_id(prototype);
    if (kind != LMD_TYPE_NULL && !mvp_lmd_is_object(prototype))
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    Item foreign;
    if (mvp_lmd_foreign_candidate(object) && mvp_lmd_foreign_property(object, ItemNull,
            prototype, object, LMD_FOREIGN_SET_PROTOTYPE, &foreign)) return foreign;
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
    if (get_type_id(held.get()) == LMD_TYPE_VMAP && js_promise_is(held.get())) {
        // validation above uses MVP callables; publish only the shared carrier's traced link.
        JsPromise* promise = (JsPromise*)held.get().vmap;
        promise->prototype_override = parent.get();
        promise->has_prototype_override = true;
        return Item{.item = ITEM_TRUE};
    }
    Map* map = mvp_lmd_property_map(held.get(), true);
    MvpLmdLibraryState* library = mvp_lmd_program_library();
    if (!library || !map) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    if (!map->type) map->type = type_tree_map_root(runtime_shape_tree());
    // D2.6.10: this traced private slot never rewrites TypeNominal::base.
    return map_shape_set(map, library->prototype_key, parent.get())
        ? Item{.item = ITEM_TRUE} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}
static Item mvp_lmd_inherited_member_flags(Item owner, const char* key, size_t length, bool* found, uint8_t* flags,
        NameRef identity = NULL, ShapeEntry** descriptor = NULL, Item actual_receiver = Item{.item = 0},
        bool semantic = false, MvpLmdClass* cls = NULL, MvpLmdLibraryState* library = NULL,
        MvpLmdPropertyCache* cache = NULL) {
    *found = false;
    // property dispatch already resolved this metadata; live prototype links are still read below.
    if (!cls) cls = mvp_lmd_class_record(owner);
    if (!cls) return Item{.item = ITEM_JS_UNDEFINED};
    Item receiver = actual_receiver.item ? actual_receiver : owner;
    if (!library) library = mvp_lmd_program_library(cls->program);
    auto resolve = [&](Map* properties) -> Item {
        TypeMap* shape = (TypeMap*)properties->type;
        ShapeEntry* field = NULL;
        bool cacheable = cache && shape && typemap_is_shared_shape(shape) &&
            (!library || !library->arguments_key);
        if (MvpLmdPropertyCacheEntry* entry = cacheable ? cache->find_shape(shape) : NULL)
            field = entry->field;
        if (!field && shape) {
            field = identity ? typemap_hash_lookup_key(shape, identity)
                : typemap_hash_lookup(shape, key, (int)length);
            // cache the visited shape's data slot, never a mutable chain's inherited value.
            if (cacheable && field && !(field->flags & JSPD_IS_ACCESSOR))
                mvp_lmd_cache_property(cache, shape, field, ItemNull, false);
        }
        if (!field) return Item{.item = ITEM_JS_UNDEFINED};
        *found = true;
        if (descriptor) *descriptor = field;
        if (flags) { *flags = field->flags; return ItemNull; }
        if (library && library->arguments_key && identity) {
            Item cell = mvp_lmd_arguments_cell(Item{.map = properties}, identity);
            if (get_type_id(cell) == LMD_TYPE_ARRAY) return cell.array->items[0];
        }
        return field->flags & JSPD_IS_ACCESSOR ? mvp_lmd_accessor_call(receiver, field, ItemNull, LMD_PROP_GET)
            : map_shape_field_to_item(properties->data, field);
    };
    Item cursor = ItemNull;
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
        // Declared class ancestry ends at the realm's ordinary intrinsic
        // prototypes; its fast-path proof does not make those methods absent.
        // closed callable methods retain their existing receiver-bound adapter.
        if (!library || statics) return Item{.item = ITEM_JS_UNDEFINED};
        cursor = library->values[LMD_LIBRARY_OBJECT_PROTOTYPE];
        if (!cursor.item || cursor.item == owner.item) return Item{.item = ITEM_JS_UNDEFINED};
    } else cursor = mvp_lmd_get_prototype_with_library(owner, library);
    // ordinary chain traversal cannot reenter; preserve the active library across its links.
    MvpLmdLibraryState* traversal_library = mvp_lmd_program_library();
    while (cursor.item != ITEM_NULL) {
        if (item_is_error(cursor)) return cursor;
        if (semantic && (get_type_id(cursor) != LMD_TYPE_MAP || mvp_lmd_is_proxy(cursor) ||
                mvp_lmd_foreign_candidate(cursor) ||
                cursor.map->map_kind == MAP_KIND_TYPED_ARRAY)) {
            // foreign prototype storage, array indices and exotic methods retain semantic dispatch.
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
        cursor = mvp_lmd_get_prototype_with_library(cursor, traversal_library);
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
    MvpLmdPropertyCacheEntry* entry = cache->find_shape(shape);
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

template<bool fixed, bool binding = false, int known_operation = -1>
static Item mvp_lmd_class_property_impl(Item owner, Item name, Item value, int64_t operation,
        MvpLmdPropertyCache* cache, Item receiver) {
    // a known operation prunes unrelated gates without duplicating property semantics.
    if constexpr (known_operation >= 0) operation = known_operation;
    auto inherited_binding = [&]() -> Item {
        RootFrame roots(3);
        if (!roots.valid()) return ItemError;
        Rooted<Item> object(roots, owner), key(roots, name), target(roots, receiver);
        Item found = mvp_lmd_class_property_impl<fixed>(object.get(), key.get(), ItemNull,
            LMD_PROP_HAS, NULL, target.get());
        if (item_is_error(found)) return found;
        if (found.item == ITEM_FALSE) return mvp_lmd_fail(LMD_MVP_REFERENCE, 0);
        // a proxy Has trap can install an own property; restart Get from the original owner.
        return mvp_lmd_class_property_impl<fixed>(object.get(), key.get(), ItemNull,
            LMD_PROP_GET, cache, target.get());
    };
    if (binding && (get_type_id(owner) != LMD_TYPE_MAP || owner.map->map_kind != MAP_KIND_PLAIN))
        return inherited_binding();
    if (!fixed && (operation & ~LMD_PROP_STRICT) == LMD_PROP_SET &&
            ((!mvp_lmd_is_object(owner)) ||
            (mvp_lmd_program_library() && mvp_lmd_program_library()->mutable_prototypes))) {
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        uint64_t home = 0;
        Rooted<Item> argument(roots, lambda_item_adopt_scalar_home(value, &home));
        Item status = mvp_lmd_set_completion(owner, name, argument.get(), receiver, cache);
        if (item_is_error(status)) return status;
        if (status.item == ITEM_FALSE && (operation & LMD_PROP_STRICT)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        return lambda_item_uses_scalar_home(argument.get()) ? push_d(it2d(argument.get())) : argument.get();
    }
    // class-definition attributes stay behind ordinary Set dispatch on this hot path.
    if ((operation & ~(LMD_PROP_STRICT | LMD_PROP_NON_ENUMERABLE)) == LMD_PROP_CREATE_DATA) {
        JsPropertyDescriptor descriptor = {};
        descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_WRITABLE |
            JS_PD_HAS_ENUMERABLE | ((operation & LMD_PROP_NON_ENUMERABLE) ? 0 : JS_PD_ENUMERABLE) | JS_PD_HAS_CONFIGURABLE;
        js_pd_set_configurable(&descriptor, true);
        descriptor.value = value;
        Item status = mvp_lmd_define_own_property(owner, name, &descriptor);
        return status.item == ITEM_FALSE ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : status;
    }
    TypeId owner_type = get_type_id(owner);
    MvpLmdClass* cls = owner_type == LMD_TYPE_MAP ? mvp_lmd_class_record(owner) : NULL;
    // resolved MVP nominal metadata already excludes a foreign Map carrier.
    if (!fixed && !cls && mvp_lmd_foreign_candidate(owner, true)) {
        int opcode = operation & ~LMD_PROP_STRICT;
        if (opcode >= LMD_PROP_KEYS && opcode <= LMD_PROP_ENTRIES)
            return mvp_lmd_object_project(owner, opcode - LMD_PROP_KEYS);
        Item foreign;
        if (mvp_lmd_foreign_property(owner, name, value, receiver, opcode, &foreign)) {
            if (item_is_error(foreign)) return foreign;
            if ((opcode == LMD_PROP_SET || opcode == LMD_PROP_DELETE) &&
                    (operation & LMD_PROP_STRICT) && foreign.item == ITEM_FALSE)
                return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            return opcode == LMD_PROP_SET ? value : foreign;
        }
    }
    if (get_type_id(owner) == LMD_TYPE_ARRAY)
        return mvp_lmd_array_property(owner, name, value, operation, receiver);
    if (!fixed && get_type_id(owner) == LMD_TYPE_VMAP && js_module_namespace_record(owner)) {
        int opcode = operation & ~LMD_PROP_STRICT;
        if (opcode >= LMD_PROP_KEYS && opcode <= LMD_PROP_ENTRIES)
            return js_module_namespace_project(owner, opcode - LMD_PROP_KEYS);
        if (opcode == LMD_PROP_OWN) {
            JsPropertyDescriptor descriptor = {};
            return js_module_namespace_get_own(owner, name, &descriptor);
        }
        JsModuleNamespaceOperation method = opcode == LMD_PROP_HAS
            ? JS_MODULE_NAMESPACE_HAS : opcode == LMD_PROP_DELETE ? JS_MODULE_NAMESPACE_DELETE :
            opcode == LMD_PROP_SET ? JS_MODULE_NAMESPACE_SET : JS_MODULE_NAMESPACE_GET;
        Item result = js_module_namespace_property(owner, name, method);
        if ((opcode == LMD_PROP_DELETE || opcode == LMD_PROP_SET) && result.item == ITEM_FALSE &&
                (operation & LMD_PROP_STRICT)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        return opcode == LMD_PROP_SET && !item_is_error(result) ? value : result;
    }
    if (!fixed && get_type_id(owner) == LMD_TYPE_VMAP && js_promise_is(owner))
        return operation >= LMD_PROP_KEYS && operation <= LMD_PROP_ENTRIES
            ? mvp_lmd_object_project(owner, operation - LMD_PROP_KEYS)
            : mvp_lmd_promise_property(owner, name, value, operation, receiver);
    if (!fixed && get_type_id(owner) == LMD_TYPE_MAP && owner.map->map_kind == MAP_KIND_TYPED_ARRAY) {
        double index;
        if (mvp_lmd_typed_array_index(owner, name, &index))
            return mvp_lmd_typed_array_property(owner, name, value, operation, receiver);
    }
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
    if (owner_type != LMD_TYPE_MAP) cls = mvp_lmd_class_record(owner);
    if (cls) {
        bool constructor = get_type_id(owner) == LMD_TYPE_FUNC;
        Map* map = constructor ? mvp_lmd_class_values(cls)[2].map : owner.map;
        auto is_metadata = [&]() {
            if (constructor) return true;
            Item* values = mvp_lmd_class_values(cls);
            return map == values[1].map || map == values[2].map;
        };
        if (operation >= LMD_PROP_KEYS && is_metadata()) {
            return mvp_lmd_object_project(Item{.map = map}, operation - LMD_PROP_KEYS);
        }
        if (operation == LMD_PROP_GET || operation == LMD_PROP_CALLEE ||
                operation == LMD_PROP_OWN || operation == LMD_PROP_HAS) {
            String* key = name.get_string();
            TypeMap* shape = (TypeMap*)map->type;
            MvpLmdPropertyCacheEntry* cached = cache && typemap_is_shared_shape(shape)
                ? cache->find_shape(shape) : NULL;
            // immutable shape identity proves the cached own descriptor; values stay live.
            ShapeEntry* field = cached && cached->field ? cached->field : typemap_hash_lookup_key(shape, key);
            bool found = field != NULL;
            if (binding && !found) return inherited_binding();
            bool presence = operation == LMD_PROP_OWN || operation == LMD_PROP_HAS;
            // an own descriptor proves presence without reading its value or consulting prototypes.
            if (found && presence) return Item{.item = ITEM_TRUE};
            if (field && (field->flags & JSPD_IS_ACCESSOR) &&
                    (operation == LMD_PROP_GET || operation == LMD_PROP_CALLEE))
                return mvp_lmd_accessor_call(receiver, field, ItemNull, LMD_PROP_GET);
            bool indexed = key->len && key->chars[0] >= '0' && key->chars[0] <= '9';
            // only indexed own names can alias a mapped parameter; misses still need the full prototype policy.
            MvpLmdLibraryState* library = fixed || (found && !indexed) ? NULL : mvp_lmd_program_library(cls->program);
            if (library && library->arguments_key && found && indexed) {
                Item cell = mvp_lmd_arguments_cell(owner, key);
                // a mapped value belongs to its cell, never to a cached Map byte offset.
                if (get_type_id(cell) == LMD_TYPE_ARRAY) return cell.array->items[0];
            }
            Item result = found && !presence ? map_shape_field_to_item(map->data, field) : Item{.item = ITEM_JS_UNDEFINED};
            if (!fixed && result.item == ITEM_JS_LAZY_GLOBAL_SENTINEL) {
                // lazy publication may transition the shape; never cache the marker's field.
                Item resolved;
                return js_resolve_lazy_global(owner, name, &resolved) ? resolved : mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
            }
            if (!found && operation != LMD_PROP_OWN) {
                uint8_t flags;
                result = mvp_lmd_inherited_member_flags(owner, key->chars, key->len, &found,
                    presence ? &flags : NULL, key, NULL, receiver, true, cls, library, presence ? NULL : cache);
            }
            if (!found && constructor && operation != LMD_PROP_OWN && (!library || !library->mutable_prototypes)) {
                result = mvp_lmd_function_method(owner, key);
                found = result.item != ITEM_JS_UNDEFINED;
            }
            if (item_is_error(result)) return result;
            // immutable shapes invalidate on shadowing/retyping; methods remain program-rooted.
            if (cache && !presence && !cls->owner && !constructor && found &&
                    (typemap_is_shared_shape(shape) || shape == &cls->shape) &&
                    (field || ((!library || !library->mutable_prototypes) && get_type_id(result) == LMD_TYPE_FUNC))) {
                mvp_lmd_cache_property(cache, shape, field, result, !is_metadata());
            }
            if (operation == LMD_PROP_OWN) return Item{.item = b2it(found)};
            if (found) return operation == LMD_PROP_HAS ? Item{.item = ITEM_TRUE} : result;
            // The nominal walk is complete, including a null prototype; no implicit Object fallback remains.
            if (map->map_kind != MAP_KIND_ORDERED)
                return Item{.item = operation == LMD_PROP_HAS ? ITEM_FALSE : ITEM_JS_UNDEFINED};
        }
        if (operation == LMD_PROP_SET) {
            bool metadata = is_metadata();
            ShapeEntry* field = typemap_hash_lookup_key((TypeMap*)map->type, name.get_string());
            ShapeEntry* descriptor = field;
            uint8_t flags = field ? field->flags : 0;
            if (!field) {
                bool found;
                Item inherited = mvp_lmd_inherited_member_flags(owner, name.get_string()->chars,
                    name.get_string()->len, &found, &flags, name.get_string(), &descriptor, Item{.item = 0}, false, cls);
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
            // the nominal lookup and descriptor checks also serve the physical shape write.
            if (map->map_kind != MAP_KIND_ORDERED)
                return map_shape_set_resolved(map, name.get_string(), field, value, !initializing || nullable_initializer)
                    ? value : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        }
        owner = Item{.map = map};
    }
    if (binding) return inherited_binding();
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
    if (operation == LMD_PROP_GET)
        return mvp_lmd_class_property_impl<false, false, LMD_PROP_GET>(owner, name, value, operation, cache, owner);
    if (operation == LMD_PROP_HAS)
        return mvp_lmd_class_property_impl<false, false, LMD_PROP_HAS>(owner, name, value, operation, cache, owner);
    if (operation == LMD_PROP_CALLEE)
        return mvp_lmd_class_property_impl<false, false, LMD_PROP_CALLEE>(owner, name, value, operation, cache, owner);
    if (operation == LMD_PROP_GET_BINDING)
        return mvp_lmd_class_property_impl<false, true>(owner, name, value, LMD_PROP_GET, cache, owner);
    return mvp_lmd_class_property_impl<false>(owner, name, value, operation, cache, owner);
}
extern "C" Item mvp_lmd_class_property_fixed(Item owner, Item name, Item value, int64_t operation,
        MvpLmdPropertyCache* cache) {
    // the compiler's closed-unit proof excludes proxies and reflective mutation before selecting this import.
    if (operation == LMD_PROP_GET)
        return mvp_lmd_class_property_impl<true, false, LMD_PROP_GET>(owner, name, value, operation, cache, owner);
    if (operation == LMD_PROP_CALLEE)
        return mvp_lmd_class_property_impl<true, false, LMD_PROP_CALLEE>(owner, name, value, operation, cache, owner);
    if (operation == LMD_PROP_GET_BINDING)
        return mvp_lmd_class_property_impl<true, true>(owner, name, value, LMD_PROP_GET, cache, owner);
    return mvp_lmd_class_property_impl<true>(owner, name, value, operation, cache, owner);
}
extern "C" Item mvp_lmd_property_with_receiver(Item owner, Item name, Item value, int64_t operation, Item receiver) {
    if (operation == LMD_PROP_GET_BINDING)
        return mvp_lmd_class_property_impl<false, true>(owner, name, value, LMD_PROP_GET, NULL, receiver);
    return mvp_lmd_class_property_impl<false>(owner, name, value, operation, NULL, receiver);
}

extern "C" Item mvp_lmd_function_home(Item function, int64_t constructor) {
    if (get_type_id(function) != LMD_TYPE_FUNC ||
            function.function->entry_abi != FN_ENTRY_ABI_MVP_LMD || !function.function->requires_runtime_context)
        return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    MvpLmdCallable* fn = (MvpLmdCallable*)function.function;
    if (fn->home_slot) {
        if (!fn->closure_env || fn->home_slot > fn->closure_field_count) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        Item home = ((Item*)fn->closure_env)[fn->home_slot - 1];
        if (get_type_id(home) == LMD_TYPE_FUNC) return mvp_lmd_function_home(home, constructor);
        if (constructor || get_type_id(home) != LMD_TYPE_MAP) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        return home;
    }
    if (!fn->home) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    return mvp_lmd_class_values(fn->home)[constructor || fn->static_method ? 0 : 1];
}
extern "C" Item mvp_lmd_class_super(Item function, int64_t constructor) {
    Item home = mvp_lmd_function_home(function, constructor);
    return item_is_error(home) ? home : mvp_lmd_get_prototype(home);
}
extern "C" Item mvp_lmd_constructor_result(Item value, Item receiver, int64_t derived) {
    TypeId type = get_type_id(value);
    // Promise carriers are objects too; explicit constructor returns retain identity.
    if (mvp_lmd_is_object(value) || type == LMD_TYPE_ERROR) return value;
    if (derived && value.item != ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    return receiver.item == ITEM_JS_TDZ ? mvp_lmd_fail(LMD_MVP_REFERENCE, 0) : receiver;
}
extern "C" Item mvp_lmd_class_initialize_instance(Item constructor, Item receiver, int64_t static_methods) {
    MvpLmdClass* cls = ((MvpLmdCallable*)constructor.function)->home;
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, constructor), object(roots, receiver), value(roots, ItemNull);
    if (cls->private_method_mask & (static_methods ? 2 : 1)) {
        RootFrame private_roots(1);
        if (!private_roots.valid()) return ItemError;
        Rooted<Item> methods(private_roots, ItemNull);
        methods.set(cls->field_keys[cls->field_count + (static_methods ? 1 : 0)]);
        FOR_EACH_MAP_FIELD((TypeMap*)methods.get().map->type, field) {
            NameRef name = name_pool_resolve_id(context->name_pool, field->name_id);
            if (!name) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            Item key = {.item = s2it(name)};
            // install all methods before fields; a repeated stamp throws on the existing own brand.
            Item stored = mvp_lmd_private_element(object.get(), key,
                field->flags & JSPD_IS_ACCESSOR ? Item{.item = ITEM_JS_UNDEFINED} :
                    map_shape_field_to_item(methods.get().map->data, field), LMD_PRIVATE_METHOD);
            if (item_is_error(stored)) return stored;
            if (field->flags & JSPD_IS_ACCESSOR) {
                Item storage = mvp_lmd_is_proxy(object.get()) ? js_proxy_private_slots(object.get(), false, nullptr, true) : object.get();
                if (!js_map_store_accessor(mvp_lmd_property_map(storage, true), name,
                        js_shape_entry_accessor_pair(field), JSPD_NON_ENUMERABLE, context->pool))
                    return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            }
        }
    }
    if (static_methods) return object.get();
    uint32_t index = 0, key_index = 0;
    for (AstNode* node = ((AstBlockNode*)cls->ast->body)->statements; node; node = node->next) {
        if (node->node_type != AST_NODE_FIELD) continue;
        uint32_t key_slot = key_index++;
        if (((AstClassFieldNode*)node)->is_static) continue;
        AstClassFieldNode* field = (AstClassFieldNode*)node;
        Item initializer = cls->initializers[index++];
        Item key = cls->field_keys ? cls->field_keys[key_slot] : Item{.item = s2it(((AstIdentNode*)field->key)->name)};
        uint64_t home = 0;
        value.set(lambda_item_adopt_scalar_home(initializer.item == ITEM_JS_UNDEFINED ? initializer :
            mvp_lmd_class_invoke(initializer, object.get(), &key, 1, Item{.item = ITEM_JS_UNDEFINED}), &home));
        if (item_is_error(value.get())) return value.get();
        MvpLmdLibraryState* library = mvp_lmd_program_library(cls->program);
        Item stored = field->is_private ? mvp_lmd_private_element(object.get(), key, value.get(), LMD_PRIVATE_ADD) :
            library && library->mutable_prototypes ?
            mvp_lmd_property_with_receiver(object.get(), key, value.get(), LMD_PROP_CREATE_DATA, object.get()) :
            mvp_lmd_property_set(object.get(), key, value.get());
        if (item_is_error(stored)) return stored;
    }
    return object.get();
}
extern "C" Item mvp_lmd_object_from_constructor(Item new_target, MvpLmdClass* fallback, int64_t source_layout) {
    if ((!fallback && !source_layout) || !mvp_lmd_is_constructor(new_target)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> target(roots, new_target), prototype(roots, ItemNull), object(roots, ItemNull);
    if (source_layout) {
        // the emitter proves a local ordinary entry before requesting its immutable source template.
        Item prepared = mvp_lmd_function_prepare(target.get());
        if (item_is_error(prepared)) return prepared;
        fallback = mvp_lmd_class_record(target.get());
        if (!fallback || !fallback->owner || !fallback->allocation_shape)
            return Item{.item = ITEM_JS_UNDEFINED};
    }
    prototype.set(mvp_lmd_constructor_prototype(target.get(),
        mvp_lmd_program_library(fallback->program)->values[LMD_LIBRARY_OBJECT_PROTOTYPE]));
    if (item_is_error(prototype.get())) return prototype.get();
    MvpLmdClass* actual = mvp_lmd_class_record(target.get());
    // a source subclass can still require its native base's physical carrier.
    // ordinary construction keeps its live prototype without claiming that ancestry.
    // the caller's ordinary allocation class is already admitted; only a different newTarget needs this walk.
    if (actual && actual != fallback) {
        MvpLmdLibraryState* library = mvp_lmd_program_library(fallback->program);
        for (TypeNominal* nominal = &actual->nominal; nominal; nominal = nominal->base) {
            if (nominal == &library->object.nominal) break;
            if (nominal->extension != &mvp_lmd_class_extension || !nominal->extension_data) {
                actual = NULL; break;
            }
            Item constructor = mvp_lmd_class_values((MvpLmdClass*)nominal->extension_data)[0];
            if (get_type_id(constructor) != LMD_TYPE_FUNC ||
                    constructor.function->entry_abi != FN_ENTRY_ABI_MVP_LMD ||
                    ((MvpLmdCallable*)constructor.function)->native_constructor) {
                actual = NULL; break;
            }
        }
    }
    MvpLmdClass* allocation = actual ? actual : fallback;
    if (source_layout) {
        Item cursor = prototype.get();
        int depth = 0;
        MvpLmdLibraryState* library = mvp_lmd_program_library(fallback->program);
        while (cursor.item != ITEM_NULL) {
            if (++depth > 32 || get_type_id(cursor) != LMD_TYPE_MAP ||
                    cursor.map->map_kind != MAP_KIND_PLAIN || mvp_lmd_foreign_candidate(cursor, true))
                return Item{.item = ITEM_JS_UNDEFINED};
            TypeMap* shape = (TypeMap*)cursor.map->type;
            if (!shape || shape->js_meta) return Item{.item = ITEM_JS_UNDEFINED};
            FOR_EACH_MAP_FIELD(allocation->allocation_shape, field) {
                // even inherited data is rejected: setters/nonwritable fields must see ordinary Set.
                if (typemap_hash_lookup(shape, field->name->str, field->name->length))
                    return Item{.item = ITEM_JS_UNDEFINED};
            }
            cursor = mvp_lmd_get_prototype_with_library(cursor, library);
            if (item_is_error(cursor)) return cursor;
        }
    }
    object.set(mvp_lmd_object_new(source_layout ? allocation->allocation_shape : &allocation->shape, 0));
    if (item_is_error(object.get())) return object.get();
    if (prototype.get().item != mvp_lmd_class_values(allocation)[1].item) {
        Item linked = mvp_lmd_set_prototype(object.get(), prototype.get());
        if (item_is_error(linked)) return linked;
    }
    return object.get();
}
extern "C" Item mvp_lmd_class_invoke(Item callee, Item receiver, Item* arguments,
        int64_t argc, Item new_target) {
    if (mvp_lmd_is_proxy(callee)) return mvp_lmd_proxy_invoke(callee, receiver, arguments, argc, new_target);
    if (get_type_id(callee) != LMD_TYPE_FUNC) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    Function* function = callee.function;
    if (function_has_abi(function, FN_ENTRY_ABI_JS_FUNCTION)) {
        // reuse the identity-cached native bridge even when ingress bypassed a
        // foreign property read (for example, a host array's own method).
        RootFrame roots(3);
        if (!roots.valid()) return ItemError;
        Rooted<Item> held(roots, callee), self(roots, receiver), target(roots, new_target);
        MvpLmdLibraryState* library = mvp_lmd_program_library();
        if (!library) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        if (library->native_callable_values)
            return mvp_lmd_dynamic_call_native(library->object.program, held.get(), self.get(),
                arguments, (uint64_t)argc, target.get());
        held.set(mvp_lmd_dynamic_import_value(library->object.program, held.get()));
        return item_is_error(held.get()) ? held.get() :
            mvp_lmd_class_invoke(held.get(), self.get(), arguments, argc, target.get());
    }
    if (function->entry_abi != FN_ENTRY_ABI_MVP_LMD || !function->requires_runtime_context ||
            function->runtime_context != context) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    MvpLmdCallable* fn = (MvpLmdCallable*)function;
    MvpLmdRealmScope realm(fn->program);
    auto invoke = [&]() -> Item {
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, callee), self(roots, receiver), target(roots, new_target);
    MvpLmdClass* reusable = NULL;
    if (fn->constructor) {
        if (new_target.item == ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        MvpLmdClass* cls = fn->home;
        MvpLmdLibraryState* library = mvp_lmd_program_library(fn->program);
        if (!mvp_lmd_class_has_constructor(cls) && cls->ast->superclass && (cls->dynamic || (library && library->mutable_prototypes))) {
            // default derived constructors follow the live constructor link; nominal ancestry stays fixed.
            RootFrame observed(1);
            if (!observed.valid()) return ItemError;
            Rooted<Item> prototype(observed, mvp_lmd_get_prototype(held.get()));
            if (item_is_error(prototype.get())) return prototype.get();
            if (!mvp_lmd_is_constructor(prototype.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            self.set(mvp_lmd_class_invoke(prototype.get(), receiver, arguments, argc, target.get()));
            return item_is_error(self.get()) ? self.get() : mvp_lmd_class_initialize_instance(held.get(), self.get());
        }
        while (!mvp_lmd_class_has_constructor(cls) && cls->nominal.base) {
            if (cls->has_instance_elements) {
                MvpLmdClass* base = (MvpLmdClass*)cls->nominal.base->extension_data;
                self.set(mvp_lmd_class_invoke(mvp_lmd_class_values(base)[0], receiver, arguments, argc, target.get()));
                return item_is_error(self.get()) ? self.get() : mvp_lmd_class_initialize_instance(held.get(), self.get());
            }
            cls = (MvpLmdClass*)cls->nominal.base->extension_data;
            held.set(mvp_lmd_class_values(cls)[0]); fn = (MvpLmdCallable*)held.get().function;
            // builtin bases own their allocation and new.target protocol, with no source constructor body.
            if (fn->native_constructor)
                return mvp_lmd_class_invoke(held.get(), self.get(), arguments, argc, target.get());
        }
        MvpLmdClass* actual = mvp_lmd_class_record(new_target);
        if (!actual || get_type_id(new_target) != LMD_TYPE_FUNC) {
            if (!mvp_lmd_is_constructor(target.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            actual = cls;
        }
        if (cls->ast->superclass) {
            self.set(Item{.item = ITEM_JS_TDZ});
        } else {
            if (actual == cls && cls->reusable_layout) reusable = cls;
            self.set(mvp_lmd_object_new(reusable && reusable->allocation_shape
                ? reusable->allocation_shape : &actual->shape, 0));
            if (item_is_error(self.get())) return self.get();
            if (mvp_lmd_is_proxy(target.get()) || (cls->dynamic && target.get().item != held.get().item)) {
                RootFrame observed(1);
                if (!observed.valid()) return ItemError;
                Rooted<Item> prototype(observed, ItemNull);
                prototype.set(mvp_lmd_constructor_prototype(target.get(),
                    mvp_lmd_program_library(fn->program)->values[LMD_LIBRARY_OBJECT_PROTOTYPE]));
                if (item_is_error(prototype.get())) return prototype.get();
                Item linked = mvp_lmd_set_prototype(self.get(), prototype.get());
                if (item_is_error(linked)) return linked;
            }
            if (cls->has_instance_elements) {
                Item initialized = mvp_lmd_class_initialize_instance(held.get(), self.get());
                if (item_is_error(initialized)) return initialized;
            }
            if (!mvp_lmd_class_has_constructor(cls)) return self.get();
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
            self.set(mvp_lmd_object_from_constructor(target.get(), properties));
            if (item_is_error(self.get())) return self.get();
        }
    }
    // the caller roots the borrowed argument span; each MIR activation owns its receiver roots.
    typedef Item (*Entry)(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);
    MvpLmdRealmScope body_realm(fn->program);
    Item result = body_realm.complete(((Entry)fn->ptr)(context, fn->program, arguments, (uint64_t)argc,
        held.get(), self.get(), target.get()));
    result = lambda_item_resolve_pending_slot(result);
    if (item_is_error(result)) return result;
    if (!fn->constructor && !fn->native_constructor && target.get().item != ITEM_JS_UNDEFINED)
        result = mvp_lmd_constructor_result(result, self.get(), false);
    // admitted initializers cannot observe these fields until every store has completed.
    if (reusable && result.item == self.get().item && get_type_id(result) == LMD_TYPE_MAP &&
            typemap_is_shared_shape((TypeMap*)result.map->type))
        reusable->allocation_shape = (TypeMap*)result.map->type;
    return result;
    };
    return realm.complete(invoke());
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
