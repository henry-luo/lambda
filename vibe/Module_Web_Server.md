# Module Web Server — Design Proposal

## Executive Summary

This proposal extends the existing HTTP server (migrating from `lib/serve/` to `lambda/serve/` as C+ `.cpp` code) into a **full-featured web application server** capable of serving static files, executing request handlers via a **worker pool** (PHP-FPM style) for Lambda Script, Python, Bash, and other languages, and providing a high-level routing/middleware API comparable to **Express.js** and **Flask**. The server is built on **libuv** for event-driven I/O and **mbedTLS** for TLS, both already integrated.

The server exposes itself to Lambda scripts as the **`io.http`** module (e.g., `io.http.create_server`). The architecture uses a **language backend registry** so new language runtimes (Ruby, PHP, etc.) can be added without modifying the core server. A pre-forked **worker pool** handles all synchronous request handlers — including Lambda JIT handlers — keeping the libuv event loop non-blocking.

**Scope for this phase**: HTTP/1.1 with WSGI support for Python. HTTP/2, WebSocket, multipart uploads, and ASGI are **deferred to future phases**.

---

## 1. Current State Assessment

### What Already Exists (`lib/serve/` → migrating to `lambda/serve/`)

| Component | Status | Notes |
|-----------|--------|-------|
| HTTP server (libuv) | ✅ Working | `server.c` → `server.cpp` — event-driven, async TCP |
| HTTPS / TLS (mbedTLS) | ✅ Working | `tls_handler.c` → `tls_handler.cpp` — TLS 1.2+, self-signed cert gen |
| Request parsing | ✅ Working | Methods, headers, query params, body |
| Response builder | ✅ Working | Status, headers, body, `http_send_file()` |
| Path-based routing | ✅ Basic | Exact-match only; no wildcards, no params |
| MIME detection | ✅ Working | `lib/mime-detect.c` — magic number + extension (integrating into `lambda/serve/mime.hpp`) |
| URL parsing | ✅ Working | `lib/url.h` — full RFC 3986 URL parser |
| File I/O | ✅ Working | `lib/file.h` — read/write/stat/stream/glob |
| Shell execution | ✅ Working | `lib/shell.h` — sync/async processes, capture |
| Lambda script runner | ✅ Working | `lambda/runner.cpp` — JIT compile + execute |
| Module registry | ✅ Working | Unified path-keyed module resolution |
| Cross-language interop | 🟡 Data model ready | `Item` type shared across Lambda/JS; transpiler activation pending |

### What's Missing

- Parameterized route matching (`/users/:id`, `/files/*path`)
- Middleware pipeline (pre/post request processing)
- Static file directory serving with index files, directory listing, ETag/caching
- Worker pool for request handler execution (PHP-FPM style)
- Language backend registry (extensible for Ruby, PHP, etc.)
- Request body parsing (JSON, form-urlencoded)
- Cookie and session management
- CORS handling
- Chunked transfer encoding / streaming responses
- WSGI bridge for Python frameworks (Flask, Django)
- Compatibility adapters for Express / Flask patterns

### Deferred to Future Phases

- WebSocket support
- HTTP/2 server-side (client-side already exists via nghttp2)
- Multipart form data / file uploads
- ASGI support for async Python frameworks (FastAPI, Starlette)
- Native Node.js/Express async support (event loop integration)

---

## 2. Architecture Overview

```
┌──────────────────────────────────────────────────────────┐
│                    Lambda CLI / Embedding                  │
│            lambda serve app.ls --port 3000                 │
│            import io.http; io.http.create_server(...)      │
├──────────────────────────────────────────────────────────┤
│                   io.http Server Module                    │
│  ┌────────────┐  ┌────────────┐  ┌─────────────────────┐ │
│  │  Router     │  │ Middleware  │  │  Body Parser        │ │
│  │  (trie)     │  │  Pipeline   │  │  (JSON/form)        │ │
│  └──────┬─────┘  └──────┬─────┘  └──────────┬──────────┘ │
│         └───────────┬────┘───────────────────┘            │
│  ┌──────────────────┴───────────────────────────────────┐ │
│  │               Request Dispatcher                      │ │
│  ├──────────┬────────────────────┬──────────────────────┤ │
│  │ Static   │   Worker Pool      │  Compat              │ │
│  │ Files    │   (PHP-FPM style)  │  Adapters            │ │
│  │          │ ┌────────────────┐ │  (Express/Flask)     │ │
│  │          │ │Language Backend│ │                      │ │
│  │          │ │  Registry      │ │                      │ │
│  │          │ │ ┌──────┬─────┐│ │                      │ │
│  │          │ │ │Lambda│ JS  ││ │                      │ │
│  │          │ │ │(JIT) │(JIT)││ │                      │ │
│  │          │ │ ├──────┼─────┤│ │                      │ │
│  │          │ │ │Python│ Bash││ │                      │ │
│  │          │ │ │(WSGI)│(CGI)││ │                      │ │
│  │          │ │ ├──────┼─────┤│ │                      │ │
│  │          │ │ │ Ruby │ PHP ││ │                      │ │
│  │          │ │ │(Rack)│(FPM)││ │  (future backends)   │ │
│  │          │ │ └──────┴─────┘│ │                      │ │
│  │          │ └────────────────┘ │                      │ │
│  └──────────┴────────────────────┴──────────────────────┘ │
├──────────────────────────────────────────────────────────┤
│              lambda/serve (C+ code, .cpp)                  │
│     HTTP Parser · Response Builder · TLS · libuv          │
└──────────────────────────────────────────────────────────┘
```

### Layered Design

1. **Transport Layer** — `lambda/serve/server.cpp` (libuv + mbedTLS)
2. **Protocol Layer** — `lambda/serve/http_request.cpp`, `http_response.cpp` (HTTP parsing/response)
3. **Worker Layer** — NEW: pre-forked worker pool with language backend registry
4. **Application Layer** — NEW: router, middleware, body parsing, static files
5. **Framework Layer** — NEW: compatibility adapters for Express/Flask patterns
6. **Module Layer** — NEW: `io.http` Lambda Script module interface

