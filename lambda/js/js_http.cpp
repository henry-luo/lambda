/**
 * js_http.cpp — Node.js-style 'http' module for LambdaJS
 *
 * Provides http.createServer(), http.request(), http.get(),
 * IncomingMessage, ServerResponse, and STATUS_CODES.
 *
 * Built on top of libuv TCP (same pattern as js_net.cpp).
 * HTTP/1.1 parsing is done inline (lightweight, no external deps).
 *
 * Registered as built-in module 'http' via js_module_get().
 */
#include "js_runtime.h"
#include "js_event_loop.h"
#include "js_class.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"
#include "../../lib/mem.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>

extern "C" Item js_readable_new(Item opts);
extern "C" Item js_readable_push(Item self, Item chunk);
extern "C" Item js_stream_destroy(Item self, Item err);
extern "C" Item js_async_hooks_enter_resource(Item resource);
extern "C" void js_async_hooks_restore_resource(Item previous);
extern "C" Item js_als_capture_context(void);
extern "C" Item js_als_context_call(Item context, Item callback, Item this_val, Item arg1, int64_t has_arg);

static Item make_string_item(const char* str, int len) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, (size_t)(len > 0 ? len : 0));
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

static bool js_http_has_marker(Item obj, const char* name) {
    Item marker = js_property_get(obj, make_string_item(name));
    TypeId type = get_type_id(marker);
    return marker.item != 0 && type != LMD_TYPE_UNDEFINED && type != LMD_TYPE_NULL;
}

static Item js_http_receiver(Item candidate, const char* marker) {
    if (js_http_has_marker(candidate, marker)) return candidate;
    Item self = js_get_this();
    if (js_http_has_marker(self, marker)) return self;
    return candidate;
}

static bool http_header_name_equals(String* name, const char* text, int text_len) {
    if (!name || (int)name->len != text_len) return false;
    for (int i = 0; i < text_len; i++) {
        char c = name->chars[i];
        if (c >= 'A' && c <= 'Z') c = c + 32;
        if (c != text[i]) return false;
    }
    return true;
}

// =============================================================================
// HTTP Status Codes
// =============================================================================

static const char* http_status_text(int code) {
    switch (code) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 206: return "Partial Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 406: return "Not Acceptable";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 416: return "Range Not Satisfiable";
        case 422: return "Unprocessable Entity";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        default:  return "unknown";
    }
}

static const char* http_response_status_message(Item response, int status) {
    Item status_message = js_property_get(response, make_string_item("statusMessage"));
    if (get_type_id(status_message) == LMD_TYPE_STRING) {
        String* sm = it2s(status_message);
        if (sm->len > 0) return sm->chars;
    }
    return http_status_text(status);
}

// =============================================================================
// Inline HTTP/1.1 Parser
// =============================================================================

typedef struct ParsedRequest {
    char method[16];
    char url[4096];
    char http_version[16];
    // headers stored as key-value pairs
    char raw_header_names[64][128];
    char raw_header_values[64][4096];
    char header_names[64][128];
    char header_values[64][4096];
    int  header_count;
    const char* body;
    int  body_len;
} ParsedRequest;

// parse an HTTP request from raw bytes. Returns 0 on success, -1 on incomplete/error.
// Sets *consumed to the number of bytes consumed (headers + body).
static int parse_http_request(const char* data, int data_len, ParsedRequest* req, int* consumed) {
    memset(req, 0, sizeof(ParsedRequest));
    *consumed = 0;

    // find end of headers
    const char* hdr_end = NULL;
    for (int i = 0; i + 3 < data_len; i++) {
        if (data[i] == '\r' && data[i+1] == '\n' && data[i+2] == '\r' && data[i+3] == '\n') {
            hdr_end = data + i + 4;
            break;
        }
    }
    if (!hdr_end) return -1; // incomplete

    // parse request line
    const char* p = data;
    const char* line_end = (const char*)memchr(p, '\r', hdr_end - p);
    if (!line_end) return -1;

    // method
    const char* sp1 = (const char*)memchr(p, ' ', line_end - p);
    if (!sp1) return -1;
    int mlen = (int)(sp1 - p);
    if (mlen >= 16) mlen = 15;
    memcpy(req->method, p, mlen);
    req->method[mlen] = '\0';

    // url
    p = sp1 + 1;
    const char* sp2 = (const char*)memchr(p, ' ', line_end - p);
    if (!sp2) return -1;
    int ulen = (int)(sp2 - p);
    if (ulen >= 4096) ulen = 4095;
    memcpy(req->url, p, ulen);
    req->url[ulen] = '\0';

    // HTTP version
    p = sp2 + 1;
    int vlen = (int)(line_end - p);
    if (vlen >= 16) vlen = 15;
    memcpy(req->http_version, p, vlen);
    req->http_version[vlen] = '\0';

    // parse headers
    p = line_end + 2; // skip \r\n
    while (p < hdr_end - 2) {
        line_end = (const char*)memchr(p, '\r', hdr_end - p);
        if (!line_end || line_end == p) break;

        const char* colon = (const char*)memchr(p, ':', line_end - p);
        if (!colon) { p = line_end + 2; continue; }

        int nlen = (int)(colon - p);
        const char* vstart = colon + 1;
        while (vstart < line_end && *vstart == ' ') vstart++;
        int vvlen = (int)(line_end - vstart);

        if (req->header_count < 64) {
            if (nlen >= 128) nlen = 127;
            if (vvlen >= 4096) vvlen = 4095;
            memcpy(req->raw_header_names[req->header_count], p, nlen);
            req->raw_header_names[req->header_count][nlen] = '\0';
            memcpy(req->raw_header_values[req->header_count], vstart, vvlen);
            req->raw_header_values[req->header_count][vvlen] = '\0';
            memcpy(req->header_names[req->header_count], p, nlen);
            req->header_names[req->header_count][nlen] = '\0';
            // lowercase the header name
            for (int i = 0; i < nlen; i++) {
                char c = req->header_names[req->header_count][i];
                if (c >= 'A' && c <= 'Z') req->header_names[req->header_count][i] = c + 32;
            }
            memcpy(req->header_values[req->header_count], vstart, vvlen);
            req->header_values[req->header_count][vvlen] = '\0';
            req->header_count++;
        }

        p = line_end + 2;
    }

    int hdr_size = (int)(hdr_end - data);

    // check for Content-Length to read body
    int content_length = 0;
    for (int i = 0; i < req->header_count; i++) {
        if (strcmp(req->header_names[i], "content-length") == 0) {
            content_length = atoi(req->header_values[i]);
            break;
        }
    }

    if (content_length > 0) {
        if (data_len - hdr_size < content_length) return -1; // incomplete body
        req->body = data + hdr_size;
        req->body_len = content_length;
        *consumed = hdr_size + content_length;
    } else {
        req->body = NULL;
        req->body_len = 0;
        *consumed = hdr_size;
    }

    return 0;
}

// =============================================================================
// HTTP Connection — handles per-client state
// =============================================================================

struct JsHttpServer; // forward

typedef struct JsHttpConn {
    uv_tcp_t     tcp;
    JsHttpServer* server;
    Item         async_resource;
    char*        recv_buf;
    int          recv_len;
    int          recv_cap;
    bool         destroyed;
} JsHttpConn;

// =============================================================================
// ServerResponse — writable response object
// =============================================================================

static void http_response_write_partial(Item self);

static bool http_header_name_case_equals(Item a, Item b) {
    if (get_type_id(a) != LMD_TYPE_STRING || get_type_id(b) != LMD_TYPE_STRING) return false;
    String* as = it2s(a);
    String* bs = it2s(b);
    if (as->len != bs->len) return false;
    for (size_t i = 0; i < as->len; i++) {
        char ac = as->chars[i];
        char bc = bs->chars[i];
        if (ac >= 'A' && ac <= 'Z') ac = ac + 32;
        if (bc >= 'A' && bc <= 'Z') bc = bc + 32;
        if (ac != bc) return false;
    }
    return true;
}

static bool http_is_valid_header_token(Item name_item) {
    if (get_type_id(name_item) != LMD_TYPE_STRING) return false;
    String* s = it2s(name_item);
    if (s->len == 0) return false;
    for (size_t i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->chars[i];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '!' || c == '#' ||
                  c == '$' || c == '%' || c == '&' || c == '\'' ||
                  c == '*' || c == '+' || c == '-' || c == '.' ||
                  c == '^' || c == '_' || c == '`' || c == '|' ||
                  c == '~';
        if (!ok) return false;
    }
    return true;
}

static Item http_item_to_header_string(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_STRING) return value;
    if (type == LMD_TYPE_INT) {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "%lld", (long long)it2i(value));
        return make_string_item(buf, len);
    }
    if (type == LMD_TYPE_INT64) {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "%lld", (long long)it2l(value));
        return make_string_item(buf, len);
    }
    if (type == LMD_TYPE_FLOAT) {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "%g", value.get_double());
        return make_string_item(buf, len);
    }
    if (type == LMD_TYPE_BOOL) return make_string_item(it2b(value) ? "true" : "false");
    if (type == LMD_TYPE_NULL) return make_string_item("null");
    return value;
}

static Item http_lowercase_header_name(Item name_item) {
    if (get_type_id(name_item) != LMD_TYPE_STRING) return name_item;
    String* s = it2s(name_item);
    char lc[256];
    int len = (int)s->len < 255 ? (int)s->len : 255;
    for (int i = 0; i < len; i++) {
        char c = s->chars[i];
        lc[i] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
    }
    return make_string_item(lc, len);
}

static Item http_validate_header_name(Item name_item) {
    Item name = http_item_to_header_string(name_item);
    if (get_type_id(name_item) != LMD_TYPE_STRING || !http_is_valid_header_token(name)) {
        char msg[512];
        if (get_type_id(name) == LMD_TYPE_STRING) {
            String* s = it2s(name);
            snprintf(msg, sizeof(msg), "Header name must be a valid HTTP token [\"%.*s\"]",
                     (int)s->len, s->chars);
        } else {
            snprintf(msg, sizeof(msg), "Header name must be a valid HTTP token");
        }
        return js_throw_type_error_code("ERR_INVALID_HTTP_TOKEN", msg);
    }
    return name;
}

static Item http_validate_header_value(Item name_item, Item value_item) {
    if (get_type_id(value_item) == LMD_TYPE_UNDEFINED) {
        char msg[512];
        String* s = get_type_id(name_item) == LMD_TYPE_STRING ? it2s(name_item) : NULL;
        if (s) {
            snprintf(msg, sizeof(msg), "Invalid value \"undefined\" for header \"%.*s\"",
                     (int)s->len, s->chars);
        } else {
            snprintf(msg, sizeof(msg), "Invalid value \"undefined\" for header");
        }
        return js_throw_type_error_code("ERR_HTTP_INVALID_HEADER_VALUE", msg);
    }
    return http_item_to_header_string(value_item);
}

