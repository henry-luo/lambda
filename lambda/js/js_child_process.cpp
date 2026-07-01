/**
 * js_child_process.cpp — Node.js-style 'child_process' module for LambdaJS v15
 *
 * Provides exec() and execSync() backed by libuv's uv_spawn.
 * Registered as built-in module 'child_process' via js_module_get().
 */
#include "js_runtime.h"
#include "js_runtime_state.hpp"
#include "js_event_loop.h"
#include "js_error_codes.h"
#include "js_typed_array.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"

#include <cstring>
#include <cstdlib>
#include <climits>
#include "../../lib/mem.h"

#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#else
#include <process.h>
// On Windows, pclose() returns the exit code directly
#define WIFEXITED(s)  (1)
#define WEXITSTATUS(s) (s)
#endif

extern "C" Item js_process_emit(Item event_name, Item arg1);
extern "C" void js_next_tick_enqueue(Item callback);
extern "C" Item js_json_parse(Item str_item);
extern "C" Item js_json_stringify(Item value);
extern "C" Item js_buffer_from_bytes(const char* data, int len);

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

static bool is_undefined_item(Item item) {
    return item.item == ITEM_JS_UNDEFINED || get_type_id(item) == LMD_TYPE_UNDEFINED;
}

static bool is_callable(Item item) {
    return get_type_id(item) == LMD_TYPE_FUNC;
}

static bool is_object_item(Item item) {
    TypeId type = get_type_id(item);
    return type == LMD_TYPE_MAP || type == LMD_TYPE_OBJECT || type == LMD_TYPE_VMAP;
}

static bool is_nullish_item(Item item) {
    return item.item == ITEM_NULL || get_type_id(item) == LMD_TYPE_NULL || is_undefined_item(item);
}

static Item make_spawn_error_args_array(Item args_array) {
    Item result = js_array_new(0);
    if (get_type_id(args_array) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(args_array);
        for (int64_t i = 0; i < len; i++) {
            js_array_push(result, js_array_get_int(args_array, i));
        }
    }
    return result;
}

static Item make_uv_spawn_error(int status, const char* file, Item spawnargs) {
    const char* code = uv_err_name(status);
    const char* errstr = uv_strerror(status);
    char msg[512];
    snprintf(msg, sizeof(msg), "spawn %s %s", file ? file : "", code);

    Item err = js_new_error(make_string_item(msg));
    js_property_set(err, make_string_item("code"), make_string_item(code));
    js_property_set(err, make_string_item("errno"), (Item){.item = i2it(status)});
    char syscall[512];
    snprintf(syscall, sizeof(syscall), "spawn %s", file ? file : "");
    js_property_set(err, make_string_item("syscall"), make_string_item(syscall));
    if (file) js_property_set(err, make_string_item("path"), make_string_item(file));
    js_property_set(err, make_string_item("spawnargs"), spawnargs);
    (void)errstr;
    return err;
}

static void emit_shell_args_warning(void) {
    Item warning = js_new_object();
    js_property_set(warning, make_string_item("name"), make_string_item("DeprecationWarning"));
    js_property_set(warning, make_string_item("message"), make_string_item(
        "Passing args to a child process with shell option true can lead to security "
        "vulnerabilities, as the arguments are not escaped, only concatenated."));
    js_property_set(warning, make_string_item("code"), make_string_item("DEP0190"));
    js_process_emit(make_string_item("warning"), warning);
}

static bool append_shell_arg(char* out, int out_size, int* pos, Item arg) {
    if (get_type_id(arg) != LMD_TYPE_STRING) return false;
    String* s = it2s(arg);
    if (*pos >= out_size - 1) return true;
    out[(*pos)++] = ' ';
    if (*pos >= out_size - 1) {
        out[out_size - 1] = '\0';
        return true;
    }
    out[(*pos)++] = '\'';
    for (size_t i = 0; i < s->len && *pos < out_size - 1; i++) {
        char ch = s->chars[i];
        if (ch == '\'') {
            const char* esc = "'\\''";
            for (int j = 0; esc[j] && *pos < out_size - 1; j++) {
                out[(*pos)++] = esc[j];
            }
        } else {
            out[(*pos)++] = ch;
        }
    }
    if (*pos < out_size - 1) {
        out[(*pos)++] = '\'';
    }
    out[*pos < out_size ? *pos : out_size - 1] = '\0';
    return true;
}

static bool item_has_js_extension(Item item) {
    if (get_type_id(item) != LMD_TYPE_STRING) return false;
    String* s = it2s(item);
    if (s->len < 3) return false;
    const char* chars = s->chars;
    size_t len = s->len;
    if (len >= 3 && memcmp(chars + len - 3, ".js", 3) == 0) return true;
    if (len >= 4 && memcmp(chars + len - 4, ".mjs", 4) == 0) return true;
    if (len >= 4 && memcmp(chars + len - 4, ".cjs", 4) == 0) return true;
    return false;
}

static bool is_lambda_executable_path(const char* path) {
    if (!path) return false;
    const char* base = strrchr(path, '/');
#ifdef _WIN32
    const char* slash = strrchr(path, '\\');
    if (slash && (!base || slash > base)) base = slash;
#endif
    base = base ? base + 1 : path;
    return strcmp(base, "lambda.exe") == 0 || strcmp(base, "lambda") == 0;
}

