/**
 * js_fetch.cpp — Browser-compatible fetch() API for LambdaJS v15
 *
 * Uses libcurl (synchronous) on a libuv thread pool
 * for non-blocking HTTP requests from JavaScript.
 * Returns a Promise<Response> matching the web fetch() API.
 */
#include "js_runtime.h"
#include "js_event_loop.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"

#include <curl/curl.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include "../../lib/mem.h"

extern Input* js_input;

// Base directory for resolving relative fetch() URLs to on-disk files.
// Set by main.cpp when --document is supplied; NULL otherwise. Owned here.
static char* g_fetch_base_dir = NULL;

extern "C" void js_fetch_set_base_path(const char* dir_path) {
    if (g_fetch_base_dir) {
        free(g_fetch_base_dir);
        g_fetch_base_dir = NULL;
    }
    if (!dir_path || !*dir_path) return;
    // Find the directory portion of dir_path; if it's a file, drop the basename.
    struct stat st;
    char* dup = strdup(dir_path);
    if (!dup) return;
    if (stat(dup, &st) == 0 && S_ISREG(st.st_mode)) {
        // strip basename
        char* slash = strrchr(dup, '/');
        if (slash) *slash = '\0';
        else { free(dup); dup = strdup("."); }
    }
    g_fetch_base_dir = dup;
}

// =============================================================================
// Helpers (shared with js_fs.cpp pattern)
// =============================================================================

static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

static Item make_string_item(const char* str, int len) {
    if (!str || len <= 0) return ItemNull;
    String* s = heap_create_name(str, (size_t)len);
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

static const char* item_to_cstr(Item value, char* buf, int buf_size) {
    if (get_type_id(value) != LMD_TYPE_STRING) return NULL;
    String* s = it2s(value);
    int len = (int)s->len;
    if (len >= buf_size) len = buf_size - 1;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';
    return buf;
}

// =============================================================================
// Fetch Work Context (per-request state)
// =============================================================================

typedef struct JsFetchWork {
    uv_work_t work;
    CURL*     easy;
    char      url[2048];

    // request options
    char*              method;   // owned, NULL → GET
    char*              body;     // owned, NULL → no body
    size_t             body_len;
    struct curl_slist* req_headers; // owned

    // response data (filled by worker thread)
    char*  response_buf;
    size_t response_len;
    size_t response_cap;
    long   status_code;
    int    curl_error;
    char   error_msg[CURL_ERROR_SIZE];

    // promise resolve/reject functions (main thread only)
    Item resolve_fn;
    Item reject_fn;
} JsFetchWork;

// =============================================================================
// curl write callback — accumulates response body
// =============================================================================

static size_t fetch_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    JsFetchWork* fw = (JsFetchWork*)userdata;
    size_t bytes = size * nmemb;
    if (fw->response_len + bytes >= fw->response_cap) {
        size_t new_cap = (fw->response_cap == 0) ? 4096 : fw->response_cap * 2;
        while (new_cap < fw->response_len + bytes + 1) new_cap *= 2;
        char* new_buf = (char*)mem_realloc(fw->response_buf, new_cap, MEM_CAT_JS_RUNTIME);
        if (!new_buf) return 0;
        fw->response_buf = new_buf;
        fw->response_cap = new_cap;
    }
    memcpy(fw->response_buf + fw->response_len, ptr, bytes);
    fw->response_len += bytes;
    fw->response_buf[fw->response_len] = '\0';
    return bytes;
}

// =============================================================================
// Worker thread: blocking curl_easy_perform
// =============================================================================