static Item http_response_raw_headers(Item self) {
    Item raw = js_property_get(self, make_string_item("__raw_headers__"));
    if (get_type_id(raw) != LMD_TYPE_ARRAY) {
        raw = js_array_new(0);
        js_property_set(self, make_string_item("__raw_headers__"), raw);
    }
    return raw;
}

static void http_raw_headers_remove_name(Item self, Item name) {
    Item raw = js_property_get(self, make_string_item("__raw_headers__"));
    if (get_type_id(raw) != LMD_TYPE_ARRAY) return;
    Item next = js_array_new(0);
    int64_t len = js_array_length(raw);
    for (int64_t i = 0; i + 1 < len; i += 2) {
        Item raw_name = js_array_get_int(raw, i);
        Item raw_value = js_array_get_int(raw, i + 1);
        if (http_header_name_case_equals(raw_name, name)) continue;
        js_array_push(next, raw_name);
        js_array_push(next, raw_value);
    }
    js_property_set(self, make_string_item("__raw_headers__"), next);
}

static void http_raw_headers_append(Item self, Item name, Item value) {
    Item raw = http_response_raw_headers(self);
    js_array_push(raw, name);
    js_array_push(raw, value);
}

static void http_response_set_header_pair(Item self, Item name_item, Item value_item) {
    Item name = http_validate_header_name(name_item);
    if (name.item == 0) return;
    Item value = http_validate_header_value(name, value_item);
    if (value.item == 0) return;

    Item headers = js_property_get(self, make_string_item("__headers__"));
    if (get_type_id(headers) != LMD_TYPE_MAP) {
        headers = js_new_object();
        js_property_set(self, make_string_item("__headers__"), headers);
    }
    js_property_set(headers, http_lowercase_header_name(name), value);
    http_raw_headers_remove_name(self, name);
    http_raw_headers_append(self, name, value);
}

static void http_response_append_raw_header_pair(Item self, Item name_item, Item value_item) {
    Item name = http_validate_header_name(name_item);
    if (name.item == 0) return;
    Item value = http_validate_header_value(name, value_item);
    if (value.item == 0) return;

    Item headers = js_property_get(self, make_string_item("__headers__"));
    if (get_type_id(headers) != LMD_TYPE_MAP) {
        headers = js_new_object();
        js_property_set(self, make_string_item("__headers__"), headers);
    }
    js_property_set(headers, http_lowercase_header_name(name), value);
    http_raw_headers_append(self, name, value);
}

static bool http_response_headers_committed(Item self) {
    Item sent = js_property_get(self, make_string_item("__sent__"));
    if (get_type_id(sent) == LMD_TYPE_BOOL && it2b(sent)) return true;
    Item headers_sent = js_property_get(self, make_string_item("__headers_sent__"));
    if (get_type_id(headers_sent) == LMD_TYPE_BOOL && it2b(headers_sent)) return true;
    Item write_head = js_property_get(self, make_string_item("__write_head_called__"));
    return get_type_id(write_head) == LMD_TYPE_BOOL && it2b(write_head);
}

static int http_response_append_headers(char* resp_buf, int pos, int cap, Item self,
                                        bool* has_content_length, bool* has_content_type) {
    Item raw = js_property_get(self, make_string_item("__raw_headers__"));
    if (get_type_id(raw) == LMD_TYPE_ARRAY && js_array_length(raw) > 0) {
        int64_t len = js_array_length(raw);
        for (int64_t i = 0; i + 1 < len; i += 2) {
            Item k = js_array_get_int(raw, i);
            Item v = js_array_get_int(raw, i + 1);
            if (get_type_id(k) == LMD_TYPE_STRING && get_type_id(v) == LMD_TYPE_STRING) {
                String* ks = it2s(k);
                String* vs = it2s(v);
                pos += snprintf(resp_buf + pos, cap - pos,
                                "%.*s: %.*s\r\n", (int)ks->len, ks->chars, (int)vs->len, vs->chars);
                if (http_header_name_equals(ks, "content-length", 14)) *has_content_length = true;
                if (http_header_name_equals(ks, "content-type", 12)) *has_content_type = true;
            }
        }
        return pos;
    }

    Item headers = js_property_get(self, make_string_item("__headers__"));
    if (get_type_id(headers) == LMD_TYPE_MAP) {
        Item keys = js_object_keys(headers);
        int64_t nkeys = js_array_length(keys);
        for (int64_t i = 0; i < nkeys; i++) {
            Item k = js_array_get_int(keys, i);
            Item v = js_property_get(headers, k);
            if (get_type_id(k) == LMD_TYPE_STRING && get_type_id(v) == LMD_TYPE_STRING) {
                String* ks = it2s(k);
                String* vs = it2s(v);
                pos += snprintf(resp_buf + pos, cap - pos,
                                "%.*s: %.*s\r\n", (int)ks->len, ks->chars, (int)vs->len, vs->chars);
                if (http_header_name_equals(ks, "content-length", 14)) *has_content_length = true;
                if (http_header_name_equals(ks, "content-type", 12)) *has_content_type = true;
            }
        }
    }
    return pos;
}

// response.writeHead(statusCode, headers?)
static Item http_response_writeHead(Item self, Item status_item, Item reason_or_headers, Item headers_item) {
    if (http_response_headers_committed(self)) {
        return js_throw_error_with_code("ERR_HTTP_HEADERS_SENT", "Cannot write headers after they are sent to the client");
    }
    js_property_set(self, make_string_item("statusCode"), status_item);

    int status = 200;
    if (get_type_id(status_item) == LMD_TYPE_INT) status = (int)it2i(status_item);

    Item headers_arg = reason_or_headers;
    if (get_type_id(reason_or_headers) == LMD_TYPE_STRING) {
        js_property_set(self, make_string_item("statusMessage"), reason_or_headers);
        headers_arg = headers_item;
    } else {
        js_property_set(self, make_string_item("statusMessage"), make_string_item(http_status_text(status)));
        TypeId reason_type = get_type_id(reason_or_headers);
        if ((reason_type == LMD_TYPE_UNDEFINED || reason_type == LMD_TYPE_NULL) &&
            (get_type_id(headers_item) == LMD_TYPE_ARRAY || get_type_id(headers_item) == LMD_TYPE_MAP)) {
            headers_arg = headers_item;
        }
    }

    if (get_type_id(headers_arg) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(headers_arg);
        if ((len & 1) != 0) {
            return js_throw_type_error_code("ERR_INVALID_ARG_VALUE",
                                            "The argument 'headers' must be an even-length array");
        }
        for (int64_t i = 0; i + 1 < len; i += 2) {
            http_raw_headers_remove_name(self, js_array_get_int(headers_arg, i));
        }
        for (int64_t i = 0; i + 1 < len; i += 2) {
            http_response_append_raw_header_pair(self,
                js_array_get_int(headers_arg, i),
                js_array_get_int(headers_arg, i + 1));
        }
    } else if (get_type_id(headers_arg) == LMD_TYPE_MAP) {
        Item keys = js_object_keys(headers_arg);
        int64_t len = js_array_length(keys);
        for (int64_t i = 0; i < len; i++) {
            Item k = js_array_get_int(keys, i);
            http_response_set_header_pair(self, k, js_property_get(headers_arg, k));
        }
    }

    js_property_set(self, make_string_item("__write_head_called__"), (Item){.item = b2it(true)});
    js_property_set(self, make_string_item("headersSent"), (Item){.item = b2it(true)});
    return self;
}

extern "C" Item js_http_res_writeHead(Item self, Item status_item, Item headers_item) {
    return http_response_writeHead(self, status_item, headers_item, make_js_undefined());
}

// response.setHeader(name, value)
extern "C" Item js_http_res_setHeader(Item self, Item name_item, Item value_item) {
    if (http_response_headers_committed(self)) {
        return js_throw_error_with_code("ERR_HTTP_HEADERS_SENT",
                                        "Cannot set headers after they are sent to the client");
    }
    http_response_set_header_pair(self, name_item, value_item);
    return self;
}

// response.getHeader(name)
extern "C" Item js_http_res_getHeader(Item self, Item name_item) {
    Item headers = js_property_get(self, make_string_item("__headers__"));
    if (get_type_id(headers) != LMD_TYPE_MAP) return make_js_undefined();
    if (get_type_id(name_item) == LMD_TYPE_STRING) {
        return js_property_get(headers, http_lowercase_header_name(name_item));
    }
    return make_js_undefined();
}

// response.getHeaders()
extern "C" Item js_http_res_getHeaders(Item self) {
    Item headers = js_property_get(self, make_string_item("__headers__"));
    if (get_type_id(headers) != LMD_TYPE_MAP) return js_new_object();
    return headers;
}

// response.removeHeader(name)
extern "C" Item js_http_res_removeHeader(Item self, Item name_item) {
    if (http_response_headers_committed(self)) {
        return js_throw_error_with_code("ERR_HTTP_HEADERS_SENT",
                                        "Cannot remove headers after they are sent to the client");
    }
    Item headers = js_property_get(self, make_string_item("__headers__"));
    if (get_type_id(headers) != LMD_TYPE_MAP) return self;
    if (get_type_id(name_item) == LMD_TYPE_STRING) {
        js_property_set(headers, http_lowercase_header_name(name_item), make_js_undefined());
        http_raw_headers_remove_name(self, name_item);
    }
    return self;
}

// response.write(chunk) — accumulate body data
extern "C" Item js_http_res_write(Item self, Item chunk_item) {
    Item body = js_property_get(self, make_string_item("__body__"));
    if (get_type_id(body) != LMD_TYPE_STRING) body = make_string_item("", 0);

    if (get_type_id(chunk_item) == LMD_TYPE_STRING) {
        // concatenate strings
        String* existing = it2s(body);
        String* chunk = it2s(chunk_item);
        int new_len = (int)existing->len + (int)chunk->len;
        char* buf = (char*)mem_alloc(new_len + 1, MEM_CAT_JS_RUNTIME);
        memcpy(buf, existing->chars, existing->len);
        memcpy(buf + existing->len, chunk->chars, chunk->len);
        buf[new_len] = '\0';
        Item new_body = make_string_item(buf, new_len);
        mem_free(buf);
        js_property_set(self, make_string_item("__body__"), new_body);
    }
    Item ending = js_property_get(self, make_string_item("__ending__"));
    if (!(get_type_id(ending) == LMD_TYPE_BOOL && it2b(ending))) {
        http_response_write_partial(self);
    }
    return (Item){.item = b2it(true)};
}