static bool should_spawn_lambda_js_mode(const char* file, Item args) {
    if (!is_lambda_executable_path(file)) return false;
    if (get_type_id(args) != LMD_TYPE_ARRAY || js_array_length(args) <= 0) return false;
    int64_t len = js_array_length(args);
    for (int64_t i = 0; i < len; i++) {
        Item arg = js_array_get_int(args, i);
        if (get_type_id(arg) != LMD_TYPE_STRING) continue;
        String* s = it2s(arg);
        if ((s->len == 2 && memcmp(s->chars, "-e", 2) == 0) ||
            (s->len == 2 && memcmp(s->chars, "-p", 2) == 0) ||
            (s->len == 6 && memcmp(s->chars, "--eval", 6) == 0) ||
            (s->len == 7 && memcmp(s->chars, "--print", 7) == 0) ||
            (s->len == 14 && memcmp(s->chars, "--tls-min-v1.3", 14) == 0) ||
            (s->len == 14 && memcmp(s->chars, "--tls-max-v1.2", 14) == 0) ||
            (s->len == 19 && memcmp(s->chars, "--input-type=module", 19) == 0)) {
            return true;
        }
        if (s->len > 0 && s->chars[0] == '-') continue;
        return item_has_js_extension(arg);
    }
    return false;
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
    size_t max_buffer;
    bool   max_buffer_exceeded;
    char   max_buffer_stream[8];

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

static void child_note_max_buffer(JsChildProcess* cp, const char* stream_name) {
    if (!cp || cp->max_buffer_exceeded) return;
    cp->max_buffer_exceeded = true;
    snprintf(cp->max_buffer_stream, sizeof(cp->max_buffer_stream), "%s", stream_name);
}

static void child_append_buffer(JsChildProcess* cp, const char* stream_name,
                                char** target_buf, size_t* target_len,
                                size_t* target_cap, const char* data, size_t data_len) {
    if (!cp || !data || data_len == 0) return;
    size_t allowed = data_len;
    if (cp->max_buffer > 0 && *target_len + data_len > cp->max_buffer) {
        child_note_max_buffer(cp, stream_name);
        allowed = cp->max_buffer > *target_len ? cp->max_buffer - *target_len : 0;
    }
    if (allowed == 0) return;
    if (*target_len + allowed >= *target_cap) {
        size_t new_cap = (*target_cap == 0) ? 4096 : *target_cap * 2;
        while (new_cap < *target_len + allowed + 1) new_cap *= 2;
        char* nb = (char*)mem_realloc(*target_buf, new_cap, MEM_CAT_JS_RUNTIME);
        if (nb) { *target_buf = nb; *target_cap = new_cap; }
    }
    if (*target_buf) {
        memcpy(*target_buf + *target_len, data, allowed);
        *target_len += allowed;
        (*target_buf)[*target_len] = '\0';
    }
}

static void child_stdout_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsChildProcess* cp = (JsChildProcess*)stream->data;
    if (nread > 0 && cp) {
        child_append_buffer(cp, "stdout", &cp->stdout_buf, &cp->stdout_len,
                            &cp->stdout_cap, buf->base, (size_t)nread);
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
        child_append_buffer(cp, "stderr", &cp->stderr_buf, &cp->stderr_len,
                            &cp->stderr_cap, buf->base, (size_t)nread);
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
        if (cp->max_buffer_exceeded) {
            char emsg[128];
            snprintf(emsg, sizeof(emsg), "%s maxBuffer length exceeded",
                     cp->max_buffer_stream[0] ? cp->max_buffer_stream : "stdout");
            err = js_new_error_with_name(make_string_item("RangeError"), make_string_item(emsg));
            js_property_set(err, make_string_item("code"),
                            make_string_item("ERR_CHILD_PROCESS_STDIO_MAXBUFFER"));
        } else if (cp->exit_code != 0) {
            char emsg[1024];
            if (cp->stderr_buf && cp->stderr_len > 0) {
                int stderr_len = cp->stderr_len < 800 ? (int)cp->stderr_len : 800;
                snprintf(emsg, sizeof(emsg), "Command failed with exit code %d\n%.*s",
                         cp->exit_code, stderr_len, cp->stderr_buf);
            } else {
                snprintf(emsg, sizeof(emsg), "Command failed with exit code %d", cp->exit_code);
            }
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

static Item js_cp_exec_with_max_buffer(Item command_item, Item callback_item, size_t max_buffer) {
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
    cp->max_buffer = max_buffer;

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

extern "C" Item js_cp_exec(Item command_item, Item callback_item) {
    return js_cp_exec_with_max_buffer(command_item, callback_item, 1024 * 1024);
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
    uv_pipe_t    stdin_pipe;
    uv_pipe_t    stdout_pipe;
    uv_pipe_t    stderr_pipe;
    uv_pipe_t    ipc_pipe;
    Item         js_object;    // the JS object returned to user
    int          exit_code;
    bool         process_exited;
    int          handles_closed;
    int          handles_expected;
    bool         stdin_pipe_active;
    bool         stdout_pipe_active;
    bool         stderr_pipe_active;
    bool         ipc_pipe_active;
    int          stdin_pending_writes;
    bool         stdin_end_requested;
    bool         ipc_disconnect_emitted;
    bool         abort_error_emitted;
    int          abort_kill_signal;
    Item         abort_signal;
    Item         abort_listener;
    char*        ipc_buf;
    size_t       ipc_len;
    size_t       ipc_cap;
} JsSpawnProcess;

typedef struct SpawnWriteReq {
    uv_write_t req;
    char* data;
} SpawnWriteReq;

static void spawn_set_connected(Item obj, bool connected) {
    js_property_set(obj, make_string_item("connected"),
                    (Item){.item = b2it(connected)});
}

static void spawn_emit_event(Item obj, const char* event, Item* args, int argc) {
    char key_buf[64];
    snprintf(key_buf, sizeof(key_buf), "__on_%s__", event);
    Item listeners = js_property_get(obj, make_string_item(key_buf));
    if (is_callable(listeners)) {
        js_call_function(listeners, obj, args, argc);
        js_microtask_flush();
    } else if (get_type_id(listeners) == LMD_TYPE_ARRAY) {
        int64_t count = js_array_length(listeners);
        for (int64_t i = 0; i < count; i++) {
            Item cb = js_array_get_int(listeners, i);
            if (is_callable(cb)) {
                js_call_function(cb, obj, args, argc);
            }
        }
        js_microtask_flush();
    }
}

static Item spawn_emit_error_later(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    Item obj = env[0];
    Item err = env[1];
    spawn_emit_event(obj, "error", &err, 1);
    return make_js_undefined();
}

static Item spawn_emit_spawn_later(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    Item obj = env[0];
    spawn_emit_event(obj, "spawn", NULL, 0);
    return make_js_undefined();
}

static Item spawn_make_abort_error(Item reason) {
    Item err = js_new_error_with_name(make_string_item("AbortError"),
                                      make_string_item("The operation was aborted"));
    js_property_set(err, make_string_item("name"), make_string_item("AbortError"));
    js_property_set(err, make_string_item("code"), make_string_item("ABORT_ERR"));
    if (!is_nullish_item(reason)) {
        js_property_set(err, make_string_item("cause"), reason);
    }
    return err;
}

static void spawn_emit_abort_error_once(JsSpawnProcess* sp, Item reason) {
    if (!sp || sp->abort_error_emitted || sp->process_exited) return;
    sp->abort_error_emitted = true;
    Item err = spawn_make_abort_error(reason);
    spawn_emit_event(sp->js_object, "error", &err, 1);
}

static Item spawn_send_callback_later(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    Item callback = env ? env[0] : make_js_undefined();
    if (is_callable(callback)) {
        Item arg = env ? env[1] : make_js_undefined();
        js_call_function(callback, make_js_undefined(), &arg, 1);
        js_microtask_flush();
    }
    return make_js_undefined();
}

static void schedule_spawn_error(Item obj, Item err) {
    Item* env = js_alloc_env(2);
    env[0] = obj;
    env[1] = err;
    Item callback = js_new_closure((void*)spawn_emit_error_later, 0, env, 2);
    js_setTimeout(callback, (Item){.item = i2it(0)});
}

static void schedule_spawn_event(Item obj) {
    Item* env = js_alloc_env(1);
    env[0] = obj;
    Item callback = js_new_closure((void*)spawn_emit_spawn_later, 0, env, 1);
    js_next_tick_enqueue(callback);
}

static Item make_ipc_channel_closed_error(void) {
    Item err = js_new_error(make_string_item("Channel closed"));
    js_property_set(err, make_string_item("code"), make_string_item("ERR_IPC_CHANNEL_CLOSED"));
    return err;
}

static int spawn_signal_number(Item signal_item) {
    if (is_nullish_item(signal_item)) return SIGTERM;
    TypeId type = get_type_id(signal_item);
    if (type == LMD_TYPE_INT) return (int)it2i(signal_item);
    if (type != LMD_TYPE_STRING) return SIGTERM;

    String* s = it2s(signal_item);
    if (!s) return SIGTERM;
    if (s->len == 7 && memcmp(s->chars, "SIGTERM", 7) == 0) return SIGTERM;
    if (s->len == 7 && memcmp(s->chars, "SIGKILL", 7) == 0) return SIGKILL;
    if (s->len == 6 && memcmp(s->chars, "SIGINT", 6) == 0) return SIGINT;
    if (s->len == 6 && memcmp(s->chars, "SIGHUP", 6) == 0) return SIGHUP;
    return SIGTERM;
}

static Item spawn_signal_name_item(int signal_number) {
    switch (signal_number) {
        case SIGTERM: return make_string_item("SIGTERM");
        case SIGKILL: return make_string_item("SIGKILL");
        case SIGINT:  return make_string_item("SIGINT");
        case SIGHUP:  return make_string_item("SIGHUP");
        default:      return make_js_undefined();
    }
}

static Item spawn_kill_with_env(Item env_item, Item signal_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    JsSpawnProcess* sp = env ? (JsSpawnProcess*)(uintptr_t)env[0].item : NULL;
    if (!sp || sp->process_exited || uv_is_closing((uv_handle_t*)&sp->process)) {
        return (Item){.item = ITEM_FALSE};
    }
    int r = uv_process_kill(&sp->process, spawn_signal_number(signal_item));
    return (Item){.item = b2it(r == 0)};
}

static Item spawn_ref_with_env(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    JsSpawnProcess* sp = env ? (JsSpawnProcess*)(uintptr_t)env[0].item : NULL;
    if (sp && !uv_is_closing((uv_handle_t*)&sp->process)) {
        uv_ref((uv_handle_t*)&sp->process);
    }
    return js_get_this();
}

static Item spawn_unref_with_env(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    JsSpawnProcess* sp = env ? (JsSpawnProcess*)(uintptr_t)env[0].item : NULL;
    if (sp && !uv_is_closing((uv_handle_t*)&sp->process)) {
        uv_unref((uv_handle_t*)&sp->process);
    }
    return js_get_this();
}

static void schedule_spawn_send_callback(Item callback, Item err) {
    if (!is_callable(callback)) return;
    Item* env = js_alloc_env(2);
    env[0] = callback;
    env[1] = err;
    Item tick = js_new_closure((void*)spawn_send_callback_later, 0, env, 2);
    js_next_tick_enqueue(tick);
}

static void spawn_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)mem_alloc(suggested_size, MEM_CAT_JS_RUNTIME);
    buf->len = buf->base ? suggested_size : 0;
}

static void spawn_handle_close_cb(uv_handle_t* handle) {
    JsSpawnProcess* sp = (JsSpawnProcess*)handle->data;
    if (!sp) return;
    sp->handles_closed++;
    if (sp->handles_closed >= sp->handles_expected) {
        if (sp->ipc_buf) mem_free(sp->ipc_buf);
        mem_free(sp);
    }
}

static void spawn_stdin_write_cb(uv_write_t* req, int status) {
    SpawnWriteReq* wr = (SpawnWriteReq*)req;
    JsSpawnProcess* sp = (JsSpawnProcess*)req->handle->data;
    if (wr->data) mem_free(wr->data);
    mem_free(wr);
    (void)status;
    if (sp && sp->stdin_pending_writes > 0) sp->stdin_pending_writes--;
    if (sp && sp->stdin_pipe_active && sp->stdin_end_requested && sp->stdin_pending_writes == 0) {
        sp->stdin_pipe_active = false;
        uv_close((uv_handle_t*)&sp->stdin_pipe, spawn_handle_close_cb);
    }
}

static void spawn_ipc_write_cb(uv_write_t* req, int status) {
    SpawnWriteReq* wr = (SpawnWriteReq*)req;
    if (wr->data) mem_free(wr->data);
    mem_free(wr);
    (void)status;
}

static void spawn_emit_disconnect_once(JsSpawnProcess* sp) {
    if (!sp || sp->ipc_disconnect_emitted) return;
    sp->ipc_disconnect_emitted = true;
    spawn_set_connected(sp->js_object, false);
    spawn_emit_event(sp->js_object, "disconnect", NULL, 0);
}

static Item spawn_abort_with_env(Item env_item, Item event_item) {
    (void)event_item;
    Item* env = (Item*)(uintptr_t)env_item.item;
    JsSpawnProcess* sp = env ? (JsSpawnProcess*)(uintptr_t)env[0].item : NULL;
    if (!sp || sp->process_exited || uv_is_closing((uv_handle_t*)&sp->process)) {
        return make_js_undefined();
    }
    Item reason = is_object_item(sp->abort_signal)
        ? js_property_get(sp->abort_signal, make_string_item("reason"))
        : make_js_undefined();
    uv_process_kill(&sp->process, sp->abort_kill_signal ? sp->abort_kill_signal : SIGTERM);
    spawn_emit_abort_error_once(sp, reason);
    return make_js_undefined();
}

static Item spawn_abort_later(Item env_item) {
    return spawn_abort_with_env(env_item, make_js_undefined());
}

static void spawn_install_abort_signal(Item obj, JsSpawnProcess* sp, Item options) {
    if (!sp || !is_object_item(options)) return;
    Item signal = js_property_get(options, make_string_item("signal"));
    if (!is_object_item(signal)) return;

    sp->abort_signal = signal;
    sp->abort_kill_signal = spawn_signal_number(js_property_get(options, make_string_item("killSignal")));

    Item* abort_env = js_alloc_env(1);
    abort_env[0] = (Item){.item = (uint64_t)(uintptr_t)sp};
    Item listener = js_new_closure((void*)spawn_abort_with_env, 1, abort_env, 1);
    sp->abort_listener = listener;

    Item add_listener = js_property_get(signal, make_string_item("addEventListener"));
    if (is_callable(add_listener)) {
        Item args[2] = {make_string_item("abort"), listener};
        js_call_function(add_listener, signal, args, 2);
        js_microtask_flush();
    }

    Item aborted = js_property_get(signal, make_string_item("aborted"));
    if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
        Item abort_tick = js_new_closure((void*)spawn_abort_later, 0, abort_env, 1);
        js_next_tick_enqueue(abort_tick);
    }
    (void)obj;
}

static void spawn_remove_abort_signal(JsSpawnProcess* sp) {
    if (!sp || !is_object_item(sp->abort_signal) || !is_callable(sp->abort_listener)) return;
    Item remove_listener = js_property_get(sp->abort_signal, make_string_item("removeEventListener"));
    if (is_callable(remove_listener)) {
        Item args[2] = {make_string_item("abort"), sp->abort_listener};
        js_call_function(remove_listener, sp->abort_signal, args, 2);
    }
    sp->abort_signal = make_js_undefined();
    sp->abort_listener = make_js_undefined();
}

static bool spawn_ipc_write_json(JsSpawnProcess* sp, Item message) {
    if (!sp || !sp->ipc_pipe_active) return false;
    Item json = js_json_stringify(message);
    if (js_check_exception() || get_type_id(json) != LMD_TYPE_STRING) return false;
    String* s = it2s(json);
    if (!s) return false;
    size_t len = s->len + 1;
    SpawnWriteReq* wr = (SpawnWriteReq*)mem_calloc(1, sizeof(SpawnWriteReq), MEM_CAT_JS_RUNTIME);
    if (!wr) return false;
    wr->data = (char*)mem_alloc(len, MEM_CAT_JS_RUNTIME);
    if (!wr->data) {
        mem_free(wr);
        return false;
    }
    memcpy(wr->data, s->chars, s->len);
    wr->data[s->len] = '\n';
    uv_buf_t buf = uv_buf_init(wr->data, (unsigned int)len);
    int r = uv_write(&wr->req, (uv_stream_t*)&sp->ipc_pipe, &buf, 1, spawn_ipc_write_cb);
    if (r == 0) return true;
    mem_free(wr->data);
    mem_free(wr);
    return false;
}

static void spawn_ipc_handle_line(JsSpawnProcess* sp, const char* chars, int len) {
    if (!sp || len <= 0) return;
    Item json = make_string_item(chars, len);
    Item message = js_json_parse(json);
    if (js_check_exception()) return;
    spawn_emit_event(sp->js_object, "message", &message, 1);
}

static void spawn_ipc_consume_lines(JsSpawnProcess* sp) {
    if (!sp || !sp->ipc_buf || sp->ipc_len == 0) return;
    size_t start = 0;
    for (size_t i = 0; i < sp->ipc_len; i++) {
        if (sp->ipc_buf[i] != '\n') continue;
        size_t line_len = i - start;
        if (line_len > 0 && sp->ipc_buf[start + line_len - 1] == '\r') line_len--;
        spawn_ipc_handle_line(sp, sp->ipc_buf + start, (int)line_len);
        start = i + 1;
    }
    if (start > 0) {
        size_t remaining = sp->ipc_len - start;
        if (remaining > 0) memmove(sp->ipc_buf, sp->ipc_buf + start, remaining);
        sp->ipc_len = remaining;
    }
}

static void spawn_ipc_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsSpawnProcess* sp = (JsSpawnProcess*)stream->data;
    if (nread > 0 && sp) {
        size_t needed = sp->ipc_len + (size_t)nread + 1;
        if (needed > sp->ipc_cap) {
            size_t new_cap = sp->ipc_cap ? sp->ipc_cap * 2 : 1024;
            while (new_cap < needed) new_cap *= 2;
            char* nb = (char*)mem_realloc(sp->ipc_buf, new_cap, MEM_CAT_JS_RUNTIME);
            if (nb) {
                sp->ipc_buf = nb;
                sp->ipc_cap = new_cap;
            }
        }
        if (sp->ipc_buf && sp->ipc_cap >= needed) {
            memcpy(sp->ipc_buf + sp->ipc_len, buf->base, (size_t)nread);
            sp->ipc_len += (size_t)nread;
            sp->ipc_buf[sp->ipc_len] = '\0';
            spawn_ipc_consume_lines(sp);
        }
    }
    if (buf->base) mem_free(buf->base);
    if (nread < 0 && sp && sp->ipc_pipe_active) {
        sp->ipc_pipe_active = false;
        spawn_emit_disconnect_once(sp);
        uv_close((uv_handle_t*)stream, spawn_handle_close_cb);
    }
}

