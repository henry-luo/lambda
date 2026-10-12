#include "mvp_lmd_runtime.h"
#include "../js_runtime.h"
#include "../js_props.h"
#include "../js_object_meta.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/heap_api.h"
#include "../../runtime/runtime-state.h"
#include "../../input/input.hpp"

static Item proxy_trap(Item handler, const char* name) {
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, handler), key(roots, Item{.item = s2it(heap_create_name(name, strlen(name)))});
    Item trap = mvp_lmd_property_with_receiver(object.get(), key.get(), ItemNull, LMD_PROP_GET, object.get());
    if (item_is_error(trap)) return trap;
    if (trap.item == ITEM_NULL || trap.item == ITEM_JS_UNDEFINED) return Item{.item = ITEM_JS_UNDEFINED};
    return mvp_lmd_is_callable(trap) ? trap : mvp_lmd_fail(LMD_MVP_TYPE, 0);
}
static bool proxy_key_equal(Item left, Item right) {
    String* a = left.get_string(); String* b = right.get_string();
    if (property_key_requires_identity(a) || property_key_requires_identity(b))
        return name_ref_id(a) == name_ref_id(b);
    return mvp_lmd_same_value_zero(left, right);
}
Item mvp_lmd_proxy_property(Item owner, Item name, Item input, Item actual_receiver, int operation,
        JsPropertyDescriptor* descriptor) {
    if (lambda_stack_is_exhausted(context->stack_limit)) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
    RootFrame roots(17);
    if (!roots.valid()) return ItemError;
    uint64_t input_home = 0, result_home = 0, current_home = 0, update_home = 0;
    Rooted<Item> object(roots, owner), key(roots, name), receiver(roots, actual_receiver);
    Rooted<Item> value(roots, lambda_item_adopt_scalar_home(input, &input_home));
    Rooted<Item> target(roots, ItemNull), handler(roots, ItemNull), trap(roots, ItemNull), result(roots, ItemNull);
    Rooted<Item> keys(roots, ItemNull), desc_object(roots, ItemNull), observable_key(roots, ItemNull);
    Rooted<Item> current_value(roots, ItemNull), current_getter(roots, ItemNull), current_setter(roots, ItemNull);
    Rooted<Item> update_value(roots, ItemNull), update_getter(roots, ItemNull), update_setter(roots, ItemNull);
    JsPropertyDescriptor update = descriptor ? *descriptor : JsPropertyDescriptor{};
    if (operation == LMD_PROXY_DEFINE) {
        update_value.set(lambda_item_adopt_scalar_home(update.value, &update_home));
        update_getter.set(update.getter); update_setter.set(update.setter); update.value = update_value.get();
    }
    JsProxyData* data = js_get_proxy_data(object.get());
    if (!data) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    if (data->revoked) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    target.set(Item{.item = data->target}); handler.set(Item{.item = data->handler});
    const char* names[] = {"get", "set", "defineProperty", "deleteProperty", "has", "getOwnPropertyDescriptor",
        "ownKeys", "getPrototypeOf", "setPrototypeOf", "isExtensible", "preventExtensions"};
    trap.set(proxy_trap(handler.get(), names[operation]));
    if (item_is_error(trap.get())) return trap.get();
    if (trap.get().item == ITEM_JS_UNDEFINED) {
        switch (operation) {
        case LMD_PROXY_GET: return mvp_lmd_property_with_receiver(target.get(), key.get(), ItemNull, LMD_PROP_GET, receiver.get());
        case LMD_PROXY_SET: return mvp_lmd_set_completion(target.get(), key.get(), value.get(), receiver.get());
        case LMD_PROXY_DEFINE: return mvp_lmd_define_own_property(target.get(), key.get(), &update);
        case LMD_PROXY_DELETE: return mvp_lmd_property_with_receiver(target.get(), key.get(), ItemNull, LMD_PROP_DELETE, target.get());
        case LMD_PROXY_HAS: return mvp_lmd_property_with_receiver(target.get(), key.get(), ItemNull, LMD_PROP_HAS, target.get());
        case LMD_PROXY_DESCRIPTOR: return mvp_lmd_get_own_property(target.get(), key.get(), descriptor);
        case LMD_PROXY_KEYS: return mvp_lmd_object_project(target.get(), 5);
        case LMD_PROXY_PROTOTYPE: return mvp_lmd_get_prototype(target.get());
        case LMD_PROXY_SET_PROTOTYPE: return mvp_lmd_set_prototype(target.get(), key.get());
        case LMD_PROXY_EXTENSIBLE: case LMD_PROXY_PREVENT:
            return mvp_lmd_object_extensible(target.get(), operation == LMD_PROXY_PREVENT);
        }
    }
    if (operation <= LMD_PROXY_DESCRIPTOR) {
        observable_key.set(property_key_requires_identity(key.get().get_string())
            ? mvp_lmd_property_symbol(key.get().get_string()) : key.get());
        if (item_is_error(observable_key.get())) return observable_key.get();
    } else observable_key.set(key.get());
    if (operation == LMD_PROXY_DEFINE) {
        desc_object.set(mvp_lmd_descriptor_object(&update));
        if (item_is_error(desc_object.get())) return desc_object.get();
    }
    Item args[] = {target.get(), observable_key.get(), value.get(), receiver.get()};
    int count = operation <= LMD_PROXY_DESCRIPTOR ? 2 : operation == LMD_PROXY_SET_PROTOTYPE ? 2 : 1;
    if (operation == LMD_PROXY_GET) { args[2] = receiver.get(); count = 3; }
    if (operation == LMD_PROXY_SET) count = 4;
    if (operation == LMD_PROXY_DEFINE) { args[2] = desc_object.get(); count = 3; }
    result.set(lambda_item_adopt_scalar_home(mvp_lmd_class_invoke(trap.get(), handler.get(), args, count,
        Item{.item = ITEM_JS_UNDEFINED}), &result_home));
    if (item_is_error(result.get())) return result.get();
    bool boolean = mvp_lmd_truthy(result.get());
    JsPropertyDescriptor current = {};
    auto read_current = [&]() -> Item {
        Item found = mvp_lmd_get_own_property(target.get(), key.get(), &current);
        if (found.item == ITEM_TRUE) {
            current_value.set(lambda_item_adopt_scalar_home(current.value, &current_home));
            current_getter.set(current.getter); current_setter.set(current.setter);
            current.value = current_value.get();
        }
        return found;
    };
    if (operation == LMD_PROXY_PROTOTYPE || operation == LMD_PROXY_SET_PROTOTYPE ||
            operation == LMD_PROXY_EXTENSIBLE || operation == LMD_PROXY_PREVENT) {
        if (operation == LMD_PROXY_PROTOTYPE && result.get().item != ITEM_NULL && !mvp_lmd_is_object(result.get()))
            return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        if ((operation == LMD_PROXY_SET_PROTOTYPE || operation == LMD_PROXY_PREVENT) && !boolean)
            return Item{.item = ITEM_FALSE};
        Item extensible = mvp_lmd_object_extensible(target.get(), false);
        if (item_is_error(extensible)) return extensible;
        if (operation == LMD_PROXY_EXTENSIBLE)
            return (extensible.item == ITEM_TRUE) == boolean ? Item{.item = b2it(boolean)} : mvp_lmd_fail(LMD_MVP_TYPE, 0);
        if (operation == LMD_PROXY_PREVENT)
            return extensible.item == ITEM_FALSE ? Item{.item = ITEM_TRUE} : mvp_lmd_fail(LMD_MVP_TYPE, 0);
        if (extensible.item == ITEM_FALSE) {
            Item prototype = mvp_lmd_get_prototype(target.get());
            if (item_is_error(prototype)) return prototype;
            if (prototype.item != (operation == LMD_PROXY_PROTOTYPE ? result.get().item : key.get().item))
                return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        }
        return operation == LMD_PROXY_PROTOTYPE ? result.get() : Item{.item = ITEM_TRUE};
    }
    if (operation == LMD_PROXY_KEYS) {
        result.set(mvp_lmd_arguments_list(result.get(), true));
        if (item_is_error(result.get())) return result.get();
        for (int64_t i = 0; i < result.get().array->length; i++) {
            key.set(mvp_lmd_property_key(result.get().array->items[i]));
            if (item_is_error(key.get())) return key.get();
            for (int64_t j = 0; j < i; j++) if (proxy_key_equal(key.get(), result.get().array->items[j]))
                return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            Item stored = mvp_lmd_array_store(result.get(), i, key.get());
            if (item_is_error(stored)) return stored;
        }
        Item extensible = mvp_lmd_object_extensible(target.get(), false);
        if (item_is_error(extensible)) return extensible;
        bool extensible_value = extensible.item == ITEM_TRUE, mismatch = false;
        keys.set(mvp_lmd_object_project(target.get(), 5));
        if (item_is_error(keys.get())) return keys.get();
        for (int64_t i = 0; i < keys.get().array->length; i++) {
            key.set(keys.get().array->items[i]);
            Item found = read_current();
            if (item_is_error(found)) return found;
            bool included = false;
            for (int64_t j = 0; j < result.get().array->length; j++)
                if (proxy_key_equal(key.get(), result.get().array->items[j])) { included = true; break; }
            // enumerate every target descriptor before deciding the invariant failure.
            mismatch |= !included && (!extensible_value || (found.item == ITEM_TRUE && !js_pd_is_configurable(&current)));
        }
        if (mismatch || (!extensible_value && keys.get().array->length != result.get().array->length))
            return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        return result.get();
    }
    if ((operation == LMD_PROXY_SET || operation == LMD_PROXY_DEFINE || operation == LMD_PROXY_DELETE) && !boolean)
        return Item{.item = ITEM_FALSE};
    if (operation == LMD_PROXY_HAS && boolean) return Item{.item = ITEM_TRUE};
    if (operation == LMD_PROXY_DESCRIPTOR && result.get().item != ITEM_JS_UNDEFINED && !mvp_lmd_is_object(result.get()))
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    Item found = read_current();
    if (item_is_error(found)) return found;
    bool exists = found.item == ITEM_TRUE;
    if (operation == LMD_PROXY_GET || operation == LMD_PROXY_SET) {
        if (exists && !js_pd_is_configurable(&current)) {
            if (js_pd_is_data(&current) && !(current.flags & JS_PD_WRITABLE) &&
                    !mvp_lmd_same_value(current.value, operation == LMD_PROXY_GET ? result.get() : value.get()))
                return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            if (js_pd_is_accessor(&current) && (operation == LMD_PROXY_GET ? current.getter : current.setter).item == ITEM_JS_UNDEFINED &&
                    (operation == LMD_PROXY_SET || result.get().item != ITEM_JS_UNDEFINED)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        }
        return operation == LMD_PROXY_SET ? Item{.item = ITEM_TRUE} :
            lambda_item_uses_scalar_home(result.get()) ? push_d(it2d(result.get())) : result.get();
    }
    if (operation == LMD_PROXY_HAS || operation == LMD_PROXY_DELETE ||
            (operation == LMD_PROXY_DESCRIPTOR && result.get().item == ITEM_JS_UNDEFINED)) {
        if (exists) {
            if (!js_pd_is_configurable(&current)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            Item extensible = mvp_lmd_object_extensible(target.get(), false);
            if (item_is_error(extensible)) return extensible;
            if (extensible.item == ITEM_FALSE) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        }
        return Item{.item = b2it(operation == LMD_PROXY_DELETE)};
    }
    Item extensible = mvp_lmd_object_extensible(target.get(), false);
    if (item_is_error(extensible)) return extensible;
    if (operation == LMD_PROXY_DESCRIPTOR) {
        Item status = mvp_lmd_descriptor_from_object(result.get(), &update, &update_home);
        if (item_is_error(status)) return status;
        // CompletePropertyDescriptor fills only absent members; accessor undefined remains explicit.
        if (js_pd_is_accessor(&update)) {
            if (!(update.flags & JS_PD_HAS_GET)) update.getter.item = ITEM_JS_UNDEFINED;
            if (!(update.flags & JS_PD_HAS_SET)) update.setter.item = ITEM_JS_UNDEFINED;
            update.flags |= JS_PD_HAS_GET | JS_PD_HAS_SET;
        } else {
            if (!(update.flags & JS_PD_HAS_VALUE)) update.value.item = ITEM_JS_UNDEFINED;
            update.flags |= JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE;
        }
        update.flags |= JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
    }
    bool protected_update = (update.flags & JS_PD_HAS_CONFIGURABLE) && !js_pd_is_configurable(&update);
    if (!mvp_lmd_descriptor_compatible(extensible.item == ITEM_TRUE, &update, exists ? &current : NULL) ||
            (protected_update && (!exists || js_pd_is_configurable(&current))) ||
            (exists && !js_pd_is_configurable(&current) && js_pd_is_data(&current) && (current.flags & JS_PD_WRITABLE) &&
                (update.flags & JS_PD_HAS_WRITABLE) && !(update.flags & JS_PD_WRITABLE)))
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (operation == LMD_PROXY_DESCRIPTOR) {
        if (lambda_item_uses_scalar_home(update.value)) update.value = push_d(it2d(update.value));
        *descriptor = update;
    }
    return Item{.item = ITEM_TRUE};
}
Item mvp_lmd_proxy_invoke(Item owner, Item actual_receiver, Item* args, int64_t count, Item new_target) {
    if (lambda_stack_is_exhausted(context->stack_limit)) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
    RootFrame roots(7);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner), receiver(roots, actual_receiver), constructor(roots, new_target);
    Rooted<Item> target(roots, ItemNull), handler(roots, ItemNull), trap(roots, ItemNull), arguments(roots, ItemNull);
    JsProxyData* data = js_get_proxy_data(object.get());
    bool construct = new_target.item != ITEM_JS_UNDEFINED;
    if (!data || data->revoked || !(construct ? data->constructable : data->callable)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    target.set(Item{.item = data->target}); handler.set(Item{.item = data->handler});
    trap.set(proxy_trap(handler.get(), construct ? "construct" : "apply"));
    if (item_is_error(trap.get())) return trap.get();
    if (trap.get().item == ITEM_JS_UNDEFINED)
        return mvp_lmd_class_invoke(target.get(), receiver.get(), args, count, constructor.get());
    arguments.set(mvp_lmd_array_new(0));
    if (item_is_error(arguments.get())) return arguments.get();
    for (int64_t i = 0; i < count; i++) {
        Item stored = mvp_lmd_array_store(arguments.get(), i, args[i]);
        if (item_is_error(stored)) return stored;
    }
    Item passed[] = {target.get(), construct ? arguments.get() : receiver.get(), construct ? constructor.get() : arguments.get()};
    Item result = mvp_lmd_class_invoke(trap.get(), handler.get(), passed, 3, Item{.item = ITEM_JS_UNDEFINED});
    return item_is_error(result) || !construct || mvp_lmd_is_object(result) ? result : mvp_lmd_fail(LMD_MVP_TYPE, 0);
}
Item mvp_lmd_proxy_project(Item owner, int64_t projection) {
    RootFrame roots(6);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner), keys(roots, mvp_lmd_proxy_property(owner, ItemNull, ItemNull, owner, LMD_PROXY_KEYS));
    Rooted<Item> result(roots, ItemNull), key(roots, ItemNull), value(roots, ItemNull), pair(roots, ItemNull);
    if (item_is_error(keys.get()) || projection == 5) return keys.get();
    result.set(mvp_lmd_array_new(0));
    if (item_is_error(result.get())) return result.get();
    uint64_t home = 0;
    for (int64_t i = 0; i < keys.get().array->length; i++) {
        key.set(keys.get().array->items[i]);
        if (projection != 4 && property_key_requires_identity(key.get().get_string())) continue;
        if (projection < 3 || projection == 4) {
            JsPropertyDescriptor descriptor = {};
            Item status = mvp_lmd_get_own_property(object.get(), key.get(), &descriptor);
            if (item_is_error(status)) return status;
            if (status.item == ITEM_FALSE || !(descriptor.flags & JS_PD_ENUMERABLE)) continue;
        }
        value.set(projection == 1 || projection == 2 ? lambda_item_adopt_scalar_home(
            mvp_lmd_property_with_receiver(object.get(), key.get(), ItemNull, LMD_PROP_GET, object.get()), &home) : key.get());
        if (item_is_error(value.get())) return value.get();
        if (projection == 2) {
            pair.set(mvp_lmd_array_new(0));
            if (item_is_error(pair.get())) return pair.get();
            Item status = mvp_lmd_array_store(pair.get(), 0, key.get());
            if (!item_is_error(status)) status = mvp_lmd_array_store(pair.get(), 1, value.get());
            if (item_is_error(status)) return status;
            value.set(pair.get());
        }
        Item status = mvp_lmd_array_store(result.get(), result.get().array->length, value.get());
        if (item_is_error(status)) return status;
    }
    return result.get();
}
template<int operation>
static JsPropertyOpResult proxy_adapter(Item owner, uint64_t, Item key, Item value, Item receiver, Item input) {
    if (!mvp_lmd_program_library()) return {JS_PROPERTY_OP_COMPLETE, mvp_lmd_fail(LMD_MVP_CAPABILITY, 0)};
    RootFrame roots(5);
    if (!roots.valid()) return {JS_PROPERTY_OP_COMPLETE, ItemError};
    uint64_t value_home = 0;
    Rooted<Item> object(roots, owner), name(roots, key), actual_receiver(roots, receiver), attributes(roots, input);
    Rooted<Item> argument(roots, lambda_item_adopt_scalar_home(value, &value_home));
    Item result = ItemNull;
    if (operation <= LMD_PROXY_DESCRIPTOR) {
        name.set(mvp_lmd_property_key(name.get()));
        if (item_is_error(name.get())) return {JS_PROPERTY_OP_COMPLETE, name.get()};
    }
    if (operation == LMD_PROXY_DEFINE) {
        JsPropertyDescriptor descriptor = {}; uint64_t home = 0;
        result = mvp_lmd_descriptor_from_object(attributes.get(), &descriptor, &home);
        if (!item_is_error(result)) result = mvp_lmd_proxy_property(object.get(), name.get(), argument.get(), actual_receiver.get(), operation, &descriptor);
    } else if (operation == LMD_PROXY_DESCRIPTOR) result = mvp_lmd_get_own_descriptor(object.get(), name.get());
    else if (operation == LMD_PROXY_KEYS) result = mvp_lmd_own_keys(object.get());
    else result = mvp_lmd_proxy_property(object.get(), name.get(), argument.get(), actual_receiver.get(), operation);
    return {JS_PROPERTY_OP_COMPLETE, result};
}
static const JsPropertyOps proxy_ops = {
    proxy_adapter<LMD_PROXY_GET>, proxy_adapter<LMD_PROXY_SET>, proxy_adapter<LMD_PROXY_DEFINE>,
    proxy_adapter<LMD_PROXY_DELETE>, proxy_adapter<LMD_PROXY_HAS>, proxy_adapter<LMD_PROXY_DESCRIPTOR>,
    proxy_adapter<LMD_PROXY_KEYS>, proxy_adapter<LMD_PROXY_PROTOTYPE>, proxy_adapter<LMD_PROXY_SET_PROTOTYPE>,
    proxy_adapter<LMD_PROXY_EXTENSIBLE>, proxy_adapter<LMD_PROXY_PREVENT>
};
static const JsClassMeta proxy_meta = {JS_CLASS_PROXY, JS_CLASS_FAMILY_PROXY,
    JS_CLASS_FLAG_EXOTIC_PROPERTIES | JS_CLASS_FLAG_NATIVE_PAYLOAD, JS_PROTO_POLICY_EXOTIC, &proxy_ops};
static Item proxy_revoke(Context*, MvpLmdProgram*, Item*, uint64_t, Item self, Item, Item) {
    Item* env = (Item*)self.function->closure_env;
    if (env[0].item != ITEM_NULL) {
        JsProxyData* data = js_get_proxy_data(env[0]);
        if (data) { data->revoked = true; data->target = data->handler = data->private_slots = ITEM_NULL; }
        env[0] = ItemNull;
    }
    return Item{.item = ITEM_JS_UNDEFINED};
}
static Item proxy_construct(Context*, MvpLmdProgram* program, Item* args, uint64_t count, Item self, Item, Item new_target) {
    bool revocable = (intptr_t)((MvpLmdNativeCallable*)self.function)->state != 0;
    if ((!revocable && new_target.item == ITEM_JS_UNDEFINED) || count < 2 ||
            !mvp_lmd_is_object(args[0]) || !mvp_lmd_is_object(args[1])) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    MvpLmdLibraryState* state = mvp_lmd_program_library(program);
    Rooted<Item> proxy(roots, js_proxy_carrier_new(args[0], args[1], &state->proxy_shape,
        mvp_lmd_is_callable(args[0]), mvp_lmd_is_constructor(args[0])));
    Rooted<Item> revoke(roots, ItemNull), result(roots, ItemNull);
    if (item_is_error(proxy.get()) || !revocable) return proxy.get();
    revoke.set(mvp_lmd_native_function(program, NULL, proxy_revoke, 0));
    if (item_is_error(revoke.get())) return revoke.get();
    Item* env = (Item*)heap_calloc_closure_env(sizeof(Item));
    if (!env) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    env[0] = proxy.get(); revoke.get().function->closure_env = env; revoke.get().function->closure_field_count = 1;
    result.set(mvp_lmd_object_new(state->object_shape, 0));
    if (item_is_error(result.get())) return result.get();
    Item stored = mvp_lmd_named_set(result.get(), "proxy", proxy.get());
    if (!item_is_error(stored)) stored = mvp_lmd_named_set(result.get(), "revoke", revoke.get());
    return item_is_error(stored) ? stored : result.get();
}
Item mvp_lmd_proxy_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state) {
    if (!state->mutable_prototypes) return ItemNull;
    state->proxy_shape = *state->object_shape;
    state->proxy_shape.nominal = NULL; state->proxy_shape.is_nominal = false; state->proxy_shape.js_meta = &proxy_meta;
    Item& constructor = state->values[LMD_LIBRARY_PROXY];
    constructor = mvp_lmd_native_function(program, NULL, proxy_construct, 2);
    if (item_is_error(constructor)) return constructor;
    ((MvpLmdCallable*)constructor.function)->native_constructor = true;
    Item ready = mvp_lmd_function_prepare(constructor);
    if (item_is_error(ready)) return ready;
    return mvp_lmd_named_set(constructor, "revocable", mvp_lmd_native_function(program, (void*)1, proxy_construct, 2));
}