---

## 3. HTTP / HTTPS and libuv Integration

### 3.1 Current libuv Integration (Already Working)

The existing `server.cpp` (migrated from `server.c`) uses libuv directly:
- `uv_tcp_t` for TCP handles
- `uv_read_start()` / `on_alloc` / `on_read` for incremental request buffering
- `uv_write()` for response delivery (inside `http_response_send()`)
- `uv_stop()` for graceful shutdown via signal handler
- Global `uv_loop_t` created per server instance

### 3.2 Enhancements Needed

#### 3.2.1 Connection Timeout

Currently no timeout enforcement. Add a `uv_timer_t` per connection:

```c
typedef struct {
    uv_tcp_t handle;
    uv_timer_t timer;       // NEW: idle timeout
    server_t *server;
    char *buf;
    size_t buf_len;
    size_t buf_cap;
} client_conn_t;
```

Start timer on connection accept; reset on each `on_read`; fire `on_close` on expiry.

#### 3.2.2 Keep-Alive (HTTP/1.1)

Currently closes connection after every response (HTTP/1.0 style). Support persistent connections:

- Parse `Connection: keep-alive` / `Connection: close` headers
- After sending response, reset buffer and restart read instead of closing
- Add configurable `keep_alive_timeout` and `max_requests_per_connection`

#### 3.2.3 Chunked Transfer Encoding

For streaming responses (SSE, large files, CGI output):

```c
// New streaming response API
void http_response_start_chunked(http_response_t *resp);
void http_response_write_chunk(http_response_t *resp, const void *data, size_t len);
void http_response_end_chunked(http_response_t *resp);
```

#### 3.2.4 Request Size Limits

Add configurable limits to prevent resource exhaustion:

```c
typedef struct {
    // ... existing fields ...
    size_t max_header_size;      // default: 8KB
    size_t max_body_size;        // default: 10MB
    size_t max_uri_length;       // default: 8KB
} server_config_t;
```

#### 3.2.5 TLS on Accepted Connections

Current `on_new_connection` uses the same callback for both HTTP and HTTPS tcp handles. For the TLS server, wrap the accepted connection with mbedTLS handshake before entering the HTTP read loop. Proposed approach:

```c
// In on_new_connection for tls_server:
// 1. Accept TCP connection
// 2. Create tls_conn_t wrapping client_conn_t + mbedtls_ssl_context
// 3. Perform async TLS handshake via uv_read_start feeding into mbedtls_ssl_handshake()
// 4. On handshake complete, switch to HTTP parsing with decrypted reads
```

---

## 4. Static File Serving

### 4.1 API

```c
// Serve static files from a directory
int server_set_static(server_t *server, const char *url_prefix,
                      const char *dir_path, static_config_t *config);

typedef struct {
    const char *index_file;      // default: "index.html"
    int directory_listing;       // default: 0 (disabled)
    int enable_etag;             // default: 1
    int enable_last_modified;    // default: 1
    int enable_gzip;             // default: 0 (future)
    size_t max_cache_size;       // in-memory cache limit (0 = no cache)
    const char **deny_patterns;  // glob patterns to deny (e.g., ".*", "*.env")
    int deny_count;
} static_config_t;
```

### 4.2 Behavior

1. **Path resolution**: `url_prefix + request_path` → `dir_path + sanitized_path`
2. **Security**: reject `..` traversal, symlinks outside root, hidden files by default
3. **Index files**: if path is directory, try `index.html` then `index.htm`
4. **MIME type**: use `lambda/serve/mime.hpp` (integrated from `lib/mime-detect.h` — magic number + extension + content sniffing)
5. **Caching**:
   - `ETag` header from file mtime + size hash
   - `Last-Modified` header from file mtime
   - Respond `304 Not Modified` when `If-None-Match` or `If-Modified-Since` matches
6. **Range requests**: support `Range: bytes=` for large file downloads / media streaming
7. **Directory listing**: optional HTML listing with file sizes and dates

### 4.3 Implementation Strategy

Implement as a built-in handler registered via `server_set_static()`. Internally creates a `RequestHandler` closure that:

```cpp
static void static_file_handler(HttpRequest *req, HttpResponse *resp, void *user_data) {
    StaticContext *ctx = (StaticContext *)user_data;
    // 1. Sanitize path (reject traversal)
    // 2. Resolve to filesystem path
    // 3. Check deny patterns
    // 4. stat() the file
    // 5. Check conditional headers (ETag, If-Modified-Since)
    // 6. Set Content-Type from MIME detection
    // 7. Use http_send_file() for small files, chunked for large
}
```

---

## 5. Worker Pool & Language Backend Registry

### 5.1 Overview

All request handlers — whether Lambda Script, JavaScript, Python, or Bash — execute through a **pre-forked worker pool** (PHP-FPM model). This keeps the libuv event loop completely non-blocking. The worker pool is managed by a **language backend registry** that makes it trivial to add new language runtimes (Ruby, PHP, etc.) in the future.

```
                    libuv event loop (main thread)
                              │
                     ┌────────┴────────┐
                     │ Request arrives  │
                     │ Route matched    │
                     │ Middleware runs  │
                     └────────┬────────┘
                              │
                    ┌─────────▼──────────┐
                    │   Worker Pool       │
                    │   (uv_queue_work)   │
                    ├─────────┬──────────┤
                    │ Worker 1│ Worker 2 │ ...  (UV_THREADPOOL_SIZE, default 4)
                    └────┬────┴────┬─────┘
                         │         │
              ┌──────────▼───┐  ┌──▼──────────┐
              │ Language      │  │ Language     │
              │ Backend:      │  │ Backend:     │
              │ Lambda (JIT)  │  │ Python (WSGI)│
              └──────────────┘  └─────────────┘
```

### 5.2 Worker Pool Design

The worker pool is inspired by **PHP-FPM**: a fixed set of pre-initialized workers ready to handle requests. Unlike per-request CGI (which spawns a new process per request), the worker pool amortizes startup cost across many requests.

#### Architecture

