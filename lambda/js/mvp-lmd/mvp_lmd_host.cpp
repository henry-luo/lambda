#include "mvp_lmd.h"
#include "mvp_lmd_runtime.h"
#include "../js_props.h"
#include "../js_runtime.h"
#include "../js_event_loop.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/heap_api.h"
#include "../../runtime/runtime-state.h"
#include "../../runtime/lambda-error.h"
#include "../../input/input.hpp"
#include "../../../lib/file.h"
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

static Item host_clock(Context*, MvpLmdProgram*, Item*, uint64_t, Item self, Item, Item) {
    MvpLmdHostState* state = (MvpLmdHostState*)((MvpLmdNativeCallable*)self.function)->state;
    // d2it tags a pointer; the shared scalar publisher owns this numeric result.
    return push_d((pn_clock() - state->origin) * 1000.0);
}

static Item host_output(Item self, Item* arguments, uint64_t count, bool line) {
    MvpLmdHostState* state = (MvpLmdHostState*)((MvpLmdNativeCallable*)self.function)->state;
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> text(roots, ItemNull);
    for (uint64_t i = 0; i < count; i++) {
        text.set(mvp_lmd_primitive_to_string(arguments[i]));
        if (item_is_error(text.get())) return text.get();
        size_t size; char* owned;
        const char* bytes = host_utf8_bytes(text.get().get_string(), &size, &owned);
        if (!bytes) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        bool failed = (line && i && fputc(' ', state->output) == EOF) ||
            fwrite(bytes, 1, size, state->output) != size;
        mem_free(owned);
        if (failed)
            return err2it(err_create_heap(ERR_FILE_WRITE_ERROR, "MVP stdout write failed", NULL));
    }
    if (line && fputc('\n', state->output) == EOF)
        return err2it(err_create_heap(ERR_FILE_WRITE_ERROR, "MVP stdout write failed", NULL));
    return Item{.item = line ? ITEM_JS_UNDEFINED : ITEM_TRUE};
}
static Item host_console_log(Context*, MvpLmdProgram*, Item* arguments, uint64_t count, Item self, Item, Item) {
    return host_output(self, arguments, count, true);
}
static Item host_stdout_write(Context*, MvpLmdProgram*, Item* arguments, uint64_t count, Item self, Item, Item) {
    if (!count || get_type_id(arguments[0]) != LMD_TYPE_STRING) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (count > 1) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    return host_output(self, arguments, 1, false);
}
static Item host_require(Context*, MvpLmdProgram*, Item* arguments, uint64_t count, Item self, Item, Item) {
    if (!count || get_type_id(arguments[0]) != LMD_TYPE_STRING) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    String* module = arguments[0].get_string();
    if (module->len != 2 || memcmp(module->chars, "fs", 2)) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    return ((MvpLmdHostState*)((MvpLmdNativeCallable*)self.function)->state)->values[LMD_HOST_FS];
}
static Item host_read_file(Context*, MvpLmdProgram*, Item* arguments, uint64_t count, Item, Item, Item) {
    if (count < 2 || get_type_id(arguments[0]) != LMD_TYPE_STRING ||
            get_type_id(arguments[1]) != LMD_TYPE_STRING) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    String* path = arguments[0].get_string();
    String* encoding = arguments[1].get_string();
    if (memchr(path->chars, 0, path->len) || !utf8_valid(path->chars, path->len))
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (!((encoding->len == 4 && !memcmp(encoding->chars, "utf8", 4)) ||
          (encoding->len == 5 && !memcmp(encoding->chars, "utf-8", 5))))
        return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    char* bytes = NULL; size_t length = 0;
    if (!file_read_all(path->chars, MEM_CAT_JS_RUNTIME, &bytes, &length))
        return err2it(err_create_heap(ERR_FILE_READ_ERROR, "MVP file read failed", NULL));
    if (!utf8_valid(bytes, length)) {
        mem_free(bytes);
        return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    }
    String* result = heap_strcpy(bytes, length);
    mem_free(bytes);
    return result ? Item{.item = s2it(result)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}
static Item host_write_file(Context*, MvpLmdProgram*, Item* arguments, uint64_t count, Item, Item, Item) {
    if (count < 2 || get_type_id(arguments[0]) != LMD_TYPE_STRING ||
            get_type_id(arguments[1]) != LMD_TYPE_STRING) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (count > 2) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    String* path = arguments[0].get_string(); String* data = arguments[1].get_string();
    if (memchr(path->chars, 0, path->len) || !utf8_valid(path->chars, path->len))
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    size_t size; char* owned;
    const char* bytes = host_utf8_bytes(data, &size, &owned);
    if (!bytes) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    int failed = write_binary_file(path->chars, bytes, size);
    mem_free(owned);
    if (failed)
        return err2it(err_create_heap(ERR_FILE_WRITE_ERROR, "MVP file write failed", NULL));
    return Item{.item = ITEM_JS_UNDEFINED};
}
Item mvp_lmd_native_function(MvpLmdProgram* program, void* state, MvpLmdNativeEntry entry, uint8_t arity,
        const char* name) {
    MvpLmdNativeCallable* function = (MvpLmdNativeCallable*)heap_calloc(sizeof(MvpLmdNativeCallable), LMD_TYPE_FUNC);
    if (!function) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    function_init_abi(function, LMD_TYPE_FUNC, FN_ENTRY_ABI_MVP_LMD);
    function->requires_runtime_context = true; function->runtime_context = context;
    function->ptr = (fn_ptr)entry; function->program = program;
    function->state = state; function->arity = arity;
    Item result = {.function = function};
    if (!name) return result;
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, result);
    String* spelling = heap_create_name(name, strlen(name));
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
    } else result = mvp_lmd_property_set(object.get(), property.get(), held.get());
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

