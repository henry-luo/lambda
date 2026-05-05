/**
 * js_child_process.cpp — Node.js-style 'child_process' module for LambdaJS v15
 *
 * Provides exec() and execSync() backed by libuv's uv_spawn.
 * Registered as built-in module 'child_process' via js_module_get().
 */
#include "js_runtime.h"
#include "js_event_loop.h"
#include "js_error_codes.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"

#include <cstring>
#include "../../lib/mem.h"

#ifndef _WIN32
#include <sys/wait.h>
#else
#include <process.h>
// On Windows, pclose() returns the exit code directly
#define WIFEXITED(s)  (1)
#define WEXITSTATUS(s) (s)
#endif

extern Input* js_input;

// =============================================================================
// Helpers
// =============================================================================

static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

static Item make_string_item(const char* str, int len) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, (size_t)(len > 0 ? len : 0));
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

static const char* item_to_cstr(Item value, char* buf, int buf_size) {
    if (get_type_id(value) != LMD_TYPE_STRING) return NULL;
    String* s = it2s(value);
    int len = (int)s->len;
    if (len >= buf_size) len = buf_size - 1;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';
    return buf;
}

#ifdef _WIN32
static bool build_windows_shell_script_command(const char* cmd, char* script_path, int script_path_size,
                                               char* out, int out_size) {
    static int script_counter = 0;
    int id = ++script_counter;
    snprintf(script_path, script_path_size, "./temp/lambda_child_%ld_%d.sh", (long)_getpid(), id);
    FILE* script = fopen(script_path, "wb");
    if (!script) {
        out[0] = '\0';
        return false;
    }
    fputs("#!/usr/bin/env sh\n", script);
    fputs(cmd, script);
    fputc('\n', script);
    fclose(script);
    snprintf(out, out_size, "sh \"%s\"", script_path);
    return true;
}
#endif

// =============================================================================
// JsChildProcess — per-exec context
// =============================================================================

typedef struct JsChildProcess {
    uv_process_t process;
    uv_pipe_t    stdout_pipe;
    uv_pipe_t    stderr_pipe;

    // buffered output
    char*  stdout_buf;
    size_t stdout_len;
    size_t stdout_cap;
    char*  stderr_buf;
    size_t stderr_len;
    size_t stderr_cap;

    // callback for exec(): (err, stdout, stderr) => ...
    Item callback;

    // lifecycle tracking
    int    pipes_closed;      // count of closed pipes (need 2: stdout + stderr)
    int    exit_code;
    bool   process_exited;
    bool   callback_fired;    // true after callback has been invoked
    int    handles_closed;    // count of uv_close completions (need 3: process + 2 pipes)
} JsChildProcess;

// =============================================================================
// Allocation callback for uv_read_start
// =============================================================================

static void child_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)mem_alloc(suggested_size, MEM_CAT_JS_RUNTIME);
    buf->len = buf->base ? suggested_size : 0;
}

// =============================================================================
// Handle close callback — frees struct after all handles fully closed
// =============================================================================

static void maybe_complete(JsChildProcess* cp);

static void child_handle_close_cb(uv_handle_t* handle) {
    JsChildProcess* cp = (JsChildProcess*)handle->data;
    if (!cp) return;
    cp->handles_closed++;
    // all 3 handles (process + 2 pipes) fully closed → safe to free struct
    if (cp->handles_closed >= 3) {
        if (cp->stdout_buf) mem_free(cp->stdout_buf);
        if (cp->stderr_buf) mem_free(cp->stderr_buf);
        mem_free(cp);
    }
}

// =============================================================================
// Read callbacks — buffer stdout/stderr
// =============================================================================

