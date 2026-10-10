#include "mvp_lmd_runtime.h"
#include "../../runtime/suspended_activation.hpp"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/runtime-state.h"
#include "../../runtime/heap_api.h"
#include "../js_well_known_names.h"

static bool iterator_object(Item value) {
    TypeId type = get_type_id(value);
    return type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY || type == LMD_TYPE_FUNC || type == LMD_TYPE_ARRAY_NUM;
}
static Item iterator_property(Item owner, const char* name) {
    String* key = heap_create_name(name, strlen(name));
    return key ? mvp_lmd_class_property(owner, Item{.item = s2it(key)}, ItemNull, LMD_PROP_GET, NULL) : ItemError;
}
extern "C" Item mvp_lmd_iterator_get(MvpLmdProgram* program, Item source) {
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, source), method(roots, ItemNull), iterator(roots, ItemNull), record(roots, ItemNull);
    Item name = {.item = s2it(well_known_name_ref(JS_SYMBOL_ITERATOR))};
    TypeId type = get_type_id(source);
    method.set(type == LMD_TYPE_ARRAY || type == LMD_TYPE_STRING ? mvp_lmd_array_method_value(program, source, name) :
        mvp_lmd_class_property(source, name, ItemNull, LMD_PROP_GET, NULL));
    if (item_is_error(method.get())) return method.get();
    if (get_type_id(method.get()) != LMD_TYPE_FUNC) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    iterator.set(mvp_lmd_class_invoke(method.get(), owner.get(), NULL, 0, Item{.item = ITEM_JS_UNDEFINED}));
    if (item_is_error(iterator.get())) return iterator.get();
    if (!iterator_object(iterator.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    method.set(iterator_property(iterator.get(), "next"));
    if (item_is_error(method.get())) return method.get();
    if (get_type_id(method.get()) != LMD_TYPE_FUNC) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    record.set(mvp_lmd_array_new(0));
    if (item_is_error(record.get())) return record.get();
    Item stored = mvp_lmd_array_store(record.get(), 0, iterator.get());
    if (!item_is_error(stored)) stored = mvp_lmd_array_store(record.get(), 1, method.get());
    return item_is_error(stored) ? stored : record.get();
}
extern "C" Item mvp_lmd_iterator_step(Item record, Item value, int64_t count) {
    Item result = mvp_lmd_class_invoke(record.array->items[1], record.array->items[0], &value, count,
        Item{.item = ITEM_JS_UNDEFINED});
    return item_is_error(result) || iterator_object(result) ? result : mvp_lmd_fail(LMD_MVP_TYPE, 0);
}
extern "C" Item mvp_lmd_iterator_close(Item record, Item completion) {
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, record), pending(roots, completion), method(roots, iterator_property(record.array->items[0], "return"));
    if (method.get().item == ITEM_JS_UNDEFINED || method.get().item == ITEM_NULL) return pending.get();
    Item result = method.get();
    if (!item_is_error(result)) result = get_type_id(result) != LMD_TYPE_FUNC ? mvp_lmd_fail(LMD_MVP_TYPE, 0) :
        mvp_lmd_class_invoke(method.get(), held.get().array->items[0], NULL, 0, Item{.item = ITEM_JS_UNDEFINED});
    // An existing throw wins over a failing close, as in IteratorClose.
    if (item_is_error(pending.get())) return pending.get();
    if (item_is_error(result)) return result;
    return iterator_object(result) ? pending.get() : mvp_lmd_fail(LMD_MVP_TYPE, 0);
}
extern "C" Item mvp_lmd_iterator_collect(MvpLmdProgram* program, Item target, Item source, Item mapper, Item receiver) {
    RootFrame roots(6);
    if (!roots.valid()) return ItemError;
    Rooted<Item> output(roots, target), input(roots, source), callback(roots, mapper), self(roots, receiver),
        record(roots, ItemNull), result(roots, ItemNull);
    if (mapper.item != ITEM_JS_UNDEFINED && get_type_id(mapper) != LMD_TYPE_FUNC) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    record.set(mvp_lmd_iterator_get(program, input.get()));
    if (item_is_error(record.get())) return record.get();
    uint64_t scalar_home = 0;
    for (uint64_t index = 0;; index++) {
        result.set(mvp_lmd_iterator_step(record.get(), Item{.item = ITEM_JS_UNDEFINED}, 0));
        if (item_is_error(result.get())) return result.get();
        Item done = iterator_property(result.get(), "done");
        if (item_is_error(done)) return done;
        if (mvp_lmd_truthy(done)) return output.get();
        result.set(lambda_item_adopt_scalar_home(iterator_property(result.get(), "value"), &scalar_home));
        if (item_is_error(result.get())) return result.get();
        if (callback.get().item != ITEM_JS_UNDEFINED) {
            Item args[] = {result.get(), Item{.item = i2it(index)}};
            result.set(mvp_lmd_class_invoke(callback.get(), self.get(), args, 2, Item{.item = ITEM_JS_UNDEFINED}));
            result.set(lambda_item_adopt_scalar_home(result.get(), &scalar_home));
            if (item_is_error(result.get())) return mvp_lmd_iterator_close(record.get(), result.get());
        }
        Item stored = output.get().array->length >= UINT32_MAX ? mvp_lmd_fail(LMD_MVP_RANGE, 0) :
            mvp_lmd_array_store(output.get(), output.get().array->length, result.get());
        if (item_is_error(stored)) return mvp_lmd_iterator_close(record.get(), stored);
    }
}

