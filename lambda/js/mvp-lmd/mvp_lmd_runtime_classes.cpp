#include "mvp_lmd_runtime.h"
#include "../js_ast.hpp"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/heap_api.h"
#include "../../runtime/runtime-state.h"
#include "../../runtime/lambda-error.h"

// class semantics live separately so new lookup sites do not reshape ordinary-object hot helpers.

MvpLmdClass* mvp_lmd_class_record(Item owner) {
    if (get_type_id(owner) == LMD_TYPE_FUNC) {
        Function* fn = owner.function;
        return fn->entry_abi == FN_ENTRY_ABI_MVP_LMD && fn->requires_runtime_context &&
            ((MvpLmdCallable*)fn)->constructor ? ((MvpLmdCallable*)fn)->home : NULL;
    }
    if (get_type_id(owner) != LMD_TYPE_MAP) return NULL;
    TypeNominal* nominal = ((TypeMap*)owner.map->type)->nominal;
    return nominal && nominal->extension == &mvp_lmd_class_extension
        ? (MvpLmdClass*)nominal->extension_data : NULL;
}
static Item mvp_lmd_inherited_member(Item owner, const char* key, size_t length, bool* found) {
    *found = false;
    MvpLmdClass* cls = mvp_lmd_class_record(owner);
    if (!cls) return Item{.item = ITEM_JS_UNDEFINED};
    bool statics = get_type_id(owner) == LMD_TYPE_FUNC || owner.map == cls->values[2].map;
    TypeNominal* nominal = &cls->nominal;
    if (statics || owner.map == cls->values[1].map) nominal = nominal->base;
    for (; nominal; nominal = nominal->base) {
        if (nominal->extension != &mvp_lmd_class_extension) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        MvpLmdClass* current = (MvpLmdClass*)nominal->extension_data;
        Map* properties = current->values[statics ? 2 : 1].map;
        ShapeEntry* field = typemap_hash_lookup((TypeMap*)properties->type, key, (int)length);
        if (field) { *found = true; return map_shape_field_to_item(properties->data, field); }
    }
    return Item{.item = ITEM_JS_UNDEFINED};
}
const TypeNominalExtension mvp_lmd_class_extension = {mvp_lmd_inherited_member};

static void mvp_lmd_cache_property(MvpLmdPropertyCache* cache, TypeMap* shape,
        ShapeEntry* field, Item inherited, bool writable) {
    MvpLmdPropertyCacheEntry* entry = NULL;
    for (int i = 0; i < MVP_LMD_PROPERTY_CACHE_SIZE; i++)
        if (cache->entries[i].shape == shape) { entry = &cache->entries[i]; break; }
    if (!entry) entry = &cache->entries[cache->next++ % MVP_LMD_PROPERTY_CACHE_SIZE];
    entry->shape = shape; entry->field = field;
    entry->inherited = field ? ItemNull : inherited; entry->writable = writable;
    // only ordinary packed fields use the inline cache lane; shared readers own optional layouts.
    entry->offset = field ? field->byte_offset : 0;
    entry->storage = field && field->name && field->byte_offset >= 0 &&
        !(field->flags & JSPD_IS_ACCESSOR) && !shape_entry_uses_native_lane(field, NULL)
        ? shape_entry_storage_type_id(field) : LMD_TYPE_ANY;
    TypeId type = entry->storage;
    entry->pointer_lane = type == LMD_TYPE_STRING ? 1 : type == LMD_TYPE_FUNC ||
        type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY || type == LMD_TYPE_ARRAY_NUM ? 2 : 0;
    entry->pointer_tag = type == LMD_TYPE_FUNC || type == LMD_TYPE_STRING ? (uint64_t)type << 56 : 0;
}

