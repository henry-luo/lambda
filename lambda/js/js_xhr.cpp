/**
 * js_xhr.cpp — XMLHttpRequest for Radiant browser context
 *
 * Synchronous HTTP via http_fetch() from input_http.cpp.
 * Each XHR instance stores its C-level state (method, url, headers,
 * response data) in a flat array indexed by an ID property on the
 * JS object. Methods use js_get_this() to resolve the current XHR.
 */

#include "js_xhr.h"
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../input/input.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"

#include <cstring>
#include <cstdlib>

static inline Item make_js_undef() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

// ============================================================================
// Per-XHR state
// ============================================================================

#define MAX_XHR 64
#define MAX_HEADERS 64

struct XhrHeader {
    char* name;
    char* value;
};

struct XhrState {
    bool    in_use;
    char*   method;         // "GET", "POST", etc.
    char*   url;            // request URL
    bool    async_flag;     // async (ignored — always sync in Radiant)
    XhrHeader req_headers[MAX_HEADERS];
    int     req_header_count;
    int     ready_state;    // 0=UNSENT, 1=OPENED, 2=HEADERS_RECEIVED, 3=LOADING, 4=DONE
    long    status;
    char*   status_text;
    char*   response_text;
    size_t  response_size;
    char**  resp_headers;       // raw header strings from http_fetch
    int     resp_header_count;
    Item    js_object;      // back-reference to the JS XHR object
};

static XhrState _xhr_pool[MAX_XHR];
static int _xhr_count = 0;

// ============================================================================
// Helpers
// ============================================================================

static void xhr_set_str(Item obj, const char* key, const char* value) {
    Item k = (Item){.item = s2it(heap_create_name(key))};
    Item v = value ? (Item){.item = s2it(heap_create_name(value))} : ItemNull;
    js_property_set(obj, k, v);
}

static void xhr_set_int(Item obj, const char* key, int value) {
    Item k = (Item){.item = s2it(heap_create_name(key))};
    Item v = (Item){.item = i2it(value)};
    js_property_set(obj, k, v);
}

static Item xhr_get_prop(Item obj, const char* key) {
    Item k = (Item){.item = s2it(heap_create_name(key))};
    return js_property_get(obj, k);
}

static int xhr_id_from_this() {
    Item self = js_get_this();
    Item id_val = xhr_get_prop(self, "__xhr_id");
    TypeId tid = get_type_id(id_val);
    if (tid == LMD_TYPE_INT) {
        return (int)it2i(id_val);
    }
    log_error("xhr: cannot resolve __xhr_id from this");
    return -1;
}

static XhrState* xhr_state_from_this() {
    int id = xhr_id_from_this();
    if (id < 0 || id >= _xhr_count || !_xhr_pool[id].in_use) {
        log_error("xhr: invalid XHR id %d", id);
        return nullptr;
    }
    return &_xhr_pool[id];
}

static void xhr_fire_readystatechange(XhrState* xhr) {
    // update readyState on the JS object
    xhr_set_int(xhr->js_object, "readyState", xhr->ready_state);

    // fire onreadystatechange if set
    Item cb = xhr_get_prop(xhr->js_object, "onreadystatechange");
    TypeId cb_type = get_type_id(cb);
    if (cb_type == LMD_TYPE_FUNC) {
        js_call_function(cb, xhr->js_object, nullptr, 0);
    }
}

static void xhr_free_state(XhrState* xhr) {
    if (xhr->method) { mem_free(xhr->method); xhr->method = nullptr; }
    if (xhr->url) { mem_free(xhr->url); xhr->url = nullptr; }
    if (xhr->status_text) { mem_free(xhr->status_text); xhr->status_text = nullptr; }
    if (xhr->response_text) { mem_free(xhr->response_text); xhr->response_text = nullptr; }
    for (int i = 0; i < xhr->req_header_count; i++) {
        if (xhr->req_headers[i].name) mem_free(xhr->req_headers[i].name);
        if (xhr->req_headers[i].value) mem_free(xhr->req_headers[i].value);
    }
    xhr->req_header_count = 0;
    // resp_headers are owned by FetchResponse and freed via free_fetch_response
    xhr->resp_headers = nullptr;
    xhr->resp_header_count = 0;
    xhr->in_use = false;
}