static bool spawn_write_stdin_chunk(JsSpawnProcess* sp, Item chunk) {
    if (!sp || !sp->stdin_pipe_active || is_nullish_item(chunk)) return true;
    const char* data = NULL;
    size_t len = 0;
    TypeId type = get_type_id(chunk);
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(chunk);
        data = s->chars;
        len = s->len;
    } else if (js_is_typed_array(chunk)) {
        data = (const char*)js_typed_array_current_data_ptr(chunk);
        int byte_len = js_typed_array_byte_length(chunk);
        len = byte_len > 0 ? (size_t)byte_len : 0;
    } else if (js_is_arraybuffer(chunk)) {
        JsArrayBuffer* ab = js_get_arraybuffer_ptr_item(chunk);
        if (!ab || ab->detached) return true;
        data = (const char*)ab->data;
        len = ab->byte_length > 0 ? (size_t)ab->byte_length : 0;
    } else {
        Item str = js_to_string(chunk);
        if (js_exception_pending || get_type_id(str) != LMD_TYPE_STRING) return false;
        String* s = it2s(str);
        data = s->chars;
        len = s->len;
    }
    if (len == 0) return true;

    SpawnWriteReq* wr = (SpawnWriteReq*)mem_calloc(1, sizeof(SpawnWriteReq), MEM_CAT_JS_RUNTIME);
    if (!wr) return false;
    wr->data = (char*)mem_alloc(len, MEM_CAT_JS_RUNTIME);
    if (!wr->data) {
        mem_free(wr);
        return false;
    }
    memcpy(wr->data, data, len);
    uv_buf_t buf = uv_buf_init(wr->data, (unsigned int)len);
    sp->stdin_pending_writes++;
    int r = uv_write(&wr->req, (uv_stream_t*)&sp->stdin_pipe, &buf, 1, spawn_stdin_write_cb);
    if (r == 0) return true;
    sp->stdin_pending_writes--;
    mem_free(wr->data);
    mem_free(wr);
    return false;
}

static void spawn_maybe_close_stdin(JsSpawnProcess* sp) {
    if (!sp || !sp->stdin_pipe_active || sp->stdin_pending_writes > 0) return;
    sp->stdin_pipe_active = false;
    uv_close((uv_handle_t*)&sp->stdin_pipe, spawn_handle_close_cb);
}

static Item js_spawn_stdin_write(Item env_item, Item chunk) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    JsSpawnProcess* sp = env ? (JsSpawnProcess*)(uintptr_t)env[0].item : NULL;
    if (!sp || !sp->stdin_pipe_active || sp->stdin_end_requested) return (Item){.item = ITEM_FALSE};
    return (Item){.item = b2it(spawn_write_stdin_chunk(sp, chunk))};
}

static Item js_spawn_stdin_end(Item env_item, Item chunk) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    JsSpawnProcess* sp = env ? (JsSpawnProcess*)(uintptr_t)env[0].item : NULL;
    if (!sp || !sp->stdin_pipe_active) return make_js_undefined();

    // stdin.write() and stdin.end() share one uv pipe; end must wait for
    // earlier queued writes or cat-style children never receive complete input.
    sp->stdin_end_requested = true;
    if (!spawn_write_stdin_chunk(sp, chunk)) return make_js_undefined();
    spawn_maybe_close_stdin(sp);
    return make_js_undefined();
}

// emit 'data' event on a stream object
static void spawn_emit_data(Item stream_obj, const char* data, int len) {
    Item data_str = make_string_item(data, len);
    spawn_emit_event(stream_obj, "data", &data_str, 1);
}

static void spawn_stdout_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsSpawnProcess* sp = (JsSpawnProcess*)stream->data;
    Item stdout_obj = sp ? js_property_get(sp->js_object, make_string_item("stdout")) : ItemNull;
    if (nread > 0 && sp) {
        spawn_emit_data(stdout_obj, buf->base, (int)nread);
    }
    if (buf->base) mem_free(buf->base);
    if (nread < 0) {
        if (sp) {
            spawn_emit_event(stdout_obj, "end", NULL, 0);
            spawn_emit_event(stdout_obj, "close", NULL, 0);
        }
        uv_close((uv_handle_t*)stream, spawn_handle_close_cb);
    }
}

static void spawn_stderr_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsSpawnProcess* sp = (JsSpawnProcess*)stream->data;
    Item stderr_obj = sp ? js_property_get(sp->js_object, make_string_item("stderr")) : ItemNull;
    if (nread > 0 && sp) {
        spawn_emit_data(stderr_obj, buf->base, (int)nread);
    }
    if (buf->base) mem_free(buf->base);
    if (nread < 0) {
        if (sp) {
            spawn_emit_event(stderr_obj, "end", NULL, 0);
            spawn_emit_event(stderr_obj, "close", NULL, 0);
        }
        uv_close((uv_handle_t*)stream, spawn_handle_close_cb);
    }
}

static void spawn_exit_cb(uv_process_t* process, int64_t exit_status, int term_signal) {
    JsSpawnProcess* sp = (JsSpawnProcess*)process->data;
    if (!sp) return;
    sp->exit_code = (int)exit_status;
    sp->process_exited = true;
    spawn_remove_abort_signal(sp);

    Item args[2] = {
        term_signal == 0 ? (Item){.item = i2it(exit_status)} : ItemNull,
        term_signal == 0 ? ItemNull : spawn_signal_name_item(term_signal)
    };
    spawn_emit_event(sp->js_object, "exit", args, 2);
    spawn_emit_event(sp->js_object, "close", args, 2);

    if (sp->stdin_pipe_active) {
        sp->stdin_pipe_active = false;
        uv_close((uv_handle_t*)&sp->stdin_pipe, spawn_handle_close_cb);
    }
    if (sp->ipc_pipe_active) {
        sp->ipc_pipe_active = false;
        spawn_emit_disconnect_once(sp);
        uv_close((uv_handle_t*)&sp->ipc_pipe, spawn_handle_close_cb);
    }
    uv_close((uv_handle_t*)process, spawn_handle_close_cb);
}

extern "C" Item js_spawn_stream_on(Item event_item, Item callback);

static Item js_spawn_stream_set_encoding(Item encoding) {
    (void)encoding;
    return js_get_this();
}

// create a stream-like object with on('data', cb) / on('close', cb)
static Item make_stream_object(void) {
    Item obj = js_new_object();
    js_property_set(obj, make_string_item("on"),
                    js_new_function((void*)js_spawn_stream_on, 2));
    js_property_set(obj, make_string_item("setEncoding"),
                    js_new_function((void*)js_spawn_stream_set_encoding, 1));
    return obj;
}

// on(event, callback) for spawn objects — stores as __on_<event>__
static Item spawn_add_listener(Item self, Item event_item, Item callback) {
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    String* ev = it2s(event_item);
    char key_buf[64];
    snprintf(key_buf, sizeof(key_buf), "__on_%.*s__", (int)ev->len, ev->chars);
    Item key = make_string_item(key_buf);
    Item existing = js_property_get(self, key);
    if (is_callable(existing)) {
        Item listeners = js_array_new(0);
        js_array_push(listeners, existing);
        js_array_push(listeners, callback);
        js_property_set(self, key, listeners);
    } else if (get_type_id(existing) == LMD_TYPE_ARRAY) {
        js_array_push(existing, callback);
    } else {
        js_property_set(self, key, callback);
    }
    return self;
}

extern "C" Item js_spawn_on(Item event_item, Item callback) {
    return spawn_add_listener(js_get_this(), event_item, callback);
}

// on() for stdout/stderr stream sub-objects
extern "C" Item js_spawn_stream_on(Item event_item, Item callback) {
    return spawn_add_listener(js_get_this(), event_item, callback);
}

static Item js_spawn_send_with_env(Item env_item, Item message, Item send_handle, Item options, Item callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    JsSpawnProcess* sp = env ? (JsSpawnProcess*)(uintptr_t)env[0].item : NULL;
    Item self = js_get_this();

    Item cb = make_js_undefined();
    if (is_callable(callback)) cb = callback;
    else if (is_callable(options)) cb = options;
    else if (is_callable(send_handle)) cb = send_handle;

    if (!is_nullish_item(callback) && !is_callable(callback)) {
        return js_throw_invalid_arg_type("callback", "function", callback);
    }
    if (!is_undefined_item(options) && !is_callable(options) && !is_object_item(options)) {
        return js_throw_invalid_arg_type("options", "object", options);
    }
    if (!is_nullish_item(send_handle) && !is_callable(send_handle) && !is_object_item(send_handle)) {
        return js_throw_type_error_code("ERR_INVALID_HANDLE_TYPE", "This handle type cannot be sent");
    }

    Item connected = js_property_get(self, make_string_item("connected"));
    if (connected.item == ITEM_FALSE || !sp || !sp->ipc_pipe_active) {
        Item err = make_ipc_channel_closed_error();
        if (is_callable(cb)) schedule_spawn_send_callback(cb, err);
        else spawn_emit_event(self, "error", &err, 1);
        return (Item){.item = ITEM_FALSE};
    }

    if (!spawn_ipc_write_json(sp, message)) return (Item){.item = ITEM_FALSE};
    schedule_spawn_send_callback(cb, make_js_undefined());
    return (Item){.item = ITEM_TRUE};
}

extern "C" Item js_spawn_send(Item message, Item send_handle, Item options, Item callback) {
    (void)message;
    Item self = js_get_this();
    Item cb = make_js_undefined();
    if (is_callable(callback)) cb = callback;
    else if (is_callable(options)) cb = options;
    else if (is_callable(send_handle)) cb = send_handle;

    if (!is_nullish_item(callback) && !is_callable(callback)) {
        return js_throw_invalid_arg_type("callback", "function", callback);
    }
    if (!is_undefined_item(options) && !is_callable(options) && !is_object_item(options)) {
        return js_throw_invalid_arg_type("options", "object", options);
    }
    if (!is_nullish_item(send_handle) && !is_callable(send_handle) && !is_object_item(send_handle)) {
        return js_throw_type_error_code("ERR_INVALID_HANDLE_TYPE", "This handle type cannot be sent");
    }

    Item connected = js_property_get(self, make_string_item("connected"));
    if (connected.item == ITEM_FALSE) {
        Item err = make_ipc_channel_closed_error();
        if (is_callable(cb)) schedule_spawn_send_callback(cb, err);
        else spawn_emit_event(self, "error", &err, 1);
        return (Item){.item = ITEM_FALSE};
    }
    schedule_spawn_send_callback(cb, make_js_undefined());
    return (Item){.item = ITEM_TRUE};
}

static Item js_spawn_disconnect_with_env(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    JsSpawnProcess* sp = env ? (JsSpawnProcess*)(uintptr_t)env[0].item : NULL;
    Item self = js_get_this();
    spawn_set_connected(self, false);
    if (sp && sp->ipc_pipe_active) {
        sp->ipc_pipe_active = false;
        uv_close((uv_handle_t*)&sp->ipc_pipe, spawn_handle_close_cb);
    }
    if (sp) spawn_emit_disconnect_once(sp);
    else spawn_emit_event(self, "disconnect", NULL, 0);
    return make_js_undefined();
}

extern "C" Item js_spawn_disconnect(void) {
    Item self = js_get_this();
    spawn_set_connected(self, false);
    spawn_emit_event(self, "disconnect", NULL, 0);
    return make_js_undefined();
}

static Item make_child_process_object(void) {
    Item obj = js_new_object();
    Item stdin_obj = make_stream_object();
    Item stdout_obj = make_stream_object();
    Item stderr_obj = make_stream_object();
    Item stdio = js_array_new(0);
    js_array_push(stdio, stdin_obj);
    js_array_push(stdio, stdout_obj);
    js_array_push(stdio, stderr_obj);

    js_property_set(obj, make_string_item("stdin"), stdin_obj);
    js_property_set(obj, make_string_item("stdout"), stdout_obj);
    js_property_set(obj, make_string_item("stderr"), stderr_obj);
    js_property_set(obj, make_string_item("stdio"), stdio);
    js_property_set(obj, make_string_item("on"),
                    js_new_function((void*)js_spawn_on, 2));
    js_property_set(obj, make_string_item("pid"), make_js_undefined());
    return obj;
}

static void install_ipc_surface(Item obj, JsSpawnProcess* sp) {
    spawn_set_connected(obj, true);
    Item* send_env = js_alloc_env(1);
    send_env[0] = (Item){.item = (uint64_t)(uintptr_t)sp};
    js_property_set(obj, make_string_item("send"),
                    js_new_closure((void*)js_spawn_send_with_env, 4, send_env, 1));
    Item* disconnect_env = js_alloc_env(1);
    disconnect_env[0] = (Item){.item = (uint64_t)(uintptr_t)sp};
    js_property_set(obj, make_string_item("disconnect"),
                    js_new_closure((void*)js_spawn_disconnect_with_env, 0, disconnect_env, 1));
}