// helper: serialize headers + body to HTTP response bytes, write to socket
static void http_response_flush(Item self) {
    Item handle_item = js_property_get(self, make_string_item("__conn__"));
    if (handle_item.item == 0) return;
    JsHttpConn* conn = (JsHttpConn*)(uintptr_t)it2i(handle_item);
    if (!conn || conn->destroyed) return;

    Item sent = js_property_get(self, make_string_item("__sent__"));
    if (get_type_id(sent) == LMD_TYPE_BOOL && it2b(sent)) return; // already sent

    // get status code
    Item status_item = js_property_get(self, make_string_item("statusCode"));
    int status = 200;
    if (get_type_id(status_item) == LMD_TYPE_INT) status = (int)it2i(status_item);

    // build response
    char resp_buf[65536];
    int pos = 0;

    // status line
    pos += snprintf(resp_buf + pos, sizeof(resp_buf) - pos,
                    "HTTP/1.1 %d %s\r\n", status, http_response_status_message(self, status));

    // body
    Item body = js_property_get(self, make_string_item("__body__"));
    const char* body_data = "";
    int body_len = 0;
    if (get_type_id(body) == LMD_TYPE_STRING) {
        String* bs = it2s(body);
        body_data = bs->chars;
        body_len = (int)bs->len;
    }

    bool has_content_length = false;
    bool has_content_type = false;
    pos = http_response_append_headers(resp_buf, pos, (int)sizeof(resp_buf), self,
                                       &has_content_length, &has_content_type);

    if (!has_content_length) {
        pos += snprintf(resp_buf + pos, sizeof(resp_buf) - pos,
                        "Content-Length: %d\r\n", body_len);
    }
    if (!has_content_type) {
        pos += snprintf(resp_buf + pos, sizeof(resp_buf) - pos,
                        "Content-Type: text/plain\r\n");
    }
    pos += snprintf(resp_buf + pos, sizeof(resp_buf) - pos, "Connection: close\r\n\r\n");

    // copy headers + body
    int total = pos + body_len;
    char* full = (char*)mem_alloc(total, MEM_CAT_JS_RUNTIME);
    memcpy(full, resp_buf, pos);
    if (body_len > 0) memcpy(full + pos, body_data, body_len);

    uv_buf_t buf = uv_buf_init(full, (unsigned int)total);
    uv_write_t* wreq = (uv_write_t*)mem_calloc(1, sizeof(uv_write_t), MEM_CAT_JS_RUNTIME);
    wreq->data = full;

    uv_write(wreq, (uv_stream_t*)&conn->tcp, &buf, 1,
        [](uv_write_t* req, int status) {
            if (req->data) mem_free(req->data);
            // close connection after response
            JsHttpConn* c = (JsHttpConn*)((uv_stream_t*)req->handle)->data;
            if (c && !c->destroyed) {
                c->destroyed = true;
                uv_close((uv_handle_t*)req->handle, [](uv_handle_t* h) {
                    JsHttpConn* cc = (JsHttpConn*)h->data;
                    if (cc) {
                        if (cc->recv_buf) mem_free(cc->recv_buf);
                        mem_free(cc);
                    }
                });
            }
            mem_free(req);
        });

    js_property_set(self, make_string_item("__sent__"), (Item){.item = b2it(true)});
    js_property_set(self, make_string_item("__write_head_called__"), (Item){.item = b2it(true)});
    js_property_set(self, make_string_item("headersSent"), (Item){.item = b2it(true)});
}

static void http_response_write_partial(Item self) {
    Item handle_item = js_property_get(self, make_string_item("__conn__"));
    if (handle_item.item == 0) return;
    JsHttpConn* conn = (JsHttpConn*)(uintptr_t)it2i(handle_item);
    if (!conn || conn->destroyed) return;

    Item body = js_property_get(self, make_string_item("__body__"));
    const char* body_data = "";
    int body_len = 0;
    if (get_type_id(body) == LMD_TYPE_STRING) {
        String* bs = it2s(body);
        body_data = bs->chars;
        body_len = (int)bs->len;
    }
    if (body_len <= 0) return;

    Item headers_sent = js_property_get(self, make_string_item("__headers_sent__"));
    bool send_headers = !(get_type_id(headers_sent) == LMD_TYPE_BOOL && it2b(headers_sent));

    char resp_buf[65536];
    int pos = 0;
    if (send_headers) {
        Item status_item = js_property_get(self, make_string_item("statusCode"));
        int status = 200;
        if (get_type_id(status_item) == LMD_TYPE_INT) status = (int)it2i(status_item);
        pos += snprintf(resp_buf + pos, sizeof(resp_buf) - pos,
                        "HTTP/1.1 %d %s\r\n", status, http_response_status_message(self, status));

        bool has_content_length = true;
        bool has_content_type = false;
        pos = http_response_append_headers(resp_buf, pos, (int)sizeof(resp_buf), self,
                                           &has_content_length, &has_content_type);
        if (!has_content_type) {
            pos += snprintf(resp_buf + pos, sizeof(resp_buf) - pos,
                            "Content-Type: text/plain\r\n");
        }
        pos += snprintf(resp_buf + pos, sizeof(resp_buf) - pos, "Connection: close\r\n\r\n");
        js_property_set(self, make_string_item("__headers_sent__"), (Item){.item = b2it(true)});
        js_property_set(self, make_string_item("__write_head_called__"), (Item){.item = b2it(true)});
        js_property_set(self, make_string_item("headersSent"), (Item){.item = b2it(true)});
    }

    int total = pos + body_len;
    char* full = (char*)mem_alloc(total, MEM_CAT_JS_RUNTIME);
    if (pos > 0) memcpy(full, resp_buf, (size_t)pos);
    memcpy(full + pos, body_data, (size_t)body_len);

    uv_buf_t buf = uv_buf_init(full, (unsigned int)total);
    uv_write_t* wreq = (uv_write_t*)mem_calloc(1, sizeof(uv_write_t), MEM_CAT_JS_RUNTIME);
    wreq->data = full;
    uv_write(wreq, (uv_stream_t*)&conn->tcp, &buf, 1,
        [](uv_write_t* req, int status) {
            if (req->data) mem_free(req->data);
            mem_free(req);
        });

    js_property_set(self, make_string_item("__body__"), make_string_item("", 0));
}

// response.end([data]) — finalize and send
extern "C" Item js_http_res_end(Item self, Item data_item) {
    js_property_set(self, make_string_item("__ending__"), (Item){.item = b2it(true)});
    if (get_type_id(data_item) == LMD_TYPE_STRING) {
        js_http_res_write(self, data_item);
    }
    js_property_set(self, make_string_item("__ending__"), (Item){.item = b2it(false)});
    http_response_flush(self);
    return self;
}

static Item js_http_res_inst_writeHead(Item maybe_self, Item status_item, Item headers_item) {
    Item self = js_http_receiver(maybe_self, "__conn__");
    if (self.item != maybe_self.item) {
        return http_response_writeHead(self, maybe_self, status_item, headers_item);
    }
    return http_response_writeHead(self, status_item, headers_item, make_js_undefined());
}

static Item js_http_res_inst_setHeader(Item maybe_self, Item name_item, Item value_item) {
    Item self = js_http_receiver(maybe_self, "__conn__");
    if (self.item != maybe_self.item) {
        return js_http_res_setHeader(self, maybe_self, name_item);
    }
    return js_http_res_setHeader(self, name_item, value_item);
}

static Item js_http_res_inst_getHeader(Item maybe_self, Item name_item) {
    Item self = js_http_receiver(maybe_self, "__conn__");
    return js_http_res_getHeader(self, self.item == maybe_self.item ? name_item : maybe_self);
}

static Item js_http_res_inst_getHeaders(Item maybe_self) {
    return js_http_res_getHeaders(js_http_receiver(maybe_self, "__conn__"));
}

static Item js_http_res_inst_removeHeader(Item maybe_self, Item name_item) {
    Item self = js_http_receiver(maybe_self, "__conn__");
    return js_http_res_removeHeader(self, self.item == maybe_self.item ? name_item : maybe_self);
}

static Item js_http_res_inst_write(Item maybe_self, Item chunk_item) {
    Item self = js_http_receiver(maybe_self, "__conn__");
    return js_http_res_write(self, self.item == maybe_self.item ? chunk_item : maybe_self);
}

static Item js_http_res_inst_end(Item maybe_self, Item data_item) {
    Item self = js_http_receiver(maybe_self, "__conn__");
    return js_http_res_end(self, self.item == maybe_self.item ? data_item : maybe_self);
}

// create a ServerResponse object for a connection
static Item make_response_object(JsHttpConn* conn) {
    Item res = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(res, JS_CLASS_SERVER_RESPONSE);  // A3-T3b
    js_property_set(res, make_string_item("__conn__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)conn)});
    js_property_set(res, make_string_item("statusCode"), (Item){.item = i2it(200)});
    js_property_set(res, make_string_item("__headers__"), js_new_object());
    js_property_set(res, make_string_item("__body__"), make_string_item("", 0));
    js_property_set(res, make_string_item("__sent__"), (Item){.item = b2it(false)});
    js_property_set(res, make_string_item("__headers_sent__"), (Item){.item = b2it(false)});
    js_property_set(res, make_string_item("__write_head_called__"), (Item){.item = b2it(false)});
    js_property_set(res, make_string_item("headersSent"), (Item){.item = b2it(false)});
    js_property_set(res, make_string_item("__raw_headers__"), js_array_new(0));
    js_property_set(res, make_string_item("__ending__"), (Item){.item = b2it(false)});
    js_property_set(res, make_string_item("statusMessage"), make_string_item(http_status_text(200)));

    js_property_set(res, make_string_item("writeHead"),
                    js_new_function((void*)js_http_res_inst_writeHead, 3));
    js_property_set(res, make_string_item("setHeader"),
                    js_new_function((void*)js_http_res_inst_setHeader, 3));
    js_property_set(res, make_string_item("getHeader"),
                    js_new_function((void*)js_http_res_inst_getHeader, 2));
    js_property_set(res, make_string_item("getHeaders"),
                    js_new_function((void*)js_http_res_inst_getHeaders, 1));
    js_property_set(res, make_string_item("removeHeader"),
                    js_new_function((void*)js_http_res_inst_removeHeader, 2));
    js_property_set(res, make_string_item("write"),
                    js_new_function((void*)js_http_res_inst_write, 2));
    js_property_set(res, make_string_item("end"),
                    js_new_function((void*)js_http_res_inst_end, 2));

    return res;
}

// =============================================================================
// IncomingMessage — readable request object
// =============================================================================

// create an IncomingMessage from a parsed HTTP request
static void http_request_headers_append(Item headers, const char* name, const char* value) {
    Item key = make_string_item(name);
    Item incoming = make_string_item(value);
    Item existing = js_property_get(headers, key);
    if (get_type_id(existing) == LMD_TYPE_STRING) {
        String* es = it2s(existing);
        String* is = it2s(incoming);
        const char* sep = strcmp(name, "cookie") == 0 ? "; " : ", ";
        int sep_len = (int)strlen(sep);
        int total = (int)es->len + sep_len + (int)is->len;
        char* buf = (char*)mem_alloc(total + 1, MEM_CAT_JS_RUNTIME);
        memcpy(buf, es->chars, es->len);
        memcpy(buf + es->len, sep, (size_t)sep_len);
        memcpy(buf + es->len + sep_len, is->chars, is->len);
        buf[total] = '\0';
        js_property_set(headers, key, make_string_item(buf, total));
        mem_free(buf);
    } else {
        js_property_set(headers, key, incoming);
    }
}

static Item make_request_object(ParsedRequest* req) {
    Item msg = js_readable_new(ItemNull);
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(msg, JS_CLASS_INCOMING_MESSAGE);  // A3-T3b

    js_property_set(msg, make_string_item("method"), make_string_item(req->method));
    js_property_set(msg, make_string_item("url"), make_string_item(req->url));
    js_property_set(msg, make_string_item("httpVersion"), make_string_item(req->http_version));

    // headers as lowercase-key object
    Item headers = js_new_object();
    Item raw_headers = js_array_new(0);
    for (int i = 0; i < req->header_count; i++) {
        js_array_push(raw_headers, make_string_item(req->raw_header_names[i]));
        js_array_push(raw_headers, make_string_item(req->raw_header_values[i]));
        http_request_headers_append(headers, req->header_names[i], req->header_values[i]);
    }
    js_property_set(msg, make_string_item("headers"), headers);
    js_property_set(msg, make_string_item("rawHeaders"), raw_headers);
    js_property_set(msg, make_string_item("trailers"), js_new_object());
    js_property_set(msg, make_string_item("rawTrailers"), js_array_new(0));

    // body
    if (req->body && req->body_len > 0) {
        Item body = make_string_item(req->body, req->body_len);
        js_property_set(msg, make_string_item("body"), body);
        js_readable_push(msg, body);
    }
    js_readable_push(msg, ItemNull);

    return msg;
}

// =============================================================================
// HTTP Server
// =============================================================================

typedef struct JsHttpServer {
    uv_tcp_t  tcp;
    Item      js_object;
    Item      request_handler;     // callback(req, res)
} JsHttpServer;

static void http_server_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)mem_alloc(suggested_size, MEM_CAT_JS_RUNTIME);
    buf->len = buf->base ? suggested_size : 0;
}

