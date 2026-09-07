/**
 * @file http_module.cpp
 * @brief Lambda io.http module — bridges C+ server API to Lambda Items
 *
 * STUBBED OUT: API migration in progress. Functions return error stubs.
 */

#include "../lambda.hpp"
#include "../../lib/log.h"

// ============================================================================
// Stubbed io.http functions (API migration pending)
// ============================================================================

extern "C" Item pn_io_http_create_server(Item config_item) {
    (void)config_item;
    log_error("io.http.create_server: not available (stubbed)");
    return ItemError;
}

extern "C" Item pn_io_http_listen(Item server_item, Item port_item) {
    (void)server_item; (void)port_item;
    log_error("io.http.listen: not available (stubbed)");
    return ItemError;
}

extern "C" Item pn_io_http_route(Item server_item, Item method_item,
                                     Item path_item, Item handler_item) {
    (void)server_item; (void)method_item; (void)path_item; (void)handler_item;
    log_error("io.http.route: not available (stubbed)");
    return ItemError;
}

extern "C" Item pn_io_http_use(Item server_item, Item middleware_item) {
    (void)server_item; (void)middleware_item;
    log_error("io.http.use: not available (stubbed)");
    return ItemError;
}

extern "C" Item pn_io_http_static(Item server_item, Item url_path_item, Item dir_path_item) {
    (void)server_item; (void)url_path_item; (void)dir_path_item;
    log_error("io.http.static: not available (stubbed)");
    return ItemError;
}

extern "C" Item pn_io_http_stop(Item server_item) {
    (void)server_item;
    log_error("io.http.stop: not available (stubbed)");
    return ItemError;
}
