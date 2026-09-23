/**
 * js_fetch.cpp — Browser-compatible fetch() API for LambdaJS v15
 *
 * Uses libcurl (synchronous) on a libuv thread pool
 * for non-blocking HTTP requests from JavaScript.
 * Returns a Promise<Response> matching the web fetch() API.
 */
#include "../js/js_runtime.h"
#include "dom.h"
#include "dom_xhr.h"
#include "realm/dom_realm.h"
#include "../input/css/dom_element.hpp"
#include "../network/cookie_jar.h"
#include "../js/js_runtime_state.hpp"
#include "../js/js_event_loop.h"
#include "../jube/jube_node_permission.h"
#include "../lambda-data.hpp"
#include "../runtime/async.h"
#include "../runtime/transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/str.h"
#include "../../lib/uv_loop.h"
#include "../../lib/byte_builder.h"

#include <curl/curl.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include "../../lib/mem.h"
#include "../../radiant/radiant.hpp"

#define MAX_FETCH_RESPONSES 256

// --document is parsed before its EvalContext exists. This is bootstrap input
// only: the first context copies it into its fetch capsule and clears it.
static char* js_fetch_bootstrap_base_path = NULL;
static void js_fetch_set_current_base_path(const char* dir_path);

extern "C" void js_fetch_set_base_path(const char* dir_path) {
    if (js_active_runtime_state) {
        js_fetch_set_current_base_path(dir_path);
        return;
    }
    if (js_fetch_bootstrap_base_path) {
        mem_free(js_fetch_bootstrap_base_path);
        js_fetch_bootstrap_base_path = NULL;
    }
    if (dir_path && dir_path[0]) {
        js_fetch_bootstrap_base_path = mem_strdup(dir_path, MEM_CAT_JS_RUNTIME);
    }
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
    CookieJar*          cookie_jar; // retained until the async transfer finishes

    // response data (filled by worker thread)
    ByteBuilder response;
    long   status_code;
    int    curl_error;
    char   error_msg[CURL_ERROR_SIZE];

    // promise resolve/reject functions (main thread only)
    Item resolve_fn;
    Item reject_fn;

    // The completion is a host task for this exact retained document realm.
    Runtime* owner_runtime;
    EvalContext* owner_context;
    void* owner_document;
    uint32_t resource_id;
    bool queued;
} JsFetchWork;

// Response bodies, relative-path policy, and the executor handoff are realm
// state. Their users run under a bound context and therefore use only direct
// owner-thread field accesses—no shared table, lock, or atomic probe.
struct JsFetchRuntimeState {
    char* base_dir = NULL;
    char* response_bodies[MAX_FETCH_RESPONSES] = {};
    int response_body_lens[MAX_FETCH_RESPONSES] = {};
    char* response_types[MAX_FETCH_RESPONSES] = {};
    int response_body_count = 0;
    JsFetchWork* pending_work = NULL;
};

static char* js_fetch_base_dir_from_path(const char* dir_path) {
    if (!dir_path || !dir_path[0]) return NULL;
    struct stat st;
    char* dup = mem_strdup(dir_path, MEM_CAT_JS_RUNTIME);
    if (!dup) return NULL;
    if (stat(dup, &st) == 0 && S_ISREG(st.st_mode)) {
        char* slash = strrchr(dup, '/');
        if (slash) *slash = '\0';
        else {
            mem_free(dup);
            dup = mem_strdup(".", MEM_CAT_JS_RUNTIME);
        }
    }
    return dup;
}

extern __thread EvalContext* context;
static void* js_fetch_capsule_construct(EvalContext* owner);
static void js_fetch_capsule_destroy(void* capsule);
static const ContextCapsuleOps js_fetch_capsule_ops = {
    "js-fetch", CONTEXT_CAPSULE_LIFETIME_REALM, sizeof(JsFetchRuntimeState),
    js_fetch_capsule_construct, NULL, js_fetch_capsule_destroy
};

