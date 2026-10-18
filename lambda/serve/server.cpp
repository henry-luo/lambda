/**
 * @file server.cpp
 * @brief HTTP/HTTPS server implementation with libuv event loop
 *
 * Orchestrates connection accept → HTTP parse → route match → worker dispatch →
 * response send lifecycle. Keep-alive connections are tracked with idle timers.
 * Graceful shutdown drains active connections before stopping.
 *
 * Migrated from lib/serve/server.c with C+ conventions, adding:
 *   - Worker pool dispatch (all handlers run on thread pool)
 *   - Middleware pipeline integration
 *   - HTTPS/TLS support
 *   - Keep-alive with configurable idle timeout
 *   - Graceful shutdown via SIGINT/SIGTERM
 */

#include "server.hpp"
#include "serve_utils.hpp"
#include "body_parser.hpp"
#include "mime.hpp"
#include "../../lib/log.h"
#include "../../lib/str.h"

#include <string.h>
#include "../../lib/mem.h"

// ============================================================================
// Forward declarations for libuv callbacks
// ============================================================================

static void on_new_connection(uv_stream_t *server_handle, int status);
static void on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
static void on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
static void on_close(uv_handle_t *handle);
static void on_signal(uv_signal_t *handle, int signum);
static void on_timeout(uv_timer_t *timer);
static void process_buffer(ClientConnection* conn);

// ============================================================================
// Server lifecycle
// ============================================================================

static Server* server_init(const ServerConfig *config, uv_loop_t *loop, int owns_loop) {
    Server *server = (Server*)serve_calloc(1, sizeof(Server));
    if (!server) return NULL;

    if (config) {
        server->config = *config;
    } else {
        server->config = server_config_default();
    }

    server->loop = loop;
    server->owns_loop = owns_loop;

    // create components
    server->router = router_create("");
    server->middleware = middleware_stack_create();
    server->worker_pool = worker_pool_create(loop, 4); // default 4 threads

    if (!server->router || !server->middleware || !server->worker_pool) {
        log_error("server failed to create components");
        server_destroy(server);
        return NULL;
    }

    return server;
}

Server* server_create(const ServerConfig *config) {
    uv_loop_t *loop = (uv_loop_t*)serve_malloc(sizeof(uv_loop_t));
    if (!loop) return NULL;
    uv_loop_init(loop);
    return server_init(config, loop, 1);
}

Server* server_create_with_loop(const ServerConfig *config, uv_loop_t *loop) {
    if (!loop) return NULL;
    return server_init(config, loop, 0);
}

void server_destroy(Server *server) {
    if (!server) return;
    server_stop(server);
    // close callbacks and queued workers still borrow the server and its pool.
    if (server->owns_loop) uv_run(server->loop, UV_RUN_DEFAULT);
    else while (server->connections || worker_pool_active_count(server->worker_pool)) {
        uv_run(server->loop, UV_RUN_ONCE);
    }

    // destroy components
    router_destroy(server->router);
    middleware_stack_destroy(server->middleware);
    worker_pool_destroy(server->worker_pool);

    if (server->tls_ctx) {
        tls_context_destroy(server->tls_ctx);
    }

    // note: backends are not owned by server (external registry)

    if (server->owns_loop && server->loop) {
        uv_loop_close(server->loop);
        serve_free(server->loop);
    }

    server_config_cleanup(&server->config);
    serve_free(server);
}

// ============================================================================
// Start listening
// ============================================================================

int server_start(Server *server, int port) {
    if (!server) return -1;
    if (port <= 0) port = server->config.port;
    if (port <= 0) port = 3000;
    server->config.port = port;

    int r = uv_tcp_init(server->loop, &server->http_handle);
    if (r != 0) {
        log_error("server uv_tcp_init failed: %s", uv_strerror(r));
        return -1;
    }
    server->http_handle.data = server;

    struct sockaddr_in addr;
    const char *bind = server->config.bind_address ? server->config.bind_address : "0.0.0.0";
    uv_ip4_addr(bind, port, &addr);

    r = uv_tcp_bind(&server->http_handle, (const struct sockaddr*)&addr, 0);
    if (r != 0) {
        log_error("server bind failed on %s:%d: %s", bind, port, uv_strerror(r));
        return -1;
    }

    int backlog = server->config.max_connections > 0 ? server->config.max_connections : 128;
    r = uv_listen((uv_stream_t*)&server->http_handle, backlog, on_new_connection);
    if (r != 0) {
        log_error("server listen failed on port %d: %s", port, uv_strerror(r));
        return -1;
    }

    server->http_listening = 1;
    log_info("server listening on http://%s:%d", bind, port);
    return 0;
}

