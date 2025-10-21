/**
 * @file server.h
 * @brief HTTP/HTTPS server implementation using libevent and mbedTLS
 *
 * this file defines the main server interface for creating and managing
 * HTTP and HTTPS servers with event-driven architecture.
 */

#ifndef SERVE_SERVER_H
#define SERVE_SERVER_H

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/bufferevent_ssl.h>
#include "mbedtls_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * server configuration structure
 */
typedef struct {
    int port;                   // http port (0 to disable)
    int ssl_port;              // https port (0 to disable)
    char *bind_address;        // ip address to bind to (NULL for all)
    char *ssl_cert_file;       // path to SSL certificate file
    char *ssl_key_file;        // path to SSL private key file
    int max_connections;       // maximum concurrent connections
    int timeout_seconds;       // connection timeout in seconds
    char *document_root;       // document root for static files
} server_config_t;

/**
 * server instance structure
 */
typedef struct {
    server_config_t config;
    struct event_base *event_base;
    struct evhttp *http_server;
    struct evhttp *https_server;
    SSL_CTX *ssl_ctx;
    int running;
    void *user_data;           // user-defined data pointer
} server_t;

/**
 * http request handler function type
 */
typedef void (*request_handler_t)(struct evhttp_request *req, void *user_data);

/**
 * server lifecycle functions
 */

/**
 * create a new server instance with the given configuration
 * @param config server configuration (will be copied)
 * @return new server instance or NULL on error
 */
server_t* server_create(const server_config_t *config);

/**
 * start the server and begin accepting connections
 * @param server server instance
 * @return 0 on success, -1 on error
 */
int server_start(server_t *server);

/**
 * stop the server and close all connections
 * @param server server instance
 */
void server_stop(server_t *server);

/**
 * destroy the server instance and free all resources
 * @param server server instance
 */
void server_destroy(server_t *server);

/**
 * run the server event loop (blocking)
 * @param server server instance
 * @return 0 on normal exit, -1 on error
 */
int server_run(server_t *server);

/**
 * request handling functions
 */

/**
 * set a handler for a specific URI path
 * @param server server instance
 * @param path URI path to handle
 * @param handler callback function
 * @param user_data user data to pass to handler
 * @return 0 on success, -1 on error
 */
int server_set_handler(server_t *server, const char *path,
                      request_handler_t handler, void *user_data);

/**
 * set the default handler for unmatched requests
 * @param server server instance
 * @param handler callback function
 * @param user_data user data to pass to handler
 * @return 0 on success, -1 on error
 */
int server_set_default_handler(server_t *server,
                              request_handler_t handler, void *user_data);

/**
 * utility functions
 */

/**
 * create a default server configuration
 * @return default configuration structure
 */
server_config_t server_config_default(void);

/**
 * validate server configuration
 * @param config configuration to validate
 * @return 0 if valid, -1 if invalid
 */
int server_config_validate(const server_config_t *config);

/**
 * free resources in server configuration
 * @param config configuration to clean up
 */
void server_config_cleanup(server_config_t *config);

/**
 * get the last error message
 * @return error message string
 */
const char* server_get_error(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVE_SERVER_H */