static JsFetchRuntimeState* js_fetch_runtime_state_get() {
    return (JsFetchRuntimeState*)context_capsule(context, CONTEXT_CAPSULE_DOM_FETCH);
}

// Construction adopts the pre-realm bootstrap base path, so the capsule owns
// that string from its first existence rather than from a later fixup.
static void* js_fetch_capsule_construct(EvalContext* owner) {
    (void)owner;
    JsFetchRuntimeState* state = (JsFetchRuntimeState*)mem_calloc(1,
        sizeof(JsFetchRuntimeState), MEM_CAT_JS_RUNTIME);
    if (!state) return NULL;
    if (js_fetch_bootstrap_base_path) {
        state->base_dir = js_fetch_base_dir_from_path(js_fetch_bootstrap_base_path);
        mem_free(js_fetch_bootstrap_base_path);
        js_fetch_bootstrap_base_path = NULL;
    }
    return state;
}

static bool js_fetch_runtime_state_ensure() {
    if (!js_active_runtime_state) return false;
    return context_capsule_ensure(context, CONTEXT_CAPSULE_DOM_FETCH,
                                  &js_fetch_capsule_ops) != NULL;
}

static void js_fetch_set_current_base_path(const char* dir_path) {
    if (!js_fetch_runtime_state_ensure()) return;
    if (context_capsule(context, CONTEXT_CAPSULE_DOM_FETCH) && js_fetch_runtime_state_get()->base_dir) {
        mem_free(js_fetch_runtime_state_get()->base_dir);
        js_fetch_runtime_state_get()->base_dir = NULL;
    }
    js_fetch_runtime_state_get()->base_dir = js_fetch_base_dir_from_path(dir_path);
}
extern "C" void js_fetch_apply_bootstrap_base_path(void) {
    if (js_fetch_bootstrap_base_path) (void)js_fetch_runtime_state_ensure();
}

#define js_fetch_state ((JsFetchRuntimeState*)context_capsule(context, CONTEXT_CAPSULE_DOM_FETCH))
#define g_fetch_base_dir (js_fetch_state->base_dir)
#define response_bodies (js_fetch_state->response_bodies)
#define response_body_lens (js_fetch_state->response_body_lens)
#define response_body_count (js_fetch_state->response_body_count)
#define response_types (js_fetch_state->response_types)
#define pending_fetch_work (js_fetch_state->pending_work)

// =============================================================================
// curl write callback — accumulates response body
// =============================================================================

static size_t fetch_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    JsFetchWork* fw = (JsFetchWork*)userdata;
    size_t bytes = size * nmemb;
    return byte_builder_append(&fw->response, ptr, bytes) ? bytes : 0;
}

static size_t fetch_header_cb(char* buffer, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    JsFetchWork* fw = (JsFetchWork*)userdata;
    if (!fw || !fw->cookie_jar || !str_istarts_with(buffer, total, "Set-Cookie:", 11)) {
        return total;
    }
    const char* effective_url = fw->url;
    char* curl_url = NULL;
    if (fw->easy) curl_easy_getinfo(fw->easy, CURLINFO_EFFECTIVE_URL, &curl_url);
    if (curl_url && curl_url[0]) effective_url = curl_url;
    char* header = mem_dup_n(buffer, total, MEM_CAT_NETWORK);
    if (!header) return 0;
    cookie_jar_store(fw->cookie_jar, effective_url, header);
    mem_free(header);
    return total;
}

static CookieJar* fetch_profile_cookie_jar(void) {
    UiContext* uicon = (UiContext*)dom_get_ui_context();
    return uicon && uicon->browsing_session
        ? session_cookie_jar(uicon->browsing_session) : nullptr;
}

static void fetch_work_destroy(JsFetchWork* fw) {
    if (!fw) return;
    cookie_jar_release(fw->cookie_jar);
    if (fw->method) mem_free(fw->method);
    if (fw->body) mem_free(fw->body);
    if (fw->req_headers) curl_slist_free_all(fw->req_headers);
    byte_builder_destroy(&fw->response);
    mem_free(fw);
}

