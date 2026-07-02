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
#include "js_typed_array.h"
#include "js_permission.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"
#include "../../lib/mem.h"
#include "../../lib/base64.h"
#include "../../lib/url.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cmath>

extern "C" Item js_readable_new(Item opts);
extern "C" Item js_readable_push(Item self, Item chunk);
extern "C" Item js_stream_on(Item self, Item event_item, Item listener);
extern "C" Item js_stream_destroy(Item self, Item err);
extern "C" Item js_net_createConnection(Item rest_args);
extern "C" Item js_net_get_socket_prototype(void);
extern "C" void js_function_set_prototype(Item fn_item, Item proto);
extern "C" Item js_buffer_from_bytes(const char* data, int len);
extern "C" Item js_buffer_from(Item data, Item encoding, Item length_item);
extern "C" Item js_async_hooks_enter_resource(Item resource);
extern "C" void js_async_hooks_restore_resource(Item previous);
extern "C" Item js_async_hooks_create_resource(const char* type_chars, int type_len);
extern "C" void js_async_hooks_emit_destroy_resource(Item resource);
extern "C" Item js_als_capture_context(void);
extern "C" Item js_als_context_call(Item context, Item callback, Item this_val, Item arg1, int64_t has_arg);
extern "C" void js_throw_value(Item error);
extern "C" Item js_process_emit(Item event_name, Item arg1);

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

static Item http_server_prototype = {0};
// release LTO binds these constructor prototypes from helper-created objects;
// keeping concrete definitions avoids unresolved optimized references.
Item http_incoming_message_prototype = {0};
Item http_server_response_prototype = {0};
static Item http_outgoing_message_prototype = {0};

static inline bool js_http_is_callable(Item item) {
    return get_type_id(item) == LMD_TYPE_FUNC;
}

static bool js_http_is_object_like(Item item) {
    TypeId type = get_type_id(item);
    return type == LMD_TYPE_MAP || type == LMD_TYPE_OBJECT || type == LMD_TYPE_VMAP;
}

static bool js_http_object_has_key(Item obj, const char* key) {
    if (!js_http_is_object_like(obj) || !key) return false;
    Item keys = js_object_keys(obj);
    if (get_type_id(keys) != LMD_TYPE_ARRAY) return false;
    int64_t len = js_array_length(keys);
    size_t key_len = strlen(key);
    for (int64_t idx = 0; idx < len; idx++) {
        Item item = js_array_get_int(keys, idx);
        if (get_type_id(item) != LMD_TYPE_STRING) continue;
        String* s = it2s(item);
        if (s && (size_t)s->len == key_len && memcmp(s->chars, key, key_len) == 0) return true;
    }
    return false;
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
    if (marker && strcmp(marker, "__client__") == 0) {
        if (js_http_has_marker(candidate, "__client_request__")) return candidate;
        if (js_http_has_marker(self, "__client_request__")) return self;
    }
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
        case 417: return "Expectation Failed";
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
        Item default_status_message = js_property_get(response, make_string_item("__default_status_message__"));
        if (get_type_id(default_status_message) == LMD_TYPE_STRING) {
            String* dm = it2s(default_status_message);
            if (sm->len == dm->len && memcmp(sm->chars, dm->chars, sm->len) == 0) {
                // statusCode assignment leaves the initial statusMessage untouched; serialize the phrase for the final code.
                return http_status_text(status);
            }
        }
        if (sm->len > 0) return sm->chars;
    }
    return http_status_text(status);
}

static void http_status_code_display(Item status_item, char* buf, int buf_size) {
    if (!buf || buf_size <= 0) return;
    TypeId type = get_type_id(status_item);
    if (type == LMD_TYPE_UNDEFINED || status_item.item == 0) {
        snprintf(buf, buf_size, "undefined");
    } else if (type == LMD_TYPE_NULL) {
        snprintf(buf, buf_size, "null");
    } else if (type == LMD_TYPE_BOOL) {
        snprintf(buf, buf_size, "%s", it2b(status_item) ? "true" : "false");
    } else if (type == LMD_TYPE_INT) {
        snprintf(buf, buf_size, "%lld", (long long)it2i(status_item));
    } else if (type == LMD_TYPE_INT64) {
        snprintf(buf, buf_size, "%lld", (long long)it2l(status_item));
    } else if (type == LMD_TYPE_FLOAT) {
        double d = status_item.get_double();
        if (isnan(d)) {
            snprintf(buf, buf_size, "NaN");
        } else if (isinf(d)) {
            snprintf(buf, buf_size, d > 0 ? "Infinity" : "-Infinity");
        } else {
            snprintf(buf, buf_size, "%g", d);
        }
    } else if (type == LMD_TYPE_STRING) {
        String* s = it2s(status_item);
        snprintf(buf, buf_size, "%.*s", (int)s->len, s->chars);
    } else if (type == LMD_TYPE_ARRAY) {
        snprintf(buf, buf_size, "[]");
    } else {
        snprintf(buf, buf_size, "{}");
    }
}

static Item http_throw_invalid_status_code(Item status_item) {
    char display[128];
    http_status_code_display(status_item, display, (int)sizeof(display));
    char message[192];
    snprintf(message, sizeof(message), "Invalid status code: %s", display);
    Item err = js_new_error_with_name(make_string_item("RangeError"), make_string_item(message));
    js_property_set(err, make_string_item("code"), make_string_item("ERR_HTTP_INVALID_STATUS_CODE"));
    js_throw_value(err);
    return ItemNull;
}

static bool http_status_code_is_valid(Item status_item, int* out_status) {
    if (!out_status) return false;
    TypeId type = get_type_id(status_item);
    int status = 0;
    if (type == LMD_TYPE_INT) {
        status = (int)it2i(status_item);
    } else if (type == LMD_TYPE_INT64) {
        int64_t value = it2l(status_item);
        if (value < 0 || value > 1000) return false;
        status = (int)value;
    } else if (type == LMD_TYPE_FLOAT) {
        double value = status_item.get_double();
        if (!isfinite(value)) return false;
        status = (int)value;
        if ((double)status != value) return false;
    } else {
        return false;
    }
    if (status < 100 || status > 999) return false;
    *out_status = status;
    return true;
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
    char raw_trailer_names[32][128];
    char raw_trailer_values[32][4096];
    char trailer_names[32][128];
    char trailer_values[32][4096];
    int  trailer_count;
    const char* body;
    int  body_len;
    int  content_length;
    bool body_complete;
    int  error_status;
} ParsedRequest;

static const char* http_request_header(ParsedRequest* req, const char* name) {
    if (!req || !name) return NULL;
    for (int i = 0; i < req->header_count; i++) {
        if (strcmp(req->header_names[i], name) == 0) return req->header_values[i];
    }
    return NULL;
}

static bool http_token_equals_ci(const char* value, int len, const char* token) {
    if (!value || !token) return false;
    int token_len = (int)strlen(token);
    if (len != token_len) return false;
    for (int i = 0; i < len; i++) {
        char a = value[i];
        char b = token[i];
        if (a >= 'A' && a <= 'Z') a = a + 32;
        if (b >= 'A' && b <= 'Z') b = b + 32;
        if (a != b) return false;
    }
    return true;
}

static bool http_header_has_token(const char* value, const char* token) {
    if (!value || !token) return false;
    const char* p = value;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        const char* start = p;
        while (*p && *p != ',') p++;
        const char* end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
        if (http_token_equals_ci(start, (int)(end - start), token)) return true;
        if (*p == ',') p++;
    }
    return false;
}

static int http_chunk_hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void http_request_store_trailer(ParsedRequest* req, const char* line_start,
                                       const char* line_end) {
    if (!req || req->trailer_count >= 32 || !line_start || !line_end || line_end <= line_start) return;
    const char* colon = (const char*)memchr(line_start, ':', (size_t)(line_end - line_start));
    if (!colon) return;

    int nlen = (int)(colon - line_start);
    const char* vstart = colon + 1;
    while (vstart < line_end && (*vstart == ' ' || *vstart == '\t')) vstart++;
    int vlen = (int)(line_end - vstart);
    if (nlen <= 0) return;
    if (nlen >= 128) nlen = 127;
    if (vlen >= 4096) vlen = 4095;

    int index = req->trailer_count;
    memcpy(req->raw_trailer_names[index], line_start, (size_t)nlen);
    req->raw_trailer_names[index][nlen] = '\0';
    memcpy(req->raw_trailer_values[index], vstart, (size_t)vlen);
    req->raw_trailer_values[index][vlen] = '\0';
    memcpy(req->trailer_names[index], line_start, (size_t)nlen);
    req->trailer_names[index][nlen] = '\0';
    for (int i = 0; i < nlen; i++) {
        char c = req->trailer_names[index][i];
        if (c >= 'A' && c <= 'Z') req->trailer_names[index][i] = c + 32;
    }
    memcpy(req->trailer_values[index], vstart, (size_t)vlen);
    req->trailer_values[index][vlen] = '\0';
    req->trailer_count++;
}

static int http_decode_chunked_request_body(char* data, int len, ParsedRequest* req,
                                            int* consumed, bool* complete, bool emit) {
    int pos = 0;
    int out_len = 0;
    if (consumed) *consumed = 0;
    if (complete) *complete = false;
    if (!data || len <= 0) return 0;

    while (pos < len) {
        int line_start = pos;
        int line_end = -1;
        for (int i = pos; i + 1 < len; i++) {
            if (data[i] == '\r' && data[i + 1] == '\n') {
                line_end = i;
                break;
            }
        }
        if (line_end < 0) return out_len;

        int size = 0;
        bool saw_digit = false;
        bool in_extension = false;
        int extension_len = 0;
        for (int i = line_start; i < line_end; i++) {
            char c = data[i];
            if (c == ';') {
                in_extension = true;
                continue;
            }
            if (in_extension) {
                if (c == '\r' || c == '\n') return -1;
                extension_len++;
                if (extension_len > 16384) return -2;
                continue;
            }
            if (c == ' ' || c == '\t') continue;
            int v = http_chunk_hex_value(c);
            if (v < 0) return -1;
            saw_digit = true;
            if (size > 0x0fffffff) return -1;
            size = size * 16 + v;
        }
        if (!saw_digit) return -1;
        pos = line_end + 2;

        if (size == 0) {
            while (pos < len) {
                int trailer_line_end = -1;
                for (int i = pos; i + 1 < len; i++) {
                    if (data[i] == '\r' && data[i + 1] == '\n') {
                        trailer_line_end = i;
                        break;
                    }
                }
                if (trailer_line_end < 0) return out_len;
                if (trailer_line_end == pos) {
                    pos += 2;
                    if (consumed) *consumed = pos;
                    if (complete) *complete = true;
                    return out_len;
                }
                if (emit) http_request_store_trailer(req, data + pos, data + trailer_line_end);
                pos = trailer_line_end + 2;
            }
            return out_len;
        }

        if (pos + size + 2 > len) return out_len;
        if (emit) memmove(data + out_len, data + pos, (size_t)size);
        out_len += size;
        pos += size;
        if (data[pos] != '\r' || data[pos + 1] != '\n') return -1;
        pos += 2;
    }
    return out_len;
}

// parse an HTTP request from raw bytes. Returns 0 on success, -1 on incomplete/error.
// Sets *consumed to the number of bytes consumed (headers + body).
static int parse_http_request(char* data, int data_len, ParsedRequest* req, int* consumed) {
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

    // check for Transfer-Encoding/Content-Length to read body
    int content_length = 0;
    bool chunked_body = false;
    bool has_content_length = false;
    for (int i = 0; i < req->header_count; i++) {
        if (strcmp(req->header_names[i], "transfer-encoding") == 0 &&
            http_header_has_token(req->header_values[i], "chunked")) {
            chunked_body = true;
        }
        if (strcmp(req->header_names[i], "content-length") == 0) {
            has_content_length = true;
            content_length = atoi(req->header_values[i]);
        }
    }

    req->content_length = content_length;
    if (chunked_body && !has_content_length) {
        int available = data_len - hdr_size;
        int chunked_consumed = 0;
        bool chunked_complete = false;
        int decoded_len = http_decode_chunked_request_body(data + hdr_size, available, req,
                                                           &chunked_consumed, &chunked_complete, false);
        if (decoded_len < 0) {
            req->body = NULL;
            req->body_len = 0;
            req->body_complete = true;
            req->error_status = decoded_len == -2 ? 413 : 400;
            *consumed = hdr_size;
            return 0;
        }
        if (chunked_complete) {
            req->trailer_count = 0;
            int emit_consumed = 0;
            bool emit_complete = false;
            int emit_len = http_decode_chunked_request_body(data + hdr_size, available, req,
                                                            &emit_consumed, &emit_complete, true);
            if (emit_len < 0 || !emit_complete) return -1;
            decoded_len = emit_len;
            chunked_consumed = emit_consumed;
        }
        req->body = decoded_len > 0 ? data + hdr_size : NULL;
        req->body_len = decoded_len;
        req->body_complete = chunked_complete;
        *consumed = hdr_size + (chunked_complete ? chunked_consumed : available);
    } else if (content_length > 0) {
        int available = data_len - hdr_size;
        int body_len = available < content_length ? available : content_length;
        req->body = data + hdr_size;
        req->body_len = body_len;
        req->body_complete = body_len >= content_length;
        *consumed = hdr_size + body_len;
    } else {
        req->body = NULL;
        req->body_len = 0;
        req->body_complete = true;
        *consumed = hdr_size;
    }

    return 0;
}

// =============================================================================
// HTTP Connection — handles per-client state
// =============================================================================

typedef struct JsHttpServer {
    uv_tcp_t  tcp;
    uv_pipe_t pipe;
    Item      js_object;
    Item      request_handler;     // callback(req, res)
    Item      incoming_message_ctor;
    Item      server_response_ctor;
    Item      timeout_callback;
    bool      reject_nonstandard_body_writes;
    bool      is_pipe;
    bool      handle_initialized;
    bool      close_requested;
    bool      handle_closed;
    bool      close_event_emitted;
    int       connection_count;
    int       timeout_msecs;
    struct JsHttpConn* connections_head;
} JsHttpServer;

typedef struct JsHttpConn {
    uv_tcp_t     tcp;
    uv_pipe_t    pipe;
    JsHttpServer* server;
    Item         async_resource;
    Item         socket_object;
    Item         current_request;
    Item         current_response;
    Item         timeout_response;
    Item         request_timeout_callback;
    Item         response_timeout_callback;
    char*        recv_buf;
    int          recv_len;
    int          recv_cap;
    int          request_count;
    int          request_body_remaining;
    int          pending_response_writes;
    int          open_response_count;
    JsHttpConn*  server_prev;
    JsHttpConn*  server_next;
    Item         timeout_timer;
    bool         destroyed;
    bool         read_ended;
    bool         is_pipe;
    bool         request_body_chunked;
    bool         close_after_response_writes;
    bool         timeout_timer_active;
} JsHttpConn;

static uv_stream_t* http_conn_stream(JsHttpConn* conn) {
    if (!conn) return NULL;
    return conn->is_pipe ? (uv_stream_t*)&conn->pipe : (uv_stream_t*)&conn->tcp;
}

static void http_server_note_conn_closed(JsHttpConn* conn);
static void http_conn_close_now(JsHttpConn* conn);
static void http_conn_clear_timeout(JsHttpConn* conn);
static void http_conn_start_timeout(JsHttpConn* conn, int64_t delay);
static bool http_conn_write_bytes(JsHttpConn* conn, Item data_item, bool close_after);
static void http_conn_maybe_close_after_response_writes(JsHttpConn* conn);
static void http_conn_maybe_close_for_server_close(JsHttpConn* conn);
static void http_response_close_request(Item res);
static void http_response_flush_partial(Item self);

typedef struct HttpResponseWriteReq {
    char* data;
    Item  response;
    Item  callback;
    bool  close_after;
    bool  final;
} HttpResponseWriteReq;

// =============================================================================
// ServerResponse — writable response object
// =============================================================================

static void http_response_emit(Item self, const char* event, Item* args, int argc, bool flush_tasks) {
    char key[64];
    snprintf(key, sizeof(key), "__on_%s__", event);
    Item cb = js_property_get(self, make_string_item(key));
    if (js_http_is_callable(cb)) {
        js_call_function(cb, self, args, argc);
        if (flush_tasks) js_microtask_flush();
    }
}

static Item http_error_with_code(const char* code, const char* message) {
    Item err = js_new_error(make_string_item(message));
    js_property_set(err, make_string_item("code"), make_string_item(code));
    return err;
}

static Item http_error_from_uv(int status) {
    Item err = js_new_error(make_string_item(uv_strerror(status)));
    const char* code = uv_err_name(status);
    if (code) {
        // Node tests branch on err.code before cleanup; missing uv codes can
        // skip close handlers and leave server handles alive until drain.
        js_property_set(err, make_string_item("code"), make_string_item(code));
    }
    return err;
}

static Item http_response_error_tick(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item res = env[0];
    Item err = env[1];
    http_response_emit(res, "error", &err, 1, true);
    return make_js_undefined();
}

static void http_response_schedule_error(Item res, Item err) {
    Item* env = js_alloc_env(2);
    env[0] = res;
    env[1] = err;
    Item tick = js_new_closure((void*)http_response_error_tick, 0, env, 2);
    js_next_tick_enqueue(tick);
}

static bool http_item_bytes(Item item, const char** data, int* len) {
    if (!data || !len) return false;
    *data = NULL;
    *len = 0;
    if (get_type_id(item) == LMD_TYPE_STRING) {
        String* s = it2s(item);
        *data = s->chars;
        *len = (int)s->len;
        return true;
    }
    if (js_is_typed_array(item)) {
        if (js_typed_array_is_out_of_bounds_item(item)) return false;
        int byte_len = js_typed_array_byte_length(item);
        void* ptr = js_typed_array_current_data_ptr(item);
        if (byte_len > 0 && !ptr) return false;
        *data = (const char*)ptr;
        *len = byte_len;
        return true;
    }
    return false;
}

static Item http_encode_write_chunk(Item chunk_item, Item encoding_item) {
    if (get_type_id(chunk_item) == LMD_TYPE_STRING &&
        get_type_id(encoding_item) == LMD_TYPE_STRING) {
        return js_buffer_from(chunk_item, encoding_item, make_js_undefined());
    }
    return chunk_item;
}

static void http_call_write_callback(Item callback) {
    if (!js_http_is_callable(callback)) return;
    js_call_function(callback, make_js_undefined(), NULL, 0);
    js_microtask_flush();
}

static void http_response_append_body(Item self, Item chunk_item) {
    const char* chunk_data = NULL;
    int chunk_len = 0;
    if (!http_item_bytes(chunk_item, &chunk_data, &chunk_len)) return;
    if (chunk_len <= 0) return;

    Item body = js_property_get(self, make_string_item("__body__"));
    const char* existing_data = "";
    int existing_len = 0;
    if (get_type_id(body) == LMD_TYPE_STRING) {
        String* existing = it2s(body);
        existing_data = existing->chars;
        existing_len = (int)existing->len;
    }

    int new_len = existing_len + chunk_len;
    char* buf = (char*)mem_alloc(new_len, MEM_CAT_JS_RUNTIME);
    if (existing_len > 0) memcpy(buf, existing_data, (size_t)existing_len);
    memcpy(buf + existing_len, chunk_data, (size_t)chunk_len);
    js_property_set(self, make_string_item("__body__"), make_string_item(buf, new_len));
    mem_free(buf);

    Item chunks = js_property_get(self, make_string_item("__chunks__"));
    if (get_type_id(chunks) == LMD_TYPE_ARRAY) {
        js_array_push(chunks, make_string_item(chunk_data, chunk_len));
    }
}

static void http_response_append_body_encoded(Item self, Item chunk_item, Item encoding_item) {
    http_response_append_body(self, http_encode_write_chunk(chunk_item, encoding_item));
}

static bool http_response_bool_prop(Item self, const char* name) {
    Item value = js_property_get(self, make_string_item(name));
    return get_type_id(value) == LMD_TYPE_BOOL && it2b(value);
}

static int http_response_int_prop(Item self, const char* name) {
    Item value = js_property_get(self, make_string_item(name));
    if (get_type_id(value) == LMD_TYPE_INT) return (int)it2i(value);
    return 0;
}

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
    Item name = get_type_id(name_item) == LMD_TYPE_UNDEFINED ?
        make_string_item("undefined") : http_item_to_header_string(name_item);
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
    return value_item;
}

static int http_append_invalid_arg_received(char* msg, int pos, int cap, Item value) {
    TypeId type = get_type_id(value);
    if (!msg || cap <= 0 || pos >= cap - 1) return pos;
    if (type == LMD_TYPE_NULL) {
        return pos + snprintf(msg + pos, (size_t)(cap - pos), " Received null");
    }
    if (type == LMD_TYPE_UNDEFINED) {
        return pos + snprintf(msg + pos, (size_t)(cap - pos), " Received undefined");
    }
    if (type == LMD_TYPE_BOOL) {
        return pos + snprintf(msg + pos, (size_t)(cap - pos), " Received type boolean (%s)",
                              it2b(value) ? "true" : "false");
    }
    if (type == LMD_TYPE_INT) {
        return pos + snprintf(msg + pos, (size_t)(cap - pos), " Received type number (%lld)",
                              (long long)it2i(value));
    }
    if (type == LMD_TYPE_INT64) {
        return pos + snprintf(msg + pos, (size_t)(cap - pos), " Received type number (%lld)",
                              (long long)it2l(value));
    }
    if (type == LMD_TYPE_FLOAT) {
        return pos + snprintf(msg + pos, (size_t)(cap - pos), " Received type number");
    }
    if (type == LMD_TYPE_ARRAY) {
        return pos + snprintf(msg + pos, (size_t)(cap - pos), " Received an instance of Array");
    }
    if (type == LMD_TYPE_MAP || type == LMD_TYPE_OBJECT || type == LMD_TYPE_VMAP) {
        return pos + snprintf(msg + pos, (size_t)(cap - pos), " Received an instance of Object");
    }
    if (type == LMD_TYPE_SYMBOL) {
        return pos + snprintf(msg + pos, (size_t)(cap - pos), " Received type symbol");
    }
    return pos + snprintf(msg + pos, (size_t)(cap - pos), " Received type object");
}

