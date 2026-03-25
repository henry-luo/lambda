# Transpile_Js15: libuv Migration — Async I/O, Server Rewrite, True Async/Await

## 1. Executive Summary

v14 established the async runtime foundation: a custom event loop with timer min-heap,
microtask FIFO queue, synchronous Promises, and ES modules. However, several critical
limitations remain:

| Problem | Current State | Impact |
|---------|:------------:|--------|
| No true async I/O | `select()`-based sleep in custom event loop | Cannot do non-blocking file/network ops |
| HTTPS broken | libevent's `bufferevent_ssl` requires OpenSSL; Lambda uses mbedTLS | `tls_create_bufferevent()` returns NULL |
| No `fetch()` API | libcurl exists but no async integration | JS can't make HTTP requests |
| No async file I/O | Lambda has synchronous `read_text_file()` only | Blocks entire runtime on disk I/O |
| No child processes | Not implemented | Can't spawn external commands |
| Generator state machine deferred | `yield` is pass-through, not true suspend/resume | `function*` doesn't work correctly |
| Promise `.then()` not microtask-scheduled | Callbacks called directly, not queued | Ordering differs from spec |
| Redundant event libs | libevent linked but only used by `lib/serve/` | Unnecessary binary bloat + dependency |

**v15 goal:** Replace both libevent (`lib/serve/`) and the custom event loop (`lambda/js/`)
with **libuv** as the unified async I/O platform, enabling a full Node.js-compatible async
runtime.

### Why libuv Now?

In v14, libuv was deferred because the JS runtime only needed timers and microtasks.
Now that the async foundation is proven, v15 has concrete requirements that justify libuv:

| Capability | Custom Loop (v14) | libuv (v15) | libevent (current) |
|-----------|:-:|:-:|:-:|
| Timer queue | ✅ 256-entry min-heap | ✅ `uv_timer_t` (unlimited) | ✅ `evtimer_*` |
| Microtask queue | ✅ 1024-entry ring buffer | ✅ Custom (on top of `uv_async_t`) | ❌ |
| TCP server | ❌ | ✅ `uv_tcp_t` + `uv_listen` | ✅ `evhttp` |
| TLS/SSL | ❌ | ✅ Manual mbedTLS on `uv_tcp_t` | ❌ Requires OpenSSL |
| Async file I/O | ❌ | ✅ `uv_fs_*` (threadpool) | ❌ |
| Child processes | ❌ | ✅ `uv_spawn` | ❌ |
| Async HTTP | ❌ | ✅ `curl_multi` + `uv_poll_t` | ❌ |
| File watching | ❌ | ✅ `uv_fs_event_t` | ❌ |
| DNS resolution | ❌ | ✅ `uv_getaddrinfo` | ❌ |
| Cross-platform | ✅ (POSIX `select()`) | ✅ (epoll/kqueue/IOCP) | ⚠️ (no mbedTLS) |

**Key insight:** libuv replaces **two** existing dependencies (libevent + custom event loop)
with one unified platform, while also solving the HTTPS/mbedTLS incompatibility that has
blocked TLS support since the server was written.

### Architecture Overview

```
┌──────────────────────────────────────────────────────────────────┐
│                       JS Execution Layer                         │
│                                                                  │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────────┐   │
│  │Generators│ │async/    │ │ES Modules│ │  Node.js APIs     │   │
│  │(state    │ │await     │ │(loader)  │ │ fs, http, child   │   │
│  │ machine) │ │(Promise) │ │          │ │ process, fetch    │   │
│  └────┬─────┘ └────┬─────┘ └────┬─────┘ └────────┬─────────┘   │
│       │             │            │                 │             │
│  ┌────▼─────────────▼────────────▼─────────────────▼─────────┐  │
│  │                  Microtask Queue (custom)                  │  │
│  │            FIFO ring buffer, flushed per macrotask         │  │
│  └─────────────────────────┬─────────────────────────────────┘  │
│                             │                                    │
│  ┌──────────────────────────▼────────────────────────────────┐  │
│  │                    libuv Event Loop                        │  │
│  │                                                            │  │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌─────────────┐  │  │
│  │  │uv_timer_t│ │uv_tcp_t  │ │uv_fs_*   │ │uv_process_t │  │  │
│  │  │(setTimeout│ │(HTTP     │ │(file I/O)│ │(child_proc) │  │  │
│  │  │setInterval│ │ server)  │ │          │ │             │  │  │
│  │  └──────────┘ └──────────┘ └──────────┘ └─────────────┘  │  │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐                  │  │
│  │  │uv_poll_t │ │uv_async_t│ │uv_signal │                  │  │
│  │  │(curl_    │ │(wakeup   │ │(graceful │                  │  │
│  │  │ multi)   │ │ from JS) │ │ shutdown)│                  │  │
│  │  └──────────┘ └──────────┘ └──────────┘                  │  │
│  └───────────────────────────────────────────────────────────┘  │
│                                                                  │
├──────────────────────────────────────────────────────────────────┤
│   ┌────────────┐  ┌────────────┐  ┌─────────────────────────┐   │
│   │  mbedTLS   │  │  libcurl   │  │  Lambda Runtime          │   │
│   │  (TLS/SSL) │  │  (HTTP/2)  │  │  (GC, MIR JIT, Items)   │   │
│   └────────────┘  └────────────┘  └─────────────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
```

---

## 2. Feature Design

### 2.1 libuv Integration Layer (Phase 1)

The foundation: add libuv as a build dependency and create a thin abstraction layer that
the JS runtime, HTTP server, and Lambda CLI all share.

#### 2.1.1 Build System Changes

libuv is built from source (static link) on all three platforms:

**macOS (setup-mac-deps.sh):**
```bash
# libuv — build from source, static link
LIBUV_VERSION="1.50.0"
if [ ! -f "mac-deps/libuv-install/lib/libuv.a" ]; then
    cd mac-deps
    curl -L "https://github.com/libuv/libuv/archive/refs/tags/v${LIBUV_VERSION}.tar.gz" \
        -o libuv.tar.gz
    tar xzf libuv.tar.gz
    cd "libuv-${LIBUV_VERSION}"
    mkdir -p build && cd build
    cmake .. -DCMAKE_INSTALL_PREFIX=../../libuv-install \
             -DBUILD_TESTING=OFF -DLIBUV_BUILD_SHARED=OFF \
             -DCMAKE_C_FLAGS="-fPIC"
    make -j$(sysctl -n hw.ncpu) && make install
    cd ../../..
fi
```