static void http_server_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsHttpConn* conn = (JsHttpConn*)stream->data;
    if (!conn) {
        if (buf->base) mem_free(buf->base);
        return;
    }

    if (nread > 0) {
        // accumulate data
        if (conn->recv_len + (int)nread > conn->recv_cap) {
            int new_cap = conn->recv_cap * 2;
            if (new_cap < conn->recv_len + (int)nread + 1) new_cap = conn->recv_len + (int)nread + 1;
            char* new_buf = (char*)mem_alloc(new_cap, MEM_CAT_JS_RUNTIME);
            if (conn->recv_buf) {
                memcpy(new_buf, conn->recv_buf, conn->recv_len);
                mem_free(conn->recv_buf);
            }
            conn->recv_buf = new_buf;
            conn->recv_cap = new_cap;
        }
        memcpy(conn->recv_buf + conn->recv_len, buf->base, (size_t)nread);
        conn->recv_len += (int)nread;

        // try to parse
        ParsedRequest req;
        int consumed = 0;
        if (parse_http_request(conn->recv_buf, conn->recv_len, &req, &consumed) == 0) {
            // successfully parsed — create req/res objects and call handler
            JsHttpServer* srv = conn->server;
            if (srv && get_type_id(srv->request_handler) == LMD_TYPE_FUNC) {
                if (conn->async_resource.item == 0) conn->async_resource = js_new_object();
                Item req_obj = make_request_object(&req);
                Item res_obj = make_response_object(conn);

                Item args[2] = { req_obj, res_obj };
                Item previous_resource = js_async_hooks_enter_resource(conn->async_resource);
                js_call_function(srv->request_handler, srv->js_object, args, 2);
                js_async_hooks_restore_resource(previous_resource);
                js_microtask_flush();

                // emit 'request' event
                Item on_req = js_property_get(srv->js_object, make_string_item("__on_request__"));
                if (get_type_id(on_req) == LMD_TYPE_FUNC) {
                    previous_resource = js_async_hooks_enter_resource(conn->async_resource);
                    js_call_function(on_req, srv->js_object, args, 2);
                    js_async_hooks_restore_resource(previous_resource);
                    js_microtask_flush();
                }
            }
        }
    }

    if (buf->base) mem_free(buf->base);

    if (nread < 0 && conn && !conn->destroyed) {
        conn->destroyed = true;
        uv_close((uv_handle_t*)stream, [](uv_handle_t* h) {
            JsHttpConn* c = (JsHttpConn*)h->data;
            if (c) {
                if (c->recv_buf) mem_free(c->recv_buf);
                mem_free(c);
            }
        });
    }
}

static void http_server_connection_cb(uv_stream_t* server, int status) {
    if (status < 0) return;
    JsHttpServer* srv = (JsHttpServer*)server->data;
    if (!srv) return;

    uv_loop_t* loop = server->loop;

    JsHttpConn* conn = (JsHttpConn*)mem_calloc(1, sizeof(JsHttpConn), MEM_CAT_JS_RUNTIME);
    uv_tcp_init(loop, &conn->tcp);
    conn->tcp.data = conn;
    conn->server = srv;
    conn->recv_cap = 8192;
    conn->recv_buf = (char*)mem_alloc(conn->recv_cap, MEM_CAT_JS_RUNTIME);

    if (uv_accept(server, (uv_stream_t*)&conn->tcp) == 0) {
        uv_read_start((uv_stream_t*)&conn->tcp, http_server_alloc_cb, http_server_read_cb);
    } else {
        mem_free(conn->recv_buf);
        uv_close((uv_handle_t*)&conn->tcp, [](uv_handle_t* h) {
            mem_free(h->data);
        });
    }
}

// server.listen(port, [host], [callback])
static Item js_http_server_listening_tick(Item self, Item on_listening, Item callback) {
    if (get_type_id(on_listening) == LMD_TYPE_FUNC) {
        js_call_function(on_listening, self, NULL, 0);
    }
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, self, NULL, 0);
    }
    return make_js_undefined();
}

extern "C" Item js_http_server_listen(Item self, Item port_item, Item host_item, Item callback) {
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0) return self;
    JsHttpServer* srv = (JsHttpServer*)(uintptr_t)it2i(handle_item);
    if (!srv) return self;

    int port = 80;
    if (get_type_id(port_item) == LMD_TYPE_FUNC) {
        callback = port_item;
        host_item = make_js_undefined();
        port = 0;
    } else if (port_item.item == 0 ||
               get_type_id(port_item) == LMD_TYPE_UNDEFINED ||
               get_type_id(port_item) == LMD_TYPE_NULL) {
        port = 0;
    } else if (get_type_id(port_item) == LMD_TYPE_INT) {
        port = (int)it2i(port_item);
    }

    char host_buf[256] = "0.0.0.0";
    if (get_type_id(host_item) == LMD_TYPE_FUNC) {
        callback = host_item;
    } else if (get_type_id(host_item) == LMD_TYPE_STRING) {
        String* h = it2s(host_item);
        int len = (int)h->len < 255 ? (int)h->len : 255;
        memcpy(host_buf, h->chars, (size_t)len);
        host_buf[len] = '\0';
    }

    struct sockaddr_in addr;
    uv_ip4_addr(host_buf, port, &addr);
    uv_tcp_bind(&srv->tcp, (const struct sockaddr*)&addr, 0);

    int r = uv_listen((uv_stream_t*)&srv->tcp, 128, http_server_connection_cb);
    if (r != 0) {
        log_error("http: server listen failed: %s", uv_strerror(r));
        Item err = js_new_error(make_string_item(uv_strerror(r)));
        Item on_err = js_property_get(self, make_string_item("__on_error__"));
        if (get_type_id(on_err) == LMD_TYPE_FUNC) {
            js_call_function(on_err, self, &err, 1);
        }
        return self;
    }

    // store listening address info
    js_property_set(self, make_string_item("listening"), (Item){.item = b2it(true)});

    Item on_listening = js_property_get(self, make_string_item("__on_listening__"));
    if (get_type_id(on_listening) == LMD_TYPE_FUNC || get_type_id(callback) == LMD_TYPE_FUNC) {
        Item bound_args[3] = { self, on_listening, callback };
        Item tick = js_bind_function(js_new_function((void*)js_http_server_listening_tick, 3),
                                     make_js_undefined(), bound_args, 3);
        js_next_tick_enqueue(tick);
    }

    return self;
}

// server.close([callback])
extern "C" Item js_http_server_close(Item self, Item callback) {
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0) return self;
    JsHttpServer* srv = (JsHttpServer*)(uintptr_t)it2i(handle_item);
    if (!srv) return self;

    js_property_set(self, make_string_item("listening"), (Item){.item = b2it(false)});

    if (!uv_is_closing((uv_handle_t*)&srv->tcp)) {
        uv_close((uv_handle_t*)&srv->tcp, [](uv_handle_t* h) {
            JsHttpServer* s = (JsHttpServer*)h->data;
            if (s) {
                Item on_close = js_property_get(s->js_object, make_string_item("__on_close__"));
                if (get_type_id(on_close) == LMD_TYPE_FUNC) {
                    js_call_function(on_close, s->js_object, NULL, 0);
                }
                mem_free(s);
            }
        });
    }

    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, self, NULL, 0);
    }

    return self;
}

// server.on(event, callback)
extern "C" Item js_http_server_on(Item self, Item event_item, Item callback) {
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    String* ev = it2s(event_item);
    char key[64];
    snprintf(key, sizeof(key), "__on_%.*s__", (int)ev->len, ev->chars);
    js_property_set(self, make_string_item(key), callback);
    return self;
}

// server.address() — returns {port, address, family}
extern "C" Item js_http_server_address(Item self) {
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0) return js_new_object();
    JsHttpServer* srv = (JsHttpServer*)(uintptr_t)it2i(handle_item);
    if (!srv) return js_new_object();

    struct sockaddr_storage saddr;
    int namelen = sizeof(saddr);
    if (uv_tcp_getsockname(&srv->tcp, (struct sockaddr*)&saddr, &namelen) != 0)
        return js_new_object();

    Item result = js_new_object();
    if (saddr.ss_family == AF_INET) {
        struct sockaddr_in* in = (struct sockaddr_in*)&saddr;
        char ip[64];
        uv_ip4_name(in, ip, sizeof(ip));
        js_property_set(result, make_string_item("address"), make_string_item(ip));
        js_property_set(result, make_string_item("port"), (Item){.item = i2it(ntohs(in->sin_port))});
        js_property_set(result, make_string_item("family"), make_string_item("IPv4"));
    }
    return result;
}

static Item js_http_server_inst_listen(Item maybe_self, Item port_item, Item host_item, Item callback) {
    Item self = js_http_receiver(maybe_self, "__server__");
    if (self.item == maybe_self.item) {
        return js_http_server_listen(self, port_item, host_item, callback);
    }
    return js_http_server_listen(self, maybe_self, port_item, host_item);
}

static Item js_http_server_inst_close(Item maybe_self, Item callback) {
    Item self = js_http_receiver(maybe_self, "__server__");
    return js_http_server_close(self, self.item == maybe_self.item ? callback : maybe_self);
}

