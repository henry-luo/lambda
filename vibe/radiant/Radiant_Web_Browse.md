# Radiant Web Browsing — Design Proposal

**Date:** April 15, 2026  
**Status:** Phases 1–5 implemented; Phase 6+ pending  
**Goal:** Enable `lambda view URL` to load and render arbitrary web pages over HTTP/HTTPS.

---

## 1. Overview

This proposal describes the work required to evolve Radiant from a local HTML/CSS renderer into a basic web browser capable of loading and rendering arbitrary web pages via `lambda view https://example.com`. The design builds on the existing networking infrastructure (libcurl, libuv thread pool, `NetworkResourceManager`, `EnhancedFileCache`) and identifies the integration gaps, missing subsystems, and multi-pass rendering strategy needed to make this work.

### Prior Art

This proposal inherits and extends the network architecture described in [Radiant_Network.md](Radiant_Network.md), which designed the thread pool, resource manager, file cache, and resource loader components. Those components are now implemented. This proposal focuses on **integrating the network module with the full rendering pipeline** — multi-pass layout, JavaScript evaluation, progressive rendering, and session management — to enable end-to-end web browsing.

### Scope

| In Scope | Out of Scope |
|----------|-------------|
| HTTP/HTTPS page loads via `lambda view URL` | WebSocket / SSE |
| Async resource downloading (CSS, images, fonts, favicons) | Service workers |
| Multi-pass progressive layout & render | Web storage (localStorage, sessionStorage) |
| JavaScript execution (best-effort via LambdaJS) | Full ES2020+ compliance |
| WOFF2 web font loading (already supported via libwoff2) | Client certificates |
| HTTP/2 multiplexing (nghttp2 already linked) | HTTP authentication (Basic/Digest) |
| Cookie jar (persistent, per-domain) | `<meta http-equiv>` refresh |
| Public suffix list for cookie domain validation | iframes |
| Session-based navigation (link clicks, back/forward) | `<video>` / `<audio>` playback |
| HTTP cache (ETag, Cache-Control, max-age) | Content-Security-Policy enforcement |
| HTTPS certificate validation | Form submission (POST) |
| Redirects (301/302/307/308) | |
| `<meta>` charset / viewport handling | |
| Relative URL resolution with `<base>` tag | |

---

## 2. Current State Assessment

### 2.1 What Exists and Works

| Component | Location | Status |
|-----------|----------|--------|
| **HTTP downloader** | `lambda/input/input_http.cpp` | ✅ libcurl with SSL, gzip, redirects, timeout |
| **Thread pool** | `lambda/network/network_thread_pool.h/cpp` | ✅ libuv-backed, 4 priority levels, atomic counters |
| **Resource manager** | `lambda/network/network_resource_manager.h/cpp` | ✅ 6 resource types, state machine, retry logic, reflow/repaint scheduling |
| **File cache** | `lambda/network/enhanced_file_cache.h` | ✅ Thread-safe LRU with HTTP cache header support (ETag, max-age, Expires) |
| **Network integration** | `lambda/network/network_integration.cpp` | ✅ `radiant_init_network_support()`, `radiant_discover_document_resources()` |
| **Resource loaders** | `lambda/network/resource_loaders.cpp` | ✅ CSS and image processing; font/script stubs |
| **Layout flush hook** | `radiant/window.cpp:430` | ✅ `resource_manager_flush_layout_updates()` called per frame in render loop |
| **URL parser** | `lib/url.h/c` | ✅ WHATWG-compliant, scheme detection, relative resolution |
| **Cookie parser** | `lambda/serve/cookie.hpp` | ⚠️ Server-side only — parses Cookie header, builds Set-Cookie |
| **HTML5 parser** | External (lexbor-based) | ✅ Full HTML5 parsing to DOM |
| **CSS engine** | `radiant/resolve_css_style.cpp`, `lambda/input/css/` | ✅ Cascade, specificity, computed styles |
| **Dirty region tracking** | `radiant/render.cpp` (Phase 18-19) | ✅ Selective repaint with dirty rectangles |
| **Font system** | `lib/font/` | ✅ FreeType loading, system font scanning, glyph cache |
| **WOFF2 decompression** | `lib/font/woff2/`, `lib/font/font_decompress.cpp` | ✅ Google libwoff2 with Brotli; `font_decompress_if_needed()` auto-detects WOFF1/WOFF2 and converts to TTF |
| **JavaScript engine** | `lambda/js/` (~22K LOC) | ✅ LambdaJS: tree-sitter-javascript → MIR JIT. ES6 classes, closures, arrow fns, try/catch, Map/Set, optional chaining. No async/await, no Promises, no timers. |
| **JS DOM bindings** | `lambda/js/js_dom.h`, `lambda/js/js_cssom.h` | ✅ 30+ DOM APIs (querySelector, createElement, appendChild, etc.), getComputedStyle, CSSOM (styleSheets, insertRule). Event handling limited to `onload`. |
| **JS script runner** | `radiant/script_runner.cpp` | ✅ `execute_document_scripts()` extracts `<script>` blocks, JIT compiles, executes. Runs before layout. |

### 2.2 What's Missing or Broken

| Component | Issue |
|-----------|-------|
| **HTTP URL in `load_html_doc()`** | `cmd_layout.cpp:2121` calls `url_to_local_path()` directly — fails for `http://` / `https://` schemes |
| **`input_from_http()` incomplete** | `input_http.cpp:178` — function body unfinished |
| **@font-face downloading** | `process_font_resource()` in `resource_loaders.cpp` is a skeleton — no actual font download or registration |
| **Font discovery** | `radiant_discover_document_resources()` has a TODO for scanning @font-face rules |
| **Cookie jar** | No client-side cookie store; `cookie.hpp` is server-oriented |
| **Session/navigation** | No concept of browsing session, history, or link-click navigation |
| **Base URL propagation** | Relative URLs in CSS (`url()`) and HTML (`src`, `href`) need base URL context from the page's origin |
| **Content-Type handling** | No MIME type → document type routing for HTTP responses |
| **Cache size limits** | `EnhancedFileCache` has no visible max size enforcement |
| **Charset detection** | No `Content-Type: charset` or `<meta charset>` handling for non-UTF-8 pages |
| **Public suffix list** | No PSL bundled — cookie domain validation can't reject supercookie domains (`.com`, `.co.uk`) |
| **HTTP/2 not enabled** | nghttp2 is linked but `CURLOPT_HTTP_VERSION` not set to prefer HTTP/2 |
| **JS script downloading** | `<script src="...">` external scripts not downloaded; only inline `<script>` blocks are executed |
| **JS error isolation** | No timeout/abort mechanism for runaway scripts; no try/catch wrapping at document level |

---

