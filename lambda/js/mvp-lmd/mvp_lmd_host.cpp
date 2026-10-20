#include "mvp_lmd.h"
#include "mvp_lmd_runtime.h"
#include "mvp_lmd_dynamic.h"
#include "../js_builtin_catalog.hpp"
#include "../js_props.h"
#include "../js_runtime.h"
#include "../js_event_loop.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/heap_api.h"
#include "../../runtime/runtime-state.h"
#include "../../runtime/lambda-error.h"
#include "../../input/input.hpp"
#include "../../../lib/mem.h"
#include "../../../lib/utf.h"
#include <math.h>

// host bindings are explicitly supplied by the embedder, outside the core globals allowlist.

static const char* host_utf8_bytes(String* value, size_t* size, char** owned) {
    *owned = NULL; *size = value->len;
    if (utf8_valid(value->chars, value->len)) return value->chars;
    if (value->len > INT_MAX) return NULL;
    int length = utf8_wtf8_encoded_len(value->chars, value->len);
    if (length < 0) return NULL;
    *owned = (char*)mem_alloc((size_t)length + 1, MEM_CAT_JS_RUNTIME);
    if (!*owned) return NULL;
    utf8_wtf8_encode(value->chars, value->len, (uint8_t*)*owned);
    (*owned)[length] = 0; *size = length;
    return *owned;
}

static Item host_clock(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item) {
    // DOM frames, timers and native callbacks share the realm's clock origin.
    return js_performance_now();
}

static Item host_output(Item self, Item* arguments, uint64_t count, bool line, FILE* stream = NULL) {
    MvpLmdHostState* state = (MvpLmdHostState*)((MvpLmdNativeCallable*)self.function)->state;
    if (!stream) stream = state->output;
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> text(roots, ItemNull);
    for (uint64_t i = 0; i < count; i++) {
        text.set(mvp_lmd_primitive_to_string(arguments[i]));
        if (item_is_error(text.get())) return text.get();
        size_t size; char* owned;
        const char* bytes = host_utf8_bytes(text.get().get_string(), &size, &owned);
        if (!bytes) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        bool failed = (line && i && fputc(' ', stream) == EOF) ||
            fwrite(bytes, 1, size, stream) != size;
        mem_free(owned);
        if (failed)
            return err2it(err_create_heap(ERR_FILE_WRITE_ERROR, "MVP stdout write failed", NULL));
    }
    if (line && fputc('\n', stream) == EOF)
        return err2it(err_create_heap(ERR_FILE_WRITE_ERROR, "MVP stdout write failed", NULL));
    return Item{.item = line ? ITEM_JS_UNDEFINED : ITEM_TRUE};
}
template<bool error_stream = false>
static Item host_console_log(Context*, MvpLmdProgram*, Item* arguments, uint64_t count, Item self, Item, Item) {
    return host_output(self, arguments, count, true, error_stream ? stderr : NULL);
}
struct MvpLmdConsoleLabelBinding { const char* name; Item (*call)(Item); };
static Item host_console_label(Context*, MvpLmdProgram*, Item* arguments, uint64_t count, Item self, Item, Item) {
    const auto* binding = (const MvpLmdConsoleLabelBinding*)((MvpLmdNativeCallable*)self.function)->state;
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> label(roots, count ? arguments[0] : make_js_undefined());
    // shared console records consume primitives; object coercion belongs to MVP.
    if (get_type_id(label.get()) != LMD_TYPE_UNDEFINED)
        label.set(mvp_lmd_primitive_to_string(label.get()));
    return item_is_error(label.get()) ? label.get() : binding->call(label.get());
}
static Item host_stdout_write(Context*, MvpLmdProgram*, Item* arguments, uint64_t count, Item self, Item, Item) {
    if (!count || get_type_id(arguments[0]) != LMD_TYPE_STRING) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (count > 1) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    return host_output(self, arguments, 1, false);
}
static Item host_require(Context*, MvpLmdProgram* program, Item* arguments, uint64_t count, Item, Item, Item) {
    if (!count || get_type_id(arguments[0]) != LMD_TYPE_STRING) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    // one loader owns native namespaces, resolution policy and the module cache.
    return mvp_lmd_dynamic_import_value(program,
        js_require_at(arguments[0], mvp_lmd_program_reference(program)), true);
}
Item mvp_lmd_native_function(MvpLmdProgram* program, void* state, MvpLmdNativeEntry entry, uint8_t arity,
        const char* name) {
    MvpLmdNativeCallable* function = (MvpLmdNativeCallable*)heap_calloc(sizeof(MvpLmdNativeCallable), LMD_TYPE_FUNC);
    if (!function) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    function_init_abi(function, LMD_TYPE_FUNC, FN_ENTRY_ABI_MVP_LMD);
    function->requires_runtime_context = true; function->runtime_context = context;
    function->ptr = (fn_ptr)entry; function->program = program;
    function->state = state; function->arity = arity;
    function->native_source = true;
    Item result = {.function = function};
    if (!name) return result;
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, result);
    String* spelling = heap_create_name(name, strlen(name));
    function->source_name = spelling;
    if (!spelling) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    Item defined = mvp_lmd_named_set(held.get(), "name", Item{.item = s2it(spelling)}, JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE);
    return item_is_error(defined) ? defined : held.get();
}
Item mvp_lmd_named_set(Item owner, Item key, Item value, int descriptor_flags) {
    if (item_is_error(value)) return value;
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    uint64_t home = 0;
    Rooted<Item> object(roots, owner), held(roots, lambda_item_adopt_scalar_home(value, &home)), property(roots, key);
    Item result;
    if (descriptor_flags >= 0) {
        // intrinsic and error fields need DefineProperty attributes, independent
        // of inherited setters; ordinary callers retain the existing Set path.
        JsPropertyDescriptor descriptor = {};
        descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
        if (!(descriptor_flags & JSPD_NON_WRITABLE)) descriptor.flags |= JS_PD_WRITABLE;
        if (!(descriptor_flags & JSPD_NON_ENUMERABLE)) descriptor.flags |= JS_PD_ENUMERABLE;
        js_pd_set_configurable(&descriptor, !(descriptor_flags & JSPD_NON_CONFIGURABLE));
        descriptor.value = held.get();
        result = mvp_lmd_define_own_property(object.get(), property.get(), &descriptor);
        if (item_is_error(result)) return result;
        if (result.item == ITEM_FALSE) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
        result = held.get();
    } else {
        // adopted host namespaces use the shared property kernel, not the packed MVP-only writer.
        result = mvp_lmd_foreign_candidate(object.get(), true)
            ? mvp_lmd_class_property(object.get(), property.get(), held.get(), LMD_PROP_SET)
            : mvp_lmd_property_set(object.get(), property.get(), held.get());
    }
    // the return value must not retain this function's stack scalar home.
    return lambda_item_uses_scalar_home(result) ? push_d(it2d(result)) : result;
}
Item mvp_lmd_named_set(Item owner, const char* key, Item value, int descriptor_flags) {
    // name-pool interning does not collect; the keyed entry roots all operands before property dispatch.
    String* name = heap_create_name(key, strlen(key));
    return name ? mvp_lmd_named_set(owner, Item{.item = s2it(name)}, value, descriptor_flags) : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}
