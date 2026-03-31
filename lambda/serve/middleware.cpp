/**
 * @file middleware.cpp
 * @brief Middleware pipeline implementation
 *
 * Continuation-passing middleware chain with path matching.
 * Each middleware calls middleware_next(ctx) to proceed, or stops the chain
 * by simply not calling next (e.g. to send an error response).
 */

#include "middleware.hpp"
#include "serve_utils.hpp"
#include "mime.hpp"
#include "../../lib/log.h"

#include <string.h>
#include <time.h>

// ============================================================================
// Middleware Stack lifecycle
// ============================================================================

MiddlewareStack* middleware_stack_create(void) {
    MiddlewareStack *stack = (MiddlewareStack*)serve_calloc(1, sizeof(MiddlewareStack));
    return stack;
}

void middleware_stack_destroy(MiddlewareStack *stack) {
    if (!stack) return;
    MiddlewareEntry *e = stack->head;
    while (e) {
        MiddlewareEntry *next = e->next;
        serve_free(e->path);
        serve_free(e);
        e = next;
    }
    serve_free(stack);
}

// ============================================================================
// Add middleware
// ============================================================================

static MiddlewareEntry* create_entry(MiddlewareFn fn, void *user_data, const char *path) {
    MiddlewareEntry *entry = (MiddlewareEntry*)serve_calloc(1, sizeof(MiddlewareEntry));
    if (!entry) return NULL;
    entry->fn = fn;
    entry->user_data = user_data;
    entry->path = path ? serve_strdup(path) : NULL;
    return entry;
}

int middleware_stack_use(MiddlewareStack *stack, MiddlewareFn fn, void *user_data) {
    if (!stack || !fn) return -1;
    MiddlewareEntry *entry = create_entry(fn, user_data, NULL);
    if (!entry) return -1;
    if (stack->tail) {
        stack->tail->next = entry;
    } else {
        stack->head = entry;
    }
    stack->tail = entry;
    stack->count++;
    return 0;
}

int middleware_stack_use_path(MiddlewareStack *stack, const char *path,
                             MiddlewareFn fn, void *user_data) {
    if (!stack || !fn || !path) return -1;
    MiddlewareEntry *entry = create_entry(fn, user_data, path);
    if (!entry) return -1;
    if (stack->tail) {
        stack->tail->next = entry;
    } else {
        stack->head = entry;
    }
    stack->tail = entry;
    stack->count++;
    return 0;
}

// ============================================================================
// Middleware chain execution
// ============================================================================

// check if a path-scoped middleware should run for the given request path
static int path_matches(const char *middleware_path, const char *request_path) {
    if (!middleware_path) return 1; // global middleware always matches
    size_t len = strlen(middleware_path);
    if (len == 0) return 1;
    // prefix match: /api matches /api, /api/, /api/users
    if (strncmp(request_path, middleware_path, len) != 0) return 0;
    // must match exactly or be followed by '/'
    char after = request_path[len];
    return (after == '\0' || after == '/');
}

void middleware_next(MiddlewareContext *ctx) {
    if (!ctx) return;

    // walk to the next matching middleware
    while (ctx->current) {
        MiddlewareEntry *entry = ctx->current;
        ctx->current = entry->next;

        if (path_matches(entry->path, ctx->req->path)) {
            entry->fn(ctx->req, ctx->resp, entry->user_data, ctx);
            return;
        }
    }
    // chain exhausted — no more middleware
}

void middleware_stack_run(MiddlewareStack *stack, HttpRequest *req,
                         HttpResponse *resp, void *app_data) {
    if (!stack || !req || !resp) return;

    MiddlewareContext ctx;
    ctx.current = stack->head;
    ctx.req = req;
    ctx.resp = resp;
    ctx.app_data = app_data;

    middleware_next(&ctx);
}

// ============================================================================
// Built-in: Logger middleware
// ============================================================================

static void logger_middleware(HttpRequest *req, HttpResponse *resp,
                              void *user_data, MiddlewareContext *ctx) {
    (void)user_data;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // proceed down the chain
    middleware_next(ctx);

    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    double ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                (end.tv_nsec - start.tv_nsec) / 1e6;

    log_info("http %s %s → %d (%.1f ms)",
             http_method_to_string(req->method), req->path,
             resp->status_code, ms);
}

MiddlewareFn middleware_logger(void) {
    return logger_middleware;
}

// ============================================================================
// Built-in: CORS middleware
// ============================================================================