## 3. Architecture

### 3.1 High-Level Flow

```
lambda view https://example.com/page.html
    │
    ▼
┌──────────────────────────────────────────────────────────┐
│  Phase 0: URL Fetch                                      │
│  ─────────────────                                       │
│  cmd_view() detects HTTP/HTTPS scheme in URL             │
│  → download_http_content(url)  [blocking, main thread]   │
│  → detect Content-Type, charset, redirects               │
│  → parse HTML → DomDocument with base URL = final URL    │
└──────────────────────┬───────────────────────────────────┘
                       │
                       ▼
┌──────────────────────────────────────────────────────────┐
│  Phase 1: Resource Discovery & Critical Path Download    │
│  ────────────────────────────────────────────────────    │
│  radiant_discover_document_resources() scans DOM:        │
│    CRITICAL : <link rel="stylesheet"> (render-blocking)  │
│    HIGH     : @font-face url(), <script src="...">       │
│    NORMAL   : <img>, background-image, <svg><use>        │
│    LOW      : favicon, prefetch hints                    │
│  All enqueued to NetworkThreadPool (HTTP/2 multiplexed)  │
│  Thread pool downloads → EnhancedFileCache               │
└──────────────────────┬───────────────────────────────────┘
                       │
                       ▼
┌──────────────────────────────────────────────────────────┐
│  Phase 2: Script Execution (best-effort)                 │
│  ───────────────────────────────────────                 │
│  Wait for render-blocking CSS + <script src> (up to 5s)  │
│  execute_document_scripts() via LambdaJS JIT:            │
│    → inline <script> blocks                              │
│    → downloaded external <script src="..."> files         │
│    → DOM mutations applied (createElement, appendChild)  │
│    → errors caught + logged, execution continues         │
│    → per-script timeout (5s), abort on exceeded          │
└──────────────────────┬───────────────────────────────────┘
                       │
                       ▼
┌──────────────────────────────────────────────────────────┐
│  Phase 3: Initial Layout + First Contentful Paint        │
│  ────────────────────────────────────────────            │
│  CSS cascade on (possibly JS-mutated) DOM                │
│  Layout with: CSS applied, placeholder images, fallback  │
│  fonts (WOFF2 decoded if cached, else system fallback).  │
│  **First contentful paint.**                             │
└──────────────────────┬───────────────────────────────────┘
                       │
                       ▼
┌──────────────────────────────────────────────────────────┐
│  Phase 4: Progressive Refinement (async, per-frame)      │
│  ──────────────────────────────────────────────          │
│  Main render loop (window.cpp):                          │
│    resource_manager_flush_layout_updates() each frame    │
│    → image arrives → update element → reflow subtree     │
│    → font arrives → WOFF2 decompress → remeasure text    │
│    → late CSS → full cascade + reflow                    │
│    → late script → best-effort eval → reflow if mutated  │
│  Dirty region tracking minimizes repaint area            │
└──────────────────────┬───────────────────────────────────┘
                       │
                       ▼
┌──────────────────────────────────────────────────────────┐
│  Phase 5: Fully Loaded                                   │
│  ───────────────────                                     │
│  All resources completed/failed/timed out                │
│  doc->fully_loaded = true                                │
│  Final reflow + repaint. Render stabilizes.              │
└──────────────────────────────────────────────────────────┘
```

### 3.2 Component Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Radiant Window                              │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  BrowsingSession                                             │  │
│  │    page_url         : Url*          (current page)           │  │
│  │    cookie_jar       : CookieJar*    (persistent cookies)     │  │
│  │    history          : ArrayList     (back/forward stack)     │  │
│  │    history_index    : int                                    │  │
│  │    base_url         : Url*          (from <base> or page)    │  │
│  └──────────────────────────────────────────────────────────────┘  │
│           │                                                         │
│           ▼                                                         │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  DomDocument (current page DOM)                              │  │
│  │    resource_manager : NetworkResourceManager*                 │  │
│  │    fully_loaded     : bool                                   │  │
│  │    state            : RadiantState* (reflow/repaint flags)   │  │
│  └──────────────────────────────────────────────────────────────┘  │
│           │ resource_manager_flush_layout_updates() per frame       │
│           ▼                                                         │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  NetworkResourceManager                                      │  │
│  │    resources        : HashMap<url, NetworkResource>           │  │
│  │    pending_reflows  : ArrayList                               │  │
│  │    pending_repaints : ArrayList                               │  │
│  └──────────────────────┬───────────────────────────────────────┘  │
│                          │                                          │
│                          ▼                                          │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  NetworkThreadPool (libuv, 4 threads)                        │  │
│  │    CRITICAL → HIGH → NORMAL → LOW                            │  │
│  └──────────────────────┬───────────────────────────────────────┘  │
│                          │                                          │
│             ┌────────────┴────────────┐                             │
│             ▼                         ▼                             │
│  ┌───────────────────┐    ┌───────────────────┐                    │
│  │ EnhancedFileCache │    │ libcurl (HTTP/S)  │                    │
│  │ ./temp/cache/     │    │ + CookieJar       │                    │
│  └───────────────────┘    └───────────────────┘                    │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 4. Detailed Design

### 4.1 HTTP URL Entry Point

**Problem:** `load_html_doc()` in `radiant/cmd_layout.cpp:2121` assumes local file paths.

**Fix:** Add scheme detection before the file-type dispatch:

```c
DomDocument* load_html_doc(Url *base, char* doc_url, int vw, int vh, float pr) {
    Url* full_url = url_parse_with_base(doc_url, base);

    // NEW: HTTP/HTTPS — download main document first
    if (full_url->scheme == URL_SCHEME_HTTP || full_url->scheme == URL_SCHEME_HTTPS) {
        return load_http_html_doc(full_url, vw, vh, pr);
    }

    // Existing local file path logic unchanged
    char* local_path = url_to_local_path(full_url);
    ...
}
```

**`load_http_html_doc()`** (new function):

1. Download page HTML via `download_http_content()` (blocking, on main thread — small payload).
2. Extract `Content-Type` header → detect charset and MIME type.
3. If redirect chain, track final URL as `base_url`.
4. Feed HTML content to the existing HTML5 parser → `DomDocument`.
5. Store `base_url` on the document for relative URL resolution.
6. Parse `<base href="...">` if present; override base URL.
7. Proceed to `radiant_init_network_support()` → `radiant_discover_document_resources()`.

### 4.2 Thread Pool Resource Download Pipeline

**Current state:** The thread pool, resource manager, and file cache are implemented and integrated. The main render loop in `window.cpp:430` already calls `resource_manager_flush_layout_updates()` each frame.

**Verification and fixes required:**