int server_start_tls(Server *server, int ssl_port,
                     const char *cert_file, const char *key_file) {
    if (!server) return -1;
    if (ssl_port <= 0) ssl_port = server->config.ssl_port;
    if (ssl_port <= 0) ssl_port = 3443;

    // create TLS context
    TlsConfig tls_cfg = tls_config_default();
    tls_cfg.cert_file = cert_file;
    tls_cfg.key_file = key_file;

    server->tls_ctx = tls_context_create(&tls_cfg);
    if (!server->tls_ctx) {
        log_error("server failed to create TLS context");
        return -1;
    }

    int r = uv_tcp_init(server->loop, &server->https_handle);
    if (r != 0) {
        log_error("server uv_tcp_init (TLS) failed: %s", uv_strerror(r));
        return -1;
    }
    server->https_handle.data = server;

    struct sockaddr_in addr;
    const char *bind = server->config.bind_address ? server->config.bind_address : "0.0.0.0";
    uv_ip4_addr(bind, ssl_port, &addr);

    r = uv_tcp_bind(&server->https_handle, (const struct sockaddr*)&addr, 0);
    if (r != 0) {
        log_error("server TLS bind failed on %s:%d: %s", bind, ssl_port, uv_strerror(r));
        return -1;
    }

    int backlog = server->config.max_connections > 0 ? server->config.max_connections : 128;
    r = uv_listen((uv_stream_t*)&server->https_handle, backlog, on_new_connection);
    if (r != 0) {
        log_error("server TLS listen failed on port %d: %s", ssl_port, uv_strerror(r));
        return -1;
    }

    server->https_listening = 1;
    server->config.ssl_port = ssl_port;
    log_info("server listening on https://%s:%d", bind, ssl_port);
    return 0;
}

// ============================================================================
// Run / Stop
// ============================================================================

int server_run(Server *server) {
    if (!server || !server->loop) return -1;

    // set up signal handlers for graceful shutdown
    uv_signal_init(server->loop, &server->sigint_handle);
    uv_signal_init(server->loop, &server->sigterm_handle);
    server->signals_initialized = 1;
    server->sigint_handle.data = server;
    server->sigterm_handle.data = server;
    uv_signal_start(&server->sigint_handle, on_signal, SIGINT);
    uv_signal_start(&server->sigterm_handle, on_signal, SIGTERM);

    log_info("server event loop running");
    int r = uv_run(server->loop, UV_RUN_DEFAULT);
    log_info("server event loop stopped");
    return r;
}

void server_stop(Server *server) {
    if (!server || server->shutting_down) return;
    server->shutting_down = 1;

    log_info("server initiating graceful shutdown (%d connections)",
             server->connection_count);

    // stop signal watchers
    if (server->signals_initialized) {
        uv_signal_stop(&server->sigint_handle);
        uv_signal_stop(&server->sigterm_handle);
        uv_close((uv_handle_t*)&server->sigint_handle, NULL);
        uv_close((uv_handle_t*)&server->sigterm_handle, NULL);
    }

    // stop accepting new connections
    if (server->http_listening) {
        uv_close((uv_handle_t*)&server->http_handle, NULL);
        server->http_listening = 0;
    }
    if (server->https_listening) {
        uv_close((uv_handle_t*)&server->https_handle, NULL);
        server->https_listening = 0;
    }

    // close all active connections
    ClientConnection *conn = server->connections;
    while (conn) {
        ClientConnection *next = conn->next;
        server_connection_close(conn);
        conn = next;
    }

    // an embedded server must not close handles owned by its host's loop.
}

// ============================================================================
// Route registration (Express-style)
// ============================================================================

