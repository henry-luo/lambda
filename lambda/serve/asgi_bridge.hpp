/**
 * @file asgi_bridge.hpp
 * @brief ASGI bridge — routes HTTP requests to Python ASGI apps via JSON IPC
 *
 * Communication uses newline-delimited JSON (NDJSON) over pipes or Unix domain
 * sockets. Binary request/response bodies are base64-encoded.
 */

#ifndef LAMBDA_SERVE_ASGI_BRIDGE_HPP
#define LAMBDA_SERVE_ASGI_BRIDGE_HPP

#include "server.hpp"
#include "../../lib/arraylist.h"
#include <uv.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// ASGI Worker
// ============================================================================

typedef enum AsgiTransport {
    ASGI_TRANSPORT_PIPE,    // stdin/stdout pipes (default)
    ASGI_TRANSPORT_UDS      // Unix domain socket
} AsgiTransport;

typedef struct AsgiWorker {
    uv_process_t    process;
    uv_pipe_t       stdin_pipe;     // C++ → Python (write)
    uv_pipe_t       stdout_pipe;    // Python → C++ (read)
    int             busy;           // 1 if currently handling a request
    int             alive;          // 1 if process is running
    uint64_t        request_id;     // current request being handled
    char           *read_buf;       // accumulator for partial reads
    size_t          read_buf_len;
    size_t          read_buf_cap;
} AsgiWorker;

// ============================================================================
// ASGI Bridge
// ============================================================================

typedef struct AsgiBridge {
    Server         *server;
    const char     *python_app;     // module:app (e.g., "myapp:app")
    const char     *python_path;    // path to python3 executable (NULL = auto)
    const char     *bridge_script;  // path to asgi_bridge.py
    AsgiTransport   transport;
    const char     *uds_path;       // UDS socket path (for ASGI_TRANSPORT_UDS)
    int             worker_count;
    AsgiWorker    **workers;
    uint64_t        next_request_id;
    uv_loop_t      *loop;
} AsgiBridge;

// ============================================================================
// API
// ============================================================================

/**
 * Create an ASGI bridge. Does not start workers yet.
 *   python_app    — "module:app" import string
 *   worker_count  — number of persistent Python workers (default: 4)
 *   transport     — ASGI_TRANSPORT_PIPE or ASGI_TRANSPORT_UDS
 */
AsgiBridge* asgi_bridge_create(Server *server, const char *python_app,
                                int worker_count, AsgiTransport transport);

/**
 * Start all ASGI workers (spawns Python subprocesses).
 * Returns 0 on success.
 */
int asgi_bridge_start(AsgiBridge *bridge);

/**
 * Register a catch-all route that forwards requests to the ASGI app.
 *   prefix — URL prefix to handle (e.g., "/" for all requests)
 */
int asgi_bridge_mount(AsgiBridge *bridge, const char *prefix);

/**
 * Send a request to an available ASGI worker.
 * This is called internally by the catch-all handler.
 */
int asgi_bridge_dispatch(AsgiBridge *bridge, HttpRequest *req, HttpResponse *resp);

/**
 * Stop all workers and clean up.
 */
void asgi_bridge_destroy(AsgiBridge *bridge);

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_SERVE_ASGI_BRIDGE_HPP