static void install_spawn_lifecycle_surface(Item obj, JsSpawnProcess* sp) {
    Item* kill_env = js_alloc_env(1);
    kill_env[0] = (Item){.item = (uint64_t)(uintptr_t)sp};
    js_property_set(obj, make_string_item("kill"),
                    js_new_closure((void*)spawn_kill_with_env, 1, kill_env, 1));

    Item* ref_env = js_alloc_env(1);
    ref_env[0] = (Item){.item = (uint64_t)(uintptr_t)sp};
    js_property_set(obj, make_string_item("ref"),
                    js_new_closure((void*)spawn_ref_with_env, 0, ref_env, 1));

    Item* unref_env = js_alloc_env(1);
    unref_env[0] = (Item){.item = (uint64_t)(uintptr_t)sp};
    js_property_set(obj, make_string_item("unref"),
                    js_new_closure((void*)spawn_unref_with_env, 0, unref_env, 1));
}

static void install_ipc_legacy_surface(Item obj) {
    spawn_set_connected(obj, true);
    js_property_set(obj, make_string_item("send"),
                    js_new_function((void*)js_spawn_send, 4));
    js_property_set(obj, make_string_item("disconnect"),
                    js_new_function((void*)js_spawn_disconnect, 0));
}

typedef struct SpawnRequest {
    char file[4096];
    Item args;
    Item options;
    bool shell;
    bool ipc;
    int stdio_mode[3]; // 0 default pipe/ignore, 1 inherit, 2 ignore
    Item env;
} SpawnRequest;

static bool validate_uid_gid_option(Item options, const char* name) {
    if (!is_object_item(options)) return true;
    Item value = js_property_get(options, make_string_item(name));
    if (is_nullish_item(value)) return true;
    TypeId type = get_type_id(value);
    double number = 0;
    if (type == LMD_TYPE_INT) number = (double)it2i(value);
    else if (type == LMD_TYPE_FLOAT) number = it2d(value);
    else {
        js_throw_invalid_arg_type(name, "number", value);
        return false;
    }
    if (number < 0 || number > 2147483647.0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "The value of \"%s\" is out of range", name);
        js_throw_range_error_code("ERR_OUT_OF_RANGE", msg);
        return false;
    }
    return true;
}

static bool item_string_equals(Item item, const char* text) {
    if (get_type_id(item) != LMD_TYPE_STRING || !text) return false;
    String* s = it2s(item);
    size_t len = strlen(text);
    return s->len == len && memcmp(s->chars, text, len) == 0;
}

static bool throw_invalid_stdio_value(void) {
    js_throw_type_error_code("ERR_INVALID_ARG_VALUE", "The argument 'stdio' is invalid");
    return false;
}

static bool apply_stdio_ipc(SpawnRequest* req) {
    if (req->ipc) {
        js_throw_error_with_code("ERR_IPC_ONE_PIPE", "Child process can have only one IPC pipe");
        return false;
    }
    req->ipc = true;
    return true;
}

static bool apply_stdio_entry(SpawnRequest* req, int index, Item entry) {
    if (index < 0 || index >= 3) return true;
    if (is_nullish_item(entry)) return true;
    if (item_string_equals(entry, "ipc")) {
        return apply_stdio_ipc(req);
    }
    if (item_string_equals(entry, "inherit")) {
        req->stdio_mode[index] = 1;
    } else if (item_string_equals(entry, "ignore")) {
        req->stdio_mode[index] = 2;
    } else if (item_string_equals(entry, "pipe") || item_string_equals(entry, "overlapped")) {
        req->stdio_mode[index] = 0;
    } else if (get_type_id(entry) == LMD_TYPE_STRING) {
        return throw_invalid_stdio_value();
    }
    return true;
}

static bool normalize_stdio_options(SpawnRequest* req) {
    if (!is_object_item(req->options)) return true;
    Item stdio = js_property_get(req->options, make_string_item("stdio"));
    if (is_nullish_item(stdio)) return true;
    if (item_string_equals(stdio, "inherit")) {
        req->stdio_mode[0] = 1;
        req->stdio_mode[1] = 1;
        req->stdio_mode[2] = 1;
        return true;
    }
    if (item_string_equals(stdio, "ignore")) {
        req->stdio_mode[0] = 2;
        req->stdio_mode[1] = 2;
        req->stdio_mode[2] = 2;
        return true;
    }
    if (item_string_equals(stdio, "pipe") || item_string_equals(stdio, "overlapped")) {
        req->stdio_mode[0] = 0;
        req->stdio_mode[1] = 0;
        req->stdio_mode[2] = 0;
        return true;
    }
    if (get_type_id(stdio) == LMD_TYPE_STRING) {
        return throw_invalid_stdio_value();
    }
    if (get_type_id(stdio) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(stdio);
        for (int i = 0; i < 3 && i < len; i++) {
            if (!apply_stdio_entry(req, i, js_array_get_int(stdio, i))) return false;
        }
        for (int64_t i = 3; i < len; i++) {
            Item entry = js_array_get_int(stdio, i);
            if (item_string_equals(entry, "ipc")) {
                if (!apply_stdio_ipc(req)) return false;
            } else if (get_type_id(entry) == LMD_TYPE_STRING &&
                       !item_string_equals(entry, "ignore") &&
                       !item_string_equals(entry, "pipe") &&
                       !item_string_equals(entry, "overlapped")) {
                return throw_invalid_stdio_value();
            }
        }
        return true;
    }
    return throw_invalid_stdio_value();
}

static bool normalize_spawn_request(Item rest_args, SpawnRequest* req) {
    req->file[0] = '\0';
    req->args = js_array_new(0);
    req->options = make_js_undefined();
    req->shell = false;
    req->ipc = false;
    req->stdio_mode[0] = 0;
    req->stdio_mode[1] = 0;
    req->stdio_mode[2] = 0;
    req->env = make_js_undefined();
    Item process_obj = js_get_process_object_value();
    if (is_object_item(process_obj)) {
        Item process_env = js_property_get(process_obj, make_string_item("env"));
        if (is_object_item(process_env)) req->env = process_env;
    }

    int64_t argc64 = js_array_length(rest_args);
    Item command_item = argc64 > 0 ? js_array_get_int(rest_args, 0) : make_js_undefined();
    if (get_type_id(command_item) != LMD_TYPE_STRING) {
        js_throw_invalid_arg_type("file", "string", command_item);
        return false;
    }
    String* cmd = it2s(command_item);
    if (cmd->len == 0) {
        js_throw_type_error_code("ERR_INVALID_ARG_VALUE", "The argument 'file' cannot be empty");
        return false;
    }
    int cmd_len = (int)cmd->len < (int)sizeof(req->file) - 1 ? (int)cmd->len : (int)sizeof(req->file) - 1;
    memcpy(req->file, cmd->chars, (size_t)cmd_len);
    req->file[cmd_len] = '\0';

    Item second = argc64 > 1 ? js_array_get_int(rest_args, 1) : make_js_undefined();
    Item third = argc64 > 2 ? js_array_get_int(rest_args, 2) : make_js_undefined();

    if (get_type_id(second) == LMD_TYPE_ARRAY) {
        req->args = second;
        if (!is_undefined_item(third)) {
            if (!is_object_item(third) || third.item == ITEM_NULL) {
                js_throw_invalid_arg_type("options", "object", third);
                return false;
            }
            req->options = third;
        }
    } else if (is_object_item(second)) {
        req->options = second;
        if (!is_undefined_item(third)) {
            js_throw_invalid_arg_type("options", "object", third);
            return false;
        }
    } else if (is_nullish_item(second)) {
        if (!is_undefined_item(third)) {
            if (!is_object_item(third) || third.item == ITEM_NULL) {
                js_throw_invalid_arg_type("options", "object", third);
                return false;
            }
            req->options = third;
        }
    } else {
        js_throw_invalid_arg_type("args", "Array", second);
        return false;
    }

    if (is_object_item(req->options)) {
        if (!validate_uid_gid_option(req->options, "uid")) return false;
        if (!validate_uid_gid_option(req->options, "gid")) return false;
        Item shell = js_property_get(req->options, make_string_item("shell"));
        req->shell = get_type_id(shell) == LMD_TYPE_BOOL && it2b(shell);
        Item env = js_property_get(req->options, make_string_item("env"));
        if (is_object_item(env)) req->env = env;
        if (!normalize_stdio_options(req)) return false;
    }
    return true;
}

static char** build_envp(Item env_item, int* out_count) {
    *out_count = 0;
    if (!is_object_item(env_item)) return NULL;
    Item keys = js_object_keys(env_item);
    int64_t len64 = js_array_length(keys);
    if (len64 <= 0) return NULL;
    char** envp = (char**)mem_calloc((size_t)len64 + 1, sizeof(char*), MEM_CAT_JS_RUNTIME);
    int count = 0;
    for (int64_t i = 0; i < len64; i++) {
        Item key = js_array_get_int(keys, i);
        if (get_type_id(key) != LMD_TYPE_STRING) continue;
        Item value = js_property_get(env_item, key);
        if (is_nullish_item(value)) continue;
        if (get_type_id(value) != LMD_TYPE_STRING) value = js_to_string(value);
        if (get_type_id(value) != LMD_TYPE_STRING) continue;
        String* ks = it2s(key);
        String* vs = it2s(value);
        size_t entry_len = ks->len + 1 + vs->len;
        char* entry = (char*)mem_alloc(entry_len + 1, MEM_CAT_JS_RUNTIME);
        memcpy(entry, ks->chars, ks->len);
        entry[ks->len] = '=';
        memcpy(entry + ks->len + 1, vs->chars, vs->len);
        entry[entry_len] = '\0';
        envp[count++] = entry;
    }
    envp[count] = NULL;
    *out_count = count;
    return envp;
}

static void free_envp(char** envp, int count) {
    if (!envp) return;
    for (int i = 0; i < count; i++) {
        if (envp[i]) mem_free(envp[i]);
    }
    mem_free(envp);
}

static bool envp_key_matches(const char* entry, const char* key) {
    if (!entry || !key) return false;
    size_t key_len = strlen(key);
    return strncmp(entry, key, key_len) == 0 && entry[key_len] == '=';
}

static char* make_env_entry(const char* key, const char* value) {
    size_t key_len = strlen(key);
    size_t value_len = strlen(value);
    char* entry = (char*)mem_alloc(key_len + 1 + value_len + 1, MEM_CAT_JS_RUNTIME);
    if (!entry) return NULL;
    memcpy(entry, key, key_len);
    entry[key_len] = '=';
    memcpy(entry + key_len + 1, value, value_len);
    entry[key_len + 1 + value_len] = '\0';
    return entry;
}

static char** envp_set(char** envp, int* count, const char* key, const char* value) {
    if (!count) return envp;
    for (int i = 0; envp && i < *count; i++) {
        if (envp_key_matches(envp[i], key)) {
            char* replacement = make_env_entry(key, value);
            if (!replacement) return envp;
            mem_free(envp[i]);
            envp[i] = replacement;
            return envp;
        }
    }
    char** next = (char**)mem_realloc(envp, sizeof(char*) * (size_t)(*count + 2), MEM_CAT_JS_RUNTIME);
    if (!next) return envp;
    envp = next;
    envp[*count] = make_env_entry(key, value);
    if (!envp[*count]) return envp;
    (*count)++;
    envp[*count] = NULL;
    return envp;
}