static Item http_throw_invalid_method_type(Item method_item) {
    char msg[512];
    int pos = snprintf(msg, sizeof(msg),
                       "The \"options.method\" property must be of type string.");
    http_append_invalid_arg_received(msg, pos, (int)sizeof(msg), method_item);
    return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
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

static Item http_new_header_object(void) {
    Item headers = js_new_object();
    js_set_prototype(headers, ItemNull);
    return headers;
}

static Item http_invalid_header_name_arg(Item name_item) {
    return js_throw_invalid_arg_type("name", "string", name_item);
}

static bool http_validate_header_name_arg(Item name_item) {
    return get_type_id(name_item) == LMD_TYPE_STRING;
}

static Item http_headers_get_names(Item self, bool raw_names) {
    Item result = js_array_new(0);
    if (raw_names) {
        Item raw = js_property_get(self, make_string_item("__raw_headers__"));
        if (get_type_id(raw) != LMD_TYPE_ARRAY) return result;
        int64_t len = js_array_length(raw);
        for (int64_t i = 0; i + 1 < len; i += 2) {
            Item raw_name = js_array_get_int(raw, i);
            if (get_type_id(raw_name) == LMD_TYPE_STRING) js_array_push(result, raw_name);
        }
        return result;
    }
    Item headers = js_property_get(self, make_string_item("__headers__"));
    if (get_type_id(headers) != LMD_TYPE_MAP) return result;
    Item keys = js_object_keys(headers);
    int64_t len = js_array_length(keys);
    for (int64_t i = 0; i < len; i++) {
        Item key = js_array_get_int(keys, i);
        Item value = js_property_get(headers, key);
        TypeId type = get_type_id(value);
        if (type != LMD_TYPE_UNDEFINED && type != LMD_TYPE_NULL) js_array_push(result, key);
    }
    return result;
}

static bool http_response_header_has_token(Item self, const char* name, const char* token) {
    Item headers = js_property_get(self, make_string_item("__headers__"));
    if (get_type_id(headers) != LMD_TYPE_MAP) return false;
    Item value = js_property_get(headers, make_string_item(name));
    if (get_type_id(value) != LMD_TYPE_STRING) return false;
    String* s = it2s(value);
    if (!s) return false;
    char buf[512];
    int len = (int)s->len < (int)sizeof(buf) - 1 ? (int)s->len : (int)sizeof(buf) - 1;
    memcpy(buf, s->chars, (size_t)len);
    buf[len] = '\0';
    return http_header_has_token(buf, token);
}

static bool http_header_value_is_array(Item value) {
    return get_type_id(value) == LMD_TYPE_ARRAY;
}

static int http_append_header_value(char* resp_buf, int pos, int cap, Item name, Item value) {
    if (get_type_id(name) != LMD_TYPE_STRING) return pos;
    String* ks = it2s(name);
    if (http_header_value_is_array(value)) {
        int64_t len = js_array_length(value);
        if (http_header_name_equals(ks, "set-cookie", 10)) {
            for (int64_t i = 0; i < len; i++) {
                Item part = http_item_to_header_string(js_array_get_int(value, i));
                if (get_type_id(part) != LMD_TYPE_STRING) continue;
                String* ps = it2s(part);
                pos += snprintf(resp_buf + pos, cap - pos,
                                "%.*s: %.*s\r\n", (int)ks->len, ks->chars,
                                (int)ps->len, ps->chars);
            }
            return pos;
        }
        char value_buf[4096];
        int vpos = 0;
        for (int64_t i = 0; i < len; i++) {
            Item part = http_item_to_header_string(js_array_get_int(value, i));
            if (get_type_id(part) != LMD_TYPE_STRING) continue;
            String* ps = it2s(part);
            if (i > 0) vpos += snprintf(value_buf + vpos, sizeof(value_buf) - vpos, ", ");
            vpos += snprintf(value_buf + vpos, sizeof(value_buf) - vpos,
                             "%.*s", (int)ps->len, ps->chars);
        }
        return pos + snprintf(resp_buf + pos, cap - pos,
                              "%.*s: %.*s\r\n", (int)ks->len, ks->chars,
                              vpos, value_buf);
    }
    Item string_value = http_item_to_header_string(value);
    if (get_type_id(string_value) != LMD_TYPE_STRING) return pos;
    String* vs = it2s(string_value);
    return pos + snprintf(resp_buf + pos, cap - pos,
                          "%.*s: %.*s\r\n", (int)ks->len, ks->chars,
                          (int)vs->len, vs->chars);
}

static void http_response_set_header_pair(Item self, Item name_item, Item value_item) {
    Item name = http_validate_header_name(name_item);
    if (js_check_exception() || name.item == 0) return;
    Item value = http_validate_header_value(name, value_item);
    if (js_check_exception() || value.item == 0) return;

    Item headers = js_property_get(self, make_string_item("__headers__"));
    if (get_type_id(headers) != LMD_TYPE_MAP) {
        headers = http_new_header_object();
        js_property_set(self, make_string_item("__headers__"), headers);
    }
    js_property_set(headers, http_lowercase_header_name(name), value);
    http_raw_headers_remove_name(self, name);
    http_raw_headers_append(self, name, value);
}

static void http_response_append_raw_header_pair(Item self, Item name_item, Item value_item) {
    Item name = http_validate_header_name(name_item);
    if (js_check_exception() || name.item == 0) return;
    Item value = http_validate_header_value(name, value_item);
    if (js_check_exception() || value.item == 0) return;

    Item headers = js_property_get(self, make_string_item("__headers__"));
    if (get_type_id(headers) != LMD_TYPE_MAP) {
        headers = http_new_header_object();
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
                                        bool* has_content_length, bool* has_content_type,
                                        bool* has_connection, bool* has_keep_alive,
                                        bool* has_transfer_encoding,
                                        bool* has_date) {
    Item raw = js_property_get(self, make_string_item("__raw_headers__"));
    if (get_type_id(raw) == LMD_TYPE_ARRAY && js_array_length(raw) > 0) {
        int64_t len = js_array_length(raw);
        for (int64_t i = 0; i + 1 < len; i += 2) {
            Item k = js_array_get_int(raw, i);
            Item v = js_array_get_int(raw, i + 1);
            if (get_type_id(k) == LMD_TYPE_STRING) {
                String* ks = it2s(k);
                pos = http_append_header_value(resp_buf, pos, cap, k, v);
                if (http_header_name_equals(ks, "content-length", 14)) *has_content_length = true;
                if (http_header_name_equals(ks, "content-type", 12)) *has_content_type = true;
                if (http_header_name_equals(ks, "connection", 10)) *has_connection = true;
                if (http_header_name_equals(ks, "keep-alive", 10)) *has_keep_alive = true;
                if (has_date && http_header_name_equals(ks, "date", 4)) *has_date = true;
                if (has_transfer_encoding && http_header_name_equals(ks, "transfer-encoding", 17)) {
                    *has_transfer_encoding = true;
                }
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
            TypeId value_type = get_type_id(v);
            if (get_type_id(k) == LMD_TYPE_STRING &&
                value_type != LMD_TYPE_UNDEFINED && value_type != LMD_TYPE_NULL) {
                String* ks = it2s(k);
                pos = http_append_header_value(resp_buf, pos, cap, k, v);
                if (http_header_name_equals(ks, "content-length", 14)) *has_content_length = true;
                if (http_header_name_equals(ks, "content-type", 12)) *has_content_type = true;
                if (http_header_name_equals(ks, "connection", 10)) *has_connection = true;
                if (http_header_name_equals(ks, "keep-alive", 10)) *has_keep_alive = true;
                if (has_date && http_header_name_equals(ks, "date", 4)) *has_date = true;
                if (has_transfer_encoding && http_header_name_equals(ks, "transfer-encoding", 17)) {
                    *has_transfer_encoding = true;
                }
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
    int status = 200;
    if (!http_status_code_is_valid(status_item, &status)) {
        return http_throw_invalid_status_code(status_item);
    }
    js_property_set(self, make_string_item("statusCode"), (Item){.item = i2it(status)});

    Item headers_arg = reason_or_headers;
    Item status_message_item = make_string_item(http_status_text(status));
    if (get_type_id(reason_or_headers) == LMD_TYPE_STRING) {
        js_property_set(self, make_string_item("statusMessage"), reason_or_headers);
        status_message_item = reason_or_headers;
        headers_arg = headers_item;
    } else {
        js_property_set(self, make_string_item("statusMessage"), status_message_item);
        TypeId reason_type = get_type_id(reason_or_headers);
        if ((reason_type == LMD_TYPE_UNDEFINED || reason_type == LMD_TYPE_NULL) &&
            (get_type_id(headers_item) == LMD_TYPE_ARRAY || get_type_id(headers_item) == LMD_TYPE_MAP)) {
            headers_arg = headers_item;
        }
    }

    if (get_type_id(headers_arg) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(headers_arg);
        bool pair_array = false;
        if (len > 0 && get_type_id(js_array_get_int(headers_arg, 0)) == LMD_TYPE_ARRAY) {
            pair_array = true;
        }
        if (pair_array) {
            for (int64_t i = 0; i < len; i++) {
                Item pair = js_array_get_int(headers_arg, i);
                if (get_type_id(pair) != LMD_TYPE_ARRAY || js_array_length(pair) < 2) {
                    return js_throw_type_error_code("ERR_INVALID_ARG_VALUE",
                                                    "The argument 'headers' must be an array of name-value pairs");
                }
                http_raw_headers_remove_name(self, js_array_get_int(pair, 0));
            }
            for (int64_t i = 0; i < len; i++) {
                Item pair = js_array_get_int(headers_arg, i);
                http_response_append_raw_header_pair(self,
                    js_array_get_int(pair, 0),
                    js_array_get_int(pair, 1));
            }
        } else {
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
    js_property_set(self, make_string_item("__explicit_write_head_called__"), (Item){.item = b2it(true)});
    js_property_set(self, make_string_item("__write_head_status__"), (Item){.item = i2it(status)});
    js_property_set(self, make_string_item("__write_head_status_message__"), status_message_item);
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
    if (!http_validate_header_name_arg(name_item)) return http_invalid_header_name_arg(name_item);
    Item headers = js_property_get(self, make_string_item("__headers__"));
    if (get_type_id(headers) != LMD_TYPE_MAP) return make_js_undefined();
    return js_property_get(headers, http_lowercase_header_name(name_item));
}

// response.getHeaders()
extern "C" Item js_http_res_getHeaders(Item self) {
    Item headers = js_property_get(self, make_string_item("__headers__"));
    if (get_type_id(headers) != LMD_TYPE_MAP) {
        return http_new_header_object();
    }
    Item copy = http_new_header_object();
    Item keys = js_object_keys(headers);
    int64_t len = js_array_length(keys);
    for (int64_t i = 0; i < len; i++) {
        Item key = js_array_get_int(keys, i);
        Item value = js_property_get(headers, key);
        TypeId type = get_type_id(value);
        if (type != LMD_TYPE_UNDEFINED && type != LMD_TYPE_NULL) {
            js_property_set(copy, key, value);
        }
    }
    return copy;
}

// response.removeHeader(name)
extern "C" Item js_http_res_removeHeader(Item self, Item name_item) {
    if (!http_validate_header_name_arg(name_item)) return http_invalid_header_name_arg(name_item);
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

extern "C" Item js_http_res_getHeaderNames(Item self) {
    return http_headers_get_names(self, false);
}

extern "C" Item js_http_res_getRawHeaderNames(Item self) {
    return http_headers_get_names(self, true);
}

extern "C" Item js_http_res_hasHeader(Item self, Item name_item) {
    if (!http_validate_header_name_arg(name_item)) return http_invalid_header_name_arg(name_item);
    Item value = js_http_res_getHeader(self, name_item);
    TypeId type = get_type_id(value);
    return (Item){.item = b2it(type != LMD_TYPE_UNDEFINED && type != LMD_TYPE_NULL)};
}

static Item http_res_write_ex(Item self, Item chunk_item, Item encoding_item, Item callback_item) {
    if (js_http_is_callable(encoding_item)) {
        callback_item = encoding_item;
        encoding_item = make_js_undefined();
    }
    bool had_explicit_write_head = http_response_bool_prop(self, "__explicit_write_head_called__");
    bool reject_body = http_response_bool_prop(self, "__reject_nonstandard_body_writes__");
    int status = http_response_int_prop(self, "statusCode");
    bool head_request = false;
    Item method = js_property_get(self, make_string_item("__request_method__"));
    if (get_type_id(method) == LMD_TYPE_STRING) {
        String* ms = it2s(method);
        head_request = ms->len == 4 && memcmp(ms->chars, "HEAD", 4) == 0;
    }
    if (reject_body && (head_request || status == 204 || status == 304)) {
        return js_throw_error_with_code("ERR_HTTP_BODY_NOT_ALLOWED",
                                        "Adding content for this request method or response status is not allowed.");
    }
    http_response_append_body_encoded(self, chunk_item, encoding_item);
    js_property_set(self, make_string_item("__has_body_write__"), (Item){.item = b2it(true)});
    js_property_set(self, make_string_item("__headers_sent__"), (Item){.item = b2it(true)});
    js_property_set(self, make_string_item("__write_head_called__"), (Item){.item = b2it(true)});
    js_property_set(self, make_string_item("headersSent"), (Item){.item = b2it(true)});
    // response bodies are buffered until end(), so write callbacks must be
    // acknowledged at the write point or nested write/end sequences stall.
    http_call_write_callback(callback_item);
    if (had_explicit_write_head &&
        !http_response_bool_prop(self, "__ending__") &&
        !http_response_bool_prop(self, "__partial_sent__")) {
        http_response_flush_partial(self);
    }
    return (Item){.item = b2it(true)};
}

// response.write(chunk) — accumulate body data
extern "C" Item js_http_res_write(Item self, Item chunk_item, Item encoding_item) {
    return http_res_write_ex(self, chunk_item, encoding_item, make_js_undefined());
}

extern "C" Item js_http_res_send_internal(Item self, Item chunk_item) {
    TypeId chunk_type = get_type_id(chunk_item);
    if (chunk_item.item != 0 && chunk_item.item != ITEM_NULL &&
        chunk_type != LMD_TYPE_UNDEFINED && chunk_type != LMD_TYPE_NULL) {
        js_http_res_write(self, chunk_item, make_js_undefined());
    }
    js_property_set(self, make_string_item("__used_send__"), (Item){.item = b2it(true)});
    return self;
}

// helper: serialize headers + body to HTTP response bytes, write to socket
static void http_response_flush(Item self) {
    Item handle_item = js_property_get(self, make_string_item("__conn__"));
    if (handle_item.item == 0) return;
    JsHttpConn* conn = (JsHttpConn*)(uintptr_t)it2i(handle_item);
    if (!conn || conn->destroyed) return;

    Item sent = js_property_get(self, make_string_item("__sent__"));
    if (get_type_id(sent) == LMD_TYPE_BOOL && it2b(sent)) return; // already sent
    bool has_open_responses_after_this = false;
    if (conn->open_response_count > 0) conn->open_response_count--;
    has_open_responses_after_this = conn->open_response_count > 0;
    if (conn->timeout_response.item == self.item) {
        // response timeout ownership is valid only while that response is open;
        // completed pipelined responses must not consume later socket timeouts.
        conn->timeout_response = make_js_undefined();
        conn->response_timeout_callback = make_js_undefined();
    }
    if (conn->current_response.item == self.item) {
        conn->current_response = make_js_undefined();
    }

    // get status code
    Item status_item = js_property_get(self, make_string_item("statusCode"));
    Item committed_status = js_property_get(self, make_string_item("__write_head_status__"));
    if (get_type_id(committed_status) == LMD_TYPE_INT) status_item = committed_status;
    int status = 200;
    if (get_type_id(status_item) == LMD_TYPE_INT) status = (int)it2i(status_item);
    Item committed_status_message = js_property_get(self, make_string_item("__write_head_status_message__"));
    const char* status_message = http_response_status_message(self, status);
    char committed_message_buf[128];
    if (get_type_id(committed_status_message) == LMD_TYPE_STRING) {
        String* cms = it2s(committed_status_message);
        int msg_len = (int)cms->len < (int)sizeof(committed_message_buf) - 1 ?
            (int)cms->len : (int)sizeof(committed_message_buf) - 1;
        memcpy(committed_message_buf, cms->chars, (size_t)msg_len);
        committed_message_buf[msg_len] = '\0';
        status_message = committed_message_buf;
    }

    // build response
    char resp_buf[65536];
    int pos = 0;

    // status line
    pos += snprintf(resp_buf + pos, sizeof(resp_buf) - pos,
                    "HTTP/1.1 %d %s\r\n", status, status_message);

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
    bool has_connection = false;
    bool has_keep_alive = false;
    bool has_transfer_encoding = false;
    bool has_date = false;
    pos = http_response_append_headers(resp_buf, pos, (int)sizeof(resp_buf), self,
                                       &has_content_length, &has_content_type,
                                       &has_connection, &has_keep_alive,
                                       &has_transfer_encoding,
                                       &has_date);

    bool used_send = http_response_bool_prop(self, "__used_send__");
    bool request_http_10 = false;
    Item req_version = js_property_get(self, make_string_item("__request_http_version__"));
    if (get_type_id(req_version) == LMD_TYPE_STRING) {
        String* vs = it2s(req_version);
        request_http_10 = vs->len == 8 && memcmp(vs->chars, "HTTP/1.0", 8) == 0;
    }
    bool has_body_write = http_response_bool_prop(self, "__has_body_write__");
    bool keep_alive_max_null = http_response_bool_prop(self, "__keep_alive_max_null__");
    bool explicit_chunked_body = has_transfer_encoding &&
        http_response_header_has_token(self, "transfer-encoding", "chunked");
    bool request_accepts_chunked_response =
        http_response_bool_prop(self, "__request_accepts_chunked_response__");
    bool chunked_allowed = !request_http_10 ||
                           request_accepts_chunked_response ||
                           explicit_chunked_body;
    bool auto_chunked_body = used_send || (has_body_write &&
        (keep_alive_max_null || (request_http_10 && request_accepts_chunked_response)));
    bool chunked_body = (explicit_chunked_body || auto_chunked_body) &&
                        chunked_allowed && !has_content_length;
    if (!has_content_length && !chunked_body && !used_send && (!has_body_write || !request_http_10)) {
        pos += snprintf(resp_buf + pos, sizeof(resp_buf) - pos,
                        "Content-Length: %d\r\n", body_len);
        has_content_length = true;
    }
    if (!has_content_type) {
        pos += snprintf(resp_buf + pos, sizeof(resp_buf) - pos,
                        "Content-Type: text/plain\r\n");
    }
    bool send_date = true;
    Item send_date_item = js_property_get(self, make_string_item("sendDate"));
    if (get_type_id(send_date_item) == LMD_TYPE_BOOL && !it2b(send_date_item)) {
        send_date = false;
    }
    if (send_date && !has_date) {
        char date_buf[64];
        time_t now = time(NULL);
        struct tm tm_utc;
        memset(&tm_utc, 0, sizeof(tm_utc));
        gmtime_r(&now, &tm_utc);
        size_t date_len = strftime(date_buf, sizeof(date_buf),
                                   "%a, %d %b %Y %H:%M:%S GMT", &tm_utc);
        if (date_len > 0) {
            pos += snprintf(resp_buf + pos, sizeof(resp_buf) - pos,
                            "Date: %.*s\r\n", (int)date_len, date_buf);
        }
    }
    bool server_close_requested = conn && conn->server && conn->server->close_requested;
    bool close_after = http_response_bool_prop(self, "__close_after_response__") ||
                       (conn && conn->read_ended) ||
                       (server_close_requested && !has_open_responses_after_this);
    if (request_http_10 && body_len > 0 && !has_content_length && !chunked_body) {
        close_after = true;
    }
    if (!has_connection) {
        pos += snprintf(resp_buf + pos, sizeof(resp_buf) - pos,
                        close_after ? "Connection: close\r\n" : "Connection: keep-alive\r\n");
    }
    int keep_alive_max = http_response_int_prop(self, "__keep_alive_max__");
    if (!close_after && !has_keep_alive) {
        if (keep_alive_max > 0) {
            pos += snprintf(resp_buf + pos, sizeof(resp_buf) - pos,
                            "Keep-Alive: timeout=65, max=%d\r\n", keep_alive_max);
        } else {
            pos += snprintf(resp_buf + pos, sizeof(resp_buf) - pos,
                            "Keep-Alive: timeout=65\r\n");
        }
    }
    if (chunked_body && !has_transfer_encoding) {
        pos += snprintf(resp_buf + pos, sizeof(resp_buf) - pos,
                        "Transfer-Encoding: chunked\r\n");
    }
    pos += snprintf(resp_buf + pos, sizeof(resp_buf) - pos, "\r\n");

    char* output_body = (char*)body_data;
    int output_body_len = body_len;
    bool suppress_body = false;
    Item method_item = js_property_get(self, make_string_item("__request_method__"));
    if (get_type_id(method_item) == LMD_TYPE_STRING) {
        String* ms = it2s(method_item);
        suppress_body = ms->len == 4 && memcmp(ms->chars, "HEAD", 4) == 0;
    }
    if (status == 204 || status == 304) suppress_body = true;
    if (suppress_body) {
        output_body_len = 0;
    }
    if (chunked_body && !suppress_body) {
        Item chunks = js_property_get(self, make_string_item("__chunks__"));
        int chunk_cap = body_len + 64;
        if (get_type_id(chunks) == LMD_TYPE_ARRAY) {
            chunk_cap = 5;
            int64_t chunk_count = js_array_length(chunks);
            for (int64_t i = 0; i < chunk_count; i++) {
                Item chunk = js_array_get_int(chunks, i);
                if (get_type_id(chunk) != LMD_TYPE_STRING) continue;
                String* cs = it2s(chunk);
                chunk_cap += (int)cs->len + 32;
            }
        }
        output_body = (char*)mem_alloc(chunk_cap, MEM_CAT_JS_RUNTIME);
        int cpos = 0;
        if (get_type_id(chunks) == LMD_TYPE_ARRAY && js_array_length(chunks) > 0) {
            int64_t chunk_count = js_array_length(chunks);
            for (int64_t i = 0; i < chunk_count; i++) {
                Item chunk = js_array_get_int(chunks, i);
                if (get_type_id(chunk) != LMD_TYPE_STRING) continue;
                String* cs = it2s(chunk);
                if (cs->len <= 0) continue;
                cpos += snprintf(output_body + cpos, chunk_cap - cpos, "%X\r\n", (unsigned int)cs->len);
                memcpy(output_body + cpos, cs->chars, cs->len);
                cpos += (int)cs->len;
                memcpy(output_body + cpos, "\r\n", 2);
                cpos += 2;
            }
        } else if (body_len > 0) {
            cpos += snprintf(output_body + cpos, chunk_cap - cpos, "%X\r\n", (unsigned int)body_len);
            memcpy(output_body + cpos, body_data, (size_t)body_len);
            cpos += body_len;
            memcpy(output_body + cpos, "\r\n", 2);
            cpos += 2;
        }
        memcpy(output_body + cpos, "0\r\n\r\n", 5);
        cpos += 5;
        output_body_len = cpos;
    }

    // copy headers + body
    int total = pos + output_body_len;
    char* full = (char*)mem_alloc(total, MEM_CAT_JS_RUNTIME);
    memcpy(full, resp_buf, pos);
    if (output_body_len > 0) memcpy(full + pos, output_body, output_body_len);
    if (chunked_body && output_body) mem_free(output_body);

    uv_buf_t buf = uv_buf_init(full, (unsigned int)total);
    uv_write_t* wreq = (uv_write_t*)mem_calloc(1, sizeof(uv_write_t), MEM_CAT_JS_RUNTIME);
    HttpResponseWriteReq* write_req =
        (HttpResponseWriteReq*)mem_calloc(1, sizeof(HttpResponseWriteReq), MEM_CAT_JS_RUNTIME);
    write_req->data = full;
    write_req->response = self;
    write_req->callback = js_property_get(self, make_string_item("__end_callback__"));
    write_req->close_after = close_after;
    write_req->final = true;
    wreq->data = write_req;
    conn->pending_response_writes++;

    int write_status = uv_write(wreq, http_conn_stream(conn), &buf, 1,
        [](uv_write_t* req, int status) {
            HttpResponseWriteReq* write_req = (HttpResponseWriteReq*)req->data;
            bool close_after = false;
            JsHttpConn* c = req && req->handle ? (JsHttpConn*)((uv_stream_t*)req->handle)->data : NULL;
            if (c && c->pending_response_writes > 0) {
                c->pending_response_writes--;
            }
            if (write_req) {
                (void)status;
                close_after = write_req->close_after;
                if (write_req->data) mem_free(write_req->data);
                mem_free(write_req);
            }
            if (close_after && c && !c->destroyed) {
                c->close_after_response_writes = true;
                http_conn_maybe_close_after_response_writes(c);
            }
            if (c && !c->destroyed) {
                http_conn_maybe_close_for_server_close(c);
            }
            mem_free(req);
        });
    if (write_status != 0) {
        if (conn->pending_response_writes > 0) conn->pending_response_writes--;
        // large pipelined responses can hit a synchronous libuv write error.
        // still settle the ServerResponse bookkeeping and end callback here;
        // otherwise common/countdown-style Node tests wait for a callback that
        // will never be delivered by an async uv_write completion.
        js_property_set(self, make_string_item("writableFinished"), (Item){.item = b2it(true)});
        http_response_emit(self, "finish", NULL, 0, false);
        http_response_close_request(self);
        if (js_http_is_callable(write_req->callback)) {
            js_call_function(write_req->callback, make_js_undefined(), NULL, 0);
        }
        js_microtask_flush();
        if (write_req->data) mem_free(write_req->data);
        mem_free(write_req);
        mem_free(wreq);
        if (close_after && conn && !conn->destroyed) {
            conn->close_after_response_writes = true;
            http_conn_maybe_close_after_response_writes(conn);
        }
    } else {
        // ServerResponse completion is JS stream bookkeeping: once bytes are
        // accepted by libuv, finish/end callbacks can run independently from
        // the slower kernel/socket flush. The native uv_write callback above
        // remains responsible for freeing the queued byte buffer.
        js_property_set(self, make_string_item("__sent__"), (Item){.item = b2it(true)});
        js_property_set(self, make_string_item("__write_head_called__"), (Item){.item = b2it(true)});
        js_property_set(self, make_string_item("headersSent"), (Item){.item = b2it(true)});
        js_property_set(self, make_string_item("writableFinished"), (Item){.item = b2it(true)});
        http_response_emit(self, "finish", NULL, 0, false);
        http_response_close_request(self);
        if (js_http_is_callable(write_req->callback)) {
            js_call_function(write_req->callback, make_js_undefined(), NULL, 0);
        }
        write_req->response = make_js_undefined();
        write_req->callback = make_js_undefined();
        js_microtask_flush();
    }

    js_property_set(self, make_string_item("__sent__"), (Item){.item = b2it(true)});
    js_property_set(self, make_string_item("__write_head_called__"), (Item){.item = b2it(true)});
    js_property_set(self, make_string_item("headersSent"), (Item){.item = b2it(true)});
}

static void http_response_flush_partial(Item self) {
    Item handle_item = js_property_get(self, make_string_item("__conn__"));
    if (get_type_id(handle_item) != LMD_TYPE_INT) return;
    JsHttpConn* conn = (JsHttpConn*)(uintptr_t)it2i(handle_item);
    if (!conn || conn->destroyed) return;

    Item status_item = js_property_get(self, make_string_item("statusCode"));
    int status = get_type_id(status_item) == LMD_TYPE_INT ? (int)it2i(status_item) : 200;
    const char* status_message = http_response_status_message(self, status);
    Item body = js_property_get(self, make_string_item("__body__"));
    const char* body_data = "";
    int body_len = 0;
    if (get_type_id(body) == LMD_TYPE_STRING) {
        String* bs = it2s(body);
        body_data = bs->chars;
        body_len = (int)bs->len;
    }

    char head[1024];
    int head_len = snprintf(head, sizeof(head),
                            "HTTP/1.1 %d %s\r\nConnection: keep-alive\r\n\r\n",
                            status, status_message);
    int total = head_len + body_len;
    char* full = (char*)mem_alloc(total, MEM_CAT_JS_RUNTIME);
    memcpy(full, head, (size_t)head_len);
    if (body_len > 0) memcpy(full + head_len, body_data, (size_t)body_len);
    // explicit writeHead()+write() can leave the response open; flushing
    // headers/body now lets clients receive and destroy the IncomingMessage.
    http_conn_write_bytes(conn, make_string_item(full, total), false);
    mem_free(full);
    js_property_set(self, make_string_item("__partial_sent__"), (Item){.item = b2it(true)});
    js_property_set(self, make_string_item("headersSent"), (Item){.item = b2it(true)});
}

static Item http_res_end_ex(Item self, Item data_item, Item encoding_item, Item callback_item) {
    Item callback = callback_item;
    if (js_http_is_callable(data_item)) {
        callback = data_item;
        data_item = make_js_undefined();
        encoding_item = make_js_undefined();
    } else if (js_http_is_callable(encoding_item)) {
        callback = encoding_item;
        encoding_item = make_js_undefined();
    }
    bool already_ended = http_response_bool_prop(self, "writableEnded") ||
                         http_response_bool_prop(self, "finished");
    if (already_ended) {
        TypeId data_type = get_type_id(data_item);
        bool has_data = data_item.item != 0 && data_item.item != ITEM_NULL &&
                        data_type != LMD_TYPE_UNDEFINED && data_type != LMD_TYPE_NULL;
        Item err = has_data ?
            http_error_with_code("ERR_STREAM_WRITE_AFTER_END", "write after end") :
            http_error_with_code("ERR_STREAM_ALREADY_FINISHED", "Cannot call end after a stream was finished");
        if (js_http_is_callable(callback)) {
            js_call_function(callback, make_js_undefined(), &err, 1);
            js_microtask_flush();
        }
        if (has_data) http_response_schedule_error(self, err);
        return self;
    }
    if (js_http_is_callable(callback)) {
        js_property_set(self, make_string_item("__end_callback__"), callback);
    } else {
        js_property_set(self, make_string_item("__end_callback__"), make_js_undefined());
    }
    js_property_set(self, make_string_item("__ending__"), (Item){.item = b2it(true)});
    if (get_type_id(data_item) != LMD_TYPE_UNDEFINED && data_item.item != ITEM_NULL) {
        http_res_write_ex(self, data_item, encoding_item, make_js_undefined());
    }
    js_property_set(self, make_string_item("__ending__"), (Item){.item = b2it(false)});
    js_property_set(self, make_string_item("finished"), (Item){.item = b2it(true)});
    js_property_set(self, make_string_item("writableEnded"), (Item){.item = b2it(true)});
    http_response_flush(self);
    return self;
}

// response.end([data], [callback]) — finalize and send
extern "C" Item js_http_res_end(Item self, Item data_item, Item callback_item) {
    return http_res_end_ex(self, data_item, make_js_undefined(), callback_item);
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

static Item js_http_res_inst_getHeaderNames(Item maybe_self) {
    return js_http_res_getHeaderNames(js_http_receiver(maybe_self, "__conn__"));
}

static Item js_http_res_inst_getRawHeaderNames(Item maybe_self) {
    return js_http_res_getRawHeaderNames(js_http_receiver(maybe_self, "__conn__"));
}

static Item js_http_res_inst_hasHeader(Item maybe_self, Item name_item) {
    Item self = js_http_receiver(maybe_self, "__conn__");
    return js_http_res_hasHeader(self, self.item == maybe_self.item ? name_item : maybe_self);
}

static Item js_http_res_inst_removeHeader(Item maybe_self, Item name_item) {
    Item self = js_http_receiver(maybe_self, "__conn__");
    return js_http_res_removeHeader(self, self.item == maybe_self.item ? name_item : maybe_self);
}

static Item js_http_res_inst_write(Item maybe_self, Item chunk_item, Item encoding_or_callback) {
    Item self = js_http_receiver(maybe_self, "__conn__");
    if (self.item == maybe_self.item) {
        return http_res_write_ex(self, chunk_item, encoding_or_callback, make_js_undefined());
    }
    return http_res_write_ex(self, maybe_self, chunk_item, encoding_or_callback);
}

static Item js_http_res_inst_send_internal(Item maybe_self, Item chunk_item) {
    Item self = js_http_receiver(maybe_self, "__conn__");
    return js_http_res_send_internal(self, self.item == maybe_self.item ? chunk_item : maybe_self);
}

static Item js_http_res_inst_end(Item maybe_self, Item data_item, Item callback_item) {
    Item self = js_http_receiver(maybe_self, "__conn__");
    if (self.item == maybe_self.item) {
        return http_res_end_ex(self, data_item, make_js_undefined(), callback_item);
    }
    return http_res_end_ex(self, maybe_self, data_item, callback_item);
}

static Item js_http_res_inst_flushHeaders(Item maybe_self) {
    Item self = js_http_receiver(maybe_self, "__conn__");
    http_response_flush(self);
    return self;
}

static Item js_http_res_inst_writeContinue(Item maybe_self) {
    Item self = js_http_receiver(maybe_self, "__conn__");
    Item callback = self.item == maybe_self.item ? make_js_undefined() : maybe_self;
    Item handle_item = js_property_get(self, make_string_item("__conn__"));
    if (get_type_id(handle_item) == LMD_TYPE_INT) {
        JsHttpConn* conn = (JsHttpConn*)(uintptr_t)it2i(handle_item);
        if (http_conn_write_bytes(conn, make_string_item("HTTP/1.1 100 Continue\r\n\r\n"), false)) {
            // writeContinue has no body buffer; the callback represents libuv
            // accepting the informational response for delivery.
            http_call_write_callback(callback);
        }
    }
    return self;
}

static Item js_http_res_inst_setTimeout(Item maybe_self, Item msecs_item, Item callback_item);

static Item js_http_res_inst_on(Item maybe_self, Item event_item, Item callback) {
    Item self = js_http_receiver(maybe_self, "__conn__");
    if (self.item != maybe_self.item) {
        callback = event_item;
        event_item = maybe_self;
    }
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    String* ev = it2s(event_item);
    char key[64];
    snprintf(key, sizeof(key), "__on_%.*s__", (int)ev->len, ev->chars);
    js_property_set(self, make_string_item(key), callback);
    if (ev->len == 6 && memcmp(ev->chars, "finish", 6) == 0 &&
        http_response_bool_prop(self, "writableFinished") &&
        js_http_is_callable(callback)) {
        js_call_function(callback, self, NULL, 0);
        js_microtask_flush();
    }
    return self;
}

// create a ServerResponse object for a connection
static Item make_response_object(JsHttpConn* conn) {
    Item res = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(res, JS_CLASS_SERVER_RESPONSE);  // A3-T3b
    if (get_type_id(http_server_response_prototype) == LMD_TYPE_MAP) {
        js_set_prototype(res, http_server_response_prototype);
    }
    js_property_set(res, make_string_item("__conn__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)conn)});
    js_property_set(res, make_string_item("statusCode"), (Item){.item = i2it(200)});
    js_property_set(res, make_string_item("__headers__"), http_new_header_object());
    js_property_set(res, make_string_item("__body__"), make_string_item("", 0));
    js_property_set(res, make_string_item("__sent__"), (Item){.item = b2it(false)});
    js_property_set(res, make_string_item("__headers_sent__"), (Item){.item = b2it(false)});
    js_property_set(res, make_string_item("__write_head_called__"), (Item){.item = b2it(false)});
    js_property_set(res, make_string_item("__used_send__"), (Item){.item = b2it(false)});
    js_property_set(res, make_string_item("__has_body_write__"), (Item){.item = b2it(false)});
    js_property_set(res, make_string_item("__reject_nonstandard_body_writes__"),
                    (Item){.item = b2it(conn && conn->server && conn->server->reject_nonstandard_body_writes)});
    js_property_set(res, make_string_item("headersSent"), (Item){.item = b2it(false)});
    js_property_set(res, make_string_item("__raw_headers__"), js_array_new(0));
    js_property_set(res, make_string_item("__chunks__"), js_array_new(0));
    js_property_set(res, make_string_item("__ending__"), (Item){.item = b2it(false)});
    js_property_set(res, make_string_item("statusMessage"), make_string_item(http_status_text(200)));
    js_property_set(res, make_string_item("__default_status_message__"), make_string_item(http_status_text(200)));
    js_property_set(res, make_string_item("finished"), (Item){.item = b2it(false)});
    js_property_set(res, make_string_item("writableEnded"), (Item){.item = b2it(false)});
    js_property_set(res, make_string_item("writableFinished"), (Item){.item = b2it(false)});
    js_property_set(res, make_string_item("writable"), (Item){.item = b2it(true)});
    js_property_set(res, make_string_item("writableCorked"), (Item){.item = i2it(0)});

    js_property_set(res, make_string_item("writeHead"),
                    js_new_function((void*)js_http_res_inst_writeHead, 3));
    js_property_set(res, make_string_item("setHeader"),
                    js_new_function((void*)js_http_res_inst_setHeader, 3));
    js_property_set(res, make_string_item("getHeader"),
                    js_new_function((void*)js_http_res_inst_getHeader, 2));
    js_property_set(res, make_string_item("getHeaders"),
                    js_new_function((void*)js_http_res_inst_getHeaders, 1));
    js_property_set(res, make_string_item("getHeaderNames"),
                    js_new_function((void*)js_http_res_inst_getHeaderNames, 1));
    js_property_set(res, make_string_item("getRawHeaderNames"),
                    js_new_function((void*)js_http_res_inst_getRawHeaderNames, 1));
    js_property_set(res, make_string_item("hasHeader"),
                    js_new_function((void*)js_http_res_inst_hasHeader, 2));
    js_property_set(res, make_string_item("removeHeader"),
                    js_new_function((void*)js_http_res_inst_removeHeader, 2));
    js_property_set(res, make_string_item("write"),
                    js_new_function((void*)js_http_res_inst_write, 3));
    js_property_set(res, make_string_item("_send"),
                    js_new_function((void*)js_http_res_inst_send_internal, 2));
    js_property_set(res, make_string_item("end"),
                    js_new_function((void*)js_http_res_inst_end, 3));
    js_property_set(res, make_string_item("flushHeaders"),
                    js_new_function((void*)js_http_res_inst_flushHeaders, 1));
    js_property_set(res, make_string_item("writeContinue"),
                    js_new_function((void*)js_http_res_inst_writeContinue, 1));
    js_property_set(res, make_string_item("setTimeout"),
                    js_new_function((void*)js_http_res_inst_setTimeout, 3));
    js_property_set(res, make_string_item("on"),
                    js_new_function((void*)js_http_res_inst_on, 3));

    if (conn && conn->server) {
        Item ctor = conn->server->server_response_ctor;
        if (get_type_id(ctor) == LMD_TYPE_FUNC || get_type_id(ctor) == LMD_TYPE_MAP) {
            Item proto = js_property_get(ctor, make_string_item("prototype"));
            if (get_type_id(proto) == LMD_TYPE_MAP) js_set_prototype(res, proto);
        }
    }

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

static Item http_conn_socket_object(JsHttpConn* conn);
static bool http_request_is_chunked(ParsedRequest* req);
static Item js_http_server_req_destroy(Item maybe_self, Item err_item);
static Item js_http_server_req_setTimeout(Item maybe_self, Item msecs_item, Item callback_item);

static Item make_request_object(JsHttpConn* conn, ParsedRequest* req) {
    Item msg = js_readable_new(ItemNull);
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(msg, JS_CLASS_INCOMING_MESSAGE);  // A3-T3b
    if (get_type_id(http_incoming_message_prototype) == LMD_TYPE_MAP) {
        js_set_prototype(msg, http_incoming_message_prototype);
    }

    js_property_set(msg, make_string_item("method"), make_string_item(req->method));
    js_property_set(msg, make_string_item("url"), make_string_item(req->url));
    int http_major = 1;
    int http_minor = 1;
    const char* version_string = req->http_version;
    if (strncmp(req->http_version, "HTTP/", 5) == 0) {
        version_string = req->http_version + 5;
        if (version_string[0] >= '0' && version_string[0] <= '9') {
            http_major = version_string[0] - '0';
        }
        const char* dot = strchr(version_string, '.');
        if (dot && dot[1] >= '0' && dot[1] <= '9') http_minor = dot[1] - '0';
    }
    js_property_set(msg, make_string_item("httpVersion"), make_string_item(version_string));
    js_property_set(msg, make_string_item("httpVersionMajor"), (Item){.item = i2it(http_major)});
    js_property_set(msg, make_string_item("httpVersionMinor"), (Item){.item = i2it(http_minor)});
    if (conn) {
        Item socket = http_conn_socket_object(conn);
        js_property_set(msg, make_string_item("socket"), socket);
        js_property_set(msg, make_string_item("connection"), socket);
        js_property_set(msg, make_string_item("__server_req_conn__"),
                        (Item){.item = i2it((int64_t)(uintptr_t)conn)});
        js_property_set(msg, make_string_item("destroy"),
                        js_new_function((void*)js_http_server_req_destroy, 2));
        js_property_set(msg, make_string_item("setTimeout"),
                        js_new_function((void*)js_http_server_req_setTimeout, 3));
    }
    Item async_resource = js_async_hooks_create_resource("HTTPINCOMINGMESSAGE", 19);
    js_property_set(msg, make_string_item("__async_resource__"), async_resource);

    // headers as lowercase-key object
    Item headers = http_new_header_object();
    Item raw_headers = js_array_new(0);
    for (int i = 0; i < req->header_count; i++) {
        js_array_push(raw_headers, make_string_item(req->raw_header_names[i]));
        js_array_push(raw_headers, make_string_item(req->raw_header_values[i]));
        http_request_headers_append(headers, req->header_names[i], req->header_values[i]);
    }
    js_property_set(msg, make_string_item("headers"), headers);
    js_property_set(msg, make_string_item("rawHeaders"), raw_headers);
    Item trailers = http_new_header_object();
    Item raw_trailers = js_array_new(0);
    for (int i = 0; i < req->trailer_count; i++) {
        js_array_push(raw_trailers, make_string_item(req->raw_trailer_names[i]));
        js_array_push(raw_trailers, make_string_item(req->raw_trailer_values[i]));
        http_request_headers_append(trailers, req->trailer_names[i], req->trailer_values[i]);
    }
    js_property_set(msg, make_string_item("trailers"), trailers);
    js_property_set(msg, make_string_item("rawTrailers"), raw_trailers);

    // body
    if (req->body && req->body_len > 0) {
        Item body = make_string_item(req->body, req->body_len);
        js_property_set(msg, make_string_item("body"), body);
        js_readable_push(msg, body);
    }
    if (req->body_complete) {
        js_readable_push(msg, ItemNull);
        // req.setTimeout() after a fully parsed request may still arm the
        // socket/server timeout, but the ended IncomingMessage must not fire.
        js_property_set(msg, make_string_item("__server_req_complete__"), (Item){.item = b2it(true)});
    } else {
        js_property_set(msg, make_string_item("__server_req_complete__"), (Item){.item = b2it(false)});
    }

    if (conn && conn->server) {
        Item ctor = conn->server->incoming_message_ctor;
        if (get_type_id(ctor) == LMD_TYPE_FUNC || get_type_id(ctor) == LMD_TYPE_MAP) {
            Item proto = js_property_get(ctor, make_string_item("prototype"));
            if (get_type_id(proto) == LMD_TYPE_MAP) js_set_prototype(msg, proto);
        }
    }

    return msg;
}

// =============================================================================
// HTTP Server
// =============================================================================

static uv_stream_t* http_server_stream(JsHttpServer* srv) {
    if (!srv) return NULL;
    if (!srv->handle_initialized) return NULL;
    return srv->is_pipe ? (uv_stream_t*)&srv->pipe : (uv_stream_t*)&srv->tcp;
}

static uv_handle_t* http_server_handle(JsHttpServer* srv) {
    if (!srv || !srv->handle_initialized) return NULL;
    return (uv_handle_t*)http_server_stream(srv);
}

static void http_server_link_conn(JsHttpServer* srv, JsHttpConn* conn) {
    if (!srv || !conn) return;
    conn->server_prev = NULL;
    conn->server_next = srv->connections_head;
    if (srv->connections_head) srv->connections_head->server_prev = conn;
    srv->connections_head = conn;
}

static void http_server_unlink_conn(JsHttpConn* conn) {
    if (!conn || !conn->server) return;
    JsHttpServer* srv = conn->server;
    if (conn->server_prev) {
        conn->server_prev->server_next = conn->server_next;
    } else if (srv->connections_head == conn) {
        srv->connections_head = conn->server_next;
    }
    if (conn->server_next) conn->server_next->server_prev = conn->server_prev;
    conn->server_prev = NULL;
    conn->server_next = NULL;
}

static bool http_conn_is_idle(JsHttpConn* conn) {
    if (!conn || conn->destroyed) return false;
    if (conn->open_response_count > 0 || conn->pending_response_writes > 0) return false;
    if (conn->current_request.item != 0 &&
        get_type_id(conn->current_request) != LMD_TYPE_UNDEFINED) return false;
    return conn->request_body_remaining <= 0 && !conn->request_body_chunked;
}

static void http_conn_maybe_close_for_server_close(JsHttpConn* conn) {
    if (!conn || !conn->server || !conn->server->close_requested) return;
    if (!http_conn_is_idle(conn)) return;
    // HTTP/1.1 keep-alive sockets stay referenced after their response; once
    // server.close() starts, idle accepted sockets must be actively drained.
    http_conn_close_now(conn);
}

static void http_server_close_idle_connections(JsHttpServer* srv) {
    if (!srv || !srv->close_requested) return;
    JsHttpConn* conn = srv->connections_head;
    while (conn) {
        JsHttpConn* next = conn->server_next;
        http_conn_maybe_close_for_server_close(conn);
        conn = next;
    }
}

static void http_server_maybe_finish_close(JsHttpServer* srv) {
    if (!srv || !srv->close_requested || !srv->handle_closed) return;
    if (srv->connection_count > 0) return;

    // libuv closes the listening handle independently from accepted sockets.
    // keep the server allocation alive until every JsHttpConn has detached,
    // otherwise late socket close/read callbacks can touch freed server state.
    if (!srv->close_event_emitted) {
        srv->close_event_emitted = true;
        Item on_close = js_property_get(srv->js_object, make_string_item("__on_close__"));
        if (get_type_id(on_close) == LMD_TYPE_FUNC) {
            js_call_function(on_close, srv->js_object, NULL, 0);
        }
    }
    mem_free(srv);
}

static void http_server_note_conn_closed(JsHttpConn* conn) {
    if (!conn || !conn->server) return;
    JsHttpServer* srv = conn->server;
    http_server_unlink_conn(conn);
    if (srv->connection_count > 0) srv->connection_count--;
    conn->server = NULL;
    http_server_maybe_finish_close(srv);
}

static void http_server_close_failed_listen(JsHttpServer* srv, Item self);

static Item http_server_error_tick(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item self = env[0];
    Item err = env[1];
    JsHttpServer* failed_srv = NULL;
    if (get_type_id(env[2]) == LMD_TYPE_INT) {
        failed_srv = (JsHttpServer*)(uintptr_t)it2i(env[2]);
    }
    Item on_err = js_property_get(self, make_string_item("__on_error__"));
    if (get_type_id(on_err) == LMD_TYPE_FUNC) {
        js_call_function(on_err, self, &err, 1);
        js_microtask_flush();
    }
    if (failed_srv) {
        http_server_close_failed_listen(failed_srv, self);
    }
    return make_js_undefined();
}

static void http_server_schedule_error(Item self, Item err, JsHttpServer* failed_srv) {
    Item* env = js_alloc_env(3);
    env[0] = self;
    env[1] = err;
    env[2] = failed_srv ? (Item){.item = i2it((int64_t)(uintptr_t)failed_srv)} : make_js_undefined();
    Item tick = js_new_closure((void*)http_server_error_tick, 0, env, 3);
    js_next_tick_enqueue(tick);
}

static void http_server_close_failed_listen(JsHttpServer* srv, Item self) {
    if (!srv) return;
    js_property_set(self, make_string_item("listening"), (Item){.item = b2it(false)});
    js_property_set(self, make_string_item("__server__"), ItemNull);
    uv_handle_t* handle = http_server_handle(srv);
    if (handle && !uv_is_closing(handle)) {
        // failed bind/listen still owns an initialized libuv handle; closing it
        // here prevents tiny error-path tests from waiting for the drain guard.
        uv_close(handle, [](uv_handle_t* h) {
            JsHttpServer* s = (JsHttpServer*)h->data;
            if (s) mem_free(s);
        });
        return;
    }
    if (!handle) {
        mem_free(srv);
    }
}

static void http_request_emit_close_now(Item req) {
    if (req.item == 0 || get_type_id(req) == LMD_TYPE_UNDEFINED) return;
    Item close_emitted_key = make_string_item("__close_emitted__");
    if (http_response_bool_prop(req, "__close_emitted__")) return;
    js_property_set(req, close_emitted_key, (Item){.item = b2it(true)});
    js_property_set(req, make_string_item("closed"), (Item){.item = b2it(true)});
    Item handle_item = js_property_get(req, make_string_item("__server_req_conn__"));
    if (get_type_id(handle_item) == LMD_TYPE_INT) {
        JsHttpConn* conn = (JsHttpConn*)(uintptr_t)it2i(handle_item);
        if (conn) conn->request_timeout_callback = make_js_undefined();
    }
    Item async_resource = js_property_get(req, make_string_item("__async_resource__"));
    // IncomingMessage async resources outlive the parser callback; destroy
    // them when the message closes so async_hooks init/destroy pairs balance.
    js_async_hooks_emit_destroy_resource(async_resource);
    Item emit_fn = js_property_get(req, make_string_item("emit"));
    if (get_type_id(emit_fn) == LMD_TYPE_FUNC) {
        Item close_event = make_string_item("close");
        js_call_function(emit_fn, req, &close_event, 1);
        js_microtask_flush();
    }
}

static void http_response_close_request(Item res) {
    Item req = js_property_get(res, make_string_item("__request__"));
    http_request_emit_close_now(req);
}

static void http_conn_destroy_unfinished_request(JsHttpConn* conn) {
    if (!conn || conn->current_request.item == 0 ||
        (!conn->request_body_chunked && conn->request_body_remaining <= 0)) return;
    Item req = conn->current_request;
    js_stream_destroy(req, make_js_undefined());
    http_request_emit_close_now(req);
    conn->current_request = make_js_undefined();
    conn->request_body_remaining = 0;
    conn->request_body_chunked = false;
}

static Item js_http_server_req_destroy(Item maybe_self, Item err_item) {
    Item self = js_http_receiver(maybe_self, "__server_req_conn__");
    Item actual_err = self.item == maybe_self.item ? err_item : maybe_self;
    TypeId err_type = get_type_id(actual_err);
    if (actual_err.item != 0 && err_type != LMD_TYPE_UNDEFINED && err_type != LMD_TYPE_NULL) {
        js_property_set(self, make_string_item("errored"), actual_err);
    }
    // Server-side IncomingMessage.destroy(err) aborts the socket, not an
    // uncaught stream error on the request object.
    js_property_set(self, make_string_item("destroyed"), (Item){.item = b2it(true)});
    js_property_set(self, make_string_item("readableAborted"), (Item){.item = b2it(true)});
    http_request_emit_close_now(self);
    Item handle_item = js_property_get(self, make_string_item("__server_req_conn__"));
    if (get_type_id(handle_item) == LMD_TYPE_INT) {
        JsHttpConn* conn = (JsHttpConn*)(uintptr_t)it2i(handle_item);
        http_conn_close_now(conn);
    }
    return self;
}

static void http_conn_close_now(JsHttpConn* conn) {
    if (!conn || conn->destroyed) return;
    // server-side socket timers capture this connection; clear before close so
    // timeout fixtures do not drain on stale timer handles.
    http_conn_clear_timeout(conn);
    http_conn_destroy_unfinished_request(conn);
    conn->destroyed = true;
    if (conn->socket_object.item != 0) {
        js_property_set(conn->socket_object, make_string_item("destroyed"), (Item){.item = b2it(true)});
    }
    http_server_note_conn_closed(conn);
    uv_handle_t* handle = (uv_handle_t*)http_conn_stream(conn);
    if (!uv_is_closing(handle)) {
        uv_close(handle, [](uv_handle_t* h) {
            JsHttpConn* c = (JsHttpConn*)h->data;
            if (c) {
                if (c->recv_buf) mem_free(c->recv_buf);
                mem_free(c);
            }
        });
    }
}

static void http_conn_maybe_close_after_response_writes(JsHttpConn* conn) {
    if (!conn || conn->destroyed) return;
    if (!conn->close_after_response_writes) return;
    if (conn->open_response_count > 0) return;
    if (conn->pending_response_writes > 0) return;
    http_conn_close_now(conn);
}

typedef struct HttpConnWriteReq {
    char* data;
    bool  close_after;
} HttpConnWriteReq;

static bool http_conn_write_bytes(JsHttpConn* conn, Item data_item, bool close_after) {
    if (!conn || conn->destroyed) return false;
    const char* data = NULL;
    int len = 0;
    if (!http_item_bytes(data_item, &data, &len)) return false;

    HttpConnWriteReq* write_req =
        (HttpConnWriteReq*)mem_calloc(1, sizeof(HttpConnWriteReq), MEM_CAT_JS_RUNTIME);
    uv_write_t* req = (uv_write_t*)mem_calloc(1, sizeof(uv_write_t), MEM_CAT_JS_RUNTIME);
    char* copy = NULL;
    if (len > 0) {
        copy = (char*)mem_alloc(len, MEM_CAT_JS_RUNTIME);
        memcpy(copy, data, (size_t)len);
    }
    write_req->data = copy;
    write_req->close_after = close_after;
    req->data = write_req;

    uv_buf_t buf = uv_buf_init(copy ? copy : (char*)"", (unsigned int)len);
    int r = uv_write(req, http_conn_stream(conn), &buf, 1,
        [](uv_write_t* req, int status) {
            HttpConnWriteReq* write_req = (HttpConnWriteReq*)req->data;
            bool close_after = false;
            if (write_req) {
                close_after = write_req->close_after;
                if (write_req->data) mem_free(write_req->data);
                mem_free(write_req);
            }
            JsHttpConn* c = (JsHttpConn*)((uv_stream_t*)req->handle)->data;
            if (close_after && c && !c->destroyed) {
                http_conn_close_now(c);
            }
            mem_free(req);
        });
    if (r != 0) {
        if (copy) mem_free(copy);
        mem_free(write_req);
        mem_free(req);
        if (close_after) http_conn_close_now(conn);
        return false;
    }
    return true;
}

static JsHttpConn* http_conn_from_socket_object(Item self) {
    Item handle_item = js_property_get(self, make_string_item("__http_conn__"));
    if (get_type_id(handle_item) != LMD_TYPE_INT) return NULL;
    return (JsHttpConn*)(uintptr_t)it2i(handle_item);
}

static Item js_http_conn_socket_write(Item maybe_self, Item data_item) {
    Item self = js_http_receiver(maybe_self, "__http_conn__");
    Item data = self.item == maybe_self.item ? data_item : maybe_self;
    JsHttpConn* conn = http_conn_from_socket_object(self);
    return (Item){.item = b2it(http_conn_write_bytes(conn, data, false))};
}

static Item js_http_conn_socket_end(Item maybe_self, Item data_item) {
    Item self = js_http_receiver(maybe_self, "__http_conn__");
    Item data = self.item == maybe_self.item ? data_item : maybe_self;
    JsHttpConn* conn = http_conn_from_socket_object(self);
    TypeId data_type = get_type_id(data);
    if (data.item != 0 && data.item != ITEM_NULL &&
        data_type != LMD_TYPE_UNDEFINED && data_type != LMD_TYPE_NULL) {
        http_conn_write_bytes(conn, data, true);
    } else {
        http_conn_close_now(conn);
    }
    return self;
}

static Item js_http_conn_socket_destroy(Item maybe_self) {
    Item self = js_http_receiver(maybe_self, "__http_conn__");
    JsHttpConn* conn = http_conn_from_socket_object(self);
    http_conn_close_now(conn);
    return self;
}

static void http_conn_socket_emit(JsHttpConn* conn, const char* event) {
    if (!conn || conn->socket_object.item == 0 || !event) return;
    char key[64];
    snprintf(key, sizeof(key), "__on_%s__", event);
    Item cb = js_property_get(conn->socket_object, make_string_item(key));
    if (js_http_is_callable(cb)) {
        Item socket = conn->socket_object;
        js_call_function(cb, conn->socket_object, &socket, 1);
        js_microtask_flush();
    }
}

static void http_conn_clear_timeout(JsHttpConn* conn) {
    if (!conn || !conn->timeout_timer_active) return;
    js_clearTimeout(conn->timeout_timer);
    conn->timeout_timer = make_js_undefined();
    conn->timeout_timer_active = false;
}

static Item js_http_conn_socket_timeout_fire(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    // timer env stores the native pointer as a tagged Lambda int; decode it
    // before use or timeout callbacks jump to stale/non-pointer addresses.
    JsHttpConn* conn = (env && get_type_id(env[0]) == LMD_TYPE_INT) ?
        (JsHttpConn*)(uintptr_t)it2i(env[0]) : NULL;
    if (!conn || conn->destroyed) return make_js_undefined();
    conn->timeout_timer_active = false;
    conn->timeout_timer = make_js_undefined();
    Item socket = http_conn_socket_object(conn);
    if (js_http_is_callable(conn->request_timeout_callback)) {
        js_call_function(conn->request_timeout_callback, socket, &socket, 1);
        js_microtask_flush();
    }
    Item timeout_response = conn->timeout_response;
    Item response_timeout_callback = conn->response_timeout_callback;
    if (timeout_response.item != 0 &&
        get_type_id(timeout_response) != LMD_TYPE_UNDEFINED) {
        // A pipelined socket can parse later responses before the head response
        // times out; fire the callback for the response that armed the timer.
        conn->timeout_response = make_js_undefined();
        conn->response_timeout_callback = make_js_undefined();
    }
    if (js_http_is_callable(response_timeout_callback)) {
        js_call_function(response_timeout_callback, socket, &socket, 1);
        js_microtask_flush();
    }
    Item response_event_target = timeout_response;
    if (response_event_target.item == 0 ||
        get_type_id(response_event_target) == LMD_TYPE_UNDEFINED) {
        response_event_target = conn->current_response;
    }
    if (response_event_target.item != 0 &&
        get_type_id(response_event_target) != LMD_TYPE_UNDEFINED) {
        Item res_timeout = js_property_get(response_event_target, make_string_item("__on_timeout__"));
        if (js_http_is_callable(res_timeout)) {
            js_call_function(res_timeout, response_event_target, &socket, 1);
            js_microtask_flush();
        }
    }
    http_conn_socket_emit(conn, "timeout");
    if (conn->server) {
        Item server_cb = conn->server->timeout_callback;
        if (!js_http_is_callable(server_cb)) {
            server_cb = js_property_get(conn->server->js_object, make_string_item("__on_timeout__"));
        }
        if (js_http_is_callable(server_cb)) {
            js_call_function(server_cb, conn->server->js_object, &socket, 1);
            js_microtask_flush();
        }
    }
    return make_js_undefined();
}

static void http_conn_start_timeout(JsHttpConn* conn, int64_t delay) {
    if (!conn || conn->destroyed) return;
    if (delay < 0) delay = 0;
    http_conn_clear_timeout(conn);
    if (delay <= 0) return;
    Item* env = js_alloc_env(1);
    env[0] = (Item){.item = i2it((int64_t)(uintptr_t)conn)};
    // all HTTP timeout APIs share the accepted socket timer; creating separate
    // timers per request/response leaves closed servers alive until the drain watchdog.
    Item timer = js_setTimeout(js_new_closure((void*)js_http_conn_socket_timeout_fire, 0, env, 1),
                               (Item){.item = i2it(delay)});
    conn->timeout_timer = timer;
    conn->timeout_timer_active = true;
}

static Item js_http_conn_socket_on(Item maybe_self, Item event_item, Item callback_item) {
    Item self = js_http_receiver(maybe_self, "__http_conn__");
    Item actual_event = self.item == maybe_self.item ? event_item : maybe_self;
    Item actual_callback = self.item == maybe_self.item ? callback_item : event_item;
    if (get_type_id(actual_event) != LMD_TYPE_STRING) return self;
    String* ev = it2s(actual_event);
    char key[64];
    snprintf(key, sizeof(key), "__on_%.*s__", (int)ev->len, ev->chars);
    js_property_set(self, make_string_item(key), actual_callback);
    return self;
}

static Item js_http_conn_socket_setTimeout(Item maybe_self, Item msecs_item, Item callback_item) {
    Item self = js_http_receiver(maybe_self, "__http_conn__");
    Item actual_msecs = self.item == maybe_self.item ? msecs_item : maybe_self;
    Item actual_callback = self.item == maybe_self.item ? callback_item : msecs_item;
    JsHttpConn* conn = http_conn_from_socket_object(self);
    if (!conn || conn->destroyed) return self;
    if (js_http_is_callable(actual_callback)) {
        js_http_conn_socket_on(self, make_string_item("timeout"), actual_callback);
    }
    int64_t delay = 0;
    if (get_type_id(actual_msecs) == LMD_TYPE_INT) delay = it2i(actual_msecs);
    if (delay < 0) delay = 0;
    http_conn_start_timeout(conn, delay);
    return self;
}

static Item js_http_server_req_setTimeout(Item maybe_self, Item msecs_item, Item callback_item) {
    Item self = js_http_receiver(maybe_self, "__server_req_conn__");
    Item actual_msecs = self.item == maybe_self.item ? msecs_item : maybe_self;
    Item actual_callback = self.item == maybe_self.item ? callback_item : msecs_item;
    Item handle_item = js_property_get(self, make_string_item("__server_req_conn__"));
    if (get_type_id(handle_item) != LMD_TYPE_INT) return self;
    JsHttpConn* conn = (JsHttpConn*)(uintptr_t)it2i(handle_item);
    if (!conn || conn->destroyed) return self;
    if (!http_response_bool_prop(self, "__server_req_complete__") &&
        js_http_is_callable(actual_callback)) {
        conn->request_timeout_callback = actual_callback;
    } else {
        conn->request_timeout_callback = make_js_undefined();
    }
    int64_t delay = 0;
    if (get_type_id(actual_msecs) == LMD_TYPE_INT) delay = it2i(actual_msecs);
    http_conn_start_timeout(conn, delay);
    return self;
}

static Item js_http_res_inst_setTimeout(Item maybe_self, Item msecs_item, Item callback_item) {
    Item self = js_http_receiver(maybe_self, "__conn__");
    Item actual_msecs = self.item == maybe_self.item ? msecs_item : maybe_self;
    Item actual_callback = self.item == maybe_self.item ? callback_item : msecs_item;
    Item handle_item = js_property_get(self, make_string_item("__conn__"));
    if (get_type_id(handle_item) != LMD_TYPE_INT) return self;
    JsHttpConn* conn = (JsHttpConn*)(uintptr_t)it2i(handle_item);
    if (!conn || conn->destroyed) return self;
    if (conn->timeout_response.item != 0 &&
        get_type_id(conn->timeout_response) != LMD_TYPE_UNDEFINED &&
        conn->timeout_response.item != self.item) {
        // pipelined responses share one socket timer; a later response must not
        // steal the timeout callback from the earlier response still in flight.
        return self;
    }
    conn->timeout_response = self;
    if (js_http_is_callable(actual_callback)) {
        conn->response_timeout_callback = actual_callback;
    } else {
        conn->response_timeout_callback = make_js_undefined();
    }
    int64_t delay = 0;
    if (get_type_id(actual_msecs) == LMD_TYPE_INT) delay = it2i(actual_msecs);
    http_conn_start_timeout(conn, delay);
    return self;
}

static Item http_conn_socket_object(JsHttpConn* conn) {
    if (!conn) return make_js_undefined();
    if (conn->socket_object.item != 0) return conn->socket_object;

    Item obj = js_new_object();
    js_class_stamp(obj, JS_CLASS_SOCKET);
    Item net_proto = js_net_get_socket_prototype();
    if (get_type_id(net_proto) == LMD_TYPE_MAP) {
        // HTTP accepted sockets are net.Socket instances; missing the shared
        // prototype makes instanceof checks skip timeout cleanup handlers.
        js_set_prototype(obj, net_proto);
    }
    js_property_set(obj, make_string_item("__http_conn__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)conn)});
    js_property_set(obj, make_string_item("on"),
                    js_new_function((void*)js_http_conn_socket_on, 3));
    js_property_set(obj, make_string_item("once"),
                    js_new_function((void*)js_http_conn_socket_on, 3));
    js_property_set(obj, make_string_item("write"),
                    js_new_function((void*)js_http_conn_socket_write, 2));
    js_property_set(obj, make_string_item("end"),
                    js_new_function((void*)js_http_conn_socket_end, 2));
    js_property_set(obj, make_string_item("destroy"),
                    js_new_function((void*)js_http_conn_socket_destroy, 1));
    js_property_set(obj, make_string_item("setTimeout"),
                    js_new_function((void*)js_http_conn_socket_setTimeout, 3));
    js_property_set(obj, make_string_item("destroyed"), (Item){.item = b2it(false)});
    Item hwm = (Item){.item = i2it(16 * 1024)};
    Item readable_state = js_new_object();
    js_property_set(readable_state, make_string_item("highWaterMark"), hwm);
    Item writable_state = js_new_object();
    js_property_set(writable_state, make_string_item("highWaterMark"), hwm);
    js_property_set(obj, make_string_item("_readableState"), readable_state);
    js_property_set(obj, make_string_item("_writableState"), writable_state);
    js_property_set(obj, make_string_item("readableHighWaterMark"), hwm);
    js_property_set(obj, make_string_item("writableHighWaterMark"), hwm);
    conn->socket_object = obj;
    return obj;
}

static int http_server_max_requests(JsHttpServer* srv) {
    if (!srv) return 0;
    Item value = js_property_get(srv->js_object, make_string_item("maxRequestsPerSocket"));
    if (get_type_id(value) == LMD_TYPE_INT) return (int)it2i(value);
    return 0;
}

static bool http_server_max_requests_is_null(JsHttpServer* srv) {
    if (!srv) return false;
    Item value = js_property_get(srv->js_object, make_string_item("maxRequestsPerSocket"));
    return get_type_id(value) == LMD_TYPE_NULL;
}

static bool http_request_has_invalid_method_start(const char* data, int len) {
    if (!data || len <= 0) return false;
    // Pipelined garbage such as leftover body bytes must be rejected promptly;
    // treating lowercase text as an incomplete method leaves the HTTP conn open until drain.
    return data[0] < 'A' || data[0] > 'Z';
}

static void http_conn_feed_request_body(JsHttpConn* conn) {
    if (!conn || conn->current_request.item == 0) return;
    if (conn->request_body_chunked) {
        // Expect/continue requests can stream chunked body bytes after the
        // headers have already dispatched the IncomingMessage.
        if (conn->recv_len <= 0) return;
        int consumed = 0;
        bool complete = false;
        int decoded_len = http_decode_chunked_request_body(conn->recv_buf, conn->recv_len,
                                                           NULL, &consumed, &complete, false);
        if (!complete || decoded_len < 0 || consumed <= 0 || consumed > conn->recv_len) {
            return;
        }
        ParsedRequest trailer_req;
        memset(&trailer_req, 0, sizeof(trailer_req));
        decoded_len = http_decode_chunked_request_body(conn->recv_buf, conn->recv_len,
                                                       &trailer_req, &consumed, &complete, true);
        if (decoded_len > 0) {
            js_readable_push(conn->current_request, make_string_item(conn->recv_buf, decoded_len));
        }
        js_readable_push(conn->current_request, ItemNull);
        js_property_set(conn->current_request, make_string_item("__server_req_complete__"),
                        (Item){.item = b2it(true)});
        conn->request_timeout_callback = make_js_undefined();
        int remaining = conn->recv_len - consumed;
        if (remaining > 0) memmove(conn->recv_buf, conn->recv_buf + consumed, (size_t)remaining);
        conn->recv_len = remaining;
        conn->current_request = make_js_undefined();
        conn->request_body_chunked = false;
        return;
    }
    if (conn->request_body_remaining <= 0) return;
    while (conn->recv_len > 0 && conn->request_body_remaining > 0) {
        int take = conn->recv_len < conn->request_body_remaining ?
            conn->recv_len : conn->request_body_remaining;
        if (take > 0) {
            js_readable_push(conn->current_request, make_string_item(conn->recv_buf, take));
            int remaining = conn->recv_len - take;
            if (remaining > 0) memmove(conn->recv_buf, conn->recv_buf + take, (size_t)remaining);
            conn->recv_len = remaining;
            conn->request_body_remaining -= take;
        }
    }
    if (conn->request_body_remaining == 0) {
        js_readable_push(conn->current_request, ItemNull);
        js_property_set(conn->current_request, make_string_item("__server_req_complete__"),
                        (Item){.item = b2it(true)});
        conn->request_timeout_callback = make_js_undefined();
        conn->current_request = make_js_undefined();
        conn->request_body_chunked = false;
    }
}

static bool http_server_emit_client_error(JsHttpConn* conn, const char* code,
                                          const char* message, int bytes_parsed) {
    if (!conn || !conn->server) return false;
    Item handler = js_property_get(conn->server->js_object, make_string_item("__on_clientError__"));
    if (!js_http_is_callable(handler)) return false;

    Item err = js_new_error(make_string_item(message));
    js_property_set(err, make_string_item("code"), make_string_item(code));
    js_property_set(err, make_string_item("bytesParsed"), (Item){.item = i2it(bytes_parsed)});
    js_property_set(err, make_string_item("rawPacket"),
                    js_buffer_from_bytes(conn->recv_buf, conn->recv_len));
    Item socket = http_conn_socket_object(conn);
    Item args[2] = { err, socket };
    js_call_function(handler, conn->server->js_object, args, 2);
    js_microtask_flush();
    return true;
}

static void http_server_send_default_error(JsHttpConn* conn, int status) {
    if (!conn || conn->destroyed) return;
    const char* reason = http_status_text(status);
    char response[160];
    int len = snprintf(response, sizeof(response),
                       "HTTP/1.1 %d %s\r\nConnection: close\r\n\r\n",
                       status, reason ? reason : "Error");
    if (len < 0) {
        http_conn_close_now(conn);
        return;
    }
    if (len > (int)sizeof(response)) len = (int)sizeof(response);
    // parser errors with no clientError listener still owe the client a final
    // HTTP error response; otherwise the accepted socket stays referenced until the drain watchdog.
    http_conn_write_bytes(conn, make_string_item(response, len), true);
}

static bool http_request_wants_keep_alive(ParsedRequest* req, bool has_buffered_request) {
    (void)has_buffered_request;
    const char* connection = http_request_header(req, "connection");
    if (connection && http_header_has_token(connection, "close")) return false;
    if (strcmp(req->http_version, "HTTP/1.0") == 0) {
        return connection && http_header_has_token(connection, "keep-alive");
    }
    // HTTP/1.1 persistence is the default; tying it to bytes already buffered
    // drops pipelined requests that arrive in a later libuv read after /1 ends.
    return true;
}

static bool http_request_has_expect_continue(ParsedRequest* req) {
    const char* expect = http_request_header(req, "expect");
    return expect && http_header_has_token(expect, "100-continue");
}

static bool http_request_has_expect_header(ParsedRequest* req) {
    const char* expect = http_request_header(req, "expect");
    return expect && expect[0] != '\0';
}

static bool http_request_is_chunked(ParsedRequest* req) {
    const char* transfer_encoding = http_request_header(req, "transfer-encoding");
    return transfer_encoding && http_header_has_token(transfer_encoding, "chunked");
}

static bool http_request_accepts_chunked_response(ParsedRequest* req) {
    const char* te = http_request_header(req, "te");
    return te && http_header_has_token(te, "chunked");
}

static Item http_response_for_request(JsHttpConn* conn, ParsedRequest* req, bool force_close,
                                      bool over_max, bool has_buffered_request) {
    Item res_obj = make_response_object(conn);
    if (conn) {
        conn->open_response_count++;
        conn->current_response = res_obj;
    }
    bool keep_alive = !force_close && !over_max && http_request_wants_keep_alive(req, has_buffered_request);
    int max_requests = http_server_max_requests(conn ? conn->server : NULL);
    bool close_after = !keep_alive || (max_requests > 0 && conn && conn->request_count >= max_requests);
    if (over_max) close_after = true;
    js_property_set(res_obj, make_string_item("__close_after_response__"), (Item){.item = b2it(close_after)});
    js_property_set(res_obj, make_string_item("__keep_alive_max__"), (Item){.item = i2it(max_requests)});
    js_property_set(res_obj, make_string_item("__keep_alive_max_null__"),
                    (Item){.item = b2it(http_server_max_requests_is_null(conn ? conn->server : NULL))});
    js_property_set(res_obj, make_string_item("__request_http_version__"),
                    req ? make_string_item(req->http_version) : make_string_item("HTTP/1.1"));
    js_property_set(res_obj, make_string_item("__request_method__"),
                    req ? make_string_item(req->method) : make_string_item("GET"));
    js_property_set(res_obj, make_string_item("__request_accepts_chunked_response__"),
                    (Item){.item = b2it(req && http_request_accepts_chunked_response(req))});
    return res_obj;
}

static void http_server_send_service_unavailable(JsHttpConn* conn) {
    if (!conn || conn->destroyed) return;
    Item res_obj = http_response_for_request(conn, NULL, true, true, false);
    js_property_set(res_obj, make_string_item("statusCode"), (Item){.item = i2it(503)});
    js_property_set(res_obj, make_string_item("statusMessage"), make_string_item("Service Unavailable"));
    js_property_set(res_obj, make_string_item("__body__"), make_string_item("", 0));
    http_response_flush(res_obj);
}

static void http_server_send_expectation_failed(JsHttpConn* conn, ParsedRequest* req,
                                                bool has_buffered_request) {
    if (!conn || conn->destroyed) return;
    Item res_obj = http_response_for_request(conn, req, false, false, has_buffered_request);
    js_property_set(res_obj, make_string_item("statusCode"), (Item){.item = i2it(417)});
    js_property_set(res_obj, make_string_item("statusMessage"), make_string_item("Expectation Failed"));
    js_property_set(res_obj, make_string_item("__body__"), make_string_item("", 0));
    http_response_flush(res_obj);
}

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
    if (js_event_loop_is_shutting_down()) {
        uv_read_stop(stream);
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

        while (conn->recv_len > 0 && !conn->destroyed) {
            if (conn->current_request.item != 0 &&
                (conn->request_body_remaining > 0 || conn->request_body_chunked)) {
                int before_body_len = conn->recv_len;
                http_conn_feed_request_body(conn);
                if (conn->recv_len <= 0) break;
                if ((conn->request_body_remaining > 0 || conn->request_body_chunked) &&
                    conn->recv_len == before_body_len) {
                    // partial streamed bodies must yield to libuv; spinning here starves timers and later reads.
                    break;
                }
                continue;
            }

            int skip = 0;
            while (skip < conn->recv_len &&
                   (conn->recv_buf[skip] == '\r' || conn->recv_buf[skip] == '\n')) {
                skip++;
            }
            if (skip > 0) {
                int remaining = conn->recv_len - skip;
                if (remaining > 0) memmove(conn->recv_buf, conn->recv_buf + skip, (size_t)remaining);
                conn->recv_len = remaining;
                if (conn->recv_len <= 0) break;
            }

            if (http_request_has_invalid_method_start(conn->recv_buf, conn->recv_len)) {
                bool handled = http_server_emit_client_error(
                    conn,
                    "HPE_INVALID_METHOD",
                    "Parse Error: Invalid method encountered",
                    1);
                if (!handled) {
                    http_server_send_default_error(conn, 400);
                    break;
                }
                if (conn->destroyed) break;
                int remaining = conn->recv_len - 1;
                if (remaining > 0) memmove(conn->recv_buf, conn->recv_buf + 1, (size_t)remaining);
                conn->recv_len = remaining;
                continue;
            }

            ParsedRequest req;
            int consumed = 0;
            if (parse_http_request(conn->recv_buf, conn->recv_len, &req, &consumed) != 0) {
                break;
            }
            if (req.error_status > 0) {
                http_server_send_default_error(conn, req.error_status);
                conn->recv_len = 0;
                break;
            }
            bool expect_continue = http_request_has_expect_continue(&req);
            bool expect_unknown = http_request_has_expect_header(&req) && !expect_continue;
            // Node emits the request after headers and streams incomplete
            // framed bodies; waiting for the terminal chunk/end prevents
            // req.setTimeout() from observing clients that never finish.
            if (consumed <= 0 || consumed > conn->recv_len) break;

            JsHttpServer* srv = conn->server;
            bool has_buffered_request = conn->recv_len > consumed;
            conn->request_count++;
            int max_requests = http_server_max_requests(srv);
            if (max_requests > 0 && conn->request_count > max_requests) {
                http_server_send_service_unavailable(conn);
            } else if (srv) {
                Item on_req = js_property_get(srv->js_object, make_string_item("__on_request__"));
                Item on_expect = expect_continue ?
                    js_property_get(srv->js_object, make_string_item("__on_checkContinue__")) :
                    (expect_unknown ?
                     js_property_get(srv->js_object, make_string_item("__on_checkExpectation__")) :
                     make_js_undefined());
                bool has_handler = get_type_id(srv->request_handler) == LMD_TYPE_FUNC;
                bool has_request_event = get_type_id(on_req) == LMD_TYPE_FUNC;
                bool has_expect_handler = get_type_id(on_expect) == LMD_TYPE_FUNC;
                if (expect_unknown && !has_expect_handler) {
                    // unknown Expect values are answered at header time; waiting for a body can deadlock chunked clients.
                    http_server_send_expectation_failed(conn, &req, has_buffered_request);
                    int remaining = conn->recv_len - consumed;
                    if (remaining > 0) memmove(conn->recv_buf, conn->recv_buf + consumed, (size_t)remaining);
                    conn->recv_len = remaining;
                    continue;
                }
                if (!has_expect_handler && !has_handler && !has_request_event) {
                    int remaining = conn->recv_len - consumed;
                    if (remaining > 0) memmove(conn->recv_buf, conn->recv_buf + consumed, (size_t)remaining);
                    conn->recv_len = remaining;
                    continue;
                }
                if (conn->async_resource.item == 0) conn->async_resource = js_new_object();
                Item req_obj = make_request_object(conn, &req);
                Item res_obj = http_response_for_request(conn, &req, false, false, has_buffered_request);
                js_property_set(res_obj, make_string_item("__request__"), req_obj);
                if (!req.body_complete) {
                    conn->current_request = req_obj;
                    conn->request_body_remaining = req.content_length - req.body_len;
                    conn->request_body_chunked = http_request_is_chunked(&req);
                }

                Item args[2] = { req_obj, res_obj };
                if (has_expect_handler) {
                    Item previous_resource = js_async_hooks_enter_resource(conn->async_resource);
                    js_call_function(on_expect, srv->js_object, args, 2);
                    js_async_hooks_restore_resource(previous_resource);
                    js_microtask_flush();
                } else {
                    if (expect_continue) {
                        http_conn_write_bytes(conn, make_string_item("HTTP/1.1 100 Continue\r\n\r\n"), false);
                    }
                    if (has_handler) {
                        Item previous_resource = js_async_hooks_enter_resource(conn->async_resource);
                        js_call_function(srv->request_handler, srv->js_object, args, 2);
                        js_async_hooks_restore_resource(previous_resource);
                        js_microtask_flush();
                    }

                    // emit 'request' event
                    if (has_request_event && (!has_handler || on_req.item != srv->request_handler.item)) {
                        Item previous_resource = js_async_hooks_enter_resource(conn->async_resource);
                        js_call_function(on_req, srv->js_object, args, 2);
                        js_async_hooks_restore_resource(previous_resource);
                        js_microtask_flush();
                    }
                }
            }
            int remaining = conn->recv_len - consumed;
            if (remaining > 0) memmove(conn->recv_buf, conn->recv_buf + consumed, (size_t)remaining);
            conn->recv_len = remaining;
        }
    }

    if (buf->base) mem_free(buf->base);

    if (nread < 0 && conn && !conn->destroyed) {
        conn->read_ended = true;
        if (conn->recv_len > 0 && conn->current_request.item == 0) {
            ParsedRequest req;
            int consumed = 0;
            if (parse_http_request(conn->recv_buf, conn->recv_len, &req, &consumed) == 0 &&
                consumed > 0 && consumed <= conn->recv_len) {
                if (req.error_status > 0) {
                    http_server_send_default_error(conn, req.error_status);
                    conn->recv_len = 0;
                } else {
                bool expect_continue = http_request_has_expect_continue(&req);
                bool expect_unknown = http_request_has_expect_header(&req) && !expect_continue;
                JsHttpServer* srv = conn->server;
                conn->request_count++;
                if (srv) {
                    Item on_req = js_property_get(srv->js_object, make_string_item("__on_request__"));
                    Item on_expect = expect_continue ?
                        js_property_get(srv->js_object, make_string_item("__on_checkContinue__")) :
                        (expect_unknown ?
                         js_property_get(srv->js_object, make_string_item("__on_checkExpectation__")) :
                         make_js_undefined());
                    bool has_handler = get_type_id(srv->request_handler) == LMD_TYPE_FUNC;
                    bool has_request_event = get_type_id(on_req) == LMD_TYPE_FUNC;
                    bool has_expect_handler = get_type_id(on_expect) == LMD_TYPE_FUNC;
                    if (expect_unknown && !has_expect_handler) {
                        // unknown Expect values are answered at header time; waiting for a body can deadlock chunked clients.
                        http_server_send_expectation_failed(conn, &req, false);
                    } else if (has_expect_handler || has_handler || has_request_event) {
                        if (conn->async_resource.item == 0) conn->async_resource = js_new_object();
                        Item req_obj = make_request_object(conn, &req);
                        Item res_obj = http_response_for_request(conn, &req, true, false, false);
                        js_property_set(res_obj, make_string_item("__request__"), req_obj);
                        if (!req.body_complete) {
                            conn->current_request = req_obj;
                            conn->request_body_remaining = req.content_length - req.body_len;
                            conn->request_body_chunked = http_request_is_chunked(&req);
                        }
                        Item args[2] = { req_obj, res_obj };
                        if (has_expect_handler) {
                            Item previous_resource = js_async_hooks_enter_resource(conn->async_resource);
                            js_call_function(on_expect, srv->js_object, args, 2);
                            js_async_hooks_restore_resource(previous_resource);
                            js_microtask_flush();
                        } else {
                            if (expect_continue) {
                                http_conn_write_bytes(conn, make_string_item("HTTP/1.1 100 Continue\r\n\r\n"), false);
                            }
                            if (has_handler) {
                                Item previous_resource = js_async_hooks_enter_resource(conn->async_resource);
                                js_call_function(srv->request_handler, srv->js_object, args, 2);
                                js_async_hooks_restore_resource(previous_resource);
                                js_microtask_flush();
                            }
                            if (has_request_event && (!has_handler || on_req.item != srv->request_handler.item)) {
                                Item previous_resource = js_async_hooks_enter_resource(conn->async_resource);
                                js_call_function(on_req, srv->js_object, args, 2);
                                js_async_hooks_restore_resource(previous_resource);
                                js_microtask_flush();
                            }
                        }
                    }
                }
                int remaining = conn->recv_len - consumed;
                if (remaining > 0) memmove(conn->recv_buf, conn->recv_buf + consumed, (size_t)remaining);
                conn->recv_len = remaining;
                }
            }
        }
        http_conn_destroy_unfinished_request(conn);
        js_microtask_flush();
        uv_read_stop(stream);
        if (conn->request_count == 0 ||
            (conn->recv_len == 0 && (conn->current_request.item == 0 ||
             get_type_id(conn->current_request) == LMD_TYPE_UNDEFINED))) {
            if (conn->pending_response_writes > 0) {
                conn->close_after_response_writes = true;
            } else {
                http_conn_close_now(conn);
            }
        }
    }
}

static void http_server_connection_cb(uv_stream_t* server, int status) {
    if (status < 0) return;
    JsHttpServer* srv = (JsHttpServer*)server->data;
    if (!srv) return;

    uv_loop_t* loop = server->loop;

    JsHttpConn* conn = (JsHttpConn*)mem_calloc(1, sizeof(JsHttpConn), MEM_CAT_JS_RUNTIME);
    conn->is_pipe = srv->is_pipe;
    if (conn->is_pipe) {
        uv_pipe_init(loop, &conn->pipe, 0);
        conn->pipe.data = conn;
    } else {
        uv_tcp_init(loop, &conn->tcp);
        conn->tcp.data = conn;
    }
    conn->server = srv;
    conn->recv_cap = 8192;
    conn->recv_buf = (char*)mem_alloc(conn->recv_cap, MEM_CAT_JS_RUNTIME);

    if (uv_accept(server, http_conn_stream(conn)) == 0) {
        srv->connection_count++;
        http_server_link_conn(srv, conn);
        if (srv->timeout_msecs > 0) http_conn_start_timeout(conn, srv->timeout_msecs);
        uv_read_start(http_conn_stream(conn), http_server_alloc_cb, http_server_read_cb);
    } else {
        mem_free(conn->recv_buf);
        uv_close((uv_handle_t*)http_conn_stream(conn), [](uv_handle_t* h) {
            mem_free(h->data);
        });
    }
}

// server.listen(port, [host], [callback])
static Item js_http_server_listening_tick(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();

    Item self = env[0];
    Item callback = env[1];
    Item on_listening = js_property_get(self, make_string_item("__on_listening__"));
    if (get_type_id(on_listening) == LMD_TYPE_FUNC) {
        js_call_function(on_listening, self, NULL, 0);
    }
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, self, NULL, 0);
    }
    js_microtask_flush();
    return make_js_undefined();
}

extern "C" Item js_http_server_listen(Item self, Item port_item, Item host_item, Item callback) {
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0) return self;
    JsHttpServer* srv = (JsHttpServer*)(uintptr_t)it2i(handle_item);
    if (!srv) return self;

    int port = 80;
    bool use_pipe = false;
    char pipe_path[4096];
    pipe_path[0] = '\0';
    if (get_type_id(port_item) == LMD_TYPE_FUNC) {
        callback = port_item;
        host_item = make_js_undefined();
        port = 0;
    } else if (get_type_id(port_item) == LMD_TYPE_STRING) {
        String* path = it2s(port_item);
        int len = (int)path->len < (int)sizeof(pipe_path) - 1 ?
            (int)path->len : (int)sizeof(pipe_path) - 1;
        memcpy(pipe_path, path->chars, (size_t)len);
        pipe_path[len] = '\0';
        use_pipe = true;
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

    if (js_http_is_object_like(port_item) && js_http_object_has_key(port_item, "fd")) {
        Item fd_item = js_property_get(port_item, make_string_item("fd"));
        TypeId fd_type = get_type_id(fd_item);
        bool valid_fd_number = false;
        int64_t fd_value = 0;
        if (fd_type == LMD_TYPE_INT) {
            fd_value = it2i(fd_item);
            valid_fd_number = true;
        } else if (fd_type == LMD_TYPE_INT64) {
            fd_value = it2l(fd_item);
            valid_fd_number = true;
        }
        if (!valid_fd_number || fd_value < 0) {
            return js_throw_type_error_code(
                "ERR_INVALID_ARG_VALUE",
                "The argument 'options.fd' must be a non-negative number");
        }
        srv->is_pipe = false;
        uv_tcp_init(lambda_uv_loop(), &srv->tcp);
        srv->tcp.data = srv;
        srv->handle_initialized = true;
        // HTTP listen(options) used to ignore fd and bind a fresh port, so
        // inherited-fd children announced readiness without accepting traffic.
        int open_r = uv_tcp_open(&srv->tcp, (uv_os_sock_t)fd_value);
        if (open_r != 0) {
            log_error("http: server fd open failed: %s", uv_strerror(open_r));
            Item err = http_error_from_uv(open_r);
            http_server_schedule_error(self, err, srv);
            return self;
        }
        int listen_r = uv_listen(http_server_stream(srv), 128, http_server_connection_cb);
        if (listen_r != 0) {
            log_error("http: server fd listen failed: %s", uv_strerror(listen_r));
            Item err = http_error_from_uv(listen_r);
            http_server_schedule_error(self, err, srv);
            return self;
        }
        js_property_set(self, make_string_item("listening"), (Item){.item = b2it(true)});

        Item* env = js_alloc_env(2);
        env[0] = self;
        env[1] = callback;
        Item tick = js_new_closure((void*)js_http_server_listening_tick, 0, env, 2);
        js_next_tick_enqueue(tick);
        return self;
    }

    int r = 0;
    srv->is_pipe = use_pipe;
    if (use_pipe) {
        uv_pipe_init(lambda_uv_loop(), &srv->pipe, 0);
        srv->pipe.data = srv;
        srv->handle_initialized = true;
        remove(pipe_path);
        r = uv_pipe_bind(&srv->pipe, pipe_path);
    } else {
        struct sockaddr_in addr;
        uv_tcp_init(lambda_uv_loop(), &srv->tcp);
        srv->tcp.data = srv;
        srv->handle_initialized = true;
        uv_ip4_addr(host_buf, port, &addr);
        r = uv_tcp_bind(&srv->tcp, (const struct sockaddr*)&addr, 0);
    }
    if (r != 0) {
        log_error("http: server bind failed: %s", uv_strerror(r));
        Item err = http_error_from_uv(r);
        http_server_schedule_error(self, err, srv);
        return self;
    }

    r = uv_listen(http_server_stream(srv), 128, http_server_connection_cb);
    if (r != 0) {
        log_error("http: server listen failed: %s", uv_strerror(r));
        Item err = http_error_from_uv(r);
        http_server_schedule_error(self, err, srv);
        return self;
    }

    // store listening address info
    js_property_set(self, make_string_item("listening"), (Item){.item = b2it(true)});

    Item* env = js_alloc_env(2);
    env[0] = self;
    env[1] = callback;
    Item tick = js_new_closure((void*)js_http_server_listening_tick, 0, env, 2);
    js_next_tick_enqueue(tick);

    return self;
}

// server.close([callback])
extern "C" Item js_http_server_close(Item self, Item callback) {
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0 ||
        get_type_id(handle_item) == LMD_TYPE_NULL ||
        get_type_id(handle_item) == LMD_TYPE_UNDEFINED) {
        if (get_type_id(callback) == LMD_TYPE_FUNC) {
            Item err = http_error_with_code("ERR_SERVER_NOT_RUNNING", "Server is not running.");
            js_call_function(callback, self, &err, 1);
            js_microtask_flush();
        }
        return self;
    }
    JsHttpServer* srv = (JsHttpServer*)(uintptr_t)it2i(handle_item);
    if (!srv || !srv->handle_initialized) {
        if (get_type_id(callback) == LMD_TYPE_FUNC) {
            Item err = http_error_with_code("ERR_SERVER_NOT_RUNNING", "Server is not running.");
            js_call_function(callback, self, &err, 1);
            js_microtask_flush();
        }
        return self;
    }

    js_property_set(self, make_string_item("listening"), (Item){.item = b2it(false)});
    srv->close_requested = true;
    http_server_close_idle_connections(srv);

    uv_handle_t* handle = http_server_handle(srv);
    if (handle && !uv_is_closing(handle)) {
        js_property_set(self, make_string_item("__server__"), ItemNull);
        uv_close(handle, [](uv_handle_t* h) {
            JsHttpServer* s = (JsHttpServer*)h->data;
            if (s) {
                s->handle_closed = true;
                http_server_maybe_finish_close(s);
            }
        });
    }

    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, self, NULL, 0);
    }

    return self;
}

extern "C" Item js_http_server_getConnections(Item self, Item callback) {
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0) return self;
    JsHttpServer* srv = (JsHttpServer*)(uintptr_t)it2i(handle_item);
    if (!srv) return self;
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item args[2] = { ItemNull, (Item){.item = i2it(srv->connection_count)} };
        js_call_function(callback, self, args, 2);
        js_microtask_flush();
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
    if (ev->len == 7 && memcmp(ev->chars, "request", 7) == 0 &&
        get_type_id(callback) == LMD_TYPE_FUNC) {
        Item handle_item = js_property_get(self, make_string_item("__server__"));
        if (get_type_id(handle_item) == LMD_TYPE_INT) {
            JsHttpServer* srv = (JsHttpServer*)(uintptr_t)it2i(handle_item);
            if (srv) srv->request_handler = callback;
        }
    }
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
    Item this_obj = js_get_this();
    if (js_class_id(this_obj) == JS_CLASS_SERVER) {
        return js_http_server_close(this_obj, maybe_self);
    }
    Item self = js_http_receiver(maybe_self, "__server__");
    return js_http_server_close(self, self.item == maybe_self.item ? callback : maybe_self);
}

static Item js_http_server_inst_getConnections(Item maybe_self, Item callback) {
    Item self = js_http_receiver(maybe_self, "__server__");
    return js_http_server_getConnections(self, self.item == maybe_self.item ? callback : maybe_self);
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
    uv_handle_t* handle = http_server_handle(srv);
    if (srv && handle && !uv_is_closing(handle)) {
        uv_ref(handle);
    }
    return self;
}

static Item js_http_server_inst_unref(Item maybe_self) {
    Item self = js_http_receiver(maybe_self, "__server__");
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (get_type_id(handle_item) != LMD_TYPE_INT) return self;
    JsHttpServer* srv = (JsHttpServer*)(uintptr_t)it2i(handle_item);
    uv_handle_t* handle = http_server_handle(srv);
    if (srv && handle && !uv_is_closing(handle)) {
        uv_unref(handle);
    }
    return self;
}

static Item js_http_server_inst_setTimeout(Item maybe_self, Item msecs_item, Item callback_item) {
    Item this_obj = js_get_this();
    Item self = js_class_id(this_obj) == JS_CLASS_SERVER ?
        this_obj : js_http_receiver(maybe_self, "__server__");
    Item actual_msecs = self.item == maybe_self.item ? msecs_item : maybe_self;
    Item actual_callback = self.item == maybe_self.item ? callback_item : msecs_item;
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (get_type_id(handle_item) != LMD_TYPE_INT) return self;
    JsHttpServer* srv = (JsHttpServer*)(uintptr_t)it2i(handle_item);
    if (!srv) return self;
    int64_t delay = 0;
    if (get_type_id(actual_msecs) == LMD_TYPE_INT) delay = it2i(actual_msecs);
    if (delay < 0) delay = 0;
    srv->timeout_msecs = delay > 2147483647 ? 2147483647 : (int)delay;
    if (js_http_is_callable(actual_callback)) srv->timeout_callback = actual_callback;
    return self;
}

// http.createServer([options], [requestListener])
extern "C" Item js_http_createServer(Item options_or_handler, Item maybe_handler) {
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        log_error("http: createServer: no event loop");
        return ItemNull;
    }

    Item handler = options_or_handler;
    Item incoming_ctor = make_js_undefined();
    Item response_ctor = make_js_undefined();
    TypeId options_type = get_type_id(options_or_handler);
    if ((options_type == LMD_TYPE_MAP || options_type == LMD_TYPE_OBJECT) &&
        get_type_id(maybe_handler) == LMD_TYPE_FUNC) {
        handler = maybe_handler;
        incoming_ctor = js_property_get(options_or_handler, make_string_item("IncomingMessage"));
        response_ctor = js_property_get(options_or_handler, make_string_item("ServerResponse"));
    }

    JsHttpServer* srv = (JsHttpServer*)mem_calloc(1, sizeof(JsHttpServer), MEM_CAT_JS_RUNTIME);
    srv->request_handler = handler;
    srv->incoming_message_ctor = incoming_ctor;
    srv->server_response_ctor = response_ctor;
    Item reject_body = js_property_get(options_or_handler, make_string_item("rejectNonStandardBodyWrites"));
    srv->reject_nonstandard_body_writes =
        get_type_id(reject_body) == LMD_TYPE_BOOL && it2b(reject_body);

    Item obj = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(obj, JS_CLASS_SERVER);  // A3-T3b
    if (get_type_id(http_server_prototype) == LMD_TYPE_MAP) {
        js_set_prototype(obj, http_server_prototype);
    }
    js_property_set(obj, make_string_item("__server__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)srv)});
    js_property_set(obj, make_string_item("listen"),
                    js_new_function((void*)js_http_server_inst_listen, 4));
    js_property_set(obj, make_string_item("close"),
                    js_new_function((void*)js_http_server_inst_close, 2));
    js_property_set(obj, make_string_item("getConnections"),
                    js_new_function((void*)js_http_server_inst_getConnections, 2));
    js_property_set(obj, make_string_item("on"),
                    js_new_function((void*)js_http_server_inst_on, 3));
    js_property_set(obj, make_string_item("setTimeout"),
                    js_new_function((void*)js_http_server_inst_setTimeout, 3));
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
    uv_pipe_t  pipe;
    Item       js_object;        // the ClientRequest object
    Item       callback;          // response callback
    Item       als_context;
    Item       async_resource;
    Item       response;
    char*      send_buf;
    int        send_len;
    int        send_head_len;
    char*      recv_buf;
    int        recv_len;
    int        recv_cap;
    int        response_hdr_size;
    int        response_delivered;
    char       method[16];
    bool       response_emitted;
    bool       response_ended;
    bool       destroyed;
    bool       sent;
    bool       end_called;
    bool       connected;
    bool       has_content_length;
    bool       content_length_explicit;
    bool       close_after_send;
    bool       request_chunked_body;
    bool       is_pipe;
    bool       abort_handler_set;
    bool       abort_scheduled;
    Item       abort_signal;
    Item       abort_handler;
} JsHttpClientReq;

static void http_client_sync_header_property(JsHttpClientReq* creq, Item req_obj) {
    if (!creq || !creq->send_buf || creq->send_head_len <= 0 || req_obj.item == 0) return;
    js_property_set(req_obj, make_string_item("_header"),
                    make_string_item(creq->send_buf, creq->send_head_len));
}

static void http_client_metadata_ensure(Item req_obj, Item* out_headers, Item* out_raw) {
    Item headers = js_property_get(req_obj, make_string_item("__headers__"));
    if (get_type_id(headers) != LMD_TYPE_MAP) {
        headers = http_new_header_object();
        js_property_set(req_obj, make_string_item("__headers__"), headers);
    }
    Item raw = js_property_get(req_obj, make_string_item("__raw_headers__"));
    if (get_type_id(raw) != LMD_TYPE_ARRAY) {
        raw = js_array_new(0);
        js_property_set(req_obj, make_string_item("__raw_headers__"), raw);
    }
    if (out_headers) *out_headers = headers;
    if (out_raw) *out_raw = raw;
}

static void http_client_metadata_set(Item req_obj, Item raw_name, Item value) {
    if (get_type_id(raw_name) != LMD_TYPE_STRING) return;
    Item headers;
    http_client_metadata_ensure(req_obj, &headers, NULL);
    Item lower = http_lowercase_header_name(raw_name);
    js_property_set(headers, lower, value);
    http_raw_headers_remove_name(req_obj, raw_name);
    Item raw;
    http_client_metadata_ensure(req_obj, NULL, &raw);
    js_array_push(raw, raw_name);
    js_array_push(raw, value);
}

static void http_client_metadata_from_headers(Item req_obj, Item headers_item) {
    if (get_type_id(headers_item) == LMD_TYPE_MAP) {
        Item keys = js_object_keys(headers_item);
        int64_t nkeys = js_array_length(keys);
        for (int64_t i = 0; i < nkeys; i++) {
            Item k = js_array_get_int(keys, i);
            http_client_metadata_set(req_obj, k, js_property_get(headers_item, k));
        }
    } else if (get_type_id(headers_item) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(headers_item);
        for (int64_t i = 0; i < len; i++) {
            Item entry = js_array_get_int(headers_item, i);
            if (get_type_id(entry) == LMD_TYPE_ARRAY && js_array_length(entry) >= 2) {
                http_client_metadata_set(req_obj, js_array_get_int(entry, 0),
                                         js_array_get_int(entry, 1));
            } else if (i + 1 < len) {
                http_client_metadata_set(req_obj, entry, js_array_get_int(headers_item, i + 1));
                i++;
            }
        }
    }
}

static Item js_http_client_getHeaderNames(Item self) {
    return http_headers_get_names(self, false);
}

static Item js_http_client_getRawHeaderNames(Item self) {
    return http_headers_get_names(self, true);
}

static void http_client_emit(Item req_obj, const char* event) {
    char key[64];
    snprintf(key, sizeof(key), "__on_%s__", event);
    Item cb = js_property_get(req_obj, make_string_item(key));
    if (js_http_is_callable(cb)) {
        js_call_function(cb, req_obj, NULL, 0);
        js_microtask_flush();
    }
}

static Item http_client_finish_tick(Item req_obj) {
    js_property_set(req_obj, make_string_item("writableFinished"), (Item){.item = b2it(true)});
    http_client_emit(req_obj, "finish");
    return make_js_undefined();
}

static void http_client_schedule_finish(Item req_obj) {
    Item scheduled = js_property_get(req_obj, make_string_item("__finish_scheduled__"));
    if (get_type_id(scheduled) == LMD_TYPE_BOOL && it2b(scheduled)) return;
    js_property_set(req_obj, make_string_item("__finish_scheduled__"), (Item){.item = b2it(true)});
    Item tick = js_bind_function(js_new_function((void*)http_client_finish_tick, 1),
                                 make_js_undefined(), &req_obj, 1);
    js_next_tick_enqueue(tick);
}

static uv_stream_t* http_client_stream(JsHttpClientReq* creq) {
    if (!creq) return NULL;
    return creq->is_pipe ? (uv_stream_t*)&creq->pipe : (uv_stream_t*)&creq->tcp;
}

static uv_handle_t* http_client_handle(JsHttpClientReq* creq) {
    return (uv_handle_t*)http_client_stream(creq);
}

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

        Item key = make_string_item(lc_name, nlen);
        Item value = make_string_item(vstart, vlen);
        if (nlen == 10 && memcmp(lc_name, "set-cookie", 10) == 0) {
            Item existing = js_property_get(*headers_obj, key);
            if (get_type_id(existing) == LMD_TYPE_ARRAY) {
                js_array_push(existing, value);
            } else {
                Item cookies = js_array_new(0);
                if (get_type_id(existing) == LMD_TYPE_STRING) js_array_push(cookies, existing);
                js_array_push(cookies, value);
                js_property_set(*headers_obj, key, cookies);
            }
        } else {
            js_property_set(*headers_obj, key, value);
        }

        p = line_end + 2;
    }

    return 0;
}

// response.on(event, cb) for client responses
extern "C" Item js_http_client_res_on(Item self2, Item ev2, Item cb2) {
    if (get_type_id(ev2) == LMD_TYPE_STRING && js_http_is_callable(cb2)) {
        String* ev = it2s(ev2);
        if (ev->len == 5 && memcmp(ev->chars, "close", 5) == 0 &&
            http_response_bool_prop(self2, "destroyed") &&
            !http_response_bool_prop(self2, "__close_emitted__")) {
            js_property_set(self2, make_string_item("__close_emitted__"), (Item){.item = b2it(true)});
            js_property_set(self2, make_string_item("closed"), (Item){.item = b2it(true)});
            js_stream_on(self2, ev2, cb2);
            // Client response destroy can happen before close listeners are
            // attached, so late close listeners must observe the pending close.
            js_call_function(cb2, self2, NULL, 0);
            js_microtask_flush();
            return self2;
        }
    }
    return js_stream_on(self2, ev2, cb2);
}

static Item js_http_client_res_inst_on(Item maybe_self, Item event_item, Item callback) {
    Item self = js_http_receiver(maybe_self, "__client__");
    if (self.item == maybe_self.item) {
        return js_http_client_res_on(self, event_item, callback);
    }
    return js_http_client_res_on(self, maybe_self, event_item);
}

static void js_http_close_client_req(JsHttpClientReq* creq);

static Item js_http_client_res_destroy(Item maybe_self, Item err_item) {
    Item self = js_http_receiver(maybe_self, "__client__");
    Item actual_err = self.item == maybe_self.item ? err_item : maybe_self;
    TypeId actual_err_type = get_type_id(actual_err);
    if (actual_err.item != 0 &&
        actual_err_type != LMD_TYPE_UNDEFINED &&
        actual_err_type != LMD_TYPE_NULL) {
        js_property_set(self, make_string_item("errored"), actual_err);
    }
    js_stream_destroy(self, make_js_undefined());
    if (actual_err.item != 0 &&
        actual_err_type != LMD_TYPE_UNDEFINED &&
        actual_err_type != LMD_TYPE_NULL) {
        // Client IncomingMessage.destroy(err) surfaces the error to process;
        // otherwise callers waiting in uncaughtException keep the server open.
        js_process_emit(make_string_item("uncaughtException"), actual_err);
        js_microtask_flush();
    }
    Item handle_item = js_property_get(self, make_string_item("__client__"));
    if (get_type_id(handle_item) == LMD_TYPE_INT) {
        JsHttpClientReq* creq = (JsHttpClientReq*)(uintptr_t)it2i(handle_item);
        if (creq && !creq->destroyed) {
            creq->destroyed = true;
            js_property_set(creq->js_object, make_string_item("destroyed"), (Item){.item = b2it(true)});
            js_http_close_client_req(creq);
        }
    }
    return self;
}

static Item js_http_client_res_inst_destroy(Item maybe_self, Item err_item) {
    Item self = js_http_receiver(maybe_self, "__client__");
    return js_http_client_res_destroy(self, self.item == maybe_self.item ? err_item : maybe_self);
}

static Item js_http_econnreset_error(Item err) {
    Item result = err;
    if (get_type_id(result) == LMD_TYPE_UNDEFINED || get_type_id(result) == LMD_TYPE_NULL || result.item == 0) {
        result = js_new_error(make_string_item("socket hang up"));
    }
    js_property_set(result, make_string_item("code"), make_string_item("ECONNRESET"));
    return result;
}

static bool js_http_signal_is_aborted(Item signal) {
    TypeId type = get_type_id(signal);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_OBJECT && type != LMD_TYPE_VMAP) return false;
    Item aborted = js_property_get(signal, make_string_item("aborted"));
    return get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted);
}

static Item js_http_make_abort_error(void) {
    Item err = js_new_error(make_string_item("The operation was aborted"));
    js_property_set(err, make_string_item("name"), make_string_item("AbortError"));
    js_property_set(err, make_string_item("code"), make_string_item("ABORT_ERR"));
    return err;
}

static Item js_http_abort_reason(JsHttpClientReq* creq) {
    if (creq && creq->abort_signal.item) {
        Item reason = js_property_get(creq->abort_signal, make_string_item("reason"));
        TypeId type = get_type_id(reason);
        if (reason.item != 0 && type != LMD_TYPE_UNDEFINED && type != LMD_TYPE_NULL) return reason;
    }
    return js_http_make_abort_error();
}

static void js_http_client_remove_abort_listener(JsHttpClientReq* creq) {
    if (!creq || !creq->abort_handler_set) return;
    Item remove_fn = js_property_get(creq->abort_signal, make_string_item("removeEventListener"));
    if (get_type_id(remove_fn) == LMD_TYPE_FUNC) {
        Item args[2] = { make_string_item("abort"), creq->abort_handler };
        js_call_function(remove_fn, creq->abort_signal, args, 2);
        js_microtask_flush();
    }
    creq->abort_handler_set = false;
    creq->abort_signal = make_js_undefined();
    creq->abort_handler = make_js_undefined();
}

static Item js_http_client_abort_scheduled(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item req_obj = env[0];
    Item handle_item = js_property_get(req_obj, make_string_item("__client__"));
    if (get_type_id(handle_item) != LMD_TYPE_INT) return make_js_undefined();
    JsHttpClientReq* creq = (JsHttpClientReq*)(uintptr_t)it2i(handle_item);
    if (!creq || creq->destroyed) return make_js_undefined();

    creq->abort_scheduled = false;
    Item err = js_http_abort_reason(creq);
    js_http_client_remove_abort_listener(creq);
    creq->destroyed = true;
    js_property_set(req_obj, make_string_item("destroyed"), (Item){.item = b2it(true)});

    Item on_err = js_property_get(req_obj, make_string_item("__on_error__"));
    if (get_type_id(on_err) == LMD_TYPE_FUNC) {
        js_call_function(on_err, req_obj, &err, 1);
        js_microtask_flush();
    }
    js_http_close_client_req(creq);
    return make_js_undefined();
}

static void js_http_client_schedule_abort(JsHttpClientReq* creq) {
    if (!creq || creq->destroyed || creq->abort_scheduled) return;
    creq->abort_scheduled = true;
    Item* env = js_alloc_env(1);
    env[0] = creq->js_object;
    Item fn = js_new_closure((void*)js_http_client_abort_scheduled, 0, env, 1);
    js_next_tick_enqueue(fn);
}

static Item js_http_client_abort_signal_event(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item req_obj = env[0];
    Item handle_item = js_property_get(req_obj, make_string_item("__client__"));
    if (get_type_id(handle_item) == LMD_TYPE_INT) {
        JsHttpClientReq* creq = (JsHttpClientReq*)(uintptr_t)it2i(handle_item);
        js_http_client_schedule_abort(creq);
    }
    return make_js_undefined();
}

static bool js_http_client_configure_abort_signal(JsHttpClientReq* creq, Item signal) {
    if (!creq || !creq->js_object.item) return false;
    TypeId type = get_type_id(signal);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_OBJECT && type != LMD_TYPE_VMAP) return false;

    creq->abort_signal = signal;
    if (js_http_signal_is_aborted(signal)) {
        js_http_client_schedule_abort(creq);
        return true;
    }

    Item add_fn = js_property_get(signal, make_string_item("addEventListener"));
    if (get_type_id(add_fn) != LMD_TYPE_FUNC) return false;

    Item* env = js_alloc_env(1);
    env[0] = creq->js_object;
    Item handler = js_new_closure((void*)js_http_client_abort_signal_event, 1, env, 1);
    Item args[2] = { make_string_item("abort"), handler };
    js_call_function(add_fn, signal, args, 2);
    js_microtask_flush();
    creq->abort_handler = handler;
    creq->abort_handler_set = true;
    return false;
}

static void js_http_free_client_req(JsHttpClientReq* creq) {
    if (!creq) return;
    js_async_hooks_emit_destroy_resource(creq->async_resource);
    if (creq->send_buf) mem_free(creq->send_buf);
    if (creq->recv_buf) mem_free(creq->recv_buf);
    mem_free(creq);
}

static void js_http_close_client_req(JsHttpClientReq* creq) {
    if (!creq) return;
    js_http_client_remove_abort_listener(creq);
    if (creq->js_object.item != 0) {
        js_property_set(creq->js_object, make_string_item("__client__"), ItemNull);
    }
    uv_handle_t* handle = http_client_handle(creq);
    if (handle && !uv_is_closing(handle)) {
        uv_close(handle, [](uv_handle_t* h) {
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

static int js_http_response_content_length(Item headers) {
    if (get_type_id(headers) != LMD_TYPE_MAP) return -1;
    Item value = js_property_get(headers, make_string_item("content-length"));
    if (get_type_id(value) != LMD_TYPE_STRING) return -1;
    String* s = it2s(value);
    if (!s || s->len <= 0) return -1;
    int result = 0;
    for (size_t i = 0; i < s->len; i++) {
        char c = s->chars[i];
        if (c < '0' || c > '9') return -1;
        result = result * 10 + (c - '0');
    }
    return result;
}

static bool js_http_response_has_header_token(Item headers, const char* name, const char* token) {
    if (get_type_id(headers) != LMD_TYPE_MAP) return false;
    Item value = js_property_get(headers, make_string_item(name));
    if (get_type_id(value) != LMD_TYPE_STRING) return false;
    String* s = it2s(value);
    if (!s) return false;
    char buf[512];
    int len = (int)s->len < (int)sizeof(buf) - 1 ? (int)s->len : (int)sizeof(buf) - 1;
    memcpy(buf, s->chars, (size_t)len);
    buf[len] = '\0';
    return http_header_has_token(buf, token);
}

static int http_decode_chunked_body(const char* data, int len, char* out,
                                    int* consumed, bool* complete) {
    int pos = 0;
    int out_len = 0;
    if (consumed) *consumed = 0;
    if (complete) *complete = false;
    while (pos < len) {
        int line_start = pos;
        int line_end = -1;
        for (int i = pos; i + 1 < len; i++) {
            if (data[i] == '\r' && data[i + 1] == '\n') {
                line_end = i;
                break;
            }
        }
        if (line_end < 0) return out_len;
        int size = 0;
        bool saw_digit = false;
        for (int i = line_start; i < line_end; i++) {
            if (data[i] == ';') break;
            int v = http_chunk_hex_value(data[i]);
            if (v < 0) return out_len;
            saw_digit = true;
            size = size * 16 + v;
        }
        if (!saw_digit) return out_len;
        pos = line_end + 2;
        if (size == 0) {
            if (pos + 1 >= len) return out_len;
            if (data[pos] != '\r' || data[pos + 1] != '\n') return out_len;
            pos += 2;
            if (consumed) *consumed = pos;
            if (complete) *complete = true;
            return out_len;
        }
        if (pos + size + 2 > len) return out_len;
        if (out && size > 0) memcpy(out + out_len, data + pos, (size_t)size);
        out_len += size;
        pos += size;
        if (data[pos] != '\r' || data[pos + 1] != '\n') return out_len;
        pos += 2;
    }
    return out_len;
}

static void js_http_client_finish_response_if_complete(JsHttpClientReq* creq, Item res,
                                                       Item headers, int body_len) {
    if (!creq || creq->response_ended) return;
    int content_length = js_http_response_content_length(headers);
    Item status_item = js_property_get(res, make_string_item("statusCode"));
    int status = get_type_id(status_item) == LMD_TYPE_INT ? (int)it2i(status_item) : 0;
    if (strcmp(creq->method, "HEAD") == 0 || status == 204 || status == 304) content_length = 0;
    if (content_length < 0 || body_len < content_length) return;
    js_readable_push(res, ItemNull);
    creq->response_ended = true;
    creq->destroyed = true;
    js_property_set(creq->js_object, make_string_item("destroyed"), (Item){.item = b2it(true)});
    js_http_close_client_req(creq);
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

    if (status_code == 100) {
        int remaining = creq->recv_len - hdr_size;
        if (remaining > 0) memmove(creq->recv_buf, creq->recv_buf + hdr_size, (size_t)remaining);
        creq->recv_len = remaining;
        http_client_emit(creq->js_object, "continue");
        return js_http_client_try_emit_response(creq);
    }

    Item res = js_readable_new(ItemNull);
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(res, JS_CLASS_INCOMING_MESSAGE);  // A3-T3b
    js_property_set(res, make_string_item("__client__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)creq)});
    js_property_set(res, make_string_item("statusCode"), (Item){.item = i2it(status_code)});
    js_property_set(res, make_string_item("statusMessage"), status_message);
    js_property_set(res, make_string_item("headers"), headers);
    js_property_set(res, make_string_item("rawHeaders"), raw_headers);
    js_property_set(res, make_string_item("destroy"),
                    js_new_function((void*)js_http_client_res_inst_destroy, 2));
    js_property_set(res, make_string_item("on"),
                    js_new_function((void*)js_http_client_res_inst_on, 3));
    js_property_set(res, make_string_item("once"),
                    js_new_function((void*)js_http_client_res_inst_on, 3));

    int body_len = creq->recv_len - hdr_size;
    bool chunked = strcmp(creq->method, "HEAD") != 0 &&
        js_http_response_has_header_token(headers, "transfer-encoding", "chunked");
    bool chunked_complete = false;
    char* decoded_body = NULL;
    int decoded_body_len = 0;
    if (chunked && body_len > 0) {
        decoded_body = (char*)mem_alloc(body_len + 1, MEM_CAT_JS_RUNTIME);
        int chunked_consumed = 0;
        decoded_body_len = http_decode_chunked_body(creq->recv_buf + hdr_size, body_len,
                                                    decoded_body, &chunked_consumed,
                                                    &chunked_complete);
        decoded_body[decoded_body_len] = '\0';
    }
    int emitted_body_len = chunked ? decoded_body_len : body_len;
    if (emitted_body_len > 0) {
        Item body = chunked ?
            make_string_item(decoded_body, emitted_body_len) :
            make_string_item(creq->recv_buf + hdr_size, emitted_body_len);
        Item body_chunks = js_array_new(0);
        js_array_push(body_chunks, body);
        js_property_set(res, make_string_item("__chunks__"), body_chunks);
        js_property_set(res, make_string_item("body"), body);
        js_readable_push(res, body);
    }
    if (decoded_body) mem_free(decoded_body);

    creq->response = res;
    creq->response_hdr_size = hdr_size;
    creq->response_delivered = creq->recv_len;
    js_http_emit_client_response(creq, res);
    if (chunked) {
        if (chunked_complete) {
            js_readable_push(res, ItemNull);
            creq->response_ended = true;
            creq->destroyed = true;
            js_property_set(creq->js_object, make_string_item("destroyed"), (Item){.item = b2it(true)});
            js_http_close_client_req(creq);
        }
    } else {
        js_http_client_finish_response_if_complete(creq, res, headers, body_len);
    }
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
                Item headers = js_property_get(creq->response, make_string_item("headers"));
                js_http_client_finish_response_if_complete(creq, creq->response, headers,
                                                           creq->recv_len - creq->response_hdr_size);
            }
        } else {
            js_http_client_try_emit_response(creq);
        }
    }

    if (buf->base) mem_free(buf->base);

    if (nread < 0) {
        // connection ended — parse response and emit
        if (creq->response.item != 0) {
            if (!creq->response_ended) {
                js_readable_push(creq->response, ItemNull);
                creq->response_ended = true;
            }
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
                js_property_set(res, make_string_item("__client__"),
                                (Item){.item = i2it((int64_t)(uintptr_t)creq)});
                js_property_set(res, make_string_item("statusCode"), (Item){.item = i2it(status_code)});
                js_property_set(res, make_string_item("statusMessage"), status_message);
                js_property_set(res, make_string_item("headers"), headers);
                js_property_set(res, make_string_item("rawHeaders"), raw_headers);
                js_property_set(res, make_string_item("destroy"),
                                js_new_function((void*)js_http_client_res_inst_destroy, 2));
                js_property_set(res, make_string_item("on"),
                                js_new_function((void*)js_http_client_res_inst_on, 3));
                js_property_set(res, make_string_item("once"),
                                js_new_function((void*)js_http_client_res_inst_on, 3));

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
                creq->response_ended = true;

                creq->response = res;
                js_http_emit_client_response(creq, res);
            }
        } else {
            // A closed socket before response headers is a request failure,
            // not a quiet EOF; server-side req.destroy() relies on this.
            Item err = js_http_econnreset_error(make_js_undefined());
            Item on_err = js_property_get(creq->js_object, make_string_item("__on_error__"));
            if (get_type_id(on_err) == LMD_TYPE_FUNC) {
                js_call_function(on_err, creq->js_object, &err, 1);
                js_microtask_flush();
            }
        }

        // cleanup
        creq->destroyed = true;
        if (creq->recv_buf) mem_free(creq->recv_buf);
        creq->recv_buf = NULL;
        uv_close((uv_handle_t*)stream, [](uv_handle_t* h) {
            JsHttpClientReq* c = (JsHttpClientReq*)h->data;
            js_http_free_client_req(c);
        });
    }
}

typedef struct HttpClientWriteReq {
    char* data;
    JsHttpClientReq* creq;
    Item  callback;
} HttpClientWriteReq;

static void http_client_write_cb(uv_write_t* req, int status) {
    HttpClientWriteReq* write_req = (HttpClientWriteReq*)req->data;
    JsHttpClientReq* creq = write_req ? write_req->creq : NULL;
    if (write_req) {
        http_call_write_callback(write_req->callback);
        if (write_req->data) mem_free(write_req->data);
        mem_free(write_req);
    }
    if (creq && creq->close_after_send && !creq->destroyed) {
        (void)status;
        creq->destroyed = true;
        js_property_set(creq->js_object, make_string_item("destroyed"), (Item){.item = b2it(true)});
        js_http_close_client_req(creq);
    }
    mem_free(req);
}

static void http_client_write_bytes(JsHttpClientReq* creq, const char* data, int len, Item callback) {
    if (!creq || !data || len <= 0 || creq->destroyed || !creq->connected) {
        http_call_write_callback(callback);
        return;
    }
    char* copy = (char*)mem_alloc(len, MEM_CAT_JS_RUNTIME);
    memcpy(copy, data, (size_t)len);
    uv_buf_t buf = uv_buf_init(copy, (unsigned int)len);
    uv_write_t* wreq = (uv_write_t*)mem_calloc(1, sizeof(uv_write_t), MEM_CAT_JS_RUNTIME);
    HttpClientWriteReq* write_req =
        (HttpClientWriteReq*)mem_calloc(1, sizeof(HttpClientWriteReq), MEM_CAT_JS_RUNTIME);
    write_req->data = copy;
    write_req->creq = creq;
    write_req->callback = callback;
    wreq->data = write_req;
    uv_write(wreq, http_client_stream(creq), &buf, 1, http_client_write_cb);
}

static void http_client_write_chunk(JsHttpClientReq* creq, String* chunk, Item callback) {
    if (!creq || !chunk || chunk->len == 0) {
        http_call_write_callback(callback);
        return;
    }
    http_client_write_bytes(creq, chunk->chars, (int)chunk->len, callback);
}

static void http_client_write_chunked_body(JsHttpClientReq* creq, Item chunk_item, Item callback) {
    const char* data = NULL;
    int len = 0;
    if (!http_item_bytes(chunk_item, &data, &len) || len <= 0) {
        http_call_write_callback(callback);
        return;
    }
    int frame_cap = len + 32;
    char* frame = (char*)mem_alloc(frame_cap, MEM_CAT_JS_RUNTIME);
    int pos = snprintf(frame, frame_cap, "%X\r\n", (unsigned int)len);
    memcpy(frame + pos, data, (size_t)len);
    pos += len;
    memcpy(frame + pos, "\r\n", 2);
    pos += 2;
    // Expect/continue requests without a known length use chunk framing; the
    // server parser only recognizes streamed bodies when each write is framed.
    http_client_write_bytes(creq, frame, pos, callback);
    mem_free(frame);
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
    int value_start = -1;
    int value_end = -1;
    int declared_content_length = -1;
    const char* p = creq->send_buf;
    const char* end = creq->send_buf + creq->send_head_len;
    while (p < end) {
        const char* line_end = (const char*)memchr(p, '\n', (size_t)(end - p));
        if (!line_end) line_end = end;
        const char* colon = (const char*)memchr(p, ':', (size_t)(line_end - p));
        if (colon && http_token_equals_ci(p, (int)(colon - p), "content-length")) {
            const char* value = colon + 1;
            while (value < line_end && (*value == ' ' || *value == '\t')) value++;
            const char* ve = line_end;
            while (ve > value && (ve[-1] == '\r' || ve[-1] == '\n')) ve--;
            value_start = (int)(value - creq->send_buf);
            value_end = (int)(ve - creq->send_buf);
            declared_content_length = 0;
            for (const char* cp = value; cp < ve; cp++) {
                if (*cp < '0' || *cp > '9') {
                    declared_content_length = -1;
                    break;
                }
                declared_content_length = declared_content_length * 10 + (*cp - '0');
            }
            break;
        }
        p = line_end < end ? line_end + 1 : end;
    }

    char len_buf[32];
    int len_buf_len = snprintf(len_buf, sizeof(len_buf), "%d", body_len);
    int new_head_len = creq->send_head_len;
    bool update_content_length = !creq->content_length_explicit;
    if (creq->content_length_explicit && declared_content_length >= 0 &&
        body_len < declared_content_length) {
        creq->close_after_send = true;
    }
    if (update_content_length && value_start >= 0 && value_end >= value_start && len_buf_len > 0) {
        new_head_len = creq->send_head_len - (value_end - value_start) + len_buf_len;
    }
    int new_len = new_head_len + body_len;
    char* new_buf = (char*)mem_alloc(new_len, MEM_CAT_JS_RUNTIME);
    if (update_content_length && value_start >= 0 && value_end >= value_start && len_buf_len > 0) {
        memcpy(new_buf, creq->send_buf, (size_t)value_start);
        memcpy(new_buf + value_start, len_buf, (size_t)len_buf_len);
        memcpy(new_buf + value_start + len_buf_len, creq->send_buf + value_end,
               (size_t)(creq->send_head_len - value_end));
        creq->send_head_len = new_head_len;
    } else {
        memcpy(new_buf, creq->send_buf, (size_t)creq->send_head_len);
    }
    if (body_len > 0) {
        memcpy(new_buf + creq->send_head_len, bs->chars, (size_t)body_len);
    }
    mem_free(creq->send_buf);
    creq->send_buf = new_buf;
    creq->send_len = new_len;
    http_client_sync_header_property(creq, req_obj);
}

static bool http_client_pending_append_chunk(JsHttpClientReq* creq, Item req_obj,
                                             const char* data, int len, bool final_chunk) {
    if (!creq || !creq->send_buf || len < 0) return false;
    char frame_head[32];
    int frame_head_len = final_chunk ? 0 :
        snprintf(frame_head, sizeof(frame_head), "%X\r\n", (unsigned int)len);
    const char* final = "0\r\n\r\n";
    int final_len = final_chunk ? 5 : 0;
    int frame_len = final_chunk ? final_len : frame_head_len + len + 2;
    char* next = (char*)mem_alloc(creq->send_len + frame_len, MEM_CAT_JS_RUNTIME);
    memcpy(next, creq->send_buf, (size_t)creq->send_len);
    int pos = creq->send_len;
    if (final_chunk) {
        memcpy(next + pos, final, (size_t)final_len);
        pos += final_len;
    } else {
        memcpy(next + pos, frame_head, (size_t)frame_head_len);
        pos += frame_head_len;
        if (len > 0) {
            memcpy(next + pos, data, (size_t)len);
            pos += len;
        }
        memcpy(next + pos, "\r\n", 2);
        pos += 2;
    }
    mem_free(creq->send_buf);
    creq->send_buf = next;
    creq->send_len = pos;
    http_client_sync_header_property(creq, req_obj);
    return true;
}

static bool http_client_convert_pending_body_to_chunked(JsHttpClientReq* creq, Item req_obj,
                                                        const char* data, int len) {
    if (!creq || !creq->send_buf || creq->sent || creq->content_length_explicit) return false;
    int line_start = -1;
    int line_end = -1;
    const char* p = creq->send_buf;
    const char* end = creq->send_buf + creq->send_head_len;
    while (p < end) {
        const char* line = p;
        const char* nl = (const char*)memchr(p, '\n', (size_t)(end - p));
        if (!nl) break;
        const char* colon = (const char*)memchr(line, ':', (size_t)(nl - line));
        if (colon && http_token_equals_ci(line, (int)(colon - line), "content-length")) {
            line_start = (int)(line - creq->send_buf);
            line_end = (int)(nl - creq->send_buf) + 1;
            break;
        }
        p = nl + 1;
    }
    const char* te = "Transfer-Encoding: chunked\r\n";
    int te_len = (int)strlen(te);
    int remove_len = line_start >= 0 ? line_end - line_start : 0;
    int insert_at = line_start >= 0 ? line_start : creq->send_head_len;
    if (line_start < 0 && insert_at >= 2 &&
        creq->send_buf[insert_at - 2] == '\r' &&
        creq->send_buf[insert_at - 1] == '\n') {
        insert_at -= 2;
    }
    int new_head_len = creq->send_head_len - remove_len + te_len;
    char* next = (char*)mem_alloc(new_head_len, MEM_CAT_JS_RUNTIME);
    memcpy(next, creq->send_buf, (size_t)insert_at);
    memcpy(next + insert_at, te, (size_t)te_len);
    int tail_start = line_start >= 0 ? line_end : insert_at;
    int tail_len = creq->send_head_len - tail_start;
    if (tail_len > 0) memcpy(next + insert_at + te_len, creq->send_buf + tail_start, (size_t)tail_len);
    mem_free(creq->send_buf);
    creq->send_buf = next;
    creq->send_head_len = new_head_len;
    creq->send_len = new_head_len;
    creq->has_content_length = false;
    creq->request_chunked_body = true;
    // Writes before end() must not synthesize a complete Content-Length body;
    // chunked framing leaves the request open until ClientRequest.end().
    return http_client_pending_append_chunk(creq, req_obj, data, len, false);
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
            creq->destroyed = true;
            js_property_set(creq->js_object, make_string_item("destroyed"), (Item){.item = b2it(true)});
            js_http_close_client_req(creq);
        }
        return;
    }

    creq->connected = true;

    // send the HTTP request
    if (creq->send_buf && creq->send_len > 0) {
        uv_buf_t buf = uv_buf_init(creq->send_buf, (unsigned int)creq->send_len);
        uv_write_t* wreq = (uv_write_t*)mem_calloc(1, sizeof(uv_write_t), MEM_CAT_JS_RUNTIME);
        HttpClientWriteReq* write_req =
            (HttpClientWriteReq*)mem_calloc(1, sizeof(HttpClientWriteReq), MEM_CAT_JS_RUNTIME);
        write_req->data = creq->send_buf;
        write_req->creq = creq;
        write_req->callback = make_js_undefined();
        wreq->data = write_req;
        creq->send_buf = NULL; // ownership transferred
        creq->sent = true;
        uv_write(wreq, http_client_stream(creq), &buf, 1, http_client_write_cb);
    }

    // start reading response
    uv_read_start(http_client_stream(creq), http_client_alloc_cb, http_client_read_cb);
}

static Item http_client_write_ex(Item self, Item data_item, Item encoding_item, Item callback_item) {
    if (js_http_is_callable(encoding_item)) {
        callback_item = encoding_item;
        encoding_item = make_js_undefined();
    }
    // store body data (will be sent on end())
    Item body = js_property_get(self, make_string_item("__req_body__"));
    if (get_type_id(body) != LMD_TYPE_STRING) body = make_string_item("", 0);

    Item encoded_item = http_encode_write_chunk(data_item, encoding_item);
    const char* chunk_data = NULL;
    int chunk_len = 0;
    if (http_item_bytes(encoded_item, &chunk_data, &chunk_len)) {
        String* existing = it2s(body);
        int new_len = (int)existing->len + chunk_len;
        char* buf = (char*)mem_alloc(new_len + 1, MEM_CAT_JS_RUNTIME);
        memcpy(buf, existing->chars, existing->len);
        if (chunk_len > 0) memcpy(buf + existing->len, chunk_data, (size_t)chunk_len);
        buf[new_len] = '\0';
        js_property_set(self, make_string_item("__req_body__"), make_string_item(buf, new_len));
        mem_free(buf);

        Item handle_item = js_property_get(self, make_string_item("__client__"));
        if (handle_item.item != 0) {
            JsHttpClientReq* creq = (JsHttpClientReq*)(uintptr_t)it2i(handle_item);
            if (creq) {
                if (creq->sent) {
                    if (creq->request_chunked_body) {
                        http_client_write_chunked_body(creq, make_string_item(chunk_data, chunk_len), callback_item);
                    } else if (creq->has_content_length) {
                        Item chunk_item = make_string_item(chunk_data, chunk_len);
                        http_client_write_chunk(creq, it2s(chunk_item), callback_item);
                    } else {
                        http_call_write_callback(callback_item);
                    }
                } else {
                    if (!creq->content_length_explicit) {
                        if (creq->request_chunked_body) {
                            http_client_pending_append_chunk(creq, self, chunk_data, chunk_len, false);
                        } else {
                            http_client_convert_pending_body_to_chunked(creq, self, chunk_data, chunk_len);
                        }
                    } else if (creq->has_content_length) {
                        http_client_update_pending_body(creq, self);
                    }
                    http_call_write_callback(callback_item);
                }
            } else {
                http_call_write_callback(callback_item);
            }
        }
    } else {
        http_call_write_callback(callback_item);
    }
    return self;
}

// ClientRequest.write(data) — for request body
extern "C" Item js_http_client_write(Item self, Item data_item) {
    return http_client_write_ex(self, data_item, make_js_undefined(), make_js_undefined());
}

static Item http_client_end_ex(Item self, Item data_item, Item encoding_item, Item callback_item) {
    if (js_http_is_callable(data_item)) {
        callback_item = data_item;
        data_item = make_js_undefined();
        encoding_item = make_js_undefined();
    } else if (js_http_is_callable(encoding_item)) {
        callback_item = encoding_item;
        encoding_item = make_js_undefined();
    }
    Item ended = js_property_get(self, make_string_item("writableEnded"));
    if (get_type_id(ended) == LMD_TYPE_BOOL && it2b(ended)) {
        return self;
    }
    TypeId data_type = get_type_id(data_item);
    if (data_item.item != 0 && data_item.item != ITEM_NULL &&
        data_type != LMD_TYPE_UNDEFINED && data_type != LMD_TYPE_NULL) {
        http_client_write_ex(self, data_item, encoding_item, make_js_undefined());
    }
    js_property_set(self, make_string_item("writableEnded"), (Item){.item = b2it(true)});
    Item handle_item = js_property_get(self, make_string_item("__client__"));
    if (handle_item.item != 0) {
        JsHttpClientReq* creq = (JsHttpClientReq*)(uintptr_t)it2i(handle_item);
        if (creq) creq->end_called = true;
        if (creq && creq->sent && creq->request_chunked_body) {
            http_client_write_bytes(creq, "0\r\n\r\n", 5, callback_item);
            http_client_schedule_finish(self);
            return self;
        }
        if (creq && !creq->sent && creq->request_chunked_body && creq->send_buf) {
            int new_len = creq->send_len + 5;
            char* new_buf = (char*)mem_alloc(new_len, MEM_CAT_JS_RUNTIME);
            memcpy(new_buf, creq->send_buf, (size_t)creq->send_len);
            memcpy(new_buf + creq->send_len, "0\r\n\r\n", 5);
            mem_free(creq->send_buf);
            creq->send_buf = new_buf;
            creq->send_len = new_len;
        }
        if (creq && !creq->sent && creq->send_buf) {
            Item body = js_property_get(self, make_string_item("__req_body__"));
            if (get_type_id(body) == LMD_TYPE_STRING) {
                String* bs = it2s(body);
                if (bs->len > 0 && creq->request_chunked_body) {
                    // pending writes are already chunk-framed; rewriting them
                    // as Content-Length here would make an unfinished request complete.
                } else if (bs->len > 0 && creq->has_content_length) {
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
                    http_client_sync_header_property(creq, self);
                }
            }
        }
    }
    // the actual send happens in connect_cb; for simplicity we pre-build
    // the full request at creation time. end() is a no-op for GET requests.
    http_client_schedule_finish(self);
    http_call_write_callback(callback_item);
    return self;
}

// ClientRequest.end([data]) — finalize request
extern "C" Item js_http_client_end(Item self, Item data_item) {
    return http_client_end_ex(self, data_item, make_js_undefined(), make_js_undefined());
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
    js_http_client_remove_abort_listener(creq);
    Item err = js_http_econnreset_error(err_item);
    Item on_err = js_property_get(self, make_string_item("__on_error__"));
    if (get_type_id(on_err) == LMD_TYPE_FUNC) {
        js_call_function(on_err, self, &err, 1);
    }
    if (creq->response.item != 0) {
        js_stream_destroy(creq->response, err);
    }
    uv_handle_t* handle = http_client_handle(creq);
    if (handle && !uv_is_closing(handle)) {
        uv_close(handle, [](uv_handle_t* h) {
            JsHttpClientReq* c = (JsHttpClientReq*)h->data;
            js_http_free_client_req(c);
        });
    }
    return self;
}

static int http_client_append_header_line(char* req_str, int rlen, int cap,
                                          Item name_item, Item value_item,
                                          bool* has_content_length);

static void http_client_insert_pending_header(JsHttpClientReq* creq, const char* line, int line_len) {
    if (!creq || creq->sent || !creq->send_buf || line_len <= 0) return;
    int insert_at = creq->send_head_len;
    if (insert_at >= 2 &&
        creq->send_buf[insert_at - 2] == '\r' &&
        creq->send_buf[insert_at - 1] == '\n') {
        insert_at -= 2;
    } else if (creq->send_len >= 2 &&
               creq->send_buf[creq->send_len - 2] == '\r' &&
               creq->send_buf[creq->send_len - 1] == '\n') {
        insert_at = creq->send_len - 2;
    }

    int new_len = creq->send_len + line_len;
    char* next = (char*)mem_alloc(new_len, MEM_CAT_JS_RUNTIME);
    memcpy(next, creq->send_buf, (size_t)insert_at);
    memcpy(next + insert_at, line, (size_t)line_len);
    memcpy(next + insert_at + line_len, creq->send_buf + insert_at,
           (size_t)(creq->send_len - insert_at));
    mem_free(creq->send_buf);
    creq->send_buf = next;
    creq->send_len = new_len;
    creq->send_head_len += line_len;
    http_client_sync_header_property(creq, creq->js_object);
}

extern "C" Item js_http_client_setHeader(Item self, Item name_item, Item value_item) {
    Item name = http_validate_header_name(name_item);
    if (name.item == 0) return self;
    Item value = http_validate_header_value(name, value_item);
    if (value.item == 0) return self;

    Item handle_item = js_property_get(self, make_string_item("__client__"));
    if (handle_item.item == 0) return self;
    JsHttpClientReq* creq = (JsHttpClientReq*)(uintptr_t)it2i(handle_item);
    if (!creq || creq->sent) return self;

    char lines[4096];
    bool has_content_length = creq->has_content_length;
    int len = http_client_append_header_line(lines, 0, (int)sizeof(lines), name, value,
                                             &has_content_length);
    if (has_content_length) {
        creq->has_content_length = true;
        creq->content_length_explicit = true;
    }
    http_client_insert_pending_header(creq, lines, len);
    http_client_metadata_set(self, name, value_item);
    return self;
}

static Item js_http_client_inst_write(Item maybe_self, Item data_item, Item encoding_or_callback) {
    Item self = js_http_receiver(maybe_self, "__client__");
    if (self.item == maybe_self.item) {
        return http_client_write_ex(self, data_item, encoding_or_callback, make_js_undefined());
    }
    return http_client_write_ex(self, maybe_self, data_item, encoding_or_callback);
}

static Item js_http_client_inst_end(Item maybe_self, Item data_item, Item encoding_or_callback) {
    Item self = js_http_receiver(maybe_self, "__client__");
    if (self.item == maybe_self.item) {
        return http_client_end_ex(self, data_item, make_js_undefined(), encoding_or_callback);
    }
    return http_client_end_ex(self, maybe_self, data_item, encoding_or_callback);
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

static Item js_http_client_inst_setHeader(Item maybe_self, Item name_item, Item value_item) {
    Item self = js_http_receiver(maybe_self, "__client__");
    if (self.item == maybe_self.item) {
        return js_http_client_setHeader(self, name_item, value_item);
    }
    return js_http_client_setHeader(self, maybe_self, name_item);
}

static Item js_http_client_inst_getHeaderNames(Item maybe_self) {
    return js_http_client_getHeaderNames(js_http_receiver(maybe_self, "__client__"));
}

static Item js_http_client_inst_getRawHeaderNames(Item maybe_self) {
    return js_http_client_getRawHeaderNames(js_http_receiver(maybe_self, "__client__"));
}

static Item http_client_make_request_object(JsHttpClientReq* creq,
                                            const char* method,
                                            const char* path) {
    Item obj = js_new_object();
    js_class_stamp(obj, JS_CLASS_CLIENT_REQUEST);
    js_property_set(obj, make_string_item("__client_request__"), (Item){.item = b2it(true)});
    if (creq) {
        js_property_set(obj, make_string_item("__client__"),
                        (Item){.item = i2it((int64_t)(uintptr_t)creq)});
    }
    js_property_set(obj, make_string_item("path"),
                    make_string_item((path && path[0]) ? path : "/"));
    js_property_set(obj, make_string_item("method"),
                    make_string_item((method && method[0]) ? method : "GET"));
    js_property_set(obj, make_string_item("write"),
                    js_new_function((void*)js_http_client_inst_write, 3));
    js_property_set(obj, make_string_item("end"),
                    js_new_function((void*)js_http_client_inst_end, 3));
    js_property_set(obj, make_string_item("on"),
                    js_new_function((void*)js_http_client_inst_on, 3));
    js_property_set(obj, make_string_item("once"),
                    js_new_function((void*)js_http_client_inst_on, 3));
    js_property_set(obj, make_string_item("setHeader"),
                    js_new_function((void*)js_http_client_inst_setHeader, 3));
    js_property_set(obj, make_string_item("getHeaderNames"),
                    js_new_function((void*)js_http_client_inst_getHeaderNames, 1));
    js_property_set(obj, make_string_item("getRawHeaderNames"),
                    js_new_function((void*)js_http_client_inst_getRawHeaderNames, 1));
    js_property_set(obj, make_string_item("destroy"),
                    js_new_function((void*)js_http_client_inst_destroy, 2));
    js_property_set(obj, make_string_item("destroyed"), (Item){.item = b2it(false)});
    js_property_set(obj, make_string_item("writable"), (Item){.item = b2it(true)});
    js_property_set(obj, make_string_item("writableEnded"), (Item){.item = b2it(false)});
    js_property_set(obj, make_string_item("writableFinished"), (Item){.item = b2it(false)});
    js_property_set(obj, make_string_item("__finish_scheduled__"), (Item){.item = b2it(false)});
    return obj;
}

static bool http_client_validate_header_pair(Item name_item, Item value_item) {
    if (get_type_id(name_item) != LMD_TYPE_STRING) return true;
    String* name = it2s(name_item);
    if (http_header_name_equals(name, "host", 4) &&
        get_type_id(value_item) == LMD_TYPE_ARRAY) {
        js_throw_invalid_arg_type("options.headers.host", "string", value_item);
        return false;
    }
    return true;
}

static bool http_client_validate_headers(Item headers_item) {
    if (get_type_id(headers_item) == LMD_TYPE_MAP) {
        Item keys = js_object_keys(headers_item);
        int64_t nkeys = js_array_length(keys);
        for (int64_t i = 0; i < nkeys; i++) {
            Item k = js_array_get_int(keys, i);
            if (!http_client_validate_header_pair(k, js_property_get(headers_item, k))) {
                return false;
            }
        }
    } else if (get_type_id(headers_item) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(headers_item);
        for (int64_t i = 0; i < len; i++) {
            Item entry = js_array_get_int(headers_item, i);
            if (get_type_id(entry) == LMD_TYPE_ARRAY && js_array_length(entry) >= 2) {
                if (!http_client_validate_header_pair(js_array_get_int(entry, 0),
                                                      js_array_get_int(entry, 1))) {
                    return false;
                }
            } else if (i + 1 < len) {
                if (!http_client_validate_header_pair(entry, js_array_get_int(headers_item, i + 1))) {
                    return false;
                }
                i++;
            }
        }
    }
    return true;
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

static bool http_client_headers_have_name(Item headers_item, const char* name, int name_len) {
    if (get_type_id(headers_item) == LMD_TYPE_MAP) {
        Item keys = js_object_keys(headers_item);
        int64_t nkeys = js_array_length(keys);
        for (int64_t i = 0; i < nkeys; i++) {
            Item k = js_array_get_int(keys, i);
            if (get_type_id(k) == LMD_TYPE_STRING && http_header_name_equals(it2s(k), name, name_len)) {
                return true;
            }
        }
    } else if (get_type_id(headers_item) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(headers_item);
        for (int64_t i = 0; i < len; i++) {
            Item entry = js_array_get_int(headers_item, i);
            Item k = entry;
            if (get_type_id(entry) == LMD_TYPE_ARRAY && js_array_length(entry) >= 1) {
                k = js_array_get_int(entry, 0);
            }
            if (get_type_id(k) == LMD_TYPE_STRING && http_header_name_equals(it2s(k), name, name_len)) {
                return true;
            }
        }
    }
    return false;
}

static int http_client_append_basic_auth(char* req_str, int rlen, int cap, Item auth_item) {
    if (get_type_id(auth_item) != LMD_TYPE_STRING) return rlen;
    String* auth = it2s(auth_item);
    size_t out_len = base64_encoded_len(auth->len, BASE64_STD);
    char* out = (char*)mem_alloc(out_len + 1, MEM_CAT_JS_RUNTIME);
    size_t written = base64_encode((const uint8_t*)auth->chars, auth->len, out, BASE64_STD);
    out[written] = '\0';
    rlen += snprintf(req_str + rlen, cap - rlen, "Authorization: Basic %.*s\r\n",
                     (int)written, out);
    mem_free(out);
    return rlen;
}

static bool http_client_parse_port(Item port_item, int* out_port) {
    if (!out_port) return false;
    TypeId type = get_type_id(port_item);
    if (type == LMD_TYPE_INT) {
        int64_t port = it2i(port_item);
        if (port < 0 || port > 65535) return false;
        *out_port = (int)port;
        return true;
    }
    if (type == LMD_TYPE_FLOAT) {
        double port_num = it2d(port_item);
        int port = (int)port_num;
        if (port_num < 0 || port_num > 65535 || (double)port != port_num) return false;
        *out_port = port;
        return true;
    }
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(port_item);
        if (!s || s->len == 0 || s->len >= 64) return false;
        char buf[64];
        memcpy(buf, s->chars, s->len);
        buf[s->len] = '\0';
        char* end = NULL;
        long port = strtol(buf, &end, 10);
        if (end == buf || *end != '\0' || port < 0 || port > 65535) return false;
        *out_port = (int)port;
        return true;
    }
    return false;
}

static Item http_client_auth_from_url_parts(Item username_item, Item password_item) {
    if (get_type_id(username_item) != LMD_TYPE_STRING) return make_js_undefined();
    String* username = it2s(username_item);
    if (!username || username->len == 0) return make_js_undefined();

    String* password = NULL;
    if (get_type_id(password_item) == LMD_TYPE_STRING) password = it2s(password_item);

    char auth_buf[1024];
    int auth_len = 0;
    if (password && password->len > 0) {
        auth_len = snprintf(auth_buf, sizeof(auth_buf), "%.*s:%.*s",
                            (int)username->len, username->chars,
                            (int)password->len, password->chars);
    } else {
        auth_len = snprintf(auth_buf, sizeof(auth_buf), "%.*s",
                            (int)username->len, username->chars);
    }
    if (auth_len < 0) auth_len = 0;
    if (auth_len >= (int)sizeof(auth_buf)) auth_len = (int)sizeof(auth_buf) - 1;

    size_t decoded_len = 0;
    char* decoded = url_decode_component(auth_buf, (size_t)auth_len, &decoded_len);
    if (!decoded) return make_string_item(auth_buf, auth_len);
    Item result = make_string_item(decoded, (int)decoded_len);
    mem_free(decoded);
    return result;
}

static void http_client_copy_path_from_url_parts(Item options_item, char* path_buf, int path_size) {
    if (!path_buf || path_size <= 0 || get_type_id(options_item) != LMD_TYPE_MAP) return;
    Item pathname_item = js_property_get(options_item, make_string_item("pathname"));
    if (get_type_id(pathname_item) != LMD_TYPE_STRING) return;
    String* pathname = it2s(pathname_item);
    if (!pathname || pathname->len == 0) return;

    Item search_item = js_property_get(options_item, make_string_item("search"));
    String* search = get_type_id(search_item) == LMD_TYPE_STRING ? it2s(search_item) : NULL;
    int pos = 0;
    int copy_len = (int)pathname->len;
    if (copy_len > path_size - 1) copy_len = path_size - 1;
    memcpy(path_buf, pathname->chars, (size_t)copy_len);
    pos = copy_len;
    if (search && search->len > 0 && pos < path_size - 1) {
        int search_len = (int)search->len;
        if (search_len > path_size - 1 - pos) search_len = path_size - 1 - pos;
        memcpy(path_buf + pos, search->chars, (size_t)search_len);
        pos += search_len;
    }
    path_buf[pos] = '\0';
}

// http.request(options, callback)
extern "C" Item js_http_request(Item options_item, Item callback) {
    int port = 80;
    char host_buf[256] = "localhost";
    char method_buf[16] = "GET";
    char path_buf[4096] = "/";
    char socket_path[4096];
    socket_path[0] = '\0';
    bool use_pipe = false;

    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        Item spath = js_property_get(options_item, make_string_item("socketPath"));
        if (get_type_id(spath) == LMD_TYPE_STRING) {
            String* ss = it2s(spath);
            int len = (int)ss->len < (int)sizeof(socket_path) - 1 ?
                (int)ss->len : (int)sizeof(socket_path) - 1;
            memcpy(socket_path, ss->chars, (size_t)len);
            socket_path[len] = '\0';
            use_pipe = true;
        }
        Item p = js_property_get(options_item, make_string_item("port"));
        http_client_parse_port(p, &port);
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
            if (ms->len > 0) {
                int len = (int)ms->len < 15 ? (int)ms->len : 15;
                memcpy(method_buf, ms->chars, (size_t)len);
                method_buf[len] = '\0';
            }
        } else if (get_type_id(m) != LMD_TYPE_UNDEFINED && get_type_id(m) != LMD_TYPE_NULL) {
            // invalid methods must fail before socket allocation; otherwise mustNotCall callbacks can leak into live requests.
            return http_throw_invalid_method_type(m);
        }
        Item pa = js_property_get(options_item, make_string_item("path"));
        if (get_type_id(pa) == LMD_TYPE_STRING) {
            String* ps = it2s(pa);
            if (ps->len > 0) {
                int len = (int)ps->len < 4095 ? (int)ps->len : 4095;
                memcpy(path_buf, ps->chars, (size_t)len);
                path_buf[len] = '\0';
            }
        } else {
            http_client_copy_path_from_url_parts(options_item, path_buf, (int)sizeof(path_buf));
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
    Item auth_item = make_js_undefined();
    Item signal_item = make_js_undefined();
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
        auth_item = js_property_get(options_item, make_string_item("auth"));
        signal_item = js_property_get(options_item, make_string_item("signal"));
        TypeId auth_type = get_type_id(auth_item);
        if (auth_type == LMD_TYPE_UNDEFINED || auth_type == LMD_TYPE_NULL) {
            auth_item = http_client_auth_from_url_parts(
                js_property_get(options_item, make_string_item("username")),
                js_property_get(options_item, make_string_item("password")));
        }
    }
    if (!http_client_validate_headers(custom_headers)) return ItemNull;

    int rlen = snprintf(req_str, sizeof(req_str),
        "%s %s HTTP/1.1\r\n", method_buf, path_buf);

    bool has_content_length = false;
    bool explicit_content_length = http_client_headers_have_name(custom_headers, "content-length", 14);
    bool has_headers_array = get_type_id(custom_headers) == LMD_TYPE_ARRAY;
    bool has_connection_header = http_client_headers_have_name(custom_headers, "connection", 10);
    bool has_expect_header = http_client_headers_have_name(custom_headers, "expect", 6);
    bool has_transfer_encoding = http_client_headers_have_name(custom_headers, "transfer-encoding", 17);
    bool use_chunked_request_body = !explicit_content_length && (has_expect_header || has_transfer_encoding);

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

    if (set_default_headers && !has_connection_header) {
        rlen += snprintf(req_str + rlen, sizeof(req_str) - rlen, "Connection: keep-alive\r\n");
    }

    rlen = http_client_append_headers(req_str, rlen, (int)sizeof(req_str),
                                      custom_headers, &has_content_length);

    if (!has_headers_array &&
        get_type_id(auth_item) == LMD_TYPE_STRING &&
        !http_client_headers_have_name(custom_headers, "authorization", 13)) {
        rlen = http_client_append_basic_auth(req_str, rlen, (int)sizeof(req_str), auth_item);
    }

    if (has_expect_header && !explicit_content_length && !has_transfer_encoding) {
        rlen += snprintf(req_str + rlen, sizeof(req_str) - rlen, "Transfer-Encoding: chunked\r\n");
    }

    rlen += snprintf(req_str + rlen, sizeof(req_str) - rlen, "\r\n");

    // create client request
    JsHttpClientReq* creq = (JsHttpClientReq*)mem_calloc(1, sizeof(JsHttpClientReq), MEM_CAT_JS_RUNTIME);
    creq->is_pipe = use_pipe;
    if (use_pipe) {
        uv_pipe_init(loop, &creq->pipe, 0);
        creq->pipe.data = creq;
    } else {
        uv_tcp_init(loop, &creq->tcp);
        creq->tcp.data = creq;
    }
    creq->callback = callback;
    creq->als_context = js_als_capture_context();
    creq->async_resource = js_async_hooks_create_resource("HTTPCLIENTREQUEST", 17);
    creq->recv_cap = 16384;
    creq->recv_buf = (char*)mem_alloc(creq->recv_cap, MEM_CAT_JS_RUNTIME);

    // copy request buffer
    creq->send_len = rlen;
    creq->send_head_len = rlen;
    creq->has_content_length = has_content_length;
    creq->content_length_explicit = explicit_content_length;
    creq->request_chunked_body = use_chunked_request_body;
    snprintf(creq->method, sizeof(creq->method), "%s", method_buf);
    creq->send_buf = (char*)mem_alloc(rlen, MEM_CAT_JS_RUNTIME);
    memcpy(creq->send_buf, req_str, rlen);

    Item obj = http_client_make_request_object(creq, method_buf, path_buf);

    creq->js_object = obj;
    http_client_sync_header_property(creq, obj);
    http_client_metadata_from_headers(obj, custom_headers);
    if (set_host && !http_client_headers_have_name(custom_headers, "host", 4)) {
        char host_meta[320];
        int host_meta_len = 0;
        if (port == 80) {
            host_meta_len = snprintf(host_meta, sizeof(host_meta), "%s", host_buf);
        } else {
            host_meta_len = snprintf(host_meta, sizeof(host_meta), "%s:%d", host_buf, port);
        }
        if (host_meta_len < 0) host_meta_len = 0;
        if (host_meta_len >= (int)sizeof(host_meta)) host_meta_len = (int)sizeof(host_meta) - 1;
        http_client_metadata_set(obj, make_string_item("Host"), make_string_item(host_meta, host_meta_len));
    }
    bool abort_before_connect = false;
    TypeId signal_type = get_type_id(signal_item);
    if (signal_item.item != 0 &&
        signal_type != LMD_TYPE_UNDEFINED &&
        signal_type != LMD_TYPE_NULL) {
        abort_before_connect = js_http_client_configure_abort_signal(creq, signal_item);
    }
    if (abort_before_connect) return obj;

    if (!js_permission_has_net()) {
        // permission denial must happen before uv_connect; otherwise the denied
        // request leaves an idle handle that waits for the drain watchdog.
        Item err = js_permission_make_net_error("connect", host_buf);
        Item bound_args[2] = { obj, err };
        Item tick = js_bind_function(js_new_function((void*)js_http_agent_socket_error_tick, 2),
                                     make_js_undefined(), bound_args, 2);
        js_next_tick_enqueue(tick);
        return obj;
    }

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

    uv_connect_t* conn = (uv_connect_t*)mem_calloc(1, sizeof(uv_connect_t), MEM_CAT_JS_RUNTIME);
    conn->data = creq;

    int r = 0;
    if (use_pipe) {
        uv_pipe_connect(conn, &creq->pipe, socket_path, http_client_connect_cb);
    } else {
        struct sockaddr_in addr;
        const char* connect_host = strcmp(host_buf, "localhost") == 0 ? "127.0.0.1" : host_buf;
        uv_ip4_addr(connect_host, port, &addr);
        r = uv_tcp_connect(conn, &creq->tcp, (const struct sockaddr*)&addr, http_client_connect_cb);
    }
    if (r != 0) {
        log_error("http: request connect failed: %s", uv_strerror(r));
        mem_free(conn);
        creq->destroyed = true;
        js_property_set(obj, make_string_item("destroyed"), (Item){.item = b2it(true)});
        js_http_close_client_req(creq);
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
        400,401,403,404,405,406,408,409,410,411,413,414,415,416,417,422,429,
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
extern "C" Item js_http_agent_createConnection(Item options, Item callback) {
    Item args = js_array_new(0);
    js_array_push(args, options);
    if (get_type_id(callback) == LMD_TYPE_FUNC) js_array_push(args, callback);
    return js_net_createConnection(args);
}

extern "C" Item js_http_ClientRequest(Item options_item, Item callback) {
    return js_http_request(options_item, callback);
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
        js_new_function((void*)js_http_agent_createConnection, 2));

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

static Item http_constructor_prototype(Item ctor, JsClass cls) {
    Item proto = js_property_get(ctor, make_string_item("prototype"));
    if (get_type_id(proto) != LMD_TYPE_MAP) {
        proto = js_new_object();
        js_property_set(ctor, make_string_item("prototype"), proto);
    }
    js_class_stamp(proto, cls);
    js_property_set(proto, make_string_item("constructor"), ctor);
    js_mark_non_enumerable(proto, make_string_item("constructor"));
    if (get_type_id(ctor) == LMD_TYPE_FUNC) {
        js_function_set_prototype(ctor, proto);
    }
    return proto;
}

extern "C" Item js_get_http_namespace(void) {
    if (http_namespace.item != 0) return http_namespace;

    http_namespace = js_new_object();

    http_set_method(http_namespace, "createServer", (void*)js_http_createServer, 2);
    http_set_method(http_namespace, "request",      (void*)js_http_request, 2);
    http_set_method(http_namespace, "get",           (void*)js_http_get, 2);

    // Server — alias for createServer (Node.js allows http.Server(cb))
    Item server_fn = js_new_function((void*)js_http_createServer, 2);
    js_property_set(http_namespace, make_string_item("Server"), server_fn);
    http_server_prototype = http_constructor_prototype(server_fn, JS_CLASS_SERVER);

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
    Item incoming_fn = js_new_function((void*)js_http_stub_ctor, 0);
    js_property_set(http_namespace, make_string_item("IncomingMessage"), incoming_fn);
    http_incoming_message_prototype =
        http_constructor_prototype(incoming_fn, JS_CLASS_INCOMING_MESSAGE);
    Item outgoing_fn = js_new_function((void*)js_http_stub_ctor, 0);
    js_property_set(http_namespace, make_string_item("OutgoingMessage"), outgoing_fn);
    http_outgoing_message_prototype =
        http_constructor_prototype(outgoing_fn, JS_CLASS_OBJECT);
    Item response_fn = js_new_function((void*)js_http_stub_ctor, 0);
    js_property_set(http_namespace, make_string_item("ServerResponse"), response_fn);
    http_server_response_prototype =
        http_constructor_prototype(response_fn, JS_CLASS_SERVER_RESPONSE);
    if (get_type_id(http_outgoing_message_prototype) == LMD_TYPE_MAP) {
        js_set_prototype(http_server_response_prototype, http_outgoing_message_prototype);
    }
    http_set_method(http_namespace, "ClientRequest",   (void*)js_http_ClientRequest, 2);

    Item default_key = make_string_item("default");
    js_property_set(http_namespace, default_key, http_namespace);

    return http_namespace;
}

extern "C" void js_http_reset(void) {
    http_namespace = (Item){0};
}
