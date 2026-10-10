#include "mvp_lmd_runtime.h"
#include "../js_ast.hpp"
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
static Item mvp_lmd_inherited_member_flags(Item owner, const char* key, size_t length, bool* found, uint8_t* flags, NameRef identity = NULL) {
    *found = false;
    MvpLmdClass* cls = mvp_lmd_class_record(owner);
    if (!cls) return Item{.item = ITEM_JS_UNDEFINED};
    bool statics = get_type_id(owner) == LMD_TYPE_FUNC || owner.map == mvp_lmd_class_values(cls)[2].map;
    TypeNominal* nominal = &cls->nominal;
    // intrinsic instance ancestry does not make Object's static methods constructor ancestors.
    if (statics) nominal = cls->ast ? nominal->base : NULL;
    else if (owner.map == mvp_lmd_class_values(cls)[1].map) nominal = nominal->base;
    for (; nominal; nominal = nominal->base) {
        if (nominal->extension != &mvp_lmd_class_extension) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        MvpLmdClass* current = (MvpLmdClass*)nominal->extension_data;
        Item prototype = mvp_lmd_class_values(current)[statics ? 2 : 1];
        if (prototype.item == ITEM_NULL) continue;
        Map* properties = prototype.map;
        ShapeEntry* field = identity ? typemap_hash_lookup_key((TypeMap*)properties->type, identity) :
            typemap_hash_lookup((TypeMap*)properties->type, key, (int)length);
        if (field) {
            *found = true;
            if (flags) { *flags = field->flags; return ItemNull; }
            if (field->flags & JSPD_IS_ACCESSOR) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
            return map_shape_field_to_item(properties->data, field);
        }
    }
    return Item{.item = ITEM_JS_UNDEFINED};
}
static Item mvp_lmd_inherited_member(Item owner, const char* key, size_t length, bool* found) {
    return mvp_lmd_inherited_member_flags(owner, key, length, found, NULL);
}
static Item mvp_lmd_nominal_owner(const void* data) {
    const MvpLmdClass* cls = (const MvpLmdClass*)data;
    return cls->owner ? Item{.function = cls->owner} : ItemNull;
}
const TypeNominalExtension mvp_lmd_class_extension = {mvp_lmd_inherited_member, mvp_lmd_nominal_owner};