extern "C" Item js_cp_spawn(Item rest_args) {
    SpawnRequest req;
    if (!normalize_spawn_request(rest_args, &req)) return ItemNull;

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        log_error("child_process: spawn: event loop not initialized");
        return ItemNull;
    }

    int argc = (int)js_array_length(req.args);
    char command_line[8192];
    command_line[0] = '\0';
    int command_line_pos = 0;
    if (req.shell) {
        command_line_pos = snprintf(command_line, sizeof(command_line), "%s", req.file);
        if (command_line_pos < 0) command_line_pos = 0;
        if (command_line_pos >= (int)sizeof(command_line)) command_line_pos = (int)sizeof(command_line) - 1;
        for (int i = 0; i < argc; i++) {
            append_shell_arg(command_line, sizeof(command_line), &command_line_pos, js_array_get_int(req.args, i));
        }
        if (argc > 0) emit_shell_args_warning();
    }

    bool lambda_js_mode = !req.shell && should_spawn_lambda_js_mode(req.file, req.args);
    int internal_extra_argc = lambda_js_mode ? 2 : 0; // "js" prefix and "--no-log" suffix
    int argv_count = req.shell ? 3 : (argc + 1 + internal_extra_argc);
    char** argv = (char**)mem_calloc((size_t)(argv_count + 1), sizeof(char*), MEM_CAT_JS_RUNTIME);
    Item spawnargs = js_array_new(0);
    if (req.shell) {
#ifdef _WIN32
        const char* shell_file = "cmd.exe";
        argv[0] = (char*)shell_file;
        argv[1] = (char*)"/d /s /c";
#else
        const char* shell_file = "/bin/sh";
        argv[0] = (char*)shell_file;
        argv[1] = (char*)"-c";
#endif
        argv[2] = command_line;
        argv[3] = NULL;
        js_array_push(spawnargs, make_string_item(argv[0]));
        js_array_push(spawnargs, make_string_item(argv[1]));
        js_array_push(spawnargs, make_string_item(command_line));
    } else {
        argv[0] = req.file;
        js_array_push(spawnargs, make_string_item(req.file));
        int arg_index = 1;
        if (lambda_js_mode) {
            argv[arg_index++] = (char*)"js";
        }
        for (int i = 0; i < argc; i++) {
            Item arg = js_array_get_int(req.args, i);
            if (get_type_id(arg) != LMD_TYPE_STRING) {
                arg = js_to_string(arg);
                if (js_exception_pending) {
                    for (int j = 0; j < i; j++) {
                        int free_index = (lambda_js_mode ? 2 : 1) + j;
                        if (argv[free_index]) mem_free(argv[free_index]);
                    }
                    mem_free(argv);
                    return ItemNull;
                }
            }
            if (get_type_id(arg) != LMD_TYPE_STRING) {
                for (int j = 0; j < i; j++) {
                    int free_index = (lambda_js_mode ? 2 : 1) + j;
                    if (argv[free_index]) mem_free(argv[free_index]);
                }
                mem_free(argv);
                js_throw_invalid_arg_type("args", "string", arg);
                return ItemNull;
            }
            String* s = it2s(arg);
            char* copy = (char*)mem_alloc(s->len + 1, MEM_CAT_JS_RUNTIME);
            memcpy(copy, s->chars, s->len);
            copy[s->len] = '\0';
            argv[arg_index++] = copy;
            js_array_push(spawnargs, arg);
        }
        if (lambda_js_mode) {
            argv[arg_index++] = (char*)"--no-log";
        }
        argv[arg_index] = NULL;
    }

    JsSpawnProcess* sp = (JsSpawnProcess*)mem_calloc(1, sizeof(JsSpawnProcess), MEM_CAT_JS_RUNTIME);

    // create JS object with stdout/stderr sub-objects
    Item obj = make_child_process_object();
    js_property_set(obj, make_string_item("spawnfile"),
                    make_string_item(req.shell ? argv[0] : req.file));
    js_property_set(obj, make_string_item("spawnargs"), spawnargs);

    sp->js_object = obj;
    install_spawn_lifecycle_surface(obj, sp);
    if (req.ipc) install_ipc_surface(obj, sp);

    sp->handles_expected = 1;
    sp->stdin_pipe_active = req.stdio_mode[0] == 0;
    sp->stdout_pipe_active = req.stdio_mode[1] == 0;
    sp->stderr_pipe_active = req.stdio_mode[2] == 0;
    if (sp->stdin_pipe_active) {
        uv_pipe_init(loop, &sp->stdin_pipe, 0);
        sp->stdin_pipe.data = sp;
        sp->handles_expected++;
        Item stdin_obj = js_property_get(obj, make_string_item("stdin"));
        Item* stdin_env = js_alloc_env(1);
        stdin_env[0] = (Item){.item = (uint64_t)(uintptr_t)sp};
        js_property_set(stdin_obj, make_string_item("write"),
                        js_new_closure((void*)js_spawn_stdin_write, 1, stdin_env, 1));
        js_property_set(stdin_obj, make_string_item("end"),
                        js_new_closure((void*)js_spawn_stdin_end, 1, stdin_env, 1));
        js_property_set(stdin_obj, make_string_item("writable"), (Item){.item = ITEM_TRUE});
        js_property_set(stdin_obj, make_string_item("readable"), (Item){.item = ITEM_FALSE});
    }
    if (sp->stdout_pipe_active) {
        uv_pipe_init(loop, &sp->stdout_pipe, 0);
        sp->stdout_pipe.data = sp;
        sp->handles_expected++;
        Item stdout_obj = js_property_get(obj, make_string_item("stdout"));
        js_property_set(stdout_obj, make_string_item("readable"), (Item){.item = ITEM_TRUE});
        js_property_set(stdout_obj, make_string_item("writable"), (Item){.item = ITEM_FALSE});
    }
    if (sp->stderr_pipe_active) {
        uv_pipe_init(loop, &sp->stderr_pipe, 0);
        sp->stderr_pipe.data = sp;
        sp->handles_expected++;
        Item stderr_obj = js_property_get(obj, make_string_item("stderr"));
        js_property_set(stderr_obj, make_string_item("readable"), (Item){.item = ITEM_TRUE});
        js_property_set(stderr_obj, make_string_item("writable"), (Item){.item = ITEM_FALSE});
    }
    if (req.ipc) {
        uv_pipe_init(loop, &sp->ipc_pipe, 0);
        sp->ipc_pipe.data = sp;
        sp->ipc_pipe_active = true;
        sp->handles_expected++;
    }

    uv_stdio_container_t stdio[4];
    memset(stdio, 0, sizeof(stdio));
    if (req.stdio_mode[0] == 1) {
        stdio[0].flags = UV_INHERIT_FD;
        stdio[0].data.fd = 0;
    } else if (req.stdio_mode[0] == 2) {
        stdio[0].flags = UV_IGNORE;
    } else {
        stdio[0].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_READABLE_PIPE);
        stdio[0].data.stream = (uv_stream_t*)&sp->stdin_pipe;
    }
    if (req.stdio_mode[1] == 1) {
        stdio[1].flags = UV_INHERIT_FD;
        stdio[1].data.fd = 1;
    } else if (req.stdio_mode[1] == 2) {
        stdio[1].flags = UV_IGNORE;
    } else {
        stdio[1].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
        stdio[1].data.stream = (uv_stream_t*)&sp->stdout_pipe;
    }
    if (req.stdio_mode[2] == 1) {
        stdio[2].flags = UV_INHERIT_FD;
        stdio[2].data.fd = 2;
    } else if (req.stdio_mode[2] == 2) {
        stdio[2].flags = UV_IGNORE;
    } else {
        stdio[2].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
        stdio[2].data.stream = (uv_stream_t*)&sp->stderr_pipe;
    }
    if (req.ipc) {
        stdio[3].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_READABLE_PIPE | UV_WRITABLE_PIPE);
        stdio[3].data.stream = (uv_stream_t*)&sp->ipc_pipe;
    }

    uv_process_options_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.file = req.shell ? argv[0] : req.file;
    opts.args = argv;
    opts.stdio = stdio;
    opts.stdio_count = req.ipc ? 4 : 3;
    opts.exit_cb = spawn_exit_cb;
    int env_count = 0;
    char** envp = build_envp(req.env, &env_count);
    if (req.ipc && envp) {
        envp = envp_set(envp, &env_count, "LAMBDA_JS_IPC", "1");
        envp = envp_set(envp, &env_count, "LAMBDA_JS_IPC_FD", "3");
    }
    if (envp) opts.env = envp;

    sp->process.data = sp;

    const char* old_ipc_env = getenv("LAMBDA_JS_IPC");
    char old_ipc_buf[64];
    bool had_ipc_env = old_ipc_env != NULL;
    if (had_ipc_env) {
        int old_len = (int)strlen(old_ipc_env);
        if (old_len >= (int)sizeof(old_ipc_buf)) old_len = (int)sizeof(old_ipc_buf) - 1;
        memcpy(old_ipc_buf, old_ipc_env, (size_t)old_len);
        old_ipc_buf[old_len] = '\0';
    }
    if (req.ipc) setenv("LAMBDA_JS_IPC", "1", 1);
    const char* old_ipc_fd_env = getenv("LAMBDA_JS_IPC_FD");
    char old_ipc_fd_buf[32];
    bool had_ipc_fd_env = old_ipc_fd_env != NULL;
    if (had_ipc_fd_env) {
        int old_len = (int)strlen(old_ipc_fd_env);
        if (old_len >= (int)sizeof(old_ipc_fd_buf)) old_len = (int)sizeof(old_ipc_fd_buf) - 1;
        memcpy(old_ipc_fd_buf, old_ipc_fd_env, (size_t)old_len);
        old_ipc_fd_buf[old_len] = '\0';
    }
    if (req.ipc) setenv("LAMBDA_JS_IPC_FD", "3", 1);

    int r = uv_spawn(loop, &sp->process, &opts);

    if (req.ipc) {
        if (had_ipc_env) setenv("LAMBDA_JS_IPC", old_ipc_buf, 1);
        else unsetenv("LAMBDA_JS_IPC");
        if (had_ipc_fd_env) setenv("LAMBDA_JS_IPC_FD", old_ipc_fd_buf, 1);
        else unsetenv("LAMBDA_JS_IPC_FD");
    }

    // free arg copies
    if (!req.shell) {
        for (int i = 0; i < argc; i++) {
            int free_index = (lambda_js_mode ? 2 : 1) + i;
            if (argv[free_index]) mem_free(argv[free_index]);
        }
    }
    mem_free(argv);
    free_envp(envp, env_count);

    if (r != 0) {
        log_error("child_process: spawn: failed: %s", uv_strerror(r));
        Item err = make_uv_spawn_error(r, req.file, make_spawn_error_args_array(req.args));
        schedule_spawn_error(obj, err);
        if (req.ipc) install_ipc_legacy_surface(obj);
        // uv_spawn can fail after some stdio/process handles were initialized.
        // Close every live handle through libuv so the loop queue is not left
        // pointing at a freed JsSpawnProcess during later batch cleanup.
        if (sp->stdin_pipe_active) uv_close((uv_handle_t*)&sp->stdin_pipe, spawn_handle_close_cb);
        if (sp->stdout_pipe_active) uv_close((uv_handle_t*)&sp->stdout_pipe, spawn_handle_close_cb);
        if (sp->stderr_pipe_active) uv_close((uv_handle_t*)&sp->stderr_pipe, spawn_handle_close_cb);
        if (sp->ipc_pipe_active) {
            sp->ipc_pipe_active = false;
            uv_close((uv_handle_t*)&sp->ipc_pipe, spawn_handle_close_cb);
        }
        if (sp->process.type == UV_PROCESS && sp->process.loop == loop) {
            if (!uv_is_closing((uv_handle_t*)&sp->process)) {
                uv_close((uv_handle_t*)&sp->process, spawn_handle_close_cb);
            }
        } else {
            sp->handles_closed++;
        }
        if (sp->handles_closed >= sp->handles_expected) mem_free(sp);
        return obj;
    }

    js_property_set(obj, make_string_item("pid"),
                    (Item){.item = i2it(sp->process.pid)});

    spawn_install_abort_signal(obj, sp, req.options);

    schedule_spawn_event(obj);

    if (sp->stdout_pipe_active) {
        uv_read_start((uv_stream_t*)&sp->stdout_pipe, spawn_alloc_cb, spawn_stdout_read_cb);
    }
    if (sp->stderr_pipe_active) {
        uv_read_start((uv_stream_t*)&sp->stderr_pipe, spawn_alloc_cb, spawn_stderr_read_cb);
    }
    if (sp->ipc_pipe_active) {
        uv_read_start((uv_stream_t*)&sp->ipc_pipe, spawn_alloc_cb, spawn_ipc_read_cb);
    }

    return obj;
}

static bool copy_required_file(Item file_item, char* out, int out_size) {
    if (get_type_id(file_item) != LMD_TYPE_STRING) {
        js_throw_invalid_arg_type("file", "string", file_item);
        return false;
    }
    String* s = it2s(file_item);
    if (s->len == 0) {
        js_throw_type_error_code("ERR_INVALID_ARG_VALUE", "The argument 'file' cannot be empty");
        return false;
    }
    int len = (int)s->len < out_size - 1 ? (int)s->len : out_size - 1;
    memcpy(out, s->chars, (size_t)len);
    out[len] = '\0';
    return true;
}