```cpp
struct WorkerPool {
    int pool_size;                   // number of workers (default: UV_THREADPOOL_SIZE)
    int max_queue_length;            // pending request queue limit
    int request_timeout_ms;          // per-request timeout (default: 30000)
    LanguageBackend *backends[16];   // registered language backends
    int backend_count;
};

// Dispatch a request to the worker pool
// The callback fires on the libuv main thread when the worker is done
void worker_pool_dispatch(WorkerPool *pool, HttpRequest *req, HttpResponse *resp,
                          RouteHandler *handler, void (*on_complete)(void *ctx), void *ctx);
```

#### Request Flow

1. **Main thread (libuv)**: receives TCP data → parses HTTP → runs middleware → matches route
2. **Main thread**: calls `worker_pool_dispatch()` → `uv_queue_work()` onto libuv thread pool
3. **Worker thread**: looks up the `LanguageBackend` for the handler's language
4. **Worker thread**: calls `backend->execute(req, resp, handler)` — runs the handler to completion
5. **Worker thread**: `uv_queue_work` completion callback fires on main thread
6. **Main thread**: sends HTTP response via `uv_write()`

#### Why Worker Pool for Lambda Handlers Too

Lambda Script handlers are synchronous (no async/await). Without the worker pool, a Lambda handler doing database queries or file I/O would block the libuv event loop, stalling all other connections. By routing **all** handler types through the worker pool — including Lambda JIT handlers — the event loop stays responsive.

### 5.3 Language Backend Registry

The backend registry is a table of language-specific execution strategies. Adding a new language requires implementing one struct:

```cpp
// Generic language backend interface
struct LanguageBackend {
    const char *name;            // "lambda", "javascript", "python", "bash", "ruby", "php"
    const char **extensions;     // {".ls", NULL}, {".py", NULL}, etc.
    int extension_count;

    // Initialize backend (called once at server startup)
    int (*init)(LanguageBackend *self, ServerConfig *config);

    // Execute a request handler (called on worker thread)
    int (*execute)(LanguageBackend *self, HttpRequest *req, HttpResponse *resp,
                   const char *script_path, void *compiled_handler);

    // Pre-compile/cache a script (optional, called on first request or file change)
    void* (*compile)(LanguageBackend *self, const char *script_path);

    // Check if script needs recompilation (for hot reload)
    int (*needs_recompile)(LanguageBackend *self, const char *script_path, void *compiled);

    // Cleanup backend (called at server shutdown)
    void (*shutdown)(LanguageBackend *self);

    void *backend_data;          // language-specific state
};

// Registry API
void backend_registry_register(WorkerPool *pool, LanguageBackend *backend);
LanguageBackend* backend_registry_find(WorkerPool *pool, const char *extension);
```

### 5.4 Built-In Language Backends

#### 5.4.1 Lambda Script Backend (In-Process JIT)

| Property | Value |
|----------|-------|
| Execution | In-process JIT via MIR |
| Extensions | `.ls` |
| Startup cost | ~0ms (already compiled) |
| Per-request cost | ~0.01ms (native function call) |
| Hot reload | Check mtime, recompile if changed |
| Concurrency | Thread-safe with per-worker GC nursery |

```cpp
// Lambda backend implementation
int lambda_backend_execute(LanguageBackend *self, HttpRequest *req, HttpResponse *resp,
                           const char *script_path, void *compiled_handler) {
    LambdaHandler *handler = (LambdaHandler *)compiled_handler;
    // 1. Convert HttpRequest → Item (Lambda map)
    Item request_item = http_request_to_item(req);
    // 2. Call handle(request_item) → response_item
    Item response_item = call_lambda_function(handler->func, request_item);
    // 3. Convert response_item → HttpResponse
    item_to_http_response(response_item, resp);
    return 0;
}
```

#### Lambda Handler Script Interface

A Lambda handler is a `.ls` script that exports a `handle` procedure:

```lambda
// api/users.ls
import .lib.db

pn handle(request) -> response {
    match request.method {
        "GET" -> {
            let users = db.query("SELECT * FROM users")
            { status: 200, headers: { "Content-Type": "application/json" }, body: users }
        }
        "POST" -> {
            let user = request.body |> json.parse
            db.insert("users", user)
            { status: 201, body: { id: user.id, created: true } }
        }
        _ -> { status: 405, body: { error: "Method not allowed" } }
    }
}
```

The request/response maps are unified across all languages (see §8):

```lambda
// request map shape (same structure for all backends)
request = {
    method: "GET",
    path: "/api/users",
    query: { page: "1", limit: "10" },
    headers: { "content-type": "application/json", "authorization": "Bearer ..." },
    body: "...",           // raw body string
    params: { id: "42" }, // route parameters
    ip: "127.0.0.1"
}

// response map shape
response = {
    status: 200,
    headers: { "Content-Type": "application/json" },
    body: ...              // string, map (auto-serialized to JSON), or binary
}
```

#### 5.4.2 JavaScript Backend (In-Process JIT)

| Property | Value |
|----------|-------|
| Execution | In-process via Lambda JS transpiler → MIR |
| Extensions | `.js` |
| Startup cost | ~5ms (transpile + JIT compile) |
| Per-request cost | ~0.02ms (native function call) |
| Hot reload | Check mtime, retranspile if changed |

Same interface as Lambda: exports `handle(request)` returning response object.

#### 5.4.3 Python Backend (Subprocess + WSGI)

| Property | Value |
|----------|-------|
| Execution | Persistent subprocess via `lib/shell.h` |
| Extensions | `.py` |
| Protocol | CGI environment variables + stdin/stdout |
| WSGI support | Via `wsgi_bridge.py` shim |
| Startup cost | ~50ms (Python interpreter startup, amortized with persistent workers) |
| Per-request cost | ~2-5ms (IPC overhead) |

For simple Python scripts, use standard CGI interface (env vars + stdin/stdout). For Flask/Django apps, use the WSGI bridge:

