/**
 * @file backend_js.cpp
 * @brief JavaScript JIT backend for web server
 *
 * Executes JavaScript handler scripts via Lambda's built-in JS runtime
 * (Tree-sitter parse → JS AST → MIR JIT). Provides Node.js/Express-style
 * handler interface: (req, res) => { ... }
 *
 * Compatible with:
 *   Express:   app.get('/', (req, res) => res.json({hello: 'world'}))
 */

#include "language_backend.hpp"
#include "serve_utils.hpp"
#include "../../lib/log.h"

#include "../lambda.h"

#include <string.h>
#include <sys/stat.h>

// ============================================================================
// JS Backend State
// ============================================================================

struct JsBackendData {
    int initialized;
};

// ============================================================================
// Backend interface
// ============================================================================

static int js_backend_init(LanguageBackend *self) {
    JsBackendData *data = (JsBackendData*)serve_calloc(1, sizeof(JsBackendData));
    if (!data) return -1;

    data->initialized = 1;
    self->backend_data = data;
    log_info("js backend initialized");
    return 0;
}

static void js_backend_shutdown(LanguageBackend *self) {
    JsBackendData *data = (JsBackendData*)self->backend_data;
    if (data) {
        serve_free(data);
        self->backend_data = NULL;
    }
    log_info("js backend shut down");
}

static int js_backend_execute(LanguageBackend *self, const char *handler_path,
                               HttpRequest *req, HttpResponse *resp) {
    JsBackendData *data = (JsBackendData*)self->backend_data;
    if (!data || !data->initialized) return BACKEND_ERROR;

    if (!serve_file_exists(handler_path)) {
        log_error("js handler not found: %s", handler_path);
        return BACKEND_NOT_FOUND;
    }

    // TODO: Phase 3 — js_runtime_exec_handler(handler_path, req, resp)
    // 1. Load .js file via Tree-sitter JS parser
    // 2. Build JS AST → compile to MIR
    // 3. Execute handler function with req/resp objects mapped to Lambda Items
    // 4. JS runtime uses libuv event loop for async operations (fetch, setTimeout)

    log_error("js backend execute not yet implemented for: %s", handler_path);
    http_response_error(resp, HTTP_501_NOT_IMPLEMENTED,
                       "JavaScript handler execution not yet implemented");
    return BACKEND_ERROR;
}

static int js_backend_compile(LanguageBackend *self, const char *handler_path) {
    (void)self;
    // TODO: Phase 3 — pre-compile JS handler to MIR
    log_info("js handler compile stub: %s", handler_path);
    return BACKEND_OK;
}

static int js_backend_needs_recompile(LanguageBackend *self, const char *handler_path) {
    (void)self;
    (void)handler_path;
    return 1; // always recompile for now
}

// ============================================================================
// Backend factory
// ============================================================================

static const char *js_extensions[] = { ".js", ".mjs" };

static LanguageBackend js_backend_instance = {
    "javascript",
    js_extensions,
    2,
    NULL,
    js_backend_init,
    js_backend_shutdown,
    js_backend_execute,
    js_backend_compile,
    js_backend_needs_recompile
};

LanguageBackend* create_js_backend(void) {
    return &js_backend_instance;
}
