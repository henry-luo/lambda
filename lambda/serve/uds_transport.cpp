//
// uds_transport.cpp — Unix domain socket transport implementation
//

#include "uds_transport.hpp"
#include "../../lib/log.h"

#include <cstdlib>
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

// ── read buffer helpers ──

static void ensure_read_capacity(UdsConnection* conn, int needed) {
    if (conn->read_len + needed <= conn->read_cap) return;
    int new_cap = conn->read_cap * 2;
    if (new_cap < conn->read_len + needed) new_cap = conn->read_len + needed;
    char* new_buf = (char*)realloc(conn->read_buf, new_cap);
    if (!new_buf) {
        log_error("UDS: read buffer realloc failed");
        return;
    }
    conn->read_buf = new_buf;
    conn->read_cap = new_cap;
}

// ── libuv callbacks ──

static void alloc_buffer(uv_handle_t* handle, size_t suggested, uv_buf_t* buf) {
    (void)handle;
    buf->base = (char*)malloc(suggested);
    buf->len = buf->base ? (int)suggested : 0;
}

static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    UdsConnection* conn = (UdsConnection*)stream->data;

    if (nread < 0) {
        free(buf->base);
        if (conn->close_cb) {
            conn->close_cb(conn->user_data);
        }
        return;
    }

    if (nread == 0) {
        free(buf->base);
        return;
    }

    ensure_read_capacity(conn, (int)nread);
    memcpy(conn->read_buf + conn->read_len, buf->base, nread);
    conn->read_len += (int)nread;
    free(buf->base);

    // process complete lines (newline-delimited)
    while (true) {
        char* nl = (char*)memchr(conn->read_buf, '\n', conn->read_len);
        if (!nl) break;

        int line_len = (int)(nl - conn->read_buf);
        if (conn->read_cb) {
            conn->read_cb(conn->read_buf, line_len, conn->user_data);
        }

        int remaining = conn->read_len - line_len - 1;
        if (remaining > 0) {
            memmove(conn->read_buf, nl + 1, remaining);
        }
        conn->read_len = remaining;
    }
}

// ── client-side ──

UdsConnection* uds_connection_create(uv_loop_t* loop) {
    UdsConnection* conn = (UdsConnection*)calloc(1, sizeof(UdsConnection));
    if (!conn) return nullptr;

    uv_pipe_init(loop, &conn->pipe, 0);
    conn->pipe.data = conn;

    conn->read_buf = (char*)malloc(UDS_READ_BUF_INITIAL);
    conn->read_len = 0;
    conn->read_cap = UDS_READ_BUF_INITIAL;

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
    free(req);
}

int uds_connection_write(UdsConnection* conn, const char* data, int len) {
    uv_write_t* req = (uv_write_t*)malloc(sizeof(uv_write_t));
    if (!req) return -1;

    uv_buf_t buf = uv_buf_init((char*)data, len);
    int r = uv_write(req, (uv_stream_t*)&conn->pipe, &buf, 1, on_write_done);
    if (r < 0) {
        log_error("UDS: uv_write failed: %s", uv_strerror(r));
        free(req);
        return r;
    }
    return 0;
}

static void on_close(uv_handle_t* handle) {
    UdsConnection* conn = (UdsConnection*)handle->data;
    free(conn->read_buf);
    free(conn);
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
    UdsServer* server = (UdsServer*)calloc(1, sizeof(UdsServer));
    if (!server) return nullptr;

    server->loop = loop;
    server->max_connections = max_connections;
    server->connections = (UdsConnection**)calloc(max_connections, sizeof(UdsConnection*));

    uv_pipe_init(loop, &server->pipe, 0);
    server->pipe.data = server;

    // clean up old socket if exists
    uds_cleanup_socket(socket_path);

    int r = uv_pipe_bind(&server->pipe, socket_path);
    if (r < 0) {
        log_error("UDS server: bind failed: %s", uv_strerror(r));
        free(server->connections);
        free(server);
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

    free(server->connections);
    free(server);
}