static char* mem_strdup(const char* s) {
    if (!s) return nullptr;
    size_t len = strlen(s);
    char* dup = (char*)mem_calloc(1, len + 1, MEM_CAT_JS_RUNTIME);
    memcpy(dup, s, len);
    return dup;
}

static const char* status_text_for_code(long code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default:  return "";
    }
}

// ============================================================================
// Constructor
// ============================================================================

extern "C" Item js_xhr_new(void) {
    if (_xhr_count >= MAX_XHR) {
        log_error("xhr: pool exhausted (max %d)", MAX_XHR);
        return ItemNull;
    }

    int id = _xhr_count++;
    XhrState* xhr = &_xhr_pool[id];
    memset(xhr, 0, sizeof(XhrState));
    xhr->in_use = true;

    Item obj = js_new_object();
    xhr->js_object = obj;

    // hidden id
    xhr_set_int(obj, "__xhr_id", id);

    // initial properties
    xhr_set_int(obj, "readyState", 0);
    xhr_set_int(obj, "status", 0);
    xhr_set_str(obj, "statusText", "");
    xhr_set_str(obj, "responseText", "");
    xhr_set_str(obj, "response", "");
    xhr_set_str(obj, "responseType", "");
    xhr_set_int(obj, "timeout", 0);
    xhr_set_int(obj, "withCredentials", 0);

    // DONE constant
    xhr_set_int(obj, "DONE", 4);
    xhr_set_int(obj, "HEADERS_RECEIVED", 2);
    xhr_set_int(obj, "LOADING", 3);
    xhr_set_int(obj, "OPENED", 1);
    xhr_set_int(obj, "UNSENT", 0);

    // attach methods
    Item open_fn = js_new_function((void*)js_xhr_open, 3);
    xhr_set_str(obj, "", ""); // dummy to avoid collision
    Item k;

    k = (Item){.item = s2it(heap_create_name("open"))};
    js_property_set(obj, k, open_fn);

    Item send_fn = js_new_function((void*)js_xhr_send, 1);
    k = (Item){.item = s2it(heap_create_name("send"))};
    js_property_set(obj, k, send_fn);

    Item srh_fn = js_new_function((void*)js_xhr_set_request_header, 2);
    k = (Item){.item = s2it(heap_create_name("setRequestHeader"))};
    js_property_set(obj, k, srh_fn);

    Item abort_fn = js_new_function((void*)js_xhr_abort, 0);
    k = (Item){.item = s2it(heap_create_name("abort"))};
    js_property_set(obj, k, abort_fn);

    Item grh_fn = js_new_function((void*)js_xhr_get_response_header, 1);
    k = (Item){.item = s2it(heap_create_name("getResponseHeader"))};
    js_property_set(obj, k, grh_fn);

    Item garh_fn = js_new_function((void*)js_xhr_get_all_response_headers, 0);
    k = (Item){.item = s2it(heap_create_name("getAllResponseHeaders"))};
    js_property_set(obj, k, garh_fn);

    // addEventListener / removeEventListener stubs (jQuery sets onreadystatechange directly)
    Item noop_fn = js_new_function(nullptr, 0);
    k = (Item){.item = s2it(heap_create_name("addEventListener"))};
    js_property_set(obj, k, noop_fn);
    k = (Item){.item = s2it(heap_create_name("removeEventListener"))};
    js_property_set(obj, k, noop_fn);

    // callback slots (initially null)
    js_property_set(obj, (Item){.item = s2it(heap_create_name("onreadystatechange"))}, ItemNull);
    js_property_set(obj, (Item){.item = s2it(heap_create_name("onload"))}, ItemNull);
    js_property_set(obj, (Item){.item = s2it(heap_create_name("onerror"))}, ItemNull);
    js_property_set(obj, (Item){.item = s2it(heap_create_name("ontimeout"))}, ItemNull);
    js_property_set(obj, (Item){.item = s2it(heap_create_name("onabort"))}, ItemNull);
    js_property_set(obj, (Item){.item = s2it(heap_create_name("onloadend"))}, ItemNull);
    js_property_set(obj, (Item){.item = s2it(heap_create_name("onloadstart"))}, ItemNull);
    js_property_set(obj, (Item){.item = s2it(heap_create_name("onprogress"))}, ItemNull);

    log_debug("xhr: created XHR id=%d", id);
    return obj;
}