static void child_stdout_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsChildProcess* cp = (JsChildProcess*)stream->data;
    if (nread > 0 && cp) {
        if (cp->stdout_len + (size_t)nread >= cp->stdout_cap) {
            size_t new_cap = (cp->stdout_cap == 0) ? 4096 : cp->stdout_cap * 2;
            while (new_cap < cp->stdout_len + (size_t)nread + 1) new_cap *= 2;
            char* nb = (char*)mem_realloc(cp->stdout_buf, new_cap, MEM_CAT_JS_RUNTIME);
            if (nb) { cp->stdout_buf = nb; cp->stdout_cap = new_cap; }
        }
        if (cp->stdout_buf) {
            memcpy(cp->stdout_buf + cp->stdout_len, buf->base, nread);
            cp->stdout_len += nread;
            cp->stdout_buf[cp->stdout_len] = '\0';
        }
    }
    if (buf->base) mem_free(buf->base);
    if (nread < 0) { // EOF or error
        uv_close((uv_handle_t*)stream, child_handle_close_cb);
        if (cp) {
            cp->pipes_closed++;
            maybe_complete(cp);
        }
    }
}

static void child_stderr_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsChildProcess* cp = (JsChildProcess*)stream->data;
    if (nread > 0 && cp) {
        if (cp->stderr_len + (size_t)nread >= cp->stderr_cap) {
            size_t new_cap = (cp->stderr_cap == 0) ? 4096 : cp->stderr_cap * 2;
            while (new_cap < cp->stderr_len + (size_t)nread + 1) new_cap *= 2;
            char* nb = (char*)mem_realloc(cp->stderr_buf, new_cap, MEM_CAT_JS_RUNTIME);
            if (nb) { cp->stderr_buf = nb; cp->stderr_cap = new_cap; }
        }
        if (cp->stderr_buf) {
            memcpy(cp->stderr_buf + cp->stderr_len, buf->base, nread);
            cp->stderr_len += nread;
            cp->stderr_buf[cp->stderr_len] = '\0';
        }
    }
    if (buf->base) mem_free(buf->base);
    if (nread < 0) { // EOF or error
        uv_close((uv_handle_t*)stream, child_handle_close_cb);
        if (cp) {
            cp->pipes_closed++;
            maybe_complete(cp);
        }
    }
}

// =============================================================================
// Process exit callback
// =============================================================================

static void child_exit_cb(uv_process_t* process, int64_t exit_status, int term_signal) {
    JsChildProcess* cp = (JsChildProcess*)process->data;
    if (!cp) return;

    cp->exit_code = (int)exit_status;
    cp->process_exited = true;

    uv_close((uv_handle_t*)process, child_handle_close_cb);
    maybe_complete(cp);
}

// =============================================================================
// Completion: called when both pipes closed AND process exited
// =============================================================================

static void maybe_complete(JsChildProcess* cp) {
    if (!cp->process_exited || cp->pipes_closed < 2) return;
    if (cp->callback_fired) return;  // already invoked
    cp->callback_fired = true;

    // call callback(err, stdout, stderr)
    if (get_type_id(cp->callback) == LMD_TYPE_FUNC) {
        Item err = ItemNull;
        if (cp->exit_code != 0) {
            char emsg[128];
            snprintf(emsg, sizeof(emsg), "Command failed with exit code %d", cp->exit_code);
            err = js_new_error(make_string_item(emsg));
        }
        Item stdout_str = cp->stdout_buf
            ? make_string_item(cp->stdout_buf, (int)cp->stdout_len)
            : make_string_item("");
        Item stderr_str = cp->stderr_buf
            ? make_string_item(cp->stderr_buf, (int)cp->stderr_len)
            : make_string_item("");

        Item args[3] = {err, stdout_str, stderr_str};
        js_call_function(cp->callback, ItemNull, args, 3);

        // flush microtasks after callback
        js_microtask_flush();
    }
    // NOTE: struct is freed by child_handle_close_cb when all handles are closed
}

// =============================================================================
// exec(command, callback) — async, buffers stdout/stderr
// =============================================================================

extern "C" Item js_cp_exec(Item command_item, Item callback_item) {
    char cmd_buf[4096];
    const char* cmd = item_to_cstr(command_item, cmd_buf, sizeof(cmd_buf));
    if (!cmd) {
        return js_throw_invalid_arg_type("command", "string", command_item);
    }

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        log_error("child_process: exec: event loop not initialized");
        return ItemNull;
    }

    JsChildProcess* cp = (JsChildProcess*)mem_calloc(1, sizeof(JsChildProcess), MEM_CAT_JS_RUNTIME);
    if (!cp) return ItemNull;

    cp->callback = callback_item;

    // init pipes
    uv_pipe_init(loop, &cp->stdout_pipe, 0);
    uv_pipe_init(loop, &cp->stderr_pipe, 0);
    cp->stdout_pipe.data = cp;
    cp->stderr_pipe.data = cp;

    // setup stdio
    uv_stdio_container_t stdio[3];
    stdio[0].flags = UV_IGNORE;
    stdio[1].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
    stdio[1].data.stream = (uv_stream_t*)&cp->stdout_pipe;
    stdio[2].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
    stdio[2].data.stream = (uv_stream_t*)&cp->stderr_pipe;

    // spawn process
    uv_process_options_t opts;
    memset(&opts, 0, sizeof(opts));
