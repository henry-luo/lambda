//
// uds_transport.cpp — Unix domain socket transport implementation
//

#include "uds_transport.hpp"
#include "../../lib/log.h"

#include "../../lib/mem.h"
#include <cstring>
#include <cstdio>
#include <unistd.h>

#define UDS_READ_BUF_INITIAL 4096

// ── path helpers ──

void uds_worker_path(const char* base_path, int worker_index, char* out_path, int out_len) {
    snprintf(out_path, out_len, "%s-%d.sock", base_path, worker_index);
}

void uds_cleanup_socket(const char* socket_path) {
    unlink(socket_path);
}

// ── libuv callbacks ──

static void alloc_buffer(uv_handle_t* handle, size_t suggested, uv_buf_t* buf) {
    (void)handle;
    buf->base = (char*)mem_alloc(suggested, MEM_CAT_SERVE);
    buf->len = buf->base ? (int)suggested : 0;
}

static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    UdsConnection* conn = (UdsConnection*)stream->data;

    if (nread < 0) {
        mem_free(buf->base);
        if (conn->close_cb) {
            conn->close_cb(conn->user_data);
        }
        return;
    }

    if (nread == 0) {
        mem_free(buf->base);
        return;
    }

    if (!line_framer_append(&conn->read_lines, buf->base, (size_t)nread)) {
        log_error("UDS: failed to append incoming line bytes");
        mem_free(buf->base);
        return;
    }
    mem_free(buf->base);

    // process complete lines (newline-delimited)
    while (true) {
        size_t line_len = 0;
        const char* line = line_framer_peek(&conn->read_lines, &line_len);
        if (!line) break;
        if (conn->read_cb) {
            conn->read_cb(line, (int)line_len, conn->user_data);
        }
        (void)line_framer_consume(&conn->read_lines, line_len + 1);
    }
}

// ── client-side ──

UdsConnection* uds_connection_create(uv_loop_t* loop) {
    UdsConnection* conn = (UdsConnection*)mem_calloc(1, sizeof(UdsConnection), MEM_CAT_SERVE);
    if (!conn) return nullptr;

    if (!line_framer_init(&conn->read_lines, UDS_READ_BUF_INITIAL, MEM_CAT_SERVE)) {
        mem_free(conn);
        return nullptr;
    }

    uv_pipe_init(loop, &conn->pipe, 0);
    conn->pipe.data = conn;

    return conn;
}

static void on_connect(uv_connect_t* req, int status) {
    UdsConnection* conn = (UdsConnection*)req->data;
    if (status < 0) {
        log_error("UDS: connect failed: %s", uv_strerror(status));
        if (conn->close_cb) {
            conn->close_cb(conn->user_data);
        }
        return;
    }
    log_debug("UDS: connected");
}

int uds_connection_connect(UdsConnection* conn, const char* socket_path) {
    conn->connect_req.data = conn;
    uv_pipe_connect(&conn->connect_req, &conn->pipe, socket_path, on_connect);
    return 0;
}

void uds_connection_start_read(UdsConnection* conn, UdsReadCallback read_cb, UdsCloseCallback close_cb, void* user_data) {
    conn->read_cb = read_cb;
    conn->close_cb = close_cb;
    conn->user_data = user_data;
    uv_read_start((uv_stream_t*)&conn->pipe, alloc_buffer, on_read);
}

static void on_write_done(uv_write_t* req, int status) {
    if (status < 0) {
        log_error("UDS: write failed: %s", uv_strerror(status));
    }
    mem_free(req);
}

int uds_connection_write(UdsConnection* conn, const char* data, int len) {
    uv_write_t* req = (uv_write_t*)mem_alloc(sizeof(uv_write_t), MEM_CAT_SERVE);
    if (!req) return -1;

    uv_buf_t buf = uv_buf_init((char*)data, len);
    int r = uv_write(req, (uv_stream_t*)&conn->pipe, &buf, 1, on_write_done);
    if (r < 0) {
        log_error("UDS: uv_write failed: %s", uv_strerror(r));
        mem_free(req);
        return r;
    }
    return 0;
}

static void on_close(uv_handle_t* handle) {
    UdsConnection* conn = (UdsConnection*)handle->data;
    line_framer_destroy(&conn->read_lines);
    mem_free(conn);
}

void uds_connection_close(UdsConnection* conn) {
    if (!conn) return;
    if (!uv_is_closing((uv_handle_t*)&conn->pipe)) {
        uv_close((uv_handle_t*)&conn->pipe, on_close);
    }
}

// ── server-side ──

static void on_server_close(uv_handle_t* handle) {
    (void)handle;
}

static void on_new_connection(uv_stream_t* server_handle, int status) {
    UdsServer* server = (UdsServer*)server_handle->data;
    if (status < 0) {
        log_error("UDS server: new connection error: %s", uv_strerror(status));
        return;
    }

    if (server->connection_count >= server->max_connections) {
        log_error("UDS server: max connections reached");
        return;
    }

    UdsConnection* conn = uds_connection_create(server->loop);
    if (!conn) return;

    int r = uv_accept(server_handle, (uv_stream_t*)&conn->pipe);
    if (r < 0) {
        log_error("UDS server: accept failed: %s", uv_strerror(r));
        uds_connection_close(conn);
        return;
    }

    server->connections[server->connection_count++] = conn;
    uds_connection_start_read(conn, server->read_cb, server->close_cb, server->user_data);
    log_debug("UDS server: accepted connection %d", server->connection_count);
}

UdsServer* uds_server_create(uv_loop_t* loop, const char* socket_path, int max_connections) {
    UdsServer* server = (UdsServer*)mem_calloc(1, sizeof(UdsServer), MEM_CAT_SERVE);
    if (!server) return nullptr;

    server->loop = loop;
    server->max_connections = max_connections;
    server->connections = (UdsConnection**)mem_calloc(max_connections, sizeof(UdsConnection*), MEM_CAT_SERVE);

    uv_pipe_init(loop, &server->pipe, 0);
    server->pipe.data = server;

    // clean up old socket if exists
    uds_cleanup_socket(socket_path);

    int r = uv_pipe_bind(&server->pipe, socket_path);
    if (r < 0) {
        log_error("UDS server: bind failed: %s", uv_strerror(r));
        mem_free(server->connections);
        mem_free(server);
        return nullptr;
    }

    return server;
}

int uds_server_listen(UdsServer* server, UdsReadCallback read_cb, UdsCloseCallback close_cb, void* user_data) {
    server->read_cb = read_cb;
    server->close_cb = close_cb;
    server->user_data = user_data;

    int r = uv_listen((uv_stream_t*)&server->pipe, 8, on_new_connection);
    if (r < 0) {
        log_error("UDS server: listen failed: %s", uv_strerror(r));
        return r;
    }
    return 0;
}

void uds_server_close(UdsServer* server) {
    if (!server) return;

    for (int i = 0; i < server->connection_count; i++) {
        uds_connection_close(server->connections[i]);
    }

    if (!uv_is_closing((uv_handle_t*)&server->pipe)) {
        uv_close((uv_handle_t*)&server->pipe, on_server_close);
    }

    mem_free(server->connections);
    mem_free(server);
}