```python
# wsgi_bridge.py — ships with Lambda web server
import sys, os, io

def run_wsgi(app_module, app_name='app'):
    mod = __import__(app_module)
    app = getattr(mod, app_name)

    environ = {
        'REQUEST_METHOD': os.environ.get('REQUEST_METHOD', 'GET'),
        'PATH_INFO': os.environ.get('PATH_INFO', '/'),
        'QUERY_STRING': os.environ.get('QUERY_STRING', ''),
        'CONTENT_TYPE': os.environ.get('CONTENT_TYPE', ''),
        'CONTENT_LENGTH': os.environ.get('CONTENT_LENGTH', '0'),
        'wsgi.input': sys.stdin.buffer,
        'wsgi.errors': sys.stderr,
        'SERVER_NAME': os.environ.get('SERVER_NAME', 'localhost'),
        'SERVER_PORT': os.environ.get('SERVER_PORT', '8080'),
        'wsgi.url_scheme': 'http',
    }
    for key, val in os.environ.items():
        if key.startswith('HTTP_'):
            environ[key] = val

    def start_response(status, headers):
        print(f"Status: {status}")
        for name, val in headers:
            print(f"{name}: {val}")
        print()

    result = app(environ, start_response)
    for chunk in result:
        sys.stdout.buffer.write(chunk)

if __name__ == '__main__':
    run_wsgi(sys.argv[1] if len(sys.argv) > 1 else 'app')
```

**Persistent Python workers**: Instead of spawning a new Python process per request, pre-fork `N` Python workers at server startup. Each worker reads requests from a pipe, executes via WSGI, and writes responses back. Workers stay alive across requests, amortizing Python startup cost.

#### 5.4.4 Bash Backend (Subprocess CGI)

| Property | Value |
|----------|-------|
| Execution | Subprocess via `lib/shell.h` |
| Extensions | `.sh` |
| Protocol | CGI environment variables + stdin/stdout |
| Startup cost | ~2ms |
| Per-request cost | ~2-5ms |

Simple CGI model — each request spawns a Bash process (or uses a persistent worker).

#### 5.4.5 Future Backends (Not Yet Implemented)

| Language | Protocol | Notes |
|----------|----------|-------|
| Ruby | Rack (via subprocess) | Similar to WSGI bridge |
| PHP | FastCGI / PHP-FPM | Connect to external PHP-FPM pool |
| Perl | CGI (subprocess) | Standard CGI interface |
| Go | Plugin / subprocess | Compiled handlers |

Adding a new backend: implement the `LanguageBackend` struct, register it via `backend_registry_register()`.

### 5.5 CGI Environment Variables

All subprocess backends (Python, Bash, and future languages) receive request data via standard CGI/1.1 environment variables:

| Variable | Value |
|----------|-------|
| `REQUEST_METHOD` | GET, POST, PUT, DELETE, etc. |
| `REQUEST_URI` | Full URI with query string |
| `PATH_INFO` | Path portion after script name |
| `QUERY_STRING` | Query string without `?` |
| `CONTENT_TYPE` | Request Content-Type header |
| `CONTENT_LENGTH` | Request body length |
| `HTTP_*` | All other request headers (uppercased, `_` for `-`) |
| `SERVER_NAME` | Server hostname |
| `SERVER_PORT` | Server port |
| `REMOTE_ADDR` | Client IP address |
| `SCRIPT_FILENAME` | Absolute path to script |
| `DOCUMENT_ROOT` | Server document root |

Script writes HTTP response to stdout: headers (terminated by blank line), then body.

### 5.6 Handler Configuration

```cpp
struct HandlerConfig {
    const char *url_prefix;         // e.g., "/api/"
    const char *script_dir;         // filesystem directory
    int hot_reload;                 // recompile on file change (dev mode)
    int timeout_ms;                 // per-request timeout (default: 30000)
    const char *python_path;        // default: "python3"
    const char *bash_path;          // default: "bash"
    int worker_count;               // persistent workers for subprocess backends
};
```

### 5.7 Security Considerations

- **Path sanitization**: prevent traversal outside `script_dir`
- **Execution whitelist**: only execute files with known extensions registered in backend registry
- **Timeout enforcement**: kill worker if it exceeds `timeout_ms`
- **Resource limits**: cap stdout capture size to prevent memory exhaustion
- **No shell expansion**: pass script path directly to interpreter, not through shell
- **Environment isolation**: do not leak server internals into CGI env

---

## 6. Router Design

### 6.1 Route Pattern Syntax

Support Express-style route patterns:

| Pattern | Example | Matches |
|---------|---------|---------|
| Exact | `/api/health` | `/api/health` only |
| Named param | `/users/:id` | `/users/42` → `{id: "42"}` |
| Optional param | `/users/:id?` | `/users` and `/users/42` |
| Wildcard | `/files/*path` | `/files/a/b/c` → `{path: "a/b/c"}` |
| Regex constraint | `/users/:id(\\d+)` | `/users/42` but not `/users/abc` |

### 6.2 Trie-Based Router

Use a radix trie for O(path-length) matching:

```cpp
struct RouteNode {
    char *segment;              // static segment or parameter name
    int is_param;               // :name parameter
    int is_wildcard;            // *name catch-all
    char *regex_constraint;     // optional regex for params
    RequestHandler handlers[9]; // indexed by HttpMethod bit position
    void *handler_data[9];
    RouteNode *children;
    RouteNode *next;            // sibling
};

// New API (replaces current exact-match routing)
int server_route(Server *server, HttpMethod method,
                const char *pattern, RequestHandler handler, void *user_data);

// Convenience macros
#define server_get(s, p, h, d)    server_route(s, HTTP_GET, p, h, d)
#define server_post(s, p, h, d)   server_route(s, HTTP_POST, p, h, d)
#define server_put(s, p, h, d)    server_route(s, HTTP_PUT, p, h, d)
#define server_delete(s, p, h, d) server_route(s, HTTP_DELETE, p, h, d)
```

### 6.3 Route Groups / Sub-Routers

```cpp
// Create a sub-router with shared prefix and middleware
Router* router_create(const char *prefix);
int router_use(Router *router, Middleware middleware, void *data);
int router_route(Router *router, HttpMethod method,
                const char *pattern, RequestHandler handler, void *data);
int server_mount(Server *server, Router *router);
```