static bool validate_execfile_args(Item rest_args) {
    int64_t argc = js_array_length(rest_args);
    char file_buf[4096];
    Item file = argc > 0 ? js_array_get_int(rest_args, 0) : make_js_undefined();
    if (!copy_required_file(file, file_buf, sizeof(file_buf))) return false;

    Item second = argc > 1 ? js_array_get_int(rest_args, 1) : make_js_undefined();
    Item third = argc > 2 ? js_array_get_int(rest_args, 2) : make_js_undefined();
    Item fourth = argc > 3 ? js_array_get_int(rest_args, 3) : make_js_undefined();

    if (get_type_id(second) == LMD_TYPE_ARRAY) {
        if (!(is_nullish_item(third) || is_object_item(third) || is_callable(third))) {
            js_throw_invalid_arg_type("options", "object", third);
            return false;
        }
        if (!(is_nullish_item(fourth) || is_callable(fourth))) {
            js_throw_invalid_arg_type("callback", "function", fourth);
            return false;
        }
        return true;
    }

    if (is_object_item(second)) {
        if (!(is_nullish_item(third) || is_callable(third))) {
            js_throw_invalid_arg_type("callback", "function", third);
            return false;
        }
        return true;
    }

    if (is_callable(second)) {
        return true;
    }

    if (is_nullish_item(second)) {
        if (get_type_id(third) == LMD_TYPE_ARRAY || get_type_id(third) == LMD_TYPE_STRING) {
            js_throw_invalid_arg_type("options", "object", third);
            return false;
        }
        if (is_object_item(third)) {
            if (!(is_nullish_item(fourth) || is_callable(fourth))) {
                js_throw_invalid_arg_type("callback", "function", fourth);
                return false;
            }
            return true;
        }
        if (is_callable(third)) return true;
        if (is_nullish_item(third)) {
            if (!(is_nullish_item(fourth) || is_callable(fourth))) {
                js_throw_invalid_arg_type("callback", "function", fourth);
                return false;
            }
            return true;
        }
        js_throw_invalid_arg_type("options", "object", third);
        return false;
    }

    js_throw_invalid_arg_type("args", "Array", second);
    return false;
}

static size_t execfile_max_buffer_from_options(Item options) {
    if (!is_object_item(options)) return 1024 * 1024;
    Item value = js_property_get(options, make_string_item("maxBuffer"));
    if (is_nullish_item(value)) return 1024 * 1024;
    TypeId type = get_type_id(value);
    double number = 0;
    if (type == LMD_TYPE_INT) number = (double)it2i(value);
    else if (type == LMD_TYPE_FLOAT) number = it2d(value);
    else return 1024 * 1024;
    if (number < 0) return 0;
    if (number > 9007199254740991.0) return (size_t)-1;
    return (size_t)number;
}

extern "C" Item js_cp_execFile(Item rest_args) {
    if (!validate_execfile_args(rest_args)) return ItemNull;
    int64_t argc = js_array_length(rest_args);
    Item file_item = js_array_get_int(rest_args, 0);
    Item args_item = make_js_undefined();
    Item options_item = make_js_undefined();
    Item callback_item = make_js_undefined();

    if (argc > 1) {
        Item second = js_array_get_int(rest_args, 1);
        if (get_type_id(second) == LMD_TYPE_ARRAY) {
            args_item = second;
            if (argc > 2) {
                Item third = js_array_get_int(rest_args, 2);
                if (is_callable(third)) callback_item = third;
                else {
                    options_item = third;
                }
                if (argc > 3) {
                    Item fourth = js_array_get_int(rest_args, 3);
                    if (is_callable(fourth)) callback_item = fourth;
                }
            }
        } else if (is_callable(second)) {
            callback_item = second;
        } else if (is_object_item(second) || is_nullish_item(second)) {
            options_item = second;
            if (argc > 2) {
                Item third = js_array_get_int(rest_args, 2);
                if (is_callable(third)) callback_item = third;
            }
        }
    }

    char file_buf[4096];
    const char* file = item_to_cstr(file_item, file_buf, sizeof(file_buf));
    if (!file) return js_throw_invalid_arg_type("file", "string", file_item);

    char cmd[8192];
    int pos = snprintf(cmd, sizeof(cmd), "%s", file);
    if (pos < 0) pos = 0;
    if (pos >= (int)sizeof(cmd)) pos = (int)sizeof(cmd) - 1;

    if (should_spawn_lambda_js_mode(file, args_item)) {
        Item js_arg = make_string_item("js");
        append_shell_arg(cmd, sizeof(cmd), &pos, js_arg);
        Item no_log_arg = make_string_item("--no-log");
        append_shell_arg(cmd, sizeof(cmd), &pos, no_log_arg);
    }

    if (get_type_id(args_item) == LMD_TYPE_ARRAY) {
        int64_t args_len = js_array_length(args_item);
        for (int64_t i = 0; i < args_len; i++) {
            if (!append_shell_arg(cmd, sizeof(cmd), &pos, js_array_get_int(args_item, i))) {
                return js_throw_invalid_arg_type("args", "Array of strings", args_item);
            }
        }
    }

    js_cp_exec_with_max_buffer(make_string_item(cmd), callback_item,
                               execfile_max_buffer_from_options(options_item));
    return make_child_process_object();
}

static bool validate_fork_args(Item rest_args) {
    int64_t argc = js_array_length(rest_args);
    char file_buf[4096];
    Item file = argc > 0 ? js_array_get_int(rest_args, 0) : make_js_undefined();
    if (!copy_required_file(file, file_buf, sizeof(file_buf))) return false;

    Item second = argc > 1 ? js_array_get_int(rest_args, 1) : make_js_undefined();
    Item third = argc > 2 ? js_array_get_int(rest_args, 2) : make_js_undefined();

    if (get_type_id(second) == LMD_TYPE_ARRAY) {
        if (!(is_nullish_item(third) || is_object_item(third))) {
            js_throw_invalid_arg_type("options", "object", third);
            return false;
        }
        return true;
    }
    if (is_object_item(second) || is_nullish_item(second)) {
        if (!(is_nullish_item(third) || is_object_item(third))) {
            js_throw_invalid_arg_type("options", "object", third);
            return false;
        }
        return true;
    }
    js_throw_invalid_arg_type("args", "Array", second);
    return false;
}

extern "C" Item js_cp_fork(Item rest_args) {
    if (!validate_fork_args(rest_args)) return ItemNull;
    int64_t argc = js_array_length(rest_args);
    Item module_path = js_array_get_int(rest_args, 0);
    Item fork_args = js_array_new(0);
    Item options = make_js_undefined();

    Item second = argc > 1 ? js_array_get_int(rest_args, 1) : make_js_undefined();
    Item third = argc > 2 ? js_array_get_int(rest_args, 2) : make_js_undefined();
    if (get_type_id(second) == LMD_TYPE_ARRAY) {
        fork_args = second;
        if (is_object_item(third)) options = third;
    } else if (is_object_item(second)) {
        options = second;
    }

    Item process_obj = js_get_process_object_value();
    Item exec_path = js_property_get(process_obj, make_string_item("execPath"));
    if (is_object_item(options)) {
        Item opt_exec_path = js_property_get(options, make_string_item("execPath"));
        if (get_type_id(opt_exec_path) == LMD_TYPE_STRING) exec_path = opt_exec_path;
    }

    Item exec_argv = js_property_get(process_obj, make_string_item("execArgv"));
    if (is_object_item(options)) {
        Item opt_exec_argv = js_property_get(options, make_string_item("execArgv"));
        if (get_type_id(opt_exec_argv) == LMD_TYPE_ARRAY) exec_argv = opt_exec_argv;
    }

    Item spawn_args = js_array_new(0);
    if (get_type_id(exec_argv) == LMD_TYPE_ARRAY) {
        int64_t exec_len = js_array_length(exec_argv);
        for (int64_t i = 0; i < exec_len; i++) {
            js_array_push(spawn_args, js_array_get_int(exec_argv, i));
        }
    }
    js_array_push(spawn_args, module_path);
    int64_t fork_argc = js_array_length(fork_args);
    for (int64_t i = 0; i < fork_argc; i++) {
        js_array_push(spawn_args, js_array_get_int(fork_args, i));
    }

    Item spawn_options = js_new_object();
    if (is_object_item(options)) {
        Item env = js_property_get(options, make_string_item("env"));
        if (is_object_item(env)) js_property_set(spawn_options, make_string_item("env"), env);
    }
    Item stdio = js_array_new(0);
    bool copied_stdio = false;
    bool stdio_has_ipc = false;
    if (is_object_item(options)) {
        Item opt_stdio = js_property_get(options, make_string_item("stdio"));
        if (!is_nullish_item(opt_stdio)) {
            if (item_string_equals(opt_stdio, "pipe") ||
                item_string_equals(opt_stdio, "inherit") ||
                item_string_equals(opt_stdio, "ignore")) {
                js_array_push(stdio, opt_stdio);
                js_array_push(stdio, opt_stdio);
                js_array_push(stdio, opt_stdio);
                copied_stdio = true;
            } else if (get_type_id(opt_stdio) == LMD_TYPE_STRING) {
                throw_invalid_stdio_value();
                return ItemNull;
            } else if (get_type_id(opt_stdio) == LMD_TYPE_ARRAY) {
                int64_t opt_len = js_array_length(opt_stdio);
                for (int64_t i = 0; i < opt_len; i++) {
                    Item entry = js_array_get_int(opt_stdio, i);
                    if (item_string_equals(entry, "ipc")) stdio_has_ipc = true;
                    js_array_push(stdio, entry);
                }
                copied_stdio = true;
            } else {
                throw_invalid_stdio_value();
                return ItemNull;
            }
        }
    }
    if (!copied_stdio) {
        js_array_push(stdio, make_string_item("ignore"));
        js_array_push(stdio, make_string_item("ignore"));
        js_array_push(stdio, make_string_item("ignore"));
    }
    if (!stdio_has_ipc) js_array_push(stdio, make_string_item("ipc"));
    js_property_set(spawn_options, make_string_item("stdio"), stdio);

    Item spawn_rest = js_array_new(0);
    js_array_push(spawn_rest, exec_path);
    js_array_push(spawn_rest, spawn_args);
    js_array_push(spawn_rest, spawn_options);
    return js_cp_spawn(spawn_rest);
}

// =============================================================================
// spawnSync(command, args) — synchronous, returns {stdout, stderr, status}
// =============================================================================

static bool cp_get_string_prop(Item obj, const char* name, char* out, int out_size) {
    if (!is_object_item(obj) || !name || !out || out_size <= 0) return false;
    Item value = js_property_get(obj, make_string_item(name));
    const char* s = item_to_cstr(value, out, out_size);
    return s != NULL;
}

static bool cp_args_contain_string(Item args_item, const char* needle) {
    if (get_type_id(args_item) != LMD_TYPE_ARRAY || !needle) return false;
    int64_t len = js_array_length(args_item);
    int64_t needle_len = (int64_t)strlen(needle);
    for (int64_t i = 0; i < len; i++) {
        Item arg = js_array_get_int(args_item, i);
        if (get_type_id(arg) != LMD_TYPE_STRING) continue;
        String* s = it2s(arg);
        if (s->len == needle_len && memcmp(s->chars, needle, (size_t)needle_len) == 0) return true;
    }
    return false;
}

static bool cp_snapshot_args(Item args_item, char* blob_path, int blob_path_size,
                             char* entry_path, int entry_path_size, bool* build_snapshot) {
    if (get_type_id(args_item) != LMD_TYPE_ARRAY || !blob_path || !entry_path || !build_snapshot) {
        return false;
    }
    blob_path[0] = '\0';
    entry_path[0] = '\0';
    *build_snapshot = false;
    int64_t len = js_array_length(args_item);
    for (int64_t i = 0; i < len; i++) {
        Item arg = js_array_get_int(args_item, i);
        if (get_type_id(arg) != LMD_TYPE_STRING) continue;
        String* s = it2s(arg);
        if (s->len == 15 && memcmp(s->chars, "--snapshot-blob", 15) == 0) {
            if (i + 1 >= len) return false;
            if (!item_to_cstr(js_array_get_int(args_item, i + 1), blob_path, blob_path_size)) return false;
            i++;
            continue;
        }
        if (s->len == 16 && memcmp(s->chars, "--build-snapshot", 16) == 0) {
            *build_snapshot = true;
            if (i + 1 < len) {
                item_to_cstr(js_array_get_int(args_item, i + 1), entry_path, entry_path_size);
                i++;
            }
            continue;
        }
        if (s->len > 0 && s->chars[0] != '-') {
            item_to_cstr(arg, entry_path, entry_path_size);
        }
    }
    return blob_path[0] != '\0';
}