static void fetch_resource_close(void* user) {
    JsFetchWork* fw = (JsFetchWork*)user;
    if (!fw) return;
    // The table releases script roots before cancellation; libuv retains the
    // native work record until it delivers the completion callback.
    fw->resource_id = 0;
    fw->owner_runtime = NULL;
    fw->owner_context = NULL;
    fw->owner_document = NULL;
    if (fw->queued) (void)uv_cancel((uv_req_t*)&fw->work);
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
    curl_easy_setopt(fw->easy, CURLOPT_HEADERFUNCTION, fetch_header_cb);
    curl_easy_setopt(fw->easy, CURLOPT_HEADERDATA, fw);
    curl_easy_setopt(fw->easy, CURLOPT_ERRORBUFFER, fw->error_msg);
    curl_easy_setopt(fw->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(fw->easy, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(fw->easy, CURLOPT_NOSIGNAL, 1L);  // thread-safe
    if (fw->cookie_jar) cookie_jar_import_curl(fw->cookie_jar, fw->easy);

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
// Per-response inferred Content-Type (used by .blob() to set Blob.type).

// Infer a Content-Type from a URL path's extension. Returns a static string
// (not freed). Used for the local-file fast path so `await fetch(x).blob()`
// produces a Blob with a meaningful `type` field.
static const char* mime_from_url(const char* url) {
    if (!url) return "application/octet-stream";
    const char* dot = strrchr(url, '.');
    if (!dot) return "application/octet-stream";
    const char* ext = dot + 1;
    if (!str_icmp_cstr(ext, "png"))  return "image/png";
    if (!str_icmp_cstr(ext, "jpg") || !str_icmp_cstr(ext, "jpeg")) return "image/jpeg";
    if (!str_icmp_cstr(ext, "gif"))  return "image/gif";
    if (!str_icmp_cstr(ext, "svg"))  return "image/svg+xml";
    if (!str_icmp_cstr(ext, "html") || !str_icmp_cstr(ext, "htm")) return "text/html";
    if (!str_icmp_cstr(ext, "css"))  return "text/css";
    if (!str_icmp_cstr(ext, "js"))   return "application/javascript";
    if (!str_icmp_cstr(ext, "json")) return "application/json";
    if (!str_icmp_cstr(ext, "txt"))  return "text/plain";
    if (!str_icmp_cstr(ext, "xml"))  return "application/xml";
    return "application/octet-stream";
}

static Item js_response_text() {
    if (!js_fetch_runtime_state_get()) return dom_realm_promise_resolve(ItemNull);
    Item this_resp = dom_realm_receiver();
    String* key = heap_create_name("__body_idx", 10);
    Item idx_item = dom_realm_get(this_resp, (Item){.item = s2it(key)});
    if (get_type_id(idx_item) != LMD_TYPE_INT) return dom_realm_promise_resolve(ItemNull);

    int idx = (int)it2i(idx_item);
    if (idx < 0 || idx >= response_body_count || !response_bodies[idx])
        return dom_realm_promise_resolve(ItemNull);

    Item body = make_string_item(response_bodies[idx], response_body_lens[idx]);
    return dom_realm_promise_resolve(body);
}

static Item js_response_json() {
    if (!js_fetch_runtime_state_get()) return dom_realm_promise_resolve(ItemNull);
    Item this_resp = dom_realm_receiver();
    String* key = heap_create_name("__body_idx", 10);
    Item idx_item = dom_realm_get(this_resp, (Item){.item = s2it(key)});
    if (get_type_id(idx_item) != LMD_TYPE_INT) return dom_realm_promise_resolve(ItemNull);

    int idx = (int)it2i(idx_item);
    if (idx < 0 || idx >= response_body_count || !response_bodies[idx])
        return dom_realm_promise_resolve(ItemNull);

    Item body_str = make_string_item(response_bodies[idx], response_body_lens[idx]);
    Item parsed = js_json_parse(body_str);
    return dom_realm_promise_resolve(parsed);
}

// Synthesise a Blob-shaped JS object whose `text()` / `arrayBuffer()` / `slice()`
// methods mirror the WPT shim's Blob polyfill closely enough for the clipboard
// suite. Used by Response.blob().
static Item js_response_blob_text() {
    Item this_blob = dom_realm_receiver();
    String* tk = heap_create_name("_text", 5);
    Item t = dom_realm_get(this_blob, (Item){.item = s2it(tk)});
    if (get_type_id(t) != LMD_TYPE_STRING) return dom_realm_promise_resolve(make_string_item(""));
    return dom_realm_promise_resolve(t);
}

static Item make_blob_object(const char* bytes, int len, const char* type) {
    Item blob = js_new_object();
    dom_realm_set_cstr(blob, "_text", make_string_item(bytes ? bytes : "", len));
    dom_realm_set_cstr(blob, "size", (Item){.item = i2it(len)});
    dom_realm_set_cstr(blob, "type", make_string_item(type ? type : "application/octet-stream"));
    dom_realm_set_native(blob, make_string_item("text"), js_response_blob_text);
    return blob;
}

static Item js_response_blob() {
    if (!js_fetch_runtime_state_get()) return dom_realm_promise_resolve(ItemNull);
    Item this_resp = dom_realm_receiver();
    String* key = heap_create_name("__body_idx", 10);
    Item idx_item = dom_realm_get(this_resp, (Item){.item = s2it(key)});
    if (get_type_id(idx_item) != LMD_TYPE_INT) return dom_realm_promise_resolve(ItemNull);
    int idx = (int)it2i(idx_item);
    if (idx < 0 || idx >= response_body_count || !response_bodies[idx])
        return dom_realm_promise_resolve(ItemNull);
    const char* type = (idx < MAX_FETCH_RESPONSES && response_types[idx]) ?
                       response_types[idx] : "application/octet-stream";
    Item blob = make_blob_object(response_bodies[idx], response_body_lens[idx], type);
    return dom_realm_promise_resolve(blob);
}

static Item build_response_object(JsFetchWork* fw) {
    Item resp = js_new_object();

    // status
    Item status_key = make_string_item("status");
    dom_realm_set(resp, status_key, (Item){.item = i2it(fw->status_code)});

    // ok (200-299)
    Item ok_key = make_string_item("ok");
    bool ok = fw->status_code >= 200 && fw->status_code <= 299;
    dom_realm_set(resp, ok_key, (Item){.item = b2it(ok)});

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
    dom_realm_set(resp, st_key, make_string_item(st));

    // url
    Item url_key = make_string_item("url");
    dom_realm_set(resp, url_key, make_string_item(fw->url));

    // store body for text()/json()/blob() methods
    int body_idx = -1;
    if (response_body_count < MAX_FETCH_RESPONSES) {
        body_idx = response_body_count++;
        response_body_lens[body_idx] = (int)fw->response.length;
        response_bodies[body_idx] = (char*)byte_builder_take(&fw->response, NULL);
        // Cache an inferred MIME for blob().type. Owned: strdup.
        if (response_types[body_idx]) { mem_free(response_types[body_idx]); response_types[body_idx] = NULL; }
        response_types[body_idx] = mem_strdup(mime_from_url(fw->url), MEM_CAT_JS_RUNTIME);
    }

    Item body_idx_key = make_string_item("__body_idx");
    dom_realm_set(resp, body_idx_key, (Item){.item = i2it(body_idx)});

    // text() method
    Item text_key = make_string_item("text");
    Item text_fn = dom_realm_new_function(js_response_text);
    dom_realm_set(resp, text_key, text_fn);

    // json() method
    Item json_key = make_string_item("json");
    Item json_fn = dom_realm_new_function(js_response_json);
    dom_realm_set(resp, json_key, json_fn);

    // blob() method — returns Promise<Blob-like object> with .type/.size/.text()
    dom_realm_set_native(resp, make_string_item("blob"), js_response_blob);

    return resp;
}

// =============================================================================
// After-work callback: resolve/reject promise on main thread
// =============================================================================

static void fetch_after_work_cb(uv_work_t* req, int status) {
    JsFetchWork* fw = (JsFetchWork*)req->data;
    if (!fw) return;
    fw->queued = false;

    // Teardown cancelled this request, so the old document realm and its
    // precise callback roots are gone. Only release the native work tail.
    if (fw->resource_id == 0 || !fw->owner_runtime || !fw->owner_context) {
        fetch_work_destroy(fw);
        return;
    }
    if (!js_runtime_context_enter_turn(fw->owner_runtime, fw->owner_context)) {
        log_error("fetch: could not enter the originating document runtime");
        fetch_work_destroy(fw);
        return;
    }
    dom_set_document(fw->owner_document);

    const RuntimeResourceEntry* entry = runtime_resource_table_entry_owned(
        js_runtime_resource_table(), fw->owner_context, fw->resource_id);
    if (!entry) {
        fetch_work_destroy(fw);
        return;
    }
    RootFrame roots(2);
    Rooted<Item> resolve_root(roots, runtime_resource_table_root_value(
        js_runtime_resource_table(), entry, 1));
    Rooted<Item> reject_root(roots, runtime_resource_table_root_value(
        js_runtime_resource_table(), entry, 2));

    if (fw->cookie_jar && !cookie_jar_flush(fw->cookie_jar)) {
        log_error("fetch: failed to persist response cookies");
    }

    if (status != 0 || fw->curl_error != 0) {
        // network error
        const char* msg = fw->error_msg[0] ? fw->error_msg : "fetch failed";
        Item error = dom_realm_new_error_named(make_string_item("TypeError"), make_string_item(msg));
        Item args[1] = {error};
        dom_realm_call(reject_root.get(), ItemNull, args, 1);
    } else {
        // success — build Response object and resolve
        Item response = build_response_object(fw);
        Item args[1] = {response};
        dom_realm_call(resolve_root.get(), ItemNull, args, 1);
    }

    // flush microtasks after resolving the promise
    dom_realm_microtask_flush();

    uint32_t resource_id = fw->resource_id;
    fw->resource_id = 0;
    runtime_resource_table_remove_owned(js_runtime_resource_table(),
        fw->owner_context, resource_id);
    fetch_work_destroy(fw);
}

// =============================================================================
// Parse fetch options: { method, headers, body }
// =============================================================================

static void fetch_apply_options(JsFetchWork* fw, Item options) {
    if (get_type_id(options) != LMD_TYPE_MAP) return;

    // method
    Item method_key = make_string_item("method");
    Item method_val = dom_realm_get(options, method_key);
    if (get_type_id(method_val) == LMD_TYPE_STRING) {
        String* ms = it2s(method_val);
        fw->method = mem_dup_n(ms->chars, ms->len, MEM_CAT_JS_RUNTIME);
    }

    // body
    Item body_key = make_string_item("body");
    Item body_val = dom_realm_get(options, body_key);
    if (get_type_id(body_val) == LMD_TYPE_STRING) {
        String* bs = it2s(body_val);
        fw->body = mem_dup_n(bs->chars, bs->len, MEM_CAT_JS_RUNTIME);
        fw->body_len = bs->len;
    }

    // headers (map of key→value strings)
    Item headers_key = make_string_item("headers");
    Item headers_val = dom_realm_get(options, headers_key);
    if (get_type_id(headers_val) == LMD_TYPE_MAP) {
        Item keys = js_object_keys(headers_val);
        if (get_type_id(keys) == LMD_TYPE_ARRAY) {
            int len = (int)keys.array->length;
            for (int i = 0; i < len; i++) {
                Item idx = {.item = i2it(i)};
                Item hkey = js_elements_get(keys, idx);
                Item hval = dom_realm_get(headers_val, hkey);
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

// Promise construction invokes this synchronously under the same context that
// started fetch(), so the handoff remains a direct capsule field.

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
    if (!js_fetch_runtime_state_ensure()) {
        return dom_realm_promise_reject(dom_realm_new_error(make_string_item(
            "fetch: no active execution context")));
    }
    char url_buf[2048];
    const char* url = js_item_to_cstr(url_item, url_buf, sizeof(url_buf));
    if (!url) {
        return dom_realm_promise_reject(dom_realm_new_error_named(make_string_item("TypeError"), make_string_item("fetch: invalid URL")));
    }

    // Browser fetch resolves a relative request against the active document,
    // not the process working directory used by file-backed test documents.
    char resolved_url[2048];
    DomDocument* document = (DomDocument*)dom_get_document();
    const char* document_url = document && document->url
        ? url_get_href(document->url) : nullptr;
    if (document_url && (strncmp(document_url, "http://", 7) == 0 ||
                         strncmp(document_url, "https://", 8) == 0)) {
        char* absolute_url = dom_resolve_network_url(url, document_url);
        if (!absolute_url || strlen(absolute_url) >= sizeof(resolved_url)) {
            if (absolute_url) mem_free(absolute_url);
            return dom_realm_promise_reject(dom_realm_new_error_named(
                make_string_item("TypeError"), make_string_item("fetch: invalid URL")));
        }
        snprintf(resolved_url, sizeof(resolved_url), "%s", absolute_url);
        mem_free(absolute_url);
        url = resolved_url;
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
            return dom_realm_promise_reject(
                dom_realm_new_error_named(make_string_item("TypeError"), make_string_item(msg)));
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 0) sz = 0;
        ByteBuilder response;
        if (!byte_builder_init(&response, (size_t)sz, MEM_CAT_JS_RUNTIME, true)) {
            fclose(f);
            return dom_realm_promise_reject(
                dom_realm_new_error(make_string_item("fetch: allocation failed")));
        }
        size_t got = (sz > 0) ? fread(response.data, 1, (size_t)sz, f) : 0;
        response.length = got;
        response.data[got] = '\0';
        fclose(f);

        // Build a synthetic JsFetchWork so build_response_object can be reused.
        JsFetchWork* fw = (JsFetchWork*)mem_calloc(1, sizeof(JsFetchWork), MEM_CAT_JS_RUNTIME);
        if (!fw) {
            byte_builder_destroy(&response);
            return dom_realm_promise_reject(
                dom_realm_new_error(make_string_item("fetch: allocation failed")));
        }
        snprintf(fw->url, sizeof(fw->url), "%s", url);
        fw->status_code = 200;
        fw->response = response;

        Item resp = build_response_object(fw);

        // build_response_object transferred ownership of response bytes;
        // free the fw struct (curl handle, headers etc. are NULL here).
        if (fw->method) mem_free(fw->method);
        if (fw->body) mem_free(fw->body);
        mem_free(fw);

        return dom_realm_promise_resolve(resp);
    }
    // ---------------------------------------------------------------------------

    if (!js_permission_has_net()) {
        // blocked network fetches must reject before queueing curl work, or the
        // permission fixture waits for the worker/drain timeout instead of catch().
        Item cause = js_permission_make_net_error("connect", url);
        Item err = dom_realm_new_error_named(make_string_item("TypeError"), make_string_item("fetch failed"));
        dom_realm_set_cstr(err, "cause", cause);
        return dom_realm_promise_reject(err);
    }

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        return dom_realm_promise_reject(dom_realm_new_error(make_string_item("fetch: event loop not initialized")));
    }

    // allocate work context
    JsFetchWork* fw = (JsFetchWork*)mem_calloc(1, sizeof(JsFetchWork), MEM_CAT_JS_RUNTIME);
    if (!fw) {
        return dom_realm_promise_reject(dom_realm_new_error(make_string_item("fetch: allocation failed")));
    }
    if (!byte_builder_init(&fw->response, 0, MEM_CAT_JS_RUNTIME, true)) {
        mem_free(fw);
        return dom_realm_promise_reject(dom_realm_new_error(make_string_item("fetch: allocation failed")));
    }

    snprintf(fw->url, sizeof(fw->url), "%s", url);
    fw->cookie_jar = fetch_profile_cookie_jar();
    if (fw->cookie_jar && !cookie_jar_retain(fw->cookie_jar)) {
        fw->cookie_jar = NULL;
    }
    fw->work.data = fw;

    // parse options (method, headers, body)
    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        fetch_apply_options(fw, options_item);
    }

    // create promise — executor captures resolve/reject into fw
    pending_fetch_work = fw;
    Item executor = dom_realm_new_function(fetch_executor);
    Item promise = dom_realm_promise_new(executor);
    pending_fetch_work = NULL;

    RootFrame roots(3);
    Rooted<Item> promise_root(roots, promise);
    Rooted<Item> resolve_root(roots, fw->resolve_fn);
    Rooted<Item> reject_root(roots, fw->reject_fn);
    fw->owner_runtime = context ? context->runtime : NULL;
    fw->owner_context = context;
    fw->owner_document = dom_get_document();
    const RuntimeResourceDescriptor* descriptor =
        runtime_resource_descriptor_from_legacy_name("HTTPClientRequest");
    Item root_values[] = {promise_root.get(), resolve_root.get(), reject_root.get()};
    fw->resource_id = fw->owner_runtime && fw->owner_context && descriptor
        ? runtime_resource_table_add_root_span_owned(js_runtime_resource_table(),
            fw->owner_context, root_values, 3, descriptor, fetch_resource_close, fw, false)
        : 0;
    if (fw->resource_id == 0) {
        Item args[1] = {dom_realm_new_error(make_string_item(
            "fetch: failed to retain request callbacks"))};
        dom_realm_call(reject_root.get(), ItemNull, args, 1);
        fetch_work_destroy(fw);
        return promise_root.get();
    }
    fw->resolve_fn = ItemNull;
    fw->reject_fn = ItemNull;

    // queue work on thread pool
    int r = uv_queue_work(loop, &fw->work, fetch_work_cb, fetch_after_work_cb);
    if (r != 0) {
        Item args[1] = {dom_realm_new_error(make_string_item("fetch: failed to queue work"))};
        dom_realm_call(reject_root.get(), ItemNull, args, 1);
        uint32_t resource_id = fw->resource_id;
        fw->resource_id = 0;
        runtime_resource_table_remove_owned(js_runtime_resource_table(),
            fw->owner_context, resource_id);
        fetch_work_destroy(fw);
    } else {
        fw->queued = true;
    }

    return promise_root.get();
}

// =============================================================================
// Reset state between runs
// =============================================================================

extern "C" void js_fetch_reset(void) {
    if (!js_fetch_runtime_state_get()) return;
    if (g_fetch_base_dir) {
        mem_free(g_fetch_base_dir);
        g_fetch_base_dir = NULL;
    }
    for (int i = 0; i < response_body_count; i++) {
        if (response_bodies[i]) {
            mem_free(response_bodies[i]);
            response_bodies[i] = NULL;
        }
        if (response_types[i]) {
            mem_free(response_types[i]);
            response_types[i] = NULL;
        }
    }
    response_body_count = 0;
    pending_fetch_work = NULL;
}

#undef js_fetch_state
#undef g_fetch_base_dir
#undef response_bodies
#undef response_body_lens
#undef response_body_count
#undef response_types
#undef pending_fetch_work

static void js_fetch_capsule_destroy(void* capsule) {
    JsFetchRuntimeState* state = (JsFetchRuntimeState*)capsule;
    // Heap reset releases response payloads before this capsule can disappear.
    if (state->base_dir || state->response_body_count || state->pending_work) {
        log_error("js-fetch: context destroyed before response state was reset");
    }
    mem_free(state);
}
