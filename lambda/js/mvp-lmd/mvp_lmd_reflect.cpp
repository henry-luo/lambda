#include "mvp_lmd_runtime.h"
#include "../js_ast.hpp"
#include "../js_props.h"
#include "../js_well_known_names.h"
#include "../js_runtime.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/heap_api.h"
#include "../../input/input.hpp"
#include <math.h>

bool mvp_lmd_is_callable(Item value) {
    if (mvp_lmd_is_proxy(value)) {
        JsProxyData* data = js_get_proxy_data(value);
        return data && data->callable;
    }
    if (get_type_id(value) != LMD_TYPE_FUNC) return false;
    return value.function->entry_abi == FN_ENTRY_ABI_MVP_LMD ||
        (function_has_abi(value.function, FN_ENTRY_ABI_JS_FUNCTION) && js_has_call_capability(value));
}
bool mvp_lmd_is_constructor(Item value) {
    if (mvp_lmd_is_proxy(value)) {
        JsProxyData* data = js_get_proxy_data(value);
        return data && data->constructable;
    }
    if (get_type_id(value) == LMD_TYPE_FUNC && function_has_abi(value.function, FN_ENTRY_ABI_JS_FUNCTION))
        return js_is_constructor(value).item == ITEM_TRUE;
    if (!mvp_lmd_is_callable(value) || !value.function->requires_runtime_context) return false;
    MvpLmdCallable* callable = (MvpLmdCallable*)value.function;
    if (callable->non_constructible) return false;
    if (callable->constructor || callable->native_constructor) return true;
    AstFuncNode* ast = (AstFuncNode*)callable->def;
    return ast && (ast->node_type == AST_NODE_FUNC || ast->node_type == AST_NODE_FUNC_EXPR);
}
Item mvp_lmd_arguments_list(Item source, bool property_keys) {
    if (!mvp_lmd_is_object(source)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, source), result(roots, ItemNull), key(roots, ItemNull), value(roots, ItemNull);
    value.set(mvp_lmd_sequence_length(object.get()));
    if (item_is_error(value.get())) return value.get();
    uint64_t length = it2i(value.get());
    result.set(mvp_lmd_array_new(0));
    if (item_is_error(result.get())) return result.get();
    for (uint32_t i = 0; i < (uint64_t)length; i++) {
        key.set(mvp_lmd_number_to_string(i));
        if (item_is_error(key.get())) return key.get();
        value.set(mvp_lmd_property_with_receiver(object.get(), key.get(), ItemNull, LMD_PROP_GET, object.get()));
        if (item_is_error(value.get())) return value.get();
        if (property_keys && get_type_id(value.get()) != LMD_TYPE_STRING && get_type_id(value.get()) != LMD_TYPE_SYMBOL)
            return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        Item stored = mvp_lmd_array_store(result.get(), i, value.get());
        if (item_is_error(stored)) return stored;
    }
    return result.get();
}
Item mvp_lmd_own_keys(Item owner) {
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> result(roots, mvp_lmd_object_project(owner, 5)), value(roots, ItemNull);
    if (item_is_error(result.get())) return result.get();
    for (int64_t i = 0; i < result.get().array->length; i++) {
        Item name = result.get().array->items[i];
        if (property_key_requires_identity(name.get_string())) {
            value.set(mvp_lmd_property_symbol(name.get_string()));
            if (item_is_error(value.get())) return value.get();
            Item stored = mvp_lmd_array_store(result.get(), i, value.get());
            if (item_is_error(stored)) return stored;
        }
    }
    return result.get();
}
enum ReflectOperation { REFLECT_APPLY, REFLECT_CONSTRUCT, REFLECT_DEFINE, REFLECT_DELETE,
    REFLECT_GET, REFLECT_DESCRIPTOR, REFLECT_PROTOTYPE, REFLECT_HAS, REFLECT_EXTENSIBLE,
    REFLECT_KEYS, REFLECT_PREVENT, REFLECT_SET, REFLECT_SET_PROTOTYPE };