**Linux (setup-linux-deps.sh):**
```bash
LIBUV_VERSION="1.50.0"
if [ ! -f "/usr/local/lib/libuv.a" ]; then
    curl -L "https://github.com/libuv/libuv/archive/refs/tags/v${LIBUV_VERSION}.tar.gz" \
        -o /tmp/libuv.tar.gz
    tar xzf /tmp/libuv.tar.gz -C /tmp
    cd "/tmp/libuv-${LIBUV_VERSION}"
    mkdir -p build && cd build
    cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local \
             -DBUILD_TESTING=OFF -DLIBUV_BUILD_SHARED=OFF \
             -DCMAKE_C_FLAGS="-fPIC"
    make -j$(nproc) && sudo make install
fi
```

**Windows (setup-windows-deps.sh — MSYS2/Clang64):**
```bash
pacman -S --noconfirm mingw-w64-clang-x86_64-libuv
# Or build from source similar to macOS
```

**build_lambda_config.json changes:**
```json
{
    "name": "libuv",
    "include": "mac-deps/libuv-install/include",
    "lib": "mac-deps/libuv-install/lib/libuv.a",
    "type": "static"
}
```

Remove `libevent` and `libevent_openssl` entries. The CLI build profile's `exclude_libraries`
list removes `libevent` and `libevent_openssl` — replace with libuv exclusion only if
the CLI doesn't need server functionality.

#### 2.1.2 Core Event Loop Wrapper

A thin C wrapper bridges libuv's event loop with Lambda's microtask queue:

```c
// lib/uv_loop.h — unified event loop for Lambda

#ifndef LAMBDA_UV_LOOP_H
#define LAMBDA_UV_LOOP_H

#include <uv.h>

#ifdef __cplusplus
extern "C" {
#endif

// Global event loop (one per process, like Node.js)
uv_loop_t* lambda_uv_loop(void);

// Lifecycle
int  lambda_uv_init(void);       // create loop, return 0 on success
int  lambda_uv_run(void);        // run until no active handles
void lambda_uv_stop(void);       // stop loop (from signal handler)
void lambda_uv_cleanup(void);    // close all handles, free loop

// Microtask integration
// Called by libuv's prepare handle before each iteration
// to drain the JS microtask queue
void lambda_uv_set_microtask_drain(void (*drain_fn)(void));

#ifdef __cplusplus
}
#endif

#endif
```

```c
// lib/uv_loop.c

#include "uv_loop.h"

static uv_loop_t *g_loop = NULL;
static uv_prepare_t g_prepare;
static void (*g_microtask_drain)(void) = NULL;

static void prepare_cb(uv_prepare_t *handle) {
    // Drain microtasks before each event loop iteration
    if (g_microtask_drain) {
        g_microtask_drain();
    }
}

uv_loop_t* lambda_uv_loop(void) {
    return g_loop;
}

int lambda_uv_init(void) {
    g_loop = malloc(sizeof(uv_loop_t));
    if (!g_loop) return -1;
    int r = uv_loop_init(g_loop);
    if (r != 0) { free(g_loop); g_loop = NULL; return r; }

    // Install prepare handle for microtask draining
    uv_prepare_init(g_loop, &g_prepare);
    uv_prepare_start(&g_prepare, prepare_cb);
    uv_unref((uv_handle_t*)&g_prepare);  // don't keep loop alive

    return 0;
}

int lambda_uv_run(void) {
    if (!g_loop) return -1;
    return uv_run(g_loop, UV_RUN_DEFAULT);
}

void lambda_uv_stop(void) {
    if (g_loop) uv_stop(g_loop);
}

void lambda_uv_cleanup(void) {
    if (!g_loop) return;
    uv_prepare_stop(&g_prepare);
    uv_loop_close(g_loop);
    free(g_loop);
    g_loop = NULL;
}

void lambda_uv_set_microtask_drain(void (*drain_fn)(void)) {
    g_microtask_drain = drain_fn;
}
```

**Estimated LOC:** ~80 (header + implementation)

---

### 2.2 HTTP Server Migration: libevent → libuv (Phase 2)

The existing `lib/serve/server.c` (265 LOC) is rewritten to use libuv's `uv_tcp_t`
with a custom HTTP parser. This also fixes the TLS/HTTPS incompatibility with mbedTLS.

#### 2.2.1 API Surface Mapping

| libevent API (current) | libuv API (replacement) | Notes |
|------------------------|------------------------|-------|
| `event_base_new()` | `lambda_uv_init()` → `uv_loop_t*` | Shared global loop |
| `evhttp_new(base)` | `uv_tcp_init(loop, &server)` | TCP handle |
| `evhttp_bind_socket(http, addr, port)` | `uv_tcp_bind(&server, &addr, 0)` + `uv_listen()` | Manual bind+listen |
| `evhttp_set_timeout(http, sec)` | `uv_timer_t` per connection | Connection timeouts |
| `evhttp_set_gencb(http, cb, arg)` | `uv_connection_cb` → `uv_read_start()` | Accept + read |
| `event_base_dispatch(base)` | `lambda_uv_run()` → `uv_run(UV_RUN_DEFAULT)` | Main loop |
| `event_base_loopbreak(base)` | `lambda_uv_stop()` → `uv_stop()` | Graceful shutdown |
| `evhttp_free(http)` | `uv_close((uv_handle_t*)&server, NULL)` | Cleanup |
| `event_base_free(base)` | `lambda_uv_cleanup()` | Cleanup |
| `evbuffer_new()` / `evbuffer_add()` | `uv_buf_t` + `uv_write()` | Write path |
| `evhttp_send_reply()` | Custom HTTP response writer | Must format HTTP/1.1 |
| `evhttp_request_get_uri()` | Parse from raw read buffer | Need HTTP parser |

#### 2.2.2 HTTP Parser Choice

libevent includes a built-in HTTP parser (`evhttp`). With libuv, we need a standalone
HTTP parser. Options:

| Parser | LOC | License | Notes |
|--------|:---:|---------|-------|
| **llhttp** (Node.js) | ~4K | MIT | Official, used by Node.js, active development |
| **picohttpparser** | ~1K | MIT | Minimal, fast, header-only |
| **Custom** | ~500 | — | Only need request line + headers for a server |

