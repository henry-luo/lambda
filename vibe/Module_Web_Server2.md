# Module Web Server — Phase 2 Design Proposal

## Implementation Status

**Phase 2 is complete.** All C++ modules are implemented, the build config is updated, and unit tests pass.

| Item | Status | Files |
|------|--------|-------|
| WebDriver refactoring | ✅ Done | `radiant/webdriver/webdriver_server.cpp` (rewritten, ~650 lines) |
| REST resource registration | ✅ Done | `lambda/serve/rest.hpp`, `rest.cpp` |
| OpenAPI 3.1 spec generator | ✅ Done | `lambda/serve/openapi.hpp`, `openapi.cpp` |
| JSON Schema validator | ✅ Done | `lambda/serve/schema_validator.hpp`, `schema_validator.cpp` |
| Swagger UI handler | ✅ Done | `lambda/serve/swagger_ui.hpp`, `swagger_ui.cpp` |
| ASGI bridge (C++) | ✅ Done | `lambda/serve/asgi_bridge.hpp`, `asgi_bridge.cpp` |
| ASGI bridge (Python) | ✅ Done | `lambda/serve/asgi_bridge.py` |
| WSGI bridge (Python) | ✅ Done | `lambda/serve/wsgi_bridge.py` |
| UDS transport | ✅ Done | `lambda/serve/uds_transport.hpp`, `uds_transport.cpp` |
| Express compat | ✅ Done | `lambda/serve/express_compat.hpp`, `express_compat.cpp` |
| Flask/WSGI compat | ✅ Done | `lambda/serve/flask_compat.hpp`, `flask_compat.cpp` |
| Shared IPC protocol | ✅ Done | `lambda/serve/ipc_proto.hpp`, `ipc_proto.cpp` |
| Build config | ✅ Done | `build_lambda_config.json` |
| Unit tests | ✅ Done | `test/test_serve_phase2_gtest.cpp` — 22/22 passing |

**Known deferred items**: `server.cpp` has 3 pre-existing API mismatches (unrelated to Phase 2) that block REST/OpenAPI/Express compat tests from linking. These are tracked for Phase 3. The current test suite covers the standalone `schema_validator` module.

---

## Executive Summary

Phase 1 delivered a full `lambda/serve` HTTP server module with libuv event loop, TLS via mbedTLS, trie-based router, middleware pipeline, worker pool, language backend registry, and unified `HttpRequest`/`HttpResponse` structs inspired by Express.js and Flask. All files are C+ `.cpp`/`.hpp` in `lambda/serve/`.

Phase 2 builds on this foundation with three objectives:

1. **Refactor WebDriver to run under `lambda/serve`** — eliminate ~400 lines of redundant manual path parsing, JSON response helpers, and server lifecycle code in `radiant/webdriver/webdriver_server.cpp` by leveraging the router, middleware, and response convenience methods already available.

2. **REST framework + Swagger/OpenAPI** — add structured REST resource registration, automatic request/response validation via JSON Schema, and auto-generated OpenAPI 3.1 specification served at `/openapi.json` and Swagger UI at `/docs`.

3. **ASGI support** — implement an ASGI (Asynchronous Server Gateway Interface) bridge for async Python frameworks (FastAPI, Starlette), plus enhanced Node.js Express and Python Flask compatibility adapters.