---

## 7. Middleware Pipeline

### 7.1 Design

Middleware functions run before or around the handler. They can:
- Modify request/response
- Short-circuit the pipeline (e.g., auth failure)
- Wrap the handler (e.g., timing, error catching)
- Run after the handler (e.g., logging, compression)

```cpp
// Middleware signature: receives next() callback to continue pipeline
typedef void (*MiddlewareFn)(HttpRequest *req, HttpResponse *resp,
                             void *user_data, void (*next)(void *ctx), void *ctx);

// Register middleware
int server_use(Server *server, MiddlewareFn middleware, void *user_data);
int server_use_path(Server *server, const char *prefix,
                   MiddlewareFn middleware, void *user_data);
```

### 7.2 Built-In Middleware

| Middleware | Purpose |
|-----------|---------|
| `mw_logger` | Request logging (method, path, status, duration) |
| `mw_cors` | CORS headers (`Access-Control-Allow-*`) |
| `mw_body_parser_json` | Parse `application/json` body into `req->parsed_body` |
| `mw_body_parser_form` | Parse `application/x-www-form-urlencoded` body |
| `mw_static` | Static file serving (wraps `server_set_static()`) |
| `mw_compress` | Gzip/deflate response compression |
| `mw_rate_limit` | Request rate limiting per IP |
| `mw_auth_basic` | HTTP Basic authentication |
| `mw_error_handler` | Catch panics/errors and return 500 |

### 7.3 Execution Order

```
Request → [Global Middleware 1] → [Global Middleware 2] → [Route Middleware] → [Handler]
                                                                                    ↓
Response ← [Global Middleware 1] ← [Global Middleware 2] ← [Route Middleware] ← [Result]
```

---

## 8. Request/Response API Extensions

### 8.1 Extended Request

The `HttpRequest` struct unifies concepts from **Node.js `IncomingMessage`**, **Flask `request`**, and **Express `req`** into a single C+ struct:

```cpp
struct HttpRequest {
    // --- Core (Node.js IncomingMessage-like) ---
    HttpMethod method;
    char *uri;                    // full URI
    char *path;                   // path without query
    char *query_string;           // raw query string
    HttpHeader *headers;          // all headers
    char *body;                   // raw body
    size_t body_len;
    int http_version_major;
    int http_version_minor;

    // --- Route params (Express req.params) ---
    HttpHeader *params;           // :id, *path from route matching

    // --- Query params (Express req.query / Flask request.args) ---
    HttpHeader *query_params;     // parsed key=value from query string

    // --- Parsed body (Express req.body after body-parser / Flask request.json) ---
    void *parsed_body;            // JSON or form data
    int parsed_body_type;         // BODY_JSON, BODY_FORM

    // --- Cookies (Express req.cookies / Flask request.cookies) ---
    HttpHeader *cookies;          // parsed Cookie header

    // --- Client info (Express req.ip / Flask request.remote_addr) ---
    char remote_addr[64];
    int remote_port;
};

// Convenience accessors (inspired by both Express and Flask)
const char* http_request_param(HttpRequest *req, const char *name);   // Express: req.params.name
const char* http_request_query(HttpRequest *req, const char *name);   // Express: req.query.name / Flask: request.args.get(name)
const char* http_request_header(HttpRequest *req, const char *name);  // Express: req.get(name) / Flask: request.headers.get(name)
const char* http_request_cookie(HttpRequest *req, const char *name);  // Express: req.cookies.name / Flask: request.cookies.get(name)
const char* http_request_cookie(http_request_t *req, const char *name);
```

### 8.2 Extended Response

Unified from **Node.js `ServerResponse`**, **Express `res`**, and **Flask `Response`**:

```cpp
// Convenience methods (Express-style chaining translated to C+ calls)
void http_response_json(HttpResponse *resp, const char *json_str);    // Express: res.json()
void http_response_html(HttpResponse *resp, const char *html_str);    // Express: res.send() with HTML
void http_response_text(HttpResponse *resp, const char *text);        // Express: res.send() with text
void http_response_set_cookie(HttpResponse *resp, const char *name,
                              const char *value, const CookieOptions *opts);  // Express: res.cookie()
void http_response_redirect(HttpResponse *resp, int status, const char *url); // Express: res.redirect()

// Streaming (Node.js-style)
void http_response_start_sse(HttpResponse *resp);
void http_response_send_event(HttpResponse *resp, const char *event,
                              const char *data);
```

---

## 9. Framework Compatibility Analysis

### 9.1 Express.js Compatibility

**Goal**: Run Express-style code written in JavaScript under the Lambda JS transpiler.

#### Express Core Concepts Mapping

| Express Concept | Lambda Web Server Equivalent |
|----------------|-------------------------------|
| `const app = express()` | `io.http.create_server(config)` |
| `app.get(path, handler)` | `server_route(server, HTTP_GET, path, handler, data)` |
| `app.use(middleware)` | `server_use(server, middleware, data)` |
| `app.use(express.json())` | `server_use(server, mw_body_parser_json, NULL)` |
| `app.use(express.static('public'))` | `server_set_static(server, "/", "public", NULL)` |
| `app.listen(3000)` | `server_start(server); server_run(server)` |
| `req.params.id` | `http_request_param(req, "id")` |
| `req.query.page` | `http_request_query(req, "page")` |
| `req.body` | `req->parsed_body` |
| `res.json({...})` | `http_response_json(resp, str)` |
| `res.status(201).send(...)` | `http_response_set_status(resp, 201); http_response_send(resp)` |
| `res.redirect(url)` | `http_response_redirect(resp, 302, url)` |
| Router sub-routes | `router_create()` + `server_mount()` |

#### Feasibility Assessment: Express

**Feasibility: PARTIAL — High-value subset achievable; full API unlikely**

What CAN be compatible:
- ✅ Route definitions (`app.get`, `app.post`, etc.)
- ✅ Middleware chain (`app.use`)
- ✅ `req`/`res` object shape for common properties
- ✅ Static file serving
- ✅ JSON body parsing
- ✅ Route parameters (`:id`)
- ✅ Query parsing