static Item js_http_server_inst_on(Item maybe_self, Item event_item, Item callback) {
    Item self = js_http_receiver(maybe_self, "__server__");
    if (self.item == maybe_self.item) {
        return js_http_server_on(self, event_item, callback);
    }
    return js_http_server_on(self, maybe_self, event_item);
}

static Item js_http_server_inst_address(Item maybe_self) {
    return js_http_server_address(js_http_receiver(maybe_self, "__server__"));
}

static Item js_http_server_inst_ref(Item maybe_self) {
    Item self = js_http_receiver(maybe_self, "__server__");
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (get_type_id(handle_item) != LMD_TYPE_INT) return self;
    JsHttpServer* srv = (JsHttpServer*)(uintptr_t)it2i(handle_item);
    if (srv && !uv_is_closing((uv_handle_t*)&srv->tcp)) {
        uv_ref((uv_handle_t*)&srv->tcp);
    }
    return self;
}

static Item js_http_server_inst_unref(Item maybe_self) {
    Item self = js_http_receiver(maybe_self, "__server__");
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (get_type_id(handle_item) != LMD_TYPE_INT) return self;
    JsHttpServer* srv = (JsHttpServer*)(uintptr_t)it2i(handle_item);
    if (srv && !uv_is_closing((uv_handle_t*)&srv->tcp)) {
        uv_unref((uv_handle_t*)&srv->tcp);
    }
    return self;
}

// http.createServer([requestListener])
extern "C" Item js_http_createServer(Item handler) {
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        log_error("http: createServer: no event loop");
        return ItemNull;
    }

    JsHttpServer* srv = (JsHttpServer*)mem_calloc(1, sizeof(JsHttpServer), MEM_CAT_JS_RUNTIME);
    uv_tcp_init(loop, &srv->tcp);
    srv->tcp.data = srv;
    srv->request_handler = handler;

    Item obj = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(obj, JS_CLASS_SERVER);  // A3-T3b
    js_property_set(obj, make_string_item("__server__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)srv)});
    js_property_set(obj, make_string_item("listen"),
                    js_new_function((void*)js_http_server_inst_listen, 4));
    js_property_set(obj, make_string_item("close"),
                    js_new_function((void*)js_http_server_inst_close, 2));
    js_property_set(obj, make_string_item("on"),
                    js_new_function((void*)js_http_server_inst_on, 3));
    js_property_set(obj, make_string_item("address"),
                    js_new_function((void*)js_http_server_inst_address, 1));
    js_property_set(obj, make_string_item("ref"),
                    js_new_function((void*)js_http_server_inst_ref, 1));
    js_property_set(obj, make_string_item("unref"),
                    js_new_function((void*)js_http_server_inst_unref, 1));
    js_property_set(obj, make_string_item("listening"), (Item){.item = b2it(false)});

    srv->js_object = obj;
    return obj;
}

// =============================================================================
// http.request(options, callback) — HTTP client
// =============================================================================

typedef struct JsHttpClientReq {
    uv_tcp_t   tcp;
    Item       js_object;        // the ClientRequest object
    Item       callback;          // response callback
    Item       als_context;
    Item       response;
    char*      send_buf;
    int        send_len;
    int        send_head_len;
    char*      recv_buf;
    int        recv_len;
    int        recv_cap;
    int        response_hdr_size;
    int        response_delivered;
    bool       response_emitted;
    bool       destroyed;
    bool       sent;
    bool       connected;
    bool       has_content_length;
} JsHttpClientReq;

static void http_client_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)mem_alloc(suggested_size, MEM_CAT_JS_RUNTIME);
    buf->len = buf->base ? suggested_size : 0;
}

// parse HTTP response status line + headers from raw bytes
static int parse_http_response_head(const char* data, int data_len,
                                     int* status_code, Item* status_message,
                                     Item* headers_obj, Item* raw_headers,
                                     int* hdr_size) {
    // find end of headers
    const char* hdr_end = NULL;
    for (int i = 0; i + 3 < data_len; i++) {
        if (data[i] == '\r' && data[i+1] == '\n' && data[i+2] == '\r' && data[i+3] == '\n') {
            hdr_end = data + i + 4;
            break;
        }
    }
    if (!hdr_end) return -1; // incomplete

    *hdr_size = (int)(hdr_end - data);

    // parse status line: "HTTP/1.1 200 OK\r\n"
    const char* p = data;
    const char* line_end = (const char*)memchr(p, '\r', hdr_end - p);
    if (!line_end) return -1;

    // skip "HTTP/x.x "
    const char* sp1 = (const char*)memchr(p, ' ', line_end - p);
    if (!sp1) return -1;
    *status_code = atoi(sp1 + 1);
    const char* msg_start = (const char*)memchr(sp1 + 1, ' ', line_end - (sp1 + 1));
    if (msg_start) {
        msg_start++;
        *status_message = make_string_item(msg_start, (int)(line_end - msg_start));
    } else {
        *status_message = make_string_item("", 0);
    }

    // parse headers
    *headers_obj = js_new_object();
    *raw_headers = js_array_new(0);
    p = line_end + 2;
    while (p < hdr_end - 2) {
        line_end = (const char*)memchr(p, '\r', hdr_end - p);
        if (!line_end || line_end == p) break;

        const char* colon = (const char*)memchr(p, ':', line_end - p);
        if (!colon) { p = line_end + 2; continue; }

        int nlen = (int)(colon - p);
        const char* vstart = colon + 1;
        while (vstart < line_end && *vstart == ' ') vstart++;
        int vlen = (int)(line_end - vstart);

        js_array_push(*raw_headers, make_string_item(p, nlen));
        js_array_push(*raw_headers, make_string_item(vstart, vlen));

        // lowercase header name
        char lc_name[256];
        if (nlen >= 256) nlen = 255;
        for (int i = 0; i < nlen; i++) {
            char c = p[i];
            lc_name[i] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
        }
        lc_name[nlen] = '\0';

        js_property_set(*headers_obj,
            make_string_item(lc_name, nlen),
            make_string_item(vstart, vlen));

        p = line_end + 2;
    }

    return 0;
}

// response.on(event, cb) for client responses
extern "C" Item js_http_client_res_on(Item self2, Item ev2, Item cb2) {
    if (get_type_id(ev2) != LMD_TYPE_STRING) return self2;
    String* evs = it2s(ev2);
    // for 'data' event, immediately call with body if available
    if (evs->len == 4 && memcmp(evs->chars, "data", 4) == 0) {
        Item body = js_property_get(self2, make_string_item("body"));
        if (get_type_id(body) == LMD_TYPE_STRING) {
            js_call_function(cb2, self2, &body, 1);
        }
    } else if (evs->len == 3 && memcmp(evs->chars, "end", 3) == 0) {
        // immediately call end
        js_call_function(cb2, self2, NULL, 0);
    }
    return self2;
}

static Item js_http_econnreset_error(Item err) {
    Item result = err;
    if (get_type_id(result) == LMD_TYPE_UNDEFINED || get_type_id(result) == LMD_TYPE_NULL || result.item == 0) {
        result = js_new_error(make_string_item("socket hang up"));
    }
    js_property_set(result, make_string_item("code"), make_string_item("ECONNRESET"));
    return result;
}

static void js_http_free_client_req(JsHttpClientReq* creq) {
    if (!creq) return;
    if (creq->send_buf) mem_free(creq->send_buf);
    if (creq->recv_buf) mem_free(creq->recv_buf);
    mem_free(creq);
}

static void js_http_close_client_req(JsHttpClientReq* creq) {
    if (!creq) return;
    if (!uv_is_closing((uv_handle_t*)&creq->tcp)) {
        uv_close((uv_handle_t*)&creq->tcp, [](uv_handle_t* h) {
            js_http_free_client_req((JsHttpClientReq*)h->data);
        });
    }
}

extern "C" Item js_http_agent_socket_error_tick(Item req_obj, Item err) {
    Item handle_item = js_property_get(req_obj, make_string_item("__client__"));
    JsHttpClientReq* creq = handle_item.item ? (JsHttpClientReq*)(uintptr_t)it2i(handle_item) : NULL;
    if (!creq || creq->destroyed) return make_js_undefined();

    creq->destroyed = true;
    js_property_set(req_obj, make_string_item("destroyed"), (Item){.item = b2it(true)});

    Item on_err = js_property_get(req_obj, make_string_item("__on_error__"));
    if (get_type_id(on_err) == LMD_TYPE_FUNC) {
        js_call_function(on_err, req_obj, &err, 1);
        js_microtask_flush();
    }

    Item on_close = js_property_get(req_obj, make_string_item("__on_close__"));
    if (get_type_id(on_close) == LMD_TYPE_FUNC) {
        js_call_function(on_close, req_obj, NULL, 0);
        js_microtask_flush();
    }

    js_http_close_client_req(creq);
    return make_js_undefined();
}

extern "C" Item js_http_agent_socket_cb(Item req_obj, Item err, Item socket) {
    TypeId err_type = get_type_id(err);
    if (err.item != 0 && err_type != LMD_TYPE_UNDEFINED && err_type != LMD_TYPE_NULL) {
        Item bound_args[2] = { req_obj, err };
        Item tick = js_bind_function(js_new_function((void*)js_http_agent_socket_error_tick, 2),
                                     make_js_undefined(), bound_args, 2);
        js_setImmediate(tick);
        return make_js_undefined();
    }

    js_property_set(req_obj, make_string_item("socket"), socket);
    return make_js_undefined();
}

static void js_http_emit_client_response(JsHttpClientReq* creq, Item res) {
    if (!creq || creq->response_emitted) return;
    creq->response_emitted = true;
    Item on_response = js_property_get(creq->js_object, make_string_item("__on_response__"));
    if (get_type_id(creq->callback) == LMD_TYPE_FUNC) {
        js_als_context_call(creq->als_context, creq->callback, creq->js_object, res, 1);
        js_microtask_flush();
    }
    if (get_type_id(on_response) == LMD_TYPE_FUNC) {
        js_als_context_call(creq->als_context, on_response, creq->js_object, res, 1);
        js_microtask_flush();
    }
}

static bool js_http_client_try_emit_response(JsHttpClientReq* creq) {
    if (!creq || creq->response.item != 0) return creq && creq->response.item != 0;
    if (creq->recv_len <= 0) return false;

    int status_code = 0;
    Item status_message;
    Item headers;
    Item raw_headers;
    int hdr_size = 0;
    if (parse_http_response_head(creq->recv_buf, creq->recv_len,
                                  &status_code, &status_message,
                                  &headers, &raw_headers, &hdr_size) != 0) {
        return false;
    }

    Item res = js_readable_new(ItemNull);
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(res, JS_CLASS_INCOMING_MESSAGE);  // A3-T3b
    js_property_set(res, make_string_item("statusCode"), (Item){.item = i2it(status_code)});
    js_property_set(res, make_string_item("statusMessage"), status_message);
    js_property_set(res, make_string_item("headers"), headers);
    js_property_set(res, make_string_item("rawHeaders"), raw_headers);

    int body_len = creq->recv_len - hdr_size;
    if (body_len > 0) {
        Item body = make_string_item(creq->recv_buf + hdr_size, body_len);
        Item body_chunks = js_array_new(0);
        js_array_push(body_chunks, body);
        js_property_set(res, make_string_item("__chunks__"), body_chunks);
        js_property_set(res, make_string_item("body"), body);
        js_readable_push(res, body);
    }

    creq->response = res;
    creq->response_hdr_size = hdr_size;
    creq->response_delivered = creq->recv_len;
    js_http_emit_client_response(creq, res);
    return true;
}

