#include "mvp_lmd_async.h"
#include "mvp_lmd_dynamic.h"
#include "../js_runtime.h"
#include "../js_runtime_state.hpp"
#include "../js_property_attrs.h"
#include "../js_props.h"
#include "../js_well_known_names.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/heap_api.h"

static Item promise_intrinsic(void* context, bool prototype) {
    MvpLmdLibraryState* library = mvp_lmd_program_library((MvpLmdProgram*)context);
    return library->promise_values[prototype ? 1 : 0];
}

static Item promise_get(void*, Item object, Item key, Item receiver) {
    // shared Promise species lookup supplies a Symbol; MVP storage consumes its canonical NameRef.
    if (get_type_id(key) != LMD_TYPE_STRING) {
        key = mvp_lmd_property_key(key);
        if (item_is_error(key)) return key;
    }
    return js_promise_is(object) ? mvp_lmd_promise_property(object, key, ItemNull, LMD_PROP_GET, receiver)
        : mvp_lmd_property_with_receiver(object, key, ItemNull, LMD_PROP_GET, receiver);
}

static Item promise_invoke(void* context, Item callback, Item receiver, Item* args, int count, Item target) {
    RootFrame roots(3);
    RootSpan arguments((size_t)count);
    if (!roots.valid() || (count && !arguments.valid())) return ItemError;
    Rooted<Item> function(roots, callback), self(roots, receiver), new_target(roots, target);
    for (int i = 0; i < count; i++) {
        uint64_t* home = lambda_side_number_alloc();
        if (!home) return ItemError;
        arguments.items()[i] = lambda_item_adopt_scalar_home(args[i], home);
    }
    // resolving functions and capability executors keep one identity in the shared bridge cache.
    for (int i = 0; i < count; i++) {
        Item exposed = mvp_lmd_dynamic_wrap_function((MvpLmdProgram*)context, arguments.items()[i]);
        if (item_is_error(exposed)) return exposed;
        arguments.items()[i] = exposed;
    }
    return mvp_lmd_class_invoke(function.get(), self.get(), arguments.items(), count, new_target.get());
}

static Item promise_iterator(void* context, Item iterable) {
    return mvp_lmd_iterator_get((MvpLmdProgram*)context, iterable);
}

static Item promise_iterator_step(void*, Item iterator) {
    return mvp_lmd_iterator_step_value(iterator);
}

static Item promise_error(void* context, const char* name, Item message, Item errors) {
    MvpLmdLibraryState* library = mvp_lmd_program_library((MvpLmdProgram*)context);
    int kind = !strcmp(name, "AggregateError") ? LMD_ERROR_AGGREGATE : LMD_ERROR_TYPE;
    if (!library->errors) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    Item constructor = library->error_values[kind * 3];
    Item args[] = {kind == LMD_ERROR_AGGREGATE ? errors : message, message};
    return mvp_lmd_class_invoke(constructor, Item{.item = ITEM_JS_UNDEFINED}, args,
        kind == LMD_ERROR_AGGREGATE ? 2 : 1, constructor);
}

static const JsPromiseHostOps promise_ops = {
    promise_get,
    [](void*, Item value) { return mvp_lmd_is_callable(value); },
    [](void*, Item value) { return mvp_lmd_is_constructor(value); },
    [](void* context, Item callback, Item receiver, Item* args, int count) {
        return promise_invoke(context, callback, receiver, args, count, Item{.item = ITEM_JS_UNDEFINED});
    },
    [](void* context, Item constructor, Item* args, int count, Item target) {
        return promise_invoke(context, constructor, Item{.item = ITEM_JS_UNDEFINED}, args, count, target);
    },
    promise_intrinsic,
    [](void* context) { return mvp_lmd_object_new(mvp_lmd_program_library((MvpLmdProgram*)context)->object_shape, 0); },
    [](void*, Item object, Item key, Item value) {
        return mvp_lmd_property_with_receiver(object, key, value, LMD_PROP_CREATE_DATA, object);
    },
    promise_iterator, promise_iterator_step,
    [](void*, Item iterator, Item completion) { return mvp_lmd_iterator_close(iterator, completion); },
    promise_error,
    [](void* context, Item error) { return mvp_lmd_caught_value(mvp_lmd_program_library((MvpLmdProgram*)context), error); },
    [](void* context, Item function) { return mvp_lmd_dynamic_wrap_function((MvpLmdProgram*)context, function); },
    [](void* opaque, Item job, Item handler) {
        MvpLmdProgram* program = (MvpLmdProgram*)opaque;
        if (!mvp_lmd_program_library(program)->test262_host) return job;
        RootFrame roots(2);
        if (!roots.valid()) return ItemError;
        Rooted<Item> task(roots, job), callback(roots, handler);
        if (mvp_lmd_is_callable(callback.get())) {
            MvpLmdProgram* owner = NULL;
            Item found = mvp_lmd_function_realm(callback.get(), &owner);
            // GetFunctionRealm failure selects the current realm for a Promise job.
            if (!item_is_error(found) && owner) program = owner;
        }
        task.set(mvp_lmd_dynamic_wrap_function(program, task.get()));
        return item_is_error(task.get()) ? task.get() : mvp_lmd_host_task(task.get(),
            Item{.item = ITEM_JS_UNDEFINED}, Item{.item = ITEM_JS_UNDEFINED});
    },
    [](void*, int64_t length) { return mvp_lmd_array_new(length); }
};