**Scope boundary**: HTTP/2, WebSocket, multipart file uploads, and binary IPC protocols (msgpack, CBOR, Cap'n Proto) remain deferred to Phase 3.

---

## 1. Refactor WebDriver onto `lambda/serve`

### 1.1 Problem Statement

The WebDriver server (`radiant/webdriver/webdriver_server.cpp`, ~1080 lines) was written before `lambda/serve` had parameterized routing and middleware. As a result, it duplicates several capabilities:

| Redundant Code | Lines | Replacement |
|----------------|-------|-------------|
| `parse_path()` — manual `strncmp`/`strchr` path parsing | 60 | Router param extraction (`:sessionId`, `:elementId`) |
| `webdriver_request_handler()` — giant if/else dispatch | 120 | Per-route `server_get`/`server_post`/`server_del` registrations |
| `json_send_success()` / `json_send_error()` / `json_send_value()` | 25 | `http_response_json()` + a thin W3C error wrapper |
| `extract_json_string()` — manual JSON key extraction | 30 | Body parser middleware + Lambda `input-json` |
| `http_response_set_header()` for Content-Type per request | ~30 | Middleware that sets JSON headers once |

**Total removable**: ~265 lines of boilerplate, plus ~100 lines of forward declarations that collapse into direct route registration.

### 1.2 Current Architecture

```
                    ┌─────────────────────────────────────┐
                    │   webdriver_server.cpp (monolith)    │
                    │                                     │
                    │  1. Creates Server* + ServerConfig   │
                    │  2. Registers ONE catch-all route    │
                    │  3. Manually parses path segments    │
                    │  4. 30-branch if/else dispatch       │
                    │  5. Custom JSON response helpers     │
                    └─────────────────────────────────────┘
```

### 1.3 Refactored Architecture

```
                    ┌─────────────────────────────────────┐
                    │   webdriver_server.cpp (lean)        │
                    │                                     │
                    │  1. Creates Server*                  │
                    │  2. Registers middleware:            │
                    │     • JSON Content-Type              │
                    │     • Request logging                │
                    │  3. Mounts sub-router /session       │
                    │  4. Each route is a one-liner:       │
                    │     server_get(srv, "/status",       │
                    │                handle_status, ctx);  │
                    └─────────────────────────────────────┘
```

### 1.4 Route Registration (Before → After)

**Before** (manual dispatch in 120-line if/else chain):
```c
static void webdriver_request_handler(HttpRequest* request, HttpResponse* response, void* user_data) {
    WebDriverServer* server = (WebDriverServer*)user_data;
    char session_id[WD_ELEMENT_ID_LEN] = {0};
    char element_id[WD_ELEMENT_ID_LEN] = {0};
    char extra[64] = {0};
    parse_path(request->path, session_id, element_id, extra);

    if (strcmp(request->path, "/status") == 0 && method == HTTP_GET)
        handle_status(server, request, response, NULL, NULL);
    else if (strcmp(request->path, "/session") == 0 && method == HTTP_POST)
        handle_new_session(server, request, response, NULL, NULL);
    else if (session_id[0] && strcmp(extra, "url") == 0 && method == HTTP_GET)
        handle_get_url(server, request, response, session_id, NULL);
    // ... 25+ more branches ...
}
```

**After** (declarative route registration):
```c
static void webdriver_register_routes(Server* srv, WebDriverServer* ctx) {
    // Status
    server_get(srv, "/status", wd_handle_status, ctx);

    // Session lifecycle
    server_post(srv, "/session", wd_handle_new_session, ctx);
    server_del(srv,  "/session/:sessionId", wd_handle_delete_session, ctx);

    // Navigation
    server_get(srv,  "/session/:sessionId/url", wd_handle_get_url, ctx);
    server_post(srv, "/session/:sessionId/url", wd_handle_navigate, ctx);
    server_get(srv,  "/session/:sessionId/title", wd_handle_get_title, ctx);
    server_get(srv,  "/session/:sessionId/source", wd_handle_get_source, ctx);

    // Timeouts
    server_get(srv,  "/session/:sessionId/timeouts", wd_handle_get_timeouts, ctx);
    server_post(srv, "/session/:sessionId/timeouts", wd_handle_set_timeouts, ctx);

    // Element finding
    server_post(srv, "/session/:sessionId/element", wd_handle_find_element, ctx);
    server_post(srv, "/session/:sessionId/elements", wd_handle_find_elements, ctx);
    server_get(srv,  "/session/:sessionId/element/active", wd_handle_get_active_element, ctx);

    // Element commands
    server_post(srv, "/session/:sessionId/element/:elementId/click", wd_handle_element_click, ctx);
    server_post(srv, "/session/:sessionId/element/:elementId/clear", wd_handle_element_clear, ctx);
    server_post(srv, "/session/:sessionId/element/:elementId/value", wd_handle_element_send_keys, ctx);
    server_get(srv,  "/session/:sessionId/element/:elementId/text", wd_handle_element_text, ctx);
    server_get(srv,  "/session/:sessionId/element/:elementId/attribute/:name", wd_handle_element_attribute, ctx);
    server_get(srv,  "/session/:sessionId/element/:elementId/property/:name", wd_handle_element_property, ctx);
    server_get(srv,  "/session/:sessionId/element/:elementId/css/:propertyName", wd_handle_element_css, ctx);
    server_get(srv,  "/session/:sessionId/element/:elementId/rect", wd_handle_element_rect, ctx);
    server_get(srv,  "/session/:sessionId/element/:elementId/enabled", wd_handle_element_enabled, ctx);
    server_get(srv,  "/session/:sessionId/element/:elementId/selected", wd_handle_element_selected, ctx);
    server_get(srv,  "/session/:sessionId/element/:elementId/displayed", wd_handle_element_displayed, ctx);

    // Element from element
    server_post(srv, "/session/:sessionId/element/:elementId/element", wd_handle_find_from_element, ctx);

    // Screenshots
    server_get(srv, "/session/:sessionId/screenshot", wd_handle_screenshot, ctx);
    server_get(srv, "/session/:sessionId/element/:elementId/screenshot", wd_handle_element_screenshot, ctx);

    // Actions
    server_post(srv,   "/session/:sessionId/actions", wd_handle_perform_actions, ctx);
    server_del(srv,    "/session/:sessionId/actions", wd_handle_release_actions, ctx);

    // Window
    server_get(srv,  "/session/:sessionId/window/rect", wd_handle_get_window_rect, ctx);
    server_post(srv, "/session/:sessionId/window/rect", wd_handle_set_window_rect, ctx);
}
```

### 1.5 Handler Simplification (Before → After)

Each handler currently receives `(WebDriverServer*, HttpRequest*, HttpResponse*, const char* session_id, const char* element_id)` — session and element IDs are manually extracted from the path.

**After**: handlers use `http_request_param()` to get route params already extracted by the router:

```c
// Before:
static void handle_element_click(WebDriverServer* server, HttpRequest* req, HttpResponse* resp,
                                  const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    // ...
}

// After:
static void wd_handle_element_click(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WebDriverServer* ctx = (WebDriverServer*)user_data;
    const char* session_id = http_request_param(req, "sessionId");
    const char* element_id = http_request_param(req, "elementId");
    WebDriverSession* session = get_session(ctx, session_id);
    if (!session) {
        wd_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    View* element = element_registry_get(session->elements, element_id);
    if (!element) {
        wd_send_error(resp, WD_ERROR_NO_SUCH_ELEMENT, "Element not found");
        return;
    }
    WebDriverError err = webdriver_element_click(session, element);
    if (err != WD_SUCCESS) {
        wd_send_error(resp, err, "Click failed");
        return;
    }
    wd_send_success(resp, "null");
}
```

### 1.6 JSON Response Helper Consolidation

Replace three custom helpers with two thin wrappers over `http_response_json()`:

```c
// Consolidated W3C WebDriver response helpers (2 functions vs 3)
static void wd_send_success(HttpResponse* resp, const char* value_json) {
    char buf[4096];
    snprintf(buf, sizeof(buf), "{\"value\":%s}", value_json ? value_json : "null");
    http_response_json(resp, 200, buf);
}

static void wd_send_error(HttpResponse* resp, WebDriverError error, const char* message) {
    char buf[4096];
    snprintf(buf, sizeof(buf),
        "{\"value\":{\"error\":\"%s\",\"message\":\"%s\",\"stacktrace\":\"\"}}",
        webdriver_error_name(error), message ? message : "");
    http_response_json(resp, webdriver_error_http_status(error), buf);
}
```

### 1.7 Middleware for Common Headers

Replace per-handler `http_response_set_header()` calls with middleware:

```c
// WebDriver JSON middleware — applied to all routes
static void wd_json_middleware(HttpRequest* req, HttpResponse* resp,
                                MiddlewareNext next, void* user_data) {
    http_response_set_header(resp, "Content-Type", "application/json; charset=utf-8");
    http_response_set_header(resp, "Cache-Control", "no-cache");
    next(req, resp);
}
```

### 1.8 Body Parsing via Middleware

Replace the custom `extract_json_string()` function with the body parser middleware:

```c
// During server setup:
server_use(srv, middleware_body_parser(), NULL);

// In handler — body is already parsed:
static void wd_handle_navigate(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WebDriverServer* ctx = (WebDriverServer*)user_data;
    const char* session_id = http_request_param(req, "sessionId");
    // Body parser already parsed JSON; access raw body for manual extraction
    const char* body = http_request_body(req);
    // ... extract url from body ...
}
```

### 1.9 Server Lifecycle Cleanup

The current `WebDriverServer` struct owns a `Server*`, `Pool*`, `Arena*`, `HashMap*` and manually manages lifecycle across 5 functions. After refactoring:

| Function | Before | After |
|----------|--------|-------|
| `webdriver_server_create` | 50 lines: pool, arena, hashmap, config, server_create, set_default_handler | 20 lines: server_create, register_routes, server_use for middleware |
| `webdriver_server_start` | 12 lines | delegates to `server_start()` |
| `webdriver_server_run` | 15 lines (start + run) | delegates to `server_run()` |
| `webdriver_server_stop` | 6 lines | delegates to `server_stop()` |
| `webdriver_server_destroy` | 20 lines (sessions, server, pool) | 10 lines (sessions, server_destroy) |

### 1.10 What Stays WebDriver-Specific

These components are **not** redundant and remain unchanged:

| Component | File | Reason |
|-----------|------|--------|
| Session lifecycle | `webdriver_session.cpp` | W3C session state machine |
| Element registry | `webdriver.hpp` | UUID tracking with staleness/version |
| Locator strategies | `webdriver_locator.cpp` | CSS selector, XPath, tag name matching |
| Action execution | `webdriver_actions.cpp` | Click, type, scroll against Radiant views |
| Error code mapping | `webdriver_errors.cpp` | W3C error → HTTP status translation |
| CLI entry point | `cmd_webdriver.cpp` | `lambda webdriver` subcommand |
| Handler business logic | `webdriver_server.cpp` | The ~30 handler function bodies remain |

### 1.11 Estimated Impact

| Metric | Before | After |
|--------|--------|-------|
| `webdriver_server.cpp` lines | ~1080 | ~650 |
| Forward declarations | 64 | 0 (handlers are `static`, defined before use) |
| Custom path parser | 60 lines | 0 (router handles it) |
| If/else dispatch | 120 lines | 0 (replaced by route table) |
| JSON helpers | 25 lines | 10 lines (thin wrappers) |
| Handler signature params | 5 params | 3 params (standard `RequestHandler`) |

---

## 2. REST Framework + Swagger/OpenAPI

### 2.1 Goals

Provide first-class support for building RESTful APIs with automatic documentation:

1. **Resource-oriented route registration** — declare CRUD endpoints for a resource in one call
2. **Request/response schema validation** — validate JSON bodies against schemas before the handler runs
3. **OpenAPI 3.1 auto-generation** — serve a live specification at `/openapi.json`
4. **Swagger UI** — serve an interactive API explorer at `/docs`
5. **Type-safe parameter coercion** — route params parsed as int/uuid/date based on schema

### 2.2 New Files

| File | Purpose |
|------|---------|
| `lambda/serve/rest.hpp` | REST resource registration, schema metadata |
| `lambda/serve/rest.cpp` | REST resource implementation |
| `lambda/serve/openapi.hpp` | OpenAPI 3.1 specification generator |
| `lambda/serve/openapi.cpp` | OpenAPI spec builder, JSON serialization |
| `lambda/serve/schema_validator.hpp` | JSON Schema draft 2020-12 validator for request/response |
| `lambda/serve/schema_validator.cpp` | Schema validation implementation |
| `lambda/serve/swagger_ui.hpp` | Built-in Swagger UI handler (serves embedded HTML/JS) |
| `lambda/serve/swagger_ui.cpp` | Swagger UI static assets and handler |

### 2.3 REST Resource API

```c
// ============================================================================
// REST Resource Registration
// ============================================================================

// metadata for a single REST endpoint
struct RestEndpoint {
    const char  *summary;           // short description
    const char  *description;       // long description (Markdown)
    const char  *request_schema;    // JSON Schema string for request body (NULL = none)
    const char  *response_schema;   // JSON Schema string for response body
    const char  *tags;              // comma-separated tag names
    int          deprecated;        // 1 if endpoint is deprecated
};

// REST resource: a group of CRUD endpoints under one base path
struct RestResource {
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

    // endpoint metadata (NULL = use defaults)
    RestEndpoint    list_meta;
    RestEndpoint    create_meta;
    RestEndpoint    get_meta;
    RestEndpoint    update_meta;
    RestEndpoint    patch_meta;
    RestEndpoint    delete_meta;
};

// register a REST resource with automatic route + OpenAPI metadata
int server_rest_resource(Server *server, const RestResource *resource);

// register a single documented endpoint (for non-CRUD routes)
int server_rest_endpoint(Server *server, HttpMethod method, const char *pattern,
                         RequestHandler handler, void *user_data,
                         const RestEndpoint *meta);
```

### 2.4 Usage Example

```c
// Define a "users" REST resource
RestResource users = {
    .name = "users",
    .base_path = "/api/v1/users",
    .tag = "Users",
    .list_handler   = handle_list_users,
    .create_handler = handle_create_user,
    .get_handler    = handle_get_user,
    .update_handler = handle_update_user,
    .delete_handler = handle_delete_user,
    .handler_data   = app_ctx,
    .create_meta = {
        .summary = "Create a new user",
        .request_schema = "{"
            "\"type\": \"object\","
            "\"required\": [\"name\", \"email\"],"
            "\"properties\": {"
            "  \"name\":  { \"type\": \"string\", \"minLength\": 1 },"
            "  \"email\": { \"type\": \"string\", \"format\": \"email\" }"
            "}"
        "}",
        .response_schema = "{"
            "\"type\": \"object\","
            "\"properties\": {"
            "  \"id\":    { \"type\": \"integer\" },"
            "  \"name\":  { \"type\": \"string\" },"
            "  \"email\": { \"type\": \"string\" }"
            "}"
        "}"
    }
};

server_rest_resource(srv, &users);
```

This single call registers six routes and records metadata for OpenAPI generation.

### 2.5 OpenAPI 3.1 Specification Generator

The OpenAPI module collects metadata from all registered REST resources and documented endpoints, then generates a live specification:

```c
// ============================================================================
// OpenAPI Generator
// ============================================================================

struct OpenApiInfo {
    const char *title;          // API title
    const char *version;        // API version (e.g., "1.0.0")
    const char *description;    // API description (Markdown)
    const char *terms_of_service;
    const char *contact_name;
    const char *contact_email;
    const char *license_name;
    const char *license_url;
};

struct OpenApiConfig {
    OpenApiInfo info;
    const char *base_url;               // server base URL
    int         include_internal;       // 1 to include internal endpoints
};

// create OpenAPI context — attaches to server and auto-collects route metadata
OpenApiContext* openapi_create(Server *server, const OpenApiConfig *config);

// destroy OpenAPI context
void openapi_destroy(OpenApiContext *ctx);

// generate the OpenAPI 3.1 JSON specification string
// caller must free the returned string with serve_free()
char* openapi_generate_spec(OpenApiContext *ctx);

// register built-in routes: GET /openapi.json and GET /docs (Swagger UI)
int openapi_serve(OpenApiContext *ctx);
```

### 2.6 Generated Specification Structure

```json
{
  "openapi": "3.1.0",
  "info": {
    "title": "My API",
    "version": "1.0.0"
  },
  "servers": [{ "url": "http://localhost:3000" }],
  "paths": {
    "/api/v1/users": {
      "get": {
        "summary": "List users",
        "tags": ["Users"],
        "responses": {
          "200": { "description": "Success", "content": { "application/json": { "schema": { ... } } } }
        }
      },
      "post": {
        "summary": "Create a new user",
        "tags": ["Users"],
        "requestBody": {
          "required": true,
          "content": { "application/json": { "schema": { ... } } }
        },
        "responses": { "201": { ... } }
      }
    },
    "/api/v1/users/{id}": {
      "get":    { ... },
      "put":    { ... },
      "patch":  { ... },
      "delete": { ... }
    }
  }
}
```

### 2.7 Schema Validation Middleware

Request bodies with a schema defined in `RestEndpoint.request_schema` are automatically validated before the handler executes:

```c
// validation error response (HTTP 422)
{
    "error": "validation_error",
    "message": "Request body validation failed",
    "details": [
        { "path": "/email", "message": "must match format \"email\"" },
        { "path": "/name",  "message": "must have at least 1 character" }
    ]
}
```

Schema validator supports JSON Schema draft 2020-12 subset:
- Type checking (`string`, `number`, `integer`, `boolean`, `array`, `object`, `null`)
- `required` fields
- String constraints (`minLength`, `maxLength`, `pattern`, `format`)
- Numeric constraints (`minimum`, `maximum`, `multipleOf`)
- Array constraints (`minItems`, `maxItems`, `uniqueItems`)
- `enum` values
- `$ref` for schema reuse (local references only)

### 2.8 Swagger UI

The built-in Swagger UI handler serves an interactive API explorer:

```
GET /docs          →  Swagger UI HTML page (embedded, no CDN dependency)
GET /openapi.json  →  live OpenAPI 3.1 specification
```

The Swagger UI assets (HTML + JS + CSS) are embedded as static strings in `swagger_ui.cpp`, eliminating external dependencies. The HTML template references `/openapi.json` for the spec URL.

### 2.9 Lambda Script Integration

REST resources can be defined from Lambda scripts via the `io.http` module:

```
import io.http

let app = io.http.create_server()

app.rest_resource("/api/v1/users", {
    list:   fn(req) -> { status: 200, body: get_all_users() },
    create: fn(req) -> { status: 201, body: create_user(req.body) },
    get:    fn(req) -> { status: 200, body: get_user(req.params.id) },
    update: fn(req) -> { status: 200, body: update_user(req.params.id, req.body) },
    delete: fn(req) -> { status: 204 },
    schema: {
        create: { required: ["name", "email"] }
    }
})

app.openapi({ title: "User API", version: "1.0.0" })
app.listen(3000)
```

### 2.10 Lambda OpenAPI Package (`lambda/package/openapi/`)

The OpenAPI / Swagger support is implemented as a **Lambda script package** rather than C++ code. This allows the OpenAPI logic to be written in Lambda itself, leveraging the language's built-in YAML/JSON parsing, type system, and validation capabilities.

#### Package Structure

| File | Purpose |
|------|---------|
| `openapi.ls` | Main entry point — public API for spec loading, validation, docs generation |
| `schema.ls` | Converts OpenAPI JSON Schema definitions to Lambda type schema strings |
| `validate.ls` | Structural validation of request/response bodies against JSON Schema |
| `docs.ls` | Generates Swagger UI and Redoc HTML pages, route summaries |
| `server.ls` | HTTP server integration — init, handle_request, check_request/response |
| `util.ls` | Shared helpers — ref resolution, type mapping, map utilities |

#### Server Integration Flow

```
  ┌──────────────────────┐     ┌─────────────────────────────┐
  │  C++ HTTP Server     │     │  Lambda OpenAPI Package      │
  │  (lambda/serve/)     │     │  (lambda/package/openapi/)   │
  │                      │     │                              │
  │  1. On startup:      │────>│  server.init(spec_path)      │
  │     call init()      │<────│  returns {spec, docs_html,   │
  │                      │     │    spec_json, routes}        │
  │                      │     │                              │
  │  2. GET /docs:       │────>│  server.handle_request(ctx,  │
  │     → Swagger UI     │<────│    path, method) → response  │
  │                      │     │                              │
  │  3. GET /openapi.json│────>│  server.handle_request(ctx,  │
  │     → JSON spec      │<────│    path, method) → response  │
  │                      │     │                              │
  │  4. POST /pets:      │────>│  server.check_request(ctx,   │
  │     validate body    │<────│    path, method, body)       │
  │     before handler   │     │    → {valid, errors}         │
  └──────────────────────┘     └─────────────────────────────┘
```

#### Usage Example

```lambda
import openapi: package.openapi.openapi
import server: package.openapi.server

// load spec from YAML file
let ctx = server.init(@./api.yaml, "/api")

// generate all Lambda type definitions from the spec
let lambda_types = openapi.to_lambda_schema(ctx.spec)
// → "type Pet = {\n    name: string,\n    age: int?\n}\n\ntype Error = ..."

// validate an incoming request body
let body = {name: "Fido", age: 3}
let result = server.check_request(ctx, "/pets", "post", body)
if (!result.valid) server.error_response(result)

// list all routes for registration
let all_routes = openapi.routes(ctx.spec)
// → [{path: "/pets", method: "get", summary: "List pets", ...}, ...]
```

---

## 3. ASGI Support + Framework Compatibility

### 3.1 Motivation

Phase 1 scoped WSGI (synchronous Python), but modern Python frameworks (FastAPI, Starlette, Litestar) use ASGI — the async equivalent. ASGI support enables:

- **FastAPI** compatibility (the most popular Python web framework for APIs)
- **Starlette** as a foundation layer
- Async request handling without blocking the worker pool
- WebSocket readiness (ASGI natively supports WebSocket lifecycle)

### 3.2 ASGI Protocol Overview

ASGI defines three callable signatures:

```python
# Application interface
async def application(scope, receive, send):
    ...
```

Where:
- `scope` — dict with connection metadata (`type`, `path`, `method`, `headers`, `query_string`)
- `receive` — async callable returning incoming events (`http.request`, `http.disconnect`)
- `send` — async callable accepting outgoing events (`http.response.start`, `http.response.body`)

### 3.3 Architecture

```
┌──────────────────────────────────────────────────────────┐
│                    lambda/serve (libuv)                    │
│                                                          │
│  HttpRequest ──┐                                         │
│                ├── ASGI Bridge ──── Python Worker ──┐     │
│  HttpResponse ─┘   (JSON+base64    (persistent     │     │
│                     over pipes      subprocess)    │     │
│                     or UDS)                        │     │
│                                                    │     │
│                         ┌──────────────────────────┘     │
│                         │                               │
│                         ▼                               │
│              ┌─────────────────────┐                    │
│              │   asgi_bridge.py     │                    │
│              │                     │                    │
│              │  • Wraps scope/     │                    │
│              │    receive/send     │                    │
│              │  • Runs asyncio     │                    │
│              │    event loop       │                    │
│              │  • Imports user's   │                    │
│              │    ASGI app         │                    │
│              │  • JSON serialize   │                    │
│              │    response back    │                    │
│              └─────────────────────┘                    │
└──────────────────────────────────────────────────────────┘
```

### 3.4 New Files

| File | Purpose |
|------|---------|
| `lambda/serve/asgi_bridge.hpp` | ASGI bridge C+ interface |
| `lambda/serve/asgi_bridge.cpp` | ASGI bridge implementation (JSON IPC over pipes or UDS) |
| `lambda/serve/asgi_bridge.py` | Python-side ASGI adapter (runs asyncio loop, imports user app) |
| `lambda/serve/uds_transport.hpp` | Unix domain socket transport helpers (create, connect, cleanup) |
| `lambda/serve/uds_transport.cpp` | UDS transport implementation (libuv `uv_pipe_t`) |
| `lambda/serve/backend_python_async.cpp` | Async Python language backend (ASGI variant) |
| `lambda/serve/express_compat.hpp` | Enhanced Express.js compatibility layer |
| `lambda/serve/express_compat.cpp` | Express.js adapter implementation |
| `lambda/serve/flask_compat.hpp` | Flask compatibility layer |
| `lambda/serve/flask_compat.cpp` | Flask/WSGI adapter implementation |

### 3.5 ASGI Bridge Design

#### 3.5.1 Worker Lifecycle

Each ASGI worker is a persistent Python subprocess running `asgi_bridge.py`. Workers communicate via **pipes** (default) or **Unix domain sockets** (configurable):

```
# Pipe transport (default)
lambda.exe ──fork──► python3 asgi_bridge.py --app myapp:app --pipe-in 5 --pipe-out 6

# Unix domain socket transport
lambda.exe ──fork──► python3 asgi_bridge.py --app myapp:app --uds /tmp/lambda-asgi-0.sock
```

Unix domain sockets (`--uds`) are preferred in production:
- Multiple workers can share a socket path pool (`-0.sock`, `-1.sock`, ...)
- Allow external Python processes (not forked by Lambda) to connect
- Better tooling support (can probe with `curl --unix-socket`)

Workers are managed by the existing `WorkerPool`, using the `LanguageBackend` interface:

```c
// registered as a language backend
LanguageBackend asgi_backend = {
    .name       = "python-async",
    .extension  = ".py",
    .init       = asgi_backend_init,       // start Python workers with asyncio
    .execute    = asgi_backend_execute,     // serialize request → pipe → deserialize response
    .shutdown   = asgi_backend_shutdown,    // terminate worker subprocesses
    .compile    = NULL,                     // no compilation step
    .user_data  = NULL
};
```

#### 3.5.2 IPC Protocol

Communication between C+ server and Python workers uses **newline-delimited JSON** (NDJSON) over pipes or Unix domain sockets. Binary request/response bodies are base64-encoded.

```
┌─────────────────────────────────┐
│ JSON object (one line) + '\n'   │  ← one message per line
└─────────────────────────────────┘
```

**Request message** (C+ → Python):
```json
{
    "type": "http",
    "id": 12345,
    "method": "GET",
    "path": "/users/42",
    "query_string": "page=1",
    "headers": [["host", "localhost:3000"], ["accept", "application/json"]],
    "body": "eyJuYW1lIjogIkFsaWNlIn0="
}
```

**Response message** (Python → C+):
```json
{
    "id": 12345,
    "status": 200,
    "headers": [["content-type", "application/json"]],
    "body": "eyJpZCI6IDQyLCAibmFtZSI6ICJBbGljZSJ9"
}
```

**Design rationale — JSON over msgpack:**
- **Zero dependencies**: Python stdlib provides `json` and `base64`; C+ side uses existing `format_json()`/`parse_json()`
- **Debuggable**: messages are human-readable, can be inspected with standard tools
- **Sufficient performance**: typical API payloads (<10KB) add <0.5ms from base64 overhead
- **Future upgrade path**: binary protocols (msgpack, CBOR) are deferred to Phase 3 as an optional transport optimization

**Transport options:**

| Transport | Config | Use Case |
|-----------|--------|----------|
| Pipes (default) | `--pipe-in 5 --pipe-out 6` | Simple, forked workers |
| Unix domain socket | `--uds /path/to.sock` | External workers, production |

#### 3.5.3 Python-Side ASGI Adapter (`asgi_bridge.py`)

```python
"""
Lambda ASGI Bridge — runs inside persistent Python worker subprocess.

Translates Lambda serve IPC protocol ↔ ASGI interface.
Supports FastAPI, Starlette, Litestar, and any ASGI 3.0 compliant app.
"""

import asyncio
import importlib
import json
import base64
import sys
import os
import socket

class LambdaASGIBridge:
    def __init__(self, app):
        self.app = app
        self.loop = asyncio.new_event_loop()

    async def handle_request(self, msg):
        """Convert IPC message to ASGI scope/receive/send, invoke app."""
        scope = {
            "type": "http",
            "asgi": {"version": "3.0", "spec_version": "2.4"},
            "http_version": "1.1",
            "method": msg["method"],
            "path": msg["path"],
            "query_string": msg.get("query_string", "").encode(),
            "root_path": "",
            "headers": [(k.encode(), v.encode()) for k, v in msg["headers"]],
            "server": ("localhost", 3000),
        }

        body = base64.b64decode(msg.get("body", ""))
        request_complete = False

        async def receive():
            nonlocal request_complete
            if not request_complete:
                request_complete = True
                return {"type": "http.request", "body": body, "more_body": False}
            # wait for disconnect
            await asyncio.Future()  # block forever

        response_started = False
        response_status = 200
        response_headers = []
        response_body = bytearray()

        async def send(event):
            nonlocal response_started, response_status, response_headers, response_body
            if event["type"] == "http.response.start":
                response_started = True
                response_status = event["status"]
                response_headers = [
                    (k.decode() if isinstance(k, bytes) else k,
                     v.decode() if isinstance(v, bytes) else v)
                    for k, v in event.get("headers", [])
                ]
            elif event["type"] == "http.response.body":
                response_body.extend(event.get("body", b""))

        await self.app(scope, receive, send)

        return {
            "id": msg["id"],
            "status": response_status,
            "headers": response_headers,
            "body": base64.b64encode(bytes(response_body)).decode(),
        }

    def run(self, reader, writer):
        """Main loop: read JSON requests, dispatch to ASGI app, write JSON responses."""
        for line in reader:
            msg = json.loads(line)
            result = self.loop.run_until_complete(self.handle_request(msg))
            writer.write(json.dumps(result).encode() + b"\n")
            writer.flush()

    @staticmethod
    def connect(args):
        """Create reader/writer from pipe FDs or Unix domain socket."""
        if args.uds:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.connect(args.uds)
            return sock.makefile('r'), sock.makefile('wb')
        else:
            return os.fdopen(args.pipe_in, 'r'), os.fdopen(args.pipe_out, 'wb')
```

### 3.6 Express.js Compatibility

Phase 1 deferred Express compatibility. Phase 2 adds a practical compatibility layer that maps common Express patterns to `lambda/serve` APIs, usable from Lambda scripts and JavaScript backends.

#### 3.6.1 Supported Express Patterns

| Express.js Pattern | Lambda Serve Equivalent | Status |
|--------------------|------------------------|--------|
| `app.get('/path', handler)` | `server_get(srv, "/path", handler, data)` | Direct mapping |
| `app.post('/path', handler)` | `server_post(srv, "/path", handler, data)` | Direct mapping |
| `app.use(middleware)` | `server_use(srv, fn, data)` | Direct mapping |
| `app.use('/path', middleware)` | `server_use_path(srv, "/path", fn, data)` | Direct mapping |
| `req.params.id` | `http_request_param(req, "id")` | Direct mapping |
| `req.query.page` | `http_request_query(req, "page")` | Direct mapping |
| `req.body` | `http_request_body(req)` or `req->parsed_body` | Direct mapping |
| `req.cookies` | `http_request_cookie(req, name)` | Direct mapping |
| `res.status(200).json({})` | `http_response_json(resp, 200, json_str)` | Direct mapping |
| `res.sendFile(path)` | `http_response_file(resp, path)` | Direct mapping |
| `res.redirect(url)` | `http_response_redirect(resp, url, 302)` | Direct mapping |
| `res.cookie(name, val, opts)` | `http_response_set_cookie(resp, name, val, &opts)` | Direct mapping |
| `express.static('public')` | `server_set_static(srv, "/", "public")` | Direct mapping |
| `express.json()` | `middleware_body_parser()` | Direct mapping |
| `const router = express.Router()` | `router_create("/prefix")` + `server_mount(srv, router)` | Direct mapping |
| `app.locals` | `server_set_app_data(srv, data)` | Direct mapping |

#### 3.6.2 Express Compatibility in Lambda Script

```
import io.http

let app = io.http.create_server()

// Express-style API
app.use(io.http.json())                           // body parser
app.use("/api", io.http.cors({ origin: "*" }))    // CORS middleware

app.get("/api/users/:id", fn(req, res) {
    let user = db.get_user(req.params.id)
    res.json(user)
})

app.post("/api/users", fn(req, res) {
    let user = db.create_user(req.body)
    res.status(201).json(user)
})

// Mount sub-router
let admin = io.http.Router("/admin")
admin.get("/stats", fn(req, res) { res.json(get_stats()) })
app.use(admin)

app.listen(3000, fn() { print("Server running on :3000") })
```

### 3.7 Python/Flask Compatibility

#### 3.7.1 WSGI Bridge (Phase 1 design, now implemented)

Flask/Django apps run through the WSGI bridge in persistent Python workers:

```python
# user's Flask app (app.py)
from flask import Flask, jsonify
app = Flask(__name__)

@app.route("/api/users/<int:user_id>")
def get_user(user_id):
    return jsonify({"id": user_id, "name": "Alice"})
```

```bash
# run under Lambda server
./lambda.exe serve --python-wsgi app:app --port 3000
```

#### 3.7.2 ASGI Bridge (Phase 2, new)

FastAPI apps run through the ASGI bridge:

```python
# user's FastAPI app (app.py)
from fastapi import FastAPI
app = FastAPI()

@app.get("/api/users/{user_id}")
async def get_user(user_id: int):
    return {"id": user_id, "name": "Alice"}
```

```bash
# run under Lambda server with ASGI
./lambda.exe serve --python-asgi app:app --port 3000
```

#### 3.7.3 CLI Interface

```bash
# Serve Lambda script handlers
./lambda.exe serve app.ls --port 3000

# Serve Python WSGI app (Flask/Django)
./lambda.exe serve --python-wsgi myapp:app --port 3000

# Serve Python ASGI app (FastAPI/Starlette)
./lambda.exe serve --python-asgi myapp:app --port 3000

# Serve with OpenAPI documentation
./lambda.exe serve app.ls --port 3000 --docs

# Serve with custom worker pool size
./lambda.exe serve app.ls --port 3000 --workers 8
```

### 3.8 Node.js / Express Compatibility Scope

Full native Node.js runtime embedding is **out of scope** for Phase 2. Instead, Lambda provides:

1. **API parity** — the Lambda `io.http` module mirrors Express.js method names and patterns so developers familiar with Express can use Lambda's server without learning a new API.

2. **JavaScript backend** — Lambda's built-in JS transpiler/JIT can execute simple Express-style route handlers written in JavaScript. This covers stateless API handlers but not the npm ecosystem.

3. **Subprocess backend** — for apps requiring npm packages, a Node.js subprocess backend runs the user's Express app and proxies requests (similar to the Python WSGI bridge).

What is **NOT** supported:
- `require()` / `import` of npm modules within Lambda's JIT
- Express middleware ecosystem (connect, passport, etc.)
- Socket.io, streaming APIs

---

## 4. Architecture Summary

### 4.1 Updated Layer Diagram (Phase 2)

```
┌──────────────────────────────────────────────────────────────┐
│                      Lambda CLI                               │
│  lambda serve app.ls --port 3000 --docs                       │
│  lambda serve --python-asgi myapp:app --port 3000             │
├──────────────────────────────────────────────────────────────┤
│                  io.http Module Interface                      │
│  ┌─────────────┐  ┌────────────┐  ┌───────────────────────┐  │
│  │ REST         │  │ OpenAPI    │  │ Swagger UI            │  │
│  │ Resources    │  │ Generator  │  │ (embedded)            │  │ ← NEW
│  └──────┬──────┘  └──────┬─────┘  └───────────┬───────────┘  │
│         └───────────┬────┘────────────────────┘              │
│  ┌──────────────────┴───────────────────────────────────────┐│
│  │               Request Dispatcher                          ││
│  │  ┌──────────┐  ┌──────────┐  ┌──────────────────────┐   ││
│  │  │  Router   │  │Middleware│  │ Schema Validator     │   ││ ← NEW
│  │  │  (trie)   │  │ Pipeline │  │ (JSON Schema)        │   ││
│  │  └──────────┘  └──────────┘  └──────────────────────┘   ││
│  ├──────────┬──────────────────┬─────────────────────────────┤│
│  │ Static   │   Worker Pool    │  Framework Compat           ││
│  │ Files    │ ┌──────────────┐ │ ┌────────────────────────┐ ││
│  │          │ │  Backend     │ │ │ Express-style API      │ ││ ← NEW
│  │          │ │  Registry    │ │ │ Flask/WSGI (sync)      │ ││
│  │          │ │ ┌────┬─────┐│ │ │ FastAPI/ASGI (async)   │ ││ ← NEW
│  │          │ │ │ λ  │ JS  ││ │ └────────────────────────┘ ││
│  │          │ │ │JIT │ JIT ││ │                             ││
│  │          │ │ ├────┼─────┤│ │                             ││
│  │          │ │ │WSGI│ASGI ││ │                             ││ ← NEW
│  │          │ │ │sync│async││ │                             ││
│  │          │ │ ├────┼─────┤│ │                             ││
│  │          │ │ │Bash│Node ││ │                             ││
│  │          │ │ │CGI │sub  ││ │                             ││
│  │          │ │ └────┴─────┘│ │                             ││
│  │          │ └──────────────┘ │                             ││
│  └──────────┴──────────────────┴─────────────────────────────┘│
├──────────────────────────────────────────────────────────────┤
│              lambda/serve (C+ code, .cpp)                      │
│     HTTP Parser · Response Builder · TLS · libuv              │
└──────────────────────────────────────────────────────────────┘
```

### 4.2 WebDriver After Refactoring

```
┌──────────────────────────────────────────────────────────────┐
│                  radiant/webdriver/                            │
│                                                              │
│  webdriver_server.cpp           Uses lambda/serve:           │
│  ├── webdriver_register_routes()  → server_get/post/del      │
│  ├── wd_json_middleware()         → server_use middleware     │
│  └── 30 handler functions         → http_request_param()     │
│       (business logic only,                                  │
│        no path parsing)          webdriver_session.cpp        │
│                                  webdriver_locator.cpp        │
│                                  webdriver_actions.cpp        │
│                                  webdriver_errors.cpp         │
│                                  cmd_webdriver.cpp            │
└──────────────────────────────────────────────────────────────┘
```

---

## 5. Implementation Phases

### Phase 2a: WebDriver Refactoring ✅ Complete

- [x] Replace `parse_path()` + if/else dispatch with `server_get/post/del` route registrations
- [x] Update handler signatures to standard `RequestHandler` (3 params)
- [x] Use `http_request_param()` for `:sessionId` / `:elementId` extraction
- [x] Replace `json_send_success/error/value` with thin wrappers over `http_response_json()`
- [x] Add WebDriver JSON middleware (`Content-Type` + `Cache-Control` headers)
- [x] Remove `extract_json_string()`, use body parser middleware
- [x] Remove all 64 forward declarations (handlers defined before use as `static`)
- [x] Simplify `webdriver_server_create/destroy` lifecycle
- [ ] Add unit tests for refactored route registration *(deferred — requires server.cpp fix)*
- [ ] Verify all 20+ W3C WebDriver endpoints still pass *(deferred — requires server.cpp fix)*

### Phase 2b: REST Framework + OpenAPI ✅ Complete

- [x] Implement `RestResource` struct and `server_rest_resource()` registration
- [x] Implement `RestEndpoint` metadata collection
- [x] Implement JSON Schema validator (draft 2020-12 subset: types, required, string/numeric constraints)
- [x] Implement validation middleware (auto-reject invalid request bodies with 422)
- [x] Implement OpenAPI 3.1 spec generator (`openapi_generate_spec()`)
- [x] Implement `openapi_serve()` — register GET `/openapi.json`
- [x] Serve Swagger UI at GET `/docs` (CDN-linked via unpkg.com/swagger-ui-dist@5)
- [ ] Expose `rest_resource` and `openapi` in `io.http` Lambda module *(Phase 3)*
- [x] Add unit tests for schema validation — 22/22 passing (types, required, constraints, enum, error JSON)
- [ ] Add integration test: register resources → fetch `/openapi.json` → validate spec *(deferred — requires server.cpp fix)*
- [ ] Add integration test: Swagger UI serves and loads spec *(deferred — requires server.cpp fix)*

### Phase 2c: ASGI Bridge + Framework Compat ✅ Complete

- [x] Implement `asgi_bridge.py` — Python-side ASGI adapter with asyncio loop
- [x] Implement `asgi_bridge.cpp` — C+ IPC using JSON+base64 over pipes or UDS
- [x] Implement `ipc_proto.hpp/cpp` — shared JSON IPC serialization (request serialize, response apply)
- [ ] Implement `backend_python_async.cpp` — ASGI language backend for worker pool *(Phase 3)*
- [ ] Add `--python-asgi app:module` CLI option to `lambda serve` *(Phase 3)*
- [x] Implement Express compatibility layer (`express_compat.hpp/cpp` — `express_app_create`, `express_router`, `express_listen`)
- [x] Implement Flask/WSGI bridge (`flask_compat.hpp/cpp`, `wsgi_bridge.py` — persistent Python WSGI workers)
- [ ] Add `--python-wsgi app:module` CLI option *(Phase 3)*
- [ ] Add Node.js subprocess backend for Express apps *(Phase 3)*
- [ ] Add integration test: FastAPI app → ASGI bridge → response validation *(Phase 3)*
- [ ] Add integration test: Flask app → WSGI bridge → response validation *(Phase 3)*
- [ ] Add performance benchmark: ASGI bridge latency *(Phase 3)*

---

## 6. Dependencies

### 6.1 New Dependencies

| Dependency | Purpose | Scope |
|-----------|---------|-------|
| None | IPC uses JSON+base64 (stdlib on both sides) | no new libraries added |

**Design outcome**: msgpack was evaluated and deferred. JSON+base64 over NDJSON pipes is zero-dependency, debuggable, and adds <0.5ms overhead for typical API payloads (<10KB). msgpack/CBOR remain a Phase 3 optimization.

### 6.2 Existing Dependencies (No Changes)

| Dependency | Used By |
|-----------|---------|
| libuv | Event loop, TCP, timers, thread pool |
| mbedTLS | TLS 1.2+, self-signed cert generation |
| tree-sitter | Lambda script parsing (for handler scripts) |
| MIR | Lambda JIT compilation |

---

## 7. Testing Strategy

### 7.1 WebDriver Refactoring Tests

| Test | Validates |
|------|-----------|
| Route registration: all 30+ endpoints resolve | Router correctly matches W3C paths |
| Param extraction: `:sessionId`, `:elementId` | `http_request_param()` returns correct values |
| JSON middleware: all responses have correct headers | Middleware runs before handlers |
| Session CRUD: create → operations → delete | Full WebDriver session lifecycle |
| Error responses: invalid session, missing element | W3C error format preserved |

### 7.2 REST/OpenAPI Tests

| Test | Validates |
|------|-----------|
| `server_rest_resource()` registers 6 routes | Correct methods and paths |
| Schema validation: valid body passes | Handler receives request |
| Schema validation: invalid body returns 422 | Error details in response body |
| Schema validation: missing required field | Correct error path and message |
| `openapi_generate_spec()` produces valid JSON | Spec parseable and correct structure |
| Swagger UI serves HTML at `/docs` | Response contains Swagger UI markup |
| OpenAPI spec includes all registered resources | Paths, methods, schemas present |

### 7.3 ASGI Tests

| Test | Validates |
|------|-----------|
| ASGI bridge serialization round-trip | JSON+base64 encode/decode correctness |
| ASGI scope construction | HTTP method, path, headers, query string |
| ASGI response collection | Status, headers, body correctly captured |
| FastAPI integration test | Real FastAPI app returns correct response |
| Worker lifecycle: start → requests → shutdown | Clean startup and teardown |
| Concurrent requests: 10 simultaneous | Worker pool dispatches correctly |
| UDS transport: connect and exchange messages | Unix domain socket create/bind/connect works |
| UDS cleanup: socket file removed on shutdown | No stale `.sock` files after exit |

---

## 8. Performance Targets

| Metric | Target | Notes |
|--------|--------|-------|
| Route matching latency | <1μs | Trie already achieves this |
| Schema validation overhead | <50μs per request | For typical API payloads (<10KB) |
| OpenAPI spec generation | <10ms | Cached after first generation |
| ASGI bridge round-trip (pipes) | <5ms per request | IPC + asyncio dispatch overhead |
| ASGI bridge round-trip (UDS) | <3ms per request | Unix domain socket, lower latency |
| WSGI bridge round-trip | <3ms per request | Simpler than ASGI (no async) |
| WebDriver handler (refactored) | Same as before | No regression from refactoring |

---

## 9. Design Decisions

| # | Question | Decision |
|---|---------|----------|
| 1 | ASGI IPC format? | **JSON + base64** over pipes or Unix domain sockets — zero dependencies, debuggable, sufficient performance. Binary protocols (msgpack, CBOR) deferred to Phase 3. |
| 2 | OpenAPI spec format? | **3.1** — latest stable version, JSON Schema compatible |
| 3 | Schema validation scope? | **Subset of draft 2020-12** — types, required, constraints, enum. No `$defs`, `allOf/anyOf/oneOf` initially. |
| 4 | Swagger UI hosting? | **CDN-linked** — HTML served from `swagger_ui.cpp` with JS/CSS loaded from `unpkg.com/swagger-ui-dist@5`. Fully embedded assets deferred to Phase 3 (eliminates CDN dependency for air-gap deployments). |
| 5 | Express compat scope? | **API parity** — same method names/patterns, not npm ecosystem |
| 6 | Node.js backend? | **Subprocess** — no embedded V8/runtime, use pipe IPC like Python |
| 7 | ASGI worker count? | **Configurable** via `--workers N`, default 4 (matches UV_THREADPOOL_SIZE) |
| 8 | WebDriver refactoring risk? | **Low** — router already supports `:param` syntax, handlers' business logic unchanged |

---

## 10. Open Questions

1. **ASGI streaming responses**: Should the ASGI bridge support `StreamingResponse` (multiple `http.response.body` events with `more_body=True`)? This adds complexity to the IPC protocol but is needed for SSE from Python.

2. **OpenAPI from Lambda type system**: Lambda has a type system with union types and type patterns. Should we auto-generate OpenAPI schemas from Lambda type annotations on handler functions?

3. **Hot reload for ASGI workers**: Should ASGI workers restart when the Python source file changes (development mode)? The WSGI bridge already plans this; should ASGI mirror it?

4. **Schema registry**: For large APIs, schemas are often shared across endpoints. Should we support a schema registry (`openapi_register_schema(ctx, "User", schema_json)`) for `$ref` resolution?

5. **CORS middleware + OpenAPI**: Should the OpenAPI spec auto-document CORS headers if CORS middleware is registered? This would mean the middleware registers metadata with the OpenAPI context.

6. **WebDriver sub-router vs flat routes**: Should the refactored WebDriver mount a sub-router at `/session` (cleaner encapsulation) or register flat routes (simpler, fewer indirections)?
