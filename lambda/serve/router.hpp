/**
 * @file router.hpp
 * @brief Trie-based HTTP router with parameterized path matching
 *
 * Supports:
 *   - Exact paths:       /api/health
 *   - Named params:      /users/:id          → params["id"] = "42"
 *   - Optional params:   /users/:id?         → matches /users and /users/42
 *   - Wildcard (catch-all): /files/*path     → params["path"] = "a/b/c"
 *   - Method-specific:   GET /users vs POST /users
 *   - Sub-routers with shared prefix (mount)
 *
 * Compatible with:
 *   Express:  app.get('/users/:id', handler)
 *   Flask:    @app.route('/users/<int:id>')
 *   FastAPI:  @app.get('/users/{user_id}')
 */

#pragma once

#include "serve_types.hpp"
#include "http_request.hpp"
#include "http_response.hpp"

// ============================================================================
// Handler function type
// ============================================================================

// request handler: receives request + response + user_data
typedef void (*RequestHandler)(HttpRequest *req, HttpResponse *resp, void *user_data);

// ============================================================================
// Route Node (internal trie structure)
// ============================================================================

struct RouteNode {
    char *segment;              // static segment text, or param name (without : or *)
    int is_param;               // 1 for :name parameters
    int is_wildcard;            // 1 for *name catch-all
    int is_optional;            // 1 for :name? optional parameters

    // handlers indexed by method (bit position in HttpMethod enum)
    // index 0=GET, 1=POST, 2=PUT, 3=DELETE, 4=HEAD, 5=OPTIONS, 6=PATCH
    RequestHandler handlers[9];
    void *handler_data[9];

    RouteNode *children;        // first child
    RouteNode *next;            // next sibling
};

// ============================================================================
// Router
// ============================================================================

struct Router {
    char *prefix;               // mount prefix (e.g. "/api/v1")
    RouteNode *root;            // trie root node
};

// create/destroy router
Router* router_create(const char *prefix);
void    router_destroy(Router *router);

// add a route. pattern uses Express-style syntax: "/users/:id", "/files/*path"
int router_add(Router *router, HttpMethod method, const char *pattern,
               RequestHandler handler, void *user_data);

// convenience: add routes for specific methods
int router_get(Router *router, const char *pattern, RequestHandler handler, void *data);
int router_post(Router *router, const char *pattern, RequestHandler handler, void *data);
int router_put(Router *router, const char *pattern, RequestHandler handler, void *data);
int router_del(Router *router, const char *pattern, RequestHandler handler, void *data);
int router_patch(Router *router, const char *pattern, RequestHandler handler, void *data);
int router_options(Router *router, const char *pattern, RequestHandler handler, void *data);
int router_all(Router *router, const char *pattern, RequestHandler handler, void *data);

// match a request path + method against the router. populates req->route_params.
// returns the matching handler, or NULL if no match.
RequestHandler router_match(Router *router, HttpMethod method, const char *path,
                            HttpHeader **out_params, void **out_user_data);

// mount a sub-router under this router's prefix
int router_mount(Router *parent, Router *child);
