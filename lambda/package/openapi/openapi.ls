// openapi/openapi.ls — Main entry point for the Lambda OpenAPI package
//
// Provides the public API for:
//   1. Loading and parsing OpenAPI YAML/JSON specs
//   2. Converting OpenAPI schemas to Lambda type definitions
//   3. Validating HTTP requests/responses against the spec
//   4. Generating Swagger UI documentation pages
//   5. Listing route metadata for server integration
//
// Usage:
//   import openapi: package.openapi.openapi
//
//   let spec = openapi.load_spec(@./api.yaml)
//   let html = openapi.docs_html(spec)
//   let result = openapi.validate_request(spec, "/pets", "post", body)

import schema: .schema
import docs: .docs
import val: .validate
import util: .util

// ============================================================
// Spec loading
// ============================================================

// Load and parse an OpenAPI spec from a YAML or JSON file.
//
// path: file path to the spec (e.g. @./openapi.yaml)
// returns: parsed OpenAPI spec map
pub fn load_spec(path) => input(path)

// Parse an OpenAPI spec from a raw string.
//
// source: YAML or JSON string
// fmt: format hint — 'yaml' or 'json'
// returns: parsed OpenAPI spec map
pub fn parse_spec(source, fmt) => parse(source, fmt)

// ============================================================
// Schema conversion
// ============================================================

// Convert all component schemas from a spec to Lambda type definitions.
//
// spec: parsed OpenAPI spec map
// returns: string with Lambda type definition syntax
pub fn to_lambda_schema(spec) => schema.spec_to_schema(spec)

// Convert a single component schema by name to a Lambda type definition.
//
// spec: parsed OpenAPI spec
// type_name: name of the schema in components.schemas
// returns: string with "type TypeName = ..." definition, or null if not found
pub fn schema_for(spec, type_name) {
    let schema_obj = spec.components.schemas[type_name];
    if (schema_obj == null) null
    else schema.convert_type(type_name, schema_obj, spec.components.schemas)
}

// ============================================================
// Request/response validation
// ============================================================

// Validate a request body.
//
// spec: parsed OpenAPI spec
// path: route path, e.g. "/pets"
// method: HTTP method, e.g. "post"
// body: parsed request body
// returns: {valid: bool, errors: [...]}
pub fn validate_request(spec, path, method, body) =>
    val.validate_request(spec, path, method, body)

// Validate a response body.
//
// spec: parsed OpenAPI spec
// path: route path
// method: HTTP method
// status: status code string, e.g. "200"
// body: parsed response body
// returns: {valid: bool, errors: [...]}
pub fn validate_response(spec, path, method, status, body) =>
    val.validate_response(spec, path, method, status, body)

// Validate request parameters (query, path, header).
//
// spec: parsed OpenAPI spec
// path: route path
// method: HTTP method
// params: map of parameter name -> value
// returns: {valid: bool, errors: [...]}
pub fn validate_params(spec, path, method, params) =>
    val.validate_params(spec, path, method, params)

// ============================================================
// Documentation generation
// ============================================================

// Generate the OpenAPI JSON spec string for the /openapi.json endpoint.
//
// spec: parsed OpenAPI spec
// returns: JSON string
pub fn spec_json(spec) => docs.openapi_json(spec)

// Generate a Swagger UI HTML page that fetches the spec from a URL.
//
// spec: parsed OpenAPI spec
// spec_url: URL to the /openapi.json endpoint (default: "/openapi.json")
// returns: HTML string
pub fn docs_html(spec, spec_url) {
    let url = if (spec_url != null) spec_url else "/openapi.json";
    let title = util.get_or(spec.info, "title", "API Documentation");
    docs.swagger_ui_html(url, title)
}

// Generate a Swagger UI HTML page with the spec embedded inline.
//
// spec: parsed OpenAPI spec
// returns: HTML string
pub fn docs_html_inline(spec) => docs.swagger_ui_inline(spec)

// Generate a Redoc HTML page.
//
// spec: parsed OpenAPI spec
// spec_url: URL to the /openapi.json endpoint (default: "/openapi.json")
// returns: HTML string
pub fn docs_redoc(spec, spec_url) {
    let url = if (spec_url != null) spec_url else "/openapi.json";
    let title = util.get_or(spec.info, "title", "API Documentation");
    docs.redoc_html(url, title)
}

// ============================================================
// Route introspection
// ============================================================

// List all routes defined in the spec with metadata.
//
// spec: parsed OpenAPI spec
// returns: array of {path, method, summary, operation_id, tags}
pub fn routes(spec) => docs.route_summary(spec)

// Get parameter definitions for a specific endpoint.
//
// spec: parsed OpenAPI spec
// path: route path
// method: HTTP method
// returns: array of {name, location, required, schema_type}
pub fn params(spec, path, method) =>
    schema.param_schemas(spec, path, method)
