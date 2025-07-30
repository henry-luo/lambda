/**
 * @file server.c
 * @brief HTTP/HTTPS server implementation
 */

#include "server.h"
#include "http_handler.h"
#include "tls_handler.h"
#include "utils.h"
#include <signal.h>
#include <errno.h>

// global server instance for signal handling
static server_t *global_server = NULL;

// signal handler for graceful shutdown
static void signal_handler(int sig) {
    if (global_server) {
        SERVE_LOG_INFO("received signal %d, shutting down server", sig);
        server_stop(global_server);
    }
}

/**
 * server lifecycle implementation
 */

server_t* server_create(const server_config_t *config) {
    if (!config) {
        serve_set_error("null configuration");
        return NULL;
    }
    
    // validate configuration
    if (server_config_validate(config) != 0) {
        return NULL;
    }
    
    // allocate server structure
    server_t *server = serve_malloc(sizeof(server_t));
    if (!server) {
        serve_set_error("failed to allocate server structure");
        return NULL;
    }
    
    // copy configuration
    server->config = *config;
    server->config.bind_address = serve_strdup(config->bind_address);
    server->config.ssl_cert_file = serve_strdup(config->ssl_cert_file);
    server->config.ssl_key_file = serve_strdup(config->ssl_key_file);
    server->config.document_root = serve_strdup(config->document_root);
    
    // create event base
    server->event_base = event_base_new();
    if (!server->event_base) {
        serve_set_error("failed to create event base");
        server_destroy(server);
        return NULL;
    }
    
    // create http server if port is specified
    if (server->config.port > 0) {
        server->http_server = evhttp_new(server->event_base);
        if (!server->http_server) {
            serve_set_error("failed to create http server");
            server_destroy(server);
            return NULL;
        }
        
        // set timeouts
        if (server->config.timeout_seconds > 0) {
            evhttp_set_timeout(server->http_server, server->config.timeout_seconds);
        }
        
        // note: evhttp_set_max_connections may not be available in all versions
        /*
        // set max connections
        if (server->config.max_connections > 0) {
            evhttp_set_max_connections(server->http_server, server->config.max_connections);
        }
        */
    }
    
    // create ssl context if ssl is enabled
    if (server->config.ssl_port > 0 && server->config.ssl_cert_file && 
        server->config.ssl_key_file) {
        
        tls_config_t tls_config = tls_config_default();
        tls_config.cert_file = server->config.ssl_cert_file;
        tls_config.key_file = server->config.ssl_key_file;
        
        server->ssl_ctx = tls_create_context(&tls_config);
        if (!server->ssl_ctx) {
            serve_set_error("failed to create ssl context");
            server_destroy(server);
            return NULL;
        }
        
        // create https server
        server->https_server = evhttp_new(server->event_base);
        if (!server->https_server) {
            serve_set_error("failed to create https server");
            server_destroy(server);
            return NULL;
        }
        
        // set timeouts for https server
        if (server->config.timeout_seconds > 0) {
            evhttp_set_timeout(server->https_server, server->config.timeout_seconds);
        }
        
        // note: evhttp_set_max_connections may not be available in all versions
        /*
        // set max connections for https server
        if (server->config.max_connections > 0) {
            evhttp_set_max_connections(server->https_server, server->config.max_connections);
        }
        */
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
    
    // start http server
    if (server->http_server && server->config.port > 0) {
        if (evhttp_bind_socket(server->http_server, bind_addr, 
                              server->config.port) < 0) {
            serve_set_error("failed to bind http server to %s:%d", 
                           bind_addr, server->config.port);
            return -1;
        }
        
        SERVE_LOG_INFO("http server listening on %s:%d", 
                      bind_addr, server->config.port);
    }
    
    // start https server
    if (server->https_server && server->config.ssl_port > 0) {
        if (evhttp_bind_socket(server->https_server, bind_addr, 
                              server->config.ssl_port) < 0) {
            serve_set_error("failed to bind https server to %s:%d", 
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

void server_stop(server_t *server) {
    if (!server || !server->running) {
        return;
    }
    
    // break event loop
    if (server->event_base) {
        event_base_loopbreak(server->event_base);
    }
    
    server->running = 0;
    global_server = NULL;
    
    SERVE_LOG_INFO("server stopped");
}

void server_destroy(server_t *server) {
    if (!server) {
        return;
    }
    
    server_stop(server);
    
    // free http server
    if (server->http_server) {
        evhttp_free(server->http_server);
    }
    
    // free https server
    if (server->https_server) {
        evhttp_free(server->https_server);
    }
    
    // free ssl context
    if (server->ssl_ctx) {
        tls_destroy_context(server->ssl_ctx);
    }
    
    // free event base
    if (server->event_base) {
        event_base_free(server->event_base);
    }
    
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
    
    // run event loop
    int result = event_base_dispatch(server->event_base);
    
    if (result < 0) {
        serve_set_error("event loop error");
        return -1;
    }
    
    SERVE_LOG_INFO("event loop exited");
    return 0;
}

/**
 * request handling implementation
 */

int server_set_handler(server_t *server, const char *path, 
                      request_handler_t handler, void *user_data) {
    if (!server || !path || !handler) {
        serve_set_error("invalid parameters");
        return -1;
    }
    
    // set handler for http server
    if (server->http_server) {
        evhttp_set_cb(server->http_server, path, handler, user_data);
    }
    
    // set handler for https server
    if (server->https_server) {
        evhttp_set_cb(server->https_server, path, handler, user_data);
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
    
    // set default handler for http server
    if (server->http_server) {
        evhttp_set_gencb(server->http_server, handler, user_data);
    }
    
    // set default handler for https server
    if (server->https_server) {
        evhttp_set_gencb(server->https_server, handler, user_data);
    }
    
    SERVE_LOG_DEBUG("default handler set");
    return 0;
}

/**
 * utility functions implementation
 */

server_config_t server_config_default(void) {
    server_config_t config = {0};
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
    
    // at least one port must be specified
    if (config->port <= 0 && config->ssl_port <= 0) {
        serve_set_error("at least one port (http or https) must be specified");
        return -1;
    }
    
    // validate port ranges
    if (config->port > 0 && (config->port < 1 || config->port > 65535)) {
        serve_set_error("invalid http port: %d", config->port);
        return -1;
    }
    
    if (config->ssl_port > 0 && (config->ssl_port < 1 || config->ssl_port > 65535)) {
        serve_set_error("invalid https port: %d", config->ssl_port);
        return -1;
    }
    
    // validate ssl configuration
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
    
    // validate other parameters
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
    if (!config) {
        return;
    }
    
    serve_free(config->bind_address);
    serve_free(config->ssl_cert_file);
    serve_free(config->ssl_key_file);
    serve_free(config->document_root);
    
    memset(config, 0, sizeof(server_config_t));
}

const char* server_get_error(void) {
    return serve_get_error();
}
