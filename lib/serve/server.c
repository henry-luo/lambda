/**
 * @file server.c
 * @brief HTTP/HTTPS server implementation using libuv
 */

#include "server.h"
#include "http_handler.h"
#include "tls_handler.h"
#include "utils.h"
#include <signal.h>
#include <string.h>
#include <stdlib.h>

// ============================================================================
// Internal Types
// ============================================================================

struct route_entry_s {
    char *path;
    request_handler_t handler;
    void *user_data;
    route_entry_t *next;
};

// per-connection context
typedef struct {
    uv_tcp_t handle;
    server_t *server;
    char *buf;
    size_t buf_len;
    size_t buf_cap;
} client_conn_t;

// global server instance for signal handling
static server_t *global_server = NULL;

// ============================================================================
// Route Table Helpers
// ============================================================================

static route_entry_t* route_find(server_t *server, const char *path) {
    for (route_entry_t *r = server->routes; r; r = r->next) {
        if (strcmp(r->path, path) == 0) return r;
    }
    return NULL;
}

static void routes_free(route_entry_t *list) {
    while (list) {
        route_entry_t *next = list->next;
        serve_free(list->path);
        serve_free(list);
        list = next;
    }
}

// ============================================================================
// Connection Callbacks
// ============================================================================

static void on_close(uv_handle_t *handle) {
    client_conn_t *conn = (client_conn_t *)handle;
    if (conn->buf) serve_free(conn->buf);
    serve_free(conn);
}

static void on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    client_conn_t *conn = (client_conn_t *)handle;
    size_t remaining = conn->buf_cap - conn->buf_len;
    if (remaining < 1024) {
        size_t new_cap = conn->buf_cap ? conn->buf_cap * 2 : 8192;
        char *new_buf = (char *)serve_malloc(new_cap);
        if (!new_buf) {
            buf->base = NULL;
            buf->len = 0;
            return;
        }
        if (conn->buf_len > 0) memcpy(new_buf, conn->buf, conn->buf_len);
        serve_free(conn->buf);
        conn->buf = new_buf;
        conn->buf_cap = new_cap;
        remaining = new_cap - conn->buf_len;
    }
    buf->base = conn->buf + conn->buf_len;
    buf->len = (unsigned int)remaining;
}

static void dispatch_request(client_conn_t *conn) {
    server_t *server = conn->server;

    http_request_t *request = http_request_parse(conn->buf, conn->buf_len);
    if (!request) {
        http_send_error(&conn->handle, HTTP_STATUS_BAD_REQUEST, "malformed request");
        uv_close((uv_handle_t *)&conn->handle, on_close);
        return;
    }

    request->client = &conn->handle;
    http_response_t *response = http_response_create(&conn->handle);
    if (!response) {
        http_request_destroy(request);
        uv_close((uv_handle_t *)&conn->handle, on_close);
        return;
    }

    // find matching route
    route_entry_t *route = request->path ? route_find(server, request->path) : NULL;
    if (route) {
        route->handler(request, response, route->user_data);
    } else if (server->default_handler) {
        server->default_handler(request, response, server->default_handler_data);
    } else {
        http_response_set_status(response, HTTP_STATUS_NOT_FOUND);
        http_response_set_header(response, "Content-Type", "text/plain");
        http_response_add_string(response, "Not Found");
    }

    // ensure response is sent
    if (!response->headers_sent) {
        http_response_send(response);
    }
    http_header_free(response->headers);
    serve_free(response->body);
    serve_free(response);

    http_request_destroy(request);

    // close connection after response (HTTP/1.0 style for simplicity)
    uv_close((uv_handle_t *)&conn->handle, on_close);
}

static void on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    client_conn_t *conn = (client_conn_t *)stream;

    if (nread < 0) {
        uv_close((uv_handle_t *)stream, on_close);
        return;
    }

    if (nread == 0) return;

    conn->buf_len += (size_t)nread;

    // check if we have the full headers (look for \r\n\r\n)
    if (conn->buf_len >= 4) {
        char *hdr_end = NULL;
        for (size_t i = 0; i <= conn->buf_len - 4; i++) {
            if (conn->buf[i] == '\r' && conn->buf[i+1] == '\n' &&
                conn->buf[i+2] == '\r' && conn->buf[i+3] == '\n') {
                hdr_end = conn->buf + i + 4;
                break;
            }
        }

        if (hdr_end) {
            // check for Content-Length to handle body
            // simple check: search for Content-Length header
            size_t header_size = (size_t)(hdr_end - conn->buf);
            size_t content_length = 0;
            const char *cl = "Content-Length:";
            size_t cl_len = strlen(cl);
            for (size_t i = 0; i + cl_len < header_size; i++) {
                if (strncasecmp(conn->buf + i, cl, cl_len) == 0) {
                    content_length = (size_t)atol(conn->buf + i + cl_len);
                    break;
                }
            }

            size_t total_needed = header_size + content_length;
            if (conn->buf_len >= total_needed) {
                uv_read_stop(stream);
                dispatch_request(conn);
            }
            // else continue reading for the body
        }
    }
}