static void http_client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsHttpClientReq* creq = (JsHttpClientReq*)stream->data;
    if (!creq) {
        if (buf->base) mem_free(buf->base);
        return;
    }

    if (nread > 0) {
        // accumulate response data
        if (creq->recv_len + (int)nread > creq->recv_cap) {
            int new_cap = creq->recv_cap * 2;
            if (new_cap < creq->recv_len + (int)nread + 1) new_cap = creq->recv_len + (int)nread + 1;
            char* new_buf = (char*)mem_alloc(new_cap, MEM_CAT_JS_RUNTIME);
            if (creq->recv_buf) {
                memcpy(new_buf, creq->recv_buf, creq->recv_len);
                mem_free(creq->recv_buf);
            }
            creq->recv_buf = new_buf;
            creq->recv_cap = new_cap;
        }
        memcpy(creq->recv_buf + creq->recv_len, buf->base, (size_t)nread);
        creq->recv_len += (int)nread;
        if (creq->response.item != 0) {
            int body_start = creq->response_hdr_size;
            int prev = creq->response_delivered;
            if (prev < body_start) prev = body_start;
            if (creq->recv_len > prev) {
                js_readable_push(creq->response, make_string_item(creq->recv_buf + prev, creq->recv_len - prev));
                creq->response_delivered = creq->recv_len;
            }
        } else {
            js_http_client_try_emit_response(creq);
        }
    }

    if (buf->base) mem_free(buf->base);

    if (nread < 0) {
        // connection ended — parse response and emit
        if (creq->response.item != 0) {
            js_readable_push(creq->response, ItemNull);
        } else if (creq->recv_len > 0) {
            int status_code = 0;
            Item status_message;
            Item headers;
            Item raw_headers;
            int hdr_size = 0;

            if (parse_http_response_head(creq->recv_buf, creq->recv_len,
                                          &status_code, &status_message,
                                          &headers, &raw_headers, &hdr_size) == 0) {
                // create IncomingMessage-style response
                Item res = js_readable_new(ItemNull);
                // T5b: legacy `__class_name__` string write retired.
                js_class_stamp(res, JS_CLASS_INCOMING_MESSAGE);  // A3-T3b
                js_property_set(res, make_string_item("statusCode"), (Item){.item = i2it(status_code)});
                js_property_set(res, make_string_item("statusMessage"), status_message);
                js_property_set(res, make_string_item("headers"), headers);
                js_property_set(res, make_string_item("rawHeaders"), raw_headers);

                int body_len = creq->recv_len - hdr_size;
                if (body_len > 0) {
                    Item body_chunks = js_array_new(0);
                    Item chunk = make_string_item(creq->recv_buf + hdr_size, body_len);
                    js_array_push(body_chunks, chunk);
                    js_property_set(res, make_string_item("__chunks__"), body_chunks);

                    // on('data', cb) support — simplified: store data for sync reading
                    js_property_set(res, make_string_item("body"),
                                    make_string_item(creq->recv_buf + hdr_size, body_len));
                    js_readable_push(res, chunk);
                }
                js_readable_push(res, ItemNull);

                creq->response = res;
                js_http_emit_client_response(creq, res);
            }
        }

        // cleanup
        creq->destroyed = true;
        if (creq->recv_buf) mem_free(creq->recv_buf);
        creq->recv_buf = NULL;
        uv_close((uv_handle_t*)stream, [](uv_handle_t* h) {
            JsHttpClientReq* c = (JsHttpClientReq*)h->data;
            if (c) mem_free(c);
        });
    }
}

static void http_client_write_cb(uv_write_t* req, int status) {
    if (req->data) mem_free(req->data);
    mem_free(req);
}

static void http_client_write_chunk(JsHttpClientReq* creq, String* chunk) {
    if (!creq || !chunk || chunk->len == 0 || creq->destroyed || !creq->connected) return;
    char* copy = (char*)mem_alloc(chunk->len, MEM_CAT_JS_RUNTIME);
    memcpy(copy, chunk->chars, chunk->len);
    uv_buf_t buf = uv_buf_init(copy, (unsigned int)chunk->len);
    uv_write_t* wreq = (uv_write_t*)mem_calloc(1, sizeof(uv_write_t), MEM_CAT_JS_RUNTIME);
    wreq->data = copy;
    uv_write(wreq, (uv_stream_t*)&creq->tcp, &buf, 1, http_client_write_cb);
}

static void http_client_update_pending_body(JsHttpClientReq* creq, Item req_obj) {
    if (!creq || creq->sent || !creq->send_buf || creq->send_head_len <= 0 ||
        !creq->has_content_length) {
        return;
    }

    Item body = js_property_get(req_obj, make_string_item("__req_body__"));
    if (get_type_id(body) != LMD_TYPE_STRING) return;
    String* bs = it2s(body);
    int body_len = (int)bs->len;
    int new_len = creq->send_head_len + body_len;
    char* new_buf = (char*)mem_alloc(new_len, MEM_CAT_JS_RUNTIME);
    memcpy(new_buf, creq->send_buf, (size_t)creq->send_head_len);
    if (body_len > 0) {
        memcpy(new_buf + creq->send_head_len, bs->chars, (size_t)body_len);
    }
    mem_free(creq->send_buf);
    creq->send_buf = new_buf;
    creq->send_len = new_len;
}

static void http_client_connect_cb(uv_connect_t* req, int status) {
    JsHttpClientReq* creq = (JsHttpClientReq*)req->data;
    mem_free(req);

    if (status != 0) {
        if (creq) {
            Item err = js_new_error(make_string_item(uv_strerror(status)));
            Item on_err = js_property_get(creq->js_object, make_string_item("__on_error__"));
            if (get_type_id(on_err) == LMD_TYPE_FUNC) {
                js_call_function(on_err, creq->js_object, &err, 1);
            }
        }
        return;
    }

    creq->connected = true;

    // send the HTTP request
    if (creq->send_buf && creq->send_len > 0) {
        uv_buf_t buf = uv_buf_init(creq->send_buf, (unsigned int)creq->send_len);
        uv_write_t* wreq = (uv_write_t*)mem_calloc(1, sizeof(uv_write_t), MEM_CAT_JS_RUNTIME);
        wreq->data = creq->send_buf;
        creq->send_buf = NULL; // ownership transferred
        creq->sent = true;
        uv_write(wreq, (uv_stream_t*)&creq->tcp, &buf, 1, http_client_write_cb);
    }

    // start reading response
    uv_read_start((uv_stream_t*)&creq->tcp, http_client_alloc_cb, http_client_read_cb);
}

// ClientRequest.write(data) — for request body
extern "C" Item js_http_client_write(Item self, Item data_item) {
    // store body data (will be sent on end())
    Item body = js_property_get(self, make_string_item("__req_body__"));
    if (get_type_id(body) != LMD_TYPE_STRING) body = make_string_item("", 0);

    if (get_type_id(data_item) == LMD_TYPE_STRING) {
        String* existing = it2s(body);
        String* chunk = it2s(data_item);
        int new_len = (int)existing->len + (int)chunk->len;
        char* buf = (char*)mem_alloc(new_len + 1, MEM_CAT_JS_RUNTIME);
        memcpy(buf, existing->chars, existing->len);
        memcpy(buf + existing->len, chunk->chars, chunk->len);
        buf[new_len] = '\0';
        js_property_set(self, make_string_item("__req_body__"), make_string_item(buf, new_len));
        mem_free(buf);

        Item handle_item = js_property_get(self, make_string_item("__client__"));
        if (handle_item.item != 0) {
            JsHttpClientReq* creq = (JsHttpClientReq*)(uintptr_t)it2i(handle_item);
            if (creq && creq->has_content_length) {
                if (creq->sent) {
                    http_client_write_chunk(creq, chunk);
                } else {
                    http_client_update_pending_body(creq, self);
                }
            }
        }
    }
    return self;
}

// ClientRequest.end([data]) — finalize request
extern "C" Item js_http_client_end(Item self, Item data_item) {
    if (get_type_id(data_item) == LMD_TYPE_STRING) {
        js_http_client_write(self, data_item);
    }
    Item handle_item = js_property_get(self, make_string_item("__client__"));
    if (handle_item.item != 0) {
        JsHttpClientReq* creq = (JsHttpClientReq*)(uintptr_t)it2i(handle_item);
        if (creq && !creq->sent && creq->send_buf) {
            Item body = js_property_get(self, make_string_item("__req_body__"));
            if (get_type_id(body) == LMD_TYPE_STRING) {
                String* bs = it2s(body);
                if (bs->len > 0 && creq->has_content_length) {
                    http_client_update_pending_body(creq, self);
                } else if (bs->len > 0 && creq->send_len >= 2) {
                    char len_header[64];
                    int len_header_len = snprintf(len_header, sizeof(len_header),
                                                  "Content-Length: %d\r\n", (int)bs->len);
                    int new_len = creq->send_len - 2 + len_header_len + 2 + (int)bs->len;
                    char* new_buf = (char*)mem_alloc(new_len, MEM_CAT_JS_RUNTIME);
                    memcpy(new_buf, creq->send_buf, (size_t)(creq->send_len - 2));
                    memcpy(new_buf + creq->send_len - 2, len_header, (size_t)len_header_len);
                    memcpy(new_buf + creq->send_len - 2 + len_header_len, "\r\n", 2);
                    memcpy(new_buf + creq->send_len - 2 + len_header_len + 2, bs->chars, bs->len);
                    mem_free(creq->send_buf);
                    creq->send_buf = new_buf;
                    creq->send_len = new_len;
                    creq->send_head_len = creq->send_len - (int)bs->len;
                    creq->has_content_length = true;
                }
            }
        }
    }
    // the actual send happens in connect_cb; for simplicity we pre-build
    // the full request at creation time. end() is a no-op for GET requests.
    return self;
}

// ClientRequest.on(event, callback)
extern "C" Item js_http_client_on(Item self, Item event_item, Item callback) {
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    String* ev = it2s(event_item);
    char key[64];
    snprintf(key, sizeof(key), "__on_%.*s__", (int)ev->len, ev->chars);
    js_property_set(self, make_string_item(key), callback);
    return self;
}

