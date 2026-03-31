// http_module_stub.cpp — stub implementations for io.http functions
// Used when libuv/server is not available in the build.
// All functions return an error indicating HTTP is not supported.

#include "lambda-data.hpp"
#include "lambda-error.h"
#include "../lib/log.h"

#define HTTP_UNAVAIL_MSG "io.http module is not available in this build"

extern "C" RetItem pn_io_http_create_server(Item /*config*/) {
    log_error("http_module_stub: %s", HTTP_UNAVAIL_MSG);
    return ri_err(err_create_simple(ERR_IO_ERROR, HTTP_UNAVAIL_MSG));
}

extern "C" RetItem pn_io_http_listen(Item /*server*/, Item /*port*/) {
    log_error("http_module_stub: %s", HTTP_UNAVAIL_MSG);
    return ri_err(err_create_simple(ERR_IO_ERROR, HTTP_UNAVAIL_MSG));
}

extern "C" RetItem pn_io_http_route(Item /*server*/, Item /*method*/, Item /*path*/, Item /*handler*/) {
    log_error("http_module_stub: %s", HTTP_UNAVAIL_MSG);
    return ri_err(err_create_simple(ERR_IO_ERROR, HTTP_UNAVAIL_MSG));
}

extern "C" RetItem pn_io_http_use(Item /*server*/, Item /*middleware*/) {
    log_error("http_module_stub: %s", HTTP_UNAVAIL_MSG);
    return ri_err(err_create_simple(ERR_IO_ERROR, HTTP_UNAVAIL_MSG));
}

extern "C" RetItem pn_io_http_static(Item /*server*/, Item /*url_path*/, Item /*dir_path*/) {
    log_error("http_module_stub: %s", HTTP_UNAVAIL_MSG);
    return ri_err(err_create_simple(ERR_IO_ERROR, HTTP_UNAVAIL_MSG));
}

extern "C" RetItem pn_io_http_stop(Item /*server*/) {
    log_error("http_module_stub: %s", HTTP_UNAVAIL_MSG);
    return ri_err(err_create_simple(ERR_IO_ERROR, HTTP_UNAVAIL_MSG));
}

extern "C" Item pn_io_http_create_server_mir(Item config) {
    RetItem ri = pn_io_http_create_server(config);
    return ri_to_item(ri);
}

extern "C" Item pn_io_http_listen_mir(Item server, Item port) {
    RetItem ri = pn_io_http_listen(server, port);
    return ri_to_item(ri);
}

extern "C" Item pn_io_http_route_mir(Item server, Item method, Item path, Item handler) {
    RetItem ri = pn_io_http_route(server, method, path, handler);
    return ri_to_item(ri);
}

extern "C" Item pn_io_http_use_mir(Item server, Item middleware) {
    RetItem ri = pn_io_http_use(server, middleware);
    return ri_to_item(ri);
}

extern "C" Item pn_io_http_static_mir(Item server, Item url_path, Item dir_path) {
    RetItem ri = pn_io_http_static(server, url_path, dir_path);
    return ri_to_item(ri);
}

extern "C" Item pn_io_http_stop_mir(Item server) {
    RetItem ri = pn_io_http_stop(server);
    return ri_to_item(ri);
}

// Stub for webdriver command (excluded with serve module)
int cmd_webdriver(int argc, char** argv) {
    (void)argc; (void)argv;
    log_error("http_module_stub: webdriver command not available in this build");
    return 1;
}