static void on_new_connection(uv_stream_t *server_handle, int status) {
    if (status < 0) return;

    server_t *server = (server_t *)server_handle->data;

    client_conn_t *conn = (client_conn_t *)serve_malloc(sizeof(client_conn_t));
    if (!conn) return;
    memset(conn, 0, sizeof(client_conn_t));
    conn->server = server;

    uv_tcp_init(server->loop, &conn->handle);

    if (uv_accept(server_handle, (uv_stream_t *)&conn->handle) == 0) {
        uv_read_start((uv_stream_t *)&conn->handle, on_alloc, on_read);
    } else {
        uv_close((uv_handle_t *)&conn->handle, on_close);
    }
}

// ============================================================================
// Signal Handler
// ============================================================================

static void signal_handler(int sig) {
    if (global_server) {
        SERVE_LOG_INFO("received signal %d, shutting down server", sig);
        server_stop(global_server);
    }
}

// ============================================================================
// Server Lifecycle
// ============================================================================

server_t* server_create(const server_config_t *config) {
    if (!config) {
        serve_set_error("null configuration");
        return NULL;
    }

    if (server_config_validate(config) != 0) {
        return NULL;
    }

    server_t *server = (server_t *)serve_malloc(sizeof(server_t));
    if (!server) {
        serve_set_error("failed to allocate server structure");
        return NULL;
    }
    memset(server, 0, sizeof(server_t));

    server->config = *config;
    server->config.bind_address = serve_strdup(config->bind_address);
    server->config.ssl_cert_file = serve_strdup(config->ssl_cert_file);
    server->config.ssl_key_file = serve_strdup(config->ssl_key_file);
    server->config.document_root = serve_strdup(config->document_root);

    // create event loop
    server->loop = (uv_loop_t *)malloc(sizeof(uv_loop_t));
    if (!server->loop || uv_loop_init(server->loop) != 0) {
        serve_set_error("failed to create event loop");
        server_destroy(server);
        return NULL;
    }

    // init tcp server handle for HTTP
    if (server->config.port > 0) {
        uv_tcp_init(server->loop, &server->tcp_server);
        server->tcp_server.data = server;
    }

    // create ssl context if ssl enabled
    if (server->config.ssl_port > 0 && server->config.ssl_cert_file &&
        server->config.ssl_key_file) {

        tls_config_t tls_config = tls_config_default();
        tls_config.cert_file = server->config.ssl_cert_file;
        tls_config.key_file = server->config.ssl_key_file;

        server->ssl_ctx = tls_create_context(&tls_config);
        if (!server->ssl_ctx) {
            SERVE_LOG_INFO("tls context creation failed, https disabled");
        } else {
            uv_tcp_init(server->loop, &server->tls_server);
            server->tls_server.data = server;
        }
    }

    server->running = 0;
    SERVE_LOG_INFO("server created successfully");
    return server;
}

int server_start(server_t *server) {
    if (!server) {
        serve_set_error("null server");
        return -1;
    }

    if (server->running) {
        serve_set_error("server already running");
        return -1;
    }

    const char *bind_addr = server->config.bind_address ?
                           server->config.bind_address : "0.0.0.0";

    // start HTTP server
    if (server->config.port > 0) {
        struct sockaddr_in addr;
        uv_ip4_addr(bind_addr, server->config.port, &addr);

        if (uv_tcp_bind(&server->tcp_server, (const struct sockaddr *)&addr, 0) != 0) {
            serve_set_error("failed to bind http server to %s:%d",
                           bind_addr, server->config.port);
            return -1;
        }

        int backlog = server->config.max_connections > 0 ?
                     server->config.max_connections : 128;
        if (uv_listen((uv_stream_t *)&server->tcp_server, backlog,
                      on_new_connection) != 0) {
            serve_set_error("failed to listen on %s:%d",
                           bind_addr, server->config.port);
            return -1;
        }

        SERVE_LOG_INFO("http server listening on %s:%d",
                      bind_addr, server->config.port);
    }

    // start HTTPS server (TLS accept handled separately)
    if (server->config.ssl_port > 0 && server->ssl_ctx) {
        struct sockaddr_in addr;
        uv_ip4_addr(bind_addr, server->config.ssl_port, &addr);

        if (uv_tcp_bind(&server->tls_server, (const struct sockaddr *)&addr, 0) != 0) {
            serve_set_error("failed to bind https server to %s:%d",
                           bind_addr, server->config.ssl_port);
            return -1;
        }

        int backlog = server->config.max_connections > 0 ?
                     server->config.max_connections : 128;
        if (uv_listen((uv_stream_t *)&server->tls_server, backlog,
                      on_new_connection) != 0) {
            serve_set_error("failed to listen on %s:%d",
                           bind_addr, server->config.ssl_port);
            return -1;
        }

        SERVE_LOG_INFO("https server listening on %s:%d",
                      bind_addr, server->config.ssl_port);
    }

    // setup signal handlers
    global_server = server;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    server->running = 1;
    SERVE_LOG_INFO("server started successfully");
    return 0;
}