int server_route(Server *server, HttpMethod method, const char *pattern,
                 RequestHandler handler, void *user_data) {
    if (!server || !server->router) return -1;
    return router_add(server->router, method, pattern, handler, user_data);
}

int server_get(Server *s, const char *p, RequestHandler h, void *d)     { return server_route(s, HTTP_GET, p, h, d); }
int server_post(Server *s, const char *p, RequestHandler h, void *d)    { return server_route(s, HTTP_POST, p, h, d); }
int server_put(Server *s, const char *p, RequestHandler h, void *d)     { return server_route(s, HTTP_PUT, p, h, d); }
int server_del(Server *s, const char *p, RequestHandler h, void *d)     { return server_route(s, HTTP_DELETE, p, h, d); }
int server_patch(Server *s, const char *p, RequestHandler h, void *d)   { return server_route(s, HTTP_PATCH, p, h, d); }
int server_options(Server *s, const char *p, RequestHandler h, void *d) { return server_route(s, HTTP_OPTIONS, p, h, d); }
int server_all(Server *s, const char *p, RequestHandler h, void *d)     { return server_route(s, HTTP_ALL, p, h, d); }

// ============================================================================
// Middleware registration
// ============================================================================

int server_use(Server *server, MiddlewareFn fn, void *user_data) {
    if (!server || !server->middleware) return -1;
    return middleware_stack_use(server->middleware, fn, user_data);
}

int server_use_path(Server *server, const char *path, MiddlewareFn fn, void *user_data) {
    if (!server || !server->middleware) return -1;
    return middleware_stack_use_path(server->middleware, path, fn, user_data);
}

// ============================================================================
// Sub-router mounting
// ============================================================================

int server_mount(Server *server, Router *sub_router) {
    if (!server || !server->router || !sub_router) return -1;
    return router_mount(server->router, sub_router);
}

// ============================================================================
// Static file serving
// ============================================================================

int server_set_static(Server *server, const char *url_path, const char *dir_path) {
    if (!server || !dir_path) return -1;
    StaticOptions *opts = (StaticOptions*)serve_calloc(1, sizeof(StaticOptions));
    if (!opts) return -1;
    opts->root = dir_path;
    opts->index_file = "index.html";
    opts->max_age = 3600; // 1 hour default cache

    const char *path = url_path ? url_path : "/";
    return server_use_path(server, path, middleware_static(), opts);
}

// ============================================================================
// Configuration helpers
// ============================================================================

void server_set_pool_size(Server *server, int pool_size) {
    if (!server) return;
    // pool size is set at creation; this is for future reconfiguration
    if (server->worker_pool) {
        server->worker_pool->pool_size = pool_size;
    }
}

void server_set_backends(Server *server, BackendRegistry *backends) {
    if (!server) return;
    server->backends = backends;
    if (server->worker_pool) {
        worker_pool_set_backends(server->worker_pool, backends);
    }
}

void server_set_app_data(Server *server, void *data) {
    if (server) server->app_data = data;
}

void* server_get_app_data(Server *server) {
    return server ? server->app_data : NULL;
}

// ============================================================================
// Connection management
// ============================================================================

ClientConnection* server_connection_accept(Server *server, uv_stream_t *listener, int is_tls) {
    ClientConnection *conn = (ClientConnection*)serve_calloc(1, sizeof(ClientConnection));
    if (!conn) return NULL;

    // libuv registers the handle's address in its loop; an initialized handle cannot move.
    if (uv_tcp_init(server->loop, &conn->handle) < 0) { serve_free(conn); return NULL; }
    if (uv_accept(listener, (uv_stream_t*)&conn->handle) < 0) {
        uv_close((uv_handle_t*)&conn->handle, (uv_close_cb)serve_free);
        return NULL;
    }
    conn->handle.data = conn;
    conn->server = server;
    conn->is_tls = is_tls;
    conn->keep_alive = server->config.keep_alive;

    // initial read buffer
    conn->read_cap = 4096;
    conn->read_buf = (char*)serve_malloc(conn->read_cap);
    conn->read_len = 0;
    conn->open_handles = 2;

    // idle timeout timer
    uv_timer_init(server->loop, &conn->timeout_timer);
    conn->timeout_timer.data = conn;
    int timeout = server->config.timeout_seconds > 0 ? server->config.timeout_seconds : 60;
    uv_timer_start(&conn->timeout_timer, on_timeout, timeout * 1000, 0);

    // add to connection list
    conn->prev = NULL;
    conn->next = server->connections;
    if (server->connections) {
        server->connections->prev = conn;
    }
    server->connections = conn;
    server->connection_count++;

    return conn;
}

