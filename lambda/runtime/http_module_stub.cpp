// http_module_stub.cpp — stub implementations for io.http functions
// Used when libuv/server is not available in the build.
// All functions return an error indicating HTTP is not supported.

#include "../lambda-data.hpp"
#include "lambda-error.h"
#include "../../lib/log.h"

#define HTTP_UNAVAIL_MSG "io.http module is not available in this build"

extern "C" Item pn_io_http_create_server(Item /*config*/) {
    log_error("http_module_stub: %s", HTTP_UNAVAIL_MSG);
    return err2it_or_error(err_create_simple(ERR_IO_ERROR, HTTP_UNAVAIL_MSG));
}

extern "C" Item pn_io_http_listen(Item /*server*/, Item /*port*/) {
    log_error("http_module_stub: %s", HTTP_UNAVAIL_MSG);
    return err2it_or_error(err_create_simple(ERR_IO_ERROR, HTTP_UNAVAIL_MSG));
}

extern "C" Item pn_io_http_route(Item /*server*/, Item /*method*/, Item /*path*/, Item /*handler*/) {
    log_error("http_module_stub: %s", HTTP_UNAVAIL_MSG);
    return err2it_or_error(err_create_simple(ERR_IO_ERROR, HTTP_UNAVAIL_MSG));
}

extern "C" Item pn_io_http_use(Item /*server*/, Item /*middleware*/) {
    log_error("http_module_stub: %s", HTTP_UNAVAIL_MSG);
    return err2it_or_error(err_create_simple(ERR_IO_ERROR, HTTP_UNAVAIL_MSG));
}

extern "C" Item pn_io_http_static(Item /*server*/, Item /*url_path*/, Item /*dir_path*/) {
    log_error("http_module_stub: %s", HTTP_UNAVAIL_MSG);
    return err2it_or_error(err_create_simple(ERR_IO_ERROR, HTTP_UNAVAIL_MSG));
}

extern "C" Item pn_io_http_stop(Item /*server*/) {
    log_error("http_module_stub: %s", HTTP_UNAVAIL_MSG);
    return err2it_or_error(err_create_simple(ERR_IO_ERROR, HTTP_UNAVAIL_MSG));
}
