/**
 * js_child_process.cpp — Node.js-style 'child_process' module for LambdaJS v15
 *
 * Provides exec() and execSync() backed by libuv's uv_spawn.
 * Registered as built-in module 'child_process' via js_module_get().
 */
#include "js_runtime.h"
#include "js_event_loop.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"

#include <cstring>
#include <cstdlib>

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
    buf->base = (char*)malloc(suggested_size);
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
        if (cp->stdout_buf) free(cp->stdout_buf);
        if (cp->stderr_buf) free(cp->stderr_buf);
        free(cp);
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
            char* nb = (char*)realloc(cp->stdout_buf, new_cap);
            if (nb) { cp->stdout_buf = nb; cp->stdout_cap = new_cap; }
        }
        if (cp->stdout_buf) {
            memcpy(cp->stdout_buf + cp->stdout_len, buf->base, nread);
            cp->stdout_len += nread;
            cp->stdout_buf[cp->stdout_len] = '\0';
        }
    }
    if (buf->base) free(buf->base);
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
            char* nb = (char*)realloc(cp->stderr_buf, new_cap);
            if (nb) { cp->stderr_buf = nb; cp->stderr_cap = new_cap; }
        }
        if (cp->stderr_buf) {
            memcpy(cp->stderr_buf + cp->stderr_len, buf->base, nread);
            cp->stderr_len += nread;
            cp->stderr_buf[cp->stderr_len] = '\0';
        }
    }
    if (buf->base) free(buf->base);
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
        log_error("child_process: exec: invalid command");
        return ItemNull;
    }

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        log_error("child_process: exec: event loop not initialized");
        return ItemNull;
    }

    JsChildProcess* cp = (JsChildProcess*)calloc(1, sizeof(JsChildProcess));
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
    opts.file = "/bin/sh";
    char* args[] = {(char*)"/bin/sh", (char*)"-c", (char*)cmd, NULL};
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
// execSync(command) — synchronous, returns stdout string
// =============================================================================

extern "C" Item js_cp_execSync(Item command_item) {
    char cmd_buf[4096];
    const char* cmd = item_to_cstr(command_item, cmd_buf, sizeof(cmd_buf));
    if (!cmd) {
        log_error("child_process: execSync: invalid command");
        return ItemNull;
    }

    // Use popen for simplicity — synchronous and blocking
    FILE* fp = popen(cmd, "r");
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
            char* nb = (char*)realloc(result_buf, new_cap);
            if (!nb) break;
            result_buf = nb;
            result_cap = new_cap;
        }
        memcpy(result_buf + result_len, chunk, chunk_len);
        result_len += chunk_len;
    }

    int status = pclose(fp);
    (void)status; // ignore exit code for execSync

    if (result_buf) {
        result_buf[result_len] = '\0';
        // trim trailing newline
        while (result_len > 0 && (result_buf[result_len - 1] == '\n' || result_buf[result_len - 1] == '\r')) {
            result_len--;
        }
        Item result = make_string_item(result_buf, (int)result_len);
        free(result_buf);
        return result;
    }

    return make_string_item("");
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

    js_cp_set_method(cp_namespace, "exec",     (void*)js_cp_exec, 2);
    js_cp_set_method(cp_namespace, "execSync", (void*)js_cp_execSync, 1);

    // set "default" for `import cp from 'child_process'`
    Item default_key = make_string_item("default");
    js_property_set(cp_namespace, default_key, cp_namespace);

    return cp_namespace;
}

extern "C" void js_child_process_reset(void) {
    cp_namespace = (Item){0};
}
