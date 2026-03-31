#pragma once

//
// uds_transport.hpp — Unix domain socket transport helpers for ASGI bridge
//
// Provides UDS server/client lifecycle wrapping libuv uv_pipe_t.
// Used by asgi_bridge when transport is ASGI_TRANSPORT_UDS.
//

#include <uv.h>

#ifdef __cplusplus
extern "C" {
#endif

// UDS connection read callback — called when a complete line is available
typedef void (*UdsReadCallback)(const char* data, int len, void* user_data);

// UDS connection close callback
typedef void (*UdsCloseCallback)(void* user_data);

// A single UDS connection (one per worker)
typedef struct UdsConnection {
    uv_pipe_t       pipe;           // libuv pipe handle
    uv_connect_t    connect_req;    // connect request (client-side)
    char*           read_buf;       // accumulation buffer
    int             read_len;       // bytes accumulated
    int             read_cap;       // buffer capacity
    UdsReadCallback read_cb;        // line read callback
    UdsCloseCallback close_cb;      // close callback
    void*           user_data;      // callback context
} UdsConnection;

// A UDS server that accepts connections from workers
typedef struct UdsServer {
    uv_pipe_t        pipe;          // listening pipe
    uv_loop_t*       loop;          // event loop
    UdsConnection**  connections;   // accepted connections
    int              connection_count;
    int              max_connections;
    UdsReadCallback  read_cb;       // shared read callback for accepted connections
    UdsCloseCallback close_cb;      // shared close callback
    void*            user_data;     // callback context
} UdsServer;

// Generate a socket path for a given worker index.
// Writes to out_path (must be at least 108 bytes).
// Format: <base_path>-<worker_index>.sock
void uds_worker_path(const char* base_path, int worker_index, char* out_path, int out_len);

// --- Client-side (C++ connecting to Python worker) ---

// Create a UDS client connection.
UdsConnection* uds_connection_create(uv_loop_t* loop);

// Connect to a UDS socket path. Returns 0 on success.
int uds_connection_connect(UdsConnection* conn, const char* socket_path);

// Start reading lines from the connection.
void uds_connection_start_read(UdsConnection* conn, UdsReadCallback read_cb, UdsCloseCallback close_cb, void* user_data);

// Write data to the connection. Returns 0 on success.
int uds_connection_write(UdsConnection* conn, const char* data, int len);

// Close and free the connection.
void uds_connection_close(UdsConnection* conn);

// --- Server-side (Python worker accepting connections) ---

// Create a UDS server bound to socket_path.
UdsServer* uds_server_create(uv_loop_t* loop, const char* socket_path, int max_connections);

// Start listening for connections.
int uds_server_listen(UdsServer* server, UdsReadCallback read_cb, UdsCloseCallback close_cb, void* user_data);

// Close the server and all connections.
void uds_server_close(UdsServer* server);

// Clean up a socket file if it exists.
void uds_cleanup_socket(const char* socket_path);

#ifdef __cplusplus
}
#endif
