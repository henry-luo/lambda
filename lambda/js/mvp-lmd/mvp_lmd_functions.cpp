#include "mvp_lmd_runtime.h"
#include "mvp_lmd_dynamic.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/runtime-state.h"
#include "../../runtime/side_stack.h"
#include "../../runtime/heap_api.h"
#include "../js_well_known_names.h"
#include "../js_props.h"
#include "../js_runtime.h"
#include "../js_function.hpp"
#include "../../input/input.hpp"
#include "../../../lib/mem.h"
#include <math.h>

Item mvp_lmd_function_realm(Item function, MvpLmdProgram** program) {
    *program = NULL;
    // bound targets and proxy targets are immutable; this walk invokes no guest getters.
    for (;;) {
        if (mvp_lmd_is_proxy(function)) {
            JsProxyData* data = js_get_proxy_data(function);
            if (!data || data->revoked) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            function.item = data->target;
            continue;
        }
        if (get_type_id(function) == LMD_TYPE_FUNC && function_has_abi(function.function, FN_ENTRY_ABI_JS_FUNCTION)) {
            MvpLmdLibraryState* library = mvp_lmd_program_library();
            *program = library ? mvp_lmd_program_for_global(library->object.program,
                ((JsFunction*)function.function)->home_global) : NULL;
            return ItemNull;
        }
        if (get_type_id(function) != LMD_TYPE_FUNC ||
                function.function->entry_abi != FN_ENTRY_ABI_MVP_LMD ||
                !function.function->requires_runtime_context || function.function->runtime_context != context)
            return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        if (function.function->ptr == (fn_ptr)mvp_lmd_function_bound) {
            function = ((Item*)function.function->closure_env)[0];
            continue;
        }
        *program = mvp_lmd_program_realm(((MvpLmdCallable*)function.function)->program);
        return ItemNull;
    }
}

Item mvp_lmd_constructor_prototype(Item target, Item intrinsic) {
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> constructor(roots, target), fallback(roots, intrinsic);
    Item prototype = mvp_lmd_property_with_receiver(constructor.get(), js_name_item("prototype", 9),
        ItemNull, LMD_PROP_GET, constructor.get());
    if (item_is_error(prototype) || mvp_lmd_is_object(prototype)) return prototype;
    MvpLmdProgram* program = NULL;
    Item found = mvp_lmd_function_realm(constructor.get(), &program);
    if (item_is_error(found)) return found;
    MvpLmdLibraryState* source = mvp_lmd_program_library();
    MvpLmdLibraryState* destination = mvp_lmd_program_library(program);
    if (source == destination) return fallback.get();
    if (!source || !destination) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    // intrinsic slots retain identity even when guest global bindings or prototype properties change.
    struct Slots { Item* source; Item* destination; int count; };
    const Slots slots[] = {
        {source->values, destination->values, LMD_LIBRARY_COUNT},
        {source->error_values, destination->error_values, LMD_ERROR_COUNT * 3},
        {source->primitive_values, destination->primitive_values, LMD_PRIMITIVE_COUNT * 3},
        {source->buffer_values, destination->buffer_values, 6},
        {source->shared_buffer_values, destination->shared_buffer_values, 3},
        {source->typed_values, destination->typed_values, 13 * 3},
        {source->promise_values, destination->promise_values, 3},
        {source->weak_values, destination->weak_values,
            source->weak_class_count < destination->weak_class_count ? source->weak_class_count * 3 : destination->weak_class_count * 3},
        {source->async_function_values, destination->async_function_values, 3},
        {source->async_generator_values, destination->async_generator_values, 5},
        {source->generator_function_values, destination->generator_function_values, 2}
    };
    for (const Slots& block : slots) if (block.source && block.destination)
        for (int i = 0; i < block.count; i++)
            if (block.source[i].item == fallback.get().item) return block.destination[i];
    return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
}