static void fetch_work_cb(uv_work_t* req) {
    JsFetchWork* fw = (JsFetchWork*)req->data;

    fw->easy = curl_easy_init();
    if (!fw->easy) {
        fw->curl_error = -1;
        snprintf(fw->error_msg, sizeof(fw->error_msg), "curl_easy_init failed");
        return;
    }

    curl_easy_setopt(fw->easy, CURLOPT_URL, fw->url);
    curl_easy_setopt(fw->easy, CURLOPT_WRITEFUNCTION, fetch_write_cb);
    curl_easy_setopt(fw->easy, CURLOPT_WRITEDATA, fw);
    curl_easy_setopt(fw->easy, CURLOPT_ERRORBUFFER, fw->error_msg);
    curl_easy_setopt(fw->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(fw->easy, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(fw->easy, CURLOPT_NOSIGNAL, 1L);  // thread-safe

    if (fw->method) {
        curl_easy_setopt(fw->easy, CURLOPT_CUSTOMREQUEST, fw->method);
    }
    if (fw->body && fw->body_len > 0) {
        curl_easy_setopt(fw->easy, CURLOPT_POSTFIELDS, fw->body);
        curl_easy_setopt(fw->easy, CURLOPT_POSTFIELDSIZE, (long)fw->body_len);
    }
    if (fw->req_headers) {
        curl_easy_setopt(fw->easy, CURLOPT_HTTPHEADER, fw->req_headers);
    }

    CURLcode res = curl_easy_perform(fw->easy);
    fw->curl_error = (int)res;

    if (res == CURLE_OK) {
        curl_easy_getinfo(fw->easy, CURLINFO_RESPONSE_CODE, &fw->status_code);
    }

    curl_easy_cleanup(fw->easy);
    fw->easy = NULL;
}

// =============================================================================
// Response object creation + .text() / .json() methods
// =============================================================================

// Stored body text for response methods
// (We use a simple slot array keyed by response index)
#define MAX_FETCH_RESPONSES 256
static char* response_bodies[MAX_FETCH_RESPONSES];
static int   response_body_lens[MAX_FETCH_RESPONSES];
static int   response_body_count = 0;
// Per-response inferred Content-Type (used by .blob() to set Blob.type).
static char* response_types[MAX_FETCH_RESPONSES];

// Infer a Content-Type from a URL path's extension. Returns a static string
// (not freed). Used for the local-file fast path so `await fetch(x).blob()`
// produces a Blob with a meaningful `type` field.
static const char* mime_from_url(const char* url) {
    if (!url) return "application/octet-stream";
    const char* dot = strrchr(url, '.');
    if (!dot) return "application/octet-stream";
    const char* ext = dot + 1;
    if (!strcasecmp(ext, "png"))  return "image/png";
    if (!strcasecmp(ext, "jpg") || !strcasecmp(ext, "jpeg")) return "image/jpeg";
    if (!strcasecmp(ext, "gif"))  return "image/gif";
    if (!strcasecmp(ext, "svg"))  return "image/svg+xml";
    if (!strcasecmp(ext, "html") || !strcasecmp(ext, "htm")) return "text/html";
    if (!strcasecmp(ext, "css"))  return "text/css";
    if (!strcasecmp(ext, "js"))   return "application/javascript";
    if (!strcasecmp(ext, "json")) return "application/json";
    if (!strcasecmp(ext, "txt"))  return "text/plain";
    if (!strcasecmp(ext, "xml"))  return "application/xml";
    return "application/octet-stream";
}

static Item js_response_text() {
    Item this_resp = js_get_this();
    String* key = heap_create_name("__body_idx", 10);
    Item idx_item = js_property_get(this_resp, (Item){.item = s2it(key)});
    if (get_type_id(idx_item) != LMD_TYPE_INT) return js_promise_resolve(ItemNull);

    int idx = (int)it2i(idx_item);
    if (idx < 0 || idx >= response_body_count || !response_bodies[idx])
        return js_promise_resolve(ItemNull);

    Item body = make_string_item(response_bodies[idx], response_body_lens[idx]);
    return js_promise_resolve(body);
}

static Item js_response_json() {
    Item this_resp = js_get_this();
    String* key = heap_create_name("__body_idx", 10);
    Item idx_item = js_property_get(this_resp, (Item){.item = s2it(key)});
    if (get_type_id(idx_item) != LMD_TYPE_INT) return js_promise_resolve(ItemNull);

    int idx = (int)it2i(idx_item);
    if (idx < 0 || idx >= response_body_count || !response_bodies[idx])
        return js_promise_resolve(ItemNull);

    Item body_str = make_string_item(response_bodies[idx], response_body_lens[idx]);
    Item parsed = js_json_parse(body_str);
    return js_promise_resolve(parsed);
}

// Synthesise a Blob-shaped JS object whose `text()` / `arrayBuffer()` / `slice()`
// methods mirror the WPT shim's Blob polyfill closely enough for the clipboard
// suite. Used by Response.blob().
static Item js_response_blob_text() {
    Item this_blob = js_get_this();
    String* tk = heap_create_name("_text", 5);
    Item t = js_property_get(this_blob, (Item){.item = s2it(tk)});
    if (get_type_id(t) != LMD_TYPE_STRING) return js_promise_resolve(make_string_item(""));
    return js_promise_resolve(t);
}

static Item make_blob_object(const char* bytes, int len, const char* type) {
    Item blob = js_new_object();
    js_property_set(blob, make_string_item("_text"), make_string_item(bytes ? bytes : "", len));
    js_property_set(blob, make_string_item("size"), (Item){.item = i2it(len)});
    js_property_set(blob, make_string_item("type"),
        make_string_item(type ? type : "application/octet-stream"));
    js_property_set(blob, make_string_item("text"),
        js_new_function((void*)js_response_blob_text, 0));
    return blob;
}

static Item js_response_blob() {
    Item this_resp = js_get_this();
    String* key = heap_create_name("__body_idx", 10);
    Item idx_item = js_property_get(this_resp, (Item){.item = s2it(key)});
    if (get_type_id(idx_item) != LMD_TYPE_INT) return js_promise_resolve(ItemNull);
    int idx = (int)it2i(idx_item);
    if (idx < 0 || idx >= response_body_count || !response_bodies[idx])
        return js_promise_resolve(ItemNull);
    const char* type = (idx < MAX_FETCH_RESPONSES && response_types[idx]) ?
                       response_types[idx] : "application/octet-stream";
    Item blob = make_blob_object(response_bodies[idx], response_body_lens[idx], type);
    return js_promise_resolve(blob);
}

static Item build_response_object(JsFetchWork* fw) {
    Item resp = js_new_object();

    // status
    Item status_key = make_string_item("status");
    js_property_set(resp, status_key, (Item){.item = i2it(fw->status_code)});

    // ok (200-299)
    Item ok_key = make_string_item("ok");
    bool ok = fw->status_code >= 200 && fw->status_code <= 299;
    js_property_set(resp, ok_key, (Item){.item = b2it(ok)});

    // statusText
    Item st_key = make_string_item("statusText");
    const char* st = (fw->status_code == 200) ? "OK" :
                     (fw->status_code == 201) ? "Created" :
                     (fw->status_code == 204) ? "No Content" :
                     (fw->status_code == 301) ? "Moved Permanently" :
                     (fw->status_code == 302) ? "Found" :
                     (fw->status_code == 304) ? "Not Modified" :
                     (fw->status_code == 400) ? "Bad Request" :
                     (fw->status_code == 401) ? "Unauthorized" :
                     (fw->status_code == 403) ? "Forbidden" :
                     (fw->status_code == 404) ? "Not Found" :
                     (fw->status_code == 500) ? "Internal Server Error" :
                     "";
    js_property_set(resp, st_key, make_string_item(st));

    // url
    Item url_key = make_string_item("url");
    js_property_set(resp, url_key, make_string_item(fw->url));

    // store body for text()/json()/blob() methods
    int body_idx = -1;
    if (response_body_count < MAX_FETCH_RESPONSES) {
        body_idx = response_body_count++;
        response_bodies[body_idx] = fw->response_buf;
        response_body_lens[body_idx] = (int)fw->response_len;
        // Cache an inferred MIME for blob().type. Owned: strdup.
        if (response_types[body_idx]) { free(response_types[body_idx]); response_types[body_idx] = NULL; }
        response_types[body_idx] = strdup(mime_from_url(fw->url));
        fw->response_buf = NULL; // ownership transferred
    }

    Item body_idx_key = make_string_item("__body_idx");
    js_property_set(resp, body_idx_key, (Item){.item = i2it(body_idx)});

    // text() method
    Item text_key = make_string_item("text");
    Item text_fn = js_new_function((void*)js_response_text, 0);
    js_property_set(resp, text_key, text_fn);

    // json() method
    Item json_key = make_string_item("json");
    Item json_fn = js_new_function((void*)js_response_json, 0);
    js_property_set(resp, json_key, json_fn);

    // blob() method — returns Promise<Blob-like object> with .type/.size/.text()
    js_property_set(resp, make_string_item("blob"),
        js_new_function((void*)js_response_blob, 0));

    return resp;
}

// =============================================================================
// After-work callback: resolve/reject promise on main thread
// =============================================================================

static void fetch_after_work_cb(uv_work_t* req, int status) {
    JsFetchWork* fw = (JsFetchWork*)req->data;

    if (status != 0 || fw->curl_error != 0) {
        // network error
        const char* msg = fw->error_msg[0] ? fw->error_msg : "fetch failed";
        Item error = js_new_error_with_name(make_string_item("TypeError"), make_string_item(msg));
        Item args[1] = {error};
        js_call_function(fw->reject_fn, ItemNull, args, 1);
    } else {
        // success — build Response object and resolve
        Item response = build_response_object(fw);
        Item args[1] = {response};
        js_call_function(fw->resolve_fn, ItemNull, args, 1);
    }

    // flush microtasks after resolving the promise
    js_microtask_flush();

    // cleanup
    if (fw->method) mem_free(fw->method);
    if (fw->body) mem_free(fw->body);
    if (fw->req_headers) curl_slist_free_all(fw->req_headers);
    if (fw->response_buf) mem_free(fw->response_buf);
    mem_free(fw);
}

// =============================================================================
// Parse fetch options: { method, headers, body }
// =============================================================================

static void fetch_apply_options(JsFetchWork* fw, Item options) {
    if (get_type_id(options) != LMD_TYPE_MAP) return;

    // method
    Item method_key = make_string_item("method");
    Item method_val = js_property_get(options, method_key);
    if (get_type_id(method_val) == LMD_TYPE_STRING) {
        String* ms = it2s(method_val);
        fw->method = (char*)mem_alloc(ms->len + 1, MEM_CAT_JS_RUNTIME);
        memcpy(fw->method, ms->chars, ms->len);
        fw->method[ms->len] = '\0';
    }

    // body
    Item body_key = make_string_item("body");
    Item body_val = js_property_get(options, body_key);
    if (get_type_id(body_val) == LMD_TYPE_STRING) {
        String* bs = it2s(body_val);
        fw->body = (char*)mem_alloc(bs->len + 1, MEM_CAT_JS_RUNTIME);
        memcpy(fw->body, bs->chars, bs->len);
        fw->body[bs->len] = '\0';
        fw->body_len = bs->len;
    }

    // headers (map of key→value strings)
    Item headers_key = make_string_item("headers");
    Item headers_val = js_property_get(options, headers_key);
    if (get_type_id(headers_val) == LMD_TYPE_MAP) {
        Item keys = js_object_keys(headers_val);
        if (get_type_id(keys) == LMD_TYPE_ARRAY) {
            int len = (int)keys.array->length;
            for (int i = 0; i < len; i++) {
                Item idx = {.item = i2it(i)};
                Item hkey = js_array_get(keys, idx);
                Item hval = js_property_get(headers_val, hkey);
                if (get_type_id(hkey) == LMD_TYPE_STRING && get_type_id(hval) == LMD_TYPE_STRING) {
                    String* ks = it2s(hkey);
                    String* vs = it2s(hval);
                    // "Header-Name: value"
                    size_t total = ks->len + 2 + vs->len + 1;
                    char* line = (char*)mem_alloc(total, MEM_CAT_JS_RUNTIME);
                    snprintf(line, total, "%.*s: %.*s", (int)ks->len, ks->chars, (int)vs->len, vs->chars);
                    fw->req_headers = curl_slist_append(fw->req_headers, line);
                    mem_free(line);
                }
            }
        }
    }
}

// =============================================================================
// Executor callback — captures resolve/reject in JsFetchWork
// =============================================================================

// We need a way to pass the JsFetchWork pointer into the executor.
// Use a thread-local (single-threaded JS, so safe).
static JsFetchWork* pending_fetch_work = NULL;

static Item fetch_executor(Item resolve_fn, Item reject_fn) {
    if (pending_fetch_work) {
        pending_fetch_work->resolve_fn = resolve_fn;
        pending_fetch_work->reject_fn = reject_fn;
    }
    return make_js_undefined();
}

// =============================================================================
// Public API: js_fetch(url [, options])
// =============================================================================

extern "C" Item js_fetch(Item url_item, Item options_item) {
    char url_buf[2048];
    const char* url = item_to_cstr(url_item, url_buf, sizeof(url_buf));
    if (!url) {
        return js_promise_reject(js_new_error_with_name(make_string_item("TypeError"), make_string_item("fetch: invalid URL")));
    }

    // ---- Local-file fast path -------------------------------------------------
    // For relative URLs (no scheme) or explicit `file://` URLs, resolve against
    // the document base directory and read directly. This makes WPT tests that
    // fetch sibling resources (e.g. `resources/greenbox.png`) work in headless
    // mode without spinning up an HTTP server.
    bool is_http = (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
    bool is_file = (strncmp(url, "file://", 7) == 0);
    bool has_scheme = false;
    for (const char* p = url; *p; p++) {
        if (*p == ':') { has_scheme = true; break; }
        if (*p == '/' || *p == '?' || *p == '#') break;
    }
    if (is_file || (!is_http && !has_scheme)) {
        // Build absolute path
        char path_buf[2048];
        const char* path = NULL;
        if (is_file) {
            path = url + 7;
            // file:///abs/path -> "/abs/path"
            if (path[0] == '/' && path[1] == '/' && path[2] == '/') path += 2;
        } else if (url[0] == '/') {
            // Document-root-relative: WPT uses paths like
            // `/resources/testharness.js` and `/clipboard-apis/...`. Walk up
            // the base directory looking for a parent whose join-with-url
            // exists on disk. This makes the fetch root effectively the
            // outermost ancestor of the document that still satisfies the
            // requested resource.
            if (g_fetch_base_dir) {
                char ancestor[2048];
                snprintf(ancestor, sizeof(ancestor), "%s", g_fetch_base_dir);
                for (;;) {
                    snprintf(path_buf, sizeof(path_buf), "%s%s", ancestor, url);
                    struct stat st;
                    if (stat(path_buf, &st) == 0) { path = path_buf; break; }
                    char* slash = strrchr(ancestor, '/');
                    if (!slash || slash == ancestor) break;
                    *slash = '\0';
                }
            }
            if (!path) path = url;  // fall through to absolute fs path
        } else {
            // Relative
            if (g_fetch_base_dir) {
                snprintf(path_buf, sizeof(path_buf), "%s/%s", g_fetch_base_dir, url);
                path = path_buf;
            } else {
                path = url;
            }
        }

        FILE* f = fopen(path, "rb");
        if (!f) {
            char msg[2200];
            snprintf(msg, sizeof(msg), "fetch failed: cannot open '%s'", path);
            return js_promise_reject(
                js_new_error_with_name(make_string_item("TypeError"), make_string_item(msg)));
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 0) sz = 0;
        char* buf = (char*)mem_alloc((size_t)sz + 1, MEM_CAT_JS_RUNTIME);
        size_t got = (sz > 0) ? fread(buf, 1, (size_t)sz, f) : 0;
        buf[got] = '\0';
        fclose(f);

        // Build a synthetic JsFetchWork so build_response_object can be reused.
        JsFetchWork* fw = (JsFetchWork*)mem_calloc(1, sizeof(JsFetchWork), MEM_CAT_JS_RUNTIME);
        snprintf(fw->url, sizeof(fw->url), "%s", url);
        fw->status_code = 200;
        fw->response_buf = buf;
        fw->response_len = got;
        fw->response_cap = (size_t)sz + 1;

        Item resp = build_response_object(fw);

        // build_response_object transferred ownership of response_buf;
        // free the fw struct (curl handle, headers etc. are NULL here).
        if (fw->method) mem_free(fw->method);
        if (fw->body) mem_free(fw->body);
        mem_free(fw);

        return js_promise_resolve(resp);
    }
    // ---------------------------------------------------------------------------

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        return js_promise_reject(js_new_error(make_string_item("fetch: event loop not initialized")));
    }

    // allocate work context
    JsFetchWork* fw = (JsFetchWork*)mem_calloc(1, sizeof(JsFetchWork), MEM_CAT_JS_RUNTIME);
    if (!fw) {
        return js_promise_reject(js_new_error(make_string_item("fetch: allocation failed")));
    }

    snprintf(fw->url, sizeof(fw->url), "%s", url);
    fw->work.data = fw;

    // parse options (method, headers, body)
    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        fetch_apply_options(fw, options_item);
    }

    // create promise — executor captures resolve/reject into fw
    pending_fetch_work = fw;
    Item executor = js_new_function((void*)fetch_executor, 2);
    Item promise = js_promise_create(executor);
    pending_fetch_work = NULL;

    // queue work on thread pool
    int r = uv_queue_work(loop, &fw->work, fetch_work_cb, fetch_after_work_cb);
    if (r != 0) {
        Item args[1] = {js_new_error(make_string_item("fetch: failed to queue work"))};
        js_call_function(fw->reject_fn, ItemNull, args, 1);
        // fw will be freed by after_work_cb if queued; since it wasn't, free now
        if (fw->method) mem_free(fw->method);
        if (fw->body) mem_free(fw->body);
        if (fw->req_headers) curl_slist_free_all(fw->req_headers);
        mem_free(fw);
    }

    return promise;
}

// =============================================================================
// Reset state between runs
// =============================================================================

extern "C" void js_fetch_reset(void) {
    for (int i = 0; i < response_body_count; i++) {
        if (response_bodies[i]) {
            mem_free(response_bodies[i]);
            response_bodies[i] = NULL;
        }
        if (response_types[i]) {
            free(response_types[i]);
            response_types[i] = NULL;
        }
    }
    response_body_count = 0;
    pending_fetch_work = NULL;
}
