/**
 * @file http_module.cpp
 * @brief Lambda io.http module — bridges C+ server API to Lambda Items
 *
 * Provides system functions for the io.http module:
 *   io.http.create_server(config?)     → creates a Server handle
 *   io.http.listen(server, port)       → starts listening
 *   io.http.route(server, method, path, handler)
 *   io.http.use(server, middleware)
 *   io.http.stop(server)               → graceful shutdown
 *   io.http.static(server, path, dir)  → serve static files
 *
 * Each function converts between Lambda Items and C+ server structs.
 *
 * Compatible with Lambda script syntax:
 *   import io.http
 *   let srv = io.http.create_server({port: 3000})
 *   io.http.route(srv, "GET", "/", fn(req) => {status: 200, body: "Hello"})
 *   io.http.listen(srv, 3000)
 */

#include "serve/server.hpp"
#include "serve/language_backend.hpp"
#include "serve/serve_utils.hpp"
#include "lambda.hpp"
#include "../lib/log.h"

#include <string.h>

// ============================================================================
// Active server tracking (for cleanup)
// ============================================================================

#define MAX_SERVERS 8
static Server* active_servers[MAX_SERVERS];
static int active_server_count = 0;

// ============================================================================
// io.http.create_server(config?) → server handle (Item)
// ============================================================================

extern "C" RetItem pn_io_http_create_server(Item config_item) {
    ServerConfig config = server_config_default();

    // if config is a map, extract settings
    if (get_type_id(config_item) == LMD_TYPE_MAP) {
        Item port_item = item_attr(config_item, "port");
        if (get_type_id(port_item) == LMD_TYPE_INT) {
            config.port = it2i(port_item);
        }

        Item bind_item = item_attr(config_item, "bind");
        if (get_type_id(bind_item) == LMD_TYPE_STRING) {
            config.bind_address = serve_strdup(it2s(bind_item)->chars);
        }

        Item timeout_item = item_attr(config_item, "timeout");
        if (get_type_id(timeout_item) == LMD_TYPE_INT) {
            config.timeout_seconds = it2i(timeout_item);
        }

        Item max_conn_item = item_attr(config_item, "max_connections");
        if (get_type_id(max_conn_item) == LMD_TYPE_INT) {
            config.max_connections = it2i(max_conn_item);
        }

        Item docroot_item = item_attr(config_item, "document_root");
        if (get_type_id(docroot_item) == LMD_TYPE_STRING) {
            config.document_root = serve_strdup(it2s(docroot_item)->chars);
        }
    }

    Server *server = server_create(&config);
    if (!server) {
        log_error("io.http.create_server failed");
        return item_to_ri(ItemError);
    }

    // register built-in backends
    BackendRegistry *backends = backend_registry_create();
    // Lambda backend is always registered
    extern LanguageBackend* create_lambda_backend(void);
    backend_registry_add(backends, create_lambda_backend());
    backend_registry_init_all(backends);
    server_set_backends(server, backends);

    // add default middleware: logger + error handler
    server_use(server, middleware_logger(), NULL);
    server_use(server, middleware_error_handler(), NULL);

    // track for cleanup
    if (active_server_count < MAX_SERVERS) {
        active_servers[active_server_count++] = server;
    }

    log_info("io.http.create_server: port=%d", config.port);

    // wrap server pointer as an opaque Item (using int64 to store pointer)
    return ri_ok(p2it(server));
}

// ============================================================================
// io.http.listen(server, port) → null
// ============================================================================

extern "C" RetItem pn_io_http_listen(Item server_item, Item port_item) {
    if (get_type_id(server_item) != LMD_TYPE_RAW_POINTER) {
        log_error("io.http.listen: first argument must be a server handle");
        return item_to_ri(ItemError);
    }

    Server *server = (Server*)it2p(server_item);
    int port = 3000;

    if (get_type_id(port_item) == LMD_TYPE_INT) {
        port = it2i(port_item);
    }

    int r = server_start(server, port);
    if (r != 0) {
        log_error("io.http.listen failed on port %d", port);
        return item_to_ri(ItemError);
    }

    // run the event loop (blocks until server stops)
    server_run(server);
    return ri_ok(ItemNull);
}

// ============================================================================
// io.http.route(server, method, path, handler) → null
// ============================================================================

// handler wrapper: calls Lambda function with request map
struct LambdaHandlerWrapper {
    Item handler_func;  // Lambda function Item
};

static void lambda_handler_bridge(HttpRequest *req, HttpResponse *resp, void *user_data) {
    LambdaHandlerWrapper *wrapper = (LambdaHandlerWrapper*)user_data;
    if (!wrapper) return;

    // TODO: Phase 5 — convert HttpRequest to Lambda Map, call function, 
    //       convert result back to HttpResponse
    // For now, this is a stub that demonstrates the bridge pattern
    (void)req;
    (void)wrapper;

    http_response_error(resp, HTTP_501_NOT_IMPLEMENTED,
                       "Lambda handler bridge not yet implemented");
}