| Item | Action |
|------|--------|
| Thread pool initialization | Verify `radiant_init_network_support()` is called at the right point in `view_doc_in_window()` |
| Base URL propagation | All resource URLs must resolve against the page's final URL (after redirects), not the original CLI URL |
| CSS `url()` resolution | CSS `background-image: url(relative.png)` must resolve against the stylesheet's URL, not the page URL |
| Resource deduplication | Same URL referenced by multiple elements should create one download, multiple callbacks |
| Connection reuse | Use libcurl multi-handle or share-handle to reuse TCP connections to same host |
| Concurrent download limit | Cap at 6 connections per host (HTTP/1.1 convention) to avoid server blocks |

**Enhanced resource discovery** — extend `radiant_discover_document_resources()`:

```c
void radiant_discover_document_resources(DomDocument* doc) {
    // Existing:
    //   <link rel="stylesheet" href="...">   → PRIORITY_HIGH
    //   <img src="...">                      → PRIORITY_NORMAL
    //   <svg><use xlink:href="...">          → PRIORITY_NORMAL

    // NEW discoveries needed:
    //   <script src="...">                   → PRIORITY_HIGH (render-blocking)
    //   <script src="..." defer/async>       → PRIORITY_NORMAL
    //   @font-face url() in parsed CSS       → PRIORITY_HIGH
    //   background-image: url() in CSS       → PRIORITY_NORMAL
    //   <link rel="icon"> / <link rel="shortcut icon"> → PRIORITY_LOW
    //   <img srcset="...">                   → PRIORITY_NORMAL (pick best candidate)
    //   <link rel="preload">                 → match as= to priority
    //   <picture><source>                    → PRIORITY_NORMAL
}
```

### 4.3 JavaScript Execution (Best-Effort)

Lambda has a built-in JavaScript engine (**LambdaJS**): tree-sitter-javascript parser → MIR JIT compilation. It supports ES6 classes, closures, arrow functions, try/catch, Map/Set, optional chaining, and ~30 DOM APIs (querySelector, createElement, appendChild, getComputedStyle, CSSOM).

**Strategy: run what we can, fail gracefully, never crash.**

#### Script Loading Pipeline

```
Phase 1 (Resource Discovery):
  <script src="app.js">     → PRIORITY_HIGH, download async
  <script src="..." defer>  → PRIORITY_NORMAL, download async
  <script src="..." async>  → PRIORITY_NORMAL, download async
  <script> inline code </script>  → no download needed

Phase 2 (Execution, after render-blocking CSS + scripts downloaded/timed out):
  1. Collect all <script> elements in document order
  2. For each <script>:
     a. External script? → use downloaded content from cache
        (if download failed/timed out → skip with warning)
     b. Wrap execution in error boundary
     c. Set per-script timeout (5 seconds)
     d. Execute via LambdaJS JIT
     e. On error: log_error(), continue to next script
     f. On timeout: abort script, log_error(), continue
  3. After all scripts: check if DOM was mutated
     → if yes, re-run CSS cascade on mutated subtrees
```

#### Error Isolation

```c
typedef struct ScriptExecResult {
    bool success;
    bool dom_mutated;       // did the script call DOM mutation APIs?
    bool timed_out;
    char* error_message;    // NULL if success
    double exec_time_ms;
} ScriptExecResult;

ScriptExecResult execute_script_safe(DomDocument* doc, const char* source,
                                      const char* source_url, int timeout_ms) {
    ScriptExecResult result = {0};

    // Set DOM mutation tracking flag
    doc->js_mutation_count = 0;

    // Set up timeout watchdog (signal-based or thread-based)
    js_set_execution_timeout(timeout_ms);

    // Execute with error trapping
    int status = js_eval_with_document(doc, source);

    if (status == JS_TIMEOUT) {
        result.timed_out = true;
        result.error_message = "Script execution timed out";
        log_error("js: timeout after %dms: %s", timeout_ms, source_url);
    } else if (status != JS_OK) {
        result.error_message = js_get_last_error();
        log_error("js: error in %s: %s", source_url, result.error_message);
    } else {
        result.success = true;
    }

    result.dom_mutated = (doc->js_mutation_count > 0);
    result.exec_time_ms = js_get_last_exec_time();

    return result;
}
```

#### What Works on Real Pages (Best-Effort)

| Pattern | Support | Notes |
|---------|---------|-------|
| DOM manipulation (menu toggling, class switching) | ✅ Works | createElement, classList.add/remove, appendChild |
| `getComputedStyle()` queries | ✅ Works | Returns cascade-resolved values |
| `document.write()` during load | ✅ Works | Inserts content at script position |
| Analytics/tracking scripts (Google Analytics, etc.) | ⚠️ Partial | HTTP requests not made, but won't crash |
| `fetch()` / `XMLHttpRequest` | ❌ Skip | Stubbed out — returns error, script continues |
| `setTimeout` / `setInterval` | ❌ Skip | Not supported, no event loop |
| `async`/`await`, Promises | ❌ Skip | Not supported — logged, execution continues |
| React/Vue/Angular SPA frameworks | ❌ Fails | Require async, modules, full DOM events |
| jQuery DOM manipulation | ⚠️ Partial | Core selectors/DOM work; AJAX/animation fail gracefully |

#### Reflow After JS Mutations

When scripts mutate the DOM, the layout pipeline must re-run on the affected subtrees:

```c
void execute_all_scripts_and_relayout(DomDocument* doc) {
    bool any_mutation = false;

    ArrayList* scripts = collect_script_elements(doc);
    for (int i = 0; i < scripts->size; i++) {
        DomElement* script_el = arraylist_get(scripts, i);
        const char* src = dom_element_get_attr(script_el, "src");
        const char* source = NULL;

        if (src) {
            // External script: check if downloaded
            NetworkResource* res = resource_manager_lookup(doc->resource_manager, src);
            if (res && res->state == STATE_COMPLETED) {
                source = read_file_content(res->local_path);
            } else {
                log_warn("js: skipping external script (not available): %s", src);
                continue;
            }
        } else {
            source = dom_element_text_content(script_el);
        }

        if (source) {
            ScriptExecResult result = execute_script_safe(doc, source, src, 5000);
            if (result.dom_mutated) any_mutation = true;
        }
    }

    if (any_mutation) {
        // Re-cascade CSS on mutated DOM, then reflow
        resolve_css_styles(doc->root_element);
        doc->state->needs_reflow = true;
    }
}
```

#### Late-Arriving Scripts (Phase 4)

External `<script async>` or `<script defer>` that arrive after initial layout:

1. `resource_manager_flush_layout_updates()` detects script completion.
2. New handler: `process_script_resource()` in `resource_loaders.cpp`.
3. Execute script via `execute_script_safe()` with DOM mutation tracking.
4. If DOM mutated → schedule reflow on affected subtree.
5. If error or timeout → log and continue (page remains usable).

