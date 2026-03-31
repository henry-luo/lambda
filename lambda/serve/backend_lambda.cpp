/**
 * @file backend_lambda.cpp
 * @brief Lambda Script JIT backend for web server
 * STUBBED OUT: API migration in progress.
 */

#include "language_backend.hpp"
#include "serve_utils.hpp"
#include "../../lib/log.h"

#include <string.h>

// ============================================================================
// Stub backend that returns errors for all operations
// ============================================================================

static int lambda_backend_init(LanguageBackend *self) {
    (void)self;
    log_info("lambda backend init (stubbed)");
    return 0;
}

static void lambda_backend_shutdown(LanguageBackend *self) {
    (void)self;
}

static int lambda_backend_execute(LanguageBackend *self, const char *handler_path,
                                   HttpRequest *req, HttpResponse *resp) {
    (void)self; (void)handler_path; (void)req;
    log_error("lambda backend execute: not available (stubbed)");
    http_response_error(resp, HTTP_500_INTERNAL_ERROR, "Lambda backend not available");
    return BACKEND_ERROR;
}

static int lambda_backend_compile(LanguageBackend *self, const char *handler_path) {
    (void)self; (void)handler_path;
    return BACKEND_ERROR;
}

static LanguageBackend lambda_backend_instance;

LanguageBackend* create_lambda_backend(void) {
    lambda_backend_instance = {};
    lambda_backend_instance.name = "lambda";
    lambda_backend_instance.init = lambda_backend_init;
    lambda_backend_instance.shutdown = lambda_backend_shutdown;
    lambda_backend_instance.execute = lambda_backend_execute;
    lambda_backend_instance.compile = lambda_backend_compile;
    return &lambda_backend_instance;
}

