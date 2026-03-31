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

#include <cstdlib>
#include <cstring>
#include <cstdio>

// ── allocation ──

static void alloc_buffer(uv_handle_t* handle, size_t suggested, uv_buf_t* buf) {
    (void)handle;
    buf->base = (char*)malloc(suggested);
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
        free(buf->base);
        w->alive = 0;
        return;
    }
    if (nread == 0) {
        free(buf->base);
        return;
    }

    // ensure capacity
    if (w->read_len + (int)nread >= w->read_cap) {
        int new_cap = w->read_cap * 2;
        if (new_cap < w->read_len + (int)nread + 1) new_cap = w->read_len + (int)nread + 1;
        char* nb = (char*)realloc(w->read_buf, new_cap);
        if (!nb) { free(buf->base); return; }
        w->read_buf = nb;
        w->read_cap = new_cap;
    }

    memcpy(w->read_buf + w->read_len, buf->base, nread);
    w->read_len += (int)nread;
    free(buf->base);

    // look for complete newline-delimited message
    char* nl = (char*)memchr(w->read_buf, '\n', w->read_len);
    if (!nl) return;

    int line_len = (int)(nl - w->read_buf);

    // the response object is stashed in the process user data
    // this is set before dispatch
    HttpResponse* resp = (HttpResponse*)w->stdin_pipe.data;
    if (resp) {
        ipc_parse_response(w->read_buf, line_len, resp);
        w->stdin_pipe.data = nullptr;
    }

    int remaining = w->read_len - line_len - 1;
    if (remaining > 0) {
        memmove(w->read_buf, nl + 1, remaining);
    }
    w->read_len = remaining;
    w->busy = 0;
}

static int start_worker(WsgiBridge* bridge, int index) {
    WsgiWorker* w = &bridge->workers[index];
    memset(w, 0, sizeof(WsgiWorker));

    w->read_buf = (char*)malloc(4096);
    w->read_cap = 4096;
    w->read_len = 0;

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
    WsgiBridge* bridge = (WsgiBridge*)calloc(1, sizeof(WsgiBridge));
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
    uv_write_t* wr = (uv_write_t*)malloc(sizeof(uv_write_t));
    wr->data = msg;

    uv_write(wr, (uv_stream_t*)&w->stdin_pipe, &buf, 1,
             [](uv_write_t* req, int status) {
                 free(req->data);  // free msg
                 if (status < 0) log_error("WSGI: write failed: %s", uv_strerror(status));
                 free(req);
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
        free(w->read_buf);
    }

    free(bridge);
}
