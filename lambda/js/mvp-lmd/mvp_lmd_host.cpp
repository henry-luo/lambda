#include "mvp_lmd.h"
#include "mvp_lmd_runtime.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/heap_api.h"
#include "../../runtime/runtime-state.h"
#include "../../runtime/lambda-error.h"
#include "../../input/input.hpp"
#include "../../../lib/file.h"
#include "../../../lib/mem.h"
#include "../../../lib/utf.h"

// host bindings are explicitly supplied by the embedder, outside the core globals allowlist.
struct MvpLmdHostCallable : MvpLmdCallable { MvpLmdHostState* state; };
typedef Item (*HostEntry)(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);

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
    MvpLmdHostState* state = ((MvpLmdHostCallable*)self.function)->state;
    // d2it tags a pointer; the shared scalar publisher owns this numeric result.
    return push_d((pn_clock() - state->origin) * 1000.0);
}

static Item host_output(Item self, Item* arguments, uint64_t count, bool line) {
    MvpLmdHostState* state = ((MvpLmdHostCallable*)self.function)->state;
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
    return ((MvpLmdHostCallable*)self.function)->state->values[LMD_HOST_FS];
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
static Item host_function(MvpLmdProgram* program, MvpLmdHostState* state, HostEntry entry, uint8_t arity) {
    MvpLmdHostCallable* function = (MvpLmdHostCallable*)heap_calloc(sizeof(MvpLmdHostCallable), LMD_TYPE_FUNC);
    if (!function) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    function_init_abi(function, LMD_TYPE_FUNC, FN_ENTRY_ABI_MVP_LMD);
    function->requires_runtime_context = true; function->runtime_context = context;
    function->ptr = (fn_ptr)entry; function->program = program;
    function->state = state; function->arity = arity;
    return Item{.function = function};
}
static Item host_set(Item owner, const char* key, Item value) {
    if (item_is_error(value)) return value;
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner), held(roots, value);
    String* name = heap_create_name(key, strlen(key));
    if (!name) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    return mvp_lmd_property_set(object.get(), Item{.item = s2it(name)}, held.get());
}
Item mvp_lmd_host_initialize(MvpLmdProgram* program, MvpLmdHostState* state,
        TypeMap* shape, const MvpLmdHost* host) {
    if (!context->name_pool) context->name_pool = name_pool_create_runtime(context->pool);
    if (!context->name_pool) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    state->output = host->output ? host->output : stdout;
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> temporary(roots, ItemNull), arguments(roots, ItemNull);
    for (int i = 0; i < LMD_HOST_COUNT; i++) {
        state->values[i] = i == LMD_HOST_REQUIRE ? host_function(program, state, host_require, 1)
            : mvp_lmd_object_new(shape, 0);
        if (item_is_error(state->values[i])) return state->values[i];
    }
    struct Binding { int owner; const char* key; HostEntry entry; uint8_t arity; };
    const Binding functions[] = {
        {LMD_HOST_PERFORMANCE, "now", host_clock, 0},
        {LMD_HOST_CONSOLE, "log", host_console_log, 0},
        {LMD_HOST_FS, "readFileSync", host_read_file, 2},
        {LMD_HOST_FS, "writeFileSync", host_write_file, 2}
    };
    for (const Binding& binding : functions) {
        Item result = host_set(state->values[binding.owner], binding.key,
            host_function(program, state, binding.entry, binding.arity));
        if (item_is_error(result)) return result;
    }
    temporary.set(mvp_lmd_object_new(shape, 0));
    if (item_is_error(temporary.get())) return temporary.get();
    Item result = host_set(temporary.get(), "write", host_function(program, state, host_stdout_write, 1));
    if (item_is_error(result)) return result;
    result = host_set(state->values[LMD_HOST_PROCESS], "stdout", temporary.get());
    if (item_is_error(result)) return result;
    arguments.set(mvp_lmd_array_new(host->argc));
    if (item_is_error(arguments.get())) return arguments.get();
    for (int i = 0; i < host->argc; i++) {
        String* text = heap_strcpy(host->argv[i], strlen(host->argv[i]));
        if (!text) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        result = mvp_lmd_array_store(arguments.get(), i, Item{.item = s2it(text)});
        if (item_is_error(result)) return result;
    }
    result = host_set(state->values[LMD_HOST_PROCESS], "argv", arguments.get());
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
    result = host_set(state->values[LMD_HOST_PROCESS], "platform", Item{.item = s2it(name)});
    state->origin = pn_clock();
    return result;
}