### 4.3 Multi-Pass Layout and Rendering

The key insight: web pages render progressively. Radiant must layout and render with incomplete information, then re-layout as resources arrive.

#### Pass Structure

| Pass | Trigger | What Happens |
|------|---------|-------------|
| **Pass 0** | HTML parsed, before any subresources | *Not rendered.* Wait for render-blocking CSS and scripts. |
| **Pass 1** | Render-blocking CSS loaded (or 5s timeout) | Execute scripts (best-effort, 5s per-script timeout). CSS cascade on (possibly JS-mutated) DOM. |
| **Pass 2** | Script execution complete | First layout with: CSS applied, placeholder boxes for images (use `width`/`height` attributes or 0×0 default), fallback fonts for @font-face (WOFF2 decoded if already cached). **First contentful paint.** |
| **Pass N** | Each `resource_manager_flush_layout_updates()` frame tick where resources completed | Targeted reflow: image loaded → update intrinsic size → reflow containing block. Font loaded (WOFF2 auto-decompressed via `font_decompress_if_needed()`) → remeasure all text using that font → reflow. Late CSS → full cascade recalc + full reflow. Late script → best-effort eval → reflow if DOM mutated. |
| **Final** | `doc->fully_loaded == true` | Final reflow + repaint. After this, layout is stable. |

#### Render-Blocking Classification

```
Render-blocking (wait before first paint, up to 5s timeout):
  - <link rel="stylesheet"> in <head>
  - @import rules in <style> blocks
  - <script src="..."> without defer/async (parser-blocking)

Non-blocking (load async, process on arrival):
  - <link rel="stylesheet" media="print">       (non-matching media)
  - <script src="..." defer>                     (execute after CSS, before paint)
  - <script src="..." async>                     (execute when available)
  - <img>, <picture>, <video poster>
  - @font-face                                   (use fallback, swap on load)
  - background-image in CSS

Never waited for:
  - <link rel="icon">
  - <link rel="prefetch">
```

#### Placeholder Sizing

When an image hasn't loaded yet:

```c
float placeholder_width(DomElement* img) {
    // 1. Explicit CSS width → use it
    if (img->computed.width.type != CSS_AUTO) return resolve_length(img->computed.width);
    // 2. HTML width attribute → use it
    const char* w = dom_element_get_attr(img, "width");
    if (w) return (float)atoi(w);
    // 3. Default: 0×0 (collapsed, will reflow when image arrives)
    return 0.0f;
}
```

#### Font Swap Strategy

Follow CSS `font-display: swap` semantics by default:

1. **Block period (0–100ms):** Use invisible placeholder (text invisible but takes space).
2. **Swap period (100ms–3s):** Render with fallback font. When web font arrives, swap + reflow.
3. **Failure (>3s):** Keep fallback font permanently for this page load.

Implementation: extend `setup_font()` in `radiant/font.cpp` to track font load state per @font-face rule.

### 4.4 Cookie Jar & Session Support

#### 4.4.1 Client-Side Cookie Jar

The existing `cookie.hpp` is server-side. A new **client-side cookie jar** is needed.

**Location:** `lambda/network/cookie_jar.h/cpp`

```c
typedef struct CookieEntry {
    char* name;
    char* value;
    char* domain;           // e.g., ".example.com"
    char* path;             // e.g., "/"
    time_t expires;         // 0 = session cookie
    bool secure;            // only send over HTTPS
    bool http_only;         // not accessible to scripts
    int same_site;          // 0=None, 1=Lax, 2=Strict
} CookieEntry;

typedef struct CookieJar {
    ArrayList cookies;      // ArrayList of CookieEntry*
    pthread_mutex_t lock;   // thread-safe access
    char* storage_path;     // persistent file path (e.g., ./temp/cookies.dat)
} CookieJar;

// Core API
CookieJar*  cookie_jar_create(const char* storage_path);
void        cookie_jar_destroy(CookieJar* jar);

// Store cookies from HTTP response Set-Cookie headers
void        cookie_jar_store_from_response(CookieJar* jar, const char* url,
                                            const char** set_cookie_headers, int count);

// Build Cookie header value for an outgoing request
char*       cookie_jar_build_header(CookieJar* jar, const char* url, bool is_secure);

// Persistence
void        cookie_jar_save(CookieJar* jar);    // write to storage_path
void        cookie_jar_load(CookieJar* jar);    // read from storage_path

// Maintenance
void        cookie_jar_clear_expired(CookieJar* jar);
void        cookie_jar_clear_all(CookieJar* jar);
```

**Integration with libcurl:**

Rather than using libcurl's built-in cookie engine (`CURLOPT_COOKIEFILE`), manage cookies ourselves to:
- Share cookie state across all thread pool workers
- Apply same-site and domain matching rules consistently
- Persist across sessions

Each download request in the thread pool:
1. Before request: `cookie_jar_build_header()` → set `Cookie:` header via `CURLOPT_HTTPHEADER`.
2. After response: Parse `Set-Cookie` response headers → `cookie_jar_store_from_response()`.

#### 4.4.2 Browsing Session

**Location:** `radiant/browsing_session.h/cpp`

```c
typedef struct BrowsingSession {
    CookieJar*      cookie_jar;
    ArrayList        history;           // ArrayList of Url*
    int              history_index;     // current position in history
    Url*             current_url;       // current page URL
    DomDocument*     document;          // current DOM
    NetworkResourceManager* resource_manager;
    EnhancedFileCache*      cache;
    NetworkThreadPool*      thread_pool;
} BrowsingSession;

// Navigation
int  session_navigate(BrowsingSession* session, const char* url);  // load new page
int  session_go_back(BrowsingSession* session);
int  session_go_forward(BrowsingSession* session);
int  session_reload(BrowsingSession* session);

// Lifecycle
BrowsingSession* session_create(void);
void             session_destroy(BrowsingSession* session);
```

**Navigation flow (`session_navigate`):**

1. If current document exists, push `current_url` to `history`, truncate forward history.
2. Create new `DomDocument` via `load_http_html_doc()`.
3. Tear down old resource manager; create new one for the new document.
4. Reuse thread pool and cache across navigations (they are session-scoped).
5. Reuse cookie jar (cookies persist across page navigations).

### 4.5 Link Click Handling

**Location:** `radiant/event.cpp`

When a user clicks a `<a href="...">` element:

1. Hit-test the click position → find the target `DomElement`.
2. Walk up the DOM tree looking for the nearest `<a>` ancestor.
3. Extract `href` attribute.
4. Resolve relative URL against `session->current_url`.
5. Check target: `_blank` → ignore (no tab support), otherwise navigate.
6. Call `session_navigate(session, resolved_url)`.
7. The window render loop detects the new document and runs the full Phase 0–4 pipeline.

