#include "mvp_lmd_runtime.h"
#include "../../runtime/suspended_activation.hpp"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/runtime-state.h"
#include "../../runtime/heap_api.h"
#include "lambda/core/js_well_known_names.h"
#include "../js_typed_array.h"
#include "../js_runtime_state.hpp"
#include "../js_runtime.h"
#include "../js_runtime_internal.hpp"
#include "../js_interp.hpp"
#include "../js_builtin_catalog.hpp"
#include "../js_property_attrs.h"
#include "../js_props.h"
#include "lib/mem.h"

const char* const mvp_lmd_iterator_names[LMD_ITERATOR_COUNT] = {"Map Iterator", "Set Iterator", "String Iterator"};
static const char* const generator_resume_names[] = {"next", "return", "throw"};

static Item sequence_next(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);
static Item sequence_next_value(MvpLmdProgram*, Item, Item, bool*);
static Item generator_resume(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);
static Item generator_resume_impl(MvpLmdProgram*, Item, Item, int, bool, bool = true);
static thread_local JsCompactGeneratorFrame* active_compact_frame = NULL;
static thread_local unsigned compact_delegate_depth = 0;
static Item generator_delegate_drive(JsGeneratorStateRecord*, Item, Item, int, bool);
static Item compact_delegate_body(Activation*, Item);

static bool compact_contains_yield(JsAstNode* node, void*) {
    return node && (node->node_type == AST_NODE_YIELD ||
        js_ast_any_child(node, compact_contains_yield, NULL));
}

int mvp_lmd_generator_compact_kind(const AstFuncNode* definition) {
    if (!mvp_lmd_generator_lazy_parameters(definition) || !definition->is_generator ||
            !definition->body || definition->body->node_type != AST_NODE_BLOCK) return 0;
    AstNode* statement = ((AstBlockNode*)definition->body)->statements;
    if (!statement) return 0;
    // earlier statements run to completion before the final suspension.
    while (statement->next) {
        if (compact_contains_yield((JsAstNode*)statement, NULL)) return 0;
        statement = statement->next;
    }
    if (statement->node_type != AST_NODE_EXPR_STMT) return 0;
    AstNode* expression = ((AstExprStmtNode*)statement)->expression;
    if (!expression || expression->node_type != AST_NODE_YIELD) return 0;
    AstYieldNode* yield = (AstYieldNode*)expression;
    if (yield->argument && compact_contains_yield((JsAstNode*)yield->argument, NULL)) return 0;
    return yield->delegate ? 2 : 1;
}

extern "C" Item mvp_lmd_compact_generator_park(Item value, int64_t kind) {
    JsCompactGeneratorFrame* frame = active_compact_frame;
    if (!frame || frame->position) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    frame->delegates = kind == 2;
    Item saved = frame->delegates ? mvp_lmd_iterator_get(frame->program, value) : value;
    if (item_is_error(saved)) return saved;
    frame->saved = lambda_item_adopt_scalar_home(saved, &frame->scalar_home);
    frame->position = frame->delegates ? 2 : 1;
    return Item{.item = ITEM_JS_UNDEFINED};
}