extern "C" Item js_http_client_destroy(Item self, Item err_item) {
    Item handle_item = js_property_get(self, make_string_item("__client__"));
    if (handle_item.item == 0) return self;
    JsHttpClientReq* creq = (JsHttpClientReq*)(uintptr_t)it2i(handle_item);
    if (!creq || creq->destroyed) return self;

    creq->destroyed = true;
    Item err = js_http_econnreset_error(err_item);
    Item on_err = js_property_get(self, make_string_item("__on_error__"));
    if (get_type_id(on_err) == LMD_TYPE_FUNC) {
        js_call_function(on_err, self, &err, 1);
    }
    if (creq->response.item != 0) {
        js_stream_destroy(creq->response, err);
    }
    if (!uv_is_closing((uv_handle_t*)&creq->tcp)) {
        uv_close((uv_handle_t*)&creq->tcp, [](uv_handle_t* h) {
            JsHttpClientReq* c = (JsHttpClientReq*)h->data;
            if (c) {
                if (c->send_buf) mem_free(c->send_buf);
                if (c->recv_buf) mem_free(c->recv_buf);
                mem_free(c);
            }
        });
    }
    return self;
}

static Item js_http_client_inst_write(Item maybe_self, Item data_item) {
    Item self = js_http_receiver(maybe_self, "__client__");
    return js_http_client_write(self, self.item == maybe_self.item ? data_item : maybe_self);
}

static Item js_http_client_inst_end(Item maybe_self, Item data_item) {
    Item self = js_http_receiver(maybe_self, "__client__");
    return js_http_client_end(self, self.item == maybe_self.item ? data_item : maybe_self);
}

static Item js_http_client_inst_on(Item maybe_self, Item event_item, Item callback) {
    Item self = js_http_receiver(maybe_self, "__client__");
    if (self.item == maybe_self.item) {
        return js_http_client_on(self, event_item, callback);
    }
    return js_http_client_on(self, maybe_self, event_item);
}

static Item js_http_client_inst_destroy(Item maybe_self, Item err_item) {
    Item self = js_http_receiver(maybe_self, "__client__");
    return js_http_client_destroy(self, self.item == maybe_self.item ? err_item : maybe_self);
}

static int http_client_append_header_line(char* req_str, int rlen, int cap,
                                          Item name_item, Item value_item,
                                          bool* has_content_length) {
    if (get_type_id(name_item) != LMD_TYPE_STRING) return rlen;
    String* name = it2s(name_item);
    if (http_header_name_equals(name, "content-length", 14)) *has_content_length = true;

    if (get_type_id(value_item) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(value_item);
        if (http_header_name_equals(name, "cookie", 6)) {
            char value_buf[4096];
            int pos = 0;
            for (int64_t i = 0; i < len; i++) {
                Item part = http_item_to_header_string(js_array_get_int(value_item, i));
                if (get_type_id(part) != LMD_TYPE_STRING) continue;
                String* ps = it2s(part);
                if (i > 0) pos += snprintf(value_buf + pos, sizeof(value_buf) - pos, "; ");
                pos += snprintf(value_buf + pos, sizeof(value_buf) - pos,
                                "%.*s", (int)ps->len, ps->chars);
            }
            return rlen + snprintf(req_str + rlen, cap - rlen,
                                   "%.*s: %.*s\r\n", (int)name->len, name->chars, pos, value_buf);
        }
        for (int64_t i = 0; i < len; i++) {
            rlen = http_client_append_header_line(req_str, rlen, cap,
                                                  name_item, js_array_get_int(value_item, i),
                                                  has_content_length);
        }
        return rlen;
    }

    Item value = http_item_to_header_string(value_item);
    if (get_type_id(value) != LMD_TYPE_STRING) return rlen;
    String* vs = it2s(value);
    return rlen + snprintf(req_str + rlen, cap - rlen,
                           "%.*s: %.*s\r\n", (int)name->len, name->chars, (int)vs->len, vs->chars);
}

static int http_client_append_headers(char* req_str, int rlen, int cap,
                                      Item headers_item, bool* has_content_length) {
    if (get_type_id(headers_item) == LMD_TYPE_MAP) {
        Item keys = js_object_keys(headers_item);
        int64_t nkeys = js_array_length(keys);
        for (int64_t i = 0; i < nkeys; i++) {
            Item k = js_array_get_int(keys, i);
            rlen = http_client_append_header_line(req_str, rlen, cap, k,
                                                  js_property_get(headers_item, k),
                                                  has_content_length);
        }
    } else if (get_type_id(headers_item) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(headers_item);
        for (int64_t i = 0; i < len; i++) {
            Item entry = js_array_get_int(headers_item, i);
            if (get_type_id(entry) == LMD_TYPE_ARRAY && js_array_length(entry) >= 2) {
                rlen = http_client_append_header_line(req_str, rlen, cap,
                                                      js_array_get_int(entry, 0),
                                                      js_array_get_int(entry, 1),
                                                      has_content_length);
            } else if (i + 1 < len) {
                rlen = http_client_append_header_line(req_str, rlen, cap,
                                                      entry,
                                                      js_array_get_int(headers_item, i + 1),
                                                      has_content_length);
                i++;
            }
        }
    }
    return rlen;
}

// http.request(options, callback)
extern "C" Item js_http_request(Item options_item, Item callback) {
    int port = 80;
    char host_buf[256] = "127.0.0.1";
    char method_buf[16] = "GET";
    char path_buf[4096] = "/";

    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        Item p = js_property_get(options_item, make_string_item("port"));
        if (get_type_id(p) == LMD_TYPE_INT) port = (int)it2i(p);
        Item h = js_property_get(options_item, make_string_item("hostname"));
        if (get_type_id(h) != LMD_TYPE_STRING)
            h = js_property_get(options_item, make_string_item("host"));
        if (get_type_id(h) == LMD_TYPE_STRING) {
            String* hs = it2s(h);
            int len = (int)hs->len < 255 ? (int)hs->len : 255;
            memcpy(host_buf, hs->chars, (size_t)len);
            host_buf[len] = '\0';
        }
        Item m = js_property_get(options_item, make_string_item("method"));
        if (get_type_id(m) == LMD_TYPE_STRING) {
            String* ms = it2s(m);
            int len = (int)ms->len < 15 ? (int)ms->len : 15;
            memcpy(method_buf, ms->chars, (size_t)len);
            method_buf[len] = '\0';
        }
        Item pa = js_property_get(options_item, make_string_item("path"));
        if (get_type_id(pa) == LMD_TYPE_STRING) {
            String* ps = it2s(pa);
            int len = (int)ps->len < 4095 ? (int)ps->len : 4095;
            memcpy(path_buf, ps->chars, (size_t)len);
            path_buf[len] = '\0';
        }
    } else if (get_type_id(options_item) == LMD_TYPE_STRING) {
        // simple string URL — parse host:port/path
        String* url = it2s(options_item);
        // basic URL parsing for http://host:port/path
        const char* s = url->chars;
        int slen = (int)url->len;
        if (slen > 7 && memcmp(s, "http://", 7) == 0) { s += 7; slen -= 7; }
        const char* slash = (const char*)memchr(s, '/', slen);
        int host_len = slash ? (int)(slash - s) : slen;
        const char* colon = (const char*)memchr(s, ':', host_len);
        if (colon) {
            int hn_len = (int)(colon - s);
            if (hn_len > 255) hn_len = 255;
            memcpy(host_buf, s, hn_len);
            host_buf[hn_len] = '\0';
            port = atoi(colon + 1);
        } else {
            if (host_len > 255) host_len = 255;
            memcpy(host_buf, s, host_len);
            host_buf[host_len] = '\0';
        }
        if (slash) {
            int plen = slen - (int)(slash - (url->chars + (url->len - slen)));
            // recalculate
            plen = (int)(url->chars + url->len - slash);
            if (plen > 4095) plen = 4095;
            memcpy(path_buf, slash, plen);
            path_buf[plen] = '\0';
        }
    }

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) return ItemNull;

    // build HTTP request string
    char req_str[8192];
    bool set_default_headers = true;
    bool set_host = true;
    Item custom_headers = make_js_undefined();
    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        bool explicit_set_host = false;
        Item sdh = js_property_get(options_item, make_string_item("setDefaultHeaders"));
        if (get_type_id(sdh) == LMD_TYPE_BOOL) set_default_headers = it2b(sdh);
        Item sh = js_property_get(options_item, make_string_item("setHost"));
        if (get_type_id(sh) == LMD_TYPE_BOOL) {
            set_host = it2b(sh);
            explicit_set_host = true;
        }
        if (!set_default_headers && !explicit_set_host) set_host = false;
        custom_headers = js_property_get(options_item, make_string_item("headers"));
    }

    int rlen = snprintf(req_str, sizeof(req_str),
        "%s %s HTTP/1.1\r\n", method_buf, path_buf);

    bool has_content_length = false;
    bool has_headers_array = get_type_id(custom_headers) == LMD_TYPE_ARRAY;

    if (set_default_headers && set_host && !has_headers_array) {
        if (port == 80) {
            rlen += snprintf(req_str + rlen, sizeof(req_str) - rlen,
                             "Host: %s\r\n", host_buf);
        } else {
            rlen += snprintf(req_str + rlen, sizeof(req_str) - rlen,
                             "Host: %s:%d\r\n", host_buf, port);
        }
    } else if (!set_default_headers && set_host) {
        if (port == 80) {
            rlen += snprintf(req_str + rlen, sizeof(req_str) - rlen,
                             "Host: %s\r\n", host_buf);
        } else {
            rlen += snprintf(req_str + rlen, sizeof(req_str) - rlen,
                             "Host: %s:%d\r\n", host_buf, port);
        }
    }

    if (set_default_headers) {
        rlen += snprintf(req_str + rlen, sizeof(req_str) - rlen, "Connection: close\r\n");
    }

    rlen = http_client_append_headers(req_str, rlen, (int)sizeof(req_str),
                                      custom_headers, &has_content_length);

    if (set_default_headers &&
        (strcmp(method_buf, "POST") == 0 || strcmp(method_buf, "PUT") == 0) &&
        !has_content_length) {
        rlen += snprintf(req_str + rlen, sizeof(req_str) - rlen, "Content-Length: 0\r\n");
        has_content_length = true;
    }

    rlen += snprintf(req_str + rlen, sizeof(req_str) - rlen, "\r\n");

    // create client request
    JsHttpClientReq* creq = (JsHttpClientReq*)mem_calloc(1, sizeof(JsHttpClientReq), MEM_CAT_JS_RUNTIME);
    uv_tcp_init(loop, &creq->tcp);
    creq->tcp.data = creq;
    creq->callback = callback;
    creq->als_context = js_als_capture_context();
    creq->recv_cap = 16384;
    creq->recv_buf = (char*)mem_alloc(creq->recv_cap, MEM_CAT_JS_RUNTIME);

    // copy request buffer
    creq->send_len = rlen;
    creq->send_head_len = rlen;
    creq->has_content_length = has_content_length;
    creq->send_buf = (char*)mem_alloc(rlen, MEM_CAT_JS_RUNTIME);
    memcpy(creq->send_buf, req_str, rlen);

    // create JS ClientRequest object
    Item obj = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(obj, JS_CLASS_CLIENT_REQUEST);  // A3-T3b
    js_property_set(obj, make_string_item("__client__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)creq)});
    js_property_set(obj, make_string_item("write"),
                    js_new_function((void*)js_http_client_inst_write, 2));
    js_property_set(obj, make_string_item("end"),
                    js_new_function((void*)js_http_client_inst_end, 2));
    js_property_set(obj, make_string_item("on"),
                    js_new_function((void*)js_http_client_inst_on, 3));
    js_property_set(obj, make_string_item("destroy"),
                    js_new_function((void*)js_http_client_inst_destroy, 2));
    js_property_set(obj, make_string_item("destroyed"), (Item){.item = b2it(false)});

    creq->js_object = obj;

    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        Item agent = js_property_get(options_item, make_string_item("agent"));
        if (get_type_id(agent) == LMD_TYPE_MAP) {
            Item create_socket = js_property_get(agent, make_string_item("createSocket"));
            if (get_type_id(create_socket) == LMD_TYPE_FUNC) {
                Item cb_arg = obj;
                Item cb = js_bind_function(js_new_function((void*)js_http_agent_socket_cb, 3),
                                           make_js_undefined(), &cb_arg, 1);
                Item args[3] = { obj, options_item, cb };
                js_call_function(create_socket, agent, args, 3);
                return obj;
            }
        }
    }

    // connect
    struct sockaddr_in addr;
    const char* connect_host = strcmp(host_buf, "localhost") == 0 ? "127.0.0.1" : host_buf;
    uv_ip4_addr(connect_host, port, &addr);

    uv_connect_t* conn = (uv_connect_t*)mem_calloc(1, sizeof(uv_connect_t), MEM_CAT_JS_RUNTIME);
    conn->data = creq;

    int r = uv_tcp_connect(conn, &creq->tcp, (const struct sockaddr*)&addr, http_client_connect_cb);
    if (r != 0) {
        log_error("http: request connect failed: %s", uv_strerror(r));
        mem_free(conn);
        mem_free(creq->send_buf);
        mem_free(creq->recv_buf);
        mem_free(creq);
        return ItemNull;
    }

    return obj;
}