void server_connection_remove(Server *server, ClientConnection *conn) {
    if (!server || !conn) return;

    // remove from linked list
    if (conn->prev) {
        conn->prev->next = conn->next;
    } else {
        server->connections = conn->next;
    }
    if (conn->next) {
        conn->next->prev = conn->prev;
    }
    server->connection_count--;
}

static void connection_maybe_free(ClientConnection* conn) {
    if (!conn || conn->open_handles || conn->pending_tasks) return;
    if (conn->server) server_connection_remove(conn->server, conn);
    if (conn->tls) {
        tls_connection_destroy(conn->tls);
    }

    serve_free(conn->read_buf);
    serve_free(conn);
}

static void on_close(uv_handle_t *handle) {
    ClientConnection* conn = (ClientConnection*)handle->data;
    if (conn) { conn->open_handles--; connection_maybe_free(conn); }
}

void server_connection_close(ClientConnection *conn) {
    if (!conn || conn->closing) return;
    conn->closing = 1;

    // stop the timeout timer
    uv_timer_stop(&conn->timeout_timer);
    if (!uv_is_closing((uv_handle_t*)&conn->timeout_timer)) {
        uv_close((uv_handle_t*)&conn->timeout_timer, on_close);
    }

    // close the TCP handle
    if (!uv_is_closing((uv_handle_t*)&conn->handle)) {
        uv_close((uv_handle_t*)&conn->handle, on_close);
    }
}

// ============================================================================
// HTTP request parsing helpers
// ============================================================================

// find end of HTTP headers (\r\n\r\n)
static const char* find_header_end(const char *buf, size_t len) {
    for (size_t i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n' &&
            buf[i+2] == '\r' && buf[i+3] == '\n') {
            return buf + i + 4;
        }
    }
    return NULL;
}

// reject ambiguous/overflowing framing before allocating or dispatching a body.
static bool parse_content_length(const char* headers, size_t length, size_t* content_length) {
    *content_length = 0;
    bool seen = false;
    size_t offset = 0;
    while (offset < length) {
        size_t start = offset;
        while (offset < length && headers[offset] != '\r' && headers[offset] != '\n') offset++;
        size_t end = offset;
        while (offset < length && (headers[offset] == '\r' || headers[offset] == '\n')) offset++;
        const char* colon = (const char*)memchr(headers + start, ':', end - start);
        if (!colon) continue;
        size_t name_length = (size_t)(colon - headers - start);
        if (str_ieq_const(headers + start, name_length, "transfer-encoding")) return false;
        if (!str_ieq_const(headers + start, name_length, "content-length")) continue;
        if (seen) return false;
        seen = true;
        size_t at = (size_t)(colon - headers) + 1;
        while (at < end && (headers[at] == ' ' || headers[at] == '\t')) at++;
        size_t digits = 0;
        while (at < end && headers[at] >= '0' && headers[at] <= '9') {
            size_t digit = (size_t)(headers[at++] - '0');
            if (*content_length > (SIZE_MAX - digit) / 10) return false;
            *content_length = *content_length * 10 + digit;
            digits++;
        }
        while (at < end && (headers[at] == ' ' || headers[at] == '\t')) at++;
        if (!digits || at != end) return false;
    }
    return true;
}

// ============================================================================
// Request dispatch (runs after complete HTTP message received)
// ============================================================================