static Item host_task_call(Item environment) {
    Item* captured = (Item*)(uintptr_t)environment.item;
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> callback(roots, captured[0]), receiver(roots, captured[1]), arguments(roots, captured[2]);
    uint32_t module_id = (uint32_t)it2i(captured[3]);
    RuntimeModuleStateScope module(context);
    if (module_id != UINT32_MAX && !module.activate(module_id)) return ItemError;
    // the existing forwarding path snapshots arguments before callbacks can collect their backing storage.
    return mvp_lmd_function_forward(callback.get(), receiver.get(), arguments.get(), NULL, 0);
}
Item mvp_lmd_host_task(Item callback, Item receiver, Item arguments) {
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> function(roots, callback), self(roots, receiver), inputs(roots, arguments);
    Item* environment = js_alloc_env(4);
    if (!environment) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    environment[0] = function.get(); environment[1] = self.get(); environment[2] = inputs.get();
    environment[3] = Item{.item = i2it(lambda_active_module_state_id())};
    return js_new_native_closure(host_task_call, 0, environment, 4);
}

static Item host_schedule(Context*, MvpLmdProgram* program, Item* args, uint64_t count,
        Item self, Item, Item) {
    MvpLmdHostState* state = (MvpLmdHostState*)((MvpLmdNativeCallable*)self.function)->state;
    bool microtask = self.item == state->timers[LMD_TIMER_MICROTASK].item;
    bool interval = self.item == state->timers[LMD_TIMER_INTERVAL].item;
    if (!count || ((microtask || !state->native_timers) && !mvp_lmd_is_callable(args[0])))
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (count > INT_MAX) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    uint64_t delay_home = 0;
    Rooted<Item> callback(roots, args[0]),
        delay(roots, lambda_item_adopt_scalar_home(count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED}, &delay_home)),
        arguments(roots, ItemNull), thunk(roots, ItemNull);
    if (!microtask && state->native_timers) {
        callback.set(js_timer_normalize_handler(callback.get(),
            [](Item value) { return mvp_lmd_primitive_to_string(value); },
            [](Item source, void* owner) -> Item {
                RootFrame roots(3);
                if (!roots.valid()) return ItemError;
                Rooted<Item> text(roots, source), evaluator(roots, ItemNull), inputs(roots, ItemNull);
                evaluator.set(mvp_lmd_native_function((MvpLmdProgram*)owner, NULL, mvp_lmd_eval_script, 1));
                if (item_is_error(evaluator.get())) return evaluator.get();
                inputs.set(js_pack_args_span((Item*)text.home(), 1));
                if (get_type_id(inputs.get()) != LMD_TYPE_ARRAY) return ItemError;
                return mvp_lmd_host_task(evaluator.get(), make_js_undefined(), inputs.get());
            }, program));
        if (item_is_error(callback.get())) return callback.get();
    }
    // own wide callback arguments before delay coercion can reenter guest code.
    arguments.set(js_pack_args_span(!microtask && count > 2 ? args + 2 : NULL,
        !microtask && count > 2 ? (int)count - 2 : 0));
    if (get_type_id(arguments.get()) != LMD_TYPE_ARRAY) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    if (!microtask) {
        delay.set(mvp_lmd_primitive_to_number(delay.get()));
        if (item_is_error(delay.get())) return delay.get();
        delay.set(js_retain_scalar(delay.get()));
        if (item_is_error(delay.get())) return delay.get();
    }
    thunk.set(mvp_lmd_host_task(callback.get(), microtask ? Item{.item = ITEM_JS_UNDEFINED} : *state->global,
        arguments.get()));
    if (item_is_error(thunk.get())) return thunk.get();
    if (microtask) {
        js_microtask_enqueue(thunk.get());
        return Item{.item = ITEM_JS_UNDEFINED};
    }
    Item timer = js_schedule_timer(thunk.get(), delay.get(), ItemNull, false, interval, !state->native_timers);
    return item_is_error(timer) || get_type_id(timer) == LMD_TYPE_INT ||
        (state->native_timers && get_type_id(timer) == LMD_TYPE_MAP) ? timer : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}

