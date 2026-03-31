/**
 * @file swagger_ui.hpp
 * @brief Built-in Swagger UI handler (serves embedded HTML/JS)
 */

#ifndef LAMBDA_SERVE_SWAGGER_UI_HPP
#define LAMBDA_SERVE_SWAGGER_UI_HPP

#include "server.hpp"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register a GET route that serves Swagger UI at the given path.
 *   docs_path   — URL path for the UI (e.g., "/docs")
 *   spec_url    — URL for the OpenAPI spec (e.g., "/openapi.json")
 *
 * Returns 0 on success, -1 on error.
 */
int swagger_ui_serve(Server *server, const char *docs_path, const char *spec_url);

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_SERVE_SWAGGER_UI_HPP