static void on_server_close(uv_handle_t *handle) {
    // nothing to do
}

void server_stop(server_t *server) {
    if (!server || !server->running) return;

    if (server->loop) {
        uv_stop(server->loop);
    }

    server->running = 0;
    global_server = NULL;
    SERVE_LOG_INFO("server stopped");
}

void server_destroy(server_t *server) {
    if (!server) return;

    server_stop(server);

    // close tcp handles
    if (server->config.port > 0 && uv_is_active((uv_handle_t *)&server->tcp_server)) {
        uv_close((uv_handle_t *)&server->tcp_server, NULL);
    }
    if (server->ssl_ctx && uv_is_active((uv_handle_t *)&server->tls_server)) {
        uv_close((uv_handle_t *)&server->tls_server, NULL);
    }

    // free ssl context
    if (server->ssl_ctx) {
        tls_destroy_context(server->ssl_ctx);
    }

    // run loop once to process all close callbacks
    if (server->loop) {
        uv_run(server->loop, UV_RUN_NOWAIT);
        uv_loop_close(server->loop);
        free(server->loop);
    }

    // free route table
    routes_free(server->routes);

    // free configuration strings
    serve_free(server->config.bind_address);
    serve_free(server->config.ssl_cert_file);
    serve_free(server->config.ssl_key_file);
    serve_free(server->config.document_root);

    serve_free(server);
    SERVE_LOG_DEBUG("server destroyed");
}

int server_run(server_t *server) {
    if (!server) {
        serve_set_error("null server");
        return -1;
    }

    if (!server->running) {
        serve_set_error("server not started");
        return -1;
    }

    SERVE_LOG_INFO("entering event loop");
    int result = uv_run(server->loop, UV_RUN_DEFAULT);
    SERVE_LOG_INFO("event loop exited");
    return result < 0 ? -1 : 0;
}

// ============================================================================
// Request Handling
// ============================================================================

int server_set_handler(server_t *server, const char *path,
                      request_handler_t handler, void *user_data) {
    if (!server || !path || !handler) {
        serve_set_error("invalid parameters");
        return -1;
    }

    // check if path already has a handler
    route_entry_t *existing = route_find(server, path);
    if (existing) {
        existing->handler = handler;
        existing->user_data = user_data;
    } else {
        route_entry_t *route = (route_entry_t *)serve_malloc(sizeof(route_entry_t));
        if (!route) return -1;
        route->path = serve_strdup(path);
        route->handler = handler;
        route->user_data = user_data;
        route->next = server->routes;
        server->routes = route;
    }

    SERVE_LOG_DEBUG("handler set for path: %s", path);
    return 0;
}

int server_set_default_handler(server_t *server,
                              request_handler_t handler, void *user_data) {
    if (!server || !handler) {
        serve_set_error("invalid parameters");
        return -1;
    }

    server->default_handler = handler;
    server->default_handler_data = user_data;

    SERVE_LOG_DEBUG("default handler set");
    return 0;
}

// ============================================================================
// Utility Functions
// ============================================================================

server_config_t server_config_default(void) {
    server_config_t config;
    memset(&config, 0, sizeof(config));
    config.port = 8080;
    config.ssl_port = 8443;
    config.max_connections = 1024;
    config.timeout_seconds = 60;
    return config;
}

int server_config_validate(const server_config_t *config) {
    if (!config) {
        serve_set_error("null configuration");
        return -1;
    }

    if (config->port <= 0 && config->ssl_port <= 0) {
        serve_set_error("at least one port (http or https) must be specified");
        return -1;
    }

    if (config->port > 0 && (config->port < 1 || config->port > 65535)) {
        serve_set_error("invalid http port: %d", config->port);
        return -1;
    }

    if (config->ssl_port > 0 && (config->ssl_port < 1 || config->ssl_port > 65535)) {
        serve_set_error("invalid https port: %d", config->ssl_port);
        return -1;
    }

    if (config->ssl_port > 0) {
        if (!config->ssl_cert_file || !config->ssl_key_file) {
            serve_set_error("ssl certificate and key files required for https");
            return -1;
        }

        if (!serve_file_exists(config->ssl_cert_file)) {
            serve_set_error("ssl certificate file not found: %s",
                           config->ssl_cert_file);
            return -1;
        }

        if (!serve_file_exists(config->ssl_key_file)) {
            serve_set_error("ssl key file not found: %s", config->ssl_key_file);
            return -1;
        }
    }

    if (config->max_connections < 0) {
        serve_set_error("invalid max connections: %d", config->max_connections);
        return -1;
    }

    if (config->timeout_seconds < 0) {
        serve_set_error("invalid timeout: %d", config->timeout_seconds);
        return -1;
    }

    return 0;
}

void server_config_cleanup(server_config_t *config) {
    if (!config) return;

    serve_free(config->bind_address);
    serve_free(config->ssl_cert_file);
    serve_free(config->ssl_key_file);
    serve_free(config->document_root);

    memset(config, 0, sizeof(server_config_t));
}

const char* server_get_error(void) {
    return serve_get_error();
}