#ifdef _WIN32
    opts.file = "sh";
    char* args[] = {(char*)"sh", (char*)"-c", (char*)cmd, NULL};
#else
    opts.file = "/bin/sh";
    char* args[] = {(char*)"/bin/sh", (char*)"-c", (char*)cmd, NULL};
#endif
    opts.args = args;
    opts.stdio = stdio;
    opts.stdio_count = 3;
    opts.exit_cb = child_exit_cb;

    cp->process.data = cp;

    int r = uv_spawn(loop, &cp->process, &opts);
    if (r != 0) {
        log_error("child_process: exec: spawn failed: %s", uv_strerror(r));
        if (get_type_id(callback_item) == LMD_TYPE_FUNC) {
            Item err = js_new_error(make_string_item(uv_strerror(r)));
            Item args_cb[3] = {err, ItemNull, ItemNull};
            js_call_function(callback_item, ItemNull, args_cb, 3);
        }
        uv_close((uv_handle_t*)&cp->stdout_pipe, child_handle_close_cb);
        uv_close((uv_handle_t*)&cp->stderr_pipe, child_handle_close_cb);
        // process handle was never started, close it too for proper cleanup
        cp->handles_closed++;  // count process as already closed
        return ItemNull;
    }

    // start reading stdout/stderr
    uv_read_start((uv_stream_t*)&cp->stdout_pipe, child_alloc_cb, child_stdout_read_cb);
    uv_read_start((uv_stream_t*)&cp->stderr_pipe, child_alloc_cb, child_stderr_read_cb);

    return make_js_undefined();
}

// =============================================================================
// execSync(command[, options]) — synchronous, returns Buffer or string
// =============================================================================

extern "C" Item js_cp_execSync(Item command_item, Item options_item) {
    char cmd_buf[4096];
    const char* cmd = item_to_cstr(command_item, cmd_buf, sizeof(cmd_buf));
    if (!cmd) {
        return js_throw_invalid_arg_type("command", "string", command_item);
    }

    (void)options_item; // options not currently used

#ifdef _WIN32
    char shell_cmd[8192];
    char script_path[256];
    if (!build_windows_shell_script_command(cmd, script_path, sizeof(script_path), shell_cmd, sizeof(shell_cmd))) {
        log_error("child_process: execSync: failed to create shell script");
        return ItemNull;
    }
    FILE* fp = popen(shell_cmd, "r");
#else
    FILE* fp = popen(cmd, "r");
#endif
    if (!fp) {
        log_error("child_process: execSync: popen failed for '%s'", cmd);
        return ItemNull;
    }

    char* result_buf = NULL;
    size_t result_len = 0;
    size_t result_cap = 0;
    char chunk[4096];

    while (fgets(chunk, sizeof(chunk), fp)) {
        size_t chunk_len = strlen(chunk);
        if (result_len + chunk_len >= result_cap) {
            size_t new_cap = (result_cap == 0) ? 4096 : result_cap * 2;
            while (new_cap < result_len + chunk_len + 1) new_cap *= 2;
            char* nb = (char*)mem_realloc(result_buf, new_cap, MEM_CAT_JS_RUNTIME);
            if (!nb) break;
            result_buf = nb;
            result_cap = new_cap;
        }
        memcpy(result_buf + result_len, chunk, chunk_len);
        result_len += chunk_len;
    }

    int status = pclose(fp);
#ifdef _WIN32
    remove(script_path);
#endif
    (void)status; // ignore exit code for execSync

    if (result_buf) {
        result_buf[result_len] = '\0';
        // trim trailing newline
        while (result_len > 0 && (result_buf[result_len - 1] == '\n' || result_buf[result_len - 1] == '\r')) {
            result_len--;
        }
        Item result = make_string_item(result_buf, (int)result_len);
        mem_free(result_buf);
        return result;
    }

    return make_string_item("");
}