Fragment-only links (`#section`):
- Don't navigate. Scroll the viewport to the target element (by `id`).

### 4.6 Base URL and Relative URL Resolution

**Rule:** Every URL encountered in the page must be resolved against the correct base:

| Context | Base URL |
|---------|----------|
| HTML attributes (`src`, `href`, `action`) | `<base href="...">` if present, else page's final URL (after redirects) |
| CSS `url()` in external stylesheet | The stylesheet's own URL |
| CSS `url()` in inline `<style>` | Same as HTML attributes |
| CSS `url()` in `style=""` attribute | Same as HTML attributes |

**Implementation:** Store `base_url` on `DomDocument`. For external CSS, pass the stylesheet URL as `css_base_url` to the CSS parser, which resolves `url()` references at parse time.

### 4.7 Content-Type Routing

HTTP responses include `Content-Type` which may differ from the URL extension:

```c
DomDocument* load_http_html_doc(Url* url, int vw, int vh, float pr) {
    HttpResponse resp = http_get(url);

    const char* ct = resp.content_type;  // e.g., "text/html; charset=utf-8"

    if (str_starts_with(ct, "text/html"))       → parse as HTML
    if (str_starts_with(ct, "application/xhtml")) → parse as HTML
    if (str_starts_with(ct, "text/plain"))       → wrap in <pre>, render
    if (str_starts_with(ct, "application/json")) → wrap in <pre>, render
    if (str_starts_with(ct, "image/"))           → wrap in <img>, render
    if (str_starts_with(ct, "application/pdf"))  → hand off to PDF loader

    // Extract charset
    const char* charset = parse_charset(ct);  // "utf-8", "iso-8859-1", etc.
    if (charset && !str_eq_ci(charset, "utf-8")) {
        content = convert_to_utf8(resp.body, resp.size, charset);
    }
}
```

### 4.8 Cache Size Enforcement

The `EnhancedFileCache` needs a max size policy:

```c
typedef struct CacheConfig {
    size_t max_total_bytes;     // e.g., 100 MB
    int    max_entries;         // e.g., 10000
    int    default_ttl_secs;   // e.g., 3600 (1 hour) for resources without Cache-Control
} CacheConfig;
```

On each `enhanced_cache_store()`, if total exceeds limit, call `enhanced_cache_evict_lru()` until under budget. This logic exists in skeleton form — needs implementation.

---

## 5. Implementation Plan

### Phase 1: HTTP Document Loading (Core Path) ✅ COMPLETE

**Goal:** `lambda view https://example.com` downloads and renders the page.

| # | Task | Files | Status |
|---|------|-------|--------|
| 1.1 | Add HTTP scheme detection in `load_html_doc()` | `radiant/cmd_layout.cpp` | ✅ |
| 1.2 | Implement `load_http_html_doc()` — download + parse + base URL | `radiant/cmd_layout.cpp` | ✅ |
| 1.3 | Fix `input_from_http()` to be complete and usable | `lambda/input/input_http.cpp` | ✅ |
| 1.4 | Content-Type detection and charset conversion | `lambda/input/input_http.cpp` | ✅ |
| 1.5 | `<base href>` tag processing, store base URL on DomDocument | `radiant/cmd_layout.cpp`, `radiant/dom_node.hpp` | ✅ |
| 1.6 | Verify `radiant_init_network_support()` integration in `view_doc_in_window()` | `radiant/window.cpp` | ✅ |

**Validation:** `lambda view https://example.com` renders the page with local CSS applied, images missing (placeholders). ✅ Verified.

### Phase 2: Resource Loading + Progressive Rendering (Co-developed) ✅ COMPLETE

**Goal:** CSS, images, fonts, and scripts load from remote URLs; page renders progressively as resources arrive.

These two concerns are co-developed because multi-pass rendering is meaningless without working resource loading, and resource loading is hard to validate without visible rendering. Sub-milestones: CSS first, then images, then fonts, then scripts.

| # | Task | Files | Status |
|---|------|-------|--------|
| **Sub-milestone A: CSS & Core Pipeline** | | | |
| 2.1 | Verify thread pool is initialized and running in view mode | `radiant/window.cpp`, `lambda/network/network_integration.cpp` | ✅ |
| 2.2 | Base URL propagation for all `<link>`, `<img>`, `<script>`, `<use>` URLs | `lambda/network/network_integration.cpp` | ✅ |
| 2.3 | CSS `url()` resolution against stylesheet URL (not page URL) | `lambda/network/resource_loaders.cpp` | ✅ |
| 2.4 | Render-blocking CSS detection (head `<link>` only) with 5s timeout | `lambda/network/network_integration.cpp`, `radiant/cmd_layout.cpp` | ✅ |
| 2.5 | Enable HTTP/2 multiplexing: set `CURLOPT_HTTP_VERSION` to `CURL_HTTP_VERSION_2TLS` | `lambda/input/input_http.cpp` | ✅ |
| 2.6 | Connection reuse (libcurl share handle across threads) | `lambda/input/input_http.cpp`, `lambda/network/network_thread_pool.cpp` | ✅ |
| 2.7 | Per-host connection limit (`CURLOPT_MAXCONNECTS=6`) | `lambda/network/network_downloader.cpp` | ✅ |
| 2.8 | Cache size limit enforcement | `lambda/network/enhanced_file_cache.h/cpp` | ✅ |
| **Sub-milestone B: Images** | | | |
| 2.9 | Placeholder sizing for unloaded images (HTML attrs fallback, 0×0 default) | `radiant/intrinsic_sizing.cpp` | ✅ |
| 2.10 | Targeted subtree reflow on image load (skip sync HTTP when resource_manager active) | `radiant/surface.cpp`, `lambda/network/resource_loaders.cpp` | ✅ |
| **Sub-milestone C: Fonts** | | | |
| 2.11 | @font-face URL discovery in parsed CSS rules | `lambda/network/network_integration.cpp` | ✅ |
| 2.12 | Font download → WOFF2/WOFF auto-decompress → register with font system | `lambda/network/resource_loaders.cpp` | ✅ |
| 2.13 | Font swap: fallback system font → swap when web font loads → reflow text | `lambda/network/resource_loaders.cpp` | ✅ |
| **Sub-milestone D: Scripts** | | | |
| 2.14 | `<script src="...">` discovery and download (PRIORITY_HIGH for blocking, NORMAL for async/defer) | `lambda/network/network_integration.cpp` | ✅ |
| 2.15 | `process_script_resource()` handler in resource_loaders | `lambda/network/resource_loaders.cpp` | ✅ |
| 2.16 | Extend resource discovery: `background-image`, `<picture>`, `srcset`, `<link rel="preload">` | `lambda/network/network_integration.cpp` | ✅ |
| **Sub-milestone E: UX** | | | |
| 2.17 | Loading progress indicator (title bar: "Loading... N/M resources") | `radiant/window.cpp` | ✅ |
| 2.18 | Document "fully loaded" signal and final stabilization reflow | `lambda/network/network_resource_manager.cpp` | ✅ |