extern "C" Item mvp_lmd_with_binding(Item owner, Item key, Item value, int64_t operation) {
    static const JsWithBindingOps ops = {
        [](Item object, Item name) { return mvp_lmd_property_with_receiver(object, name, ItemNull, LMD_PROP_HAS, object); },
        [](Item object, Item name) { return mvp_lmd_property_with_receiver(object, name, ItemNull, LMD_PROP_GET, object); },
        mvp_lmd_is_object, mvp_lmd_truthy
    };
    if (operation == LMD_PROP_HAS)
        return js_with_binding_is_visible_with_ops(key, owner,
            Item{.item = s2it(well_known_name_ref(JS_SYMBOL_UNSCOPABLES))}, &ops);
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner), name(roots, key), input(roots, value);
    int64_t mode = operation & ~LMD_PROP_STRICT;
    // resolution precedes RHS/arguments; Get/SetBindingValue rechecks this saved base.
    if (mode == LMD_PROP_GET || mode == LMD_PROP_SET) {
        Item present = ops.has(object.get(), name.get());
        if (item_is_error(present)) return present;
        if (present.item == ITEM_FALSE) {
            if (operation & LMD_PROP_STRICT) return mvp_lmd_fail(LMD_MVP_REFERENCE, 0);
            if (mode == LMD_PROP_GET) return Item{.item = ITEM_JS_UNDEFINED};
        }
    }
    return mvp_lmd_property_with_receiver(object.get(), name.get(), input.get(), operation, object.get());
}

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
    Item result;
    if (entry) {
        MvpLmdRealmScope realm(callable->program);
        result = realm.complete(entry(context, callable->program, span.items(), first + count,
            callee.get(), self.get(), Item{.item = ITEM_JS_UNDEFINED}));
    } else result = mvp_lmd_class_invoke(callee.get(), self.get(), span.items(), first + count, new_target.get());
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
static Item function_throw_type(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item) {
    return mvp_lmd_fail(LMD_MVP_TYPE, 0);
}
static Item function_has_instance(Context*, MvpLmdProgram*, Item* args, uint64_t count, Item, Item receiver, Item) {
    return mvp_lmd_instanceof(count ? args[0] : Item{.item = ITEM_JS_UNDEFINED}, receiver, 1);
}
Item mvp_lmd_arguments_cell(Item owner, String* key, bool detach) {
    // only indexed properties can alias parameters; ordinary named writes need no context lookup.
    int64_t index = mvp_lmd_string_key(key);
    if (index < 0) return Item{.item = ITEM_JS_UNDEFINED};
    MvpLmdLibraryState* state = mvp_lmd_program_library();
    if (!state || !state->arguments_key || get_type_id(owner) != LMD_TYPE_MAP || mvp_lmd_is_proxy(owner))
        return Item{.item = ITEM_JS_UNDEFINED};
    ShapeEntry* field = typemap_hash_lookup_key((TypeMap*)owner.map->type, state->arguments_key);
    if (!field) return Item{.item = ITEM_JS_UNDEFINED};
    Item mapping = map_shape_field_to_item(owner.map->data, field);
    if (index >= mapping.array->length) return Item{.item = ITEM_JS_UNDEFINED};
    Item cell = mapping.array->items[index];
    // the alias table contains only cell pointers/null; unlinking cannot allocate.
    if (detach) mapping.array->items[index] = ItemNull;
    return get_type_id(cell) == LMD_TYPE_ARRAY ? cell : Item{.item = ITEM_JS_UNDEFINED};
}
extern "C" Item mvp_lmd_arguments_new(Item function, Item* values, uint64_t count, Item cells, int64_t unmapped) {
    MvpLmdLibraryState* state = mvp_lmd_program_library();
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> callee(roots, function), mapping(roots, cells), object(roots, ItemNull), key(roots, ItemNull);
    object.set(mvp_lmd_object_new(state->object_shape, 0));
    if (item_is_error(object.get())) return object.get();
    if (!state->arguments_key)
        state->arguments_key = mvp_lmd_internal_name(LMD_INTERNAL_ARGUMENTS);
    if (!state->arguments_key) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    if (get_type_id(mapping.get()) != LMD_TYPE_ARRAY) mapping.set(mvp_lmd_array_new(0));
    if (item_is_error(mapping.get())) return mapping.get();
    // absent actual arguments never alias a later parameter assignment.
    for (int64_t i = count; i < mapping.get().array->length; i++) mapping.get().array->items[i] = ItemNull;
    if (!map_shape_set(object.get().map, state->arguments_key, mapping.get())) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    for (uint64_t i = 0; i < count; i++) {
        key.set(mvp_lmd_number_to_string(i));
        if (item_is_error(key.get())) return key.get();
        Item stored = mvp_lmd_named_set(object.get(), key.get().get_string()->chars, values[i], 0);
        if (item_is_error(stored)) return stored;
    }
    Item status = mvp_lmd_named_set(object.get(), "length", Item{.item = i2it(count)}, JSPD_NON_ENUMERABLE);
    if (item_is_error(status)) return status;
    JsPropertyDescriptor descriptor = {};
    descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_WRITABLE |
        JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
    js_pd_set_configurable(&descriptor, true);
    descriptor.value = state->values[LMD_LIBRARY_SEQUENCE_ITERATOR];
    status = mvp_lmd_define_own_property(object.get(), Item{.item = s2it(well_known_name_ref(JS_SYMBOL_ITERATOR))}, &descriptor);
    if (item_is_error(status)) return status;
    if (!unmapped) {
        status = mvp_lmd_named_set(object.get(), "callee", callee.get(), JSPD_NON_ENUMERABLE);
        return item_is_error(status) ? status : object.get();
    }
    descriptor = {};
    descriptor.flags = JS_PD_HAS_GET | JS_PD_HAS_SET | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
    descriptor.getter = descriptor.setter = state->values[LMD_LIBRARY_THROW_TYPE];
    status = mvp_lmd_define_own_property(object.get(), Item{.item = s2it(heap_create_name("callee", 6))}, &descriptor);
    return item_is_error(status) ? status : object.get();
}
Item mvp_lmd_function_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state) {
    for (int i = LMD_LIBRARY_CALL; i <= LMD_LIBRARY_BIND; i++) {
        state->values[i] = mvp_lmd_native_function(program, state, function_method, i == LMD_LIBRARY_APPLY ? 2 : 1,
            i == LMD_LIBRARY_APPLY ? "apply" : i == LMD_LIBRARY_BIND ? "bind" : "call");
        if (item_is_error(state->values[i])) return state->values[i];
    }
    if (state->mutable_prototypes) {
        Item& prototype = state->values[LMD_LIBRARY_FUNCTION_PROTOTYPE];
        prototype = mvp_lmd_native_function(program, state, function_prototype, 0);
        if (item_is_error(prototype)) return prototype;
        Item ready = mvp_lmd_function_prepare(prototype);
        if (item_is_error(ready)) return ready;
        state->values[LMD_LIBRARY_FUNCTION] = mvp_lmd_native_function(program, state, mvp_lmd_dynamic_function, 1, "Function");
        if (item_is_error(state->values[LMD_LIBRARY_FUNCTION])) return state->values[LMD_LIBRARY_FUNCTION];
        MvpLmdCallable* constructor = (MvpLmdCallable*)state->values[LMD_LIBRARY_FUNCTION].function;
        constructor->native_constructor = constructor->custom_new_target = true;
        ready = mvp_lmd_named_set(state->values[LMD_LIBRARY_FUNCTION], "prototype", prototype,
            JSPD_NON_WRITABLE | JSPD_NON_ENUMERABLE | JSPD_NON_CONFIGURABLE);
        if (item_is_error(ready)) return ready;
        ready = mvp_lmd_named_set(prototype, "constructor", state->values[LMD_LIBRARY_FUNCTION], JSPD_NON_ENUMERABLE);
        if (item_is_error(ready)) return ready;
        const char* names[] = {"call", "apply", "bind"};
        for (int i = 0; i < 3; i++) {
            Item method = state->values[LMD_LIBRARY_CALL + i];
            Item stored = mvp_lmd_named_set(method, "name",
                Item{.item = s2it(heap_create_name(names[i], strlen(names[i])))}, JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE);
            if (!item_is_error(stored)) stored = mvp_lmd_named_set(prototype, names[i], method, JSPD_NON_ENUMERABLE);
            if (item_is_error(stored)) return stored;
        }
        state->values[LMD_LIBRARY_FUNCTION_STRING] = mvp_lmd_native_function(program, state, mvp_lmd_function_source, 0, "toString");
        ready = mvp_lmd_named_set(prototype, "toString", state->values[LMD_LIBRARY_FUNCTION_STRING], JSPD_NON_ENUMERABLE);
        if (item_is_error(ready)) return ready;
        Item& poison = state->values[LMD_LIBRARY_THROW_TYPE];
        poison = mvp_lmd_native_function(program, state, function_throw_type, 0, "");
        if (item_is_error(poison)) return poison;
        for (const char* name : {"name", "length"}) {
            ready = mvp_lmd_named_set(poison, name, !strcmp(name, "name") ?
                Item{.item = s2it(heap_create_name("", 0))} : Item{.item = i2it(0)},
                JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE | JSPD_NON_CONFIGURABLE);
            if (item_is_error(ready)) return ready;
        }
        ready = mvp_lmd_object_extensible(poison, true);
        if (item_is_error(ready)) return ready;
        for (const char* name : {"caller", "arguments"}) {
            JsPropertyDescriptor descriptor = {};
            descriptor.flags = JS_PD_HAS_GET | JS_PD_HAS_SET | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
            js_pd_set_configurable(&descriptor, true);
            descriptor.getter = descriptor.setter = poison;
            ready = mvp_lmd_define_own_property(prototype, Item{.item = s2it(heap_create_name(name, strlen(name)))}, &descriptor);
            if (item_is_error(ready)) return ready;
        }
        Item key = {.item = s2it(well_known_name_ref(JS_SYMBOL_HAS_INSTANCE))};
        Item callable = mvp_lmd_native_function(program, state, function_has_instance, 1, "[Symbol.hasInstance]");
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
