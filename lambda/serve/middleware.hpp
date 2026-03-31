/**
 * @file middleware.hpp
 * @brief Middleware pipeline with next() continuation
 *
 * Provides composable request/response processing pipeline.
 * Middleware functions receive a next() callback for continuation-passing.
 *
 * Compatible with:
 *   Express:  app.use(middleware)        / app.use('/api', middleware)
 *   Flask:    @app.before_request       / @app.after_request
 *   FastAPI:  @app.middleware("http")
 */

#pragma once

#include "serve_types.hpp"
#include "http_request.hpp"
#include "http_response.hpp"

// ============================================================================
// Middleware function signature
// ============================================================================

// forward declaration
struct MiddlewareContext;

// middleware function: receives request, response, and a next() to call
// the continuation. user_data is per-middleware custom data.
typedef void (*MiddlewareFn)(HttpRequest *req, HttpResponse *resp,
                             void *user_data, MiddlewareContext *ctx);

// ============================================================================
// Middleware Entry (linked list node)
// ============================================================================

struct MiddlewareEntry {
    MiddlewareFn    fn;
    void           *user_data;
    char           *path;           // NULL = global, non-NULL = path-scoped
    MiddlewareEntry *next;
};

// ============================================================================
// Middleware Context (continuation state)
// ============================================================================

struct MiddlewareContext {
    MiddlewareEntry *current;       // current position in the chain
    HttpRequest     *req;
    HttpResponse    *resp;
    void            *app_data;      // application-level context
};

// advance to the next middleware in the chain
void middleware_next(MiddlewareContext *ctx);

// ============================================================================
// Middleware Stack
// ============================================================================

struct MiddlewareStack {
    MiddlewareEntry *head;          // first middleware
    MiddlewareEntry *tail;          // last middleware (for O(1) append)
    int count;
};

// create/destroy
MiddlewareStack* middleware_stack_create(void);
void             middleware_stack_destroy(MiddlewareStack *stack);

// add middleware (appended to end of chain)
int middleware_stack_use(MiddlewareStack *stack, MiddlewareFn fn, void *user_data);
int middleware_stack_use_path(MiddlewareStack *stack, const char *path,
                             MiddlewareFn fn, void *user_data);

// execute the middleware chain for a request
void middleware_stack_run(MiddlewareStack *stack, HttpRequest *req,
                         HttpResponse *resp, void *app_data);

// ============================================================================
// Built-in Middleware Factories
// ============================================================================

// request logger: logs method, path, status, response time
MiddlewareFn middleware_logger(void);

// CORS: configurable cross-origin resource sharing
struct CorsOptions {
    const char *origin;             // allowed origin ("*" for any)
    const char *methods;            // allowed methods ("GET, POST, PUT, DELETE")
    const char *headers;            // allowed headers
    const char *expose_headers;     // headers to expose
    int max_age;                    // preflight cache seconds
    int credentials;                // allow credentials (0/1)
};

CorsOptions cors_options_default(void);
MiddlewareFn middleware_cors(void);     // uses user_data = CorsOptions*

// error handler: catches unhandled errors, returns JSON error response
MiddlewareFn middleware_error_handler(void);

// static file server: serves files from a directory
// user_data = StaticOptions*
struct StaticOptions {
    const char *root;               // directory root path
    const char *index_file;         // default file (e.g. "index.html")
    int enable_directory_listing;   // list directory contents (0/1)
    int max_age;                    // cache-control max-age seconds
};

MiddlewareFn middleware_static(void);