static Item host_schedule(Context*, MvpLmdProgram*, Item* args, uint64_t count,
        Item self, Item, Item) {
    MvpLmdHostState* state = (MvpLmdHostState*)((MvpLmdNativeCallable*)self.function)->state;
    bool microtask = self.item == state->timers[LMD_TIMER_MICROTASK].item;
    bool interval = self.item == state->timers[LMD_TIMER_INTERVAL].item;
    if (!count || !mvp_lmd_is_callable(args[0])) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (count > INT_MAX) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    uint64_t delay_home = 0;
    Rooted<Item> callback(roots, args[0]),
        delay(roots, lambda_item_adopt_scalar_home(count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED}, &delay_home)),
        arguments(roots, ItemNull), thunk(roots, ItemNull);
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
    Item* environment = js_alloc_env(4);
    if (!environment) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    environment[0] = callback.get();
    environment[1] = microtask ? Item{.item = ITEM_JS_UNDEFINED} : *state->global;
    environment[2] = arguments.get();
    environment[3] = Item{.item = i2it(lambda_active_module_state_id())};
    thunk.set(js_new_native_closure(host_task_call, 0, environment, 4));
    if (item_is_error(thunk.get())) return thunk.get();
    if (microtask) {
        js_microtask_enqueue(thunk.get());
        return Item{.item = ITEM_JS_UNDEFINED};
    }
    Item timer = js_schedule_timer(thunk.get(), delay.get(), ItemNull, false, interval, true);
    return item_is_error(timer) || get_type_id(timer) == LMD_TYPE_INT ? timer : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
}

static Item host_clear_timer(Context*, MvpLmdProgram*, Item* args, uint64_t count,
        Item, Item, Item) {
    // both clear functions share the numeric timer identity and cancellation registry.
    if (count) {
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
    // dynamic modules need timers even when the embedder supplies no console/filesystem host.
    if (state->values && host) {
        state->output = host->output ? host->output : stdout;
        RootFrame roots(2);
        if (!roots.valid()) return ItemError;
        Rooted<Item> temporary(roots, ItemNull), arguments(roots, ItemNull);
        for (int i = 0; i < LMD_HOST_COUNT; i++) {
            state->values[i] = i == LMD_HOST_REQUIRE ? mvp_lmd_native_function(program, state, host_require, 1)
                : i == LMD_HOST_PRINT ? mvp_lmd_native_function(program, state, host_console_log, 1, "print")
                : mvp_lmd_object_new(shape, 0);
            if (item_is_error(state->values[i])) return state->values[i];
        }
        struct Binding { int owner; const char* key; MvpLmdNativeEntry entry; uint8_t arity; };
        const Binding functions[] = {
            {LMD_HOST_PERFORMANCE, "now", host_clock, 0},
            {LMD_HOST_CONSOLE, "log", host_console_log, 0},
            {LMD_HOST_FS, "readFileSync", host_read_file, 2},
            {LMD_HOST_FS, "writeFileSync", host_write_file, 2}
        };
        for (const Binding& binding : functions) {
            result = mvp_lmd_named_set(state->values[binding.owner], binding.key,
                mvp_lmd_native_function(program, state, binding.entry, binding.arity));
            if (item_is_error(result)) return result;
        }
        temporary.set(mvp_lmd_object_new(shape, 0));
        if (item_is_error(temporary.get())) return temporary.get();
        result = mvp_lmd_named_set(temporary.get(), "write", mvp_lmd_native_function(program, state, host_stdout_write, 1));
        if (item_is_error(result)) return result;
        result = mvp_lmd_named_set(state->values[LMD_HOST_PROCESS], "stdout", temporary.get());
        if (item_is_error(result)) return result;
        arguments.set(mvp_lmd_array_new(host->argc));
        if (item_is_error(arguments.get())) return arguments.get();
        for (int i = 0; i < host->argc; i++) {
            String* text = heap_strcpy(host->argv[i], strlen(host->argv[i]));
            if (!text) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            result = mvp_lmd_array_store(arguments.get(), i, Item{.item = s2it(text)});
            if (item_is_error(result)) return result;
        }
        result = mvp_lmd_named_set(state->values[LMD_HOST_PROCESS], "argv", arguments.get());
        if (item_is_error(result)) return result;
#if defined(_WIN32)
        const char* platform = "win32";
#elif defined(__APPLE__)
        const char* platform = "darwin";
#else
        const char* platform = "linux";
#endif
        String* name = heap_strcpy(platform, strlen(platform));
        if (!name) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        result = mvp_lmd_named_set(state->values[LMD_HOST_PROCESS], "platform", Item{.item = s2it(name)});
        if (item_is_error(result)) return result;
    }
    if (state->timers) {
        const char* names[] = {"setTimeout", "clearTimeout", "setInterval", "clearInterval", "queueMicrotask"};
        for (int i = 0; i < LMD_TIMER_COUNT; i++) {
            bool clear = i == LMD_TIMER_CLEAR_TIMEOUT || i == LMD_TIMER_CLEAR_INTERVAL;
            state->timers[i] = mvp_lmd_native_function(program, state,
                clear ? host_clear_timer : host_schedule, clear || i == LMD_TIMER_MICROTASK ? 1 : 2, names[i]);
            if (item_is_error(state->timers[i])) return state->timers[i];
            ((MvpLmdCallable*)state->timers[i].function)->non_constructible = true;
        }
    }
    state->origin = pn_clock();
    return result;
}