**Recommendation: llhttp.** It's the same parser Node.js uses, battle-tested, and
supports HTTP/1.0, HTTP/1.1, chunked encoding, and keep-alive. It can be vendored
as a single `.c`/`.h` file.

#### 2.2.3 Server Architecture

```c
// lib/serve/server.h (revised)

#ifndef SERVE_SERVER_H
#define SERVE_SERVER_H

#include <uv.h>
#include "mbedtls_compat.h"

typedef struct server_t {
    server_config_t config;
    uv_tcp_t tcp_handle;        // HTTP listener
    uv_tcp_t tls_handle;        // HTTPS listener
    SSL_CTX *ssl_ctx;           // mbedTLS context
    int running;
    void *user_data;
} server_t;

typedef struct client_conn {
    uv_tcp_t handle;            // client TCP handle
    server_t *server;           // back-pointer to server
    SSL *ssl;                   // mbedTLS connection (NULL for HTTP)
    uv_timer_t timeout;         // connection timeout
    char *read_buf;             // accumulation buffer for HTTP parsing
    size_t read_buf_len;
    size_t read_buf_cap;
    bool headers_complete;      // HTTP headers fully parsed
    bool keep_alive;            // HTTP/1.1 keep-alive
} client_conn_t;
```

#### 2.2.4 TLS/HTTPS with mbedTLS (Fixing the Gap)

The core problem with the current implementation: libevent's `bufferevent_ssl` requires
OpenSSL's `SSL_*` API. Since Lambda uses mbedTLS, `tls_create_bufferevent()` returns NULL.

With libuv, TLS is handled at the application level:

```c
// TLS connection lifecycle on libuv:
// 1. Accept TCP connection via uv_listen() callback
// 2. Create mbedtls_ssl_context for the connection
// 3. Set bio callbacks to read/write through uv_tcp_t
// 4. Perform TLS handshake (mbedtls_ssl_handshake)
// 5. Read/write encrypted data through mbedtls_ssl_read/write
// 6. On close: mbedtls_ssl_close_notify, then uv_close

static int tls_bio_send(void *ctx, const unsigned char *buf, size_t len) {
    client_conn_t *conn = (client_conn_t *)ctx;
    // Queue encrypted data for writing via uv_write()
    uv_buf_t wbuf = uv_buf_init((char *)buf, (unsigned int)len);
    uv_write_t *req = malloc(sizeof(uv_write_t));
    req->data = conn;
    uv_write(req, (uv_stream_t *)&conn->handle, &wbuf, 1, on_write_done);
    return (int)len;
}

static int tls_bio_recv(void *ctx, unsigned char *buf, size_t len) {
    client_conn_t *conn = (client_conn_t *)ctx;
    // Return data from conn->read_buf (filled by uv_read_start callback)
    if (conn->tls_read_avail == 0) return MBEDTLS_ERR_SSL_WANT_READ;
    size_t copy = (len < conn->tls_read_avail) ? len : conn->tls_read_avail;
    memcpy(buf, conn->tls_read_ptr, copy);
    conn->tls_read_ptr += copy;
    conn->tls_read_avail -= copy;
    return (int)copy;
}
```

This approach:
- **Works with mbedTLS directly** — no OpenSSL compatibility layer needed
- **Supports HTTP/2** — mbedTLS ALPN negotiation during handshake
- **Self-signed certs** — existing `tls_generate_self_signed_cert()` code is reused unchanged
- **No new dependencies** — mbedTLS is already linked

**Estimated LOC:** ~800 (server rewrite) + ~300 (TLS integration) + ~200 (HTTP parser integration) = ~1,300

---

### 2.3 JS Event Loop Migration (Phase 3)

Replace the custom event loop in `lambda/js/js_event_loop.cpp` (267 LOC) with libuv
primitives while preserving the existing API surface.

#### 2.3.1 Timer Migration

Current: custom min-heap with 256-entry static array and `select()`-based sleep.
New: `uv_timer_t` handles — unlimited, precise, cross-platform.

```c
typedef struct JsTimerHandle {
    uv_timer_t timer;
    int64_t id;
    Item callback;
    bool is_interval;
} JsTimerHandle;

extern "C" Item js_setTimeout(Item callback, Item delay) {
    double ms = item_to_double(delay);
    if (ms < 0) ms = 0;

    JsTimerHandle *th = pool_calloc(sizeof(JsTimerHandle));
    th->id = next_timer_id++;
    th->callback = callback;
    th->is_interval = false;

    uv_timer_init(lambda_uv_loop(), &th->timer);
    th->timer.data = th;
    uv_timer_start(&th->timer, timer_fire_cb, (uint64_t)ms, 0);

    return i2item(th->id);
}

extern "C" Item js_setInterval(Item callback, Item delay) {
    double ms = item_to_double(delay);
    if (ms < 1) ms = 1;

    JsTimerHandle *th = pool_calloc(sizeof(JsTimerHandle));
    th->id = next_timer_id++;
    th->callback = callback;
    th->is_interval = true;

    uv_timer_init(lambda_uv_loop(), &th->timer);
    th->timer.data = th;
    uv_timer_start(&th->timer, timer_fire_cb, (uint64_t)ms, (uint64_t)ms);

    return i2item(th->id);
}

static void timer_fire_cb(uv_timer_t *handle) {
    JsTimerHandle *th = (JsTimerHandle *)handle->data;
    if (get_type_id(th->callback) == LMD_TYPE_FUNC) {
        js_call_function(th->callback, ItemNull, NULL, 0);
    }
    // Microtasks are drained by the prepare handle after each callback
    if (!th->is_interval) {
        uv_close((uv_handle_t *)handle, timer_close_cb);
    }
}
```

#### 2.3.2 Microtask Queue

The microtask queue **stays custom** — libuv has no built-in microtask concept.
The existing FIFO ring buffer (1024 capacity) works well. The only change is the
drain trigger: instead of inline drain calls in the event loop, we use libuv's
`uv_prepare_t` handle which fires before each event loop iteration.

```c
// Registered during init:
lambda_uv_set_microtask_drain(js_microtask_flush);
```

This ensures microtasks are drained between every macrotask (timer callback, I/O callback,
etc.), matching the ECMAScript specification.

#### 2.3.3 Event Loop Drain

The current `js_event_loop_drain()` (called after `js_main()` returns) becomes:

```c
extern "C" int js_event_loop_drain(void) {
    // Flush any synchronous microtasks first
    js_microtask_flush();

    // Run libuv event loop until all handles are closed
    // (timers expired, no pending I/O)
    return lambda_uv_run();
}
```

This replaces the 50-line manual drain loop with a single `uv_run()` call.
libuv handles timer expiration, sleep management, and I/O multiplexing internally.

#### 2.3.4 API Compatibility

The public API in `js_event_loop.h` remains unchanged:

```c
void js_event_loop_init(void);      // now calls lambda_uv_init()
int  js_event_loop_drain(void);     // now calls lambda_uv_run()
void js_microtask_enqueue(Item cb); // unchanged (custom queue)
void js_microtask_flush(void);      // unchanged (custom queue)
Item js_setTimeout(Item cb, Item delay);     // now uses uv_timer_t
Item js_setInterval(Item cb, Item delay);    // now uses uv_timer_t
void js_clearTimeout(Item timer_id);         // now calls uv_timer_stop
void js_clearInterval(Item timer_id);        // now calls uv_timer_stop
```

No changes needed in `transpile_js_mir.cpp` or `sys_func_registry.c` — the function
signatures are identical.

**Estimated LOC:** ~150 (replacing 267 LOC of custom loop)

---

### 2.4 Async File I/O — `fs` Module (Phase 4)

Expose Node.js-style `fs` functions to JS, backed by libuv's async file I/O threadpool.

#### 2.4.1 API Surface

```js
// Callback style
fs.readFile("data.json", (err, data) => { ... });
fs.writeFile("output.txt", content, (err) => { ... });
fs.stat("file.txt", (err, stats) => { ... });
fs.readdir("./src", (err, files) => { ... });
fs.unlink("temp.txt", (err) => { ... });
fs.mkdir("output", (err) => { ... });
fs.rename("old.txt", "new.txt", (err) => { ... });
fs.exists("file.txt", (exists) => { ... });

// Promise style (preferred)
const data = await fs.promises.readFile("data.json");
await fs.promises.writeFile("output.txt", content);

// Synchronous (existing Lambda functionality, kept for compat)
const data = fs.readFileSync("data.json");
fs.writeFileSync("output.txt", content);
```

#### 2.4.2 Implementation Pattern

Each `fs.*` function creates a `uv_fs_t` request and schedules it on the event loop:

```c
typedef struct JsFsReq {
    uv_fs_t req;
    Item callback;          // JS callback function
    Item promise_resolve;   // for Promise API
    Item promise_reject;
} JsFsReq;

extern "C" Item js_fs_readFile(Item path, Item callback) {
    const char *filepath = item_to_cstr(path);
    if (!filepath) return ItemNull;

    JsFsReq *fsreq = pool_calloc(sizeof(JsFsReq));
    fsreq->callback = callback;
    fsreq->req.data = fsreq;

    uv_fs_open(lambda_uv_loop(), &fsreq->req, filepath,
               UV_FS_O_RDONLY, 0, on_fs_open_for_read);

    return ItemNull; // result delivered via callback
}

static void on_fs_open_for_read(uv_fs_t *req) {
    JsFsReq *fsreq = (JsFsReq *)req->data;
    if (req->result < 0) {
        // Error: call callback with error
        Item err = js_create_error(uv_strerror((int)req->result));
        js_call_function(fsreq->callback, ItemNull, &err, 1);
    } else {
        // Read file contents with uv_fs_read
        int fd = (int)req->result;
        uv_fs_req_cleanup(req);
        // ... queue uv_fs_read, then uv_fs_close
    }
}
```

#### 2.4.3 Transpiler Integration

`fs` functions are registered as a namespace object in the JS global scope.
The transpiler resolves `fs.readFile(...)` as a method call on the `fs` global:

```c
// In js_runtime.cpp:
static Item fs_namespace = ItemNull;

Item js_get_fs_namespace(void) {
    if (is_null(fs_namespace)) {
        fs_namespace = js_create_object();
        js_object_set(fs_namespace, "readFile", js_new_native_function(js_fs_readFile));
        js_object_set(fs_namespace, "writeFile", js_new_native_function(js_fs_writeFile));
        js_object_set(fs_namespace, "readFileSync", js_new_native_function(js_fs_readFileSync));
        js_object_set(fs_namespace, "writeFileSync", js_new_native_function(js_fs_writeFileSync));
        // ... etc
    }
    return fs_namespace;
}
```

Import handling: `import fs from 'fs'` is recognized as a built-in module specifier
in the module loader, returning the `fs` namespace object instead of loading from disk.

**Estimated LOC:** ~500 (runtime) + ~100 (transpiler wiring) = ~600

---

### 2.5 Async HTTP — `fetch()` API (Phase 5)

Integrate libcurl's multi interface with libuv for non-blocking HTTP requests from JS.

#### 2.5.1 curl_multi + uv_poll Architecture

```
┌──────────────────────────────────────────┐
│           JS:  fetch(url)                │
│              ↓                           │
│  js_fetch() → curl_easy + curl_multi     │
│              ↓                           │
│  CURLMOPT_SOCKETFUNCTION → uv_poll_t     │
│  CURLMOPT_TIMERFUNCTION → uv_timer_t     │
│              ↓                           │
│  uv_run() → poll callbacks → curl_multi  │
│              ↓                           │
│  curl completes → resolve Promise        │
└──────────────────────────────────────────┘
```

curl_multi provides socket-based I/O multiplexing via two callbacks:
- `CURLMOPT_SOCKETFUNCTION`: called when curl needs to watch/unwatch a socket → map to
  `uv_poll_start()` / `uv_poll_stop()`
- `CURLMOPT_TIMERFUNCTION`: called when curl needs a timeout → map to `uv_timer_start()`

This is the standard pattern used by Node.js and many other libuv-based applications.

#### 2.5.2 fetch() API

```js
// Basic usage (returns Promise)
const response = await fetch("https://api.example.com/data");
const data = await response.json();

// With options
const response = await fetch("https://api.example.com/post", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ key: "value" })
});
```

#### 2.5.3 Implementation

