/**
 * @file openapi.hpp
 * @brief OpenAPI 3.1 specification generator
 *
 * Collects metadata from REST resources and documented endpoints,
 * then generates a live OpenAPI 3.1 JSON specification.
 */

#ifndef LAMBDA_SERVE_OPENAPI_HPP
#define LAMBDA_SERVE_OPENAPI_HPP

#include "rest.hpp"
#include "server.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// OpenAPI Configuration
// ============================================================================

typedef struct OpenApiInfo {
    const char *title;              // API title
    const char *version;            // API version (e.g., "1.0.0")
    const char *description;        // API description (Markdown)
    const char *terms_of_service;
    const char *contact_name;
    const char *contact_email;
    const char *license_name;
    const char *license_url;
} OpenApiInfo;

typedef struct OpenApiConfig {
    OpenApiInfo info;
    const char *base_url;               // server base URL (e.g., "http://localhost:3000")
    int         include_internal;       // 1 to include internal endpoints
} OpenApiConfig;

// ============================================================================
// OpenAPI Context
// ============================================================================

typedef struct OpenApiContext {
    Server          *server;
    OpenApiConfig    config;
    EndpointRegistry *registry;     // borrowed, not owned
    char            *cached_spec;   // cached JSON spec (invalidated on changes)
} OpenApiContext;

// ============================================================================
// API
// ============================================================================

/**
 * Create an OpenAPI context attached to a server.
 * The registry should be the same one used with server_rest_resource/endpoint.
 */
OpenApiContext* openapi_create(Server *server, const OpenApiConfig *config,
                               EndpointRegistry *registry);

/**
 * Destroy OpenAPI context and free cached spec.
 */
void openapi_destroy(OpenApiContext *ctx);

/**
 * Generate the OpenAPI 3.1 JSON specification string.
 * Returns a cached copy if available. Caller must NOT free the returned string.
 */
const char* openapi_generate_spec(OpenApiContext *ctx);

/**
 * Invalidate cached spec (call after registering new endpoints).
 */
void openapi_invalidate(OpenApiContext *ctx);

/**
 * Register built-in routes:
 *   GET /openapi.json  — live specification
 *   GET /docs          — Swagger UI (if swagger_ui module is available)
 */
int openapi_serve(OpenApiContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_SERVE_OPENAPI_HPP