static Item reflect_operation(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item) {
    int operation = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    Item absent = {.item = ITEM_JS_UNDEFINED};
    RootFrame roots(6);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, count ? args[0] : absent), key(roots, count > 1 ? args[1] : absent);
    Rooted<Item> value(roots, count > 2 ? args[2] : absent), receiver(roots, count > 3 ? args[3] : object.get());
    Rooted<Item> result(roots, ItemNull), arguments(roots, ItemNull);
    if (!mvp_lmd_is_object(object.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (operation == REFLECT_APPLY || operation == REFLECT_CONSTRUCT) {
        bool construct = operation == REFLECT_CONSTRUCT;
        receiver.set(construct ? count > 2 ? value.get() : object.get() : key.get());
        if (construct ? !mvp_lmd_is_constructor(object.get()) || !mvp_lmd_is_constructor(receiver.get()) :
                !mvp_lmd_is_callable(object.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        arguments.set(mvp_lmd_arguments_list(construct ? key.get() : value.get()));
        return item_is_error(arguments.get()) ? arguments.get() : mvp_lmd_call_array(object.get(),
            construct ? absent : receiver.get(), arguments.get(), construct ? receiver.get() : absent);
    }
    switch (operation) {
    case REFLECT_PROTOTYPE: return mvp_lmd_get_prototype(object.get());
    case REFLECT_SET_PROTOTYPE: return mvp_lmd_set_prototype(object.get(), key.get());
    case REFLECT_EXTENSIBLE: case REFLECT_PREVENT:
        return mvp_lmd_object_extensible(object.get(), operation == REFLECT_PREVENT);
    case REFLECT_KEYS:
        return mvp_lmd_own_keys(object.get());
    default: break;
    }
    key.set(mvp_lmd_property_key(key.get()));
    if (item_is_error(key.get())) return key.get();
    switch (operation) {
    case REFLECT_GET:
        return mvp_lmd_property_with_receiver(object.get(), key.get(), ItemNull, LMD_PROP_GET, count > 2 ? value.get() : object.get());
    case REFLECT_SET: return mvp_lmd_set_completion(object.get(), key.get(), value.get(), receiver.get());
    case REFLECT_DELETE: return mvp_lmd_property_with_receiver(object.get(), key.get(), ItemNull, LMD_PROP_DELETE, object.get());
    case REFLECT_HAS: return mvp_lmd_property_with_receiver(object.get(), key.get(), ItemNull, LMD_PROP_HAS, object.get());
    case REFLECT_DESCRIPTOR: return mvp_lmd_get_own_descriptor(object.get(), key.get());
    case REFLECT_DEFINE: {
        JsPropertyDescriptor descriptor = {}; uint64_t home = 0;
        Item status = mvp_lmd_descriptor_from_object(value.get(), &descriptor, &home);
        return item_is_error(status) ? status : mvp_lmd_define_own_property(object.get(), key.get(), &descriptor);
    }
    default: return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    }
}
Item mvp_lmd_reflect_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state) {
    if (!state->mutable_prototypes) return ItemNull;
    Item& object = state->values[LMD_LIBRARY_REFLECT];
    if (state->native_callable_values) {
        // use the existing namespace before allocating any duplicate method wrappers.
        object = js_get_reflect_object_value();
        return object;
    }
    object = mvp_lmd_object_new(state->object_shape, 0);
    if (item_is_error(object)) return object;
    Item tag = mvp_lmd_named_set(object, Item{.item = s2it(well_known_name_ref(JS_SYMBOL_TO_STRING_TAG))},
        js_name_item("Reflect"), JSPD_NON_WRITABLE | JSPD_NON_ENUMERABLE);
    if (item_is_error(tag)) return tag;
    const char* names[] = {"apply", "construct", "defineProperty", "deleteProperty", "get", "getOwnPropertyDescriptor",
        "getPrototypeOf", "has", "isExtensible", "ownKeys", "preventExtensions", "set", "setPrototypeOf"};
    const uint8_t arities[] = {3, 2, 3, 2, 2, 2, 1, 2, 1, 1, 1, 3, 2};
    for (int i = 0; i < 13; i++) {
        Item status = mvp_lmd_named_set(object, names[i], mvp_lmd_native_function(program, (void*)(intptr_t)i,
            reflect_operation, arities[i], names[i]), JSPD_NON_ENUMERABLE);
        if (item_is_error(status)) return status;
    }
    return object;
}