```c
typedef struct JsFetchCtx {
    CURL *easy;
    Item resolve;       // Promise resolve function
    Item reject;        // Promise reject function
    char *response_buf; // accumulated response body
    size_t response_len;
    size_t response_cap;
    int status_code;
    char *headers_buf;  // response headers
} JsFetchCtx;

extern "C" Item js_fetch(Item url, Item options) {
    // Create a new Promise
    Item promise = js_promise_new_pending();
    JsPromise *p = js_promise_get(promise);

    const char *url_str = item_to_cstr(url);
    if (!url_str) {
        js_promise_reject_internal(p, js_create_error("invalid url"));
        return promise;
    }

    JsFetchCtx *ctx = pool_calloc(sizeof(JsFetchCtx));
    ctx->resolve = p->resolve_fn;
    ctx->reject = p->reject_fn;

    ctx->easy = curl_easy_init();
    curl_easy_setopt(ctx->easy, CURLOPT_URL, url_str);
    curl_easy_setopt(ctx->easy, CURLOPT_WRITEFUNCTION, fetch_write_cb);
    curl_easy_setopt(ctx->easy, CURLOPT_WRITEDATA, ctx);
    curl_easy_setopt(ctx->easy, CURLOPT_PRIVATE, ctx);

    // Parse options (method, headers, body)
    if (!is_null(options)) {
        fetch_apply_options(ctx, options);
    }

    // Add to curl_multi (shared instance on event loop)
    curl_multi_add_handle(g_curl_multi, ctx->easy);

    return promise;
}
```

**Estimated LOC:** ~400 (curl_multi+uv_poll bridge) + ~300 (fetch API) = ~700

---

### 2.6 Child Processes — `child_process` Module (Phase 6)

Expose process spawning via libuv's `uv_spawn`.

#### 2.6.1 API Surface

```js
import { exec, spawn } from 'child_process';

// exec: run command, buffer stdout/stderr, call back with result
exec("ls -la", (err, stdout, stderr) => {
    console.log(stdout);
});

// spawn: streaming I/O
const child = spawn("grep", ["-r", "pattern", "."]);
child.stdout.on("data", (chunk) => { console.log(chunk); });
child.on("close", (code) => { console.log("exited:", code); });

// Promise variant
const { stdout } = await exec("date");
```

#### 2.6.2 Implementation

```c
typedef struct JsChildProcess {
    uv_process_t process;
    uv_pipe_t stdin_pipe;
    uv_pipe_t stdout_pipe;
    uv_pipe_t stderr_pipe;
    Item on_close;              // callback: (exit_code) => ...
    Item on_stdout_data;        // callback: (chunk) => ...
    Item on_stderr_data;        // callback: (chunk) => ...
    char *stdout_buf;           // for exec() buffering
    size_t stdout_len;
    char *stderr_buf;
    size_t stderr_len;
    Item callback;              // for exec() final callback
} JsChildProcess;

extern "C" Item js_exec(Item command, Item callback) {
    const char *cmd = item_to_cstr(command);
    if (!cmd) return ItemNull;

    JsChildProcess *cp = pool_calloc(sizeof(JsChildProcess));
    cp->callback = callback;

    // Setup stdio pipes
    uv_pipe_init(lambda_uv_loop(), &cp->stdout_pipe, 0);
    uv_pipe_init(lambda_uv_loop(), &cp->stderr_pipe, 0);

    uv_stdio_container_t stdio[3];
    stdio[0].flags = UV_IGNORE;
    stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    stdio[1].data.stream = (uv_stream_t *)&cp->stdout_pipe;
    stdio[2].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    stdio[2].data.stream = (uv_stream_t *)&cp->stderr_pipe;

    uv_process_options_t opts = {0};
    opts.file = "/bin/sh";
    char *args[] = { "/bin/sh", "-c", (char *)cmd, NULL };
    opts.args = args;
    opts.stdio = stdio;
    opts.stdio_count = 3;
    opts.exit_cb = child_exit_cb;

    int r = uv_spawn(lambda_uv_loop(), &cp->process, &opts);
    if (r != 0) {
        Item err = js_create_error(uv_strerror(r));
        js_call_function(callback, ItemNull, &err, 1);
    }

    // Start reading stdout/stderr
    uv_read_start((uv_stream_t *)&cp->stdout_pipe, alloc_cb, on_stdout_read);
    uv_read_start((uv_stream_t *)&cp->stderr_pipe, alloc_cb, on_stderr_read);

    return ItemNull;
}
```

**Estimated LOC:** ~400 (exec + spawn) + ~100 (transpiler wiring) = ~500

---

### 2.7 True async/await with Proper Microtask Scheduling (Phase 7)

v14's Promise `.then()` callbacks are called directly (not microtask-scheduled).
v15 fixes this to match ECMAScript specification semantics.

#### 2.7.1 Changes

1. **`js_promise_settle()` enqueues callbacks as microtasks** instead of calling them
   directly:

```c
static void js_promise_settle(JsPromise *p, Item result, JsPromiseState state) {
    p->state = state;
    p->result = result;

    // Schedule handlers as microtasks (per spec)
    Item *handlers = (state == JS_PROMISE_FULFILLED)
                     ? p->on_fulfilled : p->on_rejected;
    int count = (state == JS_PROMISE_FULFILLED)
                ? p->on_fulfilled_count : p->on_rejected_count;

    for (int i = 0; i < count; i++) {
        js_microtask_enqueue(handlers[i]);
    }

    // Schedule finally handlers
    for (int i = 0; i < p->finally_count; i++) {
        js_microtask_enqueue(p->finally_callbacks[i]);
    }
}
```

2. **`.then()` on already-settled promises** schedules the callback as a microtask
   (currently called synchronously):

```c
Item js_promise_then(Item promise, Item on_fulfilled, Item on_rejected) {
    JsPromise *p = js_promise_get(promise);
    if (p->state == JS_PROMISE_FULFILLED && !is_null(on_fulfilled)) {
        // Spec: schedule as microtask even if already resolved
        js_microtask_enqueue(on_fulfilled);
    }
    // ...
}
```

3. **`await` in async functions** properly suspends the state machine and resumes
   via microtask when the awaited promise settles (requires generator state machine
   from Phase 8).

#### 2.7.2 Spec Compliance Test

```js
// This must print: sync, micro, macro
setTimeout(() => console.log("macro"), 0);
Promise.resolve().then(() => console.log("micro"));
console.log("sync");
```

**Estimated LOC:** ~100 (Promise settlement changes)

---

### 2.8 Generator State Machine Completion (Phase 8)

Complete the generator implementation deferred from v14. The AST parsing and runtime
structs exist; the state machine transform in `transpile_js_mir.cpp` is missing.

#### 2.8.1 State Machine Transform

For each generator function, the transpiler:

1. **Identifies yield points** in the function body
2. **Assigns a state index** to each yield point
3. **Emits a state machine function** that:
   - Takes `(generator_ptr, sent_value)` as arguments
   - Switches on `generator->state`
   - Each state block executes code between yield points
   - At each yield: saves locals to `generator->locals[]`, sets next state, returns yielded value
   - At function end: sets `generator->done = true`, returns `undefined`

```js
// Source:
function* range(start, end) {
    let i = start;
    while (i < end) {
        yield i;
        i++;
    }
}

// Transform to state machine (conceptual C):
Item range_state_machine(JsGenerator *gen, Item sent) {
    int i;
    switch (gen->state) {
    case 0: // init
        i = item_to_int(gen->locals[0]); // start
        gen->locals[2] = gen->locals[0]; // let i = start
        gen->state = 1;
        // fall through
    case 1: // loop condition
        i = item_to_int(gen->locals[2]);
        if (i >= item_to_int(gen->locals[1])) { // i < end
            gen->done = true;
            return ItemUndefined;
        }
        gen->state = 2;
        return int_to_item(i); // yield i
    case 2: // after yield
        gen->locals[2] = int_to_item(item_to_int(gen->locals[2]) + 1); // i++
        gen->state = 1;
        goto case_1; // back to loop condition
    }
}
```

#### 2.8.2 MIR Emission

The transpiler emits MIR code for the state machine:

```
// MIR pseudocode for generator body:
range_sm:
    arg gen_ptr:p, sent_val:i64
    local state:i64, done:i64
    // load state
    state = MIR_MEM(gen_ptr + STATE_OFFSET)
    // switch on state
    switch state, L0, L1, L2
L0: // state 0: init
    ... save start/end to locals array ...
    state = 1
    MIR_MEM(gen_ptr + STATE_OFFSET) = 1
    jmp L1
L1: // state 1: loop check + yield
    i = MIR_MEM(gen_ptr + LOCALS_OFFSET + 2*8)
    end = MIR_MEM(gen_ptr + LOCALS_OFFSET + 1*8)
    bge i, end, Ldone
    MIR_MEM(gen_ptr + STATE_OFFSET) = 2
    ret i  // yield i
L2: // state 2: post-yield
    i = MIR_MEM(gen_ptr + LOCALS_OFFSET + 2*8)
    i = add i, 1
    MIR_MEM(gen_ptr + LOCALS_OFFSET + 2*8) = i
    MIR_MEM(gen_ptr + STATE_OFFSET) = 1
    jmp L1
Ldone:
    MIR_MEM(gen_ptr + DONE_OFFSET) = 1
    ret ItemUndefined
```

#### 2.8.3 Iterator Protocol

Wire generators to `for...of`:

```js
for (const x of range(1, 5)) {
    console.log(x); // 1, 2, 3, 4
}

// Desugars to:
const _iter = range(1, 5);
let _result = _iter.next();
while (!_result.done) {
    const x = _result.value;
    console.log(x);
    _result = _iter.next();
}
```

The transpiler must detect when `for...of` is iterating over a generator (or any
iterator) and emit the desugared loop.

#### 2.8.4 yield* Delegation

```js
function* concat(a, b) {
    yield* a;
    yield* b;
}
```

`yield*` delegates to a sub-iterator. Each `.next()` call on the outer generator
is forwarded to the sub-iterator until it's done, then execution continues in the
outer generator.

**Estimated LOC:** ~600 (transpiler transform) + ~200 (runtime) + ~200 (for-of wiring) = ~1,000

---

### 2.9 Network Thread Pool Migration (Phase 9)

Replace the custom `network_thread_pool.h/.cpp` (used by Radiant for parallel resource
downloads) with libuv's built-in threadpool via `uv_queue_work()`.

#### 2.9.1 Current Design

```c
// network_thread_pool.h (current)
NetworkThreadPool* thread_pool_create(int num_threads);
void thread_pool_submit(NetworkThreadPool* pool, DownloadTask* task);
void thread_pool_destroy(NetworkThreadPool* pool);
```

Custom priority queue, custom pthreads, custom worker loop.

#### 2.9.2 libuv Replacement

```c
// uv_queue_work replaces the entire custom thread pool
typedef struct DownloadWorkReq {
    uv_work_t req;
    void *resource;
    ResourcePriority priority;
    TaskFunction task_fn;
} DownloadWorkReq;

void network_submit_download(void *resource, TaskFunction fn, ResourcePriority priority) {
    DownloadWorkReq *work = malloc(sizeof(DownloadWorkReq));
    work->resource = resource;
    work->task_fn = fn;
    work->priority = priority;
    work->req.data = work;

    uv_queue_work(lambda_uv_loop(), &work->req, work_cb, after_work_cb);
}

static void work_cb(uv_work_t *req) {
    DownloadWorkReq *work = (DownloadWorkReq *)req->data;
    work->task_fn(work->resource);
}

static void after_work_cb(uv_work_t *req, int status) {
    DownloadWorkReq *work = (DownloadWorkReq *)req->data;
    // Notify main thread that download is complete
    free(work);
}
```

Note: libuv's threadpool doesn't support priority queuing natively (it uses a FIFO
work queue with `UV_THREADPOOL_SIZE` workers, default 4). If priority is critical,
the priority queue logic can be retained client-side, submitting tasks in priority
order to `uv_queue_work()`.

**Estimated LOC:** ~100 (replacing ~200 LOC of custom thread pool)

---

## 3. Implementation Plan

### Phase Ordering and Dependencies

```
Phase 1: libuv Integration Layer          (foundation — no dependencies)
    │
    ├─→ Phase 2: HTTP Server Migration    (depends on Phase 1)
    │       └─→ TLS/HTTPS with mbedTLS
    │
    ├─→ Phase 3: JS Event Loop Migration  (depends on Phase 1)
    │       │
    │       ├─→ Phase 4: Async File I/O   (depends on Phase 3)
    │       ├─→ Phase 5: Async HTTP fetch  (depends on Phase 3)
    │       ├─→ Phase 6: Child Processes   (depends on Phase 3)
    │       └─→ Phase 7: Microtask Fix    (depends on Phase 3)
    │
    ├─→ Phase 8: Generator State Machine   (standalone — no libuv dependency)
    │
    └─→ Phase 9: Network Thread Pool       (depends on Phase 1, low priority)
```