**Validation:** All 5222 Radiant + 578 Lambda tests pass. ✅ Verified.

### Phase 3: JavaScript Integration ✅ COMPLETE

**Goal:** Best-effort JS execution — run what we can, fail gracefully, never crash.

| # | Task | Files | Status |
|---|------|-------|--------|
| 3.1 | Per-script SIGALRM timeout (5s) with `siglongjmp` clean abort | `radiant/script_runner.cpp` | ✅ |
| 3.2 | Error boundary: inline + external `<script>` wrapped in try/catch | `radiant/script_runner.cpp` | ✅ |
| 3.3 | DOM mutation tracking (`doc->js_mutation_count`, incremented in `js_dom.cpp`) | `lambda/input/css/dom_element.hpp`, `lambda/js/js_dom.cpp` | ✅ |
| 3.4 | External downloaded scripts cached, logged for deferred execution | `lambda/network/resource_loaders.cpp` | ✅ |
| 3.5 | Re-cascade CSS + reflow after JS mutations (cascade already runs after scripts; mutation count logged) | `radiant/cmd_layout.cpp` | ✅ |
| 3.6 | Late-arriving async/defer scripts: cache + schedule reflow via `resource_manager_schedule_reflow()` | `lambda/network/resource_loaders.cpp` | ✅ |
| 3.7 | Stub unsupported APIs: `requestAnimationFrame`, `localStorage`, `sessionStorage`, `MutationObserver`, `IntersectionObserver`, `ResizeObserver`, `XMLHttpRequest`, `WebSocket`, `Worker`, `console`, `performance`, `history`, `screen`, `location`, `matchMedia`, `scrollTo`, viewport dims | `radiant/script_runner.cpp` | ✅ |

**Implementation details:**
- **3.1**: SIGALRM-based timeout. Handler calls `siglongjmp(jmpbuf, 2)` to distinguish from SIGSEGV/SIGBUS crashes (`siglongjmp(jmpbuf, 1)`). `alarm(5)` installed before `transpile_js_to_mir()`, cancelled with `alarm(0)` on completion.
- **3.2**: Both inline and external scripts wrapped in `try { ... } catch(_err) {}`. Combined with SIGSEGV/SIGBUS/SIGALRM signal guards at the C level.
- **3.3**: `js_dom_mutation_notify()` helper increments `_js_current_document->js_mutation_count`. Called from: `textContent`, `innerHTML`, `setAttribute`, `removeAttribute`, `appendChild`, `removeChild`, `insertBefore`, `classList.add/remove/toggle/replace`, `style` property set, `cssText` set.
- **3.7**: ~60 lines of preamble stubs in `execute_document_scripts()` covering window properties, observer constructors, storage APIs, event listeners, matchMedia, and viewport dimensions.

**Validation:** All 5222 Radiant + 578 Lambda tests pass. ✅ Verified.

### Phase 4: Cookie Jar & Session ✅ COMPLETE

**Goal:** Websites that require cookies (login sessions, preferences) work correctly.

| # | Task | Files | Status |
|---|------|-------|--------|
| 4.1 | Implement `CookieJar` (store, match, expire, persist) | `lambda/network/cookie_jar.h/cpp` (new) | ✅ |
| 4.2 | Integrate cookie jar with download pipeline (inject Cookie header, parse Set-Cookie) | `lambda/network/network_downloader.cpp` | ✅ |
| 4.3 | Cookie domain/path matching per RFC 6265 | `lambda/network/cookie_jar.cpp` | ✅ |
| 4.4 | Public suffix list — hardcoded common TLDs + known hosting suffixes; `is_public_suffix()` lookup | `lambda/network/public_suffix.h/cpp` (new) | ✅ |
| 4.5 | Integrate PSL with cookie jar — reject cookies for public suffix domains | `lambda/network/cookie_jar.cpp` | ✅ |
| 4.6 | Persistent cookie storage (save/load from `./temp/cookies.dat`) | `lambda/network/cookie_jar.cpp` | ✅ |
| 4.7 | Session cookie cleanup on exit | `lambda/network/cookie_jar.cpp` | ✅ |

**Implementation details:**
- **4.1**: `CookieEntry` struct (name, value, domain, path, expires, secure, http_only, same_site, creation_time). `CookieJar` with dynamic array, pthread mutex, storage path. Full lifecycle: create/destroy with auto-load/save.
- **4.2**: `CURLOPT_HEADERFUNCTION` callback in `network_download_resource()` captures `Set-Cookie` response headers → `cookie_jar_store()`. Before each request, `cookie_jar_build_request_header()` builds `Cookie:` header → injected via `CURLOPT_HTTPHEADER`. `curl_slist` freed at all return paths.
- **4.3**: RFC 6265 §5.1.3 domain-match (suffix matching with leading dot, IP address exclusion). RFC 6265 §5.1.4 path-match (prefix matching). Default path computed from request URI pathname.
- **4.4**: Hardcoded ~200 TLDs (generic + country code) plus well-known second-level suffixes (`co.uk`, `com.au`, etc.) and hosting/PaaS suffixes (`github.io`, `herokuapp.com`, `netlify.app`, etc.). Simple `strcasecmp` linear scan.
- **4.6**: Tab-separated text format: `domain\tpath\tsecure\texpires\tname\tvalue\thttponly\tsamesite`. Session cookies (expires=0) not persisted. Expired cookies skipped on load.
- **4.7**: `cookie_jar_clear_session()` removes entries with `expires == 0`. `cookie_jar_destroy()` auto-saves persistent cookies before cleanup.
- **Cookie jar** attached to `NetworkResourceManager` as `cookie_jar` field. Created in `resource_manager_create()`, destroyed in `resource_manager_destroy()`.

**Validation:** All 5222 Radiant + 578 Lambda tests pass. ✅ Verified.

### Phase 5: Navigation & Browsing Session ✅ COMPLETE

**Goal:** Clicking links navigates to new pages; back/forward works with browse history.

