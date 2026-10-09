/**
 * js_fetch.cpp — Browser-compatible fetch() API for LambdaJS v15
 *
 * Uses libcurl (synchronous) on a libuv thread pool
 * for non-blocking HTTP requests from JavaScript.
 * Returns a Promise<Response> matching the web fetch() API.
 */
#include "../js/js_runtime.h"
#include "../js/js_headers.h"
#include "dom.h"
#include "dom_xhr.h"
#include "realm/dom_realm.h"
#include "../input/css/dom_element.hpp"
#include "../network/cookie_jar.h"
#include "../network/http_client.h"
#include "../js/js_runtime_state.hpp"
#include "../js/js_event_loop.h"
#include "../js/js_typed_array.h"
#include "../jube/jube_node_permission.h"
#include "../lambda-data.hpp"
#include "../runtime/async.h"
#include "../runtime/transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/atomic.h"
#include "../../lib/str.h"
#include "../../lib/url.h"
#include "../../lib/uv_loop.h"
#include "../../lib/byte_builder.h"
#include "../../lib/utf.h"
#include "../../lib/mime-detect.h"

#include <curl/curl.h>
#include "../network/curl_trust.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include "../../lib/mem.h"
#include "../../radiant/radiant.hpp"

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
    char** response_headers;
    int    response_header_count;
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
    atomic_int32 cancelled;
} JsFetchWork;

// Relative-path policy and the executor handoff are realm
// state. Their users run under a bound context and therefore use only direct
// owner-thread field accesses—no shared table, lock, or atomic probe.
struct JsFetchRuntimeState {
    char* base_dir = NULL;
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
#define pending_fetch_work (js_fetch_state->pending_work)

// =============================================================================
// curl write callback — accumulates response body
// =============================================================================

static size_t fetch_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    JsFetchWork* fw = (JsFetchWork*)userdata;
    if (!fw || atomic_load32(&fw->cancelled) != 0) return 0;
    size_t bytes = size * nmemb;
    return byte_builder_append(&fw->response, ptr, bytes) ? bytes : 0;
}

static size_t fetch_header_cb(char* buffer, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    JsFetchWork* fw = (JsFetchWork*)userdata;
    if (!fw) return total;
    if (atomic_load32(&fw->cancelled) != 0) return 0;

    // XHR consumes these exact response header lines after the shared fetch
    // transport returns to the document thread.
    char* header = mem_dup_n(buffer, total, MEM_CAT_JS_RUNTIME);
    if (!header) return 0;
    size_t header_len = total;
    while (header_len > 0 && (header[header_len - 1] == '\r' ||
                              header[header_len - 1] == '\n')) {
        header[--header_len] = '\0';
    }
    char** headers = (char**)mem_realloc(fw->response_headers,
        (size_t)(fw->response_header_count + 1) * sizeof(char*), MEM_CAT_JS_RUNTIME);
    if (!headers) {
        mem_free(header);
        return 0;
    }
    fw->response_headers = headers;
    fw->response_headers[fw->response_header_count++] = header;

    if (fw->cookie_jar && str_istarts_with(buffer, total, "Set-Cookie:", 11)) {
        const char* effective_url = fw->url;
        char* curl_url = NULL;
        if (fw->easy) curl_easy_getinfo(fw->easy, CURLINFO_EFFECTIVE_URL, &curl_url);
        if (curl_url && curl_url[0]) effective_url = curl_url;
        cookie_jar_store(fw->cookie_jar, effective_url, header);
    }
    return total;
}

static int fetch_progress_cb(void* user, curl_off_t download_total,
                             curl_off_t download_now, curl_off_t upload_total,
                             curl_off_t upload_now) {
    (void)download_total;
    (void)download_now;
    (void)upload_total;
    (void)upload_now;
    JsFetchWork* fw = (JsFetchWork*)user;
    // Resource-table teardown runs on the document thread; curl observes this
    // atomic flag on its worker and returns without retaining a dead realm.
    return fw && atomic_load32(&fw->cancelled) != 0;
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
    for (int i = 0; i < fw->response_header_count; i++) {
        if (fw->response_headers[i]) mem_free(fw->response_headers[i]);
    }
    if (fw->response_headers) mem_free(fw->response_headers);
    byte_builder_destroy(&fw->response);
    mem_free(fw);
}