What CANNOT run unmodified:
- ❌ **npm ecosystem**: Express middleware from npm (e.g., passport, cors, helmet) relies on Node.js runtime APIs (`Buffer`, `Stream`, `EventEmitter`, `process`, `fs`, `crypto`). These are not available in the Lambda JS runtime.
- ❌ **async/await on I/O**: Express handlers typically use `async/await` with Node.js APIs (`fs.promises`, `fetch`, database drivers). Lambda JS transpiler would need equivalent async primitives.
- ❌ **Prototype-based req/res**: Express monkey-patches `req` and `res` with methods. Lambda JS objects are `Item` maps without prototype chains.
- ❌ **Error middleware** `(err, req, res, next)` 4-arg signature detection is a runtime pattern.

**Recommended approach**: Provide a **Lambda Express compatibility module** (`express.ls` or `express.js`) that wraps the Lambda web server API to match Express conventions:

```javascript
// express_compat.js — provided as a Lambda standard library module
export function express() {
    let routes = [];
    let middleware = [];
    return {
        get: (path, handler) => routes.push({method: "GET", path, handler}),
        post: (path, handler) => routes.push({method: "POST", path, handler}),
        use: (fn) => middleware.push(fn),
        listen: (port, cb) => { /* wire into server_start */ }
    };
}
```

### 9.2 Flask Compatibility

**Goal**: Run Flask applications via the **WSGI bridge** and **persistent worker pool**.

#### Flask Core Concepts Mapping

| Flask Concept | Lambda Web Server Equivalent |
|--------------|-------------------------------|
| `@app.route('/path')` | Route registration → worker pool dispatch |
| `request.args` | Query params from `QUERY_STRING` env |
| `request.form` | Parsed form body |
| `request.json` | Parsed JSON body from stdin |
| `jsonify(data)` | JSON serialization to stdout |
| `render_template('t.html', **ctx)` | Jinja2 (requires Python dep) |
| `abort(404)` | Set HTTP status in CGI response |
| `redirect(url)` | `Location` header in CGI response |

#### Feasibility Assessment: Flask

**Feasibility: GOOD — WSGI bridge with worker pool makes this practical**

What CAN work:
- ✅ Simple route → handler mapping via WSGI bridge
- ✅ Request data access via WSGI environ
- ✅ JSON responses via WSGI start_response
- ✅ Template rendering (Jinja2 available if Python is installed)
- ✅ Flask sessions and cookies via WSGI
- ✅ **Persistent Python workers** eliminate per-request process spawn cost

What requires adaptation:
- ⚠️ **Flask extensions**: most Flask extensions (Flask-SQLAlchemy, Flask-Login) require pip packages and a persistent Python process — this works with the worker pool model
- ⚠️ **Application lifecycle**: Flask expects a long-lived `app` object — worker pool preserves this across requests

**Implementation**: The WSGI bridge (see §5.4.3) runs inside persistent Python workers. The Flask `app` object is created once per worker and reused across requests. This is the same model as Gunicorn's sync worker.

### 9.3 Async Frameworks (ASGI + Node.js/Express) — Deferred to Future Phase

**Status**: DEFERRED. Async frameworks — both Python (FastAPI, Starlette) and Node.js (Express with async middleware) — require fundamentally different execution semantics than the synchronous worker pool model.

#### Why Deferred

**Python ASGI** frameworks (FastAPI, Starlette, Django Channels) require:
1. **ASGI** (Asynchronous Server Gateway Interface) — async event loop in Python
2. **Pydantic** — runtime type validation using Python type annotations
3. **Starlette** — async HTTP toolkit with WebSocket, background tasks
4. **Auto-documentation** — OpenAPI/Swagger generation from type hints

**Node.js/Express** requires:
1. **Event loop** — Node.js's libuv-based event loop (separate from the server's libuv loop)
2. **npm ecosystem** — middleware from npm (passport, cors, helmet) relies on Node.js runtime APIs (`Buffer`, `Stream`, `EventEmitter`, `process`, `fs`, `crypto`)
3. **async/await on I/O** — Express handlers use `async/await` with Node.js APIs (`fs.promises`, `fetch`, database drivers)
4. **Prototype-based req/res** — Express monkey-patches `req` and `res` with methods via prototype chains

These cannot run under the synchronous WSGI/CGI worker pool model used in the current phase.

#### Future Async Phase Plan

**Python ASGI support**:
1. Embed a Python async event loop inside each worker
2. Use the ASGI protocol (`receive`/`send` callables) instead of CGI env vars
3. Support FastAPI, Starlette, and Django Channels
4. Enable WebSocket passthrough from libuv to ASGI

**Node.js/Express support**:
1. Embed a Node.js runtime (or a V8-based JS engine with Node.js API shims) as a language backend
2. Bridge libuv events between the server's event loop and Node.js's event loop
3. Provide Express-compatible `req`/`res` objects backed by the server's `HttpRequest`/`HttpResponse`
4. Support npm packages via a bundled `node_modules` resolution strategy
5. Alternative: run Node.js as a persistent subprocess (like Python WSGI workers) with IPC-based request forwarding

For now:
- Users needing **FastAPI** should run it under uvicorn externally
- Users needing **full Express/Node.js** should run it under Node.js externally
- Lambda server can act as a **reverse proxy** to either if co-hosting is needed
- The current Express compat shim (§9.1) covers simple Express-style routing for JS scripts running under the Lambda JS transpiler

### 9.4 Compatibility Summary

| Framework      | Approach                                          | Phase   | Fidelity             |
| -------------- | ------------------------------------------------- | ------- | -------------------- |
| **Express.js** | Compat module wrapping `io.http` server API       | Current | High for simple apps |
| **Flask**      | WSGI bridge via persistent worker pool             | Current | Good                 |
| **Django**     | WSGI bridge via persistent worker pool             | Current | Good (sync views)    |
| **FastAPI**    | ASGI support required                             | Future  | Deferred             |
| **Starlette**  | ASGI support required                             | Future  | Deferred             |
| **Express (full Node.js)** | Embedded Node.js runtime or subprocess   | Future  | Deferred             |

