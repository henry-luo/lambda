#include "mvp_lmd_runtime.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/runtime-state.h"
#include "../../runtime/side_stack.h"
#include "../../runtime/heap_api.h"
#include "../js_well_known_names.h"
#include "../js_props.h"
#include "../../input/input.hpp"
#include "../../../lib/mem.h"
#include <math.h>

Item mvp_lmd_function_forward(Item function, Item receiver, Item prefix, Item* arguments, uint64_t count, int skip, MvpLmdNativeEntry entry, Item target) {
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> callee(roots, function), self(roots, receiver), head(roots, prefix), new_target(roots, target);
    Item extent = prefix.item == ITEM_JS_UNDEFINED ? Item{.item = i2it(0)} : mvp_lmd_sequence_length(head.get());
    if (item_is_error(extent)) return extent;
    uint64_t first = (uint64_t)it2i(extent);
    first = first > (uint64_t)skip ? first - skip : 0;
    if (count > UINT32_MAX || first > UINT32_MAX - count) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
    RootSpan span(first + count ? first + count : 1);
    if (!span.valid()) return ItemError;
    uint64_t* watermark = context->side_number_top;
    if (!lambda_side_stack_ensure_tls(0, first + count)) return ItemError;
    uint64_t* homes = context->side_number_top;
    context->side_number_top += first + count;
    // snapshot incoming values before a prefix getter can reenter; these slots also survive the callee.
    for (uint64_t i = 0; i < count; i++) {
        Item value = arguments[i];
        if (item_is_error(value)) {
            context->side_number_top = watermark;
            return value;
        }
        if (value.item == ITEM_JS_DELETED_SENTINEL) value.item = ITEM_JS_UNDEFINED;
        span.items()[first + i] = lambda_item_adopt_scalar_home(value, &homes[first + i]);
    }
    for (uint64_t i = 0; i < first; i++) {
        Item value = mvp_lmd_array_index_property(head.get(), i + skip, ItemNull, LMD_PROP_GET);
        if (item_is_error(value)) {
            context->side_number_top = watermark;
            return value;
        }
        if (value.item == ITEM_JS_DELETED_SENTINEL) value.item = ITEM_JS_UNDEFINED;
        span.items()[i] = lambda_item_adopt_scalar_home(value, &homes[i]);
    }
    MvpLmdCallable* callable = (MvpLmdCallable*)callee.get().function;
    Item result = entry ? entry(context, callable->program, span.items(), first + count,
        callee.get(), self.get(), Item{.item = ITEM_JS_UNDEFINED}) :
        mvp_lmd_class_invoke(callee.get(), self.get(), span.items(), first + count, new_target.get());
    result = lambda_item_resolve_pending_slot(result);
    bool number = get_type_id(result) == LMD_TYPE_FLOAT;
    double scalar = number ? it2d(result) : 0;
    // Activation abandonment releases these side-stack homes without native destructors.
    context->side_number_top = watermark;
    return number ? push_d(scalar) : result;
}
extern "C" Item mvp_lmd_call_array(Item function, Item receiver, Item arguments, Item target) {
    if (!mvp_lmd_is_callable(function)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    return mvp_lmd_function_forward(function, receiver, arguments, NULL, 0, 0, NULL, target);
}
Item mvp_lmd_function_bound(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item, Item target) {
    Item* env = (Item*)self.function->closure_env;
    return mvp_lmd_function_forward(env[0], env[1].array->items[0], env[1], args, count, 1, NULL,
        target.item == self.item ? env[0] : target);
}
static Item function_method(Context*, MvpLmdProgram* program, Item* args, uint64_t count, Item self, Item receiver, Item) {
    if (!mvp_lmd_is_callable(receiver)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    MvpLmdLibraryState* state = (MvpLmdLibraryState*)((MvpLmdNativeCallable*)self.function)->state;
    Item absent = {.item = ITEM_JS_UNDEFINED}, this_arg = count ? args[0] : absent;
    if (self.item == state->values[LMD_LIBRARY_CALL].item)
        return mvp_lmd_function_forward(receiver, this_arg, absent, count ? args + 1 : NULL, count ? count - 1 : 0);
    if (self.item == state->values[LMD_LIBRARY_APPLY].item) {
        Item array = count > 1 ? args[1] : absent;
        if (array.item == ITEM_NULL || array.item == ITEM_JS_UNDEFINED)
            return mvp_lmd_function_forward(receiver, this_arg, absent, NULL, 0);
        if (!mvp_lmd_is_object(array)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        // Retain the owner, not a borrowed items pointer, while the argument snapshot allocates.
        return mvp_lmd_function_forward(receiver, this_arg, array, NULL, 0);
    }
    RootFrame roots(6);
    if (!roots.valid()) return ItemError;
    Rooted<Item> callee(roots, receiver), object(roots, this_arg), bound(roots, ItemNull);
    Rooted<Item> prototype(roots, mvp_lmd_get_prototype(callee.get())), metadata(roots, ItemNull), arguments(roots, ItemNull);
    if (item_is_error(prototype.get())) return prototype.get();
    arguments.set(mvp_lmd_array_new(0));
    if (item_is_error(arguments.get())) return arguments.get();
    Item stored = mvp_lmd_array_store(arguments.get(), 0, object.get());
    if (item_is_error(stored)) return stored;
    for (uint64_t i = 1; i < count; i++) {
        Item stored = mvp_lmd_array_store(arguments.get(), i, args[i]);
        if (item_is_error(stored)) return stored;
    }
    // bind observes target [[Prototype]], own length and name in this order.
    Item key = {.item = s2it(heap_create_name("length", 6))};
    Item own = mvp_lmd_property_with_receiver(callee.get(), key, ItemNull, LMD_PROP_OWN, callee.get());
    if (item_is_error(own)) return own;
    double length = 0;
    if (own.item == ITEM_TRUE) {
        metadata.set(mvp_lmd_property_with_receiver(callee.get(), key, ItemNull, LMD_PROP_GET, callee.get()));
        if (item_is_error(metadata.get())) return metadata.get();
        TypeId type = get_type_id(metadata.get());
        if (type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT)
            length = fmax(0, trunc(it2d(metadata.get())) - (double)(count ? count - 1 : 0));
    }
    bound.set(mvp_lmd_native_function(program, state, mvp_lmd_function_bound, length >= 255 ? 255 : (uint8_t)length));
    if (item_is_error(bound.get())) return bound.get();
    ((MvpLmdCallable*)bound.get().function)->native_constructor = mvp_lmd_is_constructor(callee.get());
    Item* env = (Item*)heap_calloc_closure_env(2 * sizeof(Item));
    if (!env) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    bound.get().function->closure_env = env; bound.get().function->closure_field_count = 2;
    // The captured array owns scalar homes for both thisArg and the bound arguments.
    env[0] = callee.get(); env[1] = arguments.get();
    Item linked = mvp_lmd_set_prototype(bound.get(), prototype.get());
    if (item_is_error(linked)) return linked;
    int flags = JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE;
    Item defined = mvp_lmd_named_set(bound.get(), "length", push_d(length), flags);
    if (item_is_error(defined)) return defined;
    key.item = s2it(heap_create_name("name", 4));
    metadata.set(mvp_lmd_property_with_receiver(callee.get(), key, ItemNull, LMD_PROP_GET, callee.get()));
    if (item_is_error(metadata.get())) return metadata.get();
    if (get_type_id(metadata.get()) != LMD_TYPE_STRING) metadata.set(Item{.item = s2it(heap_create_name("", 0))});
    metadata.set(mvp_lmd_string_concat(Item{.item = s2it(heap_create_name("bound ", 6))}, metadata.get()));
    if (item_is_error(metadata.get())) return metadata.get();
    defined = mvp_lmd_named_set(bound.get(), "name", metadata.get(), flags);
    if (item_is_error(defined)) return defined;
    return bound.get();
}
static Item function_prototype(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item) {
    return Item{.item = ITEM_JS_UNDEFINED};
}
static Item function_has_instance(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item, Item receiver, Item) {
    return mvp_lmd_instanceof(count ? args[0] : Item{.item = ITEM_JS_UNDEFINED}, receiver, 1);
}
Item mvp_lmd_function_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state) {
    for (int i = LMD_LIBRARY_CALL; i <= LMD_LIBRARY_BIND; i++) {
        state->values[i] = mvp_lmd_native_function(program, state, function_method, i == LMD_LIBRARY_APPLY ? 2 : 1);
        if (item_is_error(state->values[i])) return state->values[i];
    }
    if (state->mutable_prototypes) {
        Item& prototype = state->values[LMD_LIBRARY_FUNCTION_PROTOTYPE];
        prototype = mvp_lmd_native_function(program, state, function_prototype, 0);
        if (item_is_error(prototype)) return prototype;
        Item ready = mvp_lmd_function_prepare(prototype);
        if (item_is_error(ready)) return ready;
        Map* map = mvp_lmd_property_map(prototype);
        const char* names[] = {"call", "apply", "bind"};
        for (int i = 0; i < 3; i++) {
            Item stored = mvp_lmd_named_set(Item{.map = map}, names[i], state->values[LMD_LIBRARY_CALL + i]);
            if (item_is_error(stored)) return stored;
            ShapeEntry* field = typemap_hash_lookup((TypeMap*)map->type, names[i], strlen(names[i]));
            TypeMap* shape = type_tree_reflag_field(runtime_shape_tree(), (TypeMap*)map->type, field, JSPD_NON_ENUMERABLE);
            if (!shape) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            map->type = shape;
        }
        Item key = {.item = s2it(well_known_name_ref(JS_SYMBOL_HAS_INSTANCE))};
        Item callable = mvp_lmd_native_function(program, state, function_has_instance, 1);
        if (item_is_error(callable)) return callable;
        JsPropertyDescriptor descriptor = {};
        descriptor.flags = JS_PD_HAS_VALUE; descriptor.value = callable;
        Item defined = mvp_lmd_define_own_property(prototype, key, &descriptor);
        if (item_is_error(defined) || defined.item == ITEM_FALSE) return item_is_error(defined) ? defined : mvp_lmd_fail(LMD_MVP_TYPE, 0);
    }
    return ItemNull;
}
Item mvp_lmd_function_method(Item owner, String* name) {
    if (property_key_requires_identity(name)) return Item{.item = ITEM_JS_UNDEFINED};
    Function* function = owner.function;
    if (function->entry_abi != FN_ENTRY_ABI_MVP_LMD || !function->requires_runtime_context)
        return Item{.item = ITEM_JS_UNDEFINED};
    MvpLmdLibraryState* state = mvp_lmd_program_library(((MvpLmdCallable*)function)->program);
    if (!state) return Item{.item = ITEM_JS_UNDEFINED};
    const char* names[] = {"call", "apply", "bind"};
    for (int i = 0; i < 3; i++) if (name->len == strlen(names[i]) && !memcmp(name->chars, names[i], name->len))
        return state->values[LMD_LIBRARY_CALL + i];
    return Item{.item = ITEM_JS_UNDEFINED};
}