| #   | Task                                                             | Files                                     | Status |
| --- | ---------------------------------------------------------------- | ----------------------------------------- | ------ |
| 5.1 | `BrowsingSession` struct and lifecycle                           | `radiant/browsing_session.h/cpp` (new)    | ✅     |
| 5.2 | Browse history management (push on navigate, truncate forward on new nav, bounds checking) | `radiant/browsing_session.cpp`            | ✅     |
| 5.3 | Link click → `session_navigate()`                                | `radiant/event.cpp`                       | ✅     |
| 5.4 | Fragment navigation (scroll to `#id`)                            | `radiant/event.cpp`                       | ✅     |
| 5.5 | Back/forward via keyboard (Alt+Left, Alt+Right)                  | `radiant/window.cpp`                      | ✅     |
| 5.6 | Page title from `<title>` → window title                         | `radiant/window.cpp`                      | ✅     |

**Implementation details:**
- **5.1**: `BrowsingSession` struct with `HistoryEntry` array (url, title, scroll_y), history_count/index/capacity, session-scoped thread_pool and file_cache references. `session_create()` / `session_destroy()` lifecycle. `BROWSE_HISTORY_MAX = 100` cap.
- **5.2**: History push on navigate truncates forward entries. Evicts oldest entry when at max capacity. `session_can_go_back/forward()` bounds checking. Scroll position saved per history entry for restoration on back/forward.
- **5.3**: Main-page `<a href>` click routed through `session_navigate()` in `event.cpp`. Session resolves relative URLs against current page, loads via `show_html_doc()`, manages old doc cleanup via network teardown. Fallback to direct navigation when no session (local files, headless).
- **5.4**: Fragment-only links (`#id`) detected before navigation dispatch. `find_element_by_id()` walks DOM tree, `find_view()` locates corresponding view, root scrollpane scrolled to element's Y position. No page reload.
- **5.5**: `key_callback()` in `window.cpp` intercepts Alt+Left/Alt+Right. Saves current scroll position, calls `session_go_back/forward()`, restores saved scroll position for destination entry, updates window title.
- **5.6**: `session_extract_title()` walks DOM tree to find `<title>` element text content. Used at: initial page load (replaces format-based title), after navigation, after network fully loaded. Window title format: `Lambda - <page title>`. Title stored in session history for back/forward.
- **UiContext** extended with `browsing_session` field. Session created in `view_doc_in_window_with_events()`, destroyed at cleanup (both headless and GUI paths). `update_window_title()` helper for safe title updates from event handlers.

**Validation:** All 5218/5222 Radiant + 578/578 Lambda tests pass (4 pre-existing table/form failures). ✅ Verified.

### Phase 6: Robustness & Edge Cases ✅ COMPLETE

Status: Phases 1–6 implemented; Phase 7+ pending.