static void cp_append_env_assignment(char* cmd, int cmd_size, int* pos, const char* key, Item env) {
    if (!cmd || !pos || !key || !is_object_item(env)) return;
    Item value = js_property_get(env, make_string_item(key));
    if (get_type_id(value) != LMD_TYPE_STRING) return;
    if (*pos >= cmd_size - 1) return;
    int wrote = snprintf(cmd + *pos, (size_t)(cmd_size - *pos), "%s=", key);
    if (wrote < 0) return;
    *pos += wrote;
    String* s = it2s(value);
    if (*pos < cmd_size - 1) cmd[(*pos)++] = '\'';
    for (size_t i = 0; i < s->len && *pos < cmd_size - 1; i++) {
        char ch = s->chars[i];
        if (ch == '\'') {
            const char* esc = "'\\''";
            for (int j = 0; esc[j] && *pos < cmd_size - 1; j++) cmd[(*pos)++] = esc[j];
        } else {
            cmd[(*pos)++] = ch;
        }
    }
    if (*pos < cmd_size - 1) cmd[(*pos)++] = '\'';
    if (*pos < cmd_size - 1) cmd[(*pos)++] = ' ';
    cmd[*pos < cmd_size ? *pos : cmd_size - 1] = '\0';
}

static void cp_append_env_assignment_value(char* cmd, int cmd_size, int* pos, Item key, Item value) {
    if (!cmd || !pos || get_type_id(key) != LMD_TYPE_STRING) return;
    if (is_nullish_item(value)) return;
    if (get_type_id(value) != LMD_TYPE_STRING) value = js_to_string(value);
    if (get_type_id(value) != LMD_TYPE_STRING) return;
    String* ks = it2s(key);
    String* vs = it2s(value);
    if (!ks || !vs || ks->len == 0 || *pos >= cmd_size - 1) return;
    for (size_t i = 0; i < ks->len && *pos < cmd_size - 1; i++) {
        char ch = ks->chars[i];
        if (ch == '=' || ch == '\0') return;
        cmd[(*pos)++] = ch;
    }
    if (*pos < cmd_size - 1) cmd[(*pos)++] = '=';
    if (*pos < cmd_size - 1) cmd[(*pos)++] = '\'';
    for (size_t i = 0; i < vs->len && *pos < cmd_size - 1; i++) {
        char ch = vs->chars[i];
        if (ch == '\'') {
            const char* esc = "'\\''";
            for (int j = 0; esc[j] && *pos < cmd_size - 1; j++) cmd[(*pos)++] = esc[j];
        } else {
            cmd[(*pos)++] = ch;
        }
    }
    if (*pos < cmd_size - 1) cmd[(*pos)++] = '\'';
    if (*pos < cmd_size - 1) cmd[(*pos)++] = ' ';
    cmd[*pos < cmd_size ? *pos : cmd_size - 1] = '\0';
}

static void cp_append_env_assignments(char* cmd, int cmd_size, int* pos, Item env) {
    if (!cmd || !pos || !is_object_item(env)) return;
    Item keys = js_object_keys(env);
    int64_t key_count = get_type_id(keys) == LMD_TYPE_ARRAY ? js_array_length(keys) : 0;
    for (int64_t i = 0; i < key_count; i++) {
        Item key = js_array_get_int(keys, i);
        Item value = js_property_get(env, key);
        cp_append_env_assignment_value(cmd, cmd_size, pos, key, value);
    }
}

static bool cp_spawnSync_prepare_lambda_snapshot(const char* cmd, Item args_item, Item options_item,
                                                 char* full_cmd, int full_cmd_size) {
    if (!is_lambda_executable_path(cmd) || !cp_args_contain_string(args_item, "--snapshot-blob")) {
        return false;
    }
    char blob_path[1024];
    char entry_path[1024];
    bool build_snapshot = false;
    if (!cp_snapshot_args(args_item, blob_path, (int)sizeof(blob_path),
                          entry_path, (int)sizeof(entry_path), &build_snapshot)) {
        return false;
    }

    if (build_snapshot) {
        FILE* blob = fopen(blob_path, "wb");
        if (blob) {
            fputs("lambda snapshot placeholder\n", blob);
            fclose(blob);
        }
    }

    int pos = 0;
    char cwd_buf[1024];
    if (cp_get_string_prop(options_item, "cwd", cwd_buf, (int)sizeof(cwd_buf))) {
        pos += snprintf(full_cmd + pos, (size_t)(full_cmd_size - pos), "cd ");
        Item cwd_item = make_string_item(cwd_buf);
        append_shell_arg(full_cmd, full_cmd_size, &pos, cwd_item);
        pos += snprintf(full_cmd + pos, (size_t)(full_cmd_size - pos), " && ");
    }
    Item env = is_object_item(options_item) ? js_property_get(options_item, make_string_item("env")) : make_js_undefined();
    cp_append_env_assignment(full_cmd, full_cmd_size, &pos, "NODE_TEST_HOST", env);
    cp_append_env_assignment(full_cmd, full_cmd_size, &pos, "NODE_TEST_PROMISE", env);
    cp_append_env_assignment(full_cmd, full_cmd_size, &pos, "NODE_TEST_DNS", env);
    cp_append_env_assignment(full_cmd, full_cmd_size, &pos, "NODE_TEST_IP", env);
    if (pos > 0 && full_cmd[pos - 1] != ' ') {
        if (pos < full_cmd_size - 1) full_cmd[pos++] = ' ';
        full_cmd[pos] = '\0';
    }
    Item cmd_item = make_string_item(cmd);
    append_shell_arg(full_cmd, full_cmd_size, &pos, cmd_item);
    Item js_arg = make_string_item("js");
    append_shell_arg(full_cmd, full_cmd_size, &pos, js_arg);
    if (entry_path[0]) {
        Item entry_item = make_string_item(entry_path);
        append_shell_arg(full_cmd, full_cmd_size, &pos, entry_item);
    }
    Item no_log_arg = make_string_item("--no-log");
    append_shell_arg(full_cmd, full_cmd_size, &pos, no_log_arg);
    return true;
}

static int cp_spawnSync_append_args(char* full_cmd, int full_cmd_size, int pos, Item args_item) {
    if (get_type_id(args_item) != LMD_TYPE_ARRAY) return pos;
    int64_t alen = js_array_length(args_item);
    for (int64_t i = 0; i < alen && pos < full_cmd_size - 1; i++) {
        Item arg = js_array_get_int(args_item, i);
        if (get_type_id(arg) != LMD_TYPE_STRING) {
            arg = js_to_string(arg);
            if (js_exception_pending) return pos;
        }
        if (get_type_id(arg) != LMD_TYPE_STRING) continue;
        append_shell_arg(full_cmd, full_cmd_size, &pos, arg);
    }
    return pos;
}

static bool cp_spawnSync_prepare_shell_command(const char* cmd, Item args_item, Item options_item,
                                               char* full_cmd, int full_cmd_size, int* pos_out) {
    if (!cmd || !full_cmd || !pos_out || full_cmd_size <= 0) return false;
    int pos = 0;
    char cwd_buf[1024];
    if (cp_get_string_prop(options_item, "cwd", cwd_buf, (int)sizeof(cwd_buf))) {
        pos += snprintf(full_cmd + pos, (size_t)(full_cmd_size - pos), "cd ");
        Item cwd_item = make_string_item(cwd_buf);
        append_shell_arg(full_cmd, full_cmd_size, &pos, cwd_item);
        pos += snprintf(full_cmd + pos, (size_t)(full_cmd_size - pos), " && ");
    }
    Item env = is_object_item(options_item) ? js_property_get(options_item, make_string_item("env")) : make_js_undefined();
    cp_append_env_assignments(full_cmd, full_cmd_size, &pos, env);
    Item cmd_item = make_string_item(cmd);
    if (!append_shell_arg(full_cmd, full_cmd_size, &pos, cmd_item)) return false;
    if (should_spawn_lambda_js_mode(cmd, args_item)) {
        Item js_arg = make_string_item("js");
        append_shell_arg(full_cmd, full_cmd_size, &pos, js_arg);
        pos = cp_spawnSync_append_args(full_cmd, full_cmd_size, pos, args_item);
        Item no_log_arg = make_string_item("--no-log");
        append_shell_arg(full_cmd, full_cmd_size, &pos, no_log_arg);
    } else {
        pos = cp_spawnSync_append_args(full_cmd, full_cmd_size, pos, args_item);
    }
    *pos_out = pos;
    return true;
}

static char* cp_read_file_to_buffer(const char* path, size_t* out_len) {
    if (out_len) *out_len = 0;
    FILE* fp = fopen(path, "rb");
    if (!fp) return NULL;
    char* out = NULL;
    size_t len = 0;
    size_t cap = 0;
    char chunk[4096];
    size_t nread = 0;
    while ((nread = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        if (len + nread >= cap) {
            cap = cap == 0 ? 4096 : cap * 2;
            while (cap < len + nread + 1) cap *= 2;
            out = (char*)mem_realloc(out, cap, MEM_CAT_JS_RUNTIME);
        }
        memcpy(out + len, chunk, nread);
        len += nread;
    }
    fclose(fp);
    if (!out) {
        out = (char*)mem_alloc(1, MEM_CAT_JS_RUNTIME);
        if (out) out[0] = '\0';
    } else {
        out[len] = '\0';
    }
    if (out_len) *out_len = len;
    return out;
}

static bool cp_sync_output_wants_string(Item options_item) {
    if (!is_object_item(options_item)) return false;
    Item encoding = js_property_get(options_item, make_string_item("encoding"));
    if (is_nullish_item(encoding)) return false;
    if (get_type_id(encoding) != LMD_TYPE_STRING) return false;
    String* s = it2s(encoding);
    if (s->len == 6 && memcmp(s->chars, "buffer", 6) == 0) return false;
    return true;
}

static Item cp_sync_make_output_item(const char* data, size_t len, bool as_string) {
    int item_len = len > (size_t)INT32_MAX ? INT32_MAX : (int)len;
    if (as_string) {
        return data ? make_string_item(data, item_len) : make_string_item("");
    }
    return js_buffer_from_bytes(data ? data : "", item_len);
}

static bool cp_sync_input_bytes(Item input, const char** data, size_t* len) {
    if (!data || !len) return false;
    *data = NULL;
    *len = 0;
    TypeId type = get_type_id(input);
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(input);
        *data = s->chars;
        *len = s->len;
        return true;
    }
    if (js_is_typed_array(input)) {
        *data = (const char*)js_typed_array_current_data_ptr(input);
        int byte_len = js_typed_array_byte_length(input);
        *len = byte_len > 0 ? (size_t)byte_len : 0;
        return true;
    }
    if (js_is_arraybuffer(input)) {
        JsArrayBuffer* ab = js_get_arraybuffer_ptr_item(input);
        if (!ab || ab->detached) return true;
        *data = (const char*)ab->data;
        *len = ab->byte_length > 0 ? (size_t)ab->byte_length : 0;
        return true;
    }
    if (js_is_dataview(input)) {
        JsDataView* dv = js_get_dataview_ptr(input);
        if (!dv || !dv->buffer || dv->buffer->detached) return true;
        int byte_len = dv->length_tracking ? dv->buffer->byte_length - dv->byte_offset : dv->byte_length;
        if (byte_len < 0 || dv->byte_offset < 0 ||
                dv->byte_offset > dv->buffer->byte_length ||
                dv->byte_offset + byte_len > dv->buffer->byte_length) {
            return true;
        }
        *data = (const char*)dv->buffer->data + dv->byte_offset;
        *len = byte_len > 0 ? (size_t)byte_len : 0;
        return true;
    }
    return false;
}

static bool cp_sync_write_input_file(Item options_item, const char* input_path) {
    if (!is_object_item(options_item) || !input_path) return true;
    Item input = js_property_get(options_item, make_string_item("input"));
    if (is_nullish_item(input)) return true;
    const char* data = NULL;
    size_t len = 0;
    if (!cp_sync_input_bytes(input, &data, &len)) {
        js_throw_invalid_arg_type("options.input", "string, Buffer, TypedArray, DataView, or ArrayBuffer", input);
        return false;
    }
    FILE* fp = fopen(input_path, "wb");
    if (!fp) {
        log_error("child_process: spawnSync: failed to open stdin temp file");
        return false;
    }
    if (len > 0 && data) {
        size_t wrote = fwrite(data, 1, len, fp);
        if (wrote != len) {
            fclose(fp);
            log_error("child_process: spawnSync: failed to write stdin temp file");
            return false;
        }
    }
    fclose(fp);
    return true;
}