**Practical ordering:** Phases 1 → 3 → 8 → 7 → 2 → 4 → 5 → 6 → 9

Rationale:
- Phase 1 (libuv layer) unlocks everything else
- Phase 3 (JS event loop) is the most impactful for JS development
- Phase 8 (generators) is standalone and completes a v14 gap
- Phase 7 (microtask fix) is small and improves correctness
- Phase 2 (server) is a separate subsystem, can be done in parallel
- Phases 4–6 (fs/fetch/child) are incremental additions
- Phase 9 (thread pool) is low priority, Radiant-only

### Phased Delivery

| Phase | Feature | Est. LOC | Dependencies | Priority |
|:-----:|---------|:--------:|:------------:|:--------:|
| 1 | libuv integration layer | ~80 | None | P0 |
| 2 | HTTP server migration + TLS | ~1,300 | Phase 1 | P1 |
| 3 | JS event loop migration | ~150 | Phase 1 | P0 |
| 4 | Async file I/O (`fs`) | ~600 | Phase 3 | P1 |
| 5 | Async HTTP (`fetch`) | ~700 | Phase 3 | P1 |
| 6 | Child processes | ~500 | Phase 3 | P2 |
| 7 | True microtask scheduling | ~100 | Phase 3 | P1 |
| 8 | Generator state machine | ~1,000 | None | P0 |
| 9 | Network thread pool migration | ~100 | Phase 1 | P3 |
| — | **Total** | **~4,530** | | |

---

## 4. Build System Changes

### 4.1 Dependency Changes Summary

| Dependency | v14 Status | v15 Status | Action |
|-----------|:----------:|:----------:|--------|
| **libevent** | ✅ Linked (static) | ❌ Removed | Delete from `build_lambda_config.json` |
| **libevent_openssl** | ✅ Linked (static) | ❌ Removed | Delete from `build_lambda_config.json` |
| **libuv** | ❌ Not used | ✅ Added (static) | Build from source, add to `build_lambda_config.json` |
| **llhttp** | ❌ Not used | ✅ Added (vendored) | Single `.c`/`.h` in `lib/serve/` |
| **libcurl** | ✅ Linked (static) | ✅ Unchanged | Still used, now with curl_multi + uv_poll |
| **mbedTLS** | ✅ Linked (static) | ✅ Unchanged | Now directly integrated with uv_tcp_t |

### 4.2 build_lambda_config.json Changes

```json
// REMOVE these entries (all 3 platforms):
{
    "name": "libevent",
    "include": "/opt/homebrew/include",
    "lib": "/opt/homebrew/lib/libevent.a",
    "type": "static"
},
{
    "name": "libevent_openssl",
    "include": "/opt/homebrew/include",
    "lib": "/opt/homebrew/lib/libevent_openssl.a",
    "type": "static"
}

// ADD (macOS):
{
    "name": "libuv",
    "include": "mac-deps/libuv-install/include",
    "lib": "mac-deps/libuv-install/lib/libuv.a",
    "type": "static"
}

// ADD (Linux):
{
    "name": "libuv",
    "include": "/usr/local/include",
    "lib": "/usr/local/lib/libuv.a",
    "type": "static"
}

// ADD (Windows/MSYS2):
{
    "name": "libuv",
    "include": "/clang64/include",
    "lib": "/clang64/lib/libuv.a",
    "type": "static"
}
```

### 4.3 CLI Build Profile

The CLI build profile currently excludes `libevent` and `libevent_openssl`. After
migration, update `exclude_libraries` to remove those entries. If the CLI doesn't
need server functionality, add `libuv` to the exclusion list. However, if the CLI
uses `setTimeout`/`setInterval` in JS scripts, libuv must be included.

### 4.4 Source Directory

`lib/serve/` is already listed in `source_dirs`. The new `lib/uv_loop.c` file goes
in `lib/` which should already be discoverable. Verify and add if needed.

### 4.5 Platform-Specific Link Libraries

libuv requires platform-specific system libraries:
- **macOS:** No additional libraries (kqueue is built-in)
- **Linux:** `-lpthread -ldl -lrt` (usually already linked)
- **Windows:** `-lws2_32 -liphlpapi -luserenv` (WinSock, IP helper, user environment)

These should be added to the platform-specific `system_libraries` in `build_lambda_config.json`.

---

## 5. Migration Plan

### 5.1 Server Rewrite Strategy

The server rewrite is a **replacement**, not an incremental migration. The approach:

1. **Create `lib/serve/server_uv.c`** alongside existing `server.c`
2. **Port the API surface**: `server_create()`, `server_start()`, `server_run()`,
   `server_stop()`, `server_destroy()`, `server_set_handler()`
3. **Implement HTTP parsing** with llhttp (vendored)
4. **Implement TLS** with direct mbedTLS on `uv_tcp_t`
5. **Test both implementations** side by side
6. **Swap**: rename `server.c` → `server_libevent.c.bak`, `server_uv.c` → `server.c`
7. **Remove libevent** from build config

### 5.2 Event Loop Migration Strategy

The JS event loop migration is an **in-place replacement**:

1. **Keep `js_event_loop.h` unchanged** — same API
2. **Rewrite `js_event_loop.cpp`** internals to use libuv
3. **Run all 63 JS tests** — must pass without changes
4. **Remove custom timer heap and `select()` code**

### 5.3 Rollback Plan

If libuv integration causes unforeseen issues:
- The custom event loop code is preserved in git history
- `server.c` backup (`server_libevent.c.bak`) can be restored
- Build config changes are trivially reversible (re-add libevent entries)

---

## 6. Testing Strategy

### 6.1 Existing Test Preservation

All 63 JS tests must continue to pass with the libuv-based event loop. Special
attention to timer ordering tests (`async_v14.js`) which depend on precise timer
semantics.

### 6.2 New Test Files

| Phase | Test File | Scenarios |
|:-----:|-----------|-----------|
| 3 | `test/js/timer_uv.js` | Timer precision, many concurrent timers (>256), zero-delay ordering |
| 4 | `test/js/fs_basic.js` | `readFile` callback, `writeFile` + verify, `stat`, error handling |
| 4 | `test/js/fs_promises.js` | `await fs.promises.readFile()`, error rejection |
| 5 | `test/js/fetch_basic.js` | GET request, POST with body, error handling, response.json() |
| 6 | `test/js/child_process.js` | `exec("echo hello")`, exit code, stderr capture |
| 7 | `test/js/microtask_order.js` | sync→micro→macro ordering, nested microtasks |
| 8 | `test/js/generator_basic.js` | Simple yield/next, yield in loop, early return |
| 8 | `test/js/generator_for_of.js` | `for...of` consumption, `break` inside for-of |
| 8 | `test/js/generator_delegation.js` | `yield*` to sub-generator |