// http.get(options, callback) — shorthand for GET
extern "C" Item js_http_get(Item options_item, Item callback) {
    // ensure method is GET
    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        js_property_set(options_item, make_string_item("method"), make_string_item("GET"));
    }
    Item req = js_http_request(options_item, callback);
    // auto-call end() since GET has no body
    if (req.item != 0 && get_type_id(req) != LMD_TYPE_UNDEFINED) {
        js_http_client_end(req, make_js_undefined());
    }
    return req;
}

// =============================================================================
// http.STATUS_CODES — map of status code → reason phrase
// =============================================================================

static Item build_status_codes(void) {
    Item codes = js_new_object();
    static const int code_list[] = {
        100,101,200,201,202,204,206,301,302,303,304,307,308,
        400,401,403,404,405,406,408,409,410,411,413,414,415,416,422,429,
        500,501,502,503,504
    };
    for (int i = 0; i < (int)(sizeof(code_list)/sizeof(code_list[0])); i++) {
        char key[8];
        snprintf(key, sizeof(key), "%d", code_list[i]);
        js_property_set(codes, make_string_item(key),
                        make_string_item(http_status_text(code_list[i])));
    }
    return codes;
}

// =============================================================================
// http.Agent class
// =============================================================================

// Agent.prototype.getName(options)
extern "C" Item js_http_agent_getName(Item options) {
    // Node.js Agent.getName returns "host:port:localAddress:family"
    char host[256] = "localhost";
    char port[32] = "";
    char local_addr[256] = "";
    char family_str[16] = "";

    if (get_type_id(options) == LMD_TYPE_MAP) {
        Item h = js_property_get(options, make_string_item("host"));
        if (get_type_id(h) == LMD_TYPE_STRING) {
            String* s = it2s(h);
            int len = (int)s->len < 255 ? (int)s->len : 255;
            memcpy(host, s->chars, len);
            host[len] = '\0';
        }
        Item p = js_property_get(options, make_string_item("port"));
        if (get_type_id(p) == LMD_TYPE_INT) {
            snprintf(port, sizeof(port), "%d", (int)it2i(p));
        } else if (get_type_id(p) == LMD_TYPE_STRING) {
            String* s = it2s(p);
            int len = (int)s->len < 31 ? (int)s->len : 31;
            memcpy(port, s->chars, len);
            port[len] = '\0';
        }
        Item la = js_property_get(options, make_string_item("localAddress"));
        if (get_type_id(la) == LMD_TYPE_STRING) {
            String* s = it2s(la);
            int len = (int)s->len < 255 ? (int)s->len : 255;
            memcpy(local_addr, s->chars, len);
            local_addr[len] = '\0';
        }
        Item fam = js_property_get(options, make_string_item("family"));
        if (get_type_id(fam) == LMD_TYPE_INT) {
            int fv = (int)it2i(fam);
            if (fv != 0) {
                snprintf(family_str, sizeof(family_str), "%d", fv);
            }
        }
    }
    char result[600];
    snprintf(result, sizeof(result), "%s:%s:%s:%s", host, port, local_addr, family_str);
    return make_string_item(result);
}

// Agent.prototype.destroy()
extern "C" Item js_http_agent_destroy(void) {
    // Stub — clear sockets/freeSockets/requests
    Item self = js_get_this();
    js_property_set(self, make_string_item("sockets"), js_new_object());
    js_property_set(self, make_string_item("freeSockets"), js_new_object());
    js_property_set(self, make_string_item("requests"), js_new_object());
    return ItemNull;
}

// Agent.prototype.createConnection(options, cb)
extern "C" Item js_http_agent_createConnection(Item options) {
    // Delegate to net.createConnection
    extern Item js_net_createConnection(Item, Item);
    return js_net_createConnection(options, ItemNull);
}

// new http.Agent(options) constructor
extern "C" Item js_http_Agent(Item options) {
    Item agent = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(agent, JS_CLASS_AGENT);  // A3-T3b

    // defaults
    int max_sockets = 256;
    bool keep_alive = false;
    int keep_alive_msecs = 1000;

    if (get_type_id(options) == LMD_TYPE_MAP) {
        Item ms = js_property_get(options, make_string_item("maxSockets"));
        if (get_type_id(ms) == LMD_TYPE_INT) max_sockets = (int)it2i(ms);
        Item ka = js_property_get(options, make_string_item("keepAlive"));
        if (get_type_id(ka) == LMD_TYPE_BOOL) keep_alive = it2b(ka);
        Item kam = js_property_get(options, make_string_item("keepAliveMsecs"));
        if (get_type_id(kam) == LMD_TYPE_INT) keep_alive_msecs = (int)it2i(kam);
    }

    js_property_set(agent, make_string_item("maxSockets"), (Item){.item = i2it(max_sockets)});
    js_property_set(agent, make_string_item("keepAlive"), (Item){.item = b2it(keep_alive)});
    js_property_set(agent, make_string_item("keepAliveMsecs"), (Item){.item = i2it(keep_alive_msecs)});
    js_property_set(agent, make_string_item("requests"), js_new_object());
    js_property_set(agent, make_string_item("sockets"), js_new_object());
    js_property_set(agent, make_string_item("freeSockets"), js_new_object());
    js_property_set(agent, make_string_item("maxFreeSockets"), (Item){.item = i2it(256)});
    js_property_set(agent, make_string_item("options"), options);

    // methods directly on agent (will also inherit from prototype)
    js_property_set(agent, make_string_item("getName"),
        js_new_function((void*)js_http_agent_getName, 1));
    js_property_set(agent, make_string_item("destroy"),
        js_new_function((void*)js_http_agent_destroy, 0));
    js_property_set(agent, make_string_item("createConnection"),
        js_new_function((void*)js_http_agent_createConnection, 1));

    return agent;
}

// Stub constructor for IncomingMessage, ServerResponse, etc.
extern "C" Item js_http_stub_ctor(void) {
    Item obj = js_new_object();
    return obj;
}

// =============================================================================
// http Module Namespace
// =============================================================================

static Item http_namespace = {0};

static void http_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

extern "C" Item js_get_http_namespace(void) {
    if (http_namespace.item != 0) return http_namespace;

    http_namespace = js_new_object();

    http_set_method(http_namespace, "createServer", (void*)js_http_createServer, 1);
    http_set_method(http_namespace, "request",      (void*)js_http_request, 2);
    http_set_method(http_namespace, "get",           (void*)js_http_get, 2);

    // Server — alias for createServer (Node.js allows http.Server(cb))
    http_set_method(http_namespace, "Server",       (void*)js_http_createServer, 1);

    // STATUS_CODES
    js_property_set(http_namespace, make_string_item("STATUS_CODES"), build_status_codes());

    // METHODS
    Item methods = js_array_new(0);
    static const char* method_list[] = {
        "GET","HEAD","POST","PUT","DELETE","CONNECT","OPTIONS","TRACE","PATCH"
    };
    for (int i = 0; i < 9; i++) {
        js_array_push(methods, make_string_item(method_list[i]));
    }
    js_property_set(http_namespace, make_string_item("METHODS"), methods);

    // Agent constructor
    http_set_method(http_namespace, "Agent", (void*)js_http_Agent, 1);

    // globalAgent — stub Agent instance
    Item agent = js_new_object();
    js_property_set(agent, make_string_item("maxSockets"), (Item){.item = i2it(256)});
    js_property_set(agent, make_string_item("keepAlive"), (Item){.item = b2it(false)});
    js_property_set(agent, make_string_item("requests"), js_new_object());
    js_property_set(agent, make_string_item("sockets"), js_new_object());
    js_property_set(agent, make_string_item("freeSockets"), js_new_object());
    js_property_set(agent, make_string_item("getName"),
        js_new_function((void*)js_http_agent_getName, 1));
    js_property_set(agent, make_string_item("destroy"),
        js_new_function((void*)js_http_agent_destroy, 0));
    js_property_set(http_namespace, make_string_item("globalAgent"), agent);

    // Stub constructors for IncomingMessage, ServerResponse, ClientRequest, OutgoingMessage
    http_set_method(http_namespace, "IncomingMessage", (void*)js_http_stub_ctor, 0);
    http_set_method(http_namespace, "ServerResponse",  (void*)js_http_stub_ctor, 0);
    http_set_method(http_namespace, "ClientRequest",   (void*)js_http_stub_ctor, 0);
    http_set_method(http_namespace, "OutgoingMessage",  (void*)js_http_stub_ctor, 0);

    Item default_key = make_string_item("default");
    js_property_set(http_namespace, default_key, http_namespace);

    return http_namespace;
}

extern "C" void js_http_reset(void) {
    http_namespace = (Item){0};
}