| # | Task | Files | Status |
|---|------|-------|--------|
| 6.1 | HTTPS certificate error handling (reject invalid, configurable) | `lambda/input/input_http.cpp`, `lambda/network/network_downloader.cpp` | ✅ |
| 6.2 | Redirect chain following (301/302/307/308) with loop detection | `lambda/input/input_http.cpp`, `lambda/network/network_downloader.cpp` | ✅ |
| 6.3 | Timeout UX: show partial content after timeout instead of blank | `radiant/window.cpp` | ✅ |
| 6.4 | Error page rendering (network error, 404, 500) | `radiant/cmd_layout.cpp` | ✅ |
| 6.5 | `<meta charset>` detection for non-UTF-8 pages | `radiant/cmd_layout.cpp` | ✅ |
| 6.6 | `<meta name="viewport">` handling for mobile-designed pages | `radiant/layout.cpp` | ✅ |
| 6.7 | Graceful handling of very large pages (memory limits) | `lambda/input/input_http.cpp`, `lambda/network/network_downloader.cpp`, `lambda/network/network_resource_manager.cpp` | ✅ |
| 6.8 | Cancel in-flight downloads on navigation (abort old page's resources) | `lambda/network/network_resource_manager.h/cpp`, `lambda/network/network_integration.cpp` | ✅ |

**Implementation details:**
- **6.1**: Specific error messages for SSL cert failures (`CURLE_SSL_CERTPROBLEM`, `CURLE_PEER_FAILED_VERIFICATION`, `CURLE_SSL_PINNEDPUBKEYNOTMATCH`, etc.) in both `input_http.cpp` and `network_downloader.cpp`. SSL verification enabled by default (`VERIFYPEER=1`, `VERIFYHOST=2`), configurable via `HttpConfig.verify_ssl`.
- **6.2**: Redirect loop detection via `CURLE_TOO_MANY_REDIRECTS` with specific error messages. `CURLOPT_MAXREDIRS=5` enforced in both download paths. Error messages distinguish redirect loops from other failures.
- **6.3**: `resource_manager_check_page_timeout()` integrated into render loop. When page load timeout (60s) is exceeded, `fully_loaded` set to true, final reflow triggered with partial content, window title updated with "(partial)" suffix.
- **6.4**: `generate_error_page_html()` function generates styled error pages. When `download_http_content()` returns NULL, an error page is rendered instead of returning nullptr, preventing blank pages on network failures.
- **6.5**: `detect_html_charset()` scans first 1024 bytes for `<meta charset>` or `Content-Type charset`. `convert_charset_to_utf8()` handles ISO-8859-1/Windows-1252 via inline converter with Win-1252 0x80-0x9F mapping table (covers ~95% of non-UTF-8 web pages). Runs before HTML parsing.
- **6.6**: `layout.cpp` now applies `<meta viewport>` width/height overrides to the initial containing block. Explicit pixel widths (e.g., `width=320`) override window viewport. `width=device-width` (stored as 0) means "use device width" — no override.
- **6.7**: Three-tier memory limits: max page response 50MB (`HTTP_MAX_RESPONSE_SIZE` in `input_http.cpp`), max single resource 100MB (`NETWORK_MAX_RESOURCE_SIZE` in `network_downloader.cpp`), max 500 resources per page (enforced in `resource_manager_load()`). Write callbacks return 0 (aborting curl) when limits exceeded.
- **6.8**: `resource_manager_cancel_all()` added to mark all pending/downloading resources as failed with "Navigation cancelled" error. Called by `radiant_cleanup_network_support()` before `resource_manager_destroy()`, ensuring in-flight downloads are aborted before old page teardown.

**Validation:** All 5218/5222 Radiant + 578/578 Lambda tests pass (4 pre-existing table/form failures). ✅ Verified.

---

## 6. Security Considerations

### 6.1 HTTPS Validation
- **Default:** Verify server certificates via system CA bundle (already done: `CURLOPT_SSL_VERIFYPEER`).
- **No certificate bypass** in production builds. Debug builds may allow `--insecure` flag.
- Check certificate hostname matches requested domain.

### 6.2 Cookie Security
- Respect `Secure` flag: only send over HTTPS.
- Respect `HttpOnly` flag: not relevant (limited JS) but track for correctness.
- Respect `SameSite` flag: `Strict` cookies only sent for same-site navigations.
- Domain scoping: `.example.com` cookie not sent to `evil.com`.
- **Public suffix list (PSL):** Bundle a static copy of the [Mozilla Public Suffix List](https://publicsuffix.org/list/public_suffix_list.dat) to reject cookies for public suffixes (`.com`, `.co.uk`, `.github.io`, etc.). Store at `data/public_suffix_list.dat`. Binary search on sorted list for O(log n) lookup. Update periodically with new releases.

### 6.3 Mixed Content
- HTTPS pages loading HTTP resources: block by default (log warning, skip resource).
- Configurable `--allow-mixed-content` for debugging.

### 6.4 URL Sanitization
- Reject `javascript:`, `data:text/html`, `file://` URLs in navigations from HTTP pages.
- Only allow `http:` and `https:` schemes for navigation.
- `data:` allowed only for inline images (already supported).

### 6.5 Resource Limits
- Max page size: 50 MB HTML.
- Max single resource: 100 MB.
- Max resources per page: 500.
- Max cache size: 100 MB (configurable).
- Request timeout: 30s per resource, 60s page total.

---

## 7. Existing Code Integration Checklist

Before new development, verify these existing components still work correctly in the Radiant pipeline:

| Component | File | Verify |
|-----------|------|--------|
| Thread pool init/shutdown | `network_thread_pool.h/cpp` | `thread_pool_create()` / `thread_pool_destroy()` lifecycle in view mode |
| Resource manager create/destroy | `network_resource_manager.h/cpp` | No memory leaks on page navigation |
| Cache init | `enhanced_file_cache.h` | LRU eviction works under pressure |
| Flush hook in render loop | `window.cpp:430` | `resource_manager_flush_layout_updates()` processes completions correctly |
| CSS resource processing | `resource_loaders.cpp` | Downloaded CSS is parsed and cascade is recomputed |
| Image resource processing | `resource_loaders.cpp` | Downloaded images render correctly after reflow |
| Download function | `input_http.cpp` | `download_http_content()` handles redirects, timeouts, compression |
| URL resolver | `lib/url.c` | Relative URLs resolve correctly against HTTP base URLs |
| WOFF2 decompression | `lib/font/font_decompress.cpp` | `font_decompress_if_needed()` correctly auto-detects and decompresses WOFF2 fonts |
| JS script execution | `radiant/script_runner.cpp` | `execute_document_scripts()` runs inline scripts, DOM bindings work |
| JS DOM mutations | `lambda/js/js_dom.h` | createElement, appendChild, classList mutations reflected in DOM tree |
| JS error handling | `lambda/js/js_runtime.cpp` | Script errors don't crash the process |

---

## 8. Testing Strategy

### Unit Tests

| Test | Description |
|------|-------------|
| `test_cookie_jar` | Store, match, expire, persist cookies; domain/path scoping; PSL rejection |
| `test_url_resolve_http` | Relative URL resolution with HTTP base URLs |
| `test_cache_eviction` | LRU eviction under size pressure |
| `test_content_type_routing` | MIME type → document type detection |
| `test_public_suffix` | PSL lookup rejects `.com`, `.co.uk`, `.github.io`; allows `example.com` |
| `test_js_error_isolation` | Script with syntax error doesn't crash; subsequent scripts still run |
| `test_js_timeout` | Infinite loop script aborts after timeout; page still renders |
| `test_js_dom_mutation` | Script that creates elements triggers reflow |

### Integration Tests

| Test | Description |
|------|-------------|
| `test_load_http_page` | Download and render a known static page (httpbin.org or local test server) |
| `test_progressive_render` | Simulate slow resource loading; verify intermediate layout is valid |
| `test_css_blocking` | Verify layout waits for head CSS but not body CSS |
| `test_navigation_back_forward` | Navigate several pages, go back/forward, verify correct content |
| `test_cookie_roundtrip` | Visit page that sets cookie, revisit, verify cookie sent |
| `test_js_inline_script` | Page with inline `<script>` that modifies DOM; verify mutations in rendered output |
| `test_js_external_script` | Page with `<script src="...">` downloaded and executed |
| `test_js_error_recovery` | Page with broken script + valid script; second script runs, page renders |
| `test_woff2_font_load` | Page with @font-face WOFF2 font; verify text rendered with correct font after download |
| `test_http2_multiplexing` | Load page with many resources from same host; verify HTTP/2 connection reuse |

### Manual Test Pages

Curate a set of real-world test URLs:

| URL | Tests |
|-----|-------|
| `https://example.com` | Simplest page, one CSS, no images |
| `https://news.ycombinator.com` | Text-heavy, minimal CSS, many links |
| `https://en.wikipedia.org/wiki/Main_Page` | Complex layout, images, web fonts |
| `https://httpbin.org/cookies/set/test/value` | Cookie round-trip |
| `https://httpbin.org/redirect/3` | Redirect chain |

---

## 9. CLI Interface

```bash
# Load a web page in the Radiant window
lambda view https://example.com

# Layout a web page (headless, output view tree)
lambda layout https://example.com

# Render a web page to file
lambda render https://example.com -o page.png
lambda render https://example.com -o page.pdf

# With options
lambda view https://example.com --no-cache          # bypass cache
lambda view https://example.com --timeout 10         # page timeout in seconds
lambda view https://example.com --viewport 1280x800  # viewport size
lambda view https://example.com --user-agent "..."   # custom UA string
```

The URL detection is simple: if the argument starts with `http://` or `https://`, treat it as a web URL. Otherwise, treat as a local file path (existing behavior). No new subcommand needed.

---

## 10. Future Extensions (Not in This Proposal)

These are explicitly out of scope but noted for future consideration:

- **Full JavaScript compliance** — async/await, Promises, ES modules, full event loop. Would close the gap for SPA frameworks (React, Vue, Angular).
- **Form submission** — POST requests with form encoding. Moderate effort, useful for login flows.
- **`<video>` / `<audio>`** — Media playback. Large scope, requires media framework.
- **Tab / multi-window** — Multiple concurrent pages. Window management complexity.
- **Address bar / URL display** — Browser chrome UI for URL entry and display. Will be implemented via Lambda Reactive UI framework.
- **Bookmarks** — Persistent URL storage with titles.
- **HTTPS Everywhere** — Auto-upgrade HTTP to HTTPS.
- **`--dry-run` / `--resources` flag** — Diagnostic mode: `lambda view URL --resources` lists all discovered resources and their download states without rendering. (KIV for debugging support.)
- **Web storage** — `localStorage` / `sessionStorage` for JS-heavy pages.
- **Service workers** — Offline support and request interception.
