/**
 * @file rest.hpp
 * @brief REST resource registration with schema metadata for OpenAPI generation
 */

#ifndef LAMBDA_SERVE_REST_HPP
#define LAMBDA_SERVE_REST_HPP

#include "server.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// REST Endpoint Metadata
// ============================================================================

typedef struct RestEndpoint {
    const char  *summary;           // short description
    const char  *description;       // long description (Markdown)
    const char  *request_schema;    // JSON Schema string for request body (NULL = none)
    const char  *response_schema;   // JSON Schema string for response body
    const char  *tags;              // comma-separated tag names
    int          deprecated;        // 1 if endpoint is deprecated
} RestEndpoint;

// ============================================================================
// REST Resource — a group of CRUD endpoints under one base path
// ============================================================================

typedef struct RestResource {
    const char  *name;              // resource name (e.g., "users")
    const char  *base_path;         // URL prefix (e.g., "/api/v1/users")
    const char  *tag;               // OpenAPI tag for grouping

    // CRUD handlers (NULL = not supported)
    RequestHandler  list_handler;       // GET    /base_path
    RequestHandler  create_handler;     // POST   /base_path
    RequestHandler  get_handler;        // GET    /base_path/:id
    RequestHandler  update_handler;     // PUT    /base_path/:id
    RequestHandler  patch_handler;      // PATCH  /base_path/:id
    RequestHandler  delete_handler;     // DELETE /base_path/:id
    void           *handler_data;       // shared user_data for all handlers

    // endpoint metadata (zeroed = use defaults)
    RestEndpoint    list_meta;
    RestEndpoint    create_meta;
    RestEndpoint    get_meta;
    RestEndpoint    update_meta;
    RestEndpoint    patch_meta;
    RestEndpoint    delete_meta;
} RestResource;

// ============================================================================
// Registered Endpoint Record (used by OpenAPI generator)
// ============================================================================

typedef struct RegisteredEndpoint {
    HttpMethod      method;
    const char     *pattern;
    RestEndpoint    meta;
} RegisteredEndpoint;

#define MAX_REGISTERED_ENDPOINTS 256

typedef struct EndpointRegistry {
    RegisteredEndpoint  endpoints[MAX_REGISTERED_ENDPOINTS];
    int                 count;
} EndpointRegistry;

// ============================================================================
// API
// ============================================================================

/**
 * Create an endpoint registry (stores metadata for OpenAPI generation).
 * Caller should free with rest_registry_destroy().
 */
EndpointRegistry* rest_registry_create(void);

/**
 * Destroy an endpoint registry.
 */
void rest_registry_destroy(EndpointRegistry *reg);

/**
 * Register a REST resource — registers CRUD routes and records metadata.
 * If registry is non-NULL, endpoint metadata is stored for OpenAPI generation.
 * Returns 0 on success, -1 on error.
 */
int server_rest_resource(Server *server, const RestResource *resource,
                         EndpointRegistry *registry);

/**
 * Register a single documented endpoint (for non-CRUD routes).
 * If registry is non-NULL, endpoint metadata is stored for OpenAPI generation.
 * Returns 0 on success, -1 on error.
 */
int server_rest_endpoint(Server *server, HttpMethod method, const char *pattern,
                         RequestHandler handler, void *user_data,
                         const RestEndpoint *meta, EndpointRegistry *registry);

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_SERVE_REST_HPP