// ============================================================================
// Methods
// ============================================================================

extern "C" Item js_xhr_open(Item method_arg, Item url_arg, Item async_arg) {
    XhrState* xhr = xhr_state_from_this();
    if (!xhr) return make_js_undef();

    const char* method = fn_to_cstr(method_arg);
    const char* url = fn_to_cstr(url_arg);

    if (!method || !url) {
        log_error("xhr: open() requires method and url");
        return make_js_undef();
    }

    // reset any previous request state
    if (xhr->method) mem_free(xhr->method);
    if (xhr->url) mem_free(xhr->url);
    if (xhr->response_text) { mem_free(xhr->response_text); xhr->response_text = nullptr; }
    if (xhr->status_text) { mem_free(xhr->status_text); xhr->status_text = nullptr; }
    for (int i = 0; i < xhr->req_header_count; i++) {
        if (xhr->req_headers[i].name) mem_free(xhr->req_headers[i].name);
        if (xhr->req_headers[i].value) mem_free(xhr->req_headers[i].value);
    }
    xhr->req_header_count = 0;

    xhr->method = mem_strdup(method);
    xhr->url = mem_strdup(url);
    xhr->async_flag = true; // default true, ignored
    xhr->status = 0;
    xhr->response_size = 0;

    xhr->ready_state = 1; // OPENED
    xhr_fire_readystatechange(xhr);

    log_debug("xhr: open(%s, %s)", method, url);
    return make_js_undef();
}

extern "C" Item js_xhr_set_request_header(Item name_arg, Item value_arg) {
    XhrState* xhr = xhr_state_from_this();
    if (!xhr) return make_js_undef();

    if (xhr->ready_state != 1) {
        log_error("xhr: setRequestHeader() called in wrong state (%d)", xhr->ready_state);
        return make_js_undef();
    }

    const char* name = fn_to_cstr(name_arg);
    const char* value = fn_to_cstr(value_arg);
    if (!name || !value) return make_js_undef();

    if (xhr->req_header_count >= MAX_HEADERS) {
        log_error("xhr: too many request headers (max %d)", MAX_HEADERS);
        return make_js_undef();
    }

    xhr->req_headers[xhr->req_header_count].name = mem_strdup(name);
    xhr->req_headers[xhr->req_header_count].value = mem_strdup(value);
    xhr->req_header_count++;

    return make_js_undef();
}

