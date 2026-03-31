/**
 * @file server.hpp
 * @brief HTTP/HTTPS server with libuv event loop
 *
 * Main server struct that orchestrates all components: router, middleware,
 * worker pool, TLS, and language backends. Manages connection lifecycle
 * with keep-alive, timeouts, and graceful shutdown.
 *
 * Compatible with:
 *   Express:   const app = express(); app.listen(3000)
 *   Flask:     app.run(host='0.0.0.0', port=5000)
 *   FastAPI:   uvicorn.run(app, host='0.0.0.0', port=8000)
 *   PHP-FPM:   php-fpm -y /etc/php-fpm.conf
 *
 * Usage:
 *   Server *srv = server_create(NULL);
 *   server_get(srv, "/", handler, NULL);
 *   server_use(srv, middleware_logger(), NULL);
 *   server_start(srv, 3000);
 *   server_run(srv);                    // blocks until shutdown
 *   server_destroy(srv);
 */

#pragma once

#include <uv.h>
#include "serve_types.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "router.hpp"
#include "middleware.hpp"
#include "worker_pool.hpp"
#include "tls_handler.hpp"

// ============================================================================
// Connection State
// ============================================================================

struct ClientConnection {
    uv_tcp_t            handle;         // libuv TCP handle (must be first)
    struct Server      *server;         // owning server
    TlsConnection      *tls;           // TLS connection state (NULL for HTTP)
    int                 is_tls;         // 1 for HTTPS connection
    uv_timer_t          timeout_timer;  // idle timeout timer

    // HTTP parser state
    char               *read_buf;       // accumulated read buffer
    size_t              read_len;
    size_t              read_cap;

    // keep-alive tracking
    int                 request_count;
    int                 keep_alive;

    // linked list for connection tracking
    ClientConnection   *prev;
    ClientConnection   *next;
};

// ============================================================================
// Server
// ============================================================================

struct Server {
    ServerConfig        config;
    uv_loop_t          *loop;
    int                 owns_loop;      // 1 if we created the loop

    // listeners
    uv_tcp_t            http_handle;    // HTTP listener
    uv_tcp_t            https_handle;   // HTTPS listener
    int                 http_listening;
    int                 https_listening;

    // components
    Router             *router;
    MiddlewareStack    *middleware;
    WorkerPool         *worker_pool;
    TlsContext         *tls_ctx;
    BackendRegistry    *backends;

    // connection tracking
    ClientConnection   *connections;    // doubly-linked list head
    int                 connection_count;

    // signal handling for graceful shutdown
    uv_signal_t         sigint_handle;
    uv_signal_t         sigterm_handle;
    int                 shutting_down;

    // user data
    void               *app_data;
};

// ============================================================================
// Server Lifecycle
// ============================================================================

// create server with optional config (NULL for defaults)
Server* server_create(const ServerConfig *config);

// create server with an existing libuv loop
Server* server_create_with_loop(const ServerConfig *config, uv_loop_t *loop);

// destroy server and all components
void server_destroy(Server *server);

// start listening on HTTP (and optionally HTTPS) port
int server_start(Server *server, int port);

// start HTTPS listener on a separate port
int server_start_tls(Server *server, int ssl_port,
                     const char *cert_file, const char *key_file);

// run the event loop (blocks until server is stopped)
int server_run(Server *server);

// initiate graceful shutdown
void server_stop(Server *server);

// ============================================================================
// Route Registration (Express-style convenience)
// ============================================================================

int server_route(Server *server, HttpMethod method, const char *pattern,
                 RequestHandler handler, void *user_data);

int server_get(Server *server, const char *pattern, RequestHandler handler, void *data);
int server_post(Server *server, const char *pattern, RequestHandler handler, void *data);
int server_put(Server *server, const char *pattern, RequestHandler handler, void *data);
int server_del(Server *server, const char *pattern, RequestHandler handler, void *data);
int server_patch(Server *server, const char *pattern, RequestHandler handler, void *data);
int server_options(Server *server, const char *pattern, RequestHandler handler, void *data);
int server_all(Server *server, const char *pattern, RequestHandler handler, void *data);

// ============================================================================
// Middleware Registration
// ============================================================================

// add global middleware
int server_use(Server *server, MiddlewareFn fn, void *user_data);

// add path-scoped middleware
int server_use_path(Server *server, const char *path, MiddlewareFn fn, void *user_data);

// ============================================================================
// Sub-router Mounting
// ============================================================================

// mount a sub-router under a prefix
int server_mount(Server *server, Router *sub_router);

// ============================================================================
// Static File Serving
// ============================================================================

// serve static files from a directory
int server_set_static(Server *server, const char *url_path, const char *dir_path);

// ============================================================================
// Configuration
// ============================================================================

// set worker pool size
void server_set_pool_size(Server *server, int pool_size);

// set the backend registry
void server_set_backends(Server *server, BackendRegistry *backends);

// set application-level user data
void server_set_app_data(Server *server, void *data);
void* server_get_app_data(Server *server);

// ============================================================================
// Connection Management (internal but exposed for testing)
// ============================================================================

ClientConnection* server_connection_add(Server *server, uv_tcp_t *client, int is_tls);
void              server_connection_remove(Server *server, ClientConnection *conn);
void              server_connection_close(ClientConnection *conn);