static void on_request_complete(HttpRequest *req, HttpResponse *resp,
                                int status, void *user_data) {
    (void)status;
    ClientConnection *conn = (ClientConnection*)user_data;
    if (!conn) return;
    conn->pending_tasks--;
    resp->defer_send = 0;
    if (resp->send_pending) resp->headers_sent = 0;
    if (conn->closing) {
        // disconnect may precede worker completion; discard without writing a closed socket.
        resp->headers_sent = 1;
        http_request_destroy(req);
        http_response_destroy(resp);
        connection_maybe_free(conn);
        return;
    }

    // send the response if not already sent
    if (!resp->headers_sent) {
        http_response_send(resp);
    }

    // handle keep-alive
    if (conn->keep_alive && req->_keep_alive &&
        conn->request_count < conn->server->config.max_requests_per_conn) {
        // reset read buffer for next request
        size_t remaining = conn->read_len - conn->consumed_bytes;
        memmove(conn->read_buf, conn->read_buf + conn->consumed_bytes, remaining);
        conn->read_len = remaining;
        uv_read_start((uv_stream_t*)&conn->handle, on_alloc, on_read);
        // restart timeout timer
        int timeout = conn->server->config.keep_alive_timeout > 0
                     ? conn->server->config.keep_alive_timeout : 5;
        uv_timer_start(&conn->timeout_timer, on_timeout, timeout * 1000, 0);
    } else {
        // drain queued writes before closing a Connection: close response.
        conn->keep_alive = 0;
        conn->shutdown.data = conn;
        if (uv_shutdown(&conn->shutdown, (uv_stream_t*)&conn->handle,
                [](uv_shutdown_t* request, int) {
                    server_connection_close((ClientConnection*)request->data);
                }) < 0) server_connection_close(conn);
    }

    // cleanup request/response
    http_request_destroy(req);
    http_response_destroy(resp);
    if (!conn->closing && conn->keep_alive && conn->read_len) process_buffer(conn);
}

static void dispatch_request(ClientConnection *conn, const char *raw, size_t raw_len) {
    Server *server = conn->server;
    if (!server) return;

    conn->request_count++;

    // parse HTTP request
    HttpRequest *req = http_request_parse(raw, raw_len);
    if (!req) {
        log_error("server failed to parse HTTP request");
        server_connection_close(conn);
        return;
    }

    // set connection info on request
    req->client = &conn->handle;
    req->app = server->app_data;

    // get remote address
    struct sockaddr_storage addr;
    int addr_len = sizeof(addr);
    if (uv_tcp_getpeername(&conn->handle, (struct sockaddr*)&addr, &addr_len) == 0) {
        req->remote_addr[0] = '\0';
        if (addr.ss_family == AF_INET) {
            uv_ip4_name((struct sockaddr_in*)&addr, req->remote_addr, sizeof(req->remote_addr));
        } else {
            uv_ip6_name((struct sockaddr_in6*)&addr, req->remote_addr, sizeof(req->remote_addr));
        }
    }

    // create response
    HttpResponse *resp = http_response_create(&conn->handle);
    if (!resp) {
        http_request_destroy(req);
        server_connection_close(conn);
        return;
    }
    resp->defer_send = 1;

    // route matching
    HttpHeader *route_params = NULL;
    void *handler_data = NULL;
    RequestHandler handler = router_match(server->router, req->method, req->path,
                                          &route_params, &handler_data);
    req->route_params = route_params;

    // if no route matched, use a default 404 handler
    if (!handler) {
        http_response_error(resp, HTTP_404_NOT_FOUND, "Not Found");
        conn->pending_tasks++;
        on_request_complete(req, resp, 0, conn);
        return;
    }

    // dispatch to worker pool: middleware + handler run on worker thread
    conn->pending_tasks++;
    int r = worker_pool_dispatch(server->worker_pool,
                                 req, resp, handler, handler_data,
                                 server->middleware,
                                 on_request_complete, conn);
    if (r != 0) {
        http_response_error(resp, HTTP_503_SERVICE_UNAVAILABLE, "Service Unavailable");
        on_request_complete(req, resp, r, conn);
    }
}

// ============================================================================
// libuv callbacks
// ============================================================================