// =============================================================================
// spawn(command, args) — async, returns ChildProcess-like object with
// stdout/stderr EventEmitter-like on('data',cb) and on('close',cb)
// =============================================================================

typedef struct JsSpawnProcess {
    uv_process_t process;
    uv_pipe_t    stdout_pipe;
    uv_pipe_t    stderr_pipe;
    Item         js_object;    // the JS object returned to user
    int          exit_code;
    bool         process_exited;
    int          handles_closed;
} JsSpawnProcess;

static void spawn_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)mem_alloc(suggested_size, MEM_CAT_JS_RUNTIME);
    buf->len = buf->base ? suggested_size : 0;
}

static void spawn_handle_close_cb(uv_handle_t* handle) {
    JsSpawnProcess* sp = (JsSpawnProcess*)handle->data;
    if (!sp) return;
    sp->handles_closed++;
    if (sp->handles_closed >= 3) {
        mem_free(sp);
    }
}

// emit 'data' event on a stream object
static void spawn_emit_data(Item stream_obj, const char* data, int len) {
    Item on_data = js_property_get(stream_obj, make_string_item("__on_data__"));
    if (get_type_id(on_data) == LMD_TYPE_FUNC) {
        Item data_str = make_string_item(data, len);
        js_call_function(on_data, ItemNull, &data_str, 1);
    }
}

static void spawn_stdout_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsSpawnProcess* sp = (JsSpawnProcess*)stream->data;
    if (nread > 0 && sp) {
        Item stdout_obj = js_property_get(sp->js_object, make_string_item("stdout"));
        spawn_emit_data(stdout_obj, buf->base, (int)nread);
    }
    if (buf->base) mem_free(buf->base);
    if (nread < 0) {
        uv_close((uv_handle_t*)stream, spawn_handle_close_cb);
    }
}

static void spawn_stderr_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsSpawnProcess* sp = (JsSpawnProcess*)stream->data;
    if (nread > 0 && sp) {
        Item stderr_obj = js_property_get(sp->js_object, make_string_item("stderr"));
        spawn_emit_data(stderr_obj, buf->base, (int)nread);
    }
    if (buf->base) mem_free(buf->base);
    if (nread < 0) {
        uv_close((uv_handle_t*)stream, spawn_handle_close_cb);
    }
}

static void spawn_exit_cb(uv_process_t* process, int64_t exit_status, int term_signal) {
    JsSpawnProcess* sp = (JsSpawnProcess*)process->data;
    if (!sp) return;
    sp->exit_code = (int)exit_status;
    sp->process_exited = true;

    // emit 'close' on the js_object
    Item on_close = js_property_get(sp->js_object, make_string_item("__on_close__"));
    if (get_type_id(on_close) == LMD_TYPE_FUNC) {
        Item code = (Item){.item = i2it(exit_status)};
        js_call_function(on_close, ItemNull, &code, 1);
        js_microtask_flush();
    }

    uv_close((uv_handle_t*)process, spawn_handle_close_cb);
}

// create a stream-like object with on('data', cb) / on('close', cb)
static Item make_stream_object(void) {
    Item obj = js_new_object();
    // on(event, listener) — stores callbacks in hidden properties
    // simplified: just stores __on_data__ and __on_close__
    return obj;
}

// on(event, callback) for spawn objects — stores as __on_<event>__
extern "C" Item js_spawn_on(Item self, Item event_item, Item callback) {
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    String* ev = it2s(event_item);
    char key_buf[64];
    snprintf(key_buf, sizeof(key_buf), "__on_%.*s__", (int)ev->len, ev->chars);
    js_property_set(self, make_string_item(key_buf), callback);
    return self;
}

// on() for stdout/stderr stream sub-objects
extern "C" Item js_spawn_stream_on(Item self, Item event_item, Item callback) {
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    String* ev = it2s(event_item);
    char key_buf[64];
    snprintf(key_buf, sizeof(key_buf), "__on_%.*s__", (int)ev->len, ev->chars);
    js_property_set(self, make_string_item(key_buf), callback);
    return self;
}