CorsOptions cors_options_default(void) {
    CorsOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.origin = "*";
    opts.methods = "GET, POST, PUT, DELETE, PATCH, OPTIONS";
    opts.headers = "Content-Type, Authorization";
    opts.expose_headers = NULL;
    opts.max_age = 86400;
    opts.credentials = 0;
    return opts;
}

static void cors_middleware(HttpRequest *req, HttpResponse *resp,
                            void *user_data, MiddlewareContext *ctx) {
    CorsOptions *opts = (CorsOptions*)user_data;
    CorsOptions defaults = cors_options_default();
    if (!opts) opts = &defaults;

    http_response_set_header(resp, "Access-Control-Allow-Origin",
                             opts->origin ? opts->origin : "*");

    if (opts->credentials) {
        http_response_set_header(resp, "Access-Control-Allow-Credentials", "true");
    }

    if (opts->expose_headers) {
        http_response_set_header(resp, "Access-Control-Expose-Headers", opts->expose_headers);
    }

    // handle preflight OPTIONS request
    if (req->method == HTTP_OPTIONS) {
        http_response_set_header(resp, "Access-Control-Allow-Methods",
                                 opts->methods ? opts->methods : "GET, POST, PUT, DELETE, PATCH, OPTIONS");
        http_response_set_header(resp, "Access-Control-Allow-Headers",
                                 opts->headers ? opts->headers : "Content-Type, Authorization");
        if (opts->max_age > 0) {
            char age_str[16];
            snprintf(age_str, sizeof(age_str), "%d", opts->max_age);
            http_response_set_header(resp, "Access-Control-Max-Age", age_str);
        }
        http_response_status(resp, HTTP_204_NO_CONTENT);
        http_response_send(resp);
        return; // don't call next — preflight is complete
    }

    middleware_next(ctx);
}

MiddlewareFn middleware_cors(void) {
    return cors_middleware;
}

// ============================================================================
// Built-in: Error handler middleware
// ============================================================================

static void error_handler_middleware(HttpRequest *req, HttpResponse *resp,
                                     void *user_data, MiddlewareContext *ctx) {
    (void)user_data;

    // call the rest of the chain
    middleware_next(ctx);

    // if no status was set and response not sent, return 404
    if (!resp->headers_sent && resp->status_code == 0) {
        http_response_error(resp, HTTP_404_NOT_FOUND, "Not Found");
    }
}

MiddlewareFn middleware_error_handler(void) {
    return error_handler_middleware;
}

// ============================================================================
// Built-in: Static file server middleware
// ============================================================================

static void static_middleware(HttpRequest *req, HttpResponse *resp,
                              void *user_data, MiddlewareContext *ctx) {
    StaticOptions *opts = (StaticOptions*)user_data;
    if (!opts || !opts->root) {
        middleware_next(ctx);
        return;
    }

    // only serve GET and HEAD
    if (req->method != HTTP_GET && req->method != HTTP_HEAD) {
        middleware_next(ctx);
        return;
    }

    // build file path from document root + request path
    // prevent directory traversal: reject paths with ".."
    if (strstr(req->path, "..")) {
        middleware_next(ctx);
        return;
    }

    char filepath[4096];
    snprintf(filepath, sizeof(filepath), "%s%s", opts->root, req->path);

    // check if path is a directory and try index file
    size_t flen = strlen(filepath);
    if (flen > 0 && filepath[flen - 1] == '/') {
        const char *idx = opts->index_file ? opts->index_file : "index.html";
        snprintf(filepath + flen, sizeof(filepath) - flen, "%s", idx);
    }

    if (!serve_file_exists(filepath)) {
        middleware_next(ctx);
        return;
    }

    // detect content type
    const char *ext = serve_get_file_extension(filepath);
    const char *content_type = CONTENT_TYPE_OCTET;
    if (ext) {
        const char *detected = mime_detect(NULL, filepath, NULL, 0);
        if (detected) content_type = detected;
    }

    http_response_set_header(resp, "Content-Type", content_type);

    // cache control
    if (opts->max_age > 0) {
        char cache_str[64];
        snprintf(cache_str, sizeof(cache_str), "public, max-age=%d", opts->max_age);
        http_response_set_header(resp, "Cache-Control", cache_str);
    }

    http_response_file(resp, filepath, content_type);
}

MiddlewareFn middleware_static(void) {
    return static_middleware;
}