extern "C" Item js_xhr_send(Item body_arg) {
    XhrState* xhr = xhr_state_from_this();
    if (!xhr) return make_js_undef();

    if (xhr->ready_state != 1) {
        log_error("xhr: send() called in wrong state (%d)", xhr->ready_state);
        return make_js_undef();
    }

    // Build FetchConfig
    FetchConfig config = {};
    config.method = xhr->method;
    config.timeout_seconds = 10;
    config.max_redirects = 5;
    config.verify_ssl = false;
    config.enable_compression = true;
    config.user_agent = "Lambda/1.0";

    // body
    const char* body_str = nullptr;
    TypeId body_type = get_type_id(body_arg);
    if (body_type != LMD_TYPE_NULL && body_type != LMD_TYPE_UNDEFINED) {
        body_str = fn_to_cstr(body_arg);
    }
    if (body_str) {
        config.body = body_str;
        config.body_size = strlen(body_str);
    }

    // headers: build "Name: Value" strings
    char* header_strs[MAX_HEADERS];
    for (int i = 0; i < xhr->req_header_count; i++) {
        size_t nlen = strlen(xhr->req_headers[i].name);
        size_t vlen = strlen(xhr->req_headers[i].value);
        char* hdr = (char*)mem_calloc(1, nlen + 2 + vlen + 1, MEM_CAT_JS_RUNTIME);
        memcpy(hdr, xhr->req_headers[i].name, nlen);
        hdr[nlen] = ':';
        hdr[nlen + 1] = ' ';
        memcpy(hdr + nlen + 2, xhr->req_headers[i].value, vlen);
        header_strs[i] = hdr;
    }
    config.headers = xhr->req_header_count > 0 ? header_strs : nullptr;
    config.header_count = xhr->req_header_count;

    log_debug("xhr: send() %s %s", xhr->method, xhr->url);

    // fire onloadstart
    Item loadstart_cb = xhr_get_prop(xhr->js_object, "onloadstart");
    if (get_type_id(loadstart_cb) == LMD_TYPE_FUNC) {
        js_call_function(loadstart_cb, xhr->js_object, nullptr, 0);
    }

    // Perform HTTP request (synchronous)
    FetchResponse* resp = http_fetch(xhr->url, &config);

    // free header strings
    for (int i = 0; i < xhr->req_header_count; i++) {
        mem_free(header_strs[i]);
    }

    if (resp) {
        xhr->status = resp->status_code;
        xhr->status_text = mem_strdup(status_text_for_code(resp->status_code));
        if (resp->data && resp->size > 0) {
            xhr->response_text = (char*)mem_calloc(1, resp->size + 1, MEM_CAT_JS_RUNTIME);
            memcpy(xhr->response_text, resp->data, resp->size);
            xhr->response_size = resp->size;
        }
        // store response headers for getResponseHeader/getAllResponseHeaders
        xhr->resp_headers = resp->response_headers;
        xhr->resp_header_count = resp->response_header_count;

        // HEADERS_RECEIVED (readyState 2)
        xhr->ready_state = 2;
        xhr_fire_readystatechange(xhr);

        // LOADING (readyState 3)
        xhr->ready_state = 3;
        xhr_fire_readystatechange(xhr);

        // DONE (readyState 4)
        xhr->ready_state = 4;
        xhr_set_int(xhr->js_object, "status", (int)xhr->status);
        xhr_set_str(xhr->js_object, "statusText", xhr->status_text);
        xhr_set_str(xhr->js_object, "responseText", xhr->response_text ? xhr->response_text : "");
        xhr_set_str(xhr->js_object, "response", xhr->response_text ? xhr->response_text : "");
        xhr_fire_readystatechange(xhr);

        // fire onload or onerror
        if (xhr->status >= 200 && xhr->status < 600) {
            Item onload = xhr_get_prop(xhr->js_object, "onload");
            if (get_type_id(onload) == LMD_TYPE_FUNC) {
                js_call_function(onload, xhr->js_object, nullptr, 0);
            }
        } else {
            Item onerror = xhr_get_prop(xhr->js_object, "onerror");
            if (get_type_id(onerror) == LMD_TYPE_FUNC) {
                js_call_function(onerror, xhr->js_object, nullptr, 0);
            }
        }

        // fire onloadend
        Item onloadend = xhr_get_prop(xhr->js_object, "onloadend");
        if (get_type_id(onloadend) == LMD_TYPE_FUNC) {
            js_call_function(onloadend, xhr->js_object, nullptr, 0);
        }

        // Don't free response yet — headers may be queried later.
        // We copy data above, so we can free the response body and struct
        // but keep response_headers alive until XHR reset.
        // Actually, let's copy headers too so we can free_fetch_response cleanly.
        if (resp->response_header_count > 0 && resp->response_headers) {
            char** copied = (char**)mem_calloc(resp->response_header_count, sizeof(char*), MEM_CAT_JS_RUNTIME);
            for (int i = 0; i < resp->response_header_count; i++) {
                copied[i] = mem_strdup(resp->response_headers[i]);
            }
            xhr->resp_headers = copied;
            xhr->resp_header_count = resp->response_header_count;
        }

        free_fetch_response(resp);

        log_debug("xhr: done status=%ld, %zu bytes", xhr->status, xhr->response_size);
    } else {
        // Network error
        xhr->status = 0;
        xhr->status_text = mem_strdup("");
        xhr->ready_state = 4;
        xhr_set_int(xhr->js_object, "readyState", 4);
        xhr_set_int(xhr->js_object, "status", 0);
        xhr_set_str(xhr->js_object, "statusText", "");
        xhr_set_str(xhr->js_object, "responseText", "");
        xhr_set_str(xhr->js_object, "response", "");
        xhr_fire_readystatechange(xhr);

        Item onerror = xhr_get_prop(xhr->js_object, "onerror");
        if (get_type_id(onerror) == LMD_TYPE_FUNC) {
            js_call_function(onerror, xhr->js_object, nullptr, 0);
        }
        Item onloadend = xhr_get_prop(xhr->js_object, "onloadend");
        if (get_type_id(onloadend) == LMD_TYPE_FUNC) {
            js_call_function(onloadend, xhr->js_object, nullptr, 0);
        }

        log_error("xhr: network error for %s %s", xhr->method, xhr->url);
    }

    return make_js_undef();
}

