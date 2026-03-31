// openapi/server.ls — HTTP server integration for OpenAPI support
//
// Provides route handler functions that the Lambda HTTP server can call
// to serve OpenAPI documentation and validate requests.
//
// The server calls these functions to:
//   - Serve /openapi.json (the spec in JSON format)
//   - Serve /docs (Swagger UI page)
//   - Validate incoming request bodies against the spec
//
// Usage from C++ server (via language backend):
//   1. Server loads the user's OpenAPI spec file at startup
//   2. For each incoming request, server calls handle_request()
//   3. handle_request() returns {status, headers, body} map

import openapi: .openapi
import util: .util

// ============================================================
// Public API — called by the HTTP server
// ============================================================

// Initialize the OpenAPI module with a spec file path.
// Returns the parsed spec and precomputed assets.
//
// spec_path: path to the openapi.yaml or openapi.json file
// base_path: URL prefix for docs (default: "")
// returns: context map with {spec, docs_html, spec_json, routes}
pub fn init(spec_path, base_path) {
    let spec = openapi.load_spec(spec_path);
    let prefix = if (base_path != null) base_path else "";
    let spec_url = prefix ++ "/openapi.json";
    {
        spec: spec,
        docs_html: openapi.docs_html(spec, spec_url),
        spec_json: openapi.spec_json(spec),
        routes: openapi.routes(spec),
        base_path: prefix
    }
}

// Handle an OpenAPI-related request.
// Returns a response map or null if the path is not an OpenAPI endpoint.
//
// ctx: context map from init()
// path: request path (e.g. "/docs", "/openapi.json")
// method: HTTP method
// body: parsed request body (or null)
// params: map of query/path parameters
// returns: {status: int, headers: {}, body: string} or null
pub fn handle_request(ctx, path, method) {
    let prefix = ctx.base_path;

    if (path == prefix ++ "/openapi.json" and method == "get")
        {
            status: 200,
            headers: {"Content-Type": "application/json"},
            body: ctx.spec_json
        }

    else if (path == prefix ++ "/docs" and method == "get")
        {
            status: 200,
            headers: {"Content-Type": "text/html"},
            body: ctx.docs_html
        }

    else null
}

// Validate a request body for a given route.
// Call this before processing the user's route handler.
//
// ctx: context map from init()
// path: API path (e.g. "/pets")
// method: HTTP method (e.g. "post")
// body: parsed request body
// returns: {valid: bool, errors: [...]}
pub fn check_request(ctx, path, method, body) =>
    openapi.validate_request(ctx.spec, path, method, body)

// Validate a response body before sending it.
//
// ctx: context map from init()
// path: API path
// method: HTTP method
// status: status code string (e.g. "200")
// body: response body
// returns: {valid: bool, errors: [...]}
pub fn check_response(ctx, path, method, status, body) =>
    openapi.validate_response(ctx.spec, path, method, status, body)

// Validate request parameters.
//
// ctx: context map from init()
// path: API path
// method: HTTP method
// params: map of parameter name -> value
// returns: {valid: bool, errors: [...]}
pub fn check_params(ctx, path, method, params) =>
    openapi.validate_params(ctx.spec, path, method, params)

// Build a 400 error response from validation errors.
//
// result: validation result from check_request/check_params
// returns: {status: 400, headers: {...}, body: JSON string}
pub fn error_response(result) {
    let body = format({
        error: "Validation failed",
        details: result.errors
    }, 'json');
    {
        status: 400,
        headers: {"Content-Type": "application/json"},
        body: body
    }
}

// Build a 500 error response from response validation errors.
//
// result: validation result from check_response
// returns: {status: 500, headers: {...}, body: JSON string}
pub fn server_error_response(result) {
    let body = format({
        error: "Response validation failed",
        details: result.errors
    }, 'json');
    {
        status: 500,
        headers: {"Content-Type": "application/json"},
        body: body
    }
}