static Item promise_constructor(Context*, MvpLmdProgram* program, Item* args, uint64_t count,
        Item, Item, Item target) {
    Item absent = {.item = ITEM_JS_UNDEFINED};
    if (target.item == absent.item || !count || !mvp_lmd_is_callable(args[0]))
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> executor(roots, args[0]), prototype(roots, mvp_lmd_constructor_prototype(target,
        promise_intrinsic(program, true))), result(roots, ItemNull);
    if (item_is_error(prototype.get())) return prototype.get();
    result.set(js_promise_create(executor.get()));
    if (item_is_error(result.get())) return result.get();
    Item linked = mvp_lmd_set_prototype(result.get(), prototype.get());
    return item_is_error(linked) ? linked : linked.item == ITEM_FALSE
        ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : result.get();
}

static Item promise_method(Context*, MvpLmdProgram*, Item* args, uint64_t count,
        Item self, Item receiver, Item target) {
    if (target.item != ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    auto operation = (JsPromiseIntrinsicOp)(intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    return js_intrinsic_promise_operation(operation, receiver, args, (int)count);
}

Item mvp_lmd_promise_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state) {
    Item result = mvp_lmd_library_class_initialize(program, state, state->promise,
        (int)(state->promise_values - state->values), "Promise", promise_constructor, 1);
    if (item_is_error(result)) return result;
    ((MvpLmdCallable*)state->promise_values[0].function)->custom_new_target = true;
    // one immutable policy accompanies the existing realm/job owner until context teardown.
    js_promise_set_host_ops(&promise_ops, program);
    const struct { const char* name; JsPromiseIntrinsicOp operation; uint8_t arity; bool prototype; } methods[] = {
        {"resolve", JS_PROMISE_INTRINSIC_RESOLVE, 1, false},
        {"reject", JS_PROMISE_INTRINSIC_REJECT, 1, false},
        {"all", JS_PROMISE_INTRINSIC_ALL, 1, false},
        {"allSettled", JS_PROMISE_INTRINSIC_ALL_SETTLED, 1, false},
        {"any", JS_PROMISE_INTRINSIC_ANY, 1, false},
        {"race", JS_PROMISE_INTRINSIC_RACE, 1, false},
        {"withResolvers", JS_PROMISE_INTRINSIC_WITH_RESOLVERS, 0, false},
        {"then", JS_PROMISE_INTRINSIC_THEN, 2, true},
        {"catch", JS_PROMISE_INTRINSIC_CATCH, 1, true},
        {"finally", JS_PROMISE_INTRINSIC_FINALLY, 1, true}
    };
    for (const auto& method : methods) {
        result = mvp_lmd_named_set(state->promise_values[method.prototype ? 1 : 2],
            method.name, mvp_lmd_native_function(program, (void*)(intptr_t)method.operation,
                promise_method, method.arity, method.name), JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
    }
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> identity(roots, mvp_lmd_native_function(program, NULL, mvp_lmd_receiver_identity, 0, "get [Symbol.species]"));
    if (item_is_error(identity.get())) return identity.get();
    result = mvp_lmd_define_accessor(state->promise_values[2],
        Item{.item = s2it(well_known_name_ref(JS_SYMBOL_SPECIES))}, identity.get(), JSPD_NON_ENUMERABLE << 8);
    if (item_is_error(result)) return result;
    JsPropertyDescriptor descriptor = {};
    descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_HAS_ENUMERABLE |
        JS_PD_HAS_CONFIGURABLE;
    js_pd_set_configurable(&descriptor, true);
    descriptor.value = js_name_item("Promise", 7);
    return mvp_lmd_define_own_property(state->promise_values[1],
        Item{.item = s2it(well_known_name_ref(JS_SYMBOL_TO_STRING_TAG))}, &descriptor);
}

Item mvp_lmd_promise_property(Item owner, Item key, Item value, int64_t operation, Item receiver) {
    if (!js_promise_is(owner)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(5);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner), name(roots, key), held(roots, value), self(roots, receiver), storage(roots, ItemNull);
    int opcode = operation & 255;
    if (opcode == LMD_PROP_SET) {
        Item stored = mvp_lmd_set_completion(object.get(), name.get(), held.get(), self.get());
        if (item_is_error(stored)) return stored;
        return stored.item == ITEM_FALSE && (operation & LMD_PROP_STRICT)
            ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : held.get();
    }
    storage.set(js_promise_property_storage(object.get(), false));
    if (item_is_error(storage.get())) return storage.get();
    bool own = false;
    if (get_type_id(storage.get()) == LMD_TYPE_MAP) {
        JsPropertyDescriptor descriptor = {};
        Item found = mvp_lmd_get_own_property(storage.get(), name.get(), &descriptor);
        if (item_is_error(found)) return found;
        own = found.item == ITEM_TRUE;
    }
    if (opcode == LMD_PROP_OWN) return Item{.item = b2it(own)};
    if (opcode == LMD_PROP_DELETE) return own
        ? mvp_lmd_property_with_receiver(storage.get(), name.get(), held.get(), operation, self.get())
        : Item{.item = ITEM_TRUE};
    if (own) return opcode == LMD_PROP_HAS ? Item{.item = ITEM_TRUE}
        : mvp_lmd_property_with_receiver(storage.get(), name.get(), held.get(), operation, self.get());
    storage.set(js_get_prototype(object.get()));
    if (storage.get().item == ITEM_NULL || storage.get().item == ITEM_JS_UNDEFINED)
        return opcode == LMD_PROP_HAS ? Item{.item = ITEM_FALSE} : Item{.item = ITEM_JS_UNDEFINED};
    return mvp_lmd_property_with_receiver(storage.get(), name.get(), held.get(), operation, self.get());
}

static Item async_body(Activation* activation, Item) {
    // a resumed body owns its call facts after the first resumer's native stack returns.
    JsOwnedCallActivation call_facts;
    auto* state = (JsAsyncContextStateRecord*)activation_user(activation);
    // the shared carrier traces the MIR callable, packed arguments and parked root segment.
    return mvp_lmd_function_forward(state->ast_function, state->this_val, state->ast_arguments,
        NULL, 0, 0, (MvpLmdNativeEntry)state->state_fn);
}

Item mvp_lmd_async_call(Context*, MvpLmdProgram*, Item* args, uint64_t count,
        Item self, Item receiver, Item target) {
    if (target.item != ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> function(roots, self), owner(roots, receiver), arguments(roots, mvp_lmd_array_new(0)), frame(roots, ItemNull);
    if (item_is_error(arguments.get())) return arguments.get();
    for (uint64_t i = 0; i < count; i++) {
        Item stored = mvp_lmd_array_store(arguments.get(), i, args[i]);
        if (item_is_error(stored)) return stored;
    }
    auto* callable = (MvpLmdNativeCallable*)function.get().function;
    frame.set(js_async_context_create_body(async_body, callable->state, NULL, 0,
        owner.get(), function.get(), arguments.get()));
    if (item_is_error(frame.get())) return frame.get();
    Item started = js_async_start(frame.get());
    return item_is_error(started) ? started : js_async_get_promise(frame.get());
}

static Item suspended_function_constructor(Context*, MvpLmdProgram* program, Item* args, uint64_t count,
        Item self, Item, Item target) {
    auto kind = (MvpLmdDynamicFunctionKind)(intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    return mvp_lmd_dynamic_function_kind(program, args, count, target, kind);
}

static Item async_generator_method(Context*, MvpLmdProgram*, Item* args, uint64_t count,
        Item self, Item receiver, Item target) {
    if (target.item != ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    int operation = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    return js_async_generator_request(receiver, operation, count ? args[0] : Item{.item = ITEM_JS_UNDEFINED});
}

static Item suspended_function_family(MvpLmdProgram* program, MvpLmdLibraryState* state,
        MvpLmdDynamicFunctionKind kind) {
    bool asynchronous = kind != LMD_DYNAMIC_GENERATOR;
    bool generator = kind != LMD_DYNAMIC_ASYNC_FUNCTION;
    const char* name = !generator ? "AsyncFunction" : asynchronous ? "AsyncGeneratorFunction" : "GeneratorFunction";
    MvpLmdClass* family = !generator ? state->async_function : asynchronous ? state->async_generator : &state->generator;
    Item* values = !generator ? state->async_function_values : asynchronous ? state->async_generator_values
        : state->values + LMD_LIBRARY_GENERATOR;
    Item* extras = !generator ? NULL : asynchronous ? values + 3 : state->generator_function_values;
    // the synchronous generator's already-installed methods retain the same nominal family.
    if (asynchronous) {
        Item initialized = mvp_lmd_library_class_initialize(program, state, family,
            (int)(values - state->values), name, suspended_function_constructor, 1, false);
        if (item_is_error(initialized)) return initialized;
    }
    auto* constructor = (MvpLmdNativeCallable*)values[0].function;
    constructor->ptr = (fn_ptr)suspended_function_constructor;
    constructor->state = (void*)(intptr_t)kind;
    constructor->arity = 1;
    constructor->custom_new_target = true;
    if (generator) for (int i = 0; i < 2; i++) {
        // synchronous generators share the same IteratorPrototype as arrays, collections and RegExp iterators.
        extras[i] = !asynchronous && i == 0 && state->iterator_base
            ? state->iterator_base_values[1] : mvp_lmd_object_new(state->object_shape, 0);
        if (item_is_error(extras[i])) return extras[i];
    }
    Item prototype = generator ? extras[1] : values[1];
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> method(roots, ItemNull);
    Item result = mvp_lmd_named_set(values[2], "name", js_name_item(name, (int)strlen(name)),
        JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE);
    if (item_is_error(result)) return result;
    result = mvp_lmd_named_set(values[2], "length", Item{.item = i2it(1)},
        JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE);
    if (item_is_error(result)) return result;
    result = mvp_lmd_set_prototype(prototype, state->values[LMD_LIBRARY_FUNCTION_PROTOTYPE]);
    if (item_is_error(result)) return result;
    result = mvp_lmd_named_set(prototype, "constructor", values[0], JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE);
    if (item_is_error(result)) return result;
    result = mvp_lmd_named_set(values[2], "prototype", prototype,
        JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE | JSPD_NON_CONFIGURABLE);
    if (item_is_error(result)) return result;
    result = mvp_lmd_set_prototype(values[0], state->values[LMD_LIBRARY_FUNCTION]);
    if (item_is_error(result)) return result;
    if (generator) {
        result = mvp_lmd_set_prototype(values[1], extras[0]);
        if (item_is_error(result)) return result;
        result = mvp_lmd_named_set(values[1], "constructor", prototype, JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE);
        if (item_is_error(result)) return result;
        result = mvp_lmd_named_set(prototype, "prototype", values[1], JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE);
        if (item_is_error(result)) return result;
        if (asynchronous) {
            const char* names[] = {"next", "return", "throw"};
            for (int i = 0; i < 3; i++) {
                method.set(mvp_lmd_native_function(program, (void*)(intptr_t)i, async_generator_method, 1, names[i]));
                if (item_is_error(method.get())) return method.get();
                result = mvp_lmd_named_set(values[1], names[i], method.get(), JSPD_NON_ENUMERABLE);
                if (item_is_error(result)) return result;
            }
        }
        if (asynchronous || !state->iterator_base) {
            method.set(mvp_lmd_native_function(program, NULL, mvp_lmd_receiver_identity, 0,
                asynchronous ? "[Symbol.asyncIterator]" : "[Symbol.iterator]"));
            if (item_is_error(method.get())) return method.get();
            JsPropertyDescriptor descriptor = {};
            descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE | JS_PD_WRITABLE;
            js_pd_set_configurable(&descriptor, true);
            descriptor.value = method.get();
            result = mvp_lmd_define_own_property(extras[0], Item{.item = s2it(well_known_name_ref(
                asynchronous ? JS_SYMBOL_ASYNC_ITERATOR : JS_SYMBOL_ITERATOR))}, &descriptor);
            if (item_is_error(result)) return result;
        }

    }
    for (int i = 0; i < (generator ? 2 : 1); i++) {
        const char* tag = i ? asynchronous ? "AsyncGenerator" : "Generator" : name;
        JsPropertyDescriptor descriptor = {};
        descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
        js_pd_set_configurable(&descriptor, true);
        descriptor.value = js_name_item(tag, (int)strlen(tag));
        result = mvp_lmd_define_own_property(i ? values[1] : prototype,
            Item{.item = s2it(well_known_name_ref(JS_SYMBOL_TO_STRING_TAG))}, &descriptor);
        if (item_is_error(result)) return result;
    }
    return ItemNull;
}

Item mvp_lmd_suspended_functions_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state) {
    const bool present[] = {state->generator_function_values != NULL, state->async_function != NULL,
        state->async_generator != NULL};
    for (int i = 0; i < 3; i++) if (present[i]) {
        Item result = suspended_function_family(program, state, (MvpLmdDynamicFunctionKind)(i + 1));
        if (item_is_error(result)) return result;
    }
    return ItemNull;
}