extern "C" RetItem pn_io_http_route(Item server_item, Item method_item,
                                     Item path_item, Item handler_item) {
    if (get_type_id(server_item) != LMD_TYPE_RAW_POINTER) {
        log_error("io.http.route: first argument must be a server handle");
        return item_to_ri(ItemError);
    }

    Server *server = (Server*)it2p(server_item);

    // parse method string
    HttpMethod method = HTTP_GET;
    if (get_type_id(method_item) == LMD_TYPE_STRING) {
        method = http_method_from_string(it2s(method_item)->chars);
    }

    // parse path string
    const char *path = "/";
    if (get_type_id(path_item) == LMD_TYPE_STRING) {
        path = it2s(path_item)->chars;
    }

    // wrap Lambda function
    LambdaHandlerWrapper *wrapper = (LambdaHandlerWrapper*)serve_calloc(1, sizeof(LambdaHandlerWrapper));
    wrapper->handler_func = handler_item;

    server_route(server, method, path, lambda_handler_bridge, wrapper);
    return ri_ok(ItemNull);
}

// ============================================================================
// io.http.use(server, middleware_fn) → null
// ============================================================================

extern "C" RetItem pn_io_http_use(Item server_item, Item middleware_item) {
    if (get_type_id(server_item) != LMD_TYPE_RAW_POINTER) {
        log_error("io.http.use: first argument must be a server handle");
        return item_to_ri(ItemError);
    }

    Server *server = (Server*)it2p(server_item);

    // built-in middleware by name
    if (get_type_id(middleware_item) == LMD_TYPE_STRING) {
        const char *name = it2s(middleware_item)->chars;
        if (strcmp(name, "logger") == 0) {
            server_use(server, middleware_logger(), NULL);
        } else if (strcmp(name, "cors") == 0) {
            CorsOptions *opts = (CorsOptions*)serve_calloc(1, sizeof(CorsOptions));
            *opts = cors_options_default();
            server_use(server, middleware_cors(), opts);
        } else if (strcmp(name, "error_handler") == 0) {
            server_use(server, middleware_error_handler(), NULL);
        } else {
            log_error("io.http.use: unknown middleware '%s'", name);
            return item_to_ri(ItemError);
        }
        return ri_ok(ItemNull);
    }

    // TODO: Phase 5 — Lambda function as custom middleware
    log_error("io.http.use: custom Lambda middleware not yet implemented");
    return item_to_ri(ItemError);
}

// ============================================================================
// io.http.static(server, url_path, dir_path) → null
// ============================================================================

extern "C" RetItem pn_io_http_static(Item server_item, Item url_path_item, Item dir_path_item) {
    if (get_type_id(server_item) != LMD_TYPE_RAW_POINTER) {
        return item_to_ri(ItemError);
    }

    Server *server = (Server*)it2p(server_item);
    const char *url_path = "/";
    const char *dir_path = ".";

    if (get_type_id(url_path_item) == LMD_TYPE_STRING) {
        url_path = it2s(url_path_item)->chars;
    }
    if (get_type_id(dir_path_item) == LMD_TYPE_STRING) {
        dir_path = it2s(dir_path_item)->chars;
    }

    server_set_static(server, url_path, dir_path);
    return ri_ok(ItemNull);
}

// ============================================================================
// io.http.stop(server) → null
// ============================================================================

extern "C" RetItem pn_io_http_stop(Item server_item) {
    if (get_type_id(server_item) != LMD_TYPE_RAW_POINTER) {
        return item_to_ri(ItemError);
    }

    Server *server = (Server*)it2p(server_item);
    server_stop(server);
    return ri_ok(ItemNull);
}

// ============================================================================
// MIR wrappers (convert RetItem → Item for JIT ABI)
// ============================================================================

extern "C" Item pn_io_http_create_server_mir(Item config) {
    RetItem ri = pn_io_http_create_server(config);
    return ri.err ? ItemError : ri.value;
}

extern "C" Item pn_io_http_listen_mir(Item server, Item port) {
    RetItem ri = pn_io_http_listen(server, port);
    return ri.err ? ItemError : ri.value;
}

extern "C" Item pn_io_http_route_mir(Item server, Item method, Item path, Item handler) {
    RetItem ri = pn_io_http_route(server, method, path, handler);
    return ri.err ? ItemError : ri.value;
}

extern "C" Item pn_io_http_use_mir(Item server, Item middleware) {
    RetItem ri = pn_io_http_use(server, middleware);
    return ri.err ? ItemError : ri.value;
}

extern "C" Item pn_io_http_static_mir(Item server, Item url_path, Item dir_path) {
    RetItem ri = pn_io_http_static(server, url_path, dir_path);
    return ri.err ? ItemError : ri.value;
}

extern "C" Item pn_io_http_stop_mir(Item server) {
    RetItem ri = pn_io_http_stop(server);
    return ri.err ? ItemError : ri.value;
}

// ============================================================================
// Module cleanup (called at runtime shutdown)
// ============================================================================

void http_module_cleanup(void) {
    for (int i = 0; i < active_server_count; i++) {
        if (active_servers[i]) {
            server_destroy(active_servers[i]);
            active_servers[i] = NULL;
        }
    }
    active_server_count = 0;
}
