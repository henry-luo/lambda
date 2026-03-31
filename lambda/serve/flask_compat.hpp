#pragma once

//
// flask_compat.hpp — Flask/WSGI compatibility bridge
//
// Runs synchronous Python WSGI applications (Flask, Django) under
// lambda/serve by spawning persistent Python worker processes.
// Each worker runs a simple WSGI-to-JSON adapter script via stdin/stdout.
//
// For async Python apps (FastAPI, Starlette), use asgi_bridge.hpp instead.
//

#include "server.hpp"
#include <uv.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WSGI_MAX_WORKERS 16

// ── worker ──

typedef struct WsgiWorker {
    uv_process_t    process;
    uv_pipe_t       stdin_pipe;
    uv_pipe_t       stdout_pipe;
    char*           read_buf;       // accumulated output
    int             read_len;
    int             read_cap;
    int             busy;
    int             alive;
    int             request_id;
} WsgiWorker;

// ── bridge ──

typedef struct WsgiBridge {
    Server*         server;
    const char*     python_app;     // "module:attribute" string
    const char*     python_path;    // path to python3 executable
    const char*     bridge_script;  // path to wsgi_bridge.py
    int             worker_count;
    WsgiWorker      workers[WSGI_MAX_WORKERS];
    int             next_request_id;
    uv_loop_t*      loop;
} WsgiBridge;

// Create a WSGI bridge for a Python app.
//   python_app: "module:attribute" (e.g. "app:app")
//   python_path: path to python3 (NULL for "python3")
WsgiBridge* wsgi_bridge_create(Server* server, const char* python_app, const char* python_path);

// Start WSGI worker processes.
int wsgi_bridge_start(WsgiBridge* bridge);

// Mount the WSGI bridge on the server at mount_path (e.g. "/" or "/api").
// All requests under mount_path are forwarded to Python.
void wsgi_bridge_mount(WsgiBridge* bridge, const char* mount_path);

// Forward a request to an available WSGI worker.
void wsgi_bridge_dispatch(WsgiBridge* bridge, HttpRequest* req, HttpResponse* resp);

// Destroy the bridge and all workers.
void wsgi_bridge_destroy(WsgiBridge* bridge);

#ifdef __cplusplus
}
#endif