static Item host_clear_timer(Context*, MvpLmdProgram*, Item* args, uint64_t count,
        Item self, Item, Item) {
    MvpLmdHostState* state = (MvpLmdHostState*)((MvpLmdNativeCallable*)self.function)->state;
    // native handles retain their resource identity; numeric callers share the same registry.
    if (count) {
        if (state->native_timers && get_type_id(args[0]) == LMD_TYPE_MAP) {
            js_clearTimeout(args[0]);
            return make_js_undefined();
        }
        Item id = mvp_lmd_primitive_to_number(args[0]);
        if (item_is_error(id)) return id;
        double number = get_type_id(id) == LMD_TYPE_FLOAT ? it2d(id) :
            get_type_id(id) == LMD_TYPE_INT64 ? (double)it2l(id) : (double)it2i(id);
        if (isfinite(number) && number >= 0 && number <= (double)INT53_MAX)
            js_clearTimeout(Item{.item = i2it((int64_t)number)});
    }
    return Item{.item = ITEM_JS_UNDEFINED};
}

Item mvp_lmd_host_initialize(MvpLmdProgram* program, MvpLmdHostState* state,
        TypeMap* shape, const MvpLmdHost* host) {
    if (!context->name_pool) context->name_pool = name_pool_create_runtime(context->pool);
    if (!context->name_pool) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    Item result = ItemNull;
    state->native_timers = host && host->native_timers;
    MvpLmdLibraryState* library = mvp_lmd_program_library(program);
    bool native_realm = library && library->native_callable_values;
    // dynamic modules need timers even when the embedder supplies no console/filesystem host.
    if (state->values && host) {
        state->output = host->output ? host->output : stdout;
        for (int i = 0; i < LMD_HOST_COUNT; i++) {
            state->values[i] = i == LMD_HOST_REQUIRE ? mvp_lmd_native_function(program, state, host_require, 1)
                : i == LMD_HOST_PRINT ? mvp_lmd_native_function(program, state, host_console_log<>, 1, "print")
                : i == LMD_HOST_PROCESS ? (state->process_requested || (host->output && host->output != stdout)
                    ? (state->process_stdio_only ? js_get_process_object_value() : js_get_profiled_process_object())
                    : make_js_undefined())
                : native_realm ? (i == LMD_HOST_CONSOLE ? js_get_console_object_value()
                    : js_get_key_cstr(*state->global, "performance"))
                : mvp_lmd_object_new(shape, 0);
            if (item_is_error(state->values[i])) return state->values[i];
        }
        if (get_type_id(state->values[LMD_HOST_PROCESS]) == LMD_TYPE_MAP) {
            js_set_process_argv(host->argc, (const char**)host->argv);
            js_set_key_cstr(state->values[LMD_HOST_PROCESS], "argv", js_get_process_argv());
        }
        if (host->output && host->output != stdout) {
            // an embedder's output sink is installed on the canonical process namespace.
            RootFrame roots(2);
            if (!roots.valid()) return ItemError;
            Rooted<Item> stream(roots, js_get_key_cstr(state->values[LMD_HOST_PROCESS], "stdout")),
                writer(roots, mvp_lmd_native_function(program, state, host_stdout_write, 1, "write"));
            if (item_is_error(stream.get()) || item_is_error(writer.get())) return ItemError;
            js_set_key_cstr(stream.get(), "write", writer.get());
        }
        struct Binding { int owner; const char* key; MvpLmdNativeEntry entry; uint8_t arity; };
        const Binding functions[] = {
            {LMD_HOST_PERFORMANCE, "now", host_clock, 0},
            {LMD_HOST_CONSOLE, "log", host_console_log<>, 0},
            {LMD_HOST_CONSOLE, "info", host_console_log<>, 0},
            {LMD_HOST_CONSOLE, "debug", host_console_log<>, 0},
            {LMD_HOST_CONSOLE, "error", host_console_log<true>, 0},
            {LMD_HOST_CONSOLE, "warn", host_console_log<true>, 0}
        };
        for (const Binding& binding : functions) {
            if (native_realm) break; // prepared host namespaces retain their methods and descriptors.
            result = mvp_lmd_named_set(state->values[binding.owner], binding.key,
                mvp_lmd_native_function(program, state, binding.entry, binding.arity, binding.key));
            if (item_is_error(result)) return result;
        }
        if (!native_realm) result = mvp_lmd_named_set(state->values[LMD_HOST_PERFORMANCE], "timeOrigin",
            push_d(js_performance_time_origin_ms()));
        if (item_is_error(result)) return result;
        static const MvpLmdConsoleLabelBinding labels[] = {
            {"count", js_console_count_fn}, {"countReset", js_console_countReset_fn},
            {"time", js_console_time_fn}, {"timeEnd", js_console_timeEnd_fn},
            {"timeLog", js_console_timeLog_fn}
        };
        for (const auto& binding : labels) {
            if (native_realm) break;
            result = mvp_lmd_named_set(state->values[LMD_HOST_CONSOLE], binding.name,
                mvp_lmd_native_function(program, (void*)&binding, host_console_label, 0, binding.name));
            if (item_is_error(result)) return result;
        }
    }
    if (state->timers) {
        const char* names[] = {"setTimeout", "clearTimeout", "setInterval", "clearInterval", "queueMicrotask"};
        for (int i = 0; i < LMD_TIMER_COUNT; i++) {
            if (native_realm) {
                const JsBuiltinGlobalSpec* spec = js_builtin_global_find(names[i], strlen(names[i]));
                state->timers[i] = js_get_global_builtin_fn_by_id(Item{.item = i2it(spec->id)});
                if (item_is_error(state->timers[i])) return state->timers[i];
                continue;
            }
            bool clear = i == LMD_TIMER_CLEAR_TIMEOUT || i == LMD_TIMER_CLEAR_INTERVAL;
            state->timers[i] = mvp_lmd_native_function(program, state,
                clear ? host_clear_timer : host_schedule, clear || i == LMD_TIMER_MICROTASK ? 1 : 2, names[i]);
            if (item_is_error(state->timers[i])) return state->timers[i];
            ((MvpLmdCallable*)state->timers[i].function)->non_constructible = true;
        }
    }
    return result;
}