static void mvp_lmd_cache_property(MvpLmdPropertyCache* cache, TypeMap* shape,
        ShapeEntry* field, Item inherited, bool writable, bool nullable_initializer = false) {
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

extern "C" Item mvp_lmd_class_property(Item owner, Item name, Item value, int64_t operation,
        MvpLmdPropertyCache* cache) {
    bool strict = (operation & LMD_PROP_STRICT) != 0;
    operation &= ~LMD_PROP_STRICT;
    bool initializing = operation == LMD_PROP_INITIALIZE;
    if (initializing) operation = LMD_PROP_SET;
    if (get_type_id(owner) == LMD_TYPE_FUNC && !mvp_lmd_class_record(owner)) {
        if (operation == LMD_PROP_GET || operation == LMD_PROP_CALLEE || operation == LMD_PROP_HAS) {
            Item method = mvp_lmd_function_method(owner, name.get_string());
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
        if (metadata && (operation == LMD_PROP_SET || operation == LMD_PROP_DELETE)) {
            String* key = name.get_string();
            ShapeEntry* field = typemap_hash_lookup_key((TypeMap*)map->type, key);
            if ((!constructor && !cls->owner) ||
                    (constructor && field && (field->flags & JSPD_NON_ENUMERABLE)))
                return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        }
        if (metadata && operation >= LMD_PROP_KEYS) {
            return mvp_lmd_object_project(Item{.map = map}, operation - LMD_PROP_KEYS);
        }
        if (operation == LMD_PROP_GET || operation == LMD_PROP_CALLEE ||
                operation == LMD_PROP_OWN || operation == LMD_PROP_HAS) {
            String* key = name.get_string();
            ShapeEntry* field = typemap_hash_lookup_key((TypeMap*)map->type, key);
            bool found = field != NULL;
            if (field && (field->flags & JSPD_IS_ACCESSOR) &&
                    (operation == LMD_PROP_GET || operation == LMD_PROP_CALLEE)) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
            bool presence = operation == LMD_PROP_OWN || operation == LMD_PROP_HAS;
            Item result = found && !presence ? map_shape_field_to_item(map->data, field) : Item{.item = ITEM_JS_UNDEFINED};
            if (!found && operation != LMD_PROP_OWN) {
                uint8_t flags;
                result = mvp_lmd_inherited_member_flags(owner, key->chars, key->len, &found, presence ? &flags : NULL, key);
            }
            if (!found && constructor && operation != LMD_PROP_OWN) {
                result = mvp_lmd_function_method(owner, key);
                found = result.item != ITEM_JS_UNDEFINED;
            }
            if (item_is_error(result)) return result;
            // immutable shapes invalidate on shadowing/retyping; methods remain program-rooted.
            TypeMap* shape = (TypeMap*)map->type;
            if (cache && !presence && !cls->owner && !constructor && found &&
                    (typemap_is_shared_shape(shape) || shape == &cls->shape) &&
                    (field || get_type_id(result) == LMD_TYPE_FUNC)) {
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
            uint8_t flags = field ? field->flags : 0;
            if (!field) {
                bool found;
                Item inherited = mvp_lmd_inherited_member_flags(owner, name.get_string()->chars,
                    name.get_string()->len, &found, &flags, name.get_string());
                if (item_is_error(inherited)) return inherited;
            }
            if (flags & (JSPD_IS_ACCESSOR | JSPD_NON_WRITABLE))
                return flags & JSPD_IS_ACCESSOR ? mvp_lmd_fail(LMD_MVP_CAPABILITY, 0) :
                    strict ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : value;
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

extern "C" Item mvp_lmd_class_super(Item function, int64_t constructor) {
    if (get_type_id(function) != LMD_TYPE_FUNC ||
            function.function->entry_abi != FN_ENTRY_ABI_MVP_LMD || !function.function->requires_runtime_context)
        return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    MvpLmdCallable* fn = (MvpLmdCallable*)function.function;
    if (!fn->home || !fn->home->nominal.base) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    MvpLmdClass* base = (MvpLmdClass*)fn->home->nominal.base->extension_data;
    return mvp_lmd_class_values(base)[constructor ? 0 : fn->static_method ? 2 : 1];
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
        Item stored = mvp_lmd_property_set(object.get(), key, value.get());
        if (item_is_error(stored)) return stored;
    }
    return object.get();
}
extern "C" Item mvp_lmd_class_invoke(Item callee, Item receiver, Item* arguments,
        int64_t argc, Item new_target) {
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
        if (!actual || get_type_id(new_target) != LMD_TYPE_FUNC) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        if (cls->nominal.base) {
            self.set(Item{.item = ITEM_JS_TDZ});
        } else {
            if (actual == cls && cls->reusable_layout) reusable = cls;
            self.set(mvp_lmd_object_new(reusable && reusable->allocation_shape
                ? reusable->allocation_shape : &actual->shape, 0));
            if (item_is_error(self.get())) return self.get();
            if (cls->initializer_count) {
                Item initialized = mvp_lmd_class_initialize_instance(held.get(), self.get());
                if (item_is_error(initialized)) return initialized;
            }
            if (cls->constructor_id < 0) return self.get();
        }
    } else if (new_target.item != ITEM_JS_UNDEFINED && fn->native_constructor) {
        if (new_target.item != callee.item) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    } else if (new_target.item != ITEM_JS_UNDEFINED) {
        if (fn->non_constructible) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        const AstFuncNode* ast = (const AstFuncNode*)fn->def;
        if (!ast || (ast->node_type != AST_NODE_FUNC_EXPR && ast->node_type != AST_NODE_FUNC))
            return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        Item prepared = mvp_lmd_function_prepare(held.get());
        if (item_is_error(prepared)) return prepared;
        fn = (MvpLmdCallable*)held.get().function;
        if (new_target.item != held.get().item) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        self.set(mvp_lmd_object_new(&fn->properties->shape, 0));
        if (item_is_error(self.get())) return self.get();
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
extern "C" Item mvp_lmd_instanceof(Item value, Item constructor) {
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
    if (library && get_type_id(value) == LMD_TYPE_ARRAY) {
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