extern "C" Item js_cp_spawn(Item command_item, Item args_item) {
    char cmd_buf[4096];
    const char* cmd = item_to_cstr(command_item, cmd_buf, sizeof(cmd_buf));
    if (!cmd) {
        return js_throw_invalid_arg_type("command", "string", command_item);
    }

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        log_error("child_process: spawn: event loop not initialized");
        return ItemNull;
    }

    // build args array from JS array
    int argc = 0;
    char** argv = NULL;

    if (get_type_id(args_item) == LMD_TYPE_ARRAY) {
        argc = (int)js_array_length(args_item);
    }

    // argv = [cmd, ...args, NULL]
    argv = (char**)mem_alloc(sizeof(char*) * (size_t)(argc + 2), MEM_CAT_JS_RUNTIME);
    argv[0] = (char*)cmd;
    for (int i = 0; i < argc; i++) {
        Item arg = js_array_get_int(args_item, i);
        if (get_type_id(arg) == LMD_TYPE_STRING) {
            String* s = it2s(arg);
            char* copy = (char*)mem_alloc(s->len + 1, MEM_CAT_JS_RUNTIME);
            memcpy(copy, s->chars, s->len);
            copy[s->len] = '\0';
            argv[i + 1] = copy;
        } else {
            argv[i + 1] = (char*)"";
        }
    }
    argv[argc + 1] = NULL;

    JsSpawnProcess* sp = (JsSpawnProcess*)mem_calloc(1, sizeof(JsSpawnProcess), MEM_CAT_JS_RUNTIME);

    // create JS object with stdout/stderr sub-objects
    Item obj = js_new_object();
    Item stdout_obj = make_stream_object();
    Item stderr_obj = make_stream_object();
    js_property_set(stdout_obj, make_string_item("on"),
                    js_new_function((void*)js_spawn_stream_on, 3));
    js_property_set(stderr_obj, make_string_item("on"),
                    js_new_function((void*)js_spawn_stream_on, 3));
    js_property_set(obj, make_string_item("stdout"), stdout_obj);
    js_property_set(obj, make_string_item("stderr"), stderr_obj);
    js_property_set(obj, make_string_item("on"),
                    js_new_function((void*)js_spawn_on, 3));
    js_property_set(obj, make_string_item("pid"), (Item){.item = i2it(0)});

    sp->js_object = obj;

    uv_pipe_init(loop, &sp->stdout_pipe, 0);
    uv_pipe_init(loop, &sp->stderr_pipe, 0);
    sp->stdout_pipe.data = sp;
    sp->stderr_pipe.data = sp;

    uv_stdio_container_t stdio[3];
    stdio[0].flags = UV_IGNORE;
    stdio[1].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
    stdio[1].data.stream = (uv_stream_t*)&sp->stdout_pipe;
    stdio[2].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
    stdio[2].data.stream = (uv_stream_t*)&sp->stderr_pipe;

    uv_process_options_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.file = cmd;
    opts.args = argv;
    opts.stdio = stdio;
    opts.stdio_count = 3;
    opts.exit_cb = spawn_exit_cb;

    sp->process.data = sp;

    int r = uv_spawn(loop, &sp->process, &opts);

    // free arg copies
    for (int i = 0; i < argc; i++) {
        if (argv[i + 1] && argv[i + 1][0] != '\0') {
            Item arg = js_array_get_int(args_item, i);
            if (get_type_id(arg) == LMD_TYPE_STRING) {
                String* s = it2s(arg);
                if (s->len > 0) mem_free(argv[i + 1]);
            }
        }
    }
    mem_free(argv);

    if (r != 0) {
        log_error("child_process: spawn: failed: %s", uv_strerror(r));
        uv_close((uv_handle_t*)&sp->stdout_pipe, spawn_handle_close_cb);
        uv_close((uv_handle_t*)&sp->stderr_pipe, spawn_handle_close_cb);
        sp->handles_closed++;
        return ItemNull;
    }

    js_property_set(obj, make_string_item("pid"),
                    (Item){.item = i2it(sp->process.pid)});

    uv_read_start((uv_stream_t*)&sp->stdout_pipe, spawn_alloc_cb, spawn_stdout_read_cb);
    uv_read_start((uv_stream_t*)&sp->stderr_pipe, spawn_alloc_cb, spawn_stderr_read_cb);

    return obj;
}