struct FetchWorkOwner {
    JsFetchWork* work;
    ~FetchWorkOwner() { fetch_work_destroy(work); }
};

static void fetch_resource_close(void* user) {
    JsFetchWork* fw = (JsFetchWork*)user;
    if (!fw) return;
    // The table releases script roots before cancellation; libuv retains the
    // native work record until it delivers the completion callback.
    fw->resource_id = 0;
    fw->owner_runtime = NULL;
    fw->owner_context = NULL;
    fw->owner_document = NULL;
    atomic_store32(&fw->cancelled, 1);
    if (fw->queued) (void)uv_cancel((uv_req_t*)&fw->work);
}

// =============================================================================
// Worker thread: blocking curl_easy_perform
// =============================================================================

static void fetch_work_cb(uv_work_t* req) {
    JsFetchWork* fw = (JsFetchWork*)req->data;

    if (!fw || atomic_load32(&fw->cancelled) != 0) return;

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
    curl_easy_setopt(fw->easy, CURLOPT_XFERINFOFUNCTION, fetch_progress_cb);
    curl_easy_setopt(fw->easy, CURLOPT_XFERINFODATA, fw);
    curl_easy_setopt(fw->easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(fw->easy, CURLOPT_ERRORBUFFER, fw->error_msg);
    curl_easy_setopt(fw->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(fw->easy, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(fw->easy, CURLOPT_NOSIGNAL, 1L);  // thread-safe
    curl_easy_setopt(fw->easy, CURLOPT_ACCEPT_ENCODING, RADIANT_HTTP_ACCEPT_ENCODING);
    curl_use_host_trust_store(fw->easy);    // fetch() verifies by curl's default; same roots as everything else
    cookie_jar_import_curl(fw->cookie_jar, fw->easy);

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

    CURLcode res = atomic_load32(&fw->cancelled) != 0
        ? CURLE_ABORTED_BY_CALLBACK : curl_easy_perform(fw->easy);
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

static Item build_response_object(JsFetchWork* fw) {
    RootFrame roots(4);
    Rooted<Item> response(roots, ItemNull);
    Rooted<Item> headers(roots, js_headers_create_http(fw->response_headers, fw->response_header_count));
    Rooted<Item> status_text(roots, ItemNull), url(roots, ItemNull);
    JS_RETURN_IF_ERROR(headers.get());

    // statusText
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
    status_text.set(make_string_item(st));
    url.set(make_string_item(fw->url));
    if (fw->response.length > INT_MAX) return dom_realm_throw_type_error("Response body is too large");
    // the response owns a traced byte buffer; no realm-wide body index or retention cap.
    response.set(dom_realm_response_from_bytes(fw->response.data,
        (int)fw->response.length, headers.get(), (int)fw->status_code,
        status_text.get(), url.get()));
    JS_RETURN_IF_ERROR(response.get());

    // XMLHttpRequest reuses fetch's worker transport. Preserve the response
    // headers in its response handoff without exposing another curl path.
    size_t headers_len = 0;
    for (int i = 0; i < fw->response_header_count; i++) {
        headers_len += strlen(fw->response_headers[i]) + 2;
    }
    char* headers_text = (char*)mem_calloc(1, headers_len + 1, MEM_CAT_JS_RUNTIME);
    if (headers_text) {
        char* cursor = headers_text;
        for (int i = 0; i < fw->response_header_count; i++) {
            size_t line_len = strlen(fw->response_headers[i]);
            memcpy(cursor, fw->response_headers[i], line_len);
            cursor += line_len;
            *cursor++ = '\r';
            *cursor++ = '\n';
        }
        dom_realm_set_cstr(response.get(), "__xhr_response_headers",
                           make_string_item(headers_text, headers_len));
        mem_free(headers_text);
    }

    return response.get();
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
        Item error = dom_realm_new_error_named_cstr("TypeError", msg);
        Item args[1] = {error};
        dom_realm_call(reject_root.get(), ItemNull, args, 1);
    } else {
        // success — build Response object and resolve
        Item response = build_response_object(fw);
        Item args[1] = {item_is_error(response) ? js_error_lane_payload(response) : response};
        dom_realm_call(item_is_error(response) ? reject_root.get() : resolve_root.get(), ItemNull, args, 1);
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

static Item fetch_apply_options(JsFetchWork* fw, Item options) {
    if (js_is_nullish(options)) return js_status_ok();
    if (!js_is_object_value(options)) return js_throw_type_error("Invalid fetch options");
    RootFrame roots(5);
    Rooted<Item> source(roots, options), value(roots, ItemNull), entries(roots, ItemNull);
    Rooted<Item> key(roots, ItemNull), pair(roots, ItemNull);

    value.set(dom_realm_get_cstr(source.get(), "body"));
    JS_RETURN_IF_ERROR(value.get());
    if (get_type_id(value.get()) == LMD_TYPE_STRING) {
        String* body = it2s(value.get());
        fw->body = mem_dup_n(body->chars, body->len, MEM_CAT_JS_RUNTIME);
        fw->body_len = body->len;
        if (!fw->body) return ItemError;
    }

    value.set(dom_realm_get_cstr(source.get(), "headers"));
    JS_RETURN_IF_ERROR(value.get());
    entries.set(js_headers_list_from_init(value.get(), true));
    JS_RETURN_IF_ERROR(entries.get());
    for (int64_t i = 0, count = js_array_length(entries.get()); i < count; i++) {
        pair.set(js_elements_get_int(entries.get(), i));
        key.set(js_elements_get_int(pair.get(), 0));
        value.set(js_elements_get_int(pair.get(), 1));
        String* name = it2s(key.get());
        String* text = it2s(value.get());
        char* line = (char*)mem_alloc(name->len + text->len + 3, MEM_CAT_JS_RUNTIME);
        if (!line) return ItemError;
        memcpy(line, name->chars, name->len);
        size_t written = name->len;
        // libcurl uses "Name;" to send an empty value; "Name:" removes it.
        line[written++] = text->len ? ':' : ';';
        if (text->len) line[written++] = ' ';
        Utf16Iterator iterator = {(const unsigned char*)text->chars, (int64_t)text->len, 0, -1};
        uint16_t unit;
        while (utf16_iterator_next(&iterator, &unit)) line[written++] = (char)unit;
        line[written] = '\0';
        curl_slist* headers = curl_slist_append(fw->req_headers, line);
        mem_free(line);
        if (!headers) return ItemError;
        fw->req_headers = headers;
    }

    value.set(dom_realm_get_cstr(source.get(), "method"));
    JS_RETURN_IF_ERROR(value.get());
    if (get_type_id(value.get()) == LMD_TYPE_STRING) {
        String* method = it2s(value.get());
        fw->method = mem_dup_n(method->chars, method->len, MEM_CAT_JS_RUNTIME);
        if (!fw->method) return ItemError;
    }
    return js_status_ok();
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
    RootFrame argument_roots(2);
    Rooted<Item> url_root(argument_roots, url_item), options_root(argument_roots, options_item);
    if (!js_fetch_runtime_state_ensure()) {
        return dom_realm_promise_reject(dom_realm_new_error(make_string_item(
            "fetch: no active execution context")));
    }
    char url_buf[2048];
    const char* url = js_item_to_cstr(url_root.get(), url_buf, sizeof(url_buf));
    if (!url) {
        return dom_realm_promise_reject(dom_realm_new_error_named_cstr("TypeError", "fetch: invalid URL"));
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
            return dom_realm_promise_reject(dom_realm_new_error_named_cstr(
                "TypeError", "fetch: invalid URL"));
        }
        snprintf(resolved_url, sizeof(resolved_url), "%s", absolute_url);
        mem_free(absolute_url);
        url = resolved_url;
    }

    if (document && !input_resource_policy_admits(document->resource_policy, url)) {
        return dom_realm_promise_reject(dom_realm_new_error_named_cstr(
            "TypeError", "fetch: resource blocked by document policy"));
    }

    JsFetchWork* fw = (JsFetchWork*)mem_calloc(1, sizeof(JsFetchWork), MEM_CAT_JS_RUNTIME);
    if (!fw) return dom_realm_promise_reject(dom_realm_new_error(make_string_item("fetch: allocation failed")));
    FetchWorkOwner work_owner = {fw};
    Item options_status = fetch_apply_options(fw, options_root.get());
    if (item_is_error(options_status))
        return dom_realm_promise_reject(js_error_lane_payload(options_status));

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
        char* local_path = NULL;
        if (is_file) {
            // file URLs need platform drive and percent-decoding rules.
            Url* file_url = url_parse(url);
            local_path = file_url ? url_to_local_path(file_url) : NULL;
            if (file_url) url_destroy(file_url);
            path = local_path;
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

        FILE* f = path ? fopen(path, "rb") : NULL;
        if (!f) {
            char msg[2200];
            snprintf(msg, sizeof(msg), "fetch failed: cannot open '%s'",
                path ? path : url);
            if (local_path) mem_free(local_path);
            return dom_realm_promise_reject(
                dom_realm_new_error_named_cstr("TypeError", msg));
        }
        if (local_path) mem_free(local_path);
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
        snprintf(fw->url, sizeof(fw->url), "%s", url);
        fw->status_code = 200;
        fw->response = response;
        MimeDetector* detector = mime_detector_init();
        const char* mime = detector ? detect_mime_from_filename(detector, url) : nullptr;
        if (!mime) mime = "application/octet-stream";
        size_t header_length = strlen(mime) + sizeof("Content-Type: ");
        fw->response_headers = (char**)mem_calloc(1, sizeof(char*), MEM_CAT_JS_RUNTIME);
        if (fw->response_headers) {
            fw->response_headers[0] = (char*)mem_alloc(header_length, MEM_CAT_JS_RUNTIME);
            if (fw->response_headers[0]) {
                snprintf(fw->response_headers[0], header_length, "Content-Type: %s", mime);
                fw->response_header_count = 1;
            }
        }
        if (detector) mime_detector_destroy(detector);

        Item resp = build_response_object(fw);

        return item_is_error(resp)
            ? dom_realm_promise_reject(js_error_lane_payload(resp))
            : dom_realm_promise_resolve(resp);
    }
    // ---------------------------------------------------------------------------

    if (!js_permission_has_net()) {
        // blocked network fetches must reject before queueing curl work, or the
        // permission fixture waits for the worker/drain timeout instead of catch().
        RootFrame roots(2);
        Rooted<Item> cause_root(roots, js_permission_make_net_error("connect", url));
        Rooted<Item> error_root(roots, dom_realm_new_error_named_cstr("TypeError", "fetch failed"));
        // The permission cause must survive allocating the enclosing TypeError.
        dom_realm_set_cstr(error_root.get(), "cause", cause_root.get());
        return dom_realm_promise_reject(error_root.get());
    }

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        return dom_realm_promise_reject(dom_realm_new_error(make_string_item("fetch: event loop not initialized")));
    }

    if (!byte_builder_init(&fw->response, 0, MEM_CAT_JS_RUNTIME, true)) {
        return dom_realm_promise_reject(dom_realm_new_error(make_string_item("fetch: allocation failed")));
    }

    snprintf(fw->url, sizeof(fw->url), "%s", url);
    fw->cookie_jar = fetch_profile_cookie_jar();
    if (fw->cookie_jar && !cookie_jar_retain(fw->cookie_jar)) {
        fw->cookie_jar = NULL;
    }
    fw->work.data = fw;

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
    } else {
        fw->queued = true;
        // The registered request and libuv callback now own the native tail.
        work_owner.work = nullptr;
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
    pending_fetch_work = NULL;
}

#undef js_fetch_state
#undef g_fetch_base_dir
#undef pending_fetch_work

static void js_fetch_capsule_destroy(void* capsule) {
    JsFetchRuntimeState* state = (JsFetchRuntimeState*)capsule;
    // Realm teardown releases path and pending transport ownership first.
    if (state->base_dir || state->pending_work) {
        log_error("js-fetch: context destroyed before response state was reset");
    }
    mem_free(state);
}
