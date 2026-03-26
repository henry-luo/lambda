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
#include <cstdlib>

extern Input* js_input;

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
        char* new_buf = (char*)realloc(fw->response_buf, new_cap);
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

    // store body for text()/json() methods
    int body_idx = -1;
    if (response_body_count < MAX_FETCH_RESPONSES) {
        body_idx = response_body_count++;
        response_bodies[body_idx] = fw->response_buf;
        response_body_lens[body_idx] = (int)fw->response_len;
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
    if (fw->method) free(fw->method);
    if (fw->body) free(fw->body);
    if (fw->req_headers) curl_slist_free_all(fw->req_headers);
    if (fw->response_buf) free(fw->response_buf);
    free(fw);
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
        fw->method = (char*)malloc(ms->len + 1);
        memcpy(fw->method, ms->chars, ms->len);
        fw->method[ms->len] = '\0';
    }

    // body
    Item body_key = make_string_item("body");
    Item body_val = js_property_get(options, body_key);
    if (get_type_id(body_val) == LMD_TYPE_STRING) {
        String* bs = it2s(body_val);
        fw->body = (char*)malloc(bs->len + 1);
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
                    char* line = (char*)malloc(total);
                    snprintf(line, total, "%.*s: %.*s", (int)ks->len, ks->chars, (int)vs->len, vs->chars);
                    fw->req_headers = curl_slist_append(fw->req_headers, line);
                    free(line);
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

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        return js_promise_reject(js_new_error(make_string_item("fetch: event loop not initialized")));
    }

    // allocate work context
    JsFetchWork* fw = (JsFetchWork*)calloc(1, sizeof(JsFetchWork));
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
        if (fw->method) free(fw->method);
        if (fw->body) free(fw->body);
        if (fw->req_headers) curl_slist_free_all(fw->req_headers);
        free(fw);
    }

    return promise;
}

// =============================================================================
// Reset state between runs
// =============================================================================

extern "C" void js_fetch_reset(void) {
    for (int i = 0; i < response_body_count; i++) {
        if (response_bodies[i]) {
            free(response_bodies[i]);
            response_bodies[i] = NULL;
        }
    }
    response_body_count = 0;
    pending_fetch_work = NULL;
}
