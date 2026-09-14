//
// flask_compat.cpp — Flask/WSGI bridge implementation
//
// Spawns persistent Python workers running wsgi_bridge.py.
// Communication uses the same JSON-over-stdin/stdout protocol as asgi_bridge,
// but the Python side uses a synchronous WSGI adapter.
//

#include "flask_compat.hpp"
#include "ipc_proto.hpp"
#include "../../lib/log.h"

#include "../../lib/mem.h"
#include <cstring>
#include <cstdio>

// ── allocation ──

static void alloc_buffer(uv_handle_t* handle, size_t suggested, uv_buf_t* buf) {
    (void)handle;
    buf->base = (char*)mem_alloc(suggested, MEM_CAT_SERVE);
    buf->len = buf->base ? (int)suggested : 0;
}

// ── worker lifecycle ──

static void on_worker_exit(uv_process_t* proc, int64_t exit_status, int term_signal) {
    WsgiWorker* w = (WsgiWorker*)proc->data;
    w->alive = 0;
    log_info("WSGI worker exited: status=%lld signal=%d", (long long)exit_status, term_signal);
}

static void on_worker_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    WsgiWorker* w = (WsgiWorker*)stream->data;

    if (nread < 0) {
        mem_free(buf->base);
        w->alive = 0;
        return;
    }
    if (nread == 0) {
        mem_free(buf->base);
        return;
    }

    if (!line_framer_append(&w->read_lines, buf->base, (size_t)nread)) {
        log_error("WSGI: failed to append worker response bytes");
        mem_free(buf->base);
        return;
    }
    mem_free(buf->base);

    // look for complete newline-delimited message
    size_t line_len = 0;
    const char* line = line_framer_peek(&w->read_lines, &line_len);
    if (!line) return;

    // the response object is stashed in the process user data
    // this is set before dispatch
    HttpResponse* resp = (HttpResponse*)w->stdin_pipe.data;
    if (resp) {
        ipc_parse_response(line, (int)line_len, resp);
        w->stdin_pipe.data = nullptr;
    }

    (void)line_framer_consume(&w->read_lines, line_len + 1);
    w->busy = 0;
}

static int start_worker(WsgiBridge* bridge, int index) {
    WsgiWorker* w = &bridge->workers[index];
    memset(w, 0, sizeof(WsgiWorker));

    if (!line_framer_init(&w->read_lines, 4096, MEM_CAT_SERVE)) {
        log_error("WSGI: failed to allocate worker line framer");
        return -1;
    }

    uv_pipe_init(bridge->loop, &w->stdin_pipe, 0);
    uv_pipe_init(bridge->loop, &w->stdout_pipe, 0);
    w->stdout_pipe.data = w;

    uv_stdio_container_t stdio[3];
    stdio[0].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_READABLE_PIPE);
    stdio[0].data.stream = (uv_stream_t*)&w->stdin_pipe;
    stdio[1].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
    stdio[1].data.stream = (uv_stream_t*)&w->stdout_pipe;
    stdio[2].flags = UV_INHERIT_FD;
    stdio[2].data.fd = 2; // stderr

    const char* python = bridge->python_path ? bridge->python_path : "python3";
    char* args[] = {
        (char*)python,
        (char*)bridge->bridge_script,
        (char*)"--app",
        (char*)bridge->python_app,
        nullptr
    };

    uv_process_options_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.exit_cb = on_worker_exit;
    opts.file = python;
    opts.args = args;
    opts.stdio = stdio;
    opts.stdio_count = 3;

    w->process.data = w;
    int r = uv_spawn(bridge->loop, &w->process, &opts);
    if (r < 0) {
        log_error("WSGI: failed to spawn worker %d: %s", index, uv_strerror(r));
        return r;
    }

    w->alive = 1;
    uv_read_start((uv_stream_t*)&w->stdout_pipe, alloc_buffer, on_worker_read);
    log_info("WSGI: started worker %d (pid=%d)", index, w->process.pid);
    return 0;
}

// ── public API ──

WsgiBridge* wsgi_bridge_create(Server* server, const char* python_app, const char* python_path) {
    WsgiBridge* bridge = (WsgiBridge*)mem_calloc(1, sizeof(WsgiBridge), MEM_CAT_SERVE);
    if (!bridge) return nullptr;

    bridge->server = server;
    bridge->python_app = python_app;
    bridge->python_path = python_path;
    bridge->bridge_script = "lambda/serve/wsgi_bridge.py";
    bridge->worker_count = 4;
    bridge->loop = server->loop;

    return bridge;
}

int wsgi_bridge_start(WsgiBridge* bridge) {
    for (int i = 0; i < bridge->worker_count; i++) {
        if (start_worker(bridge, i) < 0) {
            return -1;
        }
    }
    return 0;
}

static WsgiWorker* find_available_worker(WsgiBridge* bridge) {
    for (int i = 0; i < bridge->worker_count; i++) {
        if (bridge->workers[i].alive && !bridge->workers[i].busy) {
            return &bridge->workers[i];
        }
    }
    return nullptr;
}

void wsgi_bridge_dispatch(WsgiBridge* bridge, HttpRequest* req, HttpResponse* resp) {
    WsgiWorker* w = find_available_worker(bridge);
    if (!w) {
        http_response_error(resp, 503, "No WSGI workers available");
        return;
    }

    w->busy = 1;
    w->request_id = bridge->next_request_id++;

    // stash response for the read callback
    w->stdin_pipe.data = resp;

    char* msg = ipc_build_request(req, w->request_id);
    if (!msg) {
        http_response_error(resp, 500, "Failed to serialize request");
        w->busy = 0;
        return;
    }

    int msg_len = (int)strlen(msg);
    uv_buf_t buf = uv_buf_init(msg, msg_len);
    uv_write_t* wr = (uv_write_t*)mem_alloc(sizeof(uv_write_t), MEM_CAT_SERVE);
    wr->data = msg;

    uv_write(wr, (uv_stream_t*)&w->stdin_pipe, &buf, 1,
             [](uv_write_t* req, int status) {
                 mem_free(req->data);  // free msg
                 if (status < 0) log_error("WSGI: write failed: %s", uv_strerror(status));
                 mem_free(req);
             });
}

static void wsgi_catch_all_handler(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WsgiBridge* bridge = (WsgiBridge*)user_data;
    wsgi_bridge_dispatch(bridge, req, resp);
}

void wsgi_bridge_mount(WsgiBridge* bridge, const char* mount_path) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "%s*path", mount_path);
    server_all(bridge->server, pattern, wsgi_catch_all_handler, bridge);
}

void wsgi_bridge_destroy(WsgiBridge* bridge) {
    if (!bridge) return;

    for (int i = 0; i < bridge->worker_count; i++) {
        WsgiWorker* w = &bridge->workers[i];
        if (w->alive) {
            uv_process_kill(&w->process, SIGTERM);
        }
        line_framer_destroy(&w->read_lines);
    }

    mem_free(bridge);
}