extern "C" Item js_xhr_abort(void) {
    XhrState* xhr = xhr_state_from_this();
    if (!xhr) return make_js_undef();

    if (xhr->ready_state == 0 || xhr->ready_state == 4) {
        return make_js_undef(); // nothing to abort
    }

    xhr->ready_state = 4;
    xhr->status = 0;
    xhr_set_int(xhr->js_object, "readyState", 4);
    xhr_set_int(xhr->js_object, "status", 0);
    xhr_set_str(xhr->js_object, "responseText", "");
    xhr_set_str(xhr->js_object, "response", "");
    xhr_fire_readystatechange(xhr);

    Item onabort = xhr_get_prop(xhr->js_object, "onabort");
    if (get_type_id(onabort) == LMD_TYPE_FUNC) {
        js_call_function(onabort, xhr->js_object, nullptr, 0);
    }
    Item onloadend = xhr_get_prop(xhr->js_object, "onloadend");
    if (get_type_id(onloadend) == LMD_TYPE_FUNC) {
        js_call_function(onloadend, xhr->js_object, nullptr, 0);
    }

    log_debug("xhr: aborted");
    return make_js_undef();
}

extern "C" Item js_xhr_get_response_header(Item name_arg) {
    XhrState* xhr = xhr_state_from_this();
    if (!xhr || xhr->ready_state < 2) return ItemNull;

    const char* name = fn_to_cstr(name_arg);
    if (!name) return ItemNull;

    size_t name_len = strlen(name);
    for (int i = 0; i < xhr->resp_header_count; i++) {
        const char* h = xhr->resp_headers[i];
        // headers are "Name: Value" format
        if (strncasecmp(h, name, name_len) == 0 && h[name_len] == ':') {
            const char* val = h + name_len + 1;
            while (*val == ' ') val++;
            return (Item){.item = s2it(heap_create_name(val))};
        }
    }
    return ItemNull;
}

extern "C" Item js_xhr_get_all_response_headers(void) {
    XhrState* xhr = xhr_state_from_this();
    if (!xhr || xhr->ready_state < 2) return (Item){.item = s2it(heap_create_name(""))};

    // concatenate all headers with \r\n
    size_t total = 0;
    for (int i = 0; i < xhr->resp_header_count; i++) {
        total += strlen(xhr->resp_headers[i]) + 2; // \r\n
    }

    if (total == 0) return (Item){.item = s2it(heap_create_name(""))};

    char* buf = (char*)mem_calloc(1, total + 1, MEM_CAT_JS_RUNTIME);
    char* p = buf;
    for (int i = 0; i < xhr->resp_header_count; i++) {
        size_t len = strlen(xhr->resp_headers[i]);
        memcpy(p, xhr->resp_headers[i], len);
        p += len;
        *p++ = '\r';
        *p++ = '\n';
    }
    *p = '\0';

    Item result = (Item){.item = s2it(heap_create_name(buf))};
    mem_free(buf);
    return result;
}

// ============================================================================
// Reset
// ============================================================================

extern "C" void js_xhr_reset(void) {
    for (int i = 0; i < _xhr_count; i++) {
        if (_xhr_pool[i].in_use) {
            // free copied response headers
            for (int h = 0; h < _xhr_pool[i].resp_header_count; h++) {
                if (_xhr_pool[i].resp_headers[h]) mem_free(_xhr_pool[i].resp_headers[h]);
            }
            if (_xhr_pool[i].resp_headers) mem_free(_xhr_pool[i].resp_headers);
            _xhr_pool[i].resp_headers = nullptr;
            _xhr_pool[i].resp_header_count = 0;
            xhr_free_state(&_xhr_pool[i]);
        }
    }
    _xhr_count = 0;
    log_debug("xhr: reset");
}