// =============================================================================
// spawnSync(command, args) — synchronous, returns {stdout, stderr, status}
// =============================================================================

extern "C" Item js_cp_spawnSync(Item command_item, Item args_item) {
    // build full command line for popen
    char cmd_buf[4096];
    const char* cmd = item_to_cstr(command_item, cmd_buf, sizeof(cmd_buf));
    if (!cmd) {
        log_error("child_process: spawnSync: invalid command");
        return ItemNull;
    }

    // build command string: cmd arg1 arg2 ...
    char full_cmd[8192];
    int pos = snprintf(full_cmd, sizeof(full_cmd), "%s", cmd);

    if (get_type_id(args_item) == LMD_TYPE_ARRAY) {
        int64_t alen = js_array_length(args_item);
        for (int64_t i = 0; i < alen && pos < (int)sizeof(full_cmd) - 256; i++) {
            Item arg = js_array_get_int(args_item, i);
            if (get_type_id(arg) == LMD_TYPE_STRING) {
                String* s = it2s(arg);
                pos += snprintf(full_cmd + pos, sizeof(full_cmd) - (size_t)pos,
                                " %.*s", (int)s->len, s->chars);
            }
        }
    }

    // redirect stderr to a temp approach — capture stdout via popen
    FILE* fp = popen(full_cmd, "r");
    if (!fp) {
        log_error("child_process: spawnSync: popen failed");
        Item result = js_new_object();
        js_property_set(result, make_string_item("status"), (Item){.item = i2it(-1)});
        js_property_set(result, make_string_item("stdout"), make_string_item(""));
        js_property_set(result, make_string_item("stderr"), make_string_item(""));
        return result;
    }

    char* out_buf = NULL;
    size_t out_len = 0;
    size_t out_cap = 0;
    char chunk[4096];
    while (fgets(chunk, sizeof(chunk), fp)) {
        size_t clen = strlen(chunk);
        if (out_len + clen >= out_cap) {
            out_cap = (out_cap == 0) ? 4096 : out_cap * 2;
            while (out_cap < out_len + clen + 1) out_cap *= 2;
            out_buf = (char*)mem_realloc(out_buf, out_cap, MEM_CAT_JS_RUNTIME);
        }
        memcpy(out_buf + out_len, chunk, clen);
        out_len += clen;
    }
    int status = pclose(fp);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    Item result = js_new_object();
    js_property_set(result, make_string_item("status"), (Item){.item = i2it(exit_code)});
    js_property_set(result, make_string_item("stdout"),
                    out_buf ? make_string_item(out_buf, (int)out_len) : make_string_item(""));
    js_property_set(result, make_string_item("stderr"), make_string_item(""));
    if (out_buf) mem_free(out_buf);

    return result;
}

// =============================================================================
// child_process Module Namespace
// =============================================================================

static Item cp_namespace = {0};

static void js_cp_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

extern "C" Item js_get_child_process_namespace(void) {
    if (cp_namespace.item != 0) return cp_namespace;

    cp_namespace = js_new_object();

    js_cp_set_method(cp_namespace, "exec",       (void*)js_cp_exec, 2);
    js_cp_set_method(cp_namespace, "execSync",   (void*)js_cp_execSync, 2);
    js_cp_set_method(cp_namespace, "spawn",      (void*)js_cp_spawn, 2);
    js_cp_set_method(cp_namespace, "spawnSync",  (void*)js_cp_spawnSync, 2);
    js_cp_set_method(cp_namespace, "execFile",   (void*)js_cp_exec, 2); // alias for now
    js_cp_set_method(cp_namespace, "execFileSync", (void*)js_cp_execSync, 2);

    // set "default" for `import cp from 'child_process'`
    Item default_key = make_string_item("default");
    js_property_set(cp_namespace, default_key, cp_namespace);

    return cp_namespace;
}

extern "C" void js_child_process_reset(void) {
    cp_namespace = (Item){0};
}