### 6.3 Server Tests

| Test | Description |
|------|-------------|
| `test/serve/http_basic.cpp` | Start server, GET request via libcurl, verify response |
| `test/serve/tls_basic.cpp` | HTTPS with self-signed cert, verify TLS handshake |
| `test/serve/concurrent.cpp` | 100 concurrent connections, verify no crashes |

### 6.4 Baseline Tests

- **Lambda baseline (679/679)**: Must remain 100% — libuv integration is isolated to JS/server
- **Radiant baseline**: Must remain 100% — thread pool migration must not regress

---

## 7. Risks and Mitigations

| Risk | Impact | Likelihood | Mitigation |
|------|--------|:----------:|------------|
| libuv cmake build fails on a platform | Blocks that platform | Low | libuv is battle-tested; fallback to system package (brew/apt/pacman) |
| Timer semantics differ between custom loop and libuv | Existing timer tests fail | Medium | libuv timers fire no earlier than requested; test with tolerance |
| curl_multi + uv_poll integration is complex | fetch() is unreliable | Medium | Follow Node.js's proven pattern (node-libcurl); extensive error handling |
| Binary size increase | Release binary grows | Low | libuv static lib is ~300KB; net change is positive since libevent (~500KB) is removed |
| GC doesn't see callbacks stored in libuv handles | Crash during GC | Medium | Pool-allocate all callbacks (same as v14 pattern); uv_handle_t.data points to pool-allocated struct |
| mbedTLS async TLS handshake is complex | HTTPS may be unreliable | Medium | Implement non-blocking handshake with `MBEDTLS_ERR_SSL_WANT_READ/WRITE`; test with popular HTTPS sites |
| Generator state machine for complex control flow | Correctness issues | High | Start with simple cases (loops, if/else); defer try/catch inside generators |
| Windows IOCP differences from macOS/Linux | Platform-specific bugs | Medium | Use libuv abstractions consistently; test on all platforms in CI |

---

## 8. Files Changed Summary

### New Files

| File | LOC | Purpose |
|------|:---:|---------|
| `lib/uv_loop.h` | ~30 | libuv event loop wrapper declarations |
| `lib/uv_loop.c` | ~60 | libuv event loop wrapper implementation |
| `lib/serve/server_uv.c` | ~600 | HTTP/HTTPS server on libuv (replaces server.c) |
| `lib/serve/tls_uv.c` | ~300 | TLS handler on libuv + mbedTLS (replaces tls_handler.c) |
| `lib/serve/llhttp.h` | ~200 | Vendored HTTP parser (header) |
| `lib/serve/llhttp.c` | ~4,000 | Vendored HTTP parser (implementation) |
| `test/js/fs_basic.js` | ~50 | File I/O tests |
| `test/js/fetch_basic.js` | ~50 | HTTP fetch tests |
| `test/js/microtask_order.js` | ~30 | Microtask ordering tests |
| `test/js/generator_basic.js` | ~60 | Generator tests |

### Modified Files

| File | Change Summary |
|------|----------------|
| `lambda/js/js_event_loop.cpp` | Rewrite internals to use `uv_timer_t` (from custom heap + `select()`) |
| `lambda/js/js_event_loop.h` | Add `#include <uv.h>`, no API changes |
| `lambda/js/js_runtime.cpp` | Add `fs` namespace, `fetch()`, `child_process` runtime functions; fix Promise microtask scheduling |
| `lambda/js/js_runtime.h` | Declare new runtime functions (fs, fetch, child_process) |
| `lambda/js/transpile_js_mir.cpp` | Generator state machine emission; built-in module detection (`'fs'`, `'child_process'`); `fetch` global |
| `lambda/js/build_js_ast.cpp` | No changes expected (yield/await already parsed) |
| `lambda/sys_func_registry.c` | Add FPTR entries for new runtime functions |
| `lambda/network/network_thread_pool.cpp` | Replace with `uv_queue_work()` wrapper |
| `build_lambda_config.json` | Remove libevent, add libuv (all 3 platforms) |
| `setup-mac-deps.sh` | Add libuv build-from-source recipe |
| `setup-linux-deps.sh` | Add libuv build-from-source recipe |
| `setup-windows-deps.sh` | Add libuv via pacman or build-from-source |

### Removed Files

| File | Reason |
|------|--------|
| `lib/serve/server.c` | Replaced by `server_uv.c` (renamed to `server.c` post-migration) |
| `lib/serve/tls_handler.c` | Replaced by `tls_uv.c` (renamed to `tls_handler.c` post-migration) |
| `lib/serve/mbedtls_compat.h` | No longer needed — mbedTLS used directly without OpenSSL shim |

### Unchanged Files

| File | Reason |
|------|--------|
| `lib/serve/http_handler.c` | Request/response API is libevent-independent (mostly) — may need adaptation for llhttp |
| `lib/serve/utils.c` | Utility functions (MIME types, file reading) are library-independent |
| `lambda/js/js_ast.hpp` | No new AST node types needed |
| `lambda/js/js_print.cpp` | No changes |
| `lambda/main.cpp` | No changes (already includes event loop) |

---

## 9. Out of Scope (Future — v16+)

| Feature | Reason |
|---------|--------|
| `Worker` / `SharedArrayBuffer` | Multi-threaded JS; separate design doc (`Lambda_Worker.md`) |
| `WeakRef` / `FinalizationRegistry` | GC callback hooks; requires GC infrastructure changes |
| `ReadableStream` / `WritableStream` | Streaming API; layer on top of libuv streams in v16 |
| HTTP/2 server | libuv provides TCP; HTTP/2 requires nghttp2 server integration |
| `WebSocket` | Requires HTTP upgrade + framing protocol |
| `net.createServer()` (raw TCP) | Low-level API; expose only if needed |
| Package manager / `node_modules` | Bare specifier resolution; separate design |
| `process.env` / `process.argv` | Simple to add but low priority for v15 |
| `Buffer` class | TypedArray exists; Buffer is a Node.js-ism |
| Cluster mode | Multi-process; depends on Worker design |