extern "C" Item mvp_lmd_class_property(Item owner, Item name, Item value, int64_t operation,
        MvpLmdPropertyCache* cache) {
    MvpLmdClass* cls = mvp_lmd_class_record(owner);
    if (cls) {
        bool constructor = get_type_id(owner) == LMD_TYPE_FUNC;
        Map* map = constructor ? cls->values[2].map : owner.map;
        bool metadata = constructor || map == cls->values[1].map || map == cls->values[2].map;
        if (metadata && (operation == LMD_PROP_SET || operation == LMD_PROP_DELETE))
            return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        if (metadata && operation >= LMD_PROP_KEYS) {
            // class metadata has only nonenumerable properties in this bounded phase.
            Array* empty = array();
            return empty ? Item{.array = empty} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        }
        if (operation == LMD_PROP_GET || operation == LMD_PROP_CALLEE ||
                operation == LMD_PROP_OWN || operation == LMD_PROP_HAS) {
            String* key = name.get_string();
            ShapeEntry* field = typemap_hash_lookup((TypeMap*)map->type, key->chars, key->len);
            bool found = field != NULL;
            Item result = found ? map_shape_field_to_item(map->data, field) : Item{.item = ITEM_JS_UNDEFINED};
            if (!found && operation != LMD_PROP_OWN)
                result = mvp_lmd_inherited_member(owner, key->chars, key->len, &found);
            if (item_is_error(result)) return result;
            // immutable shapes invalidate on shadowing/retyping; methods remain program-rooted.
            TypeMap* shape = (TypeMap*)map->type;
            if (cache && !constructor && found &&
                    (typemap_is_shared_shape(shape) || shape == &cls->shape) &&
                    (field || get_type_id(result) == LMD_TYPE_FUNC)) {
                mvp_lmd_cache_property(cache, shape, field, result, !metadata);
            }
            if (operation == LMD_PROP_OWN) return Item{.item = b2it(found)};
            if (found) return operation == LMD_PROP_HAS ? Item{.item = ITEM_TRUE} : result;
        }
        if (cache && operation == LMD_PROP_SET && !metadata && typemap_is_shared_shape((TypeMap*)map->type)) {
            ShapeEntry* field = typemap_hash_lookup((TypeMap*)map->type, name.get_string()->chars, name.get_string()->len);
            if (field) {
                mvp_lmd_cache_property(cache, (TypeMap*)map->type, field, ItemNull, true);
            }
        }
        owner = Item{.map = map};
    }
    switch (operation) {
    case LMD_PROP_GET: case LMD_PROP_CALLEE:
        return mvp_lmd_property_get(owner, name, operation == LMD_PROP_CALLEE);
    case LMD_PROP_SET: return mvp_lmd_property_set(owner, name, value);
    case LMD_PROP_DELETE: return mvp_lmd_property_delete(owner, name);
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
    return base->values[constructor ? 0 : fn->static_method ? 2 : 1];
}
extern "C" Item mvp_lmd_constructor_result(Item value, Item receiver, int64_t derived) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY || type == LMD_TYPE_ARRAY_NUM ||
            type == LMD_TYPE_FUNC || type == LMD_TYPE_ERROR) return value;
    if (derived && value.item != ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    return receiver.item == ITEM_JS_TDZ ? mvp_lmd_fail(LMD_MVP_REFERENCE, 0) : receiver;
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
            cls = (MvpLmdClass*)cls->nominal.base->extension_data;
            held.set(cls->values[0]); fn = (MvpLmdCallable*)held.get().function;
        }
        MvpLmdClass* actual = mvp_lmd_class_record(new_target);
        if (!actual || get_type_id(new_target) != LMD_TYPE_FUNC) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        if (cls->nominal.base) {
            self.set(Item{.item = ITEM_JS_TDZ});
        } else {
            if (actual == cls && cls->reusable_layout) reusable = cls;
            self.set(mvp_lmd_object_new(reusable && reusable->allocation_shape
                ? reusable->allocation_shape : &actual->shape, 0));
            if (item_is_error(self.get()) || cls->constructor_id < 0) return self.get();
        }
    } else if (new_target.item != ITEM_JS_UNDEFINED) {
        const AstFuncNode* ast = (const AstFuncNode*)fn->def;
        return mvp_lmd_fail(ast && (ast->node_type == AST_NODE_FUNC_EXPR || ast->node_type == AST_NODE_FUNC)
            ? LMD_MVP_CAPABILITY : LMD_MVP_TYPE, 0);
    }
    // the caller roots the borrowed argument span; each MIR activation owns its receiver roots.
    typedef Item (*Entry)(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);
    Item result = ((Entry)fn->ptr)(context, fn->program, arguments, (uint64_t)argc,
        held.get(), self.get(), target.get());
    // admitted initializers cannot observe these fields until every store has completed.
    if (reusable && result.item == self.get().item && get_type_id(result) == LMD_TYPE_MAP &&
            typemap_is_shared_shape((TypeMap*)result.map->type))
        reusable->allocation_shape = (TypeMap*)result.map->type;
    return result;
}
extern "C" Item mvp_lmd_instanceof(Item value, Item constructor) {
    MvpLmdClass* cls = mvp_lmd_class_record(constructor);
    if (!cls || get_type_id(constructor) != LMD_TYPE_FUNC)
        return mvp_lmd_fail(get_type_id(constructor) == LMD_TYPE_FUNC ? LMD_MVP_CAPABILITY : LMD_MVP_TYPE, 0);
    // fixed class-created links make the shared ancestry predicate exact for admitted instances.
    MvpLmdClass* actual = mvp_lmd_class_record(value);
    bool result = actual && get_type_id(value) == LMD_TYPE_MAP &&
        value.map != actual->values[1].map && value.map != actual->values[2].map &&
        lambda_nominal_derives_from(&actual->nominal, &cls->nominal);
    if (actual && get_type_id(value) == LMD_TYPE_MAP && value.map == actual->values[1].map)
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