---

## 10. Proposed Module API — Lambda Script Interface

### 10.1 The `io.http` Module

Expose the web server as a Lambda system module at `io.http`:

```lambda
import io.http

pn main() {
    let app = io.http.create_server({
        port: 3000,
        ssl_port: 3443,
        ssl_cert: "./certs/cert.pem",
        ssl_key: "./certs/key.pem",
        workers: 4
    })

    // middleware
    app.use(io.http.logger())
    app.use(io.http.cors({ origin: "*" }))
    app.use(io.http.json_parser())

    // static files
    app.static("/public", "./static")

    // routes
    app.get "/" fn(req) {
        { body: "Hello, Lambda!" }
    }

    app.get "/api/users/:id" fn(req) {
        let id = req.params.id
        let user = db.find(id)
        match user {
            null -> { status: 404, body: { error: "Not found" } }
            _    -> { body: user }
        }
    }

    app.post "/api/users" fn(req) {
        let user = req.body
        db.insert(user)
        { status: 201, body: user }
    }

    // route groups
    let api = app.router("/api/v2")
    api.use(io.http.auth_bearer(verify_token))
    api.get "/items" fn(req) { ... }
    api.post "/items" fn(req) { ... }
    app.mount(api)

    // handler directory (auto-maps scripts by file extension via backend registry)
    app.handlers("/api", "./handlers", { hot_reload: true })

    // start
    app.listen() |> print   // "Server listening on :3000"
}
```

### 10.2 CLI Integration

```bash
# Run a Lambda web server script
./lambda.exe serve app.ls

# With options
./lambda.exe serve app.ls --port 3000 --host 0.0.0.0

# Quick static file server
./lambda.exe serve --static ./public --port 8080

# Development mode (hot reload + verbose logging)
./lambda.exe serve app.ls --dev
```

### 10.3 System Functions

Register these as Lambda system functions under the `io.http` namespace:

| Function | Signature | Description |
|----------|-----------|-------------|
| `io.http.create_server` | `(config: map) -> App` | Create server instance |
| `io.http.logger` | `() -> Middleware` | Request logging middleware |
| `io.http.cors` | `(config: map) -> Middleware` | CORS middleware |
| `io.http.json_parser` | `() -> Middleware` | JSON body parser |
| `io.http.form_parser` | `() -> Middleware` | Form body parser |
| `io.http.auth_basic` | `(verifier: fn) -> Middleware` | Basic auth middleware |
| `io.http.auth_bearer` | `(verifier: fn) -> Middleware` | Bearer token auth middleware |
| `io.http.rate_limit` | `(config: map) -> Middleware` | Rate limiter |
| `io.http.html` | `(template: string, data: map) -> string` | Template rendering |

---

## 11. C+ API Summary — Source Files

### 11.1 New Source Files (all in `lambda/serve/`, C+ `.cpp`/`.hpp`)

| File | Purpose |
|------|---------|
| `lambda/serve/serve_types.hpp` | Core types: `HttpMethod`, `HttpStatus`, `HttpHeader`, `ServerConfig` |
| `lambda/serve/http_request.hpp/.cpp` | Unified HTTP request (Node.js + Flask inspired) |
| `lambda/serve/http_response.hpp/.cpp` | Unified HTTP response with streaming support |
| `lambda/serve/router.hpp/.cpp` | Trie-based parameterized router |
| `lambda/serve/middleware.hpp/.cpp` | Middleware pipeline engine |
| `lambda/serve/static_handler.hpp/.cpp` | Static file serving handler |
| `lambda/serve/worker_pool.hpp/.cpp` | Pre-forked worker pool (PHP-FPM model) |
| `lambda/serve/language_backend.hpp` | Language backend registry interface |
| `lambda/serve/backend_lambda.cpp` | Lambda Script JIT backend |
| `lambda/serve/backend_js.cpp` | JavaScript JIT backend |
| `lambda/serve/backend_python.cpp` | Python subprocess + WSGI backend |
| `lambda/serve/backend_bash.cpp` | Bash subprocess CGI backend |
| `lambda/serve/body_parser.hpp/.cpp` | JSON and form body parsing |
| `lambda/serve/cookie.hpp/.cpp` | Cookie parsing and serialization |
| `lambda/serve/mime.hpp/.cpp` | MIME detection (integrated from `lib/mime-detect.h`) |
| `lambda/serve/server.hpp/.cpp` | Server lifecycle, connection management, libuv integration |
| `lambda/serve/tls_handler.hpp/.cpp` | TLS via mbedTLS |
| `lambda/serve/serve_utils.hpp/.cpp` | Error handling, logging, string/time utils |
| `lambda/http_module.cpp` | Lambda `io.http` module — bridges C+ server API to Lambda Items |
| `lambda/serve/wsgi_bridge.py` | WSGI bridge for Flask/Django apps |
| `lambda/serve/express_compat.js` | Express.js compatibility shim |

### 11.2 Modified Files

| File | Changes |
|------|---------|
| `lambda/sys_func_registry.c` | Register `io.http.*` system functions |
| `lambda/main.cpp` | Add `serve` CLI subcommand |
| `build_lambda_config.json` | Add `lambda/serve/*.cpp` to build |

---

## 12. Implementation Phases

### Phase 1: Core Transport & Protocol (Foundation)

- [ ] Migrate `lib/serve/` → `lambda/serve/` as C+ `.cpp`/`.hpp` files
- [ ] Connection timeouts via `uv_timer_t`
- [ ] HTTP/1.1 keep-alive
- [ ] Request size limits
- [ ] Unified `HttpRequest` / `HttpResponse` structs (Node.js + Flask inspired)
- [ ] Response convenience methods (`json`, `html`, `redirect`, cookies)
- [ ] Chunked transfer encoding
- [ ] Integrate MIME detection into `lambda/serve/mime.hpp`

### Phase 2: Router + Static Files + Body Parsing

- [ ] Trie-based parameterized router (replace linked-list exact match)
- [ ] Route groups / sub-routers with `server_mount()`
- [ ] `server_set_static()` with directory serving
- [ ] ETag / Last-Modified conditional responses
- [ ] Range request support
- [ ] JSON body parser
- [ ] URL-encoded form body parser
- [ ] Cookie parsing / Set-Cookie generation