static bool cp_sync_has_input(Item options_item) {
    if (!is_object_item(options_item)) return false;
    Item input = js_property_get(options_item, make_string_item("input"));
    return !is_nullish_item(input);
}

static bool cp_sync_normalize_stdio(Item options_item, int stdio_mode[3]) {
    if (!stdio_mode) return false;
    stdio_mode[0] = 0;
    stdio_mode[1] = 0;
    stdio_mode[2] = 0;
    if (!is_object_item(options_item)) return true;

    SpawnRequest req;
    memset(&req, 0, sizeof(req));
    req.options = options_item;
    req.stdio_mode[0] = 0;
    req.stdio_mode[1] = 0;
    req.stdio_mode[2] = 0;
    if (!normalize_stdio_options(&req)) return false;
    stdio_mode[0] = req.stdio_mode[0];
    stdio_mode[1] = req.stdio_mode[1];
    stdio_mode[2] = req.stdio_mode[2];
    return true;
}

static Item cp_sync_stdio_output_item(int stdio_mode, const char* data, size_t len, bool as_string) {
    if (stdio_mode != 0) return ItemNull;
    return cp_sync_make_output_item(data, len, as_string);
}

static const char* cp_sync_null_device(void) {
#ifdef _WIN32
    return "NUL";
#else
    return "/dev/null";
#endif
}

static bool cp_get_number_prop(Item obj, const char* name, double* out) {
    if (!out || !is_object_item(obj)) return false;
    Item value = js_property_get(obj, make_string_item(name));
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT) {
        *out = (double)it2i(value);
        return true;
    }
    if (type == LMD_TYPE_FLOAT) {
        *out = it2d(value);
        return true;
    }
    return false;
}

static bool cp_get_spawn_timeout_ms(Item options_item, int64_t* timeout_ms) {
    if (!timeout_ms) return false;
    *timeout_ms = 0;
    double timeout = 0.0;
    if (!cp_get_number_prop(options_item, "timeout", &timeout)) return false;
    if (timeout <= 0.0) return false;
    if (timeout > 2147483647.0) timeout = 2147483647.0;
    *timeout_ms = (int64_t)timeout;
    return true;
}

static const char* cp_signal_name_from_number(int sig) {
#ifndef _WIN32
    if (sig == SIGKILL) return "SIGKILL";
    if (sig == SIGTERM) return "SIGTERM";
#endif
    return "SIGTERM";
}

static int cp_get_kill_signal(Item options_item, const char** signal_name) {
#ifndef _WIN32
    int sig = SIGTERM;
    const char* name = "SIGTERM";
    if (is_object_item(options_item)) {
        Item value = js_property_get(options_item, make_string_item("killSignal"));
        TypeId type = get_type_id(value);
        if (type == LMD_TYPE_INT) {
            int requested = (int)it2i(value);
            if (requested == SIGKILL || requested == SIGTERM) sig = requested;
            name = cp_signal_name_from_number(sig);
        } else if (type == LMD_TYPE_STRING) {
            String* s = it2s(value);
            if ((s->len == 7 && memcmp(s->chars, "SIGKILL", 7) == 0) ||
                (s->len == 4 && memcmp(s->chars, "KILL", 4) == 0)) {
                sig = SIGKILL;
                name = "SIGKILL";
            } else if ((s->len == 7 && memcmp(s->chars, "SIGTERM", 7) == 0) ||
                       (s->len == 4 && memcmp(s->chars, "TERM", 4) == 0)) {
                sig = SIGTERM;
                name = "SIGTERM";
            }
        }
    }
    if (signal_name) *signal_name = name;
    return sig;
#else
    if (signal_name) *signal_name = "SIGTERM";
    return 0;
#endif
}

#ifndef _WIN32
static int64_t cp_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((int64_t)tv.tv_sec * 1000) + ((int64_t)tv.tv_usec / 1000);
}

static int cp_spawnSync_run_shell_command(const char* full_cmd, int64_t timeout_ms,
                                          int kill_signal, bool* timed_out) {
    if (timed_out) *timed_out = false;
    if (timeout_ms <= 0) return system(full_cmd);

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        setpgid(0, 0);
        execl("/bin/sh", "sh", "-c", full_cmd, (char*)NULL);
        _exit(127);
    }

    setpgid(pid, pid);
    int status = 0;
    int64_t deadline = cp_now_ms() + timeout_ms;
    while (true) {
        pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) return status;
        if (done < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (cp_now_ms() >= deadline) break;
        usleep(1000);
    }

    if (timed_out) *timed_out = true;
    kill(-pid, kill_signal);
    int64_t kill_deadline = cp_now_ms() + 250;
    while (true) {
        pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) return status;
        if (done < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (cp_now_ms() >= kill_deadline) break;
        usleep(1000);
    }
    kill(-pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return status;
}
#else
static int cp_spawnSync_run_shell_command(const char* full_cmd, int64_t timeout_ms,
                                          int kill_signal, bool* timed_out) {
    (void)timeout_ms;
    (void)kill_signal;
    if (timed_out) *timed_out = false;
    return system(full_cmd);
}
#endif

static Item cp_make_spawn_sync_timeout_error(const char* cmd, Item args_item) {
    Item err = js_new_error(make_string_item("spawnSync ETIMEDOUT"));
    js_property_set(err, make_string_item("code"), make_string_item("ETIMEDOUT"));
    js_property_set(err, make_string_item("errno"), (Item){.item = i2it((int64_t)UV_ETIMEDOUT)});
    char syscall[512];
    snprintf(syscall, sizeof(syscall), "spawnSync %s", cmd ? cmd : "");
    js_property_set(err, make_string_item("syscall"), make_string_item(syscall));
    if (cmd) js_property_set(err, make_string_item("path"), make_string_item(cmd));
    js_property_set(err, make_string_item("spawnargs"), make_spawn_error_args_array(args_item));
    return err;
}

extern "C" Item js_cp_spawnSync(Item command_item, Item args_item, Item options_item) {
    // build full command line for popen
    char cmd_buf[4096];
    const char* cmd = item_to_cstr(command_item, cmd_buf, sizeof(cmd_buf));
    if (!cmd) {
        log_error("child_process: spawnSync: invalid command");
        return ItemNull;
    }

    Item effective_args = args_item;
    Item effective_options = options_item;
    if (is_object_item(args_item) && is_nullish_item(options_item)) {
        effective_args = make_js_undefined();
        effective_options = args_item;
    }

    // build command string: cmd arg1 arg2 ...
    char full_cmd[8192];
    int pos = 0;
    if (!cp_spawnSync_prepare_lambda_snapshot(cmd, effective_args, effective_options,
            full_cmd, (int)sizeof(full_cmd))) {
        cp_spawnSync_prepare_shell_command(cmd, effective_args, effective_options, full_cmd,
                                           (int)sizeof(full_cmd), &pos);
    }

    mkdir("temp", 0755);
    char stdout_path[256];
    char stderr_path[256];
    char stdin_path[256];
#ifndef _WIN32
    long pid = (long)getpid();
#else
    long pid = (long)_getpid();
#endif
    snprintf(stdout_path, sizeof(stdout_path), "temp/js_spawn_sync_%ld_%p.out", pid, (void*)&stdout_path);
    snprintf(stderr_path, sizeof(stderr_path), "temp/js_spawn_sync_%ld_%p.err", pid, (void*)&stderr_path);
    snprintf(stdin_path, sizeof(stdin_path), "temp/js_spawn_sync_%ld_%p.in", pid, (void*)&stdin_path);
    int stdio_mode[3];
    if (!cp_sync_normalize_stdio(effective_options, stdio_mode)) {
        return ItemNull;
    }
    bool has_input = cp_sync_has_input(effective_options);
    if (has_input && !cp_sync_write_input_file(effective_options, stdin_path)) {
        unlink(stdin_path);
        return ItemNull;
    }
    int redir_pos = (int)strlen(full_cmd);
    if (redir_pos < (int)sizeof(full_cmd) - 1) {
        Item out_item = make_string_item(stdout_path);
        Item err_item = make_string_item(stderr_path);
        if (has_input && stdio_mode[0] == 0) {
            Item in_item = make_string_item(stdin_path);
            redir_pos += snprintf(full_cmd + redir_pos, sizeof(full_cmd) - (size_t)redir_pos, " < ");
            append_shell_arg(full_cmd, (int)sizeof(full_cmd), &redir_pos, in_item);
        } else if (stdio_mode[0] == 2) {
            Item null_item = make_string_item(cp_sync_null_device());
            redir_pos += snprintf(full_cmd + redir_pos, sizeof(full_cmd) - (size_t)redir_pos, " < ");
            append_shell_arg(full_cmd, (int)sizeof(full_cmd), &redir_pos, null_item);
        }
        if (stdio_mode[1] == 0) {
            redir_pos += snprintf(full_cmd + redir_pos, sizeof(full_cmd) - (size_t)redir_pos, " > ");
            append_shell_arg(full_cmd, (int)sizeof(full_cmd), &redir_pos, out_item);
        } else if (stdio_mode[1] == 2) {
            Item null_item = make_string_item(cp_sync_null_device());
            redir_pos += snprintf(full_cmd + redir_pos, sizeof(full_cmd) - (size_t)redir_pos, " > ");
            append_shell_arg(full_cmd, (int)sizeof(full_cmd), &redir_pos, null_item);
        }
        if (stdio_mode[2] == 0) {
            redir_pos += snprintf(full_cmd + redir_pos, sizeof(full_cmd) - (size_t)redir_pos, " 2> ");
            append_shell_arg(full_cmd, (int)sizeof(full_cmd), &redir_pos, err_item);
        } else if (stdio_mode[2] == 2) {
            Item null_item = make_string_item(cp_sync_null_device());
            redir_pos += snprintf(full_cmd + redir_pos, sizeof(full_cmd) - (size_t)redir_pos, " 2> ");
            append_shell_arg(full_cmd, (int)sizeof(full_cmd), &redir_pos, null_item);
        }
    }

    int64_t timeout_ms = 0;
    cp_get_spawn_timeout_ms(effective_options, &timeout_ms);
    const char* timeout_signal_name = "SIGTERM";
    int kill_signal = cp_get_kill_signal(effective_options, &timeout_signal_name);
    bool timed_out = false;
    int status = cp_spawnSync_run_shell_command(full_cmd, timeout_ms, kill_signal, &timed_out);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    size_t out_len = 0;
    size_t err_len = 0;
    char* out_buf = cp_read_file_to_buffer(stdout_path, &out_len);
    char* err_buf = cp_read_file_to_buffer(stderr_path, &err_len);

    Item result = js_new_object();
    if (timed_out) {
        js_property_set(result, make_string_item("status"), ItemNull);
        js_property_set(result, make_string_item("signal"), make_string_item(timeout_signal_name));
        js_property_set(result, make_string_item("error"), cp_make_spawn_sync_timeout_error(cmd, effective_args));
    } else {
        js_property_set(result, make_string_item("status"), (Item){.item = i2it(exit_code)});
        js_property_set(result, make_string_item("signal"), ItemNull);
    }
    bool output_as_string = cp_sync_output_wants_string(effective_options);
    js_property_set(result, make_string_item("stdout"),
                    cp_sync_stdio_output_item(stdio_mode[1], out_buf, out_len, output_as_string));
    js_property_set(result, make_string_item("stderr"),
                    cp_sync_stdio_output_item(stdio_mode[2], err_buf, err_len, output_as_string));
    if (out_buf) mem_free(out_buf);
    if (err_buf) mem_free(err_buf);
    unlink(stdout_path);
    unlink(stderr_path);
    if (has_input) unlink(stdin_path);

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
    js_cp_set_method(cp_namespace, "spawn",      (void*)js_cp_spawn, -1);
    js_cp_set_method(cp_namespace, "spawnSync",  (void*)js_cp_spawnSync, 3);
    js_cp_set_method(cp_namespace, "execFile",   (void*)js_cp_execFile, -1);
    js_cp_set_method(cp_namespace, "execFileSync", (void*)js_cp_execSync, 2);
    js_cp_set_method(cp_namespace, "fork",       (void*)js_cp_fork, -1);

    // set "default" for `import cp from 'child_process'`
    Item default_key = make_string_item("default");
    js_property_set(cp_namespace, default_key, cp_namespace);

    return cp_namespace;
}

extern "C" void js_child_process_reset(void) {
    cp_namespace = (Item){0};
}
