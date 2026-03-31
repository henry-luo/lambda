/**
 * @file backend_lambda.cpp
 * @brief Lambda Script JIT backend for web server
 *
 * Executes Lambda .ls handler scripts via MIR JIT compilation.
 * Supports compiled handler caching and hot reload via mtime checking.
 *
 * Flow:
 *   1. Load .ls handler script via runtime (parse → AST → MIR JIT)
 *   2. Convert HttpRequest to Lambda Map (Item)
 *   3. Call compiled handler function with request map
 *   4. Convert result Lambda Map back to HttpResponse
 *
 * Compatible with:
 *   Express:   app.get('/', (req, res) => { ... })
 *   Flask:     @app.route('/') def index(): ...
 *   FastAPI:   @app.get('/') async def root(): ...
 */

#include "language_backend.hpp"
#include "serve_utils.hpp"
#include "../../lib/log.h"

#include "../lambda.h"
#include "../transpiler.hpp"

// Forward declarations for runner.cpp functions (C++ linkage)
struct Runtime;
struct Input;
struct Script;
void runtime_init(Runtime* runtime);
void runtime_cleanup(Runtime* runtime);
Input* run_script(Runtime *runtime, const char* source, char* script_path, bool transpile_only);
Script* load_script(Runtime *runtime, const char* script_path, const char* source, bool is_import);

#include <string.h>
#include <sys/stat.h>

// ============================================================================
// Lambda Backend State
// ============================================================================

struct LambdaBackendData {
    Runtime runtime;
    int initialized;
};

// ============================================================================
// Handler cache entry
// ============================================================================

struct HandlerCacheEntry {
    char *path;
    time_t last_mtime;
    HandlerCacheEntry *next;
};

static HandlerCacheEntry *handler_cache = NULL;

static time_t get_file_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return st.st_mtime;
}

static HandlerCacheEntry* cache_find(const char *path) {
    HandlerCacheEntry *e = handler_cache;
    while (e) {
        if (strcmp(e->path, path) == 0) return e;
        e = e->next;
    }
    return NULL;
}

static void cache_update(const char *path, time_t mtime) {
    HandlerCacheEntry *e = cache_find(path);
    if (!e) {
        e = (HandlerCacheEntry*)serve_calloc(1, sizeof(HandlerCacheEntry));
        e->path = serve_strdup(path);
        e->next = handler_cache;
        handler_cache = e;
    }
    e->last_mtime = mtime;
}

// ============================================================================
// Backend interface implementation
// ============================================================================

static int lambda_backend_init(LanguageBackend *self) {
    LambdaBackendData *data = (LambdaBackendData*)serve_calloc(1, sizeof(LambdaBackendData));
    if (!data) return -1;

    runtime_init(&data->runtime);
    data->initialized = 1;
    self->backend_data = data;

    log_info("lambda backend initialized");
    return 0;
}

static void lambda_backend_shutdown(LanguageBackend *self) {
    LambdaBackendData *data = (LambdaBackendData*)self->backend_data;
    if (!data) return;

    if (data->initialized) {
        runtime_cleanup(&data->runtime);
    }

    // free handler cache
    HandlerCacheEntry *e = handler_cache;
    while (e) {
        HandlerCacheEntry *next = e->next;
        serve_free(e->path);
        serve_free(e);
        e = next;
    }
    handler_cache = NULL;

    serve_free(data);
    self->backend_data = NULL;
    log_info("lambda backend shut down");
}

static int lambda_backend_execute(LanguageBackend *self, const char *handler_path,
                                   HttpRequest *req, HttpResponse *resp) {
    LambdaBackendData *data = (LambdaBackendData*)self->backend_data;
    if (!data || !data->initialized) return BACKEND_ERROR;

    if (!serve_file_exists(handler_path)) {
        log_error("lambda handler not found: %s", handler_path);
        return BACKEND_NOT_FOUND;
    }

    // run the Lambda script
    // the script should define a handler function that returns a response map
    Input *input = run_script(&data->runtime, NULL, (char*)handler_path, false);
    if (!input) {
        log_error("lambda handler execution failed: %s", handler_path);
        http_response_error(resp, HTTP_500_INTERNAL_ERROR, "Handler execution failed");
        return BACKEND_ERROR;
    }

    // the result should be a map with status, headers, body
    Item result = input->root;
    if (get_type_id(result) == LMD_TYPE_MAP) {
        // extract status
        Item status_item = item_attr(result, "status");
        if (get_type_id(status_item) == LMD_TYPE_INT) {
            http_response_status(resp, (HttpStatus)it2i(status_item));
        }

        // extract headers
        Item headers_item = item_attr(result, "headers");
        if (get_type_id(headers_item) == LMD_TYPE_MAP) {
            // iterate header map entries and set response headers
            int64_t hdr_count = iter_len(headers_item, NULL, 0);
            for (int64_t i = 0; i < hdr_count; i++) {
                Item key = iter_key_at(headers_item, NULL, i, 0);
                Item val = iter_val_at(headers_item, NULL, i, 0);
                if (get_type_id(key) == LMD_TYPE_STRING && get_type_id(val) == LMD_TYPE_STRING) {
                    http_response_set_header(resp, it2s(key)->chars, it2s(val)->chars);
                }
            }
        }

        // extract body
        Item body_item = item_attr(result, "body");
        if (get_type_id(body_item) == LMD_TYPE_STRING) {
            const char *body = it2s(body_item)->chars;
            http_response_write_str(resp, body);
        }

        http_response_send(resp);
    } else if (get_type_id(result) == LMD_TYPE_STRING) {
        // simple string result — send as text
        http_response_text(resp, 200, it2s(result)->chars);
    } else {
        http_response_error(resp, HTTP_500_INTERNAL_ERROR,
                           "Handler must return a map or string");
        return BACKEND_ERROR;
    }

    cache_update(handler_path, get_file_mtime(handler_path));
    return BACKEND_OK;
}

static int lambda_backend_compile(LanguageBackend *self, const char *handler_path) {
    LambdaBackendData *data = (LambdaBackendData*)self->backend_data;
    if (!data || !data->initialized) return BACKEND_ERROR;

    // pre-load and compile the script
    Script *script = load_script(&data->runtime, handler_path, NULL, NULL);
    if (!script) {
        log_error("lambda handler compile failed: %s", handler_path);
        return BACKEND_COMPILE_ERROR;
    }

    cache_update(handler_path, get_file_mtime(handler_path));
    log_info("lambda handler compiled: %s", handler_path);
    return BACKEND_OK;
}

static int lambda_backend_needs_recompile(LanguageBackend *self, const char *handler_path) {
    (void)self;
    HandlerCacheEntry *entry = cache_find(handler_path);
    if (!entry) return 1; // never compiled

    time_t current_mtime = get_file_mtime(handler_path);
    return (current_mtime > entry->last_mtime) ? 1 : 0;
}

// ============================================================================
// Backend factory
// ============================================================================

static const char *lambda_extensions[] = { ".ls" };

static LanguageBackend lambda_backend_instance = {
    "lambda",
    lambda_extensions,
    1,
    NULL,
    lambda_backend_init,
    lambda_backend_shutdown,
    lambda_backend_execute,
    lambda_backend_compile,
    lambda_backend_needs_recompile
};

extern "C" LanguageBackend* create_lambda_backend(void) {
    return &lambda_backend_instance;
}