### Phase 3: Worker Pool + Language Backends

- [ ] Worker pool engine (`worker_pool.cpp`) — pre-forked workers via `uv_queue_work`
- [ ] Language backend registry (`language_backend.hpp`)
- [ ] Lambda Script backend (in-process JIT, compiled handler caching)
- [ ] JavaScript backend (in-process JIT via transpiler)
- [ ] Python backend (persistent subprocess workers + CGI env)
- [ ] Bash backend (subprocess CGI)
- [ ] Hot reload in development mode (mtime checking)
- [ ] Per-request timeout enforcement

### Phase 4: Middleware Pipeline

- [ ] Middleware pipeline engine with `next()` continuation
- [ ] Built-in middleware: logger, CORS, error handler
- [ ] Rate limiting middleware
- [ ] Authentication middleware (basic, bearer)
- [ ] Compression middleware (gzip/deflate)

### Phase 5: Lambda Integration + Framework Compat

- [ ] `io.http` system module for Lambda scripts
- [ ] `lambda serve` CLI subcommand
- [ ] Lambda route handler interface (request/response maps)
- [ ] WSGI bridge for Flask/Django (`wsgi_bridge.py`)
- [ ] Express.js compatibility shim (`express_compat.js`)
- [ ] Template rendering integration
- [ ] Documentation and examples

### Phase 6: Future (Deferred)

- [ ] Multipart form data / file uploads (`multipart/form-data` parsing)
- [ ] WebSocket support (upgrade, frame parsing, ping/pong)
- [ ] HTTP/2 server-side (ALPN negotiation, frame multiplexing)
- [ ] ASGI support for async Python frameworks (FastAPI, Starlette)
- [ ] Native Node.js/Express backend (embedded runtime or persistent subprocess)
- [ ] Server-Sent Events (SSE) from handler scripts
- [ ] Reverse proxy mode
- [ ] Additional language backends (Ruby/Rack, PHP/FastCGI, Perl/CGI)
- [ ] Graceful shutdown with request draining

---

## 13. Performance Considerations

### Worker Pool Model

All request handlers execute on the libuv thread pool via `uv_queue_work()`. This ensures the event loop stays non-blocking regardless of handler execution time.

- **Default pool size**: `UV_THREADPOOL_SIZE` (4 threads). Configurable at startup.
- **Queuing**: when all workers are busy, requests queue up. The `max_queue_length` config prevents unbounded growth.
- **Timeout**: per-request timeout kills hung workers and returns 504 Gateway Timeout.

### In-Process Lambda/JS Handlers

- **Zero subprocess overhead**: JIT-compiled handlers execute as native function calls on worker threads
- **Shared memory**: no serialization/deserialization — `Item` maps pass directly between server and handler
- **Compiled once**: scripts compiled on first request, cached for subsequent requests
- **GC coordination**: per-worker GC nursery scoping to avoid cross-request GC pressure

### Persistent Subprocess Workers (Python/Bash)

- **Amortized startup**: Python workers start once and handle many requests (like PHP-FPM)
- **IPC overhead**: ~2-5ms per request for pipe-based communication
- **Worker recycling**: restart workers after N requests or on memory threshold to prevent leaks

### Static Files

- **sendfile optimization**: for large files, use `uv_fs_sendfile()` for zero-copy kernel-level transfer
- **In-memory cache**: optional LRU cache for frequently accessed small files
- **ETag shortcut**: avoid reading file content when ETag matches

---

## 14. Design Decisions (Resolved)

| # | Question | Decision |
|---|---------|----------|
| 1 | Persistent workers vs. per-request CGI? | **Worker pool** (PHP-FPM model). All handlers — including Lambda sync — run via `uv_queue_work`. |
| 2 | Lambda async I/O in handlers? | **Worker pool** solves this. Sync handlers run on thread pool, keeping event loop free. |
| 3 | WebSocket support priority? | **Deferred** to Phase 6. Focus on HTTP/1.1 first. |
| 4 | HTTP/2 server? | **Deferred** to Phase 6. HTTP/1.1 must be stable first. |
| 5 | Multipart file uploads? | **Deferred** to Phase 6. JSON + form-urlencoded body parsing in current scope. |
| 6 | ASGI + Node.js async? | **Deferred** to Phase 6. WSGI (sync) in current scope; ASGI (async Python) and native Node.js/Express in future. |
| 7 | Module name? | **`io.http`** — e.g., `import io.http; io.http.create_server(...)` |
| 8 | Source code location? | **`lambda/serve/`** — C+ `.cpp`/`.hpp` files (migrated from `lib/serve/`) |

## 15. Open Questions (Remaining)

1. **HTTPS client certificate auth**: The TLS handler supports `verify_peer`. Should we expose client certificate info to handlers for mutual TLS (mTLS)?

2. **Reverse proxy / load balancer**: Should the server support proxying requests to backend services? This would enable using Lambda server as a gateway.

3. **Graceful shutdown**: Current `server_stop()` calls `uv_stop()`. Should we drain in-flight requests before stopping? (Implement `uv_walk` with drain timeout.)

4. **Configuration from Lambda data**: Should server config be expressible as a Lambda data file (`.ls` or `.json`) instead of only C structs?
   ```bash
   ./lambda.exe serve --config server.json
   ```

5. **Worker pool sizing strategy**: Static pool size, or auto-scale based on load? Start with static (configurable via `workers` config), consider auto-scaling later.

6. **Python worker persistence mechanism**: Use pipes (stdin/stdout per worker) or Unix domain sockets? Pipes are simpler; Unix sockets allow multiplexing.

7. **Test strategy**: New components need:
    - Unit tests for router pattern matching
    - Integration tests for static file serving (ETag, ranges, directory listing)
    - Worker pool tests with sample handlers in all languages
    - Middleware pipeline ordering tests
    - WSGI bridge integration tests with a minimal Flask app
    - Load/stress tests for connection handling and keep-alive