static bool iterator_object(Item value) {
    return mvp_lmd_is_object(value);
}
static Item iterator_property(Item owner, const char* name) {
    String* key = heap_create_name(name, strlen(name));
    return key ? mvp_lmd_class_property(owner, Item{.item = s2it(key)}, ItemNull, LMD_PROP_GET) : ItemError;
}
extern "C" Item mvp_lmd_iterator_get(MvpLmdProgram* program, Item source) {
    return mvp_lmd_iterator_get_with_method(program, source, ItemError);
}
extern "C" Item mvp_lmd_iterator_method(MvpLmdProgram* program, Item source) {
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, source), boxed(roots, ItemNull);
    Item name = {.item = s2it(well_known_name_ref(JS_SYMBOL_ITERATOR))};
    TypeId type = get_type_id(source);
    Item method;
    if (!iterator_object(source) && type != LMD_TYPE_STRING) {
        // GetMethod boxes the lookup base while getters receive the original primitive.
        boxed.set(mvp_lmd_box_primitive(owner.get()));
        if (item_is_error(boxed.get())) return boxed.get();
        method = mvp_lmd_property_with_receiver(boxed.get(), name, ItemNull, LMD_PROP_GET, owner.get());
    } else method = type == LMD_TYPE_ARRAY || type == LMD_TYPE_STRING ?
        mvp_lmd_array_method_value(program, owner.get(), name) :
        mvp_lmd_class_property(owner.get(), name, ItemNull, LMD_PROP_GET);
    if (item_is_error(method)) return method;
    if (method.item == ITEM_NULL || method.item == ITEM_JS_UNDEFINED) return Item{.item = ITEM_JS_UNDEFINED};
    return mvp_lmd_is_callable(method) ? method : mvp_lmd_fail(LMD_MVP_TYPE, 0);
}
extern "C" Item mvp_lmd_iterator_get_with_method(MvpLmdProgram* program, Item source, Item supplied_method) {
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, source), method(roots, supplied_method), iterator(roots, ItemNull), record(roots, ItemNull);
    if (supplied_method.item == ITEM_ERROR) method.set(mvp_lmd_iterator_method(program, owner.get()));
    if (item_is_error(method.get())) return method.get();
    if (!mvp_lmd_is_callable(method.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    iterator.set(mvp_lmd_class_invoke(method.get(), owner.get(), NULL, 0, Item{.item = ITEM_JS_UNDEFINED}));
    if (item_is_error(iterator.get())) return iterator.get();
    if (!iterator_object(iterator.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    method.set(iterator_property(iterator.get(), "next"));
    if (item_is_error(method.get())) return method.get();
    // GetIterator retains next without calling it; a pattern can close before its first step.
    // this private record always has both slots; avoid growth on the first store.
    record.set(mvp_lmd_array_new(2));
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
extern "C" Item mvp_lmd_iterator_step_value(Item record) {
    return mvp_lmd_iterator_step_read_value(record, true);
}
static MvpLmdProgram* generator_intrinsic_program(Item method, Item receiver, int operation) {
    if (get_type_id(receiver) != LMD_TYPE_MAP || receiver.map->map_kind != MAP_KIND_GENERATOR ||
            get_type_id(method) != LMD_TYPE_FUNC) return NULL;
    auto* state = &((JsGeneratorMapCarrier*)receiver.map)->state;
    Item implementation = js_generator_implementation(state);
    if (state->is_async || state->ast_host || state->runtime_context != context ||
            get_type_id(implementation) != LMD_TYPE_FUNC ||
            !function_has_abi(implementation.function, FN_ENTRY_ABI_MVP_LMD)) return NULL;
    if (function_has_abi(method.function, FN_ENTRY_ABI_MVP_LMD) &&
            method.function->ptr == (fn_ptr)generator_resume &&
            method.function->requires_runtime_context && method.function->runtime_context == context &&
            (intptr_t)((MvpLmdNativeCallable*)method.function)->state == operation)
        return ((MvpLmdCallable*)method.function)->program;
    const char* name = generator_resume_names[operation];
    if (function_has_abi(method.function, FN_ENTRY_ABI_JS_FUNCTION) &&
            method.item == js_intrinsic_binding_get(JS_BUILTIN_OWNER_GENERATOR_INTERNAL, name, strlen(name)).item)
        return ((MvpLmdCallable*)implementation.function)->program;
    return NULL;
}
extern "C" Item mvp_lmd_iterator_step_read_value(Item record, int64_t read_value) {
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, record), result(roots, ItemNull);
    Item next = held.get().array->items[1];
    Item receiver = held.get().array->items[0];
    if (MvpLmdProgram* program = generator_intrinsic_program(next, receiver, 0)) {
        // borrowed intrinsics own validation errors in their callable's realm (D8.4.4).
        MvpLmdRealmScope realm(program);
        return realm.complete(generator_resume_impl(program, receiver,
            Item{.item = ITEM_JS_UNDEFINED}, 0, false, read_value != 0));
    }
    if (get_type_id(next) == LMD_TYPE_FUNC && function_has_abi(next.function, FN_ENTRY_ABI_MVP_LMD) &&
            next.function->requires_runtime_context && next.function->runtime_context == context &&
            next.function->ptr == (fn_ptr)sequence_next) {
        // cached intrinsic next owns this protocol; its result object cannot escape a value-only step.
        auto* function = (MvpLmdCallable*)next.function;
        MvpLmdRealmScope realm(function->program);
        bool done = false;
        result.set(sequence_next_value(function->program, next, held.get().array->items[0], &done));
        return realm.complete(item_is_error(result.get()) ? result.get() : done ?
            Item{.item = JS_ITER_DONE_SENTINEL} : read_value ? result.get() : Item{.item = ITEM_JS_UNDEFINED});
    }
    result.set(mvp_lmd_iterator_step(held.get(), Item{.item = ITEM_JS_UNDEFINED}, 0));
    if (item_is_error(result.get())) return result.get();
    Item done = iterator_property(result.get(), "done");
    if (item_is_error(done)) return done;
    if (mvp_lmd_truthy(done)) return Item{.item = JS_ITER_DONE_SENTINEL};
    // elisions still execute next and observe done, but must not read a custom value getter.
    return read_value ? iterator_property(result.get(), "value") : Item{.item = ITEM_JS_UNDEFINED};
}
extern "C" Item mvp_lmd_iterator_close(Item record, Item completion) {
    return mvp_lmd_iterator_close_mode(record, completion, false);
}
extern "C" Item mvp_lmd_iterator_close_mode(Item record, Item completion, int64_t asynchronous) {
    if (asynchronous) {
        RootFrame roots(3);
        if (!roots.valid()) return ItemError;
        uint64_t completion_home = 0;
        Rooted<Item> held(roots, record), pending(roots, lambda_item_adopt_scalar_home(completion, &completion_home)),
            result(roots, js_async_iterator_record_close_result(held.get()));
        if (!item_is_error(result.get()) && js_async_iterator_close_needs_await(result.get())) {
            DurableActivation* frame = (DurableActivation*)activation_user(activation_current());
            result.set(frame && frame->kind == DURABLE_ACTIVATION_JS_GENERATOR
                ? js_gen_park(result.get(), JS_GEN_PARK_AWAIT) : js_await_park(result.get()));
            if (!item_is_error(pending.get()) && !item_is_error(result.get()) && !iterator_object(result.get()))
                result.set(mvp_lmd_fail(LMD_MVP_TYPE, 0));
        }
        return js_retain_scalar(item_is_error(pending.get()) ? pending.get() :
            item_is_error(result.get()) ? result.get() : pending.get());
    }
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, record), pending(roots, completion), method(roots, iterator_property(record.array->items[0], "return"));
    if (method.get().item == ITEM_JS_UNDEFINED || method.get().item == ITEM_NULL) return pending.get();
    Item result = method.get();
    if (!item_is_error(result)) result = !mvp_lmd_is_callable(result) ? mvp_lmd_fail(LMD_MVP_TYPE, 0) :
        mvp_lmd_class_invoke(method.get(), held.get().array->items[0], NULL, 0, Item{.item = ITEM_JS_UNDEFINED});
    // An existing throw wins over a failing close, as in IteratorClose.
    if (item_is_error(pending.get())) return pending.get();
    if (item_is_error(result)) return result;
    return iterator_object(result) ? pending.get() : mvp_lmd_fail(LMD_MVP_TYPE, 0);
}
extern "C" Item mvp_lmd_iterator_collect(MvpLmdProgram* program, Item target, Item source, Item mapper, Item receiver) {
    return mvp_lmd_iterator_collect_with_method(program, target, source, mapper, receiver, ItemError);
}
extern "C" Item mvp_lmd_iterator_collect_with_method(MvpLmdProgram* program, Item target, Item source,
        Item mapper, Item receiver, Item method, int64_t array_length, bool create_properties,
        Item acquired_record) {
    RootFrame roots(6);
    if (!roots.valid()) return ItemError;
    Rooted<Item> output(roots, target), input(roots, source), callback(roots, mapper), self(roots, receiver),
        record(roots, acquired_record), result(roots, ItemNull);
    if (mapper.item != ITEM_JS_UNDEFINED && !mvp_lmd_is_callable(mapper)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    bool iterable = array_length < 0;
    if (iterable && record.get().item == ITEM_NULL) {
        // rest binding continues the existing record and its cached next;
        // an ordinary collection still performs GetIterator exactly once.
        record.set(mvp_lmd_iterator_get_with_method(program, input.get(), method));
        if (item_is_error(record.get())) return record.get();
    }
    auto abrupt = [&](Item error) { return iterable ? mvp_lmd_iterator_close(record.get(), error) : error; };
    uint64_t scalar_home = 0;
    for (uint64_t index = 0;; index++) {
        if (create_properties && iterable && index >= (uint64_t)INT53_MAX)
            return abrupt(mvp_lmd_fail(LMD_MVP_TYPE, 0));
        bool done = !iterable && index >= (uint64_t)array_length;
        if (iterable) {
            result.set(mvp_lmd_iterator_step(record.get(), Item{.item = ITEM_JS_UNDEFINED}, 0));
            if (item_is_error(result.get())) return result.get();
            Item finished = iterator_property(result.get(), "done");
            if (item_is_error(finished)) return finished;
            done = mvp_lmd_truthy(finished);
        }
        if (done) {
            if (create_properties) {
                Item stored = mvp_lmd_property_with_receiver(output.get(),
                    Item{.item = s2it(heap_create_name("length", 6))}, Item{.item = i2it(index)},
                    LMD_PROP_SET | LMD_PROP_STRICT, output.get());
                if (item_is_error(stored)) return stored;
            }
            return output.get();
        }
        result.set(lambda_item_adopt_scalar_home(iterable ? iterator_property(result.get(), "value") :
            mvp_lmd_array_index_property(input.get(), index, ItemNull, LMD_PROP_GET), &scalar_home));
        if (item_is_error(result.get())) return result.get();
        if (callback.get().item != ITEM_JS_UNDEFINED) {
            Item args[] = {result.get(), Item{.item = i2it(index)}};
            result.set(mvp_lmd_class_invoke(callback.get(), self.get(), args, 2, Item{.item = ITEM_JS_UNDEFINED}));
            result.set(lambda_item_adopt_scalar_home(result.get(), &scalar_home));
            if (item_is_error(result.get())) return abrupt(result.get());
        }
        Item stored = create_properties ? mvp_lmd_array_index_property(output.get(), index, result.get(), LMD_PROP_CREATE_DATA) :
            output.get().array->length >= UINT32_MAX ? mvp_lmd_fail(LMD_MVP_RANGE, 0) :
            mvp_lmd_array_store(output.get(), output.get().array->length, result.get());
        if (item_is_error(stored)) return abrupt(stored);
    }
}

Item mvp_lmd_iterator_result(MvpLmdProgram* program, Item value, bool done) {
    Item prepared = js_try_make_iter_result(mvp_lmd_program_library(program)->object_shape, value, done);
    if (prepared.item != ITEM_NULL) return prepared;
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
Item mvp_lmd_receiver_identity(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item receiver, Item) {
    return receiver;
}
static Item sequence_iterator(Context*, MvpLmdProgram* program, Item*, uint64_t, Item self, Item receiver, Item) {
    int code = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    int projection = code & 31;
    // array keys/entries are generic ToObject operations; collection projections
    // (6..8) retain their branded ordered-map admission.
    bool ordered = projection >= 6;
    if (code & 64) {
        if (receiver.item == ITEM_NULL || receiver.item == ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        Rooted<Item> text(roots, mvp_lmd_primitive_to_string(receiver, 1));
        return item_is_error(text.get()) ? text.get() :
            mvp_lmd_sequence_iterator_create(program, text.get(), 0, false, false, true);
    }
    if (!ordered && !mvp_lmd_is_object(receiver)) {
        Item object = mvp_lmd_box_primitive(receiver);
        return item_is_error(object) ? object :
            mvp_lmd_sequence_iterator_create(program, object, projection, false, false);
    }
    return mvp_lmd_sequence_iterator_create(program, receiver, projection, ordered, code & 32);
}
extern "C" Item mvp_lmd_sequence_iterator_create(MvpLmdProgram* program, Item receiver,
        int projection, bool ordered, bool set, bool string) {
    TypeId type = get_type_id(receiver);
    if (ordered ? type != LMD_TYPE_MAP || receiver.map->map_kind != MAP_KIND_ORDERED :
            type != LMD_TYPE_STRING && !mvp_lmd_is_object(receiver)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (ordered && !lambda_nominal_derives_from(((TypeMap*)receiver.map->type)->nominal,
            set ? &mvp_lmd_program_library(program)->set.nominal : &mvp_lmd_program_library(program)->map.nominal))
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, receiver);
    JsIteratorMapCarrier* carrier = (JsIteratorMapCarrier*)heap_calloc(sizeof(JsIteratorMapCarrier), LMD_TYPE_MAP);
    if (!carrier) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    carrier->base.type_id = LMD_TYPE_MAP; carrier->base.map_kind = MAP_KIND_ITERATOR;
    MvpLmdLibraryState* library = mvp_lmd_program_library(program);
    carrier->base.type = (ordered || string) && library->iterator_families
        ? &library->iterator_families[string ? LMD_ITERATOR_STRING : set ? LMD_ITERATOR_SET : LMD_ITERATOR_MAP].shape
        : &library->iterator.shape;
    carrier->payload.source = held.get();
    carrier->payload.ordered_projection = ordered ? projection : 0;
    carrier->payload.sequence_projection = ordered ? 0 : projection;
    if (ordered) ((OrderedMap*)held.get().map)->cursors++;
    return Item{.map = &carrier->base};
}
static Item sequence_next_value(MvpLmdProgram* program, Item self, Item receiver, bool* finished) {
    if (get_type_id(receiver) != LMD_TYPE_MAP || receiver.map->map_kind != MAP_KIND_ITERATOR)
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    MvpLmdClass* family = (MvpLmdClass*)((MvpLmdNativeCallable*)self.function)->state;
    // Physical iterator branding survives exhaustion and mutable prototype links.
    if (family && ((TypeMap*)receiver.map->type)->nominal != &family->nominal)
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, receiver), value(roots, Item{.item = ITEM_JS_UNDEFINED});
    JsIterData* data = &((JsIteratorMapCarrier*)receiver.map)->payload;
    bool done = data->source.item == ITEM_JS_UNDEFINED;
    if (!done && !data->ordered_projection && get_type_id(data->source) != LMD_TYPE_STRING) {
        bool typed = js_is_typed_array(data->source);
        if (typed && js_typed_array_is_out_of_bounds_item(data->source)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        Item extent = typed ? Item{.item = i2it(js_typed_array_length(data->source))} : mvp_lmd_sequence_length(data->source);
        if (item_is_error(extent)) return extent;
        data = &((JsIteratorMapCarrier*)held.get().map)->payload;
        done = data->index >= it2i(extent);
        if (!done) {
            int64_t index = data->index++;
            int projection = data->sequence_projection;
            value.set(projection == 1 ? Item{.item = i2it(index)} :
                mvp_lmd_array_index_property(data->source, index, ItemNull, LMD_PROP_GET));
            if (item_is_error(value.get())) return value.get();
            if (value.get().item == ITEM_JS_DELETED_SENTINEL) value.set(Item{.item = ITEM_JS_UNDEFINED});
            if (projection == 2) {
                // the pair owns its scalar payload before the iterator result allocates again.
                RootFrame pair_roots(1);
                if (!pair_roots.valid()) return ItemError;
                uint64_t value_home = 0;
                value.set(lambda_item_adopt_scalar_home(value.get(), &value_home));
                Rooted<Item> pair(pair_roots, mvp_lmd_array_new(0));
                if (item_is_error(pair.get())) return pair.get();
                Item stored = mvp_lmd_array_store(pair.get(), 0, Item{.item = i2it(index)});
                if (!item_is_error(stored)) stored = mvp_lmd_array_store(pair.get(), 1, value.get());
                if (item_is_error(stored)) return stored;
                value.set(pair.get());
            }
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
        ((JsIteratorMapCarrier*)held.get().map)->payload.source.item = ITEM_JS_UNDEFINED;
    }
    *finished = done;
    return value.get();
}
static Item sequence_next(Context*, MvpLmdProgram* program, Item*, uint64_t, Item self, Item receiver, Item) {
    bool done = false;
    Item value = sequence_next_value(program, self, receiver, &done);
    return item_is_error(value) ? value : mvp_lmd_iterator_result(program, value, done);
}
static Item generator_run_body(JsGeneratorStateRecord* state) {
    MvpLmdNativeCallable* callable = (MvpLmdNativeCallable*)js_generator_implementation(state).function;
    auto run = [&]() {
        return mvp_lmd_function_forward(state->ast_function, state->ast_this, state->ast_arguments,
            NULL, 0, 0, (MvpLmdNativeEntry)callable->state,
            Item{.item = ITEM_JS_UNDEFINED}, nullptr, callable->program);
    };
    if (!js_active_runtime_state) return run();
    // Promise/bridge callbacks can be the first resumer; their native call chain is temporary.
    JsOwnedCallActivation call_facts;
    return run();
}
static Item generator_body(Activation*, Item object) {
    return generator_run_body(&((JsGeneratorMapCarrier*)object.map)->state);
}
static Item compact_delegate_body(Activation*, Item object) {
    JsGeneratorStateRecord* state = &((JsGeneratorMapCarrier*)object.map)->state;
    JsCompactGeneratorFrame* frame = state->compact;
    return generator_delegate_drive(state, frame->saved,
        frame->pending, frame->pending_operation, false);
}

static Item generator_resume_activation(JsGeneratorStateRecord* state, Item receiver,
        Item input, bool* finished, uint64_t* output_home) {
    if (!state->activation) {
        ActivationEntry body = state->compact && state->compact->position == 4
            ? compact_delegate_body : generator_body;
        state->activation = activation_create(body, receiver, false, state);
    }
    if (!state->activation) { *finished = true; return mvp_lmd_fail(LMD_MVP_MEMORY, 0); }
    // each activation has a separate native stack and its own delegation depth.
    unsigned previous_depth = compact_delegate_depth;
    compact_delegate_depth = 0;
    ActivationStatus status = activation_resume(state->activation, input);
    compact_delegate_depth = previous_depth;
    Item result = lambda_item_adopt_scalar_home(activation_value(state->activation), output_home);
    *finished = status == ACTIVATION_DONE;
    if (activation_fault(state->activation)) result = mvp_lmd_fail(LMD_MVP_RANGE, 0);
    if (*finished) {
        activation_destroy(state->activation); state->activation = NULL;
        if (state->compact && state->compact->position == 4 && !item_is_error(result) && state->state != 1)
            result = Item{.item = ITEM_JS_UNDEFINED};
    }
    return result;
}

// callers root the record/input and result across these observable protocol accesses.
static Item generator_delegate_request(JsGeneratorStateRecord* state, Item record,
        Item received, int operation, bool* finished) {
    *finished = false;
    Item method = operation ? iterator_property(record.array->items[0], generator_resume_names[operation])
        : record.array->items[1];
    if (item_is_error(method)) return method;
    if (operation && (method.item == ITEM_NULL || method.item == ITEM_JS_UNDEFINED)) {
        *finished = true;
        if (operation == 1) { state->state = 1; return received; }
        Item closed = mvp_lmd_iterator_close(record, Item{.item = ITEM_JS_UNDEFINED});
        return item_is_error(closed) ? closed : mvp_lmd_fail(LMD_MVP_TYPE, 0);
    }
    return mvp_lmd_is_callable(method) ? method : mvp_lmd_fail(LMD_MVP_TYPE, 0);
}
static Item generator_delegate_result(JsGeneratorStateRecord* state, Item result,
        int operation, bool* finished) {
    *finished = false;
    if (item_is_error(result)) return result;
    if (!iterator_object(result)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    Item done = iterator_property(result, "done");
    if (item_is_error(done)) return done;
    if (mvp_lmd_truthy(done)) {
        state->state = operation == 1 ? 1 : 0;
        *finished = true;
        return iterator_property(result, "value");
    }
    // yield* exposes the delegate's result object, including its getters.
    state->delegate = result;
    return result;
}
static Item generator_delegate_step(JsGeneratorStateRecord* state,
        Item record, Item received, int operation, bool* finished) {
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, record), input(roots, received), result(roots, ItemNull), method(roots, ItemNull);
    method.set(generator_delegate_request(state, held.get(), input.get(), operation, finished));
    if (*finished || item_is_error(method.get())) return method.get();
    Item argument = input.get();
    result.set(mvp_lmd_class_invoke(method.get(), held.get().array->items[0], &argument, 1,
        Item{.item = ITEM_JS_UNDEFINED}));
    return generator_delegate_result(state, result.get(), operation, finished);
}

static Item compact_generator_resume(MvpLmdProgram* program, JsGeneratorStateRecord* state,
        Item receiver, Item input, int operation, bool first, bool* finished) {
    RootFrame roots(4);
    *finished = true;
    if (!roots.valid()) return ItemError;
    uint64_t input_home = 0, result_home = 0;
    Rooted<Item> current(roots, receiver), received(roots, lambda_item_adopt_scalar_home(input, &input_home)),
        result(roots, ItemNull), method(roots, ItemNull);
    auto retain = [&](Item value) { result.set(lambda_item_adopt_scalar_home(value, &result_home)); };
    state->compact->resume_program = program;
    bool returning = false;
    for (;;) {
        state = &((JsGeneratorMapCarrier*)current.get().map)->state;
        JsCompactGeneratorFrame* frame = state->compact;
        bool done = false, delegation = returning;
        {
            MvpLmdRealmScope body_realm(frame->program);
            if (returning) {
                retain(generator_delegate_result(state, result.get(), frame->pending_operation, &done));
            } else {
                if (frame->position == 0) {
                    JsCompactGeneratorFrame* previous = active_compact_frame;
                    active_compact_frame = frame;
                    retain(generator_run_body(state));
                    active_compact_frame = previous;
                    done = item_is_error(result.get()) || frame->position == 0;
                }
                if (!done && frame->position == 1) {
                    done = !first;
                    retain(first ? frame->saved : operation == 2 ? mvp_lmd_throw(received.get(), 0)
                        : operation == 1 ? received.get() : Item{.item = ITEM_JS_UNDEFINED});
                } else if (!done && frame->position == 2) {
                    delegation = true;
                    state->delegate = ItemNull;
                    if (first) { received.set(Item{.item = ITEM_JS_UNDEFINED}); operation = 0; }
                    frame->pending_operation = operation;
                    if (compact_delegate_depth >= 512) {
                        // opaque callbacks can recurse; switch stacks without replaying the JIT prelude.
                        frame->pending = lambda_item_adopt_scalar_home(received.get(), &frame->pending_scalar_home);
                        frame->position = 4;
                        retain(generator_resume_activation(state, current.get(), Item{.item = ITEM_JS_UNDEFINED},
                            &done, &frame->pending_scalar_home));
                        delegation = false;
                    } else {
                        method.set(generator_delegate_request(state, frame->saved, received.get(), operation, &done));
                        if (done || item_is_error(method.get())) retain(method.get());
                        else {
                            Item child = frame->saved.array->items[0];
                            MvpLmdProgram* child_program = generator_intrinsic_program(method.get(), child, operation);
                            auto* child_state = child_program ? &((JsGeneratorMapCarrier*)child.map)->state : NULL;
                            if (child_state && child_state->compact && child_state->compact->position != 4 &&
                                    !child_state->done && !child_state->executing && (child_state->started || !operation)) {
                                // intrinsic compact delegation is a heap link, never a recursive native call.
                                child_state->compact->parent = current.get();
                                child_state->compact->resume_program = child_program;
                                first = !child_state->started;
                                child_state->started = child_state->executing = true;
                                child_state->state = operation;
                                current.set(child); returning = false;
                                continue;
                            }
                            Item argument = received.get();
                            compact_delegate_depth++;
                            retain(mvp_lmd_class_invoke(method.get(), child, &argument, 1,
                                Item{.item = ITEM_JS_UNDEFINED}));
                            compact_delegate_depth--;
                            retain(generator_delegate_result(state, result.get(), operation, &done));
                        }
                    }
                }
            }
            if (item_is_error(result.get())) done = true;
            if (delegation && done && !item_is_error(result.get()) && state->state != 1)
                retain(Item{.item = ITEM_JS_UNDEFINED});
            if (done) {
                frame->saved = Item{.item = ITEM_JS_UNDEFINED};
                if (frame->position != 4) frame->position = 3;
            }
            retain(body_realm.complete(result.get()));
        }
        if (current.get().item == receiver.item) {
            *finished = done;
            return js_retain_scalar(result.get());
        }
        state->done = done; state->executing = false;
        {
            MvpLmdRealmScope resume_realm(frame->resume_program);
            bool delegated = state->delegate.item && state->delegate.item != ITEM_NULL;
            if (!item_is_error(result.get()) && !delegated)
                retain(mvp_lmd_iterator_result(frame->resume_program, result.get(), done));
            retain(resume_realm.complete(result.get()));
        }
        current.set(frame->parent); frame->parent = ItemNull;
        returning = true;
    }
}
static Item generator_resume_impl(MvpLmdProgram* program, Item receiver, Item value, int operation,
        bool materialize, bool read_value) {
    if (get_type_id(receiver) != LMD_TYPE_MAP || receiver.map->map_kind != MAP_KIND_GENERATOR)
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    JsGeneratorStateRecord* state = &((JsGeneratorMapCarrier*)receiver.map)->state;
    if (state->is_async || state->runtime_context != context || state->executing)
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    Item implementation = js_generator_implementation(state);
    if (state->ast_host || get_type_id(implementation) != LMD_TYPE_FUNC ||
            !function_has_abi(implementation.function, FN_ENTRY_ABI_MVP_LMD))
        return operation == 1 ? js_generator_return(receiver, value)
        : operation == 2 ? js_generator_throw(receiver, value) : js_generator_next(receiver, value);
    if (!state->started && operation) {
        state->done = true; activation_destroy(state->activation); state->activation = NULL;
    }
    if (state->done && !materialize) return Item{.item = JS_ITER_DONE_SENTINEL};
    if (state->done) return operation == 2 ? mvp_lmd_throw(value, 0) :
        mvp_lmd_iterator_result(program, operation == 1 ? value : Item{.item = ITEM_JS_UNDEFINED}, true);
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, receiver), result(roots, ItemNull);
    uint64_t result_home = 0;
    bool first = !state->started;
    state->started = state->executing = true; state->state = operation;
    if (state->compact && state->compact->position != 4) {
        bool finished = false;
        result.set(lambda_item_adopt_scalar_home(compact_generator_resume(program, state,
            held.get(), value, operation, first, &finished), &result_home));
        state->done = finished;
    } else {
        bool finished = false;
        result.set(lambda_item_adopt_scalar_home(generator_resume_activation(state,
            held.get(), value, &finished, &result_home), &result_home));
        state->done = finished;
    }
    state->executing = false;
    bool delegated_result = state->delegate.item && state->delegate.item != ITEM_NULL;
    if (!materialize && !item_is_error(result.get())) {
        if (state->done) return Item{.item = JS_ITER_DONE_SENTINEL};
        // a delegated custom result still owns observable done/value getters.
        if (delegated_result) {
            Item done = iterator_property(result.get(), "done");
            if (item_is_error(done)) return done;
            if (mvp_lmd_truthy(done)) return Item{.item = JS_ITER_DONE_SENTINEL};
            if (read_value) result.set(iterator_property(result.get(), "value"));
        }
        return read_value ? js_retain_scalar(result.get()) : Item{.item = ITEM_JS_UNDEFINED};
    }
    return item_is_error(result.get()) || delegated_result ? result.get() : mvp_lmd_iterator_result(program, result.get(), state->done);
}
Item mvp_lmd_generator_resume(MvpLmdProgram* program, Item receiver, Item value, int operation) {
    return generator_resume_impl(program, receiver, value, operation, true);
}
static Item generator_resume(Context*, MvpLmdProgram* program, Item* args, uint64_t count,
        Item self, Item receiver, Item) {
    return mvp_lmd_generator_resume(program, receiver, count ? args[0] : make_js_undefined(),
        (intptr_t)((MvpLmdNativeCallable*)self.function)->state);
}
Item mvp_lmd_generator_create_ast(MvpLmdProgram* program, Item internal, Item wrapper,
        Item arguments, Item receiver, bool is_async) {
    RootFrame roots(5);
    if (!roots.valid()) return ItemError;
    Rooted<Item> function(roots, internal), callee(roots, wrapper), args(roots, arguments),
        owner(roots, js_retain_scalar(receiver)), output(roots, ItemNull);
    Item ready = mvp_lmd_function_prepare(callee.get());
    if (item_is_error(ready)) return ready;
    MvpLmdLibraryState* library = mvp_lmd_program_library(program);
    // public AST functions have native capabilities and no MVP callable tail;
    // their generator carrier uses the realm's existing generator family.
    MvpLmdClass* cls = function_has_abi(callee.get().function, FN_ENTRY_ABI_MVP_LMD)
        ? ((MvpLmdCallable*)callee.get().function)->properties
        : is_async ? library->async_generator : &library->generator;
    output.set(js_generator_create_body(js_interp_generator_body, NULL, NULL, 0, is_async,
        function.get(), args.get(), owner.get(), &cls->shape));
    if (item_is_error(output.get())) return output.get();
    // parameter defaults run before this live prototype read, including async generators.
    owner.set(iterator_property(callee.get(), "prototype"));
    if (item_is_error(owner.get())) return owner.get();
    if (!iterator_object(owner.get())) owner.set(is_async ? library->async_generator_values[1]
        : library->values[LMD_LIBRARY_GENERATOR_PROTOTYPE]);
    Item linked = mvp_lmd_set_prototype(output.get(), owner.get());
    return item_is_error(linked) ? linked : output.get();
}
Item mvp_lmd_generator_call(Context*, MvpLmdProgram* program, Item* args, uint64_t count,
        Item self, Item receiver, Item target) {
    if (target.item != ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    return mvp_lmd_generator_call_source(program, self, self, receiver, args, count);
}
bool mvp_lmd_generator_lazy_parameters(const AstFuncNode* definition) {
    return definition && !definition->is_async &&
        !js_ast_collect_parameter_facts(definition->params).has_non_simple_params;
}
Item mvp_lmd_generator_call_source(MvpLmdProgram* program, Item implementation,
        Item source, Item receiver, Item* args, uint64_t count) {
    RootFrame roots(5);
    if (!roots.valid()) return ItemError;
    Rooted<Item> function(roots, implementation), callee(roots, source), object(roots, ItemNull), owner(roots, receiver),
        arguments(roots, mvp_lmd_array_new(count));
    if (item_is_error(arguments.get())) return arguments.get();
    for (uint64_t i = 0; i < count; i++) {
        Item stored = mvp_lmd_array_store(arguments.get(), i, args[i]);
        if (item_is_error(stored)) return stored;
    }
    Item prepared = mvp_lmd_function_prepare(function.get());
    if (item_is_error(prepared)) return prepared;
    MvpLmdClass* cls = ((MvpLmdCallable*)function.get().function)->properties;
    const AstFuncNode* definition = (const AstFuncNode*)function.get().function->def;
    if (definition->is_async) {
        auto* callable = (MvpLmdNativeCallable*)function.get().function;
        object.set(js_generator_create_body(generator_body, callable->state, NULL, 0, true,
            function.get(), arguments.get(), owner.get(), &cls->shape));
        if (item_is_error(object.get())) return object.get();
        auto* state = &((JsGeneratorMapCarrier*)object.get().map)->state;
        ActivationStatus status = ACTIVATION_DONE;
        state->executing = true;
        Item initialized = js_suspended_activation_step(state, generator_body, object.get(),
            Item{.item = ITEM_JS_UNDEFINED}, &status);
        state->executing = false;
        if (status != ACTIVATION_SUSPENDED) return item_is_error(initialized) ? initialized : mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        // parameter initializers may replace the function's prototype before allocation completes.
        owner.set(iterator_property(function.get(), "prototype"));
        if (item_is_error(owner.get())) return owner.get();
        if (!iterator_object(owner.get())) owner.set(mvp_lmd_program_library(program)->async_generator_values[1]);
        Item linked = mvp_lmd_set_prototype(object.get(), owner.get());
        return item_is_error(linked) ? linked : object.get();
    }
    JsGeneratorMapCarrier* carrier = (JsGeneratorMapCarrier*)heap_calloc(sizeof(JsGeneratorMapCarrier), LMD_TYPE_MAP);
    if (!carrier) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    carrier->base.type_id = LMD_TYPE_MAP; carrier->base.map_kind = MAP_KIND_GENERATOR;
    carrier->base.type = &cls->shape; object.set(Item{.map = &carrier->base});
    JsGeneratorStateRecord* state = &carrier->state;
    durable_activation_init(state, LMD_TYPE_MAP, DURABLE_ACTIVATION_JS_GENERATOR);
    state->runtime_context = context; state->ast_function = callee.get();
    state->implementation = function.get();
    state->ast_arguments = arguments.get();
    owned_item_slot_store(&state->ast_this, 1, 0, owner.get());
    if (((MvpLmdNativeCallable*)function.get().function)->compact_kind) {
        state->compact = (JsCompactGeneratorFrame*)mem_calloc(1, sizeof(JsCompactGeneratorFrame), MEM_CAT_JS_RUNTIME);
        if (!state->compact) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        state->compact->program = program;
    }
    // simple bindings have no call-time effects; their private argument snapshot
    // is enough until first next(). Defaults/patterns retain eager initialization.
    if (!mvp_lmd_generator_lazy_parameters(definition)) {
        state->activation = activation_create(generator_body, object.get(), false, state);
        if (!state->activation) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        state->executing = true;
        ActivationStatus status = activation_resume(state->activation, Item{.item = ITEM_JS_UNDEFINED});
        state->executing = false;
        if (status != ACTIVATION_SUSPENDED) {
            Item result = activation_value(state->activation);
            if (activation_fault(state->activation)) result = mvp_lmd_fail(LMD_MVP_RANGE, 0);
            state->done = true; activation_destroy(state->activation); state->activation = NULL;
            return item_is_error(result) ? result : mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        }
    }
    MvpLmdLibraryState* library = mvp_lmd_program_library(program);
    if (callee.get().item != function.get().item || library->mutable_prototypes || library->generator_function_values) {
        // parameter defaults run before this live lookup, just as on the async generator path.
        owner.set(iterator_property(callee.get(), "prototype"));
        if (item_is_error(owner.get())) return owner.get();
        if (!iterator_object(owner.get())) owner.set(library->values[LMD_LIBRARY_GENERATOR_PROTOTYPE]);
        Item linked = mvp_lmd_set_prototype(object.get(), owner.get());
        if (item_is_error(linked)) return linked;
    }
    return object.get();
}
// the activation fallback resumes an already acquired iterator record.
static Item generator_delegate_drive(JsGeneratorStateRecord* state,
        Item starting_record, Item initial_input, int initial_operation, bool interpreted) {
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> record(roots, starting_record), result(roots, ItemNull), received(roots, initial_input);
    uint64_t received_home = 0;
    int operation = initial_operation;
    for (;;) {
        bool finished = false;
        result.set(generator_delegate_step(state, record.get(), received.get(), operation, &finished));
        if (item_is_error(result.get())) return result.get();
        if (finished) return interpreted && operation == 1 ? js_gen_return_signal(result.get()) : result.get();
        if (interpreted) state->state = JS_GEN_PARK_YIELD;
        received.set(lambda_item_adopt_scalar_home(activation_suspend(result.get()), &received_home));
        state->delegate = ItemNull;
        operation = interpreted ? js_gen_is_return_signal(received.get()) ? 1 :
            js_gen_is_throw_signal(received.get()) ? 2 : 0 : state->state;
        if (interpreted && operation) received.set(lambda_item_adopt_scalar_home(operation == 1
            ? js_gen_return_signal_value(received.get()) : js_gen_throw_signal_value(received.get()), &received_home));
    }
}
Item mvp_lmd_generator_delegate(MvpLmdProgram* program, Item value) {
    auto* state = (JsGeneratorStateRecord*)activation_user(activation_current());
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> record(roots, mvp_lmd_iterator_get(program, value));
    return item_is_error(record.get()) ? record.get() : generator_delegate_drive(
        state, record.get(), Item{.item = ITEM_JS_UNDEFINED}, 0, state->ast_host != NULL);
}
extern "C" Item mvp_lmd_generator_park(Item value, int64_t kind) {
    Activation* activation = activation_current();
    if (!activation || !activation_user(activation)) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    if (kind == 2) {
        auto* state = (JsGeneratorStateRecord*)activation_user(activation);
        return mvp_lmd_generator_delegate(((MvpLmdCallable*)js_generator_implementation(state).function)->program, value);
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
const JsCollectionOps mvp_lmd_collection_ops = {
    mvp_lmd_is_object, mvp_lmd_is_callable, iterator_property,
    mvp_lmd_callback_call,
    [](Item input) { return mvp_lmd_iterator_get(NULL, input); },
    mvp_lmd_iterator_step_value, mvp_lmd_iterator_close,
    [](bool range, const char*) { return mvp_lmd_fail(range ? LMD_MVP_RANGE : LMD_MVP_TYPE, 0); },
    js_can_be_held_weakly,
    [](Item value) { return mvp_lmd_primitive_to_number(value); },
    [](Item value) { return !!mvp_lmd_truthy(value); },
    [](Item owner, Item method) { return mvp_lmd_iterator_get_with_method(NULL, owner, method); }
};

const MvpLmdWeakFamily mvp_lmd_weak_families[4] = {
    {"WeakMap", JS_BUILTIN_OWNER_WEAKMAP_PROTOTYPE_METHOD},
    {"WeakSet", JS_BUILTIN_OWNER_WEAKSET_PROTOTYPE_METHOD},
    {"WeakRef", JS_BUILTIN_OWNER_WEAKREF_PROTOTYPE_METHOD},
    {"FinalizationRegistry", JS_BUILTIN_OWNER_FINALIZATION_REGISTRY_PROTOTYPE_METHOD}
};
static Item weak_collection_construct(Context*, MvpLmdProgram* program, Item* args, uint64_t count,
        Item self, Item, Item target) {
    if (target.item == ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    MvpLmdLibraryState* library = mvp_lmd_program_library(program);
    MvpLmdClass* base = mvp_lmd_class_record(self);
    bool set = base == &library->weak_collections[1];
    bool lifetime = library->weak_class_count > 2 && (base == &library->weak_collections[2] ||
        base == &library->weak_collections[3]);
    bool registry = lifetime && base == &library->weak_collections[3];
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> new_target(roots, target), input(roots, count ? args[0] : Item{.item = ITEM_JS_UNDEFINED}),
        prototype(roots, ItemNull), output(roots, ItemNull);
    // validate target/callback before an observable newTarget.prototype getter.
    if (lifetime) {
        Item valid = js_weak_validate_input(registry, input.get(), &mvp_lmd_collection_ops);
        if (item_is_error(valid)) return valid;
    }
    prototype.set(mvp_lmd_constructor_prototype(new_target.get(), base->values[1]));
    if (item_is_error(prototype.get())) return prototype.get();
    MvpLmdClass* subtype = mvp_lmd_class_record(new_target.get());
    TypeMap* shape = subtype && subtype->ast && lambda_nominal_derives_from(&subtype->nominal, &base->nominal)
        ? &subtype->shape : &base->shape;
    output.set(lifetime ? js_weak_create_with_storage(shape, registry, input.get(), &mvp_lmd_collection_ops)
        : js_collection_create_with_storage(shape, context->pool, set ? 1 : 0, true));
    if (item_is_error(output.get())) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    Item linked = mvp_lmd_set_prototype(output.get(), prototype.get());
    if (item_is_error(linked)) return linked;
    return lifetime ? output.get() : js_collection_add_entries_with_ops(output.get(), input.get(), !set, &mvp_lmd_collection_ops);
}

static Item weak_collection_method(Context*, MvpLmdProgram*, Item* args, uint64_t count,
        Item self, Item receiver, Item) {
    const JsBuiltinMethodSpec* method = (const JsBuiltinMethodSpec*)((MvpLmdNativeCallable*)self.function)->state;
    if (method->owner == JS_BUILTIN_OWNER_WEAKREF_PROTOTYPE_METHOD ||
            method->owner == JS_BUILTIN_OWNER_FINALIZATION_REGISTRY_PROTOTYPE_METHOD)
        return js_weak_method_with_ops(method->builtin_id, receiver, args, count, &mvp_lmd_collection_ops);
    bool set = method->owner == JS_BUILTIN_OWNER_WEAKSET_PROTOTYPE_METHOD;
    if (!js_collection_has_brand(receiver, set ? 1 : 0, true)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    int operation = method->builtin_id == JS_BUILTIN_WEAKMAP_SET || method->builtin_id == JS_BUILTIN_WEAKSET_ADD ? 0 :
        method->builtin_id == JS_BUILTIN_WEAKMAP_GET ? 1 :
        method->builtin_id == JS_BUILTIN_WEAKMAP_HAS || method->builtin_id == JS_BUILTIN_WEAKSET_HAS ? 2 : 3;
    return js_collection_method_with_ops(receiver, operation,
        count ? args[0] : Item{.item = ITEM_JS_UNDEFINED},
        count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED}, &mvp_lmd_collection_ops);
}

static Item weak_collection_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state) {
    if (!state->weak_collections) return ItemNull;
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> function(roots, ItemNull);
    for (int family = 0; family < state->weak_class_count; family++) {
        MvpLmdClass* cls = &state->weak_collections[family];
        const char* name = mvp_lmd_weak_families[family].name;
        if (state->native_callable_values) {
            Item status = mvp_lmd_library_class_adopt(cls, state->weak_values + 3 * family,
                js_get_constructor(js_name_item(name)));
            if (item_is_error(status)) return status;
            continue;
        }
        const JsBuiltinGlobalSpec* builtin = js_builtin_global_find(name, strlen(name));
        Item status = mvp_lmd_library_class_initialize(program, state, cls,
            (int)(state->weak_values - state->values) + 3 * family, name, weak_collection_construct, builtin->param_count);
        if (item_is_error(status)) return status;
        ((MvpLmdCallable*)cls->values[0].function)->custom_new_target = true;
        status = mvp_lmd_named_set(cls->values[1], "constructor", cls->values[0], JSPD_NON_ENUMERABLE);
        if (item_is_error(status)) return status;
        JsPropertyDescriptor tag = {};
        tag.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
        js_pd_set_configurable(&tag, true);
        tag.value = Item{.item = s2it(heap_create_name(name, strlen(name)))};
        status = mvp_lmd_define_own_property(cls->values[1],
            Item{.item = s2it(well_known_name_ref(JS_SYMBOL_TO_STRING_TAG))}, &tag);
        if (item_is_error(status)) return status;
        for (int i = 0; ; i++) {
            const JsBuiltinMethodSpec* method = js_builtin_method_at(i);
            if (!method) break;
            if (method->owner != mvp_lmd_weak_families[family].prototype_owner) continue;
            function.set(mvp_lmd_native_function(program, (void*)method, weak_collection_method,
                method->param_count, method->display_name ? method->display_name : method->name));
            if (item_is_error(function.get())) return function.get();
            status = mvp_lmd_named_set(cls->values[1], method->name, function.get(), JSPD_NON_ENUMERABLE);
            if (item_is_error(status)) return status;
        }
    }
    return ItemNull;
}

const JsCollectionStorageOps mvp_lmd_collection_storage_ops = {
    [](Item owner, int kind) {
        if (!mvp_lmd_is_ordered_map(owner)) return false;
        MvpLmdLibraryState* state = mvp_lmd_program_library();
        return lambda_nominal_derives_from(((TypeMap*)owner.map->type)->nominal,
            kind ? &state->set.nominal : &state->map.nominal);
    },
    [](Item owner) { return ((OrderedMap*)owner.map)->size; },
    [](int kind) {
        MvpLmdLibraryState* state = mvp_lmd_program_library();
        return mvp_lmd_object_new(kind ? &state->set.shape : &state->map.shape, 1);
    },
    [](Item owner, Item key) { return mvp_lmd_map_call(owner, Item{.item = mvp_lmd_method_token(1)}, key, ItemNull); },
    [](Item owner, Item key) { return mvp_lmd_map_call(owner, Item{.item = mvp_lmd_method_token(3)}, key, ItemNull); },
    [](Item owner, Item key, Item value) { return mvp_lmd_map_call(owner, Item{.item = mvp_lmd_method_token(2)}, key, value); },
    [](Item owner, Item key) { return mvp_lmd_map_call(owner, Item{.item = mvp_lmd_method_token(4)}, key, ItemNull); },
    [](Item owner) { ((OrderedMap*)owner.map)->cursors++; },
    [](Item owner, JsCollectionCursor* cursor, Item* key, Item* value) {
        int64_t index = mvp_lmd_map_next(owner, cursor->position);
        if (index < 0) return false;
        cursor->position = index + 4;
        OrderedMap* map = (OrderedMap*)owner.map;
        *key = map->entries->items[index]; *value = map->entries->items[index + 1];
        return true;
    },
    [](Item owner) { ((OrderedMap*)owner.map)->cursors--; }
};

static Item collection_observable_method(Context*, MvpLmdProgram*, Item* args, uint64_t count,
        Item self, Item receiver, Item) {
    const JsBuiltinMethodSpec* method = (const JsBuiltinMethodSpec*)((MvpLmdNativeCallable*)self.function)->state;
    Item first = count ? args[0] : Item{.item = ITEM_JS_UNDEFINED};
    if (method->builtin_id == JS_BUILTIN_MAP_FOREACH || method->builtin_id == JS_BUILTIN_SET_FOREACH)
        return js_collection_for_each_with_ops(receiver, first,
            count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED}, method->builtin_id == JS_BUILTIN_SET_FOREACH,
            &mvp_lmd_collection_ops, &mvp_lmd_collection_storage_ops);
    return js_set_operation_with_ops(receiver, first, method->builtin_id,
        &mvp_lmd_collection_ops, &mvp_lmd_collection_storage_ops);
}

// Catalog identities select the existing MVP hot operation; new observable
// methods have no token and enter the shared callback/Set algorithms.
static int collection_catalog_operation(int id) {
    switch (id) {
    case JS_BUILTIN_MAP_GET: return 1;
    case JS_BUILTIN_MAP_SET: case JS_BUILTIN_SET_ADD: return 2;
    case JS_BUILTIN_MAP_HAS: case JS_BUILTIN_SET_HAS: return 3;
    case JS_BUILTIN_MAP_DELETE: case JS_BUILTIN_SET_DELETE: return 4;
    case JS_BUILTIN_MAP_CLEAR: case JS_BUILTIN_SET_CLEAR: return 5;
    case JS_BUILTIN_MAP_ENTRIES: case JS_BUILTIN_SET_ENTRIES: return 6;
    case JS_BUILTIN_MAP_KEYS: return 7;
    case JS_BUILTIN_MAP_VALUES: case JS_BUILTIN_SET_VALUES: return 8;
    default: return 0;
    }
}

static Item collection_method(Context*, MvpLmdProgram* program, Item* args, uint64_t count, Item self, Item receiver, Item) {
    int code = (intptr_t)((MvpLmdNativeCallable*)self.function)->state;
    MvpLmdLibraryState* library = mvp_lmd_program_library(program);
    MvpLmdClass* cls = code & 32 ? &library->set : &library->map;
    if (!mvp_lmd_is_ordered_map(receiver) ||
            !lambda_nominal_derives_from(((TypeMap*)receiver.map->type)->nominal, &cls->nominal))
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    int method = code & 31;
    if (!method) return Item{.item = i2it(((OrderedMap*)receiver.map)->size)};
    Item key = count ? args[0] : Item{.item = ITEM_JS_UNDEFINED};
    return mvp_lmd_map_call(receiver, Item{.item = mvp_lmd_method_token(method)}, key,
        code & 32 ? key : count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED});
}
static Item collection_construct(Context*, MvpLmdProgram* program, Item* args, uint64_t count, Item self, Item, Item target) {
    if (target.item == ITEM_JS_UNDEFINED) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    MvpLmdClass* cls = mvp_lmd_class_record(self);
    MvpLmdLibraryState* state = mvp_lmd_program_library(program);
    bool set = cls == &state->set;
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> input(roots, count ? args[0] : Item{.item = ITEM_JS_UNDEFINED}),
        output(roots, ItemNull), prototype(roots, ItemNull);
    bool observed_target = target.item != self.item || state->mutable_prototypes;
    if (observed_target) {
        prototype.set(mvp_lmd_constructor_prototype(target, cls->values[1]));
        if (item_is_error(prototype.get())) return prototype.get();
    }
    MvpLmdClass* actual = observed_target ? mvp_lmd_class_record(target) : cls;
    TypeMap* shape = actual && actual->ast && lambda_nominal_derives_from(&actual->nominal, &cls->nominal)
        ? &actual->shape : &cls->shape;
    output.set(mvp_lmd_object_new(shape, 1));
    if (item_is_error(output.get())) return output.get();
    if (shape != &cls->shape && !map_fill_reserve_data(output.get().map)) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    if (observed_target) {
        Item linked = mvp_lmd_set_prototype(output.get(), prototype.get());
        if (item_is_error(linked)) return linked;
    }
    return js_collection_add_entries_with_ops(output.get(), input.get(), !set, &mvp_lmd_collection_ops);
}
Item mvp_lmd_iterator_prototype_initialize(MvpLmdProgram*, MvpLmdClass* cls,
        Item* slots, const char* tag_name, Item existing) {
    cls->values = slots;
    slots[0] = slots[2] = ItemNull;
    if (existing.item != ITEM_NULL) {
        slots[1] = existing;
        return item_is_error(existing) ? existing : ItemNull;
    }
    slots[1] = mvp_lmd_object_new(&cls->prototype_shape, 0);
    if (item_is_error(slots[1]) || !tag_name) return item_is_error(slots[1]) ? slots[1] : ItemNull;
    // program slots own the prototype before tag allocation or descriptor installation can collect.
    JsPropertyDescriptor tag = {};
    tag.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
    js_pd_set_configurable(&tag, true);
    String* text = heap_create_name(tag_name, strlen(tag_name));
    if (!text) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    tag.value = Item{.item = s2it(text)};
    return mvp_lmd_define_own_property(slots[1],
        Item{.item = s2it(well_known_name_ref(JS_SYMBOL_TO_STRING_TAG))}, &tag);
}
Item mvp_lmd_generator_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state) {
    if (state->native_callable_values) {
        // native sequence methods already own their carriers and iterator prototype families.
        if (state->iterator_base) {
            Item result = mvp_lmd_iterator_prototype_initialize(program, state->iterator_base,
                state->iterator_base_values, NULL, js_get_iterator_proto());
            if (item_is_error(result)) return result;
        }
        state->generator.values = state->values + LMD_LIBRARY_GENERATOR;
        state->generator.values[0] = js_suspended_function_intrinsic(true, false, true);
        if (item_is_error(state->generator.values[0])) return state->generator.values[0];
        state->generator.values[1] = js_get_generator_shared_proto(false);
        if (item_is_error(state->generator.values[1])) return state->generator.values[1];
        state->generator.values[2] = state->generator.values[0];
        Item result = mvp_lmd_iterator_prototype_initialize(program, &state->iterator,
            state->values + LMD_LIBRARY_ITERATOR, NULL, js_get_array_iterator_proto());
        if (item_is_error(result)) return result;
        Item (*const prototypes[])() = {js_get_map_iterator_proto, js_get_set_iterator_proto,
            js_get_string_iterator_proto};
        if (state->iterator_families) for (int i = 0; i < LMD_ITERATOR_COUNT; i++) {
            result = mvp_lmd_iterator_prototype_initialize(program, &state->iterator_families[i],
                state->iterator_family_values + i * 3, NULL, prototypes[i]());
            if (item_is_error(result)) return result;
        }
        state->values[LMD_LIBRARY_SEQUENCE_ITERATOR] = js_intrinsic_binding_get(
            JS_BUILTIN_OWNER_ARRAY_PROTOTYPE_METHOD, "values", 6);
        if (item_is_error(state->values[LMD_LIBRARY_SEQUENCE_ITERATOR])) return state->values[LMD_LIBRARY_SEQUENCE_ITERATOR];
        state->values[LMD_LIBRARY_STRING_ITERATOR] = js_intrinsic_binding_get(
            JS_BUILTIN_OWNER_STRING_ITERATOR_INTERNAL, "[Symbol.iterator]", 17);
        if (item_is_error(state->values[LMD_LIBRARY_STRING_ITERATOR])) return state->values[LMD_LIBRARY_STRING_ITERATOR];
        for (int set = 0; set < 2; set++) {
            result = mvp_lmd_library_class_adopt(set ? &state->set : &state->map,
                state->values + (set ? LMD_LIBRARY_SET : LMD_LIBRARY_MAP),
                js_get_constructor(js_name_item(set ? "Set" : "Map")));
            if (item_is_error(result)) return result;
        }
        return weak_collection_initialize(program, state);
    }
    if (state->iterator_base) {
        Item initialized = mvp_lmd_iterator_prototype_initialize(program, state->iterator_base, state->iterator_base_values, NULL);
        if (item_is_error(initialized)) return initialized;
    }
    bool native_generator = state->native_callable_values && state->generator_function_values;
    state->generator.values = state->values + LMD_LIBRARY_GENERATOR;
    Item result = native_generator ? ItemNull : mvp_lmd_library_class_initialize(program, state, &state->generator,
        LMD_LIBRARY_GENERATOR, "Generator", generator_construct, 0, state->generator_function_values == NULL);
    if (item_is_error(result)) return result;
    for (int i = 0; i < 3; i++) {
        if (native_generator) break;
        result = mvp_lmd_named_set(state->values[LMD_LIBRARY_GENERATOR_PROTOTYPE], generator_resume_names[i],
            mvp_lmd_native_function(program, (void*)(intptr_t)i, generator_resume, 1, generator_resume_names[i]));
        if (item_is_error(result)) return result;
    }
    result = mvp_lmd_library_class_initialize(program, state, &state->iterator,
        LMD_LIBRARY_ITERATOR, "Iterator", generator_construct, 0);
    if (item_is_error(result)) return result;
    result = mvp_lmd_named_set(state->values[LMD_LIBRARY_ITERATOR_PROTOTYPE], "next",
        mvp_lmd_native_function(program, NULL, sequence_next, 0, "next"), JSPD_NON_ENUMERABLE);
    if (item_is_error(result)) return result;
    result = mvp_lmd_named_set(state->values[LMD_LIBRARY_ITERATOR_PROTOTYPE],
        Item{.item = s2it(well_known_name_ref(JS_SYMBOL_TO_STRING_TAG))},
        js_name_item("Array Iterator"), JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE);
    if (item_is_error(result)) return result;
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    const JsBuiltinMethodSpec* identity_spec = state->iterator_base
        ? js_builtin_catalog_find(JS_BUILTIN_OWNER_ITERATOR_INTERNAL, "[Symbol.iterator]", 17) : NULL;
    Rooted<Item> identity(roots, mvp_lmd_native_function(program, NULL, mvp_lmd_receiver_identity,
        identity_spec ? identity_spec->param_count : 0,
        identity_spec ? (identity_spec->display_name ? identity_spec->display_name : identity_spec->name) : NULL));
    if (item_is_error(identity.get())) return identity.get();
    NameRef key = well_known_name_ref(JS_SYMBOL_ITERATOR);
    if (state->iterator_base) {
        JsPropertyDescriptor descriptor = {};
        descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_WRITABLE | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
        descriptor.value = identity.get(); js_pd_set_configurable(&descriptor, true);
        result = mvp_lmd_define_own_property(state->iterator_base_values[1], Item{.item = s2it(key)}, &descriptor);
        if (item_is_error(result)) return result;
    }
    const int prototypes[] = {LMD_LIBRARY_GENERATOR_PROTOTYPE, LMD_LIBRARY_ITERATOR_PROTOTYPE};
    for (int index : prototypes) {
        // observed sequence/generator families inherit the one shared IteratorPrototype identity.
        if (state->iterator_base || (index == LMD_LIBRARY_GENERATOR_PROTOTYPE && state->generator_function_values)) continue;
        result = mvp_lmd_named_set(state->values[index], Item{.item = s2it(key)}, identity.get(), JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
    }
    if (state->iterator_families) for (int family = 0; family < LMD_ITERATOR_COUNT; family++) {
        MvpLmdClass* cls = &state->iterator_families[family];
        result = mvp_lmd_iterator_prototype_initialize(program, cls,
            state->iterator_family_values + family * 3, mvp_lmd_iterator_names[family]);
        if (item_is_error(result)) return result;
        identity.set(mvp_lmd_native_function(program, cls, sequence_next, 0, "next"));
        if (item_is_error(identity.get())) return identity.get();
        result = mvp_lmd_named_set(cls->values[1], "next", identity.get(), JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
    }
    state->values[LMD_LIBRARY_SEQUENCE_ITERATOR] = mvp_lmd_native_function(program, NULL, sequence_iterator, 0, "values");
    if (item_is_error(state->values[LMD_LIBRARY_SEQUENCE_ITERATOR])) return state->values[LMD_LIBRARY_SEQUENCE_ITERATOR];
    result = mvp_lmd_named_set(state->values[LMD_LIBRARY_ARRAY_PROTOTYPE], Item{.item = s2it(key)},
        state->values[LMD_LIBRARY_SEQUENCE_ITERATOR], JSPD_NON_ENUMERABLE);
    if (item_is_error(result)) return result;
    state->values[LMD_LIBRARY_STRING_ITERATOR] = mvp_lmd_native_function(program,
        (void*)(intptr_t)64, sequence_iterator, 0, "[Symbol.iterator]");
    if (item_is_error(state->values[LMD_LIBRARY_STRING_ITERATOR])) return state->values[LMD_LIBRARY_STRING_ITERATOR];
    if (state->primitives) {
        result = mvp_lmd_named_set(state->primitives[LMD_PRIMITIVE_STRING].values[1],
            Item{.item = s2it(key)}, state->values[LMD_LIBRARY_STRING_ITERATOR], JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
    }
    result = mvp_lmd_named_set(state->values[LMD_LIBRARY_ARRAY_PROTOTYPE], "values",
        state->values[LMD_LIBRARY_SEQUENCE_ITERATOR], JSPD_NON_ENUMERABLE);
    if (item_is_error(result)) return result;
    const char* sequence_names[] = {"keys", "entries"};
    for (int i = 0; i < 2; i++) {
        const JsBuiltinMethodSpec* method = js_builtin_catalog_find(
            JS_BUILTIN_OWNER_ARRAY_PROTOTYPE_METHOD, sequence_names[i], strlen(sequence_names[i]));
        identity.set(mvp_lmd_native_function(program, (void*)(intptr_t)(i + 1),
            sequence_iterator, method->param_count, method->name));
        if (item_is_error(identity.get())) return identity.get();
        result = mvp_lmd_named_set(state->values[LMD_LIBRARY_ARRAY_PROTOTYPE],
            method->name, identity.get(), JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
    }
    for (int set = 0; set < 2; set++) {
        int slot = set ? LMD_LIBRARY_SET : LMD_LIBRARY_MAP;
        result = mvp_lmd_library_class_initialize(program, state, set ? &state->set : &state->map,
            slot, set ? "Set" : "Map", collection_construct, 0);
        if (item_is_error(result)) return result;
        MvpLmdClass* cls = set ? &state->set : &state->map;
        ((MvpLmdCallable*)cls->values[0].function)->custom_new_target = true;
        for (int i = 0; ; i++) {
            const JsBuiltinMethodSpec* method = js_builtin_method_at(i);
            if (!method) break;
            if (method->owner != (set ? JS_BUILTIN_OWNER_SET_PROTOTYPE_METHOD : JS_BUILTIN_OWNER_MAP_PROTOTYPE_METHOD)) continue;
            int operation = collection_catalog_operation(method->builtin_id);
            identity.set(mvp_lmd_native_function(program,
                operation ? (void*)(intptr_t)(set * 32 + operation) : (void*)method,
                operation ? (operation <= 5 ? collection_method : sequence_iterator) : collection_observable_method,
                method->param_count, method->display_name ? method->display_name : method->name));
            if (item_is_error(identity.get())) return identity.get();
            result = mvp_lmd_named_set(cls->values[1], method->name, identity.get(), JSPD_NON_ENUMERABLE);
            if (item_is_error(result)) return result;
        }
        Item method = iterator_property(state->values[slot + 1], set ? "values" : "entries");
        if (item_is_error(method)) return method;
        result = mvp_lmd_named_set(state->values[slot + 1], Item{.item = s2it(key)}, method, JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
        if (set) {
            result = mvp_lmd_named_set(state->values[slot + 1], "keys", method, JSPD_NON_ENUMERABLE);
            if (item_is_error(result)) return result;
        }
        JsPropertyDescriptor tag = {};
        tag.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
        js_pd_set_configurable(&tag, true);
        tag.value = Item{.item = s2it(heap_create_name(set ? "Set" : "Map", 3))};
        result = mvp_lmd_define_own_property(cls->values[1],
            Item{.item = s2it(well_known_name_ref(JS_SYMBOL_TO_STRING_TAG))}, &tag);
        if (item_is_error(result)) return result;
        identity.set(mvp_lmd_native_function(program, NULL, mvp_lmd_receiver_identity, 0, "get [Symbol.species]"));
        if (item_is_error(identity.get())) return identity.get();
        result = mvp_lmd_define_accessor(cls->values[0], Item{.item = s2it(well_known_name_ref(JS_SYMBOL_SPECIES))},
            identity.get(), JSPD_NON_ENUMERABLE << 8);
        if (item_is_error(result)) return result;
        if (state->mutable_prototypes) {
            // size is an inherited accessor, so live prototype traversal must observe its descriptor.
            identity.set(mvp_lmd_native_function(program, (void*)(intptr_t)(set * 32), collection_method, 0, "get size"));
            if (item_is_error(identity.get())) return identity.get();
            Item name = {.item = s2it(heap_create_name("size", 4))};
            result = mvp_lmd_define_accessor(state->values[slot + 1], name, identity.get(), JSPD_NON_ENUMERABLE << 8);
            if (item_is_error(result)) return result;
        }
    }
    return weak_collection_initialize(program, state);
}