static Item generator_result(MvpLmdProgram* program, Item value, bool done) {
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    uint64_t home = 0;
    Rooted<Item> held(roots, lambda_item_adopt_scalar_home(value, &home));
    Rooted<Item> object(roots, mvp_lmd_object_new(mvp_lmd_program_library(program)->object_shape, 0));
    if (item_is_error(object.get())) return object.get();
    Item stored = mvp_lmd_named_set(object.get(), "value", held.get());
    if (!item_is_error(stored)) stored = mvp_lmd_named_set(object.get(), "done", Item{.item = b2it(done)});
    return item_is_error(stored) ? stored : object.get();
}
static Item iterator_identity(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item receiver, Item) {
    return receiver;
}
static Item sequence_iterator(Context*, MvpLmdProgram* program, Item*, uint64_t, Item self, Item receiver, Item) {
    TypeId type = get_type_id(receiver);
    int code = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    int projection = code & 31;
    if (projection ? type != LMD_TYPE_MAP || receiver.map->map_kind != MAP_KIND_ORDERED :
            type != LMD_TYPE_ARRAY && type != LMD_TYPE_STRING) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (projection && mvp_lmd_class_record(receiver) != (code & 32 ? &mvp_lmd_program_library(program)->set :
            &mvp_lmd_program_library(program)->map)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, receiver);
    JsIteratorMapCarrier* carrier = (JsIteratorMapCarrier*)heap_calloc(sizeof(JsIteratorMapCarrier), LMD_TYPE_MAP);
    if (!carrier) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    carrier->base.type_id = LMD_TYPE_MAP; carrier->base.map_kind = MAP_KIND_ITERATOR;
    carrier->base.type = &mvp_lmd_program_library(program)->iterator.shape;
    carrier->payload.source = held.get();
    carrier->payload.ordered_projection = projection;
    if (projection) ((OrderedMap*)held.get().map)->cursors++;
    return Item{.map = &carrier->base};
}
static Item sequence_next(Context*, MvpLmdProgram* program, Item*, uint64_t, Item, Item receiver, Item) {
    if (get_type_id(receiver) != LMD_TYPE_MAP || receiver.map->map_kind != MAP_KIND_ITERATOR)
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, receiver), value(roots, Item{.item = ITEM_JS_UNDEFINED});
    JsIterData* data = &((JsIteratorMapCarrier*)receiver.map)->payload;
    bool done = data->source.item == ITEM_JS_UNDEFINED;
    if (!done && get_type_id(data->source) == LMD_TYPE_ARRAY) {
        done = data->index >= data->source.array->length;
        if (!done) {
            value.set(data->source.array->items[data->index++]);
            if (value.get().item == ITEM_JS_DELETED_SENTINEL) value.set(Item{.item = ITEM_JS_UNDEFINED});
        }
    } else if (!done && data->ordered_projection) {
        int64_t index = mvp_lmd_map_next(data->source, data->index);
        done = index < 0;
        if (!done) {
            value.set(mvp_lmd_map_entry(data->source, index, data->ordered_projection));
            if (item_is_error(value.get())) return value.get();
            data->index = index + 4;
        }
    } else if (!done) {
        Item code = mvp_lmd_string_at(data->source, data->index, LMD_STRING_CODE);
        if (item_is_error(code)) return code;
        done = isnan(it2d(code));
        if (!done) {
            int width = 1; double first = it2d(code);
            if (first >= 0xd800 && first <= 0xdbff) {
                Item low = mvp_lmd_string_at(data->source, data->index + 1, LMD_STRING_CODE);
                if (item_is_error(low)) return low;
                double second = it2d(low); if (second >= 0xdc00 && second <= 0xdfff) width = 2;
            }
            value.set(mvp_lmd_string_range(data->source, data->index, data->index + width, 1));
            data->index += width;
            if (item_is_error(value.get())) return value.get();
        }
    }
    if (done) {
        js_iterator_map_heap_destroy(held.get().map);
        data->source.item = ITEM_JS_UNDEFINED;
    }
    return generator_result(program, value.get(), done);
}
static Item generator_body(Activation*, Item object) {
    JsGeneratorStateRecord* state = &((JsGeneratorMapCarrier*)object.map)->state;
    MvpLmdNativeCallable* callable = (MvpLmdNativeCallable*)state->ast_function.function;
    return mvp_lmd_function_forward(state->ast_function, state->ast_this, state->ast_arguments,
        NULL, 0, 0, (MvpLmdNativeEntry)callable->state);
}
static Item generator_resume(Context*, MvpLmdProgram* program, Item* args, uint64_t count,
        Item self, Item receiver, Item) {
    if (get_type_id(receiver) != LMD_TYPE_MAP || receiver.map->map_kind != MAP_KIND_GENERATOR)
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    JsGeneratorStateRecord* state = &((JsGeneratorMapCarrier*)receiver.map)->state;
    if (state->runtime_context != context || state->executing) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    int operation = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    Item value = count ? args[0] : Item{.item = ITEM_JS_UNDEFINED};
    if (!state->started && operation) {
        state->done = true; activation_destroy(state->activation); state->activation = NULL;
    }
    if (state->done) return operation == 2 ? mvp_lmd_throw(value, 0) :
        generator_result(program, operation == 1 ? value : Item{.item = ITEM_JS_UNDEFINED}, true);
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, receiver), result(roots, ItemNull);
    uint64_t result_home = 0;
    state->started = state->executing = true; state->state = operation;
    ActivationStatus status = activation_resume(state->activation, value);
    result.set(lambda_item_adopt_scalar_home(activation_value(state->activation), &result_home)); state->executing = false;
    state->done = status == ACTIVATION_DONE;
    bool delegated_result = state->delegate.item && state->delegate.item != ITEM_NULL;
    if (activation_fault(state->activation)) result.set(mvp_lmd_fail(LMD_MVP_RANGE, 0));
    if (state->done) { activation_destroy(state->activation); state->activation = NULL; }
    return item_is_error(result.get()) || delegated_result ? result.get() : generator_result(program, result.get(), state->done);
}
Item mvp_lmd_generator_call(Context*, MvpLmdProgram* program, Item* args, uint64_t count,
        Item self, Item receiver, Item target) {
    if (target.item != ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> function(roots, self), object(roots, ItemNull), owner(roots, receiver), arguments(roots, mvp_lmd_array_new(0));
    if (item_is_error(arguments.get())) return arguments.get();
    for (uint64_t i = 0; i < count; i++) {
        Item stored = mvp_lmd_array_store(arguments.get(), i, args[i]);
        if (item_is_error(stored)) return stored;
    }
    Item prepared = mvp_lmd_function_prepare(function.get());
    if (item_is_error(prepared)) return prepared;
    MvpLmdClass* cls = ((MvpLmdCallable*)function.get().function)->properties;
    JsGeneratorMapCarrier* carrier = (JsGeneratorMapCarrier*)heap_calloc(sizeof(JsGeneratorMapCarrier), LMD_TYPE_MAP);
    if (!carrier) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    carrier->base.type_id = LMD_TYPE_MAP; carrier->base.map_kind = MAP_KIND_GENERATOR;
    carrier->base.type = &cls->shape; object.set(Item{.map = &carrier->base});
    JsGeneratorStateRecord* state = &carrier->state;
    durable_activation_init(state, LMD_TYPE_MAP, DURABLE_ACTIVATION_JS_GENERATOR);
    state->runtime_context = context; state->ast_function = function.get();
    state->ast_arguments = arguments.get(); state->ast_this = owner.get();
    state->activation = activation_create(generator_body, object.get(), false, state);
    if (!state->activation) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    // Parameter initialization runs at call time; the MIR prologue parks before the body.
    state->executing = true;
    ActivationStatus status = activation_resume(state->activation, Item{.item = ITEM_JS_UNDEFINED});
    state->executing = false;
    if (status != ACTIVATION_SUSPENDED) {
        Item result = activation_value(state->activation);
        if (activation_fault(state->activation)) result = mvp_lmd_fail(LMD_MVP_RANGE, 0);
        state->done = true; activation_destroy(state->activation); state->activation = NULL;
        return item_is_error(result) ? result : mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    }
    return object.get();
}
extern "C" Item mvp_lmd_generator_park(Item value, int64_t kind) {
    Activation* activation = activation_current();
    if (!activation || !activation_user(activation)) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    if (kind == 2) {
        JsGeneratorStateRecord* state = (JsGeneratorStateRecord*)activation_user(activation);
        MvpLmdProgram* program = ((MvpLmdCallable*)state->ast_function.function)->program;
        RootFrame roots(4);
        if (!roots.valid()) return ItemError;
        Rooted<Item> record(roots, mvp_lmd_iterator_get(program, value)), result(roots, ItemNull),
            received(roots, Item{.item = ITEM_JS_UNDEFINED}), method(roots, ItemNull);
        uint64_t received_home = 0;
        if (item_is_error(record.get())) return record.get();
        int operation = 0;
        for (;;) {
            if (!operation) result.set(mvp_lmd_iterator_step(record.get(), received.get(), 1));
            else {
                method.set(iterator_property(record.get().array->items[0], operation == 1 ? "return" : "throw"));
                if (item_is_error(method.get())) return method.get();
                if (method.get().item == ITEM_NULL || method.get().item == ITEM_JS_UNDEFINED) {
                    if (operation == 1) return received.get();
                    Item closed = mvp_lmd_iterator_close(record.get(), Item{.item = ITEM_JS_UNDEFINED});
                    return item_is_error(closed) ? closed : mvp_lmd_fail(LMD_MVP_TYPE, 0);
                }
                if (get_type_id(method.get()) != LMD_TYPE_FUNC) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
                Item argument = received.get();
                result.set(mvp_lmd_class_invoke(method.get(), record.get().array->items[0], &argument, 1,
                    Item{.item = ITEM_JS_UNDEFINED}));
            }
            if (item_is_error(result.get())) return result.get();
            if (!iterator_object(result.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            Item done = iterator_property(result.get(), "done");
            if (item_is_error(done)) return done;
            if (mvp_lmd_truthy(done)) {
                state->state = operation == 1 ? 1 : 0;
                return iterator_property(result.get(), "value");
            }
            // Delegation exposes the delegate's result object itself, including extra fields.
            state->delegate = result.get();
            received.set(lambda_item_adopt_scalar_home(activation_suspend(result.get()), &received_home));
            state->delegate = ItemNull; operation = state->state;
        }
    }
    return activation_suspend(value);
}
extern "C" int64_t mvp_lmd_generator_resume_kind() {
    JsGeneratorStateRecord* state = (JsGeneratorStateRecord*)activation_user(activation_current());
    return state ? state->state : 0;
}
static Item generator_construct(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item) {
    return mvp_lmd_fail(LMD_MVP_TYPE, 0);
}
static Item collection_method(Context*, MvpLmdProgram* program, Item* args, uint64_t count, Item self, Item receiver, Item) {
    int code = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    MvpLmdLibraryState* library = mvp_lmd_program_library(program);
    MvpLmdClass* cls = code & 32 ? &library->set : &library->map;
    if (get_type_id(receiver) != LMD_TYPE_MAP || receiver.map->map_kind != MAP_KIND_ORDERED ||
            mvp_lmd_class_record(receiver) != cls) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    int method = code & 31;
    Item key = count ? args[0] : Item{.item = ITEM_JS_UNDEFINED};
    return mvp_lmd_map_call(receiver, Item{.item = mvp_lmd_method_token(method)}, key,
        code & 32 ? key : count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED});
}
static Item collection_construct(Context*, MvpLmdProgram* program, Item* args, uint64_t count, Item self, Item, Item target) {
    if (target.item == ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    MvpLmdClass* cls = mvp_lmd_class_record(self);
    bool set = cls == &mvp_lmd_program_library(program)->set;
    RootFrame roots(6);
    if (!roots.valid()) return ItemError;
    Rooted<Item> output(roots, mvp_lmd_object_new(&cls->shape, 1)), record(roots, ItemNull),
        result(roots, ItemNull), adder(roots, ItemNull), key(roots, ItemNull), value(roots, ItemNull);
    if (item_is_error(output.get()) || !count || args[0].item == ITEM_NULL || args[0].item == ITEM_JS_UNDEFINED)
        return output.get();
    adder.set(iterator_property(output.get(), set ? "add" : "set"));
    if (item_is_error(adder.get())) return adder.get();
    if (get_type_id(adder.get()) != LMD_TYPE_FUNC) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    record.set(mvp_lmd_iterator_get(program, args[0]));
    if (item_is_error(record.get())) return record.get();
    for (;;) {
        result.set(mvp_lmd_iterator_step(record.get(), Item{.item = ITEM_JS_UNDEFINED}, 0));
        if (item_is_error(result.get())) return result.get();
        Item done = iterator_property(result.get(), "done");
        if (item_is_error(done)) return done;
        if (mvp_lmd_truthy(done)) return output.get();
        result.set(iterator_property(result.get(), "value"));
        if (item_is_error(result.get())) return result.get();
        if (!set && !iterator_object(result.get())) return mvp_lmd_iterator_close(record.get(), mvp_lmd_fail(LMD_MVP_TYPE, 0));
        uint64_t homes[2] = {};
        key.set(result.get()); value.set(ItemNull);
        if (!set) {
            key.set(lambda_item_adopt_scalar_home(iterator_property(result.get(), "0"), &homes[0]));
            if (item_is_error(key.get())) return mvp_lmd_iterator_close(record.get(), key.get());
            value.set(lambda_item_adopt_scalar_home(iterator_property(result.get(), "1"), &homes[1]));
            if (item_is_error(value.get())) return mvp_lmd_iterator_close(record.get(), value.get());
        }
        Item values[2] = {key.get(), value.get()};
        Item added = mvp_lmd_class_invoke(adder.get(), output.get(), values, set ? 1 : 2, Item{.item = ITEM_JS_UNDEFINED});
        if (item_is_error(added)) return mvp_lmd_iterator_close(record.get(), added);
    }
}
Item mvp_lmd_generator_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state) {
    Item result = mvp_lmd_library_class_initialize(program, state, &state->generator,
        LMD_LIBRARY_GENERATOR, "Generator", generator_construct, 0);
    if (item_is_error(result)) return result;
    const char* names[] = {"next", "return", "throw"};
    for (int i = 0; i < 3; i++) {
        result = mvp_lmd_named_set(state->values[LMD_LIBRARY_GENERATOR_PROTOTYPE], names[i],
            mvp_lmd_native_function(program, (void*)(intptr_t)i, generator_resume, 1));
        if (item_is_error(result)) return result;
    }
    result = mvp_lmd_library_class_initialize(program, state, &state->iterator,
        LMD_LIBRARY_ITERATOR, "Iterator", generator_construct, 0);
    if (item_is_error(result)) return result;
    result = mvp_lmd_named_set(state->values[LMD_LIBRARY_ITERATOR_PROTOTYPE], "next",
        mvp_lmd_native_function(program, NULL, sequence_next, 0));
    if (item_is_error(result)) return result;
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> identity(roots, mvp_lmd_native_function(program, NULL, iterator_identity, 0));
    if (item_is_error(identity.get())) return identity.get();
    NameRef key = well_known_name_ref(JS_SYMBOL_ITERATOR);
    const int prototypes[] = {LMD_LIBRARY_GENERATOR_PROTOTYPE, LMD_LIBRARY_ITERATOR_PROTOTYPE};
    for (int index : prototypes)
        if (!map_shape_set(state->values[index].map, key, identity.get())) return ItemError;
    state->values[LMD_LIBRARY_SEQUENCE_ITERATOR] = mvp_lmd_native_function(program, NULL, sequence_iterator, 0);
    if (item_is_error(state->values[LMD_LIBRARY_SEQUENCE_ITERATOR])) return state->values[LMD_LIBRARY_SEQUENCE_ITERATOR];
    if (!map_shape_set((Map*)state->values[LMD_LIBRARY_ARRAY_PROTOTYPE].array, key,
            state->values[LMD_LIBRARY_SEQUENCE_ITERATOR])) return ItemError;
    for (int set = 0; set < 2; set++) {
        int slot = set ? LMD_LIBRARY_SET : LMD_LIBRARY_MAP;
        result = mvp_lmd_library_class_initialize(program, state, set ? &state->set : &state->map,
            slot, set ? "Set" : "Map", collection_construct, 0);
        if (item_is_error(result)) return result;
        const char* names[] = {"get", set ? "add" : "set", "has", "delete", "clear", "entries", "keys", "values"};
        for (int i = set ? 1 : 0; i < 8; i++) {
            int projection = set && i == 6 ? 8 : i + 1;
            Item callable = mvp_lmd_native_function(program, (void*)(intptr_t)(set * 32 + (i < 5 ? i + 1 : projection)),
                i < 5 ? collection_method : sequence_iterator, i < 4 ? (!set && i == 1 ? 2 : 1) : 0);
            result = mvp_lmd_named_set(state->values[slot + 1], names[i], callable);
            if (item_is_error(result)) return result;
        }
        Item method = iterator_property(state->values[slot + 1], set ? "values" : "entries");
        if (item_is_error(method) || !map_shape_set(state->values[slot + 1].map, key, method)) return ItemError;
        if (set) {
            result = mvp_lmd_named_set(state->values[slot + 1], "keys", method);
            if (item_is_error(result)) return result;
        }
    }
    return ItemNull;
}