static void on_new_connection(uv_stream_t *server_handle, int status) {
    if (status < 0) {
        log_error("server connection error: %s", uv_strerror(status));
        return;
    }

    Server *server = (Server*)server_handle->data;
    if (!server || server->shutting_down) return;

    // check max connections
    if (server->config.max_connections > 0 &&
        server->connection_count >= server->config.max_connections) {
        log_error("server max connections reached (%d), rejecting",
                  server->config.max_connections);
        uv_tcp_t *temp = (uv_tcp_t*)serve_malloc(sizeof(uv_tcp_t));
        uv_tcp_init(server->loop, temp);
        uv_accept(server_handle, (uv_stream_t*)temp);
        uv_close((uv_handle_t*)temp, (uv_close_cb)serve_free);
        return;
    }

    int is_tls = (server_handle == (uv_stream_t*)&server->https_handle);
    ClientConnection *conn = server_connection_accept(server, (uv_stream_t*)server_handle, is_tls);
    if (!conn) return;

    // for TLS connections, create TLS wrapper
    if (is_tls && server->tls_ctx) {
        conn->tls = tls_connection_create(server->tls_ctx, &conn->handle);
        if (!conn->tls) {
            log_error("server failed to create TLS connection");
            server_connection_close(conn);
            return;
        }
    }

    uv_read_start((uv_stream_t*)&conn->handle, on_alloc, on_read);
}

static void on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    (void)handle;
    buf->base = (char*)serve_malloc(suggested_size);
    buf->len = suggested_size;
}

static void on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    ClientConnection *conn = (ClientConnection*)stream->data;
    if (!conn) {
        serve_free(buf->base);
        return;
    }

    if (nread < 0) {
        if (nread != UV_EOF) {
            log_debug("server read error: %s", uv_strerror((int)nread));
        }
        serve_free(buf->base);
        server_connection_close(conn);
        return;
    }

    if (nread == 0) {
        serve_free(buf->base);
        return;
    }

    // accumulate data into connection buffer
    size_t needed = conn->read_len + (size_t)nread;
    if (needed > conn->read_cap) {
        size_t new_cap = conn->read_cap * 2;
        if (new_cap < needed) new_cap = needed;

        // enforce max header size
        Server *server = conn->server;
        size_t max_hdr = server->config.max_header_size > 0 ? server->config.max_header_size : 8192;
        size_t limit = max_hdr + server->config.max_body_size;
        if (needed > limit) {
            log_error("server request too large, closing connection");
            serve_free(buf->base);
            server_connection_close(conn);
            return;
        }

        if (new_cap > limit) new_cap = limit;
        char* resized = (char*)serve_realloc(conn->read_buf, new_cap);
        if (!resized) {
            serve_free(buf->base);
            server_connection_close(conn);
            return;
        }
        conn->read_buf = resized;
        conn->read_cap = new_cap;
    }
    memcpy(conn->read_buf + conn->read_len, buf->base, (size_t)nread);
    conn->read_len += (size_t)nread;
    serve_free(buf->base);

    process_buffer(conn);
}

static void process_buffer(ClientConnection* conn) {
    if (conn->closing || conn->pending_tasks) return;
    // check if we have a complete HTTP message
    const char *header_end = find_header_end(conn->read_buf, conn->read_len);
    if (!header_end) {
        if (conn->read_len > conn->server->config.max_header_size) server_connection_close(conn);
        return;
    }

    size_t header_len = (size_t)(header_end - conn->read_buf);
    size_t content_length = 0;
    if (header_len > conn->server->config.max_header_size ||
            !parse_content_length(conn->read_buf, header_len, &content_length) ||
            content_length > conn->server->config.max_body_size) {
        server_connection_close(conn);
        return;
    }
    size_t total_expected = header_len + content_length;

    if (conn->read_len < total_expected) return; // need more body data

    // reset timeout since we got a complete request
    uv_timer_stop(&conn->timeout_timer);

    // dispatch the request
    conn->consumed_bytes = total_expected;
    uv_read_stop((uv_stream_t*)&conn->handle);
    dispatch_request(conn, conn->read_buf, total_expected);
}

static void on_signal(uv_signal_t *handle, int signum) {
    Server *server = (Server*)handle->data;
    log_info("server received signal %d, shutting down", signum);
    server_stop(server);
}

static void on_timeout(uv_timer_t *timer) {
    ClientConnection *conn = (ClientConnection*)timer->data;
    log_debug("server connection timed out");
    server_connection_close(conn);
}
